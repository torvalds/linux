/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IOMMU API for ARM architected SMMU implementations.
 *
 * Copyright (C) 2013 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#ifndef _ARM_SMMU_REGS_H
#define _ARM_SMMU_REGS_H

#include <linux/bits.h>

/* Configuration registers */
#define ARM_SMMU_GR0_sCR0		0x0
#define sCR0_VMID16EN			BIT(31)
#define sCR0_BSU			GENMASK(15, 14)
#define sCR0_FB				BIT(13)
#define sCR0_PTM			BIT(12)
#define sCR0_VMIDPNE			BIT(11)
#define sCR0_USFCFG			BIT(10)
#define sCR0_GCFGFIE			BIT(5)
#define sCR0_GCFGFRE			BIT(4)
#define sCR0_EXIDENABLE			BIT(3)
#define sCR0_GFIE			BIT(2)
#define sCR0_GFRE			BIT(1)
#define sCR0_CLIENTPD			BIT(0)

/* Auxiliary Configuration register */
#define ARM_SMMU_GR0_sACR		0x10

/* Identification registers */
#define ARM_SMMU_GR0_ID0		0x20
#define ID0_S1TS			BIT(30)
#define ID0_S2TS			BIT(29)
#define ID0_NTS				BIT(28)
#define ID0_SMS				BIT(27)
#define ID0_ATOSNS			BIT(26)
#define ID0_PTFS_NO_AARCH32		BIT(25)
#define ID0_PTFS_NO_AARCH32S		BIT(24)
#define ID0_NUMIRPT			GENMASK(23, 16)
#define ID0_CTTW			BIT(14)
#define ID0_NUMSIDB			GENMASK(12, 9)
#define ID0_EXIDS			BIT(8)
#define ID0_NUMSMRG			GENMASK(7, 0)

#define ARM_SMMU_GR0_ID1		0x24
#define ID1_PAGESIZE			BIT(31)
#define ID1_NUMPAGENDXB			GENMASK(30, 28)
#define ID1_NUMS2CB			GENMASK(23, 16)
#define ID1_NUMCB			GENMASK(7, 0)

#define ARM_SMMU_GR0_ID2		0x28
#define ID2_VMID16			BIT(15)
#define ID2_PTFS_64K			BIT(14)
#define ID2_PTFS_16K			BIT(13)
#define ID2_PTFS_4K			BIT(12)
#define ID2_UBS				GENMASK(11, 8)
#define ID2_OAS				GENMASK(7, 4)
#define ID2_IAS				GENMASK(3, 0)

#define ARM_SMMU_GR0_ID3		0x2c
#define ARM_SMMU_GR0_ID4		0x30
#define ARM_SMMU_GR0_ID5		0x34
#define ARM_SMMU_GR0_ID6		0x38

#define ARM_SMMU_GR0_ID7		0x3c
#define ID7_MAJOR			GENMASK(7, 4)
#define ID7_MINOR			GENMASK(3, 0)

#define ARM_SMMU_GR0_sGFSR		0x48
#define ARM_SMMU_GR0_sGFSYNR0		0x50
#define ARM_SMMU_GR0_sGFSYNR1		0x54
#define ARM_SMMU_GR0_sGFSYNR2		0x58

/* Global TLB invalidation */
#define ARM_SMMU_GR0_TLBIVMID		0x64
#define ARM_SMMU_GR0_TLBIALLNSNH	0x68
#define ARM_SMMU_GR0_TLBIALLH		0x6c
#define ARM_SMMU_GR0_sTLBGSYNC		0x70

#define ARM_SMMU_GR0_sTLBGSTATUS	0x74
#define sTLBGSTATUS_GSACTIVE		BIT(0)

/* Stream mapping registers */
#define ARM_SMMU_GR0_SMR(n)		(0x800 + ((n) << 2))
#define SMR_VALID			BIT(31)
#define SMR_MASK			GENMASK(31, 16)
#define SMR_ID				GENMASK(15, 0)

#define ARM_SMMU_GR0_S2CR(n)		(0xc00 + ((n) << 2))
#define S2CR_PRIVCFG			GENMASK(25, 24)
enum arm_smmu_s2cr_privcfg {
	S2CR_PRIVCFG_DEFAULT,
	S2CR_PRIVCFG_DIPAN,
	S2CR_PRIVCFG_UNPRIV,
	S2CR_PRIVCFG_PRIV,
};
#define S2CR_TYPE			GENMASK(17, 16)
enum arm_smmu_s2cr_type {
	S2CR_TYPE_TRANS,
	S2CR_TYPE_BYPASS,
	S2CR_TYPE_FAULT,
};
#define S2CR_EXIDVALID			BIT(10)
#define S2CR_CBNDX			GENMASK(7, 0)

/* Context bank attribute registers */
#define ARM_SMMU_GR1_CBAR(n)		(0x0 + ((n) << 2))
#define CBAR_IRPTNDX			GENMASK(31, 24)
#define CBAR_TYPE			GENMASK(17, 16)
enum arm_smmu_cbar_type {
	CBAR_TYPE_S2_TRANS,
	CBAR_TYPE_S1_TRANS_S2_BYPASS,
	CBAR_TYPE_S1_TRANS_S2_FAULT,
	CBAR_TYPE_S1_TRANS_S2_TRANS,
};
#define CBAR_S1_MEMATTR			GENMASK(15, 12)
#define CBAR_S1_MEMATTR_WB		0xf
#define CBAR_S1_BPSHCFG			GENMASK(9, 8)
#define CBAR_S1_BPSHCFG_NSH		3
#define CBAR_VMID			GENMASK(7, 0)

#define ARM_SMMU_GR1_CBFRSYNRA(n)	(0x400 + ((n) << 2))

#define ARM_SMMU_GR1_CBA2R(n)		(0x800 + ((n) << 2))
#define CBA2R_VMID16			GENMASK(31, 16)
#define CBA2R_VA64			BIT(0)

#define ARM_SMMU_CB_SCTLR		0x0
#define SCTLR_S1_ASIDPNE		BIT(12)
#define SCTLR_CFCFG			BIT(7)
#define SCTLR_CFIE			BIT(6)
#define SCTLR_CFRE			BIT(5)
#define SCTLR_E				BIT(4)
#define SCTLR_AFE			BIT(2)
#define SCTLR_TRE			BIT(1)
#define SCTLR_M				BIT(0)

#define ARM_SMMU_CB_ACTLR		0x4

#define ARM_SMMU_CB_RESUME		0x8
#define RESUME_TERMINATE		BIT(0)

#define ARM_SMMU_CB_TCR2		0x10
#define TCR2_SEP			GENMASK(17, 15)
#define TCR2_SEP_UPSTREAM		0x7
#define TCR2_AS				BIT(4)

#define ARM_SMMU_CB_TTBR0		0x20
#define ARM_SMMU_CB_TTBR1		0x28
#define TTBRn_ASID			GENMASK_ULL(63, 48)

#define ARM_SMMU_CB_TCR			0x30
#define ARM_SMMU_CB_CONTEXTIDR		0x34
#define ARM_SMMU_CB_S1_MAIR0		0x38
#define ARM_SMMU_CB_S1_MAIR1		0x3c

#define ARM_SMMU_CB_PAR			0x50
#define CB_PAR_F			BIT(0)

#define ARM_SMMU_CB_FSR			0x58
#define FSR_MULTI			BIT(31)
#define FSR_SS				BIT(30)
#define FSR_UUT				BIT(8)
#define FSR_ASF				BIT(7)
#define FSR_TLBLKF			BIT(6)
#define FSR_TLBMCF			BIT(5)
#define FSR_EF				BIT(4)
#define FSR_PF				BIT(3)
#define FSR_AFF				BIT(2)
#define FSR_TF				BIT(1)

#define FSR_IGN				(FSR_AFF | FSR_ASF | \
					 FSR_TLBMCF | FSR_TLBLKF)
#define FSR_FAULT			(FSR_MULTI | FSR_SS | FSR_UUT | \
					 FSR_EF | FSR_PF | FSR_TF | FSR_IGN)

#define ARM_SMMU_CB_FAR			0x60

#define ARM_SMMU_CB_FSYNR0		0x68
#define FSYNR0_WNR			BIT(4)

#define ARM_SMMU_CB_S1_TLBIVA		0x600
#define ARM_SMMU_CB_S1_TLBIASID		0x610
#define ARM_SMMU_CB_S1_TLBIVAL		0x620
#define ARM_SMMU_CB_S2_TLBIIPAS2	0x630
#define ARM_SMMU_CB_S2_TLBIIPAS2L	0x638
#define ARM_SMMU_CB_TLBSYNC		0x7f0
#define ARM_SMMU_CB_TLBSTATUS		0x7f4
#define ARM_SMMU_CB_ATS1PR		0x800

#define ARM_SMMU_CB_ATSR		0x8f0
#define ATSR_ACTIVE			BIT(0)

#endif /* _ARM_SMMU_REGS_H */
