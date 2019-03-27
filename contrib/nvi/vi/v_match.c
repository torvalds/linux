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
static const char sccsid[] = "$Id: v_match.c,v 10.11 2012/02/11 00:33:46 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"
#include "vi.h"

/*
 * v_match -- %
 *	Search to matching character.
 *
 * PUBLIC: int v_match(SCR *, VICMD *);
 */
int
v_match(SCR *sp, VICMD *vp)
{
	VCS cs;
	MARK *mp;
	size_t cno, len, off;
	int cnt, isempty, matchc, startc, (*gc)(SCR *, VCS *);
	CHAR_T *p;
	CHAR_T *cp;
	const CHAR_T *match_chars;

	/*
	 * Historically vi would match (), {} and [] however
	 * an update included <>.  This is ok for editing HTML
	 * but a pain in the butt for C source.
	 * Making it an option lets the user decide what is 'right'.
	 */
	match_chars = VIP(sp)->mcs;

	/*
	 * !!!
	 * Historic practice; ignore the count.
	 *
	 * !!!
	 * Historical practice was to search for the initial character in the
	 * forward direction only.
	 */
	if (db_eget(sp, vp->m_start.lno, &p, &len, &isempty)) {
		if (isempty)
			goto nomatch;
		return (1);
	}
	for (off = vp->m_start.cno;; ++off) {
		if (off >= len) {
nomatch:		msgq(sp, M_BERR, "184|No match character on this line");
			return (1);
		}
		startc = p[off];
		cp = STRCHR(match_chars, startc);
		if (cp != NULL) {
			cnt = cp - match_chars;
			matchc = match_chars[cnt ^ 1];
			gc = cnt & 1 ? cs_prev : cs_next;
			break;
		}
	}

	cs.cs_lno = vp->m_start.lno;
	cs.cs_cno = off;
	if (cs_init(sp, &cs))
		return (1);
	for (cnt = 1;;) {
		if (gc(sp, &cs))
			return (1);
		if (cs.cs_flags != 0) {
			if (cs.cs_flags == CS_EOF || cs.cs_flags == CS_SOF)
				break;
			continue;
		}
		if (cs.cs_ch == startc)
			++cnt;
		else if (cs.cs_ch == matchc && --cnt == 0)
			break;
	}
	if (cnt) {
		msgq(sp, M_BERR, "185|Matching character not found");
		return (1);
	}

	vp->m_stop.lno = cs.cs_lno;
	vp->m_stop.cno = cs.cs_cno;

	/*
	 * If moving right, non-motion commands move to the end of the range.
	 * Delete and yank stay at the start.
	 *
	 * If moving left, all commands move to the end of the range.
	 *
	 * !!!
	 * Don't correct for leftward movement -- historic vi deleted the
	 * starting cursor position when deleting to a match.
	 */
	if (vp->m_start.lno < vp->m_stop.lno ||
	    (vp->m_start.lno == vp->m_stop.lno &&
	    vp->m_start.cno < vp->m_stop.cno))
		vp->m_final = ISMOTION(vp) ? vp->m_start : vp->m_stop;
	else
		vp->m_final = vp->m_stop;

	/*
	 * !!!
	 * If the motion is across lines, and the earliest cursor position
	 * is at or before any non-blank characters in the line, i.e. the
	 * movement is cutting all of the line's text, and the later cursor
	 * position has nothing other than whitespace characters between it
	 * and the end of its line, the buffer is in line mode.
	 */
	if (!ISMOTION(vp) || vp->m_start.lno == vp->m_stop.lno)
		return (0);
	mp = vp->m_start.lno < vp->m_stop.lno ? &vp->m_start : &vp->m_stop;
	if (mp->cno != 0) {
		cno = 0;
		if (nonblank(sp, mp->lno, &cno))
			return (1);
		if (cno < mp->cno)
			return (0);
	}
	mp = vp->m_start.lno < vp->m_stop.lno ? &vp->m_stop : &vp->m_start;
	if (db_get(sp, mp->lno, DBG_FATAL, &p, &len))
		return (1);
	for (p += mp->cno + 1, len -= mp->cno; --len; ++p)
		if (!isblank(*p))
			return (0);
	F_SET(vp, VM_LMODE);
	return (0);
}

/*
 * v_buildmcs --
 *	Build the match character list.
 *
 * PUBLIC: int v_buildmcs(SCR *, char *);
 */
int
v_buildmcs(SCR *sp, char *str)
{
	CHAR_T **mp = &VIP(sp)->mcs;
	size_t len = strlen(str) + 1;

	if (*mp != NULL)
		free(*mp);
	MALLOC(sp, *mp, CHAR_T *, len * sizeof(CHAR_T));
	if (*mp == NULL)
		return (1);
#ifdef USE_WIDECHAR
	if (mbstowcs(*mp, str, len) == (size_t)-1)
		return (1);
#else
	memcpy(*mp, str, len);
#endif
	return (0);
}
