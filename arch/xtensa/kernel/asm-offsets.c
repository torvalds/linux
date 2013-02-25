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
#include <asm/uaccess.h>

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
	DEFINE(PT_SIZE, sizeof(struct pt_regs));
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
	DEFINE(TASK_STRUCT_SIZE, sizeof (struct task_struct));

	/* struct thread_info (offset from start_struct) */
	DEFINE(THREAD_RA, offsetof (struct task_struct, thread.ra));
	DEFINE(THREAD_SP, offsetof (struct task_struct, thread.sp));
	DEFINE(THREAD_CPENABLE, offsetof (struct thread_info, cpenable));
#if XTENSA_HAVE_COPROCESSORS
	DEFINE(THREAD_XTREGS_CP0, offsetof (struct thread_info, xtregs_cp));
	DEFINE(THREAD_XTREGS_CP1, offsetof (struct thread_info, xtregs_cp));
	DEFINE(THREAD_XTREGS_CP2, offsetof (struct thread_info, xtregs_cp));
	DEFINE(THREAD_XTREGS_CP3, offsetof (struct thread_info, xtregs_cp));
	DEFINE(THREAD_XTREGS_CP4, offsetof (struct thread_info, xtregs_cp));
	DEFINE(THREAD_XTREGS_CP5, offsetof (struct thread_info, xtregs_cp));
	DEFINE(THREAD_XTREGS_CP6, offsetof (struct thread_info, xtregs_cp));
	DEFINE(THREAD_XTREGS_CP7, offsetof (struct thread_info, xtregs_cp));
#endif
	DEFINE(THREAD_XTREGS_USER, offsetof (struct thread_info, xtregs_user));
	DEFINE(XTREGS_USER_SIZE, sizeof(xtregs_user_t));
	DEFINE(THREAD_CURRENT_DS, offsetof (struct task_struct, \
	       thread.current_ds));

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

	return 0;
}
