/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2012 S-G Bergh <sgb@systemasis.org>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FEATURE_VOLUMEID_UBIFS
//config:	bool "UBIFS filesystem"
//config:	default y
//config:	depends on VOLUMEID
//config:	help
//config:	UBIFS (Unsorted Block Image File System) is a file
//config:	system for use with raw flash memory media.

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_UBIFS) += ubifs.o

#include "volume_id_internal.h"

#define UBIFS_NODE_MAGIC  0x06101831

/*
 * struct ubifs_ch - common header node.
 * @magic: UBIFS node magic number (%UBIFS_NODE_MAGIC)
 * @crc: CRC-32 checksum of the node header
 * @sqnum: sequence number
 * @len: full node length
 * @node_type: node type
 * @group_type: node group type
 * @padding: reserved for future, zeroes
 *
 * Every UBIFS node starts with this common part. If the node has a key, the
 * key always goes next.
 */
struct ubifs_ch {
	uint32_t magic;
	uint32_t crc;
	uint64_t sqnum;
	uint32_t len;
	uint8_t node_type;
	uint8_t group_type;
	uint8_t padding[2];
} PACKED;

/*
 * struct ubifs_sb_node - superblock node.
 * @ch: common header
 * @padding: reserved for future, zeroes
 * @key_hash: type of hash function used in keys
 * @key_fmt: format of the key
 * @flags: file-system flags (%UBIFS_FLG_BIGLPT, etc)
 * @min_io_size: minimal input/output unit size
 * @leb_size: logical eraseblock size in bytes
 * @leb_cnt: count of LEBs used by file-system
 * @max_leb_cnt: maximum count of LEBs used by file-system
 * @max_bud_bytes: maximum amount of data stored in buds
 * @log_lebs: log size in logical eraseblocks
 * @lpt_lebs: number of LEBs used for lprops table
 * @orph_lebs: number of LEBs used for recording orphans
 * @jhead_cnt: count of journal heads
 * @fanout: tree fanout (max. number of links per indexing node)
 * @lsave_cnt: number of LEB numbers in LPT's save table
 * @fmt_version: UBIFS on-flash format version
 * @default_compr: default compression algorithm (%UBIFS_COMPR_LZO, etc)
 * @padding1: reserved for future, zeroes
 * @rp_uid: reserve pool UID
 * @rp_gid: reserve pool GID
 * @rp_size: size of the reserved pool in bytes
 * @padding2: reserved for future, zeroes
 * @time_gran: time granularity in nanoseconds
 * @uuid: UUID generated when the file system image was created
 * @ro_compat_version: UBIFS R/O compatibility version
 */
struct ubifs_sb_node {
	struct ubifs_ch ch;
	uint8_t padding[2];
	uint8_t key_hash;
	uint8_t key_fmt;
	uint32_t flags;
	uint32_t min_io_size;
	uint32_t leb_size;
	uint32_t leb_cnt;
	uint32_t max_leb_cnt;
	uint64_t max_bud_bytes;
	uint32_t log_lebs;
	uint32_t lpt_lebs;
	uint32_t orph_lebs;
	uint32_t jhead_cnt;
	uint32_t fanout;
	uint32_t lsave_cnt;
	uint32_t fmt_version;
	uint16_t default_compr;
	uint8_t padding1[2];
	uint32_t rp_uid;
	uint32_t rp_gid;
	uint64_t rp_size;
	uint32_t time_gran;
	uint8_t uuid[16];
	uint32_t ro_compat_version;
/*
	uint8_t padding2[3968];
*/
} PACKED;

int FAST_FUNC volume_id_probe_ubifs(struct volume_id *id /*,uint64_t off*/)
{
#define off ((uint64_t)0)
	struct ubifs_sb_node *sb;

	dbg("UBIFS: probing at offset 0x%llx", (unsigned long long) off);
	sb = volume_id_get_buffer(id, off, sizeof(struct ubifs_sb_node));
	if (!sb)
		return -1;

	if (le32_to_cpu(sb->ch.magic) != UBIFS_NODE_MAGIC)
		return -1;

	IF_FEATURE_BLKID_TYPE(id->type = "ubifs";)
	volume_id_set_uuid(id, sb->uuid, UUID_DCE);

	return 0;
}
