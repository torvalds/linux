/*
 * volume_id - reads filesystem label and uuid
 *
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
//config:config FEATURE_VOLUMEID_EXFAT
//config:	bool "exFAT filesystem"
//config:	default y
//config:	depends on VOLUMEID
//config:	help
//config:	exFAT (extended FAT) is a proprietary file system designed especially
//config:	for flash drives. It has many features from NTFS, but with less
//config:	overhead. exFAT is used on most SDXC cards for consumer electronics.

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_EXFAT) += exfat.o

#include "volume_id_internal.h"

#define EXFAT_SB_OFFSET		0
#define EXFAT_DIR_ENTRY_SZ	32
#define EXFAT_MAX_DIR_ENTRIES	100

struct exfat_super_block {
/* 0x00 */	uint8_t		boot_jump[3];
/* 0x03 */	uint8_t		fs_name[8];
/* 0x0B */	uint8_t		must_be_zero[53];
/* 0x40 */	uint64_t	partition_offset;
/* 0x48 */	uint64_t	volume_length;
/* 0x50 */	uint32_t	fat_offset;		// Sector address of 1st FAT
/* 0x54 */	uint32_t	fat_size;		// In sectors
/* 0x58 */	uint32_t	cluster_heap_offset;	// Sector address of Data Region
/* 0x5C */	uint32_t	cluster_count;
/* 0x60 */	uint32_t	root_dir;		// Cluster address of Root Directory
/* 0x64 */	uint8_t		vol_serial_nr[4];	// Volume ID
/* 0x68 */	uint16_t	fs_revision;		// VV.MM
/* 0x6A */	uint16_t	vol_flags;
/* 0x6C */	uint8_t		bytes_per_sector;	// Power of 2: 9 => 512, 12 => 4096
/* 0x6D */	uint8_t		sectors_per_cluster;	// Power of 2
/* 0x6E */	uint8_t		nr_of_fats;		// 2 for TexFAT
/* 0x6F */	// ...
} PACKED;

struct exfat_dir_entry {
/* 0x00 */	uint8_t		entry_type;
		union {
			struct volume_label {
/* 0x01 */			uint8_t		char_count;		// Length of label
/* 0x02 */			uint16_t	vol_label[11];		// UTF16 string without null termination
/* 0x18 */			uint8_t		reserved[8];
/* 0x20 */		} PACKED label;
			struct volume_guid {
/* 0x01 */			uint8_t		sec_count;
/* 0x02 */			uint16_t	set_checksum;
/* 0x04 */			uint16_t	flags;
/* 0x06 */			uint8_t		vol_guid[16];
/* 0x16 */			uint8_t		reserved[10];
/* 0x20 */		} PACKED guid;
		} PACKED type;
} PACKED;

int FAST_FUNC volume_id_probe_exfat(struct volume_id *id /*,uint64_t off*/)
{
	struct exfat_super_block *sb;
	struct exfat_dir_entry *de;
	unsigned	sector_sz;
	unsigned	cluster_sz;
	uint64_t	root_dir_off;
	unsigned	count;
	unsigned	need_lbl_guid;

	// Primary super block
	dbg("exFAT: probing at offset 0x%x", EXFAT_SB_OFFSET);
	sb = volume_id_get_buffer(id, EXFAT_SB_OFFSET, sizeof(*sb));

	if (!sb)
		return -1;

	if (memcmp(sb->fs_name, "EXFAT   ", 8) != 0)
		return -1;

	sector_sz = 1 << sb->bytes_per_sector;
	cluster_sz = sector_sz << sb->sectors_per_cluster;
	// There are no clusters 0 and 1, so the first cluster is 2.
	root_dir_off = (uint64_t)EXFAT_SB_OFFSET +
		// Hmm... should we cast sector_sz/cluster_sz to uint64_t?
		(le32_to_cpu(sb->cluster_heap_offset)) * sector_sz +
		(le32_to_cpu(sb->root_dir) - 2) * cluster_sz;
	dbg("exFAT: sector size 0x%x bytes", sector_sz);
	dbg("exFAT: cluster size 0x%x bytes", cluster_sz);
	dbg("exFAT: root dir is at 0x%llx", (long long)root_dir_off);

	// Use DOS uuid as fallback, if no GUID set
	volume_id_set_uuid(id, sb->vol_serial_nr, UUID_DOS);

	// EXFAT_MAX_DIR_ENTRIES is used as a safety belt.
	// The Root Directory may hold an unlimited number of entries,
	// so we do not want to check all. Usually label and GUID
	// are in the beginning, but there are no guarantees.
	need_lbl_guid = (1 << 0) | (1 << 1);
	for (count = 0; count < EXFAT_MAX_DIR_ENTRIES; count++) {
		de = volume_id_get_buffer(id, root_dir_off + (count * EXFAT_DIR_ENTRY_SZ), EXFAT_DIR_ENTRY_SZ);
		if (de == NULL)
			break;
		if (de->entry_type == 0x00) {
			// End of Directory Marker
			dbg("exFAT: End of root directory reached after %u entries", count);
			break;
		}
		if (de->entry_type == 0x83) {
			// Volume Label Directory Entry
			volume_id_set_label_unicode16(id, (uint8_t *)de->type.label.vol_label,
						LE, 2 * de->type.label.char_count);
			need_lbl_guid &= ~(1 << 0);
		}
		if (de->entry_type == 0xA0) {
			// Volume GUID Directory Entry
			volume_id_set_uuid(id, de->type.guid.vol_guid, UUID_DCE);
			need_lbl_guid &= ~(1 << 1);
		}
		if (!need_lbl_guid)
			break;
	}

	IF_FEATURE_BLKID_TYPE(id->type = "exfat";)
	return 0;
}
