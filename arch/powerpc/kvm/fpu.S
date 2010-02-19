/*
 *  FPU helper code to use FPU operations from inside the kernel
 *
 *    Copyright (C) 2010 Alexander Graf (agraf@suse.de)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <asm/reg.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>

/* Instructions operating on single parameters */

/*
 * Single operation with one input operand
 *
 * R3 = (double*)&fpscr
 * R4 = (short*)&result
 * R5 = (short*)&param1
 */
#define FPS_ONE_IN(name) 					\
_GLOBAL(fps_ ## name);							\
	lfd	0,0(r3);		/* load up fpscr value */	\
	MTFSF_L(0);							\
	lfs	0,0(r5);						\
									\
	name	0,0;							\
									\
	stfs	0,0(r4);						\
	mffs	0;							\
	stfd	0,0(r3);	/* save new fpscr value */	\
	blr

/*
 * Single operation with two input operands
 *
 * R3 = (double*)&fpscr
 * R4 = (short*)&result
 * R5 = (short*)&param1
 * R6 = (short*)&param2
 */
#define FPS_TWO_IN(name) 					\
_GLOBAL(fps_ ## name);							\
	lfd	0,0(r3);		/* load up fpscr value */	\
	MTFSF_L(0);							\
	lfs	0,0(r5);						\
	lfs	1,0(r6);						\
									\
	name	0,0,1;							\
									\
	stfs	0,0(r4);						\
	mffs	0;							\
	stfd	0,0(r3);		/* save new fpscr value */	\
	blr

/*
 * Single operation with three input operands
 *
 * R3 = (double*)&fpscr
 * R4 = (short*)&result
 * R5 = (short*)&param1
 * R6 = (short*)&param2
 * R7 = (short*)&param3
 */
#define FPS_THREE_IN(name) 					\
_GLOBAL(fps_ ## name);							\
	lfd	0,0(r3);		/* load up fpscr value */	\
	MTFSF_L(0);							\
	lfs	0,0(r5);						\
	lfs	1,0(r6);						\
	lfs	2,0(r7);						\
									\
	name	0,0,1,2;						\
									\
	stfs	0,0(r4);						\
	mffs	0;							\
	stfd	0,0(r3);		/* save new fpscr value */	\
	blr

FPS_ONE_IN(fres)
FPS_ONE_IN(frsqrte)
FPS_ONE_IN(fsqrts)
FPS_TWO_IN(fadds)
FPS_TWO_IN(fdivs)
FPS_TWO_IN(fmuls)
FPS_TWO_IN(fsubs)
FPS_THREE_IN(fmadds)
FPS_THREE_IN(fmsubs)
FPS_THREE_IN(fnmadds)
FPS_THREE_IN(fnmsubs)
FPS_THREE_IN(fsel)


/* Instructions operating on double parameters */

/*
 * Beginning of double instruction processing
 *
 * R3 = (double*)&fpscr
 * R4 = (u32*)&cr
 * R5 = (double*)&result
 * R6 = (double*)&param1
 * R7 = (double*)&param2 [load_two]
 * R8 = (double*)&param3 [load_three]
 * LR = instruction call function
 */
fpd_load_three:
	lfd	2,0(r8)			/* load param3 */
fpd_load_two:
	lfd	1,0(r7)			/* load param2 */
fpd_load_one:
	lfd	0,0(r6)			/* load param1 */
fpd_load_none:
	lfd	3,0(r3)			/* load up fpscr value */
	MTFSF_L(3)
	lwz	r6, 0(r4)		/* load cr */
	mtcr	r6
	blr

/*
 * End of double instruction processing
 *
 * R3 = (double*)&fpscr
 * R4 = (u32*)&cr
 * R5 = (double*)&result
 * LR = caller of instruction call function
 */
fpd_return:
	mfcr	r6
	stfd	0,0(r5)			/* save result */
	mffs	0
	stfd	0,0(r3)			/* save new fpscr value */
	stw	r6,0(r4)		/* save new cr value */
	blr

/*
 * Double operation with no input operand
 *
 * R3 = (double*)&fpscr
 * R4 = (u32*)&cr
 * R5 = (double*)&result
 */
#define FPD_NONE_IN(name) 						\
_GLOBAL(fpd_ ## name);							\
	mflr	r12;							\
	bl	fpd_load_none;						\
	mtlr	r12;							\
									\
	name.	0;			/* call instruction */		\
	b	fpd_return

/*
 * Double operation with one input operand
 *
 * R3 = (double*)&fpscr
 * R4 = (u32*)&cr
 * R5 = (double*)&result
 * R6 = (double*)&param1
 */
#define FPD_ONE_IN(name) 						\
_GLOBAL(fpd_ ## name);							\
	mflr	r12;							\
	bl	fpd_load_one;						\
	mtlr	r12;							\
									\
	name.	0,0;			/* call instruction */		\
	b	fpd_return

/*
 * Double operation with two input operands
 *
 * R3 = (double*)&fpscr
 * R4 = (u32*)&cr
 * R5 = (double*)&result
 * R6 = (double*)&param1
 * R7 = (double*)&param2
 * R8 = (double*)&param3
 */
#define FPD_TWO_IN(name) 						\
_GLOBAL(fpd_ ## name);							\
	mflr	r12;							\
	bl	fpd_load_two;						\
	mtlr	r12;							\
									\
	name.	0,0,1;			/* call instruction */		\
	b	fpd_return

/*
 * CR Double operation with two input operands
 *
 * R3 = (double*)&fpscr
 * R4 = (u32*)&cr
 * R5 = (double*)&param1
 * R6 = (double*)&param2
 * R7 = (double*)&param3
 */
#define FPD_TWO_IN_CR(name)						\
_GLOBAL(fpd_ ## name);							\
	lfd	1,0(r6);		/* load param2 */		\
	lfd	0,0(r5);		/* load param1 */		\
	lfd	3,0(r3);		/* load up fpscr value */	\
	MTFSF_L(3);							\
	lwz	r6, 0(r4);		/* load cr */			\
	mtcr	r6;							\
									\
	name	0,0,1;			/* call instruction */		\
	mfcr	r6;							\
	mffs	0;							\
	stfd	0,0(r3);		/* save new fpscr value */	\
	stw	r6,0(r4);		/* save new cr value */		\
	blr

/*
 * Double operation with three input operands
 *
 * R3 = (double*)&fpscr
 * R4 = (u32*)&cr
 * R5 = (double*)&result
 * R6 = (double*)&param1
 * R7 = (double*)&param2
 * R8 = (double*)&param3
 */
#define FPD_THREE_IN(name) 						\
_GLOBAL(fpd_ ## name);							\
	mflr	r12;							\
	bl	fpd_load_three;						\
	mtlr	r12;							\
									\
	name.	0,0,1,2;		/* call instruction */		\
	b	fpd_return

FPD_ONE_IN(fsqrts)
FPD_ONE_IN(frsqrtes)
FPD_ONE_IN(fres)
FPD_ONE_IN(frsp)
FPD_ONE_IN(fctiw)
FPD_ONE_IN(fctiwz)
FPD_ONE_IN(fsqrt)
FPD_ONE_IN(fre)
FPD_ONE_IN(frsqrte)
FPD_ONE_IN(fneg)
FPD_ONE_IN(fabs)
FPD_TWO_IN(fadds)
FPD_TWO_IN(fsubs)
FPD_TWO_IN(fdivs)
FPD_TWO_IN(fmuls)
FPD_TWO_IN_CR(fcmpu)
FPD_TWO_IN(fcpsgn)
FPD_TWO_IN(fdiv)
FPD_TWO_IN(fadd)
FPD_TWO_IN(fmul)
FPD_TWO_IN_CR(fcmpo)
FPD_TWO_IN(fsub)
FPD_THREE_IN(fmsubs)
FPD_THREE_IN(fmadds)
FPD_THREE_IN(fnmsubs)
FPD_THREE_IN(fnmadds)
FPD_THREE_IN(fsel)
FPD_THREE_IN(fmsub)
FPD_THREE_IN(fmadd)
FPD_THREE_IN(fnmsub)
FPD_THREE_IN(fnmadd)
