/*
 * Support for emulating SAT (ata pass through) on devices based
 *       on the Cypress USB/ATA bridge supporting ATACB.
 *
 * Copyright (c) 2008 Matthieu Castet (castet.matthieu@free.fr)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_eh.h>
#include <linux/ata.h>

#include "usb.h"
#include "protocol.h"
#include "scsiglue.h"
#include "debug.h"

/*
 * ATACB is a protocol used on cypress usb<->ata bridge to
 * send raw ATA command over mass storage
 * There is a ATACB2 protocol that support LBA48 on newer chip.
 * More info that be found on cy7c68310_8.pdf and cy7c68300c_8.pdf
 * datasheet from cypress.com.
 */
void cypress_atacb_passthrough(struct scsi_cmnd *srb, struct us_data *us)
{
	unsigned char save_cmnd[MAX_COMMAND_SIZE];

	if (likely(srb->cmnd[0] != ATA_16 && srb->cmnd[0] != ATA_12)) {
		usb_stor_transparent_scsi_command(srb, us);
		return;
	}

	memcpy(save_cmnd, srb->cmnd, sizeof(save_cmnd));
	memset(srb->cmnd, 0, sizeof(srb->cmnd));

	/* check if we support the command */
	if (save_cmnd[1] >> 5) /* MULTIPLE_COUNT */
		goto invalid_fld;
	/* check protocol */
	switch((save_cmnd[1] >> 1) & 0xf) {
		case 3: /*no DATA */
		case 4: /* PIO in */
		case 5: /* PIO out */
			break;
		default:
			goto invalid_fld;
	}

	/* first build the ATACB command */
	srb->cmd_len = 16;

	srb->cmnd[0] = 0x24; /* bVSCBSignature : vendor-specific command
	                        this value can change, but most(all ?) manufacturers
							keep the cypress default : 0x24 */
	srb->cmnd[1] = 0x24; /* bVSCBSubCommand : 0x24 for ATACB */

	srb->cmnd[3] = 0xff - 1; /* features, sector count, lba low, lba med
								lba high, device, command are valid */
	srb->cmnd[4] = 1; /* TransferBlockCount : 512 */

	if (save_cmnd[0] == ATA_16) {
		srb->cmnd[ 6] = save_cmnd[ 4]; /* features */
		srb->cmnd[ 7] = save_cmnd[ 6]; /* sector count */
		srb->cmnd[ 8] = save_cmnd[ 8]; /* lba low */
		srb->cmnd[ 9] = save_cmnd[10]; /* lba med */
		srb->cmnd[10] = save_cmnd[12]; /* lba high */
		srb->cmnd[11] = save_cmnd[13]; /* device */
		srb->cmnd[12] = save_cmnd[14]; /* command */

		if (save_cmnd[1] & 0x01) {/* extended bit set for LBA48 */
			/* this could be supported by atacb2 */
			if (save_cmnd[3] || save_cmnd[5] || save_cmnd[7] || save_cmnd[9]
					|| save_cmnd[11])
				goto invalid_fld;
		}
	}
	else { /* ATA12 */
		srb->cmnd[ 6] = save_cmnd[3]; /* features */
		srb->cmnd[ 7] = save_cmnd[4]; /* sector count */
		srb->cmnd[ 8] = save_cmnd[5]; /* lba low */
		srb->cmnd[ 9] = save_cmnd[6]; /* lba med */
		srb->cmnd[10] = save_cmnd[7]; /* lba high */
		srb->cmnd[11] = save_cmnd[8]; /* device */
		srb->cmnd[12] = save_cmnd[9]; /* command */

	}
	/* Filter SET_FEATURES - XFER MODE command */
	if ((srb->cmnd[12] == ATA_CMD_SET_FEATURES)
			&& (srb->cmnd[6] == SETFEATURES_XFER))
		goto invalid_fld;

	if (srb->cmnd[12] == ATA_CMD_ID_ATA || srb->cmnd[12] == ATA_CMD_ID_ATAPI)
		srb->cmnd[2] |= (1<<7); /* set  IdentifyPacketDevice for these cmds */


	usb_stor_transparent_scsi_command(srb, us);

	/* if the device doesn't support ATACB
	 */
	if (srb->result == SAM_STAT_CHECK_CONDITION &&
			memcmp(srb->sense_buffer, usb_stor_sense_invalidCDB,
				sizeof(usb_stor_sense_invalidCDB)) == 0) {
		US_DEBUGP("cypress atacb not supported ???\n");
		goto end;
	}

	/* if ck_cond flags is set, and there wasn't critical error,
	 * build the special sense
	 */
	if ((srb->result != (DID_ERROR << 16) &&
				srb->result != (DID_ABORT << 16)) &&
			save_cmnd[2] & 0x20) {
		struct scsi_eh_save ses;
		unsigned char regs[8];
		unsigned char *sb = srb->sense_buffer;
		unsigned char *desc = sb + 8;
		int tmp_result;

		/* build the command for
		 * reading the ATA registers */
		scsi_eh_prep_cmnd(srb, &ses, NULL, 0, 0);
		srb->sdb.length = sizeof(regs);
		sg_init_one(&ses.sense_sgl, regs, srb->sdb.length);
		srb->sdb.table.sgl = &ses.sense_sgl;
		srb->sc_data_direction = DMA_FROM_DEVICE;
		srb->sdb.table.nents = 1;
		/* we use the same command as before, but we set
		 * the read taskfile bit, for not executing atacb command,
		 * but reading register selected in srb->cmnd[4]
		 */
		srb->cmnd[2] = 1;

		usb_stor_transparent_scsi_command(srb, us);
		tmp_result = srb->result;
		scsi_eh_restore_cmnd(srb, &ses);
		/* we fail to get registers, report invalid command */
		if (tmp_result != SAM_STAT_GOOD)
			goto invalid_fld;

		/* build the sense */
		memset(sb, 0, SCSI_SENSE_BUFFERSIZE);

		/* set sk, asc for a good command */
		sb[1] = RECOVERED_ERROR;
		sb[2] = 0; /* ATA PASS THROUGH INFORMATION AVAILABLE */
		sb[3] = 0x1D;

		/* XXX we should generate sk, asc, ascq from status and error
		 * regs
		 * (see 11.1 Error translation ­ ATA device error to SCSI error map)
		 * and ata_to_sense_error from libata.
		 */

		/* Sense data is current and format is descriptor. */
		sb[0] = 0x72;
		desc[0] = 0x09; /* ATA_RETURN_DESCRIPTOR */

		/* set length of additional sense data */
		sb[7] = 14;
		desc[1] = 12;

		/* Copy registers into sense buffer. */
		desc[ 2] = 0x00;
		desc[ 3] = regs[1];  /* features */
		desc[ 5] = regs[2];  /* sector count */
		desc[ 7] = regs[3];  /* lba low */
		desc[ 9] = regs[4];  /* lba med */
		desc[11] = regs[5];  /* lba high */
		desc[12] = regs[6];  /* device */
		desc[13] = regs[7];  /* command */

		srb->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;
	}
	goto end;
invalid_fld:
	srb->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

	memcpy(srb->sense_buffer,
			usb_stor_sense_invalidCDB,
			sizeof(usb_stor_sense_invalidCDB));
end:
	memcpy(srb->cmnd, save_cmnd, sizeof(save_cmnd));
	if (srb->cmnd[0] == ATA_12)
		srb->cmd_len = 12;
}
