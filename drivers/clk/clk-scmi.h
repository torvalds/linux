/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright 2025 NXP
 */

#ifndef __SCMI_CLK_H
#define __SCMI_CLK_H

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/scmi_protocol.h>
#include <linux/types.h>

#define NOT_ATOMIC	false
#define ATOMIC		true

enum scmi_clk_feats {
	SCMI_CLK_ATOMIC_SUPPORTED,
	SCMI_CLK_STATE_CTRL_SUPPORTED,
	SCMI_CLK_RATE_CTRL_SUPPORTED,
	SCMI_CLK_PARENT_CTRL_SUPPORTED,
	SCMI_CLK_DUTY_CYCLE_SUPPORTED,
	SCMI_CLK_EXT_OEM_SSC_SUPPORTED,
	SCMI_CLK_FEATS_COUNT
};

#define SCMI_MAX_CLK_OPS	BIT(SCMI_CLK_FEATS_COUNT)

struct scmi_clk {
	u32 id;
	struct device *dev;
	struct clk_hw hw;
	const struct scmi_clock_info *info;
	const struct scmi_protocol_handle *ph;
	struct clk_parent_data *parent_data;
};

#define to_scmi_clk(clk) container_of(clk, struct scmi_clk, hw)

extern const struct scmi_clk_proto_ops *scmi_proto_clk_ops;

struct scmi_clk_oem {
	int (*query_ext_oem_feats)(const struct scmi_protocol_handle *ph,
				   u32 id, unsigned int *feats_key);
	int (*set_spread_spectrum)(struct clk_hw *hw,
				   const struct clk_spread_spectrum *ss_conf);
};

int scmi_clk_oem_init(struct scmi_device *dev);

#endif
