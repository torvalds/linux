
#include <linux/device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/pci_regs.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/string.h>

/* Max address size we deal with */
#define OF_MAX_ADDR_CELLS	4
#define OF_CHECK_ADDR_COUNT(na)	((na) > 0 && (na) <= OF_MAX_ADDR_CELLS)
#define OF_CHECK_COUNTS(na, ns)	(OF_CHECK_ADDR_COUNT(na) && (ns) > 0)

static struct of_bus *of_match_bus(struct device_node *np);
static int __of_address_to_resource(struct device_node *dev,
		const __be32 *addrp, u64 size, unsigned int flags,
		const char *name, struct resource *r);

/* Debug utility */
#ifdef DEBUG
static void of_dump_addr(const char *s, const __be32 *addr, int na)
{
	printk(KERN_DEBUG "%s", s);
	while (na--)
		printk(" %08x", be32_to_cpu(*(addr++)));
	printk("\n");
}
#else
static void of_dump_addr(const char *s, const __be32 *addr, int na) { }
#endif

/* Callbacks for bus specific translators */
struct of_bus {
	const char	*name;
	const char	*addresses;
	int		(*match)(struct device_node *parent);
	void		(*count_cells)(struct device_node *child,
				       int *addrc, int *sizec);
	u64		(*map)(__be32 *addr, const __be32 *range,
				int na, int ns, int pna);
	int		(*translate)(__be32 *addr, u64 offset, int na);
	unsigned int	(*get_flags)(const __be32 *addr);
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

static u64 of_bus_default_map(__be32 *addr, const __be32 *range,
		int na, int ns, int pna)
{
	u64 cp, s, da;

	cp = of_read_number(range, na);
	s  = of_read_number(range + na + pna, ns);
	da = of_read_number(addr, na);

	pr_debug("OF: default map, cp=%llx, s=%llx, da=%llx\n",
		 (unsigned long long)cp, (unsigned long long)s,
		 (unsigned long long)da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_default_translate(__be32 *addr, u64 offset, int na)
{
	u64 a = of_read_number(addr, na);
	memset(addr, 0, na * 4);
	a += offset;
	if (na > 1)
		addr[na - 2] = cpu_to_be32(a >> 32);
	addr[na - 1] = cpu_to_be32(a & 0xffffffffu);

	return 0;
}

static unsigned int of_bus_default_get_flags(const __be32 *addr)
{
	return IORESOURCE_MEM;
}

#ifdef CONFIG_OF_ADDRESS_PCI
/*
 * PCI bus specific translator
 */

static int of_bus_pci_match(struct device_node *np)
{
	/*
 	 * "pciex" is PCI Express
	 * "vci" is for the /chaos bridge on 1st-gen PCI powermacs
	 * "ht" is hypertransport
	 */
	return !strcmp(np->type, "pci") || !strcmp(np->type, "pciex") ||
		!strcmp(np->type, "vci") || !strcmp(np->type, "ht");
}

static void of_bus_pci_count_cells(struct device_node *np,
				   int *addrc, int *sizec)
{
	if (addrc)
		*addrc = 3;
	if (sizec)
		*sizec = 2;
}

static unsigned int of_bus_pci_get_flags(const __be32 *addr)
{
	unsigned int flags = 0;
	u32 w = be32_to_cpup(addr);

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

static u64 of_bus_pci_map(__be32 *addr, const __be32 *range, int na, int ns,
		int pna)
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

	pr_debug("OF: PCI map, cp=%llx, s=%llx, da=%llx\n",
		 (unsigned long long)cp, (unsigned long long)s,
		 (unsigned long long)da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_pci_translate(__be32 *addr, u64 offset, int na)
{
	return of_bus_default_translate(addr + 1, offset, na - 1);
}
#endif /* CONFIG_OF_ADDRESS_PCI */

#ifdef CONFIG_PCI
const __be32 *of_get_pci_address(struct device_node *dev, int bar_no, u64 *size,
			unsigned int *flags)
{
	const __be32 *prop;
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
	if (!OF_CHECK_ADDR_COUNT(na))
		return NULL;

	/* Get "reg" or "assigned-addresses" property */
	prop = of_get_property(dev, bus->addresses, &psize);
	if (prop == NULL)
		return NULL;
	psize /= 4;

	onesize = na + ns;
	for (i = 0; psize >= onesize; psize -= onesize, prop += onesize, i++) {
		u32 val = be32_to_cpu(prop[0]);
		if ((val & 0xff) == ((bar_no * 4) + PCI_BASE_ADDRESS_0)) {
			if (size)
				*size = of_read_number(prop + na, ns);
			if (flags)
				*flags = bus->get_flags(prop);
			return prop;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(of_get_pci_address);

int of_pci_address_to_resource(struct device_node *dev, int bar,
			       struct resource *r)
{
	const __be32	*addrp;
	u64		size;
	unsigned int	flags;

	addrp = of_get_pci_address(dev, bar, &size, &flags);
	if (addrp == NULL)
		return -EINVAL;
	return __of_address_to_resource(dev, addrp, size, flags, NULL, r);
}
EXPORT_SYMBOL_GPL(of_pci_address_to_resource);

int of_pci_range_parser_init(struct of_pci_range_parser *parser,
				struct device_node *node)
{
	const int na = 3, ns = 2;
	int rlen;

	parser->node = node;
	parser->pna = of_n_addr_cells(node);
	parser->np = parser->pna + na + ns;

	parser->range = of_get_property(node, "ranges", &rlen);
	if (parser->range == NULL)
		return -ENOENT;

	parser->end = parser->range + rlen / sizeof(__be32);

	return 0;
}
EXPORT_SYMBOL_GPL(of_pci_range_parser_init);

struct of_pci_range *of_pci_range_parser_one(struct of_pci_range_parser *parser,
						struct of_pci_range *range)
{
	const int na = 3, ns = 2;

	if (!range)
		return NULL;

	if (!parser->range || parser->range + parser->np > parser->end)
		return NULL;

	range->pci_space = parser->range[0];
	range->flags = of_bus_pci_get_flags(parser->range);
	range->pci_addr = of_read_number(parser->range + 1, ns);
	range->cpu_addr = of_translate_address(parser->node,
				parser->range + na);
	range->size = of_read_number(parser->range + parser->pna + na, ns);

	parser->range += parser->np;

	/* Now consume following elements while they are contiguous */
	while (parser->range + parser->np <= parser->end) {
		u32 flags, pci_space;
		u64 pci_addr, cpu_addr, size;

		pci_space = be32_to_cpup(parser->range);
		flags = of_bus_pci_get_flags(parser->range);
		pci_addr = of_read_number(parser->range + 1, ns);
		cpu_addr = of_translate_address(parser->node,
				parser->range + na);
		size = of_read_number(parser->range + parser->pna + na, ns);

		if (flags != range->flags)
			break;
		if (pci_addr != range->pci_addr + range->size ||
		    cpu_addr != range->cpu_addr + range->size)
			break;

		range->size += size;
		parser->range += parser->np;
	}

	return range;
}
EXPORT_SYMBOL_GPL(of_pci_range_parser_one);

/*
 * of_pci_range_to_resource - Create a resource from an of_pci_range
 * @range:	the PCI range that describes the resource
 * @np:		device node where the range belongs to
 * @res:	pointer to a valid resource that will be updated to
 *              reflect the values contained in the range.
 *
 * Returns EINVAL if the range cannot be converted to resource.
 *
 * Note that if the range is an IO range, the resource will be converted
 * using pci_address_to_pio() which can fail if it is called too early or
 * if the range cannot be matched to any host bridge IO space (our case here).
 * To guard against that we try to register the IO range first.
 * If that fails we know that pci_address_to_pio() will do too.
 */
int of_pci_range_to_resource(struct of_pci_range *range,
			     struct device_node *np, struct resource *res)
{
	int err;
	res->flags = range->flags;
	res->parent = res->child = res->sibling = NULL;
	res->name = np->full_name;

	if (res->flags & IORESOURCE_IO) {
		unsigned long port;
		err = pci_register_io_range(range->cpu_addr, range->size);
		if (err)
			goto invalid_range;
		port = pci_address_to_pio(range->cpu_addr);
		if (port == (unsigned long)-1) {
			err = -EINVAL;
			goto invalid_range;
		}
		res->start = port;
	} else {
		res->start = range->cpu_addr;
	}
	res->end = res->start + range->size - 1;
	return 0;

invalid_range:
	res->start = (resource_size_t)OF_BAD_ADDR;
	res->end = (resource_size_t)OF_BAD_ADDR;
	return err;
}
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

static u64 of_bus_isa_map(__be32 *addr, const __be32 *range, int na, int ns,
		int pna)
{
	u64 cp, s, da;

	/* Check address type match */
	if ((addr[0] ^ range[0]) & cpu_to_be32(1))
		return OF_BAD_ADDR;

	/* Read address values, skipping high cell */
	cp = of_read_number(range + 1, na - 1);
	s  = of_read_number(range + na + pna, ns);
	da = of_read_number(addr + 1, na - 1);

	pr_debug("OF: ISA map, cp=%llx, s=%llx, da=%llx\n",
		 (unsigned long long)cp, (unsigned long long)s,
		 (unsigned long long)da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_isa_translate(__be32 *addr, u64 offset, int na)
{
	return of_bus_default_translate(addr + 1, offset, na - 1);
}

static unsigned int of_bus_isa_get_flags(const __be32 *addr)
{
	unsigned int flags = 0;
	u32 w = be32_to_cpup(addr);

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
#ifdef CONFIG_OF_ADDRESS_PCI
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
#endif /* CONFIG_OF_ADDRESS_PCI */
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

static int of_empty_ranges_quirk(struct device_node *np)
{
	if (IS_ENABLED(CONFIG_PPC)) {
		/* To save cycles, we cache the result for global "Mac" setting */
		static int quirk_state = -1;

		/* PA-SEMI sdc DT bug */
		if (of_device_is_compatible(np, "1682m-sdc"))
			return true;

		/* Make quirk cached */
		if (quirk_state < 0)
			quirk_state =
				of_machine_is_compatible("Power Macintosh") ||
				of_machine_is_compatible("MacRISC");
		return quirk_state;
	}
	return false;
}

static int of_translate_one(struct device_node *parent, struct of_bus *bus,
			    struct of_bus *pbus, __be32 *addr,
			    int na, int ns, int pna, const char *rprop)
{
	const __be32 *ranges;
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
	 *
	 * As far as we know, this damage only exists on Apple machines, so
	 * This code is only enabled on powerpc. --gcl
	 */
	ranges = of_get_property(parent, rprop, &rlen);
	if (ranges == NULL && !of_empty_ranges_quirk(parent)) {
		pr_debug("OF: no ranges; cannot translate\n");
		return 1;
	}
	if (ranges == NULL || rlen == 0) {
		offset = of_read_number(addr, na);
		memset(addr, 0, pna * 4);
		pr_debug("OF: empty ranges; 1:1 translation\n");
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
	pr_debug("OF: with offset: %llx\n", (unsigned long long)offset);

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
static u64 __of_translate_address(struct device_node *dev,
				  const __be32 *in_addr, const char *rprop)
{
	struct device_node *parent = NULL;
	struct of_bus *bus, *pbus;
	__be32 addr[OF_MAX_ADDR_CELLS];
	int na, ns, pna, pns;
	u64 result = OF_BAD_ADDR;

	pr_debug("OF: ** translation for device %s **\n", of_node_full_name(dev));

	/* Increase refcount at current level */
	of_node_get(dev);

	/* Get parent & match bus type */
	parent = of_get_parent(dev);
	if (parent == NULL)
		goto bail;
	bus = of_match_bus(parent);

	/* Count address cells & copy address locally */
	bus->count_cells(dev, &na, &ns);
	if (!OF_CHECK_COUNTS(na, ns)) {
		pr_debug("OF: Bad cell count for %s\n", of_node_full_name(dev));
		goto bail;
	}
	memcpy(addr, in_addr, na * 4);

	pr_debug("OF: bus is %s (na=%d, ns=%d) on %s\n",
	    bus->name, na, ns, of_node_full_name(parent));
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
			       of_node_full_name(dev));
			break;
		}

		pr_debug("OF: parent bus is %s (na=%d, ns=%d) on %s\n",
		    pbus->name, pna, pns, of_node_full_name(parent));

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

u64 of_translate_address(struct device_node *dev, const __be32 *in_addr)
{
	return __of_translate_address(dev, in_addr, "ranges");
}
EXPORT_SYMBOL(of_translate_address);

u64 of_translate_dma_address(struct device_node *dev, const __be32 *in_addr)
{
	return __of_translate_address(dev, in_addr, "dma-ranges");
}
EXPORT_SYMBOL(of_translate_dma_address);

const __be32 *of_get_address(struct device_node *dev, int index, u64 *size,
		    unsigned int *flags)
{
	const __be32 *prop;
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
	if (!OF_CHECK_ADDR_COUNT(na))
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

#ifdef PCI_IOBASE
struct io_range {
	struct list_head list;
	phys_addr_t start;
	resource_size_t size;
};

static LIST_HEAD(io_range_list);
static DEFINE_SPINLOCK(io_range_lock);
#endif

/*
 * Record the PCI IO range (expressed as CPU physical address + size).
 * Return a negative value if an error has occured, zero otherwise
 */
int __weak pci_register_io_range(phys_addr_t addr, resource_size_t size)
{
	int err = 0;

#ifdef PCI_IOBASE
	struct io_range *range;
	resource_size_t allocated_size = 0;

	/* check if the range hasn't been previously recorded */
	spin_lock(&io_range_lock);
	list_for_each_entry(range, &io_range_list, list) {
		if (addr >= range->start && addr + size <= range->start + size) {
			/* range already registered, bail out */
			goto end_register;
		}
		allocated_size += range->size;
	}

	/* range not registed yet, check for available space */
	if (allocated_size + size - 1 > IO_SPACE_LIMIT) {
		/* if it's too big check if 64K space can be reserved */
		if (allocated_size + SZ_64K - 1 > IO_SPACE_LIMIT) {
			err = -E2BIG;
			goto end_register;
		}

		size = SZ_64K;
		pr_warn("Requested IO range too big, new size set to 64K\n");
	}

	/* add the range to the list */
	range = kzalloc(sizeof(*range), GFP_ATOMIC);
	if (!range) {
		err = -ENOMEM;
		goto end_register;
	}

	range->start = addr;
	range->size = size;

	list_add_tail(&range->list, &io_range_list);

end_register:
	spin_unlock(&io_range_lock);
#endif

	return err;
}

phys_addr_t pci_pio_to_address(unsigned long pio)
{
	phys_addr_t address = (phys_addr_t)OF_BAD_ADDR;

#ifdef PCI_IOBASE
	struct io_range *range;
	resource_size_t allocated_size = 0;

	if (pio > IO_SPACE_LIMIT)
		return address;

	spin_lock(&io_range_lock);
	list_for_each_entry(range, &io_range_list, list) {
		if (pio >= allocated_size && pio < allocated_size + range->size) {
			address = range->start + pio - allocated_size;
			break;
		}
		allocated_size += range->size;
	}
	spin_unlock(&io_range_lock);
#endif

	return address;
}

unsigned long __weak pci_address_to_pio(phys_addr_t address)
{
#ifdef PCI_IOBASE
	struct io_range *res;
	resource_size_t offset = 0;
	unsigned long addr = -1;

	spin_lock(&io_range_lock);
	list_for_each_entry(res, &io_range_list, list) {
		if (address >= res->start && address < res->start + res->size) {
			addr = address - res->start + offset;
			break;
		}
		offset += res->size;
	}
	spin_unlock(&io_range_lock);

	return addr;
#else
	if (address > IO_SPACE_LIMIT)
		return (unsigned long)-1;

	return (unsigned long) address;
#endif
}

static int __of_address_to_resource(struct device_node *dev,
		const __be32 *addrp, u64 size, unsigned int flags,
		const char *name, struct resource *r)
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
	r->name = name ? name : dev->full_name;

	return 0;
}

/**
 * of_address_to_resource - Translate device tree address and return as resource
 *
 * Note that if your address is a PIO address, the conversion will fail if
 * the physical address can't be internally converted to an IO token with
 * pci_address_to_pio(), that is because it's either called to early or it
 * can't be matched to any host bridge IO space
 */
int of_address_to_resource(struct device_node *dev, int index,
			   struct resource *r)
{
	const __be32	*addrp;
	u64		size;
	unsigned int	flags;
	const char	*name = NULL;

	addrp = of_get_address(dev, index, &size, &flags);
	if (addrp == NULL)
		return -EINVAL;

	/* Get optional "reg-names" property to add a name to a resource */
	of_property_read_string_index(dev, "reg-names",	index, &name);

	return __of_address_to_resource(dev, addrp, size, flags, name, r);
}
EXPORT_SYMBOL_GPL(of_address_to_resource);

struct device_node *of_find_matching_node_by_address(struct device_node *from,
					const struct of_device_id *matches,
					u64 base_address)
{
	struct device_node *dn = of_find_matching_node(from, matches);
	struct resource res;

	while (dn) {
		if (of_address_to_resource(dn, 0, &res))
			continue;
		if (res.start == base_address)
			return dn;
		dn = of_find_matching_node(dn, matches);
	}

	return NULL;
}


/**
 * of_iomap - Maps the memory mapped IO for a given device_node
 * @device:	the device whose io range will be mapped
 * @index:	index of the io range
 *
 * Returns a pointer to the mapped memory
 */
void __iomem *of_iomap(struct device_node *np, int index)
{
	struct resource res;

	if (of_address_to_resource(np, index, &res))
		return NULL;

	return ioremap(res.start, resource_size(&res));
}
EXPORT_SYMBOL(of_iomap);

/*
 * of_io_request_and_map - Requests a resource and maps the memory mapped IO
 *			   for a given device_node
 * @device:	the device whose io range will be mapped
 * @index:	index of the io range
 * @name:	name of the resource
 *
 * Returns a pointer to the requested and mapped memory or an ERR_PTR() encoded
 * error code on failure. Usage example:
 *
 *	base = of_io_request_and_map(node, 0, "foo");
 *	if (IS_ERR(base))
 *		return PTR_ERR(base);
 */
void __iomem *of_io_request_and_map(struct device_node *np, int index,
					const char *name)
{
	struct resource res;
	void __iomem *mem;

	if (of_address_to_resource(np, index, &res))
		return IOMEM_ERR_PTR(-EINVAL);

	if (!request_mem_region(res.start, resource_size(&res), name))
		return IOMEM_ERR_PTR(-EBUSY);

	mem = ioremap(res.start, resource_size(&res));
	if (!mem) {
		release_mem_region(res.start, resource_size(&res));
		return IOMEM_ERR_PTR(-ENOMEM);
	}

	return mem;
}
EXPORT_SYMBOL(of_io_request_and_map);

/**
 * of_dma_get_range - Get DMA range info
 * @np:		device node to get DMA range info
 * @dma_addr:	pointer to store initial DMA address of DMA range
 * @paddr:	pointer to store initial CPU address of DMA range
 * @size:	pointer to store size of DMA range
 *
 * Look in bottom up direction for the first "dma-ranges" property
 * and parse it.
 *  dma-ranges format:
 *	DMA addr (dma_addr)	: naddr cells
 *	CPU addr (phys_addr_t)	: pna cells
 *	size			: nsize cells
 *
 * It returns -ENODEV if "dma-ranges" property was not found
 * for this device in DT.
 */
int of_dma_get_range(struct device_node *np, u64 *dma_addr, u64 *paddr, u64 *size)
{
	struct device_node *node = of_node_get(np);
	const __be32 *ranges = NULL;
	int len, naddr, nsize, pna;
	int ret = 0;
	u64 dmaaddr;

	if (!node)
		return -EINVAL;

	while (1) {
		naddr = of_n_addr_cells(node);
		nsize = of_n_size_cells(node);
		node = of_get_next_parent(node);
		if (!node)
			break;

		ranges = of_get_property(node, "dma-ranges", &len);

		/* Ignore empty ranges, they imply no translation required */
		if (ranges && len > 0)
			break;

		/*
		 * At least empty ranges has to be defined for parent node if
		 * DMA is supported
		 */
		if (!ranges)
			break;
	}

	if (!ranges) {
		pr_debug("%s: no dma-ranges found for node(%s)\n",
			 __func__, np->full_name);
		ret = -ENODEV;
		goto out;
	}

	len /= sizeof(u32);

	pna = of_n_addr_cells(node);

	/* dma-ranges format:
	 * DMA addr	: naddr cells
	 * CPU addr	: pna cells
	 * size		: nsize cells
	 */
	dmaaddr = of_read_number(ranges, naddr);
	*paddr = of_translate_dma_address(np, ranges);
	if (*paddr == OF_BAD_ADDR) {
		pr_err("%s: translation of DMA address(%pad) to CPU address failed node(%s)\n",
		       __func__, dma_addr, np->full_name);
		ret = -EINVAL;
		goto out;
	}
	*dma_addr = dmaaddr;

	*size = of_read_number(ranges + naddr + pna, nsize);

	pr_debug("dma_addr(%llx) cpu_addr(%llx) size(%llx)\n",
		 *dma_addr, *paddr, *size);

out:
	of_node_put(node);

	return ret;
}
EXPORT_SYMBOL_GPL(of_dma_get_range);

/**
 * of_dma_is_coherent - Check if device is coherent
 * @np:	device node
 *
 * It returns true if "dma-coherent" property was found
 * for this device in DT.
 */
bool of_dma_is_coherent(struct device_node *np)
{
	struct device_node *node = of_node_get(np);

	while (node) {
		if (of_property_read_bool(node, "dma-coherent")) {
			of_node_put(node);
			return true;
		}
		node = of_get_next_parent(node);
	}
	of_node_put(node);
	return false;
}
EXPORT_SYMBOL_GPL(of_dma_is_coherent);
