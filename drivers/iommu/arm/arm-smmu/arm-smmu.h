/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IOMMU API for ARM architected SMMU implementations.
 *
 * Copyright (C) 2013 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#ifndef _ARM_SMMU_H
#define _ARM_SMMU_H

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/irqreturn.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/* Configuration registers */
#define ARM_SMMU_GR0_sCR0		0x0
#define ARM_SMMU_sCR0_VMID16EN		BIT(31)
#define ARM_SMMU_sCR0_BSU		GENMASK(15, 14)
#define ARM_SMMU_sCR0_FB		BIT(13)
#define ARM_SMMU_sCR0_PTM		BIT(12)
#define ARM_SMMU_sCR0_VMIDPNE		BIT(11)
#define ARM_SMMU_sCR0_USFCFG		BIT(10)
#define ARM_SMMU_sCR0_GCFGFIE		BIT(5)
#define ARM_SMMU_sCR0_GCFGFRE		BIT(4)
#define ARM_SMMU_sCR0_EXIDENABLE	BIT(3)
#define ARM_SMMU_sCR0_GFIE		BIT(2)
#define ARM_SMMU_sCR0_GFRE		BIT(1)
#define ARM_SMMU_sCR0_CLIENTPD		BIT(0)

/* Auxiliary Configuration register */
#define ARM_SMMU_GR0_sACR		0x10

/* Identification registers */
#define ARM_SMMU_GR0_ID0		0x20
#define ARM_SMMU_ID0_S1TS		BIT(30)
#define ARM_SMMU_ID0_S2TS		BIT(29)
#define ARM_SMMU_ID0_NTS		BIT(28)
#define ARM_SMMU_ID0_SMS		BIT(27)
#define ARM_SMMU_ID0_ATOSNS		BIT(26)
#define ARM_SMMU_ID0_PTFS_NO_AARCH32	BIT(25)
#define ARM_SMMU_ID0_PTFS_NO_AARCH32S	BIT(24)
#define ARM_SMMU_ID0_NUMIRPT		GENMASK(23, 16)
#define ARM_SMMU_ID0_CTTW		BIT(14)
#define ARM_SMMU_ID0_NUMSIDB		GENMASK(12, 9)
#define ARM_SMMU_ID0_EXIDS		BIT(8)
#define ARM_SMMU_ID0_NUMSMRG		GENMASK(7, 0)

#define ARM_SMMU_GR0_ID1		0x24
#define ARM_SMMU_ID1_PAGESIZE		BIT(31)
#define ARM_SMMU_ID1_NUMPAGENDXB	GENMASK(30, 28)
#define ARM_SMMU_ID1_NUMS2CB		GENMASK(23, 16)
#define ARM_SMMU_ID1_NUMCB		GENMASK(7, 0)

#define ARM_SMMU_GR0_ID2		0x28
#define ARM_SMMU_ID2_VMID16		BIT(15)
#define ARM_SMMU_ID2_PTFS_64K		BIT(14)
#define ARM_SMMU_ID2_PTFS_16K		BIT(13)
#define ARM_SMMU_ID2_PTFS_4K		BIT(12)
#define ARM_SMMU_ID2_UBS		GENMASK(11, 8)
#define ARM_SMMU_ID2_OAS		GENMASK(7, 4)
#define ARM_SMMU_ID2_IAS		GENMASK(3, 0)

#define ARM_SMMU_GR0_ID3		0x2c
#define ARM_SMMU_GR0_ID4		0x30
#define ARM_SMMU_GR0_ID5		0x34
#define ARM_SMMU_GR0_ID6		0x38

#define ARM_SMMU_GR0_ID7		0x3c
#define ARM_SMMU_ID7_MAJOR		GENMASK(7, 4)
#define ARM_SMMU_ID7_MINOR		GENMASK(3, 0)

#define ARM_SMMU_GR0_sGFSR		0x48
#define ARM_SMMU_sGFSR_USF		BIT(1)

#define ARM_SMMU_GR0_sGFSYNR0		0x50
#define ARM_SMMU_GR0_sGFSYNR1		0x54
#define ARM_SMMU_GR0_sGFSYNR2		0x58

/* Global TLB invalidation */
#define ARM_SMMU_GR0_TLBIVMID		0x64
#define ARM_SMMU_GR0_TLBIALLNSNH	0x68
#define ARM_SMMU_GR0_TLBIALLH		0x6c
#define ARM_SMMU_GR0_sTLBGSYNC		0x70

#define ARM_SMMU_GR0_sTLBGSTATUS	0x74
#define ARM_SMMU_sTLBGSTATUS_GSACTIVE	BIT(0)

/* Stream mapping registers */
#define ARM_SMMU_GR0_SMR(n)		(0x800 + ((n) << 2))
#define ARM_SMMU_SMR_VALID		BIT(31)
#define ARM_SMMU_SMR_MASK		GENMASK(31, 16)
#define ARM_SMMU_SMR_ID			GENMASK(15, 0)

#define ARM_SMMU_GR0_S2CR(n)		(0xc00 + ((n) << 2))
#define ARM_SMMU_S2CR_PRIVCFG		GENMASK(25, 24)
enum arm_smmu_s2cr_privcfg {
	S2CR_PRIVCFG_DEFAULT,
	S2CR_PRIVCFG_DIPAN,
	S2CR_PRIVCFG_UNPRIV,
	S2CR_PRIVCFG_PRIV,
};
#define ARM_SMMU_S2CR_TYPE		GENMASK(17, 16)
enum arm_smmu_s2cr_type {
	S2CR_TYPE_TRANS,
	S2CR_TYPE_BYPASS,
	S2CR_TYPE_FAULT,
};
#define ARM_SMMU_S2CR_EXIDVALID		BIT(10)
#define ARM_SMMU_S2CR_CBNDX		GENMASK(7, 0)

/* Context bank attribute registers */
#define ARM_SMMU_GR1_CBAR(n)		(0x0 + ((n) << 2))
#define ARM_SMMU_CBAR_IRPTNDX		GENMASK(31, 24)
#define ARM_SMMU_CBAR_TYPE		GENMASK(17, 16)
enum arm_smmu_cbar_type {
	CBAR_TYPE_S2_TRANS,
	CBAR_TYPE_S1_TRANS_S2_BYPASS,
	CBAR_TYPE_S1_TRANS_S2_FAULT,
	CBAR_TYPE_S1_TRANS_S2_TRANS,
};
#define ARM_SMMU_CBAR_S1_MEMATTR	GENMASK(15, 12)
#define ARM_SMMU_CBAR_S1_MEMATTR_WB	0xf
#define ARM_SMMU_CBAR_S1_BPSHCFG	GENMASK(9, 8)
#define ARM_SMMU_CBAR_S1_BPSHCFG_NSH	3
#define ARM_SMMU_CBAR_VMID		GENMASK(7, 0)

#define ARM_SMMU_GR1_CBFRSYNRA(n)	(0x400 + ((n) << 2))

#define ARM_SMMU_GR1_CBA2R(n)		(0x800 + ((n) << 2))
#define ARM_SMMU_CBA2R_VMID16		GENMASK(31, 16)
#define ARM_SMMU_CBA2R_VA64		BIT(0)

#define ARM_SMMU_CB_SCTLR		0x0
#define ARM_SMMU_SCTLR_S1_ASIDPNE	BIT(12)
#define ARM_SMMU_SCTLR_CFCFG		BIT(7)
#define ARM_SMMU_SCTLR_HUPCF		BIT(8)
#define ARM_SMMU_SCTLR_CFIE		BIT(6)
#define ARM_SMMU_SCTLR_CFRE		BIT(5)
#define ARM_SMMU_SCTLR_E		BIT(4)
#define ARM_SMMU_SCTLR_AFE		BIT(2)
#define ARM_SMMU_SCTLR_TRE		BIT(1)
#define ARM_SMMU_SCTLR_M		BIT(0)

#define ARM_SMMU_CB_ACTLR		0x4

#define ARM_SMMU_CB_RESUME		0x8
#define ARM_SMMU_RESUME_TERMINATE	BIT(0)

#define ARM_SMMU_CB_TCR2		0x10
#define ARM_SMMU_TCR2_SEP		GENMASK(17, 15)
#define ARM_SMMU_TCR2_SEP_UPSTREAM	0x7
#define ARM_SMMU_TCR2_AS		BIT(4)
#define ARM_SMMU_TCR2_PASIZE		GENMASK(3, 0)

#define ARM_SMMU_CB_TTBR0		0x20
#define ARM_SMMU_CB_TTBR1		0x28
#define ARM_SMMU_TTBRn_ASID		GENMASK_ULL(63, 48)

#define ARM_SMMU_CB_TCR			0x30
#define ARM_SMMU_TCR_EAE		BIT(31)
#define ARM_SMMU_TCR_EPD1		BIT(23)
#define ARM_SMMU_TCR_A1			BIT(22)
#define ARM_SMMU_TCR_TG0		GENMASK(15, 14)
#define ARM_SMMU_TCR_SH0		GENMASK(13, 12)
#define ARM_SMMU_TCR_ORGN0		GENMASK(11, 10)
#define ARM_SMMU_TCR_IRGN0		GENMASK(9, 8)
#define ARM_SMMU_TCR_EPD0		BIT(7)
#define ARM_SMMU_TCR_T0SZ		GENMASK(5, 0)

#define ARM_SMMU_VTCR_RES1		BIT(31)
#define ARM_SMMU_VTCR_PS		GENMASK(18, 16)
#define ARM_SMMU_VTCR_TG0		ARM_SMMU_TCR_TG0
#define ARM_SMMU_VTCR_SH0		ARM_SMMU_TCR_SH0
#define ARM_SMMU_VTCR_ORGN0		ARM_SMMU_TCR_ORGN0
#define ARM_SMMU_VTCR_IRGN0		ARM_SMMU_TCR_IRGN0
#define ARM_SMMU_VTCR_SL0		GENMASK(7, 6)
#define ARM_SMMU_VTCR_T0SZ		ARM_SMMU_TCR_T0SZ

#define ARM_SMMU_CB_CONTEXTIDR		0x34
#define ARM_SMMU_CB_S1_MAIR0		0x38
#define ARM_SMMU_CB_S1_MAIR1		0x3c

#define ARM_SMMU_CB_PAR			0x50
#define ARM_SMMU_CB_PAR_F		BIT(0)

#define ARM_SMMU_CB_FSR			0x58
#define ARM_SMMU_FSR_MULTI		BIT(31)
#define ARM_SMMU_FSR_SS			BIT(30)
#define ARM_SMMU_FSR_UUT		BIT(8)
#define ARM_SMMU_FSR_ASF		BIT(7)
#define ARM_SMMU_FSR_TLBLKF		BIT(6)
#define ARM_SMMU_FSR_TLBMCF		BIT(5)
#define ARM_SMMU_FSR_EF			BIT(4)
#define ARM_SMMU_FSR_PF			BIT(3)
#define ARM_SMMU_FSR_AFF		BIT(2)
#define ARM_SMMU_FSR_TF			BIT(1)

#define ARM_SMMU_FSR_IGN		(ARM_SMMU_FSR_AFF |		\
					 ARM_SMMU_FSR_ASF |		\
					 ARM_SMMU_FSR_TLBMCF |		\
					 ARM_SMMU_FSR_TLBLKF)

#define ARM_SMMU_FSR_FAULT		(ARM_SMMU_FSR_MULTI |		\
					 ARM_SMMU_FSR_SS |		\
					 ARM_SMMU_FSR_UUT |		\
					 ARM_SMMU_FSR_EF |		\
					 ARM_SMMU_FSR_PF |		\
					 ARM_SMMU_FSR_TF |		\
					 ARM_SMMU_FSR_IGN)

#define ARM_SMMU_CB_FAR			0x60

#define ARM_SMMU_CB_FSYNR0		0x68
#define ARM_SMMU_FSYNR0_WNR		BIT(4)

#define ARM_SMMU_CB_FSYNR1		0x6c

#define ARM_SMMU_CB_S1_TLBIVA		0x600
#define ARM_SMMU_CB_S1_TLBIASID		0x610
#define ARM_SMMU_CB_S1_TLBIVAL		0x620
#define ARM_SMMU_CB_S2_TLBIIPAS2	0x630
#define ARM_SMMU_CB_S2_TLBIIPAS2L	0x638
#define ARM_SMMU_CB_TLBSYNC		0x7f0
#define ARM_SMMU_CB_TLBSTATUS		0x7f4
#define ARM_SMMU_CB_ATS1PR		0x800

#define ARM_SMMU_CB_ATSR		0x8f0
#define ARM_SMMU_ATSR_ACTIVE		BIT(0)


/* Maximum number of context banks per SMMU */
#define ARM_SMMU_MAX_CBS		128

#define TLB_LOOP_TIMEOUT		1000000	/* 1s! */
#define TLB_SPIN_COUNT			10

/* Shared driver definitions */
enum arm_smmu_arch_version {
	ARM_SMMU_V1,
	ARM_SMMU_V1_64K,
	ARM_SMMU_V2,
};

enum arm_smmu_implementation {
	GENERIC_SMMU,
	ARM_MMU500,
	CAVIUM_SMMUV2,
	QCOM_SMMUV2,
};

struct arm_smmu_s2cr {
	struct iommu_group		*group;
	int				count;
	enum arm_smmu_s2cr_type		type;
	enum arm_smmu_s2cr_privcfg	privcfg;
	u8				cbndx;
};

struct arm_smmu_smr {
	u16				mask;
	u16				id;
	bool				valid;
	bool				pinned;
};

struct arm_smmu_device {
	struct device			*dev;

	void __iomem			*base;
	phys_addr_t			ioaddr;
	unsigned int			numpage;
	unsigned int			pgshift;

#define ARM_SMMU_FEAT_COHERENT_WALK	(1 << 0)
#define ARM_SMMU_FEAT_STREAM_MATCH	(1 << 1)
#define ARM_SMMU_FEAT_TRANS_S1		(1 << 2)
#define ARM_SMMU_FEAT_TRANS_S2		(1 << 3)
#define ARM_SMMU_FEAT_TRANS_NESTED	(1 << 4)
#define ARM_SMMU_FEAT_TRANS_OPS		(1 << 5)
#define ARM_SMMU_FEAT_VMID16		(1 << 6)
#define ARM_SMMU_FEAT_FMT_AARCH64_4K	(1 << 7)
#define ARM_SMMU_FEAT_FMT_AARCH64_16K	(1 << 8)
#define ARM_SMMU_FEAT_FMT_AARCH64_64K	(1 << 9)
#define ARM_SMMU_FEAT_FMT_AARCH32_L	(1 << 10)
#define ARM_SMMU_FEAT_FMT_AARCH32_S	(1 << 11)
#define ARM_SMMU_FEAT_EXIDS		(1 << 12)
	u32				features;

	enum arm_smmu_arch_version	version;
	enum arm_smmu_implementation	model;
	const struct arm_smmu_impl	*impl;

	u32				num_context_banks;
	u32				num_s2_context_banks;
	DECLARE_BITMAP(context_map, ARM_SMMU_MAX_CBS);
	struct arm_smmu_cb		*cbs;
	atomic_t			irptndx;

	u32				num_mapping_groups;
	u16				streamid_mask;
	u16				smr_mask_mask;
	struct arm_smmu_smr		*smrs;
	struct arm_smmu_s2cr		*s2crs;
	struct mutex			stream_map_mutex;

	unsigned long			va_size;
	unsigned long			ipa_size;
	unsigned long			pa_size;
	unsigned long			pgsize_bitmap;

	int				num_context_irqs;
	int				num_clks;
	unsigned int			*irqs;
	struct clk_bulk_data		*clks;

	spinlock_t			global_sync_lock;

	/* IOMMU core code handle */
	struct iommu_device		iommu;
};

enum arm_smmu_context_fmt {
	ARM_SMMU_CTX_FMT_NONE,
	ARM_SMMU_CTX_FMT_AARCH64,
	ARM_SMMU_CTX_FMT_AARCH32_L,
	ARM_SMMU_CTX_FMT_AARCH32_S,
};

struct arm_smmu_cfg {
	u8				cbndx;
	u8				irptndx;
	union {
		u16			asid;
		u16			vmid;
	};
	enum arm_smmu_cbar_type		cbar;
	enum arm_smmu_context_fmt	fmt;
	bool				flush_walk_prefer_tlbiasid;
};
#define ARM_SMMU_INVALID_IRPTNDX	0xff

struct arm_smmu_cb {
	u64				ttbr[2];
	u32				tcr[2];
	u32				mair[2];
	struct arm_smmu_cfg		*cfg;
};

enum arm_smmu_domain_stage {
	ARM_SMMU_DOMAIN_S1 = 0,
	ARM_SMMU_DOMAIN_S2,
	ARM_SMMU_DOMAIN_NESTED,
	ARM_SMMU_DOMAIN_BYPASS,
};

struct arm_smmu_domain {
	struct arm_smmu_device		*smmu;
	struct io_pgtable_ops		*pgtbl_ops;
	unsigned long			pgtbl_quirks;
	const struct iommu_flush_ops	*flush_ops;
	struct arm_smmu_cfg		cfg;
	enum arm_smmu_domain_stage	stage;
	struct mutex			init_mutex; /* Protects smmu pointer */
	spinlock_t			cb_lock; /* Serialises ATS1* ops and TLB syncs */
	struct iommu_domain		domain;
};

struct arm_smmu_master_cfg {
	struct arm_smmu_device		*smmu;
	s16				smendx[];
};

static inline u32 arm_smmu_lpae_tcr(const struct io_pgtable_cfg *cfg)
{
	u32 tcr = FIELD_PREP(ARM_SMMU_TCR_TG0, cfg->arm_lpae_s1_cfg.tcr.tg) |
		FIELD_PREP(ARM_SMMU_TCR_SH0, cfg->arm_lpae_s1_cfg.tcr.sh) |
		FIELD_PREP(ARM_SMMU_TCR_ORGN0, cfg->arm_lpae_s1_cfg.tcr.orgn) |
		FIELD_PREP(ARM_SMMU_TCR_IRGN0, cfg->arm_lpae_s1_cfg.tcr.irgn) |
		FIELD_PREP(ARM_SMMU_TCR_T0SZ, cfg->arm_lpae_s1_cfg.tcr.tsz);

       /*
	* When TTBR1 is selected shift the TCR fields by 16 bits and disable
	* translation in TTBR0
	*/
	if (cfg->quirks & IO_PGTABLE_QUIRK_ARM_TTBR1) {
		tcr = (tcr << 16) & ~ARM_SMMU_TCR_A1;
		tcr |= ARM_SMMU_TCR_EPD0;
	} else
		tcr |= ARM_SMMU_TCR_EPD1;

	return tcr;
}

static inline u32 arm_smmu_lpae_tcr2(const struct io_pgtable_cfg *cfg)
{
	return FIELD_PREP(ARM_SMMU_TCR2_PASIZE, cfg->arm_lpae_s1_cfg.tcr.ips) |
	       FIELD_PREP(ARM_SMMU_TCR2_SEP, ARM_SMMU_TCR2_SEP_UPSTREAM);
}

static inline u32 arm_smmu_lpae_vtcr(const struct io_pgtable_cfg *cfg)
{
	return ARM_SMMU_VTCR_RES1 |
	       FIELD_PREP(ARM_SMMU_VTCR_PS, cfg->arm_lpae_s2_cfg.vtcr.ps) |
	       FIELD_PREP(ARM_SMMU_VTCR_TG0, cfg->arm_lpae_s2_cfg.vtcr.tg) |
	       FIELD_PREP(ARM_SMMU_VTCR_SH0, cfg->arm_lpae_s2_cfg.vtcr.sh) |
	       FIELD_PREP(ARM_SMMU_VTCR_ORGN0, cfg->arm_lpae_s2_cfg.vtcr.orgn) |
	       FIELD_PREP(ARM_SMMU_VTCR_IRGN0, cfg->arm_lpae_s2_cfg.vtcr.irgn) |
	       FIELD_PREP(ARM_SMMU_VTCR_SL0, cfg->arm_lpae_s2_cfg.vtcr.sl) |
	       FIELD_PREP(ARM_SMMU_VTCR_T0SZ, cfg->arm_lpae_s2_cfg.vtcr.tsz);
}

/* Implementation details, yay! */
struct arm_smmu_impl {
	u32 (*read_reg)(struct arm_smmu_device *smmu, int page, int offset);
	void (*write_reg)(struct arm_smmu_device *smmu, int page, int offset,
			  u32 val);
	u64 (*read_reg64)(struct arm_smmu_device *smmu, int page, int offset);
	void (*write_reg64)(struct arm_smmu_device *smmu, int page, int offset,
			    u64 val);
	int (*cfg_probe)(struct arm_smmu_device *smmu);
	int (*reset)(struct arm_smmu_device *smmu);
	int (*init_context)(struct arm_smmu_domain *smmu_domain,
			struct io_pgtable_cfg *cfg, struct device *dev);
	void (*tlb_sync)(struct arm_smmu_device *smmu, int page, int sync,
			 int status);
	int (*def_domain_type)(struct device *dev);
	irqreturn_t (*global_fault)(int irq, void *dev);
	irqreturn_t (*context_fault)(int irq, void *dev);
	int (*alloc_context_bank)(struct arm_smmu_domain *smmu_domain,
				  struct arm_smmu_device *smmu,
				  struct device *dev, int start);
	void (*write_s2cr)(struct arm_smmu_device *smmu, int idx);
	void (*write_sctlr)(struct arm_smmu_device *smmu, int idx, u32 reg);
	void (*probe_finalize)(struct arm_smmu_device *smmu, struct device *dev);
};

#define INVALID_SMENDX			-1
#define cfg_smendx(cfg, fw, i) \
	(i >= fw->num_ids ? INVALID_SMENDX : cfg->smendx[i])
#define for_each_cfg_sme(cfg, fw, i, idx) \
	for (i = 0; idx = cfg_smendx(cfg, fw, i), i < fw->num_ids; ++i)

static inline int __arm_smmu_alloc_bitmap(unsigned long *map, int start, int end)
{
	int idx;

	do {
		idx = find_next_zero_bit(map, end, start);
		if (idx == end)
			return -ENOSPC;
	} while (test_and_set_bit(idx, map));

	return idx;
}

static inline void __iomem *arm_smmu_page(struct arm_smmu_device *smmu, int n)
{
	return smmu->base + (n << smmu->pgshift);
}

static inline u32 arm_smmu_readl(struct arm_smmu_device *smmu, int page, int offset)
{
	if (smmu->impl && unlikely(smmu->impl->read_reg))
		return smmu->impl->read_reg(smmu, page, offset);
	return readl_relaxed(arm_smmu_page(smmu, page) + offset);
}

static inline void arm_smmu_writel(struct arm_smmu_device *smmu, int page,
				   int offset, u32 val)
{
	if (smmu->impl && unlikely(smmu->impl->write_reg))
		smmu->impl->write_reg(smmu, page, offset, val);
	else
		writel_relaxed(val, arm_smmu_page(smmu, page) + offset);
}

static inline u64 arm_smmu_readq(struct arm_smmu_device *smmu, int page, int offset)
{
	if (smmu->impl && unlikely(smmu->impl->read_reg64))
		return smmu->impl->read_reg64(smmu, page, offset);
	return readq_relaxed(arm_smmu_page(smmu, page) + offset);
}

static inline void arm_smmu_writeq(struct arm_smmu_device *smmu, int page,
				   int offset, u64 val)
{
	if (smmu->impl && unlikely(smmu->impl->write_reg64))
		smmu->impl->write_reg64(smmu, page, offset, val);
	else
		writeq_relaxed(val, arm_smmu_page(smmu, page) + offset);
}

#define ARM_SMMU_GR0		0
#define ARM_SMMU_GR1		1
#define ARM_SMMU_CB(s, n)	((s)->numpage + (n))

#define arm_smmu_gr0_read(s, o)		\
	arm_smmu_readl((s), ARM_SMMU_GR0, (o))
#define arm_smmu_gr0_write(s, o, v)	\
	arm_smmu_writel((s), ARM_SMMU_GR0, (o), (v))

#define arm_smmu_gr1_read(s, o)		\
	arm_smmu_readl((s), ARM_SMMU_GR1, (o))
#define arm_smmu_gr1_write(s, o, v)	\
	arm_smmu_writel((s), ARM_SMMU_GR1, (o), (v))

#define arm_smmu_cb_read(s, n, o)	\
	arm_smmu_readl((s), ARM_SMMU_CB((s), (n)), (o))
#define arm_smmu_cb_write(s, n, o, v)	\
	arm_smmu_writel((s), ARM_SMMU_CB((s), (n)), (o), (v))
#define arm_smmu_cb_readq(s, n, o)	\
	arm_smmu_readq((s), ARM_SMMU_CB((s), (n)), (o))
#define arm_smmu_cb_writeq(s, n, o, v)	\
	arm_smmu_writeq((s), ARM_SMMU_CB((s), (n)), (o), (v))

struct arm_smmu_device *arm_smmu_impl_init(struct arm_smmu_device *smmu);
struct arm_smmu_device *nvidia_smmu_impl_init(struct arm_smmu_device *smmu);
struct arm_smmu_device *qcom_smmu_impl_init(struct arm_smmu_device *smmu);

void arm_smmu_write_context_bank(struct arm_smmu_device *smmu, int idx);
int arm_mmu500_reset(struct arm_smmu_device *smmu);

#endif /* _ARM_SMMU_H */
