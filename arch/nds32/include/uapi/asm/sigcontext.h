// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef _ASMNDS32_SIGCONTEXT_H
#define _ASMNDS32_SIGCONTEXT_H

/*
 * Signal context structure - contains all info to do with the state
 * before the signal handler was invoked.  Note: only add new entries
 * to the end of the structure.
 */

struct zol_struct {
	unsigned long nds32_lc;	/* $LC */
	unsigned long nds32_le;	/* $LE */
	unsigned long nds32_lb;	/* $LB */
};

struct sigcontext {
	unsigned long trap_no;
	unsigned long error_code;
	unsigned long oldmask;
	unsigned long nds32_r0;
	unsigned long nds32_r1;
	unsigned long nds32_r2;
	unsigned long nds32_r3;
	unsigned long nds32_r4;
	unsigned long nds32_r5;
	unsigned long nds32_r6;
	unsigned long nds32_r7;
	unsigned long nds32_r8;
	unsigned long nds32_r9;
	unsigned long nds32_r10;
	unsigned long nds32_r11;
	unsigned long nds32_r12;
	unsigned long nds32_r13;
	unsigned long nds32_r14;
	unsigned long nds32_r15;
	unsigned long nds32_r16;
	unsigned long nds32_r17;
	unsigned long nds32_r18;
	unsigned long nds32_r19;
	unsigned long nds32_r20;
	unsigned long nds32_r21;
	unsigned long nds32_r22;
	unsigned long nds32_r23;
	unsigned long nds32_r24;
	unsigned long nds32_r25;
	unsigned long nds32_fp;	/* $r28 */
	unsigned long nds32_gp;	/* $r29 */
	unsigned long nds32_lp;	/* $r30 */
	unsigned long nds32_sp;	/* $r31 */
	unsigned long nds32_ipc;
	unsigned long fault_address;
	unsigned long used_math_flag;
	/* FPU Registers */
	struct zol_struct zol;
};

#endif
