/* vi: set sw=4 ts=4: */
/*
 * deluser/delgroup implementation for busybox
 *
 * Copyright (C) 1999 by Lineo, inc. and John Beppu
 * Copyright (C) 1999,2000,2001 by John Beppu <beppu@codepoet.org>
 * Copyright (C) 2007 by Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config DELUSER
//config:	bool "deluser (8.4 kb)"
//config:	default y
//config:	help
//config:	Utility for deleting a user account.
//config:
//config:config DELGROUP
//config:	bool "delgroup (5.6 kb)"
//config:	default y
//config:	help
//config:	Utility for deleting a group account.
//config:
//config:config FEATURE_DEL_USER_FROM_GROUP
//config:	bool "Support removing users from groups"
//config:	default y
//config:	depends on DELGROUP
//config:	help
//config:	If called with two non-option arguments, deluser
//config:	or delgroup will remove an user from a specified group.

//                   APPLET_NOEXEC:name      main     location         suid_type     help
//applet:IF_DELUSER( APPLET_NOEXEC(deluser,  deluser, BB_DIR_USR_SBIN, BB_SUID_DROP, deluser))
//applet:IF_DELGROUP(APPLET_NOEXEC(delgroup, deluser, BB_DIR_USR_SBIN, BB_SUID_DROP, delgroup))

//kbuild:lib-$(CONFIG_DELUSER) += deluser.o
//kbuild:lib-$(CONFIG_DELGROUP) += deluser.o

//usage:#define deluser_trivial_usage
//usage:       IF_LONG_OPTS("[--remove-home] ") "USER"
//usage:#define deluser_full_usage "\n\n"
//usage:       "Delete USER from the system"
//	--remove-home is self-explanatory enough to put it in --help

//usage:#define delgroup_trivial_usage
//usage:	IF_FEATURE_DEL_USER_FROM_GROUP("[USER] ")"GROUP"
//usage:#define delgroup_full_usage "\n\n"
//usage:       "Delete group GROUP from the system"
//usage:	IF_FEATURE_DEL_USER_FROM_GROUP(" or user USER from group GROUP")

#include "libbb.h"

int deluser_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int deluser_main(int argc, char **argv)
{
	/* User or group name */
	char *name;
	/* Username (non-NULL only in "delgroup USER GROUP" case) */
	char *member;
	/* Name of passwd or group file */
	const char *pfile;
	/* Name of shadow or gshadow file */
	const char *sfile;
	/* Are we deluser or delgroup? */
	int do_deluser = (ENABLE_DELUSER && (!ENABLE_DELGROUP || applet_name[3] == 'u'));

#if !ENABLE_LONG_OPTS
	const int opt_delhome = 0;
#else
	int opt_delhome = 0;
	if (do_deluser) {
		opt_delhome = getopt32long(argv, "",
				"remove-home\0" No_argument "\xff");
		argv += opt_delhome;
		argc -= opt_delhome;
	}
#endif

	if (geteuid() != 0)
		bb_error_msg_and_die(bb_msg_perm_denied_are_you_root);

	name = argv[1];
	member = NULL;

	switch (argc) {
	case 3:
		if (!ENABLE_FEATURE_DEL_USER_FROM_GROUP || do_deluser)
			break;
		/* It's "delgroup USER GROUP" */
		member = name;
		name = argv[2];
		/* Fallthrough */

	case 2:
		if (do_deluser) {
			/* "deluser USER" */
			struct passwd *pw;

			pw = xgetpwnam(name); /* bail out if USER is wrong */
			pfile = bb_path_passwd_file;
			if (ENABLE_FEATURE_SHADOWPASSWDS)
				sfile = bb_path_shadow_file;
			if (opt_delhome)
				remove_file(pw->pw_dir, FILEUTILS_RECUR);
		} else {
			struct group *gr;
 do_delgroup:
			/* "delgroup GROUP" or "delgroup USER GROUP" */
			if (do_deluser < 0) { /* delgroup after deluser? */
				gr = getgrnam(name);
				if (!gr)
					return EXIT_SUCCESS;
			} else {
				gr = xgetgrnam(name); /* bail out if GROUP is wrong */
			}
			if (!member) {
				/* "delgroup GROUP" */
				struct passwd *pw;
				/* Check if the group is in use */
				while ((pw = getpwent()) != NULL) {
					if (pw->pw_gid == gr->gr_gid)
						bb_error_msg_and_die("'%s' still has '%s' as their primary group!",
							pw->pw_name, name);
				}
				//endpwent();
			}
			pfile = bb_path_group_file;
			if (ENABLE_FEATURE_SHADOWPASSWDS)
				sfile = bb_path_gshadow_file;
		}

		/* Modify pfile, then sfile */
		do {
			if (update_passwd(pfile, name, NULL, member) == -1)
				return EXIT_FAILURE;
			if (ENABLE_FEATURE_SHADOWPASSWDS) {
				pfile = sfile;
				sfile = NULL;
			}
		} while (ENABLE_FEATURE_SHADOWPASSWDS && pfile);

		if (do_deluser > 0) {
			/* Delete user from all groups */
			if (update_passwd(bb_path_group_file, NULL, NULL, name) == -1)
				return EXIT_FAILURE;

			if (ENABLE_DELGROUP) {
				/* "deluser USER" also should try to delete
				 * same-named group. IOW: do "delgroup USER"
				 */
// On debian deluser is a perl script that calls userdel.
// From man userdel:
//  If USERGROUPS_ENAB is defined to yes in /etc/login.defs, userdel will
//  delete the group with the same name as the user.
				do_deluser = -1;
				goto do_delgroup;
			}
		}
		return EXIT_SUCCESS;
	}
	/* Reached only if number of command line args is wrong */
	bb_show_usage();
}
