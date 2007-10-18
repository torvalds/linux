/* 32 and 64-bit millicode, original author Hewlett-Packard
   adapted for gcc by Paul Bame <bame@debian.org>
   and Alan Modra <alan@linuxcare.com.au>.

   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.

   This file is part of GCC and is released under the terms of
   of the GNU General Public License as published by the Free Software
   Foundation; either version 2, or (at your option) any later version.
   See the file COPYING in the top-level GCC source directory for a copy
   of the license.  */

#include "milli.h"

#ifdef L_dyncall
	SUBSPA_MILLI
	ATTR_DATA
GSYM($$dyncall)
	.export $$dyncall,millicode
	.proc
	.callinfo	millicode
	.entry
	bb,>=,n %r22,30,LREF(1)		; branch if not plabel address
	depi	0,31,2,%r22		; clear the two least significant bits
	ldw	4(%r22),%r19		; load new LTP value
	ldw	0(%r22),%r22		; load address of target
LSYM(1)
	bv	%r0(%r22)		; branch to the real target
	stw	%r2,-24(%r30)		; save return address into frame marker
	.exit
	.procend
#endif
