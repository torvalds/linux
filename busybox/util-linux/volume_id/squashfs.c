/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2012 S-G Bergh <sgb@systemasis.org>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FEATURE_VOLUMEID_SQUASHFS
//config:	bool "SquashFS filesystem"
//config:	default y
//config:	depends on VOLUMEID && FEATURE_BLKID_TYPE
//config:	help
//config:	Squashfs is a compressed read-only filesystem for Linux. Squashfs is
//config:	intended for general read-only filesystem use and in constrained block
//config:	device/memory systems (e.g. embedded systems) where low overhead is
//config:	needed.

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_SQUASHFS) += squashfs.o

#include "volume_id_internal.h"

struct squashfs_superblock {
	uint32_t	magic;
/*
	uint32_t	dummy[6];
	uint16_t        major;
	uint16_t        minor;
*/
} PACKED;

int FAST_FUNC volume_id_probe_squashfs(struct volume_id *id /*,uint64_t off*/)
{
#define off ((uint64_t)0)
	struct squashfs_superblock *sb;

	dbg("SquashFS: probing at offset 0x%llx", (unsigned long long) off);
	sb = volume_id_get_buffer(id, off, 0x200);
	if (!sb)
		return -1;

	// Old SquashFS (pre 4.0) can be both big and little endian, so test for both.
	// Likewise, it is commonly used in firwmare with some non-standard signatures.
#define pack(a,b,c,d) ( (uint32_t)((a * 256 + b) * 256 + c) * 256 + d )
#define SIG1 pack('s','q','s','h')
#define SIG2 pack('h','s','q','s')
#define SIG3 pack('s','h','s','q')
#define SIG4 pack('q','s','h','s')
	if (sb->magic == SIG1
	 || sb->magic == SIG2
	 || sb->magic == SIG3
	 || sb->magic == SIG4
	) {
		IF_FEATURE_BLKID_TYPE(id->type = "squashfs";)
		return 0;
	}

	return -1;
}
