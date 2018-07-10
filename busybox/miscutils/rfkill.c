/* vi: set sw=4 ts=4: */
/*
 * rfkill implementation for busybox
 *
 * Copyright (C) 2010  Malek Degachi <malek-degachi@laposte.net>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config RFKILL
//config:	bool "rfkill (5.3 kb)"
//config:	default n # doesn't build on Ubuntu 9.04
//config:	select PLATFORM_LINUX
//config:	help
//config:	Enable/disable wireless devices.
//config:
//config:	rfkill list : list all wireless devices
//config:	rfkill list bluetooth : list all bluetooth devices
//config:	rfkill list 1 : list device corresponding to the given index
//config:	rfkill block|unblock wlan : block/unblock all wlan(wifi) devices
//config:

//applet:IF_RFKILL(APPLET(rfkill, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_RFKILL) += rfkill.o

//usage:#define rfkill_trivial_usage
//usage:       "COMMAND [INDEX|TYPE]"
//usage:#define rfkill_full_usage "\n\n"
//usage:       "Enable/disable wireless devices\n"
//usage:       "\nCommands:"
//usage:     "\n	list [INDEX|TYPE]	List current state"
//usage:     "\n	block INDEX|TYPE	Disable device"
//usage:     "\n	unblock INDEX|TYPE	Enable device"
//usage:     "\n"
//usage:     "\n	TYPE: all, wlan(wifi), bluetooth, uwb(ultrawideband),"
//usage:     "\n		wimax, wwan, gps, fm"

#include "libbb.h"
#include <linux/rfkill.h>

enum {
	OPT_b = (1 << 0), /* must be = 1 */
	OPT_u = (1 << 1),
	OPT_l = (1 << 2),
};

int rfkill_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int rfkill_main(int argc UNUSED_PARAM, char **argv)
{
	struct rfkill_event event;
	const char *rf_name;
	int rf_fd;
	int mode;
	int rf_type;
	int rf_idx;
	unsigned rf_opt = 0;

	argv++;
	/* Must have one or two params */
	if (!argv[0] || (argv[1] && argv[2]))
		bb_show_usage();

	mode = O_RDWR | O_NONBLOCK;
	rf_name = argv[1];
	if (strcmp(argv[0], "list") == 0) {
		rf_opt |= OPT_l;
		mode = O_RDONLY | O_NONBLOCK;
	} else if (strcmp(argv[0], "block") == 0 && rf_name) {
		rf_opt |= OPT_b;
	} else if (strcmp(argv[0], "unblock") == 0 && rf_name) {
		rf_opt |= OPT_u;
	} else
		bb_show_usage();

	rf_type = RFKILL_TYPE_ALL;
	rf_idx = -1;
	if (rf_name) {
		static const char rfkill_types[] ALIGN1 = "all\0wlan\0bluetooth\0uwb\0wimax\0wwan\0gps\0fm\0";
		if (strcmp(rf_name, "wifi") == 0)
			rf_name = "wlan";
		if (strcmp(rf_name, "ultrawideband") == 0)
			rf_name = "uwb";
		rf_type = index_in_strings(rfkill_types, rf_name);
		if (rf_type < 0) {
			rf_idx = xatoi_positive(rf_name);
		}
	}

	rf_fd = device_open("/dev/rfkill", mode);
	if (rf_fd < 0)
		bb_perror_msg_and_die("/dev/rfkill");

	if (rf_opt & OPT_l) {
		while (full_read(rf_fd, &event, sizeof(event)) == RFKILL_EVENT_SIZE_V1) {
			parser_t *parser;
			char *tokens[2];
			char rf_sysfs[sizeof("/sys/class/rfkill/rfkill%u/uevent") + sizeof(int)*3];
			char *name, *type;

			if (rf_type && rf_type != event.type && rf_idx < 0) {
				continue;
			}

			if (rf_idx >= 0 && event.idx != rf_idx) {
				continue;
			}

			name = NULL;
			type = NULL;
			sprintf(rf_sysfs, "/sys/class/rfkill/rfkill%u/uevent", event.idx);
			parser = config_open2(rf_sysfs, fopen_for_read);
			while (config_read(parser, tokens, 2, 2, "\n=", PARSE_NORMAL)) {
				if (strcmp(tokens[0], "RFKILL_NAME") == 0) {
					name = xstrdup(tokens[1]);
					continue;
				}
				if (strcmp(tokens[0], "RFKILL_TYPE") == 0) {
					type = xstrdup(tokens[1]);
					continue;
				}
			}
			config_close(parser);

			printf("%u: %s: %s\n", event.idx, name, type);
			printf("\tSoft blocked: %s\n", event.soft ? "yes" : "no");
			printf("\tHard blocked: %s\n", event.hard ? "yes" : "no");
			free(name);
			free(type);
		}
	} else {
		memset(&event, 0, sizeof(event));
		if (rf_type >= 0) {
			event.type = rf_type;
			event.op = RFKILL_OP_CHANGE_ALL;
		}

		if (rf_idx >= 0) {
			event.idx = rf_idx;
			event.op = RFKILL_OP_CHANGE;
		}

		/* Note: OPT_b == 1 */
		event.soft = (rf_opt & OPT_b);

		xwrite(rf_fd, &event, sizeof(event));
	}

	return EXIT_SUCCESS;
}
