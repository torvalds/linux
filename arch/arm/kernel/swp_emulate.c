// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/kernel/swp_emulate.c
 *
 *  Copyright (C) 2009 ARM Limited
 *  __user_* functions adapted from include/asm/uaccess.h
 *
 *  Implements emulation of the SWP/SWPB instructions using load-exclusive and
 *  store-exclusive for processors that have them disabled (or future ones that
 *  might not implement them).
 *
 *  Syntax of SWP{B} instruction: SWP{B}<c> <Rt>, <Rt2>, [<Rn>]
 *  Where: Rt  = destination
 *	   Rt2 = source
 *	   Rn  = address
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/syscalls.h>
#include <linux/perf_event.h>

#include <asm/opcodes.h>
#include <asm/system_info.h>
#include <asm/traps.h>
#include <linux/uaccess.h>

/*
 * Error-checking SWP macros implemented using ldrex{b}/strex{b}
 */
#define __user_swpX_asm(data, addr, res, temp, B)		\
	__asm__ __volatile__(					\
	".arch armv7-a\n"					\
	"0:	ldrex"B"	%2, [%3]\n"			\
	"1:	strex"B"	%0, %1, [%3]\n"			\
	"	cmp		%0, #0\n"			\
	"	moveq		%1, %2\n"			\
	"	movne		%0, %4\n"			\
	"2:\n"							\
	"	.section	 .text.fixup,\"ax\"\n"		\
	"	.align		2\n"				\
	"3:	mov		%0, %5\n"			\
	"	b		2b\n"				\
	"	.previous\n"					\
	"	.section	 __ex_table,\"a\"\n"		\
	"	.align		3\n"				\
	"	.long		0b, 3b\n"			\
	"	.long		1b, 3b\n"			\
	"	.previous"					\
	: "=&r" (res), "+r" (data), "=&r" (temp)		\
	: "r" (addr), "i" (-EAGAIN), "i" (-EFAULT)		\
	: "cc", "memory")

#define __user_swp_asm(data, addr, res, temp) \
	__user_swpX_asm(data, addr, res, temp, "")
#define __user_swpb_asm(data, addr, res, temp) \
	__user_swpX_asm(data, addr, res, temp, "b")

/*
 * Macros/defines for extracting register numbers from instruction.
 */
#define EXTRACT_REG_NUM(instruction, offset) \
	(((instruction) & (0xf << (offset))) >> (offset))
#define RN_OFFSET  16
#define RT_OFFSET  12
#define RT2_OFFSET  0
/*
 * Bit 22 of the instruction encoding distinguishes between
 * the SWP and SWPB variants (bit set means SWPB).
 */
#define TYPE_SWPB (1 << 22)

static unsigned long swpcounter;
static unsigned long swpbcounter;
static unsigned long abtcounter;
static pid_t         previous_pid;

#ifdef CONFIG_PROC_FS
static int proc_status_show(struct seq_file *m, void *v)
{
	seq_printf(m, "Emulated SWP:\t\t%lu\n", swpcounter);
	seq_printf(m, "Emulated SWPB:\t\t%lu\n", swpbcounter);
	seq_printf(m, "Aborted SWP{B}:\t\t%lu\n", abtcounter);
	if (previous_pid != 0)
		seq_printf(m, "Last process:\t\t%d\n", previous_pid);
	return 0;
}
#endif

/*
 * Set up process info to signal segmentation fault - called on access error.
 */
static void set_segfault(struct pt_regs *regs, unsigned long addr)
{
	int si_code;

	mmap_read_lock(current->mm);
	if (find_vma(current->mm, addr) == NULL)
		si_code = SEGV_MAPERR;
	else
		si_code = SEGV_ACCERR;
	mmap_read_unlock(current->mm);

	pr_debug("SWP{B} emulation: access caused memory abort!\n");
	arm_notify_die("Illegal memory access", regs,
		       SIGSEGV, si_code,
		       (void __user *)instruction_pointer(regs),
		       0, 0);

	abtcounter++;
}

static int emulate_swpX(unsigned int address, unsigned int *data,
			unsigned int type)
{
	unsigned int res = 0;

	if ((type != TYPE_SWPB) && (address & 0x3)) {
		/* SWP to unaligned address not permitted */
		pr_debug("SWP instruction on unaligned pointer!\n");
		return -EFAULT;
	}

	while (1) {
		unsigned long temp;
		unsigned int __ua_flags;

		__ua_flags = uaccess_save_and_enable();
		if (type == TYPE_SWPB)
			__user_swpb_asm(*data, address, res, temp);
		else
			__user_swp_asm(*data, address, res, temp);
		uaccess_restore(__ua_flags);

		if (likely(res != -EAGAIN) || signal_pending(current))
			break;

		cond_resched();
	}

	if (res == 0) {
		if (type == TYPE_SWPB)
			swpbcounter++;
		else
			swpcounter++;
	}

	return res;
}

/*
 * swp_handler logs the id of calling process, dissects the instruction, sanity
 * checks the memory location, calls emulate_swpX for the actual operation and
 * deals with fixup/error handling before returning
 */
static int swp_handler(struct pt_regs *regs, unsigned int instr)
{
	unsigned int address, destreg, data, type;
	unsigned int res = 0;

	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, regs->ARM_pc);

	res = arm_check_condition(instr, regs->ARM_cpsr);
	switch (res) {
	case ARM_OPCODE_CONDTEST_PASS:
		break;
	case ARM_OPCODE_CONDTEST_FAIL:
		/* Condition failed - return to next instruction */
		regs->ARM_pc += 4;
		return 0;
	case ARM_OPCODE_CONDTEST_UNCOND:
		/* If unconditional encoding - not a SWP, undef */
		return -EFAULT;
	default:
		return -EINVAL;
	}

	if (current->pid != previous_pid) {
		pr_debug("\"%s\" (%ld) uses deprecated SWP{B} instruction\n",
			 current->comm, (unsigned long)current->pid);
		previous_pid = current->pid;
	}

	address = regs->uregs[EXTRACT_REG_NUM(instr, RN_OFFSET)];
	data	= regs->uregs[EXTRACT_REG_NUM(instr, RT2_OFFSET)];
	destreg = EXTRACT_REG_NUM(instr, RT_OFFSET);

	type = instr & TYPE_SWPB;

	pr_debug("addr in r%d->0x%08x, dest is r%d, source in r%d->0x%08x)\n",
		 EXTRACT_REG_NUM(instr, RN_OFFSET), address,
		 destreg, EXTRACT_REG_NUM(instr, RT2_OFFSET), data);

	/* Check access in reasonable access range for both SWP and SWPB */
	if (!access_ok((void __user *)(address & ~3), 4)) {
		pr_debug("SWP{B} emulation: access to %p not allowed!\n",
			 (void *)address);
		res = -EFAULT;
	} else {
		res = emulate_swpX(address, &data, type);
	}

	if (res == 0) {
		/*
		 * On successful emulation, revert the adjustment to the PC
		 * made in kernel/traps.c in order to resume execution at the
		 * instruction following the SWP{B}.
		 */
		regs->ARM_pc += 4;
		regs->uregs[destreg] = data;
	} else if (res == -EFAULT) {
		/*
		 * Memory errors do not mean emulation failed.
		 * Set up signal info to return SEGV, then return OK
		 */
		set_segfault(regs, address);
	}

	return 0;
}

/*
 * Only emulate SWP/SWPB executed in ARM state/User mode.
 * The kernel must be SWP free and SWP{B} does not exist in Thumb/ThumbEE.
 */
static struct undef_hook swp_hook = {
	.instr_mask = 0x0fb00ff0,
	.instr_val  = 0x01000090,
	.cpsr_mask  = MODE_MASK | PSR_T_BIT | PSR_J_BIT,
	.cpsr_val   = USR_MODE,
	.fn	    = swp_handler
};

/*
 * Register handler and create status file in /proc/cpu
 * Invoked as late_initcall, since not needed before init spawned.
 */
static int __init swp_emulation_init(void)
{
	if (cpu_architecture() < CPU_ARCH_ARMv7)
		return 0;

#ifdef CONFIG_PROC_FS
	if (!proc_create_single("cpu/swp_emulation", S_IRUGO, NULL,
			proc_status_show))
		return -ENOMEM;
#endif /* CONFIG_PROC_FS */

	pr_notice("Registering SWP/SWPB emulation handler\n");
	register_undef_hook(&swp_hook);

	return 0;
}

late_initcall(swp_emulation_init);
