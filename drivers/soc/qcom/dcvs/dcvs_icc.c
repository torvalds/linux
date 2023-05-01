// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "qcom-dcvs-icc: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/interconnect.h>
#include <dt-bindings/interconnect/qcom,icc.h>
#include <soc/qcom/dcvs.h>
#include "dcvs_private.h"

struct icc_sp_data {
	struct icc_path         *icc_path;
};

static int commit_icc_freq(struct dcvs_path *path, struct dcvs_freq *freqs,
							u32 update_mask)
{
	int ret;
	struct dcvs_hw *hw = path->hw;
	struct icc_sp_data *sp_data = path->data;
	struct icc_path *icc_path = sp_data->icc_path;
	u32 icc_ib = freqs->ib * hw->width;
	u32 icc_ab = freqs->ab * hw->width;

	ret = icc_set_bw(icc_path, icc_ab, icc_ib);
	if (ret < 0) {
		dev_err(path->dev, "icc set bw request failed: %d\n", ret);
		return ret;
	}
	path->cur_freq.ib = freqs->ib;
	path->cur_freq.ab = freqs->ab;

	return ret;
}

#define ACTIVE_ONLY_TAG	QCOM_ICC_TAG_ACTIVE_ONLY
#define PERF_MODE_TAG	QCOM_ICC_TAG_PERF_MODE
int setup_icc_sp_device(struct device *dev, struct dcvs_hw *hw,
					struct dcvs_path *path)
{
	struct icc_sp_data *sp_data = NULL;
	int ret = 0;

	if (hw->type != DCVS_DDR && hw->type != DCVS_LLCC
			&& hw->type != DCVS_DDRQOS && hw->type != DCVS_UBWCP)
		return -EINVAL;

	sp_data = devm_kzalloc(dev, sizeof(*sp_data), GFP_KERNEL);
	if (!sp_data)
		return -ENOMEM;
	sp_data->icc_path = of_icc_get(dev, NULL);
	if (IS_ERR(sp_data->icc_path)) {
		ret = PTR_ERR(sp_data->icc_path);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Unable to register icc path: %d\n", ret);
		return ret;
	}
	if (hw->type == DCVS_DDR || hw->type == DCVS_LLCC
				|| hw->type == DCVS_UBWCP)
		icc_set_tag(sp_data->icc_path, ACTIVE_ONLY_TAG);
	else if (hw->type == DCVS_DDRQOS)
		icc_set_tag(sp_data->icc_path, ACTIVE_ONLY_TAG | PERF_MODE_TAG);
	path->data = sp_data;
	path->commit_dcvs_freqs = commit_icc_freq;

	return ret;
}
