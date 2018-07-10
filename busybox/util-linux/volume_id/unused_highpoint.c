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
//config:### config FEATURE_VOLUMEID_HIGHPOINTRAID
//config:###	bool "highpoint raid"
//config:###	default y
//config:###	depends on VOLUMEID

//kbuild:### lib-$(CONFIG_FEATURE_VOLUMEID_HIGHPOINTRAID) += highpoint.o

#include "volume_id_internal.h"

struct hpt37x_meta {
	uint8_t		filler1[32];
	uint32_t	magic;
} PACKED;

struct hpt45x_meta {
	uint32_t	magic;
} PACKED;

#define HPT37X_CONFIG_OFF		0x1200
#define HPT37X_MAGIC_OK			0x5a7816f0
#define HPT37X_MAGIC_BAD		0x5a7816fd

#define HPT45X_MAGIC_OK			0x5a7816f3
#define HPT45X_MAGIC_BAD		0x5a7816fd


int FAST_FUNC volume_id_probe_highpoint_37x_raid(struct volume_id *id, uint64_t off)
{
	struct hpt37x_meta *hpt;
	uint32_t magic;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	hpt = volume_id_get_buffer(id, off + HPT37X_CONFIG_OFF, 0x200);
	if (hpt == NULL)
		return -1;

	magic = hpt->magic;
	if (magic != cpu_to_le32(HPT37X_MAGIC_OK) && magic != cpu_to_le32(HPT37X_MAGIC_BAD))
		return -1;

//	volume_id_set_usage(id, VOLUME_ID_RAID);
//	id->type = "highpoint_raid_member";

	return 0;
}

int FAST_FUNC volume_id_probe_highpoint_45x_raid(struct volume_id *id, uint64_t off, uint64_t size)
{
	struct hpt45x_meta *hpt;
	uint64_t meta_off;
	uint32_t magic;

	dbg("probing at offset 0x%llx, size 0x%llx",
	    (unsigned long long) off, (unsigned long long) size);

	if (size < 0x10000)
		return -1;

	meta_off = ((size / 0x200)-11) * 0x200;
	hpt = volume_id_get_buffer(id, off + meta_off, 0x200);
	if (hpt == NULL)
		return -1;

	magic = hpt->magic;
	if (magic != cpu_to_le32(HPT45X_MAGIC_OK) && magic != cpu_to_le32(HPT45X_MAGIC_BAD))
		return -1;

//	volume_id_set_usage(id, VOLUME_ID_RAID);
//	id->type = "highpoint_raid_member";

	return 0;
}
