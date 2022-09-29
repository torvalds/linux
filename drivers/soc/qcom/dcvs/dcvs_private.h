/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_DCVS_PRIVATE_H
#define _QCOM_DCVS_PRIVATE_H

#include <linux/kernel.h>
#include <soc/qcom/dcvs.h>

/*
 * NOTE: cur_freq is maintained by SLOW and FAST paths only
 * and percpu_cur_freqs is maintained by PERCPU paths only
 */
struct dcvs_path {
	struct device		*dev;
	enum dcvs_path_type	type;
	struct dcvs_hw		*hw;
	struct dcvs_freq	cur_freq;
	u32			*percpu_cur_freqs;
	struct list_head	voter_list;
	u32			num_voters;
	struct mutex		voter_lock;
	void			*data; /* node-specific private data */
	int (*commit_dcvs_freqs)(struct dcvs_path *path,
						struct dcvs_freq *freqs,
						u32 update_mask);
};

struct dcvs_hw {
	struct device		*dev;
	struct kobject		kobj;
	enum dcvs_hw_type	type;
	u32			*freq_table;
	u32			table_len;
	u32			width;
	u32			hw_min_freq;
	u32			hw_max_freq;
	u32			boost_freq;
	struct dcvs_path	*dcvs_paths[NUM_DCVS_PATHS];
	u32			num_paths;
	u32			num_inited_paths;
};


int populate_l3_table(struct device *dev, u32 **freq_table);
int setup_epss_l3_sp_device(struct device *dev, struct dcvs_hw *hw,
					struct dcvs_path *path);
int setup_epss_l3_percpu_device(struct device *dev, struct dcvs_hw *hw,
					struct dcvs_path *path);
#if IS_ENABLED(CONFIG_QCOM_DCVS_FP)
int setup_ddrllcc_fp_device(struct device *dev, struct dcvs_hw *hw,
					struct dcvs_path *path);
#else
static inline int setup_ddrllcc_fp_device(struct device *dev,
				struct dcvs_hw *hw, struct dcvs_path *path)
{
	return -ENODEV;
}
#endif
int setup_icc_sp_device(struct device *dev, struct dcvs_hw *hw,
					struct dcvs_path *path);
#endif /* _QCOM_DCVS_PRIVATE_H */
