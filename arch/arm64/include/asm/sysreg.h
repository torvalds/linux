/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Macros for accessing system registers with older binutils.
 *
 * Copyright (C) 2014 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 */

#ifndef __ASM_SYSREG_H
#define __ASM_SYSREG_H

#include <linux/bits.h>
#include <linux/stringify.h>
#include <linux/kasan-tags.h>

#include <asm/gpr-num.h>

/*
 * ARMv8 ARM reserves the following encoding for system registers:
 * (Ref: ARMv8 ARM, Section: "System instruction class encoding overview",
 *  C5.2, version:ARM DDI 0487A.f)
 *	[20-19] : Op0
 *	[18-16] : Op1
 *	[15-12] : CRn
 *	[11-8]  : CRm
 *	[7-5]   : Op2
 */
#define Op0_shift	19
#define Op0_mask	0x3
#define Op1_shift	16
#define Op1_mask	0x7
#define CRn_shift	12
#define CRn_mask	0xf
#define CRm_shift	8
#define CRm_mask	0xf
#define Op2_shift	5
#define Op2_mask	0x7

#define sys_reg(op0, op1, crn, crm, op2) \
	(((op0) << Op0_shift) | ((op1) << Op1_shift) | \
	 ((crn) << CRn_shift) | ((crm) << CRm_shift) | \
	 ((op2) << Op2_shift))

#define sys_insn	sys_reg

#define sys_reg_Op0(id)	(((id) >> Op0_shift) & Op0_mask)
#define sys_reg_Op1(id)	(((id) >> Op1_shift) & Op1_mask)
#define sys_reg_CRn(id)	(((id) >> CRn_shift) & CRn_mask)
#define sys_reg_CRm(id)	(((id) >> CRm_shift) & CRm_mask)
#define sys_reg_Op2(id)	(((id) >> Op2_shift) & Op2_mask)

#ifndef CONFIG_BROKEN_GAS_INST

#ifdef __ASSEMBLY__
// The space separator is omitted so that __emit_inst(x) can be parsed as
// either an assembler directive or an assembler macro argument.
#define __emit_inst(x)			.inst(x)
#else
#define __emit_inst(x)			".inst " __stringify((x)) "\n\t"
#endif

#else  /* CONFIG_BROKEN_GAS_INST */

#ifndef CONFIG_CPU_BIG_ENDIAN
#define __INSTR_BSWAP(x)		(x)
#else  /* CONFIG_CPU_BIG_ENDIAN */
#define __INSTR_BSWAP(x)		((((x) << 24) & 0xff000000)	| \
					 (((x) <<  8) & 0x00ff0000)	| \
					 (((x) >>  8) & 0x0000ff00)	| \
					 (((x) >> 24) & 0x000000ff))
#endif	/* CONFIG_CPU_BIG_ENDIAN */

#ifdef __ASSEMBLY__
#define __emit_inst(x)			.long __INSTR_BSWAP(x)
#else  /* __ASSEMBLY__ */
#define __emit_inst(x)			".long " __stringify(__INSTR_BSWAP(x)) "\n\t"
#endif	/* __ASSEMBLY__ */

#endif	/* CONFIG_BROKEN_GAS_INST */

/*
 * Instructions for modifying PSTATE fields.
 * As per Arm ARM for v8-A, Section "C.5.1.3 op0 == 0b00, architectural hints,
 * barriers and CLREX, and PSTATE access", ARM DDI 0487 C.a, system instructions
 * for accessing PSTATE fields have the following encoding:
 *	Op0 = 0, CRn = 4
 *	Op1, Op2 encodes the PSTATE field modified and defines the constraints.
 *	CRm = Imm4 for the instruction.
 *	Rt = 0x1f
 */
#define pstate_field(op1, op2)		((op1) << Op1_shift | (op2) << Op2_shift)
#define PSTATE_Imm_shift		CRm_shift
#define SET_PSTATE(x, r)		__emit_inst(0xd500401f | PSTATE_ ## r | ((!!x) << PSTATE_Imm_shift))

#define PSTATE_PAN			pstate_field(0, 4)
#define PSTATE_UAO			pstate_field(0, 3)
#define PSTATE_SSBS			pstate_field(3, 1)
#define PSTATE_DIT			pstate_field(3, 2)
#define PSTATE_TCO			pstate_field(3, 4)

#define SET_PSTATE_PAN(x)		SET_PSTATE((x), PAN)
#define SET_PSTATE_UAO(x)		SET_PSTATE((x), UAO)
#define SET_PSTATE_SSBS(x)		SET_PSTATE((x), SSBS)
#define SET_PSTATE_DIT(x)		SET_PSTATE((x), DIT)
#define SET_PSTATE_TCO(x)		SET_PSTATE((x), TCO)

#define set_pstate_pan(x)		asm volatile(SET_PSTATE_PAN(x))
#define set_pstate_uao(x)		asm volatile(SET_PSTATE_UAO(x))
#define set_pstate_ssbs(x)		asm volatile(SET_PSTATE_SSBS(x))
#define set_pstate_dit(x)		asm volatile(SET_PSTATE_DIT(x))

/* Register-based PAN access, for save/restore purposes */
#define SYS_PSTATE_PAN			sys_reg(3, 0, 4, 2, 3)

#define __SYS_BARRIER_INSN(CRm, op2, Rt) \
	__emit_inst(0xd5000000 | sys_insn(0, 3, 3, (CRm), (op2)) | ((Rt) & 0x1f))

#define SB_BARRIER_INSN			__SYS_BARRIER_INSN(0, 7, 31)

#define SYS_DC_ISW			sys_insn(1, 0, 7, 6, 2)
#define SYS_DC_IGSW			sys_insn(1, 0, 7, 6, 4)
#define SYS_DC_IGDSW			sys_insn(1, 0, 7, 6, 6)
#define SYS_DC_CSW			sys_insn(1, 0, 7, 10, 2)
#define SYS_DC_CGSW			sys_insn(1, 0, 7, 10, 4)
#define SYS_DC_CGDSW			sys_insn(1, 0, 7, 10, 6)
#define SYS_DC_CISW			sys_insn(1, 0, 7, 14, 2)
#define SYS_DC_CIGSW			sys_insn(1, 0, 7, 14, 4)
#define SYS_DC_CIGDSW			sys_insn(1, 0, 7, 14, 6)

#define SYS_IC_IALLUIS			sys_insn(1, 0, 7, 1, 0)
#define SYS_IC_IALLU			sys_insn(1, 0, 7, 5, 0)
#define SYS_IC_IVAU			sys_insn(1, 3, 7, 5, 1)

#define SYS_DC_IVAC			sys_insn(1, 0, 7, 6, 1)
#define SYS_DC_IGVAC			sys_insn(1, 0, 7, 6, 3)
#define SYS_DC_IGDVAC			sys_insn(1, 0, 7, 6, 5)

#define SYS_DC_CVAC			sys_insn(1, 3, 7, 10, 1)
#define SYS_DC_CGVAC			sys_insn(1, 3, 7, 10, 3)
#define SYS_DC_CGDVAC			sys_insn(1, 3, 7, 10, 5)

#define SYS_DC_CVAU			sys_insn(1, 3, 7, 11, 1)

#define SYS_DC_CVAP			sys_insn(1, 3, 7, 12, 1)
#define SYS_DC_CGVAP			sys_insn(1, 3, 7, 12, 3)
#define SYS_DC_CGDVAP			sys_insn(1, 3, 7, 12, 5)

#define SYS_DC_CVADP			sys_insn(1, 3, 7, 13, 1)
#define SYS_DC_CGVADP			sys_insn(1, 3, 7, 13, 3)
#define SYS_DC_CGDVADP			sys_insn(1, 3, 7, 13, 5)

#define SYS_DC_CIVAC			sys_insn(1, 3, 7, 14, 1)
#define SYS_DC_CIGVAC			sys_insn(1, 3, 7, 14, 3)
#define SYS_DC_CIGDVAC			sys_insn(1, 3, 7, 14, 5)

/* Data cache zero operations */
#define SYS_DC_ZVA			sys_insn(1, 3, 7, 4, 1)
#define SYS_DC_GVA			sys_insn(1, 3, 7, 4, 3)
#define SYS_DC_GZVA			sys_insn(1, 3, 7, 4, 4)

/*
 * Automatically generated definitions for system registers, the
 * manual encodings below are in the process of being converted to
 * come from here. The header relies on the definition of sys_reg()
 * earlier in this file.
 */
#include "asm/sysreg-defs.h"

/*
 * System registers, organised loosely by encoding but grouped together
 * where the architected name contains an index. e.g. ID_MMFR<n>_EL1.
 */
#define SYS_SVCR_SMSTOP_SM_EL0		sys_reg(0, 3, 4, 2, 3)
#define SYS_SVCR_SMSTART_SM_EL0		sys_reg(0, 3, 4, 3, 3)
#define SYS_SVCR_SMSTOP_SMZA_EL0	sys_reg(0, 3, 4, 6, 3)

#define SYS_DBGBVRn_EL1(n)		sys_reg(2, 0, 0, n, 4)
#define SYS_DBGBCRn_EL1(n)		sys_reg(2, 0, 0, n, 5)
#define SYS_DBGWVRn_EL1(n)		sys_reg(2, 0, 0, n, 6)
#define SYS_DBGWCRn_EL1(n)		sys_reg(2, 0, 0, n, 7)
#define SYS_MDRAR_EL1			sys_reg(2, 0, 1, 0, 0)

#define SYS_OSLSR_EL1			sys_reg(2, 0, 1, 1, 4)
#define OSLSR_EL1_OSLM_MASK		(BIT(3) | BIT(0))
#define OSLSR_EL1_OSLM_NI		0
#define OSLSR_EL1_OSLM_IMPLEMENTED	BIT(3)
#define OSLSR_EL1_OSLK			BIT(1)

#define SYS_OSDLR_EL1			sys_reg(2, 0, 1, 3, 4)
#define SYS_DBGPRCR_EL1			sys_reg(2, 0, 1, 4, 4)
#define SYS_DBGCLAIMSET_EL1		sys_reg(2, 0, 7, 8, 6)
#define SYS_DBGCLAIMCLR_EL1		sys_reg(2, 0, 7, 9, 6)
#define SYS_DBGAUTHSTATUS_EL1		sys_reg(2, 0, 7, 14, 6)
#define SYS_MDCCSR_EL0			sys_reg(2, 3, 0, 1, 0)
#define SYS_DBGDTR_EL0			sys_reg(2, 3, 0, 4, 0)
#define SYS_DBGDTRRX_EL0		sys_reg(2, 3, 0, 5, 0)
#define SYS_DBGDTRTX_EL0		sys_reg(2, 3, 0, 5, 0)
#define SYS_DBGVCR32_EL2		sys_reg(2, 4, 0, 7, 0)

#define SYS_BRBINF_EL1(n)		sys_reg(2, 1, 8, (n & 15), (((n & 16) >> 2) | 0))
#define SYS_BRBINFINJ_EL1		sys_reg(2, 1, 9, 1, 0)
#define SYS_BRBSRC_EL1(n)		sys_reg(2, 1, 8, (n & 15), (((n & 16) >> 2) | 1))
#define SYS_BRBSRCINJ_EL1		sys_reg(2, 1, 9, 1, 1)
#define SYS_BRBTGT_EL1(n)		sys_reg(2, 1, 8, (n & 15), (((n & 16) >> 2) | 2))
#define SYS_BRBTGTINJ_EL1		sys_reg(2, 1, 9, 1, 2)
#define SYS_BRBTS_EL1			sys_reg(2, 1, 9, 0, 2)

#define SYS_BRBCR_EL1			sys_reg(2, 1, 9, 0, 0)
#define SYS_BRBFCR_EL1			sys_reg(2, 1, 9, 0, 1)
#define SYS_BRBIDR0_EL1			sys_reg(2, 1, 9, 2, 0)

#define SYS_TRCITECR_EL1		sys_reg(3, 0, 1, 2, 3)
#define SYS_TRCACATR(m)			sys_reg(2, 1, 2, ((m & 7) << 1), (2 | (m >> 3)))
#define SYS_TRCACVR(m)			sys_reg(2, 1, 2, ((m & 7) << 1), (0 | (m >> 3)))
#define SYS_TRCAUTHSTATUS		sys_reg(2, 1, 7, 14, 6)
#define SYS_TRCAUXCTLR			sys_reg(2, 1, 0, 6, 0)
#define SYS_TRCBBCTLR			sys_reg(2, 1, 0, 15, 0)
#define SYS_TRCCCCTLR			sys_reg(2, 1, 0, 14, 0)
#define SYS_TRCCIDCCTLR0		sys_reg(2, 1, 3, 0, 2)
#define SYS_TRCCIDCCTLR1		sys_reg(2, 1, 3, 1, 2)
#define SYS_TRCCIDCVR(m)		sys_reg(2, 1, 3, ((m & 7) << 1), 0)
#define SYS_TRCCLAIMCLR			sys_reg(2, 1, 7, 9, 6)
#define SYS_TRCCLAIMSET			sys_reg(2, 1, 7, 8, 6)
#define SYS_TRCCNTCTLR(m)		sys_reg(2, 1, 0, (4 | (m & 3)), 5)
#define SYS_TRCCNTRLDVR(m)		sys_reg(2, 1, 0, (0 | (m & 3)), 5)
#define SYS_TRCCNTVR(m)			sys_reg(2, 1, 0, (8 | (m & 3)), 5)
#define SYS_TRCCONFIGR			sys_reg(2, 1, 0, 4, 0)
#define SYS_TRCDEVARCH			sys_reg(2, 1, 7, 15, 6)
#define SYS_TRCDEVID			sys_reg(2, 1, 7, 2, 7)
#define SYS_TRCEVENTCTL0R		sys_reg(2, 1, 0, 8, 0)
#define SYS_TRCEVENTCTL1R		sys_reg(2, 1, 0, 9, 0)
#define SYS_TRCEXTINSELR(m)		sys_reg(2, 1, 0, (8 | (m & 3)), 4)
#define SYS_TRCIDR0			sys_reg(2, 1, 0, 8, 7)
#define SYS_TRCIDR10			sys_reg(2, 1, 0, 2, 6)
#define SYS_TRCIDR11			sys_reg(2, 1, 0, 3, 6)
#define SYS_TRCIDR12			sys_reg(2, 1, 0, 4, 6)
#define SYS_TRCIDR13			sys_reg(2, 1, 0, 5, 6)
#define SYS_TRCIDR1			sys_reg(2, 1, 0, 9, 7)
#define SYS_TRCIDR2			sys_reg(2, 1, 0, 10, 7)
#define SYS_TRCIDR3			sys_reg(2, 1, 0, 11, 7)
#define SYS_TRCIDR4			sys_reg(2, 1, 0, 12, 7)
#define SYS_TRCIDR5			sys_reg(2, 1, 0, 13, 7)
#define SYS_TRCIDR6			sys_reg(2, 1, 0, 14, 7)
#define SYS_TRCIDR7			sys_reg(2, 1, 0, 15, 7)
#define SYS_TRCIDR8			sys_reg(2, 1, 0, 0, 6)
#define SYS_TRCIDR9			sys_reg(2, 1, 0, 1, 6)
#define SYS_TRCIMSPEC(m)		sys_reg(2, 1, 0, (m & 7), 7)
#define SYS_TRCITEEDCR			sys_reg(2, 1, 0, 2, 1)
#define SYS_TRCOSLSR			sys_reg(2, 1, 1, 1, 4)
#define SYS_TRCPRGCTLR			sys_reg(2, 1, 0, 1, 0)
#define SYS_TRCQCTLR			sys_reg(2, 1, 0, 1, 1)
#define SYS_TRCRSCTLR(m)		sys_reg(2, 1, 1, (m & 15), (0 | (m >> 4)))
#define SYS_TRCRSR			sys_reg(2, 1, 0, 10, 0)
#define SYS_TRCSEQEVR(m)		sys_reg(2, 1, 0, (m & 3), 4)
#define SYS_TRCSEQRSTEVR		sys_reg(2, 1, 0, 6, 4)
#define SYS_TRCSEQSTR			sys_reg(2, 1, 0, 7, 4)
#define SYS_TRCSSCCR(m)			sys_reg(2, 1, 1, (m & 7), 2)
#define SYS_TRCSSCSR(m)			sys_reg(2, 1, 1, (8 | (m & 7)), 2)
#define SYS_TRCSSPCICR(m)		sys_reg(2, 1, 1, (m & 7), 3)
#define SYS_TRCSTALLCTLR		sys_reg(2, 1, 0, 11, 0)
#define SYS_TRCSTATR			sys_reg(2, 1, 0, 3, 0)
#define SYS_TRCSYNCPR			sys_reg(2, 1, 0, 13, 0)
#define SYS_TRCTRACEIDR			sys_reg(2, 1, 0, 0, 1)
#define SYS_TRCTSCTLR			sys_reg(2, 1, 0, 12, 0)
#define SYS_TRCVICTLR			sys_reg(2, 1, 0, 0, 2)
#define SYS_TRCVIIECTLR			sys_reg(2, 1, 0, 1, 2)
#define SYS_TRCVIPCSSCTLR		sys_reg(2, 1, 0, 3, 2)
#define SYS_TRCVISSCTLR			sys_reg(2, 1, 0, 2, 2)
#define SYS_TRCVMIDCCTLR0		sys_reg(2, 1, 3, 2, 2)
#define SYS_TRCVMIDCCTLR1		sys_reg(2, 1, 3, 3, 2)
#define SYS_TRCVMIDCVR(m)		sys_reg(2, 1, 3, ((m & 7) << 1), 1)

/* ETM */
#define SYS_TRCOSLAR			sys_reg(2, 1, 1, 0, 4)

#define SYS_BRBCR_EL2			sys_reg(2, 4, 9, 0, 0)

#define SYS_MIDR_EL1			sys_reg(3, 0, 0, 0, 0)
#define SYS_MPIDR_EL1			sys_reg(3, 0, 0, 0, 5)
#define SYS_REVIDR_EL1			sys_reg(3, 0, 0, 0, 6)

#define SYS_ACTLR_EL1			sys_reg(3, 0, 1, 0, 1)
#define SYS_RGSR_EL1			sys_reg(3, 0, 1, 0, 5)
#define SYS_GCR_EL1			sys_reg(3, 0, 1, 0, 6)

#define SYS_TCR_EL1			sys_reg(3, 0, 2, 0, 2)

#define SYS_APIAKEYLO_EL1		sys_reg(3, 0, 2, 1, 0)
#define SYS_APIAKEYHI_EL1		sys_reg(3, 0, 2, 1, 1)
#define SYS_APIBKEYLO_EL1		sys_reg(3, 0, 2, 1, 2)
#define SYS_APIBKEYHI_EL1		sys_reg(3, 0, 2, 1, 3)

#define SYS_APDAKEYLO_EL1		sys_reg(3, 0, 2, 2, 0)
#define SYS_APDAKEYHI_EL1		sys_reg(3, 0, 2, 2, 1)
#define SYS_APDBKEYLO_EL1		sys_reg(3, 0, 2, 2, 2)
#define SYS_APDBKEYHI_EL1		sys_reg(3, 0, 2, 2, 3)

#define SYS_APGAKEYLO_EL1		sys_reg(3, 0, 2, 3, 0)
#define SYS_APGAKEYHI_EL1		sys_reg(3, 0, 2, 3, 1)

#define SYS_SPSR_EL1			sys_reg(3, 0, 4, 0, 0)
#define SYS_ELR_EL1			sys_reg(3, 0, 4, 0, 1)

#define SYS_ICC_PMR_EL1			sys_reg(3, 0, 4, 6, 0)

#define SYS_AFSR0_EL1			sys_reg(3, 0, 5, 1, 0)
#define SYS_AFSR1_EL1			sys_reg(3, 0, 5, 1, 1)
#define SYS_ESR_EL1			sys_reg(3, 0, 5, 2, 0)

#define SYS_ERRIDR_EL1			sys_reg(3, 0, 5, 3, 0)
#define SYS_ERRSELR_EL1			sys_reg(3, 0, 5, 3, 1)
#define SYS_ERXFR_EL1			sys_reg(3, 0, 5, 4, 0)
#define SYS_ERXCTLR_EL1			sys_reg(3, 0, 5, 4, 1)
#define SYS_ERXSTATUS_EL1		sys_reg(3, 0, 5, 4, 2)
#define SYS_ERXADDR_EL1			sys_reg(3, 0, 5, 4, 3)
#define SYS_ERXPFGF_EL1			sys_reg(3, 0, 5, 4, 4)
#define SYS_ERXPFGCTL_EL1		sys_reg(3, 0, 5, 4, 5)
#define SYS_ERXPFGCDN_EL1		sys_reg(3, 0, 5, 4, 6)
#define SYS_ERXMISC0_EL1		sys_reg(3, 0, 5, 5, 0)
#define SYS_ERXMISC1_EL1		sys_reg(3, 0, 5, 5, 1)
#define SYS_ERXMISC2_EL1		sys_reg(3, 0, 5, 5, 2)
#define SYS_ERXMISC3_EL1		sys_reg(3, 0, 5, 5, 3)
#define SYS_TFSR_EL1			sys_reg(3, 0, 5, 6, 0)
#define SYS_TFSRE0_EL1			sys_reg(3, 0, 5, 6, 1)

#define SYS_PAR_EL1			sys_reg(3, 0, 7, 4, 0)

#define SYS_PAR_EL1_F			BIT(0)
/* When PAR_EL1.F == 1 */
#define SYS_PAR_EL1_FST			GENMASK(6, 1)
#define SYS_PAR_EL1_PTW			BIT(8)
#define SYS_PAR_EL1_S			BIT(9)
#define SYS_PAR_EL1_AssuredOnly		BIT(12)
#define SYS_PAR_EL1_TopLevel		BIT(13)
#define SYS_PAR_EL1_Overlay		BIT(14)
#define SYS_PAR_EL1_DirtyBit		BIT(15)
#define SYS_PAR_EL1_F1_IMPDEF		GENMASK_ULL(63, 48)
#define SYS_PAR_EL1_F1_RES0		(BIT(7) | BIT(10) | GENMASK_ULL(47, 16))
#define SYS_PAR_EL1_RES1		BIT(11)
/* When PAR_EL1.F == 0 */
#define SYS_PAR_EL1_SH			GENMASK_ULL(8, 7)
#define SYS_PAR_EL1_NS			BIT(9)
#define SYS_PAR_EL1_F0_IMPDEF		BIT(10)
#define SYS_PAR_EL1_NSE			BIT(11)
#define SYS_PAR_EL1_PA			GENMASK_ULL(51, 12)
#define SYS_PAR_EL1_ATTR		GENMASK_ULL(63, 56)
#define SYS_PAR_EL1_F0_RES0		(GENMASK_ULL(6, 1) | GENMASK_ULL(55, 52))

/*** Statistical Profiling Extension ***/
#define PMSEVFR_EL1_RES0_IMP	\
	(GENMASK_ULL(47, 32) | GENMASK_ULL(23, 16) | GENMASK_ULL(11, 8) |\
	 BIT_ULL(6) | BIT_ULL(4) | BIT_ULL(2) | BIT_ULL(0))
#define PMSEVFR_EL1_RES0_V1P1	\
	(PMSEVFR_EL1_RES0_IMP & ~(BIT_ULL(18) | BIT_ULL(17) | BIT_ULL(11)))
#define PMSEVFR_EL1_RES0_V1P2	\
	(PMSEVFR_EL1_RES0_V1P1 & ~BIT_ULL(6))

/* Buffer error reporting */
#define PMBSR_EL1_FAULT_FSC_SHIFT	PMBSR_EL1_MSS_SHIFT
#define PMBSR_EL1_FAULT_FSC_MASK	PMBSR_EL1_MSS_MASK

#define PMBSR_EL1_BUF_BSC_SHIFT		PMBSR_EL1_MSS_SHIFT
#define PMBSR_EL1_BUF_BSC_MASK		PMBSR_EL1_MSS_MASK

#define PMBSR_EL1_BUF_BSC_FULL		0x1UL

/*** End of Statistical Profiling Extension ***/

#define TRBSR_EL1_BSC_MASK		GENMASK(5, 0)
#define TRBSR_EL1_BSC_SHIFT		0

#define SYS_PMINTENSET_EL1		sys_reg(3, 0, 9, 14, 1)
#define SYS_PMINTENCLR_EL1		sys_reg(3, 0, 9, 14, 2)

#define SYS_PMMIR_EL1			sys_reg(3, 0, 9, 14, 6)

#define SYS_MAIR_EL1			sys_reg(3, 0, 10, 2, 0)
#define SYS_AMAIR_EL1			sys_reg(3, 0, 10, 3, 0)

#define SYS_VBAR_EL1			sys_reg(3, 0, 12, 0, 0)
#define SYS_DISR_EL1			sys_reg(3, 0, 12, 1, 1)

#define SYS_ICC_IAR0_EL1		sys_reg(3, 0, 12, 8, 0)
#define SYS_ICC_EOIR0_EL1		sys_reg(3, 0, 12, 8, 1)
#define SYS_ICC_HPPIR0_EL1		sys_reg(3, 0, 12, 8, 2)
#define SYS_ICC_BPR0_EL1		sys_reg(3, 0, 12, 8, 3)
#define SYS_ICC_AP0Rn_EL1(n)		sys_reg(3, 0, 12, 8, 4 | n)
#define SYS_ICC_AP0R0_EL1		SYS_ICC_AP0Rn_EL1(0)
#define SYS_ICC_AP0R1_EL1		SYS_ICC_AP0Rn_EL1(1)
#define SYS_ICC_AP0R2_EL1		SYS_ICC_AP0Rn_EL1(2)
#define SYS_ICC_AP0R3_EL1		SYS_ICC_AP0Rn_EL1(3)
#define SYS_ICC_AP1Rn_EL1(n)		sys_reg(3, 0, 12, 9, n)
#define SYS_ICC_AP1R0_EL1		SYS_ICC_AP1Rn_EL1(0)
#define SYS_ICC_AP1R1_EL1		SYS_ICC_AP1Rn_EL1(1)
#define SYS_ICC_AP1R2_EL1		SYS_ICC_AP1Rn_EL1(2)
#define SYS_ICC_AP1R3_EL1		SYS_ICC_AP1Rn_EL1(3)
#define SYS_ICC_DIR_EL1			sys_reg(3, 0, 12, 11, 1)
#define SYS_ICC_RPR_EL1			sys_reg(3, 0, 12, 11, 3)
#define SYS_ICC_SGI1R_EL1		sys_reg(3, 0, 12, 11, 5)
#define SYS_ICC_ASGI1R_EL1		sys_reg(3, 0, 12, 11, 6)
#define SYS_ICC_SGI0R_EL1		sys_reg(3, 0, 12, 11, 7)
#define SYS_ICC_IAR1_EL1		sys_reg(3, 0, 12, 12, 0)
#define SYS_ICC_EOIR1_EL1		sys_reg(3, 0, 12, 12, 1)
#define SYS_ICC_HPPIR1_EL1		sys_reg(3, 0, 12, 12, 2)
#define SYS_ICC_BPR1_EL1		sys_reg(3, 0, 12, 12, 3)
#define SYS_ICC_CTLR_EL1		sys_reg(3, 0, 12, 12, 4)
#define SYS_ICC_SRE_EL1			sys_reg(3, 0, 12, 12, 5)
#define SYS_ICC_IGRPEN0_EL1		sys_reg(3, 0, 12, 12, 6)
#define SYS_ICC_IGRPEN1_EL1		sys_reg(3, 0, 12, 12, 7)

#define SYS_ACCDATA_EL1			sys_reg(3, 0, 13, 0, 5)

#define SYS_CNTKCTL_EL1			sys_reg(3, 0, 14, 1, 0)

#define SYS_AIDR_EL1			sys_reg(3, 1, 0, 0, 7)

#define SYS_RNDR_EL0			sys_reg(3, 3, 2, 4, 0)
#define SYS_RNDRRS_EL0			sys_reg(3, 3, 2, 4, 1)

#define SYS_PMCR_EL0			sys_reg(3, 3, 9, 12, 0)
#define SYS_PMCNTENSET_EL0		sys_reg(3, 3, 9, 12, 1)
#define SYS_PMCNTENCLR_EL0		sys_reg(3, 3, 9, 12, 2)
#define SYS_PMOVSCLR_EL0		sys_reg(3, 3, 9, 12, 3)
#define SYS_PMSWINC_EL0			sys_reg(3, 3, 9, 12, 4)
#define SYS_PMCEID0_EL0			sys_reg(3, 3, 9, 12, 6)
#define SYS_PMCEID1_EL0			sys_reg(3, 3, 9, 12, 7)
#define SYS_PMCCNTR_EL0			sys_reg(3, 3, 9, 13, 0)
#define SYS_PMXEVTYPER_EL0		sys_reg(3, 3, 9, 13, 1)
#define SYS_PMXEVCNTR_EL0		sys_reg(3, 3, 9, 13, 2)
#define SYS_PMUSERENR_EL0		sys_reg(3, 3, 9, 14, 0)
#define SYS_PMOVSSET_EL0		sys_reg(3, 3, 9, 14, 3)

#define SYS_TPIDR_EL0			sys_reg(3, 3, 13, 0, 2)
#define SYS_TPIDRRO_EL0			sys_reg(3, 3, 13, 0, 3)
#define SYS_TPIDR2_EL0			sys_reg(3, 3, 13, 0, 5)

#define SYS_SCXTNUM_EL0			sys_reg(3, 3, 13, 0, 7)

/* Definitions for system register interface to AMU for ARMv8.4 onwards */
#define SYS_AM_EL0(crm, op2)		sys_reg(3, 3, 13, (crm), (op2))
#define SYS_AMCR_EL0			SYS_AM_EL0(2, 0)
#define SYS_AMCFGR_EL0			SYS_AM_EL0(2, 1)
#define SYS_AMCGCR_EL0			SYS_AM_EL0(2, 2)
#define SYS_AMUSERENR_EL0		SYS_AM_EL0(2, 3)
#define SYS_AMCNTENCLR0_EL0		SYS_AM_EL0(2, 4)
#define SYS_AMCNTENSET0_EL0		SYS_AM_EL0(2, 5)
#define SYS_AMCNTENCLR1_EL0		SYS_AM_EL0(3, 0)
#define SYS_AMCNTENSET1_EL0		SYS_AM_EL0(3, 1)

/*
 * Group 0 of activity monitors (architected):
 *                op0  op1  CRn   CRm       op2
 * Counter:       11   011  1101  010:n<3>  n<2:0>
 * Type:          11   011  1101  011:n<3>  n<2:0>
 * n: 0-15
 *
 * Group 1 of activity monitors (auxiliary):
 *                op0  op1  CRn   CRm       op2
 * Counter:       11   011  1101  110:n<3>  n<2:0>
 * Type:          11   011  1101  111:n<3>  n<2:0>
 * n: 0-15
 */

#define SYS_AMEVCNTR0_EL0(n)		SYS_AM_EL0(4 + ((n) >> 3), (n) & 7)
#define SYS_AMEVTYPER0_EL0(n)		SYS_AM_EL0(6 + ((n) >> 3), (n) & 7)
#define SYS_AMEVCNTR1_EL0(n)		SYS_AM_EL0(12 + ((n) >> 3), (n) & 7)
#define SYS_AMEVTYPER1_EL0(n)		SYS_AM_EL0(14 + ((n) >> 3), (n) & 7)

/* AMU v1: Fixed (architecturally defined) activity monitors */
#define SYS_AMEVCNTR0_CORE_EL0		SYS_AMEVCNTR0_EL0(0)
#define SYS_AMEVCNTR0_CONST_EL0		SYS_AMEVCNTR0_EL0(1)
#define SYS_AMEVCNTR0_INST_RET_EL0	SYS_AMEVCNTR0_EL0(2)
#define SYS_AMEVCNTR0_MEM_STALL		SYS_AMEVCNTR0_EL0(3)

#define SYS_CNTFRQ_EL0			sys_reg(3, 3, 14, 0, 0)

#define SYS_CNTPCT_EL0			sys_reg(3, 3, 14, 0, 1)
#define SYS_CNTVCT_EL0			sys_reg(3, 3, 14, 0, 2)
#define SYS_CNTPCTSS_EL0		sys_reg(3, 3, 14, 0, 5)
#define SYS_CNTVCTSS_EL0		sys_reg(3, 3, 14, 0, 6)

#define SYS_CNTP_TVAL_EL0		sys_reg(3, 3, 14, 2, 0)
#define SYS_CNTP_CTL_EL0		sys_reg(3, 3, 14, 2, 1)
#define SYS_CNTP_CVAL_EL0		sys_reg(3, 3, 14, 2, 2)

#define SYS_CNTV_TVAL_EL0		sys_reg(3, 3, 14, 3, 0)
#define SYS_CNTV_CTL_EL0		sys_reg(3, 3, 14, 3, 1)
#define SYS_CNTV_CVAL_EL0		sys_reg(3, 3, 14, 3, 2)

#define SYS_AARCH32_CNTP_TVAL		sys_reg(0, 0, 14, 2, 0)
#define SYS_AARCH32_CNTP_CTL		sys_reg(0, 0, 14, 2, 1)
#define SYS_AARCH32_CNTPCT		sys_reg(0, 0, 0, 14, 0)
#define SYS_AARCH32_CNTVCT		sys_reg(0, 1, 0, 14, 0)
#define SYS_AARCH32_CNTP_CVAL		sys_reg(0, 2, 0, 14, 0)
#define SYS_AARCH32_CNTPCTSS		sys_reg(0, 8, 0, 14, 0)
#define SYS_AARCH32_CNTVCTSS		sys_reg(0, 9, 0, 14, 0)

#define __PMEV_op2(n)			((n) & 0x7)
#define __CNTR_CRm(n)			(0x8 | (((n) >> 3) & 0x3))
#define SYS_PMEVCNTRn_EL0(n)		sys_reg(3, 3, 14, __CNTR_CRm(n), __PMEV_op2(n))
#define __TYPER_CRm(n)			(0xc | (((n) >> 3) & 0x3))
#define SYS_PMEVTYPERn_EL0(n)		sys_reg(3, 3, 14, __TYPER_CRm(n), __PMEV_op2(n))

#define SYS_PMCCFILTR_EL0		sys_reg(3, 3, 14, 15, 7)

#define SYS_VPIDR_EL2			sys_reg(3, 4, 0, 0, 0)
#define SYS_VMPIDR_EL2			sys_reg(3, 4, 0, 0, 5)

#define SYS_SCTLR_EL2			sys_reg(3, 4, 1, 0, 0)
#define SYS_ACTLR_EL2			sys_reg(3, 4, 1, 0, 1)
#define SYS_SCTLR2_EL2			sys_reg(3, 4, 1, 0, 3)
#define SYS_HCR_EL2			sys_reg(3, 4, 1, 1, 0)
#define SYS_MDCR_EL2			sys_reg(3, 4, 1, 1, 1)
#define SYS_CPTR_EL2			sys_reg(3, 4, 1, 1, 2)
#define SYS_HSTR_EL2			sys_reg(3, 4, 1, 1, 3)
#define SYS_HACR_EL2			sys_reg(3, 4, 1, 1, 7)

#define SYS_TTBR0_EL2			sys_reg(3, 4, 2, 0, 0)
#define SYS_TTBR1_EL2			sys_reg(3, 4, 2, 0, 1)
#define SYS_TCR_EL2			sys_reg(3, 4, 2, 0, 2)
#define SYS_VTTBR_EL2			sys_reg(3, 4, 2, 1, 0)
#define SYS_VTCR_EL2			sys_reg(3, 4, 2, 1, 2)

#define SYS_VNCR_EL2			sys_reg(3, 4, 2, 2, 0)
#define SYS_HAFGRTR_EL2			sys_reg(3, 4, 3, 1, 6)
#define SYS_SPSR_EL2			sys_reg(3, 4, 4, 0, 0)
#define SYS_ELR_EL2			sys_reg(3, 4, 4, 0, 1)
#define SYS_SP_EL1			sys_reg(3, 4, 4, 1, 0)
#define SYS_SPSR_irq			sys_reg(3, 4, 4, 3, 0)
#define SYS_SPSR_abt			sys_reg(3, 4, 4, 3, 1)
#define SYS_SPSR_und			sys_reg(3, 4, 4, 3, 2)
#define SYS_SPSR_fiq			sys_reg(3, 4, 4, 3, 3)
#define SYS_IFSR32_EL2			sys_reg(3, 4, 5, 0, 1)
#define SYS_AFSR0_EL2			sys_reg(3, 4, 5, 1, 0)
#define SYS_AFSR1_EL2			sys_reg(3, 4, 5, 1, 1)
#define SYS_ESR_EL2			sys_reg(3, 4, 5, 2, 0)
#define SYS_VSESR_EL2			sys_reg(3, 4, 5, 2, 3)
#define SYS_FPEXC32_EL2			sys_reg(3, 4, 5, 3, 0)
#define SYS_TFSR_EL2			sys_reg(3, 4, 5, 6, 0)

#define SYS_FAR_EL2			sys_reg(3, 4, 6, 0, 0)
#define SYS_HPFAR_EL2			sys_reg(3, 4, 6, 0, 4)

#define SYS_MAIR_EL2			sys_reg(3, 4, 10, 2, 0)
#define SYS_AMAIR_EL2			sys_reg(3, 4, 10, 3, 0)

#define SYS_VBAR_EL2			sys_reg(3, 4, 12, 0, 0)
#define SYS_RVBAR_EL2			sys_reg(3, 4, 12, 0, 1)
#define SYS_RMR_EL2			sys_reg(3, 4, 12, 0, 2)
#define SYS_VDISR_EL2			sys_reg(3, 4, 12, 1, 1)
#define __SYS__AP0Rx_EL2(x)		sys_reg(3, 4, 12, 8, x)
#define SYS_ICH_AP0R0_EL2		__SYS__AP0Rx_EL2(0)
#define SYS_ICH_AP0R1_EL2		__SYS__AP0Rx_EL2(1)
#define SYS_ICH_AP0R2_EL2		__SYS__AP0Rx_EL2(2)
#define SYS_ICH_AP0R3_EL2		__SYS__AP0Rx_EL2(3)

#define __SYS__AP1Rx_EL2(x)		sys_reg(3, 4, 12, 9, x)
#define SYS_ICH_AP1R0_EL2		__SYS__AP1Rx_EL2(0)
#define SYS_ICH_AP1R1_EL2		__SYS__AP1Rx_EL2(1)
#define SYS_ICH_AP1R2_EL2		__SYS__AP1Rx_EL2(2)
#define SYS_ICH_AP1R3_EL2		__SYS__AP1Rx_EL2(3)

#define SYS_ICH_VSEIR_EL2		sys_reg(3, 4, 12, 9, 4)
#define SYS_ICC_SRE_EL2			sys_reg(3, 4, 12, 9, 5)
#define SYS_ICH_EISR_EL2		sys_reg(3, 4, 12, 11, 3)
#define SYS_ICH_ELRSR_EL2		sys_reg(3, 4, 12, 11, 5)
#define SYS_ICH_VMCR_EL2		sys_reg(3, 4, 12, 11, 7)

#define __SYS__LR0_EL2(x)		sys_reg(3, 4, 12, 12, x)
#define SYS_ICH_LR0_EL2			__SYS__LR0_EL2(0)
#define SYS_ICH_LR1_EL2			__SYS__LR0_EL2(1)
#define SYS_ICH_LR2_EL2			__SYS__LR0_EL2(2)
#define SYS_ICH_LR3_EL2			__SYS__LR0_EL2(3)
#define SYS_ICH_LR4_EL2			__SYS__LR0_EL2(4)
#define SYS_ICH_LR5_EL2			__SYS__LR0_EL2(5)
#define SYS_ICH_LR6_EL2			__SYS__LR0_EL2(6)
#define SYS_ICH_LR7_EL2			__SYS__LR0_EL2(7)

#define __SYS__LR8_EL2(x)		sys_reg(3, 4, 12, 13, x)
#define SYS_ICH_LR8_EL2			__SYS__LR8_EL2(0)
#define SYS_ICH_LR9_EL2			__SYS__LR8_EL2(1)
#define SYS_ICH_LR10_EL2		__SYS__LR8_EL2(2)
#define SYS_ICH_LR11_EL2		__SYS__LR8_EL2(3)
#define SYS_ICH_LR12_EL2		__SYS__LR8_EL2(4)
#define SYS_ICH_LR13_EL2		__SYS__LR8_EL2(5)
#define SYS_ICH_LR14_EL2		__SYS__LR8_EL2(6)
#define SYS_ICH_LR15_EL2		__SYS__LR8_EL2(7)

#define SYS_CONTEXTIDR_EL2		sys_reg(3, 4, 13, 0, 1)
#define SYS_TPIDR_EL2			sys_reg(3, 4, 13, 0, 2)
#define SYS_SCXTNUM_EL2			sys_reg(3, 4, 13, 0, 7)

#define __AMEV_op2(m)			(m & 0x7)
#define __AMEV_CRm(n, m)		(n | ((m & 0x8) >> 3))
#define __SYS__AMEVCNTVOFF0n_EL2(m)	sys_reg(3, 4, 13, __AMEV_CRm(0x8, m), __AMEV_op2(m))
#define SYS_AMEVCNTVOFF0n_EL2(m)	__SYS__AMEVCNTVOFF0n_EL2(m)
#define __SYS__AMEVCNTVOFF1n_EL2(m)	sys_reg(3, 4, 13, __AMEV_CRm(0xA, m), __AMEV_op2(m))
#define SYS_AMEVCNTVOFF1n_EL2(m)	__SYS__AMEVCNTVOFF1n_EL2(m)

#define SYS_CNTVOFF_EL2			sys_reg(3, 4, 14, 0, 3)
#define SYS_CNTHCTL_EL2			sys_reg(3, 4, 14, 1, 0)
#define SYS_CNTHP_TVAL_EL2		sys_reg(3, 4, 14, 2, 0)
#define SYS_CNTHP_CTL_EL2		sys_reg(3, 4, 14, 2, 1)
#define SYS_CNTHP_CVAL_EL2		sys_reg(3, 4, 14, 2, 2)
#define SYS_CNTHV_TVAL_EL2		sys_reg(3, 4, 14, 3, 0)
#define SYS_CNTHV_CTL_EL2		sys_reg(3, 4, 14, 3, 1)
#define SYS_CNTHV_CVAL_EL2		sys_reg(3, 4, 14, 3, 2)

/* VHE encodings for architectural EL0/1 system registers */
#define SYS_BRBCR_EL12			sys_reg(2, 5, 9, 0, 0)
#define SYS_SCTLR_EL12			sys_reg(3, 5, 1, 0, 0)
#define SYS_CPACR_EL12			sys_reg(3, 5, 1, 0, 2)
#define SYS_SCTLR2_EL12			sys_reg(3, 5, 1, 0, 3)
#define SYS_ZCR_EL12			sys_reg(3, 5, 1, 2, 0)
#define SYS_TRFCR_EL12			sys_reg(3, 5, 1, 2, 1)
#define SYS_SMCR_EL12			sys_reg(3, 5, 1, 2, 6)
#define SYS_TTBR0_EL12			sys_reg(3, 5, 2, 0, 0)
#define SYS_TTBR1_EL12			sys_reg(3, 5, 2, 0, 1)
#define SYS_TCR_EL12			sys_reg(3, 5, 2, 0, 2)
#define SYS_TCR2_EL12			sys_reg(3, 5, 2, 0, 3)
#define SYS_SPSR_EL12			sys_reg(3, 5, 4, 0, 0)
#define SYS_ELR_EL12			sys_reg(3, 5, 4, 0, 1)
#define SYS_AFSR0_EL12			sys_reg(3, 5, 5, 1, 0)
#define SYS_AFSR1_EL12			sys_reg(3, 5, 5, 1, 1)
#define SYS_ESR_EL12			sys_reg(3, 5, 5, 2, 0)
#define SYS_TFSR_EL12			sys_reg(3, 5, 5, 6, 0)
#define SYS_FAR_EL12			sys_reg(3, 5, 6, 0, 0)
#define SYS_PMSCR_EL12			sys_reg(3, 5, 9, 9, 0)
#define SYS_MAIR_EL12			sys_reg(3, 5, 10, 2, 0)
#define SYS_AMAIR_EL12			sys_reg(3, 5, 10, 3, 0)
#define SYS_VBAR_EL12			sys_reg(3, 5, 12, 0, 0)
#define SYS_CONTEXTIDR_EL12		sys_reg(3, 5, 13, 0, 1)
#define SYS_SCXTNUM_EL12		sys_reg(3, 5, 13, 0, 7)
#define SYS_CNTKCTL_EL12		sys_reg(3, 5, 14, 1, 0)
#define SYS_CNTP_TVAL_EL02		sys_reg(3, 5, 14, 2, 0)
#define SYS_CNTP_CTL_EL02		sys_reg(3, 5, 14, 2, 1)
#define SYS_CNTP_CVAL_EL02		sys_reg(3, 5, 14, 2, 2)
#define SYS_CNTV_TVAL_EL02		sys_reg(3, 5, 14, 3, 0)
#define SYS_CNTV_CTL_EL02		sys_reg(3, 5, 14, 3, 1)
#define SYS_CNTV_CVAL_EL02		sys_reg(3, 5, 14, 3, 2)

#define SYS_SP_EL2			sys_reg(3, 6,  4, 1, 0)

/* AT instructions */
#define AT_Op0 1
#define AT_CRn 7

#define OP_AT_S1E1R	sys_insn(AT_Op0, 0, AT_CRn, 8, 0)
#define OP_AT_S1E1W	sys_insn(AT_Op0, 0, AT_CRn, 8, 1)
#define OP_AT_S1E0R	sys_insn(AT_Op0, 0, AT_CRn, 8, 2)
#define OP_AT_S1E0W	sys_insn(AT_Op0, 0, AT_CRn, 8, 3)
#define OP_AT_S1E1RP	sys_insn(AT_Op0, 0, AT_CRn, 9, 0)
#define OP_AT_S1E1WP	sys_insn(AT_Op0, 0, AT_CRn, 9, 1)
#define OP_AT_S1E1A	sys_insn(AT_Op0, 0, AT_CRn, 9, 2)
#define OP_AT_S1E2R	sys_insn(AT_Op0, 4, AT_CRn, 8, 0)
#define OP_AT_S1E2W	sys_insn(AT_Op0, 4, AT_CRn, 8, 1)
#define OP_AT_S12E1R	sys_insn(AT_Op0, 4, AT_CRn, 8, 4)
#define OP_AT_S12E1W	sys_insn(AT_Op0, 4, AT_CRn, 8, 5)
#define OP_AT_S12E0R	sys_insn(AT_Op0, 4, AT_CRn, 8, 6)
#define OP_AT_S12E0W	sys_insn(AT_Op0, 4, AT_CRn, 8, 7)
#define OP_AT_S1E2A	sys_insn(AT_Op0, 4, AT_CRn, 9, 2)

/* TLBI instructions */
#define TLBI_Op0	1

#define TLBI_Op1_EL1	0	/* Accessible from EL1 or higher */
#define TLBI_Op1_EL2	4	/* Accessible from EL2 or higher */

#define TLBI_CRn_XS	8	/* Extra Slow (the common one) */
#define TLBI_CRn_nXS	9	/* not Extra Slow (which nobody uses)*/

#define TLBI_CRm_IPAIS	0	/* S2 Inner-Shareable */
#define TLBI_CRm_nROS	1	/* non-Range, Outer-Sharable */
#define TLBI_CRm_RIS	2	/* Range, Inner-Sharable */
#define TLBI_CRm_nRIS	3	/* non-Range, Inner-Sharable */
#define TLBI_CRm_IPAONS	4	/* S2 Outer and Non-Shareable */
#define TLBI_CRm_ROS	5	/* Range, Outer-Sharable */
#define TLBI_CRm_RNS	6	/* Range, Non-Sharable */
#define TLBI_CRm_nRNS	7	/* non-Range, Non-Sharable */

#define OP_TLBI_VMALLE1OS		sys_insn(1, 0, 8, 1, 0)
#define OP_TLBI_VAE1OS			sys_insn(1, 0, 8, 1, 1)
#define OP_TLBI_ASIDE1OS		sys_insn(1, 0, 8, 1, 2)
#define OP_TLBI_VAAE1OS			sys_insn(1, 0, 8, 1, 3)
#define OP_TLBI_VALE1OS			sys_insn(1, 0, 8, 1, 5)
#define OP_TLBI_VAALE1OS		sys_insn(1, 0, 8, 1, 7)
#define OP_TLBI_RVAE1IS			sys_insn(1, 0, 8, 2, 1)
#define OP_TLBI_RVAAE1IS		sys_insn(1, 0, 8, 2, 3)
#define OP_TLBI_RVALE1IS		sys_insn(1, 0, 8, 2, 5)
#define OP_TLBI_RVAALE1IS		sys_insn(1, 0, 8, 2, 7)
#define OP_TLBI_VMALLE1IS		sys_insn(1, 0, 8, 3, 0)
#define OP_TLBI_VAE1IS			sys_insn(1, 0, 8, 3, 1)
#define OP_TLBI_ASIDE1IS		sys_insn(1, 0, 8, 3, 2)
#define OP_TLBI_VAAE1IS			sys_insn(1, 0, 8, 3, 3)
#define OP_TLBI_VALE1IS			sys_insn(1, 0, 8, 3, 5)
#define OP_TLBI_VAALE1IS		sys_insn(1, 0, 8, 3, 7)
#define OP_TLBI_RVAE1OS			sys_insn(1, 0, 8, 5, 1)
#define OP_TLBI_RVAAE1OS		sys_insn(1, 0, 8, 5, 3)
#define OP_TLBI_RVALE1OS		sys_insn(1, 0, 8, 5, 5)
#define OP_TLBI_RVAALE1OS		sys_insn(1, 0, 8, 5, 7)
#define OP_TLBI_RVAE1			sys_insn(1, 0, 8, 6, 1)
#define OP_TLBI_RVAAE1			sys_insn(1, 0, 8, 6, 3)
#define OP_TLBI_RVALE1			sys_insn(1, 0, 8, 6, 5)
#define OP_TLBI_RVAALE1			sys_insn(1, 0, 8, 6, 7)
#define OP_TLBI_VMALLE1			sys_insn(1, 0, 8, 7, 0)
#define OP_TLBI_VAE1			sys_insn(1, 0, 8, 7, 1)
#define OP_TLBI_ASIDE1			sys_insn(1, 0, 8, 7, 2)
#define OP_TLBI_VAAE1			sys_insn(1, 0, 8, 7, 3)
#define OP_TLBI_VALE1			sys_insn(1, 0, 8, 7, 5)
#define OP_TLBI_VAALE1			sys_insn(1, 0, 8, 7, 7)
#define OP_TLBI_VMALLE1OSNXS		sys_insn(1, 0, 9, 1, 0)
#define OP_TLBI_VAE1OSNXS		sys_insn(1, 0, 9, 1, 1)
#define OP_TLBI_ASIDE1OSNXS		sys_insn(1, 0, 9, 1, 2)
#define OP_TLBI_VAAE1OSNXS		sys_insn(1, 0, 9, 1, 3)
#define OP_TLBI_VALE1OSNXS		sys_insn(1, 0, 9, 1, 5)
#define OP_TLBI_VAALE1OSNXS		sys_insn(1, 0, 9, 1, 7)
#define OP_TLBI_RVAE1ISNXS		sys_insn(1, 0, 9, 2, 1)
#define OP_TLBI_RVAAE1ISNXS		sys_insn(1, 0, 9, 2, 3)
#define OP_TLBI_RVALE1ISNXS		sys_insn(1, 0, 9, 2, 5)
#define OP_TLBI_RVAALE1ISNXS		sys_insn(1, 0, 9, 2, 7)
#define OP_TLBI_VMALLE1ISNXS		sys_insn(1, 0, 9, 3, 0)
#define OP_TLBI_VAE1ISNXS		sys_insn(1, 0, 9, 3, 1)
#define OP_TLBI_ASIDE1ISNXS		sys_insn(1, 0, 9, 3, 2)
#define OP_TLBI_VAAE1ISNXS		sys_insn(1, 0, 9, 3, 3)
#define OP_TLBI_VALE1ISNXS		sys_insn(1, 0, 9, 3, 5)
#define OP_TLBI_VAALE1ISNXS		sys_insn(1, 0, 9, 3, 7)
#define OP_TLBI_RVAE1OSNXS		sys_insn(1, 0, 9, 5, 1)
#define OP_TLBI_RVAAE1OSNXS		sys_insn(1, 0, 9, 5, 3)
#define OP_TLBI_RVALE1OSNXS		sys_insn(1, 0, 9, 5, 5)
#define OP_TLBI_RVAALE1OSNXS		sys_insn(1, 0, 9, 5, 7)
#define OP_TLBI_RVAE1NXS		sys_insn(1, 0, 9, 6, 1)
#define OP_TLBI_RVAAE1NXS		sys_insn(1, 0, 9, 6, 3)
#define OP_TLBI_RVALE1NXS		sys_insn(1, 0, 9, 6, 5)
#define OP_TLBI_RVAALE1NXS		sys_insn(1, 0, 9, 6, 7)
#define OP_TLBI_VMALLE1NXS		sys_insn(1, 0, 9, 7, 0)
#define OP_TLBI_VAE1NXS			sys_insn(1, 0, 9, 7, 1)
#define OP_TLBI_ASIDE1NXS		sys_insn(1, 0, 9, 7, 2)
#define OP_TLBI_VAAE1NXS		sys_insn(1, 0, 9, 7, 3)
#define OP_TLBI_VALE1NXS		sys_insn(1, 0, 9, 7, 5)
#define OP_TLBI_VAALE1NXS		sys_insn(1, 0, 9, 7, 7)
#define OP_TLBI_IPAS2E1IS		sys_insn(1, 4, 8, 0, 1)
#define OP_TLBI_RIPAS2E1IS		sys_insn(1, 4, 8, 0, 2)
#define OP_TLBI_IPAS2LE1IS		sys_insn(1, 4, 8, 0, 5)
#define OP_TLBI_RIPAS2LE1IS		sys_insn(1, 4, 8, 0, 6)
#define OP_TLBI_ALLE2OS			sys_insn(1, 4, 8, 1, 0)
#define OP_TLBI_VAE2OS			sys_insn(1, 4, 8, 1, 1)
#define OP_TLBI_ALLE1OS			sys_insn(1, 4, 8, 1, 4)
#define OP_TLBI_VALE2OS			sys_insn(1, 4, 8, 1, 5)
#define OP_TLBI_VMALLS12E1OS		sys_insn(1, 4, 8, 1, 6)
#define OP_TLBI_RVAE2IS			sys_insn(1, 4, 8, 2, 1)
#define OP_TLBI_RVALE2IS		sys_insn(1, 4, 8, 2, 5)
#define OP_TLBI_ALLE2IS			sys_insn(1, 4, 8, 3, 0)
#define OP_TLBI_VAE2IS			sys_insn(1, 4, 8, 3, 1)
#define OP_TLBI_ALLE1IS			sys_insn(1, 4, 8, 3, 4)
#define OP_TLBI_VALE2IS			sys_insn(1, 4, 8, 3, 5)
#define OP_TLBI_VMALLS12E1IS		sys_insn(1, 4, 8, 3, 6)
#define OP_TLBI_IPAS2E1OS		sys_insn(1, 4, 8, 4, 0)
#define OP_TLBI_IPAS2E1			sys_insn(1, 4, 8, 4, 1)
#define OP_TLBI_RIPAS2E1		sys_insn(1, 4, 8, 4, 2)
#define OP_TLBI_RIPAS2E1OS		sys_insn(1, 4, 8, 4, 3)
#define OP_TLBI_IPAS2LE1OS		sys_insn(1, 4, 8, 4, 4)
#define OP_TLBI_IPAS2LE1		sys_insn(1, 4, 8, 4, 5)
#define OP_TLBI_RIPAS2LE1		sys_insn(1, 4, 8, 4, 6)
#define OP_TLBI_RIPAS2LE1OS		sys_insn(1, 4, 8, 4, 7)
#define OP_TLBI_RVAE2OS			sys_insn(1, 4, 8, 5, 1)
#define OP_TLBI_RVALE2OS		sys_insn(1, 4, 8, 5, 5)
#define OP_TLBI_RVAE2			sys_insn(1, 4, 8, 6, 1)
#define OP_TLBI_RVALE2			sys_insn(1, 4, 8, 6, 5)
#define OP_TLBI_ALLE2			sys_insn(1, 4, 8, 7, 0)
#define OP_TLBI_VAE2			sys_insn(1, 4, 8, 7, 1)
#define OP_TLBI_ALLE1			sys_insn(1, 4, 8, 7, 4)
#define OP_TLBI_VALE2			sys_insn(1, 4, 8, 7, 5)
#define OP_TLBI_VMALLS12E1		sys_insn(1, 4, 8, 7, 6)
#define OP_TLBI_IPAS2E1ISNXS		sys_insn(1, 4, 9, 0, 1)
#define OP_TLBI_RIPAS2E1ISNXS		sys_insn(1, 4, 9, 0, 2)
#define OP_TLBI_IPAS2LE1ISNXS		sys_insn(1, 4, 9, 0, 5)
#define OP_TLBI_RIPAS2LE1ISNXS		sys_insn(1, 4, 9, 0, 6)
#define OP_TLBI_ALLE2OSNXS		sys_insn(1, 4, 9, 1, 0)
#define OP_TLBI_VAE2OSNXS		sys_insn(1, 4, 9, 1, 1)
#define OP_TLBI_ALLE1OSNXS		sys_insn(1, 4, 9, 1, 4)
#define OP_TLBI_VALE2OSNXS		sys_insn(1, 4, 9, 1, 5)
#define OP_TLBI_VMALLS12E1OSNXS		sys_insn(1, 4, 9, 1, 6)
#define OP_TLBI_RVAE2ISNXS		sys_insn(1, 4, 9, 2, 1)
#define OP_TLBI_RVALE2ISNXS		sys_insn(1, 4, 9, 2, 5)
#define OP_TLBI_ALLE2ISNXS		sys_insn(1, 4, 9, 3, 0)
#define OP_TLBI_VAE2ISNXS		sys_insn(1, 4, 9, 3, 1)
#define OP_TLBI_ALLE1ISNXS		sys_insn(1, 4, 9, 3, 4)
#define OP_TLBI_VALE2ISNXS		sys_insn(1, 4, 9, 3, 5)
#define OP_TLBI_VMALLS12E1ISNXS		sys_insn(1, 4, 9, 3, 6)
#define OP_TLBI_IPAS2E1OSNXS		sys_insn(1, 4, 9, 4, 0)
#define OP_TLBI_IPAS2E1NXS		sys_insn(1, 4, 9, 4, 1)
#define OP_TLBI_RIPAS2E1NXS		sys_insn(1, 4, 9, 4, 2)
#define OP_TLBI_RIPAS2E1OSNXS		sys_insn(1, 4, 9, 4, 3)
#define OP_TLBI_IPAS2LE1OSNXS		sys_insn(1, 4, 9, 4, 4)
#define OP_TLBI_IPAS2LE1NXS		sys_insn(1, 4, 9, 4, 5)
#define OP_TLBI_RIPAS2LE1NXS		sys_insn(1, 4, 9, 4, 6)
#define OP_TLBI_RIPAS2LE1OSNXS		sys_insn(1, 4, 9, 4, 7)
#define OP_TLBI_RVAE2OSNXS		sys_insn(1, 4, 9, 5, 1)
#define OP_TLBI_RVALE2OSNXS		sys_insn(1, 4, 9, 5, 5)
#define OP_TLBI_RVAE2NXS		sys_insn(1, 4, 9, 6, 1)
#define OP_TLBI_RVALE2NXS		sys_insn(1, 4, 9, 6, 5)
#define OP_TLBI_ALLE2NXS		sys_insn(1, 4, 9, 7, 0)
#define OP_TLBI_VAE2NXS			sys_insn(1, 4, 9, 7, 1)
#define OP_TLBI_ALLE1NXS		sys_insn(1, 4, 9, 7, 4)
#define OP_TLBI_VALE2NXS		sys_insn(1, 4, 9, 7, 5)
#define OP_TLBI_VMALLS12E1NXS		sys_insn(1, 4, 9, 7, 6)

/* Misc instructions */
#define OP_GCSPUSHX			sys_insn(1, 0, 7, 7, 4)
#define OP_GCSPOPCX			sys_insn(1, 0, 7, 7, 5)
#define OP_GCSPOPX			sys_insn(1, 0, 7, 7, 6)
#define OP_GCSPUSHM			sys_insn(1, 3, 7, 7, 0)

#define OP_BRB_IALL			sys_insn(1, 1, 7, 2, 4)
#define OP_BRB_INJ			sys_insn(1, 1, 7, 2, 5)
#define OP_CFP_RCTX			sys_insn(1, 3, 7, 3, 4)
#define OP_DVP_RCTX			sys_insn(1, 3, 7, 3, 5)
#define OP_COSP_RCTX			sys_insn(1, 3, 7, 3, 6)
#define OP_CPP_RCTX			sys_insn(1, 3, 7, 3, 7)

/* Common SCTLR_ELx flags. */
#define SCTLR_ELx_ENTP2	(BIT(60))
#define SCTLR_ELx_DSSBS	(BIT(44))
#define SCTLR_ELx_ATA	(BIT(43))

#define SCTLR_ELx_EE_SHIFT	25
#define SCTLR_ELx_ENIA_SHIFT	31

#define SCTLR_ELx_ITFSB	 (BIT(37))
#define SCTLR_ELx_ENIA	 (BIT(SCTLR_ELx_ENIA_SHIFT))
#define SCTLR_ELx_ENIB	 (BIT(30))
#define SCTLR_ELx_LSMAOE (BIT(29))
#define SCTLR_ELx_nTLSMD (BIT(28))
#define SCTLR_ELx_ENDA	 (BIT(27))
#define SCTLR_ELx_EE     (BIT(SCTLR_ELx_EE_SHIFT))
#define SCTLR_ELx_EIS	 (BIT(22))
#define SCTLR_ELx_IESB	 (BIT(21))
#define SCTLR_ELx_TSCXT	 (BIT(20))
#define SCTLR_ELx_WXN	 (BIT(19))
#define SCTLR_ELx_ENDB	 (BIT(13))
#define SCTLR_ELx_I	 (BIT(12))
#define SCTLR_ELx_EOS	 (BIT(11))
#define SCTLR_ELx_SA	 (BIT(3))
#define SCTLR_ELx_C	 (BIT(2))
#define SCTLR_ELx_A	 (BIT(1))
#define SCTLR_ELx_M	 (BIT(0))

/* SCTLR_EL2 specific flags. */
#define SCTLR_EL2_RES1	((BIT(4))  | (BIT(5))  | (BIT(11)) | (BIT(16)) | \
			 (BIT(18)) | (BIT(22)) | (BIT(23)) | (BIT(28)) | \
			 (BIT(29)))

#define SCTLR_EL2_BT	(BIT(36))
#ifdef CONFIG_CPU_BIG_ENDIAN
#define ENDIAN_SET_EL2		SCTLR_ELx_EE
#else
#define ENDIAN_SET_EL2		0
#endif

#define INIT_SCTLR_EL2_MMU_ON						\
	(SCTLR_ELx_M  | SCTLR_ELx_C | SCTLR_ELx_SA | SCTLR_ELx_I |	\
	 SCTLR_ELx_IESB | SCTLR_ELx_WXN | ENDIAN_SET_EL2 |		\
	 SCTLR_ELx_ITFSB | SCTLR_EL2_RES1)

#define INIT_SCTLR_EL2_MMU_OFF \
	(SCTLR_EL2_RES1 | ENDIAN_SET_EL2)

/* SCTLR_EL1 specific flags. */
#ifdef CONFIG_CPU_BIG_ENDIAN
#define ENDIAN_SET_EL1		(SCTLR_EL1_E0E | SCTLR_ELx_EE)
#else
#define ENDIAN_SET_EL1		0
#endif

#define INIT_SCTLR_EL1_MMU_OFF \
	(ENDIAN_SET_EL1 | SCTLR_EL1_LSMAOE | SCTLR_EL1_nTLSMD | \
	 SCTLR_EL1_EIS  | SCTLR_EL1_TSCXT  | SCTLR_EL1_EOS)

#define INIT_SCTLR_EL1_MMU_ON \
	(SCTLR_ELx_M      | SCTLR_ELx_C      | SCTLR_ELx_SA    | \
	 SCTLR_EL1_SA0    | SCTLR_EL1_SED    | SCTLR_ELx_I     | \
	 SCTLR_EL1_DZE    | SCTLR_EL1_UCT    | SCTLR_EL1_nTWE  | \
	 SCTLR_ELx_IESB   | SCTLR_EL1_SPAN   | SCTLR_ELx_ITFSB | \
	 ENDIAN_SET_EL1   | SCTLR_EL1_UCI    | SCTLR_EL1_EPAN  | \
	 SCTLR_EL1_LSMAOE | SCTLR_EL1_nTLSMD | SCTLR_EL1_EIS   | \
	 SCTLR_EL1_TSCXT  | SCTLR_EL1_EOS)

/* MAIR_ELx memory attributes (used by Linux) */
#define MAIR_ATTR_DEVICE_nGnRnE		UL(0x00)
#define MAIR_ATTR_DEVICE_nGnRE		UL(0x04)
#define MAIR_ATTR_NORMAL_NC		UL(0x44)
#define MAIR_ATTR_NORMAL_TAGGED		UL(0xf0)
#define MAIR_ATTR_NORMAL		UL(0xff)
#define MAIR_ATTR_MASK			UL(0xff)

/* Position the attr at the correct index */
#define MAIR_ATTRIDX(attr, idx)		((attr) << ((idx) * 8))

/* id_aa64mmfr0 */
#define ID_AA64MMFR0_EL1_TGRAN4_SUPPORTED_MIN	0x0
#define ID_AA64MMFR0_EL1_TGRAN4_LPA2		ID_AA64MMFR0_EL1_TGRAN4_52_BIT
#define ID_AA64MMFR0_EL1_TGRAN4_SUPPORTED_MAX	0x7
#define ID_AA64MMFR0_EL1_TGRAN64_SUPPORTED_MIN	0x0
#define ID_AA64MMFR0_EL1_TGRAN64_SUPPORTED_MAX	0x7
#define ID_AA64MMFR0_EL1_TGRAN16_SUPPORTED_MIN	0x1
#define ID_AA64MMFR0_EL1_TGRAN16_LPA2		ID_AA64MMFR0_EL1_TGRAN16_52_BIT
#define ID_AA64MMFR0_EL1_TGRAN16_SUPPORTED_MAX	0xf

#define ARM64_MIN_PARANGE_BITS		32

#define ID_AA64MMFR0_EL1_TGRAN_2_SUPPORTED_DEFAULT	0x0
#define ID_AA64MMFR0_EL1_TGRAN_2_SUPPORTED_NONE		0x1
#define ID_AA64MMFR0_EL1_TGRAN_2_SUPPORTED_MIN		0x2
#define ID_AA64MMFR0_EL1_TGRAN_2_SUPPORTED_LPA2		0x3
#define ID_AA64MMFR0_EL1_TGRAN_2_SUPPORTED_MAX		0x7

#ifdef CONFIG_ARM64_PA_BITS_52
#define ID_AA64MMFR0_EL1_PARANGE_MAX	ID_AA64MMFR0_EL1_PARANGE_52
#else
#define ID_AA64MMFR0_EL1_PARANGE_MAX	ID_AA64MMFR0_EL1_PARANGE_48
#endif

#if defined(CONFIG_ARM64_4K_PAGES)
#define ID_AA64MMFR0_EL1_TGRAN_SHIFT		ID_AA64MMFR0_EL1_TGRAN4_SHIFT
#define ID_AA64MMFR0_EL1_TGRAN_LPA2		ID_AA64MMFR0_EL1_TGRAN4_52_BIT
#define ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MIN	ID_AA64MMFR0_EL1_TGRAN4_SUPPORTED_MIN
#define ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MAX	ID_AA64MMFR0_EL1_TGRAN4_SUPPORTED_MAX
#define ID_AA64MMFR0_EL1_TGRAN_2_SHIFT		ID_AA64MMFR0_EL1_TGRAN4_2_SHIFT
#elif defined(CONFIG_ARM64_16K_PAGES)
#define ID_AA64MMFR0_EL1_TGRAN_SHIFT		ID_AA64MMFR0_EL1_TGRAN16_SHIFT
#define ID_AA64MMFR0_EL1_TGRAN_LPA2		ID_AA64MMFR0_EL1_TGRAN16_52_BIT
#define ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MIN	ID_AA64MMFR0_EL1_TGRAN16_SUPPORTED_MIN
#define ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MAX	ID_AA64MMFR0_EL1_TGRAN16_SUPPORTED_MAX
#define ID_AA64MMFR0_EL1_TGRAN_2_SHIFT		ID_AA64MMFR0_EL1_TGRAN16_2_SHIFT
#elif defined(CONFIG_ARM64_64K_PAGES)
#define ID_AA64MMFR0_EL1_TGRAN_SHIFT		ID_AA64MMFR0_EL1_TGRAN64_SHIFT
#define ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MIN	ID_AA64MMFR0_EL1_TGRAN64_SUPPORTED_MIN
#define ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MAX	ID_AA64MMFR0_EL1_TGRAN64_SUPPORTED_MAX
#define ID_AA64MMFR0_EL1_TGRAN_2_SHIFT		ID_AA64MMFR0_EL1_TGRAN64_2_SHIFT
#endif

#define CPACR_EL1_FPEN_EL1EN	(BIT(20)) /* enable EL1 access */
#define CPACR_EL1_FPEN_EL0EN	(BIT(21)) /* enable EL0 access, if EL1EN set */

#define CPACR_EL1_SMEN_EL1EN	(BIT(24)) /* enable EL1 access */
#define CPACR_EL1_SMEN_EL0EN	(BIT(25)) /* enable EL0 access, if EL1EN set */

#define CPACR_EL1_ZEN_EL1EN	(BIT(16)) /* enable EL1 access */
#define CPACR_EL1_ZEN_EL0EN	(BIT(17)) /* enable EL0 access, if EL1EN set */

/* GCR_EL1 Definitions */
#define SYS_GCR_EL1_RRND	(BIT(16))
#define SYS_GCR_EL1_EXCL_MASK	0xffffUL

#ifdef CONFIG_KASAN_HW_TAGS
/*
 * KASAN always uses a whole byte for its tags. With CONFIG_KASAN_HW_TAGS it
 * only uses tags in the range 0xF0-0xFF, which we map to MTE tags 0x0-0xF.
 */
#define __MTE_TAG_MIN		(KASAN_TAG_MIN & 0xf)
#define __MTE_TAG_MAX		(KASAN_TAG_MAX & 0xf)
#define __MTE_TAG_INCL		GENMASK(__MTE_TAG_MAX, __MTE_TAG_MIN)
#define KERNEL_GCR_EL1_EXCL	(SYS_GCR_EL1_EXCL_MASK & ~__MTE_TAG_INCL)
#else
#define KERNEL_GCR_EL1_EXCL	SYS_GCR_EL1_EXCL_MASK
#endif

#define KERNEL_GCR_EL1		(SYS_GCR_EL1_RRND | KERNEL_GCR_EL1_EXCL)

/* RGSR_EL1 Definitions */
#define SYS_RGSR_EL1_TAG_MASK	0xfUL
#define SYS_RGSR_EL1_SEED_SHIFT	8
#define SYS_RGSR_EL1_SEED_MASK	0xffffUL

/* TFSR{,E0}_EL1 bit definitions */
#define SYS_TFSR_EL1_TF0_SHIFT	0
#define SYS_TFSR_EL1_TF1_SHIFT	1
#define SYS_TFSR_EL1_TF0	(UL(1) << SYS_TFSR_EL1_TF0_SHIFT)
#define SYS_TFSR_EL1_TF1	(UL(1) << SYS_TFSR_EL1_TF1_SHIFT)

/* Safe value for MPIDR_EL1: Bit31:RES1, Bit30:U:0, Bit24:MT:0 */
#define SYS_MPIDR_SAFE_VAL	(BIT(31))

/* GIC Hypervisor interface registers */
/* ICH_LR*_EL2 bit definitions */
#define ICH_LR_VIRTUAL_ID_MASK	((1ULL << 32) - 1)

#define ICH_LR_EOI		(1ULL << 41)
#define ICH_LR_GROUP		(1ULL << 60)
#define ICH_LR_HW		(1ULL << 61)
#define ICH_LR_STATE		(3ULL << 62)
#define ICH_LR_PENDING_BIT	(1ULL << 62)
#define ICH_LR_ACTIVE_BIT	(1ULL << 63)
#define ICH_LR_PHYS_ID_SHIFT	32
#define ICH_LR_PHYS_ID_MASK	(0x3ffULL << ICH_LR_PHYS_ID_SHIFT)
#define ICH_LR_PRIORITY_SHIFT	48
#define ICH_LR_PRIORITY_MASK	(0xffULL << ICH_LR_PRIORITY_SHIFT)

/* ICH_VMCR_EL2 bit definitions */
#define ICH_VMCR_ACK_CTL_SHIFT	2
#define ICH_VMCR_ACK_CTL_MASK	(1 << ICH_VMCR_ACK_CTL_SHIFT)
#define ICH_VMCR_FIQ_EN_SHIFT	3
#define ICH_VMCR_FIQ_EN_MASK	(1 << ICH_VMCR_FIQ_EN_SHIFT)
#define ICH_VMCR_CBPR_SHIFT	4
#define ICH_VMCR_CBPR_MASK	(1 << ICH_VMCR_CBPR_SHIFT)
#define ICH_VMCR_EOIM_SHIFT	9
#define ICH_VMCR_EOIM_MASK	(1 << ICH_VMCR_EOIM_SHIFT)
#define ICH_VMCR_BPR1_SHIFT	18
#define ICH_VMCR_BPR1_MASK	(7 << ICH_VMCR_BPR1_SHIFT)
#define ICH_VMCR_BPR0_SHIFT	21
#define ICH_VMCR_BPR0_MASK	(7 << ICH_VMCR_BPR0_SHIFT)
#define ICH_VMCR_PMR_SHIFT	24
#define ICH_VMCR_PMR_MASK	(0xffUL << ICH_VMCR_PMR_SHIFT)
#define ICH_VMCR_ENG0_SHIFT	0
#define ICH_VMCR_ENG0_MASK	(1 << ICH_VMCR_ENG0_SHIFT)
#define ICH_VMCR_ENG1_SHIFT	1
#define ICH_VMCR_ENG1_MASK	(1 << ICH_VMCR_ENG1_SHIFT)

/*
 * Permission Indirection Extension (PIE) permission encodings.
 * Encodings with the _O suffix, have overlays applied (Permission Overlay Extension).
 */
#define PIE_NONE_O	UL(0x0)
#define PIE_R_O		UL(0x1)
#define PIE_X_O		UL(0x2)
#define PIE_RX_O	UL(0x3)
#define PIE_RW_O	UL(0x5)
#define PIE_RWnX_O	UL(0x6)
#define PIE_RWX_O	UL(0x7)
#define PIE_R		UL(0x8)
#define PIE_GCS		UL(0x9)
#define PIE_RX		UL(0xa)
#define PIE_RW		UL(0xc)
#define PIE_RWX		UL(0xe)
#define PIE_MASK	UL(0xf)

#define PIRx_ELx_BITS_PER_IDX		4
#define PIRx_ELx_PERM_SHIFT(idx)	((idx) * PIRx_ELx_BITS_PER_IDX)
#define PIRx_ELx_PERM_PREP(idx, perm)	(((perm) & PIE_MASK) << PIRx_ELx_PERM_SHIFT(idx))

/*
 * Permission Overlay Extension (POE) permission encodings.
 */
#define POE_NONE	UL(0x0)
#define POE_R		UL(0x1)
#define POE_X		UL(0x2)
#define POE_RX		UL(0x3)
#define POE_W		UL(0x4)
#define POE_RW		UL(0x5)
#define POE_WX		UL(0x6)
#define POE_RWX		UL(0x7)
#define POE_MASK	UL(0xf)

#define POR_ELx_BITS_PER_IDX		4
#define POR_ELx_PERM_SHIFT(idx)		((idx) * POR_ELx_BITS_PER_IDX)
#define POR_ELx_PERM_GET(idx, reg)	(((reg) >> POR_ELx_PERM_SHIFT(idx)) & POE_MASK)
#define POR_ELx_PERM_PREP(idx, perm)	(((perm) & POE_MASK) << POR_ELx_PERM_SHIFT(idx))

/*
 * Definitions for Guarded Control Stack
 */

#define GCS_CAP_ADDR_MASK		GENMASK(63, 12)
#define GCS_CAP_ADDR_SHIFT		12
#define GCS_CAP_ADDR_WIDTH		52
#define GCS_CAP_ADDR(x)			FIELD_GET(GCS_CAP_ADDR_MASK, x)

#define GCS_CAP_TOKEN_MASK		GENMASK(11, 0)
#define GCS_CAP_TOKEN_SHIFT		0
#define GCS_CAP_TOKEN_WIDTH		12
#define GCS_CAP_TOKEN(x)		FIELD_GET(GCS_CAP_TOKEN_MASK, x)

#define GCS_CAP_VALID_TOKEN		0x1
#define GCS_CAP_IN_PROGRESS_TOKEN	0x5

#define GCS_CAP(x)	((((unsigned long)x) & GCS_CAP_ADDR_MASK) | \
					       GCS_CAP_VALID_TOKEN)

#define ARM64_FEATURE_FIELD_BITS	4

/* Defined for compatibility only, do not add new users. */
#define ARM64_FEATURE_MASK(x)	(x##_MASK)

#ifdef __ASSEMBLY__

	.macro	mrs_s, rt, sreg
	 __emit_inst(0xd5200000|(\sreg)|(.L__gpr_num_\rt))
	.endm

	.macro	msr_s, sreg, rt
	__emit_inst(0xd5000000|(\sreg)|(.L__gpr_num_\rt))
	.endm

#else

#include <linux/bitfield.h>
#include <linux/build_bug.h>
#include <linux/types.h>
#include <asm/alternative.h>

#define DEFINE_MRS_S						\
	__DEFINE_ASM_GPR_NUMS					\
"	.macro	mrs_s, rt, sreg\n"				\
	__emit_inst(0xd5200000|(\\sreg)|(.L__gpr_num_\\rt))	\
"	.endm\n"

#define DEFINE_MSR_S						\
	__DEFINE_ASM_GPR_NUMS					\
"	.macro	msr_s, sreg, rt\n"				\
	__emit_inst(0xd5000000|(\\sreg)|(.L__gpr_num_\\rt))	\
"	.endm\n"

#define UNDEFINE_MRS_S						\
"	.purgem	mrs_s\n"

#define UNDEFINE_MSR_S						\
"	.purgem	msr_s\n"

#define __mrs_s(v, r)						\
	DEFINE_MRS_S						\
"	mrs_s " v ", " __stringify(r) "\n"			\
	UNDEFINE_MRS_S

#define __msr_s(r, v)						\
	DEFINE_MSR_S						\
"	msr_s " __stringify(r) ", " v "\n"			\
	UNDEFINE_MSR_S

/*
 * Unlike read_cpuid, calls to read_sysreg are never expected to be
 * optimized away or replaced with synthetic values.
 */
#define read_sysreg(r) ({					\
	u64 __val;						\
	asm volatile("mrs %0, " __stringify(r) : "=r" (__val));	\
	__val;							\
})

/*
 * The "Z" constraint normally means a zero immediate, but when combined with
 * the "%x0" template means XZR.
 */
#define write_sysreg(v, r) do {					\
	u64 __val = (u64)(v);					\
	asm volatile("msr " __stringify(r) ", %x0"		\
		     : : "rZ" (__val));				\
} while (0)

/*
 * For registers without architectural names, or simply unsupported by
 * GAS.
 *
 * __check_r forces warnings to be generated by the compiler when
 * evaluating r which wouldn't normally happen due to being passed to
 * the assembler via __stringify(r).
 */
#define read_sysreg_s(r) ({						\
	u64 __val;							\
	u32 __maybe_unused __check_r = (u32)(r);			\
	asm volatile(__mrs_s("%0", r) : "=r" (__val));			\
	__val;								\
})

#define write_sysreg_s(v, r) do {					\
	u64 __val = (u64)(v);						\
	u32 __maybe_unused __check_r = (u32)(r);			\
	asm volatile(__msr_s(r, "%x0") : : "rZ" (__val));		\
} while (0)

/*
 * Modify bits in a sysreg. Bits in the clear mask are zeroed, then bits in the
 * set mask are set. Other bits are left as-is.
 */
#define sysreg_clear_set(sysreg, clear, set) do {			\
	u64 __scs_val = read_sysreg(sysreg);				\
	u64 __scs_new = (__scs_val & ~(u64)(clear)) | (set);		\
	if (__scs_new != __scs_val)					\
		write_sysreg(__scs_new, sysreg);			\
} while (0)

#define sysreg_clear_set_s(sysreg, clear, set) do {			\
	u64 __scs_val = read_sysreg_s(sysreg);				\
	u64 __scs_new = (__scs_val & ~(u64)(clear)) | (set);		\
	if (__scs_new != __scs_val)					\
		write_sysreg_s(__scs_new, sysreg);			\
} while (0)

#define read_sysreg_par() ({						\
	u64 par;							\
	asm(ALTERNATIVE("nop", "dmb sy", ARM64_WORKAROUND_1508412));	\
	par = read_sysreg(par_el1);					\
	asm(ALTERNATIVE("nop", "dmb sy", ARM64_WORKAROUND_1508412));	\
	par;								\
})

#define SYS_FIELD_VALUE(reg, field, val)	reg##_##field##_##val

#define SYS_FIELD_GET(reg, field, val)		\
		 FIELD_GET(reg##_##field##_MASK, val)

#define SYS_FIELD_PREP(reg, field, val)		\
		 FIELD_PREP(reg##_##field##_MASK, val)

#define SYS_FIELD_PREP_ENUM(reg, field, val)		\
		 FIELD_PREP(reg##_##field##_MASK,	\
			    SYS_FIELD_VALUE(reg, field, val))

#endif

#endif	/* __ASM_SYSREG_H */
