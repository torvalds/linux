/*
 * system.c - a driver for reserving pnp system resources
 *
 * Some code is based on pnpbios_core.c
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/pnp.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/ioport.h>

static const struct pnp_device_id pnp_dev_table[] = {
	/* General ID for reserving resources */
	{	"PNP0c02",		0	},
	/* memory controller */
	{	"PNP0c01",		0	},
	{	"",			0	}
};

static void reserve_ioport_range(char *pnpid, int start, int end)
{
	struct resource *res;
	char *regionid;

	regionid = kmalloc(16, GFP_KERNEL);
	if ( regionid == NULL )
		return;
	snprintf(regionid, 16, "pnp %s", pnpid);
	res = request_region(start,end-start+1,regionid);
	if ( res == NULL )
		kfree( regionid );
	else
		res->flags &= ~IORESOURCE_BUSY;
	/*
	 * Failures at this point are usually harmless. pci quirks for
	 * example do reserve stuff they know about too, so we may well
	 * have double reservations.
	 */
	printk(KERN_INFO
		"pnp: %s: ioport range 0x%x-0x%x %s reserved\n",
		pnpid, start, end,
		NULL != res ? "has been" : "could not be"
	);

	return;
}

static void reserve_resources_of_dev( struct pnp_dev *dev )
{
	int i;

	for (i=0;i<PNP_MAX_PORT;i++) {
		if (!pnp_port_valid(dev, i))
			/* end of resources */
			continue;
		if (pnp_port_start(dev, i) == 0)
			/* disabled */
			/* Do nothing */
			continue;
		if (pnp_port_start(dev, i) < 0x100)
			/*
			 * Below 0x100 is only standard PC hardware
			 * (pics, kbd, timer, dma, ...)
			 * We should not get resource conflicts there,
			 * and the kernel reserves these anyway
			 * (see arch/i386/kernel/setup.c).
			 * So, do nothing
			 */
			continue;
		if (pnp_port_end(dev, i) < pnp_port_start(dev, i))
			/* invalid endpoint */
			/* Do nothing */
			continue;
		reserve_ioport_range(
			dev->dev.bus_id,
			pnp_port_start(dev, i),
			pnp_port_end(dev, i)
		);
	}

	return;
}

static int system_pnp_probe(struct pnp_dev * dev, const struct pnp_device_id *dev_id)
{
	reserve_resources_of_dev(dev);
	return 0;
}

static struct pnp_driver system_pnp_driver = {
	.name		= "system",
	.id_table	= pnp_dev_table,
	.flags		= PNP_DRIVER_RES_DO_NOT_CHANGE,
	.probe		= system_pnp_probe,
	.remove		= NULL,
};

static int __init pnp_system_init(void)
{
	return pnp_register_driver(&system_pnp_driver);
}

/**
 * Reserve motherboard resources after PCI claim BARs,
 * but before PCI assign resources for uninitialized PCI devices
 */
fs_initcall(pnp_system_init);
