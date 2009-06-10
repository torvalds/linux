/*
 * Copyright (C) 1999-2003		Andre Hedrick <andre@linux-ide.org>
 * Portions Copyright (C) 2001	        Sun Microsystems, Inc.
 * Portions Copyright (C) 2003		Red Hat Inc
 * Portions Copyright (C) 2007		Bartlomiej Zolnierkiewicz
 * Portions Copyright (C) 2005-2009	MontaVista Software, Inc.
 *
 * Thanks to HighPoint Technologies for their assistance, and hardware.
 * Special Thanks to Jon Burchmore in SanDiego for the deep pockets, his
 * donation of an ABit BP6 mainboard, processor, and memory acellerated
 * development and support.
 *
 *
 * HighPoint has its own drivers (open source except for the RAID part)
 * available from http://www.highpoint-tech.com/BIOS%20+%20Driver/.
 * This may be useful to anyone wanting to work on this driver, however  do not
 * trust  them too much since the code tends to become less and less meaningful
 * as the time passes... :-/
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
 *		Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * - fix the clock turnaround code: it was writing to the wrong ports when
 *   called for the secondary channel, caching the current clock mode per-
 *   channel caused the cached register value to get out of sync with the
 *   actual one, the channels weren't serialized, the turnaround shouldn't
 *   be done on 66 MHz PCI bus
 * - disable UltraATA/100 for HPT370 by default as the 33 MHz clock being used
 *   does not allow for this speed anyway
 * - avoid touching disabled channels (e.g. HPT371/N are single channel chips,
 *   their primary channel is kind of virtual, it isn't tied to any pins)
 * - fix/remove bad/unused timing tables and use one set of tables for the whole
 *   HPT37x chip family; save space by introducing the separate transfer mode
 *   table in which the mode lookup is done
 * - use f_CNT value saved by  the HighPoint BIOS as reading it directly gives
 *   the wrong PCI frequency since DPLL has already been calibrated by BIOS;
 *   read it only from the function 0 of HPT374 chips
 * - fix the hotswap code:  it caused RESET- to glitch when tristating the bus,
 *   and for HPT36x the obsolete HDIO_TRISTATE_HWIF handler was called instead
 * - pass to init_chipset() handlers a copy of the IDE PCI device structure as
 *   they tamper with its fields
 * - pass  to the init_setup handlers a copy of the ide_pci_device_t structure
 *   since they may tamper with its fields
 * - prefix the driver startup messages with the real chip name
 * - claim the extra 240 bytes of I/O space for all chips
 * - optimize the UltraDMA filtering and the drive list lookup code
 * - use pci_get_slot() to get to the function 1 of HPT36x/374
 * - cache offset of the channel's misc. control registers (MCRs) being used
 *   throughout the driver
 * - only touch the relevant MCR when detecting the cable type on HPT374's
 *   function 1
 * - rename all the register related variables consistently
 * - move all the interrupt twiddling code from the speedproc handlers into
 *   init_hwif_hpt366(), also grouping all the DMA related code together there
 * - merge HPT36x/HPT37x speedproc handlers, fix PIO timing register mask and
 *   separate the UltraDMA and MWDMA masks there to avoid changing PIO timings
 *   when setting an UltraDMA mode
 * - fix hpt3xx_tune_drive() to set the PIO mode requested, not always select
 *   the best possible one
 * - clean up DMA timeout handling for HPT370
 * - switch to using the enumeration type to differ between the numerous chip
 *   variants, matching PCI device/revision ID with the chip type early, at the
 *   init_setup stage
 * - extend the hpt_info structure to hold the DPLL and PCI clock frequencies,
 *   stop duplicating it for each channel by storing the pointer in the pci_dev
 *   structure: first, at the init_setup stage, point it to a static "template"
 *   with only the chip type and its specific base DPLL frequency, the highest
 *   UltraDMA mode, and the chip settings table pointer filled,  then, at the
 *   init_chipset stage, allocate per-chip instance  and fill it with the rest
 *   of the necessary information
 * - get rid of the constant thresholds in the HPT37x PCI clock detection code,
 *   switch  to calculating  PCI clock frequency based on the chip's base DPLL
 *   frequency
 * - switch to using the  DPLL clock and enable UltraATA/133 mode by default on
 *   anything  newer than HPT370/A (except HPT374 that is not capable of this
 *   mode according to the manual)
 * - fold PCI clock detection and DPLL setup code into init_chipset_hpt366(),
 *   also fixing the interchanged 25/40 MHz PCI clock cases for HPT36x chips;
 *   unify HPT36x/37x timing setup code and the speedproc handlers by joining
 *   the register setting lists into the table indexed by the clock selected
 * - set the correct hwif->ultra_mask for each individual chip
 * - add Ultra and MW DMA mode filtering for the HPT37[24] based SATA cards
 * - stop resetting HPT370's state machine before each DMA transfer as that has
 *   caused more harm than good
 *	Sergei Shtylyov, <sshtylyov@ru.mvista.com> or <source@mvista.com>
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#define DRV_NAME "hpt366"

/* various tuning parameters */
#undef	HPT_RESET_STATE_ENGINE
#undef	HPT_DELAY_INTERRUPT

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
	"MAXTOR STM3320620A",
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

static u8 xfer_speeds[] = {
	XFER_UDMA_6,
	XFER_UDMA_5,
	XFER_UDMA_4,
	XFER_UDMA_3,
	XFER_UDMA_2,
	XFER_UDMA_1,
	XFER_UDMA_0,

	XFER_MW_DMA_2,
	XFER_MW_DMA_1,
	XFER_MW_DMA_0,

	XFER_PIO_4,
	XFER_PIO_3,
	XFER_PIO_2,
	XFER_PIO_1,
	XFER_PIO_0
};

/* Key for bus clock timings
 * 36x   37x
 * bits  bits
 * 0:3	 0:3	data_high_time. Inactive time of DIOW_/DIOR_ for PIO and MW DMA.
 *		cycles = value + 1
 * 4:7	 4:8	data_low_time. Active time of DIOW_/DIOR_ for PIO and MW DMA.
 *		cycles = value + 1
 * 8:11  9:12	cmd_high_time. Inactive time of DIOW_/DIOR_ during task file
 *		register access.
 * 12:15 13:17	cmd_low_time. Active time of DIOW_/DIOR_ during task file
 *		register access.
 * 16:18 18:20	udma_cycle_time. Clock cycles for UDMA xfer.
 * -	 21	CLK frequency: 0=ATA clock, 1=dual ATA clock.
 * 19:21 22:24	pre_high_time. Time to initialize the 1st cycle for PIO and
 *		MW DMA xfer.
 * 22:24 25:27	cmd_pre_high_time. Time to initialize the 1st PIO cycle for
 *		task file register access.
 * 28	 28	UDMA enable.
 * 29	 29	DMA  enable.
 * 30	 30	PIO MST enable. If set, the chip is in bus master mode during
 *		PIO xfer.
 * 31	 31	FIFO enable.
 */

static u32 forty_base_hpt36x[] = {
	/* XFER_UDMA_6 */	0x900fd943,
	/* XFER_UDMA_5 */	0x900fd943,
	/* XFER_UDMA_4 */	0x900fd943,
	/* XFER_UDMA_3 */	0x900ad943,
	/* XFER_UDMA_2 */	0x900bd943,
	/* XFER_UDMA_1 */	0x9008d943,
	/* XFER_UDMA_0 */	0x9008d943,

	/* XFER_MW_DMA_2 */	0xa008d943,
	/* XFER_MW_DMA_1 */	0xa010d955,
	/* XFER_MW_DMA_0 */	0xa010d9fc,

	/* XFER_PIO_4 */	0xc008d963,
	/* XFER_PIO_3 */	0xc010d974,
	/* XFER_PIO_2 */	0xc010d997,
	/* XFER_PIO_1 */	0xc010d9c7,
	/* XFER_PIO_0 */	0xc018d9d9
};

static u32 thirty_three_base_hpt36x[] = {
	/* XFER_UDMA_6 */	0x90c9a731,
	/* XFER_UDMA_5 */	0x90c9a731,
	/* XFER_UDMA_4 */	0x90c9a731,
	/* XFER_UDMA_3 */	0x90cfa731,
	/* XFER_UDMA_2 */	0x90caa731,
	/* XFER_UDMA_1 */	0x90cba731,
	/* XFER_UDMA_0 */	0x90c8a731,

	/* XFER_MW_DMA_2 */	0xa0c8a731,
	/* XFER_MW_DMA_1 */	0xa0c8a732,	/* 0xa0c8a733 */
	/* XFER_MW_DMA_0 */	0xa0c8a797,

	/* XFER_PIO_4 */	0xc0c8a731,
	/* XFER_PIO_3 */	0xc0c8a742,
	/* XFER_PIO_2 */	0xc0d0a753,
	/* XFER_PIO_1 */	0xc0d0a7a3,	/* 0xc0d0a793 */
	/* XFER_PIO_0 */	0xc0d0a7aa	/* 0xc0d0a7a7 */
};

static u32 twenty_five_base_hpt36x[] = {
	/* XFER_UDMA_6 */	0x90c98521,
	/* XFER_UDMA_5 */	0x90c98521,
	/* XFER_UDMA_4 */	0x90c98521,
	/* XFER_UDMA_3 */	0x90cf8521,
	/* XFER_UDMA_2 */	0x90cf8521,
	/* XFER_UDMA_1 */	0x90cb8521,
	/* XFER_UDMA_0 */	0x90cb8521,

	/* XFER_MW_DMA_2 */	0xa0ca8521,
	/* XFER_MW_DMA_1 */	0xa0ca8532,
	/* XFER_MW_DMA_0 */	0xa0ca8575,

	/* XFER_PIO_4 */	0xc0ca8521,
	/* XFER_PIO_3 */	0xc0ca8532,
	/* XFER_PIO_2 */	0xc0ca8542,
	/* XFER_PIO_1 */	0xc0d08572,
	/* XFER_PIO_0 */	0xc0d08585
};

#if 0
/* These are the timing tables from the HighPoint open source drivers... */
static u32 thirty_three_base_hpt37x[] = {
	/* XFER_UDMA_6 */	0x12446231,	/* 0x12646231 ?? */
	/* XFER_UDMA_5 */	0x12446231,
	/* XFER_UDMA_4 */	0x12446231,
	/* XFER_UDMA_3 */	0x126c6231,
	/* XFER_UDMA_2 */	0x12486231,
	/* XFER_UDMA_1 */	0x124c6233,
	/* XFER_UDMA_0 */	0x12506297,

	/* XFER_MW_DMA_2 */	0x22406c31,
	/* XFER_MW_DMA_1 */	0x22406c33,
	/* XFER_MW_DMA_0 */	0x22406c97,

	/* XFER_PIO_4 */	0x06414e31,
	/* XFER_PIO_3 */	0x06414e42,
	/* XFER_PIO_2 */	0x06414e53,
	/* XFER_PIO_1 */	0x06814e93,
	/* XFER_PIO_0 */	0x06814ea7
};

static u32 fifty_base_hpt37x[] = {
	/* XFER_UDMA_6 */	0x12848242,
	/* XFER_UDMA_5 */	0x12848242,
	/* XFER_UDMA_4 */	0x12ac8242,
	/* XFER_UDMA_3 */	0x128c8242,
	/* XFER_UDMA_2 */	0x120c8242,
	/* XFER_UDMA_1 */	0x12148254,
	/* XFER_UDMA_0 */	0x121882ea,

	/* XFER_MW_DMA_2 */	0x22808242,
	/* XFER_MW_DMA_1 */	0x22808254,
	/* XFER_MW_DMA_0 */	0x228082ea,

	/* XFER_PIO_4 */	0x0a81f442,
	/* XFER_PIO_3 */	0x0a81f443,
	/* XFER_PIO_2 */	0x0a81f454,
	/* XFER_PIO_1 */	0x0ac1f465,
	/* XFER_PIO_0 */	0x0ac1f48a
};

static u32 sixty_six_base_hpt37x[] = {
	/* XFER_UDMA_6 */	0x1c869c62,
	/* XFER_UDMA_5 */	0x1cae9c62,	/* 0x1c8a9c62 */
	/* XFER_UDMA_4 */	0x1c8a9c62,
	/* XFER_UDMA_3 */	0x1c8e9c62,
	/* XFER_UDMA_2 */	0x1c929c62,
	/* XFER_UDMA_1 */	0x1c9a9c62,
	/* XFER_UDMA_0 */	0x1c829c62,

	/* XFER_MW_DMA_2 */	0x2c829c62,
	/* XFER_MW_DMA_1 */	0x2c829c66,
	/* XFER_MW_DMA_0 */	0x2c829d2e,

	/* XFER_PIO_4 */	0x0c829c62,
	/* XFER_PIO_3 */	0x0c829c84,
	/* XFER_PIO_2 */	0x0c829ca6,
	/* XFER_PIO_1 */	0x0d029d26,
	/* XFER_PIO_0 */	0x0d029d5e
};
#else
/*
 * The following are the new timing tables with PIO mode data/taskfile transfer
 * overclocking fixed...
 */

/* This table is taken from the HPT370 data manual rev. 1.02 */
static u32 thirty_three_base_hpt37x[] = {
	/* XFER_UDMA_6 */	0x16455031,	/* 0x16655031 ?? */
	/* XFER_UDMA_5 */	0x16455031,
	/* XFER_UDMA_4 */	0x16455031,
	/* XFER_UDMA_3 */	0x166d5031,
	/* XFER_UDMA_2 */	0x16495031,
	/* XFER_UDMA_1 */	0x164d5033,
	/* XFER_UDMA_0 */	0x16515097,

	/* XFER_MW_DMA_2 */	0x26515031,
	/* XFER_MW_DMA_1 */	0x26515033,
	/* XFER_MW_DMA_0 */	0x26515097,

	/* XFER_PIO_4 */	0x06515021,
	/* XFER_PIO_3 */	0x06515022,
	/* XFER_PIO_2 */	0x06515033,
	/* XFER_PIO_1 */	0x06915065,
	/* XFER_PIO_0 */	0x06d1508a
};

static u32 fifty_base_hpt37x[] = {
	/* XFER_UDMA_6 */	0x1a861842,
	/* XFER_UDMA_5 */	0x1a861842,
	/* XFER_UDMA_4 */	0x1aae1842,
	/* XFER_UDMA_3 */	0x1a8e1842,
	/* XFER_UDMA_2 */	0x1a0e1842,
	/* XFER_UDMA_1 */	0x1a161854,
	/* XFER_UDMA_0 */	0x1a1a18ea,

	/* XFER_MW_DMA_2 */	0x2a821842,
	/* XFER_MW_DMA_1 */	0x2a821854,
	/* XFER_MW_DMA_0 */	0x2a8218ea,

	/* XFER_PIO_4 */	0x0a821842,
	/* XFER_PIO_3 */	0x0a821843,
	/* XFER_PIO_2 */	0x0a821855,
	/* XFER_PIO_1 */	0x0ac218a8,
	/* XFER_PIO_0 */	0x0b02190c
};

static u32 sixty_six_base_hpt37x[] = {
	/* XFER_UDMA_6 */	0x1c86fe62,
	/* XFER_UDMA_5 */	0x1caefe62,	/* 0x1c8afe62 */
	/* XFER_UDMA_4 */	0x1c8afe62,
	/* XFER_UDMA_3 */	0x1c8efe62,
	/* XFER_UDMA_2 */	0x1c92fe62,
	/* XFER_UDMA_1 */	0x1c9afe62,
	/* XFER_UDMA_0 */	0x1c82fe62,

	/* XFER_MW_DMA_2 */	0x2c82fe62,
	/* XFER_MW_DMA_1 */	0x2c82fe66,
	/* XFER_MW_DMA_0 */	0x2c82ff2e,

	/* XFER_PIO_4 */	0x0c82fe62,
	/* XFER_PIO_3 */	0x0c82fe84,
	/* XFER_PIO_2 */	0x0c82fea6,
	/* XFER_PIO_1 */	0x0d02ff26,
	/* XFER_PIO_0 */	0x0d42ff7f
};
#endif

#define HPT366_DEBUG_DRIVE_INFO		0
#define HPT371_ALLOW_ATA133_6		1
#define HPT302_ALLOW_ATA133_6		1
#define HPT372_ALLOW_ATA133_6		1
#define HPT370_ALLOW_ATA100_5		0
#define HPT366_ALLOW_ATA66_4		1
#define HPT366_ALLOW_ATA66_3		1
#define HPT366_MAX_DEVS			8

/* Supported ATA clock frequencies */
enum ata_clock {
	ATA_CLOCK_25MHZ,
	ATA_CLOCK_33MHZ,
	ATA_CLOCK_40MHZ,
	ATA_CLOCK_50MHZ,
	ATA_CLOCK_66MHZ,
	NUM_ATA_CLOCKS
};

struct hpt_timings {
	u32 pio_mask;
	u32 dma_mask;
	u32 ultra_mask;
	u32 *clock_table[NUM_ATA_CLOCKS];
};

/*
 *	Hold all the HighPoint chip information in one place.
 */

struct hpt_info {
	char *chip_name;	/* Chip name */
	u8 chip_type;		/* Chip type */
	u8 udma_mask;		/* Allowed UltraDMA modes mask. */
	u8 dpll_clk;		/* DPLL clock in MHz */
	u8 pci_clk;		/* PCI  clock in MHz */
	struct hpt_timings *timings; /* Chipset timing data */
	u8 clock;		/* ATA clock selected */
};

/* Supported HighPoint chips */
enum {
	HPT36x,
	HPT370,
	HPT370A,
	HPT374,
	HPT372,
	HPT372A,
	HPT302,
	HPT371,
	HPT372N,
	HPT302N,
	HPT371N
};

static struct hpt_timings hpt36x_timings = {
	.pio_mask	= 0xc1f8ffff,
	.dma_mask	= 0x303800ff,
	.ultra_mask	= 0x30070000,
	.clock_table	= {
		[ATA_CLOCK_25MHZ] = twenty_five_base_hpt36x,
		[ATA_CLOCK_33MHZ] = thirty_three_base_hpt36x,
		[ATA_CLOCK_40MHZ] = forty_base_hpt36x,
		[ATA_CLOCK_50MHZ] = NULL,
		[ATA_CLOCK_66MHZ] = NULL
	}
};

static struct hpt_timings hpt37x_timings = {
	.pio_mask	= 0xcfc3ffff,
	.dma_mask	= 0x31c001ff,
	.ultra_mask	= 0x303c0000,
	.clock_table	= {
		[ATA_CLOCK_25MHZ] = NULL,
		[ATA_CLOCK_33MHZ] = thirty_three_base_hpt37x,
		[ATA_CLOCK_40MHZ] = NULL,
		[ATA_CLOCK_50MHZ] = fifty_base_hpt37x,
		[ATA_CLOCK_66MHZ] = sixty_six_base_hpt37x
	}
};

static const struct hpt_info hpt36x __devinitdata = {
	.chip_name	= "HPT36x",
	.chip_type	= HPT36x,
	.udma_mask	= HPT366_ALLOW_ATA66_3 ? (HPT366_ALLOW_ATA66_4 ? ATA_UDMA4 : ATA_UDMA3) : ATA_UDMA2,
	.dpll_clk	= 0,	/* no DPLL */
	.timings	= &hpt36x_timings
};

static const struct hpt_info hpt370 __devinitdata = {
	.chip_name	= "HPT370",
	.chip_type	= HPT370,
	.udma_mask	= HPT370_ALLOW_ATA100_5 ? ATA_UDMA5 : ATA_UDMA4,
	.dpll_clk	= 48,
	.timings	= &hpt37x_timings
};

static const struct hpt_info hpt370a __devinitdata = {
	.chip_name	= "HPT370A",
	.chip_type	= HPT370A,
	.udma_mask	= HPT370_ALLOW_ATA100_5 ? ATA_UDMA5 : ATA_UDMA4,
	.dpll_clk	= 48,
	.timings	= &hpt37x_timings
};

static const struct hpt_info hpt374 __devinitdata = {
	.chip_name	= "HPT374",
	.chip_type	= HPT374,
	.udma_mask	= ATA_UDMA5,
	.dpll_clk	= 48,
	.timings	= &hpt37x_timings
};

static const struct hpt_info hpt372 __devinitdata = {
	.chip_name	= "HPT372",
	.chip_type	= HPT372,
	.udma_mask	= HPT372_ALLOW_ATA133_6 ? ATA_UDMA6 : ATA_UDMA5,
	.dpll_clk	= 55,
	.timings	= &hpt37x_timings
};

static const struct hpt_info hpt372a __devinitdata = {
	.chip_name	= "HPT372A",
	.chip_type	= HPT372A,
	.udma_mask	= HPT372_ALLOW_ATA133_6 ? ATA_UDMA6 : ATA_UDMA5,
	.dpll_clk	= 66,
	.timings	= &hpt37x_timings
};

static const struct hpt_info hpt302 __devinitdata = {
	.chip_name	= "HPT302",
	.chip_type	= HPT302,
	.udma_mask	= HPT302_ALLOW_ATA133_6 ? ATA_UDMA6 : ATA_UDMA5,
	.dpll_clk	= 66,
	.timings	= &hpt37x_timings
};

static const struct hpt_info hpt371 __devinitdata = {
	.chip_name	= "HPT371",
	.chip_type	= HPT371,
	.udma_mask	= HPT371_ALLOW_ATA133_6 ? ATA_UDMA6 : ATA_UDMA5,
	.dpll_clk	= 66,
	.timings	= &hpt37x_timings
};

static const struct hpt_info hpt372n __devinitdata = {
	.chip_name	= "HPT372N",
	.chip_type	= HPT372N,
	.udma_mask	= HPT372_ALLOW_ATA133_6 ? ATA_UDMA6 : ATA_UDMA5,
	.dpll_clk	= 77,
	.timings	= &hpt37x_timings
};

static const struct hpt_info hpt302n __devinitdata = {
	.chip_name	= "HPT302N",
	.chip_type	= HPT302N,
	.udma_mask	= HPT302_ALLOW_ATA133_6 ? ATA_UDMA6 : ATA_UDMA5,
	.dpll_clk	= 77,
	.timings	= &hpt37x_timings
};

static const struct hpt_info hpt371n __devinitdata = {
	.chip_name	= "HPT371N",
	.chip_type	= HPT371N,
	.udma_mask	= HPT371_ALLOW_ATA133_6 ? ATA_UDMA6 : ATA_UDMA5,
	.dpll_clk	= 77,
	.timings	= &hpt37x_timings
};

static int check_in_drive_list(ide_drive_t *drive, const char **list)
{
	char *m = (char *)&drive->id[ATA_ID_PROD];

	while (*list)
		if (!strcmp(*list++, m))
			return 1;
	return 0;
}

static struct hpt_info *hpt3xx_get_info(struct device *dev)
{
	struct ide_host *host	= dev_get_drvdata(dev);
	struct hpt_info *info	= (struct hpt_info *)host->host_priv;

	return dev == host->dev[1] ? info + 1 : info;
}

/*
 * The Marvell bridge chips used on the HighPoint SATA cards do not seem
 * to support the UltraDMA modes 1, 2, and 3 as well as any MWDMA modes...
 */

static u8 hpt3xx_udma_filter(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct hpt_info *info	= hpt3xx_get_info(hwif->dev);
	u8 mask 		= hwif->ultra_mask;

	switch (info->chip_type) {
	case HPT36x:
		if (!HPT366_ALLOW_ATA66_4 ||
		    check_in_drive_list(drive, bad_ata66_4))
			mask = ATA_UDMA3;

		if (!HPT366_ALLOW_ATA66_3 ||
		    check_in_drive_list(drive, bad_ata66_3))
			mask = ATA_UDMA2;
		break;
	case HPT370:
		if (!HPT370_ALLOW_ATA100_5 ||
		    check_in_drive_list(drive, bad_ata100_5))
			mask = ATA_UDMA4;
		break;
	case HPT370A:
		if (!HPT370_ALLOW_ATA100_5 ||
		    check_in_drive_list(drive, bad_ata100_5))
			return ATA_UDMA4;
	case HPT372 :
	case HPT372A:
	case HPT372N:
	case HPT374 :
		if (ata_id_is_sata(drive->id))
			mask &= ~0x0e;
		/* Fall thru */
	default:
		return mask;
	}

	return check_in_drive_list(drive, bad_ata33) ? 0x00 : mask;
}

static u8 hpt3xx_mdma_filter(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct hpt_info *info	= hpt3xx_get_info(hwif->dev);

	switch (info->chip_type) {
	case HPT372 :
	case HPT372A:
	case HPT372N:
	case HPT374 :
		if (ata_id_is_sata(drive->id))
			return 0x00;
		/* Fall thru */
	default:
		return 0x07;
	}
}

static u32 get_speed_setting(u8 speed, struct hpt_info *info)
{
	int i;

	/*
	 * Lookup the transfer mode table to get the index into
	 * the timing table.
	 *
	 * NOTE: For XFER_PIO_SLOW, PIO mode 0 timings will be used.
	 */
	for (i = 0; i < ARRAY_SIZE(xfer_speeds) - 1; i++)
		if (xfer_speeds[i] == speed)
			break;

	return info->timings->clock_table[info->clock][i];
}

static void hpt3xx_set_mode(ide_drive_t *drive, const u8 speed)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	struct hpt_info *info	= hpt3xx_get_info(hwif->dev);
	struct hpt_timings *t	= info->timings;
	u8  itr_addr		= 0x40 + (drive->dn * 4);
	u32 old_itr		= 0;
	u32 new_itr		= get_speed_setting(speed, info);
	u32 itr_mask		= speed < XFER_MW_DMA_0 ? t->pio_mask :
				 (speed < XFER_UDMA_0   ? t->dma_mask :
							  t->ultra_mask);

	pci_read_config_dword(dev, itr_addr, &old_itr);
	new_itr = (old_itr & ~itr_mask) | (new_itr & itr_mask);
	/*
	 * Disable on-chip PIO FIFO/buffer (and PIO MST mode as well)
	 * to avoid problems handling I/O errors later
	 */
	new_itr &= ~0xc0000000;

	pci_write_config_dword(dev, itr_addr, new_itr);
}

static void hpt3xx_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	hpt3xx_set_mode(drive, XFER_PIO_0 + pio);
}

static void hpt3xx_quirkproc(ide_drive_t *drive)
{
	char *m			= (char *)&drive->id[ATA_ID_PROD];
	const  char **list	= quirk_drives;

	while (*list)
		if (strstr(m, *list++)) {
			drive->quirk_list = 1;
			return;
		}

	drive->quirk_list = 0;
}

static void hpt3xx_maskproc(ide_drive_t *drive, int mask)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev	*dev	= to_pci_dev(hwif->dev);
	struct hpt_info *info	= hpt3xx_get_info(hwif->dev);

	if (drive->quirk_list == 0)
		return;

	if (info->chip_type >= HPT370) {
		u8 scr1 = 0;

		pci_read_config_byte(dev, 0x5a, &scr1);
		if (((scr1 & 0x10) >> 4) != mask) {
			if (mask)
				scr1 |=  0x10;
			else
				scr1 &= ~0x10;
			pci_write_config_byte(dev, 0x5a, scr1);
		}
	} else if (mask)
		disable_irq(hwif->irq);
	else
		enable_irq(hwif->irq);
}

/*
 * This is specific to the HPT366 UDMA chipset
 * by HighPoint|Triones Technologies, Inc.
 */
static void hpt366_dma_lost_irq(ide_drive_t *drive)
{
	struct pci_dev *dev = to_pci_dev(drive->hwif->dev);
	u8 mcr1 = 0, mcr3 = 0, scr1 = 0;

	pci_read_config_byte(dev, 0x50, &mcr1);
	pci_read_config_byte(dev, 0x52, &mcr3);
	pci_read_config_byte(dev, 0x5a, &scr1);
	printk("%s: (%s)  mcr1=0x%02x, mcr3=0x%02x, scr1=0x%02x\n",
		drive->name, __func__, mcr1, mcr3, scr1);
	if (scr1 & 0x10)
		pci_write_config_byte(dev, 0x5a, scr1 & ~0x10);
	ide_dma_lost_irq(drive);
}

static void hpt370_clear_engine(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct pci_dev *dev = to_pci_dev(hwif->dev);

	pci_write_config_byte(dev, hwif->select_data, 0x37);
	udelay(10);
}

static void hpt370_irq_timeout(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u16 bfifo		= 0;
	u8  dma_cmd;

	pci_read_config_word(dev, hwif->select_data + 2, &bfifo);
	printk(KERN_DEBUG "%s: %d bytes in FIFO\n", drive->name, bfifo & 0x1ff);

	/* get DMA command mode */
	dma_cmd = inb(hwif->dma_base + ATA_DMA_CMD);
	/* stop DMA */
	outb(dma_cmd & ~ATA_DMA_START, hwif->dma_base + ATA_DMA_CMD);
	hpt370_clear_engine(drive);
}

static void hpt370_dma_start(ide_drive_t *drive)
{
#ifdef HPT_RESET_STATE_ENGINE
	hpt370_clear_engine(drive);
#endif
	ide_dma_start(drive);
}

static int hpt370_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	u8  dma_stat		= inb(hwif->dma_base + ATA_DMA_STATUS);

	if (dma_stat & ATA_DMA_ACTIVE) {
		/* wait a little */
		udelay(20);
		dma_stat = inb(hwif->dma_base + ATA_DMA_STATUS);
		if (dma_stat & ATA_DMA_ACTIVE)
			hpt370_irq_timeout(drive);
	}
	return ide_dma_end(drive);
}

/* returns 1 if DMA IRQ issued, 0 otherwise */
static int hpt374_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u16 bfifo		= 0;
	u8  dma_stat;

	pci_read_config_word(dev, hwif->select_data + 2, &bfifo);
	if (bfifo & 0x1FF) {
//		printk("%s: %d bytes in FIFO\n", drive->name, bfifo);
		return 0;
	}

	dma_stat = inb(hwif->dma_base + ATA_DMA_STATUS);
	/* return 1 if INTR asserted */
	if (dma_stat & ATA_DMA_INTR)
		return 1;

	return 0;
}

static int hpt374_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u8 mcr	= 0, mcr_addr	= hwif->select_data;
	u8 bwsr = 0, mask	= hwif->channel ? 0x02 : 0x01;

	pci_read_config_byte(dev, 0x6a, &bwsr);
	pci_read_config_byte(dev, mcr_addr, &mcr);
	if (bwsr & mask)
		pci_write_config_byte(dev, mcr_addr, mcr | 0x30);
	return ide_dma_end(drive);
}

/**
 *	hpt3xxn_set_clock	-	perform clock switching dance
 *	@hwif: hwif to switch
 *	@mode: clocking mode (0x21 for write, 0x23 otherwise)
 *
 *	Switch the DPLL clock on the HPT3xxN devices. This is a	right mess.
 */

static void hpt3xxn_set_clock(ide_hwif_t *hwif, u8 mode)
{
	unsigned long base = hwif->extra_base;
	u8 scr2 = inb(base + 0x6b);

	if ((scr2 & 0x7f) == mode)
		return;

	/* Tristate the bus */
	outb(0x80, base + 0x63);
	outb(0x80, base + 0x67);

	/* Switch clock and reset channels */
	outb(mode, base + 0x6b);
	outb(0xc0, base + 0x69);

	/*
	 * Reset the state machines.
	 * NOTE: avoid accidentally enabling the disabled channels.
	 */
	outb(inb(base + 0x60) | 0x32, base + 0x60);
	outb(inb(base + 0x64) | 0x32, base + 0x64);

	/* Complete reset */
	outb(0x00, base + 0x69);

	/* Reconnect channels to bus */
	outb(0x00, base + 0x63);
	outb(0x00, base + 0x67);
}

/**
 *	hpt3xxn_rw_disk		-	prepare for I/O
 *	@drive: drive for command
 *	@rq: block request structure
 *
 *	This is called when a disk I/O is issued to HPT3xxN.
 *	We need it because of the clock switching.
 */

static void hpt3xxn_rw_disk(ide_drive_t *drive, struct request *rq)
{
	hpt3xxn_set_clock(drive->hwif, rq_data_dir(rq) ? 0x23 : 0x21);
}

/**
 *	hpt37x_calibrate_dpll	-	calibrate the DPLL
 *	@dev: PCI device
 *
 *	Perform a calibration cycle on the DPLL.
 *	Returns 1 if this succeeds
 */
static int hpt37x_calibrate_dpll(struct pci_dev *dev, u16 f_low, u16 f_high)
{
	u32 dpll = (f_high << 16) | f_low | 0x100;
	u8  scr2;
	int i;

	pci_write_config_dword(dev, 0x5c, dpll);

	/* Wait for oscillator ready */
	for(i = 0; i < 0x5000; ++i) {
		udelay(50);
		pci_read_config_byte(dev, 0x5b, &scr2);
		if (scr2 & 0x80)
			break;
	}
	/* See if it stays ready (we'll just bail out if it's not yet) */
	for(i = 0; i < 0x1000; ++i) {
		pci_read_config_byte(dev, 0x5b, &scr2);
		/* DPLL destabilized? */
		if(!(scr2 & 0x80))
			return 0;
	}
	/* Turn off tuning, we have the DPLL set */
	pci_read_config_dword (dev, 0x5c, &dpll);
	pci_write_config_dword(dev, 0x5c, (dpll & ~0x100));
	return 1;
}

static void hpt3xx_disable_fast_irq(struct pci_dev *dev, u8 mcr_addr)
{
	struct ide_host *host	= pci_get_drvdata(dev);
	struct hpt_info *info	= host->host_priv + (&dev->dev == host->dev[1]);
	u8  chip_type		= info->chip_type;
	u8  new_mcr, old_mcr	= 0;

	/*
	 * Disable the "fast interrupt" prediction.  Don't hold off
	 * on interrupts. (== 0x01 despite what the docs say)
	 */
	pci_read_config_byte(dev, mcr_addr + 1, &old_mcr);

	if (chip_type >= HPT374)
		new_mcr = old_mcr & ~0x07;
	else if (chip_type >= HPT370) {
		new_mcr = old_mcr;
		new_mcr &= ~0x02;
#ifdef HPT_DELAY_INTERRUPT
		new_mcr &= ~0x01;
#else
		new_mcr |=  0x01;
#endif
	} else					/* HPT366 and HPT368  */
		new_mcr = old_mcr & ~0x80;

	if (new_mcr != old_mcr)
		pci_write_config_byte(dev, mcr_addr + 1, new_mcr);
}

static int init_chipset_hpt366(struct pci_dev *dev)
{
	unsigned long io_base	= pci_resource_start(dev, 4);
	struct hpt_info *info	= hpt3xx_get_info(&dev->dev);
	const char *name	= DRV_NAME;
	u8 pci_clk,  dpll_clk	= 0;	/* PCI and DPLL clock in MHz */
	u8 chip_type;
	enum ata_clock	clock;

	chip_type = info->chip_type;

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, (L1_CACHE_BYTES / 4));
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x78);
	pci_write_config_byte(dev, PCI_MIN_GNT, 0x08);
	pci_write_config_byte(dev, PCI_MAX_LAT, 0x08);

	/*
	 * First, try to estimate the PCI clock frequency...
	 */
	if (chip_type >= HPT370) {
		u8  scr1  = 0;
		u16 f_cnt = 0;
		u32 temp  = 0;

		/* Interrupt force enable. */
		pci_read_config_byte(dev, 0x5a, &scr1);
		if (scr1 & 0x10)
			pci_write_config_byte(dev, 0x5a, scr1 & ~0x10);

		/*
		 * HighPoint does this for HPT372A.
		 * NOTE: This register is only writeable via I/O space.
		 */
		if (chip_type == HPT372A)
			outb(0x0e, io_base + 0x9c);

		/*
		 * Default to PCI clock. Make sure MA15/16 are set to output
		 * to prevent drives having problems with 40-pin cables.
		 */
		pci_write_config_byte(dev, 0x5b, 0x23);

		/*
		 * We'll have to read f_CNT value in order to determine
		 * the PCI clock frequency according to the following ratio:
		 *
		 * f_CNT = Fpci * 192 / Fdpll
		 *
		 * First try reading the register in which the HighPoint BIOS
		 * saves f_CNT value before  reprogramming the DPLL from its
		 * default setting (which differs for the various chips).
		 *
		 * NOTE: This register is only accessible via I/O space;
		 * HPT374 BIOS only saves it for the function 0, so we have to
		 * always read it from there -- no need to check the result of
		 * pci_get_slot() for the function 0 as the whole device has
		 * been already "pinned" (via function 1) in init_setup_hpt374()
		 */
		if (chip_type == HPT374 && (PCI_FUNC(dev->devfn) & 1)) {
			struct pci_dev	*dev1 = pci_get_slot(dev->bus,
							     dev->devfn - 1);
			unsigned long io_base = pci_resource_start(dev1, 4);

			temp =	inl(io_base + 0x90);
			pci_dev_put(dev1);
		} else
			temp =	inl(io_base + 0x90);

		/*
		 * In case the signature check fails, we'll have to
		 * resort to reading the f_CNT register itself in hopes
		 * that nobody has touched the DPLL yet...
		 */
		if ((temp & 0xFFFFF000) != 0xABCDE000) {
			int i;

			printk(KERN_WARNING "%s %s: no clock data saved by "
				"BIOS\n", name, pci_name(dev));

			/* Calculate the average value of f_CNT. */
			for (temp = i = 0; i < 128; i++) {
				pci_read_config_word(dev, 0x78, &f_cnt);
				temp += f_cnt & 0x1ff;
				mdelay(1);
			}
			f_cnt = temp / 128;
		} else
			f_cnt = temp & 0x1ff;

		dpll_clk = info->dpll_clk;
		pci_clk  = (f_cnt * dpll_clk) / 192;

		/* Clamp PCI clock to bands. */
		if (pci_clk < 40)
			pci_clk = 33;
		else if(pci_clk < 45)
			pci_clk = 40;
		else if(pci_clk < 55)
			pci_clk = 50;
		else
			pci_clk = 66;

		printk(KERN_INFO "%s %s: DPLL base: %d MHz, f_CNT: %d, "
			"assuming %d MHz PCI\n", name, pci_name(dev),
			dpll_clk, f_cnt, pci_clk);
	} else {
		u32 itr1 = 0;

		pci_read_config_dword(dev, 0x40, &itr1);

		/* Detect PCI clock by looking at cmd_high_time. */
		switch((itr1 >> 8) & 0x07) {
			case 0x09:
				pci_clk = 40;
				break;
			case 0x05:
				pci_clk = 25;
				break;
			case 0x07:
			default:
				pci_clk = 33;
				break;
		}
	}

	/* Let's assume we'll use PCI clock for the ATA clock... */
	switch (pci_clk) {
		case 25:
			clock = ATA_CLOCK_25MHZ;
			break;
		case 33:
		default:
			clock = ATA_CLOCK_33MHZ;
			break;
		case 40:
			clock = ATA_CLOCK_40MHZ;
			break;
		case 50:
			clock = ATA_CLOCK_50MHZ;
			break;
		case 66:
			clock = ATA_CLOCK_66MHZ;
			break;
	}

	/*
	 * Only try the DPLL if we don't have a table for the PCI clock that
	 * we are running at for HPT370/A, always use it  for anything newer...
	 *
	 * NOTE: Using the internal DPLL results in slow reads on 33 MHz PCI.
	 * We also  don't like using  the DPLL because this causes glitches
	 * on PRST-/SRST- when the state engine gets reset...
	 */
	if (chip_type >= HPT374 || info->timings->clock_table[clock] == NULL) {
		u16 f_low, delta = pci_clk < 50 ? 2 : 4;
		int adjust;

		 /*
		  * Select 66 MHz DPLL clock only if UltraATA/133 mode is
		  * supported/enabled, use 50 MHz DPLL clock otherwise...
		  */
		if (info->udma_mask == ATA_UDMA6) {
			dpll_clk = 66;
			clock = ATA_CLOCK_66MHZ;
		} else if (dpll_clk) {	/* HPT36x chips don't have DPLL */
			dpll_clk = 50;
			clock = ATA_CLOCK_50MHZ;
		}

		if (info->timings->clock_table[clock] == NULL) {
			printk(KERN_ERR "%s %s: unknown bus timing!\n",
				name, pci_name(dev));
			return -EIO;
		}

		/* Select the DPLL clock. */
		pci_write_config_byte(dev, 0x5b, 0x21);

		/*
		 * Adjust the DPLL based upon PCI clock, enable it,
		 * and wait for stabilization...
		 */
		f_low = (pci_clk * 48) / dpll_clk;

		for (adjust = 0; adjust < 8; adjust++) {
			if(hpt37x_calibrate_dpll(dev, f_low, f_low + delta))
				break;

			/*
			 * See if it'll settle at a fractionally different clock
			 */
			if (adjust & 1)
				f_low -= adjust >> 1;
			else
				f_low += adjust >> 1;
		}
		if (adjust == 8) {
			printk(KERN_ERR "%s %s: DPLL did not stabilize!\n",
				name, pci_name(dev));
			return -EIO;
		}

		printk(KERN_INFO "%s %s: using %d MHz DPLL clock\n",
			name, pci_name(dev), dpll_clk);
	} else {
		/* Mark the fact that we're not using the DPLL. */
		dpll_clk = 0;

		printk(KERN_INFO "%s %s: using %d MHz PCI clock\n",
			name, pci_name(dev), pci_clk);
	}

	/* Store the clock frequencies. */
	info->dpll_clk	= dpll_clk;
	info->pci_clk	= pci_clk;
	info->clock	= clock;

	if (chip_type >= HPT370) {
		u8  mcr1, mcr4;

		/*
		 * Reset the state engines.
		 * NOTE: Avoid accidentally enabling the disabled channels.
		 */
		pci_read_config_byte (dev, 0x50, &mcr1);
		pci_read_config_byte (dev, 0x54, &mcr4);
		pci_write_config_byte(dev, 0x50, (mcr1 | 0x32));
		pci_write_config_byte(dev, 0x54, (mcr4 | 0x32));
		udelay(100);
	}

	/*
	 * On  HPT371N, if ATA clock is 66 MHz we must set bit 2 in
	 * the MISC. register to stretch the UltraDMA Tss timing.
	 * NOTE: This register is only writeable via I/O space.
	 */
	if (chip_type == HPT371N && clock == ATA_CLOCK_66MHZ)
		outb(inb(io_base + 0x9c) | 0x04, io_base + 0x9c);

	hpt3xx_disable_fast_irq(dev, 0x50);
	hpt3xx_disable_fast_irq(dev, 0x54);

	return 0;
}

static u8 hpt3xx_cable_detect(ide_hwif_t *hwif)
{
	struct pci_dev	*dev	= to_pci_dev(hwif->dev);
	struct hpt_info *info	= hpt3xx_get_info(hwif->dev);
	u8 chip_type		= info->chip_type;
	u8 scr1 = 0, ata66	= hwif->channel ? 0x01 : 0x02;

	/*
	 * The HPT37x uses the CBLID pins as outputs for MA15/MA16
	 * address lines to access an external EEPROM.  To read valid
	 * cable detect state the pins must be enabled as inputs.
	 */
	if (chip_type == HPT374 && (PCI_FUNC(dev->devfn) & 1)) {
		/*
		 * HPT374 PCI function 1
		 * - set bit 15 of reg 0x52 to enable TCBLID as input
		 * - set bit 15 of reg 0x56 to enable FCBLID as input
		 */
		u8  mcr_addr = hwif->select_data + 2;
		u16 mcr;

		pci_read_config_word(dev, mcr_addr, &mcr);
		pci_write_config_word(dev, mcr_addr, (mcr | 0x8000));
		/* now read cable id register */
		pci_read_config_byte(dev, 0x5a, &scr1);
		pci_write_config_word(dev, mcr_addr, mcr);
	} else if (chip_type >= HPT370) {
		/*
		 * HPT370/372 and 374 pcifn 0
		 * - clear bit 0 of reg 0x5b to enable P/SCBLID as inputs
		 */
		u8 scr2 = 0;

		pci_read_config_byte(dev, 0x5b, &scr2);
		pci_write_config_byte(dev, 0x5b, (scr2 & ~1));
		/* now read cable id register */
		pci_read_config_byte(dev, 0x5a, &scr1);
		pci_write_config_byte(dev, 0x5b,  scr2);
	} else
		pci_read_config_byte(dev, 0x5a, &scr1);

	return (scr1 & ata66) ? ATA_CBL_PATA40 : ATA_CBL_PATA80;
}

static void __devinit init_hwif_hpt366(ide_hwif_t *hwif)
{
	struct hpt_info *info	= hpt3xx_get_info(hwif->dev);
	u8  chip_type		= info->chip_type;

	/* Cache the channel's MISC. control registers' offset */
	hwif->select_data	= hwif->channel ? 0x54 : 0x50;

	/*
	 * HPT3xxN chips have some complications:
	 *
	 * - on 33 MHz PCI we must clock switch
	 * - on 66 MHz PCI we must NOT use the PCI clock
	 */
	if (chip_type >= HPT372N && info->dpll_clk && info->pci_clk < 66) {
		/*
		 * Clock is shared between the channels,
		 * so we'll have to serialize them... :-(
		 */
		hwif->host->host_flags |= IDE_HFLAG_SERIALIZE;
		hwif->rw_disk = &hpt3xxn_rw_disk;
	}
}

static int __devinit init_dma_hpt366(ide_hwif_t *hwif,
				     const struct ide_port_info *d)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	unsigned long flags, base = ide_pci_dma_base(hwif, d);
	u8 dma_old, dma_new, masterdma = 0, slavedma = 0;

	if (base == 0)
		return -1;

	hwif->dma_base = base;

	if (ide_pci_check_simplex(hwif, d) < 0)
		return -1;

	if (ide_pci_set_master(dev, d->name) < 0)
		return -1;

	dma_old = inb(base + 2);

	local_irq_save(flags);

	dma_new = dma_old;
	pci_read_config_byte(dev, hwif->channel ? 0x4b : 0x43, &masterdma);
	pci_read_config_byte(dev, hwif->channel ? 0x4f : 0x47,  &slavedma);

	if (masterdma & 0x30)	dma_new |= 0x20;
	if ( slavedma & 0x30)	dma_new |= 0x40;
	if (dma_new != dma_old)
		outb(dma_new, base + 2);

	local_irq_restore(flags);

	printk(KERN_INFO "    %s: BM-DMA at 0x%04lx-0x%04lx\n",
			 hwif->name, base, base + 7);

	hwif->extra_base = base + (hwif->channel ? 8 : 16);

	if (ide_allocate_dma_engine(hwif))
		return -1;

	return 0;
}

static void __devinit hpt374_init(struct pci_dev *dev, struct pci_dev *dev2)
{
	if (dev2->irq != dev->irq) {
		/* FIXME: we need a core pci_set_interrupt() */
		dev2->irq = dev->irq;
		printk(KERN_INFO DRV_NAME " %s: PCI config space interrupt "
			"fixed\n", pci_name(dev2));
	}
}

static void __devinit hpt371_init(struct pci_dev *dev)
{
	u8 mcr1 = 0;

	/*
	 * HPT371 chips physically have only one channel, the secondary one,
	 * but the primary channel registers do exist!  Go figure...
	 * So,  we manually disable the non-existing channel here
	 * (if the BIOS hasn't done this already).
	 */
	pci_read_config_byte(dev, 0x50, &mcr1);
	if (mcr1 & 0x04)
		pci_write_config_byte(dev, 0x50, mcr1 & ~0x04);
}

static int __devinit hpt36x_init(struct pci_dev *dev, struct pci_dev *dev2)
{
	u8 mcr1 = 0, pin1 = 0, pin2 = 0;

	/*
	 * Now we'll have to force both channels enabled if
	 * at least one of them has been enabled by BIOS...
	 */
	pci_read_config_byte(dev, 0x50, &mcr1);
	if (mcr1 & 0x30)
		pci_write_config_byte(dev, 0x50, mcr1 | 0x30);

	pci_read_config_byte(dev,  PCI_INTERRUPT_PIN, &pin1);
	pci_read_config_byte(dev2, PCI_INTERRUPT_PIN, &pin2);

	if (pin1 != pin2 && dev->irq == dev2->irq) {
		printk(KERN_INFO DRV_NAME " %s: onboard version of chipset, "
			"pin1=%d pin2=%d\n", pci_name(dev), pin1, pin2);
		return 1;
	}

	return 0;
}

#define IDE_HFLAGS_HPT3XX \
	(IDE_HFLAG_NO_ATAPI_DMA | \
	 IDE_HFLAG_OFF_BOARD)

static const struct ide_port_ops hpt3xx_port_ops = {
	.set_pio_mode		= hpt3xx_set_pio_mode,
	.set_dma_mode		= hpt3xx_set_mode,
	.quirkproc		= hpt3xx_quirkproc,
	.maskproc		= hpt3xx_maskproc,
	.mdma_filter		= hpt3xx_mdma_filter,
	.udma_filter		= hpt3xx_udma_filter,
	.cable_detect		= hpt3xx_cable_detect,
};

static const struct ide_dma_ops hpt37x_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ide_dma_setup,
	.dma_start		= ide_dma_start,
	.dma_end		= hpt374_dma_end,
	.dma_test_irq		= hpt374_dma_test_irq,
	.dma_lost_irq		= ide_dma_lost_irq,
	.dma_timer_expiry	= ide_dma_sff_timer_expiry,
	.dma_sff_read_status	= ide_dma_sff_read_status,
};

static const struct ide_dma_ops hpt370_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ide_dma_setup,
	.dma_start		= hpt370_dma_start,
	.dma_end		= hpt370_dma_end,
	.dma_test_irq		= ide_dma_test_irq,
	.dma_lost_irq		= ide_dma_lost_irq,
	.dma_timer_expiry	= ide_dma_sff_timer_expiry,
	.dma_clear		= hpt370_irq_timeout,
	.dma_sff_read_status	= ide_dma_sff_read_status,
};

static const struct ide_dma_ops hpt36x_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ide_dma_setup,
	.dma_start		= ide_dma_start,
	.dma_end		= ide_dma_end,
	.dma_test_irq		= ide_dma_test_irq,
	.dma_lost_irq		= hpt366_dma_lost_irq,
	.dma_timer_expiry	= ide_dma_sff_timer_expiry,
	.dma_sff_read_status	= ide_dma_sff_read_status,
};

static const struct ide_port_info hpt366_chipsets[] __devinitdata = {
	{	/* 0: HPT36x */
		.name		= DRV_NAME,
		.init_chipset	= init_chipset_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		/*
		 * HPT36x chips have one channel per function and have
		 * both channel enable bits located differently and visible
		 * to both functions -- really stupid design decision... :-(
		 * Bit 4 is for the primary channel, bit 5 for the secondary.
		 */
		.enablebits	= {{0x50,0x10,0x10}, {0x54,0x04,0x04}},
		.port_ops	= &hpt3xx_port_ops,
		.dma_ops	= &hpt36x_dma_ops,
		.host_flags	= IDE_HFLAGS_HPT3XX | IDE_HFLAG_SINGLE,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
	},
	{	/* 1: HPT3xx */
		.name		= DRV_NAME,
		.init_chipset	= init_chipset_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.enablebits	= {{0x50,0x04,0x04}, {0x54,0x04,0x04}},
		.port_ops	= &hpt3xx_port_ops,
		.dma_ops	= &hpt37x_dma_ops,
		.host_flags	= IDE_HFLAGS_HPT3XX,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
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
	const struct hpt_info *info = NULL;
	struct hpt_info *dyn_info;
	struct pci_dev *dev2 = NULL;
	struct ide_port_info d;
	u8 idx = id->driver_data;
	u8 rev = dev->revision;
	int ret;

	if ((idx == 0 || idx == 4) && (PCI_FUNC(dev->devfn) & 1))
		return -ENODEV;

	switch (idx) {
	case 0:
		if (rev < 3)
			info = &hpt36x;
		else {
			switch (min_t(u8, rev, 6)) {
			case 3: info = &hpt370;  break;
			case 4: info = &hpt370a; break;
			case 5: info = &hpt372;  break;
			case 6: info = &hpt372n; break;
			}
			idx++;
		}
		break;
	case 1:
		info = (rev > 1) ? &hpt372n : &hpt372a;
		break;
	case 2:
		info = (rev > 1) ? &hpt302n : &hpt302;
		break;
	case 3:
		hpt371_init(dev);
		info = (rev > 1) ? &hpt371n : &hpt371;
		break;
	case 4:
		info = &hpt374;
		break;
	case 5:
		info = &hpt372n;
		break;
	}

	printk(KERN_INFO DRV_NAME ": %s chipset detected\n", info->chip_name);

	d = hpt366_chipsets[min_t(u8, idx, 1)];

	d.udma_mask = info->udma_mask;

	/* fixup ->dma_ops for HPT370/HPT370A */
	if (info == &hpt370 || info == &hpt370a)
		d.dma_ops = &hpt370_dma_ops;

	if (info == &hpt36x || info == &hpt374)
		dev2 = pci_get_slot(dev->bus, dev->devfn + 1);

	dyn_info = kzalloc(sizeof(*dyn_info) * (dev2 ? 2 : 1), GFP_KERNEL);
	if (dyn_info == NULL) {
		printk(KERN_ERR "%s %s: out of memory!\n",
			d.name, pci_name(dev));
		pci_dev_put(dev2);
		return -ENOMEM;
	}

	/*
	 * Copy everything from a static "template" structure
	 * to just allocated per-chip hpt_info structure.
	 */
	memcpy(dyn_info, info, sizeof(*dyn_info));

	if (dev2) {
		memcpy(dyn_info + 1, info, sizeof(*dyn_info));

		if (info == &hpt374)
			hpt374_init(dev, dev2);
		else {
			if (hpt36x_init(dev, dev2))
				d.host_flags &= ~IDE_HFLAG_NON_BOOTABLE;
		}

		ret = ide_pci_init_two(dev, dev2, &d, dyn_info);
		if (ret < 0) {
			pci_dev_put(dev2);
			kfree(dyn_info);
		}
		return ret;
	}

	ret = ide_pci_init_one(dev, &d, dyn_info);
	if (ret < 0)
		kfree(dyn_info);

	return ret;
}

static void __devexit hpt366_remove(struct pci_dev *dev)
{
	struct ide_host *host = pci_get_drvdata(dev);
	struct ide_info *info = host->host_priv;
	struct pci_dev *dev2 = host->dev[1] ? to_pci_dev(host->dev[1]) : NULL;

	ide_pci_remove(dev);
	pci_dev_put(dev2);
	kfree(info);
}

static const struct pci_device_id hpt366_pci_tbl[] __devinitconst = {
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT366),  0 },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT372),  1 },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT302),  2 },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT371),  3 },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT374),  4 },
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT372N), 5 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, hpt366_pci_tbl);

static struct pci_driver hpt366_pci_driver = {
	.name		= "HPT366_IDE",
	.id_table	= hpt366_pci_tbl,
	.probe		= hpt366_init_one,
	.remove		= __devexit_p(hpt366_remove),
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init hpt366_ide_init(void)
{
	return ide_pci_register_driver(&hpt366_pci_driver);
}

static void __exit hpt366_ide_exit(void)
{
	pci_unregister_driver(&hpt366_pci_driver);
}

module_init(hpt366_ide_init);
module_exit(hpt366_ide_exit);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for Highpoint HPT366 IDE");
MODULE_LICENSE("GPL");
