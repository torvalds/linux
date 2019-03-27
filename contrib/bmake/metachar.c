/*	$NetBSD: metachar.c,v 1.5 2015/06/19 08:03:35 mlelstv Exp $	*/

/*-
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#if defined(MAKE_NATIVE) || defined(HAVE_NBTOOL_CONFIG_H)
#include <sys/cdefs.h>
#endif

#if defined(__RCSID) && !defined(lint)
__RCSID("$NetBSD: metachar.c,v 1.5 2015/06/19 08:03:35 mlelstv Exp $");
#endif

#include "metachar.h"
/*
 * The following array is used to make a fast determination of which
 * characters are interpreted specially by the shell.  If a command
 * contains any of these characters, it is executed by the shell, not
 * directly by us.
 *
 * perhaps move it to ctype?
 */

unsigned char _metachar[128] = {
//    nul   soh   stx   etx   eot   enq   ack   bel
	1,    0,    0,    0,    0,    0,    0,    0,
//     bs    ht    nl    vt    np    cr    so    si
	0,    0,    1,    0,	0,    0,    0,    0,
//    dle   dc1   dc2   dc3   dc4   nak   syn   etb
	0,    0,    0,    0,    0,    0,    0,    0,
//    can    em   sub   esc    fs    gs    rs    us
	0,    0,    0,    0,    0,    0,    0,    0,
//     sp     !     "     #     $     %     &     '
	0,    1,    1,    1,    1,    0,    1,    1,
//      (     )     *     +     ,     -     .     /
	1,    1,    1,    0,    0,    0,    0,    0,
//      0     1     2     3     4     5     6     7
	0,    0,    0,    0,    0,    0,    0,    0,
//      8     9     :     ;     <     =     >     ?
	0,    0,    0,    1,    1,    0,    1,    1,
//      @     A     B     C     D     E     F     G
	0,    0,    0,    0,    0,    0,    0,    0,
//      H     I     J     K     L     M     N     O
	0,    0,    0,    0,    0,    0,    0,    0,
//      P     Q     R     S     T     U     V     W
	0,    0,    0,    0,    0,    0,    0,    0,
//      X     Y     Z     [     \     ]     ^     _
	0,    0,    0,    1,    1,    1,    1,    0,
//      `     a     b     c     d     e     f     g
	1,    0,    0,    0,    0,    0,    0,    0,
//      h     i     j     k     l     m     n     o
	0,    0,    0,    0,    0,    0,    0,    0,
//      p     q     r     s     t     u     v     w
	0,    0,    0,    0,    0,    0,    0,    0,
//      x     y     z     {     |     }     ~   del
	0,    0,    0,    1,    1,    1,    1,    0,
};

