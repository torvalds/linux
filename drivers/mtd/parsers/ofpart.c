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

static bool yesde_has_compatible(struct device_yesde *pp)
{
	return of_get_property(pp, "compatible", NULL);
}

static int parse_fixed_partitions(struct mtd_info *master,
				  const struct mtd_partition **pparts,
				  struct mtd_part_parser_data *data)
{
	struct mtd_partition *parts;
	struct device_yesde *mtd_yesde;
	struct device_yesde *ofpart_yesde;
	const char *partname;
	struct device_yesde *pp;
	int nr_parts, i, ret = 0;
	bool dedicated = true;


	/* Pull of_yesde from the master device yesde */
	mtd_yesde = mtd_get_of_yesde(master);
	if (!mtd_yesde)
		return 0;

	ofpart_yesde = of_get_child_by_name(mtd_yesde, "partitions");
	if (!ofpart_yesde) {
		/*
		 * We might get here even when ofpart isn't used at all (e.g.,
		 * when using ayesther parser), so don't be louder than
		 * KERN_DEBUG
		 */
		pr_debug("%s: 'partitions' subyesde yest found on %pOF. Trying to parse direct subyesdes as partitions.\n",
			 master->name, mtd_yesde);
		ofpart_yesde = mtd_yesde;
		dedicated = false;
	} else if (!of_device_is_compatible(ofpart_yesde, "fixed-partitions")) {
		/* The 'partitions' subyesde might be used by ayesther parser */
		return 0;
	}

	/* First count the subyesdes */
	nr_parts = 0;
	for_each_child_of_yesde(ofpart_yesde,  pp) {
		if (!dedicated && yesde_has_compatible(pp))
			continue;

		nr_parts++;
	}

	if (nr_parts == 0)
		return 0;

	parts = kcalloc(nr_parts, sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	i = 0;
	for_each_child_of_yesde(ofpart_yesde,  pp) {
		const __be32 *reg;
		int len;
		int a_cells, s_cells;

		if (!dedicated && yesde_has_compatible(pp))
			continue;

		reg = of_get_property(pp, "reg", &len);
		if (!reg) {
			if (dedicated) {
				pr_debug("%s: ofpart partition %pOF (%pOF) missing reg property.\n",
					 master->name, pp,
					 mtd_yesde);
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
				 mtd_yesde);
			goto ofpart_fail;
		}

		parts[i].offset = of_read_number(reg, a_cells);
		parts[i].size = of_read_number(reg + a_cells, s_cells);
		parts[i].of_yesde = pp;

		partname = of_get_property(pp, "label", &len);
		if (!partname)
			partname = of_get_property(pp, "name", &len);
		parts[i].name = partname;

		if (of_get_property(pp, "read-only", &len))
			parts[i].mask_flags |= MTD_WRITEABLE;

		if (of_get_property(pp, "lock", &len))
			parts[i].mask_flags |= MTD_POWERUP_LOCK;

		i++;
	}

	if (!nr_parts)
		goto ofpart_yesne;

	*pparts = parts;
	return nr_parts;

ofpart_fail:
	pr_err("%s: error parsing ofpart partition %pOF (%pOF)\n",
	       master->name, pp, mtd_yesde);
	ret = -EINVAL;
ofpart_yesne:
	of_yesde_put(pp);
	kfree(parts);
	return ret;
}

static const struct of_device_id parse_ofpart_match_table[] = {
	{ .compatible = "fixed-partitions" },
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
	struct device_yesde *dp;
	int i, plen, nr_parts;
	const struct {
		__be32 offset, len;
	} *part;
	const char *names;

	/* Pull of_yesde from the master device yesde */
	dp = mtd_get_of_yesde(master);
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
 * When MTD core canyest find the requested parser, it tries to load the module
 * with the same name. Since we provide the ofoldpart parser, we should have
 * the corresponding alias.
 */
MODULE_ALIAS("fixed-partitions");
MODULE_ALIAS("ofoldpart");
