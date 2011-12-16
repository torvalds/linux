#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <asm/leon.h>
#include <asm/leon_amba.h>

#include "of_device_common.h"
#include "irq.h"

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

static unsigned long of_bus_sbus_get_flags(const u32 *addr, unsigned long flags)
{
	return IORESOURCE_MEM;
}

 /*
 * AMBAPP bus specific translator
 */

static int of_bus_ambapp_match(struct device_node *np)
{
	return !strcmp(np->type, "ambapp");
}

static void of_bus_ambapp_count_cells(struct device_node *child,
				      int *addrc, int *sizec)
{
	if (addrc)
		*addrc = 1;
	if (sizec)
		*sizec = 1;
}

static int of_bus_ambapp_map(u32 *addr, const u32 *range,
			     int na, int ns, int pna)
{
	return of_bus_default_map(addr, range, na, ns, pna);
}

static unsigned long of_bus_ambapp_get_flags(const u32 *addr,
					     unsigned long flags)
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
		.map = of_bus_default_map,
		.get_flags = of_bus_sbus_get_flags,
	},
	/* AMBA */
	{
		.name = "ambapp",
		.addr_prop_name = "reg",
		.match = of_bus_ambapp_match,
		.count_cells = of_bus_ambapp_count_cells,
		.map = of_bus_ambapp_map,
		.get_flags = of_bus_ambapp_get_flags,
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

static int __init use_1to1_mapping(struct device_node *pp)
{
	/* If we have a ranges property in the parent, use it.  */
	if (of_find_property(pp, "ranges", NULL) != NULL)
		return 0;

	/* Some SBUS devices use intermediate nodes to express
	 * hierarchy within the device itself.  These aren't
	 * real bus nodes, and don't have a 'ranges' property.
	 * But, we should still pass the translation work up
	 * to the SBUS itself.
	 */
	if (!strcmp(pp->name, "dma") ||
	    !strcmp(pp->name, "espdma") ||
	    !strcmp(pp->name, "ledma") ||
	    !strcmp(pp->name, "lebuffer"))
		return 0;

	return 1;
}

static int of_resource_verbose;

static void __init build_device_resources(struct platform_device *op,
					  struct device *parent)
{
	struct platform_device *p_op;
	struct of_bus *bus;
	int na, ns;
	int index, num_reg;
	const void *preg;

	if (!parent)
		return;

	p_op = to_platform_device(parent);
	bus = of_match_bus(p_op->dev.of_node);
	bus->count_cells(op->dev.of_node, &na, &ns);

	preg = of_get_property(op->dev.of_node, bus->addr_prop_name, &num_reg);
	if (!preg || num_reg == 0)
		return;

	/* Convert to num-cells.  */
	num_reg /= 4;

	/* Conver to num-entries.  */
	num_reg /= na + ns;

	op->resource = op->archdata.resource;
	op->num_resources = num_reg;
	for (index = 0; index < num_reg; index++) {
		struct resource *r = &op->resource[index];
		u32 addr[OF_MAX_ADDR_CELLS];
		const u32 *reg = (preg + (index * ((na + ns) * 4)));
		struct device_node *dp = op->dev.of_node;
		struct device_node *pp = p_op->dev.of_node;
		struct of_bus *pbus, *dbus;
		u64 size, result = OF_BAD_ADDR;
		unsigned long flags;
		int dna, dns;
		int pna, pns;

		size = of_read_addr(reg + na, ns);

		memcpy(addr, reg, na * 4);

		flags = bus->get_flags(reg, 0);

		if (use_1to1_mapping(pp)) {
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
			       op->dev.of_node->full_name, index,
			       result);

		if (result != OF_BAD_ADDR) {
			r->start = result & 0xffffffff;
			r->end = result + size - 1;
			r->flags = flags | ((result >> 32ULL) & 0xffUL);
		}
		r->name = op->dev.of_node->name;
	}
}

static struct platform_device * __init scan_one_device(struct device_node *dp,
						 struct device *parent)
{
	struct platform_device *op = kzalloc(sizeof(*op), GFP_KERNEL);
	const struct linux_prom_irqs *intr;
	struct dev_archdata *sd;
	int len, i;

	if (!op)
		return NULL;

	sd = &op->dev.archdata;
	sd->op = op;

	op->dev.of_node = dp;

	intr = of_get_property(dp, "intr", &len);
	if (intr) {
		op->archdata.num_irqs = len / sizeof(struct linux_prom_irqs);
		for (i = 0; i < op->archdata.num_irqs; i++)
			op->archdata.irqs[i] =
			    sparc_irq_config.build_device_irq(op, intr[i].pri);
	} else {
		const unsigned int *irq =
			of_get_property(dp, "interrupts", &len);

		if (irq) {
			op->archdata.num_irqs = len / sizeof(unsigned int);
			for (i = 0; i < op->archdata.num_irqs; i++)
				op->archdata.irqs[i] =
				    sparc_irq_config.build_device_irq(op, irq[i]);
		} else {
			op->archdata.num_irqs = 0;
		}
	}

	build_device_resources(op, parent);

	op->dev.parent = parent;
	op->dev.bus = &platform_bus_type;
	if (!parent)
		dev_set_name(&op->dev, "root");
	else
		dev_set_name(&op->dev, "%08x", dp->phandle);

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
		struct platform_device *op = scan_one_device(dp, parent);

		if (op)
			scan_tree(dp->child, &op->dev);

		dp = dp->sibling;
	}
}

static int __init scan_of_devices(void)
{
	struct device_node *root = of_find_node_by_path("/");
	struct platform_device *parent;

	parent = scan_one_device(root, NULL);
	if (!parent)
		return 0;

	scan_tree(root->child, &parent->dev);
	return 0;
}
postcore_initcall(scan_of_devices);

static int __init of_debug(char *str)
{
	int val = 0;

	get_option(&str, &val);
	if (val & 1)
		of_resource_verbose = 1;
	return 1;
}

__setup("of_debug=", of_debug);
