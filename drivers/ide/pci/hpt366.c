/*
 * linux/drivers/ide/pci/hpt366.c		Version 0.36	April 25, 2003
 *
 * Copyright (C) 1999-2003		Andre Hedrick <andre@linux-ide.org>
 * Portions Copyright (C) 2001	        Sun Microsystems, Inc.
 * Portions Copyright (C) 2003		Red Hat Inc
 *
 * Thanks to HighPoint Technologies for their assistance, and hardware.
 * Special Thanks to Jon Burchmore in SanDiego for the deep pockets, his
 * donation of an ABit BP6 mainboard, processor, and memory acellerated
 * development and support.
 *
 *
 * Highpoint have their own driver (source except for the raid part)
 * available from http://www.highpoint-tech.com/hpt3xx-opensource-v131.tgz
 * This may be useful to anyone wanting to work on the mainstream hpt IDE.
 *
 * Note that final HPT370 support was done by force extraction of GPL.
 *
 * - add function for getting/setting power status of drive
 * - the HPT370's state machine can get confused. reset it before each dma 
 *   xfer to prevent that from happening.
 * - reset state engine whenever we get an error.
 * - check for busmaster state at end of dma. 
 * - use new highpoint timings.
 * - detect bus speed using highpoint register.
 * - use pll if we don't have a clock table. added a 66MHz table that's
 *   just 2x the 33MHz table.
 * - removed turnaround. NOTE: we never want to switch between pll and
 *   pci clocks as the chip can glitch in those cases. the highpoint
 *   approved workaround slows everything down too much to be useful. in
 *   addition, we would have to serialize access to each chip.
 * 	Adrian Sun <a.sun@sun.com>
 *
 * add drive timings for 66MHz PCI bus,
 * fix ATA Cable signal detection, fix incorrect /proc info
 * add /proc display for per-drive PIO/DMA/UDMA mode and
 * per-channel ATA-33/66 Cable detect.
 * 	Duncan Laurie <void@sun.com>
 *
 * fixup /proc output for multiple controllers
 *	Tim Hockin <thockin@sun.com>
 *
 * On hpt366: 
 * Reset the hpt366 on error, reset on dma
 * Fix disabling Fast Interrupt hpt366.
 * 	Mike Waychison <crlf@sun.com>
 *
 * Added support for 372N clocking and clock switching. The 372N needs
 * different clocks on read/write. This requires overloading rw_disk and
 * other deeply crazy things. Thanks to <http://www.hoerstreich.de> for
 * keeping me sane. 
 *		Alan Cox <alan@redhat.com>
 *
 */


#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>

/* various tuning parameters */
#define HPT_RESET_STATE_ENGINE
#undef HPT_DELAY_INTERRUPT
#undef HPT_SERIALIZE_IO

static const char *quirk_drives[] = {
	"QUANTUM FIREBALLlct08 08",
	"QUANTUM FIREBALLP KA6.4",
	"QUANTUM FIREBALLP LM20.4",
	"QUANTUM FIREBALLP LM20.5",
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

struct chipset_bus_clock_list_entry {
	u8		xfer_speed;
	unsigned int	chipset_settings;
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
static struct chipset_bus_clock_list_entry forty_base_hpt366[] = {
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

static struct chipset_bus_clock_list_entry thirty_three_base_hpt366[] = {
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

static struct chipset_bus_clock_list_entry twenty_five_base_hpt366[] = {
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

/* from highpoint documentation. these are old values */
static struct chipset_bus_clock_list_entry thirty_three_base_hpt370[] = {
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

static struct chipset_bus_clock_list_entry sixty_six_base_hpt370[] = {
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
static struct chipset_bus_clock_list_entry thirty_three_base_hpt370a[] = {
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
static struct chipset_bus_clock_list_entry sixty_six_base_hpt370a[] = {
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

static struct chipset_bus_clock_list_entry fifty_base_hpt370a[] = {
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

static struct chipset_bus_clock_list_entry thirty_three_base_hpt372[] = {
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

static struct chipset_bus_clock_list_entry fifty_base_hpt372[] = {
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

static struct chipset_bus_clock_list_entry sixty_six_base_hpt372[] = {
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

static struct chipset_bus_clock_list_entry thirty_three_base_hpt374[] = {
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

/* FIXME: 50MHz timings for HPT374 */

#if 0
static struct chipset_bus_clock_list_entry sixty_six_base_hpt374[] = {
	{	XFER_UDMA_6,	0x12406231	},	/* checkme */
	{	XFER_UDMA_5,	0x12446231	},	/* 0x14846231 */
	{	XFER_UDMA_4,	0x16814ea7	},	/* 0x14886231 */
	{	XFER_UDMA_3,	0x16814ea7	},	/* 0x148c6231 */
	{	XFER_UDMA_2,	0x16814ea7	},	/* 0x148c6231 */
	{	XFER_UDMA_1,	0x16814ea7	},	/* 0x14906231 */
	{	XFER_UDMA_0,	0x16814ea7	},	/* 0x14986231 */
	{	XFER_MW_DMA_2,	0x16814ea7	},	/* 0x26514e21 */
	{	XFER_MW_DMA_1,	0x16814ea7	},	/* 0x26514e97 */
	{	XFER_MW_DMA_0,	0x16814ea7	},	/* 0x26514e97 */
	{	XFER_PIO_4,	0x06814ea7	},	/* 0x06514e21 */
	{	XFER_PIO_3,	0x06814ea7	},	/* 0x06514e22 */
	{	XFER_PIO_2,	0x06814ea7	},	/* 0x06514e33 */
	{	XFER_PIO_1,	0x06814ea7	},	/* 0x06914e43 */
	{	XFER_PIO_0,	0x06814ea7	},	/* 0x06914e57 */
	{	0,		0x06814ea7	}
};
#endif

#define HPT366_DEBUG_DRIVE_INFO		0
#define HPT374_ALLOW_ATA133_6		0
#define HPT371_ALLOW_ATA133_6		0
#define HPT302_ALLOW_ATA133_6		0
#define HPT372_ALLOW_ATA133_6		1
#define HPT370_ALLOW_ATA100_5		1
#define HPT366_ALLOW_ATA66_4		1
#define HPT366_ALLOW_ATA66_3		1
#define HPT366_MAX_DEVS			8

#define F_LOW_PCI_33	0x23
#define F_LOW_PCI_40	0x29
#define F_LOW_PCI_50	0x2d
#define F_LOW_PCI_66	0x42

/*
 *	Hold all the highpoint quirks and revision information in one
 *	place.
 */

struct hpt_info
{
	u8 max_mode;		/* Speeds allowed */
	int revision;		/* Chipset revision */
	int flags;		/* Chipset properties */
#define PLL_MODE	1
#define IS_372N		2
				/* Speed table */
	struct chipset_bus_clock_list_entry *speed;
};

/*
 *	This wants fixing so that we do everything not by classrev
 *	(which breaks on the newest chips) but by creating an
 *	enumeration of chip variants and using that
 */

static __devinit u32 hpt_revision (struct pci_dev *dev)
{
	u32 class_rev;
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	switch(dev->device) {
		/* Remap new 372N onto 372 */
		case PCI_DEVICE_ID_TTI_HPT372N:
			class_rev = PCI_DEVICE_ID_TTI_HPT372; break;
		case PCI_DEVICE_ID_TTI_HPT374:
			class_rev = PCI_DEVICE_ID_TTI_HPT374; break;
		case PCI_DEVICE_ID_TTI_HPT371:
			class_rev = PCI_DEVICE_ID_TTI_HPT371; break;
		case PCI_DEVICE_ID_TTI_HPT302:
			class_rev = PCI_DEVICE_ID_TTI_HPT302; break;
		case PCI_DEVICE_ID_TTI_HPT372:
			class_rev = PCI_DEVICE_ID_TTI_HPT372; break;
		default:
			break;
	}
	return class_rev;
}

static int check_in_drive_lists(ide_drive_t *drive, const char **list);

static u8 hpt3xx_ratemask (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct hpt_info *info	= ide_get_hwifdata(hwif);
	u8 mode			= 0;

	/* FIXME: TODO - move this to set info->mode once at boot */

	if (info->revision >= 8) {		/* HPT374 */
		mode = (HPT374_ALLOW_ATA133_6) ? 4 : 3;
	} else if (info->revision >= 7) {	/* HPT371 */
		mode = (HPT371_ALLOW_ATA133_6) ? 4 : 3;
	} else if (info->revision >= 6) {	/* HPT302 */
		mode = (HPT302_ALLOW_ATA133_6) ? 4 : 3;
	} else if (info->revision >= 5) {	/* HPT372 */
		mode = (HPT372_ALLOW_ATA133_6) ? 4 : 3;
	} else if (info->revision >= 4) {	/* HPT370A */
		mode = (HPT370_ALLOW_ATA100_5) ? 3 : 2;
	} else if (info->revision >= 3) {	/* HPT370 */
		mode = (HPT370_ALLOW_ATA100_5) ? 3 : 2;
		mode = (check_in_drive_lists(drive, bad_ata33)) ? 0 : mode;
	} else {				/* HPT366 and HPT368 */
		mode = (check_in_drive_lists(drive, bad_ata33)) ? 0 : 2;
	}
	if (!eighty_ninty_three(drive) && mode)
		mode = min(mode, (u8)1);
	return mode;
}

/*
 *	Note for the future; the SATA hpt37x we must set
 *	either PIO or UDMA modes 0,4,5
 */
 
static u8 hpt3xx_ratefilter (ide_drive_t *drive, u8 speed)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct hpt_info *info	= ide_get_hwifdata(hwif);
	u8 mode			= hpt3xx_ratemask(drive);

	if (drive->media != ide_disk)
		return min(speed, (u8)XFER_PIO_4);

	switch(mode) {
		case 0x04:
			speed = min(speed, (u8)XFER_UDMA_6);
			break;
		case 0x03:
			speed = min(speed, (u8)XFER_UDMA_5);
			if (info->revision >= 5)
				break;
			if (check_in_drive_lists(drive, bad_ata100_5))
				speed = min(speed, (u8)XFER_UDMA_4);
			break;
		case 0x02:
			speed = min(speed, (u8)XFER_UDMA_4);
	/*
	 * CHECK ME, Does this need to be set to 5 ??
	 */
			if (info->revision >= 3)
				break;
			if ((check_in_drive_lists(drive, bad_ata66_4)) ||
			    (!(HPT366_ALLOW_ATA66_4)))
				speed = min(speed, (u8)XFER_UDMA_3);
			if ((check_in_drive_lists(drive, bad_ata66_3)) ||
			    (!(HPT366_ALLOW_ATA66_3)))
				speed = min(speed, (u8)XFER_UDMA_2);
			break;
		case 0x01:
			speed = min(speed, (u8)XFER_UDMA_2);
	/*
	 * CHECK ME, Does this need to be set to 5 ??
	 */
			if (info->revision >= 3)
				break;
			if (check_in_drive_lists(drive, bad_ata33))
				speed = min(speed, (u8)XFER_MW_DMA_2);
			break;
		case 0x00:
		default:
			speed = min(speed, (u8)XFER_MW_DMA_2);
			break;
	}
	return speed;
}

static int check_in_drive_lists (ide_drive_t *drive, const char **list)
{
	struct hd_driveid *id = drive->id;

	if (quirk_drives == list) {
		while (*list)
			if (strstr(id->model, *list++))
				return 1;
	} else {
		while (*list)
			if (!strcmp(*list++,id->model))
				return 1;
	}
	return 0;
}

static unsigned int pci_bus_clock_list (u8 speed, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed)
			return chipset_table->chipset_settings;
	return chipset_table->chipset_settings;
}

static int hpt36x_tune_chipset(ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= hwif->pci_dev;
	struct hpt_info	*info	= ide_get_hwifdata(hwif);
	u8 speed		= hpt3xx_ratefilter(drive, xferspeed);
	u8 regtime		= (drive->select.b.unit & 0x01) ? 0x44 : 0x40;
	u8 regfast		= (hwif->channel) ? 0x55 : 0x51;
	u8 drive_fast		= 0;
	u32 reg1 = 0, reg2	= 0;

	/*
	 * Disable the "fast interrupt" prediction.
	 */
	pci_read_config_byte(dev, regfast, &drive_fast);
	if (drive_fast & 0x80)
		pci_write_config_byte(dev, regfast, drive_fast & ~0x80);

	reg2 = pci_bus_clock_list(speed, info->speed);

	/*
	 * Disable on-chip PIO FIFO/buffer
	 *  (to avoid problems handling I/O errors later)
	 */
	pci_read_config_dword(dev, regtime, &reg1);
	if (speed >= XFER_MW_DMA_0) {
		reg2 = (reg2 & ~0xc0000000) | (reg1 & 0xc0000000);
	} else {
		reg2 = (reg2 & ~0x30070000) | (reg1 & 0x30070000);
	}	
	reg2 &= ~0x80000000;

	pci_write_config_dword(dev, regtime, reg2);

	return ide_config_drive_speed(drive, speed);
}

static int hpt370_tune_chipset(ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev = hwif->pci_dev;
	struct hpt_info	*info	= ide_get_hwifdata(hwif);
	u8 speed	= hpt3xx_ratefilter(drive, xferspeed);
	u8 regfast	= (drive->hwif->channel) ? 0x55 : 0x51;
	u8 drive_pci	= 0x40 + (drive->dn * 4);
	u8 new_fast	= 0, drive_fast = 0;
	u32 list_conf	= 0, drive_conf = 0;
	u32 conf_mask	= (speed >= XFER_MW_DMA_0) ? 0xc0000000 : 0x30070000;

	/*
	 * Disable the "fast interrupt" prediction.
	 * don't holdoff on interrupts. (== 0x01 despite what the docs say) 
	 */
	pci_read_config_byte(dev, regfast, &drive_fast);
	new_fast = drive_fast;
	if (new_fast & 0x02)
		new_fast &= ~0x02;

#ifdef HPT_DELAY_INTERRUPT
	if (new_fast & 0x01)
		new_fast &= ~0x01;
#else
	if ((new_fast & 0x01) == 0)
		new_fast |= 0x01;
#endif
	if (new_fast != drive_fast)
		pci_write_config_byte(dev, regfast, new_fast);

	list_conf = pci_bus_clock_list(speed, info->speed);

	pci_read_config_dword(dev, drive_pci, &drive_conf);
	list_conf = (list_conf & ~conf_mask) | (drive_conf & conf_mask);
	
	if (speed < XFER_MW_DMA_0)
		list_conf &= ~0x80000000; /* Disable on-chip PIO FIFO/buffer */
	pci_write_config_dword(dev, drive_pci, list_conf);

	return ide_config_drive_speed(drive, speed);
}

static int hpt372_tune_chipset(ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= hwif->pci_dev;
	struct hpt_info	*info	= ide_get_hwifdata(hwif);
	u8 speed	= hpt3xx_ratefilter(drive, xferspeed);
	u8 regfast	= (drive->hwif->channel) ? 0x55 : 0x51;
	u8 drive_fast	= 0, drive_pci = 0x40 + (drive->dn * 4);
	u32 list_conf	= 0, drive_conf = 0;
	u32 conf_mask	= (speed >= XFER_MW_DMA_0) ? 0xc0000000 : 0x30070000;

	/*
	 * Disable the "fast interrupt" prediction.
	 * don't holdoff on interrupts. (== 0x01 despite what the docs say)
	 */
	pci_read_config_byte(dev, regfast, &drive_fast);
	drive_fast &= ~0x07;
	pci_write_config_byte(dev, regfast, drive_fast);

	list_conf = pci_bus_clock_list(speed, info->speed);
	pci_read_config_dword(dev, drive_pci, &drive_conf);
	list_conf = (list_conf & ~conf_mask) | (drive_conf & conf_mask);
	if (speed < XFER_MW_DMA_0)
		list_conf &= ~0x80000000; /* Disable on-chip PIO FIFO/buffer */
	pci_write_config_dword(dev, drive_pci, list_conf);

	return ide_config_drive_speed(drive, speed);
}

static int hpt3xx_tune_chipset (ide_drive_t *drive, u8 speed)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct hpt_info	*info	= ide_get_hwifdata(hwif);

	if (info->revision >= 8)
		return hpt372_tune_chipset(drive, speed); /* not a typo */
	else if (info->revision >= 5)
		return hpt372_tune_chipset(drive, speed);
	else if (info->revision >= 3)
		return hpt370_tune_chipset(drive, speed);
	else	/* hpt368: hpt_minimum_revision(dev, 2) */
		return hpt36x_tune_chipset(drive, speed);
}

static void hpt3xx_tune_drive (ide_drive_t *drive, u8 pio)
{
	pio = ide_get_best_pio_mode(drive, 255, pio, NULL);
	(void) hpt3xx_tune_chipset(drive, (XFER_PIO_0 + pio));
}

/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.  Initially for designed for
 * HPT366 UDMA chipset by HighPoint|Triones Technologies, Inc.
 *
 * check_in_drive_lists(drive, bad_ata66_4)
 * check_in_drive_lists(drive, bad_ata66_3)
 * check_in_drive_lists(drive, bad_ata33)
 *
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	u8 speed = ide_dma_speed(drive, hpt3xx_ratemask(drive));
	ide_hwif_t *hwif = drive->hwif;
	struct hpt_info	*info	= ide_get_hwifdata(hwif);

	if (!speed)
		return 0;

	/* If we don't have any timings we can't do a lot */
	if (info->speed == NULL)
		return 0;

	(void) hpt3xx_tune_chipset(drive, speed);
	return ide_dma_enable(drive);
}

static int hpt3xx_quirkproc (ide_drive_t *drive)
{
	return ((int) check_in_drive_lists(drive, quirk_drives));
}

static void hpt3xx_intrproc (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;

	if (drive->quirk_list)
		return;
	/* drives in the quirk_list may not like intr setups/cleanups */
	hwif->OUTB(drive->ctl|2, IDE_CONTROL_REG);
}

static void hpt3xx_maskproc (ide_drive_t *drive, int mask)
{
	ide_hwif_t *hwif = drive->hwif;
	struct hpt_info *info = ide_get_hwifdata(hwif);
	struct pci_dev *dev = hwif->pci_dev;

	if (drive->quirk_list) {
		if (info->revision >= 3) {
			u8 reg5a = 0;
			pci_read_config_byte(dev, 0x5a, &reg5a);
			if (((reg5a & 0x10) >> 4) != mask)
				pci_write_config_byte(dev, 0x5a, mask ? (reg5a | 0x10) : (reg5a & ~0x10));
		} else {
			if (mask) {
				disable_irq(hwif->irq);
			} else {
				enable_irq(hwif->irq);
			}
		}
	} else {
		if (IDE_CONTROL_REG)
			hwif->OUTB(mask ? (drive->ctl | 2) :
						 (drive->ctl & ~2),
						 IDE_CONTROL_REG);
	}
}

static int hpt366_config_drive_xfer_rate (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct hd_driveid *id	= drive->id;

	drive->init_speed = 0;

	if ((id->capability & 1) && drive->autodma) {

		if (ide_use_dma(drive)) {
			if (config_chipset_for_dma(drive))
				return hwif->ide_dma_on(drive);
		}

		goto fast_ata_pio;

	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		hpt3xx_tune_drive(drive, 5);
		return hwif->ide_dma_off_quietly(drive);
	}
	/* IORDY not supported */
	return 0;
}

/*
 * This is specific to the HPT366 UDMA bios chipset
 * by HighPoint|Triones Technologies, Inc.
 */
static int hpt366_ide_dma_lostirq (ide_drive_t *drive)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	u8 reg50h = 0, reg52h = 0, reg5ah = 0;

	pci_read_config_byte(dev, 0x50, &reg50h);
	pci_read_config_byte(dev, 0x52, &reg52h);
	pci_read_config_byte(dev, 0x5a, &reg5ah);
	printk("%s: (%s)  reg50h=0x%02x, reg52h=0x%02x, reg5ah=0x%02x\n",
		drive->name, __FUNCTION__, reg50h, reg52h, reg5ah);
	if (reg5ah & 0x10)
		pci_write_config_byte(dev, 0x5a, reg5ah & ~0x10);
	return __ide_dma_lostirq(drive);
}

static void hpt370_clear_engine (ide_drive_t *drive)
{
	u8 regstate = HWIF(drive)->channel ? 0x54 : 0x50;
	pci_write_config_byte(HWIF(drive)->pci_dev, regstate, 0x37);
	udelay(10);
}

static void hpt370_ide_dma_start(ide_drive_t *drive)
{
#ifdef HPT_RESET_STATE_ENGINE
	hpt370_clear_engine(drive);
#endif
	ide_dma_start(drive);
}

static int hpt370_ide_dma_end (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 dma_stat		= hwif->INB(hwif->dma_status);

	if (dma_stat & 0x01) {
		/* wait a little */
		udelay(20);
		dma_stat = hwif->INB(hwif->dma_status);
	}
	if ((dma_stat & 0x01) != 0) 
		/* fallthrough */
		(void) HWIF(drive)->ide_dma_timeout(drive);

	return __ide_dma_end(drive);
}

static void hpt370_lostirq_timeout (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 bfifo = 0, reginfo	= hwif->channel ? 0x56 : 0x52;
	u8 dma_stat = 0, dma_cmd = 0;

	pci_read_config_byte(HWIF(drive)->pci_dev, reginfo, &bfifo);
	printk(KERN_DEBUG "%s: %d bytes in FIFO\n", drive->name, bfifo);
	hpt370_clear_engine(drive);
	/* get dma command mode */
	dma_cmd = hwif->INB(hwif->dma_command);
	/* stop dma */
	hwif->OUTB(dma_cmd & ~0x1, hwif->dma_command);
	dma_stat = hwif->INB(hwif->dma_status);
	/* clear errors */
	hwif->OUTB(dma_stat | 0x6, hwif->dma_status);
}

static int hpt370_ide_dma_timeout (ide_drive_t *drive)
{
	hpt370_lostirq_timeout(drive);
	hpt370_clear_engine(drive);
	return __ide_dma_timeout(drive);
}

static int hpt370_ide_dma_lostirq (ide_drive_t *drive)
{
	hpt370_lostirq_timeout(drive);
	hpt370_clear_engine(drive);
	return __ide_dma_lostirq(drive);
}

/* returns 1 if DMA IRQ issued, 0 otherwise */
static int hpt374_ide_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u16 bfifo		= 0;
	u8 reginfo		= hwif->channel ? 0x56 : 0x52;
	u8 dma_stat;

	pci_read_config_word(hwif->pci_dev, reginfo, &bfifo);
	if (bfifo & 0x1FF) {
//		printk("%s: %d bytes in FIFO\n", drive->name, bfifo);
		return 0;
	}

	dma_stat = hwif->INB(hwif->dma_status);
	/* return 1 if INTR asserted */
	if ((dma_stat & 4) == 4)
		return 1;

	if (!drive->waiting_for_dma)
		printk(KERN_WARNING "%s: (%s) called while not waiting\n",
				drive->name, __FUNCTION__);
	return 0;
}

static int hpt374_ide_dma_end (ide_drive_t *drive)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	ide_hwif_t *hwif	= HWIF(drive);
	u8 msc_stat = 0, mscreg	= hwif->channel ? 0x54 : 0x50;
	u8 bwsr_stat = 0, bwsr_mask = hwif->channel ? 0x02 : 0x01;

	pci_read_config_byte(dev, 0x6a, &bwsr_stat);
	pci_read_config_byte(dev, mscreg, &msc_stat);
	if ((bwsr_stat & bwsr_mask) == bwsr_mask)
		pci_write_config_byte(dev, mscreg, msc_stat|0x30);
	return __ide_dma_end(drive);
}

/**
 *	hpt372n_set_clock	-	perform clock switching dance
 *	@drive: Drive to switch
 *	@mode: Switching mode (0x21 for write, 0x23 otherwise)
 *
 *	Switch the DPLL clock on the HPT372N devices. This is a
 *	right mess.
 */
 
static void hpt372n_set_clock(ide_drive_t *drive, int mode)
{
	ide_hwif_t *hwif	= HWIF(drive);
	
	/* FIXME: should we check for DMA active and BUG() */
	/* Tristate the bus */
	outb(0x80, hwif->dma_base+0x73);
	outb(0x80, hwif->dma_base+0x77);
	
	/* Switch clock and reset channels */
	outb(mode, hwif->dma_base+0x7B);
	outb(0xC0, hwif->dma_base+0x79);
	
	/* Reset state machines */
	outb(0x37, hwif->dma_base+0x70);
	outb(0x37, hwif->dma_base+0x74);
	
	/* Complete reset */
	outb(0x00, hwif->dma_base+0x79);
	
	/* Reconnect channels to bus */
	outb(0x00, hwif->dma_base+0x73);
	outb(0x00, hwif->dma_base+0x77);
}

/**
 *	hpt372n_rw_disk		-	prepare for I/O
 *	@drive: drive for command
 *	@rq: block request structure
 *
 *	This is called when a disk I/O is issued to the 372N.
 *	We need it because of the clock switching.
 */

static void hpt372n_rw_disk(ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif = drive->hwif;
	int wantclock;

	wantclock = rq_data_dir(rq) ? 0x23 : 0x21;

	if (hwif->config_data != wantclock) {
		hpt372n_set_clock(drive, wantclock);
		hwif->config_data = wantclock;
	}
}

/*
 * Since SUN Cobalt is attempting to do this operation, I should disclose
 * this has been a long time ago Thu Jul 27 16:40:57 2000 was the patch date
 * HOTSWAP ATA Infrastructure.
 */

static void hpt3xx_reset (ide_drive_t *drive)
{
}

static int hpt3xx_tristate (ide_drive_t * drive, int state)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 reg59h = 0, reset	= (hwif->channel) ? 0x80 : 0x40;
	u8 regXXh = 0, state_reg= (hwif->channel) ? 0x57 : 0x53;

	pci_read_config_byte(dev, 0x59, &reg59h);
	pci_read_config_byte(dev, state_reg, &regXXh);

	if (state) {
		(void) ide_do_reset(drive);
		pci_write_config_byte(dev, state_reg, regXXh|0x80);
		pci_write_config_byte(dev, 0x59, reg59h|reset);
	} else {
		pci_write_config_byte(dev, 0x59, reg59h & ~(reset));
		pci_write_config_byte(dev, state_reg, regXXh & ~(0x80));
		(void) ide_do_reset(drive);
	}
	return 0;
}

/* 
 * set/get power state for a drive.
 * turning the power off does the following things:
 *   1) soft-reset the drive
 *   2) tri-states the ide bus
 *
 * when we turn things back on, we need to re-initialize things.
 */
#define TRISTATE_BIT  0x8000
static int hpt370_busproc(ide_drive_t * drive, int state)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= hwif->pci_dev;
	u8 tristate = 0, resetmask = 0, bus_reg = 0;
	u16 tri_reg;

	hwif->bus_state = state;

	if (hwif->channel) { 
		/* secondary channel */
		tristate = 0x56;
		resetmask = 0x80; 
	} else { 
		/* primary channel */
		tristate = 0x52;
		resetmask = 0x40;
	}

	/* grab status */
	pci_read_config_word(dev, tristate, &tri_reg);
	pci_read_config_byte(dev, 0x59, &bus_reg);

	/* set the state. we don't set it if we don't need to do so.
	 * make sure that the drive knows that it has failed if it's off */
	switch (state) {
	case BUSSTATE_ON:
		hwif->drives[0].failures = 0;
		hwif->drives[1].failures = 0;
		if ((bus_reg & resetmask) == 0)
			return 0;
		tri_reg &= ~TRISTATE_BIT;
		bus_reg &= ~resetmask;
		break;
	case BUSSTATE_OFF:
		hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
		hwif->drives[1].failures = hwif->drives[1].max_failures + 1;
		if ((tri_reg & TRISTATE_BIT) == 0 && (bus_reg & resetmask))
			return 0;
		tri_reg &= ~TRISTATE_BIT;
		bus_reg |= resetmask;
		break;
	case BUSSTATE_TRISTATE:
		hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
		hwif->drives[1].failures = hwif->drives[1].max_failures + 1;
		if ((tri_reg & TRISTATE_BIT) && (bus_reg & resetmask))
			return 0;
		tri_reg |= TRISTATE_BIT;
		bus_reg |= resetmask;
		break;
	}
	pci_write_config_byte(dev, 0x59, bus_reg);
	pci_write_config_word(dev, tristate, tri_reg);

	return 0;
}

static void __devinit hpt366_clocking(ide_hwif_t *hwif)
{
	u32 reg1	= 0;
	struct hpt_info *info = ide_get_hwifdata(hwif);

	pci_read_config_dword(hwif->pci_dev, 0x40, &reg1);

	/* detect bus speed by looking at control reg timing: */
	switch((reg1 >> 8) & 7) {
		case 5:
			info->speed = forty_base_hpt366;
			break;
		case 9:
			info->speed = twenty_five_base_hpt366;
			break;
		case 7:
		default:
			info->speed = thirty_three_base_hpt366;
			break;
	}
}

static void __devinit hpt37x_clocking(ide_hwif_t *hwif)
{
	struct hpt_info *info = ide_get_hwifdata(hwif);
	struct pci_dev *dev = hwif->pci_dev;
	int adjust, i;
	u16 freq;
	u32 pll;
	u8 reg5bh;
	
	/*
	 * default to pci clock. make sure MA15/16 are set to output
	 * to prevent drives having problems with 40-pin cables. Needed
	 * for some drives such as IBM-DTLA which will not enter ready
	 * state on reset when PDIAG is a input.
	 *
	 * ToDo: should we set 0x21 when using PLL mode ?
	 */
	pci_write_config_byte(dev, 0x5b, 0x23);

	/*
	 * set up the PLL. we need to adjust it so that it's stable. 
	 * freq = Tpll * 192 / Tpci
	 *
	 * Todo. For non x86 should probably check the dword is
	 * set to 0xABCDExxx indicating the BIOS saved f_CNT
	 */
	pci_read_config_word(dev, 0x78, &freq);
	freq &= 0x1FF;
	
	/*
	 * The 372N uses different PCI clock information and has
	 * some other complications
	 *	On PCI33 timing we must clock switch
	 *	On PCI66 timing we must NOT use the PCI clock
	 *
	 * Currently we always set up the PLL for the 372N
	 */
	 
	if(info->flags & IS_372N)
	{
		printk(KERN_INFO "hpt: HPT372N detected, using 372N timing.\n");
		if(freq < 0x55)
			pll = F_LOW_PCI_33;
		else if(freq < 0x70)
			pll = F_LOW_PCI_40;
		else if(freq < 0x7F)
			pll = F_LOW_PCI_50;
		else
			pll = F_LOW_PCI_66;
			
		printk(KERN_INFO "FREQ: %d PLL: %d\n", freq, pll);
			
		/* We always use the pll not the PCI clock on 372N */
	}
	else
	{
		if(freq < 0x9C)
			pll = F_LOW_PCI_33;
		else if(freq < 0xb0)
			pll = F_LOW_PCI_40;
		else if(freq <0xc8)
			pll = F_LOW_PCI_50;
		else
			pll = F_LOW_PCI_66;
	
		if (pll == F_LOW_PCI_33) {
			if (info->revision >= 8)
				info->speed = thirty_three_base_hpt374;
			else if (info->revision >= 5)
				info->speed = thirty_three_base_hpt372;
			else if (info->revision >= 4)
				info->speed = thirty_three_base_hpt370a;
			else
				info->speed = thirty_three_base_hpt370;
			printk(KERN_DEBUG "HPT37X: using 33MHz PCI clock\n");
		} else if (pll == F_LOW_PCI_40) {
			/* Unsupported */
		} else if (pll == F_LOW_PCI_50) {
			if (info->revision >= 8)
				info->speed = fifty_base_hpt370a;
			else if (info->revision >= 5)
				info->speed = fifty_base_hpt372;
			else if (info->revision >= 4)
				info->speed = fifty_base_hpt370a;
			else
				info->speed = fifty_base_hpt370a;
			printk(KERN_DEBUG "HPT37X: using 50MHz PCI clock\n");
		} else {
			if (info->revision >= 8) {
				printk(KERN_ERR "HPT37x: 66MHz timings are not supported.\n");
			}
			else if (info->revision >= 5)
				info->speed = sixty_six_base_hpt372;
			else if (info->revision >= 4)
				info->speed = sixty_six_base_hpt370a;
			else
				info->speed = sixty_six_base_hpt370;
			printk(KERN_DEBUG "HPT37X: using 66MHz PCI clock\n");
		}
	}
	
	/*
	 * only try the pll if we don't have a table for the clock
	 * speed that we're running at. NOTE: the internal PLL will
	 * result in slow reads when using a 33MHz PCI clock. we also
	 * don't like to use the PLL because it will cause glitches
	 * on PRST/SRST when the HPT state engine gets reset.
	 *
	 * ToDo: Use 66MHz PLL when ATA133 devices are present on a
	 * 372 device so we can get ATA133 support
	 */
	if (info->speed)
		goto init_hpt37X_done;

	info->flags |= PLL_MODE;
	
	/*
	 * FIXME: make this work correctly, esp with 372N as per
	 * reference driver code.
	 *
	 * adjust PLL based upon PCI clock, enable it, and wait for
	 * stabilization.
	 */
	adjust = 0;
	freq = (pll < F_LOW_PCI_50) ? 2 : 4;
	while (adjust++ < 6) {
		pci_write_config_dword(dev, 0x5c, (freq + pll) << 16 |
				       pll | 0x100);

		/* wait for clock stabilization */
		for (i = 0; i < 0x50000; i++) {
			pci_read_config_byte(dev, 0x5b, &reg5bh);
			if (reg5bh & 0x80) {
				/* spin looking for the clock to destabilize */
				for (i = 0; i < 0x1000; ++i) {
					pci_read_config_byte(dev, 0x5b, 
							     &reg5bh);
					if ((reg5bh & 0x80) == 0)
						goto pll_recal;
				}
				pci_read_config_dword(dev, 0x5c, &pll);
				pci_write_config_dword(dev, 0x5c, 
						       pll & ~0x100);
				pci_write_config_byte(dev, 0x5b, 0x21);
				if (info->revision >= 8)
					info->speed = fifty_base_hpt370a;
				else if (info->revision >= 5)
					info->speed = fifty_base_hpt372;
				else if (info->revision >= 4)
					info->speed = fifty_base_hpt370a;
				else
					info->speed = fifty_base_hpt370a;
				printk("HPT37X: using 50MHz internal PLL\n");
				goto init_hpt37X_done;
			}
		}
pll_recal:
		if (adjust & 1)
			pll -= (adjust >> 1);
		else
			pll += (adjust >> 1);
	} 

init_hpt37X_done:
	if (!info->speed)
		printk(KERN_ERR "HPT37X%s: unknown bus timing [%d %d].\n",
			(info->flags & IS_372N)?"N":"", pll, freq);
	/* reset state engine */
	pci_write_config_byte(dev, 0x50, 0x37); 
	pci_write_config_byte(dev, 0x54, 0x37); 
	udelay(100);
}

static int __devinit init_hpt37x(struct pci_dev *dev)
{
	u8 reg5ah;

	pci_read_config_byte(dev, 0x5a, &reg5ah);
	/* interrupt force enable */
	pci_write_config_byte(dev, 0x5a, (reg5ah & ~0x10));
	return 0;
}

static int __devinit init_hpt366(struct pci_dev *dev)
{
	u32 reg1	= 0;
	u8 drive_fast	= 0;

	/*
	 * Disable the "fast interrupt" prediction.
	 */
	pci_read_config_byte(dev, 0x51, &drive_fast);
	if (drive_fast & 0x80)
		pci_write_config_byte(dev, 0x51, drive_fast & ~0x80);
	pci_read_config_dword(dev, 0x40, &reg1);
									
	return 0;
}

static unsigned int __devinit init_chipset_hpt366(struct pci_dev *dev, const char *name)
{
	int ret = 0;

	/*
	 * FIXME: Not portable. Also, why do we enable the ROM in the first place?
	 * We don't seem to be using it.
	 */
	if (dev->resource[PCI_ROM_RESOURCE].start)
		pci_write_config_dword(dev, PCI_ROM_ADDRESS,
			dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, (L1_CACHE_BYTES / 4));
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x78);
	pci_write_config_byte(dev, PCI_MIN_GNT, 0x08);
	pci_write_config_byte(dev, PCI_MAX_LAT, 0x08);

	if (hpt_revision(dev) >= 3)
		ret = init_hpt37x(dev);
	else
		ret = init_hpt366(dev);

	if (ret)
		return ret;

	return dev->irq;
}

static void __devinit init_hwif_hpt366(ide_hwif_t *hwif)
{
	struct pci_dev *dev		= hwif->pci_dev;
	struct hpt_info *info		= ide_get_hwifdata(hwif);
	u8 ata66 = 0, regmask		= (hwif->channel) ? 0x01 : 0x02;
	
	hwif->tuneproc			= &hpt3xx_tune_drive;
	hwif->speedproc			= &hpt3xx_tune_chipset;
	hwif->quirkproc			= &hpt3xx_quirkproc;
	hwif->intrproc			= &hpt3xx_intrproc;
	hwif->maskproc			= &hpt3xx_maskproc;
	
	if(info->flags & IS_372N)
		hwif->rw_disk = &hpt372n_rw_disk;

	/*
	 * The HPT37x uses the CBLID pins as outputs for MA15/MA16
	 * address lines to access an external eeprom.  To read valid
	 * cable detect state the pins must be enabled as inputs.
	 */
	if (info->revision >= 8 && (PCI_FUNC(dev->devfn) & 1)) {
		/*
		 * HPT374 PCI function 1
		 * - set bit 15 of reg 0x52 to enable TCBLID as input
		 * - set bit 15 of reg 0x56 to enable FCBLID as input
		 */
		u16 mcr3, mcr6;
		pci_read_config_word(dev, 0x52, &mcr3);
		pci_read_config_word(dev, 0x56, &mcr6);
		pci_write_config_word(dev, 0x52, mcr3 | 0x8000);
		pci_write_config_word(dev, 0x56, mcr6 | 0x8000);
		/* now read cable id register */
		pci_read_config_byte(dev, 0x5a, &ata66);
		pci_write_config_word(dev, 0x52, mcr3);
		pci_write_config_word(dev, 0x56, mcr6);
	} else if (info->revision >= 3) {
		/*
		 * HPT370/372 and 374 pcifn 0
		 * - clear bit 0 of 0x5b to enable P/SCBLID as inputs
		 */
		u8 scr2;
		pci_read_config_byte(dev, 0x5b, &scr2);
		pci_write_config_byte(dev, 0x5b, scr2 & ~1);
		/* now read cable id register */
		pci_read_config_byte(dev, 0x5a, &ata66);
		pci_write_config_byte(dev, 0x5b, scr2);
	} else {
		pci_read_config_byte(dev, 0x5a, &ata66);
	}

#ifdef DEBUG
	printk("HPT366: reg5ah=0x%02x ATA-%s Cable Port%d\n",
		ata66, (ata66 & regmask) ? "33" : "66",
		PCI_FUNC(hwif->pci_dev->devfn));
#endif /* DEBUG */

#ifdef HPT_SERIALIZE_IO
	/* serialize access to this device */
	if (hwif->mate)
		hwif->serialized = hwif->mate->serialized = 1;
#endif

	if (info->revision >= 3) {
		u8 reg5ah = 0;
			pci_write_config_byte(dev, 0x5a, reg5ah & ~0x10);
		/*
		 * set up ioctl for power status.
		 * note: power affects both
		 * drives on each channel
		 */
		hwif->resetproc	= &hpt3xx_reset;
		hwif->busproc	= &hpt370_busproc;
	} else if (info->revision >= 2) {
		hwif->resetproc	= &hpt3xx_reset;
		hwif->busproc	= &hpt3xx_tristate;
	} else {
		hwif->resetproc = &hpt3xx_reset;
		hwif->busproc   = &hpt3xx_tristate;
	}

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x07;

	if (!(hwif->udma_four))
		hwif->udma_four = ((ata66 & regmask) ? 0 : 1);
	hwif->ide_dma_check = &hpt366_config_drive_xfer_rate;

	if (info->revision >= 8) {
		hwif->ide_dma_test_irq = &hpt374_ide_dma_test_irq;
		hwif->ide_dma_end = &hpt374_ide_dma_end;
	} else if (info->revision >= 5) {
		hwif->ide_dma_test_irq = &hpt374_ide_dma_test_irq;
		hwif->ide_dma_end = &hpt374_ide_dma_end;
	} else if (info->revision >= 3) {
		hwif->dma_start = &hpt370_ide_dma_start;
		hwif->ide_dma_end = &hpt370_ide_dma_end;
		hwif->ide_dma_timeout = &hpt370_ide_dma_timeout;
		hwif->ide_dma_lostirq = &hpt370_ide_dma_lostirq;
	} else if (info->revision >= 2)
		hwif->ide_dma_lostirq = &hpt366_ide_dma_lostirq;
	else
		hwif->ide_dma_lostirq = &hpt366_ide_dma_lostirq;

	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

static void __devinit init_dma_hpt366(ide_hwif_t *hwif, unsigned long dmabase)
{
	struct hpt_info	*info	= ide_get_hwifdata(hwif);
	u8 masterdma	= 0, slavedma = 0;
	u8 dma_new	= 0, dma_old = 0;
	u8 primary	= hwif->channel ? 0x4b : 0x43;
	u8 secondary	= hwif->channel ? 0x4f : 0x47;
	unsigned long flags;

	if (!dmabase)
		return;
		
	if(info->speed == NULL) {
		printk(KERN_WARNING "hpt: no known IDE timings, disabling DMA.\n");
		return;
	}

	dma_old = hwif->INB(dmabase+2);

	local_irq_save(flags);

	dma_new = dma_old;
	pci_read_config_byte(hwif->pci_dev, primary, &masterdma);
	pci_read_config_byte(hwif->pci_dev, secondary, &slavedma);

	if (masterdma & 0x30)	dma_new |= 0x20;
	if (slavedma & 0x30)	dma_new |= 0x40;
	if (dma_new != dma_old)
		hwif->OUTB(dma_new, dmabase+2);

	local_irq_restore(flags);

	ide_setup_dma(hwif, dmabase, 8);
}

/*
 *	We "borrow" this hook in order to set the data structures
 *	up early enough before dma or init_hwif calls are made.
 */

static void __devinit init_iops_hpt366(ide_hwif_t *hwif)
{
	struct hpt_info *info = kmalloc(sizeof(struct hpt_info), GFP_KERNEL);
	unsigned long dmabase = pci_resource_start(hwif->pci_dev, 4);
	u8 did, rid;

	if(info == NULL) {
		printk(KERN_WARNING "hpt366: out of memory.\n");
		return;
	}
	memset(info, 0, sizeof(struct hpt_info));
	ide_set_hwifdata(hwif, info);

	if(dmabase) {
		did = inb(dmabase + 0x22);
		rid = inb(dmabase + 0x28);

		if((did == 4 && rid == 6) || (did == 5 && rid > 1))
			info->flags |= IS_372N;
	}

	info->revision = hpt_revision(hwif->pci_dev);

	if (info->revision >= 3)
		hpt37x_clocking(hwif);
	else
		hpt366_clocking(hwif);
}

static int __devinit init_setup_hpt374(struct pci_dev *dev, ide_pci_device_t *d)
{
	struct pci_dev *findev = NULL;

	if (PCI_FUNC(dev->devfn) & 1)
		return -ENODEV;

	while ((findev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, findev)) != NULL) {
		if ((findev->vendor == dev->vendor) &&
		    (findev->device == dev->device) &&
		    ((findev->devfn - dev->devfn) == 1) &&
		    (PCI_FUNC(findev->devfn) & 1)) {
			if (findev->irq != dev->irq) {
				/* FIXME: we need a core pci_set_interrupt() */
				findev->irq = dev->irq;
				printk(KERN_WARNING "%s: pci-config space interrupt "
					"fixed.\n", d->name);
			}
			return ide_setup_pci_devices(dev, findev, d);
		}
	}
	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_hpt37x(struct pci_dev *dev, ide_pci_device_t *d)
{
	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_hpt366(struct pci_dev *dev, ide_pci_device_t *d)
{
	struct pci_dev *findev = NULL;
	u8 pin1 = 0, pin2 = 0;
	unsigned int class_rev;
	char *chipset_names[] = {"HPT366", "HPT366",  "HPT368",
				 "HPT370", "HPT370A", "HPT372",
				 "HPT372N" };

	if (PCI_FUNC(dev->devfn) & 1)
		return -ENODEV;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	if(dev->device == PCI_DEVICE_ID_TTI_HPT372N)
		class_rev = 6;
		
	if(class_rev <= 6)
		d->name = chipset_names[class_rev];

	switch(class_rev) {
		case 6:
		case 5:
		case 4:
		case 3:
			goto init_single;
		default:
			break;
	}

	d->channels = 1;

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin1);
	while ((findev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, findev)) != NULL) {
		if ((findev->vendor == dev->vendor) &&
		    (findev->device == dev->device) &&
		    ((findev->devfn - dev->devfn) == 1) &&
		    (PCI_FUNC(findev->devfn) & 1)) {
			pci_read_config_byte(findev, PCI_INTERRUPT_PIN, &pin2);
			if ((pin1 != pin2) && (dev->irq == findev->irq)) {
				d->bootable = ON_BOARD;
				printk("%s: onboard version of chipset, "
					"pin1=%d pin2=%d\n", d->name,
					pin1, pin2);
			}
			return ide_setup_pci_devices(dev, findev, d);
		}
	}
init_single:
	return ide_setup_pci_device(dev, d);
}

static ide_pci_device_t hpt366_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "HPT366",
		.init_setup	= init_setup_hpt366,
		.init_chipset	= init_chipset_hpt366,
		.init_iops	= init_iops_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
		.extra		= 240
	},{	/* 1 */
		.name		= "HPT372A",
		.init_setup	= init_setup_hpt37x,
		.init_chipset	= init_chipset_hpt366,
		.init_iops	= init_iops_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 2 */
		.name		= "HPT302",
		.init_setup	= init_setup_hpt37x,
		.init_chipset	= init_chipset_hpt366,
		.init_iops	= init_iops_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 3 */
		.name		= "HPT371",
		.init_setup	= init_setup_hpt37x,
		.init_chipset	= init_chipset_hpt366,
		.init_iops	= init_iops_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 4 */
		.name		= "HPT374",
		.init_setup	= init_setup_hpt374,
		.init_chipset	= init_chipset_hpt366,
		.init_iops	= init_iops_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,	/* 4 */
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	},{	/* 5 */
		.name		= "HPT372N",
		.init_setup	= init_setup_hpt37x,
		.init_chipset	= init_chipset_hpt366,
		.init_iops	= init_iops_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,	/* 4 */
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
	}
};

/**
 *	hpt366_init_one	-	called when an HPT366 is found
 *	@dev: the hpt366 device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 */
 
static int __devinit hpt366_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &hpt366_chipsets[id->driver_data];

	return d->init_setup(dev, d);
}

static struct pci_device_id hpt366_pci_tbl[] = {
	{ PCI_VENDOR_ID_TTI, PCI_DEVICE_ID_TTI_HPT366, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_TTI, PCI_DEVICE_ID_TTI_HPT372, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_TTI, PCI_DEVICE_ID_TTI_HPT302, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{ PCI_VENDOR_ID_TTI, PCI_DEVICE_ID_TTI_HPT371, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{ PCI_VENDOR_ID_TTI, PCI_DEVICE_ID_TTI_HPT374, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{ PCI_VENDOR_ID_TTI, PCI_DEVICE_ID_TTI_HPT372N, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 5},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, hpt366_pci_tbl);

static struct pci_driver driver = {
	.name		= "HPT366_IDE",
	.id_table	= hpt366_pci_tbl,
	.probe		= hpt366_init_one,
};

static int hpt366_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(hpt366_ide_init);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for Highpoint HPT366 IDE");
MODULE_LICENSE("GPL");
