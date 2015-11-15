/*
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan <cort@fsmlabs.com>
 *    Copyright (C) 1996-2001 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This file contains the code used to make IRQ descriptions in the
 * device tree to actual irq numbers on an interrupt controller
 * driver.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/string.h>
#include <linux/slab.h>

/**
 * irq_of_parse_and_map - Parse and map an interrupt into linux virq space
 * @dev: Device node of the device whose interrupt is to be mapped
 * @index: Index of the interrupt to map
 *
 * This function is a wrapper that chains of_irq_parse_one() and
 * irq_create_of_mapping() to make things easier to callers
 */
unsigned int irq_of_parse_and_map(struct device_node *dev, int index)
{
	struct of_phandle_args oirq;

	if (of_irq_parse_one(dev, index, &oirq))
		return 0;

	return irq_create_of_mapping(&oirq);
}
EXPORT_SYMBOL_GPL(irq_of_parse_and_map);

/**
 * of_irq_find_parent - Given a device node, find its interrupt parent node
 * @child: pointer to device node
 *
 * Returns a pointer to the interrupt parent node, or NULL if the interrupt
 * parent could not be determined.
 */
static struct device_node *of_irq_find_parent(struct device_node *child)
{
	struct device_node *p;
	const __be32 *parp;

	if (!of_node_get(child))
		return NULL;

	do {
		parp = of_get_property(child, "interrupt-parent", NULL);
		if (parp == NULL)
			p = of_get_parent(child);
		else {
			if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
				p = of_node_get(of_irq_dflt_pic);
			else
				p = of_find_node_by_phandle(be32_to_cpup(parp));
		}
		of_node_put(child);
		child = p;
	} while (p && of_get_property(p, "#interrupt-cells", NULL) == NULL);

	return p;
}

/**
 * of_irq_parse_raw - Low level interrupt tree parsing
 * @parent:	the device interrupt parent
 * @addr:	address specifier (start of "reg" property of the device) in be32 format
 * @out_irq:	structure of_irq updated by this function
 *
 * Returns 0 on success and a negative number on error
 *
 * This function is a low-level interrupt tree walking function. It
 * can be used to do a partial walk with synthetized reg and interrupts
 * properties, for example when resolving PCI interrupts when no device
 * node exist for the parent. It takes an interrupt specifier structure as
 * input, walks the tree looking for any interrupt-map properties, translates
 * the specifier for each map, and then returns the translated map.
 */
int of_irq_parse_raw(const __be32 *addr, struct of_phandle_args *out_irq)
{
	struct device_node *ipar, *tnode, *old = NULL, *newpar = NULL;
	__be32 initial_match_array[MAX_PHANDLE_ARGS];
	const __be32 *match_array = initial_match_array;
	const __be32 *tmp, *imap, *imask, dummy_imask[] = { [0 ... MAX_PHANDLE_ARGS] = ~0 };
	u32 intsize = 1, addrsize, newintsize = 0, newaddrsize = 0;
	int imaplen, match, i;

#ifdef DEBUG
	of_print_phandle_args("of_irq_parse_raw: ", out_irq);
#endif

	ipar = of_node_get(out_irq->np);

	/* First get the #interrupt-cells property of the current cursor
	 * that tells us how to interpret the passed-in intspec. If there
	 * is none, we are nice and just walk up the tree
	 */
	do {
		tmp = of_get_property(ipar, "#interrupt-cells", NULL);
		if (tmp != NULL) {
			intsize = be32_to_cpu(*tmp);
			break;
		}
		tnode = ipar;
		ipar = of_irq_find_parent(ipar);
		of_node_put(tnode);
	} while (ipar);
	if (ipar == NULL) {
		pr_debug(" -> no parent found !\n");
		goto fail;
	}

	pr_debug("of_irq_parse_raw: ipar=%s, size=%d\n", of_node_full_name(ipar), intsize);

	if (out_irq->args_count != intsize)
		return -EINVAL;

	/* Look for this #address-cells. We have to implement the old linux
	 * trick of looking for the parent here as some device-trees rely on it
	 */
	old = of_node_get(ipar);
	do {
		tmp = of_get_property(old, "#address-cells", NULL);
		tnode = of_get_parent(old);
		of_node_put(old);
		old = tnode;
	} while (old && tmp == NULL);
	of_node_put(old);
	old = NULL;
	addrsize = (tmp == NULL) ? 2 : be32_to_cpu(*tmp);

	pr_debug(" -> addrsize=%d\n", addrsize);

	/* Range check so that the temporary buffer doesn't overflow */
	if (WARN_ON(addrsize + intsize > MAX_PHANDLE_ARGS))
		goto fail;

	/* Precalculate the match array - this simplifies match loop */
	for (i = 0; i < addrsize; i++)
		initial_match_array[i] = addr ? addr[i] : 0;
	for (i = 0; i < intsize; i++)
		initial_match_array[addrsize + i] = cpu_to_be32(out_irq->args[i]);

	/* Now start the actual "proper" walk of the interrupt tree */
	while (ipar != NULL) {
		/* Now check if cursor is an interrupt-controller and if it is
		 * then we are done
		 */
		if (of_get_property(ipar, "interrupt-controller", NULL) !=
				NULL) {
			pr_debug(" -> got it !\n");
			return 0;
		}

		/*
		 * interrupt-map parsing does not work without a reg
		 * property when #address-cells != 0
		 */
		if (addrsize && !addr) {
			pr_debug(" -> no reg passed in when needed !\n");
			goto fail;
		}

		/* Now look for an interrupt-map */
		imap = of_get_property(ipar, "interrupt-map", &imaplen);
		/* No interrupt map, check for an interrupt parent */
		if (imap == NULL) {
			pr_debug(" -> no map, getting parent\n");
			newpar = of_irq_find_parent(ipar);
			goto skiplevel;
		}
		imaplen /= sizeof(u32);

		/* Look for a mask */
		imask = of_get_property(ipar, "interrupt-map-mask", NULL);
		if (!imask)
			imask = dummy_imask;

		/* Parse interrupt-map */
		match = 0;
		while (imaplen > (addrsize + intsize + 1) && !match) {
			/* Compare specifiers */
			match = 1;
			for (i = 0; i < (addrsize + intsize); i++, imaplen--)
				match &= !((match_array[i] ^ *imap++) & imask[i]);

			pr_debug(" -> match=%d (imaplen=%d)\n", match, imaplen);

			/* Get the interrupt parent */
			if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
				newpar = of_node_get(of_irq_dflt_pic);
			else
				newpar = of_find_node_by_phandle(be32_to_cpup(imap));
			imap++;
			--imaplen;

			/* Check if not found */
			if (newpar == NULL) {
				pr_debug(" -> imap parent not found !\n");
				goto fail;
			}

			if (!of_device_is_available(newpar))
				match = 0;

			/* Get #interrupt-cells and #address-cells of new
			 * parent
			 */
			tmp = of_get_property(newpar, "#interrupt-cells", NULL);
			if (tmp == NULL) {
				pr_debug(" -> parent lacks #interrupt-cells!\n");
				goto fail;
			}
			newintsize = be32_to_cpu(*tmp);
			tmp = of_get_property(newpar, "#address-cells", NULL);
			newaddrsize = (tmp == NULL) ? 0 : be32_to_cpu(*tmp);

			pr_debug(" -> newintsize=%d, newaddrsize=%d\n",
			    newintsize, newaddrsize);

			/* Check for malformed properties */
			if (WARN_ON(newaddrsize + newintsize > MAX_PHANDLE_ARGS))
				goto fail;
			if (imaplen < (newaddrsize + newintsize))
				goto fail;

			imap += newaddrsize + newintsize;
			imaplen -= newaddrsize + newintsize;

			pr_debug(" -> imaplen=%d\n", imaplen);
		}
		if (!match)
			goto fail;

		/*
		 * Successfully parsed an interrrupt-map translation; copy new
		 * interrupt specifier into the out_irq structure
		 */
		match_array = imap - newaddrsize - newintsize;
		for (i = 0; i < newintsize; i++)
			out_irq->args[i] = be32_to_cpup(imap - newintsize + i);
		out_irq->args_count = intsize = newintsize;
		addrsize = newaddrsize;

	skiplevel:
		/* Iterate again with new parent */
		out_irq->np = newpar;
		pr_debug(" -> new parent: %s\n", of_node_full_name(newpar));
		of_node_put(ipar);
		ipar = newpar;
		newpar = NULL;
	}
 fail:
	of_node_put(ipar);
	of_node_put(newpar);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(of_irq_parse_raw);

/**
 * of_irq_parse_one - Resolve an interrupt for a device
 * @device: the device whose interrupt is to be resolved
 * @index: index of the interrupt to resolve
 * @out_irq: structure of_irq filled by this function
 *
 * This function resolves an interrupt for a node by walking the interrupt tree,
 * finding which interrupt controller node it is attached to, and returning the
 * interrupt specifier that can be used to retrieve a Linux IRQ number.
 */
int of_irq_parse_one(struct device_node *device, int index, struct of_phandle_args *out_irq)
{
	struct device_node *p;
	const __be32 *intspec, *tmp, *addr;
	u32 intsize, intlen;
	int i, res;

	pr_debug("of_irq_parse_one: dev=%s, index=%d\n", of_node_full_name(device), index);

	/* OldWorld mac stuff is "special", handle out of line */
	if (of_irq_workarounds & OF_IMAP_OLDWORLD_MAC)
		return of_irq_parse_oldworld(device, index, out_irq);

	/* Get the reg property (if any) */
	addr = of_get_property(device, "reg", NULL);

	/* Try the new-style interrupts-extended first */
	res = of_parse_phandle_with_args(device, "interrupts-extended",
					"#interrupt-cells", index, out_irq);
	if (!res)
		return of_irq_parse_raw(addr, out_irq);

	/* Get the interrupts property */
	intspec = of_get_property(device, "interrupts", &intlen);
	if (intspec == NULL)
		return -EINVAL;

	intlen /= sizeof(*intspec);

	pr_debug(" intspec=%d intlen=%d\n", be32_to_cpup(intspec), intlen);

	/* Look for the interrupt parent. */
	p = of_irq_find_parent(device);
	if (p == NULL)
		return -EINVAL;

	/* Get size of interrupt specifier */
	tmp = of_get_property(p, "#interrupt-cells", NULL);
	if (tmp == NULL) {
		res = -EINVAL;
		goto out;
	}
	intsize = be32_to_cpu(*tmp);

	pr_debug(" intsize=%d intlen=%d\n", intsize, intlen);

	/* Check index */
	if ((index + 1) * intsize > intlen) {
		res = -EINVAL;
		goto out;
	}

	/* Copy intspec into irq structure */
	intspec += index * intsize;
	out_irq->np = p;
	out_irq->args_count = intsize;
	for (i = 0; i < intsize; i++)
		out_irq->args[i] = be32_to_cpup(intspec++);

	/* Check if there are any interrupt-map translations to process */
	res = of_irq_parse_raw(addr, out_irq);
 out:
	of_node_put(p);
	return res;
}
EXPORT_SYMBOL_GPL(of_irq_parse_one);

/**
 * of_irq_to_resource - Decode a node's IRQ and return it as a resource
 * @dev: pointer to device tree node
 * @index: zero-based index of the irq
 * @r: pointer to resource structure to return result into.
 */
int of_irq_to_resource(struct device_node *dev, int index, struct resource *r)
{
	int irq = irq_of_parse_and_map(dev, index);

	/* Only dereference the resource if both the
	 * resource and the irq are valid. */
	if (r && irq) {
		const char *name = NULL;

		memset(r, 0, sizeof(*r));
		/*
		 * Get optional "interrupt-names" property to add a name
		 * to the resource.
		 */
		of_property_read_string_index(dev, "interrupt-names", index,
					      &name);

		r->start = r->end = irq;
		r->flags = IORESOURCE_IRQ | irqd_get_trigger_type(irq_get_irq_data(irq));
		r->name = name ? name : of_node_full_name(dev);
	}

	return irq;
}
EXPORT_SYMBOL_GPL(of_irq_to_resource);

/**
 * of_irq_get - Decode a node's IRQ and return it as a Linux irq number
 * @dev: pointer to device tree node
 * @index: zero-based index of the irq
 *
 * Returns Linux irq number on success, or -EPROBE_DEFER if the irq domain
 * is not yet created.
 *
 */
int of_irq_get(struct device_node *dev, int index)
{
	int rc;
	struct of_phandle_args oirq;
	struct irq_domain *domain;

	rc = of_irq_parse_one(dev, index, &oirq);
	if (rc)
		return rc;

	domain = irq_find_host(oirq.np);
	if (!domain)
		return -EPROBE_DEFER;

	return irq_create_of_mapping(&oirq);
}
EXPORT_SYMBOL_GPL(of_irq_get);

/**
 * of_irq_get_byname - Decode a node's IRQ and return it as a Linux irq number
 * @dev: pointer to device tree node
 * @name: irq name
 *
 * Returns Linux irq number on success, or -EPROBE_DEFER if the irq domain
 * is not yet created, or error code in case of any other failure.
 */
int of_irq_get_byname(struct device_node *dev, const char *name)
{
	int index;

	if (unlikely(!name))
		return -EINVAL;

	index = of_property_match_string(dev, "interrupt-names", name);
	if (index < 0)
		return index;

	return of_irq_get(dev, index);
}
EXPORT_SYMBOL_GPL(of_irq_get_byname);

/**
 * of_irq_count - Count the number of IRQs a node uses
 * @dev: pointer to device tree node
 */
int of_irq_count(struct device_node *dev)
{
	struct of_phandle_args irq;
	int nr = 0;

	while (of_irq_parse_one(dev, nr, &irq) == 0)
		nr++;

	return nr;
}

/**
 * of_irq_to_resource_table - Fill in resource table with node's IRQ info
 * @dev: pointer to device tree node
 * @res: array of resources to fill in
 * @nr_irqs: the number of IRQs (and upper bound for num of @res elements)
 *
 * Returns the size of the filled in table (up to @nr_irqs).
 */
int of_irq_to_resource_table(struct device_node *dev, struct resource *res,
		int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++, res++)
		if (!of_irq_to_resource(dev, i, res))
			break;

	return i;
}
EXPORT_SYMBOL_GPL(of_irq_to_resource_table);

struct of_intc_desc {
	struct list_head	list;
	struct device_node	*dev;
	struct device_node	*interrupt_parent;
};

/**
 * of_irq_init - Scan and init matching interrupt controllers in DT
 * @matches: 0 terminated array of nodes to match and init function to call
 *
 * This function scans the device tree for matching interrupt controller nodes,
 * and calls their initialization functions in order with parents first.
 */
void __init of_irq_init(const struct of_device_id *matches)
{
	struct device_node *np, *parent = NULL;
	struct of_intc_desc *desc, *temp_desc;
	struct list_head intc_desc_list, intc_parent_list;

	INIT_LIST_HEAD(&intc_desc_list);
	INIT_LIST_HEAD(&intc_parent_list);

	for_each_matching_node(np, matches) {
		if (!of_find_property(np, "interrupt-controller", NULL) ||
				!of_device_is_available(np))
			continue;
		/*
		 * Here, we allocate and populate an of_intc_desc with the node
		 * pointer, interrupt-parent device_node etc.
		 */
		desc = kzalloc(sizeof(*desc), GFP_KERNEL);
		if (WARN_ON(!desc)) {
			of_node_put(np);
			goto err;
		}

		desc->dev = of_node_get(np);
		desc->interrupt_parent = of_irq_find_parent(np);
		if (desc->interrupt_parent == np)
			desc->interrupt_parent = NULL;
		list_add_tail(&desc->list, &intc_desc_list);
	}

	/*
	 * The root irq controller is the one without an interrupt-parent.
	 * That one goes first, followed by the controllers that reference it,
	 * followed by the ones that reference the 2nd level controllers, etc.
	 */
	while (!list_empty(&intc_desc_list)) {
		/*
		 * Process all controllers with the current 'parent'.
		 * First pass will be looking for NULL as the parent.
		 * The assumption is that NULL parent means a root controller.
		 */
		list_for_each_entry_safe(desc, temp_desc, &intc_desc_list, list) {
			const struct of_device_id *match;
			int ret;
			of_irq_init_cb_t irq_init_cb;

			if (desc->interrupt_parent != parent)
				continue;

			list_del(&desc->list);
			match = of_match_node(matches, desc->dev);
			if (WARN(!match->data,
			    "of_irq_init: no init function for %s\n",
			    match->compatible)) {
				kfree(desc);
				continue;
			}

			pr_debug("of_irq_init: init %s @ %p, parent %p\n",
				 match->compatible,
				 desc->dev, desc->interrupt_parent);
			irq_init_cb = (of_irq_init_cb_t)match->data;
			ret = irq_init_cb(desc->dev, desc->interrupt_parent);
			if (ret) {
				kfree(desc);
				continue;
			}

			/*
			 * This one is now set up; add it to the parent list so
			 * its children can get processed in a subsequent pass.
			 */
			list_add_tail(&desc->list, &intc_parent_list);
		}

		/* Get the next pending parent that might have children */
		desc = list_first_entry_or_null(&intc_parent_list,
						typeof(*desc), list);
		if (!desc) {
			pr_err("of_irq_init: children remain, but no parents\n");
			break;
		}
		list_del(&desc->list);
		parent = desc->dev;
		kfree(desc);
	}

	list_for_each_entry_safe(desc, temp_desc, &intc_parent_list, list) {
		list_del(&desc->list);
		kfree(desc);
	}
err:
	list_for_each_entry_safe(desc, temp_desc, &intc_desc_list, list) {
		list_del(&desc->list);
		of_node_put(desc->dev);
		kfree(desc);
	}
}

static u32 __of_msi_map_rid(struct device *dev, struct device_node **np,
			    u32 rid_in)
{
	struct device *parent_dev;
	struct device_node *msi_controller_node;
	struct device_node *msi_np = *np;
	u32 map_mask, masked_rid, rid_base, msi_base, rid_len, phandle;
	int msi_map_len;
	bool matched;
	u32 rid_out = rid_in;
	const __be32 *msi_map = NULL;

	/*
	 * Walk up the device parent links looking for one with a
	 * "msi-map" property.
	 */
	for (parent_dev = dev; parent_dev; parent_dev = parent_dev->parent) {
		if (!parent_dev->of_node)
			continue;

		msi_map = of_get_property(parent_dev->of_node,
					  "msi-map", &msi_map_len);
		if (!msi_map)
			continue;

		if (msi_map_len % (4 * sizeof(__be32))) {
			dev_err(parent_dev, "Error: Bad msi-map length: %d\n",
				msi_map_len);
			return rid_out;
		}
		/* We have a good parent_dev and msi_map, let's use them. */
		break;
	}
	if (!msi_map)
		return rid_out;

	/* The default is to select all bits. */
	map_mask = 0xffffffff;

	/*
	 * Can be overridden by "msi-map-mask" property.  If
	 * of_property_read_u32() fails, the default is used.
	 */
	of_property_read_u32(parent_dev->of_node, "msi-map-mask", &map_mask);

	masked_rid = map_mask & rid_in;
	matched = false;
	while (!matched && msi_map_len >= 4 * sizeof(__be32)) {
		rid_base = be32_to_cpup(msi_map + 0);
		phandle = be32_to_cpup(msi_map + 1);
		msi_base = be32_to_cpup(msi_map + 2);
		rid_len = be32_to_cpup(msi_map + 3);

		msi_controller_node = of_find_node_by_phandle(phandle);

		matched = (masked_rid >= rid_base &&
			   masked_rid < rid_base + rid_len);
		if (msi_np)
			matched &= msi_np == msi_controller_node;

		if (matched && !msi_np) {
			*np = msi_np = msi_controller_node;
			break;
		}

		of_node_put(msi_controller_node);
		msi_map_len -= 4 * sizeof(__be32);
		msi_map += 4;
	}
	if (!matched)
		return rid_out;

	rid_out = masked_rid + msi_base;
	dev_dbg(dev,
		"msi-map at: %s, using mask %08x, rid-base: %08x, msi-base: %08x, length: %08x, rid: %08x -> %08x\n",
		dev_name(parent_dev), map_mask, rid_base, msi_base,
		rid_len, rid_in, rid_out);

	return rid_out;
}

/**
 * of_msi_map_rid - Map a MSI requester ID for a device.
 * @dev: device for which the mapping is to be done.
 * @msi_np: device node of the expected msi controller.
 * @rid_in: unmapped MSI requester ID for the device.
 *
 * Walk up the device hierarchy looking for devices with a "msi-map"
 * property.  If found, apply the mapping to @rid_in.
 *
 * Returns the mapped MSI requester ID.
 */
u32 of_msi_map_rid(struct device *dev, struct device_node *msi_np, u32 rid_in)
{
	return __of_msi_map_rid(dev, &msi_np, rid_in);
}

static struct irq_domain *__of_get_msi_domain(struct device_node *np,
					      enum irq_domain_bus_token token)
{
	struct irq_domain *d;

	d = irq_find_matching_host(np, token);
	if (!d)
		d = irq_find_host(np);

	return d;
}

/**
 * of_msi_map_get_device_domain - Use msi-map to find the relevant MSI domain
 * @dev: device for which the mapping is to be done.
 * @rid: Requester ID for the device.
 *
 * Walk up the device hierarchy looking for devices with a "msi-map"
 * property.
 *
 * Returns: the MSI domain for this device (or NULL on failure)
 */
struct irq_domain *of_msi_map_get_device_domain(struct device *dev, u32 rid)
{
	struct device_node *np = NULL;

	__of_msi_map_rid(dev, &np, rid);
	return __of_get_msi_domain(np, DOMAIN_BUS_PCI_MSI);
}

/**
 * of_msi_get_domain - Use msi-parent to find the relevant MSI domain
 * @dev: device for which the domain is requested
 * @np: device node for @dev
 * @token: bus type for this domain
 *
 * Parse the msi-parent property (both the simple and the complex
 * versions), and returns the corresponding MSI domain.
 *
 * Returns: the MSI domain for this device (or NULL on failure).
 */
struct irq_domain *of_msi_get_domain(struct device *dev,
				     struct device_node *np,
				     enum irq_domain_bus_token token)
{
	struct device_node *msi_np;
	struct irq_domain *d;

	/* Check for a single msi-parent property */
	msi_np = of_parse_phandle(np, "msi-parent", 0);
	if (msi_np && !of_property_read_bool(msi_np, "#msi-cells")) {
		d = __of_get_msi_domain(msi_np, token);
		if (!d)
			of_node_put(msi_np);
		return d;
	}

	if (token == DOMAIN_BUS_PLATFORM_MSI) {
		/* Check for the complex msi-parent version */
		struct of_phandle_args args;
		int index = 0;

		while (!of_parse_phandle_with_args(np, "msi-parent",
						   "#msi-cells",
						   index, &args)) {
			d = __of_get_msi_domain(args.np, token);
			if (d)
				return d;

			of_node_put(args.np);
			index++;
		}
	}

	return NULL;
}

/**
 * of_msi_configure - Set the msi_domain field of a device
 * @dev: device structure to associate with an MSI irq domain
 * @np: device node for that device
 */
void of_msi_configure(struct device *dev, struct device_node *np)
{
	dev_set_msi_domain(dev,
			   of_msi_get_domain(dev, np, DOMAIN_BUS_PLATFORM_MSI));
}
