/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 *
 *	This library is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public
 *	License as published by the Free Software Foundation; either
 *	version 2.1 of the License, or (at your option) any later version.
 *
 *	This library is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *	Lesser General Public License for more details.
 *
 *	You should have received a copy of the GNU Lesser General Public
 *	License along with this library; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
//config:### config FEATURE_VOLUMEID_MSDOS
//config:###	bool "msdos filesystem"
//config:###	default y
//config:###	depends on VOLUMEID

//kbuild:### lib-$(CONFIG_FEATURE_VOLUMEID_MSDOS) += msdos.o

#include "volume_id_internal.h"

struct msdos_partition_entry {
	uint8_t		boot_ind;
	uint8_t		head;
	uint8_t		sector;
	uint8_t		cyl;
	uint8_t		sys_ind;
	uint8_t		end_head;
	uint8_t		end_sector;
	uint8_t		end_cyl;
	uint32_t	start_sect;
	uint32_t	nr_sects;
} PACKED;

#define MSDOS_PARTTABLE_OFFSET		0x1be
#define MSDOS_SIG_OFF			0x1fe
#define BSIZE				0x200
#define DOS_EXTENDED_PARTITION		0x05
#define LINUX_EXTENDED_PARTITION	0x85
#define WIN98_EXTENDED_PARTITION	0x0f
#define LINUX_RAID_PARTITION		0xfd
#define is_extended(type) \
	(type == DOS_EXTENDED_PARTITION ||	\
	 type == WIN98_EXTENDED_PARTITION ||	\
	 type == LINUX_EXTENDED_PARTITION)
#define is_raid(type) \
	(type == LINUX_RAID_PARTITION)

int FAST_FUNC volume_id_probe_msdos_part_table(struct volume_id *id, uint64_t off)
{
	const uint8_t *buf;
	int i;
	uint64_t poff;
	uint64_t plen;
	uint64_t extended = 0;
	uint64_t current;
	uint64_t next;
	int limit;
	int empty = 1;
	struct msdos_partition_entry *part;
	struct volume_id_partition *p;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	buf = volume_id_get_buffer(id, off, 0x200);
	if (buf == NULL)
		return -1;

	if (buf[MSDOS_SIG_OFF] != 0x55 || buf[MSDOS_SIG_OFF + 1] != 0xaa)
		return -1;

	/* check flags on all entries for a valid partition table */
	part = (struct msdos_partition_entry*) &buf[MSDOS_PARTTABLE_OFFSET];
	for (i = 0; i < 4; i++) {
		if (part[i].boot_ind != 0
		 && part[i].boot_ind != 0x80
		) {
			return -1;
		}

		if (part[i].nr_sects != 0)
			empty = 0;
	}
	if (empty == 1)
		return -1;

	if (id->partitions != NULL)
		free(id->partitions);
	id->partitions = xzalloc(VOLUME_ID_PARTITIONS_MAX *
				sizeof(struct volume_id_partition));

	for (i = 0; i < 4; i++) {
		poff = (uint64_t) le32_to_cpu(part[i].start_sect) * BSIZE;
		plen = (uint64_t) le32_to_cpu(part[i].nr_sects) * BSIZE;

		if (plen == 0)
			continue;

		p = &id->partitions[i];

//		p->pt_type_raw = part[i].sys_ind;

		if (is_extended(part[i].sys_ind)) {
			dbg("found extended partition at 0x%llx", (unsigned long long) poff);
//			volume_id_set_usage_part(p, VOLUME_ID_PARTITIONTABLE);
//			p->type = "msdos_extended_partition";
			if (extended == 0)
				extended = off + poff;
		} else {
			dbg("found 0x%x data partition at 0x%llx, len 0x%llx",
				part[i].sys_ind, (unsigned long long) poff, (unsigned long long) plen);

//			if (is_raid(part[i].sys_ind))
//				volume_id_set_usage_part(p, VOLUME_ID_RAID);
//			else
//				volume_id_set_usage_part(p, VOLUME_ID_UNPROBED);
		}

//		p->pt_off = off + poff;
//		p->pt_len = plen;
		id->partition_count = i+1;
	}

	next = extended;
	current = extended;
	limit = 50;

	/* follow extended partition chain and add data partitions */
	while (next != 0) {
		if (limit-- == 0) {
			dbg("extended chain limit reached");
			break;
		}

		buf = volume_id_get_buffer(id, current, 0x200);
		if (buf == NULL)
			break;

		part = (struct msdos_partition_entry*) &buf[MSDOS_PARTTABLE_OFFSET];

		if (buf[MSDOS_SIG_OFF] != 0x55 || buf[MSDOS_SIG_OFF + 1] != 0xaa)
			break;

		next = 0;

		for (i = 0; i < 4; i++) {
			poff = (uint64_t) le32_to_cpu(part[i].start_sect) * BSIZE;
			plen = (uint64_t) le32_to_cpu(part[i].nr_sects) * BSIZE;

			if (plen == 0)
				continue;

			if (is_extended(part[i].sys_ind)) {
				dbg("found extended partition at 0x%llx", (unsigned long long) poff);
				if (next == 0)
					next = extended + poff;
			} else {
				dbg("found 0x%x data partition at 0x%llx, len 0x%llx",
					part[i].sys_ind, (unsigned long long) poff, (unsigned long long) plen);

				/* we always start at the 5th entry */
//				while (id->partition_count < 4)
//					volume_id_set_usage_part(&id->partitions[id->partition_count++], VOLUME_ID_UNUSED);
				if (id->partition_count < 4)
					id->partition_count = 4;

//				p = &id->partitions[id->partition_count];

//				if (is_raid(part[i].sys_ind))
//					volume_id_set_usage_part(p, VOLUME_ID_RAID);
//				else
//					volume_id_set_usage_part(p, VOLUME_ID_UNPROBED);

//				p->pt_off = current + poff;
//				p->pt_len = plen;
				id->partition_count++;

//				p->pt_type_raw = part[i].sys_ind;

				if (id->partition_count >= VOLUME_ID_PARTITIONS_MAX) {
					dbg("too many partitions");
					next = 0;
				}
			}
		}

		current = next;
	}

//	volume_id_set_usage(id, VOLUME_ID_PARTITIONTABLE);
//	id->type = "msdos_partition_table";

	return 0;
}
