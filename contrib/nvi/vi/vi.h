/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: vi.h,v 10.29 2012/02/11 00:33:46 zy Exp $
 */

/* Definition of a vi "word". */
#define	inword(ch)	((ch) == '_' || (ISGRAPH(ch) && !ISPUNCT(ch)))

typedef struct _vikeys VIKEYS;

/* Structure passed around to functions implementing vi commands. */
typedef struct _vicmd {
	CHAR_T	key;			/* Command key. */
	CHAR_T	buffer;			/* Buffer. */
	CHAR_T	character;		/* Character. */
	u_long	count;			/* Count. */
	u_long	count2;			/* Second count (only used by z). */
	EVENT	ev;			/* Associated event. */

#define	ISCMD(p, key)	((p) == &vikeys[key])
	VIKEYS const *kp;		/* Command/Motion VIKEYS entry. */
#define	ISMOTION(vp)	(vp->rkp != NULL && F_ISSET(vp->rkp, V_MOTION))
	VIKEYS const *rkp;		/* Related C/M VIKEYS entry. */

	/*
	 * Historic vi allowed "dl" when the cursor was on the last column,
	 * deleting the last character, and similarly allowed "dw" when
	 * the cursor was on the last column of the file.  It didn't allow
	 * "dh" when the cursor was on column 1, although these cases are
	 * not strictly analogous.  The point is that some movements would
	 * succeed if they were associated with a motion command, and fail
	 * otherwise.  This is part of the off-by-1 schizophrenia that
	 * plagued vi.  Other examples are that "dfb" deleted everything
	 * up to and including the next 'b' character, while "d/b" deleted
	 * everything up to the next 'b' character.  While this implementation
	 * regularizes the interface to the extent possible, there are many
	 * special cases that can't be fixed.  The special cases are handled
	 * by setting flags per command so that the underlying command and
	 * motion routines know what's really going on.
	 *
	 * The VM_* flags are set in the vikeys array and by the underlying
	 * functions (motion component or command) as well.  For this reason,
	 * the flags in the VICMD and VIKEYS structures live in the same name
	 * space.
	 */
#define	VM_CMDFAILED	0x00000001	/* Command failed. */
#define	VM_CUTREQ	0x00000002	/* Always cut into numeric buffers. */
#define	VM_LDOUBLE	0x00000004	/* Doubled command for line mode. */
#define	VM_LMODE	0x00000008	/* Motion is line oriented. */
#define	VM_COMMASK	0x0000000f	/* Mask for VM flags. */

	/*
	 * The VM_RCM_* flags are single usage, i.e. if you set one, you have
	 * to clear the others.
	 */
#define	VM_RCM		0x00000010	/* Use relative cursor movment (RCM). */
#define	VM_RCM_SET	0x00000020	/* RCM: set to current position. */
#define	VM_RCM_SETFNB	0x00000040	/* RCM: set to first non-blank (FNB). */
#define	VM_RCM_SETLAST	0x00000080	/* RCM: set to last character. */
#define	VM_RCM_SETNNB	0x00000100	/* RCM: set to next non-blank. */
#define	VM_RCM_MASK	0x000001f0	/* Mask for RCM flags. */

	/* Flags for the underlying function. */
#define	VC_BUFFER	0x00000200	/* The buffer was set. */
#define	VC_C1RESET	0x00000400	/* Reset C1SET flag for dot commands. */
#define	VC_C1SET	0x00000800	/* Count 1 was set. */
#define	VC_C2SET	0x00001000	/* Count 2 was set. */
#define	VC_ISDOT	0x00002000	/* Command was the dot command. */
	u_int32_t flags;

	/*
	 * There are four cursor locations that we worry about: the initial
	 * cursor position, the start of the range, the end of the range,
	 * and the final cursor position.  The initial cursor position and
	 * the start of the range are both m_start, and are always the same.
	 * All locations are initialized to the starting cursor position by
	 * the main vi routines, and the underlying functions depend on this.
	 *
	 * Commands that can be motion components set the end of the range
	 * cursor position, m_stop.  All commands must set the ending cursor
	 * position, m_final.  The reason that m_stop isn't the same as m_final
	 * is that there are situations where the final position of the cursor
	 * is outside of the cut/delete range (e.g. 'd[[' from the first column
	 * of a line).  The final cursor position often varies based on the
	 * direction of the movement, as well as the command.  The only special
	 * case that the delete code handles is that it will make adjustments
	 * if the final cursor position is deleted.
	 *
	 * The reason for all of this is that the historic vi semantics were
	 * defined command-by-command.  Every function has to roll its own
	 * starting and stopping positions, and adjust them if it's being used
	 * as a motion component.  The general rules are as follows:
	 *
	 *	1: If not a motion component, the final cursor is at the end
	 *	   of the range.
	 *	2: If moving backward in the file, delete and yank move the
	 *	   final cursor to the end of the range.
	 *	3: If moving forward in the file, delete and yank leave the
	 *	   final cursor at the start of the range.
	 *
	 * Usually, if moving backward in the file and it's a motion component,
	 * the starting cursor is decremented by a single character (or, in a
	 * few cases, to the end of the previous line) so that the starting
	 * cursor character isn't cut or deleted.  No cursor adjustment is
	 * needed for moving forward, because the cut/delete routines handle
	 * m_stop inclusively, i.e. the last character in the range is cut or
	 * deleted.  This makes cutting to the EOF/EOL reasonable.
	 *
	 * The 'c', '<', '>', and '!' commands are special cases.  We ignore
	 * the final cursor position for all of them: for 'c', the text input
	 * routines set the cursor to the last character inserted; for '<',
	 * '>' and '!', the underlying ex commands that do the operation will
	 * set the cursor for us, usually to something related to the first
	 * <nonblank>.
	 */
	MARK	 m_start;		/* mark: initial cursor, range start. */
	MARK	 m_stop;		/* mark: range end. */
	MARK	 m_final;		/* mark: final cursor position. */
} VICMD;

/* Vi command table structure. */
struct _vikeys {			/* Underlying function. */
	int	 (*func)(SCR *, VICMD *);
#define	V_ABS		0x00004000	/* Absolute movement, set '' mark. */
#define	V_ABS_C		0x00008000	/* V_ABS: if the line/column changed. */
#define	V_ABS_L		0x00010000	/* V_ABS: if the line changed. */
#define	V_CHAR		0x00020000	/* Character (required, trailing). */
#define	V_CNT		0x00040000	/* Count (optional, leading). */
#define	V_DOT		0x00080000	/* On success, sets dot command. */
#define	V_KEYW		0x00100000	/* Cursor referenced word. */
#define	V_MOTION	0x00200000	/* Motion (required, trailing). */
#define	V_MOVE		0x00400000	/* Command defines movement. */
#define	V_OBUF		0x00800000	/* Buffer (optional, leading). */
#define	V_RBUF		0x01000000	/* Buffer (required, trailing). */
#define	V_SECURE	0x02000000	/* Permission denied if O_SECURE set. */
	u_int32_t flags;
	char	*usage;			/* Usage line. */
	char	*help;			/* Help line. */
};
#define	MAXVIKEY	126		/* List of vi commands. */
extern VIKEYS const vikeys[MAXVIKEY + 1];
extern VIKEYS const tmotion;		/* XXX Hacked ~ command. */

/* Character stream structure, prototypes. */
typedef struct _vcs {
	recno_t	 cs_lno;		/* Line. */
	size_t	 cs_cno;		/* Column. */
	CHAR_T	*cs_bp;			/* Buffer. */
	size_t	 cs_len;		/* Length. */
	CHAR_T	 cs_ch;			/* Character. */
#define	CS_EMP	1			/* Empty line. */
#define	CS_EOF	2			/* End-of-file. */
#define	CS_EOL	3			/* End-of-line. */
#define	CS_SOF	4			/* Start-of-file. */
	int	 cs_flags;		/* Return flags. */
} VCS;

int	cs_bblank(SCR *, VCS *);
int	cs_fblank(SCR *, VCS *);
int	cs_fspace(SCR *, VCS *);
int	cs_init(SCR *, VCS *);
int	cs_next(SCR *, VCS *);
int	cs_prev(SCR *, VCS *);

/*
 * We use a single "window" for each set of vi screens.  The model would be
 * simpler with two windows (one for the text, and one for the modeline)
 * because scrolling the text window down would work correctly then, not
 * affecting the mode line.  As it is we have to play games to make it look
 * right.  The reason for this choice is that it would be difficult for
 * curses to optimize the movement, i.e. detect that the downward scroll
 * isn't going to change the modeline, set the scrolling region on the
 * terminal and only scroll the first part of the text window.
 *
 * Structure for mapping lines to the screen.  An SMAP is an array, with one
 * structure element per screen line, which holds information describing the
 * physical line which is displayed in the screen line.  The first two fields
 * (lno and off) are all that are necessary to describe a line.  The rest of
 * the information is useful to keep information from being re-calculated.
 *
 * The SMAP always has an entry for each line of the physical screen, plus a
 * slot for the colon command line, so there is room to add any screen into
 * another one at screen exit.
 *
 * Lno is the line number.  If doing the historic vi long line folding, soff
 * is the screen offset into the line.  For example, the pair 2:1 would be
 * the first screen of line 2, and 2:2 would be the second.  In the case of
 * long lines, the screen map will tend to be staggered, e.g., 1:1, 1:2, 1:3,
 * 2:1, 3:1, etc.  If doing left-right scrolling, the coff field is the screen
 * column offset into the lines, and can take on any value, as it's adjusted
 * by the user set value O_SIDESCROLL.
 */
typedef struct _smap {
	recno_t  lno;	/* 1-N: Physical file line number. */
	size_t	 coff;		/* 0-N: Column offset in the line. */
	size_t	 soff;		/* 1-N: Screen offset in the line. */

				/* vs_line() cache information. */
	size_t	 c_sboff;	/* 0-N: offset of first character on screen. */
	size_t	 c_eboff;	/* 0-N: offset of  last character on screen. */
	u_int8_t c_scoff;	/* 0-N: offset into the first character. */
				/* 255: no character of line visible. */
	u_int8_t c_eclen;	/* 1-N: columns from the last character. */
	u_int8_t c_ecsize;	/* 1-N: size of the last character. */
} SMAP;
				/* Macros to flush/test cached information. */
#define	SMAP_CACHE(smp)		((smp)->c_ecsize != 0)
#define	SMAP_FLUSH(smp)		((smp)->c_ecsize = 0)

				/* Character search information. */
typedef enum { CNOTSET, FSEARCH, fSEARCH, TSEARCH, tSEARCH } cdir_t;

typedef enum { AB_NOTSET, AB_NOTWORD, AB_INWORD } abb_t;
typedef enum { Q_NOTSET, Q_BNEXT, Q_BTHIS, Q_VNEXT, Q_VTHIS } quote_t;

/* Vi private, per-screen memory. */
typedef struct _vi_private {
	VICMD	cmd;		/* Current command, motion. */
	VICMD	motion;

	/*
	 * !!!
	 * The saved command structure can be modified by the underlying
	 * vi functions, see v_Put() and v_put().
	 */
	VICMD	sdot;		/* Saved dot, motion command. */
	VICMD	sdotmotion;

	CHAR_T *keyw;		/* Keyword buffer. */
	size_t	klen;		/* Keyword length. */
	size_t	keywlen;	/* Keyword buffer length. */

	CHAR_T	rlast;		/* Last 'r' replacement character. */
	e_key_t	rvalue;		/* Value of last replacement character. */

	EVENT  *rep;		/* Input replay buffer. */
	size_t	rep_len;	/* Input replay buffer length. */
	size_t	rep_cnt;	/* Input replay buffer characters. */

	mtype_t	mtype;		/* Last displayed message type. */
	size_t	linecount;	/* 1-N: Output overwrite count. */
	size_t	lcontinue;	/* 1-N: Output line continue value. */
	size_t	totalcount;	/* 1-N: Output overwrite count. */

				/* Busy state. */
	int	busy_ref;	/* Busy reference count. */
	int	busy_ch;	/* Busy character. */
	size_t	busy_fx;	/* Busy character x coordinate. */
	size_t	busy_oldy;	/* Saved y coordinate. */
	size_t	busy_oldx;	/* Saved x coordinate. */
	struct timespec busy_ts;/* Busy timer. */

	MARK	sel;		/* Select start position. */

	CHAR_T *mcs;		/* Match character list. */
	char   *ps;		/* Paragraph plus section list. */

	u_long	u_ccnt;		/* Undo command count. */

	CHAR_T	lastckey;	/* Last search character. */
	cdir_t	csearchdir;	/* Character search direction. */

	SMAP   *h_smap;		/* First slot of the line map. */
	SMAP   *t_smap;		/* Last slot of the line map. */

	/*
	 * One extra slot is always allocated for the map so that we can use
	 * it to do vi :colon command input; see v_tcmd().
	 */
	recno_t	sv_tm_lno;	/* tcmd: saved TMAP lno field. */
	size_t	sv_tm_coff;	/* tcmd: saved TMAP coff field. */
	size_t	sv_tm_soff;	/* tcmd: saved TMAP soff field. */
	size_t	sv_t_maxrows;	/* tcmd: saved t_maxrows. */
	size_t	sv_t_minrows;	/* tcmd: saved t_minrows. */
	size_t	sv_t_rows;	/* tcmd: saved t_rows. */
#define	SIZE_HMAP(sp)	(VIP(sp)->srows + 1)

	/*
	 * Macros to get to the head/tail of the smap.  If the screen only has
	 * one line, HMAP can be equal to TMAP, so the code has to understand
	 * the off-by-one errors that can result.  If stepping through an SMAP
	 * and operating on each entry, use sp->t_rows as the count of slots,
	 * don't use a loop that compares <= TMAP.
	 */
#define	_HMAP(sp)	(VIP(sp)->h_smap)
#define	HMAP		_HMAP(sp)
#define	_TMAP(sp)	(VIP(sp)->t_smap)
#define	TMAP		_TMAP(sp)

	recno_t	ss_lno;	/* 1-N: vi_opt_screens cached line number. */
	size_t	ss_screens;	/* vi_opt_screens cached return value. */
#define	VI_SCR_CFLUSH(vip)	vip->ss_lno = OOBLNO

	size_t	srows;		/* 1-N: rows in the terminal/window. */
	recno_t	olno;		/* 1-N: old cursor file line. */
	size_t	ocno;		/* 0-N: old file cursor column. */
	size_t	sc_col;		/* 0-N: LOGICAL screen column. */
	SMAP   *sc_smap;	/* SMAP entry where sc_col occurs. */

#define	VIP_CUR_INVALID	0x0001	/* Cursor position is unknown. */
#define	VIP_DIVIDER	0x0002	/* Divider line was displayed. */
#define	VIP_N_EX_PAINT	0x0004	/* Clear and repaint when ex finishes. */
#define	VIP_N_EX_REDRAW	0x0008	/* Schedule SC_SCR_REDRAW when ex finishes. */
#define	VIP_N_REFRESH	0x0010	/* Repaint (from SMAP) on the next refresh. */
#define	VIP_N_RENUMBER	0x0020	/* Renumber screen on the next refresh. */
#define	VIP_RCM_LAST	0x0040	/* Cursor drawn to the last column. */
#define	VIP_S_MODELINE	0x0080	/* Skip next modeline refresh. */
#define	VIP_S_REFRESH	0x0100	/* Skip next refresh. */
	u_int16_t flags;
} VI_PRIVATE;

/* Vi private area. */
#define	VIP(sp)	((VI_PRIVATE *)((sp)->vi_private))

#define	O_NUMBER_FMT	"%7lu "			/* O_NUMBER format, length. */
#define	O_NUMBER_LENGTH	8
#define	SCREEN_COLS(sp)				/* Screen columns. */	\
	((O_ISSET(sp, O_NUMBER) ? (sp)->cols - O_NUMBER_LENGTH : (sp)->cols))

/*
 * LASTLINE is the zero-based, last line in the screen.  Note that it is correct
 * regardless of the changes in the screen to permit text input on the last line
 * of the screen, or the existence of small screens.
 */
#define LASTLINE(sp) \
	((sp)->t_maxrows < (sp)->rows ? (sp)->t_maxrows : (sp)->rows - 1)

/*
 * Small screen (see vs_refresh.c, section 6a) and one-line screen test.
 * Note, both cannot be true for the same screen.
 */
#define	IS_SMALL(sp)	((sp)->t_minrows != (sp)->t_maxrows)
#define	IS_ONELINE(sp)	((sp)->rows == 1)

#define	HALFTEXT(sp)				/* Half text. */	\
	((sp)->t_rows == 1 ? 1 : (sp)->t_rows / 2)
#define	HALFSCREEN(sp)				/* Half text screen. */	\
	((sp)->t_maxrows == 1 ? 1 : (sp)->t_maxrows / 2)

/*
 * Next tab offset.
 *
 * !!!
 * There are problems with how the historical vi handled tabs.  For example,
 * by doing "set ts=3" and building lines that fold, you can get it to step
 * through tabs as if they were spaces and move inserted characters to new
 * positions when <esc> is entered.  I believe that nvi does tabs correctly,
 * but there are some historical incompatibilities.
 */
#define	TAB_OFF(c)	COL_OFF((c), O_VAL(sp, O_TABSTOP))

/* If more than one horizontal screen being shown. */
#define	IS_HSPLIT(sp)							\
	((sp)->rows != O_VAL(sp, O_LINES))
/* If more than one vertical screen being shown. */
#define	IS_VSPLIT(sp)							\
	((sp)->cols != O_VAL(sp, O_COLUMNS))
/* If more than one screen being shown. */
#define	IS_SPLIT(sp)							\
	(IS_HSPLIT(sp) || IS_VSPLIT(sp))

/* Screen adjustment operations. */
typedef enum { A_DECREASE, A_INCREASE, A_SET } adj_t;

/* Screen position operations. */
typedef enum { P_BOTTOM, P_FILL, P_MIDDLE, P_TOP } pos_t;

/* Scrolling operations. */
typedef enum {
	CNTRL_B, CNTRL_D, CNTRL_E, CNTRL_F,
	CNTRL_U, CNTRL_Y, Z_CARAT, Z_PLUS
} scroll_t;

/* Vi common error messages. */
typedef enum {
	VIM_COMBUF, VIM_EMPTY, VIM_EOF, VIM_EOL,
	VIM_NOCOM, VIM_NOCOM_B, VIM_USAGE, VIM_WRESIZE
} vim_t;

#include "extern.h"
