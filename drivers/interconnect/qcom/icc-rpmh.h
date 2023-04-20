/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __DRIVERS_INTERCONNECT_QCOM_ICC_RPMH_H__
#define __DRIVERS_INTERCONNECT_QCOM_ICC_RPMH_H__

#include <dt-bindings/interconnect/qcom,icc.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>

#include <soc/qcom/crm.h>

#define to_qcom_provider(_provider) \
	container_of(_provider, struct qcom_icc_provider, provider)

/**
 * struct qcom_icc_provider - Qualcomm specific interconnect provider
 * @provider: generic interconnect provider
 * @dev: reference to the NoC device
 * @bcms: list of bcms that maps to the provider
 * @num_bcms: number of @bcms
 * @voter: bcm voter targeted by this provider
 */
struct qcom_icc_provider {
	struct icc_provider provider;
	struct device *dev;
	struct qcom_icc_bcm * const *bcms;
	size_t num_bcms;
	struct qcom_icc_node * const *nodes;
	size_t num_nodes;
	struct list_head probe_list;
	struct regmap *regmap;
	struct clk_bulk_data *clks;
	int num_clks;
	struct bcm_voter **voters;
	size_t num_voters;
	bool stub;
	bool skip_qos;
};

/**
 * struct bcm_db - Auxiliary data pertaining to each Bus Clock Manager (BCM)
 * @unit: divisor used to convert bytes/sec bw value to an RPMh msg
 * @width: multiplier used to convert bytes/sec bw value to an RPMh msg
 * @vcd: virtual clock domain that this bcm belongs to
 * @reserved: reserved field
 */
struct bcm_db {
	__le32 unit;
	__le16 width;
	u8 vcd;
	u8 reserved;
};

#define MAX_LINKS		128
#define MAX_BCMS		64
#define MAX_BCM_PER_NODE	3
#define MAX_VCD			10

struct qcom_icc_crm_voter {
	const char *name;
	const struct device *dev;
	enum crm_drv_type client_type;
	u32 client_idx;
	u32 pwr_states;
};

/**
 * struct qcom_icc_node - Qualcomm specific interconnect nodes
 * @name: the node name used in debugfs
 * @links: an array of nodes where we can go next while traversing
 * @id: a unique node identifier
 * @num_links: the total number of @links
 * @channels: num of channels at this node
 * @buswidth: width of the interconnect between a node and the bus
 * @sum_avg: current sum aggregate value of all avg bw requests
 * @max_peak: current max aggregate value of all peak bw requests
 * @perf_mode: current OR aggregate value of all QCOM_ICC_TAG_PERF_MODE votes
 * @bcms: list of bcms associated with this logical node
 * @num_bcms: num of @bcms
 * @clk: the local clock at this node
 * @clk_name: the local clock name at this node
 * @toggle_clk: flag used to indicate whether local clock can be enabled/disabled
 * @clk_enabled: flag used to indicate whether local clock have been enabled
 * @bw_scale_numerator: the numerator of the bandwidth scale factor
 * @bw_scale_denominator: the denominator of the bandwidth scale factor
 * @disabled : flag used to indicate state of icc node
 */
struct qcom_icc_node {
	const char *name;
	u16 links[MAX_LINKS];
	u16 id;
	u16 num_links;
	u16 channels;
	u16 buswidth;
	u64 sum_avg[QCOM_ICC_NUM_BUCKETS];
	u64 max_peak[QCOM_ICC_NUM_BUCKETS];
	bool perf_mode[QCOM_ICC_NUM_BUCKETS];
	struct qcom_icc_bcm *bcms[MAX_BCM_PER_NODE];
	size_t num_bcms;
	struct regmap *regmap;
	struct qcom_icc_qosbox *qosbox;
	const struct qcom_icc_noc_ops *noc_ops;
	struct clk *clk;
	const char *clk_name;
	bool toggle_clk;
	bool clk_enabled;
	u16 bw_scale_numerator;
	u16 bw_scale_denominator;
	bool disabled;
};

/**
 * struct qcom_icc_bcm - Qualcomm specific hardware accelerator nodes
 * known as Bus Clock Manager (BCM)
 * @name: the bcm node name used to fetch BCM data from command db
 * @type: latency or bandwidth bcm
 * @addr: address offsets used when voting to RPMH
 * @vote_x: aggregated threshold values, represents sum_bw when @type is bw bcm
 * @vote_y: aggregated threshold values, represents peak_bw when @type is bw bcm
 * @vote_scale: scaling factor for vote_x and vote_y
 * @enable_mask: optional mask to send as vote instead of vote_x/vote_y
 * @perf_mode_mask: mask to OR with enable_mask when QCOM_ICC_TAG_PERF_MODE is set
 * @dirty: flag used to indicate whether the bcm needs to be committed
 * @keepalive: flag used to indicate whether a keepalive is required
 * @keepalive_early: keepalive only prior to sync-state
 * @qos_proxy: flag used to indicate whether a proxy vote needed as part of
 * qos configuration
 * @disabled: flag used to indicate state of bcm node
 * @aux_data: auxiliary data used when calculating threshold values and
 * communicating with RPMh
 * @list: used to link to other bcms when compiling lists for commit
 * @ws_list: used to keep track of bcms that may transition between wake/sleep
 * @num_nodes: total number of @num_nodes
 * @nodes: list of qcom_icc_nodes that this BCM encapsulates
 */
struct qcom_icc_bcm {
	const char *name;
	u32 type;
	u32 addr;
	u64 vote_x[QCOM_ICC_NUM_BUCKETS];
	u64 vote_y[QCOM_ICC_NUM_BUCKETS];
	u64 vote_scale;
	u32 enable_mask;
	u32 perf_mode_mask;
	bool dirty;
	bool keepalive;
	bool keepalive_early;
	bool qos_proxy;
	bool disabled;
	struct bcm_db aux_data;
	struct list_head list;
	struct list_head ws_list;
	int voter_idx;
	u8 crm_node;
	size_t num_nodes;
	struct qcom_icc_node *nodes[];
};

struct qcom_icc_fabric {
	struct qcom_icc_node **nodes;
	size_t num_nodes;
};

struct qcom_icc_desc {
	const struct regmap_config *config;
	struct qcom_icc_node * const *nodes;
	size_t num_nodes;
	struct qcom_icc_bcm * const *bcms;
	size_t num_bcms;
	char **voters;
	size_t num_voters;
};

#define DEFINE_QNODE(_name, _id, _channels, _buswidth, ...)		\
		static struct qcom_icc_node _name = {			\
		.id = _id,						\
		.name = #_name,						\
		.channels = _channels,					\
		.buswidth = _buswidth,					\
		.num_links = ARRAY_SIZE(((int[]){ __VA_ARGS__ })),	\
		.links = { __VA_ARGS__ },				\
	}

int qcom_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
		       u32 peak_bw, u32 *agg_avg, u32 *agg_peak);
int qcom_icc_aggregate_stub(struct icc_node *node, u32 tag, u32 avg_bw,
			    u32 peak_bw, u32 *agg_avg, u32 *agg_peak);
int qcom_icc_set(struct icc_node *src, struct icc_node *dst);
int qcom_icc_set_stub(struct icc_node *src, struct icc_node *dst);
int qcom_icc_bcm_init(struct qcom_icc_provider *qp, struct qcom_icc_bcm *bcm, struct device *dev);
void qcom_icc_pre_aggregate(struct icc_node *node);
int qcom_icc_rpmh_probe(struct platform_device *pdev);
int qcom_icc_rpmh_remove(struct platform_device *pdev);
void qcom_icc_rpmh_sync_state(struct device *dev);
int qcom_icc_get_bw_stub(struct icc_node *node, u32 *avg, u32 *peak);
int qcom_icc_rpmh_configure_qos(struct qcom_icc_provider *qp);
#endif
