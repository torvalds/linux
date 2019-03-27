/*	$NetBSD: lstRemove.c,v 1.16 2014/09/07 20:55:34 joerg Exp $	*/

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
static char rcsid[] = "$NetBSD: lstRemove.c,v 1.16 2014/09/07 20:55:34 joerg Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)lstRemove.c	8.1 (Berkeley) 6/6/93";
#else
__RCSID("$NetBSD: lstRemove.c,v 1.16 2014/09/07 20:55:34 joerg Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * LstRemove.c --
 *	Remove an element from a list
 */

#include	"lstInt.h"

/*-
 *-----------------------------------------------------------------------
 * Lst_Remove --
 *	Remove the given node from the given list.
 *
 * Results:
 *	SUCCESS or FAILURE.
 *
 * Side Effects:
 *	The list's firstPtr will be set to NULL if ln is the last
 *	node on the list. firsPtr and lastPtr will be altered if ln is
 *	either the first or last node, respectively, on the list.
 *
 *-----------------------------------------------------------------------
 */
ReturnStatus
Lst_Remove(Lst l, LstNode ln)
{
    List 	list = l;
    ListNode	lNode = ln;

    if (!LstValid (l) ||
	!LstNodeValid (ln, l)) {
	    return (FAILURE);
    }

    /*
     * unlink it from the list
     */
    if (lNode->nextPtr != NULL) {
	lNode->nextPtr->prevPtr = lNode->prevPtr;
    }
    if (lNode->prevPtr != NULL) {
	lNode->prevPtr->nextPtr = lNode->nextPtr;
    }

    /*
     * if either the firstPtr or lastPtr of the list point to this node,
     * adjust them accordingly
     */
    if (list->firstPtr == lNode) {
	list->firstPtr = lNode->nextPtr;
    }
    if (list->lastPtr == lNode) {
	list->lastPtr = lNode->prevPtr;
    }

    /*
     * Sequential access stuff. If the node we're removing is the current
     * node in the list, reset the current node to the previous one. If the
     * previous one was non-existent (prevPtr == NULL), we set the
     * end to be Unknown, since it is.
     */
    if (list->isOpen && (list->curPtr == lNode)) {
	list->curPtr = list->prevPtr;
	if (list->curPtr == NULL) {
	    list->atEnd = Unknown;
	}
    }

    /*
     * the only way firstPtr can still point to ln is if ln is the last
     * node on the list (the list is circular, so lNode->nextptr == lNode in
     * this case). The list is, therefore, empty and is marked as such
     */
    if (list->firstPtr == lNode) {
	list->firstPtr = NULL;
    }

    /*
     * note that the datum is unmolested. The caller must free it as
     * necessary and as expected.
     */
    if (lNode->useCount == 0) {
	free(ln);
    } else {
	lNode->flags |= LN_DELETED;
    }

    return (SUCCESS);
}

