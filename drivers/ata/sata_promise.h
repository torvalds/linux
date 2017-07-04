/*
 *  sata_promise.h - Promise SATA common definitions and inline funcs
 *
 *  Copyright 2003-2004 Red Hat, Inc.
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/driver-api/libata.rst
 *
 */

#ifndef __SATA_PROMISE_H__
#define __SATA_PROMISE_H__

#include <linux/ata.h>

enum pdc_packet_bits {
	PDC_PKT_READ		= (1 << 2),
	PDC_PKT_NODATA		= (1 << 3),

	PDC_PKT_SIZEMASK	= (1 << 7) | (1 << 6) | (1 << 5),
	PDC_PKT_CLEAR_BSY	= (1 << 4),
	PDC_PKT_WAIT_DRDY	= (1 << 3) | (1 << 4),
	PDC_LAST_REG		= (1 << 3),

	PDC_REG_DEVCTL		= (1 << 3) | (1 << 2) | (1 << 1),
};

static inline unsigned int pdc_pkt_header(struct ata_taskfile *tf,
					  dma_addr_t sg_table,
					  unsigned int devno, u8 *buf)
{
	u8 dev_reg;
	__le32 *buf32 = (__le32 *) buf;

	/* set control bits (byte 0), zero delay seq id (byte 3),
	 * and seq id (byte 2)
	 */
	switch (tf->protocol) {
	case ATA_PROT_DMA:
		if (!(tf->flags & ATA_TFLAG_WRITE))
			buf32[0] = cpu_to_le32(PDC_PKT_READ);
		else
			buf32[0] = 0;
		break;

	case ATA_PROT_NODATA:
		buf32[0] = cpu_to_le32(PDC_PKT_NODATA);
		break;

	default:
		BUG();
		break;
	}

	buf32[1] = cpu_to_le32(sg_table);	/* S/G table addr */
	buf32[2] = 0;				/* no next-packet */

	if (devno == 0)
		dev_reg = ATA_DEVICE_OBS;
	else
		dev_reg = ATA_DEVICE_OBS | ATA_DEV1;

	/* select device */
	buf[12] = (1 << 5) | PDC_PKT_CLEAR_BSY | ATA_REG_DEVICE;
	buf[13] = dev_reg;

	/* device control register */
	buf[14] = (1 << 5) | PDC_REG_DEVCTL;
	buf[15] = tf->ctl;

	return 16; 	/* offset of next byte */
}

static inline unsigned int pdc_pkt_footer(struct ata_taskfile *tf, u8 *buf,
				  unsigned int i)
{
	if (tf->flags & ATA_TFLAG_DEVICE) {
		buf[i++] = (1 << 5) | ATA_REG_DEVICE;
		buf[i++] = tf->device;
	}

	/* and finally the command itself; also includes end-of-pkt marker */
	buf[i++] = (1 << 5) | PDC_LAST_REG | ATA_REG_CMD;
	buf[i++] = tf->command;

	return i;
}

static inline unsigned int pdc_prep_lba28(struct ata_taskfile *tf, u8 *buf, unsigned int i)
{
	/* the "(1 << 5)" should be read "(count << 5)" */

	/* ATA command block registers */
	buf[i++] = (1 << 5) | ATA_REG_FEATURE;
	buf[i++] = tf->feature;

	buf[i++] = (1 << 5) | ATA_REG_NSECT;
	buf[i++] = tf->nsect;

	buf[i++] = (1 << 5) | ATA_REG_LBAL;
	buf[i++] = tf->lbal;

	buf[i++] = (1 << 5) | ATA_REG_LBAM;
	buf[i++] = tf->lbam;

	buf[i++] = (1 << 5) | ATA_REG_LBAH;
	buf[i++] = tf->lbah;

	return i;
}

static inline unsigned int pdc_prep_lba48(struct ata_taskfile *tf, u8 *buf, unsigned int i)
{
	/* the "(2 << 5)" should be read "(count << 5)" */

	/* ATA command block registers */
	buf[i++] = (2 << 5) | ATA_REG_FEATURE;
	buf[i++] = tf->hob_feature;
	buf[i++] = tf->feature;

	buf[i++] = (2 << 5) | ATA_REG_NSECT;
	buf[i++] = tf->hob_nsect;
	buf[i++] = tf->nsect;

	buf[i++] = (2 << 5) | ATA_REG_LBAL;
	buf[i++] = tf->hob_lbal;
	buf[i++] = tf->lbal;

	buf[i++] = (2 << 5) | ATA_REG_LBAM;
	buf[i++] = tf->hob_lbam;
	buf[i++] = tf->lbam;

	buf[i++] = (2 << 5) | ATA_REG_LBAH;
	buf[i++] = tf->hob_lbah;
	buf[i++] = tf->lbah;

	return i;
}


#endif /* __SATA_PROMISE_H__ */
