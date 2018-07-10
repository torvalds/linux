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
//config:config FEATURE_VOLUMEID_FAT
//config:	bool "fat filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_FAT) += fat.o

#include "volume_id_internal.h"

/* linux/msdos_fs.h says: */
#define FAT12_MAX			0xff4
#define FAT16_MAX			0xfff4
#define FAT32_MAX			0x0ffffff6

#define FAT_ATTR_VOLUME_ID		0x08
#define FAT_ATTR_DIR			0x10
#define FAT_ATTR_LONG_NAME		0x0f
#define FAT_ATTR_MASK			0x3f
#define FAT_ENTRY_FREE			0xe5

struct vfat_super_block {
	uint8_t		boot_jump[3];
	uint8_t		sysid[8];
	uint16_t	sector_size_bytes;
	uint8_t		sectors_per_cluster;
	uint16_t	reserved_sct;
	uint8_t		fats;
	uint16_t	dir_entries;
	uint16_t	sectors;
	uint8_t		media;
	uint16_t	fat_length;
	uint16_t	secs_track;
	uint16_t	heads;
	uint32_t	hidden;
	uint32_t	total_sect;
	union {
		struct fat_super_block {
			uint8_t		unknown[3];
			uint8_t		serno[4];
			uint8_t		label[11];
			uint8_t		magic[8];
			uint8_t		dummy2[192];
			uint8_t		pmagic[2];
		} PACKED fat;
		struct fat32_super_block {
			uint32_t	fat32_length;
			uint16_t	flags;
			uint8_t		version[2];
			uint32_t	root_cluster;
			uint16_t	insfo_sector;
			uint16_t	backup_boot;
			uint16_t	reserved2[6];
			uint8_t		unknown[3];
			uint8_t		serno[4];
			uint8_t		label[11];
			uint8_t		magic[8];
			uint8_t		dummy2[164];
			uint8_t		pmagic[2];
		} PACKED fat32;
	} PACKED type;
} PACKED;

struct vfat_dir_entry {
	uint8_t		name[11];
	uint8_t		attr;
	uint16_t	time_creat;
	uint16_t	date_creat;
	uint16_t	time_acc;
	uint16_t	date_acc;
	uint16_t	cluster_high;
	uint16_t	time_write;
	uint16_t	date_write;
	uint16_t	cluster_low;
	uint32_t	size;
} PACKED;

static uint8_t *get_attr_volume_id(struct vfat_dir_entry *dir, int count)
{
	for (;--count >= 0; dir++) {
		/* end marker */
		if (dir->name[0] == 0x00) {
			dbg("end of dir");
			break;
		}

		/* empty entry */
		if (dir->name[0] == FAT_ENTRY_FREE)
			continue;

		/* long name */
		if ((dir->attr & FAT_ATTR_MASK) == FAT_ATTR_LONG_NAME)
			continue;

		if ((dir->attr & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIR)) == FAT_ATTR_VOLUME_ID) {
			/* labels do not have file data */
			if (dir->cluster_high != 0 || dir->cluster_low != 0)
				continue;

			dbg("found ATTR_VOLUME_ID id in root dir");
			return dir->name;
		}

		dbg("skip dir entry");
	}

	return NULL;
}

int FAST_FUNC volume_id_probe_vfat(struct volume_id *id /*,uint64_t fat_partition_off*/)
{
#define fat_partition_off ((uint64_t)0)
	struct vfat_super_block *vs;
	struct vfat_dir_entry *dir;
	uint16_t sector_size_bytes;
	uint16_t dir_entries;
	uint32_t sect_count;
	uint16_t reserved_sct;
	uint32_t fat_size_sct;
	uint32_t root_cluster;
	uint32_t dir_size_sct;
	uint32_t cluster_count;
	uint64_t root_start_off;
	uint32_t start_data_sct;
	uint8_t *buf;
	uint32_t buf_size;
	uint8_t *label = NULL;
	uint32_t next_cluster;
	int maxloop;

	dbg("probing at offset 0x%llx", (unsigned long long) fat_partition_off);

	vs = volume_id_get_buffer(id, fat_partition_off, 0x200);
	if (vs == NULL)
		return -1;

	/* believe only that's fat, don't trust the version
	 * the cluster_count will tell us
	 */
	if (memcmp(vs->sysid, "NTFS", 4) == 0)
		return -1;

	if (memcmp(vs->type.fat32.magic, "MSWIN", 5) == 0)
		goto valid;

	if (memcmp(vs->type.fat32.magic, "FAT32   ", 8) == 0)
		goto valid;

	if (memcmp(vs->type.fat.magic, "FAT16   ", 8) == 0)
		goto valid;

	if (memcmp(vs->type.fat.magic, "MSDOS", 5) == 0)
		goto valid;

	if (memcmp(vs->type.fat.magic, "FAT12   ", 8) == 0)
		goto valid;

	/*
	 * There are old floppies out there without a magic, so we check
	 * for well known values and guess if it's a fat volume
	 */

	/* boot jump address check */
	if ((vs->boot_jump[0] != 0xeb || vs->boot_jump[2] != 0x90)
	 && vs->boot_jump[0] != 0xe9
	) {
		return -1;
	}

	/* heads check */
	if (vs->heads == 0)
		return -1;

	/* cluster size check */
	if (vs->sectors_per_cluster == 0
	 || (vs->sectors_per_cluster & (vs->sectors_per_cluster-1))
	) {
		return -1;
	}

	/* media check */
	if (vs->media < 0xf8 && vs->media != 0xf0)
		return -1;

	/* fat count*/
	if (vs->fats != 2)
		return -1;

 valid:
	/* sector size check */
	sector_size_bytes = le16_to_cpu(vs->sector_size_bytes);
	if (sector_size_bytes != 0x200 && sector_size_bytes != 0x400
	 && sector_size_bytes != 0x800 && sector_size_bytes != 0x1000
	) {
		return -1;
	}

	dbg("sector_size_bytes 0x%x", sector_size_bytes);
	dbg("sectors_per_cluster 0x%x", vs->sectors_per_cluster);

	reserved_sct = le16_to_cpu(vs->reserved_sct);
	dbg("reserved_sct 0x%x", reserved_sct);

	sect_count = le16_to_cpu(vs->sectors);
	if (sect_count == 0)
		sect_count = le32_to_cpu(vs->total_sect);
	dbg("sect_count 0x%x", sect_count);

	fat_size_sct = le16_to_cpu(vs->fat_length);
	if (fat_size_sct == 0)
		fat_size_sct = le32_to_cpu(vs->type.fat32.fat32_length);
	fat_size_sct *= vs->fats;
	dbg("fat_size_sct 0x%x", fat_size_sct);

	dir_entries = le16_to_cpu(vs->dir_entries);
	dir_size_sct = ((dir_entries * sizeof(struct vfat_dir_entry)) +
			(sector_size_bytes-1)) / sector_size_bytes;
	dbg("dir_size_sct 0x%x", dir_size_sct);

	cluster_count = sect_count - (reserved_sct + fat_size_sct + dir_size_sct);
	cluster_count /= vs->sectors_per_cluster;
	dbg("cluster_count 0x%x", cluster_count);

//	if (cluster_count < FAT12_MAX) {
//		strcpy(id->type_version, "FAT12");
//	} else if (cluster_count < FAT16_MAX) {
//		strcpy(id->type_version, "FAT16");
//	} else {
//		strcpy(id->type_version, "FAT32");
//		goto fat32;
//	}
	if (cluster_count >= FAT16_MAX)
		goto fat32;

	/* the label may be an attribute in the root directory */
	root_start_off = (reserved_sct + fat_size_sct) * sector_size_bytes;
	dbg("root dir start 0x%llx", (unsigned long long) root_start_off);
	dbg("expected entries 0x%x", dir_entries);

	buf_size = dir_entries * sizeof(struct vfat_dir_entry);
	buf = volume_id_get_buffer(id, fat_partition_off + root_start_off, buf_size);
	if (buf == NULL)
		goto ret;

	label = get_attr_volume_id((struct vfat_dir_entry*) buf, dir_entries);

	vs = volume_id_get_buffer(id, fat_partition_off, 0x200);
	if (vs == NULL)
		return -1;

	if (label != NULL && memcmp(label, "NO NAME    ", 11) != 0) {
//		volume_id_set_label_raw(id, label, 11);
		volume_id_set_label_string(id, label, 11);
	} else if (memcmp(vs->type.fat.label, "NO NAME    ", 11) != 0) {
//		volume_id_set_label_raw(id, vs->type.fat.label, 11);
		volume_id_set_label_string(id, vs->type.fat.label, 11);
	}
	volume_id_set_uuid(id, vs->type.fat.serno, UUID_DOS);
	goto ret;

 fat32:
	/* FAT32 root dir is a cluster chain like any other directory */
	buf_size = vs->sectors_per_cluster * sector_size_bytes;
	root_cluster = le32_to_cpu(vs->type.fat32.root_cluster);
	start_data_sct = reserved_sct + fat_size_sct;

	next_cluster = root_cluster;
	maxloop = 100;
	while (--maxloop) {
		uint64_t next_off_sct;
		uint64_t next_off;
		uint64_t fat_entry_off;
		int count;

		dbg("next_cluster 0x%x", (unsigned)next_cluster);
		next_off_sct = (uint64_t)(next_cluster - 2) * vs->sectors_per_cluster;
		next_off = (start_data_sct + next_off_sct) * sector_size_bytes;
		dbg("cluster offset 0x%llx", (unsigned long long) next_off);

		/* get cluster */
		buf = volume_id_get_buffer(id, fat_partition_off + next_off, buf_size);
		if (buf == NULL)
			goto ret;

		dir = (struct vfat_dir_entry*) buf;
		count = buf_size / sizeof(struct vfat_dir_entry);
		dbg("expected entries 0x%x", count);

		label = get_attr_volume_id(dir, count);
		if (label)
			break;

		/* get FAT entry */
		fat_entry_off = (reserved_sct * sector_size_bytes) + (next_cluster * sizeof(uint32_t));
		dbg("fat_entry_off 0x%llx", (unsigned long long)fat_entry_off);
		buf = volume_id_get_buffer(id, fat_partition_off + fat_entry_off, buf_size);
		if (buf == NULL)
			goto ret;

		/* set next cluster */
		next_cluster = le32_to_cpu(*(uint32_t*)buf) & 0x0fffffff;
		if (next_cluster < 2 || next_cluster > FAT32_MAX)
			break;
	}
	if (maxloop == 0)
		dbg("reached maximum follow count of root cluster chain, give up");

	vs = volume_id_get_buffer(id, fat_partition_off, 0x200);
	if (vs == NULL)
		return -1;

	if (label != NULL && memcmp(label, "NO NAME    ", 11) != 0) {
//		volume_id_set_label_raw(id, label, 11);
		volume_id_set_label_string(id, label, 11);
	} else if (memcmp(vs->type.fat32.label, "NO NAME    ", 11) != 0) {
//		volume_id_set_label_raw(id, vs->type.fat32.label, 11);
		volume_id_set_label_string(id, vs->type.fat32.label, 11);
	}
	volume_id_set_uuid(id, vs->type.fat32.serno, UUID_DOS);

 ret:
//	volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
	IF_FEATURE_BLKID_TYPE(id->type = "vfat";)

	return 0;
}
