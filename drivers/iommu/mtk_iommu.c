// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Yong Wu <yong.wu@mediatek.com>
 */
#include <linux/arm-smccc.h>
#include <linux/bitfield.h>
#include <linux/bug.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/io-pgtable.h>
#include <linux/list.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/soc/mediatek/infracfg.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>
#include <asm/barrier.h>
#include <soc/mediatek/smi.h>

#include <dt-bindings/memory/mtk-memory-port.h>

#define REG_MMU_PT_BASE_ADDR			0x000

#define REG_MMU_INVALIDATE			0x020
#define F_ALL_INVLD				0x2
#define F_MMU_INV_RANGE				0x1

#define REG_MMU_INVLD_START_A			0x024
#define REG_MMU_INVLD_END_A			0x028

#define REG_MMU_INV_SEL_GEN2			0x02c
#define REG_MMU_INV_SEL_GEN1			0x038
#define F_INVLD_EN0				BIT(0)
#define F_INVLD_EN1				BIT(1)

#define REG_MMU_MISC_CTRL			0x048
#define F_MMU_IN_ORDER_WR_EN_MASK		(BIT(1) | BIT(17))
#define F_MMU_STANDARD_AXI_MODE_MASK		(BIT(3) | BIT(19))

#define REG_MMU_DCM_DIS				0x050
#define F_MMU_DCM				BIT(8)

#define REG_MMU_WR_LEN_CTRL			0x054
#define F_MMU_WR_THROT_DIS_MASK			(BIT(5) | BIT(21))

#define REG_MMU_CTRL_REG			0x110
#define F_MMU_TF_PROT_TO_PROGRAM_ADDR		(2 << 4)
#define F_MMU_PREFETCH_RT_REPLACE_MOD		BIT(4)
#define F_MMU_TF_PROT_TO_PROGRAM_ADDR_MT8173	(2 << 5)

#define REG_MMU_IVRP_PADDR			0x114

#define REG_MMU_VLD_PA_RNG			0x118
#define F_MMU_VLD_PA_RNG(EA, SA)		(((EA) << 8) | (SA))

#define REG_MMU_INT_CONTROL0			0x120
#define F_L2_MULIT_HIT_EN			BIT(0)
#define F_TABLE_WALK_FAULT_INT_EN		BIT(1)
#define F_PREETCH_FIFO_OVERFLOW_INT_EN		BIT(2)
#define F_MISS_FIFO_OVERFLOW_INT_EN		BIT(3)
#define F_PREFETCH_FIFO_ERR_INT_EN		BIT(5)
#define F_MISS_FIFO_ERR_INT_EN			BIT(6)
#define F_INT_CLR_BIT				BIT(12)

#define REG_MMU_INT_MAIN_CONTROL		0x124
						/* mmu0 | mmu1 */
#define F_INT_TRANSLATION_FAULT			(BIT(0) | BIT(7))
#define F_INT_MAIN_MULTI_HIT_FAULT		(BIT(1) | BIT(8))
#define F_INT_INVALID_PA_FAULT			(BIT(2) | BIT(9))
#define F_INT_ENTRY_REPLACEMENT_FAULT		(BIT(3) | BIT(10))
#define F_INT_TLB_MISS_FAULT			(BIT(4) | BIT(11))
#define F_INT_MISS_TRANSACTION_FIFO_FAULT	(BIT(5) | BIT(12))
#define F_INT_PRETETCH_TRANSATION_FIFO_FAULT	(BIT(6) | BIT(13))

#define REG_MMU_CPE_DONE			0x12C

#define REG_MMU_FAULT_ST1			0x134
#define F_REG_MMU0_FAULT_MASK			GENMASK(6, 0)
#define F_REG_MMU1_FAULT_MASK			GENMASK(13, 7)

#define REG_MMU0_FAULT_VA			0x13c
#define F_MMU_INVAL_VA_31_12_MASK		GENMASK(31, 12)
#define F_MMU_INVAL_VA_34_32_MASK		GENMASK(11, 9)
#define F_MMU_INVAL_PA_34_32_MASK		GENMASK(8, 6)
#define F_MMU_FAULT_VA_WRITE_BIT		BIT(1)
#define F_MMU_FAULT_VA_LAYER_BIT		BIT(0)

#define REG_MMU0_INVLD_PA			0x140
#define REG_MMU1_FAULT_VA			0x144
#define REG_MMU1_INVLD_PA			0x148
#define REG_MMU0_INT_ID				0x150
#define REG_MMU1_INT_ID				0x154
#define F_MMU_INT_ID_COMM_ID(a)			(((a) >> 9) & 0x7)
#define F_MMU_INT_ID_SUB_COMM_ID(a)		(((a) >> 7) & 0x3)
#define F_MMU_INT_ID_COMM_ID_EXT(a)		(((a) >> 10) & 0x7)
#define F_MMU_INT_ID_SUB_COMM_ID_EXT(a)		(((a) >> 7) & 0x7)
/* Macro for 5 bits length port ID field (default) */
#define F_MMU_INT_ID_LARB_ID(a)			(((a) >> 7) & 0x7)
#define F_MMU_INT_ID_PORT_ID(a)			(((a) >> 2) & 0x1f)
/* Macro for 6 bits length port ID field */
#define F_MMU_INT_ID_LARB_ID_WID_6(a)		(((a) >> 8) & 0x7)
#define F_MMU_INT_ID_PORT_ID_WID_6(a)		(((a) >> 2) & 0x3f)

#define MTK_PROTECT_PA_ALIGN			256
#define MTK_IOMMU_BANK_SZ			0x1000

#define PERICFG_IOMMU_1				0x714

#define HAS_4GB_MODE			BIT(0)
/* HW will use the EMI clock if there isn't the "bclk". */
#define HAS_BCLK			BIT(1)
#define HAS_VLD_PA_RNG			BIT(2)
#define RESET_AXI			BIT(3)
#define OUT_ORDER_WR_EN			BIT(4)
#define HAS_SUB_COMM_2BITS		BIT(5)
#define HAS_SUB_COMM_3BITS		BIT(6)
#define WR_THROT_EN			BIT(7)
#define HAS_LEGACY_IVRP_PADDR		BIT(8)
#define IOVA_34_EN			BIT(9)
#define SHARE_PGTABLE			BIT(10) /* 2 HW share pgtable */
#define DCM_DISABLE			BIT(11)
#define STD_AXI_MODE			BIT(12) /* For non MM iommu */
/* 2 bits: iommu type */
#define MTK_IOMMU_TYPE_MM		(0x0 << 13)
#define MTK_IOMMU_TYPE_INFRA		(0x1 << 13)
#define MTK_IOMMU_TYPE_MASK		(0x3 << 13)
/* PM and clock always on. e.g. infra iommu */
#define PM_CLK_AO			BIT(15)
#define IFA_IOMMU_PCIE_SUPPORT		BIT(16)
#define PGTABLE_PA_35_EN		BIT(17)
#define TF_PORT_TO_ADDR_MT8173		BIT(18)
#define INT_ID_PORT_WIDTH_6		BIT(19)
#define CFG_IFA_MASTER_IN_ATF		BIT(20)

#define MTK_IOMMU_HAS_FLAG_MASK(pdata, _x, mask)	\
				((((pdata)->flags) & (mask)) == (_x))

#define MTK_IOMMU_HAS_FLAG(pdata, _x)	MTK_IOMMU_HAS_FLAG_MASK(pdata, _x, _x)
#define MTK_IOMMU_IS_TYPE(pdata, _x)	MTK_IOMMU_HAS_FLAG_MASK(pdata, _x,\
							MTK_IOMMU_TYPE_MASK)

#define MTK_INVALID_LARBID		MTK_LARB_NR_MAX

#define MTK_LARB_COM_MAX	8
#define MTK_LARB_SUBCOM_MAX	8

#define MTK_IOMMU_GROUP_MAX	8
#define MTK_IOMMU_BANK_MAX	5

enum mtk_iommu_plat {
	M4U_MT2712,
	M4U_MT6779,
	M4U_MT6795,
	M4U_MT8167,
	M4U_MT8173,
	M4U_MT8183,
	M4U_MT8186,
	M4U_MT8188,
	M4U_MT8192,
	M4U_MT8195,
	M4U_MT8365,
};

struct mtk_iommu_iova_region {
	dma_addr_t		iova_base;
	unsigned long long	size;
};

struct mtk_iommu_suspend_reg {
	u32			misc_ctrl;
	u32			dcm_dis;
	u32			ctrl_reg;
	u32			vld_pa_rng;
	u32			wr_len_ctrl;

	u32			int_control[MTK_IOMMU_BANK_MAX];
	u32			int_main_control[MTK_IOMMU_BANK_MAX];
	u32			ivrp_paddr[MTK_IOMMU_BANK_MAX];
};

struct mtk_iommu_plat_data {
	enum mtk_iommu_plat	m4u_plat;
	u32			flags;
	u32			inv_sel_reg;

	char			*pericfg_comp_str;
	struct list_head	*hw_list;

	/*
	 * The IOMMU HW may support 16GB iova. In order to balance the IOVA ranges,
	 * different masters will be put in different iova ranges, for example vcodec
	 * is in 4G-8G and cam is in 8G-12G. Meanwhile, some masters may have the
	 * special IOVA range requirement, like CCU can only support the address
	 * 0x40000000-0x44000000.
	 * Here list the iova ranges this SoC supports and which larbs/ports are in
	 * which region.
	 *
	 * 16GB iova all use one pgtable, but each a region is a iommu group.
	 */
	struct {
		unsigned int	iova_region_nr;
		const struct mtk_iommu_iova_region	*iova_region;
		/*
		 * Indicate the correspondance between larbs, ports and regions.
		 *
		 * The index is the same as iova_region and larb port numbers are
		 * described as bit positions.
		 * For example, storing BIT(0) at index 2,1 means "larb 1, port0 is in region 2".
		 *              [2] = { [1] = BIT(0) }
		 */
		const u32	(*iova_region_larb_msk)[MTK_LARB_NR_MAX];
	};

	/*
	 * The IOMMU HW may have 5 banks. Each bank has a independent pgtable.
	 * Here list how many banks this SoC supports/enables and which ports are in which bank.
	 */
	struct {
		u8		banks_num;
		bool		banks_enable[MTK_IOMMU_BANK_MAX];
		unsigned int	banks_portmsk[MTK_IOMMU_BANK_MAX];
	};

	unsigned char       larbid_remap[MTK_LARB_COM_MAX][MTK_LARB_SUBCOM_MAX];
};

struct mtk_iommu_bank_data {
	void __iomem			*base;
	int				irq;
	u8				id;
	struct device			*parent_dev;
	struct mtk_iommu_data		*parent_data;
	spinlock_t			tlb_lock; /* lock for tlb range flush */
	struct mtk_iommu_domain		*m4u_dom; /* Each bank has a domain */
};

struct mtk_iommu_data {
	struct device			*dev;
	struct clk			*bclk;
	phys_addr_t			protect_base; /* protect memory base */
	struct mtk_iommu_suspend_reg	reg;
	struct iommu_group		*m4u_group[MTK_IOMMU_GROUP_MAX];
	bool                            enable_4GB;

	struct iommu_device		iommu;
	const struct mtk_iommu_plat_data *plat_data;
	struct device			*smicomm_dev;

	struct mtk_iommu_bank_data	*bank;
	struct mtk_iommu_domain		*share_dom;

	struct regmap			*pericfg;
	struct mutex			mutex; /* Protect m4u_group/m4u_dom above */

	/*
	 * In the sharing pgtable case, list data->list to the global list like m4ulist.
	 * In the non-sharing pgtable case, list data->list to the itself hw_list_head.
	 */
	struct list_head		*hw_list;
	struct list_head		hw_list_head;
	struct list_head		list;
	struct mtk_smi_larb_iommu	larb_imu[MTK_LARB_NR_MAX];
};

struct mtk_iommu_domain {
	struct io_pgtable_cfg		cfg;
	struct io_pgtable_ops		*iop;

	struct mtk_iommu_bank_data	*bank;
	struct iommu_domain		domain;

	struct mutex			mutex; /* Protect "data" in this structure */
};

static int mtk_iommu_bind(struct device *dev)
{
	struct mtk_iommu_data *data = dev_get_drvdata(dev);

	return component_bind_all(dev, &data->larb_imu);
}

static void mtk_iommu_unbind(struct device *dev)
{
	struct mtk_iommu_data *data = dev_get_drvdata(dev);

	component_unbind_all(dev, &data->larb_imu);
}

static const struct iommu_ops mtk_iommu_ops;

static int mtk_iommu_hw_init(const struct mtk_iommu_data *data, unsigned int bankid);

#define MTK_IOMMU_TLB_ADDR(iova) ({					\
	dma_addr_t _addr = iova;					\
	((lower_32_bits(_addr) & GENMASK(31, 12)) | upper_32_bits(_addr));\
})

/*
 * In M4U 4GB mode, the physical address is remapped as below:
 *
 * CPU Physical address:
 * ====================
 *
 * 0      1G       2G     3G       4G     5G
 * |---A---|---B---|---C---|---D---|---E---|
 * +--I/O--+------------Memory-------------+
 *
 * IOMMU output physical address:
 *  =============================
 *
 *                                 4G      5G     6G      7G      8G
 *                                 |---E---|---B---|---C---|---D---|
 *                                 +------------Memory-------------+
 *
 * The Region 'A'(I/O) can NOT be mapped by M4U; For Region 'B'/'C'/'D', the
 * bit32 of the CPU physical address always is needed to set, and for Region
 * 'E', the CPU physical address keep as is.
 * Additionally, The iommu consumers always use the CPU phyiscal address.
 */
#define MTK_IOMMU_4GB_MODE_REMAP_BASE	 0x140000000UL

static LIST_HEAD(m4ulist);	/* List all the M4U HWs */

#define for_each_m4u(data, head)  list_for_each_entry(data, head, list)

#define MTK_IOMMU_IOVA_SZ_4G		(SZ_4G - SZ_8M) /* 8M as gap */

static const struct mtk_iommu_iova_region single_domain[] = {
	{.iova_base = 0,		.size = MTK_IOMMU_IOVA_SZ_4G},
};

#define MT8192_MULTI_REGION_NR_MAX	6

#define MT8192_MULTI_REGION_NR	(IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT) ? \
				 MT8192_MULTI_REGION_NR_MAX : 1)

static const struct mtk_iommu_iova_region mt8192_multi_dom[MT8192_MULTI_REGION_NR] = {
	{ .iova_base = 0x0,		.size = MTK_IOMMU_IOVA_SZ_4G},	/* 0 ~ 4G,  */
	#if IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT)
	{ .iova_base = SZ_4G,		.size = MTK_IOMMU_IOVA_SZ_4G},	/* 4G ~ 8G */
	{ .iova_base = SZ_4G * 2,	.size = MTK_IOMMU_IOVA_SZ_4G},	/* 8G ~ 12G */
	{ .iova_base = SZ_4G * 3,	.size = MTK_IOMMU_IOVA_SZ_4G},	/* 12G ~ 16G */

	{ .iova_base = 0x240000000ULL,	.size = 0x4000000},	/* CCU0 */
	{ .iova_base = 0x244000000ULL,	.size = 0x4000000},	/* CCU1 */
	#endif
};

/* If 2 M4U share a domain(use the same hwlist), Put the corresponding info in first data.*/
static struct mtk_iommu_data *mtk_iommu_get_frst_data(struct list_head *hwlist)
{
	return list_first_entry(hwlist, struct mtk_iommu_data, list);
}

static struct mtk_iommu_domain *to_mtk_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct mtk_iommu_domain, domain);
}

static void mtk_iommu_tlb_flush_all(struct mtk_iommu_data *data)
{
	/* Tlb flush all always is in bank0. */
	struct mtk_iommu_bank_data *bank = &data->bank[0];
	void __iomem *base = bank->base;
	unsigned long flags;

	spin_lock_irqsave(&bank->tlb_lock, flags);
	writel_relaxed(F_INVLD_EN1 | F_INVLD_EN0, base + data->plat_data->inv_sel_reg);
	writel_relaxed(F_ALL_INVLD, base + REG_MMU_INVALIDATE);
	wmb(); /* Make sure the tlb flush all done */
	spin_unlock_irqrestore(&bank->tlb_lock, flags);
}

static void mtk_iommu_tlb_flush_range_sync(unsigned long iova, size_t size,
					   struct mtk_iommu_bank_data *bank)
{
	struct list_head *head = bank->parent_data->hw_list;
	struct mtk_iommu_bank_data *curbank;
	struct mtk_iommu_data *data;
	bool check_pm_status;
	unsigned long flags;
	void __iomem *base;
	int ret;
	u32 tmp;

	for_each_m4u(data, head) {
		/*
		 * To avoid resume the iommu device frequently when the iommu device
		 * is not active, it doesn't always call pm_runtime_get here, then tlb
		 * flush depends on the tlb flush all in the runtime resume.
		 *
		 * There are 2 special cases:
		 *
		 * Case1: The iommu dev doesn't have power domain but has bclk. This case
		 * should also avoid the tlb flush while the dev is not active to mute
		 * the tlb timeout log. like mt8173.
		 *
		 * Case2: The power/clock of infra iommu is always on, and it doesn't
		 * have the device link with the master devices. This case should avoid
		 * the PM status check.
		 */
		check_pm_status = !MTK_IOMMU_HAS_FLAG(data->plat_data, PM_CLK_AO);

		if (check_pm_status) {
			if (pm_runtime_get_if_in_use(data->dev) <= 0)
				continue;
		}

		curbank = &data->bank[bank->id];
		base = curbank->base;

		spin_lock_irqsave(&curbank->tlb_lock, flags);
		writel_relaxed(F_INVLD_EN1 | F_INVLD_EN0,
			       base + data->plat_data->inv_sel_reg);

		writel_relaxed(MTK_IOMMU_TLB_ADDR(iova), base + REG_MMU_INVLD_START_A);
		writel_relaxed(MTK_IOMMU_TLB_ADDR(iova + size - 1),
			       base + REG_MMU_INVLD_END_A);
		writel_relaxed(F_MMU_INV_RANGE, base + REG_MMU_INVALIDATE);

		/* tlb sync */
		ret = readl_poll_timeout_atomic(base + REG_MMU_CPE_DONE,
						tmp, tmp != 0, 10, 1000);

		/* Clear the CPE status */
		writel_relaxed(0, base + REG_MMU_CPE_DONE);
		spin_unlock_irqrestore(&curbank->tlb_lock, flags);

		if (ret) {
			dev_warn(data->dev,
				 "Partial TLB flush timed out, falling back to full flush\n");
			mtk_iommu_tlb_flush_all(data);
		}

		if (check_pm_status)
			pm_runtime_put(data->dev);
	}
}

static irqreturn_t mtk_iommu_isr(int irq, void *dev_id)
{
	struct mtk_iommu_bank_data *bank = dev_id;
	struct mtk_iommu_data *data = bank->parent_data;
	struct mtk_iommu_domain *dom = bank->m4u_dom;
	unsigned int fault_larb = MTK_INVALID_LARBID, fault_port = 0, sub_comm = 0;
	u32 int_state, regval, va34_32, pa34_32;
	const struct mtk_iommu_plat_data *plat_data = data->plat_data;
	void __iomem *base = bank->base;
	u64 fault_iova, fault_pa;
	bool layer, write;

	/* Read error info from registers */
	int_state = readl_relaxed(base + REG_MMU_FAULT_ST1);
	if (int_state & F_REG_MMU0_FAULT_MASK) {
		regval = readl_relaxed(base + REG_MMU0_INT_ID);
		fault_iova = readl_relaxed(base + REG_MMU0_FAULT_VA);
		fault_pa = readl_relaxed(base + REG_MMU0_INVLD_PA);
	} else {
		regval = readl_relaxed(base + REG_MMU1_INT_ID);
		fault_iova = readl_relaxed(base + REG_MMU1_FAULT_VA);
		fault_pa = readl_relaxed(base + REG_MMU1_INVLD_PA);
	}
	layer = fault_iova & F_MMU_FAULT_VA_LAYER_BIT;
	write = fault_iova & F_MMU_FAULT_VA_WRITE_BIT;
	if (MTK_IOMMU_HAS_FLAG(plat_data, IOVA_34_EN)) {
		va34_32 = FIELD_GET(F_MMU_INVAL_VA_34_32_MASK, fault_iova);
		fault_iova = fault_iova & F_MMU_INVAL_VA_31_12_MASK;
		fault_iova |= (u64)va34_32 << 32;
	}
	pa34_32 = FIELD_GET(F_MMU_INVAL_PA_34_32_MASK, fault_iova);
	fault_pa |= (u64)pa34_32 << 32;

	if (MTK_IOMMU_IS_TYPE(plat_data, MTK_IOMMU_TYPE_MM)) {
		if (MTK_IOMMU_HAS_FLAG(plat_data, HAS_SUB_COMM_2BITS)) {
			fault_larb = F_MMU_INT_ID_COMM_ID(regval);
			sub_comm = F_MMU_INT_ID_SUB_COMM_ID(regval);
			fault_port = F_MMU_INT_ID_PORT_ID(regval);
		} else if (MTK_IOMMU_HAS_FLAG(plat_data, HAS_SUB_COMM_3BITS)) {
			fault_larb = F_MMU_INT_ID_COMM_ID_EXT(regval);
			sub_comm = F_MMU_INT_ID_SUB_COMM_ID_EXT(regval);
			fault_port = F_MMU_INT_ID_PORT_ID(regval);
		} else if (MTK_IOMMU_HAS_FLAG(plat_data, INT_ID_PORT_WIDTH_6)) {
			fault_port = F_MMU_INT_ID_PORT_ID_WID_6(regval);
			fault_larb = F_MMU_INT_ID_LARB_ID_WID_6(regval);
		} else {
			fault_port = F_MMU_INT_ID_PORT_ID(regval);
			fault_larb = F_MMU_INT_ID_LARB_ID(regval);
		}
		fault_larb = data->plat_data->larbid_remap[fault_larb][sub_comm];
	}

	if (!dom || report_iommu_fault(&dom->domain, bank->parent_dev, fault_iova,
			       write ? IOMMU_FAULT_WRITE : IOMMU_FAULT_READ)) {
		dev_err_ratelimited(
			bank->parent_dev,
			"fault type=0x%x iova=0x%llx pa=0x%llx master=0x%x(larb=%d port=%d) layer=%d %s\n",
			int_state, fault_iova, fault_pa, regval, fault_larb, fault_port,
			layer, write ? "write" : "read");
	}

	/* Interrupt clear */
	regval = readl_relaxed(base + REG_MMU_INT_CONTROL0);
	regval |= F_INT_CLR_BIT;
	writel_relaxed(regval, base + REG_MMU_INT_CONTROL0);

	mtk_iommu_tlb_flush_all(data);

	return IRQ_HANDLED;
}

static unsigned int mtk_iommu_get_bank_id(struct device *dev,
					  const struct mtk_iommu_plat_data *plat_data)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	unsigned int i, portmsk = 0, bankid = 0;

	if (plat_data->banks_num == 1)
		return bankid;

	for (i = 0; i < fwspec->num_ids; i++)
		portmsk |= BIT(MTK_M4U_TO_PORT(fwspec->ids[i]));

	for (i = 0; i < plat_data->banks_num && i < MTK_IOMMU_BANK_MAX; i++) {
		if (!plat_data->banks_enable[i])
			continue;

		if (portmsk & plat_data->banks_portmsk[i]) {
			bankid = i;
			break;
		}
	}
	return bankid; /* default is 0 */
}

static int mtk_iommu_get_iova_region_id(struct device *dev,
					const struct mtk_iommu_plat_data *plat_data)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	unsigned int portidmsk = 0, larbid;
	const u32 *rgn_larb_msk;
	int i;

	if (plat_data->iova_region_nr == 1)
		return 0;

	larbid = MTK_M4U_TO_LARB(fwspec->ids[0]);
	for (i = 0; i < fwspec->num_ids; i++)
		portidmsk |= BIT(MTK_M4U_TO_PORT(fwspec->ids[i]));

	for (i = 0; i < plat_data->iova_region_nr; i++) {
		rgn_larb_msk = plat_data->iova_region_larb_msk[i];
		if (!rgn_larb_msk)
			continue;

		if ((rgn_larb_msk[larbid] & portidmsk) == portidmsk)
			return i;
	}

	dev_err(dev, "Can NOT find the region for larb(%d-%x).\n",
		larbid, portidmsk);
	return -EINVAL;
}

static int mtk_iommu_config(struct mtk_iommu_data *data, struct device *dev,
			    bool enable, unsigned int regionid)
{
	struct mtk_smi_larb_iommu    *larb_mmu;
	unsigned int                 larbid, portid;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	const struct mtk_iommu_iova_region *region;
	unsigned long portid_msk = 0;
	struct arm_smccc_res res;
	int i, ret = 0;

	for (i = 0; i < fwspec->num_ids; ++i) {
		portid = MTK_M4U_TO_PORT(fwspec->ids[i]);
		portid_msk |= BIT(portid);
	}

	if (MTK_IOMMU_IS_TYPE(data->plat_data, MTK_IOMMU_TYPE_MM)) {
		/* All ports should be in the same larb. just use 0 here */
		larbid = MTK_M4U_TO_LARB(fwspec->ids[0]);
		larb_mmu = &data->larb_imu[larbid];
		region = data->plat_data->iova_region + regionid;

		for_each_set_bit(portid, &portid_msk, 32)
			larb_mmu->bank[portid] = upper_32_bits(region->iova_base);

		dev_dbg(dev, "%s iommu for larb(%s) port 0x%lx region %d rgn-bank %d.\n",
			enable ? "enable" : "disable", dev_name(larb_mmu->dev),
			portid_msk, regionid, upper_32_bits(region->iova_base));

		if (enable)
			larb_mmu->mmu |= portid_msk;
		else
			larb_mmu->mmu &= ~portid_msk;
	} else if (MTK_IOMMU_IS_TYPE(data->plat_data, MTK_IOMMU_TYPE_INFRA)) {
		if (MTK_IOMMU_HAS_FLAG(data->plat_data, CFG_IFA_MASTER_IN_ATF)) {
			arm_smccc_smc(MTK_SIP_KERNEL_IOMMU_CONTROL,
				      IOMMU_ATF_CMD_CONFIG_INFRA_IOMMU,
				      portid_msk, enable, 0, 0, 0, 0, &res);
			ret = res.a0;
		} else {
			/* PCI dev has only one output id, enable the next writing bit for PCIe */
			if (dev_is_pci(dev)) {
				if (fwspec->num_ids != 1) {
					dev_err(dev, "PCI dev can only have one port.\n");
					return -ENODEV;
				}
				portid_msk |= BIT(portid + 1);
			}

			ret = regmap_update_bits(data->pericfg, PERICFG_IOMMU_1,
						 (u32)portid_msk, enable ? (u32)portid_msk : 0);
		}
		if (ret)
			dev_err(dev, "%s iommu(%s) inframaster 0x%lx fail(%d).\n",
				enable ? "enable" : "disable",
				dev_name(data->dev), portid_msk, ret);
	}
	return ret;
}

static int mtk_iommu_domain_finalise(struct mtk_iommu_domain *dom,
				     struct mtk_iommu_data *data,
				     unsigned int region_id)
{
	struct mtk_iommu_domain	*share_dom = data->share_dom;
	const struct mtk_iommu_iova_region *region;

	/* Share pgtable when 2 MM IOMMU share the pgtable or one IOMMU use multiple iova ranges */
	if (share_dom) {
		dom->iop = share_dom->iop;
		dom->cfg = share_dom->cfg;
		dom->domain.pgsize_bitmap = share_dom->cfg.pgsize_bitmap;
		goto update_iova_region;
	}

	dom->cfg = (struct io_pgtable_cfg) {
		.quirks = IO_PGTABLE_QUIRK_ARM_NS |
			IO_PGTABLE_QUIRK_NO_PERMS |
			IO_PGTABLE_QUIRK_ARM_MTK_EXT,
		.pgsize_bitmap = mtk_iommu_ops.pgsize_bitmap,
		.ias = MTK_IOMMU_HAS_FLAG(data->plat_data, IOVA_34_EN) ? 34 : 32,
		.iommu_dev = data->dev,
	};

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, PGTABLE_PA_35_EN))
		dom->cfg.quirks |= IO_PGTABLE_QUIRK_ARM_MTK_TTBR_EXT;

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, HAS_4GB_MODE))
		dom->cfg.oas = data->enable_4GB ? 33 : 32;
	else
		dom->cfg.oas = 35;

	dom->iop = alloc_io_pgtable_ops(ARM_V7S, &dom->cfg, data);
	if (!dom->iop) {
		dev_err(data->dev, "Failed to alloc io pgtable\n");
		return -ENOMEM;
	}

	/* Update our support page sizes bitmap */
	dom->domain.pgsize_bitmap = dom->cfg.pgsize_bitmap;

	data->share_dom = dom;

update_iova_region:
	/* Update the iova region for this domain */
	region = data->plat_data->iova_region + region_id;
	dom->domain.geometry.aperture_start = region->iova_base;
	dom->domain.geometry.aperture_end = region->iova_base + region->size - 1;
	dom->domain.geometry.force_aperture = true;
	return 0;
}

static struct iommu_domain *mtk_iommu_domain_alloc_paging(struct device *dev)
{
	struct mtk_iommu_domain *dom;

	dom = kzalloc(sizeof(*dom), GFP_KERNEL);
	if (!dom)
		return NULL;
	mutex_init(&dom->mutex);

	return &dom->domain;
}

static void mtk_iommu_domain_free(struct iommu_domain *domain)
{
	kfree(to_mtk_domain(domain));
}

static int mtk_iommu_attach_device(struct iommu_domain *domain,
				   struct device *dev)
{
	struct mtk_iommu_data *data = dev_iommu_priv_get(dev), *frstdata;
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);
	struct list_head *hw_list = data->hw_list;
	struct device *m4udev = data->dev;
	struct mtk_iommu_bank_data *bank;
	unsigned int bankid;
	int ret, region_id;

	region_id = mtk_iommu_get_iova_region_id(dev, data->plat_data);
	if (region_id < 0)
		return region_id;

	bankid = mtk_iommu_get_bank_id(dev, data->plat_data);
	mutex_lock(&dom->mutex);
	if (!dom->bank) {
		/* Data is in the frstdata in sharing pgtable case. */
		frstdata = mtk_iommu_get_frst_data(hw_list);

		mutex_lock(&frstdata->mutex);
		ret = mtk_iommu_domain_finalise(dom, frstdata, region_id);
		mutex_unlock(&frstdata->mutex);
		if (ret) {
			mutex_unlock(&dom->mutex);
			return ret;
		}
		dom->bank = &data->bank[bankid];
	}
	mutex_unlock(&dom->mutex);

	mutex_lock(&data->mutex);
	bank = &data->bank[bankid];
	if (!bank->m4u_dom) { /* Initialize the M4U HW for each a BANK */
		ret = pm_runtime_resume_and_get(m4udev);
		if (ret < 0) {
			dev_err(m4udev, "pm get fail(%d) in attach.\n", ret);
			goto err_unlock;
		}

		ret = mtk_iommu_hw_init(data, bankid);
		if (ret) {
			pm_runtime_put(m4udev);
			goto err_unlock;
		}
		bank->m4u_dom = dom;
		writel(dom->cfg.arm_v7s_cfg.ttbr, bank->base + REG_MMU_PT_BASE_ADDR);

		pm_runtime_put(m4udev);
	}
	mutex_unlock(&data->mutex);

	if (region_id > 0) {
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(34));
		if (ret) {
			dev_err(m4udev, "Failed to set dma_mask for %s(%d).\n", dev_name(dev), ret);
			return ret;
		}
	}

	return mtk_iommu_config(data, dev, true, region_id);

err_unlock:
	mutex_unlock(&data->mutex);
	return ret;
}

static int mtk_iommu_identity_attach(struct iommu_domain *identity_domain,
				     struct device *dev)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	struct mtk_iommu_data *data = dev_iommu_priv_get(dev);

	if (domain == identity_domain || !domain)
		return 0;

	mtk_iommu_config(data, dev, false, 0);
	return 0;
}

static struct iommu_domain_ops mtk_iommu_identity_ops = {
	.attach_dev = mtk_iommu_identity_attach,
};

static struct iommu_domain mtk_iommu_identity_domain = {
	.type = IOMMU_DOMAIN_IDENTITY,
	.ops = &mtk_iommu_identity_ops,
};

static int mtk_iommu_map(struct iommu_domain *domain, unsigned long iova,
			 phys_addr_t paddr, size_t pgsize, size_t pgcount,
			 int prot, gfp_t gfp, size_t *mapped)
{
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);

	/* The "4GB mode" M4U physically can not use the lower remap of Dram. */
	if (dom->bank->parent_data->enable_4GB)
		paddr |= BIT_ULL(32);

	/* Synchronize with the tlb_lock */
	return dom->iop->map_pages(dom->iop, iova, paddr, pgsize, pgcount, prot, gfp, mapped);
}

static size_t mtk_iommu_unmap(struct iommu_domain *domain,
			      unsigned long iova, size_t pgsize, size_t pgcount,
			      struct iommu_iotlb_gather *gather)
{
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);

	iommu_iotlb_gather_add_range(gather, iova, pgsize * pgcount);
	return dom->iop->unmap_pages(dom->iop, iova, pgsize, pgcount, gather);
}

static void mtk_iommu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);

	if (dom->bank)
		mtk_iommu_tlb_flush_all(dom->bank->parent_data);
}

static void mtk_iommu_iotlb_sync(struct iommu_domain *domain,
				 struct iommu_iotlb_gather *gather)
{
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);
	size_t length = gather->end - gather->start + 1;

	mtk_iommu_tlb_flush_range_sync(gather->start, length, dom->bank);
}

static int mtk_iommu_sync_map(struct iommu_domain *domain, unsigned long iova,
			      size_t size)
{
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);

	mtk_iommu_tlb_flush_range_sync(iova, size, dom->bank);
	return 0;
}

static phys_addr_t mtk_iommu_iova_to_phys(struct iommu_domain *domain,
					  dma_addr_t iova)
{
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);
	phys_addr_t pa;

	pa = dom->iop->iova_to_phys(dom->iop, iova);
	if (IS_ENABLED(CONFIG_PHYS_ADDR_T_64BIT) &&
	    dom->bank->parent_data->enable_4GB &&
	    pa >= MTK_IOMMU_4GB_MODE_REMAP_BASE)
		pa &= ~BIT_ULL(32);

	return pa;
}

static struct iommu_device *mtk_iommu_probe_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct mtk_iommu_data *data;
	struct device_link *link;
	struct device *larbdev;
	unsigned int larbid, larbidx, i;

	if (!fwspec || fwspec->ops != &mtk_iommu_ops)
		return ERR_PTR(-ENODEV); /* Not a iommu client device */

	data = dev_iommu_priv_get(dev);

	if (!MTK_IOMMU_IS_TYPE(data->plat_data, MTK_IOMMU_TYPE_MM))
		return &data->iommu;

	/*
	 * Link the consumer device with the smi-larb device(supplier).
	 * The device that connects with each a larb is a independent HW.
	 * All the ports in each a device should be in the same larbs.
	 */
	larbid = MTK_M4U_TO_LARB(fwspec->ids[0]);
	if (larbid >= MTK_LARB_NR_MAX)
		return ERR_PTR(-EINVAL);

	for (i = 1; i < fwspec->num_ids; i++) {
		larbidx = MTK_M4U_TO_LARB(fwspec->ids[i]);
		if (larbid != larbidx) {
			dev_err(dev, "Can only use one larb. Fail@larb%d-%d.\n",
				larbid, larbidx);
			return ERR_PTR(-EINVAL);
		}
	}
	larbdev = data->larb_imu[larbid].dev;
	if (!larbdev)
		return ERR_PTR(-EINVAL);

	link = device_link_add(dev, larbdev,
			       DL_FLAG_PM_RUNTIME | DL_FLAG_STATELESS);
	if (!link)
		dev_err(dev, "Unable to link %s\n", dev_name(larbdev));
	return &data->iommu;
}

static void mtk_iommu_release_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct mtk_iommu_data *data;
	struct device *larbdev;
	unsigned int larbid;

	data = dev_iommu_priv_get(dev);
	if (MTK_IOMMU_IS_TYPE(data->plat_data, MTK_IOMMU_TYPE_MM)) {
		larbid = MTK_M4U_TO_LARB(fwspec->ids[0]);
		larbdev = data->larb_imu[larbid].dev;
		device_link_remove(dev, larbdev);
	}
}

static int mtk_iommu_get_group_id(struct device *dev, const struct mtk_iommu_plat_data *plat_data)
{
	unsigned int bankid;

	/*
	 * If the bank function is enabled, each bank is a iommu group/domain.
	 * Otherwise, each iova region is a iommu group/domain.
	 */
	bankid = mtk_iommu_get_bank_id(dev, plat_data);
	if (bankid)
		return bankid;

	return mtk_iommu_get_iova_region_id(dev, plat_data);
}

static struct iommu_group *mtk_iommu_device_group(struct device *dev)
{
	struct mtk_iommu_data *c_data = dev_iommu_priv_get(dev), *data;
	struct list_head *hw_list = c_data->hw_list;
	struct iommu_group *group;
	int groupid;

	data = mtk_iommu_get_frst_data(hw_list);
	if (!data)
		return ERR_PTR(-ENODEV);

	groupid = mtk_iommu_get_group_id(dev, data->plat_data);
	if (groupid < 0)
		return ERR_PTR(groupid);

	mutex_lock(&data->mutex);
	group = data->m4u_group[groupid];
	if (!group) {
		group = iommu_group_alloc();
		if (!IS_ERR(group))
			data->m4u_group[groupid] = group;
	} else {
		iommu_group_ref_get(group);
	}
	mutex_unlock(&data->mutex);
	return group;
}

static int mtk_iommu_of_xlate(struct device *dev, struct of_phandle_args *args)
{
	struct platform_device *m4updev;

	if (args->args_count != 1) {
		dev_err(dev, "invalid #iommu-cells(%d) property for IOMMU\n",
			args->args_count);
		return -EINVAL;
	}

	if (!dev_iommu_priv_get(dev)) {
		/* Get the m4u device */
		m4updev = of_find_device_by_node(args->np);
		if (WARN_ON(!m4updev))
			return -EINVAL;

		dev_iommu_priv_set(dev, platform_get_drvdata(m4updev));
	}

	return iommu_fwspec_add_ids(dev, args->args, 1);
}

static void mtk_iommu_get_resv_regions(struct device *dev,
				       struct list_head *head)
{
	struct mtk_iommu_data *data = dev_iommu_priv_get(dev);
	unsigned int regionid = mtk_iommu_get_iova_region_id(dev, data->plat_data), i;
	const struct mtk_iommu_iova_region *resv, *curdom;
	struct iommu_resv_region *region;
	int prot = IOMMU_WRITE | IOMMU_READ;

	if ((int)regionid < 0)
		return;
	curdom = data->plat_data->iova_region + regionid;
	for (i = 0; i < data->plat_data->iova_region_nr; i++) {
		resv = data->plat_data->iova_region + i;

		/* Only reserve when the region is inside the current domain */
		if (resv->iova_base <= curdom->iova_base ||
		    resv->iova_base + resv->size >= curdom->iova_base + curdom->size)
			continue;

		region = iommu_alloc_resv_region(resv->iova_base, resv->size,
						 prot, IOMMU_RESV_RESERVED,
						 GFP_KERNEL);
		if (!region)
			return;

		list_add_tail(&region->list, head);
	}
}

static const struct iommu_ops mtk_iommu_ops = {
	.identity_domain = &mtk_iommu_identity_domain,
	.domain_alloc_paging = mtk_iommu_domain_alloc_paging,
	.probe_device	= mtk_iommu_probe_device,
	.release_device	= mtk_iommu_release_device,
	.device_group	= mtk_iommu_device_group,
	.of_xlate	= mtk_iommu_of_xlate,
	.get_resv_regions = mtk_iommu_get_resv_regions,
	.pgsize_bitmap	= SZ_4K | SZ_64K | SZ_1M | SZ_16M,
	.owner		= THIS_MODULE,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= mtk_iommu_attach_device,
		.map_pages	= mtk_iommu_map,
		.unmap_pages	= mtk_iommu_unmap,
		.flush_iotlb_all = mtk_iommu_flush_iotlb_all,
		.iotlb_sync	= mtk_iommu_iotlb_sync,
		.iotlb_sync_map	= mtk_iommu_sync_map,
		.iova_to_phys	= mtk_iommu_iova_to_phys,
		.free		= mtk_iommu_domain_free,
	}
};

static int mtk_iommu_hw_init(const struct mtk_iommu_data *data, unsigned int bankid)
{
	const struct mtk_iommu_bank_data *bankx = &data->bank[bankid];
	const struct mtk_iommu_bank_data *bank0 = &data->bank[0];
	u32 regval;

	/*
	 * Global control settings are in bank0. May re-init these global registers
	 * since no sure if there is bank0 consumers.
	 */
	if (MTK_IOMMU_HAS_FLAG(data->plat_data, TF_PORT_TO_ADDR_MT8173)) {
		regval = F_MMU_PREFETCH_RT_REPLACE_MOD |
			 F_MMU_TF_PROT_TO_PROGRAM_ADDR_MT8173;
	} else {
		regval = readl_relaxed(bank0->base + REG_MMU_CTRL_REG);
		regval |= F_MMU_TF_PROT_TO_PROGRAM_ADDR;
	}
	writel_relaxed(regval, bank0->base + REG_MMU_CTRL_REG);

	if (data->enable_4GB &&
	    MTK_IOMMU_HAS_FLAG(data->plat_data, HAS_VLD_PA_RNG)) {
		/*
		 * If 4GB mode is enabled, the validate PA range is from
		 * 0x1_0000_0000 to 0x1_ffff_ffff. here record bit[32:30].
		 */
		regval = F_MMU_VLD_PA_RNG(7, 4);
		writel_relaxed(regval, bank0->base + REG_MMU_VLD_PA_RNG);
	}
	if (MTK_IOMMU_HAS_FLAG(data->plat_data, DCM_DISABLE))
		writel_relaxed(F_MMU_DCM, bank0->base + REG_MMU_DCM_DIS);
	else
		writel_relaxed(0, bank0->base + REG_MMU_DCM_DIS);

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, WR_THROT_EN)) {
		/* write command throttling mode */
		regval = readl_relaxed(bank0->base + REG_MMU_WR_LEN_CTRL);
		regval &= ~F_MMU_WR_THROT_DIS_MASK;
		writel_relaxed(regval, bank0->base + REG_MMU_WR_LEN_CTRL);
	}

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, RESET_AXI)) {
		/* The register is called STANDARD_AXI_MODE in this case */
		regval = 0;
	} else {
		regval = readl_relaxed(bank0->base + REG_MMU_MISC_CTRL);
		if (!MTK_IOMMU_HAS_FLAG(data->plat_data, STD_AXI_MODE))
			regval &= ~F_MMU_STANDARD_AXI_MODE_MASK;
		if (MTK_IOMMU_HAS_FLAG(data->plat_data, OUT_ORDER_WR_EN))
			regval &= ~F_MMU_IN_ORDER_WR_EN_MASK;
	}
	writel_relaxed(regval, bank0->base + REG_MMU_MISC_CTRL);

	/* Independent settings for each bank */
	regval = F_L2_MULIT_HIT_EN |
		F_TABLE_WALK_FAULT_INT_EN |
		F_PREETCH_FIFO_OVERFLOW_INT_EN |
		F_MISS_FIFO_OVERFLOW_INT_EN |
		F_PREFETCH_FIFO_ERR_INT_EN |
		F_MISS_FIFO_ERR_INT_EN;
	writel_relaxed(regval, bankx->base + REG_MMU_INT_CONTROL0);

	regval = F_INT_TRANSLATION_FAULT |
		F_INT_MAIN_MULTI_HIT_FAULT |
		F_INT_INVALID_PA_FAULT |
		F_INT_ENTRY_REPLACEMENT_FAULT |
		F_INT_TLB_MISS_FAULT |
		F_INT_MISS_TRANSACTION_FIFO_FAULT |
		F_INT_PRETETCH_TRANSATION_FIFO_FAULT;
	writel_relaxed(regval, bankx->base + REG_MMU_INT_MAIN_CONTROL);

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, HAS_LEGACY_IVRP_PADDR))
		regval = (data->protect_base >> 1) | (data->enable_4GB << 31);
	else
		regval = lower_32_bits(data->protect_base) |
			 upper_32_bits(data->protect_base);
	writel_relaxed(regval, bankx->base + REG_MMU_IVRP_PADDR);

	if (devm_request_irq(bankx->parent_dev, bankx->irq, mtk_iommu_isr, 0,
			     dev_name(bankx->parent_dev), (void *)bankx)) {
		writel_relaxed(0, bankx->base + REG_MMU_PT_BASE_ADDR);
		dev_err(bankx->parent_dev, "Failed @ IRQ-%d Request\n", bankx->irq);
		return -ENODEV;
	}

	return 0;
}

static const struct component_master_ops mtk_iommu_com_ops = {
	.bind		= mtk_iommu_bind,
	.unbind		= mtk_iommu_unbind,
};

static int mtk_iommu_mm_dts_parse(struct device *dev, struct component_match **match,
				  struct mtk_iommu_data *data)
{
	struct device_node *larbnode, *frst_avail_smicomm_node = NULL;
	struct platform_device *plarbdev, *pcommdev;
	struct device_link *link;
	int i, larb_nr, ret;

	larb_nr = of_count_phandle_with_args(dev->of_node, "mediatek,larbs", NULL);
	if (larb_nr < 0)
		return larb_nr;
	if (larb_nr == 0 || larb_nr > MTK_LARB_NR_MAX)
		return -EINVAL;

	for (i = 0; i < larb_nr; i++) {
		struct device_node *smicomm_node, *smi_subcomm_node;
		u32 id;

		larbnode = of_parse_phandle(dev->of_node, "mediatek,larbs", i);
		if (!larbnode) {
			ret = -EINVAL;
			goto err_larbdev_put;
		}

		if (!of_device_is_available(larbnode)) {
			of_node_put(larbnode);
			continue;
		}

		ret = of_property_read_u32(larbnode, "mediatek,larb-id", &id);
		if (ret)/* The id is consecutive if there is no this property */
			id = i;
		if (id >= MTK_LARB_NR_MAX) {
			of_node_put(larbnode);
			ret = -EINVAL;
			goto err_larbdev_put;
		}

		plarbdev = of_find_device_by_node(larbnode);
		of_node_put(larbnode);
		if (!plarbdev) {
			ret = -ENODEV;
			goto err_larbdev_put;
		}
		if (data->larb_imu[id].dev) {
			platform_device_put(plarbdev);
			ret = -EEXIST;
			goto err_larbdev_put;
		}
		data->larb_imu[id].dev = &plarbdev->dev;

		if (!plarbdev->dev.driver) {
			ret = -EPROBE_DEFER;
			goto err_larbdev_put;
		}

		/* Get smi-(sub)-common dev from the last larb. */
		smi_subcomm_node = of_parse_phandle(larbnode, "mediatek,smi", 0);
		if (!smi_subcomm_node) {
			ret = -EINVAL;
			goto err_larbdev_put;
		}

		/*
		 * It may have two level smi-common. the node is smi-sub-common if it
		 * has a new mediatek,smi property. otherwise it is smi-commmon.
		 */
		smicomm_node = of_parse_phandle(smi_subcomm_node, "mediatek,smi", 0);
		if (smicomm_node)
			of_node_put(smi_subcomm_node);
		else
			smicomm_node = smi_subcomm_node;

		/*
		 * All the larbs that connect to one IOMMU must connect with the same
		 * smi-common.
		 */
		if (!frst_avail_smicomm_node) {
			frst_avail_smicomm_node = smicomm_node;
		} else if (frst_avail_smicomm_node != smicomm_node) {
			dev_err(dev, "mediatek,smi property is not right @larb%d.", id);
			of_node_put(smicomm_node);
			ret = -EINVAL;
			goto err_larbdev_put;
		} else {
			of_node_put(smicomm_node);
		}

		component_match_add(dev, match, component_compare_dev, &plarbdev->dev);
		platform_device_put(plarbdev);
	}

	if (!frst_avail_smicomm_node)
		return -EINVAL;

	pcommdev = of_find_device_by_node(frst_avail_smicomm_node);
	of_node_put(frst_avail_smicomm_node);
	if (!pcommdev)
		return -ENODEV;
	data->smicomm_dev = &pcommdev->dev;

	link = device_link_add(data->smicomm_dev, dev,
			       DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
	platform_device_put(pcommdev);
	if (!link) {
		dev_err(dev, "Unable to link %s.\n", dev_name(data->smicomm_dev));
		return -EINVAL;
	}
	return 0;

err_larbdev_put:
	for (i = MTK_LARB_NR_MAX - 1; i >= 0; i--) {
		if (!data->larb_imu[i].dev)
			continue;
		put_device(data->larb_imu[i].dev);
	}
	return ret;
}

static int mtk_iommu_probe(struct platform_device *pdev)
{
	struct mtk_iommu_data   *data;
	struct device           *dev = &pdev->dev;
	struct resource         *res;
	resource_size_t		ioaddr;
	struct component_match  *match = NULL;
	struct regmap		*infracfg;
	void                    *protect;
	int                     ret, banks_num, i = 0;
	u32			val;
	char                    *p;
	struct mtk_iommu_bank_data *bank;
	void __iomem		*base;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->dev = dev;
	data->plat_data = of_device_get_match_data(dev);

	/* Protect memory. HW will access here while translation fault.*/
	protect = devm_kzalloc(dev, MTK_PROTECT_PA_ALIGN * 2, GFP_KERNEL);
	if (!protect)
		return -ENOMEM;
	data->protect_base = ALIGN(virt_to_phys(protect), MTK_PROTECT_PA_ALIGN);

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, HAS_4GB_MODE)) {
		infracfg = syscon_regmap_lookup_by_phandle(dev->of_node, "mediatek,infracfg");
		if (IS_ERR(infracfg)) {
			/*
			 * Legacy devicetrees will not specify a phandle to
			 * mediatek,infracfg: in that case, we use the older
			 * way to retrieve a syscon to infra.
			 *
			 * This is for retrocompatibility purposes only, hence
			 * no more compatibles shall be added to this.
			 */
			switch (data->plat_data->m4u_plat) {
			case M4U_MT2712:
				p = "mediatek,mt2712-infracfg";
				break;
			case M4U_MT8173:
				p = "mediatek,mt8173-infracfg";
				break;
			default:
				p = NULL;
			}

			infracfg = syscon_regmap_lookup_by_compatible(p);
			if (IS_ERR(infracfg))
				return PTR_ERR(infracfg);
		}

		ret = regmap_read(infracfg, REG_INFRA_MISC, &val);
		if (ret)
			return ret;
		data->enable_4GB = !!(val & F_DDR_4GB_SUPPORT_EN);
	}

	banks_num = data->plat_data->banks_num;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	if (resource_size(res) < banks_num * MTK_IOMMU_BANK_SZ) {
		dev_err(dev, "banknr %d. res %pR is not enough.\n", banks_num, res);
		return -EINVAL;
	}
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);
	ioaddr = res->start;

	data->bank = devm_kmalloc(dev, banks_num * sizeof(*data->bank), GFP_KERNEL);
	if (!data->bank)
		return -ENOMEM;

	do {
		if (!data->plat_data->banks_enable[i])
			continue;
		bank = &data->bank[i];
		bank->id = i;
		bank->base = base + i * MTK_IOMMU_BANK_SZ;
		bank->m4u_dom = NULL;

		bank->irq = platform_get_irq(pdev, i);
		if (bank->irq < 0)
			return bank->irq;
		bank->parent_dev = dev;
		bank->parent_data = data;
		spin_lock_init(&bank->tlb_lock);
	} while (++i < banks_num);

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, HAS_BCLK)) {
		data->bclk = devm_clk_get(dev, "bclk");
		if (IS_ERR(data->bclk))
			return PTR_ERR(data->bclk);
	}

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, PGTABLE_PA_35_EN)) {
		ret = dma_set_mask(dev, DMA_BIT_MASK(35));
		if (ret) {
			dev_err(dev, "Failed to set dma_mask 35.\n");
			return ret;
		}
	}

	pm_runtime_enable(dev);

	if (MTK_IOMMU_IS_TYPE(data->plat_data, MTK_IOMMU_TYPE_MM)) {
		ret = mtk_iommu_mm_dts_parse(dev, &match, data);
		if (ret) {
			dev_err_probe(dev, ret, "mm dts parse fail\n");
			goto out_runtime_disable;
		}
	} else if (MTK_IOMMU_IS_TYPE(data->plat_data, MTK_IOMMU_TYPE_INFRA) &&
		   !MTK_IOMMU_HAS_FLAG(data->plat_data, CFG_IFA_MASTER_IN_ATF)) {
		p = data->plat_data->pericfg_comp_str;
		data->pericfg = syscon_regmap_lookup_by_compatible(p);
		if (IS_ERR(data->pericfg)) {
			ret = PTR_ERR(data->pericfg);
			goto out_runtime_disable;
		}
	}

	platform_set_drvdata(pdev, data);
	mutex_init(&data->mutex);

	ret = iommu_device_sysfs_add(&data->iommu, dev, NULL,
				     "mtk-iommu.%pa", &ioaddr);
	if (ret)
		goto out_link_remove;

	ret = iommu_device_register(&data->iommu, &mtk_iommu_ops, dev);
	if (ret)
		goto out_sysfs_remove;

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, SHARE_PGTABLE)) {
		list_add_tail(&data->list, data->plat_data->hw_list);
		data->hw_list = data->plat_data->hw_list;
	} else {
		INIT_LIST_HEAD(&data->hw_list_head);
		list_add_tail(&data->list, &data->hw_list_head);
		data->hw_list = &data->hw_list_head;
	}

	if (MTK_IOMMU_IS_TYPE(data->plat_data, MTK_IOMMU_TYPE_MM)) {
		ret = component_master_add_with_match(dev, &mtk_iommu_com_ops, match);
		if (ret)
			goto out_list_del;
	}
	return ret;

out_list_del:
	list_del(&data->list);
	iommu_device_unregister(&data->iommu);
out_sysfs_remove:
	iommu_device_sysfs_remove(&data->iommu);
out_link_remove:
	if (MTK_IOMMU_IS_TYPE(data->plat_data, MTK_IOMMU_TYPE_MM))
		device_link_remove(data->smicomm_dev, dev);
out_runtime_disable:
	pm_runtime_disable(dev);
	return ret;
}

static void mtk_iommu_remove(struct platform_device *pdev)
{
	struct mtk_iommu_data *data = platform_get_drvdata(pdev);
	struct mtk_iommu_bank_data *bank;
	int i;

	iommu_device_sysfs_remove(&data->iommu);
	iommu_device_unregister(&data->iommu);

	list_del(&data->list);

	if (MTK_IOMMU_IS_TYPE(data->plat_data, MTK_IOMMU_TYPE_MM)) {
		device_link_remove(data->smicomm_dev, &pdev->dev);
		component_master_del(&pdev->dev, &mtk_iommu_com_ops);
	}
	pm_runtime_disable(&pdev->dev);
	for (i = 0; i < data->plat_data->banks_num; i++) {
		bank = &data->bank[i];
		if (!bank->m4u_dom)
			continue;
		devm_free_irq(&pdev->dev, bank->irq, bank);
	}
}

static int __maybe_unused mtk_iommu_runtime_suspend(struct device *dev)
{
	struct mtk_iommu_data *data = dev_get_drvdata(dev);
	struct mtk_iommu_suspend_reg *reg = &data->reg;
	void __iomem *base;
	int i = 0;

	base = data->bank[i].base;
	reg->wr_len_ctrl = readl_relaxed(base + REG_MMU_WR_LEN_CTRL);
	reg->misc_ctrl = readl_relaxed(base + REG_MMU_MISC_CTRL);
	reg->dcm_dis = readl_relaxed(base + REG_MMU_DCM_DIS);
	reg->ctrl_reg = readl_relaxed(base + REG_MMU_CTRL_REG);
	reg->vld_pa_rng = readl_relaxed(base + REG_MMU_VLD_PA_RNG);
	do {
		if (!data->plat_data->banks_enable[i])
			continue;
		base = data->bank[i].base;
		reg->int_control[i] = readl_relaxed(base + REG_MMU_INT_CONTROL0);
		reg->int_main_control[i] = readl_relaxed(base + REG_MMU_INT_MAIN_CONTROL);
		reg->ivrp_paddr[i] = readl_relaxed(base + REG_MMU_IVRP_PADDR);
	} while (++i < data->plat_data->banks_num);
	clk_disable_unprepare(data->bclk);
	return 0;
}

static int __maybe_unused mtk_iommu_runtime_resume(struct device *dev)
{
	struct mtk_iommu_data *data = dev_get_drvdata(dev);
	struct mtk_iommu_suspend_reg *reg = &data->reg;
	struct mtk_iommu_domain *m4u_dom;
	void __iomem *base;
	int ret, i = 0;

	ret = clk_prepare_enable(data->bclk);
	if (ret) {
		dev_err(data->dev, "Failed to enable clk(%d) in resume\n", ret);
		return ret;
	}

	/*
	 * Uppon first resume, only enable the clk and return, since the values of the
	 * registers are not yet set.
	 */
	if (!reg->wr_len_ctrl)
		return 0;

	base = data->bank[i].base;
	writel_relaxed(reg->wr_len_ctrl, base + REG_MMU_WR_LEN_CTRL);
	writel_relaxed(reg->misc_ctrl, base + REG_MMU_MISC_CTRL);
	writel_relaxed(reg->dcm_dis, base + REG_MMU_DCM_DIS);
	writel_relaxed(reg->ctrl_reg, base + REG_MMU_CTRL_REG);
	writel_relaxed(reg->vld_pa_rng, base + REG_MMU_VLD_PA_RNG);
	do {
		m4u_dom = data->bank[i].m4u_dom;
		if (!data->plat_data->banks_enable[i] || !m4u_dom)
			continue;
		base = data->bank[i].base;
		writel_relaxed(reg->int_control[i], base + REG_MMU_INT_CONTROL0);
		writel_relaxed(reg->int_main_control[i], base + REG_MMU_INT_MAIN_CONTROL);
		writel_relaxed(reg->ivrp_paddr[i], base + REG_MMU_IVRP_PADDR);
		writel(m4u_dom->cfg.arm_v7s_cfg.ttbr, base + REG_MMU_PT_BASE_ADDR);
	} while (++i < data->plat_data->banks_num);

	/*
	 * Users may allocate dma buffer before they call pm_runtime_get,
	 * in which case it will lack the necessary tlb flush.
	 * Thus, make sure to update the tlb after each PM resume.
	 */
	mtk_iommu_tlb_flush_all(data);
	return 0;
}

static const struct dev_pm_ops mtk_iommu_pm_ops = {
	SET_RUNTIME_PM_OPS(mtk_iommu_runtime_suspend, mtk_iommu_runtime_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
};

static const struct mtk_iommu_plat_data mt2712_data = {
	.m4u_plat     = M4U_MT2712,
	.flags        = HAS_4GB_MODE | HAS_BCLK | HAS_VLD_PA_RNG | SHARE_PGTABLE |
			MTK_IOMMU_TYPE_MM,
	.hw_list      = &m4ulist,
	.inv_sel_reg  = REG_MMU_INV_SEL_GEN1,
	.iova_region  = single_domain,
	.banks_num    = 1,
	.banks_enable = {true},
	.iova_region_nr = ARRAY_SIZE(single_domain),
	.larbid_remap = {{0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}},
};

static const struct mtk_iommu_plat_data mt6779_data = {
	.m4u_plat      = M4U_MT6779,
	.flags         = HAS_SUB_COMM_2BITS | OUT_ORDER_WR_EN | WR_THROT_EN |
			 MTK_IOMMU_TYPE_MM | PGTABLE_PA_35_EN,
	.inv_sel_reg   = REG_MMU_INV_SEL_GEN2,
	.banks_num    = 1,
	.banks_enable = {true},
	.iova_region   = single_domain,
	.iova_region_nr = ARRAY_SIZE(single_domain),
	.larbid_remap  = {{0}, {1}, {2}, {3}, {5}, {7, 8}, {10}, {9}},
};

static const struct mtk_iommu_plat_data mt6795_data = {
	.m4u_plat     = M4U_MT6795,
	.flags	      = HAS_4GB_MODE | HAS_BCLK | RESET_AXI |
			HAS_LEGACY_IVRP_PADDR | MTK_IOMMU_TYPE_MM |
			TF_PORT_TO_ADDR_MT8173,
	.inv_sel_reg  = REG_MMU_INV_SEL_GEN1,
	.banks_num    = 1,
	.banks_enable = {true},
	.iova_region  = single_domain,
	.iova_region_nr = ARRAY_SIZE(single_domain),
	.larbid_remap = {{0}, {1}, {2}, {3}, {4}}, /* Linear mapping. */
};

static const struct mtk_iommu_plat_data mt8167_data = {
	.m4u_plat     = M4U_MT8167,
	.flags        = RESET_AXI | HAS_LEGACY_IVRP_PADDR | MTK_IOMMU_TYPE_MM,
	.inv_sel_reg  = REG_MMU_INV_SEL_GEN1,
	.banks_num    = 1,
	.banks_enable = {true},
	.iova_region  = single_domain,
	.iova_region_nr = ARRAY_SIZE(single_domain),
	.larbid_remap = {{0}, {1}, {2}}, /* Linear mapping. */
};

static const struct mtk_iommu_plat_data mt8173_data = {
	.m4u_plat     = M4U_MT8173,
	.flags	      = HAS_4GB_MODE | HAS_BCLK | RESET_AXI |
			HAS_LEGACY_IVRP_PADDR | MTK_IOMMU_TYPE_MM |
			TF_PORT_TO_ADDR_MT8173,
	.inv_sel_reg  = REG_MMU_INV_SEL_GEN1,
	.banks_num    = 1,
	.banks_enable = {true},
	.iova_region  = single_domain,
	.iova_region_nr = ARRAY_SIZE(single_domain),
	.larbid_remap = {{0}, {1}, {2}, {3}, {4}, {5}}, /* Linear mapping. */
};

static const struct mtk_iommu_plat_data mt8183_data = {
	.m4u_plat     = M4U_MT8183,
	.flags        = RESET_AXI | MTK_IOMMU_TYPE_MM,
	.inv_sel_reg  = REG_MMU_INV_SEL_GEN1,
	.banks_num    = 1,
	.banks_enable = {true},
	.iova_region  = single_domain,
	.iova_region_nr = ARRAY_SIZE(single_domain),
	.larbid_remap = {{0}, {4}, {5}, {6}, {7}, {2}, {3}, {1}},
};

static const unsigned int mt8186_larb_region_msk[MT8192_MULTI_REGION_NR_MAX][MTK_LARB_NR_MAX] = {
	[0] = {~0, ~0, ~0},			/* Region0: all ports for larb0/1/2 */
	[1] = {0, 0, 0, 0, ~0, 0, 0, ~0},	/* Region1: larb4/7 */
	[2] = {0, 0, 0, 0, 0, 0, 0, 0,		/* Region2: larb8/9/11/13/16/17/19/20 */
	       ~0, ~0, 0, ~0, 0, ~(u32)(BIT(9) | BIT(10)), 0, 0,
						/* larb13: the other ports except port9/10 */
	       ~0, ~0, 0, ~0, ~0},
	[3] = {0},
	[4] = {[13] = BIT(9) | BIT(10)},	/* larb13 port9/10 */
	[5] = {[14] = ~0},			/* larb14 */
};

static const struct mtk_iommu_plat_data mt8186_data_mm = {
	.m4u_plat       = M4U_MT8186,
	.flags          = HAS_BCLK | HAS_SUB_COMM_2BITS | OUT_ORDER_WR_EN |
			  WR_THROT_EN | IOVA_34_EN | MTK_IOMMU_TYPE_MM,
	.larbid_remap   = {{0}, {1, MTK_INVALID_LARBID, 8}, {4}, {7}, {2}, {9, 11, 19, 20},
			   {MTK_INVALID_LARBID, 14, 16},
			   {MTK_INVALID_LARBID, 13, MTK_INVALID_LARBID, 17}},
	.inv_sel_reg    = REG_MMU_INV_SEL_GEN2,
	.banks_num      = 1,
	.banks_enable   = {true},
	.iova_region    = mt8192_multi_dom,
	.iova_region_nr = ARRAY_SIZE(mt8192_multi_dom),
	.iova_region_larb_msk = mt8186_larb_region_msk,
};

static const struct mtk_iommu_plat_data mt8188_data_infra = {
	.m4u_plat         = M4U_MT8188,
	.flags            = WR_THROT_EN | DCM_DISABLE | STD_AXI_MODE | PM_CLK_AO |
			    MTK_IOMMU_TYPE_INFRA | IFA_IOMMU_PCIE_SUPPORT |
			    PGTABLE_PA_35_EN | CFG_IFA_MASTER_IN_ATF,
	.inv_sel_reg      = REG_MMU_INV_SEL_GEN2,
	.banks_num        = 1,
	.banks_enable     = {true},
	.iova_region      = single_domain,
	.iova_region_nr   = ARRAY_SIZE(single_domain),
};

static const u32 mt8188_larb_region_msk[MT8192_MULTI_REGION_NR_MAX][MTK_LARB_NR_MAX] = {
	[0] = {~0, ~0, ~0, ~0},               /* Region0: all ports for larb0/1/2/3 */
	[1] = {0, 0, 0, 0, 0, 0, 0, 0,
	       0, 0, 0, 0, 0, 0, 0, 0,
	       0, 0, 0, 0, 0, ~0, ~0, ~0},    /* Region1: larb19(21)/21(22)/23 */
	[2] = {0, 0, 0, 0, ~0, ~0, ~0, ~0,    /* Region2: the other larbs. */
	       ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0,
	       ~0, ~0, ~0, ~0, ~0, 0, 0, 0,
	       0, ~0},
	[3] = {0},
	[4] = {[24] = BIT(0) | BIT(1)},       /* Only larb27(24) port0/1 */
	[5] = {[24] = BIT(2) | BIT(3)},       /* Only larb27(24) port2/3 */
};

static const struct mtk_iommu_plat_data mt8188_data_vdo = {
	.m4u_plat       = M4U_MT8188,
	.flags          = HAS_BCLK | HAS_SUB_COMM_3BITS | OUT_ORDER_WR_EN |
			  WR_THROT_EN | IOVA_34_EN | SHARE_PGTABLE |
			  PGTABLE_PA_35_EN | MTK_IOMMU_TYPE_MM,
	.hw_list        = &m4ulist,
	.inv_sel_reg    = REG_MMU_INV_SEL_GEN2,
	.banks_num      = 1,
	.banks_enable   = {true},
	.iova_region    = mt8192_multi_dom,
	.iova_region_nr = ARRAY_SIZE(mt8192_multi_dom),
	.iova_region_larb_msk = mt8188_larb_region_msk,
	.larbid_remap   = {{2}, {0}, {21}, {0}, {19}, {9, 10,
			   11 /* 11a */, 25 /* 11c */},
			   {13, 0, 29 /* 16b */, 30 /* 17b */, 0}, {5}},
};

static const struct mtk_iommu_plat_data mt8188_data_vpp = {
	.m4u_plat       = M4U_MT8188,
	.flags          = HAS_BCLK | HAS_SUB_COMM_3BITS | OUT_ORDER_WR_EN |
			  WR_THROT_EN | IOVA_34_EN | SHARE_PGTABLE |
			  PGTABLE_PA_35_EN | MTK_IOMMU_TYPE_MM,
	.hw_list        = &m4ulist,
	.inv_sel_reg    = REG_MMU_INV_SEL_GEN2,
	.banks_num      = 1,
	.banks_enable   = {true},
	.iova_region    = mt8192_multi_dom,
	.iova_region_nr = ARRAY_SIZE(mt8192_multi_dom),
	.iova_region_larb_msk = mt8188_larb_region_msk,
	.larbid_remap   = {{1}, {3}, {23}, {7}, {MTK_INVALID_LARBID},
			   {12, 15, 24 /* 11b */}, {14, MTK_INVALID_LARBID,
			   16 /* 16a */, 17 /* 17a */, MTK_INVALID_LARBID,
			   27, 28 /* ccu0 */, MTK_INVALID_LARBID}, {4, 6}},
};

static const unsigned int mt8192_larb_region_msk[MT8192_MULTI_REGION_NR_MAX][MTK_LARB_NR_MAX] = {
	[0] = {~0, ~0},				/* Region0: larb0/1 */
	[1] = {0, 0, 0, 0, ~0, ~0, 0, ~0},	/* Region1: larb4/5/7 */
	[2] = {0, 0, ~0, 0, 0, 0, 0, 0,		/* Region2: larb2/9/11/13/14/16/17/18/19/20 */
	       0, ~0, 0, ~0, 0, ~(u32)(BIT(9) | BIT(10)), ~(u32)(BIT(4) | BIT(5)), 0,
	       ~0, ~0, ~0, ~0, ~0},
	[3] = {0},
	[4] = {[13] = BIT(9) | BIT(10)},	/* larb13 port9/10 */
	[5] = {[14] = BIT(4) | BIT(5)},		/* larb14 port4/5 */
};

static const struct mtk_iommu_plat_data mt8192_data = {
	.m4u_plat       = M4U_MT8192,
	.flags          = HAS_BCLK | HAS_SUB_COMM_2BITS | OUT_ORDER_WR_EN |
			  WR_THROT_EN | IOVA_34_EN | MTK_IOMMU_TYPE_MM,
	.inv_sel_reg    = REG_MMU_INV_SEL_GEN2,
	.banks_num      = 1,
	.banks_enable   = {true},
	.iova_region    = mt8192_multi_dom,
	.iova_region_nr = ARRAY_SIZE(mt8192_multi_dom),
	.iova_region_larb_msk = mt8192_larb_region_msk,
	.larbid_remap   = {{0}, {1}, {4, 5}, {7}, {2}, {9, 11, 19, 20},
			   {0, 14, 16}, {0, 13, 18, 17}},
};

static const struct mtk_iommu_plat_data mt8195_data_infra = {
	.m4u_plat	  = M4U_MT8195,
	.flags            = WR_THROT_EN | DCM_DISABLE | STD_AXI_MODE | PM_CLK_AO |
			    MTK_IOMMU_TYPE_INFRA | IFA_IOMMU_PCIE_SUPPORT,
	.pericfg_comp_str = "mediatek,mt8195-pericfg_ao",
	.inv_sel_reg      = REG_MMU_INV_SEL_GEN2,
	.banks_num	  = 5,
	.banks_enable     = {true, false, false, false, true},
	.banks_portmsk    = {[0] = GENMASK(19, 16),     /* PCIe */
			     [4] = GENMASK(31, 20),     /* USB */
			    },
	.iova_region      = single_domain,
	.iova_region_nr   = ARRAY_SIZE(single_domain),
};

static const unsigned int mt8195_larb_region_msk[MT8192_MULTI_REGION_NR_MAX][MTK_LARB_NR_MAX] = {
	[0] = {~0, ~0, ~0, ~0},               /* Region0: all ports for larb0/1/2/3 */
	[1] = {0, 0, 0, 0, 0, 0, 0, 0,
	       0, 0, 0, 0, 0, 0, 0, 0,
	       0, 0, 0, ~0, ~0, ~0, ~0, ~0,   /* Region1: larb19/20/21/22/23/24 */
	       ~0},
	[2] = {0, 0, 0, 0, ~0, ~0, ~0, ~0,    /* Region2: the other larbs. */
	       ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0,
	       ~0, ~0, 0, 0, 0, 0, 0, 0,
	       0, ~0, ~0, ~0, ~0},
	[3] = {0},
	[4] = {[18] = BIT(0) | BIT(1)},       /* Only larb18 port0/1 */
	[5] = {[18] = BIT(2) | BIT(3)},       /* Only larb18 port2/3 */
};

static const struct mtk_iommu_plat_data mt8195_data_vdo = {
	.m4u_plat	= M4U_MT8195,
	.flags          = HAS_BCLK | HAS_SUB_COMM_2BITS | OUT_ORDER_WR_EN |
			  WR_THROT_EN | IOVA_34_EN | SHARE_PGTABLE | MTK_IOMMU_TYPE_MM,
	.hw_list        = &m4ulist,
	.inv_sel_reg    = REG_MMU_INV_SEL_GEN2,
	.banks_num      = 1,
	.banks_enable   = {true},
	.iova_region	= mt8192_multi_dom,
	.iova_region_nr	= ARRAY_SIZE(mt8192_multi_dom),
	.iova_region_larb_msk = mt8195_larb_region_msk,
	.larbid_remap   = {{2, 0}, {21}, {24}, {7}, {19}, {9, 10, 11},
			   {13, 17, 15/* 17b */, 25}, {5}},
};

static const struct mtk_iommu_plat_data mt8195_data_vpp = {
	.m4u_plat	= M4U_MT8195,
	.flags          = HAS_BCLK | HAS_SUB_COMM_3BITS | OUT_ORDER_WR_EN |
			  WR_THROT_EN | IOVA_34_EN | SHARE_PGTABLE | MTK_IOMMU_TYPE_MM,
	.hw_list        = &m4ulist,
	.inv_sel_reg    = REG_MMU_INV_SEL_GEN2,
	.banks_num      = 1,
	.banks_enable   = {true},
	.iova_region	= mt8192_multi_dom,
	.iova_region_nr	= ARRAY_SIZE(mt8192_multi_dom),
	.iova_region_larb_msk = mt8195_larb_region_msk,
	.larbid_remap   = {{1}, {3},
			   {22, MTK_INVALID_LARBID, MTK_INVALID_LARBID, MTK_INVALID_LARBID, 23},
			   {8}, {20}, {12},
			   /* 16: 16a; 29: 16b; 30: CCUtop0; 31: CCUtop1 */
			   {14, 16, 29, 26, 30, 31, 18},
			   {4, MTK_INVALID_LARBID, MTK_INVALID_LARBID, MTK_INVALID_LARBID, 6}},
};

static const struct mtk_iommu_plat_data mt8365_data = {
	.m4u_plat	= M4U_MT8365,
	.flags		= RESET_AXI | INT_ID_PORT_WIDTH_6,
	.inv_sel_reg	= REG_MMU_INV_SEL_GEN1,
	.banks_num	= 1,
	.banks_enable	= {true},
	.iova_region	= single_domain,
	.iova_region_nr	= ARRAY_SIZE(single_domain),
	.larbid_remap	= {{0}, {1}, {2}, {3}, {4}, {5}}, /* Linear mapping. */
};

static const struct of_device_id mtk_iommu_of_ids[] = {
	{ .compatible = "mediatek,mt2712-m4u", .data = &mt2712_data},
	{ .compatible = "mediatek,mt6779-m4u", .data = &mt6779_data},
	{ .compatible = "mediatek,mt6795-m4u", .data = &mt6795_data},
	{ .compatible = "mediatek,mt8167-m4u", .data = &mt8167_data},
	{ .compatible = "mediatek,mt8173-m4u", .data = &mt8173_data},
	{ .compatible = "mediatek,mt8183-m4u", .data = &mt8183_data},
	{ .compatible = "mediatek,mt8186-iommu-mm",    .data = &mt8186_data_mm}, /* mm: m4u */
	{ .compatible = "mediatek,mt8188-iommu-infra", .data = &mt8188_data_infra},
	{ .compatible = "mediatek,mt8188-iommu-vdo",   .data = &mt8188_data_vdo},
	{ .compatible = "mediatek,mt8188-iommu-vpp",   .data = &mt8188_data_vpp},
	{ .compatible = "mediatek,mt8192-m4u", .data = &mt8192_data},
	{ .compatible = "mediatek,mt8195-iommu-infra", .data = &mt8195_data_infra},
	{ .compatible = "mediatek,mt8195-iommu-vdo",   .data = &mt8195_data_vdo},
	{ .compatible = "mediatek,mt8195-iommu-vpp",   .data = &mt8195_data_vpp},
	{ .compatible = "mediatek,mt8365-m4u", .data = &mt8365_data},
	{}
};

static struct platform_driver mtk_iommu_driver = {
	.probe	= mtk_iommu_probe,
	.remove_new = mtk_iommu_remove,
	.driver	= {
		.name = "mtk-iommu",
		.of_match_table = mtk_iommu_of_ids,
		.pm = &mtk_iommu_pm_ops,
	}
};
module_platform_driver(mtk_iommu_driver);

MODULE_DESCRIPTION("IOMMU API for MediaTek M4U implementations");
MODULE_LICENSE("GPL v2");
