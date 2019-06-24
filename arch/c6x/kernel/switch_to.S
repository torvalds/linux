/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter (msalter@redhat.com)
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>

#define SP	B15

	/*
	 * void __switch_to(struct thread_info *prev,
	 *      	    struct thread_info *next,
	 *		    struct task_struct *tsk) ;
	 */
ENTRY(__switch_to)
	LDDW	.D2T2	*+B4(THREAD_B15_14),B7:B6
 ||	MV	.L2X	A4,B5	; prev
 ||	MV	.L1X	B4,A5	; next
 ||	MVC	.S2	RILC,B1

	STW	.D2T2	B3,*+B5(THREAD_PC)
 ||	STDW	.D1T1	A13:A12,*+A4(THREAD_A13_12)
 ||	MVC	.S2	ILC,B0

	LDW	.D2T2	*+B4(THREAD_PC),B3
 ||	LDDW	.D1T1	*+A5(THREAD_A13_12),A13:A12

	STDW	.D1T1	A11:A10,*+A4(THREAD_A11_10)
 ||	STDW	.D2T2	B1:B0,*+B5(THREAD_RICL_ICL)
#ifndef __DSBT__
 ||	MVKL	.S2	current_ksp,B1
#endif

	STDW	.D2T2	B15:B14,*+B5(THREAD_B15_14)
 ||	STDW	.D1T1	A15:A14,*+A4(THREAD_A15_14)
#ifndef __DSBT__
 ||	MVKH	.S2	current_ksp,B1
#endif

	;; Switch to next SP
	MV	.S2	B7,SP
#ifdef __DSBT__
 ||	STW	.D2T2	B7,*+B14(current_ksp)
#else
 ||	STW	.D2T2	B7,*B1
 ||	MV	.L2	B6,B14
#endif
 ||	LDDW	.D1T1	*+A5(THREAD_RICL_ICL),A1:A0

	STDW	.D2T2	B11:B10,*+B5(THREAD_B11_10)
 ||	LDDW	.D1T1	*+A5(THREAD_A15_14),A15:A14

	STDW	.D2T2	B13:B12,*+B5(THREAD_B13_12)
 ||	LDDW	.D1T1	*+A5(THREAD_A11_10),A11:A10

	B	.S2	B3		; return in next E1
 ||	LDDW	.D2T2	*+B4(THREAD_B13_12),B13:B12

	LDDW	.D2T2	*+B4(THREAD_B11_10),B11:B10
	NOP

	MV	.L2X	A0,B0
 ||	MV	.S1	A6,A4

	MVC	.S2	B0,ILC
 ||	MV	.L2X	A1,B1

	MVC	.S2	B1,RILC
ENDPROC(__switch_to)
