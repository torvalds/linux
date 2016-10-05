/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2014 Imagination Technologies Ltd.
 * Author: Leonid Yegoshin <Leonid.Yegoshin@imgtec.com>
 * Author: Markos Chandras <markos.chandras@imgtec.com>
 *
 *      MIPS R2 user space instruction emulator for MIPS R6
 *
 */
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>

#include <asm/asm.h>
#include <asm/branch.h>
#include <asm/break.h>
#include <asm/debug.h>
#include <asm/fpu.h>
#include <asm/fpu_emulator.h>
#include <asm/inst.h>
#include <asm/mips-r2-to-r6-emul.h>
#include <asm/local.h>
#include <asm/mipsregs.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

#ifdef CONFIG_64BIT
#define ADDIU	"daddiu "
#define INS	"dins "
#define EXT	"dext "
#else
#define ADDIU	"addiu "
#define INS	"ins "
#define EXT	"ext "
#endif /* CONFIG_64BIT */

#define SB	"sb "
#define LB	"lb "
#define LL	"ll "
#define SC	"sc "

DEFINE_PER_CPU(struct mips_r2_emulator_stats, mipsr2emustats);
DEFINE_PER_CPU(struct mips_r2_emulator_stats, mipsr2bdemustats);
DEFINE_PER_CPU(struct mips_r2br_emulator_stats, mipsr2bremustats);

extern const unsigned int fpucondbit[8];

#define MIPS_R2_EMUL_TOTAL_PASS	10

int mipsr2_emulation = 0;

static int __init mipsr2emu_enable(char *s)
{
	mipsr2_emulation = 1;

	pr_info("MIPS R2-to-R6 Emulator Enabled!");

	return 1;
}
__setup("mipsr2emu", mipsr2emu_enable);

/**
 * mipsr6_emul - Emulate some frequent R2/R5/R6 instructions in delay slot
 * for performance instead of the traditional way of using a stack trampoline
 * which is rather slow.
 * @regs: Process register set
 * @ir: Instruction
 */
static inline int mipsr6_emul(struct pt_regs *regs, u32 ir)
{
	switch (MIPSInst_OPCODE(ir)) {
	case addiu_op:
		if (MIPSInst_RT(ir))
			regs->regs[MIPSInst_RT(ir)] =
				(s32)regs->regs[MIPSInst_RS(ir)] +
				(s32)MIPSInst_SIMM(ir);
		return 0;
	case daddiu_op:
		if (IS_ENABLED(CONFIG_32BIT))
			break;

		if (MIPSInst_RT(ir))
			regs->regs[MIPSInst_RT(ir)] =
				(s64)regs->regs[MIPSInst_RS(ir)] +
				(s64)MIPSInst_SIMM(ir);
		return 0;
	case lwc1_op:
	case swc1_op:
	case cop1_op:
	case cop1x_op:
		/* FPU instructions in delay slot */
		return -SIGFPE;
	case spec_op:
		switch (MIPSInst_FUNC(ir)) {
		case or_op:
			if (MIPSInst_RD(ir))
				regs->regs[MIPSInst_RD(ir)] =
					regs->regs[MIPSInst_RS(ir)] |
					regs->regs[MIPSInst_RT(ir)];
			return 0;
		case sll_op:
			if (MIPSInst_RS(ir))
				break;

			if (MIPSInst_RD(ir))
				regs->regs[MIPSInst_RD(ir)] =
					(s32)(((u32)regs->regs[MIPSInst_RT(ir)]) <<
						MIPSInst_FD(ir));
			return 0;
		case srl_op:
			if (MIPSInst_RS(ir))
				break;

			if (MIPSInst_RD(ir))
				regs->regs[MIPSInst_RD(ir)] =
					(s32)(((u32)regs->regs[MIPSInst_RT(ir)]) >>
						MIPSInst_FD(ir));
			return 0;
		case addu_op:
			if (MIPSInst_FD(ir))
				break;

			if (MIPSInst_RD(ir))
				regs->regs[MIPSInst_RD(ir)] =
					(s32)((u32)regs->regs[MIPSInst_RS(ir)] +
					      (u32)regs->regs[MIPSInst_RT(ir)]);
			return 0;
		case subu_op:
			if (MIPSInst_FD(ir))
				break;

			if (MIPSInst_RD(ir))
				regs->regs[MIPSInst_RD(ir)] =
					(s32)((u32)regs->regs[MIPSInst_RS(ir)] -
					      (u32)regs->regs[MIPSInst_RT(ir)]);
			return 0;
		case dsll_op:
			if (IS_ENABLED(CONFIG_32BIT) || MIPSInst_RS(ir))
				break;

			if (MIPSInst_RD(ir))
				regs->regs[MIPSInst_RD(ir)] =
					(s64)(((u64)regs->regs[MIPSInst_RT(ir)]) <<
						MIPSInst_FD(ir));
			return 0;
		case dsrl_op:
			if (IS_ENABLED(CONFIG_32BIT) || MIPSInst_RS(ir))
				break;

			if (MIPSInst_RD(ir))
				regs->regs[MIPSInst_RD(ir)] =
					(s64)(((u64)regs->regs[MIPSInst_RT(ir)]) >>
						MIPSInst_FD(ir));
			return 0;
		case daddu_op:
			if (IS_ENABLED(CONFIG_32BIT) || MIPSInst_FD(ir))
				break;

			if (MIPSInst_RD(ir))
				regs->regs[MIPSInst_RD(ir)] =
					(u64)regs->regs[MIPSInst_RS(ir)] +
					(u64)regs->regs[MIPSInst_RT(ir)];
			return 0;
		case dsubu_op:
			if (IS_ENABLED(CONFIG_32BIT) || MIPSInst_FD(ir))
				break;

			if (MIPSInst_RD(ir))
				regs->regs[MIPSInst_RD(ir)] =
					(s64)((u64)regs->regs[MIPSInst_RS(ir)] -
					      (u64)regs->regs[MIPSInst_RT(ir)]);
			return 0;
		}
		break;
	default:
		pr_debug("No fastpath BD emulation for instruction 0x%08x (op: %02x)\n",
			 ir, MIPSInst_OPCODE(ir));
	}

	return SIGILL;
}

/**
 * movf_func - Emulate a MOVF instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int movf_func(struct pt_regs *regs, u32 ir)
{
	u32 csr;
	u32 cond;

	csr = current->thread.fpu.fcr31;
	cond = fpucondbit[MIPSInst_RT(ir) >> 2];

	if (((csr & cond) == 0) && MIPSInst_RD(ir))
		regs->regs[MIPSInst_RD(ir)] = regs->regs[MIPSInst_RS(ir)];

	MIPS_R2_STATS(movs);

	return 0;
}

/**
 * movt_func - Emulate a MOVT instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int movt_func(struct pt_regs *regs, u32 ir)
{
	u32 csr;
	u32 cond;

	csr = current->thread.fpu.fcr31;
	cond = fpucondbit[MIPSInst_RT(ir) >> 2];

	if (((csr & cond) != 0) && MIPSInst_RD(ir))
		regs->regs[MIPSInst_RD(ir)] = regs->regs[MIPSInst_RS(ir)];

	MIPS_R2_STATS(movs);

	return 0;
}

/**
 * jr_func - Emulate a JR instruction.
 * @pt_regs: Process register set
 * @ir: Instruction
 *
 * Returns SIGILL if JR was in delay slot, SIGEMT if we
 * can't compute the EPC, SIGSEGV if we can't access the
 * userland instruction or 0 on success.
 */
static int jr_func(struct pt_regs *regs, u32 ir)
{
	int err;
	unsigned long cepc, epc, nepc;
	u32 nir;

	if (delay_slot(regs))
		return SIGILL;

	/* EPC after the RI/JR instruction */
	nepc = regs->cp0_epc;
	/* Roll back to the reserved R2 JR instruction */
	regs->cp0_epc -= 4;
	epc = regs->cp0_epc;
	err = __compute_return_epc(regs);

	if (err < 0)
		return SIGEMT;


	/* Computed EPC */
	cepc = regs->cp0_epc;

	/* Get DS instruction */
	err = __get_user(nir, (u32 __user *)nepc);
	if (err)
		return SIGSEGV;

	MIPS_R2BR_STATS(jrs);

	/* If nir == 0(NOP), then nothing else to do */
	if (nir) {
		/*
		 * Negative err means FPU instruction in BD-slot,
		 * Zero err means 'BD-slot emulation done'
		 * For anything else we go back to trampoline emulation.
		 */
		err = mipsr6_emul(regs, nir);
		if (err > 0) {
			regs->cp0_epc = nepc;
			err = mips_dsemul(regs, nir, epc, cepc);
			if (err == SIGILL)
				err = SIGEMT;
			MIPS_R2_STATS(dsemul);
		}
	}

	return err;
}

/**
 * movz_func - Emulate a MOVZ instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int movz_func(struct pt_regs *regs, u32 ir)
{
	if (((regs->regs[MIPSInst_RT(ir)]) == 0) && MIPSInst_RD(ir))
		regs->regs[MIPSInst_RD(ir)] = regs->regs[MIPSInst_RS(ir)];
	MIPS_R2_STATS(movs);

	return 0;
}

/**
 * movn_func - Emulate a MOVZ instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int movn_func(struct pt_regs *regs, u32 ir)
{
	if (((regs->regs[MIPSInst_RT(ir)]) != 0) && MIPSInst_RD(ir))
		regs->regs[MIPSInst_RD(ir)] = regs->regs[MIPSInst_RS(ir)];
	MIPS_R2_STATS(movs);

	return 0;
}

/**
 * mfhi_func - Emulate a MFHI instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int mfhi_func(struct pt_regs *regs, u32 ir)
{
	if (MIPSInst_RD(ir))
		regs->regs[MIPSInst_RD(ir)] = regs->hi;

	MIPS_R2_STATS(hilo);

	return 0;
}

/**
 * mthi_func - Emulate a MTHI instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int mthi_func(struct pt_regs *regs, u32 ir)
{
	regs->hi = regs->regs[MIPSInst_RS(ir)];

	MIPS_R2_STATS(hilo);

	return 0;
}

/**
 * mflo_func - Emulate a MFLO instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int mflo_func(struct pt_regs *regs, u32 ir)
{
	if (MIPSInst_RD(ir))
		regs->regs[MIPSInst_RD(ir)] = regs->lo;

	MIPS_R2_STATS(hilo);

	return 0;
}

/**
 * mtlo_func - Emulate a MTLO instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int mtlo_func(struct pt_regs *regs, u32 ir)
{
	regs->lo = regs->regs[MIPSInst_RS(ir)];

	MIPS_R2_STATS(hilo);

	return 0;
}

/**
 * mult_func - Emulate a MULT instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int mult_func(struct pt_regs *regs, u32 ir)
{
	s64 res;
	s32 rt, rs;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];
	res = (s64)rt * (s64)rs;

	rs = res;
	regs->lo = (s64)rs;
	rt = res >> 32;
	res = (s64)rt;
	regs->hi = res;

	MIPS_R2_STATS(muls);

	return 0;
}

/**
 * multu_func - Emulate a MULTU instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int multu_func(struct pt_regs *regs, u32 ir)
{
	u64 res;
	u32 rt, rs;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];
	res = (u64)rt * (u64)rs;
	rt = res;
	regs->lo = (s64)rt;
	regs->hi = (s64)(res >> 32);

	MIPS_R2_STATS(muls);

	return 0;
}

/**
 * div_func - Emulate a DIV instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int div_func(struct pt_regs *regs, u32 ir)
{
	s32 rt, rs;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];

	regs->lo = (s64)(rs / rt);
	regs->hi = (s64)(rs % rt);

	MIPS_R2_STATS(divs);

	return 0;
}

/**
 * divu_func - Emulate a DIVU instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int divu_func(struct pt_regs *regs, u32 ir)
{
	u32 rt, rs;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];

	regs->lo = (s64)(rs / rt);
	regs->hi = (s64)(rs % rt);

	MIPS_R2_STATS(divs);

	return 0;
}

/**
 * dmult_func - Emulate a DMULT instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 on success or SIGILL for 32-bit kernels.
 */
static int dmult_func(struct pt_regs *regs, u32 ir)
{
	s64 res;
	s64 rt, rs;

	if (IS_ENABLED(CONFIG_32BIT))
		return SIGILL;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];
	res = rt * rs;

	regs->lo = res;
	__asm__ __volatile__(
		"dmuh %0, %1, %2\t\n"
		: "=r"(res)
		: "r"(rt), "r"(rs));

	regs->hi = res;

	MIPS_R2_STATS(muls);

	return 0;
}

/**
 * dmultu_func - Emulate a DMULTU instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 on success or SIGILL for 32-bit kernels.
 */
static int dmultu_func(struct pt_regs *regs, u32 ir)
{
	u64 res;
	u64 rt, rs;

	if (IS_ENABLED(CONFIG_32BIT))
		return SIGILL;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];
	res = rt * rs;

	regs->lo = res;
	__asm__ __volatile__(
		"dmuhu %0, %1, %2\t\n"
		: "=r"(res)
		: "r"(rt), "r"(rs));

	regs->hi = res;

	MIPS_R2_STATS(muls);

	return 0;
}

/**
 * ddiv_func - Emulate a DDIV instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 on success or SIGILL for 32-bit kernels.
 */
static int ddiv_func(struct pt_regs *regs, u32 ir)
{
	s64 rt, rs;

	if (IS_ENABLED(CONFIG_32BIT))
		return SIGILL;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];

	regs->lo = rs / rt;
	regs->hi = rs % rt;

	MIPS_R2_STATS(divs);

	return 0;
}

/**
 * ddivu_func - Emulate a DDIVU instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 on success or SIGILL for 32-bit kernels.
 */
static int ddivu_func(struct pt_regs *regs, u32 ir)
{
	u64 rt, rs;

	if (IS_ENABLED(CONFIG_32BIT))
		return SIGILL;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];

	regs->lo = rs / rt;
	regs->hi = rs % rt;

	MIPS_R2_STATS(divs);

	return 0;
}

/* R6 removed instructions for the SPECIAL opcode */
static struct r2_decoder_table spec_op_table[] = {
	{ 0xfc1ff83f, 0x00000008, jr_func },
	{ 0xfc00ffff, 0x00000018, mult_func },
	{ 0xfc00ffff, 0x00000019, multu_func },
	{ 0xfc00ffff, 0x0000001c, dmult_func },
	{ 0xfc00ffff, 0x0000001d, dmultu_func },
	{ 0xffff07ff, 0x00000010, mfhi_func },
	{ 0xfc1fffff, 0x00000011, mthi_func },
	{ 0xffff07ff, 0x00000012, mflo_func },
	{ 0xfc1fffff, 0x00000013, mtlo_func },
	{ 0xfc0307ff, 0x00000001, movf_func },
	{ 0xfc0307ff, 0x00010001, movt_func },
	{ 0xfc0007ff, 0x0000000a, movz_func },
	{ 0xfc0007ff, 0x0000000b, movn_func },
	{ 0xfc00ffff, 0x0000001a, div_func },
	{ 0xfc00ffff, 0x0000001b, divu_func },
	{ 0xfc00ffff, 0x0000001e, ddiv_func },
	{ 0xfc00ffff, 0x0000001f, ddivu_func },
	{}
};

/**
 * madd_func - Emulate a MADD instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int madd_func(struct pt_regs *regs, u32 ir)
{
	s64 res;
	s32 rt, rs;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];
	res = (s64)rt * (s64)rs;
	rt = regs->hi;
	rs = regs->lo;
	res += ((((s64)rt) << 32) | (u32)rs);

	rt = res;
	regs->lo = (s64)rt;
	rs = res >> 32;
	regs->hi = (s64)rs;

	MIPS_R2_STATS(dsps);

	return 0;
}

/**
 * maddu_func - Emulate a MADDU instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int maddu_func(struct pt_regs *regs, u32 ir)
{
	u64 res;
	u32 rt, rs;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];
	res = (u64)rt * (u64)rs;
	rt = regs->hi;
	rs = regs->lo;
	res += ((((s64)rt) << 32) | (u32)rs);

	rt = res;
	regs->lo = (s64)rt;
	rs = res >> 32;
	regs->hi = (s64)rs;

	MIPS_R2_STATS(dsps);

	return 0;
}

/**
 * msub_func - Emulate a MSUB instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int msub_func(struct pt_regs *regs, u32 ir)
{
	s64 res;
	s32 rt, rs;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];
	res = (s64)rt * (s64)rs;
	rt = regs->hi;
	rs = regs->lo;
	res = ((((s64)rt) << 32) | (u32)rs) - res;

	rt = res;
	regs->lo = (s64)rt;
	rs = res >> 32;
	regs->hi = (s64)rs;

	MIPS_R2_STATS(dsps);

	return 0;
}

/**
 * msubu_func - Emulate a MSUBU instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int msubu_func(struct pt_regs *regs, u32 ir)
{
	u64 res;
	u32 rt, rs;

	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];
	res = (u64)rt * (u64)rs;
	rt = regs->hi;
	rs = regs->lo;
	res = ((((s64)rt) << 32) | (u32)rs) - res;

	rt = res;
	regs->lo = (s64)rt;
	rs = res >> 32;
	regs->hi = (s64)rs;

	MIPS_R2_STATS(dsps);

	return 0;
}

/**
 * mul_func - Emulate a MUL instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int mul_func(struct pt_regs *regs, u32 ir)
{
	s64 res;
	s32 rt, rs;

	if (!MIPSInst_RD(ir))
		return 0;
	rt = regs->regs[MIPSInst_RT(ir)];
	rs = regs->regs[MIPSInst_RS(ir)];
	res = (s64)rt * (s64)rs;

	rs = res;
	regs->regs[MIPSInst_RD(ir)] = (s64)rs;

	MIPS_R2_STATS(muls);

	return 0;
}

/**
 * clz_func - Emulate a CLZ instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int clz_func(struct pt_regs *regs, u32 ir)
{
	u32 res;
	u32 rs;

	if (!MIPSInst_RD(ir))
		return 0;

	rs = regs->regs[MIPSInst_RS(ir)];
	__asm__ __volatile__("clz %0, %1" : "=r"(res) : "r"(rs));
	regs->regs[MIPSInst_RD(ir)] = res;

	MIPS_R2_STATS(bops);

	return 0;
}

/**
 * clo_func - Emulate a CLO instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */

static int clo_func(struct pt_regs *regs, u32 ir)
{
	u32 res;
	u32 rs;

	if (!MIPSInst_RD(ir))
		return 0;

	rs = regs->regs[MIPSInst_RS(ir)];
	__asm__ __volatile__("clo %0, %1" : "=r"(res) : "r"(rs));
	regs->regs[MIPSInst_RD(ir)] = res;

	MIPS_R2_STATS(bops);

	return 0;
}

/**
 * dclz_func - Emulate a DCLZ instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int dclz_func(struct pt_regs *regs, u32 ir)
{
	u64 res;
	u64 rs;

	if (IS_ENABLED(CONFIG_32BIT))
		return SIGILL;

	if (!MIPSInst_RD(ir))
		return 0;

	rs = regs->regs[MIPSInst_RS(ir)];
	__asm__ __volatile__("dclz %0, %1" : "=r"(res) : "r"(rs));
	regs->regs[MIPSInst_RD(ir)] = res;

	MIPS_R2_STATS(bops);

	return 0;
}

/**
 * dclo_func - Emulate a DCLO instruction
 * @regs: Process register set
 * @ir: Instruction
 *
 * Returns 0 since it always succeeds.
 */
static int dclo_func(struct pt_regs *regs, u32 ir)
{
	u64 res;
	u64 rs;

	if (IS_ENABLED(CONFIG_32BIT))
		return SIGILL;

	if (!MIPSInst_RD(ir))
		return 0;

	rs = regs->regs[MIPSInst_RS(ir)];
	__asm__ __volatile__("dclo %0, %1" : "=r"(res) : "r"(rs));
	regs->regs[MIPSInst_RD(ir)] = res;

	MIPS_R2_STATS(bops);

	return 0;
}

/* R6 removed instructions for the SPECIAL2 opcode */
static struct r2_decoder_table spec2_op_table[] = {
	{ 0xfc00ffff, 0x70000000, madd_func },
	{ 0xfc00ffff, 0x70000001, maddu_func },
	{ 0xfc0007ff, 0x70000002, mul_func },
	{ 0xfc00ffff, 0x70000004, msub_func },
	{ 0xfc00ffff, 0x70000005, msubu_func },
	{ 0xfc0007ff, 0x70000020, clz_func },
	{ 0xfc0007ff, 0x70000021, clo_func },
	{ 0xfc0007ff, 0x70000024, dclz_func },
	{ 0xfc0007ff, 0x70000025, dclo_func },
	{ }
};

static inline int mipsr2_find_op_func(struct pt_regs *regs, u32 inst,
				      struct r2_decoder_table *table)
{
	struct r2_decoder_table *p;
	int err;

	for (p = table; p->func; p++) {
		if ((inst & p->mask) == p->code) {
			err = (p->func)(regs, inst);
			return err;
		}
	}
	return SIGILL;
}

/**
 * mipsr2_decoder: Decode and emulate a MIPS R2 instruction
 * @regs: Process register set
 * @inst: Instruction to decode and emulate
 * @fcr31: Floating Point Control and Status Register returned
 */
int mipsr2_decoder(struct pt_regs *regs, u32 inst, unsigned long *fcr31)
{
	int err = 0;
	unsigned long vaddr;
	u32 nir;
	unsigned long cpc, epc, nepc, r31, res, rs, rt;

	void __user *fault_addr = NULL;
	int pass = 0;

repeat:
	r31 = regs->regs[31];
	epc = regs->cp0_epc;
	err = compute_return_epc(regs);
	if (err < 0) {
		BUG();
		return SIGEMT;
	}
	pr_debug("Emulating the 0x%08x R2 instruction @ 0x%08lx (pass=%d))\n",
		 inst, epc, pass);

	switch (MIPSInst_OPCODE(inst)) {
	case spec_op:
		err = mipsr2_find_op_func(regs, inst, spec_op_table);
		if (err < 0) {
			/* FPU instruction under JR */
			regs->cp0_cause |= CAUSEF_BD;
			goto fpu_emul;
		}
		break;
	case spec2_op:
		err = mipsr2_find_op_func(regs, inst, spec2_op_table);
		break;
	case bcond_op:
		rt = MIPSInst_RT(inst);
		rs = MIPSInst_RS(inst);
		switch (rt) {
		case tgei_op:
			if ((long)regs->regs[rs] >= MIPSInst_SIMM(inst))
				do_trap_or_bp(regs, 0, 0, "TGEI");

			MIPS_R2_STATS(traps);

			break;
		case tgeiu_op:
			if (regs->regs[rs] >= MIPSInst_UIMM(inst))
				do_trap_or_bp(regs, 0, 0, "TGEIU");

			MIPS_R2_STATS(traps);

			break;
		case tlti_op:
			if ((long)regs->regs[rs] < MIPSInst_SIMM(inst))
				do_trap_or_bp(regs, 0, 0, "TLTI");

			MIPS_R2_STATS(traps);

			break;
		case tltiu_op:
			if (regs->regs[rs] < MIPSInst_UIMM(inst))
				do_trap_or_bp(regs, 0, 0, "TLTIU");

			MIPS_R2_STATS(traps);

			break;
		case teqi_op:
			if (regs->regs[rs] == MIPSInst_SIMM(inst))
				do_trap_or_bp(regs, 0, 0, "TEQI");

			MIPS_R2_STATS(traps);

			break;
		case tnei_op:
			if (regs->regs[rs] != MIPSInst_SIMM(inst))
				do_trap_or_bp(regs, 0, 0, "TNEI");

			MIPS_R2_STATS(traps);

			break;
		case bltzl_op:
		case bgezl_op:
		case bltzall_op:
		case bgezall_op:
			if (delay_slot(regs)) {
				err = SIGILL;
				break;
			}
			regs->regs[31] = r31;
			regs->cp0_epc = epc;
			err = __compute_return_epc(regs);
			if (err < 0)
				return SIGEMT;
			if (err != BRANCH_LIKELY_TAKEN)
				break;
			cpc = regs->cp0_epc;
			nepc = epc + 4;
			err = __get_user(nir, (u32 __user *)nepc);
			if (err) {
				err = SIGSEGV;
				break;
			}
			/*
			 * This will probably be optimized away when
			 * CONFIG_DEBUG_FS is not enabled
			 */
			switch (rt) {
			case bltzl_op:
				MIPS_R2BR_STATS(bltzl);
				break;
			case bgezl_op:
				MIPS_R2BR_STATS(bgezl);
				break;
			case bltzall_op:
				MIPS_R2BR_STATS(bltzall);
				break;
			case bgezall_op:
				MIPS_R2BR_STATS(bgezall);
				break;
			}

			switch (MIPSInst_OPCODE(nir)) {
			case cop1_op:
			case cop1x_op:
			case lwc1_op:
			case swc1_op:
				regs->cp0_cause |= CAUSEF_BD;
				goto fpu_emul;
			}
			if (nir) {
				err = mipsr6_emul(regs, nir);
				if (err > 0) {
					err = mips_dsemul(regs, nir, epc, cpc);
					if (err == SIGILL)
						err = SIGEMT;
					MIPS_R2_STATS(dsemul);
				}
			}
			break;
		case bltzal_op:
		case bgezal_op:
			if (delay_slot(regs)) {
				err = SIGILL;
				break;
			}
			regs->regs[31] = r31;
			regs->cp0_epc = epc;
			err = __compute_return_epc(regs);
			if (err < 0)
				return SIGEMT;
			cpc = regs->cp0_epc;
			nepc = epc + 4;
			err = __get_user(nir, (u32 __user *)nepc);
			if (err) {
				err = SIGSEGV;
				break;
			}
			/*
			 * This will probably be optimized away when
			 * CONFIG_DEBUG_FS is not enabled
			 */
			switch (rt) {
			case bltzal_op:
				MIPS_R2BR_STATS(bltzal);
				break;
			case bgezal_op:
				MIPS_R2BR_STATS(bgezal);
				break;
			}

			switch (MIPSInst_OPCODE(nir)) {
			case cop1_op:
			case cop1x_op:
			case lwc1_op:
			case swc1_op:
				regs->cp0_cause |= CAUSEF_BD;
				goto fpu_emul;
			}
			if (nir) {
				err = mipsr6_emul(regs, nir);
				if (err > 0) {
					err = mips_dsemul(regs, nir, epc, cpc);
					if (err == SIGILL)
						err = SIGEMT;
					MIPS_R2_STATS(dsemul);
				}
			}
			break;
		default:
			regs->regs[31] = r31;
			regs->cp0_epc = epc;
			err = SIGILL;
			break;
		}
		break;

	case beql_op:
	case bnel_op:
	case blezl_op:
	case bgtzl_op:
		if (delay_slot(regs)) {
			err = SIGILL;
			break;
		}
		regs->regs[31] = r31;
		regs->cp0_epc = epc;
		err = __compute_return_epc(regs);
		if (err < 0)
			return SIGEMT;
		if (err != BRANCH_LIKELY_TAKEN)
			break;
		cpc = regs->cp0_epc;
		nepc = epc + 4;
		err = __get_user(nir, (u32 __user *)nepc);
		if (err) {
			err = SIGSEGV;
			break;
		}
		/*
		 * This will probably be optimized away when
		 * CONFIG_DEBUG_FS is not enabled
		 */
		switch (MIPSInst_OPCODE(inst)) {
		case beql_op:
			MIPS_R2BR_STATS(beql);
			break;
		case bnel_op:
			MIPS_R2BR_STATS(bnel);
			break;
		case blezl_op:
			MIPS_R2BR_STATS(blezl);
			break;
		case bgtzl_op:
			MIPS_R2BR_STATS(bgtzl);
			break;
		}

		switch (MIPSInst_OPCODE(nir)) {
		case cop1_op:
		case cop1x_op:
		case lwc1_op:
		case swc1_op:
			regs->cp0_cause |= CAUSEF_BD;
			goto fpu_emul;
		}
		if (nir) {
			err = mipsr6_emul(regs, nir);
			if (err > 0) {
				err = mips_dsemul(regs, nir, epc, cpc);
				if (err == SIGILL)
					err = SIGEMT;
				MIPS_R2_STATS(dsemul);
			}
		}
		break;
	case lwc1_op:
	case swc1_op:
	case cop1_op:
	case cop1x_op:
fpu_emul:
		regs->regs[31] = r31;
		regs->cp0_epc = epc;
		if (!used_math()) {     /* First time FPU user.  */
			preempt_disable();
			err = init_fpu();
			preempt_enable();
			set_used_math();
		}
		lose_fpu(1);    /* Save FPU state for the emulator. */

		err = fpu_emulator_cop1Handler(regs, &current->thread.fpu, 0,
					       &fault_addr);
		*fcr31 = current->thread.fpu.fcr31;

		/*
		 * We can't allow the emulated instruction to leave any of
		 * the cause bits set in $fcr31.
		 */
		current->thread.fpu.fcr31 &= ~FPU_CSR_ALL_X;

		/*
		 * this is a tricky issue - lose_fpu() uses LL/SC atomics
		 * if FPU is owned and effectively cancels user level LL/SC.
		 * So, it could be logical to don't restore FPU ownership here.
		 * But the sequence of multiple FPU instructions is much much
		 * more often than LL-FPU-SC and I prefer loop here until
		 * next scheduler cycle cancels FPU ownership
		 */
		own_fpu(1);	/* Restore FPU state. */

		if (err)
			current->thread.cp0_baduaddr = (unsigned long)fault_addr;

		MIPS_R2_STATS(fpus);

		break;

	case lwl_op:
		rt = regs->regs[MIPSInst_RT(inst)];
		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (!access_ok(VERIFY_READ, vaddr, 4)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGSEGV;
			break;
		}
		__asm__ __volatile__(
			"	.set	push\n"
			"	.set	reorder\n"
#ifdef CONFIG_CPU_LITTLE_ENDIAN
			"1:"	LB	"%1, 0(%2)\n"
				INS	"%0, %1, 24, 8\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
			"2:"	LB	"%1, 0(%2)\n"
				INS	"%0, %1, 16, 8\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
			"3:"	LB	"%1, 0(%2)\n"
				INS	"%0, %1, 8, 8\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
			"4:"	LB	"%1, 0(%2)\n"
				INS	"%0, %1, 0, 8\n"
#else /* !CONFIG_CPU_LITTLE_ENDIAN */
			"1:"	LB	"%1, 0(%2)\n"
				INS	"%0, %1, 24, 8\n"
				ADDIU	"%2, %2, 1\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
			"2:"	LB	"%1, 0(%2)\n"
				INS	"%0, %1, 16, 8\n"
				ADDIU	"%2, %2, 1\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
			"3:"	LB	"%1, 0(%2)\n"
				INS	"%0, %1, 8, 8\n"
				ADDIU	"%2, %2, 1\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
			"4:"	LB	"%1, 0(%2)\n"
				INS	"%0, %1, 0, 8\n"
#endif /* CONFIG_CPU_LITTLE_ENDIAN */
			"9:	sll	%0, %0, 0\n"
			"10:\n"
			"	.insn\n"
			"	.section	.fixup,\"ax\"\n"
			"8:	li	%3,%4\n"
			"	j	10b\n"
			"	.previous\n"
			"	.section	__ex_table,\"a\"\n"
			STR(PTR) " 1b,8b\n"
			STR(PTR) " 2b,8b\n"
			STR(PTR) " 3b,8b\n"
			STR(PTR) " 4b,8b\n"
			"	.previous\n"
			"	.set	pop\n"
			: "+&r"(rt), "=&r"(rs),
			  "+&r"(vaddr), "+&r"(err)
			: "i"(SIGSEGV));

		if (MIPSInst_RT(inst) && !err)
			regs->regs[MIPSInst_RT(inst)] = rt;

		MIPS_R2_STATS(loads);

		break;

	case lwr_op:
		rt = regs->regs[MIPSInst_RT(inst)];
		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (!access_ok(VERIFY_READ, vaddr, 4)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGSEGV;
			break;
		}
		__asm__ __volatile__(
			"       .set	push\n"
			"       .set	reorder\n"
#ifdef CONFIG_CPU_LITTLE_ENDIAN
			"1:"    LB	"%1, 0(%2)\n"
				INS	"%0, %1, 0, 8\n"
				ADDIU	"%2, %2, 1\n"
			"       andi	%1, %2, 0x3\n"
			"       beq	$0, %1, 9f\n"
			"2:"    LB	"%1, 0(%2)\n"
				INS	"%0, %1, 8, 8\n"
				ADDIU	"%2, %2, 1\n"
			"       andi	%1, %2, 0x3\n"
			"       beq	$0, %1, 9f\n"
			"3:"    LB	"%1, 0(%2)\n"
				INS	"%0, %1, 16, 8\n"
				ADDIU	"%2, %2, 1\n"
			"       andi	%1, %2, 0x3\n"
			"       beq	$0, %1, 9f\n"
			"4:"    LB	"%1, 0(%2)\n"
				INS	"%0, %1, 24, 8\n"
			"       sll	%0, %0, 0\n"
#else /* !CONFIG_CPU_LITTLE_ENDIAN */
			"1:"    LB	"%1, 0(%2)\n"
				INS	"%0, %1, 0, 8\n"
			"       andi	%1, %2, 0x3\n"
			"       beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
			"2:"    LB	"%1, 0(%2)\n"
				INS	"%0, %1, 8, 8\n"
			"       andi	%1, %2, 0x3\n"
			"       beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
			"3:"    LB	"%1, 0(%2)\n"
				INS	"%0, %1, 16, 8\n"
			"       andi	%1, %2, 0x3\n"
			"       beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
			"4:"    LB	"%1, 0(%2)\n"
				INS	"%0, %1, 24, 8\n"
			"       sll	%0, %0, 0\n"
#endif /* CONFIG_CPU_LITTLE_ENDIAN */
			"9:\n"
			"10:\n"
			"	.insn\n"
			"	.section	.fixup,\"ax\"\n"
			"8:	li	%3,%4\n"
			"	j	10b\n"
			"       .previous\n"
			"	.section	__ex_table,\"a\"\n"
			STR(PTR) " 1b,8b\n"
			STR(PTR) " 2b,8b\n"
			STR(PTR) " 3b,8b\n"
			STR(PTR) " 4b,8b\n"
			"	.previous\n"
			"	.set	pop\n"
			: "+&r"(rt), "=&r"(rs),
			  "+&r"(vaddr), "+&r"(err)
			: "i"(SIGSEGV));
		if (MIPSInst_RT(inst) && !err)
			regs->regs[MIPSInst_RT(inst)] = rt;

		MIPS_R2_STATS(loads);

		break;

	case swl_op:
		rt = regs->regs[MIPSInst_RT(inst)];
		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (!access_ok(VERIFY_WRITE, vaddr, 4)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGSEGV;
			break;
		}
		__asm__ __volatile__(
			"	.set	push\n"
			"	.set	reorder\n"
#ifdef CONFIG_CPU_LITTLE_ENDIAN
				EXT	"%1, %0, 24, 8\n"
			"1:"	SB	"%1, 0(%2)\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
				EXT	"%1, %0, 16, 8\n"
			"2:"	SB	"%1, 0(%2)\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
				EXT	"%1, %0, 8, 8\n"
			"3:"	SB	"%1, 0(%2)\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
				EXT	"%1, %0, 0, 8\n"
			"4:"	SB	"%1, 0(%2)\n"
#else /* !CONFIG_CPU_LITTLE_ENDIAN */
				EXT	"%1, %0, 24, 8\n"
			"1:"	SB	"%1, 0(%2)\n"
				ADDIU	"%2, %2, 1\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				EXT	"%1, %0, 16, 8\n"
			"2:"	SB	"%1, 0(%2)\n"
				ADDIU	"%2, %2, 1\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				EXT	"%1, %0, 8, 8\n"
			"3:"	SB	"%1, 0(%2)\n"
				ADDIU	"%2, %2, 1\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				EXT	"%1, %0, 0, 8\n"
			"4:"	SB	"%1, 0(%2)\n"
#endif /* CONFIG_CPU_LITTLE_ENDIAN */
			"9:\n"
			"	.insn\n"
			"       .section        .fixup,\"ax\"\n"
			"8:	li	%3,%4\n"
			"	j	9b\n"
			"	.previous\n"
			"	.section        __ex_table,\"a\"\n"
			STR(PTR) " 1b,8b\n"
			STR(PTR) " 2b,8b\n"
			STR(PTR) " 3b,8b\n"
			STR(PTR) " 4b,8b\n"
			"	.previous\n"
			"	.set	pop\n"
			: "+&r"(rt), "=&r"(rs),
			  "+&r"(vaddr), "+&r"(err)
			: "i"(SIGSEGV)
			: "memory");

		MIPS_R2_STATS(stores);

		break;

	case swr_op:
		rt = regs->regs[MIPSInst_RT(inst)];
		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (!access_ok(VERIFY_WRITE, vaddr, 4)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGSEGV;
			break;
		}
		__asm__ __volatile__(
			"	.set	push\n"
			"	.set	reorder\n"
#ifdef CONFIG_CPU_LITTLE_ENDIAN
				EXT	"%1, %0, 0, 8\n"
			"1:"	SB	"%1, 0(%2)\n"
				ADDIU	"%2, %2, 1\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				EXT	"%1, %0, 8, 8\n"
			"2:"	SB	"%1, 0(%2)\n"
				ADDIU	"%2, %2, 1\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				EXT	"%1, %0, 16, 8\n"
			"3:"	SB	"%1, 0(%2)\n"
				ADDIU	"%2, %2, 1\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				EXT	"%1, %0, 24, 8\n"
			"4:"	SB	"%1, 0(%2)\n"
#else /* !CONFIG_CPU_LITTLE_ENDIAN */
				EXT	"%1, %0, 0, 8\n"
			"1:"	SB	"%1, 0(%2)\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
				EXT	"%1, %0, 8, 8\n"
			"2:"	SB	"%1, 0(%2)\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
				EXT	"%1, %0, 16, 8\n"
			"3:"	SB	"%1, 0(%2)\n"
			"	andi	%1, %2, 0x3\n"
			"	beq	$0, %1, 9f\n"
				ADDIU	"%2, %2, -1\n"
				EXT	"%1, %0, 24, 8\n"
			"4:"	SB	"%1, 0(%2)\n"
#endif /* CONFIG_CPU_LITTLE_ENDIAN */
			"9:\n"
			"	.insn\n"
			"	.section        .fixup,\"ax\"\n"
			"8:	li	%3,%4\n"
			"	j	9b\n"
			"	.previous\n"
			"	.section        __ex_table,\"a\"\n"
			STR(PTR) " 1b,8b\n"
			STR(PTR) " 2b,8b\n"
			STR(PTR) " 3b,8b\n"
			STR(PTR) " 4b,8b\n"
			"	.previous\n"
			"	.set	pop\n"
			: "+&r"(rt), "=&r"(rs),
			  "+&r"(vaddr), "+&r"(err)
			: "i"(SIGSEGV)
			: "memory");

		MIPS_R2_STATS(stores);

		break;

	case ldl_op:
		if (IS_ENABLED(CONFIG_32BIT)) {
		    err = SIGILL;
		    break;
		}

		rt = regs->regs[MIPSInst_RT(inst)];
		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (!access_ok(VERIFY_READ, vaddr, 8)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGSEGV;
			break;
		}
		__asm__ __volatile__(
			"	.set    push\n"
			"	.set    reorder\n"
#ifdef CONFIG_CPU_LITTLE_ENDIAN
			"1:	lb	%1, 0(%2)\n"
			"	dinsu	%0, %1, 56, 8\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"2:	lb	%1, 0(%2)\n"
			"	dinsu	%0, %1, 48, 8\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"3:	lb	%1, 0(%2)\n"
			"	dinsu	%0, %1, 40, 8\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"4:	lb	%1, 0(%2)\n"
			"	dinsu	%0, %1, 32, 8\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"5:	lb	%1, 0(%2)\n"
			"	dins	%0, %1, 24, 8\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"6:	lb	%1, 0(%2)\n"
			"	dins	%0, %1, 16, 8\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"7:	lb	%1, 0(%2)\n"
			"	dins	%0, %1, 8, 8\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"0:	lb	%1, 0(%2)\n"
			"	dins	%0, %1, 0, 8\n"
#else /* !CONFIG_CPU_LITTLE_ENDIAN */
			"1:	lb	%1, 0(%2)\n"
			"	dinsu	%0, %1, 56, 8\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"2:	lb	%1, 0(%2)\n"
			"	dinsu	%0, %1, 48, 8\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"3:	lb	%1, 0(%2)\n"
			"	dinsu	%0, %1, 40, 8\n"
			"	daddiu  %2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"4:	lb	%1, 0(%2)\n"
			"	dinsu	%0, %1, 32, 8\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"5:	lb	%1, 0(%2)\n"
			"	dins	%0, %1, 24, 8\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"6:	lb	%1, 0(%2)\n"
			"	dins	%0, %1, 16, 8\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"7:	lb	%1, 0(%2)\n"
			"	dins	%0, %1, 8, 8\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"0:	lb	%1, 0(%2)\n"
			"	dins	%0, %1, 0, 8\n"
#endif /* CONFIG_CPU_LITTLE_ENDIAN */
			"9:\n"
			"	.insn\n"
			"	.section        .fixup,\"ax\"\n"
			"8:	li	%3,%4\n"
			"	j	9b\n"
			"	.previous\n"
			"	.section        __ex_table,\"a\"\n"
			STR(PTR) " 1b,8b\n"
			STR(PTR) " 2b,8b\n"
			STR(PTR) " 3b,8b\n"
			STR(PTR) " 4b,8b\n"
			STR(PTR) " 5b,8b\n"
			STR(PTR) " 6b,8b\n"
			STR(PTR) " 7b,8b\n"
			STR(PTR) " 0b,8b\n"
			"	.previous\n"
			"	.set	pop\n"
			: "+&r"(rt), "=&r"(rs),
			  "+&r"(vaddr), "+&r"(err)
			: "i"(SIGSEGV));
		if (MIPSInst_RT(inst) && !err)
			regs->regs[MIPSInst_RT(inst)] = rt;

		MIPS_R2_STATS(loads);
		break;

	case ldr_op:
		if (IS_ENABLED(CONFIG_32BIT)) {
		    err = SIGILL;
		    break;
		}

		rt = regs->regs[MIPSInst_RT(inst)];
		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (!access_ok(VERIFY_READ, vaddr, 8)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGSEGV;
			break;
		}
		__asm__ __volatile__(
			"	.set    push\n"
			"	.set    reorder\n"
#ifdef CONFIG_CPU_LITTLE_ENDIAN
			"1:	lb      %1, 0(%2)\n"
			"	dins   %0, %1, 0, 8\n"
			"	daddiu  %2, %2, 1\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"2:	lb      %1, 0(%2)\n"
			"	dins   %0, %1, 8, 8\n"
			"	daddiu  %2, %2, 1\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"3:	lb      %1, 0(%2)\n"
			"	dins   %0, %1, 16, 8\n"
			"	daddiu  %2, %2, 1\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"4:	lb      %1, 0(%2)\n"
			"	dins   %0, %1, 24, 8\n"
			"	daddiu  %2, %2, 1\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"5:	lb      %1, 0(%2)\n"
			"	dinsu    %0, %1, 32, 8\n"
			"	daddiu  %2, %2, 1\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"6:	lb      %1, 0(%2)\n"
			"	dinsu    %0, %1, 40, 8\n"
			"	daddiu  %2, %2, 1\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"7:	lb      %1, 0(%2)\n"
			"	dinsu    %0, %1, 48, 8\n"
			"	daddiu  %2, %2, 1\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"0:	lb      %1, 0(%2)\n"
			"	dinsu    %0, %1, 56, 8\n"
#else /* !CONFIG_CPU_LITTLE_ENDIAN */
			"1:	lb      %1, 0(%2)\n"
			"	dins   %0, %1, 0, 8\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"	daddiu  %2, %2, -1\n"
			"2:	lb      %1, 0(%2)\n"
			"	dins   %0, %1, 8, 8\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"	daddiu  %2, %2, -1\n"
			"3:	lb      %1, 0(%2)\n"
			"	dins   %0, %1, 16, 8\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"	daddiu  %2, %2, -1\n"
			"4:	lb      %1, 0(%2)\n"
			"	dins   %0, %1, 24, 8\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"	daddiu  %2, %2, -1\n"
			"5:	lb      %1, 0(%2)\n"
			"	dinsu    %0, %1, 32, 8\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"	daddiu  %2, %2, -1\n"
			"6:	lb      %1, 0(%2)\n"
			"	dinsu    %0, %1, 40, 8\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"	daddiu  %2, %2, -1\n"
			"7:	lb      %1, 0(%2)\n"
			"	dinsu    %0, %1, 48, 8\n"
			"	andi    %1, %2, 0x7\n"
			"	beq     $0, %1, 9f\n"
			"	daddiu  %2, %2, -1\n"
			"0:	lb      %1, 0(%2)\n"
			"	dinsu    %0, %1, 56, 8\n"
#endif /* CONFIG_CPU_LITTLE_ENDIAN */
			"9:\n"
			"	.insn\n"
			"	.section        .fixup,\"ax\"\n"
			"8:	li     %3,%4\n"
			"	j      9b\n"
			"	.previous\n"
			"	.section        __ex_table,\"a\"\n"
			STR(PTR) " 1b,8b\n"
			STR(PTR) " 2b,8b\n"
			STR(PTR) " 3b,8b\n"
			STR(PTR) " 4b,8b\n"
			STR(PTR) " 5b,8b\n"
			STR(PTR) " 6b,8b\n"
			STR(PTR) " 7b,8b\n"
			STR(PTR) " 0b,8b\n"
			"	.previous\n"
			"	.set    pop\n"
			: "+&r"(rt), "=&r"(rs),
			  "+&r"(vaddr), "+&r"(err)
			: "i"(SIGSEGV));
		if (MIPSInst_RT(inst) && !err)
			regs->regs[MIPSInst_RT(inst)] = rt;

		MIPS_R2_STATS(loads);
		break;

	case sdl_op:
		if (IS_ENABLED(CONFIG_32BIT)) {
		    err = SIGILL;
		    break;
		}

		rt = regs->regs[MIPSInst_RT(inst)];
		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (!access_ok(VERIFY_WRITE, vaddr, 8)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGSEGV;
			break;
		}
		__asm__ __volatile__(
			"	.set	push\n"
			"	.set	reorder\n"
#ifdef CONFIG_CPU_LITTLE_ENDIAN
			"	dextu	%1, %0, 56, 8\n"
			"1:	sb	%1, 0(%2)\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"	dextu	%1, %0, 48, 8\n"
			"2:	sb	%1, 0(%2)\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"	dextu	%1, %0, 40, 8\n"
			"3:	sb	%1, 0(%2)\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"	dextu	%1, %0, 32, 8\n"
			"4:	sb	%1, 0(%2)\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"	dext	%1, %0, 24, 8\n"
			"5:	sb	%1, 0(%2)\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"	dext	%1, %0, 16, 8\n"
			"6:	sb	%1, 0(%2)\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"	dext	%1, %0, 8, 8\n"
			"7:	sb	%1, 0(%2)\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	daddiu	%2, %2, -1\n"
			"	dext	%1, %0, 0, 8\n"
			"0:	sb	%1, 0(%2)\n"
#else /* !CONFIG_CPU_LITTLE_ENDIAN */
			"	dextu	%1, %0, 56, 8\n"
			"1:	sb	%1, 0(%2)\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	dextu	%1, %0, 48, 8\n"
			"2:	sb	%1, 0(%2)\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	dextu	%1, %0, 40, 8\n"
			"3:	sb	%1, 0(%2)\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	dextu	%1, %0, 32, 8\n"
			"4:	sb	%1, 0(%2)\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	dext	%1, %0, 24, 8\n"
			"5:	sb	%1, 0(%2)\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	dext	%1, %0, 16, 8\n"
			"6:	sb	%1, 0(%2)\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	dext	%1, %0, 8, 8\n"
			"7:	sb	%1, 0(%2)\n"
			"	daddiu	%2, %2, 1\n"
			"	andi	%1, %2, 0x7\n"
			"	beq	$0, %1, 9f\n"
			"	dext	%1, %0, 0, 8\n"
			"0:	sb	%1, 0(%2)\n"
#endif /* CONFIG_CPU_LITTLE_ENDIAN */
			"9:\n"
			"	.insn\n"
			"	.section        .fixup,\"ax\"\n"
			"8:	li	%3,%4\n"
			"	j	9b\n"
			"	.previous\n"
			"	.section        __ex_table,\"a\"\n"
			STR(PTR) " 1b,8b\n"
			STR(PTR) " 2b,8b\n"
			STR(PTR) " 3b,8b\n"
			STR(PTR) " 4b,8b\n"
			STR(PTR) " 5b,8b\n"
			STR(PTR) " 6b,8b\n"
			STR(PTR) " 7b,8b\n"
			STR(PTR) " 0b,8b\n"
			"	.previous\n"
			"	.set	pop\n"
			: "+&r"(rt), "=&r"(rs),
			  "+&r"(vaddr), "+&r"(err)
			: "i"(SIGSEGV)
			: "memory");

		MIPS_R2_STATS(stores);
		break;

	case sdr_op:
		if (IS_ENABLED(CONFIG_32BIT)) {
		    err = SIGILL;
		    break;
		}

		rt = regs->regs[MIPSInst_RT(inst)];
		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (!access_ok(VERIFY_WRITE, vaddr, 8)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGSEGV;
			break;
		}
		__asm__ __volatile__(
			"       .set	push\n"
			"       .set	reorder\n"
#ifdef CONFIG_CPU_LITTLE_ENDIAN
			"       dext	%1, %0, 0, 8\n"
			"1:     sb	%1, 0(%2)\n"
			"       daddiu	%2, %2, 1\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       dext	%1, %0, 8, 8\n"
			"2:     sb	%1, 0(%2)\n"
			"       daddiu	%2, %2, 1\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       dext	%1, %0, 16, 8\n"
			"3:     sb	%1, 0(%2)\n"
			"       daddiu	%2, %2, 1\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       dext	%1, %0, 24, 8\n"
			"4:     sb	%1, 0(%2)\n"
			"       daddiu	%2, %2, 1\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       dextu	%1, %0, 32, 8\n"
			"5:     sb	%1, 0(%2)\n"
			"       daddiu	%2, %2, 1\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       dextu	%1, %0, 40, 8\n"
			"6:     sb	%1, 0(%2)\n"
			"       daddiu	%2, %2, 1\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       dextu	%1, %0, 48, 8\n"
			"7:     sb	%1, 0(%2)\n"
			"       daddiu	%2, %2, 1\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       dextu	%1, %0, 56, 8\n"
			"0:     sb	%1, 0(%2)\n"
#else /* !CONFIG_CPU_LITTLE_ENDIAN */
			"       dext	%1, %0, 0, 8\n"
			"1:     sb	%1, 0(%2)\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       daddiu	%2, %2, -1\n"
			"       dext	%1, %0, 8, 8\n"
			"2:     sb	%1, 0(%2)\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       daddiu	%2, %2, -1\n"
			"       dext	%1, %0, 16, 8\n"
			"3:     sb	%1, 0(%2)\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       daddiu	%2, %2, -1\n"
			"       dext	%1, %0, 24, 8\n"
			"4:     sb	%1, 0(%2)\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       daddiu	%2, %2, -1\n"
			"       dextu	%1, %0, 32, 8\n"
			"5:     sb	%1, 0(%2)\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       daddiu	%2, %2, -1\n"
			"       dextu	%1, %0, 40, 8\n"
			"6:     sb	%1, 0(%2)\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       daddiu	%2, %2, -1\n"
			"       dextu	%1, %0, 48, 8\n"
			"7:     sb	%1, 0(%2)\n"
			"       andi	%1, %2, 0x7\n"
			"       beq	$0, %1, 9f\n"
			"       daddiu	%2, %2, -1\n"
			"       dextu	%1, %0, 56, 8\n"
			"0:     sb	%1, 0(%2)\n"
#endif /* CONFIG_CPU_LITTLE_ENDIAN */
			"9:\n"
			"       .insn\n"
			"       .section        .fixup,\"ax\"\n"
			"8:     li	%3,%4\n"
			"       j	9b\n"
			"       .previous\n"
			"       .section        __ex_table,\"a\"\n"
			STR(PTR) " 1b,8b\n"
			STR(PTR) " 2b,8b\n"
			STR(PTR) " 3b,8b\n"
			STR(PTR) " 4b,8b\n"
			STR(PTR) " 5b,8b\n"
			STR(PTR) " 6b,8b\n"
			STR(PTR) " 7b,8b\n"
			STR(PTR) " 0b,8b\n"
			"       .previous\n"
			"       .set	pop\n"
			: "+&r"(rt), "=&r"(rs),
			  "+&r"(vaddr), "+&r"(err)
			: "i"(SIGSEGV)
			: "memory");

		MIPS_R2_STATS(stores);

		break;
	case ll_op:
		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (vaddr & 0x3) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGBUS;
			break;
		}
		if (!access_ok(VERIFY_READ, vaddr, 4)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGBUS;
			break;
		}

		if (!cpu_has_rw_llb) {
			/*
			 * An LL/SC block can't be safely emulated without
			 * a Config5/LLB availability. So it's probably time to
			 * kill our process before things get any worse. This is
			 * because Config5/LLB allows us to use ERETNC so that
			 * the LLAddr/LLB bit is not cleared when we return from
			 * an exception. MIPS R2 LL/SC instructions trap with an
			 * RI exception so once we emulate them here, we return
			 * back to userland with ERETNC. That preserves the
			 * LLAddr/LLB so the subsequent SC instruction will
			 * succeed preserving the atomic semantics of the LL/SC
			 * block. Without that, there is no safe way to emulate
			 * an LL/SC block in MIPSR2 userland.
			 */
			pr_err("Can't emulate MIPSR2 LL/SC without Config5/LLB\n");
			err = SIGKILL;
			break;
		}

		__asm__ __volatile__(
			"1:\n"
			"ll	%0, 0(%2)\n"
			"2:\n"
			".insn\n"
			".section        .fixup,\"ax\"\n"
			"3:\n"
			"li	%1, %3\n"
			"j	2b\n"
			".previous\n"
			".section        __ex_table,\"a\"\n"
			STR(PTR) " 1b,3b\n"
			".previous\n"
			: "=&r"(res), "+&r"(err)
			: "r"(vaddr), "i"(SIGSEGV)
			: "memory");

		if (MIPSInst_RT(inst) && !err)
			regs->regs[MIPSInst_RT(inst)] = res;
		MIPS_R2_STATS(llsc);

		break;

	case sc_op:
		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (vaddr & 0x3) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGBUS;
			break;
		}
		if (!access_ok(VERIFY_WRITE, vaddr, 4)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGBUS;
			break;
		}

		if (!cpu_has_rw_llb) {
			/*
			 * An LL/SC block can't be safely emulated without
			 * a Config5/LLB availability. So it's probably time to
			 * kill our process before things get any worse. This is
			 * because Config5/LLB allows us to use ERETNC so that
			 * the LLAddr/LLB bit is not cleared when we return from
			 * an exception. MIPS R2 LL/SC instructions trap with an
			 * RI exception so once we emulate them here, we return
			 * back to userland with ERETNC. That preserves the
			 * LLAddr/LLB so the subsequent SC instruction will
			 * succeed preserving the atomic semantics of the LL/SC
			 * block. Without that, there is no safe way to emulate
			 * an LL/SC block in MIPSR2 userland.
			 */
			pr_err("Can't emulate MIPSR2 LL/SC without Config5/LLB\n");
			err = SIGKILL;
			break;
		}

		res = regs->regs[MIPSInst_RT(inst)];

		__asm__ __volatile__(
			"1:\n"
			"sc	%0, 0(%2)\n"
			"2:\n"
			".insn\n"
			".section        .fixup,\"ax\"\n"
			"3:\n"
			"li	%1, %3\n"
			"j	2b\n"
			".previous\n"
			".section        __ex_table,\"a\"\n"
			STR(PTR) " 1b,3b\n"
			".previous\n"
			: "+&r"(res), "+&r"(err)
			: "r"(vaddr), "i"(SIGSEGV));

		if (MIPSInst_RT(inst) && !err)
			regs->regs[MIPSInst_RT(inst)] = res;

		MIPS_R2_STATS(llsc);

		break;

	case lld_op:
		if (IS_ENABLED(CONFIG_32BIT)) {
		    err = SIGILL;
		    break;
		}

		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (vaddr & 0x7) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGBUS;
			break;
		}
		if (!access_ok(VERIFY_READ, vaddr, 8)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGBUS;
			break;
		}

		if (!cpu_has_rw_llb) {
			/*
			 * An LL/SC block can't be safely emulated without
			 * a Config5/LLB availability. So it's probably time to
			 * kill our process before things get any worse. This is
			 * because Config5/LLB allows us to use ERETNC so that
			 * the LLAddr/LLB bit is not cleared when we return from
			 * an exception. MIPS R2 LL/SC instructions trap with an
			 * RI exception so once we emulate them here, we return
			 * back to userland with ERETNC. That preserves the
			 * LLAddr/LLB so the subsequent SC instruction will
			 * succeed preserving the atomic semantics of the LL/SC
			 * block. Without that, there is no safe way to emulate
			 * an LL/SC block in MIPSR2 userland.
			 */
			pr_err("Can't emulate MIPSR2 LL/SC without Config5/LLB\n");
			err = SIGKILL;
			break;
		}

		__asm__ __volatile__(
			"1:\n"
			"lld	%0, 0(%2)\n"
			"2:\n"
			".insn\n"
			".section        .fixup,\"ax\"\n"
			"3:\n"
			"li	%1, %3\n"
			"j	2b\n"
			".previous\n"
			".section        __ex_table,\"a\"\n"
			STR(PTR) " 1b,3b\n"
			".previous\n"
			: "=&r"(res), "+&r"(err)
			: "r"(vaddr), "i"(SIGSEGV)
			: "memory");
		if (MIPSInst_RT(inst) && !err)
			regs->regs[MIPSInst_RT(inst)] = res;

		MIPS_R2_STATS(llsc);

		break;

	case scd_op:
		if (IS_ENABLED(CONFIG_32BIT)) {
		    err = SIGILL;
		    break;
		}

		vaddr = regs->regs[MIPSInst_RS(inst)] + MIPSInst_SIMM(inst);
		if (vaddr & 0x7) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGBUS;
			break;
		}
		if (!access_ok(VERIFY_WRITE, vaddr, 8)) {
			current->thread.cp0_baduaddr = vaddr;
			err = SIGBUS;
			break;
		}

		if (!cpu_has_rw_llb) {
			/*
			 * An LL/SC block can't be safely emulated without
			 * a Config5/LLB availability. So it's probably time to
			 * kill our process before things get any worse. This is
			 * because Config5/LLB allows us to use ERETNC so that
			 * the LLAddr/LLB bit is not cleared when we return from
			 * an exception. MIPS R2 LL/SC instructions trap with an
			 * RI exception so once we emulate them here, we return
			 * back to userland with ERETNC. That preserves the
			 * LLAddr/LLB so the subsequent SC instruction will
			 * succeed preserving the atomic semantics of the LL/SC
			 * block. Without that, there is no safe way to emulate
			 * an LL/SC block in MIPSR2 userland.
			 */
			pr_err("Can't emulate MIPSR2 LL/SC without Config5/LLB\n");
			err = SIGKILL;
			break;
		}

		res = regs->regs[MIPSInst_RT(inst)];

		__asm__ __volatile__(
			"1:\n"
			"scd	%0, 0(%2)\n"
			"2:\n"
			".insn\n"
			".section        .fixup,\"ax\"\n"
			"3:\n"
			"li	%1, %3\n"
			"j	2b\n"
			".previous\n"
			".section        __ex_table,\"a\"\n"
			STR(PTR) " 1b,3b\n"
			".previous\n"
			: "+&r"(res), "+&r"(err)
			: "r"(vaddr), "i"(SIGSEGV));

		if (MIPSInst_RT(inst) && !err)
			regs->regs[MIPSInst_RT(inst)] = res;

		MIPS_R2_STATS(llsc);

		break;
	case pref_op:
		/* skip it */
		break;
	default:
		err = SIGILL;
	}

	/*
	 * Let's not return to userland just yet. It's costly and
	 * it's likely we have more R2 instructions to emulate
	 */
	if (!err && (pass++ < MIPS_R2_EMUL_TOTAL_PASS)) {
		regs->cp0_cause &= ~CAUSEF_BD;
		err = get_user(inst, (u32 __user *)regs->cp0_epc);
		if (!err)
			goto repeat;

		if (err < 0)
			err = SIGSEGV;
	}

	if (err && (err != SIGEMT)) {
		regs->regs[31] = r31;
		regs->cp0_epc = epc;
	}

	/* Likely a MIPS R6 compatible instruction */
	if (pass && (err == SIGILL))
		err = 0;

	return err;
}

#ifdef CONFIG_DEBUG_FS

static int mipsr2_stats_show(struct seq_file *s, void *unused)
{

	seq_printf(s, "Instruction\tTotal\tBDslot\n------------------------------\n");
	seq_printf(s, "movs\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.movs),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.movs));
	seq_printf(s, "hilo\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.hilo),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.hilo));
	seq_printf(s, "muls\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.muls),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.muls));
	seq_printf(s, "divs\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.divs),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.divs));
	seq_printf(s, "dsps\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.dsps),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.dsps));
	seq_printf(s, "bops\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.bops),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.bops));
	seq_printf(s, "traps\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.traps),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.traps));
	seq_printf(s, "fpus\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.fpus),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.fpus));
	seq_printf(s, "loads\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.loads),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.loads));
	seq_printf(s, "stores\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.stores),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.stores));
	seq_printf(s, "llsc\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.llsc),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.llsc));
	seq_printf(s, "dsemul\t\t%ld\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2emustats.dsemul),
		   (unsigned long)__this_cpu_read(mipsr2bdemustats.dsemul));
	seq_printf(s, "jr\t\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2bremustats.jrs));
	seq_printf(s, "bltzl\t\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2bremustats.bltzl));
	seq_printf(s, "bgezl\t\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2bremustats.bgezl));
	seq_printf(s, "bltzll\t\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2bremustats.bltzll));
	seq_printf(s, "bgezll\t\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2bremustats.bgezll));
	seq_printf(s, "bltzal\t\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2bremustats.bltzal));
	seq_printf(s, "bgezal\t\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2bremustats.bgezal));
	seq_printf(s, "beql\t\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2bremustats.beql));
	seq_printf(s, "bnel\t\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2bremustats.bnel));
	seq_printf(s, "blezl\t\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2bremustats.blezl));
	seq_printf(s, "bgtzl\t\t%ld\n",
		   (unsigned long)__this_cpu_read(mipsr2bremustats.bgtzl));

	return 0;
}

static int mipsr2_stats_clear_show(struct seq_file *s, void *unused)
{
	mipsr2_stats_show(s, unused);

	__this_cpu_write((mipsr2emustats).movs, 0);
	__this_cpu_write((mipsr2bdemustats).movs, 0);
	__this_cpu_write((mipsr2emustats).hilo, 0);
	__this_cpu_write((mipsr2bdemustats).hilo, 0);
	__this_cpu_write((mipsr2emustats).muls, 0);
	__this_cpu_write((mipsr2bdemustats).muls, 0);
	__this_cpu_write((mipsr2emustats).divs, 0);
	__this_cpu_write((mipsr2bdemustats).divs, 0);
	__this_cpu_write((mipsr2emustats).dsps, 0);
	__this_cpu_write((mipsr2bdemustats).dsps, 0);
	__this_cpu_write((mipsr2emustats).bops, 0);
	__this_cpu_write((mipsr2bdemustats).bops, 0);
	__this_cpu_write((mipsr2emustats).traps, 0);
	__this_cpu_write((mipsr2bdemustats).traps, 0);
	__this_cpu_write((mipsr2emustats).fpus, 0);
	__this_cpu_write((mipsr2bdemustats).fpus, 0);
	__this_cpu_write((mipsr2emustats).loads, 0);
	__this_cpu_write((mipsr2bdemustats).loads, 0);
	__this_cpu_write((mipsr2emustats).stores, 0);
	__this_cpu_write((mipsr2bdemustats).stores, 0);
	__this_cpu_write((mipsr2emustats).llsc, 0);
	__this_cpu_write((mipsr2bdemustats).llsc, 0);
	__this_cpu_write((mipsr2emustats).dsemul, 0);
	__this_cpu_write((mipsr2bdemustats).dsemul, 0);
	__this_cpu_write((mipsr2bremustats).jrs, 0);
	__this_cpu_write((mipsr2bremustats).bltzl, 0);
	__this_cpu_write((mipsr2bremustats).bgezl, 0);
	__this_cpu_write((mipsr2bremustats).bltzll, 0);
	__this_cpu_write((mipsr2bremustats).bgezll, 0);
	__this_cpu_write((mipsr2bremustats).bltzal, 0);
	__this_cpu_write((mipsr2bremustats).bgezal, 0);
	__this_cpu_write((mipsr2bremustats).beql, 0);
	__this_cpu_write((mipsr2bremustats).bnel, 0);
	__this_cpu_write((mipsr2bremustats).blezl, 0);
	__this_cpu_write((mipsr2bremustats).bgtzl, 0);

	return 0;
}

static int mipsr2_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, mipsr2_stats_show, inode->i_private);
}

static int mipsr2_stats_clear_open(struct inode *inode, struct file *file)
{
	return single_open(file, mipsr2_stats_clear_show, inode->i_private);
}

static const struct file_operations mipsr2_emul_fops = {
	.open                   = mipsr2_stats_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static const struct file_operations mipsr2_clear_fops = {
	.open                   = mipsr2_stats_clear_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};


static int __init mipsr2_init_debugfs(void)
{
	struct dentry		*mipsr2_emul;

	if (!mips_debugfs_dir)
		return -ENODEV;

	mipsr2_emul = debugfs_create_file("r2_emul_stats", S_IRUGO,
					  mips_debugfs_dir, NULL,
					  &mipsr2_emul_fops);
	if (!mipsr2_emul)
		return -ENOMEM;

	mipsr2_emul = debugfs_create_file("r2_emul_stats_clear", S_IRUGO,
					  mips_debugfs_dir, NULL,
					  &mipsr2_clear_fops);
	if (!mipsr2_emul)
		return -ENOMEM;

	return 0;
}

device_initcall(mipsr2_init_debugfs);

#endif /* CONFIG_DEBUG_FS */
