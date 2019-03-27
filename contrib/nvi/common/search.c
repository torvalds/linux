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
static const char sccsid[] = "$Id: search.c,v 10.27 2015/03/13 18:41:35 zy Exp $";
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
#include <unistd.h>

#include "common.h"

typedef enum { S_EMPTY, S_EOF, S_NOPREV, S_NOTFOUND, S_SOF, S_WRAP } smsg_t;

static void	search_msg(SCR *, smsg_t);
static int	search_init(SCR *, dir_t, CHAR_T *, size_t, CHAR_T **, u_int);

/*
 * search_init --
 *	Set up a search.
 */
static int
search_init(
	SCR *sp,
	dir_t dir,
	CHAR_T *ptrn,
	size_t plen,
	CHAR_T **epp,
	u_int flags)
{
	recno_t lno;
	int delim;
	CHAR_T *p, *t;

	/* If the file is empty, it's a fast search. */
	if (sp->lno <= 1) {
		if (db_last(sp, &lno))
			return (1);
		if (lno == 0) {
			if (LF_ISSET(SEARCH_MSG))
				search_msg(sp, S_EMPTY);
			return (1);
		}
	}

	if (LF_ISSET(SEARCH_PARSE)) {		/* Parse the string. */
		/*
		 * Use the saved pattern if no pattern specified, or if only
		 * one or two delimiter characters specified.
		 *
		 * !!!
		 * Historically, only the pattern itself was saved, vi didn't
		 * preserve addressing or delta information.
		 */
		if (ptrn == NULL)
			goto prev;
		if (plen == 1) {
			if (epp != NULL)
				*epp = ptrn + 1;
			goto prev;
		}
		if (ptrn[0] == ptrn[1]) {
			if (epp != NULL)
				*epp = ptrn + 2;

			/* Complain if we don't have a previous pattern. */
prev:			if (sp->re == NULL) {
				search_msg(sp, S_NOPREV);
				return (1);
			}
			/* Re-compile the search pattern if necessary. */
			if (!F_ISSET(sp, SC_RE_SEARCH) && re_compile(sp,
			    sp->re, sp->re_len, NULL, NULL, &sp->re_c,
			    RE_C_SEARCH |
			    (LF_ISSET(SEARCH_MSG) ? 0 : RE_C_SILENT)))
				return (1);

			/* Set the search direction. */
			if (LF_ISSET(SEARCH_SET))
				sp->searchdir = dir;
			return (0);
		}

		/*
		 * Set the delimiter, and move forward to the terminating
		 * delimiter, handling escaped delimiters.
		 *
		 * QUOTING NOTE:
		 * Only discard an escape character if it escapes a delimiter.
		 */
		for (delim = *ptrn, p = t = ++ptrn;; *t++ = *p++) {
			if (--plen == 0 || p[0] == delim) {
				if (plen != 0)
					++p;
				break;
			}
			if (plen > 1 && p[0] == '\\' && p[1] == delim) {
				++p;
				--plen;
			}
		}
		if (epp != NULL)
			*epp = p;

		plen = t - ptrn;
	}

	/* Compile the RE. */
	if (re_compile(sp, ptrn, plen, &sp->re, &sp->re_len, &sp->re_c,
	    RE_C_SEARCH |
	    (LF_ISSET(SEARCH_MSG) ? 0 : RE_C_SILENT) |
	    (LF_ISSET(SEARCH_TAG) ? RE_C_TAG : 0) |
	    (LF_ISSET(SEARCH_CSCOPE) ? RE_C_CSCOPE : 0)))
		return (1);

	/* Set the search direction. */
	if (LF_ISSET(SEARCH_SET))
		sp->searchdir = dir;

	return (0);
}

/*
 * f_search --
 *	Do a forward search.
 *
 * PUBLIC: int f_search(SCR *,
 * PUBLIC:    MARK *, MARK *, CHAR_T *, size_t, CHAR_T **, u_int);
 */
int
f_search(
	SCR *sp,
	MARK *fm,
	MARK *rm,
	CHAR_T *ptrn,
	size_t plen,
	CHAR_T **eptrn,
	u_int flags)
{
	busy_t btype;
	recno_t lno;
	regmatch_t match[1];
	size_t coff, len;
	int cnt, eval, rval, wrapped = 0;
	CHAR_T *l;

	if (search_init(sp, FORWARD, ptrn, plen, eptrn, flags))
		return (1);

	if (LF_ISSET(SEARCH_FILE)) {
		lno = 1;
		coff = 0;
	} else {
		if (db_get(sp, fm->lno, DBG_FATAL, &l, &len))
			return (1);
		lno = fm->lno;

		/*
		 * If doing incremental search, start searching at the previous
		 * column, so that we search a minimal distance and still match
		 * special patterns, e.g., \< for beginning of a word.
		 *
		 * Otherwise, start searching immediately after the cursor.  If
		 * at the end of the line, start searching on the next line.
		 * This is incompatible (read bug fix) with the historic vi --
		 * searches for the '$' pattern never moved forward, and the
		 * "-t foo" didn't work if the 'f' was the first character in
		 * the file.
		 */
		if (LF_ISSET(SEARCH_INCR)) {
			if ((coff = fm->cno) != 0)
				--coff;
		} else if (fm->cno + 1 >= len) {
			coff = 0;
			lno = fm->lno + 1;
			if (db_get(sp, lno, 0, &l, &len)) {
				if (!O_ISSET(sp, O_WRAPSCAN)) {
					if (LF_ISSET(SEARCH_MSG))
						search_msg(sp, S_EOF);
					return (1);
				}
				lno = 1;
				wrapped = 1;
			}
		} else
			coff = fm->cno + 1;
	}

	btype = BUSY_ON;
	for (cnt = INTERRUPT_CHECK, rval = 1;; ++lno, coff = 0) {
		if (cnt-- == 0) {
			if (INTERRUPTED(sp))
				break;
			if (LF_ISSET(SEARCH_MSG)) {
				search_busy(sp, btype);
				btype = BUSY_UPDATE;
			}
			cnt = INTERRUPT_CHECK;
		}
		if ((wrapped && lno > fm->lno) || db_get(sp, lno, 0, &l, &len)) {
			if (wrapped) {
				if (LF_ISSET(SEARCH_MSG))
					search_msg(sp, S_NOTFOUND);
				break;
			}
			if (!O_ISSET(sp, O_WRAPSCAN)) {
				if (LF_ISSET(SEARCH_MSG))
					search_msg(sp, S_EOF);
				break;
			}
			lno = 0;
			wrapped = 1;
			continue;
		}

		/* If already at EOL, just keep going. */
		if (len != 0 && coff == len)
			continue;

		/* Set the termination. */
		match[0].rm_so = coff;
		match[0].rm_eo = len;

#if defined(DEBUG) && 0
		TRACE(sp, "F search: %lu from %u to %u\n",
		    lno, coff, len != 0 ? len - 1 : len);
#endif
		/* Search the line. */
		eval = regexec(&sp->re_c, l, 1, match,
		    (match[0].rm_so == 0 ? 0 : REG_NOTBOL) | REG_STARTEND);
		if (eval == REG_NOMATCH)
			continue;
		if (eval != 0) {
			if (LF_ISSET(SEARCH_MSG))
				re_error(sp, eval, &sp->re_c);
			else
				(void)sp->gp->scr_bell(sp);
			break;
		}

		/* Warn if the search wrapped. */
		if (wrapped && LF_ISSET(SEARCH_WMSG))
			search_msg(sp, S_WRAP);

#if defined(DEBUG) && 0
		TRACE(sp, "F search: %qu to %qu\n",
		    match[0].rm_so, match[0].rm_eo);
#endif
		rm->lno = lno;
		rm->cno = match[0].rm_so;

		/*
		 * If a change command, it's possible to move beyond the end
		 * of a line.  Historic vi generally got this wrong (e.g. try
		 * "c?$<cr>").  Not all that sure this gets it right, there
		 * are lots of strange cases.
		 */
		if (!LF_ISSET(SEARCH_EOL) && rm->cno >= len)
			rm->cno = len != 0 ? len - 1 : 0;

		rval = 0;
		break;
	}

	if (LF_ISSET(SEARCH_MSG))
		search_busy(sp, BUSY_OFF);
	return (rval);
}

/*
 * b_search --
 *	Do a backward search.
 *
 * PUBLIC: int b_search(SCR *,
 * PUBLIC:    MARK *, MARK *, CHAR_T *, size_t, CHAR_T **, u_int);
 */
int
b_search(
	SCR *sp,
	MARK *fm,
	MARK *rm,
	CHAR_T *ptrn,
	size_t plen,
	CHAR_T **eptrn,
	u_int flags)
{
	busy_t btype;
	recno_t lno;
	regmatch_t match[1];
	size_t coff, last, len;
	int cnt, eval, rval, wrapped;
	CHAR_T *l;

	if (search_init(sp, BACKWARD, ptrn, plen, eptrn, flags))
		return (1);

	/*
	 * If doing incremental search, set the "starting" position past the
	 * current column, so that we search a minimal distance and still
	 * match special patterns, e.g., \> for the end of a word.  This is
	 * safe when the cursor is at the end of a line because we only use
	 * it for comparison with the location of the match.
	 *
	 * Otherwise, start searching immediately before the cursor.  If in
	 * the first column, start search on the previous line.
	 */
	if (LF_ISSET(SEARCH_INCR)) {
		lno = fm->lno;
		coff = fm->cno + 1;
	} else {
		if (fm->cno == 0) {
			if (fm->lno == 1 && !O_ISSET(sp, O_WRAPSCAN)) {
				if (LF_ISSET(SEARCH_MSG))
					search_msg(sp, S_SOF);
				return (1);
			}
			lno = fm->lno - 1;
		} else
			lno = fm->lno;
		coff = fm->cno;
	}

	btype = BUSY_ON;
	for (cnt = INTERRUPT_CHECK, rval = 1, wrapped = 0;; --lno, coff = 0) {
		if (cnt-- == 0) {
			if (INTERRUPTED(sp))
				break;
			if (LF_ISSET(SEARCH_MSG)) {
				search_busy(sp, btype);
				btype = BUSY_UPDATE;
			}
			cnt = INTERRUPT_CHECK;
		}
		if ((wrapped && lno < fm->lno) || lno == 0) {
			if (wrapped) {
				if (LF_ISSET(SEARCH_MSG))
					search_msg(sp, S_NOTFOUND);
				break;
			}
			if (!O_ISSET(sp, O_WRAPSCAN)) {
				if (LF_ISSET(SEARCH_MSG))
					search_msg(sp, S_SOF);
				break;
			}
			if (db_last(sp, &lno))
				break;
			if (lno == 0) {
				if (LF_ISSET(SEARCH_MSG))
					search_msg(sp, S_EMPTY);
				break;
			}
			++lno;
			wrapped = 1;
			continue;
		}

		if (db_get(sp, lno, 0, &l, &len))
			break;

		/* Set the termination. */
		match[0].rm_so = 0;
		match[0].rm_eo = len;

#if defined(DEBUG) && 0
		TRACE(sp, "B search: %lu from 0 to %qu\n", lno, match[0].rm_eo);
#endif
		/* Search the line. */
		eval = regexec(&sp->re_c, l, 1, match,
		    (match[0].rm_eo == len ? 0 : REG_NOTEOL) | REG_STARTEND);
		if (eval == REG_NOMATCH)
			continue;
		if (eval != 0) {
			if (LF_ISSET(SEARCH_MSG))
				re_error(sp, eval, &sp->re_c);
			else
				(void)sp->gp->scr_bell(sp);
			break;
		}

		/* Check for a match starting past the cursor. */
		if (coff != 0 && match[0].rm_so >= coff)
			continue;

		/* Warn if the search wrapped. */
		if (wrapped && LF_ISSET(SEARCH_WMSG))
			search_msg(sp, S_WRAP);

#if defined(DEBUG) && 0
		TRACE(sp, "B found: %qu to %qu\n",
		    match[0].rm_so, match[0].rm_eo);
#endif
		/*
		 * We now have the first match on the line.  Step through the
		 * line character by character until find the last acceptable
		 * match.  This is painful, we need a better interface to regex
		 * to make this work.
		 */
		for (;;) {
			last = match[0].rm_so++;
			if (match[0].rm_so >= len)
				break;
			match[0].rm_eo = len;
			eval = regexec(&sp->re_c, l, 1, match,
			    (match[0].rm_so == 0 ? 0 : REG_NOTBOL) |
			    REG_STARTEND);
			if (eval == REG_NOMATCH)
				break;
			if (eval != 0) {
				if (LF_ISSET(SEARCH_MSG))
					re_error(sp, eval, &sp->re_c);
				else
					(void)sp->gp->scr_bell(sp);
				goto err;
			}
			if (coff && match[0].rm_so >= coff)
				break;
		}
		rm->lno = lno;

		/* See comment in f_search(). */
		if (!LF_ISSET(SEARCH_EOL) && last >= len)
			rm->cno = len != 0 ? len - 1 : 0;
		else
			rm->cno = last;
		rval = 0;
		break;
	}

err:	if (LF_ISSET(SEARCH_MSG))
		search_busy(sp, BUSY_OFF);
	return (rval);
}

/*
 * search_msg --
 *	Display one of the search messages.
 */
static void
search_msg(
	SCR *sp,
	smsg_t msg)
{
	switch (msg) {
	case S_EMPTY:
		msgq(sp, M_ERR, "072|File empty; nothing to search");
		break;
	case S_EOF:
		msgq(sp, M_ERR,
		    "073|Reached end-of-file without finding the pattern");
		break;
	case S_NOPREV:
		msgq(sp, M_ERR, "074|No previous search pattern");
		break;
	case S_NOTFOUND:
		msgq(sp, M_ERR, "075|Pattern not found");
		break;
	case S_SOF:
		msgq(sp, M_ERR,
		    "076|Reached top-of-file without finding the pattern");
		break;
	case S_WRAP:
		msgq(sp, M_ERR, "077|Search wrapped");
		break;
	default:
		abort();
	}
}

/*
 * search_busy --
 *	Put up the busy searching message.
 *
 * PUBLIC: void search_busy(SCR *, busy_t);
 */
void
search_busy(
	SCR *sp,
	busy_t btype)
{
	sp->gp->scr_busy(sp, "078|Searching...", btype);
}
