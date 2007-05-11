/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */

#include <linux/sched.h>

/* Use marker if you need to separate the values later */

#define DEFINE(sym, val, marker) \
	asm volatile("\n->" #sym " %0 " #val " " #marker : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main(void)
{
	DEFINE(__THREAD_info, offsetof(struct task_struct, stack),);
	DEFINE(__THREAD_ksp, offsetof(struct task_struct, thread.ksp),);
	DEFINE(__THREAD_per, offsetof(struct task_struct, thread.per_info),);
	DEFINE(__THREAD_mm_segment,
	       offsetof(struct task_struct, thread.mm_segment),);
	BLANK();
	DEFINE(__TASK_pid, offsetof(struct task_struct, pid),);
	BLANK();
	DEFINE(__PER_atmid, offsetof(per_struct, lowcore.words.perc_atmid),);
	DEFINE(__PER_address, offsetof(per_struct, lowcore.words.address),);
	DEFINE(__PER_access_id, offsetof(per_struct, lowcore.words.access_id),);
	BLANK();
	DEFINE(__TI_task, offsetof(struct thread_info, task),);
	DEFINE(__TI_domain, offsetof(struct thread_info, exec_domain),);
	DEFINE(__TI_flags, offsetof(struct thread_info, flags),);
	DEFINE(__TI_cpu, offsetof(struct thread_info, cpu),);
	DEFINE(__TI_precount, offsetof(struct thread_info, preempt_count),);
	BLANK();
	DEFINE(__PT_ARGS, offsetof(struct pt_regs, args),);
	DEFINE(__PT_PSW, offsetof(struct pt_regs, psw),);
	DEFINE(__PT_GPRS, offsetof(struct pt_regs, gprs),);
	DEFINE(__PT_ORIG_GPR2, offsetof(struct pt_regs, orig_gpr2),);
	DEFINE(__PT_ILC, offsetof(struct pt_regs, ilc),);
	DEFINE(__PT_TRAP, offsetof(struct pt_regs, trap),);
	DEFINE(__PT_SIZE, sizeof(struct pt_regs),);
	BLANK();
	DEFINE(__SF_BACKCHAIN, offsetof(struct stack_frame, back_chain),);
	DEFINE(__SF_GPRS, offsetof(struct stack_frame, gprs),);
	DEFINE(__SF_EMPTY, offsetof(struct stack_frame, empty1),);
	return 0;
}
