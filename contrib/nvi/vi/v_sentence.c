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
static const char sccsid[] = "$Id: v_sentence.c,v 10.9 2001/06/25 15:19:35 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>

#include "../common/common.h"
#include "vi.h"

/*
 * !!!
 * In historic vi, a sentence was delimited by a '.', '?' or '!' character
 * followed by TWO spaces or a newline.  One or more empty lines was also
 * treated as a separate sentence.  The Berkeley documentation for historical
 * vi states that any number of ')', ']', '"' and '\'' characters can be
 * between the delimiter character and the spaces or end of line, however,
 * the historical implementation did not handle additional '"' characters.
 * We follow the documentation here, not the implementation.
 *
 * Once again, historical vi didn't do sentence movements associated with
 * counts consistently, mostly in the presence of lines containing only
 * white-space characters.
 *
 * This implementation also permits a single tab to delimit sentences, and
 * treats lines containing only white-space characters as empty lines.
 * Finally, tabs are eaten (along with spaces) when skipping to the start
 * of the text following a "sentence".
 */

/*
 * v_sentencef -- [count])
 *	Move forward count sentences.
 *
 * PUBLIC: int v_sentencef(SCR *, VICMD *);
 */
int
v_sentencef(SCR *sp, VICMD *vp)
{
	enum { BLANK, NONE, PERIOD } state;
	VCS cs;
	size_t len;
	u_long cnt;

	cs.cs_lno = vp->m_start.lno;
	cs.cs_cno = vp->m_start.cno;
	if (cs_init(sp, &cs))
		return (1);

	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;

	/*
	 * !!!
	 * If in white-space, the next start of sentence counts as one.
	 * This may not handle "  .  " correctly, but it's real unclear
	 * what correctly means in that case.
	 */
	if (cs.cs_flags == CS_EMP || (cs.cs_flags == 0 && isblank(cs.cs_ch))) {
		if (cs_fblank(sp, &cs))
			return (1);
		if (--cnt == 0) {
			if (vp->m_start.lno != cs.cs_lno ||
			    vp->m_start.cno != cs.cs_cno)
				goto okret;
			return (1);
		}
	}

	for (state = NONE;;) {
		if (cs_next(sp, &cs))
			return (1);
		if (cs.cs_flags == CS_EOF)
			break;
		if (cs.cs_flags == CS_EOL) {
			if ((state == PERIOD || state == BLANK) && --cnt == 0) {
				if (cs_next(sp, &cs))
					return (1);
				if (cs.cs_flags == 0 &&
				    isblank(cs.cs_ch) && cs_fblank(sp, &cs))
					return (1);
				goto okret;
			}
			state = NONE;
			continue;
		}
		if (cs.cs_flags == CS_EMP) {	/* An EMP is two sentences. */
			if (--cnt == 0)
				goto okret;
			if (cs_fblank(sp, &cs))
				return (1);
			if (--cnt == 0)
				goto okret;
			state = NONE;
			continue;
		}
		switch (cs.cs_ch) {
		case '.':
		case '?':
		case '!':
			state = PERIOD;
			break;
		case ')':
		case ']':
		case '"':
		case '\'':
			if (state != PERIOD)
				state = NONE;
			break;
		case '\t':
			if (state == PERIOD)
				state = BLANK;
			/* FALLTHROUGH */
		case ' ':
			if (state == PERIOD) {
				state = BLANK;
				break;
			}
			if (state == BLANK && --cnt == 0) {
				if (cs_fblank(sp, &cs))
					return (1);
				goto okret;
			}
			/* FALLTHROUGH */
		default:
			state = NONE;
			break;
		}
	}

	/* EOF is a movement sink, but it's an error not to have moved. */
	if (vp->m_start.lno == cs.cs_lno && vp->m_start.cno == cs.cs_cno) {
		v_eof(sp, NULL);
		return (1);
	}

okret:	vp->m_stop.lno = cs.cs_lno;
	vp->m_stop.cno = cs.cs_cno;

	/*
	 * !!!
	 * Historic, uh, features, yeah, that's right, call 'em features.
	 * If the starting and ending cursor positions are at the first
	 * column in their lines, i.e. the movement is cutting entire lines,
	 * the buffer is in line mode, and the ending position is the last
	 * character of the previous line.  Note check to make sure that
	 * it's not within a single line.
	 *
	 * Non-motion commands move to the end of the range.  Delete and
	 * yank stay at the start.  Ignore others.  Adjust the end of the
	 * range for motion commands.
	 */
	if (ISMOTION(vp)) {
		if (vp->m_start.cno == 0 &&
		    (cs.cs_flags != 0 || vp->m_stop.cno == 0)) {
			if (vp->m_start.lno < vp->m_stop.lno) {
				if (db_get(sp,
				    --vp->m_stop.lno, DBG_FATAL, NULL, &len))
					return (1);
				vp->m_stop.cno = len ? len - 1 : 0;
			}
			F_SET(vp, VM_LMODE);
		} else
			--vp->m_stop.cno;
		vp->m_final = vp->m_start;
	} else
		vp->m_final = vp->m_stop;
	return (0);
}

/*
 * v_sentenceb -- [count](
 *	Move backward count sentences.
 *
 * PUBLIC: int v_sentenceb(SCR *, VICMD *);
 */
int
v_sentenceb(SCR *sp, VICMD *vp)
{
	VCS cs;
	recno_t slno;
	size_t len, scno;
	u_long cnt;
	int last;

	/*
	 * !!!
	 * Historic vi permitted the user to hit SOF repeatedly.
	 */
	if (vp->m_start.lno == 1 && vp->m_start.cno == 0)
		return (0);

	cs.cs_lno = vp->m_start.lno;
	cs.cs_cno = vp->m_start.cno;
	if (cs_init(sp, &cs))
		return (1);

	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;

	/*
	 * !!!
	 * In empty lines, skip to the previous non-white-space character.
	 * If in text, skip to the prevous white-space character.  Believe
	 * it or not, in the paragraph:
	 *	ab cd.
	 *	AB CD.
	 * if the cursor is on the 'A' or 'B', ( moves to the 'a'.  If it
	 * is on the ' ', 'C' or 'D', it moves to the 'A'.  Yes, Virginia,
	 * Berkeley was once a major center of drug activity.
	 */
	if (cs.cs_flags == CS_EMP) {
		if (cs_bblank(sp, &cs))
			return (1);
		for (;;) {
			if (cs_prev(sp, &cs))
				return (1);
			if (cs.cs_flags != CS_EOL)
				break;
		}
	} else if (cs.cs_flags == 0 && !isblank(cs.cs_ch))
		for (;;) {
			if (cs_prev(sp, &cs))
				return (1);
			if (cs.cs_flags != 0 || isblank(cs.cs_ch))
				break;
		}

	for (last = 0;;) {
		if (cs_prev(sp, &cs))
			return (1);
		if (cs.cs_flags == CS_SOF)	/* SOF is a movement sink. */
			break;
		if (cs.cs_flags == CS_EOL) {
			last = 1;
			continue;
		}
		if (cs.cs_flags == CS_EMP) {
			if (--cnt == 0)
				goto ret;
			if (cs_bblank(sp, &cs))
				return (1);
			last = 0;
			continue;
		}
		switch (cs.cs_ch) {
		case '.':
		case '?':
		case '!':
			if (!last || --cnt != 0) {
				last = 0;
				continue;
			}

ret:			slno = cs.cs_lno;
			scno = cs.cs_cno;

			/*
			 * Move to the start of the sentence, skipping blanks
			 * and special characters.
			 */
			do {
				if (cs_next(sp, &cs))
					return (1);
			} while (!cs.cs_flags &&
			    (cs.cs_ch == ')' || cs.cs_ch == ']' ||
			    cs.cs_ch == '"' || cs.cs_ch == '\''));
			if ((cs.cs_flags || isblank(cs.cs_ch)) &&
			    cs_fblank(sp, &cs))
				return (1);

			/*
			 * If it was ".  xyz", with the cursor on the 'x', or
			 * "end.  ", with the cursor in the spaces, or the
			 * beginning of a sentence preceded by an empty line,
			 * we can end up where we started.  Fix it.
			 */
			if (vp->m_start.lno != cs.cs_lno ||
			    vp->m_start.cno != cs.cs_cno)
				goto okret;

			/*
			 * Well, if an empty line preceded possible blanks
			 * and the sentence, it could be a real sentence.
			 */
			for (;;) {
				if (cs_prev(sp, &cs))
					return (1);
				if (cs.cs_flags == CS_EOL)
					continue;
				if (cs.cs_flags == 0 && isblank(cs.cs_ch))
					continue;
				break;
			}
			if (cs.cs_flags == CS_EMP)
				goto okret;

			/* But it wasn't; try again. */
			++cnt;
			cs.cs_lno = slno;
			cs.cs_cno = scno;
			last = 0;
			break;
		case '\t':
			last = 1;
			break;
		default:
			last =
			    cs.cs_flags == CS_EOL || isblank(cs.cs_ch) ||
			    cs.cs_ch == ')' || cs.cs_ch == ']' ||
			    cs.cs_ch == '"' || cs.cs_ch == '\'' ? 1 : 0;
		}
	}

okret:	vp->m_stop.lno = cs.cs_lno;
	vp->m_stop.cno = cs.cs_cno;

	/*
	 * !!!
	 * If the starting and stopping cursor positions are at the first
	 * columns in the line, i.e. the movement is cutting an entire line,
	 * the buffer is in line mode, and the starting position is the last
	 * character of the previous line.
	 *
	 * All commands move to the end of the range.  Adjust the start of
	 * the range for motion commands.
	 */
	if (ISMOTION(vp))
		if (vp->m_start.cno == 0 &&
		    (cs.cs_flags != 0 || vp->m_stop.cno == 0)) {
			if (db_get(sp,
			    --vp->m_start.lno, DBG_FATAL, NULL, &len))
				return (1);
			vp->m_start.cno = len ? len - 1 : 0;
			F_SET(vp, VM_LMODE);
		} else
			--vp->m_start.cno;
	vp->m_final = vp->m_stop;
	return (0);
}
