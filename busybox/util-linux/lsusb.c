/* vi: set sw=4 ts=4: */
/*
 * lsusb implementation for busybox
 *
 * Copyright (C) 2009  Malek Degachi <malek-degachi@laposte.net>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config LSUSB
//config:	bool "lsusb (3.5 kb)"
//config:	default y
//config:	#select PLATFORM_LINUX
//config:	help
//config:	lsusb is a utility for displaying information about USB buses in the
//config:	system and devices connected to them.
//config:
//config:	This version uses sysfs (/sys/bus/usb/devices) only.

//applet:IF_LSUSB(APPLET_NOEXEC(lsusb, lsusb, BB_DIR_USR_BIN, BB_SUID_DROP, lsusb))

//kbuild:lib-$(CONFIG_LSUSB) += lsusb.o

//usage:#define lsusb_trivial_usage NOUSAGE_STR
//usage:#define lsusb_full_usage ""

#include "libbb.h"

static int FAST_FUNC fileAction(
		const char *fileName,
		struct stat *statbuf UNUSED_PARAM,
		void *userData UNUSED_PARAM,
		int depth UNUSED_PARAM)
{
	parser_t *parser;
	char *tokens[4];
	char *busnum = NULL, *devnum = NULL;
	int product_vid = 0, product_did = 0;
	char *uevent_filename = concat_path_file(fileName, "/uevent");

	parser = config_open2(uevent_filename, fopen_for_read);
	free(uevent_filename);

	while (config_read(parser, tokens, 4, 2, "\\/=", PARSE_NORMAL)) {
		if ((parser->lineno == 1) && strcmp(tokens[0], "DEVTYPE") == 0) {
			break;
		}

		if (strcmp(tokens[0], "PRODUCT") == 0) {
			product_vid = xstrtou(tokens[1], 16);
			product_did = xstrtou(tokens[2], 16);
			continue;
		}

		if (strcmp(tokens[0], "BUSNUM") == 0) {
			busnum = xstrdup(tokens[1]);
			continue;
		}

		if (strcmp(tokens[0], "DEVNUM") == 0) {
			devnum = xstrdup(tokens[1]);
			continue;
		}
	}
	config_close(parser);

	if (busnum) {
		printf("Bus %s Device %s: ID %04x:%04x\n", busnum, devnum, product_vid, product_did);
		free(busnum);
		free(devnum);
	}

	return TRUE;
}

int lsusb_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int lsusb_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	/* no options, no getopt */

	recursive_action("/sys/bus/usb/devices",
			ACTION_RECURSE,
			fileAction,
			NULL, /* dirAction */
			NULL, /* userData */
			0 /* depth */);

	return EXIT_SUCCESS;
}
