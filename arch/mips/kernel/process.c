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
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/export.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/personality.h>
#include <linux/sys.h>
#include <linux/user.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/kallsyms.h>
#include <linux/random.h>

#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/dsp.h>
#include <asm/fpu.h>
#include <asm/pgtable.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/elf.h>
#include <asm/isadep.h>
#include <asm/inst.h>
#include <asm/stacktrace.h>

/*
 * The idle thread. There's no useful work to be done, so just try to conserve
 * power and have a low exit latency (ie sit in a loop waiting for somebody to
 * say that they'd like to reschedule)
 */
void __noreturn cpu_idle(void)
{
	int cpu;

	/* CPU is going idle. */
	cpu = smp_processor_id();

	/* endless idle loop with no priority at all */
	while (1) {
		tick_nohz_idle_enter();
		rcu_idle_enter();
		while (!need_resched() && cpu_online(cpu)) {
#ifdef CONFIG_MIPS_MT_SMTC
			extern void smtc_idle_loop_hook(void);

			smtc_idle_loop_hook();
#endif

			if (cpu_wait) {
				/* Don't trace irqs off for idle */
				stop_critical_timings();
				(*cpu_wait)();
				start_critical_timings();
			}
		}
#ifdef CONFIG_HOTPLUG_CPU
		if (!cpu_online(cpu) && !cpu_isset(cpu, cpu_callin_map))
			play_dead();
#endif
		rcu_idle_exit();
		tick_nohz_idle_exit();
		schedule_preempt_disabled();
	}
}

asmlinkage void ret_from_fork(void);
asmlinkage void ret_from_kernel_thread(void);

void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp)
{
	unsigned long status;

	/* New thread loses kernel privileges. */
	status = regs->cp0_status & ~(ST0_CU0|ST0_CU1|ST0_FR|KU_MASK);
#ifdef CONFIG_64BIT
	status |= test_thread_flag(TIF_32BIT_REGS) ? 0 : ST0_FR;
#endif
	status |= KU_USER;
	regs->cp0_status = status;
	clear_used_math();
	clear_fpu_owner();
	if (cpu_has_dsp)
		__init_dsp();
	regs->cp0_epc = pc;
	regs->regs[29] = sp;
}

void exit_thread(void)
{
}

void flush_thread(void)
{
}

int copy_thread(unsigned long clone_flags, unsigned long usp,
	unsigned long arg, struct task_struct *p)
{
	struct thread_info *ti = task_thread_info(p);
	struct pt_regs *childregs, *regs = current_pt_regs();
	unsigned long childksp;
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
	/*  Put the stack after the struct pt_regs.  */
	childksp = (unsigned long) childregs;
	p->thread.cp0_status = read_c0_status() & ~(ST0_CU2|ST0_CU1);
	if (unlikely(p->flags & PF_KTHREAD)) {
		unsigned long status = p->thread.cp0_status;
		memset(childregs, 0, sizeof(struct pt_regs));
		ti->addr_limit = KERNEL_DS;
		p->thread.reg16 = usp; /* fn */
		p->thread.reg17 = arg;
		p->thread.reg29 = childksp;
		p->thread.reg31 = (unsigned long) ret_from_kernel_thread;
#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)
		status = (status & ~(ST0_KUP | ST0_IEP | ST0_IEC)) |
			 ((status & (ST0_KUC | ST0_IEC)) << 2);
#else
		status |= ST0_EXL;
#endif
		childregs->cp0_status = status;
		return 0;
	}
	*childregs = *regs;
	childregs->regs[7] = 0;	/* Clear error flag */
	childregs->regs[2] = 0;	/* Child gets zero as return value */
	childregs->regs[29] = usp;
	ti->addr_limit = USER_DS;

	p->thread.reg29 = (unsigned long) childregs;
	p->thread.reg31 = (unsigned long) ret_from_fork;

	/*
	 * New tasks lose permission to use the fpu. This accelerates context
	 * switching for most programs since they don't use the fpu.
	 */
	childregs->cp0_status &= ~(ST0_CU2|ST0_CU1);

#ifdef CONFIG_MIPS_MT_SMTC
	/*
	 * SMTC restores TCStatus after Status, and the CU bits
	 * are aliased there.
	 */
	childregs->cp0_tcstatus &= ~(ST0_CU2|ST0_CU1);
#endif
	clear_tsk_thread_flag(p, TIF_USEDFPU);

#ifdef CONFIG_MIPS_MT_FPAFF
	clear_tsk_thread_flag(p, TIF_FPUBOUND);
#endif /* CONFIG_MIPS_MT_FPAFF */

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

int dump_task_regs(struct task_struct *tsk, elf_gregset_t *regs)
{
	elf_dump_regs(*regs, task_pt_regs(tsk));
	return 1;
}

int dump_task_fpu(struct task_struct *t, elf_fpregset_t *fpr)
{
	memcpy(fpr, &t->thread.fpu, sizeof(current->thread.fpu));

	return 1;
}

/*
 *
 */
struct mips_frame_info {
	void		*func;
	unsigned long	func_size;
	int		frame_size;
	int		pc_offset;
};

static inline int is_ra_save_ins(union mips_instruction *ip)
{
	/* sw / sd $ra, offset($sp) */
	return (ip->i_format.opcode == sw_op || ip->i_format.opcode == sd_op) &&
		ip->i_format.rs == 29 &&
		ip->i_format.rt == 31;
}

static inline int is_jal_jalr_jr_ins(union mips_instruction *ip)
{
	if (ip->j_format.opcode == jal_op)
		return 1;
	if (ip->r_format.opcode != spec_op)
		return 0;
	return ip->r_format.func == jalr_op || ip->r_format.func == jr_op;
}

static inline int is_sp_move_ins(union mips_instruction *ip)
{
	/* addiu/daddiu sp,sp,-imm */
	if (ip->i_format.rs != 29 || ip->i_format.rt != 29)
		return 0;
	if (ip->i_format.opcode == addiu_op || ip->i_format.opcode == daddiu_op)
		return 1;
	return 0;
}

static int get_frame_info(struct mips_frame_info *info)
{
	union mips_instruction *ip = info->func;
	unsigned max_insns = info->func_size / sizeof(union mips_instruction);
	unsigned i;

	info->pc_offset = -1;
	info->frame_size = 0;

	if (!ip)
		goto err;

	if (max_insns == 0)
		max_insns = 128U;	/* unknown function size */
	max_insns = min(128U, max_insns);

	for (i = 0; i < max_insns; i++, ip++) {

		if (is_jal_jalr_jr_ins(ip))
			break;
		if (!info->frame_size) {
			if (is_sp_move_ins(ip))
				info->frame_size = - ip->i_format.simmediate;
			continue;
		}
		if (info->pc_offset == -1 && is_ra_save_ins(ip)) {
			info->pc_offset =
				ip->i_format.simmediate / sizeof(long);
			break;
		}
	}
	if (info->frame_size && info->pc_offset >= 0) /* nested */
		return 0;
	if (info->pc_offset < 0) /* leaf */
		return 1;
	/* prologue seems boggus... */
err:
	return -1;
}

static struct mips_frame_info schedule_mfi __read_mostly;

static int __init frame_info_init(void)
{
	unsigned long size = 0;
#ifdef CONFIG_KALLSYMS
	unsigned long ofs;

	kallsyms_lookup_size_offset((unsigned long)schedule, &size, &ofs);
#endif
	schedule_mfi.func = schedule;
	schedule_mfi.func_size = size;

	get_frame_info(&schedule_mfi);

	/*
	 * Without schedule() frame info, result given by
	 * thread_saved_pc() and get_wchan() are not reliable.
	 */
	if (schedule_mfi.pc_offset < 0)
		printk("Can't analyze schedule() prologue at %p\n", schedule);

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
	if (schedule_mfi.pc_offset < 0)
		return 0;
	return ((unsigned long *)t->reg29)[schedule_mfi.pc_offset];
}


#ifdef CONFIG_KALLSYMS
/* generic stack unwinding function */
unsigned long notrace unwind_stack_by_address(unsigned long stack_page,
					      unsigned long *sp,
					      unsigned long pc,
					      unsigned long *ra)
{
	struct mips_frame_info info;
	unsigned long size, ofs;
	int leaf;
	extern void ret_from_irq(void);
	extern void ret_from_exception(void);

	if (!stack_page)
		return 0;

	/*
	 * If we reached the bottom of interrupt context,
	 * return saved pc in pt_regs.
	 */
	if (pc == (unsigned long)ret_from_irq ||
	    pc == (unsigned long)ret_from_exception) {
		struct pt_regs *regs;
		if (*sp >= stack_page &&
		    *sp + sizeof(*regs) <= stack_page + THREAD_SIZE - 32) {
			regs = (struct pt_regs *)*sp;
			pc = regs->cp0_epc;
			if (__kernel_text_address(pc)) {
				*sp = regs->regs[29];
				*ra = regs->regs[31];
				return pc;
			}
		}
		return 0;
	}
	if (!kallsyms_lookup_size_offset(pc, &size, &ofs))
		return 0;
	/*
	 * Return ra if an exception occurred at the first instruction
	 */
	if (unlikely(ofs == 0)) {
		pc = *ra;
		*ra = 0;
		return pc;
	}

	info.func = (void *)(pc - ofs);
	info.func_size = ofs;	/* analyze from start to ofs */
	leaf = get_frame_info(&info);
	if (leaf < 0)
		return 0;

	if (*sp < stack_page ||
	    *sp + info.frame_size > stack_page + THREAD_SIZE - 32)
		return 0;

	if (leaf)
		/*
		 * For some extreme cases, get_frame_info() can
		 * consider wrongly a nested function as a leaf
		 * one. In that cases avoid to return always the
		 * same value.
		 */
		pc = pc != *ra ? *ra : 0;
	else
		pc = ((unsigned long *)(*sp))[info.pc_offset];

	*sp += info.frame_size;
	*ra = 0;
	return __kernel_text_address(pc) ? pc : 0;
}
EXPORT_SYMBOL(unwind_stack_by_address);

/* used by show_backtrace() */
unsigned long unwind_stack(struct task_struct *task, unsigned long *sp,
			   unsigned long pc, unsigned long *ra)
{
	unsigned long stack_page = (unsigned long)task_stack_page(task);
	return unwind_stack_by_address(stack_page, sp, pc, ra);
}
#endif

/*
 * get_wchan - a maintenance nightmare^W^Wpain in the ass ...
 */
unsigned long get_wchan(struct task_struct *task)
{
	unsigned long pc = 0;
#ifdef CONFIG_KALLSYMS
	unsigned long sp;
	unsigned long ra = 0;
#endif

	if (!task || task == current || task->state == TASK_RUNNING)
		goto out;
	if (!task_stack_page(task))
		goto out;

	pc = thread_saved_pc(task);

#ifdef CONFIG_KALLSYMS
	sp = task->thread.reg29 + schedule_mfi.frame_size;

	while (in_sched_functions(pc))
		pc = unwind_stack(task, &sp, pc, &ra);
#endif

out:
	return pc;
}

/*
 * Don't forget that the stack pointer must be aligned on a 8 bytes
 * boundary for 32-bits ABI and 16 bytes for 64-bits ABI.
 */
unsigned long arch_align_stack(unsigned long sp)
{
	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		sp -= get_random_int() & ~PAGE_MASK;

	return sp & ALMASK;
}
