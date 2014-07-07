/*
 * arch/powerpc/math-emu/math_efp.c
 *
 * Copyright (C) 2006-2008, 2010 Freescale Semiconductor, Inc.
 *
 * Author: Ebony Zhu,	<ebony.zhu@freescale.com>
 *         Yu Liu,	<yu.liu@freescale.com>
 *
 * Derived from arch/alpha/math-emu/math.c
 *              arch/powerpc/math-emu/math.c
 *
 * Description:
 * This file is the exception handler to make E500 SPE instructions
 * fully comply with IEEE-754 floating point standard.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/prctl.h>

#include <asm/uaccess.h>
#include <asm/reg.h>

#define FP_EX_BOOKE_E500_SPE
#include <asm/sfp-machine.h>

#include <math-emu/soft-fp.h>
#include <math-emu/single.h>
#include <math-emu/double.h>

#define EFAPU		0x4

#define VCT		0x4
#define SPFP		0x6
#define DPFP		0x7

#define EFSADD		0x2c0
#define EFSSUB		0x2c1
#define EFSABS		0x2c4
#define EFSNABS		0x2c5
#define EFSNEG		0x2c6
#define EFSMUL		0x2c8
#define EFSDIV		0x2c9
#define EFSCMPGT	0x2cc
#define EFSCMPLT	0x2cd
#define EFSCMPEQ	0x2ce
#define EFSCFD		0x2cf
#define EFSCFSI		0x2d1
#define EFSCTUI		0x2d4
#define EFSCTSI		0x2d5
#define EFSCTUF		0x2d6
#define EFSCTSF		0x2d7
#define EFSCTUIZ	0x2d8
#define EFSCTSIZ	0x2da

#define EVFSADD		0x280
#define EVFSSUB		0x281
#define EVFSABS		0x284
#define EVFSNABS	0x285
#define EVFSNEG		0x286
#define EVFSMUL		0x288
#define EVFSDIV		0x289
#define EVFSCMPGT	0x28c
#define EVFSCMPLT	0x28d
#define EVFSCMPEQ	0x28e
#define EVFSCTUI	0x294
#define EVFSCTSI	0x295
#define EVFSCTUF	0x296
#define EVFSCTSF	0x297
#define EVFSCTUIZ	0x298
#define EVFSCTSIZ	0x29a

#define EFDADD		0x2e0
#define EFDSUB		0x2e1
#define EFDABS		0x2e4
#define EFDNABS		0x2e5
#define EFDNEG		0x2e6
#define EFDMUL		0x2e8
#define EFDDIV		0x2e9
#define EFDCTUIDZ	0x2ea
#define EFDCTSIDZ	0x2eb
#define EFDCMPGT	0x2ec
#define EFDCMPLT	0x2ed
#define EFDCMPEQ	0x2ee
#define EFDCFS		0x2ef
#define EFDCTUI		0x2f4
#define EFDCTSI		0x2f5
#define EFDCTUF		0x2f6
#define EFDCTSF		0x2f7
#define EFDCTUIZ	0x2f8
#define EFDCTSIZ	0x2fa

#define AB	2
#define XA	3
#define XB	4
#define XCR	5
#define NOTYPE	0

#define SIGN_BIT_S	(1UL << 31)
#define SIGN_BIT_D	(1ULL << 63)
#define FP_EX_MASK	(FP_EX_INEXACT | FP_EX_INVALID | FP_EX_DIVZERO | \
			FP_EX_UNDERFLOW | FP_EX_OVERFLOW)

static int have_e500_cpu_a005_erratum;

union dw_union {
	u64 dp[1];
	u32 wp[2];
};

static unsigned long insn_type(unsigned long speinsn)
{
	unsigned long ret = NOTYPE;

	switch (speinsn & 0x7ff) {
	case EFSABS:	ret = XA;	break;
	case EFSADD:	ret = AB;	break;
	case EFSCFD:	ret = XB;	break;
	case EFSCMPEQ:	ret = XCR;	break;
	case EFSCMPGT:	ret = XCR;	break;
	case EFSCMPLT:	ret = XCR;	break;
	case EFSCTSF:	ret = XB;	break;
	case EFSCTSI:	ret = XB;	break;
	case EFSCTSIZ:	ret = XB;	break;
	case EFSCTUF:	ret = XB;	break;
	case EFSCTUI:	ret = XB;	break;
	case EFSCTUIZ:	ret = XB;	break;
	case EFSDIV:	ret = AB;	break;
	case EFSMUL:	ret = AB;	break;
	case EFSNABS:	ret = XA;	break;
	case EFSNEG:	ret = XA;	break;
	case EFSSUB:	ret = AB;	break;
	case EFSCFSI:	ret = XB;	break;

	case EVFSABS:	ret = XA;	break;
	case EVFSADD:	ret = AB;	break;
	case EVFSCMPEQ:	ret = XCR;	break;
	case EVFSCMPGT:	ret = XCR;	break;
	case EVFSCMPLT:	ret = XCR;	break;
	case EVFSCTSF:	ret = XB;	break;
	case EVFSCTSI:	ret = XB;	break;
	case EVFSCTSIZ:	ret = XB;	break;
	case EVFSCTUF:	ret = XB;	break;
	case EVFSCTUI:	ret = XB;	break;
	case EVFSCTUIZ:	ret = XB;	break;
	case EVFSDIV:	ret = AB;	break;
	case EVFSMUL:	ret = AB;	break;
	case EVFSNABS:	ret = XA;	break;
	case EVFSNEG:	ret = XA;	break;
	case EVFSSUB:	ret = AB;	break;

	case EFDABS:	ret = XA;	break;
	case EFDADD:	ret = AB;	break;
	case EFDCFS:	ret = XB;	break;
	case EFDCMPEQ:	ret = XCR;	break;
	case EFDCMPGT:	ret = XCR;	break;
	case EFDCMPLT:	ret = XCR;	break;
	case EFDCTSF:	ret = XB;	break;
	case EFDCTSI:	ret = XB;	break;
	case EFDCTSIDZ:	ret = XB;	break;
	case EFDCTSIZ:	ret = XB;	break;
	case EFDCTUF:	ret = XB;	break;
	case EFDCTUI:	ret = XB;	break;
	case EFDCTUIDZ:	ret = XB;	break;
	case EFDCTUIZ:	ret = XB;	break;
	case EFDDIV:	ret = AB;	break;
	case EFDMUL:	ret = AB;	break;
	case EFDNABS:	ret = XA;	break;
	case EFDNEG:	ret = XA;	break;
	case EFDSUB:	ret = AB;	break;
	}

	return ret;
}

int do_spe_mathemu(struct pt_regs *regs)
{
	FP_DECL_EX;
	int IR, cmp;

	unsigned long type, func, fc, fa, fb, src, speinsn;
	union dw_union vc, va, vb;

	if (get_user(speinsn, (unsigned int __user *) regs->nip))
		return -EFAULT;
	if ((speinsn >> 26) != EFAPU)
		return -EINVAL;         /* not an spe instruction */

	type = insn_type(speinsn);
	if (type == NOTYPE)
		goto illegal;

	func = speinsn & 0x7ff;
	fc = (speinsn >> 21) & 0x1f;
	fa = (speinsn >> 16) & 0x1f;
	fb = (speinsn >> 11) & 0x1f;
	src = (speinsn >> 5) & 0x7;

	vc.wp[0] = current->thread.evr[fc];
	vc.wp[1] = regs->gpr[fc];
	va.wp[0] = current->thread.evr[fa];
	va.wp[1] = regs->gpr[fa];
	vb.wp[0] = current->thread.evr[fb];
	vb.wp[1] = regs->gpr[fb];

	__FPU_FPSCR = mfspr(SPRN_SPEFSCR);

	pr_debug("speinsn:%08lx spefscr:%08lx\n", speinsn, __FPU_FPSCR);
	pr_debug("vc: %08x  %08x\n", vc.wp[0], vc.wp[1]);
	pr_debug("va: %08x  %08x\n", va.wp[0], va.wp[1]);
	pr_debug("vb: %08x  %08x\n", vb.wp[0], vb.wp[1]);

	switch (src) {
	case SPFP: {
		FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);

		switch (type) {
		case AB:
		case XCR:
			FP_UNPACK_SP(SA, va.wp + 1);
		case XB:
			FP_UNPACK_SP(SB, vb.wp + 1);
			break;
		case XA:
			FP_UNPACK_SP(SA, va.wp + 1);
			break;
		}

		pr_debug("SA: %ld %08lx %ld (%ld)\n", SA_s, SA_f, SA_e, SA_c);
		pr_debug("SB: %ld %08lx %ld (%ld)\n", SB_s, SB_f, SB_e, SB_c);

		switch (func) {
		case EFSABS:
			vc.wp[1] = va.wp[1] & ~SIGN_BIT_S;
			goto update_regs;

		case EFSNABS:
			vc.wp[1] = va.wp[1] | SIGN_BIT_S;
			goto update_regs;

		case EFSNEG:
			vc.wp[1] = va.wp[1] ^ SIGN_BIT_S;
			goto update_regs;

		case EFSADD:
			FP_ADD_S(SR, SA, SB);
			goto pack_s;

		case EFSSUB:
			FP_SUB_S(SR, SA, SB);
			goto pack_s;

		case EFSMUL:
			FP_MUL_S(SR, SA, SB);
			goto pack_s;

		case EFSDIV:
			FP_DIV_S(SR, SA, SB);
			goto pack_s;

		case EFSCMPEQ:
			cmp = 0;
			goto cmp_s;

		case EFSCMPGT:
			cmp = 1;
			goto cmp_s;

		case EFSCMPLT:
			cmp = -1;
			goto cmp_s;

		case EFSCTSF:
		case EFSCTUF:
			if (SB_c == FP_CLS_NAN) {
				vc.wp[1] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				SB_e += (func == EFSCTSF ? 31 : 32);
				FP_TO_INT_ROUND_S(vc.wp[1], SB, 32,
						(func == EFSCTSF));
			}
			goto update_regs;

		case EFSCFD: {
			FP_DECL_D(DB);
			FP_CLEAR_EXCEPTIONS;
			FP_UNPACK_DP(DB, vb.dp);

			pr_debug("DB: %ld %08lx %08lx %ld (%ld)\n",
					DB_s, DB_f1, DB_f0, DB_e, DB_c);

			FP_CONV(S, D, 1, 2, SR, DB);
			goto pack_s;
		}

		case EFSCTSI:
		case EFSCTUI:
			if (SB_c == FP_CLS_NAN) {
				vc.wp[1] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				FP_TO_INT_ROUND_S(vc.wp[1], SB, 32,
						((func & 0x3) != 0));
			}
			goto update_regs;

		case EFSCTSIZ:
		case EFSCTUIZ:
			if (SB_c == FP_CLS_NAN) {
				vc.wp[1] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				FP_TO_INT_S(vc.wp[1], SB, 32,
						((func & 0x3) != 0));
			}
			goto update_regs;

		default:
			goto illegal;
		}
		break;

pack_s:
		pr_debug("SR: %ld %08lx %ld (%ld)\n", SR_s, SR_f, SR_e, SR_c);

		FP_PACK_SP(vc.wp + 1, SR);
		goto update_regs;

cmp_s:
		FP_CMP_S(IR, SA, SB, 3);
		if (IR == 3 && (FP_ISSIGNAN_S(SA) || FP_ISSIGNAN_S(SB)))
			FP_SET_EXCEPTION(FP_EX_INVALID);
		if (IR == cmp) {
			IR = 0x4;
		} else {
			IR = 0;
		}
		goto update_ccr;
	}

	case DPFP: {
		FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);

		switch (type) {
		case AB:
		case XCR:
			FP_UNPACK_DP(DA, va.dp);
		case XB:
			FP_UNPACK_DP(DB, vb.dp);
			break;
		case XA:
			FP_UNPACK_DP(DA, va.dp);
			break;
		}

		pr_debug("DA: %ld %08lx %08lx %ld (%ld)\n",
				DA_s, DA_f1, DA_f0, DA_e, DA_c);
		pr_debug("DB: %ld %08lx %08lx %ld (%ld)\n",
				DB_s, DB_f1, DB_f0, DB_e, DB_c);

		switch (func) {
		case EFDABS:
			vc.dp[0] = va.dp[0] & ~SIGN_BIT_D;
			goto update_regs;

		case EFDNABS:
			vc.dp[0] = va.dp[0] | SIGN_BIT_D;
			goto update_regs;

		case EFDNEG:
			vc.dp[0] = va.dp[0] ^ SIGN_BIT_D;
			goto update_regs;

		case EFDADD:
			FP_ADD_D(DR, DA, DB);
			goto pack_d;

		case EFDSUB:
			FP_SUB_D(DR, DA, DB);
			goto pack_d;

		case EFDMUL:
			FP_MUL_D(DR, DA, DB);
			goto pack_d;

		case EFDDIV:
			FP_DIV_D(DR, DA, DB);
			goto pack_d;

		case EFDCMPEQ:
			cmp = 0;
			goto cmp_d;

		case EFDCMPGT:
			cmp = 1;
			goto cmp_d;

		case EFDCMPLT:
			cmp = -1;
			goto cmp_d;

		case EFDCTSF:
		case EFDCTUF:
			if (DB_c == FP_CLS_NAN) {
				vc.wp[1] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				DB_e += (func == EFDCTSF ? 31 : 32);
				FP_TO_INT_ROUND_D(vc.wp[1], DB, 32,
						(func == EFDCTSF));
			}
			goto update_regs;

		case EFDCFS: {
			FP_DECL_S(SB);
			FP_CLEAR_EXCEPTIONS;
			FP_UNPACK_SP(SB, vb.wp + 1);

			pr_debug("SB: %ld %08lx %ld (%ld)\n",
					SB_s, SB_f, SB_e, SB_c);

			FP_CONV(D, S, 2, 1, DR, SB);
			goto pack_d;
		}

		case EFDCTUIDZ:
		case EFDCTSIDZ:
			if (DB_c == FP_CLS_NAN) {
				vc.dp[0] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				FP_TO_INT_D(vc.dp[0], DB, 64,
						((func & 0x1) == 0));
			}
			goto update_regs;

		case EFDCTUI:
		case EFDCTSI:
			if (DB_c == FP_CLS_NAN) {
				vc.wp[1] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				FP_TO_INT_ROUND_D(vc.wp[1], DB, 32,
						((func & 0x3) != 0));
			}
			goto update_regs;

		case EFDCTUIZ:
		case EFDCTSIZ:
			if (DB_c == FP_CLS_NAN) {
				vc.wp[1] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				FP_TO_INT_D(vc.wp[1], DB, 32,
						((func & 0x3) != 0));
			}
			goto update_regs;

		default:
			goto illegal;
		}
		break;

pack_d:
		pr_debug("DR: %ld %08lx %08lx %ld (%ld)\n",
				DR_s, DR_f1, DR_f0, DR_e, DR_c);

		FP_PACK_DP(vc.dp, DR);
		goto update_regs;

cmp_d:
		FP_CMP_D(IR, DA, DB, 3);
		if (IR == 3 && (FP_ISSIGNAN_D(DA) || FP_ISSIGNAN_D(DB)))
			FP_SET_EXCEPTION(FP_EX_INVALID);
		if (IR == cmp) {
			IR = 0x4;
		} else {
			IR = 0;
		}
		goto update_ccr;

	}

	case VCT: {
		FP_DECL_S(SA0); FP_DECL_S(SB0); FP_DECL_S(SR0);
		FP_DECL_S(SA1); FP_DECL_S(SB1); FP_DECL_S(SR1);
		int IR0, IR1;

		switch (type) {
		case AB:
		case XCR:
			FP_UNPACK_SP(SA0, va.wp);
			FP_UNPACK_SP(SA1, va.wp + 1);
		case XB:
			FP_UNPACK_SP(SB0, vb.wp);
			FP_UNPACK_SP(SB1, vb.wp + 1);
			break;
		case XA:
			FP_UNPACK_SP(SA0, va.wp);
			FP_UNPACK_SP(SA1, va.wp + 1);
			break;
		}

		pr_debug("SA0: %ld %08lx %ld (%ld)\n",
				SA0_s, SA0_f, SA0_e, SA0_c);
		pr_debug("SA1: %ld %08lx %ld (%ld)\n",
				SA1_s, SA1_f, SA1_e, SA1_c);
		pr_debug("SB0: %ld %08lx %ld (%ld)\n",
				SB0_s, SB0_f, SB0_e, SB0_c);
		pr_debug("SB1: %ld %08lx %ld (%ld)\n",
				SB1_s, SB1_f, SB1_e, SB1_c);

		switch (func) {
		case EVFSABS:
			vc.wp[0] = va.wp[0] & ~SIGN_BIT_S;
			vc.wp[1] = va.wp[1] & ~SIGN_BIT_S;
			goto update_regs;

		case EVFSNABS:
			vc.wp[0] = va.wp[0] | SIGN_BIT_S;
			vc.wp[1] = va.wp[1] | SIGN_BIT_S;
			goto update_regs;

		case EVFSNEG:
			vc.wp[0] = va.wp[0] ^ SIGN_BIT_S;
			vc.wp[1] = va.wp[1] ^ SIGN_BIT_S;
			goto update_regs;

		case EVFSADD:
			FP_ADD_S(SR0, SA0, SB0);
			FP_ADD_S(SR1, SA1, SB1);
			goto pack_vs;

		case EVFSSUB:
			FP_SUB_S(SR0, SA0, SB0);
			FP_SUB_S(SR1, SA1, SB1);
			goto pack_vs;

		case EVFSMUL:
			FP_MUL_S(SR0, SA0, SB0);
			FP_MUL_S(SR1, SA1, SB1);
			goto pack_vs;

		case EVFSDIV:
			FP_DIV_S(SR0, SA0, SB0);
			FP_DIV_S(SR1, SA1, SB1);
			goto pack_vs;

		case EVFSCMPEQ:
			cmp = 0;
			goto cmp_vs;

		case EVFSCMPGT:
			cmp = 1;
			goto cmp_vs;

		case EVFSCMPLT:
			cmp = -1;
			goto cmp_vs;

		case EVFSCTUF:
		case EVFSCTSF:
			if (SB0_c == FP_CLS_NAN) {
				vc.wp[0] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				SB0_e += (func == EVFSCTSF ? 31 : 32);
				FP_TO_INT_ROUND_S(vc.wp[0], SB0, 32,
						(func == EVFSCTSF));
			}
			if (SB1_c == FP_CLS_NAN) {
				vc.wp[1] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				SB1_e += (func == EVFSCTSF ? 31 : 32);
				FP_TO_INT_ROUND_S(vc.wp[1], SB1, 32,
						(func == EVFSCTSF));
			}
			goto update_regs;

		case EVFSCTUI:
		case EVFSCTSI:
			if (SB0_c == FP_CLS_NAN) {
				vc.wp[0] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				FP_TO_INT_ROUND_S(vc.wp[0], SB0, 32,
						((func & 0x3) != 0));
			}
			if (SB1_c == FP_CLS_NAN) {
				vc.wp[1] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				FP_TO_INT_ROUND_S(vc.wp[1], SB1, 32,
						((func & 0x3) != 0));
			}
			goto update_regs;

		case EVFSCTUIZ:
		case EVFSCTSIZ:
			if (SB0_c == FP_CLS_NAN) {
				vc.wp[0] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				FP_TO_INT_S(vc.wp[0], SB0, 32,
						((func & 0x3) != 0));
			}
			if (SB1_c == FP_CLS_NAN) {
				vc.wp[1] = 0;
				FP_SET_EXCEPTION(FP_EX_INVALID);
			} else {
				FP_TO_INT_S(vc.wp[1], SB1, 32,
						((func & 0x3) != 0));
			}
			goto update_regs;

		default:
			goto illegal;
		}
		break;

pack_vs:
		pr_debug("SR0: %ld %08lx %ld (%ld)\n",
				SR0_s, SR0_f, SR0_e, SR0_c);
		pr_debug("SR1: %ld %08lx %ld (%ld)\n",
				SR1_s, SR1_f, SR1_e, SR1_c);

		FP_PACK_SP(vc.wp, SR0);
		FP_PACK_SP(vc.wp + 1, SR1);
		goto update_regs;

cmp_vs:
		{
			int ch, cl;

			FP_CMP_S(IR0, SA0, SB0, 3);
			FP_CMP_S(IR1, SA1, SB1, 3);
			if (IR0 == 3 && (FP_ISSIGNAN_S(SA0) || FP_ISSIGNAN_S(SB0)))
				FP_SET_EXCEPTION(FP_EX_INVALID);
			if (IR1 == 3 && (FP_ISSIGNAN_S(SA1) || FP_ISSIGNAN_S(SB1)))
				FP_SET_EXCEPTION(FP_EX_INVALID);
			ch = (IR0 == cmp) ? 1 : 0;
			cl = (IR1 == cmp) ? 1 : 0;
			IR = (ch << 3) | (cl << 2) | ((ch | cl) << 1) |
				((ch & cl) << 0);
			goto update_ccr;
		}
	}
	default:
		return -EINVAL;
	}

update_ccr:
	regs->ccr &= ~(15 << ((7 - ((speinsn >> 23) & 0x7)) << 2));
	regs->ccr |= (IR << ((7 - ((speinsn >> 23) & 0x7)) << 2));

update_regs:
	/*
	 * If the "invalid" exception sticky bit was set by the
	 * processor for non-finite input, but was not set before the
	 * instruction being emulated, clear it.  Likewise for the
	 * "underflow" bit, which may have been set by the processor
	 * for exact underflow, not just inexact underflow when the
	 * flag should be set for IEEE 754 semantics.  Other sticky
	 * exceptions will only be set by the processor when they are
	 * correct according to IEEE 754 semantics, and we must not
	 * clear sticky bits that were already set before the emulated
	 * instruction as they represent the user-visible sticky
	 * exception status.  "inexact" traps to kernel are not
	 * required for IEEE semantics and are not enabled by default,
	 * so the "inexact" sticky bit may have been set by a previous
	 * instruction without the kernel being aware of it.
	 */
	__FPU_FPSCR
	  &= ~(FP_EX_INVALID | FP_EX_UNDERFLOW) | current->thread.spefscr_last;
	__FPU_FPSCR |= (FP_CUR_EXCEPTIONS & FP_EX_MASK);
	mtspr(SPRN_SPEFSCR, __FPU_FPSCR);
	current->thread.spefscr_last = __FPU_FPSCR;

	current->thread.evr[fc] = vc.wp[0];
	regs->gpr[fc] = vc.wp[1];

	pr_debug("ccr = %08lx\n", regs->ccr);
	pr_debug("cur exceptions = %08x spefscr = %08lx\n",
			FP_CUR_EXCEPTIONS, __FPU_FPSCR);
	pr_debug("vc: %08x  %08x\n", vc.wp[0], vc.wp[1]);
	pr_debug("va: %08x  %08x\n", va.wp[0], va.wp[1]);
	pr_debug("vb: %08x  %08x\n", vb.wp[0], vb.wp[1]);

	if (current->thread.fpexc_mode & PR_FP_EXC_SW_ENABLE) {
		if ((FP_CUR_EXCEPTIONS & FP_EX_DIVZERO)
		    && (current->thread.fpexc_mode & PR_FP_EXC_DIV))
			return 1;
		if ((FP_CUR_EXCEPTIONS & FP_EX_OVERFLOW)
		    && (current->thread.fpexc_mode & PR_FP_EXC_OVF))
			return 1;
		if ((FP_CUR_EXCEPTIONS & FP_EX_UNDERFLOW)
		    && (current->thread.fpexc_mode & PR_FP_EXC_UND))
			return 1;
		if ((FP_CUR_EXCEPTIONS & FP_EX_INEXACT)
		    && (current->thread.fpexc_mode & PR_FP_EXC_RES))
			return 1;
		if ((FP_CUR_EXCEPTIONS & FP_EX_INVALID)
		    && (current->thread.fpexc_mode & PR_FP_EXC_INV))
			return 1;
	}
	return 0;

illegal:
	if (have_e500_cpu_a005_erratum) {
		/* according to e500 cpu a005 erratum, reissue efp inst */
		regs->nip -= 4;
		pr_debug("re-issue efp inst: %08lx\n", speinsn);
		return 0;
	}

	printk(KERN_ERR "\nOoops! IEEE-754 compliance handler encountered un-supported instruction.\ninst code: %08lx\n", speinsn);
	return -ENOSYS;
}

int speround_handler(struct pt_regs *regs)
{
	union dw_union fgpr;
	int s_lo, s_hi;
	int lo_inexact, hi_inexact;
	int fp_result;
	unsigned long speinsn, type, fb, fc, fptype, func;

	if (get_user(speinsn, (unsigned int __user *) regs->nip))
		return -EFAULT;
	if ((speinsn >> 26) != 4)
		return -EINVAL;         /* not an spe instruction */

	func = speinsn & 0x7ff;
	type = insn_type(func);
	if (type == XCR) return -ENOSYS;

	__FPU_FPSCR = mfspr(SPRN_SPEFSCR);
	pr_debug("speinsn:%08lx spefscr:%08lx\n", speinsn, __FPU_FPSCR);

	fptype = (speinsn >> 5) & 0x7;

	/* No need to round if the result is exact */
	lo_inexact = __FPU_FPSCR & (SPEFSCR_FG | SPEFSCR_FX);
	hi_inexact = __FPU_FPSCR & (SPEFSCR_FGH | SPEFSCR_FXH);
	if (!(lo_inexact || (hi_inexact && fptype == VCT)))
		return 0;

	fc = (speinsn >> 21) & 0x1f;
	s_lo = regs->gpr[fc] & SIGN_BIT_S;
	s_hi = current->thread.evr[fc] & SIGN_BIT_S;
	fgpr.wp[0] = current->thread.evr[fc];
	fgpr.wp[1] = regs->gpr[fc];

	fb = (speinsn >> 11) & 0x1f;
	switch (func) {
	case EFSCTUIZ:
	case EFSCTSIZ:
	case EVFSCTUIZ:
	case EVFSCTSIZ:
	case EFDCTUIDZ:
	case EFDCTSIDZ:
	case EFDCTUIZ:
	case EFDCTSIZ:
		/*
		 * These instructions always round to zero,
		 * independent of the rounding mode.
		 */
		return 0;

	case EFSCTUI:
	case EFSCTUF:
	case EVFSCTUI:
	case EVFSCTUF:
	case EFDCTUI:
	case EFDCTUF:
		fp_result = 0;
		s_lo = 0;
		s_hi = 0;
		break;

	case EFSCTSI:
	case EFSCTSF:
		fp_result = 0;
		/* Recover the sign of a zero result if possible.  */
		if (fgpr.wp[1] == 0)
			s_lo = regs->gpr[fb] & SIGN_BIT_S;
		break;

	case EVFSCTSI:
	case EVFSCTSF:
		fp_result = 0;
		/* Recover the sign of a zero result if possible.  */
		if (fgpr.wp[1] == 0)
			s_lo = regs->gpr[fb] & SIGN_BIT_S;
		if (fgpr.wp[0] == 0)
			s_hi = current->thread.evr[fb] & SIGN_BIT_S;
		break;

	case EFDCTSI:
	case EFDCTSF:
		fp_result = 0;
		s_hi = s_lo;
		/* Recover the sign of a zero result if possible.  */
		if (fgpr.wp[1] == 0)
			s_hi = current->thread.evr[fb] & SIGN_BIT_S;
		break;

	default:
		fp_result = 1;
		break;
	}

	pr_debug("round fgpr: %08x  %08x\n", fgpr.wp[0], fgpr.wp[1]);

	switch (fptype) {
	/* Since SPE instructions on E500 core can handle round to nearest
	 * and round toward zero with IEEE-754 complied, we just need
	 * to handle round toward +Inf and round toward -Inf by software.
	 */
	case SPFP:
		if ((FP_ROUNDMODE) == FP_RND_PINF) {
			if (!s_lo) fgpr.wp[1]++; /* Z > 0, choose Z1 */
		} else { /* round to -Inf */
			if (s_lo) {
				if (fp_result)
					fgpr.wp[1]++; /* Z < 0, choose Z2 */
				else
					fgpr.wp[1]--; /* Z < 0, choose Z2 */
			}
		}
		break;

	case DPFP:
		if (FP_ROUNDMODE == FP_RND_PINF) {
			if (!s_hi) {
				if (fp_result)
					fgpr.dp[0]++; /* Z > 0, choose Z1 */
				else
					fgpr.wp[1]++; /* Z > 0, choose Z1 */
			}
		} else { /* round to -Inf */
			if (s_hi) {
				if (fp_result)
					fgpr.dp[0]++; /* Z < 0, choose Z2 */
				else
					fgpr.wp[1]--; /* Z < 0, choose Z2 */
			}
		}
		break;

	case VCT:
		if (FP_ROUNDMODE == FP_RND_PINF) {
			if (lo_inexact && !s_lo)
				fgpr.wp[1]++; /* Z_low > 0, choose Z1 */
			if (hi_inexact && !s_hi)
				fgpr.wp[0]++; /* Z_high word > 0, choose Z1 */
		} else { /* round to -Inf */
			if (lo_inexact && s_lo) {
				if (fp_result)
					fgpr.wp[1]++; /* Z_low < 0, choose Z2 */
				else
					fgpr.wp[1]--; /* Z_low < 0, choose Z2 */
			}
			if (hi_inexact && s_hi) {
				if (fp_result)
					fgpr.wp[0]++; /* Z_high < 0, choose Z2 */
				else
					fgpr.wp[0]--; /* Z_high < 0, choose Z2 */
			}
		}
		break;

	default:
		return -EINVAL;
	}

	current->thread.evr[fc] = fgpr.wp[0];
	regs->gpr[fc] = fgpr.wp[1];

	pr_debug("  to fgpr: %08x  %08x\n", fgpr.wp[0], fgpr.wp[1]);

	if (current->thread.fpexc_mode & PR_FP_EXC_SW_ENABLE)
		return (current->thread.fpexc_mode & PR_FP_EXC_RES) ? 1 : 0;
	return 0;
}

int __init spe_mathemu_init(void)
{
	u32 pvr, maj, min;

	pvr = mfspr(SPRN_PVR);

	if ((PVR_VER(pvr) == PVR_VER_E500V1) ||
	    (PVR_VER(pvr) == PVR_VER_E500V2)) {
		maj = PVR_MAJ(pvr);
		min = PVR_MIN(pvr);

		/*
		 * E500 revision below 1.1, 2.3, 3.1, 4.1, 5.1
		 * need cpu a005 errata workaround
		 */
		switch (maj) {
		case 1:
			if (min < 1)
				have_e500_cpu_a005_erratum = 1;
			break;
		case 2:
			if (min < 3)
				have_e500_cpu_a005_erratum = 1;
			break;
		case 3:
		case 4:
		case 5:
			if (min < 1)
				have_e500_cpu_a005_erratum = 1;
			break;
		default:
			break;
		}
	}

	return 0;
}

module_init(spe_mathemu_init);
