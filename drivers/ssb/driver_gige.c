/*
 * Sonics Silicon Backplane
 * Broadcom Gigabit Ethernet core driver
 *
 * Copyright 2008, Broadcom Corporation
 * Copyright 2008, Michael Buesch <m@bues.ch>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_driver_gige.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/slab.h>


/*
MODULE_DESCRIPTION("SSB Broadcom Gigabit Ethernet driver");
MODULE_AUTHOR("Michael Buesch");
MODULE_LICENSE("GPL");
*/

static const struct ssb_device_id ssb_gige_tbl[] = {
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_ETHERNET_GBIT, SSB_ANY_REV),
	SSB_DEVTABLE_END
};
/* MODULE_DEVICE_TABLE(ssb, ssb_gige_tbl); */


static inline u8 gige_read8(struct ssb_gige *dev, u16 offset)
{
	return ssb_read8(dev->dev, offset);
}

static inline u16 gige_read16(struct ssb_gige *dev, u16 offset)
{
	return ssb_read16(dev->dev, offset);
}

static inline u32 gige_read32(struct ssb_gige *dev, u16 offset)
{
	return ssb_read32(dev->dev, offset);
}

static inline void gige_write8(struct ssb_gige *dev,
			       u16 offset, u8 value)
{
	ssb_write8(dev->dev, offset, value);
}

static inline void gige_write16(struct ssb_gige *dev,
				u16 offset, u16 value)
{
	ssb_write16(dev->dev, offset, value);
}

static inline void gige_write32(struct ssb_gige *dev,
				u16 offset, u32 value)
{
	ssb_write32(dev->dev, offset, value);
}

static inline
u8 gige_pcicfg_read8(struct ssb_gige *dev, unsigned int offset)
{
	BUG_ON(offset >= 256);
	return gige_read8(dev, SSB_GIGE_PCICFG + offset);
}

static inline
u16 gige_pcicfg_read16(struct ssb_gige *dev, unsigned int offset)
{
	BUG_ON(offset >= 256);
	return gige_read16(dev, SSB_GIGE_PCICFG + offset);
}

static inline
u32 gige_pcicfg_read32(struct ssb_gige *dev, unsigned int offset)
{
	BUG_ON(offset >= 256);
	return gige_read32(dev, SSB_GIGE_PCICFG + offset);
}

static inline
void gige_pcicfg_write8(struct ssb_gige *dev,
			unsigned int offset, u8 value)
{
	BUG_ON(offset >= 256);
	gige_write8(dev, SSB_GIGE_PCICFG + offset, value);
}

static inline
void gige_pcicfg_write16(struct ssb_gige *dev,
			 unsigned int offset, u16 value)
{
	BUG_ON(offset >= 256);
	gige_write16(dev, SSB_GIGE_PCICFG + offset, value);
}

static inline
void gige_pcicfg_write32(struct ssb_gige *dev,
			 unsigned int offset, u32 value)
{
	BUG_ON(offset >= 256);
	gige_write32(dev, SSB_GIGE_PCICFG + offset, value);
}

static int __devinit ssb_gige_pci_read_config(struct pci_bus *bus,
					      unsigned int devfn, int reg,
					      int size, u32 *val)
{
	struct ssb_gige *dev = container_of(bus->ops, struct ssb_gige, pci_ops);
	unsigned long flags;

	if ((PCI_SLOT(devfn) > 0) || (PCI_FUNC(devfn) > 0))
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (reg >= 256)
		return PCIBIOS_DEVICE_NOT_FOUND;

	spin_lock_irqsave(&dev->lock, flags);
	switch (size) {
	case 1:
		*val = gige_pcicfg_read8(dev, reg);
		break;
	case 2:
		*val = gige_pcicfg_read16(dev, reg);
		break;
	case 4:
		*val = gige_pcicfg_read32(dev, reg);
		break;
	default:
		WARN_ON(1);
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	return PCIBIOS_SUCCESSFUL;
}

static int __devinit ssb_gige_pci_write_config(struct pci_bus *bus,
					       unsigned int devfn, int reg,
					       int size, u32 val)
{
	struct ssb_gige *dev = container_of(bus->ops, struct ssb_gige, pci_ops);
	unsigned long flags;

	if ((PCI_SLOT(devfn) > 0) || (PCI_FUNC(devfn) > 0))
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (reg >= 256)
		return PCIBIOS_DEVICE_NOT_FOUND;

	spin_lock_irqsave(&dev->lock, flags);
	switch (size) {
	case 1:
		gige_pcicfg_write8(dev, reg, val);
		break;
	case 2:
		gige_pcicfg_write16(dev, reg, val);
		break;
	case 4:
		gige_pcicfg_write32(dev, reg, val);
		break;
	default:
		WARN_ON(1);
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	return PCIBIOS_SUCCESSFUL;
}

static int __devinit ssb_gige_probe(struct ssb_device *sdev,
				    const struct ssb_device_id *id)
{
	struct ssb_gige *dev;
	u32 base, tmslow, tmshigh;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->dev = sdev;

	spin_lock_init(&dev->lock);
	dev->pci_controller.pci_ops = &dev->pci_ops;
	dev->pci_controller.io_resource = &dev->io_resource;
	dev->pci_controller.mem_resource = &dev->mem_resource;
	dev->pci_controller.io_map_base = 0x800;
	dev->pci_ops.read = ssb_gige_pci_read_config;
	dev->pci_ops.write = ssb_gige_pci_write_config;

	dev->io_resource.name = SSB_GIGE_IO_RES_NAME;
	dev->io_resource.start = 0x800;
	dev->io_resource.end = 0x8FF;
	dev->io_resource.flags = IORESOURCE_IO | IORESOURCE_PCI_FIXED;

	if (!ssb_device_is_enabled(sdev))
		ssb_device_enable(sdev, 0);

	/* Setup BAR0. This is a 64k MMIO region. */
	base = ssb_admatch_base(ssb_read32(sdev, SSB_ADMATCH1));
	gige_pcicfg_write32(dev, PCI_BASE_ADDRESS_0, base);
	gige_pcicfg_write32(dev, PCI_BASE_ADDRESS_1, 0);

	dev->mem_resource.name = SSB_GIGE_MEM_RES_NAME;
	dev->mem_resource.start = base;
	dev->mem_resource.end = base + 0x10000 - 1;
	dev->mem_resource.flags = IORESOURCE_MEM | IORESOURCE_PCI_FIXED;

	/* Enable the memory region. */
	gige_pcicfg_write16(dev, PCI_COMMAND,
			    gige_pcicfg_read16(dev, PCI_COMMAND)
			    | PCI_COMMAND_MEMORY);

	/* Write flushing is controlled by the Flush Status Control register.
	 * We want to flush every register write with a timeout and we want
	 * to disable the IRQ mask while flushing to avoid concurrency.
	 * Note that automatic write flushing does _not_ work from
	 * an IRQ handler. The driver must flush manually by reading a register.
	 */
	gige_write32(dev, SSB_GIGE_SHIM_FLUSHSTAT, 0x00000068);

	/* Check if we have an RGMII or GMII PHY-bus.
	 * On RGMII do not bypass the DLLs */
	tmslow = ssb_read32(sdev, SSB_TMSLOW);
	tmshigh = ssb_read32(sdev, SSB_TMSHIGH);
	if (tmshigh & SSB_GIGE_TMSHIGH_RGMII) {
		tmslow &= ~SSB_GIGE_TMSLOW_TXBYPASS;
		tmslow &= ~SSB_GIGE_TMSLOW_RXBYPASS;
		dev->has_rgmii = 1;
	} else {
		tmslow |= SSB_GIGE_TMSLOW_TXBYPASS;
		tmslow |= SSB_GIGE_TMSLOW_RXBYPASS;
		dev->has_rgmii = 0;
	}
	tmslow |= SSB_GIGE_TMSLOW_DLLEN;
	ssb_write32(sdev, SSB_TMSLOW, tmslow);

	ssb_set_drvdata(sdev, dev);
	register_pci_controller(&dev->pci_controller);

	return 0;
}

bool pdev_is_ssb_gige_core(struct pci_dev *pdev)
{
	if (!pdev->resource[0].name)
		return 0;
	return (strcmp(pdev->resource[0].name, SSB_GIGE_MEM_RES_NAME) == 0);
}
EXPORT_SYMBOL(pdev_is_ssb_gige_core);

int ssb_gige_pcibios_plat_dev_init(struct ssb_device *sdev,
				   struct pci_dev *pdev)
{
	struct ssb_gige *dev = ssb_get_drvdata(sdev);
	struct resource *res;

	if (pdev->bus->ops != &dev->pci_ops) {
		/* The PCI device is not on this SSB GigE bridge device. */
		return -ENODEV;
	}

	/* Fixup the PCI resources. */
	res = &(pdev->resource[0]);
	res->flags = IORESOURCE_MEM | IORESOURCE_PCI_FIXED;
	res->name = dev->mem_resource.name;
	res->start = dev->mem_resource.start;
	res->end = dev->mem_resource.end;

	/* Fixup interrupt lines. */
	pdev->irq = ssb_mips_irq(sdev) + 2;
	pci_write_config_byte(pdev, PCI_INTERRUPT_LINE, pdev->irq);

	return 0;
}

int ssb_gige_map_irq(struct ssb_device *sdev,
		     const struct pci_dev *pdev)
{
	struct ssb_gige *dev = ssb_get_drvdata(sdev);

	if (pdev->bus->ops != &dev->pci_ops) {
		/* The PCI device is not on this SSB GigE bridge device. */
		return -ENODEV;
	}

	return ssb_mips_irq(sdev) + 2;
}

static struct ssb_driver ssb_gige_driver = {
	.name		= "BCM-GigE",
	.id_table	= ssb_gige_tbl,
	.probe		= ssb_gige_probe,
};

int ssb_gige_init(void)
{
	return ssb_driver_register(&ssb_gige_driver);
}
