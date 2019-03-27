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
static const char sccsid[] = "$Id: v_paragraph.c,v 10.10 2001/06/25 15:19:32 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"
#include "vi.h"

#define	INTEXT_CHECK {							\
	if (len == 0 || v_isempty(p, len)) {				\
		if (!--cnt)						\
			goto found;					\
		pstate = P_INBLANK;					\
	}								\
	/*								\
	 * !!!								\
	 * Historic documentation (USD:15-11, 4.2) said that formfeed	\
	 * characters (^L) in the first column delimited paragraphs.	\
	 * The historic vi code mentions formfeed characters, but never	\
	 * implements them.  It seems reasonable, do it.		\
	 */								\
	if (p[0] == '\014') {						\
		if (!--cnt)						\
			goto found;					\
		continue;						\
	}								\
	if (p[0] != '.' || len < 2)					\
		continue;						\
	for (lp = VIP(sp)->ps; *lp != '\0'; lp += 2)			\
		if (lp[0] == p[1] &&					\
		    (lp[1] == ' ' && len == 2 || lp[1] == p[2]) &&	\
		    !--cnt)						\
			goto found;					\
}

/*
 * v_paragraphf -- [count]}
 *	Move forward count paragraphs.
 *
 * Paragraphs are empty lines after text, formfeed characters, or values
 * from the paragraph or section options.
 *
 * PUBLIC: int v_paragraphf(SCR *, VICMD *);
 */
int
v_paragraphf(SCR *sp, VICMD *vp)
{
	enum { P_INTEXT, P_INBLANK } pstate;
	size_t lastlen, len;
	recno_t cnt, lastlno, lno;
	int isempty;
	CHAR_T *p;
	char *lp;

	/*
	 * !!!
	 * If the starting cursor position is at or before any non-blank
	 * characters in the line, i.e. the movement is cutting all of the
	 * line's text, the buffer is in line mode.  It's a lot easier to
	 * check here, because we know that the end is going to be the start
	 * or end of a line.
	 *
	 * This was historical practice in vi, with a single exception.  If
	 * the paragraph movement was from the start of the last line to EOF,
	 * then all the characters were deleted from the last line, but the
	 * line itself remained.  If somebody complains, don't pause, don't
	 * hesitate, just hit them.
	 */
	if (ISMOTION(vp))
		if (vp->m_start.cno == 0)
			F_SET(vp, VM_LMODE);
		else {
			vp->m_stop = vp->m_start;
			vp->m_stop.cno = 0;
			if (nonblank(sp, vp->m_stop.lno, &vp->m_stop.cno))
				return (1);
			if (vp->m_start.cno <= vp->m_stop.cno)
				F_SET(vp, VM_LMODE);
		}

	/* Figure out what state we're currently in. */
	lno = vp->m_start.lno;
	if (db_get(sp, lno, 0, &p, &len))
		goto eof;

	/*
	 * If we start in text, we want to switch states
	 * (2 * N - 1) times, in non-text, (2 * N) times.
	 */
	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	cnt *= 2;
	if (len == 0 || v_isempty(p, len))
		pstate = P_INBLANK;
	else {
		--cnt;
		pstate = P_INTEXT;
	}

	for (;;) {
		lastlno = lno;
		lastlen = len;
		if (db_get(sp, ++lno, 0, &p, &len))
			goto eof;
		switch (pstate) {
		case P_INTEXT:
			INTEXT_CHECK;
			break;
		case P_INBLANK:
			if (len == 0 || v_isempty(p, len))
				break;
			if (--cnt) {
				pstate = P_INTEXT;
				break;
			}
			/*
			 * !!!
			 * Non-motion commands move to the end of the range,
			 * delete and yank stay at the start.  Ignore others.
			 * Adjust the end of the range for motion commands;
			 * historically, a motion component was to the end of
			 * the previous line, whereas the movement command was
			 * to the start of the new "paragraph".
			 */
found:			if (ISMOTION(vp)) {
				vp->m_stop.lno = lastlno;
				vp->m_stop.cno = lastlen ? lastlen - 1 : 0;
				vp->m_final = vp->m_start;
			} else {
				vp->m_stop.lno = lno;
				vp->m_stop.cno = 0;
				vp->m_final = vp->m_stop;
			}
			return (0);
		default:
			abort();
		}
	}

	/*
	 * !!!
	 * Adjust end of the range for motion commands; EOF is a movement
	 * sink.  The } command historically moved to the end of the last
	 * line, not the beginning, from any position before the end of the
	 * last line.  It also historically worked on empty files, so we
	 * have to make it okay.
	 */
eof:	if (vp->m_start.lno == lno || vp->m_start.lno == lno - 1) {
		if (db_eget(sp, vp->m_start.lno, &p, &len, &isempty)) {
			if (!isempty)
				return (1);
			vp->m_start.cno = 0;
			return (0);
		}
		if (vp->m_start.cno == (len ? len - 1 : 0)) {
			v_eof(sp, NULL);
			return (1);
		}
	}
	/*
	 * !!!
	 * Non-motion commands move to the end of the range, delete
	 * and yank stay at the start.  Ignore others.
	 *
	 * If deleting the line (which happens if deleting to EOF), then
	 * cursor movement is to the first nonblank.
	 */
	if (ISMOTION(vp) && ISCMD(vp->rkp, 'd')) {
		F_CLR(vp, VM_RCM_MASK);
		F_SET(vp, VM_RCM_SETFNB);
	}
	vp->m_stop.lno = lno - 1;
	vp->m_stop.cno = len ? len - 1 : 0;
	vp->m_final = ISMOTION(vp) ? vp->m_start : vp->m_stop;
	return (0);
}

/*
 * v_paragraphb -- [count]{
 *	Move backward count paragraphs.
 *
 * PUBLIC: int v_paragraphb(SCR *, VICMD *);
 */
int
v_paragraphb(SCR *sp, VICMD *vp)
{
	enum { P_INTEXT, P_INBLANK } pstate;
	size_t len;
	recno_t cnt, lno;
	CHAR_T *p;
	char *lp;

	/*
	 * !!!
	 * Check for SOF.  The historic vi didn't complain if users hit SOF
	 * repeatedly, unless it was part of a motion command.  There is no
	 * question but that Emerson's editor of choice was vi.
	 *
	 * The { command historically moved to the beginning of the first
	 * line if invoked on the first line.
	 *
	 * !!!
	 * If the starting cursor position is in the first column (backward
	 * paragraph movements did NOT historically pay attention to non-blank
	 * characters) i.e. the movement is cutting the entire line, the buffer
	 * is in line mode.  Cuts from the beginning of the line also did not
	 * cut the current line, but started at the previous EOL.
	 *
	 * Correct for a left motion component while we're thinking about it.
	 */
	lno = vp->m_start.lno;

	if (ISMOTION(vp))
		if (vp->m_start.cno == 0) {
			if (vp->m_start.lno == 1) {
				v_sof(sp, &vp->m_start);
				return (1);
			} else
				--vp->m_start.lno;
			F_SET(vp, VM_LMODE);
		} else
			--vp->m_start.cno;

	if (vp->m_start.lno <= 1)
		goto sof;

	/* Figure out what state we're currently in. */
	if (db_get(sp, lno, 0, &p, &len))
		goto sof;

	/*
	 * If we start in text, we want to switch states
	 * (2 * N - 1) times, in non-text, (2 * N) times.
	 */
	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	cnt *= 2;
	if (len == 0 || v_isempty(p, len))
		pstate = P_INBLANK;
	else {
		--cnt;
		pstate = P_INTEXT;

		/*
		 * !!!
		 * If the starting cursor is past the first column,
		 * the current line is checked for a paragraph.
		 */
		if (vp->m_start.cno > 0)
			++lno;
	}

	for (;;) {
		if (db_get(sp, --lno, 0, &p, &len))
			goto sof;
		switch (pstate) {
		case P_INTEXT:
			INTEXT_CHECK;
			break;
		case P_INBLANK:
			if (len != 0 && !v_isempty(p, len)) {
				if (!--cnt)
					goto found;
				pstate = P_INTEXT;
			}
			break;
		default:
			abort();
		}
	}

	/* SOF is a movement sink. */
sof:	lno = 1;

found:	vp->m_stop.lno = lno;
	vp->m_stop.cno = 0;

	/*
	 * All commands move to the end of the range.  (We already
	 * adjusted the start of the range for motion commands).
	 */
	vp->m_final = vp->m_stop;
	return (0);
}

/*
 * v_buildps --
 *	Build the paragraph command search pattern.
 *
 * PUBLIC: int v_buildps(SCR *, char *, char *);
 */
int
v_buildps(SCR *sp, char *p_p, char *s_p)
{
	VI_PRIVATE *vip;
	size_t p_len, s_len;
	char *p;

	/*
	 * The vi paragraph command searches for either a paragraph or
	 * section option macro.
	 */
	p_len = p_p == NULL ? 0 : strlen(p_p);
	s_len = s_p == NULL ? 0 : strlen(s_p);

	if (p_len == 0 && s_len == 0)
		return (0);

	MALLOC_RET(sp, p, char *, p_len + s_len + 1);

	vip = VIP(sp);
	if (vip->ps != NULL)
		free(vip->ps);

	if (p_p != NULL)
		memmove(p, p_p, p_len + 1);
	if (s_p != NULL)
		memmove(p + p_len, s_p, s_len + 1);
	vip->ps = p;
	return (0);
}
