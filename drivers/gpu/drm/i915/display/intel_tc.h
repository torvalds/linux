/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_TC_H__
#define __INTEL_TC_H__

#include <linux/types.h>
#include <linux/mutex.h>
#include "intel_drv.h"

void icl_tc_phy_disconnect(struct intel_digital_port *dig_port);

bool intel_tc_port_connected(struct intel_digital_port *dig_port);
u32 intel_tc_port_get_lane_mask(struct intel_digital_port *dig_port);
int intel_tc_port_fia_max_lane_count(struct intel_digital_port *dig_port);

void intel_tc_port_sanitize(struct intel_digital_port *dig_port);
void intel_tc_port_lock(struct intel_digital_port *dig_port);
void intel_tc_port_unlock(struct intel_digital_port *dig_port);
void intel_tc_port_get_link(struct intel_digital_port *dig_port,
			    int required_lanes);
void intel_tc_port_put_link(struct intel_digital_port *dig_port);

static inline int intel_tc_port_ref_held(struct intel_digital_port *dig_port)
{
	return mutex_is_locked(&dig_port->tc_lock) ||
	       dig_port->tc_link_refcount;
}

void intel_tc_port_init(struct intel_digital_port *dig_port, bool is_legacy);

#endif /* __INTEL_TC_H__ */
