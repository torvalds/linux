/*
 * core.c - contains all core device and protocol registration functions
 *
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
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
#include <linux/dma-mapping.h>

#include "base.h"

static LIST_HEAD(pnp_protocols);
LIST_HEAD(pnp_global);
DEFINE_SPINLOCK(pnp_lock);

/*
 * ACPI or PNPBIOS should tell us about all platform devices, so we can
 * skip some blind probes.  ISAPNP typically enumerates only plug-in ISA
 * devices, not built-in things like COM ports.
 */
int pnp_platform_devices;
EXPORT_SYMBOL(pnp_platform_devices);

void *pnp_alloc(long size)
{
	void *result;

	result = kzalloc(size, GFP_KERNEL);
	if (!result) {
		printk(KERN_ERR "pnp: Out of Memory\n");
		return NULL;
	}
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
	struct list_head *pos;

	INIT_LIST_HEAD(&protocol->devices);
	INIT_LIST_HEAD(&protocol->cards);
	nodenum = 0;
	spin_lock(&pnp_lock);

	/* assign the lowest unused number */
	list_for_each(pos, &pnp_protocols) {
		struct pnp_protocol *cur = to_pnp_protocol(pos);
		if (cur->number == nodenum) {
			pos = &pnp_protocols;
			nodenum++;
		}
	}

	list_add_tail(&protocol->protocol_list, &pnp_protocols);
	spin_unlock(&pnp_lock);

	protocol->number = nodenum;
	dev_set_name(&protocol->dev, "pnp%d", nodenum);
	return device_register(&protocol->dev);
}

/**
 * pnp_protocol_unregister - removes a pnp protocol from the pnp layer
 * @protocol: pointer to the corresponding pnp_protocol structure
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
	struct pnp_id *id;
	struct pnp_id *next;

	id = dev->id;
	while (id) {
		next = id->next;
		kfree(id);
		id = next;
	}
}

void pnp_free_resource(struct pnp_resource *pnp_res)
{
	list_del(&pnp_res->list);
	kfree(pnp_res);
}

void pnp_free_resources(struct pnp_dev *dev)
{
	struct pnp_resource *pnp_res, *tmp;

	list_for_each_entry_safe(pnp_res, tmp, &dev->resources, list) {
		pnp_free_resource(pnp_res);
	}
}

static void pnp_release_device(struct device *dmdev)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);

	pnp_free_ids(dev);
	pnp_free_resources(dev);
	pnp_free_options(dev);
	kfree(dev);
}

struct pnp_dev *pnp_alloc_dev(struct pnp_protocol *protocol, int id,
			      const char *pnpid)
{
	struct pnp_dev *dev;
	struct pnp_id *dev_id;

	dev = kzalloc(sizeof(struct pnp_dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	INIT_LIST_HEAD(&dev->resources);
	INIT_LIST_HEAD(&dev->options);
	dev->protocol = protocol;
	dev->number = id;
	dev->dma_mask = DMA_BIT_MASK(24);

	dev->dev.parent = &dev->protocol->dev;
	dev->dev.bus = &pnp_bus_type;
	dev->dev.dma_mask = &dev->dma_mask;
	dev->dev.coherent_dma_mask = dev->dma_mask;
	dev->dev.release = &pnp_release_device;

	dev_set_name(&dev->dev, "%02x:%02x", dev->protocol->number, dev->number);

	dev_id = pnp_add_id(dev, pnpid);
	if (!dev_id) {
		kfree(dev);
		return NULL;
	}

	return dev;
}

int __pnp_add_device(struct pnp_dev *dev)
{
	pnp_fixup_device(dev);
	dev->status = PNP_READY;
	spin_lock(&pnp_lock);
	list_add_tail(&dev->global_list, &pnp_global);
	list_add_tail(&dev->protocol_list, &dev->protocol->devices);
	spin_unlock(&pnp_lock);
	if (dev->protocol->can_wakeup)
		device_set_wakeup_capable(&dev->dev,
				dev->protocol->can_wakeup(dev));
	return device_register(&dev->dev);
}

/*
 * pnp_add_device - adds a pnp device to the pnp layer
 * @dev: pointer to dev to add
 *
 *  adds to driver model, name database, fixups, interface, etc.
 */
int pnp_add_device(struct pnp_dev *dev)
{
	int ret;
	char buf[128];
	int len = 0;
	struct pnp_id *id;

	if (dev->card)
		return -EINVAL;

	ret = __pnp_add_device(dev);
	if (ret)
		return ret;

	buf[0] = '\0';
	for (id = dev->id; id; id = id->next)
		len += scnprintf(buf + len, sizeof(buf) - len, " %s", id->id);

	pnp_dbg(&dev->dev, "%s device, IDs%s (%s)\n",
		dev->protocol->name, buf, dev->active ? "active" : "disabled");
	return 0;
}

void __pnp_remove_device(struct pnp_dev *dev)
{
	spin_lock(&pnp_lock);
	list_del(&dev->global_list);
	list_del(&dev->protocol_list);
	spin_unlock(&pnp_lock);
	device_unregister(&dev->dev);
}

static int __init pnp_init(void)
{
	return bus_register(&pnp_bus_type);
}

subsys_initcall(pnp_init);

int pnp_debug;

#if defined(CONFIG_PNP_DEBUG_MESSAGES)
static int __init pnp_debug_setup(char *__unused)
{
	pnp_debug = 1;
	return 1;
}
__setup("pnp.debug", pnp_debug_setup);
#endif
