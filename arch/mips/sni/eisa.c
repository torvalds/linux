/*
 * Virtual EISA root driver.
 * Acts as a placeholder if we don't have a proper EISA bridge.
 *
 * (C) 2003 Marc Zyngier <maz@wild-wind.fr.eu.org>
 * modified for SNI usage by Thomas Bogendoerfer
 *
 * This code is released under the GPL version 2.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/eisa.h>
#include <linux/init.h>

/* The default EISA device parent (virtual root device).
 * Now use a platform device, since that's the obvious choice. */

static struct platform_device eisa_root_dev = {
	.name = "eisa",
	.id   = 0,
};

static struct eisa_root_device eisa_bus_root = {
	.dev	       = &eisa_root_dev.dev,
	.bus_base_addr = 0,
	.res	       = &ioport_resource,
	.slots	       = EISA_MAX_SLOTS,
	.dma_mask      = 0xffffffff,
	.force_probe   = 1,
};

int __init sni_eisa_root_init(void)
{
	int r;

	r = platform_device_register(&eisa_root_dev);
	if (!r)
		return r;

	dev_set_drvdata(&eisa_root_dev.dev, &eisa_bus_root);

	if (eisa_root_register(&eisa_bus_root)) {
		/* A real bridge may have been registered before
		 * us. So quietly unregister. */
		platform_device_unregister(&eisa_root_dev);
		return -1;
	}
	return 0;
}
