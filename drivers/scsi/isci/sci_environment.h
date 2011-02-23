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

#ifndef _SCI_ENVIRONMENT_H_
#define _SCI_ENVIRONMENT_H_

#include "isci.h"

struct scic_sds_controller;
struct scic_sds_phy;
struct scic_sds_port;
struct scic_sds_remote_device;

static inline struct device *scic_to_dev(struct scic_sds_controller *scic)
{
	struct isci_host *isci_host = sci_object_get_association(scic);

	return &isci_host->pdev->dev;
}

static inline struct device *sciphy_to_dev(struct scic_sds_phy *sci_phy)
{
	struct isci_phy *iphy = sci_object_get_association(sci_phy);

	if (!iphy || !iphy->isci_port || !iphy->isci_port->isci_host)
		return NULL;

	return &iphy->isci_port->isci_host->pdev->dev;
}

static inline struct device *sciport_to_dev(struct scic_sds_port *sci_port)
{
	struct isci_port *iport = sci_object_get_association(sci_port);

	if (!iport || !iport->isci_host)
		return NULL;

	return &iport->isci_host->pdev->dev;
}

static inline struct device *scirdev_to_dev(struct scic_sds_remote_device *sci_dev)
{
	struct isci_remote_device *idev = sci_object_get_association(sci_dev);

	if (!idev || !idev->isci_port || !idev->isci_port->isci_host)
		return NULL;

	return &idev->isci_port->isci_host->pdev->dev;
}

enum {
	ISCI_SI_REVA0,
	ISCI_SI_REVA2,
	ISCI_SI_REVB0,
};

extern int isci_si_rev;

static inline bool is_a0(void)
{
	return isci_si_rev == ISCI_SI_REVA0;
}

static inline bool is_a2(void)
{
	return isci_si_rev == ISCI_SI_REVA2;
}

static inline bool is_b0(void)
{
	return isci_si_rev > ISCI_SI_REVA2;
}

#endif
