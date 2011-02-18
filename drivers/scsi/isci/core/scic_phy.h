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

#ifndef _SCIC_PHY_H_
#define _SCIC_PHY_H_

/**
 * This file contains all of the interface methods that can be called by an
 *    SCIC user on a phy (SAS or SATA) object.
 *
 *
 */


#include "sci_status.h"

#include "intel_sata.h"
#include "intel_sas.h"

struct scic_sds_phy;
struct scic_sds_port;


enum sas_linkrate sci_phy_linkrate(struct scic_sds_phy *sci_phy);

/**
 * struct scic_phy_properties - This structure defines the properties common to
 *    all phys that can be retrieved.
 *
 *
 */
struct scic_phy_properties {
	/**
	 * This field specifies the port that currently contains the
	 * supplied phy.  This field may be set to NULL
	 * if the phy is not currently contained in a port.
	 */
	struct scic_sds_port *owning_port;

	/**
	 * This field specifies the link rate at which the phy is
	 * currently operating.
	 */
	enum sci_sas_link_rate negotiated_link_rate;

	/**
	 * This field indicates the protocols supported by the phy.
	 */
	struct sci_sas_identify_address_frame_protocols protocols;

	/**
	 * This field specifies the index of the phy in relation to other
	 * phys within the controller.  This index is zero relative.
	 */
	u8 index;

};

/**
 * struct scic_sas_phy_properties - This structure defines the properties,
 *    specific to a SAS phy, that can be retrieved.
 *
 *
 */
struct scic_sas_phy_properties {
	/**
	 * This field delineates the Identify Address Frame received
	 * from the remote end point.
	 */
	struct sci_sas_identify_address_frame received_iaf;

	/**
	 * This field delineates the Phy capabilities structure received
	 * from the remote end point.
	 */
	struct sas_capabilities received_capabilities;

};

/**
 * struct scic_sata_phy_properties - This structure defines the properties,
 *    specific to a SATA phy, that can be retrieved.
 *
 *
 */
struct scic_sata_phy_properties {
	/**
	 * This field delineates the signature FIS received from the
	 * attached target.
	 */
	struct sata_fis_reg_d2h signature_fis;

	/**
	 * This field specifies to the user if a port selector is connected
	 * on the specified phy.
	 */
	bool is_port_selector_present;

};

/**
 * enum scic_phy_counter_id - This enumeration depicts the various pieces of
 *    optional information that can be retrieved for a specific phy.
 *
 *
 */
enum scic_phy_counter_id {
	/**
	 * This PHY information field tracks the number of frames received.
	 */
	SCIC_PHY_COUNTER_RECEIVED_FRAME,

	/**
	 * This PHY information field tracks the number of frames transmitted.
	 */
	SCIC_PHY_COUNTER_TRANSMITTED_FRAME,

	/**
	 * This PHY information field tracks the number of DWORDs received.
	 */
	SCIC_PHY_COUNTER_RECEIVED_FRAME_WORD,

	/**
	 * This PHY information field tracks the number of DWORDs transmitted.
	 */
	SCIC_PHY_COUNTER_TRANSMITTED_FRAME_DWORD,

	/**
	 * This PHY information field tracks the number of times DWORD
	 * synchronization was lost.
	 */
	SCIC_PHY_COUNTER_LOSS_OF_SYNC_ERROR,

	/**
	 * This PHY information field tracks the number of received DWORDs with
	 * running disparity errors.
	 */
	SCIC_PHY_COUNTER_RECEIVED_DISPARITY_ERROR,

	/**
	 * This PHY information field tracks the number of received frames with a
	 * CRC error (not including short or truncated frames).
	 */
	SCIC_PHY_COUNTER_RECEIVED_FRAME_CRC_ERROR,

	/**
	 * This PHY information field tracks the number of DONE (ACK/NAK TIMEOUT)
	 * primitives received.
	 */
	SCIC_PHY_COUNTER_RECEIVED_DONE_ACK_NAK_TIMEOUT,

	/**
	 * This PHY information field tracks the number of DONE (ACK/NAK TIMEOUT)
	 * primitives transmitted.
	 */
	SCIC_PHY_COUNTER_TRANSMITTED_DONE_ACK_NAK_TIMEOUT,

	/**
	 * This PHY information field tracks the number of times the inactivity
	 * timer for connections on the phy has been utilized.
	 */
	SCIC_PHY_COUNTER_INACTIVITY_TIMER_EXPIRED,

	/**
	 * This PHY information field tracks the number of DONE (CREDIT TIMEOUT)
	 * primitives received.
	 */
	SCIC_PHY_COUNTER_RECEIVED_DONE_CREDIT_TIMEOUT,

	/**
	 * This PHY information field tracks the number of DONE (CREDIT TIMEOUT)
	 * primitives transmitted.
	 */
	SCIC_PHY_COUNTER_TRANSMITTED_DONE_CREDIT_TIMEOUT,

	/**
	 * This PHY information field tracks the number of CREDIT BLOCKED
	 * primitives received.
	 * @note Depending on remote device implementation, credit blocks
	 *       may occur regularly.
	 */
	SCIC_PHY_COUNTER_RECEIVED_CREDIT_BLOCKED,

	/**
	 * This PHY information field contains the number of short frames
	 * received.  A short frame is simply a frame smaller then what is
	 * allowed by either the SAS or SATA specification.
	 */
	SCIC_PHY_COUNTER_RECEIVED_SHORT_FRAME,

	/**
	 * This PHY information field contains the number of frames received after
	 * credit has been exhausted.
	 */
	SCIC_PHY_COUNTER_RECEIVED_FRAME_WITHOUT_CREDIT,

	/**
	 * This PHY information field contains the number of frames received after
	 * a DONE has been received.
	 */
	SCIC_PHY_COUNTER_RECEIVED_FRAME_AFTER_DONE,

	/**
	 * This PHY information field contains the number of times the phy
	 * failed to achieve DWORD synchronization during speed negotiation.
	 */
	SCIC_PHY_COUNTER_SN_DWORD_SYNC_ERROR
};


/**
 * scic_sas_phy_get_properties() - This method will enable the user to retrieve
 *    information specific to a SAS phy, such as: the received identify address
 *    frame, received phy capabilities, etc.
 * @phy: this parameter specifies the phy for which to retrieve properties.
 * @properties: This parameter specifies the properties structure into which to
 *    copy the requested information.
 *
 * This method returns an indication as to whether the SAS phy properties were
 * successfully retrieved. SCI_SUCCESS This value is returned if the SAS
 * properties are successfully retrieved. SCI_FAILURE This value is returned if
 * the SAS properties are not successfully retrieved (e.g. It's not a SAS Phy).
 */
enum sci_status scic_sas_phy_get_properties(
	struct scic_sds_phy *phy,
	struct scic_sas_phy_properties *properties);

/**
 * scic_sata_phy_get_properties() - This method will enable the user to
 *    retrieve information specific to a SATA phy, such as: the received
 *    signature FIS, if a port selector is present, etc.
 * @phy: this parameter specifies the phy for which to retrieve properties.
 * @properties: This parameter specifies the properties structure into which to
 *    copy the requested information.
 *
 * This method returns an indication as to whether the SATA phy properties were
 * successfully retrieved. SCI_SUCCESS This value is returned if the SATA
 * properties are successfully retrieved. SCI_FAILURE This value is returned if
 * the SATA properties are not successfully retrieved (e.g. It's not a SATA
 * Phy).
 */
enum sci_status scic_sata_phy_get_properties(
	struct scic_sds_phy *phy,
	struct scic_sata_phy_properties *properties);







#endif  /* _SCIC_PHY_H_ */

