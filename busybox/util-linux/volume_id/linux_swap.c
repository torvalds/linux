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
//config:config FEATURE_VOLUMEID_LINUXSWAP
//config:	bool "linux swap filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_LINUXSWAP) += linux_swap.o

#include "volume_id_internal.h"

struct swap_header_v1_2 {
	uint8_t		bootbits[1024];
	uint32_t	version;
	uint32_t	last_page;
	uint32_t	nr_badpages;
	uint8_t		uuid[16];
	uint8_t		volume_name[16];
} PACKED;

#define LARGEST_PAGESIZE			0x4000

int FAST_FUNC volume_id_probe_linux_swap(struct volume_id *id /*,uint64_t off*/)
{
#define off ((uint64_t)0)
	struct swap_header_v1_2 *sw;
	const uint8_t *buf;
	unsigned page;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	/* the swap signature is at the end of the PAGE_SIZE */
	for (page = 0x1000; page <= LARGEST_PAGESIZE; page <<= 1) {
			buf = volume_id_get_buffer(id, off + page-10, 10);
			if (buf == NULL)
				return -1;

			if (memcmp(buf, "SWAP-SPACE", 10) == 0) {
//				id->type_version[0] = '1';
//				id->type_version[1] = '\0';
				goto found;
			}

			if (memcmp(buf, "SWAPSPACE2", 10) == 0
			 || memcmp(buf, "S1SUSPEND", 9) == 0
			 || memcmp(buf, "S2SUSPEND", 9) == 0
			 || memcmp(buf, "ULSUSPEND", 9) == 0
			) {
				sw = volume_id_get_buffer(id, off, sizeof(struct swap_header_v1_2));
				if (sw == NULL)
					return -1;
//				id->type_version[0] = '2';
//				id->type_version[1] = '\0';
//				volume_id_set_label_raw(id, sw->volume_name, 16);
				volume_id_set_label_string(id, sw->volume_name, 16);
				volume_id_set_uuid(id, sw->uuid, UUID_DCE);
				goto found;
			}
	}
	return -1;

found:
//	volume_id_set_usage(id, VOLUME_ID_OTHER);
	IF_FEATURE_BLKID_TYPE(id->type = "swap";)

	return 0;
}
