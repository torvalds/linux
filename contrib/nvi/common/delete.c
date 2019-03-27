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
static const char sccsid[] = "$Id: delete.c,v 10.18 2012/02/11 15:52:33 zy Exp $";
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

#include "common.h"

/*
 * del --
 *	Delete a range of text.
 *
 * PUBLIC: int del(SCR *, MARK *, MARK *, int);
 */
int
del(
	SCR *sp,
	MARK *fm,
	MARK *tm,
	int lmode)
{
	recno_t lno;
	size_t blen, len, nlen, tlen;
	CHAR_T *bp, *p;
	int eof, rval;

	bp = NULL;

	/* Case 1 -- delete in line mode. */
	if (lmode) {
		for (lno = tm->lno; lno >= fm->lno; --lno) {
			if (db_delete(sp, lno))
				return (1);
			++sp->rptlines[L_DELETED];
			if (lno % INTERRUPT_CHECK == 0 && INTERRUPTED(sp))
				break;
		}
		goto done;
	}

	/*
	 * Case 2 -- delete to EOF.  This is a special case because it's
	 * easier to pick it off than try and find it in the other cases.
 	 */
	if (db_last(sp, &lno))
		return (1);
	if (tm->lno >= lno) {
		if (tm->lno == lno) {
			if (db_get(sp, lno, DBG_FATAL, &p, &len))
				return (1);
			eof = tm->cno != ENTIRE_LINE && tm->cno >= len ? 1 : 0;
		} else
			eof = 1;
		if (eof) {
			for (lno = tm->lno; lno > fm->lno; --lno) {
				if (db_delete(sp, lno))
					return (1);
				++sp->rptlines[L_DELETED];
				if (lno %
				    INTERRUPT_CHECK == 0 && INTERRUPTED(sp))
					break;
			}
			if (db_get(sp, fm->lno, DBG_FATAL, &p, &len))
				return (1);
			GET_SPACE_RETW(sp, bp, blen, fm->cno);
			MEMCPY(bp, p, fm->cno);
			if (db_set(sp, fm->lno, bp, fm->cno))
				return (1);
			goto done;
		}
	}

	/* Case 3 -- delete within a single line. */
	if (tm->lno == fm->lno) {
		if (db_get(sp, fm->lno, DBG_FATAL, &p, &len))
			return (1);
		GET_SPACE_RETW(sp, bp, blen, len);
		if (fm->cno != 0)
			MEMCPY(bp, p, fm->cno);
		MEMCPY(bp + fm->cno, p + (tm->cno + 1), 
			len - (tm->cno + 1));
		if (db_set(sp, fm->lno,
		    bp, len - ((tm->cno - fm->cno) + 1)))
			goto err;
		goto done;
	}

	/*
	 * Case 4 -- delete over multiple lines.
	 *
	 * Copy the start partial line into place.
	 */
	if ((tlen = fm->cno) != 0) {
		if (db_get(sp, fm->lno, DBG_FATAL, &p, NULL))
			return (1);
		GET_SPACE_RETW(sp, bp, blen, tlen + 256);
		MEMCPY(bp, p, tlen);
	}

	/* Copy the end partial line into place. */
	if (db_get(sp, tm->lno, DBG_FATAL, &p, &len))
		goto err;
	if (len != 0 && tm->cno != len - 1) {
		/*
		 * XXX
		 * We can overflow memory here, if the total length is greater
		 * than SIZE_T_MAX.  The only portable way I've found to test
		 * is depending on the overflow being less than the value.
		 */
		nlen = (len - (tm->cno + 1)) + tlen;
		if (tlen > nlen) {
			msgq(sp, M_ERR, "002|Line length overflow");
			goto err;
		}
		if (tlen == 0) {
			GET_SPACE_RETW(sp, bp, blen, nlen);
		} else
			ADD_SPACE_RETW(sp, bp, blen, nlen);

		MEMCPY(bp + tlen, p + (tm->cno + 1), len - (tm->cno + 1));
		tlen += len - (tm->cno + 1);
	}

	/* Set the current line. */
	if (db_set(sp, fm->lno, bp, tlen))
		goto err;

	/* Delete the last and intermediate lines. */
	for (lno = tm->lno; lno > fm->lno; --lno) {
		if (db_delete(sp, lno))
			goto err;
		++sp->rptlines[L_DELETED];
		if (lno % INTERRUPT_CHECK == 0 && INTERRUPTED(sp))
			break;
	}

done:	rval = 0;
	if (0)
err:		rval = 1;
	if (bp != NULL)
		FREE_SPACEW(sp, bp, blen);
	return (rval);
}
