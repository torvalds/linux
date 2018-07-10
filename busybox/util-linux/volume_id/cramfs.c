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
//config:config FEATURE_VOLUMEID_CRAMFS
//config:	bool "cramfs filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_CRAMFS) += cramfs.o

#include "volume_id_internal.h"

struct cramfs_super {
	uint32_t	magic;
	uint32_t	size;
	uint32_t	flags;
	uint32_t	future;
	uint8_t		signature[16];
	struct cramfs_info {
		uint32_t	crc;
		uint32_t	edition;
		uint32_t	blocks;
		uint32_t	files;
	} PACKED info;
	uint8_t		name[16];
} PACKED;

int FAST_FUNC volume_id_probe_cramfs(struct volume_id *id /*,uint64_t off*/)
{
#define off ((uint64_t)0)
	struct cramfs_super *cs;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	cs = volume_id_get_buffer(id, off, 0x200);
	if (cs == NULL)
		return -1;

	if (cs->magic == cpu_to_be32(0x453dcd28)) {
//		volume_id_set_label_raw(id, cs->name, 16);
		volume_id_set_label_string(id, cs->name, 16);

//		volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
		IF_FEATURE_BLKID_TYPE(id->type = "cramfs";)
		return 0;
	}

	return -1;
}
