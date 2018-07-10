/* vi: set sw=4 ts=4: */
/*
 * mkfs_ext2: utility to create EXT2 filesystem
 * inspired by genext2fs
 *
 * Busybox'ed (2009) by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config MKE2FS
//config:	bool "mke2fs (9.7 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Utility to create EXT2 filesystems.
//config:
//config:config MKFS_EXT2
//config:	bool "mkfs.ext2 (9.8 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Alias to "mke2fs".

//                    APPLET_ODDNAME:name       main       location     suid_type     help
//applet:IF_MKE2FS(   APPLET_ODDNAME(mke2fs,    mkfs_ext2, BB_DIR_SBIN, BB_SUID_DROP, mkfs_ext2))
//applet:IF_MKFS_EXT2(APPLET_ODDNAME(mkfs.ext2, mkfs_ext2, BB_DIR_SBIN, BB_SUID_DROP, mkfs_ext2))
////////:IF_MKFS_EXT3(APPLET_ODDNAME(mkfs.ext3, mkfs_ext2, BB_DIR_SBIN, BB_SUID_DROP, mkfs_ext2))

//kbuild:lib-$(CONFIG_MKE2FS) += mkfs_ext2.o
//kbuild:lib-$(CONFIG_MKFS_EXT2) += mkfs_ext2.o

//usage:#define mkfs_ext2_trivial_usage
//usage:       "[-Fn] "
/* //usage:    "[-c|-l filename] " */
//usage:       "[-b BLK_SIZE] "
/* //usage:    "[-f fragment-size] [-g blocks-per-group] " */
//usage:       "[-i INODE_RATIO] [-I INODE_SIZE] "
/* //usage:    "[-j] [-J journal-options] [-N number-of-inodes] " */
//usage:       "[-m RESERVED_PERCENT] "
/* //usage:    "[-o creator-os] [-O feature[,...]] [-q] " */
/* //usage:    "[r fs-revision-level] [-E extended-options] [-v] [-F] " */
//usage:       "[-L LABEL] "
/* //usage:    "[-M last-mounted-directory] [-S] [-T filesystem-type] " */
//usage:       "BLOCKDEV [KBYTES]"
//usage:#define mkfs_ext2_full_usage "\n\n"
//usage:       "	-b BLK_SIZE	Block size, bytes"
/* //usage:  "\n	-c		Check device for bad blocks" */
/* //usage:  "\n	-E opts		Set extended options" */
/* //usage:  "\n	-f size		Fragment size in bytes" */
//usage:     "\n	-F		Force"
/* //usage:  "\n	-g N		Number of blocks in a block group" */
//usage:     "\n	-i RATIO	Max number of files is filesystem_size / RATIO"
//usage:     "\n	-I BYTES	Inode size (min 128)"
/* //usage:  "\n	-j		Create a journal (ext3)" */
/* //usage:  "\n	-J opts		Set journal options (size/device)" */
/* //usage:  "\n	-l file		Read bad blocks list from file" */
//usage:     "\n	-L LBL		Volume label"
//usage:     "\n	-m PERCENT	Percent of blocks to reserve for admin"
/* //usage:  "\n	-M dir		Set last mounted directory" */
//usage:     "\n	-n		Dry run"
/* //usage:  "\n	-N N		Number of inodes to create" */
/* //usage:  "\n	-o os		Set the 'creator os' field" */
/* //usage:  "\n	-O features	Dir_index/filetype/has_journal/journal_dev/sparse_super" */
/* //usage:  "\n	-q		Quiet" */
/* //usage:  "\n	-r rev		Set filesystem revision" */
/* //usage:  "\n	-S		Write superblock and group descriptors only" */
/* //usage:  "\n	-T fs-type	Set usage type (news/largefile/largefile4)" */
/* //usage:  "\n	-v		Verbose" */

#include "libbb.h"
#include <linux/fs.h>
#include "bb_e2fs_defs.h"

#define ENABLE_FEATURE_MKFS_EXT2_RESERVED_GDT 0
#define ENABLE_FEATURE_MKFS_EXT2_DIR_INDEX    1

#define EXT2_HASH_HALF_MD4       1
#define EXT2_FLAGS_SIGNED_HASH   0x0001
#define EXT2_FLAGS_UNSIGNED_HASH 0x0002

// storage helpers
char BUG_wrong_field_size(void);
#define STORE_LE(field, value) \
do { \
	if (sizeof(field) == 4) \
		field = SWAP_LE32((uint32_t)(value)); \
	else if (sizeof(field) == 2) \
		field = SWAP_LE16((uint16_t)(value)); \
	else if (sizeof(field) == 1) \
		field = (uint8_t)(value); \
	else \
		BUG_wrong_field_size(); \
} while (0)

#define FETCH_LE32(field) \
	(sizeof(field) == 4 ? SWAP_LE32(field) : BUG_wrong_field_size())

// All fields are little-endian
struct ext2_dir {
	uint32_t inode1;
	uint16_t rec_len1;
	uint8_t  name_len1;
	uint8_t  file_type1;
	char     name1[4];
	uint32_t inode2;
	uint16_t rec_len2;
	uint8_t  name_len2;
	uint8_t  file_type2;
	char     name2[4];
	uint32_t inode3;
	uint16_t rec_len3;
	uint8_t  name_len3;
	uint8_t  file_type3;
	char     name3[12];
};

static unsigned int_log2(unsigned arg)
{
	unsigned r = 0;
	while ((arg >>= 1) != 0)
		r++;
	return r;
}

// taken from mkfs_minix.c. libbb candidate?
// "uint32_t size", since we never use it for anything >32 bits
static uint32_t div_roundup(uint32_t size, uint32_t n)
{
	// Overflow-resistant
	uint32_t res = size / n;
	if (res * n != size)
		res++;
	return res;
}

static void allocate(uint8_t *bitmap, uint32_t blocksize, uint32_t start, uint32_t end)
{
	uint32_t i;

//bb_error_msg("ALLOC: [%u][%u][%u]: [%u-%u]:=[%x],[%x]", blocksize, start, end, start/8, blocksize - end/8 - 1, (1 << (start & 7)) - 1, (uint8_t)(0xFF00 >> (end & 7)));
	memset(bitmap, 0, blocksize);
	i = start / 8;
	memset(bitmap, 0xFF, i);
	bitmap[i] = (1 << (start & 7)) - 1; //0..7 => 00000000..01111111
	i = end / 8;
	bitmap[blocksize - i - 1] |= 0x7F00 >> (end & 7); //0..7 => 00000000..11111110
	memset(bitmap + blocksize - i, 0xFF, i); // N.B. no overflow here!
}

static uint32_t has_super(uint32_t x)
{
	// 0, 1 and powers of 3, 5, 7 up to 2^32 limit
	static const uint32_t supers[] = {
		0, 1, 3, 5, 7, 9, 25, 27, 49, 81, 125, 243, 343, 625, 729,
		2187, 2401, 3125, 6561, 15625, 16807, 19683, 59049, 78125,
		117649, 177147, 390625, 531441, 823543, 1594323, 1953125,
		4782969, 5764801, 9765625, 14348907, 40353607, 43046721,
		48828125, 129140163, 244140625, 282475249, 387420489,
		1162261467, 1220703125, 1977326743, 3486784401/* >2^31 */,
	};
	const uint32_t *sp = supers + ARRAY_SIZE(supers);
	while (1) {
		sp--;
		if (x == *sp)
			return 1;
		if (x > *sp)
			return 0;
	}
}

#define fd 3	/* predefined output descriptor */

static void PUT(uint64_t off, void *buf, uint32_t size)
{
	//bb_error_msg("PUT[%llu]:[%u]", off, size);
	xlseek(fd, off, SEEK_SET);
	xwrite(fd, buf, size);
}

// 128 and 256-byte inodes:
// 128-byte inode is described by struct ext2_inode.
// 256-byte one just has these fields appended:
//      __u16   i_extra_isize;
//      __u16   i_pad1;
//      __u32   i_ctime_extra;  /* extra Change time (nsec << 2 | epoch) */
//      __u32   i_mtime_extra;  /* extra Modification time (nsec << 2 | epoch) */
//      __u32   i_atime_extra;  /* extra Access time (nsec << 2 | epoch) */
//      __u32   i_crtime;       /* File creation time */
//      __u32   i_crtime_extra; /* extra File creation time (nsec << 2 | epoch)*/
//      __u32   i_version_hi;   /* high 32 bits for 64-bit version */
// the rest is padding.
//
// linux/ext2_fs.h has "#define i_size_high i_dir_acl" which suggests that even
// 128-byte inode is capable of describing large files (i_dir_acl is meaningful
// only for directories, which never need i_size_high).
//
// Standard mke2fs creates a filesystem with 256-byte inodes if it is
// bigger than 0.5GB.

// Standard mke2fs 1.41.9:
// Usage: mke2fs [-c|-l filename] [-b block-size] [-f fragment-size]
//	[-i bytes-per-inode] [-I inode-size] [-J journal-options]
//	[-G meta group size] [-N number-of-inodes]
//	[-m reserved-blocks-percentage] [-o creator-os]
//	[-g blocks-per-group] [-L volume-label] [-M last-mounted-directory]
//	[-O feature[,...]] [-r fs-revision] [-E extended-option[,...]]
//	[-T fs-type] [-U UUID] [-jnqvFSV] device [blocks-count]
//
// Options not commented below are taken but silently ignored:
enum {
	OPT_c = 1 << 0,
	OPT_l = 1 << 1,
	OPT_b = 1 << 2,		// block size, in bytes
	OPT_f = 1 << 3,
	OPT_i = 1 << 4,		// bytes per inode
	OPT_I = 1 << 5,		// custom inode size, in bytes
	OPT_J = 1 << 6,
	OPT_G = 1 << 7,
	OPT_N = 1 << 8,
	OPT_m = 1 << 9,		// percentage of blocks reserved for superuser
	OPT_o = 1 << 10,
	OPT_g = 1 << 11,
	OPT_L = 1 << 12,	// label
	OPT_M = 1 << 13,
	OPT_O = 1 << 14,
	OPT_r = 1 << 15,
	OPT_E = 1 << 16,
	OPT_T = 1 << 17,
	OPT_U = 1 << 18,
	OPT_j = 1 << 19,
	OPT_n = 1 << 20,	// dry run: do not write anything
	OPT_q = 1 << 21,
	OPT_v = 1 << 22,
	OPT_F = 1 << 23,
	OPT_S = 1 << 24,
	//OPT_V = 1 << 25,	// -V version. bbox applets don't support that
};

int mkfs_ext2_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int mkfs_ext2_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned i, pos, n;
	unsigned bs, bpi;
	unsigned blocksize, blocksize_log2;
	unsigned inodesize, user_inodesize;
	unsigned reserved_percent = 5;
	unsigned long long kilobytes;
	uint32_t nblocks, nblocks_full;
	uint32_t nreserved;
	uint32_t ngroups;
	uint32_t bytes_per_inode;
	uint32_t first_block;
	uint32_t inodes_per_group;
	uint32_t group_desc_blocks;
	uint32_t inode_table_blocks;
	uint32_t lost_and_found_blocks;
	time_t timestamp;
	const char *label = "";
	struct stat st;
	struct ext2_super_block *sb; // superblock
	struct ext2_group_desc *gd; // group descriptors
	struct ext2_inode *inode;
	struct ext2_dir *dir;
	uint8_t *buf;

	// using global "option_mask32" instead of local "opts":
	// we are register starved here
	/*opts =*/ getopt32(argv, "cl:b:+f:i:+I:+J:G:N:m:+o:g:L:M:O:r:E:T:U:jnqvFS",
		/*lbfi:*/ NULL, &bs, NULL, &bpi,
		/*IJGN:*/ &user_inodesize, NULL, NULL, NULL,
		/*mogL:*/ &reserved_percent, NULL, NULL, &label,
		/*MOrE:*/ NULL, NULL, NULL, NULL,
		/*TU:*/ NULL, NULL);
	argv += optind; // argv[0] -- device

	// open the device, check the device is a block device
	xmove_fd(xopen(argv[0], O_WRONLY), fd);
	xfstat(fd, &st, argv[0]);
	if (!S_ISBLK(st.st_mode) && !(option_mask32 & OPT_F))
		bb_error_msg_and_die("%s: not a block device", argv[0]);

	// check if it is mounted
	// N.B. what if we format a file? find_mount_point will return false negative since
	// it is loop block device which is mounted!
	if (find_mount_point(argv[0], 0))
		bb_error_msg_and_die("can't format mounted filesystem");

	// get size in kbytes
	kilobytes = get_volume_size_in_bytes(fd, argv[1], 1024, /*extend:*/ !(option_mask32 & OPT_n)) / 1024;

	bytes_per_inode = 16384;
	if (kilobytes < 512*1024)
		bytes_per_inode = 4096;
	if (kilobytes < 3*1024)
		bytes_per_inode = 8192;
	if (option_mask32 & OPT_i)
		bytes_per_inode = bpi;

	// Determine block size and inode size
	// block size is a multiple of 1024
	// inode size is a multiple of 128
	blocksize = 1024;
	inodesize = sizeof(struct ext2_inode); // 128
	if (kilobytes >= 512*1024) { // mke2fs 1.41.9 compat
		blocksize = 4096;
		inodesize = 256;
	}
	if (EXT2_MAX_BLOCK_SIZE > 4096) {
		// kilobytes >> 22 == size in 4gigabyte chunks.
		// if size >= 16k gigs, blocksize must be increased.
		// Try "mke2fs -F image $((16 * 1024*1024*1024))"
		while ((kilobytes >> 22) >= blocksize)
			blocksize *= 2;
	}
	if (option_mask32 & OPT_b)
		blocksize = bs;
	if (blocksize < EXT2_MIN_BLOCK_SIZE
	 || blocksize > EXT2_MAX_BLOCK_SIZE
	 || (blocksize & (blocksize - 1)) // not power of 2
	) {
		bb_error_msg_and_die("blocksize %u is bad", blocksize);
	}
	// Do we have custom inode size?
	if (option_mask32 & OPT_I) {
		if (user_inodesize < sizeof(*inode)
		 || user_inodesize > blocksize
		 || (user_inodesize & (user_inodesize - 1)) // not power of 2
		) {
			bb_error_msg("-%c is bad", 'I');
		} else {
			inodesize = user_inodesize;
		}
	}

	if ((int32_t)bytes_per_inode < blocksize)
		bb_error_msg_and_die("-%c is bad", 'i');
	// number of bits in one block, i.e. 8*blocksize
#define blocks_per_group (8 * blocksize)
	first_block = (EXT2_MIN_BLOCK_SIZE == blocksize);
	blocksize_log2 = int_log2(blocksize);

	// Determine number of blocks
	kilobytes >>= (blocksize_log2 - EXT2_MIN_BLOCK_LOG_SIZE);
	nblocks = kilobytes;
	if (nblocks != kilobytes)
		bb_error_msg_and_die("block count doesn't fit in 32 bits");
#define kilobytes kilobytes_unused_after_this
	// Experimentally, standard mke2fs won't work on images smaller than 60k
	if (nblocks < 60)
		bb_error_msg_and_die("need >= 60 blocks");

	// How many reserved blocks?
	if (reserved_percent > 50)
		bb_error_msg_and_die("-%c is bad", 'm');
	nreserved = (uint64_t)nblocks * reserved_percent / 100;

	// N.B. killing e2fsprogs feature! Unused blocks don't account in calculations
	nblocks_full = nblocks;

	// If last block group is too small, nblocks may be decreased in order
	// to discard it, and control returns here to recalculate some
	// parameters.
	// Note: blocksize and bytes_per_inode are never recalculated.
 retry:
	// N.B. a block group can have no more than blocks_per_group blocks
	ngroups = div_roundup(nblocks - first_block, blocks_per_group);

	group_desc_blocks = div_roundup(ngroups, blocksize / sizeof(*gd));
	// TODO: reserved blocks must be marked as such in the bitmaps,
	// or resulting filesystem is corrupt
	if (ENABLE_FEATURE_MKFS_EXT2_RESERVED_GDT) {
		/*
		 * From e2fsprogs: Calculate the number of GDT blocks to reserve for online
		 * filesystem growth.
		 * The absolute maximum number of GDT blocks we can reserve is determined by
		 * the number of block pointers that can fit into a single block.
		 * We set it at 1024x the current filesystem size, or
		 * the upper block count limit (2^32), whichever is lower.
		 */
		uint32_t reserved_group_desc_blocks = 0xFFFFFFFF; // maximum block number
		if (nblocks < reserved_group_desc_blocks / 1024)
			reserved_group_desc_blocks = nblocks * 1024;
		reserved_group_desc_blocks = div_roundup(reserved_group_desc_blocks - first_block, blocks_per_group);
		reserved_group_desc_blocks = div_roundup(reserved_group_desc_blocks, blocksize / sizeof(*gd)) - group_desc_blocks;
		if (reserved_group_desc_blocks > blocksize / sizeof(uint32_t))
			reserved_group_desc_blocks = blocksize / sizeof(uint32_t);
		//TODO: STORE_LE(sb->s_reserved_gdt_blocks, reserved_group_desc_blocks);
		group_desc_blocks += reserved_group_desc_blocks;
	}

	{
		// N.B. e2fsprogs does as follows!
		uint32_t overhead, remainder;
		// ninodes is the max number of inodes in this filesystem
		uint32_t ninodes = ((uint64_t) nblocks_full * blocksize) / bytes_per_inode;
		if (ninodes < EXT2_GOOD_OLD_FIRST_INO+1)
			ninodes = EXT2_GOOD_OLD_FIRST_INO+1;
		inodes_per_group = div_roundup(ninodes, ngroups);
		// minimum number because the first EXT2_GOOD_OLD_FIRST_INO-1 are reserved
		if (inodes_per_group < 16)
			inodes_per_group = 16;
		// a block group can't have more inodes than blocks
		if (inodes_per_group > blocks_per_group)
			inodes_per_group = blocks_per_group;
		// adjust inodes per group so they completely fill the inode table blocks in the descriptor
		inodes_per_group = (div_roundup(inodes_per_group * inodesize, blocksize) * blocksize) / inodesize;
		// make sure the number of inodes per group is a multiple of 8
		inodes_per_group &= ~7;
		inode_table_blocks = div_roundup(inodes_per_group * inodesize, blocksize);

		// to be useful, lost+found should occupy at least 2 blocks (but not exceeding 16*1024 bytes),
		// and at most EXT2_NDIR_BLOCKS. So reserve these blocks right now
		/* Or e2fsprogs comment verbatim (what does it mean?):
		 * Ensure that lost+found is at least 2 blocks, so we always
		 * test large empty blocks for big-block filesystems. */
		lost_and_found_blocks = MIN(EXT2_NDIR_BLOCKS, 16 >> (blocksize_log2 - EXT2_MIN_BLOCK_LOG_SIZE));

		// the last group needs more attention: isn't it too small for possible overhead?
		overhead = (has_super(ngroups - 1) ? (1/*sb*/ + group_desc_blocks) : 0) + 1/*bbmp*/ + 1/*ibmp*/ + inode_table_blocks;
		remainder = (nblocks - first_block) % blocks_per_group;
		////can't happen, nblocks >= 60 guarantees this
		////if ((1 == ngroups)
		//// && remainder
		//// && (remainder < overhead + 1/* "/" */ + lost_and_found_blocks)
		////) {
		////	bb_error_msg_and_die("way small device");
		////}

		// Standard mke2fs uses 50. Looks like a bug in our calculation
		// of "remainder" or "overhead" - we don't match standard mke2fs
		// when we transition from one group to two groups
		// (a bit after 8M image size), but it works for two->three groups
		// transition (at 16M).
		if (remainder && (remainder < overhead + 50)) {
//bb_error_msg("CHOP[%u]", remainder);
			nblocks -= remainder;
			goto retry;
		}
	}

	if (nblocks_full - nblocks)
		printf("warning: %u blocks unused\n\n", nblocks_full - nblocks);
	printf(
		"Filesystem label=%s\n"
		"OS type: Linux\n"
		"Block size=%u (log=%u)\n"
		"Fragment size=%u (log=%u)\n"
		"%u inodes, %u blocks\n"
		"%u blocks (%u%%) reserved for the super user\n"
		"First data block=%u\n"
		"Maximum filesystem blocks=%u\n"
		"%u block groups\n"
		"%u blocks per group, %u fragments per group\n"
		"%u inodes per group"
		, label
		, blocksize, blocksize_log2 - EXT2_MIN_BLOCK_LOG_SIZE
		, blocksize, blocksize_log2 - EXT2_MIN_BLOCK_LOG_SIZE
		, inodes_per_group * ngroups, nblocks
		, nreserved, reserved_percent
		, first_block
		, group_desc_blocks * (blocksize / (unsigned)sizeof(*gd)) * blocks_per_group
		, ngroups
		, blocks_per_group, blocks_per_group
		, inodes_per_group
	);
	{
		const char *fmt = "\nSuperblock backups stored on blocks:\n"
			"\t%u";
		pos = first_block;
		for (i = 1; i < ngroups; i++) {
			pos += blocks_per_group;
			if (has_super(i)) {
				printf(fmt, (unsigned)pos);
				fmt = ", %u";
			}
		}
	}
	bb_putchar('\n');

	if (option_mask32 & OPT_n) {
		if (ENABLE_FEATURE_CLEAN_UP)
			close(fd);
		return EXIT_SUCCESS;
	}

	// TODO: 3/5 refuse if mounted
	// TODO: 4/5 compat options
	// TODO: 1/5 sanity checks
	// TODO: 0/5 more verbose error messages
	// TODO: 4/5 bigendianness: recheck, wait for ARM reporters
	// TODO: 2/5 reserved GDT: how to mark but not allocate?
	// TODO: 3/5 dir_index?

	// fill the superblock
	sb = xzalloc(1024);
	STORE_LE(sb->s_rev_level, EXT2_DYNAMIC_REV); // revision 1 filesystem
	STORE_LE(sb->s_magic, EXT2_SUPER_MAGIC);
	STORE_LE(sb->s_inode_size, inodesize);
	// set "Required extra isize" and "Desired extra isize" fields to 28
	if (inodesize != sizeof(*inode)) {
		STORE_LE(sb->s_min_extra_isize, 0x001c);
		STORE_LE(sb->s_want_extra_isize, 0x001c);
	}
	STORE_LE(sb->s_first_ino, EXT2_GOOD_OLD_FIRST_INO);
	STORE_LE(sb->s_log_block_size, blocksize_log2 - EXT2_MIN_BLOCK_LOG_SIZE);
	STORE_LE(sb->s_log_frag_size, blocksize_log2 - EXT2_MIN_BLOCK_LOG_SIZE);
	// first 1024 bytes of the device are for boot record. If block size is 1024 bytes, then
	// the first block is 1, otherwise 0
	STORE_LE(sb->s_first_data_block, first_block);
	// block and inode bitmaps occupy no more than one block, so maximum number of blocks is
	STORE_LE(sb->s_blocks_per_group, blocks_per_group);
	STORE_LE(sb->s_frags_per_group, blocks_per_group);
	// blocks
	STORE_LE(sb->s_blocks_count, nblocks);
	// reserve blocks for superuser
	STORE_LE(sb->s_r_blocks_count, nreserved);
	// ninodes
	STORE_LE(sb->s_inodes_per_group, inodes_per_group);
	STORE_LE(sb->s_inodes_count, inodes_per_group * ngroups);
	STORE_LE(sb->s_free_inodes_count, inodes_per_group * ngroups - EXT2_GOOD_OLD_FIRST_INO);
	// timestamps
	timestamp = time(NULL);
	STORE_LE(sb->s_mkfs_time, timestamp);
	STORE_LE(sb->s_wtime, timestamp);
	STORE_LE(sb->s_lastcheck, timestamp);
	// misc. Values are chosen to match mke2fs 1.41.9
	STORE_LE(sb->s_state, 1); // TODO: what's 1?
	STORE_LE(sb->s_creator_os, EXT2_OS_LINUX);
	STORE_LE(sb->s_checkinterval, 24*60*60 * 180); // 180 days
	STORE_LE(sb->s_errors, EXT2_ERRORS_DEFAULT);
	// mke2fs 1.41.9 also sets EXT3_FEATURE_COMPAT_RESIZE_INODE
	// and if >= 0.5GB, EXT3_FEATURE_RO_COMPAT_LARGE_FILE.
	// we use values which match "mke2fs -O ^resize_inode":
	// in this case 1.41.9 never sets EXT3_FEATURE_RO_COMPAT_LARGE_FILE.
	STORE_LE(sb->s_feature_compat, EXT2_FEATURE_COMPAT_SUPP
		| (EXT2_FEATURE_COMPAT_RESIZE_INO * ENABLE_FEATURE_MKFS_EXT2_RESERVED_GDT)
		| (EXT2_FEATURE_COMPAT_DIR_INDEX * ENABLE_FEATURE_MKFS_EXT2_DIR_INDEX)
	);
	STORE_LE(sb->s_feature_incompat, EXT2_FEATURE_INCOMPAT_FILETYPE);
	STORE_LE(sb->s_feature_ro_compat, EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER);
	STORE_LE(sb->s_flags, EXT2_FLAGS_UNSIGNED_HASH * ENABLE_FEATURE_MKFS_EXT2_DIR_INDEX);
	generate_uuid(sb->s_uuid);
	if (ENABLE_FEATURE_MKFS_EXT2_DIR_INDEX) {
		STORE_LE(sb->s_def_hash_version, EXT2_HASH_HALF_MD4);
		generate_uuid((uint8_t *)sb->s_hash_seed);
	}
	/*
	 * From e2fsprogs: add "jitter" to the superblock's check interval so that we
	 * don't check all the filesystems at the same time.  We use a
	 * kludgy hack of using the UUID to derive a random jitter value.
	 */
	STORE_LE(sb->s_max_mnt_count,
		EXT2_DFL_MAX_MNT_COUNT
		+ (sb->s_uuid[ARRAY_SIZE(sb->s_uuid)-1] % EXT2_DFL_MAX_MNT_COUNT));

	// write the label
	safe_strncpy((char *)sb->s_volume_name, label, sizeof(sb->s_volume_name));

	// calculate filesystem skeleton structures
	gd = xzalloc(group_desc_blocks * blocksize);
	buf = xmalloc(blocksize);
	sb->s_free_blocks_count = 0;
	for (i = 0, pos = first_block, n = nblocks - first_block;
		i < ngroups;
		i++, pos += blocks_per_group, n -= blocks_per_group
	) {
		uint32_t overhead = pos + (has_super(i) ? (1/*sb*/ + group_desc_blocks) : 0);
		uint32_t free_blocks;
		// fill group descriptors
		STORE_LE(gd[i].bg_block_bitmap, overhead + 0);
		STORE_LE(gd[i].bg_inode_bitmap, overhead + 1);
		STORE_LE(gd[i].bg_inode_table, overhead + 2);
		overhead = overhead - pos + 1/*bbmp*/ + 1/*ibmp*/ + inode_table_blocks;
		gd[i].bg_free_inodes_count = inodes_per_group;
		//STORE_LE(gd[i].bg_used_dirs_count, 0);
		// N.B. both "/" and "/lost+found" are within the first block group
		// "/" occupies 1 block, "/lost+found" occupies lost_and_found_blocks...
		if (0 == i) {
			// ... thus increased overhead for the first block group ...
			overhead += 1 + lost_and_found_blocks;
			// ... and 2 used directories
			STORE_LE(gd[i].bg_used_dirs_count, 2);
			// well known reserved inodes belong to the first block too
			gd[i].bg_free_inodes_count -= EXT2_GOOD_OLD_FIRST_INO;
		}

		// cache free block count of the group
		free_blocks = (n < blocks_per_group ? n : blocks_per_group) - overhead;

		// mark preallocated blocks as allocated
//bb_error_msg("ALLOC: [%u][%u][%u]", blocksize, overhead, blocks_per_group - (free_blocks + overhead));
		allocate(buf, blocksize,
			// reserve "overhead" blocks
			overhead,
			// mark unused trailing blocks
			blocks_per_group - (free_blocks + overhead)
		);
		// dump block bitmap
		PUT((uint64_t)(FETCH_LE32(gd[i].bg_block_bitmap)) * blocksize, buf, blocksize);
		STORE_LE(gd[i].bg_free_blocks_count, free_blocks);

		// mark preallocated inodes as allocated
		allocate(buf, blocksize,
			// mark reserved inodes
			inodes_per_group - gd[i].bg_free_inodes_count,
			// mark unused trailing inodes
			blocks_per_group - inodes_per_group
		);
		// dump inode bitmap
		//PUT((uint64_t)(FETCH_LE32(gd[i].bg_block_bitmap)) * blocksize, buf, blocksize);
		//but it's right after block bitmap, so we can just:
		xwrite(fd, buf, blocksize);
		STORE_LE(gd[i].bg_free_inodes_count, gd[i].bg_free_inodes_count);

		// count overall free blocks
		sb->s_free_blocks_count += free_blocks;
	}
	STORE_LE(sb->s_free_blocks_count, sb->s_free_blocks_count);

	// dump filesystem skeleton structures
//	printf("Writing superblocks and filesystem accounting information: ");
	for (i = 0, pos = first_block; i < ngroups; i++, pos += blocks_per_group) {
		// dump superblock and group descriptors and their backups
		if (has_super(i)) {
			// N.B. 1024 byte blocks are special
			PUT(((uint64_t)pos * blocksize) + ((0 == i && 1024 != blocksize) ? 1024 : 0),
					sb, 1024);
			PUT(((uint64_t)pos * blocksize) + blocksize,
					gd, group_desc_blocks * blocksize);
		}
	}

	// zero boot sectors
	memset(buf, 0, blocksize);
	// Disabled: standard mke2fs doesn't do this, and
	// on SPARC this destroys Sun disklabel.
	// Users who need/want zeroing can easily do it with dd.
	//PUT(0, buf, 1024); // N.B. 1024 <= blocksize, so buf[0..1023] contains zeros

	// zero inode tables
	for (i = 0; i < ngroups; ++i)
		for (n = 0; n < inode_table_blocks; ++n)
			PUT((uint64_t)(FETCH_LE32(gd[i].bg_inode_table) + n) * blocksize,
				buf, blocksize);

	// prepare directory inode
	inode = (struct ext2_inode *)buf;
	STORE_LE(inode->i_mode, S_IFDIR | S_IRWXU | S_IRGRP | S_IROTH | S_IXGRP | S_IXOTH);
	STORE_LE(inode->i_mtime, timestamp);
	STORE_LE(inode->i_atime, timestamp);
	STORE_LE(inode->i_ctime, timestamp);
	STORE_LE(inode->i_size, blocksize);
	// inode->i_blocks stores the number of 512 byte data blocks
	// (512, because it goes directly to struct stat without scaling)
	STORE_LE(inode->i_blocks, blocksize / 512);

	// dump root dir inode
	STORE_LE(inode->i_links_count, 3); // "/.", "/..", "/lost+found/.." point to this inode
	STORE_LE(inode->i_block[0], FETCH_LE32(gd[0].bg_inode_table) + inode_table_blocks);
	PUT(((uint64_t)FETCH_LE32(gd[0].bg_inode_table) * blocksize) + (EXT2_ROOT_INO-1) * inodesize,
				buf, inodesize);

	// dump lost+found dir inode
	STORE_LE(inode->i_links_count, 2); // both "/lost+found" and "/lost+found/." point to this inode
	STORE_LE(inode->i_size, lost_and_found_blocks * blocksize);
	STORE_LE(inode->i_blocks, (lost_and_found_blocks * blocksize) / 512);
	n = FETCH_LE32(inode->i_block[0]) + 1;
	for (i = 0; i < lost_and_found_blocks; ++i)
		STORE_LE(inode->i_block[i], i + n); // use next block
//bb_error_msg("LAST BLOCK USED[%u]", i + n);
	PUT(((uint64_t)FETCH_LE32(gd[0].bg_inode_table) * blocksize) + (EXT2_GOOD_OLD_FIRST_INO-1) * inodesize,
				buf, inodesize);

	// dump directories
	memset(buf, 0, blocksize);
	dir = (struct ext2_dir *)buf;

	// dump 2nd+ blocks of "/lost+found"
	STORE_LE(dir->rec_len1, blocksize); // e2fsck 1.41.4 compat (1.41.9 does not need this)
	for (i = 1; i < lost_and_found_blocks; ++i)
		PUT((uint64_t)(FETCH_LE32(gd[0].bg_inode_table) + inode_table_blocks + 1+i) * blocksize,
				buf, blocksize);

	// dump 1st block of "/lost+found"
	STORE_LE(dir->inode1, EXT2_GOOD_OLD_FIRST_INO);
	STORE_LE(dir->rec_len1, 12);
	STORE_LE(dir->name_len1, 1);
	STORE_LE(dir->file_type1, EXT2_FT_DIR);
	dir->name1[0] = '.';
	STORE_LE(dir->inode2, EXT2_ROOT_INO);
	STORE_LE(dir->rec_len2, blocksize - 12);
	STORE_LE(dir->name_len2, 2);
	STORE_LE(dir->file_type2, EXT2_FT_DIR);
	dir->name2[0] = '.'; dir->name2[1] = '.';
	PUT((uint64_t)(FETCH_LE32(gd[0].bg_inode_table) + inode_table_blocks + 1) * blocksize, buf, blocksize);

	// dump root dir block
	STORE_LE(dir->inode1, EXT2_ROOT_INO);
	STORE_LE(dir->rec_len2, 12);
	STORE_LE(dir->inode3, EXT2_GOOD_OLD_FIRST_INO);
	STORE_LE(dir->rec_len3, blocksize - 12 - 12);
	STORE_LE(dir->name_len3, 10);
	STORE_LE(dir->file_type3, EXT2_FT_DIR);
	strcpy(dir->name3, "lost+found");
	PUT((uint64_t)(FETCH_LE32(gd[0].bg_inode_table) + inode_table_blocks + 0) * blocksize, buf, blocksize);

	// cleanup
	if (ENABLE_FEATURE_CLEAN_UP) {
		free(buf);
		free(gd);
		free(sb);
	}

	xclose(fd);
	return EXIT_SUCCESS;
}
