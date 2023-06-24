// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#include "bcm-voter.h"
#include "icc-common.h"
#include "icc-rpmh.h"

/**
 * qcom_icc_pre_aggregate - cleans up stale values from prior icc_set
 * @node: icc node to operate on
 */
void qcom_icc_pre_aggregate(struct icc_node *node)
{
	size_t i;
	struct qcom_icc_node *qn;
	struct qcom_icc_provider *qp;

	qn = node->data;
	qp = to_qcom_provider(node->provider);

	for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++) {
		qn->sum_avg[i] = 0;
		qn->max_peak[i] = 0;
	}

	for (i = 0; i < qn->num_bcms; i++)
		qcom_icc_bcm_voter_add(qp->voter, qn->bcms[i]);
}
EXPORT_SYMBOL_GPL(qcom_icc_pre_aggregate);

/**
 * qcom_icc_aggregate - aggregate bw for buckets indicated by tag
 * @node: node to aggregate
 * @tag: tag to indicate which buckets to aggregate
 * @avg_bw: new bw to sum aggregate
 * @peak_bw: new bw to max aggregate
 * @agg_avg: existing aggregate avg bw val
 * @agg_peak: existing aggregate peak bw val
 */
int qcom_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
		       u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	size_t i;
	struct qcom_icc_node *qn;

	qn = node->data;

	if (!tag)
		tag = QCOM_ICC_TAG_ALWAYS;

	for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++) {
		if (tag & BIT(i)) {
			qn->sum_avg[i] += avg_bw;
			qn->max_peak[i] = max_t(u32, qn->max_peak[i], peak_bw);
		}

		if (node->init_avg || node->init_peak) {
			qn->sum_avg[i] = max_t(u64, qn->sum_avg[i], node->init_avg);
			qn->max_peak[i] = max_t(u64, qn->max_peak[i], node->init_peak);
		}
	}

	*agg_avg += avg_bw;
	*agg_peak = max_t(u32, *agg_peak, peak_bw);

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_icc_aggregate);

/**
 * qcom_icc_set - set the constraints based on path
 * @src: source node for the path to set constraints on
 * @dst: destination node for the path to set constraints on
 *
 * Return: 0 on success, or an error code otherwise
 */
int qcom_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct qcom_icc_provider *qp;
	struct icc_node *node;

	if (!src)
		node = dst;
	else
		node = src;

	qp = to_qcom_provider(node->provider);

	qcom_icc_bcm_voter_commit(qp->voter);

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_icc_set);

/**
 * qcom_icc_bcm_init - populates bcm aux data and connect qnodes
 * @bcm: bcm to be initialized
 * @dev: associated provider device
 *
 * Return: 0 on success, or an error code otherwise
 */
int qcom_icc_bcm_init(struct qcom_icc_bcm *bcm, struct device *dev)
{
	struct qcom_icc_node *qn;
	const struct bcm_db *data;
	size_t data_count;
	int i;

	/* BCM is already initialised*/
	if (bcm->addr)
		return 0;

	bcm->addr = cmd_db_read_addr(bcm->name);
	if (!bcm->addr) {
		dev_err(dev, "%s could not find RPMh address\n",
			bcm->name);
		return -EINVAL;
	}

	data = cmd_db_read_aux_data(bcm->name, &data_count);
	if (IS_ERR(data)) {
		dev_err(dev, "%s command db read error (%ld)\n",
			bcm->name, PTR_ERR(data));
		return PTR_ERR(data);
	}
	if (!data_count) {
		dev_err(dev, "%s command db missing or partial aux data\n",
			bcm->name);
		return -EINVAL;
	}

	bcm->aux_data.unit = le32_to_cpu(data->unit);
	bcm->aux_data.width = le16_to_cpu(data->width);
	bcm->aux_data.vcd = data->vcd;
	bcm->aux_data.reserved = data->reserved;
	INIT_LIST_HEAD(&bcm->list);
	INIT_LIST_HEAD(&bcm->ws_list);

	if (!bcm->vote_scale)
		bcm->vote_scale = 1000;

	/* Link Qnodes to their respective BCMs */
	for (i = 0; i < bcm->num_nodes; i++) {
		qn = bcm->nodes[i];
		qn->bcms[qn->num_bcms] = bcm;
		qn->num_bcms++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_icc_bcm_init);

int qcom_icc_rpmh_probe(struct platform_device *pdev)
{
	const struct qcom_icc_desc *desc;
	struct device *dev = &pdev->dev;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_node * const *qnodes, *qn;
	struct qcom_icc_provider *qp;
	struct icc_node *node;
	size_t num_nodes, i, j;
	int ret;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kzalloc(dev, struct_size(data, nodes, num_nodes), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	provider = &qp->provider;
	provider->dev = dev;
	provider->set = qcom_icc_set;
	provider->pre_aggregate = qcom_icc_pre_aggregate;
	provider->aggregate = qcom_icc_aggregate;
	provider->xlate_extended = qcom_icc_xlate_extended;
	INIT_LIST_HEAD(&provider->nodes);
	provider->data = data;

	qp->dev = dev;
	qp->bcms = desc->bcms;
	qp->num_bcms = desc->num_bcms;

	qp->voter = of_bcm_voter_get(qp->dev, NULL);
	if (IS_ERR(qp->voter))
		return PTR_ERR(qp->voter);

	ret = icc_provider_add(provider);
	if (ret)
		return ret;

	for (i = 0; i < qp->num_bcms; i++)
		qcom_icc_bcm_init(qp->bcms[i], dev);

	for (i = 0; i < num_nodes; i++) {
		qn = qnodes[i];
		if (!qn)
			continue;

		node = icc_node_create(qn->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = qn->name;
		node->data = qn;
		icc_node_add(node, provider);

		for (j = 0; j < qn->num_links; j++)
			icc_link_create(node, qn->links[j]);

		data->nodes[i] = node;
	}

	data->num_nodes = num_nodes;
	platform_set_drvdata(pdev, qp);

	/* Populate child NoC devices if any */
	if (of_get_child_count(dev->of_node) > 0)
		return of_platform_populate(dev->of_node, NULL, NULL, dev);

	return 0;
err:
	icc_nodes_remove(provider);
	icc_provider_del(provider);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_icc_rpmh_probe);

int qcom_icc_rpmh_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);

	icc_nodes_remove(&qp->provider);
	icc_provider_del(&qp->provider);

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_icc_rpmh_remove);

MODULE_LICENSE("GPL v2");
