/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * Copyright (C) 2012 S-G Bergh <sgb@systemasis.org>
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
//config:config FEATURE_VOLUMEID_NILFS
//config:	bool "nilfs filesystem"
//config:	default y
//config:	depends on VOLUMEID
//config:	help
//config:	NILFS is a New Implementation of a Log-Structured File System (LFS)
//config:	that supports continuous snapshots. This provides features like
//config:	versioning of the entire filesystem, restoration of files that
//config:	were deleted a few minutes ago. NILFS keeps consistency like
//config:	conventional LFS, so it provides quick recovery after system crashes.
//config:
//config:	The possible use of NILFS includes versioning, tamper detection,
//config:	SOX compliance logging, and so forth. It can serve as an alternative
//config:	filesystem for Linux desktop environment, or as a basis of advanced
//config:	storage appliances.

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_NILFS) += nilfs.o

#include "volume_id_internal.h"

#define NILFS_UUID_SIZE 16
#define NILFS_LABEL_SIZE 80
#define NILFS_SB1_OFFSET 0x400
#define NILFS_SB2_OFFSET 0x1000
#define NILFS_MAGIC 0x3434

struct nilfs2_super_block {
/* 0x00 */	uint32_t s_rev_level;				// Major revision level.
/* 0x04 */	uint16_t s_minor_rev_level;			// Minor revision level.
/* 0x06 */	uint16_t s_magic;				// Magic signature.
/* 0x08 */	uint16_t s_bytes;
/* 0x0A */	uint16_t s_flags;
/* 0x0C */	uint32_t s_crc_seed;
/* 0x10 */	uint32_t s_sum;
/* 0x14 */	uint32_t s_log_block_size;
/* 0x18 */	uint64_t s_nsegments;
/* 0x20 */	uint64_t s_dev_size;				// Block device size in bytes.
/* 0x28 */	uint64_t s_first_data_block;
/* 0x30 */	uint32_t s_blocks_per_segment;
/* 0x34 */	uint32_t s_r_segments_percentage;
/* 0x38 */	uint64_t s_last_cno;
/* 0x40 */	uint64_t s_last_pseg;
/* 0x48 */	uint64_t s_last_seq;
/* 0x50 */	uint64_t s_free_blocks_count;
/* 0x58 */	uint64_t s_ctime;
/* 0x60 */	uint64_t s_mtime;
/* 0x68 */	uint64_t s_wtime;
/* 0x70 */	uint16_t s_mnt_count;
/* 0x72 */	uint16_t s_max_mnt_count;
/* 0x74 */	uint16_t s_state;
/* 0x76 */	uint16_t s_errors;
/* 0x78 */	uint64_t s_lastcheck;
/* 0x80 */	uint32_t s_checkinterval;
/* 0x84 */	uint32_t s_creator_os;
/* 0x88 */	uint16_t s_def_resuid;
/* 0x8A */	uint16_t s_def_resgid;
/* 0x8C */	uint32_t s_first_ino;
/* 0x90 */	uint16_t s_inode_size;
/* 0x92 */	uint16_t s_dat_entry_size;
/* 0x94 */	uint16_t s_checkpoint_size;
/* 0x96 */	uint16_t s_segment_usage_size;
/* 0x98 */	uint8_t s_uuid[NILFS_UUID_SIZE];		// 128-bit UUID for volume.
/* 0xA8 */	uint8_t s_volume_name[NILFS_LABEL_SIZE];	// Volume label.
/* 0xF8 */	// ...
} PACKED;

int FAST_FUNC volume_id_probe_nilfs(struct volume_id *id /*,uint64_t off*/)
{
	struct nilfs2_super_block *sb;

	// Primary super block
	dbg("nilfs: probing at offset 0x%x", NILFS_SB1_OFFSET);

	sb = volume_id_get_buffer(id, NILFS_SB1_OFFSET, sizeof(*sb));

	if (sb == NULL)
		return -1;

	if (sb->s_magic != NILFS_MAGIC)
		return -1;

	// The secondary superblock is not always used, so ignore it for now.
	// When used it is at 4K from the end of the partition (sb->s_dev_size - NILFS_SB2_OFFSET).

	volume_id_set_label_string(id, sb->s_volume_name, NILFS_LABEL_SIZE < VOLUME_ID_LABEL_SIZE ?
				NILFS_LABEL_SIZE : VOLUME_ID_LABEL_SIZE);
	volume_id_set_uuid(id, sb->s_uuid, UUID_DCE);

	if (sb->s_rev_level == 2)
		IF_FEATURE_BLKID_TYPE(id->type = "nilfs2");

	return 0;
}
