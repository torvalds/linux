// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>

static int align_enable = 1;
static int align_count;

static inline uint32_t get_ptreg(struct pt_regs *regs, uint32_t rx)
{
	return rx == 15 ? regs->lr : *((uint32_t *)&(regs->a0) - 2 + rx);
}

static inline void put_ptreg(struct pt_regs *regs, uint32_t rx, uint32_t val)
{
	if (rx == 15)
		regs->lr = val;
	else
		*((uint32_t *)&(regs->a0) - 2 + rx) = val;
}

/*
 * Get byte-value from addr and set it to *valp.
 *
 * Success: return 0
 * Failure: return 1
 */
static int ldb_asm(uint32_t addr, uint32_t *valp)
{
	uint32_t val;
	int err;

	if (!access_ok(VERIFY_READ, (void *)addr, 1))
		return 1;

	asm volatile (
		"movi	%0, 0\n"
		"1:\n"
		"ldb	%1, (%2)\n"
		"br	3f\n"
		"2:\n"
		"movi	%0, 1\n"
		"br	3f\n"
		".section __ex_table,\"a\"\n"
		".align 2\n"
		".long	1b, 2b\n"
		".previous\n"
		"3:\n"
		: "=&r"(err), "=r"(val)
		: "r" (addr)
	);

	*valp = val;

	return err;
}

/*
 * Put byte-value to addr.
 *
 * Success: return 0
 * Failure: return 1
 */
static int stb_asm(uint32_t addr, uint32_t val)
{
	int err;

	if (!access_ok(VERIFY_WRITE, (void *)addr, 1))
		return 1;

	asm volatile (
		"movi	%0, 0\n"
		"1:\n"
		"stb	%1, (%2)\n"
		"br	3f\n"
		"2:\n"
		"movi	%0, 1\n"
		"br	3f\n"
		".section __ex_table,\"a\"\n"
		".align 2\n"
		".long	1b, 2b\n"
		".previous\n"
		"3:\n"
		: "=&r"(err)
		: "r"(val), "r" (addr)
	);

	return err;
}

/*
 * Get half-word from [rx + imm]
 *
 * Success: return 0
 * Failure: return 1
 */
static int ldh_c(struct pt_regs *regs, uint32_t rz, uint32_t addr)
{
	uint32_t byte0, byte1;

	if (ldb_asm(addr, &byte0))
		return 1;
	addr += 1;
	if (ldb_asm(addr, &byte1))
		return 1;

	byte0 |= byte1 << 8;
	put_ptreg(regs, rz, byte0);

	return 0;
}

/*
 * Store half-word to [rx + imm]
 *
 * Success: return 0
 * Failure: return 1
 */
static int sth_c(struct pt_regs *regs, uint32_t rz, uint32_t addr)
{
	uint32_t byte0, byte1;

	byte0 = byte1 = get_ptreg(regs, rz);

	byte0 &= 0xff;

	if (stb_asm(addr, byte0))
		return 1;

	addr += 1;
	byte1 = (byte1 >> 8) & 0xff;
	if (stb_asm(addr, byte1))
		return 1;

	return 0;
}

/*
 * Get word from [rx + imm]
 *
 * Success: return 0
 * Failure: return 1
 */
static int ldw_c(struct pt_regs *regs, uint32_t rz, uint32_t addr)
{
	uint32_t byte0, byte1, byte2, byte3;

	if (ldb_asm(addr, &byte0))
		return 1;

	addr += 1;
	if (ldb_asm(addr, &byte1))
		return 1;

	addr += 1;
	if (ldb_asm(addr, &byte2))
		return 1;

	addr += 1;
	if (ldb_asm(addr, &byte3))
		return 1;

	byte0 |= byte1 << 8;
	byte0 |= byte2 << 16;
	byte0 |= byte3 << 24;

	put_ptreg(regs, rz, byte0);

	return 0;
}

/*
 * Store word to [rx + imm]
 *
 * Success: return 0
 * Failure: return 1
 */
static int stw_c(struct pt_regs *regs, uint32_t rz, uint32_t addr)
{
	uint32_t byte0, byte1, byte2, byte3;

	byte0 = byte1 = byte2 = byte3 = get_ptreg(regs, rz);

	byte0 &= 0xff;

	if (stb_asm(addr, byte0))
		return 1;

	addr += 1;
	byte1 = (byte1 >> 8) & 0xff;
	if (stb_asm(addr, byte1))
		return 1;

	addr += 1;
	byte2 = (byte2 >> 16) & 0xff;
	if (stb_asm(addr, byte2))
		return 1;

	addr += 1;
	byte3 = (byte3 >> 24) & 0xff;
	if (stb_asm(addr, byte3))
		return 1;

	align_count++;

	return 0;
}

extern int fixup_exception(struct pt_regs *regs);

#define OP_LDH 0xc000
#define OP_STH 0xd000
#define OP_LDW 0x8000
#define OP_STW 0x9000

void csky_alignment(struct pt_regs *regs)
{
	int ret;
	uint16_t tmp;
	uint32_t opcode = 0;
	uint32_t rx     = 0;
	uint32_t rz     = 0;
	uint32_t imm    = 0;
	uint32_t addr   = 0;

	if (!user_mode(regs))
		goto bad_area;

	ret = get_user(tmp, (uint16_t *)instruction_pointer(regs));
	if (ret) {
		pr_err("%s get_user failed.\n", __func__);
		goto bad_area;
	}

	opcode = (uint32_t)tmp;

	rx  = opcode & 0xf;
	imm = (opcode >> 4) & 0xf;
	rz  = (opcode >> 8) & 0xf;
	opcode &= 0xf000;

	if (rx == 0 || rx == 1 || rz == 0 || rz == 1)
		goto bad_area;

	switch (opcode) {
	case OP_LDH:
		addr = get_ptreg(regs, rx) + (imm << 1);
		ret = ldh_c(regs, rz, addr);
		break;
	case OP_LDW:
		addr = get_ptreg(regs, rx) + (imm << 2);
		ret = ldw_c(regs, rz, addr);
		break;
	case OP_STH:
		addr = get_ptreg(regs, rx) + (imm << 1);
		ret = sth_c(regs, rz, addr);
		break;
	case OP_STW:
		addr = get_ptreg(regs, rx) + (imm << 2);
		ret = stw_c(regs, rz, addr);
		break;
	}

	if (ret)
		goto bad_area;

	regs->pc += 2;

	return;

bad_area:
	if (!user_mode(regs)) {
		if (fixup_exception(regs))
			return;

		bust_spinlocks(1);
		pr_alert("%s opcode: %x, rz: %d, rx: %d, imm: %d, addr: %x.\n",
				__func__, opcode, rz, rx, imm, addr);
		show_regs(regs);
		bust_spinlocks(0);
		do_exit(SIGKILL);
	}

	force_sig_fault(SIGBUS, BUS_ADRALN, (void __user *)addr, current);
}

static struct ctl_table alignment_tbl[4] = {
	{
		.procname = "enable",
		.data = &align_enable,
		.maxlen = sizeof(align_enable),
		.mode = 0666,
		.proc_handler = &proc_dointvec
	},
	{
		.procname = "count",
		.data = &align_count,
		.maxlen = sizeof(align_count),
		.mode = 0666,
		.proc_handler = &proc_dointvec
	},
	{}
};

static struct ctl_table sysctl_table[2] = {
	{
	 .procname = "csky_alignment",
	 .mode = 0555,
	 .child = alignment_tbl},
	{}
};

static struct ctl_path sysctl_path[2] = {
	{.procname = "csky"},
	{}
};

static int __init csky_alignment_init(void)
{
	register_sysctl_paths(sysctl_path, sysctl_table);
	return 0;
}

arch_initcall(csky_alignment_init);
