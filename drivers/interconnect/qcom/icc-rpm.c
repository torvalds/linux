// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Linaro Ltd
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "smd-rpm.h"
#include "icc-rpm.h"

static int qcom_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct qcom_icc_provider *qp;
	struct qcom_icc_node *qn;
	struct icc_provider *provider;
	struct icc_node *n;
	u64 sum_bw;
	u64 max_peak_bw;
	u64 rate;
	u32 agg_avg = 0;
	u32 agg_peak = 0;
	int ret, i;

	qn = src->data;
	provider = src->provider;
	qp = to_qcom_provider(provider);

	list_for_each_entry(n, &provider->nodes, node_list)
		provider->aggregate(n, 0, n->avg_bw, n->peak_bw,
				    &agg_avg, &agg_peak);

	sum_bw = icc_units_to_bps(agg_avg);
	max_peak_bw = icc_units_to_bps(agg_peak);

	/* send bandwidth request message to the RPM processor */
	if (qn->mas_rpm_id != -1) {
		ret = qcom_icc_rpm_smd_send(QCOM_SMD_RPM_ACTIVE_STATE,
					    RPM_BUS_MASTER_REQ,
					    qn->mas_rpm_id,
					    sum_bw);
		if (ret) {
			pr_err("qcom_icc_rpm_smd_send mas %d error %d\n",
			       qn->mas_rpm_id, ret);
			return ret;
		}
	}

	if (qn->slv_rpm_id != -1) {
		ret = qcom_icc_rpm_smd_send(QCOM_SMD_RPM_ACTIVE_STATE,
					    RPM_BUS_SLAVE_REQ,
					    qn->slv_rpm_id,
					    sum_bw);
		if (ret) {
			pr_err("qcom_icc_rpm_smd_send slv error %d\n",
			       ret);
			return ret;
		}
	}

	rate = max(sum_bw, max_peak_bw);

	do_div(rate, qn->buswidth);

	if (qn->rate == rate)
		return 0;

	for (i = 0; i < qp->num_clks; i++) {
		ret = clk_set_rate(qp->bus_clks[i].clk, rate);
		if (ret) {
			pr_err("%s clk_set_rate error: %d\n",
			       qp->bus_clks[i].id, ret);
			return ret;
		}
	}

	qn->rate = rate;

	return 0;
}

int qnoc_probe(struct platform_device *pdev, size_t cd_size, int cd_num,
	       const struct clk_bulk_data *cd)
{
	struct device *dev = &pdev->dev;
	const struct qcom_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_node **qnodes;
	struct qcom_icc_provider *qp;
	struct icc_node *node;
	size_t num_nodes, i;
	int ret;

	/* wait for the RPM proxy */
	if (!qcom_icc_rpm_smd_available())
		return -EPROBE_DEFER;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kzalloc(dev, struct_size(data, nodes, num_nodes),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	qp->bus_clks = devm_kmemdup(dev, cd, cd_size,
				    GFP_KERNEL);
	if (!qp->bus_clks)
		return -ENOMEM;

	qp->num_clks = cd_num;
	ret = devm_clk_bulk_get(dev, qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(qp->num_clks, qp->bus_clks);
	if (ret)
		return ret;

	provider = &qp->provider;
	INIT_LIST_HEAD(&provider->nodes);
	provider->dev = dev;
	provider->set = qcom_icc_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	provider->data = data;

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(dev, "error adding interconnect provider: %d\n", ret);
		clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
		return ret;
	}

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = qnodes[i]->name;
		node->data = qnodes[i];
		icc_node_add(node, provider);

		for (j = 0; j < qnodes[i]->num_links; j++)
			icc_link_create(node, qnodes[i]->links[j]);

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;

	platform_set_drvdata(pdev, qp);

	return 0;
err:
	icc_nodes_remove(provider);
	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
	icc_provider_del(provider);

	return ret;
}
EXPORT_SYMBOL(qnoc_probe);

int qnoc_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);

	icc_nodes_remove(&qp->provider);
	clk_bulk_disable_unprepare(qp->num_clks, qp->bus_clks);
	return icc_provider_del(&qp->provider);
}
EXPORT_SYMBOL(qnoc_remove);
