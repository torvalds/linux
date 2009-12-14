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

/* bit array to represent IR sysfs device number */
static unsigned long ir_core_dev_number;

/* class for /sys/class/irrcv */
static struct class *ir_input_class;

/**
 * show_protocol() - shows the current IR protocol
 * @d:		the device descriptor
 * @mattr:	the device attribute struct (unused)
 * @buf:	a pointer to the output buffer
 *
 * This routine is a callback routine for input read the IR protocol type.
 * it is trigged by reading /sys/class/irrcv/irrcv?/current_protocol.
 * It returns the protocol name, as understood by the driver.
 */
static ssize_t show_protocol(struct device *d,
			     struct device_attribute *mattr, char *buf)
{
	char *s;
	struct ir_input_dev *ir_dev = dev_get_drvdata(d);
	enum ir_type ir_type = ir_dev->rc_tab.ir_type;

	IR_dprintk(1, "Current protocol is %ld\n", ir_type);

	/* FIXME: doesn't support multiple protocols at the same time */
	if (ir_type == IR_TYPE_UNKNOWN)
		s = "Unknown";
	else if (ir_type == IR_TYPE_RC5)
		s = "RC-5";
	else if (ir_type == IR_TYPE_PD)
		s = "Pulse/distance";
	else if (ir_type == IR_TYPE_NEC)
		s = "NEC";
	else
		s = "Other";

	return sprintf(buf, "%s\n", s);
}

/**
 * store_protocol() - shows the current IR protocol
 * @d:		the device descriptor
 * @mattr:	the device attribute struct (unused)
 * @buf:	a pointer to the input buffer
 * @len:	length of the input buffer
 *
 * This routine is a callback routine for changing the IR protocol type.
 * it is trigged by reading /sys/class/irrcv/irrcv?/current_protocol.
 * It changes the IR the protocol name, if the IR type is recognized
 * by the driver.
 * If an unknown protocol name is used, returns -EINVAL.
 */
static ssize_t store_protocol(struct device *d,
			      struct device_attribute *mattr,
			      const char *data,
			      size_t len)
{
	struct ir_input_dev *ir_dev = dev_get_drvdata(d);
	enum ir_type ir_type = IR_TYPE_UNKNOWN;
	int rc = -EINVAL;
	unsigned long flags;
	char *buf;

	buf = strsep((char **) &data, "\n");

	if (!strcasecmp(buf, "rc-5"))
		ir_type = IR_TYPE_RC5;
	else if (!strcasecmp(buf, "pd"))
		ir_type = IR_TYPE_PD;
	else if (!strcasecmp(buf, "nec"))
		ir_type = IR_TYPE_NEC;

	if (ir_type == IR_TYPE_UNKNOWN) {
		IR_dprintk(1, "Error setting protocol to %ld\n", ir_type);
		return -EINVAL;
	}

	if (ir_dev->props->change_protocol)
		rc = ir_dev->props->change_protocol(ir_dev->props->priv,
						    ir_type);

	if (rc < 0) {
		IR_dprintk(1, "Error setting protocol to %ld\n", ir_type);
		return -EINVAL;
	}

	spin_lock_irqsave(&ir_dev->rc_tab.lock, flags);
	ir_dev->rc_tab.ir_type = ir_type;
	spin_unlock_irqrestore(&ir_dev->rc_tab.lock, flags);

	IR_dprintk(1, "Current protocol is %ld\n", ir_type);

	return len;
}

/*
 * Static device attribute struct with the sysfs attributes for IR's
 */
static DEVICE_ATTR(current_protocol, S_IRUGO | S_IWUSR,
		   show_protocol, store_protocol);

static struct attribute *ir_dev_attrs[] = {
	&dev_attr_current_protocol.attr,
};

/**
 * ir_register_class() - creates the sysfs for /sys/class/irrcv/irrcv?
 * @input_dev:	the struct input_dev descriptor of the device
 *
 * This routine is used to register the syfs code for IR class
 */
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

/**
 * ir_unregister_class() - removes the sysfs for sysfs for
 *			   /sys/class/irrcv/irrcv?
 * @input_dev:	the struct input_dev descriptor of the device
 *
 * This routine is used to unregister the syfs code for IR class
 */
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

/*
 * Init/exit code for the module. Basically, creates/removes /sys/class/irrcv
 */

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
