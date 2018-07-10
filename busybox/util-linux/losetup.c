/* vi: set sw=4 ts=4: */
/*
 * Mini losetup implementation for busybox
 *
 * Copyright (C) 2002  Matt Kraai.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config LOSETUP
//config:	bool "losetup (5.4 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	losetup is used to associate or detach a loop device with a regular
//config:	file or block device, and to query the status of a loop device. This
//config:	version does not currently support enabling data encryption.

//applet:IF_LOSETUP(APPLET_NOEXEC(losetup, losetup, BB_DIR_SBIN, BB_SUID_DROP, losetup))

//kbuild:lib-$(CONFIG_LOSETUP) += losetup.o

//usage:#define losetup_trivial_usage
//usage:       "[-r] [-o OFS] {-f|LOOPDEV} FILE - associate loop devices\n"
//usage:       "	losetup -d LOOPDEV - disassociate\n"
//usage:       "	losetup -a - show status\n"
//usage:       "	losetup -f - show next free loop device"
//usage:#define losetup_full_usage "\n\n"
//usage:       "	-o OFS	Start OFS bytes into FILE"
//usage:     "\n	-r	Read-only"
//usage:     "\n	-f	Show/use next free loop device"
//usage:
//usage:#define losetup_notes_usage
//usage:       "One argument (losetup /dev/loop1) will display the current association\n"
//usage:       "(if any), or disassociate it (with -d). The display shows the offset\n"
//usage:       "and filename of the file the loop device is currently bound to.\n\n"
//usage:       "Two arguments (losetup /dev/loop1 file.img) create a new association,\n"
//usage:       "with an optional offset (-o 12345). Encryption is not yet supported.\n"
//usage:       "losetup -f will show the first loop free loop device\n\n"

#include "libbb.h"

/* 1048575 is a max possible minor number in Linux circa 2010 */
/* for now use something less extreme */
#define MAX_LOOP_NUM 1023

int losetup_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int losetup_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opt;
	char *opt_o;
	char dev[LOOP_NAMESIZE];
	enum {
		OPT_d = (1 << 0),
		OPT_o = (1 << 1),
		OPT_f = (1 << 2),
		OPT_a = (1 << 3),
		OPT_r = (1 << 4), /* must be last */
	};

	opt = getopt32(argv, "^" "do:far" "\0" "?2:d--ofar:a--ofr", &opt_o);
	argv += optind;

	/* LOOPDEV */
	if (!opt && argv[0] && !argv[1]) {
		char *s;

		s = query_loop(argv[0]);
		if (!s)
			bb_simple_perror_msg_and_die(argv[0]);
		printf("%s: %s\n", argv[0], s);
		if (ENABLE_FEATURE_CLEAN_UP)
			free(s);
		return EXIT_SUCCESS;
	}

	/* -d LOOPDEV */
	if (opt == OPT_d && argv[0]) {
		if (del_loop(argv[0]))
			bb_simple_perror_msg_and_die(argv[0]);
		return EXIT_SUCCESS;
	}

	/* -a */
	if (opt == OPT_a) {
		int n;
		for (n = 0; n < MAX_LOOP_NUM; n++) {
			char *s;

			sprintf(dev, LOOP_FORMAT, n);
			s = query_loop(dev);
			if (s) {
				printf("%s: %s\n", dev, s);
				free(s);
			}
		}
		return EXIT_SUCCESS;
	}

	/* contains -f */
	if (opt & OPT_f) {
		char *s;
		int n = 0;

		do {
			if (n > MAX_LOOP_NUM)
				bb_error_msg_and_die("no free loop devices");
			sprintf(dev, LOOP_FORMAT, n++);
			s = query_loop(dev);
			free(s);
		} while (s);
		/* now: dev is next free "/dev/loopN" */
		if ((opt == OPT_f) && !argv[0]) {
			puts(dev);
			return EXIT_SUCCESS;
		}
	}

	/* [-r] [-o OFS] {-f|LOOPDEV} FILE */
	if (argv[0] && ((opt & OPT_f) || argv[1])) {
		unsigned long long offset = 0;
		char *d = dev;

		if (opt & OPT_o)
			offset = xatoull(opt_o);
		if (!(opt & OPT_f))
			d = *argv++;

		if (argv[0]) {
			if (set_loop(&d, argv[0], offset, (opt & OPT_r) ? BB_LO_FLAGS_READ_ONLY : 0) < 0)
				bb_simple_perror_msg_and_die(argv[0]);
			return EXIT_SUCCESS;
		}
	}

	/* TODO: util-linux 2.28 shows this when run w/o params:
	 * NAME       SIZELIMIT OFFSET AUTOCLEAR RO BACK-FILE     DIO
	 * /dev/loop0         0      0         1  0 /PATH/TO/FILE   0
	 *
	 * implemented by reading /sys:
	 *
	 * open("/sys/block", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC) = 3
	 * newfstatat(3, "loop0/loop/backing_file", {st_mode=S_IFREG|0444, st_size=4096, ...}, 0) = 0
	 * stat("/dev/loop0", {st_mode=S_IFBLK|0660, st_rdev=makedev(7, 0), ...}) = 0
	 * open("/sys/dev/block/7:0/loop/offset", O_RDONLY|O_CLOEXEC) = 5
	 * read(5, "0\n", 4096)                    = 2
	 * open("/sys/dev/block/7:0/loop/sizelimit", O_RDONLY|O_CLOEXEC) = 5
	 * read(5, "0\n", 4096)                    = 2
	 * open("/sys/dev/block/7:0/loop/offset", O_RDONLY|O_CLOEXEC) = 5
	 * read(5, "0\n", 4096)                    = 2
	 * open("/sys/dev/block/7:0/loop/autoclear", O_RDONLY|O_CLOEXEC) = 5
	 * read(5, "1\n", 4096)                    = 2
	 * open("/sys/dev/block/7:0/ro", O_RDONLY|O_CLOEXEC)     = 5
	 * read(5, "0\n", 4096)                    = 2
	 * open("/sys/dev/block/7:0/loop/backing_file", O_RDONLY|O_CLOEXEC) = 5
	 * read(5, "/PATH/TO/FILE", 4096) = 37
	 * open("/sys/dev/block/7:0/loop/dio", O_RDONLY|O_CLOEXEC) = 5
	 * read(5, "0\n", 4096)                    = 2
	 */

	bb_show_usage(); /* does not return */
	/*return EXIT_FAILURE;*/
}
