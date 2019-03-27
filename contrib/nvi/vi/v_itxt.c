/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: v_itxt.c,v 10.21 2001/06/25 15:19:32 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"
#include "vi.h"

/*
 * !!!
 * Repeated input in the historic vi is mostly wrong and this isn't very
 * backward compatible.  For example, if the user entered "3Aab\ncd" in
 * the historic vi, the "ab" was repeated 3 times, and the "\ncd" was then
 * appended to the result.  There was also a hack which I don't remember
 * right now, where "3o" would open 3 lines and then let the user fill them
 * in, to make screen movements on 300 baud modems more tolerable.  I don't
 * think it's going to be missed.
 *
 * !!!
 * There's a problem with the way that we do logging for change commands with
 * implied motions (e.g. A, I, O, cc, etc.).  Since the main vi loop logs the
 * starting cursor position before the change command "moves" the cursor, the
 * cursor position to which we return on undo will be where the user entered
 * the change command, not the start of the change.  Several of the following
 * routines re-log the cursor to make this work correctly.  Historic vi tried
 * to do the same thing, and mostly got it right.  (The only spectacular way
 * it fails is if the user entered 'o' from anywhere but the last character of
 * the line, the undo returned the cursor to the start of the line.  If the
 * user was on the last character of the line, the cursor returned to that
 * position.)  We also check for mapped keys waiting, i.e. if we're in the
 * middle of a map, don't bother logging the cursor.
 */
#define	LOG_CORRECT {							\
	if (!MAPPED_KEYS_WAITING(sp))					\
		(void)log_cursor(sp);					\
}

static u_int32_t set_txt_std(SCR *, VICMD *, u_int32_t);

/*
 * v_iA -- [count]A
 *	Append text to the end of the line.
 *
 * PUBLIC: int v_iA(SCR *, VICMD *);
 */
int
v_iA(SCR *sp, VICMD *vp)
{
	size_t len;

	if (!db_get(sp, vp->m_start.lno, 0, NULL, &len))
		sp->cno = len == 0 ? 0 : len - 1;

	LOG_CORRECT;

	return (v_ia(sp, vp));
}

/*
 * v_ia -- [count]a
 *	   [count]A
 *	Append text to the cursor position.
 *
 * PUBLIC: int v_ia(SCR *, VICMD *);
 */
int
v_ia(SCR *sp, VICMD *vp)
{
	size_t len;
	u_int32_t flags;
	int isempty;
	CHAR_T *p;

	flags = set_txt_std(sp, vp, 0);
	sp->showmode = SM_APPEND;
	sp->lno = vp->m_start.lno;

	/* Move the cursor one column to the right and repaint the screen. */
	if (db_eget(sp, sp->lno, &p, &len, &isempty)) {
		if (!isempty)
			return (1);
		len = 0;
		LF_SET(TXT_APPENDEOL);
	} else if (len) {
		if (len == sp->cno + 1) {
			sp->cno = len;
			LF_SET(TXT_APPENDEOL);
		} else
			++sp->cno;
	} else
		LF_SET(TXT_APPENDEOL);

	return (v_txt(sp, vp, NULL, p, len,
	    0, OOBLNO, F_ISSET(vp, VC_C1SET) ? vp->count : 1, flags));
}

/*
 * v_iI -- [count]I
 *	Insert text at the first nonblank.
 *
 * PUBLIC: int v_iI(SCR *, VICMD *);
 */
int
v_iI(SCR *sp, VICMD *vp)
{
	sp->cno = 0;
	if (nonblank(sp, vp->m_start.lno, &sp->cno))
		return (1);

	LOG_CORRECT;

	return (v_ii(sp, vp));
}

/*
 * v_ii -- [count]i
 *	   [count]I
 *	Insert text at the cursor position.
 *
 * PUBLIC: int v_ii(SCR *, VICMD *);
 */
int
v_ii(SCR *sp, VICMD *vp)
{
	size_t len;
	u_int32_t flags;
	int isempty;
	CHAR_T *p;

	flags = set_txt_std(sp, vp, 0);
	sp->showmode = SM_INSERT;
	sp->lno = vp->m_start.lno;

	if (db_eget(sp, sp->lno, &p, &len, &isempty)) {
		if (!isempty)
			return (1);
		len = 0;
	}

	if (len == 0)
		LF_SET(TXT_APPENDEOL);
	return (v_txt(sp, vp, NULL, p, len,
	    0, OOBLNO, F_ISSET(vp, VC_C1SET) ? vp->count : 1, flags));
}

enum which { o_cmd, O_cmd };
static int io(SCR *, VICMD *, enum which);

/*
 * v_iO -- [count]O
 *	Insert text above this line.
 *
 * PUBLIC: int v_iO(SCR *, VICMD *);
 */
int
v_iO(SCR *sp, VICMD *vp)
{
	return (io(sp, vp, O_cmd));
}

/*
 * v_io -- [count]o
 *	Insert text after this line.
 *
 * PUBLIC: int v_io(SCR *, VICMD *);
 */
int
v_io(SCR *sp, VICMD *vp)
{
	return (io(sp, vp, o_cmd));
}

static int
io(SCR *sp, VICMD *vp, enum which cmd)
{
	recno_t ai_line, lno;
	size_t len;
	u_int32_t flags;
	CHAR_T *p;

	flags = set_txt_std(sp, vp, TXT_ADDNEWLINE | TXT_APPENDEOL);
	sp->showmode = SM_INSERT;

	if (sp->lno == 1) {
		if (db_last(sp, &lno))
			return (1);
		if (lno != 0)
			goto insert;
		p = NULL;
		len = 0;
		ai_line = OOBLNO;
	} else {
insert:		p = L("");
		sp->cno = 0;
		LOG_CORRECT;

		if (cmd == O_cmd) {
			if (db_insert(sp, sp->lno, p, 0))
				return (1);
			if (db_get(sp, sp->lno, DBG_FATAL, &p, &len))
				return (1);
			ai_line = sp->lno + 1;
		} else {
			if (db_append(sp, 1, sp->lno, p, 0))
				return (1);
			if (db_get(sp, ++sp->lno, DBG_FATAL, &p, &len))
				return (1);
			ai_line = sp->lno - 1;
		}
	}
	return (v_txt(sp, vp, NULL, p, len,
	    0, ai_line, F_ISSET(vp, VC_C1SET) ? vp->count : 1, flags));
}

/*
 * v_change -- [buffer][count]c[count]motion
 *	       [buffer][count]C
 *	       [buffer][count]S
 *	Change command.
 *
 * PUBLIC: int v_change(SCR *, VICMD *);
 */
int
v_change(SCR *sp, VICMD *vp)
{
	size_t blen, len;
	u_int32_t flags;
	int isempty, lmode, rval;
	CHAR_T *bp;
	CHAR_T *p;

	/*
	 * 'c' can be combined with motion commands that set the resulting
	 * cursor position, i.e. "cG".  Clear the VM_RCM flags and make the
	 * resulting cursor position stick, inserting text has its own rules
	 * for cursor positioning.
	 */
	F_CLR(vp, VM_RCM_MASK);
	F_SET(vp, VM_RCM_SET);

	/*
	 * Find out if the file is empty, it's easier to handle it as a
	 * special case.
	 */
	if (vp->m_start.lno == vp->m_stop.lno &&
	    db_eget(sp, vp->m_start.lno, &p, &len, &isempty)) {
		if (!isempty)
			return (1);
		return (v_ia(sp, vp));
	}

	flags = set_txt_std(sp, vp, 0);
	sp->showmode = SM_CHANGE;

	/*
	 * Move the cursor to the start of the change.  Note, if autoindent
	 * is turned on, the cc command in line mode changes from the first
	 * *non-blank* character of the line, not the first character.  And,
	 * to make it just a bit more exciting, the initial space is handled
	 * as auto-indent characters.
	 */
	lmode = F_ISSET(vp, VM_LMODE) ? CUT_LINEMODE : 0;
	if (lmode) {
		vp->m_start.cno = 0;
		if (O_ISSET(sp, O_AUTOINDENT)) {
			if (nonblank(sp, vp->m_start.lno, &vp->m_start.cno))
				return (1);
			LF_SET(TXT_AICHARS);
		}
	}
	sp->lno = vp->m_start.lno;
	sp->cno = vp->m_start.cno;

	LOG_CORRECT;

	/*
	 * If not in line mode and changing within a single line, copy the
	 * text and overwrite it.
	 */
	if (!lmode && vp->m_start.lno == vp->m_stop.lno) {
		/*
		 * !!!
		 * Historic practice, c did not cut into the numeric buffers,
		 * only the unnamed one.
		 */
		if (cut(sp,
		    F_ISSET(vp, VC_BUFFER) ? &vp->buffer : NULL,
		    &vp->m_start, &vp->m_stop, lmode))
			return (1);
		if (len == 0)
			LF_SET(TXT_APPENDEOL);
		LF_SET(TXT_EMARK | TXT_OVERWRITE);
		return (v_txt(sp, vp, &vp->m_stop, p, len,
		    0, OOBLNO, F_ISSET(vp, VC_C1SET) ? vp->count : 1, flags));
	}

	/*
	 * It's trickier if in line mode or changing over multiple lines.  If
	 * we're in line mode delete all of the lines and insert a replacement
	 * line which the user edits.  If there was leading whitespace in the
	 * first line being changed, we copy it and use it as the replacement.
	 * If we're not in line mode, we delete the text and start inserting.
	 *
	 * !!!
	 * Copy the text.  Historic practice, c did not cut into the numeric
	 * buffers, only the unnamed one.
	 */
	if (cut(sp,
	    F_ISSET(vp, VC_BUFFER) ? &vp->buffer : NULL,
	    &vp->m_start, &vp->m_stop, lmode))
		return (1);

	/* If replacing entire lines and there's leading text. */
	if (lmode && vp->m_start.cno) {
		/*
		 * Get a copy of the first line changed, and copy out the
		 * leading text.
		 */
		if (db_get(sp, vp->m_start.lno, DBG_FATAL, &p, &len))
			return (1);
		GET_SPACE_RETW(sp, bp, blen, vp->m_start.cno);
		MEMMOVE(bp, p, vp->m_start.cno);
	} else
		bp = NULL;

	/* Delete the text. */
	if (del(sp, &vp->m_start, &vp->m_stop, lmode))
		return (1);

	/* If replacing entire lines, insert a replacement line. */
	if (lmode) {
		if (db_insert(sp, vp->m_start.lno, bp, vp->m_start.cno))
			return (1);
		sp->lno = vp->m_start.lno;
		len = sp->cno = vp->m_start.cno;
	}

	/* Get the line we're editing. */
	if (db_eget(sp, vp->m_start.lno, &p, &len, &isempty)) {
		if (!isempty)
			return (1);
		len = 0;
	}

	/* Check to see if we're appending to the line. */
	if (vp->m_start.cno >= len)
		LF_SET(TXT_APPENDEOL);

	rval = v_txt(sp, vp, NULL, p, len,
	    0, OOBLNO, F_ISSET(vp, VC_C1SET) ? vp->count : 1, flags);

	if (bp != NULL)
		FREE_SPACEW(sp, bp, blen);
	return (rval);
}

/*
 * v_Replace -- [count]R
 *	Overwrite multiple characters.
 *
 * PUBLIC: int v_Replace(SCR *, VICMD *);
 */
int
v_Replace(SCR *sp, VICMD *vp)
{
	size_t len;
	u_int32_t flags;
	int isempty;
	CHAR_T *p;

	flags = set_txt_std(sp, vp, 0);
	sp->showmode = SM_REPLACE;

	if (db_eget(sp, vp->m_start.lno, &p, &len, &isempty)) {
		if (!isempty)
			return (1);
		len = 0;
		LF_SET(TXT_APPENDEOL);
	} else {
		if (len == 0)
			LF_SET(TXT_APPENDEOL);
		LF_SET(TXT_OVERWRITE | TXT_REPLACE);
	}
	vp->m_stop.lno = vp->m_start.lno;
	vp->m_stop.cno = len ? len - 1 : 0;

	return (v_txt(sp, vp, &vp->m_stop, p, len,
	    0, OOBLNO, F_ISSET(vp, VC_C1SET) ? vp->count : 1, flags));
}

/*
 * v_subst -- [buffer][count]s
 *	Substitute characters.
 *
 * PUBLIC: int v_subst(SCR *, VICMD *);
 */
int
v_subst(SCR *sp, VICMD *vp)
{
	size_t len;
	u_int32_t flags;
	int isempty;
	CHAR_T *p;

	flags = set_txt_std(sp, vp, 0);
	sp->showmode = SM_CHANGE;

	if (db_eget(sp, vp->m_start.lno, &p, &len, &isempty)) {
		if (!isempty)
			return (1);
		len = 0;
		LF_SET(TXT_APPENDEOL);
	} else {
		if (len == 0)
			LF_SET(TXT_APPENDEOL);
		LF_SET(TXT_EMARK | TXT_OVERWRITE);
	}

	vp->m_stop.lno = vp->m_start.lno;
	vp->m_stop.cno =
	    vp->m_start.cno + (F_ISSET(vp, VC_C1SET) ? vp->count - 1 : 0);
	if (vp->m_stop.cno > len - 1)
		vp->m_stop.cno = len - 1;

	if (p != NULL && cut(sp,
	    F_ISSET(vp, VC_BUFFER) ? &vp->buffer : NULL,
	    &vp->m_start, &vp->m_stop, 0))
		return (1);

	return (v_txt(sp, vp, &vp->m_stop, p, len, 0, OOBLNO, 1, flags));
}

/*
 * set_txt_std --
 *	Initialize text processing flags.
 */
static u_int32_t
set_txt_std(SCR *sp, VICMD *vp, u_int32_t flags)
{
	LF_SET(TXT_CNTRLT |
	    TXT_ESCAPE | TXT_MAPINPUT | TXT_RECORD | TXT_RESOLVE);

	if (F_ISSET(vp, VC_ISDOT))
		LF_SET(TXT_REPLAY);

	if (O_ISSET(sp, O_ALTWERASE))
		LF_SET(TXT_ALTWERASE);
	if (O_ISSET(sp, O_AUTOINDENT))
		LF_SET(TXT_AUTOINDENT);
	if (O_ISSET(sp, O_BEAUTIFY))
		LF_SET(TXT_BEAUTIFY);
	if (O_ISSET(sp, O_SHOWMATCH))
		LF_SET(TXT_SHOWMATCH);
	if (F_ISSET(sp, SC_SCRIPT))
		LF_SET(TXT_CR);
	if (O_ISSET(sp, O_TTYWERASE))
		LF_SET(TXT_TTYWERASE);

	/*
	 * !!!
	 * Mapped keys were sometimes unaffected by the wrapmargin option
	 * in the historic 4BSD vi.  Consider the following commands, where
	 * each is executed on an empty line, in an 80 column screen, with
	 * the wrapmargin value set to 60.
	 *
	 *	aABC DEF <ESC>....
	 *	:map K aABC DEF ^V<ESC><CR>KKKKK
	 *	:map K 5aABC DEF ^V<ESC><CR>K
	 *
	 * The first and second commands are affected by wrapmargin.  The
	 * third is not.  (If the inserted text is itself longer than the
	 * wrapmargin value, i.e. if the "ABC DEF " string is replaced by
	 * something that's longer than 60 columns from the beginning of
	 * the line, the first two commands behave as before, but the third
	 * command gets fairly strange.)  The problem is that people wrote
	 * macros that depended on the third command NOT being affected by
	 * wrapmargin, as in this gem which centers lines:
	 *
	 *	map #c $mq81a ^V^[81^V^V|D`qld0:s/  / /g^V^M$p
	 *
	 * For compatibility reasons, we try and make it all work here.  I
	 * offer no hope that this is right, but it's probably pretty close.
	 *
	 * XXX
	 * Once I work my courage up, this is all gonna go away.  It's too
	 * evil to survive.
	 */
	if ((O_ISSET(sp, O_WRAPLEN) || O_ISSET(sp, O_WRAPMARGIN)) &&
	    (!MAPPED_KEYS_WAITING(sp) || !F_ISSET(vp, VC_C1SET)))
		LF_SET(TXT_WRAPMARGIN);
	return (flags);
}
