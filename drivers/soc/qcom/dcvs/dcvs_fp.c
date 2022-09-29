// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "qcom-dcvs-fp: " fmt

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
#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>
#include <soc/qcom/tcs.h>
#include <soc/qcom/dcvs.h>
#include "dcvs_private.h"

struct bcm_db {
	__le32	unit;
	__le16	width;
	u8	vcd;
	u8	reserved;
};

struct bcm_data {
	u32			addr;
	u32			unit;
	u32			width;
	u32			vcd;
};

enum ddrllcc_fp_idx {
	DDR_IDX,
	LLCC_IDX,
	NUM_FP_CMDS
};

struct ddrllcc_fp_data {
	struct device		*dev;
	struct dcvs_path	*paths[NUM_FP_CMDS];
	struct bcm_data		bcms[NUM_FP_CMDS];
	struct tcs_cmd		tcs_cmds[NUM_FP_CMDS];
};

struct ddrllcc_fp_data		*ddrllcc_data;
static DEFINE_MUTEX(ddrllcc_lock);

static int ddrllcc_fp_commit(struct dcvs_path *path, struct dcvs_freq *freqs,
							u32 update_mask)
{
	struct ddrllcc_fp_data *fp_data = path->data;
	struct tcs_cmd *tcs_cmds = fp_data->tcs_cmds;
	struct bcm_data *bcms = fp_data->bcms;
	struct dcvs_path **paths = fp_data->paths;
	struct device *dev = fp_data->dev;
	u32 bcm_vote;
	int i, ret = 0;

	for (i = 0; i < NUM_FP_CMDS; i++) {
		if (!(update_mask & BIT(i)))
			continue;
		bcm_vote = freqs[i].ib * bcms[i].width / bcms[i].unit;
		tcs_cmds[i].data = BCM_TCS_CMD(1, 1, 0, bcm_vote);
	}

	ret = rpmh_update_fast_path(dev, tcs_cmds, NUM_FP_CMDS, update_mask);
	if (ret < 0) {
		dev_err(dev, "Error updating RPMH fast path: %d\n", ret);
		return ret;
	}

	for (i = 0; i < NUM_FP_CMDS; i++) {
		if (!(update_mask & BIT(i)))
			continue;
		paths[i]->cur_freq.ib = freqs[i].ib;
	}

	return ret;
}

static int populate_bcm_data(struct device *dev, struct bcm_data *bcm,
					const char *of_prop)
{
	const char *bcm_name;
	const struct bcm_db *data;
	size_t data_len = 0;
	int ret;

	ret = of_property_read_string(dev->of_node, of_prop, &bcm_name);
	if (ret < 0) {
		dev_err(dev, "Error reading %s: %d\n", of_prop, ret);
		return ret;
	}
	bcm->addr = cmd_db_read_addr(bcm_name);
	if (!bcm->addr) {
		dev_err(dev, "Error getting addr for: %s\n", bcm_name);
		return -EINVAL;
	}

	data = cmd_db_read_aux_data(bcm_name, &data_len);
	if (IS_ERR(data)) {
		ret = PTR_ERR(data);
		dev_err(dev, "Error reading %s aux data: %d\n", bcm_name, ret);
		return ret;
	}
	if (data_len != sizeof(*data)) {
		dev_err(dev, "Bad data len for %s: %d\n", bcm_name, data_len);
		return -EINVAL;
	}
	bcm->unit = le32_to_cpu(data->unit) / 1000UL;
	bcm->width = le16_to_cpu(data->width);
	bcm->vcd = data->vcd;
	dev_dbg(dev, "Got BCM %s: addr=%lu, unit=%lu, width=%lu, vcd=%lu\n",
			bcm_name, bcm->addr, bcm->unit, bcm->width, bcm->vcd);

	return 0;
}

int setup_ddrllcc_fp_device(struct device *dev, struct dcvs_hw *hw,
					struct dcvs_path *path)
{
	int ret = 0;

	if (hw->type != DCVS_DDR && hw->type != DCVS_LLCC)
		return -EINVAL;

	mutex_lock(&ddrllcc_lock);
	if (!ddrllcc_data) {
		dev_dbg(dev, "Probe deferred since FP not init yet\n");
		mutex_unlock(&ddrllcc_lock);
		return -EPROBE_DEFER;
	}
	path->data = ddrllcc_data;
	path->commit_dcvs_freqs = ddrllcc_fp_commit;
	if (hw->type == DCVS_DDR)
		ddrllcc_data->paths[DDR_IDX] = path;
	else
		ddrllcc_data->paths[LLCC_IDX] = path;
	mutex_unlock(&ddrllcc_lock);

	return ret;
}
EXPORT_SYMBOL(setup_ddrllcc_fp_device);

#define DDR_BCM_PROP	"qcom,ddr-bcm-name"
#define LLCC_BCM_PROP	"qcom,llcc-bcm-name"
static int qcom_dcvs_fp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int i, ret = 0;
	struct ddrllcc_fp_data *fp_data;

	mutex_lock(&ddrllcc_lock);
	if (ddrllcc_data) {
		dev_err(dev, "Only one fast path client allowed\n");
		ret = -EINVAL;
		goto out;
	}
	fp_data = devm_kzalloc(dev, sizeof(*fp_data), GFP_KERNEL);
	if (!fp_data) {
		ret = -ENOMEM;
		goto out;
	}

	fp_data->dev = dev;
	ret = populate_bcm_data(dev, &fp_data->bcms[DDR_IDX], DDR_BCM_PROP);
	if (ret < 0) {
		dev_err(dev, "Error importing %s bcm data: %d\n",
						DDR_BCM_PROP, ret);
		goto out;
	}
	ret = populate_bcm_data(dev, &fp_data->bcms[LLCC_IDX], LLCC_BCM_PROP);
	if (ret < 0) {
		dev_err(dev, "Error importing %s bcm data: %d\n",
						LLCC_BCM_PROP, ret);
		goto out;
	}

	for (i = 0; i < NUM_FP_CMDS; i++) {
		fp_data->tcs_cmds[i].addr = fp_data->bcms[i].addr;
		fp_data->tcs_cmds[i].data = BCM_TCS_CMD(1, 1, 0, 0);
		fp_data->tcs_cmds[i].wait = 0;
	}

	ret = rpmh_init_fast_path(dev, fp_data->tcs_cmds, NUM_FP_CMDS);
	if (ret < 0) {
		dev_err(dev, "Error initializing rpmh fast path: %d\n", ret);
		goto out;
	}
	ret = rpmh_write_async(dev, RPMH_SLEEP_STATE, fp_data->tcs_cmds,
								NUM_FP_CMDS);
	if (ret < 0) {
		dev_err(dev, "Error initing dcvs_fp sleep vote: %d\n", ret);
		goto out;
	}
	for (i = 0; i < NUM_FP_CMDS; i++)
		fp_data->tcs_cmds[i].data = BCM_TCS_CMD(1, 1, 0, 1);
	ret = rpmh_write_async(dev, RPMH_WAKE_ONLY_STATE, fp_data->tcs_cmds,
								NUM_FP_CMDS);
	if (ret < 0) {
		dev_err(dev, "Error initing dcvs_fp wake vote: %d\n", ret);
		goto out;
	}
	ddrllcc_data = fp_data;
out:
	mutex_unlock(&ddrllcc_lock);

	return ret;
}

static const struct of_device_id qcom_dcvs_fp_match_table[] = {
	{ .compatible = "qcom,dcvs-fp" },
	{}
};

static struct platform_driver qcom_dcvs_fp_driver = {
	.probe = qcom_dcvs_fp_probe,
	.driver = {
		.name = "qcom-dcvs-fp",
		.of_match_table = qcom_dcvs_fp_match_table,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(qcom_dcvs_fp_driver);

MODULE_DESCRIPTION("QCOM DCVS FP Driver");
MODULE_LICENSE("GPL");
