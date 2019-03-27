/****************************************************************************
 * Copyright (c) 1998-2013,2014 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Juergen Pfeifer                                                 *
 ****************************************************************************/

/*
 * TODO - GetMousePos(POINT * result) from ntconio.c
 * TODO - implement nodelay
 * TODO - when $NCGDB is set, implement non-buffered output, like PDCurses
 */

#include <curses.priv.h>
#define CUR my_term.type.

MODULE_ID("$Id: win_driver.c,v 1.24 2014/02/23 01:23:29 tom Exp $")

#define WINMAGIC NCDRV_MAGIC(NCDRV_WINCONSOLE)

#define EXP_OPTIMIZE 0

#define okConsoleHandle(TCB) (TCB != 0 && !InvalidConsoleHandle(TCB->hdl))

#define AssertTCB() assert(TCB != 0 && (TCB->magic == WINMAGIC))
#define SetSP()     assert(TCB->csp != 0); sp = TCB->csp; (void) sp

#define GenMap(vKey,key) MAKELONG(key, vKey)

#define AdjustY(p) ((p)->buffered ? 0 : (int) (p)->SBI.srWindow.Top)

static const LONG keylist[] =
{
    GenMap(VK_PRIOR, KEY_PPAGE),
    GenMap(VK_NEXT, KEY_NPAGE),
    GenMap(VK_END, KEY_END),
    GenMap(VK_HOME, KEY_HOME),
    GenMap(VK_LEFT, KEY_LEFT),
    GenMap(VK_UP, KEY_UP),
    GenMap(VK_RIGHT, KEY_RIGHT),
    GenMap(VK_DOWN, KEY_DOWN),
    GenMap(VK_DELETE, KEY_DC),
    GenMap(VK_INSERT, KEY_IC)
};
#define N_INI ((int)(sizeof(keylist)/sizeof(keylist[0])))
#define FKEYS 24
#define MAPSIZE (FKEYS + N_INI)
#define NUMPAIRS 64

typedef struct props {
    CONSOLE_SCREEN_BUFFER_INFO SBI;
    bool progMode;
    TERM_HANDLE lastOut;
    DWORD map[MAPSIZE];
    DWORD rmap[MAPSIZE];
    WORD pairs[NUMPAIRS];
    bool buffered;
    COORD origin;
    CHAR_INFO *save_screen;
} Properties;

#define PropOf(TCB) ((Properties*)TCB->prop)

int
_nc_mingw_ioctl(int fd GCC_UNUSED,
		long int request GCC_UNUSED,
		struct termios *arg GCC_UNUSED)
{
    return 0;
    endwin();
    fprintf(stderr, "TERMINFO currently not supported on Windows.\n");
    exit(1);
}

static WORD
MapColor(bool fore, int color)
{
    static const int _cmap[] =
    {0, 4, 2, 6, 1, 5, 3, 7};
    int a;
    if (color < 0 || color > 7)
	a = fore ? 7 : 0;
    else
	a = _cmap[color];
    if (!fore)
	a = a << 4;
    return (WORD) a;
}

static WORD
MapAttr(TERMINAL_CONTROL_BLOCK * TCB, WORD res, attr_t ch)
{
    if (ch & A_COLOR) {
	int p;
	SCREEN *sp;

	AssertTCB();
	SetSP();
	p = PairNumber(ch);
	if (p > 0 && p < NUMPAIRS && TCB != 0 && sp != 0) {
	    WORD a;
	    a = PropOf(TCB)->pairs[p];
	    res = (res & 0xff00) | a;
	}
    }

    if (ch & A_REVERSE)
	res = ((res & 0xff00) | (((res & 0x07) << 4) | ((res & 0x70) >> 4)));

    if (ch & A_STANDOUT)
	res = ((res & 0xff00) | (((res & 0x07) << 4) | ((res & 0x70) >> 4))
	       | BACKGROUND_INTENSITY);

    if (ch & A_BOLD)
	res |= FOREGROUND_INTENSITY;

    if (ch & A_DIM)
	res |= BACKGROUND_INTENSITY;

    return res;
}

#if USE_WIDEC_SUPPORT
/*
 * TODO: support surrogate pairs
 * TODO: support combining characters
 * TODO: support acsc
 * TODO: check wcwidth of base character, fill if needed for double-width
 * TODO: _nc_wacs should be part of sp.
 */
static BOOL
con_write16(TERMINAL_CONTROL_BLOCK * TCB, int y, int x, cchar_t *str, int limit)
{
    int actual = 0;
    CHAR_INFO ci[limit];
    COORD loc, siz;
    SMALL_RECT rec;
    int i;
    cchar_t ch;
    SCREEN *sp;
    Properties *p = PropOf(TCB);

    AssertTCB();

    SetSP();

    for (i = actual = 0; i < limit; i++) {
	ch = str[i];
	if (isWidecExt(ch))
	    continue;
	ci[actual].Char.UnicodeChar = CharOf(ch);
	ci[actual].Attributes = MapAttr(TCB,
					PropOf(TCB)->SBI.wAttributes,
					AttrOf(ch));
	if (AttrOf(ch) & A_ALTCHARSET) {
	    if (_nc_wacs) {
		int which = CharOf(ch);
		if (which > 0
		    && which < ACS_LEN
		    && CharOf(_nc_wacs[which]) != 0) {
		    ci[actual].Char.UnicodeChar = CharOf(_nc_wacs[which]);
		} else {
		    ci[actual].Char.UnicodeChar = ' ';
		}
	    }
	}
	++actual;
    }

    loc.X = (short) 0;
    loc.Y = (short) 0;
    siz.X = (short) actual;
    siz.Y = 1;

    rec.Left = (short) x;
    rec.Top = (SHORT) (y + AdjustY(p));
    rec.Right = (short) (x + limit - 1);
    rec.Bottom = rec.Top;

    return WriteConsoleOutputW(TCB->hdl, ci, siz, loc, &rec);
}
#define con_write(tcb, y, x, str, n) con_write16(tcb, y, x, str, n)
#else
static BOOL
con_write8(TERMINAL_CONTROL_BLOCK * TCB, int y, int x, chtype *str, int n)
{
    CHAR_INFO ci[n];
    COORD loc, siz;
    SMALL_RECT rec;
    int i;
    chtype ch;
    SCREEN *sp;

    AssertTCB();

    SetSP();

    for (i = 0; i < n; i++) {
	ch = str[i];
	ci[i].Char.AsciiChar = ChCharOf(ch);
	ci[i].Attributes = MapAttr(TCB,
				   PropOf(TCB)->SBI.wAttributes,
				   ChAttrOf(ch));
	if (ChAttrOf(ch) & A_ALTCHARSET) {
	    if (sp->_acs_map)
		ci[i].Char.AsciiChar =
		ChCharOf(NCURSES_SP_NAME(_nc_acs_char) (sp, ChCharOf(ch)));
	}
    }

    loc.X = (short) 0;
    loc.Y = (short) 0;
    siz.X = (short) n;
    siz.Y = 1;

    rec.Left = (short) x;
    rec.Top = (short) y;
    rec.Right = (short) (x + n - 1);
    rec.Bottom = rec.Top;

    return WriteConsoleOutput(TCB->hdl, ci, siz, loc, &rec);
}
#define con_write(tcb, y, x, str, n) con_write8(tcb, y, x, str, n)
#endif

#if EXP_OPTIMIZE
/*
 * Comparing new/current screens, determine the last column-index for a change
 * beginning on the given row,col position.  Unlike a serial terminal, there is
 * no cost for "moving" the "cursor" on the line as we update it.
 */
static int
find_end_of_change(SCREEN *sp, int row, int col)
{
    int result = col;
    struct ldat *curdat = CurScreen(sp)->_line + row;
    struct ldat *newdat = NewScreen(sp)->_line + row;

    while (col <= newdat->lastchar) {
#if USE_WIDEC_SUPPORT
	if (isWidecExt(curdat->text[col]) || isWidecExt(newdat->text[col])) {
	    result = col;
	} else if (memcmp(&curdat->text[col],
			  &newdat->text[col],
			  sizeof(curdat->text[0]))) {
	    result = col;
	} else {
	    break;
	}
#else
	if (curdat->text[col] != newdat->text[col]) {
	    result = col;
	} else {
	    break;
	}
#endif
	++col;
    }
    return result;
}

/*
 * Given a row,col position at the end of a change-chunk, look for the
 * beginning of the next change-chunk.
 */
static int
find_next_change(SCREEN *sp, int row, int col)
{
    struct ldat *curdat = CurScreen(sp)->_line + row;
    struct ldat *newdat = NewScreen(sp)->_line + row;
    int result = newdat->lastchar + 1;

    while (++col <= newdat->lastchar) {
#if USE_WIDEC_SUPPORT
	if (isWidecExt(curdat->text[col]) != isWidecExt(newdat->text[col])) {
	    result = col;
	    break;
	} else if (memcmp(&curdat->text[col],
			  &newdat->text[col],
			  sizeof(curdat->text[0]))) {
	    result = col;
	    break;
	}
#else
	if (curdat->text[col] != newdat->text[col]) {
	    result = col;
	    break;
	}
#endif
    }
    return result;
}

#define EndChange(first) \
	find_end_of_change(sp, y, first)
#define NextChange(last) \
	find_next_change(sp, y, last)

#endif /* EXP_OPTIMIZE */

#define MARK_NOCHANGE(win,row) \
		win->_line[row].firstchar = _NOCHANGE; \
		win->_line[row].lastchar  = _NOCHANGE

static void
selectActiveHandle(TERMINAL_CONTROL_BLOCK * TCB)
{
    if (PropOf(TCB)->lastOut != TCB->hdl) {
	PropOf(TCB)->lastOut = TCB->hdl;
	SetConsoleActiveScreenBuffer(PropOf(TCB)->lastOut);
    }
}

static int
drv_doupdate(TERMINAL_CONTROL_BLOCK * TCB)
{
    int result = ERR;
    int y, nonempty, n, x0, x1, Width, Height;
    SCREEN *sp;

    AssertTCB();
    SetSP();

    T((T_CALLED("win32con::drv_doupdate(%p)"), TCB));
    if (okConsoleHandle(TCB)) {

	Width = screen_columns(sp);
	Height = screen_lines(sp);
	nonempty = min(Height, NewScreen(sp)->_maxy + 1);

	if ((CurScreen(sp)->_clear || NewScreen(sp)->_clear)) {
	    int x;
#if USE_WIDEC_SUPPORT
	    cchar_t empty[Width];
	    wchar_t blank[2] =
	    {
		L' ', L'\0'
	    };

	    for (x = 0; x < Width; x++)
		setcchar(&empty[x], blank, 0, 0, 0);
#else
	    chtype empty[Width];

	    for (x = 0; x < Width; x++)
		empty[x] = ' ';
#endif

	    for (y = 0; y < nonempty; y++) {
		con_write(TCB, y, 0, empty, Width);
		memcpy(empty,
		       CurScreen(sp)->_line[y].text,
		       (size_t) Width * sizeof(empty[0]));
	    }
	    CurScreen(sp)->_clear = FALSE;
	    NewScreen(sp)->_clear = FALSE;
	    touchwin(NewScreen(sp));
	}

	for (y = 0; y < nonempty; y++) {
	    x0 = NewScreen(sp)->_line[y].firstchar;
	    if (x0 != _NOCHANGE) {
#if EXP_OPTIMIZE
		int x2;
		int limit = NewScreen(sp)->_line[y].lastchar;
		while ((x1 = EndChange(x0)) <= limit) {
		    while ((x2 = NextChange(x1)) <= limit && x2 <= (x1 + 2)) {
			x1 = x2;
		    }
		    n = x1 - x0 + 1;
		    memcpy(&CurScreen(sp)->_line[y].text[x0],
			   &NewScreen(sp)->_line[y].text[x0],
			   n * sizeof(CurScreen(sp)->_line[y].text[x0]));
		    con_write(TCB,
			      y,
			      x0,
			      &CurScreen(sp)->_line[y].text[x0], n);
		    x0 = NextChange(x1);
		}

		/* mark line changed successfully */
		if (y <= NewScreen(sp)->_maxy) {
		    MARK_NOCHANGE(NewScreen(sp), y);
		}
		if (y <= CurScreen(sp)->_maxy) {
		    MARK_NOCHANGE(CurScreen(sp), y);
		}
#else
		x1 = NewScreen(sp)->_line[y].lastchar;
		n = x1 - x0 + 1;
		if (n > 0) {
		    memcpy(&CurScreen(sp)->_line[y].text[x0],
			   &NewScreen(sp)->_line[y].text[x0],
			   (size_t) n * sizeof(CurScreen(sp)->_line[y].text[x0]));
		    con_write(TCB,
			      y,
			      x0,
			      &CurScreen(sp)->_line[y].text[x0], n);

		    /* mark line changed successfully */
		    if (y <= NewScreen(sp)->_maxy) {
			MARK_NOCHANGE(NewScreen(sp), y);
		    }
		    if (y <= CurScreen(sp)->_maxy) {
			MARK_NOCHANGE(CurScreen(sp), y);
		    }
		}
#endif
	    }
	}

	/* put everything back in sync */
	for (y = nonempty; y <= NewScreen(sp)->_maxy; y++) {
	    MARK_NOCHANGE(NewScreen(sp), y);
	}
	for (y = nonempty; y <= CurScreen(sp)->_maxy; y++) {
	    MARK_NOCHANGE(CurScreen(sp), y);
	}

	if (!NewScreen(sp)->_leaveok) {
	    CurScreen(sp)->_curx = NewScreen(sp)->_curx;
	    CurScreen(sp)->_cury = NewScreen(sp)->_cury;

	    TCB->drv->hwcur(TCB,
			    0, 0,
			    CurScreen(sp)->_cury, CurScreen(sp)->_curx);
	}
	selectActiveHandle(TCB);
	result = OK;
    }
    returnCode(result);
}

static bool
drv_CanHandle(TERMINAL_CONTROL_BLOCK * TCB,
	      const char *tname,
	      int *errret GCC_UNUSED)
{
    bool code = FALSE;

    T((T_CALLED("win32con::drv_CanHandle(%p)"), TCB));

    assert(TCB != 0);
    assert(tname != 0);

    TCB->magic = WINMAGIC;
    if (*tname == 0 || *tname == 0 || *tname == '#') {
	code = TRUE;
    } else {
	TERMINAL my_term;
	int status;

	code = FALSE;
#if (NCURSES_USE_DATABASE || NCURSES_USE_TERMCAP)
	status = _nc_setup_tinfo(tname, &my_term.type);
#else
	status = TGETENT_NO;
#endif
	if (status != TGETENT_YES) {
	    const TERMTYPE *fallback = _nc_fallback(tname);

	    if (fallback) {
		my_term.type = *fallback;
		status = TGETENT_YES;
	    } else if (!strcmp(tname, "unknown")) {
		code = TRUE;
	    }
	}
	if (status == TGETENT_YES) {
	    if (generic_type || hard_copy)
		code = TRUE;
	}
    }

    if (code) {
	if ((TCB->term.type.Booleans) == 0) {
	    _nc_init_termtype(&(TCB->term.type));
	}
    }

    returnBool(code);
}

static int
drv_dobeepflash(TERMINAL_CONTROL_BLOCK * TCB,
		int beepFlag GCC_UNUSED)
{
    SCREEN *sp;
    int res = ERR;

    AssertTCB();
    SetSP();

    return res;
}

static int
drv_print(TERMINAL_CONTROL_BLOCK * TCB,
	  char *data GCC_UNUSED,
	  int len GCC_UNUSED)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();

    return ERR;
}

static int
drv_defaultcolors(TERMINAL_CONTROL_BLOCK * TCB,
		  int fg GCC_UNUSED,
		  int bg GCC_UNUSED)
{
    SCREEN *sp;
    int code = ERR;

    AssertTCB();
    SetSP();

    return (code);
}

static bool
get_SBI(TERMINAL_CONTROL_BLOCK * TCB)
{
    bool rc = FALSE;
    Properties *p = PropOf(TCB);
    if (GetConsoleScreenBufferInfo(TCB->hdl, &(p->SBI))) {
	T(("GetConsoleScreenBufferInfo"));
	T(("... buffer(X:%d Y:%d)",
	   p->SBI.dwSize.X,
	   p->SBI.dwSize.Y));
	T(("... window(X:%d Y:%d)",
	   p->SBI.dwMaximumWindowSize.X,
	   p->SBI.dwMaximumWindowSize.Y));
	T(("... cursor(X:%d Y:%d)",
	   p->SBI.dwCursorPosition.X,
	   p->SBI.dwCursorPosition.Y));
	T(("... display(Top:%d Bottom:%d Left:%d Right:%d)",
	   p->SBI.srWindow.Top,
	   p->SBI.srWindow.Bottom,
	   p->SBI.srWindow.Left,
	   p->SBI.srWindow.Right));
	if (p->buffered) {
	    p->origin.X = 0;
	    p->origin.Y = 0;
	} else {
	    p->origin.X = p->SBI.srWindow.Left;
	    p->origin.Y = p->SBI.srWindow.Top;
	}
	rc = TRUE;
    } else {
	T(("GetConsoleScreenBufferInfo ERR"));
    }
    return rc;
}

static void
drv_setcolor(TERMINAL_CONTROL_BLOCK * TCB,
	     int fore,
	     int color,
	     int (*outc) (SCREEN *, int) GCC_UNUSED)
{
    AssertTCB();

    if (okConsoleHandle(TCB) &&
	PropOf(TCB) != 0) {
	WORD a = MapColor(fore, color);
	a |= (WORD) ((PropOf(TCB)->SBI.wAttributes) & (fore ? 0xfff8 : 0xff8f));
	SetConsoleTextAttribute(TCB->hdl, a);
	get_SBI(TCB);
    }
}

static bool
drv_rescol(TERMINAL_CONTROL_BLOCK * TCB)
{
    bool res = FALSE;

    AssertTCB();
    if (okConsoleHandle(TCB)) {
	WORD a = FOREGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN;
	SetConsoleTextAttribute(TCB->hdl, a);
	get_SBI(TCB);
	res = TRUE;
    }
    return res;
}

static bool
drv_rescolors(TERMINAL_CONTROL_BLOCK * TCB)
{
    int result = FALSE;
    SCREEN *sp;

    AssertTCB();
    SetSP();

    return result;
}

static int
drv_size(TERMINAL_CONTROL_BLOCK * TCB, int *Lines, int *Cols)
{
    int result = ERR;

    AssertTCB();

    T((T_CALLED("win32con::drv_size(%p)"), TCB));

    if (okConsoleHandle(TCB) &&
	PropOf(TCB) != 0 &&
	Lines != NULL &&
	Cols != NULL) {
	if (PropOf(TCB)->buffered) {
	    *Lines = (int) (PropOf(TCB)->SBI.dwSize.Y);
	    *Cols = (int) (PropOf(TCB)->SBI.dwSize.X);
	} else {
	    *Lines = (int) (PropOf(TCB)->SBI.srWindow.Bottom + 1 -
			    PropOf(TCB)->SBI.srWindow.Top);
	    *Cols = (int) (PropOf(TCB)->SBI.srWindow.Right + 1 -
			   PropOf(TCB)->SBI.srWindow.Left);
	}
	result = OK;
    }
    returnCode(result);
}

static int
drv_setsize(TERMINAL_CONTROL_BLOCK * TCB GCC_UNUSED,
	    int l GCC_UNUSED,
	    int c GCC_UNUSED)
{
    AssertTCB();
    return ERR;
}

static int
drv_sgmode(TERMINAL_CONTROL_BLOCK * TCB, int setFlag, TTY * buf)
{
    DWORD dwFlag = 0;
    tcflag_t iflag;
    tcflag_t lflag;

    AssertTCB();

    if (TCB == 0 || buf == NULL)
	return ERR;

    if (setFlag) {
	iflag = buf->c_iflag;
	lflag = buf->c_lflag;

	GetConsoleMode(TCB->inp, &dwFlag);

	if (lflag & ICANON)
	    dwFlag |= ENABLE_LINE_INPUT;
	else
	    dwFlag &= (DWORD) (~ENABLE_LINE_INPUT);

	if (lflag & ECHO)
	    dwFlag |= ENABLE_ECHO_INPUT;
	else
	    dwFlag &= (DWORD) (~ENABLE_ECHO_INPUT);

	if (iflag & BRKINT)
	    dwFlag |= ENABLE_PROCESSED_INPUT;
	else
	    dwFlag &= (DWORD) (~ENABLE_PROCESSED_INPUT);

	dwFlag |= ENABLE_MOUSE_INPUT;

	buf->c_iflag = iflag;
	buf->c_lflag = lflag;
	SetConsoleMode(TCB->inp, dwFlag);
	TCB->term.Nttyb = *buf;
    } else {
	iflag = TCB->term.Nttyb.c_iflag;
	lflag = TCB->term.Nttyb.c_lflag;
	GetConsoleMode(TCB->inp, &dwFlag);

	if (dwFlag & ENABLE_LINE_INPUT)
	    lflag |= ICANON;
	else
	    lflag &= (tcflag_t) (~ICANON);

	if (dwFlag & ENABLE_ECHO_INPUT)
	    lflag |= ECHO;
	else
	    lflag &= (tcflag_t) (~ECHO);

	if (dwFlag & ENABLE_PROCESSED_INPUT)
	    iflag |= BRKINT;
	else
	    iflag &= (tcflag_t) (~BRKINT);

	TCB->term.Nttyb.c_iflag = iflag;
	TCB->term.Nttyb.c_lflag = lflag;

	*buf = TCB->term.Nttyb;
    }
    return OK;
}

static int
drv_mode(TERMINAL_CONTROL_BLOCK * TCB, int progFlag, int defFlag)
{
    SCREEN *sp;
    TERMINAL *_term = (TERMINAL *) TCB;
    int code = ERR;

    AssertTCB();
    sp = TCB->csp;

    PropOf(TCB)->progMode = progFlag;
    PropOf(TCB)->lastOut = progFlag ? TCB->hdl : TCB->out;
    SetConsoleActiveScreenBuffer(PropOf(TCB)->lastOut);

    if (progFlag) /* prog mode */  {
	if (defFlag) {
	    if ((drv_sgmode(TCB, FALSE, &(_term->Nttyb)) == OK)) {
		_term->Nttyb.c_oflag &= (tcflag_t) (~OFLAGS_TABS);
		code = OK;
	    }
	} else {
	    /* reset_prog_mode */
	    if (drv_sgmode(TCB, TRUE, &(_term->Nttyb)) == OK) {
		if (sp) {
		    if (sp->_keypad_on)
			_nc_keypad(sp, TRUE);
		    NC_BUFFERED(sp, TRUE);
		}
		code = OK;
	    }
	}
    } else {			/* shell mode */
	if (defFlag) {
	    /* def_shell_mode */
	    if (drv_sgmode(TCB, FALSE, &(_term->Ottyb)) == OK) {
		code = OK;
	    }
	} else {
	    /* reset_shell_mode */
	    if (sp) {
		_nc_keypad(sp, FALSE);
		NCURSES_SP_NAME(_nc_flush) (sp);
		NC_BUFFERED(sp, FALSE);
	    }
	    code = drv_sgmode(TCB, TRUE, &(_term->Ottyb));
	}
    }

    return (code);
}

static void
drv_screen_init(SCREEN *sp GCC_UNUSED)
{
}

static void
drv_wrap(SCREEN *sp GCC_UNUSED)
{
}

static int
rkeycompare(const void *el1, const void *el2)
{
    WORD key1 = (LOWORD((*((const LONG *) el1)))) & 0x7fff;
    WORD key2 = (LOWORD((*((const LONG *) el2)))) & 0x7fff;

    return ((key1 < key2) ? -1 : ((key1 == key2) ? 0 : 1));
}

static int
keycompare(const void *el1, const void *el2)
{
    WORD key1 = HIWORD((*((const LONG *) el1)));
    WORD key2 = HIWORD((*((const LONG *) el2)));

    return ((key1 < key2) ? -1 : ((key1 == key2) ? 0 : 1));
}

static int
MapKey(TERMINAL_CONTROL_BLOCK * TCB, WORD vKey)
{
    WORD nKey = 0;
    void *res;
    LONG key = GenMap(vKey, 0);
    int code = -1;

    AssertTCB();

    res = bsearch(&key,
		  PropOf(TCB)->map,
		  (size_t) (N_INI + FKEYS),
		  sizeof(keylist[0]),
		  keycompare);
    if (res) {
	key = *((LONG *) res);
	nKey = LOWORD(key);
	code = (int) (nKey & 0x7fff);
	if (nKey & 0x8000)
	    code = -code;
    }
    return code;
}

static void
drv_release(TERMINAL_CONTROL_BLOCK * TCB)
{
    T((T_CALLED("win32con::drv_release(%p)"), TCB));

    AssertTCB();
    if (TCB->prop)
	free(TCB->prop);

    returnVoid;
}

/*
 * Attempt to save the screen contents.  PDCurses does this if
 * PDC_RESTORE_SCREEN is set, giving the same visual appearance on restoration
 * as if the library had allocated a console buffer.
 */
static bool
save_original_screen(TERMINAL_CONTROL_BLOCK * TCB)
{
    bool result = FALSE;
    Properties *p = PropOf(TCB);
    COORD bufferSize;
    COORD bufferCoord;
    SMALL_RECT readRegion;
    size_t want;

    bufferSize.X = p->SBI.dwSize.X;
    bufferSize.Y = p->SBI.dwSize.Y;
    want = (size_t) (bufferSize.X * bufferSize.Y);

    if ((p->save_screen = malloc(want * sizeof(CHAR_INFO))) != 0) {
	bufferCoord.X = bufferCoord.Y = 0;

	readRegion.Top = 0;
	readRegion.Left = 0;
	readRegion.Bottom = (SHORT) (bufferSize.Y - 1);
	readRegion.Right = (SHORT) (bufferSize.X - 1);

	T(("... reading console buffer %dx%d into %d,%d - %d,%d at %d,%d",
	   bufferSize.Y, bufferSize.X,
	   readRegion.Top,
	   readRegion.Left,
	   readRegion.Bottom,
	   readRegion.Right,
	   bufferCoord.Y,
	   bufferCoord.X));

	if (ReadConsoleOutput(TCB->hdl,
			      p->save_screen,
			      bufferSize,
			      bufferCoord,
			      &readRegion)) {
	    result = TRUE;
	} else {
	    T((" error %#lx", (unsigned long) GetLastError()));
	    FreeAndNull(p->save_screen);

	    bufferSize.X = (SHORT) (p->SBI.srWindow.Right
				    - p->SBI.srWindow.Left + 1);
	    bufferSize.Y = (SHORT) (p->SBI.srWindow.Bottom
				    - p->SBI.srWindow.Top + 1);
	    want = (size_t) (bufferSize.X * bufferSize.Y);

	    if ((p->save_screen = malloc(want * sizeof(CHAR_INFO))) != 0) {
		bufferCoord.X = bufferCoord.Y = 0;

		readRegion.Top = p->SBI.srWindow.Top;
		readRegion.Left = p->SBI.srWindow.Left;
		readRegion.Bottom = p->SBI.srWindow.Bottom;
		readRegion.Right = p->SBI.srWindow.Right;

		T(("... reading console window %dx%d into %d,%d - %d,%d at %d,%d",
		   bufferSize.Y, bufferSize.X,
		   readRegion.Top,
		   readRegion.Left,
		   readRegion.Bottom,
		   readRegion.Right,
		   bufferCoord.Y,
		   bufferCoord.X));

		if (ReadConsoleOutput(TCB->hdl,
				      p->save_screen,
				      bufferSize,
				      bufferCoord,
				      &readRegion)) {
		    result = TRUE;
		} else {
		    T((" error %#lx", (unsigned long) GetLastError()));
		}
	    }
	}
    }

    T(("... save original screen contents %s", result ? "ok" : "err"));
    return result;
}

static void
drv_init(TERMINAL_CONTROL_BLOCK * TCB)
{
    DWORD num_buttons;

    T((T_CALLED("win32con::drv_init(%p)"), TCB));

    AssertTCB();

    if (TCB) {
	BOOL b = AllocConsole();
	WORD a;
	int i;
	bool buffered = TRUE;

	if (!b)
	    b = AttachConsole(ATTACH_PARENT_PROCESS);

	TCB->inp = GetStdHandle(STD_INPUT_HANDLE);
	TCB->out = GetStdHandle(STD_OUTPUT_HANDLE);

	if (getenv("NCGDB")) {
	    TCB->hdl = TCB->out;
	    buffered = FALSE;
	} else {
	    TCB->hdl = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
						 0,
						 NULL,
						 CONSOLE_TEXTMODE_BUFFER,
						 NULL);
	}

	if (InvalidConsoleHandle(TCB->hdl)) {
	    returnVoid;
	} else if ((TCB->prop = typeCalloc(Properties, 1)) != 0) {
	    PropOf(TCB)->buffered = buffered;
	    if (!get_SBI(TCB)) {
		FreeAndNull(TCB->prop);		/* force error in drv_size */
		returnVoid;
	    }
	    if (!buffered) {
		if (!save_original_screen(TCB)) {
		    FreeAndNull(TCB->prop);	/* force error in drv_size */
		    returnVoid;
		}
	    }
	}

	TCB->info.initcolor = TRUE;
	TCB->info.canchange = FALSE;
	TCB->info.hascolor = TRUE;
	TCB->info.caninit = TRUE;

	TCB->info.maxpairs = NUMPAIRS;
	TCB->info.maxcolors = 8;
	TCB->info.numlabels = 0;
	TCB->info.labelwidth = 0;
	TCB->info.labelheight = 0;
	TCB->info.nocolorvideo = 1;
	TCB->info.tabsize = 8;

	if (GetNumberOfConsoleMouseButtons(&num_buttons)) {
	    T(("mouse has %ld buttons", num_buttons));
	    TCB->info.numbuttons = (int) num_buttons;
	} else {
	    TCB->info.numbuttons = 1;
	}

	TCB->info.defaultPalette = _nc_cga_palette;

	for (i = 0; i < (N_INI + FKEYS); i++) {
	    if (i < N_INI)
		PropOf(TCB)->rmap[i] = PropOf(TCB)->map[i] = (DWORD) keylist[i];
	    else
		PropOf(TCB)->rmap[i] = PropOf(TCB)->map[i] =
		    GenMap((VK_F1 + (i - N_INI)), (KEY_F(1) + (i - N_INI)));
	}
	qsort(PropOf(TCB)->map,
	      (size_t) (MAPSIZE),
	      sizeof(keylist[0]),
	      keycompare);
	qsort(PropOf(TCB)->rmap,
	      (size_t) (MAPSIZE),
	      sizeof(keylist[0]),
	      rkeycompare);

	a = MapColor(true, COLOR_WHITE) | MapColor(false, COLOR_BLACK);
	for (i = 0; i < NUMPAIRS; i++)
	    PropOf(TCB)->pairs[i] = a;
    }
    returnVoid;
}

static void
drv_initpair(TERMINAL_CONTROL_BLOCK * TCB,
	     int pair,
	     int f,
	     int b)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();

    if ((pair > 0) && (pair < NUMPAIRS) && (f >= 0) && (f < 8)
	&& (b >= 0) && (b < 8)) {
	PropOf(TCB)->pairs[pair] = MapColor(true, f) | MapColor(false, b);
    }
}

static void
drv_initcolor(TERMINAL_CONTROL_BLOCK * TCB,
	      int color GCC_UNUSED,
	      int r GCC_UNUSED,
	      int g GCC_UNUSED,
	      int b GCC_UNUSED)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();
}

static void
drv_do_color(TERMINAL_CONTROL_BLOCK * TCB,
	     int old_pair GCC_UNUSED,
	     int pair GCC_UNUSED,
	     int reverse GCC_UNUSED,
	     int (*outc) (SCREEN *, int) GCC_UNUSED
)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();
}

static void
drv_initmouse(TERMINAL_CONTROL_BLOCK * TCB)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();

    sp->_mouse_type = M_TERM_DRIVER;
}

static int
drv_testmouse(TERMINAL_CONTROL_BLOCK * TCB, int delay)
{
    int rc = 0;
    SCREEN *sp;

    AssertTCB();
    SetSP();

    if (sp->_drv_mouse_head < sp->_drv_mouse_tail) {
	rc = TW_MOUSE;
    } else {
	rc = TCBOf(sp)->drv->twait(TCBOf(sp),
				   TWAIT_MASK,
				   delay,
				   (int *) 0
				   EVENTLIST_2nd(evl));
    }

    return rc;
}

static int
drv_mvcur(TERMINAL_CONTROL_BLOCK * TCB,
	  int yold GCC_UNUSED, int xold GCC_UNUSED,
	  int y, int x)
{
    int ret = ERR;
    if (okConsoleHandle(TCB)) {
	Properties *p = PropOf(TCB);
	COORD loc;
	loc.X = (short) x;
	loc.Y = (short) (y + AdjustY(p));
	SetConsoleCursorPosition(TCB->hdl, loc);
	ret = OK;
    }
    return ret;
}

static void
drv_hwlabel(TERMINAL_CONTROL_BLOCK * TCB,
	    int labnum GCC_UNUSED,
	    char *text GCC_UNUSED)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();
}

static void
drv_hwlabelOnOff(TERMINAL_CONTROL_BLOCK * TCB,
		 int OnFlag GCC_UNUSED)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();
}

static chtype
drv_conattr(TERMINAL_CONTROL_BLOCK * TCB GCC_UNUSED)
{
    chtype res = A_NORMAL;
    res |= (A_BOLD | A_DIM | A_REVERSE | A_STANDOUT | A_COLOR);
    return res;
}

static void
drv_setfilter(TERMINAL_CONTROL_BLOCK * TCB)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();
}

static void
drv_initacs(TERMINAL_CONTROL_BLOCK * TCB,
	    chtype *real_map GCC_UNUSED,
	    chtype *fake_map GCC_UNUSED)
{
#define DATA(a,b) { a, b }
    static struct {
	int acs_code;
	int use_code;
    } table[] = {
	DATA('a', 0xb1),	/* ACS_CKBOARD  */
	    DATA('f', 0xf8),	/* ACS_DEGREE   */
	    DATA('g', 0xf1),	/* ACS_PLMINUS  */
	    DATA('j', 0xd9),	/* ACS_LRCORNER */
	    DATA('l', 0xda),	/* ACS_ULCORNER */
	    DATA('k', 0xbf),	/* ACS_URCORNER */
	    DATA('m', 0xc0),	/* ACS_LLCORNER */
	    DATA('n', 0xc5),	/* ACS_PLUS     */
	    DATA('q', 0xc4),	/* ACS_HLINE    */
	    DATA('t', 0xc3),	/* ACS_LTEE     */
	    DATA('u', 0xb4),	/* ACS_RTEE     */
	    DATA('v', 0xc1),	/* ACS_BTEE     */
	    DATA('w', 0xc2),	/* ACS_TTEE     */
	    DATA('x', 0xb3),	/* ACS_VLINE    */
	    DATA('y', 0xf3),	/* ACS_LEQUAL   */
	    DATA('z', 0xf2),	/* ACS_GEQUAL   */
	    DATA('0', 0xdb),	/* ACS_BLOCK    */
	    DATA('{', 0xe3),	/* ACS_PI       */
	    DATA('}', 0x9c),	/* ACS_STERLING */
	    DATA(',', 0xae),	/* ACS_LARROW   */
	    DATA('+', 0xaf),	/* ACS_RARROW   */
	    DATA('~', 0xf9),	/* ACS_BULLET   */
    };
#undef DATA
    unsigned n;

    SCREEN *sp;
    AssertTCB();
    SetSP();

    for (n = 0; n < SIZEOF(table); ++n) {
	real_map[table[n].acs_code] = (chtype) table[n].use_code | A_ALTCHARSET;
	if (sp != 0)
	    sp->_screen_acs_map[table[n].acs_code] = TRUE;
    }
}

static ULONGLONG
tdiff(FILETIME fstart, FILETIME fend)
{
    ULARGE_INTEGER ustart;
    ULARGE_INTEGER uend;
    ULONGLONG diff;

    ustart.LowPart = fstart.dwLowDateTime;
    ustart.HighPart = fstart.dwHighDateTime;
    uend.LowPart = fend.dwLowDateTime;
    uend.HighPart = fend.dwHighDateTime;

    diff = (uend.QuadPart - ustart.QuadPart) / 10000;
    return diff;
}

static int
Adjust(int milliseconds, int diff)
{
    if (milliseconds == INFINITY)
	return milliseconds;
    milliseconds -= diff;
    if (milliseconds < 0)
	milliseconds = 0;
    return milliseconds;
}

#define BUTTON_MASK (FROM_LEFT_1ST_BUTTON_PRESSED | \
		     FROM_LEFT_2ND_BUTTON_PRESSED | \
		     FROM_LEFT_3RD_BUTTON_PRESSED | \
		     FROM_LEFT_4TH_BUTTON_PRESSED | \
		     RIGHTMOST_BUTTON_PRESSED)

static int
decode_mouse(TERMINAL_CONTROL_BLOCK * TCB, int mask)
{
    SCREEN *sp;
    int result = 0;

    AssertTCB();
    SetSP();

    if (mask & FROM_LEFT_1ST_BUTTON_PRESSED)
	result |= BUTTON1_PRESSED;
    if (mask & FROM_LEFT_2ND_BUTTON_PRESSED)
	result |= BUTTON2_PRESSED;
    if (mask & FROM_LEFT_3RD_BUTTON_PRESSED)
	result |= BUTTON3_PRESSED;
    if (mask & FROM_LEFT_4TH_BUTTON_PRESSED)
	result |= BUTTON4_PRESSED;

    if (mask & RIGHTMOST_BUTTON_PRESSED) {
	switch (TCB->info.numbuttons) {
	case 1:
	    result |= BUTTON1_PRESSED;
	    break;
	case 2:
	    result |= BUTTON2_PRESSED;
	    break;
	case 3:
	    result |= BUTTON3_PRESSED;
	    break;
	case 4:
	    result |= BUTTON4_PRESSED;
	    break;
	}
    }

    return result;
}

static int
drv_twait(TERMINAL_CONTROL_BLOCK * TCB,
	  int mode,
	  int milliseconds,
	  int *timeleft
	  EVENTLIST_2nd(_nc_eventlist * evl))
{
    SCREEN *sp;
    INPUT_RECORD inp_rec;
    BOOL b;
    DWORD nRead = 0, rc = (DWORD) (-1);
    int code = 0;
    FILETIME fstart;
    FILETIME fend;
    int diff;
    bool isImmed = (milliseconds == 0);

#define CONSUME() ReadConsoleInput(TCB->inp,&inp_rec,1,&nRead)

    AssertTCB();
    SetSP();

    TR(TRACE_IEVENT, ("start twait: %d milliseconds, mode: %d",
		      milliseconds, mode));

    if (milliseconds < 0)
	milliseconds = INFINITY;

    memset(&inp_rec, 0, sizeof(inp_rec));

    while (true) {
	GetSystemTimeAsFileTime(&fstart);
	rc = WaitForSingleObject(TCB->inp, (DWORD) milliseconds);
	GetSystemTimeAsFileTime(&fend);
	diff = (int) tdiff(fstart, fend);
	milliseconds = Adjust(milliseconds, diff);

	if (!isImmed && milliseconds == 0)
	    break;

	if (rc == WAIT_OBJECT_0) {
	    if (mode) {
		b = GetNumberOfConsoleInputEvents(TCB->inp, &nRead);
		if (b && nRead > 0) {
		    b = PeekConsoleInput(TCB->inp, &inp_rec, 1, &nRead);
		    if (b && nRead > 0) {
			switch (inp_rec.EventType) {
			case KEY_EVENT:
			    if (mode & TW_INPUT) {
				WORD vk = inp_rec.Event.KeyEvent.wVirtualKeyCode;
				char ch = inp_rec.Event.KeyEvent.uChar.AsciiChar;

				if (inp_rec.Event.KeyEvent.bKeyDown) {
				    if (0 == ch) {
					int nKey = MapKey(TCB, vk);
					if ((nKey < 0) || FALSE == sp->_keypad_on) {
					    CONSUME();
					    continue;
					}
				    }
				    code = TW_INPUT;
				    goto end;
				} else {
				    CONSUME();
				}
			    }
			    continue;
			case MOUSE_EVENT:
			    if (decode_mouse(TCB,
					     (inp_rec.Event.MouseEvent.dwButtonState
					      & BUTTON_MASK)) == 0) {
				CONSUME();
			    } else if (mode & TW_MOUSE) {
				code = TW_MOUSE;
				goto end;
			    }
			    continue;
			default:
			    selectActiveHandle(TCB);
			    continue;
			}
		    }
		}
	    }
	    continue;
	} else {
	    if (rc != WAIT_TIMEOUT) {
		code = -1;
		break;
	    } else {
		code = 0;
		break;
	    }
	}
    }
  end:

    TR(TRACE_IEVENT, ("end twait: returned %d (%d), remaining time %d msec",
		      code, errno, milliseconds));

    if (timeleft)
	*timeleft = milliseconds;

    return code;
}

static bool
handle_mouse(TERMINAL_CONTROL_BLOCK * TCB, MOUSE_EVENT_RECORD mer)
{
    SCREEN *sp;
    MEVENT work;
    bool result = FALSE;

    AssertTCB();
    SetSP();

    sp->_drv_mouse_old_buttons = sp->_drv_mouse_new_buttons;
    sp->_drv_mouse_new_buttons = mer.dwButtonState & BUTTON_MASK;

    /*
     * We're only interested if the button is pressed or released.
     * FIXME: implement continuous event-tracking.
     */
    if (sp->_drv_mouse_new_buttons != sp->_drv_mouse_old_buttons) {
	Properties *p = PropOf(TCB);

	memset(&work, 0, sizeof(work));

	if (sp->_drv_mouse_new_buttons) {

	    work.bstate |= (mmask_t) decode_mouse(TCB, sp->_drv_mouse_new_buttons);

	} else {

	    /* cf: BUTTON_PRESSED, BUTTON_RELEASED */
	    work.bstate |= (mmask_t) (decode_mouse(TCB,
						   sp->_drv_mouse_old_buttons)
				      >> 1);

	    result = TRUE;
	}

	work.x = mer.dwMousePosition.X;
	work.y = mer.dwMousePosition.Y - AdjustY(p);

	sp->_drv_mouse_fifo[sp->_drv_mouse_tail] = work;
	sp->_drv_mouse_tail += 1;
    }

    return result;
}

static int
drv_read(TERMINAL_CONTROL_BLOCK * TCB, int *buf)
{
    SCREEN *sp;
    int n = 1;
    INPUT_RECORD inp_rec;
    BOOL b;
    DWORD nRead;
    WORD vk;

    AssertTCB();
    assert(buf);
    SetSP();

    memset(&inp_rec, 0, sizeof(inp_rec));

    T((T_CALLED("win32con::drv_read(%p)"), TCB));
    while ((b = ReadConsoleInput(TCB->inp, &inp_rec, 1, &nRead))) {
	if (b && nRead > 0) {
	    if (inp_rec.EventType == KEY_EVENT) {
		if (!inp_rec.Event.KeyEvent.bKeyDown)
		    continue;
		*buf = (int) inp_rec.Event.KeyEvent.uChar.AsciiChar;
		vk = inp_rec.Event.KeyEvent.wVirtualKeyCode;
		if (*buf == 0) {
		    if (sp->_keypad_on) {
			*buf = MapKey(TCB, vk);
			if (0 > (*buf))
			    continue;
			else
			    break;
		    } else
			continue;
		} else {	/* *buf != 0 */
		    break;
		}
	    } else if (inp_rec.EventType == MOUSE_EVENT) {
		if (handle_mouse(TCB, inp_rec.Event.MouseEvent)) {
		    *buf = KEY_MOUSE;
		    break;
		}
	    }
	    continue;
	}
    }
    returnCode(n);
}

static int
drv_nap(TERMINAL_CONTROL_BLOCK * TCB GCC_UNUSED, int ms)
{
    T((T_CALLED("win32con::drv_nap(%p, %d)"), TCB, ms));
    Sleep((DWORD) ms);
    returnCode(OK);
}

static bool
drv_kyExist(TERMINAL_CONTROL_BLOCK * TCB, int keycode)
{
    SCREEN *sp;
    WORD nKey;
    void *res;
    bool found = FALSE;
    LONG key = GenMap(0, (WORD) keycode);

    AssertTCB();
    SetSP();

    AssertTCB();

    T((T_CALLED("win32con::drv_kyExist(%p, %d)"), TCB, keycode));
    res = bsearch(&key,
		  PropOf(TCB)->rmap,
		  (size_t) (N_INI + FKEYS),
		  sizeof(keylist[0]),
		  rkeycompare);
    if (res) {
	key = *((LONG *) res);
	nKey = LOWORD(key);
	if (!(nKey & 0x8000))
	    found = TRUE;
    }
    returnCode(found);
}

static int
drv_kpad(TERMINAL_CONTROL_BLOCK * TCB, int flag GCC_UNUSED)
{
    SCREEN *sp;
    int code = ERR;

    AssertTCB();
    sp = TCB->csp;

    T((T_CALLED("win32con::drv_kpad(%p, %d)"), TCB, flag));
    if (sp) {
	code = OK;
    }
    returnCode(code);
}

static int
drv_keyok(TERMINAL_CONTROL_BLOCK * TCB, int keycode, int flag)
{
    int code = ERR;
    SCREEN *sp;
    WORD nKey;
    WORD vKey;
    void *res;
    LONG key = GenMap(0, (WORD) keycode);

    AssertTCB();
    SetSP();

    T((T_CALLED("win32con::drv_keyok(%p, %d, %d)"), TCB, keycode, flag));
    if (sp) {
	res = bsearch(&key,
		      PropOf(TCB)->rmap,
		      (size_t) (N_INI + FKEYS),
		      sizeof(keylist[0]),
		      rkeycompare);
	if (res) {
	    key = *((LONG *) res);
	    vKey = HIWORD(key);
	    nKey = (LOWORD(key)) & 0x7fff;
	    if (!flag)
		nKey |= 0x8000;
	    *(LONG *) res = GenMap(vKey, nKey);
	}
    }
    returnCode(code);
}

NCURSES_EXPORT_VAR (TERM_DRIVER) _nc_WIN_DRIVER = {
    FALSE,
	drv_CanHandle,		/* CanHandle */
	drv_init,		/* init */
	drv_release,		/* release */
	drv_size,		/* size */
	drv_sgmode,		/* sgmode */
	drv_conattr,		/* conattr */
	drv_mvcur,		/* hwcur */
	drv_mode,		/* mode */
	drv_rescol,		/* rescol */
	drv_rescolors,		/* rescolors */
	drv_setcolor,		/* color */
	drv_dobeepflash,	/* DoBeepFlash */
	drv_initpair,		/* initpair */
	drv_initcolor,		/* initcolor */
	drv_do_color,		/* docolor */
	drv_initmouse,		/* initmouse */
	drv_testmouse,		/* testmouse */
	drv_setfilter,		/* setfilter */
	drv_hwlabel,		/* hwlabel */
	drv_hwlabelOnOff,	/* hwlabelOnOff */
	drv_doupdate,		/* update */
	drv_defaultcolors,	/* defaultcolors */
	drv_print,		/* print */
	drv_size,		/* getsize */
	drv_setsize,		/* setsize */
	drv_initacs,		/* initacs */
	drv_screen_init,	/* scinit */
	drv_wrap,		/* scexit */
	drv_twait,		/* twait */
	drv_read,		/* read */
	drv_nap,		/* nap */
	drv_kpad,		/* kpad */
	drv_keyok,		/* kyOk */
	drv_kyExist		/* kyExist */
};
