// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/scmi_protocol.h>
#include <linux/qcom_scmi_vendor.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/debugfs.h>

#define MAX_CLK_DOMAIN 8
#define SCMI_CPUFREQ_STATS_MSG_ID_PROTOCOL_ATTRIBUTES (16)
#define SCMI_CPUFREQ_STATS_DIR_STRING "cpufreq_stats"
#define CPUFREQ_STATS_USAGE_FILENAME "usage"
#define CPUFREQ_STATS_RESIDENCY_FILENAME "time_in_state"
#define SCMI_CPUFREQ_STATS_STR (5045524653544154) /*PERFSTAT ASCII*/
static struct scmi_device *sdev;

enum cpufreq_stat_param_ids {
	PARAM_LOG_LVL = 1,
	PARAM_PROTOCOL_ATTRIBUTE,
};
struct cpufreq_stats_prot_attr {
	uint32_t attributes;
	uint32_t statistics_address_low;
	uint32_t statistics_address_high;
	uint32_t statistics_len;
};
static const struct qcom_scmi_vendor_ops *ops;
static struct scmi_protocol_handle *ph;

enum entry_type {
	usage = 0,
	residency,
	ENTRY_MAX,
};

/* structure to uniquely identify a fs entry */
struct clkdom_entry {
	enum entry_type entry;
	u16 clkdom;
};

static struct dentry *dir;

struct scmi_stats {
	u32 signature;
	u16 revision;
	u16 attributes;
	u16 num_domains;
	u16 reserved0;
	u32 match_sequence;
	u32 perf_dom_entry_off_arr[];
} __packed;

struct perf_lvl_entry {
	u32 perf_lvl;
	u32 reserved0;
	u64 usage;
	u64 residency;
} __packed;

struct perf_dom_entry {
	u16 num_perf_levels;
	u16 curr_perf_idx;
	u32 ext_tbl_off;
	u64 ts_last_change;
	struct perf_lvl_entry perf_lvl_arr[];
} __packed;

struct stats_info {
	u32 stats_size;
	void __iomem *stats_iomem;
	u16 num_clkdom;
	struct clkdom_entry *entries;
	u32 *freq_info;
};

static struct stats_info *pinfo;

static u32 get_num_opps_for_clkdom(u32 clkdom)
{
	u32 dom_data_off;
	void __iomem *dom_data;

	dom_data_off = 4 * readl_relaxed(pinfo->stats_iomem +
					 offsetof(struct scmi_stats,
						  perf_dom_entry_off_arr) +
					 4 * clkdom);
	dom_data = pinfo->stats_iomem + dom_data_off;
	return readl_relaxed(dom_data) & 0xFF;
}

static u32 get_freq_at_idx_for_clkdom(u32 clkdom, u32 idx)
{
	u32 dom_data_off;
	void __iomem *dom_data;

	dom_data_off = 4 * readl_relaxed(pinfo->stats_iomem +
					 offsetof(struct scmi_stats,
						  perf_dom_entry_off_arr) +
					 4 * clkdom);
	dom_data = pinfo->stats_iomem + dom_data_off +
		   offsetof(struct perf_dom_entry, perf_lvl_arr) +
		   idx * sizeof(struct perf_lvl_entry) +
		   offsetof(struct perf_lvl_entry, perf_lvl);
	return readl_relaxed(dom_data);
}

static ssize_t stats_get(struct file *file, char __user *user_buf, size_t count,
			 loff_t *ppos)
{
	u16 clkdom, num_lvl, i;
	u32 match_old = 0, match_new = 0;
	ssize_t r, bytes = 0;
	u64 *vals;
	void __iomem *dom_data;
	struct clkdom_entry *entry = (struct clkdom_entry *)file->private_data;
	struct dentry *dentry = file->f_path.dentry;
	ssize_t off = 0, perf_lvl_off = 0;
	char *str;

	r = debugfs_file_get(dentry);
	if (unlikely(r))
		return r;
	if (!entry)
		return -ENOENT;
	clkdom = entry->clkdom;
	dom_data = pinfo->stats_iomem +
		   4 * readl_relaxed(pinfo->stats_iomem +
				     offsetof(struct scmi_stats,
					      perf_dom_entry_off_arr) +
				     4 * clkdom);
	num_lvl = get_num_opps_for_clkdom(clkdom);
	if (!num_lvl)
		return 0;

	vals = kcalloc(num_lvl, sizeof(u64), GFP_KERNEL);
	if (!vals)
		return -ENOMEM;
	str = kzalloc(4096, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	/* which offset within each perf_lvl entry */
	if (entry->entry == usage)
		off = offsetof(struct perf_lvl_entry, usage);
	else if (entry->entry == residency)
		off = offsetof(struct perf_lvl_entry, residency);

	/* read the iomem data for clkdom */
	do {
		match_old = readl_relaxed(
			pinfo->stats_iomem +
			offsetof(struct scmi_stats, match_sequence));
		if (match_old % 2)
			continue;
		for (i = 0; i < num_lvl; i++) {
			perf_lvl_off =
				i * sizeof(struct perf_lvl_entry) +
				offsetof(struct perf_dom_entry, perf_lvl_arr);
			vals[i] = readl_relaxed(dom_data + perf_lvl_off + off) |
				  (u64)readl_relaxed(dom_data + perf_lvl_off +
						     off + 4)
					  << 32;
		}
		match_new = readl_relaxed(
			pinfo->stats_iomem +
			offsetof(struct scmi_stats, match_sequence));
	} while (match_old != match_new);

	for (i = 0; i < num_lvl; i++) {
		bytes += scnprintf(str + bytes, 4096 - bytes, "%u %llu\n",
				 pinfo->freq_info[pinfo->freq_info[clkdom] + i],
				 vals[i]);
	}

	r += simple_read_from_buffer(user_buf, count, ppos, str, bytes);
	debugfs_file_put(dentry);
	kfree(vals);
	kfree(str);
	return r;
}

static const struct file_operations stats_ops = {
	.read = stats_get,
	.open = simple_open,
	.llseek = default_llseek,
};

static int scmi_cpufreq_stats_create_fs_entries(struct device *dev)
{
	int i;
	struct dentry *ret;
	struct dentry *clkdom_dir = NULL;
	char clkdom_name[MAX_CLK_DOMAIN];

	dir = debugfs_create_dir(SCMI_CPUFREQ_STATS_DIR_STRING, 0);
	if (IS_ERR(dir)) {
		dev_err(dev, "Debugfs directory creation failed\n");
		return -ENOENT;
	}

	for (i = 0; i < pinfo->num_clkdom; i++) {
		snprintf(clkdom_name, MAX_CLK_DOMAIN, "clkdom%d", i);

		clkdom_dir = debugfs_create_dir(clkdom_name, dir);
		if (IS_ERR(clkdom_dir)) {
			dev_err(dev,
				"Debugfs directory creation for %s failed\n",
				clkdom_name);
			return -ENOENT;
		}

		ret = debugfs_create_file(
			CPUFREQ_STATS_USAGE_FILENAME, 0400, clkdom_dir,
			pinfo->entries + i * ENTRY_MAX + usage, &stats_ops);
		ret = debugfs_create_file(
			CPUFREQ_STATS_RESIDENCY_FILENAME, 0400, clkdom_dir,
			pinfo->entries + i * ENTRY_MAX + residency, &stats_ops);
	}
	return 0;
}

static int qcom_cpufreq_stats_init(struct scmi_handle *handle, struct device *dev)
{
	u32 stats_signature;
	u16 num_clkdom = 0, revision, num_lvl = 0;
	int i, j, ret;
	struct cpufreq_stats_prot_attr prot_attr;
	struct clkdom_entry *entry;

	ret = ops->get_param(ph, &prot_attr, SCMI_CPUFREQ_STATS_STR,
			PARAM_PROTOCOL_ATTRIBUTE, 0, sizeof(prot_attr));
	if (ret) {
		dev_err(handle->dev,
		"SCMI CPUFREQ Stats CPUFREQSTATS_GET_MEM_INFO error: %d\n", ret);
		return ret;
	}
	if (prot_attr.statistics_len) {
		pinfo = devm_kzalloc(dev, sizeof(struct stats_info), GFP_KERNEL);
		if (!pinfo)
			return -ENOMEM;
		pinfo->stats_iomem = devm_ioremap(
			handle->dev,
			prot_attr.statistics_address_low |
				(u64)prot_attr.statistics_address_high << 32,
			prot_attr.statistics_len);
		if (IS_ERR(pinfo->stats_iomem))
			return -ENOMEM;
		stats_signature = readl_relaxed(
			pinfo->stats_iomem +
			offsetof(struct scmi_stats, signature));
		revision = readl_relaxed(pinfo->stats_iomem +
					 offsetof(struct scmi_stats,
						  revision)) & 0xFF;
		num_clkdom = readl_relaxed(pinfo->stats_iomem +
					   offsetof(struct scmi_stats,
						    num_domains)) & 0xFF;
		if (stats_signature != 0x50455246) {
			dev_err(handle->dev,
				"SCMI stats mem signature check failed\n");
			return -EPERM;
		}
		if (revision != 1) {
			dev_err(handle->dev,
				"SCMI stats revision not supported\n");
			return -EPERM;
		}
		if (!num_clkdom) {
			dev_err(handle->dev,
				"SCMI cpufreq stats number of clock domains are zero\n");
			return -EPERM;
		}
		pinfo->num_clkdom = num_clkdom;
	} else {
		dev_err(handle->dev,
			"SCMI cpufreq stats length is zero\n");
		return -EPERM;
	}
	/* allocate structures for each clkdom/entry pair */
	pinfo->entries = devm_kcalloc(dev, num_clkdom * ENTRY_MAX,
				 sizeof(struct clkdom_entry), GFP_KERNEL);
	if (!pinfo->entries)
		return -ENOMEM;
	/* initialize structures for each clkdom/entry pair */
	for (i = 0; i < num_clkdom; i++) {
		entry = pinfo->entries + (i * ENTRY_MAX);
		for (j = 0; j < ENTRY_MAX; j++) {
			entry[j].entry = j;
			entry[j].clkdom = i;
		}
	}
	if (scmi_cpufreq_stats_create_fs_entries(handle->dev)) {
		dev_err(handle->dev, "Failed to create debugfs entries\n");
		return -ENOENT;
	}
	/*find the number of frequencies in platform and allocate memory for storing them */
	for (i = 0; i < num_clkdom; i++)
		num_lvl += get_num_opps_for_clkdom(i);
	pinfo->freq_info =
		devm_kcalloc(dev, num_lvl + num_clkdom, sizeof(u32), GFP_KERNEL);
	if (!pinfo->freq_info) {
		dev_err(handle->dev, "Failed to allocate memory for freq entries\n");
		return -ENOMEM;
	}
	/* Cache the cpufreq values */
	for (i = 0; i < num_clkdom; i++) {
		/* find the no. of freq lvls of all preceding clkdoms */
		pinfo->freq_info[i] = num_clkdom;
		for (j = 0; j < i; j++)
			pinfo->freq_info[i] += get_num_opps_for_clkdom(j);

		num_lvl = get_num_opps_for_clkdom(i);
		if (!num_lvl)
			continue;
		for (j = 0; j < num_lvl; j++) {
			pinfo->freq_info[pinfo->freq_info[i] + j] =
				get_freq_at_idx_for_clkdom(i, j);
		}
	}
	return 0;
}

static int scmi_cpufreq_stats_probe(struct platform_device *pdev)
{
	int ret;

	sdev = get_qcom_scmi_device();
	if (IS_ERR(sdev)) {
		ret = PTR_ERR(sdev);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Error getting scmi_dev ret = %d\n", ret);
		return ret;
	}
	ops = sdev->handle->devm_protocol_get(sdev, QCOM_SCMI_VENDOR_PROTOCOL, &ph);
	if (IS_ERR(ops))
		return PTR_ERR(ops);
	return qcom_cpufreq_stats_init(sdev->handle, &pdev->dev);
}

static const struct of_device_id cpufreq_stats_v2[] = {
	{.compatible = "qcom,cpufreq-stats-v2"},
	{},
};

static struct platform_driver cpufreq_stats_v2_driver = {
	.driver = {
		.name = "cpufreq-stats-v2",
		.of_match_table = cpufreq_stats_v2,
		.suppress_bind_attrs = true,
	},
	.probe = scmi_cpufreq_stats_probe,
};

module_platform_driver(cpufreq_stats_v2_driver);
MODULE_DESCRIPTION("Qcom SCMI CPUFREQ STATS V2 driver");
MODULE_LICENSE("GPL");
