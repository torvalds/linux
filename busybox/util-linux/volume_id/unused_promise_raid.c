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
//config:### config FEATURE_VOLUMEID_PROMISERAID
//config:###	bool "promise raid"
//config:###	default y
//config:###	depends on VOLUMEID

//kbuild:### lib-$(CONFIG_FEATURE_VOLUMEID_PROMISERAID) += promise_raid.o

#include "volume_id_internal.h"

struct promise_meta {
	uint8_t	sig[24];
} PACKED;

#define PDC_CONFIG_OFF		0x1200
#define PDC_SIGNATURE		"Promise Technology, Inc."

int FAST_FUNC volume_id_probe_promise_fasttrack_raid(struct volume_id *id, uint64_t off, uint64_t size)
{
	static const unsigned short sectors[] = {
		63, 255, 256, 16, 399
	};

	struct promise_meta *pdc;
	unsigned i;

	dbg("probing at offset 0x%llx, size 0x%llx",
	    (unsigned long long) off, (unsigned long long) size);

	if (size < 0x40000)
		return -1;

	for (i = 0; i < ARRAY_SIZE(sectors); i++) {
		uint64_t meta_off;

		meta_off = ((size / 0x200) - sectors[i]) * 0x200;
		pdc = volume_id_get_buffer(id, off + meta_off, 0x200);
		if (pdc == NULL)
			return -1;

		if (memcmp(pdc->sig, PDC_SIGNATURE, sizeof(PDC_SIGNATURE)-1) == 0)
			goto found;
	}
	return -1;

 found:
//	volume_id_set_usage(id, VOLUME_ID_RAID);
//	id->type = "promise_fasttrack_raid_member";

	return 0;
}
