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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/hdpu_features.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <asm/io.h>

static int hdpu_nexus_probe(struct platform_device *pdev);
static int hdpu_nexus_remove(struct platform_device *pdev);
static int hdpu_slot_id_open(struct inode *inode, struct file *file);
static int hdpu_slot_id_read(struct seq_file *seq, void *offset);
static int hdpu_chassis_id_open(struct inode *inode, struct file *file);
static int hdpu_chassis_id_read(struct seq_file *seq, void *offset);

static struct proc_dir_entry *hdpu_slot_id;
static struct proc_dir_entry *hdpu_chassis_id;
static int slot_id = -1;
static int chassis_id = -1;

static const struct file_operations proc_slot_id = {
	.open = hdpu_slot_id_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct file_operations proc_chassis_id = {
	.open = hdpu_chassis_id_open,
	.read = seq_read,
	.llseek	= seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct platform_driver hdpu_nexus_driver = {
	.probe = hdpu_nexus_probe,
	.remove = hdpu_nexus_remove,
	.driver = {
		.name = HDPU_NEXUS_NAME,
		.owner = THIS_MODULE,
	},
};

static int hdpu_slot_id_open(struct inode *inode, struct file *file)
{
	return single_open(file, hdpu_slot_id_read, NULL);
}

static int hdpu_slot_id_read(struct seq_file *seq, void *offset)
{
	seq_printf(seq, "%d\n", slot_id);
	return 0;
}

static int hdpu_chassis_id_open(struct inode *inode, struct file *file)
{
	return single_open(file, hdpu_chassis_id_read, NULL);
}

static int hdpu_chassis_id_read(struct seq_file *seq, void *offset)
{
	seq_printf(seq, "%d\n", chassis_id);
	return 0;
}

static int hdpu_nexus_probe(struct platform_device *pdev)
{
	struct resource *res;
	int *nexus_id_addr;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		printk(KERN_ERR "sky_nexus: "
		       "Invalid memory resource.\n");
		return -EINVAL;
	}
	nexus_id_addr = ioremap(res->start,
				(unsigned long)(res->end - res->start));
	if (nexus_id_addr) {
		slot_id = (*nexus_id_addr >> 8) & 0x1f;
		chassis_id = *nexus_id_addr & 0xff;
		iounmap(nexus_id_addr);
	} else {
		printk(KERN_ERR "sky_nexus: Could not map slot id\n");
	}

	hdpu_slot_id = proc_create("sky_slot_id", 0666, NULL, &proc_slot_id);
	if (!hdpu_slot_id) {
		printk(KERN_WARNING "sky_nexus: "
		       "Unable to create proc dir entry: sky_slot_id\n");
	}

	hdpu_chassis_id = proc_create("sky_chassis_id", 0666, NULL,
				      &proc_chassis_id);
	if (!hdpu_chassis_id)
		printk(KERN_WARNING "sky_nexus: "
		       "Unable to create proc dir entry: sky_chassis_id\n");
	}

	return 0;
}

static int hdpu_nexus_remove(struct platform_device *pdev)
{
	slot_id = -1;
	chassis_id = -1;

	remove_proc_entry("sky_slot_id", NULL);
	remove_proc_entry("sky_chassis_id", NULL);

	hdpu_slot_id = 0;
	hdpu_chassis_id = 0;

	return 0;
}

static int __init nexus_init(void)
{
	return platform_driver_register(&hdpu_nexus_driver);
}

static void __exit nexus_exit(void)
{
	platform_driver_unregister(&hdpu_nexus_driver);
}

module_init(nexus_init);
module_exit(nexus_exit);

MODULE_AUTHOR("Brian Waite");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" HDPU_NEXUS_NAME);
