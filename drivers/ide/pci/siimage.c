/*
 * linux/drivers/ide/pci/siimage.c		Version 1.07	Nov 30, 2003
 *
 * Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2003		Red Hat <alan@redhat.com>
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
 *	if neccessary
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#undef SIIMAGE_VIRTUAL_DMAPIO
#undef SIIMAGE_LARGE_DMA

/**
 *	pdev_is_sata		-	check if device is SATA
 *	@pdev:	PCI device to check
 *	
 *	Returns true if this is a SATA controller
 */
 
static int pdev_is_sata(struct pci_dev *pdev)
{
	switch(pdev->device)
	{
		case PCI_DEVICE_ID_SII_3112:
		case PCI_DEVICE_ID_SII_1210SA:
			return 1;
		case PCI_DEVICE_ID_SII_680:
			return 0;
	}
	BUG();
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
 *	siimage_ratemask	-	Compute available modes
 *	@drive: IDE drive
 *
 *	Compute the available speeds for the devices on the interface.
 *	For the CMD680 this depends on the clocking mode (scsc), for the
 *	SI3312 SATA controller life is a bit simpler. Enforce UDMA33
 *	as a limit if there is no 80pin cable present.
 */
 
static byte siimage_ratemask (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 mode	= 0, scsc = 0;
	unsigned long base = (unsigned long) hwif->hwif_data;

	if (hwif->mmio)
		scsc = hwif->INB(base + 0x4A);
	else
		pci_read_config_byte(hwif->pci_dev, 0x8A, &scsc);

	if(is_sata(hwif))
	{
		if(strstr(drive->id->model, "Maxtor"))
			return 3;
		return 4;
	}
	
	if ((scsc & 0x30) == 0x10)	/* 133 */
		mode = 4;
	else if ((scsc & 0x30) == 0x20)	/* 2xPCI */
		mode = 4;
	else if ((scsc & 0x30) == 0x00)	/* 100 */
		mode = 3;
	else 	/* Disabled ? */
		BUG();

	if (!eighty_ninty_three(drive))
		mode = min(mode, (u8)1);
	return mode;
}

/**
 *	siimage_taskfile_timing	-	turn timing data to a mode
 *	@hwif: interface to query
 *
 *	Read the timing data for the interface and return the 
 *	mode that is being used.
 */
 
static byte siimage_taskfile_timing (ide_hwif_t *hwif)
{
	u16 timing	= 0x328a;
	unsigned long addr = siimage_selreg(hwif, 2);

	if (hwif->mmio)
		timing = hwif->INW(addr);
	else
		pci_read_config_word(hwif->pci_dev, addr, &timing);

	switch (timing) {
		case 0x10c1:	return 4;
		case 0x10c3:	return 3;
		case 0x1104:
		case 0x1281:	return 2;
		case 0x2283:	return 1;
		case 0x328a:
		default:	return 0;
	}
}

/**
 *	simmage_tuneproc	-	tune a drive
 *	@drive: drive to tune
 *	@mode_wanted: the target operating mode
 *
 *	Load the timing settings for this device mode into the
 *	controller. If we are in PIO mode 3 or 4 turn on IORDY
 *	monitoring (bit 9). The TF timing is bits 31:16
 */
 
static void siimage_tuneproc (ide_drive_t *drive, byte mode_wanted)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u32 speedt		= 0;
	u16 speedp		= 0;
	unsigned long addr	= siimage_seldev(drive, 0x04);
	unsigned long tfaddr	= siimage_selreg(hwif, 0x02);
	
	/* cheat for now and use the docs */
	switch(mode_wanted) {
		case 4:	
			speedp = 0x10c1; 
			speedt = 0x10c1;
			break;
		case 3:	
			speedp = 0x10C3; 
			speedt = 0x10C3;
			break;
		case 2:	
			speedp = 0x1104; 
			speedt = 0x1281;
			break;
		case 1:		
			speedp = 0x2283; 
			speedt = 0x1281;
			break;
		case 0:
		default:
			speedp = 0x328A; 
			speedt = 0x328A;
			break;
	}
	if (hwif->mmio)
	{
		hwif->OUTW(speedt, addr);
		hwif->OUTW(speedp, tfaddr);
		/* Now set up IORDY */
		if(mode_wanted == 3 || mode_wanted == 4)
			hwif->OUTW(hwif->INW(tfaddr-2)|0x200, tfaddr-2);
		else
			hwif->OUTW(hwif->INW(tfaddr-2)&~0x200, tfaddr-2);
	}
	else
	{
		pci_write_config_word(hwif->pci_dev, addr, speedp);
		pci_write_config_word(hwif->pci_dev, tfaddr, speedt);
		pci_read_config_word(hwif->pci_dev, tfaddr-2, &speedp);
		speedp &= ~0x200;
		/* Set IORDY for mode 3 or 4 */
		if(mode_wanted == 3 || mode_wanted == 4)
			speedp |= 0x200;
		pci_write_config_word(hwif->pci_dev, tfaddr-2, speedp);
	}
}

/**
 *	config_siimage_chipset_for_pio	-	set drive timings
 *	@drive: drive to tune
 *	@speed we want
 *
 *	Compute the best pio mode we can for a given device. Also honour
 *	the timings for the driver when dealing with mixed devices. Some
 *	of this is ugly but its all wrapped up here
 *
 *	The SI680 can also do VDMA - we need to start using that
 *
 *	FIXME: we use the BIOS channel timings to avoid driving the task
 *	files too fast at the disk. We need to compute the master/slave
 *	drive PIO mode properly so that we can up the speed on a hotplug
 *	system.
 */
 
static void config_siimage_chipset_for_pio (ide_drive_t *drive, byte set_speed)
{
	u8 channel_timings	= siimage_taskfile_timing(HWIF(drive));
	u8 speed = 0, set_pio	= ide_get_best_pio_mode(drive, 4, 5, NULL);

	/* WARNING PIO timing mess is going to happen b/w devices, argh */
	if ((channel_timings != set_pio) && (set_pio > channel_timings))
		set_pio = channel_timings;

	siimage_tuneproc(drive, set_pio);
	speed = XFER_PIO_0 + set_pio;
	if (set_speed)
		(void) ide_config_drive_speed(drive, speed);
}

static void config_chipset_for_pio (ide_drive_t *drive, byte set_speed)
{
	config_siimage_chipset_for_pio(drive, set_speed);
}

/**
 *	siimage_tune_chipset	-	set controller timings
 *	@drive: Drive to set up
 *	@xferspeed: speed we want to achieve
 *
 *	Tune the SII chipset for the desired mode. If we can't achieve
 *	the desired mode then tune for a lower one, but ultimately
 *	make the thing work.
 */
 
static int siimage_tune_chipset (ide_drive_t *drive, byte xferspeed)
{
	u8 ultra6[]		= { 0x0F, 0x0B, 0x07, 0x05, 0x03, 0x02, 0x01 };
	u8 ultra5[]		= { 0x0C, 0x07, 0x05, 0x04, 0x02, 0x01 };
	u16 dma[]		= { 0x2208, 0x10C2, 0x10C1 };

	ide_hwif_t *hwif	= HWIF(drive);
	u16 ultra = 0, multi	= 0;
	u8 mode = 0, unit	= drive->select.b.unit;
	u8 speed		= ide_rate_filter(siimage_ratemask(drive), xferspeed);
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

	switch(speed) {
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			siimage_tuneproc(drive, (speed - XFER_PIO_0));
			mode |= ((unit) ? 0x10 : 0x01);
			break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
			multi = dma[speed - XFER_MW_DMA_0];
			mode |= ((unit) ? 0x20 : 0x02);
			config_siimage_chipset_for_pio(drive, 0);
			break;
		case XFER_UDMA_6:
		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			multi = dma[2];
			ultra |= ((scsc) ? (ultra6[speed - XFER_UDMA_0]) :
					   (ultra5[speed - XFER_UDMA_0]));
			mode |= ((unit) ? 0x30 : 0x03);
			config_siimage_chipset_for_pio(drive, 0);
			break;
		default:
			return 1;
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
	return (ide_config_drive_speed(drive, speed));
}

/**
 *	config_chipset_for_dma	-	configure for DMA
 *	@drive: drive to configure
 *
 *	Called by the IDE layer when it wants the timings set up.
 *	For the CMD680 we also need to set up the PIO timings and
 *	enable DMA.
 */
 
static int config_chipset_for_dma (ide_drive_t *drive)
{
	u8 speed	= ide_dma_speed(drive, siimage_ratemask(drive));

	config_chipset_for_pio(drive, !speed);

	if (!speed)
		return 0;

	if (ide_set_xfer_rate(drive, speed))
		return 0;

	if (!drive->init_speed)
		drive->init_speed = speed;

	return ide_dma_enable(drive);
}

/**
 *	siimage_configure_drive_for_dma	-	set up for DMA transfers
 *	@drive: drive we are going to set up
 *
 *	Set up the drive for DMA, tune the controller and drive as 
 *	required. If the drive isn't suitable for DMA or we hit
 *	other problems then we will drop down to PIO and set up
 *	PIO appropriately
 */
 
static int siimage_config_drive_for_dma (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	if ((id->capability & 1) != 0 && drive->autodma) {

		if (ide_use_dma(drive)) {
			if (config_chipset_for_dma(drive))
				return hwif->ide_dma_on(drive);
		}

		goto fast_ata_pio;

	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		config_chipset_for_pio(drive, 1);
		return hwif->ide_dma_off_quietly(drive);
	}
	/* IORDY not supported */
	return 0;
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

#if 0
/**
 *	siimage_mmio_ide_dma_count	-	DMA bytes done
 *	@drive
 *
 *	If we are doing VDMA the CMD680 requires a little bit
 *	of more careful handling and we have to read the counts
 *	off ourselves. For non VDMA life is normal.
 */
 
static int siimage_mmio_ide_dma_count (ide_drive_t *drive)
{
#ifdef SIIMAGE_VIRTUAL_DMAPIO
	struct request *rq	= HWGROUP(drive)->rq;
	ide_hwif_t *hwif	= HWIF(drive);
	u32 count		= (rq->nr_sectors * SECTOR_SIZE);
	u32 rcount		= 0;
	unsigned long addr	= siimage_selreg(hwif, 0x1C);

	hwif->OUTL(count, addr);
	rcount = hwif->INL(addr);

	printk("\n%s: count = %d, rcount = %d, nr_sectors = %lu\n",
		drive->name, count, rcount, rq->nr_sectors);

#endif /* SIIMAGE_VIRTUAL_DMAPIO */
	return __ide_dma_count(drive);
}
#endif

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
	unsigned long base	= (unsigned long)hwif->hwif_data;
	unsigned long addr	= siimage_selreg(hwif, 0x1);

	if (SATA_ERROR_REG) {
		u32 ext_stat = hwif->INL(base + 0x10);
		u8 watchdog = 0;
		if (ext_stat & ((hwif->channel) ? 0x40 : 0x10)) {
			u32 sata_error = hwif->INL(SATA_ERROR_REG);
			hwif->OUTL(sata_error, SATA_ERROR_REG);
			watchdog = (sata_error & 0x00680000) ? 1 : 0;
#if 1
			printk(KERN_WARNING "%s: sata_error = 0x%08x, "
				"watchdog = %d, %s\n",
				drive->name, sata_error, watchdog,
				__FUNCTION__);
#endif

		} else {
			watchdog = (ext_stat & 0x8000) ? 1 : 0;
		}
		ext_stat >>= 16;

		if (!(ext_stat & 0x0404) && !watchdog)
			return 0;
	}

	/* return 1 if INTR asserted */
	if ((hwif->INB(hwif->dma_status) & 0x04) == 0x04)
		return 1;

	/* return 1 if Device INTR asserted */
	if ((hwif->INB(addr) & 8) == 8)
		return 0;	//return 1;

	return 0;
}

/**
 *	siimage_busproc		-	bus isolation ioctl
 *	@drive: drive to isolate/restore
 *	@state: bus state to set
 *
 *	Used by the SII3112 to handle bus isolation. As this is a 
 *	SATA controller the work required is quite limited, we 
 *	just have to clean up the statistics
 */
 
static int siimage_busproc (ide_drive_t * drive, int state)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u32 stat_config		= 0;
	unsigned long addr	= siimage_selreg(hwif, 0);

	if (hwif->mmio) {
		stat_config = hwif->INL(addr);
	} else
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
 *	siimage_reset_poll	-	wait for sata reset
 *	@drive: drive we are resetting
 *
 *	Poll the SATA phy and see whether it has come back from the dead
 *	yet.
 */
 
static int siimage_reset_poll (ide_drive_t *drive)
{
	if (SATA_STATUS_REG) {
		ide_hwif_t *hwif	= HWIF(drive);

		if ((hwif->INL(SATA_STATUS_REG) & 0x03) != 0x03) {
			printk(KERN_WARNING "%s: reset phy dead, status=0x%08x\n",
				hwif->name, hwif->INL(SATA_STATUS_REG));
			HWGROUP(drive)->polling = 0;
			return ide_started;
		}
		return 0;
	} else {
		return 0;
	}
}

/**
 *	siimage_pre_reset	-	reset hook
 *	@drive: IDE device being reset
 *
 *	For the SATA devices we need to handle recalibration/geometry
 *	differently
 */
 
static void siimage_pre_reset (ide_drive_t *drive)
{
	if (drive->media != ide_disk)
		return;

	if (is_sata(HWIF(drive)))
	{
		drive->special.b.set_geometry = 0;
		drive->special.b.recalibrate = 0;
	}
}

/**
 *	siimage_reset	-	reset a device on an siimage controller
 *	@drive: drive to reset
 *
 *	Perform a controller level reset fo the device. For
 *	SATA we must also check the PHY.
 */
 
static void siimage_reset (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 reset		= 0;
	unsigned long addr	= siimage_selreg(hwif, 0);

	if (hwif->mmio) {
		reset = hwif->INB(addr);
		hwif->OUTB((reset|0x03), addr);
		/* FIXME:posting */
		udelay(25);
		hwif->OUTB(reset, addr);
		(void) hwif->INB(addr);
	} else {
		pci_read_config_byte(hwif->pci_dev, addr, &reset);
		pci_write_config_byte(hwif->pci_dev, addr, reset|0x03);
		udelay(25);
		pci_write_config_byte(hwif->pci_dev, addr, reset);
		pci_read_config_byte(hwif->pci_dev, addr, &reset);
	}

	if (SATA_STATUS_REG) {
		u32 sata_stat = hwif->INL(SATA_STATUS_REG);
		printk(KERN_WARNING "%s: reset phy, status=0x%08x, %s\n",
			hwif->name, sata_stat, __FUNCTION__);
		if (!(sata_stat)) {
			printk(KERN_WARNING "%s: reset phy dead, status=0x%08x\n",
				hwif->name, sata_stat);
			drive->failures++;
		}
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
	u32 class_rev	= 0;
	u8 tmpbyte	= 0;
	u8 BA5_EN	= 0;

        pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
        class_rev &= 0xff;
	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, (class_rev) ? 1 : 255);	

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
 *	extended PRD tables. Unfortunately right now we don't.
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
	 *	the MMIO layout isnt the same as the the standard port
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
//	base += 0x10;
//	hwif->no_lba48 = 1;

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

	hw.irq				= hwif->pci_dev->irq;

	memcpy(&hwif->hw, &hw, sizeof(hw));
	memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->hw.io_ports));

	hwif->irq			= hw.irq;

       	base = (unsigned long) addr;

#ifdef SIIMAGE_LARGE_DMA
/* Watch the brackets - even Ken and Dennis get some language design wrong */
	hwif->dma_base			= base + (ch ? 0x18 : 0x10);
	hwif->dma_base2			= base + (ch ? 0x08 : 0x00);
	hwif->dma_prdtable		= hwif->dma_base2 + 4;
#else /* ! SIIMAGE_LARGE_DMA */
	hwif->dma_base			= base + (ch ? 0x08 : 0x00);
	hwif->dma_base2			= base + (ch ? 0x18 : 0x10);
#endif /* SIIMAGE_LARGE_DMA */
	hwif->mmio			= 2;
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
	struct pci_dev *dev	= hwif->pci_dev;
	u32 class_rev		= 0;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;
	
	hwif->hwif_data = NULL;

	/* Pessimal until we finish probing */
	hwif->rqsize = 15;

	if (pci_get_drvdata(dev) == NULL)
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

static unsigned int __devinit ata66_siimage(ide_hwif_t *hwif)
{
	unsigned long addr = siimage_selreg(hwif, 0);
	if (pci_get_drvdata(hwif->pci_dev) == NULL) {
		u8 ata66 = 0;
		pci_read_config_byte(hwif->pci_dev, addr, &ata66);
		return (ata66 & 0x01) ? 1 : 0;
	}

	return (hwif->INB(addr) & 0x01) ? 1 : 0;
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
	hwif->autodma = 0;
	
	hwif->resetproc = &siimage_reset;
	hwif->speedproc = &siimage_tune_chipset;
	hwif->tuneproc	= &siimage_tuneproc;
	hwif->reset_poll = &siimage_reset_poll;
	hwif->pre_reset = &siimage_pre_reset;

	if(is_sata(hwif))
		hwif->busproc   = &siimage_busproc;

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

	if (!is_sata(hwif))
		hwif->atapi_dma = 1;

	hwif->ide_dma_check = &siimage_config_drive_for_dma;
	if (!(hwif->udma_four))
		hwif->udma_four = ata66_siimage(hwif);

	if (hwif->mmio) {
		hwif->ide_dma_test_irq = &siimage_mmio_ide_dma_test_irq;
	} else {
		hwif->ide_dma_test_irq = & siimage_io_ide_dma_test_irq;
	}
	
	/*
	 *	The BIOS often doesn't set up DMA on this controller
	 *	so we always do it.
	 */

	hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

#define DECLARE_SII_DEV(name_str)			\
	{						\
		.name		= name_str,		\
		.init_chipset	= init_chipset_siimage,	\
		.init_iops	= init_iops_siimage,	\
		.init_hwif	= init_hwif_siimage,	\
		.fixup		= siimage_fixup,	\
		.channels	= 2,			\
		.autodma	= AUTODMA,		\
		.bootable	= ON_BOARD,		\
	}

static ide_pci_device_t siimage_chipsets[] __devinitdata = {
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

static struct pci_device_id siimage_pci_tbl[] = {
	{ PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_SII_680,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_SII_3112, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_CMD, PCI_DEVICE_ID_SII_1210SA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
#endif
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, siimage_pci_tbl);

static struct pci_driver driver = {
	.name		= "SiI_IDE",
	.id_table	= siimage_pci_tbl,
	.probe		= siimage_init_one,
};

static int siimage_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

module_init(siimage_ide_init);

MODULE_AUTHOR("Andre Hedrick, Alan Cox");
MODULE_DESCRIPTION("PCI driver module for SiI IDE");
MODULE_LICENSE("GPL");
