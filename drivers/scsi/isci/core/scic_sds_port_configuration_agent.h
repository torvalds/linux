/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SCIC_SDS_PORT_CONFIGURATION_AGENT_H_
#define _SCIC_SDS_PORT_CONFIGURATION_AGENT_H_

/**
 * This file contains the structures, constants and prototypes used for the
 *    core controller automatic port configuration engine.
 *
 *
 */

#include "scic_sds_port.h"
#include "scic_sds_phy.h"

struct scic_sds_controller;
struct scic_sds_port_configuration_agent;
struct scic_sds_port;
struct scic_sds_phy;

typedef void (*scic_sds_port_configuration_agent_phy_handler_t)(
	struct scic_sds_controller *,
	struct scic_sds_port_configuration_agent *,
	struct scic_sds_port *,
	struct scic_sds_phy *
	);

struct SCIC_SDS_PORT_RANGE {
	u8 min_index;
	u8 max_index;
};

struct scic_sds_port_configuration_agent {
	u16 phy_configured_mask;
	u16 phy_ready_mask;

	struct SCIC_SDS_PORT_RANGE phy_valid_port_range[SCI_MAX_PHYS];

	bool timer_pending;

	scic_sds_port_configuration_agent_phy_handler_t link_up_handler;
	scic_sds_port_configuration_agent_phy_handler_t link_down_handler;

	void *timer;

};

void scic_sds_port_configuration_agent_construct(
	struct scic_sds_port_configuration_agent *port_agent);

enum sci_status scic_sds_port_configuration_agent_initialize(
	struct scic_sds_controller *controller,
	struct scic_sds_port_configuration_agent *port_agent);

#endif /* _SCIC_SDS_PORT_CONFIGURATION_AGENT_H_ */
