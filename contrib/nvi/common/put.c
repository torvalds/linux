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
static const char sccsid[] = "$Id: put.c,v 10.19 04/07/11 17:00:24 zy Exp $";
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

#include "common.h"

/*
 * put --
 *	Put text buffer contents into the file.
 *
 * PUBLIC: int put(SCR *, CB *, CHAR_T *, MARK *, MARK *, int);
 */
int
put(
	SCR *sp,
	CB *cbp,
	CHAR_T *namep,
	MARK *cp,
	MARK *rp,
	int append)
{
	CHAR_T name;
	TEXT *ltp, *tp;
	recno_t lno;
	size_t blen, clen, len;
	int rval;
	CHAR_T *bp, *t;
	CHAR_T *p;

	if (cbp == NULL)
		if (namep == NULL) {
			cbp = sp->gp->dcbp;
			if (cbp == NULL) {
				msgq(sp, M_ERR,
				    "053|The default buffer is empty");
				return (1);
			}
		} else {
			name = *namep;
			CBNAME(sp, cbp, name);
			if (cbp == NULL) {
				msgq(sp, M_ERR, "054|Buffer %s is empty",
				    KEY_NAME(sp, name));
				return (1);
			}
		}
	tp = TAILQ_FIRST(cbp->textq);

	/*
	 * It's possible to do a put into an empty file, meaning that the cut
	 * buffer simply becomes the file.  It's a special case so that we can
	 * ignore it in general.
	 *
	 * !!!
	 * Historically, pasting into a file with no lines in vi would preserve
	 * the single blank line.  This is surely a result of the fact that the
	 * historic vi couldn't deal with a file that had no lines in it.  This
	 * implementation treats that as a bug, and does not retain the blank
	 * line.
	 *
	 * Historical practice is that the cursor ends at the first character
	 * in the file.
	 */
	if (cp->lno == 1) {
		if (db_last(sp, &lno))
			return (1);
		if (lno == 0) {
			for (; tp != NULL;
			    ++lno, ++sp->rptlines[L_ADDED], tp = TAILQ_NEXT(tp, q))
				if (db_append(sp, 1, lno, tp->lb, tp->len))
					return (1);
			rp->lno = 1;
			rp->cno = 0;
			return (0);
		}
	}

	/* If a line mode buffer, append each new line into the file. */
	if (F_ISSET(cbp, CB_LMODE)) {
		lno = append ? cp->lno : cp->lno - 1;
		rp->lno = lno + 1;
		for (; tp != NULL;
		    ++lno, ++sp->rptlines[L_ADDED], tp = TAILQ_NEXT(tp, q))
			if (db_append(sp, 1, lno, tp->lb, tp->len))
				return (1);
		rp->cno = 0;
		(void)nonblank(sp, rp->lno, &rp->cno);
		return (0);
	}

	/*
	 * If buffer was cut in character mode, replace the current line with
	 * one built from the portion of the first line to the left of the
	 * split plus the first line in the CB.  Append each intermediate line
	 * in the CB.  Append a line built from the portion of the first line
	 * to the right of the split plus the last line in the CB.
	 *
	 * Get the first line.
	 */
	lno = cp->lno;
	if (db_get(sp, lno, DBG_FATAL, &p, &len))
		return (1);

	GET_SPACE_RETW(sp, bp, blen, tp->len + len + 1);
	t = bp;

	/* Original line, left of the split. */
	if (len > 0 && (clen = cp->cno + (append ? 1 : 0)) > 0) {
		MEMCPY(bp, p, clen);
		p += clen;
		t += clen;
	}

	/* First line from the CB. */
	if (tp->len != 0) {
		MEMCPY(t, tp->lb, tp->len);
		t += tp->len;
	}

	/* Calculate length left in the original line. */
	clen = len == 0 ? 0 : len - (cp->cno + (append ? 1 : 0));

	/*
	 * !!!
	 * In the historical 4BSD version of vi, character mode puts within
	 * a single line have two cursor behaviors: if the put is from the
	 * unnamed buffer, the cursor moves to the character inserted which
	 * appears last in the file.  If the put is from a named buffer,
	 * the cursor moves to the character inserted which appears first
	 * in the file.  In System III/V, it was changed at some point and
	 * the cursor always moves to the first character.  In both versions
	 * of vi, character mode puts that cross line boundaries leave the
	 * cursor on the first character.  Nvi implements the System III/V
	 * behavior, and expect POSIX.2 to do so as well.
	 */
	rp->lno = lno;
	rp->cno = len == 0 ? 0 : sp->cno + (append && tp->len ? 1 : 0);

	/*
	 * If no more lines in the CB, append the rest of the original
	 * line and quit.  Otherwise, build the last line before doing
	 * the intermediate lines, because the line changes will lose
	 * the cached line.
	 */
	if (TAILQ_NEXT(tp, q) == NULL) {
		if (clen > 0) {
			MEMCPY(t, p, clen);
			t += clen;
		}
		if (db_set(sp, lno, bp, t - bp))
			goto err;
		if (sp->rptlchange != lno) {
			sp->rptlchange = lno;
			++sp->rptlines[L_CHANGED];
		}
	} else {
		/*
		 * Have to build both the first and last lines of the
		 * put before doing any sets or we'll lose the cached
		 * line.  Build both the first and last lines in the
		 * same buffer, so we don't have to have another buffer
		 * floating around.
		 *
		 * Last part of original line; check for space, reset
		 * the pointer into the buffer.
		 */
		ltp = TAILQ_LAST(cbp->textq, _texth);
		len = t - bp;
		ADD_SPACE_RETW(sp, bp, blen, ltp->len + clen);
		t = bp + len;

		/* Add in last part of the CB. */
		MEMCPY(t, ltp->lb, ltp->len);
		if (clen)
			MEMCPY(t + ltp->len, p, clen);
		clen += ltp->len;

		/*
		 * Now: bp points to the first character of the first
		 * line, t points to the last character of the last
		 * line, t - bp is the length of the first line, and
		 * clen is the length of the last.  Just figured you'd
		 * want to know.
		 *
		 * Output the line replacing the original line.
		 */
		if (db_set(sp, lno, bp, t - bp))
			goto err;
		if (sp->rptlchange != lno) {
			sp->rptlchange = lno;
			++sp->rptlines[L_CHANGED];
		}

		/* Output any intermediate lines in the CB. */
		for (tp = TAILQ_NEXT(tp, q); TAILQ_NEXT(tp, q) != NULL;
		    ++lno, ++sp->rptlines[L_ADDED], tp = TAILQ_NEXT(tp, q))
			if (db_append(sp, 1, lno, tp->lb, tp->len))
				goto err;

		if (db_append(sp, 1, lno, t, clen))
			goto err;
		++sp->rptlines[L_ADDED];
	}
	rval = 0;

	if (0)
err:		rval = 1;

	FREE_SPACEW(sp, bp, blen);
	return (rval);
}
