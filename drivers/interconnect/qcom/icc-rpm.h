/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Linaro Ltd
 */

#ifndef __DRIVERS_INTERCONNECT_QCOM_ICC_RPM_H
#define __DRIVERS_INTERCONNECT_QCOM_ICC_RPM_H

#include <linux/soc/qcom/smd-rpm.h>

#include <dt-bindings/interconnect/qcom,icc.h>
#include <linux/clk.h>
#include <linux/interconnect-provider.h>
#include <linux/platform_device.h>

#define RPM_BUS_MASTER_REQ	0x73616d62
#define RPM_BUS_SLAVE_REQ	0x766c7362

#define to_qcom_provider(_provider) \
	container_of(_provider, struct qcom_icc_provider, provider)

enum qcom_icc_type {
	QCOM_ICC_NOC,
	QCOM_ICC_BIMC,
	QCOM_ICC_QNOC,
};

/**
 * struct rpm_clk_resource - RPM bus clock resource
 * @resource_type: RPM resource type of the clock resource
 * @clock_id: index of the clock resource of a specific resource type
 * @branch: whether the resource represents a branch clock
*/
struct rpm_clk_resource {
	u32 resource_type;
	u32 clock_id;
	bool branch;
};

/**
 * struct qcom_icc_provider - Qualcomm specific interconnect provider
 * @provider: generic interconnect provider
 * @num_intf_clks: the total number of intf_clks clk_bulk_data entries
 * @type: the ICC provider type
 * @regmap: regmap for QoS registers read/write access
 * @qos_offset: offset to QoS registers
 * @bus_clk_rate: bus clock rate in Hz
 * @bus_clk_desc: a pointer to a rpm_clk_resource description of bus clocks
 * @bus_clk: a pointer to a HLOS-owned bus clock
 * @intf_clks: a clk_bulk_data array of interface clocks
 * @keep_alive: whether to always keep a minimum vote on the bus clocks
 * @is_on: whether the bus is powered on
 */
struct qcom_icc_provider {
	struct icc_provider provider;
	int num_intf_clks;
	enum qcom_icc_type type;
	struct regmap *regmap;
	unsigned int qos_offset;
	u32 bus_clk_rate[QCOM_SMD_RPM_STATE_NUM];
	const struct rpm_clk_resource *bus_clk_desc;
	struct clk *bus_clk;
	struct clk_bulk_data *intf_clks;
	bool keep_alive;
	bool is_on;
};

/**
 * struct qcom_icc_qos - Qualcomm specific interconnect QoS parameters
 * @areq_prio: node requests priority
 * @prio_level: priority level for bus communication
 * @limit_commands: activate/deactivate limiter mode during runtime
 * @ap_owned: indicates if the node is owned by the AP or by the RPM
 * @qos_mode: default qos mode for this node
 * @qos_port: qos port number for finding qos registers of this node
 * @urg_fwd_en: enable urgent forwarding
 */
struct qcom_icc_qos {
	u32 areq_prio;
	u32 prio_level;
	bool limit_commands;
	bool ap_owned;
	int qos_mode;
	int qos_port;
	bool urg_fwd_en;
};

/**
 * struct qcom_icc_node - Qualcomm specific interconnect nodes
 * @name: the node name used in debugfs
 * @id: a unique node identifier
 * @links: an array of nodes where we can go next while traversing
 * @num_links: the total number of @links
 * @channels: number of channels at this node (e.g. DDR channels)
 * @buswidth: width of the interconnect between a node and the bus (bytes)
 * @sum_avg: current sum aggregate value of all avg bw requests
 * @max_peak: current max aggregate value of all peak bw requests
 * @mas_rpm_id:	RPM id for devices that are bus masters
 * @slv_rpm_id:	RPM id for devices that are bus slaves
 * @qos: NoC QoS setting parameters
 */
struct qcom_icc_node {
	unsigned char *name;
	u16 id;
	const u16 *links;
	u16 num_links;
	u16 channels;
	u16 buswidth;
	u64 sum_avg[QCOM_ICC_NUM_BUCKETS];
	u64 max_peak[QCOM_ICC_NUM_BUCKETS];
	int mas_rpm_id;
	int slv_rpm_id;
	struct qcom_icc_qos qos;
};

struct qcom_icc_desc {
	struct qcom_icc_node * const *nodes;
	size_t num_nodes;
	const struct rpm_clk_resource *bus_clk_desc;
	const char * const *intf_clocks;
	size_t num_intf_clocks;
	bool keep_alive;
	enum qcom_icc_type type;
	const struct regmap_config *regmap_cfg;
	unsigned int qos_offset;
};

/* Valid for all bus types */
enum qos_mode {
	NOC_QOS_MODE_INVALID = 0,
	NOC_QOS_MODE_FIXED,
	NOC_QOS_MODE_BYPASS,
};

extern const struct rpm_clk_resource aggre1_clk;
extern const struct rpm_clk_resource aggre2_clk;
extern const struct rpm_clk_resource bimc_clk;
extern const struct rpm_clk_resource bus_0_clk;
extern const struct rpm_clk_resource bus_1_clk;
extern const struct rpm_clk_resource bus_2_clk;
extern const struct rpm_clk_resource mmaxi_0_clk;
extern const struct rpm_clk_resource mmaxi_1_clk;
extern const struct rpm_clk_resource qup_clk;

extern const struct rpm_clk_resource aggre1_branch_clk;
extern const struct rpm_clk_resource aggre2_branch_clk;

int qnoc_probe(struct platform_device *pdev);
int qnoc_remove(struct platform_device *pdev);

bool qcom_icc_rpm_smd_available(void);
int qcom_icc_rpm_smd_send(int ctx, int rsc_type, int id, u32 val);
int qcom_icc_rpm_set_bus_rate(const struct rpm_clk_resource *clk, int ctx, u32 rate);

#endif
