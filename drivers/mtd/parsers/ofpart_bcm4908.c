// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/mtd/partitions.h>

#include "ofpart_bcm4908.h"

#define BLPARAMS_FW_OFFSET		"NAND_RFS_OFS"

static long long bcm4908_partitions_fw_offset(void)
{
	struct device_node *root;
	struct property *prop;
	const char *s;

	root = of_find_node_by_path("/");
	if (!root)
		return -ENOENT;

	of_property_for_each_string(root, "brcm_blparms", prop, s) {
		size_t len = strlen(BLPARAMS_FW_OFFSET);
		unsigned long offset;
		int err;

		if (strncmp(s, BLPARAMS_FW_OFFSET, len) || s[len] != '=')
			continue;

		err = kstrtoul(s + len + 1, 0, &offset);
		if (err) {
			pr_err("failed to parse %s\n", s + len + 1);
			return err;
		}

		return offset << 10;
	}

	return -ENOENT;
}

int bcm4908_partitions_post_parse(struct mtd_info *mtd, struct mtd_partition *parts, int nr_parts)
{
	long long fw_offset;
	int i;

	fw_offset = bcm4908_partitions_fw_offset();

	for (i = 0; i < nr_parts; i++) {
		if (of_device_is_compatible(parts[i].of_node, "brcm,bcm4908-firmware")) {
			if (fw_offset < 0 || parts[i].offset == fw_offset)
				parts[i].name = "firmware";
			else
				parts[i].name = "backup";
		}
	}

	return 0;
}
