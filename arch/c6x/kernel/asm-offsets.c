/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 */

#include <linux/sched.h>
#include <linux/thread_info.h>
#include <asm/procinfo.h>
#include <linux/kbuild.h>
#include <linux/unistd.h>

void foo(void)
{
	OFFSET(REGS_A16,	pt_regs, a16);
	OFFSET(REGS_A17,	pt_regs, a17);
	OFFSET(REGS_A18,	pt_regs, a18);
	OFFSET(REGS_A19,	pt_regs, a19);
	OFFSET(REGS_A20,	pt_regs, a20);
	OFFSET(REGS_A21,	pt_regs, a21);
	OFFSET(REGS_A22,	pt_regs, a22);
	OFFSET(REGS_A23,	pt_regs, a23);
	OFFSET(REGS_A24,	pt_regs, a24);
	OFFSET(REGS_A25,	pt_regs, a25);
	OFFSET(REGS_A26,	pt_regs, a26);
	OFFSET(REGS_A27,	pt_regs, a27);
	OFFSET(REGS_A28,	pt_regs, a28);
	OFFSET(REGS_A29,	pt_regs, a29);
	OFFSET(REGS_A30,	pt_regs, a30);
	OFFSET(REGS_A31,	pt_regs, a31);

	OFFSET(REGS_B16,	pt_regs, b16);
	OFFSET(REGS_B17,	pt_regs, b17);
	OFFSET(REGS_B18,	pt_regs, b18);
	OFFSET(REGS_B19,	pt_regs, b19);
	OFFSET(REGS_B20,	pt_regs, b20);
	OFFSET(REGS_B21,	pt_regs, b21);
	OFFSET(REGS_B22,	pt_regs, b22);
	OFFSET(REGS_B23,	pt_regs, b23);
	OFFSET(REGS_B24,	pt_regs, b24);
	OFFSET(REGS_B25,	pt_regs, b25);
	OFFSET(REGS_B26,	pt_regs, b26);
	OFFSET(REGS_B27,	pt_regs, b27);
	OFFSET(REGS_B28,	pt_regs, b28);
	OFFSET(REGS_B29,	pt_regs, b29);
	OFFSET(REGS_B30,	pt_regs, b30);
	OFFSET(REGS_B31,	pt_regs, b31);

	OFFSET(REGS_A0,		pt_regs, a0);
	OFFSET(REGS_A1,		pt_regs, a1);
	OFFSET(REGS_A2,		pt_regs, a2);
	OFFSET(REGS_A3,		pt_regs, a3);
	OFFSET(REGS_A4,		pt_regs, a4);
	OFFSET(REGS_A5,		pt_regs, a5);
	OFFSET(REGS_A6,		pt_regs, a6);
	OFFSET(REGS_A7,		pt_regs, a7);
	OFFSET(REGS_A8,		pt_regs, a8);
	OFFSET(REGS_A9,		pt_regs, a9);
	OFFSET(REGS_A10,	pt_regs, a10);
	OFFSET(REGS_A11,	pt_regs, a11);
	OFFSET(REGS_A12,	pt_regs, a12);
	OFFSET(REGS_A13,	pt_regs, a13);
	OFFSET(REGS_A14,	pt_regs, a14);
	OFFSET(REGS_A15,	pt_regs, a15);

	OFFSET(REGS_B0,		pt_regs, b0);
	OFFSET(REGS_B1,		pt_regs, b1);
	OFFSET(REGS_B2,		pt_regs, b2);
	OFFSET(REGS_B3,		pt_regs, b3);
	OFFSET(REGS_B4,		pt_regs, b4);
	OFFSET(REGS_B5,		pt_regs, b5);
	OFFSET(REGS_B6,		pt_regs, b6);
	OFFSET(REGS_B7,		pt_regs, b7);
	OFFSET(REGS_B8,		pt_regs, b8);
	OFFSET(REGS_B9,		pt_regs, b9);
	OFFSET(REGS_B10,	pt_regs, b10);
	OFFSET(REGS_B11,	pt_regs, b11);
	OFFSET(REGS_B12,	pt_regs, b12);
	OFFSET(REGS_B13,	pt_regs, b13);
	OFFSET(REGS_DP,		pt_regs, dp);
	OFFSET(REGS_SP,		pt_regs, sp);

	OFFSET(REGS_TSR,	pt_regs, tsr);
	OFFSET(REGS_ORIG_A4,	pt_regs, orig_a4);

	DEFINE(REGS__END,	sizeof(struct pt_regs));
	BLANK();

	OFFSET(THREAD_PC,	thread_struct, pc);
	OFFSET(THREAD_B15_14,	thread_struct, b15_14);
	OFFSET(THREAD_A15_14,	thread_struct, a15_14);
	OFFSET(THREAD_B13_12,	thread_struct, b13_12);
	OFFSET(THREAD_A13_12,	thread_struct, a13_12);
	OFFSET(THREAD_B11_10,	thread_struct, b11_10);
	OFFSET(THREAD_A11_10,	thread_struct, a11_10);
	OFFSET(THREAD_RICL_ICL,	thread_struct, ricl_icl);
	BLANK();

	OFFSET(TASK_STATE,	task_struct, state);
	BLANK();

	OFFSET(THREAD_INFO_FLAGS,	thread_info, flags);
	OFFSET(THREAD_INFO_PREEMPT_COUNT, thread_info, preempt_count);
	BLANK();

	/* These would be unneccessary if we ran asm files
	 * through the preprocessor.
	 */
	DEFINE(KTHREAD_SIZE, THREAD_SIZE);
	DEFINE(KTHREAD_SHIFT, THREAD_SHIFT);
	DEFINE(KTHREAD_START_SP, THREAD_START_SP);
	DEFINE(ENOSYS_, ENOSYS);
	DEFINE(NR_SYSCALLS_, __NR_syscalls);

	DEFINE(_TIF_SYSCALL_TRACE, (1<<TIF_SYSCALL_TRACE));
	DEFINE(_TIF_NOTIFY_RESUME, (1<<TIF_NOTIFY_RESUME));
	DEFINE(_TIF_SIGPENDING, (1<<TIF_SIGPENDING));
	DEFINE(_TIF_NEED_RESCHED, (1<<TIF_NEED_RESCHED));

	DEFINE(_TIF_ALLWORK_MASK, TIF_ALLWORK_MASK);
	DEFINE(_TIF_WORK_MASK, TIF_WORK_MASK);
}
