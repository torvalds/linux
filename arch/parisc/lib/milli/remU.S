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

#ifdef L_remU
/* ROUTINE:	$$remU
   .	Single precision divide for remainder with unsigned binary integers.
   .
   .	The remainder must be dividend-(dividend/divisor)*divisor.
   .	Divide by zero is trapped.

   INPUT REGISTERS:
   .	arg0 ==	dividend
   .	arg1 == divisor
   .	mrp  == return pc
   .	sr0  == return space when called externally

   OUTPUT REGISTERS:
   .	arg0 =	undefined
   .	arg1 =	undefined
   .	ret1 =	remainder

   OTHER REGISTERS AFFECTED:
   .	r1   =	undefined

   SIDE EFFECTS:
   .	Causes a trap under the following conditions:  DIVIDE BY ZERO
   .	Changes memory at the following places:  NONE

   PERMISSIBLE CONTEXT:
   .	Unwindable.
   .	Does not create a stack frame.
   .	Suitable for internal or external millicode.
   .	Assumes the special millicode register conventions.

   DISCUSSION:
   .	Calls other millicode routines using mrp: NONE
   .	Calls other millicode routines: NONE  */


RDEFINE(temp,r1)
RDEFINE(rmndr,ret1)	/*  r29 */
	SUBSPA_MILLI
	ATTR_MILLI
	.export $$remU,millicode
	.proc
	.callinfo	millicode
	.entry
GSYM($$remU)
	ldo	-1(arg1),temp		/*  is there at most one bit set ? */
	and,=	arg1,temp,r0		/*  if not, don't use power of 2 */
	b	LREF(regular_seq)
	addit,=	0,arg1,r0		/*  trap on div by zero */
	and	arg0,temp,rmndr		/*  get the result for power of 2 */
	MILLIRETN
LSYM(regular_seq)
	comib,>=,n  0,arg1,LREF(special_case)
	subi	0,arg1,rmndr		/*  clear carry, negate the divisor */
	ds	r0,rmndr,r0		/*  set V-bit to 1 */
	add	arg0,arg0,temp		/*  shift msb bit into carry */
	ds	r0,arg1,rmndr		/*  1st divide step, if no carry */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  2nd divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  3rd divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  4th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  5th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  6th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  7th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  8th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  9th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  10th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  11th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  12th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  13th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  14th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  15th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  16th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  17th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  18th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  19th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  20th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  21st divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  22nd divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  23rd divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  24th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  25th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  26th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  27th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  28th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  29th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  30th divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  31st divide step */
	addc	temp,temp,temp		/*  shift temp with/into carry */
	ds	rmndr,arg1,rmndr		/*  32nd divide step, */
	comiclr,<= 0,rmndr,r0
	  add	rmndr,arg1,rmndr	/*  correction */
	MILLIRETN
	nop

/* Putting >= on the last DS and deleting COMICLR does not work!  */
LSYM(special_case)
	sub,>>=	arg0,arg1,rmndr
	  copy	arg0,rmndr
	MILLIRETN
	nop
	.exit
	.procend
	.end
#endif
