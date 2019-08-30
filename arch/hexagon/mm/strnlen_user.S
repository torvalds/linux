/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * User string length functions for kernel
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#define isrc	r0
#define max	r1	/*  Do not change!  */

#define end	r2
#define tmp1	r3

#define obo	r6	/*  off-by-one  */
#define start	r7
#define mod8	r8
#define dbuf    r15:14
#define dcmp	r13:12

/*
 * The vector mask version of this turned out *really* badly.
 * The hardware loop version also turned out *really* badly.
 * Seems straight pointer arithmetic basically wins here.
 */

#define fname __strnlen_user

	.text
	.global fname
	.type fname, @function
	.p2align 5  /*  why?  */
fname:
	{
		mod8 = and(isrc,#7);
		end = add(isrc,max);
		start = isrc;
	}
	{
		P0 = cmp.eq(mod8,#0);
		mod8 = and(end,#7);
		dcmp = #0;
		if (P0.new) jump:t dw_loop;	/*  fire up the oven  */
	}

alignment_loop:
fail_1:	{
		tmp1 = memb(start++#1);
	}
	{
		P0 = cmp.eq(tmp1,#0);
		if (P0.new) jump:nt exit_found;
		P1 = cmp.gtu(end,start);
		mod8 = and(start,#7);
	}
	{
		if (!P1) jump exit_error;  /*  hit the end  */
		P0 = cmp.eq(mod8,#0);
	}
	{
		if (!P0) jump alignment_loop;
	}



dw_loop:
fail_2:	{
		dbuf = memd(start);
		obo = add(start,#1);
	}
	{
		P0 = vcmpb.eq(dbuf,dcmp);
	}
	{
		tmp1 = P0;
		P0 = cmp.gtu(end,start);
	}
	{
		tmp1 = ct0(tmp1);
		mod8 = and(end,#7);
		if (!P0) jump end_check;
	}
	{
		P0 = cmp.eq(tmp1,#32);
		if (!P0.new) jump:nt exit_found;
		if (!P0.new) start = add(obo,tmp1);
	}
	{
		start = add(start,#8);
		jump dw_loop;
	}	/*  might be nice to combine these jumps...   */


end_check:
	{
		P0 = cmp.gt(tmp1,mod8);
		if (P0.new) jump:nt exit_error;	/*  neverfound!  */
		start = add(obo,tmp1);
	}

exit_found:
	{
		R0 = sub(start,isrc);
		jumpr R31;
	}

exit_error:
	{
		R0 = add(max,#1);
		jumpr R31;
	}

	/*  Uh, what does the "fixup" return here?  */
	.falign
fix_1:
	{
		R0 = #0;
		jumpr R31;
	}

	.size fname,.-fname


.section __ex_table,"a"
.long fail_1,fix_1
.long fail_2,fix_1
.previous
