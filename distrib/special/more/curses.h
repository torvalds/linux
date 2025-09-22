/*
 * Copyright (c) 1981, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)curses.h	8.4 (Berkeley) 8/10/94
 */

#ifndef _OCURSES_H_
#define	_OCURSES_H_

#include <sys/types.h>

#include <stdio.h>

#define	bool	char

#ifndef TRUE
#define	TRUE	(1)
#endif
#ifndef FALSE
#define	FALSE	(0)
#endif

extern char	 GT;			/* Gtty indicates tabs. */
extern char	 NONL;			/* Term can't hack LF doing a CR. */
extern char	 UPPERCASE;		/* Terminal is uppercase only. */

extern int	 My_term;		/* Use Def_term regardless. */
extern char	*Def_term;		/* Default terminal type. */

/* Termcap capabilities. */
extern char	AM, BS, CA, DA, EO, HC, IN, MI, MS, NC, NS, OS,
		PC, UL, XB, XN, XT, XS, XX;
extern char	*AL, *BC, *BT, *CD, *CE, *CL, *CM, *CR, *CS, *DC, *DL,
		*DM, *DO, *ED, *EI, *K0, *K1, *K2, *K3, *K4, *K5, *K6,
		*K7, *K8, *K9, *HO, *IC, *IM, *IP, *KD, *KE, *KH, *KL,
		*KR, *KS, *KU, *LL, *MA, *ND, *NL, *RC, *SC, *SE, *SF,
		*SO, *SR, *TA, *TE, *TI, *UC, *UE, *UP, *US, *VB, *VS,
		*VE, *al, *dl, *sf, *sr,
		*AL_PARM, *DL_PARM, *UP_PARM, *DOWN_PARM, *LEFT_PARM,
		*RIGHT_PARM;

/* END BACKWARD COMPATIBILITY ONLY. */

/* 8-bit ASCII characters. */
#define	unctrl(c)		__unctrl[(c) & 0xff]
#define	unctrllen(ch)		__unctrllen[(ch) & 0xff]

extern char	*__unctrl[256];	/* Control strings. */
extern char	 __unctrllen[256];	/* Control strings length. */

/*
 * A window an array of __LINE structures pointed to by the 'lines' pointer.
 * A line is an array of __LDATA structures pointed to by the 'line' pointer.
 *
 * IMPORTANT: the __LDATA structure must NOT induce any padding, so if new
 * fields are added -- padding fields with *constant values* should ensure
 * that the compiler will not generate any padding when storing an array of
 *  __LDATA structures.  This is to enable consistent use of memcmp, and memcpy
 * for comparing and copying arrays.
 */
typedef struct {
	char ch;			/* the actual character */

#define	__STANDOUT	0x01		/* Added characters are standout. */
	char attr;			/* attributes of character */
} __LDATA;

#define __LDATASIZE	(sizeof(__LDATA))

typedef struct {
#define	__ISDIRTY	0x01		/* Line is dirty. */
#define __ISPASTEOL	0x02		/* Cursor is past end of line */
#define __FORCEPAINT	0x04		/* Force a repaint of the line */
	unsigned int flags;
	unsigned int hash;			/* Hash value for the line. */
	size_t *firstchp, *lastchp;	/* First and last chngd columns ptrs */
	size_t firstch, lastch;		/* First and last changed columns. */
	__LDATA *line;			/* Pointer to the line text. */
} __LINE;

typedef struct __window {		/* Window structure. */
	struct __window	*nextp, *orig;	/* Subwindows list and parent. */
	size_t begy, begx;		/* Window home. */
	size_t cury, curx;		/* Current x, y coordinates. */
	size_t maxy, maxx;		/* Maximum values for curx, cury. */
	short ch_off;			/* x offset for firstch/lastch. */
	__LINE **lines;			/* Array of pointers to the lines */
	__LINE  *lspace;		/* line space (for cleanup) */
	__LDATA *wspace;		/* window space (for cleanup) */

#define	__ENDLINE	0x001		/* End of screen. */
#define	__FLUSH		0x002		/* Fflush(stdout) after refresh. */
#define	__FULLWIN	0x004		/* Window is a screen. */
#define	__IDLINE	0x008		/* Insert/delete sequences. */
#define	__SCROLLWIN	0x010		/* Last char will scroll window. */
#define	__SCROLLOK	0x020		/* Scrolling ok. */
#define	__CLEAROK	0x040		/* Clear on next refresh. */
#define __WSTANDOUT	0x080		/* Standout window */
#define __LEAVEOK	0x100		/* If curser left */
	unsigned int flags;
} WINDOW;

/* Curses external declarations. */
extern WINDOW	*curscr;		/* Current screen. */
extern WINDOW	*stdscr;		/* Standard screen. */

extern struct termios __orig_termios;	/* Terminal state before curses */
extern struct termios __baset;		/* Our base terminal state */
extern int __tcaction;			/* If terminal hardware set. */

extern int	 COLS;			/* Columns on the screen. */
extern int	 LINES;			/* Lines on the screen. */

extern char	*ttytype;		/* Full name of current terminal. */

#define	ERR	(0)			/* Error return. */
#define	OK	(1)			/* Success return. */

/* Standard screen pseudo functions. */
#define	addbytes(s, n)			__waddbytes(stdscr, s, n, 0)
#define	addch(ch)			waddch(stdscr, ch)
#define	addnstr(s, n)			waddnstr(stdscr, s, n)
#define	addstr(s)			__waddbytes(stdscr, s, strlen(s), 0)
#define	clear()				wclear(stdscr)
#define	clrtobot()			wclrtobot(stdscr)
#define	clrtoeol()			wclrtoeol(stdscr)
#define	delch()				wdelch(stdscr)
#define	deleteln()			wdeleteln(stdscr)
#define	erase()				werase(stdscr)
#define	getch()				wgetch(stdscr)
#define	getstr(s)			wgetstr(stdscr, s)
#define	inch()				winch(stdscr)
#define	insch(ch)			winsch(stdscr, ch)
#define	insertln()			winsertln(stdscr)
#define	move(y, x)			wmove(stdscr, y, x)
#define	refresh()			wrefresh(stdscr)
#define	standend()			wstandend(stdscr)
#define	standout()			wstandout(stdscr)
#define	waddbytes(w, s, n)		__waddbytes(w, s, n, 0)
#define	waddstr(w, s)			__waddbytes(w, s, strlen(s), 0)

/* Standard screen plus movement pseudo functions. */
#define	mvaddbytes(y, x, s, n)		mvwaddbytes(stdscr, y, x, s, n)
#define	mvaddch(y, x, ch)		mvwaddch(stdscr, y, x, ch)
#define	mvaddnstr(y, x, s, n)		mvwaddnstr(stdscr, y, x, s, n)
#define	mvaddstr(y, x, s)		mvwaddstr(stdscr, y, x, s)
#define	mvdelch(y, x)			mvwdelch(stdscr, y, x)
#define	mvgetch(y, x)			mvwgetch(stdscr, y, x)
#define	mvgetstr(y, x, s)		mvwgetstr(stdscr, y, x, s)
#define	mvinch(y, x)			mvwinch(stdscr, y, x)
#define	mvinsch(y, x, c)		mvwinsch(stdscr, y, x, c)
#define	mvwaddbytes(w, y, x, s, n) \
	(wmove(w, y, x) == ERR ? ERR : __waddbytes(w, s, n, 0))
#define	mvwaddch(w, y, x, ch) \
	(wmove(w, y, x) == ERR ? ERR : waddch(w, ch))
#define	mvwaddnstr(w, y, x, s, n) \
	(wmove(w, y, x) == ERR ? ERR : waddnstr(w, s, n))
#define	mvwaddstr(w, y, x, s) \
	(wmove(w, y, x) == ERR ? ERR : __waddbytes(w, s, strlen(s), 0))
#define	mvwdelch(w, y, x) \
	(wmove(w, y, x) == ERR ? ERR : wdelch(w))
#define	mvwgetch(w, y, x) \
	(wmove(w, y, x) == ERR ? ERR : wgetch(w))
#define	mvwgetstr(w, y, x, s) \
	(wmove(w, y, x) == ERR ? ERR : wgetstr(w, s))
#define	mvwinch(w, y, x) \
	(wmove(w, y, x) == ERR ? ERR : winch(w))
#define	mvwinsch(w, y, x, c) \
	(wmove(w, y, x) == ERR ? ERR : winsch(w, c))

/* Psuedo functions. */
#define	clearok(w, bf) \
	((bf) ? ((w)->flags |= __CLEAROK) : ((w)->flags &= ~__CLEAROK))
#define	flushok(w, bf) \
	((bf) ? ((w)->flags |= __FLUSH) : ((w)->flags &= ~__FLUSH))
#define	getyx(w, y, x) \
	(y) = (w)->cury, (x) = (w)->curx
#define	leaveok(w, bf) \
	((bf) ? ((w)->flags |= __LEAVEOK) : ((w)->flags &= ~__LEAVEOK))
#define	scrollok(w, bf) \
	((bf) ? ((w)->flags |= __SCROLLOK) : ((w)->flags &= ~__SCROLLOK))
#define	winch(w) \
	((w)->lines[(w)->cury]->line[(w)->curx].ch & 0177)

/* Public function prototypes. */
__BEGIN_DECLS
int	 box(WINDOW *, int, int);
int	 cbreak(void);
int	 delwin(WINDOW *);
int	 echo(void);
int	 endwin(void);
char	*fullname(char *, char *);
char	*getcap(char *);
int	 gettmode(void);
void	 idlok(WINDOW *, int);
WINDOW	*initscr(void);
char	*longname(char *, char *);
int	 mvcur(int, int, int, int);
int	 mvprintw(int, int, const char *, ...);
int	 mvscanw(int, int, const char *, ...);
int	 mvwin(WINDOW *, int, int);
int	 mvwprintw(WINDOW *, int, int, const char *, ...);
int	 mvwscanw(WINDOW *, int, int, const char *, ...);
WINDOW	*newwin(int, int, int, int);
int	 nl(void);
int	 nocbreak(void);
int	 noecho(void);
int	 nonl(void);
int	 noraw(void);
int	 overlay(WINDOW *, WINDOW *);
int	 overwrite(WINDOW *, WINDOW *);
int	 printw(const char *, ...);
int	 raw(void);
int	 resetty(void);
int	 savetty(void);
int	 scanw(const char *, ...);
int	 scroll(WINDOW *);
int	 setterm(char *);
int	 sscans(WINDOW *, const char *, ...);
WINDOW	*subwin(WINDOW *, int, int, int, int);
int	 suspendwin(void);
int	 touchline(WINDOW *, int, int, int);
int	 touchoverlap(WINDOW *, WINDOW *);
int	 touchwin(WINDOW *);
int	 vwprintw(WINDOW *, const char *, __va_list);
int      vwscanw(WINDOW *, const char *, __va_list);
int	 waddch(WINDOW *, int);
int	 waddnstr(WINDOW *, const char *, int);
int	 wclear(WINDOW *);
int	 wclrtobot(WINDOW *);
int	 wclrtoeol(WINDOW *);
int	 wdelch(WINDOW *);
int	 wdeleteln(WINDOW *);
int	 werase(WINDOW *);
int	 wgetch(WINDOW *);
int	 wgetstr(WINDOW *, char *);
int	 winsch(WINDOW *, int);
int	 winsertln(WINDOW *);
int	 wmove(WINDOW *, int, int);
int	 wprintw(WINDOW *, const char *, ...);
int	 wrefresh(WINDOW *);
int	 wscanw(WINDOW *, const char *, ...);
int	 wstandend(WINDOW *);
int	 wstandout(WINDOW *);
int	 vwprintw(WINDOW *, const char *, __va_list);

/* Private functions that are needed for user programs prototypes. */
void	 __cputchar(int);
int	 __waddbytes(WINDOW *, const char *, int, int);
__END_DECLS

/* Private functions. */
void	 __CTRACE(const char *, ...);
unsigned int	 __hash(char *, int);
void	 __id_subwins(WINDOW *);
int	 __mvcur(int, int, int, int, int);
void	 __restore_stophandler(void);
void	 __set_stophandler(void);
void	 __set_subwin(WINDOW *, WINDOW *);
void	 __startwin(void);
void	 __stop_signal_handler(int);
void	 __swflags(WINDOW *);
int	 __touchline(WINDOW *, int, int, int, int);
int	 __touchwin(WINDOW *);
char	*__tscroll(const char *, int, int);
int	 __waddch(WINDOW *, __LDATA *);

/* Private #defines. */
#define	min(a,b)	(a < b ? a : b)
#define	max(a,b)	(a > b ? a : b)

/* Private externs. */
extern int	 __echoit;
extern int	 __endwin;
extern int	 __pfast;
extern int	 __rawmode;
extern int	 __noqch;

/* Termcap functions. */
__BEGIN_DECLS
int	 tgetent(char *, char *);
int	 tgetnum(char *);
int	 tgetflag(char *);
char	*tgetstr(char *, char **);
char	*tgoto(char *, int, int);
void	 tputs(char *, int, void (*)(int));
__END_DECLS

#endif /* !_OCURSES_H_ */
