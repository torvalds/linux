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
 * This file contains the isci_module object definition.
 *
 * isci.h
 */

#if !defined(_SCI_MODULE_H_)
#define _SCI_MODULE_H_

/**
 * This file contains the SCI low level driver interface to the SCI and Libsas
 *    Libraries.
 *
 * isci.h
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/bug.h>
#include <scsi/libsas.h>
#include <scsi/scsi.h>

#include "sci_base_controller.h"
#include "scic_controller.h"
#include "host.h"
#include "timers.h"
#include "sci_status.h"
#include "request.h"
#include "events.h"

extern struct kmem_cache *isci_kmem_cache;
extern struct isci_firmware *isci_firmware;

#define ISCI_FW_NAME		"isci/isci_firmware.bin"

#define ISCI_FIRMWARE_MIN_SIZE	149

#define ISCI_FW_IDSIZE		12
#define ISCI_FW_VER_OFS		ISCI_FW_IDSIZE
#define ISCI_FW_SUBVER_OFS	ISCI_FW_VER_OFS + 1
#define ISCI_FW_DATA_OFS	ISCI_FW_SUBVER_OFS + 1

#define ISCI_FW_HDR_PHYMASK	0x1
#define ISCI_FW_HDR_PHYGEN	0x2
#define ISCI_FW_HDR_SASADDR	0x3
#define ISCI_FW_HDR_EOF		0xff

struct isci_firmware {
	const u8 *id;
	u8 version;
	u8 subversion;
	const u32 *phy_masks;
	u8 phy_masks_size;
	const u32 *phy_gens;
	u8 phy_gens_size;
	const u64 *sas_addrs;
	u8 sas_addrs_size;
};

irqreturn_t isci_msix_isr(int vec, void *data);
irqreturn_t isci_intx_isr(int vec, void *data);
irqreturn_t isci_error_isr(int vec, void *data);

bool scic_sds_controller_isr(struct scic_sds_controller *scic);
void scic_sds_controller_completion_handler(struct scic_sds_controller *scic);
bool scic_sds_controller_error_isr(struct scic_sds_controller *scic);
void scic_sds_controller_error_handler(struct scic_sds_controller *scic);

enum sci_status isci_parse_oem_parameters(
	union scic_oem_parameters *oem_params,
	int scu_index,
	struct isci_firmware *fw);

enum sci_status isci_parse_user_parameters(
	union scic_user_parameters *user_params,
	int scu_index,
	struct isci_firmware *fw);

#ifdef ISCI_SLAVE_ALLOC
extern int ISCI_SLAVE_ALLOC(struct scsi_device *scsi_dev);
#endif /* ISCI_SLAVE_ALLOC */

#ifdef ISCI_SLAVE_DESTROY
extern void ISCI_SLAVE_DESTROY(struct scsi_device *scsi_dev);
#endif  /* ISCI_SLAVE_DESTROY */
#endif  /* !defined(_SCI_MODULE_H_) */
