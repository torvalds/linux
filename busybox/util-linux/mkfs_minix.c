/* vi: set sw=4 ts=4: */
/*
 * mkfs.c - make a linux (minix) file-system.
 *
 * (C) 1991 Linus Torvalds.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/*
 * DD.MM.YY
 *
 * 24.11.91  -	Time began. Used the fsck sources to get started.
 *
 * 25.11.91  -	Corrected some bugs. Added support for ".badblocks"
 *		The algorithm for ".badblocks" is a bit weird, but
 *		it should work. Oh, well.
 *
 * 25.01.92  -	Added the -l option for getting the list of bad blocks
 *		out of a named file. (Dave Rivers, rivers@ponds.uucp)
 *
 * 28.02.92  -	Added %-information when using -c.
 *
 * 28.02.93  -	Added support for other namelengths than the original
 *		14 characters so that I can test the new kernel routines..
 *
 * 09.10.93  -	Make exit status conform to that required by fsutil
 *		(Rik Faith, faith@cs.unc.edu)
 *
 * 31.10.93  -	Added inode request feature, for backup floppies: use
 *		32 inodes, for a news partition use more.
 *		(Scott Heavner, sdh@po.cwru.edu)
 *
 * 03.01.94  -	Added support for file system valid flag.
 *		(Dr. Wettstein, greg%wind.uucp@plains.nodak.edu)
 *
 * 30.10.94  -  added support for v2 filesystem
 *	        (Andreas Schwab, schwab@issan.informatik.uni-dortmund.de)
 *
 * 09.11.94  -	Added test to prevent overwrite of mounted fs adapted
 *		from Theodore Ts'o's (tytso@athena.mit.edu) mke2fs
 *		program.  (Daniel Quinlan, quinlan@yggdrasil.com)
 *
 * 03.20.95  -	Clear first 512 bytes of filesystem to make certain that
 *		the filesystem is not misidentified as a MS-DOS FAT filesystem.
 *		(Daniel Quinlan, quinlan@yggdrasil.com)
 *
 * 02.07.96  -  Added small patch from Russell King to make the program a
 *		good deal more portable (janl@math.uio.no)
 *
 * Usage:  mkfs [-c | -l filename ] [-v] [-nXX] [-iXX] device [size-in-blocks]
 *
 *	-c for readability checking (SLOW!)
 *      -l for getting a list of bad blocks from a file.
 *	-n for namelength (currently the kernel only uses 14 or 30)
 *	-i for number of inodes
 *	-v for v2 filesystem
 *
 * The device may be a block device or a image of one, but this isn't
 * enforced (but it's not much fun on a character device :-).
 *
 * Modified for BusyBox by Erik Andersen <andersen@debian.org> --
 *	removed getopt based parser and added a hand rolled one.
 */
//config:config MKFS_MINIX
//config:	bool "mkfs_minix"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	The minix filesystem is a nice, small, compact, read-write filesystem
//config:	with little overhead. If you wish to be able to create minix
//config:	filesystems this utility will do the job for you.
//config:
//config:config FEATURE_MINIX2
//config:	bool "Support Minix fs v2 (fsck_minix/mkfs_minix)"
//config:	default y
//config:	depends on FSCK_MINIX || MKFS_MINIX
//config:	help
//config:	If you wish to be able to create version 2 minix filesystems, enable
//config:	this. If you enabled 'mkfs_minix' then you almost certainly want to
//config:	be using the version 2 filesystem support.

//                     APPLET_ODDNAME:name        main        location     suid_type     help
//applet:IF_MKFS_MINIX(APPLET_ODDNAME(mkfs.minix, mkfs_minix, BB_DIR_SBIN, BB_SUID_DROP, mkfs_minix))

//kbuild:lib-$(CONFIG_MKFS_MINIX) += mkfs_minix.o

//usage:#define mkfs_minix_trivial_usage
//usage:       "[-c | -l FILE] [-nXX] [-iXX] BLOCKDEV [KBYTES]"
//usage:#define mkfs_minix_full_usage "\n\n"
//usage:       "Make a MINIX filesystem\n"
//usage:     "\n	-c		Check device for bad blocks"
//usage:     "\n	-n [14|30]	Maximum length of filenames"
//usage:     "\n	-i INODES	Number of inodes for the filesystem"
//usage:     "\n	-l FILE		Read bad blocks list from FILE"
//usage:     "\n	-v		Make version 2 filesystem"

#include "libbb.h"
#include <mntent.h>

#include "minix.h"

/* Store the very same times/uids/gids for image consistency */
#if 1
# define CUR_TIME 0
# define GETUID 0
# define GETGID 0
#else
/* Was using this. Is it useful? NB: this will break testsuite */
# define CUR_TIME time(NULL)
# define GETUID getuid()
# define GETGID getgid()
#endif

enum {
	MAX_GOOD_BLOCKS         = 512,
	TEST_BUFFER_BLOCKS      = 16,
};

#if !ENABLE_FEATURE_MINIX2
enum { version2 = 0 };
#endif

enum { dev_fd = 3 };

struct globals {
#if ENABLE_FEATURE_MINIX2
	smallint version2;
#define version2 G.version2
#endif
	char *device_name;
	uint32_t total_blocks;
	int badblocks;
	int namelen;
	int dirsize;
	int magic;
	char *inode_buffer;
	char *inode_map;
	char *zone_map;
	int used_good_blocks;
	unsigned long req_nr_inodes;
	unsigned currently_testing;

	char root_block[BLOCK_SIZE];
	union {
		char superblock_buffer[BLOCK_SIZE];
		struct minix_superblock SB;
	} u;
	char boot_block_buffer[512];
	unsigned short good_blocks_table[MAX_GOOD_BLOCKS];
	/* check_blocks(): buffer[] was the biggest static in entire bbox */
	char check_blocks_buffer[BLOCK_SIZE * TEST_BUFFER_BLOCKS];

	unsigned short ind_block1[BLOCK_SIZE >> 1];
	unsigned short dind_block1[BLOCK_SIZE >> 1];
	unsigned long ind_block2[BLOCK_SIZE >> 2];
	unsigned long dind_block2[BLOCK_SIZE >> 2];
};
#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)

static ALWAYS_INLINE unsigned div_roundup(unsigned size, unsigned n)
{
	return (size + n-1) / n;
}

#define INODE_BUF1              (((struct minix1_inode*)G.inode_buffer) - 1)
#define INODE_BUF2              (((struct minix2_inode*)G.inode_buffer) - 1)

#define SB                      (G.u.SB)

#define SB_INODES               (SB.s_ninodes)
#define SB_IMAPS                (SB.s_imap_blocks)
#define SB_ZMAPS                (SB.s_zmap_blocks)
#define SB_FIRSTZONE            (SB.s_firstdatazone)
#define SB_ZONE_SIZE            (SB.s_log_zone_size)
#define SB_MAXSIZE              (SB.s_max_size)
#define SB_MAGIC                (SB.s_magic)

#if !ENABLE_FEATURE_MINIX2
# define SB_ZONES               (SB.s_nzones)
# define INODE_BLOCKS           div_roundup(SB_INODES, MINIX1_INODES_PER_BLOCK)
#else
# define SB_ZONES               (version2 ? SB.s_zones : SB.s_nzones)
# define INODE_BLOCKS           div_roundup(SB_INODES, \
                                (version2 ? MINIX2_INODES_PER_BLOCK : MINIX1_INODES_PER_BLOCK))
#endif

#define INODE_BUFFER_SIZE       (INODE_BLOCKS * BLOCK_SIZE)
#define NORM_FIRSTZONE          (2 + SB_IMAPS + SB_ZMAPS + INODE_BLOCKS)

/* Before you ask "where they come from?": */
/* setbit/clrbit are supplied by sys/param.h */

static int minix_bit(const char* a, unsigned i)
{
	return a[i >> 3] & (1<<(i & 7));
}

static void minix_setbit(char *a, unsigned i)
{
	setbit(a, i);
}
static void minix_clrbit(char *a, unsigned i)
{
	clrbit(a, i);
}

/* Note: do not assume 0/1, it is 0/nonzero */
#define zone_in_use(x)  minix_bit(G.zone_map,(x)-SB_FIRSTZONE+1)
/*#define inode_in_use(x) minix_bit(G.inode_map,(x))*/

#define mark_inode(x)   minix_setbit(G.inode_map,(x))
#define unmark_inode(x) minix_clrbit(G.inode_map,(x))
#define mark_zone(x)    minix_setbit(G.zone_map,(x)-SB_FIRSTZONE+1)
#define unmark_zone(x)  minix_clrbit(G.zone_map,(x)-SB_FIRSTZONE+1)

#ifndef BLKGETSIZE
# define BLKGETSIZE     _IO(0x12,96)    /* return device size */
#endif

static void write_tables(void)
{
	/* Mark the superblock valid. */
	SB.s_state |= MINIX_VALID_FS;
	SB.s_state &= ~MINIX_ERROR_FS;

	msg_eol = "seek to 0 failed";
	xlseek(dev_fd, 0, SEEK_SET);

	msg_eol = "can't clear boot sector";
	xwrite(dev_fd, G.boot_block_buffer, 512);

	msg_eol = "seek to BLOCK_SIZE failed";
	xlseek(dev_fd, BLOCK_SIZE, SEEK_SET);

	msg_eol = "can't write superblock";
	xwrite(dev_fd, G.u.superblock_buffer, BLOCK_SIZE);

	msg_eol = "can't write inode map";
	xwrite(dev_fd, G.inode_map, SB_IMAPS * BLOCK_SIZE);

	msg_eol = "can't write zone map";
	xwrite(dev_fd, G.zone_map, SB_ZMAPS * BLOCK_SIZE);

	msg_eol = "can't write inodes";
	xwrite(dev_fd, G.inode_buffer, INODE_BUFFER_SIZE);

	msg_eol = "\n";
}

static void write_block(int blk, char *buffer)
{
	xlseek(dev_fd, blk * BLOCK_SIZE, SEEK_SET);
	xwrite(dev_fd, buffer, BLOCK_SIZE);
}

static int get_free_block(void)
{
	int blk;

	if (G.used_good_blocks + 1 >= MAX_GOOD_BLOCKS)
		bb_error_msg_and_die("too many bad blocks");
	if (G.used_good_blocks)
		blk = G.good_blocks_table[G.used_good_blocks - 1] + 1;
	else
		blk = SB_FIRSTZONE;
	while (blk < SB_ZONES && zone_in_use(blk))
		blk++;
	if (blk >= SB_ZONES)
		bb_error_msg_and_die("not enough good blocks");
	G.good_blocks_table[G.used_good_blocks] = blk;
	G.used_good_blocks++;
	return blk;
}

static void mark_good_blocks(void)
{
	int blk;

	for (blk = 0; blk < G.used_good_blocks; blk++)
		mark_zone(G.good_blocks_table[blk]);
}

static int next(int zone)
{
	if (!zone)
		zone = SB_FIRSTZONE - 1;
	while (++zone < SB_ZONES)
		if (zone_in_use(zone))
			return zone;
	return 0;
}

static void make_bad_inode(void)
{
	struct minix1_inode *inode = &INODE_BUF1[MINIX_BAD_INO];
	int i, j, zone;
	int ind = 0, dind = 0;
	/* moved to globals to reduce stack usage
	unsigned short ind_block[BLOCK_SIZE >> 1];
	unsigned short dind_block[BLOCK_SIZE >> 1];
	*/
#define ind_block (G.ind_block1)
#define dind_block (G.dind_block1)

#define NEXT_BAD (zone = next(zone))

	if (!G.badblocks)
		return;
	mark_inode(MINIX_BAD_INO);
	inode->i_nlinks = 1;
	/* BTW, setting this makes all images different */
	/* it's harder to check for bugs then - diff isn't helpful :(... */
	inode->i_time = CUR_TIME;
	inode->i_mode = S_IFREG + 0000;
	inode->i_size = G.badblocks * BLOCK_SIZE;
	zone = next(0);
	for (i = 0; i < 7; i++) {
		inode->i_zone[i] = zone;
		if (!NEXT_BAD)
			goto end_bad;
	}
	inode->i_zone[7] = ind = get_free_block();
	memset(ind_block, 0, BLOCK_SIZE);
	for (i = 0; i < 512; i++) {
		ind_block[i] = zone;
		if (!NEXT_BAD)
			goto end_bad;
	}
	inode->i_zone[8] = dind = get_free_block();
	memset(dind_block, 0, BLOCK_SIZE);
	for (i = 0; i < 512; i++) {
		write_block(ind, (char *) ind_block);
		dind_block[i] = ind = get_free_block();
		memset(ind_block, 0, BLOCK_SIZE);
		for (j = 0; j < 512; j++) {
			ind_block[j] = zone;
			if (!NEXT_BAD)
				goto end_bad;
		}
	}
	bb_error_msg_and_die("too many bad blocks");
 end_bad:
	if (ind)
		write_block(ind, (char *) ind_block);
	if (dind)
		write_block(dind, (char *) dind_block);
#undef ind_block
#undef dind_block
}

#if ENABLE_FEATURE_MINIX2
static void make_bad_inode2(void)
{
	struct minix2_inode *inode = &INODE_BUF2[MINIX_BAD_INO];
	int i, j, zone;
	int ind = 0, dind = 0;
	/* moved to globals to reduce stack usage
	unsigned long ind_block[BLOCK_SIZE >> 2];
	unsigned long dind_block[BLOCK_SIZE >> 2];
	*/
#define ind_block (G.ind_block2)
#define dind_block (G.dind_block2)

	if (!G.badblocks)
		return;
	mark_inode(MINIX_BAD_INO);
	inode->i_nlinks = 1;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CUR_TIME;
	inode->i_mode = S_IFREG + 0000;
	inode->i_size = G.badblocks * BLOCK_SIZE;
	zone = next(0);
	for (i = 0; i < 7; i++) {
		inode->i_zone[i] = zone;
		if (!NEXT_BAD)
			goto end_bad;
	}
	inode->i_zone[7] = ind = get_free_block();
	memset(ind_block, 0, BLOCK_SIZE);
	for (i = 0; i < 256; i++) {
		ind_block[i] = zone;
		if (!NEXT_BAD)
			goto end_bad;
	}
	inode->i_zone[8] = dind = get_free_block();
	memset(dind_block, 0, BLOCK_SIZE);
	for (i = 0; i < 256; i++) {
		write_block(ind, (char *) ind_block);
		dind_block[i] = ind = get_free_block();
		memset(ind_block, 0, BLOCK_SIZE);
		for (j = 0; j < 256; j++) {
			ind_block[j] = zone;
			if (!NEXT_BAD)
				goto end_bad;
		}
	}
	/* Could make triple indirect block here */
	bb_error_msg_and_die("too many bad blocks");
 end_bad:
	if (ind)
		write_block(ind, (char *) ind_block);
	if (dind)
		write_block(dind, (char *) dind_block);
#undef ind_block
#undef dind_block
}
#else
void make_bad_inode2(void);
#endif

static void make_root_inode(void)
{
	struct minix1_inode *inode = &INODE_BUF1[MINIX_ROOT_INO];

	mark_inode(MINIX_ROOT_INO);
	inode->i_zone[0] = get_free_block();
	inode->i_nlinks = 2;
	inode->i_time = CUR_TIME;
	if (G.badblocks)
		inode->i_size = 3 * G.dirsize;
	else {
		G.root_block[2 * G.dirsize] = '\0';
		G.root_block[2 * G.dirsize + 1] = '\0';
		inode->i_size = 2 * G.dirsize;
	}
	inode->i_mode = S_IFDIR + 0755;
	inode->i_uid = GETUID;
	if (inode->i_uid)
		inode->i_gid = GETGID;
	write_block(inode->i_zone[0], G.root_block);
}

#if ENABLE_FEATURE_MINIX2
static void make_root_inode2(void)
{
	struct minix2_inode *inode = &INODE_BUF2[MINIX_ROOT_INO];

	mark_inode(MINIX_ROOT_INO);
	inode->i_zone[0] = get_free_block();
	inode->i_nlinks = 2;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CUR_TIME;
	if (G.badblocks)
		inode->i_size = 3 * G.dirsize;
	else {
		G.root_block[2 * G.dirsize] = '\0';
		G.root_block[2 * G.dirsize + 1] = '\0';
		inode->i_size = 2 * G.dirsize;
	}
	inode->i_mode = S_IFDIR + 0755;
	inode->i_uid = GETUID;
	if (inode->i_uid)
		inode->i_gid = GETGID;
	write_block(inode->i_zone[0], G.root_block);
}
#else
void make_root_inode2(void);
#endif

/*
 * Perform a test of a block; return the number of
 * blocks readable.
 */
static size_t do_check(char *buffer, size_t try, unsigned current_block)
{
	ssize_t got;

	/* Seek to the correct loc. */
	msg_eol = "seek failed during testing of blocks";
	xlseek(dev_fd, current_block * BLOCK_SIZE, SEEK_SET);
	msg_eol = "\n";

	/* Try the read */
	got = read(dev_fd, buffer, try * BLOCK_SIZE);
	if (got < 0)
		got = 0;
	try = ((size_t)got) / BLOCK_SIZE;

	if (got & (BLOCK_SIZE - 1))
		fprintf(stderr, "Short read at block %u\n", (unsigned)(current_block + try));
	return try;
}

static void alarm_intr(int alnum UNUSED_PARAM)
{
	if (G.currently_testing >= SB_ZONES)
		return;
	signal(SIGALRM, alarm_intr);
	alarm(5);
	if (!G.currently_testing)
		return;
	printf("%d ...", G.currently_testing);
	fflush_all();
}

static void check_blocks(void)
{
	size_t try, got;

	G.currently_testing = 0;
	signal(SIGALRM, alarm_intr);
	alarm(5);
	while (G.currently_testing < SB_ZONES) {
		msg_eol = "seek failed in check_blocks";
		xlseek(dev_fd, G.currently_testing * BLOCK_SIZE, SEEK_SET);
		msg_eol = "\n";
		try = TEST_BUFFER_BLOCKS;
		if (G.currently_testing + try > SB_ZONES)
			try = SB_ZONES - G.currently_testing;
		got = do_check(G.check_blocks_buffer, try, G.currently_testing);
		G.currently_testing += got;
		if (got == try)
			continue;
		if (G.currently_testing < SB_FIRSTZONE)
			bb_error_msg_and_die("bad blocks before data-area: cannot make fs");
		mark_zone(G.currently_testing);
		G.badblocks++;
		G.currently_testing++;
	}
	alarm(0);
	printf("%d bad block(s)\n", G.badblocks);
}

static void get_list_blocks(char *filename)
{
	FILE *listfile;
	unsigned long blockno;

	listfile = xfopen_for_read(filename);
	while (!feof(listfile)) {
		fscanf(listfile, "%lu\n", &blockno);
		mark_zone(blockno);
		G.badblocks++;
	}
	printf("%d bad block(s)\n", G.badblocks);
}

static void setup_tables(void)
{
	unsigned long inodes;
	unsigned norm_firstzone;
	unsigned sb_zmaps;
	unsigned i;

	/* memset(G.u.superblock_buffer, 0, BLOCK_SIZE); */
	/* memset(G.boot_block_buffer, 0, 512); */
	SB_MAGIC = G.magic;
	SB_ZONE_SIZE = 0;
	SB_MAXSIZE = version2 ? 0x7fffffff : (7 + 512 + 512 * 512) * 1024;
	if (version2)
		SB.s_zones = G.total_blocks;
	else
		SB.s_nzones = G.total_blocks;

	/* some magic nrs: 1 inode / 3 blocks */
	if (G.req_nr_inodes == 0)
		inodes = G.total_blocks / 3;
	else
		inodes = G.req_nr_inodes;
	/* Round up inode count to fill block size */
	if (version2)
		inodes = (inodes + MINIX2_INODES_PER_BLOCK - 1) &
		                 ~(MINIX2_INODES_PER_BLOCK - 1);
	else
		inodes = (inodes + MINIX1_INODES_PER_BLOCK - 1) &
		                 ~(MINIX1_INODES_PER_BLOCK - 1);
	if (inodes > 65535)
		inodes = 65535;
	SB_INODES = inodes;
	SB_IMAPS = div_roundup(SB_INODES + 1, BITS_PER_BLOCK);

	/* Real bad hack but overwise mkfs.minix can be thrown
	 * in infinite loop...
	 * try:
	 * dd if=/dev/zero of=test.fs count=10 bs=1024
	 * mkfs.minix -i 200 test.fs
	 */
	/* This code is not insane: NORM_FIRSTZONE is not a constant,
	 * it is calculated from SB_INODES, SB_IMAPS and SB_ZMAPS */
	i = 999;
	SB_ZMAPS = 0;
	do {
		norm_firstzone = NORM_FIRSTZONE;
		sb_zmaps = div_roundup(G.total_blocks - norm_firstzone + 1, BITS_PER_BLOCK);
		if (SB_ZMAPS == sb_zmaps) goto got_it;
		SB_ZMAPS = sb_zmaps;
		/* new SB_ZMAPS, need to recalc NORM_FIRSTZONE */
	} while (--i);
	bb_error_msg_and_die("incompatible size/inode count, try different -i N");
 got_it:

	SB_FIRSTZONE = norm_firstzone;
	G.inode_map = xmalloc(SB_IMAPS * BLOCK_SIZE);
	G.zone_map = xmalloc(SB_ZMAPS * BLOCK_SIZE);
	memset(G.inode_map, 0xff, SB_IMAPS * BLOCK_SIZE);
	memset(G.zone_map, 0xff, SB_ZMAPS * BLOCK_SIZE);
	for (i = SB_FIRSTZONE; i < SB_ZONES; i++)
		unmark_zone(i);
	for (i = MINIX_ROOT_INO; i <= SB_INODES; i++)
		unmark_inode(i);
	G.inode_buffer = xzalloc(INODE_BUFFER_SIZE);
	printf("%lu inodes\n", (unsigned long)SB_INODES);
	printf("%lu blocks\n", (unsigned long)SB_ZONES);
	printf("Firstdatazone=%lu (%lu)\n", (unsigned long)SB_FIRSTZONE, (unsigned long)norm_firstzone);
	printf("Zonesize=%u\n", BLOCK_SIZE << SB_ZONE_SIZE);
	printf("Maxsize=%lu\n", (unsigned long)SB_MAXSIZE);
}

int mkfs_minix_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int mkfs_minix_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opt;
	char *tmp;
	char *str_i;
	char *listfile = NULL;

	INIT_G();
/* default (changed to 30, per Linus's suggestion, Sun Nov 21 08:05:07 1993) */
	G.namelen = 30;
	G.dirsize = 32;
	G.magic = MINIX1_SUPER_MAGIC2;

	if (INODE_SIZE1 * MINIX1_INODES_PER_BLOCK != BLOCK_SIZE)
		bb_error_msg_and_die("bad inode size");
#if ENABLE_FEATURE_MINIX2
	if (INODE_SIZE2 * MINIX2_INODES_PER_BLOCK != BLOCK_SIZE)
		bb_error_msg_and_die("bad inode size");
#endif

	opt = getopt32(argv, "ci:l:n:+v", &str_i, &listfile, &G.namelen);
	argv += optind;
	//if (opt & 1) -c
	if (opt & 2) G.req_nr_inodes = xatoul(str_i); // -i
	//if (opt & 4) -l
	if (opt & 8) { // -n
		if (G.namelen == 14) G.magic = MINIX1_SUPER_MAGIC;
		else if (G.namelen == 30) G.magic = MINIX1_SUPER_MAGIC2;
		else bb_show_usage();
		G.dirsize = G.namelen + 2;
	}
	if (opt & 0x10) { // -v
#if ENABLE_FEATURE_MINIX2
		version2 = 1;
#else
		bb_error_msg_and_die("not compiled with minix v2 support");
#endif
	}

	G.device_name = argv[0];
	if (!G.device_name)
		bb_show_usage();

	/* Check if it is mounted */
	if (find_mount_point(G.device_name, 0))
		bb_error_msg_and_die("can't format mounted filesystem");

	xmove_fd(xopen(G.device_name, O_RDWR), dev_fd);

	G.total_blocks = get_volume_size_in_bytes(dev_fd, argv[1], 1024, /*extend:*/ 1) / 1024;

	if (G.total_blocks < 10)
		bb_error_msg_and_die("must have at least 10 blocks");

	if (version2) {
		G.magic = MINIX2_SUPER_MAGIC2;
		if (G.namelen == 14)
			G.magic = MINIX2_SUPER_MAGIC;
	} else if (G.total_blocks > 65535)
		G.total_blocks = 65535;
#if 0
	struct stat statbuf;
	xfstat(dev_fd, &statbuf, G.device_name);
/* why? */
	if (!S_ISBLK(statbuf.st_mode))
		opt &= ~1; // clear -c (check)
#if 0
/* I don't know why someone has special code to prevent mkfs.minix
 * on IDE devices. Why IDE but not SCSI, etc?... */
	else if (statbuf.st_rdev == 0x0300 || statbuf.st_rdev == 0x0340)
		/* what is this? */
		bb_error_msg_and_die("will not try "
			"to make filesystem on '%s'", G.device_name);
#endif
#endif
	tmp = G.root_block;
	*(short *) tmp = 1;
	strcpy(tmp + 2, ".");
	tmp += G.dirsize;
	*(short *) tmp = 1;
	strcpy(tmp + 2, "..");
	tmp += G.dirsize;
	*(short *) tmp = 2;
	strcpy(tmp + 2, ".badblocks");

	setup_tables();

	if (opt & 1) // -c ?
		check_blocks();
	else if (listfile)
		get_list_blocks(listfile);

	if (version2) {
		make_root_inode2();
		make_bad_inode2();
	} else {
		make_root_inode();
		make_bad_inode();
	}

	mark_good_blocks();
	write_tables();
	return 0;
}
