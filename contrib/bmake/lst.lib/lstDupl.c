/*	$NetBSD: lstDupl.c,v 1.16 2009/01/23 21:26:30 dsl Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef MAKE_NATIVE
static char rcsid[] = "$NetBSD: lstDupl.c,v 1.16 2009/01/23 21:26:30 dsl Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)lstDupl.c	8.1 (Berkeley) 6/6/93";
#else
__RCSID("$NetBSD: lstDupl.c,v 1.16 2009/01/23 21:26:30 dsl Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * listDupl.c --
 *	Duplicate a list. This includes duplicating the individual
 *	elements.
 */

#include    "lstInt.h"

/*-
 *-----------------------------------------------------------------------
 * Lst_Duplicate --
 *	Duplicate an entire list. If a function to copy a void *is
 *	given, the individual client elements will be duplicated as well.
 *
 * Input:
 *	l		the list to duplicate
 *	copyProc	A function to duplicate each void *
 *
 * Results:
 *	The new Lst structure or NULL if failure.
 *
 * Side Effects:
 *	A new list is created.
 *-----------------------------------------------------------------------
 */
Lst
Lst_Duplicate(Lst l, DuplicateProc *copyProc)
{
    Lst 	nl;
    ListNode  	ln;
    List 	list = l;

    if (!LstValid (l)) {
	return NULL;
    }

    nl = Lst_Init(list->isCirc);
    if (nl == NULL) {
	return NULL;
    }

    ln = list->firstPtr;
    while (ln != NULL) {
	if (copyProc != NULL) {
	    if (Lst_AtEnd(nl, copyProc(ln->datum)) == FAILURE) {
		return NULL;
	    }
	} else if (Lst_AtEnd(nl, ln->datum) == FAILURE) {
	    return NULL;
	}

	if (list->isCirc && ln == list->lastPtr) {
	    ln = NULL;
	} else {
	    ln = ln->nextPtr;
	}
    }

    return (nl);
}
