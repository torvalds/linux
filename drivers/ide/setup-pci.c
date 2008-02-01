/*
 *  Copyright (C) 1998-2000  Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 1995-1998  Mark Lord
 *  Copyright (C)      2007  Bartlomiej Zolnierkiewicz
 *
 *  May be copied or modified under the terms of the GNU General Public License
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ide.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/irq.h>


/**
 *	ide_match_hwif	-	match a PCI IDE against an ide_hwif
 *	@io_base: I/O base of device
 *	@bootable: set if its bootable
 *	@name: name of device
 *
 *	Match a PCI IDE port against an entry in ide_hwifs[],
 *	based on io_base port if possible. Return the matching hwif,
 *	or a new hwif. If we find an error (clashing, out of devices, etc)
 *	return NULL
 *
 *	FIXME: we need to handle mmio matches here too
 */

static ide_hwif_t *ide_match_hwif(unsigned long io_base, u8 bootable, const char *name)
{
	int h;
	ide_hwif_t *hwif;

	/*
	 * Look for a hwif with matching io_base specified using
	 * parameters to ide_setup().
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		hwif = &ide_hwifs[h];
		if (hwif->io_ports[IDE_DATA_OFFSET] == io_base) {
			if (hwif->chipset == ide_forced)
				return hwif; /* a perfect match */
		}
	}
	/*
	 * Look for a hwif with matching io_base default value.
	 * If chipset is "ide_unknown", then claim that hwif slot.
	 * Otherwise, some other chipset has already claimed it..  :(
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		hwif = &ide_hwifs[h];
		if (hwif->io_ports[IDE_DATA_OFFSET] == io_base) {
			if (hwif->chipset == ide_unknown)
				return hwif; /* match */
			printk(KERN_ERR "%s: port 0x%04lx already claimed by %s\n",
				name, io_base, hwif->name);
			return NULL;	/* already claimed */
		}
	}
	/*
	 * Okay, there is no hwif matching our io_base,
	 * so we'll just claim an unassigned slot.
	 * Give preference to claiming other slots before claiming ide0/ide1,
	 * just in case there's another interface yet-to-be-scanned
	 * which uses ports 1f0/170 (the ide0/ide1 defaults).
	 *
	 * Unless there is a bootable card that does not use the standard
	 * ports 1f0/170 (the ide0/ide1 defaults). The (bootable) flag.
	 */
	if (bootable) {
		for (h = 0; h < MAX_HWIFS; ++h) {
			hwif = &ide_hwifs[h];
			if (hwif->chipset == ide_unknown)
				return hwif;	/* pick an unused entry */
		}
	} else {
		for (h = 2; h < MAX_HWIFS; ++h) {
			hwif = ide_hwifs + h;
			if (hwif->chipset == ide_unknown)
				return hwif;	/* pick an unused entry */
		}
	}
	for (h = 0; h < 2 && h < MAX_HWIFS; ++h) {
		hwif = ide_hwifs + h;
		if (hwif->chipset == ide_unknown)
			return hwif;	/* pick an unused entry */
	}
	printk(KERN_ERR "%s: too many IDE interfaces, no room in table\n", name);
	return NULL;
}

/**
 *	ide_setup_pci_baseregs	-	place a PCI IDE controller native
 *	@dev: PCI device of interface to switch native
 *	@name: Name of interface
 *
 *	We attempt to place the PCI interface into PCI native mode. If
 *	we succeed the BARs are ok and the controller is in PCI mode.
 *	Returns 0 on success or an errno code. 
 *
 *	FIXME: if we program the interface and then fail to set the BARS
 *	we don't switch it back to legacy mode. Do we actually care ??
 */
 
static int ide_setup_pci_baseregs (struct pci_dev *dev, const char *name)
{
	u8 progif = 0;

	/*
	 * Place both IDE interfaces into PCI "native" mode:
	 */
	if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) ||
			 (progif & 5) != 5) {
		if ((progif & 0xa) != 0xa) {
			printk(KERN_INFO "%s: device not capable of full "
				"native PCI mode\n", name);
			return -EOPNOTSUPP;
		}
		printk("%s: placing both ports into native PCI mode\n", name);
		(void) pci_write_config_byte(dev, PCI_CLASS_PROG, progif|5);
		if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) ||
		    (progif & 5) != 5) {
			printk(KERN_ERR "%s: rewrite of PROGIF failed, wanted "
				"0x%04x, got 0x%04x\n",
				name, progif|5, progif);
			return -EOPNOTSUPP;
		}
	}
	return 0;
}

#ifdef CONFIG_BLK_DEV_IDEDMA_PCI
static void ide_pci_clear_simplex(unsigned long dma_base, const char *name)
{
	u8 dma_stat = inb(dma_base + 2);

	outb(dma_stat & 0x60, dma_base + 2);
	dma_stat = inb(dma_base + 2);
	if (dma_stat & 0x80)
		printk(KERN_INFO "%s: simplex device: DMA forced\n", name);
}

/**
 *	ide_get_or_set_dma_base		-	setup BMIBA
 *	@d: IDE port info
 *	@hwif: IDE interface
 *
 *	Fetch the DMA Bus-Master-I/O-Base-Address (BMIBA) from PCI space.
 *	Where a device has a partner that is already in DMA mode we check
 *	and enforce IDE simplex rules.
 */

static unsigned long ide_get_or_set_dma_base(const struct ide_port_info *d, ide_hwif_t *hwif)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	unsigned long dma_base = 0;
	u8 dma_stat = 0;

	if (hwif->mmio)
		return hwif->dma_base;

	if (hwif->mate && hwif->mate->dma_base) {
		dma_base = hwif->mate->dma_base - (hwif->channel ? 0 : 8);
	} else {
		u8 baridx = (d->host_flags & IDE_HFLAG_CS5520) ? 2 : 4;

		dma_base = pci_resource_start(dev, baridx);

		if (dma_base == 0) {
			printk(KERN_ERR "%s: DMA base is invalid\n", d->name);
			return 0;
		}
	}

	if (hwif->channel)
		dma_base += 8;

	if (d->host_flags & IDE_HFLAG_CS5520)
		goto out;

	if (d->host_flags & IDE_HFLAG_CLEAR_SIMPLEX) {
		ide_pci_clear_simplex(dma_base, d->name);
		goto out;
	}

	/*
	 * If the device claims "simplex" DMA, this means that only one of
	 * the two interfaces can be trusted with DMA at any point in time
	 * (so we should enable DMA only on one of the two interfaces).
	 *
	 * FIXME: At this point we haven't probed the drives so we can't make
	 * the appropriate decision.  Really we should defer this problem until
	 * we tune the drive then try to grab DMA ownership if we want to be
	 * the DMA end.  This has to be become dynamic to handle hot-plug.
	 */
	dma_stat = hwif->INB(dma_base + 2);
	if ((dma_stat & 0x80) && hwif->mate && hwif->mate->dma_base) {
		printk(KERN_INFO "%s: simplex device: DMA disabled\n", d->name);
		dma_base = 0;
	}
out:
	return dma_base;
}
#endif /* CONFIG_BLK_DEV_IDEDMA_PCI */

void ide_setup_pci_noise(struct pci_dev *dev, const struct ide_port_info *d)
{
	printk(KERN_INFO "%s: IDE controller (0x%04x:0x%04x rev 0x%02x) at "
			 " PCI slot %s\n", d->name, dev->vendor, dev->device,
			 dev->revision, pci_name(dev));
}

EXPORT_SYMBOL_GPL(ide_setup_pci_noise);


/**
 *	ide_pci_enable	-	do PCI enables
 *	@dev: PCI device
 *	@d: IDE port info
 *
 *	Enable the IDE PCI device. We attempt to enable the device in full
 *	but if that fails then we only need BAR4 so we will enable that.
 *	
 *	Returns zero on success or an error code
 */

static int ide_pci_enable(struct pci_dev *dev, const struct ide_port_info *d)
{
	int ret;

	if (pci_enable_device(dev)) {
		ret = pci_enable_device_bars(dev, 1 << 4);
		if (ret < 0) {
			printk(KERN_WARNING "%s: (ide_setup_pci_device:) "
				"Could not enable device.\n", d->name);
			goto out;
		}
		printk(KERN_WARNING "%s: BIOS configuration fixed.\n", d->name);
	}

	/*
	 * assume all devices can do 32-bit DMA for now, we can add
	 * a DMA mask field to the struct ide_port_info if we need it
	 * (or let lower level driver set the DMA mask)
	 */
	ret = pci_set_dma_mask(dev, DMA_32BIT_MASK);
	if (ret < 0) {
		printk(KERN_ERR "%s: can't set dma mask\n", d->name);
		goto out;
	}

	/* FIXME: Temporary - until we put in the hotplug interface logic
	   Check that the bits we want are not in use by someone else. */
	ret = pci_request_region(dev, 4, "ide_tmp");
	if (ret < 0)
		goto out;

	pci_release_region(dev, 4);
out:
	return ret;
}

/**
 *	ide_pci_configure	-	configure an unconfigured device
 *	@dev: PCI device
 *	@d: IDE port info
 *
 *	Enable and configure the PCI device we have been passed.
 *	Returns zero on success or an error code.
 */

static int ide_pci_configure(struct pci_dev *dev, const struct ide_port_info *d)
{
	u16 pcicmd = 0;
	/*
	 * PnP BIOS was *supposed* to have setup this device, but we
	 * can do it ourselves, so long as the BIOS has assigned an IRQ
	 * (or possibly the device is using a "legacy header" for IRQs).
	 * Maybe the user deliberately *disabled* the device,
	 * but we'll eventually ignore it again if no drives respond.
	 */
	if (ide_setup_pci_baseregs(dev, d->name) || pci_write_config_word(dev, PCI_COMMAND, pcicmd|PCI_COMMAND_IO)) 
	{
		printk(KERN_INFO "%s: device disabled (BIOS)\n", d->name);
		return -ENODEV;
	}
	if (pci_read_config_word(dev, PCI_COMMAND, &pcicmd)) {
		printk(KERN_ERR "%s: error accessing PCI regs\n", d->name);
		return -EIO;
	}
	if (!(pcicmd & PCI_COMMAND_IO)) {
		printk(KERN_ERR "%s: unable to enable IDE controller\n", d->name);
		return -ENXIO;
	}
	return 0;
}

/**
 *	ide_pci_check_iomem	-	check a register is I/O
 *	@dev: PCI device
 *	@d: IDE port info
 *	@bar: BAR number
 *
 *	Checks if a BAR is configured and points to MMIO space. If so
 *	print an error and return an error code. Otherwise return 0
 */

static int ide_pci_check_iomem(struct pci_dev *dev, const struct ide_port_info *d, int bar)
{
	ulong flags = pci_resource_flags(dev, bar);
	
	/* Unconfigured ? */
	if (!flags || pci_resource_len(dev, bar) == 0)
		return 0;

	/* I/O space */		
	if(flags & PCI_BASE_ADDRESS_IO_MASK)
		return 0;
		
	/* Bad */
	printk(KERN_ERR "%s: IO baseregs (BIOS) are reported "
			"as MEM, report to "
			"<andre@linux-ide.org>.\n", d->name);
	return -EINVAL;
}

/**
 *	ide_hwif_configure	-	configure an IDE interface
 *	@dev: PCI device holding interface
 *	@d: IDE port info
 *	@mate: Paired interface if any
 *
 *	Perform the initial set up for the hardware interface structure. This
 *	is done per interface port rather than per PCI device. There may be
 *	more than one port per device.
 *
 *	Returns the new hardware interface structure, or NULL on a failure
 */

static ide_hwif_t *ide_hwif_configure(struct pci_dev *dev, const struct ide_port_info *d, ide_hwif_t *mate, int port, int irq)
{
	unsigned long ctl = 0, base = 0;
	ide_hwif_t *hwif;
	u8 bootable = (d->host_flags & IDE_HFLAG_BOOTABLE) ? 1 : 0;
	u8 oldnoprobe = 0;
	struct hw_regs_s hw;

	if ((d->host_flags & IDE_HFLAG_ISA_PORTS) == 0) {
		/*  Possibly we should fail if these checks report true */
		ide_pci_check_iomem(dev, d, 2*port);
		ide_pci_check_iomem(dev, d, 2*port+1);
 
		ctl  = pci_resource_start(dev, 2*port+1);
		base = pci_resource_start(dev, 2*port);
		if ((ctl && !base) || (base && !ctl)) {
			printk(KERN_ERR "%s: inconsistent baseregs (BIOS) "
				"for port %d, skipping\n", d->name, port);
			return NULL;
		}
	}
	if (!ctl)
	{
		/* Use default values */
		ctl = port ? 0x374 : 0x3f4;
		base = port ? 0x170 : 0x1f0;
	}
	if ((hwif = ide_match_hwif(base, bootable, d->name)) == NULL)
		return NULL;	/* no room in ide_hwifs[] */

	memset(&hw, 0, sizeof(hw));
	hw.irq = hwif->irq ? hwif->irq : irq;
	hw.dev = &dev->dev;
	hw.chipset = d->chipset ? d->chipset : ide_pci;
	ide_std_init_ports(&hw, base, ctl | 2);

	if (hwif->io_ports[IDE_DATA_OFFSET] == base &&
	    hwif->io_ports[IDE_CONTROL_OFFSET] == (ctl | 2))
		oldnoprobe = hwif->noprobe;

	ide_init_port_hw(hwif, &hw);

	hwif->noprobe = oldnoprobe;

	hwif->dev = &dev->dev;
	hwif->cds = d;
	hwif->channel = port;

	if (mate) {
		hwif->mate = mate;
		mate->mate = hwif;
	}
	return hwif;
}

/**
 *	ide_hwif_setup_dma	-	configure DMA interface
 *	@dev: PCI device
 *	@d: IDE port info
 *	@hwif: IDE interface
 *
 *	Set up the DMA base for the interface. Enable the master bits as
 *	necessary and attempt to bring the device DMA into a ready to use
 *	state
 */

static void ide_hwif_setup_dma(struct pci_dev *dev, const struct ide_port_info *d, ide_hwif_t *hwif)
{
#ifdef CONFIG_BLK_DEV_IDEDMA_PCI
	u16 pcicmd;

	pci_read_config_word(dev, PCI_COMMAND, &pcicmd);

	if ((d->host_flags & IDE_HFLAG_NO_AUTODMA) == 0 ||
	    ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE &&
	     (dev->class & 0x80))) {
		unsigned long dma_base = ide_get_or_set_dma_base(d, hwif);
		if (dma_base && !(pcicmd & PCI_COMMAND_MASTER)) {
			/*
 			 * Set up BM-DMA capability
			 * (PnP BIOS should have done this)
 			 */
			pci_set_master(dev);
			if (pci_read_config_word(dev, PCI_COMMAND, &pcicmd) || !(pcicmd & PCI_COMMAND_MASTER)) {
				printk(KERN_ERR "%s: %s error updating PCICMD\n",
					hwif->name, d->name);
				dma_base = 0;
			}
		}
		if (dma_base) {
			if (d->init_dma) {
				d->init_dma(hwif, dma_base);
			} else {
				ide_setup_dma(hwif, dma_base);
			}
		} else {
			printk(KERN_INFO "%s: %s Bus-Master DMA disabled "
				"(BIOS)\n", hwif->name, d->name);
		}
	}
#endif /* CONFIG_BLK_DEV_IDEDMA_PCI*/
}

/**
 *	ide_setup_pci_controller	-	set up IDE PCI
 *	@dev: PCI device
 *	@d: IDE port info
 *	@noisy: verbose flag
 *	@config: returned as 1 if we configured the hardware
 *
 *	Set up the PCI and controller side of the IDE interface. This brings
 *	up the PCI side of the device, checks that the device is enabled
 *	and enables it if need be
 */

static int ide_setup_pci_controller(struct pci_dev *dev, const struct ide_port_info *d, int noisy, int *config)
{
	int ret;
	u16 pcicmd;

	if (noisy)
		ide_setup_pci_noise(dev, d);

	ret = ide_pci_enable(dev, d);
	if (ret < 0)
		goto out;

	ret = pci_read_config_word(dev, PCI_COMMAND, &pcicmd);
	if (ret < 0) {
		printk(KERN_ERR "%s: error accessing PCI regs\n", d->name);
		goto out;
	}
	if (!(pcicmd & PCI_COMMAND_IO)) {	/* is device disabled? */
		ret = ide_pci_configure(dev, d);
		if (ret < 0)
			goto out;
		*config = 1;
		printk(KERN_INFO "%s: device enabled (Linux)\n", d->name);
	}

out:
	return ret;
}

/**
 *	ide_pci_setup_ports	-	configure ports/devices on PCI IDE
 *	@dev: PCI device
 *	@d: IDE port info
 *	@pciirq: IRQ line
 *	@idx: ATA index table to update
 *
 *	Scan the interfaces attached to this device and do any
 *	necessary per port setup. Attach the devices and ask the
 *	generic DMA layer to do its work for us.
 *
 *	Normally called automaticall from do_ide_pci_setup_device,
 *	but is also used directly as a helper function by some controllers
 *	where the chipset setup is not the default PCI IDE one.
 */

void ide_pci_setup_ports(struct pci_dev *dev, const struct ide_port_info *d, int pciirq, u8 *idx)
{
	int channels = (d->host_flags & IDE_HFLAG_SINGLE) ? 1 : 2, port;
	ide_hwif_t *hwif, *mate = NULL;
	u8 tmp;

	/*
	 * Set up the IDE ports
	 */

	for (port = 0; port < channels; ++port) {
		const ide_pci_enablebit_t *e = &(d->enablebits[port]);

		if (e->reg && (pci_read_config_byte(dev, e->reg, &tmp) ||
		    (tmp & e->mask) != e->val)) {
			printk(KERN_INFO "%s: IDE port disabled\n", d->name);
			continue;	/* port not enabled */
		}

		if ((hwif = ide_hwif_configure(dev, d, mate, port, pciirq)) == NULL)
			continue;

		*(idx + port) = hwif->index;

		if (d->init_iops)
			d->init_iops(hwif);

		if ((d->host_flags & IDE_HFLAG_NO_DMA) == 0)
			ide_hwif_setup_dma(dev, d, hwif);

		if ((!hwif->irq && (d->host_flags & IDE_HFLAG_LEGACY_IRQS)) ||
		    (d->host_flags & IDE_HFLAG_FORCE_LEGACY_IRQS))
			hwif->irq = port ? 15 : 14;

		hwif->host_flags = d->host_flags;
		hwif->pio_mask = d->pio_mask;

		if ((d->host_flags & IDE_HFLAG_SERIALIZE) && hwif->mate)
			hwif->mate->serialized = hwif->serialized = 1;

		if (d->host_flags & IDE_HFLAG_IO_32BIT) {
			hwif->drives[0].io_32bit = 1;
			hwif->drives[1].io_32bit = 1;
		}

		if (d->host_flags & IDE_HFLAG_UNMASK_IRQS) {
			hwif->drives[0].unmask = 1;
			hwif->drives[1].unmask = 1;
		}

		if (hwif->dma_base) {
			hwif->swdma_mask = d->swdma_mask;
			hwif->mwdma_mask = d->mwdma_mask;
			hwif->ultra_mask = d->udma_mask;
		}

		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;

		if (d->host_flags & IDE_HFLAG_RQSIZE_256)
			hwif->rqsize = 256;

		if (d->init_hwif)
			/* Call chipset-specific routine
			 * for each enabled hwif
			 */
			d->init_hwif(hwif);

		mate = hwif;
	}
}

EXPORT_SYMBOL_GPL(ide_pci_setup_ports);

/*
 * ide_setup_pci_device() looks at the primary/secondary interfaces
 * on a PCI IDE device and, if they are enabled, prepares the IDE driver
 * for use with them.  This generic code works for most PCI chipsets.
 *
 * One thing that is not standardized is the location of the
 * primary/secondary interface "enable/disable" bits.  For chipsets that
 * we "know" about, this information is in the struct ide_port_info;
 * for all other chipsets, we just assume both interfaces are enabled.
 */
static int do_ide_setup_pci_device(struct pci_dev *dev,
				   const struct ide_port_info *d,
				   u8 *idx, u8 noisy)
{
	int tried_config = 0;
	int pciirq, ret;

	ret = ide_setup_pci_controller(dev, d, noisy, &tried_config);
	if (ret < 0)
		goto out;

	/*
	 * Can we trust the reported IRQ?
	 */
	pciirq = dev->irq;

	/* Is it an "IDE storage" device in non-PCI mode? */
	if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE && (dev->class & 5) != 5) {
		if (noisy)
			printk(KERN_INFO "%s: not 100%% native mode: "
				"will probe irqs later\n", d->name);
		/*
		 * This allows offboard ide-pci cards the enable a BIOS,
		 * verify interrupt settings of split-mirror pci-config
		 * space, place chipset into init-mode, and/or preserve
		 * an interrupt if the card is not native ide support.
		 */
		ret = d->init_chipset ? d->init_chipset(dev, d->name) : 0;
		if (ret < 0)
			goto out;
		pciirq = ret;
	} else if (tried_config) {
		if (noisy)
			printk(KERN_INFO "%s: will probe irqs later\n", d->name);
		pciirq = 0;
	} else if (!pciirq) {
		if (noisy)
			printk(KERN_WARNING "%s: bad irq (%d): will probe later\n",
				d->name, pciirq);
		pciirq = 0;
	} else {
		if (d->init_chipset) {
			ret = d->init_chipset(dev, d->name);
			if (ret < 0)
				goto out;
		}
		if (noisy)
			printk(KERN_INFO "%s: 100%% native mode on irq %d\n",
				d->name, pciirq);
	}

	/* FIXME: silent failure can happen */

	ide_pci_setup_ports(dev, d, pciirq, idx);
out:
	return ret;
}

int ide_setup_pci_device(struct pci_dev *dev, const struct ide_port_info *d)
{
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };
	int ret;

	ret = do_ide_setup_pci_device(dev, d, &idx[0], 1);

	if (ret >= 0)
		ide_device_add(idx);

	return ret;
}

EXPORT_SYMBOL_GPL(ide_setup_pci_device);

int ide_setup_pci_devices(struct pci_dev *dev1, struct pci_dev *dev2,
			  const struct ide_port_info *d)
{
	struct pci_dev *pdev[] = { dev1, dev2 };
	int ret, i;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };

	for (i = 0; i < 2; i++) {
		ret = do_ide_setup_pci_device(pdev[i], d, &idx[i*2], !i);
		/*
		 * FIXME: Mom, mom, they stole me the helper function to undo
		 * do_ide_setup_pci_device() on the first device!
		 */
		if (ret < 0)
			goto out;
	}

	ide_device_add(idx);
out:
	return ret;
}

EXPORT_SYMBOL_GPL(ide_setup_pci_devices);
