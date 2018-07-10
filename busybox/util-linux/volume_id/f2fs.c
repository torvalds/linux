/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2012 S-G Bergh <sgb@systemasis.org>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FEATURE_VOLUMEID_F2FS
//config:	bool "f2fs filesystem"
//config:	default y
//config:	depends on VOLUMEID
//config:	help
//config:	F2FS (aka Flash-Friendly File System) is a log-structured file system,
//config:	which is adapted to newer forms of storage. F2FS also remedies some
//config:	known issues of the older log structured file systems, such as high
//config:	cleaning overhead.

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_F2FS) += f2fs.o

#include "volume_id_internal.h"

#define F2FS_MAGIC		0xF2F52010	// F2FS Magic Number
#define F2FS_UUID_SIZE		16
#define F2FS_LABEL_SIZE		512
#define F2FS_LABEL_BYTES	1024
#define F2FS_SB1_OFFSET		0x400		// offset for 1:st super block
/*
#define F2FS_SB2_OFFSET		0x1400		// offset for 2:nd super block
*/

struct f2fs_super_block {	// According to version 1.1
/* 0x00 */	uint32_t	magic;			// Magic Number
/* 0x04 */	uint16_t	major_ver;		// Major Version
/* 0x06 */	uint16_t	minor_ver;		// Minor Version
/* 0x08 */	uint32_t	log_sectorsize;		// log2 sector size in bytes
/* 0x0C */	uint32_t	log_sectors_per_block;	// log2 # of sectors per block
/* 0x10 */	uint32_t	log_blocksize;		// log2 block size in bytes
/* 0x14 */	uint32_t	log_blocks_per_seg;	// log2 # of blocks per segment
/* 0x18 */	uint32_t	segs_per_sec;		// # of segments per section
/* 0x1C */	uint32_t	secs_per_zone;		// # of sections per zone
/* 0x20 */	uint32_t	checksum_offset;	// checksum offset inside super block
/* 0x24 */	uint64_t	block_count;		// total # of user blocks
/* 0x2C */	uint32_t	section_count;		// total # of sections
/* 0x30 */	uint32_t	segment_count;		// total # of segments
/* 0x34 */	uint32_t	segment_count_ckpt;	// # of segments for checkpoint
/* 0x38 */	uint32_t	segment_count_sit;	// # of segments for SIT
/* 0x3C */	uint32_t	segment_count_nat;	// # of segments for NAT
/* 0x40 */	uint32_t	segment_count_ssa;	// # of segments for SSA
/* 0x44 */	uint32_t	segment_count_main;	// # of segments for main area
/* 0x48 */	uint32_t	segment0_blkaddr;	// start block address of segment 0
/* 0x4C */	uint32_t	cp_blkaddr;		// start block address of checkpoint
/* 0x50 */	uint32_t	sit_blkaddr;		// start block address of SIT
/* 0x54 */	uint32_t	nat_blkaddr;		// start block address of NAT
/* 0x58 */	uint32_t	ssa_blkaddr;		// start block address of SSA
/* 0x5C */	uint32_t	main_blkaddr;		// start block address of main area
/* 0x60 */	uint32_t	root_ino;		// root inode number
/* 0x64 */	uint32_t	node_ino;		// node inode number
/* 0x68 */	uint32_t	meta_ino;		// meta inode number
/* 0x6C */	uint8_t		uuid[F2FS_UUID_SIZE];	// 128-bit uuid for volume
/* 0x7C */	uint16_t	volume_name[F2FS_LABEL_SIZE];	// volume name
// /* 0x47C */	uint32_t	extension_count;	// # of extensions below
// /* 0x480 */	uint8_t		extension_list[64][8];	// extension array
} PACKED;


int FAST_FUNC volume_id_probe_f2fs(struct volume_id *id /*,uint64_t off*/)
{
	struct f2fs_super_block *sb;

	// Go for primary super block (ignore second sb)
	dbg("f2fs: probing at offset 0x%x", F2FS_SB1_OFFSET);
	sb = volume_id_get_buffer(id, F2FS_SB1_OFFSET, sizeof(*sb));

	if (!sb)
		return -1;

	if (sb->magic != cpu_to_le32(F2FS_MAGIC))
		return -1;

	IF_FEATURE_BLKID_TYPE(id->type = "f2fs");

	// For version 1.0 we don't know sb structure and can't set label/uuid
	if (sb->major_ver == cpu_to_le16(1) && sb->minor_ver == cpu_to_le16(0))
		return 0;

	volume_id_set_label_unicode16(id, (uint8_t *)sb->volume_name,
				      LE, MIN(F2FS_LABEL_BYTES, VOLUME_ID_LABEL_SIZE));

	volume_id_set_uuid(id, sb->uuid, UUID_DCE);

	return 0;
}
