/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_TC_H__
#define __INTEL_TC_H__

#include <linux/types.h>

struct intel_digital_port;

void icl_tc_phy_disconnect(struct intel_digital_port *dig_port);

bool intel_tc_port_connected(struct intel_digital_port *dig_port);
int intel_tc_port_fia_max_lane_count(struct intel_digital_port *dig_port);

#endif /* __INTEL_TC_H__ */
