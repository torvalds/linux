/*
 * linux/drivers/ide/ppc/pmac.c
 *
 * Support for IDE interfaces on PowerMacs.
 * These IDE interfaces are memory-mapped and have a DBDMA channel
 * for doing DMA.
 *
 *  Copyright (C) 1998-2003 Paul Mackerras & Ben. Herrenschmidt
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * Some code taken from drivers/ide/ide-dma.c:
 *
 *  Copyright (c) 1995-1998  Mark Lord
 *
 * TODO: - Use pre-calculated (kauai) timing tables all the time and
 * get rid of the "rounded" tables used previously, so we have the
 * same table format for all controllers and can then just have one
 * big table
 * 
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/scatterlist.h>

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/dbdma.h>
#include <asm/ide.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/sections.h>
#include <asm/irq.h>

#ifndef CONFIG_PPC64
#include <asm/mediabay.h>
#endif

#include "../ide-timing.h"

#undef IDE_PMAC_DEBUG

#define DMA_WAIT_TIMEOUT	50

typedef struct pmac_ide_hwif {
	unsigned long			regbase;
	int				irq;
	int				kind;
	int				aapl_bus_id;
	unsigned			cable_80 : 1;
	unsigned			mediabay : 1;
	unsigned			broken_dma : 1;
	unsigned			broken_dma_warn : 1;
	struct device_node*		node;
	struct macio_dev		*mdev;
	u32				timings[4];
	volatile u32 __iomem *		*kauai_fcr;
#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
	/* Those fields are duplicating what is in hwif. We currently
	 * can't use the hwif ones because of some assumptions that are
	 * beeing done by the generic code about the kind of dma controller
	 * and format of the dma table. This will have to be fixed though.
	 */
	volatile struct dbdma_regs __iomem *	dma_regs;
	struct dbdma_cmd*		dma_table_cpu;
#endif
	
} pmac_ide_hwif_t;

static pmac_ide_hwif_t pmac_ide[MAX_HWIFS];
static int pmac_ide_count;

enum {
	controller_ohare,	/* OHare based */
	controller_heathrow,	/* Heathrow/Paddington */
	controller_kl_ata3,	/* KeyLargo ATA-3 */
	controller_kl_ata4,	/* KeyLargo ATA-4 */
	controller_un_ata6,	/* UniNorth2 ATA-6 */
	controller_k2_ata6,	/* K2 ATA-6 */
	controller_sh_ata6,	/* Shasta ATA-6 */
};

static const char* model_name[] = {
	"OHare ATA",		/* OHare based */
	"Heathrow ATA",		/* Heathrow/Paddington */
	"KeyLargo ATA-3",	/* KeyLargo ATA-3 (MDMA only) */
	"KeyLargo ATA-4",	/* KeyLargo ATA-4 (UDMA/66) */
	"UniNorth ATA-6",	/* UniNorth2 ATA-6 (UDMA/100) */
	"K2 ATA-6",		/* K2 ATA-6 (UDMA/100) */
	"Shasta ATA-6",		/* Shasta ATA-6 (UDMA/133) */
};

/*
 * Extra registers, both 32-bit little-endian
 */
#define IDE_TIMING_CONFIG	0x200
#define IDE_INTERRUPT		0x300

/* Kauai (U2) ATA has different register setup */
#define IDE_KAUAI_PIO_CONFIG	0x200
#define IDE_KAUAI_ULTRA_CONFIG	0x210
#define IDE_KAUAI_POLL_CONFIG	0x220

/*
 * Timing configuration register definitions
 */

/* Number of IDE_SYSCLK_NS ticks, argument is in nanoseconds */
#define SYSCLK_TICKS(t)		(((t) + IDE_SYSCLK_NS - 1) / IDE_SYSCLK_NS)
#define SYSCLK_TICKS_66(t)	(((t) + IDE_SYSCLK_66_NS - 1) / IDE_SYSCLK_66_NS)
#define IDE_SYSCLK_NS		30	/* 33Mhz cell */
#define IDE_SYSCLK_66_NS	15	/* 66Mhz cell */

/* 133Mhz cell, found in shasta.
 * See comments about 100 Mhz Uninorth 2...
 * Note that PIO_MASK and MDMA_MASK seem to overlap
 */
#define TR_133_PIOREG_PIO_MASK		0xff000fff
#define TR_133_PIOREG_MDMA_MASK		0x00fff800
#define TR_133_UDMAREG_UDMA_MASK	0x0003ffff
#define TR_133_UDMAREG_UDMA_EN		0x00000001

/* 100Mhz cell, found in Uninorth 2. I don't have much infos about
 * this one yet, it appears as a pci device (106b/0033) on uninorth
 * internal PCI bus and it's clock is controlled like gem or fw. It
 * appears to be an evolution of keylargo ATA4 with a timing register
 * extended to 2 32bits registers and a similar DBDMA channel. Other
 * registers seem to exist but I can't tell much about them.
 * 
 * So far, I'm using pre-calculated tables for this extracted from
 * the values used by the MacOS X driver.
 * 
 * The "PIO" register controls PIO and MDMA timings, the "ULTRA"
 * register controls the UDMA timings. At least, it seems bit 0
 * of this one enables UDMA vs. MDMA, and bits 4..7 are the
 * cycle time in units of 10ns. Bits 8..15 are used by I don't
 * know their meaning yet
 */
#define TR_100_PIOREG_PIO_MASK		0xff000fff
#define TR_100_PIOREG_MDMA_MASK		0x00fff000
#define TR_100_UDMAREG_UDMA_MASK	0x0000ffff
#define TR_100_UDMAREG_UDMA_EN		0x00000001


/* 66Mhz cell, found in KeyLargo. Can do ultra mode 0 to 2 on
 * 40 connector cable and to 4 on 80 connector one.
 * Clock unit is 15ns (66Mhz)
 * 
 * 3 Values can be programmed:
 *  - Write data setup, which appears to match the cycle time. They
 *    also call it DIOW setup.
 *  - Ready to pause time (from spec)
 *  - Address setup. That one is weird. I don't see where exactly
 *    it fits in UDMA cycles, I got it's name from an obscure piece
 *    of commented out code in Darwin. They leave it to 0, we do as
 *    well, despite a comment that would lead to think it has a
 *    min value of 45ns.
 * Apple also add 60ns to the write data setup (or cycle time ?) on
 * reads.
 */
#define TR_66_UDMA_MASK			0xfff00000
#define TR_66_UDMA_EN			0x00100000 /* Enable Ultra mode for DMA */
#define TR_66_UDMA_ADDRSETUP_MASK	0xe0000000 /* Address setup */
#define TR_66_UDMA_ADDRSETUP_SHIFT	29
#define TR_66_UDMA_RDY2PAUS_MASK	0x1e000000 /* Ready 2 pause time */
#define TR_66_UDMA_RDY2PAUS_SHIFT	25
#define TR_66_UDMA_WRDATASETUP_MASK	0x01e00000 /* Write data setup time */
#define TR_66_UDMA_WRDATASETUP_SHIFT	21
#define TR_66_MDMA_MASK			0x000ffc00
#define TR_66_MDMA_RECOVERY_MASK	0x000f8000
#define TR_66_MDMA_RECOVERY_SHIFT	15
#define TR_66_MDMA_ACCESS_MASK		0x00007c00
#define TR_66_MDMA_ACCESS_SHIFT		10
#define TR_66_PIO_MASK			0x000003ff
#define TR_66_PIO_RECOVERY_MASK		0x000003e0
#define TR_66_PIO_RECOVERY_SHIFT	5
#define TR_66_PIO_ACCESS_MASK		0x0000001f
#define TR_66_PIO_ACCESS_SHIFT		0

/* 33Mhz cell, found in OHare, Heathrow (& Paddington) and KeyLargo
 * Can do pio & mdma modes, clock unit is 30ns (33Mhz)
 * 
 * The access time and recovery time can be programmed. Some older
 * Darwin code base limit OHare to 150ns cycle time. I decided to do
 * the same here fore safety against broken old hardware ;)
 * The HalfTick bit, when set, adds half a clock (15ns) to the access
 * time and removes one from recovery. It's not supported on KeyLargo
 * implementation afaik. The E bit appears to be set for PIO mode 0 and
 * is used to reach long timings used in this mode.
 */
#define TR_33_MDMA_MASK			0x003ff800
#define TR_33_MDMA_RECOVERY_MASK	0x001f0000
#define TR_33_MDMA_RECOVERY_SHIFT	16
#define TR_33_MDMA_ACCESS_MASK		0x0000f800
#define TR_33_MDMA_ACCESS_SHIFT		11
#define TR_33_MDMA_HALFTICK		0x00200000
#define TR_33_PIO_MASK			0x000007ff
#define TR_33_PIO_E			0x00000400
#define TR_33_PIO_RECOVERY_MASK		0x000003e0
#define TR_33_PIO_RECOVERY_SHIFT	5
#define TR_33_PIO_ACCESS_MASK		0x0000001f
#define TR_33_PIO_ACCESS_SHIFT		0

/*
 * Interrupt register definitions
 */
#define IDE_INTR_DMA			0x80000000
#define IDE_INTR_DEVICE			0x40000000

/*
 * FCR Register on Kauai. Not sure what bit 0x4 is  ...
 */
#define KAUAI_FCR_UATA_MAGIC		0x00000004
#define KAUAI_FCR_UATA_RESET_N		0x00000002
#define KAUAI_FCR_UATA_ENABLE		0x00000001

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC

/* Rounded Multiword DMA timings
 * 
 * I gave up finding a generic formula for all controller
 * types and instead, built tables based on timing values
 * used by Apple in Darwin's implementation.
 */
struct mdma_timings_t {
	int	accessTime;
	int	recoveryTime;
	int	cycleTime;
};

struct mdma_timings_t mdma_timings_33[] =
{
    { 240, 240, 480 },
    { 180, 180, 360 },
    { 135, 135, 270 },
    { 120, 120, 240 },
    { 105, 105, 210 },
    {  90,  90, 180 },
    {  75,  75, 150 },
    {  75,  45, 120 },
    {   0,   0,   0 }
};

struct mdma_timings_t mdma_timings_33k[] =
{
    { 240, 240, 480 },
    { 180, 180, 360 },
    { 150, 150, 300 },
    { 120, 120, 240 },
    {  90, 120, 210 },
    {  90,  90, 180 },
    {  90,  60, 150 },
    {  90,  30, 120 },
    {   0,   0,   0 }
};

struct mdma_timings_t mdma_timings_66[] =
{
    { 240, 240, 480 },
    { 180, 180, 360 },
    { 135, 135, 270 },
    { 120, 120, 240 },
    { 105, 105, 210 },
    {  90,  90, 180 },
    {  90,  75, 165 },
    {  75,  45, 120 },
    {   0,   0,   0 }
};

/* KeyLargo ATA-4 Ultra DMA timings (rounded) */
struct {
	int	addrSetup; /* ??? */
	int	rdy2pause;
	int	wrDataSetup;
} kl66_udma_timings[] =
{
    {   0, 180,  120 },	/* Mode 0 */
    {   0, 150,  90 },	/*      1 */
    {   0, 120,  60 },	/*      2 */
    {   0, 90,   45 },	/*      3 */
    {   0, 90,   30 }	/*      4 */
};

/* UniNorth 2 ATA/100 timings */
struct kauai_timing {
	int	cycle_time;
	u32	timing_reg;
};

static struct kauai_timing	kauai_pio_timings[] =
{
	{ 930	, 0x08000fff },
	{ 600	, 0x08000a92 },
	{ 383	, 0x0800060f },
	{ 360	, 0x08000492 },
	{ 330	, 0x0800048f },
	{ 300	, 0x080003cf },
	{ 270	, 0x080003cc },
	{ 240	, 0x0800038b },
	{ 239	, 0x0800030c },
	{ 180	, 0x05000249 },
	{ 120	, 0x04000148 }
};

static struct kauai_timing	kauai_mdma_timings[] =
{
	{ 1260	, 0x00fff000 },
	{ 480	, 0x00618000 },
	{ 360	, 0x00492000 },
	{ 270	, 0x0038e000 },
	{ 240	, 0x0030c000 },
	{ 210	, 0x002cb000 },
	{ 180	, 0x00249000 },
	{ 150	, 0x00209000 },
	{ 120	, 0x00148000 },
	{ 0	, 0 },
};

static struct kauai_timing	kauai_udma_timings[] =
{
	{ 120	, 0x000070c0 },
	{ 90	, 0x00005d80 },
	{ 60	, 0x00004a60 },
	{ 45	, 0x00003a50 },
	{ 30	, 0x00002a30 },
	{ 20	, 0x00002921 },
	{ 0	, 0 },
};

static struct kauai_timing	shasta_pio_timings[] =
{
	{ 930	, 0x08000fff },
	{ 600	, 0x0A000c97 },
	{ 383	, 0x07000712 },
	{ 360	, 0x040003cd },
	{ 330	, 0x040003cd },
	{ 300	, 0x040003cd },
	{ 270	, 0x040003cd },
	{ 240	, 0x040003cd },
	{ 239	, 0x040003cd },
	{ 180	, 0x0400028b },
	{ 120	, 0x0400010a }
};

static struct kauai_timing	shasta_mdma_timings[] =
{
	{ 1260	, 0x00fff000 },
	{ 480	, 0x00820800 },
	{ 360	, 0x00820800 },
	{ 270	, 0x00820800 },
	{ 240	, 0x00820800 },
	{ 210	, 0x00820800 },
	{ 180	, 0x00820800 },
	{ 150	, 0x0028b000 },
	{ 120	, 0x001ca000 },
	{ 0	, 0 },
};

static struct kauai_timing	shasta_udma133_timings[] =
{
	{ 120   , 0x00035901, },
	{ 90    , 0x000348b1, },
	{ 60    , 0x00033881, },
	{ 45    , 0x00033861, },
	{ 30    , 0x00033841, },
	{ 20    , 0x00033031, },
	{ 15    , 0x00033021, },
	{ 0	, 0 },
};


static inline u32
kauai_lookup_timing(struct kauai_timing* table, int cycle_time)
{
	int i;
	
	for (i=0; table[i].cycle_time; i++)
		if (cycle_time > table[i+1].cycle_time)
			return table[i].timing_reg;
	return 0;
}

/* allow up to 256 DBDMA commands per xfer */
#define MAX_DCMDS		256

/* 
 * Wait 1s for disk to answer on IDE bus after a hard reset
 * of the device (via GPIO/FCR).
 * 
 * Some devices seem to "pollute" the bus even after dropping
 * the BSY bit (typically some combo drives slave on the UDMA
 * bus) after a hard reset. Since we hard reset all drives on
 * KeyLargo ATA66, we have to keep that delay around. I may end
 * up not hard resetting anymore on these and keep the delay only
 * for older interfaces instead (we have to reset when coming
 * from MacOS...) --BenH. 
 */
#define IDE_WAKEUP_DELAY	(1*HZ)

static void pmac_ide_setup_dma(pmac_ide_hwif_t *pmif, ide_hwif_t *hwif);
static int pmac_ide_build_dmatable(ide_drive_t *drive, struct request *rq);
static int pmac_ide_tune_chipset(ide_drive_t *drive, u8 speed);
static void pmac_ide_tuneproc(ide_drive_t *drive, u8 pio);
static void pmac_ide_selectproc(ide_drive_t *drive);
static void pmac_ide_kauai_selectproc(ide_drive_t *drive);

#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */

/*
 * N.B. this can't be an initfunc, because the media-bay task can
 * call ide_[un]register at any time.
 */
void
pmac_ide_init_hwif_ports(hw_regs_t *hw,
			      unsigned long data_port, unsigned long ctrl_port,
			      int *irq)
{
	int i, ix;

	if (data_port == 0)
		return;

	for (ix = 0; ix < MAX_HWIFS; ++ix)
		if (data_port == pmac_ide[ix].regbase)
			break;

	if (ix >= MAX_HWIFS) {
		/* Probably a PCI interface... */
		for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; ++i)
			hw->io_ports[i] = data_port + i - IDE_DATA_OFFSET;
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
		return;
	}

	for (i = 0; i < 8; ++i)
		hw->io_ports[i] = data_port + i * 0x10;
	hw->io_ports[8] = data_port + 0x160;

	if (irq != NULL)
		*irq = pmac_ide[ix].irq;

	hw->dev = &pmac_ide[ix].mdev->ofdev.dev;
}

#define PMAC_IDE_REG(x) ((void __iomem *)(IDE_DATA_REG+(x)))

/*
 * Apply the timings of the proper unit (master/slave) to the shared
 * timing register when selecting that unit. This version is for
 * ASICs with a single timing register
 */
static void
pmac_ide_selectproc(ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;

	if (pmif == NULL)
		return;

	if (drive->select.b.unit & 0x01)
		writel(pmif->timings[1], PMAC_IDE_REG(IDE_TIMING_CONFIG));
	else
		writel(pmif->timings[0], PMAC_IDE_REG(IDE_TIMING_CONFIG));
	(void)readl(PMAC_IDE_REG(IDE_TIMING_CONFIG));
}

/*
 * Apply the timings of the proper unit (master/slave) to the shared
 * timing register when selecting that unit. This version is for
 * ASICs with a dual timing register (Kauai)
 */
static void
pmac_ide_kauai_selectproc(ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;

	if (pmif == NULL)
		return;

	if (drive->select.b.unit & 0x01) {
		writel(pmif->timings[1], PMAC_IDE_REG(IDE_KAUAI_PIO_CONFIG));
		writel(pmif->timings[3], PMAC_IDE_REG(IDE_KAUAI_ULTRA_CONFIG));
	} else {
		writel(pmif->timings[0], PMAC_IDE_REG(IDE_KAUAI_PIO_CONFIG));
		writel(pmif->timings[2], PMAC_IDE_REG(IDE_KAUAI_ULTRA_CONFIG));
	}
	(void)readl(PMAC_IDE_REG(IDE_KAUAI_PIO_CONFIG));
}

/*
 * Force an update of controller timing values for a given drive
 */
static void
pmac_ide_do_update_timings(ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;

	if (pmif == NULL)
		return;

	if (pmif->kind == controller_sh_ata6 ||
	    pmif->kind == controller_un_ata6 ||
	    pmif->kind == controller_k2_ata6)
		pmac_ide_kauai_selectproc(drive);
	else
		pmac_ide_selectproc(drive);
}

static void
pmac_outbsync(ide_drive_t *drive, u8 value, unsigned long port)
{
	u32 tmp;
	
	writeb(value, (void __iomem *) port);
	tmp = readl(PMAC_IDE_REG(IDE_TIMING_CONFIG));
}

/*
 * Send the SET_FEATURE IDE command to the drive and update drive->id with
 * the new state. We currently don't use the generic routine as it used to
 * cause various trouble, especially with older mediabays.
 * This code is sometimes triggering a spurrious interrupt though, I need
 * to sort that out sooner or later and see if I can finally get the
 * common version to work properly in all cases
 */
static int
pmac_ide_do_setfeature(ide_drive_t *drive, u8 command)
{
	ide_hwif_t *hwif = HWIF(drive);
	int result = 1;
	
	disable_irq_nosync(hwif->irq);
	udelay(1);
	SELECT_DRIVE(drive);
	SELECT_MASK(drive, 0);
	udelay(1);
	/* Get rid of pending error state */
	(void) hwif->INB(IDE_STATUS_REG);
	/* Timeout bumped for some powerbooks */
	if (wait_for_ready(drive, 2000)) {
		/* Timeout bumped for some powerbooks */
		printk(KERN_ERR "%s: pmac_ide_do_setfeature disk not ready "
			"before SET_FEATURE!\n", drive->name);
		goto out;
	}
	udelay(10);
	hwif->OUTB(drive->ctl | 2, IDE_CONTROL_REG);
	hwif->OUTB(command, IDE_NSECTOR_REG);
	hwif->OUTB(SETFEATURES_XFER, IDE_FEATURE_REG);
	hwif->OUTBSYNC(drive, WIN_SETFEATURES, IDE_COMMAND_REG);
	udelay(1);
	/* Timeout bumped for some powerbooks */
	result = wait_for_ready(drive, 2000);
	hwif->OUTB(drive->ctl, IDE_CONTROL_REG);
	if (result)
		printk(KERN_ERR "%s: pmac_ide_do_setfeature disk not ready "
			"after SET_FEATURE !\n", drive->name);
out:
	SELECT_MASK(drive, 0);
	if (result == 0) {
		drive->id->dma_ultra &= ~0xFF00;
		drive->id->dma_mword &= ~0x0F00;
		drive->id->dma_1word &= ~0x0F00;
		switch(command) {
			case XFER_UDMA_7:
				drive->id->dma_ultra |= 0x8080; break;
			case XFER_UDMA_6:
				drive->id->dma_ultra |= 0x4040; break;
			case XFER_UDMA_5:
				drive->id->dma_ultra |= 0x2020; break;
			case XFER_UDMA_4:
				drive->id->dma_ultra |= 0x1010; break;
			case XFER_UDMA_3:
				drive->id->dma_ultra |= 0x0808; break;
			case XFER_UDMA_2:
				drive->id->dma_ultra |= 0x0404; break;
			case XFER_UDMA_1:
				drive->id->dma_ultra |= 0x0202; break;
			case XFER_UDMA_0:
				drive->id->dma_ultra |= 0x0101; break;
			case XFER_MW_DMA_2:
				drive->id->dma_mword |= 0x0404; break;
			case XFER_MW_DMA_1:
				drive->id->dma_mword |= 0x0202; break;
			case XFER_MW_DMA_0:
				drive->id->dma_mword |= 0x0101; break;
			case XFER_SW_DMA_2:
				drive->id->dma_1word |= 0x0404; break;
			case XFER_SW_DMA_1:
				drive->id->dma_1word |= 0x0202; break;
			case XFER_SW_DMA_0:
				drive->id->dma_1word |= 0x0101; break;
			default: break;
		}
	}
	enable_irq(hwif->irq);
	return result;
}

/*
 * Old tuning functions (called on hdparm -p), sets up drive PIO timings
 */
static void
pmac_ide_tuneproc(ide_drive_t *drive, u8 pio)
{
	ide_pio_data_t d;
	u32 *timings;
	unsigned accessTicks, recTicks;
	unsigned accessTime, recTime;
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	
	if (pmif == NULL)
		return;
		
	/* which drive is it ? */
	timings = &pmif->timings[drive->select.b.unit & 0x01];

	pio = ide_get_best_pio_mode(drive, pio, 4, &d);

	switch (pmif->kind) {
	case controller_sh_ata6: {
		/* 133Mhz cell */
		u32 tr = kauai_lookup_timing(shasta_pio_timings, d.cycle_time);
		if (tr == 0)
			return;
		*timings = ((*timings) & ~TR_133_PIOREG_PIO_MASK) | tr;
		break;
		}
	case controller_un_ata6:
	case controller_k2_ata6: {
		/* 100Mhz cell */
		u32 tr = kauai_lookup_timing(kauai_pio_timings, d.cycle_time);
		if (tr == 0)
			return;
		*timings = ((*timings) & ~TR_100_PIOREG_PIO_MASK) | tr;
		break;
		}
	case controller_kl_ata4:
		/* 66Mhz cell */
		recTime = d.cycle_time - ide_pio_timings[pio].active_time
				- ide_pio_timings[pio].setup_time;
		recTime = max(recTime, 150U);
		accessTime = ide_pio_timings[pio].active_time;
		accessTime = max(accessTime, 150U);
		accessTicks = SYSCLK_TICKS_66(accessTime);
		accessTicks = min(accessTicks, 0x1fU);
		recTicks = SYSCLK_TICKS_66(recTime);
		recTicks = min(recTicks, 0x1fU);
		*timings = ((*timings) & ~TR_66_PIO_MASK) |
				(accessTicks << TR_66_PIO_ACCESS_SHIFT) |
				(recTicks << TR_66_PIO_RECOVERY_SHIFT);
		break;
	default: {
		/* 33Mhz cell */
		int ebit = 0;
		recTime = d.cycle_time - ide_pio_timings[pio].active_time
				- ide_pio_timings[pio].setup_time;
		recTime = max(recTime, 150U);
		accessTime = ide_pio_timings[pio].active_time;
		accessTime = max(accessTime, 150U);
		accessTicks = SYSCLK_TICKS(accessTime);
		accessTicks = min(accessTicks, 0x1fU);
		accessTicks = max(accessTicks, 4U);
		recTicks = SYSCLK_TICKS(recTime);
		recTicks = min(recTicks, 0x1fU);
		recTicks = max(recTicks, 5U) - 4;
		if (recTicks > 9) {
			recTicks--; /* guess, but it's only for PIO0, so... */
			ebit = 1;
		}
		*timings = ((*timings) & ~TR_33_PIO_MASK) |
				(accessTicks << TR_33_PIO_ACCESS_SHIFT) |
				(recTicks << TR_33_PIO_RECOVERY_SHIFT);
		if (ebit)
			*timings |= TR_33_PIO_E;
		break;
		}
	}

#ifdef IDE_PMAC_DEBUG
	printk(KERN_ERR "%s: Set PIO timing for mode %d, reg: 0x%08x\n",
		drive->name, pio,  *timings);
#endif	

	if (drive->select.all == HWIF(drive)->INB(IDE_SELECT_REG))
		pmac_ide_do_update_timings(drive);
}

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC

/*
 * Calculate KeyLargo ATA/66 UDMA timings
 */
static int
set_timings_udma_ata4(u32 *timings, u8 speed)
{
	unsigned rdyToPauseTicks, wrDataSetupTicks, addrTicks;

	if (speed > XFER_UDMA_4)
		return 1;

	rdyToPauseTicks = SYSCLK_TICKS_66(kl66_udma_timings[speed & 0xf].rdy2pause);
	wrDataSetupTicks = SYSCLK_TICKS_66(kl66_udma_timings[speed & 0xf].wrDataSetup);
	addrTicks = SYSCLK_TICKS_66(kl66_udma_timings[speed & 0xf].addrSetup);

	*timings = ((*timings) & ~(TR_66_UDMA_MASK | TR_66_MDMA_MASK)) |
			(wrDataSetupTicks << TR_66_UDMA_WRDATASETUP_SHIFT) | 
			(rdyToPauseTicks << TR_66_UDMA_RDY2PAUS_SHIFT) |
			(addrTicks <<TR_66_UDMA_ADDRSETUP_SHIFT) |
			TR_66_UDMA_EN;
#ifdef IDE_PMAC_DEBUG
	printk(KERN_ERR "ide_pmac: Set UDMA timing for mode %d, reg: 0x%08x\n",
		speed & 0xf,  *timings);
#endif	

	return 0;
}

/*
 * Calculate Kauai ATA/100 UDMA timings
 */
static int
set_timings_udma_ata6(u32 *pio_timings, u32 *ultra_timings, u8 speed)
{
	struct ide_timing *t = ide_timing_find_mode(speed);
	u32 tr;

	if (speed > XFER_UDMA_5 || t == NULL)
		return 1;
	tr = kauai_lookup_timing(kauai_udma_timings, (int)t->udma);
	if (tr == 0)
		return 1;
	*ultra_timings = ((*ultra_timings) & ~TR_100_UDMAREG_UDMA_MASK) | tr;
	*ultra_timings = (*ultra_timings) | TR_100_UDMAREG_UDMA_EN;

	return 0;
}

/*
 * Calculate Shasta ATA/133 UDMA timings
 */
static int
set_timings_udma_shasta(u32 *pio_timings, u32 *ultra_timings, u8 speed)
{
	struct ide_timing *t = ide_timing_find_mode(speed);
	u32 tr;

	if (speed > XFER_UDMA_6 || t == NULL)
		return 1;
	tr = kauai_lookup_timing(shasta_udma133_timings, (int)t->udma);
	if (tr == 0)
		return 1;
	*ultra_timings = ((*ultra_timings) & ~TR_133_UDMAREG_UDMA_MASK) | tr;
	*ultra_timings = (*ultra_timings) | TR_133_UDMAREG_UDMA_EN;

	return 0;
}

/*
 * Calculate MDMA timings for all cells
 */
static int
set_timings_mdma(ide_drive_t *drive, int intf_type, u32 *timings, u32 *timings2,
			u8 speed, int drive_cycle_time)
{
	int cycleTime, accessTime = 0, recTime = 0;
	unsigned accessTicks, recTicks;
	struct mdma_timings_t* tm = NULL;
	int i;

	/* Get default cycle time for mode */
	switch(speed & 0xf) {
		case 0: cycleTime = 480; break;
		case 1: cycleTime = 150; break;
		case 2: cycleTime = 120; break;
		default:
			return 1;
	}
	/* Adjust for drive */
	if (drive_cycle_time && drive_cycle_time > cycleTime)
		cycleTime = drive_cycle_time;
	/* OHare limits according to some old Apple sources */	
	if ((intf_type == controller_ohare) && (cycleTime < 150))
		cycleTime = 150;
	/* Get the proper timing array for this controller */
	switch(intf_type) {
	        case controller_sh_ata6:
		case controller_un_ata6:
		case controller_k2_ata6:
			break;
		case controller_kl_ata4:
			tm = mdma_timings_66;
			break;
		case controller_kl_ata3:
			tm = mdma_timings_33k;
			break;
		default:
			tm = mdma_timings_33;
			break;
	}
	if (tm != NULL) {
		/* Lookup matching access & recovery times */
		i = -1;
		for (;;) {
			if (tm[i+1].cycleTime < cycleTime)
				break;
			i++;
		}
		if (i < 0)
			return 1;
		cycleTime = tm[i].cycleTime;
		accessTime = tm[i].accessTime;
		recTime = tm[i].recoveryTime;

#ifdef IDE_PMAC_DEBUG
		printk(KERN_ERR "%s: MDMA, cycleTime: %d, accessTime: %d, recTime: %d\n",
			drive->name, cycleTime, accessTime, recTime);
#endif
	}
	switch(intf_type) {
	case controller_sh_ata6: {
		/* 133Mhz cell */
		u32 tr = kauai_lookup_timing(shasta_mdma_timings, cycleTime);
		if (tr == 0)
			return 1;
		*timings = ((*timings) & ~TR_133_PIOREG_MDMA_MASK) | tr;
		*timings2 = (*timings2) & ~TR_133_UDMAREG_UDMA_EN;
		}
	case controller_un_ata6:
	case controller_k2_ata6: {
		/* 100Mhz cell */
		u32 tr = kauai_lookup_timing(kauai_mdma_timings, cycleTime);
		if (tr == 0)
			return 1;
		*timings = ((*timings) & ~TR_100_PIOREG_MDMA_MASK) | tr;
		*timings2 = (*timings2) & ~TR_100_UDMAREG_UDMA_EN;
		}
		break;
	case controller_kl_ata4:
		/* 66Mhz cell */
		accessTicks = SYSCLK_TICKS_66(accessTime);
		accessTicks = min(accessTicks, 0x1fU);
		accessTicks = max(accessTicks, 0x1U);
		recTicks = SYSCLK_TICKS_66(recTime);
		recTicks = min(recTicks, 0x1fU);
		recTicks = max(recTicks, 0x3U);
		/* Clear out mdma bits and disable udma */
		*timings = ((*timings) & ~(TR_66_MDMA_MASK | TR_66_UDMA_MASK)) |
			(accessTicks << TR_66_MDMA_ACCESS_SHIFT) |
			(recTicks << TR_66_MDMA_RECOVERY_SHIFT);
		break;
	case controller_kl_ata3:
		/* 33Mhz cell on KeyLargo */
		accessTicks = SYSCLK_TICKS(accessTime);
		accessTicks = max(accessTicks, 1U);
		accessTicks = min(accessTicks, 0x1fU);
		accessTime = accessTicks * IDE_SYSCLK_NS;
		recTicks = SYSCLK_TICKS(recTime);
		recTicks = max(recTicks, 1U);
		recTicks = min(recTicks, 0x1fU);
		*timings = ((*timings) & ~TR_33_MDMA_MASK) |
				(accessTicks << TR_33_MDMA_ACCESS_SHIFT) |
				(recTicks << TR_33_MDMA_RECOVERY_SHIFT);
		break;
	default: {
		/* 33Mhz cell on others */
		int halfTick = 0;
		int origAccessTime = accessTime;
		int origRecTime = recTime;
		
		accessTicks = SYSCLK_TICKS(accessTime);
		accessTicks = max(accessTicks, 1U);
		accessTicks = min(accessTicks, 0x1fU);
		accessTime = accessTicks * IDE_SYSCLK_NS;
		recTicks = SYSCLK_TICKS(recTime);
		recTicks = max(recTicks, 2U) - 1;
		recTicks = min(recTicks, 0x1fU);
		recTime = (recTicks + 1) * IDE_SYSCLK_NS;
		if ((accessTicks > 1) &&
		    ((accessTime - IDE_SYSCLK_NS/2) >= origAccessTime) &&
		    ((recTime - IDE_SYSCLK_NS/2) >= origRecTime)) {
            		halfTick = 1;
			accessTicks--;
		}
		*timings = ((*timings) & ~TR_33_MDMA_MASK) |
				(accessTicks << TR_33_MDMA_ACCESS_SHIFT) |
				(recTicks << TR_33_MDMA_RECOVERY_SHIFT);
		if (halfTick)
			*timings |= TR_33_MDMA_HALFTICK;
		}
	}
#ifdef IDE_PMAC_DEBUG
	printk(KERN_ERR "%s: Set MDMA timing for mode %d, reg: 0x%08x\n",
		drive->name, speed & 0xf,  *timings);
#endif	
	return 0;
}
#endif /* #ifdef CONFIG_BLK_DEV_IDEDMA_PMAC */

/* 
 * Speedproc. This function is called by the core to set any of the standard
 * timing (PIO, MDMA or UDMA) to both the drive and the controller.
 * You may notice we don't use this function on normal "dma check" operation,
 * our dedicated function is more precise as it uses the drive provided
 * cycle time value. We should probably fix this one to deal with that too...
 */
static int
pmac_ide_tune_chipset (ide_drive_t *drive, byte speed)
{
	int unit = (drive->select.b.unit & 0x01);
	int ret = 0;
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	u32 *timings, *timings2;

	if (pmif == NULL)
		return 1;
		
	timings = &pmif->timings[unit];
	timings2 = &pmif->timings[unit+2];
	
	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
		case XFER_UDMA_6:
		        if (pmif->kind != controller_sh_ata6)
				return 1;
		case XFER_UDMA_5:
			if (pmif->kind != controller_un_ata6 &&
			    pmif->kind != controller_k2_ata6 &&
			    pmif->kind != controller_sh_ata6)
				return 1;
		case XFER_UDMA_4:
		case XFER_UDMA_3:
			if (drive->hwif->cbl != ATA_CBL_PATA80)
				return 1;
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			if (pmif->kind == controller_kl_ata4)
				ret = set_timings_udma_ata4(timings, speed);
			else if (pmif->kind == controller_un_ata6
				 || pmif->kind == controller_k2_ata6)
				ret = set_timings_udma_ata6(timings, timings2, speed);
			else if (pmif->kind == controller_sh_ata6)
				ret = set_timings_udma_shasta(timings, timings2, speed);
			else
				ret = 1;		
			break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
			ret = set_timings_mdma(drive, pmif->kind, timings, timings2, speed, 0);
			break;
		case XFER_SW_DMA_2:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
			return 1;
#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			pmac_ide_tuneproc(drive, speed & 0x07);
			break;
		default:
			ret = 1;
	}
	if (ret)
		return ret;

	ret = pmac_ide_do_setfeature(drive, speed);
	if (ret)
		return ret;
		
	pmac_ide_do_update_timings(drive);	
	drive->current_speed = speed;

	return 0;
}

/*
 * Blast some well known "safe" values to the timing registers at init or
 * wakeup from sleep time, before we do real calculation
 */
static void
sanitize_timings(pmac_ide_hwif_t *pmif)
{
	unsigned int value, value2 = 0;
	
	switch(pmif->kind) {
		case controller_sh_ata6:
			value = 0x0a820c97;
			value2 = 0x00033031;
			break;
		case controller_un_ata6:
		case controller_k2_ata6:
			value = 0x08618a92;
			value2 = 0x00002921;
			break;
		case controller_kl_ata4:
			value = 0x0008438c;
			break;
		case controller_kl_ata3:
			value = 0x00084526;
			break;
		case controller_heathrow:
		case controller_ohare:
		default:
			value = 0x00074526;
			break;
	}
	pmif->timings[0] = pmif->timings[1] = value;
	pmif->timings[2] = pmif->timings[3] = value2;
}

unsigned long
pmac_ide_get_base(int index)
{
	return pmac_ide[index].regbase;
}

int
pmac_ide_check_base(unsigned long base)
{
	int ix;
	
 	for (ix = 0; ix < MAX_HWIFS; ++ix)
		if (base == pmac_ide[ix].regbase)
			return ix;
	return -1;
}

int
pmac_ide_get_irq(unsigned long base)
{
	int ix;

	for (ix = 0; ix < MAX_HWIFS; ++ix)
		if (base == pmac_ide[ix].regbase)
			return pmac_ide[ix].irq;
	return 0;
}

static int ide_majors[] = { 3, 22, 33, 34, 56, 57 };

dev_t __init
pmac_find_ide_boot(char *bootdevice, int n)
{
	int i;
	
	/*
	 * Look through the list of IDE interfaces for this one.
	 */
	for (i = 0; i < pmac_ide_count; ++i) {
		char *name;
		if (!pmac_ide[i].node || !pmac_ide[i].node->full_name)
			continue;
		name = pmac_ide[i].node->full_name;
		if (memcmp(name, bootdevice, n) == 0 && name[n] == 0) {
			/* XXX should cope with the 2nd drive as well... */
			return MKDEV(ide_majors[i], 0);
		}
	}

	return 0;
}

/* Suspend call back, should be called after the child devices
 * have actually been suspended
 */
static int
pmac_ide_do_suspend(ide_hwif_t *hwif)
{
	pmac_ide_hwif_t *pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	
	/* We clear the timings */
	pmif->timings[0] = 0;
	pmif->timings[1] = 0;
	
	disable_irq(pmif->irq);

	/* The media bay will handle itself just fine */
	if (pmif->mediabay)
		return 0;
	
	/* Kauai has bus control FCRs directly here */
	if (pmif->kauai_fcr) {
		u32 fcr = readl(pmif->kauai_fcr);
		fcr &= ~(KAUAI_FCR_UATA_RESET_N | KAUAI_FCR_UATA_ENABLE);
		writel(fcr, pmif->kauai_fcr);
	}

	/* Disable the bus on older machines and the cell on kauai */
	ppc_md.feature_call(PMAC_FTR_IDE_ENABLE, pmif->node, pmif->aapl_bus_id,
			    0);

	return 0;
}

/* Resume call back, should be called before the child devices
 * are resumed
 */
static int
pmac_ide_do_resume(ide_hwif_t *hwif)
{
	pmac_ide_hwif_t *pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	
	/* Hard reset & re-enable controller (do we really need to reset ? -BenH) */
	if (!pmif->mediabay) {
		ppc_md.feature_call(PMAC_FTR_IDE_RESET, pmif->node, pmif->aapl_bus_id, 1);
		ppc_md.feature_call(PMAC_FTR_IDE_ENABLE, pmif->node, pmif->aapl_bus_id, 1);
		msleep(10);
		ppc_md.feature_call(PMAC_FTR_IDE_RESET, pmif->node, pmif->aapl_bus_id, 0);

		/* Kauai has it different */
		if (pmif->kauai_fcr) {
			u32 fcr = readl(pmif->kauai_fcr);
			fcr |= KAUAI_FCR_UATA_RESET_N | KAUAI_FCR_UATA_ENABLE;
			writel(fcr, pmif->kauai_fcr);
		}

		msleep(jiffies_to_msecs(IDE_WAKEUP_DELAY));
	}

	/* Sanitize drive timings */
	sanitize_timings(pmif);

	enable_irq(pmif->irq);

	return 0;
}

/*
 * Setup, register & probe an IDE channel driven by this driver, this is
 * called by one of the 2 probe functions (macio or PCI). Note that a channel
 * that ends up beeing free of any device is not kept around by this driver
 * (it is kept in 2.4). This introduce an interface numbering change on some
 * rare machines unfortunately, but it's better this way.
 */
static int
pmac_ide_setup_device(pmac_ide_hwif_t *pmif, ide_hwif_t *hwif)
{
	struct device_node *np = pmif->node;
	const int *bidp;

	pmif->cable_80 = 0;
	pmif->broken_dma = pmif->broken_dma_warn = 0;
	if (of_device_is_compatible(np, "shasta-ata"))
		pmif->kind = controller_sh_ata6;
	else if (of_device_is_compatible(np, "kauai-ata"))
		pmif->kind = controller_un_ata6;
	else if (of_device_is_compatible(np, "K2-UATA"))
		pmif->kind = controller_k2_ata6;
	else if (of_device_is_compatible(np, "keylargo-ata")) {
		if (strcmp(np->name, "ata-4") == 0)
			pmif->kind = controller_kl_ata4;
		else
			pmif->kind = controller_kl_ata3;
	} else if (of_device_is_compatible(np, "heathrow-ata"))
		pmif->kind = controller_heathrow;
	else {
		pmif->kind = controller_ohare;
		pmif->broken_dma = 1;
	}

	bidp = of_get_property(np, "AAPL,bus-id", NULL);
	pmif->aapl_bus_id =  bidp ? *bidp : 0;

	/* Get cable type from device-tree */
	if (pmif->kind == controller_kl_ata4 || pmif->kind == controller_un_ata6
	    || pmif->kind == controller_k2_ata6
	    || pmif->kind == controller_sh_ata6) {
		const char* cable = of_get_property(np, "cable-type", NULL);
		if (cable && !strncmp(cable, "80-", 3))
			pmif->cable_80 = 1;
	}
	/* G5's seem to have incorrect cable type in device-tree. Let's assume
	 * they have a 80 conductor cable, this seem to be always the case unless
	 * the user mucked around
	 */
	if (of_device_is_compatible(np, "K2-UATA") ||
	    of_device_is_compatible(np, "shasta-ata"))
		pmif->cable_80 = 1;

	/* On Kauai-type controllers, we make sure the FCR is correct */
	if (pmif->kauai_fcr)
		writel(KAUAI_FCR_UATA_MAGIC |
		       KAUAI_FCR_UATA_RESET_N |
		       KAUAI_FCR_UATA_ENABLE, pmif->kauai_fcr);

	pmif->mediabay = 0;
	
	/* Make sure we have sane timings */
	sanitize_timings(pmif);

#ifndef CONFIG_PPC64
	/* XXX FIXME: Media bay stuff need re-organizing */
	if (np->parent && np->parent->name
	    && strcasecmp(np->parent->name, "media-bay") == 0) {
#ifdef CONFIG_PMAC_MEDIABAY
		media_bay_set_ide_infos(np->parent, pmif->regbase, pmif->irq, hwif->index);
#endif /* CONFIG_PMAC_MEDIABAY */
		pmif->mediabay = 1;
		if (!bidp)
			pmif->aapl_bus_id = 1;
	} else if (pmif->kind == controller_ohare) {
		/* The code below is having trouble on some ohare machines
		 * (timing related ?). Until I can put my hand on one of these
		 * units, I keep the old way
		 */
		ppc_md.feature_call(PMAC_FTR_IDE_ENABLE, np, 0, 1);
	} else
#endif
	{
 		/* This is necessary to enable IDE when net-booting */
		ppc_md.feature_call(PMAC_FTR_IDE_RESET, np, pmif->aapl_bus_id, 1);
		ppc_md.feature_call(PMAC_FTR_IDE_ENABLE, np, pmif->aapl_bus_id, 1);
		msleep(10);
		ppc_md.feature_call(PMAC_FTR_IDE_RESET, np, pmif->aapl_bus_id, 0);
		msleep(jiffies_to_msecs(IDE_WAKEUP_DELAY));
	}

	/* Setup MMIO ops */
	default_hwif_mmiops(hwif);
       	hwif->OUTBSYNC = pmac_outbsync;

	/* Tell common code _not_ to mess with resources */
	hwif->mmio = 1;
	hwif->hwif_data = pmif;
	pmac_ide_init_hwif_ports(&hwif->hw, pmif->regbase, 0, &hwif->irq);
	memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
	hwif->chipset = ide_pmac;
	hwif->noprobe = !hwif->io_ports[IDE_DATA_OFFSET] || pmif->mediabay;
	hwif->hold = pmif->mediabay;
	hwif->cbl = pmif->cable_80 ? ATA_CBL_PATA80 : ATA_CBL_PATA40;
	hwif->drives[0].unmask = 1;
	hwif->drives[1].unmask = 1;
	hwif->tuneproc = pmac_ide_tuneproc;
	if (pmif->kind == controller_un_ata6
	    || pmif->kind == controller_k2_ata6
	    || pmif->kind == controller_sh_ata6)
		hwif->selectproc = pmac_ide_kauai_selectproc;
	else
		hwif->selectproc = pmac_ide_selectproc;
	hwif->speedproc = pmac_ide_tune_chipset;

	printk(KERN_INFO "ide%d: Found Apple %s controller, bus ID %d%s, irq %d\n",
	       hwif->index, model_name[pmif->kind], pmif->aapl_bus_id,
	       pmif->mediabay ? " (mediabay)" : "", hwif->irq);
			
#ifdef CONFIG_PMAC_MEDIABAY
	if (pmif->mediabay && check_media_bay_by_base(pmif->regbase, MB_CD) == 0)
		hwif->noprobe = 0;
#endif /* CONFIG_PMAC_MEDIABAY */

	hwif->sg_max_nents = MAX_DCMDS;

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
	/* has a DBDMA controller channel */
	if (pmif->dma_regs)
		pmac_ide_setup_dma(pmif, hwif);
#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */

	/* We probe the hwif now */
	probe_hwif_init(hwif);

	ide_proc_register_port(hwif);

	return 0;
}

/*
 * Attach to a macio probed interface
 */
static int __devinit
pmac_ide_macio_attach(struct macio_dev *mdev, const struct of_device_id *match)
{
	void __iomem *base;
	unsigned long regbase;
	int irq;
	ide_hwif_t *hwif;
	pmac_ide_hwif_t *pmif;
	int i, rc;

	i = 0;
	while (i < MAX_HWIFS && (ide_hwifs[i].io_ports[IDE_DATA_OFFSET] != 0
	    || pmac_ide[i].node != NULL))
		++i;
	if (i >= MAX_HWIFS) {
		printk(KERN_ERR "ide-pmac: MacIO interface attach with no slot\n");
		printk(KERN_ERR "          %s\n", mdev->ofdev.node->full_name);
		return -ENODEV;
	}

	pmif = &pmac_ide[i];
	hwif = &ide_hwifs[i];

	if (macio_resource_count(mdev) == 0) {
		printk(KERN_WARNING "ide%d: no address for %s\n",
		       i, mdev->ofdev.node->full_name);
		return -ENXIO;
	}

	/* Request memory resource for IO ports */
	if (macio_request_resource(mdev, 0, "ide-pmac (ports)")) {
		printk(KERN_ERR "ide%d: can't request mmio resource !\n", i);
		return -EBUSY;
	}
			
	/* XXX This is bogus. Should be fixed in the registry by checking
	 * the kind of host interrupt controller, a bit like gatwick
	 * fixes in irq.c. That works well enough for the single case
	 * where that happens though...
	 */
	if (macio_irq_count(mdev) == 0) {
		printk(KERN_WARNING "ide%d: no intrs for device %s, using 13\n",
			i, mdev->ofdev.node->full_name);
		irq = irq_create_mapping(NULL, 13);
	} else
		irq = macio_irq(mdev, 0);

	base = ioremap(macio_resource_start(mdev, 0), 0x400);
	regbase = (unsigned long) base;

	hwif->pci_dev = mdev->bus->pdev;
	hwif->gendev.parent = &mdev->ofdev.dev;

	pmif->mdev = mdev;
	pmif->node = mdev->ofdev.node;
	pmif->regbase = regbase;
	pmif->irq = irq;
	pmif->kauai_fcr = NULL;
#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
	if (macio_resource_count(mdev) >= 2) {
		if (macio_request_resource(mdev, 1, "ide-pmac (dma)"))
			printk(KERN_WARNING "ide%d: can't request DMA resource !\n", i);
		else
			pmif->dma_regs = ioremap(macio_resource_start(mdev, 1), 0x1000);
	} else
		pmif->dma_regs = NULL;
#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */
	dev_set_drvdata(&mdev->ofdev.dev, hwif);

	rc = pmac_ide_setup_device(pmif, hwif);
	if (rc != 0) {
		/* The inteface is released to the common IDE layer */
		dev_set_drvdata(&mdev->ofdev.dev, NULL);
		iounmap(base);
		if (pmif->dma_regs)
			iounmap(pmif->dma_regs);
		memset(pmif, 0, sizeof(*pmif));
		macio_release_resource(mdev, 0);
		if (pmif->dma_regs)
			macio_release_resource(mdev, 1);
	}

	return rc;
}

static int
pmac_ide_macio_suspend(struct macio_dev *mdev, pm_message_t mesg)
{
	ide_hwif_t	*hwif = (ide_hwif_t *)dev_get_drvdata(&mdev->ofdev.dev);
	int		rc = 0;

	if (mesg.event != mdev->ofdev.dev.power.power_state.event
			&& mesg.event == PM_EVENT_SUSPEND) {
		rc = pmac_ide_do_suspend(hwif);
		if (rc == 0)
			mdev->ofdev.dev.power.power_state = mesg;
	}

	return rc;
}

static int
pmac_ide_macio_resume(struct macio_dev *mdev)
{
	ide_hwif_t	*hwif = (ide_hwif_t *)dev_get_drvdata(&mdev->ofdev.dev);
	int		rc = 0;
	
	if (mdev->ofdev.dev.power.power_state.event != PM_EVENT_ON) {
		rc = pmac_ide_do_resume(hwif);
		if (rc == 0)
			mdev->ofdev.dev.power.power_state = PMSG_ON;
	}

	return rc;
}

/*
 * Attach to a PCI probed interface
 */
static int __devinit
pmac_ide_pci_attach(struct pci_dev *pdev, const struct pci_device_id *id)
{
	ide_hwif_t *hwif;
	struct device_node *np;
	pmac_ide_hwif_t *pmif;
	void __iomem *base;
	unsigned long rbase, rlen;
	int i, rc;

	np = pci_device_to_OF_node(pdev);
	if (np == NULL) {
		printk(KERN_ERR "ide-pmac: cannot find MacIO node for Kauai ATA interface\n");
		return -ENODEV;
	}
	i = 0;
	while (i < MAX_HWIFS && (ide_hwifs[i].io_ports[IDE_DATA_OFFSET] != 0
	    || pmac_ide[i].node != NULL))
		++i;
	if (i >= MAX_HWIFS) {
		printk(KERN_ERR "ide-pmac: PCI interface attach with no slot\n");
		printk(KERN_ERR "          %s\n", np->full_name);
		return -ENODEV;
	}

	pmif = &pmac_ide[i];
	hwif = &ide_hwifs[i];

	if (pci_enable_device(pdev)) {
		printk(KERN_WARNING "ide%i: Can't enable PCI device for %s\n",
			i, np->full_name);
		return -ENXIO;
	}
	pci_set_master(pdev);
			
	if (pci_request_regions(pdev, "Kauai ATA")) {
		printk(KERN_ERR "ide%d: Cannot obtain PCI resources for %s\n",
			i, np->full_name);
		return -ENXIO;
	}

	hwif->pci_dev = pdev;
	hwif->gendev.parent = &pdev->dev;
	pmif->mdev = NULL;
	pmif->node = np;

	rbase = pci_resource_start(pdev, 0);
	rlen = pci_resource_len(pdev, 0);

	base = ioremap(rbase, rlen);
	pmif->regbase = (unsigned long) base + 0x2000;
#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
	pmif->dma_regs = base + 0x1000;
#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */
	pmif->kauai_fcr = base;
	pmif->irq = pdev->irq;

	pci_set_drvdata(pdev, hwif);

	rc = pmac_ide_setup_device(pmif, hwif);
	if (rc != 0) {
		/* The inteface is released to the common IDE layer */
		pci_set_drvdata(pdev, NULL);
		iounmap(base);
		memset(pmif, 0, sizeof(*pmif));
		pci_release_regions(pdev);
	}

	return rc;
}

static int
pmac_ide_pci_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	ide_hwif_t	*hwif = (ide_hwif_t *)pci_get_drvdata(pdev);
	int		rc = 0;
	
	if (mesg.event != pdev->dev.power.power_state.event
			&& mesg.event == PM_EVENT_SUSPEND) {
		rc = pmac_ide_do_suspend(hwif);
		if (rc == 0)
			pdev->dev.power.power_state = mesg;
	}

	return rc;
}

static int
pmac_ide_pci_resume(struct pci_dev *pdev)
{
	ide_hwif_t	*hwif = (ide_hwif_t *)pci_get_drvdata(pdev);
	int		rc = 0;
	
	if (pdev->dev.power.power_state.event != PM_EVENT_ON) {
		rc = pmac_ide_do_resume(hwif);
		if (rc == 0)
			pdev->dev.power.power_state = PMSG_ON;
	}

	return rc;
}

static struct of_device_id pmac_ide_macio_match[] = 
{
	{
	.name 		= "IDE",
	},
	{
	.name 		= "ATA",
	},
	{
	.type		= "ide",
	},
	{
	.type		= "ata",
	},
	{},
};

static struct macio_driver pmac_ide_macio_driver = 
{
	.name 		= "ide-pmac",
	.match_table	= pmac_ide_macio_match,
	.probe		= pmac_ide_macio_attach,
	.suspend	= pmac_ide_macio_suspend,
	.resume		= pmac_ide_macio_resume,
};

static struct pci_device_id pmac_ide_pci_match[] = {
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_UNI_N_ATA,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_IPID_ATA100,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_K2_ATA100,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_SH_ATA,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_IPID2_ATA,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
};

static struct pci_driver pmac_ide_pci_driver = {
	.name		= "ide-pmac",
	.id_table	= pmac_ide_pci_match,
	.probe		= pmac_ide_pci_attach,
	.suspend	= pmac_ide_pci_suspend,
	.resume		= pmac_ide_pci_resume,
};
MODULE_DEVICE_TABLE(pci, pmac_ide_pci_match);

int __init pmac_ide_probe(void)
{
	int error;

	if (!machine_is(powermac))
		return -ENODEV;

#ifdef CONFIG_BLK_DEV_IDE_PMAC_ATA100FIRST
	error = pci_register_driver(&pmac_ide_pci_driver);
	if (error)
		goto out;
	error = macio_register_driver(&pmac_ide_macio_driver);
	if (error) {
		pci_unregister_driver(&pmac_ide_pci_driver);
		goto out;
	}
#else
	error = macio_register_driver(&pmac_ide_macio_driver);
	if (error)
		goto out;
	error = pci_register_driver(&pmac_ide_pci_driver);
	if (error) {
		macio_unregister_driver(&pmac_ide_macio_driver);
		goto out;
	}
#endif
out:
	return error;
}

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC

/*
 * pmac_ide_build_dmatable builds the DBDMA command list
 * for a transfer and sets the DBDMA channel to point to it.
 */
static int
pmac_ide_build_dmatable(ide_drive_t *drive, struct request *rq)
{
	struct dbdma_cmd *table;
	int i, count = 0;
	ide_hwif_t *hwif = HWIF(drive);
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	volatile struct dbdma_regs __iomem *dma = pmif->dma_regs;
	struct scatterlist *sg;
	int wr = (rq_data_dir(rq) == WRITE);

	/* DMA table is already aligned */
	table = (struct dbdma_cmd *) pmif->dma_table_cpu;

	/* Make sure DMA controller is stopped (necessary ?) */
	writel((RUN|PAUSE|FLUSH|WAKE|DEAD) << 16, &dma->control);
	while (readl(&dma->status) & RUN)
		udelay(1);

	hwif->sg_nents = i = ide_build_sglist(drive, rq);

	if (!i)
		return 0;

	/* Build DBDMA commands list */
	sg = hwif->sg_table;
	while (i && sg_dma_len(sg)) {
		u32 cur_addr;
		u32 cur_len;

		cur_addr = sg_dma_address(sg);
		cur_len = sg_dma_len(sg);

		if (pmif->broken_dma && cur_addr & (L1_CACHE_BYTES - 1)) {
			if (pmif->broken_dma_warn == 0) {
				printk(KERN_WARNING "%s: DMA on non aligned address,"
				       "switching to PIO on Ohare chipset\n", drive->name);
				pmif->broken_dma_warn = 1;
			}
			goto use_pio_instead;
		}
		while (cur_len) {
			unsigned int tc = (cur_len < 0xfe00)? cur_len: 0xfe00;

			if (count++ >= MAX_DCMDS) {
				printk(KERN_WARNING "%s: DMA table too small\n",
				       drive->name);
				goto use_pio_instead;
			}
			st_le16(&table->command, wr? OUTPUT_MORE: INPUT_MORE);
			st_le16(&table->req_count, tc);
			st_le32(&table->phy_addr, cur_addr);
			table->cmd_dep = 0;
			table->xfer_status = 0;
			table->res_count = 0;
			cur_addr += tc;
			cur_len -= tc;
			++table;
		}
		sg++;
		i--;
	}

	/* convert the last command to an input/output last command */
	if (count) {
		st_le16(&table[-1].command, wr? OUTPUT_LAST: INPUT_LAST);
		/* add the stop command to the end of the list */
		memset(table, 0, sizeof(struct dbdma_cmd));
		st_le16(&table->command, DBDMA_STOP);
		mb();
		writel(hwif->dmatable_dma, &dma->cmdptr);
		return 1;
	}

	printk(KERN_DEBUG "%s: empty DMA table?\n", drive->name);
 use_pio_instead:
	pci_unmap_sg(hwif->pci_dev,
		     hwif->sg_table,
		     hwif->sg_nents,
		     hwif->sg_dma_direction);
	return 0; /* revert to PIO for this request */
}

/* Teardown mappings after DMA has completed.  */
static void
pmac_ide_destroy_dmatable (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct pci_dev *dev = HWIF(drive)->pci_dev;
	struct scatterlist *sg = hwif->sg_table;
	int nents = hwif->sg_nents;

	if (nents) {
		pci_unmap_sg(dev, sg, nents, hwif->sg_dma_direction);
		hwif->sg_nents = 0;
	}
}

/*
 * Pick up best MDMA timing for the drive and apply it
 */
static int
pmac_ide_mdma_enable(ide_drive_t *drive, u16 mode)
{
	ide_hwif_t *hwif = HWIF(drive);
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	int drive_cycle_time;
	struct hd_driveid *id = drive->id;
	u32 *timings, *timings2;
	u32 timing_local[2];
	int ret;

	/* which drive is it ? */
	timings = &pmif->timings[drive->select.b.unit & 0x01];
	timings2 = &pmif->timings[(drive->select.b.unit & 0x01) + 2];

	/* Check if drive provide explicit cycle time */
	if ((id->field_valid & 2) && (id->eide_dma_time))
		drive_cycle_time = id->eide_dma_time;
	else
		drive_cycle_time = 0;

	/* Copy timings to local image */
	timing_local[0] = *timings;
	timing_local[1] = *timings2;

	/* Calculate controller timings */
	ret = set_timings_mdma(	drive, pmif->kind,
				&timing_local[0],
				&timing_local[1],
				mode,
				drive_cycle_time);
	if (ret)
		return 0;

	/* Set feature on drive */
    	printk(KERN_INFO "%s: Enabling MultiWord DMA %d\n", drive->name, mode & 0xf);
	ret = pmac_ide_do_setfeature(drive, mode);
	if (ret) {
	    	printk(KERN_WARNING "%s: Failed !\n", drive->name);
	    	return 0;
	}

	/* Apply timings to controller */
	*timings = timing_local[0];
	*timings2 = timing_local[1];
	
	/* Set speed info in drive */
	drive->current_speed = mode;	
	if (!drive->init_speed)
		drive->init_speed = mode;

	return 1;
}

/*
 * Pick up best UDMA timing for the drive and apply it
 */
static int
pmac_ide_udma_enable(ide_drive_t *drive, u16 mode)
{
	ide_hwif_t *hwif = HWIF(drive);
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	u32 *timings, *timings2;
	u32 timing_local[2];
	int ret;
		
	/* which drive is it ? */
	timings = &pmif->timings[drive->select.b.unit & 0x01];
	timings2 = &pmif->timings[(drive->select.b.unit & 0x01) + 2];

	/* Copy timings to local image */
	timing_local[0] = *timings;
	timing_local[1] = *timings2;
	
	/* Calculate timings for interface */
	if (pmif->kind == controller_un_ata6
	    || pmif->kind == controller_k2_ata6)
		ret = set_timings_udma_ata6(	&timing_local[0],
						&timing_local[1],
						mode);
	else if (pmif->kind == controller_sh_ata6)
		ret = set_timings_udma_shasta(	&timing_local[0],
						&timing_local[1],
						mode);
	else
		ret = set_timings_udma_ata4(&timing_local[0], mode);
	if (ret)
		return 0;
		
	/* Set feature on drive */
    	printk(KERN_INFO "%s: Enabling Ultra DMA %d\n", drive->name, mode & 0x0f);
	ret = pmac_ide_do_setfeature(drive, mode);
	if (ret) {
		printk(KERN_WARNING "%s: Failed !\n", drive->name);
		return 0;
	}

	/* Apply timings to controller */
	*timings = timing_local[0];
	*timings2 = timing_local[1];

	/* Set speed info in drive */
	drive->current_speed = mode;	
	if (!drive->init_speed)
		drive->init_speed = mode;

	return 1;
}

/*
 * Check what is the best DMA timing setting for the drive and
 * call appropriate functions to apply it.
 */
static int
pmac_ide_dma_check(ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_hwif_t *hwif = HWIF(drive);
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	int enable = 1;
	int map;
	drive->using_dma = 0;
	
	if (drive->media == ide_floppy)
		enable = 0;
	if (((id->capability & 1) == 0) && !__ide_dma_good_drive(drive))
		enable = 0;
	if (__ide_dma_bad_drive(drive))
		enable = 0;

	if (enable) {
		u8 mode = ide_max_dma_mode(drive);

		if (mode >= XFER_UDMA_0)
			drive->using_dma = pmac_ide_udma_enable(drive, mode);
		else if (mode >= XFER_MW_DMA_0)
			drive->using_dma = pmac_ide_mdma_enable(drive, mode);
		hwif->OUTB(0, IDE_CONTROL_REG);
		/* Apply settings to controller */
		pmac_ide_do_update_timings(drive);
	}
	return 0;
}

/*
 * Prepare a DMA transfer. We build the DMA table, adjust the timings for
 * a read on KeyLargo ATA/66 and mark us as waiting for DMA completion
 */
static int
pmac_ide_dma_setup(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	struct request *rq = HWGROUP(drive)->rq;
	u8 unit = (drive->select.b.unit & 0x01);
	u8 ata4;

	if (pmif == NULL)
		return 1;
	ata4 = (pmif->kind == controller_kl_ata4);	

	if (!pmac_ide_build_dmatable(drive, rq)) {
		ide_map_sg(drive, rq);
		return 1;
	}

	/* Apple adds 60ns to wrDataSetup on reads */
	if (ata4 && (pmif->timings[unit] & TR_66_UDMA_EN)) {
		writel(pmif->timings[unit] + (!rq_data_dir(rq) ? 0x00800000UL : 0),
			PMAC_IDE_REG(IDE_TIMING_CONFIG));
		(void)readl(PMAC_IDE_REG(IDE_TIMING_CONFIG));
	}

	drive->waiting_for_dma = 1;

	return 0;
}

static void
pmac_ide_dma_exec_cmd(ide_drive_t *drive, u8 command)
{
	/* issue cmd to drive */
	ide_execute_command(drive, command, &ide_dma_intr, 2*WAIT_CMD, NULL);
}

/*
 * Kick the DMA controller into life after the DMA command has been issued
 * to the drive.
 */
static void
pmac_ide_dma_start(ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	volatile struct dbdma_regs __iomem *dma;

	dma = pmif->dma_regs;

	writel((RUN << 16) | RUN, &dma->control);
	/* Make sure it gets to the controller right now */
	(void)readl(&dma->control);
}

/*
 * After a DMA transfer, make sure the controller is stopped
 */
static int
pmac_ide_dma_end (ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	volatile struct dbdma_regs __iomem *dma;
	u32 dstat;
	
	if (pmif == NULL)
		return 0;
	dma = pmif->dma_regs;

	drive->waiting_for_dma = 0;
	dstat = readl(&dma->status);
	writel(((RUN|WAKE|DEAD) << 16), &dma->control);
	pmac_ide_destroy_dmatable(drive);
	/* verify good dma status. we don't check for ACTIVE beeing 0. We should...
	 * in theory, but with ATAPI decices doing buffer underruns, that would
	 * cause us to disable DMA, which isn't what we want
	 */
	return (dstat & (RUN|DEAD)) != RUN;
}

/*
 * Check out that the interrupt we got was for us. We can't always know this
 * for sure with those Apple interfaces (well, we could on the recent ones but
 * that's not implemented yet), on the other hand, we don't have shared interrupts
 * so it's not really a problem
 */
static int
pmac_ide_dma_test_irq (ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	volatile struct dbdma_regs __iomem *dma;
	unsigned long status, timeout;

	if (pmif == NULL)
		return 0;
	dma = pmif->dma_regs;

	/* We have to things to deal with here:
	 * 
	 * - The dbdma won't stop if the command was started
	 * but completed with an error without transferring all
	 * datas. This happens when bad blocks are met during
	 * a multi-block transfer.
	 * 
	 * - The dbdma fifo hasn't yet finished flushing to
	 * to system memory when the disk interrupt occurs.
	 * 
	 */

	/* If ACTIVE is cleared, the STOP command have passed and
	 * transfer is complete.
	 */
	status = readl(&dma->status);
	if (!(status & ACTIVE))
		return 1;
	if (!drive->waiting_for_dma)
		printk(KERN_WARNING "ide%d, ide_dma_test_irq \
			called while not waiting\n", HWIF(drive)->index);

	/* If dbdma didn't execute the STOP command yet, the
	 * active bit is still set. We consider that we aren't
	 * sharing interrupts (which is hopefully the case with
	 * those controllers) and so we just try to flush the
	 * channel for pending data in the fifo
	 */
	udelay(1);
	writel((FLUSH << 16) | FLUSH, &dma->control);
	timeout = 0;
	for (;;) {
		udelay(1);
		status = readl(&dma->status);
		if ((status & FLUSH) == 0)
			break;
		if (++timeout > 100) {
			printk(KERN_WARNING "ide%d, ide_dma_test_irq \
			timeout flushing channel\n", HWIF(drive)->index);
			break;
		}
	}	
	return 1;
}

static void pmac_ide_dma_host_off(ide_drive_t *drive)
{
}

static void pmac_ide_dma_host_on(ide_drive_t *drive)
{
}

static void
pmac_ide_dma_lost_irq (ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	volatile struct dbdma_regs __iomem *dma;
	unsigned long status;

	if (pmif == NULL)
		return;
	dma = pmif->dma_regs;

	status = readl(&dma->status);
	printk(KERN_ERR "ide-pmac lost interrupt, dma status: %lx\n", status);
}

/*
 * Allocate the data structures needed for using DMA with an interface
 * and fill the proper list of functions pointers
 */
static void __init 
pmac_ide_setup_dma(pmac_ide_hwif_t *pmif, ide_hwif_t *hwif)
{
	/* We won't need pci_dev if we switch to generic consistent
	 * DMA routines ...
	 */
	if (hwif->pci_dev == NULL)
		return;
	/*
	 * Allocate space for the DBDMA commands.
	 * The +2 is +1 for the stop command and +1 to allow for
	 * aligning the start address to a multiple of 16 bytes.
	 */
	pmif->dma_table_cpu = (struct dbdma_cmd*)pci_alloc_consistent(
		hwif->pci_dev,
		(MAX_DCMDS + 2) * sizeof(struct dbdma_cmd),
		&hwif->dmatable_dma);
	if (pmif->dma_table_cpu == NULL) {
		printk(KERN_ERR "%s: unable to allocate DMA command list\n",
		       hwif->name);
		return;
	}

	hwif->dma_off_quietly = &ide_dma_off_quietly;
	hwif->ide_dma_on = &__ide_dma_on;
	hwif->ide_dma_check = &pmac_ide_dma_check;
	hwif->dma_setup = &pmac_ide_dma_setup;
	hwif->dma_exec_cmd = &pmac_ide_dma_exec_cmd;
	hwif->dma_start = &pmac_ide_dma_start;
	hwif->ide_dma_end = &pmac_ide_dma_end;
	hwif->ide_dma_test_irq = &pmac_ide_dma_test_irq;
	hwif->dma_host_off = &pmac_ide_dma_host_off;
	hwif->dma_host_on = &pmac_ide_dma_host_on;
	hwif->dma_timeout = &ide_dma_timeout;
	hwif->dma_lost_irq = &pmac_ide_dma_lost_irq;

	hwif->atapi_dma = 1;
	switch(pmif->kind) {
		case controller_sh_ata6:
			hwif->ultra_mask = pmif->cable_80 ? 0x7f : 0x07;
			hwif->mwdma_mask = 0x07;
			hwif->swdma_mask = 0x00;
			break;
		case controller_un_ata6:
		case controller_k2_ata6:
			hwif->ultra_mask = pmif->cable_80 ? 0x3f : 0x07;
			hwif->mwdma_mask = 0x07;
			hwif->swdma_mask = 0x00;
			break;
		case controller_kl_ata4:
			hwif->ultra_mask = pmif->cable_80 ? 0x1f : 0x07;
			hwif->mwdma_mask = 0x07;
			hwif->swdma_mask = 0x00;
			break;
		default:
			hwif->ultra_mask = 0x00;
			hwif->mwdma_mask = 0x07;
			hwif->swdma_mask = 0x00;
			break;
	}	
}

#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */
