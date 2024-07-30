// SPDX-License-Identifier: GPL-2.0-only
/*
 * sd_dif.c - SCSI Data Integrity Field
 *
 * Copyright (C) 2007, 2008 Oracle Corporation
 * Written by: Martin K. Petersen <martin.petersen@oracle.com>
 */

#include <linux/blk-integrity.h>
#include <linux/t10-pi.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsicam.h>

#include "sd.h"

/*
 * Configure exchange of protection information between OS and HBA.
 */
void sd_dif_config_host(struct scsi_disk *sdkp, struct queue_limits *lim)
{
	struct scsi_device *sdp = sdkp->device;
	u8 type = sdkp->protection_type;
	struct blk_integrity *bi = &lim->integrity;
	int dif, dix;

	memset(bi, 0, sizeof(*bi));

	dif = scsi_host_dif_capable(sdp->host, type);
	dix = scsi_host_dix_capable(sdp->host, type);

	if (!dix && scsi_host_dix_capable(sdp->host, 0)) {
		dif = 0; dix = 1;
	}

	if (!dix)
		return;

	/* Enable DMA of protection information */
	if (scsi_host_get_guard(sdkp->device->host) & SHOST_DIX_GUARD_IP)
		bi->csum_type = BLK_INTEGRITY_CSUM_IP;
	else
		bi->csum_type = BLK_INTEGRITY_CSUM_CRC;

	if (type != T10_PI_TYPE3_PROTECTION)
		bi->flags |= BLK_INTEGRITY_REF_TAG;

	bi->tuple_size = sizeof(struct t10_pi_tuple);

	if (dif && type) {
		bi->flags |= BLK_INTEGRITY_DEVICE_CAPABLE;

		if (!sdkp->ATO)
			return;

		if (type == T10_PI_TYPE3_PROTECTION)
			bi->tag_size = sizeof(u16) + sizeof(u32);
		else
			bi->tag_size = sizeof(u16);
	}

	sd_first_printk(KERN_NOTICE, sdkp,
			"Enabling DIX %s, application tag size %u bytes\n",
			blk_integrity_profile_name(bi), bi->tag_size);
}
