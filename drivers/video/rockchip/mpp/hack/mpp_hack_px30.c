// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>

#include <soc/rockchip/pm_domains.h>

#include "../mpp_debug.h"
#include "../mpp_common.h"
#include "../mpp_iommu.h"
#include "mpp_hack_px30.h"

#define RK_MMU_DTE_ADDR			0x00 /* Directory table address */
#define RK_MMU_STATUS			0x04
#define RK_MMU_COMMAND			0x08
#define RK_MMU_INT_MASK			0x1C /* IRQ enable */

/* RK_MMU_COMMAND command values */
#define RK_MMU_CMD_ENABLE_PAGING	0 /* Enable memory translation */
#define RK_MMU_CMD_DISABLE_PAGING	1 /* Disable memory translation */
#define RK_MMU_CMD_ENABLE_STALL		2 /* Stall paging to allow other cmds */
#define RK_MMU_CMD_DISABLE_STALL	3 /* Stop stall re-enables paging */
#define RK_MMU_CMD_ZAP_CACHE		4 /* Shoot down entire IOTLB */
#define RK_MMU_CMD_PAGE_FAULT_DONE	5 /* Clear page fault */
#define RK_MMU_CMD_FORCE_RESET		6 /* Reset all registers */

/* RK_MMU_INT_* register fields */
#define RK_MMU_IRQ_MASK			0x03
/* RK_MMU_STATUS fields */
#define RK_MMU_STATUS_PAGING_ENABLED	BIT(0)
#define RK_MMU_STATUS_STALL_ACTIVE	BIT(2)

static bool mpp_iommu_is_paged(struct mpp_rk_iommu *iommu)
{
	int i;
	u32 status;
	bool active = true;

	for (i = 0; i < iommu->mmu_num; i++) {
		status = readl(iommu->bases[i] + RK_MMU_STATUS);
		active &= !!(status & RK_MMU_STATUS_PAGING_ENABLED);
	}

	return active;
}

static u32 mpp_iommu_get_dte_addr(struct mpp_rk_iommu *iommu)
{
	return readl(iommu->bases[0] + RK_MMU_DTE_ADDR);
}

static int mpp_iommu_enable(struct mpp_rk_iommu *iommu)
{
	int i;

	/* check iommu whether is paged */
	iommu->is_paged = mpp_iommu_is_paged(iommu);
	if (iommu->is_paged)
		return 0;

	/* enable stall */
	for (i = 0; i < iommu->mmu_num; i++)
		writel(RK_MMU_CMD_ENABLE_STALL,
		       iommu->bases[i] + RK_MMU_COMMAND);
	udelay(2);
	/* force reset */
	for (i = 0; i < iommu->mmu_num; i++)
		writel(RK_MMU_CMD_FORCE_RESET,
		       iommu->bases[i] + RK_MMU_COMMAND);
	udelay(2);

	for (i = 0; i < iommu->mmu_num; i++) {
		/* restore dte and status */
		writel(iommu->dte_addr,
		       iommu->bases[i] + RK_MMU_DTE_ADDR);
		/* zap cache */
		writel(RK_MMU_CMD_ZAP_CACHE,
		       iommu->bases[i] + RK_MMU_COMMAND);
		/* irq mask */
		writel(RK_MMU_IRQ_MASK,
		       iommu->bases[i] + RK_MMU_INT_MASK);
	}
	udelay(2);
	/* enable paging */
	for (i = 0; i < iommu->mmu_num; i++)
		writel(RK_MMU_CMD_ENABLE_PAGING,
		       iommu->bases[i] + RK_MMU_COMMAND);
	udelay(2);
	/* disable stall */
	for (i = 0; i < iommu->mmu_num; i++)
		writel(RK_MMU_CMD_DISABLE_STALL,
		       iommu->bases[i] + RK_MMU_COMMAND);
	udelay(2);

	/* iommu should be paging enable */
	iommu->is_paged = mpp_iommu_is_paged(iommu);
	if (!iommu->is_paged) {
		mpp_err("iommu->base_addr=%08x enable failed\n",
			iommu->base_addr[0]);
		return -EINVAL;
	}

	return 0;
}

static int mpp_iommu_disable(struct mpp_rk_iommu *iommu)
{
	int i;
	u32 dte;

	if (iommu->is_paged) {
		dte = readl(iommu->bases[0] + RK_MMU_DTE_ADDR);
		if (!dte)
			return -EINVAL;
		udelay(2);
		/* enable stall */
		for (i = 0; i < iommu->mmu_num; i++)
			writel(RK_MMU_CMD_ENABLE_STALL,
			       iommu->bases[i] + RK_MMU_COMMAND);
		udelay(2);
		/* disable paging */
		for (i = 0; i < iommu->mmu_num; i++)
			writel(RK_MMU_CMD_DISABLE_PAGING,
			       iommu->bases[i] + RK_MMU_COMMAND);
		udelay(2);
		/* disable stall */
		for (i = 0; i < iommu->mmu_num; i++)
			writel(RK_MMU_CMD_DISABLE_STALL,
			       iommu->bases[i] + RK_MMU_COMMAND);
		udelay(2);
	}

	return 0;
}

int px30_workaround_combo_init(struct mpp_dev *mpp)
{
	struct mpp_rk_iommu *iommu = NULL, *loop = NULL, *n;
	struct platform_device *pdev = mpp->iommu_info->pdev;

	/* find whether exist in iommu link */
	list_for_each_entry_safe(loop, n, &mpp->queue->mmu_list, link) {
		if (loop->base_addr[0] == pdev->resource[0].start) {
			iommu = loop;
			break;
		}
	}
	/* if not exist, add it */
	if (!iommu) {
		int i;
		struct resource *res;
		void __iomem *base;

		iommu = devm_kzalloc(mpp->srv->dev, sizeof(*iommu), GFP_KERNEL);
		for (i = 0; i < pdev->num_resources; i++) {
			res = platform_get_resource(pdev, IORESOURCE_MEM, i);
			if (!res)
				continue;
			base = devm_ioremap(&pdev->dev,
					    res->start, resource_size(res));
			if (IS_ERR(base))
				continue;
			iommu->base_addr[i] = res->start;
			iommu->bases[i] = base;
			iommu->mmu_num++;
		}
		iommu->grf_val = mpp->grf_info->val & MPP_GRF_VAL_MASK;
		if (mpp->hw_ops->clk_on)
			mpp->hw_ops->clk_on(mpp);
		iommu->dte_addr =  mpp_iommu_get_dte_addr(iommu);
		if (mpp->hw_ops->clk_off)
			mpp->hw_ops->clk_off(mpp);
		INIT_LIST_HEAD(&iommu->link);
		mutex_lock(&mpp->queue->mmu_lock);
		list_add_tail(&iommu->link, &mpp->queue->mmu_list);
		mutex_unlock(&mpp->queue->mmu_lock);
	}
	mpp->iommu_info->iommu = iommu;

	return 0;
}

int px30_workaround_combo_switch_grf(struct mpp_dev *mpp)
{
	int ret = 0;
	u32 curr_val;
	u32 next_val;
	bool pd_is_on;
	struct mpp_rk_iommu *loop = NULL, *n;

	if (!mpp->grf_info->grf || !mpp->grf_info->val)
		return 0;

	curr_val = mpp_get_grf(mpp->grf_info);
	next_val = mpp->grf_info->val & MPP_GRF_VAL_MASK;
	if (curr_val == next_val)
		return 0;

	pd_is_on = rockchip_pmu_pd_is_on(mpp->dev);
	if (!pd_is_on)
		rockchip_pmu_pd_on(mpp->dev);
	mpp->hw_ops->clk_on(mpp);

	list_for_each_entry_safe(loop, n, &mpp->queue->mmu_list, link) {
		/* update iommu parameters */
		if (loop->grf_val == curr_val)
			loop->is_paged = mpp_iommu_is_paged(loop);
		/* disable all iommu */
		mpp_iommu_disable(loop);
	}
	mpp_set_grf(mpp->grf_info);
	/* enable current iommu */
	ret = mpp_iommu_enable(mpp->iommu_info->iommu);

	mpp->hw_ops->clk_off(mpp);
	if (!pd_is_on)
		rockchip_pmu_pd_off(mpp->dev);

	return ret;
}
