/* vi: set sw=4 ts=4: */
/*
 * tune2fs: utility to modify EXT2 filesystem
 *
 * Busybox'ed (2009) by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config TUNE2FS
//config:	bool "tune2fs (4.4 kb)"
//config:	default n  # off: it is too limited compared to upstream version
//config:	help
//config:	tune2fs allows the system administrator to adjust various tunable
//config:	filesystem parameters on Linux ext2/ext3 filesystems.

//applet:IF_TUNE2FS(APPLET_NOEXEC(tune2fs, tune2fs, BB_DIR_SBIN, BB_SUID_DROP, tune2fs))

//TODO alias to "tune2fs -L LABEL": //applet:IF_E2LABEL(APPLET_ODDNAME(e2label, tune2fs, BB_DIR_SBIN, BB_SUID_DROP, e2label))

//kbuild:lib-$(CONFIG_TUNE2FS) += tune2fs.o

//usage:#define tune2fs_trivial_usage
//usage:       "[-c MAX_MOUNT_COUNT] "
////usage:     "[-e errors-behavior] [-g group] "
//usage:       "[-i DAYS] "
////usage:     "[-j] [-J journal-options] [-l] [-s sparse-flag] "
////usage:     "[-m reserved-blocks-percent] [-o [^]mount-options[,...]] "
////usage:     "[-r reserved-blocks-count] [-u user] "
//usage:       "[-C MOUNT_COUNT] "
//usage:       "[-L LABEL] "
////usage:     "[-M last-mounted-dir] [-O [^]feature[,...]] "
////usage:     "[-T last-check-time] [-U UUID] "
//usage:       "BLOCKDEV"
//usage:
//usage:#define tune2fs_full_usage "\n\n"
//usage:       "Adjust filesystem options on ext[23] filesystems"

#include "libbb.h"
#include <linux/fs.h>
#include "bb_e2fs_defs.h"

// storage helpers
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

enum {
	OPT_L = 1 << 0, // label
	OPT_c = 1 << 1, // max mount count
	OPT_i = 1 << 2, // check interval
	OPT_C = 1 << 3, // current mount count
};

int tune2fs_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int tune2fs_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opts;
	const char *label, *str_c, *str_i, *str_C;
	struct ext2_super_block *sb;
	int fd;

	opts = getopt32(argv, "^" "L:c:i:C:" "\0" "=1", &label, &str_c, &str_i, &str_C);
	if (!opts)
		bb_show_usage();
	argv += optind; // argv[0] -- device

	// read superblock
	fd = xopen(argv[0], O_RDWR);
	xlseek(fd, 1024, SEEK_SET);
	sb = xzalloc(1024);
	xread(fd, sb, 1024);

	// mangle superblock
	//STORE_LE(sb->s_wtime, time(NULL)); - why bother?

	if (opts & OPT_C) {
		int n = xatoi_range(str_C, 1, 0xfffe);
		STORE_LE(sb->s_mnt_count, (unsigned)n);
	}

	// set the label
	if (opts & OPT_L)
		safe_strncpy((char *)sb->s_volume_name, label, sizeof(sb->s_volume_name));

	if (opts & OPT_c) {
		int n = xatoi_range(str_c, -1, 0xfffe);
		if (n == 0)
			n = -1;
		STORE_LE(sb->s_max_mnt_count, (unsigned)n);
	}

	if (opts & OPT_i) {
		unsigned n = xatou_range(str_i, 0, (unsigned)0xffffffff / (24*60*60)) * 24*60*60;
		STORE_LE(sb->s_checkinterval, n);
	}

	// write superblock
	xlseek(fd, 1024, SEEK_SET);
	xwrite(fd, sb, 1024);

	if (ENABLE_FEATURE_CLEAN_UP) {
		free(sb);
	}

	xclose(fd);
	return EXIT_SUCCESS;
}
