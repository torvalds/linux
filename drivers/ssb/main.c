/*
 * Sonics Silicon Backplane
 * Subsystem core
 *
 * Copyright 2005, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <m@bues.ch>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "ssb_private.h"

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_regs.h>
#include <linux/ssb/ssb_driver_gige.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/mmc/sdio_func.h>
#include <linux/slab.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>


MODULE_DESCRIPTION("Sonics Silicon Backplane driver");
MODULE_LICENSE("GPL");


/* Temporary list of yet-to-be-attached buses */
static LIST_HEAD(attach_queue);
/* List if running buses */
static LIST_HEAD(buses);
/* Software ID counter */
static unsigned int next_busnumber;
/* buses_mutes locks the two buslists and the next_busnumber.
 * Don't lock this directly, but use ssb_buses_[un]lock() below. */
static DEFINE_MUTEX(buses_mutex);

/* There are differences in the codeflow, if the bus is
 * initialized from early boot, as various needed services
 * are not available early. This is a mechanism to delay
 * these initializations to after early boot has finished.
 * It's also used to avoid mutex locking, as that's not
 * available and needed early. */
static bool ssb_is_early_boot = 1;

static void ssb_buses_lock(void);
static void ssb_buses_unlock(void);


#ifdef CONFIG_SSB_PCIHOST
struct ssb_bus *ssb_pci_dev_to_bus(struct pci_dev *pdev)
{
	struct ssb_bus *bus;

	ssb_buses_lock();
	list_for_each_entry(bus, &buses, list) {
		if (bus->bustype == SSB_BUSTYPE_PCI &&
		    bus->host_pci == pdev)
			goto found;
	}
	bus = NULL;
found:
	ssb_buses_unlock();

	return bus;
}
#endif /* CONFIG_SSB_PCIHOST */

#ifdef CONFIG_SSB_PCMCIAHOST
struct ssb_bus *ssb_pcmcia_dev_to_bus(struct pcmcia_device *pdev)
{
	struct ssb_bus *bus;

	ssb_buses_lock();
	list_for_each_entry(bus, &buses, list) {
		if (bus->bustype == SSB_BUSTYPE_PCMCIA &&
		    bus->host_pcmcia == pdev)
			goto found;
	}
	bus = NULL;
found:
	ssb_buses_unlock();

	return bus;
}
#endif /* CONFIG_SSB_PCMCIAHOST */

#ifdef CONFIG_SSB_SDIOHOST
struct ssb_bus *ssb_sdio_func_to_bus(struct sdio_func *func)
{
	struct ssb_bus *bus;

	ssb_buses_lock();
	list_for_each_entry(bus, &buses, list) {
		if (bus->bustype == SSB_BUSTYPE_SDIO &&
		    bus->host_sdio == func)
			goto found;
	}
	bus = NULL;
found:
	ssb_buses_unlock();

	return bus;
}
#endif /* CONFIG_SSB_SDIOHOST */

int ssb_for_each_bus_call(unsigned long data,
			  int (*func)(struct ssb_bus *bus, unsigned long data))
{
	struct ssb_bus *bus;
	int res;

	ssb_buses_lock();
	list_for_each_entry(bus, &buses, list) {
		res = func(bus, data);
		if (res >= 0) {
			ssb_buses_unlock();
			return res;
		}
	}
	ssb_buses_unlock();

	return -ENODEV;
}

static struct ssb_device *ssb_device_get(struct ssb_device *dev)
{
	if (dev)
		get_device(dev->dev);
	return dev;
}

static void ssb_device_put(struct ssb_device *dev)
{
	if (dev)
		put_device(dev->dev);
}

static inline struct ssb_driver *ssb_driver_get(struct ssb_driver *drv)
{
	if (drv)
		get_driver(&drv->drv);
	return drv;
}

static inline void ssb_driver_put(struct ssb_driver *drv)
{
	if (drv)
		put_driver(&drv->drv);
}

static int ssb_device_resume(struct device *dev)
{
	struct ssb_device *ssb_dev = dev_to_ssb_dev(dev);
	struct ssb_driver *ssb_drv;
	int err = 0;

	if (dev->driver) {
		ssb_drv = drv_to_ssb_drv(dev->driver);
		if (ssb_drv && ssb_drv->resume)
			err = ssb_drv->resume(ssb_dev);
		if (err)
			goto out;
	}
out:
	return err;
}

static int ssb_device_suspend(struct device *dev, pm_message_t state)
{
	struct ssb_device *ssb_dev = dev_to_ssb_dev(dev);
	struct ssb_driver *ssb_drv;
	int err = 0;

	if (dev->driver) {
		ssb_drv = drv_to_ssb_drv(dev->driver);
		if (ssb_drv && ssb_drv->suspend)
			err = ssb_drv->suspend(ssb_dev, state);
		if (err)
			goto out;
	}
out:
	return err;
}

int ssb_bus_resume(struct ssb_bus *bus)
{
	int err;

	/* Reset HW state information in memory, so that HW is
	 * completely reinitialized. */
	bus->mapped_device = NULL;
#ifdef CONFIG_SSB_DRIVER_PCICORE
	bus->pcicore.setup_done = 0;
#endif

	err = ssb_bus_powerup(bus, 0);
	if (err)
		return err;
	err = ssb_pcmcia_hardware_setup(bus);
	if (err) {
		ssb_bus_may_powerdown(bus);
		return err;
	}
	ssb_chipco_resume(&bus->chipco);
	ssb_bus_may_powerdown(bus);

	return 0;
}
EXPORT_SYMBOL(ssb_bus_resume);

int ssb_bus_suspend(struct ssb_bus *bus)
{
	ssb_chipco_suspend(&bus->chipco);
	ssb_pci_xtal(bus, SSB_GPIO_XTAL | SSB_GPIO_PLL, 0);

	return 0;
}
EXPORT_SYMBOL(ssb_bus_suspend);

#ifdef CONFIG_SSB_SPROM
/** ssb_devices_freeze - Freeze all devices on the bus.
 *
 * After freezing no device driver will be handling a device
 * on this bus anymore. ssb_devices_thaw() must be called after
 * a successful freeze to reactivate the devices.
 *
 * @bus: The bus.
 * @ctx: Context structure. Pass this to ssb_devices_thaw().
 */
int ssb_devices_freeze(struct ssb_bus *bus, struct ssb_freeze_context *ctx)
{
	struct ssb_device *sdev;
	struct ssb_driver *sdrv;
	unsigned int i;

	memset(ctx, 0, sizeof(*ctx));
	ctx->bus = bus;
	SSB_WARN_ON(bus->nr_devices > ARRAY_SIZE(ctx->device_frozen));

	for (i = 0; i < bus->nr_devices; i++) {
		sdev = ssb_device_get(&bus->devices[i]);

		if (!sdev->dev || !sdev->dev->driver ||
		    !device_is_registered(sdev->dev)) {
			ssb_device_put(sdev);
			continue;
		}
		sdrv = ssb_driver_get(drv_to_ssb_drv(sdev->dev->driver));
		if (!sdrv || SSB_WARN_ON(!sdrv->remove)) {
			ssb_device_put(sdev);
			continue;
		}
		sdrv->remove(sdev);
		ctx->device_frozen[i] = 1;
	}

	return 0;
}

/** ssb_devices_thaw - Unfreeze all devices on the bus.
 *
 * This will re-attach the device drivers and re-init the devices.
 *
 * @ctx: The context structure from ssb_devices_freeze()
 */
int ssb_devices_thaw(struct ssb_freeze_context *ctx)
{
	struct ssb_bus *bus = ctx->bus;
	struct ssb_device *sdev;
	struct ssb_driver *sdrv;
	unsigned int i;
	int err, result = 0;

	for (i = 0; i < bus->nr_devices; i++) {
		if (!ctx->device_frozen[i])
			continue;
		sdev = &bus->devices[i];

		if (SSB_WARN_ON(!sdev->dev || !sdev->dev->driver))
			continue;
		sdrv = drv_to_ssb_drv(sdev->dev->driver);
		if (SSB_WARN_ON(!sdrv || !sdrv->probe))
			continue;

		err = sdrv->probe(sdev, &sdev->id);
		if (err) {
			ssb_printk(KERN_ERR PFX "Failed to thaw device %s\n",
				   dev_name(sdev->dev));
			result = err;
		}
		ssb_driver_put(sdrv);
		ssb_device_put(sdev);
	}

	return result;
}
#endif /* CONFIG_SSB_SPROM */

static void ssb_device_shutdown(struct device *dev)
{
	struct ssb_device *ssb_dev = dev_to_ssb_dev(dev);
	struct ssb_driver *ssb_drv;

	if (!dev->driver)
		return;
	ssb_drv = drv_to_ssb_drv(dev->driver);
	if (ssb_drv && ssb_drv->shutdown)
		ssb_drv->shutdown(ssb_dev);
}

static int ssb_device_remove(struct device *dev)
{
	struct ssb_device *ssb_dev = dev_to_ssb_dev(dev);
	struct ssb_driver *ssb_drv = drv_to_ssb_drv(dev->driver);

	if (ssb_drv && ssb_drv->remove)
		ssb_drv->remove(ssb_dev);
	ssb_device_put(ssb_dev);

	return 0;
}

static int ssb_device_probe(struct device *dev)
{
	struct ssb_device *ssb_dev = dev_to_ssb_dev(dev);
	struct ssb_driver *ssb_drv = drv_to_ssb_drv(dev->driver);
	int err = 0;

	ssb_device_get(ssb_dev);
	if (ssb_drv && ssb_drv->probe)
		err = ssb_drv->probe(ssb_dev, &ssb_dev->id);
	if (err)
		ssb_device_put(ssb_dev);

	return err;
}

static int ssb_match_devid(const struct ssb_device_id *tabid,
			   const struct ssb_device_id *devid)
{
	if ((tabid->vendor != devid->vendor) &&
	    tabid->vendor != SSB_ANY_VENDOR)
		return 0;
	if ((tabid->coreid != devid->coreid) &&
	    tabid->coreid != SSB_ANY_ID)
		return 0;
	if ((tabid->revision != devid->revision) &&
	    tabid->revision != SSB_ANY_REV)
		return 0;
	return 1;
}

static int ssb_bus_match(struct device *dev, struct device_driver *drv)
{
	struct ssb_device *ssb_dev = dev_to_ssb_dev(dev);
	struct ssb_driver *ssb_drv = drv_to_ssb_drv(drv);
	const struct ssb_device_id *id;

	for (id = ssb_drv->id_table;
	     id->vendor || id->coreid || id->revision;
	     id++) {
		if (ssb_match_devid(id, &ssb_dev->id))
			return 1; /* found */
	}

	return 0;
}

static int ssb_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct ssb_device *ssb_dev = dev_to_ssb_dev(dev);

	if (!dev)
		return -ENODEV;

	return add_uevent_var(env,
			     "MODALIAS=ssb:v%04Xid%04Xrev%02X",
			     ssb_dev->id.vendor, ssb_dev->id.coreid,
			     ssb_dev->id.revision);
}

#define ssb_config_attr(attrib, field, format_string) \
static ssize_t \
attrib##_show(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return sprintf(buf, format_string, dev_to_ssb_dev(dev)->field); \
}

ssb_config_attr(core_num, core_index, "%u\n")
ssb_config_attr(coreid, id.coreid, "0x%04x\n")
ssb_config_attr(vendor, id.vendor, "0x%04x\n")
ssb_config_attr(revision, id.revision, "%u\n")
ssb_config_attr(irq, irq, "%u\n")
static ssize_t
name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n",
		       ssb_core_name(dev_to_ssb_dev(dev)->id.coreid));
}

static struct device_attribute ssb_device_attrs[] = {
	__ATTR_RO(name),
	__ATTR_RO(core_num),
	__ATTR_RO(coreid),
	__ATTR_RO(vendor),
	__ATTR_RO(revision),
	__ATTR_RO(irq),
	__ATTR_NULL,
};

static struct bus_type ssb_bustype = {
	.name		= "ssb",
	.match		= ssb_bus_match,
	.probe		= ssb_device_probe,
	.remove		= ssb_device_remove,
	.shutdown	= ssb_device_shutdown,
	.suspend	= ssb_device_suspend,
	.resume		= ssb_device_resume,
	.uevent		= ssb_device_uevent,
	.dev_attrs	= ssb_device_attrs,
};

static void ssb_buses_lock(void)
{
	/* See the comment at the ssb_is_early_boot definition */
	if (!ssb_is_early_boot)
		mutex_lock(&buses_mutex);
}

static void ssb_buses_unlock(void)
{
	/* See the comment at the ssb_is_early_boot definition */
	if (!ssb_is_early_boot)
		mutex_unlock(&buses_mutex);
}

static void ssb_devices_unregister(struct ssb_bus *bus)
{
	struct ssb_device *sdev;
	int i;

	for (i = bus->nr_devices - 1; i >= 0; i--) {
		sdev = &(bus->devices[i]);
		if (sdev->dev)
			device_unregister(sdev->dev);
	}
}

void ssb_bus_unregister(struct ssb_bus *bus)
{
	ssb_buses_lock();
	ssb_devices_unregister(bus);
	list_del(&bus->list);
	ssb_buses_unlock();

	ssb_pcmcia_exit(bus);
	ssb_pci_exit(bus);
	ssb_iounmap(bus);
}
EXPORT_SYMBOL(ssb_bus_unregister);

static void ssb_release_dev(struct device *dev)
{
	struct __ssb_dev_wrapper *devwrap;

	devwrap = container_of(dev, struct __ssb_dev_wrapper, dev);
	kfree(devwrap);
}

static int ssb_devices_register(struct ssb_bus *bus)
{
	struct ssb_device *sdev;
	struct device *dev;
	struct __ssb_dev_wrapper *devwrap;
	int i, err = 0;
	int dev_idx = 0;

	for (i = 0; i < bus->nr_devices; i++) {
		sdev = &(bus->devices[i]);

		/* We don't register SSB-system devices to the kernel,
		 * as the drivers for them are built into SSB. */
		switch (sdev->id.coreid) {
		case SSB_DEV_CHIPCOMMON:
		case SSB_DEV_PCI:
		case SSB_DEV_PCIE:
		case SSB_DEV_PCMCIA:
		case SSB_DEV_MIPS:
		case SSB_DEV_MIPS_3302:
		case SSB_DEV_EXTIF:
			continue;
		}

		devwrap = kzalloc(sizeof(*devwrap), GFP_KERNEL);
		if (!devwrap) {
			ssb_printk(KERN_ERR PFX
				   "Could not allocate device\n");
			err = -ENOMEM;
			goto error;
		}
		dev = &devwrap->dev;
		devwrap->sdev = sdev;

		dev->release = ssb_release_dev;
		dev->bus = &ssb_bustype;
		dev_set_name(dev, "ssb%u:%d", bus->busnumber, dev_idx);

		switch (bus->bustype) {
		case SSB_BUSTYPE_PCI:
#ifdef CONFIG_SSB_PCIHOST
			sdev->irq = bus->host_pci->irq;
			dev->parent = &bus->host_pci->dev;
			sdev->dma_dev = dev->parent;
#endif
			break;
		case SSB_BUSTYPE_PCMCIA:
#ifdef CONFIG_SSB_PCMCIAHOST
			sdev->irq = bus->host_pcmcia->irq;
			dev->parent = &bus->host_pcmcia->dev;
#endif
			break;
		case SSB_BUSTYPE_SDIO:
#ifdef CONFIG_SSB_SDIOHOST
			dev->parent = &bus->host_sdio->dev;
#endif
			break;
		case SSB_BUSTYPE_SSB:
			dev->dma_mask = &dev->coherent_dma_mask;
			sdev->dma_dev = dev;
			break;
		}

		sdev->dev = dev;
		err = device_register(dev);
		if (err) {
			ssb_printk(KERN_ERR PFX
				   "Could not register %s\n",
				   dev_name(dev));
			/* Set dev to NULL to not unregister
			 * dev on error unwinding. */
			sdev->dev = NULL;
			kfree(devwrap);
			goto error;
		}
		dev_idx++;
	}

	return 0;
error:
	/* Unwind the already registered devices. */
	ssb_devices_unregister(bus);
	return err;
}

/* Needs ssb_buses_lock() */
static int __devinit ssb_attach_queued_buses(void)
{
	struct ssb_bus *bus, *n;
	int err = 0;
	int drop_them_all = 0;

	list_for_each_entry_safe(bus, n, &attach_queue, list) {
		if (drop_them_all) {
			list_del(&bus->list);
			continue;
		}
		/* Can't init the PCIcore in ssb_bus_register(), as that
		 * is too early in boot for embedded systems
		 * (no udelay() available). So do it here in attach stage.
		 */
		err = ssb_bus_powerup(bus, 0);
		if (err)
			goto error;
		ssb_pcicore_init(&bus->pcicore);
		ssb_bus_may_powerdown(bus);

		err = ssb_devices_register(bus);
error:
		if (err) {
			drop_them_all = 1;
			list_del(&bus->list);
			continue;
		}
		list_move_tail(&bus->list, &buses);
	}

	return err;
}

static u8 ssb_ssb_read8(struct ssb_device *dev, u16 offset)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	return readb(bus->mmio + offset);
}

static u16 ssb_ssb_read16(struct ssb_device *dev, u16 offset)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	return readw(bus->mmio + offset);
}

static u32 ssb_ssb_read32(struct ssb_device *dev, u16 offset)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	return readl(bus->mmio + offset);
}

#ifdef CONFIG_SSB_BLOCKIO
static void ssb_ssb_block_read(struct ssb_device *dev, void *buffer,
			       size_t count, u16 offset, u8 reg_width)
{
	struct ssb_bus *bus = dev->bus;
	void __iomem *addr;

	offset += dev->core_index * SSB_CORE_SIZE;
	addr = bus->mmio + offset;

	switch (reg_width) {
	case sizeof(u8): {
		u8 *buf = buffer;

		while (count) {
			*buf = __raw_readb(addr);
			buf++;
			count--;
		}
		break;
	}
	case sizeof(u16): {
		__le16 *buf = buffer;

		SSB_WARN_ON(count & 1);
		while (count) {
			*buf = (__force __le16)__raw_readw(addr);
			buf++;
			count -= 2;
		}
		break;
	}
	case sizeof(u32): {
		__le32 *buf = buffer;

		SSB_WARN_ON(count & 3);
		while (count) {
			*buf = (__force __le32)__raw_readl(addr);
			buf++;
			count -= 4;
		}
		break;
	}
	default:
		SSB_WARN_ON(1);
	}
}
#endif /* CONFIG_SSB_BLOCKIO */

static void ssb_ssb_write8(struct ssb_device *dev, u16 offset, u8 value)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	writeb(value, bus->mmio + offset);
}

static void ssb_ssb_write16(struct ssb_device *dev, u16 offset, u16 value)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	writew(value, bus->mmio + offset);
}

static void ssb_ssb_write32(struct ssb_device *dev, u16 offset, u32 value)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	writel(value, bus->mmio + offset);
}

#ifdef CONFIG_SSB_BLOCKIO
static void ssb_ssb_block_write(struct ssb_device *dev, const void *buffer,
				size_t count, u16 offset, u8 reg_width)
{
	struct ssb_bus *bus = dev->bus;
	void __iomem *addr;

	offset += dev->core_index * SSB_CORE_SIZE;
	addr = bus->mmio + offset;

	switch (reg_width) {
	case sizeof(u8): {
		const u8 *buf = buffer;

		while (count) {
			__raw_writeb(*buf, addr);
			buf++;
			count--;
		}
		break;
	}
	case sizeof(u16): {
		const __le16 *buf = buffer;

		SSB_WARN_ON(count & 1);
		while (count) {
			__raw_writew((__force u16)(*buf), addr);
			buf++;
			count -= 2;
		}
		break;
	}
	case sizeof(u32): {
		const __le32 *buf = buffer;

		SSB_WARN_ON(count & 3);
		while (count) {
			__raw_writel((__force u32)(*buf), addr);
			buf++;
			count -= 4;
		}
		break;
	}
	default:
		SSB_WARN_ON(1);
	}
}
#endif /* CONFIG_SSB_BLOCKIO */

/* Ops for the plain SSB bus without a host-device (no PCI or PCMCIA). */
static const struct ssb_bus_ops ssb_ssb_ops = {
	.read8		= ssb_ssb_read8,
	.read16		= ssb_ssb_read16,
	.read32		= ssb_ssb_read32,
	.write8		= ssb_ssb_write8,
	.write16	= ssb_ssb_write16,
	.write32	= ssb_ssb_write32,
#ifdef CONFIG_SSB_BLOCKIO
	.block_read	= ssb_ssb_block_read,
	.block_write	= ssb_ssb_block_write,
#endif
};

static int ssb_fetch_invariants(struct ssb_bus *bus,
				ssb_invariants_func_t get_invariants)
{
	struct ssb_init_invariants iv;
	int err;

	memset(&iv, 0, sizeof(iv));
	err = get_invariants(bus, &iv);
	if (err)
		goto out;
	memcpy(&bus->boardinfo, &iv.boardinfo, sizeof(iv.boardinfo));
	memcpy(&bus->sprom, &iv.sprom, sizeof(iv.sprom));
	bus->has_cardbus_slot = iv.has_cardbus_slot;
out:
	return err;
}

static int __devinit ssb_bus_register(struct ssb_bus *bus,
				      ssb_invariants_func_t get_invariants,
				      unsigned long baseaddr)
{
	int err;

	spin_lock_init(&bus->bar_lock);
	INIT_LIST_HEAD(&bus->list);
#ifdef CONFIG_SSB_EMBEDDED
	spin_lock_init(&bus->gpio_lock);
#endif

	/* Powerup the bus */
	err = ssb_pci_xtal(bus, SSB_GPIO_XTAL | SSB_GPIO_PLL, 1);
	if (err)
		goto out;

	/* Init SDIO-host device (if any), before the scan */
	err = ssb_sdio_init(bus);
	if (err)
		goto err_disable_xtal;

	ssb_buses_lock();
	bus->busnumber = next_busnumber;
	/* Scan for devices (cores) */
	err = ssb_bus_scan(bus, baseaddr);
	if (err)
		goto err_sdio_exit;

	/* Init PCI-host device (if any) */
	err = ssb_pci_init(bus);
	if (err)
		goto err_unmap;
	/* Init PCMCIA-host device (if any) */
	err = ssb_pcmcia_init(bus);
	if (err)
		goto err_pci_exit;

	/* Initialize basic system devices (if available) */
	err = ssb_bus_powerup(bus, 0);
	if (err)
		goto err_pcmcia_exit;
	ssb_chipcommon_init(&bus->chipco);
	ssb_mipscore_init(&bus->mipscore);
	err = ssb_fetch_invariants(bus, get_invariants);
	if (err) {
		ssb_bus_may_powerdown(bus);
		goto err_pcmcia_exit;
	}
	ssb_bus_may_powerdown(bus);

	/* Queue it for attach.
	 * See the comment at the ssb_is_early_boot definition. */
	list_add_tail(&bus->list, &attach_queue);
	if (!ssb_is_early_boot) {
		/* This is not early boot, so we must attach the bus now */
		err = ssb_attach_queued_buses();
		if (err)
			goto err_dequeue;
	}
	next_busnumber++;
	ssb_buses_unlock();

out:
	return err;

err_dequeue:
	list_del(&bus->list);
err_pcmcia_exit:
	ssb_pcmcia_exit(bus);
err_pci_exit:
	ssb_pci_exit(bus);
err_unmap:
	ssb_iounmap(bus);
err_sdio_exit:
	ssb_sdio_exit(bus);
err_disable_xtal:
	ssb_buses_unlock();
	ssb_pci_xtal(bus, SSB_GPIO_XTAL | SSB_GPIO_PLL, 0);
	return err;
}

#ifdef CONFIG_SSB_PCIHOST
int __devinit ssb_bus_pcibus_register(struct ssb_bus *bus,
				      struct pci_dev *host_pci)
{
	int err;

	bus->bustype = SSB_BUSTYPE_PCI;
	bus->host_pci = host_pci;
	bus->ops = &ssb_pci_ops;

	err = ssb_bus_register(bus, ssb_pci_get_invariants, 0);
	if (!err) {
		ssb_printk(KERN_INFO PFX "Sonics Silicon Backplane found on "
			   "PCI device %s\n", dev_name(&host_pci->dev));
	} else {
		ssb_printk(KERN_ERR PFX "Failed to register PCI version"
			   " of SSB with error %d\n", err);
	}

	return err;
}
EXPORT_SYMBOL(ssb_bus_pcibus_register);
#endif /* CONFIG_SSB_PCIHOST */

#ifdef CONFIG_SSB_PCMCIAHOST
int __devinit ssb_bus_pcmciabus_register(struct ssb_bus *bus,
					 struct pcmcia_device *pcmcia_dev,
					 unsigned long baseaddr)
{
	int err;

	bus->bustype = SSB_BUSTYPE_PCMCIA;
	bus->host_pcmcia = pcmcia_dev;
	bus->ops = &ssb_pcmcia_ops;

	err = ssb_bus_register(bus, ssb_pcmcia_get_invariants, baseaddr);
	if (!err) {
		ssb_printk(KERN_INFO PFX "Sonics Silicon Backplane found on "
			   "PCMCIA device %s\n", pcmcia_dev->devname);
	}

	return err;
}
EXPORT_SYMBOL(ssb_bus_pcmciabus_register);
#endif /* CONFIG_SSB_PCMCIAHOST */

#ifdef CONFIG_SSB_SDIOHOST
int __devinit ssb_bus_sdiobus_register(struct ssb_bus *bus,
				       struct sdio_func *func,
				       unsigned int quirks)
{
	int err;

	bus->bustype = SSB_BUSTYPE_SDIO;
	bus->host_sdio = func;
	bus->ops = &ssb_sdio_ops;
	bus->quirks = quirks;

	err = ssb_bus_register(bus, ssb_sdio_get_invariants, ~0);
	if (!err) {
		ssb_printk(KERN_INFO PFX "Sonics Silicon Backplane found on "
			   "SDIO device %s\n", sdio_func_id(func));
	}

	return err;
}
EXPORT_SYMBOL(ssb_bus_sdiobus_register);
#endif /* CONFIG_SSB_PCMCIAHOST */

int __devinit ssb_bus_ssbbus_register(struct ssb_bus *bus,
				      unsigned long baseaddr,
				      ssb_invariants_func_t get_invariants)
{
	int err;

	bus->bustype = SSB_BUSTYPE_SSB;
	bus->ops = &ssb_ssb_ops;

	err = ssb_bus_register(bus, get_invariants, baseaddr);
	if (!err) {
		ssb_printk(KERN_INFO PFX "Sonics Silicon Backplane found at "
			   "address 0x%08lX\n", baseaddr);
	}

	return err;
}

int __ssb_driver_register(struct ssb_driver *drv, struct module *owner)
{
	drv->drv.name = drv->name;
	drv->drv.bus = &ssb_bustype;
	drv->drv.owner = owner;

	return driver_register(&drv->drv);
}
EXPORT_SYMBOL(__ssb_driver_register);

void ssb_driver_unregister(struct ssb_driver *drv)
{
	driver_unregister(&drv->drv);
}
EXPORT_SYMBOL(ssb_driver_unregister);

void ssb_set_devtypedata(struct ssb_device *dev, void *data)
{
	struct ssb_bus *bus = dev->bus;
	struct ssb_device *ent;
	int i;

	for (i = 0; i < bus->nr_devices; i++) {
		ent = &(bus->devices[i]);
		if (ent->id.vendor != dev->id.vendor)
			continue;
		if (ent->id.coreid != dev->id.coreid)
			continue;

		ent->devtypedata = data;
	}
}
EXPORT_SYMBOL(ssb_set_devtypedata);

static u32 clkfactor_f6_resolve(u32 v)
{
	/* map the magic values */
	switch (v) {
	case SSB_CHIPCO_CLK_F6_2:
		return 2;
	case SSB_CHIPCO_CLK_F6_3:
		return 3;
	case SSB_CHIPCO_CLK_F6_4:
		return 4;
	case SSB_CHIPCO_CLK_F6_5:
		return 5;
	case SSB_CHIPCO_CLK_F6_6:
		return 6;
	case SSB_CHIPCO_CLK_F6_7:
		return 7;
	}
	return 0;
}

/* Calculate the speed the backplane would run at a given set of clockcontrol values */
u32 ssb_calc_clock_rate(u32 plltype, u32 n, u32 m)
{
	u32 n1, n2, clock, m1, m2, m3, mc;

	n1 = (n & SSB_CHIPCO_CLK_N1);
	n2 = ((n & SSB_CHIPCO_CLK_N2) >> SSB_CHIPCO_CLK_N2_SHIFT);

	switch (plltype) {
	case SSB_PLLTYPE_6: /* 100/200 or 120/240 only */
		if (m & SSB_CHIPCO_CLK_T6_MMASK)
			return SSB_CHIPCO_CLK_T6_M1;
		return SSB_CHIPCO_CLK_T6_M0;
	case SSB_PLLTYPE_1: /* 48Mhz base, 3 dividers */
	case SSB_PLLTYPE_3: /* 25Mhz, 2 dividers */
	case SSB_PLLTYPE_4: /* 48Mhz, 4 dividers */
	case SSB_PLLTYPE_7: /* 25Mhz, 4 dividers */
		n1 = clkfactor_f6_resolve(n1);
		n2 += SSB_CHIPCO_CLK_F5_BIAS;
		break;
	case SSB_PLLTYPE_2: /* 48Mhz, 4 dividers */
		n1 += SSB_CHIPCO_CLK_T2_BIAS;
		n2 += SSB_CHIPCO_CLK_T2_BIAS;
		SSB_WARN_ON(!((n1 >= 2) && (n1 <= 7)));
		SSB_WARN_ON(!((n2 >= 5) && (n2 <= 23)));
		break;
	case SSB_PLLTYPE_5: /* 25Mhz, 4 dividers */
		return 100000000;
	default:
		SSB_WARN_ON(1);
	}

	switch (plltype) {
	case SSB_PLLTYPE_3: /* 25Mhz, 2 dividers */
	case SSB_PLLTYPE_7: /* 25Mhz, 4 dividers */
		clock = SSB_CHIPCO_CLK_BASE2 * n1 * n2;
		break;
	default:
		clock = SSB_CHIPCO_CLK_BASE1 * n1 * n2;
	}
	if (!clock)
		return 0;

	m1 = (m & SSB_CHIPCO_CLK_M1);
	m2 = ((m & SSB_CHIPCO_CLK_M2) >> SSB_CHIPCO_CLK_M2_SHIFT);
	m3 = ((m & SSB_CHIPCO_CLK_M3) >> SSB_CHIPCO_CLK_M3_SHIFT);
	mc = ((m & SSB_CHIPCO_CLK_MC) >> SSB_CHIPCO_CLK_MC_SHIFT);

	switch (plltype) {
	case SSB_PLLTYPE_1: /* 48Mhz base, 3 dividers */
	case SSB_PLLTYPE_3: /* 25Mhz, 2 dividers */
	case SSB_PLLTYPE_4: /* 48Mhz, 4 dividers */
	case SSB_PLLTYPE_7: /* 25Mhz, 4 dividers */
		m1 = clkfactor_f6_resolve(m1);
		if ((plltype == SSB_PLLTYPE_1) ||
		    (plltype == SSB_PLLTYPE_3))
			m2 += SSB_CHIPCO_CLK_F5_BIAS;
		else
			m2 = clkfactor_f6_resolve(m2);
		m3 = clkfactor_f6_resolve(m3);

		switch (mc) {
		case SSB_CHIPCO_CLK_MC_BYPASS:
			return clock;
		case SSB_CHIPCO_CLK_MC_M1:
			return (clock / m1);
		case SSB_CHIPCO_CLK_MC_M1M2:
			return (clock / (m1 * m2));
		case SSB_CHIPCO_CLK_MC_M1M2M3:
			return (clock / (m1 * m2 * m3));
		case SSB_CHIPCO_CLK_MC_M1M3:
			return (clock / (m1 * m3));
		}
		return 0;
	case SSB_PLLTYPE_2:
		m1 += SSB_CHIPCO_CLK_T2_BIAS;
		m2 += SSB_CHIPCO_CLK_T2M2_BIAS;
		m3 += SSB_CHIPCO_CLK_T2_BIAS;
		SSB_WARN_ON(!((m1 >= 2) && (m1 <= 7)));
		SSB_WARN_ON(!((m2 >= 3) && (m2 <= 10)));
		SSB_WARN_ON(!((m3 >= 2) && (m3 <= 7)));

		if (!(mc & SSB_CHIPCO_CLK_T2MC_M1BYP))
			clock /= m1;
		if (!(mc & SSB_CHIPCO_CLK_T2MC_M2BYP))
			clock /= m2;
		if (!(mc & SSB_CHIPCO_CLK_T2MC_M3BYP))
			clock /= m3;
		return clock;
	default:
		SSB_WARN_ON(1);
	}
	return 0;
}

/* Get the current speed the backplane is running at */
u32 ssb_clockspeed(struct ssb_bus *bus)
{
	u32 rate;
	u32 plltype;
	u32 clkctl_n, clkctl_m;

	if (ssb_extif_available(&bus->extif))
		ssb_extif_get_clockcontrol(&bus->extif, &plltype,
					   &clkctl_n, &clkctl_m);
	else if (bus->chipco.dev)
		ssb_chipco_get_clockcontrol(&bus->chipco, &plltype,
					    &clkctl_n, &clkctl_m);
	else
		return 0;

	if (bus->chip_id == 0x5365) {
		rate = 100000000;
	} else {
		rate = ssb_calc_clock_rate(plltype, clkctl_n, clkctl_m);
		if (plltype == SSB_PLLTYPE_3) /* 25Mhz, 2 dividers */
			rate /= 2;
	}

	return rate;
}
EXPORT_SYMBOL(ssb_clockspeed);

static u32 ssb_tmslow_reject_bitmask(struct ssb_device *dev)
{
	u32 rev = ssb_read32(dev, SSB_IDLOW) & SSB_IDLOW_SSBREV;

	/* The REJECT bit seems to be different for Backplane rev 2.3 */
	switch (rev) {
	case SSB_IDLOW_SSBREV_22:
	case SSB_IDLOW_SSBREV_24:
	case SSB_IDLOW_SSBREV_26:
		return SSB_TMSLOW_REJECT;
	case SSB_IDLOW_SSBREV_23:
		return SSB_TMSLOW_REJECT_23;
	case SSB_IDLOW_SSBREV_25:     /* TODO - find the proper REJECT bit */
	case SSB_IDLOW_SSBREV_27:     /* same here */
		return SSB_TMSLOW_REJECT;	/* this is a guess */
	default:
		printk(KERN_INFO "ssb: Backplane Revision 0x%.8X\n", rev);
		WARN_ON(1);
	}
	return (SSB_TMSLOW_REJECT | SSB_TMSLOW_REJECT_23);
}

int ssb_device_is_enabled(struct ssb_device *dev)
{
	u32 val;
	u32 reject;

	reject = ssb_tmslow_reject_bitmask(dev);
	val = ssb_read32(dev, SSB_TMSLOW);
	val &= SSB_TMSLOW_CLOCK | SSB_TMSLOW_RESET | reject;

	return (val == SSB_TMSLOW_CLOCK);
}
EXPORT_SYMBOL(ssb_device_is_enabled);

static void ssb_flush_tmslow(struct ssb_device *dev)
{
	/* Make _really_ sure the device has finished the TMSLOW
	 * register write transaction, as we risk running into
	 * a machine check exception otherwise.
	 * Do this by reading the register back to commit the
	 * PCI write and delay an additional usec for the device
	 * to react to the change. */
	ssb_read32(dev, SSB_TMSLOW);
	udelay(1);
}

void ssb_device_enable(struct ssb_device *dev, u32 core_specific_flags)
{
	u32 val;

	ssb_device_disable(dev, core_specific_flags);
	ssb_write32(dev, SSB_TMSLOW,
		    SSB_TMSLOW_RESET | SSB_TMSLOW_CLOCK |
		    SSB_TMSLOW_FGC | core_specific_flags);
	ssb_flush_tmslow(dev);

	/* Clear SERR if set. This is a hw bug workaround. */
	if (ssb_read32(dev, SSB_TMSHIGH) & SSB_TMSHIGH_SERR)
		ssb_write32(dev, SSB_TMSHIGH, 0);

	val = ssb_read32(dev, SSB_IMSTATE);
	if (val & (SSB_IMSTATE_IBE | SSB_IMSTATE_TO)) {
		val &= ~(SSB_IMSTATE_IBE | SSB_IMSTATE_TO);
		ssb_write32(dev, SSB_IMSTATE, val);
	}

	ssb_write32(dev, SSB_TMSLOW,
		    SSB_TMSLOW_CLOCK | SSB_TMSLOW_FGC |
		    core_specific_flags);
	ssb_flush_tmslow(dev);

	ssb_write32(dev, SSB_TMSLOW, SSB_TMSLOW_CLOCK |
		    core_specific_flags);
	ssb_flush_tmslow(dev);
}
EXPORT_SYMBOL(ssb_device_enable);

/* Wait for bitmask in a register to get set or cleared.
 * timeout is in units of ten-microseconds */
static int ssb_wait_bits(struct ssb_device *dev, u16 reg, u32 bitmask,
			 int timeout, int set)
{
	int i;
	u32 val;

	for (i = 0; i < timeout; i++) {
		val = ssb_read32(dev, reg);
		if (set) {
			if ((val & bitmask) == bitmask)
				return 0;
		} else {
			if (!(val & bitmask))
				return 0;
		}
		udelay(10);
	}
	printk(KERN_ERR PFX "Timeout waiting for bitmask %08X on "
			    "register %04X to %s.\n",
	       bitmask, reg, (set ? "set" : "clear"));

	return -ETIMEDOUT;
}

void ssb_device_disable(struct ssb_device *dev, u32 core_specific_flags)
{
	u32 reject, val;

	if (ssb_read32(dev, SSB_TMSLOW) & SSB_TMSLOW_RESET)
		return;

	reject = ssb_tmslow_reject_bitmask(dev);

	if (ssb_read32(dev, SSB_TMSLOW) & SSB_TMSLOW_CLOCK) {
		ssb_write32(dev, SSB_TMSLOW, reject | SSB_TMSLOW_CLOCK);
		ssb_wait_bits(dev, SSB_TMSLOW, reject, 1000, 1);
		ssb_wait_bits(dev, SSB_TMSHIGH, SSB_TMSHIGH_BUSY, 1000, 0);

		if (ssb_read32(dev, SSB_IDLOW) & SSB_IDLOW_INITIATOR) {
			val = ssb_read32(dev, SSB_IMSTATE);
			val |= SSB_IMSTATE_REJECT;
			ssb_write32(dev, SSB_IMSTATE, val);
			ssb_wait_bits(dev, SSB_IMSTATE, SSB_IMSTATE_BUSY, 1000,
				      0);
		}

		ssb_write32(dev, SSB_TMSLOW,
			SSB_TMSLOW_FGC | SSB_TMSLOW_CLOCK |
			reject | SSB_TMSLOW_RESET |
			core_specific_flags);
		ssb_flush_tmslow(dev);

		if (ssb_read32(dev, SSB_IDLOW) & SSB_IDLOW_INITIATOR) {
			val = ssb_read32(dev, SSB_IMSTATE);
			val &= ~SSB_IMSTATE_REJECT;
			ssb_write32(dev, SSB_IMSTATE, val);
		}
	}

	ssb_write32(dev, SSB_TMSLOW,
		    reject | SSB_TMSLOW_RESET |
		    core_specific_flags);
	ssb_flush_tmslow(dev);
}
EXPORT_SYMBOL(ssb_device_disable);

/* Some chipsets need routing known for PCIe and 64-bit DMA */
static bool ssb_dma_translation_special_bit(struct ssb_device *dev)
{
	u16 chip_id = dev->bus->chip_id;

	if (dev->id.coreid == SSB_DEV_80211) {
		return (chip_id == 0x4322 || chip_id == 43221 ||
			chip_id == 43231 || chip_id == 43222);
	}

	return 0;
}

u32 ssb_dma_translation(struct ssb_device *dev)
{
	switch (dev->bus->bustype) {
	case SSB_BUSTYPE_SSB:
		return 0;
	case SSB_BUSTYPE_PCI:
		if (pci_is_pcie(dev->bus->host_pci) &&
		    ssb_read32(dev, SSB_TMSHIGH) & SSB_TMSHIGH_DMA64) {
			return SSB_PCIE_DMA_H32;
		} else {
			if (ssb_dma_translation_special_bit(dev))
				return SSB_PCIE_DMA_H32;
			else
				return SSB_PCI_DMA;
		}
	default:
		__ssb_dma_not_implemented(dev);
	}
	return 0;
}
EXPORT_SYMBOL(ssb_dma_translation);

int ssb_bus_may_powerdown(struct ssb_bus *bus)
{
	struct ssb_chipcommon *cc;
	int err = 0;

	/* On buses where more than one core may be working
	 * at a time, we must not powerdown stuff if there are
	 * still cores that may want to run. */
	if (bus->bustype == SSB_BUSTYPE_SSB)
		goto out;

	cc = &bus->chipco;

	if (!cc->dev)
		goto out;
	if (cc->dev->id.revision < 5)
		goto out;

	ssb_chipco_set_clockmode(cc, SSB_CLKMODE_SLOW);
	err = ssb_pci_xtal(bus, SSB_GPIO_XTAL | SSB_GPIO_PLL, 0);
	if (err)
		goto error;
out:
#ifdef CONFIG_SSB_DEBUG
	bus->powered_up = 0;
#endif
	return err;
error:
	ssb_printk(KERN_ERR PFX "Bus powerdown failed\n");
	goto out;
}
EXPORT_SYMBOL(ssb_bus_may_powerdown);

int ssb_bus_powerup(struct ssb_bus *bus, bool dynamic_pctl)
{
	int err;
	enum ssb_clkmode mode;

	err = ssb_pci_xtal(bus, SSB_GPIO_XTAL | SSB_GPIO_PLL, 1);
	if (err)
		goto error;

#ifdef CONFIG_SSB_DEBUG
	bus->powered_up = 1;
#endif

	mode = dynamic_pctl ? SSB_CLKMODE_DYNAMIC : SSB_CLKMODE_FAST;
	ssb_chipco_set_clockmode(&bus->chipco, mode);

	return 0;
error:
	ssb_printk(KERN_ERR PFX "Bus powerup failed\n");
	return err;
}
EXPORT_SYMBOL(ssb_bus_powerup);

static void ssb_broadcast_value(struct ssb_device *dev,
				u32 address, u32 data)
{
#ifdef CONFIG_SSB_DRIVER_PCICORE
	/* This is used for both, PCI and ChipCommon core, so be careful. */
	BUILD_BUG_ON(SSB_PCICORE_BCAST_ADDR != SSB_CHIPCO_BCAST_ADDR);
	BUILD_BUG_ON(SSB_PCICORE_BCAST_DATA != SSB_CHIPCO_BCAST_DATA);
#endif

	ssb_write32(dev, SSB_CHIPCO_BCAST_ADDR, address);
	ssb_read32(dev, SSB_CHIPCO_BCAST_ADDR); /* flush */
	ssb_write32(dev, SSB_CHIPCO_BCAST_DATA, data);
	ssb_read32(dev, SSB_CHIPCO_BCAST_DATA); /* flush */
}

void ssb_commit_settings(struct ssb_bus *bus)
{
	struct ssb_device *dev;

#ifdef CONFIG_SSB_DRIVER_PCICORE
	dev = bus->chipco.dev ? bus->chipco.dev : bus->pcicore.dev;
#else
	dev = bus->chipco.dev;
#endif
	if (WARN_ON(!dev))
		return;
	/* This forces an update of the cached registers. */
	ssb_broadcast_value(dev, 0xFD8, 0);
}
EXPORT_SYMBOL(ssb_commit_settings);

u32 ssb_admatch_base(u32 adm)
{
	u32 base = 0;

	switch (adm & SSB_ADM_TYPE) {
	case SSB_ADM_TYPE0:
		base = (adm & SSB_ADM_BASE0);
		break;
	case SSB_ADM_TYPE1:
		SSB_WARN_ON(adm & SSB_ADM_NEG); /* unsupported */
		base = (adm & SSB_ADM_BASE1);
		break;
	case SSB_ADM_TYPE2:
		SSB_WARN_ON(adm & SSB_ADM_NEG); /* unsupported */
		base = (adm & SSB_ADM_BASE2);
		break;
	default:
		SSB_WARN_ON(1);
	}

	return base;
}
EXPORT_SYMBOL(ssb_admatch_base);

u32 ssb_admatch_size(u32 adm)
{
	u32 size = 0;

	switch (adm & SSB_ADM_TYPE) {
	case SSB_ADM_TYPE0:
		size = ((adm & SSB_ADM_SZ0) >> SSB_ADM_SZ0_SHIFT);
		break;
	case SSB_ADM_TYPE1:
		SSB_WARN_ON(adm & SSB_ADM_NEG); /* unsupported */
		size = ((adm & SSB_ADM_SZ1) >> SSB_ADM_SZ1_SHIFT);
		break;
	case SSB_ADM_TYPE2:
		SSB_WARN_ON(adm & SSB_ADM_NEG); /* unsupported */
		size = ((adm & SSB_ADM_SZ2) >> SSB_ADM_SZ2_SHIFT);
		break;
	default:
		SSB_WARN_ON(1);
	}
	size = (1 << (size + 1));

	return size;
}
EXPORT_SYMBOL(ssb_admatch_size);

static int __init ssb_modinit(void)
{
	int err;

	/* See the comment at the ssb_is_early_boot definition */
	ssb_is_early_boot = 0;
	err = bus_register(&ssb_bustype);
	if (err)
		return err;

	/* Maybe we already registered some buses at early boot.
	 * Check for this and attach them
	 */
	ssb_buses_lock();
	err = ssb_attach_queued_buses();
	ssb_buses_unlock();
	if (err) {
		bus_unregister(&ssb_bustype);
		goto out;
	}

	err = b43_pci_ssb_bridge_init();
	if (err) {
		ssb_printk(KERN_ERR "Broadcom 43xx PCI-SSB-bridge "
			   "initialization failed\n");
		/* don't fail SSB init because of this */
		err = 0;
	}
	err = ssb_gige_init();
	if (err) {
		ssb_printk(KERN_ERR "SSB Broadcom Gigabit Ethernet "
			   "driver initialization failed\n");
		/* don't fail SSB init because of this */
		err = 0;
	}
out:
	return err;
}
/* ssb must be initialized after PCI but before the ssb drivers.
 * That means we must use some initcall between subsys_initcall
 * and device_initcall. */
fs_initcall(ssb_modinit);

static void __exit ssb_modexit(void)
{
	ssb_gige_exit();
	b43_pci_ssb_bridge_exit();
	bus_unregister(&ssb_bustype);
}
module_exit(ssb_modexit)
