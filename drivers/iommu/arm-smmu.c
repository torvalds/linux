/*
 * IOMMU API for ARM architected SMMU implementations.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2013 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 *
 * This driver currently supports:
 *	- SMMUv1 and v2 implementations
 *	- Stream-matching and stream-indexing
 *	- v7/v8 long-descriptor format
 *	- Non-secure access to the SMMU
 *	- Context fault reporting
 *	- Extended Stream ID (16 bit)
 */

#define pr_fmt(fmt) "arm-smmu: " fmt

#include <linux/acpi.h>
#include <linux/acpi_iort.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/dma-iommu.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_iommu.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <linux/amba/bus.h>

#include "io-pgtable.h"

/* Maximum number of context banks per SMMU */
#define ARM_SMMU_MAX_CBS		128

/* SMMU global address space */
#define ARM_SMMU_GR0(smmu)		((smmu)->base)
#define ARM_SMMU_GR1(smmu)		((smmu)->base + (1 << (smmu)->pgshift))

/*
 * SMMU global address space with conditional offset to access secure
 * aliases of non-secure registers (e.g. nsCR0: 0x400, nsGFSR: 0x448,
 * nsGFSYNR0: 0x450)
 */
#define ARM_SMMU_GR0_NS(smmu)						\
	((smmu)->base +							\
		((smmu->options & ARM_SMMU_OPT_SECURE_CFG_ACCESS)	\
			? 0x400 : 0))

/*
 * Some 64-bit registers only make sense to write atomically, but in such
 * cases all the data relevant to AArch32 formats lies within the lower word,
 * therefore this actually makes more sense than it might first appear.
 */
#ifdef CONFIG_64BIT
#define smmu_write_atomic_lq		writeq_relaxed
#else
#define smmu_write_atomic_lq		writel_relaxed
#endif

/* Configuration registers */
#define ARM_SMMU_GR0_sCR0		0x0
#define sCR0_CLIENTPD			(1 << 0)
#define sCR0_GFRE			(1 << 1)
#define sCR0_GFIE			(1 << 2)
#define sCR0_EXIDENABLE			(1 << 3)
#define sCR0_GCFGFRE			(1 << 4)
#define sCR0_GCFGFIE			(1 << 5)
#define sCR0_USFCFG			(1 << 10)
#define sCR0_VMIDPNE			(1 << 11)
#define sCR0_PTM			(1 << 12)
#define sCR0_FB				(1 << 13)
#define sCR0_VMID16EN			(1 << 31)
#define sCR0_BSU_SHIFT			14
#define sCR0_BSU_MASK			0x3

/* Auxiliary Configuration register */
#define ARM_SMMU_GR0_sACR		0x10

/* Identification registers */
#define ARM_SMMU_GR0_ID0		0x20
#define ARM_SMMU_GR0_ID1		0x24
#define ARM_SMMU_GR0_ID2		0x28
#define ARM_SMMU_GR0_ID3		0x2c
#define ARM_SMMU_GR0_ID4		0x30
#define ARM_SMMU_GR0_ID5		0x34
#define ARM_SMMU_GR0_ID6		0x38
#define ARM_SMMU_GR0_ID7		0x3c
#define ARM_SMMU_GR0_sGFSR		0x48
#define ARM_SMMU_GR0_sGFSYNR0		0x50
#define ARM_SMMU_GR0_sGFSYNR1		0x54
#define ARM_SMMU_GR0_sGFSYNR2		0x58

#define ID0_S1TS			(1 << 30)
#define ID0_S2TS			(1 << 29)
#define ID0_NTS				(1 << 28)
#define ID0_SMS				(1 << 27)
#define ID0_ATOSNS			(1 << 26)
#define ID0_PTFS_NO_AARCH32		(1 << 25)
#define ID0_PTFS_NO_AARCH32S		(1 << 24)
#define ID0_CTTW			(1 << 14)
#define ID0_NUMIRPT_SHIFT		16
#define ID0_NUMIRPT_MASK		0xff
#define ID0_NUMSIDB_SHIFT		9
#define ID0_NUMSIDB_MASK		0xf
#define ID0_EXIDS			(1 << 8)
#define ID0_NUMSMRG_SHIFT		0
#define ID0_NUMSMRG_MASK		0xff

#define ID1_PAGESIZE			(1 << 31)
#define ID1_NUMPAGENDXB_SHIFT		28
#define ID1_NUMPAGENDXB_MASK		7
#define ID1_NUMS2CB_SHIFT		16
#define ID1_NUMS2CB_MASK		0xff
#define ID1_NUMCB_SHIFT			0
#define ID1_NUMCB_MASK			0xff

#define ID2_OAS_SHIFT			4
#define ID2_OAS_MASK			0xf
#define ID2_IAS_SHIFT			0
#define ID2_IAS_MASK			0xf
#define ID2_UBS_SHIFT			8
#define ID2_UBS_MASK			0xf
#define ID2_PTFS_4K			(1 << 12)
#define ID2_PTFS_16K			(1 << 13)
#define ID2_PTFS_64K			(1 << 14)
#define ID2_VMID16			(1 << 15)

#define ID7_MAJOR_SHIFT			4
#define ID7_MAJOR_MASK			0xf

/* Global TLB invalidation */
#define ARM_SMMU_GR0_TLBIVMID		0x64
#define ARM_SMMU_GR0_TLBIALLNSNH	0x68
#define ARM_SMMU_GR0_TLBIALLH		0x6c
#define ARM_SMMU_GR0_sTLBGSYNC		0x70
#define ARM_SMMU_GR0_sTLBGSTATUS	0x74
#define sTLBGSTATUS_GSACTIVE		(1 << 0)
#define TLB_LOOP_TIMEOUT		1000000	/* 1s! */
#define TLB_SPIN_COUNT			10

/* Stream mapping registers */
#define ARM_SMMU_GR0_SMR(n)		(0x800 + ((n) << 2))
#define SMR_VALID			(1 << 31)
#define SMR_MASK_SHIFT			16
#define SMR_ID_SHIFT			0

#define ARM_SMMU_GR0_S2CR(n)		(0xc00 + ((n) << 2))
#define S2CR_CBNDX_SHIFT		0
#define S2CR_CBNDX_MASK			0xff
#define S2CR_EXIDVALID			(1 << 10)
#define S2CR_TYPE_SHIFT			16
#define S2CR_TYPE_MASK			0x3
enum arm_smmu_s2cr_type {
	S2CR_TYPE_TRANS,
	S2CR_TYPE_BYPASS,
	S2CR_TYPE_FAULT,
};

#define S2CR_PRIVCFG_SHIFT		24
#define S2CR_PRIVCFG_MASK		0x3
enum arm_smmu_s2cr_privcfg {
	S2CR_PRIVCFG_DEFAULT,
	S2CR_PRIVCFG_DIPAN,
	S2CR_PRIVCFG_UNPRIV,
	S2CR_PRIVCFG_PRIV,
};

/* Context bank attribute registers */
#define ARM_SMMU_GR1_CBAR(n)		(0x0 + ((n) << 2))
#define CBAR_VMID_SHIFT			0
#define CBAR_VMID_MASK			0xff
#define CBAR_S1_BPSHCFG_SHIFT		8
#define CBAR_S1_BPSHCFG_MASK		3
#define CBAR_S1_BPSHCFG_NSH		3
#define CBAR_S1_MEMATTR_SHIFT		12
#define CBAR_S1_MEMATTR_MASK		0xf
#define CBAR_S1_MEMATTR_WB		0xf
#define CBAR_TYPE_SHIFT			16
#define CBAR_TYPE_MASK			0x3
#define CBAR_TYPE_S2_TRANS		(0 << CBAR_TYPE_SHIFT)
#define CBAR_TYPE_S1_TRANS_S2_BYPASS	(1 << CBAR_TYPE_SHIFT)
#define CBAR_TYPE_S1_TRANS_S2_FAULT	(2 << CBAR_TYPE_SHIFT)
#define CBAR_TYPE_S1_TRANS_S2_TRANS	(3 << CBAR_TYPE_SHIFT)
#define CBAR_IRPTNDX_SHIFT		24
#define CBAR_IRPTNDX_MASK		0xff

#define ARM_SMMU_GR1_CBA2R(n)		(0x800 + ((n) << 2))
#define CBA2R_RW64_32BIT		(0 << 0)
#define CBA2R_RW64_64BIT		(1 << 0)
#define CBA2R_VMID_SHIFT		16
#define CBA2R_VMID_MASK			0xffff

/* Translation context bank */
#define ARM_SMMU_CB(smmu, n)	((smmu)->cb_base + ((n) << (smmu)->pgshift))

#define ARM_SMMU_CB_SCTLR		0x0
#define ARM_SMMU_CB_ACTLR		0x4
#define ARM_SMMU_CB_RESUME		0x8
#define ARM_SMMU_CB_TTBCR2		0x10
#define ARM_SMMU_CB_TTBR0		0x20
#define ARM_SMMU_CB_TTBR1		0x28
#define ARM_SMMU_CB_TTBCR		0x30
#define ARM_SMMU_CB_CONTEXTIDR		0x34
#define ARM_SMMU_CB_S1_MAIR0		0x38
#define ARM_SMMU_CB_S1_MAIR1		0x3c
#define ARM_SMMU_CB_PAR			0x50
#define ARM_SMMU_CB_FSR			0x58
#define ARM_SMMU_CB_FAR			0x60
#define ARM_SMMU_CB_FSYNR0		0x68
#define ARM_SMMU_CB_S1_TLBIVA		0x600
#define ARM_SMMU_CB_S1_TLBIASID		0x610
#define ARM_SMMU_CB_S1_TLBIVAL		0x620
#define ARM_SMMU_CB_S2_TLBIIPAS2	0x630
#define ARM_SMMU_CB_S2_TLBIIPAS2L	0x638
#define ARM_SMMU_CB_TLBSYNC		0x7f0
#define ARM_SMMU_CB_TLBSTATUS		0x7f4
#define ARM_SMMU_CB_ATS1PR		0x800
#define ARM_SMMU_CB_ATSR		0x8f0

#define SCTLR_S1_ASIDPNE		(1 << 12)
#define SCTLR_CFCFG			(1 << 7)
#define SCTLR_CFIE			(1 << 6)
#define SCTLR_CFRE			(1 << 5)
#define SCTLR_E				(1 << 4)
#define SCTLR_AFE			(1 << 2)
#define SCTLR_TRE			(1 << 1)
#define SCTLR_M				(1 << 0)

#define ARM_MMU500_ACTLR_CPRE		(1 << 1)

#define ARM_MMU500_ACR_CACHE_LOCK	(1 << 26)
#define ARM_MMU500_ACR_SMTNMB_TLBEN	(1 << 8)

#define CB_PAR_F			(1 << 0)

#define ATSR_ACTIVE			(1 << 0)

#define RESUME_RETRY			(0 << 0)
#define RESUME_TERMINATE		(1 << 0)

#define TTBCR2_SEP_SHIFT		15
#define TTBCR2_SEP_UPSTREAM		(0x7 << TTBCR2_SEP_SHIFT)
#define TTBCR2_AS			(1 << 4)

#define TTBRn_ASID_SHIFT		48

#define FSR_MULTI			(1 << 31)
#define FSR_SS				(1 << 30)
#define FSR_UUT				(1 << 8)
#define FSR_ASF				(1 << 7)
#define FSR_TLBLKF			(1 << 6)
#define FSR_TLBMCF			(1 << 5)
#define FSR_EF				(1 << 4)
#define FSR_PF				(1 << 3)
#define FSR_AFF				(1 << 2)
#define FSR_TF				(1 << 1)

#define FSR_IGN				(FSR_AFF | FSR_ASF | \
					 FSR_TLBMCF | FSR_TLBLKF)
#define FSR_FAULT			(FSR_MULTI | FSR_SS | FSR_UUT | \
					 FSR_EF | FSR_PF | FSR_TF | FSR_IGN)

#define FSYNR0_WNR			(1 << 4)

#define MSI_IOVA_BASE			0x8000000
#define MSI_IOVA_LENGTH			0x100000

static int force_stage;
module_param(force_stage, int, S_IRUGO);
MODULE_PARM_DESC(force_stage,
	"Force SMMU mappings to be installed at a particular stage of translation. A value of '1' or '2' forces the corresponding stage. All other values are ignored (i.e. no stage is forced). Note that selecting a specific stage will disable support for nested translation.");
static bool disable_bypass;
module_param(disable_bypass, bool, S_IRUGO);
MODULE_PARM_DESC(disable_bypass,
	"Disable bypass streams such that incoming transactions from devices that are not attached to an iommu domain will report an abort back to the device and will not be allowed to pass through the SMMU.");

enum arm_smmu_arch_version {
	ARM_SMMU_V1,
	ARM_SMMU_V1_64K,
	ARM_SMMU_V2,
};

enum arm_smmu_implementation {
	GENERIC_SMMU,
	ARM_MMU500,
	CAVIUM_SMMUV2,
};

/* Until ACPICA headers cover IORT rev. C */
#ifndef ACPI_IORT_SMMU_CORELINK_MMU401
#define ACPI_IORT_SMMU_CORELINK_MMU401	0x4
#endif
#ifndef ACPI_IORT_SMMU_CAVIUM_THUNDERX
#define ACPI_IORT_SMMU_CAVIUM_THUNDERX	0x5
#endif

struct arm_smmu_s2cr {
	struct iommu_group		*group;
	int				count;
	enum arm_smmu_s2cr_type		type;
	enum arm_smmu_s2cr_privcfg	privcfg;
	u8				cbndx;
};

#define s2cr_init_val (struct arm_smmu_s2cr){				\
	.type = disable_bypass ? S2CR_TYPE_FAULT : S2CR_TYPE_BYPASS,	\
}

struct arm_smmu_smr {
	u16				mask;
	u16				id;
	bool				valid;
};

struct arm_smmu_master_cfg {
	struct arm_smmu_device		*smmu;
	s16				smendx[];
};
#define INVALID_SMENDX			-1
#define __fwspec_cfg(fw) ((struct arm_smmu_master_cfg *)fw->iommu_priv)
#define fwspec_smmu(fw)  (__fwspec_cfg(fw)->smmu)
#define fwspec_smendx(fw, i) \
	(i >= fw->num_ids ? INVALID_SMENDX : __fwspec_cfg(fw)->smendx[i])
#define for_each_cfg_sme(fw, i, idx) \
	for (i = 0; idx = fwspec_smendx(fw, i), i < fw->num_ids; ++i)

struct arm_smmu_device {
	struct device			*dev;

	void __iomem			*base;
	void __iomem			*cb_base;
	unsigned long			pgshift;

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

#define ARM_SMMU_OPT_SECURE_CFG_ACCESS (1 << 0)
	u32				options;
	enum arm_smmu_arch_version	version;
	enum arm_smmu_implementation	model;

	u32				num_context_banks;
	u32				num_s2_context_banks;
	DECLARE_BITMAP(context_map, ARM_SMMU_MAX_CBS);
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

	u32				num_global_irqs;
	u32				num_context_irqs;
	unsigned int			*irqs;

	u32				cavium_id_base; /* Specific to Cavium */

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
	u32				cbar;
	enum arm_smmu_context_fmt	fmt;
};
#define INVALID_IRPTNDX			0xff

enum arm_smmu_domain_stage {
	ARM_SMMU_DOMAIN_S1 = 0,
	ARM_SMMU_DOMAIN_S2,
	ARM_SMMU_DOMAIN_NESTED,
	ARM_SMMU_DOMAIN_BYPASS,
};

struct arm_smmu_domain {
	struct arm_smmu_device		*smmu;
	struct io_pgtable_ops		*pgtbl_ops;
	struct arm_smmu_cfg		cfg;
	enum arm_smmu_domain_stage	stage;
	struct mutex			init_mutex; /* Protects smmu pointer */
	spinlock_t			cb_lock; /* Serialises ATS1* ops */
	struct iommu_domain		domain;
};

struct arm_smmu_option_prop {
	u32 opt;
	const char *prop;
};

static atomic_t cavium_smmu_context_count = ATOMIC_INIT(0);

static bool using_legacy_binding, using_generic_binding;

static struct arm_smmu_option_prop arm_smmu_options[] = {
	{ ARM_SMMU_OPT_SECURE_CFG_ACCESS, "calxeda,smmu-secure-config-access" },
	{ 0, NULL},
};

static struct arm_smmu_domain *to_smmu_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct arm_smmu_domain, domain);
}

static void parse_driver_options(struct arm_smmu_device *smmu)
{
	int i = 0;

	do {
		if (of_property_read_bool(smmu->dev->of_node,
						arm_smmu_options[i].prop)) {
			smmu->options |= arm_smmu_options[i].opt;
			dev_notice(smmu->dev, "option %s\n",
				arm_smmu_options[i].prop);
		}
	} while (arm_smmu_options[++i].opt);
}

static struct device_node *dev_get_dev_node(struct device *dev)
{
	if (dev_is_pci(dev)) {
		struct pci_bus *bus = to_pci_dev(dev)->bus;

		while (!pci_is_root_bus(bus))
			bus = bus->parent;
		return of_node_get(bus->bridge->parent->of_node);
	}

	return of_node_get(dev->of_node);
}

static int __arm_smmu_get_pci_sid(struct pci_dev *pdev, u16 alias, void *data)
{
	*((__be32 *)data) = cpu_to_be32(alias);
	return 0; /* Continue walking */
}

static int __find_legacy_master_phandle(struct device *dev, void *data)
{
	struct of_phandle_iterator *it = *(void **)data;
	struct device_node *np = it->node;
	int err;

	of_for_each_phandle(it, err, dev->of_node, "mmu-masters",
			    "#stream-id-cells", 0)
		if (it->node == np) {
			*(void **)data = dev;
			return 1;
		}
	it->node = np;
	return err == -ENOENT ? 0 : err;
}

static struct platform_driver arm_smmu_driver;
static struct iommu_ops arm_smmu_ops;

static int arm_smmu_register_legacy_master(struct device *dev,
					   struct arm_smmu_device **smmu)
{
	struct device *smmu_dev;
	struct device_node *np;
	struct of_phandle_iterator it;
	void *data = &it;
	u32 *sids;
	__be32 pci_sid;
	int err;

	np = dev_get_dev_node(dev);
	if (!np || !of_find_property(np, "#stream-id-cells", NULL)) {
		of_node_put(np);
		return -ENODEV;
	}

	it.node = np;
	err = driver_for_each_device(&arm_smmu_driver.driver, NULL, &data,
				     __find_legacy_master_phandle);
	smmu_dev = data;
	of_node_put(np);
	if (err == 0)
		return -ENODEV;
	if (err < 0)
		return err;

	if (dev_is_pci(dev)) {
		/* "mmu-masters" assumes Stream ID == Requester ID */
		pci_for_each_dma_alias(to_pci_dev(dev), __arm_smmu_get_pci_sid,
				       &pci_sid);
		it.cur = &pci_sid;
		it.cur_count = 1;
	}

	err = iommu_fwspec_init(dev, &smmu_dev->of_node->fwnode,
				&arm_smmu_ops);
	if (err)
		return err;

	sids = kcalloc(it.cur_count, sizeof(*sids), GFP_KERNEL);
	if (!sids)
		return -ENOMEM;

	*smmu = dev_get_drvdata(smmu_dev);
	of_phandle_iterator_args(&it, sids, it.cur_count);
	err = iommu_fwspec_add_ids(dev, sids, it.cur_count);
	kfree(sids);
	return err;
}

static int __arm_smmu_alloc_bitmap(unsigned long *map, int start, int end)
{
	int idx;

	do {
		idx = find_next_zero_bit(map, end, start);
		if (idx == end)
			return -ENOSPC;
	} while (test_and_set_bit(idx, map));

	return idx;
}

static void __arm_smmu_free_bitmap(unsigned long *map, int idx)
{
	clear_bit(idx, map);
}

/* Wait for any pending TLB invalidations to complete */
static void __arm_smmu_tlb_sync(struct arm_smmu_device *smmu,
				void __iomem *sync, void __iomem *status)
{
	unsigned int spin_cnt, delay;

	writel_relaxed(0, sync);
	for (delay = 1; delay < TLB_LOOP_TIMEOUT; delay *= 2) {
		for (spin_cnt = TLB_SPIN_COUNT; spin_cnt > 0; spin_cnt--) {
			if (!(readl_relaxed(status) & sTLBGSTATUS_GSACTIVE))
				return;
			cpu_relax();
		}
		udelay(delay);
	}
	dev_err_ratelimited(smmu->dev,
			    "TLB sync timed out -- SMMU may be deadlocked\n");
}

static void arm_smmu_tlb_sync_global(struct arm_smmu_device *smmu)
{
	void __iomem *base = ARM_SMMU_GR0(smmu);

	__arm_smmu_tlb_sync(smmu, base + ARM_SMMU_GR0_sTLBGSYNC,
			    base + ARM_SMMU_GR0_sTLBGSTATUS);
}

static void arm_smmu_tlb_sync_context(void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	void __iomem *base = ARM_SMMU_CB(smmu, smmu_domain->cfg.cbndx);

	__arm_smmu_tlb_sync(smmu, base + ARM_SMMU_CB_TLBSYNC,
			    base + ARM_SMMU_CB_TLBSTATUS);
}

static void arm_smmu_tlb_sync_vmid(void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;

	arm_smmu_tlb_sync_global(smmu_domain->smmu);
}

static void arm_smmu_tlb_inv_context_s1(void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	void __iomem *base = ARM_SMMU_CB(smmu_domain->smmu, cfg->cbndx);

	writel_relaxed(cfg->asid, base + ARM_SMMU_CB_S1_TLBIASID);
	arm_smmu_tlb_sync_context(cookie);
}

static void arm_smmu_tlb_inv_context_s2(void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	void __iomem *base = ARM_SMMU_GR0(smmu);

	writel_relaxed(smmu_domain->cfg.vmid, base + ARM_SMMU_GR0_TLBIVMID);
	arm_smmu_tlb_sync_global(smmu);
}

static void arm_smmu_tlb_inv_range_nosync(unsigned long iova, size_t size,
					  size_t granule, bool leaf, void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	bool stage1 = cfg->cbar != CBAR_TYPE_S2_TRANS;
	void __iomem *reg = ARM_SMMU_CB(smmu_domain->smmu, cfg->cbndx);

	if (stage1) {
		reg += leaf ? ARM_SMMU_CB_S1_TLBIVAL : ARM_SMMU_CB_S1_TLBIVA;

		if (cfg->fmt != ARM_SMMU_CTX_FMT_AARCH64) {
			iova &= ~12UL;
			iova |= cfg->asid;
			do {
				writel_relaxed(iova, reg);
				iova += granule;
			} while (size -= granule);
		} else {
			iova >>= 12;
			iova |= (u64)cfg->asid << 48;
			do {
				writeq_relaxed(iova, reg);
				iova += granule >> 12;
			} while (size -= granule);
		}
	} else {
		reg += leaf ? ARM_SMMU_CB_S2_TLBIIPAS2L :
			      ARM_SMMU_CB_S2_TLBIIPAS2;
		iova >>= 12;
		do {
			smmu_write_atomic_lq(iova, reg);
			iova += granule >> 12;
		} while (size -= granule);
	}
}

/*
 * On MMU-401 at least, the cost of firing off multiple TLBIVMIDs appears
 * almost negligible, but the benefit of getting the first one in as far ahead
 * of the sync as possible is significant, hence we don't just make this a
 * no-op and set .tlb_sync to arm_smmu_inv_context_s2() as you might think.
 */
static void arm_smmu_tlb_inv_vmid_nosync(unsigned long iova, size_t size,
					 size_t granule, bool leaf, void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	void __iomem *base = ARM_SMMU_GR0(smmu_domain->smmu);

	writel_relaxed(smmu_domain->cfg.vmid, base + ARM_SMMU_GR0_TLBIVMID);
}

static const struct iommu_gather_ops arm_smmu_s1_tlb_ops = {
	.tlb_flush_all	= arm_smmu_tlb_inv_context_s1,
	.tlb_add_flush	= arm_smmu_tlb_inv_range_nosync,
	.tlb_sync	= arm_smmu_tlb_sync_context,
};

static const struct iommu_gather_ops arm_smmu_s2_tlb_ops_v2 = {
	.tlb_flush_all	= arm_smmu_tlb_inv_context_s2,
	.tlb_add_flush	= arm_smmu_tlb_inv_range_nosync,
	.tlb_sync	= arm_smmu_tlb_sync_context,
};

static const struct iommu_gather_ops arm_smmu_s2_tlb_ops_v1 = {
	.tlb_flush_all	= arm_smmu_tlb_inv_context_s2,
	.tlb_add_flush	= arm_smmu_tlb_inv_vmid_nosync,
	.tlb_sync	= arm_smmu_tlb_sync_vmid,
};

static irqreturn_t arm_smmu_context_fault(int irq, void *dev)
{
	u32 fsr, fsynr;
	unsigned long iova;
	struct iommu_domain *domain = dev;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	void __iomem *cb_base;

	cb_base = ARM_SMMU_CB(smmu, cfg->cbndx);
	fsr = readl_relaxed(cb_base + ARM_SMMU_CB_FSR);

	if (!(fsr & FSR_FAULT))
		return IRQ_NONE;

	fsynr = readl_relaxed(cb_base + ARM_SMMU_CB_FSYNR0);
	iova = readq_relaxed(cb_base + ARM_SMMU_CB_FAR);

	dev_err_ratelimited(smmu->dev,
	"Unhandled context fault: fsr=0x%x, iova=0x%08lx, fsynr=0x%x, cb=%d\n",
			    fsr, iova, fsynr, cfg->cbndx);

	writel(fsr, cb_base + ARM_SMMU_CB_FSR);
	return IRQ_HANDLED;
}

static irqreturn_t arm_smmu_global_fault(int irq, void *dev)
{
	u32 gfsr, gfsynr0, gfsynr1, gfsynr2;
	struct arm_smmu_device *smmu = dev;
	void __iomem *gr0_base = ARM_SMMU_GR0_NS(smmu);

	gfsr = readl_relaxed(gr0_base + ARM_SMMU_GR0_sGFSR);
	gfsynr0 = readl_relaxed(gr0_base + ARM_SMMU_GR0_sGFSYNR0);
	gfsynr1 = readl_relaxed(gr0_base + ARM_SMMU_GR0_sGFSYNR1);
	gfsynr2 = readl_relaxed(gr0_base + ARM_SMMU_GR0_sGFSYNR2);

	if (!gfsr)
		return IRQ_NONE;

	dev_err_ratelimited(smmu->dev,
		"Unexpected global fault, this could be serious\n");
	dev_err_ratelimited(smmu->dev,
		"\tGFSR 0x%08x, GFSYNR0 0x%08x, GFSYNR1 0x%08x, GFSYNR2 0x%08x\n",
		gfsr, gfsynr0, gfsynr1, gfsynr2);

	writel(gfsr, gr0_base + ARM_SMMU_GR0_sGFSR);
	return IRQ_HANDLED;
}

static void arm_smmu_init_context_bank(struct arm_smmu_domain *smmu_domain,
				       struct io_pgtable_cfg *pgtbl_cfg)
{
	u32 reg, reg2;
	u64 reg64;
	bool stage1;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	void __iomem *cb_base, *gr1_base;

	gr1_base = ARM_SMMU_GR1(smmu);
	stage1 = cfg->cbar != CBAR_TYPE_S2_TRANS;
	cb_base = ARM_SMMU_CB(smmu, cfg->cbndx);

	if (smmu->version > ARM_SMMU_V1) {
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64)
			reg = CBA2R_RW64_64BIT;
		else
			reg = CBA2R_RW64_32BIT;
		/* 16-bit VMIDs live in CBA2R */
		if (smmu->features & ARM_SMMU_FEAT_VMID16)
			reg |= cfg->vmid << CBA2R_VMID_SHIFT;

		writel_relaxed(reg, gr1_base + ARM_SMMU_GR1_CBA2R(cfg->cbndx));
	}

	/* CBAR */
	reg = cfg->cbar;
	if (smmu->version < ARM_SMMU_V2)
		reg |= cfg->irptndx << CBAR_IRPTNDX_SHIFT;

	/*
	 * Use the weakest shareability/memory types, so they are
	 * overridden by the ttbcr/pte.
	 */
	if (stage1) {
		reg |= (CBAR_S1_BPSHCFG_NSH << CBAR_S1_BPSHCFG_SHIFT) |
			(CBAR_S1_MEMATTR_WB << CBAR_S1_MEMATTR_SHIFT);
	} else if (!(smmu->features & ARM_SMMU_FEAT_VMID16)) {
		/* 8-bit VMIDs live in CBAR */
		reg |= cfg->vmid << CBAR_VMID_SHIFT;
	}
	writel_relaxed(reg, gr1_base + ARM_SMMU_GR1_CBAR(cfg->cbndx));

	/*
	 * TTBCR
	 * We must write this before the TTBRs, since it determines the
	 * access behaviour of some fields (in particular, ASID[15:8]).
	 */
	if (stage1) {
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH32_S) {
			reg = pgtbl_cfg->arm_v7s_cfg.tcr;
			reg2 = 0;
		} else {
			reg = pgtbl_cfg->arm_lpae_s1_cfg.tcr;
			reg2 = pgtbl_cfg->arm_lpae_s1_cfg.tcr >> 32;
			reg2 |= TTBCR2_SEP_UPSTREAM;
			if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64)
				reg2 |= TTBCR2_AS;
		}
		if (smmu->version > ARM_SMMU_V1)
			writel_relaxed(reg2, cb_base + ARM_SMMU_CB_TTBCR2);
	} else {
		reg = pgtbl_cfg->arm_lpae_s2_cfg.vtcr;
	}
	writel_relaxed(reg, cb_base + ARM_SMMU_CB_TTBCR);

	/* TTBRs */
	if (stage1) {
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH32_S) {
			reg = pgtbl_cfg->arm_v7s_cfg.ttbr[0];
			writel_relaxed(reg, cb_base + ARM_SMMU_CB_TTBR0);
			reg = pgtbl_cfg->arm_v7s_cfg.ttbr[1];
			writel_relaxed(reg, cb_base + ARM_SMMU_CB_TTBR1);
			writel_relaxed(cfg->asid, cb_base + ARM_SMMU_CB_CONTEXTIDR);
		} else {
			reg64 = pgtbl_cfg->arm_lpae_s1_cfg.ttbr[0];
			reg64 |= (u64)cfg->asid << TTBRn_ASID_SHIFT;
			writeq_relaxed(reg64, cb_base + ARM_SMMU_CB_TTBR0);
			reg64 = pgtbl_cfg->arm_lpae_s1_cfg.ttbr[1];
			reg64 |= (u64)cfg->asid << TTBRn_ASID_SHIFT;
			writeq_relaxed(reg64, cb_base + ARM_SMMU_CB_TTBR1);
		}
	} else {
		reg64 = pgtbl_cfg->arm_lpae_s2_cfg.vttbr;
		writeq_relaxed(reg64, cb_base + ARM_SMMU_CB_TTBR0);
	}

	/* MAIRs (stage-1 only) */
	if (stage1) {
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH32_S) {
			reg = pgtbl_cfg->arm_v7s_cfg.prrr;
			reg2 = pgtbl_cfg->arm_v7s_cfg.nmrr;
		} else {
			reg = pgtbl_cfg->arm_lpae_s1_cfg.mair[0];
			reg2 = pgtbl_cfg->arm_lpae_s1_cfg.mair[1];
		}
		writel_relaxed(reg, cb_base + ARM_SMMU_CB_S1_MAIR0);
		writel_relaxed(reg2, cb_base + ARM_SMMU_CB_S1_MAIR1);
	}

	/* SCTLR */
	reg = SCTLR_CFIE | SCTLR_CFRE | SCTLR_AFE | SCTLR_TRE | SCTLR_M;
	if (stage1)
		reg |= SCTLR_S1_ASIDPNE;
#ifdef __BIG_ENDIAN
	reg |= SCTLR_E;
#endif
	writel_relaxed(reg, cb_base + ARM_SMMU_CB_SCTLR);
}

static int arm_smmu_init_domain_context(struct iommu_domain *domain,
					struct arm_smmu_device *smmu)
{
	int irq, start, ret = 0;
	unsigned long ias, oas;
	struct io_pgtable_ops *pgtbl_ops;
	struct io_pgtable_cfg pgtbl_cfg;
	enum io_pgtable_fmt fmt;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	const struct iommu_gather_ops *tlb_ops;

	mutex_lock(&smmu_domain->init_mutex);
	if (smmu_domain->smmu)
		goto out_unlock;

	if (domain->type == IOMMU_DOMAIN_IDENTITY) {
		smmu_domain->stage = ARM_SMMU_DOMAIN_BYPASS;
		smmu_domain->smmu = smmu;
		goto out_unlock;
	}

	/*
	 * Mapping the requested stage onto what we support is surprisingly
	 * complicated, mainly because the spec allows S1+S2 SMMUs without
	 * support for nested translation. That means we end up with the
	 * following table:
	 *
	 * Requested        Supported        Actual
	 *     S1               N              S1
	 *     S1             S1+S2            S1
	 *     S1               S2             S2
	 *     S1               S1             S1
	 *     N                N              N
	 *     N              S1+S2            S2
	 *     N                S2             S2
	 *     N                S1             S1
	 *
	 * Note that you can't actually request stage-2 mappings.
	 */
	if (!(smmu->features & ARM_SMMU_FEAT_TRANS_S1))
		smmu_domain->stage = ARM_SMMU_DOMAIN_S2;
	if (!(smmu->features & ARM_SMMU_FEAT_TRANS_S2))
		smmu_domain->stage = ARM_SMMU_DOMAIN_S1;

	/*
	 * Choosing a suitable context format is even more fiddly. Until we
	 * grow some way for the caller to express a preference, and/or move
	 * the decision into the io-pgtable code where it arguably belongs,
	 * just aim for the closest thing to the rest of the system, and hope
	 * that the hardware isn't esoteric enough that we can't assume AArch64
	 * support to be a superset of AArch32 support...
	 */
	if (smmu->features & ARM_SMMU_FEAT_FMT_AARCH32_L)
		cfg->fmt = ARM_SMMU_CTX_FMT_AARCH32_L;
	if (IS_ENABLED(CONFIG_IOMMU_IO_PGTABLE_ARMV7S) &&
	    !IS_ENABLED(CONFIG_64BIT) && !IS_ENABLED(CONFIG_ARM_LPAE) &&
	    (smmu->features & ARM_SMMU_FEAT_FMT_AARCH32_S) &&
	    (smmu_domain->stage == ARM_SMMU_DOMAIN_S1))
		cfg->fmt = ARM_SMMU_CTX_FMT_AARCH32_S;
	if ((IS_ENABLED(CONFIG_64BIT) || cfg->fmt == ARM_SMMU_CTX_FMT_NONE) &&
	    (smmu->features & (ARM_SMMU_FEAT_FMT_AARCH64_64K |
			       ARM_SMMU_FEAT_FMT_AARCH64_16K |
			       ARM_SMMU_FEAT_FMT_AARCH64_4K)))
		cfg->fmt = ARM_SMMU_CTX_FMT_AARCH64;

	if (cfg->fmt == ARM_SMMU_CTX_FMT_NONE) {
		ret = -EINVAL;
		goto out_unlock;
	}

	switch (smmu_domain->stage) {
	case ARM_SMMU_DOMAIN_S1:
		cfg->cbar = CBAR_TYPE_S1_TRANS_S2_BYPASS;
		start = smmu->num_s2_context_banks;
		ias = smmu->va_size;
		oas = smmu->ipa_size;
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64) {
			fmt = ARM_64_LPAE_S1;
		} else if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH32_L) {
			fmt = ARM_32_LPAE_S1;
			ias = min(ias, 32UL);
			oas = min(oas, 40UL);
		} else {
			fmt = ARM_V7S;
			ias = min(ias, 32UL);
			oas = min(oas, 32UL);
		}
		tlb_ops = &arm_smmu_s1_tlb_ops;
		break;
	case ARM_SMMU_DOMAIN_NESTED:
		/*
		 * We will likely want to change this if/when KVM gets
		 * involved.
		 */
	case ARM_SMMU_DOMAIN_S2:
		cfg->cbar = CBAR_TYPE_S2_TRANS;
		start = 0;
		ias = smmu->ipa_size;
		oas = smmu->pa_size;
		if (cfg->fmt == ARM_SMMU_CTX_FMT_AARCH64) {
			fmt = ARM_64_LPAE_S2;
		} else {
			fmt = ARM_32_LPAE_S2;
			ias = min(ias, 40UL);
			oas = min(oas, 40UL);
		}
		if (smmu->version == ARM_SMMU_V2)
			tlb_ops = &arm_smmu_s2_tlb_ops_v2;
		else
			tlb_ops = &arm_smmu_s2_tlb_ops_v1;
		break;
	default:
		ret = -EINVAL;
		goto out_unlock;
	}
	ret = __arm_smmu_alloc_bitmap(smmu->context_map, start,
				      smmu->num_context_banks);
	if (ret < 0)
		goto out_unlock;

	cfg->cbndx = ret;
	if (smmu->version < ARM_SMMU_V2) {
		cfg->irptndx = atomic_inc_return(&smmu->irptndx);
		cfg->irptndx %= smmu->num_context_irqs;
	} else {
		cfg->irptndx = cfg->cbndx;
	}

	if (smmu_domain->stage == ARM_SMMU_DOMAIN_S2)
		cfg->vmid = cfg->cbndx + 1 + smmu->cavium_id_base;
	else
		cfg->asid = cfg->cbndx + smmu->cavium_id_base;

	pgtbl_cfg = (struct io_pgtable_cfg) {
		.pgsize_bitmap	= smmu->pgsize_bitmap,
		.ias		= ias,
		.oas		= oas,
		.tlb		= tlb_ops,
		.iommu_dev	= smmu->dev,
	};

	if (smmu->features & ARM_SMMU_FEAT_COHERENT_WALK)
		pgtbl_cfg.quirks = IO_PGTABLE_QUIRK_NO_DMA;

	smmu_domain->smmu = smmu;
	pgtbl_ops = alloc_io_pgtable_ops(fmt, &pgtbl_cfg, smmu_domain);
	if (!pgtbl_ops) {
		ret = -ENOMEM;
		goto out_clear_smmu;
	}

	/* Update the domain's page sizes to reflect the page table format */
	domain->pgsize_bitmap = pgtbl_cfg.pgsize_bitmap;
	domain->geometry.aperture_end = (1UL << ias) - 1;
	domain->geometry.force_aperture = true;

	/* Initialise the context bank with our page table cfg */
	arm_smmu_init_context_bank(smmu_domain, &pgtbl_cfg);

	/*
	 * Request context fault interrupt. Do this last to avoid the
	 * handler seeing a half-initialised domain state.
	 */
	irq = smmu->irqs[smmu->num_global_irqs + cfg->irptndx];
	ret = devm_request_irq(smmu->dev, irq, arm_smmu_context_fault,
			       IRQF_SHARED, "arm-smmu-context-fault", domain);
	if (ret < 0) {
		dev_err(smmu->dev, "failed to request context IRQ %d (%u)\n",
			cfg->irptndx, irq);
		cfg->irptndx = INVALID_IRPTNDX;
	}

	mutex_unlock(&smmu_domain->init_mutex);

	/* Publish page table ops for map/unmap */
	smmu_domain->pgtbl_ops = pgtbl_ops;
	return 0;

out_clear_smmu:
	smmu_domain->smmu = NULL;
out_unlock:
	mutex_unlock(&smmu_domain->init_mutex);
	return ret;
}

static void arm_smmu_destroy_domain_context(struct iommu_domain *domain)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	void __iomem *cb_base;
	int irq;

	if (!smmu || domain->type == IOMMU_DOMAIN_IDENTITY)
		return;

	/*
	 * Disable the context bank and free the page tables before freeing
	 * it.
	 */
	cb_base = ARM_SMMU_CB(smmu, cfg->cbndx);
	writel_relaxed(0, cb_base + ARM_SMMU_CB_SCTLR);

	if (cfg->irptndx != INVALID_IRPTNDX) {
		irq = smmu->irqs[smmu->num_global_irqs + cfg->irptndx];
		devm_free_irq(smmu->dev, irq, domain);
	}

	free_io_pgtable_ops(smmu_domain->pgtbl_ops);
	__arm_smmu_free_bitmap(smmu->context_map, cfg->cbndx);
}

static struct iommu_domain *arm_smmu_domain_alloc(unsigned type)
{
	struct arm_smmu_domain *smmu_domain;

	if (type != IOMMU_DOMAIN_UNMANAGED &&
	    type != IOMMU_DOMAIN_DMA &&
	    type != IOMMU_DOMAIN_IDENTITY)
		return NULL;
	/*
	 * Allocate the domain and initialise some of its data structures.
	 * We can't really do anything meaningful until we've added a
	 * master.
	 */
	smmu_domain = kzalloc(sizeof(*smmu_domain), GFP_KERNEL);
	if (!smmu_domain)
		return NULL;

	if (type == IOMMU_DOMAIN_DMA && (using_legacy_binding ||
	    iommu_get_dma_cookie(&smmu_domain->domain))) {
		kfree(smmu_domain);
		return NULL;
	}

	mutex_init(&smmu_domain->init_mutex);
	spin_lock_init(&smmu_domain->cb_lock);

	return &smmu_domain->domain;
}

static void arm_smmu_domain_free(struct iommu_domain *domain)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);

	/*
	 * Free the domain resources. We assume that all devices have
	 * already been detached.
	 */
	iommu_put_dma_cookie(domain);
	arm_smmu_destroy_domain_context(domain);
	kfree(smmu_domain);
}

static void arm_smmu_write_smr(struct arm_smmu_device *smmu, int idx)
{
	struct arm_smmu_smr *smr = smmu->smrs + idx;
	u32 reg = smr->id << SMR_ID_SHIFT | smr->mask << SMR_MASK_SHIFT;

	if (!(smmu->features & ARM_SMMU_FEAT_EXIDS) && smr->valid)
		reg |= SMR_VALID;
	writel_relaxed(reg, ARM_SMMU_GR0(smmu) + ARM_SMMU_GR0_SMR(idx));
}

static void arm_smmu_write_s2cr(struct arm_smmu_device *smmu, int idx)
{
	struct arm_smmu_s2cr *s2cr = smmu->s2crs + idx;
	u32 reg = (s2cr->type & S2CR_TYPE_MASK) << S2CR_TYPE_SHIFT |
		  (s2cr->cbndx & S2CR_CBNDX_MASK) << S2CR_CBNDX_SHIFT |
		  (s2cr->privcfg & S2CR_PRIVCFG_MASK) << S2CR_PRIVCFG_SHIFT;

	if (smmu->features & ARM_SMMU_FEAT_EXIDS && smmu->smrs &&
	    smmu->smrs[idx].valid)
		reg |= S2CR_EXIDVALID;
	writel_relaxed(reg, ARM_SMMU_GR0(smmu) + ARM_SMMU_GR0_S2CR(idx));
}

static void arm_smmu_write_sme(struct arm_smmu_device *smmu, int idx)
{
	arm_smmu_write_s2cr(smmu, idx);
	if (smmu->smrs)
		arm_smmu_write_smr(smmu, idx);
}

/*
 * The width of SMR's mask field depends on sCR0_EXIDENABLE, so this function
 * should be called after sCR0 is written.
 */
static void arm_smmu_test_smr_masks(struct arm_smmu_device *smmu)
{
	void __iomem *gr0_base = ARM_SMMU_GR0(smmu);
	u32 smr;

	if (!smmu->smrs)
		return;

	/*
	 * SMR.ID bits may not be preserved if the corresponding MASK
	 * bits are set, so check each one separately. We can reject
	 * masters later if they try to claim IDs outside these masks.
	 */
	smr = smmu->streamid_mask << SMR_ID_SHIFT;
	writel_relaxed(smr, gr0_base + ARM_SMMU_GR0_SMR(0));
	smr = readl_relaxed(gr0_base + ARM_SMMU_GR0_SMR(0));
	smmu->streamid_mask = smr >> SMR_ID_SHIFT;

	smr = smmu->streamid_mask << SMR_MASK_SHIFT;
	writel_relaxed(smr, gr0_base + ARM_SMMU_GR0_SMR(0));
	smr = readl_relaxed(gr0_base + ARM_SMMU_GR0_SMR(0));
	smmu->smr_mask_mask = smr >> SMR_MASK_SHIFT;
}

static int arm_smmu_find_sme(struct arm_smmu_device *smmu, u16 id, u16 mask)
{
	struct arm_smmu_smr *smrs = smmu->smrs;
	int i, free_idx = -ENOSPC;

	/* Stream indexing is blissfully easy */
	if (!smrs)
		return id;

	/* Validating SMRs is... less so */
	for (i = 0; i < smmu->num_mapping_groups; ++i) {
		if (!smrs[i].valid) {
			/*
			 * Note the first free entry we come across, which
			 * we'll claim in the end if nothing else matches.
			 */
			if (free_idx < 0)
				free_idx = i;
			continue;
		}
		/*
		 * If the new entry is _entirely_ matched by an existing entry,
		 * then reuse that, with the guarantee that there also cannot
		 * be any subsequent conflicting entries. In normal use we'd
		 * expect simply identical entries for this case, but there's
		 * no harm in accommodating the generalisation.
		 */
		if ((mask & smrs[i].mask) == mask &&
		    !((id ^ smrs[i].id) & ~smrs[i].mask))
			return i;
		/*
		 * If the new entry has any other overlap with an existing one,
		 * though, then there always exists at least one stream ID
		 * which would cause a conflict, and we can't allow that risk.
		 */
		if (!((id ^ smrs[i].id) & ~(smrs[i].mask | mask)))
			return -EINVAL;
	}

	return free_idx;
}

static bool arm_smmu_free_sme(struct arm_smmu_device *smmu, int idx)
{
	if (--smmu->s2crs[idx].count)
		return false;

	smmu->s2crs[idx] = s2cr_init_val;
	if (smmu->smrs)
		smmu->smrs[idx].valid = false;

	return true;
}

static int arm_smmu_master_alloc_smes(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev->iommu_fwspec;
	struct arm_smmu_master_cfg *cfg = fwspec->iommu_priv;
	struct arm_smmu_device *smmu = cfg->smmu;
	struct arm_smmu_smr *smrs = smmu->smrs;
	struct iommu_group *group;
	int i, idx, ret;

	mutex_lock(&smmu->stream_map_mutex);
	/* Figure out a viable stream map entry allocation */
	for_each_cfg_sme(fwspec, i, idx) {
		u16 sid = fwspec->ids[i];
		u16 mask = fwspec->ids[i] >> SMR_MASK_SHIFT;

		if (idx != INVALID_SMENDX) {
			ret = -EEXIST;
			goto out_err;
		}

		ret = arm_smmu_find_sme(smmu, sid, mask);
		if (ret < 0)
			goto out_err;

		idx = ret;
		if (smrs && smmu->s2crs[idx].count == 0) {
			smrs[idx].id = sid;
			smrs[idx].mask = mask;
			smrs[idx].valid = true;
		}
		smmu->s2crs[idx].count++;
		cfg->smendx[i] = (s16)idx;
	}

	group = iommu_group_get_for_dev(dev);
	if (!group)
		group = ERR_PTR(-ENOMEM);
	if (IS_ERR(group)) {
		ret = PTR_ERR(group);
		goto out_err;
	}
	iommu_group_put(group);

	/* It worked! Now, poke the actual hardware */
	for_each_cfg_sme(fwspec, i, idx) {
		arm_smmu_write_sme(smmu, idx);
		smmu->s2crs[idx].group = group;
	}

	mutex_unlock(&smmu->stream_map_mutex);
	return 0;

out_err:
	while (i--) {
		arm_smmu_free_sme(smmu, cfg->smendx[i]);
		cfg->smendx[i] = INVALID_SMENDX;
	}
	mutex_unlock(&smmu->stream_map_mutex);
	return ret;
}

static void arm_smmu_master_free_smes(struct iommu_fwspec *fwspec)
{
	struct arm_smmu_device *smmu = fwspec_smmu(fwspec);
	struct arm_smmu_master_cfg *cfg = fwspec->iommu_priv;
	int i, idx;

	mutex_lock(&smmu->stream_map_mutex);
	for_each_cfg_sme(fwspec, i, idx) {
		if (arm_smmu_free_sme(smmu, idx))
			arm_smmu_write_sme(smmu, idx);
		cfg->smendx[i] = INVALID_SMENDX;
	}
	mutex_unlock(&smmu->stream_map_mutex);
}

static int arm_smmu_domain_add_master(struct arm_smmu_domain *smmu_domain,
				      struct iommu_fwspec *fwspec)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_s2cr *s2cr = smmu->s2crs;
	u8 cbndx = smmu_domain->cfg.cbndx;
	enum arm_smmu_s2cr_type type;
	int i, idx;

	if (smmu_domain->stage == ARM_SMMU_DOMAIN_BYPASS)
		type = S2CR_TYPE_BYPASS;
	else
		type = S2CR_TYPE_TRANS;

	for_each_cfg_sme(fwspec, i, idx) {
		if (type == s2cr[idx].type && cbndx == s2cr[idx].cbndx)
			continue;

		s2cr[idx].type = type;
		s2cr[idx].privcfg = S2CR_PRIVCFG_DEFAULT;
		s2cr[idx].cbndx = cbndx;
		arm_smmu_write_s2cr(smmu, idx);
	}
	return 0;
}

static int arm_smmu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	int ret;
	struct iommu_fwspec *fwspec = dev->iommu_fwspec;
	struct arm_smmu_device *smmu;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);

	if (!fwspec || fwspec->ops != &arm_smmu_ops) {
		dev_err(dev, "cannot attach to SMMU, is it on the same bus?\n");
		return -ENXIO;
	}

	/*
	 * FIXME: The arch/arm DMA API code tries to attach devices to its own
	 * domains between of_xlate() and add_device() - we have no way to cope
	 * with that, so until ARM gets converted to rely on groups and default
	 * domains, just say no (but more politely than by dereferencing NULL).
	 * This should be at least a WARN_ON once that's sorted.
	 */
	if (!fwspec->iommu_priv)
		return -ENODEV;

	smmu = fwspec_smmu(fwspec);
	/* Ensure that the domain is finalised */
	ret = arm_smmu_init_domain_context(domain, smmu);
	if (ret < 0)
		return ret;

	/*
	 * Sanity check the domain. We don't support domains across
	 * different SMMUs.
	 */
	if (smmu_domain->smmu != smmu) {
		dev_err(dev,
			"cannot attach to SMMU %s whilst already attached to domain on SMMU %s\n",
			dev_name(smmu_domain->smmu->dev), dev_name(smmu->dev));
		return -EINVAL;
	}

	/* Looks ok, so add the device to the domain */
	return arm_smmu_domain_add_master(smmu_domain, fwspec);
}

static int arm_smmu_map(struct iommu_domain *domain, unsigned long iova,
			phys_addr_t paddr, size_t size, int prot)
{
	struct io_pgtable_ops *ops = to_smmu_domain(domain)->pgtbl_ops;

	if (!ops)
		return -ENODEV;

	return ops->map(ops, iova, paddr, size, prot);
}

static size_t arm_smmu_unmap(struct iommu_domain *domain, unsigned long iova,
			     size_t size)
{
	struct io_pgtable_ops *ops = to_smmu_domain(domain)->pgtbl_ops;

	if (!ops)
		return 0;

	return ops->unmap(ops, iova, size);
}

static phys_addr_t arm_smmu_iova_to_phys_hard(struct iommu_domain *domain,
					      dma_addr_t iova)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct io_pgtable_ops *ops= smmu_domain->pgtbl_ops;
	struct device *dev = smmu->dev;
	void __iomem *cb_base;
	u32 tmp;
	u64 phys;
	unsigned long va, flags;

	cb_base = ARM_SMMU_CB(smmu, cfg->cbndx);

	spin_lock_irqsave(&smmu_domain->cb_lock, flags);
	/* ATS1 registers can only be written atomically */
	va = iova & ~0xfffUL;
	if (smmu->version == ARM_SMMU_V2)
		smmu_write_atomic_lq(va, cb_base + ARM_SMMU_CB_ATS1PR);
	else /* Register is only 32-bit in v1 */
		writel_relaxed(va, cb_base + ARM_SMMU_CB_ATS1PR);

	if (readl_poll_timeout_atomic(cb_base + ARM_SMMU_CB_ATSR, tmp,
				      !(tmp & ATSR_ACTIVE), 5, 50)) {
		spin_unlock_irqrestore(&smmu_domain->cb_lock, flags);
		dev_err(dev,
			"iova to phys timed out on %pad. Falling back to software table walk.\n",
			&iova);
		return ops->iova_to_phys(ops, iova);
	}

	phys = readq_relaxed(cb_base + ARM_SMMU_CB_PAR);
	spin_unlock_irqrestore(&smmu_domain->cb_lock, flags);
	if (phys & CB_PAR_F) {
		dev_err(dev, "translation fault!\n");
		dev_err(dev, "PAR = 0x%llx\n", phys);
		return 0;
	}

	return (phys & GENMASK_ULL(39, 12)) | (iova & 0xfff);
}

static phys_addr_t arm_smmu_iova_to_phys(struct iommu_domain *domain,
					dma_addr_t iova)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct io_pgtable_ops *ops = smmu_domain->pgtbl_ops;

	if (domain->type == IOMMU_DOMAIN_IDENTITY)
		return iova;

	if (!ops)
		return 0;

	if (smmu_domain->smmu->features & ARM_SMMU_FEAT_TRANS_OPS &&
			smmu_domain->stage == ARM_SMMU_DOMAIN_S1)
		return arm_smmu_iova_to_phys_hard(domain, iova);

	return ops->iova_to_phys(ops, iova);
}

static bool arm_smmu_capable(enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		/*
		 * Return true here as the SMMU can always send out coherent
		 * requests.
		 */
		return true;
	case IOMMU_CAP_NOEXEC:
		return true;
	default:
		return false;
	}
}

static int arm_smmu_match_node(struct device *dev, void *data)
{
	return dev->fwnode == data;
}

static
struct arm_smmu_device *arm_smmu_get_by_fwnode(struct fwnode_handle *fwnode)
{
	struct device *dev = driver_find_device(&arm_smmu_driver.driver, NULL,
						fwnode, arm_smmu_match_node);
	put_device(dev);
	return dev ? dev_get_drvdata(dev) : NULL;
}

static int arm_smmu_add_device(struct device *dev)
{
	struct arm_smmu_device *smmu;
	struct arm_smmu_master_cfg *cfg;
	struct iommu_fwspec *fwspec = dev->iommu_fwspec;
	int i, ret;

	if (using_legacy_binding) {
		ret = arm_smmu_register_legacy_master(dev, &smmu);
		fwspec = dev->iommu_fwspec;
		if (ret)
			goto out_free;
	} else if (fwspec && fwspec->ops == &arm_smmu_ops) {
		smmu = arm_smmu_get_by_fwnode(fwspec->iommu_fwnode);
	} else {
		return -ENODEV;
	}

	ret = -EINVAL;
	for (i = 0; i < fwspec->num_ids; i++) {
		u16 sid = fwspec->ids[i];
		u16 mask = fwspec->ids[i] >> SMR_MASK_SHIFT;

		if (sid & ~smmu->streamid_mask) {
			dev_err(dev, "stream ID 0x%x out of range for SMMU (0x%x)\n",
				sid, smmu->streamid_mask);
			goto out_free;
		}
		if (mask & ~smmu->smr_mask_mask) {
			dev_err(dev, "SMR mask 0x%x out of range for SMMU (0x%x)\n",
				mask, smmu->smr_mask_mask);
			goto out_free;
		}
	}

	ret = -ENOMEM;
	cfg = kzalloc(offsetof(struct arm_smmu_master_cfg, smendx[i]),
		      GFP_KERNEL);
	if (!cfg)
		goto out_free;

	cfg->smmu = smmu;
	fwspec->iommu_priv = cfg;
	while (i--)
		cfg->smendx[i] = INVALID_SMENDX;

	ret = arm_smmu_master_alloc_smes(dev);
	if (ret)
		goto out_free;

	iommu_device_link(&smmu->iommu, dev);

	return 0;

out_free:
	if (fwspec)
		kfree(fwspec->iommu_priv);
	iommu_fwspec_free(dev);
	return ret;
}

static void arm_smmu_remove_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev->iommu_fwspec;
	struct arm_smmu_master_cfg *cfg;
	struct arm_smmu_device *smmu;


	if (!fwspec || fwspec->ops != &arm_smmu_ops)
		return;

	cfg  = fwspec->iommu_priv;
	smmu = cfg->smmu;

	iommu_device_unlink(&smmu->iommu, dev);
	arm_smmu_master_free_smes(fwspec);
	iommu_group_remove_device(dev);
	kfree(fwspec->iommu_priv);
	iommu_fwspec_free(dev);
}

static struct iommu_group *arm_smmu_device_group(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev->iommu_fwspec;
	struct arm_smmu_device *smmu = fwspec_smmu(fwspec);
	struct iommu_group *group = NULL;
	int i, idx;

	for_each_cfg_sme(fwspec, i, idx) {
		if (group && smmu->s2crs[idx].group &&
		    group != smmu->s2crs[idx].group)
			return ERR_PTR(-EINVAL);

		group = smmu->s2crs[idx].group;
	}

	if (group)
		return iommu_group_ref_get(group);

	if (dev_is_pci(dev))
		group = pci_device_group(dev);
	else
		group = generic_device_group(dev);

	return group;
}

static int arm_smmu_domain_get_attr(struct iommu_domain *domain,
				    enum iommu_attr attr, void *data)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);

	if (domain->type != IOMMU_DOMAIN_UNMANAGED)
		return -EINVAL;

	switch (attr) {
	case DOMAIN_ATTR_NESTING:
		*(int *)data = (smmu_domain->stage == ARM_SMMU_DOMAIN_NESTED);
		return 0;
	default:
		return -ENODEV;
	}
}

static int arm_smmu_domain_set_attr(struct iommu_domain *domain,
				    enum iommu_attr attr, void *data)
{
	int ret = 0;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);

	if (domain->type != IOMMU_DOMAIN_UNMANAGED)
		return -EINVAL;

	mutex_lock(&smmu_domain->init_mutex);

	switch (attr) {
	case DOMAIN_ATTR_NESTING:
		if (smmu_domain->smmu) {
			ret = -EPERM;
			goto out_unlock;
		}

		if (*(int *)data)
			smmu_domain->stage = ARM_SMMU_DOMAIN_NESTED;
		else
			smmu_domain->stage = ARM_SMMU_DOMAIN_S1;

		break;
	default:
		ret = -ENODEV;
	}

out_unlock:
	mutex_unlock(&smmu_domain->init_mutex);
	return ret;
}

static int arm_smmu_of_xlate(struct device *dev, struct of_phandle_args *args)
{
	u32 mask, fwid = 0;

	if (args->args_count > 0)
		fwid |= (u16)args->args[0];

	if (args->args_count > 1)
		fwid |= (u16)args->args[1] << SMR_MASK_SHIFT;
	else if (!of_property_read_u32(args->np, "stream-match-mask", &mask))
		fwid |= (u16)mask << SMR_MASK_SHIFT;

	return iommu_fwspec_add_ids(dev, &fwid, 1);
}

static void arm_smmu_get_resv_regions(struct device *dev,
				      struct list_head *head)
{
	struct iommu_resv_region *region;
	int prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_MMIO;

	region = iommu_alloc_resv_region(MSI_IOVA_BASE, MSI_IOVA_LENGTH,
					 prot, IOMMU_RESV_SW_MSI);
	if (!region)
		return;

	list_add_tail(&region->list, head);

	iommu_dma_get_resv_regions(dev, head);
}

static void arm_smmu_put_resv_regions(struct device *dev,
				      struct list_head *head)
{
	struct iommu_resv_region *entry, *next;

	list_for_each_entry_safe(entry, next, head, list)
		kfree(entry);
}

static struct iommu_ops arm_smmu_ops = {
	.capable		= arm_smmu_capable,
	.domain_alloc		= arm_smmu_domain_alloc,
	.domain_free		= arm_smmu_domain_free,
	.attach_dev		= arm_smmu_attach_dev,
	.map			= arm_smmu_map,
	.unmap			= arm_smmu_unmap,
	.map_sg			= default_iommu_map_sg,
	.iova_to_phys		= arm_smmu_iova_to_phys,
	.add_device		= arm_smmu_add_device,
	.remove_device		= arm_smmu_remove_device,
	.device_group		= arm_smmu_device_group,
	.domain_get_attr	= arm_smmu_domain_get_attr,
	.domain_set_attr	= arm_smmu_domain_set_attr,
	.of_xlate		= arm_smmu_of_xlate,
	.get_resv_regions	= arm_smmu_get_resv_regions,
	.put_resv_regions	= arm_smmu_put_resv_regions,
	.pgsize_bitmap		= -1UL, /* Restricted during device attach */
};

static void arm_smmu_device_reset(struct arm_smmu_device *smmu)
{
	void __iomem *gr0_base = ARM_SMMU_GR0(smmu);
	void __iomem *cb_base;
	int i;
	u32 reg, major;

	/* clear global FSR */
	reg = readl_relaxed(ARM_SMMU_GR0_NS(smmu) + ARM_SMMU_GR0_sGFSR);
	writel(reg, ARM_SMMU_GR0_NS(smmu) + ARM_SMMU_GR0_sGFSR);

	/*
	 * Reset stream mapping groups: Initial values mark all SMRn as
	 * invalid and all S2CRn as bypass unless overridden.
	 */
	for (i = 0; i < smmu->num_mapping_groups; ++i)
		arm_smmu_write_sme(smmu, i);

	if (smmu->model == ARM_MMU500) {
		/*
		 * Before clearing ARM_MMU500_ACTLR_CPRE, need to
		 * clear CACHE_LOCK bit of ACR first. And, CACHE_LOCK
		 * bit is only present in MMU-500r2 onwards.
		 */
		reg = readl_relaxed(gr0_base + ARM_SMMU_GR0_ID7);
		major = (reg >> ID7_MAJOR_SHIFT) & ID7_MAJOR_MASK;
		reg = readl_relaxed(gr0_base + ARM_SMMU_GR0_sACR);
		if (major >= 2)
			reg &= ~ARM_MMU500_ACR_CACHE_LOCK;
		/*
		 * Allow unmatched Stream IDs to allocate bypass
		 * TLB entries for reduced latency.
		 */
		reg |= ARM_MMU500_ACR_SMTNMB_TLBEN;
		writel_relaxed(reg, gr0_base + ARM_SMMU_GR0_sACR);
	}

	/* Make sure all context banks are disabled and clear CB_FSR  */
	for (i = 0; i < smmu->num_context_banks; ++i) {
		cb_base = ARM_SMMU_CB(smmu, i);
		writel_relaxed(0, cb_base + ARM_SMMU_CB_SCTLR);
		writel_relaxed(FSR_FAULT, cb_base + ARM_SMMU_CB_FSR);
		/*
		 * Disable MMU-500's not-particularly-beneficial next-page
		 * prefetcher for the sake of errata #841119 and #826419.
		 */
		if (smmu->model == ARM_MMU500) {
			reg = readl_relaxed(cb_base + ARM_SMMU_CB_ACTLR);
			reg &= ~ARM_MMU500_ACTLR_CPRE;
			writel_relaxed(reg, cb_base + ARM_SMMU_CB_ACTLR);
		}
	}

	/* Invalidate the TLB, just in case */
	writel_relaxed(0, gr0_base + ARM_SMMU_GR0_TLBIALLH);
	writel_relaxed(0, gr0_base + ARM_SMMU_GR0_TLBIALLNSNH);

	reg = readl_relaxed(ARM_SMMU_GR0_NS(smmu) + ARM_SMMU_GR0_sCR0);

	/* Enable fault reporting */
	reg |= (sCR0_GFRE | sCR0_GFIE | sCR0_GCFGFRE | sCR0_GCFGFIE);

	/* Disable TLB broadcasting. */
	reg |= (sCR0_VMIDPNE | sCR0_PTM);

	/* Enable client access, handling unmatched streams as appropriate */
	reg &= ~sCR0_CLIENTPD;
	if (disable_bypass)
		reg |= sCR0_USFCFG;
	else
		reg &= ~sCR0_USFCFG;

	/* Disable forced broadcasting */
	reg &= ~sCR0_FB;

	/* Don't upgrade barriers */
	reg &= ~(sCR0_BSU_MASK << sCR0_BSU_SHIFT);

	if (smmu->features & ARM_SMMU_FEAT_VMID16)
		reg |= sCR0_VMID16EN;

	if (smmu->features & ARM_SMMU_FEAT_EXIDS)
		reg |= sCR0_EXIDENABLE;

	/* Push the button */
	arm_smmu_tlb_sync_global(smmu);
	writel(reg, ARM_SMMU_GR0_NS(smmu) + ARM_SMMU_GR0_sCR0);
}

static int arm_smmu_id_size_to_bits(int size)
{
	switch (size) {
	case 0:
		return 32;
	case 1:
		return 36;
	case 2:
		return 40;
	case 3:
		return 42;
	case 4:
		return 44;
	case 5:
	default:
		return 48;
	}
}

static int arm_smmu_device_cfg_probe(struct arm_smmu_device *smmu)
{
	unsigned long size;
	void __iomem *gr0_base = ARM_SMMU_GR0(smmu);
	u32 id;
	bool cttw_reg, cttw_fw = smmu->features & ARM_SMMU_FEAT_COHERENT_WALK;
	int i;

	dev_notice(smmu->dev, "probing hardware configuration...\n");
	dev_notice(smmu->dev, "SMMUv%d with:\n",
			smmu->version == ARM_SMMU_V2 ? 2 : 1);

	/* ID0 */
	id = readl_relaxed(gr0_base + ARM_SMMU_GR0_ID0);

	/* Restrict available stages based on module parameter */
	if (force_stage == 1)
		id &= ~(ID0_S2TS | ID0_NTS);
	else if (force_stage == 2)
		id &= ~(ID0_S1TS | ID0_NTS);

	if (id & ID0_S1TS) {
		smmu->features |= ARM_SMMU_FEAT_TRANS_S1;
		dev_notice(smmu->dev, "\tstage 1 translation\n");
	}

	if (id & ID0_S2TS) {
		smmu->features |= ARM_SMMU_FEAT_TRANS_S2;
		dev_notice(smmu->dev, "\tstage 2 translation\n");
	}

	if (id & ID0_NTS) {
		smmu->features |= ARM_SMMU_FEAT_TRANS_NESTED;
		dev_notice(smmu->dev, "\tnested translation\n");
	}

	if (!(smmu->features &
		(ARM_SMMU_FEAT_TRANS_S1 | ARM_SMMU_FEAT_TRANS_S2))) {
		dev_err(smmu->dev, "\tno translation support!\n");
		return -ENODEV;
	}

	if ((id & ID0_S1TS) &&
		((smmu->version < ARM_SMMU_V2) || !(id & ID0_ATOSNS))) {
		smmu->features |= ARM_SMMU_FEAT_TRANS_OPS;
		dev_notice(smmu->dev, "\taddress translation ops\n");
	}

	/*
	 * In order for DMA API calls to work properly, we must defer to what
	 * the FW says about coherency, regardless of what the hardware claims.
	 * Fortunately, this also opens up a workaround for systems where the
	 * ID register value has ended up configured incorrectly.
	 */
	cttw_reg = !!(id & ID0_CTTW);
	if (cttw_fw || cttw_reg)
		dev_notice(smmu->dev, "\t%scoherent table walk\n",
			   cttw_fw ? "" : "non-");
	if (cttw_fw != cttw_reg)
		dev_notice(smmu->dev,
			   "\t(IDR0.CTTW overridden by FW configuration)\n");

	/* Max. number of entries we have for stream matching/indexing */
	if (smmu->version == ARM_SMMU_V2 && id & ID0_EXIDS) {
		smmu->features |= ARM_SMMU_FEAT_EXIDS;
		size = 1 << 16;
	} else {
		size = 1 << ((id >> ID0_NUMSIDB_SHIFT) & ID0_NUMSIDB_MASK);
	}
	smmu->streamid_mask = size - 1;
	if (id & ID0_SMS) {
		smmu->features |= ARM_SMMU_FEAT_STREAM_MATCH;
		size = (id >> ID0_NUMSMRG_SHIFT) & ID0_NUMSMRG_MASK;
		if (size == 0) {
			dev_err(smmu->dev,
				"stream-matching supported, but no SMRs present!\n");
			return -ENODEV;
		}

		/* Zero-initialised to mark as invalid */
		smmu->smrs = devm_kcalloc(smmu->dev, size, sizeof(*smmu->smrs),
					  GFP_KERNEL);
		if (!smmu->smrs)
			return -ENOMEM;

		dev_notice(smmu->dev,
			   "\tstream matching with %lu register groups", size);
	}
	/* s2cr->type == 0 means translation, so initialise explicitly */
	smmu->s2crs = devm_kmalloc_array(smmu->dev, size, sizeof(*smmu->s2crs),
					 GFP_KERNEL);
	if (!smmu->s2crs)
		return -ENOMEM;
	for (i = 0; i < size; i++)
		smmu->s2crs[i] = s2cr_init_val;

	smmu->num_mapping_groups = size;
	mutex_init(&smmu->stream_map_mutex);

	if (smmu->version < ARM_SMMU_V2 || !(id & ID0_PTFS_NO_AARCH32)) {
		smmu->features |= ARM_SMMU_FEAT_FMT_AARCH32_L;
		if (!(id & ID0_PTFS_NO_AARCH32S))
			smmu->features |= ARM_SMMU_FEAT_FMT_AARCH32_S;
	}

	/* ID1 */
	id = readl_relaxed(gr0_base + ARM_SMMU_GR0_ID1);
	smmu->pgshift = (id & ID1_PAGESIZE) ? 16 : 12;

	/* Check for size mismatch of SMMU address space from mapped region */
	size = 1 << (((id >> ID1_NUMPAGENDXB_SHIFT) & ID1_NUMPAGENDXB_MASK) + 1);
	size <<= smmu->pgshift;
	if (smmu->cb_base != gr0_base + size)
		dev_warn(smmu->dev,
			"SMMU address space size (0x%lx) differs from mapped region size (0x%tx)!\n",
			size * 2, (smmu->cb_base - gr0_base) * 2);

	smmu->num_s2_context_banks = (id >> ID1_NUMS2CB_SHIFT) & ID1_NUMS2CB_MASK;
	smmu->num_context_banks = (id >> ID1_NUMCB_SHIFT) & ID1_NUMCB_MASK;
	if (smmu->num_s2_context_banks > smmu->num_context_banks) {
		dev_err(smmu->dev, "impossible number of S2 context banks!\n");
		return -ENODEV;
	}
	dev_notice(smmu->dev, "\t%u context banks (%u stage-2 only)\n",
		   smmu->num_context_banks, smmu->num_s2_context_banks);
	/*
	 * Cavium CN88xx erratum #27704.
	 * Ensure ASID and VMID allocation is unique across all SMMUs in
	 * the system.
	 */
	if (smmu->model == CAVIUM_SMMUV2) {
		smmu->cavium_id_base =
			atomic_add_return(smmu->num_context_banks,
					  &cavium_smmu_context_count);
		smmu->cavium_id_base -= smmu->num_context_banks;
		dev_notice(smmu->dev, "\tenabling workaround for Cavium erratum 27704\n");
	}

	/* ID2 */
	id = readl_relaxed(gr0_base + ARM_SMMU_GR0_ID2);
	size = arm_smmu_id_size_to_bits((id >> ID2_IAS_SHIFT) & ID2_IAS_MASK);
	smmu->ipa_size = size;

	/* The output mask is also applied for bypass */
	size = arm_smmu_id_size_to_bits((id >> ID2_OAS_SHIFT) & ID2_OAS_MASK);
	smmu->pa_size = size;

	if (id & ID2_VMID16)
		smmu->features |= ARM_SMMU_FEAT_VMID16;

	/*
	 * What the page table walker can address actually depends on which
	 * descriptor format is in use, but since a) we don't know that yet,
	 * and b) it can vary per context bank, this will have to do...
	 */
	if (dma_set_mask_and_coherent(smmu->dev, DMA_BIT_MASK(size)))
		dev_warn(smmu->dev,
			 "failed to set DMA mask for table walker\n");

	if (smmu->version < ARM_SMMU_V2) {
		smmu->va_size = smmu->ipa_size;
		if (smmu->version == ARM_SMMU_V1_64K)
			smmu->features |= ARM_SMMU_FEAT_FMT_AARCH64_64K;
	} else {
		size = (id >> ID2_UBS_SHIFT) & ID2_UBS_MASK;
		smmu->va_size = arm_smmu_id_size_to_bits(size);
		if (id & ID2_PTFS_4K)
			smmu->features |= ARM_SMMU_FEAT_FMT_AARCH64_4K;
		if (id & ID2_PTFS_16K)
			smmu->features |= ARM_SMMU_FEAT_FMT_AARCH64_16K;
		if (id & ID2_PTFS_64K)
			smmu->features |= ARM_SMMU_FEAT_FMT_AARCH64_64K;
	}

	/* Now we've corralled the various formats, what'll it do? */
	if (smmu->features & ARM_SMMU_FEAT_FMT_AARCH32_S)
		smmu->pgsize_bitmap |= SZ_4K | SZ_64K | SZ_1M | SZ_16M;
	if (smmu->features &
	    (ARM_SMMU_FEAT_FMT_AARCH32_L | ARM_SMMU_FEAT_FMT_AARCH64_4K))
		smmu->pgsize_bitmap |= SZ_4K | SZ_2M | SZ_1G;
	if (smmu->features & ARM_SMMU_FEAT_FMT_AARCH64_16K)
		smmu->pgsize_bitmap |= SZ_16K | SZ_32M;
	if (smmu->features & ARM_SMMU_FEAT_FMT_AARCH64_64K)
		smmu->pgsize_bitmap |= SZ_64K | SZ_512M;

	if (arm_smmu_ops.pgsize_bitmap == -1UL)
		arm_smmu_ops.pgsize_bitmap = smmu->pgsize_bitmap;
	else
		arm_smmu_ops.pgsize_bitmap |= smmu->pgsize_bitmap;
	dev_notice(smmu->dev, "\tSupported page sizes: 0x%08lx\n",
		   smmu->pgsize_bitmap);


	if (smmu->features & ARM_SMMU_FEAT_TRANS_S1)
		dev_notice(smmu->dev, "\tStage-1: %lu-bit VA -> %lu-bit IPA\n",
			   smmu->va_size, smmu->ipa_size);

	if (smmu->features & ARM_SMMU_FEAT_TRANS_S2)
		dev_notice(smmu->dev, "\tStage-2: %lu-bit IPA -> %lu-bit PA\n",
			   smmu->ipa_size, smmu->pa_size);

	return 0;
}

struct arm_smmu_match_data {
	enum arm_smmu_arch_version version;
	enum arm_smmu_implementation model;
};

#define ARM_SMMU_MATCH_DATA(name, ver, imp)	\
static struct arm_smmu_match_data name = { .version = ver, .model = imp }

ARM_SMMU_MATCH_DATA(smmu_generic_v1, ARM_SMMU_V1, GENERIC_SMMU);
ARM_SMMU_MATCH_DATA(smmu_generic_v2, ARM_SMMU_V2, GENERIC_SMMU);
ARM_SMMU_MATCH_DATA(arm_mmu401, ARM_SMMU_V1_64K, GENERIC_SMMU);
ARM_SMMU_MATCH_DATA(arm_mmu500, ARM_SMMU_V2, ARM_MMU500);
ARM_SMMU_MATCH_DATA(cavium_smmuv2, ARM_SMMU_V2, CAVIUM_SMMUV2);

static const struct of_device_id arm_smmu_of_match[] = {
	{ .compatible = "arm,smmu-v1", .data = &smmu_generic_v1 },
	{ .compatible = "arm,smmu-v2", .data = &smmu_generic_v2 },
	{ .compatible = "arm,mmu-400", .data = &smmu_generic_v1 },
	{ .compatible = "arm,mmu-401", .data = &arm_mmu401 },
	{ .compatible = "arm,mmu-500", .data = &arm_mmu500 },
	{ .compatible = "cavium,smmu-v2", .data = &cavium_smmuv2 },
	{ },
};
MODULE_DEVICE_TABLE(of, arm_smmu_of_match);

#ifdef CONFIG_ACPI
static int acpi_smmu_get_data(u32 model, struct arm_smmu_device *smmu)
{
	int ret = 0;

	switch (model) {
	case ACPI_IORT_SMMU_V1:
	case ACPI_IORT_SMMU_CORELINK_MMU400:
		smmu->version = ARM_SMMU_V1;
		smmu->model = GENERIC_SMMU;
		break;
	case ACPI_IORT_SMMU_CORELINK_MMU401:
		smmu->version = ARM_SMMU_V1_64K;
		smmu->model = GENERIC_SMMU;
		break;
	case ACPI_IORT_SMMU_V2:
		smmu->version = ARM_SMMU_V2;
		smmu->model = GENERIC_SMMU;
		break;
	case ACPI_IORT_SMMU_CORELINK_MMU500:
		smmu->version = ARM_SMMU_V2;
		smmu->model = ARM_MMU500;
		break;
	case ACPI_IORT_SMMU_CAVIUM_THUNDERX:
		smmu->version = ARM_SMMU_V2;
		smmu->model = CAVIUM_SMMUV2;
		break;
	default:
		ret = -ENODEV;
	}

	return ret;
}

static int arm_smmu_device_acpi_probe(struct platform_device *pdev,
				      struct arm_smmu_device *smmu)
{
	struct device *dev = smmu->dev;
	struct acpi_iort_node *node =
		*(struct acpi_iort_node **)dev_get_platdata(dev);
	struct acpi_iort_smmu *iort_smmu;
	int ret;

	/* Retrieve SMMU1/2 specific data */
	iort_smmu = (struct acpi_iort_smmu *)node->node_data;

	ret = acpi_smmu_get_data(iort_smmu->model, smmu);
	if (ret < 0)
		return ret;

	/* Ignore the configuration access interrupt */
	smmu->num_global_irqs = 1;

	if (iort_smmu->flags & ACPI_IORT_SMMU_COHERENT_WALK)
		smmu->features |= ARM_SMMU_FEAT_COHERENT_WALK;

	return 0;
}
#else
static inline int arm_smmu_device_acpi_probe(struct platform_device *pdev,
					     struct arm_smmu_device *smmu)
{
	return -ENODEV;
}
#endif

static int arm_smmu_device_dt_probe(struct platform_device *pdev,
				    struct arm_smmu_device *smmu)
{
	const struct arm_smmu_match_data *data;
	struct device *dev = &pdev->dev;
	bool legacy_binding;

	if (of_property_read_u32(dev->of_node, "#global-interrupts",
				 &smmu->num_global_irqs)) {
		dev_err(dev, "missing #global-interrupts property\n");
		return -ENODEV;
	}

	data = of_device_get_match_data(dev);
	smmu->version = data->version;
	smmu->model = data->model;

	parse_driver_options(smmu);

	legacy_binding = of_find_property(dev->of_node, "mmu-masters", NULL);
	if (legacy_binding && !using_generic_binding) {
		if (!using_legacy_binding)
			pr_notice("deprecated \"mmu-masters\" DT property in use; DMA API support unavailable\n");
		using_legacy_binding = true;
	} else if (!legacy_binding && !using_legacy_binding) {
		using_generic_binding = true;
	} else {
		dev_err(dev, "not probing due to mismatched DT properties\n");
		return -ENODEV;
	}

	if (of_dma_is_coherent(dev->of_node))
		smmu->features |= ARM_SMMU_FEAT_COHERENT_WALK;

	return 0;
}

static void arm_smmu_bus_init(void)
{
	/* Oh, for a proper bus abstraction */
	if (!iommu_present(&platform_bus_type))
		bus_set_iommu(&platform_bus_type, &arm_smmu_ops);
#ifdef CONFIG_ARM_AMBA
	if (!iommu_present(&amba_bustype))
		bus_set_iommu(&amba_bustype, &arm_smmu_ops);
#endif
#ifdef CONFIG_PCI
	if (!iommu_present(&pci_bus_type)) {
		pci_request_acs();
		bus_set_iommu(&pci_bus_type, &arm_smmu_ops);
	}
#endif
}

static int arm_smmu_device_probe(struct platform_device *pdev)
{
	struct resource *res;
	resource_size_t ioaddr;
	struct arm_smmu_device *smmu;
	struct device *dev = &pdev->dev;
	int num_irqs, i, err;

	smmu = devm_kzalloc(dev, sizeof(*smmu), GFP_KERNEL);
	if (!smmu) {
		dev_err(dev, "failed to allocate arm_smmu_device\n");
		return -ENOMEM;
	}
	smmu->dev = dev;

	if (dev->of_node)
		err = arm_smmu_device_dt_probe(pdev, smmu);
	else
		err = arm_smmu_device_acpi_probe(pdev, smmu);

	if (err)
		return err;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ioaddr = res->start;
	smmu->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(smmu->base))
		return PTR_ERR(smmu->base);
	smmu->cb_base = smmu->base + resource_size(res) / 2;

	num_irqs = 0;
	while ((res = platform_get_resource(pdev, IORESOURCE_IRQ, num_irqs))) {
		num_irqs++;
		if (num_irqs > smmu->num_global_irqs)
			smmu->num_context_irqs++;
	}

	if (!smmu->num_context_irqs) {
		dev_err(dev, "found %d interrupts but expected at least %d\n",
			num_irqs, smmu->num_global_irqs + 1);
		return -ENODEV;
	}

	smmu->irqs = devm_kzalloc(dev, sizeof(*smmu->irqs) * num_irqs,
				  GFP_KERNEL);
	if (!smmu->irqs) {
		dev_err(dev, "failed to allocate %d irqs\n", num_irqs);
		return -ENOMEM;
	}

	for (i = 0; i < num_irqs; ++i) {
		int irq = platform_get_irq(pdev, i);

		if (irq < 0) {
			dev_err(dev, "failed to get irq index %d\n", i);
			return -ENODEV;
		}
		smmu->irqs[i] = irq;
	}

	err = arm_smmu_device_cfg_probe(smmu);
	if (err)
		return err;

	if (smmu->version == ARM_SMMU_V2 &&
	    smmu->num_context_banks != smmu->num_context_irqs) {
		dev_err(dev,
			"found only %d context interrupt(s) but %d required\n",
			smmu->num_context_irqs, smmu->num_context_banks);
		return -ENODEV;
	}

	for (i = 0; i < smmu->num_global_irqs; ++i) {
		err = devm_request_irq(smmu->dev, smmu->irqs[i],
				       arm_smmu_global_fault,
				       IRQF_SHARED,
				       "arm-smmu global fault",
				       smmu);
		if (err) {
			dev_err(dev, "failed to request global IRQ %d (%u)\n",
				i, smmu->irqs[i]);
			return err;
		}
	}

	err = iommu_device_sysfs_add(&smmu->iommu, smmu->dev, NULL,
				     "smmu.%pa", &ioaddr);
	if (err) {
		dev_err(dev, "Failed to register iommu in sysfs\n");
		return err;
	}

	iommu_device_set_ops(&smmu->iommu, &arm_smmu_ops);
	iommu_device_set_fwnode(&smmu->iommu, dev->fwnode);

	err = iommu_device_register(&smmu->iommu);
	if (err) {
		dev_err(dev, "Failed to register iommu\n");
		return err;
	}

	platform_set_drvdata(pdev, smmu);
	arm_smmu_device_reset(smmu);
	arm_smmu_test_smr_masks(smmu);

	/*
	 * For ACPI and generic DT bindings, an SMMU will be probed before
	 * any device which might need it, so we want the bus ops in place
	 * ready to handle default domain setup as soon as any SMMU exists.
	 */
	if (!using_legacy_binding)
		arm_smmu_bus_init();

	return 0;
}

/*
 * With the legacy DT binding in play, though, we have no guarantees about
 * probe order, but then we're also not doing default domains, so we can
 * delay setting bus ops until we're sure every possible SMMU is ready,
 * and that way ensure that no add_device() calls get missed.
 */
static int arm_smmu_legacy_bus_init(void)
{
	if (using_legacy_binding)
		arm_smmu_bus_init();
	return 0;
}
device_initcall_sync(arm_smmu_legacy_bus_init);

static int arm_smmu_device_remove(struct platform_device *pdev)
{
	struct arm_smmu_device *smmu = platform_get_drvdata(pdev);

	if (!smmu)
		return -ENODEV;

	if (!bitmap_empty(smmu->context_map, ARM_SMMU_MAX_CBS))
		dev_err(&pdev->dev, "removing device with active domains!\n");

	/* Turn the thing off */
	writel(sCR0_CLIENTPD, ARM_SMMU_GR0_NS(smmu) + ARM_SMMU_GR0_sCR0);
	return 0;
}

static struct platform_driver arm_smmu_driver = {
	.driver	= {
		.name		= "arm-smmu",
		.of_match_table	= of_match_ptr(arm_smmu_of_match),
	},
	.probe	= arm_smmu_device_probe,
	.remove	= arm_smmu_device_remove,
};
module_platform_driver(arm_smmu_driver);

IOMMU_OF_DECLARE(arm_smmuv1, "arm,smmu-v1", NULL);
IOMMU_OF_DECLARE(arm_smmuv2, "arm,smmu-v2", NULL);
IOMMU_OF_DECLARE(arm_mmu400, "arm,mmu-400", NULL);
IOMMU_OF_DECLARE(arm_mmu401, "arm,mmu-401", NULL);
IOMMU_OF_DECLARE(arm_mmu500, "arm,mmu-500", NULL);
IOMMU_OF_DECLARE(cavium_smmuv2, "cavium,smmu-v2", NULL);

MODULE_DESCRIPTION("IOMMU API for ARM architected SMMU implementations");
MODULE_AUTHOR("Will Deacon <will.deacon@arm.com>");
MODULE_LICENSE("GPL v2");
