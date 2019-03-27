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
static const char sccsid[] = "$Id: v_replace.c,v 10.24 2001/06/25 15:19:34 skimo Exp $";
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
 * v_replace -- [count]r<char>
 *
 * !!!
 * The r command in historic vi was almost beautiful in its badness.  For
 * example, "r<erase>" and "r<word erase>" beeped the terminal and deleted
 * a single character.  "Nr<carriage return>", where N was greater than 1,
 * inserted a single carriage return.  "r<escape>" did cancel the command,
 * but "r<literal><escape>" erased a single character.  To enter a literal
 * <literal> character, it required three <literal> characters after the
 * command.  This may not be right, but at least it's not insane.
 *
 * PUBLIC: int v_replace(SCR *, VICMD *);
 */
int
v_replace(SCR *sp, VICMD *vp)
{
	EVENT ev;
	VI_PRIVATE *vip;
	TEXT *tp;
	size_t blen, len;
	u_long cnt;
	int quote, rval;
	CHAR_T *bp;
	CHAR_T *p;

	vip = VIP(sp);

	/*
	 * If the line doesn't exist, or it's empty, replacement isn't
	 * allowed.  It's not hard to implement, but:
	 *
	 *	1: It's historic practice (vi beeped before the replacement
	 *	   character was even entered).
	 *	2: For consistency, this change would require that the more
	 *	   general case, "Nr", when the user is < N characters from
	 *	   the end of the line, also work, which would be a bit odd.
	 *	3: Replacing with a <newline> has somewhat odd semantics.
	 */
	if (db_get(sp, vp->m_start.lno, DBG_FATAL, &p, &len))
		return (1);
	if (len == 0) {
		msgq(sp, M_BERR, "186|No characters to replace");
		return (1);
	}

	/*
	 * Figure out how many characters to be replace.  For no particular
	 * reason (other than that the semantics of replacing the newline
	 * are confusing) only permit the replacement of the characters in
	 * the current line.  I suppose we could append replacement characters
	 * to the line, but I see no compelling reason to do so.  Check this
	 * before we get the character to match historic practice, where Nr
	 * failed immediately if there were less than N characters from the
	 * cursor to the end of the line.
	 */
	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	vp->m_stop.lno = vp->m_start.lno;
	vp->m_stop.cno = vp->m_start.cno + cnt - 1;
	if (vp->m_stop.cno > len - 1) {
		v_eol(sp, &vp->m_start);
		return (1);
	}

	/*
	 * If it's not a repeat, reset the current mode and get a replacement
	 * character.
	 */
	quote = 0;
	if (!F_ISSET(vp, VC_ISDOT)) {
		sp->showmode = SM_REPLACE;
		if (vs_refresh(sp, 0))
			return (1);
next:		if (v_event_get(sp, &ev, 0, 0))
			return (1);

		switch (ev.e_event) {
		case E_CHARACTER:
			/*
			 * <literal_next> means escape the next character.
			 * <escape> means they changed their minds.
			 */
			if (!quote) {
				if (ev.e_value == K_VLNEXT) {
					quote = 1;
					goto next;
				}
				if (ev.e_value == K_ESCAPE)
					return (0);
			}
			vip->rlast = ev.e_c;
			vip->rvalue = ev.e_value;
			break;
		case E_ERR:
		case E_EOF:
			F_SET(sp, SC_EXIT_FORCE);
			return (1);
		case E_INTERRUPT:
			/* <interrupt> means they changed their minds. */
			return (0);
		case E_WRESIZE:
			/* <resize> interrupts the input mode. */
			v_emsg(sp, NULL, VIM_WRESIZE);
			return (0);
		case E_REPAINT:
			if (vs_repaint(sp, &ev))
				return (1);
			goto next;
		default:
			v_event_err(sp, &ev);
			return (0);
		}
	}

	/* Copy the line. */
	GET_SPACE_RETW(sp, bp, blen, len);
	MEMMOVE(bp, p, len);
	p = bp;

	/*
	 * Versions of nvi before 1.57 created N new lines when they replaced
	 * N characters with <carriage-return> or <newline> characters.  This
	 * is different from the historic vi, which replaced N characters with
	 * a single new line.  Users complained, so we match historic practice.
	 */
	if ((!quote && vip->rvalue == K_CR) || vip->rvalue == K_NL) {
		/* Set return line. */
		vp->m_stop.lno = vp->m_start.lno + 1;
		vp->m_stop.cno = 0;

		/* The first part of the current line. */
		if (db_set(sp, vp->m_start.lno, p, vp->m_start.cno))
			goto err_ret;

		/*
		 * The rest of the current line.  And, of course, now it gets
		 * tricky.  If there are characters left in the line and if
		 * the autoindent edit option is set, white space after the
		 * replaced character is discarded, autoindent is applied, and
		 * the cursor moves to the last indent character.
		 */
		p += vp->m_start.cno + cnt;
		len -= vp->m_start.cno + cnt;
		if (len != 0 && O_ISSET(sp, O_AUTOINDENT))
			for (; len && isblank(*p); --len, ++p);

		if ((tp = text_init(sp, p, len, len)) == NULL)
			goto err_ret;

		if (len != 0 && O_ISSET(sp, O_AUTOINDENT)) {
			if (v_txt_auto(sp, vp->m_start.lno, NULL, 0, tp))
				goto err_ret;
			vp->m_stop.cno = tp->ai ? tp->ai - 1 : 0;
		} else
			vp->m_stop.cno = 0;

		vp->m_stop.cno = tp->ai ? tp->ai - 1 : 0;
		if (db_append(sp, 1, vp->m_start.lno, tp->lb, tp->len))
err_ret:		rval = 1;
		else {
			text_free(tp);
			rval = 0;
		}
	} else {
		STRSET(bp + vp->m_start.cno, vip->rlast, cnt);
		rval = db_set(sp, vp->m_start.lno, bp, len);
	}
	FREE_SPACEW(sp, bp, blen);

	vp->m_final = vp->m_stop;
	return (rval);
}
