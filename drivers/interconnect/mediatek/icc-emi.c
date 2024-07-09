// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek External Memory Interface (EMI) Interconnect driver
 *
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2024 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/dvfsrc.h>

#include "icc-emi.h"

static int mtk_emi_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
				 u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	struct mtk_icc_node *in = node->data;

	*agg_avg += avg_bw;
	*agg_peak = max_t(u32, *agg_peak, peak_bw);

	in->sum_avg = *agg_avg;
	in->max_peak = *agg_peak;

	return 0;
}

static int mtk_emi_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct mtk_icc_node *node = dst->data;
	struct device *dev;
	int ret;

	if (unlikely(!src->provider))
		return -EINVAL;

	dev = src->provider->dev;

	switch (node->ep) {
	case 0:
		break;
	case 1:
		ret = mtk_dvfsrc_send_request(dev, MTK_DVFSRC_CMD_PEAK_BW, node->max_peak);
		if (ret) {
			dev_err(dev, "Cannot send peak bw request: %d\n", ret);
			return ret;
		}

		ret = mtk_dvfsrc_send_request(dev, MTK_DVFSRC_CMD_BW, node->sum_avg);
		if (ret) {
			dev_err(dev, "Cannot send bw request: %d\n", ret);
			return ret;
		}
		break;
	case 2:
		ret = mtk_dvfsrc_send_request(dev, MTK_DVFSRC_CMD_HRT_BW, node->sum_avg);
		if (ret) {
			dev_err(dev, "Cannot send HRT bw request: %d\n", ret);
			return ret;
		}
		break;
	default:
		dev_err(src->provider->dev, "Unknown endpoint %u\n", node->ep);
		return -EINVAL;
	}

	return 0;
}

int mtk_emi_icc_probe(struct platform_device *pdev)
{
	const struct mtk_icc_desc *desc;
	struct device *dev = &pdev->dev;
	struct icc_node *node;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct mtk_icc_node **mnodes;
	int i, j, ret;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	mnodes = desc->nodes;

	provider = devm_kzalloc(dev, sizeof(*provider), GFP_KERNEL);
	if (!provider)
		return -ENOMEM;

	data = devm_kzalloc(dev, struct_size(data, nodes, desc->num_nodes), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	provider->dev = pdev->dev.parent;
	provider->set = mtk_emi_icc_set;
	provider->aggregate = mtk_emi_icc_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	INIT_LIST_HEAD(&provider->nodes);
	provider->data = data;

	for (i = 0; i < desc->num_nodes; i++) {
		if (!mnodes[i])
			continue;

		node = icc_node_create(mnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = mnodes[i]->name;
		node->data = mnodes[i];
		icc_node_add(node, provider);

		for (j = 0; j < mnodes[i]->num_links; j++)
			icc_link_create(node, mnodes[i]->links[j]);

		data->nodes[i] = node;
	}
	data->num_nodes = desc->num_nodes;

	ret = icc_provider_register(provider);
	if (ret)
		goto err;

	platform_set_drvdata(pdev, provider);

	return 0;
err:
	icc_nodes_remove(provider);
	return ret;
}
EXPORT_SYMBOL_GPL(mtk_emi_icc_probe);

void mtk_emi_icc_remove(struct platform_device *pdev)
{
	struct icc_provider *provider = platform_get_drvdata(pdev);

	icc_provider_deregister(provider);
	icc_nodes_remove(provider);
}
EXPORT_SYMBOL_GPL(mtk_emi_icc_remove);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_AUTHOR("Henry Chen <henryc.chen@mediatek.com>");
MODULE_DESCRIPTION("MediaTek External Memory Interface interconnect driver");
MODULE_LICENSE("GPL");
