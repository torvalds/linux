/*
 *  sata_promise.h - Promise SATA common definitions and inline funcs
 *
 *  Copyright 2003-2004 Red Hat, Inc.
 *
 *  The contents of this file are subject to the Open
 *  Software License version 1.1 that can be found at
 *  http://www.opensource.org/licenses/osl-1.1.txt and is included herein
 *  by reference.
 *
 *  Alternatively, the contents of this file may be used under the terms
 *  of the GNU General Public License version 2 (the "GPL") as distributed
 *  in the kernel source COPYING file, in which case the provisions of
 *  the GPL are applicable instead of the above.  If you wish to allow
 *  the use of your version of this file only under the terms of the
 *  GPL and not to allow others to use your version of this file under
 *  the OSL, indicate your decision by deleting the provisions above and
 *  replace them with the notice and other provisions required by the GPL.
 *  If you do not delete the provisions above, a recipient may use your
 *  version of this file under either the OSL or the GPL.
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
	u32 *buf32 = (u32 *) buf;

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
