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
#include <linux/device.h>
#include <linux/mutex.h>
#include <asm/system.h>
#include <asm/irq.h>

#define IN_CARD_SERVICES
#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include "cs_internal.h"

#define to_socket(_dev) container_of(_dev, struct pcmcia_socket, dev)

static ssize_t pccard_show_type(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct pcmcia_socket *s = to_socket(dev);

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;
	if (s->state & SOCKET_CARDBUS)
		return sprintf(buf, "32-bit\n");
	return sprintf(buf, "16-bit\n");
}
static DEVICE_ATTR(card_type, 0444, pccard_show_type, NULL);

static ssize_t pccard_show_voltage(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct pcmcia_socket *s = to_socket(dev);

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;
	if (s->socket.Vcc)
		return sprintf(buf, "%d.%dV\n", s->socket.Vcc / 10,
			       s->socket.Vcc % 10);
	return sprintf(buf, "X.XV\n");
}
static DEVICE_ATTR(card_voltage, 0444, pccard_show_voltage, NULL);

static ssize_t pccard_show_vpp(struct device *dev, struct device_attribute *attr,
			       char *buf)
{
	struct pcmcia_socket *s = to_socket(dev);
	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;
	return sprintf(buf, "%d.%dV\n", s->socket.Vpp / 10, s->socket.Vpp % 10);
}
static DEVICE_ATTR(card_vpp, 0444, pccard_show_vpp, NULL);

static ssize_t pccard_show_vcc(struct device *dev, struct device_attribute *attr,
			       char *buf)
{
	struct pcmcia_socket *s = to_socket(dev);
	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;
	return sprintf(buf, "%d.%dV\n", s->socket.Vcc / 10, s->socket.Vcc % 10);
}
static DEVICE_ATTR(card_vcc, 0444, pccard_show_vcc, NULL);


static ssize_t pccard_store_insert(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	ssize_t ret;
	struct pcmcia_socket *s = to_socket(dev);

	if (!count)
		return -EINVAL;

	ret = pcmcia_insert_card(s);

	return ret ? ret : count;
}
static DEVICE_ATTR(card_insert, 0200, NULL, pccard_store_insert);


static ssize_t pccard_show_card_pm_state(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct pcmcia_socket *s = to_socket(dev);
	return sprintf(buf, "%s\n", s->state & SOCKET_SUSPEND ? "off" : "on");
}

static ssize_t pccard_store_card_pm_state(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	ssize_t ret = -EINVAL;
	struct pcmcia_socket *s = to_socket(dev);

	if (!count)
		return -EINVAL;

	if (!(s->state & SOCKET_SUSPEND) && !strncmp(buf, "off", 3))
		ret = pcmcia_suspend_card(s);
	else if ((s->state & SOCKET_SUSPEND) && !strncmp(buf, "on", 2))
		ret = pcmcia_resume_card(s);

	return ret ? -ENODEV : count;
}
static DEVICE_ATTR(card_pm_state, 0644, pccard_show_card_pm_state, pccard_store_card_pm_state);

static ssize_t pccard_store_eject(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	ssize_t ret;
	struct pcmcia_socket *s = to_socket(dev);

	if (!count)
		return -EINVAL;

	ret = pcmcia_eject_card(s);

	return ret ? ret : count;
}
static DEVICE_ATTR(card_eject, 0200, NULL, pccard_store_eject);


static ssize_t pccard_show_irq_mask(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct pcmcia_socket *s = to_socket(dev);
	return sprintf(buf, "0x%04x\n", s->irq_mask);
}

static ssize_t pccard_store_irq_mask(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
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
static DEVICE_ATTR(card_irq_mask, 0600, pccard_show_irq_mask, pccard_store_irq_mask);


static ssize_t pccard_show_resource(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct pcmcia_socket *s = to_socket(dev);
	return sprintf(buf, "%s\n", s->resource_setup_done ? "yes" : "no");
}

static ssize_t pccard_store_resource(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned long flags;
	struct pcmcia_socket *s = to_socket(dev);

	if (!count)
		return -EINVAL;

	spin_lock_irqsave(&s->lock, flags);
	if (!s->resource_setup_done)
		s->resource_setup_done = 1;
	spin_unlock_irqrestore(&s->lock, flags);

	mutex_lock(&s->skt_mutex);
	if ((s->callback) &&
	    (s->state & SOCKET_PRESENT) &&
	    !(s->state & SOCKET_CARDBUS)) {
		if (try_module_get(s->callback->owner)) {
			s->callback->requery(s, 0);
			module_put(s->callback->owner);
		}
	}
	mutex_unlock(&s->skt_mutex);

	return count;
}
static DEVICE_ATTR(available_resources_setup_done, 0600, pccard_show_resource, pccard_store_resource);


static ssize_t pccard_extract_cis(struct pcmcia_socket *s, char *buf, loff_t off, size_t count)
{
	tuple_t tuple;
	int status, i;
	loff_t pointer = 0;
	ssize_t ret = 0;
	u_char *tuplebuffer;
	u_char *tempbuffer;

	tuplebuffer = kmalloc(sizeof(u_char) * 256, GFP_KERNEL);
	if (!tuplebuffer)
		return -ENOMEM;

	tempbuffer = kmalloc(sizeof(u_char) * 258, GFP_KERNEL);
	if (!tempbuffer) {
		ret = -ENOMEM;
		goto free_tuple;
	}

	memset(&tuple, 0, sizeof(tuple_t));

	tuple.Attributes = TUPLE_RETURN_LINK | TUPLE_RETURN_COMMON;
	tuple.DesiredTuple = RETURN_FIRST_TUPLE;
	tuple.TupleOffset = 0;

	status = pccard_get_first_tuple(s, BIND_FN_ALL, &tuple);
	while (!status) {
		tuple.TupleData = tuplebuffer;
		tuple.TupleDataMax = 255;
		memset(tuplebuffer, 0, sizeof(u_char) * 255);

		status = pccard_get_tuple_data(s, &tuple);
		if (status)
			break;

		if (off < (pointer + 2 + tuple.TupleDataLen)) {
			tempbuffer[0] = tuple.TupleCode & 0xff;
			tempbuffer[1] = tuple.TupleLink & 0xff;
			for (i = 0; i < tuple.TupleDataLen; i++)
				tempbuffer[i + 2] = tuplebuffer[i] & 0xff;

			for (i = 0; i < (2 + tuple.TupleDataLen); i++) {
				if (((i + pointer) >= off) &&
				    (i + pointer) < (off + count)) {
					buf[ret] = tempbuffer[i];
					ret++;
				}
			}
		}

		pointer += 2 + tuple.TupleDataLen;

		if (pointer >= (off + count))
			break;

		if (tuple.TupleCode == CISTPL_END)
			break;
		status = pccard_get_next_tuple(s, BIND_FN_ALL, &tuple);
	}

	kfree(tempbuffer);
 free_tuple:
	kfree(tuplebuffer);

	return (ret);
}

static ssize_t pccard_show_cis(struct kobject *kobj,
			       struct bin_attribute *bin_attr,
			       char *buf, loff_t off, size_t count)
{
	unsigned int size = 0x200;

	if (off >= size)
		count = 0;
	else {
		struct pcmcia_socket *s;
		cisinfo_t cisinfo;

		if (off + count > size)
			count = size - off;

		s = to_socket(container_of(kobj, struct device, kobj));

		if (!(s->state & SOCKET_PRESENT))
			return -ENODEV;
		if (pccard_validate_cis(s, BIND_FN_ALL, &cisinfo))
			return -EIO;
		if (!cisinfo.Chains)
			return -ENODATA;

		count = pccard_extract_cis(s, buf, off, count);
	}

	return (count);
}

static ssize_t pccard_store_cis(struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct pcmcia_socket *s = to_socket(container_of(kobj, struct device, kobj));
	cisdump_t *cis;
	int error;

	if (off)
		return -EINVAL;

	if (count >= 0x200)
		return -EINVAL;

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;

	cis = kzalloc(sizeof(cisdump_t), GFP_KERNEL);
	if (!cis)
		return -ENOMEM;

	cis->Length = count + 1;
	memcpy(cis->Data, buf, count);

	error = pcmcia_replace_cis(s, cis);
	kfree(cis);
	if (error)
		return -EIO;

	mutex_lock(&s->skt_mutex);
	if ((s->callback) && (s->state & SOCKET_PRESENT) &&
	    !(s->state & SOCKET_CARDBUS)) {
		if (try_module_get(s->callback->owner)) {
			s->callback->requery(s, 1);
			module_put(s->callback->owner);
		}
	}
	mutex_unlock(&s->skt_mutex);

	return count;
}


static struct device_attribute *pccard_socket_attributes[] = {
	&dev_attr_card_type,
	&dev_attr_card_voltage,
	&dev_attr_card_vpp,
	&dev_attr_card_vcc,
	&dev_attr_card_insert,
	&dev_attr_card_pm_state,
	&dev_attr_card_eject,
	&dev_attr_card_irq_mask,
	&dev_attr_available_resources_setup_done,
	NULL,
};

static struct bin_attribute pccard_cis_attr = {
	.attr = { .name = "cis", .mode = S_IRUGO | S_IWUSR },
	.size = 0x200,
	.read = pccard_show_cis,
	.write = pccard_store_cis,
};

static int __devinit pccard_sysfs_add_socket(struct device *dev,
					     struct class_interface *class_intf)
{
	struct device_attribute **attr;
	int ret = 0;

	for (attr = pccard_socket_attributes; *attr; attr++) {
		ret = device_create_file(dev, *attr);
		if (ret)
			break;
	}
	if (!ret)
		ret = sysfs_create_bin_file(&dev->kobj, &pccard_cis_attr);

	return ret;
}

static void __devexit pccard_sysfs_remove_socket(struct device *dev,
						 struct class_interface *class_intf)
{
	struct device_attribute **attr;

	sysfs_remove_bin_file(&dev->kobj, &pccard_cis_attr);
	for (attr = pccard_socket_attributes; *attr; attr++)
		device_remove_file(dev, *attr);
}

struct class_interface pccard_sysfs_interface = {
	.class = &pcmcia_socket_class,
	.add_dev = &pccard_sysfs_add_socket,
	.remove_dev = __devexit_p(&pccard_sysfs_remove_socket),
};
