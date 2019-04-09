/*
 * Rockchip PVTM support.
 *
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/soc/rockchip/pvtm.h>
#include <linux/thermal.h>

#define PX30_PVTM_CORE		0
#define PX30_PVTM_PMU		1

#define RK1808_PVTM_CORE	0
#define RK1808_PVTM_PMU		1
#define RK1808_PVTM_NPU		2

#define RK3288_PVTM_CORE	0
#define RK3288_PVTM_GPU		1

#define RK3366_PVTM_CORE	0
#define RK3366_PVTM_GPU		1
#define RK3366_PVTM_PMU		2

#define RK3399_PVTM_CORE_L	0
#define RK3399_PVTM_CORE_B	1
#define RK3399_PVTM_DDR		2
#define RK3399_PVTM_GPU		3
#define RK3399_PVTM_PMU		4

#define PVTM_START_EN		0x1

#define wr_mask_bit(v, off, mask)	((v) << (off) | (mask) << (16 + off))

#define PVTM(_ch, _name, _num_sub, _start, _en, _cal, _done, _freq)	\
{					\
	.ch = _ch,			\
	.clk_name = _name,		\
	.num_sub = _num_sub,		\
	.bit_start = _start,		\
	.bit_en = _en,			\
	.reg_cal = _cal,		\
	.bit_freq_done = _done,		\
	.reg_freq = _freq,		\
}

struct rockchip_pvtm;

struct rockchip_pvtm_channel {
	u32 reg_cal;
	u32 reg_freq;
	unsigned char ch;
	unsigned char *clk_name;
	unsigned int num_sub;
	unsigned int bit_start;
	unsigned int bit_en;
	unsigned int bit_freq_done;
};

struct rockchip_pvtm_info {
	u32 con;
	u32 sta;
	unsigned int num_channels;
	const struct rockchip_pvtm_channel *channels;
	u32 (*get_value)(struct rockchip_pvtm *pvtm, unsigned int sub_ch,
			 unsigned int time_us);
	void (*set_ring_sel)(struct rockchip_pvtm *pvtm, unsigned int sub_ch);
};

struct rockchip_pvtm {
	u32 con;
	u32 sta;
	struct list_head node;
	struct device *dev;
	struct regmap *grf;
	struct clk *clk;
	struct reset_control *rst;
	const struct rockchip_pvtm_channel *channel;
	struct thermal_zone_device *tz;
	u32 (*get_value)(struct rockchip_pvtm *pvtm, unsigned int sub_ch,
			 unsigned int time_us);
	void (*set_ring_sel)(struct rockchip_pvtm *pvtm, unsigned int sub_ch);
};

static LIST_HEAD(pvtm_list);

#ifdef CONFIG_DEBUG_FS

static struct dentry *rootdir;

static int pvtm_value_show(struct seq_file *s, void *data)
{
	struct rockchip_pvtm *pvtm = (struct rockchip_pvtm *)s->private;
	u32 value;
	int i, ret, cur_temp;

	if (!pvtm) {
		pr_err("pvtm struct NULL\n");
		return -EINVAL;
	}

	if (pvtm->tz && pvtm->tz->ops && pvtm->tz->ops->get_temp) {
		ret = pvtm->tz->ops->get_temp(pvtm->tz, &cur_temp);
		if (ret)
			dev_err(pvtm->dev, "debug failed to get temp\n");
		else
			seq_printf(s, "temp: %d ", cur_temp);
	}
	seq_puts(s, "pvtm: ");
	for (i = 0; i < pvtm->channel->num_sub; i++) {
		value = pvtm->get_value(pvtm, i, 1000);
		seq_printf(s, "%d ", value);
	}
	seq_puts(s, "\n");

	return 0;
}

static int pvtm_value_open(struct inode *inode, struct file *file)
{
	return single_open(file, pvtm_value_show, inode->i_private);
}

static const struct file_operations pvtm_value_fops = {
	.open		= pvtm_value_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init pvtm_debug_init(void)
{
	struct dentry *dentry, *d;
	struct rockchip_pvtm *pvtm;

	rootdir = debugfs_create_dir("pvtm", NULL);
	if (!rootdir) {
		pr_err("Failed to create pvtm debug directory\n");
		return -ENOMEM;
	}

	if (list_empty(&pvtm_list)) {
		pr_info("pvtm list NULL\n");
		return 0;
	}

	list_for_each_entry(pvtm, &pvtm_list, node) {
		dentry = debugfs_create_dir(pvtm->channel->clk_name, rootdir);
		if (!dentry) {
			dev_err(pvtm->dev, "failed to creat pvtm %s debug dir\n",
				pvtm->channel->clk_name);
			return -ENOMEM;
		}

		d = debugfs_create_file("value", 0444, dentry,
					(void *)pvtm, &pvtm_value_fops);
		if (!d) {
			dev_err(pvtm->dev, "failed to pvtm %s value node\n",
				pvtm->channel->clk_name);
			return -ENOMEM;
		}
	}

	return 0;
}

late_initcall(pvtm_debug_init);

#endif

static int rockchip_pvtm_reset(struct rockchip_pvtm *pvtm)
{
	int ret;

	ret = reset_control_assert(pvtm->rst);
	if (ret) {
		dev_err(pvtm->dev, "failed to assert pvtm %d\n", ret);
		return ret;
	}

	udelay(2);

	ret = reset_control_deassert(pvtm->rst);
	if (ret) {
		dev_err(pvtm->dev, "failed to deassert pvtm %d\n", ret);
		return ret;
	}

	return 0;
}

u32 rockchip_get_pvtm_value(unsigned int ch, unsigned int sub_ch,
			    unsigned int time_us)
{
	struct rockchip_pvtm *pvtm;

	if (list_empty(&pvtm_list)) {
		pr_err("pvtm list NULL\n");
		return -EINVAL;
	}

	list_for_each_entry(pvtm, &pvtm_list, node) {
		if (pvtm->channel->ch == ch)
			break;
	}

	if (sub_ch >= pvtm->channel->num_sub) {
		pr_err("invalid pvtm sub_ch %d\n", sub_ch);
		return -EINVAL;
	}

	return pvtm->get_value(pvtm, sub_ch, time_us);
}
EXPORT_SYMBOL(rockchip_get_pvtm_value);

static void rockchip_pvtm_delay(unsigned int delay)
{
	unsigned int ms = delay / 1000;
	unsigned int us = delay % 1000;

	if (ms > 0) {
		if (ms < 20)
			us += ms * 1000;
		else
			msleep(ms);
	}

	if (us >= 10)
		usleep_range(us, us + 100);
	else
		udelay(us);
}

static void px30_pvtm_set_ring_sel(struct rockchip_pvtm *pvtm,
				   unsigned int sub_ch)
{
	unsigned int ch = pvtm->channel->ch;

	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(sub_ch, (ch * 0x4 + 0x2), 0x3));
}

static void rk1808_pvtm_set_ring_sel(struct rockchip_pvtm *pvtm,
				     unsigned int sub_ch)
{
	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(sub_ch, 0x2, 0x7));
}

static void rk3399_pvtm_set_ring_sel(struct rockchip_pvtm *pvtm,
				     unsigned int sub_ch)
{
	unsigned int ch = pvtm->channel->ch;

	if (ch == RK3399_PVTM_CORE_B) {
		regmap_write(pvtm->grf, pvtm->con + 0x14,
			     wr_mask_bit(sub_ch >> 0x3, 0, 0x1));
		sub_ch &= 0x3;
	}
	if (ch != RK3399_PVTM_PMU)
		regmap_write(pvtm->grf, pvtm->con,
			     wr_mask_bit(sub_ch, (ch * 0x4 + 0x2), 0x3));
}

static u32 rockchip_pvtm_get_value(struct rockchip_pvtm *pvtm,
				   unsigned int sub_ch,
				   unsigned int time_us)
{
	const struct rockchip_pvtm_channel *channel = pvtm->channel;
	unsigned int ch = pvtm->channel->ch;
	unsigned int clk_cnt, check_cnt = 100;
	u32 sta, val = 0;
	int ret;

	ret = clk_prepare_enable(pvtm->clk);
	if (ret < 0) {
		dev_err(pvtm->dev, "failed to enable %d\n", ch);
		return 0;
	}

	if (pvtm->rst) {
		ret = rockchip_pvtm_reset(pvtm);
		if (ret) {
			clk_disable_unprepare(pvtm->clk);
			return 0;
		}
	}

	/* if last status is enabled, stop calculating cycles first*/
	regmap_read(pvtm->grf, pvtm->con, &sta);
	if (sta & PVTM_START_EN)
		regmap_write(pvtm->grf, pvtm->con,
			     wr_mask_bit(0, channel->bit_start, 0x1));

	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(0x1, channel->bit_en, 0x1));

	if (pvtm->set_ring_sel)
		pvtm->set_ring_sel(pvtm, sub_ch);

	/* clk = 24 Mhz, T = 1 / 24 us */
	clk_cnt = time_us * 24;
	regmap_write(pvtm->grf, pvtm->con + channel->reg_cal, clk_cnt);

	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(0x1, channel->bit_start, 0x1));

	rockchip_pvtm_delay(time_us);

	while (check_cnt) {
		regmap_read(pvtm->grf, pvtm->sta, &sta);
		if (sta & BIT(channel->bit_freq_done))
			break;
		udelay(4);
		check_cnt--;
	}

	if (check_cnt) {
		regmap_read(pvtm->grf, pvtm->sta + channel->reg_freq, &val);
	} else {
		dev_err(pvtm->dev, "wait pvtm_done timeout!\n");
		val = 0;
	}

	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(0, channel->bit_start, 0x1));

	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(0, channel->bit_en, 0x1));

	clk_disable_unprepare(pvtm->clk);

	return val;
}

static const struct rockchip_pvtm_channel px30_pvtm_channels[] = {
	PVTM(PX30_PVTM_CORE, "core", 3, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_info px30_pvtm = {
	.con = 0x80,
	.sta = 0x88,
	.num_channels = ARRAY_SIZE(px30_pvtm_channels),
	.channels = px30_pvtm_channels,
	.get_value = rockchip_pvtm_get_value,
	.set_ring_sel = px30_pvtm_set_ring_sel,
};

static const struct rockchip_pvtm_channel px30_pmupvtm_channels[] = {
	PVTM(PX30_PVTM_PMU, "pmu", 1, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_info px30_pmupvtm = {
	.con = 0x180,
	.sta = 0x190,
	.num_channels = ARRAY_SIZE(px30_pmupvtm_channels),
	.channels = px30_pmupvtm_channels,
	.get_value = rockchip_pvtm_get_value,
};

static const struct rockchip_pvtm_channel rk1808_pvtm_channels[] = {
	PVTM(RK1808_PVTM_CORE, "core", 5, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_info rk1808_pvtm = {
	.con = 0x80,
	.sta = 0x88,
	.num_channels = ARRAY_SIZE(rk1808_pvtm_channels),
	.channels = rk1808_pvtm_channels,
	.get_value = rockchip_pvtm_get_value,
	.set_ring_sel = rk1808_pvtm_set_ring_sel,
};

static const struct rockchip_pvtm_channel rk1808_pmupvtm_channels[] = {
	PVTM(RK1808_PVTM_PMU, "pmu", 1, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_info rk1808_pmupvtm = {
	.con = 0x180,
	.sta = 0x190,
	.num_channels = ARRAY_SIZE(rk1808_pmupvtm_channels),
	.channels = rk1808_pmupvtm_channels,
	.get_value = rockchip_pvtm_get_value,
};

static const struct rockchip_pvtm_channel rk1808_npupvtm_channels[] = {
	PVTM(RK1808_PVTM_NPU, "npu", 5, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_info rk1808_npupvtm = {
	.con = 0x780,
	.sta = 0x788,
	.num_channels = ARRAY_SIZE(rk1808_npupvtm_channels),
	.channels = rk1808_npupvtm_channels,
	.get_value = rockchip_pvtm_get_value,
	.set_ring_sel = rk1808_pvtm_set_ring_sel,
};

static const struct rockchip_pvtm_channel rk3288_pvtm_channels[] = {
	PVTM(RK3288_PVTM_CORE, "core", 1, 0, 1, 0x4, 1, 0x4),
	PVTM(RK3288_PVTM_GPU, "gpu", 1, 8, 9, 0x8, 0, 0x8),
};

static const struct rockchip_pvtm_info rk3288_pvtm = {
	.con = 0x368,
	.sta = 0x374,
	.num_channels = ARRAY_SIZE(rk3288_pvtm_channels),
	.channels = rk3288_pvtm_channels,
	.get_value = rockchip_pvtm_get_value,
};

static const struct rockchip_pvtm_info rk3308_pmupvtm = {
	.con = 0x440,
	.sta = 0x448,
	.num_channels = ARRAY_SIZE(px30_pmupvtm_channels),
	.channels = px30_pmupvtm_channels,
	.get_value = rockchip_pvtm_get_value,
};

static const struct rockchip_pvtm_channel rk3366_pvtm_channels[] = {
	PVTM(RK3366_PVTM_CORE, "core", 1, 0, 1, 0x4, 0, 0x4),
	PVTM(RK3366_PVTM_GPU, "gpu", 1, 8, 9, 0x8, 1, 0x8),
};

static const struct rockchip_pvtm_info rk3366_pvtm = {
	.con = 0x800,
	.sta = 0x80c,
	.num_channels = ARRAY_SIZE(rk3366_pvtm_channels),
	.channels = rk3366_pvtm_channels,
	.get_value = rockchip_pvtm_get_value,
};

static const struct rockchip_pvtm_channel rk3366_pmupvtm_channels[] = {
	PVTM(RK3366_PVTM_PMU, "pmu", 1, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_info rk3366_pmupvtm = {
	.con = 0x180,
	.sta = 0x190,
	.num_channels = ARRAY_SIZE(rk3366_pmupvtm_channels),
	.channels = rk3366_pmupvtm_channels,
	.get_value = rockchip_pvtm_get_value,
};

static const struct rockchip_pvtm_channel rk3399_pvtm_channels[] = {
	PVTM(RK3399_PVTM_CORE_L, "core_l", 4, 0, 1, 0x4, 0, 0x4),
	PVTM(RK3399_PVTM_CORE_B, "core_b", 6, 4, 5, 0x8, 1, 0x8),
	PVTM(RK3399_PVTM_DDR, "ddr", 4, 8, 9, 0xc, 3, 0x10),
	PVTM(RK3399_PVTM_GPU, "gpu", 4, 12, 13, 0x10, 2, 0xc),
};

static const struct rockchip_pvtm_info rk3399_pvtm = {
	.con = 0xe600,
	.sta = 0xe620,
	.num_channels = ARRAY_SIZE(rk3399_pvtm_channels),
	.channels = rk3399_pvtm_channels,
	.get_value = rockchip_pvtm_get_value,
	.set_ring_sel = rk3399_pvtm_set_ring_sel,
};

static const struct rockchip_pvtm_channel rk3399_pmupvtm_channels[] = {
	PVTM(RK3399_PVTM_PMU, "pmu", 1, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_info rk3399_pmupvtm = {
	.con = 0x240,
	.sta = 0x248,
	.num_channels = ARRAY_SIZE(rk3399_pmupvtm_channels),
	.channels = rk3399_pmupvtm_channels,
	.get_value = rockchip_pvtm_get_value,
};

static const struct of_device_id rockchip_pvtm_match[] = {
	{
		.compatible = "rockchip,px30-pvtm",
		.data = (void *)&px30_pvtm,
	},
	{
		.compatible = "rockchip,px30-pmu-pvtm",
		.data = (void *)&px30_pmupvtm,
	},
	{
		.compatible = "rockchip,rk1808-pvtm",
		.data = (void *)&rk1808_pvtm,
	},
	{
		.compatible = "rockchip,rk1808-pmu-pvtm",
		.data = (void *)&rk1808_pmupvtm,
	},
	{
		.compatible = "rockchip,rk1808-npu-pvtm",
		.data = (void *)&rk1808_npupvtm,
	},
	{
		.compatible = "rockchip,rk3288-pvtm",
		.data = (void *)&rk3288_pvtm,
	},
	{
		.compatible = "rockchip,rk3308-pvtm",
		.data = (void *)&px30_pvtm,
	},
	{
		.compatible = "rockchip,rk3308-pmu-pvtm",
		.data = (void *)&rk3308_pmupvtm,
	},
	{
		.compatible = "rockchip,rk3366-pvtm",
		.data = (void *)&rk3366_pvtm,
	},
	{
		.compatible = "rockchip,rk3366-pmu-pvtm",
		.data = (void *)&rk3366_pmupvtm,
	},
	{
		.compatible = "rockchip,rk3399-pvtm",
		.data = (void *)&rk3399_pvtm,
	},
	{
		.compatible = "rockchip,rk3399-pmu-pvtm",
		.data = (void *)&rk3399_pmupvtm,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_pvtm_match);

static int rockchip_pvtm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	const struct rockchip_pvtm_info *info;
	struct rockchip_pvtm *pvtm;
	struct regmap *grf;
	struct thermal_zone_device *pvtm_tz = NULL;
	const char *tz_name;
	int i;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "missing pvtm data\n");
		return -EINVAL;
	}

	info = match->data;

	if (!dev->parent || !dev->parent->of_node)
		return -EINVAL;

	grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(grf))
		return PTR_ERR(grf);

	pvtm = devm_kzalloc(dev, sizeof(*pvtm) * info->num_channels,
			    GFP_KERNEL);
	if (!pvtm)
		return -ENOMEM;

	if (!of_property_read_string(np, "thermal-zone", &tz_name)) {
		pvtm_tz = thermal_zone_get_zone_by_name(tz_name);
		if (IS_ERR(pvtm_tz)) {
			dev_err(dev, "debug failed to get pvtm_tz\n");
			pvtm_tz = NULL;
		}
	}

	for (i = 0; i < info->num_channels; i++) {
		pvtm[i].dev = &pdev->dev;
		pvtm[i].grf = grf;
		pvtm[i].con = info->con;
		pvtm[i].sta = info->sta;
		pvtm[i].get_value = info->get_value;
		pvtm[i].channel = &info->channels[i];
		pvtm[i].tz = pvtm_tz;
		if (info->set_ring_sel)
			pvtm[i].set_ring_sel = info->set_ring_sel;

		pvtm[i].clk = devm_clk_get(dev, info->channels[i].clk_name);
		if (IS_ERR_OR_NULL(pvtm[i].clk)) {
			dev_err(dev, "failed to get clk %d %s\n", i,
				info->channels[i].clk_name);
			return PTR_ERR(pvtm[i].clk);
		}

		pvtm[i].rst =
			devm_reset_control_get(dev, info->channels[i].clk_name);
		if (IS_ERR_OR_NULL(pvtm[i].rst)) {
			dev_info(dev, "failed to get rst %d %s\n", i,
				 info->channels[i].clk_name);
			pvtm[i].rst = NULL;
		}

		list_add(&pvtm[i].node, &pvtm_list);
	}

	return 0;
}

static struct platform_driver rockchip_pvtm_driver = {
	.probe = rockchip_pvtm_probe,
	.driver = {
		.name  = "rockchip-pvtm",
		.of_match_table = rockchip_pvtm_match,
	},
};

module_platform_driver(rockchip_pvtm_driver);

MODULE_DESCRIPTION("Rockchip PVTM driver");
MODULE_AUTHOR("Finley Xiao <finley.xiao@rock-chips.com>");
MODULE_LICENSE("GPL v2");
