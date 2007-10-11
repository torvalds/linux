/*
 * cp1emu.c: a MIPS coprocessor 1 (fpu) instruction emulator
 *
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 * http://www.algor.co.uk
 *
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000  MIPS Technologies, Inc.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * A complete emulator for MIPS coprocessor 1 instructions.  This is
 * required for #float(switch) or #float(trap), where it catches all
 * COP1 instructions via the "CoProcessor Unusable" exception.
 *
 * More surprisingly it is also required for #float(ieee), to help out
 * the hardware fpu at the boundaries of the IEEE-754 representation
 * (denormalised values, infinities, underflow, etc).  It is made
 * quite nasty because emulation of some non-COP1 instructions is
 * required, e.g. in branch delay slots.
 *
 * Note if you know that you won't have an fpu, then you'll get much
 * better performance by compiling with -msoft-float!
 */
#include <linux/sched.h>
#include <linux/debugfs.h>

#include <asm/inst.h>
#include <asm/bootinfo.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/mipsregs.h>
#include <asm/fpu_emulator.h>
#include <asm/uaccess.h>
#include <asm/branch.h>

#include "ieee754.h"
#include "dsemul.h"

/* Strap kernel emulator for full MIPS IV emulation */

#ifdef __mips
#undef __mips
#endif
#define __mips 4

/* Function which emulates a floating point instruction. */

static int fpu_emu(struct pt_regs *, struct mips_fpu_struct *,
	mips_instruction);

#if __mips >= 4 && __mips != 32
static int fpux_emu(struct pt_regs *,
	struct mips_fpu_struct *, mips_instruction);
#endif

/* Further private data for which no space exists in mips_fpu_struct */

struct mips_fpu_emulator_stats fpuemustats;

/* Control registers */

#define FPCREG_RID	0	/* $0  = revision id */
#define FPCREG_CSR	31	/* $31 = csr */

/* Convert Mips rounding mode (0..3) to IEEE library modes. */
static const unsigned char ieee_rm[4] = {
	[FPU_CSR_RN] = IEEE754_RN,
	[FPU_CSR_RZ] = IEEE754_RZ,
	[FPU_CSR_RU] = IEEE754_RU,
	[FPU_CSR_RD] = IEEE754_RD,
};
/* Convert IEEE library modes to Mips rounding mode (0..3). */
static const unsigned char mips_rm[4] = {
	[IEEE754_RN] = FPU_CSR_RN,
	[IEEE754_RZ] = FPU_CSR_RZ,
	[IEEE754_RD] = FPU_CSR_RD,
	[IEEE754_RU] = FPU_CSR_RU,
};

#if __mips >= 4
/* convert condition code register number to csr bit */
static const unsigned int fpucondbit[8] = {
	FPU_CSR_COND0,
	FPU_CSR_COND1,
	FPU_CSR_COND2,
	FPU_CSR_COND3,
	FPU_CSR_COND4,
	FPU_CSR_COND5,
	FPU_CSR_COND6,
	FPU_CSR_COND7
};
#endif


/*
 * Redundant with logic already in kernel/branch.c,
 * embedded in compute_return_epc.  At some point,
 * a single subroutine should be used across both
 * modules.
 */
static int isBranchInstr(mips_instruction * i)
{
	switch (MIPSInst_OPCODE(*i)) {
	case spec_op:
		switch (MIPSInst_FUNC(*i)) {
		case jalr_op:
		case jr_op:
			return 1;
		}
		break;

	case bcond_op:
		switch (MIPSInst_RT(*i)) {
		case bltz_op:
		case bgez_op:
		case bltzl_op:
		case bgezl_op:
		case bltzal_op:
		case bgezal_op:
		case bltzall_op:
		case bgezall_op:
			return 1;
		}
		break;

	case j_op:
	case jal_op:
	case jalx_op:
	case beq_op:
	case bne_op:
	case blez_op:
	case bgtz_op:
	case beql_op:
	case bnel_op:
	case blezl_op:
	case bgtzl_op:
		return 1;

	case cop0_op:
	case cop1_op:
	case cop2_op:
	case cop1x_op:
		if (MIPSInst_RS(*i) == bc_op)
			return 1;
		break;
	}

	return 0;
}

/*
 * In the Linux kernel, we support selection of FPR format on the
 * basis of the Status.FR bit.  This does imply that, if a full 32
 * FPRs are desired, there needs to be a flip-flop that can be written
 * to one at that bit position.  In any case, O32 MIPS ABI uses
 * only the even FPRs (Status.FR = 0).
 */

#define CP0_STATUS_FR_SUPPORT

#ifdef CP0_STATUS_FR_SUPPORT
#define FR_BIT ST0_FR
#else
#define FR_BIT 0
#endif

#define SIFROMREG(si, x) ((si) = \
			(xcp->cp0_status & FR_BIT) || !(x & 1) ? \
			(int)ctx->fpr[x] : \
			(int)(ctx->fpr[x & ~1] >> 32 ))
#define SITOREG(si, x)	(ctx->fpr[x & ~((xcp->cp0_status & FR_BIT) == 0)] = \
			(xcp->cp0_status & FR_BIT) || !(x & 1) ? \
			ctx->fpr[x & ~1] >> 32 << 32 | (u32)(si) : \
			ctx->fpr[x & ~1] << 32 >> 32 | (u64)(si) << 32)

#define DIFROMREG(di, x) ((di) = \
			ctx->fpr[x & ~((xcp->cp0_status & FR_BIT) == 0)])
#define DITOREG(di, x)	(ctx->fpr[x & ~((xcp->cp0_status & FR_BIT) == 0)] \
			= (di))

#define SPFROMREG(sp, x) SIFROMREG((sp).bits, x)
#define SPTOREG(sp, x)	SITOREG((sp).bits, x)
#define DPFROMREG(dp, x)	DIFROMREG((dp).bits, x)
#define DPTOREG(dp, x)	DITOREG((dp).bits, x)

/*
 * Emulate the single floating point instruction pointed at by EPC.
 * Two instructions if the instruction is in a branch delay slot.
 */

static int cop1Emulate(struct pt_regs *xcp, struct mips_fpu_struct *ctx)
{
	mips_instruction ir;
	unsigned long emulpc, contpc;
	unsigned int cond;

	if (get_user(ir, (mips_instruction __user *) xcp->cp0_epc)) {
		fpuemustats.errors++;
		return SIGBUS;
	}

	/* XXX NEC Vr54xx bug workaround */
	if ((xcp->cp0_cause & CAUSEF_BD) && !isBranchInstr(&ir))
		xcp->cp0_cause &= ~CAUSEF_BD;

	if (xcp->cp0_cause & CAUSEF_BD) {
		/*
		 * The instruction to be emulated is in a branch delay slot
		 * which means that we have to  emulate the branch instruction
		 * BEFORE we do the cop1 instruction.
		 *
		 * This branch could be a COP1 branch, but in that case we
		 * would have had a trap for that instruction, and would not
		 * come through this route.
		 *
		 * Linux MIPS branch emulator operates on context, updating the
		 * cp0_epc.
		 */
		emulpc = xcp->cp0_epc + 4;	/* Snapshot emulation target */

		if (__compute_return_epc(xcp)) {
#ifdef CP1DBG
			printk("failed to emulate branch at %p\n",
				(void *) (xcp->cp0_epc));
#endif
			return SIGILL;
		}
		if (get_user(ir, (mips_instruction __user *) emulpc)) {
			fpuemustats.errors++;
			return SIGBUS;
		}
		/* __compute_return_epc() will have updated cp0_epc */
		contpc = xcp->cp0_epc;
		/* In order not to confuse ptrace() et al, tweak context */
		xcp->cp0_epc = emulpc - 4;
	} else {
		emulpc = xcp->cp0_epc;
		contpc = xcp->cp0_epc + 4;
	}

      emul:
	fpuemustats.emulated++;
	switch (MIPSInst_OPCODE(ir)) {
	case ldc1_op:{
		u64 __user *va = (u64 __user *) (xcp->regs[MIPSInst_RS(ir)] +
			MIPSInst_SIMM(ir));
		u64 val;

		fpuemustats.loads++;
		if (get_user(val, va)) {
			fpuemustats.errors++;
			return SIGBUS;
		}
		DITOREG(val, MIPSInst_RT(ir));
		break;
	}

	case sdc1_op:{
		u64 __user *va = (u64 __user *) (xcp->regs[MIPSInst_RS(ir)] +
			MIPSInst_SIMM(ir));
		u64 val;

		fpuemustats.stores++;
		DIFROMREG(val, MIPSInst_RT(ir));
		if (put_user(val, va)) {
			fpuemustats.errors++;
			return SIGBUS;
		}
		break;
	}

	case lwc1_op:{
		u32 __user *va = (u32 __user *) (xcp->regs[MIPSInst_RS(ir)] +
			MIPSInst_SIMM(ir));
		u32 val;

		fpuemustats.loads++;
		if (get_user(val, va)) {
			fpuemustats.errors++;
			return SIGBUS;
		}
		SITOREG(val, MIPSInst_RT(ir));
		break;
	}

	case swc1_op:{
		u32 __user *va = (u32 __user *) (xcp->regs[MIPSInst_RS(ir)] +
			MIPSInst_SIMM(ir));
		u32 val;

		fpuemustats.stores++;
		SIFROMREG(val, MIPSInst_RT(ir));
		if (put_user(val, va)) {
			fpuemustats.errors++;
			return SIGBUS;
		}
		break;
	}

	case cop1_op:
		switch (MIPSInst_RS(ir)) {

#if defined(__mips64)
		case dmfc_op:
			/* copregister fs -> gpr[rt] */
			if (MIPSInst_RT(ir) != 0) {
				DIFROMREG(xcp->regs[MIPSInst_RT(ir)],
					MIPSInst_RD(ir));
			}
			break;

		case dmtc_op:
			/* copregister fs <- rt */
			DITOREG(xcp->regs[MIPSInst_RT(ir)], MIPSInst_RD(ir));
			break;
#endif

		case mfc_op:
			/* copregister rd -> gpr[rt] */
			if (MIPSInst_RT(ir) != 0) {
				SIFROMREG(xcp->regs[MIPSInst_RT(ir)],
					MIPSInst_RD(ir));
			}
			break;

		case mtc_op:
			/* copregister rd <- rt */
			SITOREG(xcp->regs[MIPSInst_RT(ir)], MIPSInst_RD(ir));
			break;

		case cfc_op:{
			/* cop control register rd -> gpr[rt] */
			u32 value;

			if (ir == CP1UNDEF) {
				return do_dsemulret(xcp);
			}
			if (MIPSInst_RD(ir) == FPCREG_CSR) {
				value = ctx->fcr31;
				value = (value & ~0x3) | mips_rm[value & 0x3];
#ifdef CSRTRACE
				printk("%p gpr[%d]<-csr=%08x\n",
					(void *) (xcp->cp0_epc),
					MIPSInst_RT(ir), value);
#endif
			}
			else if (MIPSInst_RD(ir) == FPCREG_RID)
				value = 0;
			else
				value = 0;
			if (MIPSInst_RT(ir))
				xcp->regs[MIPSInst_RT(ir)] = value;
			break;
		}

		case ctc_op:{
			/* copregister rd <- rt */
			u32 value;

			if (MIPSInst_RT(ir) == 0)
				value = 0;
			else
				value = xcp->regs[MIPSInst_RT(ir)];

			/* we only have one writable control reg
			 */
			if (MIPSInst_RD(ir) == FPCREG_CSR) {
#ifdef CSRTRACE
				printk("%p gpr[%d]->csr=%08x\n",
					(void *) (xcp->cp0_epc),
					MIPSInst_RT(ir), value);
#endif
				value &= (FPU_CSR_FLUSH | FPU_CSR_ALL_E | FPU_CSR_ALL_S | 0x03);
				ctx->fcr31 &= ~(FPU_CSR_FLUSH | FPU_CSR_ALL_E | FPU_CSR_ALL_S | 0x03);
				/* convert to ieee library modes */
				ctx->fcr31 |= (value & ~0x3) | ieee_rm[value & 0x3];
			}
			if ((ctx->fcr31 >> 5) & ctx->fcr31 & FPU_CSR_ALL_E) {
				return SIGFPE;
			}
			break;
		}

		case bc_op:{
			int likely = 0;

			if (xcp->cp0_cause & CAUSEF_BD)
				return SIGILL;

#if __mips >= 4
			cond = ctx->fcr31 & fpucondbit[MIPSInst_RT(ir) >> 2];
#else
			cond = ctx->fcr31 & FPU_CSR_COND;
#endif
			switch (MIPSInst_RT(ir) & 3) {
			case bcfl_op:
				likely = 1;
			case bcf_op:
				cond = !cond;
				break;
			case bctl_op:
				likely = 1;
			case bct_op:
				break;
			default:
				/* thats an illegal instruction */
				return SIGILL;
			}

			xcp->cp0_cause |= CAUSEF_BD;
			if (cond) {
				/* branch taken: emulate dslot
				 * instruction
				 */
				xcp->cp0_epc += 4;
				contpc = (xcp->cp0_epc +
					(MIPSInst_SIMM(ir) << 2));

				if (get_user(ir,
				    (mips_instruction __user *) xcp->cp0_epc)) {
					fpuemustats.errors++;
					return SIGBUS;
				}

				switch (MIPSInst_OPCODE(ir)) {
				case lwc1_op:
				case swc1_op:
#if (__mips >= 2 || defined(__mips64))
				case ldc1_op:
				case sdc1_op:
#endif
				case cop1_op:
#if __mips >= 4 && __mips != 32
				case cop1x_op:
#endif
					/* its one of ours */
					goto emul;
#if __mips >= 4
				case spec_op:
					if (MIPSInst_FUNC(ir) == movc_op)
						goto emul;
					break;
#endif
				}

				/*
				 * Single step the non-cp1
				 * instruction in the dslot
				 */
				return mips_dsemul(xcp, ir, contpc);
			}
			else {
				/* branch not taken */
				if (likely) {
					/*
					 * branch likely nullifies
					 * dslot if not taken
					 */
					xcp->cp0_epc += 4;
					contpc += 4;
					/*
					 * else continue & execute
					 * dslot as normal insn
					 */
				}
			}
			break;
		}

		default:
			if (!(MIPSInst_RS(ir) & 0x10))
				return SIGILL;
			{
				int sig;

				/* a real fpu computation instruction */
				if ((sig = fpu_emu(xcp, ctx, ir)))
					return sig;
			}
		}
		break;

#if __mips >= 4 && __mips != 32
	case cop1x_op:{
		int sig;

		if ((sig = fpux_emu(xcp, ctx, ir)))
			return sig;
		break;
	}
#endif

#if __mips >= 4
	case spec_op:
		if (MIPSInst_FUNC(ir) != movc_op)
			return SIGILL;
		cond = fpucondbit[MIPSInst_RT(ir) >> 2];
		if (((ctx->fcr31 & cond) != 0) == ((MIPSInst_RT(ir) & 1) != 0))
			xcp->regs[MIPSInst_RD(ir)] =
				xcp->regs[MIPSInst_RS(ir)];
		break;
#endif

	default:
		return SIGILL;
	}

	/* we did it !! */
	xcp->cp0_epc = contpc;
	xcp->cp0_cause &= ~CAUSEF_BD;

	return 0;
}

/*
 * Conversion table from MIPS compare ops 48-63
 * cond = ieee754dp_cmp(x,y,IEEE754_UN,sig);
 */
static const unsigned char cmptab[8] = {
	0,			/* cmp_0 (sig) cmp_sf */
	IEEE754_CUN,		/* cmp_un (sig) cmp_ngle */
	IEEE754_CEQ,		/* cmp_eq (sig) cmp_seq */
	IEEE754_CEQ | IEEE754_CUN,	/* cmp_ueq (sig) cmp_ngl  */
	IEEE754_CLT,		/* cmp_olt (sig) cmp_lt */
	IEEE754_CLT | IEEE754_CUN,	/* cmp_ult (sig) cmp_nge */
	IEEE754_CLT | IEEE754_CEQ,	/* cmp_ole (sig) cmp_le */
	IEEE754_CLT | IEEE754_CEQ | IEEE754_CUN,	/* cmp_ule (sig) cmp_ngt */
};


#if __mips >= 4 && __mips != 32

/*
 * Additional MIPS4 instructions
 */

#define DEF3OP(name, p, f1, f2, f3) \
static ieee754##p fpemu_##p##_##name(ieee754##p r, ieee754##p s, \
    ieee754##p t) \
{ \
	struct _ieee754_csr ieee754_csr_save; \
	s = f1(s, t); \
	ieee754_csr_save = ieee754_csr; \
	s = f2(s, r); \
	ieee754_csr_save.cx |= ieee754_csr.cx; \
	ieee754_csr_save.sx |= ieee754_csr.sx; \
	s = f3(s); \
	ieee754_csr.cx |= ieee754_csr_save.cx; \
	ieee754_csr.sx |= ieee754_csr_save.sx; \
	return s; \
}

static ieee754dp fpemu_dp_recip(ieee754dp d)
{
	return ieee754dp_div(ieee754dp_one(0), d);
}

static ieee754dp fpemu_dp_rsqrt(ieee754dp d)
{
	return ieee754dp_div(ieee754dp_one(0), ieee754dp_sqrt(d));
}

static ieee754sp fpemu_sp_recip(ieee754sp s)
{
	return ieee754sp_div(ieee754sp_one(0), s);
}

static ieee754sp fpemu_sp_rsqrt(ieee754sp s)
{
	return ieee754sp_div(ieee754sp_one(0), ieee754sp_sqrt(s));
}

DEF3OP(madd, sp, ieee754sp_mul, ieee754sp_add, );
DEF3OP(msub, sp, ieee754sp_mul, ieee754sp_sub, );
DEF3OP(nmadd, sp, ieee754sp_mul, ieee754sp_add, ieee754sp_neg);
DEF3OP(nmsub, sp, ieee754sp_mul, ieee754sp_sub, ieee754sp_neg);
DEF3OP(madd, dp, ieee754dp_mul, ieee754dp_add, );
DEF3OP(msub, dp, ieee754dp_mul, ieee754dp_sub, );
DEF3OP(nmadd, dp, ieee754dp_mul, ieee754dp_add, ieee754dp_neg);
DEF3OP(nmsub, dp, ieee754dp_mul, ieee754dp_sub, ieee754dp_neg);

static int fpux_emu(struct pt_regs *xcp, struct mips_fpu_struct *ctx,
	mips_instruction ir)
{
	unsigned rcsr = 0;	/* resulting csr */

	fpuemustats.cp1xops++;

	switch (MIPSInst_FMA_FFMT(ir)) {
	case s_fmt:{		/* 0 */

		ieee754sp(*handler) (ieee754sp, ieee754sp, ieee754sp);
		ieee754sp fd, fr, fs, ft;
		u32 __user *va;
		u32 val;

		switch (MIPSInst_FUNC(ir)) {
		case lwxc1_op:
			va = (void __user *) (xcp->regs[MIPSInst_FR(ir)] +
				xcp->regs[MIPSInst_FT(ir)]);

			fpuemustats.loads++;
			if (get_user(val, va)) {
				fpuemustats.errors++;
				return SIGBUS;
			}
			SITOREG(val, MIPSInst_FD(ir));
			break;

		case swxc1_op:
			va = (void __user *) (xcp->regs[MIPSInst_FR(ir)] +
				xcp->regs[MIPSInst_FT(ir)]);

			fpuemustats.stores++;

			SIFROMREG(val, MIPSInst_FS(ir));
			if (put_user(val, va)) {
				fpuemustats.errors++;
				return SIGBUS;
			}
			break;

		case madd_s_op:
			handler = fpemu_sp_madd;
			goto scoptop;
		case msub_s_op:
			handler = fpemu_sp_msub;
			goto scoptop;
		case nmadd_s_op:
			handler = fpemu_sp_nmadd;
			goto scoptop;
		case nmsub_s_op:
			handler = fpemu_sp_nmsub;
			goto scoptop;

		      scoptop:
			SPFROMREG(fr, MIPSInst_FR(ir));
			SPFROMREG(fs, MIPSInst_FS(ir));
			SPFROMREG(ft, MIPSInst_FT(ir));
			fd = (*handler) (fr, fs, ft);
			SPTOREG(fd, MIPSInst_FD(ir));

		      copcsr:
			if (ieee754_cxtest(IEEE754_INEXACT))
				rcsr |= FPU_CSR_INE_X | FPU_CSR_INE_S;
			if (ieee754_cxtest(IEEE754_UNDERFLOW))
				rcsr |= FPU_CSR_UDF_X | FPU_CSR_UDF_S;
			if (ieee754_cxtest(IEEE754_OVERFLOW))
				rcsr |= FPU_CSR_OVF_X | FPU_CSR_OVF_S;
			if (ieee754_cxtest(IEEE754_INVALID_OPERATION))
				rcsr |= FPU_CSR_INV_X | FPU_CSR_INV_S;

			ctx->fcr31 = (ctx->fcr31 & ~FPU_CSR_ALL_X) | rcsr;
			if ((ctx->fcr31 >> 5) & ctx->fcr31 & FPU_CSR_ALL_E) {
				/*printk ("SIGFPE: fpu csr = %08x\n",
				   ctx->fcr31); */
				return SIGFPE;
			}

			break;

		default:
			return SIGILL;
		}
		break;
	}

	case d_fmt:{		/* 1 */
		ieee754dp(*handler) (ieee754dp, ieee754dp, ieee754dp);
		ieee754dp fd, fr, fs, ft;
		u64 __user *va;
		u64 val;

		switch (MIPSInst_FUNC(ir)) {
		case ldxc1_op:
			va = (void __user *) (xcp->regs[MIPSInst_FR(ir)] +
				xcp->regs[MIPSInst_FT(ir)]);

			fpuemustats.loads++;
			if (get_user(val, va)) {
				fpuemustats.errors++;
				return SIGBUS;
			}
			DITOREG(val, MIPSInst_FD(ir));
			break;

		case sdxc1_op:
			va = (void __user *) (xcp->regs[MIPSInst_FR(ir)] +
				xcp->regs[MIPSInst_FT(ir)]);

			fpuemustats.stores++;
			DIFROMREG(val, MIPSInst_FS(ir));
			if (put_user(val, va)) {
				fpuemustats.errors++;
				return SIGBUS;
			}
			break;

		case madd_d_op:
			handler = fpemu_dp_madd;
			goto dcoptop;
		case msub_d_op:
			handler = fpemu_dp_msub;
			goto dcoptop;
		case nmadd_d_op:
			handler = fpemu_dp_nmadd;
			goto dcoptop;
		case nmsub_d_op:
			handler = fpemu_dp_nmsub;
			goto dcoptop;

		      dcoptop:
			DPFROMREG(fr, MIPSInst_FR(ir));
			DPFROMREG(fs, MIPSInst_FS(ir));
			DPFROMREG(ft, MIPSInst_FT(ir));
			fd = (*handler) (fr, fs, ft);
			DPTOREG(fd, MIPSInst_FD(ir));
			goto copcsr;

		default:
			return SIGILL;
		}
		break;
	}

	case 0x7:		/* 7 */
		if (MIPSInst_FUNC(ir) != pfetch_op) {
			return SIGILL;
		}
		/* ignore prefx operation */
		break;

	default:
		return SIGILL;
	}

	return 0;
}
#endif



/*
 * Emulate a single COP1 arithmetic instruction.
 */
static int fpu_emu(struct pt_regs *xcp, struct mips_fpu_struct *ctx,
	mips_instruction ir)
{
	int rfmt;		/* resulting format */
	unsigned rcsr = 0;	/* resulting csr */
	unsigned cond;
	union {
		ieee754dp d;
		ieee754sp s;
		int w;
#ifdef __mips64
		s64 l;
#endif
	} rv;			/* resulting value */

	fpuemustats.cp1ops++;
	switch (rfmt = (MIPSInst_FFMT(ir) & 0xf)) {
	case s_fmt:{		/* 0 */
		union {
			ieee754sp(*b) (ieee754sp, ieee754sp);
			ieee754sp(*u) (ieee754sp);
		} handler;

		switch (MIPSInst_FUNC(ir)) {
			/* binary ops */
		case fadd_op:
			handler.b = ieee754sp_add;
			goto scopbop;
		case fsub_op:
			handler.b = ieee754sp_sub;
			goto scopbop;
		case fmul_op:
			handler.b = ieee754sp_mul;
			goto scopbop;
		case fdiv_op:
			handler.b = ieee754sp_div;
			goto scopbop;

			/* unary  ops */
#if __mips >= 2 || defined(__mips64)
		case fsqrt_op:
			handler.u = ieee754sp_sqrt;
			goto scopuop;
#endif
#if __mips >= 4 && __mips != 32
		case frsqrt_op:
			handler.u = fpemu_sp_rsqrt;
			goto scopuop;
		case frecip_op:
			handler.u = fpemu_sp_recip;
			goto scopuop;
#endif
#if __mips >= 4
		case fmovc_op:
			cond = fpucondbit[MIPSInst_FT(ir) >> 2];
			if (((ctx->fcr31 & cond) != 0) !=
				((MIPSInst_FT(ir) & 1) != 0))
				return 0;
			SPFROMREG(rv.s, MIPSInst_FS(ir));
			break;
		case fmovz_op:
			if (xcp->regs[MIPSInst_FT(ir)] != 0)
				return 0;
			SPFROMREG(rv.s, MIPSInst_FS(ir));
			break;
		case fmovn_op:
			if (xcp->regs[MIPSInst_FT(ir)] == 0)
				return 0;
			SPFROMREG(rv.s, MIPSInst_FS(ir));
			break;
#endif
		case fabs_op:
			handler.u = ieee754sp_abs;
			goto scopuop;
		case fneg_op:
			handler.u = ieee754sp_neg;
			goto scopuop;
		case fmov_op:
			/* an easy one */
			SPFROMREG(rv.s, MIPSInst_FS(ir));
			goto copcsr;

			/* binary op on handler */
		      scopbop:
			{
				ieee754sp fs, ft;

				SPFROMREG(fs, MIPSInst_FS(ir));
				SPFROMREG(ft, MIPSInst_FT(ir));

				rv.s = (*handler.b) (fs, ft);
				goto copcsr;
			}
		      scopuop:
			{
				ieee754sp fs;

				SPFROMREG(fs, MIPSInst_FS(ir));
				rv.s = (*handler.u) (fs);
				goto copcsr;
			}
		      copcsr:
			if (ieee754_cxtest(IEEE754_INEXACT))
				rcsr |= FPU_CSR_INE_X | FPU_CSR_INE_S;
			if (ieee754_cxtest(IEEE754_UNDERFLOW))
				rcsr |= FPU_CSR_UDF_X | FPU_CSR_UDF_S;
			if (ieee754_cxtest(IEEE754_OVERFLOW))
				rcsr |= FPU_CSR_OVF_X | FPU_CSR_OVF_S;
			if (ieee754_cxtest(IEEE754_ZERO_DIVIDE))
				rcsr |= FPU_CSR_DIV_X | FPU_CSR_DIV_S;
			if (ieee754_cxtest(IEEE754_INVALID_OPERATION))
				rcsr |= FPU_CSR_INV_X | FPU_CSR_INV_S;
			break;

			/* unary conv ops */
		case fcvts_op:
			return SIGILL;	/* not defined */
		case fcvtd_op:{
			ieee754sp fs;

			SPFROMREG(fs, MIPSInst_FS(ir));
			rv.d = ieee754dp_fsp(fs);
			rfmt = d_fmt;
			goto copcsr;
		}
		case fcvtw_op:{
			ieee754sp fs;

			SPFROMREG(fs, MIPSInst_FS(ir));
			rv.w = ieee754sp_tint(fs);
			rfmt = w_fmt;
			goto copcsr;
		}

#if __mips >= 2 || defined(__mips64)
		case fround_op:
		case ftrunc_op:
		case fceil_op:
		case ffloor_op:{
			unsigned int oldrm = ieee754_csr.rm;
			ieee754sp fs;

			SPFROMREG(fs, MIPSInst_FS(ir));
			ieee754_csr.rm = ieee_rm[MIPSInst_FUNC(ir) & 0x3];
			rv.w = ieee754sp_tint(fs);
			ieee754_csr.rm = oldrm;
			rfmt = w_fmt;
			goto copcsr;
		}
#endif /* __mips >= 2 */

#if defined(__mips64)
		case fcvtl_op:{
			ieee754sp fs;

			SPFROMREG(fs, MIPSInst_FS(ir));
			rv.l = ieee754sp_tlong(fs);
			rfmt = l_fmt;
			goto copcsr;
		}

		case froundl_op:
		case ftruncl_op:
		case fceill_op:
		case ffloorl_op:{
			unsigned int oldrm = ieee754_csr.rm;
			ieee754sp fs;

			SPFROMREG(fs, MIPSInst_FS(ir));
			ieee754_csr.rm = ieee_rm[MIPSInst_FUNC(ir) & 0x3];
			rv.l = ieee754sp_tlong(fs);
			ieee754_csr.rm = oldrm;
			rfmt = l_fmt;
			goto copcsr;
		}
#endif /* defined(__mips64) */

		default:
			if (MIPSInst_FUNC(ir) >= fcmp_op) {
				unsigned cmpop = MIPSInst_FUNC(ir) - fcmp_op;
				ieee754sp fs, ft;

				SPFROMREG(fs, MIPSInst_FS(ir));
				SPFROMREG(ft, MIPSInst_FT(ir));
				rv.w = ieee754sp_cmp(fs, ft,
					cmptab[cmpop & 0x7], cmpop & 0x8);
				rfmt = -1;
				if ((cmpop & 0x8) && ieee754_cxtest
					(IEEE754_INVALID_OPERATION))
					rcsr = FPU_CSR_INV_X | FPU_CSR_INV_S;
				else
					goto copcsr;

			}
			else {
				return SIGILL;
			}
			break;
		}
		break;
	}

	case d_fmt:{
		union {
			ieee754dp(*b) (ieee754dp, ieee754dp);
			ieee754dp(*u) (ieee754dp);
		} handler;

		switch (MIPSInst_FUNC(ir)) {
			/* binary ops */
		case fadd_op:
			handler.b = ieee754dp_add;
			goto dcopbop;
		case fsub_op:
			handler.b = ieee754dp_sub;
			goto dcopbop;
		case fmul_op:
			handler.b = ieee754dp_mul;
			goto dcopbop;
		case fdiv_op:
			handler.b = ieee754dp_div;
			goto dcopbop;

			/* unary  ops */
#if __mips >= 2 || defined(__mips64)
		case fsqrt_op:
			handler.u = ieee754dp_sqrt;
			goto dcopuop;
#endif
#if __mips >= 4 && __mips != 32
		case frsqrt_op:
			handler.u = fpemu_dp_rsqrt;
			goto dcopuop;
		case frecip_op:
			handler.u = fpemu_dp_recip;
			goto dcopuop;
#endif
#if __mips >= 4
		case fmovc_op:
			cond = fpucondbit[MIPSInst_FT(ir) >> 2];
			if (((ctx->fcr31 & cond) != 0) !=
				((MIPSInst_FT(ir) & 1) != 0))
				return 0;
			DPFROMREG(rv.d, MIPSInst_FS(ir));
			break;
		case fmovz_op:
			if (xcp->regs[MIPSInst_FT(ir)] != 0)
				return 0;
			DPFROMREG(rv.d, MIPSInst_FS(ir));
			break;
		case fmovn_op:
			if (xcp->regs[MIPSInst_FT(ir)] == 0)
				return 0;
			DPFROMREG(rv.d, MIPSInst_FS(ir));
			break;
#endif
		case fabs_op:
			handler.u = ieee754dp_abs;
			goto dcopuop;

		case fneg_op:
			handler.u = ieee754dp_neg;
			goto dcopuop;

		case fmov_op:
			/* an easy one */
			DPFROMREG(rv.d, MIPSInst_FS(ir));
			goto copcsr;

			/* binary op on handler */
		      dcopbop:{
				ieee754dp fs, ft;

				DPFROMREG(fs, MIPSInst_FS(ir));
				DPFROMREG(ft, MIPSInst_FT(ir));

				rv.d = (*handler.b) (fs, ft);
				goto copcsr;
			}
		      dcopuop:{
				ieee754dp fs;

				DPFROMREG(fs, MIPSInst_FS(ir));
				rv.d = (*handler.u) (fs);
				goto copcsr;
			}

			/* unary conv ops */
		case fcvts_op:{
			ieee754dp fs;

			DPFROMREG(fs, MIPSInst_FS(ir));
			rv.s = ieee754sp_fdp(fs);
			rfmt = s_fmt;
			goto copcsr;
		}
		case fcvtd_op:
			return SIGILL;	/* not defined */

		case fcvtw_op:{
			ieee754dp fs;

			DPFROMREG(fs, MIPSInst_FS(ir));
			rv.w = ieee754dp_tint(fs);	/* wrong */
			rfmt = w_fmt;
			goto copcsr;
		}

#if __mips >= 2 || defined(__mips64)
		case fround_op:
		case ftrunc_op:
		case fceil_op:
		case ffloor_op:{
			unsigned int oldrm = ieee754_csr.rm;
			ieee754dp fs;

			DPFROMREG(fs, MIPSInst_FS(ir));
			ieee754_csr.rm = ieee_rm[MIPSInst_FUNC(ir) & 0x3];
			rv.w = ieee754dp_tint(fs);
			ieee754_csr.rm = oldrm;
			rfmt = w_fmt;
			goto copcsr;
		}
#endif

#if defined(__mips64)
		case fcvtl_op:{
			ieee754dp fs;

			DPFROMREG(fs, MIPSInst_FS(ir));
			rv.l = ieee754dp_tlong(fs);
			rfmt = l_fmt;
			goto copcsr;
		}

		case froundl_op:
		case ftruncl_op:
		case fceill_op:
		case ffloorl_op:{
			unsigned int oldrm = ieee754_csr.rm;
			ieee754dp fs;

			DPFROMREG(fs, MIPSInst_FS(ir));
			ieee754_csr.rm = ieee_rm[MIPSInst_FUNC(ir) & 0x3];
			rv.l = ieee754dp_tlong(fs);
			ieee754_csr.rm = oldrm;
			rfmt = l_fmt;
			goto copcsr;
		}
#endif /* __mips >= 3 */

		default:
			if (MIPSInst_FUNC(ir) >= fcmp_op) {
				unsigned cmpop = MIPSInst_FUNC(ir) - fcmp_op;
				ieee754dp fs, ft;

				DPFROMREG(fs, MIPSInst_FS(ir));
				DPFROMREG(ft, MIPSInst_FT(ir));
				rv.w = ieee754dp_cmp(fs, ft,
					cmptab[cmpop & 0x7], cmpop & 0x8);
				rfmt = -1;
				if ((cmpop & 0x8)
					&&
					ieee754_cxtest
					(IEEE754_INVALID_OPERATION))
					rcsr = FPU_CSR_INV_X | FPU_CSR_INV_S;
				else
					goto copcsr;

			}
			else {
				return SIGILL;
			}
			break;
		}
		break;
	}

	case w_fmt:{
		ieee754sp fs;

		switch (MIPSInst_FUNC(ir)) {
		case fcvts_op:
			/* convert word to single precision real */
			SPFROMREG(fs, MIPSInst_FS(ir));
			rv.s = ieee754sp_fint(fs.bits);
			rfmt = s_fmt;
			goto copcsr;
		case fcvtd_op:
			/* convert word to double precision real */
			SPFROMREG(fs, MIPSInst_FS(ir));
			rv.d = ieee754dp_fint(fs.bits);
			rfmt = d_fmt;
			goto copcsr;
		default:
			return SIGILL;
		}
		break;
	}

#if defined(__mips64)
	case l_fmt:{
		switch (MIPSInst_FUNC(ir)) {
		case fcvts_op:
			/* convert long to single precision real */
			rv.s = ieee754sp_flong(ctx->fpr[MIPSInst_FS(ir)]);
			rfmt = s_fmt;
			goto copcsr;
		case fcvtd_op:
			/* convert long to double precision real */
			rv.d = ieee754dp_flong(ctx->fpr[MIPSInst_FS(ir)]);
			rfmt = d_fmt;
			goto copcsr;
		default:
			return SIGILL;
		}
		break;
	}
#endif

	default:
		return SIGILL;
	}

	/*
	 * Update the fpu CSR register for this operation.
	 * If an exception is required, generate a tidy SIGFPE exception,
	 * without updating the result register.
	 * Note: cause exception bits do not accumulate, they are rewritten
	 * for each op; only the flag/sticky bits accumulate.
	 */
	ctx->fcr31 = (ctx->fcr31 & ~FPU_CSR_ALL_X) | rcsr;
	if ((ctx->fcr31 >> 5) & ctx->fcr31 & FPU_CSR_ALL_E) {
		/*printk ("SIGFPE: fpu csr = %08x\n",ctx->fcr31); */
		return SIGFPE;
	}

	/*
	 * Now we can safely write the result back to the register file.
	 */
	switch (rfmt) {
	case -1:{
#if __mips >= 4
		cond = fpucondbit[MIPSInst_FD(ir) >> 2];
#else
		cond = FPU_CSR_COND;
#endif
		if (rv.w)
			ctx->fcr31 |= cond;
		else
			ctx->fcr31 &= ~cond;
		break;
	}
	case d_fmt:
		DPTOREG(rv.d, MIPSInst_FD(ir));
		break;
	case s_fmt:
		SPTOREG(rv.s, MIPSInst_FD(ir));
		break;
	case w_fmt:
		SITOREG(rv.w, MIPSInst_FD(ir));
		break;
#if defined(__mips64)
	case l_fmt:
		DITOREG(rv.l, MIPSInst_FD(ir));
		break;
#endif
	default:
		return SIGILL;
	}

	return 0;
}

int fpu_emulator_cop1Handler(struct pt_regs *xcp, struct mips_fpu_struct *ctx,
	int has_fpu)
{
	unsigned long oldepc, prevepc;
	mips_instruction insn;
	int sig = 0;

	oldepc = xcp->cp0_epc;
	do {
		prevepc = xcp->cp0_epc;

		if (get_user(insn, (mips_instruction __user *) xcp->cp0_epc)) {
			fpuemustats.errors++;
			return SIGBUS;
		}
		if (insn == 0)
			xcp->cp0_epc += 4;	/* skip nops */
		else {
			/*
			 * The 'ieee754_csr' is an alias of
			 * ctx->fcr31.  No need to copy ctx->fcr31 to
			 * ieee754_csr.  But ieee754_csr.rm is ieee
			 * library modes. (not mips rounding mode)
			 */
			/* convert to ieee library modes */
			ieee754_csr.rm = ieee_rm[ieee754_csr.rm];
			sig = cop1Emulate(xcp, ctx);
			/* revert to mips rounding mode */
			ieee754_csr.rm = mips_rm[ieee754_csr.rm];
		}

		if (has_fpu)
			break;
		if (sig)
			break;

		cond_resched();
	} while (xcp->cp0_epc > prevepc);

	/* SIGILL indicates a non-fpu instruction */
	if (sig == SIGILL && xcp->cp0_epc != oldepc)
		/* but if epc has advanced, then ignore it */
		sig = 0;

	return sig;
}

#ifdef CONFIG_DEBUG_FS
extern struct dentry *mips_debugfs_dir;
static int __init debugfs_fpuemu(void)
{
	struct dentry *d, *dir;
	int i;
	static struct {
		const char *name;
		unsigned int *v;
	} vars[] __initdata = {
		{ "emulated", &fpuemustats.emulated },
		{ "loads",    &fpuemustats.loads },
		{ "stores",   &fpuemustats.stores },
		{ "cp1ops",   &fpuemustats.cp1ops },
		{ "cp1xops",  &fpuemustats.cp1xops },
		{ "errors",   &fpuemustats.errors },
	};

	if (!mips_debugfs_dir)
		return -ENODEV;
	dir = debugfs_create_dir("fpuemustats", mips_debugfs_dir);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	for (i = 0; i < ARRAY_SIZE(vars); i++) {
		d = debugfs_create_u32(vars[i].name, S_IRUGO, dir, vars[i].v);
		if (IS_ERR(d))
			return PTR_ERR(d);
	}
	return 0;
}
__initcall(debugfs_fpuemu);
#endif
