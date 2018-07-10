/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2017 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config SHRED
//config:	bool "shred (5 kb)"
//config:	default y
//config:	help
//config:	Overwrite a file to hide its contents, and optionally delete it

//applet:IF_SHRED(APPLET(shred, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SHRED) += shred.o

//usage:#define shred_trivial_usage
//usage:       "FILE..."
//usage:#define shred_full_usage "\n\n"
//usage:       "Overwrite/delete FILEs\n"
//usage:     "\n	-f	Chmod to ensure writability"
//usage:     "\n	-n N	Overwrite N times (default 3)"
//usage:     "\n	-z	Final overwrite with zeros"
//usage:     "\n	-u	Remove file"
//-x and -v are accepted but have no effect

/* shred (GNU coreutils) 8.25:
-f, --force		change permissions to allow writing if necessary
-u			truncate and remove file after overwriting
-z, --zero		add a final overwrite with zeros to hide shredding
-n, --iterations=N	overwrite N times instead of the default (3)
-v, --verbose		show progress
-x, --exact		do not round file sizes up to the next full block; this is the default for non-regular files
--random-source=FILE	get random bytes from FILE
-s, --size=N		shred this many bytes (suffixes like K, M, G accepted)
--remove[=HOW]		like -u but give control on HOW to delete;  See below
*/

#include "libbb.h"

int shred_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int shred_main(int argc UNUSED_PARAM, char **argv)
{
	int rand_fd = rand_fd; /* for compiler */
	int zero_fd;
	unsigned num_iter = 3;
	unsigned opt;
	enum {
		OPT_f = (1 << 0),
		OPT_u = (1 << 1),
		OPT_z = (1 << 2),
		OPT_n = (1 << 3),
		OPT_v = (1 << 4),
		OPT_x = (1 << 5),
	};

	opt = getopt32(argv, "fuzn:+vx", &num_iter);
	argv += optind;

	zero_fd = xopen("/dev/zero", O_RDONLY);
	if (num_iter != 0)
		rand_fd = xopen("/dev/urandom", O_RDONLY);

	if (!*argv)
		bb_show_usage();

	for (;;) {
		struct stat sb;
		const char *fname;
		unsigned i;
		int fd;

		fname = *argv++;
		if (!fname)
			break;
		fd = -1;
		if (opt & OPT_f) {
			fd = open(fname, O_WRONLY);
			if (fd < 0)
				chmod(fname, 0666);
		}
		if (fd < 0)
			fd = xopen(fname, O_WRONLY);

		if (fstat(fd, &sb) == 0 && sb.st_size > 0) {
			off_t size = sb.st_size;

			for (i = 0; i < num_iter; i++) {
				bb_copyfd_size(rand_fd, fd, size);
				fdatasync(fd);
				xlseek(fd, 0, SEEK_SET);
			}
			if (opt & OPT_z) {
				bb_copyfd_size(zero_fd, fd, size);
				fdatasync(fd);
			}
			if (opt & OPT_u) {
				ftruncate(fd, 0);
				xunlink(fname);
			}
			xclose(fd);
		}
	}

	return EXIT_SUCCESS;
}
