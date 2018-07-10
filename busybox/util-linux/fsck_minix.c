/* vi: set sw=4 ts=4: */
/*
 * fsck.c - a file system consistency checker for Linux.
 *
 * (C) 1991, 1992 Linus Torvalds.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/*
 * 09.11.91  -  made the first rudimentary functions
 *
 * 10.11.91  -  updated, does checking, no repairs yet.
 *		Sent out to the mailing-list for testing.
 *
 * 14.11.91  -  Testing seems to have gone well. Added some
 *		correction-code, and changed some functions.
 *
 * 15.11.91  -  More correction code. Hopefully it notices most
 *		cases now, and tries to do something about them.
 *
 * 16.11.91  -  More corrections (thanks to Mika Jalava). Most
 *		things seem to work now. Yeah, sure.
 *
 * 19.04.92  -  Had to start over again from this old version, as a
 *		kernel bug ate my enhanced fsck in february.
 *
 * 28.02.93  -  added support for different directory entry sizes..
 *
 * Sat Mar  6 18:59:42 1993, faith@cs.unc.edu: Output namelen with
 *                           superblock information
 *
 * Sat Oct  9 11:17:11 1993, faith@cs.unc.edu: make exit status conform
 *                           to that required by fsutil
 *
 * Mon Jan  3 11:06:52 1994 - Dr. Wettstein (greg%wind.uucp@plains.nodak.edu)
 *                            Added support for file system valid flag.  Also
 *                            added program_version variable and output of
 *                            program name and version number when program
 *                            is executed.
 *
 * 30.10.94  - added support for v2 filesystem
 *             (Andreas Schwab, schwab@issan.informatik.uni-dortmund.de)
 *
 * 10.12.94  - added test to prevent checking of mounted fs adapted
 *             from Theodore Ts'o's (tytso@athena.mit.edu) e2fsck
 *             program.  (Daniel Quinlan, quinlan@yggdrasil.com)
 *
 * 01.07.96  - Fixed the v2 fs stuff to use the right #defines and such
 *             for modern libcs (janl@math.uio.no, Nicolai Langfeldt)
 *
 * 02.07.96  - Added C bit fiddling routines from rmk@ecs.soton.ac.uk
 *             (Russell King).  He made them for ARM.  It would seem
 *             that the ARM is powerful enough to do this in C whereas
 *             i386 and m64k must use assembly to get it fast >:-)
 *             This should make minix fsck system-independent.
 *             (janl@math.uio.no, Nicolai Langfeldt)
 *
 * 04.11.96  - Added minor fixes from Andreas Schwab to avoid compiler
 *             warnings.  Added mc68k bitops from
 *             Joerg Dorchain <dorchain@mpi-sb.mpg.de>.
 *
 * 06.11.96  - Added v2 code submitted by Joerg Dorchain, but written by
 *             Andreas Schwab.
 *
 * 1999-02-22 Arkadiusz Mickiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 *
 *
 * I've had no time to add comments - hopefully the function names
 * are comments enough. As with all file system checkers, this assumes
 * the file system is quiescent - don't use it on a mounted device
 * unless you can be sure nobody is writing to it (and remember that the
 * kernel can write to it when it searches for files).
 *
 * Usage: fsck [-larvsm] device
 *	-l for a listing of all the filenames
 *	-a for automatic repairs (not implemented)
 *	-r for repairs (interactive) (not implemented)
 *	-v for verbose (tells how many files)
 *	-s for superblock info
 *	-m for minix-like "mode not cleared" warnings
 *	-f force filesystem check even if filesystem marked as valid
 *
 * The device may be a block device or a image of one, but this isn't
 * enforced (but it's not much fun on a character device :-).
 */
//config:config FSCK_MINIX
//config:	bool "fsck_minix"
//config:	default y
//config:	help
//config:	The minix filesystem is a nice, small, compact, read-write filesystem
//config:	with little overhead. It is not a journaling filesystem however and
//config:	can experience corruption if it is not properly unmounted or if the
//config:	power goes off in the middle of a write. This utility allows you to
//config:	check for and attempt to repair any corruption that occurs to a minix
//config:	filesystem.

//applet:IF_FSCK_MINIX(APPLET_ODDNAME(fsck.minix, fsck_minix, BB_DIR_SBIN, BB_SUID_DROP, fsck_minix))

//kbuild:lib-$(CONFIG_FSCK_MINIX) += fsck_minix.o

//usage:#define fsck_minix_trivial_usage
//usage:       "[-larvsmf] BLOCKDEV"
//usage:#define fsck_minix_full_usage "\n\n"
//usage:       "Check MINIX filesystem\n"
//usage:     "\n	-l	List all filenames"
//usage:     "\n	-r	Perform interactive repairs"
//usage:     "\n	-a	Perform automatic repairs"
//usage:     "\n	-v	Verbose"
//usage:     "\n	-s	Output superblock information"
//usage:     "\n	-m	Show \"mode not cleared\" warnings"
//usage:     "\n	-f	Force file system check"

#include <mntent.h>
#include "libbb.h"
#include "minix.h"

#ifndef BLKGETSIZE
#define BLKGETSIZE _IO(0x12,96)    /* return device size */
#endif

struct BUG_bad_inode_size {
	char BUG_bad_inode1_size[(INODE_SIZE1 * MINIX1_INODES_PER_BLOCK != BLOCK_SIZE) ? -1 : 1];
#if ENABLE_FEATURE_MINIX2
	char BUG_bad_inode2_size[(INODE_SIZE2 * MINIX2_INODES_PER_BLOCK != BLOCK_SIZE) ? -1 : 1];
#endif
};

enum {
#ifdef UNUSED
	MINIX1_LINK_MAX = 250,
	MINIX2_LINK_MAX = 65530,
	MINIX_I_MAP_SLOTS = 8,
	MINIX_Z_MAP_SLOTS = 64,
	MINIX_V1 = 0x0001,      /* original minix fs */
	MINIX_V2 = 0x0002,      /* minix V2 fs */
#endif
	MINIX_NAME_MAX = 255,         /* # chars in a file name */
};


#if !ENABLE_FEATURE_MINIX2
enum { version2 = 0 };
#endif

enum { MAX_DEPTH = 32 };

enum { dev_fd = 3 };

struct globals {
#if ENABLE_FEATURE_MINIX2
	smallint version2;
#endif
	smallint changed;  /* is filesystem modified? */
	smallint errors_uncorrected;  /* flag if some error was not corrected */
	smallint termios_set;
	smallint dirsize;
	smallint namelen;
	const char *device_name;
	int directory, regular, blockdev, chardev, links, symlinks, total;
	char *inode_buffer;

	char *inode_map;
	char *zone_map;

	unsigned char *inode_count;
	unsigned char *zone_count;

	/* File-name data */
	int name_depth;
	char *name_component[MAX_DEPTH+1];

	/* Bigger stuff */
	struct termios sv_termios;
	union {
		char superblock_buffer[BLOCK_SIZE];
		struct minix_superblock Super;
	} u;
	char add_zone_ind_blk[BLOCK_SIZE];
	char add_zone_dind_blk[BLOCK_SIZE];
	IF_FEATURE_MINIX2(char add_zone_tind_blk[BLOCK_SIZE];)
	char check_file_blk[BLOCK_SIZE];

	/* File-name data */
	char current_name[MAX_DEPTH * MINIX_NAME_MAX];
};
#define G (*ptr_to_globals)
#if ENABLE_FEATURE_MINIX2
#define version2           (G.version2           )
#endif
#define changed            (G.changed            )
#define errors_uncorrected (G.errors_uncorrected )
#define termios_set        (G.termios_set        )
#define dirsize            (G.dirsize            )
#define namelen            (G.namelen            )
#define device_name        (G.device_name        )
#define directory          (G.directory          )
#define regular            (G.regular            )
#define blockdev           (G.blockdev           )
#define chardev            (G.chardev            )
#define links              (G.links              )
#define symlinks           (G.symlinks           )
#define total              (G.total              )
#define inode_buffer       (G.inode_buffer       )
#define inode_map          (G.inode_map          )
#define zone_map           (G.zone_map           )
#define inode_count        (G.inode_count        )
#define zone_count         (G.zone_count         )
#define name_depth         (G.name_depth         )
#define name_component     (G.name_component     )
#define sv_termios         (G.sv_termios         )
#define superblock_buffer  (G.u.superblock_buffer)
#define add_zone_ind_blk   (G.add_zone_ind_blk   )
#define add_zone_dind_blk  (G.add_zone_dind_blk  )
#define add_zone_tind_blk  (G.add_zone_tind_blk  )
#define check_file_blk     (G.check_file_blk     )
#define current_name       (G.current_name       )
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
	dirsize = 16; \
	namelen = 14; \
	current_name[0] = '/'; \
	/*current_name[1] = '\0';*/ \
	name_component[0] = &current_name[0]; \
} while (0)


#define OPTION_STR "larvsmf"
enum {
	OPT_l = (1 << 0),
	OPT_a = (1 << 1),
	OPT_r = (1 << 2),
	OPT_v = (1 << 3),
	OPT_s = (1 << 4),
	OPT_w = (1 << 5),
	OPT_f = (1 << 6),
};
#define OPT_list      (option_mask32 & OPT_l)
#define OPT_automatic (option_mask32 & OPT_a)
#define OPT_repair    (option_mask32 & OPT_r)
#define OPT_verbose   (option_mask32 & OPT_v)
#define OPT_show      (option_mask32 & OPT_s)
#define OPT_warn_mode (option_mask32 & OPT_w)
#define OPT_force     (option_mask32 & OPT_f)
/* non-automatic repairs requested? */
#define OPT_manual    ((option_mask32 & (OPT_a|OPT_r)) == OPT_r)


#define Inode1 (((struct minix1_inode *) inode_buffer)-1)
#define Inode2 (((struct minix2_inode *) inode_buffer)-1)

#define Super (G.u.Super)

#if ENABLE_FEATURE_MINIX2
# define ZONES    ((unsigned)(version2 ? Super.s_zones : Super.s_nzones))
#else
# define ZONES    ((unsigned)(Super.s_nzones))
#endif
#define INODES    ((unsigned)Super.s_ninodes)
#define IMAPS     ((unsigned)Super.s_imap_blocks)
#define ZMAPS     ((unsigned)Super.s_zmap_blocks)
#define FIRSTZONE ((unsigned)Super.s_firstdatazone)
#define ZONESIZE  ((unsigned)Super.s_log_zone_size)
#define MAXSIZE   ((unsigned)Super.s_max_size)
#define MAGIC     (Super.s_magic)

/* gcc likes this more (code is smaller) than macro variant */
static ALWAYS_INLINE unsigned div_roundup(unsigned size, unsigned n)
{
	return (size + n-1) / n;
}

#if !ENABLE_FEATURE_MINIX2
#define INODE_BLOCKS            div_roundup(INODES, MINIX1_INODES_PER_BLOCK)
#else
#define INODE_BLOCKS            div_roundup(INODES, \
                                (version2 ? MINIX2_INODES_PER_BLOCK : MINIX1_INODES_PER_BLOCK))
#endif

#define INODE_BUFFER_SIZE       (INODE_BLOCKS * BLOCK_SIZE)
#define NORM_FIRSTZONE          (2 + IMAPS + ZMAPS + INODE_BLOCKS)

/* Before you ask "where they come from?": */
/* setbit/clrbit are supplied by sys/param.h */

static int minix_bit(const char *a, unsigned i)
{
	return (a[i >> 3] & (1<<(i & 7)));
}

static void minix_setbit(char *a, unsigned i)
{
	setbit(a, i);
	changed = 1;
}
static void minix_clrbit(char *a, unsigned i)
{
	clrbit(a, i);
	changed = 1;
}

/* Note: do not assume 0/1, it is 0/nonzero */
#define zone_in_use(x)  (minix_bit(zone_map,(x)-FIRSTZONE+1))
#define inode_in_use(x) (minix_bit(inode_map,(x)))

#define mark_inode(x)   (minix_setbit(inode_map,(x)))
#define unmark_inode(x) (minix_clrbit(inode_map,(x)))

#define mark_zone(x)    (minix_setbit(zone_map,(x)-FIRSTZONE+1))
#define unmark_zone(x)  (minix_clrbit(zone_map,(x)-FIRSTZONE+1))


static void recursive_check(unsigned ino);
#if ENABLE_FEATURE_MINIX2
static void recursive_check2(unsigned ino);
#endif

static void die(const char *str) NORETURN;
static void die(const char *str)
{
	if (termios_set)
		tcsetattr_stdin_TCSANOW(&sv_termios);
	bb_error_msg_and_die("%s", str);
}

static void push_filename(const char *name)
{
	//  /dir/dir/dir/file
	//  ^   ^   ^
	// [0] [1] [2] <-name_component[i]
	if (name_depth < MAX_DEPTH) {
		int len;
		char *p = name_component[name_depth];
		*p++ = '/';
		len = sprintf(p, "%.*s", namelen, name);
		name_component[name_depth + 1] = p + len;
	}
	name_depth++;
}

static void pop_filename(void)
{
	name_depth--;
	if (name_depth < MAX_DEPTH) {
		*name_component[name_depth] = '\0';
		if (!name_depth) {
			current_name[0] = '/';
			current_name[1] = '\0';
		}
	}
}

static int ask(const char *string, int def)
{
	int c;

	if (!OPT_repair) {
		bb_putchar('\n');
		errors_uncorrected = 1;
		return 0;
	}
	if (OPT_automatic) {
		bb_putchar('\n');
		if (!def)
			errors_uncorrected = 1;
		return def;
	}
	printf(def ? "%s (y/n)? " : "%s (n/y)? ", string);
	for (;;) {
		fflush_all();
		c = getchar();
		if (c == EOF) {
			if (!def)
				errors_uncorrected = 1;
			return def;
		}
		if (c == '\n')
			break;
		c |= 0x20; /* tolower */
		if (c == 'y') {
			def = 1;
			break;
		}
		if (c == 'n') {
			def = 0;
			break;
		}
	}
	if (def)
		puts("y");
	else {
		puts("n");
		errors_uncorrected = 1;
	}
	return def;
}

/*
 * Make certain that we aren't checking a filesystem that is on a
 * mounted partition.  Code adapted from e2fsck, Copyright (C) 1993,
 * 1994 Theodore Ts'o.  Also licensed under GPL.
 */
static void check_mount(void)
{
	if (find_mount_point(device_name, 0)) {
		int cont;
#if ENABLE_FEATURE_MTAB_SUPPORT
		/*
		 * If the root is mounted read-only, then /etc/mtab is
		 * probably not correct; so we won't issue a warning based on
		 * it.
		 */
		int fd = open(bb_path_mtab_file, O_RDWR);

		if (fd < 0 && errno == EROFS)
			return;
		close(fd);
#endif
		printf("%s is mounted. ", device_name);
		cont = 0;
		if (isatty(0) && isatty(1))
			cont = ask("Do you really want to continue", 0);
		if (!cont) {
			puts("Check aborted");
			exit(EXIT_SUCCESS);
		}
	}
}

/*
 * check_zone_nr checks to see that *nr is a valid zone nr. If it
 * isn't, it will possibly be repaired. Check_zone_nr sets *corrected
 * if an error was corrected, and returns the zone (0 for no zone
 * or a bad zone-number).
 */
static int check_zone_nr2(uint32_t *nr, smallint *corrected)
{
	const char *msg;
	if (!*nr)
		return 0;
	if (*nr < FIRSTZONE)
		msg = "< FIRSTZONE";
	else if (*nr >= ZONES)
		msg = ">= ZONES";
	else
		return *nr;
	printf("Zone nr %s in file '%s'. ", msg, current_name);
	if (ask("Remove block", 1)) {
		*nr = 0;
		*corrected = 1;
	}
	return 0;
}

static int check_zone_nr(uint16_t *nr, smallint *corrected)
{
	uint32_t nr32 = *nr;
	int r = check_zone_nr2(&nr32, corrected);
	*nr = (uint16_t)nr32;
	return r;
}

/*
 * read-block reads block nr into the buffer at addr.
 */
static void read_block(unsigned nr, void *addr)
{
	if (!nr) {
		memset(addr, 0, BLOCK_SIZE);
		return;
	}
	xlseek(dev_fd, BLOCK_SIZE * nr, SEEK_SET);
	if (BLOCK_SIZE != full_read(dev_fd, addr, BLOCK_SIZE)) {
		printf("%s: bad block %u in file '%s'\n",
				bb_msg_read_error, nr, current_name);
		errors_uncorrected = 1;
		memset(addr, 0, BLOCK_SIZE);
	}
}

/*
 * write_block writes block nr to disk.
 */
static void write_block(unsigned nr, void *addr)
{
	if (!nr)
		return;
	if (nr < FIRSTZONE || nr >= ZONES) {
		puts("Internal error: trying to write bad block\n"
			"Write request ignored");
		errors_uncorrected = 1;
		return;
	}
	xlseek(dev_fd, BLOCK_SIZE * nr, SEEK_SET);
	if (BLOCK_SIZE != full_write(dev_fd, addr, BLOCK_SIZE)) {
		printf("%s: bad block %u in file '%s'\n",
				bb_msg_write_error, nr, current_name);
		errors_uncorrected = 1;
	}
}

/*
 * map_block calculates the absolute block nr of a block in a file.
 * It sets 'changed' if the inode has needed changing, and re-writes
 * any indirect blocks with errors.
 */
static int map_block(struct minix1_inode *inode, unsigned blknr)
{
	uint16_t ind[BLOCK_SIZE >> 1];
	int block, result;
	smallint blk_chg;

	if (blknr < 7)
		return check_zone_nr(inode->i_zone + blknr, &changed);
	blknr -= 7;
	if (blknr < 512) {
		block = check_zone_nr(inode->i_zone + 7, &changed);
		goto common;
	}
	blknr -= 512;
	block = check_zone_nr(inode->i_zone + 8, &changed);
	read_block(block, ind); /* double indirect */
	blk_chg = 0;
	result = check_zone_nr(&ind[blknr / 512], &blk_chg);
	if (blk_chg)
		write_block(block, ind);
	block = result;
 common:
	read_block(block, ind);
	blk_chg = 0;
	result = check_zone_nr(&ind[blknr % 512], &blk_chg);
	if (blk_chg)
		write_block(block, ind);
	return result;
}

#if ENABLE_FEATURE_MINIX2
static int map_block2(struct minix2_inode *inode, unsigned blknr)
{
	uint32_t ind[BLOCK_SIZE >> 2];
	int block, result;
	smallint blk_chg;

	if (blknr < 7)
		return check_zone_nr2(inode->i_zone + blknr, &changed);
	blknr -= 7;
	if (blknr < 256) {
		block = check_zone_nr2(inode->i_zone + 7, &changed);
		goto common2;
	}
	blknr -= 256;
	if (blknr < 256 * 256) {
		block = check_zone_nr2(inode->i_zone + 8, &changed);
		goto common1;
	}
	blknr -= 256 * 256;
	block = check_zone_nr2(inode->i_zone + 9, &changed);
	read_block(block, ind); /* triple indirect */
	blk_chg = 0;
	result = check_zone_nr2(&ind[blknr / (256 * 256)], &blk_chg);
	if (blk_chg)
		write_block(block, ind);
	block = result;
 common1:
	read_block(block, ind); /* double indirect */
	blk_chg = 0;
	result = check_zone_nr2(&ind[(blknr / 256) % 256], &blk_chg);
	if (blk_chg)
		write_block(block, ind);
	block = result;
 common2:
	read_block(block, ind);
	blk_chg = 0;
	result = check_zone_nr2(&ind[blknr % 256], &blk_chg);
	if (blk_chg)
		write_block(block, ind);
	return result;
}
#endif

static void write_superblock(void)
{
	/*
	 * Set the state of the filesystem based on whether or not there
	 * are uncorrected errors.  The filesystem valid flag is
	 * unconditionally set if we get this far.
	 */
	Super.s_state |= MINIX_VALID_FS | MINIX_ERROR_FS;
	if (!errors_uncorrected)
		Super.s_state &= ~MINIX_ERROR_FS;

	xlseek(dev_fd, BLOCK_SIZE, SEEK_SET);
	if (BLOCK_SIZE != full_write(dev_fd, superblock_buffer, BLOCK_SIZE))
		die("can't write superblock");
}

static void write_tables(void)
{
	write_superblock();

	if (IMAPS * BLOCK_SIZE != write(dev_fd, inode_map, IMAPS * BLOCK_SIZE))
		die("can't write inode map");
	if (ZMAPS * BLOCK_SIZE != write(dev_fd, zone_map, ZMAPS * BLOCK_SIZE))
		die("can't write zone map");
	if (INODE_BUFFER_SIZE != write(dev_fd, inode_buffer, INODE_BUFFER_SIZE))
		die("can't write inodes");
}

static void get_dirsize(void)
{
	int block;
	char blk[BLOCK_SIZE];
	int size;

#if ENABLE_FEATURE_MINIX2
	if (version2)
		block = Inode2[MINIX_ROOT_INO].i_zone[0];
	else
#endif
		block = Inode1[MINIX_ROOT_INO].i_zone[0];
	read_block(block, blk);
	for (size = 16; size < BLOCK_SIZE; size <<= 1) {
		if (strcmp(blk + size + 2, "..") == 0) {
			dirsize = size;
			namelen = size - 2;
			return;
		}
	}
	/* use defaults */
}

static void read_superblock(void)
{
	xlseek(dev_fd, BLOCK_SIZE, SEEK_SET);
	if (BLOCK_SIZE != full_read(dev_fd, superblock_buffer, BLOCK_SIZE))
		die("can't read superblock");
	/* already initialized to:
	namelen = 14;
	dirsize = 16;
	version2 = 0;
	*/
	if (MAGIC == MINIX1_SUPER_MAGIC) {
	} else if (MAGIC == MINIX1_SUPER_MAGIC2) {
		namelen = 30;
		dirsize = 32;
#if ENABLE_FEATURE_MINIX2
	} else if (MAGIC == MINIX2_SUPER_MAGIC) {
		version2 = 1;
	} else if (MAGIC == MINIX2_SUPER_MAGIC2) {
		namelen = 30;
		dirsize = 32;
		version2 = 1;
#endif
	} else
		die("bad magic number in superblock");
	if (ZONESIZE != 0 || BLOCK_SIZE != 1024)
		die("only 1k blocks/zones supported");
	if (IMAPS * BLOCK_SIZE * 8 < INODES + 1)
		die("bad s_imap_blocks field in superblock");
	if (ZMAPS * BLOCK_SIZE * 8 < ZONES - FIRSTZONE + 1)
		die("bad s_zmap_blocks field in superblock");
}

static void read_tables(void)
{
	inode_map = xzalloc(IMAPS * BLOCK_SIZE);
	zone_map = xzalloc(ZMAPS * BLOCK_SIZE);
	inode_buffer = xmalloc(INODE_BUFFER_SIZE);
	inode_count = xmalloc(INODES + 1);
	zone_count = xmalloc(ZONES);
	if (IMAPS * BLOCK_SIZE != read(dev_fd, inode_map, IMAPS * BLOCK_SIZE))
		die("can't read inode map");
	if (ZMAPS * BLOCK_SIZE != read(dev_fd, zone_map, ZMAPS * BLOCK_SIZE))
		die("can't read zone map");
	if (INODE_BUFFER_SIZE != read(dev_fd, inode_buffer, INODE_BUFFER_SIZE))
		die("can't read inodes");
	if (NORM_FIRSTZONE != FIRSTZONE) {
		puts("warning: firstzone!=norm_firstzone");
		errors_uncorrected = 1;
	}
	get_dirsize();
	if (OPT_show) {
		printf("%u inodes\n"
			"%u blocks\n"
			"Firstdatazone=%u (%u)\n"
			"Zonesize=%u\n"
			"Maxsize=%u\n"
			"Filesystem state=%u\n"
			"namelen=%u\n\n",
			INODES,
			ZONES,
			FIRSTZONE, NORM_FIRSTZONE,
			BLOCK_SIZE << ZONESIZE,
			MAXSIZE,
			Super.s_state,
			namelen);
	}
}

static void get_inode_common(unsigned nr, uint16_t i_mode)
{
	total++;
	if (!inode_count[nr]) {
		if (!inode_in_use(nr)) {
			printf("Inode %u is marked as 'unused', but it is used "
					"for file '%s'\n", nr, current_name);
			if (OPT_repair) {
				if (ask("Mark as 'in use'", 1))
					mark_inode(nr);
				else
					errors_uncorrected = 1;
			}
		}
		if (S_ISDIR(i_mode))
			directory++;
		else if (S_ISREG(i_mode))
			regular++;
		else if (S_ISCHR(i_mode))
			chardev++;
		else if (S_ISBLK(i_mode))
			blockdev++;
		else if (S_ISLNK(i_mode))
			symlinks++;
		else if (S_ISSOCK(i_mode));
		else if (S_ISFIFO(i_mode));
		else {
			printf("%s has mode %05o\n", current_name, i_mode);
		}
	} else
		links++;
	if (!++inode_count[nr]) {
		puts("Warning: inode count too big");
		inode_count[nr]--;
		errors_uncorrected = 1;
	}
}

static struct minix1_inode *get_inode(unsigned nr)
{
	struct minix1_inode *inode;

	if (!nr || nr > INODES)
		return NULL;
	inode = Inode1 + nr;
	get_inode_common(nr, inode->i_mode);
	return inode;
}

#if ENABLE_FEATURE_MINIX2
static struct minix2_inode *get_inode2(unsigned nr)
{
	struct minix2_inode *inode;

	if (!nr || nr > INODES)
		return NULL;
	inode = Inode2 + nr;
	get_inode_common(nr, inode->i_mode);
	return inode;
}
#endif

static void check_root(void)
{
	struct minix1_inode *inode = Inode1 + MINIX_ROOT_INO;

	if (!inode || !S_ISDIR(inode->i_mode))
		die("root inode isn't a directory");
}

#if ENABLE_FEATURE_MINIX2
static void check_root2(void)
{
	struct minix2_inode *inode = Inode2 + MINIX_ROOT_INO;

	if (!inode || !S_ISDIR(inode->i_mode))
		die("root inode isn't a directory");
}
#else
void check_root2(void);
#endif

static int add_zone_common(int block, smallint *corrected)
{
	if (!block)
		return 0;
	if (zone_count[block]) {
		printf("Already used block is reused in file '%s'. ",
				current_name);
		if (ask("Clear", 1)) {
			block = 0;
			*corrected = 1;
			return -1; /* "please zero out *znr" */
		}
	}
	if (!zone_in_use(block)) {
		printf("Block %d in file '%s' is marked as 'unused'. ",
				block, current_name);
		if (ask("Correct", 1))
			mark_zone(block);
	}
	if (!++zone_count[block])
		zone_count[block]--;
	return block;
}

static int add_zone(uint16_t *znr, smallint *corrected)
{
	int block;

	block = check_zone_nr(znr, corrected);
	block = add_zone_common(block, corrected);
	if (block == -1) {
		*znr = 0;
		block = 0;
	}
	return block;
}

#if ENABLE_FEATURE_MINIX2
static int add_zone2(uint32_t *znr, smallint *corrected)
{
	int block;

	block = check_zone_nr2(znr, corrected);
	block = add_zone_common(block, corrected);
	if (block == -1) {
		*znr = 0;
		block = 0;
	}
	return block;
}
#endif

static void add_zone_ind(uint16_t *znr, smallint *corrected)
{
	int i;
	int block;
	smallint chg_blk = 0;

	block = add_zone(znr, corrected);
	if (!block)
		return;
	read_block(block, add_zone_ind_blk);
	for (i = 0; i < (BLOCK_SIZE >> 1); i++)
		add_zone(i + (uint16_t *) add_zone_ind_blk, &chg_blk);
	if (chg_blk)
		write_block(block, add_zone_ind_blk);
}

#if ENABLE_FEATURE_MINIX2
static void add_zone_ind2(uint32_t *znr, smallint *corrected)
{
	int i;
	int block;
	smallint chg_blk = 0;

	block = add_zone2(znr, corrected);
	if (!block)
		return;
	read_block(block, add_zone_ind_blk);
	for (i = 0; i < BLOCK_SIZE >> 2; i++)
		add_zone2(i + (uint32_t *) add_zone_ind_blk, &chg_blk);
	if (chg_blk)
		write_block(block, add_zone_ind_blk);
}
#endif

static void add_zone_dind(uint16_t *znr, smallint *corrected)
{
	int i;
	int block;
	smallint chg_blk = 0;

	block = add_zone(znr, corrected);
	if (!block)
		return;
	read_block(block, add_zone_dind_blk);
	for (i = 0; i < (BLOCK_SIZE >> 1); i++)
		add_zone_ind(i + (uint16_t *) add_zone_dind_blk, &chg_blk);
	if (chg_blk)
		write_block(block, add_zone_dind_blk);
}

#if ENABLE_FEATURE_MINIX2
static void add_zone_dind2(uint32_t *znr, smallint *corrected)
{
	int i;
	int block;
	smallint chg_blk = 0;

	block = add_zone2(znr, corrected);
	if (!block)
		return;
	read_block(block, add_zone_dind_blk);
	for (i = 0; i < BLOCK_SIZE >> 2; i++)
		add_zone_ind2(i + (uint32_t *) add_zone_dind_blk, &chg_blk);
	if (chg_blk)
		write_block(block, add_zone_dind_blk);
}

static void add_zone_tind2(uint32_t *znr, smallint *corrected)
{
	int i;
	int block;
	smallint chg_blk = 0;

	block = add_zone2(znr, corrected);
	if (!block)
		return;
	read_block(block, add_zone_tind_blk);
	for (i = 0; i < BLOCK_SIZE >> 2; i++)
		add_zone_dind2(i + (uint32_t *) add_zone_tind_blk, &chg_blk);
	if (chg_blk)
		write_block(block, add_zone_tind_blk);
}
#endif

static void check_zones(unsigned i)
{
	struct minix1_inode *inode;

	if (!i || i > INODES)
		return;
	if (inode_count[i] > 1)		/* have we counted this file already? */
		return;
	inode = Inode1 + i;
	if (!S_ISDIR(inode->i_mode)
	 && !S_ISREG(inode->i_mode)
	 && !S_ISLNK(inode->i_mode)
	) {
		return;
	}
	for (i = 0; i < 7; i++)
		add_zone(i + inode->i_zone, &changed);
	add_zone_ind(7 + inode->i_zone, &changed);
	add_zone_dind(8 + inode->i_zone, &changed);
}

#if ENABLE_FEATURE_MINIX2
static void check_zones2(unsigned i)
{
	struct minix2_inode *inode;

	if (!i || i > INODES)
		return;
	if (inode_count[i] > 1)		/* have we counted this file already? */
		return;
	inode = Inode2 + i;
	if (!S_ISDIR(inode->i_mode) && !S_ISREG(inode->i_mode)
		&& !S_ISLNK(inode->i_mode))
		return;
	for (i = 0; i < 7; i++)
		add_zone2(i + inode->i_zone, &changed);
	add_zone_ind2(7 + inode->i_zone, &changed);
	add_zone_dind2(8 + inode->i_zone, &changed);
	add_zone_tind2(9 + inode->i_zone, &changed);
}
#endif

static void check_file(struct minix1_inode *dir, unsigned offset)
{
	struct minix1_inode *inode;
	int ino;
	char *name;
	int block;

	block = map_block(dir, offset / BLOCK_SIZE);
	read_block(block, check_file_blk);
	name = check_file_blk + (offset % BLOCK_SIZE) + 2;
	ino = *(uint16_t *) (name - 2);
	if (ino > INODES) {
		printf("%s contains a bad inode number for file '%.*s'. ",
				current_name, namelen, name);
		if (ask("Remove", 1)) {
			*(uint16_t *) (name - 2) = 0;
			write_block(block, check_file_blk);
		}
		ino = 0;
	}
	push_filename(name);
	inode = get_inode(ino);
	pop_filename();
	if (!offset) {
		if (inode && LONE_CHAR(name, '.'))
			return;
		printf("%s: bad directory: '.' isn't first\n", current_name);
		errors_uncorrected = 1;
	}
	if (offset == dirsize) {
		if (inode && strcmp("..", name) == 0)
			return;
		printf("%s: bad directory: '..' isn't second\n", current_name);
		errors_uncorrected = 1;
	}
	if (!inode)
		return;
	push_filename(name);
	if (OPT_list) {
		if (OPT_verbose)
			printf("%6d %07o %3d ", ino, inode->i_mode, inode->i_nlinks);
		printf("%s%s\n", current_name, S_ISDIR(inode->i_mode) ? ":" : "");
	}
	check_zones(ino);
	if (inode && S_ISDIR(inode->i_mode))
		recursive_check(ino);
	pop_filename();
}

#if ENABLE_FEATURE_MINIX2
static void check_file2(struct minix2_inode *dir, unsigned offset)
{
	struct minix2_inode *inode;
	int ino;
	char *name;
	int block;

	block = map_block2(dir, offset / BLOCK_SIZE);
	read_block(block, check_file_blk);
	name = check_file_blk + (offset % BLOCK_SIZE) + 2;
	ino = *(uint16_t *) (name - 2);
	if (ino > INODES) {
		printf("%s contains a bad inode number for file '%.*s'. ",
				current_name, namelen, name);
		if (ask("Remove", 1)) {
			*(uint16_t *) (name - 2) = 0;
			write_block(block, check_file_blk);
		}
		ino = 0;
	}
	push_filename(name);
	inode = get_inode2(ino);
	pop_filename();
	if (!offset) {
		if (inode && LONE_CHAR(name, '.'))
			return;
		printf("%s: bad directory: '.' isn't first\n", current_name);
		errors_uncorrected = 1;
	}
	if (offset == dirsize) {
		if (inode && strcmp("..", name) == 0)
			return;
		printf("%s: bad directory: '..' isn't second\n", current_name);
		errors_uncorrected = 1;
	}
	if (!inode)
		return;
	push_filename(name);
	if (OPT_list) {
		if (OPT_verbose)
			printf("%6d %07o %3d ", ino, inode->i_mode, inode->i_nlinks);
		printf("%s%s\n", current_name, S_ISDIR(inode->i_mode) ? ":" : "");
	}
	check_zones2(ino);
	if (inode && S_ISDIR(inode->i_mode))
		recursive_check2(ino);
	pop_filename();
}
#endif

static void recursive_check(unsigned ino)
{
	struct minix1_inode *dir;
	unsigned offset;

	dir = Inode1 + ino;
	if (!S_ISDIR(dir->i_mode))
		die("internal error");
	if (dir->i_size < 2 * dirsize) {
		printf("%s: bad directory: size<32", current_name);
		errors_uncorrected = 1;
	}
	for (offset = 0; offset < dir->i_size; offset += dirsize)
		check_file(dir, offset);
}

#if ENABLE_FEATURE_MINIX2
static void recursive_check2(unsigned ino)
{
	struct minix2_inode *dir;
	unsigned offset;

	dir = Inode2 + ino;
	if (!S_ISDIR(dir->i_mode))
		die("internal error");
	if (dir->i_size < 2 * dirsize) {
		printf("%s: bad directory: size<32", current_name);
		errors_uncorrected = 1;
	}
	for (offset = 0; offset < dir->i_size; offset += dirsize)
		check_file2(dir, offset);
}
#endif

static int bad_zone(int i)
{
	char buffer[BLOCK_SIZE];

	xlseek(dev_fd, BLOCK_SIZE * i, SEEK_SET);
	return (BLOCK_SIZE != full_read(dev_fd, buffer, BLOCK_SIZE));
}

static void check_counts(void)
{
	int i;

	for (i = 1; i <= INODES; i++) {
		if (OPT_warn_mode && Inode1[i].i_mode && !inode_in_use(i)) {
			printf("Inode %d has non-zero mode. ", i);
			if (ask("Clear", 1)) {
				Inode1[i].i_mode = 0;
				changed = 1;
			}
		}
		if (!inode_count[i]) {
			if (!inode_in_use(i))
				continue;
			printf("Unused inode %d is marked as 'used' in the bitmap. ", i);
			if (ask("Clear", 1))
				unmark_inode(i);
			continue;
		}
		if (!inode_in_use(i)) {
			printf("Inode %d is used, but marked as 'unused' in the bitmap. ", i);
			if (ask("Set", 1))
				mark_inode(i);
		}
		if (Inode1[i].i_nlinks != inode_count[i]) {
			printf("Inode %d (mode=%07o), i_nlinks=%d, counted=%d. ",
				i, Inode1[i].i_mode, Inode1[i].i_nlinks,
				inode_count[i]);
			if (ask("Set i_nlinks to count", 1)) {
				Inode1[i].i_nlinks = inode_count[i];
				changed = 1;
			}
		}
	}
	for (i = FIRSTZONE; i < ZONES; i++) {
		if ((zone_in_use(i) != 0) == zone_count[i])
			continue;
		if (!zone_count[i]) {
			if (bad_zone(i))
				continue;
			printf("Zone %d is marked 'in use', but no file uses it. ", i);
			if (ask("Unmark", 1))
				unmark_zone(i);
			continue;
		}
		printf("Zone %d: %sin use, counted=%d\n",
			i, zone_in_use(i) ? "" : "not ", zone_count[i]);
	}
}

#if ENABLE_FEATURE_MINIX2
static void check_counts2(void)
{
	int i;

	for (i = 1; i <= INODES; i++) {
		if (OPT_warn_mode && Inode2[i].i_mode && !inode_in_use(i)) {
			printf("Inode %d has non-zero mode. ", i);
			if (ask("Clear", 1)) {
				Inode2[i].i_mode = 0;
				changed = 1;
			}
		}
		if (!inode_count[i]) {
			if (!inode_in_use(i))
				continue;
			printf("Unused inode %d is marked as 'used' in the bitmap. ", i);
			if (ask("Clear", 1))
				unmark_inode(i);
			continue;
		}
		if (!inode_in_use(i)) {
			printf("Inode %d is used, but marked as 'unused' in the bitmap. ", i);
			if (ask("Set", 1))
				mark_inode(i);
		}
		if (Inode2[i].i_nlinks != inode_count[i]) {
			printf("Inode %d (mode=%07o), i_nlinks=%d, counted=%d. ",
				i, Inode2[i].i_mode, Inode2[i].i_nlinks,
				inode_count[i]);
			if (ask("Set i_nlinks to count", 1)) {
				Inode2[i].i_nlinks = inode_count[i];
				changed = 1;
			}
		}
	}
	for (i = FIRSTZONE; i < ZONES; i++) {
		if ((zone_in_use(i) != 0) == zone_count[i])
			continue;
		if (!zone_count[i]) {
			if (bad_zone(i))
				continue;
			printf("Zone %d is marked 'in use', but no file uses it. ", i);
			if (ask("Unmark", 1))
				unmark_zone(i);
			continue;
		}
		printf("Zone %d: %sin use, counted=%d\n",
			i, zone_in_use(i) ? "" : "not ", zone_count[i]);
	}
}
#endif

static void check(void)
{
	memset(inode_count, 0, (INODES + 1) * sizeof(*inode_count));
	memset(zone_count, 0, ZONES * sizeof(*zone_count));
	check_zones(MINIX_ROOT_INO);
	recursive_check(MINIX_ROOT_INO);
	check_counts();
}

#if ENABLE_FEATURE_MINIX2
static void check2(void)
{
	memset(inode_count, 0, (INODES + 1) * sizeof(*inode_count));
	memset(zone_count, 0, ZONES * sizeof(*zone_count));
	check_zones2(MINIX_ROOT_INO);
	recursive_check2(MINIX_ROOT_INO);
	check_counts2();
}
#else
void check2(void);
#endif

int fsck_minix_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int fsck_minix_main(int argc UNUSED_PARAM, char **argv)
{
	int retcode = 0;

	xfunc_error_retval = 8;

	INIT_G();

	getopt32(argv, "^" OPTION_STR "\0" "=1:ar" /* one arg; -a assumes -r */);
	argv += optind;
	device_name = argv[0];

	check_mount();  /* trying to check a mounted filesystem? */
	if (OPT_manual) {
		if (!isatty(0) || !isatty(1))
			die("need terminal for interactive repairs");
	}
	xmove_fd(xopen(device_name, OPT_repair ? O_RDWR : O_RDONLY), dev_fd);

	/*sync(); paranoia? */
	read_superblock();

	/*
	 * Determine whether or not we should continue with the checking.
	 * This is based on the status of the filesystem valid and error
	 * flags and whether or not the -f switch was specified on the
	 * command line.
	 */
	printf("%s: %s\n", applet_name, bb_banner);

	if (!(Super.s_state & MINIX_ERROR_FS)
	 && (Super.s_state & MINIX_VALID_FS) && !OPT_force
	) {
		if (OPT_repair)
			printf("%s is clean, check is skipped\n", device_name);
		return 0;
	} else if (OPT_force)
		printf("Forcing filesystem check on %s\n", device_name);
	else if (OPT_repair)
		printf("Filesystem on %s is dirty, needs checking\n",
			device_name);

	read_tables();

	if (OPT_manual) {
		set_termios_to_raw(STDIN_FILENO, &sv_termios, 0);
		termios_set = 1;
	}

	if (version2) {
		check_root2();
		check2();
	} else {
		check_root();
		check();
	}

	if (OPT_verbose) {
		int i, free_cnt;

		for (i = 1, free_cnt = 0; i <= INODES; i++)
			if (!inode_in_use(i))
				free_cnt++;
		printf("\n%6u inodes used (%u%%)\n", (INODES - free_cnt),
			100 * (INODES - free_cnt) / INODES);
		for (i = FIRSTZONE, free_cnt = 0; i < ZONES; i++)
			if (!zone_in_use(i))
				free_cnt++;
		printf("%6u zones used (%u%%)\n\n"
			"%6u regular files\n"
			"%6u directories\n"
			"%6u character device files\n"
			"%6u block device files\n"
			"%6u links\n"
			"%6u symbolic links\n"
			"------\n"
			"%6u files\n",
			(ZONES - free_cnt), 100 * (ZONES - free_cnt) / ZONES,
			regular, directory, chardev, blockdev,
			links - 2 * directory + 1, symlinks,
			total - 2 * directory + 1);
	}
	if (changed) {
		write_tables();
		puts("FILE SYSTEM HAS BEEN CHANGED");
		sync();
	} else if (OPT_repair)
		write_superblock();

	if (OPT_manual)
		tcsetattr_stdin_TCSANOW(&sv_termios);

	if (changed)
		retcode += 3;
	if (errors_uncorrected)
		retcode += 4;
	return retcode;
}
