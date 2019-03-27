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
static const char sccsid[] = "$Id: ex_print.c,v 10.26 2013/11/02 02:11:07 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../common/common.h"

static int ex_prchars(SCR *,
    const CHAR_T *, size_t *, size_t, u_int, int);

/*
 * ex_list -- :[line [,line]] l[ist] [count] [flags]
 *
 *	Display the addressed lines such that the output is unambiguous.
 *
 * PUBLIC: int ex_list(SCR *, EXCMD *);
 */
int
ex_list(SCR *sp, EXCMD *cmdp)
{
	if (ex_print(sp, cmdp,
	    &cmdp->addr1, &cmdp->addr2, cmdp->iflags | E_C_LIST))
		return (1);
	sp->lno = cmdp->addr2.lno;
	sp->cno = cmdp->addr2.cno;
	return (0);
}

/*
 * ex_number -- :[line [,line]] nu[mber] [count] [flags]
 *
 *	Display the addressed lines with a leading line number.
 *
 * PUBLIC: int ex_number(SCR *, EXCMD *);
 */
int
ex_number(SCR *sp, EXCMD *cmdp)
{
	if (ex_print(sp, cmdp,
	    &cmdp->addr1, &cmdp->addr2, cmdp->iflags | E_C_HASH))
		return (1);
	sp->lno = cmdp->addr2.lno;
	sp->cno = cmdp->addr2.cno;
	return (0);
}

/*
 * ex_pr -- :[line [,line]] p[rint] [count] [flags]
 *
 *	Display the addressed lines.
 *
 * PUBLIC: int ex_pr(SCR *, EXCMD *);
 */
int
ex_pr(SCR *sp, EXCMD *cmdp)
{
	if (ex_print(sp, cmdp, &cmdp->addr1, &cmdp->addr2, cmdp->iflags))
		return (1);
	sp->lno = cmdp->addr2.lno;
	sp->cno = cmdp->addr2.cno;
	return (0);
}

/*
 * ex_print --
 *	Print the selected lines.
 *
 * PUBLIC: int ex_print(SCR *, EXCMD *, MARK *, MARK *, u_int32_t);
 */
int
ex_print(SCR *sp, EXCMD *cmdp, MARK *fp, MARK *tp, u_int32_t flags)
{
	GS *gp;
	recno_t from, to;
	size_t col, len;
	CHAR_T *p;
	CHAR_T buf[10];

	NEEDFILE(sp, cmdp);

	gp = sp->gp;
	for (from = fp->lno, to = tp->lno; from <= to; ++from) {
		col = 0;

		/*
		 * Display the line number.  The %6 format is specified
		 * by POSIX 1003.2, and is almost certainly large enough.
		 * Check, though, just in case.
		 */
		if (LF_ISSET(E_C_HASH)) {
			if (from <= 999999) {
				SPRINTF(buf, SIZE(buf), L("%6u  "), from);
				p = buf;
			} else
				p = L("TOOBIG  ");
			if (ex_prchars(sp, p, &col, 8, 0, 0))
				return (1);
		}

		/*
		 * Display the line.  The format for E_C_PRINT isn't very good,
		 * especially in handling end-of-line tabs, but they're almost
		 * backward compatible.
		 */
		if (db_get(sp, from, DBG_FATAL, &p, &len))
			return (1);

		if (len == 0 && !LF_ISSET(E_C_LIST))
			(void)ex_puts(sp, "\n");
		else if (ex_ldisplay(sp, p, len, col, flags))
			return (1);

		if (INTERRUPTED(sp))
			break;
	}
	return (0);
}

/*
 * ex_ldisplay --
 *	Display a line without any preceding number.
 *
 * PUBLIC: int ex_ldisplay(SCR *, const CHAR_T *, size_t, size_t, u_int);
 */
int
ex_ldisplay(SCR *sp, const CHAR_T *p, size_t len, size_t col, u_int flags)
{
	if (len > 0 && ex_prchars(sp, p, &col, len, LF_ISSET(E_C_LIST), 0))
		return (1);
	if (!INTERRUPTED(sp) && LF_ISSET(E_C_LIST)) {
		p = L("$");
		if (ex_prchars(sp, p, &col, 1, LF_ISSET(E_C_LIST), 0))
			return (1);
	}
	if (!INTERRUPTED(sp))
		(void)ex_puts(sp, "\n");
	return (0);
}

/*
 * ex_scprint --
 *	Display a line for the substitute with confirmation routine.
 *
 * PUBLIC: int ex_scprint(SCR *, MARK *, MARK *);
 */
int
ex_scprint(SCR *sp, MARK *fp, MARK *tp)
{
	CHAR_T *p;
	size_t col, len;

	col = 0;
	if (O_ISSET(sp, O_NUMBER)) {
		p = L("        ");
		if (ex_prchars(sp, p, &col, 8, 0, 0))
			return (1);
	}

	if (db_get(sp, fp->lno, DBG_FATAL, &p, &len))
		return (1);

	if (ex_prchars(sp, p, &col, fp->cno, 0, ' '))
		return (1);
	p += fp->cno;
	if (ex_prchars(sp,
	    p, &col, tp->cno == fp->cno ? 1 : tp->cno - fp->cno, 0, '^'))
		return (1);
	if (INTERRUPTED(sp))
		return (1);
	p = L("[ynq]");		/* XXX: should be msg_cat. */
	if (ex_prchars(sp, p, &col, 5, 0, 0))
		return (1);
	(void)ex_fflush(sp);
	return (0);
}

/*
 * ex_prchars --
 *	Local routine to dump characters to the screen.
 */
static int
ex_prchars(SCR *sp, const CHAR_T *p, size_t *colp, size_t len, 
	    u_int flags, int repeatc)
{
	CHAR_T ch;
	char *kp;
	GS *gp;
	size_t col, tlen, ts;

	if (O_ISSET(sp, O_LIST))
		LF_SET(E_C_LIST);
	gp = sp->gp;
	ts = O_VAL(sp, O_TABSTOP);
	for (col = *colp; len--;)
		if ((ch = *p++) == L('\t') && !LF_ISSET(E_C_LIST))
			for (tlen = ts - col % ts;
			    col < sp->cols && tlen--; ++col) {
				(void)ex_printf(sp,
				    "%c", repeatc ? repeatc : ' ');
				if (INTERRUPTED(sp))
					goto intr;
			}
		else {
			kp = KEY_NAME(sp, ch);
			tlen = KEY_COL(sp, ch);

			/*
			 * Start a new line if the last character does not fit
			 * into the current line.  The implicit new lines are
			 * not interruptible.
			 */
			if (col + tlen > sp->cols) {
				col = 0;
				(void)ex_puts(sp, "\n");
			}

			col += tlen;
			if (!repeatc) {
				(void)ex_puts(sp, kp);
				if (INTERRUPTED(sp))
					goto intr;
			} else while (tlen--) {
				(void)ex_printf(sp, "%c", repeatc);
				if (INTERRUPTED(sp))
					goto intr;
			}
			if (col == sp->cols) {
				col = 0;
				(void)ex_puts(sp, "\n");
			}
		}
intr:	*colp = col;
	return (0);
}

/*
 * ex_printf --
 *	Ex's version of printf.
 *
 * PUBLIC: int ex_printf(SCR *, const char *, ...);
 */
int
ex_printf(
	SCR *sp,
	const char *fmt,
	...)
{
	EX_PRIVATE *exp;
	va_list ap;
	size_t n;

	exp = EXP(sp);

	va_start(ap, fmt);
	exp->obp_len += n = vsnprintf(exp->obp + exp->obp_len,
	    sizeof(exp->obp) - exp->obp_len, fmt, ap);
	va_end(ap);

	/* Flush when reach a <newline> or half the buffer. */
	if (exp->obp[exp->obp_len - 1] == '\n' ||
	    exp->obp_len > sizeof(exp->obp) / 2)
		(void)ex_fflush(sp);
	return (n);
}

/*
 * ex_puts --
 *	Ex's version of puts.
 *
 * PUBLIC: int ex_puts(SCR *, const char *);
 */
int
ex_puts(SCR *sp, const char *str)
{
	EX_PRIVATE *exp;
	int doflush, n;

	exp = EXP(sp);

	/* Flush when reach a <newline> or the end of the buffer. */
	for (doflush = n = 0; *str != '\0'; ++n) {
		if (exp->obp_len > sizeof(exp->obp))
			(void)ex_fflush(sp);
		if ((exp->obp[exp->obp_len++] = *str++) == '\n')
			doflush = 1;
	}
	if (doflush)
		(void)ex_fflush(sp);
	return (n);
}

/*
 * ex_fflush --
 *	Ex's version of fflush.
 *
 * PUBLIC: int ex_fflush(SCR *sp);
 */
int
ex_fflush(SCR *sp)
{
	EX_PRIVATE *exp;

	exp = EXP(sp);

	if (exp->obp_len != 0) {
		sp->gp->scr_msg(sp, M_NONE, exp->obp, exp->obp_len);
		exp->obp_len = 0;
	}
	return (0);
}
