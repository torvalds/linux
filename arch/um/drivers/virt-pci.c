// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Intel Corporation
 * Author: Johannes Berg <johannes@sipsolutions.net>
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/logic_iomem.h>
#include <linux/of_platform.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/unaligned.h>
#include <irq_kern.h>

#include "virt-pci.h"

#define MAX_DEVICES 8
#define MAX_MSI_VECTORS 32
#define CFG_SPACE_SIZE 4096

struct um_pci_device_reg {
	struct um_pci_device *dev;
	void __iomem *iomem;
};

static struct pci_host_bridge *bridge;
static DEFINE_MUTEX(um_pci_mtx);
static struct um_pci_device *um_pci_platform_device;
static struct um_pci_device_reg um_pci_devices[MAX_DEVICES];
static struct fwnode_handle *um_pci_fwnode;
static struct irq_domain *um_pci_inner_domain;
static struct irq_domain *um_pci_msi_domain;
static unsigned long um_pci_msi_used[BITS_TO_LONGS(MAX_MSI_VECTORS)];

static unsigned long um_pci_cfgspace_read(void *priv, unsigned int offset,
					  int size)
{
	struct um_pci_device_reg *reg = priv;
	struct um_pci_device *dev = reg->dev;

	if (!dev)
		return ULONG_MAX;

	switch (size) {
	case 1:
	case 2:
	case 4:
#ifdef CONFIG_64BIT
	case 8:
#endif
		break;
	default:
		WARN(1, "invalid config space read size %d\n", size);
		return ULONG_MAX;
	}

	return dev->ops->cfgspace_read(dev, offset, size);
}

static void um_pci_cfgspace_write(void *priv, unsigned int offset, int size,
				  unsigned long val)
{
	struct um_pci_device_reg *reg = priv;
	struct um_pci_device *dev = reg->dev;

	if (!dev)
		return;

	switch (size) {
	case 1:
	case 2:
	case 4:
#ifdef CONFIG_64BIT
	case 8:
#endif
		break;
	default:
		WARN(1, "invalid config space write size %d\n", size);
		return;
	}

	dev->ops->cfgspace_write(dev, offset, size, val);
}

static const struct logic_iomem_ops um_pci_device_cfgspace_ops = {
	.read = um_pci_cfgspace_read,
	.write = um_pci_cfgspace_write,
};

static unsigned long um_pci_bar_read(void *priv, unsigned int offset,
				     int size)
{
	u8 *resptr = priv;
	struct um_pci_device *dev = container_of(resptr - *resptr,
						 struct um_pci_device,
						 resptr[0]);
	u8 bar = *resptr;

	switch (size) {
	case 1:
	case 2:
	case 4:
#ifdef CONFIG_64BIT
	case 8:
#endif
		break;
	default:
		WARN(1, "invalid bar read size %d\n", size);
		return ULONG_MAX;
	}

	return dev->ops->bar_read(dev, bar, offset, size);
}

static void um_pci_bar_write(void *priv, unsigned int offset, int size,
			     unsigned long val)
{
	u8 *resptr = priv;
	struct um_pci_device *dev = container_of(resptr - *resptr,
						 struct um_pci_device,
						 resptr[0]);
	u8 bar = *resptr;

	switch (size) {
	case 1:
	case 2:
	case 4:
#ifdef CONFIG_64BIT
	case 8:
#endif
		break;
	default:
		WARN(1, "invalid bar write size %d\n", size);
		return;
	}

	dev->ops->bar_write(dev, bar, offset, size, val);
}

static void um_pci_bar_copy_from(void *priv, void *buffer,
				 unsigned int offset, int size)
{
	u8 *resptr = priv;
	struct um_pci_device *dev = container_of(resptr - *resptr,
						 struct um_pci_device,
						 resptr[0]);
	u8 bar = *resptr;

	dev->ops->bar_copy_from(dev, bar, buffer, offset, size);
}

static void um_pci_bar_copy_to(void *priv, unsigned int offset,
			       const void *buffer, int size)
{
	u8 *resptr = priv;
	struct um_pci_device *dev = container_of(resptr - *resptr,
						 struct um_pci_device,
						 resptr[0]);
	u8 bar = *resptr;

	dev->ops->bar_copy_to(dev, bar, offset, buffer, size);
}

static void um_pci_bar_set(void *priv, unsigned int offset, u8 value, int size)
{
	u8 *resptr = priv;
	struct um_pci_device *dev = container_of(resptr - *resptr,
						 struct um_pci_device,
						 resptr[0]);
	u8 bar = *resptr;

	dev->ops->bar_set(dev, bar, offset, value, size);
}

static const struct logic_iomem_ops um_pci_device_bar_ops = {
	.read = um_pci_bar_read,
	.write = um_pci_bar_write,
	.set = um_pci_bar_set,
	.copy_from = um_pci_bar_copy_from,
	.copy_to = um_pci_bar_copy_to,
};

static void __iomem *um_pci_map_bus(struct pci_bus *bus, unsigned int devfn,
				    int where)
{
	struct um_pci_device_reg *dev;
	unsigned int busn = bus->number;

	if (busn > 0)
		return NULL;

	/* not allowing functions for now ... */
	if (devfn % 8)
		return NULL;

	if (devfn / 8 >= ARRAY_SIZE(um_pci_devices))
		return NULL;

	dev = &um_pci_devices[devfn / 8];
	if (!dev)
		return NULL;

	return (void __iomem *)((unsigned long)dev->iomem + where);
}

static struct pci_ops um_pci_ops = {
	.map_bus = um_pci_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static void um_pci_rescan(void)
{
	pci_lock_rescan_remove();
	pci_rescan_bus(bridge->bus);
	pci_unlock_rescan_remove();
}

#ifdef CONFIG_OF
/* Copied from arch/x86/kernel/devicetree.c */
struct device_node *pcibios_get_phb_of_node(struct pci_bus *bus)
{
	struct device_node *np;

	for_each_node_by_type(np, "pci") {
		const void *prop;
		unsigned int bus_min;

		prop = of_get_property(np, "bus-range", NULL);
		if (!prop)
			continue;
		bus_min = be32_to_cpup(prop);
		if (bus->number == bus_min)
			return np;
	}
	return NULL;
}
#endif

static struct resource virt_cfgspace_resource = {
	.name = "PCI config space",
	.start = 0xf0000000 - MAX_DEVICES * CFG_SPACE_SIZE,
	.end = 0xf0000000 - 1,
	.flags = IORESOURCE_MEM,
};

static long um_pci_map_cfgspace(unsigned long offset, size_t size,
				const struct logic_iomem_ops **ops,
				void **priv)
{
	if (WARN_ON(size > CFG_SPACE_SIZE || offset % CFG_SPACE_SIZE))
		return -EINVAL;

	if (offset / CFG_SPACE_SIZE < MAX_DEVICES) {
		*ops = &um_pci_device_cfgspace_ops;
		*priv = &um_pci_devices[offset / CFG_SPACE_SIZE];
		return 0;
	}

	WARN(1, "cannot map offset 0x%lx/0x%zx\n", offset, size);
	return -ENOENT;
}

static const struct logic_iomem_region_ops um_pci_cfgspace_ops = {
	.map = um_pci_map_cfgspace,
};

static struct resource virt_iomem_resource = {
	.name = "PCI iomem",
	.start = 0xf0000000,
	.end = 0xffffffff,
	.flags = IORESOURCE_MEM,
};

struct um_pci_map_iomem_data {
	unsigned long offset;
	size_t size;
	const struct logic_iomem_ops **ops;
	void **priv;
	long ret;
};

static int um_pci_map_iomem_walk(struct pci_dev *pdev, void *_data)
{
	struct um_pci_map_iomem_data *data = _data;
	struct um_pci_device_reg *reg = &um_pci_devices[pdev->devfn / 8];
	struct um_pci_device *dev;
	int i;

	if (!reg->dev)
		return 0;

	for (i = 0; i < ARRAY_SIZE(dev->resptr); i++) {
		struct resource *r = &pdev->resource[i];

		if ((r->flags & IORESOURCE_TYPE_BITS) != IORESOURCE_MEM)
			continue;

		/*
		 * must be the whole or part of the resource,
		 * not allowed to only overlap
		 */
		if (data->offset < r->start || data->offset > r->end)
			continue;
		if (data->offset + data->size - 1 > r->end)
			continue;

		dev = reg->dev;
		*data->ops = &um_pci_device_bar_ops;
		dev->resptr[i] = i;
		*data->priv = &dev->resptr[i];
		data->ret = data->offset - r->start;

		/* no need to continue */
		return 1;
	}

	return 0;
}

static long um_pci_map_iomem(unsigned long offset, size_t size,
			     const struct logic_iomem_ops **ops,
			     void **priv)
{
	struct um_pci_map_iomem_data data = {
		/* we want the full address here */
		.offset = offset + virt_iomem_resource.start,
		.size = size,
		.ops = ops,
		.priv = priv,
		.ret = -ENOENT,
	};

	pci_walk_bus(bridge->bus, um_pci_map_iomem_walk, &data);
	return data.ret;
}

static const struct logic_iomem_region_ops um_pci_iomem_ops = {
	.map = um_pci_map_iomem,
};

static void um_pci_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	/*
	 * This is a very low address and not actually valid 'physical' memory
	 * in UML, so we can simply map MSI(-X) vectors to there, it cannot be
	 * legitimately written to by the device in any other way.
	 * We use the (virtual) IRQ number here as the message to simplify the
	 * code that receives the message, where for now we simply trust the
	 * device to send the correct message.
	 */
	msg->address_hi = 0;
	msg->address_lo = 0xa0000;
	msg->data = data->irq;
}

static struct irq_chip um_pci_msi_bottom_irq_chip = {
	.name = "UM virtual MSI",
	.irq_compose_msi_msg = um_pci_compose_msi_msg,
};

static int um_pci_inner_domain_alloc(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs,
				     void *args)
{
	unsigned long bit;

	WARN_ON(nr_irqs != 1);

	mutex_lock(&um_pci_mtx);
	bit = find_first_zero_bit(um_pci_msi_used, MAX_MSI_VECTORS);
	if (bit >= MAX_MSI_VECTORS) {
		mutex_unlock(&um_pci_mtx);
		return -ENOSPC;
	}

	set_bit(bit, um_pci_msi_used);
	mutex_unlock(&um_pci_mtx);

	irq_domain_set_info(domain, virq, bit, &um_pci_msi_bottom_irq_chip,
			    domain->host_data, handle_simple_irq,
			    NULL, NULL);

	return 0;
}

static void um_pci_inner_domain_free(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);

	mutex_lock(&um_pci_mtx);

	if (!test_bit(d->hwirq, um_pci_msi_used))
		pr_err("trying to free unused MSI#%lu\n", d->hwirq);
	else
		__clear_bit(d->hwirq, um_pci_msi_used);

	mutex_unlock(&um_pci_mtx);
}

static const struct irq_domain_ops um_pci_inner_domain_ops = {
	.alloc = um_pci_inner_domain_alloc,
	.free = um_pci_inner_domain_free,
};

static struct irq_chip um_pci_msi_irq_chip = {
	.name = "UM virtual PCIe MSI",
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static struct msi_domain_info um_pci_msi_domain_info = {
	.flags	= MSI_FLAG_USE_DEF_DOM_OPS |
		  MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_PCI_MSIX,
	.chip	= &um_pci_msi_irq_chip,
};

static struct resource busn_resource = {
	.name	= "PCI busn",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUS,
};

static int um_pci_map_irq(const struct pci_dev *pdev, u8 slot, u8 pin)
{
	struct um_pci_device_reg *reg = &um_pci_devices[pdev->devfn / 8];

	if (WARN_ON(!reg->dev))
		return -EINVAL;

	/* Yes, we map all pins to the same IRQ ... doesn't matter for now. */
	return reg->dev->irq;
}

void *pci_root_bus_fwnode(struct pci_bus *bus)
{
	return um_pci_fwnode;
}

static long um_pci_map_platform(unsigned long offset, size_t size,
				const struct logic_iomem_ops **ops,
				void **priv)
{
	if (!um_pci_platform_device)
		return -ENOENT;

	*ops = &um_pci_device_bar_ops;
	*priv = &um_pci_platform_device->resptr[0];

	return offset;
}

static const struct logic_iomem_region_ops um_pci_platform_ops = {
	.map = um_pci_map_platform,
};

static struct resource virt_platform_resource = {
	.name = "platform",
	.start = 0x10000000,
	.end = 0x1fffffff,
	.flags = IORESOURCE_MEM,
};

int um_pci_device_register(struct um_pci_device *dev)
{
	int i, free = -1;
	int err = 0;

	mutex_lock(&um_pci_mtx);
	for (i = 0; i < MAX_DEVICES; i++) {
		if (um_pci_devices[i].dev)
			continue;
		free = i;
		break;
	}

	if (free < 0) {
		err = -ENOSPC;
		goto out;
	}

	dev->irq = irq_alloc_desc(numa_node_id());
	if (dev->irq < 0) {
		err = dev->irq;
		goto out;
	}

	um_pci_devices[free].dev = dev;

out:
	mutex_unlock(&um_pci_mtx);
	if (!err)
		um_pci_rescan();
	return err;
}

void um_pci_device_unregister(struct um_pci_device *dev)
{
	int i;

	mutex_lock(&um_pci_mtx);
	for (i = 0; i < MAX_DEVICES; i++) {
		if (um_pci_devices[i].dev != dev)
			continue;
		um_pci_devices[i].dev = NULL;
		irq_free_desc(dev->irq);
		break;
	}
	mutex_unlock(&um_pci_mtx);

	if (i < MAX_DEVICES) {
		struct pci_dev *pci_dev;

		pci_dev = pci_get_slot(bridge->bus, i);
		if (pci_dev)
			pci_stop_and_remove_bus_device_locked(pci_dev);
	}
}

int um_pci_platform_device_register(struct um_pci_device *dev)
{
	guard(mutex)(&um_pci_mtx);
	if (um_pci_platform_device)
		return -EBUSY;
	um_pci_platform_device = dev;
	return 0;
}

void um_pci_platform_device_unregister(struct um_pci_device *dev)
{
	guard(mutex)(&um_pci_mtx);
	if (um_pci_platform_device == dev)
		um_pci_platform_device = NULL;
}

static int __init um_pci_init(void)
{
	struct irq_domain_info inner_domain_info = {
		.size		= MAX_MSI_VECTORS,
		.hwirq_max	= MAX_MSI_VECTORS,
		.ops		= &um_pci_inner_domain_ops,
	};
	int err, i;

	WARN_ON(logic_iomem_add_region(&virt_cfgspace_resource,
				       &um_pci_cfgspace_ops));
	WARN_ON(logic_iomem_add_region(&virt_iomem_resource,
				       &um_pci_iomem_ops));
	WARN_ON(logic_iomem_add_region(&virt_platform_resource,
				       &um_pci_platform_ops));

	bridge = pci_alloc_host_bridge(0);
	if (!bridge) {
		err = -ENOMEM;
		goto free;
	}

	um_pci_fwnode = irq_domain_alloc_named_fwnode("um-pci");
	if (!um_pci_fwnode) {
		err = -ENOMEM;
		goto free;
	}

	inner_domain_info.fwnode = um_pci_fwnode;
	um_pci_inner_domain = irq_domain_instantiate(&inner_domain_info);
	if (IS_ERR(um_pci_inner_domain)) {
		err = PTR_ERR(um_pci_inner_domain);
		goto free;
	}

	um_pci_msi_domain = pci_msi_create_irq_domain(um_pci_fwnode,
						      &um_pci_msi_domain_info,
						      um_pci_inner_domain);
	if (!um_pci_msi_domain) {
		err = -ENOMEM;
		goto free;
	}

	pci_add_resource(&bridge->windows, &virt_iomem_resource);
	pci_add_resource(&bridge->windows, &busn_resource);
	bridge->ops = &um_pci_ops;
	bridge->map_irq = um_pci_map_irq;

	for (i = 0; i < MAX_DEVICES; i++) {
		resource_size_t start;

		start = virt_cfgspace_resource.start + i * CFG_SPACE_SIZE;
		um_pci_devices[i].iomem = ioremap(start, CFG_SPACE_SIZE);
		if (WARN(!um_pci_devices[i].iomem, "failed to map %d\n", i)) {
			err = -ENOMEM;
			goto free;
		}
	}

	err = pci_host_probe(bridge);
	if (err)
		goto free;

	return 0;

free:
	if (!IS_ERR_OR_NULL(um_pci_inner_domain))
		irq_domain_remove(um_pci_inner_domain);
	if (um_pci_fwnode)
		irq_domain_free_fwnode(um_pci_fwnode);
	if (bridge) {
		pci_free_resource_list(&bridge->windows);
		pci_free_host_bridge(bridge);
	}
	return err;
}
device_initcall(um_pci_init);

static void __exit um_pci_exit(void)
{
	irq_domain_remove(um_pci_msi_domain);
	irq_domain_remove(um_pci_inner_domain);
	pci_free_resource_list(&bridge->windows);
	pci_free_host_bridge(bridge);
}
module_exit(um_pci_exit);
