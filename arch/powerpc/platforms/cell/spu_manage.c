/*
 * spu management operations for of based platforms
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 * Copyright 2006 Sony Corp.
 * (C) Copyright 2007 TOSHIBA CORPORATION
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/device.h>

#include <asm/spu.h>
#include <asm/spu_priv1.h>
#include <asm/firmware.h>
#include <asm/prom.h>

#include "interrupt.h"

struct device_node *spu_devnode(struct spu *spu)
{
	return spu->devnode;
}

EXPORT_SYMBOL_GPL(spu_devnode);

static u64 __init find_spu_unit_number(struct device_node *spe)
{
	const unsigned int *prop;
	int proplen;
	prop = of_get_property(spe, "unit-id", &proplen);
	if (proplen == 4)
		return (u64)*prop;

	prop = of_get_property(spe, "reg", &proplen);
	if (proplen == 4)
		return (u64)*prop;

	return 0;
}

static void spu_unmap(struct spu *spu)
{
	if (!firmware_has_feature(FW_FEATURE_LPAR))
		iounmap(spu->priv1);
	iounmap(spu->priv2);
	iounmap(spu->problem);
	iounmap((__force u8 __iomem *)spu->local_store);
}

static int __init spu_map_interrupts_old(struct spu *spu,
	struct device_node *np)
{
	unsigned int isrc;
	const u32 *tmp;
	int nid;

	/* Get the interrupt source unit from the device-tree */
	tmp = of_get_property(np, "isrc", NULL);
	if (!tmp)
		return -ENODEV;
	isrc = tmp[0];

	tmp = of_get_property(np->parent->parent, "node-id", NULL);
	if (!tmp) {
		printk(KERN_WARNING "%s: can't find node-id\n", __FUNCTION__);
		nid = spu->node;
	} else
		nid = tmp[0];

	/* Add the node number */
	isrc |= nid << IIC_IRQ_NODE_SHIFT;

	/* Now map interrupts of all 3 classes */
	spu->irqs[0] = irq_create_mapping(NULL, IIC_IRQ_CLASS_0 | isrc);
	spu->irqs[1] = irq_create_mapping(NULL, IIC_IRQ_CLASS_1 | isrc);
	spu->irqs[2] = irq_create_mapping(NULL, IIC_IRQ_CLASS_2 | isrc);

	/* Right now, we only fail if class 2 failed */
	return spu->irqs[2] == NO_IRQ ? -EINVAL : 0;
}

static void __iomem * __init spu_map_prop_old(struct spu *spu,
					      struct device_node *n,
					      const char *name)
{
	const struct address_prop {
		unsigned long address;
		unsigned int len;
	} __attribute__((packed)) *prop;
	int proplen;

	prop = of_get_property(n, name, &proplen);
	if (prop == NULL || proplen != sizeof (struct address_prop))
		return NULL;

	return ioremap(prop->address, prop->len);
}

static int __init spu_map_device_old(struct spu *spu)
{
	struct device_node *node = spu->devnode;
	const char *prop;
	int ret;

	ret = -ENODEV;
	spu->name = of_get_property(node, "name", NULL);
	if (!spu->name)
		goto out;

	prop = of_get_property(node, "local-store", NULL);
	if (!prop)
		goto out;
	spu->local_store_phys = *(unsigned long *)prop;

	/* we use local store as ram, not io memory */
	spu->local_store = (void __force *)
		spu_map_prop_old(spu, node, "local-store");
	if (!spu->local_store)
		goto out;

	prop = of_get_property(node, "problem", NULL);
	if (!prop)
		goto out_unmap;
	spu->problem_phys = *(unsigned long *)prop;

	spu->problem = spu_map_prop_old(spu, node, "problem");
	if (!spu->problem)
		goto out_unmap;

	spu->priv2 = spu_map_prop_old(spu, node, "priv2");
	if (!spu->priv2)
		goto out_unmap;

	if (!firmware_has_feature(FW_FEATURE_LPAR)) {
		spu->priv1 = spu_map_prop_old(spu, node, "priv1");
		if (!spu->priv1)
			goto out_unmap;
	}

	ret = 0;
	goto out;

out_unmap:
	spu_unmap(spu);
out:
	return ret;
}

static int __init spu_map_interrupts(struct spu *spu, struct device_node *np)
{
	struct of_irq oirq;
	int ret;
	int i;

	for (i=0; i < 3; i++) {
		ret = of_irq_map_one(np, i, &oirq);
		if (ret) {
			pr_debug("spu_new: failed to get irq %d\n", i);
			goto err;
		}
		ret = -EINVAL;
		pr_debug("  irq %d no 0x%x on %s\n", i, oirq.specifier[0],
			 oirq.controller->full_name);
		spu->irqs[i] = irq_create_of_mapping(oirq.controller,
					oirq.specifier, oirq.size);
		if (spu->irqs[i] == NO_IRQ) {
			pr_debug("spu_new: failed to map it !\n");
			goto err;
		}
	}
	return 0;

err:
	pr_debug("failed to map irq %x for spu %s\n", *oirq.specifier,
		spu->name);
	for (; i >= 0; i--) {
		if (spu->irqs[i] != NO_IRQ)
			irq_dispose_mapping(spu->irqs[i]);
	}
	return ret;
}

static int spu_map_resource(struct spu *spu, int nr,
			    void __iomem** virt, unsigned long *phys)
{
	struct device_node *np = spu->devnode;
	struct resource resource = { };
	unsigned long len;
	int ret;

	ret = of_address_to_resource(np, nr, &resource);
	if (ret)
		return ret;
	if (phys)
		*phys = resource.start;
	len = resource.end - resource.start + 1;
	*virt = ioremap(resource.start, len);
	if (!*virt)
		return -EINVAL;
	return 0;
}

static int __init spu_map_device(struct spu *spu)
{
	struct device_node *np = spu->devnode;
	int ret = -ENODEV;

	spu->name = of_get_property(np, "name", NULL);
	if (!spu->name)
		goto out;

	ret = spu_map_resource(spu, 0, (void __iomem**)&spu->local_store,
			       &spu->local_store_phys);
	if (ret) {
		pr_debug("spu_new: failed to map %s resource 0\n",
			 np->full_name);
		goto out;
	}
	ret = spu_map_resource(spu, 1, (void __iomem**)&spu->problem,
			       &spu->problem_phys);
	if (ret) {
		pr_debug("spu_new: failed to map %s resource 1\n",
			 np->full_name);
		goto out_unmap;
	}
	ret = spu_map_resource(spu, 2, (void __iomem**)&spu->priv2, NULL);
	if (ret) {
		pr_debug("spu_new: failed to map %s resource 2\n",
			 np->full_name);
		goto out_unmap;
	}
	if (!firmware_has_feature(FW_FEATURE_LPAR))
		ret = spu_map_resource(spu, 3,
			       (void __iomem**)&spu->priv1, NULL);
	if (ret) {
		pr_debug("spu_new: failed to map %s resource 3\n",
			 np->full_name);
		goto out_unmap;
	}
	pr_debug("spu_new: %s maps:\n", np->full_name);
	pr_debug("  local store   : 0x%016lx -> 0x%p\n",
		 spu->local_store_phys, spu->local_store);
	pr_debug("  problem state : 0x%016lx -> 0x%p\n",
		 spu->problem_phys, spu->problem);
	pr_debug("  priv2         :                       0x%p\n", spu->priv2);
	pr_debug("  priv1         :                       0x%p\n", spu->priv1);

	return 0;

out_unmap:
	spu_unmap(spu);
out:
	pr_debug("failed to map spe %s: %d\n", spu->name, ret);
	return ret;
}

static int __init of_enumerate_spus(int (*fn)(void *data))
{
	int ret;
	struct device_node *node;

	ret = -ENODEV;
	for (node = of_find_node_by_type(NULL, "spe");
			node; node = of_find_node_by_type(node, "spe")) {
		ret = fn(node);
		if (ret) {
			printk(KERN_WARNING "%s: Error initializing %s\n",
				__FUNCTION__, node->name);
			break;
		}
	}
	return ret;
}

static int __init of_create_spu(struct spu *spu, void *data)
{
	int ret;
	struct device_node *spe = (struct device_node *)data;
	static int legacy_map = 0, legacy_irq = 0;

	spu->devnode = of_node_get(spe);
	spu->spe_id = find_spu_unit_number(spe);

	spu->node = of_node_to_nid(spe);
	if (spu->node >= MAX_NUMNODES) {
		printk(KERN_WARNING "SPE %s on node %d ignored,"
		       " node number too big\n", spe->full_name, spu->node);
		printk(KERN_WARNING "Check if CONFIG_NUMA is enabled.\n");
		ret = -ENODEV;
		goto out;
	}

	ret = spu_map_device(spu);
	if (ret) {
		if (!legacy_map) {
			legacy_map = 1;
			printk(KERN_WARNING "%s: Legacy device tree found, "
				"trying to map old style\n", __FUNCTION__);
		}
		ret = spu_map_device_old(spu);
		if (ret) {
			printk(KERN_ERR "Unable to map %s\n",
				spu->name);
			goto out;
		}
	}

	ret = spu_map_interrupts(spu, spe);
	if (ret) {
		if (!legacy_irq) {
			legacy_irq = 1;
			printk(KERN_WARNING "%s: Legacy device tree found, "
				"trying old style irq\n", __FUNCTION__);
		}
		ret = spu_map_interrupts_old(spu, spe);
		if (ret) {
			printk(KERN_ERR "%s: could not map interrupts",
				spu->name);
			goto out_unmap;
		}
	}

	pr_debug("Using SPE %s %p %p %p %p %d\n", spu->name,
		spu->local_store, spu->problem, spu->priv1,
		spu->priv2, spu->number);
	goto out;

out_unmap:
	spu_unmap(spu);
out:
	return ret;
}

static int of_destroy_spu(struct spu *spu)
{
	spu_unmap(spu);
	of_node_put(spu->devnode);
	return 0;
}

const struct spu_management_ops spu_management_of_ops = {
	.enumerate_spus = of_enumerate_spus,
	.create_spu = of_create_spu,
	.destroy_spu = of_destroy_spu,
};
