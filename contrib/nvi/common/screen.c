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
static const char sccsid[] = "$Id: screen.c,v 10.25 2011/12/04 04:06:45 zy Exp $";
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
#include <unistd.h>

#include "common.h"
#include "../vi/vi.h"

/*
 * screen_init --
 *	Do the default initialization of an SCR structure.
 *
 * PUBLIC: int screen_init(GS *, SCR *, SCR **);
 */
int
screen_init(
	GS *gp,
	SCR *orig,
	SCR **spp)
{
	SCR *sp;
	size_t len;

	*spp = NULL;
	CALLOC_RET(orig, sp, SCR *, 1, sizeof(SCR));
	*spp = sp;

/* INITIALIZED AT SCREEN CREATE. */
	sp->id = ++gp->id;
	sp->refcnt = 1;

	sp->gp = gp;			/* All ref the GS structure. */

	sp->ccnt = 2;			/* Anything > 1 */

	/*
	 * XXX
	 * sp->defscroll is initialized by the opts_init() code because
	 * we don't have the option information yet.
	 */

	TAILQ_INIT(sp->tiq);

/* PARTIALLY OR COMPLETELY COPIED FROM PREVIOUS SCREEN. */
	if (orig == NULL) {
		sp->searchdir = NOTSET;
	} else {
		/* Alternate file name. */
		if (orig->alt_name != NULL &&
		    (sp->alt_name = strdup(orig->alt_name)) == NULL)
			goto mem;

		/* Last executed at buffer. */
		if (F_ISSET(orig, SC_AT_SET)) {
			F_SET(sp, SC_AT_SET);
			sp->at_lbuf = orig->at_lbuf;
		}

		/* Retain searching/substitution information. */
		sp->searchdir = orig->searchdir == NOTSET ? NOTSET : FORWARD;
		if (orig->re != NULL && (sp->re =
		    v_wstrdup(sp, orig->re, orig->re_len)) == NULL)
			goto mem;
		sp->re_len = orig->re_len;
		if (orig->subre != NULL && (sp->subre =
		    v_wstrdup(sp, orig->subre, orig->subre_len)) == NULL)
			goto mem;
		sp->subre_len = orig->subre_len;
		if (orig->repl != NULL && (sp->repl =
		    v_wstrdup(sp, orig->repl, orig->repl_len)) == NULL)
			goto mem;
		sp->repl_len = orig->repl_len;
		if (orig->newl_len) {
			len = orig->newl_len * sizeof(size_t);
			MALLOC(sp, sp->newl, size_t *, len);
			if (sp->newl == NULL) {
mem:				msgq(orig, M_SYSERR, NULL);
				goto err;
			}
			sp->newl_len = orig->newl_len;
			sp->newl_cnt = orig->newl_cnt;
			memcpy(sp->newl, orig->newl, len);
		}

		if (opts_copy(orig, sp))
			goto err;

		F_SET(sp, F_ISSET(orig, SC_EX | SC_VI));
	}

	if (ex_screen_copy(orig, sp))		/* Ex. */
		goto err;
	if (v_screen_copy(orig, sp))		/* Vi. */
		goto err;
	sp->cl_private = 0;			/* XXX */
	conv_init(orig, sp);			/* XXX */

	*spp = sp;
	return (0);

err:	screen_end(sp);
	return (1);
}

/*
 * screen_end --
 *	Release a screen, no matter what had (and had not) been
 *	initialized.
 *
 * PUBLIC: int screen_end(SCR *);
 */
int
screen_end(SCR *sp)
{
	int rval;

	/* If multiply referenced, just decrement the count and return. */
	 if (--sp->refcnt != 0)
		 return (0);

	/*
	 * Remove the screen from the displayed queue.
	 *
	 * If a created screen failed during initialization, it may not
	 * be linked into the chain.
	 */
	if (TAILQ_ENTRY_ISVALID(sp, q))
		TAILQ_REMOVE(sp->gp->dq, sp, q);

	/* The screen is no longer real. */
	F_CLR(sp, SC_SCR_EX | SC_SCR_VI);

	rval = 0;
	if (v_screen_end(sp))			/* End vi. */
		rval = 1;
	if (ex_screen_end(sp))			/* End ex. */
		rval = 1;

	/* Free file names. */
	{ char **ap;
		if (!F_ISSET(sp, SC_ARGNOFREE) && sp->argv != NULL) {
			for (ap = sp->argv; *ap != NULL; ++ap)
				free(*ap);
			free(sp->argv);
		}
	}

	/* Free any text input. */
	if (!TAILQ_EMPTY(sp->tiq))
		text_lfree(sp->tiq);

	/* Free alternate file name. */
	if (sp->alt_name != NULL)
		free(sp->alt_name);

	/* Free up search information. */
	if (sp->re != NULL)
		free(sp->re);
	if (F_ISSET(sp, SC_RE_SEARCH))
		regfree(&sp->re_c);
	if (sp->subre != NULL)
		free(sp->subre);
	if (F_ISSET(sp, SC_RE_SUBST))
		regfree(&sp->subre_c);
	if (sp->repl != NULL)
		free(sp->repl);
	if (sp->newl != NULL)
		free(sp->newl);

	/* Free the iconv environment */
	conv_end(sp);

	/* Free all the options */
	opts_free(sp);

	/* Free the screen itself. */
	free(sp);

	return (rval);
}

/*
 * screen_next --
 *	Return the next screen in the queue.
 *
 * PUBLIC: SCR *screen_next(SCR *);
 */
SCR *
screen_next(SCR *sp)
{
	GS *gp;
	SCR *next;

	/* Try the display queue, without returning the current screen. */
	gp = sp->gp;
	TAILQ_FOREACH(next, gp->dq, q)
		if (next != sp)
			break;
	if (next != NULL)
		return (next);

	/* Try the hidden queue; if found, move screen to the display queue. */
	if (!TAILQ_EMPTY(gp->hq)) {
		next = TAILQ_FIRST(gp->hq);
		TAILQ_REMOVE(gp->hq, next, q);
		TAILQ_INSERT_HEAD(gp->dq, next, q);
		return (next);
	}
	return (NULL);
}
