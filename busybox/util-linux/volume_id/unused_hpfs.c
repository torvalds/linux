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
//config:### config FEATURE_VOLUMEID_HPFS
//config:###	bool "hpfs filesystem"
//config:###	default y
//config:###	depends on VOLUMEID

//kbuild:### lib-$(CONFIG_FEATURE_VOLUMEID_HPFS) += hpfs.o

#include "volume_id_internal.h"

struct hpfs_super {
	uint8_t		magic[4];
	uint8_t		version;
} PACKED;

#define HPFS_SUPERBLOCK_OFFSET			0x2000

int FAST_FUNC volume_id_probe_hpfs(struct volume_id *id, uint64_t off)
{
	struct hpfs_super *hs;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	hs = volume_id_get_buffer(id, off + HPFS_SUPERBLOCK_OFFSET, 0x200);
	if (hs == NULL)
		return -1;

	if (memcmp(hs->magic, "\x49\xe8\x95\xf9", 4) == 0) {
//		sprintf(id->type_version, "%u", hs->version);
//		volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
//		id->type = "hpfs";
		return 0;
	}

	return -1;
}
