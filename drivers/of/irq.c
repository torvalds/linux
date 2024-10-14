// SPDX-License-Identifier: GPL-2.0+
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
 * This file contains the code used to make IRQ descriptions in the
 * device tree to actual irq numbers on an interrupt controller
 * driver.
 */

#define pr_fmt(fmt)	"OF: " fmt

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "of_private.h"

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
 * Return: A pointer to the interrupt parent node, or NULL if the interrupt
 * parent could not be determined.
 */
struct device_node *of_irq_find_parent(struct device_node *child)
{
	struct device_node *p;
	phandle parent;

	if (!of_node_get(child))
		return NULL;

	do {
		if (of_property_read_u32(child, "interrupt-parent", &parent)) {
			p = of_get_parent(child);
		} else	{
			if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
				p = of_node_get(of_irq_dflt_pic);
			else
				p = of_find_node_by_phandle(parent);
		}
		of_node_put(child);
		child = p;
	} while (p && of_get_property(p, "#interrupt-cells", NULL) == NULL);

	return p;
}
EXPORT_SYMBOL_GPL(of_irq_find_parent);

/*
 * These interrupt controllers abuse interrupt-map for unspeakable
 * reasons and rely on the core code to *ignore* it (the drivers do
 * their own parsing of the property). The PAsemi entry covers a
 * non-sensical interrupt-map that is better left ignored.
 *
 * If you think of adding to the list for something *new*, think
 * again. There is a high chance that you will be sent back to the
 * drawing board.
 */
static const char * const of_irq_imap_abusers[] = {
	"CBEA,platform-spider-pic",
	"sti,platform-spider-pic",
	"realtek,rtl-intc",
	"fsl,ls1021a-extirq",
	"fsl,ls1043a-extirq",
	"fsl,ls1088a-extirq",
	"renesas,rza1-irqc",
	"pasemi,rootbus",
	NULL,
};

const __be32 *of_irq_parse_imap_parent(const __be32 *imap, int len, struct of_phandle_args *out_irq)
{
	u32 intsize, addrsize;
	struct device_node *np;

	/* Get the interrupt parent */
	if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
		np = of_node_get(of_irq_dflt_pic);
	else
		np = of_find_node_by_phandle(be32_to_cpup(imap));
	imap++;

	/* Check if not found */
	if (!np) {
		pr_debug(" -> imap parent not found !\n");
		return NULL;
	}

	/* Get #interrupt-cells and #address-cells of new parent */
	if (of_property_read_u32(np, "#interrupt-cells",
					&intsize)) {
		pr_debug(" -> parent lacks #interrupt-cells!\n");
		of_node_put(np);
		return NULL;
	}
	if (of_property_read_u32(np, "#address-cells",
					&addrsize))
		addrsize = 0;

	pr_debug(" -> intsize=%d, addrsize=%d\n",
		intsize, addrsize);

	/* Check for malformed properties */
	if (WARN_ON(addrsize + intsize > MAX_PHANDLE_ARGS)
		|| (len < (addrsize + intsize))) {
		of_node_put(np);
		return NULL;
	}

	pr_debug(" -> imaplen=%d\n", len);

	imap += addrsize + intsize;

	out_irq->np = np;
	for (int i = 0; i < intsize; i++)
		out_irq->args[i] = be32_to_cpup(imap - intsize + i);
	out_irq->args_count = intsize;

	return imap;
}

/**
 * of_irq_parse_raw - Low level interrupt tree parsing
 * @addr:	address specifier (start of "reg" property of the device) in be32 format
 * @out_irq:	structure of_phandle_args updated by this function
 *
 * This function is a low-level interrupt tree walking function. It
 * can be used to do a partial walk with synthetized reg and interrupts
 * properties, for example when resolving PCI interrupts when no device
 * node exist for the parent. It takes an interrupt specifier structure as
 * input, walks the tree looking for any interrupt-map properties, translates
 * the specifier for each map, and then returns the translated map.
 *
 * Return: 0 on success and a negative number on error
 */
int of_irq_parse_raw(const __be32 *addr, struct of_phandle_args *out_irq)
{
	struct device_node *ipar, *tnode, *old = NULL;
	__be32 initial_match_array[MAX_PHANDLE_ARGS];
	const __be32 *match_array = initial_match_array;
	const __be32 *tmp, dummy_imask[] = { [0 ... MAX_PHANDLE_ARGS] = cpu_to_be32(~0) };
	u32 intsize = 1, addrsize;
	int i, rc = -EINVAL;

#ifdef DEBUG
	of_print_phandle_args("of_irq_parse_raw: ", out_irq);
#endif

	ipar = of_node_get(out_irq->np);

	/* First get the #interrupt-cells property of the current cursor
	 * that tells us how to interpret the passed-in intspec. If there
	 * is none, we are nice and just walk up the tree
	 */
	do {
		if (!of_property_read_u32(ipar, "#interrupt-cells", &intsize))
			break;
		tnode = ipar;
		ipar = of_irq_find_parent(ipar);
		of_node_put(tnode);
	} while (ipar);
	if (ipar == NULL) {
		pr_debug(" -> no parent found !\n");
		goto fail;
	}

	pr_debug("of_irq_parse_raw: ipar=%pOF, size=%d\n", ipar, intsize);

	if (out_irq->args_count != intsize)
		goto fail;

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
	if (WARN_ON(addrsize + intsize > MAX_PHANDLE_ARGS)) {
		rc = -EFAULT;
		goto fail;
	}

	/* Precalculate the match array - this simplifies match loop */
	for (i = 0; i < addrsize; i++)
		initial_match_array[i] = addr ? addr[i] : 0;
	for (i = 0; i < intsize; i++)
		initial_match_array[addrsize + i] = cpu_to_be32(out_irq->args[i]);

	/* Now start the actual "proper" walk of the interrupt tree */
	while (ipar != NULL) {
		int imaplen, match;
		const __be32 *imap, *oldimap, *imask;
		struct device_node *newpar;
		/*
		 * Now check if cursor is an interrupt-controller and
		 * if it is then we are done, unless there is an
		 * interrupt-map which takes precedence except on one
		 * of these broken platforms that want to parse
		 * interrupt-map themselves for $reason.
		 */
		bool intc = of_property_read_bool(ipar, "interrupt-controller");

		imap = of_get_property(ipar, "interrupt-map", &imaplen);
		if (intc &&
		    (!imap || of_device_compatible_match(ipar, of_irq_imap_abusers))) {
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
		while (imaplen > (addrsize + intsize + 1)) {
			/* Compare specifiers */
			match = 1;
			for (i = 0; i < (addrsize + intsize); i++, imaplen--)
				match &= !((match_array[i] ^ *imap++) & imask[i]);

			pr_debug(" -> match=%d (imaplen=%d)\n", match, imaplen);

			oldimap = imap;
			imap = of_irq_parse_imap_parent(oldimap, imaplen, out_irq);
			if (!imap)
				goto fail;

			match &= of_device_is_available(out_irq->np);
			if (match)
				break;

			of_node_put(out_irq->np);
			imaplen -= imap - oldimap;
			pr_debug(" -> imaplen=%d\n", imaplen);
		}
		if (!match)
			goto fail;

		/*
		 * Successfully parsed an interrupt-map translation; copy new
		 * interrupt specifier into the out_irq structure
		 */
		match_array = oldimap + 1;

		newpar = out_irq->np;
		intsize = out_irq->args_count;
		addrsize = (imap - match_array) - intsize;

		if (ipar == newpar) {
			pr_debug("%pOF interrupt-map entry to self\n", ipar);
			return 0;
		}

	skiplevel:
		/* Iterate again with new parent */
		pr_debug(" -> new parent: %pOF\n", newpar);
		of_node_put(ipar);
		ipar = newpar;
		newpar = NULL;
	}
	rc = -ENOENT; /* No interrupt-map found */

 fail:
	of_node_put(ipar);

	return rc;
}
EXPORT_SYMBOL_GPL(of_irq_parse_raw);

/**
 * of_irq_parse_one - Resolve an interrupt for a device
 * @device: the device whose interrupt is to be resolved
 * @index: index of the interrupt to resolve
 * @out_irq: structure of_phandle_args filled by this function
 *
 * This function resolves an interrupt for a node by walking the interrupt tree,
 * finding which interrupt controller node it is attached to, and returning the
 * interrupt specifier that can be used to retrieve a Linux IRQ number.
 */
int of_irq_parse_one(struct device_node *device, int index, struct of_phandle_args *out_irq)
{
	struct device_node *p;
	const __be32 *addr;
	u32 intsize;
	int i, res, addr_len;
	__be32 addr_buf[3] = { 0 };

	pr_debug("of_irq_parse_one: dev=%pOF, index=%d\n", device, index);

	/* OldWorld mac stuff is "special", handle out of line */
	if (of_irq_workarounds & OF_IMAP_OLDWORLD_MAC)
		return of_irq_parse_oldworld(device, index, out_irq);

	/* Get the reg property (if any) */
	addr = of_get_property(device, "reg", &addr_len);

	/* Prevent out-of-bounds read in case of longer interrupt parent address size */
	if (addr_len > sizeof(addr_buf))
		addr_len = sizeof(addr_buf);
	if (addr)
		memcpy(addr_buf, addr, addr_len);

	/* Try the new-style interrupts-extended first */
	res = of_parse_phandle_with_args(device, "interrupts-extended",
					"#interrupt-cells", index, out_irq);
	if (!res)
		return of_irq_parse_raw(addr_buf, out_irq);

	/* Look for the interrupt parent. */
	p = of_irq_find_parent(device);
	if (p == NULL)
		return -EINVAL;

	/* Get size of interrupt specifier */
	if (of_property_read_u32(p, "#interrupt-cells", &intsize)) {
		res = -EINVAL;
		goto out;
	}

	pr_debug(" parent=%pOF, intsize=%d\n", p, intsize);

	/* Copy intspec into irq structure */
	out_irq->np = p;
	out_irq->args_count = intsize;
	for (i = 0; i < intsize; i++) {
		res = of_property_read_u32_index(device, "interrupts",
						 (index * intsize) + i,
						 out_irq->args + i);
		if (res)
			goto out;
	}

	pr_debug(" intspec=%d\n", *out_irq->args);


	/* Check if there are any interrupt-map translations to process */
	res = of_irq_parse_raw(addr_buf, out_irq);
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
	int irq = of_irq_get(dev, index);

	if (irq < 0)
		return irq;

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

		*r = DEFINE_RES_IRQ_NAMED(irq, name ?: of_node_full_name(dev));
		r->flags |= irq_get_trigger_type(irq);
	}

	return irq;
}
EXPORT_SYMBOL_GPL(of_irq_to_resource);

/**
 * of_irq_get - Decode a node's IRQ and return it as a Linux IRQ number
 * @dev: pointer to device tree node
 * @index: zero-based index of the IRQ
 *
 * Return: Linux IRQ number on success, or 0 on the IRQ mapping failure, or
 * -EPROBE_DEFER if the IRQ domain is not yet created, or error code in case
 * of any other failure.
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
	if (!domain) {
		rc = -EPROBE_DEFER;
		goto out;
	}

	rc = irq_create_of_mapping(&oirq);
out:
	of_node_put(oirq.np);

	return rc;
}
EXPORT_SYMBOL_GPL(of_irq_get);

/**
 * of_irq_get_byname - Decode a node's IRQ and return it as a Linux IRQ number
 * @dev: pointer to device tree node
 * @name: IRQ name
 *
 * Return: Linux IRQ number on success, or 0 on the IRQ mapping failure, or
 * -EPROBE_DEFER if the IRQ domain is not yet created, or error code in case
 * of any other failure.
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
 * Return: The size of the filled in table (up to @nr_irqs).
 */
int of_irq_to_resource_table(struct device_node *dev, struct resource *res,
		int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++, res++)
		if (of_irq_to_resource(dev, i, res) <= 0)
			break;

	return i;
}
EXPORT_SYMBOL_GPL(of_irq_to_resource_table);

struct of_intc_desc {
	struct list_head	list;
	of_irq_init_cb_t	irq_init_cb;
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
	const struct of_device_id *match;
	struct device_node *np, *parent = NULL;
	struct of_intc_desc *desc, *temp_desc;
	struct list_head intc_desc_list, intc_parent_list;

	INIT_LIST_HEAD(&intc_desc_list);
	INIT_LIST_HEAD(&intc_parent_list);

	for_each_matching_node_and_match(np, matches, &match) {
		if (!of_property_read_bool(np, "interrupt-controller") ||
				!of_device_is_available(np))
			continue;

		if (WARN(!match->data, "of_irq_init: no init function for %s\n",
			 match->compatible))
			continue;

		/*
		 * Here, we allocate and populate an of_intc_desc with the node
		 * pointer, interrupt-parent device_node etc.
		 */
		desc = kzalloc(sizeof(*desc), GFP_KERNEL);
		if (!desc) {
			of_node_put(np);
			goto err;
		}

		desc->irq_init_cb = match->data;
		desc->dev = of_node_get(np);
		/*
		 * interrupts-extended can reference multiple parent domains.
		 * Arbitrarily pick the first one; assume any other parents
		 * are the same distance away from the root irq controller.
		 */
		desc->interrupt_parent = of_parse_phandle(np, "interrupts-extended", 0);
		if (!desc->interrupt_parent)
			desc->interrupt_parent = of_irq_find_parent(np);
		if (desc->interrupt_parent == np) {
			of_node_put(desc->interrupt_parent);
			desc->interrupt_parent = NULL;
		}
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
			int ret;

			if (desc->interrupt_parent != parent)
				continue;

			list_del(&desc->list);

			of_node_set_flag(desc->dev, OF_POPULATED);

			pr_debug("of_irq_init: init %pOF (%p), parent %p\n",
				 desc->dev,
				 desc->dev, desc->interrupt_parent);
			ret = desc->irq_init_cb(desc->dev,
						desc->interrupt_parent);
			if (ret) {
				pr_err("%s: Failed to init %pOF (%p), parent %p\n",
				       __func__, desc->dev, desc->dev,
				       desc->interrupt_parent);
				of_node_clear_flag(desc->dev, OF_POPULATED);
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

static u32 __of_msi_map_id(struct device *dev, struct device_node **np,
			    u32 id_in)
{
	struct device *parent_dev;
	u32 id_out = id_in;

	/*
	 * Walk up the device parent links looking for one with a
	 * "msi-map" property.
	 */
	for (parent_dev = dev; parent_dev; parent_dev = parent_dev->parent)
		if (!of_map_id(parent_dev->of_node, id_in, "msi-map",
				"msi-map-mask", np, &id_out))
			break;
	return id_out;
}

/**
 * of_msi_map_id - Map a MSI ID for a device.
 * @dev: device for which the mapping is to be done.
 * @msi_np: device node of the expected msi controller.
 * @id_in: unmapped MSI ID for the device.
 *
 * Walk up the device hierarchy looking for devices with a "msi-map"
 * property.  If found, apply the mapping to @id_in.
 *
 * Return: The mapped MSI ID.
 */
u32 of_msi_map_id(struct device *dev, struct device_node *msi_np, u32 id_in)
{
	return __of_msi_map_id(dev, &msi_np, id_in);
}

/**
 * of_msi_map_get_device_domain - Use msi-map to find the relevant MSI domain
 * @dev: device for which the mapping is to be done.
 * @id: Device ID.
 * @bus_token: Bus token
 *
 * Walk up the device hierarchy looking for devices with a "msi-map"
 * property.
 *
 * Returns: the MSI domain for this device (or NULL on failure)
 */
struct irq_domain *of_msi_map_get_device_domain(struct device *dev, u32 id,
						u32 bus_token)
{
	struct device_node *np = NULL;

	__of_msi_map_id(dev, &np, id);
	return irq_find_matching_host(np, bus_token);
}

/**
 * of_msi_get_domain - Use msi-parent to find the relevant MSI domain
 * @dev: device for which the domain is requested
 * @np: device node for @dev
 * @token: bus type for this domain
 *
 * Parse the msi-parent property and returns the corresponding MSI domain.
 *
 * Returns: the MSI domain for this device (or NULL on failure).
 */
struct irq_domain *of_msi_get_domain(struct device *dev,
				     struct device_node *np,
				     enum irq_domain_bus_token token)
{
	struct of_phandle_iterator it;
	struct irq_domain *d;
	int err;

	of_for_each_phandle(&it, err, np, "msi-parent", "#msi-cells", 0) {
		d = irq_find_matching_host(it.node, token);
		if (d)
			return d;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(of_msi_get_domain);

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
EXPORT_SYMBOL_GPL(of_msi_configure);
