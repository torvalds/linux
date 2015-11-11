/*
 * Support for IDE interfaces on Celleb platform
 *
 * (C) Copyright 2006 TOSHIBA CORPORATION
 *
 * This code is based on drivers/ata/ata_piix.c:
 *  Copyright 2003-2005 Red Hat Inc
 *  Copyright 2003-2005 Jeff Garzik
 *  Copyright (C) 1998-1999 Andrzej Krzysztofowicz, Author and Maintainer
 *  Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2003 Red Hat Inc
 *
 * and drivers/ata/ahci.c:
 *  Copyright 2004-2005 Red Hat, Inc.
 *
 * and drivers/ata/libata-core.c:
 *  Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
 *  Copyright 2003-2004 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME		"pata_scc"
#define DRV_VERSION		"0.3"

#define PCI_DEVICE_ID_TOSHIBA_SCC_ATA		0x01b4

/* PCI BARs */
#define SCC_CTRL_BAR		0
#define SCC_BMID_BAR		1

/* offset of CTRL registers */
#define SCC_CTL_PIOSHT		0x000
#define SCC_CTL_PIOCT		0x004
#define SCC_CTL_MDMACT		0x008
#define SCC_CTL_MCRCST		0x00C
#define SCC_CTL_SDMACT		0x010
#define SCC_CTL_SCRCST		0x014
#define SCC_CTL_UDENVT		0x018
#define SCC_CTL_TDVHSEL 	0x020
#define SCC_CTL_MODEREG 	0x024
#define SCC_CTL_ECMODE		0xF00
#define SCC_CTL_MAEA0		0xF50
#define SCC_CTL_MAEC0		0xF54
#define SCC_CTL_CCKCTRL 	0xFF0

/* offset of BMID registers */
#define SCC_DMA_CMD		0x000
#define SCC_DMA_STATUS		0x004
#define SCC_DMA_TABLE_OFS	0x008
#define SCC_DMA_INTMASK 	0x010
#define SCC_DMA_INTST		0x014
#define SCC_DMA_PTERADD 	0x018
#define SCC_REG_CMD_ADDR	0x020
#define SCC_REG_DATA		0x000
#define SCC_REG_ERR		0x004
#define SCC_REG_FEATURE 	0x004
#define SCC_REG_NSECT		0x008
#define SCC_REG_LBAL		0x00C
#define SCC_REG_LBAM		0x010
#define SCC_REG_LBAH		0x014
#define SCC_REG_DEVICE		0x018
#define SCC_REG_STATUS		0x01C
#define SCC_REG_CMD		0x01C
#define SCC_REG_ALTSTATUS	0x020

/* register value */
#define TDVHSEL_MASTER		0x00000001
#define TDVHSEL_SLAVE		0x00000004

#define MODE_JCUSFEN		0x00000080

#define ECMODE_VALUE		0x01

#define CCKCTRL_ATARESET	0x00040000
#define CCKCTRL_BUFCNT		0x00020000
#define CCKCTRL_CRST		0x00010000
#define CCKCTRL_OCLKEN		0x00000100
#define CCKCTRL_ATACLKOEN	0x00000002
#define CCKCTRL_LCLKEN		0x00000001

#define QCHCD_IOS_SS		0x00000001

#define QCHSD_STPDIAG		0x00020000

#define INTMASK_MSK		0xD1000012
#define INTSTS_SERROR		0x80000000
#define INTSTS_PRERR		0x40000000
#define INTSTS_RERR		0x10000000
#define INTSTS_ICERR		0x01000000
#define INTSTS_BMSINT		0x00000010
#define INTSTS_BMHE		0x00000008
#define INTSTS_IOIRQS		0x00000004
#define INTSTS_INTRQ		0x00000002
#define INTSTS_ACTEINT		0x00000001


/* PIO transfer mode table */
/* JCHST */
static const unsigned long JCHSTtbl[2][7] = {
	{0x0E, 0x05, 0x02, 0x03, 0x02, 0x00, 0x00},	/* 100MHz */
	{0x13, 0x07, 0x04, 0x04, 0x03, 0x00, 0x00}	/* 133MHz */
};

/* JCHHT */
static const unsigned long JCHHTtbl[2][7] = {
	{0x0E, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00},	/* 100MHz */
	{0x13, 0x03, 0x03, 0x03, 0x03, 0x00, 0x00}	/* 133MHz */
};

/* JCHCT */
static const unsigned long JCHCTtbl[2][7] = {
	{0x1D, 0x1D, 0x1C, 0x0B, 0x06, 0x00, 0x00},	/* 100MHz */
	{0x27, 0x26, 0x26, 0x0E, 0x09, 0x00, 0x00}	/* 133MHz */
};

/* DMA transfer mode  table */
/* JCHDCTM/JCHDCTS */
static const unsigned long JCHDCTxtbl[2][7] = {
	{0x0A, 0x06, 0x04, 0x03, 0x01, 0x00, 0x00},	/* 100MHz */
	{0x0E, 0x09, 0x06, 0x04, 0x02, 0x01, 0x00}	/* 133MHz */
};

/* JCSTWTM/JCSTWTS  */
static const unsigned long JCSTWTxtbl[2][7] = {
	{0x06, 0x04, 0x03, 0x02, 0x02, 0x02, 0x00},	/* 100MHz */
	{0x09, 0x06, 0x04, 0x02, 0x02, 0x02, 0x02}	/* 133MHz */
};

/* JCTSS */
static const unsigned long JCTSStbl[2][7] = {
	{0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x00},	/* 100MHz */
	{0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05}	/* 133MHz */
};

/* JCENVT */
static const unsigned long JCENVTtbl[2][7] = {
	{0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00},	/* 100MHz */
	{0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02}	/* 133MHz */
};

/* JCACTSELS/JCACTSELM */
static const unsigned long JCACTSELtbl[2][7] = {
	{0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00},	/* 100MHz */
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}	/* 133MHz */
};

static const struct pci_device_id scc_pci_tbl[] = {
	{ PCI_VDEVICE(TOSHIBA_2, PCI_DEVICE_ID_TOSHIBA_SCC_ATA), 0},
	{ }	/* terminate list */
};

/**
 *	scc_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: um
 *
 *	Set PIO mode for device.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void scc_set_piomode (struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio = adev->pio_mode - XFER_PIO_0;
	void __iomem *ctrl_base = ap->host->iomap[SCC_CTRL_BAR];
	void __iomem *cckctrl_port = ctrl_base + SCC_CTL_CCKCTRL;
	void __iomem *piosht_port = ctrl_base + SCC_CTL_PIOSHT;
	void __iomem *pioct_port = ctrl_base + SCC_CTL_PIOCT;
	unsigned long reg;
	int offset;

	reg = in_be32(cckctrl_port);
	if (reg & CCKCTRL_ATACLKOEN)
		offset = 1;	/* 133MHz */
	else
		offset = 0;	/* 100MHz */

	reg = JCHSTtbl[offset][pio] << 16 | JCHHTtbl[offset][pio];
	out_be32(piosht_port, reg);
	reg = JCHCTtbl[offset][pio];
	out_be32(pioct_port, reg);
}

/**
 *	scc_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: um
 *
 *	Set UDMA mode for device.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void scc_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	unsigned int udma = adev->dma_mode;
	unsigned int is_slave = (adev->devno != 0);
	u8 speed = udma;
	void __iomem *ctrl_base = ap->host->iomap[SCC_CTRL_BAR];
	void __iomem *cckctrl_port = ctrl_base + SCC_CTL_CCKCTRL;
	void __iomem *mdmact_port = ctrl_base + SCC_CTL_MDMACT;
	void __iomem *mcrcst_port = ctrl_base + SCC_CTL_MCRCST;
	void __iomem *sdmact_port = ctrl_base + SCC_CTL_SDMACT;
	void __iomem *scrcst_port = ctrl_base + SCC_CTL_SCRCST;
	void __iomem *udenvt_port = ctrl_base + SCC_CTL_UDENVT;
	void __iomem *tdvhsel_port = ctrl_base + SCC_CTL_TDVHSEL;
	int offset, idx;

	if (in_be32(cckctrl_port) & CCKCTRL_ATACLKOEN)
		offset = 1;	/* 133MHz */
	else
		offset = 0;	/* 100MHz */

	if (speed >= XFER_UDMA_0)
		idx = speed - XFER_UDMA_0;
	else
		return;

	if (is_slave) {
		out_be32(sdmact_port, JCHDCTxtbl[offset][idx]);
		out_be32(scrcst_port, JCSTWTxtbl[offset][idx]);
		out_be32(tdvhsel_port,
			 (in_be32(tdvhsel_port) & ~TDVHSEL_SLAVE) | (JCACTSELtbl[offset][idx] << 2));
	} else {
		out_be32(mdmact_port, JCHDCTxtbl[offset][idx]);
		out_be32(mcrcst_port, JCSTWTxtbl[offset][idx]);
		out_be32(tdvhsel_port,
			 (in_be32(tdvhsel_port) & ~TDVHSEL_MASTER) | JCACTSELtbl[offset][idx]);
	}
	out_be32(udenvt_port,
		 JCTSStbl[offset][idx] << 16 | JCENVTtbl[offset][idx]);
}

unsigned long scc_mode_filter(struct ata_device *adev, unsigned long mask)
{
	/* errata A308 workaround: limit ATAPI UDMA mode to UDMA4 */
	if (adev->class == ATA_DEV_ATAPI &&
	    (mask & (0xE0 << ATA_SHIFT_UDMA))) {
		printk(KERN_INFO "%s: limit ATAPI UDMA to UDMA4\n", DRV_NAME);
		mask &= ~(0xE0 << ATA_SHIFT_UDMA);
	}
	return mask;
}

/**
 *	scc_tf_load - send taskfile registers to host controller
 *	@ap: Port to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Note: Original code is ata_sff_tf_load().
 */

static void scc_tf_load (struct ata_port *ap, const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		out_be32(ioaddr->ctl_addr, tf->ctl);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		out_be32(ioaddr->feature_addr, tf->hob_feature);
		out_be32(ioaddr->nsect_addr, tf->hob_nsect);
		out_be32(ioaddr->lbal_addr, tf->hob_lbal);
		out_be32(ioaddr->lbam_addr, tf->hob_lbam);
		out_be32(ioaddr->lbah_addr, tf->hob_lbah);
		VPRINTK("hob: feat 0x%X nsect 0x%X, lba 0x%X 0x%X 0x%X\n",
			tf->hob_feature,
			tf->hob_nsect,
			tf->hob_lbal,
			tf->hob_lbam,
			tf->hob_lbah);
	}

	if (is_addr) {
		out_be32(ioaddr->feature_addr, tf->feature);
		out_be32(ioaddr->nsect_addr, tf->nsect);
		out_be32(ioaddr->lbal_addr, tf->lbal);
		out_be32(ioaddr->lbam_addr, tf->lbam);
		out_be32(ioaddr->lbah_addr, tf->lbah);
		VPRINTK("feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		out_be32(ioaddr->device_addr, tf->device);
		VPRINTK("device 0x%X\n", tf->device);
	}

	ata_wait_idle(ap);
}

/**
 *	scc_check_status - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Note: Original code is ata_check_status().
 */

static u8 scc_check_status (struct ata_port *ap)
{
	return in_be32(ap->ioaddr.status_addr);
}

/**
 *	scc_tf_read - input device's ATA taskfile shadow registers
 *	@ap: Port from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Note: Original code is ata_sff_tf_read().
 */

static void scc_tf_read (struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->command = scc_check_status(ap);
	tf->feature = in_be32(ioaddr->error_addr);
	tf->nsect = in_be32(ioaddr->nsect_addr);
	tf->lbal = in_be32(ioaddr->lbal_addr);
	tf->lbam = in_be32(ioaddr->lbam_addr);
	tf->lbah = in_be32(ioaddr->lbah_addr);
	tf->device = in_be32(ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		out_be32(ioaddr->ctl_addr, tf->ctl | ATA_HOB);
		tf->hob_feature = in_be32(ioaddr->error_addr);
		tf->hob_nsect = in_be32(ioaddr->nsect_addr);
		tf->hob_lbal = in_be32(ioaddr->lbal_addr);
		tf->hob_lbam = in_be32(ioaddr->lbam_addr);
		tf->hob_lbah = in_be32(ioaddr->lbah_addr);
		out_be32(ioaddr->ctl_addr, tf->ctl);
		ap->last_ctl = tf->ctl;
	}
}

/**
 *	scc_exec_command - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Note: Original code is ata_sff_exec_command().
 */

static void scc_exec_command (struct ata_port *ap,
			      const struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->print_id, tf->command);

	out_be32(ap->ioaddr.command_addr, tf->command);
	ata_sff_pause(ap);
}

/**
 *	scc_check_altstatus - Read device alternate status reg
 *	@ap: port where the device is
 */

static u8 scc_check_altstatus (struct ata_port *ap)
{
	return in_be32(ap->ioaddr.altstatus_addr);
}

/**
 *	scc_dev_select - Select device 0/1 on ATA bus
 *	@ap: ATA channel to manipulate
 *	@device: ATA device (numbered from zero) to select
 *
 *	Note: Original code is ata_sff_dev_select().
 */

static void scc_dev_select (struct ata_port *ap, unsigned int device)
{
	u8 tmp;

	if (device == 0)
		tmp = ATA_DEVICE_OBS;
	else
		tmp = ATA_DEVICE_OBS | ATA_DEV1;

	out_be32(ap->ioaddr.device_addr, tmp);
	ata_sff_pause(ap);
}

/**
 *	scc_set_devctl - Write device control reg
 *	@ap: port where the device is
 *	@ctl: value to write
 */

static void scc_set_devctl(struct ata_port *ap, u8 ctl)
{
	out_be32(ap->ioaddr.ctl_addr, ctl);
}

/**
 *	scc_bmdma_setup - Set up PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	Note: Original code is ata_bmdma_setup().
 */

static void scc_bmdma_setup (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	u8 dmactl;
	void __iomem *mmio = ap->ioaddr.bmdma_addr;

	/* load PRD table addr */
	out_be32(mmio + SCC_DMA_TABLE_OFS, ap->bmdma_prd_dma);

	/* specify data direction, triple-check start bit is clear */
	dmactl = in_be32(mmio + SCC_DMA_CMD);
	dmactl &= ~(ATA_DMA_WR | ATA_DMA_START);
	if (!rw)
		dmactl |= ATA_DMA_WR;
	out_be32(mmio + SCC_DMA_CMD, dmactl);

	/* issue r/w command */
	ap->ops->sff_exec_command(ap, &qc->tf);
}

/**
 *	scc_bmdma_start - Start a PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	Note: Original code is ata_bmdma_start().
 */

static void scc_bmdma_start (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	u8 dmactl;
	void __iomem *mmio = ap->ioaddr.bmdma_addr;

	/* start host DMA transaction */
	dmactl = in_be32(mmio + SCC_DMA_CMD);
	out_be32(mmio + SCC_DMA_CMD, dmactl | ATA_DMA_START);
}

/**
 *	scc_devchk - PATA device presence detection
 *	@ap: ATA channel to examine
 *	@device: Device to examine (starting at zero)
 *
 *	Note: Original code is ata_devchk().
 */

static unsigned int scc_devchk (struct ata_port *ap,
				unsigned int device)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 nsect, lbal;

	ap->ops->sff_dev_select(ap, device);

	out_be32(ioaddr->nsect_addr, 0x55);
	out_be32(ioaddr->lbal_addr, 0xaa);

	out_be32(ioaddr->nsect_addr, 0xaa);
	out_be32(ioaddr->lbal_addr, 0x55);

	out_be32(ioaddr->nsect_addr, 0x55);
	out_be32(ioaddr->lbal_addr, 0xaa);

	nsect = in_be32(ioaddr->nsect_addr);
	lbal = in_be32(ioaddr->lbal_addr);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return 1;	/* we found a device */

	return 0;		/* nothing found */
}

/**
 *	scc_wait_after_reset - wait for devices to become ready after reset
 *
 *	Note: Original code is ata_sff_wait_after_reset
 */

static int scc_wait_after_reset(struct ata_link *link, unsigned int devmask,
				unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int dev0 = devmask & (1 << 0);
	unsigned int dev1 = devmask & (1 << 1);
	int rc, ret = 0;

	/* Spec mandates ">= 2ms" before checking status.  We wait
	 * 150ms, because that was the magic delay used for ATAPI
	 * devices in Hale Landis's ATADRVR, for the period of time
	 * between when the ATA command register is written, and then
	 * status is checked.  Because waiting for "a while" before
	 * checking status is fine, post SRST, we perform this magic
	 * delay here as well.
	 *
	 * Old drivers/ide uses the 2mS rule and then waits for ready.
	 */
	ata_msleep(ap, 150);

	/* always check readiness of the master device */
	rc = ata_sff_wait_ready(link, deadline);
	/* -ENODEV means the odd clown forgot the D7 pulldown resistor
	 * and TF status is 0xff, bail out on it too.
	 */
	if (rc)
		return rc;

	/* if device 1 was found in ata_devchk, wait for register
	 * access briefly, then wait for BSY to clear.
	 */
	if (dev1) {
		int i;

		ap->ops->sff_dev_select(ap, 1);

		/* Wait for register access.  Some ATAPI devices fail
		 * to set nsect/lbal after reset, so don't waste too
		 * much time on it.  We're gonna wait for !BSY anyway.
		 */
		for (i = 0; i < 2; i++) {
			u8 nsect, lbal;

			nsect = in_be32(ioaddr->nsect_addr);
			lbal = in_be32(ioaddr->lbal_addr);
			if ((nsect == 1) && (lbal == 1))
				break;
			ata_msleep(ap, 50);	/* give drive a breather */
		}

		rc = ata_sff_wait_ready(link, deadline);
		if (rc) {
			if (rc != -ENODEV)
				return rc;
			ret = rc;
		}
	}

	/* is all this really necessary? */
	ap->ops->sff_dev_select(ap, 0);
	if (dev1)
		ap->ops->sff_dev_select(ap, 1);
	if (dev0)
		ap->ops->sff_dev_select(ap, 0);

	return ret;
}

/**
 *	scc_bus_softreset - PATA device software reset
 *
 *	Note: Original code is ata_bus_softreset().
 */

static unsigned int scc_bus_softreset(struct ata_port *ap, unsigned int devmask,
                                      unsigned long deadline)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	DPRINTK("ata%u: bus reset via SRST\n", ap->print_id);

	/* software reset.  causes dev0 to be selected */
	out_be32(ioaddr->ctl_addr, ap->ctl);
	udelay(20);
	out_be32(ioaddr->ctl_addr, ap->ctl | ATA_SRST);
	udelay(20);
	out_be32(ioaddr->ctl_addr, ap->ctl);

	scc_wait_after_reset(&ap->link, devmask, deadline);

	return 0;
}

/**
 *	scc_softreset - reset host port via ATA SRST
 *	@ap: port to reset
 *	@classes: resulting classes of attached devices
 *	@deadline: deadline jiffies for the operation
 *
 *	Note: Original code is ata_sff_softreset().
 */

static int scc_softreset(struct ata_link *link, unsigned int *classes,
			 unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	unsigned int slave_possible = ap->flags & ATA_FLAG_SLAVE_POSS;
	unsigned int devmask = 0, err_mask;
	u8 err;

	DPRINTK("ENTER\n");

	/* determine if device 0/1 are present */
	if (scc_devchk(ap, 0))
		devmask |= (1 << 0);
	if (slave_possible && scc_devchk(ap, 1))
		devmask |= (1 << 1);

	/* select device 0 again */
	ap->ops->sff_dev_select(ap, 0);

	/* issue bus reset */
	DPRINTK("about to softreset, devmask=%x\n", devmask);
	err_mask = scc_bus_softreset(ap, devmask, deadline);
	if (err_mask) {
		ata_port_err(ap, "SRST failed (err_mask=0x%x)\n", err_mask);
		return -EIO;
	}

	/* determine by signature whether we have ATA or ATAPI devices */
	classes[0] = ata_sff_dev_classify(&ap->link.device[0],
					  devmask & (1 << 0), &err);
	if (slave_possible && err != 0x81)
		classes[1] = ata_sff_dev_classify(&ap->link.device[1],
						  devmask & (1 << 1), &err);

	DPRINTK("EXIT, classes[0]=%u [1]=%u\n", classes[0], classes[1]);
	return 0;
}

/**
 *	scc_bmdma_stop - Stop PCI IDE BMDMA transfer
 *	@qc: Command we are ending DMA for
 */

static void scc_bmdma_stop (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *ctrl_base = ap->host->iomap[SCC_CTRL_BAR];
	void __iomem *bmid_base = ap->host->iomap[SCC_BMID_BAR];
	u32 reg;

	while (1) {
		reg = in_be32(bmid_base + SCC_DMA_INTST);

		if (reg & INTSTS_SERROR) {
			printk(KERN_WARNING "%s: SERROR\n", DRV_NAME);
			out_be32(bmid_base + SCC_DMA_INTST, INTSTS_SERROR|INTSTS_BMSINT);
			out_be32(bmid_base + SCC_DMA_CMD,
				 in_be32(bmid_base + SCC_DMA_CMD) & ~ATA_DMA_START);
			continue;
		}

		if (reg & INTSTS_PRERR) {
			u32 maea0, maec0;
			maea0 = in_be32(ctrl_base + SCC_CTL_MAEA0);
			maec0 = in_be32(ctrl_base + SCC_CTL_MAEC0);
			printk(KERN_WARNING "%s: PRERR [addr:%x cmd:%x]\n", DRV_NAME, maea0, maec0);
			out_be32(bmid_base + SCC_DMA_INTST, INTSTS_PRERR|INTSTS_BMSINT);
			out_be32(bmid_base + SCC_DMA_CMD,
				 in_be32(bmid_base + SCC_DMA_CMD) & ~ATA_DMA_START);
			continue;
		}

		if (reg & INTSTS_RERR) {
			printk(KERN_WARNING "%s: Response Error\n", DRV_NAME);
			out_be32(bmid_base + SCC_DMA_INTST, INTSTS_RERR|INTSTS_BMSINT);
			out_be32(bmid_base + SCC_DMA_CMD,
				 in_be32(bmid_base + SCC_DMA_CMD) & ~ATA_DMA_START);
			continue;
		}

		if (reg & INTSTS_ICERR) {
			out_be32(bmid_base + SCC_DMA_CMD,
				 in_be32(bmid_base + SCC_DMA_CMD) & ~ATA_DMA_START);
			printk(KERN_WARNING "%s: Illegal Configuration\n", DRV_NAME);
			out_be32(bmid_base + SCC_DMA_INTST, INTSTS_ICERR|INTSTS_BMSINT);
			continue;
		}

		if (reg & INTSTS_BMSINT) {
			unsigned int classes;
			unsigned long deadline = ata_deadline(jiffies, ATA_TMOUT_BOOT);
			printk(KERN_WARNING "%s: Internal Bus Error\n", DRV_NAME);
			out_be32(bmid_base + SCC_DMA_INTST, INTSTS_BMSINT);
			/* TBD: SW reset */
			scc_softreset(&ap->link, &classes, deadline);
			continue;
		}

		if (reg & INTSTS_BMHE) {
			out_be32(bmid_base + SCC_DMA_INTST, INTSTS_BMHE);
			continue;
		}

		if (reg & INTSTS_ACTEINT) {
			out_be32(bmid_base + SCC_DMA_INTST, INTSTS_ACTEINT);
			continue;
		}

		if (reg & INTSTS_IOIRQS) {
			out_be32(bmid_base + SCC_DMA_INTST, INTSTS_IOIRQS);
			continue;
		}
		break;
	}

	/* clear start/stop bit */
	out_be32(bmid_base + SCC_DMA_CMD,
		 in_be32(bmid_base + SCC_DMA_CMD) & ~ATA_DMA_START);

	/* one-PIO-cycle guaranteed wait, per spec, for HDMA1:0 transition */
	ata_sff_dma_pause(ap);	/* dummy read */
}

/**
 *	scc_bmdma_status - Read PCI IDE BMDMA status
 *	@ap: Port associated with this ATA transaction.
 */

static u8 scc_bmdma_status (struct ata_port *ap)
{
	void __iomem *mmio = ap->ioaddr.bmdma_addr;
	u8 host_stat = in_be32(mmio + SCC_DMA_STATUS);
	u32 int_status = in_be32(mmio + SCC_DMA_INTST);
	struct ata_queued_cmd *qc = ata_qc_from_tag(ap, ap->link.active_tag);
	static int retry = 0;

	/* return if IOS_SS is cleared */
	if (!(in_be32(mmio + SCC_DMA_CMD) & ATA_DMA_START))
		return host_stat;

	/* errata A252,A308 workaround: Step4 */
	if ((scc_check_altstatus(ap) & ATA_ERR)
					&& (int_status & INTSTS_INTRQ))
		return (host_stat | ATA_DMA_INTR);

	/* errata A308 workaround Step5 */
	if (int_status & INTSTS_IOIRQS) {
		host_stat |= ATA_DMA_INTR;

		/* We don't check ATAPI DMA because it is limited to UDMA4 */
		if ((qc->tf.protocol == ATA_PROT_DMA &&
		     qc->dev->xfer_mode > XFER_UDMA_4)) {
			if (!(int_status & INTSTS_ACTEINT)) {
				printk(KERN_WARNING "ata%u: operation failed (transfer data loss)\n",
				       ap->print_id);
				host_stat |= ATA_DMA_ERR;
				if (retry++)
					ap->udma_mask &= ~(1 << qc->dev->xfer_mode);
			} else
				retry = 0;
		}
	}

	return host_stat;
}

/**
 *	scc_data_xfer - Transfer data by PIO
 *	@dev: device for this I/O
 *	@buf: data buffer
 *	@buflen: buffer length
 *	@rw: read/write
 *
 *	Note: Original code is ata_sff_data_xfer().
 */

static unsigned int scc_data_xfer (struct ata_device *dev, unsigned char *buf,
				   unsigned int buflen, int rw)
{
	struct ata_port *ap = dev->link->ap;
	unsigned int words = buflen >> 1;
	unsigned int i;
	__le16 *buf16 = (__le16 *) buf;
	void __iomem *mmio = ap->ioaddr.data_addr;

	/* Transfer multiple of 2 bytes */
	if (rw == READ)
		for (i = 0; i < words; i++)
			buf16[i] = cpu_to_le16(in_be32(mmio));
	else
		for (i = 0; i < words; i++)
			out_be32(mmio, le16_to_cpu(buf16[i]));

	/* Transfer trailing 1 byte, if any. */
	if (unlikely(buflen & 0x01)) {
		__le16 align_buf[1] = { 0 };
		unsigned char *trailing_buf = buf + buflen - 1;

		if (rw == READ) {
			align_buf[0] = cpu_to_le16(in_be32(mmio));
			memcpy(trailing_buf, align_buf, 1);
		} else {
			memcpy(align_buf, trailing_buf, 1);
			out_be32(mmio, le16_to_cpu(align_buf[0]));
		}
		words++;
	}

	return words << 1;
}

/**
 *	scc_postreset - standard postreset callback
 *	@ap: the target ata_port
 *	@classes: classes of attached devices
 *
 *	Note: Original code is ata_sff_postreset().
 */

static void scc_postreset(struct ata_link *link, unsigned int *classes)
{
	struct ata_port *ap = link->ap;

	DPRINTK("ENTER\n");

	/* is double-select really necessary? */
	if (classes[0] != ATA_DEV_NONE)
		ap->ops->sff_dev_select(ap, 1);
	if (classes[1] != ATA_DEV_NONE)
		ap->ops->sff_dev_select(ap, 0);

	/* bail out if no device is present */
	if (classes[0] == ATA_DEV_NONE && classes[1] == ATA_DEV_NONE) {
		DPRINTK("EXIT, no device\n");
		return;
	}

	/* set up device control */
	out_be32(ap->ioaddr.ctl_addr, ap->ctl);

	DPRINTK("EXIT\n");
}

/**
 *	scc_irq_clear - Clear PCI IDE BMDMA interrupt.
 *	@ap: Port associated with this ATA transaction.
 *
 *	Note: Original code is ata_bmdma_irq_clear().
 */

static void scc_irq_clear (struct ata_port *ap)
{
	void __iomem *mmio = ap->ioaddr.bmdma_addr;

	if (!mmio)
		return;

	out_be32(mmio + SCC_DMA_STATUS, in_be32(mmio + SCC_DMA_STATUS));
}

/**
 *	scc_port_start - Set port up for dma.
 *	@ap: Port to initialize
 *
 *	Allocate space for PRD table using ata_bmdma_port_start().
 *	Set PRD table address for PTERADD. (PRD Transfer End Read)
 */

static int scc_port_start (struct ata_port *ap)
{
	void __iomem *mmio = ap->ioaddr.bmdma_addr;
	int rc;

	rc = ata_bmdma_port_start(ap);
	if (rc)
		return rc;

	out_be32(mmio + SCC_DMA_PTERADD, ap->bmdma_prd_dma);
	return 0;
}

/**
 *	scc_port_stop - Undo scc_port_start()
 *	@ap: Port to shut down
 *
 *	Reset PTERADD.
 */

static void scc_port_stop (struct ata_port *ap)
{
	void __iomem *mmio = ap->ioaddr.bmdma_addr;

	out_be32(mmio + SCC_DMA_PTERADD, 0);
}

static struct scsi_host_template scc_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations scc_pata_ops = {
	.inherits		= &ata_bmdma_port_ops,

	.set_piomode		= scc_set_piomode,
	.set_dmamode		= scc_set_dmamode,
	.mode_filter		= scc_mode_filter,

	.sff_tf_load		= scc_tf_load,
	.sff_tf_read		= scc_tf_read,
	.sff_exec_command	= scc_exec_command,
	.sff_check_status	= scc_check_status,
	.sff_check_altstatus	= scc_check_altstatus,
	.sff_dev_select		= scc_dev_select,
	.sff_set_devctl		= scc_set_devctl,

	.bmdma_setup		= scc_bmdma_setup,
	.bmdma_start		= scc_bmdma_start,
	.bmdma_stop		= scc_bmdma_stop,
	.bmdma_status		= scc_bmdma_status,
	.sff_data_xfer		= scc_data_xfer,

	.cable_detect		= ata_cable_80wire,
	.softreset		= scc_softreset,
	.postreset		= scc_postreset,

	.sff_irq_clear		= scc_irq_clear,

	.port_start		= scc_port_start,
	.port_stop		= scc_port_stop,
};

static struct ata_port_info scc_port_info[] = {
	{
		.flags		= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= ATA_PIO4,
		/* No MWDMA */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &scc_pata_ops,
	},
};

/**
 *	scc_reset_controller - initialize SCC PATA controller.
 */

static int scc_reset_controller(struct ata_host *host)
{
	void __iomem *ctrl_base = host->iomap[SCC_CTRL_BAR];
	void __iomem *bmid_base = host->iomap[SCC_BMID_BAR];
	void __iomem *cckctrl_port = ctrl_base + SCC_CTL_CCKCTRL;
	void __iomem *mode_port = ctrl_base + SCC_CTL_MODEREG;
	void __iomem *ecmode_port = ctrl_base + SCC_CTL_ECMODE;
	void __iomem *intmask_port = bmid_base + SCC_DMA_INTMASK;
	void __iomem *dmastatus_port = bmid_base + SCC_DMA_STATUS;
	u32 reg = 0;

	out_be32(cckctrl_port, reg);
	reg |= CCKCTRL_ATACLKOEN;
	out_be32(cckctrl_port, reg);
	reg |= CCKCTRL_LCLKEN | CCKCTRL_OCLKEN;
	out_be32(cckctrl_port, reg);
	reg |= CCKCTRL_CRST;
	out_be32(cckctrl_port, reg);

	for (;;) {
		reg = in_be32(cckctrl_port);
		if (reg & CCKCTRL_CRST)
			break;
		udelay(5000);
	}

	reg |= CCKCTRL_ATARESET;
	out_be32(cckctrl_port, reg);
	out_be32(ecmode_port, ECMODE_VALUE);
	out_be32(mode_port, MODE_JCUSFEN);
	out_be32(intmask_port, INTMASK_MSK);

	if (in_be32(dmastatus_port) & QCHSD_STPDIAG) {
		printk(KERN_WARNING "%s: failed to detect 80c cable. (PDIAG# is high)\n", DRV_NAME);
		return -EIO;
	}

	return 0;
}

/**
 *	scc_setup_ports - initialize ioaddr with SCC PATA port offsets.
 *	@ioaddr: IO address structure to be initialized
 *	@base: base address of BMID region
 */

static void scc_setup_ports (struct ata_ioports *ioaddr, void __iomem *base)
{
	ioaddr->cmd_addr = base + SCC_REG_CMD_ADDR;
	ioaddr->altstatus_addr = ioaddr->cmd_addr + SCC_REG_ALTSTATUS;
	ioaddr->ctl_addr = ioaddr->cmd_addr + SCC_REG_ALTSTATUS;
	ioaddr->bmdma_addr = base;
	ioaddr->data_addr = ioaddr->cmd_addr + SCC_REG_DATA;
	ioaddr->error_addr = ioaddr->cmd_addr + SCC_REG_ERR;
	ioaddr->feature_addr = ioaddr->cmd_addr + SCC_REG_FEATURE;
	ioaddr->nsect_addr = ioaddr->cmd_addr + SCC_REG_NSECT;
	ioaddr->lbal_addr = ioaddr->cmd_addr + SCC_REG_LBAL;
	ioaddr->lbam_addr = ioaddr->cmd_addr + SCC_REG_LBAM;
	ioaddr->lbah_addr = ioaddr->cmd_addr + SCC_REG_LBAH;
	ioaddr->device_addr = ioaddr->cmd_addr + SCC_REG_DEVICE;
	ioaddr->status_addr = ioaddr->cmd_addr + SCC_REG_STATUS;
	ioaddr->command_addr = ioaddr->cmd_addr + SCC_REG_CMD;
}

static int scc_host_init(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	int rc;

	rc = scc_reset_controller(host);
	if (rc)
		return rc;

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;

	scc_setup_ports(&host->ports[0]->ioaddr, host->iomap[SCC_BMID_BAR]);

	pci_set_master(pdev);

	return 0;
}

/**
 *	scc_init_one - Register SCC PATA device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in scc_pci_tbl matching with @pdev
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, or -ERRNO value.
 */

static int scc_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	unsigned int board_idx = (unsigned int) ent->driver_data;
	const struct ata_port_info *ppi[] = { &scc_port_info[board_idx], NULL };
	struct ata_host *host;
	int rc;

	ata_print_version_once(&pdev->dev, DRV_VERSION);

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, 1);
	if (!host)
		return -ENOMEM;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, (1 << SCC_CTRL_BAR) | (1 << SCC_BMID_BAR), DRV_NAME);
	if (rc == -EBUSY)
		pcim_pin_device(pdev);
	if (rc)
		return rc;
	host->iomap = pcim_iomap_table(pdev);

	ata_port_pbar_desc(host->ports[0], SCC_CTRL_BAR, -1, "ctrl");
	ata_port_pbar_desc(host->ports[0], SCC_BMID_BAR, -1, "bmid");

	rc = scc_host_init(host);
	if (rc)
		return rc;

	return ata_host_activate(host, pdev->irq, ata_bmdma_interrupt,
				 IRQF_SHARED, &scc_sht);
}

static struct pci_driver scc_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= scc_pci_tbl,
	.probe			= scc_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend		= ata_pci_device_suspend,
	.resume			= ata_pci_device_resume,
#endif
};

module_pci_driver(scc_pci_driver);

MODULE_AUTHOR("Toshiba corp");
MODULE_DESCRIPTION("SCSI low-level driver for Toshiba SCC PATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, scc_pci_tbl);
MODULE_VERSION(DRV_VERSION);
