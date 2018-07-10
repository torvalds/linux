/*
 * Copyright (c) 2017 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config RESUME
//config:	bool "resume (3.3 kb)"
//config:	default y
//config:	help
//config:	Resume from saved "suspend-to-disk" image

//applet:IF_RESUME(APPLET_NOEXEC(resume, resume, BB_DIR_BIN, BB_SUID_DROP, resume))

//kbuild:lib-$(CONFIG_RESUME) += resume.o

#include "libbb.h"

/* This is a NOEXEC applet. Be very careful! */

/* name_to_dev_t() in klibc-utils supports extended device name formats,
 * apart from the usual case where /dev/NAME already exists.
 *
 * - device number in hexadecimal represents itself (in dev_t layout).
 * - device number in major:minor decimal represents itself.
 * - if block device (or partition) with this name is found in sysfs.
 * - if /dev/ prefix is not given, it is assumed.
 *
 * klibc-utils also recognizes these, but they don't work
 * for "resume" tool purposes (thus we don't support them (yet?)):
 * - /dev/nfs
 * - /dev/ram (alias to /dev/ram0)
 * - /dev/mtd
 */
static dev_t name_to_dev_t(const char *devname)
{
	char devfile[sizeof(int)*3 * 2 + 4];
	char *sysname;
	unsigned major_num, minor_num;
	struct stat st;
	int r;

	if (strncmp(devname, "/dev/", 5) != 0) {
		char *cptr;

		cptr = strchr(devname, ':');
		if (cptr) {
			/* Colon-separated decimal device number? */
			*cptr = '\0';
			major_num = bb_strtou(devname, NULL, 10);
			if (!errno)
				minor_num = bb_strtou(cptr + 1, NULL, 10);
			*cptr = ':';
			if (!errno)
				return makedev(major_num, minor_num);
		} else {
			/* Hexadecimal device number? */
			dev_t res = (dev_t) bb_strtoul(devname, NULL, 16);
			if (!errno)
				return res;
		}

		devname = xasprintf("/dev/%s", devname);
	}
	/* Now devname is always "/dev/FOO" */

	if (stat(devname, &st) == 0 && S_ISBLK(st.st_mode))
		return st.st_rdev;

	/* Full blockdevs as well as partitions may be visible
	 * in /sys/class/block/ even if /dev is not populated.
	 */
	sysname = xasprintf("/sys/class/block/%s/dev", devname + 5);
	r = open_read_close(sysname, devfile, sizeof(devfile) - 1);
	//free(sysname);
	if (r > 0) {
		devfile[r] = '\0';
		if (sscanf(devfile, "%u:%u", &major_num, &minor_num) == 2) {
			return makedev(major_num, minor_num);
		}
	}

	return (dev_t) 0;
}

//usage:#define resume_trivial_usage
//usage:       "BLOCKDEV [OFFSET]"
//usage:#define resume_full_usage "\n"
//usage:   "\n""Restore system state from 'suspend-to-disk' data in BLOCKDEV"

int resume_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int resume_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned long long ofs;
	dev_t resume_device;
	char *s;
	int fd;

	argv++;
	if (!argv[0])
		bb_show_usage();

	resume_device = name_to_dev_t(argv[0]);
	if (major(resume_device) == 0) {
		bb_error_msg_and_die("invalid resume device: %s", argv[0]);
	}
	ofs = (argv[1] ? xstrtoull(argv[1], 0) : 0);

	fd = xopen("/sys/power/resume", O_WRONLY);
	s = xasprintf("%u:%u:%llu", major(resume_device), minor(resume_device), ofs);

	xwrite_str(fd, s);
	/* if write() returns, resume did not succeed */

	return EXIT_FAILURE; /* klibc-utils exits -1 aka 255 */
}
