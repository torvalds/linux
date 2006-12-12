#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>

#include <asm/errno.h>
#include <asm/of_device.h>

/**
 * of_match_device - Tell if an of_device structure has a matching
 * of_match structure
 * @ids: array of of device match structures to search in
 * @dev: the of device structure to match against
 *
 * Used by a driver to check whether an of_device present in the
 * system is in its list of supported devices.
 */
const struct of_device_id *of_match_device(const struct of_device_id *matches,
					const struct of_device *dev)
{
	if (!dev->node)
		return NULL;
	while (matches->name[0] || matches->type[0] || matches->compatible[0]) {
		int match = 1;
		if (matches->name[0])
			match &= dev->node->name
				&& !strcmp(matches->name, dev->node->name);
		if (matches->type[0])
			match &= dev->node->type
				&& !strcmp(matches->type, dev->node->type);
		if (matches->compatible[0])
			match &= of_device_is_compatible(dev->node,
							 matches->compatible);
		if (match)
			return matches;
		matches++;
	}
	return NULL;
}

static int of_platform_bus_match(struct device *dev, struct device_driver *drv)
{
	struct of_device * of_dev = to_of_device(dev);
	struct of_platform_driver * of_drv = to_of_platform_driver(drv);
	const struct of_device_id * matches = of_drv->match_table;

	if (!matches)
		return 0;

	return of_match_device(matches, of_dev) != NULL;
}

struct of_device *of_dev_get(struct of_device *dev)
{
	struct device *tmp;

	if (!dev)
		return NULL;
	tmp = get_device(&dev->dev);
	if (tmp)
		return to_of_device(tmp);
	else
		return NULL;
}

void of_dev_put(struct of_device *dev)
{
	if (dev)
		put_device(&dev->dev);
}


static int of_device_probe(struct device *dev)
{
	int error = -ENODEV;
	struct of_platform_driver *drv;
	struct of_device *of_dev;
	const struct of_device_id *match;

	drv = to_of_platform_driver(dev->driver);
	of_dev = to_of_device(dev);

	if (!drv->probe)
		return error;

	of_dev_get(of_dev);

	match = of_match_device(drv->match_table, of_dev);
	if (match)
		error = drv->probe(of_dev, match);
	if (error)
		of_dev_put(of_dev);

	return error;
}

static int of_device_remove(struct device *dev)
{
	struct of_device * of_dev = to_of_device(dev);
	struct of_platform_driver * drv = to_of_platform_driver(dev->driver);

	if (dev->driver && drv->remove)
		drv->remove(of_dev);
	return 0;
}

static int of_device_suspend(struct device *dev, pm_message_t state)
{
	struct of_device * of_dev = to_of_device(dev);
	struct of_platform_driver * drv = to_of_platform_driver(dev->driver);
	int error = 0;

	if (dev->driver && drv->suspend)
		error = drv->suspend(of_dev, state);
	return error;
}

static int of_device_resume(struct device * dev)
{
	struct of_device * of_dev = to_of_device(dev);
	struct of_platform_driver * drv = to_of_platform_driver(dev->driver);
	int error = 0;

	if (dev->driver && drv->resume)
		error = drv->resume(of_dev);
	return error;
}

void __iomem *of_ioremap(struct resource *res, unsigned long offset, unsigned long size, char *name)
{
	unsigned long ret = res->start + offset;
	struct resource *r;

	if (res->flags & IORESOURCE_MEM)
		r = request_mem_region(ret, size, name);
	else
		r = request_region(ret, size, name);
	if (!r)
		ret = 0;

	return (void __iomem *) ret;
}
EXPORT_SYMBOL(of_ioremap);

void of_iounmap(void __iomem *base, unsigned long size)
{
	release_region((unsigned long) base, size);
}
EXPORT_SYMBOL(of_iounmap);

static int node_match(struct device *dev, void *data)
{
	struct of_device *op = to_of_device(dev);
	struct device_node *dp = data;

	return (op->node == dp);
}

struct of_device *of_find_device_by_node(struct device_node *dp)
{
	struct device *dev = bus_find_device(&of_bus_type, NULL,
					     dp, node_match);

	if (dev)
		return to_of_device(dev);

	return NULL;
}
EXPORT_SYMBOL(of_find_device_by_node);

#ifdef CONFIG_PCI
struct bus_type isa_bus_type = {
       .name	= "isa",
       .match	= of_platform_bus_match,
       .probe	= of_device_probe,
       .remove	= of_device_remove,
       .suspend	= of_device_suspend,
       .resume	= of_device_resume,
};
EXPORT_SYMBOL(isa_bus_type);

struct bus_type ebus_bus_type = {
       .name	= "ebus",
       .match	= of_platform_bus_match,
       .probe	= of_device_probe,
       .remove	= of_device_remove,
       .suspend	= of_device_suspend,
       .resume	= of_device_resume,
};
EXPORT_SYMBOL(ebus_bus_type);
#endif

#ifdef CONFIG_SBUS
struct bus_type sbus_bus_type = {
       .name	= "sbus",
       .match	= of_platform_bus_match,
       .probe	= of_device_probe,
       .remove	= of_device_remove,
       .suspend	= of_device_suspend,
       .resume	= of_device_resume,
};
EXPORT_SYMBOL(sbus_bus_type);
#endif

struct bus_type of_bus_type = {
       .name	= "of",
       .match	= of_platform_bus_match,
       .probe	= of_device_probe,
       .remove	= of_device_remove,
       .suspend	= of_device_suspend,
       .resume	= of_device_resume,
};
EXPORT_SYMBOL(of_bus_type);

static inline u64 of_read_addr(const u32 *cell, int size)
{
	u64 r = 0;
	while (size--)
		r = (r << 32) | *(cell++);
	return r;
}

static void __init get_cells(struct device_node *dp,
			     int *addrc, int *sizec)
{
	if (addrc)
		*addrc = of_n_addr_cells(dp);
	if (sizec)
		*sizec = of_n_size_cells(dp);
}

/* Max address size we deal with */
#define OF_MAX_ADDR_CELLS	4

struct of_bus {
	const char	*name;
	const char	*addr_prop_name;
	int		(*match)(struct device_node *parent);
	void		(*count_cells)(struct device_node *child,
				       int *addrc, int *sizec);
	int		(*map)(u32 *addr, const u32 *range,
			       int na, int ns, int pna);
	unsigned int	(*get_flags)(u32 *addr);
};

/*
 * Default translator (generic bus)
 */

static void of_bus_default_count_cells(struct device_node *dev,
				       int *addrc, int *sizec)
{
	get_cells(dev, addrc, sizec);
}

/* Make sure the least significant 64-bits are in-range.  Even
 * for 3 or 4 cell values it is a good enough approximation.
 */
static int of_out_of_range(const u32 *addr, const u32 *base,
			   const u32 *size, int na, int ns)
{
	u64 a = of_read_addr(addr, na);
	u64 b = of_read_addr(base, na);

	if (a < b)
		return 1;

	b += of_read_addr(size, ns);
	if (a >= b)
		return 1;

	return 0;
}

static int of_bus_default_map(u32 *addr, const u32 *range,
			      int na, int ns, int pna)
{
	u32 result[OF_MAX_ADDR_CELLS];
	int i;

	if (ns > 2) {
		printk("of_device: Cannot handle size cells (%d) > 2.", ns);
		return -EINVAL;
	}

	if (of_out_of_range(addr, range, range + na + pna, na, ns))
		return -EINVAL;

	/* Start with the parent range base.  */
	memcpy(result, range + na, pna * 4);

	/* Add in the child address offset.  */
	for (i = 0; i < na; i++)
		result[pna - 1 - i] +=
			(addr[na - 1 - i] -
			 range[na - 1 - i]);

	memcpy(addr, result, pna * 4);

	return 0;
}

static unsigned int of_bus_default_get_flags(u32 *addr)
{
	return IORESOURCE_MEM;
}

/*
 * PCI bus specific translator
 */

static int of_bus_pci_match(struct device_node *np)
{
	if (!strcmp(np->type, "pci") || !strcmp(np->type, "pciex")) {
		/* Do not do PCI specific frobbing if the
		 * PCI bridge lacks a ranges property.  We
		 * want to pass it through up to the next
		 * parent as-is, not with the PCI translate
		 * method which chops off the top address cell.
		 */
		if (!of_find_property(np, "ranges", NULL))
			return 0;

		return 1;
	}

	return 0;
}

static void of_bus_pci_count_cells(struct device_node *np,
				   int *addrc, int *sizec)
{
	if (addrc)
		*addrc = 3;
	if (sizec)
		*sizec = 2;
}

static int of_bus_pci_map(u32 *addr, const u32 *range,
			  int na, int ns, int pna)
{
	u32 result[OF_MAX_ADDR_CELLS];
	int i;

	/* Check address type match */
	if ((addr[0] ^ range[0]) & 0x03000000)
		return -EINVAL;

	if (of_out_of_range(addr + 1, range + 1, range + na + pna,
			    na - 1, ns))
		return -EINVAL;

	/* Start with the parent range base.  */
	memcpy(result, range + na, pna * 4);

	/* Add in the child address offset, skipping high cell.  */
	for (i = 0; i < na - 1; i++)
		result[pna - 1 - i] +=
			(addr[na - 1 - i] -
			 range[na - 1 - i]);

	memcpy(addr, result, pna * 4);

	return 0;
}

static unsigned int of_bus_pci_get_flags(u32 *addr)
{
	unsigned int flags = 0;
	u32 w = addr[0];

	switch((w >> 24) & 0x03) {
	case 0x01:
		flags |= IORESOURCE_IO;
	case 0x02: /* 32 bits */
	case 0x03: /* 64 bits */
		flags |= IORESOURCE_MEM;
	}
	if (w & 0x40000000)
		flags |= IORESOURCE_PREFETCH;
	return flags;
}

/*
 * SBUS bus specific translator
 */

static int of_bus_sbus_match(struct device_node *np)
{
	return !strcmp(np->name, "sbus") ||
		!strcmp(np->name, "sbi");
}

static void of_bus_sbus_count_cells(struct device_node *child,
				   int *addrc, int *sizec)
{
	if (addrc)
		*addrc = 2;
	if (sizec)
		*sizec = 1;
}

/*
 * FHC/Central bus specific translator.
 *
 * This is just needed to hard-code the address and size cell
 * counts.  'fhc' and 'central' nodes lack the #address-cells and
 * #size-cells properties, and if you walk to the root on such
 * Enterprise boxes all you'll get is a #size-cells of 2 which is
 * not what we want to use.
 */
static int of_bus_fhc_match(struct device_node *np)
{
	return !strcmp(np->name, "fhc") ||
		!strcmp(np->name, "central");
}

#define of_bus_fhc_count_cells of_bus_sbus_count_cells

/*
 * Array of bus specific translators
 */

static struct of_bus of_busses[] = {
	/* PCI */
	{
		.name = "pci",
		.addr_prop_name = "assigned-addresses",
		.match = of_bus_pci_match,
		.count_cells = of_bus_pci_count_cells,
		.map = of_bus_pci_map,
		.get_flags = of_bus_pci_get_flags,
	},
	/* SBUS */
	{
		.name = "sbus",
		.addr_prop_name = "reg",
		.match = of_bus_sbus_match,
		.count_cells = of_bus_sbus_count_cells,
		.map = of_bus_default_map,
		.get_flags = of_bus_default_get_flags,
	},
	/* FHC */
	{
		.name = "fhc",
		.addr_prop_name = "reg",
		.match = of_bus_fhc_match,
		.count_cells = of_bus_fhc_count_cells,
		.map = of_bus_default_map,
		.get_flags = of_bus_default_get_flags,
	},
	/* Default */
	{
		.name = "default",
		.addr_prop_name = "reg",
		.match = NULL,
		.count_cells = of_bus_default_count_cells,
		.map = of_bus_default_map,
		.get_flags = of_bus_default_get_flags,
	},
};

static struct of_bus *of_match_bus(struct device_node *np)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(of_busses); i ++)
		if (!of_busses[i].match || of_busses[i].match(np))
			return &of_busses[i];
	BUG();
	return NULL;
}

static int __init build_one_resource(struct device_node *parent,
				     struct of_bus *bus,
				     struct of_bus *pbus,
				     u32 *addr,
				     int na, int ns, int pna)
{
	u32 *ranges;
	unsigned int rlen;
	int rone;

	ranges = of_get_property(parent, "ranges", &rlen);
	if (ranges == NULL || rlen == 0) {
		u32 result[OF_MAX_ADDR_CELLS];
		int i;

		memset(result, 0, pna * 4);
		for (i = 0; i < na; i++)
			result[pna - 1 - i] =
				addr[na - 1 - i];

		memcpy(addr, result, pna * 4);
		return 0;
	}

	/* Now walk through the ranges */
	rlen /= 4;
	rone = na + pna + ns;
	for (; rlen >= rone; rlen -= rone, ranges += rone) {
		if (!bus->map(addr, ranges, na, ns, pna))
			return 0;
	}

	return 1;
}

static int __init use_1to1_mapping(struct device_node *pp)
{
	char *model;

	/* If this is on the PMU bus, don't try to translate it even
	 * if a ranges property exists.
	 */
	if (!strcmp(pp->name, "pmu"))
		return 1;

	/* If we have a ranges property in the parent, use it.  */
	if (of_find_property(pp, "ranges", NULL) != NULL)
		return 0;

	/* If the parent is the dma node of an ISA bus, pass
	 * the translation up to the root.
	 */
	if (!strcmp(pp->name, "dma"))
		return 0;

	/* Similarly for Simba PCI bridges.  */
	model = of_get_property(pp, "model", NULL);
	if (model && !strcmp(model, "SUNW,simba"))
		return 0;

	return 1;
}

static int of_resource_verbose;

static void __init build_device_resources(struct of_device *op,
					  struct device *parent)
{
	struct of_device *p_op;
	struct of_bus *bus;
	int na, ns;
	int index, num_reg;
	void *preg;

	if (!parent)
		return;

	p_op = to_of_device(parent);
	bus = of_match_bus(p_op->node);
	bus->count_cells(op->node, &na, &ns);

	preg = of_get_property(op->node, bus->addr_prop_name, &num_reg);
	if (!preg || num_reg == 0)
		return;

	/* Convert to num-cells.  */
	num_reg /= 4;

	/* Convert to num-entries.  */
	num_reg /= na + ns;

	/* Prevent overruning the op->resources[] array.  */
	if (num_reg > PROMREG_MAX) {
		printk(KERN_WARNING "%s: Too many regs (%d), "
		       "limiting to %d.\n",
		       op->node->full_name, num_reg, PROMREG_MAX);
		num_reg = PROMREG_MAX;
	}

	for (index = 0; index < num_reg; index++) {
		struct resource *r = &op->resource[index];
		u32 addr[OF_MAX_ADDR_CELLS];
		u32 *reg = (preg + (index * ((na + ns) * 4)));
		struct device_node *dp = op->node;
		struct device_node *pp = p_op->node;
		struct of_bus *pbus;
		u64 size, result = OF_BAD_ADDR;
		unsigned long flags;
		int dna, dns;
		int pna, pns;

		size = of_read_addr(reg + na, ns);
		flags = bus->get_flags(reg);

		memcpy(addr, reg, na * 4);

		if (use_1to1_mapping(pp)) {
			result = of_read_addr(addr, na);
			goto build_res;
		}

		dna = na;
		dns = ns;

		while (1) {
			dp = pp;
			pp = dp->parent;
			if (!pp) {
				result = of_read_addr(addr, dna);
				break;
			}

			pbus = of_match_bus(pp);
			pbus->count_cells(dp, &pna, &pns);

			if (build_one_resource(dp, bus, pbus, addr,
					       dna, dns, pna))
				break;

			dna = pna;
			dns = pns;
			bus = pbus;
		}

	build_res:
		memset(r, 0, sizeof(*r));

		if (of_resource_verbose)
			printk("%s reg[%d] -> %lx\n",
			       op->node->full_name, index,
			       result);

		if (result != OF_BAD_ADDR) {
			if (tlb_type == hypervisor)
				result &= 0x0fffffffffffffffUL;

			r->start = result;
			r->end = result + size - 1;
			r->flags = flags;
		} else {
			r->start = ~0UL;
			r->end = ~0UL;
		}
		r->name = op->node->name;
	}
}

static struct device_node * __init
apply_interrupt_map(struct device_node *dp, struct device_node *pp,
		    u32 *imap, int imlen, u32 *imask,
		    unsigned int *irq_p)
{
	struct device_node *cp;
	unsigned int irq = *irq_p;
	struct of_bus *bus;
	phandle handle;
	u32 *reg;
	int na, num_reg, i;

	bus = of_match_bus(pp);
	bus->count_cells(dp, &na, NULL);

	reg = of_get_property(dp, "reg", &num_reg);
	if (!reg || !num_reg)
		return NULL;

	imlen /= ((na + 3) * 4);
	handle = 0;
	for (i = 0; i < imlen; i++) {
		int j;

		for (j = 0; j < na; j++) {
			if ((reg[j] & imask[j]) != imap[j])
				goto next;
		}
		if (imap[na] == irq) {
			handle = imap[na + 1];
			irq = imap[na + 2];
			break;
		}

	next:
		imap += (na + 3);
	}
	if (i == imlen) {
		/* Psycho and Sabre PCI controllers can have 'interrupt-map'
		 * properties that do not include the on-board device
		 * interrupts.  Instead, the device's 'interrupts' property
		 * is already a fully specified INO value.
		 *
		 * Handle this by deciding that, if we didn't get a
		 * match in the parent's 'interrupt-map', and the
		 * parent is an IRQ translater, then use the parent as
		 * our IRQ controller.
		 */
		if (pp->irq_trans)
			return pp;

		return NULL;
	}

	*irq_p = irq;
	cp = of_find_node_by_phandle(handle);

	return cp;
}

static unsigned int __init pci_irq_swizzle(struct device_node *dp,
					   struct device_node *pp,
					   unsigned int irq)
{
	struct linux_prom_pci_registers *regs;
	unsigned int devfn, slot, ret;

	if (irq < 1 || irq > 4)
		return irq;

	regs = of_get_property(dp, "reg", NULL);
	if (!regs)
		return irq;

	devfn = (regs->phys_hi >> 8) & 0xff;
	slot = (devfn >> 3) & 0x1f;

	ret = ((irq - 1 + (slot & 3)) & 3) + 1;

	return ret;
}

static int of_irq_verbose;

static unsigned int __init build_one_device_irq(struct of_device *op,
						struct device *parent,
						unsigned int irq)
{
	struct device_node *dp = op->node;
	struct device_node *pp, *ip;
	unsigned int orig_irq = irq;

	if (irq == 0xffffffff)
		return irq;

	if (dp->irq_trans) {
		irq = dp->irq_trans->irq_build(dp, irq,
					       dp->irq_trans->data);

		if (of_irq_verbose)
			printk("%s: direct translate %x --> %x\n",
			       dp->full_name, orig_irq, irq);

		return irq;
	}

	/* Something more complicated.  Walk up to the root, applying
	 * interrupt-map or bus specific translations, until we hit
	 * an IRQ translator.
	 *
	 * If we hit a bus type or situation we cannot handle, we
	 * stop and assume that the original IRQ number was in a
	 * format which has special meaning to it's immediate parent.
	 */
	pp = dp->parent;
	ip = NULL;
	while (pp) {
		void *imap, *imsk;
		int imlen;

		imap = of_get_property(pp, "interrupt-map", &imlen);
		imsk = of_get_property(pp, "interrupt-map-mask", NULL);
		if (imap && imsk) {
			struct device_node *iret;
			int this_orig_irq = irq;

			iret = apply_interrupt_map(dp, pp,
						   imap, imlen, imsk,
						   &irq);

			if (of_irq_verbose)
				printk("%s: Apply [%s:%x] imap --> [%s:%x]\n",
				       op->node->full_name,
				       pp->full_name, this_orig_irq,
				       (iret ? iret->full_name : "NULL"), irq);

			if (!iret)
				break;

			if (iret->irq_trans) {
				ip = iret;
				break;
			}
		} else {
			if (!strcmp(pp->type, "pci") ||
			    !strcmp(pp->type, "pciex")) {
				unsigned int this_orig_irq = irq;

				irq = pci_irq_swizzle(dp, pp, irq);
				if (of_irq_verbose)
					printk("%s: PCI swizzle [%s] "
					       "%x --> %x\n",
					       op->node->full_name,
					       pp->full_name, this_orig_irq,
					       irq);

			}

			if (pp->irq_trans) {
				ip = pp;
				break;
			}
		}
		dp = pp;
		pp = pp->parent;
	}
	if (!ip)
		return orig_irq;

	irq = ip->irq_trans->irq_build(op->node, irq,
				       ip->irq_trans->data);
	if (of_irq_verbose)
		printk("%s: Apply IRQ trans [%s] %x --> %x\n",
		       op->node->full_name, ip->full_name, orig_irq, irq);

	return irq;
}

static struct of_device * __init scan_one_device(struct device_node *dp,
						 struct device *parent)
{
	struct of_device *op = kzalloc(sizeof(*op), GFP_KERNEL);
	unsigned int *irq;
	int len, i;

	if (!op)
		return NULL;

	op->node = dp;

	op->clock_freq = of_getintprop_default(dp, "clock-frequency",
					       (25*1000*1000));
	op->portid = of_getintprop_default(dp, "upa-portid", -1);
	if (op->portid == -1)
		op->portid = of_getintprop_default(dp, "portid", -1);

	irq = of_get_property(dp, "interrupts", &len);
	if (irq) {
		memcpy(op->irqs, irq, len);
		op->num_irqs = len / 4;
	} else {
		op->num_irqs = 0;
	}

	/* Prevent overruning the op->irqs[] array.  */
	if (op->num_irqs > PROMINTR_MAX) {
		printk(KERN_WARNING "%s: Too many irqs (%d), "
		       "limiting to %d.\n",
		       dp->full_name, op->num_irqs, PROMINTR_MAX);
		op->num_irqs = PROMINTR_MAX;
	}

	build_device_resources(op, parent);
	for (i = 0; i < op->num_irqs; i++)
		op->irqs[i] = build_one_device_irq(op, parent, op->irqs[i]);

	op->dev.parent = parent;
	op->dev.bus = &of_bus_type;
	if (!parent)
		strcpy(op->dev.bus_id, "root");
	else
		sprintf(op->dev.bus_id, "%08x", dp->node);

	if (of_device_register(op)) {
		printk("%s: Could not register of device.\n",
		       dp->full_name);
		kfree(op);
		op = NULL;
	}

	return op;
}

static void __init scan_tree(struct device_node *dp, struct device *parent)
{
	while (dp) {
		struct of_device *op = scan_one_device(dp, parent);

		if (op)
			scan_tree(dp->child, &op->dev);

		dp = dp->sibling;
	}
}

static void __init scan_of_devices(void)
{
	struct device_node *root = of_find_node_by_path("/");
	struct of_device *parent;

	parent = scan_one_device(root, NULL);
	if (!parent)
		return;

	scan_tree(root->child, &parent->dev);
}

static int __init of_bus_driver_init(void)
{
	int err;

	err = bus_register(&of_bus_type);
#ifdef CONFIG_PCI
	if (!err)
		err = bus_register(&isa_bus_type);
	if (!err)
		err = bus_register(&ebus_bus_type);
#endif
#ifdef CONFIG_SBUS
	if (!err)
		err = bus_register(&sbus_bus_type);
#endif

	if (!err)
		scan_of_devices();

	return err;
}

postcore_initcall(of_bus_driver_init);

static int __init of_debug(char *str)
{
	int val = 0;

	get_option(&str, &val);
	if (val & 1)
		of_resource_verbose = 1;
	if (val & 2)
		of_irq_verbose = 1;
	return 1;
}

__setup("of_debug=", of_debug);

int of_register_driver(struct of_platform_driver *drv, struct bus_type *bus)
{
	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = bus;

	/* register with core */
	return driver_register(&drv->driver);
}

void of_unregister_driver(struct of_platform_driver *drv)
{
	driver_unregister(&drv->driver);
}


static ssize_t dev_show_devspec(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct of_device *ofdev;

	ofdev = to_of_device(dev);
	return sprintf(buf, "%s", ofdev->node->full_name);
}

static DEVICE_ATTR(devspec, S_IRUGO, dev_show_devspec, NULL);

/**
 * of_release_dev - free an of device structure when all users of it are finished.
 * @dev: device that's been disconnected
 *
 * Will be called only by the device core when all users of this of device are
 * done.
 */
void of_release_dev(struct device *dev)
{
	struct of_device *ofdev;

        ofdev = to_of_device(dev);

	kfree(ofdev);
}

int of_device_register(struct of_device *ofdev)
{
	int rc;

	BUG_ON(ofdev->node == NULL);

	rc = device_register(&ofdev->dev);
	if (rc)
		return rc;

	rc = device_create_file(&ofdev->dev, &dev_attr_devspec);
	if (rc)
		device_unregister(&ofdev->dev);

	return rc;
}

void of_device_unregister(struct of_device *ofdev)
{
	device_remove_file(&ofdev->dev, &dev_attr_devspec);
	device_unregister(&ofdev->dev);
}

struct of_device* of_platform_device_create(struct device_node *np,
					    const char *bus_id,
					    struct device *parent,
					    struct bus_type *bus)
{
	struct of_device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->dev.parent = parent;
	dev->dev.bus = bus;
	dev->dev.release = of_release_dev;

	strlcpy(dev->dev.bus_id, bus_id, BUS_ID_SIZE);

	if (of_device_register(dev) != 0) {
		kfree(dev);
		return NULL;
	}

	return dev;
}

EXPORT_SYMBOL(of_match_device);
EXPORT_SYMBOL(of_register_driver);
EXPORT_SYMBOL(of_unregister_driver);
EXPORT_SYMBOL(of_device_register);
EXPORT_SYMBOL(of_device_unregister);
EXPORT_SYMBOL(of_dev_get);
EXPORT_SYMBOL(of_dev_put);
EXPORT_SYMBOL(of_platform_device_create);
EXPORT_SYMBOL(of_release_dev);
