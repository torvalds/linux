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
//config:config FEATURE_VOLUMEID_XFS
//config:	bool "xfs filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_XFS) += xfs.o

#include "volume_id_internal.h"

struct xfs_super_block {
	uint8_t	magic[4];
	uint32_t	blocksize;
	uint64_t	dblocks;
	uint64_t	rblocks;
	uint32_t	dummy1[2];
	uint8_t	uuid[16];
	uint32_t	dummy2[15];
	uint8_t	fname[12];
	uint32_t	dummy3[2];
	uint64_t	icount;
	uint64_t	ifree;
	uint64_t	fdblocks;
} PACKED;

int FAST_FUNC volume_id_probe_xfs(struct volume_id *id /*,uint64_t off*/)
{
#define off ((uint64_t)0)
	struct xfs_super_block *xs;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	xs = volume_id_get_buffer(id, off, 0x200);
	if (xs == NULL)
		return -1;

	if (memcmp(xs->magic, "XFSB", 4) != 0)
		return -1;

//	volume_id_set_label_raw(id, xs->fname, 12);
	volume_id_set_label_string(id, xs->fname, 12);
	volume_id_set_uuid(id, xs->uuid, UUID_DCE);

//	volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
	IF_FEATURE_BLKID_TYPE(id->type = "xfs";)

	return 0;
}
