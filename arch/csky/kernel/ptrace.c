// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/audit.h>
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
#include <linux/tracehook.h>
#include <linux/uaccess.h>
#include <linux/user.h>

#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/asm-offsets.h>

#include <abi/regdef.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

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

	/* Enable irq */
	regs->sr |= BIT(6);
}

static void singlestep_enable(struct task_struct *tsk)
{
	struct pt_regs *regs;

	regs = task_pt_regs(tsk);
	regs->sr = (regs->sr & TRACE_MODE_MASK) | TRACE_MODE_SI;

	/* Disable irq */
	regs->sr &= ~BIT(6);
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
		   struct membuf to)
{
	struct pt_regs *regs = task_pt_regs(target);

	/* Abiv1 regs->tls is fake and we need sync here. */
	regs->tls = task_thread_info(target)->tp_value;

	return membuf_write(&to, regs, sizeof(regs));
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
		   struct membuf to)
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

	return membuf_write(&to, &tmp, sizeof(tmp));
#else
	return membuf_write(&to, regs, sizeof(*regs));
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
		.regset_get = gpr_get,
		.set = gpr_set,
	},
	[REGSET_FPR] = {
		.core_note_type = NT_PRFPREG,
		.n = sizeof(struct user_fp) / sizeof(u32),
		.size = sizeof(u32),
		.align = sizeof(u32),
		.regset_get = fpr_get,
		.set = fpr_set,
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

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r) {.name = #r, .offset = offsetof(struct pt_regs, r)}
#define REG_OFFSET_END {.name = NULL, .offset = 0}

static const struct pt_regs_offset regoffset_table[] = {
	REG_OFFSET_NAME(tls),
	REG_OFFSET_NAME(lr),
	REG_OFFSET_NAME(pc),
	REG_OFFSET_NAME(sr),
	REG_OFFSET_NAME(usp),
	REG_OFFSET_NAME(orig_a0),
	REG_OFFSET_NAME(a0),
	REG_OFFSET_NAME(a1),
	REG_OFFSET_NAME(a2),
	REG_OFFSET_NAME(a3),
	REG_OFFSET_NAME(regs[0]),
	REG_OFFSET_NAME(regs[1]),
	REG_OFFSET_NAME(regs[2]),
	REG_OFFSET_NAME(regs[3]),
	REG_OFFSET_NAME(regs[4]),
	REG_OFFSET_NAME(regs[5]),
	REG_OFFSET_NAME(regs[6]),
	REG_OFFSET_NAME(regs[7]),
	REG_OFFSET_NAME(regs[8]),
	REG_OFFSET_NAME(regs[9]),
#if defined(__CSKYABIV2__)
	REG_OFFSET_NAME(exregs[0]),
	REG_OFFSET_NAME(exregs[1]),
	REG_OFFSET_NAME(exregs[2]),
	REG_OFFSET_NAME(exregs[3]),
	REG_OFFSET_NAME(exregs[4]),
	REG_OFFSET_NAME(exregs[5]),
	REG_OFFSET_NAME(exregs[6]),
	REG_OFFSET_NAME(exregs[7]),
	REG_OFFSET_NAME(exregs[8]),
	REG_OFFSET_NAME(exregs[9]),
	REG_OFFSET_NAME(exregs[10]),
	REG_OFFSET_NAME(exregs[11]),
	REG_OFFSET_NAME(exregs[12]),
	REG_OFFSET_NAME(exregs[13]),
	REG_OFFSET_NAME(exregs[14]),
	REG_OFFSET_NAME(rhi),
	REG_OFFSET_NAME(rlo),
	REG_OFFSET_NAME(dcsr),
#endif
	REG_OFFSET_END,
};

/**
 * regs_query_register_offset() - query register offset from its name
 * @name:	the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;

	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

/**
 * regs_within_kernel_stack() - check the address in the stack
 * @regs:      pt_regs which contains kernel stack pointer.
 * @addr:      address which is checked.
 *
 * regs_within_kernel_stack() checks @addr is within the kernel stack page(s).
 * If @addr is within the kernel stack, it returns true. If not, returns false.
 */
static bool regs_within_kernel_stack(struct pt_regs *regs, unsigned long addr)
{
	return (addr & ~(THREAD_SIZE - 1))  ==
		(kernel_stack_pointer(regs) & ~(THREAD_SIZE - 1));
}

/**
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:	pt_regs which contains kernel stack pointer.
 * @n:		stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specified by @regs. If the @n th entry is NOT in the kernel stack,
 * this returns 0.
 */
unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n)
{
	unsigned long *addr = (unsigned long *)kernel_stack_pointer(regs);

	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return *addr;
	else
		return 0;
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

asmlinkage int syscall_trace_enter(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		if (tracehook_report_syscall_entry(regs))
			return -1;

	if (secure_computing() == -1)
		return -1;

	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_enter(regs, syscall_get_nr(current, regs));

	audit_syscall_entry(regs_syscallid(regs), regs->a0, regs->a1, regs->a2, regs->a3);
	return 0;
}

asmlinkage void syscall_trace_exit(struct pt_regs *regs)
{
	audit_syscall_exit(regs);

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, 0);

	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_exit(regs, syscall_get_return_value(current, regs));
}

void show_regs(struct pt_regs *fp)
{
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
	pr_info("r14: 0x%08lx   r1: 0x%08lx\n",
		fp->regs[8], fp->regs[9]);
#endif

	return;
}
