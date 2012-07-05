/*
 * Broadcom specific AMBA
 * PCI Host
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/slab.h>
#include <linux/bcma/bcma.h>
#include <linux/pci.h>
#include <linux/module.h>

static void bcma_host_pci_switch_core(struct bcma_device *core)
{
	pci_write_config_dword(core->bus->host_pci, BCMA_PCI_BAR0_WIN,
			       core->addr);
	pci_write_config_dword(core->bus->host_pci, BCMA_PCI_BAR0_WIN2,
			       core->wrap);
	core->bus->mapped_core = core;
	bcma_debug(core->bus, "Switched to core: 0x%X\n", core->id.id);
}

/* Provides access to the requested core. Returns base offset that has to be
 * used. It makes use of fixed windows when possible. */
static u16 bcma_host_pci_provide_access_to_core(struct bcma_device *core)
{
	switch (core->id.id) {
	case BCMA_CORE_CHIPCOMMON:
		return 3 * BCMA_CORE_SIZE;
	case BCMA_CORE_PCIE:
		return 2 * BCMA_CORE_SIZE;
	}

	if (core->bus->mapped_core != core)
		bcma_host_pci_switch_core(core);
	return 0;
}

static u8 bcma_host_pci_read8(struct bcma_device *core, u16 offset)
{
	offset += bcma_host_pci_provide_access_to_core(core);
	return ioread8(core->bus->mmio + offset);
}

static u16 bcma_host_pci_read16(struct bcma_device *core, u16 offset)
{
	offset += bcma_host_pci_provide_access_to_core(core);
	return ioread16(core->bus->mmio + offset);
}

static u32 bcma_host_pci_read32(struct bcma_device *core, u16 offset)
{
	offset += bcma_host_pci_provide_access_to_core(core);
	return ioread32(core->bus->mmio + offset);
}

static void bcma_host_pci_write8(struct bcma_device *core, u16 offset,
				 u8 value)
{
	offset += bcma_host_pci_provide_access_to_core(core);
	iowrite8(value, core->bus->mmio + offset);
}

static void bcma_host_pci_write16(struct bcma_device *core, u16 offset,
				 u16 value)
{
	offset += bcma_host_pci_provide_access_to_core(core);
	iowrite16(value, core->bus->mmio + offset);
}

static void bcma_host_pci_write32(struct bcma_device *core, u16 offset,
				 u32 value)
{
	offset += bcma_host_pci_provide_access_to_core(core);
	iowrite32(value, core->bus->mmio + offset);
}

#ifdef CONFIG_BCMA_BLOCKIO
void bcma_host_pci_block_read(struct bcma_device *core, void *buffer,
			      size_t count, u16 offset, u8 reg_width)
{
	void __iomem *addr = core->bus->mmio + offset;
	if (core->bus->mapped_core != core)
		bcma_host_pci_switch_core(core);
	switch (reg_width) {
	case sizeof(u8):
		ioread8_rep(addr, buffer, count);
		break;
	case sizeof(u16):
		WARN_ON(count & 1);
		ioread16_rep(addr, buffer, count >> 1);
		break;
	case sizeof(u32):
		WARN_ON(count & 3);
		ioread32_rep(addr, buffer, count >> 2);
		break;
	default:
		WARN_ON(1);
	}
}

void bcma_host_pci_block_write(struct bcma_device *core, const void *buffer,
			       size_t count, u16 offset, u8 reg_width)
{
	void __iomem *addr = core->bus->mmio + offset;
	if (core->bus->mapped_core != core)
		bcma_host_pci_switch_core(core);
	switch (reg_width) {
	case sizeof(u8):
		iowrite8_rep(addr, buffer, count);
		break;
	case sizeof(u16):
		WARN_ON(count & 1);
		iowrite16_rep(addr, buffer, count >> 1);
		break;
	case sizeof(u32):
		WARN_ON(count & 3);
		iowrite32_rep(addr, buffer, count >> 2);
		break;
	default:
		WARN_ON(1);
	}
}
#endif

static u32 bcma_host_pci_aread32(struct bcma_device *core, u16 offset)
{
	if (core->bus->mapped_core != core)
		bcma_host_pci_switch_core(core);
	return ioread32(core->bus->mmio + (1 * BCMA_CORE_SIZE) + offset);
}

static void bcma_host_pci_awrite32(struct bcma_device *core, u16 offset,
				  u32 value)
{
	if (core->bus->mapped_core != core)
		bcma_host_pci_switch_core(core);
	iowrite32(value, core->bus->mmio + (1 * BCMA_CORE_SIZE) + offset);
}

const struct bcma_host_ops bcma_host_pci_ops = {
	.read8		= bcma_host_pci_read8,
	.read16		= bcma_host_pci_read16,
	.read32		= bcma_host_pci_read32,
	.write8		= bcma_host_pci_write8,
	.write16	= bcma_host_pci_write16,
	.write32	= bcma_host_pci_write32,
#ifdef CONFIG_BCMA_BLOCKIO
	.block_read	= bcma_host_pci_block_read,
	.block_write	= bcma_host_pci_block_write,
#endif
	.aread32	= bcma_host_pci_aread32,
	.awrite32	= bcma_host_pci_awrite32,
};

static int __devinit bcma_host_pci_probe(struct pci_dev *dev,
					 const struct pci_device_id *id)
{
	struct bcma_bus *bus;
	int err = -ENOMEM;
	const char *name;
	u32 val;

	/* Alloc */
	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus)
		goto out;

	/* Basic PCI configuration */
	err = pci_enable_device(dev);
	if (err)
		goto err_kfree_bus;

	name = dev_name(&dev->dev);
	if (dev->driver && dev->driver->name)
		name = dev->driver->name;
	err = pci_request_regions(dev, name);
	if (err)
		goto err_pci_disable;
	pci_set_master(dev);

	/* Disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_read_config_dword(dev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(dev, 0x40, val & 0xffff00ff);

	/* SSB needed additional powering up, do we have any AMBA PCI cards? */
	if (!pci_is_pcie(dev))
		bcma_err(bus, "PCI card detected, report problems.\n");

	/* Map MMIO */
	err = -ENOMEM;
	bus->mmio = pci_iomap(dev, 0, ~0UL);
	if (!bus->mmio)
		goto err_pci_release_regions;

	/* Host specific */
	bus->host_pci = dev;
	bus->hosttype = BCMA_HOSTTYPE_PCI;
	bus->ops = &bcma_host_pci_ops;

	bus->boardinfo.vendor = bus->host_pci->subsystem_vendor;
	bus->boardinfo.type = bus->host_pci->subsystem_device;

	/* Register */
	err = bcma_bus_register(bus);
	if (err)
		goto err_pci_unmap_mmio;

	pci_set_drvdata(dev, bus);

out:
	return err;

err_pci_unmap_mmio:
	pci_iounmap(dev, bus->mmio);
err_pci_release_regions:
	pci_release_regions(dev);
err_pci_disable:
	pci_disable_device(dev);
err_kfree_bus:
	kfree(bus);
	return err;
}

static void __devexit bcma_host_pci_remove(struct pci_dev *dev)
{
	struct bcma_bus *bus = pci_get_drvdata(dev);

	bcma_bus_unregister(bus);
	pci_iounmap(dev, bus->mmio);
	pci_release_regions(dev);
	pci_disable_device(dev);
	kfree(bus);
	pci_set_drvdata(dev, NULL);
}

#ifdef CONFIG_PM
static int bcma_host_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct bcma_bus *bus = pci_get_drvdata(pdev);

	bus->mapped_core = NULL;

	return bcma_bus_suspend(bus);
}

static int bcma_host_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct bcma_bus *bus = pci_get_drvdata(pdev);

	return bcma_bus_resume(bus);
}

static SIMPLE_DEV_PM_OPS(bcma_pm_ops, bcma_host_pci_suspend,
			 bcma_host_pci_resume);
#define BCMA_PM_OPS	(&bcma_pm_ops)

#else /* CONFIG_PM */

#define BCMA_PM_OPS     NULL

#endif /* CONFIG_PM */

static DEFINE_PCI_DEVICE_TABLE(bcma_pci_bridge_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x0576) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 43224) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4331) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4353) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4357) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4727) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, bcma_pci_bridge_tbl);

static struct pci_driver bcma_pci_bridge_driver = {
	.name = "bcma-pci-bridge",
	.id_table = bcma_pci_bridge_tbl,
	.probe = bcma_host_pci_probe,
	.remove = __devexit_p(bcma_host_pci_remove),
	.driver.pm = BCMA_PM_OPS,
};

int __init bcma_host_pci_init(void)
{
	return pci_register_driver(&bcma_pci_bridge_driver);
}

void __exit bcma_host_pci_exit(void)
{
	pci_unregister_driver(&bcma_pci_bridge_driver);
}
