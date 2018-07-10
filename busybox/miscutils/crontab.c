/* vi: set sw=4 ts=4: */
/*
 * CRONTAB
 *
 * usually setuid root, -c option only works if getuid() == geteuid()
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.west.oic.com)
 * Vladimir Oleynik <dzo@simtreas.ru> (C) 2002
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config CRONTAB
//config:	bool "crontab (9.7 kb)"
//config:	default y
//config:	help
//config:	Crontab manipulates the crontab for a particular user. Only
//config:	the superuser may specify a different user and/or crontab directory.
//config:	Note that busybox binary must be setuid root for this applet to
//config:	work properly.

/* Needs to be run by root or be suid root - needs to change /var/spool/cron* files: */
//applet:IF_CRONTAB(APPLET(crontab, BB_DIR_USR_BIN, BB_SUID_REQUIRE))

//kbuild:lib-$(CONFIG_CRONTAB) += crontab.o

//usage:#define crontab_trivial_usage
//usage:       "[-c DIR] [-u USER] [-ler]|[FILE]"
//usage:#define crontab_full_usage "\n\n"
//usage:       "	-c	Crontab directory"
//usage:     "\n	-u	User"
//usage:     "\n	-l	List crontab"
//usage:     "\n	-e	Edit crontab"
//usage:     "\n	-r	Delete crontab"
//usage:     "\n	FILE	Replace crontab by FILE ('-': stdin)"

#include "libbb.h"

#define CRONTABS        CONFIG_FEATURE_CROND_DIR "/crontabs"
#ifndef CRONUPDATE
#define CRONUPDATE      "cron.update"
#endif

static void edit_file(const struct passwd *pas, const char *file)
{
	const char *ptr;
	pid_t pid;

	pid = xvfork();
	if (pid) { /* parent */
		wait4pid(pid);
		return;
	}

	/* CHILD - change user and run editor */
	/* initgroups, setgid, setuid */
	change_identity(pas);
	setup_environment(pas->pw_shell,
			SETUP_ENV_CHANGEENV | SETUP_ENV_TO_TMP,
			pas);
	ptr = getenv("VISUAL");
	if (!ptr) {
		ptr = getenv("EDITOR");
		if (!ptr)
			ptr = "vi";
	}

	BB_EXECLP(ptr, ptr, file, NULL);
	bb_perror_msg_and_die("can't execute '%s'", ptr);
}

int crontab_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int crontab_main(int argc UNUSED_PARAM, char **argv)
{
	const struct passwd *pas;
	const char *crontab_dir = CRONTABS;
	char *tmp_fname;
	char *new_fname;
	char *user_name;  /* -u USER */
	int fd;
	int src_fd;
	int opt_ler;

	/* file [opts]     Replace crontab from file
	 * - [opts]        Replace crontab from stdin
	 * -u user         User
	 * -c dir          Crontab directory
	 * -l              List crontab for user
	 * -e              Edit crontab for user
	 * -r              Delete crontab for user
	 * bbox also supports -d == -r, but most other crontab
	 * implementations do not. Deprecated.
	 */
	enum {
		OPT_u = (1 << 0),
		OPT_c = (1 << 1),
		OPT_l = (1 << 2),
		OPT_e = (1 << 3),
		OPT_r = (1 << 4),
		OPT_ler = OPT_l + OPT_e + OPT_r,
	};

	opt_ler = getopt32(argv, "^" "u:c:lerd" "\0" "?1:dr"/*max one arg; -d implies -r*/,
				&user_name, &crontab_dir
	);
	argv += optind;

	if (sanitize_env_if_suid()) { /* Clears dangerous stuff, sets PATH */
		/* Run by non-root */
		if (opt_ler & (OPT_u|OPT_c))
			bb_error_msg_and_die(bb_msg_you_must_be_root);
	}

	if (opt_ler & OPT_u) {
		pas = xgetpwnam(user_name);
	} else {
		pas = xgetpwuid(getuid());
	}

#define user_name DONT_USE_ME_BEYOND_THIS_POINT

	/* From now on, keep only -l, -e, -r bits */
	opt_ler &= OPT_ler;
	if ((opt_ler - 1) & opt_ler) /* more than one bit set? */
		bb_show_usage();

	/* Read replacement file under user's UID/GID/group vector */
	src_fd = STDIN_FILENO;
	if (!opt_ler) { /* Replace? */
		if (!argv[0])
			bb_show_usage();
		if (NOT_LONE_DASH(argv[0])) {
			src_fd = xopen_as_uid_gid(argv[0], O_RDONLY, pas->pw_uid, pas->pw_gid);
		}
	}

	/* cd to our crontab directory */
	xchdir(crontab_dir);

	tmp_fname = NULL;

	/* Handle requested operation */
	switch (opt_ler) {

	default: /* case OPT_r: Delete */
		unlink(pas->pw_name);
		break;

	case OPT_l: /* List */
		{
			char *args[2] = { pas->pw_name, NULL };
			return bb_cat(args);
			/* list exits,
			 * the rest go play with cron update file */
		}

	case OPT_e: /* Edit */
		tmp_fname = xasprintf("%s.%u", crontab_dir, (unsigned)getpid());
		/* No O_EXCL: we don't want to be stuck if earlier crontabs
		 * were killed, leaving stale temp file behind */
		src_fd = xopen3(tmp_fname, O_RDWR|O_CREAT|O_TRUNC, 0600);
		fchown(src_fd, pas->pw_uid, pas->pw_gid);
		fd = open(pas->pw_name, O_RDONLY);
		if (fd >= 0) {
			bb_copyfd_eof(fd, src_fd);
			close(fd);
			xlseek(src_fd, 0, SEEK_SET);
		}
		close_on_exec_on(src_fd); /* don't want editor to see this fd */
		edit_file(pas, tmp_fname);
		/* fall through */

	case 0: /* Replace (no -l, -e, or -r were given) */
		new_fname = xasprintf("%s.new", pas->pw_name);
		fd = open(new_fname, O_WRONLY|O_CREAT|O_TRUNC|O_APPEND, 0600);
		if (fd >= 0) {
			bb_copyfd_eof(src_fd, fd);
			close(fd);
			xrename(new_fname, pas->pw_name);
		} else {
			bb_error_msg("can't create %s/%s",
					crontab_dir, new_fname);
		}
		if (tmp_fname)
			unlink(tmp_fname);
		/*free(tmp_fname);*/
		/*free(new_fname);*/
	} /* switch */

	/* Bump notification file.  Handle window where crond picks file up
	 * before we can write our entry out.
	 */
	while ((fd = open(CRONUPDATE, O_WRONLY|O_CREAT|O_APPEND, 0600)) >= 0) {
		struct stat st;

		fdprintf(fd, "%s\n", pas->pw_name);
		if (fstat(fd, &st) != 0 || st.st_nlink != 0) {
			/*close(fd);*/
			break;
		}
		/* st.st_nlink == 0:
		 * file was deleted, maybe crond missed our notification */
		close(fd);
		/* loop */
	}
	if (fd < 0) {
		bb_error_msg("can't append to %s/%s",
				crontab_dir, CRONUPDATE);
	}
	return 0;
}
