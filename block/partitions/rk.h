/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  block/partitions/rk.h
 */

/* error message prefix */
#define ERRP "rkpart: "

/* debug macro */
#define RKPART_DEBUG 0
#if RKPART_DEBUG
#define dbg(x)  do {    \
printk("DEBUG-CMDLINE-PART: ");	\
printk x;       \
} while (0)
#else
#define dbg(x)
#endif

/* At least 1GB disk support*/
#define SECTOR_1G	0x200000

/* Default partition table offet got from loader: 4MB*/
#define FROM_OFFSET	0x2000

/* special size referring to all the remaining space in a partition */
#define SIZE_REMAINING UINT_MAX
#define OFFSET_CONTINUOUS UINT_MAX

struct rk_partition {
	char *name;
	sector_t from;
	sector_t size;
};
struct cmdline_rk_partition {
	struct cmdline_rk_partition *next;
	char *rk_id;
	int num_parts;
	struct rk_partition *parts;
};
int rkpart_partition(struct parsed_partitions *state);
