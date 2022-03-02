#include <windows.h>
#include "my_string.h"

/*
features:
- move around the file with arrow keys
- insert/delete characters at current cursor position
- ability to save and quit from the program

later on:
- highlight text and be able to copy/cut/delete it

*/
#define BUF_SIZE 65536

#define ESC "\x1b"
#define CSI "\x1b["
#define CR 0xD
#define LF 0xA

#define CTRL_KEY(k) ((k) & 0x1f)

typedef struct row {
    char *buf;
    size_t size;
} row_t;

row_t *rows; /* the rows */
size_t nrows = 0; /* number of rows */

size_t row_offset = 0, col_offset = 0; /*  */


HANDLE hstdin, con, dbcon;
CONSOLE_SCREEN_BUFFER_INFO csbi; /* holds coordinates of cursor as well as console size */
CONSOLE_CURSOR_INFO cci; /* for cursor visibility */

DWORD read, written;
DWORD old_mode, new_mode;

BOOL ctrl_event_proc(DWORD type) {
    return TRUE;
}

void cls() {
    FillConsoleOutputCharacterA(con, ' ', csbi.dwSize.X * csbi.dwSize.Y, (COORD){0, 0}, &written);
}

void hide_cursor() {
    cci.bVisible = FALSE;
    SetConsoleCursorInfo(con, &cci);
}

void show_cursor() {
    cci.bVisible = TRUE;
    SetConsoleCursorInfo(con, &cci);
}

void scroll() {
    if (csbi.dwCursorPosition.Y < row_offset)
        row_offset = csbi.dwCursorPosition.Y;
    if (csbi.dwCursorPosition.Y >= row_offset + csbi.dwSize.Y)
        row_offset = csbi.dwCursorPosition.Y - csbi.dwSize.Y + 1;
    
    if (csbi.dwCursorPosition.X < col_offset)
        col_offset = csbi.dwCursorPosition.X;
    if (csbi.dwCursorPosition.X >= col_offset + csbi.dwSize.X)
        col_offset = csbi.dwCursorPosition.X - csbi.dwSize.X + 1;
}

/* rewrite this to use 1 write call */
/* implement double buffering read/writeconsoleoutput apis */
void refresh() {
    scroll();
    //hide_cursor();
    string_t buf = STRING_INIT;
    
    int y;
    for (y = 0; y < csbi.dwSize.Y; ++y) {
        size_t filerow = y + row_offset;
        FillConsoleOutputCharacterA(dbcon, ' ', csbi.dwSize.X, (COORD){0, y}, &written);
        if (filerow >= nrows) {
            str_append(&buf, "~", 1);
        } else {
            int len = rows[filerow].size - col_offset;
            if (len < 0) len = 0;
            if (len > csbi.dwSize.X) len = csbi.dwSize.X;
            str_append(&buf, rows[filerow].buf + col_offset, len);
        }
        
        if (y < csbi.dwSize.Y - 1) str_append(&buf, "\r\n", 2);
    }

    SetConsoleCursorPosition(dbcon, (COORD){0, 0});
    WriteConsoleA(dbcon, buf.buf, buf.len, &written, NULL);
    
    CHAR_INFO *ci = (CHAR_INFO*)malloc(csbi.dwSize.X * csbi.dwSize.Y * sizeof(CHAR_INFO));
    SMALL_RECT sr;
    sr.Top = 0;
    sr.Left = 0;
    sr.Bottom = csbi.dwSize.Y - 1;
    sr.Right = csbi.dwSize.X - 1;
    ReadConsoleOutputA(dbcon, ci, (COORD){csbi.dwSize.X, csbi.dwSize.Y}, (COORD){0, 0}, &sr);
    WriteConsoleOutputA(con, ci, (COORD){csbi.dwSize.X, csbi.dwSize.Y}, (COORD){0, 0}, &sr);
    SetConsoleCursorPosition(con, (COORD){csbi.dwCursorPosition.X - col_offset, csbi.dwCursorPosition.Y - row_offset});
    //show_cursor();
}

void read_key_event() {
    INPUT_RECORD ir;
    KEY_EVENT_RECORD ker;
    if (!ReadConsoleInputA(hstdin, &ir, 1, &read)) { exit(1); }
    if (ir.EventType != KEY_EVENT) return;
    ker = ir.Event.KeyEvent;
    if (!ker.bKeyDown) return;
    //printf("%d | %d\n", ker.uChar.AsciiChar, ker.wVirtualKeyCode);
    row_t *r = (csbi.dwCursorPosition.Y >= nrows) ? NULL : &rows[csbi.dwCursorPosition.Y];
    switch (ker.uChar.AsciiChar) {
        case CTRL_KEY('q'):
            cls();
            exit(0);
            break;
        case 0:
            switch (ker.wVirtualKeyCode) {
                case VK_LEFT:
                    if (csbi.dwCursorPosition.X != 0) csbi.dwCursorPosition.X--;
                    break;
                case VK_RIGHT:
                    if (r && csbi.dwCursorPosition.X < r->size) csbi.dwCursorPosition.X++;
                    break;
                case VK_UP:
                    if (csbi.dwCursorPosition.Y != 0) csbi.dwCursorPosition.Y--;
                    break;
                case VK_DOWN:
                    if (csbi.dwCursorPosition.Y < nrows) csbi.dwCursorPosition.Y++;
                    break;
                case VK_PRIOR: csbi.dwCursorPosition.Y = 0; break;
                case VK_NEXT: csbi.dwCursorPosition.Y = csbi.dwSize.Y - 1; break;
                case VK_HOME: csbi.dwCursorPosition.X = 0; break;
                case VK_END: csbi.dwCursorPosition.X = csbi.dwSize.X - 1; break;
            }
            r = (csbi.dwCursorPosition.Y >= nrows) ? NULL : &rows[csbi.dwCursorPosition.Y];
            size_t rowlen = r ? r->size : 0;
            if (csbi.dwCursorPosition.X > rowlen) csbi.dwCursorPosition.X = rowlen;


            break;
    }
}

char *getline(ssize_t *size, FILE *stream) {
	int c, growby = 80;
    ssize_t i = 0, linebufsize = 0;
	char *linebuf = NULL;

	while (1) {
		c = fgetc(stream);
		if (c == EOF) break;
		while (i > linebufsize - 2)
			linebuf = (char*)realloc(linebuf, linebufsize += growby);
        linebuf[i++] = (char)c;
        if (c == '\n' || c == '\0') break;
        
	}
	if (i == 0) return NULL;
	linebuf[i] = 0;
    *size = i;
	return linebuf;
}

void append_row(char *s, size_t len) {
    rows = (row_t*)realloc(rows, sizeof(row_t) * (nrows + 1));
    size_t last_row = nrows;

    rows[last_row].size = len;
    rows[last_row].buf = (char*)malloc(len + 1);
    memcpy(rows[last_row].buf, s, len);
    rows[last_row].buf[len] = '\0';
    nrows++;
}

void open_file(char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) exit(1);

    char *line = NULL;
    ssize_t linelen = 0;

    while ((line = getline(&linelen, fp)) != NULL) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            line[linelen - 1] = '\0';
            linelen--;
        }
        append_row(line, linelen);
        printf("[%d] %s\n", linelen, line);
    }
}

int main(int argc, char **argv) {
    hstdin = GetStdHandle(STD_INPUT_HANDLE);
    con = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        CONSOLE_TEXTMODE_BUFFER, NULL);
    /* double buffer console */
    dbcon = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        CONSOLE_TEXTMODE_BUFFER, NULL);
    SetConsoleActiveScreenBuffer(con);
    
    SetConsoleTitleA("No file");

    GetConsoleMode(con, &old_mode);
    new_mode = old_mode | ~0x0200 | ~ENABLE_LINE_INPUT | ~ENABLE_PROCESSED_INPUT;
    SetConsoleMode(con, new_mode);
    SetConsoleCtrlHandler(ctrl_event_proc, TRUE);

    GetConsoleScreenBufferInfo(con, &csbi);
    GetConsoleCursorInfo(con, &cci);

    cci.dwSize = 100;
    SetConsoleCursorInfo(con, &cci);

    SetConsoleCursorPosition(con, (COORD){0, 0});
    
    if (argc >= 2)
        open_file(argv[1]);

    while (1) {
        refresh();
        read_key_event();
        //GetConsoleScreenBufferInfo(con, &csbi);
    }

    SetConsoleMode(con, old_mode);
}