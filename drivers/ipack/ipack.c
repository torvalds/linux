/*
 * Industry-pack bus support functions.
 *
 * Copyright (C) 2011-2012 CERN (www.cern.ch)
 * Author: Samuel Iglesias Gonsalvez <siglesias@igalia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/io.h>
#include <linux/ipack.h>

#define to_ipack_dev(device) container_of(device, struct ipack_device, dev)
#define to_ipack_driver(drv) container_of(drv, struct ipack_driver, driver)

static DEFINE_IDA(ipack_ida);

static void ipack_device_release(struct device *dev)
{
	struct ipack_device *device = to_ipack_dev(dev);
	kfree(device->id);
	device->release(device);
}

static inline const struct ipack_device_id *
ipack_match_one_device(const struct ipack_device_id *id,
		       const struct ipack_device *device)
{
	if ((id->format == IPACK_ANY_FORMAT ||
				id->format == device->id_format) &&
	    (id->vendor == IPACK_ANY_ID || id->vendor == device->id_vendor) &&
	    (id->device == IPACK_ANY_ID || id->device == device->id_device))
		return id;
	return NULL;
}

static const struct ipack_device_id *
ipack_match_id(const struct ipack_device_id *ids, struct ipack_device *idev)
{
	if (ids) {
		while (ids->vendor || ids->device) {
			if (ipack_match_one_device(ids, idev))
				return ids;
			ids++;
		}
	}
	return NULL;
}

static int ipack_bus_match(struct device *dev, struct device_driver *drv)
{
	struct ipack_device *idev = to_ipack_dev(dev);
	struct ipack_driver *idrv = to_ipack_driver(drv);
	const struct ipack_device_id *found_id;

	found_id = ipack_match_id(idrv->id_table, idev);
	return found_id ? 1 : 0;
}

static int ipack_bus_probe(struct device *device)
{
	struct ipack_device *dev = to_ipack_dev(device);
	struct ipack_driver *drv = to_ipack_driver(device->driver);

	if (!drv->ops->probe)
		return -EINVAL;

	return drv->ops->probe(dev);
}

static int ipack_bus_remove(struct device *device)
{
	struct ipack_device *dev = to_ipack_dev(device);
	struct ipack_driver *drv = to_ipack_driver(device->driver);

	if (!drv->ops->remove)
		return -EINVAL;

	drv->ops->remove(dev);
	return 0;
}

static int ipack_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct ipack_device *idev;

	if (!dev)
		return -ENODEV;

	idev = to_ipack_dev(dev);

	if (add_uevent_var(env,
			   "MODALIAS=ipack:f%02Xv%08Xd%08X", idev->id_format,
			   idev->id_vendor, idev->id_device))
		return -ENOMEM;

	return 0;
}

#define ipack_device_attr(field, format_string)				\
static ssize_t								\
field##_show(struct device *dev, struct device_attribute *attr,		\
		char *buf)						\
{									\
	struct ipack_device *idev = to_ipack_dev(dev);			\
	return sprintf(buf, format_string, idev->field);		\
}

static ssize_t id_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	unsigned int i, c, l, s;
	struct ipack_device *idev = to_ipack_dev(dev);


	switch (idev->id_format) {
	case IPACK_ID_VERSION_1:
		l = 0x7; s = 1; break;
	case IPACK_ID_VERSION_2:
		l = 0xf; s = 2; break;
	default:
		return -EIO;
	}
	c = 0;
	for (i = 0; i < idev->id_avail; i++) {
		if (i > 0) {
			if ((i & l) == 0)
				buf[c++] = '\n';
			else if ((i & s) == 0)
				buf[c++] = ' ';
		}
		sprintf(&buf[c], "%02x", idev->id[i]);
		c += 2;
	}
	buf[c++] = '\n';
	return c;
}

static ssize_t
id_vendor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ipack_device *idev = to_ipack_dev(dev);
	switch (idev->id_format) {
	case IPACK_ID_VERSION_1:
		return sprintf(buf, "0x%02x\n", idev->id_vendor);
	case IPACK_ID_VERSION_2:
		return sprintf(buf, "0x%06x\n", idev->id_vendor);
	default:
		return -EIO;
	}
}

static ssize_t
id_device_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ipack_device *idev = to_ipack_dev(dev);
	switch (idev->id_format) {
	case IPACK_ID_VERSION_1:
		return sprintf(buf, "0x%02x\n", idev->id_device);
	case IPACK_ID_VERSION_2:
		return sprintf(buf, "0x%04x\n", idev->id_device);
	default:
		return -EIO;
	}
}

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct ipack_device *idev = to_ipack_dev(dev);

	return sprintf(buf, "ipac:f%02Xv%08Xd%08X", idev->id_format,
		       idev->id_vendor, idev->id_device);
}

ipack_device_attr(id_format, "0x%hhx\n");

static DEVICE_ATTR_RO(id);
static DEVICE_ATTR_RO(id_device);
static DEVICE_ATTR_RO(id_format);
static DEVICE_ATTR_RO(id_vendor);
static DEVICE_ATTR_RO(modalias);

static struct attribute *ipack_attrs[] = {
	&dev_attr_id.attr,
	&dev_attr_id_device.attr,
	&dev_attr_id_format.attr,
	&dev_attr_id_vendor.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ipack);

static struct bus_type ipack_bus_type = {
	.name      = "ipack",
	.probe     = ipack_bus_probe,
	.match     = ipack_bus_match,
	.remove    = ipack_bus_remove,
	.dev_groups = ipack_groups,
	.uevent	   = ipack_uevent,
};

struct ipack_bus_device *ipack_bus_register(struct device *parent, int slots,
					    const struct ipack_bus_ops *ops,
					    struct module *owner)
{
	int bus_nr;
	struct ipack_bus_device *bus;

	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return NULL;

	bus_nr = ida_simple_get(&ipack_ida, 0, 0, GFP_KERNEL);
	if (bus_nr < 0) {
		kfree(bus);
		return NULL;
	}

	bus->bus_nr = bus_nr;
	bus->parent = parent;
	bus->slots = slots;
	bus->ops = ops;
	bus->owner = owner;
	return bus;
}
EXPORT_SYMBOL_GPL(ipack_bus_register);

static int ipack_unregister_bus_member(struct device *dev, void *data)
{
	struct ipack_device *idev = to_ipack_dev(dev);
	struct ipack_bus_device *bus = data;

	if (idev->bus == bus)
		ipack_device_del(idev);

	return 1;
}

int ipack_bus_unregister(struct ipack_bus_device *bus)
{
	bus_for_each_dev(&ipack_bus_type, NULL, bus,
		ipack_unregister_bus_member);
	ida_simple_remove(&ipack_ida, bus->bus_nr);
	kfree(bus);
	return 0;
}
EXPORT_SYMBOL_GPL(ipack_bus_unregister);

int ipack_driver_register(struct ipack_driver *edrv, struct module *owner,
			  const char *name)
{
	edrv->driver.owner = owner;
	edrv->driver.name = name;
	edrv->driver.bus = &ipack_bus_type;
	return driver_register(&edrv->driver);
}
EXPORT_SYMBOL_GPL(ipack_driver_register);

void ipack_driver_unregister(struct ipack_driver *edrv)
{
	driver_unregister(&edrv->driver);
}
EXPORT_SYMBOL_GPL(ipack_driver_unregister);

static u16 ipack_crc_byte(u16 crc, u8 c)
{
	int i;

	crc ^= c << 8;
	for (i = 0; i < 8; i++)
		crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
	return crc;
}

/*
 * The algorithm in lib/crc-ccitt.c does not seem to apply since it uses the
 * opposite bit ordering.
 */
static u8 ipack_calc_crc1(struct ipack_device *dev)
{
	u8 c;
	u16 crc;
	unsigned int i;

	crc = 0xffff;
	for (i = 0; i < dev->id_avail; i++) {
		c = (i != 11) ? dev->id[i] : 0;
		crc = ipack_crc_byte(crc, c);
	}
	crc = ~crc;
	return crc & 0xff;
}

static u16 ipack_calc_crc2(struct ipack_device *dev)
{
	u8 c;
	u16 crc;
	unsigned int i;

	crc = 0xffff;
	for (i = 0; i < dev->id_avail; i++) {
		c = ((i != 0x18) && (i != 0x19)) ? dev->id[i] : 0;
		crc = ipack_crc_byte(crc, c);
	}
	crc = ~crc;
	return crc;
}

static void ipack_parse_id1(struct ipack_device *dev)
{
	u8 *id = dev->id;
	u8 crc;

	dev->id_vendor = id[4];
	dev->id_device = id[5];
	dev->speed_8mhz = 1;
	dev->speed_32mhz = (id[7] == 'H');
	crc = ipack_calc_crc1(dev);
	dev->id_crc_correct = (crc == id[11]);
	if (!dev->id_crc_correct) {
		dev_warn(&dev->dev, "ID CRC invalid found 0x%x, expected 0x%x.\n",
				id[11], crc);
	}
}

static void ipack_parse_id2(struct ipack_device *dev)
{
	__be16 *id = (__be16 *) dev->id;
	u16 flags, crc;

	dev->id_vendor = ((be16_to_cpu(id[3]) & 0xff) << 16)
			 + be16_to_cpu(id[4]);
	dev->id_device = be16_to_cpu(id[5]);
	flags = be16_to_cpu(id[10]);
	dev->speed_8mhz = !!(flags & 2);
	dev->speed_32mhz = !!(flags & 4);
	crc = ipack_calc_crc2(dev);
	dev->id_crc_correct = (crc == be16_to_cpu(id[12]));
	if (!dev->id_crc_correct) {
		dev_warn(&dev->dev, "ID CRC invalid found 0x%x, expected 0x%x.\n",
				id[11], crc);
	}
}

static int ipack_device_read_id(struct ipack_device *dev)
{
	u8 __iomem *idmem;
	int i;
	int ret = 0;

	idmem = ioremap(dev->region[IPACK_ID_SPACE].start,
			dev->region[IPACK_ID_SPACE].size);
	if (!idmem) {
		dev_err(&dev->dev, "error mapping memory\n");
		return -ENOMEM;
	}

	/* Determine ID PROM Data Format.  If we find the ids "IPAC" or "IPAH"
	 * we are dealing with a IndustryPack  format 1 device.  If we detect
	 * "VITA4 " (16 bit big endian formatted) we are dealing with a
	 * IndustryPack format 2 device */
	if ((ioread8(idmem + 1) == 'I') &&
			(ioread8(idmem + 3) == 'P') &&
			(ioread8(idmem + 5) == 'A') &&
			((ioread8(idmem + 7) == 'C') ||
			 (ioread8(idmem + 7) == 'H'))) {
		dev->id_format = IPACK_ID_VERSION_1;
		dev->id_avail = ioread8(idmem + 0x15);
		if ((dev->id_avail < 0x0c) || (dev->id_avail > 0x40)) {
			dev_warn(&dev->dev, "invalid id size");
			dev->id_avail = 0x0c;
		}
	} else if ((ioread8(idmem + 0) == 'I') &&
			(ioread8(idmem + 1) == 'V') &&
			(ioread8(idmem + 2) == 'A') &&
			(ioread8(idmem + 3) == 'T') &&
			(ioread8(idmem + 4) == ' ') &&
			(ioread8(idmem + 5) == '4')) {
		dev->id_format = IPACK_ID_VERSION_2;
		dev->id_avail = ioread16be(idmem + 0x16);
		if ((dev->id_avail < 0x1a) || (dev->id_avail > 0x40)) {
			dev_warn(&dev->dev, "invalid id size");
			dev->id_avail = 0x1a;
		}
	} else {
		dev->id_format = IPACK_ID_VERSION_INVALID;
		dev->id_avail = 0;
	}

	if (!dev->id_avail) {
		ret = -ENODEV;
		goto out;
	}

	/* Obtain the amount of memory required to store a copy of the complete
	 * ID ROM contents */
	dev->id = kmalloc(dev->id_avail, GFP_KERNEL);
	if (!dev->id) {
		ret = -ENOMEM;
		goto out;
	}
	for (i = 0; i < dev->id_avail; i++) {
		if (dev->id_format == IPACK_ID_VERSION_1)
			dev->id[i] = ioread8(idmem + (i << 1) + 1);
		else
			dev->id[i] = ioread8(idmem + i);
	}

	/* now we can finally work with the copy */
	switch (dev->id_format) {
	case IPACK_ID_VERSION_1:
		ipack_parse_id1(dev);
		break;
	case IPACK_ID_VERSION_2:
		ipack_parse_id2(dev);
		break;
	}

out:
	iounmap(idmem);

	return ret;
}

int ipack_device_init(struct ipack_device *dev)
{
	int ret;

	dev->dev.bus = &ipack_bus_type;
	dev->dev.release = ipack_device_release;
	dev->dev.parent = dev->bus->parent;
	dev_set_name(&dev->dev,
		     "ipack-dev.%u.%u", dev->bus->bus_nr, dev->slot);
	device_initialize(&dev->dev);

	if (dev->bus->ops->set_clockrate(dev, 8))
		dev_warn(&dev->dev, "failed to switch to 8 MHz operation for reading of device ID.\n");
	if (dev->bus->ops->reset_timeout(dev))
		dev_warn(&dev->dev, "failed to reset potential timeout.");

	ret = ipack_device_read_id(dev);
	if (ret < 0) {
		dev_err(&dev->dev, "error reading device id section.\n");
		return ret;
	}

	/* if the device supports 32 MHz operation, use it. */
	if (dev->speed_32mhz) {
		ret = dev->bus->ops->set_clockrate(dev, 32);
		if (ret < 0)
			dev_err(&dev->dev, "failed to switch to 32 MHz operation.\n");
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ipack_device_init);

int ipack_device_add(struct ipack_device *dev)
{
	return device_add(&dev->dev);
}
EXPORT_SYMBOL_GPL(ipack_device_add);

void ipack_device_del(struct ipack_device *dev)
{
	device_del(&dev->dev);
	ipack_put_device(dev);
}
EXPORT_SYMBOL_GPL(ipack_device_del);

void ipack_get_device(struct ipack_device *dev)
{
	get_device(&dev->dev);
}
EXPORT_SYMBOL_GPL(ipack_get_device);

void ipack_put_device(struct ipack_device *dev)
{
	put_device(&dev->dev);
}
EXPORT_SYMBOL_GPL(ipack_put_device);

static int __init ipack_init(void)
{
	ida_init(&ipack_ida);
	return bus_register(&ipack_bus_type);
}

static void __exit ipack_exit(void)
{
	bus_unregister(&ipack_bus_type);
	ida_destroy(&ipack_ida);
}

module_init(ipack_init);
module_exit(ipack_exit);

MODULE_AUTHOR("Samuel Iglesias Gonsalvez <siglesias@igalia.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Industry-pack bus core");
