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
//config:config FEATURE_VOLUMEID_LINUXRAID
//config:	bool "linuxraid"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_LINUXRAID) += linux_raid.o

#include "volume_id_internal.h"

struct mdp_super_block {
	uint32_t	md_magic;
	uint32_t	major_version;
	uint32_t	minor_version;
	uint32_t	patch_version;
	uint32_t	gvalid_words;
	uint32_t	set_uuid0;
	uint32_t	ctime;
	uint32_t	level;
	uint32_t	size;
	uint32_t	nr_disks;
	uint32_t	raid_disks;
	uint32_t	md_minor;
	uint32_t	not_persistent;
	uint32_t	set_uuid1;
	uint32_t	set_uuid2;
	uint32_t	set_uuid3;
} PACKED;

#define MD_RESERVED_BYTES		0x10000
#define MD_MAGIC			0xa92b4efc

int FAST_FUNC volume_id_probe_linux_raid(struct volume_id *id /*,uint64_t off*/, uint64_t size)
{
	typedef uint32_t aliased_uint32_t FIX_ALIASING;
#define off ((uint64_t)0)
	uint64_t sboff;
	uint8_t uuid[16];
	struct mdp_super_block *mdp;

	dbg("probing at offset 0x%llx, size 0x%llx",
	    (unsigned long long) off, (unsigned long long) size);

	if (size < 0x10000)
		return -1;

	sboff = (size & ~(MD_RESERVED_BYTES - 1)) - MD_RESERVED_BYTES;
	mdp = volume_id_get_buffer(id, off + sboff, 0x800);
	if (mdp == NULL)
		return -1;

	if (mdp->md_magic != cpu_to_le32(MD_MAGIC))
		return -1;

	*(aliased_uint32_t*)uuid = mdp->set_uuid0;
	memcpy(&uuid[4], &mdp->set_uuid1, 12);
	volume_id_set_uuid(id, uuid, UUID_DCE);

//	snprintf(id->type_version, sizeof(id->type_version)-1, "%u.%u.%u",
//		le32_to_cpu(mdp->major_version),
//		le32_to_cpu(mdp->minor_version),
//		le32_to_cpu(mdp->patch_version));

	dbg("found raid signature");
//	volume_id_set_usage(id, VOLUME_ID_RAID);
	IF_FEATURE_BLKID_TYPE(id->type = "linux_raid_member";)

	return 0;
}
