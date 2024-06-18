// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/bcm47xx_nvram.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include "ofpart_linksys_ns.h"

#define NVRAM_BOOT_PART		"bootpartition"

static int ofpart_linksys_ns_bootpartition(void)
{
	char buf[4];
	int bootpartition;

	/* Check CFE environment variable */
	if (bcm47xx_nvram_getenv(NVRAM_BOOT_PART, buf, sizeof(buf)) > 0) {
		if (!kstrtoint(buf, 0, &bootpartition))
			return bootpartition;
		pr_warn("Failed to parse %s value \"%s\"\n", NVRAM_BOOT_PART,
			buf);
	} else {
		pr_warn("Failed to get NVRAM \"%s\"\n", NVRAM_BOOT_PART);
	}

	return 0;
}

int linksys_ns_partitions_post_parse(struct mtd_info *mtd,
				     struct mtd_partition *parts,
				     int nr_parts)
{
	int bootpartition = ofpart_linksys_ns_bootpartition();
	int trx_idx = 0;
	int i;

	for (i = 0; i < nr_parts; i++) {
		if (of_device_is_compatible(parts[i].of_node, "linksys,ns-firmware")) {
			if (trx_idx++ == bootpartition)
				parts[i].name = "firmware";
			else
				parts[i].name = "backup";
		}
	}

	return 0;
}
