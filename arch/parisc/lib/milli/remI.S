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

#ifdef L_remI
/* ROUTINE:	$$remI

   DESCRIPTION:
   .	$$remI returns the remainder of the division of two signed 32-bit
   .	integers.  The sign of the remainder is the same as the sign of
   .	the dividend.


   INPUT REGISTERS:
   .	arg0 == dividend
   .	arg1 == divisor
   .	mrp  == return pc
   .	sr0  == return space when called externally

   OUTPUT REGISTERS:
   .	arg0 = destroyed
   .	arg1 = destroyed
   .	ret1 = remainder

   OTHER REGISTERS AFFECTED:
   .	r1   = undefined

   SIDE EFFECTS:
   .	Causes a trap under the following conditions:  DIVIDE BY ZERO
   .	Changes memory at the following places:  NONE

   PERMISSIBLE CONTEXT:
   .	Unwindable
   .	Does not create a stack frame
   .	Is usable for internal or external microcode

   DISCUSSION:
   .	Calls other millicode routines via mrp:  NONE
   .	Calls other millicode routines:  NONE  */

RDEFINE(tmp,r1)
RDEFINE(retreg,ret1)

	SUBSPA_MILLI
	ATTR_MILLI
	.proc
	.callinfo millicode
	.entry
GSYM($$remI)
GSYM($$remoI)
	.export $$remI,MILLICODE
	.export $$remoI,MILLICODE
	ldo		-1(arg1),tmp		/*  is there at most one bit set ? */
	and,<>		arg1,tmp,r0		/*  if not, don't use power of 2 */
	addi,>		0,arg1,r0		/*  if denominator > 0, use power */
						/*  of 2 */
	b,n		LREF(neg_denom)
LSYM(pow2)
	comb,>,n	0,arg0,LREF(neg_num)	/*  is numerator < 0 ? */
	and		arg0,tmp,retreg		/*  get the result */
	MILLIRETN
LSYM(neg_num)
	subi		0,arg0,arg0		/*  negate numerator */
	and		arg0,tmp,retreg		/*  get the result */
	subi		0,retreg,retreg		/*  negate result */
	MILLIRETN
LSYM(neg_denom)
	addi,<		0,arg1,r0		/*  if arg1 >= 0, it's not power */
						/*  of 2 */
	b,n		LREF(regular_seq)
	sub		r0,arg1,tmp		/*  make denominator positive */
	comb,=,n	arg1,tmp,LREF(regular_seq) /*  test against 0x80000000 and 0 */
	ldo		-1(tmp),retreg		/*  is there at most one bit set ? */
	and,=		tmp,retreg,r0		/*  if not, go to regular_seq */
	b,n		LREF(regular_seq)
	comb,>,n	0,arg0,LREF(neg_num_2)	/*  if arg0 < 0, negate it  */
	and		arg0,retreg,retreg
	MILLIRETN
LSYM(neg_num_2)
	subi		0,arg0,tmp		/*  test against 0x80000000 */
	and		tmp,retreg,retreg
	subi		0,retreg,retreg
	MILLIRETN
LSYM(regular_seq)
	addit,=		0,arg1,0		/*  trap if div by zero */
	add,>=		0,arg0,retreg		/*  move dividend, if retreg < 0, */
	sub		0,retreg,retreg		/*    make it positive */
	sub		0,arg1, tmp		/*  clear carry,  */
						/*    negate the divisor */
	ds		0, tmp,0		/*  set V-bit to the comple- */
						/*    ment of the divisor sign */
	or		0,0, tmp		/*  clear  tmp */
	add		retreg,retreg,retreg	/*  shift msb bit into carry */
	ds		 tmp,arg1, tmp		/*  1st divide step, if no carry */
						/*    out, msb of quotient = 0 */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
LSYM(t1)
	ds		 tmp,arg1, tmp		/*  2nd divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  3rd divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  4th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  5th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  6th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  7th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  8th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  9th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  10th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  11th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  12th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  13th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  14th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  15th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  16th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  17th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  18th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  19th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  20th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  21st divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  22nd divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  23rd divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  24th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  25th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  26th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  27th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  28th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  29th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  30th divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  31st divide step */
	addc		retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds		 tmp,arg1, tmp		/*  32nd divide step, */
	addc		retreg,retreg,retreg	/*  shift last bit into retreg */
	movb,>=,n	 tmp,retreg,LREF(finish) /*  branch if pos.  tmp */
	add,<		arg1,0,0		/*  if arg1 > 0, add arg1 */
	add,tr		 tmp,arg1,retreg	/*    for correcting remainder tmp */
	sub		 tmp,arg1,retreg	/*  else add absolute value arg1 */
LSYM(finish)
	add,>=		arg0,0,0		/*  set sign of remainder */
	sub		0,retreg,retreg		/*    to sign of dividend */
	MILLIRET
	nop
	.exit
	.procend
#ifdef milliext
	.origin 0x00000200
#endif
	.end
#endif
