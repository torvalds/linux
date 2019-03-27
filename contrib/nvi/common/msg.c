/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1991, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: msg.c,v 11.1 2015/02/09 11:12:44 marc Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "../vi/vi.h"

/*
 * msgq --
 *	Display a message.
 *
 * PUBLIC: void msgq(SCR *, mtype_t, const char *, ...);
 */
void
msgq(
	SCR *sp,
	mtype_t mt,
	const char *fmt,
	...)
{
#ifndef NL_ARGMAX
#define	__NL_ARGMAX	20		/* Set to 9 by System V. */
	struct {
		const char *str;	/* String pointer. */
		size_t	 arg;		/* Argument number. */
		size_t	 prefix;	/* Prefix string length. */
		size_t	 skip;		/* Skipped string length. */
		size_t	 suffix;	/* Suffix string length. */
	} str[__NL_ARGMAX];
#endif
	static int reenter;		/* STATIC: Re-entrancy check. */
	GS *gp;
	size_t blen, len, mlen, nlen;
	const char *p;
	char *bp, *mp;
	va_list ap;
#ifndef NL_ARGMAX
	int ch;
	char *rbp, *s_rbp;
	const char *t, *u;
	size_t cnt1, cnt2, soff;
#endif

	/*
	 * !!!
	 * It's possible to enter msg when there's no screen to hold the
	 * message.  If sp is NULL, ignore the special cases and put the
	 * message out to stderr.
	 */
	if (sp == NULL) {
		gp = NULL;
		if (mt == M_BERR)
			mt = M_ERR;
		else if (mt == M_VINFO)
			mt = M_INFO;
	} else {
		gp = sp->gp;
		switch (mt) {
		case M_BERR:
			if (F_ISSET(sp, SC_VI) && !O_ISSET(sp, O_VERBOSE)) {
				F_SET(gp, G_BELLSCHED);
				return;
			}
			mt = M_ERR;
			break;
		case M_VINFO:
			if (!O_ISSET(sp, O_VERBOSE))
				return;
			mt = M_INFO;
			/* FALLTHROUGH */
		case M_INFO:
			if (F_ISSET(sp, SC_EX_SILENT))
				return;
			break;
		case M_ERR:
		case M_SYSERR:
			break;
		default:
			abort();
		}
	}

	/*
	 * It's possible to reenter msg when it allocates space.  We're
	 * probably dead anyway, but there's no reason to drop core.
	 *
	 * XXX
	 * Yes, there's a race, but it should only be two instructions.
	 */
	if (reenter++)
		return;

	/* Get space for the message. */
	nlen = 1024;
	if (0) {
retry:		FREE_SPACE(sp, bp, blen);
		nlen *= 2;
	}
	bp = NULL;
	blen = 0;
	GET_SPACE_GOTOC(sp, bp, blen, nlen);

	/*
	 * Error prefix.
	 *
	 * mp:	 pointer to the current next character to be written
	 * mlen: length of the already written characters
	 * blen: total length of the buffer
	 */
#define	REM	(blen - mlen)
	mp = bp;
	mlen = 0;
	if (mt == M_SYSERR) {
		p = msg_cat(sp, "020|Error: ", &len);
		if (REM < len)
			goto retry;
		memcpy(mp, p, len);
		mp += len;
		mlen += len;
	}

	/*
	 * If we're running an ex command that the user didn't enter, display
	 * the file name and line number prefix.
	 */
	if ((mt == M_ERR || mt == M_SYSERR) &&
	    sp != NULL && gp != NULL && gp->if_name != NULL) {
		CHAR_T *wp;
		size_t wlen;

		CHAR2INT(sp, gp->if_name, strlen(gp->if_name) + 1, wp, wlen);
		for (; *wp != '\0'; ++wp) {
			len = snprintf(mp, REM, "%s", KEY_NAME(sp, *wp));
			mp += len;
			if ((mlen += len) > blen)
				goto retry;
		}
		len = snprintf(mp, REM, ", %d: ", gp->if_lno);
		mp += len;
		if ((mlen += len) > blen)
			goto retry;
	}

	/* If nothing to format, we're done. */
	if (fmt == NULL)
		goto nofmt;
	fmt = msg_cat(sp, fmt, NULL);

#ifndef NL_ARGMAX
	/*
	 * Nvi should run on machines that don't support the numbered argument
	 * specifications (%[digit]*$).  We do this by reformatting the string
	 * so that we can hand it to vsprintf(3) and it will use the arguments
	 * in the right order.  When vsprintf returns, we put the string back
	 * into the right order.  It's undefined, according to SVID III, to mix
	 * numbered argument specifications with the standard style arguments,
	 * so this should be safe.
	 *
	 * In addition, we also need a character that is known to not occur in
	 * any vi message, for separating the parts of the string.  As callers
	 * of msgq are responsible for making sure that all the non-printable
	 * characters are formatted for printing before calling msgq, we use a
	 * random non-printable character selected at terminal initialization
	 * time.  This code isn't fast by any means, but as messages should be
	 * relatively short and normally have only a few arguments, it won't be
	 * too bad.  Regardless, nobody has come up with any other solution.
	 *
	 * The result of this loop is an array of pointers into the message
	 * string, with associated lengths and argument numbers.  The array
	 * is in the "correct" order, and the arg field contains the argument
	 * order.
	 */
	for (p = fmt, soff = 0; soff < __NL_ARGMAX;) {
		for (t = p; *p != '\0' && *p != '%'; ++p);
		if (*p == '\0')
			break;
		++p;
		if (!isdigit(*p)) {
			if (*p == '%')
				++p;
			continue;
		}
		for (u = p; *++p != '\0' && isdigit(*p););
		if (*p != '$')
			continue;

		/* Up to, and including the % character. */
		str[soff].str = t;
		str[soff].prefix = u - t;

		/* Up to, and including the $ character. */
		str[soff].arg = atoi(u);
		str[soff].skip = (p - u) + 1;
		if (str[soff].arg >= __NL_ARGMAX)
			goto ret;

		/* Up to, and including the conversion character. */
		for (u = p; (ch = *++p) != '\0';)
			if (isalpha(ch) &&
			    strchr("diouxXfeEgGcspn", ch) != NULL)
				break;
		str[soff].suffix = p - u;
		if (ch != '\0')
			++p;
		++soff;
	}

	/* If no magic strings, we're done. */
	if (soff == 0)
		goto format;

	 /* Get space for the reordered strings. */
	if ((rbp = malloc(nlen)) == NULL)
		goto ret;
	s_rbp = rbp;

	/*
	 * Reorder the strings into the message string based on argument
	 * order.
	 *
	 * !!!
	 * We ignore arguments that are out of order, i.e. if we don't find
	 * an argument, we continue.  Assume (almost certainly incorrectly)
	 * that whoever created the string knew what they were doing.
	 *
	 * !!!
	 * Brute force "sort", but since we don't expect more than one or two
	 * arguments in a string, the setup cost of a fast sort will be more
	 * expensive than the loop.
	 */
	for (cnt1 = 1; cnt1 <= soff; ++cnt1)
		for (cnt2 = 0; cnt2 < soff; ++cnt2)
			if (cnt1 == str[cnt2].arg) {
				memmove(s_rbp, str[cnt2].str, str[cnt2].prefix);
				memmove(s_rbp + str[cnt2].prefix,
				    str[cnt2].str + str[cnt2].prefix +
				    str[cnt2].skip, str[cnt2].suffix);
				s_rbp += str[cnt2].prefix + str[cnt2].suffix;
				*s_rbp++ =
				    gp == NULL ? DEFAULT_NOPRINT : gp->noprint;
				break;
			}
	*s_rbp = '\0';
	fmt = rbp;
#endif

#ifndef NL_ARGMAX
format:	/* Format the arguments into the string. */
#endif
	va_start(ap, fmt);
	len = vsnprintf(mp, REM, fmt, ap);
	va_end(ap);
	if (len >= nlen)
		goto retry;

#ifndef NL_ARGMAX
	if (soff == 0)
		goto nofmt;

	/*
	 * Go through the resulting string, and, for each separator character
	 * separated string, enter its new starting position and length in the
	 * array.
	 */
	for (p = t = mp, cnt1 = 1,
	    ch = gp == NULL ? DEFAULT_NOPRINT : gp->noprint; *p != '\0'; ++p)
		if (*p == ch) {
			for (cnt2 = 0; cnt2 < soff; ++cnt2)
				if (str[cnt2].arg == cnt1)
					break;
			str[cnt2].str = t;
			str[cnt2].prefix = p - t;
			t = p + 1;
			++cnt1;
		}

	/*
	 * Reorder the strings once again, putting them back into the
	 * message buffer.
	 *
	 * !!!
	 * Note, the length of the message gets decremented once for
	 * each substring, when we discard the separator character.
	 */
	for (s_rbp = rbp, cnt1 = 0; cnt1 < soff; ++cnt1) {
		memmove(rbp, str[cnt1].str, str[cnt1].prefix);
		rbp += str[cnt1].prefix;
		--len;
	}
	memmove(mp, s_rbp, rbp - s_rbp);

	/* Free the reordered string memory. */
	free(s_rbp);
#endif

nofmt:	mp += len;
	if ((mlen += len) > blen)
		goto retry;
	if (mt == M_SYSERR) {
		len = snprintf(mp, REM, ": %s", strerror(errno));
		mp += len;
		if ((mlen += len) > blen)
			goto retry;
		mt = M_ERR;
	}

	/* Add trailing newline. */
	if ((mlen += 1) > blen)
		goto retry;
	*mp = '\n';

	if (sp != NULL)
		(void)ex_fflush(sp);
	if (gp != NULL)
		gp->scr_msg(sp, mt, bp, mlen);
	else
		(void)fprintf(stderr, "%.*s", (int)mlen, bp);

	/* Cleanup. */
#ifndef NL_ARGMAX
ret:
#endif
	FREE_SPACE(sp, bp, blen);
alloc_err:
	reenter = 0;
}

/*
 * msgq_wstr --
 *	Display a message with an embedded string.
 *
 * PUBLIC: void msgq_wstr(SCR *, mtype_t, const CHAR_T *, const char *);
 */
void
msgq_wstr(
	SCR *sp,
	mtype_t mtype,
	const CHAR_T *str,
	const char *fmt)
{
	size_t nlen;
	CONST char *nstr;

	if (str == NULL) {
		msgq(sp, mtype, "%s", fmt);
		return;
	}
	INT2CHAR(sp, str, STRLEN(str) + 1, nstr, nlen);
	msgq_str(sp, mtype, nstr, fmt);
}

/*
 * msgq_str --
 *	Display a message with an embedded string.
 *
 * PUBLIC: void msgq_str(SCR *, mtype_t, const char *, const char *);
 */
void
msgq_str(
	SCR *sp,
	mtype_t mtype,
	const char *str,
	const char *fmt)
{
	int nf, sv_errno;
	char *p;

	if (str == NULL) {
		msgq(sp, mtype, "%s", fmt);
		return;
	}

	sv_errno = errno;
	p = msg_print(sp, str, &nf);
	errno = sv_errno;
	msgq(sp, mtype, fmt, p);
	if (nf)
		FREE_SPACE(sp, p, 0);
}

/*
 * mod_rpt --
 *	Report on the lines that changed.
 *
 * !!!
 * Historic vi documentation (USD:15-8) claimed that "The editor will also
 * always tell you when a change you make affects text which you cannot see."
 * This wasn't true -- edit a large file and do "100d|1".  We don't implement
 * this semantic since it requires tracking each line that changes during a
 * command instead of just keeping count.
 *
 * Line counts weren't right in historic vi, either.  For example, given the
 * file:
 *	abc
 *	def
 * the command 2d}, from the 'b' would report that two lines were deleted,
 * not one.
 *
 * PUBLIC: void mod_rpt(SCR *);
 */
void
mod_rpt(SCR *sp)
{
	static char * const action[] = {
		"293|added",
		"294|changed",
		"295|deleted",
		"296|joined",
		"297|moved",
		"298|shifted",
		"299|yanked",
	};
	static char * const lines[] = {
		"300|line",
		"301|lines",
	};
	recno_t total;
	u_long rptval;
	int first, cnt;
	size_t blen, len, tlen;
	const char *t;
	char * const *ap;
	char *bp, *p;

	/* Change reports are turned off in batch mode. */
	if (F_ISSET(sp, SC_EX_SILENT))
		return;

	/* Reset changing line number. */
	sp->rptlchange = OOBLNO;

	/*
	 * Don't build a message if not enough changed.
	 *
	 * !!!
	 * And now, a vi clone test.  Historically, vi reported if the number
	 * of changed lines was > than the value, not >=, unless it was a yank
	 * command, which used >=.  No lie.  Furthermore, an action was never
	 * reported for a single line action.  This is consistent for actions
	 * other than yank, but yank didn't report single line actions even if
	 * the report edit option was set to 1.  In addition, setting report to
	 * 0 in the 4BSD historic vi was equivalent to setting it to 1, for an
	 * unknown reason (this bug was fixed in System III/V at some point).
	 * I got complaints, so nvi conforms to System III/V historic practice
	 * except that we report a yank of 1 line if report is set to 1.
	 */
#define	ARSIZE(a)	sizeof(a) / sizeof (*a)
#define	MAXNUM		25
	rptval = O_VAL(sp, O_REPORT);
	for (cnt = 0, total = 0; cnt < ARSIZE(action); ++cnt)
		total += sp->rptlines[cnt];
	if (total == 0)
		return;
	if (total <= rptval && sp->rptlines[L_YANKED] < rptval) {
		for (cnt = 0; cnt < ARSIZE(action); ++cnt)
			sp->rptlines[cnt] = 0;
		return;
	}

	/* Build and display the message. */
	GET_SPACE_GOTOC(sp, bp, blen, sizeof(action) * MAXNUM + 1);
	for (p = bp, first = 1, tlen = 0,
	    ap = action, cnt = 0; cnt < ARSIZE(action); ++ap, ++cnt)
		if (sp->rptlines[cnt] != 0) {
			if (first)
				first = 0;
			else {
				*p++ = ';';
				*p++ = ' ';
				tlen += 2;
			}
			len = snprintf(p, MAXNUM, "%lu ",
			    (u_long)sp->rptlines[cnt]);
			p += len;
			tlen += len;
			t = msg_cat(sp,
			    lines[sp->rptlines[cnt] == 1 ? 0 : 1], &len);
			memcpy(p, t, len);
			p += len;
			tlen += len;
			*p++ = ' ';
			++tlen;
			t = msg_cat(sp, *ap, &len);
			memcpy(p, t, len);
			p += len;
			tlen += len;
			sp->rptlines[cnt] = 0;
		}

	/* Add trailing newline. */
	*p = '\n';
	++tlen;

	(void)ex_fflush(sp);
	sp->gp->scr_msg(sp, M_INFO, bp, tlen);

	FREE_SPACE(sp, bp, blen);
alloc_err:
	return;

#undef ARSIZE
#undef MAXNUM
}

/*
 * msgq_status --
 *	Report on the file's status.
 *
 * PUBLIC: void msgq_status(SCR *, recno_t, u_int);
 */
void
msgq_status(
	SCR *sp,
	recno_t lno,
	u_int flags)
{
	recno_t last;
	size_t blen, len;
	int cnt, needsep;
	const char *t;
	char **ap, *bp, *np, *p, *s, *ep;
	CHAR_T *wp;
	size_t wlen;

	/* Get sufficient memory. */
	len = strlen(sp->frp->name);
	GET_SPACE_GOTOC(sp, bp, blen, len * MAX_CHARACTER_COLUMNS + 128);
	p = bp;
	ep = bp + blen;

	/* Convert the filename. */
	CHAR2INT(sp, sp->frp->name, len + 1, wp, wlen);

	/* Copy in the filename. */
	for (; *wp != '\0'; ++wp) {
		len = KEY_LEN(sp, *wp);
		memcpy(p, KEY_NAME(sp, *wp), len);
		p += len;
	}
	np = p;
	*p++ = ':';
	*p++ = ' ';

	/* Copy in the argument count. */
	if (F_ISSET(sp, SC_STATUS_CNT) && sp->argv != NULL) {
		for (cnt = 0, ap = sp->argv; *ap != NULL; ++ap, ++cnt);
		if (cnt > 1) {
			(void)snprintf(p, ep - p,
			    msg_cat(sp, "317|%d files to edit", NULL), cnt);
			p += strlen(p);
			*p++ = ':';
			*p++ = ' ';
		}
		F_CLR(sp, SC_STATUS_CNT);
	}

	/*
	 * See nvi/exf.c:file_init() for a description of how and when the
	 * read-only bit is set.
	 *
	 * !!!
	 * The historic display for "name changed" was "[Not edited]".
	 */
	needsep = 0;
	if (F_ISSET(sp->frp, FR_NEWFILE)) {
		F_CLR(sp->frp, FR_NEWFILE);
		t = msg_cat(sp, "021|new file", &len);
		memcpy(p, t, len);
		p += len;
		needsep = 1;
	} else {
		if (F_ISSET(sp->frp, FR_NAMECHANGE)) {
			t = msg_cat(sp, "022|name changed", &len);
			memcpy(p, t, len);
			p += len;
			needsep = 1;
		}
		if (needsep) {
			*p++ = ',';
			*p++ = ' ';
		}
		if (F_ISSET(sp->ep, F_MODIFIED))
			t = msg_cat(sp, "023|modified", &len);
		else
			t = msg_cat(sp, "024|unmodified", &len);
		memcpy(p, t, len);
		p += len;
		needsep = 1;
	}
	if (F_ISSET(sp->frp, FR_UNLOCKED)) {
		if (needsep) {
			*p++ = ',';
			*p++ = ' ';
		}
		t = msg_cat(sp, "025|UNLOCKED", &len);
		memcpy(p, t, len);
		p += len;
		needsep = 1;
	}
	if (O_ISSET(sp, O_READONLY)) {
		if (needsep) {
			*p++ = ',';
			*p++ = ' ';
		}
		t = msg_cat(sp, "026|readonly", &len);
		memcpy(p, t, len);
		p += len;
		needsep = 1;
	}
	if (needsep) {
		*p++ = ':';
		*p++ = ' ';
	}
	if (LF_ISSET(MSTAT_SHOWLAST)) {
		if (db_last(sp, &last))
			return;
		if (last == 0) {
			t = msg_cat(sp, "028|empty file", &len);
			memcpy(p, t, len);
			p += len;
		} else {
			t = msg_cat(sp, "027|line %lu of %lu [%ld%%]", &len);
			(void)snprintf(p, ep - p, t, (u_long)lno, (u_long)last,
			    ((u_long)lno * 100) / last);
			p += strlen(p);
		}
	} else {
		t = msg_cat(sp, "029|line %lu", &len);
		(void)snprintf(p, ep - p, t, (u_long)lno);
		p += strlen(p);
	}
#ifdef DEBUG
	(void)snprintf(p, ep - p, " (pid %lu)", (u_long)getpid());
	p += strlen(p);
#endif
	*p++ = '\n';
	len = p - bp;

	/*
	 * There's a nasty problem with long path names.  Cscope and tags files
	 * can result in long paths and vi will request a continuation key from
	 * the user as soon as it starts the screen.  Unfortunately, the user
	 * has already typed ahead, and chaos results.  If we assume that the
	 * characters in the filenames and informational messages only take a
	 * single screen column each, we can trim the filename.
	 *
	 * XXX
	 * Status lines get put up at fairly awkward times.  For example, when
	 * you do a filter read (e.g., :read ! echo foo) in the top screen of a
	 * split screen, we have to repaint the status lines for all the screens
	 * below the top screen.  We don't want users having to enter continue
	 * characters for those screens.  Make it really hard to screw this up.
	 */
	s = bp;
	if (LF_ISSET(MSTAT_TRUNCATE) && len > sp->cols) {
		for (; s < np && (*s != '/' || (p - s) > sp->cols - 3); ++s);
		if (s == np) {
			s = p - (sp->cols - 5);
			*--s = ' ';
		}
		*--s = '.';
		*--s = '.';
		*--s = '.';
		len = p - s;
	}

	/* Flush any waiting ex messages. */
	(void)ex_fflush(sp);

	sp->gp->scr_msg(sp, M_INFO, s, len);

	FREE_SPACE(sp, bp, blen);
alloc_err:
	return;
}

/*
 * msg_open --
 *	Open the message catalogs.
 *
 * PUBLIC: int msg_open(SCR *, char *);
 */
int
msg_open(
	SCR *sp,
	char *file)
{
	/*
	 * !!!
	 * Assume that the first file opened is the system default, and that
	 * all subsequent ones user defined.  Only display error messages
	 * if we can't open the user defined ones -- it's useful to know if
	 * the system one wasn't there, but if nvi is being shipped with an
	 * installed system, the file will be there, if it's not, then the
	 * message will be repeated every time nvi is started up.
	 */
	static int first = 1;
	nl_catd catd;
	char *p;
	int rval = 0;

	if ((p = strrchr(file, '/')) != NULL && p[1] == '\0') {
		/* Confirms to XPG4. */
		if ((p = join(file, setlocale(LC_MESSAGES, NULL))) == NULL) {
			msgq(sp, M_SYSERR, NULL);
			return (1);
		}
	} else {
		/* Make sure it's recognized as a path by catopen(3). */
		if ((p = join(".", file)) == NULL) {
			msgq(sp, M_SYSERR, NULL);
			return (1);
		}
	}
	errno = 0;
	if ((catd = catopen(p, NL_CAT_LOCALE)) == (nl_catd)-1) {
		if (first) {
			first = 0;
			rval = 1;
			goto ret;
		}

		/*
		 * POSIX.1-2008 gives no instruction on how to report a
		 * corrupt catalog file.  Errno == 0 is not rare; add
		 * EFTYPE, which is seen on FreeBSD, for a good measure.
		 */
		if (errno == 0 || errno == EFTYPE)
			msgq_str(sp, M_ERR, p,
			    "030|The file %s is not a message catalog");
		else
			msgq_str(sp, M_SYSERR, p, "%s");
		rval = 1;
		goto ret;
	}
	first = 0;

	msg_close(sp->gp);
	sp->gp->catd = catd;
ret:	free(p);
	return (rval);
}

/*
 * msg_close --
 *	Close the message catalogs.
 *
 * PUBLIC: void msg_close(GS *);
 */
void
msg_close(GS *gp)
{
	if (gp->catd != (nl_catd)-1)
		(void)catclose(gp->catd);
}

/*
 * msg_cont --
 *	Return common continuation messages.
 *
 * PUBLIC: const char *msg_cmsg(SCR *, cmsg_t, size_t *);
 */
const char *
msg_cmsg(
	SCR *sp,
	cmsg_t which,
	size_t *lenp)
{
	switch (which) {
	case CMSG_CONF:
		return (msg_cat(sp, "268|confirm? [ynq]", lenp));
	case CMSG_CONT:
		return (msg_cat(sp, "269|Press any key to continue: ", lenp));
	case CMSG_CONT_EX:
		return (msg_cat(sp,
	    "270|Press any key to continue [: to enter more ex commands]: ",
		    lenp));
	case CMSG_CONT_R:
		return (msg_cat(sp, "161|Press Enter to continue: ", lenp));
	case CMSG_CONT_S:
		return (msg_cat(sp, "275| cont?", lenp));
	case CMSG_CONT_Q:
		return (msg_cat(sp,
		    "271|Press any key to continue [q to quit]: ", lenp));
	default:
		abort();
	}
	/* NOTREACHED */
}

/*
 * msg_cat --
 *	Return a single message from the catalog, plus its length.
 *
 * !!!
 * Only a single catalog message can be accessed at a time, if multiple
 * ones are needed, they must be copied into local memory.
 *
 * PUBLIC: const char *msg_cat(SCR *, const char *, size_t *);
 */
const char *
msg_cat(
	SCR *sp,
	const char *str,
	size_t *lenp)
{
	GS *gp;
	char *p;
	int msgno;

	/*
	 * If it's not a catalog message, i.e. has doesn't have a leading
	 * number and '|' symbol, we're done.
	 */
	if (isdigit(str[0]) &&
	    isdigit(str[1]) && isdigit(str[2]) && str[3] == '|') {
		msgno = atoi(str);
		str = &str[4];

		gp = sp == NULL ? NULL : sp->gp;
		if (gp != NULL && gp->catd != (nl_catd)-1 &&
		    (p = catgets(gp->catd, 1, msgno, str)) != NULL) {
			if (lenp != NULL)
				*lenp = strlen(p);
			return (p);
		}
	}
	if (lenp != NULL)
		*lenp = strlen(str);
	return (str);
}

/*
 * msg_print --
 *	Return a printable version of a string, in allocated memory.
 *
 * PUBLIC: char *msg_print(SCR *, const char *, int *);
 */
char *
msg_print(
	SCR *sp,
	const char *s,
	int *needfree)
{
	size_t blen, nlen;
	char *bp, *ep, *p, *t;
	CHAR_T *wp, *cp;
	size_t wlen;

	*needfree = 0;

	/* XXX Not good for debugging ex_read & ex_filter.*/
	CHAR2INT5(sp, EXP(sp)->ibcw, (char *)s, strlen(s) + 1, wp, wlen);
	for (cp = wp; *cp != '\0'; ++cp)
		if (!ISPRINT(*cp))
			break;
	if (*cp == '\0')
		return ((char *)s);	/* SAFE: needfree set to 0. */

	nlen = 0;
	if (0) {
retry:		if (sp == NULL)
			free(bp);
		else
			FREE_SPACE(sp, bp, blen);
		*needfree = 0;
	}
	nlen += 256;
	if (sp == NULL) {
		if ((bp = malloc(nlen)) == NULL)
			goto alloc_err;
	} else
		GET_SPACE_GOTOC(sp, bp, blen, nlen);
	if (0) {
alloc_err:	return ("");
	}
	*needfree = 1;

	for (p = bp, ep = (bp + blen) - 1; *wp != '\0' && p < ep; ++wp)
		for (t = KEY_NAME(sp, *wp); *t != '\0' && p < ep; *p++ = *t++);
	if (p == ep)
		goto retry;
	*p = '\0';
	return (bp);
}
