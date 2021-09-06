// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Flash partitions described by the OF (or flattened) device tree
 *
 * Copyright © 2006 MontaVista Software Inc.
 * Author: Vitaly Wool <vwool@ru.mvista.com>
 *
 * Revised to handle newer style flash binding by:
 *   Copyright © 2007 David Gibson, IBM Corporation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/mtd/partitions.h>

#include "ofpart_bcm4908.h"
#include "ofpart_linksys_ns.h"

struct fixed_partitions_quirks {
	int (*post_parse)(struct mtd_info *mtd, struct mtd_partition *parts, int nr_parts);
};

static struct fixed_partitions_quirks bcm4908_partitions_quirks = {
	.post_parse = bcm4908_partitions_post_parse,
};

static struct fixed_partitions_quirks linksys_ns_partitions_quirks = {
	.post_parse = linksys_ns_partitions_post_parse,
};

static const struct of_device_id parse_ofpart_match_table[];

static bool node_has_compatible(struct device_node *pp)
{
	return of_get_property(pp, "compatible", NULL);
}

static int parse_fixed_partitions(struct mtd_info *master,
				  const struct mtd_partition **pparts,
				  struct mtd_part_parser_data *data)
{
	const struct fixed_partitions_quirks *quirks;
	const struct of_device_id *of_id;
	struct mtd_partition *parts;
	struct device_node *mtd_node;
	struct device_node *ofpart_node;
	const char *partname;
	struct device_node *pp;
	int nr_parts, i, ret = 0;
	bool dedicated = true;

	/* Pull of_node from the master device node */
	mtd_node = mtd_get_of_node(master);
	if (!mtd_node)
		return 0;

	if (!master->parent) { /* Master */
		ofpart_node = of_get_child_by_name(mtd_node, "partitions");
		if (!ofpart_node) {
			/*
			 * We might get here even when ofpart isn't used at all (e.g.,
			 * when using another parser), so don't be louder than
			 * KERN_DEBUG
			 */
			pr_debug("%s: 'partitions' subnode not found on %pOF. Trying to parse direct subnodes as partitions.\n",
				master->name, mtd_node);
			ofpart_node = mtd_node;
			dedicated = false;
		}
	} else { /* Partition */
		ofpart_node = mtd_node;
	}

	of_id = of_match_node(parse_ofpart_match_table, ofpart_node);
	if (dedicated && !of_id) {
		/* The 'partitions' subnode might be used by another parser */
		return 0;
	}

	quirks = of_id ? of_id->data : NULL;

	/* First count the subnodes */
	nr_parts = 0;
	for_each_child_of_node(ofpart_node,  pp) {
		if (!dedicated && node_has_compatible(pp))
			continue;

		nr_parts++;
	}

	if (nr_parts == 0)
		return 0;

	parts = kcalloc(nr_parts, sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	i = 0;
	for_each_child_of_node(ofpart_node,  pp) {
		const __be32 *reg;
		int len;
		int a_cells, s_cells;

		if (!dedicated && node_has_compatible(pp))
			continue;

		reg = of_get_property(pp, "reg", &len);
		if (!reg) {
			if (dedicated) {
				pr_debug("%s: ofpart partition %pOF (%pOF) missing reg property.\n",
					 master->name, pp,
					 mtd_node);
				goto ofpart_fail;
			} else {
				nr_parts--;
				continue;
			}
		}

		a_cells = of_n_addr_cells(pp);
		s_cells = of_n_size_cells(pp);
		if (len / 4 != a_cells + s_cells) {
			pr_debug("%s: ofpart partition %pOF (%pOF) error parsing reg property.\n",
				 master->name, pp,
				 mtd_node);
			goto ofpart_fail;
		}

		parts[i].offset = of_read_number(reg, a_cells);
		parts[i].size = of_read_number(reg + a_cells, s_cells);
		parts[i].of_node = pp;

		partname = of_get_property(pp, "label", &len);
		if (!partname)
			partname = of_get_property(pp, "name", &len);
		parts[i].name = partname;

		if (of_get_property(pp, "read-only", &len))
			parts[i].mask_flags |= MTD_WRITEABLE;

		if (of_get_property(pp, "lock", &len))
			parts[i].mask_flags |= MTD_POWERUP_LOCK;

		if (of_property_read_bool(pp, "slc-mode"))
			parts[i].add_flags |= MTD_SLC_ON_MLC_EMULATION;

		i++;
	}

	if (!nr_parts)
		goto ofpart_none;

	if (quirks && quirks->post_parse)
		quirks->post_parse(master, parts, nr_parts);

	*pparts = parts;
	return nr_parts;

ofpart_fail:
	pr_err("%s: error parsing ofpart partition %pOF (%pOF)\n",
	       master->name, pp, mtd_node);
	ret = -EINVAL;
ofpart_none:
	of_node_put(pp);
	kfree(parts);
	return ret;
}

static const struct of_device_id parse_ofpart_match_table[] = {
	/* Generic */
	{ .compatible = "fixed-partitions" },
	/* Customized */
	{ .compatible = "brcm,bcm4908-partitions", .data = &bcm4908_partitions_quirks, },
	{ .compatible = "linksys,ns-partitions", .data = &linksys_ns_partitions_quirks, },
	{},
};
MODULE_DEVICE_TABLE(of, parse_ofpart_match_table);

static struct mtd_part_parser ofpart_parser = {
	.parse_fn = parse_fixed_partitions,
	.name = "fixed-partitions",
	.of_match_table = parse_ofpart_match_table,
};

static int parse_ofoldpart_partitions(struct mtd_info *master,
				      const struct mtd_partition **pparts,
				      struct mtd_part_parser_data *data)
{
	struct mtd_partition *parts;
	struct device_node *dp;
	int i, plen, nr_parts;
	const struct {
		__be32 offset, len;
	} *part;
	const char *names;

	/* Pull of_node from the master device node */
	dp = mtd_get_of_node(master);
	if (!dp)
		return 0;

	part = of_get_property(dp, "partitions", &plen);
	if (!part)
		return 0; /* No partitions found */

	pr_warn("Device tree uses obsolete partition map binding: %pOF\n", dp);

	nr_parts = plen / sizeof(part[0]);

	parts = kcalloc(nr_parts, sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	names = of_get_property(dp, "partition-names", &plen);

	for (i = 0; i < nr_parts; i++) {
		parts[i].offset = be32_to_cpu(part->offset);
		parts[i].size   = be32_to_cpu(part->len) & ~1;
		/* bit 0 set signifies read only partition */
		if (be32_to_cpu(part->len) & 1)
			parts[i].mask_flags = MTD_WRITEABLE;

		if (names && (plen > 0)) {
			int len = strlen(names) + 1;

			parts[i].name = names;
			plen -= len;
			names += len;
		} else {
			parts[i].name = "unnamed";
		}

		part++;
	}

	*pparts = parts;
	return nr_parts;
}

static struct mtd_part_parser ofoldpart_parser = {
	.parse_fn = parse_ofoldpart_partitions,
	.name = "ofoldpart",
};

static int __init ofpart_parser_init(void)
{
	register_mtd_parser(&ofpart_parser);
	register_mtd_parser(&ofoldpart_parser);
	return 0;
}

static void __exit ofpart_parser_exit(void)
{
	deregister_mtd_parser(&ofpart_parser);
	deregister_mtd_parser(&ofoldpart_parser);
}

module_init(ofpart_parser_init);
module_exit(ofpart_parser_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Parser for MTD partitioning information in device tree");
MODULE_AUTHOR("Vitaly Wool, David Gibson");
/*
 * When MTD core cannot find the requested parser, it tries to load the module
 * with the same name. Since we provide the ofoldpart parser, we should have
 * the corresponding alias.
 */
MODULE_ALIAS("fixed-partitions");
MODULE_ALIAS("ofoldpart");
