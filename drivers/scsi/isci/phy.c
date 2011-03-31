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

#include "isci.h"
#include "phy.h"
#include "scic_port.h"
#include "scic_config_parameters.h"

struct scic_sds_phy;
extern enum sci_status scic_sds_phy_start(struct scic_sds_phy *sci_phy);
extern enum sci_status scic_sds_phy_stop(struct scic_sds_phy *sci_phy);

/**
 * isci_phy_init() - This function is called by the probe function to
 *    initialize the phy objects. This func assumes that the isci_port objects
 *    associated with the SCU have been initialized.
 * @isci_phy: This parameter specifies the isci_phy object to initialize
 * @isci_host: This parameter specifies the parent SCU host object for this
 *    isci_phy
 * @index: This parameter specifies which SCU phy associates with this
 *    isci_phy. Generally, SCU phy 0 relates isci_phy 0, etc.
 *
 */
void isci_phy_init(
	struct isci_phy *phy,
	struct isci_host *isci_host,
	int index)
{
	struct scic_sds_controller *scic = isci_host->core_controller;
	struct scic_sds_phy *scic_phy;
	union scic_oem_parameters oem;
	enum sci_status status = SCI_SUCCESS;
	u64 sas_addr;

	/*--------------- SCU_Phy Initialization Stuff -----------------------*/

	status = scic_controller_get_phy_handle(scic, index, &scic_phy);
	if (status == SCI_SUCCESS) {
		sci_object_set_association(scic_phy, (void *)phy);
		phy->sci_phy_handle = scic_phy;
	} else
		dev_err(&isci_host->pdev->dev,
			"failed scic_controller_get_phy_handle\n");

	scic_oem_parameters_get(scic, &oem);
	sas_addr = oem.sds1.phys[index].sas_address.high;
	sas_addr <<= 32;
	sas_addr |= oem.sds1.phys[index].sas_address.low;
	swab64s(&sas_addr);

	memcpy(phy->sas_addr, &sas_addr, sizeof(sas_addr));

	phy->isci_port = NULL;
	phy->sas_phy.enabled = 0;
	phy->sas_phy.id = index;
	phy->sas_phy.sas_addr = &phy->sas_addr[0];
	phy->sas_phy.frame_rcvd = (u8 *)&phy->frame_rcvd;
	phy->sas_phy.ha = &isci_host->sas_ha;
	phy->sas_phy.lldd_phy = phy;
	phy->sas_phy.enabled = 1;
	phy->sas_phy.class = SAS;
	phy->sas_phy.iproto = SAS_PROTOCOL_ALL;
	phy->sas_phy.tproto = 0;
	phy->sas_phy.type = PHY_TYPE_PHYSICAL;
	phy->sas_phy.role = PHY_ROLE_INITIATOR;
	phy->sas_phy.oob_mode = OOB_NOT_CONNECTED;
	phy->sas_phy.linkrate = SAS_LINK_RATE_UNKNOWN;
	memset((u8 *)&phy->frame_rcvd, 0, sizeof(phy->frame_rcvd));
}


/**
 * isci_phy_control() - This function is one of the SAS Domain Template
 *    functions. This is a phy management function.
 * @phy: This parameter specifies the sphy being controlled.
 * @func: This parameter specifies the phy control function being invoked.
 * @buf: This parameter is specific to the phy function being invoked.
 *
 * status, zero indicates success.
 */
int isci_phy_control(struct asd_sas_phy *sas_phy,
		     enum phy_func func,
		     void *buf)
{
	int ret = 0;
	struct isci_phy *iphy = sas_phy->lldd_phy;
	struct isci_port *iport = iphy->isci_port;
	struct isci_host *ihost = sas_phy->ha->lldd_ha;
	unsigned long flags;

	dev_dbg(&ihost->pdev->dev,
		"%s: phy %p; func %d; buf %p; isci phy %p, port %p\n",
		__func__, sas_phy, func, buf, iphy, iport);

	switch (func) {
	case PHY_FUNC_DISABLE:
		spin_lock_irqsave(&ihost->scic_lock, flags);
		scic_sds_phy_stop(iphy->sci_phy_handle);
		spin_unlock_irqrestore(&ihost->scic_lock, flags);
		break;

	case PHY_FUNC_LINK_RESET:
		spin_lock_irqsave(&ihost->scic_lock, flags);
		scic_sds_phy_stop(iphy->sci_phy_handle);
		scic_sds_phy_start(iphy->sci_phy_handle);
		spin_unlock_irqrestore(&ihost->scic_lock, flags);
		break;

	case PHY_FUNC_HARD_RESET:
		if (!iport)
			return -ENODEV;

		/* Perform the port reset. */
		ret = isci_port_perform_hard_reset(ihost, iport, iphy);

		break;

	default:
		dev_dbg(&ihost->pdev->dev,
			   "%s: phy %p; func %d NOT IMPLEMENTED!\n",
			   __func__, sas_phy, func);
		ret = -ENOSYS;
		break;
	}
	return ret;
}
