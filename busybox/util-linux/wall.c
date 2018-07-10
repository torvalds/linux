/* vi: set sw=4 ts=4: */
/*
 * wall - write a message to all logged-in users
 * Copyright (c) 2009 Bernhard Reutner-Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config WALL
//config:	bool "wall (2.5 kb)"
//config:	default y
//config:	depends on FEATURE_UTMP
//config:	help
//config:	Write a message to all users that are logged in.

/* Needs to be run by root or be suid root - needs to write to /dev/TTY: */
//applet:IF_WALL(APPLET(wall, BB_DIR_USR_BIN, BB_SUID_REQUIRE))

//kbuild:lib-$(CONFIG_WALL) += wall.o

//usage:#define wall_trivial_usage
//usage:	"[FILE]"
//usage:#define wall_full_usage "\n\n"
//usage:	"Write content of FILE or stdin to all logged-in users"
//usage:
//usage:#define wall_sample_usage
//usage:	"echo foo | wall\n"
//usage:	"wall ./mymessage"

#include "libbb.h"

int wall_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int wall_main(int argc UNUSED_PARAM, char **argv)
{
	struct utmpx *ut;
	char *msg;
	int fd;

	fd = STDIN_FILENO;
	if (argv[1]) {
		/* The applet is setuid.
		 * Access to the file must be under user's uid/gid.
		 */
		fd = xopen_as_uid_gid(argv[1], O_RDONLY, getuid(), getgid());
	}
	msg = xmalloc_read(fd, NULL);
	if (ENABLE_FEATURE_CLEAN_UP && argv[1])
		close(fd);
	setutxent();
	while ((ut = getutxent()) != NULL) {
		char *line;
		if (ut->ut_type != USER_PROCESS)
			continue;
		line = concat_path_file("/dev", ut->ut_line);
		xopen_xwrite_close(line, msg);
		free(line);
	}
	if (ENABLE_FEATURE_CLEAN_UP) {
		endutxent();
		free(msg);
	}
	return EXIT_SUCCESS;
}
