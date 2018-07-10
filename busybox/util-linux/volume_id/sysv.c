/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2005 Kay Sievers <kay.sievers@vrfy.org>
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
//config:config FEATURE_VOLUMEID_SYSV
//config:	bool "sysv filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_SYSV) += sysv.o

#include "volume_id_internal.h"

#define SYSV_NICINOD			100
#define SYSV_NICFREE			50

struct sysv_super {
	uint16_t	s_isize;
	uint16_t	s_pad0;
	uint32_t	s_fsize;
	uint16_t	s_nfree;
	uint16_t	s_pad1;
	uint32_t	s_free[SYSV_NICFREE];
	uint16_t	s_ninode;
	uint16_t	s_pad2;
	uint16_t	s_inode[SYSV_NICINOD];
	uint8_t		s_flock;
	uint8_t		s_ilock;
	uint8_t		s_fmod;
	uint8_t		s_ronly;
	uint32_t	s_time;
	uint16_t	s_dinfo[4];
	uint32_t	s_tfree;
	uint16_t	s_tinode;
	uint16_t	s_pad3;
	uint8_t		s_fname[6];
	uint8_t		s_fpack[6];
	uint32_t	s_fill[12];
	uint32_t	s_state;
	uint32_t	s_magic;
	uint32_t	s_type;
} PACKED;

#define XENIX_NICINOD				100
#define XENIX_NICFREE				100

struct xenix_super {
	uint16_t	s_isize;
	uint32_t	s_fsize;
	uint16_t	s_nfree;
	uint32_t	s_free[XENIX_NICFREE];
	uint16_t	s_ninode;
	uint16_t	s_inode[XENIX_NICINOD];
	uint8_t		s_flock;
	uint8_t		s_ilock;
	uint8_t		s_fmod;
	uint8_t		s_ronly;
	uint32_t	s_time;
	uint32_t	s_tfree;
	uint16_t	s_tinode;
	uint16_t	s_dinfo[4];
	uint8_t		s_fname[6];
	uint8_t		s_fpack[6];
	uint8_t		s_clean;
	uint8_t		s_fill[371];
	uint32_t	s_magic;
	uint32_t	s_type;
} PACKED;

#define SYSV_SUPERBLOCK_BLOCK			0x01
#define SYSV_MAGIC				0xfd187e20
#define XENIX_SUPERBLOCK_BLOCK			0x18
#define XENIX_MAGIC				0x2b5544
#define SYSV_MAX_BLOCKSIZE			0x800

int FAST_FUNC volume_id_probe_sysv(struct volume_id *id /*,uint64_t off*/)
{
#define off ((uint64_t)0)
	struct sysv_super *vs;
	struct xenix_super *xs;
	unsigned boff;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	for (boff = 0x200; boff <= SYSV_MAX_BLOCKSIZE; boff <<= 1) {
		vs = volume_id_get_buffer(id, off + (boff * SYSV_SUPERBLOCK_BLOCK), 0x200);
		if (vs == NULL)
			return -1;

		if (vs->s_magic == cpu_to_le32(SYSV_MAGIC) || vs->s_magic == cpu_to_be32(SYSV_MAGIC)) {
//			volume_id_set_label_raw(id, vs->s_fname, 6);
			volume_id_set_label_string(id, vs->s_fname, 6);
			IF_FEATURE_BLKID_TYPE(id->type = "sysv");
			goto found;
		}
	}

	for (boff = 0x200; boff <= SYSV_MAX_BLOCKSIZE; boff <<= 1) {
		xs = volume_id_get_buffer(id, off + (boff + XENIX_SUPERBLOCK_BLOCK), 0x200);
		if (xs == NULL)
			return -1;

		if (xs->s_magic == cpu_to_le32(XENIX_MAGIC) || xs->s_magic == cpu_to_be32(XENIX_MAGIC)) {
//			volume_id_set_label_raw(id, xs->s_fname, 6);
			volume_id_set_label_string(id, xs->s_fname, 6);
			IF_FEATURE_BLKID_TYPE(id->type = "xenix";)
			goto found;
		}
	}

	return -1;

 found:
//	volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
	return 0;
}
