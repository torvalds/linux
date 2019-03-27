/*-
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: ex_argv.c,v 11.2 2012/10/09 23:00:29 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"

static int argv_alloc(SCR *, size_t);
static int argv_comp(const void *, const void *);
static int argv_fexp(SCR *, EXCMD *,
	CHAR_T *, size_t, CHAR_T *, size_t *, CHAR_T **, size_t *, int);
static int argv_sexp(SCR *, CHAR_T **, size_t *, size_t *);
static int argv_flt_user(SCR *, EXCMD *, CHAR_T *, size_t);

/*
 * argv_init --
 *	Build  a prototype arguments list.
 *
 * PUBLIC: int argv_init(SCR *, EXCMD *);
 */
int
argv_init(SCR *sp, EXCMD *excp)
{
	EX_PRIVATE *exp;

	exp = EXP(sp);
	exp->argsoff = 0;
	argv_alloc(sp, 1);

	excp->argv = exp->args;
	excp->argc = exp->argsoff;
	return (0);
}

/*
 * argv_exp0 --
 *	Append a string to the argument list.
 *
 * PUBLIC: int argv_exp0(SCR *, EXCMD *, CHAR_T *, size_t);
 */
int
argv_exp0(SCR *sp, EXCMD *excp, CHAR_T *cmd, size_t cmdlen)
{
	EX_PRIVATE *exp;

	exp = EXP(sp);
	argv_alloc(sp, cmdlen);
	MEMCPY(exp->args[exp->argsoff]->bp, cmd, cmdlen);
	exp->args[exp->argsoff]->bp[cmdlen] = '\0';
	exp->args[exp->argsoff]->len = cmdlen;
	++exp->argsoff;
	excp->argv = exp->args;
	excp->argc = exp->argsoff;
	return (0);
}

/*
 * argv_exp1 --
 *	Do file name expansion on a string, and append it to the
 *	argument list.
 *
 * PUBLIC: int argv_exp1(SCR *, EXCMD *, CHAR_T *, size_t, int);
 */
int
argv_exp1(SCR *sp, EXCMD *excp, CHAR_T *cmd, size_t cmdlen, int is_bang)
{
	EX_PRIVATE *exp;
	size_t blen, len;
	CHAR_T *p, *t, *bp;

	GET_SPACE_RETW(sp, bp, blen, 512);

	len = 0;
	exp = EXP(sp);
	if (argv_fexp(sp, excp, cmd, cmdlen, bp, &len, &bp, &blen, is_bang)) {
		FREE_SPACEW(sp, bp, blen);
		return (1);
	}

	/* If it's empty, we're done. */
	if (len != 0) {
		for (p = bp, t = bp + len; p < t; ++p)
			if (!cmdskip(*p))
				break;
		if (p == t)
			goto ret;
	} else
		goto ret;

	(void)argv_exp0(sp, excp, bp, len);

ret:	FREE_SPACEW(sp, bp, blen);
	return (0);
}

/*
 * argv_exp2 --
 *	Do file name and shell expansion on a string, and append it to
 *	the argument list.
 *
 * PUBLIC: int argv_exp2(SCR *, EXCMD *, CHAR_T *, size_t);
 */
int
argv_exp2(SCR *sp, EXCMD *excp, CHAR_T *cmd, size_t cmdlen)
{
	size_t blen, len, n;
	int rval;
	CHAR_T *bp, *p;

	GET_SPACE_RETW(sp, bp, blen, 512);

#define	SHELLECHO	L("echo ")
#define	SHELLOFFSET	(SIZE(SHELLECHO) - 1)
	MEMCPY(bp, SHELLECHO, SHELLOFFSET);
	p = bp + SHELLOFFSET;
	len = SHELLOFFSET;

#if defined(DEBUG) && 0
	TRACE(sp, "file_argv: {%.*s}\n", (int)cmdlen, cmd);
#endif

	if (argv_fexp(sp, excp, cmd, cmdlen, p, &len, &bp, &blen, 0)) {
		rval = 1;
		goto err;
	}

#if defined(DEBUG) && 0
	TRACE(sp, "before shell: %d: {%s}\n", len, bp);
#endif

	/*
	 * Do shell word expansion -- it's very, very hard to figure out what
	 * magic characters the user's shell expects.  Historically, it was a
	 * union of v7 shell and csh meta characters.  We match that practice
	 * by default, so ":read \%" tries to read a file named '%'.  It would
	 * make more sense to pass any special characters through the shell,
	 * but then, if your shell was csh, the above example will behave
	 * differently in nvi than in vi.  If you want to get other characters
	 * passed through to your shell, change the "meta" option.
	 */
	if (opts_empty(sp, O_SHELL, 1) || opts_empty(sp, O_SHELLMETA, 1))
		n = 0;
	else {
		p = bp + SHELLOFFSET;
		n = len - SHELLOFFSET;
		for (; n > 0; --n, ++p)
			if (IS_SHELLMETA(sp, *p))
				break;
	}

	/*
	 * If we found a meta character in the string, fork a shell to expand
	 * it.  Unfortunately, this is comparatively slow.  Historically, it
	 * didn't matter much, since users don't enter meta characters as part
	 * of pathnames that frequently.  The addition of filename completion
	 * broke that assumption because it's easy to use.  To increase the
	 * completion performance, nvi used to have an internal routine to
	 * handle "filename*".  However, the shell special characters does not
	 * limit to "shellmeta", so such a hack breaks historic practice.
	 * After it all, we split the completion logic out from here.
	 */
	switch (n) {
	case 0:
		p = bp + SHELLOFFSET;
		len -= SHELLOFFSET;
		rval = argv_exp3(sp, excp, p, len);
		break;
	default:
		if (argv_sexp(sp, &bp, &blen, &len)) {
			rval = 1;
			goto err;
		}
		p = bp;
		rval = argv_exp3(sp, excp, p, len);
		break;
	}

err:	FREE_SPACEW(sp, bp, blen);
	return (rval);
}

/*
 * argv_exp3 --
 *	Take a string and break it up into an argv, which is appended
 *	to the argument list.
 *
 * PUBLIC: int argv_exp3(SCR *, EXCMD *, CHAR_T *, size_t);
 */
int
argv_exp3(SCR *sp, EXCMD *excp, CHAR_T *cmd, size_t cmdlen)
{
	EX_PRIVATE *exp;
	size_t len;
	int ch, off;
	CHAR_T *ap, *p;

	for (exp = EXP(sp); cmdlen > 0; ++exp->argsoff) {
		/* Skip any leading whitespace. */
		for (; cmdlen > 0; --cmdlen, ++cmd) {
			ch = *cmd;
			if (!cmdskip(ch))
				break;
		}
		if (cmdlen == 0)
			break;

		/*
		 * Determine the length of this whitespace delimited
		 * argument.
		 *
		 * QUOTING NOTE:
		 *
		 * Skip any character preceded by the user's quoting
		 * character.
		 */
		for (ap = cmd, len = 0; cmdlen > 0; ++cmd, --cmdlen, ++len) {
			ch = *cmd;
			if (IS_ESCAPE(sp, excp, ch) && cmdlen > 1) {
				++cmd;
				--cmdlen;
			} else if (cmdskip(ch))
				break;
		}

		/*
		 * Copy the argument into place.
		 *
		 * QUOTING NOTE:
		 *
		 * Lose quote chars.
		 */
		argv_alloc(sp, len);
		off = exp->argsoff;
		exp->args[off]->len = len;
		for (p = exp->args[off]->bp; len > 0; --len, *p++ = *ap++)
			if (IS_ESCAPE(sp, excp, *ap))
				++ap;
		*p = '\0';
	}
	excp->argv = exp->args;
	excp->argc = exp->argsoff;

#if defined(DEBUG) && 0
	for (cnt = 0; cnt < exp->argsoff; ++cnt)
		TRACE(sp, "arg %d: {%s}\n", cnt, exp->argv[cnt]);
#endif
	return (0);
}

/*
 * argv_flt_ex --
 *	Filter the ex commands with a prefix, and append the results to
 *	the argument list.
 *
 * PUBLIC: int argv_flt_ex(SCR *, EXCMD *, CHAR_T *, size_t);
 */
int
argv_flt_ex(SCR *sp, EXCMD *excp, CHAR_T *cmd, size_t cmdlen)
{
	EX_PRIVATE *exp;
	EXCMDLIST const *cp;
	int off;
	size_t len;

	exp = EXP(sp);

	for (off = exp->argsoff, cp = cmds; cp->name != NULL; ++cp) {
		len = STRLEN(cp->name);
		if (cmdlen > 0 &&
		    (cmdlen > len || MEMCMP(cmd, cp->name, cmdlen)))
			continue;

		/* Copy the matched ex command name. */
		argv_alloc(sp, len + 1);
		MEMCPY(exp->args[exp->argsoff]->bp, cp->name, len + 1);
		exp->args[exp->argsoff]->len = len;
		++exp->argsoff;
		excp->argv = exp->args;
		excp->argc = exp->argsoff;
	}

	return (0);
}

/*
 * argv_flt_user --
 *	Filter the ~user list on the system with a prefix, and append
 *	the results to the argument list.
 */
static int
argv_flt_user(SCR *sp, EXCMD *excp, CHAR_T *uname, size_t ulen)
{
	EX_PRIVATE *exp;
	struct passwd *pw;
	int off;
	char *np;
	size_t len, nlen;

	exp = EXP(sp);
	off = exp->argsoff;

	/* The input must come with a leading '~'. */
	INT2CHAR(sp, uname + 1, ulen - 1, np, nlen);
	if ((np = v_strdup(sp, np, nlen)) == NULL)
		return (1);

	setpwent();
	while ((pw = getpwent()) != NULL) {
		len = strlen(pw->pw_name);
		if (nlen > 0 &&
		    (nlen > len || memcmp(np, pw->pw_name, nlen)))
			continue;

		/* Copy '~' + the matched user name. */
		CHAR2INT(sp, pw->pw_name, len + 1, uname, ulen);
		argv_alloc(sp, ulen + 1);
		exp->args[exp->argsoff]->bp[0] = '~';
		MEMCPY(exp->args[exp->argsoff]->bp + 1, uname, ulen);
		exp->args[exp->argsoff]->len = ulen;
		++exp->argsoff;
		excp->argv = exp->args;
		excp->argc = exp->argsoff;
	}
	endpwent();
	free(np);

	qsort(exp->args + off, exp->argsoff - off, sizeof(ARGS *), argv_comp);
	return (0);
}

/*
 * argv_fexp --
 *	Do file name and bang command expansion.
 */
static int
argv_fexp(SCR *sp, EXCMD *excp, CHAR_T *cmd, size_t cmdlen, CHAR_T *p, size_t *lenp, CHAR_T **bpp, size_t *blenp, int is_bang)
{
	EX_PRIVATE *exp;
	char *t;
	size_t blen, len, off, tlen;
	CHAR_T *bp;
	CHAR_T *wp;
	size_t wlen;

	/* Replace file name characters. */
	for (bp = *bpp, blen = *blenp, len = *lenp; cmdlen > 0; --cmdlen, ++cmd)
		switch (*cmd) {
		case '!':
			if (!is_bang)
				goto ins_ch;
			exp = EXP(sp);
			if (exp->lastbcomm == NULL) {
				msgq(sp, M_ERR,
				    "115|No previous command to replace \"!\"");
				return (1);
			}
			len += tlen = STRLEN(exp->lastbcomm);
			off = p - bp;
			ADD_SPACE_RETW(sp, bp, blen, len);
			p = bp + off;
			MEMCPY(p, exp->lastbcomm, tlen);
			p += tlen;
			F_SET(excp, E_MODIFY);
			break;
		case '%':
			if ((t = sp->frp->name) == NULL) {
				msgq(sp, M_ERR,
				    "116|No filename to substitute for %%");
				return (1);
			}
			tlen = strlen(t);
			len += tlen;
			off = p - bp;
			ADD_SPACE_RETW(sp, bp, blen, len);
			p = bp + off;
			CHAR2INT(sp, t, tlen, wp, wlen);
			MEMCPY(p, wp, wlen);
			p += wlen;
			F_SET(excp, E_MODIFY);
			break;
		case '#':
			if ((t = sp->alt_name) == NULL) {
				msgq(sp, M_ERR,
				    "117|No filename to substitute for #");
				return (1);
			}
			len += tlen = strlen(t);
			off = p - bp;
			ADD_SPACE_RETW(sp, bp, blen, len);
			p = bp + off;
			CHAR2INT(sp, t, tlen, wp, wlen);
			MEMCPY(p, wp, wlen);
			p += wlen;
			F_SET(excp, E_MODIFY);
			break;
		case '\\':
			/*
			 * QUOTING NOTE:
			 *
			 * Strip any backslashes that protected the file
			 * expansion characters.
			 */
			if (cmdlen > 1 &&
			    (cmd[1] == '%' || cmd[1] == '#' || cmd[1] == '!')) {
				++cmd;
				--cmdlen;
			}
			/* FALLTHROUGH */
		default:
ins_ch:			++len;
			off = p - bp;
			ADD_SPACE_RETW(sp, bp, blen, len);
			p = bp + off;
			*p++ = *cmd;
		}

	/* Nul termination. */
	++len;
	off = p - bp;
	ADD_SPACE_RETW(sp, bp, blen, len);
	p = bp + off;
	*p = '\0';

	/* Return the new string length, buffer, buffer length. */
	*lenp = len - 1;
	*bpp = bp;
	*blenp = blen;
	return (0);
}

/*
 * argv_alloc --
 *	Make more space for arguments.
 */
static int
argv_alloc(SCR *sp, size_t len)
{
	ARGS *ap;
	EX_PRIVATE *exp;
	int cnt, off;

	/*
	 * Allocate room for another argument, always leaving
	 * enough room for an ARGS structure with a length of 0.
	 */
#define	INCREMENT	20
	exp = EXP(sp);
	off = exp->argsoff;
	if (exp->argscnt == 0 || off + 2 >= exp->argscnt - 1) {
		cnt = exp->argscnt + INCREMENT;
		REALLOC(sp, exp->args, ARGS **, cnt * sizeof(ARGS *));
		if (exp->args == NULL) {
			(void)argv_free(sp);
			goto mem;
		}
		memset(&exp->args[exp->argscnt], 0, INCREMENT * sizeof(ARGS *));
		exp->argscnt = cnt;
	}

	/* First argument. */
	if (exp->args[off] == NULL) {
		CALLOC(sp, exp->args[off], ARGS *, 1, sizeof(ARGS));
		if (exp->args[off] == NULL)
			goto mem;
	}

	/* First argument buffer. */
	ap = exp->args[off];
	ap->len = 0;
	if (ap->blen < len + 1) {
		ap->blen = len + 1;
		REALLOC(sp, ap->bp, CHAR_T *, ap->blen * sizeof(CHAR_T));
		if (ap->bp == NULL) {
			ap->bp = NULL;
			ap->blen = 0;
			F_CLR(ap, A_ALLOCATED);
mem:			msgq(sp, M_SYSERR, NULL);
			return (1);
		}
		F_SET(ap, A_ALLOCATED);
	}

	/* Second argument. */
	if (exp->args[++off] == NULL) {
		CALLOC(sp, exp->args[off], ARGS *, 1, sizeof(ARGS));
		if (exp->args[off] == NULL)
			goto mem;
	}
	/* 0 length serves as end-of-argument marker. */
	exp->args[off]->len = 0;
	return (0);
}

/*
 * argv_free --
 *	Free up argument structures.
 *
 * PUBLIC: int argv_free(SCR *);
 */
int
argv_free(SCR *sp)
{
	EX_PRIVATE *exp;
	int off;

	exp = EXP(sp);
	if (exp->args != NULL) {
		for (off = 0; off < exp->argscnt; ++off) {
			if (exp->args[off] == NULL)
				continue;
			if (F_ISSET(exp->args[off], A_ALLOCATED))
				free(exp->args[off]->bp);
			free(exp->args[off]);
		}
		free(exp->args);
	}
	exp->args = NULL;
	exp->argscnt = 0;
	exp->argsoff = 0;
	return (0);
}

/*
 * argv_flt_path --
 *	Find all file names matching the prefix and append them to the
 *	argument list.
 *
 * PUBLIC: int argv_flt_path(SCR *, EXCMD *, CHAR_T *, size_t);
 */
int
argv_flt_path(SCR *sp, EXCMD *excp, CHAR_T *path, size_t plen)
{
	struct dirent *dp;
	DIR *dirp;
	EX_PRIVATE *exp;
	int off;
	size_t dlen, len, nlen;
	CHAR_T *dname;
	CHAR_T *p, *np, *n;
	char *name, *tp, *epd = NULL;
	CHAR_T *wp;
	size_t wlen;

	exp = EXP(sp);

	/* Set up the name and length for comparison. */
	if ((path = v_wstrdup(sp, path, plen)) == NULL)
		return (1);
	if ((p = STRRCHR(path, '/')) == NULL) {
		if (*path == '~') {
			int rc;
			
			/* Filter ~user list instead. */
			rc = argv_flt_user(sp, excp, path, plen);
			free(path);
			return (rc);
		}
		dname = L(".");
		dlen = 0;
		np = path;
	} else {
		if (p == path) {
			dname = L("/");
			dlen = 1;
		} else {
			*p = '\0';
			dname = path;
			dlen = p - path;
		}
		np = p + 1;
	}

	INT2CHAR(sp, dname, dlen + 1, tp, nlen);
	if ((epd = expanduser(tp)) != NULL)
		tp = epd;
	if ((dirp = opendir(tp)) == NULL) {
		free(epd);
		free(path);
		return (1);
	}
	free(epd);

	INT2CHAR(sp, np, STRLEN(np), tp, nlen);
	if ((name = v_strdup(sp, tp, nlen)) == NULL) {
		free(path);
		return (1);
	}

	for (off = exp->argsoff; (dp = readdir(dirp)) != NULL;) {
		if (nlen == 0) {
			if (dp->d_name[0] == '.')
				continue;
			len = dp->d_namlen;
		} else {
			len = dp->d_namlen;
			if (len < nlen || memcmp(dp->d_name, name, nlen))
				continue;
		}

		/* Directory + name + slash + null. */
		CHAR2INT(sp, dp->d_name, len + 1, wp, wlen);
		argv_alloc(sp, dlen + wlen + 1);
		n = exp->args[exp->argsoff]->bp;
		if (dlen != 0) {
			MEMCPY(n, dname, dlen);
			n += dlen;
			if (dlen > 1 || dname[0] != '/')
				*n++ = '/';
			exp->args[exp->argsoff]->len = dlen + 1;
		}
		MEMCPY(n, wp, wlen);
		exp->args[exp->argsoff]->len += wlen - 1;
		++exp->argsoff;
		excp->argv = exp->args;
		excp->argc = exp->argsoff;
	}
	closedir(dirp);
	free(name);
	free(path);

	qsort(exp->args + off, exp->argsoff - off, sizeof(ARGS *), argv_comp);
	return (0);
}

/*
 * argv_comp --
 *	Alphabetic comparison.
 */
static int
argv_comp(const void *a, const void *b)
{
	return (STRCMP((*(ARGS **)a)->bp, (*(ARGS **)b)->bp));
}

/*
 * argv_sexp --
 *	Fork a shell, pipe a command through it, and read the output into
 *	a buffer.
 */
static int
argv_sexp(SCR *sp, CHAR_T **bpp, size_t *blenp, size_t *lenp)
{
	enum { SEXP_ERR, SEXP_EXPANSION_ERR, SEXP_OK } rval;
	FILE *ifp;
	pid_t pid;
	size_t blen, len;
	int ch, std_output[2];
	CHAR_T *bp, *p;
	char *sh, *sh_path;
	char *np;
	size_t nlen;

	/* Secure means no shell access. */
	if (O_ISSET(sp, O_SECURE)) {
		msgq(sp, M_ERR,
"289|Shell expansions not supported when the secure edit option is set");
		return (1);
	}

	sh_path = O_STR(sp, O_SHELL);
	if ((sh = strrchr(sh_path, '/')) == NULL)
		sh = sh_path;
	else
		++sh;

	/* Local copies of the buffer variables. */
	bp = *bpp;
	blen = *blenp;

	/*
	 * There are two different processes running through this code, named
	 * the utility (the shell) and the parent. The utility reads standard
	 * input and writes standard output and standard error output.  The
	 * parent writes to the utility, reads its standard output and ignores
	 * its standard error output.  Historically, the standard error output
	 * was discarded by vi, as it produces a lot of noise when file patterns
	 * don't match.
	 *
	 * The parent reads std_output[0], and the utility writes std_output[1].
	 */
	ifp = NULL;
	std_output[0] = std_output[1] = -1;
	if (pipe(std_output) < 0) {
		msgq(sp, M_SYSERR, "pipe");
		return (1);
	}
	if ((ifp = fdopen(std_output[0], "r")) == NULL) {
		msgq(sp, M_SYSERR, "fdopen");
		goto err;
	}

	/*
	 * Do the minimal amount of work possible, the shell is going to run
	 * briefly and then exit.  We sincerely hope.
	 */
	switch (pid = vfork()) {
	case -1:			/* Error. */
		msgq(sp, M_SYSERR, "vfork");
err:		if (ifp != NULL)
			(void)fclose(ifp);
		else if (std_output[0] != -1)
			close(std_output[0]);
		if (std_output[1] != -1)
			close(std_output[0]);
		return (1);
	case 0:				/* Utility. */
		/* Redirect stdout to the write end of the pipe. */
		(void)dup2(std_output[1], STDOUT_FILENO);

		/* Close the utility's file descriptors. */
		(void)close(std_output[0]);
		(void)close(std_output[1]);
		(void)close(STDERR_FILENO);

		/*
		 * XXX
		 * Assume that all shells have -c.
		 */
		INT2CHAR(sp, bp, STRLEN(bp)+1, np, nlen);
		execl(sh_path, sh, "-c", np, (char *)NULL);
		msgq_str(sp, M_SYSERR, sh_path, "118|Error: execl: %s");
		_exit(127);
	default:			/* Parent. */
		/* Close the pipe ends the parent won't use. */
		(void)close(std_output[1]);
		break;
	}

	/*
	 * Copy process standard output into a buffer.
	 *
	 * !!!
	 * Historic vi apparently discarded leading \n and \r's from
	 * the shell output stream.  We don't on the grounds that any
	 * shell that does that is broken.
	 */
	for (p = bp, len = 0, ch = EOF;
	    (ch = GETC(ifp)) != EOF; *p++ = ch, blen-=sizeof(CHAR_T), ++len)
		if (blen < 5) {
			ADD_SPACE_GOTOW(sp, bp, *blenp, *blenp * 2);
			p = bp + len;
			blen = *blenp - len;
		}

	/* Delete the final newline, nul terminate the string. */
	if (p > bp && (p[-1] == '\n' || p[-1] == '\r')) {
		--p;
		--len;
	}
	*p = '\0';
	*lenp = len;
	*bpp = bp;		/* *blenp is already updated. */

	if (ferror(ifp))
		goto ioerr;
	if (fclose(ifp)) {
ioerr:		msgq_str(sp, M_ERR, sh, "119|I/O error: %s");
alloc_err:	rval = SEXP_ERR;
	} else
		rval = SEXP_OK;

	/*
	 * Wait for the process.  If the shell process fails (e.g., "echo $q"
	 * where q wasn't a defined variable) or if the returned string has
	 * no characters or only blank characters, (e.g., "echo $5"), complain
	 * that the shell expansion failed.  We can't know for certain that's
	 * the error, but it's a good guess, and it matches historic practice.
	 * This won't catch "echo foo_$5", but that's not a common error and
	 * historic vi didn't catch it either.
	 */
	if (proc_wait(sp, (long)pid, sh, 1, 0))
		rval = SEXP_EXPANSION_ERR;

	for (p = bp; len; ++p, --len)
		if (!cmdskip(*p))
			break;
	if (len == 0)
		rval = SEXP_EXPANSION_ERR;

	if (rval == SEXP_EXPANSION_ERR)
		msgq(sp, M_ERR, "304|Shell expansion failed");

	return (rval == SEXP_OK ? 0 : 1);
}

/*
 * argv_esc --
 *	Escape a string into an ex and shell argument.
 *
 * PUBLIC: CHAR_T *argv_esc(SCR *, EXCMD *, CHAR_T *, size_t);
 */
CHAR_T *
argv_esc(SCR *sp, EXCMD *excp, CHAR_T *str, size_t len)
{
	size_t blen, off;
	CHAR_T *bp, *p;
	int ch;

	GET_SPACE_GOTOW(sp, bp, blen, len + 1);

	/*
	 * Leaving the first '~' unescaped causes the user to need a
	 * "./" prefix to edit a file which really starts with a '~'.
	 * However, the file completion happens to not work for these
	 * files without the prefix.
	 * 
	 * All ex expansion characters, "!%#", are double escaped.
	 */
	for (p = bp; len > 0; ++str, --len) {
		ch = *str;
		off = p - bp;
		if (blen / sizeof(CHAR_T) - off < 3) {
			ADD_SPACE_GOTOW(sp, bp, blen, off + 3);
			p = bp + off;
		}
		if (cmdskip(ch) || ch == '\n' ||
		    IS_ESCAPE(sp, excp, ch))			/* Ex. */
			*p++ = CH_LITERAL;
		else switch (ch) {
		case '~':					/* ~user. */
			if (p != bp)
				*p++ = '\\';
			break;
		case '+':					/* Ex +cmd. */
			if (p == bp)
				*p++ = '\\';
			break;
		case '!': case '%': case '#':			/* Ex exp. */
			*p++ = '\\';
			*p++ = '\\';
			break;
		case ',': case '-': case '.': case '/':		/* Safe. */
		case ':': case '=': case '@': case '_':
			break;
		default:					/* Unsafe. */
			if (isascii(ch) && !isalnum(ch))
				*p++ = '\\';
		}
		*p++ = ch;
	}
	*p = '\0';

	return bp;

alloc_err:
	return NULL;
}

/*
 * argv_uesc --
 *	Unescape an escaped ex and shell argument.
 *
 * PUBLIC: CHAR_T *argv_uesc(SCR *, EXCMD *, CHAR_T *, size_t);
 */
CHAR_T *
argv_uesc(SCR *sp, EXCMD *excp, CHAR_T *str, size_t len)
{
	size_t blen;
	CHAR_T *bp, *p;

	GET_SPACE_GOTOW(sp, bp, blen, len + 1);

	for (p = bp; len > 0; ++str, --len) {
		if (IS_ESCAPE(sp, excp, *str)) {
			if (--len < 1)
				break;
			++str;
		} else if (*str == '\\') {
			if (--len < 1)
				break;
			++str;

			/* Check for double escaping. */
			if (*str == '\\' && len > 1)
				switch (str[1]) {
				case '!': case '%': case '#':
					++str;
					--len;
				}
		}
		*p++ = *str;
	}
	*p = '\0';

	return bp;

alloc_err:
	return NULL;
}
