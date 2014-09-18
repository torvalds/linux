/*
 * Copyright (C) 1996 David S. Miller
 * Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Kevin Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.
 *
 * Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
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

#include <linux/compat.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/kbuild.h>
#include <asm/ptrace.h>
#include <asm/processor.h>

/*  This file is used to produce asm/linkerscript constants from header
    files typically used in c.  Specifically, it generates asm-offsets.h  */

int main(void)
{
	COMMENT("This is a comment.");
	/*  might get these from somewhere else.  */
	DEFINE(_PAGE_SIZE, PAGE_SIZE);
	DEFINE(_PAGE_SHIFT, PAGE_SHIFT);
	BLANK();

	COMMENT("Hexagon pt_regs definitions");
	OFFSET(_PT_SYSCALL_NR, pt_regs, syscall_nr);
	OFFSET(_PT_GPUGP, pt_regs, gpugp);
	OFFSET(_PT_CS1CS0, pt_regs, cs1cs0);
	OFFSET(_PT_R3130, pt_regs, r3130);
	OFFSET(_PT_R2928, pt_regs, r2928);
	OFFSET(_PT_R2726, pt_regs, r2726);
	OFFSET(_PT_R2524, pt_regs, r2524);
	OFFSET(_PT_R2322, pt_regs, r2322);
	OFFSET(_PT_R2120, pt_regs, r2120);
	OFFSET(_PT_R1918, pt_regs, r1918);
	OFFSET(_PT_R1716, pt_regs, r1716);
	OFFSET(_PT_R1514, pt_regs, r1514);
	OFFSET(_PT_R1312, pt_regs, r1312);
	OFFSET(_PT_R1110, pt_regs, r1110);
	OFFSET(_PT_R0908, pt_regs, r0908);
	OFFSET(_PT_R0706, pt_regs, r0706);
	OFFSET(_PT_R0504, pt_regs, r0504);
	OFFSET(_PT_R0302, pt_regs, r0302);
	OFFSET(_PT_R0100, pt_regs, r0100);
	OFFSET(_PT_LC0SA0, pt_regs, lc0sa0);
	OFFSET(_PT_LC1SA1, pt_regs, lc1sa1);
	OFFSET(_PT_M1M0, pt_regs, m1m0);
	OFFSET(_PT_PREDSUSR, pt_regs, predsusr);
	OFFSET(_PT_EVREC, pt_regs, hvmer);
	OFFSET(_PT_ER_VMEL, pt_regs, hvmer.vmel);
	OFFSET(_PT_ER_VMEST, pt_regs, hvmer.vmest);
	OFFSET(_PT_ER_VMPSP, pt_regs, hvmer.vmpsp);
	OFFSET(_PT_ER_VMBADVA, pt_regs, hvmer.vmbadva);
	DEFINE(_PT_REGS_SIZE, sizeof(struct pt_regs));
	BLANK();

	COMMENT("Hexagon thread_info definitions");
	OFFSET(_THREAD_INFO_FLAGS, thread_info, flags);
	OFFSET(_THREAD_INFO_PT_REGS, thread_info, regs);
	OFFSET(_THREAD_INFO_SP, thread_info, sp);
	DEFINE(_THREAD_SIZE, THREAD_SIZE);
	BLANK();

	COMMENT("Hexagon hexagon_switch_stack definitions");
	OFFSET(_SWITCH_R1716, hexagon_switch_stack, r1716);
	OFFSET(_SWITCH_R1918, hexagon_switch_stack, r1918);
	OFFSET(_SWITCH_R2120, hexagon_switch_stack, r2120);
	OFFSET(_SWITCH_R2322, hexagon_switch_stack, r2322);

	OFFSET(_SWITCH_R2524, hexagon_switch_stack, r2524);
	OFFSET(_SWITCH_R2726, hexagon_switch_stack, r2726);
	OFFSET(_SWITCH_FP, hexagon_switch_stack, fp);
	OFFSET(_SWITCH_LR, hexagon_switch_stack, lr);
	DEFINE(_SWITCH_STACK_SIZE, sizeof(struct hexagon_switch_stack));
	BLANK();

	COMMENT("Hexagon task_struct definitions");
	OFFSET(_TASK_THREAD_INFO, task_struct, stack);
	OFFSET(_TASK_STRUCT_THREAD, task_struct, thread);

	COMMENT("Hexagon thread_struct definitions");
	OFFSET(_THREAD_STRUCT_SWITCH_SP, thread_struct, switch_sp);

	return 0;
}
