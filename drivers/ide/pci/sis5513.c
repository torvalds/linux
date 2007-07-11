/*
 * linux/drivers/ide/pci/sis5513.c	Version 0.25	Jun 10, 2007
 *
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

#define DISPLAY_SIS_TIMINGS

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

#if defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_IDE_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 sis_proc = 0;

static struct pci_dev *bmide_dev;

static char* cable_type[] = {
	"80 pins",
	"40 pins"
};

static char* recovery_time[] ={
	"12 PCICLK", "1 PCICLK",
	"2 PCICLK", "3 PCICLK",
	"4 PCICLK", "5 PCICLCK",
	"6 PCICLK", "7 PCICLCK",
	"8 PCICLK", "9 PCICLCK",
	"10 PCICLK", "11 PCICLK",
	"13 PCICLK", "14 PCICLK",
	"15 PCICLK", "15 PCICLK"
};

static char* active_time[] = {
	"8 PCICLK", "1 PCICLCK",
	"2 PCICLK", "3 PCICLK",
	"4 PCICLK", "5 PCICLK",
	"6 PCICLK", "12 PCICLK"
};

static char* cycle_time[] = {
	"Reserved", "2 CLK",
	"3 CLK", "4 CLK",
	"5 CLK", "6 CLK",
	"7 CLK", "8 CLK",
	"9 CLK", "10 CLK",
	"11 CLK", "12 CLK",
	"13 CLK", "14 CLK",
	"15 CLK", "16 CLK"
};

/* Generic add master or slave info function */
static char* get_drives_info (char *buffer, u8 pos)
{
	u8 reg00, reg01, reg10, reg11; /* timing registers */
	u32 regdw0, regdw1;
	char* p = buffer;

/* Postwrite/Prefetch */
	if (chipset_family < ATA_133) {
		pci_read_config_byte(bmide_dev, 0x4b, &reg00);
		p += sprintf(p, "Drive %d:        Postwrite %s \t \t Postwrite %s\n",
			     pos, (reg00 & (0x10 << pos)) ? "Enabled" : "Disabled",
			     (reg00 & (0x40 << pos)) ? "Enabled" : "Disabled");
		p += sprintf(p, "                Prefetch  %s \t \t Prefetch  %s\n",
			     (reg00 & (0x01 << pos)) ? "Enabled" : "Disabled",
			     (reg00 & (0x04 << pos)) ? "Enabled" : "Disabled");
		pci_read_config_byte(bmide_dev, 0x40+2*pos, &reg00);
		pci_read_config_byte(bmide_dev, 0x41+2*pos, &reg01);
		pci_read_config_byte(bmide_dev, 0x44+2*pos, &reg10);
		pci_read_config_byte(bmide_dev, 0x45+2*pos, &reg11);
	} else {
		u32 reg54h;
		u8 drive_pci = 0x40;
		pci_read_config_dword(bmide_dev, 0x54, &reg54h);
		if (reg54h & 0x40000000) {
			// Configuration space remapped to 0x70
			drive_pci = 0x70;
		}
		pci_read_config_dword(bmide_dev, (unsigned long)drive_pci+4*pos, &regdw0);
		pci_read_config_dword(bmide_dev, (unsigned long)drive_pci+4*pos+8, &regdw1);

		p += sprintf(p, "Drive %d:\n", pos);
	}


/* UDMA */
	if (chipset_family >= ATA_133) {
		p += sprintf(p, "                UDMA %s \t \t \t UDMA %s\n",
			     (regdw0 & 0x04) ? "Enabled" : "Disabled",
			     (regdw1 & 0x04) ? "Enabled" : "Disabled");
		p += sprintf(p, "                UDMA Cycle Time    %s \t UDMA Cycle Time    %s\n",
			     cycle_time[(regdw0 & 0xF0) >> 4],
			     cycle_time[(regdw1 & 0xF0) >> 4]);
	} else if (chipset_family >= ATA_33) {
		p += sprintf(p, "                UDMA %s \t \t \t UDMA %s\n",
			     (reg01 & 0x80) ? "Enabled" : "Disabled",
			     (reg11 & 0x80) ? "Enabled" : "Disabled");

		p += sprintf(p, "                UDMA Cycle Time    ");
		switch(chipset_family) {
			case ATA_33:	p += sprintf(p, cycle_time[(reg01 & 0x60) >> 5]); break;
			case ATA_66:
			case ATA_100a:	p += sprintf(p, cycle_time[(reg01 & 0x70) >> 4]); break;
			case ATA_100:
			case ATA_133a:	p += sprintf(p, cycle_time[reg01 & 0x0F]); break;
			default:	p += sprintf(p, "?"); break;
		}
		p += sprintf(p, " \t UDMA Cycle Time    ");
		switch(chipset_family) {
			case ATA_33:	p += sprintf(p, cycle_time[(reg11 & 0x60) >> 5]); break;
			case ATA_66:
			case ATA_100a:	p += sprintf(p, cycle_time[(reg11 & 0x70) >> 4]); break;
			case ATA_100:
			case ATA_133a:  p += sprintf(p, cycle_time[reg11 & 0x0F]); break;
			default:	p += sprintf(p, "?"); break;
		}
		p += sprintf(p, "\n");
	}


	if (chipset_family < ATA_133) {	/* else case TODO */

/* Data Active */
		p += sprintf(p, "                Data Active Time   ");
		switch(chipset_family) {
			case ATA_16: /* confirmed */
			case ATA_33:
			case ATA_66:
			case ATA_100a: p += sprintf(p, active_time[reg01 & 0x07]); break;
			case ATA_100:
			case ATA_133a: p += sprintf(p, active_time[(reg00 & 0x70) >> 4]); break;
			default: p += sprintf(p, "?"); break;
		}
		p += sprintf(p, " \t Data Active Time   ");
		switch(chipset_family) {
			case ATA_16:
			case ATA_33:
			case ATA_66:
			case ATA_100a: p += sprintf(p, active_time[reg11 & 0x07]); break;
			case ATA_100:
			case ATA_133a: p += sprintf(p, active_time[(reg10 & 0x70) >> 4]); break;
			default: p += sprintf(p, "?"); break;
		}
		p += sprintf(p, "\n");

/* Data Recovery */
	/* warning: may need (reg&0x07) for pre ATA66 chips */
		p += sprintf(p, "                Data Recovery Time %s \t Data Recovery Time %s\n",
			     recovery_time[reg00 & 0x0f], recovery_time[reg10 & 0x0f]);
	}

	return p;
}

static char* get_masters_info(char* buffer)
{
	return get_drives_info(buffer, 0);
}

static char* get_slaves_info(char* buffer)
{
	return get_drives_info(buffer, 1);
}

/* Main get_info, called on /proc/ide/sis reads */
static int sis_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	int len;
	u8 reg;
	u16 reg2, reg3;

	p += sprintf(p, "\nSiS 5513 ");
	switch(chipset_family) {
		case ATA_16: p += sprintf(p, "DMA 16"); break;
		case ATA_33: p += sprintf(p, "Ultra 33"); break;
		case ATA_66: p += sprintf(p, "Ultra 66"); break;
		case ATA_100a:
		case ATA_100: p += sprintf(p, "Ultra 100"); break;
		case ATA_133a:
		case ATA_133: p += sprintf(p, "Ultra 133"); break;
		default: p+= sprintf(p, "Unknown???"); break;
	}
	p += sprintf(p, " chipset\n");
	p += sprintf(p, "--------------- Primary Channel "
		     "---------------- Secondary Channel "
		     "-------------\n");

/* Status */
	pci_read_config_byte(bmide_dev, 0x4a, &reg);
	if (chipset_family == ATA_133) {
		pci_read_config_word(bmide_dev, 0x50, &reg2);
		pci_read_config_word(bmide_dev, 0x52, &reg3);
	}
	p += sprintf(p, "Channel Status: ");
	if (chipset_family < ATA_66) {
		p += sprintf(p, "%s \t \t \t \t %s\n",
			     (reg & 0x04) ? "On" : "Off",
			     (reg & 0x02) ? "On" : "Off");
	} else if (chipset_family < ATA_133) {
		p += sprintf(p, "%s \t \t \t \t %s \n",
			     (reg & 0x02) ? "On" : "Off",
			     (reg & 0x04) ? "On" : "Off");
	} else { /* ATA_133 */
		p += sprintf(p, "%s \t \t \t \t %s \n",
			     (reg2 & 0x02) ? "On" : "Off",
			     (reg3 & 0x02) ? "On" : "Off");
	}

/* Operation Mode */
	pci_read_config_byte(bmide_dev, 0x09, &reg);
	p += sprintf(p, "Operation Mode: %s \t \t \t %s \n",
		     (reg & 0x01) ? "Native" : "Compatible",
		     (reg & 0x04) ? "Native" : "Compatible");

/* 80-pin cable ? */
	if (chipset_family >= ATA_133) {
		p += sprintf(p, "Cable Type:     %s \t \t \t %s\n",
			     (reg2 & 0x01) ? cable_type[1] : cable_type[0],
			     (reg3 & 0x01) ? cable_type[1] : cable_type[0]);
	} else if (chipset_family > ATA_33) {
		pci_read_config_byte(bmide_dev, 0x48, &reg);
		p += sprintf(p, "Cable Type:     %s \t \t \t %s\n",
			     (reg & 0x10) ? cable_type[1] : cable_type[0],
			     (reg & 0x20) ? cable_type[1] : cable_type[0]);
	}

/* Prefetch Count */
	if (chipset_family < ATA_133) {
		pci_read_config_word(bmide_dev, 0x4c, &reg2);
		pci_read_config_word(bmide_dev, 0x4e, &reg3);
		p += sprintf(p, "Prefetch Count: %d \t \t \t \t %d\n",
			     reg2, reg3);
	}

	p = get_masters_info(p);
	p = get_slaves_info(p);

	len = (p - buffer) - offset;
	*addr = buffer + offset;

	return len > count ? count : len;
}
#endif /* defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_IDE_PROC_FS) */

/*
 * Configuration functions
 */
/* Enables per-drive prefetch and postwrite */
static void config_drive_art_rwp (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	u8 reg4bh		= 0;
	u8 rw_prefetch		= (0x11 << drive->dn);

	if (drive->media != ide_disk)
		return;
	pci_read_config_byte(dev, 0x4b, &reg4bh);

	if ((reg4bh & rw_prefetch) != rw_prefetch)
		pci_write_config_byte(dev, 0x4b, reg4bh|rw_prefetch);
}

/* Set per-drive active and recovery time */
static void config_art_rwp_pio (ide_drive_t *drive, u8 pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	u8 drive_pci, test1, test2;

	config_drive_art_rwp(drive);

	/* In pre ATA_133 case, drives sit at 0x40 + 4*drive->dn */
	drive_pci = 0x40;
	/* In SiS962 case drives sit at (0x40 or 0x70) + 8*drive->dn) */
	if (chipset_family >= ATA_133) {
		u32 reg54h;
		pci_read_config_dword(dev, 0x54, &reg54h);
		if (reg54h & 0x40000000) drive_pci = 0x70;
		drive_pci += ((drive->dn)*0x4);
	} else {
		drive_pci += ((drive->dn)*0x2);
	}

	/* register layout changed with newer ATA100 chips */
	if (chipset_family < ATA_100) {
		pci_read_config_byte(dev, drive_pci, &test1);
		pci_read_config_byte(dev, drive_pci+1, &test2);

		/* Clear active and recovery timings */
		test1 &= ~0x0F;
		test2 &= ~0x07;

		switch(pio) {
			case 4:		test1 |= 0x01; test2 |= 0x03; break;
			case 3:		test1 |= 0x03; test2 |= 0x03; break;
			case 2:		test1 |= 0x04; test2 |= 0x04; break;
			case 1:		test1 |= 0x07; test2 |= 0x06; break;
			case 0:		/* PIO0: register setting == X000 */
			default:	break;
		}
		pci_write_config_byte(dev, drive_pci, test1);
		pci_write_config_byte(dev, drive_pci+1, test2);
	} else if (chipset_family < ATA_133) {
		switch(pio) { /*		active  recovery
						  v     v */
			case 4:		test1 = 0x30|0x01; break;
			case 3:		test1 = 0x30|0x03; break;
			case 2:		test1 = 0x40|0x04; break;
			case 1:		test1 = 0x60|0x07; break;
			case 0:		test1 = 0x00; break;
			default:	break;
		}
		pci_write_config_byte(dev, drive_pci, test1);
	} else { /* ATA_133 */
		u32 test3;
		pci_read_config_dword(dev, drive_pci, &test3);
		test3 &= 0xc0c00fff;
		if (test3 & 0x08) {
			test3 |= ini_time_value[ATA_133][pio] << 12;
			test3 |= act_time_value[ATA_133][pio] << 16;
			test3 |= rco_time_value[ATA_133][pio] << 24;
		} else {
			test3 |= ini_time_value[ATA_100][pio] << 12;
			test3 |= act_time_value[ATA_100][pio] << 16;
			test3 |= rco_time_value[ATA_100][pio] << 24;
		}
		pci_write_config_dword(dev, drive_pci, test3);
	}
}

static int sis5513_tune_drive(ide_drive_t *drive, u8 pio)
{
	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
	config_art_rwp_pio(drive, pio);
	return ide_config_drive_speed(drive, XFER_PIO_0 + pio);
}

static void sis5513_tuneproc(ide_drive_t *drive, u8 pio)
{
	(void)sis5513_tune_drive(drive, pio);
}

static int sis5513_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	u8 drive_pci, reg, speed;
	u32 regdw;

	speed = ide_rate_filter(drive, xferspeed);

	/* See config_art_rwp_pio for drive pci config registers */
	drive_pci = 0x40;
	if (chipset_family >= ATA_133) {
		u32 reg54h;
		pci_read_config_dword(dev, 0x54, &reg54h);
		if (reg54h & 0x40000000) drive_pci = 0x70;
		drive_pci += ((drive->dn)*0x4);
		pci_read_config_dword(dev, (unsigned long)drive_pci, &regdw);
		/* Disable UDMA bit for non UDMA modes on UDMA chips */
		if (speed < XFER_UDMA_0) {
			regdw &= 0xfffffffb;
			pci_write_config_dword(dev, (unsigned long)drive_pci, regdw);
		}
	
	} else {
		drive_pci += ((drive->dn)*0x2);
		pci_read_config_byte(dev, drive_pci+1, &reg);
		/* Disable UDMA bit for non UDMA modes on UDMA chips */
		if ((speed < XFER_UDMA_0) && (chipset_family > ATA_16)) {
			reg &= 0x7F;
			pci_write_config_byte(dev, drive_pci+1, reg);
		}
	}

	/* Config chip for mode */
	switch(speed) {
		case XFER_UDMA_6:
		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			if (chipset_family >= ATA_133) {
				regdw |= 0x04;
				regdw &= 0xfffff00f;
				/* check if ATA133 enable */
				if (regdw & 0x08) {
					regdw |= (unsigned long)cycle_time_value[ATA_133][speed-XFER_UDMA_0] << 4;
					regdw |= (unsigned long)cvs_time_value[ATA_133][speed-XFER_UDMA_0] << 8;
				} else {
				/* if ATA133 disable, we should not set speed above UDMA5 */
					if (speed > XFER_UDMA_5)
						speed = XFER_UDMA_5;
					regdw |= (unsigned long)cycle_time_value[ATA_100][speed-XFER_UDMA_0] << 4;
					regdw |= (unsigned long)cvs_time_value[ATA_100][speed-XFER_UDMA_0] << 8;
				}
				pci_write_config_dword(dev, (unsigned long)drive_pci, regdw);
			} else {
				/* Force the UDMA bit on if we want to use UDMA */
				reg |= 0x80;
				/* clean reg cycle time bits */
				reg &= ~((0xFF >> (8 - cycle_time_range[chipset_family]))
					 << cycle_time_offset[chipset_family]);
				/* set reg cycle time bits */
				reg |= cycle_time_value[chipset_family][speed-XFER_UDMA_0]
					<< cycle_time_offset[chipset_family];
				pci_write_config_byte(dev, drive_pci+1, reg);
			}
			break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_2:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
			break;
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			return sis5513_tune_drive(drive, speed - XFER_PIO_0);
		default:
			BUG();
			break;
	}

	return ide_config_drive_speed(drive, speed);
}

static int sis5513_config_xfer_rate(ide_drive_t *drive)
{
	/*
	 * TODO: always set PIO mode and remove this
	 */
	sis5513_tuneproc(drive, 255);

	drive->init_speed = 0;

	if (ide_tune_dma(drive))
		return 0;

	if (ide_use_fast_pio(drive))
		sis5513_tuneproc(drive, 255);

	return -1;
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
			u8 hostrev;
			pci_read_config_byte(host, PCI_REVISION_ID, &hostrev);
			if (hostrev >= 0x30)
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
			u8 sbrev;

			pci_read_config_byte(dev, 0x4a, &idecfg);
			pci_write_config_byte(dev, 0x4a, idecfg | 0x10);
			pci_read_config_word(dev, PCI_DEVICE_ID, &trueid);
			pci_write_config_byte(dev, 0x4a, idecfg);

			if (trueid == 0x5517) { /* SiS 961/961B */

				lpc_bridge = pci_get_slot(dev->bus, 0x10); /* Bus 0, Dev 2, Fn 0 */
				pci_read_config_byte(lpc_bridge, PCI_REVISION_ID, &sbrev);
				pci_read_config_byte(dev, 0x49, &prefctl);
				pci_dev_put(lpc_bridge);

				if (sbrev == 0x10 && (prefctl & 0x80)) {
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

#if defined(DISPLAY_SIS_TIMINGS) && defined(CONFIG_IDE_PROC_FS)
		if (!sis_proc) {
			sis_proc = 1;
			bmide_dev = dev;
			ide_pci_create_host_proc("sis", sis_get_info);
		}
#endif
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
	/* end marker */
	{ 0, }
};

static u8 __devinit ata66_sis5513(ide_hwif_t *hwif)
{
	struct pci_dev *pdev = hwif->pci_dev;
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
		pci_read_config_word(hwif->pci_dev, reg_addr, &regw);
		ata66 = (regw & 0x8000) ? 0 : 1;
	} else if (chipset_family >= ATA_66) {
		u8 reg48h = 0;
		u8 mask = hwif->channel ? 0x20 : 0x10;
		pci_read_config_byte(hwif->pci_dev, 0x48, &reg48h);
		ata66 = (reg48h & mask) ? 0 : 1;
	}

	return ata66 ? ATA_CBL_PATA80 : ATA_CBL_PATA40;
}

static void __devinit init_hwif_sis5513 (ide_hwif_t *hwif)
{
	u8 udma_rates[] = { 0x00, 0x00, 0x07, 0x1f, 0x3f, 0x3f, 0x7f, 0x7f };

	hwif->autodma = 0;

	if (!hwif->irq)
		hwif->irq = hwif->channel ? 15 : 14;

	hwif->tuneproc = &sis5513_tuneproc;
	hwif->speedproc = &sis5513_tune_chipset;

	if (!(hwif->dma_base)) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->atapi_dma = 1;

	hwif->ultra_mask = udma_rates[chipset_family];
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

	if (!chipset_family)
		return;

	if (hwif->cbl != ATA_CBL_PATA40_SHORT)
		hwif->cbl = ata66_sis5513(hwif);

	if (chipset_family > ATA_16) {
		hwif->ide_dma_check = &sis5513_config_xfer_rate;
		if (!noautodma)
			hwif->autodma = 1;
	}
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
	return;
}

static ide_pci_device_t sis5513_chipset __devinitdata = {
	.name		= "SIS5513",
	.init_chipset	= init_chipset_sis5513,
	.init_hwif	= init_hwif_sis5513,
	.channels	= 2,
	.autodma	= NOAUTODMA,
	.enablebits	= {{0x4a,0x02,0x02}, {0x4a,0x04,0x04}},
	.bootable	= ON_BOARD,
};

static int __devinit sis5513_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &sis5513_chipset);
}

static struct pci_device_id sis5513_pci_tbl[] = {
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_5513, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_5518, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_1180, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
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
