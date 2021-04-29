/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Linaro Ltd
 */

#ifndef __DRIVERS_INTERCONNECT_QCOM_ICC_RPM_H
#define __DRIVERS_INTERCONNECT_QCOM_ICC_RPM_H

#define RPM_BUS_MASTER_REQ	0x73616d62
#define RPM_BUS_SLAVE_REQ	0x766c7362

#define QCOM_MAX_LINKS 12

#define to_qcom_provider(_provider) \
	container_of(_provider, struct qcom_icc_provider, provider)

/**
 * struct qcom_icc_provider - Qualcomm specific interconnect provider
 * @provider: generic interconnect provider
 * @bus_clks: the clk_bulk_data table of bus clocks
 * @num_clks: the total number of clk_bulk_data entries
 */
struct qcom_icc_provider {
	struct icc_provider provider;
	struct clk_bulk_data *bus_clks;
	int num_clks;
};

/**
 * struct qcom_icc_node - Qualcomm specific interconnect nodes
 * @name: the node name used in debugfs
 * @id: a unique node identifier
 * @links: an array of nodes where we can go next while traversing
 * @num_links: the total number of @links
 * @buswidth: width of the interconnect between a node and the bus (bytes)
 * @mas_rpm_id:	RPM id for devices that are bus masters
 * @slv_rpm_id:	RPM id for devices that are bus slaves
 * @rate: current bus clock rate in Hz
 */
struct qcom_icc_node {
	unsigned char *name;
	u16 id;
	u16 links[QCOM_MAX_LINKS];
	u16 num_links;
	u16 buswidth;
	int mas_rpm_id;
	int slv_rpm_id;
	u64 rate;
};

struct qcom_icc_desc {
	struct qcom_icc_node **nodes;
	size_t num_nodes;
};

#define DEFINE_QNODE(_name, _id, _buswidth, _mas_rpm_id, _slv_rpm_id,	\
		     ...)						\
		static struct qcom_icc_node _name = {			\
		.name = #_name,						\
		.id = _id,						\
		.buswidth = _buswidth,					\
		.mas_rpm_id = _mas_rpm_id,				\
		.slv_rpm_id = _slv_rpm_id,				\
		.num_links = ARRAY_SIZE(((int[]){ __VA_ARGS__ })),	\
		.links = { __VA_ARGS__ },				\
	}


int qnoc_probe(struct platform_device *pdev, size_t cd_size, int cd_num,
	       const struct clk_bulk_data *cd);
int qnoc_remove(struct platform_device *pdev);

#endif
