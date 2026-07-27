// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/smem.h>
#include <linux/units.h>

#include "smem.h"

#define SMEM_DDR_INFO_ID		603

#define MAX_DDR_FREQ_NUM_V3		13
#define MAX_DDR_FREQ_NUM_V5		14

#define MAX_CHAN_NUM			8
#define MAX_RANK_NUM			2

#define DDR_HBB_MIN			13
#define DDR_HBB_MAX			19

#define MAX_SHUB_ENTRIES		8

static struct smem_dram *__dram;

enum ddr_info_version {
	INFO_UNKNOWN,
	INFO_V3,
	INFO_V3_WITH_14_FREQS,
	INFO_V4,
	INFO_V5,
	INFO_V5_WITH_6_REGIONS,
	INFO_V6, /* INFO_V6 seems to only have shipped with 6 DDR regions, unlike V7 */
	INFO_V7,
	INFO_V7_WITH_6_REGIONS,
};

struct smem_dram {
	unsigned long frequencies[MAX_DDR_FREQ_NUM_V5];
	u32 num_frequencies;
	u8 hbb;
};

enum ddr_type {
	DDR_TYPE_NODDR = 0,
	DDR_TYPE_LPDDR1 = 1,
	DDR_TYPE_LPDDR2 = 2,
	DDR_TYPE_PCDDR2 = 3,
	DDR_TYPE_PCDDR3 = 4,
	DDR_TYPE_LPDDR3 = 5,
	DDR_TYPE_LPDDR4 = 6,
	DDR_TYPE_LPDDR4X = 7,
	DDR_TYPE_LPDDR5 = 8,
	DDR_TYPE_LPDDR5X = 9,
};

/* The data structures below are NOT __packed on purpose! */

/* Structs used across multiple versions */
struct ddr_part_details {
	__le16 revision_id1;
	__le16 revision_id2;
	__le16 width;
	__le16 density;
};

struct ddr_freq_table {
	__le32 freq_khz;
	u8 enabled;
};

/* V3 */
struct ddr_freq_plan_v3 {
	struct ddr_freq_table ddr_freq[MAX_DDR_FREQ_NUM_V3];
	u8 num_ddr_freqs;
	phys_addr_t clk_period_address;
};

struct ddr_details_v3 {
	u8 manufacturer_id;
	u8 device_type;
	struct ddr_part_details ddr_params[MAX_CHAN_NUM];
	struct ddr_freq_plan_v3 ddr_freq_tbl;
	u8 num_channels;
};

/* Some V3 structs have an additional frequency level */
struct ddr_freq_plan_v3_14freqs {
	struct ddr_freq_table ddr_freq[MAX_DDR_FREQ_NUM_V3 + 1];
	u8 num_ddr_freqs;
	phys_addr_t clk_period_address;
};

struct ddr_details_v3_14freqs {
	u8 manufacturer_id;
	u8 device_type;
	struct ddr_part_details ddr_params[MAX_CHAN_NUM];
	struct ddr_freq_plan_v3_14freqs ddr_freq_tbl;
	u8 num_channels;
};

/* V4 */
struct ddr_details_v4 {
	u8 manufacturer_id;
	u8 device_type;
	struct ddr_part_details ddr_params[MAX_CHAN_NUM];
	struct ddr_freq_plan_v3 ddr_freq_tbl;
	u8 num_channels;
	u8 num_ranks[MAX_CHAN_NUM];
	u8 highest_bank_addr_bit[MAX_CHAN_NUM][MAX_RANK_NUM];
};

/* V5 */
struct shub_freq_table {
	u8 enable;
	__le32 freq_khz;
};

struct shub_freq_plan_entry {
	u8 num_shub_freqs;
	struct shub_freq_table shub_freq[MAX_SHUB_ENTRIES];
};

struct ddr_xbl2quantum_smem_data {
	phys_addr_t ssr_cookie_addr;
	__le32 reserved[10];
};

struct ddr_freq_plan_v5 {
	struct ddr_freq_table ddr_freq[MAX_DDR_FREQ_NUM_V5];
	u8 num_ddr_freqs;
	phys_addr_t clk_period_address;
	__le32 max_nom_ddr_freq;
};

struct ddr_region_v5 {
	__le64 start_address;
	__le64 size;
	__le64 mem_controller_address;
	__le32 granule_size; /* MiB */
	u8  ddr_rank;
#define DDR_RANK_0	BIT(0)
#define DDR_RANK_1	BIT(1)
	u8  segments_start_index;
	__le64 segments_start_offset;
};

struct ddr_regions_v5 {
	__le32 ddr_region_num; /* We expect this to always be 4 or 6 */
	__le64 ddr_rank0_size;
	__le64 ddr_rank1_size;
	__le64 ddr_cs0_start_addr;
	__le64 ddr_cs1_start_addr;
	__le32 highest_bank_addr_bit;
	struct ddr_region_v5 ddr_region[] __counted_by_le(ddr_region_num);
};

struct ddr_details_v5 {
	u8 manufacturer_id;
	u8 device_type;
	struct ddr_part_details ddr_params[MAX_CHAN_NUM];
	struct ddr_freq_plan_v5 ddr_freq_tbl;
	u8 num_channels;
	u8 _padding;
	struct ddr_regions_v5 ddr_regions;
};

/* V6 */
struct ddr_misc_info_v6 {
	__le32 dsf_version;
	__le32 reserved[10];
};

/* V7 */
struct ddr_details_v7 {
	u8 manufacturer_id;
	u8 device_type;
	struct ddr_part_details ddr_params[MAX_CHAN_NUM];
	struct ddr_freq_plan_v5 ddr_freq_tbl;
	u8 num_channels;
	u8 sct_config;
	struct ddr_regions_v5 ddr_regions;
};

/**
 * qcom_smem_dram_get_hbb(): Get the Highest bank address bit
 *
 * Context: Check qcom_smem_is_available() before calling this function.
 * Because __dram * is initialized by smem_dram_parse(), which is in turn
 * called from * qcom_smem_probe(), __dram will only be NULL if the data
 * couldn't have been found/interpreted correctly.
 *
 * Return: highest bank bit on success, -ENODATA on failure.
 */
int qcom_smem_dram_get_hbb(void)
{
	if (!__dram || !__dram->hbb)
		return -ENODATA;

	if (__dram->hbb < DDR_HBB_MIN || __dram->hbb > DDR_HBB_MAX)
		return -ENODATA;

	return __dram->hbb;
}
EXPORT_SYMBOL_GPL(qcom_smem_dram_get_hbb);

static void smem_dram_parse_v3_data(struct smem_dram *dram, void *data)
{
	struct ddr_details_v3 *details = data;

	for (int i = 0; i < MAX_DDR_FREQ_NUM_V3; i++) {
		struct ddr_freq_table *freq_entry = &details->ddr_freq_tbl.ddr_freq[i];

		if (freq_entry->freq_khz && freq_entry->enabled) {
			u32 freq_khz = le32_to_cpu(freq_entry->freq_khz);
			dram->frequencies[dram->num_frequencies++] = 1000 * freq_khz;
		}
	}
}

static void smem_dram_parse_v3_14freqs_data(struct smem_dram *dram, void *data)
{
	struct ddr_details_v3_14freqs *details = data;

	for (int i = 0; i < MAX_DDR_FREQ_NUM_V3 + 1; i++) {
		struct ddr_freq_table *freq_entry = &details->ddr_freq_tbl.ddr_freq[i];

		if (freq_entry->freq_khz && freq_entry->enabled)
			dram->frequencies[dram->num_frequencies++] = 1000 * freq_entry->freq_khz;
	}
}

static void smem_dram_parse_v4_data(struct smem_dram *dram, void *data)
{
	struct ddr_details_v4 *details = data;

	/* Rank 0 channel 0 entry holds the correct value */
	dram->hbb = details->highest_bank_addr_bit[0][0];

	for (int i = 0; i < MAX_DDR_FREQ_NUM_V3; i++) {
		struct ddr_freq_table *freq_entry = &details->ddr_freq_tbl.ddr_freq[i];

		if (freq_entry->freq_khz && freq_entry->enabled) {
			u32 freq_khz = le32_to_cpu(freq_entry->freq_khz);
			dram->frequencies[dram->num_frequencies++] = 1000 * freq_khz;
		}
	}
}

static void smem_dram_parse_v5_data(struct smem_dram *dram, void *data)
{
	struct ddr_details_v5 *details = data;
	struct ddr_regions_v5 *region = &details->ddr_regions;

	dram->hbb = le32_to_cpu(region[0].highest_bank_addr_bit);

	for (int i = 0; i < MAX_DDR_FREQ_NUM_V5; i++) {
		struct ddr_freq_table *freq_entry = &details->ddr_freq_tbl.ddr_freq[i];

		if (freq_entry->freq_khz && freq_entry->enabled) {
			u32 freq_khz = le32_to_cpu(freq_entry->freq_khz);
			dram->frequencies[dram->num_frequencies++] = 1000 * freq_khz;
		}
	}
}

static void smem_dram_parse_v7_data(struct smem_dram *dram, void *data)
{
	struct ddr_details_v7 *details = data;
	struct ddr_regions_v5 *region = &details->ddr_regions;

	dram->hbb = le32_to_cpu(region[0].highest_bank_addr_bit);

	for (int i = 0; i < MAX_DDR_FREQ_NUM_V5; i++) {
		struct ddr_freq_table *freq_entry = &details->ddr_freq_tbl.ddr_freq[i];

		if (freq_entry->freq_khz && freq_entry->enabled) {
			u32 freq_khz = le32_to_cpu(freq_entry->freq_khz);
			dram->frequencies[dram->num_frequencies++] = 1000 * freq_khz;
		}
	}
}

/* The structure contains no version field, so we have to perform some guesswork.. */
static int smem_dram_infer_struct_version(size_t size)
{
	/* Some early versions provided less bytes of less useful data */
	if (size < sizeof(struct ddr_details_v3))
		return -EINVAL;

	if (size == sizeof(struct ddr_details_v3))
		return INFO_V3;

	if (size == sizeof(struct ddr_details_v3_14freqs))
		return INFO_V3_WITH_14_FREQS;

	if (size == sizeof(struct ddr_details_v4))
		return INFO_V4;

	if (size == sizeof(struct ddr_details_v5) +
		    4 * sizeof(struct ddr_region_v5))
		return INFO_V5;

	if (size == sizeof(struct ddr_details_v5) +
		    4 * sizeof(struct ddr_region_v5) +
		    sizeof(struct ddr_xbl2quantum_smem_data) +
		    sizeof(struct shub_freq_plan_entry))
		return INFO_V5;

	if (size == sizeof(struct ddr_details_v5) +
		    6 * sizeof(struct ddr_region_v5))
		return INFO_V5_WITH_6_REGIONS;

	if (size == sizeof(struct ddr_details_v5) +
		    6 * sizeof(struct ddr_region_v5) +
		    sizeof(struct ddr_xbl2quantum_smem_data) +
		    sizeof(struct shub_freq_plan_entry))
		return INFO_V5_WITH_6_REGIONS;

	if (size == sizeof(struct ddr_details_v5) +
		    6 * sizeof(struct ddr_region_v5) +
		    sizeof(struct ddr_misc_info_v6) +
		    sizeof(struct shub_freq_plan_entry))
		return INFO_V6;

	if (size == sizeof(struct ddr_details_v7) +
		    4 * sizeof(struct ddr_region_v5) +
		    sizeof(struct ddr_misc_info_v6) +
		    sizeof(struct shub_freq_plan_entry))
		return INFO_V7;

	if (size == sizeof(struct ddr_details_v7) +
		    6 * sizeof(struct ddr_region_v5) +
		    sizeof(struct ddr_misc_info_v6) +
		    sizeof(struct shub_freq_plan_entry))
		return INFO_V7_WITH_6_REGIONS;

	return INFO_UNKNOWN;
}

static int smem_dram_frequencies_show(struct seq_file *s, void *unused)
{
	struct smem_dram *dram = s->private;

	for (int i = 0; i < dram->num_frequencies; i++)
		seq_printf(s, "%lu\n", dram->frequencies[i]);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(smem_dram_frequencies);

static int smem_hbb_show(struct seq_file *s, void *unused)
{
	struct smem_dram *dram = s->private;

	if (!dram->hbb)
		return -EINVAL;

	seq_printf(s, "%d\n", dram->hbb);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(smem_hbb);

struct dentry *smem_dram_parse(struct qcom_smem *smem, struct device *dev)
{
	struct dentry *debugfs_dir;
	enum ddr_info_version ver;
	struct smem_dram *dram;
	size_t actual_size;
	void *data;

	/* No need to check qcom_smem_is_available(), this func is called by the SMEM driver */
	data = __qcom_smem_get(smem, QCOM_SMEM_HOST_ANY, SMEM_DDR_INFO_ID, &actual_size);
	if (IS_ERR_OR_NULL(data))
		return ERR_PTR(-ENODATA);

	ver = smem_dram_infer_struct_version(actual_size);
	if (ver < 0) {
		/* Some SoCs don't provide data that's useful for us */
		return ERR_PTR(-ENODATA);
	} else if (ver == INFO_UNKNOWN) {
		/* In other cases, we may not have added support for a newer struct revision */
		dev_err(dev, "Found an unknown type of DRAM info struct (size = %zu)\n",
			actual_size);
		return ERR_PTR(-EINVAL);
	}

	dram = devm_kzalloc(dev, sizeof(*dram), GFP_KERNEL);
	if (!dram)
		return ERR_PTR(-ENOMEM);

	switch (ver) {
	case INFO_V3:
		smem_dram_parse_v3_data(dram, data);
		break;
	case INFO_V3_WITH_14_FREQS:
		smem_dram_parse_v3_14freqs_data(dram, data);
		break;
	case INFO_V4:
		smem_dram_parse_v4_data(dram, data);
		break;
	case INFO_V5:
	case INFO_V5_WITH_6_REGIONS:
	case INFO_V6:
		smem_dram_parse_v5_data(dram, data);
		break;
	case INFO_V7:
	case INFO_V7_WITH_6_REGIONS:
		smem_dram_parse_v7_data(dram, data);
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	debugfs_dir = debugfs_create_dir("qcom_smem", NULL);
	debugfs_create_file("dram_frequencies", 0444, debugfs_dir, dram,
			    &smem_dram_frequencies_fops);
	debugfs_create_file("hbb", 0444, debugfs_dir, dram, &smem_hbb_fops);

	__dram = dram;

	return debugfs_dir;
}
