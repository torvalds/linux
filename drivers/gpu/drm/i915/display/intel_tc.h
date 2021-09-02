/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_TC_H__
#define __INTEL_TC_H__

#include <linux/mutex.h>
#include <linux/types.h>

struct intel_digital_port;
struct intel_encoder;

bool intel_tc_port_connected(struct intel_encoder *encoder);
void intel_tc_port_disconnect_phy(struct intel_digital_port *dig_port);

u32 intel_tc_port_get_lane_mask(struct intel_digital_port *dig_port);
u32 intel_tc_port_get_pin_assignment_mask(struct intel_digital_port *dig_port);
int intel_tc_port_fia_max_lane_count(struct intel_digital_port *dig_port);
void intel_tc_port_set_fia_lane_count(struct intel_digital_port *dig_port,
				      int required_lanes);

void intel_tc_port_sanitize(struct intel_digital_port *dig_port);
void intel_tc_port_lock(struct intel_digital_port *dig_port);
void intel_tc_port_unlock(struct intel_digital_port *dig_port);
void intel_tc_port_get_link(struct intel_digital_port *dig_port,
			    int required_lanes);
void intel_tc_port_put_link(struct intel_digital_port *dig_port);
bool intel_tc_port_ref_held(struct intel_digital_port *dig_port);

void intel_tc_port_init(struct intel_digital_port *dig_port, bool is_legacy);

bool intel_tc_cold_requires_aux_pw(struct intel_digital_port *dig_port);

#endif /* __INTEL_TC_H__ */
