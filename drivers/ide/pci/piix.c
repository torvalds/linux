/*
 *  linux/drivers/ide/pci/piix.c	Version 0.47	February 8, 2007
 *
 *  Copyright (C) 1998-1999 Andrzej Krzysztofowicz, Author and Maintainer
 *  Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2003 Red Hat Inc <alan@redhat.com>
 *  Copyright (C) 2006-2007 MontaVista Software, Inc. <source@mvista.com>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  PIO mode setting function for Intel chipsets.
 *  For use instead of BIOS settings.
 *
 * 40-41
 * 42-43
 * 
 *                 41
 *                 43
 *
 * | PIO 0       | c0 | 80 | 0 | 	piix_tune_drive(drive, 0);
 * | PIO 2 | SW2 | d0 | 90 | 4 | 	piix_tune_drive(drive, 2);
 * | PIO 3 | MW1 | e1 | a1 | 9 | 	piix_tune_drive(drive, 3);
 * | PIO 4 | MW2 | e3 | a3 | b | 	piix_tune_drive(drive, 4);
 * 
 * sitre = word40 & 0x4000; primary
 * sitre = word42 & 0x4000; secondary
 *
 * 44 8421|8421    hdd|hdb
 *
 * 48 8421         hdd|hdc|hdb|hda udma enabled
 *
 *    0001         hda
 *    0010         hdb
 *    0100         hdc
 *    1000         hdd
 *
 * 4a 84|21        hdb|hda
 * 4b 84|21        hdd|hdc
 *
 *    ata-33/82371AB
 *    ata-33/82371EB
 *    ata-33/82801AB            ata-66/82801AA
 *    00|00 udma 0              00|00 reserved
 *    01|01 udma 1              01|01 udma 3
 *    10|10 udma 2              10|10 udma 4
 *    11|11 reserved            11|11 reserved
 *
 * 54 8421|8421    ata66 drive|ata66 enable
 *
 * pci_read_config_word(HWIF(drive)->pci_dev, 0x40, &reg40);
 * pci_read_config_word(HWIF(drive)->pci_dev, 0x42, &reg42);
 * pci_read_config_word(HWIF(drive)->pci_dev, 0x44, &reg44);
 * pci_read_config_byte(HWIF(drive)->pci_dev, 0x48, &reg48);
 * pci_read_config_word(HWIF(drive)->pci_dev, 0x4a, &reg4a);
 * pci_read_config_byte(HWIF(drive)->pci_dev, 0x54, &reg54);
 *
 * Documentation
 *	Publically available from Intel web site. Errata documentation
 * is also publically available. As an aide to anyone hacking on this
 * driver the list of errata that are relevant is below.going back to
 * PIIX4. Older device documentation is now a bit tricky to find.
 *
 * Errata of note:
 *
 * Unfixable
 *	PIIX4    errata #9	- Only on ultra obscure hw
 *	ICH3	 errata #13     - Not observed to affect real hw
 *				  by Intel
 *
 * Things we must deal with
 *	PIIX4	errata #10	- BM IDE hang with non UDMA
 *				  (must stop/start dma to recover)
 *	440MX   errata #15	- As PIIX4 errata #10
 *	PIIX4	errata #15	- Must not read control registers
 * 				  during a PIO transfer
 *	440MX   errata #13	- As PIIX4 errata #15
 *	ICH2	errata #21	- DMA mode 0 doesn't work right
 *	ICH0/1  errata #55	- As ICH2 errata #21
 *	ICH2	spec c #9	- Extra operations needed to handle
 *				  drive hotswap [NOT YET SUPPORTED]
 *	ICH2    spec c #20	- IDE PRD must not cross a 64K boundary
 *				  and must be dword aligned
 *	ICH2    spec c #24	- UDMA mode 4,5 t85/86 should be 6ns not 3.3
 *
 * Should have been BIOS fixed:
 *	450NX:	errata #19	- DMA hangs on old 450NX
 *	450NX:  errata #20	- DMA hangs on old 450NX
 *	450NX:  errata #25	- Corruption with DMA on old 450NX
 *	ICH3    errata #15      - IDE deadlock under high load
 *				  (BIOS must set dev 31 fn 0 bit 23)
 *	ICH3	errata #18	- Don't use native mode
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/io.h>

static int no_piix_dma;

/**
 *	piix_dma_2_pio		-	return the PIO mode matching DMA
 *	@xfer_rate: transfer speed
 *
 *	Returns the nearest equivalent PIO timing for the PIO or DMA
 *	mode requested by the controller.
 */
 
static u8 piix_dma_2_pio (u8 xfer_rate) {
	switch(xfer_rate) {
		case XFER_UDMA_6:
		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
		case XFER_MW_DMA_2:
		case XFER_PIO_4:
			return 4;
		case XFER_MW_DMA_1:
		case XFER_PIO_3:
			return 3;
		case XFER_SW_DMA_2:
		case XFER_PIO_2:
			return 2;
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
		case XFER_PIO_1:
		case XFER_PIO_0:
		case XFER_PIO_SLOW:
		default:
			return 0;
	}
}

/**
 *	piix_tune_pio		-	tune PIIX for PIO mode
 *	@drive: drive to tune
 *	@pio: desired PIO mode
 *
 *	Set the interface PIO mode based upon the settings done by AMI BIOS.
 */
static void piix_tune_pio (ide_drive_t *drive, u8 pio)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int is_slave		= drive->dn & 1;
	int master_port		= hwif->channel ? 0x42 : 0x40;
	int slave_port		= 0x44;
	unsigned long flags;
	u16 master_data;
	u8 slave_data;
	static DEFINE_SPINLOCK(tune_lock);
	int control = 0;

				     /* ISP  RTC */
	static const u8 timings[][2]= {
					{ 0, 0 },
					{ 0, 0 },
					{ 1, 0 },
					{ 2, 1 },
					{ 2, 3 }, };

	/*
	 * Master vs slave is synchronized above us but the slave register is
	 * shared by the two hwifs so the corner case of two slave timeouts in
	 * parallel must be locked.
	 */
	spin_lock_irqsave(&tune_lock, flags);
	pci_read_config_word(dev, master_port, &master_data);

	if (pio > 1)
		control |= 1;	/* Programmable timing on */
	if (drive->media == ide_disk)
		control |= 4;	/* Prefetch, post write */
	if (pio > 2)
		control |= 2;	/* IORDY */
	if (is_slave) {
		master_data |=  0x4000;
		master_data &= ~0x0070;
		if (pio > 1) {
			/* Set PPE, IE and TIME */
			master_data |= control << 4;
		}
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data &= hwif->channel ? 0x0f : 0xf0;
		slave_data |= ((timings[pio][0] << 2) | timings[pio][1]) <<
			       (hwif->channel ? 4 : 0);
	} else {
		master_data &= ~0x3307;
		if (pio > 1) {
			/* enable PPE, IE and TIME */
			master_data |= control;
		}
		master_data |= (timings[pio][0] << 12) | (timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);
	spin_unlock_irqrestore(&tune_lock, flags);
}

/**
 *	piix_tune_drive		-	tune a drive attached to PIIX
 *	@drive: drive to tune
 *	@pio: desired PIO mode
 *
 *	Set the drive's PIO mode (might be useful if drive is not registered
 *	in CMOS for any reason).
 */
static void piix_tune_drive (ide_drive_t *drive, u8 pio)
{
	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
	piix_tune_pio(drive, pio);
	(void) ide_config_drive_speed(drive, XFER_PIO_0 + pio);
}

/**
 *	piix_tune_chipset	-	tune a PIIX interface
 *	@drive: IDE drive to tune
 *	@xferspeed: speed to configure
 *
 *	Set a PIIX interface channel to the desired speeds. This involves
 *	requires the right timing data into the PIIX configuration space
 *	then setting the drive parameters appropriately
 */
 
static int piix_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 maslave		= hwif->channel ? 0x42 : 0x40;
	u8 speed		= ide_rate_filter(drive, xferspeed);
	int a_speed		= 3 << (drive->dn * 4);
	int u_flag		= 1 << drive->dn;
	int v_flag		= 0x01 << drive->dn;
	int w_flag		= 0x10 << drive->dn;
	int u_speed		= 0;
	int			sitre;
	u16			reg4042, reg4a;
	u8			reg48, reg54, reg55;

	pci_read_config_word(dev, maslave, &reg4042);
	sitre = (reg4042 & 0x4000) ? 1 : 0;
	pci_read_config_byte(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);
	pci_read_config_byte(dev, 0x54, &reg54);
	pci_read_config_byte(dev, 0x55, &reg55);

	switch(speed) {
		case XFER_UDMA_4:
		case XFER_UDMA_2:	u_speed = 2 << (drive->dn * 4); break;
		case XFER_UDMA_5:
		case XFER_UDMA_3:
		case XFER_UDMA_1:	u_speed = 1 << (drive->dn * 4); break;
		case XFER_UDMA_0:	u_speed = 0 << (drive->dn * 4); break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_SW_DMA_2:	break;
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_0:	break;
		default:		return -1;
	}

	if (speed >= XFER_UDMA_0) {
		if (!(reg48 & u_flag))
			pci_write_config_byte(dev, 0x48, reg48 | u_flag);
		if (speed == XFER_UDMA_5) {
			pci_write_config_byte(dev, 0x55, (u8) reg55|w_flag);
		} else {
			pci_write_config_byte(dev, 0x55, (u8) reg55 & ~w_flag);
		}
		if ((reg4a & a_speed) != u_speed)
			pci_write_config_word(dev, 0x4a, (reg4a & ~a_speed) | u_speed);
		if (speed > XFER_UDMA_2) {
			if (!(reg54 & v_flag))
				pci_write_config_byte(dev, 0x54, reg54 | v_flag);
		} else
			pci_write_config_byte(dev, 0x54, reg54 & ~v_flag);
	} else {
		if (reg48 & u_flag)
			pci_write_config_byte(dev, 0x48, reg48 & ~u_flag);
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		if (reg54 & v_flag)
			pci_write_config_byte(dev, 0x54, reg54 & ~v_flag);
		if (reg55 & w_flag)
			pci_write_config_byte(dev, 0x55, (u8) reg55 & ~w_flag);
	}

	piix_tune_pio(drive, piix_dma_2_pio(speed));
	return ide_config_drive_speed(drive, speed);
}

/**
 *	piix_config_drive_xfer_rate	-	set up an IDE device
 *	@drive: IDE drive to configure
 *
 *	Set up the PIIX interface for the best available speed on this
 *	interface, preferring DMA to PIO.
 */
 
static int piix_config_drive_xfer_rate (ide_drive_t *drive)
{
	drive->init_speed = 0;

	if (ide_tune_dma(drive))
		return 0;

	if (ide_use_fast_pio(drive))
		piix_tune_drive(drive, 255);

	return -1;
}

/**
 *	piix_is_ichx	-	check if ICHx
 *	@dev: PCI device to check
 *
 *	returns 1 if ICHx, 0 otherwise.
 */
static int piix_is_ichx(struct pci_dev *dev)
{
        switch (dev->device) {
		case PCI_DEVICE_ID_INTEL_82801EB_1:
		case PCI_DEVICE_ID_INTEL_82801AA_1:
		case PCI_DEVICE_ID_INTEL_82801AB_1:
		case PCI_DEVICE_ID_INTEL_82801BA_8:
		case PCI_DEVICE_ID_INTEL_82801BA_9:
		case PCI_DEVICE_ID_INTEL_82801CA_10:
		case PCI_DEVICE_ID_INTEL_82801CA_11:
		case PCI_DEVICE_ID_INTEL_82801DB_1:
		case PCI_DEVICE_ID_INTEL_82801DB_10:
		case PCI_DEVICE_ID_INTEL_82801DB_11:
		case PCI_DEVICE_ID_INTEL_82801EB_11:
		case PCI_DEVICE_ID_INTEL_82801E_11:
		case PCI_DEVICE_ID_INTEL_ESB_2:
		case PCI_DEVICE_ID_INTEL_ICH6_19:
		case PCI_DEVICE_ID_INTEL_ICH7_21:
		case PCI_DEVICE_ID_INTEL_ESB2_18:
		case PCI_DEVICE_ID_INTEL_ICH8_6:
			return 1;
	}

	return 0;
}

/**
 *	init_chipset_piix	-	set up the PIIX chipset
 *	@dev: PCI device to set up
 *	@name: Name of the device
 *
 *	Initialize the PCI device as required. For the PIIX this turns
 *	out to be nice and simple
 */

static unsigned int __devinit init_chipset_piix (struct pci_dev *dev, const char *name)
{
	if (piix_is_ichx(dev)) {
		unsigned int extra = 0;
		pci_read_config_dword(dev, 0x54, &extra);
		pci_write_config_dword(dev, 0x54, extra|0x400);
	}

	return 0;
}

/**
 *	piix_dma_clear_irq	-	clear BMDMA status
 *	@drive: IDE drive to clear
 *
 *	Called from ide_intr() for PIO interrupts
 *	to clear BMDMA status as needed by ICHx
 */
static void piix_dma_clear_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	u8 dma_stat;

	/* clear the INTR & ERROR bits */
	dma_stat = hwif->INB(hwif->dma_status);
	/* Should we force the bit as well ? */
	hwif->OUTB(dma_stat, hwif->dma_status);
}

static u8 __devinit piix_cable_detect(ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	u8 reg54h = 0, mask = hwif->channel ? 0xc0 : 0x30;

	pci_read_config_byte(dev, 0x54, &reg54h);

	return (reg54h & mask) ? ATA_CBL_PATA80 : ATA_CBL_PATA40;
}

/**
 *	init_hwif_piix		-	fill in the hwif for the PIIX
 *	@hwif: IDE interface
 *
 *	Set up the ide_hwif_t for the PIIX interface according to the
 *	capabilities of the hardware.
 */

static void __devinit init_hwif_piix(ide_hwif_t *hwif)
{
#ifndef CONFIG_IA64
	if (!hwif->irq)
		hwif->irq = hwif->channel ? 15 : 14;
#endif /* CONFIG_IA64 */

	if (hwif->pci_dev->device == PCI_DEVICE_ID_INTEL_82371MX) {
		/* This is a painful system best to let it self tune for now */
		return;
	}

	hwif->autodma = 0;
	hwif->tuneproc = &piix_tune_drive;
	hwif->speedproc = &piix_tune_chipset;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

	if (!hwif->dma_base)
		return;

	/* ICHx need to clear the bmdma status for all interrupts */
	if (piix_is_ichx(hwif->pci_dev))
		hwif->ide_dma_clear_irq = &piix_dma_clear_irq;

	hwif->atapi_dma = 1;

	hwif->ultra_mask = hwif->cds->udma_mask;
	hwif->mwdma_mask = 0x06;
	hwif->swdma_mask = 0x04;

	if (hwif->ultra_mask & 0x78) {
		if (hwif->cbl != ATA_CBL_PATA40_SHORT)
			hwif->cbl = piix_cable_detect(hwif);
	}

	if (no_piix_dma)
		hwif->ultra_mask = hwif->mwdma_mask = hwif->swdma_mask = 0;

	hwif->ide_dma_check = &piix_config_drive_xfer_rate;
	if (!noautodma)
		hwif->autodma = 1;

	hwif->drives[1].autodma = hwif->autodma;
	hwif->drives[0].autodma = hwif->autodma;
}

#define DECLARE_PIIX_DEV(name_str, udma) \
	{						\
		.name		= name_str,		\
		.init_chipset	= init_chipset_piix,	\
		.init_hwif	= init_hwif_piix,	\
		.channels	= 2,			\
		.autodma	= AUTODMA,		\
		.enablebits	= {{0x41,0x80,0x80}, {0x43,0x80,0x80}}, \
		.bootable	= ON_BOARD,		\
		.udma_mask	= udma,			\
	}

static ide_pci_device_t piix_pci_info[] __devinitdata = {
	/*  0 */ DECLARE_PIIX_DEV("PIIXa", 0x00),	/* no udma */
	/*  1 */ DECLARE_PIIX_DEV("PIIXb", 0x00),	/* no udma */

	/*  2 */
	{	/*
		 * MPIIX actually has only a single IDE channel mapped to
		 * the primary or secondary ports depending on the value
		 * of the bit 14 of the IDETIM register at offset 0x6c
		 */
		.name		= "MPIIX",
		.init_hwif	= init_hwif_piix,
		.channels	= 2,
		.autodma	= NODMA,
		.enablebits	= {{0x6d,0xc0,0x80}, {0x6d,0xc0,0xc0}},
		.bootable	= ON_BOARD,
		.flags		= IDEPCI_FLAG_ISA_PORTS
	},

	/*  3 */ DECLARE_PIIX_DEV("PIIX3", 0x00),	/* no udma */
	/*  4 */ DECLARE_PIIX_DEV("PIIX4", 0x07),	/* udma0-2 */
	/*  5 */ DECLARE_PIIX_DEV("ICH0",  0x07),	/* udma0-2 */
	/*  6 */ DECLARE_PIIX_DEV("PIIX4", 0x07),	/* udma0-2 */
	/*  7 */ DECLARE_PIIX_DEV("ICH",   0x1f),	/* udma0-4 */
	/*  8 */ DECLARE_PIIX_DEV("PIIX4", 0x1f),	/* udma0-4 */
	/*  9 */ DECLARE_PIIX_DEV("PIIX4", 0x07),	/* udma0-2 */
	/* 10 */ DECLARE_PIIX_DEV("ICH2",  0x3f),	/* udma0-5 */
	/* 11 */ DECLARE_PIIX_DEV("ICH2M", 0x3f),	/* udma0-5 */
	/* 12 */ DECLARE_PIIX_DEV("ICH3M", 0x3f),	/* udma0-5 */
	/* 13 */ DECLARE_PIIX_DEV("ICH3",  0x3f),	/* udma0-5 */
	/* 14 */ DECLARE_PIIX_DEV("ICH4",  0x3f),	/* udma0-5 */
	/* 15 */ DECLARE_PIIX_DEV("ICH5",  0x3f),	/* udma0-5 */
	/* 16 */ DECLARE_PIIX_DEV("C-ICH", 0x3f),	/* udma0-5 */
	/* 17 */ DECLARE_PIIX_DEV("ICH4",  0x3f),	/* udma0-5 */
	/* 18 */ DECLARE_PIIX_DEV("ICH5-SATA", 0x3f),	/* udma0-5 */
	/* 19 */ DECLARE_PIIX_DEV("ICH5",  0x3f),	/* udma0-5 */
	/* 20 */ DECLARE_PIIX_DEV("ICH6",  0x3f),	/* udma0-5 */
	/* 21 */ DECLARE_PIIX_DEV("ICH7",  0x3f),	/* udma0-5 */
	/* 22 */ DECLARE_PIIX_DEV("ICH4",  0x3f),	/* udma0-5 */
	/* 23 */ DECLARE_PIIX_DEV("ESB2",  0x3f),	/* udma0-5 */
	/* 24 */ DECLARE_PIIX_DEV("ICH8M", 0x3f),	/* udma0-5 */
};

/**
 *	piix_init_one	-	called when a PIIX is found
 *	@dev: the piix device
 *	@id: the matching pci id
 *
 *	Called when the PCI registration layer (or the IDE initialization)
 *	finds a device matching our IDE device tables.
 */
 
static int __devinit piix_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &piix_pci_info[id->driver_data];

	return ide_setup_pci_device(dev, d);
}

/**
 *	piix_check_450nx	-	Check for problem 450NX setup
 *	
 *	Check for the present of 450NX errata #19 and errata #25. If
 *	they are found, disable use of DMA IDE
 */

static void __devinit piix_check_450nx(void)
{
	struct pci_dev *pdev = NULL;
	u16 cfg;
	u8 rev;
	while((pdev=pci_get_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82454NX, pdev))!=NULL)
	{
		/* Look for 450NX PXB. Check for problem configurations
		   A PCI quirk checks bit 6 already */
		pci_read_config_byte(pdev, PCI_REVISION_ID, &rev);
		pci_read_config_word(pdev, 0x41, &cfg);
		/* Only on the original revision: IDE DMA can hang */
		if(rev == 0x00)
			no_piix_dma = 1;
		/* On all revisions below 5 PXB bus lock must be disabled for IDE */
		else if(cfg & (1<<14) && rev < 5)
			no_piix_dma = 2;
	}
	if(no_piix_dma)
		printk(KERN_WARNING "piix: 450NX errata present, disabling IDE DMA.\n");
	if(no_piix_dma == 2)
		printk(KERN_WARNING "piix: A BIOS update may resolve this.\n");
}		

static struct pci_device_id piix_pci_tbl[] = {
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371FB_0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371FB_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371MX,   PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB,   PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AB_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 5},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82443MX_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 6},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AA_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 7},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82372FB_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 8},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82451NX,   PCI_ANY_ID, PCI_ANY_ID, 0, 0, 9},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 10},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 11},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_10,PCI_ANY_ID, PCI_ANY_ID, 0, 0, 12},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_11,PCI_ANY_ID, PCI_ANY_ID, 0, 0, 13},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_11,PCI_ANY_ID, PCI_ANY_ID, 0, 0, 14},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801EB_11,PCI_ANY_ID, PCI_ANY_ID, 0, 0, 15},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801E_11, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 16},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_10,PCI_ANY_ID, PCI_ANY_ID, 0, 0, 17},
#ifdef CONFIG_BLK_DEV_IDE_SATA
 	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801EB_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 18},
#endif
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB_2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 19},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH6_19, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 20},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_21, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 21},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 22},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB2_18, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 23},
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH8_6, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 24},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, piix_pci_tbl);

static struct pci_driver driver = {
	.name		= "PIIX_IDE",
	.id_table	= piix_pci_tbl,
	.probe		= piix_init_one,
};

static int __init piix_ide_init(void)
{
	piix_check_450nx();
	return ide_pci_register_driver(&driver);
}

module_init(piix_ide_init);

MODULE_AUTHOR("Andre Hedrick, Andrzej Krzysztofowicz");
MODULE_DESCRIPTION("PCI driver module for Intel PIIX IDE");
MODULE_LICENSE("GPL");
