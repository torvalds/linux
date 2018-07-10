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
//config:config FEATURE_VOLUMEID_ISO9660
//config:	bool "iso9660 filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_ISO9660) += iso9660.o

#include "volume_id_internal.h"

#define ISO_SUPERBLOCK_OFFSET		0x8000
#define ISO_SECTOR_SIZE			0x800
#define ISO_VD_OFFSET			(ISO_SUPERBLOCK_OFFSET + ISO_SECTOR_SIZE)
#define ISO_VD_PRIMARY			0x1
#define ISO_VD_SUPPLEMENTARY		0x2
#define ISO_VD_END			0xff
#define ISO_VD_MAX			16

struct iso_volume_descriptor {
	uint8_t		vd_type;
	uint8_t		vd_id[5];
	uint8_t		vd_version;
	uint8_t		flags;
	uint8_t		system_id[32];
	uint8_t		volume_id[32];
	uint8_t		unused[8];
	uint8_t		space_size[8];
	uint8_t		escape_sequences[8];
} PACKED;

struct high_sierra_volume_descriptor {
	uint8_t		foo[8];
	uint8_t		type;
	uint8_t		id[4];
	uint8_t		version;
} PACKED;

int FAST_FUNC volume_id_probe_iso9660(struct volume_id *id /*,uint64_t off*/)
{
#define off ((uint64_t)0)
	uint8_t *buf;
	struct iso_volume_descriptor *is;
	struct high_sierra_volume_descriptor *hs;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	buf = volume_id_get_buffer(id, off + ISO_SUPERBLOCK_OFFSET, 0x200);
	if (buf == NULL)
		return -1;

	is = (struct iso_volume_descriptor *) buf;

	if (memcmp(is->vd_id, "CD001", 5) == 0) {
		int vd_offset;
		int i;

		dbg("read label from PVD");
//		volume_id_set_label_raw(id, is->volume_id, 32);
		volume_id_set_label_string(id, is->volume_id, 32);

		dbg("looking for SVDs");
		vd_offset = ISO_VD_OFFSET;
		for (i = 0; i < ISO_VD_MAX; i++) {
			uint8_t svd_label[64];

			is = volume_id_get_buffer(id, off + vd_offset, 0x200);
			if (is == NULL || is->vd_type == ISO_VD_END)
				break;
			if (is->vd_type != ISO_VD_SUPPLEMENTARY)
				continue;

			dbg("found SVD at offset 0x%llx", (unsigned long long) (off + vd_offset));
			if (memcmp(is->escape_sequences, "%/@", 3) == 0
			 || memcmp(is->escape_sequences, "%/C", 3) == 0
			 || memcmp(is->escape_sequences, "%/E", 3) == 0
			) {
				dbg("Joliet extension found");
				volume_id_set_unicode16((char *)svd_label, sizeof(svd_label), is->volume_id, BE, 32);
				if (memcmp(id->label, svd_label, 16) == 0) {
					dbg("SVD label is identical, use the possibly longer PVD one");
					break;
				}

//				volume_id_set_label_raw(id, is->volume_id, 32);
				volume_id_set_label_string(id, svd_label, 32);
//				strcpy(id->type_version, "Joliet Extension");
				goto found;
			}
			vd_offset += ISO_SECTOR_SIZE;
		}
		goto found;
	}

	hs = (struct high_sierra_volume_descriptor *) buf;

	if (memcmp(hs->id, "CDROM", 5) == 0) {
//		strcpy(id->type_version, "High Sierra");
		goto found;
	}

	return -1;

 found:
//	volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
	IF_FEATURE_BLKID_TYPE(id->type = "iso9660";)

	return 0;
}
