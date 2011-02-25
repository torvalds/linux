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

#ifndef _SCIC_PORT_H_
#define _SCIC_PORT_H_

/**
 * This file contains all of the interface methods that can be called by an SCI
 *    Core user on a SAS or SATA port.
 *
 *
 */


#include "sci_status.h"
#include "intel_sas.h"

struct scic_sds_port;

enum SCIC_PORT_NOT_READY_REASON_CODE {
	SCIC_PORT_NOT_READY_NO_ACTIVE_PHYS,
	SCIC_PORT_NOT_READY_HARD_RESET_REQUESTED,
	SCIC_PORT_NOT_READY_INVALID_PORT_CONFIGURATION,
	SCIC_PORT_NOT_READY_RECONFIGURING,

	SCIC_PORT_NOT_READY_REASON_CODE_MAX
};

/**
 * struct scic_port_end_point_properties - This structure defines the
 *    properties that can be retrieved for each end-point local or remote
 *    (attached) port in the controller.
 *
 *
 */
struct scic_port_end_point_properties {
	/**
	 * This field indicates the SAS address for the associated end
	 * point in the port.
	 */
	struct sci_sas_address sas_address;

	/**
	 * This field indicates the protocols supported by the associated
	 * end-point in the port.
	 */
	struct sci_sas_identify_address_frame_protocols protocols;

};

/**
 * struct scic_port_properties - This structure defines the properties that can
 *    be retrieved for each port in the controller.
 *
 *
 */
struct scic_port_properties {
	/**
	 * This field specifies the logical index of the port (0 relative).
	 */
	u32 index;

	/**
	 * This field indicates the local end-point properties for port.
	 */
	struct scic_port_end_point_properties local;

	/**
	 * This field indicates the remote (attached) end-point properties
	 * for the port.
	 */
	struct scic_port_end_point_properties remote;

	/**
	 * This field specifies the phys contained inside the port.
	 */
	u32 phy_mask;

};

/**
 * scic_port_get_properties() - This method simply returns the properties
 *    regarding the port, such as: physical index, protocols, sas address, etc.
 * @port: this parameter specifies the port for which to retrieve the physical
 *    index.
 * @properties: This parameter specifies the properties structure into which to
 *    copy the requested information.
 *
 * Indicate if the user specified a valid port. SCI_SUCCESS This value is
 * returned if the specified port was valid. SCI_FAILURE_INVALID_PORT This
 * value is returned if the specified port is not valid.  When this value is
 * returned, no data is copied to the properties output parameter.
 */
enum sci_status scic_port_get_properties(
	struct scic_sds_port *port,
	struct scic_port_properties *properties);

/**
 * scic_port_stop() - This method will make the port no longer ready for
 *    operation.  After invoking this method IO operation is not possible.
 * @port: This parameter specifies the port to be stopped.
 *
 * Indicate if the port was successfully stopped. SCI_SUCCESS This value is
 * returned if the port was successfully stopped. SCI_WARNING_ALREADY_IN_STATE
 * This value is returned if the port is already stopped or in the process of
 * stopping. SCI_FAILURE_INVALID_PORT This value is returned if the supplied
 * port is not valid. SCI_FAILURE_INVALID_STATE This value is returned if a
 * stop operation can't be completed due to the state of port.
 */
enum sci_status scic_port_stop(
	struct scic_sds_port *port);

/**
 * scic_port_hard_reset() - This method will request the SCI implementation to
 *    perform a HARD RESET on the SAS Port.  If/When the HARD RESET completes
 *    the SCI user will be notified via an SCI OS callback indicating a direct
 *    attached device was found.
 * @port: a handle corresponding to the SAS port to be hard reset.
 * @reset_timeout: This parameter specifies the number of milliseconds in which
 *    the port reset operation should complete.
 *
 * The SCI User callback in SCIC_USER_CALLBACKS_T will only be called once for
 * each phy in the SAS Port at completion of the hard reset sequence. Return a
 * status indicating whether the hard reset started successfully. SCI_SUCCESS
 * This value is returned if the hard reset operation started successfully.
 */
enum sci_status scic_port_hard_reset(
	struct scic_sds_port *port,
	u32 reset_timeout);

/**
 * scic_port_enable_broadcast_change_notification() - This API method enables
 *    the broadcast change notification from underneath hardware.
 * @port: The port upon which broadcast change notifications (BCN) are to be
 *    enabled.
 *
 */
void scic_port_enable_broadcast_change_notification(
	struct scic_sds_port *port);


#endif  /* _SCIC_PORT_H_ */

