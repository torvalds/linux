/*
 * Context switch support for Hexagon
 *
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
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

#include <asm/asm-offsets.h>

.text

/*
 * The register used as a fast-path thread information pointer
 * is determined as a kernel configuration option.  If it happens
 * to be a callee-save register, we're going to be saving and
 * restoring it twice here.
 *
 * This code anticipates a revised ABI where R20-23 are added
 * to the set of callee-save registers, but this should be
 * backward compatible to legacy tools.
 */


/*
 *	void switch_to(struct task_struct *prev,
 *		struct task_struct *next, struct task_struct *last);
 */
	.p2align 2
	.globl __switch_to
	.type	__switch_to, @function

/*
 * When we exit the wormhole, we need to store the previous task
 * in the new R0's pointer.  Technically it should be R2, but they should
 * be the same; seems like a legacy thing.  In short, don't butcher
 * R0, let it go back out unmolested.
 */

__switch_to:
	/*
	 * Push callee-saves onto "prev" stack.
	 * Here, we're sneaky because the LR and FP
	 * storage of the thread_stack structure
	 * is automagically allocated by allocframe,
	 * so we pass struct size less 8.
	 */
	allocframe(#(_SWITCH_STACK_SIZE - 8));
	memd(R29+#(_SWITCH_R2726))=R27:26;
	memd(R29+#(_SWITCH_R2524))=R25:24;
	memd(R29+#(_SWITCH_R2322))=R23:22;
	memd(R29+#(_SWITCH_R2120))=R21:20;
	memd(R29+#(_SWITCH_R1918))=R19:18;
	memd(R29+#(_SWITCH_R1716))=R17:16;
	/* Stash thread_info pointer in task_struct */
	memw(R0+#_TASK_THREAD_INFO) = THREADINFO_REG;
	memw(R0 +#(_TASK_STRUCT_THREAD + _THREAD_STRUCT_SWITCH_SP)) = R29;
	/* Switch to "next" stack and restore callee saves from there */
	R29 = memw(R1 + #(_TASK_STRUCT_THREAD + _THREAD_STRUCT_SWITCH_SP));
	{
	    R27:26 = memd(R29+#(_SWITCH_R2726));
	    R25:24 = memd(R29+#(_SWITCH_R2524));
	}
	{
	    R23:22 = memd(R29+#(_SWITCH_R2322));
	    R21:20 = memd(R29+#(_SWITCH_R2120));
	}
	{
	    R19:18 = memd(R29+#(_SWITCH_R1918));
	    R17:16 = memd(R29+#(_SWITCH_R1716));
	}
	{
	    /* THREADINFO_REG is currently one of the callee-saved regs
	     * above, and so be sure to re-load it last.
	     */
	    THREADINFO_REG = memw(R1 + #_TASK_THREAD_INFO);
	    R31:30 = memd(R29+#_SWITCH_FP);
	}
	{
	    R29 = add(R29,#_SWITCH_STACK_SIZE);
	    jumpr R31;
	}
	.size	__switch_to, .-__switch_to
