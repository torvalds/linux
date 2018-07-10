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
//config:### config FEATURE_VOLUMEID_MAC
//config:###	bool "mac filesystem"
//config:###	default y
//config:###	depends on VOLUMEID

//kbuild:### lib-$(CONFIG_FEATURE_VOLUMEID_MAC) += mac.o

#include "volume_id_internal.h"

struct mac_driver_desc {
	uint8_t		signature[2];
	uint16_t	block_size;
	uint32_t	block_count;
} PACKED;

struct mac_partition {
	uint8_t		signature[2];
	uint16_t	res1;
	uint32_t	map_count;
	uint32_t	start_block;
	uint32_t	block_count;
	uint8_t		name[32];
	uint8_t		type[32];
} PACKED;

int FAST_FUNC volume_id_probe_mac_partition_map(struct volume_id *id, uint64_t off)
{
	const uint8_t *buf;
	struct mac_driver_desc *driver;
	struct mac_partition *part;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	buf = volume_id_get_buffer(id, off, 0x200);
	if (buf == NULL)
		return -1;

	part = (struct mac_partition *) buf;
	if (part->signature[0] == 'P' && part->signature[1] == 'M' /* "PM" */
	 && (memcmp(part->type, "Apple_partition_map", 19) == 0)
	) {
		/* linux creates an own subdevice for the map
		 * just return the type if the drive header is missing */
//		volume_id_set_usage(id, VOLUME_ID_PARTITIONTABLE);
//		id->type = "mac_partition_map";
		return 0;
	}

	driver = (struct mac_driver_desc *) buf;
	if (driver->signature[0] == 'E' && driver->signature[1] == 'R') { /* "ER" */
		/* we are on a main device, like a CD
		 * just try to probe the first partition from the map */
		unsigned bsize = be16_to_cpu(driver->block_size);
		int part_count;
		int i;

		/* get first entry of partition table */
		buf = volume_id_get_buffer(id, off +  bsize, 0x200);
		if (buf == NULL)
			return -1;

		part = (struct mac_partition *) buf;
		if (part->signature[0] != 'P' || part->signature[1] != 'M') /* not "PM" */
			return -1;

		part_count = be32_to_cpu(part->map_count);
		dbg("expecting %d partition entries", part_count);

		if (id->partitions != NULL)
			free(id->partitions);
		id->partitions = xzalloc(part_count * sizeof(struct volume_id_partition));

		id->partition_count = part_count;

		for (i = 0; i < part_count; i++) {
			uint64_t poff;
			uint64_t plen;

			buf = volume_id_get_buffer(id, off + ((i+1) * bsize), 0x200);
			if (buf == NULL)
				return -1;

			part = (struct mac_partition *) buf;
			if (part->signature[0] != 'P' || part->signature[1] != 'M') /* not "PM" */
				return -1;

			poff = be32_to_cpu(part->start_block) * bsize;
			plen = be32_to_cpu(part->block_count) * bsize;
			dbg("found '%s' partition entry at 0x%llx, len 0x%llx",
					part->type, (unsigned long long) poff,
					(unsigned long long) plen);

//			id->partitions[i].pt_off = poff;
//			id->partitions[i].pt_len = plen;

//			if (memcmp(part->type, "Apple_Free", 10) == 0) {
//				volume_id_set_usage_part(&id->partitions[i], VOLUME_ID_UNUSED);
//			} else if (memcmp(part->type, "Apple_partition_map", 19) == 0) {
//				volume_id_set_usage_part(&id->partitions[i], VOLUME_ID_PARTITIONTABLE);
//			} else {
//				volume_id_set_usage_part(&id->partitions[i], VOLUME_ID_UNPROBED);
//			}
		}
//		volume_id_set_usage(id, VOLUME_ID_PARTITIONTABLE);
//		id->type = "mac_partition_map";
		return 0;
	}

	return -1;
}
