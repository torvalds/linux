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
static const char sccsid[] = "$Id: options_f.c,v 10.34 04/07/11 16:06:29 zy Exp $";
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
#include <unistd.h>

#include "common.h"

/*
 * PUBLIC: int f_altwerase(SCR *, OPTION *, char *, u_long *);
 */
int
f_altwerase(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	if (*valp)
		O_CLR(sp, O_TTYWERASE);
	return (0);
}

/*
 * PUBLIC: int f_columns(SCR *, OPTION *, char *, u_long *);
 */
int
f_columns(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	/* Validate the number. */
	if (*valp < MINIMUM_SCREEN_COLS) {
		msgq(sp, M_ERR, "040|Screen columns too small, less than %d",
		    MINIMUM_SCREEN_COLS);
		return (1);
	}

	/*
	 * !!!
	 * It's not uncommon for allocation of huge chunks of memory to cause
	 * core dumps on various systems.  So, we prune out numbers that are
	 * "obviously" wrong.  Vi will not work correctly if it has the wrong
	 * number of lines/columns for the screen, but at least we don't drop
	 * core.
	 */
#define	MAXIMUM_SCREEN_COLS	500
	if (*valp > MAXIMUM_SCREEN_COLS) {
		msgq(sp, M_ERR, "041|Screen columns too large, greater than %d",
		    MAXIMUM_SCREEN_COLS);
		return (1);
	}
	return (0);
}

/*
 * PUBLIC: int f_lines(SCR *, OPTION *, char *, u_long *);
 */
int
f_lines(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	/* Validate the number. */
	if (*valp < MINIMUM_SCREEN_ROWS) {
		msgq(sp, M_ERR, "042|Screen lines too small, less than %d",
		    MINIMUM_SCREEN_ROWS);
		return (1);
	}

	/*
	 * !!!
	 * It's not uncommon for allocation of huge chunks of memory to cause
	 * core dumps on various systems.  So, we prune out numbers that are
	 * "obviously" wrong.  Vi will not work correctly if it has the wrong
	 * number of lines/columns for the screen, but at least we don't drop
	 * core.
	 */
#define	MAXIMUM_SCREEN_ROWS	500
	if (*valp > MAXIMUM_SCREEN_ROWS) {
		msgq(sp, M_ERR, "043|Screen lines too large, greater than %d",
		    MAXIMUM_SCREEN_ROWS);
		return (1);
	}

	/*
	 * Set the value, and the related scroll value.  If no window
	 * value set, set a new default window.
	 */
	o_set(sp, O_LINES, 0, NULL, *valp);
	if (*valp == 1) {
		sp->defscroll = 1;

		if (O_VAL(sp, O_WINDOW) == O_D_VAL(sp, O_WINDOW) ||
		    O_VAL(sp, O_WINDOW) > *valp) {
			o_set(sp, O_WINDOW, 0, NULL, 1);
			o_set(sp, O_WINDOW, OS_DEF, NULL, 1);
		}
	} else {
		sp->defscroll = (*valp - 1) / 2;

		if (O_VAL(sp, O_WINDOW) == O_D_VAL(sp, O_WINDOW) ||
		    O_VAL(sp, O_WINDOW) > *valp) {
			o_set(sp, O_WINDOW, 0, NULL, *valp - 1);
			o_set(sp, O_WINDOW, OS_DEF, NULL, *valp - 1);
		}
	}
	return (0);
}

/*
 * PUBLIC: int f_lisp(SCR *, OPTION *, char *, u_long *);
 */
int
f_lisp(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	msgq(sp, M_ERR, "044|The lisp option is not implemented");
	return (0);
}

/*
 * PUBLIC: int f_msgcat(SCR *, OPTION *, char *, u_long *);
 */
int
f_msgcat(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	(void)msg_open(sp, str);
	return (0);
}

/*
 * PUBLIC: int f_print(SCR *, OPTION *, char *, u_long *);
 */
int
f_print(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	int offset = op - sp->opts;

	/* Preset the value, needed for reinitialization of lookup table. */
	if (offset == O_OCTAL) {
		if (*valp)
			O_SET(sp, offset);
		else
			O_CLR(sp, offset);
	} else if (o_set(sp, offset, OS_STRDUP, str, 0))
		return(1);

	/* Reinitialize the key fast lookup table. */
	v_key_ilookup(sp);

	/* Reformat the screen. */
	F_SET(sp, SC_SCR_REFORMAT);
	return (0);
}

/*
 * PUBLIC: int f_readonly(SCR *, OPTION *, char *, u_long *);
 */
int
f_readonly(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	/*
	 * !!!
	 * See the comment in exf.c.
	 */
	if (*valp)
		F_SET(sp, SC_READONLY);
	else
		F_CLR(sp, SC_READONLY);
	return (0);
}

/*
 * PUBLIC: int f_recompile(SCR *, OPTION *, char *, u_long *);
 */
int
f_recompile(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	if (F_ISSET(sp, SC_RE_SEARCH)) {
		regfree(&sp->re_c);
		F_CLR(sp, SC_RE_SEARCH);
	}
	if (F_ISSET(sp, SC_RE_SUBST)) {
		regfree(&sp->subre_c);
		F_CLR(sp, SC_RE_SUBST);
	}
	return (0);
}

/*
 * PUBLIC: int f_reformat(SCR *, OPTION *, char *, u_long *);
 */
int
f_reformat(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	F_SET(sp, SC_SCR_REFORMAT);
	return (0);
}

/*
 * PUBLIC: int f_ttywerase(SCR *, OPTION *, char *, u_long *);
 */
int
f_ttywerase(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	if (*valp)
		O_CLR(sp, O_ALTWERASE);
	return (0);
}

/*
 * PUBLIC: int f_w300(SCR *, OPTION *, char *, u_long *);
 */
int
f_w300(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	u_long v;

	/* Historical behavior for w300 was < 1200. */
	if (sp->gp->scr_baud(sp, &v))
		return (1);
	if (v >= 1200)
		return (0);

	return (f_window(sp, op, str, valp));
}

/*
 * PUBLIC: int f_w1200(SCR *, OPTION *, char *, u_long *);
 */
int
f_w1200(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	u_long v;

	/* Historical behavior for w1200 was == 1200. */
	if (sp->gp->scr_baud(sp, &v))
		return (1);
	if (v < 1200 || v > 4800)
		return (0);

	return (f_window(sp, op, str, valp));
}

/*
 * PUBLIC: int f_w9600(SCR *, OPTION *, char *, u_long *);
 */
int
f_w9600(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	u_long v;

	/* Historical behavior for w9600 was > 1200. */
	if (sp->gp->scr_baud(sp, &v))
		return (1);
	if (v <= 4800)
		return (0);

	return (f_window(sp, op, str, valp));
}

/*
 * PUBLIC: int f_window(SCR *, OPTION *, char *, u_long *);
 */
int
f_window(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	if (*valp >= O_VAL(sp, O_LINES) - 1 &&
	    (*valp = O_VAL(sp, O_LINES) - 1) == 0)
		*valp = 1;
	return (0);
}

/*
 * PUBLIC: int f_encoding(SCR *, OPTION *, char *, u_long *);
 */
int
f_encoding(
	SCR *sp,
	OPTION *op,
	char *str,
	u_long *valp)
{
	int offset = op - sp->opts;

	return conv_enc(sp, offset, str);
}
