/* vi: set sw=4 ts=4: */
/*
 * addgroup - add groups to /etc/group and /etc/gshadow
 *
 * Copyright (C) 1999 by Lineo, inc. and John Beppu
 * Copyright (C) 1999,2000,2001 by John Beppu <beppu@codepoet.org>
 * Copyright (C) 2007 by Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config ADDGROUP
//config:	bool "addgroup (8.2 kb)"
//config:	default y
//config:	select LONG_OPTS
//config:	help
//config:	Utility for creating a new group account.
//config:
//config:config FEATURE_ADDUSER_TO_GROUP
//config:	bool "Support adding users to groups"
//config:	default y
//config:	depends on ADDGROUP
//config:	help
//config:	If called with two non-option arguments,
//config:	addgroup will add an existing user to an
//config:	existing group.

//applet:IF_ADDGROUP(APPLET_NOEXEC(addgroup, addgroup, BB_DIR_USR_SBIN, BB_SUID_DROP, addgroup))

//kbuild:lib-$(CONFIG_ADDGROUP) += addgroup.o

//usage:#define addgroup_trivial_usage
//usage:       "[-g GID] [-S] " IF_FEATURE_ADDUSER_TO_GROUP("[USER] ") "GROUP"
//usage:#define addgroup_full_usage "\n\n"
//usage:       "Add a group" IF_FEATURE_ADDUSER_TO_GROUP(" or add a user to a group") "\n"
//usage:     "\n	-g GID	Group id"
//usage:     "\n	-S	Create a system group"

#include "libbb.h"

#if CONFIG_LAST_SYSTEM_ID < CONFIG_FIRST_SYSTEM_ID
#error Bad LAST_SYSTEM_ID or FIRST_SYSTEM_ID in .config
#endif
#if CONFIG_LAST_ID < CONFIG_LAST_SYSTEM_ID
#error Bad LAST_ID or LAST_SYSTEM_ID in .config
#endif

#define OPT_GID                       (1 << 0)
#define OPT_SYSTEM_ACCOUNT            (1 << 1)

static void xgroup_study(struct group *g)
{
	unsigned max = CONFIG_LAST_ID;

	/* Make sure gr_name is unused */
	if (getgrnam(g->gr_name)) {
		bb_error_msg_and_die("%s '%s' in use", "group", g->gr_name);
		/* these format strings are reused in adduser and addgroup */
	}

	/* if a specific gid is requested, the --system switch and */
	/* min and max values are overridden, and the range of valid */
	/* gid values is set to [0, INT_MAX] */
	if (!(option_mask32 & OPT_GID)) {
		if (option_mask32 & OPT_SYSTEM_ACCOUNT) {
			g->gr_gid = CONFIG_FIRST_SYSTEM_ID;
			max = CONFIG_LAST_SYSTEM_ID;
		} else {
			g->gr_gid = CONFIG_LAST_SYSTEM_ID + 1;
		}
	}
	/* Check if the desired gid is free
	 * or find the first free one */
	while (1) {
		if (!getgrgid(g->gr_gid)) {
			return; /* found free group: return */
		}
		if (option_mask32 & OPT_GID) {
			/* -g N, cannot pick gid other than N: error */
			bb_error_msg_and_die("%s '%s' in use", "gid", itoa(g->gr_gid));
			/* this format strings is reused in adduser and addgroup */
		}
		if (g->gr_gid == max) {
			/* overflowed: error */
			bb_error_msg_and_die("no %cids left", 'g');
			/* this format string is reused in adduser and addgroup */
		}
		g->gr_gid++;
	}
}

/* append a new user to the passwd file */
static void new_group(char *group, gid_t gid)
{
	struct group gr;
	char *p;

	/* make sure gid and group haven't already been allocated */
	gr.gr_gid = gid;
	gr.gr_name = group;
	xgroup_study(&gr);

	/* add entry to group */
	p = xasprintf("x:%u:", (unsigned) gr.gr_gid);
	if (update_passwd(bb_path_group_file, group, p, NULL) < 0)
		exit(EXIT_FAILURE);
	if (ENABLE_FEATURE_CLEAN_UP)
		free(p);
#if ENABLE_FEATURE_SHADOWPASSWDS
	/* /etc/gshadow fields:
	 * 1. Group name.
	 * 2. Encrypted password.
	 *    If set, non-members of the group can join the group
	 *    by typing the password for that group using the newgrp command.
	 *    If the value is of this field ! then no user is allowed
	 *    to access the group using the newgrp command. A value of !!
	 *    is treated the same as a value of ! only it indicates
	 *    that a password has never been set before. If the value is null,
	 *    only group members can log into the group.
	 * 3. Group administrators (comma delimited list).
	 *    Group members listed here can add or remove group members
	 *    using the gpasswd command.
	 * 4. Group members (comma delimited list).
	 */
	/* Ignore errors: if file is missing we assume admin doesn't want it */
	update_passwd(bb_path_gshadow_file, group, "!::", NULL);
#endif
}

//FIXME: upstream addgroup has no short options! NOT COMPATIBLE!
static const char addgroup_longopts[] ALIGN1 =
		"gid\0"                 Required_argument "g"
		"system\0"              No_argument       "S"
		;

/*
 * addgroup will take a login_name as its first parameter.
 *
 * gid can be customized via command-line parameters.
 * If called with two non-option arguments, addgroup
 * will add an existing user to an existing group.
 */
int addgroup_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int addgroup_main(int argc UNUSED_PARAM, char **argv)
{
#if ENABLE_FEATURE_ADDUSER_TO_GROUP
	unsigned opts;
#endif
	const char *gid = "0";

	/* need to be root */
	if (geteuid()) {
		bb_error_msg_and_die(bb_msg_perm_denied_are_you_root);
	}
	/* Syntax:
	 *  addgroup group
	 *  addgroup --gid num group
	 *  addgroup user group
	 * Check for min, max and missing args */
#if ENABLE_FEATURE_ADDUSER_TO_GROUP
	opts =
#endif
	getopt32long(argv, "^" "g:S" "\0" "-1:?2", addgroup_longopts,
				&gid
	);
	/* move past the commandline options */
	argv += optind;
	//argc -= optind;

#if ENABLE_FEATURE_ADDUSER_TO_GROUP
	if (argv[1]) {
		struct group *gr;

		if (opts & OPT_GID) {
			/* -g was there, but "addgroup -g num user group"
			 * is a no-no */
			bb_show_usage();
		}

		/* check if group and user exist */
		xuname2uid(argv[0]); /* unknown user: exit */
		gr = xgetgrnam(argv[1]); /* unknown group: exit */
		/* check if user is already in this group */
		for (; *(gr->gr_mem) != NULL; (gr->gr_mem)++) {
			if (strcmp(argv[0], *(gr->gr_mem)) == 0) {
				/* user is already in group: do nothing */
				return EXIT_SUCCESS;
			}
		}
		if (update_passwd(bb_path_group_file, argv[1], NULL, argv[0]) < 0) {
			return EXIT_FAILURE;
		}
# if ENABLE_FEATURE_SHADOWPASSWDS
		update_passwd(bb_path_gshadow_file, argv[1], NULL, argv[0]);
# endif
	} else
#endif /* ENABLE_FEATURE_ADDUSER_TO_GROUP */
	{
		die_if_bad_username(argv[0]);
		new_group(argv[0], xatou_range(gid, 0, CONFIG_LAST_ID));
	}
	/* Reached only on success */
	return EXIT_SUCCESS;
}
