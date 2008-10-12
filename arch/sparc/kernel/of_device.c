#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

static int node_match(struct device *dev, void *data)
{
	struct of_device *op = to_of_device(dev);
	struct device_node *dp = data;

	return (op->node == dp);
}

struct of_device *of_find_device_by_node(struct device_node *dp)
{
	struct device *dev = bus_find_device(&of_platform_bus_type, NULL,
					     dp, node_match);

	if (dev)
		return to_of_device(dev);

	return NULL;
}
EXPORT_SYMBOL(of_find_device_by_node);

#ifdef CONFIG_PCI
struct bus_type ebus_bus_type;
EXPORT_SYMBOL(ebus_bus_type);
#endif

#ifdef CONFIG_SBUS
struct bus_type sbus_bus_type;
EXPORT_SYMBOL(sbus_bus_type);
#endif

struct bus_type of_platform_bus_type;
EXPORT_SYMBOL(of_platform_bus_type);

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
	unsigned long	(*get_flags)(const u32 *addr, unsigned long);
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

static unsigned long of_bus_default_get_flags(const u32 *addr, unsigned long flags)
{
	if (flags)
		return flags;
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

static unsigned long of_bus_pci_get_flags(const u32 *addr, unsigned long flags)
{
	u32 w = addr[0];

	/* For PCI, we override whatever child busses may have used.  */
	flags = 0;
	switch((w >> 24) & 0x03) {
	case 0x01:
		flags |= IORESOURCE_IO;
		break;

	case 0x02: /* 32 bits */
	case 0x03: /* 64 bits */
		flags |= IORESOURCE_MEM;
		break;
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

static int of_bus_sbus_map(u32 *addr, const u32 *range, int na, int ns, int pna)
{
	return of_bus_default_map(addr, range, na, ns, pna);
}

static unsigned long of_bus_sbus_get_flags(const u32 *addr, unsigned long flags)
{
	return IORESOURCE_MEM;
}


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
		.map = of_bus_sbus_map,
		.get_flags = of_bus_sbus_get_flags,
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
	const u32 *ranges;
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

static int of_resource_verbose;

static void __init build_device_resources(struct of_device *op,
					  struct device *parent)
{
	struct of_device *p_op;
	struct of_bus *bus;
	int na, ns;
	int index, num_reg;
	const void *preg;

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

	/* Conver to num-entries.  */
	num_reg /= na + ns;

	for (index = 0; index < num_reg; index++) {
		struct resource *r = &op->resource[index];
		u32 addr[OF_MAX_ADDR_CELLS];
		const u32 *reg = (preg + (index * ((na + ns) * 4)));
		struct device_node *dp = op->node;
		struct device_node *pp = p_op->node;
		struct of_bus *pbus, *dbus;
		u64 size, result = OF_BAD_ADDR;
		unsigned long flags;
		int dna, dns;
		int pna, pns;

		size = of_read_addr(reg + na, ns);

		memcpy(addr, reg, na * 4);

		flags = bus->get_flags(reg, 0);

		/* If the immediate parent has no ranges property to apply,
		 * just use a 1<->1 mapping.
		 */
		if (of_find_property(pp, "ranges", NULL) == NULL) {
			result = of_read_addr(addr, na);
			goto build_res;
		}

		dna = na;
		dns = ns;
		dbus = bus;

		while (1) {
			dp = pp;
			pp = dp->parent;
			if (!pp) {
				result = of_read_addr(addr, dna);
				break;
			}

			pbus = of_match_bus(pp);
			pbus->count_cells(dp, &pna, &pns);

			if (build_one_resource(dp, dbus, pbus, addr,
					       dna, dns, pna))
				break;

			flags = pbus->get_flags(addr, flags);

			dna = pna;
			dns = pns;
			dbus = pbus;
		}

	build_res:
		memset(r, 0, sizeof(*r));

		if (of_resource_verbose)
			printk("%s reg[%d] -> %llx\n",
			       op->node->full_name, index,
			       result);

		if (result != OF_BAD_ADDR) {
			r->start = result & 0xffffffff;
			r->end = result + size - 1;
			r->flags = flags | ((result >> 32ULL) & 0xffUL);
		}
		r->name = op->node->name;
	}
}

static struct of_device * __init scan_one_device(struct device_node *dp,
						 struct device *parent)
{
	struct of_device *op = kzalloc(sizeof(*op), GFP_KERNEL);
	const struct linux_prom_irqs *intr;
	struct dev_archdata *sd;
	int len, i;

	if (!op)
		return NULL;

	sd = &op->dev.archdata;
	sd->prom_node = dp;
	sd->op = op;

	op->node = dp;

	op->clock_freq = of_getintprop_default(dp, "clock-frequency",
					       (25*1000*1000));
	op->portid = of_getintprop_default(dp, "upa-portid", -1);
	if (op->portid == -1)
		op->portid = of_getintprop_default(dp, "portid", -1);

	intr = of_get_property(dp, "intr", &len);
	if (intr) {
		op->num_irqs = len / sizeof(struct linux_prom_irqs);
		for (i = 0; i < op->num_irqs; i++)
			op->irqs[i] = intr[i].pri;
	} else {
		const unsigned int *irq =
			of_get_property(dp, "interrupts", &len);

		if (irq) {
			op->num_irqs = len / sizeof(unsigned int);
			for (i = 0; i < op->num_irqs; i++)
				op->irqs[i] = irq[i];
		} else {
			op->num_irqs = 0;
		}
	}
	if (sparc_cpu_model == sun4d) {
		static int pil_to_sbus[] = {
			0, 0, 1, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 0,
		};
		struct device_node *io_unit, *sbi = dp->parent;
		const struct linux_prom_registers *regs;
		int board, slot;

		while (sbi) {
			if (!strcmp(sbi->name, "sbi"))
				break;

			sbi = sbi->parent;
		}
		if (!sbi)
			goto build_resources;

		regs = of_get_property(dp, "reg", NULL);
		if (!regs)
			goto build_resources;

		slot = regs->which_io;

		/* If SBI's parent is not io-unit or the io-unit lacks
		 * a "board#" property, something is very wrong.
		 */
		if (!sbi->parent || strcmp(sbi->parent->name, "io-unit")) {
			printk("%s: Error, parent is not io-unit.\n",
			       sbi->full_name);
			goto build_resources;
		}
		io_unit = sbi->parent;
		board = of_getintprop_default(io_unit, "board#", -1);
		if (board == -1) {
			printk("%s: Error, lacks board# property.\n",
			       io_unit->full_name);
			goto build_resources;
		}

		for (i = 0; i < op->num_irqs; i++) {
			int this_irq = op->irqs[i];
			int sbusl = pil_to_sbus[this_irq];

			if (sbusl)
				this_irq = (((board + 1) << 5) +
					    (sbusl << 2) +
					    slot);

			op->irqs[i] = this_irq;
		}
	}

build_resources:
	build_device_resources(op, parent);

	op->dev.parent = parent;
	op->dev.bus = &of_platform_bus_type;
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

	err = of_bus_type_init(&of_platform_bus_type, "of");
#ifdef CONFIG_PCI
	if (!err)
		err = of_bus_type_init(&ebus_bus_type, "ebus");
#endif
#ifdef CONFIG_SBUS
	if (!err)
		err = of_bus_type_init(&sbus_bus_type, "sbus");
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
	return 1;
}

__setup("of_debug=", of_debug);
