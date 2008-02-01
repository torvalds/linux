/*
 * Copyright (C) 1999-2000	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2002		Lionel Bouton <Lionel.Bouton@inet6.fr>, Maintainer
 * Copyright (C) 2003		Vojtech Pavlik <vojtech@suse.cz>
 * Copyright (C) 2007		Bartlomiej Zolnierkiewicz
 *
 * May be copied or modified under the terms of the GNU General Public License
 *
 *
 * Thanks :
 *
 * SiS Taiwan		: for direct support and hardware.
 * Daniela Engert	: for initial ATA100 advices and numerous others.
 * John Fremlin, Manfred Spraul, Dave Morgan, Peter Kjellerstedt	:
 *			  for checking code correctness, providing patches.
 *
 *
 * Original tests and design on the SiS620 chipset.
 * ATA100 tests and design on the SiS735 chipset.
 * ATA16/33 support from specs
 * ATA133 support for SiS961/962 by L.C. Chang <lcchang@sis.com.tw>
 * ATA133 961/962/963 fixes by Vojtech Pavlik <vojtech@suse.cz>
 *
 * Documentation:
 *	SiS chipset documentation available under NDA to companies only
 *      (not to individuals).
 */

/*
 * The original SiS5513 comes from a SiS5511/55112/5513 chipset. The original
 * SiS5513 was also used in the SiS5596/5513 chipset. Thus if we see a SiS5511
 * or SiS5596, we can assume we see the first MWDMA-16 capable SiS5513 chip.
 *
 * Later SiS chipsets integrated the 5513 functionality into the NorthBridge,
 * starting with SiS5571 and up to SiS745. The PCI ID didn't change, though. We
 * can figure out that we have a more modern and more capable 5513 by looking
 * for the respective NorthBridge IDs.
 *
 * Even later (96x family) SiS chipsets use the MuTIOL link and place the 5513
 * into the SouthBrige. Here we cannot rely on looking up the NorthBridge PCI
 * ID, while the now ATA-133 capable 5513 still has the same PCI ID.
 * Fortunately the 5513 can be 'unmasked' by fiddling with some config space
 * bits, changing its device id to the true one - 5517 for 961 and 5518 for
 * 962/963.
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

#include <asm/irq.h>

#include "ide-timing.h"

/* registers layout and init values are chipset family dependant */

#define ATA_16		0x01
#define ATA_33		0x02
#define ATA_66		0x03
#define ATA_100a	0x04 // SiS730/SiS550 is ATA100 with ATA66 layout
#define ATA_100		0x05
#define ATA_133a	0x06 // SiS961b with 133 support
#define ATA_133		0x07 // SiS962/963

static u8 chipset_family;

/*
 * Devices supported
 */
static const struct {
	const char *name;
	u16 host_id;
	u8 chipset_family;
	u8 flags;
} SiSHostChipInfo[] = {
	{ "SiS968",	PCI_DEVICE_ID_SI_968,	ATA_133  },
	{ "SiS966",	PCI_DEVICE_ID_SI_966,	ATA_133  },
	{ "SiS965",	PCI_DEVICE_ID_SI_965,	ATA_133  },
	{ "SiS745",	PCI_DEVICE_ID_SI_745,	ATA_100  },
	{ "SiS735",	PCI_DEVICE_ID_SI_735,	ATA_100  },
	{ "SiS733",	PCI_DEVICE_ID_SI_733,	ATA_100  },
	{ "SiS635",	PCI_DEVICE_ID_SI_635,	ATA_100  },
	{ "SiS633",	PCI_DEVICE_ID_SI_633,	ATA_100  },

	{ "SiS730",	PCI_DEVICE_ID_SI_730,	ATA_100a },
	{ "SiS550",	PCI_DEVICE_ID_SI_550,	ATA_100a },

	{ "SiS640",	PCI_DEVICE_ID_SI_640,	ATA_66   },
	{ "SiS630",	PCI_DEVICE_ID_SI_630,	ATA_66   },
	{ "SiS620",	PCI_DEVICE_ID_SI_620,	ATA_66   },
	{ "SiS540",	PCI_DEVICE_ID_SI_540,	ATA_66   },
	{ "SiS530",	PCI_DEVICE_ID_SI_530,	ATA_66   },

	{ "SiS5600",	PCI_DEVICE_ID_SI_5600,	ATA_33   },
	{ "SiS5598",	PCI_DEVICE_ID_SI_5598,	ATA_33   },
	{ "SiS5597",	PCI_DEVICE_ID_SI_5597,	ATA_33   },
	{ "SiS5591/2",	PCI_DEVICE_ID_SI_5591,	ATA_33   },
	{ "SiS5582",	PCI_DEVICE_ID_SI_5582,	ATA_33   },
	{ "SiS5581",	PCI_DEVICE_ID_SI_5581,	ATA_33   },

	{ "SiS5596",	PCI_DEVICE_ID_SI_5596,	ATA_16   },
	{ "SiS5571",	PCI_DEVICE_ID_SI_5571,	ATA_16   },
	{ "SiS5517",	PCI_DEVICE_ID_SI_5517,	ATA_16   },
	{ "SiS551x",	PCI_DEVICE_ID_SI_5511,	ATA_16   },
};

/* Cycle time bits and values vary across chip dma capabilities
   These three arrays hold the register layout and the values to set.
   Indexed by chipset_family and (dma_mode - XFER_UDMA_0) */

/* {0, ATA_16, ATA_33, ATA_66, ATA_100a, ATA_100, ATA_133} */
static u8 cycle_time_offset[] = {0,0,5,4,4,0,0};
static u8 cycle_time_range[] = {0,0,2,3,3,4,4};
static u8 cycle_time_value[][XFER_UDMA_6 - XFER_UDMA_0 + 1] = {
	{0,0,0,0,0,0,0}, /* no udma */
	{0,0,0,0,0,0,0}, /* no udma */
	{3,2,1,0,0,0,0}, /* ATA_33 */
	{7,5,3,2,1,0,0}, /* ATA_66 */
	{7,5,3,2,1,0,0}, /* ATA_100a (730 specific), differences are on cycle_time range and offset */
	{11,7,5,4,2,1,0}, /* ATA_100 */
	{15,10,7,5,3,2,1}, /* ATA_133a (earliest 691 southbridges) */
	{15,10,7,5,3,2,1}, /* ATA_133 */
};
/* CRC Valid Setup Time vary across IDE clock setting 33/66/100/133
   See SiS962 data sheet for more detail */
static u8 cvs_time_value[][XFER_UDMA_6 - XFER_UDMA_0 + 1] = {
	{0,0,0,0,0,0,0}, /* no udma */
	{0,0,0,0,0,0,0}, /* no udma */
	{2,1,1,0,0,0,0},
	{4,3,2,1,0,0,0},
	{4,3,2,1,0,0,0},
	{6,4,3,1,1,1,0},
	{9,6,4,2,2,2,2},
	{9,6,4,2,2,2,2},
};
/* Initialize time, Active time, Recovery time vary across
   IDE clock settings. These 3 arrays hold the register value
   for PIO0/1/2/3/4 and DMA0/1/2 mode in order */
static u8 ini_time_value[][8] = {
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{2,1,0,0,0,1,0,0},
	{4,3,1,1,1,3,1,1},
	{4,3,1,1,1,3,1,1},
	{6,4,2,2,2,4,2,2},
	{9,6,3,3,3,6,3,3},
	{9,6,3,3,3,6,3,3},
};
static u8 act_time_value[][8] = {
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{9,9,9,2,2,7,2,2},
	{19,19,19,5,4,14,5,4},
	{19,19,19,5,4,14,5,4},
	{28,28,28,7,6,21,7,6},
	{38,38,38,10,9,28,10,9},
	{38,38,38,10,9,28,10,9},
};
static u8 rco_time_value[][8] = {
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{9,2,0,2,0,7,1,1},
	{19,5,1,5,2,16,3,2},
	{19,5,1,5,2,16,3,2},
	{30,9,3,9,4,25,6,4},
	{40,12,4,12,5,34,12,5},
	{40,12,4,12,5,34,12,5},
};

/*
 * Printing configuration
 */
/* Used for chipset type printing at boot time */
static char* chipset_capability[] = {
	"ATA", "ATA 16",
	"ATA 33", "ATA 66",
	"ATA 100 (1st gen)", "ATA 100 (2nd gen)",
	"ATA 133 (1st gen)", "ATA 133 (2nd gen)"
};

/*
 * Configuration functions
 */

static u8 sis_ata133_get_base(ide_drive_t *drive)
{
	struct pci_dev *dev = to_pci_dev(drive->hwif->dev);
	u32 reg54 = 0;

	pci_read_config_dword(dev, 0x54, &reg54);

	return ((reg54 & 0x40000000) ? 0x70 : 0x40) + drive->dn * 4;
}

static void sis_ata16_program_timings(ide_drive_t *drive, const u8 mode)
{
	struct pci_dev *dev = to_pci_dev(drive->hwif->dev);
	u16 t1 = 0;
	u8 drive_pci = 0x40 + drive->dn * 2;

	const u16 pio_timings[]   = { 0x000, 0x607, 0x404, 0x303, 0x301 };
	const u16 mwdma_timings[] = { 0x008, 0x302, 0x301 };

	pci_read_config_word(dev, drive_pci, &t1);

	/* clear active/recovery timings */
	t1 &= ~0x070f;
	if (mode >= XFER_MW_DMA_0) {
		if (chipset_family > ATA_16)
			t1 &= ~0x8000;	/* disable UDMA */
		t1 |= mwdma_timings[mode - XFER_MW_DMA_0];
	} else
		t1 |= pio_timings[mode - XFER_PIO_0];

	pci_write_config_word(dev, drive_pci, t1);
}

static void sis_ata100_program_timings(ide_drive_t *drive, const u8 mode)
{
	struct pci_dev *dev = to_pci_dev(drive->hwif->dev);
	u8 t1, drive_pci = 0x40 + drive->dn * 2;

	/* timing bits: 7:4 active 3:0 recovery */
	const u8 pio_timings[]   = { 0x00, 0x67, 0x44, 0x33, 0x31 };
	const u8 mwdma_timings[] = { 0x08, 0x32, 0x31 };

	if (mode >= XFER_MW_DMA_0) {
		u8 t2 = 0;

		pci_read_config_byte(dev, drive_pci, &t2);
		t2 &= ~0x80;	/* disable UDMA */
		pci_write_config_byte(dev, drive_pci, t2);

		t1 = mwdma_timings[mode - XFER_MW_DMA_0];
	} else
		t1 = pio_timings[mode - XFER_PIO_0];

	pci_write_config_byte(dev, drive_pci + 1, t1);
}

static void sis_ata133_program_timings(ide_drive_t *drive, const u8 mode)
{
	struct pci_dev *dev = to_pci_dev(drive->hwif->dev);
	u32 t1 = 0;
	u8 drive_pci = sis_ata133_get_base(drive), clk, idx;

	pci_read_config_dword(dev, drive_pci, &t1);

	t1 &= 0xc0c00fff;
	clk = (t1 & 0x08) ? ATA_133 : ATA_100;
	if (mode >= XFER_MW_DMA_0) {
		t1 &= ~0x04;	/* disable UDMA */
		idx = mode - XFER_MW_DMA_0 + 5;
	} else
		idx = mode - XFER_PIO_0;
	t1 |= ini_time_value[clk][idx] << 12;
	t1 |= act_time_value[clk][idx] << 16;
	t1 |= rco_time_value[clk][idx] << 24;

	pci_write_config_dword(dev, drive_pci, t1);
}

static void sis_program_timings(ide_drive_t *drive, const u8 mode)
{
	if (chipset_family < ATA_100)		/* ATA_16/33/66/100a */
		sis_ata16_program_timings(drive, mode);
	else if (chipset_family < ATA_133)	/* ATA_100/133a */
		sis_ata100_program_timings(drive, mode);
	else					/* ATA_133 */
		sis_ata133_program_timings(drive, mode);
}

static void config_drive_art_rwp (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u8 reg4bh		= 0;
	u8 rw_prefetch		= 0;

	pci_read_config_byte(dev, 0x4b, &reg4bh);

	if (drive->media == ide_disk)
		rw_prefetch = 0x11 << drive->dn;

	if ((reg4bh & (0x11 << drive->dn)) != rw_prefetch)
		pci_write_config_byte(dev, 0x4b, reg4bh|rw_prefetch);
}

static void sis_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
	config_drive_art_rwp(drive);
	sis_program_timings(drive, XFER_PIO_0 + pio);
}

static void sis_ata133_program_udma_timings(ide_drive_t *drive, const u8 mode)
{
	struct pci_dev *dev = to_pci_dev(drive->hwif->dev);
	u32 regdw = 0;
	u8 drive_pci = sis_ata133_get_base(drive), clk, idx;

	pci_read_config_dword(dev, drive_pci, &regdw);

	regdw |= 0x04;
	regdw &= 0xfffff00f;
	/* check if ATA133 enable */
	clk = (regdw & 0x08) ? ATA_133 : ATA_100;
	idx = mode - XFER_UDMA_0;
	regdw |= cycle_time_value[clk][idx] << 4;
	regdw |= cvs_time_value[clk][idx] << 8;

	pci_write_config_dword(dev, drive_pci, regdw);
}

static void sis_ata33_program_udma_timings(ide_drive_t *drive, const u8 mode)
{
	struct pci_dev *dev = to_pci_dev(drive->hwif->dev);
	u8 drive_pci = 0x40 + drive->dn * 2, reg = 0, i = chipset_family;

	pci_read_config_byte(dev, drive_pci + 1, &reg);

	/* force the UDMA bit on if we want to use UDMA */
	reg |= 0x80;
	/* clean reg cycle time bits */
	reg &= ~((0xff >> (8 - cycle_time_range[i])) << cycle_time_offset[i]);
	/* set reg cycle time bits */
	reg |= cycle_time_value[i][mode - XFER_UDMA_0] << cycle_time_offset[i];

	pci_write_config_byte(dev, drive_pci + 1, reg);
}

static void sis_program_udma_timings(ide_drive_t *drive, const u8 mode)
{
	if (chipset_family >= ATA_133)	/* ATA_133 */
		sis_ata133_program_udma_timings(drive, mode);
	else				/* ATA_33/66/100a/100/133a */
		sis_ata33_program_udma_timings(drive, mode);
}

static void sis_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	if (speed >= XFER_UDMA_0)
		sis_program_udma_timings(drive, speed);
	else
		sis_program_timings(drive, speed);
}

static u8 sis5513_ata133_udma_filter(ide_drive_t *drive)
{
	struct pci_dev *dev = to_pci_dev(drive->hwif->dev);
	u32 regdw = 0;
	u8 drive_pci = sis_ata133_get_base(drive);

	pci_read_config_dword(dev, drive_pci, &regdw);

	/* if ATA133 disable, we should not set speed above UDMA5 */
	return (regdw & 0x08) ? ATA_UDMA6 : ATA_UDMA5;
}

/* Chip detection and general config */
static unsigned int __devinit init_chipset_sis5513 (struct pci_dev *dev, const char *name)
{
	struct pci_dev *host;
	int i = 0;

	chipset_family = 0;

	for (i = 0; i < ARRAY_SIZE(SiSHostChipInfo) && !chipset_family; i++) {

		host = pci_get_device(PCI_VENDOR_ID_SI, SiSHostChipInfo[i].host_id, NULL);

		if (!host)
			continue;

		chipset_family = SiSHostChipInfo[i].chipset_family;

		/* Special case for SiS630 : 630S/ET is ATA_100a */
		if (SiSHostChipInfo[i].host_id == PCI_DEVICE_ID_SI_630) {
			if (host->revision >= 0x30)
				chipset_family = ATA_100a;
		}
		pci_dev_put(host);
	
		printk(KERN_INFO "SIS5513: %s %s controller\n",
			 SiSHostChipInfo[i].name, chipset_capability[chipset_family]);
	}

	if (!chipset_family) { /* Belongs to pci-quirks */

			u32 idemisc;
			u16 trueid;

			/* Disable ID masking and register remapping */
			pci_read_config_dword(dev, 0x54, &idemisc);
			pci_write_config_dword(dev, 0x54, (idemisc & 0x7fffffff));
			pci_read_config_word(dev, PCI_DEVICE_ID, &trueid);
			pci_write_config_dword(dev, 0x54, idemisc);

			if (trueid == 0x5518) {
				printk(KERN_INFO "SIS5513: SiS 962/963 MuTIOL IDE UDMA133 controller\n");
				chipset_family = ATA_133;

				/* Check for 5513 compability mapping
				 * We must use this, else the port enabled code will fail,
				 * as it expects the enablebits at 0x4a.
				 */
				if ((idemisc & 0x40000000) == 0) {
					pci_write_config_dword(dev, 0x54, idemisc | 0x40000000);
					printk(KERN_INFO "SIS5513: Switching to 5513 register mapping\n");
				}
			}
	}

	if (!chipset_family) { /* Belongs to pci-quirks */

			struct pci_dev *lpc_bridge;
			u16 trueid;
			u8 prefctl;
			u8 idecfg;

			pci_read_config_byte(dev, 0x4a, &idecfg);
			pci_write_config_byte(dev, 0x4a, idecfg | 0x10);
			pci_read_config_word(dev, PCI_DEVICE_ID, &trueid);
			pci_write_config_byte(dev, 0x4a, idecfg);

			if (trueid == 0x5517) { /* SiS 961/961B */

				lpc_bridge = pci_get_slot(dev->bus, 0x10); /* Bus 0, Dev 2, Fn 0 */
				pci_read_config_byte(dev, 0x49, &prefctl);
				pci_dev_put(lpc_bridge);

				if (lpc_bridge->revision == 0x10 && (prefctl & 0x80)) {
					printk(KERN_INFO "SIS5513: SiS 961B MuTIOL IDE UDMA133 controller\n");
					chipset_family = ATA_133a;
				} else {
					printk(KERN_INFO "SIS5513: SiS 961 MuTIOL IDE UDMA100 controller\n");
					chipset_family = ATA_100;
				}
			}
	}

	if (!chipset_family)
		return -1;

	/* Make general config ops here
	   1/ tell IDE channels to operate in Compatibility mode only
	   2/ tell old chips to allow per drive IDE timings */

	{
		u8 reg;
		u16 regw;

		switch(chipset_family) {
			case ATA_133:
				/* SiS962 operation mode */
				pci_read_config_word(dev, 0x50, &regw);
				if (regw & 0x08)
					pci_write_config_word(dev, 0x50, regw&0xfff7);
				pci_read_config_word(dev, 0x52, &regw);
				if (regw & 0x08)
					pci_write_config_word(dev, 0x52, regw&0xfff7);
				break;
			case ATA_133a:
			case ATA_100:
				/* Fixup latency */
				pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x80);
				/* Set compatibility bit */
				pci_read_config_byte(dev, 0x49, &reg);
				if (!(reg & 0x01)) {
					pci_write_config_byte(dev, 0x49, reg|0x01);
				}
				break;
			case ATA_100a:
			case ATA_66:
				/* Fixup latency */
				pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x10);

				/* On ATA_66 chips the bit was elsewhere */
				pci_read_config_byte(dev, 0x52, &reg);
				if (!(reg & 0x04)) {
					pci_write_config_byte(dev, 0x52, reg|0x04);
				}
				break;
			case ATA_33:
				/* On ATA_33 we didn't have a single bit to set */
				pci_read_config_byte(dev, 0x09, &reg);
				if ((reg & 0x0f) != 0x00) {
					pci_write_config_byte(dev, 0x09, reg&0xf0);
				}
			case ATA_16:
				/* force per drive recovery and active timings
				   needed on ATA_33 and below chips */
				pci_read_config_byte(dev, 0x52, &reg);
				if (!(reg & 0x08)) {
					pci_write_config_byte(dev, 0x52, reg|0x08);
				}
				break;
		}
	}

	return 0;
}

struct sis_laptop {
	u16 device;
	u16 subvendor;
	u16 subdevice;
};

static const struct sis_laptop sis_laptop[] = {
	/* devid, subvendor, subdev */
	{ 0x5513, 0x1043, 0x1107 },	/* ASUS A6K */
	{ 0x5513, 0x1734, 0x105f },	/* FSC Amilo A1630 */
	{ 0x5513, 0x1071, 0x8640 },     /* EasyNote K5305 */
	/* end marker */
	{ 0, }
};

static u8 __devinit ata66_sis5513(ide_hwif_t *hwif)
{
	struct pci_dev *pdev = to_pci_dev(hwif->dev);
	const struct sis_laptop *lap = &sis_laptop[0];
	u8 ata66 = 0;

	while (lap->device) {
		if (lap->device == pdev->device &&
		    lap->subvendor == pdev->subsystem_vendor &&
		    lap->subdevice == pdev->subsystem_device)
			return ATA_CBL_PATA40_SHORT;
		lap++;
	}

	if (chipset_family >= ATA_133) {
		u16 regw = 0;
		u16 reg_addr = hwif->channel ? 0x52: 0x50;
		pci_read_config_word(pdev, reg_addr, &regw);
		ata66 = (regw & 0x8000) ? 0 : 1;
	} else if (chipset_family >= ATA_66) {
		u8 reg48h = 0;
		u8 mask = hwif->channel ? 0x20 : 0x10;
		pci_read_config_byte(pdev, 0x48, &reg48h);
		ata66 = (reg48h & mask) ? 0 : 1;
	}

	return ata66 ? ATA_CBL_PATA80 : ATA_CBL_PATA40;
}

static void __devinit init_hwif_sis5513 (ide_hwif_t *hwif)
{
	u8 udma_rates[] = { 0x00, 0x00, 0x07, 0x1f, 0x3f, 0x3f, 0x7f, 0x7f };

	hwif->set_pio_mode = &sis_set_pio_mode;
	hwif->set_dma_mode = &sis_set_dma_mode;

	if (chipset_family >= ATA_133)
		hwif->udma_filter = sis5513_ata133_udma_filter;

	if (hwif->dma_base == 0)
		return;

	hwif->ultra_mask = udma_rates[chipset_family];

	if (hwif->cbl != ATA_CBL_PATA40_SHORT)
		hwif->cbl = ata66_sis5513(hwif);
}

static const struct ide_port_info sis5513_chipset __devinitdata = {
	.name		= "SIS5513",
	.init_chipset	= init_chipset_sis5513,
	.init_hwif	= init_hwif_sis5513,
	.enablebits	= {{0x4a,0x02,0x02}, {0x4a,0x04,0x04}},
	.host_flags	= IDE_HFLAG_LEGACY_IRQS | IDE_HFLAG_NO_AUTODMA |
			  IDE_HFLAG_BOOTABLE,
	.pio_mask	= ATA_PIO4,
	.mwdma_mask	= ATA_MWDMA2,
};

static int __devinit sis5513_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &sis5513_chipset);
}

static const struct pci_device_id sis5513_pci_tbl[] = {
	{ PCI_VDEVICE(SI, PCI_DEVICE_ID_SI_5513), 0 },
	{ PCI_VDEVICE(SI, PCI_DEVICE_ID_SI_5518), 0 },
	{ PCI_VDEVICE(SI, PCI_DEVICE_ID_SI_1180), 0 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, sis5513_pci_tbl);

static struct pci_driver driver = {
	.name		= "SIS_IDE",
	.id_table	= sis5513_pci_tbl,
	.probe		= sis5513_init_one,
};

static int __init sis5513_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(sis5513_ide_init);

MODULE_AUTHOR("Lionel Bouton, L C Chang, Andre Hedrick, Vojtech Pavlik");
MODULE_DESCRIPTION("PCI driver module for SIS IDE");
MODULE_LICENSE("GPL");

/*
 * TODO:
 *	- CLEANUP
 *	- Use drivers/ide/ide-timing.h !
 *	- More checks in the config registers (force values instead of
 *	  relying on the BIOS setting them correctly).
 *	- Further optimisations ?
 *	  . for example ATA66+ regs 0x48 & 0x4A
 */
