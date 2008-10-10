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
 *  Standards documents from:
 *	http://www.t13.org (ATA standards, PCI DMA IDE spec)
 *	http://www.t10.org (SCSI MMC - for ATAPI MMC)
 *	http://www.sata-io.org (SATA)
 *	http://www.compactflash.org (CF)
 *	http://www.qic.org (QIC157 - Tape and DSC)
 *	http://www.ce-ata.org (CE-ATA: not supported)
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>
#include <linux/scatterlist.h>
#include <linux/io.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <asm/byteorder.h>
#include <linux/cdrom.h>

#include "libata.h"


/* debounce timing parameters in msecs { interval, duration, timeout } */
const unsigned long sata_deb_timing_normal[]		= {   5,  100, 2000 };
const unsigned long sata_deb_timing_hotplug[]		= {  25,  500, 2000 };
const unsigned long sata_deb_timing_long[]		= { 100, 2000, 5000 };

const struct ata_port_operations ata_base_port_ops = {
	.prereset		= ata_std_prereset,
	.postreset		= ata_std_postreset,
	.error_handler		= ata_std_error_handler,
};

const struct ata_port_operations sata_port_ops = {
	.inherits		= &ata_base_port_ops,

	.qc_defer		= ata_std_qc_defer,
	.hardreset		= sata_std_hardreset,
};

static unsigned int ata_dev_init_params(struct ata_device *dev,
					u16 heads, u16 sectors);
static unsigned int ata_dev_set_xfermode(struct ata_device *dev);
static unsigned int ata_dev_set_feature(struct ata_device *dev,
					u8 enable, u8 feature);
static void ata_dev_xfermask(struct ata_device *dev);
static unsigned long ata_dev_blacklisted(const struct ata_device *dev);

unsigned int ata_print_id = 1;
static struct workqueue_struct *ata_wq;

struct workqueue_struct *ata_aux_wq;

struct ata_force_param {
	const char	*name;
	unsigned int	cbl;
	int		spd_limit;
	unsigned long	xfer_mask;
	unsigned int	horkage_on;
	unsigned int	horkage_off;
	unsigned int	lflags;
};

struct ata_force_ent {
	int			port;
	int			device;
	struct ata_force_param	param;
};

static struct ata_force_ent *ata_force_tbl;
static int ata_force_tbl_size;

static char ata_force_param_buf[PAGE_SIZE] __initdata;
/* param_buf is thrown away after initialization, disallow read */
module_param_string(force, ata_force_param_buf, sizeof(ata_force_param_buf), 0);
MODULE_PARM_DESC(force, "Force ATA configurations including cable type, link speed and transfer mode (see Documentation/kernel-parameters.txt for details)");

static int atapi_enabled = 1;
module_param(atapi_enabled, int, 0444);
MODULE_PARM_DESC(atapi_enabled, "Enable discovery of ATAPI devices (0=off, 1=on)");

static int atapi_dmadir = 0;
module_param(atapi_dmadir, int, 0444);
MODULE_PARM_DESC(atapi_dmadir, "Enable ATAPI DMADIR bridge support (0=off, 1=on)");

int atapi_passthru16 = 1;
module_param(atapi_passthru16, int, 0444);
MODULE_PARM_DESC(atapi_passthru16, "Enable ATA_16 passthru for ATAPI devices; on by default (0=off, 1=on)");

int libata_fua = 0;
module_param_named(fua, libata_fua, int, 0444);
MODULE_PARM_DESC(fua, "FUA support (0=off, 1=on)");

static int ata_ignore_hpa;
module_param_named(ignore_hpa, ata_ignore_hpa, int, 0644);
MODULE_PARM_DESC(ignore_hpa, "Ignore HPA limit (0=keep BIOS limits, 1=ignore limits, using full disk)");

static int libata_dma_mask = ATA_DMA_MASK_ATA|ATA_DMA_MASK_ATAPI|ATA_DMA_MASK_CFA;
module_param_named(dma, libata_dma_mask, int, 0444);
MODULE_PARM_DESC(dma, "DMA enable/disable (0x1==ATA, 0x2==ATAPI, 0x4==CF)");

static int ata_probe_timeout;
module_param(ata_probe_timeout, int, 0444);
MODULE_PARM_DESC(ata_probe_timeout, "Set ATA probing timeout (seconds)");

int libata_noacpi = 0;
module_param_named(noacpi, libata_noacpi, int, 0444);
MODULE_PARM_DESC(noacpi, "Disables the use of ACPI in probe/suspend/resume when set");

int libata_allow_tpm = 0;
module_param_named(allow_tpm, libata_allow_tpm, int, 0444);
MODULE_PARM_DESC(allow_tpm, "Permit the use of TPM commands");

MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("Library module for ATA devices");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);


/*
 * Iterator helpers.  Don't use directly.
 *
 * LOCKING:
 * Host lock or EH context.
 */
struct ata_link *__ata_port_next_link(struct ata_port *ap,
				      struct ata_link *link, bool dev_only)
{
	/* NULL link indicates start of iteration */
	if (!link) {
		if (dev_only && sata_pmp_attached(ap))
			return ap->pmp_link;
		return &ap->link;
	}

	/* we just iterated over the host master link, what's next? */
	if (link == &ap->link) {
		if (!sata_pmp_attached(ap)) {
			if (unlikely(ap->slave_link) && !dev_only)
				return ap->slave_link;
			return NULL;
		}
		return ap->pmp_link;
	}

	/* slave_link excludes PMP */
	if (unlikely(link == ap->slave_link))
		return NULL;

	/* iterate to the next PMP link */
	if (++link < ap->pmp_link + ap->nr_pmp_links)
		return link;
	return NULL;
}

/**
 *	ata_dev_phys_link - find physical link for a device
 *	@dev: ATA device to look up physical link for
 *
 *	Look up physical link which @dev is attached to.  Note that
 *	this is different from @dev->link only when @dev is on slave
 *	link.  For all other cases, it's the same as @dev->link.
 *
 *	LOCKING:
 *	Don't care.
 *
 *	RETURNS:
 *	Pointer to the found physical link.
 */
struct ata_link *ata_dev_phys_link(struct ata_device *dev)
{
	struct ata_port *ap = dev->link->ap;

	if (!ap->slave_link)
		return dev->link;
	if (!dev->devno)
		return &ap->link;
	return ap->slave_link;
}

/**
 *	ata_force_cbl - force cable type according to libata.force
 *	@ap: ATA port of interest
 *
 *	Force cable type according to libata.force and whine about it.
 *	The last entry which has matching port number is used, so it
 *	can be specified as part of device force parameters.  For
 *	example, both "a:40c,1.00:udma4" and "1.00:40c,udma4" have the
 *	same effect.
 *
 *	LOCKING:
 *	EH context.
 */
void ata_force_cbl(struct ata_port *ap)
{
	int i;

	for (i = ata_force_tbl_size - 1; i >= 0; i--) {
		const struct ata_force_ent *fe = &ata_force_tbl[i];

		if (fe->port != -1 && fe->port != ap->print_id)
			continue;

		if (fe->param.cbl == ATA_CBL_NONE)
			continue;

		ap->cbl = fe->param.cbl;
		ata_port_printk(ap, KERN_NOTICE,
				"FORCE: cable set to %s\n", fe->param.name);
		return;
	}
}

/**
 *	ata_force_link_limits - force link limits according to libata.force
 *	@link: ATA link of interest
 *
 *	Force link flags and SATA spd limit according to libata.force
 *	and whine about it.  When only the port part is specified
 *	(e.g. 1:), the limit applies to all links connected to both
 *	the host link and all fan-out ports connected via PMP.  If the
 *	device part is specified as 0 (e.g. 1.00:), it specifies the
 *	first fan-out link not the host link.  Device number 15 always
 *	points to the host link whether PMP is attached or not.  If the
 *	controller has slave link, device number 16 points to it.
 *
 *	LOCKING:
 *	EH context.
 */
static void ata_force_link_limits(struct ata_link *link)
{
	bool did_spd = false;
	int linkno = link->pmp;
	int i;

	if (ata_is_host_link(link))
		linkno += 15;

	for (i = ata_force_tbl_size - 1; i >= 0; i--) {
		const struct ata_force_ent *fe = &ata_force_tbl[i];

		if (fe->port != -1 && fe->port != link->ap->print_id)
			continue;

		if (fe->device != -1 && fe->device != linkno)
			continue;

		/* only honor the first spd limit */
		if (!did_spd && fe->param.spd_limit) {
			link->hw_sata_spd_limit = (1 << fe->param.spd_limit) - 1;
			ata_link_printk(link, KERN_NOTICE,
					"FORCE: PHY spd limit set to %s\n",
					fe->param.name);
			did_spd = true;
		}

		/* let lflags stack */
		if (fe->param.lflags) {
			link->flags |= fe->param.lflags;
			ata_link_printk(link, KERN_NOTICE,
					"FORCE: link flag 0x%x forced -> 0x%x\n",
					fe->param.lflags, link->flags);
		}
	}
}

/**
 *	ata_force_xfermask - force xfermask according to libata.force
 *	@dev: ATA device of interest
 *
 *	Force xfer_mask according to libata.force and whine about it.
 *	For consistency with link selection, device number 15 selects
 *	the first device connected to the host link.
 *
 *	LOCKING:
 *	EH context.
 */
static void ata_force_xfermask(struct ata_device *dev)
{
	int devno = dev->link->pmp + dev->devno;
	int alt_devno = devno;
	int i;

	/* allow n.15/16 for devices attached to host port */
	if (ata_is_host_link(dev->link))
		alt_devno += 15;

	for (i = ata_force_tbl_size - 1; i >= 0; i--) {
		const struct ata_force_ent *fe = &ata_force_tbl[i];
		unsigned long pio_mask, mwdma_mask, udma_mask;

		if (fe->port != -1 && fe->port != dev->link->ap->print_id)
			continue;

		if (fe->device != -1 && fe->device != devno &&
		    fe->device != alt_devno)
			continue;

		if (!fe->param.xfer_mask)
			continue;

		ata_unpack_xfermask(fe->param.xfer_mask,
				    &pio_mask, &mwdma_mask, &udma_mask);
		if (udma_mask)
			dev->udma_mask = udma_mask;
		else if (mwdma_mask) {
			dev->udma_mask = 0;
			dev->mwdma_mask = mwdma_mask;
		} else {
			dev->udma_mask = 0;
			dev->mwdma_mask = 0;
			dev->pio_mask = pio_mask;
		}

		ata_dev_printk(dev, KERN_NOTICE,
			"FORCE: xfer_mask set to %s\n", fe->param.name);
		return;
	}
}

/**
 *	ata_force_horkage - force horkage according to libata.force
 *	@dev: ATA device of interest
 *
 *	Force horkage according to libata.force and whine about it.
 *	For consistency with link selection, device number 15 selects
 *	the first device connected to the host link.
 *
 *	LOCKING:
 *	EH context.
 */
static void ata_force_horkage(struct ata_device *dev)
{
	int devno = dev->link->pmp + dev->devno;
	int alt_devno = devno;
	int i;

	/* allow n.15/16 for devices attached to host port */
	if (ata_is_host_link(dev->link))
		alt_devno += 15;

	for (i = 0; i < ata_force_tbl_size; i++) {
		const struct ata_force_ent *fe = &ata_force_tbl[i];

		if (fe->port != -1 && fe->port != dev->link->ap->print_id)
			continue;

		if (fe->device != -1 && fe->device != devno &&
		    fe->device != alt_devno)
			continue;

		if (!(~dev->horkage & fe->param.horkage_on) &&
		    !(dev->horkage & fe->param.horkage_off))
			continue;

		dev->horkage |= fe->param.horkage_on;
		dev->horkage &= ~fe->param.horkage_off;

		ata_dev_printk(dev, KERN_NOTICE,
			"FORCE: horkage modified (%s)\n", fe->param.name);
	}
}

/**
 *	atapi_cmd_type - Determine ATAPI command type from SCSI opcode
 *	@opcode: SCSI opcode
 *
 *	Determine ATAPI command type from @opcode.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	ATAPI_{READ|WRITE|READ_CD|PASS_THRU|MISC}
 */
int atapi_cmd_type(u8 opcode)
{
	switch (opcode) {
	case GPCMD_READ_10:
	case GPCMD_READ_12:
		return ATAPI_READ;

	case GPCMD_WRITE_10:
	case GPCMD_WRITE_12:
	case GPCMD_WRITE_AND_VERIFY_10:
		return ATAPI_WRITE;

	case GPCMD_READ_CD:
	case GPCMD_READ_CD_MSF:
		return ATAPI_READ_CD;

	case ATA_16:
	case ATA_12:
		if (atapi_passthru16)
			return ATAPI_PASS_THRU;
		/* fall thru */
	default:
		return ATAPI_MISC;
	}
}

/**
 *	ata_tf_to_fis - Convert ATA taskfile to SATA FIS structure
 *	@tf: Taskfile to convert
 *	@pmp: Port multiplier port
 *	@is_cmd: This FIS is for command
 *	@fis: Buffer into which data will output
 *
 *	Converts a standard ATA taskfile to a Serial ATA
 *	FIS structure (Register - Host to Device).
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_tf_to_fis(const struct ata_taskfile *tf, u8 pmp, int is_cmd, u8 *fis)
{
	fis[0] = 0x27;			/* Register - Host to Device FIS */
	fis[1] = pmp & 0xf;		/* Port multiplier number*/
	if (is_cmd)
		fis[1] |= (1 << 7);	/* bit 7 indicates Command FIS */

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
	} else if (lba48 && (dev->link->ap->flags & ATA_FLAG_PIO_LBA48)) {
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
unsigned long ata_pack_xfermask(unsigned long pio_mask,
				unsigned long mwdma_mask,
				unsigned long udma_mask)
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
void ata_unpack_xfermask(unsigned long xfer_mask, unsigned long *pio_mask,
			 unsigned long *mwdma_mask, unsigned long *udma_mask)
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
	{ ATA_SHIFT_PIO, ATA_NR_PIO_MODES, XFER_PIO_0 },
	{ ATA_SHIFT_MWDMA, ATA_NR_MWDMA_MODES, XFER_MW_DMA_0 },
	{ ATA_SHIFT_UDMA, ATA_NR_UDMA_MODES, XFER_UDMA_0 },
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
 *	Matching XFER_* value, 0xff if no match found.
 */
u8 ata_xfer_mask2mode(unsigned long xfer_mask)
{
	int highbit = fls(xfer_mask) - 1;
	const struct ata_xfer_ent *ent;

	for (ent = ata_xfer_tbl; ent->shift >= 0; ent++)
		if (highbit >= ent->shift && highbit < ent->shift + ent->bits)
			return ent->base + highbit - ent->shift;
	return 0xff;
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
unsigned long ata_xfer_mode2mask(u8 xfer_mode)
{
	const struct ata_xfer_ent *ent;

	for (ent = ata_xfer_tbl; ent->shift >= 0; ent++)
		if (xfer_mode >= ent->base && xfer_mode < ent->base + ent->bits)
			return ((2 << (ent->shift + xfer_mode - ent->base)) - 1)
				& ~((1 << ent->shift) - 1);
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
int ata_xfer_mode2shift(unsigned long xfer_mode)
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
const char *ata_mode_string(unsigned long xfer_mask)
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
	if (ata_dev_enabled(dev)) {
		if (ata_msg_drv(dev->link->ap))
			ata_dev_printk(dev, KERN_WARNING, "disabled\n");
		ata_acpi_on_disable(dev);
		ata_down_xfermask_limit(dev, ATA_DNXFER_FORCE_PIO0 |
					     ATA_DNXFER_QUIET);
		dev->class++;
	}
}

static int ata_dev_set_dipm(struct ata_device *dev, enum link_pm policy)
{
	struct ata_link *link = dev->link;
	struct ata_port *ap = link->ap;
	u32 scontrol;
	unsigned int err_mask;
	int rc;

	/*
	 * disallow DIPM for drivers which haven't set
	 * ATA_FLAG_IPM.  This is because when DIPM is enabled,
	 * phy ready will be set in the interrupt status on
	 * state changes, which will cause some drivers to
	 * think there are errors - additionally drivers will
	 * need to disable hot plug.
	 */
	if (!(ap->flags & ATA_FLAG_IPM) || !ata_dev_enabled(dev)) {
		ap->pm_policy = NOT_AVAILABLE;
		return -EINVAL;
	}

	/*
	 * For DIPM, we will only enable it for the
	 * min_power setting.
	 *
	 * Why?  Because Disks are too stupid to know that
	 * If the host rejects a request to go to SLUMBER
	 * they should retry at PARTIAL, and instead it
	 * just would give up.  So, for medium_power to
	 * work at all, we need to only allow HIPM.
	 */
	rc = sata_scr_read(link, SCR_CONTROL, &scontrol);
	if (rc)
		return rc;

	switch (policy) {
	case MIN_POWER:
		/* no restrictions on IPM transitions */
		scontrol &= ~(0x3 << 8);
		rc = sata_scr_write(link, SCR_CONTROL, scontrol);
		if (rc)
			return rc;

		/* enable DIPM */
		if (dev->flags & ATA_DFLAG_DIPM)
			err_mask = ata_dev_set_feature(dev,
					SETFEATURES_SATA_ENABLE, SATA_DIPM);
		break;
	case MEDIUM_POWER:
		/* allow IPM to PARTIAL */
		scontrol &= ~(0x1 << 8);
		scontrol |= (0x2 << 8);
		rc = sata_scr_write(link, SCR_CONTROL, scontrol);
		if (rc)
			return rc;

		/*
		 * we don't have to disable DIPM since IPM flags
		 * disallow transitions to SLUMBER, which effectively
		 * disable DIPM if it does not support PARTIAL
		 */
		break;
	case NOT_AVAILABLE:
	case MAX_PERFORMANCE:
		/* disable all IPM transitions */
		scontrol |= (0x3 << 8);
		rc = sata_scr_write(link, SCR_CONTROL, scontrol);
		if (rc)
			return rc;

		/*
		 * we don't have to disable DIPM since IPM flags
		 * disallow all transitions which effectively
		 * disable DIPM anyway.
		 */
		break;
	}

	/* FIXME: handle SET FEATURES failure */
	(void) err_mask;

	return 0;
}

/**
 *	ata_dev_enable_pm - enable SATA interface power management
 *	@dev:  device to enable power management
 *	@policy: the link power management policy
 *
 *	Enable SATA Interface power management.  This will enable
 *	Device Interface Power Management (DIPM) for min_power
 * 	policy, and then call driver specific callbacks for
 *	enabling Host Initiated Power management.
 *
 *	Locking: Caller.
 *	Returns: -EINVAL if IPM is not supported, 0 otherwise.
 */
void ata_dev_enable_pm(struct ata_device *dev, enum link_pm policy)
{
	int rc = 0;
	struct ata_port *ap = dev->link->ap;

	/* set HIPM first, then DIPM */
	if (ap->ops->enable_pm)
		rc = ap->ops->enable_pm(ap, policy);
	if (rc)
		goto enable_pm_out;
	rc = ata_dev_set_dipm(dev, policy);

enable_pm_out:
	if (rc)
		ap->pm_policy = MAX_PERFORMANCE;
	else
		ap->pm_policy = policy;
	return /* rc */;	/* hopefully we can use 'rc' eventually */
}

#ifdef CONFIG_PM
/**
 *	ata_dev_disable_pm - disable SATA interface power management
 *	@dev: device to disable power management
 *
 *	Disable SATA Interface power management.  This will disable
 *	Device Interface Power Management (DIPM) without changing
 * 	policy,  call driver specific callbacks for disabling Host
 * 	Initiated Power management.
 *
 *	Locking: Caller.
 *	Returns: void
 */
static void ata_dev_disable_pm(struct ata_device *dev)
{
	struct ata_port *ap = dev->link->ap;

	ata_dev_set_dipm(dev, MAX_PERFORMANCE);
	if (ap->ops->disable_pm)
		ap->ops->disable_pm(ap);
}
#endif	/* CONFIG_PM */

void ata_lpm_schedule(struct ata_port *ap, enum link_pm policy)
{
	ap->pm_policy = policy;
	ap->link.eh_info.action |= ATA_EH_LPM;
	ap->link.eh_info.flags |= ATA_EHI_NO_AUTOPSY;
	ata_port_schedule_eh(ap);
}

#ifdef CONFIG_PM
static void ata_lpm_enable(struct ata_host *host)
{
	struct ata_link *link;
	struct ata_port *ap;
	struct ata_device *dev;
	int i;

	for (i = 0; i < host->n_ports; i++) {
		ap = host->ports[i];
		ata_port_for_each_link(link, ap) {
			ata_link_for_each_dev(dev, link)
				ata_dev_disable_pm(dev);
		}
	}
}

static void ata_lpm_disable(struct ata_host *host)
{
	int i;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		ata_lpm_schedule(ap, ap->pm_policy);
	}
}
#endif	/* CONFIG_PM */

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
 *	Device type, %ATA_DEV_ATA, %ATA_DEV_ATAPI, %ATA_DEV_PMP or
 *	%ATA_DEV_UNKNOWN the event of failure.
 */
unsigned int ata_dev_classify(const struct ata_taskfile *tf)
{
	/* Apple's open source Darwin code hints that some devices only
	 * put a proper signature into the LBA mid/high registers,
	 * So, we only check those.  It's sufficient for uniqueness.
	 *
	 * ATA/ATAPI-7 (d1532v1r1: Feb. 19, 2003) specified separate
	 * signatures for ATA and ATAPI devices attached on SerialATA,
	 * 0x3c/0xc3 and 0x69/0x96 respectively.  However, SerialATA
	 * spec has never mentioned about using different signatures
	 * for ATA/ATAPI devices.  Then, Serial ATA II: Port
	 * Multiplier specification began to use 0x69/0x96 to identify
	 * port multpliers and 0x3c/0xc3 to identify SEMB device.
	 * ATA/ATAPI-7 dropped descriptions about 0x3c/0xc3 and
	 * 0x69/0x96 shortly and described them as reserved for
	 * SerialATA.
	 *
	 * We follow the current spec and consider that 0x69/0x96
	 * identifies a port multiplier and 0x3c/0xc3 a SEMB device.
	 */
	if ((tf->lbam == 0) && (tf->lbah == 0)) {
		DPRINTK("found ATA device by sig\n");
		return ATA_DEV_ATA;
	}

	if ((tf->lbam == 0x14) && (tf->lbah == 0xeb)) {
		DPRINTK("found ATAPI device by sig\n");
		return ATA_DEV_ATAPI;
	}

	if ((tf->lbam == 0x69) && (tf->lbah == 0x96)) {
		DPRINTK("found PMP device by sig\n");
		return ATA_DEV_PMP;
	}

	if ((tf->lbam == 0x3c) && (tf->lbah == 0xc3)) {
		printk(KERN_INFO "ata: SEMB device ignored\n");
		return ATA_DEV_SEMB_UNSUP; /* not yet */
	}

	DPRINTK("unknown device\n");
	return ATA_DEV_UNKNOWN;
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

	BUG_ON(len & 1);

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

u64 ata_tf_to_lba48(const struct ata_taskfile *tf)
{
	u64 sectors = 0;

	sectors |= ((u64)(tf->hob_lbah & 0xff)) << 40;
	sectors |= ((u64)(tf->hob_lbam & 0xff)) << 32;
	sectors |= (tf->hob_lbal & 0xff) << 24;
	sectors |= (tf->lbah & 0xff) << 16;
	sectors |= (tf->lbam & 0xff) << 8;
	sectors |= (tf->lbal & 0xff);

	return sectors;
}

u64 ata_tf_to_lba(const struct ata_taskfile *tf)
{
	u64 sectors = 0;

	sectors |= (tf->device & 0x0f) << 24;
	sectors |= (tf->lbah & 0xff) << 16;
	sectors |= (tf->lbam & 0xff) << 8;
	sectors |= (tf->lbal & 0xff);

	return sectors;
}

/**
 *	ata_read_native_max_address - Read native max address
 *	@dev: target device
 *	@max_sectors: out parameter for the result native max address
 *
 *	Perform an LBA48 or LBA28 native size query upon the device in
 *	question.
 *
 *	RETURNS:
 *	0 on success, -EACCES if command is aborted by the drive.
 *	-EIO on other errors.
 */
static int ata_read_native_max_address(struct ata_device *dev, u64 *max_sectors)
{
	unsigned int err_mask;
	struct ata_taskfile tf;
	int lba48 = ata_id_has_lba48(dev->id);

	ata_tf_init(dev, &tf);

	/* always clear all address registers */
	tf.flags |= ATA_TFLAG_DEVICE | ATA_TFLAG_ISADDR;

	if (lba48) {
		tf.command = ATA_CMD_READ_NATIVE_MAX_EXT;
		tf.flags |= ATA_TFLAG_LBA48;
	} else
		tf.command = ATA_CMD_READ_NATIVE_MAX;

	tf.protocol |= ATA_PROT_NODATA;
	tf.device |= ATA_LBA;

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0, 0);
	if (err_mask) {
		ata_dev_printk(dev, KERN_WARNING, "failed to read native "
			       "max address (err_mask=0x%x)\n", err_mask);
		if (err_mask == AC_ERR_DEV && (tf.feature & ATA_ABORTED))
			return -EACCES;
		return -EIO;
	}

	if (lba48)
		*max_sectors = ata_tf_to_lba48(&tf) + 1;
	else
		*max_sectors = ata_tf_to_lba(&tf) + 1;
	if (dev->horkage & ATA_HORKAGE_HPA_SIZE)
		(*max_sectors)--;
	return 0;
}

/**
 *	ata_set_max_sectors - Set max sectors
 *	@dev: target device
 *	@new_sectors: new max sectors value to set for the device
 *
 *	Set max sectors of @dev to @new_sectors.
 *
 *	RETURNS:
 *	0 on success, -EACCES if command is aborted or denied (due to
 *	previous non-volatile SET_MAX) by the drive.  -EIO on other
 *	errors.
 */
static int ata_set_max_sectors(struct ata_device *dev, u64 new_sectors)
{
	unsigned int err_mask;
	struct ata_taskfile tf;
	int lba48 = ata_id_has_lba48(dev->id);

	new_sectors--;

	ata_tf_init(dev, &tf);

	tf.flags |= ATA_TFLAG_DEVICE | ATA_TFLAG_ISADDR;

	if (lba48) {
		tf.command = ATA_CMD_SET_MAX_EXT;
		tf.flags |= ATA_TFLAG_LBA48;

		tf.hob_lbal = (new_sectors >> 24) & 0xff;
		tf.hob_lbam = (new_sectors >> 32) & 0xff;
		tf.hob_lbah = (new_sectors >> 40) & 0xff;
	} else {
		tf.command = ATA_CMD_SET_MAX;

		tf.device |= (new_sectors >> 24) & 0xf;
	}

	tf.protocol |= ATA_PROT_NODATA;
	tf.device |= ATA_LBA;

	tf.lbal = (new_sectors >> 0) & 0xff;
	tf.lbam = (new_sectors >> 8) & 0xff;
	tf.lbah = (new_sectors >> 16) & 0xff;

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0, 0);
	if (err_mask) {
		ata_dev_printk(dev, KERN_WARNING, "failed to set "
			       "max address (err_mask=0x%x)\n", err_mask);
		if (err_mask == AC_ERR_DEV &&
		    (tf.feature & (ATA_ABORTED | ATA_IDNF)))
			return -EACCES;
		return -EIO;
	}

	return 0;
}

/**
 *	ata_hpa_resize		-	Resize a device with an HPA set
 *	@dev: Device to resize
 *
 *	Read the size of an LBA28 or LBA48 disk with HPA features and resize
 *	it if required to the full size of the media. The caller must check
 *	the drive has the HPA feature set enabled.
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
static int ata_hpa_resize(struct ata_device *dev)
{
	struct ata_eh_context *ehc = &dev->link->eh_context;
	int print_info = ehc->i.flags & ATA_EHI_PRINTINFO;
	u64 sectors = ata_id_n_sectors(dev->id);
	u64 native_sectors;
	int rc;

	/* do we need to do it? */
	if (dev->class != ATA_DEV_ATA ||
	    !ata_id_has_lba(dev->id) || !ata_id_hpa_enabled(dev->id) ||
	    (dev->horkage & ATA_HORKAGE_BROKEN_HPA))
		return 0;

	/* read native max address */
	rc = ata_read_native_max_address(dev, &native_sectors);
	if (rc) {
		/* If device aborted the command or HPA isn't going to
		 * be unlocked, skip HPA resizing.
		 */
		if (rc == -EACCES || !ata_ignore_hpa) {
			ata_dev_printk(dev, KERN_WARNING, "HPA support seems "
				       "broken, skipping HPA handling\n");
			dev->horkage |= ATA_HORKAGE_BROKEN_HPA;

			/* we can continue if device aborted the command */
			if (rc == -EACCES)
				rc = 0;
		}

		return rc;
	}

	/* nothing to do? */
	if (native_sectors <= sectors || !ata_ignore_hpa) {
		if (!print_info || native_sectors == sectors)
			return 0;

		if (native_sectors > sectors)
			ata_dev_printk(dev, KERN_INFO,
				"HPA detected: current %llu, native %llu\n",
				(unsigned long long)sectors,
				(unsigned long long)native_sectors);
		else if (native_sectors < sectors)
			ata_dev_printk(dev, KERN_WARNING,
				"native sectors (%llu) is smaller than "
				"sectors (%llu)\n",
				(unsigned long long)native_sectors,
				(unsigned long long)sectors);
		return 0;
	}

	/* let's unlock HPA */
	rc = ata_set_max_sectors(dev, native_sectors);
	if (rc == -EACCES) {
		/* if device aborted the command, skip HPA resizing */
		ata_dev_printk(dev, KERN_WARNING, "device aborted resize "
			       "(%llu -> %llu), skipping HPA handling\n",
			       (unsigned long long)sectors,
			       (unsigned long long)native_sectors);
		dev->horkage |= ATA_HORKAGE_BROKEN_HPA;
		return 0;
	} else if (rc)
		return rc;

	/* re-read IDENTIFY data */
	rc = ata_dev_reread_id(dev, 0);
	if (rc) {
		ata_dev_printk(dev, KERN_ERR, "failed to re-read IDENTIFY "
			       "data after HPA resizing\n");
		return rc;
	}

	if (print_info) {
		u64 new_sectors = ata_id_n_sectors(dev->id);
		ata_dev_printk(dev, KERN_INFO,
			"HPA unlocked: %llu -> %llu, native %llu\n",
			(unsigned long long)sectors,
			(unsigned long long)new_sectors,
			(unsigned long long)native_sectors);
	}

	return 0;
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
unsigned long ata_id_xfermask(const u16 *id)
{
	unsigned long pio_mask, mwdma_mask, udma_mask;

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
 *	ata_pio_queue_task - Queue port_task
 *	@ap: The ata_port to queue port_task for
 *	@fn: workqueue function to be scheduled
 *	@data: data for @fn to use
 *	@delay: delay time in msecs for workqueue function
 *
 *	Schedule @fn(@data) for execution after @delay jiffies using
 *	port_task.  There is one port_task per port and it's the
 *	user(low level driver)'s responsibility to make sure that only
 *	one task is active at any given time.
 *
 *	libata core layer takes care of synchronization between
 *	port_task and EH.  ata_pio_queue_task() may be ignored for EH
 *	synchronization.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_pio_queue_task(struct ata_port *ap, void *data, unsigned long delay)
{
	ap->port_task_data = data;

	/* may fail if ata_port_flush_task() in progress */
	queue_delayed_work(ata_wq, &ap->port_task, msecs_to_jiffies(delay));
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
	DPRINTK("ENTER\n");

	cancel_rearming_delayed_work(&ap->port_task);

	if (ata_msg_ctl(ap))
		ata_port_printk(ap, KERN_DEBUG, "%s: EXIT\n", __func__);
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
 *	@sgl: sg list for the data buffer of the command
 *	@n_elem: Number of sg entries
 *	@timeout: Timeout in msecs (0 for default)
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
			      int dma_dir, struct scatterlist *sgl,
			      unsigned int n_elem, unsigned long timeout)
{
	struct ata_link *link = dev->link;
	struct ata_port *ap = link->ap;
	u8 command = tf->command;
	int auto_timeout = 0;
	struct ata_queued_cmd *qc;
	unsigned int tag, preempted_tag;
	u32 preempted_sactive, preempted_qc_active;
	int preempted_nr_active_links;
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

	preempted_tag = link->active_tag;
	preempted_sactive = link->sactive;
	preempted_qc_active = ap->qc_active;
	preempted_nr_active_links = ap->nr_active_links;
	link->active_tag = ATA_TAG_POISON;
	link->sactive = 0;
	ap->qc_active = 0;
	ap->nr_active_links = 0;

	/* prepare & issue qc */
	qc->tf = *tf;
	if (cdb)
		memcpy(qc->cdb, cdb, ATAPI_CDB_LEN);
	qc->flags |= ATA_QCFLAG_RESULT_TF;
	qc->dma_dir = dma_dir;
	if (dma_dir != DMA_NONE) {
		unsigned int i, buflen = 0;
		struct scatterlist *sg;

		for_each_sg(sgl, sg, n_elem, i)
			buflen += sg->length;

		ata_sg_init(qc, sgl, n_elem);
		qc->nbytes = buflen;
	}

	qc->private_data = &wait;
	qc->complete_fn = ata_qc_complete_internal;

	ata_qc_issue(qc);

	spin_unlock_irqrestore(ap->lock, flags);

	if (!timeout) {
		if (ata_probe_timeout)
			timeout = ata_probe_timeout * 1000;
		else {
			timeout = ata_internal_cmd_timeout(dev, command);
			auto_timeout = 1;
		}
	}

	rc = wait_for_completion_timeout(&wait, msecs_to_jiffies(timeout));

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

	/* perform minimal error analysis */
	if (qc->flags & ATA_QCFLAG_FAILED) {
		if (qc->result_tf.command & (ATA_ERR | ATA_DF))
			qc->err_mask |= AC_ERR_DEV;

		if (!qc->err_mask)
			qc->err_mask |= AC_ERR_OTHER;

		if (qc->err_mask & ~AC_ERR_OTHER)
			qc->err_mask &= ~AC_ERR_OTHER;
	}

	/* finish up */
	spin_lock_irqsave(ap->lock, flags);

	*tf = qc->result_tf;
	err_mask = qc->err_mask;

	ata_qc_free(qc);
	link->active_tag = preempted_tag;
	link->sactive = preempted_sactive;
	ap->qc_active = preempted_qc_active;
	ap->nr_active_links = preempted_nr_active_links;

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

	if ((err_mask & AC_ERR_TIMEOUT) && auto_timeout)
		ata_internal_cmd_timed_out(dev, command);

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
 *	@timeout: Timeout in msecs (0 for default)
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
			   int dma_dir, void *buf, unsigned int buflen,
			   unsigned long timeout)
{
	struct scatterlist *psg = NULL, sg;
	unsigned int n_elem = 0;

	if (dma_dir != DMA_NONE) {
		WARN_ON(!buf);
		sg_init_one(&sg, buf, buflen);
		psg = &sg;
		n_elem++;
	}

	return ata_exec_internal_sg(dev, tf, cdb, dma_dir, psg, n_elem,
				    timeout);
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

	return ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0, 0);
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
	/* Controller doesn't support  IORDY. Probably a pointless check
	   as the caller should know this */
	if (adev->link->ap->flags & ATA_FLAG_NO_IORDY)
		return 0;
	/* PIO3 and higher it is mandatory */
	if (adev->pio_mode > XFER_PIO_2)
		return 1;
	/* We turn it on when possible */
	if (ata_id_has_iordy(adev->id))
		return 1;
	return 0;
}

/**
 *	ata_pio_mask_no_iordy	-	Return the non IORDY mask
 *	@adev: ATA device
 *
 *	Compute the highest mode possible if we are not using iordy. Return
 *	-1 if no iordy mode is available.
 */

static u32 ata_pio_mask_no_iordy(const struct ata_device *adev)
{
	/* If we have no drive specific rule, then PIO 2 is non IORDY */
	if (adev->id[ATA_ID_FIELD_VALID] & 2) {	/* EIDE */
		u16 pio = adev->id[ATA_ID_EIDE_PIO];
		/* Is the speed faster than the drive allows non IORDY ? */
		if (pio) {
			/* This is cycle times not frequency - watch the logic! */
			if (pio > 240)	/* PIO2 is 240nS per cycle */
				return 3 << ATA_SHIFT_PIO;
			return 7 << ATA_SHIFT_PIO;
		}
	}
	return 3 << ATA_SHIFT_PIO;
}

/**
 *	ata_do_dev_read_id		-	default ID read method
 *	@dev: device
 *	@tf: proposed taskfile
 *	@id: data buffer
 *
 *	Issue the identify taskfile and hand back the buffer containing
 *	identify data. For some RAID controllers and for pre ATA devices
 *	this function is wrapped or replaced by the driver
 */
unsigned int ata_do_dev_read_id(struct ata_device *dev,
					struct ata_taskfile *tf, u16 *id)
{
	return ata_exec_internal(dev, tf, NULL, DMA_FROM_DEVICE,
				     id, sizeof(id[0]) * ATA_ID_WORDS, 0);
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
 *	FIXME: ATA_CMD_ID_ATA is optional for early drives and right
 *	now we abort if we hit that case.
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
	struct ata_port *ap = dev->link->ap;
	unsigned int class = *p_class;
	struct ata_taskfile tf;
	unsigned int err_mask = 0;
	const char *reason;
	int may_fallback = 1, tried_spinup = 0;
	int rc;

	if (ata_msg_ctl(ap))
		ata_dev_printk(dev, KERN_DEBUG, "%s: ENTER\n", __func__);

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

	if (ap->ops->read_id)
		err_mask = ap->ops->read_id(dev, &tf, id);
	else
		err_mask = ata_do_dev_read_id(dev, &tf, id);

	if (err_mask) {
		if (err_mask & AC_ERR_NODEV_HINT) {
			ata_dev_printk(dev, KERN_DEBUG,
				       "NODEV after polling detection\n");
			return -ENOENT;
		}

		if ((err_mask == AC_ERR_DEV) && (tf.feature & ATA_ABORTED)) {
			/* Device or controller might have reported
			 * the wrong device class.  Give a shot at the
			 * other IDENTIFY if the current one is
			 * aborted by the device.
			 */
			if (may_fallback) {
				may_fallback = 0;

				if (class == ATA_DEV_ATA)
					class = ATA_DEV_ATAPI;
				else
					class = ATA_DEV_ATA;
				goto retry;
			}

			/* Control reaches here iff the device aborted
			 * both flavors of IDENTIFYs which happens
			 * sometimes with phantom devices.
			 */
			ata_dev_printk(dev, KERN_DEBUG,
				       "both IDENTIFYs aborted, assuming NODEV\n");
			return -ENOENT;
		}

		rc = -EIO;
		reason = "I/O error";
		goto err_out;
	}

	/* Falling back doesn't make sense if ID data was read
	 * successfully at least once.
	 */
	may_fallback = 0;

	swap_buf_le16(id, ATA_ID_WORDS);

	/* sanity check */
	rc = -EINVAL;
	reason = "device reports invalid type";

	if (class == ATA_DEV_ATA) {
		if (!ata_id_is_ata(id) && !ata_id_is_cfa(id))
			goto err_out;
	} else {
		if (ata_id_is_ata(id))
			goto err_out;
	}

	if (!tried_spinup && (id[2] == 0x37c8 || id[2] == 0x738c)) {
		tried_spinup = 1;
		/*
		 * Drive powered-up in standby mode, and requires a specific
		 * SET_FEATURES spin-up subcommand before it will accept
		 * anything other than the original IDENTIFY command.
		 */
		err_mask = ata_dev_set_feature(dev, SETFEATURES_SPINUP, 0);
		if (err_mask && id[2] != 0x738c) {
			rc = -EIO;
			reason = "SPINUP failed";
			goto err_out;
		}
		/*
		 * If the drive initially returned incomplete IDENTIFY info,
		 * we now must reissue the IDENTIFY command.
		 */
		if (id[2] == 0x37c8)
			goto retry;
	}

	if ((flags & ATA_READID_POSTRESET) && class == ATA_DEV_ATA) {
		/*
		 * The exact sequence expected by certain pre-ATA4 drives is:
		 * SRST RESET
		 * IDENTIFY (optional in early ATA)
		 * INITIALIZE DEVICE PARAMETERS (later IDE and ATA)
		 * anything else..
		 * Some drives were very specific about that exact sequence.
		 *
		 * Note that ATA4 says lba is mandatory so the second check
		 * shoud never trigger.
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
	struct ata_port *ap = dev->link->ap;
	return ((ap->cbl == ATA_CBL_SATA) && (!ata_id_is_sata(dev->id)));
}

static void ata_dev_config_ncq(struct ata_device *dev,
			       char *desc, size_t desc_sz)
{
	struct ata_port *ap = dev->link->ap;
	int hdepth = 0, ddepth = ata_id_queue_depth(dev->id);

	if (!ata_id_has_ncq(dev->id)) {
		desc[0] = '\0';
		return;
	}
	if (dev->horkage & ATA_HORKAGE_NONCQ) {
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
	struct ata_port *ap = dev->link->ap;
	struct ata_eh_context *ehc = &dev->link->eh_context;
	int print_info = ehc->i.flags & ATA_EHI_PRINTINFO;
	const u16 *id = dev->id;
	unsigned long xfer_mask;
	char revbuf[7];		/* XYZ-99\0 */
	char fwrevbuf[ATA_ID_FW_REV_LEN+1];
	char modelbuf[ATA_ID_PROD_LEN+1];
	int rc;

	if (!ata_dev_enabled(dev) && ata_msg_info(ap)) {
		ata_dev_printk(dev, KERN_INFO, "%s: ENTER/EXIT -- nodev\n",
			       __func__);
		return 0;
	}

	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG, "%s: ENTER\n", __func__);

	/* set horkage */
	dev->horkage |= ata_dev_blacklisted(dev);
	ata_force_horkage(dev);

	if (dev->horkage & ATA_HORKAGE_DISABLE) {
		ata_dev_printk(dev, KERN_INFO,
			       "unsupported device, disabling\n");
		ata_dev_disable(dev);
		return 0;
	}

	if ((!atapi_enabled || (ap->flags & ATA_FLAG_NO_ATAPI)) &&
	    dev->class == ATA_DEV_ATAPI) {
		ata_dev_printk(dev, KERN_WARNING,
			"WARNING: ATAPI is %s, device ignored.\n",
			atapi_enabled ? "not supported with this driver"
				      : "disabled");
		ata_dev_disable(dev);
		return 0;
	}

	/* let ACPI work its magic */
	rc = ata_acpi_on_devcfg(dev);
	if (rc)
		return rc;

	/* massage HPA, do it early as it might change IDENTIFY data */
	rc = ata_hpa_resize(dev);
	if (rc)
		return rc;

	/* print device capabilities */
	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG,
			       "%s: cfg 49:%04x 82:%04x 83:%04x 84:%04x "
			       "85:%04x 86:%04x 87:%04x 88:%04x\n",
			       __func__,
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

	/* SCSI only uses 4-char revisions, dump full 8 chars from ATA */
	ata_id_c_string(dev->id, fwrevbuf, ATA_ID_FW_REV,
			sizeof(fwrevbuf));

	ata_id_c_string(dev->id, modelbuf, ATA_ID_PROD,
			sizeof(modelbuf));

	/* ATA-specific feature tests */
	if (dev->class == ATA_DEV_ATA) {
		if (ata_id_is_cfa(id)) {
			if (id[162] & 1) /* CPRM may make this media unusable */
				ata_dev_printk(dev, KERN_WARNING,
					       "supports DRM functions and may "
					       "not be fully accessable.\n");
			snprintf(revbuf, 7, "CFA");
		} else {
			snprintf(revbuf, 7, "ATA-%d", ata_id_major_version(id));
			/* Warn the user if the device has TPM extensions */
			if (ata_id_has_tpm(id))
				ata_dev_printk(dev, KERN_WARNING,
					       "supports DRM functions and may "
					       "not be fully accessable.\n");
		}

		dev->n_sectors = ata_id_n_sectors(id);

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
		const char *cdb_intr_string = "";
		const char *atapi_an_string = "";
		const char *dma_dir_string = "";
		u32 sntf;

		rc = atapi_cdb_len(id);
		if ((rc < 12) || (rc > ATAPI_CDB_LEN)) {
			if (ata_msg_warn(ap))
				ata_dev_printk(dev, KERN_WARNING,
					       "unsupported CDB len\n");
			rc = -EINVAL;
			goto err_out_nosup;
		}
		dev->cdb_len = (unsigned int) rc;

		/* Enable ATAPI AN if both the host and device have
		 * the support.  If PMP is attached, SNTF is required
		 * to enable ATAPI AN to discern between PHY status
		 * changed notifications and ATAPI ANs.
		 */
		if ((ap->flags & ATA_FLAG_AN) && ata_id_has_atapi_AN(id) &&
		    (!sata_pmp_attached(ap) ||
		     sata_scr_read(&ap->link, SCR_NOTIFICATION, &sntf) == 0)) {
			unsigned int err_mask;

			/* issue SET feature command to turn this on */
			err_mask = ata_dev_set_feature(dev,
					SETFEATURES_SATA_ENABLE, SATA_AN);
			if (err_mask)
				ata_dev_printk(dev, KERN_ERR,
					"failed to enable ATAPI AN "
					"(err_mask=0x%x)\n", err_mask);
			else {
				dev->flags |= ATA_DFLAG_AN;
				atapi_an_string = ", ATAPI AN";
			}
		}

		if (ata_id_cdb_intr(dev->id)) {
			dev->flags |= ATA_DFLAG_CDB_INTR;
			cdb_intr_string = ", CDB intr";
		}

		if (atapi_dmadir || atapi_id_dmadir(dev->id)) {
			dev->flags |= ATA_DFLAG_DMADIR;
			dma_dir_string = ", DMADIR";
		}

		/* print device info to dmesg */
		if (ata_msg_drv(ap) && print_info)
			ata_dev_printk(dev, KERN_INFO,
				       "ATAPI: %s, %s, max %s%s%s%s\n",
				       modelbuf, fwrevbuf,
				       ata_mode_string(xfer_mask),
				       cdb_intr_string, atapi_an_string,
				       dma_dir_string);
	}

	/* determine max_sectors */
	dev->max_sectors = ATA_MAX_SECTORS;
	if (dev->flags & ATA_DFLAG_LBA48)
		dev->max_sectors = ATA_MAX_SECTORS_LBA48;

	if (!(dev->horkage & ATA_HORKAGE_IPM)) {
		if (ata_id_has_hipm(dev->id))
			dev->flags |= ATA_DFLAG_HIPM;
		if (ata_id_has_dipm(dev->id))
			dev->flags |= ATA_DFLAG_DIPM;
	}

	/* Limit PATA drive on SATA cable bridge transfers to udma5,
	   200 sectors */
	if (ata_dev_knobble(dev)) {
		if (ata_msg_drv(ap) && print_info)
			ata_dev_printk(dev, KERN_INFO,
				       "applying bridge limits\n");
		dev->udma_mask &= ATA_UDMA5;
		dev->max_sectors = ATA_MAX_SECTORS;
	}

	if ((dev->class == ATA_DEV_ATAPI) &&
	    (atapi_command_packet_set(id) == TYPE_TAPE)) {
		dev->max_sectors = ATA_MAX_SECTORS_TAPE;
		dev->horkage |= ATA_HORKAGE_STUCK_ERR;
	}

	if (dev->horkage & ATA_HORKAGE_MAX_SEC_128)
		dev->max_sectors = min_t(unsigned int, ATA_MAX_SECTORS_128,
					 dev->max_sectors);

	if (ata_dev_blacklisted(dev) & ATA_HORKAGE_IPM) {
		dev->horkage |= ATA_HORKAGE_IPM;

		/* reset link pm_policy for this port to no pm */
		ap->pm_policy = MAX_PERFORMANCE;
	}

	if (ap->ops->dev_config)
		ap->ops->dev_config(dev);

	if (dev->horkage & ATA_HORKAGE_DIAGNOSTIC) {
		/* Let the user know. We don't want to disallow opens for
		   rescue purposes, or in case the vendor is just a blithering
		   idiot. Do this after the dev_config call as some controllers
		   with buggy firmware may want to avoid reporting false device
		   bugs */

		if (print_info) {
			ata_dev_printk(dev, KERN_WARNING,
"Drive reports diagnostics failure. This may indicate a drive\n");
			ata_dev_printk(dev, KERN_WARNING,
"fault or invalid emulation. Contact drive vendor for information.\n");
		}
	}

	return 0;

err_out_nosup:
	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG,
			       "%s: EXIT, err\n", __func__);
	return rc;
}

/**
 *	ata_cable_40wire	-	return 40 wire cable type
 *	@ap: port
 *
 *	Helper method for drivers which want to hardwire 40 wire cable
 *	detection.
 */

int ata_cable_40wire(struct ata_port *ap)
{
	return ATA_CBL_PATA40;
}

/**
 *	ata_cable_80wire	-	return 80 wire cable type
 *	@ap: port
 *
 *	Helper method for drivers which want to hardwire 80 wire cable
 *	detection.
 */

int ata_cable_80wire(struct ata_port *ap)
{
	return ATA_CBL_PATA80;
}

/**
 *	ata_cable_unknown	-	return unknown PATA cable.
 *	@ap: port
 *
 *	Helper method for drivers which have no PATA cable detection.
 */

int ata_cable_unknown(struct ata_port *ap)
{
	return ATA_CBL_PATA_UNK;
}

/**
 *	ata_cable_ignore	-	return ignored PATA cable.
 *	@ap: port
 *
 *	Helper method for drivers which don't use cable type to limit
 *	transfer mode.
 */
int ata_cable_ignore(struct ata_port *ap)
{
	return ATA_CBL_PATA_IGN;
}

/**
 *	ata_cable_sata	-	return SATA cable type
 *	@ap: port
 *
 *	Helper method for drivers which have SATA cables
 */

int ata_cable_sata(struct ata_port *ap)
{
	return ATA_CBL_SATA;
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
	int rc;
	struct ata_device *dev;

	ata_port_probe(ap);

	ata_link_for_each_dev(dev, &ap->link)
		tries[dev->devno] = ATA_PROBE_MAX_TRIES;

 retry:
	ata_link_for_each_dev(dev, &ap->link) {
		/* If we issue an SRST then an ATA drive (not ATAPI)
		 * may change configuration and be in PIO0 timing. If
		 * we do a hard reset (or are coming from power on)
		 * this is true for ATA or ATAPI. Until we've set a
		 * suitable controller mode we should not touch the
		 * bus as we may be talking too fast.
		 */
		dev->pio_mode = XFER_PIO_0;

		/* If the controller has a pio mode setup function
		 * then use it to set the chipset to rights. Don't
		 * touch the DMA setup as that will be dealt with when
		 * configuring devices.
		 */
		if (ap->ops->set_piomode)
			ap->ops->set_piomode(ap, dev);
	}

	/* reset and determine device classes */
	ap->ops->phy_reset(ap);

	ata_link_for_each_dev(dev, &ap->link) {
		if (!(ap->flags & ATA_FLAG_DISABLED) &&
		    dev->class != ATA_DEV_UNKNOWN)
			classes[dev->devno] = dev->class;
		else
			classes[dev->devno] = ATA_DEV_NONE;

		dev->class = ATA_DEV_UNKNOWN;
	}

	ata_port_probe(ap);

	/* read IDENTIFY page and configure devices. We have to do the identify
	   specific sequence bass-ackwards so that PDIAG- is released by
	   the slave device */

	ata_link_for_each_dev_reverse(dev, &ap->link) {
		if (tries[dev->devno])
			dev->class = classes[dev->devno];

		if (!ata_dev_enabled(dev))
			continue;

		rc = ata_dev_read_id(dev, &dev->class, ATA_READID_POSTRESET,
				     dev->id);
		if (rc)
			goto fail;
	}

	/* Now ask for the cable type as PDIAG- should have been released */
	if (ap->ops->cable_detect)
		ap->cbl = ap->ops->cable_detect(ap);

	/* We may have SATA bridge glue hiding here irrespective of the
	   reported cable types and sensed types */
	ata_link_for_each_dev(dev, &ap->link) {
		if (!ata_dev_enabled(dev))
			continue;
		/* SATA drives indicate we have a bridge. We don't know which
		   end of the link the bridge is which is a problem */
		if (ata_id_is_sata(dev->id))
			ap->cbl = ATA_CBL_SATA;
	}

	/* After the identify sequence we can now set up the devices. We do
	   this in the normal order so that the user doesn't get confused */

	ata_link_for_each_dev(dev, &ap->link) {
		if (!ata_dev_enabled(dev))
			continue;

		ap->link.eh_context.i.flags |= ATA_EHI_PRINTINFO;
		rc = ata_dev_configure(dev);
		ap->link.eh_context.i.flags &= ~ATA_EHI_PRINTINFO;
		if (rc)
			goto fail;
	}

	/* configure transfer mode */
	rc = ata_set_mode(&ap->link, &dev);
	if (rc)
		goto fail;

	ata_link_for_each_dev(dev, &ap->link)
		if (ata_dev_enabled(dev))
			return 0;

	/* no device present, disable port */
	ata_port_disable(ap);
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
			sata_down_spd_limit(&ap->link);
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
 *	@link: SATA link to printk link status about
 *
 *	This function prints link speed and status of a SATA link.
 *
 *	LOCKING:
 *	None.
 */
static void sata_print_link_status(struct ata_link *link)
{
	u32 sstatus, scontrol, tmp;

	if (sata_scr_read(link, SCR_STATUS, &sstatus))
		return;
	sata_scr_read(link, SCR_CONTROL, &scontrol);

	if (ata_phys_link_online(link)) {
		tmp = (sstatus >> 4) & 0xf;
		ata_link_printk(link, KERN_INFO,
				"SATA link up %s (SStatus %X SControl %X)\n",
				sata_spd_string(tmp), sstatus, scontrol);
	} else {
		ata_link_printk(link, KERN_INFO,
				"SATA link down (SStatus %X SControl %X)\n",
				sstatus, scontrol);
	}
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
	struct ata_link *link = adev->link;
	struct ata_device *pair = &link->device[1 - adev->devno];
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
	ap->link.device[0].class = ATA_DEV_NONE;
	ap->link.device[1].class = ATA_DEV_NONE;
	ap->flags |= ATA_FLAG_DISABLED;
}

/**
 *	sata_down_spd_limit - adjust SATA spd limit downward
 *	@link: Link to adjust SATA spd limit for
 *
 *	Adjust SATA spd limit of @link downward.  Note that this
 *	function only adjusts the limit.  The change must be applied
 *	using sata_set_spd().
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure
 */
int sata_down_spd_limit(struct ata_link *link)
{
	u32 sstatus, spd, mask;
	int rc, highbit;

	if (!sata_scr_valid(link))
		return -EOPNOTSUPP;

	/* If SCR can be read, use it to determine the current SPD.
	 * If not, use cached value in link->sata_spd.
	 */
	rc = sata_scr_read(link, SCR_STATUS, &sstatus);
	if (rc == 0)
		spd = (sstatus >> 4) & 0xf;
	else
		spd = link->sata_spd;

	mask = link->sata_spd_limit;
	if (mask <= 1)
		return -EINVAL;

	/* unconditionally mask off the highest bit */
	highbit = fls(mask) - 1;
	mask &= ~(1 << highbit);

	/* Mask off all speeds higher than or equal to the current
	 * one.  Force 1.5Gbps if current SPD is not available.
	 */
	if (spd > 1)
		mask &= (1 << (spd - 1)) - 1;
	else
		mask &= 1;

	/* were we already at the bottom? */
	if (!mask)
		return -EINVAL;

	link->sata_spd_limit = mask;

	ata_link_printk(link, KERN_WARNING, "limiting SATA link speed to %s\n",
			sata_spd_string(fls(mask)));

	return 0;
}

static int __sata_set_spd_needed(struct ata_link *link, u32 *scontrol)
{
	struct ata_link *host_link = &link->ap->link;
	u32 limit, target, spd;

	limit = link->sata_spd_limit;

	/* Don't configure downstream link faster than upstream link.
	 * It doesn't speed up anything and some PMPs choke on such
	 * configuration.
	 */
	if (!ata_is_host_link(link) && host_link->sata_spd)
		limit &= (1 << host_link->sata_spd) - 1;

	if (limit == UINT_MAX)
		target = 0;
	else
		target = fls(limit);

	spd = (*scontrol >> 4) & 0xf;
	*scontrol = (*scontrol & ~0xf0) | ((target & 0xf) << 4);

	return spd != target;
}

/**
 *	sata_set_spd_needed - is SATA spd configuration needed
 *	@link: Link in question
 *
 *	Test whether the spd limit in SControl matches
 *	@link->sata_spd_limit.  This function is used to determine
 *	whether hardreset is necessary to apply SATA spd
 *	configuration.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	1 if SATA spd configuration is needed, 0 otherwise.
 */
static int sata_set_spd_needed(struct ata_link *link)
{
	u32 scontrol;

	if (sata_scr_read(link, SCR_CONTROL, &scontrol))
		return 1;

	return __sata_set_spd_needed(link, &scontrol);
}

/**
 *	sata_set_spd - set SATA spd according to spd limit
 *	@link: Link to set SATA spd for
 *
 *	Set SATA spd of @link according to sata_spd_limit.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	0 if spd doesn't need to be changed, 1 if spd has been
 *	changed.  Negative errno if SCR registers are inaccessible.
 */
int sata_set_spd(struct ata_link *link)
{
	u32 scontrol;
	int rc;

	if ((rc = sata_scr_read(link, SCR_CONTROL, &scontrol)))
		return rc;

	if (!__sata_set_spd_needed(link, &scontrol))
		return 0;

	if ((rc = sata_scr_write(link, SCR_CONTROL, scontrol)))
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
/*	{ XFER_PIO_SLOW, 120, 290, 240, 960, 290, 240, 960,   0 }, */
	{ XFER_PIO_0,     70, 290, 240, 600, 165, 150, 600,   0 },
	{ XFER_PIO_1,     50, 290,  93, 383, 125, 100, 383,   0 },
	{ XFER_PIO_2,     30, 290,  40, 330, 100,  90, 240,   0 },
	{ XFER_PIO_3,     30,  80,  70, 180,  80,  70, 180,   0 },
	{ XFER_PIO_4,     25,  70,  25, 120,  70,  25, 120,   0 },
	{ XFER_PIO_5,     15,  65,  25, 100,  65,  25, 100,   0 },
	{ XFER_PIO_6,     10,  55,  20,  80,  55,  20,  80,   0 },

	{ XFER_SW_DMA_0, 120,   0,   0,   0, 480, 480, 960,   0 },
	{ XFER_SW_DMA_1,  90,   0,   0,   0, 240, 240, 480,   0 },
	{ XFER_SW_DMA_2,  60,   0,   0,   0, 120, 120, 240,   0 },

	{ XFER_MW_DMA_0,  60,   0,   0,   0, 215, 215, 480,   0 },
	{ XFER_MW_DMA_1,  45,   0,   0,   0,  80,  50, 150,   0 },
	{ XFER_MW_DMA_2,  25,   0,   0,   0,  70,  25, 120,   0 },
	{ XFER_MW_DMA_3,  25,   0,   0,   0,  65,  25, 100,   0 },
	{ XFER_MW_DMA_4,  25,   0,   0,   0,  55,  20,  80,   0 },

/*	{ XFER_UDMA_SLOW,  0,   0,   0,   0,   0,   0,   0, 150 }, */
	{ XFER_UDMA_0,     0,   0,   0,   0,   0,   0,   0, 120 },
	{ XFER_UDMA_1,     0,   0,   0,   0,   0,   0,   0,  80 },
	{ XFER_UDMA_2,     0,   0,   0,   0,   0,   0,   0,  60 },
	{ XFER_UDMA_3,     0,   0,   0,   0,   0,   0,   0,  45 },
	{ XFER_UDMA_4,     0,   0,   0,   0,   0,   0,   0,  30 },
	{ XFER_UDMA_5,     0,   0,   0,   0,   0,   0,   0,  20 },
	{ XFER_UDMA_6,     0,   0,   0,   0,   0,   0,   0,  15 },

	{ 0xFF }
};

#define ENOUGH(v, unit)		(((v)-1)/(unit)+1)
#define EZ(v, unit)		((v)?ENOUGH(v, unit):0)

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

const struct ata_timing *ata_timing_find_mode(u8 xfer_mode)
{
	const struct ata_timing *t = ata_timing;

	while (xfer_mode > t->mode)
		t++;

	if (xfer_mode == t->mode)
		return t;
	return NULL;
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
		if (speed >= XFER_PIO_0 && speed <= XFER_SW_DMA_0) {
			if (speed <= XFER_PIO_2) p.cycle = p.cyc8b = adev->id[ATA_ID_EIDE_PIO];
					    else p.cycle = p.cyc8b = adev->id[ATA_ID_EIDE_PIO_IORDY];
		} else if (speed >= XFER_MW_DMA_0 && speed <= XFER_MW_DMA_2) {
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

	/* In a few cases quantisation may produce enough errors to
	   leave t->cycle too low for the sum of active and recovery
	   if so we must correct this */
	if (t->active + t->recover > t->cycle)
		t->cycle = t->active + t->recover;

	return 0;
}

/**
 *	ata_timing_cycle2mode - find xfer mode for the specified cycle duration
 *	@xfer_shift: ATA_SHIFT_* value for transfer type to examine.
 *	@cycle: cycle duration in ns
 *
 *	Return matching xfer mode for @cycle.  The returned mode is of
 *	the transfer type specified by @xfer_shift.  If @cycle is too
 *	slow for @xfer_shift, 0xff is returned.  If @cycle is faster
 *	than the fastest known mode, the fasted mode is returned.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Matching xfer_mode, 0xff if no match found.
 */
u8 ata_timing_cycle2mode(unsigned int xfer_shift, int cycle)
{
	u8 base_mode = 0xff, last_mode = 0xff;
	const struct ata_xfer_ent *ent;
	const struct ata_timing *t;

	for (ent = ata_xfer_tbl; ent->shift >= 0; ent++)
		if (ent->shift == xfer_shift)
			base_mode = ent->base;

	for (t = ata_timing_find_mode(base_mode);
	     t && ata_xfer_mode2shift(t->mode) == xfer_shift; t++) {
		unsigned short this_cycle;

		switch (xfer_shift) {
		case ATA_SHIFT_PIO:
		case ATA_SHIFT_MWDMA:
			this_cycle = t->cycle;
			break;
		case ATA_SHIFT_UDMA:
			this_cycle = t->udma;
			break;
		default:
			return 0xff;
		}

		if (cycle > this_cycle)
			break;

		last_mode = t->mode;
	}

	return last_mode;
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
	unsigned long orig_mask, xfer_mask;
	unsigned long pio_mask, mwdma_mask, udma_mask;
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
	struct ata_eh_context *ehc = &dev->link->eh_context;
	const char *dev_err_whine = "";
	int ign_dev_err = 0;
	unsigned int err_mask;
	int rc;

	dev->flags &= ~ATA_DFLAG_PIO;
	if (dev->xfer_shift == ATA_SHIFT_PIO)
		dev->flags |= ATA_DFLAG_PIO;

	err_mask = ata_dev_set_xfermode(dev);

	if (err_mask & ~AC_ERR_DEV)
		goto fail;

	/* revalidate */
	ehc->i.flags |= ATA_EHI_POST_SETMODE;
	rc = ata_dev_revalidate(dev, ATA_DEV_UNKNOWN, 0);
	ehc->i.flags &= ~ATA_EHI_POST_SETMODE;
	if (rc)
		return rc;

	if (dev->xfer_shift == ATA_SHIFT_PIO) {
		/* Old CFA may refuse this command, which is just fine */
		if (ata_id_is_cfa(dev->id))
			ign_dev_err = 1;
		/* Catch several broken garbage emulations plus some pre
		   ATA devices */
		if (ata_id_major_version(dev->id) == 0 &&
					dev->pio_mode <= XFER_PIO_2)
			ign_dev_err = 1;
		/* Some very old devices and some bad newer ones fail
		   any kind of SET_XFERMODE request but support PIO0-2
		   timings and no IORDY */
		if (!ata_id_has_iordy(dev->id) && dev->pio_mode <= XFER_PIO_2)
			ign_dev_err = 1;
	}
	/* Early MWDMA devices do DMA but don't allow DMA mode setting.
	   Don't fail an MWDMA0 set IFF the device indicates it is in MWDMA0 */
	if (dev->xfer_shift == ATA_SHIFT_MWDMA &&
	    dev->dma_mode == XFER_MW_DMA_0 &&
	    (dev->id[63] >> 8) & 1)
		ign_dev_err = 1;

	/* if the device is actually configured correctly, ignore dev err */
	if (dev->xfer_mode == ata_xfer_mask2mode(ata_id_xfermask(dev->id)))
		ign_dev_err = 1;

	if (err_mask & AC_ERR_DEV) {
		if (!ign_dev_err)
			goto fail;
		else
			dev_err_whine = " (device error ignored)";
	}

	DPRINTK("xfer_shift=%u, xfer_mode=0x%x\n",
		dev->xfer_shift, (int)dev->xfer_mode);

	ata_dev_printk(dev, KERN_INFO, "configured for %s%s\n",
		       ata_mode_string(ata_xfer_mode2mask(dev->xfer_mode)),
		       dev_err_whine);

	return 0;

 fail:
	ata_dev_printk(dev, KERN_ERR, "failed to set xfermode "
		       "(err_mask=0x%x)\n", err_mask);
	return -EIO;
}

/**
 *	ata_do_set_mode - Program timings and issue SET FEATURES - XFER
 *	@link: link on which timings will be programmed
 *	@r_failed_dev: out parameter for failed device
 *
 *	Standard implementation of the function used to tune and set
 *	ATA device disk transfer mode (PIO3, UDMA6, etc.).  If
 *	ata_dev_set_mode() fails, pointer to the failing device is
 *	returned in @r_failed_dev.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	0 on success, negative errno otherwise
 */

int ata_do_set_mode(struct ata_link *link, struct ata_device **r_failed_dev)
{
	struct ata_port *ap = link->ap;
	struct ata_device *dev;
	int rc = 0, used_dma = 0, found = 0;

	/* step 1: calculate xfer_mask */
	ata_link_for_each_dev(dev, link) {
		unsigned long pio_mask, dma_mask;
		unsigned int mode_mask;

		if (!ata_dev_enabled(dev))
			continue;

		mode_mask = ATA_DMA_MASK_ATA;
		if (dev->class == ATA_DEV_ATAPI)
			mode_mask = ATA_DMA_MASK_ATAPI;
		else if (ata_id_is_cfa(dev->id))
			mode_mask = ATA_DMA_MASK_CFA;

		ata_dev_xfermask(dev);
		ata_force_xfermask(dev);

		pio_mask = ata_pack_xfermask(dev->pio_mask, 0, 0);
		dma_mask = ata_pack_xfermask(0, dev->mwdma_mask, dev->udma_mask);

		if (libata_dma_mask & mode_mask)
			dma_mask = ata_pack_xfermask(0, dev->mwdma_mask, dev->udma_mask);
		else
			dma_mask = 0;

		dev->pio_mode = ata_xfer_mask2mode(pio_mask);
		dev->dma_mode = ata_xfer_mask2mode(dma_mask);

		found = 1;
		if (ata_dma_enabled(dev))
			used_dma = 1;
	}
	if (!found)
		goto out;

	/* step 2: always set host PIO timings */
	ata_link_for_each_dev(dev, link) {
		if (!ata_dev_enabled(dev))
			continue;

		if (dev->pio_mode == 0xff) {
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
	ata_link_for_each_dev(dev, link) {
		if (!ata_dev_enabled(dev) || !ata_dma_enabled(dev))
			continue;

		dev->xfer_mode = dev->dma_mode;
		dev->xfer_shift = ata_xfer_mode2shift(dev->dma_mode);
		if (ap->ops->set_dmamode)
			ap->ops->set_dmamode(ap, dev);
	}

	/* step 4: update devices' xfer mode */
	ata_link_for_each_dev(dev, link) {
		/* don't update suspended devices' xfer mode */
		if (!ata_dev_enabled(dev))
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

 out:
	if (rc)
		*r_failed_dev = dev;
	return rc;
}

/**
 *	ata_wait_ready - wait for link to become ready
 *	@link: link to be waited on
 *	@deadline: deadline jiffies for the operation
 *	@check_ready: callback to check link readiness
 *
 *	Wait for @link to become ready.  @check_ready should return
 *	positive number if @link is ready, 0 if it isn't, -ENODEV if
 *	link doesn't seem to be occupied, other errno for other error
 *	conditions.
 *
 *	Transient -ENODEV conditions are allowed for
 *	ATA_TMOUT_FF_WAIT.
 *
 *	LOCKING:
 *	EH context.
 *
 *	RETURNS:
 *	0 if @linke is ready before @deadline; otherwise, -errno.
 */
int ata_wait_ready(struct ata_link *link, unsigned long deadline,
		   int (*check_ready)(struct ata_link *link))
{
	unsigned long start = jiffies;
	unsigned long nodev_deadline = ata_deadline(start, ATA_TMOUT_FF_WAIT);
	int warned = 0;

	/* Slave readiness can't be tested separately from master.  On
	 * M/S emulation configuration, this function should be called
	 * only on the master and it will handle both master and slave.
	 */
	WARN_ON(link == link->ap->slave_link);

	if (time_after(nodev_deadline, deadline))
		nodev_deadline = deadline;

	while (1) {
		unsigned long now = jiffies;
		int ready, tmp;

		ready = tmp = check_ready(link);
		if (ready > 0)
			return 0;

		/* -ENODEV could be transient.  Ignore -ENODEV if link
		 * is online.  Also, some SATA devices take a long
		 * time to clear 0xff after reset.  For example,
		 * HHD424020F7SV00 iVDR needs >= 800ms while Quantum
		 * GoVault needs even more than that.  Wait for
		 * ATA_TMOUT_FF_WAIT on -ENODEV if link isn't offline.
		 *
		 * Note that some PATA controllers (pata_ali) explode
		 * if status register is read more than once when
		 * there's no device attached.
		 */
		if (ready == -ENODEV) {
			if (ata_link_online(link))
				ready = 0;
			else if ((link->ap->flags & ATA_FLAG_SATA) &&
				 !ata_link_offline(link) &&
				 time_before(now, nodev_deadline))
				ready = 0;
		}

		if (ready)
			return ready;
		if (time_after(now, deadline))
			return -EBUSY;

		if (!warned && time_after(now, start + 5 * HZ) &&
		    (deadline - now > 3 * HZ)) {
			ata_link_printk(link, KERN_WARNING,
				"link is slow to respond, please be patient "
				"(ready=%d)\n", tmp);
			warned = 1;
		}

		msleep(50);
	}
}

/**
 *	ata_wait_after_reset - wait for link to become ready after reset
 *	@link: link to be waited on
 *	@deadline: deadline jiffies for the operation
 *	@check_ready: callback to check link readiness
 *
 *	Wait for @link to become ready after reset.
 *
 *	LOCKING:
 *	EH context.
 *
 *	RETURNS:
 *	0 if @linke is ready before @deadline; otherwise, -errno.
 */
int ata_wait_after_reset(struct ata_link *link, unsigned long deadline,
				int (*check_ready)(struct ata_link *link))
{
	msleep(ATA_WAIT_AFTER_RESET);

	return ata_wait_ready(link, deadline, check_ready);
}

/**
 *	sata_link_debounce - debounce SATA phy status
 *	@link: ATA link to debounce SATA phy status for
 *	@params: timing parameters { interval, duratinon, timeout } in msec
 *	@deadline: deadline jiffies for the operation
 *
*	Make sure SStatus of @link reaches stable state, determined by
 *	holding the same value where DET is not 1 for @duration polled
 *	every @interval, before @timeout.  Timeout constraints the
 *	beginning of the stable state.  Because DET gets stuck at 1 on
 *	some controllers after hot unplugging, this functions waits
 *	until timeout then returns 0 if DET is stable at 1.
 *
 *	@timeout is further limited by @deadline.  The sooner of the
 *	two is used.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int sata_link_debounce(struct ata_link *link, const unsigned long *params,
		       unsigned long deadline)
{
	unsigned long interval = params[0];
	unsigned long duration = params[1];
	unsigned long last_jiffies, t;
	u32 last, cur;
	int rc;

	t = ata_deadline(jiffies, params[2]);
	if (time_before(t, deadline))
		deadline = t;

	if ((rc = sata_scr_read(link, SCR_STATUS, &cur)))
		return rc;
	cur &= 0xf;

	last = cur;
	last_jiffies = jiffies;

	while (1) {
		msleep(interval);
		if ((rc = sata_scr_read(link, SCR_STATUS, &cur)))
			return rc;
		cur &= 0xf;

		/* DET stable? */
		if (cur == last) {
			if (cur == 1 && time_before(jiffies, deadline))
				continue;
			if (time_after(jiffies,
				       ata_deadline(last_jiffies, duration)))
				return 0;
			continue;
		}

		/* unstable, start over */
		last = cur;
		last_jiffies = jiffies;

		/* Check deadline.  If debouncing failed, return
		 * -EPIPE to tell upper layer to lower link speed.
		 */
		if (time_after(jiffies, deadline))
			return -EPIPE;
	}
}

/**
 *	sata_link_resume - resume SATA link
 *	@link: ATA link to resume SATA
 *	@params: timing parameters { interval, duratinon, timeout } in msec
 *	@deadline: deadline jiffies for the operation
 *
 *	Resume SATA phy @link and debounce it.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int sata_link_resume(struct ata_link *link, const unsigned long *params,
		     unsigned long deadline)
{
	u32 scontrol, serror;
	int rc;

	if ((rc = sata_scr_read(link, SCR_CONTROL, &scontrol)))
		return rc;

	scontrol = (scontrol & 0x0f0) | 0x300;

	if ((rc = sata_scr_write(link, SCR_CONTROL, scontrol)))
		return rc;

	/* Some PHYs react badly if SStatus is pounded immediately
	 * after resuming.  Delay 200ms before debouncing.
	 */
	msleep(200);

	if ((rc = sata_link_debounce(link, params, deadline)))
		return rc;

	/* clear SError, some PHYs require this even for SRST to work */
	if (!(rc = sata_scr_read(link, SCR_ERROR, &serror)))
		rc = sata_scr_write(link, SCR_ERROR, serror);

	return rc != -EINVAL ? rc : 0;
}

/**
 *	ata_std_prereset - prepare for reset
 *	@link: ATA link to be reset
 *	@deadline: deadline jiffies for the operation
 *
 *	@link is about to be reset.  Initialize it.  Failure from
 *	prereset makes libata abort whole reset sequence and give up
 *	that port, so prereset should be best-effort.  It does its
 *	best to prepare for reset sequence but if things go wrong, it
 *	should just whine, not fail.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_std_prereset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct ata_eh_context *ehc = &link->eh_context;
	const unsigned long *timing = sata_ehc_deb_timing(ehc);
	int rc;

	/* if we're about to do hardreset, nothing more to do */
	if (ehc->i.action & ATA_EH_HARDRESET)
		return 0;

	/* if SATA, resume link */
	if (ap->flags & ATA_FLAG_SATA) {
		rc = sata_link_resume(link, timing, deadline);
		/* whine about phy resume failure but proceed */
		if (rc && rc != -EOPNOTSUPP)
			ata_link_printk(link, KERN_WARNING, "failed to resume "
					"link for reset (errno=%d)\n", rc);
	}

	/* no point in trying softreset on offline link */
	if (ata_phys_link_offline(link))
		ehc->i.action &= ~ATA_EH_SOFTRESET;

	return 0;
}

/**
 *	sata_link_hardreset - reset link via SATA phy reset
 *	@link: link to reset
 *	@timing: timing parameters { interval, duratinon, timeout } in msec
 *	@deadline: deadline jiffies for the operation
 *	@online: optional out parameter indicating link onlineness
 *	@check_ready: optional callback to check link readiness
 *
 *	SATA phy-reset @link using DET bits of SControl register.
 *	After hardreset, link readiness is waited upon using
 *	ata_wait_ready() if @check_ready is specified.  LLDs are
 *	allowed to not specify @check_ready and wait itself after this
 *	function returns.  Device classification is LLD's
 *	responsibility.
 *
 *	*@online is set to one iff reset succeeded and @link is online
 *	after reset.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int sata_link_hardreset(struct ata_link *link, const unsigned long *timing,
			unsigned long deadline,
			bool *online, int (*check_ready)(struct ata_link *))
{
	u32 scontrol;
	int rc;

	DPRINTK("ENTER\n");

	if (online)
		*online = false;

	if (sata_set_spd_needed(link)) {
		/* SATA spec says nothing about how to reconfigure
		 * spd.  To be on the safe side, turn off phy during
		 * reconfiguration.  This works for at least ICH7 AHCI
		 * and Sil3124.
		 */
		if ((rc = sata_scr_read(link, SCR_CONTROL, &scontrol)))
			goto out;

		scontrol = (scontrol & 0x0f0) | 0x304;

		if ((rc = sata_scr_write(link, SCR_CONTROL, scontrol)))
			goto out;

		sata_set_spd(link);
	}

	/* issue phy wake/reset */
	if ((rc = sata_scr_read(link, SCR_CONTROL, &scontrol)))
		goto out;

	scontrol = (scontrol & 0x0f0) | 0x301;

	if ((rc = sata_scr_write_flush(link, SCR_CONTROL, scontrol)))
		goto out;

	/* Couldn't find anything in SATA I/II specs, but AHCI-1.1
	 * 10.4.2 says at least 1 ms.
	 */
	msleep(1);

	/* bring link back */
	rc = sata_link_resume(link, timing, deadline);
	if (rc)
		goto out;
	/* if link is offline nothing more to do */
	if (ata_phys_link_offline(link))
		goto out;

	/* Link is online.  From this point, -ENODEV too is an error. */
	if (online)
		*online = true;

	if (sata_pmp_supported(link->ap) && ata_is_host_link(link)) {
		/* If PMP is supported, we have to do follow-up SRST.
		 * Some PMPs don't send D2H Reg FIS after hardreset if
		 * the first port is empty.  Wait only for
		 * ATA_TMOUT_PMP_SRST_WAIT.
		 */
		if (check_ready) {
			unsigned long pmp_deadline;

			pmp_deadline = ata_deadline(jiffies,
						    ATA_TMOUT_PMP_SRST_WAIT);
			if (time_after(pmp_deadline, deadline))
				pmp_deadline = deadline;
			ata_wait_ready(link, pmp_deadline, check_ready);
		}
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	if (check_ready)
		rc = ata_wait_ready(link, deadline, check_ready);
 out:
	if (rc && rc != -EAGAIN) {
		/* online is set iff link is online && reset succeeded */
		if (online)
			*online = false;
		ata_link_printk(link, KERN_ERR,
				"COMRESET failed (errno=%d)\n", rc);
	}
	DPRINTK("EXIT, rc=%d\n", rc);
	return rc;
}

/**
 *	sata_std_hardreset - COMRESET w/o waiting or classification
 *	@link: link to reset
 *	@class: resulting class of attached device
 *	@deadline: deadline jiffies for the operation
 *
 *	Standard SATA COMRESET w/o waiting or classification.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 if link offline, -EAGAIN if link online, -errno on errors.
 */
int sata_std_hardreset(struct ata_link *link, unsigned int *class,
		       unsigned long deadline)
{
	const unsigned long *timing = sata_ehc_deb_timing(&link->eh_context);
	bool online;
	int rc;

	/* do hardreset */
	rc = sata_link_hardreset(link, timing, deadline, &online, NULL);
	return online ? -EAGAIN : rc;
}

/**
 *	ata_std_postreset - standard postreset callback
 *	@link: the target ata_link
 *	@classes: classes of attached devices
 *
 *	This function is invoked after a successful reset.  Note that
 *	the device might have been reset more than once using
 *	different reset methods before postreset is invoked.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */
void ata_std_postreset(struct ata_link *link, unsigned int *classes)
{
	u32 serror;

	DPRINTK("ENTER\n");

	/* reset complete, clear SError */
	if (!sata_scr_read(link, SCR_ERROR, &serror))
		sata_scr_write(link, SCR_ERROR, serror);

	/* print link status */
	sata_print_link_status(link);

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

	if (dev->class != new_class) {
		ata_dev_printk(dev, KERN_INFO, "class mismatch %d != %d\n",
			       dev->class, new_class);
		return 0;
	}

	ata_id_c_string(old_id, model[0], ATA_ID_PROD, sizeof(model[0]));
	ata_id_c_string(new_id, model[1], ATA_ID_PROD, sizeof(model[1]));
	ata_id_c_string(old_id, serial[0], ATA_ID_SERNO, sizeof(serial[0]));
	ata_id_c_string(new_id, serial[1], ATA_ID_SERNO, sizeof(serial[1]));

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

	return 1;
}

/**
 *	ata_dev_reread_id - Re-read IDENTIFY data
 *	@dev: target ATA device
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
int ata_dev_reread_id(struct ata_device *dev, unsigned int readid_flags)
{
	unsigned int class = dev->class;
	u16 *id = (void *)dev->link->ap->sector_buf;
	int rc;

	/* read ID data */
	rc = ata_dev_read_id(dev, &class, readid_flags, id);
	if (rc)
		return rc;

	/* is the device still there? */
	if (!ata_dev_same_device(dev, class, id))
		return -ENODEV;

	memcpy(dev->id, id, sizeof(id[0]) * ATA_ID_WORDS);
	return 0;
}

/**
 *	ata_dev_revalidate - Revalidate ATA device
 *	@dev: device to revalidate
 *	@new_class: new class code
 *	@readid_flags: read ID flags
 *
 *	Re-read IDENTIFY page, make sure @dev is still attached to the
 *	port and reconfigure it according to the new IDENTIFY page.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, negative errno otherwise
 */
int ata_dev_revalidate(struct ata_device *dev, unsigned int new_class,
		       unsigned int readid_flags)
{
	u64 n_sectors = dev->n_sectors;
	int rc;

	if (!ata_dev_enabled(dev))
		return -ENODEV;

	/* fail early if !ATA && !ATAPI to avoid issuing [P]IDENTIFY to PMP */
	if (ata_class_enabled(new_class) &&
	    new_class != ATA_DEV_ATA && new_class != ATA_DEV_ATAPI) {
		ata_dev_printk(dev, KERN_INFO, "class mismatch %u != %u\n",
			       dev->class, new_class);
		rc = -ENODEV;
		goto fail;
	}

	/* re-read ID */
	rc = ata_dev_reread_id(dev, readid_flags);
	if (rc)
		goto fail;

	/* configure device according to the new ID */
	rc = ata_dev_configure(dev);
	if (rc)
		goto fail;

	/* verify n_sectors hasn't changed */
	if (dev->class == ATA_DEV_ATA && n_sectors &&
	    dev->n_sectors != n_sectors) {
		ata_dev_printk(dev, KERN_INFO, "n_sectors mismatch "
			       "%llu != %llu\n",
			       (unsigned long long)n_sectors,
			       (unsigned long long)dev->n_sectors);

		/* restore original n_sectors */
		dev->n_sectors = n_sectors;

		rc = -ENODEV;
		goto fail;
	}

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
	{ "SAMSUNG CD-ROM SN-124", "N001",	ATA_HORKAGE_NODMA },
	{ "Seagate STT20000A", NULL,		ATA_HORKAGE_NODMA },
	/* Odd clown on sil3726/4726 PMPs */
	{ "Config  Disk",	NULL,		ATA_HORKAGE_DISABLE },

	/* Weird ATAPI devices */
	{ "TORiSAN DVD-ROM DRD-N216", NULL,	ATA_HORKAGE_MAX_SEC_128 },

	/* Devices we expect to fail diagnostics */

	/* Devices where NCQ should be avoided */
	/* NCQ is slow */
	{ "WDC WD740ADFD-00",	NULL,		ATA_HORKAGE_NONCQ },
	{ "WDC WD740ADFD-00NLR1", NULL,		ATA_HORKAGE_NONCQ, },
	/* http://thread.gmane.org/gmane.linux.ide/14907 */
	{ "FUJITSU MHT2060BH",	NULL,		ATA_HORKAGE_NONCQ },
	/* NCQ is broken */
	{ "Maxtor *",		"BANC*",	ATA_HORKAGE_NONCQ },
	{ "Maxtor 7V300F0",	"VA111630",	ATA_HORKAGE_NONCQ },
	{ "ST380817AS",		"3.42",		ATA_HORKAGE_NONCQ },
	{ "ST3160023AS",	"3.42",		ATA_HORKAGE_NONCQ },

	/* Blacklist entries taken from Silicon Image 3124/3132
	   Windows driver .inf file - also several Linux problem reports */
	{ "HTS541060G9SA00",    "MB3OC60D",     ATA_HORKAGE_NONCQ, },
	{ "HTS541080G9SA00",    "MB4OC60D",     ATA_HORKAGE_NONCQ, },
	{ "HTS541010G9SA00",    "MBZOC60D",     ATA_HORKAGE_NONCQ, },

	/* devices which puke on READ_NATIVE_MAX */
	{ "HDS724040KLSA80",	"KFAOA20N",	ATA_HORKAGE_BROKEN_HPA, },
	{ "WDC WD3200JD-00KLB0", "WD-WCAMR1130137", ATA_HORKAGE_BROKEN_HPA },
	{ "WDC WD2500JD-00HBB0", "WD-WMAL71490727", ATA_HORKAGE_BROKEN_HPA },
	{ "MAXTOR 6L080L4",	"A93.0500",	ATA_HORKAGE_BROKEN_HPA },

	/* Devices which report 1 sector over size HPA */
	{ "ST340823A",		NULL,		ATA_HORKAGE_HPA_SIZE, },
	{ "ST320413A",		NULL,		ATA_HORKAGE_HPA_SIZE, },
	{ "ST310211A",		NULL,		ATA_HORKAGE_HPA_SIZE, },

	/* Devices which get the IVB wrong */
	{ "QUANTUM FIREBALLlct10 05", "A03.0900", ATA_HORKAGE_IVB, },
	/* Maybe we should just blacklist TSSTcorp... */
	{ "TSSTcorp CDDVDW SH-S202H", "SB00",	  ATA_HORKAGE_IVB, },
	{ "TSSTcorp CDDVDW SH-S202H", "SB01",	  ATA_HORKAGE_IVB, },
	{ "TSSTcorp CDDVDW SH-S202J", "SB00",	  ATA_HORKAGE_IVB, },
	{ "TSSTcorp CDDVDW SH-S202J", "SB01",	  ATA_HORKAGE_IVB, },
	{ "TSSTcorp CDDVDW SH-S202N", "SB00",	  ATA_HORKAGE_IVB, },
	{ "TSSTcorp CDDVDW SH-S202N", "SB01",	  ATA_HORKAGE_IVB, },

	/* End Marker */
	{ }
};

static int strn_pattern_cmp(const char *patt, const char *name, int wildchar)
{
	const char *p;
	int len;

	/*
	 * check for trailing wildcard: *\0
	 */
	p = strchr(patt, wildchar);
	if (p && ((*(p + 1)) == 0))
		len = p - patt;
	else {
		len = strlen(name);
		if (!len) {
			if (!*patt)
				return 0;
			return -1;
		}
	}

	return strncmp(patt, name, len);
}

static unsigned long ata_dev_blacklisted(const struct ata_device *dev)
{
	unsigned char model_num[ATA_ID_PROD_LEN + 1];
	unsigned char model_rev[ATA_ID_FW_REV_LEN + 1];
	const struct ata_blacklist_entry *ad = ata_device_blacklist;

	ata_id_c_string(dev->id, model_num, ATA_ID_PROD, sizeof(model_num));
	ata_id_c_string(dev->id, model_rev, ATA_ID_FW_REV, sizeof(model_rev));

	while (ad->model_num) {
		if (!strn_pattern_cmp(ad->model_num, model_num, '*')) {
			if (ad->model_rev == NULL)
				return ad->horkage;
			if (!strn_pattern_cmp(ad->model_rev, model_rev, '*'))
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
	if ((dev->link->ap->flags & ATA_FLAG_PIO_POLLING) &&
	    (dev->flags & ATA_DFLAG_CDB_INTR))
		return 1;
	return (dev->horkage & ATA_HORKAGE_NODMA) ? 1 : 0;
}

/**
 *	ata_is_40wire		-	check drive side detection
 *	@dev: device
 *
 *	Perform drive side detection decoding, allowing for device vendors
 *	who can't follow the documentation.
 */

static int ata_is_40wire(struct ata_device *dev)
{
	if (dev->horkage & ATA_HORKAGE_IVB)
		return ata_drive_40wire_relaxed(dev->id);
	return ata_drive_40wire(dev->id);
}

/**
 *	cable_is_40wire		-	40/80/SATA decider
 *	@ap: port to consider
 *
 *	This function encapsulates the policy for speed management
 *	in one place. At the moment we don't cache the result but
 *	there is a good case for setting ap->cbl to the result when
 *	we are called with unknown cables (and figuring out if it
 *	impacts hotplug at all).
 *
 *	Return 1 if the cable appears to be 40 wire.
 */

static int cable_is_40wire(struct ata_port *ap)
{
	struct ata_link *link;
	struct ata_device *dev;

	/* If the controller thinks we are 40 wire, we are */
	if (ap->cbl == ATA_CBL_PATA40)
		return 1;
	/* If the controller thinks we are 80 wire, we are */
	if (ap->cbl == ATA_CBL_PATA80 || ap->cbl == ATA_CBL_SATA)
		return 0;
	/* If the system is known to be 40 wire short cable (eg laptop),
	   then we allow 80 wire modes even if the drive isn't sure */
	if (ap->cbl == ATA_CBL_PATA40_SHORT)
		return 0;
	/* If the controller doesn't know we scan

	   - Note: We look for all 40 wire detects at this point.
	     Any 80 wire detect is taken to be 80 wire cable
	     because
	     - In many setups only the one drive (slave if present)
               will give a valid detect
             - If you have a non detect capable drive you don't
               want it to colour the choice
        */
	ata_port_for_each_link(link, ap) {
		ata_link_for_each_dev(dev, link) {
			if (!ata_is_40wire(dev))
				return 0;
		}
	}
	return 1;
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
	struct ata_link *link = dev->link;
	struct ata_port *ap = link->ap;
	struct ata_host *host = ap->host;
	unsigned long xfer_mask;

	/* controller modes available */
	xfer_mask = ata_pack_xfermask(ap->pio_mask,
				      ap->mwdma_mask, ap->udma_mask);

	/* drive modes available */
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

	if (ap->flags & ATA_FLAG_NO_IORDY)
		xfer_mask &= ata_pio_mask_no_iordy(dev);

	if (ap->ops->mode_filter)
		xfer_mask = ap->ops->mode_filter(dev, xfer_mask);

	/* Apply cable rule here.  Don't apply it early because when
	 * we handle hot plug the cable type can itself change.
	 * Check this last so that we know if the transfer rate was
	 * solely limited by the cable.
	 * Unknown or 80 wire cables reported host side are checked
	 * drive side as well. Cases where we know a 40wire cable
	 * is used safely for 80 are not checked here.
	 */
	if (xfer_mask & (0xF8 << ATA_SHIFT_UDMA))
		/* UDMA/44 or higher would be available */
		if (cable_is_40wire(ap)) {
			ata_dev_printk(dev, KERN_WARNING,
				 "limited to UDMA/33 due to 40-wire cable\n");
			xfer_mask &= ~(0xF8 << ATA_SHIFT_UDMA);
		}

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

	/* Some controllers and ATAPI devices show flaky interrupt
	 * behavior after setting xfer mode.  Use polling instead.
	 */
	ata_tf_init(dev, &tf);
	tf.command = ATA_CMD_SET_FEATURES;
	tf.feature = SETFEATURES_XFER;
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE | ATA_TFLAG_POLLING;
	tf.protocol = ATA_PROT_NODATA;
	/* If we are using IORDY we must send the mode setting command */
	if (ata_pio_need_iordy(dev))
		tf.nsect = dev->xfer_mode;
	/* If the device has IORDY and the controller does not - turn it off */
 	else if (ata_id_has_iordy(dev->id))
		tf.nsect = 0x01;
	else /* In the ancient relic department - skip all of this */
		return 0;

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0, 0);

	DPRINTK("EXIT, err_mask=%x\n", err_mask);
	return err_mask;
}
/**
 *	ata_dev_set_feature - Issue SET FEATURES - SATA FEATURES
 *	@dev: Device to which command will be sent
 *	@enable: Whether to enable or disable the feature
 *	@feature: The sector count represents the feature to set
 *
 *	Issue SET FEATURES - SATA FEATURES command to device @dev
 *	on port @ap with sector count
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	0 on success, AC_ERR_* mask otherwise.
 */
static unsigned int ata_dev_set_feature(struct ata_device *dev, u8 enable,
					u8 feature)
{
	struct ata_taskfile tf;
	unsigned int err_mask;

	/* set up set-features taskfile */
	DPRINTK("set features - SATA features\n");

	ata_tf_init(dev, &tf);
	tf.command = ATA_CMD_SET_FEATURES;
	tf.feature = enable;
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.protocol = ATA_PROT_NODATA;
	tf.nsect = feature;

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0, 0);

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

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0, 0);
	/* A clean abort indicates an original or just out of spec drive
	   and we should continue as we issue the setup based on the
	   drive reported working geometry */
	if (err_mask == AC_ERR_DEV && (tf.feature & ATA_ABORTED))
		err_mask = 0;

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
	struct scatterlist *sg = qc->sg;
	int dir = qc->dma_dir;

	WARN_ON(sg == NULL);

	VPRINTK("unmapping %u sg elements\n", qc->n_elem);

	if (qc->n_elem)
		dma_unmap_sg(ap->dev, sg, qc->n_elem, dir);

	qc->flags &= ~ATA_QCFLAG_DMAMAP;
	qc->sg = NULL;
}

/**
 *	atapi_check_dma - Check whether ATAPI DMA can be supported
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
int atapi_check_dma(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	/* Don't allow DMA if it isn't multiple of 16 bytes.  Quite a
	 * few ATAPI devices choke on such DMA requests.
	 */
	if (unlikely(qc->nbytes & 15))
		return 1;

	if (ap->ops->check_atapi_dma)
		return ap->ops->check_atapi_dma(qc);

	return 0;
}

/**
 *	ata_std_qc_defer - Check whether a qc needs to be deferred
 *	@qc: ATA command in question
 *
 *	Non-NCQ commands cannot run with any other command, NCQ or
 *	not.  As upper layer only knows the queue depth, we are
 *	responsible for maintaining exclusion.  This function checks
 *	whether a new command @qc can be issued.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	ATA_DEFER_* if deferring is needed, 0 otherwise.
 */
int ata_std_qc_defer(struct ata_queued_cmd *qc)
{
	struct ata_link *link = qc->dev->link;

	if (qc->tf.protocol == ATA_PROT_NCQ) {
		if (!ata_tag_valid(link->active_tag))
			return 0;
	} else {
		if (!ata_tag_valid(link->active_tag) && !link->sactive)
			return 0;
	}

	return ATA_DEFER_LINK;
}

void ata_noop_qc_prep(struct ata_queued_cmd *qc) { }

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
	qc->sg = sg;
	qc->n_elem = n_elem;
	qc->cursg = qc->sg;
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
	unsigned int n_elem;

	VPRINTK("ENTER, ata%u\n", ap->print_id);

	n_elem = dma_map_sg(ap->dev, qc->sg, qc->n_elem, qc->dma_dir);
	if (n_elem < 1)
		return -1;

	DPRINTK("%d sg elements mapped\n", n_elem);

	qc->n_elem = n_elem;
	qc->flags |= ATA_QCFLAG_DMAMAP;

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
	struct ata_port *ap = dev->link->ap;
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
	struct ata_link *link = qc->dev->link;

	WARN_ON(qc == NULL);	/* ata_qc_from_tag _might_ return NULL */
	WARN_ON(!(qc->flags & ATA_QCFLAG_ACTIVE));

	if (likely(qc->flags & ATA_QCFLAG_DMAMAP))
		ata_sg_clean(qc);

	/* command should be marked inactive atomically with qc completion */
	if (qc->tf.protocol == ATA_PROT_NCQ) {
		link->sactive &= ~(1 << qc->tag);
		if (!link->sactive)
			ap->nr_active_links--;
	} else {
		link->active_tag = ATA_TAG_POISON;
		ap->nr_active_links--;
	}

	/* clear exclusive status */
	if (unlikely(qc->flags & ATA_QCFLAG_CLEAR_EXCL &&
		     ap->excl_link == link))
		ap->excl_link = NULL;

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
	ap->ops->qc_fill_rtf(qc);
}

static void ata_verify_xfer(struct ata_queued_cmd *qc)
{
	struct ata_device *dev = qc->dev;

	if (ata_tag_internal(qc->tag))
		return;

	if (ata_is_nodata(qc->tf.protocol))
		return;

	if ((dev->mwdma_mask || dev->udma_mask) && ata_is_pio(qc->tf.protocol))
		return;

	dev->flags &= ~ATA_DFLAG_DUBIOUS_XFER;
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
		struct ata_device *dev = qc->dev;
		struct ata_eh_info *ehi = &dev->link->eh_info;

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

		/* Some commands need post-processing after successful
		 * completion.
		 */
		switch (qc->tf.command) {
		case ATA_CMD_SET_FEATURES:
			if (qc->tf.feature != SETFEATURES_WC_ON &&
			    qc->tf.feature != SETFEATURES_WC_OFF)
				break;
			/* fall through */
		case ATA_CMD_INIT_DEV_PARAMS: /* CHS translation changed */
		case ATA_CMD_SET_MULTI: /* multi_count changed */
			/* revalidate device */
			ehi->dev_action[dev->devno] |= ATA_EH_REVALIDATE;
			ata_port_schedule_eh(ap);
			break;

		case ATA_CMD_SLEEP:
			dev->flags |= ATA_DFLAG_SLEEPING;
			break;
		}

		if (unlikely(dev->flags & ATA_DFLAG_DUBIOUS_XFER))
			ata_verify_xfer(qc);

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
int ata_qc_complete_multiple(struct ata_port *ap, u32 qc_active)
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
			ata_qc_complete(qc);
			nr_done++;
		}
	}

	return nr_done;
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
	struct ata_link *link = qc->dev->link;
	u8 prot = qc->tf.protocol;

	/* Make sure only one non-NCQ command is outstanding.  The
	 * check is skipped for old EH because it reuses active qc to
	 * request ATAPI sense.
	 */
	WARN_ON(ap->ops->error_handler && ata_tag_valid(link->active_tag));

	if (ata_is_ncq(prot)) {
		WARN_ON(link->sactive & (1 << qc->tag));

		if (!link->sactive)
			ap->nr_active_links++;
		link->sactive |= 1 << qc->tag;
	} else {
		WARN_ON(link->sactive);

		ap->nr_active_links++;
		link->active_tag = qc->tag;
	}

	qc->flags |= ATA_QCFLAG_ACTIVE;
	ap->qc_active |= 1 << qc->tag;

	/* We guarantee to LLDs that they will have at least one
	 * non-zero sg if the command is a data command.
	 */
	BUG_ON(ata_is_data(prot) && (!qc->sg || !qc->n_elem || !qc->nbytes));

	if (ata_is_dma(prot) || (ata_is_pio(prot) &&
				 (ap->flags & ATA_FLAG_PIO_DMA)))
		if (ata_sg_setup(qc))
			goto sg_err;

	/* if device is sleeping, schedule reset and abort the link */
	if (unlikely(qc->dev->flags & ATA_DFLAG_SLEEPING)) {
		link->eh_info.action |= ATA_EH_RESET;
		ata_ehi_push_desc(&link->eh_info, "waking up from sleep");
		ata_link_abort(link);
		return;
	}

	ap->ops->qc_prep(qc);

	qc->err_mask |= ap->ops->qc_issue(qc);
	if (unlikely(qc->err_mask))
		goto err;
	return;

sg_err:
	qc->err_mask |= AC_ERR_SYSTEM;
err:
	ata_qc_complete(qc);
}

/**
 *	sata_scr_valid - test whether SCRs are accessible
 *	@link: ATA link to test SCR accessibility for
 *
 *	Test whether SCRs are accessible for @link.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	1 if SCRs are accessible, 0 otherwise.
 */
int sata_scr_valid(struct ata_link *link)
{
	struct ata_port *ap = link->ap;

	return (ap->flags & ATA_FLAG_SATA) && ap->ops->scr_read;
}

/**
 *	sata_scr_read - read SCR register of the specified port
 *	@link: ATA link to read SCR for
 *	@reg: SCR to read
 *	@val: Place to store read value
 *
 *	Read SCR register @reg of @link into *@val.  This function is
 *	guaranteed to succeed if @link is ap->link, the cable type of
 *	the port is SATA and the port implements ->scr_read.
 *
 *	LOCKING:
 *	None if @link is ap->link.  Kernel thread context otherwise.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure.
 */
int sata_scr_read(struct ata_link *link, int reg, u32 *val)
{
	if (ata_is_host_link(link)) {
		if (sata_scr_valid(link))
			return link->ap->ops->scr_read(link, reg, val);
		return -EOPNOTSUPP;
	}

	return sata_pmp_scr_read(link, reg, val);
}

/**
 *	sata_scr_write - write SCR register of the specified port
 *	@link: ATA link to write SCR for
 *	@reg: SCR to write
 *	@val: value to write
 *
 *	Write @val to SCR register @reg of @link.  This function is
 *	guaranteed to succeed if @link is ap->link, the cable type of
 *	the port is SATA and the port implements ->scr_read.
 *
 *	LOCKING:
 *	None if @link is ap->link.  Kernel thread context otherwise.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure.
 */
int sata_scr_write(struct ata_link *link, int reg, u32 val)
{
	if (ata_is_host_link(link)) {
		if (sata_scr_valid(link))
			return link->ap->ops->scr_write(link, reg, val);
		return -EOPNOTSUPP;
	}

	return sata_pmp_scr_write(link, reg, val);
}

/**
 *	sata_scr_write_flush - write SCR register of the specified port and flush
 *	@link: ATA link to write SCR for
 *	@reg: SCR to write
 *	@val: value to write
 *
 *	This function is identical to sata_scr_write() except that this
 *	function performs flush after writing to the register.
 *
 *	LOCKING:
 *	None if @link is ap->link.  Kernel thread context otherwise.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure.
 */
int sata_scr_write_flush(struct ata_link *link, int reg, u32 val)
{
	if (ata_is_host_link(link)) {
		int rc;

		if (sata_scr_valid(link)) {
			rc = link->ap->ops->scr_write(link, reg, val);
			if (rc == 0)
				rc = link->ap->ops->scr_read(link, reg, &val);
			return rc;
		}
		return -EOPNOTSUPP;
	}

	return sata_pmp_scr_write(link, reg, val);
}

/**
 *	ata_phys_link_online - test whether the given link is online
 *	@link: ATA link to test
 *
 *	Test whether @link is online.  Note that this function returns
 *	0 if online status of @link cannot be obtained, so
 *	ata_link_online(link) != !ata_link_offline(link).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	True if the port online status is available and online.
 */
bool ata_phys_link_online(struct ata_link *link)
{
	u32 sstatus;

	if (sata_scr_read(link, SCR_STATUS, &sstatus) == 0 &&
	    (sstatus & 0xf) == 0x3)
		return true;
	return false;
}

/**
 *	ata_phys_link_offline - test whether the given link is offline
 *	@link: ATA link to test
 *
 *	Test whether @link is offline.  Note that this function
 *	returns 0 if offline status of @link cannot be obtained, so
 *	ata_link_online(link) != !ata_link_offline(link).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	True if the port offline status is available and offline.
 */
bool ata_phys_link_offline(struct ata_link *link)
{
	u32 sstatus;

	if (sata_scr_read(link, SCR_STATUS, &sstatus) == 0 &&
	    (sstatus & 0xf) != 0x3)
		return true;
	return false;
}

/**
 *	ata_link_online - test whether the given link is online
 *	@link: ATA link to test
 *
 *	Test whether @link is online.  This is identical to
 *	ata_phys_link_online() when there's no slave link.  When
 *	there's a slave link, this function should only be called on
 *	the master link and will return true if any of M/S links is
 *	online.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	True if the port online status is available and online.
 */
bool ata_link_online(struct ata_link *link)
{
	struct ata_link *slave = link->ap->slave_link;

	WARN_ON(link == slave);	/* shouldn't be called on slave link */

	return ata_phys_link_online(link) ||
		(slave && ata_phys_link_online(slave));
}

/**
 *	ata_link_offline - test whether the given link is offline
 *	@link: ATA link to test
 *
 *	Test whether @link is offline.  This is identical to
 *	ata_phys_link_offline() when there's no slave link.  When
 *	there's a slave link, this function should only be called on
 *	the master link and will return true if both M/S links are
 *	offline.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	True if the port offline status is available and offline.
 */
bool ata_link_offline(struct ata_link *link)
{
	struct ata_link *slave = link->ap->slave_link;

	WARN_ON(link == slave);	/* shouldn't be called on slave link */

	return ata_phys_link_offline(link) &&
		(!slave || ata_phys_link_offline(slave));
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
		struct ata_link *link;

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
		__ata_port_for_each_link(link, ap) {
			link->eh_info.action |= action;
			link->eh_info.flags |= ehi_flags;
		}

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
	int rc;

	/*
	 * disable link pm on all ports before requesting
	 * any pm activity
	 */
	ata_lpm_enable(host);

	rc = ata_host_request_pm(host, mesg, 0, ATA_EHI_QUIET, 1);
	if (rc == 0)
		host->dev->power.power_state = mesg;
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
	ata_host_request_pm(host, PMSG_ON, ATA_EH_RESET,
			    ATA_EHI_NO_AUTOPSY | ATA_EHI_QUIET, 0);
	host->dev->power.power_state = PMSG_ON;

	/* reenable link pm */
	ata_lpm_disable(host);
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

	ap->prd = dmam_alloc_coherent(dev, ATA_PRD_TBL_SZ, &ap->prd_dma,
				      GFP_KERNEL);
	if (!ap->prd)
		return -ENOMEM;

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
	struct ata_link *link = ata_dev_phys_link(dev);
	struct ata_port *ap = link->ap;
	unsigned long flags;

	/* SATA spd limit is bound to the attached device, reset together */
	link->sata_spd_limit = link->hw_sata_spd_limit;
	link->sata_spd = 0;

	/* High bits of dev->flags are used to record warm plug
	 * requests which occur asynchronously.  Synchronize using
	 * host lock.
	 */
	spin_lock_irqsave(ap->lock, flags);
	dev->flags &= ~ATA_DFLAG_INIT_MASK;
	dev->horkage = 0;
	spin_unlock_irqrestore(ap->lock, flags);

	memset((void *)dev + ATA_DEVICE_CLEAR_OFFSET, 0,
	       sizeof(*dev) - ATA_DEVICE_CLEAR_OFFSET);
	dev->pio_mask = UINT_MAX;
	dev->mwdma_mask = UINT_MAX;
	dev->udma_mask = UINT_MAX;
}

/**
 *	ata_link_init - Initialize an ata_link structure
 *	@ap: ATA port link is attached to
 *	@link: Link structure to initialize
 *	@pmp: Port multiplier port number
 *
 *	Initialize @link.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */
void ata_link_init(struct ata_port *ap, struct ata_link *link, int pmp)
{
	int i;

	/* clear everything except for devices */
	memset(link, 0, offsetof(struct ata_link, device[0]));

	link->ap = ap;
	link->pmp = pmp;
	link->active_tag = ATA_TAG_POISON;
	link->hw_sata_spd_limit = UINT_MAX;

	/* can't use iterator, ap isn't initialized yet */
	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		struct ata_device *dev = &link->device[i];

		dev->link = link;
		dev->devno = dev - link->device;
		ata_dev_init(dev);
	}
}

/**
 *	sata_link_init_spd - Initialize link->sata_spd_limit
 *	@link: Link to configure sata_spd_limit for
 *
 *	Initialize @link->[hw_]sata_spd_limit to the currently
 *	configured value.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int sata_link_init_spd(struct ata_link *link)
{
	u8 spd;
	int rc;

	rc = sata_scr_read(link, SCR_CONTROL, &link->saved_scontrol);
	if (rc)
		return rc;

	spd = (link->saved_scontrol >> 4) & 0xf;
	if (spd)
		link->hw_sata_spd_limit &= (1 << spd) - 1;

	ata_force_link_limits(link);

	link->sata_spd_limit = link->hw_sata_spd_limit;

	return 0;
}

/**
 *	ata_port_alloc - allocate and initialize basic ATA port resources
 *	@host: ATA host this allocated port belongs to
 *
 *	Allocate and initialize basic ATA port resources.
 *
 *	RETURNS:
 *	Allocate ATA port on success, NULL on failure.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 */
struct ata_port *ata_port_alloc(struct ata_host *host)
{
	struct ata_port *ap;

	DPRINTK("ENTER\n");

	ap = kzalloc(sizeof(*ap), GFP_KERNEL);
	if (!ap)
		return NULL;

	ap->pflags |= ATA_PFLAG_INITIALIZING;
	ap->lock = &host->lock;
	ap->flags = ATA_FLAG_DISABLED;
	ap->print_id = -1;
	ap->ctl = ATA_DEVCTL_OBS;
	ap->host = host;
	ap->dev = host->dev;
	ap->last_ctl = 0xFF;

#if defined(ATA_VERBOSE_DEBUG)
	/* turn on all debugging levels */
	ap->msg_enable = 0x00FF;
#elif defined(ATA_DEBUG)
	ap->msg_enable = ATA_MSG_DRV | ATA_MSG_INFO | ATA_MSG_CTL | ATA_MSG_WARN | ATA_MSG_ERR;
#else
	ap->msg_enable = ATA_MSG_DRV | ATA_MSG_ERR | ATA_MSG_WARN;
#endif

#ifdef CONFIG_ATA_SFF
	INIT_DELAYED_WORK(&ap->port_task, ata_pio_task);
#endif
	INIT_DELAYED_WORK(&ap->hotplug_task, ata_scsi_hotplug);
	INIT_WORK(&ap->scsi_rescan_task, ata_scsi_dev_rescan);
	INIT_LIST_HEAD(&ap->eh_done_q);
	init_waitqueue_head(&ap->eh_wait_q);
	init_completion(&ap->park_req_pending);
	init_timer_deferrable(&ap->fastdrain_timer);
	ap->fastdrain_timer.function = ata_eh_fastdrain_timerfn;
	ap->fastdrain_timer.data = (unsigned long)ap;

	ap->cbl = ATA_CBL_NONE;

	ata_link_init(ap, &ap->link, 0);

#ifdef ATA_IRQ_TRAP
	ap->stats.unhandled_irq = 1;
	ap->stats.idle_irq = 1;
#endif
	return ap;
}

static void ata_host_release(struct device *gendev, void *res)
{
	struct ata_host *host = dev_get_drvdata(gendev);
	int i;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		if (!ap)
			continue;

		if (ap->scsi_host)
			scsi_host_put(ap->scsi_host);

		kfree(ap->pmp_link);
		kfree(ap->slave_link);
		kfree(ap);
		host->ports[i] = NULL;
	}

	dev_set_drvdata(gendev, NULL);
}

/**
 *	ata_host_alloc - allocate and init basic ATA host resources
 *	@dev: generic device this host is associated with
 *	@max_ports: maximum number of ATA ports associated with this host
 *
 *	Allocate and initialize basic ATA host resources.  LLD calls
 *	this function to allocate a host, initializes it fully and
 *	attaches it using ata_host_register().
 *
 *	@max_ports ports are allocated and host->n_ports is
 *	initialized to @max_ports.  The caller is allowed to decrease
 *	host->n_ports before calling ata_host_register().  The unused
 *	ports will be automatically freed on registration.
 *
 *	RETURNS:
 *	Allocate ATA host on success, NULL on failure.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 */
struct ata_host *ata_host_alloc(struct device *dev, int max_ports)
{
	struct ata_host *host;
	size_t sz;
	int i;

	DPRINTK("ENTER\n");

	if (!devres_open_group(dev, NULL, GFP_KERNEL))
		return NULL;

	/* alloc a container for our list of ATA ports (buses) */
	sz = sizeof(struct ata_host) + (max_ports + 1) * sizeof(void *);
	/* alloc a container for our list of ATA ports (buses) */
	host = devres_alloc(ata_host_release, sz, GFP_KERNEL);
	if (!host)
		goto err_out;

	devres_add(dev, host);
	dev_set_drvdata(dev, host);

	spin_lock_init(&host->lock);
	host->dev = dev;
	host->n_ports = max_ports;

	/* allocate ports bound to this host */
	for (i = 0; i < max_ports; i++) {
		struct ata_port *ap;

		ap = ata_port_alloc(host);
		if (!ap)
			goto err_out;

		ap->port_no = i;
		host->ports[i] = ap;
	}

	devres_remove_group(dev, NULL);
	return host;

 err_out:
	devres_release_group(dev, NULL);
	return NULL;
}

/**
 *	ata_host_alloc_pinfo - alloc host and init with port_info array
 *	@dev: generic device this host is associated with
 *	@ppi: array of ATA port_info to initialize host with
 *	@n_ports: number of ATA ports attached to this host
 *
 *	Allocate ATA host and initialize with info from @ppi.  If NULL
 *	terminated, @ppi may contain fewer entries than @n_ports.  The
 *	last entry will be used for the remaining ports.
 *
 *	RETURNS:
 *	Allocate ATA host on success, NULL on failure.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 */
struct ata_host *ata_host_alloc_pinfo(struct device *dev,
				      const struct ata_port_info * const * ppi,
				      int n_ports)
{
	const struct ata_port_info *pi;
	struct ata_host *host;
	int i, j;

	host = ata_host_alloc(dev, n_ports);
	if (!host)
		return NULL;

	for (i = 0, j = 0, pi = NULL; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		if (ppi[j])
			pi = ppi[j++];

		ap->pio_mask = pi->pio_mask;
		ap->mwdma_mask = pi->mwdma_mask;
		ap->udma_mask = pi->udma_mask;
		ap->flags |= pi->flags;
		ap->link.flags |= pi->link_flags;
		ap->ops = pi->port_ops;

		if (!host->ops && (pi->port_ops != &ata_dummy_port_ops))
			host->ops = pi->port_ops;
	}

	return host;
}

/**
 *	ata_slave_link_init - initialize slave link
 *	@ap: port to initialize slave link for
 *
 *	Create and initialize slave link for @ap.  This enables slave
 *	link handling on the port.
 *
 *	In libata, a port contains links and a link contains devices.
 *	There is single host link but if a PMP is attached to it,
 *	there can be multiple fan-out links.  On SATA, there's usually
 *	a single device connected to a link but PATA and SATA
 *	controllers emulating TF based interface can have two - master
 *	and slave.
 *
 *	However, there are a few controllers which don't fit into this
 *	abstraction too well - SATA controllers which emulate TF
 *	interface with both master and slave devices but also have
 *	separate SCR register sets for each device.  These controllers
 *	need separate links for physical link handling
 *	(e.g. onlineness, link speed) but should be treated like a
 *	traditional M/S controller for everything else (e.g. command
 *	issue, softreset).
 *
 *	slave_link is libata's way of handling this class of
 *	controllers without impacting core layer too much.  For
 *	anything other than physical link handling, the default host
 *	link is used for both master and slave.  For physical link
 *	handling, separate @ap->slave_link is used.  All dirty details
 *	are implemented inside libata core layer.  From LLD's POV, the
 *	only difference is that prereset, hardreset and postreset are
 *	called once more for the slave link, so the reset sequence
 *	looks like the following.
 *
 *	prereset(M) -> prereset(S) -> hardreset(M) -> hardreset(S) ->
 *	softreset(M) -> postreset(M) -> postreset(S)
 *
 *	Note that softreset is called only for the master.  Softreset
 *	resets both M/S by definition, so SRST on master should handle
 *	both (the standard method will work just fine).
 *
 *	LOCKING:
 *	Should be called before host is registered.
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int ata_slave_link_init(struct ata_port *ap)
{
	struct ata_link *link;

	WARN_ON(ap->slave_link);
	WARN_ON(ap->flags & ATA_FLAG_PMP);

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	ata_link_init(ap, link, 1);
	ap->slave_link = link;
	return 0;
}

static void ata_host_stop(struct device *gendev, void *res)
{
	struct ata_host *host = dev_get_drvdata(gendev);
	int i;

	WARN_ON(!(host->flags & ATA_HOST_STARTED));

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		if (ap->ops->port_stop)
			ap->ops->port_stop(ap);
	}

	if (host->ops->host_stop)
		host->ops->host_stop(host);
}

/**
 *	ata_finalize_port_ops - finalize ata_port_operations
 *	@ops: ata_port_operations to finalize
 *
 *	An ata_port_operations can inherit from another ops and that
 *	ops can again inherit from another.  This can go on as many
 *	times as necessary as long as there is no loop in the
 *	inheritance chain.
 *
 *	Ops tables are finalized when the host is started.  NULL or
 *	unspecified entries are inherited from the closet ancestor
 *	which has the method and the entry is populated with it.
 *	After finalization, the ops table directly points to all the
 *	methods and ->inherits is no longer necessary and cleared.
 *
 *	Using ATA_OP_NULL, inheriting ops can force a method to NULL.
 *
 *	LOCKING:
 *	None.
 */
static void ata_finalize_port_ops(struct ata_port_operations *ops)
{
	static DEFINE_SPINLOCK(lock);
	const struct ata_port_operations *cur;
	void **begin = (void **)ops;
	void **end = (void **)&ops->inherits;
	void **pp;

	if (!ops || !ops->inherits)
		return;

	spin_lock(&lock);

	for (cur = ops->inherits; cur; cur = cur->inherits) {
		void **inherit = (void **)cur;

		for (pp = begin; pp < end; pp++, inherit++)
			if (!*pp)
				*pp = *inherit;
	}

	for (pp = begin; pp < end; pp++)
		if (IS_ERR(*pp))
			*pp = NULL;

	ops->inherits = NULL;

	spin_unlock(&lock);
}

/**
 *	ata_host_start - start and freeze ports of an ATA host
 *	@host: ATA host to start ports for
 *
 *	Start and then freeze ports of @host.  Started status is
 *	recorded in host->flags, so this function can be called
 *	multiple times.  Ports are guaranteed to get started only
 *	once.  If host->ops isn't initialized yet, its set to the
 *	first non-dummy port ops.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 if all ports are started successfully, -errno otherwise.
 */
int ata_host_start(struct ata_host *host)
{
	int have_stop = 0;
	void *start_dr = NULL;
	int i, rc;

	if (host->flags & ATA_HOST_STARTED)
		return 0;

	ata_finalize_port_ops(host->ops);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		ata_finalize_port_ops(ap->ops);

		if (!host->ops && !ata_port_is_dummy(ap))
			host->ops = ap->ops;

		if (ap->ops->port_stop)
			have_stop = 1;
	}

	if (host->ops->host_stop)
		have_stop = 1;

	if (have_stop) {
		start_dr = devres_alloc(ata_host_stop, 0, GFP_KERNEL);
		if (!start_dr)
			return -ENOMEM;
	}

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		if (ap->ops->port_start) {
			rc = ap->ops->port_start(ap);
			if (rc) {
				if (rc != -ENODEV)
					dev_printk(KERN_ERR, host->dev,
						"failed to start port %d "
						"(errno=%d)\n", i, rc);
				goto err_out;
			}
		}
		ata_eh_freeze_port(ap);
	}

	if (start_dr)
		devres_add(host->dev, start_dr);
	host->flags |= ATA_HOST_STARTED;
	return 0;

 err_out:
	while (--i >= 0) {
		struct ata_port *ap = host->ports[i];

		if (ap->ops->port_stop)
			ap->ops->port_stop(ap);
	}
	devres_free(start_dr);
	return rc;
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
/* KILLME - the only user left is ipr */
void ata_host_init(struct ata_host *host, struct device *dev,
		   unsigned long flags, struct ata_port_operations *ops)
{
	spin_lock_init(&host->lock);
	host->dev = dev;
	host->flags = flags;
	host->ops = ops;
}

/**
 *	ata_host_register - register initialized ATA host
 *	@host: ATA host to register
 *	@sht: template for SCSI host
 *
 *	Register initialized ATA host.  @host is allocated using
 *	ata_host_alloc() and fully initialized by LLD.  This function
 *	starts ports, registers @host with ATA and SCSI layers and
 *	probe registered devices.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_host_register(struct ata_host *host, struct scsi_host_template *sht)
{
	int i, rc;

	/* host must have been started */
	if (!(host->flags & ATA_HOST_STARTED)) {
		dev_printk(KERN_ERR, host->dev,
			   "BUG: trying to register unstarted host\n");
		WARN_ON(1);
		return -EINVAL;
	}

	/* Blow away unused ports.  This happens when LLD can't
	 * determine the exact number of ports to allocate at
	 * allocation time.
	 */
	for (i = host->n_ports; host->ports[i]; i++)
		kfree(host->ports[i]);

	/* give ports names and add SCSI hosts */
	for (i = 0; i < host->n_ports; i++)
		host->ports[i]->print_id = ata_print_id++;

	rc = ata_scsi_add_hosts(host, sht);
	if (rc)
		return rc;

	/* associate with ACPI nodes */
	ata_acpi_associate(host);

	/* set cable, sata_spd_limit and report */
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		unsigned long xfer_mask;

		/* set SATA cable type if still unset */
		if (ap->cbl == ATA_CBL_NONE && (ap->flags & ATA_FLAG_SATA))
			ap->cbl = ATA_CBL_SATA;

		/* init sata_spd_limit to the current value */
		sata_link_init_spd(&ap->link);
		if (ap->slave_link)
			sata_link_init_spd(ap->slave_link);

		/* print per-port info to dmesg */
		xfer_mask = ata_pack_xfermask(ap->pio_mask, ap->mwdma_mask,
					      ap->udma_mask);

		if (!ata_port_is_dummy(ap)) {
			ata_port_printk(ap, KERN_INFO,
					"%cATA max %s %s\n",
					(ap->flags & ATA_FLAG_SATA) ? 'S' : 'P',
					ata_mode_string(xfer_mask),
					ap->link.eh_info.desc);
			ata_ehi_clear_desc(&ap->link.eh_info);
		} else
			ata_port_printk(ap, KERN_INFO, "DUMMY\n");
	}

	/* perform each probe synchronously */
	DPRINTK("probe begin\n");
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		/* probe */
		if (ap->ops->error_handler) {
			struct ata_eh_info *ehi = &ap->link.eh_info;
			unsigned long flags;

			ata_port_probe(ap);

			/* kick EH for boot probing */
			spin_lock_irqsave(ap->lock, flags);

			ehi->probe_mask |= ATA_ALL_DEVICES;
			ehi->action |= ATA_EH_RESET | ATA_EH_LPM;
			ehi->flags |= ATA_EHI_NO_AUTOPSY | ATA_EHI_QUIET;

			ap->pflags &= ~ATA_PFLAG_INITIALIZING;
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

		ata_scsi_scan_host(ap, 1);
	}

	return 0;
}

/**
 *	ata_host_activate - start host, request IRQ and register it
 *	@host: target ATA host
 *	@irq: IRQ to request
 *	@irq_handler: irq_handler used when requesting IRQ
 *	@irq_flags: irq_flags used when requesting IRQ
 *	@sht: scsi_host_template to use when registering the host
 *
 *	After allocating an ATA host and initializing it, most libata
 *	LLDs perform three steps to activate the host - start host,
 *	request IRQ and register it.  This helper takes necessasry
 *	arguments and performs the three steps in one go.
 *
 *	An invalid IRQ skips the IRQ registration and expects the host to
 *	have set polling mode on the port. In this case, @irq_handler
 *	should be NULL.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_host_activate(struct ata_host *host, int irq,
		      irq_handler_t irq_handler, unsigned long irq_flags,
		      struct scsi_host_template *sht)
{
	int i, rc;

	rc = ata_host_start(host);
	if (rc)
		return rc;

	/* Special case for polling mode */
	if (!irq) {
		WARN_ON(irq_handler);
		return ata_host_register(host, sht);
	}

	rc = devm_request_irq(host->dev, irq, irq_handler, irq_flags,
			      dev_driver_string(host->dev), host);
	if (rc)
		return rc;

	for (i = 0; i < host->n_ports; i++)
		ata_port_desc(host->ports[i], "irq %d", irq);

	rc = ata_host_register(host, sht);
	/* if failed, just free the IRQ and leave ports alone */
	if (rc)
		devm_free_irq(host->dev, irq, host);

	return rc;
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
static void ata_port_detach(struct ata_port *ap)
{
	unsigned long flags;
	struct ata_link *link;
	struct ata_device *dev;

	if (!ap->ops->error_handler)
		goto skip_eh;

	/* tell EH we're leaving & flush EH */
	spin_lock_irqsave(ap->lock, flags);
	ap->pflags |= ATA_PFLAG_UNLOADING;
	spin_unlock_irqrestore(ap->lock, flags);

	ata_port_wait_eh(ap);

	/* EH is now guaranteed to see UNLOADING - EH context belongs
	 * to us.  Restore SControl and disable all existing devices.
	 */
	__ata_port_for_each_link(link, ap) {
		sata_scr_write(link, SCR_CONTROL, link->saved_scontrol);
		ata_link_for_each_dev(dev, link)
			ata_dev_disable(dev);
	}

	/* Final freeze & EH.  All in-flight commands are aborted.  EH
	 * will be skipped and retrials will be terminated with bad
	 * target.
	 */
	spin_lock_irqsave(ap->lock, flags);
	ata_port_freeze(ap);	/* won't be thawed */
	spin_unlock_irqrestore(ap->lock, flags);

	ata_port_wait_eh(ap);
	cancel_rearming_delayed_work(&ap->hotplug_task);

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

	/* the host is dead now, dissociate ACPI */
	ata_acpi_dissociate(host);
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
	struct device *dev = &pdev->dev;
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

	if (mesg.event & PM_EVENT_SLEEP)
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

static int __init ata_parse_force_one(char **cur,
				      struct ata_force_ent *force_ent,
				      const char **reason)
{
	/* FIXME: Currently, there's no way to tag init const data and
	 * using __initdata causes build failure on some versions of
	 * gcc.  Once __initdataconst is implemented, add const to the
	 * following structure.
	 */
	static struct ata_force_param force_tbl[] __initdata = {
		{ "40c",	.cbl		= ATA_CBL_PATA40 },
		{ "80c",	.cbl		= ATA_CBL_PATA80 },
		{ "short40c",	.cbl		= ATA_CBL_PATA40_SHORT },
		{ "unk",	.cbl		= ATA_CBL_PATA_UNK },
		{ "ign",	.cbl		= ATA_CBL_PATA_IGN },
		{ "sata",	.cbl		= ATA_CBL_SATA },
		{ "1.5Gbps",	.spd_limit	= 1 },
		{ "3.0Gbps",	.spd_limit	= 2 },
		{ "noncq",	.horkage_on	= ATA_HORKAGE_NONCQ },
		{ "ncq",	.horkage_off	= ATA_HORKAGE_NONCQ },
		{ "pio0",	.xfer_mask	= 1 << (ATA_SHIFT_PIO + 0) },
		{ "pio1",	.xfer_mask	= 1 << (ATA_SHIFT_PIO + 1) },
		{ "pio2",	.xfer_mask	= 1 << (ATA_SHIFT_PIO + 2) },
		{ "pio3",	.xfer_mask	= 1 << (ATA_SHIFT_PIO + 3) },
		{ "pio4",	.xfer_mask	= 1 << (ATA_SHIFT_PIO + 4) },
		{ "pio5",	.xfer_mask	= 1 << (ATA_SHIFT_PIO + 5) },
		{ "pio6",	.xfer_mask	= 1 << (ATA_SHIFT_PIO + 6) },
		{ "mwdma0",	.xfer_mask	= 1 << (ATA_SHIFT_MWDMA + 0) },
		{ "mwdma1",	.xfer_mask	= 1 << (ATA_SHIFT_MWDMA + 1) },
		{ "mwdma2",	.xfer_mask	= 1 << (ATA_SHIFT_MWDMA + 2) },
		{ "mwdma3",	.xfer_mask	= 1 << (ATA_SHIFT_MWDMA + 3) },
		{ "mwdma4",	.xfer_mask	= 1 << (ATA_SHIFT_MWDMA + 4) },
		{ "udma0",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 0) },
		{ "udma16",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 0) },
		{ "udma/16",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 0) },
		{ "udma1",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 1) },
		{ "udma25",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 1) },
		{ "udma/25",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 1) },
		{ "udma2",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 2) },
		{ "udma33",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 2) },
		{ "udma/33",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 2) },
		{ "udma3",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 3) },
		{ "udma44",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 3) },
		{ "udma/44",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 3) },
		{ "udma4",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 4) },
		{ "udma66",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 4) },
		{ "udma/66",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 4) },
		{ "udma5",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 5) },
		{ "udma100",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 5) },
		{ "udma/100",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 5) },
		{ "udma6",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 6) },
		{ "udma133",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 6) },
		{ "udma/133",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 6) },
		{ "udma7",	.xfer_mask	= 1 << (ATA_SHIFT_UDMA + 7) },
		{ "nohrst",	.lflags		= ATA_LFLAG_NO_HRST },
		{ "nosrst",	.lflags		= ATA_LFLAG_NO_SRST },
		{ "norst",	.lflags		= ATA_LFLAG_NO_HRST | ATA_LFLAG_NO_SRST },
	};
	char *start = *cur, *p = *cur;
	char *id, *val, *endp;
	const struct ata_force_param *match_fp = NULL;
	int nr_matches = 0, i;

	/* find where this param ends and update *cur */
	while (*p != '\0' && *p != ',')
		p++;

	if (*p == '\0')
		*cur = p;
	else
		*cur = p + 1;

	*p = '\0';

	/* parse */
	p = strchr(start, ':');
	if (!p) {
		val = strstrip(start);
		goto parse_val;
	}
	*p = '\0';

	id = strstrip(start);
	val = strstrip(p + 1);

	/* parse id */
	p = strchr(id, '.');
	if (p) {
		*p++ = '\0';
		force_ent->device = simple_strtoul(p, &endp, 10);
		if (p == endp || *endp != '\0') {
			*reason = "invalid device";
			return -EINVAL;
		}
	}

	force_ent->port = simple_strtoul(id, &endp, 10);
	if (p == endp || *endp != '\0') {
		*reason = "invalid port/link";
		return -EINVAL;
	}

 parse_val:
	/* parse val, allow shortcuts so that both 1.5 and 1.5Gbps work */
	for (i = 0; i < ARRAY_SIZE(force_tbl); i++) {
		const struct ata_force_param *fp = &force_tbl[i];

		if (strncasecmp(val, fp->name, strlen(val)))
			continue;

		nr_matches++;
		match_fp = fp;

		if (strcasecmp(val, fp->name) == 0) {
			nr_matches = 1;
			break;
		}
	}

	if (!nr_matches) {
		*reason = "unknown value";
		return -EINVAL;
	}
	if (nr_matches > 1) {
		*reason = "ambigious value";
		return -EINVAL;
	}

	force_ent->param = *match_fp;

	return 0;
}

static void __init ata_parse_force_param(void)
{
	int idx = 0, size = 1;
	int last_port = -1, last_device = -1;
	char *p, *cur, *next;

	/* calculate maximum number of params and allocate force_tbl */
	for (p = ata_force_param_buf; *p; p++)
		if (*p == ',')
			size++;

	ata_force_tbl = kzalloc(sizeof(ata_force_tbl[0]) * size, GFP_KERNEL);
	if (!ata_force_tbl) {
		printk(KERN_WARNING "ata: failed to extend force table, "
		       "libata.force ignored\n");
		return;
	}

	/* parse and populate the table */
	for (cur = ata_force_param_buf; *cur != '\0'; cur = next) {
		const char *reason = "";
		struct ata_force_ent te = { .port = -1, .device = -1 };

		next = cur;
		if (ata_parse_force_one(&next, &te, &reason)) {
			printk(KERN_WARNING "ata: failed to parse force "
			       "parameter \"%s\" (%s)\n",
			       cur, reason);
			continue;
		}

		if (te.port == -1) {
			te.port = last_port;
			te.device = last_device;
		}

		ata_force_tbl[idx++] = te;

		last_port = te.port;
		last_device = te.device;
	}

	ata_force_tbl_size = idx;
}

static int __init ata_init(void)
{
	ata_parse_force_param();

	ata_wq = create_workqueue("ata");
	if (!ata_wq)
		goto free_force_tbl;

	ata_aux_wq = create_singlethread_workqueue("ata_aux");
	if (!ata_aux_wq)
		goto free_wq;

	printk(KERN_DEBUG "libata version " DRV_VERSION " loaded.\n");
	return 0;

free_wq:
	destroy_workqueue(ata_wq);
free_force_tbl:
	kfree(ata_force_tbl);
	return -ENOMEM;
}

static void __exit ata_exit(void)
{
	kfree(ata_force_tbl);
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
 *	@interval: polling interval in milliseconds
 *	@timeout: timeout in milliseconds
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
		      unsigned long interval, unsigned long timeout)
{
	unsigned long deadline;
	u32 tmp;

	tmp = ioread32(reg);

	/* Calculate timeout _after_ the first read to make sure
	 * preceding writes reach the controller before starting to
	 * eat away the timeout.
	 */
	deadline = ata_deadline(jiffies, timeout);

	while ((tmp & mask) == val && time_before(jiffies, deadline)) {
		msleep(interval);
		tmp = ioread32(reg);
	}

	return tmp;
}

/*
 * Dummy port_ops
 */
static unsigned int ata_dummy_qc_issue(struct ata_queued_cmd *qc)
{
	return AC_ERR_SYSTEM;
}

static void ata_dummy_error_handler(struct ata_port *ap)
{
	/* truly dummy */
}

struct ata_port_operations ata_dummy_port_ops = {
	.qc_prep		= ata_noop_qc_prep,
	.qc_issue		= ata_dummy_qc_issue,
	.error_handler		= ata_dummy_error_handler,
};

const struct ata_port_info ata_dummy_port_info = {
	.port_ops		= &ata_dummy_port_ops,
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
EXPORT_SYMBOL_GPL(ata_base_port_ops);
EXPORT_SYMBOL_GPL(sata_port_ops);
EXPORT_SYMBOL_GPL(ata_dummy_port_ops);
EXPORT_SYMBOL_GPL(ata_dummy_port_info);
EXPORT_SYMBOL_GPL(__ata_port_next_link);
EXPORT_SYMBOL_GPL(ata_std_bios_param);
EXPORT_SYMBOL_GPL(ata_host_init);
EXPORT_SYMBOL_GPL(ata_host_alloc);
EXPORT_SYMBOL_GPL(ata_host_alloc_pinfo);
EXPORT_SYMBOL_GPL(ata_slave_link_init);
EXPORT_SYMBOL_GPL(ata_host_start);
EXPORT_SYMBOL_GPL(ata_host_register);
EXPORT_SYMBOL_GPL(ata_host_activate);
EXPORT_SYMBOL_GPL(ata_host_detach);
EXPORT_SYMBOL_GPL(ata_sg_init);
EXPORT_SYMBOL_GPL(ata_qc_complete);
EXPORT_SYMBOL_GPL(ata_qc_complete_multiple);
EXPORT_SYMBOL_GPL(atapi_cmd_type);
EXPORT_SYMBOL_GPL(ata_tf_to_fis);
EXPORT_SYMBOL_GPL(ata_tf_from_fis);
EXPORT_SYMBOL_GPL(ata_pack_xfermask);
EXPORT_SYMBOL_GPL(ata_unpack_xfermask);
EXPORT_SYMBOL_GPL(ata_xfer_mask2mode);
EXPORT_SYMBOL_GPL(ata_xfer_mode2mask);
EXPORT_SYMBOL_GPL(ata_xfer_mode2shift);
EXPORT_SYMBOL_GPL(ata_mode_string);
EXPORT_SYMBOL_GPL(ata_id_xfermask);
EXPORT_SYMBOL_GPL(ata_port_start);
EXPORT_SYMBOL_GPL(ata_do_set_mode);
EXPORT_SYMBOL_GPL(ata_std_qc_defer);
EXPORT_SYMBOL_GPL(ata_noop_qc_prep);
EXPORT_SYMBOL_GPL(ata_port_probe);
EXPORT_SYMBOL_GPL(ata_dev_disable);
EXPORT_SYMBOL_GPL(sata_set_spd);
EXPORT_SYMBOL_GPL(ata_wait_after_reset);
EXPORT_SYMBOL_GPL(sata_link_debounce);
EXPORT_SYMBOL_GPL(sata_link_resume);
EXPORT_SYMBOL_GPL(ata_std_prereset);
EXPORT_SYMBOL_GPL(sata_link_hardreset);
EXPORT_SYMBOL_GPL(sata_std_hardreset);
EXPORT_SYMBOL_GPL(ata_std_postreset);
EXPORT_SYMBOL_GPL(ata_dev_classify);
EXPORT_SYMBOL_GPL(ata_dev_pair);
EXPORT_SYMBOL_GPL(ata_port_disable);
EXPORT_SYMBOL_GPL(ata_ratelimit);
EXPORT_SYMBOL_GPL(ata_wait_register);
EXPORT_SYMBOL_GPL(ata_scsi_ioctl);
EXPORT_SYMBOL_GPL(ata_scsi_queuecmd);
EXPORT_SYMBOL_GPL(ata_scsi_slave_config);
EXPORT_SYMBOL_GPL(ata_scsi_slave_destroy);
EXPORT_SYMBOL_GPL(ata_scsi_change_queue_depth);
EXPORT_SYMBOL_GPL(sata_scr_valid);
EXPORT_SYMBOL_GPL(sata_scr_read);
EXPORT_SYMBOL_GPL(sata_scr_write);
EXPORT_SYMBOL_GPL(sata_scr_write_flush);
EXPORT_SYMBOL_GPL(ata_link_online);
EXPORT_SYMBOL_GPL(ata_link_offline);
#ifdef CONFIG_PM
EXPORT_SYMBOL_GPL(ata_host_suspend);
EXPORT_SYMBOL_GPL(ata_host_resume);
#endif /* CONFIG_PM */
EXPORT_SYMBOL_GPL(ata_id_string);
EXPORT_SYMBOL_GPL(ata_id_c_string);
EXPORT_SYMBOL_GPL(ata_do_dev_read_id);
EXPORT_SYMBOL_GPL(ata_scsi_simulate);

EXPORT_SYMBOL_GPL(ata_pio_need_iordy);
EXPORT_SYMBOL_GPL(ata_timing_find_mode);
EXPORT_SYMBOL_GPL(ata_timing_compute);
EXPORT_SYMBOL_GPL(ata_timing_merge);
EXPORT_SYMBOL_GPL(ata_timing_cycle2mode);

#ifdef CONFIG_PCI
EXPORT_SYMBOL_GPL(pci_test_config_bits);
EXPORT_SYMBOL_GPL(ata_pci_remove_one);
#ifdef CONFIG_PM
EXPORT_SYMBOL_GPL(ata_pci_device_do_suspend);
EXPORT_SYMBOL_GPL(ata_pci_device_do_resume);
EXPORT_SYMBOL_GPL(ata_pci_device_suspend);
EXPORT_SYMBOL_GPL(ata_pci_device_resume);
#endif /* CONFIG_PM */
#endif /* CONFIG_PCI */

EXPORT_SYMBOL_GPL(__ata_ehi_push_desc);
EXPORT_SYMBOL_GPL(ata_ehi_push_desc);
EXPORT_SYMBOL_GPL(ata_ehi_clear_desc);
EXPORT_SYMBOL_GPL(ata_port_desc);
#ifdef CONFIG_PCI
EXPORT_SYMBOL_GPL(ata_port_pbar_desc);
#endif /* CONFIG_PCI */
EXPORT_SYMBOL_GPL(ata_port_schedule_eh);
EXPORT_SYMBOL_GPL(ata_link_abort);
EXPORT_SYMBOL_GPL(ata_port_abort);
EXPORT_SYMBOL_GPL(ata_port_freeze);
EXPORT_SYMBOL_GPL(sata_async_notification);
EXPORT_SYMBOL_GPL(ata_eh_freeze_port);
EXPORT_SYMBOL_GPL(ata_eh_thaw_port);
EXPORT_SYMBOL_GPL(ata_eh_qc_complete);
EXPORT_SYMBOL_GPL(ata_eh_qc_retry);
EXPORT_SYMBOL_GPL(ata_eh_analyze_ncq_error);
EXPORT_SYMBOL_GPL(ata_do_eh);
EXPORT_SYMBOL_GPL(ata_std_error_handler);

EXPORT_SYMBOL_GPL(ata_cable_40wire);
EXPORT_SYMBOL_GPL(ata_cable_80wire);
EXPORT_SYMBOL_GPL(ata_cable_unknown);
EXPORT_SYMBOL_GPL(ata_cable_ignore);
EXPORT_SYMBOL_GPL(ata_cable_sata);
