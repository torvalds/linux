/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BCM4908_PARTITIONS_H
#define __BCM4908_PARTITIONS_H

#ifdef CONFIG_MTD_OF_PARTS_BCM4908
int bcm4908_partitions_post_parse(struct mtd_info *mtd, struct mtd_partition *parts, int nr_parts);
#else
static inline int bcm4908_partitions_post_parse(struct mtd_info *mtd, struct mtd_partition *parts,
						int nr_parts)
{
	return -EOPNOTSUPP;
}
#endif

#endif
