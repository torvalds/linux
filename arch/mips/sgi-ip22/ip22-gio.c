// SPDX-License-Identifier: GPL-2.0-only
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <asm/addrspace.h>
#include <asm/paccess.h>
#include <asm/gio_device.h>
#include <asm/sgi/gio.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/mc.h>
#include <asm/sgi/ip22.h>

static struct bus_type gio_bus_type;

static struct {
	const char *name;
	__u8	   id;
} gio_name_table[] = {
	{ .name = "SGI Impact", .id = 0x10 },
	{ .name = "Phobos G160", .id = 0x35 },
	{ .name = "Phobos G130", .id = 0x36 },
	{ .name = "Phobos G100", .id = 0x37 },
	{ .name = "Set Engineering GFE", .id = 0x38 },
	/* fake IDs */
	{ .name = "SGI Newport", .id = 0x7e },
	{ .name = "SGI GR2/GR3", .id = 0x7f },
};

static void gio_bus_release(struct device *dev)
{
	kfree(dev);
}

static struct device gio_bus = {
	.init_name = "gio",
	.release = &gio_bus_release,
};

/**
 * gio_match_device - Tell if an of_device structure has a matching
 * gio_match structure
 * @ids: array of of device match structures to search in
 * @dev: the of device structure to match against
 *
 * Used by a driver to check whether an of_device present in the
 * system is in its list of supported devices.
 */
static const struct gio_device_id *
gio_match_device(const struct gio_device_id *match,
		 const struct gio_device *dev)
{
	const struct gio_device_id *ids;

	for (ids = match; ids->id != 0xff; ids++)
		if (ids->id == dev->id.id)
			return ids;

	return NULL;
}

struct gio_device *gio_dev_get(struct gio_device *dev)
{
	struct device *tmp;

	if (!dev)
		return NULL;
	tmp = get_device(&dev->dev);
	if (tmp)
		return to_gio_device(tmp);
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(gio_dev_get);

void gio_dev_put(struct gio_device *dev)
{
	if (dev)
		put_device(&dev->dev);
}
EXPORT_SYMBOL_GPL(gio_dev_put);

/**
 * gio_release_dev - free an gio device structure when all users of it are finished.
 * @dev: device that's been disconnected
 *
 * Will be called only by the device core when all users of this gio device are
 * done.
 */
void gio_release_dev(struct device *dev)
{
	struct gio_device *giodev;

	giodev = to_gio_device(dev);
	kfree(giodev);
}
EXPORT_SYMBOL_GPL(gio_release_dev);

int gio_device_register(struct gio_device *giodev)
{
	giodev->dev.bus = &gio_bus_type;
	giodev->dev.parent = &gio_bus;
	return device_register(&giodev->dev);
}
EXPORT_SYMBOL_GPL(gio_device_register);

void gio_device_unregister(struct gio_device *giodev)
{
	device_unregister(&giodev->dev);
}
EXPORT_SYMBOL_GPL(gio_device_unregister);

static int gio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct gio_device *gio_dev = to_gio_device(dev);
	struct gio_driver *gio_drv = to_gio_driver(drv);

	return gio_match_device(gio_drv->id_table, gio_dev) != NULL;
}

static int gio_device_probe(struct device *dev)
{
	int error = -ENODEV;
	struct gio_driver *drv;
	struct gio_device *gio_dev;
	const struct gio_device_id *match;

	drv = to_gio_driver(dev->driver);
	gio_dev = to_gio_device(dev);

	if (!drv->probe)
		return error;

	gio_dev_get(gio_dev);

	match = gio_match_device(drv->id_table, gio_dev);
	if (match)
		error = drv->probe(gio_dev, match);
	if (error)
		gio_dev_put(gio_dev);

	return error;
}

static void gio_device_remove(struct device *dev)
{
	struct gio_device *gio_dev = to_gio_device(dev);
	struct gio_driver *drv = to_gio_driver(dev->driver);

	if (drv->remove)
		drv->remove(gio_dev);
}

static void gio_device_shutdown(struct device *dev)
{
	struct gio_device *gio_dev = to_gio_device(dev);
	struct gio_driver *drv = to_gio_driver(dev->driver);

	if (dev->driver && drv->shutdown)
		drv->shutdown(gio_dev);
}

static ssize_t modalias_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	struct gio_device *gio_dev = to_gio_device(dev);
	int len = snprintf(buf, PAGE_SIZE, "gio:%x\n", gio_dev->id.id);

	return (len >= PAGE_SIZE) ? (PAGE_SIZE - 1) : len;
}
static DEVICE_ATTR_RO(modalias);

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct gio_device *giodev;

	giodev = to_gio_device(dev);
	return sprintf(buf, "%s", giodev->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t id_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	struct gio_device *giodev;

	giodev = to_gio_device(dev);
	return sprintf(buf, "%x", giodev->id.id);
}
static DEVICE_ATTR_RO(id);

static struct attribute *gio_dev_attrs[] = {
	&dev_attr_modalias.attr,
	&dev_attr_name.attr,
	&dev_attr_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gio_dev);

static int gio_device_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct gio_device *gio_dev = to_gio_device(dev);

	add_uevent_var(env, "MODALIAS=gio:%x", gio_dev->id.id);
	return 0;
}

int gio_register_driver(struct gio_driver *drv)
{
	/* initialize common driver fields */
	if (!drv->driver.name)
		drv->driver.name = drv->name;
	if (!drv->driver.owner)
		drv->driver.owner = drv->owner;
	drv->driver.bus = &gio_bus_type;

	/* register with core */
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(gio_register_driver);

void gio_unregister_driver(struct gio_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(gio_unregister_driver);

void gio_set_master(struct gio_device *dev)
{
	u32 tmp = sgimc->giopar;

	switch (dev->slotno) {
	case 0:
		tmp |= SGIMC_GIOPAR_MASTERGFX;
		break;
	case 1:
		tmp |= SGIMC_GIOPAR_MASTEREXP0;
		break;
	case 2:
		tmp |= SGIMC_GIOPAR_MASTEREXP1;
		break;
	}
	sgimc->giopar = tmp;
}
EXPORT_SYMBOL_GPL(gio_set_master);

void ip22_gio_set_64bit(int slotno)
{
	u32 tmp = sgimc->giopar;

	switch (slotno) {
	case 0:
		tmp |= SGIMC_GIOPAR_GFX64;
		break;
	case 1:
		tmp |= SGIMC_GIOPAR_EXP064;
		break;
	case 2:
		tmp |= SGIMC_GIOPAR_EXP164;
		break;
	}
	sgimc->giopar = tmp;
}

static int ip22_gio_id(unsigned long addr, u32 *res)
{
	u8 tmp8;
	u8 tmp16;
	u32 tmp32;
	u8 *ptr8;
	u16 *ptr16;
	u32 *ptr32;

	ptr32 = (void *)CKSEG1ADDR(addr);
	if (!get_dbe(tmp32, ptr32)) {
		/*
		 * We got no DBE, but this doesn't mean anything.
		 * If GIO is pipelined (which can't be disabled
		 * for GFX slot) we don't get a DBE, but we see
		 * the transfer size as data. So we do an 8bit
		 * and a 16bit access and check whether the common
		 * data matches
		 */
		ptr8 = (void *)CKSEG1ADDR(addr + 3);
		if (get_dbe(tmp8, ptr8)) {
			/*
			 * 32bit access worked, but 8bit doesn't
			 * so we don't see phantom reads on
			 * a pipelined bus, but a real card which
			 * doesn't support 8 bit reads
			 */
			*res = tmp32;
			return 1;
		}
		ptr16 = (void *)CKSEG1ADDR(addr + 2);
		get_dbe(tmp16, ptr16);
		if (tmp8 == (tmp16 & 0xff) &&
		    tmp8 == (tmp32 & 0xff) &&
		    tmp16 == (tmp32 & 0xffff)) {
			*res = tmp32;
			return 1;
		}
	}
	return 0; /* nothing here */
}

#define HQ2_MYSTERY_OFFS       0x6A07C
#define NEWPORT_USTATUS_OFFS   0xF133C

static int ip22_is_gr2(unsigned long addr)
{
	u32 tmp;
	u32 *ptr;

	/* HQ2 only allows 32bit accesses */
	ptr = (void *)CKSEG1ADDR(addr + HQ2_MYSTERY_OFFS);
	if (!get_dbe(tmp, ptr)) {
		if (tmp == 0xdeadbeef)
			return 1;
	}
	return 0;
}


static void ip22_check_gio(int slotno, unsigned long addr, int irq)
{
	const char *name = "Unknown";
	struct gio_device *gio_dev;
	u32 tmp;
	__u8 id;
	int i;

	/* first look for GR2/GR3 by checking mystery register */
	if (ip22_is_gr2(addr))
		tmp = 0x7f;
	else {
		if (!ip22_gio_id(addr, &tmp)) {
			/*
			 * no GIO signature at start address of slot
			 * since Newport doesn't have one, we check if
			 * user status register is readable
			 */
			if (ip22_gio_id(addr + NEWPORT_USTATUS_OFFS, &tmp))
				tmp = 0x7e;
			else
				tmp = 0;
		}
	}
	if (tmp) {
		id = GIO_ID(tmp);
		if (tmp & GIO_32BIT_ID) {
			if (tmp & GIO_64BIT_IFACE)
				ip22_gio_set_64bit(slotno);
		}
		for (i = 0; i < ARRAY_SIZE(gio_name_table); i++) {
			if (id == gio_name_table[i].id) {
				name = gio_name_table[i].name;
				break;
			}
		}
		printk(KERN_INFO "GIO: slot %d : %s (id %x)\n",
		       slotno, name, id);
		gio_dev = kzalloc(sizeof *gio_dev, GFP_KERNEL);
		if (!gio_dev)
			return;
		gio_dev->name = name;
		gio_dev->slotno = slotno;
		gio_dev->id.id = id;
		gio_dev->resource.start = addr;
		gio_dev->resource.end = addr + 0x3fffff;
		gio_dev->resource.flags = IORESOURCE_MEM;
		gio_dev->irq = irq;
		dev_set_name(&gio_dev->dev, "%d", slotno);
		gio_device_register(gio_dev);
	} else
		printk(KERN_INFO "GIO: slot %d : Empty\n", slotno);
}

static struct bus_type gio_bus_type = {
	.name	   = "gio",
	.dev_groups = gio_dev_groups,
	.match	   = gio_bus_match,
	.probe	   = gio_device_probe,
	.remove	   = gio_device_remove,
	.shutdown  = gio_device_shutdown,
	.uevent	   = gio_device_uevent,
};

static struct resource gio_bus_resource = {
	.start = GIO_SLOT_GFX_BASE,
	.end   = GIO_SLOT_GFX_BASE + 0x9fffff,
	.name  = "GIO Bus",
	.flags = IORESOURCE_MEM,
};

int __init ip22_gio_init(void)
{
	unsigned int pbdma __maybe_unused;
	int ret;

	ret = device_register(&gio_bus);
	if (ret) {
		put_device(&gio_bus);
		return ret;
	}

	ret = bus_register(&gio_bus_type);
	if (!ret) {
		request_resource(&iomem_resource, &gio_bus_resource);
		printk(KERN_INFO "GIO: Probing bus...\n");

		if (ip22_is_fullhouse()) {
			/* Indigo2 */
			ip22_check_gio(0, GIO_SLOT_GFX_BASE, SGI_GIO_1_IRQ);
			ip22_check_gio(1, GIO_SLOT_EXP0_BASE, SGI_GIO_1_IRQ);
		} else {
			/* Indy/Challenge S */
			if (get_dbe(pbdma, (unsigned int *)&hpc3c1->pbdma[1]))
				ip22_check_gio(0, GIO_SLOT_GFX_BASE,
					       SGI_GIO_0_IRQ);
			ip22_check_gio(1, GIO_SLOT_EXP0_BASE, SGI_GIOEXP0_IRQ);
			ip22_check_gio(2, GIO_SLOT_EXP1_BASE, SGI_GIOEXP1_IRQ);
		}
	} else
		device_unregister(&gio_bus);

	return ret;
}

subsys_initcall(ip22_gio_init);
