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

#include <asm/bootinfo.h>
#include <asm/cpu.h>
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
 * We use this if we don't have any better idle routine..
 * (This to kill: kernel/platform.c.
 */
void default_idle (void)
{
}

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
		schedule();
	}
}

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

	childksp = (unsigned long)ti + THREAD_SIZE - 32;

	preempt_disable();

	if (is_fpu_owner()) {
		save_fp(p);
	}

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

struct mips_frame_info {
	int frame_offset;
	int pc_offset;
};
static struct mips_frame_info schedule_frame;
static struct mips_frame_info schedule_timeout_frame;
static struct mips_frame_info sleep_on_frame;
static struct mips_frame_info sleep_on_timeout_frame;
static struct mips_frame_info wait_for_completion_frame;
static int mips_frame_info_initialized;
static int __init get_frame_info(struct mips_frame_info *info, void *func)
{
	int i;
	union mips_instruction *ip = (union mips_instruction *)func;
	info->pc_offset = -1;
	info->frame_offset = -1;
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
					break;
				info->pc_offset =
					ip->i_format.simmediate / sizeof(long);
			}
			/* sw / sd $s8, offset($sp) */
			if (ip->i_format.rt == 30) {
				if (info->frame_offset != -1)
					break;
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
	mips_frame_info_initialized =
		!get_frame_info(&schedule_frame, schedule) &&
		!get_frame_info(&schedule_timeout_frame, schedule_timeout) &&
		!get_frame_info(&sleep_on_frame, sleep_on) &&
		!get_frame_info(&sleep_on_timeout_frame, sleep_on_timeout) &&
		!get_frame_info(&wait_for_completion_frame, wait_for_completion);

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
	unsigned long frame, pc;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	if (!mips_frame_info_initialized)
		return 0;
	pc = thread_saved_pc(p);
	if (!in_sched_functions(pc))
		goto out;

	if (pc >= (unsigned long) sleep_on_timeout)
		goto schedule_timeout_caller;
	if (pc >= (unsigned long) sleep_on)
		goto schedule_caller;
	if (pc >= (unsigned long) interruptible_sleep_on_timeout)
		goto schedule_timeout_caller;
	if (pc >= (unsigned long)interruptible_sleep_on)
		goto schedule_caller;
	if (pc >= (unsigned long)wait_for_completion)
		goto schedule_caller;
	goto schedule_timeout_caller;

schedule_caller:
	frame = ((unsigned long *)p->thread.reg30)[schedule_frame.frame_offset];
	if (pc >= (unsigned long) sleep_on)
		pc = ((unsigned long *)frame)[sleep_on_frame.pc_offset];
	else
		pc = ((unsigned long *)frame)[wait_for_completion_frame.pc_offset];
	goto out;

schedule_timeout_caller:
	/*
	 * The schedule_timeout frame
	 */
	frame = ((unsigned long *)p->thread.reg30)[schedule_frame.frame_offset];

	/*
	 * frame now points to sleep_on_timeout's frame
	 */
	pc    = ((unsigned long *)frame)[schedule_timeout_frame.pc_offset];

	if (in_sched_functions(pc)) {
		/* schedule_timeout called by [interruptible_]sleep_on_timeout */
		frame = ((unsigned long *)frame)[schedule_timeout_frame.frame_offset];
		pc    = ((unsigned long *)frame)[sleep_on_timeout_frame.pc_offset];
	}

out:

#ifdef CONFIG_64BIT
	if (current->thread.mflags & MF_32BIT_REGS) /* Kludge for 32-bit ps  */
		pc &= 0xffffffffUL;
#endif

	return pc;
}

EXPORT_SYMBOL(get_wchan);
