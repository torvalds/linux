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
//config:### config FEATURE_VOLUMEID_NVIDIARAID
//config:###	bool "nvidia raid"
//config:###	default y
//config:###	depends on VOLUMEID

//kbuild:### lib-$(CONFIG_FEATURE_VOLUMEID_NVIDIARAID) += nvidia_raid.o

#include "volume_id_internal.h"

struct nvidia_meta {
	uint8_t		vendor[8];
	uint32_t	size;
	uint32_t	chksum;
	uint16_t	version;
} PACKED;

#define NVIDIA_SIGNATURE		"NVIDIA"

int FAST_FUNC volume_id_probe_nvidia_raid(struct volume_id *id, uint64_t off, uint64_t size)
{
	uint64_t meta_off;
	struct nvidia_meta *nv;

	dbg("probing at offset 0x%llx, size 0x%llx",
	    (unsigned long long) off, (unsigned long long) size);

	if (size < 0x10000)
		return -1;

	meta_off = ((size / 0x200)-2) * 0x200;
	nv = volume_id_get_buffer(id, off + meta_off, 0x200);
	if (nv == NULL)
		return -1;

	if (memcmp(nv->vendor, NVIDIA_SIGNATURE, sizeof(NVIDIA_SIGNATURE)-1) != 0)
		return -1;

//	volume_id_set_usage(id, VOLUME_ID_RAID);
//	snprintf(id->type_version, sizeof(id->type_version)-1, "%u", le16_to_cpu(nv->version));
//	id->type = "nvidia_raid_member";

	return 0;
}
