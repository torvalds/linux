/*
 * linux/drivers/ide/pci/siimage.c		Version 1.19	Nov 16 2007
 *
 * Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2003		Red Hat <alan@redhat.com>
 * Copyright (C) 2007		MontaVista Software, Inc.
 * Copyright (C) 2007		Bartlomiej Zolnierkiewicz
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  Documentation for CMD680:
 *  http://gkernel.sourceforge.net/specs/sii/sii-0680a-v1.31.pdf.bz2
 *
 *  Documentation for SiI 3112:
 *  http://gkernel.sourceforge.net/specs/sii/3112A_SiI-DS-0095-B2.pdf.bz2
 *
 *  Errata and other documentation only available under NDA.
 *
 *
 *  FAQ Items:
 *	If you are using Marvell SATA-IDE adapters with Maxtor drives
 *	ensure the system is set up for ATA100/UDMA5 not UDMA6.
 *
 *	If you are using WD drives with SATA bridges you must set the
 *	drive to "Single". "Master" will hang
 *
 *	If you have strange problems with nVidia chipset systems please
 *	see the SI support documentation and update your system BIOS
 *	if necessary
 *
 *  The Dell DRAC4 has some interesting features including effectively hot
 *  unplugging/replugging the virtual CD interface when the DRAC is reset.
 *  This often causes drivers/ide/siimage to panic but is ok with the rather
 *  smarter code in libata.
 *
 * TODO:
 * - IORDY fixes
 * - VDMA support
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

/**
 *	pdev_is_sata		-	check if device is SATA
 *	@pdev:	PCI device to check
 *	
 *	Returns true if this is a SATA controller
 */
 
static int pdev_is_sata(struct pci_dev *pdev)
{
#ifdef CONFIG_BLK_DEV_IDE_SATA
	switch(pdev->device) {
		case PCI_DEVICE_ID_SII_3112:
		case PCI_DEVICE_ID_SII_1210SA:
			return 1;
		case PCI_DEVICE_ID_SII_680:
			return 0;
	}
	BUG();
#endif
	return 0;
}

/**
 *	is_sata			-	check if hwif is SATA
 *	@hwif:	interface to check
 *	
 *	Returns true if this is a SATA controller
 */
 
static inline int is_sata(ide_hwif_t *hwif)
{
	return pdev_is_sata(hwif->pci_dev);
}

/**
 *	siimage_selreg		-	return register base
 *	@hwif: interface
 *	@r: config offset
 *
 *	Turn a config register offset into the right address in either
 *	PCI space or MMIO space to access the control register in question
 *	Thankfully this is a configuration operation so isnt performance
 *	criticial. 
 */
 
static unsigned long siimage_selreg(ide_hwif_t *hwif, int r)
{
	unsigned long base = (unsigned long)hwif->hwif_data;
	base += 0xA0 + r;
	if(hwif->mmio)
		base += (hwif->channel << 6);
	else
		base += (hwif->channel << 4);
	return base;
}
	
/**
 *	siimage_seldev		-	return register base
 *	@hwif: interface
 *	@r: config offset
 *
 *	Turn a config register offset into the right address in either
 *	PCI space or MMIO space to access the control register in question
 *	including accounting for the unit shift.
 */
 
static inline unsigned long siimage_seldev(ide_drive_t *drive, int r)
{
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned long base = (unsigned long)hwif->hwif_data;
	base += 0xA0 + r;
	if(hwif->mmio)
		base += (hwif->channel << 6);
	else
		base += (hwif->channel << 4);
	base |= drive->select.b.unit << drive->select.b.unit;
	return base;
}

/**
 *	sil_udma_filter		-	compute UDMA mask
 *	@drive: IDE device
 *
 *	Compute the available UDMA speeds for the device on the interface.
 *
 *	For the CMD680 this depends on the clocking mode (scsc), for the
 *	SI3112 SATA controller life is a bit simpler.
 */

static u8 sil_pata_udma_filter(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	unsigned long base = (unsigned long) hwif->hwif_data;
	u8 mask = 0, scsc = 0;

	if (hwif->mmio)
		scsc = hwif->INB(base + 0x4A);
	else
		pci_read_config_byte(hwif->pci_dev, 0x8A, &scsc);

	if ((scsc & 0x30) == 0x10)	/* 133 */
		mask = ATA_UDMA6;
	else if ((scsc & 0x30) == 0x20)	/* 2xPCI */
		mask = ATA_UDMA6;
	else if ((scsc & 0x30) == 0x00)	/* 100 */
		mask = ATA_UDMA5;
	else 	/* Disabled ? */
		BUG();

	return mask;
}

static u8 sil_sata_udma_filter(ide_drive_t *drive)
{
	return strstr(drive->id->model, "Maxtor") ? ATA_UDMA5 : ATA_UDMA6;
}

/**
 *	sil_set_pio_mode	-	set host controller for PIO mode
 *	@drive: drive
 *	@pio: PIO mode number
 *
 *	Load the timing settings for this device mode into the
 *	controller. If we are in PIO mode 3 or 4 turn on IORDY
 *	monitoring (bit 9). The TF timing is bits 31:16
 */

static void sil_set_pio_mode(ide_drive_t *drive, u8 pio)
{
	const u16 tf_speed[]	= { 0x328a, 0x2283, 0x1281, 0x10c3, 0x10c1 };
	const u16 data_speed[]	= { 0x328a, 0x2283, 0x1104, 0x10c3, 0x10c1 };

	ide_hwif_t *hwif	= HWIF(drive);
	ide_drive_t *pair	= ide_get_paired_drive(drive);
	u32 speedt		= 0;
	u16 speedp		= 0;
	unsigned long addr	= siimage_seldev(drive, 0x04);
	unsigned long tfaddr	= siimage_selreg(hwif, 0x02);
	unsigned long base	= (unsigned long)hwif->hwif_data;
	u8 tf_pio		= pio;
	u8 addr_mask		= hwif->channel ? (hwif->mmio ? 0xF4 : 0x84)
						: (hwif->mmio ? 0xB4 : 0x80);
	u8 mode			= 0;
	u8 unit			= drive->select.b.unit;

	/* trim *taskfile* PIO to the slowest of the master/slave */
	if (pair->present) {
		u8 pair_pio = ide_get_best_pio_mode(pair, 255, 4);

		if (pair_pio < tf_pio)
			tf_pio = pair_pio;
	}

	/* cheat for now and use the docs */
	speedp = data_speed[pio];
	speedt = tf_speed[tf_pio];

	if (hwif->mmio) {
		hwif->OUTW(speedp, addr);
		hwif->OUTW(speedt, tfaddr);
		/* Now set up IORDY */
		if (pio > 2)
			hwif->OUTW(hwif->INW(tfaddr-2)|0x200, tfaddr-2);
		else
			hwif->OUTW(hwif->INW(tfaddr-2)&~0x200, tfaddr-2);

		mode = hwif->INB(base + addr_mask);
		mode &= ~(unit ? 0x30 : 0x03);
		mode |= (unit ? 0x10 : 0x01);
		hwif->OUTB(mode, base + addr_mask);
	} else {
		pci_write_config_word(hwif->pci_dev, addr, speedp);
		pci_write_config_word(hwif->pci_dev, tfaddr, speedt);
		pci_read_config_word(hwif->pci_dev, tfaddr-2, &speedp);
		speedp &= ~0x200;
		/* Set IORDY for mode 3 or 4 */
		if (pio > 2)
			speedp |= 0x200;
		pci_write_config_word(hwif->pci_dev, tfaddr-2, speedp);

		pci_read_config_byte(hwif->pci_dev, addr_mask, &mode);
		mode &= ~(unit ? 0x30 : 0x03);
		mode |= (unit ? 0x10 : 0x01);
		pci_write_config_byte(hwif->pci_dev, addr_mask, mode);
	}
}

/**
 *	sil_set_dma_mode	-	set host controller for DMA mode
 *	@drive: drive
 *	@speed: DMA mode
 *
 *	Tune the SiI chipset for the desired DMA mode.
 */

static void sil_set_dma_mode(ide_drive_t *drive, const u8 speed)
{
	u8 ultra6[]		= { 0x0F, 0x0B, 0x07, 0x05, 0x03, 0x02, 0x01 };
	u8 ultra5[]		= { 0x0C, 0x07, 0x05, 0x04, 0x02, 0x01 };
	u16 dma[]		= { 0x2208, 0x10C2, 0x10C1 };

	ide_hwif_t *hwif	= HWIF(drive);
	u16 ultra = 0, multi	= 0;
	u8 mode = 0, unit	= drive->select.b.unit;
	unsigned long base	= (unsigned long)hwif->hwif_data;
	u8 scsc = 0, addr_mask	= ((hwif->channel) ?
				    ((hwif->mmio) ? 0xF4 : 0x84) :
				    ((hwif->mmio) ? 0xB4 : 0x80));
				    
	unsigned long ma	= siimage_seldev(drive, 0x08);
	unsigned long ua	= siimage_seldev(drive, 0x0C);

	if (hwif->mmio) {
		scsc = hwif->INB(base + 0x4A);
		mode = hwif->INB(base + addr_mask);
		multi = hwif->INW(ma);
		ultra = hwif->INW(ua);
	} else {
		pci_read_config_byte(hwif->pci_dev, 0x8A, &scsc);
		pci_read_config_byte(hwif->pci_dev, addr_mask, &mode);
		pci_read_config_word(hwif->pci_dev, ma, &multi);
		pci_read_config_word(hwif->pci_dev, ua, &ultra);
	}

	mode &= ~((unit) ? 0x30 : 0x03);
	ultra &= ~0x3F;
	scsc = ((scsc & 0x30) == 0x00) ? 0 : 1;

	scsc = is_sata(hwif) ? 1 : scsc;

	if (speed >= XFER_UDMA_0) {
		multi = dma[2];
		ultra |= (scsc ? ultra6[speed - XFER_UDMA_0] :
				 ultra5[speed - XFER_UDMA_0]);
		mode |= (unit ? 0x30 : 0x03);
	} else {
		multi = dma[speed - XFER_MW_DMA_0];
		mode |= (unit ? 0x20 : 0x02);
	}

	if (hwif->mmio) {
		hwif->OUTB(mode, base + addr_mask);
		hwif->OUTW(multi, ma);
		hwif->OUTW(ultra, ua);
	} else {
		pci_write_config_byte(hwif->pci_dev, addr_mask, mode);
		pci_write_config_word(hwif->pci_dev, ma, multi);
		pci_write_config_word(hwif->pci_dev, ua, ultra);
	}
}

/* returns 1 if dma irq issued, 0 otherwise */
static int siimage_io_ide_dma_test_irq (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 dma_altstat		= 0;
	unsigned long addr	= siimage_selreg(hwif, 1);

	/* return 1 if INTR asserted */
	if ((hwif->INB(hwif->dma_status) & 4) == 4)
		return 1;

	/* return 1 if Device INTR asserted */
	pci_read_config_byte(hwif->pci_dev, addr, &dma_altstat);
	if (dma_altstat & 8)
		return 0;	//return 1;
	return 0;
}

/**
 *	siimage_mmio_ide_dma_test_irq	-	check we caused an IRQ
 *	@drive: drive we are testing
 *
 *	Check if we caused an IDE DMA interrupt. We may also have caused
 *	SATA status interrupts, if so we clean them up and continue.
 */
 
static int siimage_mmio_ide_dma_test_irq (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	unsigned long addr	= siimage_selreg(hwif, 0x1);

	if (SATA_ERROR_REG) {
		unsigned long base = (unsigned long)hwif->hwif_data;

		u32 ext_stat = readl((void __iomem *)(base + 0x10));
		u8 watchdog = 0;
		if (ext_stat & ((hwif->channel) ? 0x40 : 0x10)) {
			u32 sata_error = readl((void __iomem *)SATA_ERROR_REG);
			writel(sata_error, (void __iomem *)SATA_ERROR_REG);
			watchdog = (sata_error & 0x00680000) ? 1 : 0;
			printk(KERN_WARNING "%s: sata_error = 0x%08x, "
				"watchdog = %d, %s\n",
				drive->name, sata_error, watchdog,
				__FUNCTION__);

		} else {
			watchdog = (ext_stat & 0x8000) ? 1 : 0;
		}
		ext_stat >>= 16;

		if (!(ext_stat & 0x0404) && !watchdog)
			return 0;
	}

	/* return 1 if INTR asserted */
	if ((readb((void __iomem *)hwif->dma_status) & 0x04) == 0x04)
		return 1;

	/* return 1 if Device INTR asserted */
	if ((readb((void __iomem *)addr) & 8) == 8)
		return 0;	//return 1;

	return 0;
}

/**
 *	sil_sata_busproc	-	bus isolation IOCTL
 *	@drive: drive to isolate/restore
 *	@state: bus state to set
 *
 *	Used by the SII3112 to handle bus isolation. As this is a 
 *	SATA controller the work required is quite limited, we 
 *	just have to clean up the statistics
 */

static int sil_sata_busproc(ide_drive_t * drive, int state)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u32 stat_config		= 0;
	unsigned long addr	= siimage_selreg(hwif, 0);

	if (hwif->mmio)
		stat_config = readl((void __iomem *)addr);
	else
		pci_read_config_dword(hwif->pci_dev, addr, &stat_config);

	switch (state) {
		case BUSSTATE_ON:
			hwif->drives[0].failures = 0;
			hwif->drives[1].failures = 0;
			break;
		case BUSSTATE_OFF:
			hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
			hwif->drives[1].failures = hwif->drives[1].max_failures + 1;
			break;
		case BUSSTATE_TRISTATE:
			hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
			hwif->drives[1].failures = hwif->drives[1].max_failures + 1;
			break;
		default:
			return -EINVAL;
	}
	hwif->bus_state = state;
	return 0;
}

/**
 *	sil_sata_reset_poll	-	wait for SATA reset
 *	@drive: drive we are resetting
 *
 *	Poll the SATA phy and see whether it has come back from the dead
 *	yet.
 */

static int sil_sata_reset_poll(ide_drive_t *drive)
{
	if (SATA_STATUS_REG) {
		ide_hwif_t *hwif	= HWIF(drive);

		/* SATA_STATUS_REG is valid only when in MMIO mode */
		if ((readl((void __iomem *)SATA_STATUS_REG) & 0x03) != 0x03) {
			printk(KERN_WARNING "%s: reset phy dead, status=0x%08x\n",
				hwif->name, readl((void __iomem *)SATA_STATUS_REG));
			HWGROUP(drive)->polling = 0;
			return ide_started;
		}
	}

	return 0;
}

/**
 *	sil_sata_pre_reset	-	reset hook
 *	@drive: IDE device being reset
 *
 *	For the SATA devices we need to handle recalibration/geometry
 *	differently
 */

static void sil_sata_pre_reset(ide_drive_t *drive)
{
	if (drive->media == ide_disk) {
		drive->special.b.set_geometry = 0;
		drive->special.b.recalibrate = 0;
	}
}

/**
 *	proc_reports_siimage		-	add siimage controller to proc
 *	@dev: PCI device
 *	@clocking: SCSC value
 *	@name: controller name
 *
 *	Report the clocking mode of the controller and add it to
 *	the /proc interface layer
 */
 
static void proc_reports_siimage (struct pci_dev *dev, u8 clocking, const char *name)
{
	if (!pdev_is_sata(dev)) {
		printk(KERN_INFO "%s: BASE CLOCK ", name);
		clocking &= 0x03;
		switch (clocking) {
			case 0x03: printk("DISABLED!\n"); break;
			case 0x02: printk("== 2X PCI\n"); break;
			case 0x01: printk("== 133\n"); break;
			case 0x00: printk("== 100\n"); break;
		}
	}
}

/**
 *	setup_mmio_siimage	-	switch an SI controller into MMIO
 *	@dev: PCI device we are configuring
 *	@name: device name
 *
 *	Attempt to put the device into mmio mode. There are some slight
 *	complications here with certain systems where the mmio bar isnt
 *	mapped so we have to be sure we can fall back to I/O.
 */
 
static unsigned int setup_mmio_siimage (struct pci_dev *dev, const char *name)
{
	unsigned long bar5	= pci_resource_start(dev, 5);
	unsigned long barsize	= pci_resource_len(dev, 5);
	u8 tmpbyte	= 0;
	void __iomem *ioaddr;
	u32 tmp, irq_mask;

	/*
	 *	Drop back to PIO if we can't map the mmio. Some
	 *	systems seem to get terminally confused in the PCI
	 *	spaces.
	 */
	 
	if(!request_mem_region(bar5, barsize, name))
	{
		printk(KERN_WARNING "siimage: IDE controller MMIO ports not available.\n");
		return 0;
	}
		
	ioaddr = ioremap(bar5, barsize);

	if (ioaddr == NULL)
	{
		release_mem_region(bar5, barsize);
		return 0;
	}

	pci_set_master(dev);
	pci_set_drvdata(dev, (void *) ioaddr);

	if (pdev_is_sata(dev)) {
		/* make sure IDE0/1 interrupts are not masked */
		irq_mask = (1 << 22) | (1 << 23);
		tmp = readl(ioaddr + 0x48);
		if (tmp & irq_mask) {
			tmp &= ~irq_mask;
			writel(tmp, ioaddr + 0x48);
			readl(ioaddr + 0x48); /* flush */
		}
		writel(0, ioaddr + 0x148);
		writel(0, ioaddr + 0x1C8);
	}

	writeb(0, ioaddr + 0xB4);
	writeb(0, ioaddr + 0xF4);
	tmpbyte = readb(ioaddr + 0x4A);

	switch(tmpbyte & 0x30) {
		case 0x00:
			/* In 100 MHz clocking, try and switch to 133 */
			writeb(tmpbyte|0x10, ioaddr + 0x4A);
			break;
		case 0x10:
			/* On 133Mhz clocking */
			break;
		case 0x20:
			/* On PCIx2 clocking */
			break;
		case 0x30:
			/* Clocking is disabled */
			/* 133 clock attempt to force it on */
			writeb(tmpbyte & ~0x20, ioaddr + 0x4A);
			break;
	}
	
	writeb(      0x72, ioaddr + 0xA1);
	writew(    0x328A, ioaddr + 0xA2);
	writel(0x62DD62DD, ioaddr + 0xA4);
	writel(0x43924392, ioaddr + 0xA8);
	writel(0x40094009, ioaddr + 0xAC);
	writeb(      0x72, ioaddr + 0xE1);
	writew(    0x328A, ioaddr + 0xE2);
	writel(0x62DD62DD, ioaddr + 0xE4);
	writel(0x43924392, ioaddr + 0xE8);
	writel(0x40094009, ioaddr + 0xEC);

	if (pdev_is_sata(dev)) {
		writel(0xFFFF0000, ioaddr + 0x108);
		writel(0xFFFF0000, ioaddr + 0x188);
		writel(0x00680000, ioaddr + 0x148);
		writel(0x00680000, ioaddr + 0x1C8);
	}

	tmpbyte = readb(ioaddr + 0x4A);

	proc_reports_siimage(dev, (tmpbyte>>4), name);
	return 1;
}

/**
 *	init_chipset_siimage	-	set up an SI device
 *	@dev: PCI device
 *	@name: device name
 *
 *	Perform the initial PCI set up for this device. Attempt to switch
 *	to 133MHz clocking if the system isn't already set up to do it.
 */

static unsigned int __devinit init_chipset_siimage(struct pci_dev *dev, const char *name)
{
	u8 rev = dev->revision, tmpbyte = 0, BA5_EN = 0;

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, rev ? 1 : 255);

	pci_read_config_byte(dev, 0x8A, &BA5_EN);
	if ((BA5_EN & 0x01) || (pci_resource_start(dev, 5))) {
		if (setup_mmio_siimage(dev, name)) {
			return 0;
		}
	}

	pci_write_config_byte(dev, 0x80, 0x00);
	pci_write_config_byte(dev, 0x84, 0x00);
	pci_read_config_byte(dev, 0x8A, &tmpbyte);
	switch(tmpbyte & 0x30) {
		case 0x00:
			/* 133 clock attempt to force it on */
			pci_write_config_byte(dev, 0x8A, tmpbyte|0x10);
		case 0x30:
			/* if clocking is disabled */
			/* 133 clock attempt to force it on */
			pci_write_config_byte(dev, 0x8A, tmpbyte & ~0x20);
		case 0x10:
			/* 133 already */
			break;
		case 0x20:
			/* BIOS set PCI x2 clocking */
			break;
	}

	pci_read_config_byte(dev,   0x8A, &tmpbyte);

	pci_write_config_byte(dev,  0xA1, 0x72);
	pci_write_config_word(dev,  0xA2, 0x328A);
	pci_write_config_dword(dev, 0xA4, 0x62DD62DD);
	pci_write_config_dword(dev, 0xA8, 0x43924392);
	pci_write_config_dword(dev, 0xAC, 0x40094009);
	pci_write_config_byte(dev,  0xB1, 0x72);
	pci_write_config_word(dev,  0xB2, 0x328A);
	pci_write_config_dword(dev, 0xB4, 0x62DD62DD);
	pci_write_config_dword(dev, 0xB8, 0x43924392);
	pci_write_config_dword(dev, 0xBC, 0x40094009);

	proc_reports_siimage(dev, (tmpbyte>>4), name);
	return 0;
}

/**
 *	init_mmio_iops_siimage	-	set up the iops for MMIO
 *	@hwif: interface to set up
 *
 *	The basic setup here is fairly simple, we can use standard MMIO
 *	operations. However we do have to set the taskfile register offsets
 *	by hand as there isnt a standard defined layout for them this
 *	time.
 *
 *	The hardware supports buffered taskfiles and also some rather nice
 *	extended PRD tables. For better SI3112 support use the libata driver
 */

static void __devinit init_mmio_iops_siimage(ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	void *addr		= pci_get_drvdata(dev);
	u8 ch			= hwif->channel;
	hw_regs_t		hw;
	unsigned long		base;

	/*
	 *	Fill in the basic HWIF bits
	 */

	default_hwif_mmiops(hwif);
	hwif->hwif_data			= addr;

	/*
	 *	Now set up the hw. We have to do this ourselves as
	 *	the MMIO layout isnt the same as the standard port
	 *	based I/O
	 */

	memset(&hw, 0, sizeof(hw_regs_t));

	base = (unsigned long)addr;
	if (ch)
		base += 0xC0;
	else
		base += 0x80;

	/*
	 *	The buffered task file doesn't have status/control
	 *	so we can't currently use it sanely since we want to
	 *	use LBA48 mode.
	 */	
	hw.io_ports[IDE_DATA_OFFSET]	= base;
	hw.io_ports[IDE_ERROR_OFFSET]	= base + 1;
	hw.io_ports[IDE_NSECTOR_OFFSET]	= base + 2;
	hw.io_ports[IDE_SECTOR_OFFSET]	= base + 3;
	hw.io_ports[IDE_LCYL_OFFSET]	= base + 4;
	hw.io_ports[IDE_HCYL_OFFSET]	= base + 5;
	hw.io_ports[IDE_SELECT_OFFSET]	= base + 6;
	hw.io_ports[IDE_STATUS_OFFSET]	= base + 7;
	hw.io_ports[IDE_CONTROL_OFFSET]	= base + 10;

	hw.io_ports[IDE_IRQ_OFFSET]	= 0;

	if (pdev_is_sata(dev)) {
		base = (unsigned long)addr;
		if (ch)
			base += 0x80;
		hwif->sata_scr[SATA_STATUS_OFFSET]	= base + 0x104;
		hwif->sata_scr[SATA_ERROR_OFFSET]	= base + 0x108;
		hwif->sata_scr[SATA_CONTROL_OFFSET]	= base + 0x100;
		hwif->sata_misc[SATA_MISC_OFFSET]	= base + 0x140;
		hwif->sata_misc[SATA_PHY_OFFSET]	= base + 0x144;
		hwif->sata_misc[SATA_IEN_OFFSET]	= base + 0x148;
	}

	memcpy(hwif->io_ports, hw.io_ports, sizeof(hwif->io_ports));

	hwif->irq = dev->irq;

	hwif->dma_base = (unsigned long)addr + (ch ? 0x08 : 0x00);

	hwif->mmio = 1;
}

static int is_dev_seagate_sata(ide_drive_t *drive)
{
	const char *s = &drive->id->model[0];
	unsigned len;

	if (!drive->present)
		return 0;

	len = strnlen(s, sizeof(drive->id->model));

	if ((len > 4) && (!memcmp(s, "ST", 2))) {
		if ((!memcmp(s + len - 2, "AS", 2)) ||
		    (!memcmp(s + len - 3, "ASL", 3))) {
			printk(KERN_INFO "%s: applying pessimistic Seagate "
					 "errata fix\n", drive->name);
			return 1;
		}
	}
	return 0;
}

/**
 *	siimage_fixup		-	post probe fixups
 *	@hwif: interface to fix up
 *
 *	Called after drive probe we use this to decide whether the
 *	Seagate fixup must be applied. This used to be in init_iops but
 *	that can occur before we know what drives are present.
 */

static void __devinit siimage_fixup(ide_hwif_t *hwif)
{
	/* Try and raise the rqsize */
	if (!is_sata(hwif) || !is_dev_seagate_sata(&hwif->drives[0]))
		hwif->rqsize = 128;
}

/**
 *	init_iops_siimage	-	set up iops
 *	@hwif: interface to set up
 *
 *	Do the basic setup for the SIIMAGE hardware interface
 *	and then do the MMIO setup if we can. This is the first
 *	look in we get for setting up the hwif so that we
 *	can get the iops right before using them.
 */

static void __devinit init_iops_siimage(ide_hwif_t *hwif)
{
	hwif->hwif_data = NULL;

	/* Pessimal until we finish probing */
	hwif->rqsize = 15;

	if (pci_get_drvdata(hwif->pci_dev) == NULL)
		return;

	init_mmio_iops_siimage(hwif);
}

/**
 *	ata66_siimage	-	check for 80 pin cable
 *	@hwif: interface to check
 *
 *	Check for the presence of an ATA66 capable cable on the
 *	interface.
 */

static u8 __devinit ata66_siimage(ide_hwif_t *hwif)
{
	unsigned long addr = siimage_selreg(hwif, 0);
	u8 ata66 = 0;

	if (pci_get_drvdata(hwif->pci_dev) == NULL)
		pci_read_config_byte(hwif->pci_dev, addr, &ata66);
	else
		ata66 = hwif->INB(addr);

	return (ata66 & 0x01) ? ATA_CBL_PATA80 : ATA_CBL_PATA40;
}

/**
 *	init_hwif_siimage	-	set up hwif structs
 *	@hwif: interface to set up
 *
 *	We do the basic set up of the interface structure. The SIIMAGE
 *	requires several custom handlers so we override the default
 *	ide DMA handlers appropriately
 */

static void __devinit init_hwif_siimage(ide_hwif_t *hwif)
{
	u8 sata = is_sata(hwif);

	hwif->set_pio_mode = &sil_set_pio_mode;
	hwif->set_dma_mode = &sil_set_dma_mode;

	if (sata) {
		static int first = 1;

		hwif->busproc = &sil_sata_busproc;
		hwif->reset_poll = &sil_sata_reset_poll;
		hwif->pre_reset = &sil_sata_pre_reset;
		hwif->udma_filter = &sil_sata_udma_filter;

		if (first) {
			printk(KERN_INFO "siimage: For full SATA support you should use the libata sata_sil module.\n");
			first = 0;
		}
	} else
		hwif->udma_filter = &sil_pata_udma_filter;

	if (hwif->dma_base == 0)
		return;

	if (sata)
		hwif->host_flags |= IDE_HFLAG_NO_ATAPI_DMA;

	if (hwif->cbl != ATA_CBL_PATA40_SHORT)
		hwif->cbl = ata66_siimage(hwif);

	if (hwif->mmio) {
		hwif->ide_dma_test_irq = &siimage_mmio_ide_dma_test_irq;
	} else {
		hwif->ide_dma_test_irq = & siimage_io_ide_dma_test_irq;
	}
}

#define DECLARE_SII_DEV(name_str)			\
	{						\
		.name		= name_str,		\
		.init_chipset	= init_chipset_siimage,	\
		.init_iops	= init_iops_siimage,	\
		.init_hwif	= init_hwif_siimage,	\
		.fixup		= siimage_fixup,	\
		.host_flags	= IDE_HFLAG_BOOTABLE,	\
		.pio_mask	= ATA_PIO4,		\
		.mwdma_mask	= ATA_MWDMA2,		\
		.udma_mask	= ATA_UDMA6,		\
	}

static const struct ide_port_info siimage_chipsets[] __devinitdata = {
	/* 0 */ DECLARE_SII_DEV("SiI680"),
	/* 1 */ DECLARE_SII_DEV("SiI3112 Serial ATA"),
	/* 2 */ DECLARE_SII_DEV("Adaptec AAR-1210SA")
};

/**
 *	siimage_init_one	-	pci layer discovery entry
 *	@dev: PCI device
 *	@id: ident table entry
 *
 *	Called by the PCI code when it finds an SI680 or SI3112 controller.
 *	We then use the IDE PCI generic helper to do most of the work.
 */
 
static int __devinit siimage_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	return ide_setup_pci_device(dev, &siimage_chipsets[id->driver_data]);
}

static const struct pci_device_id siimage_pci_tbl[] = {
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_SII_680),    0 },
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_SII_3112),   1 },
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_SII_1210SA), 2 },
#endif
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, siimage_pci_tbl);

static struct pci_driver driver = {
	.name		= "SiI_IDE",
	.id_table	= siimage_pci_tbl,
	.probe		= siimage_init_one,
};

static int __init siimage_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(siimage_ide_init);

MODULE_AUTHOR("Andre Hedrick, Alan Cox");
MODULE_DESCRIPTION("PCI driver module for SiI IDE");
MODULE_LICENSE("GPL");
