/*
 *  Promise PATA TX2/TX4/TX2000/133 IDE driver for pdc20268 to pdc20277.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Ported to libata by:
 *  Albert Lee <albertcc@tw.ibm.com> IBM Corporation
 *
 *  Copyright (C) 1998-2002		Andre Hedrick <andre@linux-ide.org>
 *  Portions Copyright (C) 1999 Promise Technology, Inc.
 *
 *  Author: Frank Tiernan (frankt@promise.com)
 *  Released under terms of General Public License
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/DocBook/libata.*
 *
 *  Hardware information only available under NDA.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <linux/libata.h>

#define DRV_NAME	"pata_pdc2027x"
#define DRV_VERSION	"1.0"
#undef PDC_DEBUG

#ifdef PDC_DEBUG
#define PDPRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, __func__, ## args)
#else
#define PDPRINTK(fmt, args...)
#endif

enum {
	PDC_MMIO_BAR		= 5,

	PDC_UDMA_100		= 0,
	PDC_UDMA_133		= 1,

	PDC_100_MHZ		= 100000000,
	PDC_133_MHZ		= 133333333,

	PDC_SYS_CTL		= 0x1100,
	PDC_ATA_CTL		= 0x1104,
	PDC_GLOBAL_CTL		= 0x1108,
	PDC_CTCR0		= 0x110C,
	PDC_CTCR1		= 0x1110,
	PDC_BYTE_COUNT		= 0x1120,
	PDC_PLL_CTL		= 0x1202,
};

static int pdc2027x_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);
static void pdc2027x_error_handler(struct ata_port *ap);
static void pdc2027x_set_piomode(struct ata_port *ap, struct ata_device *adev);
static void pdc2027x_set_dmamode(struct ata_port *ap, struct ata_device *adev);
static int pdc2027x_check_atapi_dma(struct ata_queued_cmd *qc);
static unsigned long pdc2027x_mode_filter(struct ata_device *adev, unsigned long mask);
static int pdc2027x_cable_detect(struct ata_port *ap);
static int pdc2027x_set_mode(struct ata_link *link, struct ata_device **r_failed);

/*
 * ATA Timing Tables based on 133MHz controller clock.
 * These tables are only used when the controller is in 133MHz clock.
 * If the controller is in 100MHz clock, the ASIC hardware will
 * set the timing registers automatically when "set feature" command
 * is issued to the device. However, if the controller clock is 133MHz,
 * the following tables must be used.
 */
static struct pdc2027x_pio_timing {
	u8 value0, value1, value2;
} pdc2027x_pio_timing_tbl [] = {
	{ 0xfb, 0x2b, 0xac }, /* PIO mode 0 */
	{ 0x46, 0x29, 0xa4 }, /* PIO mode 1 */
	{ 0x23, 0x26, 0x64 }, /* PIO mode 2 */
	{ 0x27, 0x0d, 0x35 }, /* PIO mode 3, IORDY on, Prefetch off */
	{ 0x23, 0x09, 0x25 }, /* PIO mode 4, IORDY on, Prefetch off */
};

static struct pdc2027x_mdma_timing {
	u8 value0, value1;
} pdc2027x_mdma_timing_tbl [] = {
	{ 0xdf, 0x5f }, /* MDMA mode 0 */
	{ 0x6b, 0x27 }, /* MDMA mode 1 */
	{ 0x69, 0x25 }, /* MDMA mode 2 */
};

static struct pdc2027x_udma_timing {
	u8 value0, value1, value2;
} pdc2027x_udma_timing_tbl [] = {
	{ 0x4a, 0x0f, 0xd5 }, /* UDMA mode 0 */
	{ 0x3a, 0x0a, 0xd0 }, /* UDMA mode 1 */
	{ 0x2a, 0x07, 0xcd }, /* UDMA mode 2 */
	{ 0x1a, 0x05, 0xcd }, /* UDMA mode 3 */
	{ 0x1a, 0x03, 0xcd }, /* UDMA mode 4 */
	{ 0x1a, 0x02, 0xcb }, /* UDMA mode 5 */
	{ 0x1a, 0x01, 0xcb }, /* UDMA mode 6 */
};

static const struct pci_device_id pdc2027x_pci_tbl[] = {
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20268), PDC_UDMA_100 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20269), PDC_UDMA_133 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20270), PDC_UDMA_100 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20271), PDC_UDMA_133 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20275), PDC_UDMA_133 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20276), PDC_UDMA_133 },
	{ PCI_VDEVICE(PROMISE, PCI_DEVICE_ID_PROMISE_20277), PDC_UDMA_133 },

	{ }	/* terminate list */
};

static struct pci_driver pdc2027x_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= pdc2027x_pci_tbl,
	.probe			= pdc2027x_init_one,
	.remove			= ata_pci_remove_one,
};

static struct scsi_host_template pdc2027x_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations pdc2027x_pata100_ops = {
	.mode_filter		= ata_pci_default_filter,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.check_atapi_dma	= pdc2027x_check_atapi_dma,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= pdc2027x_error_handler,
	.post_internal_cmd 	= ata_bmdma_post_internal_cmd,
	.cable_detect		= pdc2027x_cable_detect,

	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,

	.port_start		= ata_sff_port_start,
};

static struct ata_port_operations pdc2027x_pata133_ops = {
	.set_piomode		= pdc2027x_set_piomode,
	.set_dmamode		= pdc2027x_set_dmamode,
	.set_mode		= pdc2027x_set_mode,
	.mode_filter		= pdc2027x_mode_filter,

	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.check_atapi_dma	= pdc2027x_check_atapi_dma,
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= pdc2027x_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.cable_detect		= pdc2027x_cable_detect,

	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,

	.port_start		= ata_sff_port_start,
};

static struct ata_port_info pdc2027x_port_info[] = {
	/* PDC_UDMA_100 */
	{
		.flags		= ATA_FLAG_NO_LEGACY | ATA_FLAG_SLAVE_POSS |
		                  ATA_FLAG_MMIO,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= ATA_UDMA5, /* udma0-5 */
		.port_ops	= &pdc2027x_pata100_ops,
	},
	/* PDC_UDMA_133 */
	{
		.flags		= ATA_FLAG_NO_LEGACY | ATA_FLAG_SLAVE_POSS |
                        	  ATA_FLAG_MMIO,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= ATA_UDMA6, /* udma0-6 */
		.port_ops	= &pdc2027x_pata133_ops,
	},
};

MODULE_AUTHOR("Andre Hedrick, Frank Tiernan, Albert Lee");
MODULE_DESCRIPTION("libata driver module for Promise PDC20268 to PDC20277");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, pdc2027x_pci_tbl);

/**
 *	port_mmio - Get the MMIO address of PDC2027x extended registers
 *	@ap: Port
 *	@offset: offset from mmio base
 */
static inline void __iomem *port_mmio(struct ata_port *ap, unsigned int offset)
{
	return ap->host->iomap[PDC_MMIO_BAR] + ap->port_no * 0x100 + offset;
}

/**
 *	dev_mmio - Get the MMIO address of PDC2027x extended registers
 *	@ap: Port
 *	@adev: device
 *	@offset: offset from mmio base
 */
static inline void __iomem *dev_mmio(struct ata_port *ap, struct ata_device *adev, unsigned int offset)
{
	u8 adj = (adev->devno) ? 0x08 : 0x00;
	return port_mmio(ap, offset) + adj;
}

/**
 *	pdc2027x_pata_cable_detect - Probe host controller cable detect info
 *	@ap: Port for which cable detect info is desired
 *
 *	Read 80c cable indicator from Promise extended register.
 *      This register is latched when the system is reset.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static int pdc2027x_cable_detect(struct ata_port *ap)
{
	u32 cgcr;

	/* check cable detect results */
	cgcr = ioread32(port_mmio(ap, PDC_GLOBAL_CTL));
	if (cgcr & (1 << 26))
		goto cbl40;

	PDPRINTK("No cable or 80-conductor cable on port %d\n", ap->port_no);

	return ATA_CBL_PATA80;
cbl40:
	printk(KERN_INFO DRV_NAME ": 40-conductor cable detected on port %d\n", ap->port_no);
	return ATA_CBL_PATA40;
}

/**
 * pdc2027x_port_enabled - Check PDC ATA control register to see whether the port is enabled.
 * @ap: Port to check
 */
static inline int pdc2027x_port_enabled(struct ata_port *ap)
{
	return ioread8(port_mmio(ap, PDC_ATA_CTL)) & 0x02;
}

/**
 *	pdc2027x_prereset - prereset for PATA host controller
 *	@link: Target link
 *	@deadline: deadline jiffies for the operation
 *
 *	Probeinit including cable detection.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static int pdc2027x_prereset(struct ata_link *link, unsigned long deadline)
{
	/* Check whether port enabled */
	if (!pdc2027x_port_enabled(link->ap))
		return -ENOENT;
	return ata_std_prereset(link, deadline);
}

/**
 *	pdc2027x_error_handler - Perform reset on PATA port and classify
 *	@ap: Port to reset
 *
 *	Reset PATA phy and classify attached devices.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void pdc2027x_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, pdc2027x_prereset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	pdc2720x_mode_filter	-	mode selection filter
 *	@adev: ATA device
 *	@mask: list of modes proposed
 *
 *	Block UDMA on devices that cause trouble with this controller.
 */

static unsigned long pdc2027x_mode_filter(struct ata_device *adev, unsigned long mask)
{
	unsigned char model_num[ATA_ID_PROD_LEN + 1];
	struct ata_device *pair = ata_dev_pair(adev);

	if (adev->class != ATA_DEV_ATA || adev->devno == 0 || pair == NULL)
		return ata_pci_default_filter(adev, mask);

	/* Check for slave of a Maxtor at UDMA6 */
	ata_id_c_string(pair->id, model_num, ATA_ID_PROD,
			  ATA_ID_PROD_LEN + 1);
	/* If the master is a maxtor in UDMA6 then the slave should not use UDMA 6 */
	if (strstr(model_num, "Maxtor") == NULL && pair->dma_mode == XFER_UDMA_6)
		mask &= ~ (1 << (6 + ATA_SHIFT_UDMA));

	return ata_pci_default_filter(adev, mask);
}

/**
 *	pdc2027x_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port to configure
 *	@adev: um
 *	@pio: PIO mode, 0 - 4
 *
 *	Set PIO mode for device.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void pdc2027x_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio = adev->pio_mode - XFER_PIO_0;
	u32 ctcr0, ctcr1;

	PDPRINTK("adev->pio_mode[%X]\n", adev->pio_mode);

	/* Sanity check */
	if (pio > 4) {
		printk(KERN_ERR DRV_NAME ": Unknown pio mode [%d] ignored\n", pio);
		return;

	}

	/* Set the PIO timing registers using value table for 133MHz */
	PDPRINTK("Set pio regs... \n");

	ctcr0 = ioread32(dev_mmio(ap, adev, PDC_CTCR0));
	ctcr0 &= 0xffff0000;
	ctcr0 |= pdc2027x_pio_timing_tbl[pio].value0 |
		(pdc2027x_pio_timing_tbl[pio].value1 << 8);
	iowrite32(ctcr0, dev_mmio(ap, adev, PDC_CTCR0));

	ctcr1 = ioread32(dev_mmio(ap, adev, PDC_CTCR1));
	ctcr1 &= 0x00ffffff;
	ctcr1 |= (pdc2027x_pio_timing_tbl[pio].value2 << 24);
	iowrite32(ctcr1, dev_mmio(ap, adev, PDC_CTCR1));

	PDPRINTK("Set pio regs done\n");

	PDPRINTK("Set to pio mode[%u] \n", pio);
}

/**
 *	pdc2027x_set_dmamode - Initialize host controller PATA UDMA timings
 *	@ap: Port to configure
 *	@adev: um
 *	@udma: udma mode, XFER_UDMA_0 to XFER_UDMA_6
 *
 *	Set UDMA mode for device.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */
static void pdc2027x_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	unsigned int dma_mode = adev->dma_mode;
	u32 ctcr0, ctcr1;

	if ((dma_mode >= XFER_UDMA_0) &&
	   (dma_mode <= XFER_UDMA_6)) {
		/* Set the UDMA timing registers with value table for 133MHz */
		unsigned int udma_mode = dma_mode & 0x07;

		if (dma_mode == XFER_UDMA_2) {
			/*
			 * Turn off tHOLD.
			 * If tHOLD is '1', the hardware will add half clock for data hold time.
			 * This code segment seems to be no effect. tHOLD will be overwritten below.
			 */
			ctcr1 = ioread32(dev_mmio(ap, adev, PDC_CTCR1));
			iowrite32(ctcr1 & ~(1 << 7), dev_mmio(ap, adev, PDC_CTCR1));
		}

		PDPRINTK("Set udma regs... \n");

		ctcr1 = ioread32(dev_mmio(ap, adev, PDC_CTCR1));
		ctcr1 &= 0xff000000;
		ctcr1 |= pdc2027x_udma_timing_tbl[udma_mode].value0 |
			(pdc2027x_udma_timing_tbl[udma_mode].value1 << 8) |
			(pdc2027x_udma_timing_tbl[udma_mode].value2 << 16);
		iowrite32(ctcr1, dev_mmio(ap, adev, PDC_CTCR1));

		PDPRINTK("Set udma regs done\n");

		PDPRINTK("Set to udma mode[%u] \n", udma_mode);

	} else  if ((dma_mode >= XFER_MW_DMA_0) &&
		   (dma_mode <= XFER_MW_DMA_2)) {
		/* Set the MDMA timing registers with value table for 133MHz */
		unsigned int mdma_mode = dma_mode & 0x07;

		PDPRINTK("Set mdma regs... \n");
		ctcr0 = ioread32(dev_mmio(ap, adev, PDC_CTCR0));

		ctcr0 &= 0x0000ffff;
		ctcr0 |= (pdc2027x_mdma_timing_tbl[mdma_mode].value0 << 16) |
			(pdc2027x_mdma_timing_tbl[mdma_mode].value1 << 24);

		iowrite32(ctcr0, dev_mmio(ap, adev, PDC_CTCR0));
		PDPRINTK("Set mdma regs done\n");

		PDPRINTK("Set to mdma mode[%u] \n", mdma_mode);
	} else {
		printk(KERN_ERR DRV_NAME ": Unknown dma mode [%u] ignored\n", dma_mode);
	}
}

/**
 *	pdc2027x_set_mode - Set the timing registers back to correct values.
 *	@link: link to configure
 *	@r_failed: Returned device for failure
 *
 *	The pdc2027x hardware will look at "SET FEATURES" and change the timing registers
 *	automatically. The values set by the hardware might be incorrect, under 133Mhz PLL.
 *	This function overwrites the possibly incorrect values set by the hardware to be correct.
 */
static int pdc2027x_set_mode(struct ata_link *link, struct ata_device **r_failed)
{
	struct ata_port *ap = link->ap;
	struct ata_device *dev;
	int rc;

	rc = ata_do_set_mode(link, r_failed);
	if (rc < 0)
		return rc;

	ata_link_for_each_dev(dev, link) {
		if (ata_dev_enabled(dev)) {

			pdc2027x_set_piomode(ap, dev);

			/*
			 * Enable prefetch if the device support PIO only.
			 */
			if (dev->xfer_shift == ATA_SHIFT_PIO) {
				u32 ctcr1 = ioread32(dev_mmio(ap, dev, PDC_CTCR1));
				ctcr1 |= (1 << 25);
				iowrite32(ctcr1, dev_mmio(ap, dev, PDC_CTCR1));

				PDPRINTK("Turn on prefetch\n");
			} else {
				pdc2027x_set_dmamode(ap, dev);
			}
		}
	}
	return 0;
}

/**
 *	pdc2027x_check_atapi_dma - Check whether ATAPI DMA can be supported for this command
 *	@qc: Metadata associated with taskfile to check
 *
 *	LOCKING:
 *	None (inherited from caller).
 *
 *	RETURNS: 0 when ATAPI DMA can be used
 *		 1 otherwise
 */
static int pdc2027x_check_atapi_dma(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	u8 *scsicmd = cmd->cmnd;
	int rc = 1; /* atapi dma off by default */

	/*
	 * This workaround is from Promise's GPL driver.
	 * If ATAPI DMA is used for commands not in the
	 * following white list, say MODE_SENSE and REQUEST_SENSE,
	 * pdc2027x might hit the irq lost problem.
	 */
	switch (scsicmd[0]) {
	case READ_10:
	case WRITE_10:
	case READ_12:
	case WRITE_12:
	case READ_6:
	case WRITE_6:
	case 0xad: /* READ_DVD_STRUCTURE */
	case 0xbe: /* READ_CD */
		/* ATAPI DMA is ok */
		rc = 0;
		break;
	default:
		;
	}

	return rc;
}

/**
 * pdc_read_counter - Read the ctr counter
 * @host: target ATA host
 */

static long pdc_read_counter(struct ata_host *host)
{
	void __iomem *mmio_base = host->iomap[PDC_MMIO_BAR];
	long counter;
	int retry = 1;
	u32 bccrl, bccrh, bccrlv, bccrhv;

retry:
	bccrl = ioread32(mmio_base + PDC_BYTE_COUNT) & 0x7fff;
	bccrh = ioread32(mmio_base + PDC_BYTE_COUNT + 0x100) & 0x7fff;

	/* Read the counter values again for verification */
	bccrlv = ioread32(mmio_base + PDC_BYTE_COUNT) & 0x7fff;
	bccrhv = ioread32(mmio_base + PDC_BYTE_COUNT + 0x100) & 0x7fff;

	counter = (bccrh << 15) | bccrl;

	PDPRINTK("bccrh [%X] bccrl [%X]\n", bccrh,  bccrl);
	PDPRINTK("bccrhv[%X] bccrlv[%X]\n", bccrhv, bccrlv);

	/*
	 * The 30-bit decreasing counter are read by 2 pieces.
	 * Incorrect value may be read when both bccrh and bccrl are changing.
	 * Ex. When 7900 decrease to 78FF, wrong value 7800 might be read.
	 */
	if (retry && !(bccrh == bccrhv && bccrl >= bccrlv)) {
		retry--;
		PDPRINTK("rereading counter\n");
		goto retry;
	}

	return counter;
}

/**
 * adjust_pll - Adjust the PLL input clock in Hz.
 *
 * @pdc_controller: controller specific information
 * @host: target ATA host
 * @pll_clock: The input of PLL in HZ
 */
static void pdc_adjust_pll(struct ata_host *host, long pll_clock, unsigned int board_idx)
{
	void __iomem *mmio_base = host->iomap[PDC_MMIO_BAR];
	u16 pll_ctl;
	long pll_clock_khz = pll_clock / 1000;
	long pout_required = board_idx? PDC_133_MHZ:PDC_100_MHZ;
	long ratio = pout_required / pll_clock_khz;
	int F, R;

	/* Sanity check */
	if (unlikely(pll_clock_khz < 5000L || pll_clock_khz > 70000L)) {
		printk(KERN_ERR DRV_NAME ": Invalid PLL input clock %ldkHz, give up!\n", pll_clock_khz);
		return;
	}

#ifdef PDC_DEBUG
	PDPRINTK("pout_required is %ld\n", pout_required);

	/* Show the current clock value of PLL control register
	 * (maybe already configured by the firmware)
	 */
	pll_ctl = ioread16(mmio_base + PDC_PLL_CTL);

	PDPRINTK("pll_ctl[%X]\n", pll_ctl);
#endif

	/*
	 * Calculate the ratio of F, R and OD
	 * POUT = (F + 2) / (( R + 2) * NO)
	 */
	if (ratio < 8600L) { /* 8.6x */
		/* Using NO = 0x01, R = 0x0D */
		R = 0x0d;
	} else if (ratio < 12900L) { /* 12.9x */
		/* Using NO = 0x01, R = 0x08 */
		R = 0x08;
	} else if (ratio < 16100L) { /* 16.1x */
		/* Using NO = 0x01, R = 0x06 */
		R = 0x06;
	} else if (ratio < 64000L) { /* 64x */
		R = 0x00;
	} else {
		/* Invalid ratio */
		printk(KERN_ERR DRV_NAME ": Invalid ratio %ld, give up!\n", ratio);
		return;
	}

	F = (ratio * (R+2)) / 1000 - 2;

	if (unlikely(F < 0 || F > 127)) {
		/* Invalid F */
		printk(KERN_ERR DRV_NAME ": F[%d] invalid!\n", F);
		return;
	}

	PDPRINTK("F[%d] R[%d] ratio*1000[%ld]\n", F, R, ratio);

	pll_ctl = (R << 8) | F;

	PDPRINTK("Writing pll_ctl[%X]\n", pll_ctl);

	iowrite16(pll_ctl, mmio_base + PDC_PLL_CTL);
	ioread16(mmio_base + PDC_PLL_CTL); /* flush */

	/* Wait the PLL circuit to be stable */
	mdelay(30);

#ifdef PDC_DEBUG
	/*
	 *  Show the current clock value of PLL control register
	 * (maybe configured by the firmware)
	 */
	pll_ctl = ioread16(mmio_base + PDC_PLL_CTL);

	PDPRINTK("pll_ctl[%X]\n", pll_ctl);
#endif

	return;
}

/**
 * detect_pll_input_clock - Detect the PLL input clock in Hz.
 * @host: target ATA host
 * Ex. 16949000 on 33MHz PCI bus for pdc20275.
 *     Half of the PCI clock.
 */
static long pdc_detect_pll_input_clock(struct ata_host *host)
{
	void __iomem *mmio_base = host->iomap[PDC_MMIO_BAR];
	u32 scr;
	long start_count, end_count;
	struct timeval start_time, end_time;
	long pll_clock, usec_elapsed;

	/* Start the test mode */
	scr = ioread32(mmio_base + PDC_SYS_CTL);
	PDPRINTK("scr[%X]\n", scr);
	iowrite32(scr | (0x01 << 14), mmio_base + PDC_SYS_CTL);
	ioread32(mmio_base + PDC_SYS_CTL); /* flush */

	/* Read current counter value */
	start_count = pdc_read_counter(host);
	do_gettimeofday(&start_time);

	/* Let the counter run for 100 ms. */
	mdelay(100);

	/* Read the counter values again */
	end_count = pdc_read_counter(host);
	do_gettimeofday(&end_time);

	/* Stop the test mode */
	scr = ioread32(mmio_base + PDC_SYS_CTL);
	PDPRINTK("scr[%X]\n", scr);
	iowrite32(scr & ~(0x01 << 14), mmio_base + PDC_SYS_CTL);
	ioread32(mmio_base + PDC_SYS_CTL); /* flush */

	/* calculate the input clock in Hz */
	usec_elapsed = (end_time.tv_sec - start_time.tv_sec) * 1000000 +
		(end_time.tv_usec - start_time.tv_usec);

	pll_clock = ((start_count - end_count) & 0x3fffffff) / 100 *
		(100000000 / usec_elapsed);

	PDPRINTK("start[%ld] end[%ld] \n", start_count, end_count);
	PDPRINTK("PLL input clock[%ld]Hz\n", pll_clock);

	return pll_clock;
}

/**
 * pdc_hardware_init - Initialize the hardware.
 * @host: target ATA host
 * @board_idx: board identifier
 */
static int pdc_hardware_init(struct ata_host *host, unsigned int board_idx)
{
	long pll_clock;

	/*
	 * Detect PLL input clock rate.
	 * On some system, where PCI bus is running at non-standard clock rate.
	 * Ex. 25MHz or 40MHz, we have to adjust the cycle_time.
	 * The pdc20275 controller employs PLL circuit to help correct timing registers setting.
	 */
	pll_clock = pdc_detect_pll_input_clock(host);

	dev_printk(KERN_INFO, host->dev, "PLL input clock %ld kHz\n", pll_clock/1000);

	/* Adjust PLL control register */
	pdc_adjust_pll(host, pll_clock, board_idx);

	return 0;
}

/**
 * pdc_ata_setup_port - setup the mmio address
 * @port: ata ioports to setup
 * @base: base address
 */
static void pdc_ata_setup_port(struct ata_ioports *port, void __iomem *base)
{
	port->cmd_addr		=
	port->data_addr		= base;
	port->feature_addr	=
	port->error_addr	= base + 0x05;
	port->nsect_addr	= base + 0x0a;
	port->lbal_addr		= base + 0x0f;
	port->lbam_addr		= base + 0x10;
	port->lbah_addr		= base + 0x15;
	port->device_addr	= base + 0x1a;
	port->command_addr	=
	port->status_addr	= base + 0x1f;
	port->altstatus_addr	=
	port->ctl_addr		= base + 0x81a;
}

/**
 * pdc2027x_init_one - PCI probe function
 * Called when an instance of PCI adapter is inserted.
 * This function checks whether the hardware is supported,
 * initialize hardware and register an instance of ata_host to
 * libata.  (implements struct pci_driver.probe() )
 *
 * @pdev: instance of pci_dev found
 * @ent:  matching entry in the id_tbl[]
 */
static int __devinit pdc2027x_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	static const unsigned long cmd_offset[] = { 0x17c0, 0x15c0 };
	static const unsigned long bmdma_offset[] = { 0x1000, 0x1008 };
	unsigned int board_idx = (unsigned int) ent->driver_data;
	const struct ata_port_info *ppi[] =
		{ &pdc2027x_port_info[board_idx], NULL };
	struct ata_host *host;
	void __iomem *mmio_base;
	int i, rc;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/* alloc host */
	host = ata_host_alloc_pinfo(&pdev->dev, ppi, 2);
	if (!host)
		return -ENOMEM;

	/* acquire resources and fill host */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, 1 << PDC_MMIO_BAR, DRV_NAME);
	if (rc)
		return rc;
	host->iomap = pcim_iomap_table(pdev);

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;

	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;

	mmio_base = host->iomap[PDC_MMIO_BAR];

	for (i = 0; i < 2; i++) {
		struct ata_port *ap = host->ports[i];

		pdc_ata_setup_port(&ap->ioaddr, mmio_base + cmd_offset[i]);
		ap->ioaddr.bmdma_addr = mmio_base + bmdma_offset[i];

		ata_port_pbar_desc(ap, PDC_MMIO_BAR, -1, "mmio");
		ata_port_pbar_desc(ap, PDC_MMIO_BAR, cmd_offset[i], "cmd");
	}

	//pci_enable_intx(pdev);

	/* initialize adapter */
	if (pdc_hardware_init(host, board_idx) != 0)
		return -EIO;

	pci_set_master(pdev);
	return ata_host_activate(host, pdev->irq, ata_interrupt, IRQF_SHARED,
				 &pdc2027x_sht);
}

/**
 * pdc2027x_init - Called after this module is loaded into the kernel.
 */
static int __init pdc2027x_init(void)
{
	return pci_register_driver(&pdc2027x_pci_driver);
}

/**
 * pdc2027x_exit - Called before this module unloaded from the kernel
 */
static void __exit pdc2027x_exit(void)
{
	pci_unregister_driver(&pdc2027x_pci_driver);
}

module_init(pdc2027x_init);
module_exit(pdc2027x_exit);
