/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */

#include <linux/sched.h>
#include <linux/kbuild.h>
#include <asm/vdso.h>

int main(void)
{
	DEFINE(__THREAD_info, offsetof(struct task_struct, stack));
	DEFINE(__THREAD_ksp, offsetof(struct task_struct, thread.ksp));
	DEFINE(__THREAD_per, offsetof(struct task_struct, thread.per_info));
	DEFINE(__THREAD_mm_segment,
	       offsetof(struct task_struct, thread.mm_segment));
	BLANK();
	DEFINE(__TASK_pid, offsetof(struct task_struct, pid));
	BLANK();
	DEFINE(__PER_atmid, offsetof(per_struct, lowcore.words.perc_atmid));
	DEFINE(__PER_address, offsetof(per_struct, lowcore.words.address));
	DEFINE(__PER_access_id, offsetof(per_struct, lowcore.words.access_id));
	BLANK();
	DEFINE(__TI_task, offsetof(struct thread_info, task));
	DEFINE(__TI_domain, offsetof(struct thread_info, exec_domain));
	DEFINE(__TI_flags, offsetof(struct thread_info, flags));
	DEFINE(__TI_cpu, offsetof(struct thread_info, cpu));
	DEFINE(__TI_precount, offsetof(struct thread_info, preempt_count));
	BLANK();
	DEFINE(__PT_ARGS, offsetof(struct pt_regs, args));
	DEFINE(__PT_PSW, offsetof(struct pt_regs, psw));
	DEFINE(__PT_GPRS, offsetof(struct pt_regs, gprs));
	DEFINE(__PT_ORIG_GPR2, offsetof(struct pt_regs, orig_gpr2));
	DEFINE(__PT_ILC, offsetof(struct pt_regs, ilc));
	DEFINE(__PT_SVCNR, offsetof(struct pt_regs, svcnr));
	DEFINE(__PT_SIZE, sizeof(struct pt_regs));
	BLANK();
	DEFINE(__SF_BACKCHAIN, offsetof(struct stack_frame, back_chain));
	DEFINE(__SF_GPRS, offsetof(struct stack_frame, gprs));
	DEFINE(__SF_EMPTY, offsetof(struct stack_frame, empty1));
	BLANK();
	/* timeval/timezone offsets for use by vdso */
	DEFINE(__VDSO_UPD_COUNT, offsetof(struct vdso_data, tb_update_count));
	DEFINE(__VDSO_XTIME_STAMP, offsetof(struct vdso_data, xtime_tod_stamp));
	DEFINE(__VDSO_XTIME_SEC, offsetof(struct vdso_data, xtime_clock_sec));
	DEFINE(__VDSO_XTIME_NSEC, offsetof(struct vdso_data, xtime_clock_nsec));
	DEFINE(__VDSO_WTOM_SEC, offsetof(struct vdso_data, wtom_clock_sec));
	DEFINE(__VDSO_WTOM_NSEC, offsetof(struct vdso_data, wtom_clock_nsec));
	DEFINE(__VDSO_TIMEZONE, offsetof(struct vdso_data, tz_minuteswest));
	DEFINE(__VDSO_ECTG_OK, offsetof(struct vdso_data, ectg_available));
	DEFINE(__VDSO_ECTG_BASE,
	       offsetof(struct vdso_per_cpu_data, ectg_timer_base));
	DEFINE(__VDSO_ECTG_USER,
	       offsetof(struct vdso_per_cpu_data, ectg_user_time));
	/* constants used by the vdso */
	DEFINE(CLOCK_REALTIME, CLOCK_REALTIME);
	DEFINE(CLOCK_MONOTONIC, CLOCK_MONOTONIC);
	DEFINE(CLOCK_REALTIME_RES, MONOTONIC_RES_NSEC);

	return 0;
}
