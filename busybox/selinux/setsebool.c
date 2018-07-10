/*
 * setsebool
 * Simple setsebool
 * NOTE: -P option requires libsemanage, so this feature is
 * omitted in this version
 * Yuichi Nakamura <ynakam@hitachisoft.jp>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config SETSEBOOL
//config:	bool "setsebool (1.7 kb)"
//config:	default n
//config:	depends on SELINUX
//config:	help
//config:	Enable support for change boolean.
//config:	semanage and -P option is not supported yet.

//applet:IF_SETSEBOOL(APPLET(setsebool, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SETSEBOOL) += setsebool.o

//usage:#define setsebool_trivial_usage
//usage:       "boolean value"
//usage:#define setsebool_full_usage "\n\n"
//usage:       "Change boolean setting"

#include "libbb.h"

int setsebool_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setsebool_main(int argc, char **argv)
{
	char *p;
	int value;

	if (argc != 3)
		bb_show_usage();

	p = argv[2];

	if (LONE_CHAR(p, '1') || strcasecmp(p, "true") == 0 || strcasecmp(p, "on") == 0) {
		value = 1;
	} else if (LONE_CHAR(p, '0') || strcasecmp(p, "false") == 0 || strcasecmp(p, "off") == 0) {
		value = 0;
	} else {
		bb_show_usage();
	}

	if (security_set_boolean(argv[1], value) < 0)
		bb_error_msg_and_die("can't set boolean");

	return 0;
}
