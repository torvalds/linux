// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2018 Andes Technology Corporation

#include <asm/bitfield.h>
#include <asm/uaccess.h>
#include <asm/sfp-machine.h>
#include <asm/fpuemu.h>
#include <asm/nds32_fpu_inst.h>

#define DPFROMREG(dp, x) (dp = (void *)((unsigned long *)fpu_reg + 2*x))
#ifdef __NDS32_EL__
#define SPFROMREG(sp, x)\
	((sp) = (void *)((unsigned long *)fpu_reg + (x^1)))
#else
#define SPFROMREG(sp, x) ((sp) = (void *)((unsigned long *)fpu_reg + x))
#endif

#define DEF3OP(name, p, f1, f2) \
void fpemu_##name##p(void *ft, void *fa, void *fb) \
{ \
	f1(fa, fa, fb); \
	f2(ft, ft, fa); \
}

#define DEF3OPNEG(name, p, f1, f2, f3) \
void fpemu_##name##p(void *ft, void *fa, void *fb) \
{ \
	f1(fa, fa, fb); \
	f2(ft, ft, fa); \
	f3(ft, ft); \
}
DEF3OP(fmadd, s, fmuls, fadds);
DEF3OP(fmsub, s, fmuls, fsubs);
DEF3OP(fmadd, d, fmuld, faddd);
DEF3OP(fmsub, d, fmuld, fsubd);
DEF3OPNEG(fnmadd, s, fmuls, fadds, fnegs);
DEF3OPNEG(fnmsub, s, fmuls, fsubs, fnegs);
DEF3OPNEG(fnmadd, d, fmuld, faddd, fnegd);
DEF3OPNEG(fnmsub, d, fmuld, fsubd, fnegd);

static const unsigned char cmptab[8] = {
	SF_CEQ,
	SF_CEQ,
	SF_CLT,
	SF_CLT,
	SF_CLT | SF_CEQ,
	SF_CLT | SF_CEQ,
	SF_CUN,
	SF_CUN
};

enum ARGTYPE {
	S1S = 1,
	S2S,
	S1D,
	CS,
	D1D,
	D2D,
	D1S,
	CD
};
union func_t {
	void (*t)(void *ft, void *fa, void *fb);
	void (*b)(void *ft, void *fa);
};
/*
 * Emulate a single FPU arithmetic instruction.
 */
static int fpu_emu(struct fpu_struct *fpu_reg, unsigned long insn)
{
	int rfmt;		/* resulting format */
	union func_t func;
	int ftype = 0;

	switch (rfmt = NDS32Insn_OPCODE_COP0(insn)) {
	case fs1_op:{
			switch (NDS32Insn_OPCODE_BIT69(insn)) {
			case fadds_op:
				func.t = fadds;
				ftype = S2S;
				break;
			case fsubs_op:
				func.t = fsubs;
				ftype = S2S;
				break;
			case fmadds_op:
				func.t = fpemu_fmadds;
				ftype = S2S;
				break;
			case fmsubs_op:
				func.t = fpemu_fmsubs;
				ftype = S2S;
				break;
			case fnmadds_op:
				func.t = fpemu_fnmadds;
				ftype = S2S;
				break;
			case fnmsubs_op:
				func.t = fpemu_fnmsubs;
				ftype = S2S;
				break;
			case fmuls_op:
				func.t = fmuls;
				ftype = S2S;
				break;
			case fdivs_op:
				func.t = fdivs;
				ftype = S2S;
				break;
			case fs1_f2op_op:
				switch (NDS32Insn_OPCODE_BIT1014(insn)) {
				case fs2d_op:
					func.b = fs2d;
					ftype = S1D;
					break;
				case fsqrts_op:
					func.b = fsqrts;
					ftype = S1S;
					break;
				default:
					return SIGILL;
				}
				break;
			default:
				return SIGILL;
			}
			break;
		}
	case fs2_op:
		switch (NDS32Insn_OPCODE_BIT69(insn)) {
		case fcmpeqs_op:
		case fcmpeqs_e_op:
		case fcmplts_op:
		case fcmplts_e_op:
		case fcmples_op:
		case fcmples_e_op:
		case fcmpuns_op:
		case fcmpuns_e_op:
			ftype = CS;
			break;
		default:
			return SIGILL;
		}
		break;
	case fd1_op:{
			switch (NDS32Insn_OPCODE_BIT69(insn)) {
			case faddd_op:
				func.t = faddd;
				ftype = D2D;
				break;
			case fsubd_op:
				func.t = fsubd;
				ftype = D2D;
				break;
			case fmaddd_op:
				func.t = fpemu_fmaddd;
				ftype = D2D;
				break;
			case fmsubd_op:
				func.t = fpemu_fmsubd;
				ftype = D2D;
				break;
			case fnmaddd_op:
				func.t = fpemu_fnmaddd;
				ftype = D2D;
				break;
			case fnmsubd_op:
				func.t = fpemu_fnmsubd;
				ftype = D2D;
				break;
			case fmuld_op:
				func.t = fmuld;
				ftype = D2D;
				break;
			case fdivd_op:
				func.t = fdivd;
				ftype = D2D;
				break;
			case fd1_f2op_op:
				switch (NDS32Insn_OPCODE_BIT1014(insn)) {
				case fd2s_op:
					func.b = fd2s;
					ftype = D1S;
					break;
				case fsqrtd_op:
					func.b = fsqrtd;
					ftype = D1D;
					break;
				default:
					return SIGILL;
				}
				break;
			default:
				return SIGILL;

			}
			break;
		}

	case fd2_op:
		switch (NDS32Insn_OPCODE_BIT69(insn)) {
		case fcmpeqd_op:
		case fcmpeqd_e_op:
		case fcmpltd_op:
		case fcmpltd_e_op:
		case fcmpled_op:
		case fcmpled_e_op:
		case fcmpund_op:
		case fcmpund_e_op:
			ftype = CD;
			break;
		default:
			return SIGILL;
		}
		break;

	default:
		return SIGILL;
	}

	switch (ftype) {
	case S1S:{
			void *ft, *fa;

			SPFROMREG(ft, NDS32Insn_OPCODE_Rt(insn));
			SPFROMREG(fa, NDS32Insn_OPCODE_Ra(insn));
			func.b(ft, fa);
			break;
		}
	case S2S:{
			void *ft, *fa, *fb;

			SPFROMREG(ft, NDS32Insn_OPCODE_Rt(insn));
			SPFROMREG(fa, NDS32Insn_OPCODE_Ra(insn));
			SPFROMREG(fb, NDS32Insn_OPCODE_Rb(insn));
			func.t(ft, fa, fb);
			break;
		}
	case S1D:{
			void *ft, *fa;

			DPFROMREG(ft, NDS32Insn_OPCODE_Rt(insn));
			SPFROMREG(fa, NDS32Insn_OPCODE_Ra(insn));
			func.b(ft, fa);
			break;
		}
	case CS:{
			unsigned int cmpop = NDS32Insn_OPCODE_BIT69(insn);
			void *ft, *fa, *fb;

			SPFROMREG(ft, NDS32Insn_OPCODE_Rt(insn));
			SPFROMREG(fa, NDS32Insn_OPCODE_Ra(insn));
			SPFROMREG(fb, NDS32Insn_OPCODE_Rb(insn));
			if (cmpop < 0x8) {
				cmpop = cmptab[cmpop];
				fcmps(ft, fa, fb, cmpop);
			} else
				return SIGILL;
			break;
		}
	case D1D:{
			void *ft, *fa;

			DPFROMREG(ft, NDS32Insn_OPCODE_Rt(insn));
			DPFROMREG(fa, NDS32Insn_OPCODE_Ra(insn));
			func.b(ft, fa);
			break;
		}
	case D2D:{
			void *ft, *fa, *fb;

			DPFROMREG(ft, NDS32Insn_OPCODE_Rt(insn));
			DPFROMREG(fa, NDS32Insn_OPCODE_Ra(insn));
			DPFROMREG(fb, NDS32Insn_OPCODE_Rb(insn));
			func.t(ft, fa, fb);
			break;
		}
	case D1S:{
			void *ft, *fa;

			SPFROMREG(ft, NDS32Insn_OPCODE_Rt(insn));
			DPFROMREG(fa, NDS32Insn_OPCODE_Ra(insn));
			func.b(ft, fa);
			break;
		}
	case CD:{
			unsigned int cmpop = NDS32Insn_OPCODE_BIT69(insn);
			void *ft, *fa, *fb;

			SPFROMREG(ft, NDS32Insn_OPCODE_Rt(insn));
			DPFROMREG(fa, NDS32Insn_OPCODE_Ra(insn));
			DPFROMREG(fb, NDS32Insn_OPCODE_Rb(insn));
			if (cmpop < 0x8) {
				cmpop = cmptab[cmpop];
				fcmpd(ft, fa, fb, cmpop);
			} else
				return SIGILL;
			break;
		}
	default:
		return SIGILL;
	}

	/*
	 * If an exception is required, generate a tidy SIGFPE exception.
	 */
#if IS_ENABLED(CONFIG_SUPPORT_DENORMAL_ARITHMETIC)
	if (((fpu_reg->fpcsr << 5) & fpu_reg->fpcsr & FPCSR_mskALLE_NO_UDFE) ||
	    ((fpu_reg->fpcsr & FPCSR_mskUDF) && (fpu_reg->UDF_trap)))
#else
	if ((fpu_reg->fpcsr << 5) & fpu_reg->fpcsr & FPCSR_mskALLE)
#endif
		return SIGFPE;
	return 0;
}


int do_fpuemu(struct pt_regs *regs, struct fpu_struct *fpu)
{
	unsigned long insn = 0, addr = regs->ipc;
	unsigned long emulpc, contpc;
	unsigned char *pc = (void *)&insn;
	char c;
	int i = 0, ret;

	for (i = 0; i < 4; i++) {
		if (__get_user(c, (unsigned char *)addr++))
			return SIGBUS;
		*pc++ = c;
	}

	insn = be32_to_cpu(insn);

	emulpc = regs->ipc;
	contpc = regs->ipc + 4;

	if (NDS32Insn_OPCODE(insn) != cop0_op)
		return SIGILL;
	switch (NDS32Insn_OPCODE_COP0(insn)) {
	case fs1_op:
	case fs2_op:
	case fd1_op:
	case fd2_op:
		{
			/* a real fpu computation instruction */
			ret = fpu_emu(fpu, insn);
			if (!ret)
				regs->ipc = contpc;
		}
		break;

	default:
		return SIGILL;
	}

	return ret;
}
