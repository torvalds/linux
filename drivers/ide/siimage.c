/*
 * Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 * Copyright (C) 2003		Red Hat
 * Copyright (C) 2007-2008	MontaVista Software, Inc.
 * Copyright (C) 2007-2008	Bartlomiej Zolnierkiewicz
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
 *	ensure the system is set up for ATA100/UDMA5, not UDMA6.
 *
 *	If you are using WD drives with SATA bridges you must set the
 *	drive to "Single". "Master" will hang.
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
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/io.h>

#define DRV_NAME "siimage"

/**
 *	pdev_is_sata		-	check if device is SATA
 *	@pdev:	PCI device to check
 *
 *	Returns true if this is a SATA controller
 */

static int pdev_is_sata(struct pci_dev *pdev)
{
#ifdef CONFIG_BLK_DEV_IDE_SATA
	switch (pdev->device) {
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
	return pdev_is_sata(to_pci_dev(hwif->dev));
}

/**
 *	siimage_selreg		-	return register base
 *	@hwif: interface
 *	@r: config offset
 *
 *	Turn a config register offset into the right address in either
 *	PCI space or MMIO space to access the control register in question
 *	Thankfully this is a configuration operation, so isn't performance
 *	critical.
 */

static unsigned long siimage_selreg(ide_hwif_t *hwif, int r)
{
	unsigned long base = (unsigned long)hwif->hwif_data;

	base += 0xA0 + r;
	if (hwif->host_flags & IDE_HFLAG_MMIO)
		base += hwif->channel << 6;
	else
		base += hwif->channel << 4;
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
	ide_hwif_t *hwif	= drive->hwif;
	unsigned long base	= (unsigned long)hwif->hwif_data;
	u8 unit			= drive->dn & 1;

	base += 0xA0 + r;
	if (hwif->host_flags & IDE_HFLAG_MMIO)
		base += hwif->channel << 6;
	else
		base += hwif->channel << 4;
	base |= unit << unit;
	return base;
}

static u8 sil_ioread8(struct pci_dev *dev, unsigned long addr)
{
	struct ide_host *host = pci_get_drvdata(dev);
	u8 tmp = 0;

	if (host->host_priv)
		tmp = readb((void __iomem *)addr);
	else
		pci_read_config_byte(dev, addr, &tmp);

	return tmp;
}

static u16 sil_ioread16(struct pci_dev *dev, unsigned long addr)
{
	struct ide_host *host = pci_get_drvdata(dev);
	u16 tmp = 0;

	if (host->host_priv)
		tmp = readw((void __iomem *)addr);
	else
		pci_read_config_word(dev, addr, &tmp);

	return tmp;
}

static void sil_iowrite8(struct pci_dev *dev, u8 val, unsigned long addr)
{
	struct ide_host *host = pci_get_drvdata(dev);

	if (host->host_priv)
		writeb(val, (void __iomem *)addr);
	else
		pci_write_config_byte(dev, addr, val);
}

static void sil_iowrite16(struct pci_dev *dev, u16 val, unsigned long addr)
{
	struct ide_host *host = pci_get_drvdata(dev);

	if (host->host_priv)
		writew(val, (void __iomem *)addr);
	else
		pci_write_config_word(dev, addr, val);
}

static void sil_iowrite32(struct pci_dev *dev, u32 val, unsigned long addr)
{
	struct ide_host *host = pci_get_drvdata(dev);

	if (host->host_priv)
		writel(val, (void __iomem *)addr);
	else
		pci_write_config_dword(dev, addr, val);
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
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	unsigned long base	= (unsigned long)hwif->hwif_data;
	u8 scsc, mask		= 0;

	base += (hwif->host_flags & IDE_HFLAG_MMIO) ? 0x4A : 0x8A;

	scsc = sil_ioread8(dev, base);

	switch (scsc & 0x30) {
	case 0x10:	/* 133 */
		mask = ATA_UDMA6;
		break;
	case 0x20:	/* 2xPCI */
		mask = ATA_UDMA6;
		break;
	case 0x00:	/* 100 */
		mask = ATA_UDMA5;
		break;
	default: 	/* Disabled ? */
		BUG();
	}

	return mask;
}

static u8 sil_sata_udma_filter(ide_drive_t *drive)
{
	char *m = (char *)&drive->id[ATA_ID_PROD];

	return strstr(m, "Maxtor") ? ATA_UDMA5 : ATA_UDMA6;
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
	static const u16 tf_speed[]   = { 0x328a, 0x2283, 0x1281, 0x10c3, 0x10c1 };
	static const u16 data_speed[] = { 0x328a, 0x2283, 0x1104, 0x10c3, 0x10c1 };

	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	ide_drive_t *pair	= ide_get_pair_dev(drive);
	u32 speedt		= 0;
	u16 speedp		= 0;
	unsigned long addr	= siimage_seldev(drive, 0x04);
	unsigned long tfaddr	= siimage_selreg(hwif,	0x02);
	unsigned long base	= (unsigned long)hwif->hwif_data;
	u8 tf_pio		= pio;
	u8 mmio			= (hwif->host_flags & IDE_HFLAG_MMIO) ? 1 : 0;
	u8 addr_mask		= hwif->channel ? (mmio ? 0xF4 : 0x84)
						: (mmio ? 0xB4 : 0x80);
	u8 mode			= 0;
	u8 unit			= drive->dn & 1;

	/* trim *taskfile* PIO to the slowest of the master/slave */
	if (pair) {
		u8 pair_pio = ide_get_best_pio_mode(pair, 255, 4);

		if (pair_pio < tf_pio)
			tf_pio = pair_pio;
	}

	/* cheat for now and use the docs */
	speedp = data_speed[pio];
	speedt = tf_speed[tf_pio];

	sil_iowrite16(dev, speedp, addr);
	sil_iowrite16(dev, speedt, tfaddr);

	/* now set up IORDY */
	speedp = sil_ioread16(dev, tfaddr - 2);
	speedp &= ~0x200;
	if (pio > 2)
		speedp |= 0x200;
	sil_iowrite16(dev, speedp, tfaddr - 2);

	mode = sil_ioread8(dev, base + addr_mask);
	mode &= ~(unit ? 0x30 : 0x03);
	mode |= unit ? 0x10 : 0x01;
	sil_iowrite8(dev, mode, base + addr_mask);
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
	static const u8 ultra6[] = { 0x0F, 0x0B, 0x07, 0x05, 0x03, 0x02, 0x01 };
	static const u8 ultra5[] = { 0x0C, 0x07, 0x05, 0x04, 0x02, 0x01 };
	static const u16 dma[]	 = { 0x2208, 0x10C2, 0x10C1 };

	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	unsigned long base	= (unsigned long)hwif->hwif_data;
	u16 ultra = 0, multi	= 0;
	u8 mode = 0, unit	= drive->dn & 1;
	u8 mmio			= (hwif->host_flags & IDE_HFLAG_MMIO) ? 1 : 0;
	u8 scsc = 0, addr_mask	= hwif->channel ? (mmio ? 0xF4 : 0x84)
						: (mmio ? 0xB4 : 0x80);
	unsigned long ma	= siimage_seldev(drive, 0x08);
	unsigned long ua	= siimage_seldev(drive, 0x0C);

	scsc  = sil_ioread8 (dev, base + (mmio ? 0x4A : 0x8A));
	mode  = sil_ioread8 (dev, base + addr_mask);
	multi = sil_ioread16(dev, ma);
	ultra = sil_ioread16(dev, ua);

	mode  &= ~(unit ? 0x30 : 0x03);
	ultra &= ~0x3F;
	scsc = ((scsc & 0x30) == 0x00) ? 0 : 1;

	scsc = is_sata(hwif) ? 1 : scsc;

	if (speed >= XFER_UDMA_0) {
		multi  = dma[2];
		ultra |= scsc ? ultra6[speed - XFER_UDMA_0] :
				ultra5[speed - XFER_UDMA_0];
		mode  |= unit ? 0x30 : 0x03;
	} else {
		multi = dma[speed - XFER_MW_DMA_0];
		mode |= unit ? 0x20 : 0x02;
	}

	sil_iowrite8 (dev, mode, base + addr_mask);
	sil_iowrite16(dev, multi, ma);
	sil_iowrite16(dev, ultra, ua);
}

/* returns 1 if dma irq issued, 0 otherwise */
static int siimage_io_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	u8 dma_altstat		= 0;
	unsigned long addr	= siimage_selreg(hwif, 1);

	/* return 1 if INTR asserted */
	if (inb(hwif->dma_base + ATA_DMA_STATUS) & 4)
		return 1;

	/* return 1 if Device INTR asserted */
	pci_read_config_byte(dev, addr, &dma_altstat);
	if (dma_altstat & 8)
		return 0;	/* return 1; */

	return 0;
}

/**
 *	siimage_mmio_dma_test_irq	-	check we caused an IRQ
 *	@drive: drive we are testing
 *
 *	Check if we caused an IDE DMA interrupt. We may also have caused
 *	SATA status interrupts, if so we clean them up and continue.
 */

static int siimage_mmio_dma_test_irq(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= drive->hwif;
	unsigned long addr	= siimage_selreg(hwif, 0x1);
	void __iomem *sata_error_addr
		= (void __iomem *)hwif->sata_scr[SATA_ERROR_OFFSET];

	if (sata_error_addr) {
		unsigned long base	= (unsigned long)hwif->hwif_data;
		u32 ext_stat		= readl((void __iomem *)(base + 0x10));
		u8 watchdog		= 0;

		if (ext_stat & ((hwif->channel) ? 0x40 : 0x10)) {
			u32 sata_error = readl(sata_error_addr);

			writel(sata_error, sata_error_addr);
			watchdog = (sata_error & 0x00680000) ? 1 : 0;
			printk(KERN_WARNING "%s: sata_error = 0x%08x, "
				"watchdog = %d, %s\n",
				drive->name, sata_error, watchdog, __func__);
		} else
			watchdog = (ext_stat & 0x8000) ? 1 : 0;

		ext_stat >>= 16;
		if (!(ext_stat & 0x0404) && !watchdog)
			return 0;
	}

	/* return 1 if INTR asserted */
	if (readb((void __iomem *)(hwif->dma_base + ATA_DMA_STATUS)) & 4)
		return 1;

	/* return 1 if Device INTR asserted */
	if (readb((void __iomem *)addr) & 8)
		return 0;	/* return 1; */

	return 0;
}

static int siimage_dma_test_irq(ide_drive_t *drive)
{
	if (drive->hwif->host_flags & IDE_HFLAG_MMIO)
		return siimage_mmio_dma_test_irq(drive);
	else
		return siimage_io_dma_test_irq(drive);
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
	ide_hwif_t *hwif = drive->hwif;
	void __iomem *sata_status_addr
		= (void __iomem *)hwif->sata_scr[SATA_STATUS_OFFSET];

	if (sata_status_addr) {
		/* SATA Status is available only when in MMIO mode */
		u32 sata_stat = readl(sata_status_addr);

		if ((sata_stat & 0x03) != 0x03) {
			printk(KERN_WARNING "%s: reset phy dead, status=0x%08x\n",
					    hwif->name, sata_stat);
			return -ENXIO;
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
 *	init_chipset_siimage	-	set up an SI device
 *	@dev: PCI device
 *
 *	Perform the initial PCI set up for this device. Attempt to switch
 *	to 133 MHz clocking if the system isn't already set up to do it.
 */

static unsigned int init_chipset_siimage(struct pci_dev *dev)
{
	struct ide_host *host = pci_get_drvdata(dev);
	void __iomem *ioaddr = host->host_priv;
	unsigned long base, scsc_addr;
	u8 rev = dev->revision, tmp;

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, rev ? 1 : 255);

	if (ioaddr)
		pci_set_master(dev);

	base = (unsigned long)ioaddr;

	if (ioaddr && pdev_is_sata(dev)) {
		u32 tmp32, irq_mask;

		/* make sure IDE0/1 interrupts are not masked */
		irq_mask = (1 << 22) | (1 << 23);
		tmp32 = readl(ioaddr + 0x48);
		if (tmp32 & irq_mask) {
			tmp32 &= ~irq_mask;
			writel(tmp32, ioaddr + 0x48);
			readl(ioaddr + 0x48); /* flush */
		}
		writel(0, ioaddr + 0x148);
		writel(0, ioaddr + 0x1C8);
	}

	sil_iowrite8(dev, 0, base ? (base + 0xB4) : 0x80);
	sil_iowrite8(dev, 0, base ? (base + 0xF4) : 0x84);

	scsc_addr = base ? (base + 0x4A) : 0x8A;
	tmp = sil_ioread8(dev, scsc_addr);

	switch (tmp & 0x30) {
	case 0x00:
		/* On 100 MHz clocking, try and switch to 133 MHz */
		sil_iowrite8(dev, tmp | 0x10, scsc_addr);
		break;
	case 0x30:
		/* Clocking is disabled, attempt to force 133MHz clocking. */
		sil_iowrite8(dev, tmp & ~0x20, scsc_addr);
	case 0x10:
		/* On 133Mhz clocking. */
		break;
	case 0x20:
		/* On PCIx2 clocking. */
		break;
	}

	tmp = sil_ioread8(dev, scsc_addr);

	sil_iowrite8 (dev,       0x72, base + 0xA1);
	sil_iowrite16(dev,     0x328A, base + 0xA2);
	sil_iowrite32(dev, 0x62DD62DD, base + 0xA4);
	sil_iowrite32(dev, 0x43924392, base + 0xA8);
	sil_iowrite32(dev, 0x40094009, base + 0xAC);
	sil_iowrite8 (dev,       0x72, base ? (base + 0xE1) : 0xB1);
	sil_iowrite16(dev,     0x328A, base ? (base + 0xE2) : 0xB2);
	sil_iowrite32(dev, 0x62DD62DD, base ? (base + 0xE4) : 0xB4);
	sil_iowrite32(dev, 0x43924392, base ? (base + 0xE8) : 0xB8);
	sil_iowrite32(dev, 0x40094009, base ? (base + 0xEC) : 0xBC);

	if (base && pdev_is_sata(dev)) {
		writel(0xFFFF0000, ioaddr + 0x108);
		writel(0xFFFF0000, ioaddr + 0x188);
		writel(0x00680000, ioaddr + 0x148);
		writel(0x00680000, ioaddr + 0x1C8);
	}

	/* report the clocking mode of the controller */
	if (!pdev_is_sata(dev)) {
		static const char *clk_str[] =
			{ "== 100", "== 133", "== 2X PCI", "DISABLED!" };

		tmp >>= 4;
		printk(KERN_INFO DRV_NAME " %s: BASE CLOCK %s\n",
			pci_name(dev), clk_str[tmp & 3]);
	}

	return 0;
}

/**
 *	init_mmio_iops_siimage	-	set up the iops for MMIO
 *	@hwif: interface to set up
 *
 *	The basic setup here is fairly simple, we can use standard MMIO
 *	operations. However we do have to set the taskfile register offsets
 *	by hand as there isn't a standard defined layout for them this time.
 *
 *	The hardware supports buffered taskfiles and also some rather nice
 *	extended PRD tables. For better SI3112 support use the libata driver
 */

static void __devinit init_mmio_iops_siimage(ide_hwif_t *hwif)
{
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	struct ide_host *host	= pci_get_drvdata(dev);
	void *addr		= host->host_priv;
	u8 ch			= hwif->channel;
	struct ide_io_ports *io_ports = &hwif->io_ports;
	unsigned long base;

	/*
	 *	Fill in the basic hwif bits
	 */
	hwif->host_flags |= IDE_HFLAG_MMIO;

	hwif->hwif_data	= addr;

	/*
	 *	Now set up the hw. We have to do this ourselves as the
	 *	MMIO layout isn't the same as the standard port based I/O.
	 */
	memset(io_ports, 0, sizeof(*io_ports));

	base = (unsigned long)addr;
	if (ch)
		base += 0xC0;
	else
		base += 0x80;

	/*
	 *	The buffered task file doesn't have status/control, so we
	 *	can't currently use it sanely since we want to use LBA48 mode.
	 */
	io_ports->data_addr	= base;
	io_ports->error_addr	= base + 1;
	io_ports->nsect_addr	= base + 2;
	io_ports->lbal_addr	= base + 3;
	io_ports->lbam_addr	= base + 4;
	io_ports->lbah_addr	= base + 5;
	io_ports->device_addr	= base + 6;
	io_ports->status_addr	= base + 7;
	io_ports->ctl_addr	= base + 10;

	if (pdev_is_sata(dev)) {
		base = (unsigned long)addr;
		if (ch)
			base += 0x80;
		hwif->sata_scr[SATA_STATUS_OFFSET]	= base + 0x104;
		hwif->sata_scr[SATA_ERROR_OFFSET]	= base + 0x108;
		hwif->sata_scr[SATA_CONTROL_OFFSET]	= base + 0x100;
	}

	hwif->irq = dev->irq;

	hwif->dma_base = (unsigned long)addr + (ch ? 0x08 : 0x00);
}

static int is_dev_seagate_sata(ide_drive_t *drive)
{
	const char *s	= (const char *)&drive->id[ATA_ID_PROD];
	unsigned len	= strnlen(s, ATA_ID_PROD_LEN);

	if ((len > 4) && (!memcmp(s, "ST", 2)))
		if ((!memcmp(s + len - 2, "AS", 2)) ||
		    (!memcmp(s + len - 3, "ASL", 3))) {
			printk(KERN_INFO "%s: applying pessimistic Seagate "
					 "errata fix\n", drive->name);
			return 1;
		}

	return 0;
}

/**
 *	sil_quirkproc		-	post probe fixups
 *	@drive: drive
 *
 *	Called after drive probe we use this to decide whether the
 *	Seagate fixup must be applied. This used to be in init_iops but
 *	that can occur before we know what drives are present.
 */

static void sil_quirkproc(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;

	/* Try and rise the rqsize */
	if (!is_sata(hwif) || !is_dev_seagate_sata(drive))
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
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	struct ide_host *host = pci_get_drvdata(dev);

	hwif->hwif_data = NULL;

	/* Pessimal until we finish probing */
	hwif->rqsize = 15;

	if (host->host_priv)
		init_mmio_iops_siimage(hwif);
}

/**
 *	sil_cable_detect	-	cable detection
 *	@hwif: interface to check
 *
 *	Check for the presence of an ATA66 capable cable on the interface.
 */

static u8 sil_cable_detect(ide_hwif_t *hwif)
{
	struct pci_dev *dev	= to_pci_dev(hwif->dev);
	unsigned long addr	= siimage_selreg(hwif, 0);
	u8 ata66		= sil_ioread8(dev, addr);

	return (ata66 & 0x01) ? ATA_CBL_PATA80 : ATA_CBL_PATA40;
}

static const struct ide_port_ops sil_pata_port_ops = {
	.set_pio_mode		= sil_set_pio_mode,
	.set_dma_mode		= sil_set_dma_mode,
	.quirkproc		= sil_quirkproc,
	.udma_filter		= sil_pata_udma_filter,
	.cable_detect		= sil_cable_detect,
};

static const struct ide_port_ops sil_sata_port_ops = {
	.set_pio_mode		= sil_set_pio_mode,
	.set_dma_mode		= sil_set_dma_mode,
	.reset_poll		= sil_sata_reset_poll,
	.pre_reset		= sil_sata_pre_reset,
	.quirkproc		= sil_quirkproc,
	.udma_filter		= sil_sata_udma_filter,
	.cable_detect		= sil_cable_detect,
};

static const struct ide_dma_ops sil_dma_ops = {
	.dma_host_set		= ide_dma_host_set,
	.dma_setup		= ide_dma_setup,
	.dma_exec_cmd		= ide_dma_exec_cmd,
	.dma_start		= ide_dma_start,
	.dma_end		= ide_dma_end,
	.dma_test_irq		= siimage_dma_test_irq,
	.dma_timeout		= ide_dma_timeout,
	.dma_lost_irq		= ide_dma_lost_irq,
	.dma_sff_read_status	= ide_dma_sff_read_status,
};

#define DECLARE_SII_DEV(p_ops)				\
	{						\
		.name		= DRV_NAME,		\
		.init_chipset	= init_chipset_siimage,	\
		.init_iops	= init_iops_siimage,	\
		.port_ops	= p_ops,		\
		.dma_ops	= &sil_dma_ops,		\
		.pio_mask	= ATA_PIO4,		\
		.mwdma_mask	= ATA_MWDMA2,		\
		.udma_mask	= ATA_UDMA6,		\
	}

static const struct ide_port_info siimage_chipsets[] __devinitdata = {
	/* 0: SiI680 */  DECLARE_SII_DEV(&sil_pata_port_ops),
	/* 1: SiI3112 */ DECLARE_SII_DEV(&sil_sata_port_ops)
};

/**
 *	siimage_init_one	-	PCI layer discovery entry
 *	@dev: PCI device
 *	@id: ident table entry
 *
 *	Called by the PCI code when it finds an SiI680 or SiI3112 controller.
 *	We then use the IDE PCI generic helper to do most of the work.
 */

static int __devinit siimage_init_one(struct pci_dev *dev,
				      const struct pci_device_id *id)
{
	void __iomem *ioaddr = NULL;
	resource_size_t bar5 = pci_resource_start(dev, 5);
	unsigned long barsize = pci_resource_len(dev, 5);
	int rc;
	struct ide_port_info d;
	u8 idx = id->driver_data;
	u8 BA5_EN;

	d = siimage_chipsets[idx];

	if (idx) {
		static int first = 1;

		if (first) {
			printk(KERN_INFO DRV_NAME ": For full SATA support you "
				"should use the libata sata_sil module.\n");
			first = 0;
		}

		d.host_flags |= IDE_HFLAG_NO_ATAPI_DMA;
	}

	rc = pci_enable_device(dev);
	if (rc)
		return rc;

	pci_read_config_byte(dev, 0x8A, &BA5_EN);
	if ((BA5_EN & 0x01) || bar5) {
		/*
		* Drop back to PIO if we can't map the MMIO. Some systems
		* seem to get terminally confused in the PCI spaces.
		*/
		if (!request_mem_region(bar5, barsize, d.name)) {
			printk(KERN_WARNING DRV_NAME " %s: MMIO ports not "
				"available\n", pci_name(dev));
		} else {
			ioaddr = pci_ioremap_bar(dev, 5);
			if (ioaddr == NULL)
				release_mem_region(bar5, barsize);
		}
	}

	rc = ide_pci_init_one(dev, &d, ioaddr);
	if (rc) {
		if (ioaddr) {
			iounmap(ioaddr);
			release_mem_region(bar5, barsize);
		}
		pci_disable_device(dev);
	}

	return rc;
}

static void __devexit siimage_remove(struct pci_dev *dev)
{
	struct ide_host *host = pci_get_drvdata(dev);
	void __iomem *ioaddr = host->host_priv;

	ide_pci_remove(dev);

	if (ioaddr) {
		resource_size_t bar5 = pci_resource_start(dev, 5);
		unsigned long barsize = pci_resource_len(dev, 5);

		iounmap(ioaddr);
		release_mem_region(bar5, barsize);
	}

	pci_disable_device(dev);
}

static const struct pci_device_id siimage_pci_tbl[] = {
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_SII_680),    0 },
#ifdef CONFIG_BLK_DEV_IDE_SATA
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_SII_3112),   1 },
	{ PCI_VDEVICE(CMD, PCI_DEVICE_ID_SII_1210SA), 1 },
#endif
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, siimage_pci_tbl);

static struct pci_driver siimage_pci_driver = {
	.name		= "SiI_IDE",
	.id_table	= siimage_pci_tbl,
	.probe		= siimage_init_one,
	.remove		= __devexit_p(siimage_remove),
	.suspend	= ide_pci_suspend,
	.resume		= ide_pci_resume,
};

static int __init siimage_ide_init(void)
{
	return ide_pci_register_driver(&siimage_pci_driver);
}

static void __exit siimage_ide_exit(void)
{
	pci_unregister_driver(&siimage_pci_driver);
}

module_init(siimage_ide_init);
module_exit(siimage_ide_exit);

MODULE_AUTHOR("Andre Hedrick, Alan Cox");
MODULE_DESCRIPTION("PCI driver module for SiI IDE");
MODULE_LICENSE("GPL");
