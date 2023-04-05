// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <dt-bindings/interconnect/qcom,osm-l3.h>

#include "sc7180.h"
#include "sdm845.h"
#include "sm8150.h"
#include "sm8250.h"

#define LUT_MAX_ENTRIES			40U
#define LUT_SRC				GENMASK(31, 30)
#define LUT_L_VAL			GENMASK(7, 0)
#define CLK_HW_DIV			2

/* OSM Register offsets */
#define REG_ENABLE			0x0
#define OSM_LUT_ROW_SIZE		32
#define OSM_REG_FREQ_LUT		0x110
#define OSM_REG_PERF_STATE		0x920

/* EPSS Register offsets */
#define EPSS_LUT_ROW_SIZE		4
#define EPSS_REG_FREQ_LUT		0x100
#define EPSS_REG_PERF_STATE		0x320

#define OSM_L3_MAX_LINKS		1

#define to_qcom_provider(_provider) \
	container_of(_provider, struct qcom_osm_l3_icc_provider, provider)

struct qcom_osm_l3_icc_provider {
	void __iomem *base;
	unsigned int max_state;
	unsigned int reg_perf_state;
	unsigned long lut_tables[LUT_MAX_ENTRIES];
	struct icc_provider provider;
};

/**
 * struct qcom_icc_node - Qualcomm specific interconnect nodes
 * @name: the node name used in debugfs
 * @links: an array of nodes where we can go next while traversing
 * @id: a unique node identifier
 * @num_links: the total number of @links
 * @buswidth: width of the interconnect between a node and the bus
 */
struct qcom_icc_node {
	const char *name;
	u16 links[OSM_L3_MAX_LINKS];
	u16 id;
	u16 num_links;
	u16 buswidth;
};

struct qcom_icc_desc {
	const struct qcom_icc_node **nodes;
	size_t num_nodes;
	unsigned int lut_row_size;
	unsigned int reg_freq_lut;
	unsigned int reg_perf_state;
};

#define DEFINE_QNODE(_name, _id, _buswidth, ...)			\
	static const struct qcom_icc_node _name = {			\
		.name = #_name,						\
		.id = _id,						\
		.buswidth = _buswidth,					\
		.num_links = ARRAY_SIZE(((int[]){ __VA_ARGS__ })),	\
		.links = { __VA_ARGS__ },				\
	}

DEFINE_QNODE(sdm845_osm_apps_l3, SDM845_MASTER_OSM_L3_APPS, 16, SDM845_SLAVE_OSM_L3);
DEFINE_QNODE(sdm845_osm_l3, SDM845_SLAVE_OSM_L3, 16);

static const struct qcom_icc_node *sdm845_osm_l3_nodes[] = {
	[MASTER_OSM_L3_APPS] = &sdm845_osm_apps_l3,
	[SLAVE_OSM_L3] = &sdm845_osm_l3,
};

static const struct qcom_icc_desc sdm845_icc_osm_l3 = {
	.nodes = sdm845_osm_l3_nodes,
	.num_nodes = ARRAY_SIZE(sdm845_osm_l3_nodes),
	.lut_row_size = OSM_LUT_ROW_SIZE,
	.reg_freq_lut = OSM_REG_FREQ_LUT,
	.reg_perf_state = OSM_REG_PERF_STATE,
};

DEFINE_QNODE(sc7180_osm_apps_l3, SC7180_MASTER_OSM_L3_APPS, 16, SC7180_SLAVE_OSM_L3);
DEFINE_QNODE(sc7180_osm_l3, SC7180_SLAVE_OSM_L3, 16);

static const struct qcom_icc_node *sc7180_osm_l3_nodes[] = {
	[MASTER_OSM_L3_APPS] = &sc7180_osm_apps_l3,
	[SLAVE_OSM_L3] = &sc7180_osm_l3,
};

static const struct qcom_icc_desc sc7180_icc_osm_l3 = {
	.nodes = sc7180_osm_l3_nodes,
	.num_nodes = ARRAY_SIZE(sc7180_osm_l3_nodes),
	.lut_row_size = OSM_LUT_ROW_SIZE,
	.reg_freq_lut = OSM_REG_FREQ_LUT,
	.reg_perf_state = OSM_REG_PERF_STATE,
};

DEFINE_QNODE(sm8150_osm_apps_l3, SM8150_MASTER_OSM_L3_APPS, 32, SM8150_SLAVE_OSM_L3);
DEFINE_QNODE(sm8150_osm_l3, SM8150_SLAVE_OSM_L3, 32);

static const struct qcom_icc_node *sm8150_osm_l3_nodes[] = {
	[MASTER_OSM_L3_APPS] = &sm8150_osm_apps_l3,
	[SLAVE_OSM_L3] = &sm8150_osm_l3,
};

static const struct qcom_icc_desc sm8150_icc_osm_l3 = {
	.nodes = sm8150_osm_l3_nodes,
	.num_nodes = ARRAY_SIZE(sm8150_osm_l3_nodes),
	.lut_row_size = OSM_LUT_ROW_SIZE,
	.reg_freq_lut = OSM_REG_FREQ_LUT,
	.reg_perf_state = OSM_REG_PERF_STATE,
};

DEFINE_QNODE(sm8250_epss_apps_l3, SM8250_MASTER_EPSS_L3_APPS, 32, SM8250_SLAVE_EPSS_L3);
DEFINE_QNODE(sm8250_epss_l3, SM8250_SLAVE_EPSS_L3, 32);

static const struct qcom_icc_node *sm8250_epss_l3_nodes[] = {
	[MASTER_EPSS_L3_APPS] = &sm8250_epss_apps_l3,
	[SLAVE_EPSS_L3_SHARED] = &sm8250_epss_l3,
};

static const struct qcom_icc_desc sm8250_icc_epss_l3 = {
	.nodes = sm8250_epss_l3_nodes,
	.num_nodes = ARRAY_SIZE(sm8250_epss_l3_nodes),
	.lut_row_size = EPSS_LUT_ROW_SIZE,
	.reg_freq_lut = EPSS_REG_FREQ_LUT,
	.reg_perf_state = EPSS_REG_PERF_STATE,
};

static int qcom_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct qcom_osm_l3_icc_provider *qp;
	struct icc_provider *provider;
	const struct qcom_icc_node *qn;
	struct icc_node *n;
	unsigned int index;
	u32 agg_peak = 0;
	u32 agg_avg = 0;
	u64 rate;

	qn = src->data;
	provider = src->provider;
	qp = to_qcom_provider(provider);

	list_for_each_entry(n, &provider->nodes, node_list)
		provider->aggregate(n, 0, n->avg_bw, n->peak_bw,
				    &agg_avg, &agg_peak);

	rate = max(agg_avg, agg_peak);
	rate = icc_units_to_bps(rate);
	do_div(rate, qn->buswidth);

	for (index = 0; index < qp->max_state - 1; index++) {
		if (qp->lut_tables[index] >= rate)
			break;
	}

	writel_relaxed(index, qp->base + qp->reg_perf_state);

	return 0;
}

static int qcom_osm_l3_remove(struct platform_device *pdev)
{
	struct qcom_osm_l3_icc_provider *qp = platform_get_drvdata(pdev);

	icc_nodes_remove(&qp->provider);
	return icc_provider_del(&qp->provider);
}

static int qcom_osm_l3_probe(struct platform_device *pdev)
{
	u32 info, src, lval, i, prev_freq = 0, freq;
	static unsigned long hw_rate, xo_rate;
	struct qcom_osm_l3_icc_provider *qp;
	const struct qcom_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	const struct qcom_icc_node **qnodes;
	struct icc_node *node;
	size_t num_nodes;
	struct clk *clk;
	int ret;

	clk = clk_get(&pdev->dev, "xo");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	xo_rate = clk_get_rate(clk);
	clk_put(clk);

	clk = clk_get(&pdev->dev, "alternate");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	hw_rate = clk_get_rate(clk) / CLK_HW_DIV;
	clk_put(clk);

	qp = devm_kzalloc(&pdev->dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	qp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(qp->base))
		return PTR_ERR(qp->base);

	/* HW should be in enabled state to proceed */
	if (!(readl_relaxed(qp->base + REG_ENABLE) & 0x1)) {
		dev_err(&pdev->dev, "error hardware not enabled\n");
		return -ENODEV;
	}

	desc = device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	qp->reg_perf_state = desc->reg_perf_state;

	for (i = 0; i < LUT_MAX_ENTRIES; i++) {
		info = readl_relaxed(qp->base + desc->reg_freq_lut +
				     i * desc->lut_row_size);
		src = FIELD_GET(LUT_SRC, info);
		lval = FIELD_GET(LUT_L_VAL, info);
		if (src)
			freq = xo_rate * lval;
		else
			freq = hw_rate;

		/* Two of the same frequencies signify end of table */
		if (i > 0 && prev_freq == freq)
			break;

		dev_dbg(&pdev->dev, "index=%d freq=%d\n", i, freq);

		qp->lut_tables[i] = freq;
		prev_freq = freq;
	}
	qp->max_state = i;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	data = devm_kzalloc(&pdev->dev, struct_size(data, nodes, num_nodes), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	provider = &qp->provider;
	provider->dev = &pdev->dev;
	provider->set = qcom_icc_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	INIT_LIST_HEAD(&provider->nodes);
	provider->data = data;

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(&pdev->dev, "error adding interconnect provider\n");
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
		/* Cast away const and add it back in qcom_icc_set() */
		node->data = (void *)qnodes[i];
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
	icc_provider_del(provider);

	return ret;
}

static const struct of_device_id osm_l3_of_match[] = {
	{ .compatible = "qcom,sc7180-osm-l3", .data = &sc7180_icc_osm_l3 },
	{ .compatible = "qcom,sdm845-osm-l3", .data = &sdm845_icc_osm_l3 },
	{ .compatible = "qcom,sm8150-osm-l3", .data = &sm8150_icc_osm_l3 },
	{ .compatible = "qcom,sm8250-epss-l3", .data = &sm8250_icc_epss_l3 },
	{ }
};
MODULE_DEVICE_TABLE(of, osm_l3_of_match);

static struct platform_driver osm_l3_driver = {
	.probe = qcom_osm_l3_probe,
	.remove = qcom_osm_l3_remove,
	.driver = {
		.name = "osm-l3",
		.of_match_table = osm_l3_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(osm_l3_driver);

MODULE_DESCRIPTION("Qualcomm OSM L3 interconnect driver");
MODULE_LICENSE("GPL v2");
