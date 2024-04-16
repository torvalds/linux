// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt)	"OF: " fmt

#include <linux/device.h>
#include <linux/fwnode.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/logic_pio.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dma-direct.h> /* for bus_dma_region */

#include "of_private.h"

/* Max address size we deal with */
#define OF_MAX_ADDR_CELLS	4
#define OF_CHECK_ADDR_COUNT(na)	((na) > 0 && (na) <= OF_MAX_ADDR_CELLS)
#define OF_CHECK_COUNTS(na, ns)	(OF_CHECK_ADDR_COUNT(na) && (ns) > 0)

static struct of_bus *of_match_bus(struct device_node *np);
static int __of_address_to_resource(struct device_node *dev, int index,
		int bar_no, struct resource *r);
static bool of_mmio_is_nonposted(struct device_node *np);

/* Debug utility */
#ifdef DEBUG
static void of_dump_addr(const char *s, const __be32 *addr, int na)
{
	pr_debug("%s", s);
	while (na--)
		pr_cont(" %08x", be32_to_cpu(*(addr++)));
	pr_cont("\n");
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
	bool	has_flags;
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

	pr_debug("default map, cp=%llx, s=%llx, da=%llx\n", cp, s, da);

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

#ifdef CONFIG_PCI
static unsigned int of_bus_pci_get_flags(const __be32 *addr)
{
	unsigned int flags = 0;
	u32 w = be32_to_cpup(addr);

	if (!IS_ENABLED(CONFIG_PCI))
		return 0;

	switch((w >> 24) & 0x03) {
	case 0x01:
		flags |= IORESOURCE_IO;
		break;
	case 0x02: /* 32 bits */
		flags |= IORESOURCE_MEM;
		break;

	case 0x03: /* 64 bits */
		flags |= IORESOURCE_MEM | IORESOURCE_MEM_64;
		break;
	}
	if (w & 0x40000000)
		flags |= IORESOURCE_PREFETCH;
	return flags;
}

/*
 * PCI bus specific translator
 */

static bool of_node_is_pcie(struct device_node *np)
{
	bool is_pcie = of_node_name_eq(np, "pcie");

	if (is_pcie)
		pr_warn_once("%pOF: Missing device_type\n", np);

	return is_pcie;
}

static int of_bus_pci_match(struct device_node *np)
{
	/*
 	 * "pciex" is PCI Express
	 * "vci" is for the /chaos bridge on 1st-gen PCI powermacs
	 * "ht" is hypertransport
	 *
	 * If none of the device_type match, and that the node name is
	 * "pcie", accept the device as PCI (with a warning).
	 */
	return of_node_is_type(np, "pci") || of_node_is_type(np, "pciex") ||
		of_node_is_type(np, "vci") || of_node_is_type(np, "ht") ||
		of_node_is_pcie(np);
}

static void of_bus_pci_count_cells(struct device_node *np,
				   int *addrc, int *sizec)
{
	if (addrc)
		*addrc = 3;
	if (sizec)
		*sizec = 2;
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

	pr_debug("PCI map, cp=%llx, s=%llx, da=%llx\n", cp, s, da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_pci_translate(__be32 *addr, u64 offset, int na)
{
	return of_bus_default_translate(addr + 1, offset, na - 1);
}
#endif /* CONFIG_PCI */

int of_pci_address_to_resource(struct device_node *dev, int bar,
			       struct resource *r)
{

	if (!IS_ENABLED(CONFIG_PCI))
		return -ENOSYS;

	return __of_address_to_resource(dev, -1, bar, r);
}
EXPORT_SYMBOL_GPL(of_pci_address_to_resource);

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

	if (!IS_ENABLED(CONFIG_PCI))
		return -ENOSYS;

	if (res->flags & IORESOURCE_IO) {
		unsigned long port;
		err = pci_register_io_range(&np->fwnode, range->cpu_addr,
				range->size);
		if (err)
			goto invalid_range;
		port = pci_address_to_pio(range->cpu_addr);
		if (port == (unsigned long)-1) {
			err = -EINVAL;
			goto invalid_range;
		}
		res->start = port;
	} else {
		if ((sizeof(resource_size_t) < 8) &&
		    upper_32_bits(range->cpu_addr)) {
			err = -EINVAL;
			goto invalid_range;
		}

		res->start = range->cpu_addr;
	}
	res->end = res->start + range->size - 1;
	return 0;

invalid_range:
	res->start = (resource_size_t)OF_BAD_ADDR;
	res->end = (resource_size_t)OF_BAD_ADDR;
	return err;
}
EXPORT_SYMBOL(of_pci_range_to_resource);

/*
 * ISA bus specific translator
 */

static int of_bus_isa_match(struct device_node *np)
{
	return of_node_name_eq(np, "isa");
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

	pr_debug("ISA map, cp=%llx, s=%llx, da=%llx\n", cp, s, da);

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
#ifdef CONFIG_PCI
	/* PCI */
	{
		.name = "pci",
		.addresses = "assigned-addresses",
		.match = of_bus_pci_match,
		.count_cells = of_bus_pci_count_cells,
		.map = of_bus_pci_map,
		.translate = of_bus_pci_translate,
		.has_flags = true,
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
		.has_flags = true,
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

	/*
	 * Normally, an absence of a "ranges" property means we are
	 * crossing a non-translatable boundary, and thus the addresses
	 * below the current cannot be converted to CPU physical ones.
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
	 *
	 * This quirk also applies for 'dma-ranges' which frequently exist in
	 * child nodes without 'dma-ranges' in the parent nodes. --RobH
	 */
	ranges = of_get_property(parent, rprop, &rlen);
	if (ranges == NULL && !of_empty_ranges_quirk(parent) &&
	    strcmp(rprop, "dma-ranges")) {
		pr_debug("no ranges; cannot translate\n");
		return 1;
	}
	if (ranges == NULL || rlen == 0) {
		offset = of_read_number(addr, na);
		memset(addr, 0, pna * 4);
		pr_debug("empty ranges; 1:1 translation\n");
		goto finish;
	}

	pr_debug("walking ranges...\n");

	/* Now walk through the ranges */
	rlen /= 4;
	rone = na + pna + ns;
	for (; rlen >= rone; rlen -= rone, ranges += rone) {
		offset = bus->map(addr, ranges, na, ns, pna);
		if (offset != OF_BAD_ADDR)
			break;
	}
	if (offset == OF_BAD_ADDR) {
		pr_debug("not found !\n");
		return 1;
	}
	memcpy(addr, ranges + na, 4 * pna);

 finish:
	of_dump_addr("parent translation for:", addr, pna);
	pr_debug("with offset: %llx\n", offset);

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
 *
 * Whenever the translation fails, the *host pointer will be set to the
 * device that had registered logical PIO mapping, and the return code is
 * relative to that node.
 */
static u64 __of_translate_address(struct device_node *dev,
				  struct device_node *(*get_parent)(const struct device_node *),
				  const __be32 *in_addr, const char *rprop,
				  struct device_node **host)
{
	struct device_node *parent = NULL;
	struct of_bus *bus, *pbus;
	__be32 addr[OF_MAX_ADDR_CELLS];
	int na, ns, pna, pns;
	u64 result = OF_BAD_ADDR;

	pr_debug("** translation for device %pOF **\n", dev);

	/* Increase refcount at current level */
	of_node_get(dev);

	*host = NULL;
	/* Get parent & match bus type */
	parent = get_parent(dev);
	if (parent == NULL)
		goto bail;
	bus = of_match_bus(parent);

	/* Count address cells & copy address locally */
	bus->count_cells(dev, &na, &ns);
	if (!OF_CHECK_COUNTS(na, ns)) {
		pr_debug("Bad cell count for %pOF\n", dev);
		goto bail;
	}
	memcpy(addr, in_addr, na * 4);

	pr_debug("bus is %s (na=%d, ns=%d) on %pOF\n",
	    bus->name, na, ns, parent);
	of_dump_addr("translating address:", addr, na);

	/* Translate */
	for (;;) {
		struct logic_pio_hwaddr *iorange;

		/* Switch to parent bus */
		of_node_put(dev);
		dev = parent;
		parent = get_parent(dev);

		/* If root, we have finished */
		if (parent == NULL) {
			pr_debug("reached root node\n");
			result = of_read_number(addr, na);
			break;
		}

		/*
		 * For indirectIO device which has no ranges property, get
		 * the address from reg directly.
		 */
		iorange = find_io_range_by_fwnode(&dev->fwnode);
		if (iorange && (iorange->flags != LOGIC_PIO_CPU_MMIO)) {
			result = of_read_number(addr + 1, na - 1);
			pr_debug("indirectIO matched(%pOF) 0x%llx\n",
				 dev, result);
			*host = of_node_get(dev);
			break;
		}

		/* Get new parent bus and counts */
		pbus = of_match_bus(parent);
		pbus->count_cells(dev, &pna, &pns);
		if (!OF_CHECK_COUNTS(pna, pns)) {
			pr_err("Bad cell count for %pOF\n", dev);
			break;
		}

		pr_debug("parent bus is %s (na=%d, ns=%d) on %pOF\n",
		    pbus->name, pna, pns, parent);

		/* Apply bus translation */
		if (of_translate_one(dev, bus, pbus, addr, na, ns, pna, rprop))
			break;

		/* Complete the move up one level */
		na = pna;
		ns = pns;
		bus = pbus;

		of_dump_addr("one level translation:", addr, na);
	}
 bail:
	of_node_put(parent);
	of_node_put(dev);

	return result;
}

u64 of_translate_address(struct device_node *dev, const __be32 *in_addr)
{
	struct device_node *host;
	u64 ret;

	ret = __of_translate_address(dev, of_get_parent,
				     in_addr, "ranges", &host);
	if (host) {
		of_node_put(host);
		return OF_BAD_ADDR;
	}

	return ret;
}
EXPORT_SYMBOL(of_translate_address);

#ifdef CONFIG_HAS_DMA
struct device_node *__of_get_dma_parent(const struct device_node *np)
{
	struct of_phandle_args args;
	int ret, index;

	index = of_property_match_string(np, "interconnect-names", "dma-mem");
	if (index < 0)
		return of_get_parent(np);

	ret = of_parse_phandle_with_args(np, "interconnects",
					 "#interconnect-cells",
					 index, &args);
	if (ret < 0)
		return of_get_parent(np);

	return of_node_get(args.np);
}
#endif

static struct device_node *of_get_next_dma_parent(struct device_node *np)
{
	struct device_node *parent;

	parent = __of_get_dma_parent(np);
	of_node_put(np);

	return parent;
}

u64 of_translate_dma_address(struct device_node *dev, const __be32 *in_addr)
{
	struct device_node *host;
	u64 ret;

	ret = __of_translate_address(dev, __of_get_dma_parent,
				     in_addr, "dma-ranges", &host);

	if (host) {
		of_node_put(host);
		return OF_BAD_ADDR;
	}

	return ret;
}
EXPORT_SYMBOL(of_translate_dma_address);

/**
 * of_translate_dma_region - Translate device tree address and size tuple
 * @dev: device tree node for which to translate
 * @prop: pointer into array of cells
 * @start: return value for the start of the DMA range
 * @length: return value for the length of the DMA range
 *
 * Returns a pointer to the cell immediately following the translated DMA region.
 */
const __be32 *of_translate_dma_region(struct device_node *dev, const __be32 *prop,
				      phys_addr_t *start, size_t *length)
{
	struct device_node *parent;
	u64 address, size;
	int na, ns;

	parent = __of_get_dma_parent(dev);
	if (!parent)
		return NULL;

	na = of_bus_n_addr_cells(parent);
	ns = of_bus_n_size_cells(parent);

	of_node_put(parent);

	address = of_translate_dma_address(dev, prop);
	if (address == OF_BAD_ADDR)
		return NULL;

	size = of_read_number(prop + na, ns);

	if (start)
		*start = address;

	if (length)
		*length = size;

	return prop + na + ns;
}
EXPORT_SYMBOL(of_translate_dma_region);

const __be32 *__of_get_address(struct device_node *dev, int index, int bar_no,
			       u64 *size, unsigned int *flags)
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
	if (strcmp(bus->name, "pci") && (bar_no >= 0)) {
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
		/* PCI bus matches on BAR number instead of index */
		if (((bar_no >= 0) && ((val & 0xff) == ((bar_no * 4) + PCI_BASE_ADDRESS_0))) ||
		    ((index >= 0) && (i == index))) {
			if (size)
				*size = of_read_number(prop + na, ns);
			if (flags)
				*flags = bus->get_flags(prop);
			return prop;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(__of_get_address);

static int parser_init(struct of_pci_range_parser *parser,
			struct device_node *node, const char *name)
{
	int rlen;

	parser->node = node;
	parser->pna = of_n_addr_cells(node);
	parser->na = of_bus_n_addr_cells(node);
	parser->ns = of_bus_n_size_cells(node);
	parser->dma = !strcmp(name, "dma-ranges");
	parser->bus = of_match_bus(node);

	parser->range = of_get_property(node, name, &rlen);
	if (parser->range == NULL)
		return -ENOENT;

	parser->end = parser->range + rlen / sizeof(__be32);

	return 0;
}

int of_pci_range_parser_init(struct of_pci_range_parser *parser,
				struct device_node *node)
{
	return parser_init(parser, node, "ranges");
}
EXPORT_SYMBOL_GPL(of_pci_range_parser_init);

int of_pci_dma_range_parser_init(struct of_pci_range_parser *parser,
				struct device_node *node)
{
	return parser_init(parser, node, "dma-ranges");
}
EXPORT_SYMBOL_GPL(of_pci_dma_range_parser_init);
#define of_dma_range_parser_init of_pci_dma_range_parser_init

struct of_pci_range *of_pci_range_parser_one(struct of_pci_range_parser *parser,
						struct of_pci_range *range)
{
	int na = parser->na;
	int ns = parser->ns;
	int np = parser->pna + na + ns;
	int busflag_na = 0;

	if (!range)
		return NULL;

	if (!parser->range || parser->range + np > parser->end)
		return NULL;

	range->flags = parser->bus->get_flags(parser->range);

	/* A extra cell for resource flags */
	if (parser->bus->has_flags)
		busflag_na = 1;

	range->bus_addr = of_read_number(parser->range + busflag_na, na - busflag_na);

	if (parser->dma)
		range->cpu_addr = of_translate_dma_address(parser->node,
				parser->range + na);
	else
		range->cpu_addr = of_translate_address(parser->node,
				parser->range + na);
	range->size = of_read_number(parser->range + parser->pna + na, ns);

	parser->range += np;

	/* Now consume following elements while they are contiguous */
	while (parser->range + np <= parser->end) {
		u32 flags = 0;
		u64 bus_addr, cpu_addr, size;

		flags = parser->bus->get_flags(parser->range);
		bus_addr = of_read_number(parser->range + busflag_na, na - busflag_na);
		if (parser->dma)
			cpu_addr = of_translate_dma_address(parser->node,
					parser->range + na);
		else
			cpu_addr = of_translate_address(parser->node,
					parser->range + na);
		size = of_read_number(parser->range + parser->pna + na, ns);

		if (flags != range->flags)
			break;
		if (bus_addr != range->bus_addr + range->size ||
		    cpu_addr != range->cpu_addr + range->size)
			break;

		range->size += size;
		parser->range += np;
	}

	return range;
}
EXPORT_SYMBOL_GPL(of_pci_range_parser_one);

static u64 of_translate_ioport(struct device_node *dev, const __be32 *in_addr,
			u64 size)
{
	u64 taddr;
	unsigned long port;
	struct device_node *host;

	taddr = __of_translate_address(dev, of_get_parent,
				       in_addr, "ranges", &host);
	if (host) {
		/* host-specific port access */
		port = logic_pio_trans_hwaddr(&host->fwnode, taddr, size);
		of_node_put(host);
	} else {
		/* memory-mapped I/O range */
		port = pci_address_to_pio(taddr);
	}

	if (port == (unsigned long)-1)
		return OF_BAD_ADDR;

	return port;
}

static int __of_address_to_resource(struct device_node *dev, int index, int bar_no,
		struct resource *r)
{
	u64 taddr;
	const __be32	*addrp;
	u64		size;
	unsigned int	flags;
	const char	*name = NULL;

	addrp = __of_get_address(dev, index, bar_no, &size, &flags);
	if (addrp == NULL)
		return -EINVAL;

	/* Get optional "reg-names" property to add a name to a resource */
	if (index >= 0)
		of_property_read_string_index(dev, "reg-names",	index, &name);

	if (flags & IORESOURCE_MEM)
		taddr = of_translate_address(dev, addrp);
	else if (flags & IORESOURCE_IO)
		taddr = of_translate_ioport(dev, addrp, size);
	else
		return -EINVAL;

	if (taddr == OF_BAD_ADDR)
		return -EINVAL;
	memset(r, 0, sizeof(struct resource));

	if (of_mmio_is_nonposted(dev))
		flags |= IORESOURCE_MEM_NONPOSTED;

	r->start = taddr;
	r->end = taddr + size - 1;
	r->flags = flags;
	r->name = name ? name : dev->full_name;

	return 0;
}

/**
 * of_address_to_resource - Translate device tree address and return as resource
 * @dev:	Caller's Device Node
 * @index:	Index into the array
 * @r:		Pointer to resource array
 *
 * Note that if your address is a PIO address, the conversion will fail if
 * the physical address can't be internally converted to an IO token with
 * pci_address_to_pio(), that is because it's either called too early or it
 * can't be matched to any host bridge IO space
 */
int of_address_to_resource(struct device_node *dev, int index,
			   struct resource *r)
{
	return __of_address_to_resource(dev, index, -1, r);
}
EXPORT_SYMBOL_GPL(of_address_to_resource);

/**
 * of_iomap - Maps the memory mapped IO for a given device_node
 * @np:		the device whose io range will be mapped
 * @index:	index of the io range
 *
 * Returns a pointer to the mapped memory
 */
void __iomem *of_iomap(struct device_node *np, int index)
{
	struct resource res;

	if (of_address_to_resource(np, index, &res))
		return NULL;

	if (res.flags & IORESOURCE_MEM_NONPOSTED)
		return ioremap_np(res.start, resource_size(&res));
	else
		return ioremap(res.start, resource_size(&res));
}
EXPORT_SYMBOL(of_iomap);

/*
 * of_io_request_and_map - Requests a resource and maps the memory mapped IO
 *			   for a given device_node
 * @device:	the device whose io range will be mapped
 * @index:	index of the io range
 * @name:	name "override" for the memory region request or NULL
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

	if (!name)
		name = res.name;
	if (!request_mem_region(res.start, resource_size(&res), name))
		return IOMEM_ERR_PTR(-EBUSY);

	if (res.flags & IORESOURCE_MEM_NONPOSTED)
		mem = ioremap_np(res.start, resource_size(&res));
	else
		mem = ioremap(res.start, resource_size(&res));

	if (!mem) {
		release_mem_region(res.start, resource_size(&res));
		return IOMEM_ERR_PTR(-ENOMEM);
	}

	return mem;
}
EXPORT_SYMBOL(of_io_request_and_map);

#ifdef CONFIG_HAS_DMA
/**
 * of_dma_get_range - Get DMA range info and put it into a map array
 * @np:		device node to get DMA range info
 * @map:	dma range structure to return
 *
 * Look in bottom up direction for the first "dma-ranges" property
 * and parse it.  Put the information into a DMA offset map array.
 *
 * dma-ranges format:
 *	DMA addr (dma_addr)	: naddr cells
 *	CPU addr (phys_addr_t)	: pna cells
 *	size			: nsize cells
 *
 * It returns -ENODEV if "dma-ranges" property was not found for this
 * device in the DT.
 */
int of_dma_get_range(struct device_node *np, const struct bus_dma_region **map)
{
	struct device_node *node = of_node_get(np);
	const __be32 *ranges = NULL;
	bool found_dma_ranges = false;
	struct of_range_parser parser;
	struct of_range range;
	struct bus_dma_region *r;
	int len, num_ranges = 0;
	int ret = 0;

	while (node) {
		ranges = of_get_property(node, "dma-ranges", &len);

		/* Ignore empty ranges, they imply no translation required */
		if (ranges && len > 0)
			break;

		/* Once we find 'dma-ranges', then a missing one is an error */
		if (found_dma_ranges && !ranges) {
			ret = -ENODEV;
			goto out;
		}
		found_dma_ranges = true;

		node = of_get_next_dma_parent(node);
	}

	if (!node || !ranges) {
		pr_debug("no dma-ranges found for node(%pOF)\n", np);
		ret = -ENODEV;
		goto out;
	}

	of_dma_range_parser_init(&parser, node);
	for_each_of_range(&parser, &range) {
		if (range.cpu_addr == OF_BAD_ADDR) {
			pr_err("translation of DMA address(%llx) to CPU address failed node(%pOF)\n",
			       range.bus_addr, node);
			continue;
		}
		num_ranges++;
	}

	if (!num_ranges) {
		ret = -EINVAL;
		goto out;
	}

	r = kcalloc(num_ranges + 1, sizeof(*r), GFP_KERNEL);
	if (!r) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Record all info in the generic DMA ranges array for struct device,
	 * returning an error if we don't find any parsable ranges.
	 */
	*map = r;
	of_dma_range_parser_init(&parser, node);
	for_each_of_range(&parser, &range) {
		pr_debug("dma_addr(%llx) cpu_addr(%llx) size(%llx)\n",
			 range.bus_addr, range.cpu_addr, range.size);
		if (range.cpu_addr == OF_BAD_ADDR)
			continue;
		r->cpu_start = range.cpu_addr;
		r->dma_start = range.bus_addr;
		r->size = range.size;
		r->offset = range.cpu_addr - range.bus_addr;
		r++;
	}
out:
	of_node_put(node);
	return ret;
}
#endif /* CONFIG_HAS_DMA */

/**
 * of_dma_get_max_cpu_address - Gets highest CPU address suitable for DMA
 * @np: The node to start searching from or NULL to start from the root
 *
 * Gets the highest CPU physical address that is addressable by all DMA masters
 * in the sub-tree pointed by np, or the whole tree if NULL is passed. If no
 * DMA constrained device is found, it returns PHYS_ADDR_MAX.
 */
phys_addr_t __init of_dma_get_max_cpu_address(struct device_node *np)
{
	phys_addr_t max_cpu_addr = PHYS_ADDR_MAX;
	struct of_range_parser parser;
	phys_addr_t subtree_max_addr;
	struct device_node *child;
	struct of_range range;
	const __be32 *ranges;
	u64 cpu_end = 0;
	int len;

	if (!np)
		np = of_root;

	ranges = of_get_property(np, "dma-ranges", &len);
	if (ranges && len) {
		of_dma_range_parser_init(&parser, np);
		for_each_of_range(&parser, &range)
			if (range.cpu_addr + range.size > cpu_end)
				cpu_end = range.cpu_addr + range.size - 1;

		if (max_cpu_addr > cpu_end)
			max_cpu_addr = cpu_end;
	}

	for_each_available_child_of_node(np, child) {
		subtree_max_addr = of_dma_get_max_cpu_address(child);
		if (max_cpu_addr > subtree_max_addr)
			max_cpu_addr = subtree_max_addr;
	}

	return max_cpu_addr;
}

/**
 * of_dma_is_coherent - Check if device is coherent
 * @np:	device node
 *
 * It returns true if "dma-coherent" property was found
 * for this device in the DT, or if DMA is coherent by
 * default for OF devices on the current platform and no
 * "dma-noncoherent" property was found for this device.
 */
bool of_dma_is_coherent(struct device_node *np)
{
	struct device_node *node;
	bool is_coherent = IS_ENABLED(CONFIG_OF_DMA_DEFAULT_COHERENT);

	node = of_node_get(np);

	while (node) {
		if (of_property_read_bool(node, "dma-coherent")) {
			is_coherent = true;
			break;
		}
		if (of_property_read_bool(node, "dma-noncoherent")) {
			is_coherent = false;
			break;
		}
		node = of_get_next_dma_parent(node);
	}
	of_node_put(node);
	return is_coherent;
}
EXPORT_SYMBOL_GPL(of_dma_is_coherent);

/**
 * of_mmio_is_nonposted - Check if device uses non-posted MMIO
 * @np:	device node
 *
 * Returns true if the "nonposted-mmio" property was found for
 * the device's bus.
 *
 * This is currently only enabled on builds that support Apple ARM devices, as
 * an optimization.
 */
static bool of_mmio_is_nonposted(struct device_node *np)
{
	struct device_node *parent;
	bool nonposted;

	if (!IS_ENABLED(CONFIG_ARCH_APPLE))
		return false;

	parent = of_get_parent(np);
	if (!parent)
		return false;

	nonposted = of_property_read_bool(parent, "nonposted-mmio");

	of_node_put(parent);
	return nonposted;
}
