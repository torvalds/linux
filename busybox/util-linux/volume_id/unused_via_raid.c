/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2005 Kay Sievers <kay.sievers@vrfy.org>
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
//config:### config FEATURE_VOLUMEID_VIARAID
//config:###	bool "via raid"
//config:###	default y
//config:###	depends on VOLUMEID

//kbuild:### lib-$(CONFIG_FEATURE_VOLUMEID_VIARAID) += via_raid.o

#include "volume_id_internal.h"

struct via_meta {
	uint16_t	signature;
	uint8_t		version_number;
	struct via_array {
		uint16_t	disk_bits;
		uint8_t		disk_array_ex;
		uint32_t	capacity_low;
		uint32_t	capacity_high;
		uint32_t	serial_checksum;
	} PACKED array;
	uint32_t	serial_checksum[8];
	uint8_t		checksum;
} PACKED;

#define VIA_SIGNATURE		0xAA55

int FAST_FUNC volume_id_probe_via_raid(struct volume_id *id, uint64_t off, uint64_t size)
{
	uint64_t meta_off;
	struct via_meta *via;

	dbg("probing at offset 0x%llx, size 0x%llx",
	    (unsigned long long) off, (unsigned long long) size);

	if (size < 0x10000)
		return -1;

	meta_off = ((size / 0x200)-1) * 0x200;

	via = volume_id_get_buffer(id, off + meta_off, 0x200);
	if (via == NULL)
		return -1;

	if (via->signature != cpu_to_le16(VIA_SIGNATURE))
		return -1;

	if (via->version_number > 1)
		return -1;

//	volume_id_set_usage(id, VOLUME_ID_RAID);
//	id->type_version[0] = '0' + via->version_number;
//	id->type_version[1] = '\0';
//	id->type = "via_raid_member";

	return 0;
}
