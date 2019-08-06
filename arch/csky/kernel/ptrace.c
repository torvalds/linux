// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/uaccess.h>
#include <linux/user.h>

#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/asm-offsets.h>

#include <abi/regdef.h>

/* sets the trace bits. */
#define TRACE_MODE_SI      (1 << 14)
#define TRACE_MODE_RUN     0
#define TRACE_MODE_MASK    ~(0x3 << 14)

/*
 * Make sure the single step bit is not set.
 */
static void singlestep_disable(struct task_struct *tsk)
{
	struct pt_regs *regs;

	regs = task_pt_regs(tsk);
	regs->sr = (regs->sr & TRACE_MODE_MASK) | TRACE_MODE_RUN;
}

static void singlestep_enable(struct task_struct *tsk)
{
	struct pt_regs *regs;

	regs = task_pt_regs(tsk);
	regs->sr = (regs->sr & TRACE_MODE_MASK) | TRACE_MODE_SI;
}

/*
 * Make sure the single step bit is set.
 */
void user_enable_single_step(struct task_struct *child)
{
	singlestep_enable(child);
}

void user_disable_single_step(struct task_struct *child)
{
	singlestep_disable(child);
}

enum csky_regset {
	REGSET_GPR,
	REGSET_FPR,
};

static int gpr_get(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	struct pt_regs *regs;

	regs = task_pt_regs(target);

	/* Abiv1 regs->tls is fake and we need sync here. */
	regs->tls = task_thread_info(target)->tp_value;

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, regs, 0, -1);
}

static int gpr_set(struct task_struct *target,
		    const struct user_regset *regset,
		    unsigned int pos, unsigned int count,
		    const void *kbuf, const void __user *ubuf)
{
	int ret;
	struct pt_regs regs;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &regs, 0, -1);
	if (ret)
		return ret;

	regs.sr = task_pt_regs(target)->sr;
#ifdef CONFIG_CPU_HAS_HILO
	regs.dcsr = task_pt_regs(target)->dcsr;
#endif
	task_thread_info(target)->tp_value = regs.tls;

	*task_pt_regs(target) = regs;

	return 0;
}

static int fpr_get(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	struct user_fp *regs = (struct user_fp *)&target->thread.user_fp;

#if defined(CONFIG_CPU_HAS_FPUV2) && !defined(CONFIG_CPU_HAS_VDSP)
	int i;
	struct user_fp tmp = *regs;

	for (i = 0; i < 16; i++) {
		tmp.vr[i*4] = regs->vr[i*2];
		tmp.vr[i*4 + 1] = regs->vr[i*2 + 1];
	}

	for (i = 0; i < 32; i++)
		tmp.vr[64 + i] = regs->vr[32 + i];

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, &tmp, 0, -1);
#else
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, regs, 0, -1);
#endif
}

static int fpr_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret;
	struct user_fp *regs = (struct user_fp *)&target->thread.user_fp;

#if defined(CONFIG_CPU_HAS_FPUV2) && !defined(CONFIG_CPU_HAS_VDSP)
	int i;
	struct user_fp tmp;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &tmp, 0, -1);

	*regs = tmp;

	for (i = 0; i < 16; i++) {
		regs->vr[i*2] = tmp.vr[i*4];
		regs->vr[i*2 + 1] = tmp.vr[i*4 + 1];
	}

	for (i = 0; i < 32; i++)
		regs->vr[32 + i] = tmp.vr[64 + i];
#else
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, regs, 0, -1);
#endif

	return ret;
}

static const struct user_regset csky_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = sizeof(struct pt_regs) / sizeof(u32),
		.size = sizeof(u32),
		.align = sizeof(u32),
		.get = &gpr_get,
		.set = &gpr_set,
	},
	[REGSET_FPR] = {
		.core_note_type = NT_PRFPREG,
		.n = sizeof(struct user_fp) / sizeof(u32),
		.size = sizeof(u32),
		.align = sizeof(u32),
		.get = &fpr_get,
		.set = &fpr_set,
	},
};

static const struct user_regset_view user_csky_view = {
	.name = "csky",
	.e_machine = ELF_ARCH,
	.regsets = csky_regsets,
	.n = ARRAY_SIZE(csky_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_csky_view;
}

void ptrace_disable(struct task_struct *child)
{
	singlestep_disable(child);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	long ret = -EIO;

	switch (request) {
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

/*
 * If process's system calls is traces, do some corresponding handles in this
 * function before entering system call function and after exiting system call
 * function.
 */
asmlinkage void syscall_trace(int why, struct pt_regs *regs)
{
	long saved_why;
	/*
	 * Save saved_why, why is used to denote syscall entry/exit;
	 * why = 0:entry, why = 1: exit
	 */
	saved_why = regs->regs[SYSTRACE_SAVENUM];
	regs->regs[SYSTRACE_SAVENUM] = why;

	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
					? 0x80 : 0));

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}

	regs->regs[SYSTRACE_SAVENUM] = saved_why;
}

extern void show_stack(struct task_struct *task, unsigned long *stack);
void show_regs(struct pt_regs *fp)
{
	unsigned long   *sp;
	unsigned char   *tp;
	int	i;

	pr_info("\nCURRENT PROCESS:\n\n");
	pr_info("COMM=%s PID=%d\n", current->comm, current->pid);

	if (current->mm) {
		pr_info("TEXT=%08x-%08x DATA=%08x-%08x BSS=%08x-%08x\n",
		       (int) current->mm->start_code,
		       (int) current->mm->end_code,
		       (int) current->mm->start_data,
		       (int) current->mm->end_data,
		       (int) current->mm->end_data,
		       (int) current->mm->brk);
		pr_info("USER-STACK=%08x  KERNEL-STACK=%08x\n\n",
		       (int) current->mm->start_stack,
		       (int) (((unsigned long) current) + 2 * PAGE_SIZE));
	}

	pr_info("PC: 0x%08lx (%pS)\n", (long)fp->pc, (void *)fp->pc);
	pr_info("LR: 0x%08lx (%pS)\n", (long)fp->lr, (void *)fp->lr);
	pr_info("SP: 0x%08lx\n", (long)fp);
	pr_info("orig_a0: 0x%08lx\n", fp->orig_a0);
	pr_info("PSR: 0x%08lx\n", (long)fp->sr);

	pr_info(" a0: 0x%08lx   a1: 0x%08lx   a2: 0x%08lx   a3: 0x%08lx\n",
		fp->a0, fp->a1, fp->a2, fp->a3);
#if defined(__CSKYABIV2__)
	pr_info(" r4: 0x%08lx   r5: 0x%08lx   r6: 0x%08lx   r7: 0x%08lx\n",
		fp->regs[0], fp->regs[1], fp->regs[2], fp->regs[3]);
	pr_info(" r8: 0x%08lx   r9: 0x%08lx  r10: 0x%08lx  r11: 0x%08lx\n",
		fp->regs[4], fp->regs[5], fp->regs[6], fp->regs[7]);
	pr_info("r12: 0x%08lx  r13: 0x%08lx  r15: 0x%08lx\n",
		fp->regs[8], fp->regs[9], fp->lr);
	pr_info("r16: 0x%08lx  r17: 0x%08lx  r18: 0x%08lx  r19: 0x%08lx\n",
		fp->exregs[0], fp->exregs[1], fp->exregs[2], fp->exregs[3]);
	pr_info("r20: 0x%08lx  r21: 0x%08lx  r22: 0x%08lx  r23: 0x%08lx\n",
		fp->exregs[4], fp->exregs[5], fp->exregs[6], fp->exregs[7]);
	pr_info("r24: 0x%08lx  r25: 0x%08lx  r26: 0x%08lx  r27: 0x%08lx\n",
		fp->exregs[8], fp->exregs[9], fp->exregs[10], fp->exregs[11]);
	pr_info("r28: 0x%08lx  r29: 0x%08lx  r30: 0x%08lx  tls: 0x%08lx\n",
		fp->exregs[12], fp->exregs[13], fp->exregs[14], fp->tls);
	pr_info(" hi: 0x%08lx   lo: 0x%08lx\n",
		fp->rhi, fp->rlo);
#else
	pr_info(" r6: 0x%08lx   r7: 0x%08lx   r8: 0x%08lx   r9: 0x%08lx\n",
		fp->regs[0], fp->regs[1], fp->regs[2], fp->regs[3]);
	pr_info("r10: 0x%08lx  r11: 0x%08lx  r12: 0x%08lx  r13: 0x%08lx\n",
		fp->regs[4], fp->regs[5], fp->regs[6], fp->regs[7]);
	pr_info("r14: 0x%08lx   r1: 0x%08lx  r15: 0x%08lx\n",
		fp->regs[8], fp->regs[9], fp->lr);
#endif

	pr_info("\nCODE:");
	tp = ((unsigned char *) fp->pc) - 0x20;
	tp += ((int)tp % 4) ? 2 : 0;
	for (sp = (unsigned long *) tp, i = 0; (i < 0x40);  i += 4) {
		if ((i % 0x10) == 0)
			pr_cont("\n%08x: ", (int) (tp + i));
		pr_cont("%08x ", (int) *sp++);
	}
	pr_cont("\n");

	pr_info("\nKERNEL STACK:");
	tp = ((unsigned char *) fp) - 0x40;
	for (sp = (unsigned long *) tp, i = 0; (i < 0xc0); i += 4) {
		if ((i % 0x10) == 0)
			pr_cont("\n%08x: ", (int) (tp + i));
		pr_cont("%08x ", (int) *sp++);
	}
	pr_cont("\n");

	show_stack(NULL, (unsigned long *)fp->regs[4]);
	return;
}
