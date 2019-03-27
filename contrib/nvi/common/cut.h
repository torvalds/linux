/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1991, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: cut.h,v 10.10 2012/02/11 15:52:33 zy Exp $
 */

typedef struct _texth TEXTH;		/* TEXT list head structure. */
TAILQ_HEAD(_texth, _text);

/* Cut buffers. */
struct _cb {
	SLIST_ENTRY(_cb) q;		/* Linked list of cut buffers. */
	TEXTH	 textq[1];		/* Linked list of TEXT structures. */
	/* XXXX Needed ? Can non ascii-chars be cut buffer names ? */
	CHAR_T	 name;			/* Cut buffer name. */
	size_t	 len;			/* Total length of cut text. */

#define	CB_LMODE	0x01		/* Cut was in line mode. */
	u_int8_t flags;
};

/* Lines/blocks of text. */
struct _text {				/* Text: a linked list of lines. */
	TAILQ_ENTRY(_text) q;		/* Linked list of text structures. */
	CHAR_T	*lb;			/* Line buffer. */
	size_t	 lb_len;		/* Line buffer length. */
	size_t	 len;			/* Line length. */

	/* These fields are used by the vi text input routine. */
	recno_t	 lno;			/* 1-N: file line. */

#define	ENTIRE_LINE	((size_t)-1)	/* cno: end of the line. */
	size_t	 cno;			/* 0-N: file character in line. */
	size_t	 ai;			/* 0-N: autoindent bytes. */
	size_t	 insert;		/* 0-N: bytes to insert (push). */
	size_t	 offset;		/* 0-N: initial, unerasable chars. */
	size_t	 owrite;		/* 0-N: chars to overwrite. */
	size_t	 R_erase;		/* 0-N: 'R' erase count. */
	size_t	 sv_cno;		/* 0-N: Saved line cursor. */
	size_t	 sv_len;		/* 0-N: Saved line length. */

	/*
	 * These fields returns information from the vi text input routine.
	 *
	 * The termination condition.  Note, this field is only valid if the
	 * text input routine returns success.
	 *	TERM_BS:	User backspaced over the prompt.
	 *	TERM_CEDIT:	User entered <edit-char>.
	 *	TERM_CR:	User entered <carriage-return>; no data.
	 *	TERM_ESC:	User entered <escape>; no data.
	 *	TERM_OK:	Data available.
	 *	TERM_SEARCH:	Incremental search.
	 */
	enum {
	    TERM_BS, TERM_CEDIT, TERM_CR, TERM_ESC, TERM_OK, TERM_SEARCH
	} term;
};

/*
 * Get named buffer 'name'.
 * Translate upper-case buffer names to lower-case buffer names.
 */
#define	CBNAME(sp, cbp, nch) {						\
	CHAR_T L__name;							\
	L__name = isupper(nch) ? tolower(nch) : (nch);			\
	SLIST_FOREACH(cbp, sp->gp->cutq, q)				\
		if (cbp->name == L__name)				\
			break;						\
}

/* Flags to the cut() routine. */
#define	CUT_LINEMODE	0x01		/* Cut in line mode. */
#define	CUT_NUMOPT	0x02		/* Numeric buffer: optional. */
#define	CUT_NUMREQ	0x04		/* Numeric buffer: required. */
