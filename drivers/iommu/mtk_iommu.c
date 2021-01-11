// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Yong Wu <yong.wu@mediatek.com>
 */
#include <linux/bug.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/dma-iommu.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_iommu.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/soc/mediatek/infracfg.h>
#include <asm/barrier.h>
#include <soc/mediatek/smi.h>

#include "mtk_iommu.h"

#define REG_MMU_PT_BASE_ADDR			0x000
#define MMU_PT_ADDR_MASK			GENMASK(31, 7)

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
#define F_MMU_FAULT_VA_WRITE_BIT		BIT(1)
#define F_MMU_FAULT_VA_LAYER_BIT		BIT(0)

#define REG_MMU0_INVLD_PA			0x140
#define REG_MMU1_FAULT_VA			0x144
#define REG_MMU1_INVLD_PA			0x148
#define REG_MMU0_INT_ID				0x150
#define REG_MMU1_INT_ID				0x154
#define F_MMU_INT_ID_COMM_ID(a)			(((a) >> 9) & 0x7)
#define F_MMU_INT_ID_SUB_COMM_ID(a)		(((a) >> 7) & 0x3)
#define F_MMU_INT_ID_LARB_ID(a)			(((a) >> 7) & 0x7)
#define F_MMU_INT_ID_PORT_ID(a)			(((a) >> 2) & 0x1f)

#define MTK_PROTECT_PA_ALIGN			256

#define HAS_4GB_MODE			BIT(0)
/* HW will use the EMI clock if there isn't the "bclk". */
#define HAS_BCLK			BIT(1)
#define HAS_VLD_PA_RNG			BIT(2)
#define RESET_AXI			BIT(3)
#define OUT_ORDER_WR_EN			BIT(4)
#define HAS_SUB_COMM			BIT(5)
#define WR_THROT_EN			BIT(6)
#define HAS_LEGACY_IVRP_PADDR		BIT(7)
#define IOVA_34_EN			BIT(8)

#define MTK_IOMMU_HAS_FLAG(pdata, _x) \
		((((pdata)->flags) & (_x)) == (_x))

struct mtk_iommu_domain {
	struct io_pgtable_cfg		cfg;
	struct io_pgtable_ops		*iop;

	struct iommu_domain		domain;
};

static const struct iommu_ops mtk_iommu_ops;

static int mtk_iommu_hw_init(const struct mtk_iommu_data *data);

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

#define for_each_m4u(data)	list_for_each_entry(data, &m4ulist, list)

/*
 * There may be 1 or 2 M4U HWs, But we always expect they are in the same domain
 * for the performance.
 *
 * Here always return the mtk_iommu_data of the first probed M4U where the
 * iommu domain information is recorded.
 */
static struct mtk_iommu_data *mtk_iommu_get_m4u_data(void)
{
	struct mtk_iommu_data *data;

	for_each_m4u(data)
		return data;

	return NULL;
}

static struct mtk_iommu_domain *to_mtk_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct mtk_iommu_domain, domain);
}

static void mtk_iommu_tlb_flush_all(struct mtk_iommu_data *data)
{
	for_each_m4u(data) {
		writel_relaxed(F_INVLD_EN1 | F_INVLD_EN0,
			       data->base + data->plat_data->inv_sel_reg);
		writel_relaxed(F_ALL_INVLD, data->base + REG_MMU_INVALIDATE);
		wmb(); /* Make sure the tlb flush all done */
	}
}

static void mtk_iommu_tlb_flush_range_sync(unsigned long iova, size_t size,
					   size_t granule,
					   struct mtk_iommu_data *data)
{
	unsigned long flags;
	int ret;
	u32 tmp;

	for_each_m4u(data) {
		spin_lock_irqsave(&data->tlb_lock, flags);
		writel_relaxed(F_INVLD_EN1 | F_INVLD_EN0,
			       data->base + data->plat_data->inv_sel_reg);

		writel_relaxed(iova, data->base + REG_MMU_INVLD_START_A);
		writel_relaxed(iova + size - 1,
			       data->base + REG_MMU_INVLD_END_A);
		writel_relaxed(F_MMU_INV_RANGE,
			       data->base + REG_MMU_INVALIDATE);

		/* tlb sync */
		ret = readl_poll_timeout_atomic(data->base + REG_MMU_CPE_DONE,
						tmp, tmp != 0, 10, 1000);
		if (ret) {
			dev_warn(data->dev,
				 "Partial TLB flush timed out, falling back to full flush\n");
			mtk_iommu_tlb_flush_all(data);
		}
		/* Clear the CPE status */
		writel_relaxed(0, data->base + REG_MMU_CPE_DONE);
		spin_unlock_irqrestore(&data->tlb_lock, flags);
	}
}

static irqreturn_t mtk_iommu_isr(int irq, void *dev_id)
{
	struct mtk_iommu_data *data = dev_id;
	struct mtk_iommu_domain *dom = data->m4u_dom;
	u32 int_state, regval, fault_iova, fault_pa;
	unsigned int fault_larb, fault_port, sub_comm = 0;
	bool layer, write;

	/* Read error info from registers */
	int_state = readl_relaxed(data->base + REG_MMU_FAULT_ST1);
	if (int_state & F_REG_MMU0_FAULT_MASK) {
		regval = readl_relaxed(data->base + REG_MMU0_INT_ID);
		fault_iova = readl_relaxed(data->base + REG_MMU0_FAULT_VA);
		fault_pa = readl_relaxed(data->base + REG_MMU0_INVLD_PA);
	} else {
		regval = readl_relaxed(data->base + REG_MMU1_INT_ID);
		fault_iova = readl_relaxed(data->base + REG_MMU1_FAULT_VA);
		fault_pa = readl_relaxed(data->base + REG_MMU1_INVLD_PA);
	}
	layer = fault_iova & F_MMU_FAULT_VA_LAYER_BIT;
	write = fault_iova & F_MMU_FAULT_VA_WRITE_BIT;
	fault_port = F_MMU_INT_ID_PORT_ID(regval);
	if (MTK_IOMMU_HAS_FLAG(data->plat_data, HAS_SUB_COMM)) {
		fault_larb = F_MMU_INT_ID_COMM_ID(regval);
		sub_comm = F_MMU_INT_ID_SUB_COMM_ID(regval);
	} else {
		fault_larb = F_MMU_INT_ID_LARB_ID(regval);
	}
	fault_larb = data->plat_data->larbid_remap[fault_larb][sub_comm];

	if (report_iommu_fault(&dom->domain, data->dev, fault_iova,
			       write ? IOMMU_FAULT_WRITE : IOMMU_FAULT_READ)) {
		dev_err_ratelimited(
			data->dev,
			"fault type=0x%x iova=0x%x pa=0x%x larb=%d port=%d layer=%d %s\n",
			int_state, fault_iova, fault_pa, fault_larb, fault_port,
			layer, write ? "write" : "read");
	}

	/* Interrupt clear */
	regval = readl_relaxed(data->base + REG_MMU_INT_CONTROL0);
	regval |= F_INT_CLR_BIT;
	writel_relaxed(regval, data->base + REG_MMU_INT_CONTROL0);

	mtk_iommu_tlb_flush_all(data);

	return IRQ_HANDLED;
}

static void mtk_iommu_config(struct mtk_iommu_data *data,
			     struct device *dev, bool enable)
{
	struct mtk_smi_larb_iommu    *larb_mmu;
	unsigned int                 larbid, portid;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	int i;

	for (i = 0; i < fwspec->num_ids; ++i) {
		larbid = MTK_M4U_TO_LARB(fwspec->ids[i]);
		portid = MTK_M4U_TO_PORT(fwspec->ids[i]);
		larb_mmu = &data->larb_imu[larbid];

		dev_dbg(dev, "%s iommu port: %d\n",
			enable ? "enable" : "disable", portid);

		if (enable)
			larb_mmu->mmu |= MTK_SMI_MMU_EN(portid);
		else
			larb_mmu->mmu &= ~MTK_SMI_MMU_EN(portid);
	}
}

static int mtk_iommu_domain_finalise(struct mtk_iommu_domain *dom)
{
	struct mtk_iommu_data *data = mtk_iommu_get_m4u_data();

	dom->cfg = (struct io_pgtable_cfg) {
		.quirks = IO_PGTABLE_QUIRK_ARM_NS |
			IO_PGTABLE_QUIRK_NO_PERMS |
			IO_PGTABLE_QUIRK_ARM_MTK_EXT,
		.pgsize_bitmap = mtk_iommu_ops.pgsize_bitmap,
		.ias = MTK_IOMMU_HAS_FLAG(data->plat_data, IOVA_34_EN) ? 34 : 32,
		.iommu_dev = data->dev,
	};

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, HAS_4GB_MODE))
		dom->cfg.oas = data->enable_4GB ? 33 : 32;
	else
		dom->cfg.oas = 35;

	dom->iop = alloc_io_pgtable_ops(ARM_V7S, &dom->cfg, data);
	if (!dom->iop) {
		dev_err(data->dev, "Failed to alloc io pgtable\n");
		return -EINVAL;
	}

	/* Update our support page sizes bitmap */
	dom->domain.pgsize_bitmap = dom->cfg.pgsize_bitmap;
	return 0;
}

static struct iommu_domain *mtk_iommu_domain_alloc(unsigned type)
{
	struct mtk_iommu_domain *dom;

	if (type != IOMMU_DOMAIN_DMA)
		return NULL;

	dom = kzalloc(sizeof(*dom), GFP_KERNEL);
	if (!dom)
		return NULL;

	if (iommu_get_dma_cookie(&dom->domain))
		goto  free_dom;

	if (mtk_iommu_domain_finalise(dom))
		goto  put_dma_cookie;

	dom->domain.geometry.aperture_start = 0;
	dom->domain.geometry.aperture_end = DMA_BIT_MASK(32);
	dom->domain.geometry.force_aperture = true;

	return &dom->domain;

put_dma_cookie:
	iommu_put_dma_cookie(&dom->domain);
free_dom:
	kfree(dom);
	return NULL;
}

static void mtk_iommu_domain_free(struct iommu_domain *domain)
{
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);

	free_io_pgtable_ops(dom->iop);
	iommu_put_dma_cookie(domain);
	kfree(to_mtk_domain(domain));
}

static int mtk_iommu_attach_device(struct iommu_domain *domain,
				   struct device *dev)
{
	struct mtk_iommu_data *data = dev_iommu_priv_get(dev);
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);
	int ret;

	if (!data)
		return -ENODEV;

	if (!data->m4u_dom) { /* Initialize the M4U HW */
		ret = mtk_iommu_hw_init(data);
		if (ret)
			return ret;
		data->m4u_dom = dom;
		writel(dom->cfg.arm_v7s_cfg.ttbr & MMU_PT_ADDR_MASK,
		       data->base + REG_MMU_PT_BASE_ADDR);
	}

	mtk_iommu_config(data, dev, true);
	return 0;
}

static void mtk_iommu_detach_device(struct iommu_domain *domain,
				    struct device *dev)
{
	struct mtk_iommu_data *data = dev_iommu_priv_get(dev);

	if (!data)
		return;

	mtk_iommu_config(data, dev, false);
}

static int mtk_iommu_map(struct iommu_domain *domain, unsigned long iova,
			 phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);
	struct mtk_iommu_data *data = mtk_iommu_get_m4u_data();

	/* The "4GB mode" M4U physically can not use the lower remap of Dram. */
	if (data->enable_4GB)
		paddr |= BIT_ULL(32);

	/* Synchronize with the tlb_lock */
	return dom->iop->map(dom->iop, iova, paddr, size, prot, gfp);
}

static size_t mtk_iommu_unmap(struct iommu_domain *domain,
			      unsigned long iova, size_t size,
			      struct iommu_iotlb_gather *gather)
{
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);
	unsigned long end = iova + size - 1;

	if (gather->start > iova)
		gather->start = iova;
	if (gather->end < end)
		gather->end = end;
	return dom->iop->unmap(dom->iop, iova, size, gather);
}

static void mtk_iommu_flush_iotlb_all(struct iommu_domain *domain)
{
	mtk_iommu_tlb_flush_all(mtk_iommu_get_m4u_data());
}

static void mtk_iommu_iotlb_sync(struct iommu_domain *domain,
				 struct iommu_iotlb_gather *gather)
{
	struct mtk_iommu_data *data = mtk_iommu_get_m4u_data();
	size_t length = gather->end - gather->start + 1;

	mtk_iommu_tlb_flush_range_sync(gather->start, length, gather->pgsize,
				       data);
}

static void mtk_iommu_sync_map(struct iommu_domain *domain, unsigned long iova,
			       size_t size)
{
	struct mtk_iommu_data *data = mtk_iommu_get_m4u_data();

	mtk_iommu_tlb_flush_range_sync(iova, size, size, data);
}

static phys_addr_t mtk_iommu_iova_to_phys(struct iommu_domain *domain,
					  dma_addr_t iova)
{
	struct mtk_iommu_domain *dom = to_mtk_domain(domain);
	struct mtk_iommu_data *data = mtk_iommu_get_m4u_data();
	phys_addr_t pa;

	pa = dom->iop->iova_to_phys(dom->iop, iova);
	if (data->enable_4GB && pa >= MTK_IOMMU_4GB_MODE_REMAP_BASE)
		pa &= ~BIT_ULL(32);

	return pa;
}

static struct iommu_device *mtk_iommu_probe_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct mtk_iommu_data *data;

	if (!fwspec || fwspec->ops != &mtk_iommu_ops)
		return ERR_PTR(-ENODEV); /* Not a iommu client device */

	data = dev_iommu_priv_get(dev);

	return &data->iommu;
}

static void mtk_iommu_release_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (!fwspec || fwspec->ops != &mtk_iommu_ops)
		return;

	iommu_fwspec_free(dev);
}

static struct iommu_group *mtk_iommu_device_group(struct device *dev)
{
	struct mtk_iommu_data *data = mtk_iommu_get_m4u_data();

	if (!data)
		return ERR_PTR(-ENODEV);

	/* All the client devices are in the same m4u iommu-group */
	if (!data->m4u_group) {
		data->m4u_group = iommu_group_alloc();
		if (IS_ERR(data->m4u_group))
			dev_err(dev, "Failed to allocate M4U IOMMU group\n");
	} else {
		iommu_group_ref_get(data->m4u_group);
	}
	return data->m4u_group;
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

static const struct iommu_ops mtk_iommu_ops = {
	.domain_alloc	= mtk_iommu_domain_alloc,
	.domain_free	= mtk_iommu_domain_free,
	.attach_dev	= mtk_iommu_attach_device,
	.detach_dev	= mtk_iommu_detach_device,
	.map		= mtk_iommu_map,
	.unmap		= mtk_iommu_unmap,
	.flush_iotlb_all = mtk_iommu_flush_iotlb_all,
	.iotlb_sync	= mtk_iommu_iotlb_sync,
	.iotlb_sync_map	= mtk_iommu_sync_map,
	.iova_to_phys	= mtk_iommu_iova_to_phys,
	.probe_device	= mtk_iommu_probe_device,
	.release_device	= mtk_iommu_release_device,
	.device_group	= mtk_iommu_device_group,
	.of_xlate	= mtk_iommu_of_xlate,
	.pgsize_bitmap	= SZ_4K | SZ_64K | SZ_1M | SZ_16M,
};

static int mtk_iommu_hw_init(const struct mtk_iommu_data *data)
{
	u32 regval;
	int ret;

	ret = clk_prepare_enable(data->bclk);
	if (ret) {
		dev_err(data->dev, "Failed to enable iommu bclk(%d)\n", ret);
		return ret;
	}

	if (data->plat_data->m4u_plat == M4U_MT8173) {
		regval = F_MMU_PREFETCH_RT_REPLACE_MOD |
			 F_MMU_TF_PROT_TO_PROGRAM_ADDR_MT8173;
	} else {
		regval = readl_relaxed(data->base + REG_MMU_CTRL_REG);
		regval |= F_MMU_TF_PROT_TO_PROGRAM_ADDR;
	}
	writel_relaxed(regval, data->base + REG_MMU_CTRL_REG);

	regval = F_L2_MULIT_HIT_EN |
		F_TABLE_WALK_FAULT_INT_EN |
		F_PREETCH_FIFO_OVERFLOW_INT_EN |
		F_MISS_FIFO_OVERFLOW_INT_EN |
		F_PREFETCH_FIFO_ERR_INT_EN |
		F_MISS_FIFO_ERR_INT_EN;
	writel_relaxed(regval, data->base + REG_MMU_INT_CONTROL0);

	regval = F_INT_TRANSLATION_FAULT |
		F_INT_MAIN_MULTI_HIT_FAULT |
		F_INT_INVALID_PA_FAULT |
		F_INT_ENTRY_REPLACEMENT_FAULT |
		F_INT_TLB_MISS_FAULT |
		F_INT_MISS_TRANSACTION_FIFO_FAULT |
		F_INT_PRETETCH_TRANSATION_FIFO_FAULT;
	writel_relaxed(regval, data->base + REG_MMU_INT_MAIN_CONTROL);

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, HAS_LEGACY_IVRP_PADDR))
		regval = (data->protect_base >> 1) | (data->enable_4GB << 31);
	else
		regval = lower_32_bits(data->protect_base) |
			 upper_32_bits(data->protect_base);
	writel_relaxed(regval, data->base + REG_MMU_IVRP_PADDR);

	if (data->enable_4GB &&
	    MTK_IOMMU_HAS_FLAG(data->plat_data, HAS_VLD_PA_RNG)) {
		/*
		 * If 4GB mode is enabled, the validate PA range is from
		 * 0x1_0000_0000 to 0x1_ffff_ffff. here record bit[32:30].
		 */
		regval = F_MMU_VLD_PA_RNG(7, 4);
		writel_relaxed(regval, data->base + REG_MMU_VLD_PA_RNG);
	}
	writel_relaxed(0, data->base + REG_MMU_DCM_DIS);
	if (MTK_IOMMU_HAS_FLAG(data->plat_data, WR_THROT_EN)) {
		/* write command throttling mode */
		regval = readl_relaxed(data->base + REG_MMU_WR_LEN_CTRL);
		regval &= ~F_MMU_WR_THROT_DIS_MASK;
		writel_relaxed(regval, data->base + REG_MMU_WR_LEN_CTRL);
	}

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, RESET_AXI)) {
		/* The register is called STANDARD_AXI_MODE in this case */
		regval = 0;
	} else {
		regval = readl_relaxed(data->base + REG_MMU_MISC_CTRL);
		regval &= ~F_MMU_STANDARD_AXI_MODE_MASK;
		if (MTK_IOMMU_HAS_FLAG(data->plat_data, OUT_ORDER_WR_EN))
			regval &= ~F_MMU_IN_ORDER_WR_EN_MASK;
	}
	writel_relaxed(regval, data->base + REG_MMU_MISC_CTRL);

	if (devm_request_irq(data->dev, data->irq, mtk_iommu_isr, 0,
			     dev_name(data->dev), (void *)data)) {
		writel_relaxed(0, data->base + REG_MMU_PT_BASE_ADDR);
		clk_disable_unprepare(data->bclk);
		dev_err(data->dev, "Failed @ IRQ-%d Request\n", data->irq);
		return -ENODEV;
	}

	return 0;
}

static const struct component_master_ops mtk_iommu_com_ops = {
	.bind		= mtk_iommu_bind,
	.unbind		= mtk_iommu_unbind,
};

static int mtk_iommu_probe(struct platform_device *pdev)
{
	struct mtk_iommu_data   *data;
	struct device           *dev = &pdev->dev;
	struct resource         *res;
	resource_size_t		ioaddr;
	struct component_match  *match = NULL;
	struct regmap		*infracfg;
	void                    *protect;
	int                     i, larb_nr, ret;
	u32			val;
	char                    *p;

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

		ret = regmap_read(infracfg, REG_INFRA_MISC, &val);
		if (ret)
			return ret;
		data->enable_4GB = !!(val & F_DDR_4GB_SUPPORT_EN);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);
	ioaddr = res->start;

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0)
		return data->irq;

	if (MTK_IOMMU_HAS_FLAG(data->plat_data, HAS_BCLK)) {
		data->bclk = devm_clk_get(dev, "bclk");
		if (IS_ERR(data->bclk))
			return PTR_ERR(data->bclk);
	}

	larb_nr = of_count_phandle_with_args(dev->of_node,
					     "mediatek,larbs", NULL);
	if (larb_nr < 0)
		return larb_nr;

	for (i = 0; i < larb_nr; i++) {
		struct device_node *larbnode;
		struct platform_device *plarbdev;
		u32 id;

		larbnode = of_parse_phandle(dev->of_node, "mediatek,larbs", i);
		if (!larbnode)
			return -EINVAL;

		if (!of_device_is_available(larbnode)) {
			of_node_put(larbnode);
			continue;
		}

		ret = of_property_read_u32(larbnode, "mediatek,larb-id", &id);
		if (ret)/* The id is consecutive if there is no this property */
			id = i;

		plarbdev = of_find_device_by_node(larbnode);
		if (!plarbdev) {
			of_node_put(larbnode);
			return -EPROBE_DEFER;
		}
		data->larb_imu[id].dev = &plarbdev->dev;

		component_match_add_release(dev, &match, release_of,
					    compare_of, larbnode);
	}

	platform_set_drvdata(pdev, data);

	ret = iommu_device_sysfs_add(&data->iommu, dev, NULL,
				     "mtk-iommu.%pa", &ioaddr);
	if (ret)
		return ret;

	iommu_device_set_ops(&data->iommu, &mtk_iommu_ops);
	iommu_device_set_fwnode(&data->iommu, &pdev->dev.of_node->fwnode);

	ret = iommu_device_register(&data->iommu);
	if (ret)
		goto out_sysfs_remove;

	spin_lock_init(&data->tlb_lock);
	list_add_tail(&data->list, &m4ulist);

	if (!iommu_present(&platform_bus_type)) {
		ret = bus_set_iommu(&platform_bus_type, &mtk_iommu_ops);
		if (ret)
			goto out_list_del;
	}

	ret = component_master_add_with_match(dev, &mtk_iommu_com_ops, match);
	if (ret)
		goto out_bus_set_null;
	return ret;

out_bus_set_null:
	bus_set_iommu(&platform_bus_type, NULL);
out_list_del:
	list_del(&data->list);
	iommu_device_unregister(&data->iommu);
out_sysfs_remove:
	iommu_device_sysfs_remove(&data->iommu);
	return ret;
}

static int mtk_iommu_remove(struct platform_device *pdev)
{
	struct mtk_iommu_data *data = platform_get_drvdata(pdev);

	iommu_device_sysfs_remove(&data->iommu);
	iommu_device_unregister(&data->iommu);

	if (iommu_present(&platform_bus_type))
		bus_set_iommu(&platform_bus_type, NULL);

	clk_disable_unprepare(data->bclk);
	devm_free_irq(&pdev->dev, data->irq, data);
	component_master_del(&pdev->dev, &mtk_iommu_com_ops);
	return 0;
}

static int __maybe_unused mtk_iommu_suspend(struct device *dev)
{
	struct mtk_iommu_data *data = dev_get_drvdata(dev);
	struct mtk_iommu_suspend_reg *reg = &data->reg;
	void __iomem *base = data->base;

	reg->wr_len_ctrl = readl_relaxed(base + REG_MMU_WR_LEN_CTRL);
	reg->misc_ctrl = readl_relaxed(base + REG_MMU_MISC_CTRL);
	reg->dcm_dis = readl_relaxed(base + REG_MMU_DCM_DIS);
	reg->ctrl_reg = readl_relaxed(base + REG_MMU_CTRL_REG);
	reg->int_control0 = readl_relaxed(base + REG_MMU_INT_CONTROL0);
	reg->int_main_control = readl_relaxed(base + REG_MMU_INT_MAIN_CONTROL);
	reg->ivrp_paddr = readl_relaxed(base + REG_MMU_IVRP_PADDR);
	reg->vld_pa_rng = readl_relaxed(base + REG_MMU_VLD_PA_RNG);
	clk_disable_unprepare(data->bclk);
	return 0;
}

static int __maybe_unused mtk_iommu_resume(struct device *dev)
{
	struct mtk_iommu_data *data = dev_get_drvdata(dev);
	struct mtk_iommu_suspend_reg *reg = &data->reg;
	struct mtk_iommu_domain *m4u_dom = data->m4u_dom;
	void __iomem *base = data->base;
	int ret;

	ret = clk_prepare_enable(data->bclk);
	if (ret) {
		dev_err(data->dev, "Failed to enable clk(%d) in resume\n", ret);
		return ret;
	}
	writel_relaxed(reg->wr_len_ctrl, base + REG_MMU_WR_LEN_CTRL);
	writel_relaxed(reg->misc_ctrl, base + REG_MMU_MISC_CTRL);
	writel_relaxed(reg->dcm_dis, base + REG_MMU_DCM_DIS);
	writel_relaxed(reg->ctrl_reg, base + REG_MMU_CTRL_REG);
	writel_relaxed(reg->int_control0, base + REG_MMU_INT_CONTROL0);
	writel_relaxed(reg->int_main_control, base + REG_MMU_INT_MAIN_CONTROL);
	writel_relaxed(reg->ivrp_paddr, base + REG_MMU_IVRP_PADDR);
	writel_relaxed(reg->vld_pa_rng, base + REG_MMU_VLD_PA_RNG);
	if (m4u_dom)
		writel(m4u_dom->cfg.arm_v7s_cfg.ttbr & MMU_PT_ADDR_MASK,
		       base + REG_MMU_PT_BASE_ADDR);
	return 0;
}

static const struct dev_pm_ops mtk_iommu_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(mtk_iommu_suspend, mtk_iommu_resume)
};

static const struct mtk_iommu_plat_data mt2712_data = {
	.m4u_plat     = M4U_MT2712,
	.flags        = HAS_4GB_MODE | HAS_BCLK | HAS_VLD_PA_RNG,
	.inv_sel_reg  = REG_MMU_INV_SEL_GEN1,
	.larbid_remap = {{0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}},
};

static const struct mtk_iommu_plat_data mt6779_data = {
	.m4u_plat      = M4U_MT6779,
	.flags         = HAS_SUB_COMM | OUT_ORDER_WR_EN | WR_THROT_EN,
	.inv_sel_reg   = REG_MMU_INV_SEL_GEN2,
	.larbid_remap  = {{0}, {1}, {2}, {3}, {5}, {7, 8}, {10}, {9}},
};

static const struct mtk_iommu_plat_data mt8167_data = {
	.m4u_plat     = M4U_MT8167,
	.flags        = RESET_AXI | HAS_LEGACY_IVRP_PADDR,
	.inv_sel_reg  = REG_MMU_INV_SEL_GEN1,
	.larbid_remap = {{0}, {1}, {2}}, /* Linear mapping. */
};

static const struct mtk_iommu_plat_data mt8173_data = {
	.m4u_plat     = M4U_MT8173,
	.flags	      = HAS_4GB_MODE | HAS_BCLK | RESET_AXI |
			HAS_LEGACY_IVRP_PADDR,
	.inv_sel_reg  = REG_MMU_INV_SEL_GEN1,
	.larbid_remap = {{0}, {1}, {2}, {3}, {4}, {5}}, /* Linear mapping. */
};

static const struct mtk_iommu_plat_data mt8183_data = {
	.m4u_plat     = M4U_MT8183,
	.flags        = RESET_AXI,
	.inv_sel_reg  = REG_MMU_INV_SEL_GEN1,
	.larbid_remap = {{0}, {4}, {5}, {6}, {7}, {2}, {3}, {1}},
};

static const struct of_device_id mtk_iommu_of_ids[] = {
	{ .compatible = "mediatek,mt2712-m4u", .data = &mt2712_data},
	{ .compatible = "mediatek,mt6779-m4u", .data = &mt6779_data},
	{ .compatible = "mediatek,mt8167-m4u", .data = &mt8167_data},
	{ .compatible = "mediatek,mt8173-m4u", .data = &mt8173_data},
	{ .compatible = "mediatek,mt8183-m4u", .data = &mt8183_data},
	{}
};

static struct platform_driver mtk_iommu_driver = {
	.probe	= mtk_iommu_probe,
	.remove	= mtk_iommu_remove,
	.driver	= {
		.name = "mtk-iommu",
		.of_match_table = mtk_iommu_of_ids,
		.pm = &mtk_iommu_pm_ops,
	}
};

static int __init mtk_iommu_init(void)
{
	int ret;

	ret = platform_driver_register(&mtk_iommu_driver);
	if (ret != 0)
		pr_err("Failed to register MTK IOMMU driver\n");

	return ret;
}

subsys_initcall(mtk_iommu_init)
