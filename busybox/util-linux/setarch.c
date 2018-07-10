/* vi: set sw=4 ts=4: */
/*
 * linux32/linux64 allows for changing uname emulation.
 *
 * Copyright 2002 Andi Kleen, SuSE Labs.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config SETARCH
//config:	bool "setarch (3.4 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	The linux32 utility is used to create a 32bit environment for the
//config:	specified program (usually a shell). It only makes sense to have
//config:	this util on a system that supports both 64bit and 32bit userland
//config:	(like amd64/x86, ppc64/ppc, sparc64/sparc, etc...).
//config:
//config:config LINUX32
//config:	bool "linux32 (3.2 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Alias to "setarch linux32".
//config:
//config:config LINUX64
//config:	bool "linux64 (3.2 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Alias to "setarch linux64".

//applet:IF_SETARCH(APPLET_NOEXEC(setarch, setarch, BB_DIR_BIN, BB_SUID_DROP, setarch))
//                  APPLET_NOEXEC:name     main     location    suid_type     help
//applet:IF_LINUX32(APPLET_NOEXEC(linux32, setarch, BB_DIR_BIN, BB_SUID_DROP, linux32))
//applet:IF_LINUX64(APPLET_NOEXEC(linux64, setarch, BB_DIR_BIN, BB_SUID_DROP, linux64))

//kbuild:lib-$(CONFIG_SETARCH) += setarch.o
//kbuild:lib-$(CONFIG_LINUX32) += setarch.o
//kbuild:lib-$(CONFIG_LINUX64) += setarch.o

//usage:#define setarch_trivial_usage
//usage:       "PERSONALITY [-R] PROG ARGS"
//usage:#define setarch_full_usage "\n\n"
//usage:       "PERSONALITY may be:"
//usage:   "\n""	linux32	Set 32bit uname emulation"
//usage:   "\n""	linux64	Set 64bit uname emulation"
//usage:   "\n"
//usage:   "\n""	-R	Disable address space randomization"
//usage:
//usage:#define linux32_trivial_usage NOUSAGE_STR
//usage:#define linux32_full_usage ""
//usage:
//usage:#define linux64_trivial_usage NOUSAGE_STR
//usage:#define linux64_full_usage ""

#include "libbb.h"
#include <sys/personality.h>

#ifndef ADDR_NO_RANDOMIZE
# define ADDR_NO_RANDOMIZE       0x0040000
#endif

int setarch_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setarch_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opts;
	unsigned long pers;

	/* Figure out what personality we are supposed to switch to ...
	 * we can be invoked as either:
	 * argv[0],argv[1] == "setarch","personality"
	 * argv[0]         == "personality"
	 */
	if (ENABLE_SETARCH && applet_name[0] == 's'
	 && argv[1] && is_prefixed_with(argv[1], "linux")
	) {
		argv++;
		applet_name = argv[0];
	}
	if ((!ENABLE_SETARCH && !ENABLE_LINUX32) || applet_name[5] == '6')
		/* linux64 */
		pers = PER_LINUX;
	else
	if ((!ENABLE_SETARCH && !ENABLE_LINUX64) || applet_name[5] == '3')
		/* linux32 */
		pers = PER_LINUX32;
	else
		bb_show_usage();

	opts = getopt32(argv, "+R"); /* '+': stop at first non-option */
	if (opts)
		pers |= ADDR_NO_RANDOMIZE;

	/* Try to set personality */
	if (personality(pers) < 0)
		bb_perror_msg_and_die("personality(0x%lx)", pers);

	argv += optind;
	if (!argv[0])
		(--argv)[0] = (char*)"/bin/sh";

	/* Try to execute the program */
	BB_EXECVP_or_die(argv);
}
