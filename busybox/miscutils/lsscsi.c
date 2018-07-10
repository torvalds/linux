/* vi: set sw=4 ts=4: */
/*
 * lsscsi implementation for busybox
 *
 * Copyright (C) 2017 Markus Gothe <nietzsche@lysator.liu.se>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config LSSCSI
//config:	bool "lsscsi (2.4 kb)"
//config:	default y
//config:	#select PLATFORM_LINUX
//config:	help
//config:	lsscsi is a utility for displaying information about SCSI buses in the
//config:	system and devices connected to them.
//config:
//config:	This version uses sysfs (/sys/bus/scsi/devices) only.

//applet:IF_LSSCSI(APPLET_NOEXEC(lsscsi, lsscsi, BB_DIR_USR_BIN, BB_SUID_DROP, lsscsi))

//kbuild:lib-$(CONFIG_LSSCSI) += lsscsi.o

//usage:#define lsscsi_trivial_usage NOUSAGE_STR
//usage:#define lsscsi_full_usage ""

#include "libbb.h"

static const char scsi_dir[] ALIGN1 = "/sys/bus/scsi/devices";

static char *get_line(const char *filename, char *buf, unsigned *bufsize_p)
{
	unsigned bufsize = *bufsize_p;
	ssize_t sz;

	if ((int)(bufsize - 2) <= 0)
		return buf;

	sz = open_read_close(filename, buf, bufsize - 2);
	if (sz < 0)
		sz = 0;
	buf[sz] = '\0';

	sz = (trim(buf) - buf) + 1;
	bufsize -= sz;
	buf += sz;
	buf[0] = '\0';

	*bufsize_p = bufsize;
	return buf;
}

int lsscsi_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int lsscsi_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	struct dirent *de;
	DIR *dir;

	xchdir(scsi_dir);

	dir = xopendir(".");
	while ((de = readdir(dir)) != NULL) {
		char buf[256];
		char *ptr;
		unsigned bufsize;
		const char *vendor;
		const char *type_str;
		const char *type_name;
		const char *model;
		const char *rev;
		unsigned type;

		if (!isdigit(de->d_name[0]))
			continue;
		if (!strchr(de->d_name, ':'))
			continue;
		if (chdir(de->d_name) != 0)
			continue;

		bufsize = sizeof(buf);
		vendor = buf;
		ptr = get_line("vendor", buf, &bufsize);
		type_str = ptr;
		ptr = get_line("type", ptr, &bufsize);
		model = ptr;
		ptr = get_line("model", ptr, &bufsize);
		rev = ptr;
		ptr = get_line("rev", ptr, &bufsize);

		printf("[%s]\t", de->d_name);

#define scsi_device_types \
	"disk\0"    "tape\0"    "printer\0" "process\0" \
	"worm\0"    "\0"        "scanner\0" "optical\0" \
	"mediumx\0" "comms\0"   "\0"        "\0"        \
	"storage\0" "enclosu\0" "sim dsk\0" "opti rd\0" \
	"bridge\0"  "osd\0"     "adi\0"     "\0"        \
	"\0"        "\0"        "\0"        "\0"        \
	"\0"        "\0"        "\0"        "\0"        \
	"\0"        "\0"        "wlun\0"    "no dev"
		type = bb_strtou(type_str, NULL, 10);
		if (errno
		 || type >= 0x20
		 || (type_name = nth_string(scsi_device_types, type))[0] == '\0'
		) {
			printf("(%s)\t", type_str);
		} else {
			printf("%s\t", type_name);
		}

		printf("%s\t""%s\t""%s\n",
			vendor,
			model,
			rev
		);
		/* TODO: also output device column, e.g. "/dev/sdX" */

		/* chdir("..") may not work as expected,
		 * since we might have followed a symlink.
		 */
		xchdir(scsi_dir);
	}

	if (ENABLE_FEATURE_CLEAN_UP)
		closedir(dir);

	return EXIT_SUCCESS;
}
