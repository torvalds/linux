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
//config:config FEATURE_VOLUMEID_EXT
//config:	bool "Ext filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_EXT) += ext.o

#include "volume_id_internal.h"
#include "bb_e2fs_defs.h"

#define EXT_SUPERBLOCK_OFFSET			0x400

int FAST_FUNC volume_id_probe_ext(struct volume_id *id /*,uint64_t off*/)
{
#define off ((uint64_t)0)
	struct ext2_super_block *es;

	dbg("ext: probing at offset 0x%llx", (unsigned long long) off);

	es = volume_id_get_buffer(id, off + EXT_SUPERBLOCK_OFFSET, 0x200);
	if (es == NULL)
		return -1;

	if (es->s_magic != cpu_to_le16(EXT2_SUPER_MAGIC)) {
		dbg("ext: no magic found");
		return -1;
	}

//	volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
//	volume_id_set_label_raw(id, es->volume_name, 16);
	volume_id_set_label_string(id, (void*)es->s_volume_name, 16);
	volume_id_set_uuid(id, es->s_uuid, UUID_DCE);
	dbg("ext: label '%s' uuid '%s'", id->label, id->uuid);

#if ENABLE_FEATURE_BLKID_TYPE
	if ((es->s_feature_ro_compat & cpu_to_le32(EXT4_FEATURE_RO_COMPAT_HUGE_FILE | EXT4_FEATURE_RO_COMPAT_DIR_NLINK))
	 || (es->s_feature_incompat & cpu_to_le32(EXT4_FEATURE_INCOMPAT_EXTENTS | EXT4_FEATURE_INCOMPAT_64BIT))
	) {
		id->type = "ext4";
	}
	else if (es->s_feature_compat & cpu_to_le32(EXT3_FEATURE_COMPAT_HAS_JOURNAL))
		id->type = "ext3";
	else
		id->type = "ext2";
#endif
	return 0;
}
