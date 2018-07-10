/* vi: set sw=4 ts=4: */
/*
 * mesg implementation for busybox
 *
 * Copyright (c) 2002 Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config MESG
//config:	bool "mesg (1.2 kb)"
//config:	default y
//config:	help
//config:	Mesg controls access to your terminal by others. It is typically
//config:	used to allow or disallow other users to write to your terminal
//config:
//config:config FEATURE_MESG_ENABLE_ONLY_GROUP
//config:	bool "Enable writing to tty only by group, not by everybody"
//config:	default y
//config:	depends on MESG
//config:	help
//config:	Usually, ttys are owned by group "tty", and "write" tool is
//config:	setgid to this group. This way, "mesg y" only needs to enable
//config:	"write by owning group" bit in tty mode.
//config:
//config:	If you set this option to N, "mesg y" will enable writing
//config:	by anybody at all. This is not recommended.

//applet:IF_MESG(APPLET_NOFORK(mesg, mesg, BB_DIR_USR_BIN, BB_SUID_DROP, mesg))

//kbuild:lib-$(CONFIG_MESG) += mesg.o

//usage:#define mesg_trivial_usage
//usage:       "[y|n]"
//usage:#define mesg_full_usage "\n\n"
//usage:       "Control write access to your terminal\n"
//usage:       "	y	Allow write access to your terminal\n"
//usage:       "	n	Disallow write access to your terminal"

#include "libbb.h"

#if ENABLE_FEATURE_MESG_ENABLE_ONLY_GROUP
#define S_IWGRP_OR_S_IWOTH  S_IWGRP
#else
#define S_IWGRP_OR_S_IWOTH  (S_IWGRP | S_IWOTH)
#endif

int mesg_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int mesg_main(int argc UNUSED_PARAM, char **argv)
{
	struct stat sb;
	mode_t m;
	char c = 0;

	argv++;

	if (argv[0]
	 && (argv[1] || ((c = argv[0][0]) != 'y' && c != 'n'))
	) {
		bb_show_usage();
	}

	/* We are a NOFORK applet.
	 * (Not that it's very useful, but code is trivially NOFORK-safe).
	 * Play nice. Do not leak anything.
	 */

	if (!isatty(STDIN_FILENO))
		bb_error_msg_and_die("not a tty");

	xfstat(STDIN_FILENO, &sb, "stdin");
	if (c == 0) {
		puts((sb.st_mode & (S_IWGRP|S_IWOTH)) ? "is y" : "is n");
		return EXIT_SUCCESS;
	}
	m = (c == 'y') ? sb.st_mode | S_IWGRP_OR_S_IWOTH
	               : sb.st_mode & ~(S_IWGRP|S_IWOTH);
	if (fchmod(STDIN_FILENO, m) != 0)
		bb_perror_nomsg_and_die();
	return EXIT_SUCCESS;
}
