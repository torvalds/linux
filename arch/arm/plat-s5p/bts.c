/* linux/arch/arm/plat-s5p/bts.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/clk.h>
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
#include <linux/pm_runtime.h>
#include <plat/pd.h>
#endif
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/bts.h>
#include <mach/map.h>

/* BTS register */
#define BTS_CONTROL 0x0
#define BTS_SHAPING_ON_OFF_REG0 0x4
#define BTS_MASTER_PRIORITY 0x8
#define BTS_SHAPING_ON_OFF_REG2 0xc
#define BTS_SHAPING_ON_OFF_REG1 0x44
#define BTS_DEBLOCKING_SOURCE_SELECTION 0x50

/* FBM register */
#define FBM_MODESEL0 0x0
#define FBM_THRESHOLDSEL0 0x40

/* BTS priority values */
#define BTS_PRIOR_HARDTIME 15
#define BTS_PRIOR_BESTEFFORT 8

/* Fields of BTS_CONTROL register */
#define BTS_ON_OFF (1<<0)
#define BLOCKING_ON_OFF (1<<2)
#define FLEXIBLE_ON_OFF (1<<7)

/* Fields of FLEXIBLE_BLOCKING_CONTROL register */
#define SEL_GRP0 (1<<0)
#define SEL_LEFT0 (1<<4)
#define SEL_RIGHT0 (1<<5)
#define SEL_GRP1 (1<<8)
#define SEL_LEFT1 (1<<12)
#define SEL_RIGHT1 (1<<13)
#define SEL_GRP2 (1<<16)
#define SEL_LEFT2 (1<<20)
#define SEL_RIGHT2 (1<<21)

/* Fields of FBM MODESEL0 register */
#define RD_COUNTER 0
#define WT_COUNTER 1
#define RDWT_COUNTER 2

/* Values of FBM THRESHOLDSEL0 register */
#define FBM_THR_HARDTIME 0x3
#define FBM_THR_BE 0x4

/* Shaping Value for Low priority */
#define LOW_SHAPING_VAL0 0x100010
#define LOW_SHAPING_VAL1 0x3ff
#define LOW_SHAPING_VAL2_BE 0x200
#define LOW_SHAPING_VAL2_HARDTIME 0x3
#define MASTER_PRIOR_BE_NUMBER (1<<16)
#define MASTER_PRIOR_HARDTIME_NUMBER (3<<16)

static LIST_HEAD(fbm_list);
static LIST_HEAD(bts_list);

struct exynos_bts_local_data {
	enum exynos_bts_id id;
	void __iomem	*base;
	enum bts_priority def_priority;
};

struct exynos_bts_data {
	struct list_head node;
	struct device *dev;
	struct exynos_bts_local_data *bts_local_data;
	struct clk *clk;
	enum exynos_pd_block pd_block;
	u32 listnum;
};

struct exynos_fbm_data {
	struct exynos_fbm_resource fbm;
	struct list_head node;
};

static void bts_set_control(void __iomem *base, enum bts_priority prior)
{
	u32 val = BTS_ON_OFF;

	if (prior == BTS_BE)
		val |= BLOCKING_ON_OFF|FLEXIBLE_ON_OFF;

	writel(val, base + BTS_CONTROL);
}

static void bts_set_master_priority(void __iomem *base,
		enum bts_priority prior)
{
	u32 val = 0;
	int priority = BTS_PRIOR_BESTEFFORT;

	if (prior == BTS_BE) {
		val = MASTER_PRIOR_BE_NUMBER;
	} else if (prior == BTS_HARDTIME) {
		val = MASTER_PRIOR_HARDTIME_NUMBER;
		priority = BTS_PRIOR_HARDTIME;
	}

	val |= (priority<<8) | (priority<<4) | (priority);
	writel(val, base + BTS_MASTER_PRIORITY);
}

static void bts_set_besteffort_shaping(void __iomem *base,
		enum bts_priority prior)
{
	if (prior == BTS_BE) {
		writel(LOW_SHAPING_VAL0, base + BTS_SHAPING_ON_OFF_REG0);
		writel(LOW_SHAPING_VAL1, base + BTS_SHAPING_ON_OFF_REG1);
		writel(LOW_SHAPING_VAL2_BE, base + BTS_SHAPING_ON_OFF_REG2);
	} else if (prior == BTS_HARDTIME) {
		writel(LOW_SHAPING_VAL2_HARDTIME,
			base + BTS_SHAPING_ON_OFF_REG2);
	}
}

static void bts_set_deblocking(void __iomem *base,
		enum bts_fbm_group deblocking)
{
	u32 val = 0;

	if (deblocking & BTS_FBM_G0_L)
		val |= SEL_GRP0 | SEL_LEFT0;
	if (deblocking & BTS_FBM_G0_R)
		val |= SEL_GRP0 | SEL_RIGHT0;
	if (deblocking & BTS_FBM_G1_L)
		val |= SEL_GRP1 | SEL_LEFT1;
	if (deblocking & BTS_FBM_G1_R)
		val |= SEL_GRP1 | SEL_RIGHT1;
	if (deblocking & BTS_FBM_G2_L)
		val |= SEL_GRP2 | SEL_LEFT2;
	if (deblocking & BTS_FBM_G2_R)
		val |= SEL_GRP2 | SEL_RIGHT2;

	writel(val, base + BTS_DEBLOCKING_SOURCE_SELECTION);
}

static enum bts_fbm_group find_fbm_group(enum bts_priority prior)
{
	struct exynos_fbm_data *fbm_data;
	enum bts_fbm_group fbm_group = 0;

	list_for_each_entry(fbm_data, &fbm_list, node) {
		if (prior == BTS_BE) {
			if (fbm_data->fbm.priority == BTS_HARDTIME)
				fbm_group |= fbm_data->fbm.fbm_group;
		} else if (prior == BTS_HARDTIME) {
			if ((fbm_data->fbm.priority == BTS_BE) ||
				(fbm_data->fbm.priority == BTS_HARDTIME))
				fbm_group |= fbm_data->fbm.fbm_group;
		}
	}

	return fbm_group;
}

static void fbm_init_config(void __iomem *base, enum bts_priority prior)
{
	if (prior == BTS_BE) {
		writel(RD_COUNTER, base + FBM_MODESEL0);
		writel(FBM_THR_BE, base + FBM_THRESHOLDSEL0);
	} else if (prior == BTS_HARDTIME) {
		writel(RDWT_COUNTER, base + FBM_MODESEL0);
		writel(FBM_THR_HARDTIME, base + FBM_THRESHOLDSEL0);
	}
}

static void bts_init_config(void __iomem *base, enum bts_priority prior)
{
	if (prior == BTS_BE) {
		bts_set_besteffort_shaping(base, prior);
		bts_set_deblocking(base, find_fbm_group(prior));
		bts_set_master_priority(base, prior);
		bts_set_control(base, prior);
	} else if (prior == BTS_HARDTIME) {
		bts_set_besteffort_shaping(base, prior);
		bts_set_master_priority(base, prior);
		bts_set_control(base, prior);
	}
}

static void __exynos_bts_enable(struct exynos_bts_data *bts_data)
{
	struct exynos_bts_local_data *bts_local_data;
	int i;

	if (bts_data->clk)
		clk_enable(bts_data->clk);

	bts_local_data = bts_data->bts_local_data;
	for (i = 0; i < bts_data->listnum; i++) {
		bts_init_config(bts_local_data->base,
				bts_local_data->def_priority);
		bts_local_data++;
	}

	if (bts_data->clk)
		clk_disable(bts_data->clk);
}

void exynos_bts_set_priority(enum bts_priority prior)
{
	struct exynos_bts_data *bts_data;
	struct exynos_bts_local_data *bts_local_data;
	int i;

	list_for_each_entry(bts_data, &bts_list, node) {
		bts_local_data = bts_data->bts_local_data;
		for (i = 0; i < bts_data->listnum; i++) {
			bts_local_data = bts_data->bts_local_data;
			if ((bts_local_data->id == BTS_CPU) |
					(bts_local_data->id == BTS_G3D_ACP) |
					(bts_local_data->id == BTS_ROTATOR))
				bts_set_deblocking(bts_local_data->base,
						find_fbm_group(prior));
			bts_local_data++;
		}
	}
}

void exynos_bts_enable(enum exynos_pd_block pd_block)
{
	struct exynos_bts_data *bts_data;
	struct exynos_fbm_data *fbm_data;

	if (pd_block == PD_TOP) {
		list_for_each_entry(fbm_data, &fbm_list, node)
			fbm_init_config((void __iomem *)fbm_data->fbm.base,
					fbm_data->fbm.priority);
	}

	list_for_each_entry(bts_data, &bts_list, node) {
		if (bts_data->pd_block == pd_block)
			__exynos_bts_enable(bts_data);
	}
}

static int bts_probe(struct platform_device *pdev)
{
	struct exynos_bts_pdata *bts_pdata;
	struct exynos_fbm_resource *fbm_res;
	struct exynos_bts_data *bts_data;
	struct exynos_bts_local_data *bts_local_data, *bts_local_data_h;
	struct exynos_fbm_data *fbm_data;
	struct resource *res = NULL;
	void __iomem	*base;
	struct clk *clk = NULL;
	int i, ret = 0;

	bts_pdata = pdev->dev.platform_data;
	fbm_res = bts_pdata->fbm->res;

	if (!bts_pdata) {
		dev_err(&pdev->dev, "platform data is missed!\n");
		return -ENODEV;
	}

	if (list_empty(&fbm_list)) {
		for (i = 0; i < bts_pdata->fbm->res_num; i++) {
			base = ioremap(fbm_res->base, FBM_THRESHOLDSEL0);
			if (!base)
				return -ENODEV;
			fbm_init_config(base, fbm_res->priority);
			fbm_data = kzalloc(sizeof(struct exynos_fbm_data),
						GFP_KERNEL);
			fbm_data->fbm.base = (u32)base;
			fbm_data->fbm.fbm_group = fbm_res->fbm_group;
			fbm_data->fbm.priority = fbm_res->priority;
			list_add_tail(&fbm_data->node, &fbm_list);
			fbm_res++;
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't get resource!\n");
		return -ENODEV;
	}

	if (bts_pdata->clk_name) {
		clk = clk_get(pdev->dev.parent, bts_pdata->clk_name);
		if (IS_ERR(clk))
			return -EINVAL;
		clk_enable(clk);
	}

	bts_data = kzalloc(sizeof(struct exynos_bts_data), GFP_KERNEL);
	bts_data->listnum = bts_pdata->res_num;
	bts_local_data_h = bts_local_data =
		kzalloc(sizeof(struct exynos_bts_local_data)*bts_data->listnum,
				GFP_KERNEL);

	for (i = 0; i < bts_data->listnum; i++) {
		bts_local_data->id = bts_pdata->id;
		bts_local_data->base = ioremap(res->start, resource_size(res));
		bts_local_data->def_priority = bts_pdata->def_priority;
		if (!bts_local_data->base) {
			ret = -ENXIO;
			goto probe_err;
		}
		bts_init_config(bts_local_data->base,
				bts_local_data->def_priority);
		bts_local_data++;
		res++;
	}

	bts_data->bts_local_data = bts_local_data_h;
	bts_data->pd_block = bts_pdata->pd_block;
	bts_data->clk = clk;
	bts_data->dev = &pdev->dev;
	list_add_tail(&bts_data->node, &bts_list);
	pdev->dev.platform_data = bts_data;

probe_err:

	if (bts_pdata->clk_name)
		clk_disable(clk);

	return ret;
}

static int bts_remove(struct platform_device *pdev)
{
	struct exynos_fbm_data *fbm_data;
	struct exynos_bts_data *bts_data = pdev->dev.platform_data;
	struct exynos_bts_local_data *bts_local_data;
	int i;

	bts_local_data = bts_data->bts_local_data;
	for (i = 0; i < bts_data->listnum; i++) {
		bts_local_data++;
		iounmap(bts_local_data->base);
	}
	kfree(bts_data->bts_local_data);
	list_del(&bts_data->node);
	kfree(bts_data);

	if (list_empty(&bts_list))
		list_for_each_entry(fbm_data, &fbm_list, node) {
			iounmap((void __iomem *)fbm_data->fbm.base);
			kfree(fbm_data);
			list_del(&fbm_data->node);
		}

	if (bts_data->clk)
		clk_put(bts_data->clk);

	return 0;
}

static struct platform_driver bts_driver = {
	.driver	= {
		.owner	= THIS_MODULE,
		.name = "exynos-bts"
	},
	.probe	= bts_probe,
	.remove	= bts_remove,
};

static int __init bts_init(void)
{
	return platform_driver_register(&bts_driver);
}
arch_initcall(bts_init);
