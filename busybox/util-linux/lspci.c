/* vi: set sw=4 ts=4: */
/*
 * lspci implementation for busybox
 *
 * Copyright (C) 2009  Malek Degachi <malek-degachi@laposte.net>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config LSPCI
//config:	bool "lspci (5.7 kb)"
//config:	default y
//config:	#select PLATFORM_LINUX
//config:	help
//config:	lspci is a utility for displaying information about PCI buses in the
//config:	system and devices connected to them.
//config:
//config:	This version uses sysfs (/sys/bus/pci/devices) only.

//applet:IF_LSPCI(APPLET_NOEXEC(lspci, lspci, BB_DIR_USR_BIN, BB_SUID_DROP, lspci))

//kbuild:lib-$(CONFIG_LSPCI) += lspci.o

//usage:#define lspci_trivial_usage
//usage:       "[-mk]"
//usage:#define lspci_full_usage "\n\n"
//usage:       "List all PCI devices"
//usage:     "\n"
//usage:     "\n	-m	Parsable output"
//usage:     "\n	-k	Show driver"

#include "libbb.h"

enum {
	OPT_m = (1 << 0),
	OPT_k = (1 << 1),
};

/*
 * PCI_SLOT_NAME PCI_CLASS: PCI_VID:PCI_DID [PCI_SUBSYS_VID:PCI_SUBSYS_DID] [DRIVER]
 */
static int FAST_FUNC fileAction(
		const char *fileName,
		struct stat *statbuf UNUSED_PARAM,
		void *userData UNUSED_PARAM,
		int depth UNUSED_PARAM)
{
	parser_t *parser;
	char *tokens[3];
	char *pci_slot_name = NULL, *driver = NULL;
	int pci_class = 0, pci_vid = 0, pci_did = 0;
	int pci_subsys_vid = 0, pci_subsys_did = 0;

	char *uevent_filename = concat_path_file(fileName, "/uevent");
	parser = config_open2(uevent_filename, fopen_for_read);
	free(uevent_filename);

	while (config_read(parser, tokens, 3, 2, "\0:=", PARSE_NORMAL)) {
		if (strcmp(tokens[0], "DRIVER") == 0) {
			driver = xstrdup(tokens[1]);
			continue;
		}

		if (strcmp(tokens[0], "PCI_CLASS") == 0) {
			pci_class = xstrtou(tokens[1], 16)>>8;
			continue;
		}

		if (strcmp(tokens[0], "PCI_ID") == 0) {
			pci_vid = xstrtou(tokens[1], 16);
			pci_did = xstrtou(tokens[2], 16);
			continue;
		}

		if (strcmp(tokens[0], "PCI_SUBSYS_ID") == 0) {
			pci_subsys_vid = xstrtou(tokens[1], 16);
			pci_subsys_did = xstrtou(tokens[2], 16);
			continue;
		}

		if (strcmp(tokens[0], "PCI_SLOT_NAME") == 0) {
			pci_slot_name = xstrdup(tokens[2]);
			continue;
		}
	}
	config_close(parser);


	if (option_mask32 & OPT_m) {
		printf("%s \"Class %04x\" \"%04x\" \"%04x\" \"%04x\" \"%04x\"",
			pci_slot_name, pci_class, pci_vid, pci_did,
			pci_subsys_vid, pci_subsys_did);
	} else {
		printf("%s Class %04x: %04x:%04x",
			pci_slot_name, pci_class, pci_vid, pci_did);
	}

	if ((option_mask32 & OPT_k) && driver) {
		if (option_mask32 & OPT_m) {
			printf(" \"%s\"", driver);
		} else {
			printf(" %s", driver);
		}
	}
	bb_putchar('\n');

	free(driver);
	free(pci_slot_name);

	return TRUE;
}

int lspci_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int lspci_main(int argc UNUSED_PARAM, char **argv)
{
	getopt32(argv, "m" /*non-compat:*/ "k" /*ignored:*/ "nv");

	recursive_action("/sys/bus/pci/devices",
			ACTION_RECURSE,
			fileAction,
			NULL, /* dirAction */
			NULL, /* userData */
			0 /* depth */);

	return EXIT_SUCCESS;
}
