// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <linux/atomic.h>
#include <linux/coresight.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/of_platform.h>

#include <linux/mali_kbase_debug_coresight_csf.h>
#include <coresight-priv.h>
#include "sources/coresight_mali_sources.h"

static int coresight_mali_source_trace_id(struct coresight_device *csdev)
{
	struct coresight_mali_source_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	return drvdata->trcid;
}

static int coresight_mali_enable_source(struct coresight_device *csdev, struct perf_event *event,
					u32 mode)
{
	return coresight_mali_enable_component(csdev, mode);
}

static void coresight_mali_disable_source(struct coresight_device *csdev, struct perf_event *event)
{
	coresight_mali_disable_component(csdev);
}

static const struct coresight_ops_source coresight_mali_source_ops = {
	.trace_id = coresight_mali_source_trace_id,
	.enable = coresight_mali_enable_source,
	.disable = coresight_mali_disable_source
};

static const struct coresight_ops mali_cs_ops = {
	.source_ops = &coresight_mali_source_ops,
};

int coresight_mali_sources_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct coresight_platform_data *pdata = NULL;
	struct coresight_mali_source_drvdata *drvdata = NULL;
	struct coresight_desc desc = { 0 };
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct platform_device *gpu_pdev = NULL;
	struct device_node *gpu_node = NULL;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	dev_set_drvdata(dev, drvdata);
	drvdata->base.dev = dev;

#if KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE
	pdata = coresight_get_platform_data(dev);
#else
	if (np)
		pdata = of_get_coresight_platform_data(dev, np);
#endif
	if (IS_ERR(pdata)) {
		dev_err(drvdata->base.dev, "Failed to get platform data\n");
		ret = PTR_ERR(pdata);
		goto devm_kfree_drvdata;
	}

	dev->platform_data = pdata;

	gpu_node = of_parse_phandle(np, "gpu", 0);
	if (!gpu_node) {
		dev_err(drvdata->base.dev, "GPU node not available\n");
		goto devm_kfree_drvdata;
	}
	gpu_pdev = of_find_device_by_node(gpu_node);
	if (gpu_pdev == NULL) {
		dev_err(drvdata->base.dev, "Couldn't find GPU device from node\n");
		goto devm_kfree_drvdata;
	}

	drvdata->base.gpu_dev = platform_get_drvdata(gpu_pdev);
	if (!drvdata->base.gpu_dev) {
		dev_err(drvdata->base.dev, "GPU dev not available\n");
		goto devm_kfree_drvdata;
	}

	ret = coresight_mali_sources_init_drvdata(drvdata);
	if (ret) {
		dev_err(drvdata->base.dev, "Failed to init source driver data\n");
		goto kbase_client_unregister;
	}

	desc.type = CORESIGHT_DEV_TYPE_SOURCE;
	desc.subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_SOFTWARE;
	desc.ops = &mali_cs_ops;
	desc.pdata = pdata;
	desc.dev = dev;
	desc.groups = coresight_mali_source_groups_get();

#if KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE
	desc.name = devm_kasprintf(dev, GFP_KERNEL, "%s", drvdata->type_name);
	if (!desc.name) {
		ret = -ENOMEM;
		goto devm_kfree_drvdata;
	}
#endif
	drvdata->base.csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->base.csdev)) {
		dev_err(drvdata->base.dev, "Failed to register coresight device\n");
		ret = PTR_ERR(drvdata->base.csdev);
		goto devm_kfree_drvdata;
	}

	return ret;

kbase_client_unregister:
	if (drvdata->base.csdev != NULL)
		coresight_unregister(drvdata->base.csdev);

	coresight_mali_sources_deinit_drvdata(drvdata);

devm_kfree_drvdata:
	devm_kfree(dev, drvdata);

	return ret;
}

int coresight_mali_sources_remove(struct platform_device *pdev)
{
	struct coresight_mali_source_drvdata *drvdata = dev_get_drvdata(&pdev->dev);

	if (drvdata->base.csdev != NULL)
		coresight_unregister(drvdata->base.csdev);

	coresight_mali_sources_deinit_drvdata(drvdata);

	devm_kfree(&pdev->dev, drvdata);

	return 0;
}

MODULE_AUTHOR("ARM Ltd.");
MODULE_DESCRIPTION("Arm Coresight Mali source");
MODULE_LICENSE("GPL");
