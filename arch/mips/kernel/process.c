/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000 by Ralf Baechle and others.
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2004 Thiemo Seufer
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/mman.h>
#include <linux/personality.h>
#include <linux/sys.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/init.h>
#include <linux/completion.h>

#include <asm/abi.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/dsp.h>
#include <asm/fpu.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/elf.h>
#include <asm/isadep.h>
#include <asm/inst.h>

/*
 * The idle thread. There's no useful work to be done, so just try to conserve
 * power and have a low exit latency (ie sit in a loop waiting for somebody to
 * say that they'd like to reschedule)
 */
ATTRIB_NORET void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		while (!need_resched())
			if (cpu_wait)
				(*cpu_wait)();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

extern int do_signal(sigset_t *oldset, struct pt_regs *regs);
extern int do_signal32(sigset_t *oldset, struct pt_regs *regs);

/*
 * Native o32 and N64 ABI without DSP ASE
 */
extern int setup_frame(struct k_sigaction * ka, struct pt_regs *regs,
        int signr, sigset_t *set);
extern int setup_rt_frame(struct k_sigaction * ka, struct pt_regs *regs,
        int signr, sigset_t *set, siginfo_t *info);

struct mips_abi mips_abi = {
	.do_signal	= do_signal,
#ifdef CONFIG_TRAD_SIGNALS
	.setup_frame	= setup_frame,
#endif
	.setup_rt_frame	= setup_rt_frame
};

#ifdef CONFIG_MIPS32_O32
/*
 * o32 compatibility on 64-bit kernels, without DSP ASE
 */
extern int setup_frame_32(struct k_sigaction * ka, struct pt_regs *regs,
        int signr, sigset_t *set);
extern int setup_rt_frame_32(struct k_sigaction * ka, struct pt_regs *regs,
        int signr, sigset_t *set, siginfo_t *info);

struct mips_abi mips_abi_32 = {
	.do_signal	= do_signal32,
	.setup_frame	= setup_frame_32,
	.setup_rt_frame	= setup_rt_frame_32
};
#endif /* CONFIG_MIPS32_O32 */

#ifdef CONFIG_MIPS32_N32
/*
 * N32 on 64-bit kernels, without DSP ASE
 */
extern int setup_rt_frame_n32(struct k_sigaction * ka, struct pt_regs *regs,
        int signr, sigset_t *set, siginfo_t *info);

struct mips_abi mips_abi_n32 = {
	.do_signal	= do_signal,
	.setup_rt_frame	= setup_rt_frame_n32
};
#endif /* CONFIG_MIPS32_N32 */

asmlinkage void ret_from_fork(void);

void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp)
{
	unsigned long status;

	/* New thread loses kernel privileges. */
	status = regs->cp0_status & ~(ST0_CU0|ST0_CU1|KU_MASK);
#ifdef CONFIG_64BIT
	status &= ~ST0_FR;
	status |= (current->thread.mflags & MF_32BIT_REGS) ? 0 : ST0_FR;
#endif
	status |= KU_USER;
	regs->cp0_status = status;
	clear_used_math();
	lose_fpu();
	if (cpu_has_dsp)
		__init_dsp();
	regs->cp0_epc = pc;
	regs->regs[29] = sp;
	current_thread_info()->addr_limit = USER_DS;
}

void exit_thread(void)
{
}

void flush_thread(void)
{
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	unsigned long unused, struct task_struct *p, struct pt_regs *regs)
{
	struct thread_info *ti = p->thread_info;
	struct pt_regs *childregs;
	long childksp;
	p->set_child_tid = p->clear_child_tid = NULL;

	childksp = (unsigned long)ti + THREAD_SIZE - 32;

	preempt_disable();

	if (is_fpu_owner())
		save_fp(p);

	if (cpu_has_dsp)
		save_dsp(p);

	preempt_enable();

	/* set up new TSS. */
	childregs = (struct pt_regs *) childksp - 1;
	*childregs = *regs;
	childregs->regs[7] = 0;	/* Clear error flag */

#if defined(CONFIG_BINFMT_IRIX)
	if (current->personality != PER_LINUX) {
		/* Under IRIX things are a little different. */
		childregs->regs[3] = 1;
		regs->regs[3] = 0;
	}
#endif
	childregs->regs[2] = 0;	/* Child gets zero as return value */
	regs->regs[2] = p->pid;

	if (childregs->cp0_status & ST0_CU0) {
		childregs->regs[28] = (unsigned long) ti;
		childregs->regs[29] = childksp;
		ti->addr_limit = KERNEL_DS;
	} else {
		childregs->regs[29] = usp;
		ti->addr_limit = USER_DS;
	}
	p->thread.reg29 = (unsigned long) childregs;
	p->thread.reg31 = (unsigned long) ret_from_fork;

	/*
	 * New tasks lose permission to use the fpu. This accelerates context
	 * switching for most programs since they don't use the fpu.
	 */
	p->thread.cp0_status = read_c0_status() & ~(ST0_CU2|ST0_CU1);
	childregs->cp0_status &= ~(ST0_CU2|ST0_CU1);
	clear_tsk_thread_flag(p, TIF_USEDFPU);

	if (clone_flags & CLONE_SETTLS)
		ti->tp_value = regs->regs[7];

	return 0;
}

/* Fill in the fpu structure for a core dump.. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *r)
{
	memcpy(r, &current->thread.fpu, sizeof(current->thread.fpu));

	return 1;
}

void dump_regs(elf_greg_t *gp, struct pt_regs *regs)
{
	int i;

	for (i = 0; i < EF_R0; i++)
		gp[i] = 0;
	gp[EF_R0] = 0;
	for (i = 1; i <= 31; i++)
		gp[EF_R0 + i] = regs->regs[i];
	gp[EF_R26] = 0;
	gp[EF_R27] = 0;
	gp[EF_LO] = regs->lo;
	gp[EF_HI] = regs->hi;
	gp[EF_CP0_EPC] = regs->cp0_epc;
	gp[EF_CP0_BADVADDR] = regs->cp0_badvaddr;
	gp[EF_CP0_STATUS] = regs->cp0_status;
	gp[EF_CP0_CAUSE] = regs->cp0_cause;
#ifdef EF_UNUSED0
	gp[EF_UNUSED0] = 0;
#endif
}

int dump_task_regs (struct task_struct *tsk, elf_gregset_t *regs)
{
	struct thread_info *ti = tsk->thread_info;
	long ksp = (unsigned long)ti + THREAD_SIZE - 32;
	dump_regs(&(*regs)[0], (struct pt_regs *) ksp - 1);
	return 1;
}

int dump_task_fpu (struct task_struct *t, elf_fpregset_t *fpr)
{
	memcpy(fpr, &t->thread.fpu, sizeof(current->thread.fpu));

	return 1;
}

/*
 * Create a kernel thread
 */
ATTRIB_NORET void kernel_thread_helper(void *arg, int (*fn)(void *))
{
	do_exit(fn(arg));
}

long kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));

	regs.regs[4] = (unsigned long) arg;
	regs.regs[5] = (unsigned long) fn;
	regs.cp0_epc = (unsigned long) kernel_thread_helper;
	regs.cp0_status = read_c0_status();
#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)
	regs.cp0_status &= ~(ST0_KUP | ST0_IEC);
	regs.cp0_status |= ST0_IEP;
#else
	regs.cp0_status |= ST0_EXL;
#endif

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL, NULL);
}

static struct mips_frame_info {
	void *func;
	int omit_fp;	/* compiled without fno-omit-frame-pointer */
	int frame_offset;
	int pc_offset;
} schedule_frame, mfinfo[] = {
	{ schedule, 0 },	/* must be first */
	/* arch/mips/kernel/semaphore.c */
	{ __down, 1 },
	{ __down_interruptible, 1 },
	/* kernel/sched.c */
#ifdef CONFIG_PREEMPT
	{ preempt_schedule, 0 },
#endif
	{ wait_for_completion, 0 },
	{ interruptible_sleep_on, 0 },
	{ interruptible_sleep_on_timeout, 0 },
	{ sleep_on, 0 },
	{ sleep_on_timeout, 0 },
	{ yield, 0 },
	{ io_schedule, 0 },
	{ io_schedule_timeout, 0 },
#if defined(CONFIG_SMP) && defined(CONFIG_PREEMPT)
	{ __preempt_spin_lock, 0 },
	{ __preempt_write_lock, 0 },
#endif
	/* kernel/timer.c */
	{ schedule_timeout, 1 },
/*	{ nanosleep_restart, 1 }, */
	/* lib/rwsem-spinlock.c */
	{ __down_read, 1 },
	{ __down_write, 1 },
};

static int mips_frame_info_initialized;
static int __init get_frame_info(struct mips_frame_info *info)
{
	int i;
	void *func = info->func;
	union mips_instruction *ip = (union mips_instruction *)func;
	info->pc_offset = -1;
	info->frame_offset = info->omit_fp ? 0 : -1;
	for (i = 0; i < 128; i++, ip++) {
		/* if jal, jalr, jr, stop. */
		if (ip->j_format.opcode == jal_op ||
		    (ip->r_format.opcode == spec_op &&
		     (ip->r_format.func == jalr_op ||
		      ip->r_format.func == jr_op)))
			break;

		if (
#ifdef CONFIG_32BIT
		    ip->i_format.opcode == sw_op &&
#endif
#ifdef CONFIG_64BIT
		    ip->i_format.opcode == sd_op &&
#endif
		    ip->i_format.rs == 29)
		{
			/* sw / sd $ra, offset($sp) */
			if (ip->i_format.rt == 31) {
				if (info->pc_offset != -1)
					continue;
				info->pc_offset =
					ip->i_format.simmediate / sizeof(long);
			}
			/* sw / sd $s8, offset($sp) */
			if (ip->i_format.rt == 30) {
//#if 0	/* gcc 3.4 does aggressive optimization... */
				if (info->frame_offset != -1)
					continue;
//#endif
				info->frame_offset =
					ip->i_format.simmediate / sizeof(long);
			}
		}
	}
	if (info->pc_offset == -1 || info->frame_offset == -1) {
		printk("Can't analyze prologue code at %p\n", func);
		info->pc_offset = -1;
		info->frame_offset = -1;
		return -1;
	}

	return 0;
}

static int __init frame_info_init(void)
{
	int i, found;
	for (i = 0; i < ARRAY_SIZE(mfinfo); i++)
		if (get_frame_info(&mfinfo[i]))
			return -1;
	schedule_frame = mfinfo[0];
	/* bubble sort */
	do {
		struct mips_frame_info tmp;
		found = 0;
		for (i = 1; i < ARRAY_SIZE(mfinfo); i++) {
			if (mfinfo[i-1].func > mfinfo[i].func) {
				tmp = mfinfo[i];
				mfinfo[i] = mfinfo[i-1];
				mfinfo[i-1] = tmp;
				found = 1;
			}
		}
	} while (found);
	mips_frame_info_initialized = 1;
	return 0;
}

arch_initcall(frame_info_init);

/*
 * Return saved PC of a blocked thread.
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	struct thread_struct *t = &tsk->thread;

	/* New born processes are a special case */
	if (t->reg31 == (unsigned long) ret_from_fork)
		return t->reg31;

	if (schedule_frame.pc_offset < 0)
		return 0;
	return ((unsigned long *)t->reg29)[schedule_frame.pc_offset];
}

/* get_wchan - a maintenance nightmare^W^Wpain in the ass ...  */
unsigned long get_wchan(struct task_struct *p)
{
	unsigned long stack_page;
	unsigned long frame, pc;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	stack_page = (unsigned long)p->thread_info;
	if (!stack_page || !mips_frame_info_initialized)
		return 0;

	pc = thread_saved_pc(p);
	if (!in_sched_functions(pc))
		return pc;

	frame = ((unsigned long *)p->thread.reg30)[schedule_frame.frame_offset];
	do {
		int i;

		if (frame < stack_page || frame > stack_page + THREAD_SIZE - 32)
			return 0;

		for (i = ARRAY_SIZE(mfinfo) - 1; i >= 0; i--) {
			if (pc >= (unsigned long) mfinfo[i].func)
				break;
		}
		if (i < 0)
			break;

		if (mfinfo[i].omit_fp)
			break;
		pc = ((unsigned long *)frame)[mfinfo[i].pc_offset];
		frame = ((unsigned long *)frame)[mfinfo[i].frame_offset];
	} while (in_sched_functions(pc));

	return pc;
}

EXPORT_SYMBOL(get_wchan);
