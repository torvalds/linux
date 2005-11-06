/*
 *	Sky Nexus Register Driver
 *
 *	Copyright (C) 2002 Brian Waite
 *
 *	This driver allows reading the Nexus register
 *	It exports the /proc/sky_chassis_id and also
 *	/proc/sky_slot_id pseudo-file for status information.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/hdpu_features.h>
#include <linux/pci.h>

#include <linux/platform_device.h>

static int hdpu_nexus_probe(struct device *ddev);
static int hdpu_nexus_remove(struct device *ddev);

static struct proc_dir_entry *hdpu_slot_id;
static struct proc_dir_entry *hdpu_chassis_id;
static int slot_id = -1;
static int chassis_id = -1;

static struct device_driver hdpu_nexus_driver = {
	.name = HDPU_NEXUS_NAME,
	.bus = &platform_bus_type,
	.probe = hdpu_nexus_probe,
	.remove = hdpu_nexus_remove,
};

int hdpu_slot_id_read(char *buffer, char **buffer_location, off_t offset,
		      int buffer_length, int *zero, void *ptr)
{

	if (offset > 0)
		return 0;
	return sprintf(buffer, "%d\n", slot_id);
}

int hdpu_chassis_id_read(char *buffer, char **buffer_location, off_t offset,
			 int buffer_length, int *zero, void *ptr)
{

	if (offset > 0)
		return 0;
	return sprintf(buffer, "%d\n", chassis_id);
}

static int hdpu_nexus_probe(struct device *ddev)
{
	struct platform_device *pdev = to_platform_device(ddev);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int *nexus_id_addr;
	nexus_id_addr =
	    ioremap(res->start, (unsigned long)(res->end - res->start));
	if (nexus_id_addr) {
		slot_id = (*nexus_id_addr >> 8) & 0x1f;
		chassis_id = *nexus_id_addr & 0xff;
		iounmap(nexus_id_addr);
	} else
		printk("Could not map slot id\n");
	hdpu_slot_id = create_proc_entry("sky_slot_id", 0666, &proc_root);
	hdpu_slot_id->read_proc = hdpu_slot_id_read;
	hdpu_slot_id->nlink = 1;

	hdpu_chassis_id = create_proc_entry("sky_chassis_id", 0666, &proc_root);
	hdpu_chassis_id->read_proc = hdpu_chassis_id_read;
	hdpu_chassis_id->nlink = 1;
	return 0;
}

static int hdpu_nexus_remove(struct device *ddev)
{
	slot_id = -1;
	chassis_id = -1;
	remove_proc_entry("sky_slot_id", &proc_root);
	remove_proc_entry("sky_chassis_id", &proc_root);
	hdpu_slot_id = 0;
	hdpu_chassis_id = 0;
	return 0;
}

static int __init nexus_init(void)
{
	int rc;
	rc = driver_register(&hdpu_nexus_driver);
	return rc;
}

static void __exit nexus_exit(void)
{
	driver_unregister(&hdpu_nexus_driver);
}

module_init(nexus_init);
module_exit(nexus_exit);

MODULE_AUTHOR("Brian Waite");
MODULE_LICENSE("GPL");
