/* Copyright (c) 2012-2015, 2017-2018, The Linux Foundation.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk/clk-conf.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <drm/drm_print.h>

#include "dpu_io_util.h"

void msm_dss_put_clk(struct dss_clk *clk_arry, int num_clk)
{
	int i;

	for (i = num_clk - 1; i >= 0; i--) {
		if (clk_arry[i].clk)
			clk_put(clk_arry[i].clk);
		clk_arry[i].clk = NULL;
	}
}

int msm_dss_get_clk(struct device *dev, struct dss_clk *clk_arry, int num_clk)
{
	int i, rc = 0;

	for (i = 0; i < num_clk; i++) {
		clk_arry[i].clk = clk_get(dev, clk_arry[i].clk_name);
		rc = PTR_ERR_OR_ZERO(clk_arry[i].clk);
		if (rc) {
			DEV_ERR("%pS->%s: '%s' get failed. rc=%d\n",
				__builtin_return_address(0), __func__,
				clk_arry[i].clk_name, rc);
			goto error;
		}
	}

	return rc;

error:
	for (i--; i >= 0; i--) {
		if (clk_arry[i].clk)
			clk_put(clk_arry[i].clk);
		clk_arry[i].clk = NULL;
	}

	return rc;
}

int msm_dss_clk_set_rate(struct dss_clk *clk_arry, int num_clk)
{
	int i, rc = 0;

	for (i = 0; i < num_clk; i++) {
		if (clk_arry[i].clk) {
			if (clk_arry[i].type != DSS_CLK_AHB) {
				DEV_DBG("%pS->%s: '%s' rate %ld\n",
					__builtin_return_address(0), __func__,
					clk_arry[i].clk_name,
					clk_arry[i].rate);
				rc = clk_set_rate(clk_arry[i].clk,
					clk_arry[i].rate);
				if (rc) {
					DEV_ERR("%pS->%s: %s failed. rc=%d\n",
						__builtin_return_address(0),
						__func__,
						clk_arry[i].clk_name, rc);
					break;
				}
			}
		} else {
			DEV_ERR("%pS->%s: '%s' is not available\n",
				__builtin_return_address(0), __func__,
				clk_arry[i].clk_name);
			rc = -EPERM;
			break;
		}
	}

	return rc;
}

int msm_dss_enable_clk(struct dss_clk *clk_arry, int num_clk, int enable)
{
	int i, rc = 0;

	if (enable) {
		for (i = 0; i < num_clk; i++) {
			DEV_DBG("%pS->%s: enable '%s'\n",
				__builtin_return_address(0), __func__,
				clk_arry[i].clk_name);
			if (clk_arry[i].clk) {
				rc = clk_prepare_enable(clk_arry[i].clk);
				if (rc)
					DEV_ERR("%pS->%s: %s en fail. rc=%d\n",
						__builtin_return_address(0),
						__func__,
						clk_arry[i].clk_name, rc);
			} else {
				DEV_ERR("%pS->%s: '%s' is not available\n",
					__builtin_return_address(0), __func__,
					clk_arry[i].clk_name);
				rc = -EPERM;
			}

			if (rc) {
				msm_dss_enable_clk(&clk_arry[i],
					i, false);
				break;
			}
		}
	} else {
		for (i = num_clk - 1; i >= 0; i--) {
			DEV_DBG("%pS->%s: disable '%s'\n",
				__builtin_return_address(0), __func__,
				clk_arry[i].clk_name);

			if (clk_arry[i].clk)
				clk_disable_unprepare(clk_arry[i].clk);
			else
				DEV_ERR("%pS->%s: '%s' is not available\n",
					__builtin_return_address(0), __func__,
					clk_arry[i].clk_name);
		}
	}

	return rc;
}

int msm_dss_parse_clock(struct platform_device *pdev,
			struct dss_module_power *mp)
{
	u32 i, rc = 0;
	const char *clock_name;
	int num_clk = 0;

	if (!pdev || !mp)
		return -EINVAL;

	mp->num_clk = 0;
	num_clk = of_property_count_strings(pdev->dev.of_node, "clock-names");
	if (num_clk <= 0) {
		pr_debug("clocks are not defined\n");
		return 0;
	}

	mp->clk_config = devm_kcalloc(&pdev->dev,
				      num_clk, sizeof(struct dss_clk),
				      GFP_KERNEL);
	if (!mp->clk_config)
		return -ENOMEM;

	for (i = 0; i < num_clk; i++) {
		rc = of_property_read_string_index(pdev->dev.of_node,
						   "clock-names", i,
						   &clock_name);
		if (rc) {
			DRM_DEV_ERROR(&pdev->dev, "Failed to get clock name for %d\n",
				i);
			break;
		}
		strlcpy(mp->clk_config[i].clk_name, clock_name,
			sizeof(mp->clk_config[i].clk_name));

		mp->clk_config[i].type = DSS_CLK_AHB;
	}

	rc = msm_dss_get_clk(&pdev->dev, mp->clk_config, num_clk);
	if (rc) {
		DRM_DEV_ERROR(&pdev->dev, "Failed to get clock refs %d\n", rc);
		goto err;
	}

	rc = of_clk_set_defaults(pdev->dev.of_node, false);
	if (rc) {
		DRM_DEV_ERROR(&pdev->dev, "Failed to set clock defaults %d\n", rc);
		goto err;
	}

	for (i = 0; i < num_clk; i++) {
		u32 rate = clk_get_rate(mp->clk_config[i].clk);
		if (!rate)
			continue;
		mp->clk_config[i].rate = rate;
		mp->clk_config[i].type = DSS_CLK_PCLK;
	}

	mp->num_clk = num_clk;
	return 0;

err:
	msm_dss_put_clk(mp->clk_config, num_clk);
	return rc;
}
