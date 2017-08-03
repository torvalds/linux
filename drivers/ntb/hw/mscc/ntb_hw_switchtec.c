/*
 * Microsemi Switchtec(tm) PCIe Management Driver
 * Copyright (c) 2017, Microsemi Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/switchtec.h>
#include <linux/module.h>

MODULE_DESCRIPTION("Microsemi Switchtec(tm) NTB Driver");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Microsemi Corporation");

struct switchtec_ntb {
	struct switchtec_dev *stdev;
};

static int switchtec_ntb_add(struct device *dev,
			     struct class_interface *class_intf)
{
	struct switchtec_dev *stdev = to_stdev(dev);
	struct switchtec_ntb *sndev;

	stdev->sndev = NULL;

	if (stdev->pdev->class != MICROSEMI_NTB_CLASSCODE)
		return -ENODEV;

	sndev = kzalloc_node(sizeof(*sndev), GFP_KERNEL, dev_to_node(dev));
	if (!sndev)
		return -ENOMEM;

	sndev->stdev = stdev;

	stdev->sndev = sndev;
	dev_info(dev, "NTB device registered");

	return 0;
}

void switchtec_ntb_remove(struct device *dev,
			  struct class_interface *class_intf)
{
	struct switchtec_dev *stdev = to_stdev(dev);
	struct switchtec_ntb *sndev = stdev->sndev;

	if (!sndev)
		return;

	stdev->sndev = NULL;
	kfree(sndev);
	dev_info(dev, "ntb device unregistered");
}

static struct class_interface switchtec_interface  = {
	.add_dev = switchtec_ntb_add,
	.remove_dev = switchtec_ntb_remove,
};

static int __init switchtec_ntb_init(void)
{
	switchtec_interface.class = switchtec_class;
	return class_interface_register(&switchtec_interface);
}
module_init(switchtec_ntb_init);

static void __exit switchtec_ntb_exit(void)
{
	class_interface_unregister(&switchtec_interface);
}
module_exit(switchtec_ntb_exit);
