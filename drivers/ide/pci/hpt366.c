/*
 * linux/drivers/ide/pci/hpt366.c		Version 1.02	Apr 18, 2007
 *
 * Copyright (C) 1999-2003		Andre Hedrick <andre@linux-ide.org>
 * Portions Copyright (C) 2001	        Sun Microsystems, Inc.
 * Portions Copyright (C) 2003		Red Hat Inc
 * Portions Copyright (C) 2005-2007	MontaVista Software, Inc.
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
 *		Alan Cox <alan@redhat.com>
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
 *   the wrong PCI frequency since DPLL has already been calibrated by BIOS
 * - fix the hotswap code:  it caused RESET- to glitch when tristating the bus,
 *   and for HPT36x the obsolete HDIO_TRISTATE_HWIF handler was called instead
 * - pass to init_chipset() handlers a copy of the IDE PCI device structure as
 *   they tamper with its fields
 * - pass  to the init_setup handlers a copy of the ide_pci_device_t structure
 *   since they may tamper with its fields
 * - prefix the driver startup messages with the real chip name
 * - claim the extra 240 bytes of I/O space for all chips
 * - optimize the rate masking/filtering and the drive list lookup code
 * - use pci_get_slot() to get to the function 1 of HPT36x/374
 * - cache offset of the channel's misc. control registers (MCRs) being used
 *   throughout the driver
 * - only touch the relevant MCR when detecting the cable type on HPT374's
 *   function 1
 * - rename all the register related variables consistently
 * - move all the interrupt twiddling code from the speedproc handlers into
 *   init_hwif_hpt366(), also grouping all the DMA related code together there
 * - merge two HPT37x speedproc handlers, fix the PIO timing register mask and
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
 *   supported DMA mode, and the chip settings table pointer filled, then, at
 *   the init_chipset stage, allocate per-chip instance  and fill it with the
 *   rest of the necessary information
 * - get rid of the constant thresholds in the HPT37x PCI clock detection code,
 *   switch  to calculating  PCI clock frequency based on the chip's base DPLL
 *   frequency
 * - switch to using the  DPLL clock and enable UltraATA/133 mode by default on
 *   anything  newer than HPT370/A
 * - fold PCI clock detection and DPLL setup code into init_chipset_hpt366(),
 *   also fixing the interchanged 25/40 MHz PCI clock cases for HPT36x chips;
 *   unify HPT36x/37x timing setup code and the speedproc handlers by joining
 *   the register setting lists into the table indexed by the clock selected
 *	Sergei Shtylyov, <sshtylyov@ru.mvista.com> or <source@mvista.com>
 */

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
#undef	HPT_DELAY_INTERRUPT
#define HPT_SERIALIZE_IO	0

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

#define HPT366_DEBUG_DRIVE_INFO		0
#define HPT374_ALLOW_ATA133_6		1
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

/*
 *	Hold all the HighPoint chip information in one place.
 */

struct hpt_info {
	u8 chip_type;		/* Chip type */
	u8 max_mode;		/* Speeds allowed */
	u8 dpll_clk;		/* DPLL clock in MHz */
	u8 pci_clk;		/* PCI  clock in MHz */
	u32 **settings; 	/* Chipset settings table */
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

static u32 *hpt36x_settings[NUM_ATA_CLOCKS] = {
	twenty_five_base_hpt36x,
	thirty_three_base_hpt36x,
	forty_base_hpt36x,
	NULL,
	NULL
};

static u32 *hpt37x_settings[NUM_ATA_CLOCKS] = {
	NULL,
	thirty_three_base_hpt37x,
	NULL,
	fifty_base_hpt37x,
	sixty_six_base_hpt37x
};

static struct hpt_info hpt36x __devinitdata = {
	.chip_type	= HPT36x,
	.max_mode	= (HPT366_ALLOW_ATA66_4 || HPT366_ALLOW_ATA66_3) ? 2 : 1,
	.dpll_clk	= 0,	/* no DPLL */
	.settings	= hpt36x_settings
};

static struct hpt_info hpt370 __devinitdata = {
	.chip_type	= HPT370,
	.max_mode	= HPT370_ALLOW_ATA100_5 ? 3 : 2,
	.dpll_clk	= 48,
	.settings	= hpt37x_settings
};

static struct hpt_info hpt370a __devinitdata = {
	.chip_type	= HPT370A,
	.max_mode	= HPT370_ALLOW_ATA100_5 ? 3 : 2,
	.dpll_clk	= 48,
	.settings	= hpt37x_settings
};

static struct hpt_info hpt374 __devinitdata = {
	.chip_type	= HPT374,
	.max_mode	= HPT374_ALLOW_ATA133_6 ? 4 : 3,
	.dpll_clk	= 48,
	.settings	= hpt37x_settings
};

static struct hpt_info hpt372 __devinitdata = {
	.chip_type	= HPT372,
	.max_mode	= HPT372_ALLOW_ATA133_6 ? 4 : 3,
	.dpll_clk	= 55,
	.settings	= hpt37x_settings
};

static struct hpt_info hpt372a __devinitdata = {
	.chip_type	= HPT372A,
	.max_mode	= HPT372_ALLOW_ATA133_6 ? 4 : 3,
	.dpll_clk	= 66,
	.settings	= hpt37x_settings
};

static struct hpt_info hpt302 __devinitdata = {
	.chip_type	= HPT302,
	.max_mode	= HPT302_ALLOW_ATA133_6 ? 4 : 3,
	.dpll_clk	= 66,
	.settings	= hpt37x_settings
};

static struct hpt_info hpt371 __devinitdata = {
	.chip_type	= HPT371,
	.max_mode	= HPT371_ALLOW_ATA133_6 ? 4 : 3,
	.dpll_clk	= 66,
	.settings	= hpt37x_settings
};

static struct hpt_info hpt372n __devinitdata = {
	.chip_type	= HPT372N,
	.max_mode	= HPT372_ALLOW_ATA133_6 ? 4 : 3,
	.dpll_clk	= 77,
	.settings	= hpt37x_settings
};

static struct hpt_info hpt302n __devinitdata = {
	.chip_type	= HPT302N,
	.max_mode	= HPT302_ALLOW_ATA133_6 ? 4 : 3,
	.dpll_clk	= 77,
	.settings	= hpt37x_settings
};

static struct hpt_info hpt371n __devinitdata = {
	.chip_type	= HPT371N,
	.max_mode	= HPT371_ALLOW_ATA133_6 ? 4 : 3,
	.dpll_clk	= 77,
	.settings	= hpt37x_settings
};

static int check_in_drive_list(ide_drive_t *drive, const char **list)
{
	struct hd_driveid *id = drive->id;

	while (*list)
		if (!strcmp(*list++,id->model))
			return 1;
	return 0;
}

static u8 hpt3xx_ratemask(ide_drive_t *drive)
{
	struct hpt_info *info	= pci_get_drvdata(HWIF(drive)->pci_dev);
	u8 mode			= info->max_mode;

	if (!eighty_ninty_three(drive) && mode)
		mode = min(mode, (u8)1);
	return mode;
}

/*
 *	Note for the future; the SATA hpt37x we must set
 *	either PIO or UDMA modes 0,4,5
 */
 
static u8 hpt3xx_ratefilter(ide_drive_t *drive, u8 speed)
{
	struct hpt_info *info	= pci_get_drvdata(HWIF(drive)->pci_dev);
	u8 chip_type		= info->chip_type;
	u8 mode			= hpt3xx_ratemask(drive);

	if (drive->media != ide_disk)
		return min(speed, (u8)XFER_PIO_4);

	switch (mode) {
		case 0x04:
			speed = min_t(u8, speed, XFER_UDMA_6);
			break;
		case 0x03:
			speed = min_t(u8, speed, XFER_UDMA_5);
			if (chip_type >= HPT374)
				break;
			if (!check_in_drive_list(drive, bad_ata100_5))
				goto check_bad_ata33;
			/* fall thru */
		case 0x02:
			speed = min_t(u8, speed, XFER_UDMA_4);

			/*
			 * CHECK ME, Does this need to be changed to HPT374 ??
			 */
			if (chip_type >= HPT370)
				goto check_bad_ata33;
			if (HPT366_ALLOW_ATA66_4 &&
			    !check_in_drive_list(drive, bad_ata66_4))
				goto check_bad_ata33;

			speed = min_t(u8, speed, XFER_UDMA_3);
			if (HPT366_ALLOW_ATA66_3 &&
			    !check_in_drive_list(drive, bad_ata66_3))
				goto check_bad_ata33;
			/* fall thru */
		case 0x01:
			speed = min_t(u8, speed, XFER_UDMA_2);

		check_bad_ata33:
			if (chip_type >= HPT370A)
				break;
			if (!check_in_drive_list(drive, bad_ata33))
				break;
			/* fall thru */
		case 0x00:
		default:
			speed = min_t(u8, speed, XFER_MW_DMA_2);
			break;
	}
	return speed;
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
	/*
	 * NOTE: info->settings only points to the pointer
	 * to the list of the actual register values
	 */
	return (*info->settings)[i];
}

static int hpt36x_tune_chipset(ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev  *dev	= hwif->pci_dev;
	struct hpt_info	*info	= pci_get_drvdata(dev);
	u8  speed		= hpt3xx_ratefilter(drive, xferspeed);
	u8  itr_addr		= drive->dn ? 0x44 : 0x40;
	u32 itr_mask		= speed < XFER_MW_DMA_0 ? 0x30070000 :
				 (speed < XFER_UDMA_0   ? 0xc0070000 : 0xc03800ff);
	u32 new_itr		= get_speed_setting(speed, info);
	u32 old_itr		= 0;

	/*
	 * Disable on-chip PIO FIFO/buffer (and PIO MST mode as well)
	 * to avoid problems handling I/O errors later
	 */
	pci_read_config_dword(dev, itr_addr, &old_itr);
	new_itr  = (new_itr & ~itr_mask) | (old_itr & itr_mask);
	new_itr &= ~0xc0000000;

	pci_write_config_dword(dev, itr_addr, new_itr);

	return ide_config_drive_speed(drive, speed);
}

static int hpt37x_tune_chipset(ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev  *dev	= hwif->pci_dev;
	struct hpt_info	*info	= pci_get_drvdata(dev);
	u8  speed		= hpt3xx_ratefilter(drive, xferspeed);
	u8  itr_addr		= 0x40 + (drive->dn * 4);
	u32 itr_mask		= speed < XFER_MW_DMA_0 ? 0x303c0000 :
				 (speed < XFER_UDMA_0   ? 0xc03c0000 : 0xc1c001ff);
	u32 new_itr		= get_speed_setting(speed, info);
	u32 old_itr		= 0;

	pci_read_config_dword(dev, itr_addr, &old_itr);
	new_itr = (new_itr & ~itr_mask) | (old_itr & itr_mask);
	
	if (speed < XFER_MW_DMA_0)
		new_itr &= ~0x80000000; /* Disable on-chip PIO FIFO/buffer */
	pci_write_config_dword(dev, itr_addr, new_itr);

	return ide_config_drive_speed(drive, speed);
}

static int hpt3xx_tune_chipset(ide_drive_t *drive, u8 speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hpt_info	*info	= pci_get_drvdata(hwif->pci_dev);

	if (info->chip_type >= HPT370)
		return hpt37x_tune_chipset(drive, speed);
	else	/* hpt368: hpt_minimum_revision(dev, 2) */
		return hpt36x_tune_chipset(drive, speed);
}

static void hpt3xx_tune_drive(ide_drive_t *drive, u8 pio)
{
	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
	(void) hpt3xx_tune_chipset (drive, XFER_PIO_0 + pio);
}

/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.  Initially designed for
 * HPT366 UDMA chipset by HighPoint|Triones Technologies, Inc.
 *
 */
static int config_chipset_for_dma(ide_drive_t *drive)
{
	u8 speed = ide_dma_speed(drive, hpt3xx_ratemask(drive));

	if (!speed)
		return 0;

	(void) hpt3xx_tune_chipset(drive, speed);
	return ide_dma_enable(drive);
}

static int hpt3xx_quirkproc(ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	const  char **list	= quirk_drives;

	while (*list)
		if (strstr(id->model, *list++))
			return 1;
	return 0;
}

static void hpt3xx_intrproc(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	if (drive->quirk_list)
		return;
	/* drives in the quirk_list may not like intr setups/cleanups */
	hwif->OUTB(drive->ctl | 2, IDE_CONTROL_REG);
}

static void hpt3xx_maskproc(ide_drive_t *drive, int mask)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev	*dev	= hwif->pci_dev;
	struct hpt_info *info	= pci_get_drvdata(dev);

	if (drive->quirk_list) {
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
		} else {
			if (mask)
				disable_irq(hwif->irq);
			else
				enable_irq (hwif->irq);
		}
	} else
		hwif->OUTB(mask ? (drive->ctl | 2) : (drive->ctl & ~2),
			   IDE_CONTROL_REG);
}

static int hpt366_config_drive_xfer_rate(ide_drive_t *drive)
{
	drive->init_speed = 0;

	if (ide_use_dma(drive) && config_chipset_for_dma(drive))
		return 0;

	if (ide_use_fast_pio(drive))
		hpt3xx_tune_drive(drive, 255);

	return -1;
}

/*
 * This is specific to the HPT366 UDMA chipset
 * by HighPoint|Triones Technologies, Inc.
 */
static int hpt366_ide_dma_lostirq(ide_drive_t *drive)
{
	struct pci_dev *dev = HWIF(drive)->pci_dev;
	u8 mcr1 = 0, mcr3 = 0, scr1 = 0;

	pci_read_config_byte(dev, 0x50, &mcr1);
	pci_read_config_byte(dev, 0x52, &mcr3);
	pci_read_config_byte(dev, 0x5a, &scr1);
	printk("%s: (%s)  mcr1=0x%02x, mcr3=0x%02x, scr1=0x%02x\n",
		drive->name, __FUNCTION__, mcr1, mcr3, scr1);
	if (scr1 & 0x10)
		pci_write_config_byte(dev, 0x5a, scr1 & ~0x10);
	return __ide_dma_lostirq(drive);
}

static void hpt370_clear_engine(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	pci_write_config_byte(hwif->pci_dev, hwif->select_data, 0x37);
	udelay(10);
}

static void hpt370_irq_timeout(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u16 bfifo		= 0;
	u8  dma_cmd;

	pci_read_config_word(hwif->pci_dev, hwif->select_data + 2, &bfifo);
	printk(KERN_DEBUG "%s: %d bytes in FIFO\n", drive->name, bfifo & 0x1ff);

	/* get DMA command mode */
	dma_cmd = hwif->INB(hwif->dma_command);
	/* stop DMA */
	hwif->OUTB(dma_cmd & ~0x1, hwif->dma_command);
	hpt370_clear_engine(drive);
}

static void hpt370_ide_dma_start(ide_drive_t *drive)
{
#ifdef HPT_RESET_STATE_ENGINE
	hpt370_clear_engine(drive);
#endif
	ide_dma_start(drive);
}

static int hpt370_ide_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8  dma_stat		= hwif->INB(hwif->dma_status);

	if (dma_stat & 0x01) {
		/* wait a little */
		udelay(20);
		dma_stat = hwif->INB(hwif->dma_status);
		if (dma_stat & 0x01)
			hpt370_irq_timeout(drive);
	}
	return __ide_dma_end(drive);
}

static int hpt370_ide_dma_timeout(ide_drive_t *drive)
{
	hpt370_irq_timeout(drive);
	return __ide_dma_timeout(drive);
}

/* returns 1 if DMA IRQ issued, 0 otherwise */
static int hpt374_ide_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u16 bfifo		= 0;
	u8  dma_stat;

	pci_read_config_word(hwif->pci_dev, hwif->select_data + 2, &bfifo);
	if (bfifo & 0x1FF) {
//		printk("%s: %d bytes in FIFO\n", drive->name, bfifo);
		return 0;
	}

	dma_stat = inb(hwif->dma_status);
	/* return 1 if INTR asserted */
	if (dma_stat & 4)
		return 1;

	if (!drive->waiting_for_dma)
		printk(KERN_WARNING "%s: (%s) called while not waiting\n",
				drive->name, __FUNCTION__);
	return 0;
}

static int hpt374_ide_dma_end(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev	*dev	= hwif->pci_dev;
	u8 mcr	= 0, mcr_addr	= hwif->select_data;
	u8 bwsr = 0, mask	= hwif->channel ? 0x02 : 0x01;

	pci_read_config_byte(dev, 0x6a, &bwsr);
	pci_read_config_byte(dev, mcr_addr, &mcr);
	if (bwsr & mask)
		pci_write_config_byte(dev, mcr_addr, mcr | 0x30);
	return __ide_dma_end(drive);
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
	u8 scr2 = hwif->INB(hwif->dma_master + 0x7b);

	if ((scr2 & 0x7f) == mode)
		return;

	/* Tristate the bus */
	hwif->OUTB(0x80, hwif->dma_master + 0x73);
	hwif->OUTB(0x80, hwif->dma_master + 0x77);

	/* Switch clock and reset channels */
	hwif->OUTB(mode, hwif->dma_master + 0x7b);
	hwif->OUTB(0xc0, hwif->dma_master + 0x79);

	/*
	 * Reset the state machines.
	 * NOTE: avoid accidentally enabling the disabled channels.
	 */
	hwif->OUTB(hwif->INB(hwif->dma_master + 0x70) | 0x32,
		   hwif->dma_master + 0x70);
	hwif->OUTB(hwif->INB(hwif->dma_master + 0x74) | 0x32,
		   hwif->dma_master + 0x74);

	/* Complete reset */
	hwif->OUTB(0x00, hwif->dma_master + 0x79);

	/* Reconnect channels to bus */
	hwif->OUTB(0x00, hwif->dma_master + 0x73);
	hwif->OUTB(0x00, hwif->dma_master + 0x77);
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
	hpt3xxn_set_clock(HWIF(drive), rq_data_dir(rq) ? 0x23 : 0x21);
}

/* 
 * Set/get power state for a drive.
 * NOTE: affects both drives on each channel.
 *
 * When we turn the power back on, we need to re-initialize things.
 */
#define TRISTATE_BIT  0x8000

static int hpt3xx_busproc(ide_drive_t *drive, int state)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8  mcr_addr		= hwif->select_data + 2;
	u8  resetmask		= hwif->channel ? 0x80 : 0x40;
	u8  bsr2		= 0;
	u16 mcr			= 0;

	hwif->bus_state = state;

	/* Grab the status. */
	pci_read_config_word(dev, mcr_addr, &mcr);
	pci_read_config_byte(dev, 0x59, &bsr2);

	/*
	 * Set the state. We don't set it if we don't need to do so.
	 * Make sure that the drive knows that it has failed if it's off.
	 */
	switch (state) {
	case BUSSTATE_ON:
		if (!(bsr2 & resetmask))
			return 0;
		hwif->drives[0].failures = hwif->drives[1].failures = 0;

		pci_write_config_byte(dev, 0x59, bsr2 & ~resetmask);
		pci_write_config_word(dev, mcr_addr, mcr & ~TRISTATE_BIT);
		return 0;
	case BUSSTATE_OFF:
		if ((bsr2 & resetmask) && !(mcr & TRISTATE_BIT))
			return 0;
		mcr &= ~TRISTATE_BIT;
		break;
	case BUSSTATE_TRISTATE:
		if ((bsr2 & resetmask) &&  (mcr & TRISTATE_BIT))
			return 0;
		mcr |= TRISTATE_BIT;
		break;
	default:
		return -EINVAL;
	}

	hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
	hwif->drives[1].failures = hwif->drives[1].max_failures + 1;

	pci_write_config_word(dev, mcr_addr, mcr);
	pci_write_config_byte(dev, 0x59, bsr2 | resetmask);
	return 0;
}

/**
 *	hpt37x_calibrate_dpll	-	calibrate the DPLL
 *	@dev: PCI device
 *
 *	Perform a calibration cycle on the DPLL.
 *	Returns 1 if this succeeds
 */
static int __devinit hpt37x_calibrate_dpll(struct pci_dev *dev, u16 f_low, u16 f_high)
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

static unsigned int __devinit init_chipset_hpt366(struct pci_dev *dev, const char *name)
{
	struct hpt_info *info	= kmalloc(sizeof(struct hpt_info), GFP_KERNEL);
	unsigned long io_base	= pci_resource_start(dev, 4);
	u8 pci_clk,  dpll_clk	= 0;	/* PCI and DPLL clock in MHz */
	enum ata_clock	clock;

	if (info == NULL) {
		printk(KERN_ERR "%s: out of memory!\n", name);
		return -ENOMEM;
	}

	/*
	 * Copy everything from a static "template" structure
	 * to just allocated per-chip hpt_info structure.
	 */
	*info = *(struct hpt_info *)pci_get_drvdata(dev);

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

	/*
	 * First, try to estimate the PCI clock frequency...
	 */
	if (info->chip_type >= HPT370) {
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
		if (info->chip_type == HPT372A)
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
		 * NOTE: This register is only accessible via I/O space.
		 *
		 * In case the signature check fails, we'll have to resort to
		 * reading the f_CNT register itself in hopes that nobody has
		 * touched the DPLL yet...
		 */
		temp = inl(io_base + 0x90);
		if ((temp & 0xFFFFF000) != 0xABCDE000) {
			int i;

			printk(KERN_WARNING "%s: no clock data saved by BIOS\n",
			       name);

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

		printk(KERN_INFO "%s: DPLL base: %d MHz, f_CNT: %d, "
		       "assuming %d MHz PCI\n", name, dpll_clk, f_cnt, pci_clk);
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
	if (info->chip_type >= HPT374 || info->settings[clock] == NULL) {
		u16 f_low, delta = pci_clk < 50 ? 2 : 4;
		int adjust;

		 /*
		  * Select 66 MHz DPLL clock only if UltraATA/133 mode is
		  * supported/enabled, use 50 MHz DPLL clock otherwise...
		  */
		if (info->max_mode == 0x04) {
			dpll_clk = 66;
			clock = ATA_CLOCK_66MHZ;
		} else if (dpll_clk) {	/* HPT36x chips don't have DPLL */
			dpll_clk = 50;
			clock = ATA_CLOCK_50MHZ;
		}

		if (info->settings[clock] == NULL) {
			printk(KERN_ERR "%s: unknown bus timing!\n", name);
			kfree(info);
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
			printk(KERN_ERR "%s: DPLL did not stabilize!\n", name);
			kfree(info);
			return -EIO;
		}

		printk("%s: using %d MHz DPLL clock\n", name, dpll_clk);
	} else {
		/* Mark the fact that we're not using the DPLL. */
		dpll_clk = 0;

		printk("%s: using %d MHz PCI clock\n", name, pci_clk);
	}

	/*
	 * Advance the table pointer to a slot which points to the list
	 * of the register values settings matching the clock being used.
	 */
	info->settings += clock;

	/* Store the clock frequencies. */
	info->dpll_clk	= dpll_clk;
	info->pci_clk	= pci_clk;

	/* Point to this chip's own instance of the hpt_info structure. */
	pci_set_drvdata(dev, info);

	if (info->chip_type >= HPT370) {
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
	if (info->chip_type == HPT371N && clock == ATA_CLOCK_66MHZ)

		outb(inb(io_base + 0x9c) | 0x04, io_base + 0x9c);

	return dev->irq;
}

static void __devinit init_hwif_hpt366(ide_hwif_t *hwif)
{
	struct pci_dev	*dev		= hwif->pci_dev;
	struct hpt_info *info		= pci_get_drvdata(dev);
	int serialize			= HPT_SERIALIZE_IO;
	u8  scr1 = 0, ata66		= (hwif->channel) ? 0x01 : 0x02;
	u8  chip_type			= info->chip_type;
	u8  new_mcr, old_mcr 		= 0;

	/* Cache the channel's MISC. control registers' offset */
	hwif->select_data		= hwif->channel ? 0x54 : 0x50;

	hwif->tuneproc			= &hpt3xx_tune_drive;
	hwif->speedproc			= &hpt3xx_tune_chipset;
	hwif->quirkproc			= &hpt3xx_quirkproc;
	hwif->intrproc			= &hpt3xx_intrproc;
	hwif->maskproc			= &hpt3xx_maskproc;
	hwif->busproc			= &hpt3xx_busproc;

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
		serialize = 1;
		hwif->rw_disk = &hpt3xxn_rw_disk;
	}

	/* Serialize access to this device if needed */
	if (serialize && hwif->mate)
		hwif->serialized = hwif->mate->serialized = 1;

	/*
	 * Disable the "fast interrupt" prediction.  Don't hold off
	 * on interrupts. (== 0x01 despite what the docs say)
	 */
	pci_read_config_byte(dev, hwif->select_data + 1, &old_mcr);

	if (info->chip_type >= HPT374)
		new_mcr = old_mcr & ~0x07;
	else if (info->chip_type >= HPT370) {
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
		pci_write_config_byte(dev, hwif->select_data + 1, new_mcr);

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = hwif->drives[1].autotune = 1;
		return;
	}

	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x07;

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

		pci_read_config_word (dev, mcr_addr, &mcr);
		pci_write_config_word(dev, mcr_addr, (mcr | 0x8000));
		/* now read cable id register */
		pci_read_config_byte (dev, 0x5a, &scr1);
		pci_write_config_word(dev, mcr_addr, mcr);
	} else if (chip_type >= HPT370) {
		/*
		 * HPT370/372 and 374 pcifn 0
		 * - clear bit 0 of reg 0x5b to enable P/SCBLID as inputs
		 */
		u8 scr2 = 0;

		pci_read_config_byte (dev, 0x5b, &scr2);
		pci_write_config_byte(dev, 0x5b, (scr2 & ~1));
		/* now read cable id register */
		pci_read_config_byte (dev, 0x5a, &scr1);
		pci_write_config_byte(dev, 0x5b,  scr2);
	} else
		pci_read_config_byte (dev, 0x5a, &scr1);

	if (!hwif->udma_four)
		hwif->udma_four = (scr1 & ata66) ? 0 : 1;

	hwif->ide_dma_check		= &hpt366_config_drive_xfer_rate;

	if (chip_type >= HPT374) {
		hwif->ide_dma_test_irq	= &hpt374_ide_dma_test_irq;
		hwif->ide_dma_end	= &hpt374_ide_dma_end;
	} else if (chip_type >= HPT370) {
		hwif->dma_start 	= &hpt370_ide_dma_start;
		hwif->ide_dma_end	= &hpt370_ide_dma_end;
		hwif->ide_dma_timeout	= &hpt370_ide_dma_timeout;
	} else
		hwif->ide_dma_lostirq	= &hpt366_ide_dma_lostirq;

	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->drives[1].autodma = hwif->autodma;
}

static void __devinit init_dma_hpt366(ide_hwif_t *hwif, unsigned long dmabase)
{
	struct pci_dev	*dev		= hwif->pci_dev;
	u8 masterdma	= 0, slavedma	= 0;
	u8 dma_new	= 0, dma_old	= 0;
	unsigned long flags;

	dma_old = hwif->INB(dmabase + 2);

	local_irq_save(flags);

	dma_new = dma_old;
	pci_read_config_byte(dev, hwif->channel ? 0x4b : 0x43, &masterdma);
	pci_read_config_byte(dev, hwif->channel ? 0x4f : 0x47,  &slavedma);

	if (masterdma & 0x30)	dma_new |= 0x20;
	if ( slavedma & 0x30)	dma_new |= 0x40;
	if (dma_new != dma_old)
		hwif->OUTB(dma_new, dmabase + 2);

	local_irq_restore(flags);

	ide_setup_dma(hwif, dmabase, 8);
}

static int __devinit init_setup_hpt374(struct pci_dev *dev, ide_pci_device_t *d)
{
	struct pci_dev *dev2;

	if (PCI_FUNC(dev->devfn) & 1)
		return -ENODEV;

	pci_set_drvdata(dev, &hpt374);

	if ((dev2 = pci_get_slot(dev->bus, dev->devfn + 1)) != NULL) {
		int ret;

		pci_set_drvdata(dev2, &hpt374);

		if (dev2->irq != dev->irq) {
			/* FIXME: we need a core pci_set_interrupt() */
			dev2->irq = dev->irq;
			printk(KERN_WARNING "%s: PCI config space interrupt "
			       "fixed.\n", d->name);
		}
		ret = ide_setup_pci_devices(dev, dev2, d);
		if (ret < 0)
			pci_dev_put(dev2);
		return ret;
	}
	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_hpt372n(struct pci_dev *dev, ide_pci_device_t *d)
{
	pci_set_drvdata(dev, &hpt372n);

	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_hpt371(struct pci_dev *dev, ide_pci_device_t *d)
{
	struct hpt_info *info;
	u8 rev = 0, mcr1 = 0;

	pci_read_config_byte(dev, PCI_REVISION_ID, &rev);

	if (rev > 1) {
		d->name = "HPT371N";

		info = &hpt371n;
	} else
		info = &hpt371;

	/*
	 * HPT371 chips physically have only one channel, the secondary one,
	 * but the primary channel registers do exist!  Go figure...
	 * So,  we manually disable the non-existing channel here
	 * (if the BIOS hasn't done this already).
	 */
	pci_read_config_byte(dev, 0x50, &mcr1);
	if (mcr1 & 0x04)
		pci_write_config_byte(dev, 0x50, mcr1 & ~0x04);

	pci_set_drvdata(dev, info);

	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_hpt372a(struct pci_dev *dev, ide_pci_device_t *d)
{
	struct hpt_info *info;
	u8 rev = 0;

	pci_read_config_byte(dev, PCI_REVISION_ID, &rev);

	if (rev > 1) {
		d->name = "HPT372N";

		info = &hpt372n;
	} else
		info = &hpt372a;
	pci_set_drvdata(dev, info);

	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_hpt302(struct pci_dev *dev, ide_pci_device_t *d)
{
	struct hpt_info *info;
	u8 rev = 0;

	pci_read_config_byte(dev, PCI_REVISION_ID, &rev);

	if (rev > 1) {
		d->name = "HPT302N";

		info = &hpt302n;
	} else
		info = &hpt302;
	pci_set_drvdata(dev, info);

	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_hpt366(struct pci_dev *dev, ide_pci_device_t *d)
{
	struct pci_dev *dev2;
	u8 rev = 0;
	static char   *chipset_names[] = { "HPT366", "HPT366",  "HPT368",
					   "HPT370", "HPT370A", "HPT372",
					   "HPT372N" };
	static struct hpt_info *info[] = { &hpt36x,  &hpt36x,  &hpt36x,
					   &hpt370,  &hpt370a, &hpt372,
					   &hpt372n  };

	if (PCI_FUNC(dev->devfn) & 1)
		return -ENODEV;

	pci_read_config_byte(dev, PCI_REVISION_ID, &rev);

	if (rev > 6)
		rev = 6;
		
	d->name = chipset_names[rev];

	pci_set_drvdata(dev, info[rev]);

	if (rev > 2)
		goto init_single;

	d->channels = 1;

	if ((dev2 = pci_get_slot(dev->bus, dev->devfn + 1)) != NULL) {
	  	u8  pin1 = 0, pin2 = 0;
		int ret;

		pci_set_drvdata(dev2, info[rev]);

		pci_read_config_byte(dev,  PCI_INTERRUPT_PIN, &pin1);
		pci_read_config_byte(dev2, PCI_INTERRUPT_PIN, &pin2);
		if (pin1 != pin2 && dev->irq == dev2->irq) {
			d->bootable = ON_BOARD;
			printk("%s: onboard version of chipset, pin1=%d pin2=%d\n",
			       d->name, pin1, pin2);
		}
		ret = ide_setup_pci_devices(dev, dev2, d);
		if (ret < 0)
			pci_dev_put(dev2);
		return ret;
	}
init_single:
	return ide_setup_pci_device(dev, d);
}

static ide_pci_device_t hpt366_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "HPT366",
		.init_setup	= init_setup_hpt366,
		.init_chipset	= init_chipset_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x04,0x04}, {0x54,0x04,0x04}},
		.bootable	= OFF_BOARD,
		.extra		= 240
	},{	/* 1 */
		.name		= "HPT372A",
		.init_setup	= init_setup_hpt372a,
		.init_chipset	= init_chipset_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x04,0x04}, {0x54,0x04,0x04}},
		.bootable	= OFF_BOARD,
		.extra		= 240
	},{	/* 2 */
		.name		= "HPT302",
		.init_setup	= init_setup_hpt302,
		.init_chipset	= init_chipset_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x04,0x04}, {0x54,0x04,0x04}},
		.bootable	= OFF_BOARD,
		.extra		= 240
	},{	/* 3 */
		.name		= "HPT371",
		.init_setup	= init_setup_hpt371,
		.init_chipset	= init_chipset_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x04,0x04}, {0x54,0x04,0x04}},
		.bootable	= OFF_BOARD,
		.extra		= 240
	},{	/* 4 */
		.name		= "HPT374",
		.init_setup	= init_setup_hpt374,
		.init_chipset	= init_chipset_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,	/* 4 */
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x04,0x04}, {0x54,0x04,0x04}},
		.bootable	= OFF_BOARD,
		.extra		= 240
	},{	/* 5 */
		.name		= "HPT372N",
		.init_setup	= init_setup_hpt372n,
		.init_chipset	= init_chipset_hpt366,
		.init_hwif	= init_hwif_hpt366,
		.init_dma	= init_dma_hpt366,
		.channels	= 2,	/* 4 */
		.autodma	= AUTODMA,
		.enablebits	= {{0x50,0x04,0x04}, {0x54,0x04,0x04}},
		.bootable	= OFF_BOARD,
		.extra		= 240
	}
};

/**
 *	hpt366_init_one	-	called when an HPT366 is found
 *	@dev: the hpt366 device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 *
 *	NOTE: since we'll have to modify some fields of the ide_pci_device_t
 *	structure depending on the chip's revision, we'd better pass a local
 *	copy down the call chain...
 */
static int __devinit hpt366_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t d = hpt366_chipsets[id->driver_data];

	return d.init_setup(dev, &d);
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

static int __init hpt366_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(hpt366_ide_init);

MODULE_AUTHOR("Andre Hedrick");
MODULE_DESCRIPTION("PCI driver module for Highpoint HPT366 IDE");
MODULE_LICENSE("GPL");
