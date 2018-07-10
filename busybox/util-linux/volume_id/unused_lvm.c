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
//config:### config FEATURE_VOLUMEID_LVM
//config:###	bool "lvm"
//config:###	default y
//config:###	depends on VOLUMEID

//kbuild:### lib-$(CONFIG_FEATURE_VOLUMEID_LVM) += lvm.o

#include "volume_id_internal.h"

struct lvm1_super_block {
	uint8_t	id[2];
} PACKED;

struct lvm2_super_block {
	uint8_t		id[8];
	uint64_t	sector_xl;
	uint32_t	crc_xl;
	uint32_t	offset_xl;
	uint8_t		type[8];
} PACKED;

#define LVM1_SB_OFF			0x400

int FAST_FUNC volume_id_probe_lvm1(struct volume_id *id, uint64_t off)
{
	struct lvm1_super_block *lvm;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	lvm = volume_id_get_buffer(id, off + LVM1_SB_OFF, 0x800);
	if (lvm == NULL)
		return -1;

	if (lvm->id[0] != 'H' || lvm->id[1] != 'M')
		return -1;

//	volume_id_set_usage(id, VOLUME_ID_RAID);
//	id->type = "LVM1_member";

	return 0;
}

#define LVM2_LABEL_ID			"LABELONE"
#define LVM2LABEL_SCAN_SECTORS		4

int FAST_FUNC volume_id_probe_lvm2(struct volume_id *id, uint64_t off)
{
	const uint8_t *buf;
	unsigned soff;
	struct lvm2_super_block *lvm;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	buf = volume_id_get_buffer(id, off, LVM2LABEL_SCAN_SECTORS * 0x200);
	if (buf == NULL)
		return -1;


	for (soff = 0; soff < LVM2LABEL_SCAN_SECTORS * 0x200; soff += 0x200) {
		lvm = (struct lvm2_super_block *) &buf[soff];

		if (memcmp(lvm->id, LVM2_LABEL_ID, 8) == 0)
			goto found;
	}

	return -1;

 found:
//	memcpy(id->type_version, lvm->type, 8);
//	volume_id_set_usage(id, VOLUME_ID_RAID);
//	id->type = "LVM2_member";

	return 0;
}
