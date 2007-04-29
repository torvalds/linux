/*
 * Libata driver for the highpoint 372N and 302N UDMA66 ATA controllers.
 *
 * This driver is heavily based upon:
 *
 * linux/drivers/ide/pci/hpt366.c		Version 0.36	April 25, 2003
 *
 * Copyright (C) 1999-2003		Andre Hedrick <andre@linux-ide.org>
 * Portions Copyright (C) 2001	        Sun Microsystems, Inc.
 * Portions Copyright (C) 2003		Red Hat Inc
 * Portions Copyright (C) 2005-2006	MontaVista Software, Inc.
 *
 *
 * TODO
 *	Work out best PLL policy
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME	"pata_hpt3x2n"
#define DRV_VERSION	"0.3.3"

enum {
	HPT_PCI_FAST	=	(1 << 31),
	PCI66		=	(1 << 1),
	USE_DPLL	=	(1 << 0)
};

struct hpt_clock {
	u8	xfer_speed;
	u32	timing;
};

struct hpt_chip {
	const char *name;
	struct hpt_clock *clocks[3];
};

/* key for bus clock timings
 * bit
 * 0:3    data_high_time. inactive time of DIOW_/DIOR_ for PIO and MW
 *        DMA. cycles = value + 1
 * 4:8    data_low_time. active time of DIOW_/DIOR_ for PIO and MW
 *        DMA. cycles = value + 1
 * 9:12   cmd_high_time. inactive time of DIOW_/DIOR_ during task file
 *        register access.
 * 13:17  cmd_low_time. active time of DIOW_/DIOR_ during task file
 *        register access.
 * 18:21  udma_cycle_time. clock freq and clock cycles for UDMA xfer.
 *        during task file register access.
 * 22:24  pre_high_time. time to initialize 1st cycle for PIO and MW DMA
 *        xfer.
 * 25:27  cmd_pre_high_time. time to initialize 1st PIO cycle for task
 *        register access.
 * 28     UDMA enable
 * 29     DMA enable
 * 30     PIO_MST enable. if set, the chip is in bus master mode during
 *        PIO.
 * 31     FIFO enable.
 */

/* 66MHz DPLL clocks */

static struct hpt_clock hpt3x2n_clocks[] = {
	{	XFER_UDMA_7,	0x1c869c62	},
	{	XFER_UDMA_6,	0x1c869c62	},
	{	XFER_UDMA_5,	0x1c8a9c62	},
	{	XFER_UDMA_4,	0x1c8a9c62	},
	{	XFER_UDMA_3,	0x1c8e9c62	},
	{	XFER_UDMA_2,	0x1c929c62	},
	{	XFER_UDMA_1,	0x1c9a9c62	},
	{	XFER_UDMA_0,	0x1c829c62	},

	{	XFER_MW_DMA_2,	0x2c829c62	},
	{	XFER_MW_DMA_1,	0x2c829c66	},
	{	XFER_MW_DMA_0,	0x2c829d2c	},

	{	XFER_PIO_4,	0x0c829c62	},
	{	XFER_PIO_3,	0x0c829c84	},
	{	XFER_PIO_2,	0x0c829ca6	},
	{	XFER_PIO_1,	0x0d029d26	},
	{	XFER_PIO_0,	0x0d029d5e	},
	{	0,		0x0d029d5e	}
};

/**
 *	hpt3x2n_find_mode	-	reset the hpt3x2n bus
 *	@ap: ATA port
 *	@speed: transfer mode
 *
 *	Return the 32bit register programming information for this channel
 *	that matches the speed provided. For the moment the clocks table
 *	is hard coded but easy to change. This will be needed if we use
 *	different DPLLs
 */

static u32 hpt3x2n_find_mode(struct ata_port *ap, int speed)
{
	struct hpt_clock *clocks = hpt3x2n_clocks;

	while(clocks->xfer_speed) {
		if (clocks->xfer_speed == speed)
			return clocks->timing;
		clocks++;
	}
	BUG();
	return 0xffffffffU;	/* silence compiler warning */
}

/**
 *	hpt3x2n_cable_detect	-	Detect the cable type
 *	@ap: ATA port to detect on
 *
 *	Return the cable type attached to this port
 */

static int hpt3x2n_cable_detect(struct ata_port *ap)
{
	u8 scr2, ata66;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	pci_read_config_byte(pdev, 0x5B, &scr2);
	pci_write_config_byte(pdev, 0x5B, scr2 & ~0x01);
	/* Cable register now active */
	pci_read_config_byte(pdev, 0x5A, &ata66);
	/* Restore state */
	pci_write_config_byte(pdev, 0x5B, scr2);

	if (ata66 & (1 << ap->port_no))
		return ATA_CBL_PATA40;
	else
		return ATA_CBL_PATA80;
}

/**
 *	hpt3x2n_pre_reset	-	reset the hpt3x2n bus
 *	@ap: ATA port to reset
 *	@deadline: deadline jiffies for the operation
 *
 *	Perform the initial reset handling for the 3x2n series controllers.
 *	Reset the hardware and state machine,
 */

static int hpt3xn_pre_reset(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	/* Reset the state machine */
	pci_write_config_byte(pdev, 0x50 + 4 * ap->port_no, 0x37);
	udelay(100);
	return ata_std_prereset(ap);
}

/**
 *	hpt3x2n_error_handler	-	probe the hpt3x2n bus
 *	@ap: ATA port to reset
 *
 *	Perform the probe reset handling for the 3x2N
 */

static void hpt3x2n_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, hpt3xn_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	hpt3x2n_set_piomode		-	PIO setup
 *	@ap: ATA interface
 *	@adev: device on the interface
 *
 *	Perform PIO mode setup.
 */

static void hpt3x2n_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 addr1, addr2;
	u32 reg;
	u32 mode;
	u8 fast;

	addr1 = 0x40 + 4 * (adev->devno + 2 * ap->port_no);
	addr2 = 0x51 + 4 * ap->port_no;

	/* Fast interrupt prediction disable, hold off interrupt disable */
	pci_read_config_byte(pdev, addr2, &fast);
	fast &= ~0x07;
	pci_write_config_byte(pdev, addr2, fast);

	pci_read_config_dword(pdev, addr1, &reg);
	mode = hpt3x2n_find_mode(ap, adev->pio_mode);
	mode &= ~0x8000000;	/* No FIFO in PIO */
	mode &= ~0x30070000;	/* Leave config bits alone */
	reg &= 0x30070000;	/* Strip timing bits */
	pci_write_config_dword(pdev, addr1, reg | mode);
}

/**
 *	hpt3x2n_set_dmamode		-	DMA timing setup
 *	@ap: ATA interface
 *	@adev: Device being configured
 *
 *	Set up the channel for MWDMA or UDMA modes. Much the same as with
 *	PIO, load the mode number and then set MWDMA or UDMA flag.
 */

static void hpt3x2n_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 addr1, addr2;
	u32 reg;
	u32 mode;
	u8 fast;

	addr1 = 0x40 + 4 * (adev->devno + 2 * ap->port_no);
	addr2 = 0x51 + 4 * ap->port_no;

	/* Fast interrupt prediction disable, hold off interrupt disable */
	pci_read_config_byte(pdev, addr2, &fast);
	fast &= ~0x07;
	pci_write_config_byte(pdev, addr2, fast);

	pci_read_config_dword(pdev, addr1, &reg);
	mode = hpt3x2n_find_mode(ap, adev->dma_mode);
	mode |= 0x8000000;	/* FIFO in MWDMA or UDMA */
	mode &= ~0xC0000000;	/* Leave config bits alone */
	reg &= 0xC0000000;	/* Strip timing bits */
	pci_write_config_dword(pdev, addr1, reg | mode);
}

/**
 *	hpt3x2n_bmdma_end		-	DMA engine stop
 *	@qc: ATA command
 *
 *	Clean up after the HPT3x2n and later DMA engine
 */

static void hpt3x2n_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int mscreg = 0x50 + 2 * ap->port_no;
	u8 bwsr_stat, msc_stat;

	pci_read_config_byte(pdev, 0x6A, &bwsr_stat);
	pci_read_config_byte(pdev, mscreg, &msc_stat);
	if (bwsr_stat & (1 << ap->port_no))
		pci_write_config_byte(pdev, mscreg, msc_stat | 0x30);
	ata_bmdma_stop(qc);
}

/**
 *	hpt3x2n_set_clock	-	clock control
 *	@ap: ATA port
 *	@source: 0x21 or 0x23 for PLL or PCI sourced clock
 *
 *	Switch the ATA bus clock between the PLL and PCI clock sources
 *	while correctly isolating the bus and resetting internal logic
 *
 *	We must use the DPLL for
 *	-	writing
 *	-	second channel UDMA7 (SATA ports) or higher
 *	-	66MHz PCI
 *
 *	or we will underclock the device and get reduced performance.
 */

static void hpt3x2n_set_clock(struct ata_port *ap, int source)
{
	void __iomem *bmdma = ap->ioaddr.bmdma_addr;

	/* Tristate the bus */
	iowrite8(0x80, bmdma+0x73);
	iowrite8(0x80, bmdma+0x77);

	/* Switch clock and reset channels */
	iowrite8(source, bmdma+0x7B);
	iowrite8(0xC0, bmdma+0x79);

	/* Reset state machines */
	iowrite8(0x37, bmdma+0x70);
	iowrite8(0x37, bmdma+0x74);

	/* Complete reset */
	iowrite8(0x00, bmdma+0x79);

	/* Reconnect channels to bus */
	iowrite8(0x00, bmdma+0x73);
	iowrite8(0x00, bmdma+0x77);
}

/* Check if our partner interface is busy */

static int hpt3x2n_pair_idle(struct ata_port *ap)
{
	struct ata_host *host = ap->host;
	struct ata_port *pair = host->ports[ap->port_no ^ 1];

	if (pair->hsm_task_state == HSM_ST_IDLE)
		return 1;
	return 0;
}

static int hpt3x2n_use_dpll(struct ata_port *ap, int writing)
{
	long flags = (long)ap->host->private_data;
	/* See if we should use the DPLL */
	if (writing)
		return USE_DPLL;	/* Needed for write */
	if (flags & PCI66)
		return USE_DPLL;	/* Needed at 66Mhz */
	return 0;
}

static unsigned int hpt3x2n_qc_issue_prot(struct ata_queued_cmd *qc)
{
	struct ata_taskfile *tf = &qc->tf;
	struct ata_port *ap = qc->ap;
	int flags = (long)ap->host->private_data;

	if (hpt3x2n_pair_idle(ap)) {
		int dpll = hpt3x2n_use_dpll(ap, (tf->flags & ATA_TFLAG_WRITE));
		if ((flags & USE_DPLL) != dpll) {
			if (dpll == 1)
				hpt3x2n_set_clock(ap, 0x21);
			else
				hpt3x2n_set_clock(ap, 0x23);
		}
	}
	return ata_qc_issue_prot(qc);
}

static struct scsi_host_template hpt3x2n_sht = {
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

/*
 *	Configuration for HPT3x2n.
 */

static struct ata_port_operations hpt3x2n_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= hpt3x2n_set_piomode,
	.set_dmamode	= hpt3x2n_set_dmamode,
	.mode_filter	= ata_pci_default_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= hpt3x2n_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= hpt3x2n_cable_detect,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= hpt3x2n_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= hpt3x2n_qc_issue_prot,

	.data_xfer	= ata_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

/**
 *	hpt3xn_calibrate_dpll		-	Calibrate the DPLL loop
 *	@dev: PCI device
 *
 *	Perform a calibration cycle on the HPT3xN DPLL. Returns 1 if this
 *	succeeds
 */

static int hpt3xn_calibrate_dpll(struct pci_dev *dev)
{
	u8 reg5b;
	u32 reg5c;
	int tries;

	for(tries = 0; tries < 0x5000; tries++) {
		udelay(50);
		pci_read_config_byte(dev, 0x5b, &reg5b);
		if (reg5b & 0x80) {
			/* See if it stays set */
			for(tries = 0; tries < 0x1000; tries ++) {
				pci_read_config_byte(dev, 0x5b, &reg5b);
				/* Failed ? */
				if ((reg5b & 0x80) == 0)
					return 0;
			}
			/* Turn off tuning, we have the DPLL set */
			pci_read_config_dword(dev, 0x5c, &reg5c);
			pci_write_config_dword(dev, 0x5c, reg5c & ~ 0x100);
			return 1;
		}
	}
	/* Never went stable */
	return 0;
}

static int hpt3x2n_pci_clock(struct pci_dev *pdev)
{
	unsigned long freq;
	u32 fcnt;
	unsigned long iobase = pci_resource_start(pdev, 4);

	fcnt = inl(iobase + 0x90);	/* Not PCI readable for some chips */
	if ((fcnt >> 12) != 0xABCDE) {
		printk(KERN_WARNING "hpt3xn: BIOS clock data not set.\n");
		return 33;	/* Not BIOS set */
	}
	fcnt &= 0x1FF;

	freq = (fcnt * 77) / 192;

	/* Clamp to bands */
	if (freq < 40)
		return 33;
	if (freq < 45)
		return 40;
	if (freq < 55)
		return 50;
	return 66;
}

/**
 *	hpt3x2n_init_one		-	Initialise an HPT37X/302
 *	@dev: PCI device
 *	@id: Entry in match table
 *
 *	Initialise an HPT3x2n device. There are some interesting complications
 *	here. Firstly the chip may report 366 and be one of several variants.
 *	Secondly all the timings depend on the clock for the chip which we must
 *	detect and look up
 *
 *	This is the known chip mappings. It may be missing a couple of later
 *	releases.
 *
 *	Chip version		PCI		Rev	Notes
 *	HPT372			4 (HPT366)	5	Other driver
 *	HPT372N			4 (HPT366)	6	UDMA133
 *	HPT372			5 (HPT372)	1	Other driver
 *	HPT372N			5 (HPT372)	2	UDMA133
 *	HPT302			6 (HPT302)	*	Other driver
 *	HPT302N			6 (HPT302)	> 1	UDMA133
 *	HPT371			7 (HPT371)	*	Other driver
 *	HPT371N			7 (HPT371)	> 1	UDMA133
 *	HPT374			8 (HPT374)	*	Other driver
 *	HPT372N			9 (HPT372N)	*	UDMA133
 *
 *	(1) UDMA133 support depends on the bus clock
 *
 *	To pin down		HPT371N
 */

static int hpt3x2n_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	/* HPT372N and friends - UDMA133 */
	static struct ata_port_info info = {
		.sht = &hpt3x2n_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x7f,
		.port_ops = &hpt3x2n_port_ops
	};
	struct ata_port_info *port_info[2];
	struct ata_port_info *port = &info;

	u8 irqmask;
	u32 class_rev;

	unsigned int pci_mhz;
	unsigned int f_low, f_high;
	int adjust;
	unsigned long iobase = pci_resource_start(dev, 4);

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xFF;

	switch(dev->device) {
		case PCI_DEVICE_ID_TTI_HPT366:
			if (class_rev < 6)
				return -ENODEV;
			break;
		case PCI_DEVICE_ID_TTI_HPT371:
			if (class_rev < 2)
				return -ENODEV;
			/* 371N if rev > 1 */
			break;
		case PCI_DEVICE_ID_TTI_HPT372:
			/* 372N if rev >= 1*/
			if (class_rev == 0)
				return -ENODEV;
			break;
		case PCI_DEVICE_ID_TTI_HPT302:
			if (class_rev < 2)
				return -ENODEV;
			break;
		case PCI_DEVICE_ID_TTI_HPT372N:
			break;
		default:
			printk(KERN_ERR "pata_hpt3x2n: PCI table is bogus please report (%d).\n", dev->device);
			return -ENODEV;
	}

	/* Ok so this is a chip we support */

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, (L1_CACHE_BYTES / 4));
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x78);
	pci_write_config_byte(dev, PCI_MIN_GNT, 0x08);
	pci_write_config_byte(dev, PCI_MAX_LAT, 0x08);

	pci_read_config_byte(dev, 0x5A, &irqmask);
	irqmask &= ~0x10;
	pci_write_config_byte(dev, 0x5a, irqmask);

	/*
	 * HPT371 chips physically have only one channel, the secondary one,
	 * but the primary channel registers do exist!  Go figure...
	 * So,  we manually disable the non-existing channel here
	 * (if the BIOS hasn't done this already).
	 */
	if (dev->device == PCI_DEVICE_ID_TTI_HPT371) {
		u8 mcr1;
		pci_read_config_byte(dev, 0x50, &mcr1);
		mcr1 &= ~0x04;
		pci_write_config_byte(dev, 0x50, mcr1);
	}

	/* Tune the PLL. HPT recommend using 75 for SATA, 66 for UDMA133 or
	   50 for UDMA100. Right now we always use 66 */

	pci_mhz = hpt3x2n_pci_clock(dev);

	f_low = (pci_mhz * 48) / 66;	/* PCI Mhz for 66Mhz DPLL */
	f_high = f_low + 2;		/* Tolerance */

	pci_write_config_dword(dev, 0x5C, (f_high << 16) | f_low | 0x100);
	/* PLL clock */
	pci_write_config_byte(dev, 0x5B, 0x21);

	/* Unlike the 37x we don't try jiggling the frequency */
	for(adjust = 0; adjust < 8; adjust++) {
		if (hpt3xn_calibrate_dpll(dev))
			break;
		pci_write_config_dword(dev, 0x5C, (f_high << 16) | f_low);
	}
	if (adjust == 8) {
		printk(KERN_WARNING "hpt3x2n: DPLL did not stabilize.\n");
		return -ENODEV;
	}

	/* Set our private data up. We only need a few flags so we use
	   it directly */
	port->private_data = NULL;
	if (pci_mhz > 60) {
		port->private_data = (void *)PCI66;
		/*
		 * On  HPT371N, if ATA clock is 66 MHz we must set bit 2 in
		 * the MISC. register to stretch the UltraDMA Tss timing.
		 * NOTE: This register is only writeable via I/O space.
		 */
		if (dev->device == PCI_DEVICE_ID_TTI_HPT371)
			outb(inb(iobase + 0x9c) | 0x04, iobase + 0x9c);
	}

	/* Now kick off ATA set up */
	port_info[0] = port_info[1] = port;
	return ata_pci_init_one(dev, port_info, 2);
}

static const struct pci_device_id hpt3x2n[] = {
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT366), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT371), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT372), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT302), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT372N), },

	{ },
};

static struct pci_driver hpt3x2n_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= hpt3x2n,
	.probe 		= hpt3x2n_init_one,
	.remove		= ata_pci_remove_one
};

static int __init hpt3x2n_init(void)
{
	return pci_register_driver(&hpt3x2n_pci_driver);
}

static void __exit hpt3x2n_exit(void)
{
	pci_unregister_driver(&hpt3x2n_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for the Highpoint HPT3x2n/30x");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, hpt3x2n);
MODULE_VERSION(DRV_VERSION);

module_init(hpt3x2n_init);
module_exit(hpt3x2n_exit);
