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

#define PVTM(_id, _name, _num_rings, _start, _en, _cal, _done, _freq)	\
{					\
	.id = _id,			\
	.name = _name,			\
	.num_rings = _num_rings,	\
	.bit_start = _start,		\
	.bit_en = _en,			\
	.reg_cal = _cal,		\
	.bit_freq_done = _done,		\
	.reg_freq = _freq,		\
}

struct rockchip_pvtm;

struct rockchip_pvtm_ops {
	u32 (*get_value)(struct rockchip_pvtm *pvtm, unsigned int ring_sel,
			 unsigned int time_us);
	void (*set_ring_sel)(struct rockchip_pvtm *pvtm, unsigned int ring_sel);
};

struct rockchip_pvtm_info {
	u32 reg_cal;
	u32 reg_freq;
	unsigned char id;
	unsigned char *name;
	unsigned int num_rings;
	unsigned int bit_start;
	unsigned int bit_en;
	unsigned int bit_freq_done;
};

struct rockchip_pvtm_data {
	u32 con;
	u32 sta;
	unsigned int num_pvtms;
	const struct rockchip_pvtm_info *infos;
	const struct rockchip_pvtm_ops ops;
};

struct rockchip_pvtm {
	u32 con;
	u32 sta;
	struct list_head node;
	struct device *dev;
	struct regmap *grf;
	void __iomem *base;
	int num_clks;
	struct clk_bulk_data *clks;
	struct reset_control *rst;
	struct thermal_zone_device *tz;
	const struct rockchip_pvtm_info *info;
	const struct rockchip_pvtm_ops *ops;
};

static LIST_HEAD(pvtm_list);

#ifdef CONFIG_DEBUG_FS

static struct dentry *rootdir;

static int pvtm_value_show(struct seq_file *s, void *data)
{
	struct rockchip_pvtm *pvtm = (struct rockchip_pvtm *)s->private;
	u32 value;
	int i, ret, cur_temp;

	if (!pvtm || !pvtm->ops->get_value) {
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
	for (i = 0; i < pvtm->info->num_rings; i++) {
		value = pvtm->ops->get_value(pvtm, i, 1000);
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
		dentry = debugfs_create_dir(pvtm->info->name, rootdir);
		if (!dentry) {
			dev_err(pvtm->dev, "failed to creat pvtm %s debug dir\n",
				pvtm->info->name);
			return -ENOMEM;
		}

		d = debugfs_create_file("value", 0444, dentry,
					(void *)pvtm, &pvtm_value_fops);
		if (!d) {
			dev_err(pvtm->dev, "failed to pvtm %s value node\n",
				pvtm->info->name);
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

u32 rockchip_get_pvtm_value(unsigned int id, unsigned int ring_sel,
			    unsigned int time_us)
{
	struct rockchip_pvtm *p, *pvtm = NULL;

	if (list_empty(&pvtm_list)) {
		pr_err("pvtm list NULL\n");
		return -EINVAL;
	}

	list_for_each_entry(p, &pvtm_list, node) {
		if (p->info->id == id) {
			pvtm = p;
			break;
		}
	}

	if (!pvtm) {
		pr_err("invalid pvtm id %d\n", id);
		return -EINVAL;
	}

	if (ring_sel >= pvtm->info->num_rings) {
		pr_err("invalid pvtm ring %d\n", ring_sel);
		return -EINVAL;
	}

	return pvtm->ops->get_value(pvtm, ring_sel, time_us);
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
				   unsigned int ring_sel)
{
	unsigned int id = pvtm->info->id;

	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(ring_sel, (id * 0x4 + 0x2), 0x3));
}

static void rk1808_pvtm_set_ring_sel(struct rockchip_pvtm *pvtm,
				     unsigned int ring_sel)
{
	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(ring_sel, 0x2, 0x7));
}

static void rk3399_pvtm_set_ring_sel(struct rockchip_pvtm *pvtm,
				     unsigned int ring_sel)
{
	unsigned int id = pvtm->info->id;

	if (id == 1) {
		regmap_write(pvtm->grf, pvtm->con + 0x14,
			     wr_mask_bit(ring_sel >> 0x3, 0, 0x1));
		ring_sel &= 0x3;
	}
	if (id != 4)
		regmap_write(pvtm->grf, pvtm->con,
			     wr_mask_bit(ring_sel, (id * 0x4 + 0x2), 0x3));
}

static u32 rockchip_pvtm_get_value(struct rockchip_pvtm *pvtm,
				   unsigned int ring_sel,
				   unsigned int time_us)
{
	const struct rockchip_pvtm_info *info = pvtm->info;
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
	if (sta & BIT(info->bit_en))
		regmap_write(pvtm->grf, pvtm->con,
			     wr_mask_bit(0, info->bit_start, 0x1));

	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(0x1, info->bit_en, 0x1));

	if (pvtm->ops->set_ring_sel)
		pvtm->ops->set_ring_sel(pvtm, ring_sel);

	/* clk = 24 Mhz, T = 1 / 24 us */
	clk_cnt = time_us * 24;
	regmap_write(pvtm->grf, pvtm->con + info->reg_cal, clk_cnt);

	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(0x1, info->bit_start, 0x1));

	rockchip_pvtm_delay(time_us);

	while (check_cnt) {
		regmap_read(pvtm->grf, pvtm->sta, &sta);
		if (sta & BIT(info->bit_freq_done))
			break;
		udelay(4);
		check_cnt--;
	}

	if (check_cnt) {
		regmap_read(pvtm->grf, pvtm->sta + info->reg_freq, &val);
	} else {
		dev_err(pvtm->dev, "wait pvtm_done timeout!\n");
		val = 0;
	}

	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(0, info->bit_start, 0x1));

	regmap_write(pvtm->grf, pvtm->con,
		     wr_mask_bit(0, info->bit_en, 0x1));

disable_clks:
	clk_bulk_disable_unprepare(pvtm->num_clks, pvtm->clks);

	return val;
}

static void rv1126_pvtm_set_ring_sel(struct rockchip_pvtm *pvtm,
				     unsigned int ring_sel)
{
	writel_relaxed(wr_mask_bit(ring_sel, 0x2, 0x7), pvtm->base + pvtm->con);
}

static u32 rv1126_pvtm_get_value(struct rockchip_pvtm *pvtm,
				 unsigned int ring_sel,
				 unsigned int time_us)
{
	const struct rockchip_pvtm_info *info = pvtm->info;
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
	sta = readl_relaxed(pvtm->base + pvtm->con);
	if (sta & BIT(info->bit_en))
		writel_relaxed(wr_mask_bit(0, info->bit_start, 0x1),
			       pvtm->base + pvtm->con);

	writel_relaxed(wr_mask_bit(0x1, info->bit_en, 0x1),
		       pvtm->base + pvtm->con);

	if (pvtm->ops->set_ring_sel)
		pvtm->ops->set_ring_sel(pvtm, ring_sel);

	/* clk = 24 Mhz, T = 1 / 24 us */
	clk_cnt = time_us * 24;
	writel_relaxed(clk_cnt, pvtm->base + pvtm->con + info->reg_cal);

	writel_relaxed(wr_mask_bit(0x1, info->bit_start, 0x1),
		       pvtm->base + pvtm->con);

	rockchip_pvtm_delay(time_us);

	while (check_cnt) {
		sta = readl_relaxed(pvtm->base + pvtm->sta);
		if (sta & BIT(info->bit_freq_done))
			break;
		udelay(4);
		check_cnt--;
	}

	if (check_cnt) {
		val = readl_relaxed(pvtm->base + pvtm->sta + info->reg_freq);
	} else {
		dev_err(pvtm->dev, "wait pvtm_done timeout!\n");
		val = 0;
	}

	writel_relaxed(wr_mask_bit(0, info->bit_start, 0x1),
		       pvtm->base + pvtm->con);
	writel_relaxed(wr_mask_bit(0, info->bit_en, 0x1),
		       pvtm->base + pvtm->con);

disable_clks:
	clk_bulk_disable_unprepare(pvtm->num_clks, pvtm->clks);

	return val;
}

static const struct rockchip_pvtm_info px30_pvtm_infos[] = {
	PVTM(0, "core", 3, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data px30_pvtm = {
	.con = 0x80,
	.sta = 0x88,
	.num_pvtms = ARRAY_SIZE(px30_pvtm_infos),
	.infos = px30_pvtm_infos,
	.ops = {
		.get_value = rockchip_pvtm_get_value,
		.set_ring_sel = px30_pvtm_set_ring_sel,
	},
};

static const struct rockchip_pvtm_info px30_pmupvtm_infos[] = {
	PVTM(1, "pmu", 1, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data px30_pmupvtm = {
	.con = 0x180,
	.sta = 0x190,
	.num_pvtms = ARRAY_SIZE(px30_pmupvtm_infos),
	.infos = px30_pmupvtm_infos,
	.ops =  {
		.get_value = rockchip_pvtm_get_value,
	},
};

static const struct rockchip_pvtm_info rk1808_pvtm_infos[] = {
	PVTM(0, "core", 5, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data rk1808_pvtm = {
	.con = 0x80,
	.sta = 0x88,
	.num_pvtms = ARRAY_SIZE(rk1808_pvtm_infos),
	.infos = rk1808_pvtm_infos,
	.ops = {
		.get_value = rockchip_pvtm_get_value,
		.set_ring_sel = rk1808_pvtm_set_ring_sel,
	},
};

static const struct rockchip_pvtm_info rk1808_pmupvtm_infos[] = {
	PVTM(1, "pmu", 1, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data rk1808_pmupvtm = {
	.con = 0x180,
	.sta = 0x190,
	.num_pvtms = ARRAY_SIZE(rk1808_pmupvtm_infos),
	.infos = rk1808_pmupvtm_infos,
	.ops = {
		.get_value = rockchip_pvtm_get_value,
	},
};

static const struct rockchip_pvtm_info rk1808_npupvtm_infos[] = {
	PVTM(2, "npu", 5, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data rk1808_npupvtm = {
	.con = 0x780,
	.sta = 0x788,
	.num_pvtms = ARRAY_SIZE(rk1808_npupvtm_infos),
	.infos = rk1808_npupvtm_infos,
	.ops = {
		.get_value = rockchip_pvtm_get_value,
		.set_ring_sel = rk1808_pvtm_set_ring_sel,
	},
};

static const struct rockchip_pvtm_info rk3288_pvtm_infos[] = {
	PVTM(0, "core", 1, 0, 1, 0x4, 1, 0x4),
	PVTM(1, "gpu", 1, 8, 9, 0x8, 0, 0x8),
};

static const struct rockchip_pvtm_data rk3288_pvtm = {
	.con = 0x368,
	.sta = 0x374,
	.num_pvtms = ARRAY_SIZE(rk3288_pvtm_infos),
	.infos = rk3288_pvtm_infos,
	.ops = {
		.get_value = rockchip_pvtm_get_value,
	},
};

static const struct rockchip_pvtm_data rk3308_pmupvtm = {
	.con = 0x440,
	.sta = 0x448,
	.num_pvtms = ARRAY_SIZE(px30_pmupvtm_infos),
	.infos = px30_pmupvtm_infos,
	.ops = {
		.get_value = rockchip_pvtm_get_value,
	},
};

static const struct rockchip_pvtm_info rk3399_pvtm_infos[] = {
	PVTM(0, "core_l", 4, 0, 1, 0x4, 0, 0x4),
	PVTM(1, "core_b", 6, 4, 5, 0x8, 1, 0x8),
	PVTM(2, "ddr", 4, 8, 9, 0xc, 3, 0x10),
	PVTM(3, "gpu", 4, 12, 13, 0x10, 2, 0xc),
};

static const struct rockchip_pvtm_data rk3399_pvtm = {
	.con = 0xe600,
	.sta = 0xe620,
	.num_pvtms = ARRAY_SIZE(rk3399_pvtm_infos),
	.infos = rk3399_pvtm_infos,
	.ops = {
		.get_value = rockchip_pvtm_get_value,
		.set_ring_sel = rk3399_pvtm_set_ring_sel,
	},
};

static const struct rockchip_pvtm_info rk3399_pmupvtm_infos[] = {
	PVTM(4, "pmu", 1, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data rk3399_pmupvtm = {
	.con = 0x240,
	.sta = 0x248,
	.num_pvtms = ARRAY_SIZE(rk3399_pmupvtm_infos),
	.infos = rk3399_pmupvtm_infos,
	.ops = {
		.get_value = rockchip_pvtm_get_value,
	},
};

static const struct rockchip_pvtm_info rk3568_corepvtm_infos[] = {
	PVTM(0, "core", 7, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data rk3568_corepvtm = {
	.con = 0x4,
	.sta = 0x80,
	.num_pvtms = ARRAY_SIZE(rk3568_corepvtm_infos),
	.infos = rk3568_corepvtm_infos,
	.ops = {
		.get_value = rv1126_pvtm_get_value,
		.set_ring_sel = rv1126_pvtm_set_ring_sel,
	},
};

static const struct rockchip_pvtm_info rk3568_gpupvtm_infos[] = {
	PVTM(1, "gpu", 7, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data rk3568_gpupvtm = {
	.con = 0x4,
	.sta = 0x80,
	.num_pvtms = ARRAY_SIZE(rk3568_gpupvtm_infos),
	.infos = rk3568_gpupvtm_infos,
	.ops = {
		.get_value = rv1126_pvtm_get_value,
		.set_ring_sel = rv1126_pvtm_set_ring_sel,
	},
};

static const struct rockchip_pvtm_info rk3568_npupvtm_infos[] = {
	PVTM(2, "npu", 7, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data rk3568_npupvtm = {
	.con = 0x4,
	.sta = 0x80,
	.num_pvtms = ARRAY_SIZE(rk3568_npupvtm_infos),
	.infos = rk3568_npupvtm_infos,
	.ops = {
		.get_value = rv1126_pvtm_get_value,
		.set_ring_sel = rv1126_pvtm_set_ring_sel,
	},
};

static const struct rockchip_pvtm_info rv1126_cpupvtm_infos[] = {
	PVTM(0, "cpu", 7, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data rv1126_cpupvtm = {
	.con = 0x4,
	.sta = 0x80,
	.num_pvtms = ARRAY_SIZE(rv1126_cpupvtm_infos),
	.infos = rv1126_cpupvtm_infos,
	.ops = {
		.get_value = rv1126_pvtm_get_value,
		.set_ring_sel = rv1126_pvtm_set_ring_sel,
	},
};

static const struct rockchip_pvtm_info rv1126_npupvtm_infos[] = {
	PVTM(1, "npu", 7, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data rv1126_npupvtm = {
	.con = 0x4,
	.sta = 0x80,
	.num_pvtms = ARRAY_SIZE(rv1126_npupvtm_infos),
	.infos = rv1126_npupvtm_infos,
	.ops = {
		.get_value = rv1126_pvtm_get_value,
		.set_ring_sel = rv1126_pvtm_set_ring_sel,
	},
};

static const struct rockchip_pvtm_info rv1126_pmupvtm_infos[] = {
	PVTM(2, "pmu", 1, 0, 1, 0x4, 0, 0x4),
};

static const struct rockchip_pvtm_data rv1126_pmupvtm = {
	.con = 0x4,
	.sta = 0x80,
	.num_pvtms = ARRAY_SIZE(rv1126_pmupvtm_infos),
	.infos = rv1126_pmupvtm_infos,
	.ops = {
		.get_value = rv1126_pvtm_get_value,
	},
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
	{
		.compatible = "rockchip,rK3568-core-pvtm",
		.data = (void *)&rk3568_corepvtm,
	},
	{
		.compatible = "rockchip,rk3568-gpu-pvtm",
		.data = (void *)&rk3568_gpupvtm,
	},
	{
		.compatible = "rockchip,rk3568-npu-pvtm",
		.data = (void *)&rk3568_npupvtm,
	},
	{
		.compatible = "rockchip,rv1126-cpu-pvtm",
		.data = (void *)&rv1126_cpupvtm,
	},
	{
		.compatible = "rockchip,rv1126-npu-pvtm",
		.data = (void *)&rv1126_npupvtm,
	},
	{
		.compatible = "rockchip,rv1126-pmu-pvtm",
		.data = (void *)&rv1126_pmupvtm,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_pvtm_match);

static int rockchip_pvtm_get_index(const struct rockchip_pvtm_data *data,
				   u32 ch, u32 *index)
{
	int i;

	for (i = 0; i < data->num_pvtms; i++) {
		if (ch == data->infos[i].id) {
			*index = i;
			return 0;
		}
	}

	return -EINVAL;
}

static struct rockchip_pvtm *
rockchip_pvtm_init(struct device *dev, struct device_node *node,
		   const struct rockchip_pvtm_data *data,
		   struct regmap *grf, void __iomem *base)
{
	struct rockchip_pvtm *pvtm;
	const char *tz_name;
	u32 id, index, i;

	if (of_property_read_u32(node, "reg", &id)) {
		dev_err(dev, "%s: failed to retrieve pvtm id\n", node->name);
		return NULL;
	}
	if (rockchip_pvtm_get_index(data, id, &index)) {
		dev_err(dev, "%s: invalid pvtm id %d\n", node->name, id);
		return NULL;
	}

	pvtm = devm_kzalloc(dev, sizeof(*pvtm), GFP_KERNEL);
	if (!pvtm)
		return NULL;

	pvtm->dev = dev;
	pvtm->grf = grf;
	pvtm->base = base;
	pvtm->con = data->con;
	pvtm->sta = data->sta;
	pvtm->ops = &data->ops;
	pvtm->info = &data->infos[index];

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

static void rockchip_del_pvtm(const struct rockchip_pvtm_data *data)
{
	struct rockchip_pvtm *pvtm, *tmp;
	int i;

	if (list_empty(&pvtm_list))
		return;

	for (i = 0; i < data->num_pvtms; i++) {
		list_for_each_entry_safe(pvtm, tmp, &pvtm_list, node) {
			if (pvtm->info->id == data->infos[i].id)
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
	struct regmap *grf = NULL;
	void __iomem *base = NULL;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "missing pvtm data\n");
		return -EINVAL;
	}

	if (dev->parent && dev->parent->of_node) {
		grf = syscon_node_to_regmap(dev->parent->of_node);
		if (IS_ERR(grf))
			return PTR_ERR(grf);
	} else {
		base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(base))
			return PTR_ERR(base);
	}

	for_each_available_child_of_node(np, node) {
		pvtm = rockchip_pvtm_init(dev, node, match->data, grf, base);
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
