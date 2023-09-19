// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2019, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/io.h>

#define CXIP_LM_CDEV_DRIVER "cx-ipeak-cooling-device"
#define CXIP_LM_CDEV_MAX_STATE 1

#define CXIP_LM_VOTE_STATUS       0x0
#define CXIP_LM_BYPASS            0x4
#define CXIP_LM_VOTE_CLEAR        0x8
#define CXIP_LM_VOTE_SET          0xc
#define CXIP_LM_FEATURE_EN        0x10
#define CXIP_LM_BYPASS_VAL        0xff20
#define CXIP_LM_THERM_VOTE_VAL    0x80
#define CXIP_LM_FEATURE_EN_VAL    0x1

struct cxip_lm_cooling_device {
	struct thermal_cooling_device	*cool_dev;
	char				cdev_name[THERMAL_NAME_LENGTH];
	void				*cx_ip_reg_base;
	unsigned int			therm_clnt;
	unsigned int			*bypass_clnts;
	unsigned int			bypass_clnt_cnt;
	bool				state;
};

static void cxip_lm_therm_vote_apply(struct cxip_lm_cooling_device *cxip_dev,
					bool vote)
{
	int vote_offset = 0, val = 0, sts_offset = 0;

	if (!cxip_dev->therm_clnt) {
		vote_offset = vote ? CXIP_LM_VOTE_SET : CXIP_LM_VOTE_CLEAR;
		val = CXIP_LM_THERM_VOTE_VAL;
		sts_offset = CXIP_LM_VOTE_STATUS;
	} else {
		vote_offset = cxip_dev->therm_clnt;
		val = vote ? 0x1 : 0x0;
		sts_offset = vote_offset;
	}

	writel_relaxed(val, cxip_dev->cx_ip_reg_base + vote_offset);
	pr_debug("%s vote for cxip_lm. vote:0x%x\n",
		vote ? "Applied" : "Cleared",
		readl_relaxed(cxip_dev->cx_ip_reg_base + sts_offset));
}

static void cxip_lm_initialize_cxip_hw(struct cxip_lm_cooling_device *cxip_dev)
{
	int i = 0;

	/* Set CXIP LM proxy vote for clients who are not participating */
	if (cxip_dev->bypass_clnt_cnt)
		for (i = 0; i < cxip_dev->bypass_clnt_cnt; i++)
			writel_relaxed(0x1, cxip_dev->cx_ip_reg_base +
					cxip_dev->bypass_clnts[i]);
	else if (!cxip_dev->therm_clnt)
		writel_relaxed(CXIP_LM_BYPASS_VAL,
			cxip_dev->cx_ip_reg_base + CXIP_LM_BYPASS);

	/* Enable CXIP LM HW */
	writel_relaxed(CXIP_LM_FEATURE_EN_VAL, cxip_dev->cx_ip_reg_base +
			CXIP_LM_FEATURE_EN);
}

static int cxip_lm_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = CXIP_LM_CDEV_MAX_STATE;

	return 0;
}

static int cxip_lm_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct cxip_lm_cooling_device *cxip_dev = cdev->devdata;
	int ret = 0;

	if (state > CXIP_LM_CDEV_MAX_STATE)
		return -EINVAL;

	if (cxip_dev->state == state)
		return 0;

	cxip_lm_therm_vote_apply(cxip_dev, state);
	cxip_dev->state = state;

	return ret;
}

static int cxip_lm_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct cxip_lm_cooling_device *cxip_dev = cdev->devdata;

	*state = cxip_dev->state;

	return 0;
}

static struct thermal_cooling_device_ops cxip_lm_device_ops = {
	.get_max_state = cxip_lm_get_max_state,
	.get_cur_state = cxip_lm_get_cur_state,
	.set_cur_state = cxip_lm_set_cur_state,
};

static int cxip_lm_cdev_remove(struct platform_device *pdev)
{
	struct cxip_lm_cooling_device *cxip_dev =
		(struct cxip_lm_cooling_device *)dev_get_drvdata(&pdev->dev);

	if (cxip_dev) {
		if (cxip_dev->cool_dev) {
			thermal_cooling_device_unregister(cxip_dev->cool_dev);
			cxip_dev->cool_dev = NULL;
		}

		if (cxip_dev->cx_ip_reg_base)
			cxip_lm_therm_vote_apply(cxip_dev->cx_ip_reg_base,
							false);
	}

	return 0;
}

static int cxip_lm_get_devicetree_data(struct platform_device *pdev,
					struct cxip_lm_cooling_device *cxip_dev,
					struct device_node *np)
{
	int ret = 0;

	ret = of_property_read_u32(np, "qcom,thermal-client-offset",
			&cxip_dev->therm_clnt);
	if (ret) {
		dev_dbg(&pdev->dev,
			"error for qcom,thermal-client-offset. ret:%d\n",
			ret);
		cxip_dev->therm_clnt = 0;
		ret = 0;
		return ret;
	}

	ret = of_property_count_u32_elems(np, "qcom,bypass-client-list");
	if (ret <= 0) {
		dev_dbg(&pdev->dev, "Invalid number of clients err:%d\n", ret);
		ret = 0;
		return ret;
	}
	cxip_dev->bypass_clnt_cnt = ret;

	cxip_dev->bypass_clnts = devm_kcalloc(&pdev->dev,
				cxip_dev->bypass_clnt_cnt,
				sizeof(*cxip_dev->bypass_clnts), GFP_KERNEL);
	if (!cxip_dev->bypass_clnts)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "qcom,bypass-client-list",
		cxip_dev->bypass_clnts, cxip_dev->bypass_clnt_cnt);
	if (ret) {
		dev_dbg(&pdev->dev, "bypass client list err:%d, cnt:%d\n",
			ret, cxip_dev->bypass_clnt_cnt);
		cxip_dev->bypass_clnt_cnt = 0;
		ret = 0;
	}

	return ret;
}

static int cxip_lm_cdev_probe(struct platform_device *pdev)
{
	struct cxip_lm_cooling_device *cxip_dev = NULL;
	int ret = 0;
	struct device_node *np;
	struct resource *res = NULL;

	np = dev_of_node(&pdev->dev);
	if (!np) {
		dev_err(&pdev->dev,
			"of node not available for cxip_lm cdev\n");
		return -EINVAL;
	}

	cxip_dev = devm_kzalloc(&pdev->dev, sizeof(*cxip_dev), GFP_KERNEL);
	if (!cxip_dev)
		return -ENOMEM;

	ret = cxip_lm_get_devicetree_data(pdev, cxip_dev, np);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"cxip_lm platform get resource failed\n");
		return -ENODEV;
	}

	cxip_dev->cx_ip_reg_base = devm_ioremap(&pdev->dev, res->start,
						resource_size(res));
	if (!cxip_dev->cx_ip_reg_base) {
		dev_err(&pdev->dev, "cxip_lm reg remap failed\n");
		return -ENOMEM;
	}

	cxip_lm_initialize_cxip_hw(cxip_dev);

	/* Set thermal vote till we get first vote from TF */
	cxip_dev->state = true;
	cxip_lm_therm_vote_apply(cxip_dev, cxip_dev->state);

	strscpy(cxip_dev->cdev_name, np->name, THERMAL_NAME_LENGTH);
	cxip_dev->cool_dev = thermal_of_cooling_device_register(
					np, cxip_dev->cdev_name, cxip_dev,
					&cxip_lm_device_ops);
	if (IS_ERR(cxip_dev->cool_dev)) {
		ret = PTR_ERR(cxip_dev->cool_dev);
		dev_err(&pdev->dev, "cxip_lm cdev register err:%d\n",
				ret);
		cxip_dev->cool_dev = NULL;
		cxip_lm_therm_vote_apply(cxip_dev->cx_ip_reg_base,
						false);
		return ret;
	}

	dev_set_drvdata(&pdev->dev, cxip_dev);

	return ret;
}

static const struct of_device_id cxip_lm_cdev_of_match[] = {
	{.compatible = "qcom,cxip-lm-cooling-device", },
	{}
};

static struct platform_driver cxip_lm_cdev_driver = {
	.driver = {
		.name = CXIP_LM_CDEV_DRIVER,
		.of_match_table = cxip_lm_cdev_of_match,
	},
	.probe = cxip_lm_cdev_probe,
	.remove = cxip_lm_cdev_remove,
};
module_platform_driver(cxip_lm_cdev_driver);
MODULE_DESCRIPTION("CX IPEAK cooling device driver");
MODULE_LICENSE("GPL");
