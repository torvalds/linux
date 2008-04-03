#include <linux/elfcore.h>
#include <linux/sched.h>
#include <asm/fpu.h>

/*
 * Capture the user space registers if the task is not running (in user space)
 */
int dump_task_regs(struct task_struct *tsk, elf_gregset_t *regs)
{
	struct pt_regs ptregs;

	ptregs = *task_pt_regs(tsk);
	elf_core_copy_regs(regs, &ptregs);

	return 1;
}

int dump_task_fpu(struct task_struct *tsk, elf_fpregset_t *fpu)
{
	int fpvalid = 0;

#if defined(CONFIG_SH_FPU)
	fpvalid = !!tsk_used_math(tsk);
	if (fpvalid) {
		unlazy_fpu(tsk, task_pt_regs(tsk));
		memcpy(fpu, &tsk->thread.fpu.hard, sizeof(*fpu));
	}
#endif

	return fpvalid;
}

