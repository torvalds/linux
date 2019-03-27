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
static const char sccsid[] = "$Id: v_ch.c,v 10.11 2011/12/02 19:49:50 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "../common/common.h"
#include "vi.h"

static void notfound(SCR *, ARG_CHAR_T);
static void noprev(SCR *);

/*
 * v_chrepeat -- [count];
 *	Repeat the last F, f, T or t search.
 *
 * PUBLIC: int v_chrepeat(SCR *, VICMD *);
 */
int
v_chrepeat(SCR *sp, VICMD *vp)
{
	vp->character = VIP(sp)->lastckey;

	switch (VIP(sp)->csearchdir) {
	case CNOTSET:
		noprev(sp);
		return (1);
	case FSEARCH:
		return (v_chF(sp, vp));
	case fSEARCH:
		return (v_chf(sp, vp));
	case TSEARCH:
		return (v_chT(sp, vp));
	case tSEARCH:
		return (v_cht(sp, vp));
	default:
		abort();
	}
	/* NOTREACHED */
}

/*
 * v_chrrepeat -- [count],
 *	Repeat the last F, f, T or t search in the reverse direction.
 *
 * PUBLIC: int v_chrrepeat(SCR *, VICMD *);
 */
int
v_chrrepeat(SCR *sp, VICMD *vp)
{
	cdir_t savedir;
	int rval;

	vp->character = VIP(sp)->lastckey;
	savedir = VIP(sp)->csearchdir;

	switch (VIP(sp)->csearchdir) {
	case CNOTSET:
		noprev(sp);
		return (1);
	case FSEARCH:
		rval = v_chf(sp, vp);
		break;
	case fSEARCH:
		rval = v_chF(sp, vp);
		break;
	case TSEARCH:
		rval = v_cht(sp, vp);
		break;
	case tSEARCH:
		rval = v_chT(sp, vp);
		break;
	default:
		abort();
	}
	VIP(sp)->csearchdir = savedir;
	return (rval);
}

/*
 * v_cht -- [count]tc
 *	Search forward in the line for the character before the next
 *	occurrence of the specified character.
 *
 * PUBLIC: int v_cht(SCR *, VICMD *);
 */
int
v_cht(SCR *sp, VICMD *vp)
{
	if (v_chf(sp, vp))
		return (1);

	/*
	 * v_chf places the cursor on the character, where the 't'
	 * command wants it to its left.  We know this is safe since
	 * we had to move right for v_chf() to have succeeded.
	 */
	--vp->m_stop.cno;

	/*
	 * Make any necessary correction to the motion decision made
	 * by the v_chf routine.
	 */
	if (!ISMOTION(vp))
		vp->m_final = vp->m_stop;

	VIP(sp)->csearchdir = tSEARCH;
	return (0);
}

/*
 * v_chf -- [count]fc
 *	Search forward in the line for the next occurrence of the
 *	specified character.
 *
 * PUBLIC: int v_chf(SCR *, VICMD *);
 */
int
v_chf(SCR *sp, VICMD *vp)
{
	size_t len;
	u_long cnt;
	int isempty;
	ARG_CHAR_T key;
	CHAR_T *endp, *p, *startp;

	/*
	 * !!!
	 * If it's a dot command, it doesn't reset the key for which we're
	 * searching, e.g. in "df1|f2|.|;", the ';' searches for a '2'.
	 */
	key = vp->character;
	if (!F_ISSET(vp, VC_ISDOT))
		VIP(sp)->lastckey = key;
	VIP(sp)->csearchdir = fSEARCH;

	if (db_eget(sp, vp->m_start.lno, &p, &len, &isempty)) {
		if (isempty)
			goto empty;
		return (1);
	}

	if (len == 0) {
empty:		notfound(sp, key);
		return (1);
	}

	endp = (startp = p) + len;
	p += vp->m_start.cno;
	for (cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1; cnt--;) {
		while (++p < endp && *p != key);
		if (p == endp) {
			notfound(sp, key);
			return (1);
		}
	}

	vp->m_stop.cno = p - startp;

	/*
	 * Non-motion commands move to the end of the range.
	 * Delete and yank stay at the start, ignore others.
	 */
	vp->m_final = ISMOTION(vp) ? vp->m_start : vp->m_stop;
	return (0);
}

/*
 * v_chT -- [count]Tc
 *	Search backward in the line for the character after the next
 *	occurrence of the specified character.
 *
 * PUBLIC: int v_chT(SCR *, VICMD *);
 */
int
v_chT(SCR *sp, VICMD *vp)
{
	if (v_chF(sp, vp))
		return (1);

	/*
	 * v_chF places the cursor on the character, where the 'T'
	 * command wants it to its right.  We know this is safe since
	 * we had to move left for v_chF() to have succeeded.
	 */
	++vp->m_stop.cno;
	vp->m_final = vp->m_stop;

	VIP(sp)->csearchdir = TSEARCH;
	return (0);
}

/*
 * v_chF -- [count]Fc
 *	Search backward in the line for the next occurrence of the
 *	specified character.
 *
 * PUBLIC: int v_chF(SCR *, VICMD *);
 */
int
v_chF(SCR *sp, VICMD *vp)
{
	size_t len;
	u_long cnt;
	int isempty;
	ARG_CHAR_T key;
	CHAR_T *endp, *p;

	/*
	 * !!!
	 * If it's a dot command, it doesn't reset the key for which
	 * we're searching, e.g. in "df1|f2|.|;", the ';' searches
	 * for a '2'.
	 */
	key = vp->character;
	if (!F_ISSET(vp, VC_ISDOT))
		VIP(sp)->lastckey = key;
	VIP(sp)->csearchdir = FSEARCH;

	if (db_eget(sp, vp->m_start.lno, &p, &len, &isempty)) {
		if (isempty)
			goto empty;
		return (1);
	}

	if (len == 0) {
empty:		notfound(sp, key);
		return (1);
	}

	endp = p - 1;
	p += vp->m_start.cno;
	for (cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1; cnt--;) {
		while (--p > endp && *p != key);
		if (p == endp) {
			notfound(sp, key);
			return (1);
		}
	}

	vp->m_stop.cno = (p - endp) - 1;

	/*
	 * All commands move to the end of the range.  Motion commands
	 * adjust the starting point to the character before the current
	 * one.
	 */
	vp->m_final = vp->m_stop;
	if (ISMOTION(vp))
		--vp->m_start.cno;
	return (0);
}

static void
noprev(SCR *sp)
{
	msgq(sp, M_BERR, "178|No previous F, f, T or t search");
}

static void
notfound(SCR *sp, ARG_CHAR_T ch)
{
	msgq(sp, M_BERR, "179|%s not found", KEY_NAME(sp, ch));
}
