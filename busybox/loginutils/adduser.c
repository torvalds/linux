/* vi: set sw=4 ts=4: */
/*
 * adduser - add users to /etc/passwd and /etc/shadow
 *
 * Copyright (C) 1999 by Lineo, inc. and John Beppu
 * Copyright (C) 1999,2000,2001 by John Beppu <beppu@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config ADDUSER
//config:	bool "adduser (15 kb)"
//config:	default y
//config:	select LONG_OPTS
//config:	help
//config:	Utility for creating a new user account.
//config:
//config:config FEATURE_CHECK_NAMES
//config:	bool "Enable sanity check on user/group names in adduser and addgroup"
//config:	default n
//config:	depends on ADDUSER || ADDGROUP
//config:	help
//config:	Enable sanity check on user and group names in adduser and addgroup.
//config:	To avoid problems, the user or group name should consist only of
//config:	letters, digits, underscores, periods, at signs and dashes,
//config:	and not start with a dash (as defined by IEEE Std 1003.1-2001).
//config:	For compatibility with Samba machine accounts "$" is also supported
//config:	at the end of the user or group name.
//config:
//config:config LAST_ID
//config:	int "Last valid uid or gid for adduser and addgroup"
//config:	depends on ADDUSER || ADDGROUP
//config:	default 60000
//config:	help
//config:	Last valid uid or gid for adduser and addgroup
//config:
//config:config FIRST_SYSTEM_ID
//config:	int "First valid system uid or gid for adduser and addgroup"
//config:	depends on ADDUSER || ADDGROUP
//config:	range 0 LAST_ID
//config:	default 100
//config:	help
//config:	First valid system uid or gid for adduser and addgroup
//config:
//config:config LAST_SYSTEM_ID
//config:	int "Last valid system uid or gid for adduser and addgroup"
//config:	depends on ADDUSER || ADDGROUP
//config:	range FIRST_SYSTEM_ID LAST_ID
//config:	default 999
//config:	help
//config:	Last valid system uid or gid for adduser and addgroup

//applet:IF_ADDUSER(APPLET_NOEXEC(adduser, adduser, BB_DIR_USR_SBIN, BB_SUID_DROP, adduser))

//kbuild:lib-$(CONFIG_ADDUSER) += adduser.o

//usage:#define adduser_trivial_usage
//usage:       "[OPTIONS] USER [GROUP]"
//usage:#define adduser_full_usage "\n\n"
//usage:       "Create new user, or add USER to GROUP\n"
//usage:     "\n	-h DIR		Home directory"
//usage:     "\n	-g GECOS	GECOS field"
//usage:     "\n	-s SHELL	Login shell"
//usage:     "\n	-G GRP		Group"
//usage:     "\n	-S		Create a system user"
//usage:     "\n	-D		Don't assign a password"
//usage:     "\n	-H		Don't create home directory"
//usage:     "\n	-u UID		User id"
//usage:     "\n	-k SKEL		Skeleton directory (/etc/skel)"

#include "libbb.h"

#if CONFIG_LAST_SYSTEM_ID < CONFIG_FIRST_SYSTEM_ID
#error Bad LAST_SYSTEM_ID or FIRST_SYSTEM_ID in .config
#endif
#if CONFIG_LAST_ID < CONFIG_LAST_SYSTEM_ID
#error Bad LAST_ID or LAST_SYSTEM_ID in .config
#endif


/* #define OPT_HOME           (1 << 0) */ /* unused */
/* #define OPT_GECOS          (1 << 1) */ /* unused */
#define OPT_SHELL          (1 << 2)
#define OPT_GID            (1 << 3)
#define OPT_DONT_SET_PASS  (1 << 4)
#define OPT_SYSTEM_ACCOUNT (1 << 5)
#define OPT_DONT_MAKE_HOME (1 << 6)
#define OPT_UID            (1 << 7)
#define OPT_SKEL           (1 << 8)

/* remix */
/* recoded such that the uid may be passed in *p */
static void passwd_study(struct passwd *p)
{
	int max = CONFIG_LAST_ID;

	if (getpwnam(p->pw_name)) {
		bb_error_msg_and_die("%s '%s' in use", "user", p->pw_name);
		/* this format string is reused in adduser and addgroup */
	}

	if (!(option_mask32 & OPT_UID)) {
		if (option_mask32 & OPT_SYSTEM_ACCOUNT) {
			p->pw_uid = CONFIG_FIRST_SYSTEM_ID;
			max = CONFIG_LAST_SYSTEM_ID;
		} else {
			p->pw_uid = CONFIG_LAST_SYSTEM_ID + 1;
		}
	}
	/* check for a free uid (and maybe gid) */
	while (getpwuid(p->pw_uid) || (p->pw_gid == (gid_t)-1 && getgrgid(p->pw_uid))) {
		if (option_mask32 & OPT_UID) {
			/* -u N, cannot pick uid other than N: error */
			bb_error_msg_and_die("%s '%s' in use", "uid", itoa(p->pw_uid));
			/* this format string is reused in adduser and addgroup */
		}
		if (p->pw_uid == max) {
			bb_error_msg_and_die("no %cids left", 'u');
			/* this format string is reused in adduser and addgroup */
		}
		p->pw_uid++;
	}

	if (p->pw_gid == (gid_t)-1) {
		p->pw_gid = p->pw_uid; /* new gid = uid */
		if (getgrnam(p->pw_name)) {
			bb_error_msg_and_die("%s '%s' in use", "group", p->pw_name);
			/* this format string is reused in adduser and addgroup */
		}
	}
}

static int addgroup_wrapper(struct passwd *p, const char *group_name)
{
	char *argv[6];

	argv[0] = (char*)"addgroup";
	if (group_name) {
		/* Add user to existing group */
		argv[1] = (char*)"--";
		argv[2] = p->pw_name;
		argv[3] = (char*)group_name;
		argv[4] = NULL;
	} else {
		/* Add user to his own group with the first free gid
		 * found in passwd_study.
		 */
		argv[1] = (char*)"--gid";
		argv[2] = utoa(p->pw_gid);
		argv[3] = (char*)"--";
		argv[4] = p->pw_name;
		argv[5] = NULL;
	}

	return spawn_and_wait(argv);
}

static void passwd_wrapper(const char *login_name) NORETURN;

static void passwd_wrapper(const char *login_name)
{
	BB_EXECLP("passwd", "passwd", "--", login_name, NULL);
	bb_error_msg_and_die("can't execute passwd, you must set password manually");
}

//FIXME: upstream adduser has no short options! NOT COMPATIBLE!
static const char adduser_longopts[] ALIGN1 =
		"home\0"                Required_argument "h"
		"gecos\0"               Required_argument "g"
		"shell\0"               Required_argument "s"
		"ingroup\0"             Required_argument "G"
		"disabled-password\0"   No_argument       "D"
		"empty-password\0"      No_argument       "D"
		"system\0"              No_argument       "S"
		"no-create-home\0"      No_argument       "H"
		"uid\0"                 Required_argument "u"
		"skel\0"                Required_argument "k"
		;

/*
 * adduser will take a login_name as its first parameter.
 * home, shell, gecos:
 * can be customized via command-line parameters.
 */
int adduser_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int adduser_main(int argc UNUSED_PARAM, char **argv)
{
	struct passwd pw;
	const char *usegroup = NULL;
	char *p;
	unsigned opts;
	char *uid;
	const char *skel = "/etc/skel";

	/* got root? */
	if (geteuid()) {
		bb_error_msg_and_die(bb_msg_perm_denied_are_you_root);
	}

	pw.pw_gecos = (char *)"Linux User,,,";
	/* We assume that newly created users "inherit" root's shell setting */
	pw.pw_shell = (char *)get_shell_name();
	pw.pw_dir = NULL;

	opts = getopt32long(argv, "^"
			"h:g:s:G:DSHu:k:"
			/* at least one and at most two non-option args */
			/* disable interactive passwd for system accounts */
			"\0" "-1:?2:SD",
			adduser_longopts,
			&pw.pw_dir, &pw.pw_gecos, &pw.pw_shell,
			&usegroup, &uid, &skel
	);
	if (opts & OPT_UID)
		pw.pw_uid = xatou_range(uid, 0, CONFIG_LAST_ID);

	argv += optind;
	pw.pw_name = argv[0];

	if (!opts && argv[1]) {
		/* if called with two non-option arguments, adduser
		 * will add an existing user to an existing group.
		 */
		return addgroup_wrapper(&pw, argv[1]);
	}

	/* fill in the passwd struct */
	die_if_bad_username(pw.pw_name);
	if (!pw.pw_dir) {
		/* create string for $HOME if not specified already */
		pw.pw_dir = xasprintf("/home/%s", argv[0]);
	}
	pw.pw_passwd = (char *)"x";
	if (opts & OPT_SYSTEM_ACCOUNT) {
		if (!usegroup) {
			usegroup = "nogroup";
		}
		if (!(opts & OPT_SHELL)) {
			pw.pw_shell = (char *) "/bin/false";
		}
	}
	pw.pw_gid = usegroup ? xgroup2gid(usegroup) : -1; /* exits on failure */

	/* make sure everything is kosher and setup uid && maybe gid */
	passwd_study(&pw);

	p = xasprintf("x:%u:%u:%s:%s:%s",
			(unsigned) pw.pw_uid, (unsigned) pw.pw_gid,
			pw.pw_gecos, pw.pw_dir, pw.pw_shell);
	if (update_passwd(bb_path_passwd_file, pw.pw_name, p, NULL) < 0) {
		return EXIT_FAILURE;
	}
	if (ENABLE_FEATURE_CLEAN_UP)
		free(p);
#if ENABLE_FEATURE_SHADOWPASSWDS
	/* /etc/shadow fields:
	 * 1. username
	 * 2. encrypted password
	 * 3. last password change (unix date (unix time/24*60*60))
	 * 4. minimum days required between password changes
	 * 5. maximum days password is valid
	 * 6. days before password is to expire that user is warned
	 * 7. days after password expires that account is disabled
	 * 8. unix date when login expires (i.e. when it may no longer be used)
	 */
	/* fields:     2 3  4 5     6 78 */
	p = xasprintf("!:%u:0:99999:7:::", (unsigned)(time(NULL)) / (24*60*60));
	/* ignore errors: if file is missing we suppose admin doesn't want it */
	update_passwd(bb_path_shadow_file, pw.pw_name, p, NULL);
	if (ENABLE_FEATURE_CLEAN_UP)
		free(p);
#endif

	/* add to group */
	addgroup_wrapper(&pw, usegroup);

	/* clear the umask for this process so it doesn't
	 * screw up the permissions on the mkdir and chown. */
	umask(0);
	if (!(opts & OPT_DONT_MAKE_HOME)) {
		/* set the owner and group so it is owned by the new user,
		 * then fix up the permissions to 2755. Can't do it before
		 * since chown will clear the setgid bit */
		int mkdir_err = mkdir(pw.pw_dir, 0755);
		if (mkdir_err == 0) {
			/* New home. Copy /etc/skel to it */
			const char *args[] = {
				"chown",
				"-R",
				xasprintf("%u:%u", (int)pw.pw_uid, (int)pw.pw_gid),
				pw.pw_dir,
				NULL
			};
			/* Be silent on any errors (like: no /etc/skel) */
			if (!(opts & OPT_SKEL))
				logmode = LOGMODE_NONE;
			copy_file(skel, pw.pw_dir, FILEUTILS_RECUR);
			logmode = LOGMODE_STDIO;
			chown_main(4, (char**)args);
		}
		if ((mkdir_err != 0 && errno != EEXIST)
		 || chown(pw.pw_dir, pw.pw_uid, pw.pw_gid) != 0
		 || chmod(pw.pw_dir, 02755) != 0 /* set setgid bit on homedir */
		) {
			bb_simple_perror_msg(pw.pw_dir);
		}
	}

	if (!(opts & OPT_DONT_SET_PASS)) {
		/* interactively set passwd */
		passwd_wrapper(pw.pw_name);
	}

	return EXIT_SUCCESS;
}
