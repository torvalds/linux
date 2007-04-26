/*
 *  libata-core.c - helper library for ATA
 *
 *  Maintained by:  Jeff Garzik <jgarzik@pobox.com>
 *    		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
 *  Copyright 2003-2004 Jeff Garzik
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
 *  as Documentation/DocBook/libata.*
 *
 *  Hardware documentation available from http://www.t13.org/ and
 *  http://www.sata-io.org/
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/scatterlist.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <asm/io.h>
#include <asm/semaphore.h>
#include <asm/byteorder.h>

#include "libata.h"

#define DRV_VERSION	"2.20"	/* must be exactly four chars */


/* debounce timing parameters in msecs { interval, duration, timeout } */
const unsigned long sata_deb_timing_normal[]		= {   5,  100, 2000 };
const unsigned long sata_deb_timing_hotplug[]		= {  25,  500, 2000 };
const unsigned long sata_deb_timing_long[]		= { 100, 2000, 5000 };

static unsigned int ata_dev_init_params(struct ata_device *dev,
					u16 heads, u16 sectors);
static unsigned int ata_dev_set_xfermode(struct ata_device *dev);
static void ata_dev_xfermask(struct ata_device *dev);

static unsigned int ata_print_id = 1;
static struct workqueue_struct *ata_wq;

struct workqueue_struct *ata_aux_wq;

int atapi_enabled = 1;
module_param(atapi_enabled, int, 0444);
MODULE_PARM_DESC(atapi_enabled, "Enable discovery of ATAPI devices (0=off, 1=on)");

int atapi_dmadir = 0;
module_param(atapi_dmadir, int, 0444);
MODULE_PARM_DESC(atapi_dmadir, "Enable ATAPI DMADIR bridge support (0=off, 1=on)");

int libata_fua = 0;
module_param_named(fua, libata_fua, int, 0444);
MODULE_PARM_DESC(fua, "FUA support (0=off, 1=on)");

static int ata_probe_timeout = ATA_TMOUT_INTERNAL / HZ;
module_param(ata_probe_timeout, int, 0444);
MODULE_PARM_DESC(ata_probe_timeout, "Set ATA probing timeout (seconds)");

int libata_noacpi = 1;
module_param_named(noacpi, libata_noacpi, int, 0444);
MODULE_PARM_DESC(noacpi, "Disables the use of ACPI in suspend/resume when set");

MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("Library module for ATA devices");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);


/**
 *	ata_tf_to_fis - Convert ATA taskfile to SATA FIS structure
 *	@tf: Taskfile to convert
 *	@fis: Buffer into which data will output
 *	@pmp: Port multiplier port
 *
 *	Converts a standard ATA taskfile to a Serial ATA
 *	FIS structure (Register - Host to Device).
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_tf_to_fis(const struct ata_taskfile *tf, u8 *fis, u8 pmp)
{
	fis[0] = 0x27;	/* Register - Host to Device FIS */
	fis[1] = (pmp & 0xf) | (1 << 7); /* Port multiplier number,
					    bit 7 indicates Command FIS */
	fis[2] = tf->command;
	fis[3] = tf->feature;

	fis[4] = tf->lbal;
	fis[5] = tf->lbam;
	fis[6] = tf->lbah;
	fis[7] = tf->device;

	fis[8] = tf->hob_lbal;
	fis[9] = tf->hob_lbam;
	fis[10] = tf->hob_lbah;
	fis[11] = tf->hob_feature;

	fis[12] = tf->nsect;
	fis[13] = tf->hob_nsect;
	fis[14] = 0;
	fis[15] = tf->ctl;

	fis[16] = 0;
	fis[17] = 0;
	fis[18] = 0;
	fis[19] = 0;
}

/**
 *	ata_tf_from_fis - Convert SATA FIS to ATA taskfile
 *	@fis: Buffer from which data will be input
 *	@tf: Taskfile to output
 *
 *	Converts a serial ATA FIS structure to a standard ATA taskfile.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_tf_from_fis(const u8 *fis, struct ata_taskfile *tf)
{
	tf->command	= fis[2];	/* status */
	tf->feature	= fis[3];	/* error */

	tf->lbal	= fis[4];
	tf->lbam	= fis[5];
	tf->lbah	= fis[6];
	tf->device	= fis[7];

	tf->hob_lbal	= fis[8];
	tf->hob_lbam	= fis[9];
	tf->hob_lbah	= fis[10];

	tf->nsect	= fis[12];
	tf->hob_nsect	= fis[13];
}

static const u8 ata_rw_cmds[] = {
	/* pio multi */
	ATA_CMD_READ_MULTI,
	ATA_CMD_WRITE_MULTI,
	ATA_CMD_READ_MULTI_EXT,
	ATA_CMD_WRITE_MULTI_EXT,
	0,
	0,
	0,
	ATA_CMD_WRITE_MULTI_FUA_EXT,
	/* pio */
	ATA_CMD_PIO_READ,
	ATA_CMD_PIO_WRITE,
	ATA_CMD_PIO_READ_EXT,
	ATA_CMD_PIO_WRITE_EXT,
	0,
	0,
	0,
	0,
	/* dma */
	ATA_CMD_READ,
	ATA_CMD_WRITE,
	ATA_CMD_READ_EXT,
	ATA_CMD_WRITE_EXT,
	0,
	0,
	0,
	ATA_CMD_WRITE_FUA_EXT
};

/**
 *	ata_rwcmd_protocol - set taskfile r/w commands and protocol
 *	@tf: command to examine and configure
 *	@dev: device tf belongs to
 *
 *	Examine the device configuration and tf->flags to calculate
 *	the proper read/write commands and protocol to use.
 *
 *	LOCKING:
 *	caller.
 */
static int ata_rwcmd_protocol(struct ata_taskfile *tf, struct ata_device *dev)
{
	u8 cmd;

	int index, fua, lba48, write;

	fua = (tf->flags & ATA_TFLAG_FUA) ? 4 : 0;
	lba48 = (tf->flags & ATA_TFLAG_LBA48) ? 2 : 0;
	write = (tf->flags & ATA_TFLAG_WRITE) ? 1 : 0;

	if (dev->flags & ATA_DFLAG_PIO) {
		tf->protocol = ATA_PROT_PIO;
		index = dev->multi_count ? 0 : 8;
	} else if (lba48 && (dev->ap->flags & ATA_FLAG_PIO_LBA48)) {
		/* Unable to use DMA due to host limitation */
		tf->protocol = ATA_PROT_PIO;
		index = dev->multi_count ? 0 : 8;
	} else {
		tf->protocol = ATA_PROT_DMA;
		index = 16;
	}

	cmd = ata_rw_cmds[index + fua + lba48 + write];
	if (cmd) {
		tf->command = cmd;
		return 0;
	}
	return -1;
}

/**
 *	ata_tf_read_block - Read block address from ATA taskfile
 *	@tf: ATA taskfile of interest
 *	@dev: ATA device @tf belongs to
 *
 *	LOCKING:
 *	None.
 *
 *	Read block address from @tf.  This function can handle all
 *	three address formats - LBA, LBA48 and CHS.  tf->protocol and
 *	flags select the address format to use.
 *
 *	RETURNS:
 *	Block address read from @tf.
 */
u64 ata_tf_read_block(struct ata_taskfile *tf, struct ata_device *dev)
{
	u64 block = 0;

	if (tf->flags & ATA_TFLAG_LBA) {
		if (tf->flags & ATA_TFLAG_LBA48) {
			block |= (u64)tf->hob_lbah << 40;
			block |= (u64)tf->hob_lbam << 32;
			block |= tf->hob_lbal << 24;
		} else
			block |= (tf->device & 0xf) << 24;

		block |= tf->lbah << 16;
		block |= tf->lbam << 8;
		block |= tf->lbal;
	} else {
		u32 cyl, head, sect;

		cyl = tf->lbam | (tf->lbah << 8);
		head = tf->device & 0xf;
		sect = tf->lbal;

		block = (cyl * dev->heads + head) * dev->sectors + sect;
	}

	return block;
}

/**
 *	ata_build_rw_tf - Build ATA taskfile for given read/write request
 *	@tf: Target ATA taskfile
 *	@dev: ATA device @tf belongs to
 *	@block: Block address
 *	@n_block: Number of blocks
 *	@tf_flags: RW/FUA etc...
 *	@tag: tag
 *
 *	LOCKING:
 *	None.
 *
 *	Build ATA taskfile @tf for read/write request described by
 *	@block, @n_block, @tf_flags and @tag on @dev.
 *
 *	RETURNS:
 *
 *	0 on success, -ERANGE if the request is too large for @dev,
 *	-EINVAL if the request is invalid.
 */
int ata_build_rw_tf(struct ata_taskfile *tf, struct ata_device *dev,
		    u64 block, u32 n_block, unsigned int tf_flags,
		    unsigned int tag)
{
	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf->flags |= tf_flags;

	if (ata_ncq_enabled(dev) && likely(tag != ATA_TAG_INTERNAL)) {
		/* yay, NCQ */
		if (!lba_48_ok(block, n_block))
			return -ERANGE;

		tf->protocol = ATA_PROT_NCQ;
		tf->flags |= ATA_TFLAG_LBA | ATA_TFLAG_LBA48;

		if (tf->flags & ATA_TFLAG_WRITE)
			tf->command = ATA_CMD_FPDMA_WRITE;
		else
			tf->command = ATA_CMD_FPDMA_READ;

		tf->nsect = tag << 3;
		tf->hob_feature = (n_block >> 8) & 0xff;
		tf->feature = n_block & 0xff;

		tf->hob_lbah = (block >> 40) & 0xff;
		tf->hob_lbam = (block >> 32) & 0xff;
		tf->hob_lbal = (block >> 24) & 0xff;
		tf->lbah = (block >> 16) & 0xff;
		tf->lbam = (block >> 8) & 0xff;
		tf->lbal = block & 0xff;

		tf->device = 1 << 6;
		if (tf->flags & ATA_TFLAG_FUA)
			tf->device |= 1 << 7;
	} else if (dev->flags & ATA_DFLAG_LBA) {
		tf->flags |= ATA_TFLAG_LBA;

		if (lba_28_ok(block, n_block)) {
			/* use LBA28 */
			tf->device |= (block >> 24) & 0xf;
		} else if (lba_48_ok(block, n_block)) {
			if (!(dev->flags & ATA_DFLAG_LBA48))
				return -ERANGE;

			/* use LBA48 */
			tf->flags |= ATA_TFLAG_LBA48;

			tf->hob_nsect = (n_block >> 8) & 0xff;

			tf->hob_lbah = (block >> 40) & 0xff;
			tf->hob_lbam = (block >> 32) & 0xff;
			tf->hob_lbal = (block >> 24) & 0xff;
		} else
			/* request too large even for LBA48 */
			return -ERANGE;

		if (unlikely(ata_rwcmd_protocol(tf, dev) < 0))
			return -EINVAL;

		tf->nsect = n_block & 0xff;

		tf->lbah = (block >> 16) & 0xff;
		tf->lbam = (block >> 8) & 0xff;
		tf->lbal = block & 0xff;

		tf->device |= ATA_LBA;
	} else {
		/* CHS */
		u32 sect, head, cyl, track;

		/* The request -may- be too large for CHS addressing. */
		if (!lba_28_ok(block, n_block))
			return -ERANGE;

		if (unlikely(ata_rwcmd_protocol(tf, dev) < 0))
			return -EINVAL;

		/* Convert LBA to CHS */
		track = (u32)block / dev->sectors;
		cyl   = track / dev->heads;
		head  = track % dev->heads;
		sect  = (u32)block % dev->sectors + 1;

		DPRINTK("block %u track %u cyl %u head %u sect %u\n",
			(u32)block, track, cyl, head, sect);

		/* Check whether the converted CHS can fit.
		   Cylinder: 0-65535
		   Head: 0-15
		   Sector: 1-255*/
		if ((cyl >> 16) || (head >> 4) || (sect >> 8) || (!sect))
			return -ERANGE;

		tf->nsect = n_block & 0xff; /* Sector count 0 means 256 sectors */
		tf->lbal = sect;
		tf->lbam = cyl;
		tf->lbah = cyl >> 8;
		tf->device |= head;
	}

	return 0;
}

/**
 *	ata_pack_xfermask - Pack pio, mwdma and udma masks into xfer_mask
 *	@pio_mask: pio_mask
 *	@mwdma_mask: mwdma_mask
 *	@udma_mask: udma_mask
 *
 *	Pack @pio_mask, @mwdma_mask and @udma_mask into a single
 *	unsigned int xfer_mask.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Packed xfer_mask.
 */
static unsigned int ata_pack_xfermask(unsigned int pio_mask,
				      unsigned int mwdma_mask,
				      unsigned int udma_mask)
{
	return ((pio_mask << ATA_SHIFT_PIO) & ATA_MASK_PIO) |
		((mwdma_mask << ATA_SHIFT_MWDMA) & ATA_MASK_MWDMA) |
		((udma_mask << ATA_SHIFT_UDMA) & ATA_MASK_UDMA);
}

/**
 *	ata_unpack_xfermask - Unpack xfer_mask into pio, mwdma and udma masks
 *	@xfer_mask: xfer_mask to unpack
 *	@pio_mask: resulting pio_mask
 *	@mwdma_mask: resulting mwdma_mask
 *	@udma_mask: resulting udma_mask
 *
 *	Unpack @xfer_mask into @pio_mask, @mwdma_mask and @udma_mask.
 *	Any NULL distination masks will be ignored.
 */
static void ata_unpack_xfermask(unsigned int xfer_mask,
				unsigned int *pio_mask,
				unsigned int *mwdma_mask,
				unsigned int *udma_mask)
{
	if (pio_mask)
		*pio_mask = (xfer_mask & ATA_MASK_PIO) >> ATA_SHIFT_PIO;
	if (mwdma_mask)
		*mwdma_mask = (xfer_mask & ATA_MASK_MWDMA) >> ATA_SHIFT_MWDMA;
	if (udma_mask)
		*udma_mask = (xfer_mask & ATA_MASK_UDMA) >> ATA_SHIFT_UDMA;
}

static const struct ata_xfer_ent {
	int shift, bits;
	u8 base;
} ata_xfer_tbl[] = {
	{ ATA_SHIFT_PIO, ATA_BITS_PIO, XFER_PIO_0 },
	{ ATA_SHIFT_MWDMA, ATA_BITS_MWDMA, XFER_MW_DMA_0 },
	{ ATA_SHIFT_UDMA, ATA_BITS_UDMA, XFER_UDMA_0 },
	{ -1, },
};

/**
 *	ata_xfer_mask2mode - Find matching XFER_* for the given xfer_mask
 *	@xfer_mask: xfer_mask of interest
 *
 *	Return matching XFER_* value for @xfer_mask.  Only the highest
 *	bit of @xfer_mask is considered.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Matching XFER_* value, 0 if no match found.
 */
static u8 ata_xfer_mask2mode(unsigned int xfer_mask)
{
	int highbit = fls(xfer_mask) - 1;
	const struct ata_xfer_ent *ent;

	for (ent = ata_xfer_tbl; ent->shift >= 0; ent++)
		if (highbit >= ent->shift && highbit < ent->shift + ent->bits)
			return ent->base + highbit - ent->shift;
	return 0;
}

/**
 *	ata_xfer_mode2mask - Find matching xfer_mask for XFER_*
 *	@xfer_mode: XFER_* of interest
 *
 *	Return matching xfer_mask for @xfer_mode.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Matching xfer_mask, 0 if no match found.
 */
static unsigned int ata_xfer_mode2mask(u8 xfer_mode)
{
	const struct ata_xfer_ent *ent;

	for (ent = ata_xfer_tbl; ent->shift >= 0; ent++)
		if (xfer_mode >= ent->base && xfer_mode < ent->base + ent->bits)
			return 1 << (ent->shift + xfer_mode - ent->base);
	return 0;
}

/**
 *	ata_xfer_mode2shift - Find matching xfer_shift for XFER_*
 *	@xfer_mode: XFER_* of interest
 *
 *	Return matching xfer_shift for @xfer_mode.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Matching xfer_shift, -1 if no match found.
 */
static int ata_xfer_mode2shift(unsigned int xfer_mode)
{
	const struct ata_xfer_ent *ent;

	for (ent = ata_xfer_tbl; ent->shift >= 0; ent++)
		if (xfer_mode >= ent->base && xfer_mode < ent->base + ent->bits)
			return ent->shift;
	return -1;
}

/**
 *	ata_mode_string - convert xfer_mask to string
 *	@xfer_mask: mask of bits supported; only highest bit counts.
 *
 *	Determine string which represents the highest speed
 *	(highest bit in @modemask).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Constant C string representing highest speed listed in
 *	@mode_mask, or the constant C string "<n/a>".
 */
static const char *ata_mode_string(unsigned int xfer_mask)
{
	static const char * const xfer_mode_str[] = {
		"PIO0",
		"PIO1",
		"PIO2",
		"PIO3",
		"PIO4",
		"PIO5",
		"PIO6",
		"MWDMA0",
		"MWDMA1",
		"MWDMA2",
		"MWDMA3",
		"MWDMA4",
		"UDMA/16",
		"UDMA/25",
		"UDMA/33",
		"UDMA/44",
		"UDMA/66",
		"UDMA/100",
		"UDMA/133",
		"UDMA7",
	};
	int highbit;

	highbit = fls(xfer_mask) - 1;
	if (highbit >= 0 && highbit < ARRAY_SIZE(xfer_mode_str))
		return xfer_mode_str[highbit];
	return "<n/a>";
}

static const char *sata_spd_string(unsigned int spd)
{
	static const char * const spd_str[] = {
		"1.5 Gbps",
		"3.0 Gbps",
	};

	if (spd == 0 || (spd - 1) >= ARRAY_SIZE(spd_str))
		return "<unknown>";
	return spd_str[spd - 1];
}

void ata_dev_disable(struct ata_device *dev)
{
	if (ata_dev_enabled(dev) && ata_msg_drv(dev->ap)) {
		ata_dev_printk(dev, KERN_WARNING, "disabled\n");
		ata_down_xfermask_limit(dev, ATA_DNXFER_FORCE_PIO0 |
					     ATA_DNXFER_QUIET);
		dev->class++;
	}
}

/**
 *	ata_devchk - PATA device presence detection
 *	@ap: ATA channel to examine
 *	@device: Device to examine (starting at zero)
 *
 *	This technique was originally described in
 *	Hale Landis's ATADRVR (www.ata-atapi.com), and
 *	later found its way into the ATA/ATAPI spec.
 *
 *	Write a pattern to the ATA shadow registers,
 *	and if a device is present, it will respond by
 *	correctly storing and echoing back the
 *	ATA shadow register contents.
 *
 *	LOCKING:
 *	caller.
 */

static unsigned int ata_devchk(struct ata_port *ap, unsigned int device)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 nsect, lbal;

	ap->ops->dev_select(ap, device);

	iowrite8(0x55, ioaddr->nsect_addr);
	iowrite8(0xaa, ioaddr->lbal_addr);

	iowrite8(0xaa, ioaddr->nsect_addr);
	iowrite8(0x55, ioaddr->lbal_addr);

	iowrite8(0x55, ioaddr->nsect_addr);
	iowrite8(0xaa, ioaddr->lbal_addr);

	nsect = ioread8(ioaddr->nsect_addr);
	lbal = ioread8(ioaddr->lbal_addr);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return 1;	/* we found a device */

	return 0;		/* nothing found */
}

/**
 *	ata_dev_classify - determine device type based on ATA-spec signature
 *	@tf: ATA taskfile register set for device to be identified
 *
 *	Determine from taskfile register contents whether a device is
 *	ATA or ATAPI, as per "Signature and persistence" section
 *	of ATA/PI spec (volume 1, sect 5.14).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Device type, %ATA_DEV_ATA, %ATA_DEV_ATAPI, or %ATA_DEV_UNKNOWN
 *	the event of failure.
 */

unsigned int ata_dev_classify(const struct ata_taskfile *tf)
{
	/* Apple's open source Darwin code hints that some devices only
	 * put a proper signature into the LBA mid/high registers,
	 * So, we only check those.  It's sufficient for uniqueness.
	 */

	if (((tf->lbam == 0) && (tf->lbah == 0)) ||
	    ((tf->lbam == 0x3c) && (tf->lbah == 0xc3))) {
		DPRINTK("found ATA device by sig\n");
		return ATA_DEV_ATA;
	}

	if (((tf->lbam == 0x14) && (tf->lbah == 0xeb)) ||
	    ((tf->lbam == 0x69) && (tf->lbah == 0x96))) {
		DPRINTK("found ATAPI device by sig\n");
		return ATA_DEV_ATAPI;
	}

	DPRINTK("unknown device\n");
	return ATA_DEV_UNKNOWN;
}

/**
 *	ata_dev_try_classify - Parse returned ATA device signature
 *	@ap: ATA channel to examine
 *	@device: Device to examine (starting at zero)
 *	@r_err: Value of error register on completion
 *
 *	After an event -- SRST, E.D.D., or SATA COMRESET -- occurs,
 *	an ATA/ATAPI-defined set of values is placed in the ATA
 *	shadow registers, indicating the results of device detection
 *	and diagnostics.
 *
 *	Select the ATA device, and read the values from the ATA shadow
 *	registers.  Then parse according to the Error register value,
 *	and the spec-defined values examined by ata_dev_classify().
 *
 *	LOCKING:
 *	caller.
 *
 *	RETURNS:
 *	Device type - %ATA_DEV_ATA, %ATA_DEV_ATAPI or %ATA_DEV_NONE.
 */

unsigned int
ata_dev_try_classify(struct ata_port *ap, unsigned int device, u8 *r_err)
{
	struct ata_taskfile tf;
	unsigned int class;
	u8 err;

	ap->ops->dev_select(ap, device);

	memset(&tf, 0, sizeof(tf));

	ap->ops->tf_read(ap, &tf);
	err = tf.feature;
	if (r_err)
		*r_err = err;

	/* see if device passed diags: if master then continue and warn later */
	if (err == 0 && device == 0)
		/* diagnostic fail : do nothing _YET_ */
		ap->device[device].horkage |= ATA_HORKAGE_DIAGNOSTIC;
	else if (err == 1)
		/* do nothing */ ;
	else if ((device == 0) && (err == 0x81))
		/* do nothing */ ;
	else
		return ATA_DEV_NONE;

	/* determine if device is ATA or ATAPI */
	class = ata_dev_classify(&tf);

	if (class == ATA_DEV_UNKNOWN)
		return ATA_DEV_NONE;
	if ((class == ATA_DEV_ATA) && (ata_chk_status(ap) == 0))
		return ATA_DEV_NONE;
	return class;
}

/**
 *	ata_id_string - Convert IDENTIFY DEVICE page into string
 *	@id: IDENTIFY DEVICE results we will examine
 *	@s: string into which data is output
 *	@ofs: offset into identify device page
 *	@len: length of string to return. must be an even number.
 *
 *	The strings in the IDENTIFY DEVICE page are broken up into
 *	16-bit chunks.  Run through the string, and output each
 *	8-bit chunk linearly, regardless of platform.
 *
 *	LOCKING:
 *	caller.
 */

void ata_id_string(const u16 *id, unsigned char *s,
		   unsigned int ofs, unsigned int len)
{
	unsigned int c;

	while (len > 0) {
		c = id[ofs] >> 8;
		*s = c;
		s++;

		c = id[ofs] & 0xff;
		*s = c;
		s++;

		ofs++;
		len -= 2;
	}
}

/**
 *	ata_id_c_string - Convert IDENTIFY DEVICE page into C string
 *	@id: IDENTIFY DEVICE results we will examine
 *	@s: string into which data is output
 *	@ofs: offset into identify device page
 *	@len: length of string to return. must be an odd number.
 *
 *	This function is identical to ata_id_string except that it
 *	trims trailing spaces and terminates the resulting string with
 *	null.  @len must be actual maximum length (even number) + 1.
 *
 *	LOCKING:
 *	caller.
 */
void ata_id_c_string(const u16 *id, unsigned char *s,
		     unsigned int ofs, unsigned int len)
{
	unsigned char *p;

	WARN_ON(!(len & 1));

	ata_id_string(id, s, ofs, len - 1);

	p = s + strnlen(s, len - 1);
	while (p > s && p[-1] == ' ')
		p--;
	*p = '\0';
}

static u64 ata_id_n_sectors(const u16 *id)
{
	if (ata_id_has_lba(id)) {
		if (ata_id_has_lba48(id))
			return ata_id_u64(id, 100);
		else
			return ata_id_u32(id, 60);
	} else {
		if (ata_id_current_chs_valid(id))
			return ata_id_u32(id, 57);
		else
			return id[1] * id[3] * id[6];
	}
}

/**
 *	ata_id_to_dma_mode	-	Identify DMA mode from id block
 *	@dev: device to identify
 *	@unknown: mode to assume if we cannot tell
 *
 *	Set up the timing values for the device based upon the identify
 *	reported values for the DMA mode. This function is used by drivers
 *	which rely upon firmware configured modes, but wish to report the
 *	mode correctly when possible.
 *
 *	In addition we emit similarly formatted messages to the default
 *	ata_dev_set_mode handler, in order to provide consistency of
 *	presentation.
 */

void ata_id_to_dma_mode(struct ata_device *dev, u8 unknown)
{
	unsigned int mask;
	u8 mode;

	/* Pack the DMA modes */
	mask = ((dev->id[63] >> 8) << ATA_SHIFT_MWDMA) & ATA_MASK_MWDMA;
	if (dev->id[53] & 0x04)
		mask |= ((dev->id[88] >> 8) << ATA_SHIFT_UDMA) & ATA_MASK_UDMA;

	/* Select the mode in use */
	mode = ata_xfer_mask2mode(mask);

	if (mode != 0) {
		ata_dev_printk(dev, KERN_INFO, "configured for %s\n",
		       ata_mode_string(mask));
	} else {
		/* SWDMA perhaps ? */
		mode = unknown;
		ata_dev_printk(dev, KERN_INFO, "configured for DMA\n");
	}

	/* Configure the device reporting */
	dev->xfer_mode = mode;
	dev->xfer_shift = ata_xfer_mode2shift(mode);
}

/**
 *	ata_noop_dev_select - Select device 0/1 on ATA bus
 *	@ap: ATA channel to manipulate
 *	@device: ATA device (numbered from zero) to select
 *
 *	This function performs no actual function.
 *
 *	May be used as the dev_select() entry in ata_port_operations.
 *
 *	LOCKING:
 *	caller.
 */
void ata_noop_dev_select (struct ata_port *ap, unsigned int device)
{
}


/**
 *	ata_std_dev_select - Select device 0/1 on ATA bus
 *	@ap: ATA channel to manipulate
 *	@device: ATA device (numbered from zero) to select
 *
 *	Use the method defined in the ATA specification to
 *	make either device 0, or device 1, active on the
 *	ATA channel.  Works with both PIO and MMIO.
 *
 *	May be used as the dev_select() entry in ata_port_operations.
 *
 *	LOCKING:
 *	caller.
 */

void ata_std_dev_select (struct ata_port *ap, unsigned int device)
{
	u8 tmp;

	if (device == 0)
		tmp = ATA_DEVICE_OBS;
	else
		tmp = ATA_DEVICE_OBS | ATA_DEV1;

	iowrite8(tmp, ap->ioaddr.device_addr);
	ata_pause(ap);		/* needed; also flushes, for mmio */
}

/**
 *	ata_dev_select - Select device 0/1 on ATA bus
 *	@ap: ATA channel to manipulate
 *	@device: ATA device (numbered from zero) to select
 *	@wait: non-zero to wait for Status register BSY bit to clear
 *	@can_sleep: non-zero if context allows sleeping
 *
 *	Use the method defined in the ATA specification to
 *	make either device 0, or device 1, active on the
 *	ATA channel.
 *
 *	This is a high-level version of ata_std_dev_select(),
 *	which additionally provides the services of inserting
 *	the proper pauses and status polling, where needed.
 *
 *	LOCKING:
 *	caller.
 */

void ata_dev_select(struct ata_port *ap, unsigned int device,
			   unsigned int wait, unsigned int can_sleep)
{
	if (ata_msg_probe(ap))
		ata_port_printk(ap, KERN_INFO, "ata_dev_select: ENTER, "
				"device %u, wait %u\n", device, wait);

	if (wait)
		ata_wait_idle(ap);

	ap->ops->dev_select(ap, device);

	if (wait) {
		if (can_sleep && ap->device[device].class == ATA_DEV_ATAPI)
			msleep(150);
		ata_wait_idle(ap);
	}
}

/**
 *	ata_dump_id - IDENTIFY DEVICE info debugging output
 *	@id: IDENTIFY DEVICE page to dump
 *
 *	Dump selected 16-bit words from the given IDENTIFY DEVICE
 *	page.
 *
 *	LOCKING:
 *	caller.
 */

static inline void ata_dump_id(const u16 *id)
{
	DPRINTK("49==0x%04x  "
		"53==0x%04x  "
		"63==0x%04x  "
		"64==0x%04x  "
		"75==0x%04x  \n",
		id[49],
		id[53],
		id[63],
		id[64],
		id[75]);
	DPRINTK("80==0x%04x  "
		"81==0x%04x  "
		"82==0x%04x  "
		"83==0x%04x  "
		"84==0x%04x  \n",
		id[80],
		id[81],
		id[82],
		id[83],
		id[84]);
	DPRINTK("88==0x%04x  "
		"93==0x%04x\n",
		id[88],
		id[93]);
}

/**
 *	ata_id_xfermask - Compute xfermask from the given IDENTIFY data
 *	@id: IDENTIFY data to compute xfer mask from
 *
 *	Compute the xfermask for this device. This is not as trivial
 *	as it seems if we must consider early devices correctly.
 *
 *	FIXME: pre IDE drive timing (do we care ?).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Computed xfermask
 */
static unsigned int ata_id_xfermask(const u16 *id)
{
	unsigned int pio_mask, mwdma_mask, udma_mask;

	/* Usual case. Word 53 indicates word 64 is valid */
	if (id[ATA_ID_FIELD_VALID] & (1 << 1)) {
		pio_mask = id[ATA_ID_PIO_MODES] & 0x03;
		pio_mask <<= 3;
		pio_mask |= 0x7;
	} else {
		/* If word 64 isn't valid then Word 51 high byte holds
		 * the PIO timing number for the maximum. Turn it into
		 * a mask.
		 */
		u8 mode = (id[ATA_ID_OLD_PIO_MODES] >> 8) & 0xFF;
		if (mode < 5)	/* Valid PIO range */
                	pio_mask = (2 << mode) - 1;
		else
			pio_mask = 1;

		/* But wait.. there's more. Design your standards by
		 * committee and you too can get a free iordy field to
		 * process. However its the speeds not the modes that
		 * are supported... Note drivers using the timing API
		 * will get this right anyway
		 */
	}

	mwdma_mask = id[ATA_ID_MWDMA_MODES] & 0x07;

	if (ata_id_is_cfa(id)) {
		/*
		 *	Process compact flash extended modes
		 */
		int pio = id[163] & 0x7;
		int dma = (id[163] >> 3) & 7;

		if (pio)
			pio_mask |= (1 << 5);
		if (pio > 1)
			pio_mask |= (1 << 6);
		if (dma)
			mwdma_mask |= (1 << 3);
		if (dma > 1)
			mwdma_mask |= (1 << 4);
	}

	udma_mask = 0;
	if (id[ATA_ID_FIELD_VALID] & (1 << 2))
		udma_mask = id[ATA_ID_UDMA_MODES] & 0xff;

	return ata_pack_xfermask(pio_mask, mwdma_mask, udma_mask);
}

/**
 *	ata_port_queue_task - Queue port_task
 *	@ap: The ata_port to queue port_task for
 *	@fn: workqueue function to be scheduled
 *	@data: data for @fn to use
 *	@delay: delay time for workqueue function
 *
 *	Schedule @fn(@data) for execution after @delay jiffies using
 *	port_task.  There is one port_task per port and it's the
 *	user(low level driver)'s responsibility to make sure that only
 *	one task is active at any given time.
 *
 *	libata core layer takes care of synchronization between
 *	port_task and EH.  ata_port_queue_task() may be ignored for EH
 *	synchronization.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_port_queue_task(struct ata_port *ap, work_func_t fn, void *data,
			 unsigned long delay)
{
	int rc;

	if (ap->pflags & ATA_PFLAG_FLUSH_PORT_TASK)
		return;

	PREPARE_DELAYED_WORK(&ap->port_task, fn);
	ap->port_task_data = data;

	rc = queue_delayed_work(ata_wq, &ap->port_task, delay);

	/* rc == 0 means that another user is using port task */
	WARN_ON(rc == 0);
}

/**
 *	ata_port_flush_task - Flush port_task
 *	@ap: The ata_port to flush port_task for
 *
 *	After this function completes, port_task is guranteed not to
 *	be running or scheduled.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */
void ata_port_flush_task(struct ata_port *ap)
{
	unsigned long flags;

	DPRINTK("ENTER\n");

	spin_lock_irqsave(ap->lock, flags);
	ap->pflags |= ATA_PFLAG_FLUSH_PORT_TASK;
	spin_unlock_irqrestore(ap->lock, flags);

	DPRINTK("flush #1\n");
	flush_workqueue(ata_wq);

	/*
	 * At this point, if a task is running, it's guaranteed to see
	 * the FLUSH flag; thus, it will never queue pio tasks again.
	 * Cancel and flush.
	 */
	if (!cancel_delayed_work(&ap->port_task)) {
		if (ata_msg_ctl(ap))
			ata_port_printk(ap, KERN_DEBUG, "%s: flush #2\n",
					__FUNCTION__);
		flush_workqueue(ata_wq);
	}

	spin_lock_irqsave(ap->lock, flags);
	ap->pflags &= ~ATA_PFLAG_FLUSH_PORT_TASK;
	spin_unlock_irqrestore(ap->lock, flags);

	if (ata_msg_ctl(ap))
		ata_port_printk(ap, KERN_DEBUG, "%s: EXIT\n", __FUNCTION__);
}

static void ata_qc_complete_internal(struct ata_queued_cmd *qc)
{
	struct completion *waiting = qc->private_data;

	complete(waiting);
}

/**
 *	ata_exec_internal_sg - execute libata internal command
 *	@dev: Device to which the command is sent
 *	@tf: Taskfile registers for the command and the result
 *	@cdb: CDB for packet command
 *	@dma_dir: Data tranfer direction of the command
 *	@sg: sg list for the data buffer of the command
 *	@n_elem: Number of sg entries
 *
 *	Executes libata internal command with timeout.  @tf contains
 *	command on entry and result on return.  Timeout and error
 *	conditions are reported via return value.  No recovery action
 *	is taken after a command times out.  It's caller's duty to
 *	clean up after timeout.
 *
 *	LOCKING:
 *	None.  Should be called with kernel context, might sleep.
 *
 *	RETURNS:
 *	Zero on success, AC_ERR_* mask on failure
 */
unsigned ata_exec_internal_sg(struct ata_device *dev,
			      struct ata_taskfile *tf, const u8 *cdb,
			      int dma_dir, struct scatterlist *sg,
			      unsigned int n_elem)
{
	struct ata_port *ap = dev->ap;
	u8 command = tf->command;
	struct ata_queued_cmd *qc;
	unsigned int tag, preempted_tag;
	u32 preempted_sactive, preempted_qc_active;
	DECLARE_COMPLETION_ONSTACK(wait);
	unsigned long flags;
	unsigned int err_mask;
	int rc;

	spin_lock_irqsave(ap->lock, flags);

	/* no internal command while frozen */
	if (ap->pflags & ATA_PFLAG_FROZEN) {
		spin_unlock_irqrestore(ap->lock, flags);
		return AC_ERR_SYSTEM;
	}

	/* initialize internal qc */

	/* XXX: Tag 0 is used for drivers with legacy EH as some
	 * drivers choke if any other tag is given.  This breaks
	 * ata_tag_internal() test for those drivers.  Don't use new
	 * EH stuff without converting to it.
	 */
	if (ap->ops->error_handler)
		tag = ATA_TAG_INTERNAL;
	else
		tag = 0;

	if (test_and_set_bit(tag, &ap->qc_allocated))
		BUG();
	qc = __ata_qc_from_tag(ap, tag);

	qc->tag = tag;
	qc->scsicmd = NULL;
	qc->ap = ap;
	qc->dev = dev;
	ata_qc_reinit(qc);

	preempted_tag = ap->active_tag;
	preempted_sactive = ap->sactive;
	preempted_qc_active = ap->qc_active;
	ap->active_tag = ATA_TAG_POISON;
	ap->sactive = 0;
	ap->qc_active = 0;

	/* prepare & issue qc */
	qc->tf = *tf;
	if (cdb)
		memcpy(qc->cdb, cdb, ATAPI_CDB_LEN);
	qc->flags |= ATA_QCFLAG_RESULT_TF;
	qc->dma_dir = dma_dir;
	if (dma_dir != DMA_NONE) {
		unsigned int i, buflen = 0;

		for (i = 0; i < n_elem; i++)
			buflen += sg[i].length;

		ata_sg_init(qc, sg, n_elem);
		qc->nbytes = buflen;
	}

	qc->private_data = &wait;
	qc->complete_fn = ata_qc_complete_internal;

	ata_qc_issue(qc);

	spin_unlock_irqrestore(ap->lock, flags);

	rc = wait_for_completion_timeout(&wait, ata_probe_timeout);

	ata_port_flush_task(ap);

	if (!rc) {
		spin_lock_irqsave(ap->lock, flags);

		/* We're racing with irq here.  If we lose, the
		 * following test prevents us from completing the qc
		 * twice.  If we win, the port is frozen and will be
		 * cleaned up by ->post_internal_cmd().
		 */
		if (qc->flags & ATA_QCFLAG_ACTIVE) {
			qc->err_mask |= AC_ERR_TIMEOUT;

			if (ap->ops->error_handler)
				ata_port_freeze(ap);
			else
				ata_qc_complete(qc);

			if (ata_msg_warn(ap))
				ata_dev_printk(dev, KERN_WARNING,
					"qc timeout (cmd 0x%x)\n", command);
		}

		spin_unlock_irqrestore(ap->lock, flags);
	}

	/* do post_internal_cmd */
	if (ap->ops->post_internal_cmd)
		ap->ops->post_internal_cmd(qc);

	if ((qc->flags & ATA_QCFLAG_FAILED) && !qc->err_mask) {
		if (ata_msg_warn(ap))
			ata_dev_printk(dev, KERN_WARNING,
				"zero err_mask for failed "
				"internal command, assuming AC_ERR_OTHER\n");
		qc->err_mask |= AC_ERR_OTHER;
	}

	/* finish up */
	spin_lock_irqsave(ap->lock, flags);

	*tf = qc->result_tf;
	err_mask = qc->err_mask;

	ata_qc_free(qc);
	ap->active_tag = preempted_tag;
	ap->sactive = preempted_sactive;
	ap->qc_active = preempted_qc_active;

	/* XXX - Some LLDDs (sata_mv) disable port on command failure.
	 * Until those drivers are fixed, we detect the condition
	 * here, fail the command with AC_ERR_SYSTEM and reenable the
	 * port.
	 *
	 * Note that this doesn't change any behavior as internal
	 * command failure results in disabling the device in the
	 * higher layer for LLDDs without new reset/EH callbacks.
	 *
	 * Kill the following code as soon as those drivers are fixed.
	 */
	if (ap->flags & ATA_FLAG_DISABLED) {
		err_mask |= AC_ERR_SYSTEM;
		ata_port_probe(ap);
	}

	spin_unlock_irqrestore(ap->lock, flags);

	return err_mask;
}

/**
 *	ata_exec_internal - execute libata internal command
 *	@dev: Device to which the command is sent
 *	@tf: Taskfile registers for the command and the result
 *	@cdb: CDB for packet command
 *	@dma_dir: Data tranfer direction of the command
 *	@buf: Data buffer of the command
 *	@buflen: Length of data buffer
 *
 *	Wrapper around ata_exec_internal_sg() which takes simple
 *	buffer instead of sg list.
 *
 *	LOCKING:
 *	None.  Should be called with kernel context, might sleep.
 *
 *	RETURNS:
 *	Zero on success, AC_ERR_* mask on failure
 */
unsigned ata_exec_internal(struct ata_device *dev,
			   struct ata_taskfile *tf, const u8 *cdb,
			   int dma_dir, void *buf, unsigned int buflen)
{
	struct scatterlist *psg = NULL, sg;
	unsigned int n_elem = 0;

	if (dma_dir != DMA_NONE) {
		WARN_ON(!buf);
		sg_init_one(&sg, buf, buflen);
		psg = &sg;
		n_elem++;
	}

	return ata_exec_internal_sg(dev, tf, cdb, dma_dir, psg, n_elem);
}

/**
 *	ata_do_simple_cmd - execute simple internal command
 *	@dev: Device to which the command is sent
 *	@cmd: Opcode to execute
 *
 *	Execute a 'simple' command, that only consists of the opcode
 *	'cmd' itself, without filling any other registers
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	Zero on success, AC_ERR_* mask on failure
 */
unsigned int ata_do_simple_cmd(struct ata_device *dev, u8 cmd)
{
	struct ata_taskfile tf;

	ata_tf_init(dev, &tf);

	tf.command = cmd;
	tf.flags |= ATA_TFLAG_DEVICE;
	tf.protocol = ATA_PROT_NODATA;

	return ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0);
}

/**
 *	ata_pio_need_iordy	-	check if iordy needed
 *	@adev: ATA device
 *
 *	Check if the current speed of the device requires IORDY. Used
 *	by various controllers for chip configuration.
 */

unsigned int ata_pio_need_iordy(const struct ata_device *adev)
{
	int pio;
	int speed = adev->pio_mode - XFER_PIO_0;

	if (speed < 2)
		return 0;
	if (speed > 2)
		return 1;

	/* If we have no drive specific rule, then PIO 2 is non IORDY */

	if (adev->id[ATA_ID_FIELD_VALID] & 2) {	/* EIDE */
		pio = adev->id[ATA_ID_EIDE_PIO];
		/* Is the speed faster than the drive allows non IORDY ? */
		if (pio) {
			/* This is cycle times not frequency - watch the logic! */
			if (pio > 240)	/* PIO2 is 240nS per cycle */
				return 1;
			return 0;
		}
	}
	return 0;
}

/**
 *	ata_dev_read_id - Read ID data from the specified device
 *	@dev: target device
 *	@p_class: pointer to class of the target device (may be changed)
 *	@flags: ATA_READID_* flags
 *	@id: buffer to read IDENTIFY data into
 *
 *	Read ID data from the specified device.  ATA_CMD_ID_ATA is
 *	performed on ATA devices and ATA_CMD_ID_ATAPI on ATAPI
 *	devices.  This function also issues ATA_CMD_INIT_DEV_PARAMS
 *	for pre-ATA4 drives.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_dev_read_id(struct ata_device *dev, unsigned int *p_class,
		    unsigned int flags, u16 *id)
{
	struct ata_port *ap = dev->ap;
	unsigned int class = *p_class;
	struct ata_taskfile tf;
	unsigned int err_mask = 0;
	const char *reason;
	int rc;

	if (ata_msg_ctl(ap))
		ata_dev_printk(dev, KERN_DEBUG, "%s: ENTER\n", __FUNCTION__);

	ata_dev_select(ap, dev->devno, 1, 1); /* select device 0/1 */

 retry:
	ata_tf_init(dev, &tf);

	switch (class) {
	case ATA_DEV_ATA:
		tf.command = ATA_CMD_ID_ATA;
		break;
	case ATA_DEV_ATAPI:
		tf.command = ATA_CMD_ID_ATAPI;
		break;
	default:
		rc = -ENODEV;
		reason = "unsupported class";
		goto err_out;
	}

	tf.protocol = ATA_PROT_PIO;

	/* Some devices choke if TF registers contain garbage.  Make
	 * sure those are properly initialized.
	 */
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;

	/* Device presence detection is unreliable on some
	 * controllers.  Always poll IDENTIFY if available.
	 */
	tf.flags |= ATA_TFLAG_POLLING;

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_FROM_DEVICE,
				     id, sizeof(id[0]) * ATA_ID_WORDS);
	if (err_mask) {
		if (err_mask & AC_ERR_NODEV_HINT) {
			DPRINTK("ata%u.%d: NODEV after polling detection\n",
				ap->print_id, dev->devno);
			return -ENOENT;
		}

		rc = -EIO;
		reason = "I/O error";
		goto err_out;
	}

	swap_buf_le16(id, ATA_ID_WORDS);

	/* sanity check */
	rc = -EINVAL;
	reason = "device reports illegal type";

	if (class == ATA_DEV_ATA) {
		if (!ata_id_is_ata(id) && !ata_id_is_cfa(id))
			goto err_out;
	} else {
		if (ata_id_is_ata(id))
			goto err_out;
	}

	if ((flags & ATA_READID_POSTRESET) && class == ATA_DEV_ATA) {
		/*
		 * The exact sequence expected by certain pre-ATA4 drives is:
		 * SRST RESET
		 * IDENTIFY
		 * INITIALIZE DEVICE PARAMETERS
		 * anything else..
		 * Some drives were very specific about that exact sequence.
		 */
		if (ata_id_major_version(id) < 4 || !ata_id_has_lba(id)) {
			err_mask = ata_dev_init_params(dev, id[3], id[6]);
			if (err_mask) {
				rc = -EIO;
				reason = "INIT_DEV_PARAMS failed";
				goto err_out;
			}

			/* current CHS translation info (id[53-58]) might be
			 * changed. reread the identify device info.
			 */
			flags &= ~ATA_READID_POSTRESET;
			goto retry;
		}
	}

	*p_class = class;

	return 0;

 err_out:
	if (ata_msg_warn(ap))
		ata_dev_printk(dev, KERN_WARNING, "failed to IDENTIFY "
			       "(%s, err_mask=0x%x)\n", reason, err_mask);
	return rc;
}

static inline u8 ata_dev_knobble(struct ata_device *dev)
{
	return ((dev->ap->cbl == ATA_CBL_SATA) && (!ata_id_is_sata(dev->id)));
}

static void ata_dev_config_ncq(struct ata_device *dev,
			       char *desc, size_t desc_sz)
{
	struct ata_port *ap = dev->ap;
	int hdepth = 0, ddepth = ata_id_queue_depth(dev->id);

	if (!ata_id_has_ncq(dev->id)) {
		desc[0] = '\0';
		return;
	}
	if (ata_device_blacklisted(dev) & ATA_HORKAGE_NONCQ) {
		snprintf(desc, desc_sz, "NCQ (not used)");
		return;
	}
	if (ap->flags & ATA_FLAG_NCQ) {
		hdepth = min(ap->scsi_host->can_queue, ATA_MAX_QUEUE - 1);
		dev->flags |= ATA_DFLAG_NCQ;
	}

	if (hdepth >= ddepth)
		snprintf(desc, desc_sz, "NCQ (depth %d)", ddepth);
	else
		snprintf(desc, desc_sz, "NCQ (depth %d/%d)", hdepth, ddepth);
}

static void ata_set_port_max_cmd_len(struct ata_port *ap)
{
	int i;

	if (ap->scsi_host) {
		unsigned int len = 0;

		for (i = 0; i < ATA_MAX_DEVICES; i++)
			len = max(len, ap->device[i].cdb_len);

		ap->scsi_host->max_cmd_len = len;
	}
}

/**
 *	ata_dev_configure - Configure the specified ATA/ATAPI device
 *	@dev: Target device to configure
 *
 *	Configure @dev according to @dev->id.  Generic and low-level
 *	driver specific fixups are also applied.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise
 */
int ata_dev_configure(struct ata_device *dev)
{
	struct ata_port *ap = dev->ap;
	int print_info = ap->eh_context.i.flags & ATA_EHI_PRINTINFO;
	const u16 *id = dev->id;
	unsigned int xfer_mask;
	char revbuf[7];		/* XYZ-99\0 */
	char fwrevbuf[ATA_ID_FW_REV_LEN+1];
	char modelbuf[ATA_ID_PROD_LEN+1];
	int rc;

	if (!ata_dev_enabled(dev) && ata_msg_info(ap)) {
		ata_dev_printk(dev, KERN_INFO, "%s: ENTER/EXIT -- nodev\n",
			       __FUNCTION__);
		return 0;
	}

	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG, "%s: ENTER\n", __FUNCTION__);

	/* set _SDD */
	rc = ata_acpi_push_id(ap, dev->devno);
	if (rc) {
		ata_dev_printk(dev, KERN_WARNING, "failed to set _SDD(%d)\n",
			rc);
	}

	/* retrieve and execute the ATA task file of _GTF */
	ata_acpi_exec_tfs(ap);

	/* print device capabilities */
	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG,
			       "%s: cfg 49:%04x 82:%04x 83:%04x 84:%04x "
			       "85:%04x 86:%04x 87:%04x 88:%04x\n",
			       __FUNCTION__,
			       id[49], id[82], id[83], id[84],
			       id[85], id[86], id[87], id[88]);

	/* initialize to-be-configured parameters */
	dev->flags &= ~ATA_DFLAG_CFG_MASK;
	dev->max_sectors = 0;
	dev->cdb_len = 0;
	dev->n_sectors = 0;
	dev->cylinders = 0;
	dev->heads = 0;
	dev->sectors = 0;

	/*
	 * common ATA, ATAPI feature tests
	 */

	/* find max transfer mode; for printk only */
	xfer_mask = ata_id_xfermask(id);

	if (ata_msg_probe(ap))
		ata_dump_id(id);

	/* ATA-specific feature tests */
	if (dev->class == ATA_DEV_ATA) {
		if (ata_id_is_cfa(id)) {
			if (id[162] & 1) /* CPRM may make this media unusable */
				ata_dev_printk(dev, KERN_WARNING,
					       "supports DRM functions and may "
					       "not be fully accessable.\n");
			snprintf(revbuf, 7, "CFA");
		}
		else
			snprintf(revbuf, 7, "ATA-%d",  ata_id_major_version(id));

		dev->n_sectors = ata_id_n_sectors(id);

		/* SCSI only uses 4-char revisions, dump full 8 chars from ATA */
		ata_id_c_string(dev->id, fwrevbuf, ATA_ID_FW_REV,
				sizeof(fwrevbuf));

		ata_id_c_string(dev->id, modelbuf, ATA_ID_PROD,
				sizeof(modelbuf));

		if (dev->id[59] & 0x100)
			dev->multi_count = dev->id[59] & 0xff;

		if (ata_id_has_lba(id)) {
			const char *lba_desc;
			char ncq_desc[20];

			lba_desc = "LBA";
			dev->flags |= ATA_DFLAG_LBA;
			if (ata_id_has_lba48(id)) {
				dev->flags |= ATA_DFLAG_LBA48;
				lba_desc = "LBA48";

				if (dev->n_sectors >= (1UL << 28) &&
				    ata_id_has_flush_ext(id))
					dev->flags |= ATA_DFLAG_FLUSH_EXT;
			}

			/* config NCQ */
			ata_dev_config_ncq(dev, ncq_desc, sizeof(ncq_desc));

			/* print device info to dmesg */
			if (ata_msg_drv(ap) && print_info) {
				ata_dev_printk(dev, KERN_INFO,
					"%s: %s, %s, max %s\n",
					revbuf, modelbuf, fwrevbuf,
					ata_mode_string(xfer_mask));
				ata_dev_printk(dev, KERN_INFO,
					"%Lu sectors, multi %u: %s %s\n",
					(unsigned long long)dev->n_sectors,
					dev->multi_count, lba_desc, ncq_desc);
			}
		} else {
			/* CHS */

			/* Default translation */
			dev->cylinders	= id[1];
			dev->heads	= id[3];
			dev->sectors	= id[6];

			if (ata_id_current_chs_valid(id)) {
				/* Current CHS translation is valid. */
				dev->cylinders = id[54];
				dev->heads     = id[55];
				dev->sectors   = id[56];
			}

			/* print device info to dmesg */
			if (ata_msg_drv(ap) && print_info) {
				ata_dev_printk(dev, KERN_INFO,
					"%s: %s, %s, max %s\n",
					revbuf,	modelbuf, fwrevbuf,
					ata_mode_string(xfer_mask));
				ata_dev_printk(dev, KERN_INFO,
					"%Lu sectors, multi %u, CHS %u/%u/%u\n",
					(unsigned long long)dev->n_sectors,
					dev->multi_count, dev->cylinders,
					dev->heads, dev->sectors);
			}
		}

		dev->cdb_len = 16;
	}

	/* ATAPI-specific feature tests */
	else if (dev->class == ATA_DEV_ATAPI) {
		char *cdb_intr_string = "";

		rc = atapi_cdb_len(id);
		if ((rc < 12) || (rc > ATAPI_CDB_LEN)) {
			if (ata_msg_warn(ap))
				ata_dev_printk(dev, KERN_WARNING,
					       "unsupported CDB len\n");
			rc = -EINVAL;
			goto err_out_nosup;
		}
		dev->cdb_len = (unsigned int) rc;

		if (ata_id_cdb_intr(dev->id)) {
			dev->flags |= ATA_DFLAG_CDB_INTR;
			cdb_intr_string = ", CDB intr";
		}

		/* print device info to dmesg */
		if (ata_msg_drv(ap) && print_info)
			ata_dev_printk(dev, KERN_INFO, "ATAPI, max %s%s\n",
				       ata_mode_string(xfer_mask),
				       cdb_intr_string);
	}

	/* determine max_sectors */
	dev->max_sectors = ATA_MAX_SECTORS;
	if (dev->flags & ATA_DFLAG_LBA48)
		dev->max_sectors = ATA_MAX_SECTORS_LBA48;

	if (dev->horkage & ATA_HORKAGE_DIAGNOSTIC) {
		/* Let the user know. We don't want to disallow opens for
		   rescue purposes, or in case the vendor is just a blithering
		   idiot */
                if (print_info) {
			ata_dev_printk(dev, KERN_WARNING,
"Drive reports diagnostics failure. This may indicate a drive\n");
			ata_dev_printk(dev, KERN_WARNING,
"fault or invalid emulation. Contact drive vendor for information.\n");
		}
	}

	ata_set_port_max_cmd_len(ap);

	/* limit bridge transfers to udma5, 200 sectors */
	if (ata_dev_knobble(dev)) {
		if (ata_msg_drv(ap) && print_info)
			ata_dev_printk(dev, KERN_INFO,
				       "applying bridge limits\n");
		dev->udma_mask &= ATA_UDMA5;
		dev->max_sectors = ATA_MAX_SECTORS;
	}

	if (ata_device_blacklisted(dev) & ATA_HORKAGE_MAX_SEC_128)
		dev->max_sectors = min(ATA_MAX_SECTORS_128, dev->max_sectors);

	/* limit ATAPI DMA to R/W commands only */
	if (ata_device_blacklisted(dev) & ATA_HORKAGE_DMA_RW_ONLY)
		dev->horkage |= ATA_HORKAGE_DMA_RW_ONLY;

	if (ap->ops->dev_config)
		ap->ops->dev_config(ap, dev);

	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG, "%s: EXIT, drv_stat = 0x%x\n",
			__FUNCTION__, ata_chk_status(ap));
	return 0;

err_out_nosup:
	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG,
			       "%s: EXIT, err\n", __FUNCTION__);
	return rc;
}

/**
 *	ata_bus_probe - Reset and probe ATA bus
 *	@ap: Bus to probe
 *
 *	Master ATA bus probing function.  Initiates a hardware-dependent
 *	bus reset, then attempts to identify any devices found on
 *	the bus.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	Zero on success, negative errno otherwise.
 */

int ata_bus_probe(struct ata_port *ap)
{
	unsigned int classes[ATA_MAX_DEVICES];
	int tries[ATA_MAX_DEVICES];
	int i, rc;
	struct ata_device *dev;

	ata_port_probe(ap);

	for (i = 0; i < ATA_MAX_DEVICES; i++)
		tries[i] = ATA_PROBE_MAX_TRIES;

 retry:
	/* reset and determine device classes */
	ap->ops->phy_reset(ap);

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		dev = &ap->device[i];

		if (!(ap->flags & ATA_FLAG_DISABLED) &&
		    dev->class != ATA_DEV_UNKNOWN)
			classes[dev->devno] = dev->class;
		else
			classes[dev->devno] = ATA_DEV_NONE;

		dev->class = ATA_DEV_UNKNOWN;
	}

	ata_port_probe(ap);

	/* after the reset the device state is PIO 0 and the controller
	   state is undefined. Record the mode */

	for (i = 0; i < ATA_MAX_DEVICES; i++)
		ap->device[i].pio_mode = XFER_PIO_0;

	/* read IDENTIFY page and configure devices. We have to do the identify
	   specific sequence bass-ackwards so that PDIAG- is released by
	   the slave device */

	for (i = ATA_MAX_DEVICES - 1; i >=  0; i--) {
		dev = &ap->device[i];

		if (tries[i])
			dev->class = classes[i];

		if (!ata_dev_enabled(dev))
			continue;

		rc = ata_dev_read_id(dev, &dev->class, ATA_READID_POSTRESET,
				     dev->id);
		if (rc)
			goto fail;
	}

	/* After the identify sequence we can now set up the devices. We do
	   this in the normal order so that the user doesn't get confused */

	for(i = 0; i < ATA_MAX_DEVICES; i++) {
		dev = &ap->device[i];
		if (!ata_dev_enabled(dev))
			continue;

		ap->eh_context.i.flags |= ATA_EHI_PRINTINFO;
		rc = ata_dev_configure(dev);
		ap->eh_context.i.flags &= ~ATA_EHI_PRINTINFO;
		if (rc)
			goto fail;
	}

	/* configure transfer mode */
	rc = ata_set_mode(ap, &dev);
	if (rc)
		goto fail;

	for (i = 0; i < ATA_MAX_DEVICES; i++)
		if (ata_dev_enabled(&ap->device[i]))
			return 0;

	/* no device present, disable port */
	ata_port_disable(ap);
	ap->ops->port_disable(ap);
	return -ENODEV;

 fail:
	tries[dev->devno]--;

	switch (rc) {
	case -EINVAL:
		/* eeek, something went very wrong, give up */
		tries[dev->devno] = 0;
		break;

	case -ENODEV:
		/* give it just one more chance */
		tries[dev->devno] = min(tries[dev->devno], 1);
	case -EIO:
		if (tries[dev->devno] == 1) {
			/* This is the last chance, better to slow
			 * down than lose it.
			 */
			sata_down_spd_limit(ap);
			ata_down_xfermask_limit(dev, ATA_DNXFER_PIO);
		}
	}

	if (!tries[dev->devno])
		ata_dev_disable(dev);

	goto retry;
}

/**
 *	ata_port_probe - Mark port as enabled
 *	@ap: Port for which we indicate enablement
 *
 *	Modify @ap data structure such that the system
 *	thinks that the entire port is enabled.
 *
 *	LOCKING: host lock, or some other form of
 *	serialization.
 */

void ata_port_probe(struct ata_port *ap)
{
	ap->flags &= ~ATA_FLAG_DISABLED;
}

/**
 *	sata_print_link_status - Print SATA link status
 *	@ap: SATA port to printk link status about
 *
 *	This function prints link speed and status of a SATA link.
 *
 *	LOCKING:
 *	None.
 */
static void sata_print_link_status(struct ata_port *ap)
{
	u32 sstatus, scontrol, tmp;

	if (sata_scr_read(ap, SCR_STATUS, &sstatus))
		return;
	sata_scr_read(ap, SCR_CONTROL, &scontrol);

	if (ata_port_online(ap)) {
		tmp = (sstatus >> 4) & 0xf;
		ata_port_printk(ap, KERN_INFO,
				"SATA link up %s (SStatus %X SControl %X)\n",
				sata_spd_string(tmp), sstatus, scontrol);
	} else {
		ata_port_printk(ap, KERN_INFO,
				"SATA link down (SStatus %X SControl %X)\n",
				sstatus, scontrol);
	}
}

/**
 *	__sata_phy_reset - Wake/reset a low-level SATA PHY
 *	@ap: SATA port associated with target SATA PHY.
 *
 *	This function issues commands to standard SATA Sxxx
 *	PHY registers, to wake up the phy (and device), and
 *	clear any reset condition.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 */
void __sata_phy_reset(struct ata_port *ap)
{
	u32 sstatus;
	unsigned long timeout = jiffies + (HZ * 5);

	if (ap->flags & ATA_FLAG_SATA_RESET) {
		/* issue phy wake/reset */
		sata_scr_write_flush(ap, SCR_CONTROL, 0x301);
		/* Couldn't find anything in SATA I/II specs, but
		 * AHCI-1.1 10.4.2 says at least 1 ms. */
		mdelay(1);
	}
	/* phy wake/clear reset */
	sata_scr_write_flush(ap, SCR_CONTROL, 0x300);

	/* wait for phy to become ready, if necessary */
	do {
		msleep(200);
		sata_scr_read(ap, SCR_STATUS, &sstatus);
		if ((sstatus & 0xf) != 1)
			break;
	} while (time_before(jiffies, timeout));

	/* print link status */
	sata_print_link_status(ap);

	/* TODO: phy layer with polling, timeouts, etc. */
	if (!ata_port_offline(ap))
		ata_port_probe(ap);
	else
		ata_port_disable(ap);

	if (ap->flags & ATA_FLAG_DISABLED)
		return;

	if (ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT)) {
		ata_port_disable(ap);
		return;
	}

	ap->cbl = ATA_CBL_SATA;
}

/**
 *	sata_phy_reset - Reset SATA bus.
 *	@ap: SATA port associated with target SATA PHY.
 *
 *	This function resets the SATA bus, and then probes
 *	the bus for devices.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 */
void sata_phy_reset(struct ata_port *ap)
{
	__sata_phy_reset(ap);
	if (ap->flags & ATA_FLAG_DISABLED)
		return;
	ata_bus_reset(ap);
}

/**
 *	ata_dev_pair		-	return other device on cable
 *	@adev: device
 *
 *	Obtain the other device on the same cable, or if none is
 *	present NULL is returned
 */

struct ata_device *ata_dev_pair(struct ata_device *adev)
{
	struct ata_port *ap = adev->ap;
	struct ata_device *pair = &ap->device[1 - adev->devno];
	if (!ata_dev_enabled(pair))
		return NULL;
	return pair;
}

/**
 *	ata_port_disable - Disable port.
 *	@ap: Port to be disabled.
 *
 *	Modify @ap data structure such that the system
 *	thinks that the entire port is disabled, and should
 *	never attempt to probe or communicate with devices
 *	on this port.
 *
 *	LOCKING: host lock, or some other form of
 *	serialization.
 */

void ata_port_disable(struct ata_port *ap)
{
	ap->device[0].class = ATA_DEV_NONE;
	ap->device[1].class = ATA_DEV_NONE;
	ap->flags |= ATA_FLAG_DISABLED;
}

/**
 *	sata_down_spd_limit - adjust SATA spd limit downward
 *	@ap: Port to adjust SATA spd limit for
 *
 *	Adjust SATA spd limit of @ap downward.  Note that this
 *	function only adjusts the limit.  The change must be applied
 *	using sata_set_spd().
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure
 */
int sata_down_spd_limit(struct ata_port *ap)
{
	u32 sstatus, spd, mask;
	int rc, highbit;

	rc = sata_scr_read(ap, SCR_STATUS, &sstatus);
	if (rc)
		return rc;

	mask = ap->sata_spd_limit;
	if (mask <= 1)
		return -EINVAL;
	highbit = fls(mask) - 1;
	mask &= ~(1 << highbit);

	spd = (sstatus >> 4) & 0xf;
	if (spd <= 1)
		return -EINVAL;
	spd--;
	mask &= (1 << spd) - 1;
	if (!mask)
		return -EINVAL;

	ap->sata_spd_limit = mask;

	ata_port_printk(ap, KERN_WARNING, "limiting SATA link speed to %s\n",
			sata_spd_string(fls(mask)));

	return 0;
}

static int __sata_set_spd_needed(struct ata_port *ap, u32 *scontrol)
{
	u32 spd, limit;

	if (ap->sata_spd_limit == UINT_MAX)
		limit = 0;
	else
		limit = fls(ap->sata_spd_limit);

	spd = (*scontrol >> 4) & 0xf;
	*scontrol = (*scontrol & ~0xf0) | ((limit & 0xf) << 4);

	return spd != limit;
}

/**
 *	sata_set_spd_needed - is SATA spd configuration needed
 *	@ap: Port in question
 *
 *	Test whether the spd limit in SControl matches
 *	@ap->sata_spd_limit.  This function is used to determine
 *	whether hardreset is necessary to apply SATA spd
 *	configuration.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	1 if SATA spd configuration is needed, 0 otherwise.
 */
int sata_set_spd_needed(struct ata_port *ap)
{
	u32 scontrol;

	if (sata_scr_read(ap, SCR_CONTROL, &scontrol))
		return 0;

	return __sata_set_spd_needed(ap, &scontrol);
}

/**
 *	sata_set_spd - set SATA spd according to spd limit
 *	@ap: Port to set SATA spd for
 *
 *	Set SATA spd of @ap according to sata_spd_limit.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	0 if spd doesn't need to be changed, 1 if spd has been
 *	changed.  Negative errno if SCR registers are inaccessible.
 */
int sata_set_spd(struct ata_port *ap)
{
	u32 scontrol;
	int rc;

	if ((rc = sata_scr_read(ap, SCR_CONTROL, &scontrol)))
		return rc;

	if (!__sata_set_spd_needed(ap, &scontrol))
		return 0;

	if ((rc = sata_scr_write(ap, SCR_CONTROL, scontrol)))
		return rc;

	return 1;
}

/*
 * This mode timing computation functionality is ported over from
 * drivers/ide/ide-timing.h and was originally written by Vojtech Pavlik
 */
/*
 * PIO 0-4, MWDMA 0-2 and UDMA 0-6 timings (in nanoseconds).
 * These were taken from ATA/ATAPI-6 standard, rev 0a, except
 * for UDMA6, which is currently supported only by Maxtor drives.
 *
 * For PIO 5/6 MWDMA 3/4 see the CFA specification 3.0.
 */

static const struct ata_timing ata_timing[] = {

	{ XFER_UDMA_6,     0,   0,   0,   0,   0,   0,   0,  15 },
	{ XFER_UDMA_5,     0,   0,   0,   0,   0,   0,   0,  20 },
	{ XFER_UDMA_4,     0,   0,   0,   0,   0,   0,   0,  30 },
	{ XFER_UDMA_3,     0,   0,   0,   0,   0,   0,   0,  45 },

	{ XFER_MW_DMA_4,  25,   0,   0,   0,  55,  20,  80,   0 },
	{ XFER_MW_DMA_3,  25,   0,   0,   0,  65,  25, 100,   0 },
	{ XFER_UDMA_2,     0,   0,   0,   0,   0,   0,   0,  60 },
	{ XFER_UDMA_1,     0,   0,   0,   0,   0,   0,   0,  80 },
	{ XFER_UDMA_0,     0,   0,   0,   0,   0,   0,   0, 120 },

/*	{ XFER_UDMA_SLOW,  0,   0,   0,   0,   0,   0,   0, 150 }, */

	{ XFER_MW_DMA_2,  25,   0,   0,   0,  70,  25, 120,   0 },
	{ XFER_MW_DMA_1,  45,   0,   0,   0,  80,  50, 150,   0 },
	{ XFER_MW_DMA_0,  60,   0,   0,   0, 215, 215, 480,   0 },

	{ XFER_SW_DMA_2,  60,   0,   0,   0, 120, 120, 240,   0 },
	{ XFER_SW_DMA_1,  90,   0,   0,   0, 240, 240, 480,   0 },
	{ XFER_SW_DMA_0, 120,   0,   0,   0, 480, 480, 960,   0 },

	{ XFER_PIO_6,     10,  55,  20,  80,  55,  20,  80,   0 },
	{ XFER_PIO_5,     15,  65,  25, 100,  65,  25, 100,   0 },
	{ XFER_PIO_4,     25,  70,  25, 120,  70,  25, 120,   0 },
	{ XFER_PIO_3,     30,  80,  70, 180,  80,  70, 180,   0 },

	{ XFER_PIO_2,     30, 290,  40, 330, 100,  90, 240,   0 },
	{ XFER_PIO_1,     50, 290,  93, 383, 125, 100, 383,   0 },
	{ XFER_PIO_0,     70, 290, 240, 600, 165, 150, 600,   0 },

/*	{ XFER_PIO_SLOW, 120, 290, 240, 960, 290, 240, 960,   0 }, */

	{ 0xFF }
};

#define ENOUGH(v,unit)		(((v)-1)/(unit)+1)
#define EZ(v,unit)		((v)?ENOUGH(v,unit):0)

static void ata_timing_quantize(const struct ata_timing *t, struct ata_timing *q, int T, int UT)
{
	q->setup   = EZ(t->setup   * 1000,  T);
	q->act8b   = EZ(t->act8b   * 1000,  T);
	q->rec8b   = EZ(t->rec8b   * 1000,  T);
	q->cyc8b   = EZ(t->cyc8b   * 1000,  T);
	q->active  = EZ(t->active  * 1000,  T);
	q->recover = EZ(t->recover * 1000,  T);
	q->cycle   = EZ(t->cycle   * 1000,  T);
	q->udma    = EZ(t->udma    * 1000, UT);
}

void ata_timing_merge(const struct ata_timing *a, const struct ata_timing *b,
		      struct ata_timing *m, unsigned int what)
{
	if (what & ATA_TIMING_SETUP  ) m->setup   = max(a->setup,   b->setup);
	if (what & ATA_TIMING_ACT8B  ) m->act8b   = max(a->act8b,   b->act8b);
	if (what & ATA_TIMING_REC8B  ) m->rec8b   = max(a->rec8b,   b->rec8b);
	if (what & ATA_TIMING_CYC8B  ) m->cyc8b   = max(a->cyc8b,   b->cyc8b);
	if (what & ATA_TIMING_ACTIVE ) m->active  = max(a->active,  b->active);
	if (what & ATA_TIMING_RECOVER) m->recover = max(a->recover, b->recover);
	if (what & ATA_TIMING_CYCLE  ) m->cycle   = max(a->cycle,   b->cycle);
	if (what & ATA_TIMING_UDMA   ) m->udma    = max(a->udma,    b->udma);
}

static const struct ata_timing* ata_timing_find_mode(unsigned short speed)
{
	const struct ata_timing *t;

	for (t = ata_timing; t->mode != speed; t++)
		if (t->mode == 0xFF)
			return NULL;
	return t;
}

int ata_timing_compute(struct ata_device *adev, unsigned short speed,
		       struct ata_timing *t, int T, int UT)
{
	const struct ata_timing *s;
	struct ata_timing p;

	/*
	 * Find the mode.
	 */

	if (!(s = ata_timing_find_mode(speed)))
		return -EINVAL;

	memcpy(t, s, sizeof(*s));

	/*
	 * If the drive is an EIDE drive, it can tell us it needs extended
	 * PIO/MW_DMA cycle timing.
	 */

	if (adev->id[ATA_ID_FIELD_VALID] & 2) {	/* EIDE drive */
		memset(&p, 0, sizeof(p));
		if(speed >= XFER_PIO_0 && speed <= XFER_SW_DMA_0) {
			if (speed <= XFER_PIO_2) p.cycle = p.cyc8b = adev->id[ATA_ID_EIDE_PIO];
					    else p.cycle = p.cyc8b = adev->id[ATA_ID_EIDE_PIO_IORDY];
		} else if(speed >= XFER_MW_DMA_0 && speed <= XFER_MW_DMA_2) {
			p.cycle = adev->id[ATA_ID_EIDE_DMA_MIN];
		}
		ata_timing_merge(&p, t, t, ATA_TIMING_CYCLE | ATA_TIMING_CYC8B);
	}

	/*
	 * Convert the timing to bus clock counts.
	 */

	ata_timing_quantize(t, t, T, UT);

	/*
	 * Even in DMA/UDMA modes we still use PIO access for IDENTIFY,
	 * S.M.A.R.T * and some other commands. We have to ensure that the
	 * DMA cycle timing is slower/equal than the fastest PIO timing.
	 */

	if (speed > XFER_PIO_6) {
		ata_timing_compute(adev, adev->pio_mode, &p, T, UT);
		ata_timing_merge(&p, t, t, ATA_TIMING_ALL);
	}

	/*
	 * Lengthen active & recovery time so that cycle time is correct.
	 */

	if (t->act8b + t->rec8b < t->cyc8b) {
		t->act8b += (t->cyc8b - (t->act8b + t->rec8b)) / 2;
		t->rec8b = t->cyc8b - t->act8b;
	}

	if (t->active + t->recover < t->cycle) {
		t->active += (t->cycle - (t->active + t->recover)) / 2;
		t->recover = t->cycle - t->active;
	}

	return 0;
}

/**
 *	ata_down_xfermask_limit - adjust dev xfer masks downward
 *	@dev: Device to adjust xfer masks
 *	@sel: ATA_DNXFER_* selector
 *
 *	Adjust xfer masks of @dev downward.  Note that this function
 *	does not apply the change.  Invoking ata_set_mode() afterwards
 *	will apply the limit.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure
 */
int ata_down_xfermask_limit(struct ata_device *dev, unsigned int sel)
{
	char buf[32];
	unsigned int orig_mask, xfer_mask;
	unsigned int pio_mask, mwdma_mask, udma_mask;
	int quiet, highbit;

	quiet = !!(sel & ATA_DNXFER_QUIET);
	sel &= ~ATA_DNXFER_QUIET;

	xfer_mask = orig_mask = ata_pack_xfermask(dev->pio_mask,
						  dev->mwdma_mask,
						  dev->udma_mask);
	ata_unpack_xfermask(xfer_mask, &pio_mask, &mwdma_mask, &udma_mask);

	switch (sel) {
	case ATA_DNXFER_PIO:
		highbit = fls(pio_mask) - 1;
		pio_mask &= ~(1 << highbit);
		break;

	case ATA_DNXFER_DMA:
		if (udma_mask) {
			highbit = fls(udma_mask) - 1;
			udma_mask &= ~(1 << highbit);
			if (!udma_mask)
				return -ENOENT;
		} else if (mwdma_mask) {
			highbit = fls(mwdma_mask) - 1;
			mwdma_mask &= ~(1 << highbit);
			if (!mwdma_mask)
				return -ENOENT;
		}
		break;

	case ATA_DNXFER_40C:
		udma_mask &= ATA_UDMA_MASK_40C;
		break;

	case ATA_DNXFER_FORCE_PIO0:
		pio_mask &= 1;
	case ATA_DNXFER_FORCE_PIO:
		mwdma_mask = 0;
		udma_mask = 0;
		break;

	default:
		BUG();
	}

	xfer_mask &= ata_pack_xfermask(pio_mask, mwdma_mask, udma_mask);

	if (!(xfer_mask & ATA_MASK_PIO) || xfer_mask == orig_mask)
		return -ENOENT;

	if (!quiet) {
		if (xfer_mask & (ATA_MASK_MWDMA | ATA_MASK_UDMA))
			snprintf(buf, sizeof(buf), "%s:%s",
				 ata_mode_string(xfer_mask),
				 ata_mode_string(xfer_mask & ATA_MASK_PIO));
		else
			snprintf(buf, sizeof(buf), "%s",
				 ata_mode_string(xfer_mask));

		ata_dev_printk(dev, KERN_WARNING,
			       "limiting speed to %s\n", buf);
	}

	ata_unpack_xfermask(xfer_mask, &dev->pio_mask, &dev->mwdma_mask,
			    &dev->udma_mask);

	return 0;
}

static int ata_dev_set_mode(struct ata_device *dev)
{
	struct ata_eh_context *ehc = &dev->ap->eh_context;
	unsigned int err_mask;
	int rc;

	dev->flags &= ~ATA_DFLAG_PIO;
	if (dev->xfer_shift == ATA_SHIFT_PIO)
		dev->flags |= ATA_DFLAG_PIO;

	err_mask = ata_dev_set_xfermode(dev);
	/* Old CFA may refuse this command, which is just fine */
	if (dev->xfer_shift == ATA_SHIFT_PIO && ata_id_is_cfa(dev->id))
        	err_mask &= ~AC_ERR_DEV;

	if (err_mask) {
		ata_dev_printk(dev, KERN_ERR, "failed to set xfermode "
			       "(err_mask=0x%x)\n", err_mask);
		return -EIO;
	}

	ehc->i.flags |= ATA_EHI_POST_SETMODE;
	rc = ata_dev_revalidate(dev, 0);
	ehc->i.flags &= ~ATA_EHI_POST_SETMODE;
	if (rc)
		return rc;

	DPRINTK("xfer_shift=%u, xfer_mode=0x%x\n",
		dev->xfer_shift, (int)dev->xfer_mode);

	ata_dev_printk(dev, KERN_INFO, "configured for %s\n",
		       ata_mode_string(ata_xfer_mode2mask(dev->xfer_mode)));
	return 0;
}

/**
 *	ata_set_mode - Program timings and issue SET FEATURES - XFER
 *	@ap: port on which timings will be programmed
 *	@r_failed_dev: out paramter for failed device
 *
 *	Set ATA device disk transfer mode (PIO3, UDMA6, etc.).  If
 *	ata_set_mode() fails, pointer to the failing device is
 *	returned in @r_failed_dev.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	0 on success, negative errno otherwise
 */
int ata_set_mode(struct ata_port *ap, struct ata_device **r_failed_dev)
{
	struct ata_device *dev;
	int i, rc = 0, used_dma = 0, found = 0;

	/* has private set_mode? */
	if (ap->ops->set_mode)
		return ap->ops->set_mode(ap, r_failed_dev);

	/* step 1: calculate xfer_mask */
	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		unsigned int pio_mask, dma_mask;

		dev = &ap->device[i];

		if (!ata_dev_enabled(dev))
			continue;

		ata_dev_xfermask(dev);

		pio_mask = ata_pack_xfermask(dev->pio_mask, 0, 0);
		dma_mask = ata_pack_xfermask(0, dev->mwdma_mask, dev->udma_mask);
		dev->pio_mode = ata_xfer_mask2mode(pio_mask);
		dev->dma_mode = ata_xfer_mask2mode(dma_mask);

		found = 1;
		if (dev->dma_mode)
			used_dma = 1;
	}
	if (!found)
		goto out;

	/* step 2: always set host PIO timings */
	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		dev = &ap->device[i];
		if (!ata_dev_enabled(dev))
			continue;

		if (!dev->pio_mode) {
			ata_dev_printk(dev, KERN_WARNING, "no PIO support\n");
			rc = -EINVAL;
			goto out;
		}

		dev->xfer_mode = dev->pio_mode;
		dev->xfer_shift = ATA_SHIFT_PIO;
		if (ap->ops->set_piomode)
			ap->ops->set_piomode(ap, dev);
	}

	/* step 3: set host DMA timings */
	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		dev = &ap->device[i];

		if (!ata_dev_enabled(dev) || !dev->dma_mode)
			continue;

		dev->xfer_mode = dev->dma_mode;
		dev->xfer_shift = ata_xfer_mode2shift(dev->dma_mode);
		if (ap->ops->set_dmamode)
			ap->ops->set_dmamode(ap, dev);
	}

	/* step 4: update devices' xfer mode */
	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		dev = &ap->device[i];

		/* don't update suspended devices' xfer mode */
		if (!ata_dev_ready(dev))
			continue;

		rc = ata_dev_set_mode(dev);
		if (rc)
			goto out;
	}

	/* Record simplex status. If we selected DMA then the other
	 * host channels are not permitted to do so.
	 */
	if (used_dma && (ap->host->flags & ATA_HOST_SIMPLEX))
		ap->host->simplex_claimed = ap;

	/* step5: chip specific finalisation */
	if (ap->ops->post_set_mode)
		ap->ops->post_set_mode(ap);
 out:
	if (rc)
		*r_failed_dev = dev;
	return rc;
}

/**
 *	ata_tf_to_host - issue ATA taskfile to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues ATA taskfile register set to ATA host controller,
 *	with proper synchronization with interrupt handler and
 *	other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */

static inline void ata_tf_to_host(struct ata_port *ap,
				  const struct ata_taskfile *tf)
{
	ap->ops->tf_load(ap, tf);
	ap->ops->exec_command(ap, tf);
}

/**
 *	ata_busy_sleep - sleep until BSY clears, or timeout
 *	@ap: port containing status register to be polled
 *	@tmout_pat: impatience timeout
 *	@tmout: overall timeout
 *
 *	Sleep until ATA Status register bit BSY clears,
 *	or a timeout occurs.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_busy_sleep(struct ata_port *ap,
		   unsigned long tmout_pat, unsigned long tmout)
{
	unsigned long timer_start, timeout;
	u8 status;

	status = ata_busy_wait(ap, ATA_BUSY, 300);
	timer_start = jiffies;
	timeout = timer_start + tmout_pat;
	while (status != 0xff && (status & ATA_BUSY) &&
	       time_before(jiffies, timeout)) {
		msleep(50);
		status = ata_busy_wait(ap, ATA_BUSY, 3);
	}

	if (status != 0xff && (status & ATA_BUSY))
		ata_port_printk(ap, KERN_WARNING,
				"port is slow to respond, please be patient "
				"(Status 0x%x)\n", status);

	timeout = timer_start + tmout;
	while (status != 0xff && (status & ATA_BUSY) &&
	       time_before(jiffies, timeout)) {
		msleep(50);
		status = ata_chk_status(ap);
	}

	if (status == 0xff)
		return -ENODEV;

	if (status & ATA_BUSY) {
		ata_port_printk(ap, KERN_ERR, "port failed to respond "
				"(%lu secs, Status 0x%x)\n",
				tmout / HZ, status);
		return -EBUSY;
	}

	return 0;
}

static void ata_bus_post_reset(struct ata_port *ap, unsigned int devmask)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int dev0 = devmask & (1 << 0);
	unsigned int dev1 = devmask & (1 << 1);
	unsigned long timeout;

	/* if device 0 was found in ata_devchk, wait for its
	 * BSY bit to clear
	 */
	if (dev0)
		ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);

	/* if device 1 was found in ata_devchk, wait for
	 * register access, then wait for BSY to clear
	 */
	timeout = jiffies + ATA_TMOUT_BOOT;
	while (dev1) {
		u8 nsect, lbal;

		ap->ops->dev_select(ap, 1);
		nsect = ioread8(ioaddr->nsect_addr);
		lbal = ioread8(ioaddr->lbal_addr);
		if ((nsect == 1) && (lbal == 1))
			break;
		if (time_after(jiffies, timeout)) {
			dev1 = 0;
			break;
		}
		msleep(50);	/* give drive a breather */
	}
	if (dev1)
		ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);

	/* is all this really necessary? */
	ap->ops->dev_select(ap, 0);
	if (dev1)
		ap->ops->dev_select(ap, 1);
	if (dev0)
		ap->ops->dev_select(ap, 0);
}

static unsigned int ata_bus_softreset(struct ata_port *ap,
				      unsigned int devmask)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	DPRINTK("ata%u: bus reset via SRST\n", ap->print_id);

	/* software reset.  causes dev0 to be selected */
	iowrite8(ap->ctl, ioaddr->ctl_addr);
	udelay(20);	/* FIXME: flush */
	iowrite8(ap->ctl | ATA_SRST, ioaddr->ctl_addr);
	udelay(20);	/* FIXME: flush */
	iowrite8(ap->ctl, ioaddr->ctl_addr);

	/* spec mandates ">= 2ms" before checking status.
	 * We wait 150ms, because that was the magic delay used for
	 * ATAPI devices in Hale Landis's ATADRVR, for the period of time
	 * between when the ATA command register is written, and then
	 * status is checked.  Because waiting for "a while" before
	 * checking status is fine, post SRST, we perform this magic
	 * delay here as well.
	 *
	 * Old drivers/ide uses the 2mS rule and then waits for ready
	 */
	msleep(150);

	/* Before we perform post reset processing we want to see if
	 * the bus shows 0xFF because the odd clown forgets the D7
	 * pulldown resistor.
	 */
	if (ata_check_status(ap) == 0xFF)
		return 0;

	ata_bus_post_reset(ap, devmask);

	return 0;
}

/**
 *	ata_bus_reset - reset host port and associated ATA channel
 *	@ap: port to reset
 *
 *	This is typically the first time we actually start issuing
 *	commands to the ATA channel.  We wait for BSY to clear, then
 *	issue EXECUTE DEVICE DIAGNOSTIC command, polling for its
 *	result.  Determine what devices, if any, are on the channel
 *	by looking at the device 0/1 error register.  Look at the signature
 *	stored in each device's taskfile registers, to determine if
 *	the device is ATA or ATAPI.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *	Obtains host lock.
 *
 *	SIDE EFFECTS:
 *	Sets ATA_FLAG_DISABLED if bus reset fails.
 */

void ata_bus_reset(struct ata_port *ap)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int slave_possible = ap->flags & ATA_FLAG_SLAVE_POSS;
	u8 err;
	unsigned int dev0, dev1 = 0, devmask = 0;

	DPRINTK("ENTER, host %u, port %u\n", ap->print_id, ap->port_no);

	/* determine if device 0/1 are present */
	if (ap->flags & ATA_FLAG_SATA_RESET)
		dev0 = 1;
	else {
		dev0 = ata_devchk(ap, 0);
		if (slave_possible)
			dev1 = ata_devchk(ap, 1);
	}

	if (dev0)
		devmask |= (1 << 0);
	if (dev1)
		devmask |= (1 << 1);

	/* select device 0 again */
	ap->ops->dev_select(ap, 0);

	/* issue bus reset */
	if (ap->flags & ATA_FLAG_SRST)
		if (ata_bus_softreset(ap, devmask))
			goto err_out;

	/*
	 * determine by signature whether we have ATA or ATAPI devices
	 */
	ap->device[0].class = ata_dev_try_classify(ap, 0, &err);
	if ((slave_possible) && (err != 0x81))
		ap->device[1].class = ata_dev_try_classify(ap, 1, &err);

	/* re-enable interrupts */
	ap->ops->irq_on(ap);

	/* is double-select really necessary? */
	if (ap->device[1].class != ATA_DEV_NONE)
		ap->ops->dev_select(ap, 1);
	if (ap->device[0].class != ATA_DEV_NONE)
		ap->ops->dev_select(ap, 0);

	/* if no devices were detected, disable this port */
	if ((ap->device[0].class == ATA_DEV_NONE) &&
	    (ap->device[1].class == ATA_DEV_NONE))
		goto err_out;

	if (ap->flags & (ATA_FLAG_SATA_RESET | ATA_FLAG_SRST)) {
		/* set up device control for ATA_FLAG_SATA_RESET */
		iowrite8(ap->ctl, ioaddr->ctl_addr);
	}

	DPRINTK("EXIT\n");
	return;

err_out:
	ata_port_printk(ap, KERN_ERR, "disabling port\n");
	ap->ops->port_disable(ap);

	DPRINTK("EXIT\n");
}

/**
 *	sata_phy_debounce - debounce SATA phy status
 *	@ap: ATA port to debounce SATA phy status for
 *	@params: timing parameters { interval, duratinon, timeout } in msec
 *
 *	Make sure SStatus of @ap reaches stable state, determined by
 *	holding the same value where DET is not 1 for @duration polled
 *	every @interval, before @timeout.  Timeout constraints the
 *	beginning of the stable state.  Because, after hot unplugging,
 *	DET gets stuck at 1 on some controllers, this functions waits
 *	until timeout then returns 0 if DET is stable at 1.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int sata_phy_debounce(struct ata_port *ap, const unsigned long *params)
{
	unsigned long interval_msec = params[0];
	unsigned long duration = params[1] * HZ / 1000;
	unsigned long timeout = jiffies + params[2] * HZ / 1000;
	unsigned long last_jiffies;
	u32 last, cur;
	int rc;

	if ((rc = sata_scr_read(ap, SCR_STATUS, &cur)))
		return rc;
	cur &= 0xf;

	last = cur;
	last_jiffies = jiffies;

	while (1) {
		msleep(interval_msec);
		if ((rc = sata_scr_read(ap, SCR_STATUS, &cur)))
			return rc;
		cur &= 0xf;

		/* DET stable? */
		if (cur == last) {
			if (cur == 1 && time_before(jiffies, timeout))
				continue;
			if (time_after(jiffies, last_jiffies + duration))
				return 0;
			continue;
		}

		/* unstable, start over */
		last = cur;
		last_jiffies = jiffies;

		/* check timeout */
		if (time_after(jiffies, timeout))
			return -EBUSY;
	}
}

/**
 *	sata_phy_resume - resume SATA phy
 *	@ap: ATA port to resume SATA phy for
 *	@params: timing parameters { interval, duratinon, timeout } in msec
 *
 *	Resume SATA phy of @ap and debounce it.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int sata_phy_resume(struct ata_port *ap, const unsigned long *params)
{
	u32 scontrol;
	int rc;

	if ((rc = sata_scr_read(ap, SCR_CONTROL, &scontrol)))
		return rc;

	scontrol = (scontrol & 0x0f0) | 0x300;

	if ((rc = sata_scr_write(ap, SCR_CONTROL, scontrol)))
		return rc;

	/* Some PHYs react badly if SStatus is pounded immediately
	 * after resuming.  Delay 200ms before debouncing.
	 */
	msleep(200);

	return sata_phy_debounce(ap, params);
}

static void ata_wait_spinup(struct ata_port *ap)
{
	struct ata_eh_context *ehc = &ap->eh_context;
	unsigned long end, secs;
	int rc;

	/* first, debounce phy if SATA */
	if (ap->cbl == ATA_CBL_SATA) {
		rc = sata_phy_debounce(ap, sata_deb_timing_hotplug);

		/* if debounced successfully and offline, no need to wait */
		if ((rc == 0 || rc == -EOPNOTSUPP) && ata_port_offline(ap))
			return;
	}

	/* okay, let's give the drive time to spin up */
	end = ehc->i.hotplug_timestamp + ATA_SPINUP_WAIT * HZ / 1000;
	secs = ((end - jiffies) + HZ - 1) / HZ;

	if (time_after(jiffies, end))
		return;

	if (secs > 5)
		ata_port_printk(ap, KERN_INFO, "waiting for device to spin up "
				"(%lu secs)\n", secs);

	schedule_timeout_uninterruptible(end - jiffies);
}

/**
 *	ata_std_prereset - prepare for reset
 *	@ap: ATA port to be reset
 *
 *	@ap is about to be reset.  Initialize it.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_std_prereset(struct ata_port *ap)
{
	struct ata_eh_context *ehc = &ap->eh_context;
	const unsigned long *timing = sata_ehc_deb_timing(ehc);
	int rc;

	/* handle link resume & hotplug spinup */
	if ((ehc->i.flags & ATA_EHI_RESUME_LINK) &&
	    (ap->flags & ATA_FLAG_HRST_TO_RESUME))
		ehc->i.action |= ATA_EH_HARDRESET;

	if ((ehc->i.flags & ATA_EHI_HOTPLUGGED) &&
	    (ap->flags & ATA_FLAG_SKIP_D2H_BSY))
		ata_wait_spinup(ap);

	/* if we're about to do hardreset, nothing more to do */
	if (ehc->i.action & ATA_EH_HARDRESET)
		return 0;

	/* if SATA, resume phy */
	if (ap->cbl == ATA_CBL_SATA) {
		rc = sata_phy_resume(ap, timing);
		if (rc && rc != -EOPNOTSUPP) {
			/* phy resume failed */
			ata_port_printk(ap, KERN_WARNING, "failed to resume "
					"link for reset (errno=%d)\n", rc);
			return rc;
		}
	}

	/* Wait for !BSY if the controller can wait for the first D2H
	 * Reg FIS and we don't know that no device is attached.
	 */
	if (!(ap->flags & ATA_FLAG_SKIP_D2H_BSY) && !ata_port_offline(ap))
		ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);

	return 0;
}

/**
 *	ata_std_softreset - reset host port via ATA SRST
 *	@ap: port to reset
 *	@classes: resulting classes of attached devices
 *
 *	Reset host port using ATA SRST.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_std_softreset(struct ata_port *ap, unsigned int *classes)
{
	unsigned int slave_possible = ap->flags & ATA_FLAG_SLAVE_POSS;
	unsigned int devmask = 0, err_mask;
	u8 err;

	DPRINTK("ENTER\n");

	if (ata_port_offline(ap)) {
		classes[0] = ATA_DEV_NONE;
		goto out;
	}

	/* determine if device 0/1 are present */
	if (ata_devchk(ap, 0))
		devmask |= (1 << 0);
	if (slave_possible && ata_devchk(ap, 1))
		devmask |= (1 << 1);

	/* select device 0 again */
	ap->ops->dev_select(ap, 0);

	/* issue bus reset */
	DPRINTK("about to softreset, devmask=%x\n", devmask);
	err_mask = ata_bus_softreset(ap, devmask);
	if (err_mask) {
		ata_port_printk(ap, KERN_ERR, "SRST failed (err_mask=0x%x)\n",
				err_mask);
		return -EIO;
	}

	/* determine by signature whether we have ATA or ATAPI devices */
	classes[0] = ata_dev_try_classify(ap, 0, &err);
	if (slave_possible && err != 0x81)
		classes[1] = ata_dev_try_classify(ap, 1, &err);

 out:
	DPRINTK("EXIT, classes[0]=%u [1]=%u\n", classes[0], classes[1]);
	return 0;
}

/**
 *	sata_port_hardreset - reset port via SATA phy reset
 *	@ap: port to reset
 *	@timing: timing parameters { interval, duratinon, timeout } in msec
 *
 *	SATA phy-reset host port using DET bits of SControl register.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int sata_port_hardreset(struct ata_port *ap, const unsigned long *timing)
{
	u32 scontrol;
	int rc;

	DPRINTK("ENTER\n");

	if (sata_set_spd_needed(ap)) {
		/* SATA spec says nothing about how to reconfigure
		 * spd.  To be on the safe side, turn off phy during
		 * reconfiguration.  This works for at least ICH7 AHCI
		 * and Sil3124.
		 */
		if ((rc = sata_scr_read(ap, SCR_CONTROL, &scontrol)))
			goto out;

		scontrol = (scontrol & 0x0f0) | 0x304;

		if ((rc = sata_scr_write(ap, SCR_CONTROL, scontrol)))
			goto out;

		sata_set_spd(ap);
	}

	/* issue phy wake/reset */
	if ((rc = sata_scr_read(ap, SCR_CONTROL, &scontrol)))
		goto out;

	scontrol = (scontrol & 0x0f0) | 0x301;

	if ((rc = sata_scr_write_flush(ap, SCR_CONTROL, scontrol)))
		goto out;

	/* Couldn't find anything in SATA I/II specs, but AHCI-1.1
	 * 10.4.2 says at least 1 ms.
	 */
	msleep(1);

	/* bring phy back */
	rc = sata_phy_resume(ap, timing);
 out:
	DPRINTK("EXIT, rc=%d\n", rc);
	return rc;
}

/**
 *	sata_std_hardreset - reset host port via SATA phy reset
 *	@ap: port to reset
 *	@class: resulting class of attached device
 *
 *	SATA phy-reset host port using DET bits of SControl register,
 *	wait for !BSY and classify the attached device.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int sata_std_hardreset(struct ata_port *ap, unsigned int *class)
{
	const unsigned long *timing = sata_ehc_deb_timing(&ap->eh_context);
	int rc;

	DPRINTK("ENTER\n");

	/* do hardreset */
	rc = sata_port_hardreset(ap, timing);
	if (rc) {
		ata_port_printk(ap, KERN_ERR,
				"COMRESET failed (errno=%d)\n", rc);
		return rc;
	}

	/* TODO: phy layer with polling, timeouts, etc. */
	if (ata_port_offline(ap)) {
		*class = ATA_DEV_NONE;
		DPRINTK("EXIT, link offline\n");
		return 0;
	}

	/* wait a while before checking status, see SRST for more info */
	msleep(150);

	if (ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT)) {
		ata_port_printk(ap, KERN_ERR,
				"COMRESET failed (device not ready)\n");
		return -EIO;
	}

	ap->ops->dev_select(ap, 0);	/* probably unnecessary */

	*class = ata_dev_try_classify(ap, 0, NULL);

	DPRINTK("EXIT, class=%u\n", *class);
	return 0;
}

/**
 *	ata_std_postreset - standard postreset callback
 *	@ap: the target ata_port
 *	@classes: classes of attached devices
 *
 *	This function is invoked after a successful reset.  Note that
 *	the device might have been reset more than once using
 *	different reset methods before postreset is invoked.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */
void ata_std_postreset(struct ata_port *ap, unsigned int *classes)
{
	u32 serror;

	DPRINTK("ENTER\n");

	/* print link status */
	sata_print_link_status(ap);

	/* clear SError */
	if (sata_scr_read(ap, SCR_ERROR, &serror) == 0)
		sata_scr_write(ap, SCR_ERROR, serror);

	/* re-enable interrupts */
	if (!ap->ops->error_handler)
		ap->ops->irq_on(ap);

	/* is double-select really necessary? */
	if (classes[0] != ATA_DEV_NONE)
		ap->ops->dev_select(ap, 1);
	if (classes[1] != ATA_DEV_NONE)
		ap->ops->dev_select(ap, 0);

	/* bail out if no device is present */
	if (classes[0] == ATA_DEV_NONE && classes[1] == ATA_DEV_NONE) {
		DPRINTK("EXIT, no device\n");
		return;
	}

	/* set up device control */
	if (ap->ioaddr.ctl_addr)
		iowrite8(ap->ctl, ap->ioaddr.ctl_addr);

	DPRINTK("EXIT\n");
}

/**
 *	ata_dev_same_device - Determine whether new ID matches configured device
 *	@dev: device to compare against
 *	@new_class: class of the new device
 *	@new_id: IDENTIFY page of the new device
 *
 *	Compare @new_class and @new_id against @dev and determine
 *	whether @dev is the device indicated by @new_class and
 *	@new_id.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	1 if @dev matches @new_class and @new_id, 0 otherwise.
 */
static int ata_dev_same_device(struct ata_device *dev, unsigned int new_class,
			       const u16 *new_id)
{
	const u16 *old_id = dev->id;
	unsigned char model[2][ATA_ID_PROD_LEN + 1];
	unsigned char serial[2][ATA_ID_SERNO_LEN + 1];
	u64 new_n_sectors;

	if (dev->class != new_class) {
		ata_dev_printk(dev, KERN_INFO, "class mismatch %d != %d\n",
			       dev->class, new_class);
		return 0;
	}

	ata_id_c_string(old_id, model[0], ATA_ID_PROD, sizeof(model[0]));
	ata_id_c_string(new_id, model[1], ATA_ID_PROD, sizeof(model[1]));
	ata_id_c_string(old_id, serial[0], ATA_ID_SERNO, sizeof(serial[0]));
	ata_id_c_string(new_id, serial[1], ATA_ID_SERNO, sizeof(serial[1]));
	new_n_sectors = ata_id_n_sectors(new_id);

	if (strcmp(model[0], model[1])) {
		ata_dev_printk(dev, KERN_INFO, "model number mismatch "
			       "'%s' != '%s'\n", model[0], model[1]);
		return 0;
	}

	if (strcmp(serial[0], serial[1])) {
		ata_dev_printk(dev, KERN_INFO, "serial number mismatch "
			       "'%s' != '%s'\n", serial[0], serial[1]);
		return 0;
	}

	if (dev->class == ATA_DEV_ATA && dev->n_sectors != new_n_sectors) {
		ata_dev_printk(dev, KERN_INFO, "n_sectors mismatch "
			       "%llu != %llu\n",
			       (unsigned long long)dev->n_sectors,
			       (unsigned long long)new_n_sectors);
		return 0;
	}

	return 1;
}

/**
 *	ata_dev_revalidate - Revalidate ATA device
 *	@dev: device to revalidate
 *	@readid_flags: read ID flags
 *
 *	Re-read IDENTIFY page and make sure @dev is still attached to
 *	the port.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, negative errno otherwise
 */
int ata_dev_revalidate(struct ata_device *dev, unsigned int readid_flags)
{
	unsigned int class = dev->class;
	u16 *id = (void *)dev->ap->sector_buf;
	int rc;

	if (!ata_dev_enabled(dev)) {
		rc = -ENODEV;
		goto fail;
	}

	/* read ID data */
	rc = ata_dev_read_id(dev, &class, readid_flags, id);
	if (rc)
		goto fail;

	/* is the device still there? */
	if (!ata_dev_same_device(dev, class, id)) {
		rc = -ENODEV;
		goto fail;
	}

	memcpy(dev->id, id, sizeof(id[0]) * ATA_ID_WORDS);

	/* configure device according to the new ID */
	rc = ata_dev_configure(dev);
	if (rc == 0)
		return 0;

 fail:
	ata_dev_printk(dev, KERN_ERR, "revalidation failed (errno=%d)\n", rc);
	return rc;
}

struct ata_blacklist_entry {
	const char *model_num;
	const char *model_rev;
	unsigned long horkage;
};

static const struct ata_blacklist_entry ata_device_blacklist [] = {
	/* Devices with DMA related problems under Linux */
	{ "WDC AC11000H",	NULL,		ATA_HORKAGE_NODMA },
	{ "WDC AC22100H",	NULL,		ATA_HORKAGE_NODMA },
	{ "WDC AC32500H",	NULL,		ATA_HORKAGE_NODMA },
	{ "WDC AC33100H",	NULL,		ATA_HORKAGE_NODMA },
	{ "WDC AC31600H",	NULL,		ATA_HORKAGE_NODMA },
	{ "WDC AC32100H",	"24.09P07",	ATA_HORKAGE_NODMA },
	{ "WDC AC23200L",	"21.10N21",	ATA_HORKAGE_NODMA },
	{ "Compaq CRD-8241B", 	NULL,		ATA_HORKAGE_NODMA },
	{ "CRD-8400B",		NULL, 		ATA_HORKAGE_NODMA },
	{ "CRD-8480B",		NULL,		ATA_HORKAGE_NODMA },
	{ "CRD-8482B",		NULL,		ATA_HORKAGE_NODMA },
	{ "CRD-84",		NULL,		ATA_HORKAGE_NODMA },
	{ "SanDisk SDP3B",	NULL,		ATA_HORKAGE_NODMA },
	{ "SanDisk SDP3B-64",	NULL,		ATA_HORKAGE_NODMA },
	{ "SANYO CD-ROM CRD",	NULL,		ATA_HORKAGE_NODMA },
	{ "HITACHI CDR-8",	NULL,		ATA_HORKAGE_NODMA },
	{ "HITACHI CDR-8335",	NULL,		ATA_HORKAGE_NODMA },
	{ "HITACHI CDR-8435",	NULL,		ATA_HORKAGE_NODMA },
	{ "Toshiba CD-ROM XM-6202B", NULL,	ATA_HORKAGE_NODMA },
	{ "TOSHIBA CD-ROM XM-1702BC", NULL,	ATA_HORKAGE_NODMA },
	{ "CD-532E-A", 		NULL,		ATA_HORKAGE_NODMA },
	{ "E-IDE CD-ROM CR-840",NULL,		ATA_HORKAGE_NODMA },
	{ "CD-ROM Drive/F5A",	NULL,		ATA_HORKAGE_NODMA },
	{ "WPI CDD-820", 	NULL,		ATA_HORKAGE_NODMA },
	{ "SAMSUNG CD-ROM SC-148C", NULL,	ATA_HORKAGE_NODMA },
	{ "SAMSUNG CD-ROM SC",	NULL,		ATA_HORKAGE_NODMA },
	{ "ATAPI CD-ROM DRIVE 40X MAXIMUM",NULL,ATA_HORKAGE_NODMA },
	{ "_NEC DV5800A", 	NULL,		ATA_HORKAGE_NODMA },
	{ "SAMSUNG CD-ROM SN-124","N001",	ATA_HORKAGE_NODMA },

	/* Weird ATAPI devices */
	{ "TORiSAN DVD-ROM DRD-N216", NULL,	ATA_HORKAGE_MAX_SEC_128 |
						ATA_HORKAGE_DMA_RW_ONLY },

	/* Devices we expect to fail diagnostics */

	/* Devices where NCQ should be avoided */
	/* NCQ is slow */
        { "WDC WD740ADFD-00",   NULL,		ATA_HORKAGE_NONCQ },
	/* http://thread.gmane.org/gmane.linux.ide/14907 */
	{ "FUJITSU MHT2060BH",	NULL,		ATA_HORKAGE_NONCQ },
	/* NCQ is broken */
	{ "Maxtor 6L250S0",     "BANC1G10",     ATA_HORKAGE_NONCQ },
	/* NCQ hard hangs device under heavier load, needs hard power cycle */
	{ "Maxtor 6B250S0",	"BANC1B70",	ATA_HORKAGE_NONCQ },
	/* Blacklist entries taken from Silicon Image 3124/3132
	   Windows driver .inf file - also several Linux problem reports */
	{ "HTS541060G9SA00",    "MB3OC60D",     ATA_HORKAGE_NONCQ, },
	{ "HTS541080G9SA00",    "MB4OC60D",     ATA_HORKAGE_NONCQ, },
	{ "HTS541010G9SA00",    "MBZOC60D",     ATA_HORKAGE_NONCQ, },

	/* Devices with NCQ limits */

	/* End Marker */
	{ }
};

unsigned long ata_device_blacklisted(const struct ata_device *dev)
{
	unsigned char model_num[ATA_ID_PROD_LEN + 1];
	unsigned char model_rev[ATA_ID_FW_REV_LEN + 1];
	const struct ata_blacklist_entry *ad = ata_device_blacklist;

	ata_id_c_string(dev->id, model_num, ATA_ID_PROD, sizeof(model_num));
	ata_id_c_string(dev->id, model_rev, ATA_ID_FW_REV, sizeof(model_rev));

	while (ad->model_num) {
		if (!strcmp(ad->model_num, model_num)) {
			if (ad->model_rev == NULL)
				return ad->horkage;
			if (!strcmp(ad->model_rev, model_rev))
				return ad->horkage;
		}
		ad++;
	}
	return 0;
}

static int ata_dma_blacklisted(const struct ata_device *dev)
{
	/* We don't support polling DMA.
	 * DMA blacklist those ATAPI devices with CDB-intr (and use PIO)
	 * if the LLDD handles only interrupts in the HSM_ST_LAST state.
	 */
	if ((dev->ap->flags & ATA_FLAG_PIO_POLLING) &&
	    (dev->flags & ATA_DFLAG_CDB_INTR))
		return 1;
	return (ata_device_blacklisted(dev) & ATA_HORKAGE_NODMA) ? 1 : 0;
}

/**
 *	ata_dev_xfermask - Compute supported xfermask of the given device
 *	@dev: Device to compute xfermask for
 *
 *	Compute supported xfermask of @dev and store it in
 *	dev->*_mask.  This function is responsible for applying all
 *	known limits including host controller limits, device
 *	blacklist, etc...
 *
 *	LOCKING:
 *	None.
 */
static void ata_dev_xfermask(struct ata_device *dev)
{
	struct ata_port *ap = dev->ap;
	struct ata_host *host = ap->host;
	unsigned long xfer_mask;

	/* controller modes available */
	xfer_mask = ata_pack_xfermask(ap->pio_mask,
				      ap->mwdma_mask, ap->udma_mask);

	/* Apply cable rule here.  Don't apply it early because when
	 * we handle hot plug the cable type can itself change.
	 */
	if (ap->cbl == ATA_CBL_PATA40)
		xfer_mask &= ~(0xF8 << ATA_SHIFT_UDMA);
	/* Apply drive side cable rule. Unknown or 80 pin cables reported
	 * host side are checked drive side as well. Cases where we know a
	 * 40wire cable is used safely for 80 are not checked here.
	 */
        if (ata_drive_40wire(dev->id) && (ap->cbl == ATA_CBL_PATA_UNK || ap->cbl == ATA_CBL_PATA80))
		xfer_mask &= ~(0xF8 << ATA_SHIFT_UDMA);


	xfer_mask &= ata_pack_xfermask(dev->pio_mask,
				       dev->mwdma_mask, dev->udma_mask);
	xfer_mask &= ata_id_xfermask(dev->id);

	/*
	 *	CFA Advanced TrueIDE timings are not allowed on a shared
	 *	cable
	 */
	if (ata_dev_pair(dev)) {
		/* No PIO5 or PIO6 */
		xfer_mask &= ~(0x03 << (ATA_SHIFT_PIO + 5));
		/* No MWDMA3 or MWDMA 4 */
		xfer_mask &= ~(0x03 << (ATA_SHIFT_MWDMA + 3));
	}

	if (ata_dma_blacklisted(dev)) {
		xfer_mask &= ~(ATA_MASK_MWDMA | ATA_MASK_UDMA);
		ata_dev_printk(dev, KERN_WARNING,
			       "device is on DMA blacklist, disabling DMA\n");
	}

	if ((host->flags & ATA_HOST_SIMPLEX) &&
            host->simplex_claimed && host->simplex_claimed != ap) {
		xfer_mask &= ~(ATA_MASK_MWDMA | ATA_MASK_UDMA);
		ata_dev_printk(dev, KERN_WARNING, "simplex DMA is claimed by "
			       "other device, disabling DMA\n");
	}

	if (ap->ops->mode_filter)
		xfer_mask = ap->ops->mode_filter(ap, dev, xfer_mask);

	ata_unpack_xfermask(xfer_mask, &dev->pio_mask,
			    &dev->mwdma_mask, &dev->udma_mask);
}

/**
 *	ata_dev_set_xfermode - Issue SET FEATURES - XFER MODE command
 *	@dev: Device to which command will be sent
 *
 *	Issue SET FEATURES - XFER MODE command to device @dev
 *	on port @ap.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	0 on success, AC_ERR_* mask otherwise.
 */

static unsigned int ata_dev_set_xfermode(struct ata_device *dev)
{
	struct ata_taskfile tf;
	unsigned int err_mask;

	/* set up set-features taskfile */
	DPRINTK("set features - xfer mode\n");

	ata_tf_init(dev, &tf);
	tf.command = ATA_CMD_SET_FEATURES;
	tf.feature = SETFEATURES_XFER;
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.protocol = ATA_PROT_NODATA;
	tf.nsect = dev->xfer_mode;

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0);

	DPRINTK("EXIT, err_mask=%x\n", err_mask);
	return err_mask;
}

/**
 *	ata_dev_init_params - Issue INIT DEV PARAMS command
 *	@dev: Device to which command will be sent
 *	@heads: Number of heads (taskfile parameter)
 *	@sectors: Number of sectors (taskfile parameter)
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, AC_ERR_* mask otherwise.
 */
static unsigned int ata_dev_init_params(struct ata_device *dev,
					u16 heads, u16 sectors)
{
	struct ata_taskfile tf;
	unsigned int err_mask;

	/* Number of sectors per track 1-255. Number of heads 1-16 */
	if (sectors < 1 || sectors > 255 || heads < 1 || heads > 16)
		return AC_ERR_INVALID;

	/* set up init dev params taskfile */
	DPRINTK("init dev params \n");

	ata_tf_init(dev, &tf);
	tf.command = ATA_CMD_INIT_DEV_PARAMS;
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.protocol = ATA_PROT_NODATA;
	tf.nsect = sectors;
	tf.device |= (heads - 1) & 0x0f; /* max head = num. of heads - 1 */

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0);

	DPRINTK("EXIT, err_mask=%x\n", err_mask);
	return err_mask;
}

/**
 *	ata_sg_clean - Unmap DMA memory associated with command
 *	@qc: Command containing DMA memory to be released
 *
 *	Unmap all mapped DMA memory associated with this command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_sg_clean(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scatterlist *sg = qc->__sg;
	int dir = qc->dma_dir;
	void *pad_buf = NULL;

	WARN_ON(!(qc->flags & ATA_QCFLAG_DMAMAP));
	WARN_ON(sg == NULL);

	if (qc->flags & ATA_QCFLAG_SINGLE)
		WARN_ON(qc->n_elem > 1);

	VPRINTK("unmapping %u sg elements\n", qc->n_elem);

	/* if we padded the buffer out to 32-bit bound, and data
	 * xfer direction is from-device, we must copy from the
	 * pad buffer back into the supplied buffer
	 */
	if (qc->pad_len && !(qc->tf.flags & ATA_TFLAG_WRITE))
		pad_buf = ap->pad + (qc->tag * ATA_DMA_PAD_SZ);

	if (qc->flags & ATA_QCFLAG_SG) {
		if (qc->n_elem)
			dma_unmap_sg(ap->dev, sg, qc->n_elem, dir);
		/* restore last sg */
		sg[qc->orig_n_elem - 1].length += qc->pad_len;
		if (pad_buf) {
			struct scatterlist *psg = &qc->pad_sgent;
			void *addr = kmap_atomic(psg->page, KM_IRQ0);
			memcpy(addr + psg->offset, pad_buf, qc->pad_len);
			kunmap_atomic(addr, KM_IRQ0);
		}
	} else {
		if (qc->n_elem)
			dma_unmap_single(ap->dev,
				sg_dma_address(&sg[0]), sg_dma_len(&sg[0]),
				dir);
		/* restore sg */
		sg->length += qc->pad_len;
		if (pad_buf)
			memcpy(qc->buf_virt + sg->length - qc->pad_len,
			       pad_buf, qc->pad_len);
	}

	qc->flags &= ~ATA_QCFLAG_DMAMAP;
	qc->__sg = NULL;
}

/**
 *	ata_fill_sg - Fill PCI IDE PRD table
 *	@qc: Metadata associated with taskfile to be transferred
 *
 *	Fill PCI IDE PRD (scatter-gather) table with segments
 *	associated with the current disk command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 */
static void ata_fill_sg(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scatterlist *sg;
	unsigned int idx;

	WARN_ON(qc->__sg == NULL);
	WARN_ON(qc->n_elem == 0 && qc->pad_len == 0);

	idx = 0;
	ata_for_each_sg(sg, qc) {
		u32 addr, offset;
		u32 sg_len, len;

		/* determine if physical DMA addr spans 64K boundary.
		 * Note h/w doesn't support 64-bit, so we unconditionally
		 * truncate dma_addr_t to u32.
		 */
		addr = (u32) sg_dma_address(sg);
		sg_len = sg_dma_len(sg);

		while (sg_len) {
			offset = addr & 0xffff;
			len = sg_len;
			if ((offset + sg_len) > 0x10000)
				len = 0x10000 - offset;

			ap->prd[idx].addr = cpu_to_le32(addr);
			ap->prd[idx].flags_len = cpu_to_le32(len & 0xffff);
			VPRINTK("PRD[%u] = (0x%X, 0x%X)\n", idx, addr, len);

			idx++;
			sg_len -= len;
			addr += len;
		}
	}

	if (idx)
		ap->prd[idx - 1].flags_len |= cpu_to_le32(ATA_PRD_EOT);
}
/**
 *	ata_check_atapi_dma - Check whether ATAPI DMA can be supported
 *	@qc: Metadata associated with taskfile to check
 *
 *	Allow low-level driver to filter ATA PACKET commands, returning
 *	a status indicating whether or not it is OK to use DMA for the
 *	supplied PACKET command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS: 0 when ATAPI DMA can be used
 *               nonzero otherwise
 */
int ata_check_atapi_dma(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	int rc = 0; /* Assume ATAPI DMA is OK by default */

	/* some drives can only do ATAPI DMA on read/write */
	if (unlikely(qc->dev->horkage & ATA_HORKAGE_DMA_RW_ONLY)) {
		struct scsi_cmnd *cmd = qc->scsicmd;
		u8 *scsicmd = cmd->cmnd;

		switch (scsicmd[0]) {
		case READ_10:
		case WRITE_10:
		case READ_12:
		case WRITE_12:
		case READ_6:
		case WRITE_6:
			/* atapi dma maybe ok */
			break;
		default:
			/* turn off atapi dma */
			return 1;
		}
	}

	if (ap->ops->check_atapi_dma)
		rc = ap->ops->check_atapi_dma(qc);

	return rc;
}
/**
 *	ata_qc_prep - Prepare taskfile for submission
 *	@qc: Metadata associated with taskfile to be prepared
 *
 *	Prepare ATA taskfile for submission.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_qc_prep(struct ata_queued_cmd *qc)
{
	if (!(qc->flags & ATA_QCFLAG_DMAMAP))
		return;

	ata_fill_sg(qc);
}

void ata_noop_qc_prep(struct ata_queued_cmd *qc) { }

/**
 *	ata_sg_init_one - Associate command with memory buffer
 *	@qc: Command to be associated
 *	@buf: Memory buffer
 *	@buflen: Length of memory buffer, in bytes.
 *
 *	Initialize the data-related elements of queued_cmd @qc
 *	to point to a single memory buffer, @buf of byte length @buflen.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */

void ata_sg_init_one(struct ata_queued_cmd *qc, void *buf, unsigned int buflen)
{
	qc->flags |= ATA_QCFLAG_SINGLE;

	qc->__sg = &qc->sgent;
	qc->n_elem = 1;
	qc->orig_n_elem = 1;
	qc->buf_virt = buf;
	qc->nbytes = buflen;

	sg_init_one(&qc->sgent, buf, buflen);
}

/**
 *	ata_sg_init - Associate command with scatter-gather table.
 *	@qc: Command to be associated
 *	@sg: Scatter-gather table.
 *	@n_elem: Number of elements in s/g table.
 *
 *	Initialize the data-related elements of queued_cmd @qc
 *	to point to a scatter-gather table @sg, containing @n_elem
 *	elements.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */

void ata_sg_init(struct ata_queued_cmd *qc, struct scatterlist *sg,
		 unsigned int n_elem)
{
	qc->flags |= ATA_QCFLAG_SG;
	qc->__sg = sg;
	qc->n_elem = n_elem;
	qc->orig_n_elem = n_elem;
}

/**
 *	ata_sg_setup_one - DMA-map the memory buffer associated with a command.
 *	@qc: Command with memory buffer to be mapped.
 *
 *	DMA-map the memory buffer associated with queued_cmd @qc.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, negative on error.
 */

static int ata_sg_setup_one(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	int dir = qc->dma_dir;
	struct scatterlist *sg = qc->__sg;
	dma_addr_t dma_address;
	int trim_sg = 0;

	/* we must lengthen transfers to end on a 32-bit boundary */
	qc->pad_len = sg->length & 3;
	if (qc->pad_len) {
		void *pad_buf = ap->pad + (qc->tag * ATA_DMA_PAD_SZ);
		struct scatterlist *psg = &qc->pad_sgent;

		WARN_ON(qc->dev->class != ATA_DEV_ATAPI);

		memset(pad_buf, 0, ATA_DMA_PAD_SZ);

		if (qc->tf.flags & ATA_TFLAG_WRITE)
			memcpy(pad_buf, qc->buf_virt + sg->length - qc->pad_len,
			       qc->pad_len);

		sg_dma_address(psg) = ap->pad_dma + (qc->tag * ATA_DMA_PAD_SZ);
		sg_dma_len(psg) = ATA_DMA_PAD_SZ;
		/* trim sg */
		sg->length -= qc->pad_len;
		if (sg->length == 0)
			trim_sg = 1;

		DPRINTK("padding done, sg->length=%u pad_len=%u\n",
			sg->length, qc->pad_len);
	}

	if (trim_sg) {
		qc->n_elem--;
		goto skip_map;
	}

	dma_address = dma_map_single(ap->dev, qc->buf_virt,
				     sg->length, dir);
	if (dma_mapping_error(dma_address)) {
		/* restore sg */
		sg->length += qc->pad_len;
		return -1;
	}

	sg_dma_address(sg) = dma_address;
	sg_dma_len(sg) = sg->length;

skip_map:
	DPRINTK("mapped buffer of %d bytes for %s\n", sg_dma_len(sg),
		qc->tf.flags & ATA_TFLAG_WRITE ? "write" : "read");

	return 0;
}

/**
 *	ata_sg_setup - DMA-map the scatter-gather table associated with a command.
 *	@qc: Command with scatter-gather table to be mapped.
 *
 *	DMA-map the scatter-gather table associated with queued_cmd @qc.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, negative on error.
 *
 */

static int ata_sg_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scatterlist *sg = qc->__sg;
	struct scatterlist *lsg = &sg[qc->n_elem - 1];
	int n_elem, pre_n_elem, dir, trim_sg = 0;

	VPRINTK("ENTER, ata%u\n", ap->print_id);
	WARN_ON(!(qc->flags & ATA_QCFLAG_SG));

	/* we must lengthen transfers to end on a 32-bit boundary */
	qc->pad_len = lsg->length & 3;
	if (qc->pad_len) {
		void *pad_buf = ap->pad + (qc->tag * ATA_DMA_PAD_SZ);
		struct scatterlist *psg = &qc->pad_sgent;
		unsigned int offset;

		WARN_ON(qc->dev->class != ATA_DEV_ATAPI);

		memset(pad_buf, 0, ATA_DMA_PAD_SZ);

		/*
		 * psg->page/offset are used to copy to-be-written
		 * data in this function or read data in ata_sg_clean.
		 */
		offset = lsg->offset + lsg->length - qc->pad_len;
		psg->page = nth_page(lsg->page, offset >> PAGE_SHIFT);
		psg->offset = offset_in_page(offset);

		if (qc->tf.flags & ATA_TFLAG_WRITE) {
			void *addr = kmap_atomic(psg->page, KM_IRQ0);
			memcpy(pad_buf, addr + psg->offset, qc->pad_len);
			kunmap_atomic(addr, KM_IRQ0);
		}

		sg_dma_address(psg) = ap->pad_dma + (qc->tag * ATA_DMA_PAD_SZ);
		sg_dma_len(psg) = ATA_DMA_PAD_SZ;
		/* trim last sg */
		lsg->length -= qc->pad_len;
		if (lsg->length == 0)
			trim_sg = 1;

		DPRINTK("padding done, sg[%d].length=%u pad_len=%u\n",
			qc->n_elem - 1, lsg->length, qc->pad_len);
	}

	pre_n_elem = qc->n_elem;
	if (trim_sg && pre_n_elem)
		pre_n_elem--;

	if (!pre_n_elem) {
		n_elem = 0;
		goto skip_map;
	}

	dir = qc->dma_dir;
	n_elem = dma_map_sg(ap->dev, sg, pre_n_elem, dir);
	if (n_elem < 1) {
		/* restore last sg */
		lsg->length += qc->pad_len;
		return -1;
	}

	DPRINTK("%d sg elements mapped\n", n_elem);

skip_map:
	qc->n_elem = n_elem;

	return 0;
}

/**
 *	swap_buf_le16 - swap halves of 16-bit words in place
 *	@buf:  Buffer to swap
 *	@buf_words:  Number of 16-bit words in buffer.
 *
 *	Swap halves of 16-bit words if needed to convert from
 *	little-endian byte order to native cpu byte order, or
 *	vice-versa.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void swap_buf_le16(u16 *buf, unsigned int buf_words)
{
#ifdef __BIG_ENDIAN
	unsigned int i;

	for (i = 0; i < buf_words; i++)
		buf[i] = le16_to_cpu(buf[i]);
#endif /* __BIG_ENDIAN */
}

/**
 *	ata_data_xfer - Transfer data by PIO
 *	@adev: device to target
 *	@buf: data buffer
 *	@buflen: buffer length
 *	@write_data: read/write
 *
 *	Transfer data from/to the device data register by PIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_data_xfer(struct ata_device *adev, unsigned char *buf,
		   unsigned int buflen, int write_data)
{
	struct ata_port *ap = adev->ap;
	unsigned int words = buflen >> 1;

	/* Transfer multiple of 2 bytes */
	if (write_data)
		iowrite16_rep(ap->ioaddr.data_addr, buf, words);
	else
		ioread16_rep(ap->ioaddr.data_addr, buf, words);

	/* Transfer trailing 1 byte, if any. */
	if (unlikely(buflen & 0x01)) {
		u16 align_buf[1] = { 0 };
		unsigned char *trailing_buf = buf + buflen - 1;

		if (write_data) {
			memcpy(align_buf, trailing_buf, 1);
			iowrite16(le16_to_cpu(align_buf[0]), ap->ioaddr.data_addr);
		} else {
			align_buf[0] = cpu_to_le16(ioread16(ap->ioaddr.data_addr));
			memcpy(trailing_buf, align_buf, 1);
		}
	}
}

/**
 *	ata_data_xfer_noirq - Transfer data by PIO
 *	@adev: device to target
 *	@buf: data buffer
 *	@buflen: buffer length
 *	@write_data: read/write
 *
 *	Transfer data from/to the device data register by PIO. Do the
 *	transfer with interrupts disabled.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_data_xfer_noirq(struct ata_device *adev, unsigned char *buf,
			 unsigned int buflen, int write_data)
{
	unsigned long flags;
	local_irq_save(flags);
	ata_data_xfer(adev, buf, buflen, write_data);
	local_irq_restore(flags);
}


/**
 *	ata_pio_sector - Transfer ATA_SECT_SIZE (512 bytes) of data.
 *	@qc: Command on going
 *
 *	Transfer ATA_SECT_SIZE of data from/to the ATA device.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_pio_sector(struct ata_queued_cmd *qc)
{
	int do_write = (qc->tf.flags & ATA_TFLAG_WRITE);
	struct scatterlist *sg = qc->__sg;
	struct ata_port *ap = qc->ap;
	struct page *page;
	unsigned int offset;
	unsigned char *buf;

	if (qc->curbytes == qc->nbytes - ATA_SECT_SIZE)
		ap->hsm_task_state = HSM_ST_LAST;

	page = sg[qc->cursg].page;
	offset = sg[qc->cursg].offset + qc->cursg_ofs;

	/* get the current page and offset */
	page = nth_page(page, (offset >> PAGE_SHIFT));
	offset %= PAGE_SIZE;

	DPRINTK("data %s\n", qc->tf.flags & ATA_TFLAG_WRITE ? "write" : "read");

	if (PageHighMem(page)) {
		unsigned long flags;

		/* FIXME: use a bounce buffer */
		local_irq_save(flags);
		buf = kmap_atomic(page, KM_IRQ0);

		/* do the actual data transfer */
		ap->ops->data_xfer(qc->dev, buf + offset, ATA_SECT_SIZE, do_write);

		kunmap_atomic(buf, KM_IRQ0);
		local_irq_restore(flags);
	} else {
		buf = page_address(page);
		ap->ops->data_xfer(qc->dev, buf + offset, ATA_SECT_SIZE, do_write);
	}

	qc->curbytes += ATA_SECT_SIZE;
	qc->cursg_ofs += ATA_SECT_SIZE;

	if (qc->cursg_ofs == (&sg[qc->cursg])->length) {
		qc->cursg++;
		qc->cursg_ofs = 0;
	}
}

/**
 *	ata_pio_sectors - Transfer one or many 512-byte sectors.
 *	@qc: Command on going
 *
 *	Transfer one or many ATA_SECT_SIZE of data from/to the
 *	ATA device for the DRQ request.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_pio_sectors(struct ata_queued_cmd *qc)
{
	if (is_multi_taskfile(&qc->tf)) {
		/* READ/WRITE MULTIPLE */
		unsigned int nsect;

		WARN_ON(qc->dev->multi_count == 0);

		nsect = min((qc->nbytes - qc->curbytes) / ATA_SECT_SIZE,
			    qc->dev->multi_count);
		while (nsect--)
			ata_pio_sector(qc);
	} else
		ata_pio_sector(qc);
}

/**
 *	atapi_send_cdb - Write CDB bytes to hardware
 *	@ap: Port to which ATAPI device is attached.
 *	@qc: Taskfile currently active
 *
 *	When device has indicated its readiness to accept
 *	a CDB, this function is called.  Send the CDB.
 *
 *	LOCKING:
 *	caller.
 */

static void atapi_send_cdb(struct ata_port *ap, struct ata_queued_cmd *qc)
{
	/* send SCSI cdb */
	DPRINTK("send cdb\n");
	WARN_ON(qc->dev->cdb_len < 12);

	ap->ops->data_xfer(qc->dev, qc->cdb, qc->dev->cdb_len, 1);
	ata_altstatus(ap); /* flush */

	switch (qc->tf.protocol) {
	case ATA_PROT_ATAPI:
		ap->hsm_task_state = HSM_ST;
		break;
	case ATA_PROT_ATAPI_NODATA:
		ap->hsm_task_state = HSM_ST_LAST;
		break;
	case ATA_PROT_ATAPI_DMA:
		ap->hsm_task_state = HSM_ST_LAST;
		/* initiate bmdma */
		ap->ops->bmdma_start(qc);
		break;
	}
}

/**
 *	__atapi_pio_bytes - Transfer data from/to the ATAPI device.
 *	@qc: Command on going
 *	@bytes: number of bytes
 *
 *	Transfer Transfer data from/to the ATAPI device.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 */

static void __atapi_pio_bytes(struct ata_queued_cmd *qc, unsigned int bytes)
{
	int do_write = (qc->tf.flags & ATA_TFLAG_WRITE);
	struct scatterlist *sg = qc->__sg;
	struct ata_port *ap = qc->ap;
	struct page *page;
	unsigned char *buf;
	unsigned int offset, count;

	if (qc->curbytes + bytes >= qc->nbytes)
		ap->hsm_task_state = HSM_ST_LAST;

next_sg:
	if (unlikely(qc->cursg >= qc->n_elem)) {
		/*
		 * The end of qc->sg is reached and the device expects
		 * more data to transfer. In order not to overrun qc->sg
		 * and fulfill length specified in the byte count register,
		 *    - for read case, discard trailing data from the device
		 *    - for write case, padding zero data to the device
		 */
		u16 pad_buf[1] = { 0 };
		unsigned int words = bytes >> 1;
		unsigned int i;

		if (words) /* warning if bytes > 1 */
			ata_dev_printk(qc->dev, KERN_WARNING,
				       "%u bytes trailing data\n", bytes);

		for (i = 0; i < words; i++)
			ap->ops->data_xfer(qc->dev, (unsigned char*)pad_buf, 2, do_write);

		ap->hsm_task_state = HSM_ST_LAST;
		return;
	}

	sg = &qc->__sg[qc->cursg];

	page = sg->page;
	offset = sg->offset + qc->cursg_ofs;

	/* get the current page and offset */
	page = nth_page(page, (offset >> PAGE_SHIFT));
	offset %= PAGE_SIZE;

	/* don't overrun current sg */
	count = min(sg->length - qc->cursg_ofs, bytes);

	/* don't cross page boundaries */
	count = min(count, (unsigned int)PAGE_SIZE - offset);

	DPRINTK("data %s\n", qc->tf.flags & ATA_TFLAG_WRITE ? "write" : "read");

	if (PageHighMem(page)) {
		unsigned long flags;

		/* FIXME: use bounce buffer */
		local_irq_save(flags);
		buf = kmap_atomic(page, KM_IRQ0);

		/* do the actual data transfer */
		ap->ops->data_xfer(qc->dev,  buf + offset, count, do_write);

		kunmap_atomic(buf, KM_IRQ0);
		local_irq_restore(flags);
	} else {
		buf = page_address(page);
		ap->ops->data_xfer(qc->dev,  buf + offset, count, do_write);
	}

	bytes -= count;
	qc->curbytes += count;
	qc->cursg_ofs += count;

	if (qc->cursg_ofs == sg->length) {
		qc->cursg++;
		qc->cursg_ofs = 0;
	}

	if (bytes)
		goto next_sg;
}

/**
 *	atapi_pio_bytes - Transfer data from/to the ATAPI device.
 *	@qc: Command on going
 *
 *	Transfer Transfer data from/to the ATAPI device.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void atapi_pio_bytes(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *dev = qc->dev;
	unsigned int ireason, bc_lo, bc_hi, bytes;
	int i_write, do_write = (qc->tf.flags & ATA_TFLAG_WRITE) ? 1 : 0;

	/* Abuse qc->result_tf for temp storage of intermediate TF
	 * here to save some kernel stack usage.
	 * For normal completion, qc->result_tf is not relevant. For
	 * error, qc->result_tf is later overwritten by ata_qc_complete().
	 * So, the correctness of qc->result_tf is not affected.
	 */
	ap->ops->tf_read(ap, &qc->result_tf);
	ireason = qc->result_tf.nsect;
	bc_lo = qc->result_tf.lbam;
	bc_hi = qc->result_tf.lbah;
	bytes = (bc_hi << 8) | bc_lo;

	/* shall be cleared to zero, indicating xfer of data */
	if (ireason & (1 << 0))
		goto err_out;

	/* make sure transfer direction matches expected */
	i_write = ((ireason & (1 << 1)) == 0) ? 1 : 0;
	if (do_write != i_write)
		goto err_out;

	VPRINTK("ata%u: xfering %d bytes\n", ap->print_id, bytes);

	__atapi_pio_bytes(qc, bytes);

	return;

err_out:
	ata_dev_printk(dev, KERN_INFO, "ATAPI check failed\n");
	qc->err_mask |= AC_ERR_HSM;
	ap->hsm_task_state = HSM_ST_ERR;
}

/**
 *	ata_hsm_ok_in_wq - Check if the qc can be handled in the workqueue.
 *	@ap: the target ata_port
 *	@qc: qc on going
 *
 *	RETURNS:
 *	1 if ok in workqueue, 0 otherwise.
 */

static inline int ata_hsm_ok_in_wq(struct ata_port *ap, struct ata_queued_cmd *qc)
{
	if (qc->tf.flags & ATA_TFLAG_POLLING)
		return 1;

	if (ap->hsm_task_state == HSM_ST_FIRST) {
		if (qc->tf.protocol == ATA_PROT_PIO &&
		    (qc->tf.flags & ATA_TFLAG_WRITE))
		    return 1;

		if (is_atapi_taskfile(&qc->tf) &&
		    !(qc->dev->flags & ATA_DFLAG_CDB_INTR))
			return 1;
	}

	return 0;
}

/**
 *	ata_hsm_qc_complete - finish a qc running on standard HSM
 *	@qc: Command to complete
 *	@in_wq: 1 if called from workqueue, 0 otherwise
 *
 *	Finish @qc which is running on standard HSM.
 *
 *	LOCKING:
 *	If @in_wq is zero, spin_lock_irqsave(host lock).
 *	Otherwise, none on entry and grabs host lock.
 */
static void ata_hsm_qc_complete(struct ata_queued_cmd *qc, int in_wq)
{
	struct ata_port *ap = qc->ap;
	unsigned long flags;

	if (ap->ops->error_handler) {
		if (in_wq) {
			spin_lock_irqsave(ap->lock, flags);

			/* EH might have kicked in while host lock is
			 * released.
			 */
			qc = ata_qc_from_tag(ap, qc->tag);
			if (qc) {
				if (likely(!(qc->err_mask & AC_ERR_HSM))) {
					ap->ops->irq_on(ap);
					ata_qc_complete(qc);
				} else
					ata_port_freeze(ap);
			}

			spin_unlock_irqrestore(ap->lock, flags);
		} else {
			if (likely(!(qc->err_mask & AC_ERR_HSM)))
				ata_qc_complete(qc);
			else
				ata_port_freeze(ap);
		}
	} else {
		if (in_wq) {
			spin_lock_irqsave(ap->lock, flags);
			ap->ops->irq_on(ap);
			ata_qc_complete(qc);
			spin_unlock_irqrestore(ap->lock, flags);
		} else
			ata_qc_complete(qc);
	}

	ata_altstatus(ap); /* flush */
}

/**
 *	ata_hsm_move - move the HSM to the next state.
 *	@ap: the target ata_port
 *	@qc: qc on going
 *	@status: current device status
 *	@in_wq: 1 if called from workqueue, 0 otherwise
 *
 *	RETURNS:
 *	1 when poll next status needed, 0 otherwise.
 */
int ata_hsm_move(struct ata_port *ap, struct ata_queued_cmd *qc,
		 u8 status, int in_wq)
{
	unsigned long flags = 0;
	int poll_next;

	WARN_ON((qc->flags & ATA_QCFLAG_ACTIVE) == 0);

	/* Make sure ata_qc_issue_prot() does not throw things
	 * like DMA polling into the workqueue. Notice that
	 * in_wq is not equivalent to (qc->tf.flags & ATA_TFLAG_POLLING).
	 */
	WARN_ON(in_wq != ata_hsm_ok_in_wq(ap, qc));

fsm_start:
	DPRINTK("ata%u: protocol %d task_state %d (dev_stat 0x%X)\n",
		ap->print_id, qc->tf.protocol, ap->hsm_task_state, status);

	switch (ap->hsm_task_state) {
	case HSM_ST_FIRST:
		/* Send first data block or PACKET CDB */

		/* If polling, we will stay in the work queue after
		 * sending the data. Otherwise, interrupt handler
		 * takes over after sending the data.
		 */
		poll_next = (qc->tf.flags & ATA_TFLAG_POLLING);

		/* check device status */
		if (unlikely((status & ATA_DRQ) == 0)) {
			/* handle BSY=0, DRQ=0 as error */
			if (likely(status & (ATA_ERR | ATA_DF)))
				/* device stops HSM for abort/error */
				qc->err_mask |= AC_ERR_DEV;
			else
				/* HSM violation. Let EH handle this */
				qc->err_mask |= AC_ERR_HSM;

			ap->hsm_task_state = HSM_ST_ERR;
			goto fsm_start;
		}

		/* Device should not ask for data transfer (DRQ=1)
		 * when it finds something wrong.
		 * We ignore DRQ here and stop the HSM by
		 * changing hsm_task_state to HSM_ST_ERR and
		 * let the EH abort the command or reset the device.
		 */
		if (unlikely(status & (ATA_ERR | ATA_DF))) {
			ata_port_printk(ap, KERN_WARNING, "DRQ=1 with device "
					"error, dev_stat 0x%X\n", status);
			qc->err_mask |= AC_ERR_HSM;
			ap->hsm_task_state = HSM_ST_ERR;
			goto fsm_start;
		}

		/* Send the CDB (atapi) or the first data block (ata pio out).
		 * During the state transition, interrupt handler shouldn't
		 * be invoked before the data transfer is complete and
		 * hsm_task_state is changed. Hence, the following locking.
		 */
		if (in_wq)
			spin_lock_irqsave(ap->lock, flags);

		if (qc->tf.protocol == ATA_PROT_PIO) {
			/* PIO data out protocol.
			 * send first data block.
			 */

			/* ata_pio_sectors() might change the state
			 * to HSM_ST_LAST. so, the state is changed here
			 * before ata_pio_sectors().
			 */
			ap->hsm_task_state = HSM_ST;
			ata_pio_sectors(qc);
			ata_altstatus(ap); /* flush */
		} else
			/* send CDB */
			atapi_send_cdb(ap, qc);

		if (in_wq)
			spin_unlock_irqrestore(ap->lock, flags);

		/* if polling, ata_pio_task() handles the rest.
		 * otherwise, interrupt handler takes over from here.
		 */
		break;

	case HSM_ST:
		/* complete command or read/write the data register */
		if (qc->tf.protocol == ATA_PROT_ATAPI) {
			/* ATAPI PIO protocol */
			if ((status & ATA_DRQ) == 0) {
				/* No more data to transfer or device error.
				 * Device error will be tagged in HSM_ST_LAST.
				 */
				ap->hsm_task_state = HSM_ST_LAST;
				goto fsm_start;
			}

			/* Device should not ask for data transfer (DRQ=1)
			 * when it finds something wrong.
			 * We ignore DRQ here and stop the HSM by
			 * changing hsm_task_state to HSM_ST_ERR and
			 * let the EH abort the command or reset the device.
			 */
			if (unlikely(status & (ATA_ERR | ATA_DF))) {
				ata_port_printk(ap, KERN_WARNING, "DRQ=1 with "
						"device error, dev_stat 0x%X\n",
						status);
				qc->err_mask |= AC_ERR_HSM;
				ap->hsm_task_state = HSM_ST_ERR;
				goto fsm_start;
			}

			atapi_pio_bytes(qc);

			if (unlikely(ap->hsm_task_state == HSM_ST_ERR))
				/* bad ireason reported by device */
				goto fsm_start;

		} else {
			/* ATA PIO protocol */
			if (unlikely((status & ATA_DRQ) == 0)) {
				/* handle BSY=0, DRQ=0 as error */
				if (likely(status & (ATA_ERR | ATA_DF)))
					/* device stops HSM for abort/error */
					qc->err_mask |= AC_ERR_DEV;
				else
					/* HSM violation. Let EH handle this.
					 * Phantom devices also trigger this
					 * condition.  Mark hint.
					 */
					qc->err_mask |= AC_ERR_HSM |
							AC_ERR_NODEV_HINT;

				ap->hsm_task_state = HSM_ST_ERR;
				goto fsm_start;
			}

			/* For PIO reads, some devices may ask for
			 * data transfer (DRQ=1) alone with ERR=1.
			 * We respect DRQ here and transfer one
			 * block of junk data before changing the
			 * hsm_task_state to HSM_ST_ERR.
			 *
			 * For PIO writes, ERR=1 DRQ=1 doesn't make
			 * sense since the data block has been
			 * transferred to the device.
			 */
			if (unlikely(status & (ATA_ERR | ATA_DF))) {
				/* data might be corrputed */
				qc->err_mask |= AC_ERR_DEV;

				if (!(qc->tf.flags & ATA_TFLAG_WRITE)) {
					ata_pio_sectors(qc);
					ata_altstatus(ap);
					status = ata_wait_idle(ap);
				}

				if (status & (ATA_BUSY | ATA_DRQ))
					qc->err_mask |= AC_ERR_HSM;

				/* ata_pio_sectors() might change the
				 * state to HSM_ST_LAST. so, the state
				 * is changed after ata_pio_sectors().
				 */
				ap->hsm_task_state = HSM_ST_ERR;
				goto fsm_start;
			}

			ata_pio_sectors(qc);

			if (ap->hsm_task_state == HSM_ST_LAST &&
			    (!(qc->tf.flags & ATA_TFLAG_WRITE))) {
				/* all data read */
				ata_altstatus(ap);
				status = ata_wait_idle(ap);
				goto fsm_start;
			}
		}

		ata_altstatus(ap); /* flush */
		poll_next = 1;
		break;

	case HSM_ST_LAST:
		if (unlikely(!ata_ok(status))) {
			qc->err_mask |= __ac_err_mask(status);
			ap->hsm_task_state = HSM_ST_ERR;
			goto fsm_start;
		}

		/* no more data to transfer */
		DPRINTK("ata%u: dev %u command complete, drv_stat 0x%x\n",
			ap->print_id, qc->dev->devno, status);

		WARN_ON(qc->err_mask);

		ap->hsm_task_state = HSM_ST_IDLE;

		/* complete taskfile transaction */
		ata_hsm_qc_complete(qc, in_wq);

		poll_next = 0;
		break;

	case HSM_ST_ERR:
		/* make sure qc->err_mask is available to
		 * know what's wrong and recover
		 */
		WARN_ON(qc->err_mask == 0);

		ap->hsm_task_state = HSM_ST_IDLE;

		/* complete taskfile transaction */
		ata_hsm_qc_complete(qc, in_wq);

		poll_next = 0;
		break;
	default:
		poll_next = 0;
		BUG();
	}

	return poll_next;
}

static void ata_pio_task(struct work_struct *work)
{
	struct ata_port *ap =
		container_of(work, struct ata_port, port_task.work);
	struct ata_queued_cmd *qc = ap->port_task_data;
	u8 status;
	int poll_next;

fsm_start:
	WARN_ON(ap->hsm_task_state == HSM_ST_IDLE);

	/*
	 * This is purely heuristic.  This is a fast path.
	 * Sometimes when we enter, BSY will be cleared in
	 * a chk-status or two.  If not, the drive is probably seeking
	 * or something.  Snooze for a couple msecs, then
	 * chk-status again.  If still busy, queue delayed work.
	 */
	status = ata_busy_wait(ap, ATA_BUSY, 5);
	if (status & ATA_BUSY) {
		msleep(2);
		status = ata_busy_wait(ap, ATA_BUSY, 10);
		if (status & ATA_BUSY) {
			ata_port_queue_task(ap, ata_pio_task, qc, ATA_SHORT_PAUSE);
			return;
		}
	}

	/* move the HSM */
	poll_next = ata_hsm_move(ap, qc, status, 1);

	/* another command or interrupt handler
	 * may be running at this point.
	 */
	if (poll_next)
		goto fsm_start;
}

/**
 *	ata_qc_new - Request an available ATA command, for queueing
 *	@ap: Port associated with device @dev
 *	@dev: Device from whom we request an available command structure
 *
 *	LOCKING:
 *	None.
 */

static struct ata_queued_cmd *ata_qc_new(struct ata_port *ap)
{
	struct ata_queued_cmd *qc = NULL;
	unsigned int i;

	/* no command while frozen */
	if (unlikely(ap->pflags & ATA_PFLAG_FROZEN))
		return NULL;

	/* the last tag is reserved for internal command. */
	for (i = 0; i < ATA_MAX_QUEUE - 1; i++)
		if (!test_and_set_bit(i, &ap->qc_allocated)) {
			qc = __ata_qc_from_tag(ap, i);
			break;
		}

	if (qc)
		qc->tag = i;

	return qc;
}

/**
 *	ata_qc_new_init - Request an available ATA command, and initialize it
 *	@dev: Device from whom we request an available command structure
 *
 *	LOCKING:
 *	None.
 */

struct ata_queued_cmd *ata_qc_new_init(struct ata_device *dev)
{
	struct ata_port *ap = dev->ap;
	struct ata_queued_cmd *qc;

	qc = ata_qc_new(ap);
	if (qc) {
		qc->scsicmd = NULL;
		qc->ap = ap;
		qc->dev = dev;

		ata_qc_reinit(qc);
	}

	return qc;
}

/**
 *	ata_qc_free - free unused ata_queued_cmd
 *	@qc: Command to complete
 *
 *	Designed to free unused ata_queued_cmd object
 *	in case something prevents using it.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_qc_free(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int tag;

	WARN_ON(qc == NULL);	/* ata_qc_from_tag _might_ return NULL */

	qc->flags = 0;
	tag = qc->tag;
	if (likely(ata_tag_valid(tag))) {
		qc->tag = ATA_TAG_POISON;
		clear_bit(tag, &ap->qc_allocated);
	}
}

void __ata_qc_complete(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	WARN_ON(qc == NULL);	/* ata_qc_from_tag _might_ return NULL */
	WARN_ON(!(qc->flags & ATA_QCFLAG_ACTIVE));

	if (likely(qc->flags & ATA_QCFLAG_DMAMAP))
		ata_sg_clean(qc);

	/* command should be marked inactive atomically with qc completion */
	if (qc->tf.protocol == ATA_PROT_NCQ)
		ap->sactive &= ~(1 << qc->tag);
	else
		ap->active_tag = ATA_TAG_POISON;

	/* atapi: mark qc as inactive to prevent the interrupt handler
	 * from completing the command twice later, before the error handler
	 * is called. (when rc != 0 and atapi request sense is needed)
	 */
	qc->flags &= ~ATA_QCFLAG_ACTIVE;
	ap->qc_active &= ~(1 << qc->tag);

	/* call completion callback */
	qc->complete_fn(qc);
}

static void fill_result_tf(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	qc->result_tf.flags = qc->tf.flags;
	ap->ops->tf_read(ap, &qc->result_tf);
}

/**
 *	ata_qc_complete - Complete an active ATA command
 *	@qc: Command to complete
 *	@err_mask: ATA Status register contents
 *
 *	Indicate to the mid and upper layers that an ATA
 *	command has completed, with either an ok or not-ok status.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_qc_complete(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	/* XXX: New EH and old EH use different mechanisms to
	 * synchronize EH with regular execution path.
	 *
	 * In new EH, a failed qc is marked with ATA_QCFLAG_FAILED.
	 * Normal execution path is responsible for not accessing a
	 * failed qc.  libata core enforces the rule by returning NULL
	 * from ata_qc_from_tag() for failed qcs.
	 *
	 * Old EH depends on ata_qc_complete() nullifying completion
	 * requests if ATA_QCFLAG_EH_SCHEDULED is set.  Old EH does
	 * not synchronize with interrupt handler.  Only PIO task is
	 * taken care of.
	 */
	if (ap->ops->error_handler) {
		WARN_ON(ap->pflags & ATA_PFLAG_FROZEN);

		if (unlikely(qc->err_mask))
			qc->flags |= ATA_QCFLAG_FAILED;

		if (unlikely(qc->flags & ATA_QCFLAG_FAILED)) {
			if (!ata_tag_internal(qc->tag)) {
				/* always fill result TF for failed qc */
				fill_result_tf(qc);
				ata_qc_schedule_eh(qc);
				return;
			}
		}

		/* read result TF if requested */
		if (qc->flags & ATA_QCFLAG_RESULT_TF)
			fill_result_tf(qc);

		__ata_qc_complete(qc);
	} else {
		if (qc->flags & ATA_QCFLAG_EH_SCHEDULED)
			return;

		/* read result TF if failed or requested */
		if (qc->err_mask || qc->flags & ATA_QCFLAG_RESULT_TF)
			fill_result_tf(qc);

		__ata_qc_complete(qc);
	}
}

/**
 *	ata_qc_complete_multiple - Complete multiple qcs successfully
 *	@ap: port in question
 *	@qc_active: new qc_active mask
 *	@finish_qc: LLDD callback invoked before completing a qc
 *
 *	Complete in-flight commands.  This functions is meant to be
 *	called from low-level driver's interrupt routine to complete
 *	requests normally.  ap->qc_active and @qc_active is compared
 *	and commands are completed accordingly.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Number of completed commands on success, -errno otherwise.
 */
int ata_qc_complete_multiple(struct ata_port *ap, u32 qc_active,
			     void (*finish_qc)(struct ata_queued_cmd *))
{
	int nr_done = 0;
	u32 done_mask;
	int i;

	done_mask = ap->qc_active ^ qc_active;

	if (unlikely(done_mask & qc_active)) {
		ata_port_printk(ap, KERN_ERR, "illegal qc_active transition "
				"(%08x->%08x)\n", ap->qc_active, qc_active);
		return -EINVAL;
	}

	for (i = 0; i < ATA_MAX_QUEUE; i++) {
		struct ata_queued_cmd *qc;

		if (!(done_mask & (1 << i)))
			continue;

		if ((qc = ata_qc_from_tag(ap, i))) {
			if (finish_qc)
				finish_qc(qc);
			ata_qc_complete(qc);
			nr_done++;
		}
	}

	return nr_done;
}

static inline int ata_should_dma_map(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	switch (qc->tf.protocol) {
	case ATA_PROT_NCQ:
	case ATA_PROT_DMA:
	case ATA_PROT_ATAPI_DMA:
		return 1;

	case ATA_PROT_ATAPI:
	case ATA_PROT_PIO:
		if (ap->flags & ATA_FLAG_PIO_DMA)
			return 1;

		/* fall through */

	default:
		return 0;
	}

	/* never reached */
}

/**
 *	ata_qc_issue - issue taskfile to device
 *	@qc: command to issue to device
 *
 *	Prepare an ATA command to submission to device.
 *	This includes mapping the data into a DMA-able
 *	area, filling in the S/G table, and finally
 *	writing the taskfile to hardware, starting the command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	/* Make sure only one non-NCQ command is outstanding.  The
	 * check is skipped for old EH because it reuses active qc to
	 * request ATAPI sense.
	 */
	WARN_ON(ap->ops->error_handler && ata_tag_valid(ap->active_tag));

	if (qc->tf.protocol == ATA_PROT_NCQ) {
		WARN_ON(ap->sactive & (1 << qc->tag));
		ap->sactive |= 1 << qc->tag;
	} else {
		WARN_ON(ap->sactive);
		ap->active_tag = qc->tag;
	}

	qc->flags |= ATA_QCFLAG_ACTIVE;
	ap->qc_active |= 1 << qc->tag;

	if (ata_should_dma_map(qc)) {
		if (qc->flags & ATA_QCFLAG_SG) {
			if (ata_sg_setup(qc))
				goto sg_err;
		} else if (qc->flags & ATA_QCFLAG_SINGLE) {
			if (ata_sg_setup_one(qc))
				goto sg_err;
		}
	} else {
		qc->flags &= ~ATA_QCFLAG_DMAMAP;
	}

	ap->ops->qc_prep(qc);

	qc->err_mask |= ap->ops->qc_issue(qc);
	if (unlikely(qc->err_mask))
		goto err;
	return;

sg_err:
	qc->flags &= ~ATA_QCFLAG_DMAMAP;
	qc->err_mask |= AC_ERR_SYSTEM;
err:
	ata_qc_complete(qc);
}

/**
 *	ata_qc_issue_prot - issue taskfile to device in proto-dependent manner
 *	@qc: command to issue to device
 *
 *	Using various libata functions and hooks, this function
 *	starts an ATA command.  ATA commands are grouped into
 *	classes called "protocols", and issuing each type of protocol
 *	is slightly different.
 *
 *	May be used as the qc_issue() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, AC_ERR_* mask on failure
 */

unsigned int ata_qc_issue_prot(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	/* Use polling pio if the LLD doesn't handle
	 * interrupt driven pio and atapi CDB interrupt.
	 */
	if (ap->flags & ATA_FLAG_PIO_POLLING) {
		switch (qc->tf.protocol) {
		case ATA_PROT_PIO:
		case ATA_PROT_NODATA:
		case ATA_PROT_ATAPI:
		case ATA_PROT_ATAPI_NODATA:
			qc->tf.flags |= ATA_TFLAG_POLLING;
			break;
		case ATA_PROT_ATAPI_DMA:
			if (qc->dev->flags & ATA_DFLAG_CDB_INTR)
				/* see ata_dma_blacklisted() */
				BUG();
			break;
		default:
			break;
		}
	}

	/* Some controllers show flaky interrupt behavior after
	 * setting xfer mode.  Use polling instead.
	 */
	if (unlikely(qc->tf.command == ATA_CMD_SET_FEATURES &&
		     qc->tf.feature == SETFEATURES_XFER) &&
	    (ap->flags & ATA_FLAG_SETXFER_POLLING))
		qc->tf.flags |= ATA_TFLAG_POLLING;

	/* select the device */
	ata_dev_select(ap, qc->dev->devno, 1, 0);

	/* start the command */
	switch (qc->tf.protocol) {
	case ATA_PROT_NODATA:
		if (qc->tf.flags & ATA_TFLAG_POLLING)
			ata_qc_set_polling(qc);

		ata_tf_to_host(ap, &qc->tf);
		ap->hsm_task_state = HSM_ST_LAST;

		if (qc->tf.flags & ATA_TFLAG_POLLING)
			ata_port_queue_task(ap, ata_pio_task, qc, 0);

		break;

	case ATA_PROT_DMA:
		WARN_ON(qc->tf.flags & ATA_TFLAG_POLLING);

		ap->ops->tf_load(ap, &qc->tf);	 /* load tf registers */
		ap->ops->bmdma_setup(qc);	    /* set up bmdma */
		ap->ops->bmdma_start(qc);	    /* initiate bmdma */
		ap->hsm_task_state = HSM_ST_LAST;
		break;

	case ATA_PROT_PIO:
		if (qc->tf.flags & ATA_TFLAG_POLLING)
			ata_qc_set_polling(qc);

		ata_tf_to_host(ap, &qc->tf);

		if (qc->tf.flags & ATA_TFLAG_WRITE) {
			/* PIO data out protocol */
			ap->hsm_task_state = HSM_ST_FIRST;
			ata_port_queue_task(ap, ata_pio_task, qc, 0);

			/* always send first data block using
			 * the ata_pio_task() codepath.
			 */
		} else {
			/* PIO data in protocol */
			ap->hsm_task_state = HSM_ST;

			if (qc->tf.flags & ATA_TFLAG_POLLING)
				ata_port_queue_task(ap, ata_pio_task, qc, 0);

			/* if polling, ata_pio_task() handles the rest.
			 * otherwise, interrupt handler takes over from here.
			 */
		}

		break;

	case ATA_PROT_ATAPI:
	case ATA_PROT_ATAPI_NODATA:
		if (qc->tf.flags & ATA_TFLAG_POLLING)
			ata_qc_set_polling(qc);

		ata_tf_to_host(ap, &qc->tf);

		ap->hsm_task_state = HSM_ST_FIRST;

		/* send cdb by polling if no cdb interrupt */
		if ((!(qc->dev->flags & ATA_DFLAG_CDB_INTR)) ||
		    (qc->tf.flags & ATA_TFLAG_POLLING))
			ata_port_queue_task(ap, ata_pio_task, qc, 0);
		break;

	case ATA_PROT_ATAPI_DMA:
		WARN_ON(qc->tf.flags & ATA_TFLAG_POLLING);

		ap->ops->tf_load(ap, &qc->tf);	 /* load tf registers */
		ap->ops->bmdma_setup(qc);	    /* set up bmdma */
		ap->hsm_task_state = HSM_ST_FIRST;

		/* send cdb by polling if no cdb interrupt */
		if (!(qc->dev->flags & ATA_DFLAG_CDB_INTR))
			ata_port_queue_task(ap, ata_pio_task, qc, 0);
		break;

	default:
		WARN_ON(1);
		return AC_ERR_SYSTEM;
	}

	return 0;
}

/**
 *	ata_host_intr - Handle host interrupt for given (port, task)
 *	@ap: Port on which interrupt arrived (possibly...)
 *	@qc: Taskfile currently active in engine
 *
 *	Handle host interrupt for given queued command.  Currently,
 *	only DMA interrupts are handled.  All other commands are
 *	handled via polling with interrupts disabled (nIEN bit).
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	One if interrupt was handled, zero if not (shared irq).
 */

inline unsigned int ata_host_intr (struct ata_port *ap,
				   struct ata_queued_cmd *qc)
{
	struct ata_eh_info *ehi = &ap->eh_info;
	u8 status, host_stat = 0;

	VPRINTK("ata%u: protocol %d task_state %d\n",
		ap->print_id, qc->tf.protocol, ap->hsm_task_state);

	/* Check whether we are expecting interrupt in this state */
	switch (ap->hsm_task_state) {
	case HSM_ST_FIRST:
		/* Some pre-ATAPI-4 devices assert INTRQ
		 * at this state when ready to receive CDB.
		 */

		/* Check the ATA_DFLAG_CDB_INTR flag is enough here.
		 * The flag was turned on only for atapi devices.
		 * No need to check is_atapi_taskfile(&qc->tf) again.
		 */
		if (!(qc->dev->flags & ATA_DFLAG_CDB_INTR))
			goto idle_irq;
		break;
	case HSM_ST_LAST:
		if (qc->tf.protocol == ATA_PROT_DMA ||
		    qc->tf.protocol == ATA_PROT_ATAPI_DMA) {
			/* check status of DMA engine */
			host_stat = ap->ops->bmdma_status(ap);
			VPRINTK("ata%u: host_stat 0x%X\n",
				ap->print_id, host_stat);

			/* if it's not our irq... */
			if (!(host_stat & ATA_DMA_INTR))
				goto idle_irq;

			/* before we do anything else, clear DMA-Start bit */
			ap->ops->bmdma_stop(qc);

			if (unlikely(host_stat & ATA_DMA_ERR)) {
				/* error when transfering data to/from memory */
				qc->err_mask |= AC_ERR_HOST_BUS;
				ap->hsm_task_state = HSM_ST_ERR;
			}
		}
		break;
	case HSM_ST:
		break;
	default:
		goto idle_irq;
	}

	/* check altstatus */
	status = ata_altstatus(ap);
	if (status & ATA_BUSY)
		goto idle_irq;

	/* check main status, clearing INTRQ */
	status = ata_chk_status(ap);
	if (unlikely(status & ATA_BUSY))
		goto idle_irq;

	/* ack bmdma irq events */
	ap->ops->irq_clear(ap);

	ata_hsm_move(ap, qc, status, 0);

	if (unlikely(qc->err_mask) && (qc->tf.protocol == ATA_PROT_DMA ||
				       qc->tf.protocol == ATA_PROT_ATAPI_DMA))
		ata_ehi_push_desc(ehi, "BMDMA stat 0x%x", host_stat);

	return 1;	/* irq handled */

idle_irq:
	ap->stats.idle_irq++;

#ifdef ATA_IRQ_TRAP
	if ((ap->stats.idle_irq % 1000) == 0) {
		ap->ops->irq_ack(ap, 0); /* debug trap */
		ata_port_printk(ap, KERN_WARNING, "irq trap\n");
		return 1;
	}
#endif
	return 0;	/* irq not handled */
}

/**
 *	ata_interrupt - Default ATA host interrupt handler
 *	@irq: irq line (unused)
 *	@dev_instance: pointer to our ata_host information structure
 *
 *	Default interrupt handler for PCI IDE devices.  Calls
 *	ata_host_intr() for each port that is not disabled.
 *
 *	LOCKING:
 *	Obtains host lock during operation.
 *
 *	RETURNS:
 *	IRQ_NONE or IRQ_HANDLED.
 */

irqreturn_t ata_interrupt (int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	unsigned int i;
	unsigned int handled = 0;
	unsigned long flags;

	/* TODO: make _irqsave conditional on x86 PCI IDE legacy mode */
	spin_lock_irqsave(&host->lock, flags);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap;

		ap = host->ports[i];
		if (ap &&
		    !(ap->flags & ATA_FLAG_DISABLED)) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING)) &&
			    (qc->flags & ATA_QCFLAG_ACTIVE))
				handled |= ata_host_intr(ap, qc);
		}
	}

	spin_unlock_irqrestore(&host->lock, flags);

	return IRQ_RETVAL(handled);
}

/**
 *	sata_scr_valid - test whether SCRs are accessible
 *	@ap: ATA port to test SCR accessibility for
 *
 *	Test whether SCRs are accessible for @ap.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	1 if SCRs are accessible, 0 otherwise.
 */
int sata_scr_valid(struct ata_port *ap)
{
	return ap->cbl == ATA_CBL_SATA && ap->ops->scr_read;
}

/**
 *	sata_scr_read - read SCR register of the specified port
 *	@ap: ATA port to read SCR for
 *	@reg: SCR to read
 *	@val: Place to store read value
 *
 *	Read SCR register @reg of @ap into *@val.  This function is
 *	guaranteed to succeed if the cable type of the port is SATA
 *	and the port implements ->scr_read.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure.
 */
int sata_scr_read(struct ata_port *ap, int reg, u32 *val)
{
	if (sata_scr_valid(ap)) {
		*val = ap->ops->scr_read(ap, reg);
		return 0;
	}
	return -EOPNOTSUPP;
}

/**
 *	sata_scr_write - write SCR register of the specified port
 *	@ap: ATA port to write SCR for
 *	@reg: SCR to write
 *	@val: value to write
 *
 *	Write @val to SCR register @reg of @ap.  This function is
 *	guaranteed to succeed if the cable type of the port is SATA
 *	and the port implements ->scr_read.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure.
 */
int sata_scr_write(struct ata_port *ap, int reg, u32 val)
{
	if (sata_scr_valid(ap)) {
		ap->ops->scr_write(ap, reg, val);
		return 0;
	}
	return -EOPNOTSUPP;
}

/**
 *	sata_scr_write_flush - write SCR register of the specified port and flush
 *	@ap: ATA port to write SCR for
 *	@reg: SCR to write
 *	@val: value to write
 *
 *	This function is identical to sata_scr_write() except that this
 *	function performs flush after writing to the register.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure.
 */
int sata_scr_write_flush(struct ata_port *ap, int reg, u32 val)
{
	if (sata_scr_valid(ap)) {
		ap->ops->scr_write(ap, reg, val);
		ap->ops->scr_read(ap, reg);
		return 0;
	}
	return -EOPNOTSUPP;
}

/**
 *	ata_port_online - test whether the given port is online
 *	@ap: ATA port to test
 *
 *	Test whether @ap is online.  Note that this function returns 0
 *	if online status of @ap cannot be obtained, so
 *	ata_port_online(ap) != !ata_port_offline(ap).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	1 if the port online status is available and online.
 */
int ata_port_online(struct ata_port *ap)
{
	u32 sstatus;

	if (!sata_scr_read(ap, SCR_STATUS, &sstatus) && (sstatus & 0xf) == 0x3)
		return 1;
	return 0;
}

/**
 *	ata_port_offline - test whether the given port is offline
 *	@ap: ATA port to test
 *
 *	Test whether @ap is offline.  Note that this function returns
 *	0 if offline status of @ap cannot be obtained, so
 *	ata_port_online(ap) != !ata_port_offline(ap).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	1 if the port offline status is available and offline.
 */
int ata_port_offline(struct ata_port *ap)
{
	u32 sstatus;

	if (!sata_scr_read(ap, SCR_STATUS, &sstatus) && (sstatus & 0xf) != 0x3)
		return 1;
	return 0;
}

int ata_flush_cache(struct ata_device *dev)
{
	unsigned int err_mask;
	u8 cmd;

	if (!ata_try_flush_cache(dev))
		return 0;

	if (dev->flags & ATA_DFLAG_FLUSH_EXT)
		cmd = ATA_CMD_FLUSH_EXT;
	else
		cmd = ATA_CMD_FLUSH;

	err_mask = ata_do_simple_cmd(dev, cmd);
	if (err_mask) {
		ata_dev_printk(dev, KERN_ERR, "failed to flush cache\n");
		return -EIO;
	}

	return 0;
}

#ifdef CONFIG_PM
static int ata_host_request_pm(struct ata_host *host, pm_message_t mesg,
			       unsigned int action, unsigned int ehi_flags,
			       int wait)
{
	unsigned long flags;
	int i, rc;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		/* Previous resume operation might still be in
		 * progress.  Wait for PM_PENDING to clear.
		 */
		if (ap->pflags & ATA_PFLAG_PM_PENDING) {
			ata_port_wait_eh(ap);
			WARN_ON(ap->pflags & ATA_PFLAG_PM_PENDING);
		}

		/* request PM ops to EH */
		spin_lock_irqsave(ap->lock, flags);

		ap->pm_mesg = mesg;
		if (wait) {
			rc = 0;
			ap->pm_result = &rc;
		}

		ap->pflags |= ATA_PFLAG_PM_PENDING;
		ap->eh_info.action |= action;
		ap->eh_info.flags |= ehi_flags;

		ata_port_schedule_eh(ap);

		spin_unlock_irqrestore(ap->lock, flags);

		/* wait and check result */
		if (wait) {
			ata_port_wait_eh(ap);
			WARN_ON(ap->pflags & ATA_PFLAG_PM_PENDING);
			if (rc)
				return rc;
		}
	}

	return 0;
}

/**
 *	ata_host_suspend - suspend host
 *	@host: host to suspend
 *	@mesg: PM message
 *
 *	Suspend @host.  Actual operation is performed by EH.  This
 *	function requests EH to perform PM operations and waits for EH
 *	to finish.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int ata_host_suspend(struct ata_host *host, pm_message_t mesg)
{
	int i, j, rc;

	rc = ata_host_request_pm(host, mesg, 0, ATA_EHI_QUIET, 1);
	if (rc)
		goto fail;

	/* EH is quiescent now.  Fail if we have any ready device.
	 * This happens if hotplug occurs between completion of device
	 * suspension and here.
	 */
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		for (j = 0; j < ATA_MAX_DEVICES; j++) {
			struct ata_device *dev = &ap->device[j];

			if (ata_dev_ready(dev)) {
				ata_port_printk(ap, KERN_WARNING,
						"suspend failed, device %d "
						"still active\n", dev->devno);
				rc = -EBUSY;
				goto fail;
			}
		}
	}

	host->dev->power.power_state = mesg;
	return 0;

 fail:
	ata_host_resume(host);
	return rc;
}

/**
 *	ata_host_resume - resume host
 *	@host: host to resume
 *
 *	Resume @host.  Actual operation is performed by EH.  This
 *	function requests EH to perform PM operations and returns.
 *	Note that all resume operations are performed parallely.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void ata_host_resume(struct ata_host *host)
{
	ata_host_request_pm(host, PMSG_ON, ATA_EH_SOFTRESET,
			    ATA_EHI_NO_AUTOPSY | ATA_EHI_QUIET, 0);
	host->dev->power.power_state = PMSG_ON;
}
#endif

/**
 *	ata_port_start - Set port up for dma.
 *	@ap: Port to initialize
 *
 *	Called just after data structures for each port are
 *	initialized.  Allocates space for PRD table.
 *
 *	May be used as the port_start() entry in ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
int ata_port_start(struct ata_port *ap)
{
	struct device *dev = ap->dev;
	int rc;

	ap->prd = dmam_alloc_coherent(dev, ATA_PRD_TBL_SZ, &ap->prd_dma,
				      GFP_KERNEL);
	if (!ap->prd)
		return -ENOMEM;

	rc = ata_pad_alloc(ap, dev);
	if (rc)
		return rc;

	DPRINTK("prd alloc, virt %p, dma %llx\n", ap->prd,
		(unsigned long long)ap->prd_dma);
	return 0;
}

/**
 *	ata_dev_init - Initialize an ata_device structure
 *	@dev: Device structure to initialize
 *
 *	Initialize @dev in preparation for probing.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_dev_init(struct ata_device *dev)
{
	struct ata_port *ap = dev->ap;
	unsigned long flags;

	/* SATA spd limit is bound to the first device */
	ap->sata_spd_limit = ap->hw_sata_spd_limit;

	/* High bits of dev->flags are used to record warm plug
	 * requests which occur asynchronously.  Synchronize using
	 * host lock.
	 */
	spin_lock_irqsave(ap->lock, flags);
	dev->flags &= ~ATA_DFLAG_INIT_MASK;
	spin_unlock_irqrestore(ap->lock, flags);

	memset((void *)dev + ATA_DEVICE_CLEAR_OFFSET, 0,
	       sizeof(*dev) - ATA_DEVICE_CLEAR_OFFSET);
	dev->pio_mask = UINT_MAX;
	dev->mwdma_mask = UINT_MAX;
	dev->udma_mask = UINT_MAX;
}

/**
 *	ata_port_init - Initialize an ata_port structure
 *	@ap: Structure to initialize
 *	@host: Collection of hosts to which @ap belongs
 *	@ent: Probe information provided by low-level driver
 *	@port_no: Port number associated with this ata_port
 *
 *	Initialize a new ata_port structure.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_port_init(struct ata_port *ap, struct ata_host *host,
		   const struct ata_probe_ent *ent, unsigned int port_no)
{
	unsigned int i;

	ap->lock = &host->lock;
	ap->flags = ATA_FLAG_DISABLED;
	ap->print_id = ata_print_id++;
	ap->ctl = ATA_DEVCTL_OBS;
	ap->host = host;
	ap->dev = ent->dev;
	ap->port_no = port_no;
	if (port_no == 1 && ent->pinfo2) {
		ap->pio_mask = ent->pinfo2->pio_mask;
		ap->mwdma_mask = ent->pinfo2->mwdma_mask;
		ap->udma_mask = ent->pinfo2->udma_mask;
		ap->flags |= ent->pinfo2->flags;
		ap->ops = ent->pinfo2->port_ops;
	} else {
		ap->pio_mask = ent->pio_mask;
		ap->mwdma_mask = ent->mwdma_mask;
		ap->udma_mask = ent->udma_mask;
		ap->flags |= ent->port_flags;
		ap->ops = ent->port_ops;
	}
	ap->hw_sata_spd_limit = UINT_MAX;
	ap->active_tag = ATA_TAG_POISON;
	ap->last_ctl = 0xFF;

#if defined(ATA_VERBOSE_DEBUG)
	/* turn on all debugging levels */
	ap->msg_enable = 0x00FF;
#elif defined(ATA_DEBUG)
	ap->msg_enable = ATA_MSG_DRV | ATA_MSG_INFO | ATA_MSG_CTL | ATA_MSG_WARN | ATA_MSG_ERR;
#else
	ap->msg_enable = ATA_MSG_DRV | ATA_MSG_ERR | ATA_MSG_WARN;
#endif

	INIT_DELAYED_WORK(&ap->port_task, NULL);
	INIT_DELAYED_WORK(&ap->hotplug_task, ata_scsi_hotplug);
	INIT_WORK(&ap->scsi_rescan_task, ata_scsi_dev_rescan);
	INIT_LIST_HEAD(&ap->eh_done_q);
	init_waitqueue_head(&ap->eh_wait_q);

	/* set cable type */
	ap->cbl = ATA_CBL_NONE;
	if (ap->flags & ATA_FLAG_SATA)
		ap->cbl = ATA_CBL_SATA;

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		struct ata_device *dev = &ap->device[i];
		dev->ap = ap;
		dev->devno = i;
		ata_dev_init(dev);
	}

#ifdef ATA_IRQ_TRAP
	ap->stats.unhandled_irq = 1;
	ap->stats.idle_irq = 1;
#endif

	memcpy(&ap->ioaddr, &ent->port[port_no], sizeof(struct ata_ioports));
}

/**
 *	ata_port_init_shost - Initialize SCSI host associated with ATA port
 *	@ap: ATA port to initialize SCSI host for
 *	@shost: SCSI host associated with @ap
 *
 *	Initialize SCSI host @shost associated with ATA port @ap.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static void ata_port_init_shost(struct ata_port *ap, struct Scsi_Host *shost)
{
	ap->scsi_host = shost;

	shost->unique_id = ap->print_id;
	shost->max_id = 16;
	shost->max_lun = 1;
	shost->max_channel = 1;
	shost->max_cmd_len = 12;
}

/**
 *	ata_port_add - Attach low-level ATA driver to system
 *	@ent: Information provided by low-level driver
 *	@host: Collections of ports to which we add
 *	@port_no: Port number associated with this host
 *
 *	Attach low-level ATA driver to system.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	New ata_port on success, for NULL on error.
 */
static struct ata_port * ata_port_add(const struct ata_probe_ent *ent,
				      struct ata_host *host,
				      unsigned int port_no)
{
	struct Scsi_Host *shost;
	struct ata_port *ap;

	DPRINTK("ENTER\n");

	if (!ent->port_ops->error_handler &&
	    !(ent->port_flags & (ATA_FLAG_SATA_RESET | ATA_FLAG_SRST))) {
		printk(KERN_ERR "ata%u: no reset mechanism available\n",
		       port_no);
		return NULL;
	}

	shost = scsi_host_alloc(ent->sht, sizeof(struct ata_port));
	if (!shost)
		return NULL;

	shost->transportt = &ata_scsi_transport_template;

	ap = ata_shost_to_port(shost);

	ata_port_init(ap, host, ent, port_no);
	ata_port_init_shost(ap, shost);

	return ap;
}

static void ata_host_release(struct device *gendev, void *res)
{
	struct ata_host *host = dev_get_drvdata(gendev);
	int i;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		if (ap && ap->ops->port_stop)
			ap->ops->port_stop(ap);
	}

	if (host->ops->host_stop)
		host->ops->host_stop(host);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		if (ap)
			scsi_host_put(ap->scsi_host);

		host->ports[i] = NULL;
	}

	dev_set_drvdata(gendev, NULL);
}

/**
 *	ata_sas_host_init - Initialize a host struct
 *	@host:	host to initialize
 *	@dev:	device host is attached to
 *	@flags:	host flags
 *	@ops:	port_ops
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 */

void ata_host_init(struct ata_host *host, struct device *dev,
		   unsigned long flags, const struct ata_port_operations *ops)
{
	spin_lock_init(&host->lock);
	host->dev = dev;
	host->flags = flags;
	host->ops = ops;
}

/**
 *	ata_device_add - Register hardware device with ATA and SCSI layers
 *	@ent: Probe information describing hardware device to be registered
 *
 *	This function processes the information provided in the probe
 *	information struct @ent, allocates the necessary ATA and SCSI
 *	host information structures, initializes them, and registers
 *	everything with requisite kernel subsystems.
 *
 *	This function requests irqs, probes the ATA bus, and probes
 *	the SCSI bus.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	Number of ports registered.  Zero on error (no ports registered).
 */
int ata_device_add(const struct ata_probe_ent *ent)
{
	unsigned int i;
	struct device *dev = ent->dev;
	struct ata_host *host;
	int rc;

	DPRINTK("ENTER\n");

	if (ent->irq == 0) {
		dev_printk(KERN_ERR, dev, "is not available: No interrupt assigned.\n");
		return 0;
	}

	if (!devres_open_group(dev, ata_device_add, GFP_KERNEL))
		return 0;

	/* alloc a container for our list of ATA ports (buses) */
	host = devres_alloc(ata_host_release, sizeof(struct ata_host) +
			    (ent->n_ports * sizeof(void *)), GFP_KERNEL);
	if (!host)
		goto err_out;
	devres_add(dev, host);
	dev_set_drvdata(dev, host);

	ata_host_init(host, dev, ent->_host_flags, ent->port_ops);
	host->n_ports = ent->n_ports;
	host->irq = ent->irq;
	host->irq2 = ent->irq2;
	host->iomap = ent->iomap;
	host->private_data = ent->private_data;

	/* register each port bound to this device */
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap;
		unsigned long xfer_mode_mask;
		int irq_line = ent->irq;

		ap = ata_port_add(ent, host, i);
		host->ports[i] = ap;
		if (!ap)
			goto err_out;

		/* dummy? */
		if (ent->dummy_port_mask & (1 << i)) {
			ata_port_printk(ap, KERN_INFO, "DUMMY\n");
			ap->ops = &ata_dummy_port_ops;
			continue;
		}

		/* start port */
		rc = ap->ops->port_start(ap);
		if (rc) {
			host->ports[i] = NULL;
			scsi_host_put(ap->scsi_host);
			goto err_out;
		}

		/* Report the secondary IRQ for second channel legacy */
		if (i == 1 && ent->irq2)
			irq_line = ent->irq2;

		xfer_mode_mask =(ap->udma_mask << ATA_SHIFT_UDMA) |
				(ap->mwdma_mask << ATA_SHIFT_MWDMA) |
				(ap->pio_mask << ATA_SHIFT_PIO);

		/* print per-port info to dmesg */
		ata_port_printk(ap, KERN_INFO, "%cATA max %s cmd 0x%p "
				"ctl 0x%p bmdma 0x%p irq %d\n",
				ap->flags & ATA_FLAG_SATA ? 'S' : 'P',
				ata_mode_string(xfer_mode_mask),
				ap->ioaddr.cmd_addr,
				ap->ioaddr.ctl_addr,
				ap->ioaddr.bmdma_addr,
				irq_line);

		/* freeze port before requesting IRQ */
		ata_eh_freeze_port(ap);
	}

	/* obtain irq, that may be shared between channels */
	rc = devm_request_irq(dev, ent->irq, ent->port_ops->irq_handler,
			      ent->irq_flags, DRV_NAME, host);
	if (rc) {
		dev_printk(KERN_ERR, dev, "irq %lu request failed: %d\n",
			   ent->irq, rc);
		goto err_out;
	}

	/* do we have a second IRQ for the other channel, eg legacy mode */
	if (ent->irq2) {
		/* We will get weird core code crashes later if this is true
		   so trap it now */
		BUG_ON(ent->irq == ent->irq2);

		rc = devm_request_irq(dev, ent->irq2,
				ent->port_ops->irq_handler, ent->irq_flags,
				DRV_NAME, host);
		if (rc) {
			dev_printk(KERN_ERR, dev, "irq %lu request failed: %d\n",
				   ent->irq2, rc);
			goto err_out;
		}
	}

	/* resource acquisition complete */
	devres_remove_group(dev, ata_device_add);

	/* perform each probe synchronously */
	DPRINTK("probe begin\n");
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		u32 scontrol;
		int rc;

		/* init sata_spd_limit to the current value */
		if (sata_scr_read(ap, SCR_CONTROL, &scontrol) == 0) {
			int spd = (scontrol >> 4) & 0xf;
			ap->hw_sata_spd_limit &= (1 << spd) - 1;
		}
		ap->sata_spd_limit = ap->hw_sata_spd_limit;

		rc = scsi_add_host(ap->scsi_host, dev);
		if (rc) {
			ata_port_printk(ap, KERN_ERR, "scsi_add_host failed\n");
			/* FIXME: do something useful here */
			/* FIXME: handle unconditional calls to
			 * scsi_scan_host and ata_host_remove, below,
			 * at the very least
			 */
		}

		if (ap->ops->error_handler) {
			struct ata_eh_info *ehi = &ap->eh_info;
			unsigned long flags;

			ata_port_probe(ap);

			/* kick EH for boot probing */
			spin_lock_irqsave(ap->lock, flags);

			ehi->probe_mask = (1 << ATA_MAX_DEVICES) - 1;
			ehi->action |= ATA_EH_SOFTRESET;
			ehi->flags |= ATA_EHI_NO_AUTOPSY | ATA_EHI_QUIET;

			ap->pflags |= ATA_PFLAG_LOADING;
			ata_port_schedule_eh(ap);

			spin_unlock_irqrestore(ap->lock, flags);

			/* wait for EH to finish */
			ata_port_wait_eh(ap);
		} else {
			DPRINTK("ata%u: bus probe begin\n", ap->print_id);
			rc = ata_bus_probe(ap);
			DPRINTK("ata%u: bus probe end\n", ap->print_id);

			if (rc) {
				/* FIXME: do something useful here?
				 * Current libata behavior will
				 * tear down everything when
				 * the module is removed
				 * or the h/w is unplugged.
				 */
			}
		}
	}

	/* probes are done, now scan each port's disk(s) */
	DPRINTK("host probe begin\n");
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		ata_scsi_scan_host(ap);
	}

	VPRINTK("EXIT, returning %u\n", ent->n_ports);
	return ent->n_ports; /* success */

 err_out:
	devres_release_group(dev, ata_device_add);
	VPRINTK("EXIT, returning %d\n", rc);
	return 0;
}

/**
 *	ata_port_detach - Detach ATA port in prepration of device removal
 *	@ap: ATA port to be detached
 *
 *	Detach all ATA devices and the associated SCSI devices of @ap;
 *	then, remove the associated SCSI host.  @ap is guaranteed to
 *	be quiescent on return from this function.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void ata_port_detach(struct ata_port *ap)
{
	unsigned long flags;
	int i;

	if (!ap->ops->error_handler)
		goto skip_eh;

	/* tell EH we're leaving & flush EH */
	spin_lock_irqsave(ap->lock, flags);
	ap->pflags |= ATA_PFLAG_UNLOADING;
	spin_unlock_irqrestore(ap->lock, flags);

	ata_port_wait_eh(ap);

	/* EH is now guaranteed to see UNLOADING, so no new device
	 * will be attached.  Disable all existing devices.
	 */
	spin_lock_irqsave(ap->lock, flags);

	for (i = 0; i < ATA_MAX_DEVICES; i++)
		ata_dev_disable(&ap->device[i]);

	spin_unlock_irqrestore(ap->lock, flags);

	/* Final freeze & EH.  All in-flight commands are aborted.  EH
	 * will be skipped and retrials will be terminated with bad
	 * target.
	 */
	spin_lock_irqsave(ap->lock, flags);
	ata_port_freeze(ap);	/* won't be thawed */
	spin_unlock_irqrestore(ap->lock, flags);

	ata_port_wait_eh(ap);

	/* Flush hotplug task.  The sequence is similar to
	 * ata_port_flush_task().
	 */
	flush_workqueue(ata_aux_wq);
	cancel_delayed_work(&ap->hotplug_task);
	flush_workqueue(ata_aux_wq);

 skip_eh:
	/* remove the associated SCSI host */
	scsi_remove_host(ap->scsi_host);
}

/**
 *	ata_host_detach - Detach all ports of an ATA host
 *	@host: Host to detach
 *
 *	Detach all ports of @host.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void ata_host_detach(struct ata_host *host)
{
	int i;

	for (i = 0; i < host->n_ports; i++)
		ata_port_detach(host->ports[i]);
}

struct ata_probe_ent *
ata_probe_ent_alloc(struct device *dev, const struct ata_port_info *port)
{
	struct ata_probe_ent *probe_ent;

	probe_ent = devm_kzalloc(dev, sizeof(*probe_ent), GFP_KERNEL);
	if (!probe_ent) {
		printk(KERN_ERR DRV_NAME "(%s): out of memory\n",
		       kobject_name(&(dev->kobj)));
		return NULL;
	}

	INIT_LIST_HEAD(&probe_ent->node);
	probe_ent->dev = dev;

	probe_ent->sht = port->sht;
	probe_ent->port_flags = port->flags;
	probe_ent->pio_mask = port->pio_mask;
	probe_ent->mwdma_mask = port->mwdma_mask;
	probe_ent->udma_mask = port->udma_mask;
	probe_ent->port_ops = port->port_ops;
	probe_ent->private_data = port->private_data;

	return probe_ent;
}

/**
 *	ata_std_ports - initialize ioaddr with standard port offsets.
 *	@ioaddr: IO address structure to be initialized
 *
 *	Utility function which initializes data_addr, error_addr,
 *	feature_addr, nsect_addr, lbal_addr, lbam_addr, lbah_addr,
 *	device_addr, status_addr, and command_addr to standard offsets
 *	relative to cmd_addr.
 *
 *	Does not set ctl_addr, altstatus_addr, bmdma_addr, or scr_addr.
 */

void ata_std_ports(struct ata_ioports *ioaddr)
{
	ioaddr->data_addr = ioaddr->cmd_addr + ATA_REG_DATA;
	ioaddr->error_addr = ioaddr->cmd_addr + ATA_REG_ERR;
	ioaddr->feature_addr = ioaddr->cmd_addr + ATA_REG_FEATURE;
	ioaddr->nsect_addr = ioaddr->cmd_addr + ATA_REG_NSECT;
	ioaddr->lbal_addr = ioaddr->cmd_addr + ATA_REG_LBAL;
	ioaddr->lbam_addr = ioaddr->cmd_addr + ATA_REG_LBAM;
	ioaddr->lbah_addr = ioaddr->cmd_addr + ATA_REG_LBAH;
	ioaddr->device_addr = ioaddr->cmd_addr + ATA_REG_DEVICE;
	ioaddr->status_addr = ioaddr->cmd_addr + ATA_REG_STATUS;
	ioaddr->command_addr = ioaddr->cmd_addr + ATA_REG_CMD;
}


#ifdef CONFIG_PCI

/**
 *	ata_pci_remove_one - PCI layer callback for device removal
 *	@pdev: PCI device that was removed
 *
 *	PCI layer indicates to libata via this hook that hot-unplug or
 *	module unload event has occurred.  Detach all ports.  Resource
 *	release is handled via devres.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 */
void ata_pci_remove_one(struct pci_dev *pdev)
{
	struct device *dev = pci_dev_to_dev(pdev);
	struct ata_host *host = dev_get_drvdata(dev);

	ata_host_detach(host);
}

/* move to PCI subsystem */
int pci_test_config_bits(struct pci_dev *pdev, const struct pci_bits *bits)
{
	unsigned long tmp = 0;

	switch (bits->width) {
	case 1: {
		u8 tmp8 = 0;
		pci_read_config_byte(pdev, bits->reg, &tmp8);
		tmp = tmp8;
		break;
	}
	case 2: {
		u16 tmp16 = 0;
		pci_read_config_word(pdev, bits->reg, &tmp16);
		tmp = tmp16;
		break;
	}
	case 4: {
		u32 tmp32 = 0;
		pci_read_config_dword(pdev, bits->reg, &tmp32);
		tmp = tmp32;
		break;
	}

	default:
		return -EINVAL;
	}

	tmp &= bits->mask;

	return (tmp == bits->val) ? 1 : 0;
}

#ifdef CONFIG_PM
void ata_pci_device_do_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	pci_save_state(pdev);
	pci_disable_device(pdev);

	if (mesg.event == PM_EVENT_SUSPEND)
		pci_set_power_state(pdev, PCI_D3hot);
}

int ata_pci_device_do_resume(struct pci_dev *pdev)
{
	int rc;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	rc = pcim_enable_device(pdev);
	if (rc) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "failed to enable device after resume (%d)\n", rc);
		return rc;
	}

	pci_set_master(pdev);
	return 0;
}

int ata_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int rc = 0;

	rc = ata_host_suspend(host, mesg);
	if (rc)
		return rc;

	ata_pci_device_do_suspend(pdev, mesg);

	return 0;
}

int ata_pci_device_resume(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc == 0)
		ata_host_resume(host);
	return rc;
}
#endif /* CONFIG_PM */

#endif /* CONFIG_PCI */


static int __init ata_init(void)
{
	ata_probe_timeout *= HZ;
	ata_wq = create_workqueue("ata");
	if (!ata_wq)
		return -ENOMEM;

	ata_aux_wq = create_singlethread_workqueue("ata_aux");
	if (!ata_aux_wq) {
		destroy_workqueue(ata_wq);
		return -ENOMEM;
	}

	printk(KERN_DEBUG "libata version " DRV_VERSION " loaded.\n");
	return 0;
}

static void __exit ata_exit(void)
{
	destroy_workqueue(ata_wq);
	destroy_workqueue(ata_aux_wq);
}

subsys_initcall(ata_init);
module_exit(ata_exit);

static unsigned long ratelimit_time;
static DEFINE_SPINLOCK(ata_ratelimit_lock);

int ata_ratelimit(void)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&ata_ratelimit_lock, flags);

	if (time_after(jiffies, ratelimit_time)) {
		rc = 1;
		ratelimit_time = jiffies + (HZ/5);
	} else
		rc = 0;

	spin_unlock_irqrestore(&ata_ratelimit_lock, flags);

	return rc;
}

/**
 *	ata_wait_register - wait until register value changes
 *	@reg: IO-mapped register
 *	@mask: Mask to apply to read register value
 *	@val: Wait condition
 *	@interval_msec: polling interval in milliseconds
 *	@timeout_msec: timeout in milliseconds
 *
 *	Waiting for some bits of register to change is a common
 *	operation for ATA controllers.  This function reads 32bit LE
 *	IO-mapped register @reg and tests for the following condition.
 *
 *	(*@reg & mask) != val
 *
 *	If the condition is met, it returns; otherwise, the process is
 *	repeated after @interval_msec until timeout.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	The final register value.
 */
u32 ata_wait_register(void __iomem *reg, u32 mask, u32 val,
		      unsigned long interval_msec,
		      unsigned long timeout_msec)
{
	unsigned long timeout;
	u32 tmp;

	tmp = ioread32(reg);

	/* Calculate timeout _after_ the first read to make sure
	 * preceding writes reach the controller before starting to
	 * eat away the timeout.
	 */
	timeout = jiffies + (timeout_msec * HZ) / 1000;

	while ((tmp & mask) == val && time_before(jiffies, timeout)) {
		msleep(interval_msec);
		tmp = ioread32(reg);
	}

	return tmp;
}

/*
 * Dummy port_ops
 */
static void ata_dummy_noret(struct ata_port *ap)	{ }
static int ata_dummy_ret0(struct ata_port *ap)		{ return 0; }
static void ata_dummy_qc_noret(struct ata_queued_cmd *qc) { }

static u8 ata_dummy_check_status(struct ata_port *ap)
{
	return ATA_DRDY;
}

static unsigned int ata_dummy_qc_issue(struct ata_queued_cmd *qc)
{
	return AC_ERR_SYSTEM;
}

const struct ata_port_operations ata_dummy_port_ops = {
	.port_disable		= ata_port_disable,
	.check_status		= ata_dummy_check_status,
	.check_altstatus	= ata_dummy_check_status,
	.dev_select		= ata_noop_dev_select,
	.qc_prep		= ata_noop_qc_prep,
	.qc_issue		= ata_dummy_qc_issue,
	.freeze			= ata_dummy_noret,
	.thaw			= ata_dummy_noret,
	.error_handler		= ata_dummy_noret,
	.post_internal_cmd	= ata_dummy_qc_noret,
	.irq_clear		= ata_dummy_noret,
	.port_start		= ata_dummy_ret0,
	.port_stop		= ata_dummy_noret,
};

/*
 * libata is essentially a library of internal helper functions for
 * low-level ATA host controller drivers.  As such, the API/ABI is
 * likely to change as new drivers are added and updated.
 * Do not depend on ABI/API stability.
 */

EXPORT_SYMBOL_GPL(sata_deb_timing_normal);
EXPORT_SYMBOL_GPL(sata_deb_timing_hotplug);
EXPORT_SYMBOL_GPL(sata_deb_timing_long);
EXPORT_SYMBOL_GPL(ata_dummy_port_ops);
EXPORT_SYMBOL_GPL(ata_std_bios_param);
EXPORT_SYMBOL_GPL(ata_std_ports);
EXPORT_SYMBOL_GPL(ata_host_init);
EXPORT_SYMBOL_GPL(ata_device_add);
EXPORT_SYMBOL_GPL(ata_host_detach);
EXPORT_SYMBOL_GPL(ata_sg_init);
EXPORT_SYMBOL_GPL(ata_sg_init_one);
EXPORT_SYMBOL_GPL(ata_hsm_move);
EXPORT_SYMBOL_GPL(ata_qc_complete);
EXPORT_SYMBOL_GPL(ata_qc_complete_multiple);
EXPORT_SYMBOL_GPL(ata_qc_issue_prot);
EXPORT_SYMBOL_GPL(ata_tf_load);
EXPORT_SYMBOL_GPL(ata_tf_read);
EXPORT_SYMBOL_GPL(ata_noop_dev_select);
EXPORT_SYMBOL_GPL(ata_std_dev_select);
EXPORT_SYMBOL_GPL(ata_tf_to_fis);
EXPORT_SYMBOL_GPL(ata_tf_from_fis);
EXPORT_SYMBOL_GPL(ata_check_status);
EXPORT_SYMBOL_GPL(ata_altstatus);
EXPORT_SYMBOL_GPL(ata_exec_command);
EXPORT_SYMBOL_GPL(ata_port_start);
EXPORT_SYMBOL_GPL(ata_interrupt);
EXPORT_SYMBOL_GPL(ata_data_xfer);
EXPORT_SYMBOL_GPL(ata_data_xfer_noirq);
EXPORT_SYMBOL_GPL(ata_qc_prep);
EXPORT_SYMBOL_GPL(ata_noop_qc_prep);
EXPORT_SYMBOL_GPL(ata_bmdma_setup);
EXPORT_SYMBOL_GPL(ata_bmdma_start);
EXPORT_SYMBOL_GPL(ata_bmdma_irq_clear);
EXPORT_SYMBOL_GPL(ata_bmdma_status);
EXPORT_SYMBOL_GPL(ata_bmdma_stop);
EXPORT_SYMBOL_GPL(ata_bmdma_freeze);
EXPORT_SYMBOL_GPL(ata_bmdma_thaw);
EXPORT_SYMBOL_GPL(ata_bmdma_drive_eh);
EXPORT_SYMBOL_GPL(ata_bmdma_error_handler);
EXPORT_SYMBOL_GPL(ata_bmdma_post_internal_cmd);
EXPORT_SYMBOL_GPL(ata_port_probe);
EXPORT_SYMBOL_GPL(ata_dev_disable);
EXPORT_SYMBOL_GPL(sata_set_spd);
EXPORT_SYMBOL_GPL(sata_phy_debounce);
EXPORT_SYMBOL_GPL(sata_phy_resume);
EXPORT_SYMBOL_GPL(sata_phy_reset);
EXPORT_SYMBOL_GPL(__sata_phy_reset);
EXPORT_SYMBOL_GPL(ata_bus_reset);
EXPORT_SYMBOL_GPL(ata_std_prereset);
EXPORT_SYMBOL_GPL(ata_std_softreset);
EXPORT_SYMBOL_GPL(sata_port_hardreset);
EXPORT_SYMBOL_GPL(sata_std_hardreset);
EXPORT_SYMBOL_GPL(ata_std_postreset);
EXPORT_SYMBOL_GPL(ata_dev_classify);
EXPORT_SYMBOL_GPL(ata_dev_pair);
EXPORT_SYMBOL_GPL(ata_port_disable);
EXPORT_SYMBOL_GPL(ata_ratelimit);
EXPORT_SYMBOL_GPL(ata_wait_register);
EXPORT_SYMBOL_GPL(ata_busy_sleep);
EXPORT_SYMBOL_GPL(ata_port_queue_task);
EXPORT_SYMBOL_GPL(ata_scsi_ioctl);
EXPORT_SYMBOL_GPL(ata_scsi_queuecmd);
EXPORT_SYMBOL_GPL(ata_scsi_slave_config);
EXPORT_SYMBOL_GPL(ata_scsi_slave_destroy);
EXPORT_SYMBOL_GPL(ata_scsi_change_queue_depth);
EXPORT_SYMBOL_GPL(ata_host_intr);
EXPORT_SYMBOL_GPL(sata_scr_valid);
EXPORT_SYMBOL_GPL(sata_scr_read);
EXPORT_SYMBOL_GPL(sata_scr_write);
EXPORT_SYMBOL_GPL(sata_scr_write_flush);
EXPORT_SYMBOL_GPL(ata_port_online);
EXPORT_SYMBOL_GPL(ata_port_offline);
#ifdef CONFIG_PM
EXPORT_SYMBOL_GPL(ata_host_suspend);
EXPORT_SYMBOL_GPL(ata_host_resume);
#endif /* CONFIG_PM */
EXPORT_SYMBOL_GPL(ata_id_string);
EXPORT_SYMBOL_GPL(ata_id_c_string);
EXPORT_SYMBOL_GPL(ata_id_to_dma_mode);
EXPORT_SYMBOL_GPL(ata_device_blacklisted);
EXPORT_SYMBOL_GPL(ata_scsi_simulate);

EXPORT_SYMBOL_GPL(ata_pio_need_iordy);
EXPORT_SYMBOL_GPL(ata_timing_compute);
EXPORT_SYMBOL_GPL(ata_timing_merge);

#ifdef CONFIG_PCI
EXPORT_SYMBOL_GPL(pci_test_config_bits);
EXPORT_SYMBOL_GPL(ata_pci_init_native_mode);
EXPORT_SYMBOL_GPL(ata_pci_init_one);
EXPORT_SYMBOL_GPL(ata_pci_remove_one);
#ifdef CONFIG_PM
EXPORT_SYMBOL_GPL(ata_pci_device_do_suspend);
EXPORT_SYMBOL_GPL(ata_pci_device_do_resume);
EXPORT_SYMBOL_GPL(ata_pci_device_suspend);
EXPORT_SYMBOL_GPL(ata_pci_device_resume);
#endif /* CONFIG_PM */
EXPORT_SYMBOL_GPL(ata_pci_default_filter);
EXPORT_SYMBOL_GPL(ata_pci_clear_simplex);
#endif /* CONFIG_PCI */

#ifdef CONFIG_PM
EXPORT_SYMBOL_GPL(ata_scsi_device_suspend);
EXPORT_SYMBOL_GPL(ata_scsi_device_resume);
#endif /* CONFIG_PM */

EXPORT_SYMBOL_GPL(ata_eng_timeout);
EXPORT_SYMBOL_GPL(ata_port_schedule_eh);
EXPORT_SYMBOL_GPL(ata_port_abort);
EXPORT_SYMBOL_GPL(ata_port_freeze);
EXPORT_SYMBOL_GPL(ata_eh_freeze_port);
EXPORT_SYMBOL_GPL(ata_eh_thaw_port);
EXPORT_SYMBOL_GPL(ata_eh_qc_complete);
EXPORT_SYMBOL_GPL(ata_eh_qc_retry);
EXPORT_SYMBOL_GPL(ata_do_eh);
EXPORT_SYMBOL_GPL(ata_irq_on);
EXPORT_SYMBOL_GPL(ata_dummy_irq_on);
EXPORT_SYMBOL_GPL(ata_irq_ack);
EXPORT_SYMBOL_GPL(ata_dummy_irq_ack);
EXPORT_SYMBOL_GPL(ata_dev_try_classify);
