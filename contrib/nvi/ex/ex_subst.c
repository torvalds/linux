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
static const char sccsid[] = "$Id: ex_subst.c,v 10.53 2011/12/21 20:40:35 zy Exp $";
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

#include "../common/common.h"
#include "../vi/vi.h"

#define	SUB_FIRST	0x01		/* The 'r' flag isn't reasonable. */
#define	SUB_MUSTSETR	0x02		/* The 'r' flag is required. */

static int re_conv(SCR *, CHAR_T **, size_t *, int *);
static int re_cscope_conv(SCR *, CHAR_T **, size_t *, int *);
static int re_sub(SCR *,
		CHAR_T *, CHAR_T **, size_t *, size_t *, regmatch_t [10]);
static int re_tag_conv(SCR *, CHAR_T **, size_t *, int *);
static int s(SCR *, EXCMD *, CHAR_T *, regex_t *, u_int);

/*
 * ex_s --
 *	[line [,line]] s[ubstitute] [[/;]pat[/;]/repl[/;] [cgr] [count] [#lp]]
 *
 *	Substitute on lines matching a pattern.
 *
 * PUBLIC: int ex_s(SCR *, EXCMD *);
 */
int
ex_s(SCR *sp, EXCMD *cmdp)
{
	regex_t *re;
	size_t blen, len;
	u_int flags;
	int delim;
	CHAR_T *bp, *p, *ptrn, *rep, *t;

	/*
	 * Skip leading white space.
	 *
	 * !!!
	 * Historic vi allowed any non-alphanumeric to serve as the
	 * substitution command delimiter.
	 *
	 * !!!
	 * If the arguments are empty, it's the same as &, i.e. we
	 * repeat the last substitution.
	 */
	if (cmdp->argc == 0)
		goto subagain;
	for (p = cmdp->argv[0]->bp,
	    len = cmdp->argv[0]->len; len > 0; --len, ++p) {
		if (!cmdskip(*p))
			break;
	}
	if (len == 0)
subagain:	return (ex_subagain(sp, cmdp));

	delim = *p++;
	if (!isascii(delim) || isalnum(delim) || delim == '\\')
		return (s(sp, cmdp, p, &sp->subre_c, SUB_MUSTSETR));

	/*
	 * !!!
	 * The full-blown substitute command reset the remembered
	 * state of the 'c' and 'g' suffices.
	 */
	sp->c_suffix = sp->g_suffix = 0;

	/*
	 * Get the pattern string, toss escaping characters.
	 *
	 * !!!
	 * Historic vi accepted any of the following forms:
	 *
	 *	:s/abc/def/		change "abc" to "def"
	 *	:s/abc/def		change "abc" to "def"
	 *	:s/abc/			delete "abc"
	 *	:s/abc			delete "abc"
	 *
	 * QUOTING NOTE:
	 *
	 * Only toss an escaping character if it escapes a delimiter.
	 * This means that "s/A/\\\\f" replaces "A" with "\\f".  It
	 * would be nice to be more regular, i.e. for each layer of
	 * escaping a single escaping character is removed, but that's
	 * not how the historic vi worked.
	 */
	for (ptrn = t = p;;) {
		if (p[0] == '\0' || p[0] == delim) {
			if (p[0] == delim)
				++p;
			/*
			 * !!!
			 * Nul terminate the pattern string -- it's passed
			 * to regcomp which doesn't understand anything else.
			 */
			*t = '\0';
			break;
		}
		if (p[0] == '\\')
			if (p[1] == delim)
				++p;
			else if (p[1] == '\\')
				*t++ = *p++;
		*t++ = *p++;
	}

	/*
	 * If the pattern string is empty, use the last RE (not just the
	 * last substitution RE).
	 */
	if (*ptrn == '\0') {
		if (sp->re == NULL) {
			ex_emsg(sp, NULL, EXM_NOPREVRE);
			return (1);
		}

		/* Re-compile the RE if necessary. */
		if (!F_ISSET(sp, SC_RE_SEARCH) &&
		    re_compile(sp, sp->re, sp->re_len,
		    NULL, NULL, &sp->re_c, RE_C_SEARCH))
			return (1);
		flags = 0;
	} else {
		/*
		 * !!!
		 * Compile the RE.  Historic practice is that substitutes set
		 * the search direction as well as both substitute and search
		 * RE's.  We compile the RE twice, as we don't want to bother
		 * ref counting the pattern string and (opaque) structure.
		 */
		if (re_compile(sp, ptrn, t - ptrn, &sp->re,
		    &sp->re_len, &sp->re_c, RE_C_SEARCH))
			return (1);
		if (re_compile(sp, ptrn, t - ptrn, &sp->subre,
		    &sp->subre_len, &sp->subre_c, RE_C_SUBST))
			return (1);
		
		flags = SUB_FIRST;
		sp->searchdir = FORWARD;
	}
	re = &sp->re_c;

	/*
	 * Get the replacement string.
	 *
	 * The special character & (\& if O_MAGIC not set) matches the
	 * entire RE.  No handling of & is required here, it's done by
	 * re_sub().
	 *
	 * The special character ~ (\~ if O_MAGIC not set) inserts the
	 * previous replacement string into this replacement string.
	 * Count ~'s to figure out how much space we need.  We could
	 * special case nonexistent last patterns or whether or not
	 * O_MAGIC is set, but it's probably not worth the effort.
	 *
	 * QUOTING NOTE:
	 *
	 * Only toss an escaping character if it escapes a delimiter or
	 * if O_MAGIC is set and it escapes a tilde.
	 *
	 * !!!
	 * If the entire replacement pattern is "%", then use the last
	 * replacement pattern.  This semantic was added to vi in System
	 * V and then percolated elsewhere, presumably around the time
	 * that it was added to their version of ed(1).
	 */
	if (p[0] == '\0' || p[0] == delim) {
		if (p[0] == delim)
			++p;
		if (sp->repl != NULL)
			free(sp->repl);
		sp->repl = NULL;
		sp->repl_len = 0;
	} else if (p[0] == '%' && (p[1] == '\0' || p[1] == delim))
		p += p[1] == delim ? 2 : 1;
	else {
		for (rep = p, len = 0;
		    p[0] != '\0' && p[0] != delim; ++p, ++len)
			if (p[0] == '~')
				len += sp->repl_len;
		GET_SPACE_RETW(sp, bp, blen, len);
		for (t = bp, len = 0, p = rep;;) {
			if (p[0] == '\0' || p[0] == delim) {
				if (p[0] == delim)
					++p;
				break;
			}
			if (p[0] == '\\') {
				if (p[1] == delim)
					++p;
				else if (p[1] == '\\') {
					*t++ = *p++;
					++len;
				} else if (p[1] == '~') {
					++p;
					if (!O_ISSET(sp, O_MAGIC))
						goto tilde;
				}
			} else if (p[0] == '~' && O_ISSET(sp, O_MAGIC)) {
tilde:				++p;
				MEMCPY(t, sp->repl, sp->repl_len);
				t += sp->repl_len;
				len += sp->repl_len;
				continue;
			}
			*t++ = *p++;
			++len;
		}
		if ((sp->repl_len = len) != 0) {
			if (sp->repl != NULL)
				free(sp->repl);
			MALLOC(sp, sp->repl, CHAR_T *, len * sizeof(CHAR_T));
			if (sp->repl == NULL) {
				FREE_SPACEW(sp, bp, blen);
				return (1);
			}
			MEMCPY(sp->repl, bp, len);
		}
		FREE_SPACEW(sp, bp, blen);
	}
	return (s(sp, cmdp, p, re, flags));
}

/*
 * ex_subagain --
 *	[line [,line]] & [cgr] [count] [#lp]]
 *
 *	Substitute using the last substitute RE and replacement pattern.
 *
 * PUBLIC: int ex_subagain(SCR *, EXCMD *);
 */
int
ex_subagain(SCR *sp, EXCMD *cmdp)
{
	if (sp->subre == NULL) {
		ex_emsg(sp, NULL, EXM_NOPREVRE);
		return (1);
	}
	if (!F_ISSET(sp, SC_RE_SUBST) &&
	    re_compile(sp, sp->subre, sp->subre_len,
	    NULL, NULL, &sp->subre_c, RE_C_SUBST))
		return (1);
	return (s(sp,
	    cmdp, cmdp->argc ? cmdp->argv[0]->bp : NULL, &sp->subre_c, 0));
}

/*
 * ex_subtilde --
 *	[line [,line]] ~ [cgr] [count] [#lp]]
 *
 *	Substitute using the last RE and last substitute replacement pattern.
 *
 * PUBLIC: int ex_subtilde(SCR *, EXCMD *);
 */
int
ex_subtilde(SCR *sp, EXCMD *cmdp)
{
	if (sp->re == NULL) {
		ex_emsg(sp, NULL, EXM_NOPREVRE);
		return (1);
	}
	if (!F_ISSET(sp, SC_RE_SEARCH) && re_compile(sp, sp->re,
	    sp->re_len, NULL, NULL, &sp->re_c, RE_C_SEARCH))
		return (1);
	return (s(sp,
	    cmdp, cmdp->argc ? cmdp->argv[0]->bp : NULL, &sp->re_c, 0));
}

/*
 * s --
 * Do the substitution.  This stuff is *really* tricky.  There are lots of
 * special cases, and general nastiness.  Don't mess with it unless you're
 * pretty confident.
 * 
 * The nasty part of the substitution is what happens when the replacement
 * string contains newlines.  It's a bit tricky -- consider the information
 * that has to be retained for "s/f\(o\)o/^M\1^M\1/".  The solution here is
 * to build a set of newline offsets which we use to break the line up later,
 * when the replacement is done.  Don't change it unless you're *damned*
 * confident.
 */
#define	NEEDNEWLINE(sp) {						\
	if (sp->newl_len == sp->newl_cnt) {				\
		sp->newl_len += 25;					\
		REALLOC(sp, sp->newl, size_t *,				\
		    sp->newl_len * sizeof(size_t));			\
		if (sp->newl == NULL) {					\
			sp->newl_len = 0;				\
			return (1);					\
		}							\
	}								\
}

#define	BUILD(sp, l, len) {						\
	if (lbclen + (len) > lblen) {					\
		lblen = p2roundup(MAX(lbclen + (len), 256));		\
		REALLOC(sp, lb, CHAR_T *, lblen * sizeof(CHAR_T));	\
		if (lb == NULL) {					\
			lbclen = 0;					\
			return (1);					\
		}							\
	}								\
	MEMCPY(lb + lbclen, l, len);					\
	lbclen += len;							\
}

#define	NEEDSP(sp, len, pnt) {						\
	if (lbclen + (len) > lblen) {					\
		lblen = p2roundup(MAX(lbclen + (len), 256));		\
		REALLOC(sp, lb, CHAR_T *, lblen * sizeof(CHAR_T));	\
		if (lb == NULL) {					\
			lbclen = 0;					\
			return (1);					\
		}							\
		pnt = lb + lbclen;					\
	}								\
}

static int
s(SCR *sp, EXCMD *cmdp, CHAR_T *s, regex_t *re, u_int flags)
{
	EVENT ev;
	MARK from, to;
	TEXTH tiq[] = {{ 0 }};
	recno_t elno, lno, slno;
	u_long ul;
	regmatch_t match[10];
	size_t blen, cnt, last, lbclen, lblen, len, llen;
	size_t offset, saved_offset, scno;
	int cflag, lflag, nflag, pflag, rflag;
	int didsub, do_eol_match, eflags, empty_ok, eval;
	int linechanged, matched, quit, rval;
	CHAR_T *bp, *lb;
	enum nresult nret;

	NEEDFILE(sp, cmdp);

	slno = sp->lno;
	scno = sp->cno;

	/*
	 * !!!
	 * Historically, the 'g' and 'c' suffices were always toggled as flags,
	 * so ":s/A/B/" was the same as ":s/A/B/ccgg".  If O_EDCOMPATIBLE was
	 * not set, they were initialized to 0 for all substitute commands.  If
	 * O_EDCOMPATIBLE was set, they were initialized to 0 only if the user
	 * specified substitute/replacement patterns (see ex_s()).
	 */
	if (!O_ISSET(sp, O_EDCOMPATIBLE))
		sp->c_suffix = sp->g_suffix = 0;

	/*
	 * Historic vi permitted the '#', 'l' and 'p' options in vi mode, but
	 * it only displayed the last change.  I'd disallow them, but they are
	 * useful in combination with the [v]global commands.  In the current
	 * model the problem is combining them with the 'c' flag -- the screen
	 * would have to flip back and forth between the confirm screen and the
	 * ex print screen, which would be pretty awful.  We do display all
	 * changes, though, for what that's worth.
	 *
	 * !!!
	 * Historic vi was fairly strict about the order of "options", the
	 * count, and "flags".  I'm somewhat fuzzy on the difference between
	 * options and flags, anyway, so this is a simpler approach, and we
	 * just take it them in whatever order the user gives them.  (The ex
	 * usage statement doesn't reflect this.)
	 */
	cflag = lflag = nflag = pflag = rflag = 0;
	if (s == NULL)
		goto noargs;
	for (lno = OOBLNO; *s != '\0'; ++s)
		switch (*s) {
		case ' ':
		case '\t':
			continue;
		case '+':
			++cmdp->flagoff;
			break;
		case '-':
			--cmdp->flagoff;
			break;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (lno != OOBLNO)
				goto usage;
			errno = 0;
			nret = nget_uslong(&ul, s, &s, 10);
			lno = ul;
			if (*s == '\0')		/* Loop increment correction. */
				--s;
			if (nret != NUM_OK) {
				if (nret == NUM_OVER)
					msgq(sp, M_ERR, "153|Count overflow");
				else if (nret == NUM_UNDER)
					msgq(sp, M_ERR, "154|Count underflow");
				else
					msgq(sp, M_SYSERR, NULL);
				return (1);
			}
			/*
			 * In historic vi, the count was inclusive from the
			 * second address.
			 */
			cmdp->addr1.lno = cmdp->addr2.lno;
			cmdp->addr2.lno += lno - 1;
			if (!db_exist(sp, cmdp->addr2.lno) &&
			    db_last(sp, &cmdp->addr2.lno))
				return (1);
			break;
		case '#':
			nflag = 1;
			break;
		case 'c':
			sp->c_suffix = !sp->c_suffix;

			/* Ex text structure initialization. */
			if (F_ISSET(sp, SC_EX))
				TAILQ_INIT(tiq);
			break;
		case 'g':
			sp->g_suffix = !sp->g_suffix;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'p':
			pflag = 1;
			break;
		case 'r':
			if (LF_ISSET(SUB_FIRST)) {
				msgq(sp, M_ERR,
		    "155|Regular expression specified; r flag meaningless");
				return (1);
			}
			if (!F_ISSET(sp, SC_RE_SEARCH)) {
				ex_emsg(sp, NULL, EXM_NOPREVRE);
				return (1);
			}
			rflag = 1;
			re = &sp->re_c;
			break;
		default:
			goto usage;
		}

	if (*s != '\0' || (!rflag && LF_ISSET(SUB_MUSTSETR))) {
usage:		ex_emsg(sp, cmdp->cmd->usage, EXM_USAGE);
		return (1);
	}

noargs:	if (F_ISSET(sp, SC_VI) && sp->c_suffix && (lflag || nflag || pflag)) {
		msgq(sp, M_ERR,
"156|The #, l and p flags may not be combined with the c flag in vi mode");
		return (1);
	}

	/*
	 * bp:		if interactive, line cache
	 * blen:	if interactive, line cache length
	 * lb:		build buffer pointer.
	 * lbclen:	current length of built buffer.
	 * lblen;	length of build buffer.
	 */
	bp = lb = NULL;
	blen = lbclen = lblen = 0;

	/* For each line... */
	lno = cmdp->addr1.lno == 0 ? 1 : cmdp->addr1.lno;
	for (matched = quit = 0,
	    elno = cmdp->addr2.lno; !quit && lno <= elno; ++lno) {

		/* Someone's unhappy, time to stop. */
		if (INTERRUPTED(sp))
			break;

		/* Get the line. */
		if (db_get(sp, lno, DBG_FATAL, &s, &llen))
			goto err;

		/*
		 * Make a local copy if doing confirmation -- when calling
		 * the confirm routine we're likely to lose the cached copy.
		 */
		if (sp->c_suffix) {
			if (bp == NULL) {
				GET_SPACE_RETW(sp, bp, blen, llen);
			} else
				ADD_SPACE_RETW(sp, bp, blen, llen);
			MEMCPY(bp, s, llen);
			s = bp;
		}

		/* Start searching from the beginning. */
		offset = 0;
		len = llen;

		/* Reset the build buffer offset. */
		lbclen = 0;

		/* Reset empty match flag. */
		empty_ok = 1;

		/*
		 * We don't want to have to do a setline if the line didn't
		 * change -- keep track of whether or not this line changed.
		 * If doing confirmations, don't want to keep setting the
		 * line if change is refused -- keep track of substitutions.
		 */
		didsub = linechanged = 0;

		/* New line, do an EOL match. */
		do_eol_match = 1;

		/* It's not nul terminated, but we pretend it is. */
		eflags = REG_STARTEND;

		/*
		 * The search area is from s + offset to the EOL.
		 *
		 * Generally, match[0].rm_so is the offset of the start
		 * of the match from the start of the search, and offset
		 * is the offset of the start of the last search.
		 */
nextmatch:	match[0].rm_so = 0;
		match[0].rm_eo = len;

		/* Get the next match. */
		eval = regexec(re, s + offset, 10, match, eflags);

		/*
		 * There wasn't a match or if there was an error, deal with
		 * it.  If there was a previous match in this line, resolve
		 * the changes into the database.  Otherwise, just move on.
		 */
		if (eval == REG_NOMATCH)
			goto endmatch;
		if (eval != 0) {
			re_error(sp, eval, re);
			goto err;
		}
		matched = 1;

		/* Only the first search can match an anchored expression. */
		eflags |= REG_NOTBOL;

		/*
		 * !!!
		 * It's possible to match 0-length strings -- for example, the
		 * command s;a*;X;, when matched against the string "aabb" will
		 * result in "XbXbX", i.e. the matches are "aa", the space
		 * between the b's and the space between the b's and the end of
		 * the string.  There is a similar space between the beginning
		 * of the string and the a's.  The rule that we use (because vi
		 * historically used it) is that any 0-length match, occurring
		 * immediately after a match, is ignored.  Otherwise, the above
		 * example would have resulted in "XXbXbX".  Another example is
		 * incorrectly using " *" to replace groups of spaces with one
		 * space.
		 *
		 * The way we do this is that if we just had a successful match,
		 * the starting offset does not skip characters, and the match
		 * is empty, ignore the match and move forward.  If there's no
		 * more characters in the string, we were attempting to match
		 * after the last character, so quit.
		 */
		if (!empty_ok && match[0].rm_so == 0 && match[0].rm_eo == 0) {
			empty_ok = 1;
			if (len == 0)
				goto endmatch;
			BUILD(sp, s + offset, 1)
			++offset;
			--len;
			goto nextmatch;
		}

		/* Confirm change. */
		if (sp->c_suffix) {
			/*
			 * Set the cursor position for confirmation.  Note,
			 * if we matched on a '$', the cursor may be past
			 * the end of line.
			 */
			from.lno = to.lno = lno;
			from.cno = match[0].rm_so + offset;
			to.cno = match[0].rm_eo + offset;
			/*
			 * Both ex and vi have to correct for a change before
			 * the first character in the line.
			 */
			if (llen == 0)
				from.cno = to.cno = 0;
			if (F_ISSET(sp, SC_VI)) {
				/*
				 * Only vi has to correct for a change after
				 * the last character in the line.
				 *
				 * XXX
				 * It would be nice to change the vi code so
				 * that we could display a cursor past EOL.
				 */
				if (to.cno >= llen)
					to.cno = llen - 1;
				if (from.cno >= llen)
					from.cno = llen - 1;

				sp->lno = from.lno;
				sp->cno = from.cno;
				if (vs_refresh(sp, 1))
					goto err;

				vs_update(sp, msg_cat(sp,
				    "169|Confirm change? [n]", NULL), NULL);

				if (v_event_get(sp, &ev, 0, 0))
					goto err;
				switch (ev.e_event) {
				case E_CHARACTER:
					break;
				case E_EOF:
				case E_ERR:
				case E_INTERRUPT:
					goto lquit;
				default:
					v_event_err(sp, &ev);
					goto lquit;
				}
			} else {
				if (ex_print(sp, cmdp, &from, &to, 0) ||
				    ex_scprint(sp, &from, &to))
					goto lquit;
				if (ex_txt(sp, tiq, 0, TXT_CR))
					goto err;
				ev.e_c = TAILQ_FIRST(tiq)->lb[0];
			}

			switch (ev.e_c) {
			case CH_YES:
				break;
			default:
			case CH_NO:
				didsub = 0;
				BUILD(sp, s +offset, match[0].rm_eo);
				goto skip;
			case CH_QUIT:
				/* Set the quit/interrupted flags. */
lquit:				quit = 1;
				F_SET(sp->gp, G_INTERRUPTED);

				/*
				 * Resolve any changes, then return to (and
				 * exit from) the main loop.
				 */
				goto endmatch;
			}
		}

		/*
		 * Set the cursor to the last position changed, converting
		 * from 1-based to 0-based.
		 */
		sp->lno = lno;
		sp->cno = match[0].rm_so;

		/* Copy the bytes before the match into the build buffer. */
		BUILD(sp, s + offset, match[0].rm_so);

		/* Substitute the matching bytes. */
		didsub = 1;
		if (re_sub(sp, s + offset, &lb, &lbclen, &lblen, match))
			goto err;

		/* Set the change flag so we know this line was modified. */
		linechanged = 1;

		/* Move past the matched bytes. */
skip:		offset += match[0].rm_eo;
		len -= match[0].rm_eo;

		/* A match cannot be followed by an empty pattern. */
		empty_ok = 0;

		/*
		 * If doing a global change with confirmation, we have to
		 * update the screen.  The basic idea is to store the line
		 * so the screen update routines can find it, and restart.
		 */
		if (didsub && sp->c_suffix && sp->g_suffix) {
			/*
			 * The new search offset will be the end of the
			 * modified line.
			 */
			saved_offset = lbclen;

			/* Copy the rest of the line. */
			if (len)
				BUILD(sp, s + offset, len)

			/* Set the new offset. */
			offset = saved_offset;

			/* Store inserted lines, adjusting the build buffer. */
			last = 0;
			if (sp->newl_cnt) {
				for (cnt = 0;
				    cnt < sp->newl_cnt; ++cnt, ++lno, ++elno) {
					if (db_insert(sp, lno,
					    lb + last, sp->newl[cnt] - last))
						goto err;
					last = sp->newl[cnt] + 1;
					++sp->rptlines[L_ADDED];
				}
				lbclen -= last;
				offset -= last;
				sp->newl_cnt = 0;
			}

			/* Store and retrieve the line. */
			if (db_set(sp, lno, lb + last, lbclen))
				goto err;
			if (db_get(sp, lno, DBG_FATAL, &s, &llen))
				goto err;
			ADD_SPACE_RETW(sp, bp, blen, llen)
			MEMCPY(bp, s, llen);
			s = bp;
			len = llen - offset;

			/* Restart the build. */
			lbclen = 0;
			BUILD(sp, s, offset);

			/*
			 * If we haven't already done the after-the-string
			 * match, do one.  Set REG_NOTEOL so the '$' pattern
			 * only matches once.
			 */
			if (!do_eol_match)
				goto endmatch;
			if (offset == len) {
				do_eol_match = 0;
				eflags |= REG_NOTEOL;
			}
			goto nextmatch;
		}

		/*
		 * If it's a global:
		 *
		 * If at the end of the string, do a test for the after
		 * the string match.  Set REG_NOTEOL so the '$' pattern
		 * only matches once.
		 */
		if (sp->g_suffix && do_eol_match) {
			if (len == 0) {
				do_eol_match = 0;
				eflags |= REG_NOTEOL;
			}
			goto nextmatch;
		}

endmatch:	if (!linechanged)
			continue;

		/* Copy any remaining bytes into the build buffer. */
		if (len)
			BUILD(sp, s + offset, len)

		/* Store inserted lines, adjusting the build buffer. */
		last = 0;
		if (sp->newl_cnt) {
			for (cnt = 0;
			    cnt < sp->newl_cnt; ++cnt, ++lno, ++elno) {
				if (db_insert(sp,
				    lno, lb + last, sp->newl[cnt] - last))
					goto err;
				last = sp->newl[cnt] + 1;
				++sp->rptlines[L_ADDED];
			}
			lbclen -= last;
			sp->newl_cnt = 0;
		}

		/* Store the changed line. */
		if (db_set(sp, lno, lb + last, lbclen))
			goto err;

		/* Update changed line counter. */
		if (sp->rptlchange != lno) {
			sp->rptlchange = lno;
			++sp->rptlines[L_CHANGED];
		}

		/*
		 * !!!
		 * Display as necessary.  Historic practice is to only
		 * display the last line of a line split into multiple
		 * lines.
		 */
		if (lflag || nflag || pflag) {
			from.lno = to.lno = lno;
			from.cno = to.cno = 0;
			if (lflag)
				(void)ex_print(sp, cmdp, &from, &to, E_C_LIST);
			if (nflag)
				(void)ex_print(sp, cmdp, &from, &to, E_C_HASH);
			if (pflag)
				(void)ex_print(sp, cmdp, &from, &to, E_C_PRINT);
		}
	}

	/*
	 * !!!
	 * Historically, vi attempted to leave the cursor at the same place if
	 * the substitution was done at the current cursor position.  Otherwise
	 * it moved it to the first non-blank of the last line changed.  There
	 * were some problems: for example, :s/$/foo/ with the cursor on the
	 * last character of the line left the cursor on the last character, or
	 * the & command with multiple occurrences of the matching string in the
	 * line usually left the cursor in a fairly random position.
	 *
	 * We try to do the same thing, with the exception that if the user is
	 * doing substitution with confirmation, we move to the last line about
	 * which the user was consulted, as opposed to the last line that they
	 * actually changed.  This prevents a screen flash if the user doesn't
	 * change many of the possible lines.
	 */
	if (!sp->c_suffix && (sp->lno != slno || sp->cno != scno)) {
		sp->cno = 0;
		(void)nonblank(sp, sp->lno, &sp->cno);
	}

	/*
	 * If not in a global command, and nothing matched, say so.
	 * Else, if none of the lines displayed, put something up.
	 */
	rval = 0;
	if (!matched) {
		if (!F_ISSET(sp, SC_EX_GLOBAL)) {
			msgq(sp, M_ERR, "157|No match found");
			goto err;
		}
	} else if (!lflag && !nflag && !pflag)
		F_SET(cmdp, E_AUTOPRINT);

	if (0) {
err:		rval = 1;
	}

	if (bp != NULL)
		FREE_SPACEW(sp, bp, blen);
	if (lb != NULL)
		free(lb);
	return (rval);
}

/*
 * re_compile --
 *	Compile the RE.
 *
 * PUBLIC: int re_compile(SCR *,
 * PUBLIC:     CHAR_T *, size_t, CHAR_T **, size_t *, regex_t *, u_int);
 */
int
re_compile(SCR *sp, CHAR_T *ptrn, size_t plen, CHAR_T **ptrnp, size_t *lenp, regex_t *rep, u_int flags)
{
	size_t len;
	int reflags, replaced, rval;
	CHAR_T *p;

	/* Set RE flags. */
	reflags = 0;
	if (!LF_ISSET(RE_C_CSCOPE | RE_C_TAG)) {
		if (O_ISSET(sp, O_EXTENDED))
			reflags |= REG_EXTENDED;
		if (O_ISSET(sp, O_IGNORECASE))
			reflags |= REG_ICASE;
		if (O_ISSET(sp, O_ICLOWER)) {
			for (p = ptrn, len = plen; len > 0; ++p, --len)
				if (ISUPPER(*p))
					break;
			if (len == 0)
				reflags |= REG_ICASE;
		}
	}

	/* If we're replacing a saved value, clear the old one. */
	if (LF_ISSET(RE_C_SEARCH) && F_ISSET(sp, SC_RE_SEARCH)) {
		regfree(&sp->re_c);
		F_CLR(sp, SC_RE_SEARCH);
	}
	if (LF_ISSET(RE_C_SUBST) && F_ISSET(sp, SC_RE_SUBST)) {
		regfree(&sp->subre_c);
		F_CLR(sp, SC_RE_SUBST);
	}

	/*
	 * If we're saving the string, it's a pattern we haven't seen before,
	 * so convert the vi-style RE's to POSIX 1003.2 RE's.  Save a copy for
	 * later recompilation.   Free any previously saved value.
	 */
	if (ptrnp != NULL) {
		replaced = 0;
		if (LF_ISSET(RE_C_CSCOPE)) {
			if (re_cscope_conv(sp, &ptrn, &plen, &replaced))
				return (1);
			/*
			 * XXX
			 * Currently, the match-any-<blank> expression used in
			 * re_cscope_conv() requires extended RE's.  This may
			 * not be right or safe.
			 */
			reflags |= REG_EXTENDED;
		} else if (LF_ISSET(RE_C_TAG)) {
			if (re_tag_conv(sp, &ptrn, &plen, &replaced))
				return (1);
		} else
			if (re_conv(sp, &ptrn, &plen, &replaced))
				return (1);

		/* Discard previous pattern. */
		if (*ptrnp != NULL) {
			free(*ptrnp);
			*ptrnp = NULL;
		}
		if (lenp != NULL)
			*lenp = plen;

		/*
		 * Copy the string into allocated memory.
		 *
		 * XXX
		 * Regcomp isn't 8-bit clean, so the pattern is nul-terminated
		 * for now.  There's just no other solution.  
		 */
		MALLOC(sp, *ptrnp, CHAR_T *, (plen + 1) * sizeof(CHAR_T));
		if (*ptrnp != NULL) {
			MEMCPY(*ptrnp, ptrn, plen);
			(*ptrnp)[plen] = '\0';
		}

		/* Free up conversion-routine-allocated memory. */
		if (replaced)
			FREE_SPACEW(sp, ptrn, 0);

		if (*ptrnp == NULL)
			return (1);

		ptrn = *ptrnp;
	}

	/*
	 * XXX
	 * Regcomp isn't 8-bit clean, so we just lost if the pattern
	 * contained a nul.  Bummer!
	 */
	if ((rval = regcomp(rep, ptrn, /* plen, */ reflags)) != 0) {
		if (!LF_ISSET(RE_C_SILENT))
			re_error(sp, rval, rep); 
		return (1);
	}

	if (LF_ISSET(RE_C_SEARCH))
		F_SET(sp, SC_RE_SEARCH);
	if (LF_ISSET(RE_C_SUBST))
		F_SET(sp, SC_RE_SUBST);

	return (0);
}

/*
 * re_conv --
 *	Convert vi's regular expressions into something that the
 *	the POSIX 1003.2 RE functions can handle.
 *
 * There are three conversions we make to make vi's RE's (specifically
 * the global, search, and substitute patterns) work with POSIX RE's.
 *
 * 1: If O_MAGIC is not set, strip backslashes from the magic character
 *    set (.[*~) that have them, and add them to the ones that don't.
 * 2: If O_MAGIC is not set, the string "\~" is replaced with the text
 *    from the last substitute command's replacement string.  If O_MAGIC
 *    is set, it's the string "~".
 * 3: The pattern \<ptrn\> does "word" searches, convert it to use the
 *    new RE escapes.
 *
 * !!!/XXX
 * This doesn't exactly match the historic behavior of vi because we do
 * the ~ substitution before calling the RE engine, so magic characters
 * in the replacement string will be expanded by the RE engine, and they
 * weren't historically.  It's a bug.
 */
static int
re_conv(SCR *sp, CHAR_T **ptrnp, size_t *plenp, int *replacedp)
{
	size_t blen, len, needlen;
	int magic;
	CHAR_T *bp, *p, *t;

	/*
	 * First pass through, we figure out how much space we'll need.
	 * We do it in two passes, on the grounds that most of the time
	 * the user is doing a search and won't have magic characters.
	 * That way we can skip most of the memory allocation and copies.
	 */
	magic = 0;
	for (p = *ptrnp, len = *plenp, needlen = 0; len > 0; ++p, --len)
		switch (*p) {
		case '\\':
			if (len > 1) {
				--len;
				switch (*++p) {
				case '<':
					magic = 1;
					needlen += RE_WSTART_LEN + 1;
					break;
				case '>':
					magic = 1;
					needlen += RE_WSTOP_LEN + 1;
					break;
				case '~':
					if (!O_ISSET(sp, O_MAGIC)) {
						magic = 1;
						needlen += sp->repl_len;
					}
					break;
				case '.':
				case '[':
				case '*':
					if (!O_ISSET(sp, O_MAGIC)) {
						magic = 1;
						needlen += 1;
					}
					break;
				default:
					needlen += 2;
				}
			} else
				needlen += 1;
			break;
		case '~':
			if (O_ISSET(sp, O_MAGIC)) {
				magic = 1;
				needlen += sp->repl_len;
			}
			break;
		case '.':
		case '[':
		case '*':
			if (!O_ISSET(sp, O_MAGIC)) {
				magic = 1;
				needlen += 2;
			}
			break;
		default:
			needlen += 1;
			break;
		}

	if (!magic) {
		*replacedp = 0;
		return (0);
	}

	/* Get enough memory to hold the final pattern. */
	*replacedp = 1;
	GET_SPACE_RETW(sp, bp, blen, needlen);

	for (p = *ptrnp, len = *plenp, t = bp; len > 0; ++p, --len)
		switch (*p) {
		case '\\':
			if (len > 1) {
				--len;
				switch (*++p) {
				case '<':
					MEMCPY(t,
					    RE_WSTART, RE_WSTART_LEN);
					t += RE_WSTART_LEN;
					break;
				case '>':
					MEMCPY(t,
					    RE_WSTOP, RE_WSTOP_LEN);
					t += RE_WSTOP_LEN;
					break;
				case '~':
					if (O_ISSET(sp, O_MAGIC))
						*t++ = '~';
					else {
						MEMCPY(t,
						    sp->repl, sp->repl_len);
						t += sp->repl_len;
					}
					break;
				case '.':
				case '[':
				case '*':
					if (O_ISSET(sp, O_MAGIC))
						*t++ = '\\';
					*t++ = *p;
					break;
				default:
					*t++ = '\\';
					*t++ = *p;
				}
			} else
				*t++ = '\\';
			break;
		case '~':
			if (O_ISSET(sp, O_MAGIC)) {
				MEMCPY(t, sp->repl, sp->repl_len);
				t += sp->repl_len;
			} else
				*t++ = '~';
			break;
		case '.':
		case '[':
		case '*':
			if (!O_ISSET(sp, O_MAGIC))
				*t++ = '\\';
			*t++ = *p;
			break;
		default:
			*t++ = *p;
			break;
		}

	*ptrnp = bp;
	*plenp = t - bp;
	return (0);
}

/*
 * re_tag_conv --
 *	Convert a tags search path into something that the POSIX
 *	1003.2 RE functions can handle.
 */
static int
re_tag_conv(SCR *sp, CHAR_T **ptrnp, size_t *plenp, int *replacedp)
{
	size_t blen, len;
	int lastdollar;
	CHAR_T *bp, *p, *t;

	len = *plenp;

	/* Max memory usage is 2 times the length of the string. */
	*replacedp = 1;
	GET_SPACE_RETW(sp, bp, blen, len * 2);

	p = *ptrnp;
	t = bp;

	/* If the last character is a '/' or '?', we just strip it. */
	if (len > 0 && (p[len - 1] == '/' || p[len - 1] == '?'))
		--len;

	/* If the next-to-last or last character is a '$', it's magic. */
	if (len > 0 && p[len - 1] == '$') {
		--len;
		lastdollar = 1;
	} else
		lastdollar = 0;

	/* If the first character is a '/' or '?', we just strip it. */
	if (len > 0 && (p[0] == '/' || p[0] == '?')) {
		++p;
		--len;
	}

	/* If the first or second character is a '^', it's magic. */
	if (p[0] == '^') {
		*t++ = *p++;
		--len;
	}

	/*
	 * Escape every other magic character we can find, meanwhile stripping
	 * the backslashes ctags inserts when escaping the search delimiter
	 * characters.
	 */
	for (; len > 0; --len) {
		if (p[0] == '\\' && (p[1] == '/' || p[1] == '?')) {
			++p;
			--len;
		} else if (STRCHR(L("^.[]$*"), p[0]))
			*t++ = '\\';
		*t++ = *p++;
	}
	if (lastdollar)
		*t++ = '$';

	*ptrnp = bp;
	*plenp = t - bp;
	return (0);
}

/*
 * re_cscope_conv --
 *	 Convert a cscope search path into something that the POSIX
 *      1003.2 RE functions can handle.
 */
static int
re_cscope_conv(SCR *sp, CHAR_T **ptrnp, size_t *plenp, int *replacedp)
{
	size_t blen, len, nspaces;
	CHAR_T *bp, *t;
	CHAR_T *p;
	CHAR_T *wp;
	size_t wlen;

	/*
	 * Each space in the source line printed by cscope represents an
	 * arbitrary sequence of spaces, tabs, and comments.
	 */
#define	CSCOPE_RE_SPACE		"([ \t]|/\\*([^*]|\\*/)*\\*/)*"
#define CSCOPE_LEN	sizeof(CSCOPE_RE_SPACE) - 1
	CHAR2INT(sp, CSCOPE_RE_SPACE, CSCOPE_LEN, wp, wlen);
	for (nspaces = 0, p = *ptrnp, len = *plenp; len > 0; ++p, --len)
		if (*p == ' ')
			++nspaces;

	/*
	 * Allocate plenty of space:
	 *	the string, plus potential escaping characters;
	 *	nspaces + 2 copies of CSCOPE_RE_SPACE;
	 *	^, $, nul terminator characters.
	 */
	*replacedp = 1;
	len = (p - *ptrnp) * 2 + (nspaces + 2) * sizeof(CSCOPE_RE_SPACE) + 3;
	GET_SPACE_RETW(sp, bp, blen, len);

	p = *ptrnp;
	t = bp;

	*t++ = '^';
	MEMCPY(t, wp, wlen);
	t += wlen;

	for (len = *plenp; len > 0; ++p, --len)
		if (*p == ' ') {
			MEMCPY(t, wp, wlen);
			t += wlen;
		} else {
			if (STRCHR(L("\\^.[]$*+?()|{}"), *p))
				*t++ = '\\';
			*t++ = *p;
		}

	MEMCPY(t, wp, wlen);
	t += wlen;
	*t++ = '$';

	*ptrnp = bp;
	*plenp = t - bp;
	return (0);
}

/*
 * re_error --
 *	Report a regular expression error.
 *
 * PUBLIC: void re_error(SCR *, int, regex_t *);
 */
void
re_error(SCR *sp, int errcode, regex_t *preg)
{
	size_t s;
	char *oe;

	s = regerror(errcode, preg, "", 0);
	MALLOC(sp, oe, char *, s);
	if (oe != NULL) {
		(void)regerror(errcode, preg, oe, s);
		msgq(sp, M_ERR, "RE error: %s", oe);
		free(oe);
	}
}

/*
 * re_sub --
 * 	Do the substitution for a regular expression.
 */
static int
re_sub(
	SCR *sp,
	CHAR_T *ip,			/* Input line. */
	CHAR_T **lbp,
	size_t *lbclenp,
	size_t *lblenp,
	regmatch_t match[10])
{
	enum { C_NOTSET, C_LOWER, C_ONELOWER, C_ONEUPPER, C_UPPER } conv;
	size_t lbclen, lblen;		/* Local copies. */
	size_t mlen;			/* Match length. */
	size_t rpl;			/* Remaining replacement length. */
	CHAR_T *rp;			/* Replacement pointer. */
	int ch;
	int no;				/* Match replacement offset. */
	CHAR_T *p, *t;			/* Buffer pointers. */
	CHAR_T *lb;			/* Local copies. */

	lb = *lbp;			/* Get local copies. */
	lbclen = *lbclenp;
	lblen = *lblenp;

	/*
	 * QUOTING NOTE:
	 *
	 * There are some special sequences that vi provides in the
	 * replacement patterns.
	 *	 & string the RE matched (\& if nomagic set)
	 *	\# n-th regular subexpression
	 *	\E end \U, \L conversion
	 *	\e end \U, \L conversion
	 *	\l convert the next character to lower-case
	 *	\L convert to lower-case, until \E, \e, or end of replacement
	 *	\u convert the next character to upper-case
	 *	\U convert to upper-case, until \E, \e, or end of replacement
	 *
	 * Otherwise, since this is the lowest level of replacement, discard
	 * all escaping characters.  This (hopefully) matches historic practice.
	 */
#define	OUTCH(ch, nltrans) {						\
	ARG_CHAR_T __ch = (ch);						\
	e_key_t __value = KEY_VAL(sp, __ch);				\
	if (nltrans && (__value == K_CR || __value == K_NL)) {		\
		NEEDNEWLINE(sp);					\
		sp->newl[sp->newl_cnt++] = lbclen;			\
	} else if (conv != C_NOTSET) {					\
		switch (conv) {						\
		case C_ONELOWER:					\
			conv = C_NOTSET;				\
			/* FALLTHROUGH */				\
		case C_LOWER:						\
			if (ISUPPER(__ch))				\
				__ch = TOLOWER(__ch);			\
			break;						\
		case C_ONEUPPER:					\
			conv = C_NOTSET;				\
			/* FALLTHROUGH */				\
		case C_UPPER:						\
			if (ISLOWER(__ch))				\
				__ch = TOUPPER(__ch);			\
			break;						\
		default:						\
			abort();					\
		}							\
	}								\
	NEEDSP(sp, 1, p);						\
	*p++ = __ch;							\
	++lbclen;							\
}
	conv = C_NOTSET;
	for (rp = sp->repl, rpl = sp->repl_len, p = lb + lbclen; rpl--;) {
		switch (ch = *rp++) {
		case '&':
			if (O_ISSET(sp, O_MAGIC)) {
				no = 0;
				goto subzero;
			}
			break;
		case '\\':
			if (rpl == 0)
				break;
			--rpl;
			switch (ch = *rp) {
			case '&':
				++rp;
				if (!O_ISSET(sp, O_MAGIC)) {
					no = 0;
					goto subzero;
				}
				break;
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				no = *rp++ - '0';
subzero:			if (match[no].rm_so == -1 ||
				    match[no].rm_eo == -1)
					break;
				mlen = match[no].rm_eo - match[no].rm_so;
				for (t = ip + match[no].rm_so; mlen--; ++t)
					OUTCH(*t, 0);
				continue;
			case 'e':
			case 'E':
				++rp;
				conv = C_NOTSET;
				continue;
			case 'l':
				++rp;
				conv = C_ONELOWER;
				continue;
			case 'L':
				++rp;
				conv = C_LOWER;
				continue;
			case 'u':
				++rp;
				conv = C_ONEUPPER;
				continue;
			case 'U':
				++rp;
				conv = C_UPPER;
				continue;
			case '\r':
				OUTCH(ch, 0);
				continue;
			default:
				++rp;
				break;
			}
		}
		OUTCH(ch, 1);
	}

	*lbp = lb;			/* Update caller's information. */
	*lbclenp = lbclen;
	*lblenp = lblen;
	return (0);
}
