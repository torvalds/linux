/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) Andre Masella <andre@masella.no-ip.org>
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
//config:config FEATURE_VOLUMEID_OCFS2
//config:	bool "ocfs2 filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_OCFS2) += ocfs2.o

#include "volume_id_internal.h"

/* All these values are taken from ocfs2-tools's ocfs2_fs.h */
#define OCFS2_VOL_UUID_LEN			16
#define OCFS2_MAX_VOL_LABEL_LEN			64
#define OCFS2_SUPERBLOCK_OFFSET			0x2000


/* This is the superblock. The OCFS2 header files have structs in structs.
This is one has been simplified since we only care about the superblock.
*/

struct ocfs2_super_block {
	uint8_t		i_signature[8];			/* Signature for validation */
	uint32_t	i_generation;			/* Generation number */
	int16_t		i_suballoc_slot;			/* Slot suballocator this inode belongs to */
	uint16_t	i_suballoc_bit;			/* Bit offset in suballocator block group */
	uint32_t	i_reserved0;
	uint32_t	i_clusters;			/* Cluster count */
	uint32_t	i_uid;				/* Owner UID */
	uint32_t	i_gid;				/* Owning GID */
	uint64_t	i_size;				/* Size in bytes */
	uint16_t	i_mode;				/* File mode */
	uint16_t	i_links_count;			/* Links count */
	uint32_t	i_flags;				/* File flags */
	uint64_t	i_atime;				/* Access time */
	uint64_t	i_ctime;				/* Creation time */
	uint64_t	i_mtime;				/* Modification time */
	uint64_t	i_dtime;				/* Deletion time */
	uint64_t	i_blkno;				/* Offset on disk, in blocks */
	uint64_t	i_last_eb_blk;			/* Pointer to last extent block */
	uint32_t	i_fs_generation;			/* Generation per fs-instance */
	uint32_t	i_atime_nsec;
	uint32_t	i_ctime_nsec;
	uint32_t	i_mtime_nsec;
	uint64_t	i_reserved1[9];
	uint64_t	i_pad1;				/* Generic way to refer to this 64bit union */
	/* Normally there is a union of the different block types, but we only care about the superblock. */
	uint16_t	s_major_rev_level;
	uint16_t	s_minor_rev_level;
	uint16_t	s_mnt_count;
	int16_t		s_max_mnt_count;
	uint16_t	s_state;				/* File system state */
	uint16_t	s_errors;				/* Behaviour when detecting errors */
	uint32_t	s_checkinterval;			/* Max time between checks */
	uint64_t	s_lastcheck;			/* Time of last check */
	uint32_t	s_creator_os;			/* OS */
	uint32_t	s_feature_compat;			/* Compatible feature set */
	uint32_t	s_feature_incompat;		/* Incompatible feature set */
	uint32_t	s_feature_ro_compat;		/* Readonly-compatible feature set */
	uint64_t	s_root_blkno;			/* Offset, in blocks, of root directory dinode */
	uint64_t	s_system_dir_blkno;		/* Offset, in blocks, of system directory dinode */
	uint32_t	s_blocksize_bits;			/* Blocksize for this fs */
	uint32_t	s_clustersize_bits;		/* Clustersize for this fs */
	uint16_t	s_max_slots;			/* Max number of simultaneous mounts before tunefs required */
	uint16_t	s_reserved1;
	uint32_t	s_reserved2;
	uint64_t	s_first_cluster_group;		/* Block offset of 1st cluster group header */
	uint8_t		s_label[OCFS2_MAX_VOL_LABEL_LEN];	/* Label for mounting, etc. */
	uint8_t		s_uuid[OCFS2_VOL_UUID_LEN];	/* 128-bit uuid */
} PACKED;

int FAST_FUNC volume_id_probe_ocfs2(struct volume_id *id /*,uint64_t off*/)
{
#define off ((uint64_t)0)
	struct ocfs2_super_block *os;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	os = volume_id_get_buffer(id, off + OCFS2_SUPERBLOCK_OFFSET, 0x200);
	if (os == NULL)
		return -1;

	if (memcmp(os->i_signature, "OCFSV2", 6) != 0) {
		return -1;
	}

//	volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
//	volume_id_set_label_raw(id, os->s_label, OCFS2_MAX_VOL_LABEL_LEN < VOLUME_ID_LABEL_SIZE ?
//					OCFS2_MAX_VOL_LABEL_LEN : VOLUME_ID_LABEL_SIZE);
	volume_id_set_label_string(id, os->s_label, OCFS2_MAX_VOL_LABEL_LEN < VOLUME_ID_LABEL_SIZE ?
					OCFS2_MAX_VOL_LABEL_LEN : VOLUME_ID_LABEL_SIZE);
	volume_id_set_uuid(id, os->s_uuid, UUID_DCE);
	IF_FEATURE_BLKID_TYPE(id->type = "ocfs2";)
	return 0;
}
