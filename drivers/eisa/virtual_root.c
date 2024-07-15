// SPDX-License-Identifier: GPL-2.0-only
/*
 * Virtual EISA root driver.
 * Acts as a placeholder if we don't have a proper EISA bridge.
 *
 * (C) 2003 Marc Zyngier <maz@wild-wind.fr.eu.org>
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/eisa.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#if defined(CONFIG_EISA_VLB_PRIMING)
#define EISA_FORCE_PROBE_DEFAULT 1
#else
#define EISA_FORCE_PROBE_DEFAULT 0
#endif

static int force_probe = EISA_FORCE_PROBE_DEFAULT;
static void virtual_eisa_release (struct device *);

/* The default EISA device parent (virtual root device).
 * Now use a platform device, since that's the obvious choice. */

static struct platform_device eisa_root_dev = {
	.name = "eisa",
	.id   = 0,
	.dev  = {
		.release = virtual_eisa_release,
	},
};

static struct eisa_root_device eisa_bus_root = {
	.dev		= &eisa_root_dev.dev,
	.bus_base_addr	= 0,
	.res		= &ioport_resource,
	.slots		= EISA_MAX_SLOTS,
	.dma_mask	= 0xffffffff,
};

static void virtual_eisa_release (struct device *dev)
{
	/* nothing really to do here */
}

static int __init virtual_eisa_root_init (void)
{
	int r;

	if ((r = platform_device_register (&eisa_root_dev)))
		return r;

	eisa_bus_root.force_probe = force_probe;

	dev_set_drvdata(&eisa_root_dev.dev, &eisa_bus_root);

	if (eisa_root_register (&eisa_bus_root)) {
		/* A real bridge may have been registered before
		 * us. So quietly unregister. */
		platform_device_unregister (&eisa_root_dev);
		return -1;
	}

	return 0;
}

module_param (force_probe, int, 0444);

device_initcall (virtual_eisa_root_init);
