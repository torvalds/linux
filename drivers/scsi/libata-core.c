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

#include <linux/config.h>
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
#include "scsi_priv.h"
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <asm/io.h>
#include <asm/semaphore.h>
#include <asm/byteorder.h>

#include "libata.h"

static unsigned int ata_busy_sleep (struct ata_port *ap,
				    unsigned long tmout_pat,
			    	    unsigned long tmout);
static void ata_dev_reread_id(struct ata_port *ap, struct ata_device *dev);
static void ata_dev_init_params(struct ata_port *ap, struct ata_device *dev);
static void ata_set_mode(struct ata_port *ap);
static void ata_dev_set_xfermode(struct ata_port *ap, struct ata_device *dev);
static unsigned int ata_get_mode_mask(const struct ata_port *ap, int shift);
static int fgb(u32 bitmap);
static int ata_choose_xfer_mode(const struct ata_port *ap,
				u8 *xfer_mode_out,
				unsigned int *xfer_shift_out);
static void __ata_qc_complete(struct ata_queued_cmd *qc);
static void ata_pio_error(struct ata_port *ap);

static unsigned int ata_unique_id = 1;
static struct workqueue_struct *ata_wq;

int atapi_enabled = 0;
module_param(atapi_enabled, int, 0444);
MODULE_PARM_DESC(atapi_enabled, "Enable discovery of ATAPI devices (0=off, 1=on)");

MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("Library module for ATA devices");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/**
 *	ata_tf_load_pio - send taskfile registers to host controller
 *	@ap: Port to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_tf_load_pio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		outb(tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		outb(tf->hob_feature, ioaddr->feature_addr);
		outb(tf->hob_nsect, ioaddr->nsect_addr);
		outb(tf->hob_lbal, ioaddr->lbal_addr);
		outb(tf->hob_lbam, ioaddr->lbam_addr);
		outb(tf->hob_lbah, ioaddr->lbah_addr);
		VPRINTK("hob: feat 0x%X nsect 0x%X, lba 0x%X 0x%X 0x%X\n",
			tf->hob_feature,
			tf->hob_nsect,
			tf->hob_lbal,
			tf->hob_lbam,
			tf->hob_lbah);
	}

	if (is_addr) {
		outb(tf->feature, ioaddr->feature_addr);
		outb(tf->nsect, ioaddr->nsect_addr);
		outb(tf->lbal, ioaddr->lbal_addr);
		outb(tf->lbam, ioaddr->lbam_addr);
		outb(tf->lbah, ioaddr->lbah_addr);
		VPRINTK("feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		outb(tf->device, ioaddr->device_addr);
		VPRINTK("device 0x%X\n", tf->device);
	}

	ata_wait_idle(ap);
}

/**
 *	ata_tf_load_mmio - send taskfile registers to host controller
 *	@ap: Port to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller using MMIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_tf_load_mmio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		writeb(tf->ctl, (void __iomem *) ap->ioaddr.ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		writeb(tf->hob_feature, (void __iomem *) ioaddr->feature_addr);
		writeb(tf->hob_nsect, (void __iomem *) ioaddr->nsect_addr);
		writeb(tf->hob_lbal, (void __iomem *) ioaddr->lbal_addr);
		writeb(tf->hob_lbam, (void __iomem *) ioaddr->lbam_addr);
		writeb(tf->hob_lbah, (void __iomem *) ioaddr->lbah_addr);
		VPRINTK("hob: feat 0x%X nsect 0x%X, lba 0x%X 0x%X 0x%X\n",
			tf->hob_feature,
			tf->hob_nsect,
			tf->hob_lbal,
			tf->hob_lbam,
			tf->hob_lbah);
	}

	if (is_addr) {
		writeb(tf->feature, (void __iomem *) ioaddr->feature_addr);
		writeb(tf->nsect, (void __iomem *) ioaddr->nsect_addr);
		writeb(tf->lbal, (void __iomem *) ioaddr->lbal_addr);
		writeb(tf->lbam, (void __iomem *) ioaddr->lbam_addr);
		writeb(tf->lbah, (void __iomem *) ioaddr->lbah_addr);
		VPRINTK("feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		writeb(tf->device, (void __iomem *) ioaddr->device_addr);
		VPRINTK("device 0x%X\n", tf->device);
	}

	ata_wait_idle(ap);
}


/**
 *	ata_tf_load - send taskfile registers to host controller
 *	@ap: Port to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller using MMIO
 *	or PIO as indicated by the ATA_FLAG_MMIO flag.
 *	Writes the control, feature, nsect, lbal, lbam, and lbah registers.
 *	Optionally (ATA_TFLAG_LBA48) writes hob_feature, hob_nsect,
 *	hob_lbal, hob_lbam, and hob_lbah.
 *
 *	This function waits for idle (!BUSY and !DRQ) after writing
 *	registers.  If the control register has a new value, this
 *	function also waits for idle after writing control and before
 *	writing the remaining registers.
 *
 *	May be used as the tf_load() entry in ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_tf_load(struct ata_port *ap, const struct ata_taskfile *tf)
{
	if (ap->flags & ATA_FLAG_MMIO)
		ata_tf_load_mmio(ap, tf);
	else
		ata_tf_load_pio(ap, tf);
}

/**
 *	ata_exec_command_pio - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues PIO write to ATA command register, with proper
 *	synchronization with interrupt handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_exec_command_pio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->id, tf->command);

       	outb(tf->command, ap->ioaddr.command_addr);
	ata_pause(ap);
}


/**
 *	ata_exec_command_mmio - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues MMIO write to ATA command register, with proper
 *	synchronization with interrupt handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_exec_command_mmio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->id, tf->command);

       	writeb(tf->command, (void __iomem *) ap->ioaddr.command_addr);
	ata_pause(ap);
}


/**
 *	ata_exec_command - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues PIO/MMIO write to ATA command register, with proper
 *	synchronization with interrupt handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
void ata_exec_command(struct ata_port *ap, const struct ata_taskfile *tf)
{
	if (ap->flags & ATA_FLAG_MMIO)
		ata_exec_command_mmio(ap, tf);
	else
		ata_exec_command_pio(ap, tf);
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
 *	spin_lock_irqsave(host_set lock)
 */

static inline void ata_tf_to_host(struct ata_port *ap,
				  const struct ata_taskfile *tf)
{
	ap->ops->tf_load(ap, tf);
	ap->ops->exec_command(ap, tf);
}

/**
 *	ata_tf_read_pio - input device's ATA taskfile shadow registers
 *	@ap: Port from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_tf_read_pio(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->command = ata_check_status(ap);
	tf->feature = inb(ioaddr->error_addr);
	tf->nsect = inb(ioaddr->nsect_addr);
	tf->lbal = inb(ioaddr->lbal_addr);
	tf->lbam = inb(ioaddr->lbam_addr);
	tf->lbah = inb(ioaddr->lbah_addr);
	tf->device = inb(ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		outb(tf->ctl | ATA_HOB, ioaddr->ctl_addr);
		tf->hob_feature = inb(ioaddr->error_addr);
		tf->hob_nsect = inb(ioaddr->nsect_addr);
		tf->hob_lbal = inb(ioaddr->lbal_addr);
		tf->hob_lbam = inb(ioaddr->lbam_addr);
		tf->hob_lbah = inb(ioaddr->lbah_addr);
	}
}

/**
 *	ata_tf_read_mmio - input device's ATA taskfile shadow registers
 *	@ap: Port from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf via MMIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_tf_read_mmio(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->command = ata_check_status(ap);
	tf->feature = readb((void __iomem *)ioaddr->error_addr);
	tf->nsect = readb((void __iomem *)ioaddr->nsect_addr);
	tf->lbal = readb((void __iomem *)ioaddr->lbal_addr);
	tf->lbam = readb((void __iomem *)ioaddr->lbam_addr);
	tf->lbah = readb((void __iomem *)ioaddr->lbah_addr);
	tf->device = readb((void __iomem *)ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		writeb(tf->ctl | ATA_HOB, (void __iomem *) ap->ioaddr.ctl_addr);
		tf->hob_feature = readb((void __iomem *)ioaddr->error_addr);
		tf->hob_nsect = readb((void __iomem *)ioaddr->nsect_addr);
		tf->hob_lbal = readb((void __iomem *)ioaddr->lbal_addr);
		tf->hob_lbam = readb((void __iomem *)ioaddr->lbam_addr);
		tf->hob_lbah = readb((void __iomem *)ioaddr->lbah_addr);
	}
}


/**
 *	ata_tf_read - input device's ATA taskfile shadow registers
 *	@ap: Port from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf.
 *
 *	Reads nsect, lbal, lbam, lbah, and device.  If ATA_TFLAG_LBA48
 *	is set, also reads the hob registers.
 *
 *	May be used as the tf_read() entry in ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	if (ap->flags & ATA_FLAG_MMIO)
		ata_tf_read_mmio(ap, tf);
	else
		ata_tf_read_pio(ap, tf);
}

/**
 *	ata_check_status_pio - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile status register for currently-selected device
 *	and return its value. This also clears pending interrupts
 *      from this device
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static u8 ata_check_status_pio(struct ata_port *ap)
{
	return inb(ap->ioaddr.status_addr);
}

/**
 *	ata_check_status_mmio - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile status register for currently-selected device
 *	via MMIO and return its value. This also clears pending interrupts
 *      from this device
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static u8 ata_check_status_mmio(struct ata_port *ap)
{
       	return readb((void __iomem *) ap->ioaddr.status_addr);
}


/**
 *	ata_check_status - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile status register for currently-selected device
 *	and return its value. This also clears pending interrupts
 *      from this device
 *
 *	May be used as the check_status() entry in ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
u8 ata_check_status(struct ata_port *ap)
{
	if (ap->flags & ATA_FLAG_MMIO)
		return ata_check_status_mmio(ap);
	return ata_check_status_pio(ap);
}


/**
 *	ata_altstatus - Read device alternate status reg
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile alternate status register for
 *	currently-selected device and return its value.
 *
 *	Note: may NOT be used as the check_altstatus() entry in
 *	ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
u8 ata_altstatus(struct ata_port *ap)
{
	if (ap->ops->check_altstatus)
		return ap->ops->check_altstatus(ap);

	if (ap->flags & ATA_FLAG_MMIO)
		return readb((void __iomem *)ap->ioaddr.altstatus_addr);
	return inb(ap->ioaddr.altstatus_addr);
}


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
 *	Converts a standard ATA taskfile to a Serial ATA
 *	FIS structure (Register - Host to Device).
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
	/* pio */
	ATA_CMD_PIO_READ,
	ATA_CMD_PIO_WRITE,
	ATA_CMD_PIO_READ_EXT,
	ATA_CMD_PIO_WRITE_EXT,
	/* dma */
	ATA_CMD_READ,
	ATA_CMD_WRITE,
	ATA_CMD_READ_EXT,
	ATA_CMD_WRITE_EXT
};

/**
 *	ata_rwcmd_protocol - set taskfile r/w commands and protocol
 *	@qc: command to examine and configure
 *
 *	Examine the device configuration and tf->flags to calculate 
 *	the proper read/write commands and protocol to use.
 *
 *	LOCKING:
 *	caller.
 */
void ata_rwcmd_protocol(struct ata_queued_cmd *qc)
{
	struct ata_taskfile *tf = &qc->tf;
	struct ata_device *dev = qc->dev;

	int index, lba48, write;
 
	lba48 = (tf->flags & ATA_TFLAG_LBA48) ? 2 : 0;
	write = (tf->flags & ATA_TFLAG_WRITE) ? 1 : 0;

	if (dev->flags & ATA_DFLAG_PIO) {
		tf->protocol = ATA_PROT_PIO;
		index = dev->multi_count ? 0 : 4;
	} else {
		tf->protocol = ATA_PROT_DMA;
		index = 8;
	}

	tf->command = ata_rw_cmds[index + lba48 + write];
}

static const char * xfer_mode_str[] = {
	"UDMA/16",
	"UDMA/25",
	"UDMA/33",
	"UDMA/44",
	"UDMA/66",
	"UDMA/100",
	"UDMA/133",
	"UDMA7",
	"MWDMA0",
	"MWDMA1",
	"MWDMA2",
	"PIO0",
	"PIO1",
	"PIO2",
	"PIO3",
	"PIO4",
};

/**
 *	ata_udma_string - convert UDMA bit offset to string
 *	@mask: mask of bits supported; only highest bit counts.
 *
 *	Determine string which represents the highest speed
 *	(highest bit in @udma_mask).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Constant C string representing highest speed listed in
 *	@udma_mask, or the constant C string "<n/a>".
 */

static const char *ata_mode_string(unsigned int mask)
{
	int i;

	for (i = 7; i >= 0; i--)
		if (mask & (1 << i))
			goto out;
	for (i = ATA_SHIFT_MWDMA + 2; i >= ATA_SHIFT_MWDMA; i--)
		if (mask & (1 << i))
			goto out;
	for (i = ATA_SHIFT_PIO + 4; i >= ATA_SHIFT_PIO; i--)
		if (mask & (1 << i))
			goto out;

	return "<n/a>";

out:
	return xfer_mode_str[i];
}

/**
 *	ata_pio_devchk - PATA device presence detection
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

static unsigned int ata_pio_devchk(struct ata_port *ap,
				   unsigned int device)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 nsect, lbal;

	ap->ops->dev_select(ap, device);

	outb(0x55, ioaddr->nsect_addr);
	outb(0xaa, ioaddr->lbal_addr);

	outb(0xaa, ioaddr->nsect_addr);
	outb(0x55, ioaddr->lbal_addr);

	outb(0x55, ioaddr->nsect_addr);
	outb(0xaa, ioaddr->lbal_addr);

	nsect = inb(ioaddr->nsect_addr);
	lbal = inb(ioaddr->lbal_addr);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return 1;	/* we found a device */

	return 0;		/* nothing found */
}

/**
 *	ata_mmio_devchk - PATA device presence detection
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

static unsigned int ata_mmio_devchk(struct ata_port *ap,
				    unsigned int device)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 nsect, lbal;

	ap->ops->dev_select(ap, device);

	writeb(0x55, (void __iomem *) ioaddr->nsect_addr);
	writeb(0xaa, (void __iomem *) ioaddr->lbal_addr);

	writeb(0xaa, (void __iomem *) ioaddr->nsect_addr);
	writeb(0x55, (void __iomem *) ioaddr->lbal_addr);

	writeb(0x55, (void __iomem *) ioaddr->nsect_addr);
	writeb(0xaa, (void __iomem *) ioaddr->lbal_addr);

	nsect = readb((void __iomem *) ioaddr->nsect_addr);
	lbal = readb((void __iomem *) ioaddr->lbal_addr);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return 1;	/* we found a device */

	return 0;		/* nothing found */
}

/**
 *	ata_devchk - PATA device presence detection
 *	@ap: ATA channel to examine
 *	@device: Device to examine (starting at zero)
 *
 *	Dispatch ATA device presence detection, depending
 *	on whether we are using PIO or MMIO to talk to the
 *	ATA shadow registers.
 *
 *	LOCKING:
 *	caller.
 */

static unsigned int ata_devchk(struct ata_port *ap,
				    unsigned int device)
{
	if (ap->flags & ATA_FLAG_MMIO)
		return ata_mmio_devchk(ap, device);
	return ata_pio_devchk(ap, device);
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
 */

static u8 ata_dev_try_classify(struct ata_port *ap, unsigned int device)
{
	struct ata_device *dev = &ap->device[device];
	struct ata_taskfile tf;
	unsigned int class;
	u8 err;

	ap->ops->dev_select(ap, device);

	memset(&tf, 0, sizeof(tf));

	ap->ops->tf_read(ap, &tf);
	err = tf.feature;

	dev->class = ATA_DEV_NONE;

	/* see if device passed diags */
	if (err == 1)
		/* do nothing */ ;
	else if ((device == 0) && (err == 0x81))
		/* do nothing */ ;
	else
		return err;

	/* determine if device if ATA or ATAPI */
	class = ata_dev_classify(&tf);
	if (class == ATA_DEV_UNKNOWN)
		return err;
	if ((class == ATA_DEV_ATA) && (ata_chk_status(ap) == 0))
		return err;

	dev->class = class;

	return err;
}

/**
 *	ata_dev_id_string - Convert IDENTIFY DEVICE page into string
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

void ata_dev_id_string(const u16 *id, unsigned char *s,
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

	if (ap->flags & ATA_FLAG_MMIO) {
		writeb(tmp, (void __iomem *) ap->ioaddr.device_addr);
	} else {
		outb(tmp, ap->ioaddr.device_addr);
	}
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
	VPRINTK("ENTER, ata%u: device %u, wait %u\n",
		ap->id, device, wait);

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
 *	@dev: Device whose IDENTIFY DEVICE page we will dump
 *
 *	Dump selected 16-bit words from a detected device's
 *	IDENTIFY PAGE page.
 *
 *	LOCKING:
 *	caller.
 */

static inline void ata_dump_id(const struct ata_device *dev)
{
	DPRINTK("49==0x%04x  "
		"53==0x%04x  "
		"63==0x%04x  "
		"64==0x%04x  "
		"75==0x%04x  \n",
		dev->id[49],
		dev->id[53],
		dev->id[63],
		dev->id[64],
		dev->id[75]);
	DPRINTK("80==0x%04x  "
		"81==0x%04x  "
		"82==0x%04x  "
		"83==0x%04x  "
		"84==0x%04x  \n",
		dev->id[80],
		dev->id[81],
		dev->id[82],
		dev->id[83],
		dev->id[84]);
	DPRINTK("88==0x%04x  "
		"93==0x%04x\n",
		dev->id[88],
		dev->id[93]);
}

/*
 *	Compute the PIO modes available for this device. This is not as
 *	trivial as it seems if we must consider early devices correctly.
 *
 *	FIXME: pre IDE drive timing (do we care ?). 
 */

static unsigned int ata_pio_modes(const struct ata_device *adev)
{
	u16 modes;

	/* Usual case. Word 53 indicates word 88 is valid */
	if (adev->id[ATA_ID_FIELD_VALID] & (1 << 2)) {
		modes = adev->id[ATA_ID_PIO_MODES] & 0x03;
		modes <<= 3;
		modes |= 0x7;
		return modes;
	}

	/* If word 88 isn't valid then Word 51 holds the PIO timing number
	   for the maximum. Turn it into a mask and return it */
	modes = (2 << (adev->id[ATA_ID_OLD_PIO_MODES] & 0xFF)) - 1 ;
	return modes;
}

/**
 *	ata_dev_identify - obtain IDENTIFY x DEVICE page
 *	@ap: port on which device we wish to probe resides
 *	@device: device bus address, starting at zero
 *
 *	Following bus reset, we issue the IDENTIFY [PACKET] DEVICE
 *	command, and read back the 512-byte device information page.
 *	The device information page is fed to us via the standard
 *	PIO-IN protocol, but we hand-code it here. (TODO: investigate
 *	using standard PIO-IN paths)
 *
 *	After reading the device information page, we use several
 *	bits of information from it to initialize data structures
 *	that will be used during the lifetime of the ata_device.
 *	Other data from the info page is used to disqualify certain
 *	older ATA devices we do not wish to support.
 *
 *	LOCKING:
 *	Inherited from caller.  Some functions called by this function
 *	obtain the host_set lock.
 */

static void ata_dev_identify(struct ata_port *ap, unsigned int device)
{
	struct ata_device *dev = &ap->device[device];
	unsigned int major_version;
	u16 tmp;
	unsigned long xfer_modes;
	unsigned int using_edd;
	DECLARE_COMPLETION(wait);
	struct ata_queued_cmd *qc;
	unsigned long flags;
	int rc;

	if (!ata_dev_present(dev)) {
		DPRINTK("ENTER/EXIT (host %u, dev %u) -- nodev\n",
			ap->id, device);
		return;
	}

	if (ap->flags & (ATA_FLAG_SRST | ATA_FLAG_SATA_RESET))
		using_edd = 0;
	else
		using_edd = 1;

	DPRINTK("ENTER, host %u, dev %u\n", ap->id, device);

	assert (dev->class == ATA_DEV_ATA || dev->class == ATA_DEV_ATAPI ||
		dev->class == ATA_DEV_NONE);

	ata_dev_select(ap, device, 1, 1); /* select device 0/1 */

	qc = ata_qc_new_init(ap, dev);
	BUG_ON(qc == NULL);

	ata_sg_init_one(qc, dev->id, sizeof(dev->id));
	qc->dma_dir = DMA_FROM_DEVICE;
	qc->tf.protocol = ATA_PROT_PIO;
	qc->nsect = 1;

retry:
	if (dev->class == ATA_DEV_ATA) {
		qc->tf.command = ATA_CMD_ID_ATA;
		DPRINTK("do ATA identify\n");
	} else {
		qc->tf.command = ATA_CMD_ID_ATAPI;
		DPRINTK("do ATAPI identify\n");
	}

	qc->waiting = &wait;
	qc->complete_fn = ata_qc_complete_noop;

	spin_lock_irqsave(&ap->host_set->lock, flags);
	rc = ata_qc_issue(qc);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	if (rc)
		goto err_out;
	else
		wait_for_completion(&wait);

	spin_lock_irqsave(&ap->host_set->lock, flags);
	ap->ops->tf_read(ap, &qc->tf);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	if (qc->tf.command & ATA_ERR) {
		/*
		 * arg!  EDD works for all test cases, but seems to return
		 * the ATA signature for some ATAPI devices.  Until the
		 * reason for this is found and fixed, we fix up the mess
		 * here.  If IDENTIFY DEVICE returns command aborted
		 * (as ATAPI devices do), then we issue an
		 * IDENTIFY PACKET DEVICE.
		 *
		 * ATA software reset (SRST, the default) does not appear
		 * to have this problem.
		 */
		if ((using_edd) && (dev->class == ATA_DEV_ATA)) {
			u8 err = qc->tf.feature;
			if (err & ATA_ABORTED) {
				dev->class = ATA_DEV_ATAPI;
				qc->cursg = 0;
				qc->cursg_ofs = 0;
				qc->cursect = 0;
				qc->nsect = 1;
				goto retry;
			}
		}
		goto err_out;
	}

	swap_buf_le16(dev->id, ATA_ID_WORDS);

	/* print device capabilities */
	printk(KERN_DEBUG "ata%u: dev %u cfg "
	       "49:%04x 82:%04x 83:%04x 84:%04x 85:%04x 86:%04x 87:%04x 88:%04x\n",
	       ap->id, device, dev->id[49],
	       dev->id[82], dev->id[83], dev->id[84],
	       dev->id[85], dev->id[86], dev->id[87],
	       dev->id[88]);

	/*
	 * common ATA, ATAPI feature tests
	 */

	/* we require DMA support (bits 8 of word 49) */
	if (!ata_id_has_dma(dev->id)) {
		printk(KERN_DEBUG "ata%u: no dma\n", ap->id);
		goto err_out_nosup;
	}

	/* quick-n-dirty find max transfer mode; for printk only */
	xfer_modes = dev->id[ATA_ID_UDMA_MODES];
	if (!xfer_modes)
		xfer_modes = (dev->id[ATA_ID_MWDMA_MODES]) << ATA_SHIFT_MWDMA;
	if (!xfer_modes)
		xfer_modes = ata_pio_modes(dev);

	ata_dump_id(dev);

	/* ATA-specific feature tests */
	if (dev->class == ATA_DEV_ATA) {
		if (!ata_id_is_ata(dev->id))	/* sanity check */
			goto err_out_nosup;

		/* get major version */
		tmp = dev->id[ATA_ID_MAJOR_VER];
		for (major_version = 14; major_version >= 1; major_version--)
			if (tmp & (1 << major_version))
				break;

		/*
		 * The exact sequence expected by certain pre-ATA4 drives is:
		 * SRST RESET
		 * IDENTIFY
		 * INITIALIZE DEVICE PARAMETERS
		 * anything else..
		 * Some drives were very specific about that exact sequence.
		 */
		if (major_version < 4 || (!ata_id_has_lba(dev->id))) {
			ata_dev_init_params(ap, dev);

			/* current CHS translation info (id[53-58]) might be
			 * changed. reread the identify device info.
			 */
			ata_dev_reread_id(ap, dev);
		}

		if (ata_id_has_lba(dev->id)) {
			dev->flags |= ATA_DFLAG_LBA;

			if (ata_id_has_lba48(dev->id)) {
				dev->flags |= ATA_DFLAG_LBA48;
				dev->n_sectors = ata_id_u64(dev->id, 100);
			} else {
				dev->n_sectors = ata_id_u32(dev->id, 60);
			}

			/* print device info to dmesg */
			printk(KERN_INFO "ata%u: dev %u ATA-%d, max %s, %Lu sectors:%s\n",
			       ap->id, device,
			       major_version,
			       ata_mode_string(xfer_modes),
			       (unsigned long long)dev->n_sectors,
			       dev->flags & ATA_DFLAG_LBA48 ? " LBA48" : " LBA");
		} else { 
			/* CHS */

			/* Default translation */
			dev->cylinders	= dev->id[1];
			dev->heads	= dev->id[3];
			dev->sectors	= dev->id[6];
			dev->n_sectors	= dev->cylinders * dev->heads * dev->sectors;

			if (ata_id_current_chs_valid(dev->id)) {
				/* Current CHS translation is valid. */
				dev->cylinders = dev->id[54];
				dev->heads     = dev->id[55];
				dev->sectors   = dev->id[56];
				
				dev->n_sectors = ata_id_u32(dev->id, 57);
			}

			/* print device info to dmesg */
			printk(KERN_INFO "ata%u: dev %u ATA-%d, max %s, %Lu sectors: CHS %d/%d/%d\n",
			       ap->id, device,
			       major_version,
			       ata_mode_string(xfer_modes),
			       (unsigned long long)dev->n_sectors,
			       (int)dev->cylinders, (int)dev->heads, (int)dev->sectors);

		}

		if (dev->id[59] & 0x100) {
			dev->multi_count = dev->id[59] & 0xff;
			DPRINTK("ata%u: dev %u multi count %u\n",
				ap->id, device, dev->multi_count);
		}

		ap->host->max_cmd_len = 16;
	}

	/* ATAPI-specific feature tests */
	else {
		if (ata_id_is_ata(dev->id))		/* sanity check */
			goto err_out_nosup;

		rc = atapi_cdb_len(dev->id);
		if ((rc < 12) || (rc > ATAPI_CDB_LEN)) {
			printk(KERN_WARNING "ata%u: unsupported CDB len\n", ap->id);
			goto err_out_nosup;
		}
		ap->cdb_len = (unsigned int) rc;
		ap->host->max_cmd_len = (unsigned char) ap->cdb_len;

		if (ata_id_cdb_intr(dev->id))
			dev->flags |= ATA_DFLAG_CDB_INTR;

		/* print device info to dmesg */
		printk(KERN_INFO "ata%u: dev %u ATAPI, max %s\n",
		       ap->id, device,
		       ata_mode_string(xfer_modes));
	}

	DPRINTK("EXIT, drv_stat = 0x%x\n", ata_chk_status(ap));
	return;

err_out_nosup:
	printk(KERN_WARNING "ata%u: dev %u not supported, ignoring\n",
	       ap->id, device);
err_out:
	dev->class++;	/* converts ATA_DEV_xxx into ATA_DEV_xxx_UNSUP */
	DPRINTK("EXIT, err\n");
}


static inline u8 ata_dev_knobble(const struct ata_port *ap)
{
	return ((ap->cbl == ATA_CBL_SATA) && (!ata_id_is_sata(ap->device->id)));
}

/**
 * 	ata_dev_config - Run device specific handlers and check for
 * 			 SATA->PATA bridges
 * 	@ap: Bus
 * 	@i:  Device
 *
 * 	LOCKING:
 */

void ata_dev_config(struct ata_port *ap, unsigned int i)
{
	/* limit bridge transfers to udma5, 200 sectors */
	if (ata_dev_knobble(ap)) {
		printk(KERN_INFO "ata%u(%u): applying bridge limits\n",
			ap->id, ap->device->devno);
		ap->udma_mask &= ATA_UDMA5;
		ap->host->max_sectors = ATA_MAX_SECTORS;
		ap->host->hostt->max_sectors = ATA_MAX_SECTORS;
		ap->device->flags |= ATA_DFLAG_LOCK_SECTORS;
	}

	if (ap->ops->dev_config)
		ap->ops->dev_config(ap, &ap->device[i]);
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
 *	Zero on success, non-zero on error.
 */

static int ata_bus_probe(struct ata_port *ap)
{
	unsigned int i, found = 0;

	ap->ops->phy_reset(ap);
	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		goto err_out;

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		ata_dev_identify(ap, i);
		if (ata_dev_present(&ap->device[i])) {
			found = 1;
			ata_dev_config(ap,i);
		}
	}

	if ((!found) || (ap->flags & ATA_FLAG_PORT_DISABLED))
		goto err_out_disable;

	ata_set_mode(ap);
	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		goto err_out_disable;

	return 0;

err_out_disable:
	ap->ops->port_disable(ap);
err_out:
	return -1;
}

/**
 *	ata_port_probe - Mark port as enabled
 *	@ap: Port for which we indicate enablement
 *
 *	Modify @ap data structure such that the system
 *	thinks that the entire port is enabled.
 *
 *	LOCKING: host_set lock, or some other form of
 *	serialization.
 */

void ata_port_probe(struct ata_port *ap)
{
	ap->flags &= ~ATA_FLAG_PORT_DISABLED;
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
		scr_write_flush(ap, SCR_CONTROL, 0x301);
		/* Couldn't find anything in SATA I/II specs, but
		 * AHCI-1.1 10.4.2 says at least 1 ms. */
		mdelay(1);
	}
	scr_write_flush(ap, SCR_CONTROL, 0x300); /* phy wake/clear reset */

	/* wait for phy to become ready, if necessary */
	do {
		msleep(200);
		sstatus = scr_read(ap, SCR_STATUS);
		if ((sstatus & 0xf) != 1)
			break;
	} while (time_before(jiffies, timeout));

	/* TODO: phy layer with polling, timeouts, etc. */
	if (sata_dev_present(ap))
		ata_port_probe(ap);
	else {
		sstatus = scr_read(ap, SCR_STATUS);
		printk(KERN_INFO "ata%u: no device found (phy stat %08x)\n",
		       ap->id, sstatus);
		ata_port_disable(ap);
	}

	if (ap->flags & ATA_FLAG_PORT_DISABLED)
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
	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		return;
	ata_bus_reset(ap);
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
 *	LOCKING: host_set lock, or some other form of
 *	serialization.
 */

void ata_port_disable(struct ata_port *ap)
{
	ap->device[0].class = ATA_DEV_NONE;
	ap->device[1].class = ATA_DEV_NONE;
	ap->flags |= ATA_FLAG_PORT_DISABLED;
}

/*
 * This mode timing computation functionality is ported over from
 * drivers/ide/ide-timing.h and was originally written by Vojtech Pavlik
 */
/*
 * PIO 0-5, MWDMA 0-2 and UDMA 0-6 timings (in nanoseconds).
 * These were taken from ATA/ATAPI-6 standard, rev 0a, except
 * for PIO 5, which is a nonstandard extension and UDMA6, which
 * is currently supported only by Maxtor drives. 
 */

static const struct ata_timing ata_timing[] = {

	{ XFER_UDMA_6,     0,   0,   0,   0,   0,   0,   0,  15 },
	{ XFER_UDMA_5,     0,   0,   0,   0,   0,   0,   0,  20 },
	{ XFER_UDMA_4,     0,   0,   0,   0,   0,   0,   0,  30 },
	{ XFER_UDMA_3,     0,   0,   0,   0,   0,   0,   0,  45 },

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

/*	{ XFER_PIO_5,     20,  50,  30, 100,  50,  30, 100,   0 }, */
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

	ata_timing_quantize(s, t, T, UT);

	/*
	 * Even in DMA/UDMA modes we still use PIO access for IDENTIFY, S.M.A.R.T
	 * and some other commands. We have to ensure that the DMA cycle timing is
	 * slower/equal than the fastest PIO timing.
	 */

	if (speed > XFER_PIO_4) {
		ata_timing_compute(adev, adev->pio_mode, &p, T, UT);
		ata_timing_merge(&p, t, t, ATA_TIMING_ALL);
	}

	/*
	 * Lenghten active & recovery time so that cycle time is correct.
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

static const struct {
	unsigned int shift;
	u8 base;
} xfer_mode_classes[] = {
	{ ATA_SHIFT_UDMA,	XFER_UDMA_0 },
	{ ATA_SHIFT_MWDMA,	XFER_MW_DMA_0 },
	{ ATA_SHIFT_PIO,	XFER_PIO_0 },
};

static inline u8 base_from_shift(unsigned int shift)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(xfer_mode_classes); i++)
		if (xfer_mode_classes[i].shift == shift)
			return xfer_mode_classes[i].base;

	return 0xff;
}

static void ata_dev_set_mode(struct ata_port *ap, struct ata_device *dev)
{
	int ofs, idx;
	u8 base;

	if (!ata_dev_present(dev) || (ap->flags & ATA_FLAG_PORT_DISABLED))
		return;

	if (dev->xfer_shift == ATA_SHIFT_PIO)
		dev->flags |= ATA_DFLAG_PIO;

	ata_dev_set_xfermode(ap, dev);

	base = base_from_shift(dev->xfer_shift);
	ofs = dev->xfer_mode - base;
	idx = ofs + dev->xfer_shift;
	WARN_ON(idx >= ARRAY_SIZE(xfer_mode_str));

	DPRINTK("idx=%d xfer_shift=%u, xfer_mode=0x%x, base=0x%x, offset=%d\n",
		idx, dev->xfer_shift, (int)dev->xfer_mode, (int)base, ofs);

	printk(KERN_INFO "ata%u: dev %u configured for %s\n",
		ap->id, dev->devno, xfer_mode_str[idx]);
}

static int ata_host_set_pio(struct ata_port *ap)
{
	unsigned int mask;
	int x, i;
	u8 base, xfer_mode;

	mask = ata_get_mode_mask(ap, ATA_SHIFT_PIO);
	x = fgb(mask);
	if (x < 0) {
		printk(KERN_WARNING "ata%u: no PIO support\n", ap->id);
		return -1;
	}

	base = base_from_shift(ATA_SHIFT_PIO);
	xfer_mode = base + x;

	DPRINTK("base 0x%x xfer_mode 0x%x mask 0x%x x %d\n",
		(int)base, (int)xfer_mode, mask, x);

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		struct ata_device *dev = &ap->device[i];
		if (ata_dev_present(dev)) {
			dev->pio_mode = xfer_mode;
			dev->xfer_mode = xfer_mode;
			dev->xfer_shift = ATA_SHIFT_PIO;
			if (ap->ops->set_piomode)
				ap->ops->set_piomode(ap, dev);
		}
	}

	return 0;
}

static void ata_host_set_dma(struct ata_port *ap, u8 xfer_mode,
			    unsigned int xfer_shift)
{
	int i;

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		struct ata_device *dev = &ap->device[i];
		if (ata_dev_present(dev)) {
			dev->dma_mode = xfer_mode;
			dev->xfer_mode = xfer_mode;
			dev->xfer_shift = xfer_shift;
			if (ap->ops->set_dmamode)
				ap->ops->set_dmamode(ap, dev);
		}
	}
}

/**
 *	ata_set_mode - Program timings and issue SET FEATURES - XFER
 *	@ap: port on which timings will be programmed
 *
 *	Set ATA device disk transfer mode (PIO3, UDMA6, etc.).
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 */
static void ata_set_mode(struct ata_port *ap)
{
	unsigned int xfer_shift;
	u8 xfer_mode;
	int rc;

	/* step 1: always set host PIO timings */
	rc = ata_host_set_pio(ap);
	if (rc)
		goto err_out;

	/* step 2: choose the best data xfer mode */
	xfer_mode = xfer_shift = 0;
	rc = ata_choose_xfer_mode(ap, &xfer_mode, &xfer_shift);
	if (rc)
		goto err_out;

	/* step 3: if that xfer mode isn't PIO, set host DMA timings */
	if (xfer_shift != ATA_SHIFT_PIO)
		ata_host_set_dma(ap, xfer_mode, xfer_shift);

	/* step 4: update devices' xfer mode */
	ata_dev_set_mode(ap, &ap->device[0]);
	ata_dev_set_mode(ap, &ap->device[1]);

	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		return;

	if (ap->ops->post_set_mode)
		ap->ops->post_set_mode(ap);

	return;

err_out:
	ata_port_disable(ap);
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
 *	LOCKING: None.
 *
 */

static unsigned int ata_busy_sleep (struct ata_port *ap,
				    unsigned long tmout_pat,
			    	    unsigned long tmout)
{
	unsigned long timer_start, timeout;
	u8 status;

	status = ata_busy_wait(ap, ATA_BUSY, 300);
	timer_start = jiffies;
	timeout = timer_start + tmout_pat;
	while ((status & ATA_BUSY) && (time_before(jiffies, timeout))) {
		msleep(50);
		status = ata_busy_wait(ap, ATA_BUSY, 3);
	}

	if (status & ATA_BUSY)
		printk(KERN_WARNING "ata%u is slow to respond, "
		       "please be patient\n", ap->id);

	timeout = timer_start + tmout;
	while ((status & ATA_BUSY) && (time_before(jiffies, timeout))) {
		msleep(50);
		status = ata_chk_status(ap);
	}

	if (status & ATA_BUSY) {
		printk(KERN_ERR "ata%u failed to respond (%lu secs)\n",
		       ap->id, tmout / HZ);
		return 1;
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
		if (ap->flags & ATA_FLAG_MMIO) {
			nsect = readb((void __iomem *) ioaddr->nsect_addr);
			lbal = readb((void __iomem *) ioaddr->lbal_addr);
		} else {
			nsect = inb(ioaddr->nsect_addr);
			lbal = inb(ioaddr->lbal_addr);
		}
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

/**
 *	ata_bus_edd - Issue EXECUTE DEVICE DIAGNOSTIC command.
 *	@ap: Port to reset and probe
 *
 *	Use the EXECUTE DEVICE DIAGNOSTIC command to reset and
 *	probe the bus.  Not often used these days.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *	Obtains host_set lock.
 *
 */

static unsigned int ata_bus_edd(struct ata_port *ap)
{
	struct ata_taskfile tf;
	unsigned long flags;

	/* set up execute-device-diag (bus reset) taskfile */
	/* also, take interrupts to a known state (disabled) */
	DPRINTK("execute-device-diag\n");
	ata_tf_init(ap, &tf, 0);
	tf.ctl |= ATA_NIEN;
	tf.command = ATA_CMD_EDD;
	tf.protocol = ATA_PROT_NODATA;

	/* do bus reset */
	spin_lock_irqsave(&ap->host_set->lock, flags);
	ata_tf_to_host(ap, &tf);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	/* spec says at least 2ms.  but who knows with those
	 * crazy ATAPI devices...
	 */
	msleep(150);

	return ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);
}

static unsigned int ata_bus_softreset(struct ata_port *ap,
				      unsigned int devmask)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	DPRINTK("ata%u: bus reset via SRST\n", ap->id);

	/* software reset.  causes dev0 to be selected */
	if (ap->flags & ATA_FLAG_MMIO) {
		writeb(ap->ctl, (void __iomem *) ioaddr->ctl_addr);
		udelay(20);	/* FIXME: flush */
		writeb(ap->ctl | ATA_SRST, (void __iomem *) ioaddr->ctl_addr);
		udelay(20);	/* FIXME: flush */
		writeb(ap->ctl, (void __iomem *) ioaddr->ctl_addr);
	} else {
		outb(ap->ctl, ioaddr->ctl_addr);
		udelay(10);
		outb(ap->ctl | ATA_SRST, ioaddr->ctl_addr);
		udelay(10);
		outb(ap->ctl, ioaddr->ctl_addr);
	}

	/* spec mandates ">= 2ms" before checking status.
	 * We wait 150ms, because that was the magic delay used for
	 * ATAPI devices in Hale Landis's ATADRVR, for the period of time
	 * between when the ATA command register is written, and then
	 * status is checked.  Because waiting for "a while" before
	 * checking status is fine, post SRST, we perform this magic
	 * delay here as well.
	 */
	msleep(150);

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
 *	Obtains host_set lock.
 *
 *	SIDE EFFECTS:
 *	Sets ATA_FLAG_PORT_DISABLED if bus reset fails.
 */

void ata_bus_reset(struct ata_port *ap)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int slave_possible = ap->flags & ATA_FLAG_SLAVE_POSS;
	u8 err;
	unsigned int dev0, dev1 = 0, rc = 0, devmask = 0;

	DPRINTK("ENTER, host %u, port %u\n", ap->id, ap->port_no);

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
		rc = ata_bus_softreset(ap, devmask);
	else if ((ap->flags & ATA_FLAG_SATA_RESET) == 0) {
		/* set up device control */
		if (ap->flags & ATA_FLAG_MMIO)
			writeb(ap->ctl, (void __iomem *) ioaddr->ctl_addr);
		else
			outb(ap->ctl, ioaddr->ctl_addr);
		rc = ata_bus_edd(ap);
	}

	if (rc)
		goto err_out;

	/*
	 * determine by signature whether we have ATA or ATAPI devices
	 */
	err = ata_dev_try_classify(ap, 0);
	if ((slave_possible) && (err != 0x81))
		ata_dev_try_classify(ap, 1);

	/* re-enable interrupts */
	if (ap->ioaddr.ctl_addr)	/* FIXME: hack. create a hook instead */
		ata_irq_on(ap);

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
		if (ap->flags & ATA_FLAG_MMIO)
			writeb(ap->ctl, (void __iomem *) ioaddr->ctl_addr);
		else
			outb(ap->ctl, ioaddr->ctl_addr);
	}

	DPRINTK("EXIT\n");
	return;

err_out:
	printk(KERN_ERR "ata%u: disabling port\n", ap->id);
	ap->ops->port_disable(ap);

	DPRINTK("EXIT\n");
}

static void ata_pr_blacklisted(const struct ata_port *ap,
			       const struct ata_device *dev)
{
	printk(KERN_WARNING "ata%u: dev %u is on DMA blacklist, disabling DMA\n",
		ap->id, dev->devno);
}

static const char * ata_dma_blacklist [] = {
	"WDC AC11000H",
	"WDC AC22100H",
	"WDC AC32500H",
	"WDC AC33100H",
	"WDC AC31600H",
	"WDC AC32100H",
	"WDC AC23200L",
	"Compaq CRD-8241B",
	"CRD-8400B",
	"CRD-8480B",
	"CRD-8482B",
 	"CRD-84",
	"SanDisk SDP3B",
	"SanDisk SDP3B-64",
	"SANYO CD-ROM CRD",
	"HITACHI CDR-8",
	"HITACHI CDR-8335",
	"HITACHI CDR-8435",
	"Toshiba CD-ROM XM-6202B",
	"TOSHIBA CD-ROM XM-1702BC",
	"CD-532E-A",
	"E-IDE CD-ROM CR-840",
	"CD-ROM Drive/F5A",
	"WPI CDD-820",
	"SAMSUNG CD-ROM SC-148C",
	"SAMSUNG CD-ROM SC",
	"SanDisk SDP3B-64",
	"ATAPI CD-ROM DRIVE 40X MAXIMUM",
	"_NEC DV5800A",
};

static int ata_dma_blacklisted(const struct ata_device *dev)
{
	unsigned char model_num[40];
	char *s;
	unsigned int len;
	int i;

	ata_dev_id_string(dev->id, model_num, ATA_ID_PROD_OFS,
			  sizeof(model_num));
	s = &model_num[0];
	len = strnlen(s, sizeof(model_num));

	/* ATAPI specifies that empty space is blank-filled; remove blanks */
	while ((len > 0) && (s[len - 1] == ' ')) {
		len--;
		s[len] = 0;
	}

	for (i = 0; i < ARRAY_SIZE(ata_dma_blacklist); i++)
		if (!strncmp(ata_dma_blacklist[i], s, len))
			return 1;

	return 0;
}

static unsigned int ata_get_mode_mask(const struct ata_port *ap, int shift)
{
	const struct ata_device *master, *slave;
	unsigned int mask;

	master = &ap->device[0];
	slave = &ap->device[1];

	assert (ata_dev_present(master) || ata_dev_present(slave));

	if (shift == ATA_SHIFT_UDMA) {
		mask = ap->udma_mask;
		if (ata_dev_present(master)) {
			mask &= (master->id[ATA_ID_UDMA_MODES] & 0xff);
			if (ata_dma_blacklisted(master)) {
				mask = 0;
				ata_pr_blacklisted(ap, master);
			}
		}
		if (ata_dev_present(slave)) {
			mask &= (slave->id[ATA_ID_UDMA_MODES] & 0xff);
			if (ata_dma_blacklisted(slave)) {
				mask = 0;
				ata_pr_blacklisted(ap, slave);
			}
		}
	}
	else if (shift == ATA_SHIFT_MWDMA) {
		mask = ap->mwdma_mask;
		if (ata_dev_present(master)) {
			mask &= (master->id[ATA_ID_MWDMA_MODES] & 0x07);
			if (ata_dma_blacklisted(master)) {
				mask = 0;
				ata_pr_blacklisted(ap, master);
			}
		}
		if (ata_dev_present(slave)) {
			mask &= (slave->id[ATA_ID_MWDMA_MODES] & 0x07);
			if (ata_dma_blacklisted(slave)) {
				mask = 0;
				ata_pr_blacklisted(ap, slave);
			}
		}
	}
	else if (shift == ATA_SHIFT_PIO) {
		mask = ap->pio_mask;
		if (ata_dev_present(master)) {
			/* spec doesn't return explicit support for
			 * PIO0-2, so we fake it
			 */
			u16 tmp_mode = master->id[ATA_ID_PIO_MODES] & 0x03;
			tmp_mode <<= 3;
			tmp_mode |= 0x7;
			mask &= tmp_mode;
		}
		if (ata_dev_present(slave)) {
			/* spec doesn't return explicit support for
			 * PIO0-2, so we fake it
			 */
			u16 tmp_mode = slave->id[ATA_ID_PIO_MODES] & 0x03;
			tmp_mode <<= 3;
			tmp_mode |= 0x7;
			mask &= tmp_mode;
		}
	}
	else {
		mask = 0xffffffff; /* shut up compiler warning */
		BUG();
	}

	return mask;
}

/* find greatest bit */
static int fgb(u32 bitmap)
{
	unsigned int i;
	int x = -1;

	for (i = 0; i < 32; i++)
		if (bitmap & (1 << i))
			x = i;

	return x;
}

/**
 *	ata_choose_xfer_mode - attempt to find best transfer mode
 *	@ap: Port for which an xfer mode will be selected
 *	@xfer_mode_out: (output) SET FEATURES - XFER MODE code
 *	@xfer_shift_out: (output) bit shift that selects this mode
 *
 *	Based on host and device capabilities, determine the
 *	maximum transfer mode that is amenable to all.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	Zero on success, negative on error.
 */

static int ata_choose_xfer_mode(const struct ata_port *ap,
				u8 *xfer_mode_out,
				unsigned int *xfer_shift_out)
{
	unsigned int mask, shift;
	int x, i;

	for (i = 0; i < ARRAY_SIZE(xfer_mode_classes); i++) {
		shift = xfer_mode_classes[i].shift;
		mask = ata_get_mode_mask(ap, shift);

		x = fgb(mask);
		if (x >= 0) {
			*xfer_mode_out = xfer_mode_classes[i].base + x;
			*xfer_shift_out = shift;
			return 0;
		}
	}

	return -1;
}

/**
 *	ata_dev_set_xfermode - Issue SET FEATURES - XFER MODE command
 *	@ap: Port associated with device @dev
 *	@dev: Device to which command will be sent
 *
 *	Issue SET FEATURES - XFER MODE command to device @dev
 *	on port @ap.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 */

static void ata_dev_set_xfermode(struct ata_port *ap, struct ata_device *dev)
{
	DECLARE_COMPLETION(wait);
	struct ata_queued_cmd *qc;
	int rc;
	unsigned long flags;

	/* set up set-features taskfile */
	DPRINTK("set features - xfer mode\n");

	qc = ata_qc_new_init(ap, dev);
	BUG_ON(qc == NULL);

	qc->tf.command = ATA_CMD_SET_FEATURES;
	qc->tf.feature = SETFEATURES_XFER;
	qc->tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	qc->tf.protocol = ATA_PROT_NODATA;
	qc->tf.nsect = dev->xfer_mode;

	qc->waiting = &wait;
	qc->complete_fn = ata_qc_complete_noop;

	spin_lock_irqsave(&ap->host_set->lock, flags);
	rc = ata_qc_issue(qc);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	if (rc)
		ata_port_disable(ap);
	else
		wait_for_completion(&wait);

	DPRINTK("EXIT\n");
}

/**
 *	ata_dev_reread_id - Reread the device identify device info
 *	@ap: port where the device is
 *	@dev: device to reread the identify device info
 *
 *	LOCKING:
 */

static void ata_dev_reread_id(struct ata_port *ap, struct ata_device *dev)
{
	DECLARE_COMPLETION(wait);
	struct ata_queued_cmd *qc;
	unsigned long flags;
	int rc;

	qc = ata_qc_new_init(ap, dev);
	BUG_ON(qc == NULL);

	ata_sg_init_one(qc, dev->id, sizeof(dev->id));
	qc->dma_dir = DMA_FROM_DEVICE;

	if (dev->class == ATA_DEV_ATA) {
		qc->tf.command = ATA_CMD_ID_ATA;
		DPRINTK("do ATA identify\n");
	} else {
		qc->tf.command = ATA_CMD_ID_ATAPI;
		DPRINTK("do ATAPI identify\n");
	}

	qc->tf.flags |= ATA_TFLAG_DEVICE;
	qc->tf.protocol = ATA_PROT_PIO;
	qc->nsect = 1;

	qc->waiting = &wait;
	qc->complete_fn = ata_qc_complete_noop;

	spin_lock_irqsave(&ap->host_set->lock, flags);
	rc = ata_qc_issue(qc);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	if (rc)
		goto err_out;

	wait_for_completion(&wait);

	swap_buf_le16(dev->id, ATA_ID_WORDS);

	ata_dump_id(dev);

	DPRINTK("EXIT\n");

	return;
err_out:
	ata_port_disable(ap);
}

/**
 *	ata_dev_init_params - Issue INIT DEV PARAMS command
 *	@ap: Port associated with device @dev
 *	@dev: Device to which command will be sent
 *
 *	LOCKING:
 */

static void ata_dev_init_params(struct ata_port *ap, struct ata_device *dev)
{
	DECLARE_COMPLETION(wait);
	struct ata_queued_cmd *qc;
	int rc;
	unsigned long flags;
	u16 sectors = dev->id[6];
	u16 heads   = dev->id[3];

	/* Number of sectors per track 1-255. Number of heads 1-16 */
	if (sectors < 1 || sectors > 255 || heads < 1 || heads > 16)
		return;

	/* set up init dev params taskfile */
	DPRINTK("init dev params \n");

	qc = ata_qc_new_init(ap, dev);
	BUG_ON(qc == NULL);

	qc->tf.command = ATA_CMD_INIT_DEV_PARAMS;
	qc->tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	qc->tf.protocol = ATA_PROT_NODATA;
	qc->tf.nsect = sectors;
	qc->tf.device |= (heads - 1) & 0x0f; /* max head = num. of heads - 1 */

	qc->waiting = &wait;
	qc->complete_fn = ata_qc_complete_noop;

	spin_lock_irqsave(&ap->host_set->lock, flags);
	rc = ata_qc_issue(qc);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	if (rc)
		ata_port_disable(ap);
	else
		wait_for_completion(&wait);

	DPRINTK("EXIT\n");
}

/**
 *	ata_sg_clean - Unmap DMA memory associated with command
 *	@qc: Command containing DMA memory to be released
 *
 *	Unmap all mapped DMA memory associated with this command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_sg_clean(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scatterlist *sg = qc->__sg;
	int dir = qc->dma_dir;
	void *pad_buf = NULL;

	assert(qc->flags & ATA_QCFLAG_DMAMAP);
	assert(sg != NULL);

	if (qc->flags & ATA_QCFLAG_SINGLE)
		assert(qc->n_elem == 1);

	DPRINTK("unmapping %u sg elements\n", qc->n_elem);

	/* if we padded the buffer out to 32-bit bound, and data
	 * xfer direction is from-device, we must copy from the
	 * pad buffer back into the supplied buffer
	 */
	if (qc->pad_len && !(qc->tf.flags & ATA_TFLAG_WRITE))
		pad_buf = ap->pad + (qc->tag * ATA_DMA_PAD_SZ);

	if (qc->flags & ATA_QCFLAG_SG) {
		dma_unmap_sg(ap->host_set->dev, sg, qc->n_elem, dir);
		/* restore last sg */
		sg[qc->orig_n_elem - 1].length += qc->pad_len;
		if (pad_buf) {
			struct scatterlist *psg = &qc->pad_sgent;
			void *addr = kmap_atomic(psg->page, KM_IRQ0);
			memcpy(addr + psg->offset, pad_buf, qc->pad_len);
			kunmap_atomic(psg->page, KM_IRQ0);
		}
	} else {
		dma_unmap_single(ap->host_set->dev, sg_dma_address(&sg[0]),
				 sg_dma_len(&sg[0]), dir);
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
 *	spin_lock_irqsave(host_set lock)
 *
 */
static void ata_fill_sg(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scatterlist *sg;
	unsigned int idx;

	assert(qc->__sg != NULL);
	assert(qc->n_elem > 0);

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
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS: 0 when ATAPI DMA can be used
 *               nonzero otherwise
 */
int ata_check_atapi_dma(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	int rc = 0; /* Assume ATAPI DMA is OK by default */

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
 *	spin_lock_irqsave(host_set lock)
 */
void ata_qc_prep(struct ata_queued_cmd *qc)
{
	if (!(qc->flags & ATA_QCFLAG_DMAMAP))
		return;

	ata_fill_sg(qc);
}

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
 *	spin_lock_irqsave(host_set lock)
 */

void ata_sg_init_one(struct ata_queued_cmd *qc, void *buf, unsigned int buflen)
{
	struct scatterlist *sg;

	qc->flags |= ATA_QCFLAG_SINGLE;

	memset(&qc->sgent, 0, sizeof(qc->sgent));
	qc->__sg = &qc->sgent;
	qc->n_elem = 1;
	qc->orig_n_elem = 1;
	qc->buf_virt = buf;

	sg = qc->__sg;
	sg_init_one(sg, buf, buflen);
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
 *	spin_lock_irqsave(host_set lock)
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
 *	spin_lock_irqsave(host_set lock)
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

	/* we must lengthen transfers to end on a 32-bit boundary */
	qc->pad_len = sg->length & 3;
	if (qc->pad_len) {
		void *pad_buf = ap->pad + (qc->tag * ATA_DMA_PAD_SZ);
		struct scatterlist *psg = &qc->pad_sgent;

		assert(qc->dev->class == ATA_DEV_ATAPI);

		memset(pad_buf, 0, ATA_DMA_PAD_SZ);

		if (qc->tf.flags & ATA_TFLAG_WRITE)
			memcpy(pad_buf, qc->buf_virt + sg->length - qc->pad_len,
			       qc->pad_len);

		sg_dma_address(psg) = ap->pad_dma + (qc->tag * ATA_DMA_PAD_SZ);
		sg_dma_len(psg) = ATA_DMA_PAD_SZ;
		/* trim sg */
		sg->length -= qc->pad_len;

		DPRINTK("padding done, sg->length=%u pad_len=%u\n",
			sg->length, qc->pad_len);
	}

	dma_address = dma_map_single(ap->host_set->dev, qc->buf_virt,
				     sg->length, dir);
	if (dma_mapping_error(dma_address)) {
		/* restore sg */
		sg->length += qc->pad_len;
		return -1;
	}

	sg_dma_address(sg) = dma_address;
	sg_dma_len(sg) = sg->length;

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
 *	spin_lock_irqsave(host_set lock)
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
	int n_elem, dir;

	VPRINTK("ENTER, ata%u\n", ap->id);
	assert(qc->flags & ATA_QCFLAG_SG);

	/* we must lengthen transfers to end on a 32-bit boundary */
	qc->pad_len = lsg->length & 3;
	if (qc->pad_len) {
		void *pad_buf = ap->pad + (qc->tag * ATA_DMA_PAD_SZ);
		struct scatterlist *psg = &qc->pad_sgent;
		unsigned int offset;

		assert(qc->dev->class == ATA_DEV_ATAPI);

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
			kunmap_atomic(psg->page, KM_IRQ0);
		}

		sg_dma_address(psg) = ap->pad_dma + (qc->tag * ATA_DMA_PAD_SZ);
		sg_dma_len(psg) = ATA_DMA_PAD_SZ;
		/* trim last sg */
		lsg->length -= qc->pad_len;

		DPRINTK("padding done, sg[%d].length=%u pad_len=%u\n",
			qc->n_elem - 1, lsg->length, qc->pad_len);
	}

	dir = qc->dma_dir;
	n_elem = dma_map_sg(ap->host_set->dev, sg, qc->n_elem, dir);
	if (n_elem < 1) {
		/* restore last sg */
		lsg->length += qc->pad_len;
		return -1;
	}

	DPRINTK("%d sg elements mapped\n", n_elem);

	qc->n_elem = n_elem;

	return 0;
}

/**
 *	ata_poll_qc_complete - turn irq back on and finish qc
 *	@qc: Command to complete
 *	@err_mask: ATA status register content
 *
 *	LOCKING:
 *	None.  (grabs host lock)
 */

void ata_poll_qc_complete(struct ata_queued_cmd *qc, unsigned int err_mask)
{
	struct ata_port *ap = qc->ap;
	unsigned long flags;

	spin_lock_irqsave(&ap->host_set->lock, flags);
	ata_irq_on(ap);
	ata_qc_complete(qc, err_mask);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);
}

/**
 *	ata_pio_poll -
 *	@ap: the target ata_port
 *
 *	LOCKING:
 *	None.  (executing in kernel thread context)
 *
 *	RETURNS:
 *	timeout value to use
 */

static unsigned long ata_pio_poll(struct ata_port *ap)
{
	u8 status;
	unsigned int poll_state = HSM_ST_UNKNOWN;
	unsigned int reg_state = HSM_ST_UNKNOWN;

	switch (ap->hsm_task_state) {
	case HSM_ST:
	case HSM_ST_POLL:
		poll_state = HSM_ST_POLL;
		reg_state = HSM_ST;
		break;
	case HSM_ST_LAST:
	case HSM_ST_LAST_POLL:
		poll_state = HSM_ST_LAST_POLL;
		reg_state = HSM_ST_LAST;
		break;
	default:
		BUG();
		break;
	}

	status = ata_chk_status(ap);
	if (status & ATA_BUSY) {
		if (time_after(jiffies, ap->pio_task_timeout)) {
			ap->hsm_task_state = HSM_ST_TMOUT;
			return 0;
		}
		ap->hsm_task_state = poll_state;
		return ATA_SHORT_PAUSE;
	}

	ap->hsm_task_state = reg_state;
	return 0;
}

/**
 *	ata_pio_complete - check if drive is busy or idle
 *	@ap: the target ata_port
 *
 *	LOCKING:
 *	None.  (executing in kernel thread context)
 *
 *	RETURNS:
 *	Zero if qc completed.
 *	Non-zero if has next.
 */

static int ata_pio_complete (struct ata_port *ap)
{
	struct ata_queued_cmd *qc;
	u8 drv_stat;

	/*
	 * This is purely heuristic.  This is a fast path.  Sometimes when
	 * we enter, BSY will be cleared in a chk-status or two.  If not,
	 * the drive is probably seeking or something.  Snooze for a couple
	 * msecs, then chk-status again.  If still busy, fall back to
	 * HSM_ST_LAST_POLL state.
	 */
	drv_stat = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 10);
	if (drv_stat & (ATA_BUSY | ATA_DRQ)) {
		msleep(2);
		drv_stat = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 10);
		if (drv_stat & (ATA_BUSY | ATA_DRQ)) {
			ap->hsm_task_state = HSM_ST_LAST_POLL;
			ap->pio_task_timeout = jiffies + ATA_TMOUT_PIO;
			return 1;
		}
	}

	drv_stat = ata_wait_idle(ap);
	if (!ata_ok(drv_stat)) {
		ap->hsm_task_state = HSM_ST_ERR;
		return 1;
	}

	qc = ata_qc_from_tag(ap, ap->active_tag);
	assert(qc != NULL);

	ap->hsm_task_state = HSM_ST_IDLE;

	ata_poll_qc_complete(qc, 0);

	/* another command may start at this point */

	return 0;
}


/**
 *	swap_buf_le16 - swap halves of 16-words in place
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
 *	ata_mmio_data_xfer - Transfer data by MMIO
 *	@ap: port to read/write
 *	@buf: data buffer
 *	@buflen: buffer length
 *	@write_data: read/write
 *
 *	Transfer data from/to the device data register by MMIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_mmio_data_xfer(struct ata_port *ap, unsigned char *buf,
			       unsigned int buflen, int write_data)
{
	unsigned int i;
	unsigned int words = buflen >> 1;
	u16 *buf16 = (u16 *) buf;
	void __iomem *mmio = (void __iomem *)ap->ioaddr.data_addr;

	/* Transfer multiple of 2 bytes */
	if (write_data) {
		for (i = 0; i < words; i++)
			writew(le16_to_cpu(buf16[i]), mmio);
	} else {
		for (i = 0; i < words; i++)
			buf16[i] = cpu_to_le16(readw(mmio));
	}

	/* Transfer trailing 1 byte, if any. */
	if (unlikely(buflen & 0x01)) {
		u16 align_buf[1] = { 0 };
		unsigned char *trailing_buf = buf + buflen - 1;

		if (write_data) {
			memcpy(align_buf, trailing_buf, 1);
			writew(le16_to_cpu(align_buf[0]), mmio);
		} else {
			align_buf[0] = cpu_to_le16(readw(mmio));
			memcpy(trailing_buf, align_buf, 1);
		}
	}
}

/**
 *	ata_pio_data_xfer - Transfer data by PIO
 *	@ap: port to read/write
 *	@buf: data buffer
 *	@buflen: buffer length
 *	@write_data: read/write
 *
 *	Transfer data from/to the device data register by PIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_pio_data_xfer(struct ata_port *ap, unsigned char *buf,
			      unsigned int buflen, int write_data)
{
	unsigned int words = buflen >> 1;

	/* Transfer multiple of 2 bytes */
	if (write_data)
		outsw(ap->ioaddr.data_addr, buf, words);
	else
		insw(ap->ioaddr.data_addr, buf, words);

	/* Transfer trailing 1 byte, if any. */
	if (unlikely(buflen & 0x01)) {
		u16 align_buf[1] = { 0 };
		unsigned char *trailing_buf = buf + buflen - 1;

		if (write_data) {
			memcpy(align_buf, trailing_buf, 1);
			outw(le16_to_cpu(align_buf[0]), ap->ioaddr.data_addr);
		} else {
			align_buf[0] = cpu_to_le16(inw(ap->ioaddr.data_addr));
			memcpy(trailing_buf, align_buf, 1);
		}
	}
}

/**
 *	ata_data_xfer - Transfer data from/to the data register.
 *	@ap: port to read/write
 *	@buf: data buffer
 *	@buflen: buffer length
 *	@do_write: read/write
 *
 *	Transfer data from/to the device data register.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_data_xfer(struct ata_port *ap, unsigned char *buf,
			  unsigned int buflen, int do_write)
{
	if (ap->flags & ATA_FLAG_MMIO)
		ata_mmio_data_xfer(ap, buf, buflen, do_write);
	else
		ata_pio_data_xfer(ap, buf, buflen, do_write);
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

	if (qc->cursect == (qc->nsect - 1))
		ap->hsm_task_state = HSM_ST_LAST;

	page = sg[qc->cursg].page;
	offset = sg[qc->cursg].offset + qc->cursg_ofs * ATA_SECT_SIZE;

	/* get the current page and offset */
	page = nth_page(page, (offset >> PAGE_SHIFT));
	offset %= PAGE_SIZE;

	DPRINTK("data %s\n", qc->tf.flags & ATA_TFLAG_WRITE ? "write" : "read");

	if (PageHighMem(page)) {
		unsigned long flags;

		local_irq_save(flags);
		buf = kmap_atomic(page, KM_IRQ0);

		/* do the actual data transfer */
		ata_data_xfer(ap, buf + offset, ATA_SECT_SIZE, do_write);

		kunmap_atomic(buf, KM_IRQ0);
		local_irq_restore(flags);
	} else {
		buf = page_address(page);
		ata_data_xfer(ap, buf + offset, ATA_SECT_SIZE, do_write);
	}

	qc->cursect++;
	qc->cursg_ofs++;

	if ((qc->cursg_ofs * ATA_SECT_SIZE) == (&sg[qc->cursg])->length) {
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

		assert(qc->dev->multi_count);

		nsect = min(qc->nsect - qc->cursect, qc->dev->multi_count);
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
	assert(ap->cdb_len >= 12);

	ata_data_xfer(ap, qc->cdb, ap->cdb_len, 1);
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
 *	ata_pio_first_block - Write first data block to hardware
 *	@ap: Port to which ATA/ATAPI device is attached.
 *
 *	When device has indicated its readiness to accept
 *	the data, this function sends out the CDB or 
 *	the first data block by PIO.
 *	After this, 
 *	  - If polling, ata_pio_task() handles the rest.
 *	  - Otherwise, interrupt handler takes over.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	Zero if irq handler takes over
 *	Non-zero if has next (polling).
 */

static int ata_pio_first_block(struct ata_port *ap)
{
	struct ata_queued_cmd *qc;
	u8 status;
	unsigned long flags;
	int has_next;

	qc = ata_qc_from_tag(ap, ap->active_tag);
	assert(qc != NULL);
	assert(qc->flags & ATA_QCFLAG_ACTIVE);

	/* if polling, we will stay in the work queue after sending the data.
	 * otherwise, interrupt handler takes over after sending the data.
	 */
	has_next = (qc->tf.flags & ATA_TFLAG_POLLING);

	/* sleep-wait for BSY to clear */
	DPRINTK("busy wait\n");
	if (ata_busy_sleep(ap, ATA_TMOUT_DATAOUT_QUICK, ATA_TMOUT_DATAOUT)) {
		ap->hsm_task_state = HSM_ST_TMOUT;
		goto err_out;
	}

	/* make sure DRQ is set */
	status = ata_chk_status(ap);
	if ((status & (ATA_BUSY | ATA_DRQ)) != ATA_DRQ) {
		/* device status error */
		ap->hsm_task_state = HSM_ST_ERR;
		goto err_out;
	}

	/* Send the CDB (atapi) or the first data block (ata pio out).
	 * During the state transition, interrupt handler shouldn't
	 * be invoked before the data transfer is complete and
	 * hsm_task_state is changed. Hence, the following locking.
	 */
	spin_lock_irqsave(&ap->host_set->lock, flags);

	if (qc->tf.protocol == ATA_PROT_PIO) {
		/* PIO data out protocol.
		 * send first data block.
		 */

		/* ata_pio_sectors() might change the state to HSM_ST_LAST.
		 * so, the state is changed here before ata_pio_sectors().
		 */
		ap->hsm_task_state = HSM_ST;
		ata_pio_sectors(qc);
		ata_altstatus(ap); /* flush */
	} else
		/* send CDB */
		atapi_send_cdb(ap, qc);

	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	/* if polling, ata_pio_task() handles the rest.
	 * otherwise, interrupt handler takes over from here.
	 */
	return has_next;

err_out:
	return 1; /* has next */
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
			printk(KERN_WARNING "ata%u: %u bytes trailing data\n",
			       ap->id, bytes);

		for (i = 0; i < words; i++)
			ata_data_xfer(ap, (unsigned char*)pad_buf, 2, do_write);

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

		local_irq_save(flags);
		buf = kmap_atomic(page, KM_IRQ0);

		/* do the actual data transfer */
		ata_data_xfer(ap, buf + offset, count, do_write);

		kunmap_atomic(buf, KM_IRQ0);
		local_irq_restore(flags);
	} else {
		buf = page_address(page);
		ata_data_xfer(ap, buf + offset, count, do_write);
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

	ap->ops->tf_read(ap, &qc->tf);
	ireason = qc->tf.nsect;
	bc_lo = qc->tf.lbam;
	bc_hi = qc->tf.lbah;
	bytes = (bc_hi << 8) | bc_lo;

	/* shall be cleared to zero, indicating xfer of data */
	if (ireason & (1 << 0))
		goto err_out;

	/* make sure transfer direction matches expected */
	i_write = ((ireason & (1 << 1)) == 0) ? 1 : 0;
	if (do_write != i_write)
		goto err_out;

	VPRINTK("ata%u: xfering %d bytes\n", ap->id, bytes);

	__atapi_pio_bytes(qc, bytes);

	return;

err_out:
	printk(KERN_INFO "ata%u: dev %u: ATAPI check failed\n",
	      ap->id, dev->devno);
	ap->hsm_task_state = HSM_ST_ERR;
}

/**
 *	ata_pio_block - start PIO on a block
 *	@ap: the target ata_port
 *
 *	LOCKING:
 *	None.  (executing in kernel thread context)
 */

static void ata_pio_block(struct ata_port *ap)
{
	struct ata_queued_cmd *qc;
	u8 status;

	/*
	 * This is purely heuristic.  This is a fast path.
	 * Sometimes when we enter, BSY will be cleared in
	 * a chk-status or two.  If not, the drive is probably seeking
	 * or something.  Snooze for a couple msecs, then
	 * chk-status again.  If still busy, fall back to
	 * HSM_ST_POLL state.
	 */
	status = ata_busy_wait(ap, ATA_BUSY, 5);
	if (status & ATA_BUSY) {
		msleep(2);
		status = ata_busy_wait(ap, ATA_BUSY, 10);
		if (status & ATA_BUSY) {
			ap->hsm_task_state = HSM_ST_POLL;
			ap->pio_task_timeout = jiffies + ATA_TMOUT_PIO;
			return;
		}
	}

	qc = ata_qc_from_tag(ap, ap->active_tag);
	assert(qc != NULL);

	if (is_atapi_taskfile(&qc->tf)) {
		/* no more data to transfer or unsupported ATAPI command */
		if ((status & ATA_DRQ) == 0) {
			ap->hsm_task_state = HSM_ST_LAST;
			return;
		}

		atapi_pio_bytes(qc);
	} else {
		/* handle BSY=0, DRQ=0 as error */
		if ((status & ATA_DRQ) == 0) {
			ap->hsm_task_state = HSM_ST_ERR;
			return;
		}

		ata_pio_sectors(qc);
	}

	ata_altstatus(ap); /* flush */
}

static void ata_pio_error(struct ata_port *ap)
{
	struct ata_queued_cmd *qc;

	printk(KERN_WARNING "ata%u: PIO error\n", ap->id);

	qc = ata_qc_from_tag(ap, ap->active_tag);
	assert(qc != NULL);

	ap->hsm_task_state = HSM_ST_IDLE;

	ata_poll_qc_complete(qc, AC_ERR_ATA_BUS);
}

static void ata_pio_task(void *_data)
{
	struct ata_port *ap = _data;
	unsigned long timeout;
	int has_next;

fsm_start:
	timeout = 0;
	has_next = 1;

	switch (ap->hsm_task_state) {
	case HSM_ST_FIRST:
		has_next = ata_pio_first_block(ap);
		break;

	case HSM_ST:
		ata_pio_block(ap);
		break;

	case HSM_ST_LAST:
		has_next = ata_pio_complete(ap);
		break;

	case HSM_ST_POLL:
	case HSM_ST_LAST_POLL:
		timeout = ata_pio_poll(ap);
		break;

	case HSM_ST_TMOUT:
	case HSM_ST_ERR:
		ata_pio_error(ap);
		return;

	default:
		BUG();
		return;
	}

	if (timeout)
		queue_delayed_work(ata_wq, &ap->pio_task, timeout);
	else if (has_next)
		goto fsm_start;
}

/**
 *	ata_qc_timeout - Handle timeout of queued command
 *	@qc: Command that timed out
 *
 *	Some part of the kernel (currently, only the SCSI layer)
 *	has noticed that the active command on port @ap has not
 *	completed after a specified length of time.  Handle this
 *	condition by disabling DMA (if necessary) and completing
 *	transactions, with error if necessary.
 *
 *	This also handles the case of the "lost interrupt", where
 *	for some reason (possibly hardware bug, possibly driver bug)
 *	an interrupt was not delivered to the driver, even though the
 *	transaction completed successfully.
 *
 *	LOCKING:
 *	Inherited from SCSI layer (none, can sleep)
 */

static void ata_qc_timeout(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_host_set *host_set = ap->host_set;
	struct ata_device *dev = qc->dev;
	u8 host_stat = 0, drv_stat;
	unsigned long flags;

	DPRINTK("ENTER\n");

	/* FIXME: doesn't this conflict with timeout handling? */
	if (qc->dev->class == ATA_DEV_ATAPI && qc->scsicmd) {
		struct scsi_cmnd *cmd = qc->scsicmd;

		if (!(cmd->eh_eflags & SCSI_EH_CANCEL_CMD)) {

			/* finish completing original command */
			spin_lock_irqsave(&host_set->lock, flags);
			__ata_qc_complete(qc);
			spin_unlock_irqrestore(&host_set->lock, flags);

			atapi_request_sense(ap, dev, cmd);

			cmd->result = (CHECK_CONDITION << 1) | (DID_OK << 16);
			scsi_finish_command(cmd);

			goto out;
		}
	}

	spin_lock_irqsave(&host_set->lock, flags);

	/* hack alert!  We cannot use the supplied completion
	 * function from inside the ->eh_strategy_handler() thread.
	 * libata is the only user of ->eh_strategy_handler() in
	 * any kernel, so the default scsi_done() assumes it is
	 * not being called from the SCSI EH.
	 */
	qc->scsidone = scsi_finish_command;

	switch (qc->tf.protocol) {

	case ATA_PROT_DMA:
	case ATA_PROT_ATAPI_DMA:
		host_stat = ap->ops->bmdma_status(ap);

		/* before we do anything else, clear DMA-Start bit */
		ap->ops->bmdma_stop(qc);

		/* fall through */

	default:
		ata_altstatus(ap);
		drv_stat = ata_chk_status(ap);

		/* ack bmdma irq events */
		ap->ops->irq_clear(ap);

		printk(KERN_ERR "ata%u: command 0x%x timeout, stat 0x%x host_stat 0x%x\n",
		       ap->id, qc->tf.command, drv_stat, host_stat);

		ap->hsm_task_state = HSM_ST_IDLE;

		/* complete taskfile transaction */
		ata_qc_complete(qc, ac_err_mask(drv_stat));
		break;
	}

	spin_unlock_irqrestore(&host_set->lock, flags);

out:
	DPRINTK("EXIT\n");
}

/**
 *	ata_eng_timeout - Handle timeout of queued command
 *	@ap: Port on which timed-out command is active
 *
 *	Some part of the kernel (currently, only the SCSI layer)
 *	has noticed that the active command on port @ap has not
 *	completed after a specified length of time.  Handle this
 *	condition by disabling DMA (if necessary) and completing
 *	transactions, with error if necessary.
 *
 *	This also handles the case of the "lost interrupt", where
 *	for some reason (possibly hardware bug, possibly driver bug)
 *	an interrupt was not delivered to the driver, even though the
 *	transaction completed successfully.
 *
 *	LOCKING:
 *	Inherited from SCSI layer (none, can sleep)
 */

void ata_eng_timeout(struct ata_port *ap)
{
	struct ata_queued_cmd *qc;

	DPRINTK("ENTER\n");

	qc = ata_qc_from_tag(ap, ap->active_tag);
	if (qc)
		ata_qc_timeout(qc);
	else {
		printk(KERN_ERR "ata%u: BUG: timeout without command\n",
		       ap->id);
		goto out;
	}

out:
	DPRINTK("EXIT\n");
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

	for (i = 0; i < ATA_MAX_QUEUE; i++)
		if (!test_and_set_bit(i, &ap->qactive)) {
			qc = ata_qc_from_tag(ap, i);
			break;
		}

	if (qc)
		qc->tag = i;

	return qc;
}

/**
 *	ata_qc_new_init - Request an available ATA command, and initialize it
 *	@ap: Port associated with device @dev
 *	@dev: Device from whom we request an available command structure
 *
 *	LOCKING:
 *	None.
 */

struct ata_queued_cmd *ata_qc_new_init(struct ata_port *ap,
				      struct ata_device *dev)
{
	struct ata_queued_cmd *qc;

	qc = ata_qc_new(ap);
	if (qc) {
		qc->__sg = NULL;
		qc->flags = 0;
		qc->scsicmd = NULL;
		qc->ap = ap;
		qc->dev = dev;
		qc->cursect = qc->cursg = qc->cursg_ofs = 0;
		qc->nsect = 0;
		qc->nbytes = qc->curbytes = 0;

		ata_tf_init(ap, &qc->tf, dev->devno);
	}

	return qc;
}

int ata_qc_complete_noop(struct ata_queued_cmd *qc, unsigned int err_mask)
{
	return 0;
}

static void __ata_qc_complete(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int tag, do_clear = 0;

	qc->flags = 0;
	tag = qc->tag;
	if (likely(ata_tag_valid(tag))) {
		if (tag == ap->active_tag)
			ap->active_tag = ATA_TAG_POISON;
		qc->tag = ATA_TAG_POISON;
		do_clear = 1;
	}

	if (qc->waiting) {
		struct completion *waiting = qc->waiting;
		qc->waiting = NULL;
		complete(waiting);
	}

	if (likely(do_clear))
		clear_bit(tag, &ap->qactive);
}

/**
 *	ata_qc_free - free unused ata_queued_cmd
 *	@qc: Command to complete
 *
 *	Designed to free unused ata_queued_cmd object
 *	in case something prevents using it.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
void ata_qc_free(struct ata_queued_cmd *qc)
{
	assert(qc != NULL);	/* ata_qc_from_tag _might_ return NULL */
	assert(qc->waiting == NULL);	/* nothing should be waiting */

	__ata_qc_complete(qc);
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
 *	spin_lock_irqsave(host_set lock)
 */

void ata_qc_complete(struct ata_queued_cmd *qc, unsigned int err_mask)
{
	int rc;

	assert(qc != NULL);	/* ata_qc_from_tag _might_ return NULL */
	assert(qc->flags & ATA_QCFLAG_ACTIVE);

	if (likely(qc->flags & ATA_QCFLAG_DMAMAP))
		ata_sg_clean(qc);

	/* atapi: mark qc as inactive to prevent the interrupt handler
	 * from completing the command twice later, before the error handler
	 * is called. (when rc != 0 and atapi request sense is needed)
	 */
	qc->flags &= ~ATA_QCFLAG_ACTIVE;

	/* call completion callback */
	rc = qc->complete_fn(qc, err_mask);

	/* if callback indicates not to complete command (non-zero),
	 * return immediately
	 */
	if (rc != 0)
		return;

	__ata_qc_complete(qc);

	VPRINTK("EXIT\n");
}

static inline int ata_should_dma_map(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
	case ATA_PROT_ATAPI_DMA:
		return 1;

	case ATA_PROT_ATAPI:
	case ATA_PROT_PIO:
	case ATA_PROT_PIO_MULT:
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
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Zero on success, negative on error.
 */

int ata_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	if (ata_should_dma_map(qc)) {
		if (qc->flags & ATA_QCFLAG_SG) {
			if (ata_sg_setup(qc))
				goto err_out;
		} else if (qc->flags & ATA_QCFLAG_SINGLE) {
			if (ata_sg_setup_one(qc))
				goto err_out;
		}
	} else {
		qc->flags &= ~ATA_QCFLAG_DMAMAP;
	}

	ap->ops->qc_prep(qc);

	qc->ap->active_tag = qc->tag;
	qc->flags |= ATA_QCFLAG_ACTIVE;

	return ap->ops->qc_issue(qc);

err_out:
	return -1;
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
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Zero on success, negative on error.
 */

int ata_qc_issue_prot(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	/* Use polling pio if the LLD doesn't handle
	 * interrupt driven pio and atapi CDB interrupt.
	 */
	if (ap->flags & ATA_FLAG_PIO_POLLING) {
		switch (qc->tf.protocol) {
		case ATA_PROT_PIO:
		case ATA_PROT_ATAPI:
		case ATA_PROT_ATAPI_NODATA:
			qc->tf.flags |= ATA_TFLAG_POLLING;
			break;
		case ATA_PROT_ATAPI_DMA:
			if (qc->dev->flags & ATA_DFLAG_CDB_INTR)
				BUG();
			break;
		default:
			break;
		}
	}

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
			queue_work(ata_wq, &ap->pio_task);

		break;

	case ATA_PROT_DMA:
		assert(!(qc->tf.flags & ATA_TFLAG_POLLING));

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
			queue_work(ata_wq, &ap->pio_task);

			/* always send first data block using
			 * the ata_pio_task() codepath.
			 */
		} else {
			/* PIO data in protocol */
			ap->hsm_task_state = HSM_ST;

			if (qc->tf.flags & ATA_TFLAG_POLLING)
				queue_work(ata_wq, &ap->pio_task);

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
			queue_work(ata_wq, &ap->pio_task);
		break;

	case ATA_PROT_ATAPI_DMA:
		assert(!(qc->tf.flags & ATA_TFLAG_POLLING));

		ap->ops->tf_load(ap, &qc->tf);	 /* load tf registers */
		ap->ops->bmdma_setup(qc);	    /* set up bmdma */
		ap->hsm_task_state = HSM_ST_FIRST;

		/* send cdb by polling if no cdb interrupt */
		if (!(qc->dev->flags & ATA_DFLAG_CDB_INTR))
			queue_work(ata_wq, &ap->pio_task);
		break;

	default:
		WARN_ON(1);
		return -1;
	}

	return 0;
}

/**
 *	ata_bmdma_setup_mmio - Set up PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_bmdma_setup_mmio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	u8 dmactl;
	void __iomem *mmio = (void __iomem *) ap->ioaddr.bmdma_addr;

	/* load PRD table addr. */
	mb();	/* make sure PRD table writes are visible to controller */
	writel(ap->prd_dma, mmio + ATA_DMA_TABLE_OFS);

	/* specify data direction, triple-check start bit is clear */
	dmactl = readb(mmio + ATA_DMA_CMD);
	dmactl &= ~(ATA_DMA_WR | ATA_DMA_START);
	if (!rw)
		dmactl |= ATA_DMA_WR;
	writeb(dmactl, mmio + ATA_DMA_CMD);

	/* issue r/w command */
	ap->ops->exec_command(ap, &qc->tf);
}

/**
 *	ata_bmdma_start_mmio - Start a PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_bmdma_start_mmio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *mmio = (void __iomem *) ap->ioaddr.bmdma_addr;
	u8 dmactl;

	/* start host DMA transaction */
	dmactl = readb(mmio + ATA_DMA_CMD);
	writeb(dmactl | ATA_DMA_START, mmio + ATA_DMA_CMD);

	/* Strictly, one may wish to issue a readb() here, to
	 * flush the mmio write.  However, control also passes
	 * to the hardware at this point, and it will interrupt
	 * us when we are to resume control.  So, in effect,
	 * we don't care when the mmio write flushes.
	 * Further, a read of the DMA status register _immediately_
	 * following the write may not be what certain flaky hardware
	 * is expected, so I think it is best to not add a readb()
	 * without first all the MMIO ATA cards/mobos.
	 * Or maybe I'm just being paranoid.
	 */
}

/**
 *	ata_bmdma_setup_pio - Set up PCI IDE BMDMA transaction (PIO)
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_bmdma_setup_pio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	u8 dmactl;

	/* load PRD table addr. */
	outl(ap->prd_dma, ap->ioaddr.bmdma_addr + ATA_DMA_TABLE_OFS);

	/* specify data direction, triple-check start bit is clear */
	dmactl = inb(ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	dmactl &= ~(ATA_DMA_WR | ATA_DMA_START);
	if (!rw)
		dmactl |= ATA_DMA_WR;
	outb(dmactl, ap->ioaddr.bmdma_addr + ATA_DMA_CMD);

	/* issue r/w command */
	ap->ops->exec_command(ap, &qc->tf);
}

/**
 *	ata_bmdma_start_pio - Start a PCI IDE BMDMA transaction (PIO)
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_bmdma_start_pio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	u8 dmactl;

	/* start host DMA transaction */
	dmactl = inb(ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	outb(dmactl | ATA_DMA_START,
	     ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
}


/**
 *	ata_bmdma_start - Start a PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	Writes the ATA_DMA_START flag to the DMA command register.
 *
 *	May be used as the bmdma_start() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
void ata_bmdma_start(struct ata_queued_cmd *qc)
{
	if (qc->ap->flags & ATA_FLAG_MMIO)
		ata_bmdma_start_mmio(qc);
	else
		ata_bmdma_start_pio(qc);
}


/**
 *	ata_bmdma_setup - Set up PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	Writes address of PRD table to device's PRD Table Address
 *	register, sets the DMA control register, and calls
 *	ops->exec_command() to start the transfer.
 *
 *	May be used as the bmdma_setup() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
void ata_bmdma_setup(struct ata_queued_cmd *qc)
{
	if (qc->ap->flags & ATA_FLAG_MMIO)
		ata_bmdma_setup_mmio(qc);
	else
		ata_bmdma_setup_pio(qc);
}


/**
 *	ata_bmdma_irq_clear - Clear PCI IDE BMDMA interrupt.
 *	@ap: Port associated with this ATA transaction.
 *
 *	Clear interrupt and error flags in DMA status register.
 *
 *	May be used as the irq_clear() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_bmdma_irq_clear(struct ata_port *ap)
{
    if (ap->flags & ATA_FLAG_MMIO) {
        void __iomem *mmio = ((void __iomem *) ap->ioaddr.bmdma_addr) + ATA_DMA_STATUS;
        writeb(readb(mmio), mmio);
    } else {
        unsigned long addr = ap->ioaddr.bmdma_addr + ATA_DMA_STATUS;
        outb(inb(addr), addr);
    }

}


/**
 *	ata_bmdma_status - Read PCI IDE BMDMA status
 *	@ap: Port associated with this ATA transaction.
 *
 *	Read and return BMDMA status register.
 *
 *	May be used as the bmdma_status() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

u8 ata_bmdma_status(struct ata_port *ap)
{
	u8 host_stat;
	if (ap->flags & ATA_FLAG_MMIO) {
		void __iomem *mmio = (void __iomem *) ap->ioaddr.bmdma_addr;
		host_stat = readb(mmio + ATA_DMA_STATUS);
	} else
		host_stat = inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	return host_stat;
}


/**
 *	ata_bmdma_stop - Stop PCI IDE BMDMA transfer
 *	@qc: Command we are ending DMA for
 *
 *	Clears the ATA_DMA_START flag in the dma control register
 *
 *	May be used as the bmdma_stop() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	if (ap->flags & ATA_FLAG_MMIO) {
		void __iomem *mmio = (void __iomem *) ap->ioaddr.bmdma_addr;

		/* clear start/stop bit */
		writeb(readb(mmio + ATA_DMA_CMD) & ~ATA_DMA_START,
			mmio + ATA_DMA_CMD);
	} else {
		/* clear start/stop bit */
		outb(inb(ap->ioaddr.bmdma_addr + ATA_DMA_CMD) & ~ATA_DMA_START,
			ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	}

	/* one-PIO-cycle guaranteed wait, per spec, for HDMA1:0 transition */
	ata_altstatus(ap);        /* dummy read */
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
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	One if interrupt was handled, zero if not (shared irq).
 */

inline unsigned int ata_host_intr (struct ata_port *ap,
				   struct ata_queued_cmd *qc)
{
	u8 status, host_stat = 0;

	VPRINTK("ata%u: protocol %d task_state %d\n",
		ap->id, qc->tf.protocol, ap->hsm_task_state);

	/* Check whether we are expecting interrupt in this state */
	switch (ap->hsm_task_state) {
	case HSM_ST_FIRST:
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
			VPRINTK("ata%u: host_stat 0x%X\n", ap->id, host_stat);

			/* if it's not our irq... */
			if (!(host_stat & ATA_DMA_INTR))
				goto idle_irq;

			/* before we do anything else, clear DMA-Start bit */
			ap->ops->bmdma_stop(qc);
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

	DPRINTK("ata%u: protocol %d task_state %d (dev_stat 0x%X)\n",
		ap->id, qc->tf.protocol, ap->hsm_task_state, status);

	/* ack bmdma irq events */
	ap->ops->irq_clear(ap);

	/* check error */
	if (unlikely((status & ATA_ERR) || (host_stat & ATA_DMA_ERR)))
		ap->hsm_task_state = HSM_ST_ERR;

fsm_start:
	switch (ap->hsm_task_state) {
	case HSM_ST_FIRST:
		/* Some pre-ATAPI-4 devices assert INTRQ 
		 * at this state when ready to receive CDB.
		 */

		/* check device status */
		if (unlikely((status & (ATA_BUSY | ATA_DRQ)) != ATA_DRQ)) {
			/* Wrong status. Let EH handle this */
			ap->hsm_task_state = HSM_ST_ERR;
			goto fsm_start;
		}

		atapi_send_cdb(ap, qc);

		break;

	case HSM_ST:
		/* complete command or read/write the data register */
		if (qc->tf.protocol == ATA_PROT_ATAPI) {
			/* ATAPI PIO protocol */
			if ((status & ATA_DRQ) == 0) {
				/* no more data to transfer */
				ap->hsm_task_state = HSM_ST_LAST;
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
				ap->hsm_task_state = HSM_ST_ERR;
				goto fsm_start;
			}

			ata_pio_sectors(qc);

			if (ap->hsm_task_state == HSM_ST_LAST &&
			    (!(qc->tf.flags & ATA_TFLAG_WRITE))) {
				/* all data read */
				ata_altstatus(ap);
				status = ata_chk_status(ap);
				goto fsm_start;
			}
		}

		ata_altstatus(ap); /* flush */
		break;

	case HSM_ST_LAST:
		if (unlikely(status & ATA_DRQ)) {
			/* handle DRQ=1 as error */
			ap->hsm_task_state = HSM_ST_ERR;
			goto fsm_start;
		}

		/* no more data to transfer */
		DPRINTK("ata%u: command complete, drv_stat 0x%x\n",
			ap->id, status);

		ap->hsm_task_state = HSM_ST_IDLE;

		/* complete taskfile transaction */
		ata_qc_complete(qc, ac_err_mask(status));
		break;

	case HSM_ST_ERR:
		printk(KERN_ERR "ata%u: command error, drv_stat 0x%x host_stat 0x%x\n",
		       ap->id, status, host_stat);

		ap->hsm_task_state = HSM_ST_IDLE;
		ata_qc_complete(qc, status | ATA_ERR);
		break;
	default:
		goto idle_irq;
	}

	return 1;	/* irq handled */

idle_irq:
	ap->stats.idle_irq++;

#ifdef ATA_IRQ_TRAP
	if ((ap->stats.idle_irq % 1000) == 0) {
		handled = 1;
		ata_irq_ack(ap, 0); /* debug trap */
		printk(KERN_WARNING "ata%d: irq trap\n", ap->id);
	}
#endif
	return 0;	/* irq not handled */
}

/**
 *	ata_interrupt - Default ATA host interrupt handler
 *	@irq: irq line (unused)
 *	@dev_instance: pointer to our ata_host_set information structure
 *	@regs: unused
 *
 *	Default interrupt handler for PCI IDE devices.  Calls
 *	ata_host_intr() for each port that is not disabled.
 *
 *	LOCKING:
 *	Obtains host_set lock during operation.
 *
 *	RETURNS:
 *	IRQ_NONE or IRQ_HANDLED.
 */

irqreturn_t ata_interrupt (int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	unsigned int i;
	unsigned int handled = 0;
	unsigned long flags;

	/* TODO: make _irqsave conditional on x86 PCI IDE legacy mode */
	spin_lock_irqsave(&host_set->lock, flags);

	for (i = 0; i < host_set->n_ports; i++) {
		struct ata_port *ap;

		ap = host_set->ports[i];
		if (ap &&
		    !(ap->flags & ATA_FLAG_PORT_DISABLED)) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING)) &&
			    (qc->flags & ATA_QCFLAG_ACTIVE))
				handled |= ata_host_intr(ap, qc);
		}
	}

	spin_unlock_irqrestore(&host_set->lock, flags);

	return IRQ_RETVAL(handled);
}

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

int ata_port_start (struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	int rc;

	ap->prd = dma_alloc_coherent(dev, ATA_PRD_TBL_SZ, &ap->prd_dma, GFP_KERNEL);
	if (!ap->prd)
		return -ENOMEM;

	rc = ata_pad_alloc(ap, dev);
	if (rc) {
		dma_free_coherent(dev, ATA_PRD_TBL_SZ, ap->prd, ap->prd_dma);
		return rc;
	}

	DPRINTK("prd alloc, virt %p, dma %llx\n", ap->prd, (unsigned long long) ap->prd_dma);

	return 0;
}


/**
 *	ata_port_stop - Undo ata_port_start()
 *	@ap: Port to shut down
 *
 *	Frees the PRD table.
 *
 *	May be used as the port_stop() entry in ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_port_stop (struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;

	dma_free_coherent(dev, ATA_PRD_TBL_SZ, ap->prd, ap->prd_dma);
	ata_pad_free(ap, dev);
}

void ata_host_stop (struct ata_host_set *host_set)
{
	if (host_set->mmio_base)
		iounmap(host_set->mmio_base);
}


/**
 *	ata_host_remove - Unregister SCSI host structure with upper layers
 *	@ap: Port to unregister
 *	@do_unregister: 1 if we fully unregister, 0 to just stop the port
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_host_remove(struct ata_port *ap, unsigned int do_unregister)
{
	struct Scsi_Host *sh = ap->host;

	DPRINTK("ENTER\n");

	if (do_unregister)
		scsi_remove_host(sh);

	ap->ops->port_stop(ap);
}

/**
 *	ata_host_init - Initialize an ata_port structure
 *	@ap: Structure to initialize
 *	@host: associated SCSI mid-layer structure
 *	@host_set: Collection of hosts to which @ap belongs
 *	@ent: Probe information provided by low-level driver
 *	@port_no: Port number associated with this ata_port
 *
 *	Initialize a new ata_port structure, and its associated
 *	scsi_host.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_host_init(struct ata_port *ap, struct Scsi_Host *host,
			  struct ata_host_set *host_set,
			  const struct ata_probe_ent *ent, unsigned int port_no)
{
	unsigned int i;

	host->max_id = 16;
	host->max_lun = 1;
	host->max_channel = 1;
	host->unique_id = ata_unique_id++;
	host->max_cmd_len = 12;

	ap->flags = ATA_FLAG_PORT_DISABLED;
	ap->id = host->unique_id;
	ap->host = host;
	ap->ctl = ATA_DEVCTL_OBS;
	ap->host_set = host_set;
	ap->port_no = port_no;
	ap->hard_port_no =
		ent->legacy_mode ? ent->hard_port_no : port_no;
	ap->pio_mask = ent->pio_mask;
	ap->mwdma_mask = ent->mwdma_mask;
	ap->udma_mask = ent->udma_mask;
	ap->flags |= ent->host_flags;
	ap->ops = ent->port_ops;
	ap->cbl = ATA_CBL_NONE;
	ap->active_tag = ATA_TAG_POISON;
	ap->last_ctl = 0xFF;

	INIT_WORK(&ap->pio_task, ata_pio_task, ap);

	for (i = 0; i < ATA_MAX_DEVICES; i++)
		ap->device[i].devno = i;

#ifdef ATA_IRQ_TRAP
	ap->stats.unhandled_irq = 1;
	ap->stats.idle_irq = 1;
#endif

	memcpy(&ap->ioaddr, &ent->port[port_no], sizeof(struct ata_ioports));
}

/**
 *	ata_host_add - Attach low-level ATA driver to system
 *	@ent: Information provided by low-level driver
 *	@host_set: Collections of ports to which we add
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

static struct ata_port * ata_host_add(const struct ata_probe_ent *ent,
				      struct ata_host_set *host_set,
				      unsigned int port_no)
{
	struct Scsi_Host *host;
	struct ata_port *ap;
	int rc;

	DPRINTK("ENTER\n");
	host = scsi_host_alloc(ent->sht, sizeof(struct ata_port));
	if (!host)
		return NULL;

	ap = (struct ata_port *) &host->hostdata[0];

	ata_host_init(ap, host, host_set, ent, port_no);

	rc = ap->ops->port_start(ap);
	if (rc)
		goto err_out;

	return ap;

err_out:
	scsi_host_put(host);
	return NULL;
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
	unsigned int count = 0, i;
	struct device *dev = ent->dev;
	struct ata_host_set *host_set;

	DPRINTK("ENTER\n");
	/* alloc a container for our list of ATA ports (buses) */
	host_set = kzalloc(sizeof(struct ata_host_set) +
			   (ent->n_ports * sizeof(void *)), GFP_KERNEL);
	if (!host_set)
		return 0;
	spin_lock_init(&host_set->lock);

	host_set->dev = dev;
	host_set->n_ports = ent->n_ports;
	host_set->irq = ent->irq;
	host_set->mmio_base = ent->mmio_base;
	host_set->private_data = ent->private_data;
	host_set->ops = ent->port_ops;

	/* register each port bound to this device */
	for (i = 0; i < ent->n_ports; i++) {
		struct ata_port *ap;
		unsigned long xfer_mode_mask;

		ap = ata_host_add(ent, host_set, i);
		if (!ap)
			goto err_out;

		host_set->ports[i] = ap;
		xfer_mode_mask =(ap->udma_mask << ATA_SHIFT_UDMA) |
				(ap->mwdma_mask << ATA_SHIFT_MWDMA) |
				(ap->pio_mask << ATA_SHIFT_PIO);

		/* print per-port info to dmesg */
		printk(KERN_INFO "ata%u: %cATA max %s cmd 0x%lX ctl 0x%lX "
				 "bmdma 0x%lX irq %lu\n",
			ap->id,
			ap->flags & ATA_FLAG_SATA ? 'S' : 'P',
			ata_mode_string(xfer_mode_mask),
	       		ap->ioaddr.cmd_addr,
	       		ap->ioaddr.ctl_addr,
	       		ap->ioaddr.bmdma_addr,
	       		ent->irq);

		ata_chk_status(ap);
		host_set->ops->irq_clear(ap);
		count++;
	}

	if (!count)
		goto err_free_ret;

	/* obtain irq, that is shared between channels */
	if (request_irq(ent->irq, ent->port_ops->irq_handler, ent->irq_flags,
			DRV_NAME, host_set))
		goto err_out;

	/* perform each probe synchronously */
	DPRINTK("probe begin\n");
	for (i = 0; i < count; i++) {
		struct ata_port *ap;
		int rc;

		ap = host_set->ports[i];

		DPRINTK("ata%u: probe begin\n", ap->id);
		rc = ata_bus_probe(ap);
		DPRINTK("ata%u: probe end\n", ap->id);

		if (rc) {
			/* FIXME: do something useful here?
			 * Current libata behavior will
			 * tear down everything when
			 * the module is removed
			 * or the h/w is unplugged.
			 */
		}

		rc = scsi_add_host(ap->host, dev);
		if (rc) {
			printk(KERN_ERR "ata%u: scsi_add_host failed\n",
			       ap->id);
			/* FIXME: do something useful here */
			/* FIXME: handle unconditional calls to
			 * scsi_scan_host and ata_host_remove, below,
			 * at the very least
			 */
		}
	}

	/* probes are done, now scan each port's disk(s) */
	DPRINTK("probe begin\n");
	for (i = 0; i < count; i++) {
		struct ata_port *ap = host_set->ports[i];

		ata_scsi_scan_host(ap);
	}

	dev_set_drvdata(dev, host_set);

	VPRINTK("EXIT, returning %u\n", ent->n_ports);
	return ent->n_ports; /* success */

err_out:
	for (i = 0; i < count; i++) {
		ata_host_remove(host_set->ports[i], 1);
		scsi_host_put(host_set->ports[i]->host);
	}
err_free_ret:
	kfree(host_set);
	VPRINTK("EXIT, returning 0\n");
	return 0;
}

/**
 *	ata_host_set_remove - PCI layer callback for device removal
 *	@host_set: ATA host set that was removed
 *
 *	Unregister all objects associated with this host set. Free those 
 *	objects.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 */

void ata_host_set_remove(struct ata_host_set *host_set)
{
	struct ata_port *ap;
	unsigned int i;

	for (i = 0; i < host_set->n_ports; i++) {
		ap = host_set->ports[i];
		scsi_remove_host(ap->host);
	}

	free_irq(host_set->irq, host_set);

	for (i = 0; i < host_set->n_ports; i++) {
		ap = host_set->ports[i];

		ata_scsi_release(ap->host);

		if ((ap->flags & ATA_FLAG_NO_LEGACY) == 0) {
			struct ata_ioports *ioaddr = &ap->ioaddr;

			if (ioaddr->cmd_addr == 0x1f0)
				release_region(0x1f0, 8);
			else if (ioaddr->cmd_addr == 0x170)
				release_region(0x170, 8);
		}

		scsi_host_put(ap->host);
	}

	if (host_set->ops->host_stop)
		host_set->ops->host_stop(host_set);

	kfree(host_set);
}

/**
 *	ata_scsi_release - SCSI layer callback hook for host unload
 *	@host: libata host to be unloaded
 *
 *	Performs all duties necessary to shut down a libata port...
 *	Kill port kthread, disable port, and release resources.
 *
 *	LOCKING:
 *	Inherited from SCSI layer.
 *
 *	RETURNS:
 *	One.
 */

int ata_scsi_release(struct Scsi_Host *host)
{
	struct ata_port *ap = (struct ata_port *) &host->hostdata[0];

	DPRINTK("ENTER\n");

	ap->ops->port_disable(ap);
	ata_host_remove(ap, 0);

	DPRINTK("EXIT\n");
	return 1;
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

static struct ata_probe_ent *
ata_probe_ent_alloc(struct device *dev, const struct ata_port_info *port)
{
	struct ata_probe_ent *probe_ent;

	probe_ent = kzalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (!probe_ent) {
		printk(KERN_ERR DRV_NAME "(%s): out of memory\n",
		       kobject_name(&(dev->kobj)));
		return NULL;
	}

	INIT_LIST_HEAD(&probe_ent->node);
	probe_ent->dev = dev;

	probe_ent->sht = port->sht;
	probe_ent->host_flags = port->host_flags;
	probe_ent->pio_mask = port->pio_mask;
	probe_ent->mwdma_mask = port->mwdma_mask;
	probe_ent->udma_mask = port->udma_mask;
	probe_ent->port_ops = port->port_ops;

	return probe_ent;
}



#ifdef CONFIG_PCI

void ata_pci_host_stop (struct ata_host_set *host_set)
{
	struct pci_dev *pdev = to_pci_dev(host_set->dev);

	pci_iounmap(pdev, host_set->mmio_base);
}

/**
 *	ata_pci_init_native_mode - Initialize native-mode driver
 *	@pdev:  pci device to be initialized
 *	@port:  array[2] of pointers to port info structures.
 *	@ports: bitmap of ports present
 *
 *	Utility function which allocates and initializes an
 *	ata_probe_ent structure for a standard dual-port
 *	PIO-based IDE controller.  The returned ata_probe_ent
 *	structure can be passed to ata_device_add().  The returned
 *	ata_probe_ent structure should then be freed with kfree().
 *
 *	The caller need only pass the address of the primary port, the
 *	secondary will be deduced automatically. If the device has non
 *	standard secondary port mappings this function can be called twice,
 *	once for each interface.
 */

struct ata_probe_ent *
ata_pci_init_native_mode(struct pci_dev *pdev, struct ata_port_info **port, int ports)
{
	struct ata_probe_ent *probe_ent =
		ata_probe_ent_alloc(pci_dev_to_dev(pdev), port[0]);
	int p = 0;

	if (!probe_ent)
		return NULL;

	probe_ent->irq = pdev->irq;
	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->private_data = port[0]->private_data;

	if (ports & ATA_PORT_PRIMARY) {
		probe_ent->port[p].cmd_addr = pci_resource_start(pdev, 0);
		probe_ent->port[p].altstatus_addr =
		probe_ent->port[p].ctl_addr =
			pci_resource_start(pdev, 1) | ATA_PCI_CTL_OFS;
		probe_ent->port[p].bmdma_addr = pci_resource_start(pdev, 4);
		ata_std_ports(&probe_ent->port[p]);
		p++;
	}

	if (ports & ATA_PORT_SECONDARY) {
		probe_ent->port[p].cmd_addr = pci_resource_start(pdev, 2);
		probe_ent->port[p].altstatus_addr =
		probe_ent->port[p].ctl_addr =
			pci_resource_start(pdev, 3) | ATA_PCI_CTL_OFS;
		probe_ent->port[p].bmdma_addr = pci_resource_start(pdev, 4) + 8;
		ata_std_ports(&probe_ent->port[p]);
		p++;
	}

	probe_ent->n_ports = p;
	return probe_ent;
}

static struct ata_probe_ent *ata_pci_init_legacy_port(struct pci_dev *pdev, struct ata_port_info *port, int port_num)
{
	struct ata_probe_ent *probe_ent;

	probe_ent = ata_probe_ent_alloc(pci_dev_to_dev(pdev), port);
	if (!probe_ent)
		return NULL;

	probe_ent->legacy_mode = 1;
	probe_ent->n_ports = 1;
	probe_ent->hard_port_no = port_num;
	probe_ent->private_data = port->private_data;

	switch(port_num)
	{
		case 0:
			probe_ent->irq = 14;
			probe_ent->port[0].cmd_addr = 0x1f0;
			probe_ent->port[0].altstatus_addr =
			probe_ent->port[0].ctl_addr = 0x3f6;
			break;
		case 1:
			probe_ent->irq = 15;
			probe_ent->port[0].cmd_addr = 0x170;
			probe_ent->port[0].altstatus_addr =
			probe_ent->port[0].ctl_addr = 0x376;
			break;
	}
	probe_ent->port[0].bmdma_addr = pci_resource_start(pdev, 4) + 8 * port_num;
	ata_std_ports(&probe_ent->port[0]);
	return probe_ent;
}

/**
 *	ata_pci_init_one - Initialize/register PCI IDE host controller
 *	@pdev: Controller to be initialized
 *	@port_info: Information from low-level host driver
 *	@n_ports: Number of ports attached to host controller
 *
 *	This is a helper function which can be called from a driver's
 *	xxx_init_one() probe function if the hardware uses traditional
 *	IDE taskfile registers.
 *
 *	This function calls pci_enable_device(), reserves its register
 *	regions, sets the dma mask, enables bus master mode, and calls
 *	ata_device_add()
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, negative on errno-based value on error.
 */

int ata_pci_init_one (struct pci_dev *pdev, struct ata_port_info **port_info,
		      unsigned int n_ports)
{
	struct ata_probe_ent *probe_ent = NULL, *probe_ent2 = NULL;
	struct ata_port_info *port[2];
	u8 tmp8, mask;
	unsigned int legacy_mode = 0;
	int disable_dev_on_err = 1;
	int rc;

	DPRINTK("ENTER\n");

	port[0] = port_info[0];
	if (n_ports > 1)
		port[1] = port_info[1];
	else
		port[1] = port[0];

	if ((port[0]->host_flags & ATA_FLAG_NO_LEGACY) == 0
	    && (pdev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
		/* TODO: What if one channel is in native mode ... */
		pci_read_config_byte(pdev, PCI_CLASS_PROG, &tmp8);
		mask = (1 << 2) | (1 << 0);
		if ((tmp8 & mask) != mask)
			legacy_mode = (1 << 3);
	}

	/* FIXME... */
	if ((!legacy_mode) && (n_ports > 2)) {
		printk(KERN_ERR "ata: BUG: native mode, n_ports > 2\n");
		n_ports = 2;
		/* For now */
	}

	/* FIXME: Really for ATA it isn't safe because the device may be
	   multi-purpose and we want to leave it alone if it was already
	   enabled. Secondly for shared use as Arjan says we want refcounting
	   
	   Checking dev->is_enabled is insufficient as this is not set at
	   boot for the primary video which is BIOS enabled
         */
         
	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		disable_dev_on_err = 0;
		goto err_out;
	}

	/* FIXME: Should use platform specific mappers for legacy port ranges */
	if (legacy_mode) {
		if (!request_region(0x1f0, 8, "libata")) {
			struct resource *conflict, res;
			res.start = 0x1f0;
			res.end = 0x1f0 + 8 - 1;
			conflict = ____request_resource(&ioport_resource, &res);
			if (!strcmp(conflict->name, "libata"))
				legacy_mode |= (1 << 0);
			else {
				disable_dev_on_err = 0;
				printk(KERN_WARNING "ata: 0x1f0 IDE port busy\n");
			}
		} else
			legacy_mode |= (1 << 0);

		if (!request_region(0x170, 8, "libata")) {
			struct resource *conflict, res;
			res.start = 0x170;
			res.end = 0x170 + 8 - 1;
			conflict = ____request_resource(&ioport_resource, &res);
			if (!strcmp(conflict->name, "libata"))
				legacy_mode |= (1 << 1);
			else {
				disable_dev_on_err = 0;
				printk(KERN_WARNING "ata: 0x170 IDE port busy\n");
			}
		} else
			legacy_mode |= (1 << 1);
	}

	/* we have legacy mode, but all ports are unavailable */
	if (legacy_mode == (1 << 3)) {
		rc = -EBUSY;
		goto err_out_regions;
	}

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;

	if (legacy_mode) {
		if (legacy_mode & (1 << 0))
			probe_ent = ata_pci_init_legacy_port(pdev, port[0], 0);
		if (legacy_mode & (1 << 1))
			probe_ent2 = ata_pci_init_legacy_port(pdev, port[1], 1);
	} else {
		if (n_ports == 2)
			probe_ent = ata_pci_init_native_mode(pdev, port, ATA_PORT_PRIMARY | ATA_PORT_SECONDARY);
		else
			probe_ent = ata_pci_init_native_mode(pdev, port, ATA_PORT_PRIMARY);
	}
	if (!probe_ent && !probe_ent2) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	pci_set_master(pdev);

	/* FIXME: check ata_device_add return */
	if (legacy_mode) {
		if (legacy_mode & (1 << 0))
			ata_device_add(probe_ent);
		if (legacy_mode & (1 << 1))
			ata_device_add(probe_ent2);
	} else
		ata_device_add(probe_ent);

	kfree(probe_ent);
	kfree(probe_ent2);

	return 0;

err_out_regions:
	if (legacy_mode & (1 << 0))
		release_region(0x1f0, 8);
	if (legacy_mode & (1 << 1))
		release_region(0x170, 8);
	pci_release_regions(pdev);
err_out:
	if (disable_dev_on_err)
		pci_disable_device(pdev);
	return rc;
}

/**
 *	ata_pci_remove_one - PCI layer callback for device removal
 *	@pdev: PCI device that was removed
 *
 *	PCI layer indicates to libata via this hook that
 *	hot-unplug or module unload event has occurred.
 *	Handle this by unregistering all objects associated
 *	with this PCI device.  Free those objects.  Then finally
 *	release PCI resources and disable device.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 */

void ata_pci_remove_one (struct pci_dev *pdev)
{
	struct device *dev = pci_dev_to_dev(pdev);
	struct ata_host_set *host_set = dev_get_drvdata(dev);

	ata_host_set_remove(host_set);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	dev_set_drvdata(dev, NULL);
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
#endif /* CONFIG_PCI */


static int __init ata_init(void)
{
	ata_wq = create_workqueue("ata");
	if (!ata_wq)
		return -ENOMEM;

	printk(KERN_DEBUG "libata version " DRV_VERSION " loaded.\n");
	return 0;
}

static void __exit ata_exit(void)
{
	destroy_workqueue(ata_wq);
}

module_init(ata_init);
module_exit(ata_exit);

static unsigned long ratelimit_time;
static spinlock_t ata_ratelimit_lock = SPIN_LOCK_UNLOCKED;

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

/*
 * libata is essentially a library of internal helper functions for
 * low-level ATA host controller drivers.  As such, the API/ABI is
 * likely to change as new drivers are added and updated.
 * Do not depend on ABI/API stability.
 */

EXPORT_SYMBOL_GPL(ata_std_bios_param);
EXPORT_SYMBOL_GPL(ata_std_ports);
EXPORT_SYMBOL_GPL(ata_device_add);
EXPORT_SYMBOL_GPL(ata_host_set_remove);
EXPORT_SYMBOL_GPL(ata_sg_init);
EXPORT_SYMBOL_GPL(ata_sg_init_one);
EXPORT_SYMBOL_GPL(ata_qc_complete);
EXPORT_SYMBOL_GPL(ata_qc_issue_prot);
EXPORT_SYMBOL_GPL(ata_eng_timeout);
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
EXPORT_SYMBOL_GPL(ata_port_stop);
EXPORT_SYMBOL_GPL(ata_host_stop);
EXPORT_SYMBOL_GPL(ata_interrupt);
EXPORT_SYMBOL_GPL(ata_qc_prep);
EXPORT_SYMBOL_GPL(ata_bmdma_setup);
EXPORT_SYMBOL_GPL(ata_bmdma_start);
EXPORT_SYMBOL_GPL(ata_bmdma_irq_clear);
EXPORT_SYMBOL_GPL(ata_bmdma_status);
EXPORT_SYMBOL_GPL(ata_bmdma_stop);
EXPORT_SYMBOL_GPL(ata_port_probe);
EXPORT_SYMBOL_GPL(sata_phy_reset);
EXPORT_SYMBOL_GPL(__sata_phy_reset);
EXPORT_SYMBOL_GPL(ata_bus_reset);
EXPORT_SYMBOL_GPL(ata_port_disable);
EXPORT_SYMBOL_GPL(ata_ratelimit);
EXPORT_SYMBOL_GPL(ata_scsi_ioctl);
EXPORT_SYMBOL_GPL(ata_scsi_queuecmd);
EXPORT_SYMBOL_GPL(ata_scsi_error);
EXPORT_SYMBOL_GPL(ata_scsi_slave_config);
EXPORT_SYMBOL_GPL(ata_scsi_release);
EXPORT_SYMBOL_GPL(ata_host_intr);
EXPORT_SYMBOL_GPL(ata_dev_classify);
EXPORT_SYMBOL_GPL(ata_dev_id_string);
EXPORT_SYMBOL_GPL(ata_dev_config);
EXPORT_SYMBOL_GPL(ata_scsi_simulate);

EXPORT_SYMBOL_GPL(ata_timing_compute);
EXPORT_SYMBOL_GPL(ata_timing_merge);

#ifdef CONFIG_PCI
EXPORT_SYMBOL_GPL(pci_test_config_bits);
EXPORT_SYMBOL_GPL(ata_pci_host_stop);
EXPORT_SYMBOL_GPL(ata_pci_init_native_mode);
EXPORT_SYMBOL_GPL(ata_pci_init_one);
EXPORT_SYMBOL_GPL(ata_pci_remove_one);
#endif /* CONFIG_PCI */
