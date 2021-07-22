// SPDX-License-Identifier: GPL-2.0
/*
 *  fs/partitions/karma.c
 *  Rio Karma partition info.
 *
 *  Copyright (C) 2006 Bob Copeland (me@bobcopeland.com)
 *  based on osf.c
 */

#include "check.h"
#include <linux/compiler.h>

#define KARMA_LABEL_MAGIC		0xAB56

int karma_partition(struct parsed_partitions *state)
{
	int i;
	int slot = 1;
	Sector sect;
	unsigned char *data;
	struct disklabel {
		u8 d_reserved[270];
		struct d_partition {
			__le32 p_res;
			u8 p_fstype;
			u8 p_res2[3];
			__le32 p_offset;
			__le32 p_size;
		} d_partitions[2];
		u8 d_blank[208];
		__le16 d_magic;
	} __packed *label;
	struct d_partition *p;

	data = read_part_sector(state, 0, &sect);
	if (!data)
		return -1;

	label = (struct disklabel *)data;
	if (le16_to_cpu(label->d_magic) != KARMA_LABEL_MAGIC) {
		put_dev_sector(sect);
		return 0;
	}

	p = label->d_partitions;
	for (i = 0 ; i < 2; i++, p++) {
		if (slot == state->limit)
			break;

		if (p->p_fstype == 0x4d && le32_to_cpu(p->p_size)) {
			put_partition(state, slot, le32_to_cpu(p->p_offset),
				le32_to_cpu(p->p_size));
		}
		slot++;
	}
	strlcat(state->pp_buf, "\n", PAGE_SIZE);
	put_dev_sector(sect);
	return 1;
}

