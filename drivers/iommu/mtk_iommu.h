/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Honghui Zhang <honghui.zhang@mediatek.com>
 */

#ifndef _MTK_IOMMU_H_
#define _MTK_IOMMU_H_

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <soc/mediatek/smi.h>

struct mtk_iommu_suspend_reg {
	u32				standard_axi_mode;
	u32				dcm_dis;
	u32				ctrl_reg;
	u32				int_control0;
	u32				int_main_control;
	u32				ivrp_paddr;
	u32				vld_pa_rng;
};

enum mtk_iommu_plat {
	M4U_MT2701,
	M4U_MT2712,
	M4U_MT8173,
	M4U_MT8183,
};

struct mtk_iommu_plat_data {
	enum mtk_iommu_plat m4u_plat;
	bool                has_4gb_mode;

	/* HW will use the EMI clock if there isn't the "bclk". */
	bool                has_bclk;
	bool                has_vld_pa_rng;
	bool                reset_axi;
	unsigned char       larbid_remap[MTK_LARB_NR_MAX];
};

struct mtk_iommu_domain;

struct mtk_iommu_data {
	void __iomem			*base;
	int				irq;
	struct device			*dev;
	struct clk			*bclk;
	phys_addr_t			protect_base; /* protect memory base */
	struct mtk_iommu_suspend_reg	reg;
	struct mtk_iommu_domain		*m4u_dom;
	struct iommu_group		*m4u_group;
	bool                            enable_4GB;
	bool				tlb_flush_active;

	struct iommu_device		iommu;
	const struct mtk_iommu_plat_data *plat_data;

	struct list_head		list;
	struct mtk_smi_larb_iommu	larb_imu[MTK_LARB_NR_MAX];
};

static inline int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static inline void release_of(struct device *dev, void *data)
{
	of_node_put(data);
}

static inline int mtk_iommu_bind(struct device *dev)
{
	struct mtk_iommu_data *data = dev_get_drvdata(dev);

	return component_bind_all(dev, &data->larb_imu);
}

static inline void mtk_iommu_unbind(struct device *dev)
{
	struct mtk_iommu_data *data = dev_get_drvdata(dev);

	component_unbind_all(dev, &data->larb_imu);
}

#endif
