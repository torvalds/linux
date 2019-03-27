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
static const char sccsid[] = "$Id: ex_join.c,v 10.17 2004/03/16 14:14:04 skimo Exp $";
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

/*
 * ex_join -- :[line [,line]] j[oin][!] [count] [flags]
 *	Join lines.
 *
 * PUBLIC: int ex_join(SCR *, EXCMD *);
 */
int
ex_join(SCR *sp, EXCMD *cmdp)
{
	recno_t from, to;
	size_t blen, clen, len, tlen;
	int echar = 0, extra, first;
	CHAR_T *bp, *tbp = NULL;
	CHAR_T *p;

	NEEDFILE(sp, cmdp);

	from = cmdp->addr1.lno;
	to = cmdp->addr2.lno;

	/* Check for no lines to join. */
	if (!db_exist(sp, from + 1)) {
		msgq(sp, M_ERR, "131|No following lines to join");
		return (1);
	}

	GET_SPACE_RETW(sp, bp, blen, 256);

	/*
	 * The count for the join command was off-by-one,
	 * historically, to other counts for other commands.
	 */
	if (F_ISSET(cmdp, E_ADDR_DEF) || cmdp->addrcnt == 1)
		++cmdp->addr2.lno;

	clen = tlen = 0;
	for (first = 1,
	    from = cmdp->addr1.lno, to = cmdp->addr2.lno; from <= to; ++from) {
		/*
		 * Get next line.  Historic versions of vi allowed "10J" while
		 * less than 10 lines from the end-of-file, so we do too.
		 */
		if (db_get(sp, from, 0, &p, &len)) {
			cmdp->addr2.lno = from - 1;
			break;
		}

		/* Empty lines just go away. */
		if (len == 0)
			continue;

		/*
		 * Get more space if necessary.  Note, tlen isn't the length
		 * of the new line, it's roughly the amount of space needed.
		 * tbp - bp is the length of the new line.
		 */
		tlen += len + 2;
		ADD_SPACE_RETW(sp, bp, blen, tlen);
		tbp = bp + clen;

		/*
		 * Historic practice:
		 *
		 * If force specified, join without modification.
		 * If the current line ends with whitespace, strip leading
		 *    whitespace from the joined line.
		 * If the next line starts with a ), do nothing.
		 * If the current line ends with ., insert two spaces.
		 * Else, insert one space.
		 *
		 * One change -- add ? and ! to the list of characters for
		 * which we insert two spaces.  I expect that POSIX 1003.2
		 * will require this as well.
		 *
		 * Echar is the last character in the last line joined.
		 */
		extra = 0;
		if (!first && !FL_ISSET(cmdp->iflags, E_C_FORCE)) {
			if (isblank(echar))
				for (; len && isblank(*p); --len, ++p);
			else if (p[0] != ')') {
				if (STRCHR(L(".?!"), echar)) {
					*tbp++ = ' ';
					++clen;
					extra = 1;
				}
				*tbp++ = ' ';
				++clen;
				for (; len && isblank(*p); --len, ++p);
			}
		}

		if (len != 0) {
			MEMCPY(tbp, p, len);
			tbp += len;
			clen += len;
			echar = p[len - 1];
		} else
			echar = ' ';

		/*
		 * Historic practice for vi was to put the cursor at the first
		 * inserted whitespace character, if there was one, or the
		 * first character of the joined line, if there wasn't, or the
		 * last character of the line if joined to an empty line.  If
		 * a count was specified, the cursor was moved as described
		 * for the first line joined, ignoring subsequent lines.  If
		 * the join was a ':' command, the cursor was placed at the
		 * first non-blank character of the line unless the cursor was
		 * "attracted" to the end of line when the command was executed
		 * in which case it moved to the new end of line.  There are
		 * probably several more special cases, but frankly, my dear,
		 * I don't give a damn.  This implementation puts the cursor
		 * on the first inserted whitespace character, the first
		 * character of the joined line, or the last character of the
		 * line regardless.  Note, if the cursor isn't on the joined
		 * line (possible with : commands), it is reset to the starting
		 * line.
		 */
		if (first) {
			sp->cno = (tbp - bp) - (1 + extra);
			first = 0;
		} else
			sp->cno = (tbp - bp) - len - (1 + extra);
	}
	sp->lno = cmdp->addr1.lno;

	/* Delete the joined lines. */
	for (from = cmdp->addr1.lno, to = cmdp->addr2.lno; to > from; --to)
		if (db_delete(sp, to))
			goto err;

	/* If the original line changed, reset it. */
	if (!first && db_set(sp, from, bp, tbp - bp)) {
err:		FREE_SPACEW(sp, bp, blen);
		return (1);
	}
	FREE_SPACEW(sp, bp, blen);

	sp->rptlines[L_JOINED] += (cmdp->addr2.lno - cmdp->addr1.lno) + 1;
	return (0);
}
