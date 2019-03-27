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
static const char sccsid[] = "$Id: v_undo.c,v 10.6 2001/06/25 15:19:36 skimo Exp $";
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

/*
 * v_Undo -- U
 *	Undo changes to this line.
 *
 * PUBLIC: int v_Undo(SCR *, VICMD *);
 */
int
v_Undo(SCR *sp, VICMD *vp)
{
	/*
	 * Historically, U reset the cursor to the first column in the line
	 * (not the first non-blank).  This seems a bit non-intuitive, but,
	 * considering that we may have undone multiple changes, anything
	 * else (including the cursor position stored in the logging records)
	 * is going to appear random.
	 */
	vp->m_final.cno = 0;

	/*
	 * !!!
	 * Set up the flags so that an immediately subsequent 'u' will roll
	 * forward, instead of backward.  In historic vi, a 'u' following a
	 * 'U' redid all of the changes to the line.  Given that the user has
	 * explicitly discarded those changes by entering 'U', it seems likely
	 * that the user wants something between the original and end forms of
	 * the line, so starting to replay the changes seems the best way to
	 * get to there.
	 */
	F_SET(sp->ep, F_UNDO);
	sp->ep->lundo = BACKWARD;

	return (log_setline(sp));
}

/*
 * v_undo -- u
 *	Undo the last change.
 *
 * PUBLIC: int v_undo(SCR *, VICMD *);
 */
int
v_undo(SCR *sp, VICMD *vp)
{
	EXF *ep;

	/* Set the command count. */
	VIP(sp)->u_ccnt = sp->ccnt;

	/*
	 * !!!
	 * In historic vi, 'u' toggled between "undo" and "redo", i.e. 'u'
	 * undid the last undo.  However, if there has been a change since
	 * the last undo/redo, we always do an undo.  To make this work when
	 * the user can undo multiple operations, we leave the old semantic
	 * unchanged, but make '.' after a 'u' do another undo/redo operation.
	 * This has two problems.
	 *
	 * The first is that 'u' didn't set '.' in historic vi.  So, if a
	 * user made a change, realized it was in the wrong place, does a
	 * 'u' to undo it, moves to the right place and then does '.', the
	 * change was reapplied.  To make this work, we only apply the '.'
	 * to the undo command if it's the command immediately following an
	 * undo command.  See vi/vi.c:getcmd() for the details.
	 *
	 * The second is that the traditional way to view the numbered cut
	 * buffers in vi was to enter the commands "1pu.u.u.u. which will
	 * no longer work because the '.' immediately follows the 'u' command.
	 * Since we provide a much better method of viewing buffers, and
	 * nobody can think of a better way of adding in multiple undo, this
	 * remains broken.
	 *
	 * !!!
	 * There is change to historic practice for the final cursor position
	 * in this implementation.  In historic vi, if an undo was isolated to
	 * a single line, the cursor moved to the start of the change, and
	 * then, subsequent 'u' commands would not move it again. (It has been
	 * pointed out that users used multiple undo commands to get the cursor
	 * to the start of the changed text.)  Nvi toggles between the cursor
	 * position before and after the change was made.  One final issue is
	 * that historic vi only did this if the user had not moved off of the
	 * line before entering the undo command; otherwise, vi would move the
	 * cursor to the most attractive position on the changed line.
	 *
	 * It would be difficult to match historic practice in this area. You
	 * not only have to know that the changes were isolated to one line,
	 * but whether it was the first or second undo command as well.  And,
	 * to completely match historic practice, we'd have to track users line
	 * changes, too.  This isn't worth the effort.
	 */
	ep = sp->ep;
	if (!F_ISSET(ep, F_UNDO)) {
		F_SET(ep, F_UNDO);
		ep->lundo = BACKWARD;
	} else if (!F_ISSET(vp, VC_ISDOT))
		ep->lundo = ep->lundo == BACKWARD ? FORWARD : BACKWARD;

	switch (ep->lundo) {
	case BACKWARD:
		return (log_backward(sp, &vp->m_final));
	case FORWARD:
		return (log_forward(sp, &vp->m_final));
	default:
		abort();
	}
	/* NOTREACHED */
}
