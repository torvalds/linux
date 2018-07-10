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
//config:### config FEATURE_VOLUMEID_SILICONRAID
//config:###	bool "silicon raid"
//config:###	default y
//config:###	depends on VOLUMEID

//kbuild:### lib-$(CONFIG_FEATURE_VOLUMEID_SILICONRAID) += silicon_raid.o

#include "volume_id_internal.h"

struct silicon_meta {
	uint8_t		unknown0[0x2E];
	uint8_t		ascii_version[0x36 - 0x2E];
	uint8_t		diskname[0x56 - 0x36];
	uint8_t		unknown1[0x60 - 0x56];
	uint32_t	magic;
	uint32_t	unknown1a[0x6C - 0x64];
	uint32_t	array_sectors_low;
	uint32_t	array_sectors_high;
	uint8_t		unknown2[0x78 - 0x74];
	uint32_t	thisdisk_sectors;
	uint8_t		unknown3[0x100 - 0x7C];
	uint8_t		unknown4[0x104 - 0x100];
	uint16_t	product_id;
	uint16_t	vendor_id;
	uint16_t	minor_ver;
	uint16_t	major_ver;
} PACKED;

#define SILICON_MAGIC		0x2F000000

int FAST_FUNC volume_id_probe_silicon_medley_raid(struct volume_id *id, uint64_t off, uint64_t size)
{
	uint64_t meta_off;
	struct silicon_meta *sil;

	dbg("probing at offset 0x%llx, size 0x%llx",
	    (unsigned long long) off, (unsigned long long) size);

	if (size < 0x10000)
		return -1;

	meta_off = ((size / 0x200)-1) * 0x200;
	sil = volume_id_get_buffer(id, off + meta_off, 0x200);
	if (sil == NULL)
		return -1;

	if (sil->magic != cpu_to_le32(SILICON_MAGIC))
		return -1;

//	volume_id_set_usage(id, VOLUME_ID_RAID);
//	snprintf(id->type_version, sizeof(id->type_version)-1, "%u.%u",
//		le16_to_cpu(sil->major_ver), le16_to_cpu(sil->minor_ver));
//	id->type = "silicon_medley_raid_member";

	return 0;
}
