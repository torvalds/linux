// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/ptrace.h>
#include <linux/sched/task_stack.h>
#include <linux/regset.h>
#include <linux/unistd.h>
#include <linux/elf.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r) {.name = #r, .offset = offsetof(struct pt_regs, r)}
#define REG_OFFSET_END {.name = NULL, .offset = 0}

#ifdef CONFIG_ISA_ARCOMPACT
static const struct pt_regs_offset regoffset_table[] = {
	REG_OFFSET_NAME(bta),
	REG_OFFSET_NAME(lp_start),
	REG_OFFSET_NAME(lp_end),
	REG_OFFSET_NAME(lp_count),
	REG_OFFSET_NAME(status32),
	REG_OFFSET_NAME(ret),
	REG_OFFSET_NAME(blink),
	REG_OFFSET_NAME(fp),
	REG_OFFSET_NAME(r26),
	REG_OFFSET_NAME(r12),
	REG_OFFSET_NAME(r11),
	REG_OFFSET_NAME(r10),
	REG_OFFSET_NAME(r9),
	REG_OFFSET_NAME(r8),
	REG_OFFSET_NAME(r7),
	REG_OFFSET_NAME(r6),
	REG_OFFSET_NAME(r5),
	REG_OFFSET_NAME(r4),
	REG_OFFSET_NAME(r3),
	REG_OFFSET_NAME(r2),
	REG_OFFSET_NAME(r1),
	REG_OFFSET_NAME(r0),
	REG_OFFSET_NAME(sp),
	REG_OFFSET_NAME(orig_r0),
	REG_OFFSET_NAME(event),
	REG_OFFSET_NAME(user_r25),
	REG_OFFSET_END,
};

#else

static const struct pt_regs_offset regoffset_table[] = {
	REG_OFFSET_NAME(orig_r0),
	REG_OFFSET_NAME(event),
	REG_OFFSET_NAME(bta),
	REG_OFFSET_NAME(user_r25),
	REG_OFFSET_NAME(r26),
	REG_OFFSET_NAME(fp),
	REG_OFFSET_NAME(sp),
	REG_OFFSET_NAME(r12),
	REG_OFFSET_NAME(r30),
#ifdef CONFIG_ARC_HAS_ACCL_REGS
	REG_OFFSET_NAME(r58),
	REG_OFFSET_NAME(r59),
#endif
#ifdef CONFIG_ARC_DSP_SAVE_RESTORE_REGS
	REG_OFFSET_NAME(DSP_CTRL),
#endif
	REG_OFFSET_NAME(r0),
	REG_OFFSET_NAME(r1),
	REG_OFFSET_NAME(r2),
	REG_OFFSET_NAME(r3),
	REG_OFFSET_NAME(r4),
	REG_OFFSET_NAME(r5),
	REG_OFFSET_NAME(r6),
	REG_OFFSET_NAME(r7),
	REG_OFFSET_NAME(r8),
	REG_OFFSET_NAME(r9),
	REG_OFFSET_NAME(r10),
	REG_OFFSET_NAME(r11),
	REG_OFFSET_NAME(blink),
	REG_OFFSET_NAME(lp_end),
	REG_OFFSET_NAME(lp_start),
	REG_OFFSET_NAME(lp_count),
	REG_OFFSET_NAME(ei),
	REG_OFFSET_NAME(ldi),
	REG_OFFSET_NAME(jli),
	REG_OFFSET_NAME(ret),
	REG_OFFSET_NAME(status32),
	REG_OFFSET_END,
};
#endif

static struct callee_regs *task_callee_regs(struct task_struct *tsk)
{
	struct callee_regs *tmp = (struct callee_regs *)tsk->thread.callee_reg;
	return tmp;
}

static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       struct membuf to)
{
	const struct pt_regs *ptregs = task_pt_regs(target);
	const struct callee_regs *cregs = task_callee_regs(target);
	unsigned int stop_pc_val;

	membuf_zero(&to, 4);	// pad
	membuf_store(&to, ptregs->bta);
	membuf_store(&to, ptregs->lp_start);
	membuf_store(&to, ptregs->lp_end);
	membuf_store(&to, ptregs->lp_count);
	membuf_store(&to, ptregs->status32);
	membuf_store(&to, ptregs->ret);
	membuf_store(&to, ptregs->blink);
	membuf_store(&to, ptregs->fp);
	membuf_store(&to, ptregs->r26);	// gp
	membuf_store(&to, ptregs->r12);
	membuf_store(&to, ptregs->r11);
	membuf_store(&to, ptregs->r10);
	membuf_store(&to, ptregs->r9);
	membuf_store(&to, ptregs->r8);
	membuf_store(&to, ptregs->r7);
	membuf_store(&to, ptregs->r6);
	membuf_store(&to, ptregs->r5);
	membuf_store(&to, ptregs->r4);
	membuf_store(&to, ptregs->r3);
	membuf_store(&to, ptregs->r2);
	membuf_store(&to, ptregs->r1);
	membuf_store(&to, ptregs->r0);
	membuf_store(&to, ptregs->sp);
	membuf_zero(&to, 4);	// pad2
	membuf_store(&to, cregs->r25);
	membuf_store(&to, cregs->r24);
	membuf_store(&to, cregs->r23);
	membuf_store(&to, cregs->r22);
	membuf_store(&to, cregs->r21);
	membuf_store(&to, cregs->r20);
	membuf_store(&to, cregs->r19);
	membuf_store(&to, cregs->r18);
	membuf_store(&to, cregs->r17);
	membuf_store(&to, cregs->r16);
	membuf_store(&to, cregs->r15);
	membuf_store(&to, cregs->r14);
	membuf_store(&to, cregs->r13);
	membuf_store(&to, target->thread.fault_address); // efa

	if (in_brkpt_trap(ptregs)) {
		stop_pc_val = target->thread.fault_address;
		pr_debug("\t\tstop_pc (brk-pt)\n");
	} else {
		stop_pc_val = ptregs->ret;
		pr_debug("\t\tstop_pc (others)\n");
	}

	return membuf_store(&to, stop_pc_val); // stop_pc
}

static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	const struct pt_regs *ptregs = task_pt_regs(target);
	const struct callee_regs *cregs = task_callee_regs(target);
	int ret = 0;

#define REG_IN_CHUNK(FIRST, NEXT, PTR)	\
	if (!ret)			\
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, \
			(void *)(PTR), \
			offsetof(struct user_regs_struct, FIRST), \
			offsetof(struct user_regs_struct, NEXT));

#define REG_IN_ONE(LOC, PTR)		\
	if (!ret)			\
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, \
			(void *)(PTR), \
			offsetof(struct user_regs_struct, LOC), \
			offsetof(struct user_regs_struct, LOC) + 4);

#define REG_IGNORE_ONE(LOC)		\
	if (!ret)			\
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf, \
			offsetof(struct user_regs_struct, LOC), \
			offsetof(struct user_regs_struct, LOC) + 4);

	REG_IGNORE_ONE(pad);

	REG_IN_ONE(scratch.bta, &ptregs->bta);
	REG_IN_ONE(scratch.lp_start, &ptregs->lp_start);
	REG_IN_ONE(scratch.lp_end, &ptregs->lp_end);
	REG_IN_ONE(scratch.lp_count, &ptregs->lp_count);

	REG_IGNORE_ONE(scratch.status32);

	REG_IN_ONE(scratch.ret, &ptregs->ret);
	REG_IN_ONE(scratch.blink, &ptregs->blink);
	REG_IN_ONE(scratch.fp, &ptregs->fp);
	REG_IN_ONE(scratch.gp, &ptregs->r26);
	REG_IN_ONE(scratch.r12, &ptregs->r12);
	REG_IN_ONE(scratch.r11, &ptregs->r11);
	REG_IN_ONE(scratch.r10, &ptregs->r10);
	REG_IN_ONE(scratch.r9, &ptregs->r9);
	REG_IN_ONE(scratch.r8, &ptregs->r8);
	REG_IN_ONE(scratch.r7, &ptregs->r7);
	REG_IN_ONE(scratch.r6, &ptregs->r6);
	REG_IN_ONE(scratch.r5, &ptregs->r5);
	REG_IN_ONE(scratch.r4, &ptregs->r4);
	REG_IN_ONE(scratch.r3, &ptregs->r3);
	REG_IN_ONE(scratch.r2, &ptregs->r2);
	REG_IN_ONE(scratch.r1, &ptregs->r1);
	REG_IN_ONE(scratch.r0, &ptregs->r0);
	REG_IN_ONE(scratch.sp, &ptregs->sp);

	REG_IGNORE_ONE(pad2);

	REG_IN_ONE(callee.r25, &cregs->r25);
	REG_IN_ONE(callee.r24, &cregs->r24);
	REG_IN_ONE(callee.r23, &cregs->r23);
	REG_IN_ONE(callee.r22, &cregs->r22);
	REG_IN_ONE(callee.r21, &cregs->r21);
	REG_IN_ONE(callee.r20, &cregs->r20);
	REG_IN_ONE(callee.r19, &cregs->r19);
	REG_IN_ONE(callee.r18, &cregs->r18);
	REG_IN_ONE(callee.r17, &cregs->r17);
	REG_IN_ONE(callee.r16, &cregs->r16);
	REG_IN_ONE(callee.r15, &cregs->r15);
	REG_IN_ONE(callee.r14, &cregs->r14);
	REG_IN_ONE(callee.r13, &cregs->r13);

	REG_IGNORE_ONE(efa);			/* efa update invalid */
	REG_IGNORE_ONE(stop_pc);		/* PC updated via @ret */

	return ret;
}

#ifdef CONFIG_ISA_ARCV2
static int arcv2regs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       struct membuf to)
{
	const struct pt_regs *regs = task_pt_regs(target);

	if (IS_ENABLED(CONFIG_ARC_HAS_ACCL_REGS))
		/*
		 * itemized copy not needed like above as layout of regs (r30,r58,r59)
		 * is exactly same in kernel (pt_regs) and userspace (user_regs_arcv2)
		 */
		return membuf_write(&to, &regs->r30, sizeof(struct user_regs_arcv2));


	membuf_write(&to, &regs->r30, 4); /* r30 only */
	return membuf_zero(&to, sizeof(struct user_regs_arcv2) - 4);
}

static int arcv2regs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	const struct pt_regs *regs = task_pt_regs(target);
	int ret, copy_sz;

	if (IS_ENABLED(CONFIG_ARC_HAS_ACCL_REGS))
		copy_sz = sizeof(struct user_regs_arcv2);
	else
		copy_sz = 4;	/* r30 only */

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, (void *)&regs->r30,
				  0, copy_sz);

	return ret;
}

#endif

enum arc_getset {
	REGSET_CMN,
	REGSET_ARCV2,
};

static const struct user_regset arc_regsets[] = {
	[REGSET_CMN] = {
	       .core_note_type = NT_PRSTATUS,
	       .n = ELF_NGREG,
	       .size = sizeof(unsigned long),
	       .align = sizeof(unsigned long),
	       .regset_get = genregs_get,
	       .set = genregs_set,
	},
#ifdef CONFIG_ISA_ARCV2
	[REGSET_ARCV2] = {
	       .core_note_type = NT_ARC_V2,
	       .n = ELF_ARCV2REG,
	       .size = sizeof(unsigned long),
	       .align = sizeof(unsigned long),
	       .regset_get = arcv2regs_get,
	       .set = arcv2regs_set,
	},
#endif
};

static const struct user_regset_view user_arc_view = {
	.name		= "arc",
	.e_machine	= EM_ARC_INUSE,
	.regsets	= arc_regsets,
	.n		= ARRAY_SIZE(arc_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_arc_view;
}

void ptrace_disable(struct task_struct *child)
{
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret = -EIO;

	pr_debug("REQ=%ld: ADDR =0x%lx, DATA=0x%lx)\n", request, addr, data);

	switch (request) {
	case PTRACE_GET_THREAD_AREA:
		ret = put_user(task_thread_info(child)->thr_ptr,
			       (unsigned long __user *)data);
		break;
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

asmlinkage int syscall_trace_entry(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		if (ptrace_report_syscall_entry(regs))
			return ULONG_MAX;

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_enter(regs, syscall_get_nr(current, regs));
#endif

	return regs->r8;
}

asmlinkage void syscall_trace_exit(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		ptrace_report_syscall_exit(regs, 0);

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_exit(regs, regs_return_value(regs));
#endif
}

int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;

	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

const char *regs_query_register_name(unsigned int offset)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (roff->offset == offset)
			return roff->name;
	return NULL;
}

bool regs_within_kernel_stack(struct pt_regs *regs, unsigned long addr)
{
	return (addr & ~(THREAD_SIZE - 1))  ==
		(kernel_stack_pointer(regs) & ~(THREAD_SIZE - 1));
}

unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n)
{
	unsigned long *addr = (unsigned long *)kernel_stack_pointer(regs);

	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return *addr;
	else
		return 0;
}
