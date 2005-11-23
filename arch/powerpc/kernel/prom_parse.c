#undef DEBUG

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/pci_regs.h>
#include <linux/module.h>
#include <asm/prom.h>

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

/* Debug utility */
#ifdef DEBUG
static void of_dump_addr(const char *s, u32 *addr, int na)
{
	printk("%s", s);
	while(na--)
		printk(" %08x", *(addr++));
	printk("\n");
}
#else
static void of_dump_addr(const char *s, u32 *addr, int na) { }
#endif

/* Read a big address */
static inline u64 of_read_addr(u32 *cell, int size)
{
	u64 r = 0;
	while (size--)
		r = (r << 32) | *(cell++);
	return r;
}

/* Callbacks for bus specific translators */
struct of_bus {
	const char	*name;
	const char	*addresses;
	int		(*match)(struct device_node *parent);
	void		(*count_cells)(struct device_node *child,
				       int *addrc, int *sizec);
	u64		(*map)(u32 *addr, u32 *range, int na, int ns, int pna);
	int		(*translate)(u32 *addr, u64 offset, int na);
};


/*
 * Default translator (generic bus)
 */

static void of_default_count_cells(struct device_node *dev,
				   int *addrc, int *sizec)
{
	if (addrc)
		*addrc = prom_n_addr_cells(dev);
	if (sizec)
		*sizec = prom_n_size_cells(dev);
}

static u64 of_default_map(u32 *addr, u32 *range, int na, int ns, int pna)
{
	u64 cp, s, da;

	cp = of_read_addr(range, na);
	s  = of_read_addr(range + na + pna, ns);
	da = of_read_addr(addr, na);

	DBG("OF: default map, cp="PRu64", s="PRu64", da="PRu64"\n",
	    cp, s, da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_default_translate(u32 *addr, u64 offset, int na)
{
	u64 a = of_read_addr(addr, na);
	memset(addr, 0, na * 4);
	a += offset;
	if (na > 1)
		addr[na - 2] = a >> 32;
	addr[na - 1] = a & 0xffffffffu;

	return 0;
}


/*
 * PCI bus specific translator
 */

static int of_bus_pci_match(struct device_node *np)
{
	return !strcmp(np->type, "pci");
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

	DBG("OF: PCI map, cp="PRu64", s="PRu64", da="PRu64"\n", cp, s, da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_pci_translate(u32 *addr, u64 offset, int na)
{
	return of_default_translate(addr + 1, offset, na - 1);
}

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

static u64 of_bus_isa_map(u32 *addr, u32 *range, int na, int ns, int pna)
{
	u64 cp, s, da;

	/* Check address type match */
	if ((addr[0] ^ range[0]) & 0x00000001)
		return OF_BAD_ADDR;

	/* Read address values, skipping high cell */
	cp = of_read_addr(range + 1, na - 1);
	s  = of_read_addr(range + na + pna, ns);
	da = of_read_addr(addr + 1, na - 1);

	DBG("OF: ISA map, cp="PRu64", s="PRu64", da="PRu64"\n", cp, s, da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_isa_translate(u32 *addr, u64 offset, int na)
{
	return of_default_translate(addr + 1, offset, na - 1);
}

/*
 * Array of bus specific translators
 */

static struct of_bus of_busses[] = {
	/* PCI */
	{
		.name = "pci",
		.addresses = "assigned-addresses",
		.match = of_bus_pci_match,
		.count_cells = of_bus_pci_count_cells,
		.map = of_bus_pci_map,
		.translate = of_bus_pci_translate,
	},
	/* ISA */
	{
		.name = "isa",
		.addresses = "reg",
		.match = of_bus_isa_match,
		.count_cells = of_bus_isa_count_cells,
		.map = of_bus_isa_map,
		.translate = of_bus_isa_translate,
	},
	/* Default */
	{
		.name = "default",
		.addresses = "reg",
		.match = NULL,
		.count_cells = of_default_count_cells,
		.map = of_default_map,
		.translate = of_default_translate,
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
			    int na, int ns, int pna)
{
	u32 *ranges;
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
	ranges = (u32 *)get_property(parent, "ranges", &rlen);
	if (ranges == NULL || rlen == 0) {
		offset = of_read_addr(addr, na);
		memset(addr, 0, pna);
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
	DBG("OF: with offset: %lx\n", offset);

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
u64 of_translate_address(struct device_node *dev, u32 *in_addr)
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
			result = of_read_addr(addr, na);
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

u32 *of_get_address(struct device_node *dev, int index, u64 *size)
{
	u32 *prop;
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
	prop = (u32 *)get_property(dev, bus->addresses, &psize);
	if (prop == NULL)
		return NULL;
	psize /= 4;

	onesize = na + ns;
	for (i = 0; psize >= onesize; psize -= onesize, prop += onesize, i++)
		if (i == index) {
			if (size)
				*size = of_read_addr(prop + na, ns);
			return prop;
		}
	return NULL;
}
EXPORT_SYMBOL(of_get_address);

u32 *of_get_pci_address(struct device_node *dev, int bar_no, u64 *size)
{
	u32 *addr;
	int index;

	for (index = 0; (addr = of_get_address(dev, index, size)) != NULL;
	     index++) {
		if ((addr[0] & 0xff) == ((bar_no * 4) + PCI_BASE_ADDRESS_0))
			return addr;
	}
	return NULL;
}
EXPORT_SYMBOL(of_get_pci_address);
