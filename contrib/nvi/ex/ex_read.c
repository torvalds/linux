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
static const char sccsid[] = "$Id: ex_read.c,v 10.44 2001/06/25 15:19:19 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"
#include "../vi/vi.h"

/*
 * ex_read --	:read [file]
 *		:read [!cmd]
 *	Read from a file or utility.
 *
 * !!!
 * Historical vi wouldn't undo a filter read, for no apparent reason.
 *
 * PUBLIC: int ex_read(SCR *, EXCMD *);
 */
int
ex_read(SCR *sp, EXCMD *cmdp)
{
	enum { R_ARG, R_EXPANDARG, R_FILTER } which;
	struct stat sb;
	CHAR_T *arg = NULL;
	char *name = NULL;
	size_t nlen;
	EX_PRIVATE *exp;
	FILE *fp;
	FREF *frp;
	GS *gp;
	MARK rm;
	recno_t nlines;
	size_t arglen = 0;
	int argc, rval;
	char *p;

	gp = sp->gp;

	/*
	 * 0 args: read the current pathname.
	 * 1 args: check for "read !arg".
	 */
	switch (cmdp->argc) {
	case 0:
		which = R_ARG;
		break;
	case 1:
		arg = cmdp->argv[0]->bp;
		arglen = cmdp->argv[0]->len;
		if (*arg == '!') {
			++arg;
			--arglen;
			which = R_FILTER;

			/* Secure means no shell access. */
			if (O_ISSET(sp, O_SECURE)) {
				ex_wemsg(sp, cmdp->cmd->name, EXM_SECURE_F);
				return (1);
			}
		} else
			which = R_EXPANDARG;
		break;
	default:
		abort();
		/* NOTREACHED */
	}

	/* Load a temporary file if no file being edited. */
	if (sp->ep == NULL) {
		if ((frp = file_add(sp, NULL)) == NULL)
			return (1);
		if (file_init(sp, frp, NULL, 0))
			return (1);
	}

	switch (which) {
	case R_FILTER:
		/*
		 * File name and bang expand the user's argument.  If
		 * we don't get an additional argument, it's illegal.
		 */
		argc = cmdp->argc;
		if (argv_exp1(sp, cmdp, arg, arglen, 1))
			return (1);
		if (argc == cmdp->argc) {
			ex_emsg(sp, cmdp->cmd->usage, EXM_USAGE);
			return (1);
		}
		argc = cmdp->argc - 1;

		/* Set the last bang command. */
		exp = EXP(sp);
		if (exp->lastbcomm != NULL)
			free(exp->lastbcomm);
		if ((exp->lastbcomm =
		    v_wstrdup(sp, cmdp->argv[argc]->bp,
				cmdp->argv[argc]->len)) == NULL) {
			msgq(sp, M_SYSERR, NULL);
			return (1);
		}

		/*
		 * Vi redisplayed the user's argument if it changed, ex
		 * always displayed a !, plus the user's argument if it
		 * changed.
		 */
		if (F_ISSET(sp, SC_VI)) {
			if (F_ISSET(cmdp, E_MODIFY))
				(void)vs_update(sp, "!", cmdp->argv[argc]->bp);
		} else {
			if (F_ISSET(cmdp, E_MODIFY))
				(void)ex_printf(sp,
				    "!"WS"\n", cmdp->argv[argc]->bp);
			else
				(void)ex_puts(sp, "!\n");
			(void)ex_fflush(sp);
		}

		/*
		 * Historically, filter reads as the first ex command didn't
		 * wait for the user. If SC_SCR_EXWROTE not already set, set
		 * the don't-wait flag.
		 */
		if (!F_ISSET(sp, SC_SCR_EXWROTE))
			F_SET(sp, SC_EX_WAIT_NO);

		/*
		 * Switch into ex canonical mode.  The reason to restore the
		 * original terminal modes for read filters is so that users
		 * can do things like ":r! cat /dev/tty".
		 *
		 * !!!
		 * We do not output an extra <newline>, so that we don't touch
		 * the screen on a normal read.
		 */
		if (F_ISSET(sp, SC_VI)) {
			if (gp->scr_screen(sp, SC_EX)) {
				ex_wemsg(sp, cmdp->cmd->name, EXM_NOCANON_F);
				return (1);
			}
			/*
			 * !!!
			 * Historically, the read command doesn't switch to
			 * the alternate X11 xterm screen, if doing a filter
			 * read -- don't set SA_ALTERNATE.
			 */
			F_SET(sp, SC_SCR_EX | SC_SCR_EXWROTE);
		}

		if (ex_filter(sp, cmdp, &cmdp->addr1,
		    NULL, &rm, cmdp->argv[argc]->bp, FILTER_READ))
			return (1);

		/* The filter version of read set the autoprint flag. */
		F_SET(cmdp, E_AUTOPRINT);

		/*
		 * If in vi mode, move to the first nonblank.  Might have
		 * switched into ex mode, so saved the original SC_VI value.
		 */
		sp->lno = rm.lno;
		if (F_ISSET(sp, SC_VI)) {
			sp->cno = 0;
			(void)nonblank(sp, sp->lno, &sp->cno);
		}
		return (0);
	case R_ARG:
		name = sp->frp->name;
		break;
	case R_EXPANDARG:
		if (argv_exp2(sp, cmdp, arg, arglen))
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
			INT2CHAR(sp, cmdp->argv[1]->bp, cmdp->argv[1]->len + 1, 
				 name, nlen);
			/*
			 * !!!
			 * Historically, the read and write commands renamed
			 * "unnamed" files, or, if the file had a name, set
			 * the alternate file name.
			 */
			if (F_ISSET(sp->frp, FR_TMPFILE) &&
			    !F_ISSET(sp->frp, FR_EXNAMED)) {
				if ((p = strdup(name)) != NULL) {
					free(sp->frp->name);
					sp->frp->name = p;
				}
				/*
				 * The file has a real name, it's no longer a
				 * temporary, clear the temporary file flags.
				 */
				F_CLR(sp->frp, FR_TMPEXIT | FR_TMPFILE);
				F_SET(sp->frp, FR_NAMECHANGE | FR_EXNAMED);

				/* Notify the screen. */
				(void)sp->gp->scr_rename(sp, sp->frp->name, 1);
				name = sp->frp->name;
			} else {
				set_alt_name(sp, name);
				name = sp->alt_name;
			}
			break;
		default:
			ex_wemsg(sp, cmdp->argv[0]->bp, EXM_FILECOUNT);
			return (1);
		
		}
		break;
	}

	/*
	 * !!!
	 * Historically, vi did not permit reads from non-regular files, nor
	 * did it distinguish between "read !" and "read!", so there was no
	 * way to "force" it.  We permit reading from named pipes too, since
	 * they didn't exist when the original implementation of vi was done
	 * and they seem a reasonable addition.
	 */
	if ((fp = fopen(name, "r")) == NULL || fstat(fileno(fp), &sb)) {
		msgq_str(sp, M_SYSERR, name, "%s");
		return (1);
	}
	if (!S_ISFIFO(sb.st_mode) && !S_ISREG(sb.st_mode)) {
		(void)fclose(fp);
		msgq(sp, M_ERR,
		    "145|Only regular files and named pipes may be read");
		return (1);
	}

	/* Try and get a lock. */
	if (file_lock(sp, NULL, fileno(fp), 0) == LOCK_UNAVAIL)
		msgq(sp, M_ERR, "146|%s: read lock was unavailable", name);

	rval = ex_readfp(sp, name, fp, &cmdp->addr1, &nlines, 0);

	/*
	 * In vi, set the cursor to the first line read in, if anything read
	 * in, otherwise, the address.  (Historic vi set it to the line after
	 * the address regardless, but since that line may not exist we don't
	 * bother.)
	 *
	 * In ex, set the cursor to the last line read in, if anything read in,
	 * otherwise, the address.
	 */
	if (F_ISSET(sp, SC_VI)) {
		sp->lno = cmdp->addr1.lno;
		if (nlines)
			++sp->lno;
	} else
		sp->lno = cmdp->addr1.lno + nlines;
	return (rval);
}

/*
 * ex_readfp --
 *	Read lines into the file.
 *
 * PUBLIC: int ex_readfp(SCR *, char *, FILE *, MARK *, recno_t *, int);
 */
int
ex_readfp(SCR *sp, char *name, FILE *fp, MARK *fm, recno_t *nlinesp, int silent)
{
	EX_PRIVATE *exp;
	GS *gp;
	recno_t lcnt, lno;
	size_t len;
	u_long ccnt;			/* XXX: can't print off_t portably. */
	int nf, rval;
	char *p;
	size_t wlen;
	CHAR_T *wp;

	gp = sp->gp;
	exp = EXP(sp);

	/*
	 * Add in the lines from the output.  Insertion starts at the line
	 * following the address.
	 */
	ccnt = 0;
	lcnt = 0;
	p = "147|Reading...";
	for (lno = fm->lno; !ex_getline(sp, fp, &len); ++lno, ++lcnt) {
		if ((lcnt + 1) % INTERRUPT_CHECK == 0) {
			if (INTERRUPTED(sp))
				break;
			if (!silent) {
				gp->scr_busy(sp, p,
				    p == NULL ? BUSY_UPDATE : BUSY_ON);
				p = NULL;
			}
		}
		FILE2INT5(sp, exp->ibcw, exp->ibp, len, wp, wlen);
		if (db_append(sp, 1, lno, wp, wlen))
			goto err;
		ccnt += len;
	}

	if (ferror(fp) || fclose(fp))
		goto err;

	/* Return the number of lines read in. */
	if (nlinesp != NULL)
		*nlinesp = lcnt;

	if (!silent) {
		p = msg_print(sp, name, &nf);
		msgq(sp, M_INFO,
		    "148|%s: %lu lines, %lu characters", p,
		    (u_long)lcnt, ccnt);
		if (nf)
			FREE_SPACE(sp, p, 0);
	}

	rval = 0;
	if (0) {
err:		msgq_str(sp, M_SYSERR, name, "%s");
		(void)fclose(fp);
		rval = 1;
	}

	if (!silent)
		gp->scr_busy(sp, NULL, BUSY_OFF);
	return (rval);
}
