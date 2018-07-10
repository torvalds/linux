/*
 * Copyright (c) 2017 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config NUKE
//config:	bool "nuke (2.4 kb)"
//config:	default y
//config:	help
//config:	Alias to "rm -rf".

//applet:IF_NUKE(APPLET_NOEXEC(nuke, nuke, BB_DIR_BIN, BB_SUID_DROP, nuke))

//kbuild:lib-$(CONFIG_NUKE) += nuke.o

//usage:#define nuke_trivial_usage
//usage:       "DIR..."
//usage:#define nuke_full_usage "\n\n"
//usage:       "Remove DIRs"

#include "libbb.h"

/* This is a NOEXEC applet. Be very careful! */

int nuke_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int nuke_main(int argc UNUSED_PARAM, char **argv)
{
// klibc-utils do not check opts, will try to delete "-dir" args
	//opt = getopt32(argv, "");
	//argv += optind;

	while (*++argv) {
#if 0
// klibc-utils do not check this, will happily operate on ".."
		const char *base = bb_get_last_path_component_strip(*argv);
		if (DOT_OR_DOTDOT(base)) {
			bb_error_msg("can't remove '.' or '..'");
			continue;
		}
#endif
		remove_file(*argv, FILEUTILS_FORCE | FILEUTILS_RECUR);
	}

// klibc-utils do not indicate errors
	return EXIT_SUCCESS;
}
