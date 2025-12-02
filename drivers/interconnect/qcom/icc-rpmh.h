/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __DRIVERS_INTERCONNECT_QCOM_ICC_RPMH_H__
#define __DRIVERS_INTERCONNECT_QCOM_ICC_RPMH_H__

#include <dt-bindings/interconnect/qcom,icc.h>
#include <linux/regmap.h>

#define to_qcom_provider(_provider) \
	container_of(_provider, struct qcom_icc_provider, provider)

/**
 * struct qcom_icc_provider - Qualcomm specific interconnect provider
 * @provider: generic interconnect provider
 * @dev: reference to the NoC device
 * @bcms: list of bcms that maps to the provider
 * @num_bcms: number of @bcms
 * @voter: bcm voter targeted by this provider
 * @nodes: list of icc nodes that maps to the provider
 * @num_nodes: number of @nodes
 * @regmap: used for QoS, register access
 * @clks : clks required for register access
 * @num_clks: number of @clks
 */
struct qcom_icc_provider {
	struct icc_provider provider;
	struct device *dev;
	struct qcom_icc_bcm * const *bcms;
	size_t num_bcms;
	struct bcm_voter *voter;
	struct qcom_icc_node * const *nodes;
	size_t num_nodes;
	struct regmap *regmap;
	struct clk_bulk_data *clks;
	int num_clks;
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

#define MAX_PORTS		4

/**
 * struct qcom_icc_qosbox - Qualcomm specific QoS config
 * @prio: priority value assigned to requests on the node
 * @urg_fwd: whether to forward the urgency promotion issued by master
 * (endpoint), or discard
 * @prio_fwd_disable: whether to forward the priority driven by master, or
 * override by @prio
 * @num_ports: number of @ports
 * @port_offsets: qos register offsets
 */
struct qcom_icc_qosbox {
	const u32 prio;
	const bool urg_fwd;
	const bool prio_fwd_disable;
	const u32 num_ports;
	const u32 port_offsets[MAX_PORTS];
};

#define MAX_LINKS		128
#define MAX_BCMS		64
#define MAX_BCM_PER_NODE	3
#define MAX_VCD			10

/**
 * struct qcom_icc_node - Qualcomm specific interconnect nodes
 * @name: the node name used in debugfs
 * @links: an array of nodes where we can go next while traversing
 * @id: a unique node identifier
 * @link_nodes: links associated with this node
 * @node: icc_node associated with this node
 * @num_links: the total number of @links
 * @channels: num of channels at this node
 * @buswidth: width of the interconnect between a node and the bus
 * @sum_avg: current sum aggregate value of all avg bw requests
 * @max_peak: current max aggregate value of all peak bw requests
 * @bcms: list of bcms associated with this logical node
 * @num_bcms: num of @bcms
 * @qosbox: QoS config data associated with node
 */
struct qcom_icc_node {
	const char *name;
	u16 links[MAX_LINKS];
	u16 id;
	struct qcom_icc_node **link_nodes;
	struct icc_node *node;
	u16 num_links;
	u16 channels;
	u16 buswidth;
	u64 sum_avg[QCOM_ICC_NUM_BUCKETS];
	u64 max_peak[QCOM_ICC_NUM_BUCKETS];
	struct qcom_icc_bcm *bcms[MAX_BCM_PER_NODE];
	size_t num_bcms;
	const struct qcom_icc_qosbox *qosbox;
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
 * @dirty: flag used to indicate whether the bcm needs to be committed
 * @keepalive: flag used to indicate whether a keepalive is required
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
	bool dirty;
	bool keepalive;
	struct bcm_db aux_data;
	struct list_head list;
	struct list_head ws_list;
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
	bool qos_requires_clocks;
	bool alloc_dyn_id;
};

int qcom_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
		       u32 peak_bw, u32 *agg_avg, u32 *agg_peak);
int qcom_icc_set(struct icc_node *src, struct icc_node *dst);
int qcom_icc_bcm_init(struct qcom_icc_bcm *bcm, struct device *dev);
void qcom_icc_pre_aggregate(struct icc_node *node);
int qcom_icc_rpmh_probe(struct platform_device *pdev);
void qcom_icc_rpmh_remove(struct platform_device *pdev);

#endif
