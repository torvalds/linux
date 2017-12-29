/**
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Companion module to the iio simple dummy example driver.
 * The purpose of this is to generate 'fake' event interrupts thus
 * allowing that driver's code to be as close as possible to that of
 * a normal driver talking to hardware.  The approach used here
 * is not intended to be general and just happens to work for this
 * particular use case.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/sysfs.h>

#include "iio_dummy_evgen.h"
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/irq_sim.h>

/* Fiddly bit of faking and irq without hardware */
#define IIO_EVENTGEN_NO 10

/**
 * @regs: irq regs we are faking
 * @lock: protect the evgen state
 * @inuse: mask of which irqs are connected
 * @irq_sim: interrupt simulator
 * @base: base of irq range
 */
struct iio_dummy_eventgen {
	struct iio_dummy_regs regs[IIO_EVENTGEN_NO];
	struct mutex lock;
	bool inuse[IIO_EVENTGEN_NO];
	struct irq_sim irq_sim;
	int base;
};

/* We can only ever have one instance of this 'device' */
static struct iio_dummy_eventgen *iio_evgen;

static int iio_dummy_evgen_create(void)
{
	int ret;

	iio_evgen = kzalloc(sizeof(*iio_evgen), GFP_KERNEL);
	if (!iio_evgen)
		return -ENOMEM;

	ret = irq_sim_init(&iio_evgen->irq_sim, IIO_EVENTGEN_NO);
	if (ret) {
		kfree(iio_evgen);
		return ret;
	}

	iio_evgen->base = irq_sim_irqnum(&iio_evgen->irq_sim, 0);
	mutex_init(&iio_evgen->lock);

	return 0;
}

/**
 * iio_dummy_evgen_get_irq() - get an evgen provided irq for a device
 *
 * This function will give a free allocated irq to a client device.
 * That irq can then be caused to 'fire' by using the associated sysfs file.
 */
int iio_dummy_evgen_get_irq(void)
{
	int i, ret = 0;

	if (!iio_evgen)
		return -ENODEV;

	mutex_lock(&iio_evgen->lock);
	for (i = 0; i < IIO_EVENTGEN_NO; i++) {
		if (!iio_evgen->inuse[i]) {
			ret = irq_sim_irqnum(&iio_evgen->irq_sim, i);
			iio_evgen->inuse[i] = true;
			break;
		}
	}
	mutex_unlock(&iio_evgen->lock);
	if (i == IIO_EVENTGEN_NO)
		return -ENOMEM;

	return ret;
}
EXPORT_SYMBOL_GPL(iio_dummy_evgen_get_irq);

/**
 * iio_dummy_evgen_release_irq() - give the irq back.
 * @irq: irq being returned to the pool
 *
 * Used by client driver instances to give the irqs back when they disconnect
 */
void iio_dummy_evgen_release_irq(int irq)
{
	mutex_lock(&iio_evgen->lock);
	iio_evgen->inuse[irq - iio_evgen->base] = false;
	mutex_unlock(&iio_evgen->lock);
}
EXPORT_SYMBOL_GPL(iio_dummy_evgen_release_irq);

struct iio_dummy_regs *iio_dummy_evgen_get_regs(int irq)
{
	return &iio_evgen->regs[irq - iio_evgen->base];
}
EXPORT_SYMBOL_GPL(iio_dummy_evgen_get_regs);

static void iio_dummy_evgen_free(void)
{
	irq_sim_fini(&iio_evgen->irq_sim);
	kfree(iio_evgen);
}

static void iio_evgen_release(struct device *dev)
{
	iio_dummy_evgen_free();
}

static ssize_t iio_evgen_poke(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	unsigned long event;
	int ret;

	ret = kstrtoul(buf, 10, &event);
	if (ret)
		return ret;

	iio_evgen->regs[this_attr->address].reg_id   = this_attr->address;
	iio_evgen->regs[this_attr->address].reg_data = event;

	irq_sim_fire(&iio_evgen->irq_sim, this_attr->address);

	return len;
}

static IIO_DEVICE_ATTR(poke_ev0, S_IWUSR, NULL, &iio_evgen_poke, 0);
static IIO_DEVICE_ATTR(poke_ev1, S_IWUSR, NULL, &iio_evgen_poke, 1);
static IIO_DEVICE_ATTR(poke_ev2, S_IWUSR, NULL, &iio_evgen_poke, 2);
static IIO_DEVICE_ATTR(poke_ev3, S_IWUSR, NULL, &iio_evgen_poke, 3);
static IIO_DEVICE_ATTR(poke_ev4, S_IWUSR, NULL, &iio_evgen_poke, 4);
static IIO_DEVICE_ATTR(poke_ev5, S_IWUSR, NULL, &iio_evgen_poke, 5);
static IIO_DEVICE_ATTR(poke_ev6, S_IWUSR, NULL, &iio_evgen_poke, 6);
static IIO_DEVICE_ATTR(poke_ev7, S_IWUSR, NULL, &iio_evgen_poke, 7);
static IIO_DEVICE_ATTR(poke_ev8, S_IWUSR, NULL, &iio_evgen_poke, 8);
static IIO_DEVICE_ATTR(poke_ev9, S_IWUSR, NULL, &iio_evgen_poke, 9);

static struct attribute *iio_evgen_attrs[] = {
	&iio_dev_attr_poke_ev0.dev_attr.attr,
	&iio_dev_attr_poke_ev1.dev_attr.attr,
	&iio_dev_attr_poke_ev2.dev_attr.attr,
	&iio_dev_attr_poke_ev3.dev_attr.attr,
	&iio_dev_attr_poke_ev4.dev_attr.attr,
	&iio_dev_attr_poke_ev5.dev_attr.attr,
	&iio_dev_attr_poke_ev6.dev_attr.attr,
	&iio_dev_attr_poke_ev7.dev_attr.attr,
	&iio_dev_attr_poke_ev8.dev_attr.attr,
	&iio_dev_attr_poke_ev9.dev_attr.attr,
	NULL,
};

static const struct attribute_group iio_evgen_group = {
	.attrs = iio_evgen_attrs,
};

static const struct attribute_group *iio_evgen_groups[] = {
	&iio_evgen_group,
	NULL
};

static struct device iio_evgen_dev = {
	.bus = &iio_bus_type,
	.groups = iio_evgen_groups,
	.release = &iio_evgen_release,
};

static __init int iio_dummy_evgen_init(void)
{
	int ret = iio_dummy_evgen_create();

	if (ret < 0)
		return ret;
	device_initialize(&iio_evgen_dev);
	dev_set_name(&iio_evgen_dev, "iio_evgen");
	return device_add(&iio_evgen_dev);
}
module_init(iio_dummy_evgen_init);

static __exit void iio_dummy_evgen_exit(void)
{
	device_unregister(&iio_evgen_dev);
}
module_exit(iio_dummy_evgen_exit);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("IIO dummy driver");
MODULE_LICENSE("GPL v2");
