/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2018 Sven-Göran Bergh <sgb@systemaxion.se>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FEATURE_VOLUMEID_LFS
//config:	bool "LittleFS filesystem"
//config:	default y
//config:	depends on VOLUMEID && FEATURE_BLKID_TYPE
//config:	help
//config:	LittleFS is a small fail-safe filesystem designed for embedded
//config:	systems. It has strong copy-on-write guarantees and storage on disk
//config:	is always kept in a valid state. It also provides a form of dynamic
//config:	wear levelling for systems that can not fit a full flash translation
//config:	layer.

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_LFS) += lfs.o

#include "volume_id_internal.h"

#define LFS_SB1_OFFSET		0x10
#define LFS_MAGIC_NAME		"littlefs"
#define LFS_MAGIC_LEN		8

// The superblock is stored in the first metadata pair, i.e the first two blocks.
struct lfs_super_block {	// A block in a metadata pair
// /* 0x00 */	uint32_t	rev_count;		// Revision count
// /* 0x04 */	uint32_t	dir_size;		// Directory size
// /* 0x08 */	uint64_t	tail_ptr;		// Tail pointer
/* 0x10 */	uint8_t		entry_type;		// Entry type
/* 0x11 */	uint8_t		entry_len;		// Entry length
/* 0x12 */	uint8_t		att_len;		// Attribute length
/* 0x13 */	uint8_t 	name_len;		// Name length
/* 0x14 */	uint64_t	root_dir;		// Root directory
/* 0x1C */	uint32_t	block_size;		// Block size
/* 0x20 */	uint32_t	block_count;		// Block count
/* 0x24 */	uint16_t	ver_major;		// Version major
/* 0x26 */	uint16_t	ver_minor;		// Version minor
/* 0x28 */	uint8_t		magic[LFS_MAGIC_LEN];	// Magic string "littlefs"
// /* 0x30 */	uint32_t	crc;			// CRC-32 checksum
} PACKED;

int FAST_FUNC volume_id_probe_lfs(struct volume_id *id /*,uint64_t off*/)
{
	struct lfs_super_block *sb;

	// Go for primary super block (ignore second sb)
	dbg("lfs: probing at offset 0x%x", LFS_SB1_OFFSET);
	sb = volume_id_get_buffer(id, LFS_SB1_OFFSET, sizeof(*sb));

	if (!sb)
		return -1;

	if (memcmp(sb->magic, LFS_MAGIC_NAME, LFS_MAGIC_LEN) != 0)
		return -1;

	IF_FEATURE_BLKID_TYPE(id->type = LFS_MAGIC_NAME);

	return 0;
}
