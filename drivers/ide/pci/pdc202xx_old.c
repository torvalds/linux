/*
 *  linux/drivers/ide/pci/pdc202xx_old.c	Version 0.36	Sept 11, 2002
 *
 *  Copyright (C) 1998-2002		Andre Hedrick <andre@linux-ide.org>
 *
 *  Promise Ultra33 cards with BIOS v1.20 through 1.28 will need this
 *  compiled into the kernel if you have more than one card installed.
 *  Note that BIOS v1.29 is reported to fix the problem.  Since this is
 *  safe chipset tuning, including this support is harmless
 *
 *  Promise Ultra66 cards with BIOS v1.11 this
 *  compiled into the kernel if you have more than one card installed.
 *
 *  Promise Ultra100 cards.
 *
 *  The latest chipset code will support the following ::
 *  Three Ultra33 controllers and 12 drives.
 *  8 are UDMA supported and 4 are limited to DMA mode 2 multi-word.
 *  The 8/4 ratio is a BIOS code limit by promise.
 *
 *  UNLESS you enable "CONFIG_PDC202XX_BURST"
 *
 */

/*
 *  Portions Copyright (C) 1999 Promise Technology, Inc.
 *  Author: Frank Tiernan (frankt@promise.com)
 *  Released under terms of General Public License
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

#include <asm/io.h>
#include <asm/irq.h>

#define PDC202_DEBUG_CABLE		0
#define PDC202XX_DEBUG_DRIVE_INFO	0

static const char *pdc_quirk_drives[] = {
	"QUANTUM FIREBALLlct08 08",
	"QUANTUM FIREBALLP KA6.4",
	"QUANTUM FIREBALLP KA9.1",
	"QUANTUM FIREBALLP LM20.4",
	"QUANTUM FIREBALLP KX13.6",
	"QUANTUM FIREBALLP KX20.5",
	"QUANTUM FIREBALLP KX27.3",
	"QUANTUM FIREBALLP LM20.5",
	NULL
};

/* A Register */
#define	SYNC_ERRDY_EN	0xC0

#define	SYNC_IN		0x80	/* control bit, different for master vs. slave drives */
#define	ERRDY_EN	0x40	/* control bit, different for master vs. slave drives */
#define	IORDY_EN	0x20	/* PIO: IOREADY */
#define	PREFETCH_EN	0x10	/* PIO: PREFETCH */

#define	PA3		0x08	/* PIO"A" timing */
#define	PA2		0x04	/* PIO"A" timing */
#define	PA1		0x02	/* PIO"A" timing */
#define	PA0		0x01	/* PIO"A" timing */

/* B Register */

#define	MB2		0x80	/* DMA"B" timing */
#define	MB1		0x40	/* DMA"B" timing */
#define	MB0		0x20	/* DMA"B" timing */

#define	PB4		0x10	/* PIO_FORCE 1:0 */

#define	PB3		0x08	/* PIO"B" timing */	/* PIO flow Control mode */
#define	PB2		0x04	/* PIO"B" timing */	/* PIO 4 */
#define	PB1		0x02	/* PIO"B" timing */	/* PIO 3 half */
#define	PB0		0x01	/* PIO"B" timing */	/* PIO 3 other half */

/* C Register */
#define	IORDYp_NO_SPEED	0x4F
#define	SPEED_DIS	0x0F

#define	DMARQp		0x80
#define	IORDYp		0x40
#define	DMAR_EN		0x20
#define	DMAW_EN		0x10

#define	MC3		0x08	/* DMA"C" timing */
#define	MC2		0x04	/* DMA"C" timing */
#define	MC1		0x02	/* DMA"C" timing */
#define	MC0		0x01	/* DMA"C" timing */

#if 0
	unsigned long bibma  = pci_resource_start(dev, 4);
	u8 hi = 0, lo = 0;

	u8 sc1c	= inb_p((u16)bibma + 0x1c); 
	u8 sc1e	= inb_p((u16)bibma + 0x1e);
	u8 sc1f	= inb_p((u16)bibma + 0x1f);

	p += sprintf(p, "Host Mode                            : %s\n",
		(sc1f & 0x08) ? "Tri-Stated" : "Normal");
	p += sprintf(p, "Bus Clocking                         : %s\n",
		((sc1f & 0xC0) == 0xC0) ? "100 External" :
		((sc1f & 0x80) == 0x80) ? "66 External" :
		((sc1f & 0x40) == 0x40) ? "33 External" : "33 PCI Internal");
	p += sprintf(p, "IO pad select                        : %s mA\n",
		((sc1c & 0x03) == 0x03) ? "10" :
		((sc1c & 0x02) == 0x02) ? "8" :
		((sc1c & 0x01) == 0x01) ? "6" :
		((sc1c & 0x00) == 0x00) ? "4" : "??");
	hi = sc1e >> 4;
	lo = sc1e & 0xf;
	p += sprintf(p, "Status Polling Period                : %d\n", hi);
	p += sprintf(p, "Interrupt Check Status Polling Delay : %d\n", lo);
#endif

static u8 pdc202xx_ratemask (ide_drive_t *drive)
{
	u8 mode;

	switch(HWIF(drive)->pci_dev->device) {
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
			mode = 3;
			break;
		case PCI_DEVICE_ID_PROMISE_20263:
		case PCI_DEVICE_ID_PROMISE_20262:
			mode = 2;
			break;
		case PCI_DEVICE_ID_PROMISE_20246:
			return 1;
		default:
			return 0;
	}
	if (!eighty_ninty_three(drive))
		mode = min(mode, (u8)1);
	return mode;
}

static int check_in_drive_lists (ide_drive_t *drive, const char **list)
{
	struct hd_driveid *id = drive->id;

	if (pdc_quirk_drives == list) {
		while (*list) {
			if (strstr(id->model, *list++)) {
				return 2;
			}
		}
	} else {
		while (*list) {
			if (!strcmp(*list++,id->model)) {
				return 1;
			}
		}
	}
	return 0;
}

static int pdc202xx_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 drive_pci		= 0x60 + (drive->dn << 2);
	u8 speed	= ide_rate_filter(pdc202xx_ratemask(drive), xferspeed);

	u32			drive_conf;
	u8			AP, BP, CP, DP;
	u8			TA = 0, TB = 0, TC = 0;

	if ((drive->media != ide_disk) && (speed < XFER_SW_DMA_0))
		return -1;

	pci_read_config_dword(dev, drive_pci, &drive_conf);
	pci_read_config_byte(dev, (drive_pci), &AP);
	pci_read_config_byte(dev, (drive_pci)|0x01, &BP);
	pci_read_config_byte(dev, (drive_pci)|0x02, &CP);
	pci_read_config_byte(dev, (drive_pci)|0x03, &DP);

	if (speed < XFER_SW_DMA_0) {
		if ((AP & 0x0F) || (BP & 0x07)) {
			/* clear PIO modes of lower 8421 bits of A Register */
			pci_write_config_byte(dev, (drive_pci), AP &~0x0F);
			pci_read_config_byte(dev, (drive_pci), &AP);

			/* clear PIO modes of lower 421 bits of B Register */
			pci_write_config_byte(dev, (drive_pci)|0x01, BP &~0x07);
			pci_read_config_byte(dev, (drive_pci)|0x01, &BP);

			pci_read_config_byte(dev, (drive_pci), &AP);
			pci_read_config_byte(dev, (drive_pci)|0x01, &BP);
		}
	} else {
		if ((BP & 0xF0) && (CP & 0x0F)) {
			/* clear DMA modes of upper 842 bits of B Register */
			/* clear PIO forced mode upper 1 bit of B Register */
			pci_write_config_byte(dev, (drive_pci)|0x01, BP &~0xF0);
			pci_read_config_byte(dev, (drive_pci)|0x01, &BP);

			/* clear DMA modes of lower 8421 bits of C Register */
			pci_write_config_byte(dev, (drive_pci)|0x02, CP &~0x0F);
			pci_read_config_byte(dev, (drive_pci)|0x02, &CP);
		}
	}

	pci_read_config_byte(dev, (drive_pci), &AP);
	pci_read_config_byte(dev, (drive_pci)|0x01, &BP);
	pci_read_config_byte(dev, (drive_pci)|0x02, &CP);

	switch(speed) {
		case XFER_UDMA_6:	speed = XFER_UDMA_5;
		case XFER_UDMA_5:
		case XFER_UDMA_4:	TB = 0x20; TC = 0x01; break;
		case XFER_UDMA_2:	TB = 0x20; TC = 0x01; break;
		case XFER_UDMA_3:
		case XFER_UDMA_1:	TB = 0x40; TC = 0x02; break;
		case XFER_UDMA_0:
		case XFER_MW_DMA_2:	TB = 0x60; TC = 0x03; break;
		case XFER_MW_DMA_1:	TB = 0x60; TC = 0x04; break;
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_2:	TB = 0x60; TC = 0x05; break;
		case XFER_SW_DMA_1:	TB = 0x80; TC = 0x06; break;
		case XFER_SW_DMA_0:	TB = 0xC0; TC = 0x0B; break;
		case XFER_PIO_4:	TA = 0x01; TB = 0x04; break;
		case XFER_PIO_3:	TA = 0x02; TB = 0x06; break;
		case XFER_PIO_2:	TA = 0x03; TB = 0x08; break;
		case XFER_PIO_1:	TA = 0x05; TB = 0x0C; break;
		case XFER_PIO_0:
		default:		TA = 0x09; TB = 0x13; break;
	}

	if (speed < XFER_SW_DMA_0) {
		pci_write_config_byte(dev, (drive_pci), AP|TA);
		pci_write_config_byte(dev, (drive_pci)|0x01, BP|TB);
	} else {
		pci_write_config_byte(dev, (drive_pci)|0x01, BP|TB);
		pci_write_config_byte(dev, (drive_pci)|0x02, CP|TC);
	}

#if PDC202XX_DEBUG_DRIVE_INFO
	printk(KERN_DEBUG "%s: %s drive%d 0x%08x ",
		drive->name, ide_xfer_verbose(speed),
		drive->dn, drive_conf);
		pci_read_config_dword(dev, drive_pci, &drive_conf);
	printk("0x%08x\n", drive_conf);
#endif /* PDC202XX_DEBUG_DRIVE_INFO */

	return (ide_config_drive_speed(drive, speed));
}


/*   0    1    2    3    4    5    6   7   8
 * 960, 480, 390, 300, 240, 180, 120, 90, 60
 *           180, 150, 120,  90,  60
 * DMA_Speed
 * 180, 120,  90,  90,  90,  60,  30
 *  11,   5,   4,   3,   2,   1,   0
 */
static void config_chipset_for_pio (ide_drive_t *drive, u8 pio)
{
	u8 speed = 0;

	if (pio == 5) pio = 4;
	speed = XFER_PIO_0 + ide_get_best_pio_mode(drive, 255, pio, NULL);
        
	pdc202xx_tune_chipset(drive, speed);
}

static u8 pdc202xx_old_cable_detect (ide_hwif_t *hwif)
{
	u16 CIS = 0, mask = (hwif->channel) ? (1<<11) : (1<<10);
	pci_read_config_word(hwif->pci_dev, 0x50, &CIS);
	return (CIS & mask) ? 1 : 0;
}

/*
 * Set the control register to use the 66MHz system
 * clock for UDMA 3/4/5 mode operation when necessary.
 *
 * It may also be possible to leave the 66MHz clock on
 * and readjust the timing parameters.
 */
static void pdc_old_enable_66MHz_clock(ide_hwif_t *hwif)
{
	unsigned long clock_reg = hwif->dma_master + 0x11;
	u8 clock = hwif->INB(clock_reg);

	hwif->OUTB(clock | (hwif->channel ? 0x08 : 0x02), clock_reg);
}

static void pdc_old_disable_66MHz_clock(ide_hwif_t *hwif)
{
	unsigned long clock_reg = hwif->dma_master + 0x11;
	u8 clock = hwif->INB(clock_reg);

	hwif->OUTB(clock & ~(hwif->channel ? 0x08 : 0x02), clock_reg);
}

static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u32 drive_conf		= 0;
	u8 drive_pci		= 0x60 + (drive->dn << 2);
	u8 test1 = 0, test2 = 0, speed = -1;
	u8 AP = 0, cable = 0;

	u8 ultra_66		= ((id->dma_ultra & 0x0010) ||
				   (id->dma_ultra & 0x0008)) ? 1 : 0;

	if (dev->device != PCI_DEVICE_ID_PROMISE_20246)
		cable = pdc202xx_old_cable_detect(hwif);
	else
		ultra_66 = 0;

	if (ultra_66 && cable) {
		printk(KERN_WARNING "Warning: %s channel requires an 80-pin cable for operation.\n", hwif->channel ? "Secondary":"Primary");
		printk(KERN_WARNING "%s reduced to Ultra33 mode.\n", drive->name);
	}

	if (dev->device != PCI_DEVICE_ID_PROMISE_20246)
		pdc_old_disable_66MHz_clock(drive->hwif);

	drive_pci = 0x60 + (drive->dn << 2);
	pci_read_config_dword(dev, drive_pci, &drive_conf);
	if ((drive_conf != 0x004ff304) && (drive_conf != 0x004ff3c4))
		goto chipset_is_set;

	pci_read_config_byte(dev, drive_pci, &test1);
	if (!(test1 & SYNC_ERRDY_EN)) {
		if (drive->select.b.unit & 0x01) {
			pci_read_config_byte(dev, drive_pci - 4, &test2);
			if ((test2 & SYNC_ERRDY_EN) &&
			    !(test1 & SYNC_ERRDY_EN)) {
				pci_write_config_byte(dev, drive_pci,
					test1|SYNC_ERRDY_EN);
			}
		} else {
			pci_write_config_byte(dev, drive_pci,
				test1|SYNC_ERRDY_EN);
		}
	}

chipset_is_set:

	if (drive->media == ide_disk) {
		pci_read_config_byte(dev, (drive_pci), &AP);
		if (id->capability & 4)	/* IORDY_EN */
			pci_write_config_byte(dev, (drive_pci), AP|IORDY_EN);
		pci_read_config_byte(dev, (drive_pci), &AP);
		if (drive->media == ide_disk)	/* PREFETCH_EN */
			pci_write_config_byte(dev, (drive_pci), AP|PREFETCH_EN);
	}

	speed = ide_dma_speed(drive, pdc202xx_ratemask(drive));

	if (!(speed)) {
		/* restore original pci-config space */
		pci_write_config_dword(dev, drive_pci, drive_conf);
		hwif->tuneproc(drive, 5);
		return 0;
	}

	(void) hwif->speedproc(drive, speed);
	return ide_dma_enable(drive);
}

static int pdc202xx_config_drive_xfer_rate (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	drive->init_speed = 0;

	if (id && (id->capability & 1) && drive->autodma) {

		if (ide_use_dma(drive)) {
			if (config_chipset_for_dma(drive))
				return hwif->ide_dma_on(drive);
		}

		goto fast_ata_pio;

	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		hwif->tuneproc(drive, 5);
		return hwif->ide_dma_off_quietly(drive);
	}
	/* IORDY not supported */
	return 0;
}

static int pdc202xx_quirkproc (ide_drive_t *drive)
{
	return ((int) check_in_drive_lists(drive, pdc_quirk_drives));
}

static void pdc202xx_old_ide_dma_start(ide_drive_t *drive)
{
	if (drive->current_speed > XFER_UDMA_2)
		pdc_old_enable_66MHz_clock(drive->hwif);
	if (drive->addressing == 1) {
		struct request *rq	= HWGROUP(drive)->rq;
		ide_hwif_t *hwif	= HWIF(drive);
//		struct pci_dev *dev	= hwif->pci_dev;
//		unsgned long high_16	= pci_resource_start(dev, 4);
		unsigned long high_16   = hwif->dma_master;
		unsigned long atapi_reg	= high_16 + (hwif->channel ? 0x24 : 0x20);
		u32 word_count	= 0;
		u8 clock = hwif->INB(high_16 + 0x11);

		hwif->OUTB(clock|(hwif->channel ? 0x08 : 0x02), high_16+0x11);
		word_count = (rq->nr_sectors << 8);
		word_count = (rq_data_dir(rq) == READ) ?
					word_count | 0x05000000 :
					word_count | 0x06000000;
		hwif->OUTL(word_count, atapi_reg);
	}
	ide_dma_start(drive);
}

static int pdc202xx_old_ide_dma_end(ide_drive_t *drive)
{
	if (drive->addressing == 1) {
		ide_hwif_t *hwif	= HWIF(drive);
//		unsigned long high_16	= pci_resource_start(hwif->pci_dev, 4);
		unsigned long high_16	= hwif->dma_master;
		unsigned long atapi_reg	= high_16 + (hwif->channel ? 0x24 : 0x20);
		u8 clock		= 0;

		hwif->OUTL(0, atapi_reg); /* zero out extra */
		clock = hwif->INB(high_16 + 0x11);
		hwif->OUTB(clock & ~(hwif->channel ? 0x08:0x02), high_16+0x11);
	}
	if (drive->current_speed > XFER_UDMA_2)
		pdc_old_disable_66MHz_clock(drive->hwif);
	return __ide_dma_end(drive);
}

static int pdc202xx_old_ide_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
//	struct pci_dev *dev	= hwif->pci_dev;
//	unsigned long high_16	= pci_resource_start(dev, 4);
	unsigned long high_16	= hwif->dma_master;
	u8 dma_stat		= hwif->INB(hwif->dma_status);
	u8 sc1d			= hwif->INB((high_16 + 0x001d));

	if (hwif->channel) {
		/* bit7: Error, bit6: Interrupting, bit5: FIFO Full, bit4: FIFO Empty */
		if ((sc1d & 0x50) == 0x50)
			goto somebody_else;
		else if ((sc1d & 0x40) == 0x40)
			return (dma_stat & 4) == 4;
	} else {
		/* bit3: Error, bit2: Interrupting, bit1: FIFO Full, bit0: FIFO Empty */
		if ((sc1d & 0x05) == 0x05)
			goto somebody_else;
		else if ((sc1d & 0x04) == 0x04)
			return (dma_stat & 4) == 4;
	}
somebody_else:
	return (dma_stat & 4) == 4;	/* return 1 if INTR asserted */
}

static int pdc202xx_ide_dma_lostirq(ide_drive_t *drive)
{
	if (HWIF(drive)->resetproc != NULL)
		HWIF(drive)->resetproc(drive);
	return __ide_dma_lostirq(drive);
}

static int pdc202xx_ide_dma_timeout(ide_drive_t *drive)
{
	if (HWIF(drive)->resetproc != NULL)
		HWIF(drive)->resetproc(drive);
	return __ide_dma_timeout(drive);
}

static void pdc202xx_reset_host (ide_hwif_t *hwif)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
//	unsigned long high_16	= hwif->dma_base - (8*(hwif->channel));
	unsigned long high_16	= hwif->dma_master;
#else /* !CONFIG_BLK_DEV_IDEDMA */
	unsigned long high_16	= pci_resource_start(hwif->pci_dev, 4);
#endif /* CONFIG_BLK_DEV_IDEDMA */
	u8 udma_speed_flag	= hwif->INB(high_16|0x001f);

	hwif->OUTB((udma_speed_flag | 0x10), (high_16|0x001f));
	mdelay(100);
	hwif->OUTB((udma_speed_flag & ~0x10), (high_16|0x001f));
	mdelay(2000);	/* 2 seconds ?! */

	printk(KERN_WARNING "PDC202XX: %s channel reset.\n",
		hwif->channel ? "Secondary" : "Primary");
}

static void pdc202xx_reset (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	ide_hwif_t *mate	= hwif->mate;
	
	pdc202xx_reset_host(hwif);
	pdc202xx_reset_host(mate);
#if 0
	/*
	 * FIXME: Have to kick all the drives again :-/
	 * What a pain in the ACE!
	 */
	if (hwif->present) {
		u16 hunit = 0;
		for (hunit = 0; hunit < MAX_DRIVES; ++hunit) {
			ide_drive_t *hdrive = &hwif->drives[hunit];
			if (hdrive->present) {
				if (hwif->ide_dma_check)
					hwif->ide_dma_check(hdrive);
				else
					hwif->tuneproc(hdrive, 5);
			}
		}
	}
	if (mate->present) {
		u16 munit = 0;
		for (munit = 0; munit < MAX_DRIVES; ++munit) {
			ide_drive_t *mdrive = &mate->drives[munit];
			if (mdrive->present) {
				if (mate->ide_dma_check) 
					mate->ide_dma_check(mdrive);
				else
					mate->tuneproc(mdrive, 5);
			}
		}
	}
#else
	hwif->tuneproc(drive, 5);
#endif
}

/*
 * Since SUN Cobalt is attempting to do this operation, I should disclose
 * this has been a long time ago Thu Jul 27 16:40:57 2000 was the patch date
 * HOTSWAP ATA Infrastructure.
 */
static int pdc202xx_tristate (ide_drive_t * drive, int state)
{
	ide_hwif_t *hwif	= HWIF(drive);
//	unsigned long high_16	= hwif->dma_base - (8*(hwif->channel));
	unsigned long high_16	= hwif->dma_master;
	u8 sc1f			= hwif->INB(high_16|0x001f);

	if (!hwif)
		return -EINVAL;

//	hwif->bus_state = state;

	if (state) {
		hwif->OUTB(sc1f | 0x08, (high_16|0x001f));
	} else {
		hwif->OUTB(sc1f & ~0x08, (high_16|0x001f));
	}
	return 0;
}

static unsigned int __devinit init_chipset_pdc202xx(struct pci_dev *dev, const char *name)
{
	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS,
			dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk(KERN_INFO "%s: ROM enabled at 0x%08lx\n",
			name, dev->resource[PCI_ROM_RESOURCE].start);
	}

	/*
	 * software reset -  this is required because the bios
	 * will set UDMA timing on if the hdd supports it. The
	 * user may want to turn udma off. A bug in the pdc20262
	 * is that it cannot handle a downgrade in timing from
	 * UDMA to DMA. Disk accesses after issuing a set
	 * feature command will result in errors. A software
	 * reset leaves the timing registers intact,
	 * but resets the drives.
	 */
#if 0
	if ((dev->device == PCI_DEVICE_ID_PROMISE_20267) ||
	    (dev->device == PCI_DEVICE_ID_PROMISE_20265) ||
	    (dev->device == PCI_DEVICE_ID_PROMISE_20263) ||
	    (dev->device == PCI_DEVICE_ID_PROMISE_20262)) {
		unsigned long high_16	= pci_resource_start(dev, 4);
		byte udma_speed_flag	= inb(high_16 + 0x001f);
		outb(udma_speed_flag | 0x10, high_16 + 0x001f);
		mdelay(100);
		outb(udma_speed_flag & ~0x10, high_16 + 0x001f);
		mdelay(2000);	/* 2 seconds ?! */
	}

#endif
	return dev->irq;
}

static void __devinit init_hwif_pdc202xx(ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;

	/* PDC20265 has problems with large LBA48 requests */
	if ((dev->device == PCI_DEVICE_ID_PROMISE_20267) ||
	    (dev->device == PCI_DEVICE_ID_PROMISE_20265))
		hwif->rqsize = 256;

	hwif->autodma = 0;
	hwif->tuneproc  = &config_chipset_for_pio;
	hwif->quirkproc = &pdc202xx_quirkproc;

	if (hwif->pci_dev->device != PCI_DEVICE_ID_PROMISE_20246) {
		hwif->busproc   = &pdc202xx_tristate;
		hwif->resetproc = &pdc202xx_reset;
	}

	hwif->speedproc = &pdc202xx_tune_chipset;

	hwif->drives[0].autotune = hwif->drives[1].autotune = 1;

	hwif->ultra_mask = 0x3f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

	hwif->ide_dma_check = &pdc202xx_config_drive_xfer_rate;
	hwif->ide_dma_lostirq = &pdc202xx_ide_dma_lostirq;
	hwif->ide_dma_timeout = &pdc202xx_ide_dma_timeout;

	if (hwif->pci_dev->device != PCI_DEVICE_ID_PROMISE_20246) {
		if (!(hwif->udma_four))
			hwif->udma_four = (pdc202xx_old_cable_detect(hwif)) ? 0 : 1;
		hwif->dma_start = &pdc202xx_old_ide_dma_start;
		hwif->ide_dma_end = &pdc202xx_old_ide_dma_end;
	} 
	hwif->ide_dma_test_irq = &pdc202xx_old_ide_dma_test_irq;

	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->drives[1].autodma = hwif->autodma;
#if PDC202_DEBUG_CABLE
	printk(KERN_DEBUG "%s: %s-pin cable\n",
		hwif->name, hwif->udma_four ? "80" : "40");
#endif /* PDC202_DEBUG_CABLE */	
}

static void __devinit init_dma_pdc202xx(ide_hwif_t *hwif, unsigned long dmabase)
{
	u8 udma_speed_flag = 0, primary_mode = 0, secondary_mode = 0;

	if (hwif->channel) {
		ide_setup_dma(hwif, dmabase, 8);
		return;
	}

	udma_speed_flag	= hwif->INB((dmabase|0x1f));
	primary_mode	= hwif->INB((dmabase|0x1a));
	secondary_mode	= hwif->INB((dmabase|0x1b));
	printk(KERN_INFO "%s: (U)DMA Burst Bit %sABLED " \
		"Primary %s Mode " \
		"Secondary %s Mode.\n", hwif->cds->name,
		(udma_speed_flag & 1) ? "EN" : "DIS",
		(primary_mode & 1) ? "MASTER" : "PCI",
		(secondary_mode & 1) ? "MASTER" : "PCI" );

#ifdef CONFIG_PDC202XX_BURST
	if (!(udma_speed_flag & 1)) {
		printk(KERN_INFO "%s: FORCING BURST BIT 0x%02x->0x%02x ",
			hwif->cds->name, udma_speed_flag,
			(udma_speed_flag|1));
		hwif->OUTB(udma_speed_flag|1,(dmabase|0x1f));
		printk("%sACTIVE\n",
			(hwif->INB(dmabase|0x1f)&1) ? "":"IN");
	}
#endif /* CONFIG_PDC202XX_BURST */
#ifdef CONFIG_PDC202XX_MASTER
	if (!(primary_mode & 1)) {
		printk(KERN_INFO "%s: FORCING PRIMARY MODE BIT "
			"0x%02x -> 0x%02x ", hwif->cds->name,
			primary_mode, (primary_mode|1));
		hwif->OUTB(primary_mode|1, (dmabase|0x1a));
		printk("%s\n",
			(hwif->INB((dmabase|0x1a)) & 1) ? "MASTER" : "PCI");
	}

	if (!(secondary_mode & 1)) {
		printk(KERN_INFO "%s: FORCING SECONDARY MODE BIT "
			"0x%02x -> 0x%02x ", hwif->cds->name,
			secondary_mode, (secondary_mode|1));
		hwif->OUTB(secondary_mode|1, (dmabase|0x1b));
		printk("%s\n",
			(hwif->INB((dmabase|0x1b)) & 1) ? "MASTER" : "PCI");
	}
#endif /* CONFIG_PDC202XX_MASTER */

	ide_setup_dma(hwif, dmabase, 8);
}

static int __devinit init_setup_pdc202ata4(struct pci_dev *dev,
					   ide_pci_device_t *d)
{
	if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE) {
		u8 irq = 0, irq2 = 0;
		pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
		/* 0xbc */
		pci_read_config_byte(dev, (PCI_INTERRUPT_LINE)|0x80, &irq2);
		if (irq != irq2) {
			pci_write_config_byte(dev,
				(PCI_INTERRUPT_LINE)|0x80, irq);     /* 0xbc */
			printk(KERN_INFO "%s: pci-config space interrupt "
				"mirror fixed.\n", d->name);
		}
	}

#if 0
        if (dev->device == PCI_DEVICE_ID_PROMISE_20262)
        if (e->reg && (pci_read_config_byte(dev, e->reg, &tmp) ||
             (tmp & e->mask) != e->val))

        if (d->enablebits[0].reg != d->enablebits[1].reg) {
                d->enablebits[0].reg    = d->enablebits[1].reg;
                d->enablebits[0].mask   = d->enablebits[1].mask;
                d->enablebits[0].val    = d->enablebits[1].val;
        }
#endif

	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_pdc20265(struct pci_dev *dev,
					 ide_pci_device_t *d)
{
	if ((dev->bus->self) &&
	    (dev->bus->self->vendor == PCI_VENDOR_ID_INTEL) &&
	    ((dev->bus->self->device == PCI_DEVICE_ID_INTEL_I960) ||
	     (dev->bus->self->device == PCI_DEVICE_ID_INTEL_I960RM))) {
		printk(KERN_INFO "ide: Skipping Promise PDC20265 "
			"attached to I2O RAID controller.\n");
		return -ENODEV;
	}

#if 0
        {
                u8 pri = 0, sec = 0;

        if (e->reg && (pci_read_config_byte(dev, e->reg, &tmp) ||
             (tmp & e->mask) != e->val))

        if (d->enablebits[0].reg != d->enablebits[1].reg) {
                d->enablebits[0].reg    = d->enablebits[1].reg;
                d->enablebits[0].mask   = d->enablebits[1].mask;
                d->enablebits[0].val    = d->enablebits[1].val;
        }
        }
#endif

	return ide_setup_pci_device(dev, d);
}

static int __devinit init_setup_pdc202xx(struct pci_dev *dev,
					 ide_pci_device_t *d)
{
	return ide_setup_pci_device(dev, d);
}

static ide_pci_device_t pdc202xx_chipsets[] __devinitdata = {
	{	/* 0 */
		.name		= "PDC20246",
		.init_setup	= init_setup_pdc202ata4,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
		.extra		= 16,
	},{	/* 1 */
		.name		= "PDC20262",
		.init_setup	= init_setup_pdc202ata4,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
		.extra		= 48,
		.flags		= IDEPCI_FLAG_FORCE_PDC,
	},{	/* 2 */
		.name		= "PDC20263",
		.init_setup	= init_setup_pdc202ata4,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
		.extra		= 48,
	},{	/* 3 */
		.name		= "PDC20265",
		.init_setup	= init_setup_pdc20265,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
		.extra		= 48,
		.flags		= IDEPCI_FLAG_FORCE_PDC,
	},{	/* 4 */
		.name		= "PDC20267",
		.init_setup	= init_setup_pdc202xx,
		.init_chipset	= init_chipset_pdc202xx,
		.init_hwif	= init_hwif_pdc202xx,
		.init_dma	= init_dma_pdc202xx,
		.channels	= 2,
		.autodma	= AUTODMA,
		.bootable	= OFF_BOARD,
		.extra		= 48,
	}
};

/**
 *	pdc202xx_init_one	-	called when a PDC202xx is found
 *	@dev: the pdc202xx device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 */
 
static int __devinit pdc202xx_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &pdc202xx_chipsets[id->driver_data];

	return d->init_setup(dev, d);
}

static struct pci_device_id pdc202xx_pci_tbl[] = {
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20246, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20262, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20263, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20265, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{ PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20267, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, pdc202xx_pci_tbl);

static struct pci_driver driver = {
	.name		= "Promise_Old_IDE",
	.id_table	= pdc202xx_pci_tbl,
	.probe		= pdc202xx_init_one,
};

static int pdc202xx_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(pdc202xx_ide_init);

MODULE_AUTHOR("Andre Hedrick, Frank Tiernan");
MODULE_DESCRIPTION("PCI driver module for older Promise IDE");
MODULE_LICENSE("GPL");
