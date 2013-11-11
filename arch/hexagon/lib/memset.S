/*
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */


/* HEXAGON assembly optimized memset */
/* Replaces the standard library function memset */


        .macro HEXAGON_OPT_FUNC_BEGIN name
	.text
	.p2align 4
	.globl \name
	.type  \name, @function
\name:
	.endm

	.macro HEXAGON_OPT_FUNC_FINISH name
	.size  \name, . - \name
	.endm

/* FUNCTION: memset (v2 version) */
#if __HEXAGON_ARCH__ < 3
HEXAGON_OPT_FUNC_BEGIN memset
	{
		r6 = #8
		r7 = extractu(r0, #3 , #0)
		p0 = cmp.eq(r2, #0)
		p1 = cmp.gtu(r2, #7)
	}
	{
		r4 = vsplatb(r1)
		r8 = r0           /* leave r0 intact for return val  */
		r9 = sub(r6, r7)  /* bytes until double alignment  */
		if p0 jumpr r31   /* count == 0, so return  */
	}
	{
		r3 = #0
		r7 = #0
		p0 = tstbit(r9, #0)
		if p1 jump 2f /* skip byte loop */
	}

/* less than 8 bytes to set, so just set a byte at a time and return  */

		loop0(1f, r2) /* byte loop */
	.falign
1: /* byte loop */
	{
		memb(r8++#1) = r4
	}:endloop0
		jumpr r31
	.falign
2: /* skip byte loop */
	{
		r6 = #1
		p0 = tstbit(r9, #1)
		p1 = cmp.eq(r2, #1)
		if !p0 jump 3f /* skip initial byte store */
	}
	{
		memb(r8++#1) = r4
		r3:2 = sub(r3:2, r7:6)
		if p1 jumpr r31
	}
	.falign
3: /* skip initial byte store */
	{
		r6 = #2
		p0 = tstbit(r9, #2)
		p1 = cmp.eq(r2, #2)
		if !p0 jump 4f /* skip initial half store */
	}
	{
		memh(r8++#2) = r4
		r3:2 = sub(r3:2, r7:6)
		if p1 jumpr r31
	}
	.falign
4: /* skip initial half store */
	{
		r6 = #4
		p0 = cmp.gtu(r2, #7)
		p1 = cmp.eq(r2, #4)
		if !p0 jump 5f /* skip initial word store */
	}
	{
		memw(r8++#4) = r4
		r3:2 = sub(r3:2, r7:6)
		p0 = cmp.gtu(r2, #11)
		if p1 jumpr r31
	}
	.falign
5: /* skip initial word store */
	{
		r10 = lsr(r2, #3)
		p1 = cmp.eq(r3, #1)
		if !p0 jump 7f /* skip double loop */
	}
	{
		r5 = r4
		r6 = #8
		loop0(6f, r10) /* double loop */
	}

/* set bytes a double word at a time  */

	.falign
6: /* double loop */
	{
		memd(r8++#8) = r5:4
		r3:2 = sub(r3:2, r7:6)
		p1 = cmp.eq(r2, #8)
	}:endloop0
	.falign
7: /* skip double loop */
	{
		p0 = tstbit(r2, #2)
		if p1 jumpr r31
	}
	{
		r6 = #4
		p0 = tstbit(r2, #1)
		p1 = cmp.eq(r2, #4)
		if !p0 jump 8f /* skip final word store */
	}
	{
		memw(r8++#4) = r4
		r3:2 = sub(r3:2, r7:6)
		if p1 jumpr r31
	}
	.falign
8: /* skip final word store */
	{
		p1 = cmp.eq(r2, #2)
		if !p0 jump 9f /* skip final half store */
	}
	{
		memh(r8++#2) = r4
		if p1 jumpr r31
	}
	.falign
9: /* skip final half store */
	{
		memb(r8++#1) = r4
		jumpr r31
	}
HEXAGON_OPT_FUNC_FINISH memset
#endif


/*  FUNCTION: memset (v3 and higher version)  */
#if __HEXAGON_ARCH__ >= 3
HEXAGON_OPT_FUNC_BEGIN memset
	{
		r7=vsplatb(r1)
		r6 = r0
		if (r2==#0) jump:nt .L1
	}
	{
		r5:4=combine(r7,r7)
		p0 = cmp.gtu(r2,#8)
		if (p0.new) jump:nt .L3
	}
	{
		r3 = r0
		loop0(.L47,r2)
	}
	.falign
.L47:
	{
		memb(r3++#1) = r1
	}:endloop0 /* start=.L47 */
		jumpr r31
.L3:
	{
		p0 = tstbit(r0,#0)
		if (!p0.new) jump:nt .L8
		p1 = cmp.eq(r2, #1)
	}
	{
		r6 = add(r0, #1)
		r2 = add(r2,#-1)
		memb(r0) = r1
		if (p1) jump .L1
	}
.L8:
	{
		p0 = tstbit(r6,#1)
		if (!p0.new) jump:nt .L10
	}
	{
		r2 = add(r2,#-2)
		memh(r6++#2) = r7
		p0 = cmp.eq(r2, #2)
		if (p0.new) jump:nt .L1
	}
.L10:
	{
		p0 = tstbit(r6,#2)
		if (!p0.new) jump:nt .L12
	}
	{
		r2 = add(r2,#-4)
		memw(r6++#4) = r7
		p0 = cmp.eq(r2, #4)
		if (p0.new) jump:nt .L1
	}
.L12:
	{
		p0 = cmp.gtu(r2,#127)
		if (!p0.new) jump:nt .L14
	}
		r3 = and(r6,#31)
		if (r3==#0) jump:nt .L17
	{
		memd(r6++#8) = r5:4
		r2 = add(r2,#-8)
	}
		r3 = and(r6,#31)
		if (r3==#0) jump:nt .L17
	{
		memd(r6++#8) = r5:4
		r2 = add(r2,#-8)
	}
		r3 = and(r6,#31)
		if (r3==#0) jump:nt .L17
	{
		memd(r6++#8) = r5:4
		r2 = add(r2,#-8)
	}
.L17:
	{
		r3 = lsr(r2,#5)
		if (r1!=#0) jump:nt .L18
	}
	{
		r8 = r3
		r3 = r6
		loop0(.L46,r3)
	}
	.falign
.L46:
	{
		dczeroa(r6)
		r6 = add(r6,#32)
		r2 = add(r2,#-32)
	}:endloop0 /* start=.L46 */
.L14:
	{
		p0 = cmp.gtu(r2,#7)
		if (!p0.new) jump:nt .L28
		r8 = lsr(r2,#3)
	}
		loop0(.L44,r8)
	.falign
.L44:
	{
		memd(r6++#8) = r5:4
		r2 = add(r2,#-8)
	}:endloop0 /* start=.L44 */
.L28:
	{
		p0 = tstbit(r2,#2)
		if (!p0.new) jump:nt .L33
	}
	{
		r2 = add(r2,#-4)
		memw(r6++#4) = r7
	}
.L33:
	{
		p0 = tstbit(r2,#1)
		if (!p0.new) jump:nt .L35
	}
	{
		r2 = add(r2,#-2)
		memh(r6++#2) = r7
	}
.L35:
		p0 = cmp.eq(r2,#1)
		if (p0) memb(r6) = r1
.L1:
		jumpr r31
.L18:
		loop0(.L45,r3)
	.falign
.L45:
		dczeroa(r6)
	{
		memd(r6++#8) = r5:4
		r2 = add(r2,#-32)
	}
		memd(r6++#8) = r5:4
		memd(r6++#8) = r5:4
	{
		memd(r6++#8) = r5:4
	}:endloop0 /* start=.L45  */
		jump .L14
HEXAGON_OPT_FUNC_FINISH memset
#endif
