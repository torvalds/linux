/*
 * Libata driver for the highpoint 37x and 30x UDMA66 ATA controllers.
 *
 * This driver is heavily based upon:
 *
 * linux/drivers/ide/pci/hpt366.c		Version 0.36	April 25, 2003
 *
 * Copyright (C) 1999-2003		Andre Hedrick <andre@linux-ide.org>
 * Portions Copyright (C) 2001	        Sun Microsystems, Inc.
 * Portions Copyright (C) 2003		Red Hat Inc
 *
 * TODO
 *	PLL mode
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

#define DRV_NAME	"pata_hpt37x"
#define DRV_VERSION	"0.5"

struct hpt_clock {
	u8	xfer_speed;
	u32	timing;
};

struct hpt_chip {
	const char *name;
	unsigned int base;
	struct hpt_clock const *clocks[4];
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

/* from highpoint documentation. these are old values */
static const struct hpt_clock hpt370_timings_33[] = {
/*	{	XFER_UDMA_5,	0x1A85F442,	0x16454e31	}, */
	{	XFER_UDMA_5,	0x16454e31	},
	{	XFER_UDMA_4,	0x16454e31	},
	{	XFER_UDMA_3,	0x166d4e31	},
	{	XFER_UDMA_2,	0x16494e31	},
	{	XFER_UDMA_1,	0x164d4e31	},
	{	XFER_UDMA_0,	0x16514e31	},

	{	XFER_MW_DMA_2,	0x26514e21	},
	{	XFER_MW_DMA_1,	0x26514e33	},
	{	XFER_MW_DMA_0,	0x26514e97	},

	{	XFER_PIO_4,	0x06514e21	},
	{	XFER_PIO_3,	0x06514e22	},
	{	XFER_PIO_2,	0x06514e33	},
	{	XFER_PIO_1,	0x06914e43	},
	{	XFER_PIO_0,	0x06914e57	},
	{	0,		0x06514e57	}
};

static const struct hpt_clock hpt370_timings_66[] = {
	{	XFER_UDMA_5,	0x14846231	},
	{	XFER_UDMA_4,	0x14886231	},
	{	XFER_UDMA_3,	0x148c6231	},
	{	XFER_UDMA_2,	0x148c6231	},
	{	XFER_UDMA_1,	0x14906231	},
	{	XFER_UDMA_0,	0x14986231	},

	{	XFER_MW_DMA_2,	0x26514e21	},
	{	XFER_MW_DMA_1,	0x26514e33	},
	{	XFER_MW_DMA_0,	0x26514e97	},

	{	XFER_PIO_4,	0x06514e21	},
	{	XFER_PIO_3,	0x06514e22	},
	{	XFER_PIO_2,	0x06514e33	},
	{	XFER_PIO_1,	0x06914e43	},
	{	XFER_PIO_0,	0x06914e57	},
	{	0,		0x06514e57	}
};

/* these are the current (4 sep 2001) timings from highpoint */
static const struct hpt_clock hpt370a_timings_33[] = {
	{	XFER_UDMA_5,	0x12446231	},
	{	XFER_UDMA_4,	0x12446231	},
	{	XFER_UDMA_3,	0x126c6231	},
	{	XFER_UDMA_2,	0x12486231	},
	{	XFER_UDMA_1,	0x124c6233	},
	{	XFER_UDMA_0,	0x12506297	},

	{	XFER_MW_DMA_2,	0x22406c31	},
	{	XFER_MW_DMA_1,	0x22406c33	},
	{	XFER_MW_DMA_0,	0x22406c97	},

	{	XFER_PIO_4,	0x06414e31	},
	{	XFER_PIO_3,	0x06414e42	},
	{	XFER_PIO_2,	0x06414e53	},
	{	XFER_PIO_1,	0x06814e93	},
	{	XFER_PIO_0,	0x06814ea7	},
	{	0,		0x06814ea7	}
};

/* 2x 33MHz timings */
static const struct hpt_clock hpt370a_timings_66[] = {
	{	XFER_UDMA_5,	0x1488e673	},
	{	XFER_UDMA_4,	0x1488e673	},
	{	XFER_UDMA_3,	0x1498e673	},
	{	XFER_UDMA_2,	0x1490e673	},
	{	XFER_UDMA_1,	0x1498e677	},
	{	XFER_UDMA_0,	0x14a0e73f	},

	{	XFER_MW_DMA_2,	0x2480fa73	},
	{	XFER_MW_DMA_1,	0x2480fa77	},
	{	XFER_MW_DMA_0,	0x2480fb3f	},

	{	XFER_PIO_4,	0x0c82be73	},
	{	XFER_PIO_3,	0x0c82be95	},
	{	XFER_PIO_2,	0x0c82beb7	},
	{	XFER_PIO_1,	0x0d02bf37	},
	{	XFER_PIO_0,	0x0d02bf5f	},
	{	0,		0x0d02bf5f	}
};

static const struct hpt_clock hpt370a_timings_50[] = {
	{	XFER_UDMA_5,	0x12848242	},
	{	XFER_UDMA_4,	0x12ac8242	},
	{	XFER_UDMA_3,	0x128c8242	},
	{	XFER_UDMA_2,	0x120c8242	},
	{	XFER_UDMA_1,	0x12148254	},
	{	XFER_UDMA_0,	0x121882ea	},

	{	XFER_MW_DMA_2,	0x22808242	},
	{	XFER_MW_DMA_1,	0x22808254	},
	{	XFER_MW_DMA_0,	0x228082ea	},

	{	XFER_PIO_4,	0x0a81f442	},
	{	XFER_PIO_3,	0x0a81f443	},
	{	XFER_PIO_2,	0x0a81f454	},
	{	XFER_PIO_1,	0x0ac1f465	},
	{	XFER_PIO_0,	0x0ac1f48a	},
	{	0,		0x0ac1f48a	}
};

static const struct hpt_clock hpt372_timings_33[] = {
	{	XFER_UDMA_6,	0x1c81dc62	},
	{	XFER_UDMA_5,	0x1c6ddc62	},
	{	XFER_UDMA_4,	0x1c8ddc62	},
	{	XFER_UDMA_3,	0x1c8edc62	},	/* checkme */
	{	XFER_UDMA_2,	0x1c91dc62	},
	{	XFER_UDMA_1,	0x1c9adc62	},	/* checkme */
	{	XFER_UDMA_0,	0x1c82dc62	},	/* checkme */

	{	XFER_MW_DMA_2,	0x2c829262	},
	{	XFER_MW_DMA_1,	0x2c829266	},	/* checkme */
	{	XFER_MW_DMA_0,	0x2c82922e	},	/* checkme */

	{	XFER_PIO_4,	0x0c829c62	},
	{	XFER_PIO_3,	0x0c829c84	},
	{	XFER_PIO_2,	0x0c829ca6	},
	{	XFER_PIO_1,	0x0d029d26	},
	{	XFER_PIO_0,	0x0d029d5e	},
	{	0,		0x0d029d5e	}
};

static const struct hpt_clock hpt372_timings_50[] = {
	{	XFER_UDMA_5,	0x12848242	},
	{	XFER_UDMA_4,	0x12ac8242	},
	{	XFER_UDMA_3,	0x128c8242	},
	{	XFER_UDMA_2,	0x120c8242	},
	{	XFER_UDMA_1,	0x12148254	},
	{	XFER_UDMA_0,	0x121882ea	},

	{	XFER_MW_DMA_2,	0x22808242	},
	{	XFER_MW_DMA_1,	0x22808254	},
	{	XFER_MW_DMA_0,	0x228082ea	},

	{	XFER_PIO_4,	0x0a81f442	},
	{	XFER_PIO_3,	0x0a81f443	},
	{	XFER_PIO_2,	0x0a81f454	},
	{	XFER_PIO_1,	0x0ac1f465	},
	{	XFER_PIO_0,	0x0ac1f48a	},
	{	0,		0x0a81f443	}
};

static const struct hpt_clock hpt372_timings_66[] = {
	{	XFER_UDMA_6,	0x1c869c62	},
	{	XFER_UDMA_5,	0x1cae9c62	},
	{	XFER_UDMA_4,	0x1c8a9c62	},
	{	XFER_UDMA_3,	0x1c8e9c62	},
	{	XFER_UDMA_2,	0x1c929c62	},
	{	XFER_UDMA_1,	0x1c9a9c62	},
	{	XFER_UDMA_0,	0x1c829c62	},

	{	XFER_MW_DMA_2,	0x2c829c62	},
	{	XFER_MW_DMA_1,	0x2c829c66	},
	{	XFER_MW_DMA_0,	0x2c829d2e	},

	{	XFER_PIO_4,	0x0c829c62	},
	{	XFER_PIO_3,	0x0c829c84	},
	{	XFER_PIO_2,	0x0c829ca6	},
	{	XFER_PIO_1,	0x0d029d26	},
	{	XFER_PIO_0,	0x0d029d5e	},
	{	0,		0x0d029d26	}
};

static const struct hpt_clock hpt374_timings_33[] = {
	{	XFER_UDMA_6,	0x12808242	},
	{	XFER_UDMA_5,	0x12848242	},
	{	XFER_UDMA_4,	0x12ac8242	},
	{	XFER_UDMA_3,	0x128c8242	},
	{	XFER_UDMA_2,	0x120c8242	},
	{	XFER_UDMA_1,	0x12148254	},
	{	XFER_UDMA_0,	0x121882ea	},

	{	XFER_MW_DMA_2,	0x22808242	},
	{	XFER_MW_DMA_1,	0x22808254	},
	{	XFER_MW_DMA_0,	0x228082ea	},

	{	XFER_PIO_4,	0x0a81f442	},
	{	XFER_PIO_3,	0x0a81f443	},
	{	XFER_PIO_2,	0x0a81f454	},
	{	XFER_PIO_1,	0x0ac1f465	},
	{	XFER_PIO_0,	0x0ac1f48a	},
	{	0,		0x06814e93	}
};

static const struct hpt_chip hpt370 = {
	"HPT370",
	48,
	{
		hpt370_timings_33,
		NULL,
		NULL,
		hpt370_timings_66
	}
};

static const struct hpt_chip hpt370a = {
	"HPT370A",
	48,
	{
		hpt370a_timings_33,
		NULL,
		hpt370a_timings_50,
		hpt370a_timings_66
	}
};

static const struct hpt_chip hpt372 = {
	"HPT372",
	55,
	{
		hpt372_timings_33,
		NULL,
		hpt372_timings_50,
		hpt372_timings_66
	}
};

static const struct hpt_chip hpt302 = {
	"HPT302",
	66,
	{
		hpt372_timings_33,
		NULL,
		hpt372_timings_50,
		hpt372_timings_66
	}
};

static const struct hpt_chip hpt371 = {
	"HPT371",
	66,
	{
		hpt372_timings_33,
		NULL,
		hpt372_timings_50,
		hpt372_timings_66
	}
};

static const struct hpt_chip hpt372a = {
	"HPT372A",
	66,
	{
		hpt372_timings_33,
		NULL,
		hpt372_timings_50,
		hpt372_timings_66
	}
};

static const struct hpt_chip hpt374 = {
	"HPT374",
	48,
	{
		hpt374_timings_33,
		NULL,
		NULL,
		NULL
	}
};

/**
 *	hpt37x_find_mode	-	reset the hpt37x bus
 *	@ap: ATA port
 *	@speed: transfer mode
 *
 *	Return the 32bit register programming information for this channel
 *	that matches the speed provided.
 */

static u32 hpt37x_find_mode(struct ata_port *ap, int speed)
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

static int hpt_dma_blacklisted(const struct ata_device *dev, char *modestr, const char *list[])
{
	unsigned char model_num[40];
	char *s;
	unsigned int len;
	int i = 0;

	ata_id_string(dev->id, model_num, ATA_ID_PROD_OFS,
			  sizeof(model_num));
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

static const char *bad_ata100_5[] = {
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

/**
 *	hpt370_filter	-	mode selection filter
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Block UDMA on devices that cause trouble with this controller.
 */

static unsigned long hpt370_filter(const struct ata_port *ap, struct ata_device *adev, unsigned long mask)
{
	if (adev->class != ATA_DEV_ATA) {
		if (hpt_dma_blacklisted(adev, "UDMA", bad_ata33))
			mask &= ~ATA_MASK_UDMA;
		if (hpt_dma_blacklisted(adev, "UDMA100", bad_ata100_5))
			mask &= ~(0x1F << ATA_SHIFT_UDMA);
	}
	return ata_pci_default_filter(ap, adev, mask);
}

/**
 *	hpt370a_filter	-	mode selection filter
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Block UDMA on devices that cause trouble with this controller.
 */

static unsigned long hpt370a_filter(const struct ata_port *ap, struct ata_device *adev, unsigned long mask)
{
	if (adev->class != ATA_DEV_ATA) {
		if (hpt_dma_blacklisted(adev, "UDMA100", bad_ata100_5))
			mask &= ~ (0x1F << ATA_SHIFT_UDMA);
	}
	return ata_pci_default_filter(ap, adev, mask);
}

/**
 *	hpt37x_pre_reset	-	reset the hpt37x bus
 *	@ap: ATA port to reset
 *
 *	Perform the initial reset handling for the 370/372 and 374 func 0
 */

static int hpt37x_pre_reset(struct ata_port *ap)
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
		ap->cbl = ATA_CBL_PATA40;
	else
		ap->cbl = ATA_CBL_PATA80;

	/* Reset the state machine */
	pci_write_config_byte(pdev, 0x50, 0x37);
	pci_write_config_byte(pdev, 0x54, 0x37);
	udelay(100);

	return ata_std_prereset(ap);
}

/**
 *	hpt37x_error_handler	-	reset the hpt374
 *	@ap: ATA port to reset
 *
 *	Perform probe for HPT37x, except for HPT374 channel 2
 */

static void hpt37x_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, hpt37x_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

static int hpt374_pre_reset(struct ata_port *ap)
{
	u16 mcr3, mcr6;
	u8 ata66;

	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	/* Do the extra channel work */
	pci_read_config_word(pdev, 0x52, &mcr3);
	pci_read_config_word(pdev, 0x56, &mcr6);
	/* Set bit 15 of 0x52 to enable TCBLID as input
	   Set bit 15 of 0x56 to enable FCBLID as input
	 */
	pci_write_config_word(pdev, 0x52, mcr3 | 0x8000);
	pci_write_config_word(pdev, 0x56, mcr6 | 0x8000);
	pci_read_config_byte(pdev, 0x5A, &ata66);
	/* Reset TCBLID/FCBLID to output */
	pci_write_config_word(pdev, 0x52, mcr3);
	pci_write_config_word(pdev, 0x56, mcr6);

	if (ata66 & (1 << ap->port_no))
		ap->cbl = ATA_CBL_PATA40;
	else
		ap->cbl = ATA_CBL_PATA80;

	/* Reset the state machine */
	pci_write_config_byte(pdev, 0x50, 0x37);
	pci_write_config_byte(pdev, 0x54, 0x37);
	udelay(100);

	return ata_std_prereset(ap);
}

/**
 *	hpt374_error_handler	-	reset the hpt374
 *	@classes:
 *
 *	The 374 cable detect is a little different due to the extra
 *	channels. The function 0 channels work like usual but function 1
 *	is special
 */

static void hpt374_error_handler(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	if (!(PCI_FUNC(pdev->devfn) & 1))
		hpt37x_error_handler(ap);
	else
		ata_bmdma_drive_eh(ap, hpt374_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	hpt370_set_piomode		-	PIO setup
 *	@ap: ATA interface
 *	@adev: device on the interface
 *
 *	Perform PIO mode setup.
 */

static void hpt370_set_piomode(struct ata_port *ap, struct ata_device *adev)
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
	fast &= ~0x02;
	fast |= 0x01;
	pci_write_config_byte(pdev, addr2, fast);

	pci_read_config_dword(pdev, addr1, &reg);
	mode = hpt37x_find_mode(ap, adev->pio_mode);
	mode &= ~0x8000000;	/* No FIFO in PIO */
	mode &= ~0x30070000;	/* Leave config bits alone */
	reg &= 0x30070000;	/* Strip timing bits */
	pci_write_config_dword(pdev, addr1, reg | mode);
}

/**
 *	hpt370_set_dmamode		-	DMA timing setup
 *	@ap: ATA interface
 *	@adev: Device being configured
 *
 *	Set up the channel for MWDMA or UDMA modes. Much the same as with
 *	PIO, load the mode number and then set MWDMA or UDMA flag.
 */

static void hpt370_set_dmamode(struct ata_port *ap, struct ata_device *adev)
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
	fast &= ~0x02;
	fast |= 0x01;
	pci_write_config_byte(pdev, addr2, fast);

	pci_read_config_dword(pdev, addr1, &reg);
	mode = hpt37x_find_mode(ap, adev->dma_mode);
	mode |= 0x8000000;	/* FIFO in MWDMA or UDMA */
	mode &= ~0xC0000000;	/* Leave config bits alone */
	reg &= 0xC0000000;	/* Strip timing bits */
	pci_write_config_dword(pdev, addr1, reg | mode);
}

/**
 *	hpt370_bmdma_start		-	DMA engine begin
 *	@qc: ATA command
 *
 *	The 370 and 370A want us to reset the DMA engine each time we
 *	use it. The 372 and later are fine.
 */

static void hpt370_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	pci_write_config_byte(pdev, 0x50 + 4 * ap->port_no, 0x37);
	udelay(10);
	ata_bmdma_start(qc);
}

/**
 *	hpt370_bmdma_end		-	DMA engine stop
 *	@qc: ATA command
 *
 *	Work around the HPT370 DMA engine.
 */

static void hpt370_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 dma_stat = inb(ap->ioaddr.bmdma_addr + 2);
	u8 dma_cmd;
	unsigned long bmdma = ap->ioaddr.bmdma_addr;

	if (dma_stat & 0x01) {
		udelay(20);
		dma_stat = inb(bmdma + 2);
	}
	if (dma_stat & 0x01) {
		/* Clear the engine */
		pci_write_config_byte(pdev, 0x50 + 4 * ap->port_no, 0x37);
		udelay(10);
		/* Stop DMA */
		dma_cmd = inb(bmdma );
		outb(dma_cmd & 0xFE, bmdma);
		/* Clear Error */
		dma_stat = inb(bmdma + 2);
		outb(dma_stat | 0x06 , bmdma + 2);
		/* Clear the engine */
		pci_write_config_byte(pdev, 0x50 + 4 * ap->port_no, 0x37);
		udelay(10);
	}
	ata_bmdma_stop(qc);
}

/**
 *	hpt372_set_piomode		-	PIO setup
 *	@ap: ATA interface
 *	@adev: device on the interface
 *
 *	Perform PIO mode setup.
 */

static void hpt372_set_piomode(struct ata_port *ap, struct ata_device *adev)
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
	mode = hpt37x_find_mode(ap, adev->pio_mode);

	printk("Find mode for %d reports %X\n", adev->pio_mode, mode);
	mode &= ~0x80000000;	/* No FIFO in PIO */
	mode &= ~0x30070000;	/* Leave config bits alone */
	reg &= 0x30070000;	/* Strip timing bits */
	pci_write_config_dword(pdev, addr1, reg | mode);
}

/**
 *	hpt372_set_dmamode		-	DMA timing setup
 *	@ap: ATA interface
 *	@adev: Device being configured
 *
 *	Set up the channel for MWDMA or UDMA modes. Much the same as with
 *	PIO, load the mode number and then set MWDMA or UDMA flag.
 */

static void hpt372_set_dmamode(struct ata_port *ap, struct ata_device *adev)
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
	mode = hpt37x_find_mode(ap, adev->dma_mode);
	printk("Find mode for DMA %d reports %X\n", adev->dma_mode, mode);
	mode &= ~0xC0000000;	/* Leave config bits alone */
	mode |= 0x80000000;	/* FIFO in MWDMA or UDMA */
	reg &= 0xC0000000;	/* Strip timing bits */
	pci_write_config_dword(pdev, addr1, reg | mode);
}

/**
 *	hpt37x_bmdma_end		-	DMA engine stop
 *	@qc: ATA command
 *
 *	Clean up after the HPT372 and later DMA engine
 */

static void hpt37x_bmdma_stop(struct ata_queued_cmd *qc)
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


static struct scsi_host_template hpt37x_sht = {
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
 *	Configuration for HPT370
 */

static struct ata_port_operations hpt370_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= hpt370_set_piomode,
	.set_dmamode	= hpt370_set_dmamode,
	.mode_filter	= hpt370_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= hpt37x_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= hpt370_bmdma_start,
	.bmdma_stop	= hpt370_bmdma_stop,
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

/*
 *	Configuration for HPT370A. Close to 370 but less filters
 */

static struct ata_port_operations hpt370a_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= hpt370_set_piomode,
	.set_dmamode	= hpt370_set_dmamode,
	.mode_filter	= hpt370a_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= hpt37x_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= hpt370_bmdma_start,
	.bmdma_stop	= hpt370_bmdma_stop,
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

/*
 *	Configuration for HPT372, HPT371, HPT302. Slightly different PIO
 *	and DMA mode setting functionality.
 */

static struct ata_port_operations hpt372_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= hpt372_set_piomode,
	.set_dmamode	= hpt372_set_dmamode,
	.mode_filter	= ata_pci_default_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= hpt37x_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= hpt37x_bmdma_stop,
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

/*
 *	Configuration for HPT374. Mode setting works like 372 and friends
 *	but we have a different cable detection procedure.
 */

static struct ata_port_operations hpt374_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= hpt372_set_piomode,
	.set_dmamode	= hpt372_set_dmamode,
	.mode_filter	= ata_pci_default_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= hpt374_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= hpt37x_bmdma_stop,
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
 *	htp37x_clock_slot	-	Turn timing to PC clock entry
 *	@freq: Reported frequency timing
 *	@base: Base timing
 *
 *	Turn the timing data intoa clock slot (0 for 33, 1 for 40, 2 for 50
 *	and 3 for 66Mhz)
 */

static int hpt37x_clock_slot(unsigned int freq, unsigned int base)
{
	unsigned int f = (base * freq) / 192;	/* Mhz */
	if (f < 40)
		return 0;	/* 33Mhz slot */
	if (f < 45)
		return 1;	/* 40Mhz slot */
	if (f < 55)
		return 2;	/* 50Mhz slot */
	return 3;		/* 60Mhz slot */
}

/**
 *	hpt37x_calibrate_dpll		-	Calibrate the DPLL loop
 *	@dev: PCI device
 *
 *	Perform a calibration cycle on the HPT37x DPLL. Returns 1 if this
 *	succeeds
 */

static int hpt37x_calibrate_dpll(struct pci_dev *dev)
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
/**
 *	hpt37x_init_one		-	Initialise an HPT37X/302
 *	@dev: PCI device
 *	@id: Entry in match table
 *
 *	Initialise an HPT37x device. There are some interesting complications
 *	here. Firstly the chip may report 366 and be one of several variants.
 *	Secondly all the timings depend on the clock for the chip which we must
 *	detect and look up
 *
 *	This is the known chip mappings. It may be missing a couple of later
 *	releases.
 *
 *	Chip version		PCI		Rev	Notes
 *	HPT366			4 (HPT366)	0	Other driver
 *	HPT366			4 (HPT366)	1	Other driver
 *	HPT368			4 (HPT366)	2	Other driver
 *	HPT370			4 (HPT366)	3	UDMA100
 *	HPT370A			4 (HPT366)	4	UDMA100
 *	HPT372			4 (HPT366)	5	UDMA133 (1)
 *	HPT372N			4 (HPT366)	6	Other driver
 *	HPT372A			5 (HPT372)	1	UDMA133 (1)
 *	HPT372N			5 (HPT372)	2	Other driver
 *	HPT302			6 (HPT302)	1	UDMA133
 *	HPT302N			6 (HPT302)	2	Other driver
 *	HPT371			7 (HPT371)	*	UDMA133
 *	HPT374			8 (HPT374)	*	UDMA133 4 channel
 *	HPT372N			9 (HPT372N)	*	Other driver
 *
 *	(1) UDMA133 support depends on the bus clock
 */

static int hpt37x_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	/* HPT370 - UDMA100 */
	static struct ata_port_info info_hpt370 = {
		.sht = &hpt37x_sht,
		.flags = ATA_FLAG_SLAVE_POSS|ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x3f,
		.port_ops = &hpt370_port_ops
	};
	/* HPT370A - UDMA100 */
	static struct ata_port_info info_hpt370a = {
		.sht = &hpt37x_sht,
		.flags = ATA_FLAG_SLAVE_POSS|ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x3f,
		.port_ops = &hpt370a_port_ops
	};
	/* HPT371, 372 and friends - UDMA133 */
	static struct ata_port_info info_hpt372 = {
		.sht = &hpt37x_sht,
		.flags = ATA_FLAG_SLAVE_POSS|ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x7f,
		.port_ops = &hpt372_port_ops
	};
	/* HPT371, 372 and friends - UDMA100 at 50MHz clock */
	static struct ata_port_info info_hpt372_50 = {
		.sht = &hpt37x_sht,
		.flags = ATA_FLAG_SLAVE_POSS|ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x3f,
		.port_ops = &hpt372_port_ops
	};
	/* HPT374 - UDMA133 */
	static struct ata_port_info info_hpt374 = {
		.sht = &hpt37x_sht,
		.flags = ATA_FLAG_SLAVE_POSS|ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x7f,
		.port_ops = &hpt374_port_ops
	};

	static const int MHz[4] = { 33, 40, 50, 66 };

	struct ata_port_info *port_info[2];
	struct ata_port_info *port;

	u8 irqmask;
	u32 class_rev;
	u32 freq;

	const struct hpt_chip *chip_table;
	int clock_slot;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xFF;

	if (dev->device == PCI_DEVICE_ID_TTI_HPT366) {
		/* May be a later chip in disguise. Check */
		/* Older chips are in the HPT366 driver. Ignore them */
		if (class_rev < 3)
			return -ENODEV;
		/* N series chips have their own driver. Ignore */
		if (class_rev == 6)
			return -ENODEV;

		switch(class_rev) {
			case 3:
				port = &info_hpt370;
				chip_table = &hpt370;
				break;
			case 4:
				port = &info_hpt370a;
				chip_table = &hpt370a;
				break;
			case 5:
				port = &info_hpt372;
				chip_table = &hpt372;
				break;
			default:
				printk(KERN_ERR "pata_hpt37x: Unknown HPT366 subtype please report (%d).\n", class_rev);
				return -ENODEV;
		}
	} else {
		switch(dev->device) {
			case PCI_DEVICE_ID_TTI_HPT372:
				/* 372N if rev >= 2*/
				if (class_rev >= 2)
					return -ENODEV;
				port = &info_hpt372;
				chip_table = &hpt372a;
				break;
			case PCI_DEVICE_ID_TTI_HPT302:
				/* 302N if rev > 1 */
				if (class_rev > 1)
					return -ENODEV;
				port = &info_hpt372;
				/* Check this */
				chip_table = &hpt302;
				break;
			case PCI_DEVICE_ID_TTI_HPT371:
				port = &info_hpt372;
				chip_table = &hpt371;
				break;
			case PCI_DEVICE_ID_TTI_HPT374:
				chip_table = &hpt374;
				port = &info_hpt374;
				break;
			default:
				printk(KERN_ERR "pata_hpt37x: PCI table is bogus please report (%d).\n", dev->device);
				return -ENODEV;
		}
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
	 * default to pci clock. make sure MA15/16 are set to output
	 * to prevent drives having problems with 40-pin cables. Needed
	 * for some drives such as IBM-DTLA which will not enter ready
	 * state on reset when PDIAG is a input.
	 */

	pci_write_config_byte(dev, 0x5b, 0x23);

	pci_read_config_dword(dev, 0x70, &freq);
	if ((freq >> 12) != 0xABCDE) {
		int i;
		u8 sr;
		u32 total = 0;

		printk(KERN_WARNING "pata_hpt37x: BIOS has not set timing clocks.\n");

		/* This is the process the HPT371 BIOS is reported to use */
		for(i = 0; i < 128; i++) {
			pci_read_config_byte(dev, 0x78, &sr);
			total += sr;
			udelay(15);
		}
		freq = total / 128;
	}
	freq &= 0x1FF;

	/*
	 *	Turn the frequency check into a band and then find a timing
	 *	table to match it.
	 */

	clock_slot = hpt37x_clock_slot(freq, chip_table->base);
	if (chip_table->clocks[clock_slot] == NULL) {
		/*
		 *	We need to try PLL mode instead
		 */
		unsigned int f_low = (MHz[clock_slot] * chip_table->base) / 192;
		unsigned int f_high = f_low + 2;
		int adjust;

		for(adjust = 0; adjust < 8; adjust++) {
			if (hpt37x_calibrate_dpll(dev))
				break;
			/* See if it'll settle at a fractionally different clock */
			if ((adjust & 3) == 3) {
				f_low --;
				f_high ++;
			}
			pci_write_config_dword(dev, 0x5C, (f_high << 16) | f_low);
		}
		if (adjust == 8) {
			printk(KERN_WARNING "hpt37x: DPLL did not stabilize.\n");
			return -ENODEV;
		}
		/* Check if this works for all cases */
		port->private_data = (void *)hpt370_timings_66;

		printk(KERN_INFO "hpt37x: Bus clock %dMHz, using DPLL.\n", MHz[clock_slot]);
	} else {
		port->private_data = (void *)chip_table->clocks[clock_slot];
		/*
		 *	Perform a final fixup. The 371 and 372 clock determines
		 *	if UDMA133 is available.
		 */

		if (clock_slot == 2 && chip_table == &hpt372) {	/* 50Mhz */
			printk(KERN_WARNING "pata_hpt37x: No UDMA133 support available with 50MHz bus clock.\n");
			if (port == &info_hpt372)
				port = &info_hpt372_50;
			else BUG();
		}
		printk(KERN_INFO "hpt37x: %s: Bus clock %dMHz.\n", chip_table->name, MHz[clock_slot]);
	}
	port_info[0] = port_info[1] = port;
	/* Now kick off ATA set up */
	return ata_pci_init_one(dev, port_info, 2);
}

static const struct pci_device_id hpt37x[] = {
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT366), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT371), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT372), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT374), },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT302), },

	{ },
};

static struct pci_driver hpt37x_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= hpt37x,
	.probe 		= hpt37x_init_one,
	.remove		= ata_pci_remove_one
};

static int __init hpt37x_init(void)
{
	return pci_register_driver(&hpt37x_pci_driver);
}

static void __exit hpt37x_exit(void)
{
	pci_unregister_driver(&hpt37x_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for the Highpoint HPT37x/30x");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, hpt37x);
MODULE_VERSION(DRV_VERSION);

module_init(hpt37x_init);
module_exit(hpt37x_exit);
