#undef DEBUG

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/pci_regs.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/etherdevice.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

#ifdef DEBUG
#define DBG(fmt...) do { printk(fmt); } while(0)
#else
#define DBG(fmt...) do { } while(0)
#endif

#ifdef CONFIG_PPC64
#define PRu64	"%lx"
#else
#define PRu64	"%llx"
#endif

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
	printk("%s", s);
	while(na--)
		printk(" %08x", *(addr++));
	printk("\n");
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

	DBG("OF: default map, cp="PRu64", s="PRu64", da="PRu64"\n",
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

static unsigned int of_bus_pci_get_flags(const u32 *addr)
{
	unsigned int flags = 0;
	u32 w = addr[0];

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

static u64 of_bus_pci_map(u32 *addr, const u32 *range, int na, int ns, int pna)
{
	u64 cp, s, da;
	unsigned int af, rf;

	af = of_bus_pci_get_flags(addr);
	rf = of_bus_pci_get_flags(range);

	/* Check address type match */
	if ((af ^ rf) & (IORESOURCE_MEM | IORESOURCE_IO))
		return OF_BAD_ADDR;

	/* Read address values, skipping high cell */
	cp = of_read_number(range + 1, na - 1);
	s  = of_read_number(range + na + pna, ns);
	da = of_read_number(addr + 1, na - 1);

	DBG("OF: PCI map, cp="PRu64", s="PRu64", da="PRu64"\n", cp, s, da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_pci_translate(u32 *addr, u64 offset, int na)
{
	return of_bus_default_translate(addr + 1, offset, na - 1);
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
	if (dn) {
		rc = of_irq_map_one(dn, 0, out_irq);
		if (!rc)
			return rc;
	}

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
#ifdef CONFIG_PPC64
			ppnode = pci_bus_to_OF_node(pdev->bus);
#else
			struct pci_controller *host;
			host = pci_bus_to_host(pdev->bus);
			ppnode = host ? host->dn : NULL;
#endif
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
		 * Fortunately, in most cases, interrupt-map-mask doesn't include
		 * the bus number as part of the matching.
		 * You should still be careful about that though if you intend
		 * to rely on this function (you ship  a firmware that doesn't
		 * create device nodes for all PCI devices).
		 */
		if (ppnode)
			break;

		/* We can only get here if we hit a P2P bridge with no node,
		 * let's do standard swizzling and try again
		 */
		lspec = pci_swizzle_interrupt_pin(pdev, lspec);
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

	DBG("OF: ISA map, cp="PRu64", s="PRu64", da="PRu64"\n", cp, s, da);

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

	for (i = 0; i < ARRAY_SIZE(of_busses); i ++)
		if (!of_busses[i].match || of_busses[i].match(np))
			return &of_busses[i];
	BUG();
	return NULL;
}

static int of_translate_one(struct device_node *parent, struct of_bus *bus,
			    struct of_bus *pbus, u32 *addr,
			    int na, int ns, int pna, const char *rprop)
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
	ranges = of_get_property(parent, rprop, &rlen);
	if (ranges == NULL || rlen == 0) {
		offset = of_read_number(addr, na);
		memset(addr, 0, pna * 4);
		DBG("OF: no ranges, 1:1 translation\n");
		goto finish;
	}

	DBG("OF: walking ranges...\n");

	/* Now walk through the ranges */
	rlen /= 4;
	rone = na + pna + ns;
	for (; rlen >= rone; rlen -= rone, ranges += rone) {
		offset = bus->map(addr, ranges, na, ns, pna);
		if (offset != OF_BAD_ADDR)
			break;
	}
	if (offset == OF_BAD_ADDR) {
		DBG("OF: not found !\n");
		return 1;
	}
	memcpy(addr, ranges + na, 4 * pna);

 finish:
	of_dump_addr("OF: parent translation for:", addr, pna);
	DBG("OF: with offset: "PRu64"\n", offset);

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
u64 __of_translate_address(struct device_node *dev, const u32 *in_addr,
			   const char *rprop)
{
	struct device_node *parent = NULL;
	struct of_bus *bus, *pbus;
	u32 addr[OF_MAX_ADDR_CELLS];
	int na, ns, pna, pns;
	u64 result = OF_BAD_ADDR;

	DBG("OF: ** translation for device %s **\n", dev->full_name);

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

	DBG("OF: bus is %s (na=%d, ns=%d) on %s\n",
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
			DBG("OF: reached root node\n");
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

		DBG("OF: parent bus is %s (na=%d, ns=%d) on %s\n",
		    pbus->name, pna, pns, parent->full_name);

		/* Apply bus translation */
		if (of_translate_one(dev, bus, pbus, addr, na, ns, pna, rprop))
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

u64 of_translate_address(struct device_node *dev, const u32 *in_addr)
{
	return __of_translate_address(dev, in_addr, "ranges");
}
EXPORT_SYMBOL(of_translate_address);

u64 of_translate_dma_address(struct device_node *dev, const u32 *in_addr)
{
	return __of_translate_address(dev, in_addr, "dma-ranges");
}
EXPORT_SYMBOL(of_translate_dma_address);

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
	prop = of_get_property(dev, bus->addresses, &psize);
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
		port = pci_address_to_pio(taddr);
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

void __iomem *of_iomap(struct device_node *np, int index)
{
	struct resource res;

	if (of_address_to_resource(np, index, &res))
		return NULL;

	return ioremap(res.start, 1 + res.end - res.start);
}
EXPORT_SYMBOL(of_iomap);
