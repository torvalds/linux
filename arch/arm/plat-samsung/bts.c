/* linux/arch/arm/plat-samsung/bts.c
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
#include <linux/list.h>
#if defined(CONFIG_PM_RUNTIME)
#include <linux/pm_runtime.h>
#endif
#include <linux/debugfs.h>

#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/bts.h>
#include <mach/map.h>

/* BTS (Bus traffic shaper) registers */
#define BTS_CONTROL 0x0
#define BTS_SHAPING_ON_OFF_REG0 0x4
#define BTS_MASTER_PRIORITY 0x8
#define BTS_SHAPING_ON_OFF_REG1 0x44
#define BTS_DEBLOCKING_SOURCE_SELECTION 0x50

/* FBM (Flexible Blocking Monitor) registers */
#define BTS_THRESHOLD_SEL0 0x40

/*
 * Fields of BTS_CONTROL register
 * BTS_CTL_ENABLE indicates BTS_ON_OFF field
 * BTS_CTL_BLOCKING indicates BLOCKING_ON_OFF field
 * BTS_CTL_DEBLOCKING indicates DEBLOCKING_ON_OFF field
 */
#define BTS_CTL_ENABLE (1<<0)
#define BTS_CTL_BLOCKING (1<<2)
#define BTS_CTL_DEBLOCKING (1<<7)

/* Fields of DEBLOCKING_SOURCE_SELECTION register */
#define SEL_GRP0 (1<<0)
#define SEL_LEFT0 (1<<4)
#define SEL_RIGHT0 (1<<5)
#define SEL_GRP1 (1<<8)
#define SEL_LEFT1 (1<<12)
#define SEL_RIGHT1 (1<<13)
#define SEL_GRP2 (1<<16)
#define SEL_LEFT2 (1<<20)
#define SEL_RIGHT2 (1<<21)

/* Shaping Value for Low priority */
#define LOW_SHAPING_VAL0 0x0
#define LOW_SHAPING_VAL1 0x3ff
#define MASTER_PRIOR_NUMBER (1<<16)

#define BTS_DISABLE 0
#define BTS_ENABLE  1

#define BTS_UP_THRESHOLD 0x4
#define BTS_MIXER_THRESHOLD 0x3

static LIST_HEAD(fbm_list);
static LIST_HEAD(bts_list);

/* Structure for status of a physical BTS device */
struct exynos_bts_status {
	enum bts_priority 	cur_priority;
	enum bts_fbm_input_port fbm_src;
	bool is_enabled;
	bool is_blocked;
	bool is_deblocked;
};

/* Structure for a physical BTS device */
struct exynos_bts_local_data {
	struct exynos_bts_status cur_status;
	void __iomem		*base;
	enum bts_priority	 def_priority;
	/*
	 * Indicate that it could be changable for selecting deblock sources
	 * in case of controlling bus traffic
	 */
	bool deblock_changable;
	bool threshold_changable;
};

/* Structure for a BTS driver.
 * A BTS driver handles several physical BTS devices
 * that are grouped together by clock and power control.
 * bts_local_data member contains the physical device list.
 */
struct exynos_bts_data {
	struct list_head node;
	struct device *dev;
	struct clk *clk;
	struct exynos_bts_local_data *bts_local_data;
	char *pd_name;
	enum bts_traffic_control traffic_control_act;
	u32 listnum;
};

/* Structure for FBM devices */
struct exynos_fbm_data {
	struct exynos_fbm_resource fbm;
	struct list_head node;
};

/* Find FBM input port name with FBM source order */
static enum bts_fbm_input_port find_fbm_port(enum bts_deblock_src_order fbm)
{
	struct exynos_fbm_data *fbm_data;
	enum bts_fbm_input_port fbm_input_port = 0;

	list_for_each_entry(fbm_data, &fbm_list, node) {
		if (fbm & fbm_data->fbm.deblock_src_order)
			fbm_input_port |= fbm_data->fbm.port_name;
	}

	return fbm_input_port;
}

/* Basic control of a BTS device */
static void bts_set_control(struct exynos_bts_local_data *data,
			    enum bts_priority prior)
{
	u32 val = BTS_CTL_ENABLE;

	data->cur_status.is_enabled = true;
	if (prior == BTS_PRIOR_BE) {
		val |= BTS_CTL_BLOCKING | BTS_CTL_DEBLOCKING;
		data->cur_status.is_blocked = true;
		data->cur_status.is_deblocked = true;
	}
	writel(val, data->base + BTS_CONTROL);
}

/* on/off a BTS device */
static void bts_onoff(struct exynos_bts_local_data *data, bool on)
{
	u32 val = readl(data->base + BTS_CONTROL);
	if (on) {
		val |= BTS_CTL_ENABLE;
		data->cur_status.is_enabled = true;
	}
	else {
		val &= ~BTS_CTL_ENABLE;
		data->cur_status.is_enabled = false;
	}

	writel(val, data->base + BTS_CONTROL);
}

/* set priority of BTS device */
static void bts_set_priority(struct exynos_bts_local_data *data,
			     enum bts_priority prior)
{
	u32 val;

	val = MASTER_PRIOR_NUMBER | (prior<<8) | (prior<<4) | (prior);
	writel(val, data->base + BTS_MASTER_PRIORITY);
	data->cur_status.cur_priority = prior;
}

/* set the blocking value for best effort IPs to BTS device */
static void bts_set_blocking(struct exynos_bts_local_data *data)
{
	writel(LOW_SHAPING_VAL0, data->base + BTS_SHAPING_ON_OFF_REG0);
	writel(LOW_SHAPING_VAL1, data->base + BTS_SHAPING_ON_OFF_REG1);
}

/* set the deblocking source according to FBM input port name */
static void bts_set_deblocking(struct exynos_bts_local_data *data,
			       enum bts_fbm_input_port port_name)
{
	u32 val = 0;

	if (port_name & BTS_FBM_G0_L)
		val |= SEL_GRP0 | SEL_LEFT0;
	if (port_name & BTS_FBM_G0_R)
		val |= SEL_GRP0 | SEL_RIGHT0;
	if (port_name & BTS_FBM_G1_L)
		val |= SEL_GRP1 | SEL_LEFT1;
	if (port_name & BTS_FBM_G1_R)
		val |= SEL_GRP1 | SEL_RIGHT1;
	if (port_name & BTS_FBM_G2_L)
		val |= SEL_GRP2 | SEL_LEFT2;
	if (port_name & BTS_FBM_G2_R)
		val |= SEL_GRP2 | SEL_RIGHT2;
	writel(val, data->base + BTS_DEBLOCKING_SOURCE_SELECTION);
	data->cur_status.fbm_src = port_name;
}

/* initialize a BTS device with default bus traffic values */
static void bts_set_default_bus_traffic(struct exynos_bts_local_data *data,
					enum bts_priority prior)
{
	switch (prior) {
	case BTS_FBM_DDR_R1:
		data->cur_status.is_enabled = true;
		data->cur_status.cur_priority = prior;
		break;
	case BTS_PRIOR_BE:
		bts_set_blocking(data);
		bts_set_deblocking(data, find_fbm_port(BTS_1ST_FBM_SRC));
	case BTS_PRIOR_HARDTIME:
		bts_set_priority(data, prior);
		bts_set_control(data, prior);
		break;
	default:
		break;
	}
}

/* restore a BTS device with previous value */
static void bts_restore_bus_traffic(struct exynos_bts_local_data *data)
{
	enum bts_priority prior = data->cur_status.cur_priority;

	switch (prior) {
	case BTS_FBM_DDR_R1:
		data->cur_status.is_enabled = true;
		data->cur_status.cur_priority = prior;
		break;
	case BTS_PRIOR_BE:
		bts_set_blocking(data);
		bts_set_deblocking(data, data->cur_status.fbm_src);
	case BTS_PRIOR_HARDTIME:
		bts_set_priority(data, prior);
		bts_set_control(data, prior);
		break;
	default:
		break;
	}
}

/* change threshold FBM */
void exynos_bts_change_threshold(enum bts_bw_change bw_change)
{
	struct exynos_bts_data *bts_data;
	struct exynos_bts_local_data *bts_local_data;
	char *reason;
	int threshold;
	int i;

	list_for_each_entry(bts_data, &bts_list, node) {
		bts_local_data = bts_data->bts_local_data;
		for (i = 0; i < bts_data->listnum; i++) {
			if (bts_local_data->threshold_changable) {
				if (bw_change == BTS_INCREASE_BW) {
					threshold = BTS_UP_THRESHOLD;
					reason = "increasing";
				} else if (bw_change == BTS_MIXER_BW) {
					threshold = BTS_MIXER_THRESHOLD;
					reason = "mixer";
				}

				writel(threshold, bts_local_data->base + BTS_THRESHOLD_SEL0);
				pr_debug("%s: Change BTS threshold for %s BW\n",
					dev_name(bts_data->dev), reason);
			}
			bts_local_data++;
		}
	}
}

/* change deblocking FBM source */
static void bts_change_deblock_src(enum bts_bw_change bw_change)
{
	struct exynos_bts_data *bts_data;
	struct exynos_bts_local_data *bts_local_data;
	int i;

	list_for_each_entry(bts_data, &bts_list, node) {
		bts_local_data = bts_data->bts_local_data;
		for (i = 0; i < bts_data->listnum; i++) {
			if (bts_local_data->deblock_changable) {
#if defined(CONFIG_PM_RUNTIME)
				pm_runtime_get_sync(bts_data->dev->parent);
#endif
				if (bts_data->clk)
					clk_enable(bts_data->clk);
				bts_onoff(bts_local_data, BTS_DISABLE);
				pr_debug("%s: Change BTS deblocking source for %s BW\n",
					dev_name(bts_data->dev),
					(bw_change == BTS_INCREASE_BW) ?
					"increasing" : "decreasing");
				bts_set_deblocking(bts_local_data,
					find_fbm_port(
						(bw_change == BTS_INCREASE_BW)
						? BTS_1ST_FBM_SRC
						: (BTS_1ST_FBM_SRC |
						BTS_2ND_FBM_SRC)));
				bts_onoff(bts_local_data, BTS_ENABLE);
				if (bts_data->clk)
					clk_disable(bts_data->clk);
#if defined(CONFIG_PM_RUNTIME)
				pm_runtime_put_sync(bts_data->dev->parent);
#endif
			}
			bts_local_data++;
		}
	}
}

/* turn physical BTS devices on/off */
static void bts_devs_onoff(struct exynos_bts_data *bts_data, bool on)
{
	struct exynos_bts_local_data *bts_local_data;
	int i;

#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_get_sync(bts_data->dev->parent);
#endif
	if (bts_data->clk)
		clk_enable(bts_data->clk);

	bts_local_data = bts_data->bts_local_data;
	for (i = 0; i < bts_data->listnum; i++) {
		pr_debug("%s: BTS %sable for %s requests to memory\n",
				dev_name(bts_data->dev), on ? "En" : "Dis", on ?
				"blocking" : "de-blocking");
		bts_onoff(bts_local_data, on);
		bts_local_data++;
	}

	if (bts_data->clk)
		clk_disable(bts_data->clk);
#if defined(CONFIG_PM_RUNTIME)
	pm_runtime_put_sync(bts_data->dev->parent);
#endif
}

/* init physical bts devices */
static void bts_devs_init(struct exynos_bts_data *bts_data)
{
	struct exynos_bts_local_data *bts_local_data;
	int i;

	pr_debug("%s requested %s from [%s] power domain\n",
		dev_name(bts_data->dev), __func__, (bts_data->pd_name == NULL) ?
						"NULL" : bts_data->pd_name);

	if (bts_data->clk)
		clk_enable(bts_data->clk);

	bts_local_data = bts_data->bts_local_data;
	for (i = 0; i < bts_data->listnum; i++) {
		pr_debug("%s: Restore bus BW for res[%d]\n",
						dev_name(bts_data->dev), i);
		bts_restore_bus_traffic(bts_local_data);
		bts_local_data++;
	}

	if (bts_data->clk)
		clk_disable(bts_data->clk);
}

void exynos_bts_change_bus_traffic(struct device *dev,
				   enum bts_bw_change bw_change)
{
	struct exynos_bts_data *bts_data;

	pr_debug("%s: change bus traffic to %s BW\n", dev_name(dev),
		(bw_change == BTS_INCREASE_BW) ? "increase" : "decrease");
	list_for_each_entry(bts_data, &bts_list, node) {
		if (bts_data->dev->parent == dev) {
			switch (bts_data->traffic_control_act) {
			/*
			 * BTS_DISABLE means turning off blocking feature
			 * to increase bus bandwidth
			 */
			case BTS_ON_OFF:
				bts_devs_onoff(bts_data,
					(bw_change == BTS_INCREASE_BW)
					?  BTS_DISABLE : BTS_ENABLE);
				break;
			/*
			 * Increasing the caller device's bus bandwidth
			 * by decreasing others' bus bandwidth
			 */
			case BTS_CHANGE_OTHER_DEBLOCK:
				bts_change_deblock_src(
					(bw_change == BTS_INCREASE_BW)
					? BTS_DECREASE_BW : BTS_INCREASE_BW);
				break;
			default:
				dev_err(bts_data->dev,
					"[%d] Unknown bus traffic control action\n",
					bts_data->traffic_control_act);
				break;
			}
		}
	}
}

void exynos_bts_initialize(char *pd_name, bool power_on)
{
	struct exynos_bts_data *bts_data;
	struct exynos_bts_status *cur_status;

	list_for_each_entry(bts_data, &bts_list, node) {
		if ((!pd_name && !bts_data->pd_name) ||
			(pd_name && bts_data->pd_name &&
			 !strcmp(bts_data->pd_name, pd_name))) {
			if (power_on) {
				bts_devs_init(bts_data);
			} else {
				/*
				 * Do NOT touch HW register because BTS's master
				 * IP is already turned power off
				 */
				cur_status = &bts_data->bts_local_data->cur_status;
				cur_status->is_enabled = false;
			}
		}
	}
}

static struct dentry *bts_debugfs;

static int bts_debug_show(struct seq_file *s, void *unused)
{
	struct exynos_bts_data *bts_data;
	struct exynos_bts_status *cur_status;

	list_for_each_entry(bts_data, &bts_list, node) {
		cur_status = &bts_data->bts_local_data->cur_status;
		seq_printf(s, "%12s: BTS %3s", dev_name(bts_data->dev),
			cur_status->is_enabled ? "ON" : "OFF\n");
		if (!cur_status->is_enabled)
			continue;
		seq_printf(s, ", priority %2d, ", cur_status->cur_priority);
		seq_printf(s, "Block %3s, Deblock %3s, Deblock src : ",
			cur_status->is_blocked ? "ON" : "OFF",
			cur_status->is_deblocked ? "ON" : "OFF");
		if (cur_status->fbm_src & BTS_FBM_G0_L)
			seq_printf(s, "%s ", "BTS_FBM_G0_L");
		if (cur_status->fbm_src & BTS_FBM_G0_R)
			seq_printf(s, "%s ", "BTS_FBM_G0_R");
		if (cur_status->fbm_src & BTS_FBM_G1_L)
			seq_printf(s, "%s ", "BTS_FBM_G1_L");
		if (cur_status->fbm_src & BTS_FBM_G1_R)
			seq_printf(s, "%s ", "BTS_FBM_G1_R");
		if (cur_status->fbm_src & BTS_FBM_G2_L)
			seq_printf(s, "%s ", "BTS_FBM_G2_L");
		if (cur_status->fbm_src & BTS_FBM_G2_R)
			seq_printf(s, "%s ", "BTS_FBM_G2_R");
		seq_printf(s, " \n");
	}

	return 0;
}

static int bts_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, bts_debug_show, inode->i_private);
}

const static struct file_operations bts_dev_status_fops = {
	.open		= bts_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int bts_probe(struct platform_device *pdev)
{
	struct exynos_bts_pdata *bts_pdata;
	struct exynos_fbm_resource *fbm_res;
	struct exynos_bts_data *bts_data;
	struct exynos_bts_local_data *bts_local_data, *bts_local_data_h;
	struct exynos_fbm_data *fbm_data = NULL, *next;
	struct resource *res = NULL;
	struct clk *clk = NULL;
	int i, ret = 0;

	bts_pdata = pdev->dev.platform_data;
	if (!bts_pdata) {
		dev_err(&pdev->dev, "platform data is missed!\n");
		return -ENODEV;
	}

	fbm_res = bts_pdata->fbm->res;

	if (list_empty(&fbm_list)) {
		for (i = 0; i < bts_pdata->fbm->res_num; i++) {
			fbm_data = kzalloc(sizeof(struct exynos_fbm_data),
						GFP_KERNEL);
			if (!fbm_data) {
				ret = -ENOMEM;
				dev_err(&pdev->dev,
					"failed to kzalloc() for fbm\n");
				goto probe_err1;
			}
			list_add_tail(&fbm_data->node, &fbm_list);
			fbm_data->fbm.port_name = fbm_res->port_name;
			fbm_data->fbm.deblock_src_order =
						fbm_res->deblock_src_order;
			fbm_res++;
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't get resource!\n");
		ret = -ENODEV;
		goto probe_err1;
	}

	if (bts_pdata->clk_name) {
		clk = clk_get(pdev->dev.parent, bts_pdata->clk_name);
		if (IS_ERR(clk)) {
			ret = -EINVAL;
			dev_err(&pdev->dev, "failed to clk_get()\n");
			goto probe_err1;
		}
		clk_enable(clk);
	}

	bts_data = kzalloc(sizeof(struct exynos_bts_data), GFP_KERNEL);
	if (!bts_data) {
		ret = -ENOMEM;
		dev_err(&pdev->dev,
				"failed to kzalloc() for exynos_bts_data\n");
		goto probe_err2;
	}
	bts_data->listnum = bts_pdata->res_num;
	bts_data->traffic_control_act = bts_pdata->traffic_control_act;
	bts_local_data_h = bts_local_data =
		kzalloc(sizeof(struct exynos_bts_local_data)*bts_data->listnum,
								GFP_KERNEL);
	if (!bts_local_data_h) {
		ret = -ENOMEM;
		dev_err(&pdev->dev,
			"failed to kzalloc() for exynos_bts_local_data\n");
		goto probe_err3;
	}

	for (i = 0; i < bts_data->listnum; i++) {
		bts_local_data->base = ioremap(res->start, resource_size(res));
		if (!bts_local_data->base) {
			ret = -ENXIO;
			dev_err(&pdev->dev, "failed to ioremap()\n");
			goto probe_err4;
		}
		bts_local_data->def_priority = bts_pdata->def_priority;
		bts_local_data->deblock_changable =
						bts_pdata->deblock_changable;
		bts_local_data->threshold_changable =
						bts_pdata->threshold_changable;
		pr_debug("%s: Set default bus BW with priority %d for res[%d]\n",
					dev_name(&pdev->dev),
					bts_local_data->def_priority, i);
		bts_set_default_bus_traffic(bts_local_data,
						bts_local_data->def_priority);
		bts_local_data++;
		res++;
	}

	bts_data->bts_local_data = bts_local_data_h;
	bts_data->pd_name = bts_pdata->pd_name;
	bts_data->clk = clk;
	bts_data->dev = &pdev->dev;

	list_add_tail(&bts_data->node, &bts_list);
	pdev->dev.platform_data = bts_data;

	if (bts_pdata->clk_name)
		clk_disable(clk);
	return 0;

probe_err4:
	bts_local_data = bts_local_data_h;
	for (i = 0; i < bts_data->listnum; i++) {
		if (bts_local_data->base)
			iounmap(bts_local_data->base);
		bts_local_data++;
	}
	kfree(bts_local_data_h);

probe_err3:
	kfree(bts_data);

probe_err2:
	if (bts_pdata->clk_name) {
		clk_disable(clk);
		clk_put(bts_data->clk);
	}

probe_err1:
	list_for_each_entry_safe(fbm_data, next, &fbm_list, node) {
		if (fbm_data) {
			list_del(&fbm_data->node);
			kfree(fbm_data);
		}
	}

	return ret;
}

static int bts_remove(struct platform_device *pdev)
{
	struct exynos_fbm_data *fbm_data, *next;
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

	if (bts_data->clk)
		clk_put(bts_data->clk);
	kfree(bts_data);

	if (!list_empty(&bts_list))
		list_for_each_entry_safe(fbm_data, next, &fbm_list, node) {
			list_del(&fbm_data->node);
			kfree(fbm_data);
		}

	return 0;
}

static struct platform_driver bts_driver = {
	.driver	= {
		.name = "exynos-bts"
	},
	.probe	= bts_probe,
	.remove	= bts_remove,
};

static int __init bts_init(void)
{
	bts_debugfs =
		debugfs_create_file("bts_dev_status",
				    S_IRUGO, NULL, NULL, &bts_dev_status_fops);
	if (IS_ERR_OR_NULL(bts_debugfs)) {
		bts_debugfs = NULL;
		pr_err("%s: debugfs_create_file() failed\n", __func__);
	}

	return platform_driver_register(&bts_driver);
}
arch_initcall(bts_init);
