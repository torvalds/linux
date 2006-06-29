#include <linux/config.h>
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

static inline u64 of_read_addr(u32 *cell, int size)
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
	u64		(*map)(u32 *addr, u32 *range, int na, int ns, int pna);
	int		(*translate)(u32 *addr, u64 offset, int na);
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

static u64 of_bus_default_map(u32 *addr, u32 *range, int na, int ns, int pna)
{
	u64 cp, s, da;

	cp = of_read_addr(range, na);
	s  = of_read_addr(range + na + pna, ns);
	da = of_read_addr(addr, na);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_default_translate(u32 *addr, u64 offset, int na)
{
	u64 a = of_read_addr(addr, na);
	memset(addr, 0, na * 4);
	a += offset;
	if (na > 1)
		addr[na - 2] = a >> 32;
	addr[na - 1] = a & 0xffffffffu;

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
	return !strcmp(np->type, "pci") || !strcmp(np->type, "pciex");
}

static void of_bus_pci_count_cells(struct device_node *np,
				   int *addrc, int *sizec)
{
	if (addrc)
		*addrc = 3;
	if (sizec)
		*sizec = 2;
}

static u64 of_bus_pci_map(u32 *addr, u32 *range, int na, int ns, int pna)
{
	u64 cp, s, da;

	/* Check address type match */
	if ((addr[0] ^ range[0]) & 0x03000000)
		return OF_BAD_ADDR;

	/* Read address values, skipping high cell */
	cp = of_read_addr(range + 1, na - 1);
	s  = of_read_addr(range + na + pna, ns);
	da = of_read_addr(addr + 1, na - 1);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_pci_translate(u32 *addr, u64 offset, int na)
{
	return of_bus_default_translate(addr + 1, offset, na - 1);
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

static u64 of_bus_sbus_map(u32 *addr, u32 *range, int na, int ns, int pna)
{
	return of_bus_default_map(addr, range, na, ns, pna);
}

static int of_bus_sbus_translate(u32 *addr, u64 offset, int na)
{
	return of_bus_default_translate(addr, offset, na);
}

static unsigned int of_bus_sbus_get_flags(u32 *addr)
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
		.translate = of_bus_pci_translate,
		.get_flags = of_bus_pci_get_flags,
	},
	/* SBUS */
	{
		.name = "sbus",
		.addr_prop_name = "reg",
		.match = of_bus_sbus_match,
		.count_cells = of_bus_sbus_count_cells,
		.map = of_bus_sbus_map,
		.translate = of_bus_sbus_translate,
		.get_flags = of_bus_sbus_get_flags,
	},
	/* Default */
	{
		.name = "default",
		.addr_prop_name = "reg",
		.match = NULL,
		.count_cells = of_bus_default_count_cells,
		.map = of_bus_default_map,
		.translate = of_bus_default_translate,
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
	u64 offset = OF_BAD_ADDR;

	ranges = of_get_property(parent, "ranges", &rlen);
	if (ranges == NULL || rlen == 0) {
		offset = of_read_addr(addr, na);
		memset(addr, 0, pna * 4);
		goto finish;
	}

	/* Now walk through the ranges */
	rlen /= 4;
	rone = na + pna + ns;
	for (; rlen >= rone; rlen -= rone, ranges += rone) {
		offset = bus->map(addr, ranges, na, ns, pna);
		if (offset != OF_BAD_ADDR)
			break;
	}
	if (offset == OF_BAD_ADDR)
		return 1;

	memcpy(addr, ranges + na, 4 * pna);

finish:
	/* Translate it into parent bus space */
	return pbus->translate(addr, offset, pna);
}

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

	/* Conver to num-entries.  */
	num_reg /= na + ns;

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

		/* If the immediate parent has no ranges property to apply,
		 * just use a 1<->1 mapping.
		 */
		if (of_find_property(pp, "ranges", NULL) == NULL) {
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

			if (build_one_resource(dp, bus, pbus, addr, dna, dns, pna))
				break;

			dna = pna;
			dns = pns;
			bus = pbus;
		}

	build_res:
		memset(r, 0, sizeof(*r));
		if (result != OF_BAD_ADDR) {
			r->start = result & 0xffffffff;
			r->end = result + size - 1;
			r->flags = flags | ((result >> 32ULL) & 0xffUL);
		} else {
			r->start = ~0UL;
			r->end = ~0UL;
		}
		r->name = op->node->name;
	}
}

static struct of_device * __init scan_one_device(struct device_node *dp,
						 struct device *parent)
{
	struct of_device *op = kzalloc(sizeof(*op), GFP_KERNEL);
	struct linux_prom_irqs *intr;
	int len, i;

	if (!op)
		return NULL;

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
		unsigned int *irq = of_get_property(dp, "interrupts", &len);

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
		struct device_node *busp = dp->parent;
		struct linux_prom_registers *regs;
		int board = of_getintprop_default(busp, "board#", 0);
		int slot;

		regs = of_get_property(dp, "reg", NULL);
		slot = regs->which_io;

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

	build_device_resources(op, parent);

	op->dev.parent = parent;
	op->dev.bus = &of_bus_type;
	if (!parent)
		strcpy(op->dev.bus_id, "root");
	else
		strcpy(op->dev.bus_id, dp->path_component_name);

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

	device_create_file(&ofdev->dev, &dev_attr_devspec);

	return 0;
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

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;
	memset(dev, 0, sizeof(*dev));

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
