// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "qcom-dcvs-epss: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <soc/qcom/dcvs.h>
#include "dcvs_private.h"

struct epss_dev_data {
	void __iomem	*l3_base;
	u32		l3_shared_offset;
	u32		*l3_percpu_offsets;
};

struct epss_dev_data *epss_data;
static DEFINE_MUTEX(epss_lock);

#define L3_VOTING_OFFSET	0x90
#define L3_DOMAIN_OFFSET	0x1000

#define MAX_L3_ENTRIES		40U
#define INIT_HZ			300000000UL
#define XO_HZ			19200000UL
#define FTBL_ROW_SIZE		4
#define SRC_MASK		GENMASK(31, 30)
#define SRC_SHIFT		30
#define MULT_MASK		GENMASK(7, 0)
int populate_l3_table(struct device *dev, u32 **freq_table)
{
	int idx, ret, len;
	u32 data, src, mult;
	unsigned long freq, prev_freq = 0;
	struct resource res;
	void __iomem *ftbl_base;
	unsigned int ftbl_row_size;
	u32 *tmp_l3_table;

	idx = of_property_match_string(dev->of_node, "reg-names", "l3tbl-base");
	if (idx < 0) {
		dev_err(dev, "Unable to find l3tbl-base: %d\n", idx);
		return -EINVAL;
	}

	ret = of_address_to_resource(dev->of_node, idx, &res);
	if (ret < 0) {
		dev_err(dev, "Unable to get resource from address: %d\n", ret);
		return -EINVAL;
	}

	ftbl_base = ioremap(res.start, resource_size(&res));
	if (!ftbl_base) {
		dev_err(dev, "Unable to map l3tbl-base!\n");
		return -ENOMEM;
	}

	ret = of_property_read_u32(dev->of_node, "qcom,ftbl-row-size",
						&ftbl_row_size);
	if (ret < 0)
		ftbl_row_size = FTBL_ROW_SIZE;

	tmp_l3_table = kcalloc(MAX_L3_ENTRIES, sizeof(*tmp_l3_table), GFP_KERNEL);
	if (!tmp_l3_table) {
		iounmap(ftbl_base);
		return -ENOMEM;
	}

	for (idx = 0; idx < MAX_L3_ENTRIES; idx++) {
		data = readl_relaxed(ftbl_base + idx * ftbl_row_size);
		src = ((data & SRC_MASK) >> SRC_SHIFT);
		mult = (data & MULT_MASK);
		freq = src ? XO_HZ * mult : INIT_HZ;

		/* Two of the same frequencies means end of table */
		if (idx > 0 && prev_freq == freq)
			break;

		tmp_l3_table[idx] = freq / 1000UL;
		prev_freq = freq;
	}
	len = idx;

	*freq_table = devm_kzalloc(dev, len * sizeof(**freq_table), GFP_KERNEL);
	if (!*freq_table) {
		iounmap(ftbl_base);
		return -ENOMEM;
	}

	for (idx = 0; idx < len; idx++)
		(*freq_table)[idx] = tmp_l3_table[idx];

	iounmap(ftbl_base);
	kfree(tmp_l3_table);

	return len;
}

static int commit_epss_l3(struct dcvs_path *path, struct dcvs_freq *freqs,
						u32 update_mask, bool shared)
{
	struct dcvs_hw *hw = path->hw;
	struct epss_dev_data *d = path->data;
	int cpu;
	u32 idx, offset;

	for (idx = 0; idx < hw->table_len; idx++)
		if (freqs->ib <= hw->freq_table[idx])
			break;

	if (hw->type == DCVS_L3 || hw->type == DCVS_L3_1) {
		if (shared)
			offset = d->l3_shared_offset;
		else {
			cpu = smp_processor_id();
			offset = d->l3_percpu_offsets[cpu];
		}
		writel_relaxed(idx, d->l3_base + offset);
	}

	path->cur_freq.ib = freqs->ib;

	return 0;
}

static int commit_epss_l3_shared(struct dcvs_path *path,
						struct dcvs_freq *freqs,
						u32 update_mask)
{
	return commit_epss_l3(path, freqs, update_mask, true);
}

static int commit_epss_l3_percpu(struct dcvs_path *path,
						struct dcvs_freq *freqs,
						u32 update_mask)
{
	return commit_epss_l3(path, freqs, update_mask, false);
}

static int init_epss_data(struct device *dev)
{
	int idx, ret = 0;
	struct resource res;

	epss_data = devm_kzalloc(dev, sizeof(*epss_data), GFP_KERNEL);
	if (!epss_data)
		return -ENOMEM;

	idx = of_property_match_string(dev->parent->of_node, "reg-names",
								"l3-base");
	if (idx < 0) {
		dev_err(dev, "%s: Unable to find l3-base: %d\n", __func__, idx);
		return -EINVAL;
	}

	ret = of_address_to_resource(dev->parent->of_node, idx, &res);
	if (ret < 0) {
		dev_err(dev, "Unable to get resource from address: %d\n", ret);
		return ret;
	}

	epss_data->l3_base = devm_ioremap(dev->parent, res.start,
							resource_size(&res));
	if (!epss_data->l3_base) {
		dev_err(dev, "Unable to map l3-base!\n");
		return -ENOMEM;
	}

	return ret;

}

static int populate_shared_offset(struct device *dev, u32 *offset)
{
	int ret;

	ret = of_property_read_u32(dev->of_node, "qcom,shared-offset", offset);
	if (ret < 0) {
		dev_err(dev, "Error reading shared offset: %d\n", ret);
		return ret;
	}

	return ret;
}

#define PERCPU_OFFSETS	"qcom,percpu-offsets"
static int populate_percpu_offsets(struct device *dev, u32 **cpu_offsets)
{
	int ret, len;
	struct device_node *of_node = dev->of_node;

	if (of_parse_phandle(of_node, PERCPU_OFFSETS, 0))
		of_node = of_parse_phandle(of_node, PERCPU_OFFSETS, 0);

	if (!of_find_property(of_node, PERCPU_OFFSETS, &len)) {
		dev_err(dev, "Unable to find percpu offsets prop!\n");
		return -EINVAL;
	}
	len /= sizeof(**cpu_offsets);
	if (len != num_possible_cpus()) {
		dev_err(dev, "Invalid percpu offsets table len=%d\n", len);
		return -EINVAL;
	}

	*cpu_offsets = devm_kzalloc(dev, len * sizeof(**cpu_offsets),
								GFP_KERNEL);
	if (!*cpu_offsets)
		return -ENOMEM;

	ret = of_property_read_u32_array(of_node, PERCPU_OFFSETS, *cpu_offsets,
									len);
	if (ret < 0) {
		dev_err(dev, "Error reading percpu offsets from DT: %d\n", ret);
		return ret;
	}

	return ret;
}

static int setup_epss_l3_device(struct device *dev, struct dcvs_hw *hw,
					struct dcvs_path *path, bool shared)
{
	int ret = 0;

	mutex_lock(&epss_lock);
	if (!epss_data)
		ret = init_epss_data(dev);
	mutex_unlock(&epss_lock);

	if (ret < 0)
		return ret;

	if (shared) {
		ret = populate_shared_offset(dev, &epss_data->l3_shared_offset);
		path->commit_dcvs_freqs = commit_epss_l3_shared;
	} else {
		ret = populate_percpu_offsets(dev, &epss_data->l3_percpu_offsets);
		path->commit_dcvs_freqs = commit_epss_l3_percpu;
	}
	if (ret < 0)
		return ret;

	path->data = epss_data;

	return ret;
}

int setup_epss_l3_sp_device(struct device *dev, struct dcvs_hw *hw,
					struct dcvs_path *path)
{
	return setup_epss_l3_device(dev, hw, path, true);
}

int setup_epss_l3_percpu_device(struct device *dev, struct dcvs_hw *hw,
					struct dcvs_path *path)
{
	return setup_epss_l3_device(dev, hw, path, false);
}
