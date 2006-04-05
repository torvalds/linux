/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000 by Ralf Baechle and others.
 * Copyright (C) 2005, 2006 by Ralf Baechle (ralf@linux-mips.org)
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
#include <linux/kallsyms.h>

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
#ifdef CONFIG_MIPS_MT_SMTC
#include <asm/mipsmtregs.h>
extern void smtc_idle_loop_hook(void);
#endif /* CONFIG_MIPS_MT_SMTC */

/*
 * The idle thread. There's no useful work to be done, so just try to conserve
 * power and have a low exit latency (ie sit in a loop waiting for somebody to
 * say that they'd like to reschedule)
 */
ATTRIB_NORET void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		while (!need_resched()) {
#ifdef CONFIG_MIPS_MT_SMTC
			smtc_idle_loop_hook();
#endif /* CONFIG_MIPS_MT_SMTC */
			if (cpu_wait)
				(*cpu_wait)();
		}
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

/*
 * Native o32 and N64 ABI without DSP ASE
 */
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
	struct thread_info *ti = task_thread_info(p);
	struct pt_regs *childregs;
	long childksp;
	p->set_child_tid = p->clear_child_tid = NULL;

	childksp = (unsigned long)task_stack_page(p) + THREAD_SIZE - 32;

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

void elf_dump_regs(elf_greg_t *gp, struct pt_regs *regs)
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
	elf_dump_regs(*regs, task_pt_regs(tsk));
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
	unsigned long func_size;
	int frame_size;
	int pc_offset;
} *schedule_frame, mfinfo[64];
static int mfinfo_num;

static int __init get_frame_info(struct mips_frame_info *info)
{
	int i;
	void *func = info->func;
	union mips_instruction *ip = (union mips_instruction *)func;
	info->pc_offset = -1;
	info->frame_size = 0;
	for (i = 0; i < 128; i++, ip++) {
		/* if jal, jalr, jr, stop. */
		if (ip->j_format.opcode == jal_op ||
		    (ip->r_format.opcode == spec_op &&
		     (ip->r_format.func == jalr_op ||
		      ip->r_format.func == jr_op)))
			break;

		if (info->func_size && i >= info->func_size / 4)
			break;
		if (
#ifdef CONFIG_32BIT
		    ip->i_format.opcode == addiu_op &&
#endif
#ifdef CONFIG_64BIT
		    ip->i_format.opcode == daddiu_op &&
#endif
		    ip->i_format.rs == 29 &&
		    ip->i_format.rt == 29) {
			/* addiu/daddiu sp,sp,-imm */
			if (info->frame_size)
				continue;
			info->frame_size = - ip->i_format.simmediate;
		}

		if (
#ifdef CONFIG_32BIT
		    ip->i_format.opcode == sw_op &&
#endif
#ifdef CONFIG_64BIT
		    ip->i_format.opcode == sd_op &&
#endif
		    ip->i_format.rs == 29 &&
		    ip->i_format.rt == 31) {
			/* sw / sd $ra, offset($sp) */
			if (info->pc_offset != -1)
				continue;
			info->pc_offset =
				ip->i_format.simmediate / sizeof(long);
		}
	}
	if (info->pc_offset == -1 || info->frame_size == 0) {
		if (func == schedule)
			printk("Can't analyze prologue code at %p\n", func);
		info->pc_offset = -1;
		info->frame_size = 0;
	}

	return 0;
}

static int __init frame_info_init(void)
{
	int i;
#ifdef CONFIG_KALLSYMS
	char *modname;
	char namebuf[KSYM_NAME_LEN + 1];
	unsigned long start, size, ofs;
	extern char __sched_text_start[], __sched_text_end[];
	extern char __lock_text_start[], __lock_text_end[];

	start = (unsigned long)__sched_text_start;
	for (i = 0; i < ARRAY_SIZE(mfinfo); i++) {
		if (start == (unsigned long)schedule)
			schedule_frame = &mfinfo[i];
		if (!kallsyms_lookup(start, &size, &ofs, &modname, namebuf))
			break;
		mfinfo[i].func = (void *)(start + ofs);
		mfinfo[i].func_size = size;
		start += size - ofs;
		if (start >= (unsigned long)__lock_text_end)
			break;
		if (start == (unsigned long)__sched_text_end)
			start = (unsigned long)__lock_text_start;
	}
#else
	mfinfo[0].func = schedule;
	schedule_frame = &mfinfo[0];
#endif
	for (i = 0; i < ARRAY_SIZE(mfinfo) && mfinfo[i].func; i++)
		get_frame_info(&mfinfo[i]);

	mfinfo_num = i;
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

	if (!schedule_frame || schedule_frame->pc_offset < 0)
		return 0;
	return ((unsigned long *)t->reg29)[schedule_frame->pc_offset];
}

/* get_wchan - a maintenance nightmare^W^Wpain in the ass ...  */
unsigned long get_wchan(struct task_struct *p)
{
	unsigned long stack_page;
	unsigned long pc;
#ifdef CONFIG_KALLSYMS
	unsigned long frame;
#endif

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	stack_page = (unsigned long)task_stack_page(p);
	if (!stack_page || !mfinfo_num)
		return 0;

	pc = thread_saved_pc(p);
#ifdef CONFIG_KALLSYMS
	if (!in_sched_functions(pc))
		return pc;

	frame = p->thread.reg29 + schedule_frame->frame_size;
	do {
		int i;

		if (frame < stack_page || frame > stack_page + THREAD_SIZE - 32)
			return 0;

		for (i = mfinfo_num - 1; i >= 0; i--) {
			if (pc >= (unsigned long) mfinfo[i].func)
				break;
		}
		if (i < 0)
			break;

		pc = ((unsigned long *)frame)[mfinfo[i].pc_offset];
		if (!mfinfo[i].frame_size)
			break;
		frame += mfinfo[i].frame_size;
	} while (in_sched_functions(pc));
#endif

	return pc;
}

