/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * Copyright (C) 2009 Vladimir Dronnikov <dronnikov@gmail.com>
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
//config:config FEATURE_VOLUMEID_BTRFS
//config:	bool "btrfs filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_BTRFS) += btrfs.o

#include "volume_id_internal.h"

#define BTRFS_UUID_SIZE 16
#define BTRFS_LABEL_SIZE 256
#define BTRFS_CSUM_SIZE 32
#define BTRFS_FSID_SIZE 16

#define BTRFS_MAGIC "_BHRfS_M"

struct btrfs_dev_item {
	uint64_t devid;
	uint64_t total_bytes;
	uint64_t bytes_used;
	uint32_t io_align;
	uint32_t io_width;
	uint32_t sector_size;
	uint64_t type;
	uint64_t generation;
	uint64_t start_offset;
	uint32_t dev_group;
	uint8_t seek_speed;
	uint8_t bandwidth;
	uint8_t uuid[BTRFS_UUID_SIZE];
	uint8_t fsid[BTRFS_UUID_SIZE];
} PACKED;

struct btrfs_super_block {
	uint8_t csum[BTRFS_CSUM_SIZE];
	uint8_t fsid[BTRFS_FSID_SIZE];	// UUID
	uint64_t bytenr;
	uint64_t flags;
	uint8_t magic[8];
	uint64_t generation;
	uint64_t root;
	uint64_t chunk_root;
	uint64_t log_root;
	uint64_t log_root_transid;
	uint64_t total_bytes;
	uint64_t bytes_used;
	uint64_t root_dir_objectid;
	uint64_t num_devices;
	uint32_t sectorsize;
	uint32_t nodesize;
	uint32_t leafsize;
	uint32_t stripesize;
	uint32_t sys_chunk_array_size;
	uint64_t chunk_root_generation;
	uint64_t compat_flags;
	uint64_t compat_ro_flags;
	uint64_t incompat_flags;
	uint16_t csum_type;
	uint8_t root_level;
	uint8_t chunk_root_level;
	uint8_t log_root_level;
	struct btrfs_dev_item dev_item;
	uint8_t label[BTRFS_LABEL_SIZE];	// LABEL
	// ...
} PACKED;

int FAST_FUNC volume_id_probe_btrfs(struct volume_id *id /*,uint64_t off*/)
{
	// btrfs has superblocks at 64K, 64M and 256G
	// minimum btrfs size is 256M
	// so we never step out the device if we analyze
	// the first and the second superblocks
	struct btrfs_super_block *sb;
	unsigned off = 64;

	while (off < 64*1024*1024) {
		off *= 1024;
		dbg("btrfs: probing at offset 0x%x", off);

		sb = volume_id_get_buffer(id, off, sizeof(*sb));
		if (sb == NULL)
			return -1;

		if (memcmp(sb->magic, BTRFS_MAGIC, 8) != 0)
			return -1;
	}

	// N.B.: btrfs natively supports 256 (>VOLUME_ID_LABEL_SIZE) size labels
	volume_id_set_label_string(id, sb->label, VOLUME_ID_LABEL_SIZE);
	volume_id_set_uuid(id, sb->fsid, UUID_DCE);
	IF_FEATURE_BLKID_TYPE(id->type = "btrfs";)

	return 0;
}
