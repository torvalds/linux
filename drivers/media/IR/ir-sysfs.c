/* ir-register.c - handle IR scancode->keycode tables
 *
 * Copyright (C) 2009 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/input.h>
#include <linux/device.h>
#include <media/ir-core.h>

#define IRRCV_NUM_DEVICES	256

unsigned long ir_core_dev_number;

static struct class *ir_input_class;

static DEVICE_ATTR(ir_protocol, S_IRUGO | S_IWUSR, NULL, NULL);

static struct attribute *ir_dev_attrs[] = {
	&dev_attr_ir_protocol.attr,
};

int ir_register_class(struct input_dev *input_dev)
{
	int rc;
	struct kobject *kobj;

	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	int devno = find_first_zero_bit(&ir_core_dev_number,
					IRRCV_NUM_DEVICES);

	if (unlikely(devno < 0))
		return devno;

	ir_dev->attr.attrs = ir_dev_attrs;
	ir_dev->class_dev = device_create(ir_input_class, NULL,
					  input_dev->dev.devt, ir_dev,
					  "irrcv%d", devno);
	kobj = &ir_dev->class_dev->kobj;

	printk(KERN_WARNING "Creating IR device %s\n", kobject_name(kobj));
	rc = sysfs_create_group(kobj, &ir_dev->attr);
	if (unlikely(rc < 0)) {
		device_destroy(ir_input_class, input_dev->dev.devt);
		return -ENOMEM;
	}

	ir_dev->devno = devno;
	set_bit(devno, &ir_core_dev_number);

	return 0;
};

void ir_unregister_class(struct input_dev *input_dev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	struct kobject *kobj;

	clear_bit(ir_dev->devno, &ir_core_dev_number);

	kobj = &ir_dev->class_dev->kobj;

	sysfs_remove_group(kobj, &ir_dev->attr);
	device_destroy(ir_input_class, input_dev->dev.devt);

	kfree(ir_dev->attr.name);
}

static int __init ir_core_init(void)
{
	ir_input_class = class_create(THIS_MODULE, "irrcv");
	if (IS_ERR(ir_input_class)) {
		printk(KERN_ERR "ir_core: unable to register irrcv class\n");
		return PTR_ERR(ir_input_class);
	}

	return 0;
}

static void __exit ir_core_exit(void)
{
	class_destroy(ir_input_class);
}

module_init(ir_core_init);
module_exit(ir_core_exit);
