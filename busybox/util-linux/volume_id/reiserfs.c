/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * Copyright (C) 2005 Tobias Klauser <tklauser@access.unizh.ch>
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
//config:config FEATURE_VOLUMEID_REISERFS
//config:	bool "Reiser filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_REISERFS) += reiserfs.o

#include "volume_id_internal.h"

struct reiserfs_super_block {
	uint32_t	blocks_count;
	uint32_t	free_blocks;
	uint32_t	root_block;
	uint32_t	journal_block;
	uint32_t	journal_dev;
	uint32_t	orig_journal_size;
	uint32_t	dummy2[5];
	uint16_t	blocksize;
	uint16_t	dummy3[3];
	uint8_t		magic[12];
	uint32_t	dummy4[5];
	uint8_t		uuid[16];
	uint8_t		label[16];
} PACKED;

struct reiser4_super_block {
	uint8_t		magic[16];
	uint16_t	dummy[2];
	uint8_t		uuid[16];
	uint8_t		label[16];
	uint64_t	dummy2;
} PACKED;

#define REISERFS1_SUPERBLOCK_OFFSET		0x2000
#define REISERFS_SUPERBLOCK_OFFSET		0x10000

int FAST_FUNC volume_id_probe_reiserfs(struct volume_id *id /*,uint64_t off*/)
{
#define off ((uint64_t)0)
	struct reiserfs_super_block *rs;
	struct reiser4_super_block *rs4;

	dbg("reiserfs: probing at offset 0x%llx", (unsigned long long) off);

	rs = volume_id_get_buffer(id, off + REISERFS_SUPERBLOCK_OFFSET, 0x200);
	if (rs == NULL)
		return -1;

	if (memcmp(rs->magic, "ReIsErFs", 8) == 0) {
		dbg("reiserfs: ReIsErFs, no label");
//		strcpy(id->type_version, "3.5");
		goto found;
	}
	if (memcmp(rs->magic, "ReIsEr2Fs", 9) == 0) {
		dbg("reiserfs: ReIsEr2Fs");
//		strcpy(id->type_version, "3.6");
		goto found_label;
	}
	if (memcmp(rs->magic, "ReIsEr3Fs", 9) == 0) {
		dbg("reiserfs: ReIsEr3Fs");
//		strcpy(id->type_version, "JR");
		goto found_label;
	}

	rs4 = (struct reiser4_super_block *) rs;
	if (memcmp(rs4->magic, "ReIsEr4", 7) == 0) {
//		strcpy(id->type_version, "4");
//		volume_id_set_label_raw(id, rs4->label, 16);
		volume_id_set_label_string(id, rs4->label, 16);
		volume_id_set_uuid(id, rs4->uuid, UUID_DCE);
		dbg("reiserfs: ReIsEr4, label '%s' uuid '%s'", id->label, id->uuid);
		goto found;
	}

	rs = volume_id_get_buffer(id, off + REISERFS1_SUPERBLOCK_OFFSET, 0x200);
	if (rs == NULL)
		return -1;

	if (memcmp(rs->magic, "ReIsErFs", 8) == 0) {
		dbg("reiserfs: ReIsErFs, no label");
//		strcpy(id->type_version, "3.5");
		goto found;
	}

	dbg("reiserfs: no signature found");
	return -1;

 found_label:
//	volume_id_set_label_raw(id, rs->label, 16);
	volume_id_set_label_string(id, rs->label, 16);
	volume_id_set_uuid(id, rs->uuid, UUID_DCE);
	dbg("reiserfs: label '%s' uuid '%s'", id->label, id->uuid);

 found:
//	volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
	IF_FEATURE_BLKID_TYPE(id->type = "reiserfs";)

	return 0;
}
