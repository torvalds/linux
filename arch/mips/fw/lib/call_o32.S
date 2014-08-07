/*
 *	O32 interface for the 64 (or N32) ABI.
 *
 *	Copyright (C) 2002, 2014  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <asm/asm.h>
#include <asm/regdef.h>

/* O32 register size.  */
#define O32_SZREG	4
/* Maximum number of arguments supported.  Must be even!  */
#define O32_ARGC	32
/* Number of static registers we save.  */
#define O32_STATC	11
/* Argument area frame size.  */
#define O32_ARGSZ	(O32_SZREG * O32_ARGC)
/* Static register save area frame size.  */
#define O32_STATSZ	(SZREG * O32_STATC)
/* Stack pointer register save area frame size.  */
#define O32_SPSZ	SZREG
/* Combined area frame size.  */
#define O32_FRAMESZ	(O32_ARGSZ + O32_SPSZ + O32_STATSZ)
/* Switched stack frame size.  */
#define O32_NFRAMESZ	(O32_ARGSZ + O32_SPSZ)

		.text

/*
 * O32 function call dispatcher, for interfacing 32-bit ROM routines.
 *
 * The standard 64 (N32) calling sequence is supported, with a0 holding
 * a function pointer, a1 a pointer to the new stack to call the
 * function with or 0 if no stack switching is requested, a2-a7 -- the
 * function call's first six arguments, and the stack -- the remaining
 * arguments (up to O32_ARGC, including a2-a7).  Static registers, gp
 * and fp are preserved, v0 holds the result.  This code relies on the
 * called o32 function for sp and ra restoration and this dispatcher has
 * to be placed in a KSEGx (or KUSEG) address space.  Any pointers
 * passed have to point to addresses within one of these spaces as well.
 */
NESTED(call_o32, O32_FRAMESZ, ra)
		REG_SUBU	sp,O32_FRAMESZ

		REG_S		ra,O32_FRAMESZ-1*SZREG(sp)
		REG_S		fp,O32_FRAMESZ-2*SZREG(sp)
		REG_S		gp,O32_FRAMESZ-3*SZREG(sp)
		REG_S		s7,O32_FRAMESZ-4*SZREG(sp)
		REG_S		s6,O32_FRAMESZ-5*SZREG(sp)
		REG_S		s5,O32_FRAMESZ-6*SZREG(sp)
		REG_S		s4,O32_FRAMESZ-7*SZREG(sp)
		REG_S		s3,O32_FRAMESZ-8*SZREG(sp)
		REG_S		s2,O32_FRAMESZ-9*SZREG(sp)
		REG_S		s1,O32_FRAMESZ-10*SZREG(sp)
		REG_S		s0,O32_FRAMESZ-11*SZREG(sp)

		move		jp,a0

		move		fp,sp
		beqz		a1,0f
		REG_SUBU	fp,a1,O32_NFRAMESZ
0:
		REG_S		sp,O32_NFRAMESZ-1*SZREG(fp)

		sll		a0,a2,zero
		sll		a1,a3,zero
		sll		a2,a4,zero
		sll		a3,a5,zero
		sw		a6,4*O32_SZREG(fp)
		sw		a7,5*O32_SZREG(fp)

		PTR_LA		t0,O32_FRAMESZ(sp)
		PTR_LA		t1,6*O32_SZREG(fp)
		li		t2,O32_ARGC-6
1:
		lw		t3,(t0)
		REG_ADDU	t0,SZREG
		sw		t3,(t1)
		REG_SUBU	t2,1
		REG_ADDU	t1,O32_SZREG
		bnez		t2,1b

		move		sp,fp

		jalr		jp

		REG_L		sp,O32_NFRAMESZ-1*SZREG(sp)

		REG_L		s0,O32_FRAMESZ-11*SZREG(sp)
		REG_L		s1,O32_FRAMESZ-10*SZREG(sp)
		REG_L		s2,O32_FRAMESZ-9*SZREG(sp)
		REG_L		s3,O32_FRAMESZ-8*SZREG(sp)
		REG_L		s4,O32_FRAMESZ-7*SZREG(sp)
		REG_L		s5,O32_FRAMESZ-6*SZREG(sp)
		REG_L		s6,O32_FRAMESZ-5*SZREG(sp)
		REG_L		s7,O32_FRAMESZ-4*SZREG(sp)
		REG_L		gp,O32_FRAMESZ-3*SZREG(sp)
		REG_L		fp,O32_FRAMESZ-2*SZREG(sp)
		REG_L		ra,O32_FRAMESZ-1*SZREG(sp)

		REG_ADDU	sp,O32_FRAMESZ
		jr		ra
END(call_o32)
