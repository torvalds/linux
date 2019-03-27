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
static const char sccsid[] = "$Id: v_init.c,v 10.10 2012/02/11 00:33:46 zy Exp $";
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

#include "../common/common.h"
#include "vi.h"

/*
 * v_screen_copy --
 *	Copy vi screen.
 *
 * PUBLIC: int v_screen_copy(SCR *, SCR *);
 */
int
v_screen_copy(SCR *orig, SCR *sp)
{
	VI_PRIVATE *ovip, *nvip;

	/* Create the private vi structure. */
	CALLOC_RET(orig, nvip, VI_PRIVATE *, 1, sizeof(VI_PRIVATE));
	sp->vi_private = nvip;

	/* Invalidate the line size cache. */
	VI_SCR_CFLUSH(nvip);

	if (orig == NULL) {
		nvip->csearchdir = CNOTSET;
	} else {
		ovip = VIP(orig);

		/* User can replay the last input, but nothing else. */
		if (ovip->rep_len != 0) {
			MALLOC_RET(orig, nvip->rep, EVENT *, ovip->rep_len);
			memmove(nvip->rep, ovip->rep, ovip->rep_len);
			nvip->rep_len = ovip->rep_len;
		}

		/* Copy the match characters information. */
		if (ovip->mcs != NULL && (nvip->mcs =
		    v_wstrdup(sp, ovip->mcs, STRLEN(ovip->mcs))) == NULL)
			return (1);

		/* Copy the paragraph/section information. */
		if (ovip->ps != NULL && (nvip->ps =
		    v_strdup(sp, ovip->ps, strlen(ovip->ps))) == NULL)
			return (1);

		nvip->lastckey = ovip->lastckey;
		nvip->csearchdir = ovip->csearchdir;

		nvip->srows = ovip->srows;
	}
	return (0);
}

/*
 * v_screen_end --
 *	End a vi screen.
 *
 * PUBLIC: int v_screen_end(SCR *);
 */
int
v_screen_end(SCR *sp)
{
	VI_PRIVATE *vip;

	if ((vip = VIP(sp)) == NULL)
		return (0);
	if (vip->keyw != NULL)
		free(vip->keyw);
	if (vip->rep != NULL)
		free(vip->rep);
	if (vip->mcs != NULL)
		free(vip->mcs);
	if (vip->ps != NULL)
		free(vip->ps);

	if (HMAP != NULL)
		free(HMAP);

	free(vip);
	sp->vi_private = NULL;

	return (0);
}

/*
 * v_optchange --
 *	Handle change of options for vi.
 *
 * PUBLIC: int v_optchange(SCR *, int, char *, u_long *);
 */
int
v_optchange(SCR *sp, int offset, char *str, u_long *valp)
{
	switch (offset) {
	case O_MATCHCHARS:
		return (v_buildmcs(sp, str));
	case O_PARAGRAPHS:
		return (v_buildps(sp, str, O_STR(sp, O_SECTIONS)));
	case O_SECTIONS:
		return (v_buildps(sp, O_STR(sp, O_PARAGRAPHS), str));
	case O_WINDOW:
		return (vs_crel(sp, *valp));
	}
	return (0);
}
