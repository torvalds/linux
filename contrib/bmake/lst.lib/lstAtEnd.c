/*	$NetBSD: lstAtEnd.c,v 1.13 2009/01/23 21:26:30 dsl Exp $	*/

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
static char rcsid[] = "$NetBSD: lstAtEnd.c,v 1.13 2009/01/23 21:26:30 dsl Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)lstAtEnd.c	8.1 (Berkeley) 6/6/93";
#else
__RCSID("$NetBSD: lstAtEnd.c,v 1.13 2009/01/23 21:26:30 dsl Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * LstAtEnd.c --
 *	Add a node at the end of the list
 */

#include	"lstInt.h"

/*-
 *-----------------------------------------------------------------------
 * Lst_AtEnd --
 *	Add a node to the end of the given list
 *
 * Input:
 *	l		List to which to add the datum
 *	d		Datum to add
 *
 * Results:
 *	SUCCESS if life is good.
 *
 * Side Effects:
 *	A new ListNode is created and added to the list.
 *
 *-----------------------------------------------------------------------
 */
ReturnStatus
Lst_AtEnd(Lst l, void *d)
{
    LstNode	end;

    end = Lst_Last(l);
    return (Lst_InsertAfter(l, end, d));
}
