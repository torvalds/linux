/*
 *  Copyright (C) 1998-2000  Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 1995-1998  Mark Lord
 *  Copyright (C)      2007  Bartlomiej Zolnierkiewicz
 *
 *  May be copied or modified under the terms of the GNU General Public License
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ide.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>

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

static int ide_setup_pci_baseregs(struct pci_dev *dev, const char *name)
{
	u8 progif = 0;

	/*
	 * Place both IDE interfaces into PCI "native" mode:
	 */
	if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) ||
			 (progif & 5) != 5) {
		if ((progif & 0xa) != 0xa) {
			printk(KERN_INFO "%s %s: device not capable of full "
				"native PCI mode\n", name, pci_name(dev));
			return -EOPNOTSUPP;
		}
		printk(KERN_INFO "%s %s: placing both ports into native PCI "
			"mode\n", name, pci_name(dev));
		(void) pci_write_config_byte(dev, PCI_CLASS_PROG, progif|5);
		if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) ||
		    (progif & 5) != 5) {
			printk(KERN_ERR "%s %s: rewrite of PROGIF failed, "
				"wanted 0x%04x, got 0x%04x\n",
				name, pci_name(dev), progif | 5, progif);
			return -EOPNOTSUPP;
		}
	}
	return 0;
}

#ifdef CONFIG_BLK_DEV_IDEDMA_PCI
static int ide_pci_clear_simplex(unsigned long dma_base, const char *name)
{
	u8 dma_stat = inb(dma_base + 2);

	outb(dma_stat & 0x60, dma_base + 2);
	dma_stat = inb(dma_base + 2);

	return (dma_stat & 0x80) ? 1 : 0;
}

/**
 *	ide_pci_dma_base	-	setup BMIBA
 *	@hwif: IDE interface
 *	@d: IDE port info
 *
 *	Fetch the DMA Bus-Master-I/O-Base-Address (BMIBA) from PCI space.
 */

unsigned long ide_pci_dma_base(ide_hwif_t *hwif, const struct ide_port_info *d)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	unsigned long dma_base = 0;

	if (hwif->host_flags & IDE_HFLAG_MMIO)
		return hwif->dma_base;

	if (hwif->mate && hwif->mate->dma_base) {
		dma_base = hwif->mate->dma_base - (hwif->channel ? 0 : 8);
	} else {
		u8 baridx = (d->host_flags & IDE_HFLAG_CS5520) ? 2 : 4;

		dma_base = pci_resource_start(dev, baridx);

		if (dma_base == 0) {
			printk(KERN_ERR "%s %s: DMA base is invalid\n",
				d->name, pci_name(dev));
			return 0;
		}
	}

	if (hwif->channel)
		dma_base += 8;

	return dma_base;
}
EXPORT_SYMBOL_GPL(ide_pci_dma_base);

int ide_pci_check_simplex(ide_hwif_t *hwif, const struct ide_port_info *d)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);
	u8 dma_stat;

	if (d->host_flags & (IDE_HFLAG_MMIO | IDE_HFLAG_CS5520))
		goto out;

	if (d->host_flags & IDE_HFLAG_CLEAR_SIMPLEX) {
		if (ide_pci_clear_simplex(hwif->dma_base, d->name))
			printk(KERN_INFO "%s %s: simplex device: DMA forced\n",
				d->name, pci_name(dev));
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
	dma_stat = hwif->dma_ops->dma_sff_read_status(hwif);
	if ((dma_stat & 0x80) && hwif->mate && hwif->mate->dma_base) {
		printk(KERN_INFO "%s %s: simplex device: DMA disabled\n",
			d->name, pci_name(dev));
		return -1;
	}
out:
	return 0;
}
EXPORT_SYMBOL_GPL(ide_pci_check_simplex);

/*
 * Set up BM-DMA capability (PnP BIOS should have done this)
 */
int ide_pci_set_master(struct pci_dev *dev, const char *name)
{
	u16 pcicmd;

	pci_read_config_word(dev, PCI_COMMAND, &pcicmd);

	if ((pcicmd & PCI_COMMAND_MASTER) == 0) {
		pci_set_master(dev);

		if (pci_read_config_word(dev, PCI_COMMAND, &pcicmd) ||
		    (pcicmd & PCI_COMMAND_MASTER) == 0) {
			printk(KERN_ERR "%s %s: error updating PCICMD\n",
				name, pci_name(dev));
			return -EIO;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ide_pci_set_master);
#endif /* CONFIG_BLK_DEV_IDEDMA_PCI */

void ide_setup_pci_noise(struct pci_dev *dev, const struct ide_port_info *d)
{
	printk(KERN_INFO "%s %s: IDE controller (0x%04x:0x%04x rev 0x%02x)\n",
		d->name, pci_name(dev),
		dev->vendor, dev->device, dev->revision);
}
EXPORT_SYMBOL_GPL(ide_setup_pci_noise);


/**
 *	ide_pci_enable	-	do PCI enables
 *	@dev: PCI device
 *	@d: IDE port info
 *
 *	Enable the IDE PCI device. We attempt to enable the device in full
 *	but if that fails then we only need IO space. The PCI code should
 *	have setup the proper resources for us already for controllers in
 *	legacy mode.
 *
 *	Returns zero on success or an error code
 */

static int ide_pci_enable(struct pci_dev *dev, const struct ide_port_info *d)
{
	int ret, bars;

	if (pci_enable_device(dev)) {
		ret = pci_enable_device_io(dev);
		if (ret < 0) {
			printk(KERN_WARNING "%s %s: couldn't enable device\n",
				d->name, pci_name(dev));
			goto out;
		}
		printk(KERN_WARNING "%s %s: BIOS configuration fixed\n",
			d->name, pci_name(dev));
	}

	/*
	 * assume all devices can do 32-bit DMA for now, we can add
	 * a DMA mask field to the struct ide_port_info if we need it
	 * (or let lower level driver set the DMA mask)
	 */
	ret = pci_set_dma_mask(dev, DMA_BIT_MASK(32));
	if (ret < 0) {
		printk(KERN_ERR "%s %s: can't set DMA mask\n",
			d->name, pci_name(dev));
		goto out;
	}

	if (d->host_flags & IDE_HFLAG_SINGLE)
		bars = (1 << 2) - 1;
	else
		bars = (1 << 4) - 1;

	if ((d->host_flags & IDE_HFLAG_NO_DMA) == 0) {
		if (d->host_flags & IDE_HFLAG_CS5520)
			bars |= (1 << 2);
		else
			bars |= (1 << 4);
	}

	ret = pci_request_selected_regions(dev, bars, d->name);
	if (ret < 0)
		printk(KERN_ERR "%s %s: can't reserve resources\n",
			d->name, pci_name(dev));
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
	if (ide_setup_pci_baseregs(dev, d->name) ||
	    pci_write_config_word(dev, PCI_COMMAND, pcicmd | PCI_COMMAND_IO)) {
		printk(KERN_INFO "%s %s: device disabled (BIOS)\n",
			d->name, pci_name(dev));
		return -ENODEV;
	}
	if (pci_read_config_word(dev, PCI_COMMAND, &pcicmd)) {
		printk(KERN_ERR "%s %s: error accessing PCI regs\n",
			d->name, pci_name(dev));
		return -EIO;
	}
	if (!(pcicmd & PCI_COMMAND_IO)) {
		printk(KERN_ERR "%s %s: unable to enable IDE controller\n",
			d->name, pci_name(dev));
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
 *	Checks if a BAR is configured and points to MMIO space. If so,
 *	return an error code. Otherwise return 0
 */

static int ide_pci_check_iomem(struct pci_dev *dev, const struct ide_port_info *d,
			       int bar)
{
	ulong flags = pci_resource_flags(dev, bar);

	/* Unconfigured ? */
	if (!flags || pci_resource_len(dev, bar) == 0)
		return 0;

	/* I/O space */
	if (flags & IORESOURCE_IO)
		return 0;

	/* Bad */
	return -EINVAL;
}

/**
 *	ide_hw_configure	-	configure a hw_regs_t instance
 *	@dev: PCI device holding interface
 *	@d: IDE port info
 *	@port: port number
 *	@hw: hw_regs_t instance corresponding to this port
 *
 *	Perform the initial set up for the hardware interface structure. This
 *	is done per interface port rather than per PCI device. There may be
 *	more than one port per device.
 *
 *	Returns zero on success or an error code.
 */

static int ide_hw_configure(struct pci_dev *dev, const struct ide_port_info *d,
			    unsigned int port, hw_regs_t *hw)
{
	unsigned long ctl = 0, base = 0;

	if ((d->host_flags & IDE_HFLAG_ISA_PORTS) == 0) {
		if (ide_pci_check_iomem(dev, d, 2 * port) ||
		    ide_pci_check_iomem(dev, d, 2 * port + 1)) {
			printk(KERN_ERR "%s %s: I/O baseregs (BIOS) are "
				"reported as MEM for port %d!\n",
				d->name, pci_name(dev), port);
			return -EINVAL;
		}

		ctl  = pci_resource_start(dev, 2*port+1);
		base = pci_resource_start(dev, 2*port);
	} else {
		/* Use default values */
		ctl = port ? 0x374 : 0x3f4;
		base = port ? 0x170 : 0x1f0;
	}

	if (!base || !ctl) {
		printk(KERN_ERR "%s %s: bad PCI BARs for port %d, skipping\n",
			d->name, pci_name(dev), port);
		return -EINVAL;
	}

	memset(hw, 0, sizeof(*hw));
	hw->dev = &dev->dev;
	hw->chipset = d->chipset ? d->chipset : ide_pci;
	ide_std_init_ports(hw, base, ctl | 2);

	return 0;
}

#ifdef CONFIG_BLK_DEV_IDEDMA_PCI
/**
 *	ide_hwif_setup_dma	-	configure DMA interface
 *	@hwif: IDE interface
 *	@d: IDE port info
 *
 *	Set up the DMA base for the interface. Enable the master bits as
 *	necessary and attempt to bring the device DMA into a ready to use
 *	state
 */

int ide_hwif_setup_dma(ide_hwif_t *hwif, const struct ide_port_info *d)
{
	struct pci_dev *dev = to_pci_dev(hwif->dev);

	if ((d->host_flags & IDE_HFLAG_NO_AUTODMA) == 0 ||
	    ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE &&
	     (dev->class & 0x80))) {
		unsigned long base = ide_pci_dma_base(hwif, d);

		if (base == 0)
			return -1;

		hwif->dma_base = base;

		if (hwif->dma_ops == NULL)
			hwif->dma_ops = &sff_dma_ops;

		if (ide_pci_check_simplex(hwif, d) < 0)
			return -1;

		if (ide_pci_set_master(dev, d->name) < 0)
			return -1;

		if (hwif->host_flags & IDE_HFLAG_MMIO)
			printk(KERN_INFO "    %s: MMIO-DMA\n", hwif->name);
		else
			printk(KERN_INFO "    %s: BM-DMA at 0x%04lx-0x%04lx\n",
					 hwif->name, base, base + 7);

		hwif->extra_base = base + (hwif->channel ? 8 : 16);

		if (ide_allocate_dma_engine(hwif))
			return -1;
	}

	return 0;
}
#endif /* CONFIG_BLK_DEV_IDEDMA_PCI */

/**
 *	ide_setup_pci_controller	-	set up IDE PCI
 *	@dev: PCI device
 *	@d: IDE port info
 *	@noisy: verbose flag
 *
 *	Set up the PCI and controller side of the IDE interface. This brings
 *	up the PCI side of the device, checks that the device is enabled
 *	and enables it if need be
 */

static int ide_setup_pci_controller(struct pci_dev *dev,
				    const struct ide_port_info *d, int noisy)
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
		printk(KERN_ERR "%s %s: error accessing PCI regs\n",
			d->name, pci_name(dev));
		goto out;
	}
	if (!(pcicmd & PCI_COMMAND_IO)) {	/* is device disabled? */
		ret = ide_pci_configure(dev, d);
		if (ret < 0)
			goto out;
		printk(KERN_INFO "%s %s: device enabled (Linux)\n",
			d->name, pci_name(dev));
	}

out:
	return ret;
}

/**
 *	ide_pci_setup_ports	-	configure ports/devices on PCI IDE
 *	@dev: PCI device
 *	@d: IDE port info
 *	@hw: hw_regs_t instances corresponding to this PCI IDE device
 *	@hws: hw_regs_t pointers table to update
 *
 *	Scan the interfaces attached to this device and do any
 *	necessary per port setup. Attach the devices and ask the
 *	generic DMA layer to do its work for us.
 *
 *	Normally called automaticall from do_ide_pci_setup_device,
 *	but is also used directly as a helper function by some controllers
 *	where the chipset setup is not the default PCI IDE one.
 */

void ide_pci_setup_ports(struct pci_dev *dev, const struct ide_port_info *d,
			 hw_regs_t *hw, hw_regs_t **hws)
{
	int channels = (d->host_flags & IDE_HFLAG_SINGLE) ? 1 : 2, port;
	u8 tmp;

	/*
	 * Set up the IDE ports
	 */

	for (port = 0; port < channels; ++port) {
		const struct ide_pci_enablebit *e = &d->enablebits[port];

		if (e->reg && (pci_read_config_byte(dev, e->reg, &tmp) ||
		    (tmp & e->mask) != e->val)) {
			printk(KERN_INFO "%s %s: IDE port disabled\n",
				d->name, pci_name(dev));
			continue;	/* port not enabled */
		}

		if (ide_hw_configure(dev, d, port, hw + port))
			continue;

		*(hws + port) = hw + port;
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
				   u8 noisy)
{
	int pciirq, ret;

	/*
	 * Can we trust the reported IRQ?
	 */
	pciirq = dev->irq;

	/*
	 * This allows offboard ide-pci cards the enable a BIOS,
	 * verify interrupt settings of split-mirror pci-config
	 * space, place chipset into init-mode, and/or preserve
	 * an interrupt if the card is not native ide support.
	 */
	ret = d->init_chipset ? d->init_chipset(dev) : 0;
	if (ret < 0)
		goto out;

	if (ide_pci_is_in_compatibility_mode(dev)) {
		if (noisy)
			printk(KERN_INFO "%s %s: not 100%% native mode: will "
				"probe irqs later\n", d->name, pci_name(dev));
		pciirq = 0;
	} else if (!pciirq && noisy) {
		printk(KERN_WARNING "%s %s: bad irq (%d): will probe later\n",
			d->name, pci_name(dev), pciirq);
	} else if (noisy) {
		printk(KERN_INFO "%s %s: 100%% native mode on irq %d\n",
			d->name, pci_name(dev), pciirq);
	}

	ret = pciirq;
out:
	return ret;
}

int ide_pci_init_one(struct pci_dev *dev, const struct ide_port_info *d,
		     void *priv)
{
	struct ide_host *host;
	hw_regs_t hw[4], *hws[] = { NULL, NULL, NULL, NULL };
	int ret;

	ret = ide_setup_pci_controller(dev, d, 1);
	if (ret < 0)
		goto out;

	ide_pci_setup_ports(dev, d, &hw[0], &hws[0]);

	host = ide_host_alloc(d, hws);
	if (host == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	host->dev[0] = &dev->dev;

	host->host_priv = priv;

	host->irq_flags = IRQF_SHARED;

	pci_set_drvdata(dev, host);

	ret = do_ide_setup_pci_device(dev, d, 1);
	if (ret < 0)
		goto out;

	/* fixup IRQ */
	if (ide_pci_is_in_compatibility_mode(dev)) {
		hw[0].irq = pci_get_legacy_ide_irq(dev, 0);
		hw[1].irq = pci_get_legacy_ide_irq(dev, 1);
	} else
		hw[1].irq = hw[0].irq = ret;

	ret = ide_host_register(host, d, hws);
	if (ret)
		ide_host_free(host);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(ide_pci_init_one);

int ide_pci_init_two(struct pci_dev *dev1, struct pci_dev *dev2,
		     const struct ide_port_info *d, void *priv)
{
	struct pci_dev *pdev[] = { dev1, dev2 };
	struct ide_host *host;
	int ret, i;
	hw_regs_t hw[4], *hws[] = { NULL, NULL, NULL, NULL };

	for (i = 0; i < 2; i++) {
		ret = ide_setup_pci_controller(pdev[i], d, !i);
		if (ret < 0)
			goto out;

		ide_pci_setup_ports(pdev[i], d, &hw[i*2], &hws[i*2]);
	}

	host = ide_host_alloc(d, hws);
	if (host == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	host->dev[0] = &dev1->dev;
	host->dev[1] = &dev2->dev;

	host->host_priv = priv;

	host->irq_flags = IRQF_SHARED;

	pci_set_drvdata(pdev[0], host);
	pci_set_drvdata(pdev[1], host);

	for (i = 0; i < 2; i++) {
		ret = do_ide_setup_pci_device(pdev[i], d, !i);

		/*
		 * FIXME: Mom, mom, they stole me the helper function to undo
		 * do_ide_setup_pci_device() on the first device!
		 */
		if (ret < 0)
			goto out;

		/* fixup IRQ */
		if (ide_pci_is_in_compatibility_mode(pdev[i])) {
			hw[i*2].irq = pci_get_legacy_ide_irq(pdev[i], 0);
			hw[i*2 + 1].irq = pci_get_legacy_ide_irq(pdev[i], 1);
		} else
			hw[i*2 + 1].irq = hw[i*2].irq = ret;
	}

	ret = ide_host_register(host, d, hws);
	if (ret)
		ide_host_free(host);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(ide_pci_init_two);

void ide_pci_remove(struct pci_dev *dev)
{
	struct ide_host *host = pci_get_drvdata(dev);
	struct pci_dev *dev2 = host->dev[1] ? to_pci_dev(host->dev[1]) : NULL;
	int bars;

	if (host->host_flags & IDE_HFLAG_SINGLE)
		bars = (1 << 2) - 1;
	else
		bars = (1 << 4) - 1;

	if ((host->host_flags & IDE_HFLAG_NO_DMA) == 0) {
		if (host->host_flags & IDE_HFLAG_CS5520)
			bars |= (1 << 2);
		else
			bars |= (1 << 4);
	}

	ide_host_remove(host);

	if (dev2)
		pci_release_selected_regions(dev2, bars);
	pci_release_selected_regions(dev, bars);

	if (dev2)
		pci_disable_device(dev2);
	pci_disable_device(dev);
}
EXPORT_SYMBOL_GPL(ide_pci_remove);

#ifdef CONFIG_PM
int ide_pci_suspend(struct pci_dev *dev, pm_message_t state)
{
	pci_save_state(dev);
	pci_disable_device(dev);
	pci_set_power_state(dev, pci_choose_state(dev, state));

	return 0;
}
EXPORT_SYMBOL_GPL(ide_pci_suspend);

int ide_pci_resume(struct pci_dev *dev)
{
	struct ide_host *host = pci_get_drvdata(dev);
	int rc;

	pci_set_power_state(dev, PCI_D0);

	rc = pci_enable_device(dev);
	if (rc)
		return rc;

	pci_restore_state(dev);
	pci_set_master(dev);

	if (host->init_chipset)
		host->init_chipset(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(ide_pci_resume);
#endif
