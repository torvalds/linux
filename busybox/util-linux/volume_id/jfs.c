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
//config:config FEATURE_VOLUMEID_JFS
//config:	bool "jfs filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_JFS) += jfs.o

#include "volume_id_internal.h"

struct jfs_super_block {
	uint8_t		magic[4];
	uint32_t	version;
	uint64_t	size;
	uint32_t	bsize;
	uint32_t	dummy1;
	uint32_t	pbsize;
	uint32_t	dummy2[27];
	uint8_t		uuid[16];
	uint8_t		label[16];
	uint8_t		loguuid[16];
} PACKED;

#define JFS_SUPERBLOCK_OFFSET			0x8000

int FAST_FUNC volume_id_probe_jfs(struct volume_id *id /*,uint64_t off*/)
{
#define off ((uint64_t)0)
	struct jfs_super_block *js;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	js = volume_id_get_buffer(id, off + JFS_SUPERBLOCK_OFFSET, 0x200);
	if (js == NULL)
		return -1;

	if (memcmp(js->magic, "JFS1", 4) != 0)
		return -1;

//	volume_id_set_label_raw(id, js->label, 16);
	volume_id_set_label_string(id, js->label, 16);
	volume_id_set_uuid(id, js->uuid, UUID_DCE);

//	volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
	IF_FEATURE_BLKID_TYPE(id->type = "jfs";)

	return 0;
}
