/* vi: set sw=4 ts=4: */
/*
 * mkfs_reiser: utility to create ReiserFS filesystem
 *
 * Busybox'ed (2009) by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config MKFS_REISER
//config:	bool "mkfs_reiser"
//config:	default n
//config:	select PLATFORM_LINUX
//config:	help
//config:	Utility to create ReiserFS filesystems.
//config:	Note: this applet needs a lot of testing and polishing.

//applet:IF_MKFS_REISER(APPLET_ODDNAME(mkfs.reiser, mkfs_reiser, BB_DIR_SBIN, BB_SUID_DROP, mkfs_reiser))

//kbuild:lib-$(CONFIG_MKFS_REISER) += mkfs_reiser.o

//usage:#define mkfs_reiser_trivial_usage
//usage:       "[-f] [-l LABEL] BLOCKDEV [4K-BLOCKS]"
//usage:#define mkfs_reiser_full_usage "\n\n"
//usage:       "Make a ReiserFS V3 filesystem\n"
//usage:     "\n	-f	Force"
//usage:     "\n	-l LBL	Volume label"

#include "libbb.h"
#include <linux/fs.h>

char BUG_wrong_field_size(void);
#define STORE_LE(field, value) \
do { \
	if (sizeof(field) == 4) \
		field = SWAP_LE32(value); \
	else if (sizeof(field) == 2) \
		field = SWAP_LE16(value); \
	else if (sizeof(field) == 1) \
		field = (value); \
	else \
		BUG_wrong_field_size(); \
} while (0)

#define FETCH_LE32(field) \
	(sizeof(field) == 4 ? SWAP_LE32(field) : BUG_wrong_field_size())

struct journal_params {
	uint32_t jp_journal_1st_block;      /* where does journal start from on its device */
	uint32_t jp_journal_dev;            /* journal device st_rdev */
	uint32_t jp_journal_size;           /* size of the journal on FS creation. used to make sure they don't overflow it */
	uint32_t jp_journal_trans_max;      /* max number of blocks in a transaction.  */
	uint32_t jp_journal_magic;          /* random value made on fs creation (this was sb_journal_block_count) */
	uint32_t jp_journal_max_batch;      /* max number of blocks to batch into a trans */
	uint32_t jp_journal_max_commit_age; /* in seconds, how old can an async commit be */
	uint32_t jp_journal_max_trans_age;  /* in seconds, how old can a transaction be */
};

struct reiserfs_journal_header {
	uint32_t jh_last_flush_trans_id;    /* id of last fully flushed transaction */
	uint32_t jh_first_unflushed_offset; /* offset in the log of where to start replay after a crash */
	uint32_t jh_mount_id;
	struct journal_params jh_journal;
	uint32_t jh_last_check_mount_id;    /* the mount id of the fs during the last reiserfsck --check. */
};

struct reiserfs_super_block {
	uint32_t sb_block_count;            /* 0 number of block on data device */
	uint32_t sb_free_blocks;            /* 4 free blocks count */
	uint32_t sb_root_block;             /* 8 root of the tree */

	struct journal_params sb_journal;   /* 12 */

	uint16_t sb_blocksize;          /* 44 */
	uint16_t sb_oid_maxsize;        /* 46 max size of object id array, see get_objectid() commentary */
	uint16_t sb_oid_cursize;        /* 48 current size of object id array */
	uint16_t sb_umount_state;       /* 50 this is set to 1 when filesystem was umounted, to 2 - when not */

	char s_magic[10];               /* 52 "ReIsErFs" or "ReIsEr2Fs" or "ReIsEr3Fs" */
	uint16_t sb_fs_state;           /* 62 it is set to used by fsck to mark which phase of rebuilding is done (used for fsck debugging) */
	uint32_t sb_hash_function_code; /* 64 code of function which was/is/will be used to sort names in a directory. See codes in above */
	uint16_t sb_tree_height;        /* 68 height of filesytem tree. Tree consisting of only one root block has 2 here */
	uint16_t sb_bmap_nr;            /* 70 amount of bitmap blocks needed to address each block of file system */
	uint16_t sb_version;            /* 72 this field is only reliable on filesystem with non-standard journal */
	uint16_t sb_reserved_for_journal;  /* 74 size in blocks of journal area on main device, we need to keep after non-standard journal relocation */
	uint32_t sb_inode_generation;   /* 76 */
	uint32_t sb_flags;              /* 80 Right now used only by inode-attributes, if enabled */
	unsigned char s_uuid[16];       /* 84 filesystem unique identifier */
	unsigned char s_label[16];      /* 100 filesystem volume label */
	uint16_t sb_mnt_count;          /* 116 */
	uint16_t sb_max_mnt_count;      /* 118 */
	uint32_t sb_lastcheck;          /* 120 */
	uint32_t sb_check_interval;     /* 124 */
/* zero filled by mkreiserfs and reiserfs_convert_objectid_map_v1() so any additions must be updated there as well. */
	char s_unused[76];              /* 128 */
	/* 204 */
};

/* Header of a disk block.  More precisely, header of a formatted leaf
   or internal node, and not the header of an unformatted node. */
struct block_head {
	uint16_t blk2_level;        /* Level of a block in the tree. */
	uint16_t blk2_nr_item;      /* Number of keys/items in a block. */
	uint16_t blk2_free_space;   /* Block free space in bytes. */
	uint16_t blk_reserved;
	uint32_t reserved[4];
};

#define REISERFS_DISK_OFFSET_IN_BYTES (64 * 1024)

#define REISERFS_3_6_SUPER_MAGIC_STRING "ReIsEr2Fs"
#define REISERFS_FORMAT_3_6     2
#define DEFAULT_MAX_MNT_COUNT   30                      /* 30 mounts */
#define DEFAULT_CHECK_INTERVAL  (180 * 60 * 60 * 24)    /* 180 days */

#define FS_CLEANLY_UMOUNTED     1 /* this was REISERFS_VALID_FS */

#define JOURNAL_MIN_SIZE        512
/* biggest possible single transaction, don't change for now (8/3/99) */
#define JOURNAL_TRANS_MAX       1024
#define JOURNAL_TRANS_MIN       256     /* need to check whether it works */
#define JOURNAL_DEFAULT_RATIO   8       /* default journal size / max trans length */
#define JOURNAL_MIN_RATIO       2
/* max blocks to batch into one transaction, don't make this any bigger than 900 */
#define JOURNAL_MAX_BATCH       900
#define JOURNAL_MAX_COMMIT_AGE  30


// Standard mkreiserfs 3.6.21:
//   -b | --block-size N              size of file-system block, in bytes
//   -j | --journal-device FILE       path to separate device to hold journal
//   -s | --journal-size N            size of the journal in blocks
//   -o | --journal-offset N          offset of the journal from the start of
//                                    the separate device, in blocks
//   -t | --transaction-max-size N    maximal size of transaction, in blocks
//   -B | --badblocks file            store all bad blocks given in file on the fs
//   -h | --hash rupasov|tea|r5       hash function to use by default
//   -u | --uuid UUID                 store UUID in the superblock
//   -l | --label LABEL               store LABEL in the superblock
//   --format 3.5|3.6                 old 3.5 format or newer 3.6
//   -f | --force                     specified once, make mkreiserfs the whole
//                                    disk, not block device or mounted partition;
//                                    specified twice, do not ask for confirmation
//   -q | --quiet                     quiet work without messages, progress and
//                                    questions. Useful if run in a script. For use
//                                    by end users only.
//   -d | --debug                     print debugging information during mkreiser
//   -V                               print version and exit

// Options not commented below are taken but silently ignored:
enum {
	OPT_b = 1 << 0,
	OPT_j = 1 << 1,
	OPT_s = 1 << 2,
	OPT_o = 1 << 3,
	OPT_t = 1 << 4,
	OPT_B = 1 << 5,
	OPT_h = 1 << 6,
	OPT_u = 1 << 7,
	OPT_l = 1 << 8,		// label
	OPT_f = 1 << 9,		// ask no questions
	OPT_q = 1 << 10,
	OPT_d = 1 << 11,
	//OPT_V = 1 << 12,	// -V version. bbox applets don't support that
};

int mkfs_reiser_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int mkfs_reiser_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned blocksize = 4096;
	unsigned journal_blocks = 8192;
	unsigned blocks, bitmap_blocks, i, block;
	time_t timestamp;
	const char *label = "";
	struct stat st;
	int fd;
	uint8_t *buf;
	struct reiserfs_super_block *sb;
	struct journal_params *jp;
	struct block_head *root;

	// using global "option_mask32" instead of local "opts":
	// we are register starved here
	/*opts =*/ getopt32(argv, "^" "b:+j:s:o:t:B:h:u:l:fqd" "\0" "-1",
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &label);
	argv += optind; // argv[0] -- device

	// check the device is a block device
	fd = xopen(argv[0], O_WRONLY | O_EXCL);
	xfstat(fd, &st, argv[0]);
	if (!S_ISBLK(st.st_mode) && !(option_mask32 & OPT_f))
		bb_error_msg_and_die("%s: not a block device", argv[0]);

	// check if it is mounted
	// N.B. what if we format a file? find_mount_point will return false negative since
	// it is loop block device which is mounted!
	if (find_mount_point(argv[0], 0))
		bb_error_msg_and_die("can't format mounted filesystem");

	// open the device, get size in blocks
	blocks = get_volume_size_in_bytes(fd, argv[1], blocksize, /*extend:*/ 1) / blocksize;

	// block number sanity check
	// we have a limit: skipped area, super block, journal and root block
	// all have to be addressed by one first bitmap
	block = REISERFS_DISK_OFFSET_IN_BYTES / blocksize // boot area
		+ 1		// sb
		+ 1		// bitmap#0
		+ journal_blocks+1	// journal
	;

	// count overhead
	bitmap_blocks = (blocks - 1) / (blocksize * 8) + 1;
	i = block + bitmap_blocks;

	// check overhead
	if (MIN(blocksize * 8, blocks) < i)
		bb_error_msg_and_die("need >= %u blocks", i);

	// ask confirmation?
	// TODO: ???

	// wipe out first REISERFS_DISK_OFFSET_IN_BYTES of device
	// TODO: do we really need to wipe?!
	xlseek(fd, REISERFS_DISK_OFFSET_IN_BYTES, SEEK_SET);

	// fill superblock
	sb = (struct reiserfs_super_block *)xzalloc(blocksize);
	// block count
	STORE_LE(sb->sb_block_count, blocks);
	STORE_LE(sb->sb_free_blocks, blocks - i);
	// TODO: decypher!
	STORE_LE(sb->sb_root_block, block);
	// fill journal related fields
	jp = &sb->sb_journal;
	STORE_LE(jp->jp_journal_1st_block, REISERFS_DISK_OFFSET_IN_BYTES / blocksize + 1/*sb*/ + 1/*bmp#0*/);
	timestamp = time(NULL);
	srand(timestamp);
	STORE_LE(jp->jp_journal_magic, rand());
	STORE_LE(jp->jp_journal_size, journal_blocks);
	STORE_LE(jp->jp_journal_trans_max, JOURNAL_TRANS_MAX);
	STORE_LE(jp->jp_journal_max_batch, JOURNAL_MAX_BATCH);
	STORE_LE(jp->jp_journal_max_commit_age, JOURNAL_MAX_COMMIT_AGE);
	// sizes
	STORE_LE(sb->sb_blocksize, blocksize);
	STORE_LE(sb->sb_oid_maxsize, (blocksize - sizeof(*sb)) / sizeof(uint32_t) / 2 * 2);
	STORE_LE(sb->sb_oid_cursize, 2); // "." and ".."
	strcpy(sb->s_magic, REISERFS_3_6_SUPER_MAGIC_STRING);
	STORE_LE(sb->sb_bmap_nr, (bitmap_blocks > ((1LL << 16) - 1)) ? 0 : bitmap_blocks);
	// misc
	STORE_LE(sb->sb_version, REISERFS_FORMAT_3_6);
	STORE_LE(sb->sb_lastcheck, timestamp);
	STORE_LE(sb->sb_check_interval, DEFAULT_CHECK_INTERVAL);
	STORE_LE(sb->sb_mnt_count, 1);
	STORE_LE(sb->sb_max_mnt_count, DEFAULT_MAX_MNT_COUNT);
	STORE_LE(sb->sb_umount_state, FS_CLEANLY_UMOUNTED);
	STORE_LE(sb->sb_tree_height, 2);
	STORE_LE(sb->sb_hash_function_code, 3); // R5_HASH
	STORE_LE(sb->sb_flags, 1);
	//STORE_LE(sb->sb_reserved_for_journal, 0);
	// create UUID
	generate_uuid(sb->s_uuid);
	// write the label
	safe_strncpy((char *)sb->s_label, label, sizeof(sb->s_label));

	// TODO: EMPIRIC! ENDIANNESS!
	// superblock has only 204 bytes. What are these?
	buf = (uint8_t *)sb;
	buf[205] = 1;
	buf[209] = 3;

	// put superblock
	xwrite(fd, sb, blocksize);

	// create bitmaps
	buf = xzalloc(blocksize);

	// bitmap #0 uses initial "block"+1 blocks
	i = block + 1;
	memset(buf, 0xFF, i / 8);
	buf[i / 8] = (1 << (i & 7)) - 1; //0..7 => 00000000..01111111
	// mark trailing absent blocks, if any
	if (blocks < 8*blocksize) {
		unsigned n = 8*blocksize - blocks;
		i = n / 8;
		buf[blocksize - i - 1] |= 0x7F00 >> (n & 7); //0..7 => 00000000..11111110
		memset(buf + blocksize - i, 0xFF, i); // N.B. no overflow here!
	}
	// put bitmap #0
	xwrite(fd, buf, blocksize);

	// now go journal blocks
	memset(buf, 0, blocksize);
	for (i = 0; i < journal_blocks; i++)
		xwrite(fd, buf, blocksize);
	// dump journal control block
	memcpy(&((struct reiserfs_journal_header *)buf)->jh_journal, &sb->sb_journal, sizeof(sb->sb_journal));
	xwrite(fd, buf, blocksize);

	// other bitmaps are in every (8*blocksize)-th block
	// N.B. they use the only block -- namely bitmap itself!
	buf[0] = 0x01;
	// put bitmaps
	for (i = 1; i < bitmap_blocks; i++) {
		xlseek(fd, i*8*blocksize * blocksize, SEEK_SET);
		// mark trailing absent blocks, if any
		if (i == bitmap_blocks - 1 && (blocks % (8*blocksize))) {
			unsigned n = 8*blocksize - blocks % (8*blocksize);
			unsigned j = n / 8;
			buf[blocksize - j - 1] |= 0x7F00 >> (n & 7); //0..7 => 00000000..11111110
			memset(buf + blocksize - j, 0xFF, j); // N.B. no overflow here!
		}
		xwrite(fd, buf, blocksize);
	}

	// fill root block
	// block head
	memset(buf, 0, blocksize);
	root = (struct block_head *)buf;
	STORE_LE(root->blk2_level, 1); // leaf node
	STORE_LE(root->blk2_nr_item, 2); // "." and ".."
	STORE_LE(root->blk2_free_space, blocksize - sizeof(struct block_head));
	// item head
	// root directory
	// TODO: EMPIRIC! ENDIANNESS!
	// TODO: indented assignments seem to be timestamps
buf[4] = 0134;
buf[24] = 01;
buf[28] = 02;
buf[42] = 054;
buf[44] = 0324;
buf[45] = 017;
buf[46] = 01;
buf[48] = 01;
buf[52] = 02;
buf[56] = 01;
buf[60] = 0364;
buf[61] = 01;
buf[64] = 02;
buf[66] = 060;
buf[68] = 0244;
buf[69] = 017;
buf[4004] = 01;
buf[4008] = 01;
buf[4012] = 02;
buf[4016] = 050;
buf[4018] = 04;
buf[4020] = 02;
buf[4028] = 01;
buf[4032] = 040;
buf[4034] = 04;

buf[4036] = 056; buf[4037] = 056;	// ".."
buf[4044] = 056;			// "."

buf[4052] = 0355;
buf[4053] = 0101;
buf[4056] = 03;
buf[4060] = 060;
		buf[4076] = 0173;
		buf[4077] = 0240;
	buf[4078] = 0344;
	buf[4079] = 0112;
		buf[4080] = 0173;
		buf[4081] = 0240;
	buf[4082] = 0344;
	buf[4083] = 0112;
		buf[4084] = 0173;
		buf[4085] = 0240;
	buf[4086] = 0344;
	buf[4087] = 0112;
buf[4088] = 01;

	// put root block
	xlseek(fd, block * blocksize, SEEK_SET);
	xwrite(fd, buf, blocksize);

	// cleanup
	if (ENABLE_FEATURE_CLEAN_UP) {
		free(buf);
		free(sb);
	}

	xclose(fd);
	return EXIT_SUCCESS;
}
