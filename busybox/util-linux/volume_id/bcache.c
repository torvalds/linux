/*
 * Copyright (C) 2013 Rolf Fokkens <rolf@fokkens.nl>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 * Based on code fragments from bcache-tools by Kent Overstreet:
 * http://evilpiepirate.org/git/bcache-tools.git
 */
//config:config FEATURE_VOLUMEID_BCACHE
//config:	bool "bcache filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_BCACHE) += bcache.o

#include "volume_id_internal.h"

#define SB_LABEL_SIZE      32
#define SB_JOURNAL_BUCKETS 256U

static const char bcache_magic[] ALIGN1 = {
	0xc6, 0x85, 0x73, 0xf6, 0x4e, 0x1a, 0x45, 0xca,
	0x82, 0x65, 0xf5, 0x7f, 0x48, 0xba, 0x6d, 0x81
};

struct bcache_super_block {
	uint64_t		csum;
	uint64_t		offset;	/* sector where this sb was written */
	uint64_t		version;

	uint8_t			magic[16];

	uint8_t			uuid[16];
	union {
		uint8_t		set_uuid[16];
		uint64_t	set_magic;
	};
	uint8_t			label[SB_LABEL_SIZE];

	uint64_t		flags;
	uint64_t		seq;
	uint64_t		pad[8];

	union {
	struct {
		/* Cache devices */
		uint64_t	nbuckets;	/* device size */

		uint16_t	block_size;	/* sectors */
		uint16_t	bucket_size;	/* sectors */

		uint16_t	nr_in_set;
		uint16_t	nr_this_dev;
	};
	struct {
		/* Backing devices */
		uint64_t	data_offset;

		/*
		 * block_size from the cache device section is still used by
		 * backing devices, so don't add anything here until we fix
		 * things to not need it for backing devices anymore
		 */
	};
	};

	uint32_t		last_mount;	/* time_t */

	uint16_t		first_bucket;
	union {
		uint16_t	njournal_buckets;
		uint16_t	keys;
	};
	uint64_t		d[SB_JOURNAL_BUCKETS];	/* journal buckets */
};

/* magic string */
#define BCACHE_SB_MAGIC     bcache_magic
/* magic string len */
#define BCACHE_SB_MAGIC_LEN sizeof (bcache_magic)
/* super block offset */
#define BCACHE_SB_OFF       0x1000
/* supper block offset in kB */
#define BCACHE_SB_KBOFF     (BCACHE_SB_OFF >> 10)
/* magic string offset within super block */
#define BCACHE_SB_MAGIC_OFF offsetof (struct bcache_super_block, magic)

int FAST_FUNC volume_id_probe_bcache(struct volume_id *id /*,uint64_t off*/)
{
	struct bcache_super_block *sb;

	sb = volume_id_get_buffer(id, BCACHE_SB_OFF, sizeof(*sb));
	if (sb == NULL)
		return -1;

	if (memcmp(sb->magic, BCACHE_SB_MAGIC, BCACHE_SB_MAGIC_LEN) != 0)
		return -1;

	volume_id_set_label_string(id, sb->label, SB_LABEL_SIZE);
	volume_id_set_uuid(id, sb->uuid, UUID_DCE);
	IF_FEATURE_BLKID_TYPE(id->type = "bcache";)

	return 0;
}
