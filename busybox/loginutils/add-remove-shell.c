/*
 * add-shell and remove-shell implementation for busybox
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Written by Alexander Shishkin <virtuoso@slind.org>
 *
 * Licensed under GPLv2 or later, see the LICENSE file in this source tree
 * for details.
 */
//config:config ADD_SHELL
//config:	bool "add-shell (2.8 kb)"
//config:	default y if DESKTOP
//config:	help
//config:	Add shells to /etc/shells.
//config:
//config:config REMOVE_SHELL
//config:	bool "remove-shell (2.7 kb)"
//config:	default y if DESKTOP
//config:	help
//config:	Remove shells from /etc/shells.

//                       APPLET_NOEXEC:name          main              location         suid_type     help
//applet:IF_ADD_SHELL(   APPLET_NOEXEC(add-shell   , add_remove_shell, BB_DIR_USR_SBIN, BB_SUID_DROP, add_shell   ))
//applet:IF_REMOVE_SHELL(APPLET_NOEXEC(remove-shell, add_remove_shell, BB_DIR_USR_SBIN, BB_SUID_DROP, remove_shell))

//kbuild:lib-$(CONFIG_ADD_SHELL)    += add-remove-shell.o
//kbuild:lib-$(CONFIG_REMOVE_SHELL) += add-remove-shell.o

//usage:#define add_shell_trivial_usage
//usage:       "SHELL..."
//usage:#define add_shell_full_usage "\n\n"
//usage:       "Add SHELLs to /etc/shells"

//usage:#define remove_shell_trivial_usage
//usage:       "SHELL..."
//usage:#define remove_shell_full_usage "\n\n"
//usage:       "Remove SHELLs from /etc/shells"

#include "libbb.h"

#define SHELLS_FILE "/etc/shells"

#define REMOVE_SHELL (ENABLE_REMOVE_SHELL && (!ENABLE_ADD_SHELL || applet_name[0] == 'r'))
#define ADD_SHELL    (ENABLE_ADD_SHELL && (!ENABLE_REMOVE_SHELL || applet_name[0] == 'a'))

#define dont_add ((char*)(uintptr_t)1)

int add_remove_shell_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int add_remove_shell_main(int argc UNUSED_PARAM, char **argv)
{
	FILE *orig_fp;
	char *orig_fn;
	char *new_fn;
	struct stat sb;

	sb.st_mode = 0666;

	argv++;

	orig_fn = xmalloc_follow_symlinks(SHELLS_FILE);
	if (!orig_fn)
		return EXIT_FAILURE;
	orig_fp = fopen_for_read(orig_fn);
	if (orig_fp)
		xfstat(fileno(orig_fp), &sb, orig_fn);


	new_fn = xasprintf("%s.tmp", orig_fn);
	/*
	 * O_TRUNC or O_EXCL? At the first glance, O_EXCL looks better,
	 * since it prevents races. But: (1) it requires a retry loop,
	 * (2) if /etc/shells.tmp is *stale*, then retry loop
	 * with O_EXCL will never succeed - it should have a timeout,
	 * after which it should revert to O_TRUNC.
	 * For now, I settle for O_TRUNC instead.
	 */
	xmove_fd(xopen3(new_fn, O_WRONLY | O_CREAT | O_TRUNC, sb.st_mode), STDOUT_FILENO);
	/* TODO?
	xfchown(STDOUT_FILENO, sb.st_uid, sb.st_gid);
	*/

	if (orig_fp) {
		/* Copy old file, possibly skipping removed shell names */
		char *line;
		while ((line = xmalloc_fgetline(orig_fp)) != NULL) {
			char **cpp = argv;
			while (*cpp) {
				if (*cpp != dont_add && strcmp(*cpp, line) == 0) {
					/* Old file has this shell name */
					if (REMOVE_SHELL) {
						/* we are remove-shell */
						/* delete this name by not copying it */
						goto next_line;
					}
					/* we are add-shell */
					/* mark this name as "do not add" */
					*cpp = dont_add;
				}
				cpp++;
			}
			/* copy shell name from old to new file */
			puts(line);
 next_line:
			free(line);
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			fclose(orig_fp);
	}

	if (ADD_SHELL) {
		char **cpp = argv;
		while (*cpp) {
			if (*cpp != dont_add)
				puts(*cpp);
			cpp++;
		}
	}

	/* Ensure we wrote out everything */
	if (fclose(stdout) != 0) {
		xunlink(new_fn);
		bb_perror_msg_and_die("%s: write error", new_fn);
	}

	/* Small hole: if rename fails, /etc/shells.tmp is not removed */
	xrename(new_fn, orig_fn);

	if (ENABLE_FEATURE_CLEAN_UP) {
		free(orig_fn);
		free(new_fn);
	}

	return EXIT_SUCCESS;
}
