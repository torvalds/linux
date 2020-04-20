// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip PVTM support.
 *
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/soc/rockchip/pvtm.h>
#include <linux/thermal.h>

#define wr_mask_bit(v, off, mask)	((v) << (off) | (mask) << (16 + off))

#define PVTM(_ch, _name, _num_sub, _start, _en, _cal, _done, _freq)	\
{					\
	.ch = _ch,			\
	.name = _name,			\
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
	unsigned char *name;
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
	int num_clks;
	struct clk_bulk_data *clks;
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

	if (!pvtm || !pvtm->get_value) {
		seq_puts(s, "unsupported\n");
		return 0;
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
		dentry = debugfs_create_dir(pvtm->channel->name, rootdir);
		if (!dentry) {
			dev_err(pvtm->dev, "failed to creat pvtm %s debug dir\n",
				pvtm->channel->name);
			return -ENOMEM;
		}

		d = debugfs_create_file("value", 0444, dentry,
					(void *)pvtm, &pvtm_value_fops);
		if (!d) {
			dev_err(pvtm->dev, "failed to pvtm %s value node\n",
				pvtm->channel->name);
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
	struct rockchip_pvtm *p, *pvtm = NULL;

	if (list_empty(&pvtm_list)) {
		pr_err("pvtm list NULL\n");
		return -EINVAL;
	}

	list_for_each_entry(p, &pvtm_list, node) {
		if (p->channel->ch == ch) {
			pvtm = p;
			break;
		}
	}

	if (!pvtm) {
		pr_err("invalid pvtm ch %d\n", ch);
		return -EINVAL;
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

	if (ch == 1) {
		regmap_write(pvtm->grf, pvtm->con + 0x14,
			     wr_mask_bit(sub_ch >> 0x3, 0, 0x1));
		sub_ch &= 0x3;
	}
	if (ch != 4)
		regmap_write(pvtm->grf, pvtm->con,
			     wr_mask_bit(sub_ch, (ch * 0x4 + 0x2), 0x3));
}

static u32 rockchip_pvtm_get_value(struct rockchip_pvtm *pvtm,
				   unsigned int sub_ch,
				   unsigned int time_us)
{
	const struct rockchip_pvtm_channel *channel = pvtm->channel;
	unsigned int clk_cnt, check_cnt = 100;
	u32 sta, val = 0;
	int ret;

	ret = clk_bulk_prepare_enable(pvtm->num_clks, pvtm->clks);
	if (ret < 0) {
		dev_err(pvtm->dev, "failed to prepare/enable pvtm clks\n");
		return 0;
	}
	ret = rockchip_pvtm_reset(pvtm);
	if (ret) {
		dev_err(pvtm->dev, "failed to reset pvtm\n");
		goto disable_clks;
	}

	/* if last status is enabled, stop calculating cycles first*/
	regmap_read(pvtm->grf, pvtm->con, &sta);
	if (sta & BIT(channel->bit_en))
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

disable_clks:
	clk_bulk_disable_unprepare(pvtm->num_clks, pvtm->clks);

	return val;
}

static const struct rockchip_pvtm_channel px30_pvtm_channels[] = {
	PVTM(0, "core", 3, 0, 1, 0x4, 0, 0x4),
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
	PVTM(1, "pmu", 1, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_info px30_pmupvtm = {
	.con = 0x180,
	.sta = 0x190,
	.num_channels = ARRAY_SIZE(px30_pmupvtm_channels),
	.channels = px30_pmupvtm_channels,
	.get_value = rockchip_pvtm_get_value,
};

static const struct rockchip_pvtm_channel rk1808_pvtm_channels[] = {
	PVTM(0, "core", 5, 0, 1, 0x4, 0, 0x4),
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
	PVTM(1, "pmu", 1, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_info rk1808_pmupvtm = {
	.con = 0x180,
	.sta = 0x190,
	.num_channels = ARRAY_SIZE(rk1808_pmupvtm_channels),
	.channels = rk1808_pmupvtm_channels,
	.get_value = rockchip_pvtm_get_value,
};

static const struct rockchip_pvtm_channel rk1808_npupvtm_channels[] = {
	PVTM(2, "npu", 5, 0, 1, 0x4, 0, 0x4),
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
	PVTM(0, "core", 1, 0, 1, 0x4, 1, 0x4),
	PVTM(1, "gpu", 1, 8, 9, 0x8, 0, 0x8),
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

static const struct rockchip_pvtm_channel rk3399_pvtm_channels[] = {
	PVTM(0, "core_l", 4, 0, 1, 0x4, 0, 0x4),
	PVTM(1, "core_b", 6, 4, 5, 0x8, 1, 0x8),
	PVTM(2, "ddr", 4, 8, 9, 0xc, 3, 0x10),
	PVTM(3, "gpu", 4, 12, 13, 0x10, 2, 0xc),
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
	PVTM(4, "pmu", 1, 0, 1, 0x4, 0, 0x4),
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

static int rockchip_pvtm_get_ch_index(const struct rockchip_pvtm_info *info,
				      u32 ch, u32 *index)
{
	int i;

	for (i = 0; i < info->num_channels; i++) {
		if (ch == info->channels[i].ch) {
			*index = i;
			return 0;
		}
	}

	return -EINVAL;
}

static struct rockchip_pvtm *
rockchip_pvtm_init(struct device *dev, struct device_node *node,
		   const struct rockchip_pvtm_info *info,
		   struct regmap *grf)
{
	struct rockchip_pvtm *pvtm;
	const char *tz_name;
	u32 ch, index, i;

	if (of_property_read_u32(node, "reg", &ch)) {
		dev_err(dev, "%s: failed to retrieve pvtm ch\n", node->name);
		return NULL;
	}
	if (rockchip_pvtm_get_ch_index(info, ch, &index)) {
		dev_err(dev, "%s: invalid pvtm ch %d\n", node->name, ch);
		return NULL;
	}

	pvtm = devm_kzalloc(dev, sizeof(*pvtm), GFP_KERNEL);
	if (!pvtm)
		return NULL;

	pvtm->dev = dev;
	pvtm->grf = grf;
	pvtm->con = info->con;
	pvtm->sta = info->sta;
	pvtm->get_value = info->get_value;
	pvtm->channel = &info->channels[index];
	if (info->set_ring_sel)
		pvtm->set_ring_sel = info->set_ring_sel;

	if (!of_property_read_string(node, "thermal-zone", &tz_name)) {
		pvtm->tz = thermal_zone_get_zone_by_name(tz_name);
		if (IS_ERR(pvtm->tz)) {
			dev_err(pvtm->dev, "failed to retrieve pvtm_tz\n");
			pvtm->tz = NULL;
		}
	}

	pvtm->num_clks = of_clk_get_parent_count(node);
	if (pvtm->num_clks <= 0) {
		dev_err(dev, "%s: does not have clocks\n", node->name);
		return NULL;
	}
	pvtm->clks = devm_kcalloc(dev, pvtm->num_clks, sizeof(*pvtm->clks),
				  GFP_KERNEL);
	if (!pvtm->clks)
		return NULL;
	for (i = 0; i < pvtm->num_clks; i++) {
		pvtm->clks[i].clk = of_clk_get(node, i);
		if (IS_ERR(pvtm->clks[i].clk)) {
			dev_err(dev, "%s: failed to get clk at index %d\n",
				node->name, i);
			return NULL;
		}
	}

	pvtm->rst = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(pvtm->rst))
		dev_dbg(dev, "%s: failed to get reset\n", node->name);

	return pvtm;
}

static void rockchip_del_pvtm(const struct rockchip_pvtm_info *info)
{
	struct rockchip_pvtm *pvtm, *tmp;
	int i;

	if (list_empty(&pvtm_list))
		return;

	for (i = 0; i < info->num_channels; i++) {
		list_for_each_entry_safe(pvtm, tmp, &pvtm_list, node) {
			if (pvtm->channel->ch == info->channels[i].ch)
				list_del(&pvtm->node);
		}
	}
}

static int rockchip_pvtm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *node;
	const struct of_device_id *match;
	struct rockchip_pvtm *pvtm;
	struct regmap *grf;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "missing pvtm data\n");
		return -EINVAL;
	}

	if (!dev->parent || !dev->parent->of_node)
		return -EINVAL;
	grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(grf))
		return PTR_ERR(grf);

	for_each_available_child_of_node(np, node) {
		pvtm = rockchip_pvtm_init(dev, node, match->data, grf);
		if (!pvtm) {
			dev_err(dev, "failed to handle node %s\n", node->name);
			goto error;
		}
		list_add(&pvtm->node, &pvtm_list);
	}

	return 0;

error:
	rockchip_del_pvtm(match->data);

	return -EINVAL;
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
