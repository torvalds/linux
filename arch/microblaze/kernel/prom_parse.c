#undef DEBUG

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/pci_regs.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/etherdevice.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

#define PRu64	"%llx"

/* Max address size we deal with */
#define OF_MAX_ADDR_CELLS	4
#define OF_CHECK_COUNTS(na, ns)	((na) > 0 && (na) <= OF_MAX_ADDR_CELLS && \
			(ns) > 0)

static struct of_bus *of_match_bus(struct device_node *np);
static int __of_address_to_resource(struct device_node *dev,
		const u32 *addrp, u64 size, unsigned int flags,
		struct resource *r);

/* Debug utility */
#ifdef DEBUG
static void of_dump_addr(const char *s, const u32 *addr, int na)
{
	printk(KERN_INFO "%s", s);
	while (na--)
		printk(KERN_INFO " %08x", *(addr++));
	printk(KERN_INFO "\n");
}
#else
static void of_dump_addr(const char *s, const u32 *addr, int na) { }
#endif

/* Callbacks for bus specific translators */
struct of_bus {
	const char	*name;
	const char	*addresses;
	int		(*match)(struct device_node *parent);
	void		(*count_cells)(struct device_node *child,
					int *addrc, int *sizec);
	u64		(*map)(u32 *addr, const u32 *range,
				int na, int ns, int pna);
	int		(*translate)(u32 *addr, u64 offset, int na);
	unsigned int	(*get_flags)(const u32 *addr);
};

/*
 * Default translator (generic bus)
 */

static void of_bus_default_count_cells(struct device_node *dev,
					int *addrc, int *sizec)
{
	if (addrc)
		*addrc = of_n_addr_cells(dev);
	if (sizec)
		*sizec = of_n_size_cells(dev);
}

static u64 of_bus_default_map(u32 *addr, const u32 *range,
		int na, int ns, int pna)
{
	u64 cp, s, da;

	cp = of_read_number(range, na);
	s  = of_read_number(range + na + pna, ns);
	da = of_read_number(addr, na);

	pr_debug("OF: default map, cp="PRu64", s="PRu64", da="PRu64"\n",
		cp, s, da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_default_translate(u32 *addr, u64 offset, int na)
{
	u64 a = of_read_number(addr, na);
	memset(addr, 0, na * 4);
	a += offset;
	if (na > 1)
		addr[na - 2] = a >> 32;
	addr[na - 1] = a & 0xffffffffu;

	return 0;
}

static unsigned int of_bus_default_get_flags(const u32 *addr)
{
	return IORESOURCE_MEM;
}

#ifdef CONFIG_PCI
/*
 * PCI bus specific translator
 */

static int of_bus_pci_match(struct device_node *np)
{
	/* "vci" is for the /chaos bridge on 1st-gen PCI powermacs */
	return !strcmp(np->type, "pci") || !strcmp(np->type, "vci");
}

static void of_bus_pci_count_cells(struct device_node *np,
				int *addrc, int *sizec)
{
	if (addrc)
		*addrc = 3;
	if (sizec)
		*sizec = 2;
}

static u64 of_bus_pci_map(u32 *addr, const u32 *range, int na, int ns, int pna)
{
	u64 cp, s, da;

	/* Check address type match */
	if ((addr[0] ^ range[0]) & 0x03000000)
		return OF_BAD_ADDR;

	/* Read address values, skipping high cell */
	cp = of_read_number(range + 1, na - 1);
	s  = of_read_number(range + na + pna, ns);
	da = of_read_number(addr + 1, na - 1);

	pr_debug("OF: PCI map, cp="PRu64", s="PRu64", da="PRu64"\n", cp, s, da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_pci_translate(u32 *addr, u64 offset, int na)
{
	return of_bus_default_translate(addr + 1, offset, na - 1);
}

static unsigned int of_bus_pci_get_flags(const u32 *addr)
{
	unsigned int flags = 0;
	u32 w = addr[0];

	switch ((w >> 24) & 0x03) {
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

const u32 *of_get_pci_address(struct device_node *dev, int bar_no, u64 *size,
			unsigned int *flags)
{
	const u32 *prop;
	unsigned int psize;
	struct device_node *parent;
	struct of_bus *bus;
	int onesize, i, na, ns;

	/* Get parent & match bus type */
	parent = of_get_parent(dev);
	if (parent == NULL)
		return NULL;
	bus = of_match_bus(parent);
	if (strcmp(bus->name, "pci")) {
		of_node_put(parent);
		return NULL;
	}
	bus->count_cells(dev, &na, &ns);
	of_node_put(parent);
	if (!OF_CHECK_COUNTS(na, ns))
		return NULL;

	/* Get "reg" or "assigned-addresses" property */
	prop = of_get_property(dev, bus->addresses, &psize);
	if (prop == NULL)
		return NULL;
	psize /= 4;

	onesize = na + ns;
	for (i = 0; psize >= onesize; psize -= onesize, prop += onesize, i++)
		if ((prop[0] & 0xff) == ((bar_no * 4) + PCI_BASE_ADDRESS_0)) {
			if (size)
				*size = of_read_number(prop + na, ns);
			if (flags)
				*flags = bus->get_flags(prop);
			return prop;
		}
	return NULL;
}
EXPORT_SYMBOL(of_get_pci_address);

int of_pci_address_to_resource(struct device_node *dev, int bar,
				struct resource *r)
{
	const u32	*addrp;
	u64		size;
	unsigned int	flags;

	addrp = of_get_pci_address(dev, bar, &size, &flags);
	if (addrp == NULL)
		return -EINVAL;
	return __of_address_to_resource(dev, addrp, size, flags, r);
}
EXPORT_SYMBOL_GPL(of_pci_address_to_resource);

static u8 of_irq_pci_swizzle(u8 slot, u8 pin)
{
	return (((pin - 1) + slot) % 4) + 1;
}

int of_irq_map_pci(struct pci_dev *pdev, struct of_irq *out_irq)
{
	struct device_node *dn, *ppnode;
	struct pci_dev *ppdev;
	u32 lspec;
	u32 laddr[3];
	u8 pin;
	int rc;

	/* Check if we have a device node, if yes, fallback to standard OF
	 * parsing
	 */
	dn = pci_device_to_OF_node(pdev);
	if (dn)
		return of_irq_map_one(dn, 0, out_irq);

	/* Ok, we don't, time to have fun. Let's start by building up an
	 * interrupt spec.  we assume #interrupt-cells is 1, which is standard
	 * for PCI. If you do different, then don't use that routine.
	 */
	rc = pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &pin);
	if (rc != 0)
		return rc;
	/* No pin, exit */
	if (pin == 0)
		return -ENODEV;

	/* Now we walk up the PCI tree */
	lspec = pin;
	for (;;) {
		/* Get the pci_dev of our parent */
		ppdev = pdev->bus->self;

		/* Ouch, it's a host bridge... */
		if (ppdev == NULL) {
			struct pci_controller *host;
			host = pci_bus_to_host(pdev->bus);
			ppnode = host ? host->arch_data : NULL;
			/* No node for host bridge ? give up */
			if (ppnode == NULL)
				return -EINVAL;
		} else
			/* We found a P2P bridge, check if it has a node */
			ppnode = pci_device_to_OF_node(ppdev);

		/* Ok, we have found a parent with a device-node, hand over to
		 * the OF parsing code.
		 * We build a unit address from the linux device to be used for
		 * resolution. Note that we use the linux bus number which may
		 * not match your firmware bus numbering.
		 * Fortunately, in most cases, interrupt-map-mask doesn't
		 * include the bus number as part of the matching.
		 * You should still be careful about that though if you intend
		 * to rely on this function (you ship  a firmware that doesn't
		 * create device nodes for all PCI devices).
		 */
		if (ppnode)
			break;

		/* We can only get here if we hit a P2P bridge with no node,
		 * let's do standard swizzling and try again
		 */
		lspec = of_irq_pci_swizzle(PCI_SLOT(pdev->devfn), lspec);
		pdev = ppdev;
	}

	laddr[0] = (pdev->bus->number << 16)
		| (pdev->devfn << 8);
	laddr[1]  = laddr[2] = 0;
	return of_irq_map_raw(ppnode, &lspec, 1, laddr, out_irq);
}
EXPORT_SYMBOL_GPL(of_irq_map_pci);
#endif /* CONFIG_PCI */

/*
 * ISA bus specific translator
 */

static int of_bus_isa_match(struct device_node *np)
{
	return !strcmp(np->name, "isa");
}

static void of_bus_isa_count_cells(struct device_node *child,
				int *addrc, int *sizec)
{
	if (addrc)
		*addrc = 2;
	if (sizec)
		*sizec = 1;
}

static u64 of_bus_isa_map(u32 *addr, const u32 *range, int na, int ns, int pna)
{
	u64 cp, s, da;

	/* Check address type match */
	if ((addr[0] ^ range[0]) & 0x00000001)
		return OF_BAD_ADDR;

	/* Read address values, skipping high cell */
	cp = of_read_number(range + 1, na - 1);
	s  = of_read_number(range + na + pna, ns);
	da = of_read_number(addr + 1, na - 1);

	pr_debug("OF: ISA map, cp="PRu64", s="PRu64", da="PRu64"\n", cp, s, da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_isa_translate(u32 *addr, u64 offset, int na)
{
	return of_bus_default_translate(addr + 1, offset, na - 1);
}

static unsigned int of_bus_isa_get_flags(const u32 *addr)
{
	unsigned int flags = 0;
	u32 w = addr[0];

	if (w & 1)
		flags |= IORESOURCE_IO;
	else
		flags |= IORESOURCE_MEM;
	return flags;
}

/*
 * Array of bus specific translators
 */

static struct of_bus of_busses[] = {
#ifdef CONFIG_PCI
	/* PCI */
	{
		.name = "pci",
		.addresses = "assigned-addresses",
		.match = of_bus_pci_match,
		.count_cells = of_bus_pci_count_cells,
		.map = of_bus_pci_map,
		.translate = of_bus_pci_translate,
		.get_flags = of_bus_pci_get_flags,
	},
#endif /* CONFIG_PCI */
	/* ISA */
	{
		.name = "isa",
		.addresses = "reg",
		.match = of_bus_isa_match,
		.count_cells = of_bus_isa_count_cells,
		.map = of_bus_isa_map,
		.translate = of_bus_isa_translate,
		.get_flags = of_bus_isa_get_flags,
	},
	/* Default */
	{
		.name = "default",
		.addresses = "reg",
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

	for (i = 0; i < ARRAY_SIZE(of_busses); i++)
		if (!of_busses[i].match || of_busses[i].match(np))
			return &of_busses[i];
	BUG();
	return NULL;
}

static int of_translate_one(struct device_node *parent, struct of_bus *bus,
			struct of_bus *pbus, u32 *addr,
			int na, int ns, int pna)
{
	const u32 *ranges;
	unsigned int rlen;
	int rone;
	u64 offset = OF_BAD_ADDR;

	/* Normally, an absence of a "ranges" property means we are
	 * crossing a non-translatable boundary, and thus the addresses
	 * below the current not cannot be converted to CPU physical ones.
	 * Unfortunately, while this is very clear in the spec, it's not
	 * what Apple understood, and they do have things like /uni-n or
	 * /ht nodes with no "ranges" property and a lot of perfectly
	 * useable mapped devices below them. Thus we treat the absence of
	 * "ranges" as equivalent to an empty "ranges" property which means
	 * a 1:1 translation at that level. It's up to the caller not to try
	 * to translate addresses that aren't supposed to be translated in
	 * the first place. --BenH.
	 */
	ranges = of_get_property(parent, "ranges", (int *) &rlen);
	if (ranges == NULL || rlen == 0) {
		offset = of_read_number(addr, na);
		memset(addr, 0, pna * 4);
		pr_debug("OF: no ranges, 1:1 translation\n");
		goto finish;
	}

	pr_debug("OF: walking ranges...\n");

	/* Now walk through the ranges */
	rlen /= 4;
	rone = na + pna + ns;
	for (; rlen >= rone; rlen -= rone, ranges += rone) {
		offset = bus->map(addr, ranges, na, ns, pna);
		if (offset != OF_BAD_ADDR)
			break;
	}
	if (offset == OF_BAD_ADDR) {
		pr_debug("OF: not found !\n");
		return 1;
	}
	memcpy(addr, ranges + na, 4 * pna);

 finish:
	of_dump_addr("OF: parent translation for:", addr, pna);
	pr_debug("OF: with offset: "PRu64"\n", offset);

	/* Translate it into parent bus space */
	return pbus->translate(addr, offset, pna);
}

/*
 * Translate an address from the device-tree into a CPU physical address,
 * this walks up the tree and applies the various bus mappings on the
 * way.
 *
 * Note: We consider that crossing any level with #size-cells == 0 to mean
 * that translation is impossible (that is we are not dealing with a value
 * that can be mapped to a cpu physical address). This is not really specified
 * that way, but this is traditionally the way IBM at least do things
 */
u64 of_translate_address(struct device_node *dev, const u32 *in_addr)
{
	struct device_node *parent = NULL;
	struct of_bus *bus, *pbus;
	u32 addr[OF_MAX_ADDR_CELLS];
	int na, ns, pna, pns;
	u64 result = OF_BAD_ADDR;

	pr_debug("OF: ** translation for device %s **\n", dev->full_name);

	/* Increase refcount at current level */
	of_node_get(dev);

	/* Get parent & match bus type */
	parent = of_get_parent(dev);
	if (parent == NULL)
		goto bail;
	bus = of_match_bus(parent);

	/* Cound address cells & copy address locally */
	bus->count_cells(dev, &na, &ns);
	if (!OF_CHECK_COUNTS(na, ns)) {
		printk(KERN_ERR "prom_parse: Bad cell count for %s\n",
			dev->full_name);
		goto bail;
	}
	memcpy(addr, in_addr, na * 4);

	pr_debug("OF: bus is %s (na=%d, ns=%d) on %s\n",
		bus->name, na, ns, parent->full_name);
	of_dump_addr("OF: translating address:", addr, na);

	/* Translate */
	for (;;) {
		/* Switch to parent bus */
		of_node_put(dev);
		dev = parent;
		parent = of_get_parent(dev);

		/* If root, we have finished */
		if (parent == NULL) {
			pr_debug("OF: reached root node\n");
			result = of_read_number(addr, na);
			break;
		}

		/* Get new parent bus and counts */
		pbus = of_match_bus(parent);
		pbus->count_cells(dev, &pna, &pns);
		if (!OF_CHECK_COUNTS(pna, pns)) {
			printk(KERN_ERR "prom_parse: Bad cell count for %s\n",
				dev->full_name);
			break;
		}

		pr_debug("OF: parent bus is %s (na=%d, ns=%d) on %s\n",
			pbus->name, pna, pns, parent->full_name);

		/* Apply bus translation */
		if (of_translate_one(dev, bus, pbus, addr, na, ns, pna))
			break;

		/* Complete the move up one level */
		na = pna;
		ns = pns;
		bus = pbus;

		of_dump_addr("OF: one level translation:", addr, na);
	}
 bail:
	of_node_put(parent);
	of_node_put(dev);

	return result;
}
EXPORT_SYMBOL(of_translate_address);

const u32 *of_get_address(struct device_node *dev, int index, u64 *size,
			unsigned int *flags)
{
	const u32 *prop;
	unsigned int psize;
	struct device_node *parent;
	struct of_bus *bus;
	int onesize, i, na, ns;

	/* Get parent & match bus type */
	parent = of_get_parent(dev);
	if (parent == NULL)
		return NULL;
	bus = of_match_bus(parent);
	bus->count_cells(dev, &na, &ns);
	of_node_put(parent);
	if (!OF_CHECK_COUNTS(na, ns))
		return NULL;

	/* Get "reg" or "assigned-addresses" property */
	prop = of_get_property(dev, bus->addresses, (int *) &psize);
	if (prop == NULL)
		return NULL;
	psize /= 4;

	onesize = na + ns;
	for (i = 0; psize >= onesize; psize -= onesize, prop += onesize, i++)
		if (i == index) {
			if (size)
				*size = of_read_number(prop + na, ns);
			if (flags)
				*flags = bus->get_flags(prop);
			return prop;
		}
	return NULL;
}
EXPORT_SYMBOL(of_get_address);

static int __of_address_to_resource(struct device_node *dev, const u32 *addrp,
				u64 size, unsigned int flags,
				struct resource *r)
{
	u64 taddr;

	if ((flags & (IORESOURCE_IO | IORESOURCE_MEM)) == 0)
		return -EINVAL;
	taddr = of_translate_address(dev, addrp);
	if (taddr == OF_BAD_ADDR)
		return -EINVAL;
	memset(r, 0, sizeof(struct resource));
	if (flags & IORESOURCE_IO) {
		unsigned long port;
		port = -1; /* pci_address_to_pio(taddr); */
		if (port == (unsigned long)-1)
			return -EINVAL;
		r->start = port;
		r->end = port + size - 1;
	} else {
		r->start = taddr;
		r->end = taddr + size - 1;
	}
	r->flags = flags;
	r->name = dev->name;
	return 0;
}

int of_address_to_resource(struct device_node *dev, int index,
			struct resource *r)
{
	const u32	*addrp;
	u64		size;
	unsigned int	flags;

	addrp = of_get_address(dev, index, &size, &flags);
	if (addrp == NULL)
		return -EINVAL;
	return __of_address_to_resource(dev, addrp, size, flags, r);
}
EXPORT_SYMBOL_GPL(of_address_to_resource);

void of_parse_dma_window(struct device_node *dn, const void *dma_window_prop,
		unsigned long *busno, unsigned long *phys, unsigned long *size)
{
	const u32 *dma_window;
	u32 cells;
	const unsigned char *prop;

	dma_window = dma_window_prop;

	/* busno is always one cell */
	*busno = *(dma_window++);

	prop = of_get_property(dn, "ibm,#dma-address-cells", NULL);
	if (!prop)
		prop = of_get_property(dn, "#address-cells", NULL);

	cells = prop ? *(u32 *)prop : of_n_addr_cells(dn);
	*phys = of_read_number(dma_window, cells);

	dma_window += cells;

	prop = of_get_property(dn, "ibm,#dma-size-cells", NULL);
	cells = prop ? *(u32 *)prop : of_n_size_cells(dn);
	*size = of_read_number(dma_window, cells);
}

/*
 * Interrupt remapper
 */

static unsigned int of_irq_workarounds;
static struct device_node *of_irq_dflt_pic;

static struct device_node *of_irq_find_parent(struct device_node *child)
{
	struct device_node *p;
	const phandle *parp;

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
				p = of_find_node_by_phandle(*parp);
		}
		of_node_put(child);
		child = p;
	} while (p && of_get_property(p, "#interrupt-cells", NULL) == NULL);

	return p;
}

/* This doesn't need to be called if you don't have any special workaround
 * flags to pass
 */
void of_irq_map_init(unsigned int flags)
{
	of_irq_workarounds = flags;

	/* OldWorld, don't bother looking at other things */
	if (flags & OF_IMAP_OLDWORLD_MAC)
		return;

	/* If we don't have phandles, let's try to locate a default interrupt
	 * controller (happens when booting with BootX). We do a first match
	 * here, hopefully, that only ever happens on machines with one
	 * controller.
	 */
	if (flags & OF_IMAP_NO_PHANDLE) {
		struct device_node *np;

		for (np = NULL; (np = of_find_all_nodes(np)) != NULL;) {
			if (of_get_property(np, "interrupt-controller", NULL)
				== NULL)
				continue;
			/* Skip /chosen/interrupt-controller */
			if (strcmp(np->name, "chosen") == 0)
				continue;
			/* It seems like at least one person on this planet
			 * wants to use BootX on a machine with an AppleKiwi
			 * controller which happens to pretend to be an
			 * interrupt controller too.
			 */
			if (strcmp(np->name, "AppleKiwi") == 0)
				continue;
			/* I think we found one ! */
			of_irq_dflt_pic = np;
			break;
		}
	}

}

int of_irq_map_raw(struct device_node *parent, const u32 *intspec, u32 ointsize,
		const u32 *addr, struct of_irq *out_irq)
{
	struct device_node *ipar, *tnode, *old = NULL, *newpar = NULL;
	const u32 *tmp, *imap, *imask;
	u32 intsize = 1, addrsize, newintsize = 0, newaddrsize = 0;
	int imaplen, match, i;

	pr_debug("of_irq_map_raw: par=%s,intspec=[0x%08x 0x%08x...],"
		"ointsize=%d\n",
		parent->full_name, intspec[0], intspec[1], ointsize);

	ipar = of_node_get(parent);

	/* First get the #interrupt-cells property of the current cursor
	 * that tells us how to interpret the passed-in intspec. If there
	 * is none, we are nice and just walk up the tree
	 */
	do {
		tmp = of_get_property(ipar, "#interrupt-cells", NULL);
		if (tmp != NULL) {
			intsize = *tmp;
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

	pr_debug("of_irq_map_raw: ipar=%s, size=%d\n",
			ipar->full_name, intsize);

	if (ointsize != intsize)
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
	addrsize = (tmp == NULL) ? 2 : *tmp;

	pr_debug(" -> addrsize=%d\n", addrsize);

	/* Now start the actual "proper" walk of the interrupt tree */
	while (ipar != NULL) {
		/* Now check if cursor is an interrupt-controller and if it is
		 * then we are done
		 */
		if (of_get_property(ipar, "interrupt-controller", NULL) !=
				NULL) {
			pr_debug(" -> got it !\n");
			memcpy(out_irq->specifier, intspec,
				intsize * sizeof(u32));
			out_irq->size = intsize;
			out_irq->controller = ipar;
			of_node_put(old);
			return 0;
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

		/* If we were passed no "reg" property and we attempt to parse
		 * an interrupt-map, then #address-cells must be 0.
		 * Fail if it's not.
		 */
		if (addr == NULL && addrsize != 0) {
			pr_debug(" -> no reg passed in when needed !\n");
			goto fail;
		}

		/* Parse interrupt-map */
		match = 0;
		while (imaplen > (addrsize + intsize + 1) && !match) {
			/* Compare specifiers */
			match = 1;
			for (i = 0; i < addrsize && match; ++i) {
				u32 mask = imask ? imask[i] : 0xffffffffu;
				match = ((addr[i] ^ imap[i]) & mask) == 0;
			}
			for (; i < (addrsize + intsize) && match; ++i) {
				u32 mask = imask ? imask[i] : 0xffffffffu;
				match =
					((intspec[i-addrsize] ^ imap[i])
						& mask) == 0;
			}
			imap += addrsize + intsize;
			imaplen -= addrsize + intsize;

			pr_debug(" -> match=%d (imaplen=%d)\n", match, imaplen);

			/* Get the interrupt parent */
			if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
				newpar = of_node_get(of_irq_dflt_pic);
			else
				newpar =
					of_find_node_by_phandle((phandle)*imap);
			imap++;
			--imaplen;

			/* Check if not found */
			if (newpar == NULL) {
				pr_debug(" -> imap parent not found !\n");
				goto fail;
			}

			/* Get #interrupt-cells and #address-cells of new
			 * parent
			 */
			tmp = of_get_property(newpar, "#interrupt-cells", NULL);
			if (tmp == NULL) {
				pr_debug(" -> parent lacks "
						"#interrupt-cells!\n");
				goto fail;
			}
			newintsize = *tmp;
			tmp = of_get_property(newpar, "#address-cells", NULL);
			newaddrsize = (tmp == NULL) ? 0 : *tmp;

			pr_debug(" -> newintsize=%d, newaddrsize=%d\n",
				newintsize, newaddrsize);

			/* Check for malformed properties */
			if (imaplen < (newaddrsize + newintsize))
				goto fail;

			imap += newaddrsize + newintsize;
			imaplen -= newaddrsize + newintsize;

			pr_debug(" -> imaplen=%d\n", imaplen);
		}
		if (!match)
			goto fail;

		of_node_put(old);
		old = of_node_get(newpar);
		addrsize = newaddrsize;
		intsize = newintsize;
		intspec = imap - intsize;
		addr = intspec - addrsize;

skiplevel:
		/* Iterate again with new parent */
		pr_debug(" -> new parent: %s\n",
				newpar ? newpar->full_name : "<>");
		of_node_put(ipar);
		ipar = newpar;
		newpar = NULL;
	}
fail:
	of_node_put(ipar);
	of_node_put(old);
	of_node_put(newpar);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(of_irq_map_raw);

int of_irq_map_one(struct device_node *device,
			int index, struct of_irq *out_irq)
{
	struct device_node *p;
	const u32 *intspec, *tmp, *addr;
	u32 intsize, intlen;
	int res;

	pr_debug("of_irq_map_one: dev=%s, index=%d\n",
			device->full_name, index);

	/* Get the interrupts property */
	intspec = of_get_property(device, "interrupts", (int *) &intlen);
	if (intspec == NULL)
		return -EINVAL;
	intlen /= sizeof(u32);

	pr_debug(" intspec=%d intlen=%d\n", *intspec, intlen);

	/* Get the reg property (if any) */
	addr = of_get_property(device, "reg", NULL);

	/* Look for the interrupt parent. */
	p = of_irq_find_parent(device);
	if (p == NULL)
		return -EINVAL;

	/* Get size of interrupt specifier */
	tmp = of_get_property(p, "#interrupt-cells", NULL);
	if (tmp == NULL) {
		of_node_put(p);
		return -EINVAL;
	}
	intsize = *tmp;

	pr_debug(" intsize=%d intlen=%d\n", intsize, intlen);

	/* Check index */
	if ((index + 1) * intsize > intlen)
		return -EINVAL;

	/* Get new specifier and map it */
	res = of_irq_map_raw(p, intspec + index * intsize, intsize,
				addr, out_irq);
	of_node_put(p);
	return res;
}
EXPORT_SYMBOL_GPL(of_irq_map_one);

/**
 * Search the device tree for the best MAC address to use.  'mac-address' is
 * checked first, because that is supposed to contain to "most recent" MAC
 * address. If that isn't set, then 'local-mac-address' is checked next,
 * because that is the default address.  If that isn't set, then the obsolete
 * 'address' is checked, just in case we're using an old device tree.
 *
 * Note that the 'address' property is supposed to contain a virtual address of
 * the register set, but some DTS files have redefined that property to be the
 * MAC address.
 *
 * All-zero MAC addresses are rejected, because those could be properties that
 * exist in the device tree, but were not set by U-Boot.  For example, the
 * DTS could define 'mac-address' and 'local-mac-address', with zero MAC
 * addresses.  Some older U-Boots only initialized 'local-mac-address'.  In
 * this case, the real MAC is in 'local-mac-address', and 'mac-address' exists
 * but is all zeros.
*/
const void *of_get_mac_address(struct device_node *np)
{
	struct property *pp;

	pp = of_find_property(np, "mac-address", NULL);
	if (pp && (pp->length == 6) && is_valid_ether_addr(pp->value))
		return pp->value;

	pp = of_find_property(np, "local-mac-address", NULL);
	if (pp && (pp->length == 6) && is_valid_ether_addr(pp->value))
		return pp->value;

	pp = of_find_property(np, "address", NULL);
	if (pp && (pp->length == 6) && is_valid_ether_addr(pp->value))
		return pp->value;

	return NULL;
}
EXPORT_SYMBOL(of_get_mac_address);

int of_irq_to_resource(struct device_node *dev, int index, struct resource *r)
{
	struct of_irq out_irq;
	int irq;
	int res;

	res = of_irq_map_one(dev, index, &out_irq);

	/* Get irq for the device */
	if (res) {
		pr_debug("IRQ not found... code = %d", res);
		return NO_IRQ;
	}
	/* Assuming single interrupt controller... */
	irq = out_irq.specifier[0];

	pr_debug("IRQ found = %d", irq);

	/* Only dereference the resource if both the
	 * resource and the irq are valid. */
	if (r && irq != NO_IRQ) {
		r->start = r->end = irq;
		r->flags = IORESOURCE_IRQ;
	}

	return irq;
}
EXPORT_SYMBOL_GPL(of_irq_to_resource);

void __iomem *of_iomap(struct device_node *np, int index)
{
	struct resource res;

	if (of_address_to_resource(np, index, &res))
		return NULL;

	return ioremap(res.start, 1 + res.end - res.start);
}
EXPORT_SYMBOL(of_iomap);
