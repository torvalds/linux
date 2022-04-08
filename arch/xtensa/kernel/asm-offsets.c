/*
 * arch/xtensa/kernel/asm-offsets.c
 *
 * Generates definitions from c-type structures used by assembly sources.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 */

#include <asm/processor.h>
#include <asm/coprocessor.h>

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/thread_info.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/kbuild.h>

#include <asm/ptrace.h>
#include <asm/traps.h>
#include <linux/uaccess.h>

int main(void)
{
	/* struct pt_regs */
	DEFINE(PT_PC, offsetof (struct pt_regs, pc));
	DEFINE(PT_PS, offsetof (struct pt_regs, ps));
	DEFINE(PT_DEPC, offsetof (struct pt_regs, depc));
	DEFINE(PT_EXCCAUSE, offsetof (struct pt_regs, exccause));
	DEFINE(PT_EXCVADDR, offsetof (struct pt_regs, excvaddr));
	DEFINE(PT_DEBUGCAUSE, offsetof (struct pt_regs, debugcause));
	DEFINE(PT_WMASK, offsetof (struct pt_regs, wmask));
	DEFINE(PT_LBEG, offsetof (struct pt_regs, lbeg));
	DEFINE(PT_LEND, offsetof (struct pt_regs, lend));
	DEFINE(PT_LCOUNT, offsetof (struct pt_regs, lcount));
	DEFINE(PT_SAR, offsetof (struct pt_regs, sar));
	DEFINE(PT_ICOUNTLEVEL, offsetof (struct pt_regs, icountlevel));
	DEFINE(PT_SYSCALL, offsetof (struct pt_regs, syscall));
	DEFINE(PT_SCOMPARE1, offsetof(struct pt_regs, scompare1));
	DEFINE(PT_THREADPTR, offsetof(struct pt_regs, threadptr));
	DEFINE(PT_AREG, offsetof (struct pt_regs, areg[0]));
	DEFINE(PT_AREG0, offsetof (struct pt_regs, areg[0]));
	DEFINE(PT_AREG1, offsetof (struct pt_regs, areg[1]));
	DEFINE(PT_AREG2, offsetof (struct pt_regs, areg[2]));
	DEFINE(PT_AREG3, offsetof (struct pt_regs, areg[3]));
	DEFINE(PT_AREG4, offsetof (struct pt_regs, areg[4]));
	DEFINE(PT_AREG5, offsetof (struct pt_regs, areg[5]));
	DEFINE(PT_AREG6, offsetof (struct pt_regs, areg[6]));
	DEFINE(PT_AREG7, offsetof (struct pt_regs, areg[7]));
	DEFINE(PT_AREG8, offsetof (struct pt_regs, areg[8]));
	DEFINE(PT_AREG9, offsetof (struct pt_regs, areg[9]));
	DEFINE(PT_AREG10, offsetof (struct pt_regs, areg[10]));
	DEFINE(PT_AREG11, offsetof (struct pt_regs, areg[11]));
	DEFINE(PT_AREG12, offsetof (struct pt_regs, areg[12]));
	DEFINE(PT_AREG13, offsetof (struct pt_regs, areg[13]));
	DEFINE(PT_AREG14, offsetof (struct pt_regs, areg[14]));
	DEFINE(PT_AREG15, offsetof (struct pt_regs, areg[15]));
	DEFINE(PT_WINDOWBASE, offsetof (struct pt_regs, windowbase));
	DEFINE(PT_WINDOWSTART, offsetof(struct pt_regs, windowstart));
	DEFINE(PT_KERNEL_SIZE, offsetof(struct pt_regs, areg[16]));
	DEFINE(PT_AREG_END, offsetof (struct pt_regs, areg[XCHAL_NUM_AREGS]));
	DEFINE(PT_USER_SIZE, offsetof(struct pt_regs, areg[XCHAL_NUM_AREGS]));
	DEFINE(PT_XTREGS_OPT, offsetof(struct pt_regs, xtregs_opt));
	DEFINE(XTREGS_OPT_SIZE, sizeof(xtregs_opt_t));

	/* struct task_struct */
	DEFINE(TASK_PTRACE, offsetof (struct task_struct, ptrace));
	DEFINE(TASK_MM, offsetof (struct task_struct, mm));
	DEFINE(TASK_ACTIVE_MM, offsetof (struct task_struct, active_mm));
	DEFINE(TASK_PID, offsetof (struct task_struct, pid));
	DEFINE(TASK_THREAD, offsetof (struct task_struct, thread));
	DEFINE(TASK_THREAD_INFO, offsetof (struct task_struct, stack));
#ifdef CONFIG_STACKPROTECTOR
	DEFINE(TASK_STACK_CANARY, offsetof(struct task_struct, stack_canary));
#endif
	DEFINE(TASK_STRUCT_SIZE, sizeof (struct task_struct));

	/* offsets in thread_info struct */
	OFFSET(TI_TASK, thread_info, task);
	OFFSET(TI_FLAGS, thread_info, flags);
	OFFSET(TI_STSTUS, thread_info, status);
	OFFSET(TI_CPU, thread_info, cpu);
	OFFSET(TI_PRE_COUNT, thread_info, preempt_count);

	/* struct thread_info (offset from start_struct) */
	DEFINE(THREAD_RA, offsetof (struct task_struct, thread.ra));
	DEFINE(THREAD_SP, offsetof (struct task_struct, thread.sp));
	DEFINE(THREAD_CPENABLE, offsetof (struct thread_info, cpenable));
#if XCHAL_HAVE_EXCLUSIVE
	DEFINE(THREAD_ATOMCTL8, offsetof (struct thread_info, atomctl8));
#endif
#if XTENSA_HAVE_COPROCESSORS
	DEFINE(THREAD_XTREGS_CP0, offsetof(struct thread_info, xtregs_cp.cp0));
	DEFINE(THREAD_XTREGS_CP1, offsetof(struct thread_info, xtregs_cp.cp1));
	DEFINE(THREAD_XTREGS_CP2, offsetof(struct thread_info, xtregs_cp.cp2));
	DEFINE(THREAD_XTREGS_CP3, offsetof(struct thread_info, xtregs_cp.cp3));
	DEFINE(THREAD_XTREGS_CP4, offsetof(struct thread_info, xtregs_cp.cp4));
	DEFINE(THREAD_XTREGS_CP5, offsetof(struct thread_info, xtregs_cp.cp5));
	DEFINE(THREAD_XTREGS_CP6, offsetof(struct thread_info, xtregs_cp.cp6));
	DEFINE(THREAD_XTREGS_CP7, offsetof(struct thread_info, xtregs_cp.cp7));
#endif
	DEFINE(THREAD_XTREGS_USER, offsetof (struct thread_info, xtregs_user));
	DEFINE(XTREGS_USER_SIZE, sizeof(xtregs_user_t));

	/* struct mm_struct */
	DEFINE(MM_USERS, offsetof(struct mm_struct, mm_users));
	DEFINE(MM_PGD, offsetof (struct mm_struct, pgd));
	DEFINE(MM_CONTEXT, offsetof (struct mm_struct, context));

	/* struct page */
	DEFINE(PAGE_FLAGS, offsetof(struct page, flags));

	/* constants */
	DEFINE(_CLONE_VM, CLONE_VM);
	DEFINE(_CLONE_UNTRACED, CLONE_UNTRACED);
	DEFINE(PG_ARCH_1, PG_arch_1);

	/* struct debug_table */
	DEFINE(DT_DEBUG_EXCEPTION,
	       offsetof(struct debug_table, debug_exception));
	DEFINE(DT_DEBUG_SAVE, offsetof(struct debug_table, debug_save));
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	DEFINE(DT_DBREAKC_SAVE, offsetof(struct debug_table, dbreakc_save));
	DEFINE(DT_ICOUNT_SAVE, offsetof(struct debug_table, icount_save));
	DEFINE(DT_ICOUNT_LEVEL_SAVE,
	       offsetof(struct debug_table, icount_level_save));
#endif

	/* struct exc_table */
	DEFINE(EXC_TABLE_KSTK, offsetof(struct exc_table, kstk));
	DEFINE(EXC_TABLE_DOUBLE_SAVE, offsetof(struct exc_table, double_save));
	DEFINE(EXC_TABLE_FIXUP, offsetof(struct exc_table, fixup));
	DEFINE(EXC_TABLE_PARAM, offsetof(struct exc_table, fixup_param));
	DEFINE(EXC_TABLE_FAST_USER,
	       offsetof(struct exc_table, fast_user_handler));
	DEFINE(EXC_TABLE_FAST_KERNEL,
	       offsetof(struct exc_table, fast_kernel_handler));
	DEFINE(EXC_TABLE_DEFAULT, offsetof(struct exc_table, default_handler));

	return 0;
}
