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
//config:### config FEATURE_VOLUMEID_LSIRAID
//config:###	bool "lsi raid"
//config:###	default y
//config:###	depends on VOLUMEID

//kbuild:### lib-$(CONFIG_FEATURE_VOLUMEID_LSIRAID) += lsi_raid.o

#include "volume_id_internal.h"

struct lsi_meta {
	uint8_t		sig[6];
} PACKED;

#define LSI_SIGNATURE		"$XIDE$"

int FAST_FUNC volume_id_probe_lsi_mega_raid(struct volume_id *id, uint64_t off, uint64_t size)
{
	uint64_t meta_off;
	struct lsi_meta *lsi;

	dbg("probing at offset 0x%llx, size 0x%llx",
	    (unsigned long long) off, (unsigned long long) size);

	if (size < 0x10000)
		return -1;

	meta_off = ((size / 0x200)-1) * 0x200;
	lsi = volume_id_get_buffer(id, off + meta_off, 0x200);
	if (lsi == NULL)
		return -1;

	if (memcmp(lsi->sig, LSI_SIGNATURE, sizeof(LSI_SIGNATURE)-1) != 0)
		return -1;

//	volume_id_set_usage(id, VOLUME_ID_RAID);
//	id->type = "lsi_mega_raid_member";

	return 0;
}
