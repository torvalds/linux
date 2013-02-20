/*
 * Broadcom specific AMBA
 * Bus subsystem
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/bcma/bcma.h>
#include <linux/slab.h>

MODULE_DESCRIPTION("Broadcom's specific AMBA driver");
MODULE_LICENSE("GPL");

/* contains the number the next bus should get. */
static unsigned int bcma_bus_next_num = 0;

/* bcma_buses_mutex locks the bcma_bus_next_num */
static DEFINE_MUTEX(bcma_buses_mutex);

static int bcma_bus_match(struct device *dev, struct device_driver *drv);
static int bcma_device_probe(struct device *dev);
static int bcma_device_remove(struct device *dev);
static int bcma_device_uevent(struct device *dev, struct kobj_uevent_env *env);

static ssize_t manuf_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	return sprintf(buf, "0x%03X\n", core->id.manuf);
}
static ssize_t id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	return sprintf(buf, "0x%03X\n", core->id.id);
}
static ssize_t rev_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	return sprintf(buf, "0x%02X\n", core->id.rev);
}
static ssize_t class_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	return sprintf(buf, "0x%X\n", core->id.class);
}
static struct device_attribute bcma_device_attrs[] = {
	__ATTR_RO(manuf),
	__ATTR_RO(id),
	__ATTR_RO(rev),
	__ATTR_RO(class),
	__ATTR_NULL,
};

static struct bus_type bcma_bus_type = {
	.name		= "bcma",
	.match		= bcma_bus_match,
	.probe		= bcma_device_probe,
	.remove		= bcma_device_remove,
	.uevent		= bcma_device_uevent,
	.dev_attrs	= bcma_device_attrs,
};

static u16 bcma_cc_core_id(struct bcma_bus *bus)
{
	if (bus->chipinfo.id == BCMA_CHIP_ID_BCM4706)
		return BCMA_CORE_4706_CHIPCOMMON;
	return BCMA_CORE_CHIPCOMMON;
}

struct bcma_device *bcma_find_core(struct bcma_bus *bus, u16 coreid)
{
	struct bcma_device *core;

	list_for_each_entry(core, &bus->cores, list) {
		if (core->id.id == coreid)
			return core;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(bcma_find_core);

static struct bcma_device *bcma_find_core_unit(struct bcma_bus *bus, u16 coreid,
					       u8 unit)
{
	struct bcma_device *core;

	list_for_each_entry(core, &bus->cores, list) {
		if (core->id.id == coreid && core->core_unit == unit)
			return core;
	}
	return NULL;
}

static void bcma_release_core_dev(struct device *dev)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	if (core->io_addr)
		iounmap(core->io_addr);
	if (core->io_wrap)
		iounmap(core->io_wrap);
	kfree(core);
}

static int bcma_register_cores(struct bcma_bus *bus)
{
	struct bcma_device *core;
	int err, dev_id = 0;

	list_for_each_entry(core, &bus->cores, list) {
		/* We support that cores ourself */
		switch (core->id.id) {
		case BCMA_CORE_4706_CHIPCOMMON:
		case BCMA_CORE_CHIPCOMMON:
		case BCMA_CORE_PCI:
		case BCMA_CORE_PCIE:
		case BCMA_CORE_MIPS_74K:
		case BCMA_CORE_4706_MAC_GBIT_COMMON:
			continue;
		}

		core->dev.release = bcma_release_core_dev;
		core->dev.bus = &bcma_bus_type;
		dev_set_name(&core->dev, "bcma%d:%d", bus->num, dev_id);

		switch (bus->hosttype) {
		case BCMA_HOSTTYPE_PCI:
			core->dev.parent = &bus->host_pci->dev;
			core->dma_dev = &bus->host_pci->dev;
			core->irq = bus->host_pci->irq;
			break;
		case BCMA_HOSTTYPE_SOC:
			core->dev.dma_mask = &core->dev.coherent_dma_mask;
			core->dma_dev = &core->dev;
			break;
		case BCMA_HOSTTYPE_SDIO:
			break;
		}

		err = device_register(&core->dev);
		if (err) {
			bcma_err(bus,
				 "Could not register dev for core 0x%03X\n",
				 core->id.id);
			continue;
		}
		core->dev_registered = true;
		dev_id++;
	}

#ifdef CONFIG_BCMA_SFLASH
	if (bus->drv_cc.sflash.present) {
		err = platform_device_register(&bcma_sflash_dev);
		if (err)
			bcma_err(bus, "Error registering serial flash\n");
	}
#endif

#ifdef CONFIG_BCMA_NFLASH
	if (bus->drv_cc.nflash.present) {
		err = platform_device_register(&bcma_nflash_dev);
		if (err)
			bcma_err(bus, "Error registering NAND flash\n");
	}
#endif
	err = bcma_gpio_init(&bus->drv_cc);
	if (err == -ENOTSUPP)
		bcma_debug(bus, "GPIO driver not activated\n");
	else if (err)
		bcma_err(bus, "Error registering GPIO driver: %i\n", err);

	if (bus->hosttype == BCMA_HOSTTYPE_SOC) {
		err = bcma_chipco_watchdog_register(&bus->drv_cc);
		if (err)
			bcma_err(bus, "Error registering watchdog driver\n");
	}

	return 0;
}

static void bcma_unregister_cores(struct bcma_bus *bus)
{
	struct bcma_device *core, *tmp;

	list_for_each_entry_safe(core, tmp, &bus->cores, list) {
		list_del(&core->list);
		if (core->dev_registered)
			device_unregister(&core->dev);
	}
	if (bus->hosttype == BCMA_HOSTTYPE_SOC)
		platform_device_unregister(bus->drv_cc.watchdog);
}

int bcma_bus_register(struct bcma_bus *bus)
{
	int err;
	struct bcma_device *core;

	mutex_lock(&bcma_buses_mutex);
	bus->num = bcma_bus_next_num++;
	mutex_unlock(&bcma_buses_mutex);

	/* Scan for devices (cores) */
	err = bcma_bus_scan(bus);
	if (err) {
		bcma_err(bus, "Failed to scan: %d\n", err);
		return -1;
	}

	/* Early init CC core */
	core = bcma_find_core(bus, bcma_cc_core_id(bus));
	if (core) {
		bus->drv_cc.core = core;
		bcma_core_chipcommon_early_init(&bus->drv_cc);
	}

	/* Try to get SPROM */
	err = bcma_sprom_get(bus);
	if (err == -ENOENT) {
		bcma_err(bus, "No SPROM available\n");
	} else if (err)
		bcma_err(bus, "Failed to get SPROM: %d\n", err);

	/* Init CC core */
	core = bcma_find_core(bus, bcma_cc_core_id(bus));
	if (core) {
		bus->drv_cc.core = core;
		bcma_core_chipcommon_init(&bus->drv_cc);
	}

	/* Init MIPS core */
	core = bcma_find_core(bus, BCMA_CORE_MIPS_74K);
	if (core) {
		bus->drv_mips.core = core;
		bcma_core_mips_init(&bus->drv_mips);
	}

	/* Init PCIE core */
	core = bcma_find_core_unit(bus, BCMA_CORE_PCIE, 0);
	if (core) {
		bus->drv_pci[0].core = core;
		bcma_core_pci_init(&bus->drv_pci[0]);
	}

	/* Init PCIE core */
	core = bcma_find_core_unit(bus, BCMA_CORE_PCIE, 1);
	if (core) {
		bus->drv_pci[1].core = core;
		bcma_core_pci_init(&bus->drv_pci[1]);
	}

	/* Init GBIT MAC COMMON core */
	core = bcma_find_core(bus, BCMA_CORE_4706_MAC_GBIT_COMMON);
	if (core) {
		bus->drv_gmac_cmn.core = core;
		bcma_core_gmac_cmn_init(&bus->drv_gmac_cmn);
	}

	/* Register found cores */
	bcma_register_cores(bus);

	bcma_info(bus, "Bus registered\n");

	return 0;
}

void bcma_bus_unregister(struct bcma_bus *bus)
{
	struct bcma_device *cores[3];
	int err;

	err = bcma_gpio_unregister(&bus->drv_cc);
	if (err == -EBUSY)
		bcma_err(bus, "Some GPIOs are still in use.\n");
	else if (err)
		bcma_err(bus, "Can not unregister GPIO driver: %i\n", err);

	cores[0] = bcma_find_core(bus, BCMA_CORE_MIPS_74K);
	cores[1] = bcma_find_core(bus, BCMA_CORE_PCIE);
	cores[2] = bcma_find_core(bus, BCMA_CORE_4706_MAC_GBIT_COMMON);

	bcma_unregister_cores(bus);

	kfree(cores[2]);
	kfree(cores[1]);
	kfree(cores[0]);
}

int __init bcma_bus_early_register(struct bcma_bus *bus,
				   struct bcma_device *core_cc,
				   struct bcma_device *core_mips)
{
	int err;
	struct bcma_device *core;
	struct bcma_device_id match;

	bcma_init_bus(bus);

	match.manuf = BCMA_MANUF_BCM;
	match.id = bcma_cc_core_id(bus);
	match.class = BCMA_CL_SIM;
	match.rev = BCMA_ANY_REV;

	/* Scan for chip common core */
	err = bcma_bus_scan_early(bus, &match, core_cc);
	if (err) {
		bcma_err(bus, "Failed to scan for common core: %d\n", err);
		return -1;
	}

	match.manuf = BCMA_MANUF_MIPS;
	match.id = BCMA_CORE_MIPS_74K;
	match.class = BCMA_CL_SIM;
	match.rev = BCMA_ANY_REV;

	/* Scan for mips core */
	err = bcma_bus_scan_early(bus, &match, core_mips);
	if (err) {
		bcma_err(bus, "Failed to scan for mips core: %d\n", err);
		return -1;
	}

	/* Early init CC core */
	core = bcma_find_core(bus, bcma_cc_core_id(bus));
	if (core) {
		bus->drv_cc.core = core;
		bcma_core_chipcommon_early_init(&bus->drv_cc);
	}

	/* Early init MIPS core */
	core = bcma_find_core(bus, BCMA_CORE_MIPS_74K);
	if (core) {
		bus->drv_mips.core = core;
		bcma_core_mips_early_init(&bus->drv_mips);
	}

	bcma_info(bus, "Early bus registered\n");

	return 0;
}

#ifdef CONFIG_PM
int bcma_bus_suspend(struct bcma_bus *bus)
{
	struct bcma_device *core;

	list_for_each_entry(core, &bus->cores, list) {
		struct device_driver *drv = core->dev.driver;
		if (drv) {
			struct bcma_driver *adrv = container_of(drv, struct bcma_driver, drv);
			if (adrv->suspend)
				adrv->suspend(core);
		}
	}
	return 0;
}

int bcma_bus_resume(struct bcma_bus *bus)
{
	struct bcma_device *core;

	/* Init CC core */
	if (bus->drv_cc.core) {
		bus->drv_cc.setup_done = false;
		bcma_core_chipcommon_init(&bus->drv_cc);
	}

	list_for_each_entry(core, &bus->cores, list) {
		struct device_driver *drv = core->dev.driver;
		if (drv) {
			struct bcma_driver *adrv = container_of(drv, struct bcma_driver, drv);
			if (adrv->resume)
				adrv->resume(core);
		}
	}

	return 0;
}
#endif

int __bcma_driver_register(struct bcma_driver *drv, struct module *owner)
{
	drv->drv.name = drv->name;
	drv->drv.bus = &bcma_bus_type;
	drv->drv.owner = owner;

	return driver_register(&drv->drv);
}
EXPORT_SYMBOL_GPL(__bcma_driver_register);

void bcma_driver_unregister(struct bcma_driver *drv)
{
	driver_unregister(&drv->drv);
}
EXPORT_SYMBOL_GPL(bcma_driver_unregister);

static int bcma_bus_match(struct device *dev, struct device_driver *drv)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	struct bcma_driver *adrv = container_of(drv, struct bcma_driver, drv);
	const struct bcma_device_id *cid = &core->id;
	const struct bcma_device_id *did;

	for (did = adrv->id_table; did->manuf || did->id || did->rev; did++) {
	    if ((did->manuf == cid->manuf || did->manuf == BCMA_ANY_MANUF) &&
		(did->id == cid->id || did->id == BCMA_ANY_ID) &&
		(did->rev == cid->rev || did->rev == BCMA_ANY_REV) &&
		(did->class == cid->class || did->class == BCMA_ANY_CLASS))
			return 1;
	}
	return 0;
}

static int bcma_device_probe(struct device *dev)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	struct bcma_driver *adrv = container_of(dev->driver, struct bcma_driver,
					       drv);
	int err = 0;

	if (adrv->probe)
		err = adrv->probe(core);

	return err;
}

static int bcma_device_remove(struct device *dev)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	struct bcma_driver *adrv = container_of(dev->driver, struct bcma_driver,
					       drv);

	if (adrv->remove)
		adrv->remove(core);

	return 0;
}

static int bcma_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);

	return add_uevent_var(env,
			      "MODALIAS=bcma:m%04Xid%04Xrev%02Xcl%02X",
			      core->id.manuf, core->id.id,
			      core->id.rev, core->id.class);
}

static int __init bcma_modinit(void)
{
	int err;

	err = bus_register(&bcma_bus_type);
	if (err)
		return err;

#ifdef CONFIG_BCMA_HOST_PCI
	err = bcma_host_pci_init();
	if (err) {
		pr_err("PCI host initialization failed\n");
		err = 0;
	}
#endif

	return err;
}
fs_initcall(bcma_modinit);

static void __exit bcma_modexit(void)
{
#ifdef CONFIG_BCMA_HOST_PCI
	bcma_host_pci_exit();
#endif
	bus_unregister(&bcma_bus_type);
}
module_exit(bcma_modexit)
