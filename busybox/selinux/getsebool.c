/*
 * getsebool
 *
 * Based on libselinux 1.33.1
 * Port to BusyBox  Hiroshi Shinji <shiroshi@my.email.ne.jp>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config GETSEBOOL
//config:	bool "getsebool (5.5 kb)"
//config:	default n
//config:	depends on SELINUX
//config:	help
//config:	Enable support to get SELinux boolean values.

//applet:IF_GETSEBOOL(APPLET(getsebool, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_GETSEBOOL) += getsebool.o

//usage:#define getsebool_trivial_usage
//usage:       "-a or getsebool boolean..."
//usage:#define getsebool_full_usage "\n\n"
//usage:       "	-a	Show all selinux booleans"

#include "libbb.h"

int getsebool_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int getsebool_main(int argc, char **argv)
{
	int i, rc = 0, active, pending, len = 0;
	char **names;
	unsigned opt;

	selinux_or_die();
	opt = getopt32(argv, "a");

	if (opt) { /* -a */
		if (argc > 2)
			bb_show_usage();

		rc = security_get_boolean_names(&names, &len);
		if (rc)
			bb_perror_msg_and_die("can't get boolean names");

		if (!len) {
			puts("No booleans");
			return 0;
		}
	}

	if (!len) {
		if (argc < 2)
			bb_show_usage();
		len = argc - 1;
		names = xmalloc(sizeof(char *) * len);
		for (i = 0; i < len; i++)
			names[i] = xstrdup(argv[i + 1]);
	}

	for (i = 0; i < len; i++) {
		active = security_get_boolean_active(names[i]);
		if (active < 0) {
			bb_error_msg_and_die("error getting active value for %s", names[i]);
		}
		pending = security_get_boolean_pending(names[i]);
		if (pending < 0) {
			bb_error_msg_and_die("error getting pending value for %s", names[i]);
		}
		printf("%s --> %s", names[i], (active ? "on" : "off"));
		if (pending != active)
			printf(" pending: %s", (pending ? "on" : "off"));
		bb_putchar('\n');
	}

	if (ENABLE_FEATURE_CLEAN_UP) {
		for (i = 0; i < len; i++)
			free(names[i]);
		free(names);
	}

	return rc;
}
