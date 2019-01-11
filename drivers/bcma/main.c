/*
 * Broadcom specific AMBA
 * Bus subsystem
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/module.h>
#include <linux/mmc/sdio_func.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/bcma/bcma.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

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
static DEVICE_ATTR_RO(manuf);

static ssize_t id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	return sprintf(buf, "0x%03X\n", core->id.id);
}
static DEVICE_ATTR_RO(id);

static ssize_t rev_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	return sprintf(buf, "0x%02X\n", core->id.rev);
}
static DEVICE_ATTR_RO(rev);

static ssize_t class_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	return sprintf(buf, "0x%X\n", core->id.class);
}
static DEVICE_ATTR_RO(class);

static struct attribute *bcma_device_attrs[] = {
	&dev_attr_manuf.attr,
	&dev_attr_id.attr,
	&dev_attr_rev.attr,
	&dev_attr_class.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bcma_device);

static struct bus_type bcma_bus_type = {
	.name		= "bcma",
	.match		= bcma_bus_match,
	.probe		= bcma_device_probe,
	.remove		= bcma_device_remove,
	.uevent		= bcma_device_uevent,
	.dev_groups	= bcma_device_groups,
};

static u16 bcma_cc_core_id(struct bcma_bus *bus)
{
	if (bus->chipinfo.id == BCMA_CHIP_ID_BCM4706)
		return BCMA_CORE_4706_CHIPCOMMON;
	return BCMA_CORE_CHIPCOMMON;
}

struct bcma_device *bcma_find_core_unit(struct bcma_bus *bus, u16 coreid,
					u8 unit)
{
	struct bcma_device *core;

	list_for_each_entry(core, &bus->cores, list) {
		if (core->id.id == coreid && core->core_unit == unit)
			return core;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(bcma_find_core_unit);

bool bcma_wait_value(struct bcma_device *core, u16 reg, u32 mask, u32 value,
		     int timeout)
{
	unsigned long deadline = jiffies + timeout;
	u32 val;

	do {
		val = bcma_read32(core, reg);
		if ((val & mask) == value)
			return true;
		cpu_relax();
		udelay(10);
	} while (!time_after_eq(jiffies, deadline));

	bcma_warn(core->bus, "Timeout waiting for register 0x%04X!\n", reg);

	return false;
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

static bool bcma_is_core_needed_early(u16 core_id)
{
	switch (core_id) {
	case BCMA_CORE_NS_NAND:
	case BCMA_CORE_NS_QSPI:
		return true;
	}

	return false;
}

static struct device_node *bcma_of_find_child_device(struct device *parent,
						     struct bcma_device *core)
{
	struct device_node *node;
	u64 size;
	const __be32 *reg;

	if (!parent->of_node)
		return NULL;

	for_each_child_of_node(parent->of_node, node) {
		reg = of_get_address(node, 0, &size, NULL);
		if (!reg)
			continue;
		if (of_translate_address(node, reg) == core->addr)
			return node;
	}
	return NULL;
}

static int bcma_of_irq_parse(struct device *parent,
			     struct bcma_device *core,
			     struct of_phandle_args *out_irq, int num)
{
	__be32 laddr[1];
	int rc;

	if (core->dev.of_node) {
		rc = of_irq_parse_one(core->dev.of_node, num, out_irq);
		if (!rc)
			return rc;
	}

	out_irq->np = parent->of_node;
	out_irq->args_count = 1;
	out_irq->args[0] = num;

	laddr[0] = cpu_to_be32(core->addr);
	return of_irq_parse_raw(laddr, out_irq);
}

static unsigned int bcma_of_get_irq(struct device *parent,
				    struct bcma_device *core, int num)
{
	struct of_phandle_args out_irq;
	int ret;

	if (!IS_ENABLED(CONFIG_OF_IRQ) || !parent->of_node)
		return 0;

	ret = bcma_of_irq_parse(parent, core, &out_irq, num);
	if (ret) {
		bcma_debug(core->bus, "bcma_of_get_irq() failed with rc=%d\n",
			   ret);
		return 0;
	}

	return irq_create_of_mapping(&out_irq);
}

static void bcma_of_fill_device(struct device *parent,
				struct bcma_device *core)
{
	struct device_node *node;

	node = bcma_of_find_child_device(parent, core);
	if (node)
		core->dev.of_node = node;

	core->irq = bcma_of_get_irq(parent, core, 0);

	of_dma_configure(&core->dev, node, false);
}

unsigned int bcma_core_irq(struct bcma_device *core, int num)
{
	struct bcma_bus *bus = core->bus;
	unsigned int mips_irq;

	switch (bus->hosttype) {
	case BCMA_HOSTTYPE_PCI:
		return bus->host_pci->irq;
	case BCMA_HOSTTYPE_SOC:
		if (bus->drv_mips.core && num == 0) {
			mips_irq = bcma_core_mips_irq(core);
			return mips_irq <= 4 ? mips_irq + 2 : 0;
		}
		if (bus->host_pdev)
			return bcma_of_get_irq(&bus->host_pdev->dev, core, num);
		return 0;
	case BCMA_HOSTTYPE_SDIO:
		return 0;
	}

	return 0;
}
EXPORT_SYMBOL(bcma_core_irq);

void bcma_prepare_core(struct bcma_bus *bus, struct bcma_device *core)
{
	core->dev.release = bcma_release_core_dev;
	core->dev.bus = &bcma_bus_type;
	dev_set_name(&core->dev, "bcma%d:%d", bus->num, core->core_index);
	core->dev.parent = bcma_bus_get_host_dev(bus);
	if (core->dev.parent)
		bcma_of_fill_device(core->dev.parent, core);

	switch (bus->hosttype) {
	case BCMA_HOSTTYPE_PCI:
		core->dma_dev = &bus->host_pci->dev;
		core->irq = bus->host_pci->irq;
		break;
	case BCMA_HOSTTYPE_SOC:
		if (IS_ENABLED(CONFIG_OF) && bus->host_pdev) {
			core->dma_dev = &bus->host_pdev->dev;
		} else {
			core->dev.dma_mask = &core->dev.coherent_dma_mask;
			core->dma_dev = &core->dev;
		}
		break;
	case BCMA_HOSTTYPE_SDIO:
		break;
	}
}

struct device *bcma_bus_get_host_dev(struct bcma_bus *bus)
{
	switch (bus->hosttype) {
	case BCMA_HOSTTYPE_PCI:
		if (bus->host_pci)
			return &bus->host_pci->dev;
		else
			return NULL;
	case BCMA_HOSTTYPE_SOC:
		if (bus->host_pdev)
			return &bus->host_pdev->dev;
		else
			return NULL;
	case BCMA_HOSTTYPE_SDIO:
		if (bus->host_sdio)
			return &bus->host_sdio->dev;
		else
			return NULL;
	}
	return NULL;
}

void bcma_init_bus(struct bcma_bus *bus)
{
	mutex_lock(&bcma_buses_mutex);
	bus->num = bcma_bus_next_num++;
	mutex_unlock(&bcma_buses_mutex);

	INIT_LIST_HEAD(&bus->cores);
	bus->nr_cores = 0;

	bcma_detect_chip(bus);
}

static void bcma_register_core(struct bcma_bus *bus, struct bcma_device *core)
{
	int err;

	err = device_register(&core->dev);
	if (err) {
		bcma_err(bus, "Could not register dev for core 0x%03X\n",
			 core->id.id);
		put_device(&core->dev);
		return;
	}
	core->dev_registered = true;
}

static int bcma_register_devices(struct bcma_bus *bus)
{
	struct bcma_device *core;
	int err;

	list_for_each_entry(core, &bus->cores, list) {
		/* We support that cores ourself */
		switch (core->id.id) {
		case BCMA_CORE_4706_CHIPCOMMON:
		case BCMA_CORE_CHIPCOMMON:
		case BCMA_CORE_NS_CHIPCOMMON_B:
		case BCMA_CORE_PCI:
		case BCMA_CORE_PCIE:
		case BCMA_CORE_PCIE2:
		case BCMA_CORE_MIPS_74K:
		case BCMA_CORE_4706_MAC_GBIT_COMMON:
			continue;
		}

		/* Early cores were already registered */
		if (bcma_is_core_needed_early(core->id.id))
			continue;

		/* Only first GMAC core on BCM4706 is connected and working */
		if (core->id.id == BCMA_CORE_4706_MAC_GBIT &&
		    core->core_unit > 0)
			continue;

		bcma_register_core(bus, core);
	}

#ifdef CONFIG_BCMA_PFLASH
	if (bus->drv_cc.pflash.present) {
		err = platform_device_register(&bcma_pflash_dev);
		if (err)
			bcma_err(bus, "Error registering parallel flash\n");
	}
#endif

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

void bcma_unregister_cores(struct bcma_bus *bus)
{
	struct bcma_device *core, *tmp;

	list_for_each_entry_safe(core, tmp, &bus->cores, list) {
		if (!core->dev_registered)
			continue;
		list_del(&core->list);
		device_unregister(&core->dev);
	}
	if (bus->hosttype == BCMA_HOSTTYPE_SOC)
		platform_device_unregister(bus->drv_cc.watchdog);

	/* Now noone uses internally-handled cores, we can free them */
	list_for_each_entry_safe(core, tmp, &bus->cores, list) {
		list_del(&core->list);
		kfree(core);
	}
}

int bcma_bus_register(struct bcma_bus *bus)
{
	int err;
	struct bcma_device *core;
	struct device *dev;

	/* Scan for devices (cores) */
	err = bcma_bus_scan(bus);
	if (err) {
		bcma_err(bus, "Failed to scan: %d\n", err);
		return err;
	}

	/* Early init CC core */
	core = bcma_find_core(bus, bcma_cc_core_id(bus));
	if (core) {
		bus->drv_cc.core = core;
		bcma_core_chipcommon_early_init(&bus->drv_cc);
	}

	/* Early init PCIE core */
	core = bcma_find_core(bus, BCMA_CORE_PCIE);
	if (core) {
		bus->drv_pci[0].core = core;
		bcma_core_pci_early_init(&bus->drv_pci[0]);
	}

	dev = bcma_bus_get_host_dev(bus);
	if (dev) {
		of_platform_default_populate(dev->of_node, NULL, dev);
	}

	/* Cores providing flash access go before SPROM init */
	list_for_each_entry(core, &bus->cores, list) {
		if (bcma_is_core_needed_early(core->id.id))
			bcma_register_core(bus, core);
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

	/* Init CC core */
	core = bcma_find_core(bus, BCMA_CORE_NS_CHIPCOMMON_B);
	if (core) {
		bus->drv_cc_b.core = core;
		bcma_core_chipcommon_b_init(&bus->drv_cc_b);
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

	/* Init PCIe Gen 2 core */
	core = bcma_find_core_unit(bus, BCMA_CORE_PCIE2, 0);
	if (core) {
		bus->drv_pcie2.core = core;
		bcma_core_pcie2_init(&bus->drv_pcie2);
	}

	/* Init GBIT MAC COMMON core */
	core = bcma_find_core(bus, BCMA_CORE_4706_MAC_GBIT_COMMON);
	if (core) {
		bus->drv_gmac_cmn.core = core;
		bcma_core_gmac_cmn_init(&bus->drv_gmac_cmn);
	}

	/* Register found cores */
	bcma_register_devices(bus);

	bcma_info(bus, "Bus registered\n");

	return 0;
}

void bcma_bus_unregister(struct bcma_bus *bus)
{
	int err;

	err = bcma_gpio_unregister(&bus->drv_cc);
	if (err == -EBUSY)
		bcma_err(bus, "Some GPIOs are still in use.\n");
	else if (err)
		bcma_err(bus, "Can not unregister GPIO driver: %i\n", err);

	bcma_core_chipcommon_b_free(&bus->drv_cc_b);

	bcma_unregister_cores(bus);
}

/*
 * This is a special version of bus registration function designed for SoCs.
 * It scans bus and performs basic initialization of main cores only.
 * Please note it requires memory allocation, however it won't try to sleep.
 */
int __init bcma_bus_early_register(struct bcma_bus *bus)
{
	int err;
	struct bcma_device *core;

	/* Scan for devices (cores) */
	err = bcma_bus_scan(bus);
	if (err) {
		bcma_err(bus, "Failed to scan bus: %d\n", err);
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

	get_device(dev);
	if (adrv->probe)
		err = adrv->probe(core);
	if (err)
		put_device(dev);

	return err;
}

static int bcma_device_remove(struct device *dev)
{
	struct bcma_device *core = container_of(dev, struct bcma_device, dev);
	struct bcma_driver *adrv = container_of(dev->driver, struct bcma_driver,
					       drv);

	if (adrv->remove)
		adrv->remove(core);
	put_device(dev);

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

static unsigned int bcma_bus_registered;

/*
 * If built-in, bus has to be registered early, before any driver calls
 * bcma_driver_register.
 * Otherwise registering driver would trigger BUG in driver_register.
 */
static int __init bcma_init_bus_register(void)
{
	int err;

	if (bcma_bus_registered)
		return 0;

	err = bus_register(&bcma_bus_type);
	if (!err)
		bcma_bus_registered = 1;

	return err;
}
#ifndef MODULE
fs_initcall(bcma_init_bus_register);
#endif

/* Main initialization has to be done with SPI/mtd/NAND/SPROM available */
static int __init bcma_modinit(void)
{
	int err;

	err = bcma_init_bus_register();
	if (err)
		return err;

	err = bcma_host_soc_register_driver();
	if (err) {
		pr_err("SoC host initialization failed\n");
		err = 0;
	}
#ifdef CONFIG_BCMA_HOST_PCI
	err = bcma_host_pci_init();
	if (err) {
		pr_err("PCI host initialization failed\n");
		err = 0;
	}
#endif

	return err;
}
module_init(bcma_modinit);

static void __exit bcma_modexit(void)
{
#ifdef CONFIG_BCMA_HOST_PCI
	bcma_host_pci_exit();
#endif
	bcma_host_soc_unregister_driver();
	bus_unregister(&bcma_bus_type);
}
module_exit(bcma_modexit)
