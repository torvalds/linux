/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UWB Multi-interface Controller support.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * UMC (UWB Multi-interface Controller) capabilities (e.g., radio
 * controller, host controller) are presented as devices on the "umc"
 * bus.
 *
 * The radio controller is not strictly a UMC capability but it's
 * useful to present it as such.
 *
 * References:
 *
 *   [WHCI] Wireless Host Controller Interface Specification for
 *          Certified Wireless Universal Serial Bus, revision 0.95.
 *
 * How this works is kind of convoluted but simple. The whci.ko driver
 * loads when WHCI devices are detected. These WHCI devices expose
 * many devices in the same PCI function (they couldn't have reused
 * functions, no), so for each PCI function that exposes these many
 * devices, whci ceates a umc_dev [whci_probe() -> whci_add_cap()]
 * with umc_device_create() and adds it to the bus with
 * umc_device_register().
 *
 * umc_device_register() calls device_register() which will push the
 * bus management code to load your UMC driver's somehting_probe()
 * that you have registered for that capability code.
 *
 * Now when the WHCI device is removed, whci_remove() will go over
 * each umc_dev assigned to each of the PCI function's capabilities
 * and through whci_del_cap() call umc_device_unregister() each
 * created umc_dev. Of course, if you are bound to the device, your
 * driver's something_remove() will be called.
 */

#ifndef _LINUX_UWB_UMC_H_
#define _LINUX_UWB_UMC_H_

#include <linux/device.h>
#include <linux/pci.h>

/*
 * UMC capability IDs.
 *
 * 0x00 is reserved so use it for the radio controller device.
 *
 * [WHCI] table 2-8
 */
#define UMC_CAP_ID_WHCI_RC      0x00 /* radio controller */
#define UMC_CAP_ID_WHCI_WUSB_HC 0x01 /* WUSB host controller */

/**
 * struct umc_dev - UMC capability device
 *
 * @version:  version of the specification this capability conforms to.
 * @cap_id:   capability ID.
 * @bar:      PCI Bar (64 bit) where the resource lies
 * @resource: register space resource.
 * @irq:      interrupt line.
 */
struct umc_dev {
	u16		version;
	u8		cap_id;
	u8		bar;
	struct resource resource;
	unsigned	irq;
	struct device	dev;
};

#define to_umc_dev(d) container_of(d, struct umc_dev, dev)

/**
 * struct umc_driver - UMC capability driver
 * @cap_id: supported capability ID.
 * @match: driver specific capability matching function.
 * @match_data: driver specific data for match() (e.g., a
 * table of pci_device_id's if umc_match_pci_id() is used).
 */
struct umc_driver {
	char *name;
	u8 cap_id;
	int (*match)(struct umc_driver *, struct umc_dev *);
	const void *match_data;

	int  (*probe)(struct umc_dev *);
	void (*remove)(struct umc_dev *);
	int  (*pre_reset)(struct umc_dev *);
	int  (*post_reset)(struct umc_dev *);

	struct device_driver driver;
};

#define to_umc_driver(d) container_of(d, struct umc_driver, driver)

extern struct bus_type umc_bus_type;

struct umc_dev *umc_device_create(struct device *parent, int n);
int __must_check umc_device_register(struct umc_dev *umc);
void umc_device_unregister(struct umc_dev *umc);

int __must_check __umc_driver_register(struct umc_driver *umc_drv,
				       struct module *mod,
				       const char *mod_name);

/**
 * umc_driver_register - register a UMC capabiltity driver.
 * @umc_drv:  pointer to the driver.
 */
#define umc_driver_register(umc_drv) \
	__umc_driver_register(umc_drv, THIS_MODULE, KBUILD_MODNAME)

void umc_driver_unregister(struct umc_driver *umc_drv);

/*
 * Utility function you can use to match (umc_driver->match) against a
 * null-terminated array of 'struct pci_device_id' in
 * umc_driver->match_data.
 */
int umc_match_pci_id(struct umc_driver *umc_drv, struct umc_dev *umc);

/**
 * umc_parent_pci_dev - return the UMC's parent PCI device or NULL if none
 * @umc_dev: UMC device whose parent PCI device we are looking for
 *
 * DIRTY!!! DON'T RELY ON THIS
 *
 * FIXME: This is as dirty as it gets, but we need some way to check
 * the correct type of umc_dev->parent (so that for example, we can
 * cast to pci_dev). Casting to pci_dev is necessary because at some
 * point we need to request resources from the device. Mapping is
 * easily over come (ioremap and stuff are bus agnostic), but hooking
 * up to some error handlers (such as pci error handlers) might need
 * this.
 *
 * THIS might (probably will) be removed in the future, so don't count
 * on it.
 */
static inline struct pci_dev *umc_parent_pci_dev(struct umc_dev *umc_dev)
{
	struct pci_dev *pci_dev = NULL;
	if (dev_is_pci(umc_dev->dev.parent))
		pci_dev = to_pci_dev(umc_dev->dev.parent);
	return pci_dev;
}

/**
 * umc_dev_get() - reference a UMC device.
 * @umc_dev: Pointer to UMC device.
 *
 * NOTE: we are assuming in this whole scheme that the parent device
 *       is referenced at _probe() time and unreferenced at _remove()
 *       time by the parent's subsystem.
 */
static inline struct umc_dev *umc_dev_get(struct umc_dev *umc_dev)
{
	get_device(&umc_dev->dev);
	return umc_dev;
}

/**
 * umc_dev_put() - unreference a UMC device.
 * @umc_dev: Pointer to UMC device.
 */
static inline void umc_dev_put(struct umc_dev *umc_dev)
{
	put_device(&umc_dev->dev);
}

/**
 * umc_set_drvdata - set UMC device's driver data.
 * @umc_dev: Pointer to UMC device.
 * @data:    Data to set.
 */
static inline void umc_set_drvdata(struct umc_dev *umc_dev, void *data)
{
	dev_set_drvdata(&umc_dev->dev, data);
}

/**
 * umc_get_drvdata - recover UMC device's driver data.
 * @umc_dev: Pointer to UMC device.
 */
static inline void *umc_get_drvdata(struct umc_dev *umc_dev)
{
	return dev_get_drvdata(&umc_dev->dev);
}

int umc_controller_reset(struct umc_dev *umc);

#endif /* #ifndef _LINUX_UWB_UMC_H_ */
