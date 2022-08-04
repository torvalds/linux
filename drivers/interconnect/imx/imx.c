// SPDX-License-Identifier: GPL-2.0
/*
 * Interconnect framework driver for i.MX SoC
 *
 * Copyright (c) 2019, BayLibre
 * Copyright (c) 2019-2020, NXP
 * Author: Alexandre Bailon <abailon@baylibre.com>
 * Author: Leonard Crestez <leonard.crestez@nxp.com>
 */

#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>

#include "imx.h"

/* private icc_node data */
struct imx_icc_node {
	const struct imx_icc_node_desc *desc;
	struct device *qos_dev;
	struct dev_pm_qos_request qos_req;
};

static int imx_icc_node_set(struct icc_node *node)
{
	struct device *dev = node->provider->dev;
	struct imx_icc_node *node_data = node->data;
	u64 freq;

	if (!node_data->qos_dev)
		return 0;

	freq = (node->avg_bw + node->peak_bw) * node_data->desc->adj->bw_mul;
	do_div(freq, node_data->desc->adj->bw_div);
	dev_dbg(dev, "node %s device %s avg_bw %ukBps peak_bw %ukBps min_freq %llukHz\n",
		node->name, dev_name(node_data->qos_dev),
		node->avg_bw, node->peak_bw, freq);

	if (freq > S32_MAX) {
		dev_err(dev, "%s can't request more than S32_MAX freq\n",
				node->name);
		return -ERANGE;
	}

	dev_pm_qos_update_request(&node_data->qos_req, freq);

	return 0;
}

static int imx_icc_set(struct icc_node *src, struct icc_node *dst)
{
	return imx_icc_node_set(dst);
}

/* imx_icc_node_destroy() - Destroy an imx icc_node, including private data */
static void imx_icc_node_destroy(struct icc_node *node)
{
	struct imx_icc_node *node_data = node->data;
	int ret;

	if (dev_pm_qos_request_active(&node_data->qos_req)) {
		ret = dev_pm_qos_remove_request(&node_data->qos_req);
		if (ret)
			dev_warn(node->provider->dev,
				 "failed to remove qos request for %s\n",
				 dev_name(node_data->qos_dev));
	}

	put_device(node_data->qos_dev);
	icc_node_del(node);
	icc_node_destroy(node->id);
}

static int imx_icc_node_init_qos(struct icc_provider *provider,
				 struct icc_node *node)
{
	struct imx_icc_node *node_data = node->data;
	const struct imx_icc_node_adj_desc *adj = node_data->desc->adj;
	struct device *dev = provider->dev;
	struct device_node *dn = NULL;
	struct platform_device *pdev;

	if (adj->main_noc) {
		node_data->qos_dev = dev;
		dev_dbg(dev, "icc node %s[%d] is main noc itself\n",
			node->name, node->id);
	} else {
		dn = of_parse_phandle(dev->of_node, adj->phandle_name, 0);
		if (!dn) {
			dev_warn(dev, "Failed to parse %s\n",
				 adj->phandle_name);
			return -ENODEV;
		}
		/* Allow scaling to be disabled on a per-node basis */
		if (!of_device_is_available(dn)) {
			dev_warn(dev, "Missing property %s, skip scaling %s\n",
				 adj->phandle_name, node->name);
			of_node_put(dn);
			return 0;
		}

		pdev = of_find_device_by_node(dn);
		of_node_put(dn);
		if (!pdev) {
			dev_warn(dev, "node %s[%d] missing device for %pOF\n",
				 node->name, node->id, dn);
			return -EPROBE_DEFER;
		}
		node_data->qos_dev = &pdev->dev;
		dev_dbg(dev, "node %s[%d] has device node %pOF\n",
			node->name, node->id, dn);
	}

	return dev_pm_qos_add_request(node_data->qos_dev,
				      &node_data->qos_req,
				      DEV_PM_QOS_MIN_FREQUENCY, 0);
}

static struct icc_node *imx_icc_node_add(struct icc_provider *provider,
					 const struct imx_icc_node_desc *node_desc)
{
	struct device *dev = provider->dev;
	struct imx_icc_node *node_data;
	struct icc_node *node;
	int ret;

	node = icc_node_create(node_desc->id);
	if (IS_ERR(node)) {
		dev_err(dev, "failed to create node %d\n", node_desc->id);
		return node;
	}

	if (node->data) {
		dev_err(dev, "already created node %s id=%d\n",
			node_desc->name, node_desc->id);
		return ERR_PTR(-EEXIST);
	}

	node_data = devm_kzalloc(dev, sizeof(*node_data), GFP_KERNEL);
	if (!node_data) {
		icc_node_destroy(node->id);
		return ERR_PTR(-ENOMEM);
	}

	node->name = node_desc->name;
	node->data = node_data;
	node_data->desc = node_desc;
	icc_node_add(node, provider);

	if (node_desc->adj) {
		ret = imx_icc_node_init_qos(provider, node);
		if (ret < 0) {
			imx_icc_node_destroy(node);
			return ERR_PTR(ret);
		}
	}

	return node;
}

static void imx_icc_unregister_nodes(struct icc_provider *provider)
{
	struct icc_node *node, *tmp;

	list_for_each_entry_safe(node, tmp, &provider->nodes, node_list)
		imx_icc_node_destroy(node);
}

static int imx_icc_register_nodes(struct icc_provider *provider,
				  const struct imx_icc_node_desc *descs,
				  int count)
{
	struct icc_onecell_data *provider_data = provider->data;
	int ret;
	int i;

	for (i = 0; i < count; i++) {
		struct icc_node *node;
		const struct imx_icc_node_desc *node_desc = &descs[i];
		size_t j;

		node = imx_icc_node_add(provider, node_desc);
		if (IS_ERR(node)) {
			ret = dev_err_probe(provider->dev, PTR_ERR(node),
					    "failed to add %s\n", node_desc->name);
			goto err;
		}
		provider_data->nodes[node->id] = node;

		for (j = 0; j < node_desc->num_links; j++) {
			ret = icc_link_create(node, node_desc->links[j]);
			if (ret) {
				dev_err(provider->dev, "failed to link node %d to %d: %d\n",
					node->id, node_desc->links[j], ret);
				goto err;
			}
		}
	}

	return 0;

err:
	imx_icc_unregister_nodes(provider);

	return ret;
}

static int get_max_node_id(struct imx_icc_node_desc *nodes, int nodes_count)
{
	int i, ret = 0;

	for (i = 0; i < nodes_count; ++i)
		if (nodes[i].id > ret)
			ret = nodes[i].id;

	return ret;
}

int imx_icc_register(struct platform_device *pdev,
		     struct imx_icc_node_desc *nodes, int nodes_count)
{
	struct device *dev = &pdev->dev;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	int max_node_id;
	int ret;

	/* icc_onecell_data is indexed by node_id, unlike nodes param */
	max_node_id = get_max_node_id(nodes, nodes_count);
	data = devm_kzalloc(dev, struct_size(data, nodes, max_node_id),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->num_nodes = max_node_id;

	provider = devm_kzalloc(dev, sizeof(*provider), GFP_KERNEL);
	if (!provider)
		return -ENOMEM;
	provider->set = imx_icc_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	provider->data = data;
	provider->dev = dev->parent;
	platform_set_drvdata(pdev, provider);

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(dev, "error adding interconnect provider: %d\n", ret);
		return ret;
	}

	ret = imx_icc_register_nodes(provider, nodes, nodes_count);
	if (ret)
		goto provider_del;

	return 0;

provider_del:
	icc_provider_del(provider);
	return ret;
}
EXPORT_SYMBOL_GPL(imx_icc_register);

int imx_icc_unregister(struct platform_device *pdev)
{
	struct icc_provider *provider = platform_get_drvdata(pdev);

	imx_icc_unregister_nodes(provider);

	return icc_provider_del(provider);
}
EXPORT_SYMBOL_GPL(imx_icc_unregister);

MODULE_LICENSE("GPL v2");
