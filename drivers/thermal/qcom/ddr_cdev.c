// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/module.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/interconnect.h>

#define DDR_CDEV_NAME "ddr-cdev"

struct ddr_cdev {
	uint32_t cur_state;
	uint32_t max_state;
	struct thermal_cooling_device *cdev;
	struct icc_path *icc_path;
	struct device *dev;
	uint32_t *freq_table;
	uint32_t freq_table_size;
};

/**
 * ddr_set_cur_state - callback function to set the ddr state.
 * @cdev: thermal cooling device pointer.
 * @state: set this variable to the current cooling state.
 *
 * Callback for the thermal cooling device to change the
 * DDR state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int ddr_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct ddr_cdev *ddr_cdev = cdev->devdata;
	int ret = 0;

	/* Request state should be less than max_level */
	if (state > ddr_cdev->max_state)
		return -EINVAL;

	/* Check if the old cooling action is same as new cooling action */
	if (ddr_cdev->cur_state == state)
		return 0;

	ret = icc_set_bw(ddr_cdev->icc_path, 0, ddr_cdev->freq_table[state]);
	if (ret < 0) {
		dev_err(ddr_cdev->dev, "Error placing DDR freq%u. err:%d\n",
			ddr_cdev->freq_table[state], ret);
		return ret;
	}

	ddr_cdev->cur_state = state;

	return ret;
}

/**
 * ddr_get_cur_state - callback function to get the current cooling
 *				state.
 * @cdev: thermal cooling device pointer.
 * @state: fill this variable with the current cooling state.
 *
 * Callback for the thermal cooling device to return the
 * current DDR state request.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int ddr_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct ddr_cdev *ddr_cdev = cdev->devdata;
	*state = ddr_cdev->cur_state;

	return 0;
}

/**
 * ddr_get_max_state - callback function to get the max cooling state.
 * @cdev: thermal cooling device pointer.
 * @state: fill this variable with the max cooling state.
 *
 * Callback for the thermal cooling device to return the DDR
 * max cooling state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int ddr_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct ddr_cdev *ddr_cdev = cdev->devdata;
	*state = ddr_cdev->max_state;

	return 0;
}

static struct thermal_cooling_device_ops ddr_cdev_ops = {
	.get_max_state = ddr_get_max_state,
	.get_cur_state = ddr_get_cur_state,
	.set_cur_state = ddr_set_cur_state,
};

static int ddr_cdev_probe(struct platform_device *pdev)
{
	int ret = 0, opp_ct = 0, bus_width = 1, idx = 0;
	struct ddr_cdev *ddr_cdev = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	uint32_t *freq_table = NULL;
	char cdev_name[THERMAL_NAME_LENGTH] = DDR_CDEV_NAME;

	ddr_cdev = devm_kzalloc(dev, sizeof(*ddr_cdev), GFP_KERNEL);
	if (!ddr_cdev)
		return -ENOMEM;
	ddr_cdev->icc_path = of_icc_get(dev, NULL);
	if (IS_ERR(ddr_cdev->icc_path)) {
		ret = PTR_ERR(ddr_cdev->icc_path);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Unable to register icc path: %d\n",
					ret);
		return ret;
	}

	if (!of_find_property(np, "qcom,freq-table", &opp_ct)) {
		dev_err(dev, "No DDR frequency entries\n");
		ret = -ENODEV;
		goto err_exit;
	}

	if (opp_ct <= 0) {
		dev_err(dev, "No DDR frequency\n");
		ret = -ENODEV;
		goto err_exit;
	}
	opp_ct = opp_ct / sizeof(*freq_table);

	/* Add one more entry for 0 or no DDR BW vote */
	opp_ct++;

	freq_table = devm_kcalloc(dev, opp_ct, sizeof(*freq_table), GFP_KERNEL);
	if (!freq_table) {
		ret = -ENOMEM;
		goto err_exit;
	}
	freq_table[0] = 0;

	ret = of_property_read_u32_array(np, "qcom,freq-table",
			&freq_table[1], opp_ct - 1);
	if (ret < 0) {
		dev_err(dev, "DDR frequency read error:%d\n", ret);
		goto err_exit;
	}

	ddr_cdev->freq_table = freq_table;
	ddr_cdev->freq_table_size = opp_ct;
	ddr_cdev->cur_state = 0;
	ddr_cdev->max_state = opp_ct - 1;
	ddr_cdev->dev = dev;

	ret = of_property_read_u32(np, "qcom,bus-width", &bus_width);
	if (ret < 0) {
		dev_err(dev, "DDR bus width read error:%d\n", ret);
		goto err_exit;
	}

	for (idx = 0; idx < opp_ct; idx++)
		ddr_cdev->freq_table[idx] *= bus_width;

	ret = icc_set_bw(ddr_cdev->icc_path, 0, freq_table[0]);
	if (ret < 0) {
		dev_err(dev, "Error placing DDR freq request. err:%d\n",
			ret);
		goto err_exit;
	}

	ddr_cdev->cdev = thermal_of_cooling_device_register(np, cdev_name,
					ddr_cdev, &ddr_cdev_ops);
	if (IS_ERR(ddr_cdev->cdev)) {
		ret = PTR_ERR(ddr_cdev->cdev);
		dev_err(dev, "Cdev register failed for %s, ret:%d\n",
			cdev_name, ret);
		goto err_exit;
	}
	dev_dbg(dev, "Cooling device [%s] registered.\n", cdev_name);
	dev_set_drvdata(dev, ddr_cdev);

	return 0;
err_exit:
	icc_put(ddr_cdev->icc_path);

	return ret;
}

static int ddr_cdev_remove(struct platform_device *pdev)
{
	struct ddr_cdev *ddr_cdev =
		(struct ddr_cdev *)dev_get_drvdata(&pdev->dev);

	if (ddr_cdev->cdev) {
		thermal_cooling_device_unregister(ddr_cdev->cdev);
		ddr_cdev->cdev = NULL;
	}
	if (ddr_cdev->icc_path) {
		icc_set_bw(ddr_cdev->icc_path, 0, ddr_cdev->freq_table[0]);
		icc_put(ddr_cdev->icc_path);
		ddr_cdev->icc_path = NULL;
	}

	return 0;
}

static const struct of_device_id ddr_cdev_match[] = {
	{ .compatible = "qcom,ddr-cooling-device", },
	{},
};

static struct platform_driver ddr_cdev_driver = {
	.probe		= ddr_cdev_probe,
	.remove         = ddr_cdev_remove,
	.driver		= {
		.name = KBUILD_MODNAME,
		.of_match_table = ddr_cdev_match,
	},
};
module_platform_driver(ddr_cdev_driver);
MODULE_LICENSE("GPL");
