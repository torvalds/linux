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

#define PSTATE_PAN			pstate_field(0, 4)
#define PSTATE_UAO			pstate_field(0, 3)
#define PSTATE_SSBS			pstate_field(3, 1)
#define PSTATE_TCO			pstate_field(3, 4)

#define SET_PSTATE_PAN(x)		__emit_inst(0xd500401f | PSTATE_PAN | ((!!x) << PSTATE_Imm_shift))
#define SET_PSTATE_UAO(x)		__emit_inst(0xd500401f | PSTATE_UAO | ((!!x) << PSTATE_Imm_shift))
#define SET_PSTATE_SSBS(x)		__emit_inst(0xd500401f | PSTATE_SSBS | ((!!x) << PSTATE_Imm_shift))
#define SET_PSTATE_TCO(x)		__emit_inst(0xd500401f | PSTATE_TCO | ((!!x) << PSTATE_Imm_shift))

#define set_pstate_pan(x)		asm volatile(SET_PSTATE_PAN(x))
#define set_pstate_uao(x)		asm volatile(SET_PSTATE_UAO(x))
#define set_pstate_ssbs(x)		asm volatile(SET_PSTATE_SSBS(x))

#define __SYS_BARRIER_INSN(CRm, op2, Rt) \
	__emit_inst(0xd5000000 | sys_insn(0, 3, 3, (CRm), (op2)) | ((Rt) & 0x1f))

#define SB_BARRIER_INSN			__SYS_BARRIER_INSN(0, 7, 31)

#define SYS_DC_ISW			sys_insn(1, 0, 7, 6, 2)
#define SYS_DC_CSW			sys_insn(1, 0, 7, 10, 2)
#define SYS_DC_CISW			sys_insn(1, 0, 7, 14, 2)

/*
 * System registers, organised loosely by encoding but grouped together
 * where the architected name contains an index. e.g. ID_MMFR<n>_EL1.
 */
#define SYS_OSDTRRX_EL1			sys_reg(2, 0, 0, 0, 2)
#define SYS_MDCCINT_EL1			sys_reg(2, 0, 0, 2, 0)
#define SYS_MDSCR_EL1			sys_reg(2, 0, 0, 2, 2)
#define SYS_OSDTRTX_EL1			sys_reg(2, 0, 0, 3, 2)
#define SYS_OSECCR_EL1			sys_reg(2, 0, 0, 6, 2)
#define SYS_DBGBVRn_EL1(n)		sys_reg(2, 0, 0, n, 4)
#define SYS_DBGBCRn_EL1(n)		sys_reg(2, 0, 0, n, 5)
#define SYS_DBGWVRn_EL1(n)		sys_reg(2, 0, 0, n, 6)
#define SYS_DBGWCRn_EL1(n)		sys_reg(2, 0, 0, n, 7)
#define SYS_MDRAR_EL1			sys_reg(2, 0, 1, 0, 0)

#define SYS_OSLAR_EL1			sys_reg(2, 0, 1, 0, 4)
#define SYS_OSLAR_OSLK			BIT(0)

#define SYS_OSLSR_EL1			sys_reg(2, 0, 1, 1, 4)
#define SYS_OSLSR_OSLM_MASK		(BIT(3) | BIT(0))
#define SYS_OSLSR_OSLM_NI		0
#define SYS_OSLSR_OSLM_IMPLEMENTED	BIT(3)
#define SYS_OSLSR_OSLK			BIT(1)

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

#define SYS_MIDR_EL1			sys_reg(3, 0, 0, 0, 0)
#define SYS_MPIDR_EL1			sys_reg(3, 0, 0, 0, 5)
#define SYS_REVIDR_EL1			sys_reg(3, 0, 0, 0, 6)

#define SYS_ID_PFR0_EL1			sys_reg(3, 0, 0, 1, 0)
#define SYS_ID_PFR1_EL1			sys_reg(3, 0, 0, 1, 1)
#define SYS_ID_PFR2_EL1			sys_reg(3, 0, 0, 3, 4)
#define SYS_ID_DFR0_EL1			sys_reg(3, 0, 0, 1, 2)
#define SYS_ID_DFR1_EL1			sys_reg(3, 0, 0, 3, 5)
#define SYS_ID_AFR0_EL1			sys_reg(3, 0, 0, 1, 3)
#define SYS_ID_MMFR0_EL1		sys_reg(3, 0, 0, 1, 4)
#define SYS_ID_MMFR1_EL1		sys_reg(3, 0, 0, 1, 5)
#define SYS_ID_MMFR2_EL1		sys_reg(3, 0, 0, 1, 6)
#define SYS_ID_MMFR3_EL1		sys_reg(3, 0, 0, 1, 7)
#define SYS_ID_MMFR4_EL1		sys_reg(3, 0, 0, 2, 6)
#define SYS_ID_MMFR5_EL1		sys_reg(3, 0, 0, 3, 6)

#define SYS_ID_ISAR0_EL1		sys_reg(3, 0, 0, 2, 0)
#define SYS_ID_ISAR1_EL1		sys_reg(3, 0, 0, 2, 1)
#define SYS_ID_ISAR2_EL1		sys_reg(3, 0, 0, 2, 2)
#define SYS_ID_ISAR3_EL1		sys_reg(3, 0, 0, 2, 3)
#define SYS_ID_ISAR4_EL1		sys_reg(3, 0, 0, 2, 4)
#define SYS_ID_ISAR5_EL1		sys_reg(3, 0, 0, 2, 5)
#define SYS_ID_ISAR6_EL1		sys_reg(3, 0, 0, 2, 7)

#define SYS_MVFR0_EL1			sys_reg(3, 0, 0, 3, 0)
#define SYS_MVFR1_EL1			sys_reg(3, 0, 0, 3, 1)
#define SYS_MVFR2_EL1			sys_reg(3, 0, 0, 3, 2)

#define SYS_ID_AA64PFR0_EL1		sys_reg(3, 0, 0, 4, 0)
#define SYS_ID_AA64PFR1_EL1		sys_reg(3, 0, 0, 4, 1)
#define SYS_ID_AA64ZFR0_EL1		sys_reg(3, 0, 0, 4, 4)

#define SYS_ID_AA64DFR0_EL1		sys_reg(3, 0, 0, 5, 0)
#define SYS_ID_AA64DFR1_EL1		sys_reg(3, 0, 0, 5, 1)

#define SYS_ID_AA64AFR0_EL1		sys_reg(3, 0, 0, 5, 4)
#define SYS_ID_AA64AFR1_EL1		sys_reg(3, 0, 0, 5, 5)

#define SYS_ID_AA64ISAR0_EL1		sys_reg(3, 0, 0, 6, 0)
#define SYS_ID_AA64ISAR1_EL1		sys_reg(3, 0, 0, 6, 1)
#define SYS_ID_AA64ISAR2_EL1		sys_reg(3, 0, 0, 6, 2)

#define SYS_ID_AA64MMFR0_EL1		sys_reg(3, 0, 0, 7, 0)
#define SYS_ID_AA64MMFR1_EL1		sys_reg(3, 0, 0, 7, 1)
#define SYS_ID_AA64MMFR2_EL1		sys_reg(3, 0, 0, 7, 2)

#define SYS_SCTLR_EL1			sys_reg(3, 0, 1, 0, 0)
#define SYS_ACTLR_EL1			sys_reg(3, 0, 1, 0, 1)
#define SYS_CPACR_EL1			sys_reg(3, 0, 1, 0, 2)
#define SYS_RGSR_EL1			sys_reg(3, 0, 1, 0, 5)
#define SYS_GCR_EL1			sys_reg(3, 0, 1, 0, 6)

#define SYS_ZCR_EL1			sys_reg(3, 0, 1, 2, 0)
#define SYS_TRFCR_EL1			sys_reg(3, 0, 1, 2, 1)

#define SYS_TTBR0_EL1			sys_reg(3, 0, 2, 0, 0)
#define SYS_TTBR1_EL1			sys_reg(3, 0, 2, 0, 1)
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
#define SYS_ERXMISC0_EL1		sys_reg(3, 0, 5, 5, 0)
#define SYS_ERXMISC1_EL1		sys_reg(3, 0, 5, 5, 1)
#define SYS_TFSR_EL1			sys_reg(3, 0, 5, 6, 0)
#define SYS_TFSRE0_EL1			sys_reg(3, 0, 5, 6, 1)

#define SYS_FAR_EL1			sys_reg(3, 0, 6, 0, 0)
#define SYS_PAR_EL1			sys_reg(3, 0, 7, 4, 0)

#define SYS_PAR_EL1_F			BIT(0)
#define SYS_PAR_EL1_FST			GENMASK(6, 1)

/*** Statistical Profiling Extension ***/
/* ID registers */
#define SYS_PMSIDR_EL1			sys_reg(3, 0, 9, 9, 7)
#define SYS_PMSIDR_EL1_FE_SHIFT		0
#define SYS_PMSIDR_EL1_FT_SHIFT		1
#define SYS_PMSIDR_EL1_FL_SHIFT		2
#define SYS_PMSIDR_EL1_ARCHINST_SHIFT	3
#define SYS_PMSIDR_EL1_LDS_SHIFT	4
#define SYS_PMSIDR_EL1_ERND_SHIFT	5
#define SYS_PMSIDR_EL1_INTERVAL_SHIFT	8
#define SYS_PMSIDR_EL1_INTERVAL_MASK	0xfUL
#define SYS_PMSIDR_EL1_MAXSIZE_SHIFT	12
#define SYS_PMSIDR_EL1_MAXSIZE_MASK	0xfUL
#define SYS_PMSIDR_EL1_COUNTSIZE_SHIFT	16
#define SYS_PMSIDR_EL1_COUNTSIZE_MASK	0xfUL

#define SYS_PMBIDR_EL1			sys_reg(3, 0, 9, 10, 7)
#define SYS_PMBIDR_EL1_ALIGN_SHIFT	0
#define SYS_PMBIDR_EL1_ALIGN_MASK	0xfU
#define SYS_PMBIDR_EL1_P_SHIFT		4
#define SYS_PMBIDR_EL1_F_SHIFT		5

/* Sampling controls */
#define SYS_PMSCR_EL1			sys_reg(3, 0, 9, 9, 0)
#define SYS_PMSCR_EL1_E0SPE_SHIFT	0
#define SYS_PMSCR_EL1_E1SPE_SHIFT	1
#define SYS_PMSCR_EL1_CX_SHIFT		3
#define SYS_PMSCR_EL1_PA_SHIFT		4
#define SYS_PMSCR_EL1_TS_SHIFT		5
#define SYS_PMSCR_EL1_PCT_SHIFT		6

#define SYS_PMSCR_EL2			sys_reg(3, 4, 9, 9, 0)
#define SYS_PMSCR_EL2_E0HSPE_SHIFT	0
#define SYS_PMSCR_EL2_E2SPE_SHIFT	1
#define SYS_PMSCR_EL2_CX_SHIFT		3
#define SYS_PMSCR_EL2_PA_SHIFT		4
#define SYS_PMSCR_EL2_TS_SHIFT		5
#define SYS_PMSCR_EL2_PCT_SHIFT		6

#define SYS_PMSICR_EL1			sys_reg(3, 0, 9, 9, 2)

#define SYS_PMSIRR_EL1			sys_reg(3, 0, 9, 9, 3)
#define SYS_PMSIRR_EL1_RND_SHIFT	0
#define SYS_PMSIRR_EL1_INTERVAL_SHIFT	8
#define SYS_PMSIRR_EL1_INTERVAL_MASK	0xffffffUL

/* Filtering controls */
#define SYS_PMSNEVFR_EL1		sys_reg(3, 0, 9, 9, 1)

#define SYS_PMSFCR_EL1			sys_reg(3, 0, 9, 9, 4)
#define SYS_PMSFCR_EL1_FE_SHIFT		0
#define SYS_PMSFCR_EL1_FT_SHIFT		1
#define SYS_PMSFCR_EL1_FL_SHIFT		2
#define SYS_PMSFCR_EL1_B_SHIFT		16
#define SYS_PMSFCR_EL1_LD_SHIFT		17
#define SYS_PMSFCR_EL1_ST_SHIFT		18

#define SYS_PMSEVFR_EL1			sys_reg(3, 0, 9, 9, 5)
#define SYS_PMSEVFR_EL1_RES0_8_2	\
	(GENMASK_ULL(47, 32) | GENMASK_ULL(23, 16) | GENMASK_ULL(11, 8) |\
	 BIT_ULL(6) | BIT_ULL(4) | BIT_ULL(2) | BIT_ULL(0))
#define SYS_PMSEVFR_EL1_RES0_8_3	\
	(SYS_PMSEVFR_EL1_RES0_8_2 & ~(BIT_ULL(18) | BIT_ULL(17) | BIT_ULL(11)))

#define SYS_PMSLATFR_EL1		sys_reg(3, 0, 9, 9, 6)
#define SYS_PMSLATFR_EL1_MINLAT_SHIFT	0

/* Buffer controls */
#define SYS_PMBLIMITR_EL1		sys_reg(3, 0, 9, 10, 0)
#define SYS_PMBLIMITR_EL1_E_SHIFT	0
#define SYS_PMBLIMITR_EL1_FM_SHIFT	1
#define SYS_PMBLIMITR_EL1_FM_MASK	0x3UL
#define SYS_PMBLIMITR_EL1_FM_STOP_IRQ	(0 << SYS_PMBLIMITR_EL1_FM_SHIFT)

#define SYS_PMBPTR_EL1			sys_reg(3, 0, 9, 10, 1)

/* Buffer error reporting */
#define SYS_PMBSR_EL1			sys_reg(3, 0, 9, 10, 3)
#define SYS_PMBSR_EL1_COLL_SHIFT	16
#define SYS_PMBSR_EL1_S_SHIFT		17
#define SYS_PMBSR_EL1_EA_SHIFT		18
#define SYS_PMBSR_EL1_DL_SHIFT		19
#define SYS_PMBSR_EL1_EC_SHIFT		26
#define SYS_PMBSR_EL1_EC_MASK		0x3fUL

#define SYS_PMBSR_EL1_EC_BUF		(0x0UL << SYS_PMBSR_EL1_EC_SHIFT)
#define SYS_PMBSR_EL1_EC_FAULT_S1	(0x24UL << SYS_PMBSR_EL1_EC_SHIFT)
#define SYS_PMBSR_EL1_EC_FAULT_S2	(0x25UL << SYS_PMBSR_EL1_EC_SHIFT)

#define SYS_PMBSR_EL1_FAULT_FSC_SHIFT	0
#define SYS_PMBSR_EL1_FAULT_FSC_MASK	0x3fUL

#define SYS_PMBSR_EL1_BUF_BSC_SHIFT	0
#define SYS_PMBSR_EL1_BUF_BSC_MASK	0x3fUL

#define SYS_PMBSR_EL1_BUF_BSC_FULL	(0x1UL << SYS_PMBSR_EL1_BUF_BSC_SHIFT)

/*** End of Statistical Profiling Extension ***/

/*
 * TRBE Registers
 */
#define SYS_TRBLIMITR_EL1		sys_reg(3, 0, 9, 11, 0)
#define SYS_TRBPTR_EL1			sys_reg(3, 0, 9, 11, 1)
#define SYS_TRBBASER_EL1		sys_reg(3, 0, 9, 11, 2)
#define SYS_TRBSR_EL1			sys_reg(3, 0, 9, 11, 3)
#define SYS_TRBMAR_EL1			sys_reg(3, 0, 9, 11, 4)
#define SYS_TRBTRG_EL1			sys_reg(3, 0, 9, 11, 6)
#define SYS_TRBIDR_EL1			sys_reg(3, 0, 9, 11, 7)

#define TRBLIMITR_LIMIT_MASK		GENMASK_ULL(51, 0)
#define TRBLIMITR_LIMIT_SHIFT		12
#define TRBLIMITR_NVM			BIT(5)
#define TRBLIMITR_TRIG_MODE_MASK	GENMASK(1, 0)
#define TRBLIMITR_TRIG_MODE_SHIFT	3
#define TRBLIMITR_FILL_MODE_MASK	GENMASK(1, 0)
#define TRBLIMITR_FILL_MODE_SHIFT	1
#define TRBLIMITR_ENABLE		BIT(0)
#define TRBPTR_PTR_MASK			GENMASK_ULL(63, 0)
#define TRBPTR_PTR_SHIFT		0
#define TRBBASER_BASE_MASK		GENMASK_ULL(51, 0)
#define TRBBASER_BASE_SHIFT		12
#define TRBSR_EC_MASK			GENMASK(5, 0)
#define TRBSR_EC_SHIFT			26
#define TRBSR_IRQ			BIT(22)
#define TRBSR_TRG			BIT(21)
#define TRBSR_WRAP			BIT(20)
#define TRBSR_ABORT			BIT(18)
#define TRBSR_STOP			BIT(17)
#define TRBSR_MSS_MASK			GENMASK(15, 0)
#define TRBSR_MSS_SHIFT			0
#define TRBSR_BSC_MASK			GENMASK(5, 0)
#define TRBSR_BSC_SHIFT			0
#define TRBSR_FSC_MASK			GENMASK(5, 0)
#define TRBSR_FSC_SHIFT			0
#define TRBMAR_SHARE_MASK		GENMASK(1, 0)
#define TRBMAR_SHARE_SHIFT		8
#define TRBMAR_OUTER_MASK		GENMASK(3, 0)
#define TRBMAR_OUTER_SHIFT		4
#define TRBMAR_INNER_MASK		GENMASK(3, 0)
#define TRBMAR_INNER_SHIFT		0
#define TRBTRG_TRG_MASK			GENMASK(31, 0)
#define TRBTRG_TRG_SHIFT		0
#define TRBIDR_FLAG			BIT(5)
#define TRBIDR_PROG			BIT(4)
#define TRBIDR_ALIGN_MASK		GENMASK(3, 0)
#define TRBIDR_ALIGN_SHIFT		0

#define SYS_PMINTENSET_EL1		sys_reg(3, 0, 9, 14, 1)
#define SYS_PMINTENCLR_EL1		sys_reg(3, 0, 9, 14, 2)

#define SYS_PMMIR_EL1			sys_reg(3, 0, 9, 14, 6)

#define SYS_MAIR_EL1			sys_reg(3, 0, 10, 2, 0)
#define SYS_AMAIR_EL1			sys_reg(3, 0, 10, 3, 0)

#define SYS_LORSA_EL1			sys_reg(3, 0, 10, 4, 0)
#define SYS_LOREA_EL1			sys_reg(3, 0, 10, 4, 1)
#define SYS_LORN_EL1			sys_reg(3, 0, 10, 4, 2)
#define SYS_LORC_EL1			sys_reg(3, 0, 10, 4, 3)
#define SYS_LORID_EL1			sys_reg(3, 0, 10, 4, 7)

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

#define SYS_CONTEXTIDR_EL1		sys_reg(3, 0, 13, 0, 1)
#define SYS_TPIDR_EL1			sys_reg(3, 0, 13, 0, 4)

#define SYS_SCXTNUM_EL1			sys_reg(3, 0, 13, 0, 7)

#define SYS_CNTKCTL_EL1			sys_reg(3, 0, 14, 1, 0)

#define SYS_CCSIDR_EL1			sys_reg(3, 1, 0, 0, 0)
#define SYS_CLIDR_EL1			sys_reg(3, 1, 0, 0, 1)
#define SYS_GMID_EL1			sys_reg(3, 1, 0, 0, 4)
#define SYS_AIDR_EL1			sys_reg(3, 1, 0, 0, 7)

#define SYS_CSSELR_EL1			sys_reg(3, 2, 0, 0, 0)

#define SYS_CTR_EL0			sys_reg(3, 3, 0, 0, 1)
#define SYS_DCZID_EL0			sys_reg(3, 3, 0, 0, 7)

#define SYS_RNDR_EL0			sys_reg(3, 3, 2, 4, 0)
#define SYS_RNDRRS_EL0			sys_reg(3, 3, 2, 4, 1)

#define SYS_PMCR_EL0			sys_reg(3, 3, 9, 12, 0)
#define SYS_PMCNTENSET_EL0		sys_reg(3, 3, 9, 12, 1)
#define SYS_PMCNTENCLR_EL0		sys_reg(3, 3, 9, 12, 2)
#define SYS_PMOVSCLR_EL0		sys_reg(3, 3, 9, 12, 3)
#define SYS_PMSWINC_EL0			sys_reg(3, 3, 9, 12, 4)
#define SYS_PMSELR_EL0			sys_reg(3, 3, 9, 12, 5)
#define SYS_PMCEID0_EL0			sys_reg(3, 3, 9, 12, 6)
#define SYS_PMCEID1_EL0			sys_reg(3, 3, 9, 12, 7)
#define SYS_PMCCNTR_EL0			sys_reg(3, 3, 9, 13, 0)
#define SYS_PMXEVTYPER_EL0		sys_reg(3, 3, 9, 13, 1)
#define SYS_PMXEVCNTR_EL0		sys_reg(3, 3, 9, 13, 2)
#define SYS_PMUSERENR_EL0		sys_reg(3, 3, 9, 14, 0)
#define SYS_PMOVSSET_EL0		sys_reg(3, 3, 9, 14, 3)

#define SYS_TPIDR_EL0			sys_reg(3, 3, 13, 0, 2)
#define SYS_TPIDRRO_EL0			sys_reg(3, 3, 13, 0, 3)

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

#define SYS_CNTPCTSS_EL0		sys_reg(3, 3, 14, 0, 5)
#define SYS_CNTVCTSS_EL0		sys_reg(3, 3, 14, 0, 6)

#define SYS_CNTP_TVAL_EL0		sys_reg(3, 3, 14, 2, 0)
#define SYS_CNTP_CTL_EL0		sys_reg(3, 3, 14, 2, 1)
#define SYS_CNTP_CVAL_EL0		sys_reg(3, 3, 14, 2, 2)

#define SYS_CNTV_CTL_EL0		sys_reg(3, 3, 14, 3, 1)
#define SYS_CNTV_CVAL_EL0		sys_reg(3, 3, 14, 3, 2)

#define SYS_AARCH32_CNTP_TVAL		sys_reg(0, 0, 14, 2, 0)
#define SYS_AARCH32_CNTP_CTL		sys_reg(0, 0, 14, 2, 1)
#define SYS_AARCH32_CNTP_CVAL		sys_reg(0, 2, 0, 14, 0)

#define __PMEV_op2(n)			((n) & 0x7)
#define __CNTR_CRm(n)			(0x8 | (((n) >> 3) & 0x3))
#define SYS_PMEVCNTRn_EL0(n)		sys_reg(3, 3, 14, __CNTR_CRm(n), __PMEV_op2(n))
#define __TYPER_CRm(n)			(0xc | (((n) >> 3) & 0x3))
#define SYS_PMEVTYPERn_EL0(n)		sys_reg(3, 3, 14, __TYPER_CRm(n), __PMEV_op2(n))

#define SYS_PMCCFILTR_EL0		sys_reg(3, 3, 14, 15, 7)

#define SYS_SCTLR_EL2			sys_reg(3, 4, 1, 0, 0)
#define SYS_HFGRTR_EL2			sys_reg(3, 4, 1, 1, 4)
#define SYS_HFGWTR_EL2			sys_reg(3, 4, 1, 1, 5)
#define SYS_HFGITR_EL2			sys_reg(3, 4, 1, 1, 6)
#define SYS_ZCR_EL2			sys_reg(3, 4, 1, 2, 0)
#define SYS_TRFCR_EL2			sys_reg(3, 4, 1, 2, 1)
#define SYS_DACR32_EL2			sys_reg(3, 4, 3, 0, 0)
#define SYS_HDFGRTR_EL2			sys_reg(3, 4, 3, 1, 4)
#define SYS_HDFGWTR_EL2			sys_reg(3, 4, 3, 1, 5)
#define SYS_HAFGRTR_EL2			sys_reg(3, 4, 3, 1, 6)
#define SYS_SPSR_EL2			sys_reg(3, 4, 4, 0, 0)
#define SYS_ELR_EL2			sys_reg(3, 4, 4, 0, 1)
#define SYS_IFSR32_EL2			sys_reg(3, 4, 5, 0, 1)
#define SYS_ESR_EL2			sys_reg(3, 4, 5, 2, 0)
#define SYS_VSESR_EL2			sys_reg(3, 4, 5, 2, 3)
#define SYS_FPEXC32_EL2			sys_reg(3, 4, 5, 3, 0)
#define SYS_TFSR_EL2			sys_reg(3, 4, 5, 6, 0)
#define SYS_FAR_EL2			sys_reg(3, 4, 6, 0, 0)

#define SYS_VDISR_EL2			sys_reg(3, 4, 12, 1,  1)
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
#define SYS_ICH_HCR_EL2			sys_reg(3, 4, 12, 11, 0)
#define SYS_ICH_VTR_EL2			sys_reg(3, 4, 12, 11, 1)
#define SYS_ICH_MISR_EL2		sys_reg(3, 4, 12, 11, 2)
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

/* VHE encodings for architectural EL0/1 system registers */
#define SYS_SCTLR_EL12			sys_reg(3, 5, 1, 0, 0)
#define SYS_CPACR_EL12			sys_reg(3, 5, 1, 0, 2)
#define SYS_ZCR_EL12			sys_reg(3, 5, 1, 2, 0)
#define SYS_TTBR0_EL12			sys_reg(3, 5, 2, 0, 0)
#define SYS_TTBR1_EL12			sys_reg(3, 5, 2, 0, 1)
#define SYS_TCR_EL12			sys_reg(3, 5, 2, 0, 2)
#define SYS_SPSR_EL12			sys_reg(3, 5, 4, 0, 0)
#define SYS_ELR_EL12			sys_reg(3, 5, 4, 0, 1)
#define SYS_AFSR0_EL12			sys_reg(3, 5, 5, 1, 0)
#define SYS_AFSR1_EL12			sys_reg(3, 5, 5, 1, 1)
#define SYS_ESR_EL12			sys_reg(3, 5, 5, 2, 0)
#define SYS_TFSR_EL12			sys_reg(3, 5, 5, 6, 0)
#define SYS_FAR_EL12			sys_reg(3, 5, 6, 0, 0)
#define SYS_MAIR_EL12			sys_reg(3, 5, 10, 2, 0)
#define SYS_AMAIR_EL12			sys_reg(3, 5, 10, 3, 0)
#define SYS_VBAR_EL12			sys_reg(3, 5, 12, 0, 0)
#define SYS_CONTEXTIDR_EL12		sys_reg(3, 5, 13, 0, 1)
#define SYS_CNTKCTL_EL12		sys_reg(3, 5, 14, 1, 0)
#define SYS_CNTP_TVAL_EL02		sys_reg(3, 5, 14, 2, 0)
#define SYS_CNTP_CTL_EL02		sys_reg(3, 5, 14, 2, 1)
#define SYS_CNTP_CVAL_EL02		sys_reg(3, 5, 14, 2, 2)
#define SYS_CNTV_TVAL_EL02		sys_reg(3, 5, 14, 3, 0)
#define SYS_CNTV_CTL_EL02		sys_reg(3, 5, 14, 3, 1)
#define SYS_CNTV_CVAL_EL02		sys_reg(3, 5, 14, 3, 2)

/* Common SCTLR_ELx flags. */
#define SCTLR_ELx_DSSBS	(BIT(44))
#define SCTLR_ELx_ATA	(BIT(43))

#define SCTLR_ELx_TCF_SHIFT	40
#define SCTLR_ELx_TCF_NONE	(UL(0x0) << SCTLR_ELx_TCF_SHIFT)
#define SCTLR_ELx_TCF_SYNC	(UL(0x1) << SCTLR_ELx_TCF_SHIFT)
#define SCTLR_ELx_TCF_ASYNC	(UL(0x2) << SCTLR_ELx_TCF_SHIFT)
#define SCTLR_ELx_TCF_ASYMM	(UL(0x3) << SCTLR_ELx_TCF_SHIFT)
#define SCTLR_ELx_TCF_MASK	(UL(0x3) << SCTLR_ELx_TCF_SHIFT)

#define SCTLR_ELx_ENIA_SHIFT	31

#define SCTLR_ELx_ITFSB	(BIT(37))
#define SCTLR_ELx_ENIA	(BIT(SCTLR_ELx_ENIA_SHIFT))
#define SCTLR_ELx_ENIB	(BIT(30))
#define SCTLR_ELx_ENDA	(BIT(27))
#define SCTLR_ELx_EE    (BIT(25))
#define SCTLR_ELx_IESB	(BIT(21))
#define SCTLR_ELx_WXN	(BIT(19))
#define SCTLR_ELx_ENDB	(BIT(13))
#define SCTLR_ELx_I	(BIT(12))
#define SCTLR_ELx_SA	(BIT(3))
#define SCTLR_ELx_C	(BIT(2))
#define SCTLR_ELx_A	(BIT(1))
#define SCTLR_ELx_M	(BIT(0))

/* SCTLR_EL2 specific flags. */
#define SCTLR_EL2_RES1	((BIT(4))  | (BIT(5))  | (BIT(11)) | (BIT(16)) | \
			 (BIT(18)) | (BIT(22)) | (BIT(23)) | (BIT(28)) | \
			 (BIT(29)))

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
#define SCTLR_EL1_EPAN		(BIT(57))
#define SCTLR_EL1_ATA0		(BIT(42))

#define SCTLR_EL1_TCF0_SHIFT	38
#define SCTLR_EL1_TCF0_NONE	(UL(0x0) << SCTLR_EL1_TCF0_SHIFT)
#define SCTLR_EL1_TCF0_SYNC	(UL(0x1) << SCTLR_EL1_TCF0_SHIFT)
#define SCTLR_EL1_TCF0_ASYNC	(UL(0x2) << SCTLR_EL1_TCF0_SHIFT)
#define SCTLR_EL1_TCF0_ASYMM	(UL(0x3) << SCTLR_EL1_TCF0_SHIFT)
#define SCTLR_EL1_TCF0_MASK	(UL(0x3) << SCTLR_EL1_TCF0_SHIFT)

#define SCTLR_EL1_BT1		(BIT(36))
#define SCTLR_EL1_BT0		(BIT(35))
#define SCTLR_EL1_UCI		(BIT(26))
#define SCTLR_EL1_E0E		(BIT(24))
#define SCTLR_EL1_SPAN		(BIT(23))
#define SCTLR_EL1_NTWE		(BIT(18))
#define SCTLR_EL1_NTWI		(BIT(16))
#define SCTLR_EL1_UCT		(BIT(15))
#define SCTLR_EL1_DZE		(BIT(14))
#define SCTLR_EL1_UMA		(BIT(9))
#define SCTLR_EL1_SED		(BIT(8))
#define SCTLR_EL1_ITD		(BIT(7))
#define SCTLR_EL1_CP15BEN	(BIT(5))
#define SCTLR_EL1_SA0		(BIT(4))

#define SCTLR_EL1_RES1	((BIT(11)) | (BIT(20)) | (BIT(22)) | (BIT(28)) | \
			 (BIT(29)))

#ifdef CONFIG_CPU_BIG_ENDIAN
#define ENDIAN_SET_EL1		(SCTLR_EL1_E0E | SCTLR_ELx_EE)
#else
#define ENDIAN_SET_EL1		0
#endif

#define INIT_SCTLR_EL1_MMU_OFF \
	(ENDIAN_SET_EL1 | SCTLR_EL1_RES1)

#define INIT_SCTLR_EL1_MMU_ON \
	(SCTLR_ELx_M    | SCTLR_ELx_C    | SCTLR_ELx_SA   | SCTLR_EL1_SA0   | \
	 SCTLR_EL1_SED  | SCTLR_ELx_I    | SCTLR_EL1_DZE  | SCTLR_EL1_UCT   | \
	 SCTLR_EL1_NTWE | SCTLR_ELx_IESB | SCTLR_EL1_SPAN | SCTLR_ELx_ITFSB | \
	 ENDIAN_SET_EL1 | SCTLR_EL1_UCI  | SCTLR_EL1_EPAN | SCTLR_EL1_RES1)

/* MAIR_ELx memory attributes (used by Linux) */
#define MAIR_ATTR_DEVICE_nGnRnE		UL(0x00)
#define MAIR_ATTR_DEVICE_nGnRE		UL(0x04)
#define MAIR_ATTR_NORMAL_NC		UL(0x44)
#define MAIR_ATTR_NORMAL_TAGGED		UL(0xf0)
#define MAIR_ATTR_NORMAL		UL(0xff)
#define MAIR_ATTR_MASK			UL(0xff)

/* Position the attr at the correct index */
#define MAIR_ATTRIDX(attr, idx)		((attr) << ((idx) * 8))

/* id_aa64isar0 */
#define ID_AA64ISAR0_RNDR_SHIFT		60
#define ID_AA64ISAR0_TLB_SHIFT		56
#define ID_AA64ISAR0_TS_SHIFT		52
#define ID_AA64ISAR0_FHM_SHIFT		48
#define ID_AA64ISAR0_DP_SHIFT		44
#define ID_AA64ISAR0_SM4_SHIFT		40
#define ID_AA64ISAR0_SM3_SHIFT		36
#define ID_AA64ISAR0_SHA3_SHIFT		32
#define ID_AA64ISAR0_RDM_SHIFT		28
#define ID_AA64ISAR0_ATOMICS_SHIFT	20
#define ID_AA64ISAR0_CRC32_SHIFT	16
#define ID_AA64ISAR0_SHA2_SHIFT		12
#define ID_AA64ISAR0_SHA1_SHIFT		8
#define ID_AA64ISAR0_AES_SHIFT		4

#define ID_AA64ISAR0_TLB_RANGE_NI	0x0
#define ID_AA64ISAR0_TLB_RANGE		0x2

/* id_aa64isar1 */
#define ID_AA64ISAR1_I8MM_SHIFT		52
#define ID_AA64ISAR1_DGH_SHIFT		48
#define ID_AA64ISAR1_BF16_SHIFT		44
#define ID_AA64ISAR1_SPECRES_SHIFT	40
#define ID_AA64ISAR1_SB_SHIFT		36
#define ID_AA64ISAR1_FRINTTS_SHIFT	32
#define ID_AA64ISAR1_GPI_SHIFT		28
#define ID_AA64ISAR1_GPA_SHIFT		24
#define ID_AA64ISAR1_LRCPC_SHIFT	20
#define ID_AA64ISAR1_FCMA_SHIFT		16
#define ID_AA64ISAR1_JSCVT_SHIFT	12
#define ID_AA64ISAR1_API_SHIFT		8
#define ID_AA64ISAR1_APA_SHIFT		4
#define ID_AA64ISAR1_DPB_SHIFT		0

#define ID_AA64ISAR1_APA_NI			0x0
#define ID_AA64ISAR1_APA_ARCHITECTED		0x1
#define ID_AA64ISAR1_APA_ARCH_EPAC		0x2
#define ID_AA64ISAR1_APA_ARCH_EPAC2		0x3
#define ID_AA64ISAR1_APA_ARCH_EPAC2_FPAC	0x4
#define ID_AA64ISAR1_APA_ARCH_EPAC2_FPAC_CMB	0x5
#define ID_AA64ISAR1_API_NI			0x0
#define ID_AA64ISAR1_API_IMP_DEF		0x1
#define ID_AA64ISAR1_API_IMP_DEF_EPAC		0x2
#define ID_AA64ISAR1_API_IMP_DEF_EPAC2		0x3
#define ID_AA64ISAR1_API_IMP_DEF_EPAC2_FPAC	0x4
#define ID_AA64ISAR1_API_IMP_DEF_EPAC2_FPAC_CMB	0x5
#define ID_AA64ISAR1_GPA_NI			0x0
#define ID_AA64ISAR1_GPA_ARCHITECTED		0x1
#define ID_AA64ISAR1_GPI_NI			0x0
#define ID_AA64ISAR1_GPI_IMP_DEF		0x1

/* id_aa64isar2 */
#define ID_AA64ISAR2_RPRES_SHIFT	4
#define ID_AA64ISAR2_WFXT_SHIFT		0

#define ID_AA64ISAR2_RPRES_8BIT		0x0
#define ID_AA64ISAR2_RPRES_12BIT	0x1
/*
 * Value 0x1 has been removed from the architecture, and is
 * reserved, but has not yet been removed from the ARM ARM
 * as of ARM DDI 0487G.b.
 */
#define ID_AA64ISAR2_WFXT_NI		0x0
#define ID_AA64ISAR2_WFXT_SUPPORTED	0x2

/* id_aa64pfr0 */
#define ID_AA64PFR0_CSV3_SHIFT		60
#define ID_AA64PFR0_CSV2_SHIFT		56
#define ID_AA64PFR0_DIT_SHIFT		48
#define ID_AA64PFR0_AMU_SHIFT		44
#define ID_AA64PFR0_MPAM_SHIFT		40
#define ID_AA64PFR0_SEL2_SHIFT		36
#define ID_AA64PFR0_SVE_SHIFT		32
#define ID_AA64PFR0_RAS_SHIFT		28
#define ID_AA64PFR0_GIC_SHIFT		24
#define ID_AA64PFR0_ASIMD_SHIFT		20
#define ID_AA64PFR0_FP_SHIFT		16
#define ID_AA64PFR0_EL3_SHIFT		12
#define ID_AA64PFR0_EL2_SHIFT		8
#define ID_AA64PFR0_EL1_SHIFT		4
#define ID_AA64PFR0_EL0_SHIFT		0

#define ID_AA64PFR0_AMU			0x1
#define ID_AA64PFR0_SVE			0x1
#define ID_AA64PFR0_RAS_V1		0x1
#define ID_AA64PFR0_RAS_V1P1		0x2
#define ID_AA64PFR0_FP_NI		0xf
#define ID_AA64PFR0_FP_SUPPORTED	0x0
#define ID_AA64PFR0_ASIMD_NI		0xf
#define ID_AA64PFR0_ASIMD_SUPPORTED	0x0
#define ID_AA64PFR0_ELx_64BIT_ONLY	0x1
#define ID_AA64PFR0_ELx_32BIT_64BIT	0x2

/* id_aa64pfr1 */
#define ID_AA64PFR1_MPAMFRAC_SHIFT	16
#define ID_AA64PFR1_RASFRAC_SHIFT	12
#define ID_AA64PFR1_MTE_SHIFT		8
#define ID_AA64PFR1_SSBS_SHIFT		4
#define ID_AA64PFR1_BT_SHIFT		0

#define ID_AA64PFR1_SSBS_PSTATE_NI	0
#define ID_AA64PFR1_SSBS_PSTATE_ONLY	1
#define ID_AA64PFR1_SSBS_PSTATE_INSNS	2
#define ID_AA64PFR1_BT_BTI		0x1

#define ID_AA64PFR1_MTE_NI		0x0
#define ID_AA64PFR1_MTE_EL0		0x1
#define ID_AA64PFR1_MTE			0x2
#define ID_AA64PFR1_MTE_ASYMM		0x3

/* id_aa64zfr0 */
#define ID_AA64ZFR0_F64MM_SHIFT		56
#define ID_AA64ZFR0_F32MM_SHIFT		52
#define ID_AA64ZFR0_I8MM_SHIFT		44
#define ID_AA64ZFR0_SM4_SHIFT		40
#define ID_AA64ZFR0_SHA3_SHIFT		32
#define ID_AA64ZFR0_BF16_SHIFT		20
#define ID_AA64ZFR0_BITPERM_SHIFT	16
#define ID_AA64ZFR0_AES_SHIFT		4
#define ID_AA64ZFR0_SVEVER_SHIFT	0

#define ID_AA64ZFR0_F64MM		0x1
#define ID_AA64ZFR0_F32MM		0x1
#define ID_AA64ZFR0_I8MM		0x1
#define ID_AA64ZFR0_BF16		0x1
#define ID_AA64ZFR0_SM4			0x1
#define ID_AA64ZFR0_SHA3		0x1
#define ID_AA64ZFR0_BITPERM		0x1
#define ID_AA64ZFR0_AES			0x1
#define ID_AA64ZFR0_AES_PMULL		0x2
#define ID_AA64ZFR0_SVEVER_SVE2		0x1

/* id_aa64mmfr0 */
#define ID_AA64MMFR0_ECV_SHIFT		60
#define ID_AA64MMFR0_FGT_SHIFT		56
#define ID_AA64MMFR0_EXS_SHIFT		44
#define ID_AA64MMFR0_TGRAN4_2_SHIFT	40
#define ID_AA64MMFR0_TGRAN64_2_SHIFT	36
#define ID_AA64MMFR0_TGRAN16_2_SHIFT	32
#define ID_AA64MMFR0_TGRAN4_SHIFT	28
#define ID_AA64MMFR0_TGRAN64_SHIFT	24
#define ID_AA64MMFR0_TGRAN16_SHIFT	20
#define ID_AA64MMFR0_BIGENDEL0_SHIFT	16
#define ID_AA64MMFR0_SNSMEM_SHIFT	12
#define ID_AA64MMFR0_BIGENDEL_SHIFT	8
#define ID_AA64MMFR0_ASID_SHIFT		4
#define ID_AA64MMFR0_PARANGE_SHIFT	0

#define ID_AA64MMFR0_ASID_8		0x0
#define ID_AA64MMFR0_ASID_16		0x2

#define ID_AA64MMFR0_TGRAN4_NI			0xf
#define ID_AA64MMFR0_TGRAN4_SUPPORTED_MIN	0x0
#define ID_AA64MMFR0_TGRAN4_SUPPORTED_MAX	0x7
#define ID_AA64MMFR0_TGRAN64_NI			0xf
#define ID_AA64MMFR0_TGRAN64_SUPPORTED_MIN	0x0
#define ID_AA64MMFR0_TGRAN64_SUPPORTED_MAX	0x7
#define ID_AA64MMFR0_TGRAN16_NI			0x0
#define ID_AA64MMFR0_TGRAN16_SUPPORTED_MIN	0x1
#define ID_AA64MMFR0_TGRAN16_SUPPORTED_MAX	0xf

#define ID_AA64MMFR0_PARANGE_32		0x0
#define ID_AA64MMFR0_PARANGE_36		0x1
#define ID_AA64MMFR0_PARANGE_40		0x2
#define ID_AA64MMFR0_PARANGE_42		0x3
#define ID_AA64MMFR0_PARANGE_44		0x4
#define ID_AA64MMFR0_PARANGE_48		0x5
#define ID_AA64MMFR0_PARANGE_52		0x6

#define ARM64_MIN_PARANGE_BITS		32

#define ID_AA64MMFR0_TGRAN_2_SUPPORTED_DEFAULT	0x0
#define ID_AA64MMFR0_TGRAN_2_SUPPORTED_NONE	0x1
#define ID_AA64MMFR0_TGRAN_2_SUPPORTED_MIN	0x2
#define ID_AA64MMFR0_TGRAN_2_SUPPORTED_MAX	0x7

#ifdef CONFIG_ARM64_PA_BITS_52
#define ID_AA64MMFR0_PARANGE_MAX	ID_AA64MMFR0_PARANGE_52
#else
#define ID_AA64MMFR0_PARANGE_MAX	ID_AA64MMFR0_PARANGE_48
#endif

/* id_aa64mmfr1 */
#define ID_AA64MMFR1_AFP_SHIFT		44
#define ID_AA64MMFR1_ETS_SHIFT		36
#define ID_AA64MMFR1_TWED_SHIFT		32
#define ID_AA64MMFR1_XNX_SHIFT		28
#define ID_AA64MMFR1_SPECSEI_SHIFT	24
#define ID_AA64MMFR1_PAN_SHIFT		20
#define ID_AA64MMFR1_LOR_SHIFT		16
#define ID_AA64MMFR1_HPD_SHIFT		12
#define ID_AA64MMFR1_VHE_SHIFT		8
#define ID_AA64MMFR1_VMIDBITS_SHIFT	4
#define ID_AA64MMFR1_HADBS_SHIFT	0

#define ID_AA64MMFR1_VMIDBITS_8		0
#define ID_AA64MMFR1_VMIDBITS_16	2

/* id_aa64mmfr2 */
#define ID_AA64MMFR2_E0PD_SHIFT		60
#define ID_AA64MMFR2_EVT_SHIFT		56
#define ID_AA64MMFR2_BBM_SHIFT		52
#define ID_AA64MMFR2_TTL_SHIFT		48
#define ID_AA64MMFR2_FWB_SHIFT		40
#define ID_AA64MMFR2_IDS_SHIFT		36
#define ID_AA64MMFR2_AT_SHIFT		32
#define ID_AA64MMFR2_ST_SHIFT		28
#define ID_AA64MMFR2_NV_SHIFT		24
#define ID_AA64MMFR2_CCIDX_SHIFT	20
#define ID_AA64MMFR2_LVA_SHIFT		16
#define ID_AA64MMFR2_IESB_SHIFT		12
#define ID_AA64MMFR2_LSM_SHIFT		8
#define ID_AA64MMFR2_UAO_SHIFT		4
#define ID_AA64MMFR2_CNP_SHIFT		0

/* id_aa64dfr0 */
#define ID_AA64DFR0_MTPMU_SHIFT		48
#define ID_AA64DFR0_TRBE_SHIFT		44
#define ID_AA64DFR0_TRACE_FILT_SHIFT	40
#define ID_AA64DFR0_DOUBLELOCK_SHIFT	36
#define ID_AA64DFR0_PMSVER_SHIFT	32
#define ID_AA64DFR0_CTX_CMPS_SHIFT	28
#define ID_AA64DFR0_WRPS_SHIFT		20
#define ID_AA64DFR0_BRPS_SHIFT		12
#define ID_AA64DFR0_PMUVER_SHIFT	8
#define ID_AA64DFR0_TRACEVER_SHIFT	4
#define ID_AA64DFR0_DEBUGVER_SHIFT	0

#define ID_AA64DFR0_PMUVER_8_0		0x1
#define ID_AA64DFR0_PMUVER_8_1		0x4
#define ID_AA64DFR0_PMUVER_8_4		0x5
#define ID_AA64DFR0_PMUVER_8_5		0x6
#define ID_AA64DFR0_PMUVER_8_7		0x7
#define ID_AA64DFR0_PMUVER_IMP_DEF	0xf

#define ID_AA64DFR0_PMSVER_8_2		0x1
#define ID_AA64DFR0_PMSVER_8_3		0x2

#define ID_DFR0_PERFMON_SHIFT		24

#define ID_DFR0_PERFMON_8_0		0x3
#define ID_DFR0_PERFMON_8_1		0x4
#define ID_DFR0_PERFMON_8_4		0x5
#define ID_DFR0_PERFMON_8_5		0x6

#define ID_ISAR4_SWP_FRAC_SHIFT		28
#define ID_ISAR4_PSR_M_SHIFT		24
#define ID_ISAR4_SYNCH_PRIM_FRAC_SHIFT	20
#define ID_ISAR4_BARRIER_SHIFT		16
#define ID_ISAR4_SMC_SHIFT		12
#define ID_ISAR4_WRITEBACK_SHIFT	8
#define ID_ISAR4_WITHSHIFTS_SHIFT	4
#define ID_ISAR4_UNPRIV_SHIFT		0

#define ID_DFR1_MTPMU_SHIFT		0

#define ID_ISAR0_DIVIDE_SHIFT		24
#define ID_ISAR0_DEBUG_SHIFT		20
#define ID_ISAR0_COPROC_SHIFT		16
#define ID_ISAR0_CMPBRANCH_SHIFT	12
#define ID_ISAR0_BITFIELD_SHIFT		8
#define ID_ISAR0_BITCOUNT_SHIFT		4
#define ID_ISAR0_SWAP_SHIFT		0

#define ID_ISAR5_RDM_SHIFT		24
#define ID_ISAR5_CRC32_SHIFT		16
#define ID_ISAR5_SHA2_SHIFT		12
#define ID_ISAR5_SHA1_SHIFT		8
#define ID_ISAR5_AES_SHIFT		4
#define ID_ISAR5_SEVL_SHIFT		0

#define ID_ISAR6_I8MM_SHIFT		24
#define ID_ISAR6_BF16_SHIFT		20
#define ID_ISAR6_SPECRES_SHIFT		16
#define ID_ISAR6_SB_SHIFT		12
#define ID_ISAR6_FHM_SHIFT		8
#define ID_ISAR6_DP_SHIFT		4
#define ID_ISAR6_JSCVT_SHIFT		0

#define ID_MMFR0_INNERSHR_SHIFT		28
#define ID_MMFR0_FCSE_SHIFT		24
#define ID_MMFR0_AUXREG_SHIFT		20
#define ID_MMFR0_TCM_SHIFT		16
#define ID_MMFR0_SHARELVL_SHIFT		12
#define ID_MMFR0_OUTERSHR_SHIFT		8
#define ID_MMFR0_PMSA_SHIFT		4
#define ID_MMFR0_VMSA_SHIFT		0

#define ID_MMFR4_EVT_SHIFT		28
#define ID_MMFR4_CCIDX_SHIFT		24
#define ID_MMFR4_LSM_SHIFT		20
#define ID_MMFR4_HPDS_SHIFT		16
#define ID_MMFR4_CNP_SHIFT		12
#define ID_MMFR4_XNX_SHIFT		8
#define ID_MMFR4_AC2_SHIFT		4
#define ID_MMFR4_SPECSEI_SHIFT		0

#define ID_MMFR5_ETS_SHIFT		0

#define ID_PFR0_DIT_SHIFT		24
#define ID_PFR0_CSV2_SHIFT		16
#define ID_PFR0_STATE3_SHIFT		12
#define ID_PFR0_STATE2_SHIFT		8
#define ID_PFR0_STATE1_SHIFT		4
#define ID_PFR0_STATE0_SHIFT		0

#define ID_DFR0_PERFMON_SHIFT		24
#define ID_DFR0_MPROFDBG_SHIFT		20
#define ID_DFR0_MMAPTRC_SHIFT		16
#define ID_DFR0_COPTRC_SHIFT		12
#define ID_DFR0_MMAPDBG_SHIFT		8
#define ID_DFR0_COPSDBG_SHIFT		4
#define ID_DFR0_COPDBG_SHIFT		0

#define ID_PFR2_SSBS_SHIFT		4
#define ID_PFR2_CSV3_SHIFT		0

#define MVFR0_FPROUND_SHIFT		28
#define MVFR0_FPSHVEC_SHIFT		24
#define MVFR0_FPSQRT_SHIFT		20
#define MVFR0_FPDIVIDE_SHIFT		16
#define MVFR0_FPTRAP_SHIFT		12
#define MVFR0_FPDP_SHIFT		8
#define MVFR0_FPSP_SHIFT		4
#define MVFR0_SIMD_SHIFT		0

#define MVFR1_SIMDFMAC_SHIFT		28
#define MVFR1_FPHP_SHIFT		24
#define MVFR1_SIMDHP_SHIFT		20
#define MVFR1_SIMDSP_SHIFT		16
#define MVFR1_SIMDINT_SHIFT		12
#define MVFR1_SIMDLS_SHIFT		8
#define MVFR1_FPDNAN_SHIFT		4
#define MVFR1_FPFTZ_SHIFT		0

#define ID_PFR1_GIC_SHIFT		28
#define ID_PFR1_VIRT_FRAC_SHIFT		24
#define ID_PFR1_SEC_FRAC_SHIFT		20
#define ID_PFR1_GENTIMER_SHIFT		16
#define ID_PFR1_VIRTUALIZATION_SHIFT	12
#define ID_PFR1_MPROGMOD_SHIFT		8
#define ID_PFR1_SECURITY_SHIFT		4
#define ID_PFR1_PROGMOD_SHIFT		0

#if defined(CONFIG_ARM64_4K_PAGES)
#define ID_AA64MMFR0_TGRAN_SHIFT		ID_AA64MMFR0_TGRAN4_SHIFT
#define ID_AA64MMFR0_TGRAN_SUPPORTED_MIN	ID_AA64MMFR0_TGRAN4_SUPPORTED_MIN
#define ID_AA64MMFR0_TGRAN_SUPPORTED_MAX	ID_AA64MMFR0_TGRAN4_SUPPORTED_MAX
#define ID_AA64MMFR0_TGRAN_2_SHIFT		ID_AA64MMFR0_TGRAN4_2_SHIFT
#elif defined(CONFIG_ARM64_16K_PAGES)
#define ID_AA64MMFR0_TGRAN_SHIFT		ID_AA64MMFR0_TGRAN16_SHIFT
#define ID_AA64MMFR0_TGRAN_SUPPORTED_MIN	ID_AA64MMFR0_TGRAN16_SUPPORTED_MIN
#define ID_AA64MMFR0_TGRAN_SUPPORTED_MAX	ID_AA64MMFR0_TGRAN16_SUPPORTED_MAX
#define ID_AA64MMFR0_TGRAN_2_SHIFT		ID_AA64MMFR0_TGRAN16_2_SHIFT
#elif defined(CONFIG_ARM64_64K_PAGES)
#define ID_AA64MMFR0_TGRAN_SHIFT		ID_AA64MMFR0_TGRAN64_SHIFT
#define ID_AA64MMFR0_TGRAN_SUPPORTED_MIN	ID_AA64MMFR0_TGRAN64_SUPPORTED_MIN
#define ID_AA64MMFR0_TGRAN_SUPPORTED_MAX	ID_AA64MMFR0_TGRAN64_SUPPORTED_MAX
#define ID_AA64MMFR0_TGRAN_2_SHIFT		ID_AA64MMFR0_TGRAN64_2_SHIFT
#endif

#define MVFR2_FPMISC_SHIFT		4
#define MVFR2_SIMDMISC_SHIFT		0

#define DCZID_DZP_SHIFT			4
#define DCZID_BS_SHIFT			0

/*
 * The ZCR_ELx_LEN_* definitions intentionally include bits [8:4] which
 * are reserved by the SVE architecture for future expansion of the LEN
 * field, with compatible semantics.
 */
#define ZCR_ELx_LEN_SHIFT	0
#define ZCR_ELx_LEN_SIZE	9
#define ZCR_ELx_LEN_MASK	0x1ff

#define CPACR_EL1_ZEN_EL1EN	(BIT(16)) /* enable EL1 access */
#define CPACR_EL1_ZEN_EL0EN	(BIT(17)) /* enable EL0 access, if EL1EN set */
#define CPACR_EL1_ZEN		(CPACR_EL1_ZEN_EL1EN | CPACR_EL1_ZEN_EL0EN)

/* TCR EL1 Bit Definitions */
#define SYS_TCR_EL1_TCMA1	(BIT(58))
#define SYS_TCR_EL1_TCMA0	(BIT(57))

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

/* GMID_EL1 field definitions */
#define SYS_GMID_EL1_BS_SHIFT	0
#define SYS_GMID_EL1_BS_SIZE	4

/* TFSR{,E0}_EL1 bit definitions */
#define SYS_TFSR_EL1_TF0_SHIFT	0
#define SYS_TFSR_EL1_TF1_SHIFT	1
#define SYS_TFSR_EL1_TF0	(UL(1) << SYS_TFSR_EL1_TF0_SHIFT)
#define SYS_TFSR_EL1_TF1	(UL(1) << SYS_TFSR_EL1_TF1_SHIFT)

/* Safe value for MPIDR_EL1: Bit31:RES1, Bit30:U:0, Bit24:MT:0 */
#define SYS_MPIDR_SAFE_VAL	(BIT(31))

#define TRFCR_ELx_TS_SHIFT		5
#define TRFCR_ELx_TS_VIRTUAL		((0x1UL) << TRFCR_ELx_TS_SHIFT)
#define TRFCR_ELx_TS_GUEST_PHYSICAL	((0x2UL) << TRFCR_ELx_TS_SHIFT)
#define TRFCR_ELx_TS_PHYSICAL		((0x3UL) << TRFCR_ELx_TS_SHIFT)
#define TRFCR_EL2_CX			BIT(3)
#define TRFCR_ELx_ExTRE			BIT(1)
#define TRFCR_ELx_E0TRE			BIT(0)


/* GIC Hypervisor interface registers */
/* ICH_MISR_EL2 bit definitions */
#define ICH_MISR_EOI		(1 << 0)
#define ICH_MISR_U		(1 << 1)

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

/* ICH_HCR_EL2 bit definitions */
#define ICH_HCR_EN		(1 << 0)
#define ICH_HCR_UIE		(1 << 1)
#define ICH_HCR_NPIE		(1 << 3)
#define ICH_HCR_TC		(1 << 10)
#define ICH_HCR_TALL0		(1 << 11)
#define ICH_HCR_TALL1		(1 << 12)
#define ICH_HCR_TDIR		(1 << 14)
#define ICH_HCR_EOIcount_SHIFT	27
#define ICH_HCR_EOIcount_MASK	(0x1f << ICH_HCR_EOIcount_SHIFT)

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

/* ICH_VTR_EL2 bit definitions */
#define ICH_VTR_PRI_BITS_SHIFT	29
#define ICH_VTR_PRI_BITS_MASK	(7 << ICH_VTR_PRI_BITS_SHIFT)
#define ICH_VTR_ID_BITS_SHIFT	23
#define ICH_VTR_ID_BITS_MASK	(7 << ICH_VTR_ID_BITS_SHIFT)
#define ICH_VTR_SEIS_SHIFT	22
#define ICH_VTR_SEIS_MASK	(1 << ICH_VTR_SEIS_SHIFT)
#define ICH_VTR_A3V_SHIFT	21
#define ICH_VTR_A3V_MASK	(1 << ICH_VTR_A3V_SHIFT)
#define ICH_VTR_TDS_SHIFT	19
#define ICH_VTR_TDS_MASK	(1 << ICH_VTR_TDS_SHIFT)

#define ARM64_FEATURE_FIELD_BITS	4

/* Create a mask for the feature bits of the specified feature. */
#define ARM64_FEATURE_MASK(x)	(GENMASK_ULL(x##_SHIFT + ARM64_FEATURE_FIELD_BITS - 1, x##_SHIFT))

#ifdef __ASSEMBLY__

	.macro	mrs_s, rt, sreg
	 __emit_inst(0xd5200000|(\sreg)|(.L__gpr_num_\rt))
	.endm

	.macro	msr_s, sreg, rt
	__emit_inst(0xd5000000|(\sreg)|(.L__gpr_num_\rt))
	.endm

#else

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
 */
#define read_sysreg_s(r) ({						\
	u64 __val;							\
	asm volatile(__mrs_s("%0", r) : "=r" (__val));			\
	__val;								\
})

#define write_sysreg_s(v, r) do {					\
	u64 __val = (u64)(v);						\
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

#endif

#endif	/* __ASM_SYSREG_H */
