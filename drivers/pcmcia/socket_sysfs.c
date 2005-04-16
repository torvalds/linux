/*
 * socket_sysfs.c -- most of socket-related sysfs output
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * (C) 2003 - 2004		Dominik Brodowski
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <asm/system.h>
#include <asm/irq.h>

#define IN_CARD_SERVICES
#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include "cs_internal.h"

#define to_socket(_dev) container_of(_dev, struct pcmcia_socket, dev)

static ssize_t pccard_show_type(struct class_device *dev, char *buf)
{
	int val;
	struct pcmcia_socket *s = to_socket(dev);

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;
	s->ops->get_status(s, &val);
	if (val & SS_CARDBUS)
		return sprintf(buf, "32-bit\n");
	if (val & SS_DETECT)
		return sprintf(buf, "16-bit\n");
	return sprintf(buf, "invalid\n");
}
static CLASS_DEVICE_ATTR(card_type, 0400, pccard_show_type, NULL);

static ssize_t pccard_show_voltage(struct class_device *dev, char *buf)
{
	int val;
	struct pcmcia_socket *s = to_socket(dev);

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;
	s->ops->get_status(s, &val);
	if (val & SS_3VCARD)
		return sprintf(buf, "3.3V\n");
	if (val & SS_XVCARD)
		return sprintf(buf, "X.XV\n");
	return sprintf(buf, "5.0V\n");
}
static CLASS_DEVICE_ATTR(card_voltage, 0400, pccard_show_voltage, NULL);

static ssize_t pccard_show_vpp(struct class_device *dev, char *buf)
{
	struct pcmcia_socket *s = to_socket(dev);
	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;
	return sprintf(buf, "%d.%dV\n", s->socket.Vpp / 10, s->socket.Vpp % 10);
}
static CLASS_DEVICE_ATTR(card_vpp, 0400, pccard_show_vpp, NULL);

static ssize_t pccard_show_vcc(struct class_device *dev, char *buf)
{
	struct pcmcia_socket *s = to_socket(dev);
	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;
	return sprintf(buf, "%d.%dV\n", s->socket.Vcc / 10, s->socket.Vcc % 10);
}
static CLASS_DEVICE_ATTR(card_vcc, 0400, pccard_show_vcc, NULL);


static ssize_t pccard_store_insert(struct class_device *dev, const char *buf, size_t count)
{
	ssize_t ret;
	struct pcmcia_socket *s = to_socket(dev);

	if (!count)
		return -EINVAL;

	ret = pcmcia_insert_card(s);

	return ret ? ret : count;
}
static CLASS_DEVICE_ATTR(card_insert, 0200, NULL, pccard_store_insert);

static ssize_t pccard_store_eject(struct class_device *dev, const char *buf, size_t count)
{
	ssize_t ret;
	struct pcmcia_socket *s = to_socket(dev);

	if (!count)
		return -EINVAL;

	ret = pcmcia_eject_card(s);

	return ret ? ret : count;
}
static CLASS_DEVICE_ATTR(card_eject, 0200, NULL, pccard_store_eject);


static ssize_t pccard_show_irq_mask(struct class_device *dev, char *buf)
{
	struct pcmcia_socket *s = to_socket(dev);
	return sprintf(buf, "0x%04x\n", s->irq_mask);
}

static ssize_t pccard_store_irq_mask(struct class_device *dev, const char *buf, size_t count)
{
	ssize_t ret;
	struct pcmcia_socket *s = to_socket(dev);
	u32 mask;

	if (!count)
		return -EINVAL;

	ret = sscanf (buf, "0x%x\n", &mask);

	if (ret == 1) {
		s->irq_mask &= mask;
		ret = 0;
	}

	return ret ? ret : count;
}
static CLASS_DEVICE_ATTR(card_irq_mask, 0600, pccard_show_irq_mask, pccard_store_irq_mask);


static ssize_t pccard_show_resource(struct class_device *dev, char *buf)
{
	struct pcmcia_socket *s = to_socket(dev);
	return sprintf(buf, "%s\n", s->resource_setup_done ? "yes" : "no");
}

static ssize_t pccard_store_resource(struct class_device *dev, const char *buf, size_t count)
{
	unsigned long flags;
	struct pcmcia_socket *s = to_socket(dev);

	if (!count)
		return -EINVAL;

	spin_lock_irqsave(&s->lock, flags);
	if (!s->resource_setup_done) {
		s->resource_setup_done = 1;
		spin_unlock_irqrestore(&s->lock, flags);

		down(&s->skt_sem);
		if ((s->callback) &&
		    (s->state & SOCKET_PRESENT) &&
		    !(s->state & SOCKET_CARDBUS)) {
			if (try_module_get(s->callback->owner)) {
				s->callback->resources_done(s);
				module_put(s->callback->owner);
			}
		}
		up(&s->skt_sem);

		return count;
	}
	spin_unlock_irqrestore(&s->lock, flags);

	return count;
}
static CLASS_DEVICE_ATTR(available_resources_setup_done, 0600, pccard_show_resource, pccard_store_resource);


static struct class_device_attribute *pccard_socket_attributes[] = {
	&class_device_attr_card_type,
	&class_device_attr_card_voltage,
	&class_device_attr_card_vpp,
	&class_device_attr_card_vcc,
	&class_device_attr_card_insert,
	&class_device_attr_card_eject,
	&class_device_attr_card_irq_mask,
	&class_device_attr_available_resources_setup_done,
	NULL,
};

static int __devinit pccard_sysfs_add_socket(struct class_device *class_dev)
{
	struct class_device_attribute **attr;
	int ret = 0;

	for (attr = pccard_socket_attributes; *attr; attr++) {
		ret = class_device_create_file(class_dev, *attr);
		if (ret)
			break;
	}

	return ret;
}

static void __devexit pccard_sysfs_remove_socket(struct class_device *class_dev)
{
	struct class_device_attribute **attr;

	for (attr = pccard_socket_attributes; *attr; attr++)
		class_device_remove_file(class_dev, *attr);
}

struct class_interface pccard_sysfs_interface = {
	.class = &pcmcia_socket_class,
	.add = &pccard_sysfs_add_socket,
	.remove = __devexit_p(&pccard_sysfs_remove_socket),
};
