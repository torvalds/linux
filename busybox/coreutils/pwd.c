/* vi: set sw=4 ts=4: */
/*
 * Mini pwd implementation for busybox
 *
 * Copyright (C) 1995, 1996 by Bruce Perens <bruce@pixar.com>.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config PWD
//config:	bool "pwd (3.4 kb)"
//config:	default y
//config:	help
//config:	pwd is used to print the current directory.

//applet:IF_PWD(APPLET_NOFORK(pwd, pwd, BB_DIR_BIN, BB_SUID_DROP, pwd))

//kbuild:lib-$(CONFIG_PWD) += pwd.o

//usage:#define pwd_trivial_usage
//usage:       ""
//usage:#define pwd_full_usage "\n\n"
//usage:       "Print the full filename of the current working directory"
//usage:
//usage:#define pwd_example_usage
//usage:       "$ pwd\n"
//usage:       "/root\n"

#include "libbb.h"

static int logical_getcwd(void)
{
	struct stat st1;
	struct stat st2;
	char *wd;
	char *p;

	wd = getenv("PWD");
	if (!wd || wd[0] != '/')
		return 0;

	p = wd;
	while (*p) {
		/* doing strstr(p, "/.") by hand is smaller and faster... */
		if (*p++ != '/')
			continue;
		if (*p != '.')
			continue;
		/* we found "/.", skip to next char */
		p++;
		if (*p == '.')
			p++; /* we found "/.." */
		if (*p == '\0' || *p == '/')
			return 0; /* "/./" or "/../" component: bad */
	}

	if (stat(wd, &st1) != 0)
		return 0;
	if (stat(".", &st2) != 0)
		return 0;
	if (st1.st_ino != st2.st_ino)
		return 0;
	if (st1.st_dev != st2.st_dev)
		return 0;

	puts(wd);
	return 1;
}

int pwd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int pwd_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	char *buf;

	if (ENABLE_DESKTOP) {
		/* TODO: assume -L if $POSIXLY_CORRECT? (coreutils does that)
		 * Rationale:
		 * POSIX requires a default of -L, but most scripts expect -P
		 */
		unsigned opt = getopt32(argv, "LP");
		if ((opt & 1) && logical_getcwd())
			return fflush_all();
	}

	buf = xrealloc_getcwd_or_warn(NULL);

	if (buf) {
		puts(buf);
		free(buf);
		return fflush_all();
	}

	return EXIT_FAILURE;
}
