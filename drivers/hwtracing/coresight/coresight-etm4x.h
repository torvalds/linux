/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 */

#ifndef _CORESIGHT_CORESIGHT_ETM_H
#define _CORESIGHT_CORESIGHT_ETM_H

#include <asm/local.h>
#include <linux/const.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include "coresight-priv.h"

/*
 * Device registers:
 * 0x000 - 0x2FC: Trace		registers
 * 0x300 - 0x314: Management	registers
 * 0x318 - 0xEFC: Trace		registers
 * 0xF00: Management		registers
 * 0xFA0 - 0xFA4: Trace		registers
 * 0xFA8 - 0xFFC: Management	registers
 */
/* Trace registers (0x000-0x2FC) */
/* Main control and configuration registers */
#define TRCPRGCTLR			0x004
#define TRCPROCSELR			0x008
#define TRCSTATR			0x00C
#define TRCCONFIGR			0x010
#define TRCAUXCTLR			0x018
#define TRCEVENTCTL0R			0x020
#define TRCEVENTCTL1R			0x024
#define TRCRSR				0x028
#define TRCSTALLCTLR			0x02C
#define TRCTSCTLR			0x030
#define TRCSYNCPR			0x034
#define TRCCCCTLR			0x038
#define TRCBBCTLR			0x03C
#define TRCTRACEIDR			0x040
#define TRCQCTLR			0x044
/* Filtering control registers */
#define TRCVICTLR			0x080
#define TRCVIIECTLR			0x084
#define TRCVISSCTLR			0x088
#define TRCVIPCSSCTLR			0x08C
#define TRCVDCTLR			0x0A0
#define TRCVDSACCTLR			0x0A4
#define TRCVDARCCTLR			0x0A8
/* Derived resources registers */
#define TRCSEQEVRn(n)			(0x100 + (n * 4)) /* n = 0-2 */
#define TRCSEQRSTEVR			0x118
#define TRCSEQSTR			0x11C
#define TRCEXTINSELR			0x120
#define TRCEXTINSELRn(n)		(0x120 + (n * 4)) /* n = 0-3 */
#define TRCCNTRLDVRn(n)			(0x140 + (n * 4)) /* n = 0-3 */
#define TRCCNTCTLRn(n)			(0x150 + (n * 4)) /* n = 0-3 */
#define TRCCNTVRn(n)			(0x160 + (n * 4)) /* n = 0-3 */
/* ID registers */
#define TRCIDR8				0x180
#define TRCIDR9				0x184
#define TRCIDR10			0x188
#define TRCIDR11			0x18C
#define TRCIDR12			0x190
#define TRCIDR13			0x194
#define TRCIMSPEC0			0x1C0
#define TRCIMSPECn(n)			(0x1C0 + (n * 4)) /* n = 1-7 */
#define TRCIDR0				0x1E0
#define TRCIDR1				0x1E4
#define TRCIDR2				0x1E8
#define TRCIDR3				0x1EC
#define TRCIDR4				0x1F0
#define TRCIDR5				0x1F4
#define TRCIDR6				0x1F8
#define TRCIDR7				0x1FC
/*
 * Resource selection registers, n = 2-31.
 * First pair (regs 0, 1) is always present and is reserved.
 */
#define TRCRSCTLRn(n)			(0x200 + (n * 4))
/* Single-shot comparator registers, n = 0-7 */
#define TRCSSCCRn(n)			(0x280 + (n * 4))
#define TRCSSCSRn(n)			(0x2A0 + (n * 4))
#define TRCSSPCICRn(n)			(0x2C0 + (n * 4))
/* Management registers (0x300-0x314) */
#define TRCOSLAR			0x300
#define TRCOSLSR			0x304
#define TRCPDCR				0x310
#define TRCPDSR				0x314
/* Trace registers (0x318-0xEFC) */
/* Address Comparator registers n = 0-15 */
#define TRCACVRn(n)			(0x400 + (n * 8))
#define TRCACATRn(n)			(0x480 + (n * 8))
/* Data Value Comparator Value registers, n = 0-7 */
#define TRCDVCVRn(n)			(0x500 + (n * 16))
#define TRCDVCMRn(n)			(0x580 + (n * 16))
/* ContextID/Virtual ContextID comparators, n = 0-7 */
#define TRCCIDCVRn(n)			(0x600 + (n * 8))
#define TRCVMIDCVRn(n)			(0x640 + (n * 8))
#define TRCCIDCCTLR0			0x680
#define TRCCIDCCTLR1			0x684
#define TRCVMIDCCTLR0			0x688
#define TRCVMIDCCTLR1			0x68C
/* Management register (0xF00) */
/* Integration control registers */
#define TRCITCTRL			0xF00
/* Trace registers (0xFA0-0xFA4) */
/* Claim tag registers */
#define TRCCLAIMSET			0xFA0
#define TRCCLAIMCLR			0xFA4
/* Management registers (0xFA8-0xFFC) */
#define TRCDEVAFF0			0xFA8
#define TRCDEVAFF1			0xFAC
#define TRCLAR				0xFB0
#define TRCLSR				0xFB4
#define TRCAUTHSTATUS			0xFB8
#define TRCDEVARCH			0xFBC
#define TRCDEVID			0xFC8
#define TRCDEVTYPE			0xFCC
#define TRCPIDR4			0xFD0
#define TRCPIDR5			0xFD4
#define TRCPIDR6			0xFD8
#define TRCPIDR7			0xFDC
#define TRCPIDR0			0xFE0
#define TRCPIDR1			0xFE4
#define TRCPIDR2			0xFE8
#define TRCPIDR3			0xFEC
#define TRCCIDR0			0xFF0
#define TRCCIDR1			0xFF4
#define TRCCIDR2			0xFF8
#define TRCCIDR3			0xFFC

#define TRCRSR_TA			BIT(12)

/*
 * Bit positions of registers that are defined above, in the sysreg.h style
 * of _MASK for multi bit fields and BIT() for single bits.
 */
#define TRCIDR0_INSTP0_MASK			GENMASK(2, 1)
#define TRCIDR0_TRCBB				BIT(5)
#define TRCIDR0_TRCCOND				BIT(6)
#define TRCIDR0_TRCCCI				BIT(7)
#define TRCIDR0_RETSTACK			BIT(9)
#define TRCIDR0_NUMEVENT_MASK			GENMASK(11, 10)
#define TRCIDR0_QSUPP_MASK			GENMASK(16, 15)
#define TRCIDR0_TSSIZE_MASK			GENMASK(28, 24)

#define TRCIDR2_CIDSIZE_MASK			GENMASK(9, 5)
#define TRCIDR2_VMIDSIZE_MASK			GENMASK(14, 10)
#define TRCIDR2_CCSIZE_MASK			GENMASK(28, 25)

#define TRCIDR3_CCITMIN_MASK			GENMASK(11, 0)
#define TRCIDR3_EXLEVEL_S_MASK			GENMASK(19, 16)
#define TRCIDR3_EXLEVEL_NS_MASK			GENMASK(23, 20)
#define TRCIDR3_TRCERR				BIT(24)
#define TRCIDR3_SYNCPR				BIT(25)
#define TRCIDR3_STALLCTL			BIT(26)
#define TRCIDR3_SYSSTALL			BIT(27)
#define TRCIDR3_NUMPROC_LO_MASK			GENMASK(30, 28)
#define TRCIDR3_NUMPROC_HI_MASK			GENMASK(13, 12)
#define TRCIDR3_NOOVERFLOW			BIT(31)

#define TRCIDR4_NUMACPAIRS_MASK			GENMASK(3, 0)
#define TRCIDR4_NUMPC_MASK			GENMASK(15, 12)
#define TRCIDR4_NUMRSPAIR_MASK			GENMASK(19, 16)
#define TRCIDR4_NUMSSCC_MASK			GENMASK(23, 20)
#define TRCIDR4_NUMCIDC_MASK			GENMASK(27, 24)
#define TRCIDR4_NUMVMIDC_MASK			GENMASK(31, 28)

#define TRCIDR5_NUMEXTIN_MASK			GENMASK(8, 0)
#define TRCIDR5_TRACEIDSIZE_MASK		GENMASK(21, 16)
#define TRCIDR5_ATBTRIG				BIT(22)
#define TRCIDR5_LPOVERRIDE			BIT(23)
#define TRCIDR5_NUMSEQSTATE_MASK		GENMASK(27, 25)
#define TRCIDR5_NUMCNTR_MASK			GENMASK(30, 28)

#define TRCCONFIGR_INSTP0_LOAD			BIT(1)
#define TRCCONFIGR_INSTP0_STORE			BIT(2)
#define TRCCONFIGR_INSTP0_LOAD_STORE		(TRCCONFIGR_INSTP0_LOAD | TRCCONFIGR_INSTP0_STORE)
#define TRCCONFIGR_BB				BIT(3)
#define TRCCONFIGR_CCI				BIT(4)
#define TRCCONFIGR_CID				BIT(6)
#define TRCCONFIGR_VMID				BIT(7)
#define TRCCONFIGR_COND_MASK			GENMASK(10, 8)
#define TRCCONFIGR_TS				BIT(11)
#define TRCCONFIGR_RS				BIT(12)
#define TRCCONFIGR_QE_W_COUNTS			BIT(13)
#define TRCCONFIGR_QE_WO_COUNTS			BIT(14)
#define TRCCONFIGR_VMIDOPT			BIT(15)
#define TRCCONFIGR_DA				BIT(16)
#define TRCCONFIGR_DV				BIT(17)

#define TRCEVENTCTL1R_INSTEN_MASK		GENMASK(3, 0)
#define TRCEVENTCTL1R_INSTEN_0			BIT(0)
#define TRCEVENTCTL1R_INSTEN_1			BIT(1)
#define TRCEVENTCTL1R_INSTEN_2			BIT(2)
#define TRCEVENTCTL1R_INSTEN_3			BIT(3)
#define TRCEVENTCTL1R_ATB			BIT(11)
#define TRCEVENTCTL1R_LPOVERRIDE		BIT(12)

#define TRCSTALLCTLR_ISTALL			BIT(8)
#define TRCSTALLCTLR_INSTPRIORITY		BIT(10)
#define TRCSTALLCTLR_NOOVERFLOW			BIT(13)

#define TRCVICTLR_EVENT_MASK			GENMASK(7, 0)
#define TRCVICTLR_SSSTATUS			BIT(9)
#define TRCVICTLR_TRCRESET			BIT(10)
#define TRCVICTLR_TRCERR			BIT(11)
#define TRCVICTLR_EXLEVEL_MASK			GENMASK(22, 16)
#define TRCVICTLR_EXLEVEL_S_MASK		GENMASK(19, 16)
#define TRCVICTLR_EXLEVEL_NS_MASK		GENMASK(22, 20)

#define TRCACATRn_TYPE_MASK			GENMASK(1, 0)
#define TRCACATRn_CONTEXTTYPE_MASK		GENMASK(3, 2)
#define TRCACATRn_CONTEXTTYPE_CTXID		BIT(2)
#define TRCACATRn_CONTEXTTYPE_VMID		BIT(3)
#define TRCACATRn_CONTEXT_MASK			GENMASK(6, 4)
#define TRCACATRn_EXLEVEL_MASK			GENMASK(14, 8)

#define TRCSSCSRn_STATUS			BIT(31)
#define TRCSSCCRn_SAC_ARC_RST_MASK		GENMASK(24, 0)

#define TRCSSPCICRn_PC_MASK			GENMASK(7, 0)

#define TRCBBCTLR_MODE				BIT(8)
#define TRCBBCTLR_RANGE_MASK			GENMASK(7, 0)

#define TRCRSCTLRn_PAIRINV			BIT(21)
#define TRCRSCTLRn_INV				BIT(20)
#define TRCRSCTLRn_GROUP_MASK			GENMASK(19, 16)
#define TRCRSCTLRn_SELECT_MASK			GENMASK(15, 0)

/*
 * System instructions to access ETM registers.
 * See ETMv4.4 spec ARM IHI0064F section 4.3.6 System instructions
 */
#define ETM4x_OFFSET_TO_REG(x)		((x) >> 2)

#define ETM4x_CRn(n)			(((n) >> 7) & 0x7)
#define ETM4x_Op2(n)			(((n) >> 4) & 0x7)
#define ETM4x_CRm(n)			((n) & 0xf)

#include <asm/sysreg.h>
#define ETM4x_REG_NUM_TO_SYSREG(n)				\
	sys_reg(2, 1, ETM4x_CRn(n), ETM4x_CRm(n), ETM4x_Op2(n))

#define READ_ETM4x_REG(reg)					\
	read_sysreg_s(ETM4x_REG_NUM_TO_SYSREG((reg)))
#define WRITE_ETM4x_REG(val, reg)				\
	write_sysreg_s(val, ETM4x_REG_NUM_TO_SYSREG((reg)))

#define read_etm4x_sysreg_const_offset(offset)			\
	READ_ETM4x_REG(ETM4x_OFFSET_TO_REG(offset))

#define write_etm4x_sysreg_const_offset(val, offset)		\
	WRITE_ETM4x_REG(val, ETM4x_OFFSET_TO_REG(offset))

#define CASE_READ(res, x)					\
	case (x): { (res) = read_etm4x_sysreg_const_offset((x)); break; }

#define CASE_WRITE(val, x)					\
	case (x): { write_etm4x_sysreg_const_offset((val), (x)); break; }

#define CASE_NOP(__unused, x)					\
	case (x):	/* fall through */

#define ETE_ONLY_SYSREG_LIST(op, val)		\
	CASE_##op((val), TRCRSR)		\
	CASE_##op((val), TRCEXTINSELRn(1))	\
	CASE_##op((val), TRCEXTINSELRn(2))	\
	CASE_##op((val), TRCEXTINSELRn(3))

/* List of registers accessible via System instructions */
#define ETM4x_ONLY_SYSREG_LIST(op, val)		\
	CASE_##op((val), TRCPROCSELR)		\
	CASE_##op((val), TRCVDCTLR)		\
	CASE_##op((val), TRCVDSACCTLR)		\
	CASE_##op((val), TRCVDARCCTLR)		\
	CASE_##op((val), TRCOSLAR)

#define ETM_COMMON_SYSREG_LIST(op, val)		\
	CASE_##op((val), TRCPRGCTLR)		\
	CASE_##op((val), TRCSTATR)		\
	CASE_##op((val), TRCCONFIGR)		\
	CASE_##op((val), TRCAUXCTLR)		\
	CASE_##op((val), TRCEVENTCTL0R)		\
	CASE_##op((val), TRCEVENTCTL1R)		\
	CASE_##op((val), TRCSTALLCTLR)		\
	CASE_##op((val), TRCTSCTLR)		\
	CASE_##op((val), TRCSYNCPR)		\
	CASE_##op((val), TRCCCCTLR)		\
	CASE_##op((val), TRCBBCTLR)		\
	CASE_##op((val), TRCTRACEIDR)		\
	CASE_##op((val), TRCQCTLR)		\
	CASE_##op((val), TRCVICTLR)		\
	CASE_##op((val), TRCVIIECTLR)		\
	CASE_##op((val), TRCVISSCTLR)		\
	CASE_##op((val), TRCVIPCSSCTLR)		\
	CASE_##op((val), TRCSEQEVRn(0))		\
	CASE_##op((val), TRCSEQEVRn(1))		\
	CASE_##op((val), TRCSEQEVRn(2))		\
	CASE_##op((val), TRCSEQRSTEVR)		\
	CASE_##op((val), TRCSEQSTR)		\
	CASE_##op((val), TRCEXTINSELR)		\
	CASE_##op((val), TRCCNTRLDVRn(0))	\
	CASE_##op((val), TRCCNTRLDVRn(1))	\
	CASE_##op((val), TRCCNTRLDVRn(2))	\
	CASE_##op((val), TRCCNTRLDVRn(3))	\
	CASE_##op((val), TRCCNTCTLRn(0))	\
	CASE_##op((val), TRCCNTCTLRn(1))	\
	CASE_##op((val), TRCCNTCTLRn(2))	\
	CASE_##op((val), TRCCNTCTLRn(3))	\
	CASE_##op((val), TRCCNTVRn(0))		\
	CASE_##op((val), TRCCNTVRn(1))		\
	CASE_##op((val), TRCCNTVRn(2))		\
	CASE_##op((val), TRCCNTVRn(3))		\
	CASE_##op((val), TRCIDR8)		\
	CASE_##op((val), TRCIDR9)		\
	CASE_##op((val), TRCIDR10)		\
	CASE_##op((val), TRCIDR11)		\
	CASE_##op((val), TRCIDR12)		\
	CASE_##op((val), TRCIDR13)		\
	CASE_##op((val), TRCIMSPECn(0))		\
	CASE_##op((val), TRCIMSPECn(1))		\
	CASE_##op((val), TRCIMSPECn(2))		\
	CASE_##op((val), TRCIMSPECn(3))		\
	CASE_##op((val), TRCIMSPECn(4))		\
	CASE_##op((val), TRCIMSPECn(5))		\
	CASE_##op((val), TRCIMSPECn(6))		\
	CASE_##op((val), TRCIMSPECn(7))		\
	CASE_##op((val), TRCIDR0)		\
	CASE_##op((val), TRCIDR1)		\
	CASE_##op((val), TRCIDR2)		\
	CASE_##op((val), TRCIDR3)		\
	CASE_##op((val), TRCIDR4)		\
	CASE_##op((val), TRCIDR5)		\
	CASE_##op((val), TRCIDR6)		\
	CASE_##op((val), TRCIDR7)		\
	CASE_##op((val), TRCRSCTLRn(2))		\
	CASE_##op((val), TRCRSCTLRn(3))		\
	CASE_##op((val), TRCRSCTLRn(4))		\
	CASE_##op((val), TRCRSCTLRn(5))		\
	CASE_##op((val), TRCRSCTLRn(6))		\
	CASE_##op((val), TRCRSCTLRn(7))		\
	CASE_##op((val), TRCRSCTLRn(8))		\
	CASE_##op((val), TRCRSCTLRn(9))		\
	CASE_##op((val), TRCRSCTLRn(10))	\
	CASE_##op((val), TRCRSCTLRn(11))	\
	CASE_##op((val), TRCRSCTLRn(12))	\
	CASE_##op((val), TRCRSCTLRn(13))	\
	CASE_##op((val), TRCRSCTLRn(14))	\
	CASE_##op((val), TRCRSCTLRn(15))	\
	CASE_##op((val), TRCRSCTLRn(16))	\
	CASE_##op((val), TRCRSCTLRn(17))	\
	CASE_##op((val), TRCRSCTLRn(18))	\
	CASE_##op((val), TRCRSCTLRn(19))	\
	CASE_##op((val), TRCRSCTLRn(20))	\
	CASE_##op((val), TRCRSCTLRn(21))	\
	CASE_##op((val), TRCRSCTLRn(22))	\
	CASE_##op((val), TRCRSCTLRn(23))	\
	CASE_##op((val), TRCRSCTLRn(24))	\
	CASE_##op((val), TRCRSCTLRn(25))	\
	CASE_##op((val), TRCRSCTLRn(26))	\
	CASE_##op((val), TRCRSCTLRn(27))	\
	CASE_##op((val), TRCRSCTLRn(28))	\
	CASE_##op((val), TRCRSCTLRn(29))	\
	CASE_##op((val), TRCRSCTLRn(30))	\
	CASE_##op((val), TRCRSCTLRn(31))	\
	CASE_##op((val), TRCSSCCRn(0))		\
	CASE_##op((val), TRCSSCCRn(1))		\
	CASE_##op((val), TRCSSCCRn(2))		\
	CASE_##op((val), TRCSSCCRn(3))		\
	CASE_##op((val), TRCSSCCRn(4))		\
	CASE_##op((val), TRCSSCCRn(5))		\
	CASE_##op((val), TRCSSCCRn(6))		\
	CASE_##op((val), TRCSSCCRn(7))		\
	CASE_##op((val), TRCSSCSRn(0))		\
	CASE_##op((val), TRCSSCSRn(1))		\
	CASE_##op((val), TRCSSCSRn(2))		\
	CASE_##op((val), TRCSSCSRn(3))		\
	CASE_##op((val), TRCSSCSRn(4))		\
	CASE_##op((val), TRCSSCSRn(5))		\
	CASE_##op((val), TRCSSCSRn(6))		\
	CASE_##op((val), TRCSSCSRn(7))		\
	CASE_##op((val), TRCSSPCICRn(0))	\
	CASE_##op((val), TRCSSPCICRn(1))	\
	CASE_##op((val), TRCSSPCICRn(2))	\
	CASE_##op((val), TRCSSPCICRn(3))	\
	CASE_##op((val), TRCSSPCICRn(4))	\
	CASE_##op((val), TRCSSPCICRn(5))	\
	CASE_##op((val), TRCSSPCICRn(6))	\
	CASE_##op((val), TRCSSPCICRn(7))	\
	CASE_##op((val), TRCOSLSR)		\
	CASE_##op((val), TRCACVRn(0))		\
	CASE_##op((val), TRCACVRn(1))		\
	CASE_##op((val), TRCACVRn(2))		\
	CASE_##op((val), TRCACVRn(3))		\
	CASE_##op((val), TRCACVRn(4))		\
	CASE_##op((val), TRCACVRn(5))		\
	CASE_##op((val), TRCACVRn(6))		\
	CASE_##op((val), TRCACVRn(7))		\
	CASE_##op((val), TRCACVRn(8))		\
	CASE_##op((val), TRCACVRn(9))		\
	CASE_##op((val), TRCACVRn(10))		\
	CASE_##op((val), TRCACVRn(11))		\
	CASE_##op((val), TRCACVRn(12))		\
	CASE_##op((val), TRCACVRn(13))		\
	CASE_##op((val), TRCACVRn(14))		\
	CASE_##op((val), TRCACVRn(15))		\
	CASE_##op((val), TRCACATRn(0))		\
	CASE_##op((val), TRCACATRn(1))		\
	CASE_##op((val), TRCACATRn(2))		\
	CASE_##op((val), TRCACATRn(3))		\
	CASE_##op((val), TRCACATRn(4))		\
	CASE_##op((val), TRCACATRn(5))		\
	CASE_##op((val), TRCACATRn(6))		\
	CASE_##op((val), TRCACATRn(7))		\
	CASE_##op((val), TRCACATRn(8))		\
	CASE_##op((val), TRCACATRn(9))		\
	CASE_##op((val), TRCACATRn(10))		\
	CASE_##op((val), TRCACATRn(11))		\
	CASE_##op((val), TRCACATRn(12))		\
	CASE_##op((val), TRCACATRn(13))		\
	CASE_##op((val), TRCACATRn(14))		\
	CASE_##op((val), TRCACATRn(15))		\
	CASE_##op((val), TRCDVCVRn(0))		\
	CASE_##op((val), TRCDVCVRn(1))		\
	CASE_##op((val), TRCDVCVRn(2))		\
	CASE_##op((val), TRCDVCVRn(3))		\
	CASE_##op((val), TRCDVCVRn(4))		\
	CASE_##op((val), TRCDVCVRn(5))		\
	CASE_##op((val), TRCDVCVRn(6))		\
	CASE_##op((val), TRCDVCVRn(7))		\
	CASE_##op((val), TRCDVCMRn(0))		\
	CASE_##op((val), TRCDVCMRn(1))		\
	CASE_##op((val), TRCDVCMRn(2))		\
	CASE_##op((val), TRCDVCMRn(3))		\
	CASE_##op((val), TRCDVCMRn(4))		\
	CASE_##op((val), TRCDVCMRn(5))		\
	CASE_##op((val), TRCDVCMRn(6))		\
	CASE_##op((val), TRCDVCMRn(7))		\
	CASE_##op((val), TRCCIDCVRn(0))		\
	CASE_##op((val), TRCCIDCVRn(1))		\
	CASE_##op((val), TRCCIDCVRn(2))		\
	CASE_##op((val), TRCCIDCVRn(3))		\
	CASE_##op((val), TRCCIDCVRn(4))		\
	CASE_##op((val), TRCCIDCVRn(5))		\
	CASE_##op((val), TRCCIDCVRn(6))		\
	CASE_##op((val), TRCCIDCVRn(7))		\
	CASE_##op((val), TRCVMIDCVRn(0))	\
	CASE_##op((val), TRCVMIDCVRn(1))	\
	CASE_##op((val), TRCVMIDCVRn(2))	\
	CASE_##op((val), TRCVMIDCVRn(3))	\
	CASE_##op((val), TRCVMIDCVRn(4))	\
	CASE_##op((val), TRCVMIDCVRn(5))	\
	CASE_##op((val), TRCVMIDCVRn(6))	\
	CASE_##op((val), TRCVMIDCVRn(7))	\
	CASE_##op((val), TRCCIDCCTLR0)		\
	CASE_##op((val), TRCCIDCCTLR1)		\
	CASE_##op((val), TRCVMIDCCTLR0)		\
	CASE_##op((val), TRCVMIDCCTLR1)		\
	CASE_##op((val), TRCCLAIMSET)		\
	CASE_##op((val), TRCCLAIMCLR)		\
	CASE_##op((val), TRCAUTHSTATUS)		\
	CASE_##op((val), TRCDEVARCH)		\
	CASE_##op((val), TRCDEVID)

/* List of registers only accessible via memory-mapped interface */
#define ETM_MMAP_LIST(op, val)			\
	CASE_##op((val), TRCDEVTYPE)		\
	CASE_##op((val), TRCPDCR)		\
	CASE_##op((val), TRCPDSR)		\
	CASE_##op((val), TRCDEVAFF0)		\
	CASE_##op((val), TRCDEVAFF1)		\
	CASE_##op((val), TRCLAR)		\
	CASE_##op((val), TRCLSR)		\
	CASE_##op((val), TRCITCTRL)		\
	CASE_##op((val), TRCPIDR4)		\
	CASE_##op((val), TRCPIDR0)		\
	CASE_##op((val), TRCPIDR1)		\
	CASE_##op((val), TRCPIDR2)		\
	CASE_##op((val), TRCPIDR3)

#define ETM4x_READ_SYSREG_CASES(res)		\
	ETM_COMMON_SYSREG_LIST(READ, (res))	\
	ETM4x_ONLY_SYSREG_LIST(READ, (res))

#define ETM4x_WRITE_SYSREG_CASES(val)		\
	ETM_COMMON_SYSREG_LIST(WRITE, (val))	\
	ETM4x_ONLY_SYSREG_LIST(WRITE, (val))

#define ETM_COMMON_SYSREG_LIST_CASES		\
	ETM_COMMON_SYSREG_LIST(NOP, __unused)

#define ETM4x_ONLY_SYSREG_LIST_CASES		\
	ETM4x_ONLY_SYSREG_LIST(NOP, __unused)

#define ETM4x_SYSREG_LIST_CASES			\
	ETM_COMMON_SYSREG_LIST_CASES		\
	ETM4x_ONLY_SYSREG_LIST(NOP, __unused)

#define ETM4x_MMAP_LIST_CASES		ETM_MMAP_LIST(NOP, __unused)

/* ETE only supports system register access */
#define ETE_READ_CASES(res)			\
	ETM_COMMON_SYSREG_LIST(READ, (res))	\
	ETE_ONLY_SYSREG_LIST(READ, (res))

#define ETE_WRITE_CASES(val)			\
	ETM_COMMON_SYSREG_LIST(WRITE, (val))	\
	ETE_ONLY_SYSREG_LIST(WRITE, (val))

#define ETE_ONLY_SYSREG_LIST_CASES		\
	ETE_ONLY_SYSREG_LIST(NOP, __unused)

#define read_etm4x_sysreg_offset(offset, _64bit)				\
	({									\
		u64 __val;							\
										\
		if (__is_constexpr((offset)))					\
			__val = read_etm4x_sysreg_const_offset((offset));	\
		else								\
			__val = etm4x_sysreg_read((offset), true, (_64bit));	\
		__val;								\
	 })

#define write_etm4x_sysreg_offset(val, offset, _64bit)			\
	do {								\
		if (__builtin_constant_p((offset)))			\
			write_etm4x_sysreg_const_offset((val),		\
							(offset));	\
		else							\
			etm4x_sysreg_write((val), (offset), true,	\
					   (_64bit));			\
	} while (0)


#define etm4x_relaxed_read32(csa, offset)				\
	((u32)((csa)->io_mem ?						\
		 readl_relaxed((csa)->base + (offset)) :		\
		 read_etm4x_sysreg_offset((offset), false)))

#define etm4x_relaxed_read64(csa, offset)				\
	((u64)((csa)->io_mem ?						\
		 readq_relaxed((csa)->base + (offset)) :		\
		 read_etm4x_sysreg_offset((offset), true)))

#define etm4x_read32(csa, offset)					\
	({								\
		u32 __val = etm4x_relaxed_read32((csa), (offset));	\
		__io_ar(__val);						\
		__val;							\
	 })

#define etm4x_read64(csa, offset)					\
	({								\
		u64 __val = etm4x_relaxed_read64((csa), (offset));	\
		__io_ar(__val);						\
		__val;							\
	 })

#define etm4x_relaxed_write32(csa, val, offset)				\
	do {								\
		if ((csa)->io_mem)					\
			writel_relaxed((val), (csa)->base + (offset));	\
		else							\
			write_etm4x_sysreg_offset((val), (offset),	\
						  false);		\
	} while (0)

#define etm4x_relaxed_write64(csa, val, offset)				\
	do {								\
		if ((csa)->io_mem)					\
			writeq_relaxed((val), (csa)->base + (offset));	\
		else							\
			write_etm4x_sysreg_offset((val), (offset),	\
						  true);		\
	} while (0)

#define etm4x_write32(csa, val, offset)					\
	do {								\
		__io_bw();						\
		etm4x_relaxed_write32((csa), (val), (offset));		\
	} while (0)

#define etm4x_write64(csa, val, offset)					\
	do {								\
		__io_bw();						\
		etm4x_relaxed_write64((csa), (val), (offset));		\
	} while (0)


/* ETMv4 resources */
#define ETM_MAX_NR_PE			8
#define ETMv4_MAX_CNTR			4
#define ETM_MAX_SEQ_STATES		4
#define ETM_MAX_EXT_INP_SEL		4
#define ETM_MAX_EXT_INP			256
#define ETM_MAX_EXT_OUT			4
#define ETM_MAX_SINGLE_ADDR_CMP		16
#define ETM_MAX_ADDR_RANGE_CMP		(ETM_MAX_SINGLE_ADDR_CMP / 2)
#define ETM_MAX_DATA_VAL_CMP		8
#define ETMv4_MAX_CTXID_CMP		8
#define ETM_MAX_VMID_CMP		8
#define ETM_MAX_PE_CMP			8
#define ETM_MAX_RES_SEL			32
#define ETM_MAX_SS_CMP			8

#define ETMv4_SYNC_MASK			0x1F
#define ETM_CYC_THRESHOLD_MASK		0xFFF
#define ETM_CYC_THRESHOLD_DEFAULT       0x100
#define ETMv4_EVENT_MASK		0xFF
#define ETM_CNTR_MAX_VAL		0xFFFF
#define ETM_TRACEID_MASK		0x3f

/* ETMv4 programming modes */
#define ETM_MODE_EXCLUDE		BIT(0)
#define ETM_MODE_LOAD			BIT(1)
#define ETM_MODE_STORE			BIT(2)
#define ETM_MODE_LOAD_STORE		BIT(3)
#define ETM_MODE_BB			BIT(4)
#define ETMv4_MODE_CYCACC		BIT(5)
#define ETMv4_MODE_CTXID		BIT(6)
#define ETM_MODE_VMID			BIT(7)
#define ETM_MODE_COND(val)		BMVAL(val, 8, 10)
#define ETMv4_MODE_TIMESTAMP		BIT(11)
#define ETM_MODE_RETURNSTACK		BIT(12)
#define ETM_MODE_QELEM(val)		BMVAL(val, 13, 14)
#define ETM_MODE_DATA_TRACE_ADDR	BIT(15)
#define ETM_MODE_DATA_TRACE_VAL		BIT(16)
#define ETM_MODE_ISTALL			BIT(17)
#define ETM_MODE_DSTALL			BIT(18)
#define ETM_MODE_ATB_TRIGGER		BIT(19)
#define ETM_MODE_LPOVERRIDE		BIT(20)
#define ETM_MODE_ISTALL_EN		BIT(21)
#define ETM_MODE_DSTALL_EN		BIT(22)
#define ETM_MODE_INSTPRIO		BIT(23)
#define ETM_MODE_NOOVERFLOW		BIT(24)
#define ETM_MODE_TRACE_RESET		BIT(25)
#define ETM_MODE_TRACE_ERR		BIT(26)
#define ETM_MODE_VIEWINST_STARTSTOP	BIT(27)
#define ETMv4_MODE_ALL			(GENMASK(27, 0) | \
					 ETM_MODE_EXCL_KERN | \
					 ETM_MODE_EXCL_USER)

/*
 * TRCOSLSR.OSLM advertises the OS Lock model.
 * OSLM[2:0] = TRCOSLSR[4:3,0]
 *
 *	0b000 - Trace OS Lock is not implemented.
 *	0b010 - Trace OS Lock is implemented.
 *	0b100 - Trace OS Lock is not implemented, unit is controlled by PE OS Lock.
 */
#define ETM_OSLOCK_NI		0b000
#define ETM_OSLOCK_PRESENT	0b010
#define ETM_OSLOCK_PE		0b100

#define ETM_OSLSR_OSLM(oslsr)	((((oslsr) & GENMASK(4, 3)) >> 2) | (oslsr & 0x1))

/*
 * TRCDEVARCH Bit field definitions
 * Bits[31:21]	- ARCHITECT = Always Arm Ltd.
 *                * Bits[31:28] = 0x4
 *                * Bits[27:21] = 0b0111011
 * Bit[20]	- PRESENT,  Indicates the presence of this register.
 *
 * Bit[19:16]	- REVISION, Revision of the architecture.
 *
 * Bit[15:0]	- ARCHID, Identifies this component as an ETM
 *                * Bits[15:12] - architecture version of ETM
 *                *             = 4 for ETMv4
 *                * Bits[11:0] = 0xA13, architecture part number for ETM.
 */
#define ETM_DEVARCH_ARCHITECT_MASK		GENMASK(31, 21)
#define ETM_DEVARCH_ARCHITECT_ARM		((0x4 << 28) | (0b0111011 << 21))
#define ETM_DEVARCH_PRESENT			BIT(20)
#define ETM_DEVARCH_REVISION_SHIFT		16
#define ETM_DEVARCH_REVISION_MASK		GENMASK(19, 16)
#define ETM_DEVARCH_REVISION(x)			\
	(((x) & ETM_DEVARCH_REVISION_MASK) >> ETM_DEVARCH_REVISION_SHIFT)
#define ETM_DEVARCH_ARCHID_MASK			GENMASK(15, 0)
#define ETM_DEVARCH_ARCHID_ARCH_VER_SHIFT	12
#define ETM_DEVARCH_ARCHID_ARCH_VER_MASK	GENMASK(15, 12)
#define ETM_DEVARCH_ARCHID_ARCH_VER(x)		\
	(((x) & ETM_DEVARCH_ARCHID_ARCH_VER_MASK) >> ETM_DEVARCH_ARCHID_ARCH_VER_SHIFT)

#define ETM_DEVARCH_MAKE_ARCHID_ARCH_VER(ver)			\
	(((ver) << ETM_DEVARCH_ARCHID_ARCH_VER_SHIFT) & ETM_DEVARCH_ARCHID_ARCH_VER_MASK)

#define ETM_DEVARCH_ARCHID_ARCH_PART(x)		((x) & 0xfffUL)

#define ETM_DEVARCH_MAKE_ARCHID(major)			\
	((ETM_DEVARCH_MAKE_ARCHID_ARCH_VER(major)) | ETM_DEVARCH_ARCHID_ARCH_PART(0xA13))

#define ETM_DEVARCH_ARCHID_ETMv4x		ETM_DEVARCH_MAKE_ARCHID(0x4)
#define ETM_DEVARCH_ARCHID_ETE			ETM_DEVARCH_MAKE_ARCHID(0x5)

#define ETM_DEVARCH_ID_MASK						\
	(ETM_DEVARCH_ARCHITECT_MASK | ETM_DEVARCH_ARCHID_MASK | ETM_DEVARCH_PRESENT)
#define ETM_DEVARCH_ETMv4x_ARCH						\
	(ETM_DEVARCH_ARCHITECT_ARM | ETM_DEVARCH_ARCHID_ETMv4x | ETM_DEVARCH_PRESENT)
#define ETM_DEVARCH_ETE_ARCH						\
	(ETM_DEVARCH_ARCHITECT_ARM | ETM_DEVARCH_ARCHID_ETE | ETM_DEVARCH_PRESENT)

#define TRCSTATR_IDLE_BIT		0
#define TRCSTATR_PMSTABLE_BIT		1
#define ETM_DEFAULT_ADDR_COMP		0

#define TRCSSCSRn_PC			BIT(3)

/* PowerDown Control Register bits */
#define TRCPDCR_PU			BIT(3)

#define TRCACATR_EXLEVEL_SHIFT		8

/*
 * Exception level mask for Secure and Non-Secure ELs.
 * ETM defines the bits for EL control (e.g, TRVICTLR, TRCACTRn).
 * The Secure and Non-Secure ELs are always to gether.
 * Non-secure EL3 is never implemented.
 * We use the following generic mask as they appear in different
 * registers and this can be shifted for the appropriate
 * fields.
 */
#define ETM_EXLEVEL_S_APP		BIT(0)	/* Secure EL0		*/
#define ETM_EXLEVEL_S_OS		BIT(1)	/* Secure EL1		*/
#define ETM_EXLEVEL_S_HYP		BIT(2)	/* Secure EL2		*/
#define ETM_EXLEVEL_S_MON		BIT(3)	/* Secure EL3/Monitor	*/
#define ETM_EXLEVEL_NS_APP		BIT(4)	/* NonSecure EL0	*/
#define ETM_EXLEVEL_NS_OS		BIT(5)	/* NonSecure EL1	*/
#define ETM_EXLEVEL_NS_HYP		BIT(6)	/* NonSecure EL2	*/

/* access level controls in TRCACATRn */
#define TRCACATR_EXLEVEL_SHIFT		8

#define ETM_TRCIDR1_ARCH_MAJOR_SHIFT	8
#define ETM_TRCIDR1_ARCH_MAJOR_MASK	(0xfU << ETM_TRCIDR1_ARCH_MAJOR_SHIFT)
#define ETM_TRCIDR1_ARCH_MAJOR(x)	\
	(((x) & ETM_TRCIDR1_ARCH_MAJOR_MASK) >> ETM_TRCIDR1_ARCH_MAJOR_SHIFT)
#define ETM_TRCIDR1_ARCH_MINOR_SHIFT	4
#define ETM_TRCIDR1_ARCH_MINOR_MASK	(0xfU << ETM_TRCIDR1_ARCH_MINOR_SHIFT)
#define ETM_TRCIDR1_ARCH_MINOR(x)	\
	(((x) & ETM_TRCIDR1_ARCH_MINOR_MASK) >> ETM_TRCIDR1_ARCH_MINOR_SHIFT)
#define ETM_TRCIDR1_ARCH_SHIFT		ETM_TRCIDR1_ARCH_MINOR_SHIFT
#define ETM_TRCIDR1_ARCH_MASK		\
	(ETM_TRCIDR1_ARCH_MAJOR_MASK | ETM_TRCIDR1_ARCH_MINOR_MASK)

#define ETM_TRCIDR1_ARCH_ETMv4		0x4

/*
 * Driver representation of the ETM architecture.
 * The version of an ETM component can be detected from
 *
 * TRCDEVARCH	- CoreSight architected register
 *                - Bits[15:12] - Major version
 *                - Bits[19:16] - Minor version
 *
 * We must rely only on TRCDEVARCH for the version information. Even though,
 * TRCIDR1 also provides the architecture version, it is a "Trace" register
 * and as such must be accessed only with Trace power domain ON. This may
 * not be available at probe time.
 *
 * Now to make certain decisions easier based on the version
 * we use an internal representation of the version in the
 * driver, as follows :
 *
 * ETM_ARCH_VERSION[7:0], where :
 *      Bits[7:4] - Major version
 *      Bits[3:0] - Minro version
 */
#define ETM_ARCH_VERSION(major, minor)		\
	((((major) & 0xfU) << 4) | (((minor) & 0xfU)))
#define ETM_ARCH_MAJOR_VERSION(arch)	(((arch) >> 4) & 0xfU)
#define ETM_ARCH_MINOR_VERSION(arch)	((arch) & 0xfU)

#define ETM_ARCH_V4	ETM_ARCH_VERSION(4, 0)
#define ETM_ARCH_ETE	ETM_ARCH_VERSION(5, 0)

/* Interpretation of resource numbers change at ETM v4.3 architecture */
#define ETM_ARCH_V4_3	ETM_ARCH_VERSION(4, 3)

static inline u8 etm_devarch_to_arch(u32 devarch)
{
	return ETM_ARCH_VERSION(ETM_DEVARCH_ARCHID_ARCH_VER(devarch),
				ETM_DEVARCH_REVISION(devarch));
}

enum etm_impdef_type {
	ETM4_IMPDEF_HISI_CORE_COMMIT,
	ETM4_IMPDEF_FEATURE_MAX,
};

/**
 * struct etmv4_config - configuration information related to an ETMv4
 * @mode:	Controls various modes supported by this ETM.
 * @pe_sel:	Controls which PE to trace.
 * @cfg:	Controls the tracing options.
 * @eventctrl0: Controls the tracing of arbitrary events.
 * @eventctrl1: Controls the behavior of the events that @event_ctrl0 selects.
 * @stallctl:	If functionality that prevents trace unit buffer overflows
 *		is available.
 * @ts_ctrl:	Controls the insertion of global timestamps in the
 *		trace streams.
 * @syncfreq:	Controls how often trace synchronization requests occur.
 *		the TRCCCCTLR register.
 * @ccctlr:	Sets the threshold value for cycle counting.
 * @vinst_ctrl:	Controls instruction trace filtering.
 * @viiectlr:	Set or read, the address range comparators.
 * @vissctlr:	Set, or read, the single address comparators that control the
 *		ViewInst start-stop logic.
 * @vipcssctlr:	Set, or read, which PE comparator inputs can control the
 *		ViewInst start-stop logic.
 * @seq_idx:	Sequencor index selector.
 * @seq_ctrl:	Control for the sequencer state transition control register.
 * @seq_rst:	Moves the sequencer to state 0 when a programmed event occurs.
 * @seq_state:	Set, or read the sequencer state.
 * @cntr_idx:	Counter index seletor.
 * @cntrldvr:	Sets or returns the reload count value for a counter.
 * @cntr_ctrl:	Controls the operation of a counter.
 * @cntr_val:	Sets or returns the value for a counter.
 * @res_idx:	Resource index selector.
 * @res_ctrl:	Controls the selection of the resources in the trace unit.
 * @ss_idx:	Single-shot index selector.
 * @ss_ctrl:	Controls the corresponding single-shot comparator resource.
 * @ss_status:	The status of the corresponding single-shot comparator.
 * @ss_pe_cmp:	Selects the PE comparator inputs for Single-shot control.
 * @addr_idx:	Address comparator index selector.
 * @addr_val:	Value for address comparator.
 * @addr_acc:	Address comparator access type.
 * @addr_type:	Current status of the comparator register.
 * @ctxid_idx:	Context ID index selector.
 * @ctxid_pid:	Value of the context ID comparator.
 * @ctxid_mask0:Context ID comparator mask for comparator 0-3.
 * @ctxid_mask1:Context ID comparator mask for comparator 4-7.
 * @vmid_idx:	VM ID index selector.
 * @vmid_val:	Value of the VM ID comparator.
 * @vmid_mask0:	VM ID comparator mask for comparator 0-3.
 * @vmid_mask1:	VM ID comparator mask for comparator 4-7.
 * @ext_inp:	External input selection.
 * @s_ex_level: Secure ELs where tracing is supported.
 */
struct etmv4_config {
	u32				mode;
	u32				pe_sel;
	u32				cfg;
	u32				eventctrl0;
	u32				eventctrl1;
	u32				stall_ctrl;
	u32				ts_ctrl;
	u32				syncfreq;
	u32				ccctlr;
	u32				bb_ctrl;
	u32				vinst_ctrl;
	u32				viiectlr;
	u32				vissctlr;
	u32				vipcssctlr;
	u8				seq_idx;
	u32				seq_ctrl[ETM_MAX_SEQ_STATES];
	u32				seq_rst;
	u32				seq_state;
	u8				cntr_idx;
	u32				cntrldvr[ETMv4_MAX_CNTR];
	u32				cntr_ctrl[ETMv4_MAX_CNTR];
	u32				cntr_val[ETMv4_MAX_CNTR];
	u8				res_idx;
	u32				res_ctrl[ETM_MAX_RES_SEL];
	u8				ss_idx;
	u32				ss_ctrl[ETM_MAX_SS_CMP];
	u32				ss_status[ETM_MAX_SS_CMP];
	u32				ss_pe_cmp[ETM_MAX_SS_CMP];
	u8				addr_idx;
	u64				addr_val[ETM_MAX_SINGLE_ADDR_CMP];
	u64				addr_acc[ETM_MAX_SINGLE_ADDR_CMP];
	u8				addr_type[ETM_MAX_SINGLE_ADDR_CMP];
	u8				ctxid_idx;
	u64				ctxid_pid[ETMv4_MAX_CTXID_CMP];
	u32				ctxid_mask0;
	u32				ctxid_mask1;
	u8				vmid_idx;
	u64				vmid_val[ETM_MAX_VMID_CMP];
	u32				vmid_mask0;
	u32				vmid_mask1;
	u32				ext_inp;
	u8				s_ex_level;
};

/**
 * struct etm4_save_state - state to be preserved when ETM is without power
 */
struct etmv4_save_state {
	u32	trcprgctlr;
	u32	trcprocselr;
	u32	trcconfigr;
	u32	trcauxctlr;
	u32	trceventctl0r;
	u32	trceventctl1r;
	u32	trcstallctlr;
	u32	trctsctlr;
	u32	trcsyncpr;
	u32	trcccctlr;
	u32	trcbbctlr;
	u32	trctraceidr;
	u32	trcqctlr;

	u32	trcvictlr;
	u32	trcviiectlr;
	u32	trcvissctlr;
	u32	trcvipcssctlr;
	u32	trcvdctlr;
	u32	trcvdsacctlr;
	u32	trcvdarcctlr;

	u32	trcseqevr[ETM_MAX_SEQ_STATES];
	u32	trcseqrstevr;
	u32	trcseqstr;
	u32	trcextinselr;
	u32	trccntrldvr[ETMv4_MAX_CNTR];
	u32	trccntctlr[ETMv4_MAX_CNTR];
	u32	trccntvr[ETMv4_MAX_CNTR];

	u32	trcrsctlr[ETM_MAX_RES_SEL];

	u32	trcssccr[ETM_MAX_SS_CMP];
	u32	trcsscsr[ETM_MAX_SS_CMP];
	u32	trcsspcicr[ETM_MAX_SS_CMP];

	u64	trcacvr[ETM_MAX_SINGLE_ADDR_CMP];
	u64	trcacatr[ETM_MAX_SINGLE_ADDR_CMP];
	u64	trccidcvr[ETMv4_MAX_CTXID_CMP];
	u64	trcvmidcvr[ETM_MAX_VMID_CMP];
	u32	trccidcctlr0;
	u32	trccidcctlr1;
	u32	trcvmidcctlr0;
	u32	trcvmidcctlr1;

	u32	trcclaimset;

	u32	cntr_val[ETMv4_MAX_CNTR];
	u32	seq_state;
	u32	vinst_ctrl;
	u32	ss_status[ETM_MAX_SS_CMP];

	u32	trcpdcr;
};

/**
 * struct etm4_drvdata - specifics associated to an ETM component
 * @base:       Memory mapped base address for this component.
 * @csdev:      Component vitals needed by the framework.
 * @spinlock:   Only one at a time pls.
 * @mode:	This tracer's mode, i.e sysFS, Perf or disabled.
 * @cpu:        The cpu this component is affined to.
 * @arch:       ETM architecture version.
 * @nr_pe:	The number of processing entity available for tracing.
 * @nr_pe_cmp:	The number of processing entity comparator inputs that are
 *		available for tracing.
 * @nr_addr_cmp:Number of pairs of address comparators available
 *		as found in ETMIDR4 0-3.
 * @nr_cntr:    Number of counters as found in ETMIDR5 bit 28-30.
 * @nr_ext_inp: Number of external input.
 * @numcidc:	Number of contextID comparators.
 * @numvmidc:	Number of VMID comparators.
 * @nrseqstate: The number of sequencer states that are implemented.
 * @nr_event:	Indicates how many events the trace unit support.
 * @nr_resource:The number of resource selection pairs available for tracing.
 * @nr_ss_cmp:	Number of single-shot comparator controls that are available.
 * @trcid:	value of the current ID for this component.
 * @trcid_size: Indicates the trace ID width.
 * @ts_size:	Global timestamp size field.
 * @ctxid_size:	Size of the context ID field to consider.
 * @vmid_size:	Size of the VM ID comparator to consider.
 * @ccsize:	Indicates the size of the cycle counter in bits.
 * @ccitmin:	minimum value that can be programmed in
 * @s_ex_level:	In secure state, indicates whether instruction tracing is
 *		supported for the corresponding Exception level.
 * @ns_ex_level:In non-secure state, indicates whether instruction tracing is
 *		supported for the corresponding Exception level.
 * @sticky_enable: true if ETM base configuration has been done.
 * @boot_enable:True if we should start tracing at boot time.
 * @os_unlock:  True if access to management registers is allowed.
 * @instrp0:	Tracing of load and store instructions
 *		as P0 elements is supported.
 * @trcbb:	Indicates if the trace unit supports branch broadcast tracing.
 * @trccond:	If the trace unit supports conditional
 *		instruction tracing.
 * @retstack:	Indicates if the implementation supports a return stack.
 * @trccci:	Indicates if the trace unit supports cycle counting
 *		for instruction.
 * @q_support:	Q element support characteristics.
 * @trc_error:	Whether a trace unit can trace a system
 *		error exception.
 * @syncpr:	Indicates if an implementation has a fixed
 *		synchronization period.
 * @stall_ctrl:	Enables trace unit functionality that prevents trace
 *		unit buffer overflows.
 * @sysstall:	Does the system support stall control of the PE?
 * @nooverflow:	Indicate if overflow prevention is supported.
 * @atbtrig:	If the implementation can support ATB triggers
 * @lpoverride:	If the implementation can support low-power state over.
 * @trfcr:	If the CPU supports FEAT_TRF, value of the TRFCR_ELx that
 *		allows tracing at all ELs. We don't want to compute this
 *		at runtime, due to the additional setting of TRFCR_CX when
 *		in EL2. Otherwise, 0.
 * @config:	structure holding configuration parameters.
 * @save_trfcr:	Saved TRFCR_EL1 register during a CPU PM event.
 * @save_state:	State to be preserved across power loss
 * @state_needs_restore: True when there is context to restore after PM exit
 * @skip_power_up: Indicates if an implementation can skip powering up
 *		   the trace unit.
 * @arch_features: Bitmap of arch features of etmv4 devices.
 */
struct etmv4_drvdata {
	void __iomem			*base;
	struct coresight_device		*csdev;
	spinlock_t			spinlock;
	local_t				mode;
	int				cpu;
	u8				arch;
	u8				nr_pe;
	u8				nr_pe_cmp;
	u8				nr_addr_cmp;
	u8				nr_cntr;
	u8				nr_ext_inp;
	u8				numcidc;
	u8				numvmidc;
	u8				nrseqstate;
	u8				nr_event;
	u8				nr_resource;
	u8				nr_ss_cmp;
	u8				trcid;
	u8				trcid_size;
	u8				ts_size;
	u8				ctxid_size;
	u8				vmid_size;
	u8				ccsize;
	u8				ccitmin;
	u8				s_ex_level;
	u8				ns_ex_level;
	u8				q_support;
	u8				os_lock_model;
	bool				sticky_enable;
	bool				boot_enable;
	bool				os_unlock;
	bool				instrp0;
	bool				trcbb;
	bool				trccond;
	bool				retstack;
	bool				trccci;
	bool				trc_error;
	bool				syncpr;
	bool				stallctl;
	bool				sysstall;
	bool				nooverflow;
	bool				atbtrig;
	bool				lpoverride;
	u64				trfcr;
	struct etmv4_config		config;
	u64				save_trfcr;
	struct etmv4_save_state		*save_state;
	bool				state_needs_restore;
	bool				skip_power_up;
	DECLARE_BITMAP(arch_features, ETM4_IMPDEF_FEATURE_MAX);
};

/* Address comparator access types */
enum etm_addr_acctype {
	TRCACATRn_TYPE_ADDR,
	TRCACATRn_TYPE_DATA_LOAD_ADDR,
	TRCACATRn_TYPE_DATA_STORE_ADDR,
	TRCACATRn_TYPE_DATA_LOAD_STORE_ADDR,
};

/* Address comparator context types */
enum etm_addr_ctxtype {
	ETM_CTX_NONE,
	ETM_CTX_CTXID,
	ETM_CTX_VMID,
	ETM_CTX_CTXID_VMID,
};

extern const struct attribute_group *coresight_etmv4_groups[];
void etm4_config_trace_mode(struct etmv4_config *config);

u64 etm4x_sysreg_read(u32 offset, bool _relaxed, bool _64bit);
void etm4x_sysreg_write(u64 val, u32 offset, bool _relaxed, bool _64bit);

static inline bool etm4x_is_ete(struct etmv4_drvdata *drvdata)
{
	return drvdata->arch >= ETM_ARCH_ETE;
}

int etm4_read_alloc_trace_id(struct etmv4_drvdata *drvdata);
void etm4_release_trace_id(struct etmv4_drvdata *drvdata);
#endif
