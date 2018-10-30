// SPDX-License-Identifier: GPL-2.0+
/*
 * cpcihp_generic.c
 *
 * Generic port I/O CompactPCI driver
 *
 * Copyright 2002 SOMA Networks, Inc.
 * Copyright 2001 Intel San Luis Obispo
 * Copyright 2000,2001 MontaVista Software Inc.
 *
 * This generic CompactPCI hotplug driver should allow using the PCI hotplug
 * mechanism on any CompactPCI board that exposes the #ENUM signal as a bit
 * in a system register that can be read through standard port I/O.
 *
 * Send feedback to <scottm@somanetworks.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/string.h>
#include "cpci_hotplug.h"

#define DRIVER_VERSION	"0.1"
#define DRIVER_AUTHOR	"Scott Murray <scottm@somanetworks.com>"
#define DRIVER_DESC	"Generic port I/O CompactPCI Hot Plug Driver"

#if !defined(MODULE)
#define MY_NAME	"cpcihp_generic"
#else
#define MY_NAME	THIS_MODULE->name
#endif

#define dbg(format, arg...)					\
	do {							\
		if (debug)					\
			printk(KERN_DEBUG "%s: " format "\n",	\
				MY_NAME, ## arg);		\
	} while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format "\n", MY_NAME, ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format "\n", MY_NAME, ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format "\n", MY_NAME, ## arg)

/* local variables */
static bool debug;
static char *bridge;
static u8 bridge_busnr;
static u8 bridge_slot;
static struct pci_bus *bus;
static u8 first_slot;
static u8 last_slot;
static u16 port;
static unsigned int enum_bit;
static u8 enum_mask;

static struct cpci_hp_controller_ops generic_hpc_ops;
static struct cpci_hp_controller generic_hpc;

static int __init validate_parameters(void)
{
	char *str;
	char *p;
	unsigned long tmp;

	if (!bridge) {
		info("not configured, disabling.");
		return -EINVAL;
	}
	str = bridge;
	if (!*str)
		return -EINVAL;

	tmp = simple_strtoul(str, &p, 16);
	if (p == str || tmp > 0xff) {
		err("Invalid hotplug bus bridge device bus number");
		return -EINVAL;
	}
	bridge_busnr = (u8) tmp;
	dbg("bridge_busnr = 0x%02x", bridge_busnr);
	if (*p != ':') {
		err("Invalid hotplug bus bridge device");
		return -EINVAL;
	}
	str = p + 1;
	tmp = simple_strtoul(str, &p, 16);
	if (p == str || tmp > 0x1f) {
		err("Invalid hotplug bus bridge device slot number");
		return -EINVAL;
	}
	bridge_slot = (u8) tmp;
	dbg("bridge_slot = 0x%02x", bridge_slot);

	dbg("first_slot = 0x%02x", first_slot);
	dbg("last_slot = 0x%02x", last_slot);
	if (!(first_slot && last_slot)) {
		err("Need to specify first_slot and last_slot");
		return -EINVAL;
	}
	if (last_slot < first_slot) {
		err("first_slot must be less than last_slot");
		return -EINVAL;
	}

	dbg("port = 0x%04x", port);
	dbg("enum_bit = 0x%02x", enum_bit);
	if (enum_bit > 7) {
		err("Invalid #ENUM bit");
		return -EINVAL;
	}
	enum_mask = 1 << enum_bit;
	return 0;
}

static int query_enum(void)
{
	u8 value;

	value = inb_p(port);
	return ((value & enum_mask) == enum_mask);
}

static int __init cpcihp_generic_init(void)
{
	int status;
	struct resource *r;
	struct pci_dev *dev;

	info(DRIVER_DESC " version: " DRIVER_VERSION);
	status = validate_parameters();
	if (status)
		return status;

	r = request_region(port, 1, "#ENUM hotswap signal register");
	if (!r)
		return -EBUSY;

	dev = pci_get_domain_bus_and_slot(0, bridge_busnr,
					  PCI_DEVFN(bridge_slot, 0));
	if (!dev || dev->hdr_type != PCI_HEADER_TYPE_BRIDGE) {
		err("Invalid bridge device %s", bridge);
		pci_dev_put(dev);
		return -EINVAL;
	}
	bus = dev->subordinate;
	pci_dev_put(dev);

	memset(&generic_hpc, 0, sizeof(struct cpci_hp_controller));
	generic_hpc_ops.query_enum = query_enum;
	generic_hpc.ops = &generic_hpc_ops;

	status = cpci_hp_register_controller(&generic_hpc);
	if (status != 0) {
		err("Could not register cPCI hotplug controller");
		return -ENODEV;
	}
	dbg("registered controller");

	status = cpci_hp_register_bus(bus, first_slot, last_slot);
	if (status != 0) {
		err("Could not register cPCI hotplug bus");
		goto init_bus_register_error;
	}
	dbg("registered bus");

	status = cpci_hp_start();
	if (status != 0) {
		err("Could not started cPCI hotplug system");
		goto init_start_error;
	}
	dbg("started cpci hp system");
	return 0;
init_start_error:
	cpci_hp_unregister_bus(bus);
init_bus_register_error:
	cpci_hp_unregister_controller(&generic_hpc);
	err("status = %d", status);
	return status;

}

static void __exit cpcihp_generic_exit(void)
{
	cpci_hp_stop();
	cpci_hp_unregister_bus(bus);
	cpci_hp_unregister_controller(&generic_hpc);
	release_region(port, 1);
}

module_init(cpcihp_generic_init);
module_exit(cpcihp_generic_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");
module_param(bridge, charp, 0);
MODULE_PARM_DESC(bridge, "Hotswap bus bridge device, <bus>:<slot> (bus and slot are in hexadecimal)");
module_param(first_slot, byte, 0);
MODULE_PARM_DESC(first_slot, "Hotswap bus first slot number");
module_param(last_slot, byte, 0);
MODULE_PARM_DESC(last_slot, "Hotswap bus last slot number");
module_param_hw(port, ushort, ioport, 0);
MODULE_PARM_DESC(port, "#ENUM signal I/O port");
module_param(enum_bit, uint, 0);
MODULE_PARM_DESC(enum_bit, "#ENUM signal bit (0-7)");
