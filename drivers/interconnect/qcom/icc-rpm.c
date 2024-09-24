// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Linaro Ltd
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <asm/div64.h>
#include <linux/clk.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <dt-bindings/interconnect/qcom,icc.h>

#include "icc-rpm.h"
#include "qnoc-qos-rpm.h"

static int qcom_icc_rpm_smd_send_msg(int ctx, int rsc_type, int rpm_id, u64 val)
{
	int ret;
	struct msm_rpm_kvp rpm_kvp;

	rpm_kvp.length = sizeof(uint64_t);
	rpm_kvp.key = RPM_MASTER_FIELD_BW;
	rpm_kvp.data = (uint8_t *)&val;

	ret = msm_rpm_send_message(ctx, rsc_type, rpm_id, &rpm_kvp, 1);

	return ret;
}

/**
 * qcom_icc_get_bw_stub - initializes the bw values to zero
 * @node: icc node to operate on
 * @avg_bw: initial bw to sum aggregate
 * @peak_bw: initial bw to max aggregate
 */
int qcom_icc_get_bw_stub(struct icc_node *node, u32 *avg, u32 *peak)
{
	*avg = 0;
	*peak = 0;

	return 0;
}
EXPORT_SYMBOL(qcom_icc_get_bw_stub);

/**
 * qcom_icc_rpm_pre_aggregate - cleans up stale values from prior icc_set
 * @node: icc node to operate on
 */
void qcom_icc_rpm_pre_aggregate(struct icc_node *node)
{
	size_t i;
	struct qcom_icc_node *qn;

	qn = node->data;

	for (i = 0; i < RPM_NUM_CXT; i++) {
		qn->sum_avg[i] = 0;
		qn->max_peak[i] = 0;
	}
}
EXPORT_SYMBOL(qcom_icc_rpm_pre_aggregate);

/**
 * qcom_icc_rpm_aggregate - aggregate bw for buckets indicated by tag
 * @node: node to aggregate
 * @tag: tag to indicate which buckets to aggregate
 * @avg_bw: new bw to sum aggregate
 * @peak_bw: new bw to max aggregate
 * @agg_avg: existing aggregate avg bw val
 * @agg_peak: existing aggregate peak bw val
 */
int qcom_icc_rpm_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
		       u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	size_t i;
	struct qcom_icc_node *qn;

	qn = node->data;

	if (tag && !(tag & QCOM_ICC_TAG_SLEEP))
		tag = BIT(RPM_ACTIVE_CXT);
	else
		tag = BIT(RPM_SLEEP_CXT) | BIT(RPM_ACTIVE_CXT);

	for (i = 0; i < RPM_NUM_CXT; i++) {
		if (tag & BIT(i)) {
			qn->sum_avg[i] += avg_bw;
			qn->max_peak[i] = max_t(u32, qn->max_peak[i], peak_bw);
		}
	}

	*agg_avg += avg_bw;
	*agg_peak = max_t(u32, *agg_peak, peak_bw);

	qn->dirty = true;

	return 0;
}
EXPORT_SYMBOL(qcom_icc_rpm_aggregate);

/**
 * qcom_icc_rpm_set - set the constraints based on path
 * @src: source node for the path to set constraints on
 * @dst: destination node for the path to set constraints on
 *
 * Return: 0 on success, or an error code otherwise
 */
int qcom_icc_rpm_set(struct icc_node *src, struct icc_node *dst)
{
	struct qcom_icc_provider *qp;
	struct qcom_icc_node *qn;
	struct icc_node *n, *node;
	struct icc_provider *provider;
	int ret, i;
	int rpm_ctx;
	u64 clk_rate, sum_avg, max_peak;
	u64 bus_clk_rate[RPM_NUM_CXT] = {0, 0};

	if (!src)
		node = dst;
	else
		node = src;

	qp = to_qcom_provider(node->provider);
	qn = node->data;

	if (!qn->dirty)
		return 0;

	provider = node->provider;

	list_for_each_entry(n, &provider->nodes, node_list) {
		qn = n->data;
		for (i = 0; i < RPM_NUM_CXT; i++) {
			sum_avg = icc_units_to_bps(qn->sum_avg[i]);

			sum_avg *= qp->util_factor;
			do_div(sum_avg, DEFAULT_UTIL_FACTOR);

			do_div(sum_avg, qn->channels);
			max_peak = icc_units_to_bps(qn->max_peak[i]);

			clk_rate = max(sum_avg, max_peak);
			do_div(clk_rate, qn->buswidth);

			bus_clk_rate[i] = max(bus_clk_rate[i], clk_rate);

			if (bus_clk_rate[i] > RPM_CLK_MAX_LEVEL)
				bus_clk_rate[i] = RPM_CLK_MAX_LEVEL;
		}
	}

	for (i = 0; i < RPM_NUM_CXT; i++) {
		if (qp->bus_clk_cur_rate[i] != bus_clk_rate[i]) {
			if (qp->keepalive && i == RPM_ACTIVE_CXT) {
				if (qp->init)
					ret = clk_set_rate(qp->bus_clks[i].clk,
							RPM_CLK_MAX_LEVEL);
				else if (bus_clk_rate[i] == 0)
					ret = clk_set_rate(qp->bus_clks[i].clk,
							RPM_CLK_MIN_LEVEL);
				else
					ret = clk_set_rate(qp->bus_clks[i].clk,
							bus_clk_rate[i]);
			} else {
				ret = clk_set_rate(qp->bus_clks[i].clk,
							bus_clk_rate[i]);
			}

			if (ret) {
				pr_err("%s clk_set_rate error: %d\n",
					qp->bus_clks[i].id, ret);
				return ret;
			}

			qp->bus_clk_cur_rate[i] = bus_clk_rate[i];
		}
	}

	list_for_each_entry(n, &provider->nodes, node_list) {
		qn = n->data;
		if (!qn->dirty)
			continue;

		qn->dirty = false;
		if ((qn->mas_rpm_id == -1) && (qn->slv_rpm_id == -1))
			continue;

		/* send bandwidth request message to the RPM processor */
		for (i = 0; i < RPM_NUM_CXT; i++) {
			if (qn->last_sum_avg[i] != qn->sum_avg[i]) {
				rpm_ctx = (i == RPM_SLEEP_CXT) ?
					RPM_SLEEP_SET : RPM_ACTIVE_SET;

				sum_avg = icc_units_to_bps(qn->sum_avg[i]);

				if (qn->mas_rpm_id != -1) {
					ret = qcom_icc_rpm_smd_send_msg(
						rpm_ctx,
						RPM_BUS_MASTER_REQ,
						qn->mas_rpm_id,
						sum_avg);

					if (ret) {
						pr_err("qcom_icc_rpm_smd_send_msg mas %d error %d\n",
							qn->mas_rpm_id, ret);
						return ret;
					}
				}

				if (qn->slv_rpm_id != -1) {
					ret = qcom_icc_rpm_smd_send_msg(
						rpm_ctx,
						RPM_BUS_SLAVE_REQ,
						qn->slv_rpm_id,
						sum_avg);

					if (ret) {
						pr_err("qcom_icc_rpm_smd_send_msg slv %d error %d\n",
							qn->slv_rpm_id, ret);
						return ret;
					}
				}

				qn->last_sum_avg[i] = qn->sum_avg[i];
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(qcom_icc_rpm_set);

MODULE_LICENSE("GPL");
