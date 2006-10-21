/*
 * Libata driver for the highpoint 366 and 368 UDMA66 ATA controllers.
 *
 * This driver is heavily based upon:
 *
 * linux/drivers/ide/pci/hpt366.c		Version 0.36	April 25, 2003
 *
 * Copyright (C) 1999-2003		Andre Hedrick <andre@linux-ide.org>
 * Portions Copyright (C) 2001	        Sun Microsystems, Inc.
 * Portions Copyright (C) 2003		Red Hat Inc
 *
 *
 * TODO
 *	Maybe PLL mode
 *	Look into engine reset on timeout errors. Should not be
 *		required.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME	"pata_hpt366"
#define DRV_VERSION	"0.5"

struct hpt_clock {
	u8	xfer_speed;
	u32	timing;
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

static const struct hpt_clock hpt366_40[] = {
	{	XFER_UDMA_4,	0x900fd943	},
	{	XFER_UDMA_3,	0x900ad943	},
	{	XFER_UDMA_2,	0x900bd943	},
	{	XFER_UDMA_1,	0x9008d943	},
	{	XFER_UDMA_0,	0x9008d943	},

	{	XFER_MW_DMA_2,	0xa008d943	},
	{	XFER_MW_DMA_1,	0xa010d955	},
	{	XFER_MW_DMA_0,	0xa010d9fc	},

	{	XFER_PIO_4,	0xc008d963	},
	{	XFER_PIO_3,	0xc010d974	},
	{	XFER_PIO_2,	0xc010d997	},
	{	XFER_PIO_1,	0xc010d9c7	},
	{	XFER_PIO_0,	0xc018d9d9	},
	{	0,		0x0120d9d9	}
};

static const struct hpt_clock hpt366_33[] = {
	{	XFER_UDMA_4,	0x90c9a731	},
	{	XFER_UDMA_3,	0x90cfa731	},
	{	XFER_UDMA_2,	0x90caa731	},
	{	XFER_UDMA_1,	0x90cba731	},
	{	XFER_UDMA_0,	0x90c8a731	},

	{	XFER_MW_DMA_2,	0xa0c8a731	},
	{	XFER_MW_DMA_1,	0xa0c8a732	},	/* 0xa0c8a733 */
	{	XFER_MW_DMA_0,	0xa0c8a797	},

	{	XFER_PIO_4,	0xc0c8a731	},
	{	XFER_PIO_3,	0xc0c8a742	},
	{	XFER_PIO_2,	0xc0d0a753	},
	{	XFER_PIO_1,	0xc0d0a7a3	},	/* 0xc0d0a793 */
	{	XFER_PIO_0,	0xc0d0a7aa	},	/* 0xc0d0a7a7 */
	{	0,		0x0120a7a7	}
};

static const struct hpt_clock hpt366_25[] = {
	{	XFER_UDMA_4,	0x90c98521	},
	{	XFER_UDMA_3,	0x90cf8521	},
	{	XFER_UDMA_2,	0x90cf8521	},
	{	XFER_UDMA_1,	0x90cb8521	},
	{	XFER_UDMA_0,	0x90cb8521	},

	{	XFER_MW_DMA_2,	0xa0ca8521	},
	{	XFER_MW_DMA_1,	0xa0ca8532	},
	{	XFER_MW_DMA_0,	0xa0ca8575	},

	{	XFER_PIO_4,	0xc0ca8521	},
	{	XFER_PIO_3,	0xc0ca8532	},
	{	XFER_PIO_2,	0xc0ca8542	},
	{	XFER_PIO_1,	0xc0d08572	},
	{	XFER_PIO_0,	0xc0d08585	},
	{	0,		0x01208585	}
};

static const char *bad_ata33[] = {
	"Maxtor 92720U8", "Maxtor 92040U6", "Maxtor 91360U4", "Maxtor 91020U3", "Maxtor 90845U3", "Maxtor 90650U2",
	"Maxtor 91360D8", "Maxtor 91190D7", "Maxtor 91020D6", "Maxtor 90845D5", "Maxtor 90680D4", "Maxtor 90510D3", "Maxtor 90340D2",
	"Maxtor 91152D8", "Maxtor 91008D7", "Maxtor 90845D6", "Maxtor 90840D6", "Maxtor 90720D5", "Maxtor 90648D5", "Maxtor 90576D4",
	"Maxtor 90510D4",
	"Maxtor 90432D3", "Maxtor 90288D2", "Maxtor 90256D2",
	"Maxtor 91000D8", "Maxtor 90910D8", "Maxtor 90875D7", "Maxtor 90840D7", "Maxtor 90750D6", "Maxtor 90625D5", "Maxtor 90500D4",
	"Maxtor 91728D8", "Maxtor 91512D7", "Maxtor 91303D6", "Maxtor 91080D5", "Maxtor 90845D4", "Maxtor 90680D4", "Maxtor 90648D3", "Maxtor 90432D2",
	NULL
};

static const char *bad_ata66_4[] = {
	"IBM-DTLA-307075",
	"IBM-DTLA-307060",
	"IBM-DTLA-307045",
	"IBM-DTLA-307030",
	"IBM-DTLA-307020",
	"IBM-DTLA-307015",
	"IBM-DTLA-305040",
	"IBM-DTLA-305030",
	"IBM-DTLA-305020",
	"IC35L010AVER07-0",
	"IC35L020AVER07-0",
	"IC35L030AVER07-0",
	"IC35L040AVER07-0",
	"IC35L060AVER07-0",
	"WDC AC310200R",
	NULL
};

static const char *bad_ata66_3[] = {
	"WDC AC310200R",
	NULL
};

static int hpt_dma_blacklisted(const struct ata_device *dev, char *modestr, const char *list[])
{
	unsigned char model_num[40];
	char *s;
	unsigned int len;
	int i = 0;

	ata_id_string(dev->id, model_num, ATA_ID_PROD_OFS, sizeof(model_num));
	s = &model_num[0];
	len = strnlen(s, sizeof(model_num));

	/* ATAPI specifies that empty space is blank-filled; remove blanks */
	while ((len > 0) && (s[len - 1] == ' ')) {
		len--;
		s[len] = 0;
	}

	while(list[i] != NULL) {
		if (!strncmp(list[i], s, len)) {
			printk(KERN_WARNING DRV_NAME ": %s is not supported for %s.\n",
				modestr, list[i]);
			return 1;
		}
		i++;
	}
	return 0;
}

/**
 *	hpt366_filter	-	mode selection filter
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Block UDMA on devices that cause trouble with this controller.
 */

static unsigned long hpt366_filter(const struct ata_port *ap, struct ata_device *adev, unsigned long mask)
{
	if (adev->class == ATA_DEV_ATA) {
		if (hpt_dma_blacklisted(adev, "UDMA",  bad_ata33))
			mask &= ~ATA_MASK_UDMA;
		if (hpt_dma_blacklisted(adev, "UDMA3", bad_ata66_3))
			mask &= ~(0x07 << ATA_SHIFT_UDMA);
		if (hpt_dma_blacklisted(adev, "UDMA4", bad_ata66_4))
			mask &= ~(0x0F << ATA_SHIFT_UDMA);
	}
	return ata_pci_default_filter(ap, adev, mask);
}

/**
 *	hpt36x_find_mode	-	reset the hpt36x bus
 *	@ap: ATA port
 *	@speed: transfer mode
 *
 *	Return the 32bit register programming information for this channel
 *	that matches the speed provided.
 */

static u32 hpt36x_find_mode(struct ata_port *ap, int speed)
{
	struct hpt_clock *clocks = ap->host->private_data;

	while(clocks->xfer_speed) {
		if (clocks->xfer_speed == speed)
			return clocks->timing;
		clocks++;
	}
	BUG();
	return 0xffffffffU;	/* silence compiler warning */
}

static int hpt36x_pre_reset(struct ata_port *ap)
{
	u8 ata66;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	pci_read_config_byte(pdev, 0x5A, &ata66);
	if (ata66 & (1 << ap->port_no))
		ap->cbl = ATA_CBL_PATA40;
	else
		ap->cbl = ATA_CBL_PATA80;
	return ata_std_prereset(ap);
}

/**
 *	hpt36x_error_handler	-	reset the hpt36x bus
 *	@ap: ATA port to reset
 *
 *	Perform the reset handling for the 366/368
 */

static void hpt36x_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, hpt36x_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	hpt366_set_piomode		-	PIO setup
 *	@ap: ATA interface
 *	@adev: device on the interface
 *
 *	Perform PIO mode setup.
 */

static void hpt366_set_piomode(struct ata_port *ap, struct ata_device *adev)
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
	if (fast & 0x80) {
		fast &= ~0x80;
		pci_write_config_byte(pdev, addr2, fast);
	}

	pci_read_config_dword(pdev, addr1, &reg);
	mode = hpt36x_find_mode(ap, adev->pio_mode);
	mode &= ~0x8000000;	/* No FIFO in PIO */
	mode &= ~0x30070000;	/* Leave config bits alone */
	reg &= 0x30070000;	/* Strip timing bits */
	pci_write_config_dword(pdev, addr1, reg | mode);
}

/**
 *	hpt366_set_dmamode		-	DMA timing setup
 *	@ap: ATA interface
 *	@adev: Device being configured
 *
 *	Set up the channel for MWDMA or UDMA modes. Much the same as with
 *	PIO, load the mode number and then set MWDMA or UDMA flag.
 */

static void hpt366_set_dmamode(struct ata_port *ap, struct ata_device *adev)
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
	if (fast & 0x80) {
		fast &= ~0x80;
		pci_write_config_byte(pdev, addr2, fast);
	}

	pci_read_config_dword(pdev, addr1, &reg);
	mode = hpt36x_find_mode(ap, adev->dma_mode);
	mode |= 0x8000000;	/* FIFO in MWDMA or UDMA */
	mode &= ~0xC0000000;	/* Leave config bits alone */
	reg &= 0xC0000000;	/* Strip timing bits */
	pci_write_config_dword(pdev, addr1, reg | mode);
}

static struct scsi_host_template hpt36x_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

/*
 *	Configuration for HPT366/68
 */

static struct ata_port_operations hpt366_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= hpt366_set_piomode,
	.set_dmamode	= hpt366_set_dmamode,
	.mode_filter	= hpt366_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= hpt36x_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_pio_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,

	.port_start	= ata_port_start,
	.port_stop	= ata_port_stop,
	.host_stop	= ata_host_stop
};

/**
 *	hpt36x_init_one		-	Initialise an HPT366/368
 *	@dev: PCI device
 *	@id: Entry in match table
 *
 *	Initialise an HPT36x device. There are some interesting complications
 *	here. Firstly the chip may report 366 and be one of several variants.
 *	Secondly all the timings depend on the clock for the chip which we must
 *	detect and look up
 *
 *	This is the known chip mappings. It may be missing a couple of later
 *	releases.
 *
 *	Chip version		PCI		Rev	Notes
 *	HPT366			4 (HPT366)	0	UDMA66
 *	HPT366			4 (HPT366)	1	UDMA66
 *	HPT368			4 (HPT366)	2	UDMA66
 *	HPT37x/30x		4 (HPT366)	3+	Other driver
 *
 */

static int hpt36x_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static struct ata_port_info info_hpt366 = {
		.sht = &hpt36x_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x1f,
		.port_ops = &hpt366_port_ops
	};
	struct ata_port_info *port_info[2] = {&info_hpt366, &info_hpt366};

	u32 class_rev;
	u32 reg1;
	u8 drive_fast;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xFF;

	/* May be a later chip in disguise. Check */
	/* Newer chips are not in the HPT36x driver. Ignore them */
	if (class_rev > 2)
			return -ENODEV;

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, (L1_CACHE_BYTES / 4));
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x78);
	pci_write_config_byte(dev, PCI_MIN_GNT, 0x08);
	pci_write_config_byte(dev, PCI_MAX_LAT, 0x08);

	pci_read_config_byte(dev, 0x51, &drive_fast);
	if (drive_fast & 0x80)
		pci_write_config_byte(dev, 0x51, drive_fast & ~0x80);

	pci_read_config_dword(dev, 0x40,  &reg1);

	/* PCI clocking determines the ATA timing values to use */
	/* info_hpt366 is safe against re-entry so we can scribble on it */
	switch((reg1 & 0x700) >> 8) {
		case 5:
			info_hpt366.private_data = &hpt366_40;
			break;
		case 9:
			info_hpt366.private_data = &hpt366_25;
			break;
		default:
			info_hpt366.private_data = &hpt366_33;
			break;
	}
	/* Now kick off ATA set up */
	return ata_pci_init_one(dev, port_info, 2);
}

static const struct pci_device_id hpt36x[] = {
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT366), },

	{ },
};

static struct pci_driver hpt36x_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= hpt36x,
	.probe 		= hpt36x_init_one,
	.remove		= ata_pci_remove_one
};

static int __init hpt36x_init(void)
{
	return pci_register_driver(&hpt36x_pci_driver);
}


static void __exit hpt36x_exit(void)
{
	pci_unregister_driver(&hpt36x_pci_driver);
}


MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for the Highpoint HPT366/368");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, hpt36x);
MODULE_VERSION(DRV_VERSION);

module_init(hpt36x_init);
module_exit(hpt36x_exit);
