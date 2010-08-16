/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

/*
 * Copyright (C) 2004 Amit S. Kale <amitkale@linsyssoft.com>
 * Copyright (C) 2000-2001 VERITAS Software Corporation.
 * Copyright (C) 2002 Andi Kleen, SuSE Labs
 * Copyright (C) 2004 LinSysSoft Technologies Pvt. Ltd.
 * Copyright (C) 2007 MontaVista Software, Inc.
 * Copyright (C) 2007-2008 Jason Wessel, Wind River Systems, Inc.
 */
/****************************************************************************
 *  Contributor:     Lake Stevens Instrument Division$
 *  Written by:      Glenn Engel $
 *  Updated by:	     Amit Kale<akale@veritas.com>
 *  Updated by:	     Tom Rini <trini@kernel.crashing.org>
 *  Updated by:	     Jason Wessel <jason.wessel@windriver.com>
 *  Modified for 386 by Jim Kingdon, Cygnus Support.
 *  Origianl kgdb, compatibility with 2.1.xx kernel by
 *  David Grothe <dave@gcom.com>
 *  Integrated into 2.2.5 kernel by Tigran Aivazian <tigran@sco.com>
 *  X86_64 changes from Andi Kleen's patch merged by Jim Houston
 */
#include <linux/spinlock.h>
#include <linux/kdebug.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/kgdb.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/nmi.h>
#include <linux/hw_breakpoint.h>

#include <asm/debugreg.h>
#include <asm/apicdef.h>
#include <asm/system.h>
#include <asm/apic.h>

/**
 *	pt_regs_to_gdb_regs - Convert ptrace regs to GDB regs
 *	@gdb_regs: A pointer to hold the registers in the order GDB wants.
 *	@regs: The &struct pt_regs of the current process.
 *
 *	Convert the pt_regs in @regs into the format for registers that
 *	GDB expects, stored in @gdb_regs.
 */
void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
#ifndef CONFIG_X86_32
	u32 *gdb_regs32 = (u32 *)gdb_regs;
#endif
	gdb_regs[GDB_AX]	= regs->ax;
	gdb_regs[GDB_BX]	= regs->bx;
	gdb_regs[GDB_CX]	= regs->cx;
	gdb_regs[GDB_DX]	= regs->dx;
	gdb_regs[GDB_SI]	= regs->si;
	gdb_regs[GDB_DI]	= regs->di;
	gdb_regs[GDB_BP]	= regs->bp;
	gdb_regs[GDB_PC]	= regs->ip;
#ifdef CONFIG_X86_32
	gdb_regs[GDB_PS]	= regs->flags;
	gdb_regs[GDB_DS]	= regs->ds;
	gdb_regs[GDB_ES]	= regs->es;
	gdb_regs[GDB_CS]	= regs->cs;
	gdb_regs[GDB_FS]	= 0xFFFF;
	gdb_regs[GDB_GS]	= 0xFFFF;
	if (user_mode_vm(regs)) {
		gdb_regs[GDB_SS] = regs->ss;
		gdb_regs[GDB_SP] = regs->sp;
	} else {
		gdb_regs[GDB_SS] = __KERNEL_DS;
		gdb_regs[GDB_SP] = kernel_stack_pointer(regs);
	}
#else
	gdb_regs[GDB_R8]	= regs->r8;
	gdb_regs[GDB_R9]	= regs->r9;
	gdb_regs[GDB_R10]	= regs->r10;
	gdb_regs[GDB_R11]	= regs->r11;
	gdb_regs[GDB_R12]	= regs->r12;
	gdb_regs[GDB_R13]	= regs->r13;
	gdb_regs[GDB_R14]	= regs->r14;
	gdb_regs[GDB_R15]	= regs->r15;
	gdb_regs32[GDB_PS]	= regs->flags;
	gdb_regs32[GDB_CS]	= regs->cs;
	gdb_regs32[GDB_SS]	= regs->ss;
	gdb_regs[GDB_SP]	= kernel_stack_pointer(regs);
#endif
}

/**
 *	sleeping_thread_to_gdb_regs - Convert ptrace regs to GDB regs
 *	@gdb_regs: A pointer to hold the registers in the order GDB wants.
 *	@p: The &struct task_struct of the desired process.
 *
 *	Convert the register values of the sleeping process in @p to
 *	the format that GDB expects.
 *	This function is called when kgdb does not have access to the
 *	&struct pt_regs and therefore it should fill the gdb registers
 *	@gdb_regs with what has	been saved in &struct thread_struct
 *	thread field during switch_to.
 */
void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
#ifndef CONFIG_X86_32
	u32 *gdb_regs32 = (u32 *)gdb_regs;
#endif
	gdb_regs[GDB_AX]	= 0;
	gdb_regs[GDB_BX]	= 0;
	gdb_regs[GDB_CX]	= 0;
	gdb_regs[GDB_DX]	= 0;
	gdb_regs[GDB_SI]	= 0;
	gdb_regs[GDB_DI]	= 0;
	gdb_regs[GDB_BP]	= *(unsigned long *)p->thread.sp;
#ifdef CONFIG_X86_32
	gdb_regs[GDB_DS]	= __KERNEL_DS;
	gdb_regs[GDB_ES]	= __KERNEL_DS;
	gdb_regs[GDB_PS]	= 0;
	gdb_regs[GDB_CS]	= __KERNEL_CS;
	gdb_regs[GDB_PC]	= p->thread.ip;
	gdb_regs[GDB_SS]	= __KERNEL_DS;
	gdb_regs[GDB_FS]	= 0xFFFF;
	gdb_regs[GDB_GS]	= 0xFFFF;
#else
	gdb_regs32[GDB_PS]	= *(unsigned long *)(p->thread.sp + 8);
	gdb_regs32[GDB_CS]	= __KERNEL_CS;
	gdb_regs32[GDB_SS]	= __KERNEL_DS;
	gdb_regs[GDB_PC]	= 0;
	gdb_regs[GDB_R8]	= 0;
	gdb_regs[GDB_R9]	= 0;
	gdb_regs[GDB_R10]	= 0;
	gdb_regs[GDB_R11]	= 0;
	gdb_regs[GDB_R12]	= 0;
	gdb_regs[GDB_R13]	= 0;
	gdb_regs[GDB_R14]	= 0;
	gdb_regs[GDB_R15]	= 0;
#endif
	gdb_regs[GDB_SP]	= p->thread.sp;
}

/**
 *	gdb_regs_to_pt_regs - Convert GDB regs to ptrace regs.
 *	@gdb_regs: A pointer to hold the registers we've received from GDB.
 *	@regs: A pointer to a &struct pt_regs to hold these values in.
 *
 *	Convert the GDB regs in @gdb_regs into the pt_regs, and store them
 *	in @regs.
 */
void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
#ifndef CONFIG_X86_32
	u32 *gdb_regs32 = (u32 *)gdb_regs;
#endif
	regs->ax		= gdb_regs[GDB_AX];
	regs->bx		= gdb_regs[GDB_BX];
	regs->cx		= gdb_regs[GDB_CX];
	regs->dx		= gdb_regs[GDB_DX];
	regs->si		= gdb_regs[GDB_SI];
	regs->di		= gdb_regs[GDB_DI];
	regs->bp		= gdb_regs[GDB_BP];
	regs->ip		= gdb_regs[GDB_PC];
#ifdef CONFIG_X86_32
	regs->flags		= gdb_regs[GDB_PS];
	regs->ds		= gdb_regs[GDB_DS];
	regs->es		= gdb_regs[GDB_ES];
	regs->cs		= gdb_regs[GDB_CS];
#else
	regs->r8		= gdb_regs[GDB_R8];
	regs->r9		= gdb_regs[GDB_R9];
	regs->r10		= gdb_regs[GDB_R10];
	regs->r11		= gdb_regs[GDB_R11];
	regs->r12		= gdb_regs[GDB_R12];
	regs->r13		= gdb_regs[GDB_R13];
	regs->r14		= gdb_regs[GDB_R14];
	regs->r15		= gdb_regs[GDB_R15];
	regs->flags		= gdb_regs32[GDB_PS];
	regs->cs		= gdb_regs32[GDB_CS];
	regs->ss		= gdb_regs32[GDB_SS];
#endif
}

static struct hw_breakpoint {
	unsigned		enabled;
	unsigned long		addr;
	int			len;
	int			type;
	struct perf_event	**pev;
} breakinfo[4];

static unsigned long early_dr7;

static void kgdb_correct_hw_break(void)
{
	int breakno;

	for (breakno = 0; breakno < 4; breakno++) {
		struct perf_event *bp;
		struct arch_hw_breakpoint *info;
		int val;
		int cpu = raw_smp_processor_id();
		if (!breakinfo[breakno].enabled)
			continue;
		if (dbg_is_early) {
			set_debugreg(breakinfo[breakno].addr, breakno);
			early_dr7 |= encode_dr7(breakno,
						breakinfo[breakno].len,
						breakinfo[breakno].type);
			set_debugreg(early_dr7, 7);
			continue;
		}
		bp = *per_cpu_ptr(breakinfo[breakno].pev, cpu);
		info = counter_arch_bp(bp);
		if (bp->attr.disabled != 1)
			continue;
		bp->attr.bp_addr = breakinfo[breakno].addr;
		bp->attr.bp_len = breakinfo[breakno].len;
		bp->attr.bp_type = breakinfo[breakno].type;
		info->address = breakinfo[breakno].addr;
		info->len = breakinfo[breakno].len;
		info->type = breakinfo[breakno].type;
		val = arch_install_hw_breakpoint(bp);
		if (!val)
			bp->attr.disabled = 0;
	}
	if (!dbg_is_early)
		hw_breakpoint_restore();
}

static int hw_break_reserve_slot(int breakno)
{
	int cpu;
	int cnt = 0;
	struct perf_event **pevent;

	if (dbg_is_early)
		return 0;

	for_each_online_cpu(cpu) {
		cnt++;
		pevent = per_cpu_ptr(breakinfo[breakno].pev, cpu);
		if (dbg_reserve_bp_slot(*pevent))
			goto fail;
	}

	return 0;

fail:
	for_each_online_cpu(cpu) {
		cnt--;
		if (!cnt)
			break;
		pevent = per_cpu_ptr(breakinfo[breakno].pev, cpu);
		dbg_release_bp_slot(*pevent);
	}
	return -1;
}

static int hw_break_release_slot(int breakno)
{
	struct perf_event **pevent;
	int cpu;

	if (dbg_is_early)
		return 0;

	for_each_online_cpu(cpu) {
		pevent = per_cpu_ptr(breakinfo[breakno].pev, cpu);
		if (dbg_release_bp_slot(*pevent))
			/*
			 * The debugger is responisble for handing the retry on
			 * remove failure.
			 */
			return -1;
	}
	return 0;
}

static int
kgdb_remove_hw_break(unsigned long addr, int len, enum kgdb_bptype bptype)
{
	int i;

	for (i = 0; i < 4; i++)
		if (breakinfo[i].addr == addr && breakinfo[i].enabled)
			break;
	if (i == 4)
		return -1;

	if (hw_break_release_slot(i)) {
		printk(KERN_ERR "Cannot remove hw breakpoint at %lx\n", addr);
		return -1;
	}
	breakinfo[i].enabled = 0;

	return 0;
}

static void kgdb_remove_all_hw_break(void)
{
	int i;
	int cpu = raw_smp_processor_id();
	struct perf_event *bp;

	for (i = 0; i < 4; i++) {
		if (!breakinfo[i].enabled)
			continue;
		bp = *per_cpu_ptr(breakinfo[i].pev, cpu);
		if (bp->attr.disabled == 1)
			continue;
		if (dbg_is_early)
			early_dr7 &= ~encode_dr7(i, breakinfo[i].len,
						 breakinfo[i].type);
		else
			arch_uninstall_hw_breakpoint(bp);
		bp->attr.disabled = 1;
	}
}

static int
kgdb_set_hw_break(unsigned long addr, int len, enum kgdb_bptype bptype)
{
	int i;

	for (i = 0; i < 4; i++)
		if (!breakinfo[i].enabled)
			break;
	if (i == 4)
		return -1;

	switch (bptype) {
	case BP_HARDWARE_BREAKPOINT:
		len = 1;
		breakinfo[i].type = X86_BREAKPOINT_EXECUTE;
		break;
	case BP_WRITE_WATCHPOINT:
		breakinfo[i].type = X86_BREAKPOINT_WRITE;
		break;
	case BP_ACCESS_WATCHPOINT:
		breakinfo[i].type = X86_BREAKPOINT_RW;
		break;
	default:
		return -1;
	}
	switch (len) {
	case 1:
		breakinfo[i].len = X86_BREAKPOINT_LEN_1;
		break;
	case 2:
		breakinfo[i].len = X86_BREAKPOINT_LEN_2;
		break;
	case 4:
		breakinfo[i].len = X86_BREAKPOINT_LEN_4;
		break;
#ifdef CONFIG_X86_64
	case 8:
		breakinfo[i].len = X86_BREAKPOINT_LEN_8;
		break;
#endif
	default:
		return -1;
	}
	breakinfo[i].addr = addr;
	if (hw_break_reserve_slot(i)) {
		breakinfo[i].addr = 0;
		return -1;
	}
	breakinfo[i].enabled = 1;

	return 0;
}

/**
 *	kgdb_disable_hw_debug - Disable hardware debugging while we in kgdb.
 *	@regs: Current &struct pt_regs.
 *
 *	This function will be called if the particular architecture must
 *	disable hardware debugging while it is processing gdb packets or
 *	handling exception.
 */
void kgdb_disable_hw_debug(struct pt_regs *regs)
{
	int i;
	int cpu = raw_smp_processor_id();
	struct perf_event *bp;

	/* Disable hardware debugging while we are in kgdb: */
	set_debugreg(0UL, 7);
	for (i = 0; i < 4; i++) {
		if (!breakinfo[i].enabled)
			continue;
		if (dbg_is_early) {
			early_dr7 &= ~encode_dr7(i, breakinfo[i].len,
						 breakinfo[i].type);
			continue;
		}
		bp = *per_cpu_ptr(breakinfo[i].pev, cpu);
		if (bp->attr.disabled == 1)
			continue;
		arch_uninstall_hw_breakpoint(bp);
		bp->attr.disabled = 1;
	}
}

#ifdef CONFIG_SMP
/**
 *	kgdb_roundup_cpus - Get other CPUs into a holding pattern
 *	@flags: Current IRQ state
 *
 *	On SMP systems, we need to get the attention of the other CPUs
 *	and get them be in a known state.  This should do what is needed
 *	to get the other CPUs to call kgdb_wait(). Note that on some arches,
 *	the NMI approach is not used for rounding up all the CPUs. For example,
 *	in case of MIPS, smp_call_function() is used to roundup CPUs. In
 *	this case, we have to make sure that interrupts are enabled before
 *	calling smp_call_function(). The argument to this function is
 *	the flags that will be used when restoring the interrupts. There is
 *	local_irq_save() call before kgdb_roundup_cpus().
 *
 *	On non-SMP systems, this is not called.
 */
void kgdb_roundup_cpus(unsigned long flags)
{
	apic->send_IPI_allbutself(APIC_DM_NMI);
}
#endif

/**
 *	kgdb_arch_handle_exception - Handle architecture specific GDB packets.
 *	@vector: The error vector of the exception that happened.
 *	@signo: The signal number of the exception that happened.
 *	@err_code: The error code of the exception that happened.
 *	@remcom_in_buffer: The buffer of the packet we have read.
 *	@remcom_out_buffer: The buffer of %BUFMAX bytes to write a packet into.
 *	@regs: The &struct pt_regs of the current process.
 *
 *	This function MUST handle the 'c' and 's' command packets,
 *	as well packets to set / remove a hardware breakpoint, if used.
 *	If there are additional packets which the hardware needs to handle,
 *	they are handled here.  The code should return -1 if it wants to
 *	process more packets, and a %0 or %1 if it wants to exit from the
 *	kgdb callback.
 */
int kgdb_arch_handle_exception(int e_vector, int signo, int err_code,
			       char *remcomInBuffer, char *remcomOutBuffer,
			       struct pt_regs *linux_regs)
{
	unsigned long addr;
	char *ptr;
	int newPC;

	switch (remcomInBuffer[0]) {
	case 'c':
	case 's':
		/* try to read optional parameter, pc unchanged if no parm */
		ptr = &remcomInBuffer[1];
		if (kgdb_hex2long(&ptr, &addr))
			linux_regs->ip = addr;
	case 'D':
	case 'k':
		newPC = linux_regs->ip;

		/* clear the trace bit */
		linux_regs->flags &= ~X86_EFLAGS_TF;
		atomic_set(&kgdb_cpu_doing_single_step, -1);

		/* set the trace bit if we're stepping */
		if (remcomInBuffer[0] == 's') {
			linux_regs->flags |= X86_EFLAGS_TF;
			atomic_set(&kgdb_cpu_doing_single_step,
				   raw_smp_processor_id());
		}

		kgdb_correct_hw_break();

		return 0;
	}

	/* this means that we do not want to exit from the handler: */
	return -1;
}

static inline int
single_step_cont(struct pt_regs *regs, struct die_args *args)
{
	/*
	 * Single step exception from kernel space to user space so
	 * eat the exception and continue the process:
	 */
	printk(KERN_ERR "KGDB: trap/step from kernel to user space, "
			"resuming...\n");
	kgdb_arch_handle_exception(args->trapnr, args->signr,
				   args->err, "c", "", regs);
	/*
	 * Reset the BS bit in dr6 (pointed by args->err) to
	 * denote completion of processing
	 */
	(*(unsigned long *)ERR_PTR(args->err)) &= ~DR_STEP;

	return NOTIFY_STOP;
}

static int was_in_debug_nmi[NR_CPUS];

static int __kgdb_notify(struct die_args *args, unsigned long cmd)
{
	struct pt_regs *regs = args->regs;

	switch (cmd) {
	case DIE_NMI:
		if (atomic_read(&kgdb_active) != -1) {
			/* KGDB CPU roundup */
			kgdb_nmicallback(raw_smp_processor_id(), regs);
			was_in_debug_nmi[raw_smp_processor_id()] = 1;
			touch_nmi_watchdog();
			return NOTIFY_STOP;
		}
		return NOTIFY_DONE;

	case DIE_NMI_IPI:
		/* Just ignore, we will handle the roundup on DIE_NMI. */
		return NOTIFY_DONE;

	case DIE_NMIUNKNOWN:
		if (was_in_debug_nmi[raw_smp_processor_id()]) {
			was_in_debug_nmi[raw_smp_processor_id()] = 0;
			return NOTIFY_STOP;
		}
		return NOTIFY_DONE;

	case DIE_NMIWATCHDOG:
		if (atomic_read(&kgdb_active) != -1) {
			/* KGDB CPU roundup: */
			kgdb_nmicallback(raw_smp_processor_id(), regs);
			return NOTIFY_STOP;
		}
		/* Enter debugger: */
		break;

	case DIE_DEBUG:
		if (atomic_read(&kgdb_cpu_doing_single_step) != -1) {
			if (user_mode(regs))
				return single_step_cont(regs, args);
			break;
		} else if (test_thread_flag(TIF_SINGLESTEP))
			/* This means a user thread is single stepping
			 * a system call which should be ignored
			 */
			return NOTIFY_DONE;
		/* fall through */
	default:
		if (user_mode(regs))
			return NOTIFY_DONE;
	}

	if (kgdb_handle_exception(args->trapnr, args->signr, cmd, regs))
		return NOTIFY_DONE;

	/* Must touch watchdog before return to normal operation */
	touch_nmi_watchdog();
	return NOTIFY_STOP;
}

int kgdb_ll_trap(int cmd, const char *str,
		 struct pt_regs *regs, long err, int trap, int sig)
{
	struct die_args args = {
		.regs	= regs,
		.str	= str,
		.err	= err,
		.trapnr	= trap,
		.signr	= sig,

	};

	if (!kgdb_io_module_registered)
		return NOTIFY_DONE;

	return __kgdb_notify(&args, cmd);
}

static int
kgdb_notify(struct notifier_block *self, unsigned long cmd, void *ptr)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret = __kgdb_notify(ptr, cmd);
	local_irq_restore(flags);

	return ret;
}

static struct notifier_block kgdb_notifier = {
	.notifier_call	= kgdb_notify,

	/*
	 * Lowest-prio notifier priority, we want to be notified last:
	 */
	.priority	= -INT_MAX,
};

/**
 *	kgdb_arch_init - Perform any architecture specific initalization.
 *
 *	This function will handle the initalization of any architecture
 *	specific callbacks.
 */
int kgdb_arch_init(void)
{
	return register_die_notifier(&kgdb_notifier);
}

static void kgdb_hw_overflow_handler(struct perf_event *event, int nmi,
		struct perf_sample_data *data, struct pt_regs *regs)
{
	kgdb_ll_trap(DIE_DEBUG, "debug", regs, 0, 0, SIGTRAP);
}

void kgdb_arch_late(void)
{
	int i, cpu;
	struct perf_event_attr attr;
	struct perf_event **pevent;

	/*
	 * Pre-allocate the hw breakpoint structions in the non-atomic
	 * portion of kgdb because this operation requires mutexs to
	 * complete.
	 */
	hw_breakpoint_init(&attr);
	attr.bp_addr = (unsigned long)kgdb_arch_init;
	attr.bp_len = HW_BREAKPOINT_LEN_1;
	attr.bp_type = HW_BREAKPOINT_W;
	attr.disabled = 1;
	for (i = 0; i < 4; i++) {
		if (breakinfo[i].pev)
			continue;
		breakinfo[i].pev = register_wide_hw_breakpoint(&attr, NULL);
		if (IS_ERR(breakinfo[i].pev)) {
			printk(KERN_ERR "kgdb: Could not allocate hw"
			       "breakpoints\nDisabling the kernel debugger\n");
			breakinfo[i].pev = NULL;
			kgdb_arch_exit();
			return;
		}
		for_each_online_cpu(cpu) {
			pevent = per_cpu_ptr(breakinfo[i].pev, cpu);
			pevent[0]->hw.sample_period = 1;
			pevent[0]->overflow_handler = kgdb_hw_overflow_handler;
			if (pevent[0]->destroy != NULL) {
				pevent[0]->destroy = NULL;
				release_bp_slot(*pevent);
			}
		}
	}
}

/**
 *	kgdb_arch_exit - Perform any architecture specific uninitalization.
 *
 *	This function will handle the uninitalization of any architecture
 *	specific callbacks, for dynamic registration and unregistration.
 */
void kgdb_arch_exit(void)
{
	int i;
	for (i = 0; i < 4; i++) {
		if (breakinfo[i].pev) {
			unregister_wide_hw_breakpoint(breakinfo[i].pev);
			breakinfo[i].pev = NULL;
		}
	}
	unregister_die_notifier(&kgdb_notifier);
}

/**
 *
 *	kgdb_skipexception - Bail out of KGDB when we've been triggered.
 *	@exception: Exception vector number
 *	@regs: Current &struct pt_regs.
 *
 *	On some architectures we need to skip a breakpoint exception when
 *	it occurs after a breakpoint has been removed.
 *
 * Skip an int3 exception when it occurs after a breakpoint has been
 * removed. Backtrack eip by 1 since the int3 would have caused it to
 * increment by 1.
 */
int kgdb_skipexception(int exception, struct pt_regs *regs)
{
	if (exception == 3 && kgdb_isremovedbreak(regs->ip - 1)) {
		regs->ip -= 1;
		return 1;
	}
	return 0;
}

unsigned long kgdb_arch_pc(int exception, struct pt_regs *regs)
{
	if (exception == 3)
		return instruction_pointer(regs) - 1;
	return instruction_pointer(regs);
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long ip)
{
	regs->ip = ip;
}

struct kgdb_arch arch_kgdb_ops = {
	/* Breakpoint instruction: */
	.gdb_bpt_instr		= { 0xcc },
	.flags			= KGDB_HW_BREAKPOINT,
	.set_hw_breakpoint	= kgdb_set_hw_break,
	.remove_hw_breakpoint	= kgdb_remove_hw_break,
	.remove_all_hw_break	= kgdb_remove_all_hw_break,
	.correct_hw_break	= kgdb_correct_hw_break,
};
