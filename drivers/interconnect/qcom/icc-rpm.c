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
#include <linux/regmap.h>
#include <linux/slab.h>

#include "smd-rpm.h"
#include "icc-common.h"
#include "icc-rpm.h"

/* QNOC QoS */
#define QNOC_QOS_MCTL_LOWn_ADDR(n)	(0x8 + (n * 0x1000))
#define QNOC_QOS_MCTL_DFLT_PRIO_MASK	0x70
#define QNOC_QOS_MCTL_DFLT_PRIO_SHIFT	4
#define QNOC_QOS_MCTL_URGFWD_EN_MASK	0x8
#define QNOC_QOS_MCTL_URGFWD_EN_SHIFT	3

/* BIMC QoS */
#define M_BKE_REG_BASE(n)		(0x300 + (0x4000 * n))
#define M_BKE_EN_ADDR(n)		(M_BKE_REG_BASE(n))
#define M_BKE_HEALTH_CFG_ADDR(i, n)	(M_BKE_REG_BASE(n) + 0x40 + (0x4 * i))

#define M_BKE_HEALTH_CFG_LIMITCMDS_MASK	0x80000000
#define M_BKE_HEALTH_CFG_AREQPRIO_MASK	0x300
#define M_BKE_HEALTH_CFG_PRIOLVL_MASK	0x3
#define M_BKE_HEALTH_CFG_AREQPRIO_SHIFT	0x8
#define M_BKE_HEALTH_CFG_LIMITCMDS_SHIFT 0x1f

#define M_BKE_EN_EN_BMASK		0x1

/* NoC QoS */
#define NOC_QOS_PRIORITYn_ADDR(n)	(0x8 + (n * 0x1000))
#define NOC_QOS_PRIORITY_P1_MASK	0xc
#define NOC_QOS_PRIORITY_P0_MASK	0x3
#define NOC_QOS_PRIORITY_P1_SHIFT	0x2

#define NOC_QOS_MODEn_ADDR(n)		(0xc + (n * 0x1000))
#define NOC_QOS_MODEn_MASK		0x3

#define NOC_QOS_MODE_FIXED_VAL		0x0
#define NOC_QOS_MODE_BYPASS_VAL		0x2

static int qcom_icc_set_qnoc_qos(struct icc_node *src)
{
	struct icc_provider *provider = src->provider;
	struct qcom_icc_provider *qp = to_qcom_provider(provider);
	struct qcom_icc_node *qn = src->data;
	struct qcom_icc_qos *qos = &qn->qos;
	int rc;

	rc = regmap_update_bits(qp->regmap,
			qp->qos_offset + QNOC_QOS_MCTL_LOWn_ADDR(qos->qos_port),
			QNOC_QOS_MCTL_DFLT_PRIO_MASK,
			qos->areq_prio << QNOC_QOS_MCTL_DFLT_PRIO_SHIFT);
	if (rc)
		return rc;

	return regmap_update_bits(qp->regmap,
			qp->qos_offset + QNOC_QOS_MCTL_LOWn_ADDR(qos->qos_port),
			QNOC_QOS_MCTL_URGFWD_EN_MASK,
			!!qos->urg_fwd_en << QNOC_QOS_MCTL_URGFWD_EN_SHIFT);
}

static int qcom_icc_bimc_set_qos_health(struct qcom_icc_provider *qp,
					struct qcom_icc_qos *qos,
					int regnum)
{
	u32 val;
	u32 mask;

	val = qos->prio_level;
	mask = M_BKE_HEALTH_CFG_PRIOLVL_MASK;

	val |= qos->areq_prio << M_BKE_HEALTH_CFG_AREQPRIO_SHIFT;
	mask |= M_BKE_HEALTH_CFG_AREQPRIO_MASK;

	/* LIMITCMDS is not present on M_BKE_HEALTH_3 */
	if (regnum != 3) {
		val |= qos->limit_commands << M_BKE_HEALTH_CFG_LIMITCMDS_SHIFT;
		mask |= M_BKE_HEALTH_CFG_LIMITCMDS_MASK;
	}

	return regmap_update_bits(qp->regmap,
				  qp->qos_offset + M_BKE_HEALTH_CFG_ADDR(regnum, qos->qos_port),
				  mask, val);
}

static int qcom_icc_set_bimc_qos(struct icc_node *src)
{
	struct qcom_icc_provider *qp;
	struct qcom_icc_node *qn;
	struct icc_provider *provider;
	u32 mode = NOC_QOS_MODE_BYPASS;
	u32 val = 0;
	int i, rc = 0;

	qn = src->data;
	provider = src->provider;
	qp = to_qcom_provider(provider);

	if (qn->qos.qos_mode != NOC_QOS_MODE_INVALID)
		mode = qn->qos.qos_mode;

	/* QoS Priority: The QoS Health parameters are getting considered
	 * only if we are NOT in Bypass Mode.
	 */
	if (mode != NOC_QOS_MODE_BYPASS) {
		for (i = 3; i >= 0; i--) {
			rc = qcom_icc_bimc_set_qos_health(qp,
							  &qn->qos, i);
			if (rc)
				return rc;
		}

		/* Set BKE_EN to 1 when Fixed, Regulator or Limiter Mode */
		val = 1;
	}

	return regmap_update_bits(qp->regmap,
				  qp->qos_offset + M_BKE_EN_ADDR(qn->qos.qos_port),
				  M_BKE_EN_EN_BMASK, val);
}

static int qcom_icc_noc_set_qos_priority(struct qcom_icc_provider *qp,
					 struct qcom_icc_qos *qos)
{
	u32 val;
	int rc;

	/* Must be updated one at a time, P1 first, P0 last */
	val = qos->areq_prio << NOC_QOS_PRIORITY_P1_SHIFT;
	rc = regmap_update_bits(qp->regmap,
				qp->qos_offset + NOC_QOS_PRIORITYn_ADDR(qos->qos_port),
				NOC_QOS_PRIORITY_P1_MASK, val);
	if (rc)
		return rc;

	return regmap_update_bits(qp->regmap,
				  qp->qos_offset + NOC_QOS_PRIORITYn_ADDR(qos->qos_port),
				  NOC_QOS_PRIORITY_P0_MASK, qos->prio_level);
}

static int qcom_icc_set_noc_qos(struct icc_node *src)
{
	struct qcom_icc_provider *qp;
	struct qcom_icc_node *qn;
	struct icc_provider *provider;
	u32 mode = NOC_QOS_MODE_BYPASS_VAL;
	int rc = 0;

	qn = src->data;
	provider = src->provider;
	qp = to_qcom_provider(provider);

	if (qn->qos.qos_port < 0) {
		dev_dbg(src->provider->dev,
			"NoC QoS: Skipping %s: vote aggregated on parent.\n",
			qn->name);
		return 0;
	}

	if (qn->qos.qos_mode == NOC_QOS_MODE_FIXED) {
		dev_dbg(src->provider->dev, "NoC QoS: %s: Set Fixed mode\n", qn->name);
		mode = NOC_QOS_MODE_FIXED_VAL;
		rc = qcom_icc_noc_set_qos_priority(qp, &qn->qos);
		if (rc)
			return rc;
	} else if (qn->qos.qos_mode == NOC_QOS_MODE_BYPASS) {
		dev_dbg(src->provider->dev, "NoC QoS: %s: Set Bypass mode\n", qn->name);
		mode = NOC_QOS_MODE_BYPASS_VAL;
	} else {
		/* How did we get here? */
	}

	return regmap_update_bits(qp->regmap,
				  qp->qos_offset + NOC_QOS_MODEn_ADDR(qn->qos.qos_port),
				  NOC_QOS_MODEn_MASK, mode);
}

static int qcom_icc_qos_set(struct icc_node *node)
{
	struct qcom_icc_provider *qp = to_qcom_provider(node->provider);
	struct qcom_icc_node *qn = node->data;

	dev_dbg(node->provider->dev, "Setting QoS for %s\n", qn->name);

	switch (qp->type) {
	case QCOM_ICC_BIMC:
		return qcom_icc_set_bimc_qos(node);
	case QCOM_ICC_QNOC:
		return qcom_icc_set_qnoc_qos(node);
	default:
		return qcom_icc_set_noc_qos(node);
	}
}

static int qcom_icc_rpm_set(struct qcom_icc_node *qn, u64 sum_bw)
{
	int ret = 0;

	if (qn->qos.ap_owned)
		return 0;

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
			pr_err("qcom_icc_rpm_smd_send slv %d error %d\n",
			       qn->slv_rpm_id, ret);
			return ret;
		}
	}

	return ret;
}

/**
 * qcom_icc_pre_bw_aggregate - cleans up values before re-aggregate requests
 * @node: icc node to operate on
 */
static void qcom_icc_pre_bw_aggregate(struct icc_node *node)
{
	struct qcom_icc_node *qn;
	size_t i;

	qn = node->data;
	for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++) {
		qn->sum_avg[i] = 0;
		qn->max_peak[i] = 0;
	}
}

/**
 * qcom_icc_bw_aggregate - aggregate bw for buckets indicated by tag
 * @node: node to aggregate
 * @tag: tag to indicate which buckets to aggregate
 * @avg_bw: new bw to sum aggregate
 * @peak_bw: new bw to max aggregate
 * @agg_avg: existing aggregate avg bw val
 * @agg_peak: existing aggregate peak bw val
 */
static int qcom_icc_bw_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
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
	}

	*agg_avg += avg_bw;
	*agg_peak = max_t(u32, *agg_peak, peak_bw);
	return 0;
}

/**
 * qcom_icc_bus_aggregate - aggregate bandwidth by traversing all nodes
 * @provider: generic interconnect provider
 * @agg_avg: an array for aggregated average bandwidth of buckets
 * @agg_peak: an array for aggregated peak bandwidth of buckets
 * @max_agg_avg: pointer to max value of aggregated average bandwidth
 */
static void qcom_icc_bus_aggregate(struct icc_provider *provider,
				   u64 *agg_avg, u64 *agg_peak,
				   u64 *max_agg_avg)
{
	struct icc_node *node;
	struct qcom_icc_node *qn;
	u64 sum_avg[QCOM_ICC_NUM_BUCKETS];
	int i;

	/* Initialise aggregate values */
	for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++) {
		agg_avg[i] = 0;
		agg_peak[i] = 0;
	}

	*max_agg_avg = 0;

	/*
	 * Iterate nodes on the interconnect and aggregate bandwidth
	 * requests for every bucket.
	 */
	list_for_each_entry(node, &provider->nodes, node_list) {
		qn = node->data;
		for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++) {
			if (qn->channels)
				sum_avg[i] = div_u64(qn->sum_avg[i], qn->channels);
			else
				sum_avg[i] = qn->sum_avg[i];
			agg_avg[i] += sum_avg[i];
			agg_peak[i] = max_t(u64, agg_peak[i], qn->max_peak[i]);
		}
	}

	/* Find maximum values across all buckets */
	for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++)
		*max_agg_avg = max_t(u64, *max_agg_avg, agg_avg[i]);
}

static int qcom_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct qcom_icc_provider *qp;
	struct qcom_icc_node *src_qn = NULL, *dst_qn = NULL;
	struct icc_provider *provider;
	u64 sum_bw;
	u64 rate;
	u64 agg_avg[QCOM_ICC_NUM_BUCKETS], agg_peak[QCOM_ICC_NUM_BUCKETS];
	u64 max_agg_avg;
	int ret, i;
	int bucket;

	src_qn = src->data;
	if (dst)
		dst_qn = dst->data;
	provider = src->provider;
	qp = to_qcom_provider(provider);

	qcom_icc_bus_aggregate(provider, agg_avg, agg_peak, &max_agg_avg);

	sum_bw = icc_units_to_bps(max_agg_avg);

	ret = qcom_icc_rpm_set(src_qn, sum_bw);
	if (ret)
		return ret;

	if (dst_qn) {
		ret = qcom_icc_rpm_set(dst_qn, sum_bw);
		if (ret)
			return ret;
	}

	for (i = 0; i < qp->num_bus_clks; i++) {
		/*
		 * Use WAKE bucket for active clock, otherwise, use SLEEP bucket
		 * for other clocks.  If a platform doesn't set interconnect
		 * path tags, by default use sleep bucket for all clocks.
		 *
		 * Note, AMC bucket is not supported yet.
		 */
		if (!strcmp(qp->bus_clks[i].id, "bus_a"))
			bucket = QCOM_ICC_BUCKET_WAKE;
		else
			bucket = QCOM_ICC_BUCKET_SLEEP;

		rate = icc_units_to_bps(max(agg_avg[bucket], agg_peak[bucket]));
		do_div(rate, src_qn->buswidth);
		rate = min_t(u64, rate, LONG_MAX);

		if (qp->bus_clk_rate[i] == rate)
			continue;

		ret = clk_set_rate(qp->bus_clks[i].clk, rate);
		if (ret) {
			pr_err("%s clk_set_rate error: %d\n",
			       qp->bus_clks[i].id, ret);
			return ret;
		}
		qp->bus_clk_rate[i] = rate;
	}

	return 0;
}

static const char * const bus_clocks[] = {
	"bus", "bus_a",
};

int qnoc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct qcom_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_node * const *qnodes;
	struct qcom_icc_provider *qp;
	struct icc_node *node;
	size_t num_nodes, i;
	const char * const *cds = NULL;
	int cd_num;
	int ret;

	/* wait for the RPM proxy */
	if (!qcom_icc_rpm_smd_available())
		return -EPROBE_DEFER;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	if (desc->num_intf_clocks) {
		cds = desc->intf_clocks;
		cd_num = desc->num_intf_clocks;
	} else {
		/* 0 intf clocks is perfectly fine */
		cd_num = 0;
	}

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	qp->intf_clks = devm_kcalloc(dev, cd_num, sizeof(*qp->intf_clks), GFP_KERNEL);
	if (!qp->intf_clks)
		return -ENOMEM;

	data = devm_kzalloc(dev, struct_size(data, nodes, num_nodes),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	qp->num_intf_clks = cd_num;
	for (i = 0; i < cd_num; i++)
		qp->intf_clks[i].id = cds[i];

	qp->num_bus_clks = desc->no_clk_scaling ? 0 : NUM_BUS_CLKS;
	for (i = 0; i < qp->num_bus_clks; i++)
		qp->bus_clks[i].id = bus_clocks[i];

	qp->type = desc->type;
	qp->qos_offset = desc->qos_offset;

	if (desc->regmap_cfg) {
		struct resource *res;
		void __iomem *mmio;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			/* Try parent's regmap */
			qp->regmap = dev_get_regmap(dev->parent, NULL);
			if (qp->regmap)
				goto regmap_done;
			return -ENODEV;
		}

		mmio = devm_ioremap_resource(dev, res);
		if (IS_ERR(mmio))
			return PTR_ERR(mmio);

		qp->regmap = devm_regmap_init_mmio(dev, mmio, desc->regmap_cfg);
		if (IS_ERR(qp->regmap)) {
			dev_err(dev, "Cannot regmap interconnect bus resource\n");
			return PTR_ERR(qp->regmap);
		}
	}

regmap_done:
	ret = devm_clk_bulk_get(dev, qp->num_bus_clks, qp->bus_clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(qp->num_bus_clks, qp->bus_clks);
	if (ret)
		return ret;

	ret = devm_clk_bulk_get(dev, qp->num_intf_clks, qp->intf_clks);
	if (ret)
		return ret;

	provider = &qp->provider;
	provider->dev = dev;
	provider->set = qcom_icc_set;
	provider->pre_aggregate = qcom_icc_pre_bw_aggregate;
	provider->aggregate = qcom_icc_bw_aggregate;
	provider->xlate_extended = qcom_icc_xlate_extended;
	provider->data = data;

	icc_provider_init(provider);

	/* If this fails, bus accesses will crash the platform! */
	ret = clk_bulk_prepare_enable(qp->num_intf_clks, qp->intf_clks);
	if (ret)
		return ret;

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err_remove_nodes;
		}

		node->name = qnodes[i]->name;
		node->data = qnodes[i];
		icc_node_add(node, provider);

		for (j = 0; j < qnodes[i]->num_links; j++)
			icc_link_create(node, qnodes[i]->links[j]);

		/* Set QoS registers (we only need to do it once, generally) */
		if (qnodes[i]->qos.ap_owned &&
		    qnodes[i]->qos.qos_mode != NOC_QOS_MODE_INVALID) {
			ret = qcom_icc_qos_set(node);
			if (ret)
				return ret;
		}

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;

	clk_bulk_disable_unprepare(qp->num_intf_clks, qp->intf_clks);

	ret = icc_provider_register(provider);
	if (ret)
		goto err_remove_nodes;

	platform_set_drvdata(pdev, qp);

	/* Populate child NoC devices if any */
	if (of_get_child_count(dev->of_node) > 0) {
		ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
		if (ret)
			goto err_deregister_provider;
	}

	return 0;

err_deregister_provider:
	icc_provider_deregister(provider);
err_remove_nodes:
	icc_nodes_remove(provider);
	clk_bulk_disable_unprepare(qp->num_bus_clks, qp->bus_clks);

	return ret;
}
EXPORT_SYMBOL(qnoc_probe);

int qnoc_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);

	icc_provider_deregister(&qp->provider);
	icc_nodes_remove(&qp->provider);
	clk_bulk_disable_unprepare(qp->num_bus_clks, qp->bus_clks);

	return 0;
}
EXPORT_SYMBOL(qnoc_remove);
