// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2014, 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "devfreq-icc: " fmt

#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_fdt.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/time.h>

#include <soc/qcom/devfreq_icc.h>
#include <soc/qcom/of_common.h>

#include <trace/events/power.h>

/* Has to be UL to avoid errors in 32 bit. Use cautiously to avoid overflows.*/
#define MBYTE			(1UL << 20)
#define HZ_TO_MBPS(hz, w)	(mult_frac(w, hz, MBYTE))
#define MBPS_TO_HZ(mbps, w)	(mult_frac(mbps, MBYTE, w))
#define MBPS_TO_ICC(mbps)	(mult_frac(mbps, MBYTE, 1000))

enum dev_type {
	STD_MBPS_DEV,
	L3_HZ_DEV,
	L3_MBPS_DEV,
	NUM_DEV_TYPES
};

struct devfreq_icc_spec {
	enum dev_type	type;
};

struct dev_data {
	struct icc_path			*icc_path;
	u32				cur_ab;
	u32				cur_ib;
	unsigned long			gov_ab;
	const struct devfreq_icc_spec	*spec;
	unsigned int			width;
	struct devfreq			*df;
	struct devfreq_dev_profile	dp;
};

#define MAX_L3_ENTRIES	40U
static unsigned long	l3_freqs[MAX_L3_ENTRIES];
static			DEFINE_MUTEX(l3_freqs_lock);
static bool		use_cached_l3_freqs;

static u64 mbps_to_hz_icc(u32 in, uint width)
{
	u64 result;
	u32 quot = in / width;
	u32 rem = in % width;

	result = quot * MBYTE + div_u64(rem * MBYTE, width);
	return result;
}

static int set_bw(struct device *dev, u32 new_ib, u32 new_ab)
{
	struct dev_data *d = dev_get_drvdata(dev);
	int ret;
	u64 icc_ib = new_ib, icc_ab = new_ab;

	if (d->cur_ib == new_ib && d->cur_ab == new_ab)
		return 0;

	if (d->spec->type == L3_MBPS_DEV) {
		icc_ib = mbps_to_hz_icc(new_ib, d->width);
		icc_ab = mbps_to_hz_icc(new_ab, d->width);
	} else if (d->spec->type == STD_MBPS_DEV) {
		icc_ib = mbps_to_hz_icc(new_ib, 1000);
		icc_ab = mbps_to_hz_icc(new_ab, 1000);
	}

	dev_dbg(dev, "ICC BW: AB: %llu IB: %llu\n", icc_ab, icc_ib);

	ret = icc_set_bw(d->icc_path, icc_ab, icc_ib);
	if (ret < 0) {
		dev_err(dev, "icc set bandwidth request failed (%d)\n", ret);
	} else {
		d->cur_ib = new_ib;
		d->cur_ab = new_ab;
	}

	return ret;
}

static int icc_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct dev_data *d = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (!IS_ERR(opp))
		dev_pm_opp_put(opp);

	return set_bw(dev, *freq, d->gov_ab);
}

static int icc_get_dev_status(struct device *dev,
				struct devfreq_dev_status *stat)
{
	struct dev_data *d = dev_get_drvdata(dev);

	stat->private_data = &d->gov_ab;
	return 0;
}

#define INIT_HZ			300000000UL
#define XO_HZ			19200000UL
#define FTBL_ROW_SIZE		4
#define SRC_MASK		GENMASK(31, 30)
#define SRC_SHIFT		30
#define MULT_MASK		GENMASK(7, 0)

static int populate_l3_opp_table(struct device *dev)
{
	struct dev_data *d = dev_get_drvdata(dev);
	int idx, ret;
	u32 data, src, mult, i;
	unsigned long freq, prev_freq = 0;
	struct resource res;
	void __iomem *ftbl_base;
	unsigned int ftbl_row_size = FTBL_ROW_SIZE;

	idx = of_property_match_string(dev->of_node, "reg-names", "ftbl-base");
	if (idx < 0) {
		dev_err(dev, "Unable to find ftbl-base: %d\n", idx);
		return -EINVAL;
	}

	ret = of_address_to_resource(dev->of_node, idx, &res);
	if (ret < 0) {
		dev_err(dev, "Unable to get resource from address: %d\n", ret);
		return -EINVAL;
	}

	ftbl_base = ioremap(res.start, resource_size(&res));
	if (!ftbl_base) {
		dev_err(dev, "Unable to map ftbl-base!\n");
		return -ENOMEM;
	}

	of_property_read_u32(dev->of_node, "qcom,ftbl-row-size",
						&ftbl_row_size);

	for (i = 0; i < MAX_L3_ENTRIES; i++) {
		data = readl_relaxed(ftbl_base + i * ftbl_row_size);
		src = ((data & SRC_MASK) >> SRC_SHIFT);
		mult = (data & MULT_MASK);
		freq = src ? XO_HZ * mult : INIT_HZ;

		/* Two of the same frequencies means end of table */
		if (i > 0 && prev_freq == freq)
			break;

		if (d->spec->type == L3_MBPS_DEV)
			dev_pm_opp_add(dev, HZ_TO_MBPS(freq, d->width), 0);
		else
			dev_pm_opp_add(dev, freq, 0);
		l3_freqs[i] = freq;
		prev_freq = freq;
	}

	iounmap(ftbl_base);
	use_cached_l3_freqs = true;

	return 0;
}

static int copy_l3_opp_table(struct device *dev)
{
	struct dev_data *d = dev_get_drvdata(dev);
	int idx;

	for (idx = 0; idx < MAX_L3_ENTRIES; idx++) {
		if (!l3_freqs[idx])
			break;

		if (d->spec->type == L3_MBPS_DEV)
			dev_pm_opp_add(dev,
					HZ_TO_MBPS(l3_freqs[idx], d->width), 0);
		else
			dev_pm_opp_add(dev, l3_freqs[idx], 0);
	}

	if (!idx) {
		dev_err(dev, "No L3 frequencies copied for device!\n");
		return -EINVAL;
	}

	return 0;
}

#define PROP_ACTIVE	"qcom,active-only"
#define ACTIVE_ONLY_TAG	0x3

int devfreq_add_icc(struct device *dev)
{
	struct dev_data *d;
	struct devfreq_dev_profile *p;
	const char *gov_name;
	int ret;
	struct opp_table *opp_table;
	u32 version;

	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	dev_set_drvdata(dev, d);

	d->spec = of_device_get_match_data(dev);
	if (!d->spec) {
		dev_err(dev, "Unknown device type!\n");
		return -ENODEV;
	}

	p = &d->dp;
	p->polling_ms = 500;
	p->target = icc_target;
	p->get_dev_status = icc_get_dev_status;

	if (of_device_is_compatible(dev->of_node, "qcom,devfreq-icc-ddr")) {
		version = (1 << of_fdt_get_ddrtype());
		opp_table = dev_pm_opp_get_opp_table(dev);
		if (IS_ERR(opp_table)) {
			dev_err(dev, "Failed to set supported hardware\n");
			return PTR_ERR(opp_table);
		}
	}

	if (d->spec->type == L3_MBPS_DEV) {
		ret = of_property_read_u32(dev->of_node, "qcom,bus-width",
						&d->width);
		if (ret < 0 || !d->width) {
			dev_err(dev, "Missing or invalid bus-width: %d\n", ret);
			return -EINVAL;
		}
	}

	if (d->spec->type == L3_HZ_DEV || d->spec->type == L3_MBPS_DEV) {
		mutex_lock(&l3_freqs_lock);
		if (use_cached_l3_freqs) {
			mutex_unlock(&l3_freqs_lock);
			ret = copy_l3_opp_table(dev);
		} else {
			ret = populate_l3_opp_table(dev);
			mutex_unlock(&l3_freqs_lock);
		}
	} else {
		ret = dev_pm_opp_of_add_table(dev);
	}
	if (ret < 0)
		dev_err(dev, "Couldn't parse OPP table:%d\n", ret);

	d->icc_path = of_icc_get(dev, NULL);
	if (IS_ERR(d->icc_path)) {
		ret = PTR_ERR(d->icc_path);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Unable to register icc path: %d\n", ret);
		return ret;
	}

	if (of_property_read_bool(dev->of_node, PROP_ACTIVE))
		icc_set_tag(d->icc_path, ACTIVE_ONLY_TAG);

	if (of_property_read_string(dev->of_node, "governor", &gov_name))
		gov_name = "performance";

	d->df = devfreq_add_device(dev, p, gov_name, NULL);
	if (IS_ERR(d->df)) {
		icc_put(d->icc_path);
		return PTR_ERR(d->df);
	}

	return 0;
}

int devfreq_remove_icc(struct device *dev)
{
	struct dev_data *d = dev_get_drvdata(dev);

	icc_put(d->icc_path);
	devfreq_remove_device(d->df);
	return 0;
}

int devfreq_suspend_icc(struct device *dev)
{
	struct dev_data *d = dev_get_drvdata(dev);

	return devfreq_suspend_device(d->df);
}

int devfreq_resume_icc(struct device *dev)
{
	struct dev_data *d = dev_get_drvdata(dev);

	return devfreq_resume_device(d->df);
}

static int devfreq_icc_probe(struct platform_device *pdev)
{
	return devfreq_add_icc(&pdev->dev);
}

static int devfreq_icc_remove(struct platform_device *pdev)
{
	return devfreq_remove_icc(&pdev->dev);
}

static const struct devfreq_icc_spec spec[] = {
	[0] = { STD_MBPS_DEV },
	[1] = { L3_HZ_DEV },
	[2] = { L3_MBPS_DEV },
};

static const struct of_device_id devfreq_icc_match_table[] = {
	{ .compatible = "qcom,devfreq-icc-l3bw", .data = &spec[2] },
	{ .compatible = "qcom,devfreq-icc-l3", .data = &spec[1] },
	{ .compatible = "qcom,devfreq-icc-llcc", .data = &spec[0] },
	{ .compatible = "qcom,devfreq-icc-ddr", .data = &spec[0] },
	{ .compatible = "qcom,devfreq-icc", .data = &spec[0] },
	{}
};

static struct platform_driver devfreq_icc_driver = {
	.probe = devfreq_icc_probe,
	.remove = devfreq_icc_remove,
	.driver = {
		.name = "devfreq-icc",
		.of_match_table = devfreq_icc_match_table,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(devfreq_icc_driver);

MODULE_DESCRIPTION("Device DDR bandwidth voting driver MSM SoCs");
MODULE_LICENSE("GPL");
