/*
 *  Derived from from linux/arch/arm/kernel/swp_emulate.c
 *
 *  Copyright (C) 2009 ARM Limited
 *  Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include <linux/syscalls.h>
#include <linux/perf_event.h>

#include <asm/opcodes.h>
#include <asm/traps.h>
#include <asm/uaccess.h>
#include <asm/system_misc.h>
#include <linux/debugfs.h>

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

static pid_t previous_pid;

u64 swpb_count = 0;
u64 swp_count = 0;

/*
 * swp_handler logs the id of calling process, dissects the instruction, sanity
 * checks the memory location, calls emulate_swpX for the actual operation and
 * deals with fixup/error handling before returning
 */
static int swp_handler(struct pt_regs *regs, unsigned int instr)
{
	u32 address_reg, destreg, data, type;
	uintptr_t address;
	unsigned int res = 0;
	u32 temp32;
	u8 temp8;

	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, regs->pc);

	res = arm_check_condition(instr, regs->pstate);
	switch (res) {
	case ARM_OPCODE_CONDTEST_PASS:
		break;
	case ARM_OPCODE_CONDTEST_FAIL:
		/* Condition failed - return to next instruction */
		regs->pc += 4;
		return 0;
	case ARM_OPCODE_CONDTEST_UNCOND:
		/* If unconditional encoding - not a SWP, undef */
		return -EFAULT;
	default:
		return -EINVAL;
	}

	if (current->pid != previous_pid) {
		pr_warn("\"%s\" (%ld) uses obsolete SWP{B} instruction\n",
			 current->comm, (unsigned long)current->pid);
		previous_pid = current->pid;
	}

	address = regs->regs[EXTRACT_REG_NUM(instr, RN_OFFSET)] & 0xffffffff;
	data = regs->regs[EXTRACT_REG_NUM(instr, RT2_OFFSET)];
	destreg = EXTRACT_REG_NUM(instr, RT_OFFSET);

	type = instr & TYPE_SWPB;

	/* Check access in reasonable access range for both SWP and SWPB */
	if (!access_ok(VERIFY_WRITE, (address & ~3), 4)) {
		pr_debug("SWP{B} emulation: access to %p not allowed!\n",
			 (void *)address);
		res = -EFAULT;
	}
	if (type == TYPE_SWPB) {
		do {
			temp8 = ldax8((u8 *) address);
		} while (stx8((u8 *) address, (u8) data));
		regs->regs[destreg] = temp8;
		regs->pc += 4;
		swpb_count++;
	} else if (address & 0x3) {
		/* SWP to unaligned address not permitted */
		pr_debug("SWP instruction on unaligned pointer!\n");
		return -EFAULT;
	} else {
		do {
			temp32 = ldax32((u32 *) address);
		} while (stlx32((u32 *) address, (u32) data));
		regs->regs[destreg] = temp32;
		regs->pc += 4;
		swp_count++;
	}

	return 0;
}

/*
 * Only emulate SWP/SWPB executed in ARM state/User mode.
 * The kernel must be SWP free and SWP{B} does not exist in Thumb/ThumbEE.
 */
static struct undef_hook swp_hook = {
	.instr_mask	= 0x0fb00ff0,
	.instr_val	= 0x01000090,
	.pstate_mask	= COMPAT_PSR_MODE_MASK | COMPAT_PSR_T_BIT,
	.pstate_val	= COMPAT_PSR_MODE_USR,
	.fn		= swp_handler
};

/*
 * Register handler and create status file in /proc/cpu
 * Invoked as late_initcall, since not needed before init spawned.
 */
static int __init swp_emulation_init(void)
{
	struct dentry *dir;
	dir = debugfs_create_dir("swp_emulate", NULL);
	debugfs_create_u64("swp_count", S_IRUGO | S_IWUSR, dir, &swp_count);
	debugfs_create_u64("swpb_count", S_IRUGO | S_IWUSR, dir, &swpb_count);

	pr_notice("Registering SWP/SWPB emulation handler\n");
	register_undef_hook(&swp_hook);


	return 0;
}

late_initcall(swp_emulation_init);
