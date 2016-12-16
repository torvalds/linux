/*
 * FDT Address translation based on u-boot fdt_support.c which in turn was
 * based on the kernel unflattened DT address translation code.
 *
 * (C) Copyright 2007
 * Gerald Van Baren, Custom IDEAS, vanbaren@cideas.com
 *
 * Copyright 2010-2011 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#define pr_fmt(fmt)	"OF: fdt: " fmt

#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/sizes.h>

/* Max address size we deal with */
#define OF_MAX_ADDR_CELLS	4
#define OF_CHECK_COUNTS(na, ns)	((na) > 0 && (na) <= OF_MAX_ADDR_CELLS && \
			(ns) > 0)

/* Debug utility */
#ifdef DEBUG
static void __init of_dump_addr(const char *s, const __be32 *addr, int na)
{
	pr_debug("%s", s);
	while(na--)
		pr_cont(" %08x", *(addr++));
	pr_cont("\n");
}
#else
static void __init of_dump_addr(const char *s, const __be32 *addr, int na) { }
#endif

/* Callbacks for bus specific translators */
struct of_bus {
	void		(*count_cells)(const void *blob, int parentoffset,
				int *addrc, int *sizec);
	u64		(*map)(__be32 *addr, const __be32 *range,
				int na, int ns, int pna);
	int		(*translate)(__be32 *addr, u64 offset, int na);
};

/* Default translator (generic bus) */
static void __init fdt_bus_default_count_cells(const void *blob, int parentoffset,
					       int *addrc, int *sizec)
{
	const __be32 *prop;

	if (addrc) {
		prop = fdt_getprop(blob, parentoffset, "#address-cells", NULL);
		if (prop)
			*addrc = be32_to_cpup(prop);
		else
			*addrc = dt_root_addr_cells;
	}

	if (sizec) {
		prop = fdt_getprop(blob, parentoffset, "#size-cells", NULL);
		if (prop)
			*sizec = be32_to_cpup(prop);
		else
			*sizec = dt_root_size_cells;
	}
}

static u64 __init fdt_bus_default_map(__be32 *addr, const __be32 *range,
				      int na, int ns, int pna)
{
	u64 cp, s, da;

	cp = of_read_number(range, na);
	s  = of_read_number(range + na + pna, ns);
	da = of_read_number(addr, na);

	pr_debug("default map, cp=%llx, s=%llx, da=%llx\n",
	    cp, s, da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int __init fdt_bus_default_translate(__be32 *addr, u64 offset, int na)
{
	u64 a = of_read_number(addr, na);
	memset(addr, 0, na * 4);
	a += offset;
	if (na > 1)
		addr[na - 2] = cpu_to_fdt32(a >> 32);
	addr[na - 1] = cpu_to_fdt32(a & 0xffffffffu);

	return 0;
}

/* Array of bus specific translators */
static const struct of_bus of_busses[] __initconst = {
	/* Default */
	{
		.count_cells = fdt_bus_default_count_cells,
		.map = fdt_bus_default_map,
		.translate = fdt_bus_default_translate,
	},
};

static int __init fdt_translate_one(const void *blob, int parent,
				    const struct of_bus *bus,
				    const struct of_bus *pbus, __be32 *addr,
				    int na, int ns, int pna, const char *rprop)
{
	const __be32 *ranges;
	int rlen;
	int rone;
	u64 offset = OF_BAD_ADDR;

	ranges = fdt_getprop(blob, parent, rprop, &rlen);
	if (!ranges)
		return 1;
	if (rlen == 0) {
		offset = of_read_number(addr, na);
		memset(addr, 0, pna * 4);
		pr_debug("empty ranges, 1:1 translation\n");
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
 */
static u64 __init fdt_translate_address(const void *blob, int node_offset)
{
	int parent, len;
	const struct of_bus *bus, *pbus;
	const __be32 *reg;
	__be32 addr[OF_MAX_ADDR_CELLS];
	int na, ns, pna, pns;
	u64 result = OF_BAD_ADDR;

	pr_debug("** translation for device %s **\n",
		 fdt_get_name(blob, node_offset, NULL));

	reg = fdt_getprop(blob, node_offset, "reg", &len);
	if (!reg) {
		pr_err("warning: device tree node '%s' has no address.\n",
			fdt_get_name(blob, node_offset, NULL));
		goto bail;
	}

	/* Get parent & match bus type */
	parent = fdt_parent_offset(blob, node_offset);
	if (parent < 0)
		goto bail;
	bus = &of_busses[0];

	/* Cound address cells & copy address locally */
	bus->count_cells(blob, parent, &na, &ns);
	if (!OF_CHECK_COUNTS(na, ns)) {
		pr_err("Bad cell count for %s\n",
		       fdt_get_name(blob, node_offset, NULL));
		goto bail;
	}
	memcpy(addr, reg, na * 4);

	pr_debug("bus (na=%d, ns=%d) on %s\n",
		 na, ns, fdt_get_name(blob, parent, NULL));
	of_dump_addr("translating address:", addr, na);

	/* Translate */
	for (;;) {
		/* Switch to parent bus */
		node_offset = parent;
		parent = fdt_parent_offset(blob, node_offset);

		/* If root, we have finished */
		if (parent < 0) {
			pr_debug("reached root node\n");
			result = of_read_number(addr, na);
			break;
		}

		/* Get new parent bus and counts */
		pbus = &of_busses[0];
		pbus->count_cells(blob, parent, &pna, &pns);
		if (!OF_CHECK_COUNTS(pna, pns)) {
			pr_err("Bad cell count for %s\n",
				fdt_get_name(blob, node_offset, NULL));
			break;
		}

		pr_debug("parent bus (na=%d, ns=%d) on %s\n",
			 pna, pns, fdt_get_name(blob, parent, NULL));

		/* Apply bus translation */
		if (fdt_translate_one(blob, node_offset, bus, pbus,
					addr, na, ns, pna, "ranges"))
			break;

		/* Complete the move up one level */
		na = pna;
		ns = pns;
		bus = pbus;

		of_dump_addr("one level translation:", addr, na);
	}
 bail:
	return result;
}

/**
 * of_flat_dt_translate_address - translate DT addr into CPU phys addr
 * @node: node in the flat blob
 */
u64 __init of_flat_dt_translate_address(unsigned long node)
{
	return fdt_translate_address(initial_boot_params, node);
}
