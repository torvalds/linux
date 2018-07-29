/*
 * sd_dif.c - SCSI Data Integrity Field
 *
 * Copyright (C) 2007, 2008 Oracle Corporation
 * Written by: Martin K. Petersen <martin.petersen@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 */

#include <linux/blkdev.h>
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
void sd_dif_config_host(struct scsi_disk *sdkp)
{
	struct scsi_device *sdp = sdkp->device;
	struct gendisk *disk = sdkp->disk;
	u8 type = sdkp->protection_type;
	struct blk_integrity bi;
	int dif, dix;

	dif = scsi_host_dif_capable(sdp->host, type);
	dix = scsi_host_dix_capable(sdp->host, type);

	if (!dix && scsi_host_dix_capable(sdp->host, 0)) {
		dif = 0; dix = 1;
	}

	if (!dix)
		return;

	memset(&bi, 0, sizeof(bi));

	/* Enable DMA of protection information */
	if (scsi_host_get_guard(sdkp->device->host) & SHOST_DIX_GUARD_IP) {
		if (type == T10_PI_TYPE3_PROTECTION)
			bi.profile = &t10_pi_type3_ip;
		else
			bi.profile = &t10_pi_type1_ip;

		bi.flags |= BLK_INTEGRITY_IP_CHECKSUM;
	} else
		if (type == T10_PI_TYPE3_PROTECTION)
			bi.profile = &t10_pi_type3_crc;
		else
			bi.profile = &t10_pi_type1_crc;

	bi.tuple_size = sizeof(struct t10_pi_tuple);
	sd_printk(KERN_NOTICE, sdkp,
		  "Enabling DIX %s protection\n", bi.profile->name);

	if (dif && type) {
		bi.flags |= BLK_INTEGRITY_DEVICE_CAPABLE;

		if (!sdkp->ATO)
			goto out;

		if (type == T10_PI_TYPE3_PROTECTION)
			bi.tag_size = sizeof(u16) + sizeof(u32);
		else
			bi.tag_size = sizeof(u16);

		sd_printk(KERN_NOTICE, sdkp, "DIF application tag size %u\n",
			  bi.tag_size);
	}

out:
	blk_integrity_register(disk, &bi);
}

