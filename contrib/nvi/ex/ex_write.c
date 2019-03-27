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
static const char sccsid[] = "$Id: ex_write.c,v 10.43 2015/04/03 15:18:45 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"

enum which {WN, WQ, WRITE, XIT};
static int exwr(SCR *, EXCMD *, enum which);

/*
 * ex_wn --	:wn[!] [>>] [file]
 *	Write to a file and switch to the next one.
 *
 * PUBLIC: int ex_wn(SCR *, EXCMD *);
 */
int
ex_wn(SCR *sp, EXCMD *cmdp)
{
	if (exwr(sp, cmdp, WN))
		return (1);
	if (file_m3(sp, 0))
		return (1);

	/* The file name isn't a new file to edit. */
	cmdp->argc = 0;

	return (ex_next(sp, cmdp));
}

/*
 * ex_wq --	:wq[!] [>>] [file]
 *	Write to a file and quit.
 *
 * PUBLIC: int ex_wq(SCR *, EXCMD *);
 */
int
ex_wq(SCR *sp, EXCMD *cmdp)
{
	int force;

	if (exwr(sp, cmdp, WQ))
		return (1);
	if (file_m3(sp, 0))
		return (1);

	force = FL_ISSET(cmdp->iflags, E_C_FORCE);

	if (ex_ncheck(sp, force))
		return (1);

	F_SET(sp, force ? SC_EXIT_FORCE : SC_EXIT);
	return (0);
}

/*
 * ex_write --	:write[!] [>>] [file]
 *		:write [!] [cmd]
 *	Write to a file.
 *
 * PUBLIC: int ex_write(SCR *, EXCMD *);
 */
int
ex_write(SCR *sp, EXCMD *cmdp)
{
	return (exwr(sp, cmdp, WRITE));
}


/*
 * ex_xit -- :x[it]! [file]
 *	Write out any modifications and quit.
 *
 * PUBLIC: int ex_xit(SCR *, EXCMD *);
 */
int
ex_xit(SCR *sp, EXCMD *cmdp)
{
	int force;

	NEEDFILE(sp, cmdp);

	if (F_ISSET(sp->ep, F_MODIFIED) && exwr(sp, cmdp, XIT))
		return (1);
	if (file_m3(sp, 0))
		return (1);

	force = FL_ISSET(cmdp->iflags, E_C_FORCE);

	if (ex_ncheck(sp, force))
		return (1);

	F_SET(sp, force ? SC_EXIT_FORCE : SC_EXIT);
	return (0);
}

/*
 * exwr --
 *	The guts of the ex write commands.
 */
static int
exwr(SCR *sp, EXCMD *cmdp, enum which cmd)
{
	MARK rm;
	int flags;
	char *name;
	CHAR_T *p = NULL;
	size_t nlen;
	char *n;
	int rc;
	EX_PRIVATE *exp;

	NEEDFILE(sp, cmdp);

	/* All write commands can have an associated '!'. */
	LF_INIT(FS_POSSIBLE);
	if (FL_ISSET(cmdp->iflags, E_C_FORCE))
		LF_SET(FS_FORCE);

	/* Skip any leading whitespace. */
	if (cmdp->argc != 0)
		for (p = cmdp->argv[0]->bp; *p != '\0' && cmdskip(*p); ++p);

	/* If "write !" it's a pipe to a utility. */
	if (cmdp->argc != 0 && cmd == WRITE && *p == '!') {
		/* Secure means no shell access. */
		if (O_ISSET(sp, O_SECURE)) {
			ex_wemsg(sp, cmdp->cmd->name, EXM_SECURE_F);
			return (1);
		}

		/* Expand the argument. */
		for (++p; *p && cmdskip(*p); ++p);
		if (*p == '\0') {
			ex_emsg(sp, cmdp->cmd->usage, EXM_USAGE);
			return (1);
		}
		if (argv_exp1(sp, cmdp, p, STRLEN(p), 1))
			return (1);

		/* Set the last bang command */
		exp = EXP(sp);
		free(exp->lastbcomm);
		exp->lastbcomm = v_wstrdup(sp, cmdp->argv[1]->bp,
		    cmdp->argv[1]->len);

		/*
		 * Historically, vi waited after a write filter even if there
		 * wasn't any output from the command.  People complained when
		 * nvi waited only if there was output, wanting the visual cue
		 * that the program hadn't written anything.
		 */
		F_SET(sp, SC_EX_WAIT_YES);

		/*
		 * !!!
		 * Ignore the return cursor position, the cursor doesn't
		 * move.
		 */
		if (ex_filter(sp, cmdp, &cmdp->addr1,
		    &cmdp->addr2, &rm, cmdp->argv[1]->bp, FILTER_WRITE))
			return (1);

		/* Ex terminates with a bang, even if the command fails. */
		if (!F_ISSET(sp, SC_VI) && !F_ISSET(sp, SC_EX_SILENT))
			(void)ex_puts(sp, "!\n");

		return (0);
	}

	/* Set the FS_ALL flag if we're writing the entire file. */
	if (cmdp->addr1.lno <= 1 && !db_exist(sp, cmdp->addr2.lno + 1))
		LF_SET(FS_ALL);

	/* If "write >>" it's an append to a file. */
	if (cmdp->argc != 0 && cmd != XIT && p[0] == '>' && p[1] == '>') {
		LF_SET(FS_APPEND);

		/* Skip ">>" and whitespace. */
		for (p += 2; *p && cmdskip(*p); ++p);
	}

	/* If no other arguments, just write the file back. */
	if (cmdp->argc == 0 || *p == '\0')
		return (file_write(sp,
		    &cmdp->addr1, &cmdp->addr2, NULL, flags));

	/* Build an argv so we get an argument count and file expansion. */
	if (argv_exp2(sp, cmdp, p, STRLEN(p)))
		return (1);

	/*
	 *  0 args: impossible.
	 *  1 args: impossible (I hope).
	 *  2 args: read it.
	 * >2 args: object, too many args.
	 *
	 * The 1 args case depends on the argv_sexp() function refusing
	 * to return success without at least one non-blank character.
	 */
	switch (cmdp->argc) {
	case 0:
	case 1:
		abort();
		/* NOTREACHED */
	case 2:
		INT2CHAR(sp, cmdp->argv[1]->bp, cmdp->argv[1]->len+1,
			 n, nlen);
		name = v_strdup(sp, n, nlen - 1);

		/*
		 * !!!
		 * Historically, the read and write commands renamed
		 * "unnamed" files, or, if the file had a name, set
		 * the alternate file name.
		 */
		if (F_ISSET(sp->frp, FR_TMPFILE) &&
		    !F_ISSET(sp->frp, FR_EXNAMED)) {
			if ((n = v_strdup(sp, name, nlen - 1)) != NULL) {
				free(sp->frp->name);
				sp->frp->name = n;
			}
			/*
			 * The file has a real name, it's no longer a
			 * temporary, clear the temporary file flags.
			 *
			 * !!!
			 * If we're writing the whole file, FR_NAMECHANGE
			 * will be cleared by the write routine -- this is
			 * historic practice.
			 */
			F_CLR(sp->frp, FR_TMPEXIT | FR_TMPFILE);
			F_SET(sp->frp, FR_NAMECHANGE | FR_EXNAMED);

			/* Notify the screen. */
			(void)sp->gp->scr_rename(sp, sp->frp->name, 1);
		} else
			set_alt_name(sp, name);
		break;
	default:
		INT2CHAR(sp, p, STRLEN(p) + 1, n, nlen);
		ex_emsg(sp, n, EXM_FILECOUNT);
		return (1);
	}

	rc = file_write(sp, &cmdp->addr1, &cmdp->addr2, name, flags);

	free(name);

	return rc;
}

/*
 * ex_writefp --
 *	Write a range of lines to a FILE *.
 *
 * PUBLIC: int ex_writefp(SCR *,
 * PUBLIC:    char *, FILE *, MARK *, MARK *, u_long *, u_long *, int);
 */
int
ex_writefp(SCR *sp, char *name, FILE *fp, MARK *fm, MARK *tm, u_long *nlno, u_long *nch, int silent)
{
	struct stat sb;
	GS *gp;
	u_long ccnt;			/* XXX: can't print off_t portably. */
	recno_t fline, tline, lcnt;
	size_t len;
	int rval;
	char *msg, *p;

	gp = sp->gp;
	fline = fm->lno;
	tline = tm->lno;

	if (nlno != NULL) {
		*nch = 0;
		*nlno = 0;
	}

	/*
	 * The vi filter code has multiple processes running simultaneously,
	 * and one of them calls ex_writefp().  The "unsafe" function calls
	 * in this code are to db_get() and msgq().  Db_get() is safe, see
	 * the comment in ex_filter.c:ex_filter() for details.  We don't call
	 * msgq if the multiple process bit in the EXF is set.
	 *
	 * !!!
	 * Historic vi permitted files of 0 length to be written.  However,
	 * since the way vi got around dealing with "empty" files was to
	 * always have a line in the file no matter what, it wrote them as
	 * files of a single, empty line.  We write empty files.
	 *
	 * "Alex, I'll take vi trivia for $1000."
	 */
	ccnt = 0;
	lcnt = 0;
	msg = "253|Writing...";
	if (tline != 0)
		for (; fline <= tline; ++fline, ++lcnt) {
			/* Caller has to provide any interrupt message. */
			if ((lcnt + 1) % INTERRUPT_CHECK == 0) {
				if (INTERRUPTED(sp))
					break;
				if (!silent) {
					gp->scr_busy(sp, msg, msg == NULL ?
					    BUSY_UPDATE : BUSY_ON);
					msg = NULL;
				}
			}
			if (db_rget(sp, fline, &p, &len))
				goto err;
			if (fwrite(p, 1, len, fp) != len)
				goto err;
			ccnt += len;
			if (putc('\n', fp) != '\n')
				break;
			++ccnt;
		}

	if (fflush(fp))
		goto err;
	/*
	 * XXX
	 * I don't trust NFS -- check to make sure that we're talking to
	 * a regular file and sync so that NFS is forced to flush.
	 */
	if (!fstat(fileno(fp), &sb) &&
	    S_ISREG(sb.st_mode) && fsync(fileno(fp)))
		goto err;

	if (fclose(fp))
		goto err;

	rval = 0;
	if (0) {
err:		if (!F_ISSET(sp->ep, F_MULTILOCK))
			msgq_str(sp, M_SYSERR, name, "%s");
		(void)fclose(fp);
		rval = 1;
	}

	if (!silent)
		gp->scr_busy(sp, NULL, BUSY_OFF);

	/* Report the possibly partial transfer. */
	if (nlno != NULL) {
		*nch = ccnt;
		*nlno = lcnt;
	}
	return (rval);
}
