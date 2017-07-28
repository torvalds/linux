/*
 *  dell-smo8800.c - Dell Latitude ACPI SMO88XX freefall sensor driver
 *
 *  Copyright (C) 2012 Sonal Santan <sonal.santan@gmail.com>
 *  Copyright (C) 2014 Pali Rohár <pali.rohar@gmail.com>
 *
 *  This is loosely based on lis3lv02d driver.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#define DRIVER_NAME "smo8800"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

struct smo8800_device {
	u32 irq;                     /* acpi device irq */
	atomic_t counter;            /* count after last read */
	struct miscdevice miscdev;   /* for /dev/freefall */
	unsigned long misc_opened;   /* whether the device is open */
	wait_queue_head_t misc_wait; /* Wait queue for the misc dev */
	struct device *dev;          /* acpi device */
};

static irqreturn_t smo8800_interrupt_quick(int irq, void *data)
{
	struct smo8800_device *smo8800 = data;

	atomic_inc(&smo8800->counter);
	wake_up_interruptible(&smo8800->misc_wait);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t smo8800_interrupt_thread(int irq, void *data)
{
	struct smo8800_device *smo8800 = data;

	dev_info(smo8800->dev, "detected free fall\n");
	return IRQ_HANDLED;
}

static acpi_status smo8800_get_resource(struct acpi_resource *resource,
					void *context)
{
	struct acpi_resource_extended_irq *irq;

	if (resource->type != ACPI_RESOURCE_TYPE_EXTENDED_IRQ)
		return AE_OK;

	irq = &resource->data.extended_irq;
	if (!irq || !irq->interrupt_count)
		return AE_OK;

	*((u32 *)context) = irq->interrupts[0];
	return AE_CTRL_TERMINATE;
}

static u32 smo8800_get_irq(struct acpi_device *device)
{
	u32 irq = 0;
	acpi_status status;

	status = acpi_walk_resources(device->handle, METHOD_NAME__CRS,
				     smo8800_get_resource, &irq);
	if (ACPI_FAILURE(status)) {
		dev_err(&device->dev, "acpi_walk_resources failed\n");
		return 0;
	}

	return irq;
}

static ssize_t smo8800_misc_read(struct file *file, char __user *buf,
				 size_t count, loff_t *pos)
{
	struct smo8800_device *smo8800 = container_of(file->private_data,
					 struct smo8800_device, miscdev);

	u32 data = 0;
	unsigned char byte_data = 0;
	ssize_t retval = 1;

	if (count < 1)
		return -EINVAL;

	atomic_set(&smo8800->counter, 0);
	retval = wait_event_interruptible(smo8800->misc_wait,
				(data = atomic_xchg(&smo8800->counter, 0)));

	if (retval)
		return retval;

	byte_data = 1;
	retval = 1;

	if (data < 255)
		byte_data = data;
	else
		byte_data = 255;

	if (put_user(byte_data, buf))
		retval = -EFAULT;

	return retval;
}

static int smo8800_misc_open(struct inode *inode, struct file *file)
{
	struct smo8800_device *smo8800 = container_of(file->private_data,
					 struct smo8800_device, miscdev);

	if (test_and_set_bit(0, &smo8800->misc_opened))
		return -EBUSY; /* already open */

	atomic_set(&smo8800->counter, 0);
	return 0;
}

static int smo8800_misc_release(struct inode *inode, struct file *file)
{
	struct smo8800_device *smo8800 = container_of(file->private_data,
					 struct smo8800_device, miscdev);

	clear_bit(0, &smo8800->misc_opened); /* release the device */
	return 0;
}

static const struct file_operations smo8800_misc_fops = {
	.owner = THIS_MODULE,
	.read = smo8800_misc_read,
	.open = smo8800_misc_open,
	.release = smo8800_misc_release,
};

static int smo8800_add(struct acpi_device *device)
{
	int err;
	struct smo8800_device *smo8800;

	smo8800 = devm_kzalloc(&device->dev, sizeof(*smo8800), GFP_KERNEL);
	if (!smo8800) {
		dev_err(&device->dev, "failed to allocate device data\n");
		return -ENOMEM;
	}

	smo8800->dev = &device->dev;
	smo8800->miscdev.minor = MISC_DYNAMIC_MINOR;
	smo8800->miscdev.name = "freefall";
	smo8800->miscdev.fops = &smo8800_misc_fops;

	init_waitqueue_head(&smo8800->misc_wait);

	err = misc_register(&smo8800->miscdev);
	if (err) {
		dev_err(&device->dev, "failed to register misc dev: %d\n", err);
		return err;
	}

	device->driver_data = smo8800;

	smo8800->irq = smo8800_get_irq(device);
	if (!smo8800->irq) {
		dev_err(&device->dev, "failed to obtain IRQ\n");
		err = -EINVAL;
		goto error;
	}

	err = request_threaded_irq(smo8800->irq, smo8800_interrupt_quick,
				   smo8800_interrupt_thread,
				   IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				   DRIVER_NAME, smo8800);
	if (err) {
		dev_err(&device->dev,
			"failed to request thread for IRQ %d: %d\n",
			smo8800->irq, err);
		goto error;
	}

	dev_dbg(&device->dev, "device /dev/freefall registered with IRQ %d\n",
		 smo8800->irq);
	return 0;

error:
	misc_deregister(&smo8800->miscdev);
	return err;
}

static int smo8800_remove(struct acpi_device *device)
{
	struct smo8800_device *smo8800 = device->driver_data;

	free_irq(smo8800->irq, smo8800);
	misc_deregister(&smo8800->miscdev);
	dev_dbg(&device->dev, "device /dev/freefall unregistered\n");
	return 0;
}

static const struct acpi_device_id smo8800_ids[] = {
	{ "SMO8800", 0 },
	{ "SMO8801", 0 },
	{ "SMO8810", 0 },
	{ "SMO8811", 0 },
	{ "SMO8820", 0 },
	{ "SMO8821", 0 },
	{ "SMO8830", 0 },
	{ "SMO8831", 0 },
	{ "", 0 },
};

MODULE_DEVICE_TABLE(acpi, smo8800_ids);

static struct acpi_driver smo8800_driver = {
	.name = DRIVER_NAME,
	.class = "Latitude",
	.ids = smo8800_ids,
	.ops = {
		.add = smo8800_add,
		.remove = smo8800_remove,
	},
	.owner = THIS_MODULE,
};

module_acpi_driver(smo8800_driver);

MODULE_DESCRIPTION("Dell Latitude freefall driver (ACPI SMO88XX)");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sonal Santan, Pali Rohár");
