// SPDX-License-Identifier: GPL-2.0
/*
 * Handle unaligned accesses by emulation.
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1996, 1998, 1999, 2002 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/debugfs.h>
#include <linux/perf_event.h>

#include <asm/asm.h>
#include <asm/branch.h>
#include <asm/fpu.h>
#include <asm/inst.h>

#include "access-helper.h"

#ifdef CONFIG_DEBUG_FS
static u32 unaligned_instructions_user;
static u32 unaligned_instructions_kernel;
#endif

static inline unsigned long read_fpr(unsigned int idx)
{
#define READ_FPR(idx, __value)		\
	__asm__ __volatile__("movfr2gr.d %0, $f"#idx"\n\t" : "=r"(__value));

	unsigned long __value;

	switch (idx) {
	case 0:
		READ_FPR(0, __value);
		break;
	case 1:
		READ_FPR(1, __value);
		break;
	case 2:
		READ_FPR(2, __value);
		break;
	case 3:
		READ_FPR(3, __value);
		break;
	case 4:
		READ_FPR(4, __value);
		break;
	case 5:
		READ_FPR(5, __value);
		break;
	case 6:
		READ_FPR(6, __value);
		break;
	case 7:
		READ_FPR(7, __value);
		break;
	case 8:
		READ_FPR(8, __value);
		break;
	case 9:
		READ_FPR(9, __value);
		break;
	case 10:
		READ_FPR(10, __value);
		break;
	case 11:
		READ_FPR(11, __value);
		break;
	case 12:
		READ_FPR(12, __value);
		break;
	case 13:
		READ_FPR(13, __value);
		break;
	case 14:
		READ_FPR(14, __value);
		break;
	case 15:
		READ_FPR(15, __value);
		break;
	case 16:
		READ_FPR(16, __value);
		break;
	case 17:
		READ_FPR(17, __value);
		break;
	case 18:
		READ_FPR(18, __value);
		break;
	case 19:
		READ_FPR(19, __value);
		break;
	case 20:
		READ_FPR(20, __value);
		break;
	case 21:
		READ_FPR(21, __value);
		break;
	case 22:
		READ_FPR(22, __value);
		break;
	case 23:
		READ_FPR(23, __value);
		break;
	case 24:
		READ_FPR(24, __value);
		break;
	case 25:
		READ_FPR(25, __value);
		break;
	case 26:
		READ_FPR(26, __value);
		break;
	case 27:
		READ_FPR(27, __value);
		break;
	case 28:
		READ_FPR(28, __value);
		break;
	case 29:
		READ_FPR(29, __value);
		break;
	case 30:
		READ_FPR(30, __value);
		break;
	case 31:
		READ_FPR(31, __value);
		break;
	default:
		panic("unexpected idx '%d'", idx);
	}
#undef READ_FPR
	return __value;
}

static inline void write_fpr(unsigned int idx, unsigned long value)
{
#define WRITE_FPR(idx, value)		\
	__asm__ __volatile__("movgr2fr.d $f"#idx", %0\n\t" :: "r"(value));

	switch (idx) {
	case 0:
		WRITE_FPR(0, value);
		break;
	case 1:
		WRITE_FPR(1, value);
		break;
	case 2:
		WRITE_FPR(2, value);
		break;
	case 3:
		WRITE_FPR(3, value);
		break;
	case 4:
		WRITE_FPR(4, value);
		break;
	case 5:
		WRITE_FPR(5, value);
		break;
	case 6:
		WRITE_FPR(6, value);
		break;
	case 7:
		WRITE_FPR(7, value);
		break;
	case 8:
		WRITE_FPR(8, value);
		break;
	case 9:
		WRITE_FPR(9, value);
		break;
	case 10:
		WRITE_FPR(10, value);
		break;
	case 11:
		WRITE_FPR(11, value);
		break;
	case 12:
		WRITE_FPR(12, value);
		break;
	case 13:
		WRITE_FPR(13, value);
		break;
	case 14:
		WRITE_FPR(14, value);
		break;
	case 15:
		WRITE_FPR(15, value);
		break;
	case 16:
		WRITE_FPR(16, value);
		break;
	case 17:
		WRITE_FPR(17, value);
		break;
	case 18:
		WRITE_FPR(18, value);
		break;
	case 19:
		WRITE_FPR(19, value);
		break;
	case 20:
		WRITE_FPR(20, value);
		break;
	case 21:
		WRITE_FPR(21, value);
		break;
	case 22:
		WRITE_FPR(22, value);
		break;
	case 23:
		WRITE_FPR(23, value);
		break;
	case 24:
		WRITE_FPR(24, value);
		break;
	case 25:
		WRITE_FPR(25, value);
		break;
	case 26:
		WRITE_FPR(26, value);
		break;
	case 27:
		WRITE_FPR(27, value);
		break;
	case 28:
		WRITE_FPR(28, value);
		break;
	case 29:
		WRITE_FPR(29, value);
		break;
	case 30:
		WRITE_FPR(30, value);
		break;
	case 31:
		WRITE_FPR(31, value);
		break;
	default:
		panic("unexpected idx '%d'", idx);
	}
#undef WRITE_FPR
}

void emulate_load_store_insn(struct pt_regs *regs, void __user *addr, unsigned int *pc)
{
	bool fp = false;
	bool sign, write;
	bool user = user_mode(regs);
	unsigned int res, size = 0;
	unsigned long value = 0;
	union loongarch_instruction insn;

	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, 0);

	__get_inst(&insn.word, pc, user);

	switch (insn.reg2i12_format.opcode) {
	case ldh_op:
		size = 2;
		sign = true;
		write = false;
		break;
	case ldhu_op:
		size = 2;
		sign = false;
		write = false;
		break;
	case sth_op:
		size = 2;
		sign = true;
		write = true;
		break;
	case ldw_op:
		size = 4;
		sign = true;
		write = false;
		break;
	case ldwu_op:
		size = 4;
		sign = false;
		write = false;
		break;
	case stw_op:
		size = 4;
		sign = true;
		write = true;
		break;
	case ldd_op:
		size = 8;
		sign = true;
		write = false;
		break;
	case std_op:
		size = 8;
		sign = true;
		write = true;
		break;
	case flds_op:
		size = 4;
		fp = true;
		sign = true;
		write = false;
		break;
	case fsts_op:
		size = 4;
		fp = true;
		sign = true;
		write = true;
		break;
	case fldd_op:
		size = 8;
		fp = true;
		sign = true;
		write = false;
		break;
	case fstd_op:
		size = 8;
		fp = true;
		sign = true;
		write = true;
		break;
	}

	switch (insn.reg2i14_format.opcode) {
	case ldptrw_op:
		size = 4;
		sign = true;
		write = false;
		break;
	case stptrw_op:
		size = 4;
		sign = true;
		write = true;
		break;
	case ldptrd_op:
		size = 8;
		sign = true;
		write = false;
		break;
	case stptrd_op:
		size = 8;
		sign = true;
		write = true;
		break;
	}

	switch (insn.reg3_format.opcode) {
	case ldxh_op:
		size = 2;
		sign = true;
		write = false;
		break;
	case ldxhu_op:
		size = 2;
		sign = false;
		write = false;
		break;
	case stxh_op:
		size = 2;
		sign = true;
		write = true;
		break;
	case ldxw_op:
		size = 4;
		sign = true;
		write = false;
		break;
	case ldxwu_op:
		size = 4;
		sign = false;
		write = false;
		break;
	case stxw_op:
		size = 4;
		sign = true;
		write = true;
		break;
	case ldxd_op:
		size = 8;
		sign = true;
		write = false;
		break;
	case stxd_op:
		size = 8;
		sign = true;
		write = true;
		break;
	case fldxs_op:
		size = 4;
		fp = true;
		sign = true;
		write = false;
		break;
	case fstxs_op:
		size = 4;
		fp = true;
		sign = true;
		write = true;
		break;
	case fldxd_op:
		size = 8;
		fp = true;
		sign = true;
		write = false;
		break;
	case fstxd_op:
		size = 8;
		fp = true;
		sign = true;
		write = true;
		break;
	}

	if (!size)
		goto sigbus;
	if (user && !access_ok(addr, size))
		goto sigbus;

	if (!write) {
		res = unaligned_read(addr, &value, size, sign);
		if (res)
			goto fault;

		/* Rd is the same field in any formats */
		if (!fp)
			regs->regs[insn.reg3_format.rd] = value;
		else {
			if (is_fpu_owner())
				write_fpr(insn.reg3_format.rd, value);
			else
				set_fpr64(&current->thread.fpu.fpr[insn.reg3_format.rd], 0, value);
		}
	} else {
		/* Rd is the same field in any formats */
		if (!fp)
			value = regs->regs[insn.reg3_format.rd];
		else {
			if (is_fpu_owner())
				value = read_fpr(insn.reg3_format.rd);
			else
				value = get_fpr64(&current->thread.fpu.fpr[insn.reg3_format.rd], 0);
		}

		res = unaligned_write(addr, value, size);
		if (res)
			goto fault;
	}

#ifdef CONFIG_DEBUG_FS
	if (user)
		unaligned_instructions_user++;
	else
		unaligned_instructions_kernel++;
#endif

	compute_return_era(regs);

	return;

fault:
	/* Did we have an exception handler installed? */
	if (fixup_exception(regs))
		return;

	die_if_kernel("Unhandled kernel unaligned access", regs);
	force_sig(SIGSEGV);

	return;

sigbus:
	die_if_kernel("Unhandled kernel unaligned access", regs);
	force_sig(SIGBUS);

	return;
}

#ifdef CONFIG_DEBUG_FS
static int __init debugfs_unaligned(void)
{
	struct dentry *d;

	d = debugfs_create_dir("loongarch", NULL);

	debugfs_create_u32("unaligned_instructions_user",
				S_IRUGO, d, &unaligned_instructions_user);
	debugfs_create_u32("unaligned_instructions_kernel",
				S_IRUGO, d, &unaligned_instructions_kernel);

	return 0;
}
arch_initcall(debugfs_unaligned);
#endif
