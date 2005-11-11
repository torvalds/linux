/*
 * core.c - contains all core device and protocol registration functions
 *
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/pnp.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "base.h"


static LIST_HEAD(pnp_protocols);
LIST_HEAD(pnp_global);
DEFINE_SPINLOCK(pnp_lock);

void *pnp_alloc(long size)
{
	void *result;

	result = kmalloc(size, GFP_KERNEL);
	if (!result){
		printk(KERN_ERR "pnp: Out of Memory\n");
		return NULL;
	}
	memset(result, 0, size);
	return result;
}

/**
 * pnp_protocol_register - adds a pnp protocol to the pnp layer
 * @protocol: pointer to the corresponding pnp_protocol structure
 *
 *  Ex protocols: ISAPNP, PNPBIOS, etc
 */

int pnp_register_protocol(struct pnp_protocol *protocol)
{
	int nodenum;
	struct list_head * pos;

	if (!protocol)
		return -EINVAL;

	INIT_LIST_HEAD(&protocol->devices);
	INIT_LIST_HEAD(&protocol->cards);
	nodenum = 0;
	spin_lock(&pnp_lock);

	/* assign the lowest unused number */
	list_for_each(pos,&pnp_protocols) {
		struct pnp_protocol * cur = to_pnp_protocol(pos);
		if (cur->number == nodenum){
			pos = &pnp_protocols;
			nodenum++;
		}
	}

	list_add_tail(&protocol->protocol_list, &pnp_protocols);
	spin_unlock(&pnp_lock);

	protocol->number = nodenum;
	sprintf(protocol->dev.bus_id, "pnp%d", nodenum);
	return device_register(&protocol->dev);
}

/**
 * pnp_protocol_unregister - removes a pnp protocol from the pnp layer
 * @protocol: pointer to the corresponding pnp_protocol structure
 *
 */
void pnp_unregister_protocol(struct pnp_protocol *protocol)
{
	spin_lock(&pnp_lock);
	list_del(&protocol->protocol_list);
	spin_unlock(&pnp_lock);
	device_unregister(&protocol->dev);
}


static void pnp_free_ids(struct pnp_dev *dev)
{
	struct pnp_id * id;
	struct pnp_id * next;
	if (!dev)
		return;
	id = dev->id;
	while (id) {
		next = id->next;
		kfree(id);
		id = next;
	}
}

static void pnp_release_device(struct device *dmdev)
{
	struct pnp_dev * dev = to_pnp_dev(dmdev);
	pnp_free_option(dev->independent);
	pnp_free_option(dev->dependent);
	pnp_free_ids(dev);
	kfree(dev);
}

int __pnp_add_device(struct pnp_dev *dev)
{
	int ret;
	pnp_fixup_device(dev);
	dev->dev.bus = &pnp_bus_type;
	dev->dev.release = &pnp_release_device;
	dev->status = PNP_READY;
	spin_lock(&pnp_lock);
	list_add_tail(&dev->global_list, &pnp_global);
	list_add_tail(&dev->protocol_list, &dev->protocol->devices);
	spin_unlock(&pnp_lock);

	ret = device_register(&dev->dev);
	if (ret == 0)
		pnp_interface_attach_device(dev);
	return ret;
}

/*
 * pnp_add_device - adds a pnp device to the pnp layer
 * @dev: pointer to dev to add
 *
 *  adds to driver model, name database, fixups, interface, etc.
 */

int pnp_add_device(struct pnp_dev *dev)
{
	if (!dev || !dev->protocol || dev->card)
		return -EINVAL;
	dev->dev.parent = &dev->protocol->dev;
	sprintf(dev->dev.bus_id, "%02x:%02x", dev->protocol->number, dev->number);
	return __pnp_add_device(dev);
}

void __pnp_remove_device(struct pnp_dev *dev)
{
	spin_lock(&pnp_lock);
	list_del(&dev->global_list);
	list_del(&dev->protocol_list);
	spin_unlock(&pnp_lock);
	device_unregister(&dev->dev);
}

/**
 * pnp_remove_device - removes a pnp device from the pnp layer
 * @dev: pointer to dev to add
 *
 * this function will free all mem used by dev
 */
#if 0
void pnp_remove_device(struct pnp_dev *dev)
{
	if (!dev || dev->card)
		return;
	__pnp_remove_device(dev);
}
#endif  /*  0  */

static int __init pnp_init(void)
{
	printk(KERN_INFO "Linux Plug and Play Support v0.97 (c) Adam Belay\n");
	return bus_register(&pnp_bus_type);
}

subsys_initcall(pnp_init);

#if 0
EXPORT_SYMBOL(pnp_register_protocol);
EXPORT_SYMBOL(pnp_unregister_protocol);
EXPORT_SYMBOL(pnp_add_device);
EXPORT_SYMBOL(pnp_remove_device);
#endif  /*  0  */
