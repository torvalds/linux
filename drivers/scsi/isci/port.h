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

/**
 * This file contains the isci_port object definition.
 *
 * port.h
 */

#if !defined(_ISCI_PORT_H_)
#define _ISCI_PORT_H_

struct isci_phy;
struct isci_host;


enum isci_status {
	isci_freed        = 0x00,
	isci_starting     = 0x01,
	isci_ready        = 0x02,
	isci_ready_for_io = 0x03,
	isci_stopping     = 0x04,
	isci_stopped      = 0x05,
};

/**
 * struct isci_port - This class represents the port object used to internally
 *    represent libsas port objects. It also keeps a list of remote device
 *    objects.
 *
 *
 */
struct isci_port {

	struct scic_sds_port *sci_port_handle;

	enum isci_status status;
	struct isci_host *isci_host;
	struct asd_sas_port sas_port;
	struct list_head remote_dev_list;
	spinlock_t state_lock;
	struct list_head domain_dev_list;
	struct completion start_complete;
	struct completion hard_reset_complete;
	enum sci_status hard_reset_status;
};

#define to_isci_port(p)	\
	container_of(p, struct isci_port, sas_port);

enum isci_status isci_port_get_state(
	struct isci_port *isci_port);



void isci_port_formed(
	struct asd_sas_phy *);

void isci_port_deformed(
	struct asd_sas_phy *);

void isci_port_bc_change_received(
	struct isci_host *isci_host,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy);

void isci_port_link_up(
	struct isci_host *isci_host,
	struct scic_sds_port *port,
	struct scic_sds_phy *phy);

void isci_port_link_down(
	struct isci_host *isci_host,
	struct isci_phy *isci_phy,
	struct isci_port *port);

void isci_port_ready(
	struct isci_host *isci_host,
	struct isci_port *isci_port);

void isci_port_not_ready(
	struct isci_host *isci_host,
	struct isci_port *port);

void isci_port_init(
	struct isci_port *port,
	struct isci_host *host,
	int index);

void isci_port_hard_reset_complete(
	struct isci_port *isci_port,
	enum sci_status completion_status);

int isci_port_perform_hard_reset(struct isci_host *ihost, struct isci_port *iport,
				 struct isci_phy *iphy);

void isci_port_invalid_link_up(
		struct scic_sds_controller *scic,
		struct scic_sds_port *sci_port,
		struct scic_sds_phy *phy);

void isci_port_stop_complete(
		struct scic_sds_controller *scic,
		struct scic_sds_port *sci_port,
		enum sci_status completion_status);

#endif /* !defined(_ISCI_PORT_H_) */

