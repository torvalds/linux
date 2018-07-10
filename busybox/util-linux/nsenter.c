/* vi: set sw=4 ts=4: */
/*
 * Mini nsenter implementation for busybox.
 *
 * Copyright (C) 2016 by Bartosz Golaszewski <bartekgola@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config NSENTER
//config:	bool "nsenter (8.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Run program with namespaces of other processes.

//applet:IF_NSENTER(APPLET(nsenter, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_NSENTER) += nsenter.o

//usage:#define nsenter_trivial_usage
//usage:       "[OPTIONS] [PROG [ARGS]]"
//usage:#define nsenter_full_usage "\n"
//usage:     "\n	-t PID		Target process to get namespaces from"
//usage:     "\n	-m[FILE]	Enter mount namespace"
//usage:     "\n	-u[FILE]	Enter UTS namespace (hostname etc)"
//usage:     "\n	-i[FILE]	Enter System V IPC namespace"
//usage:     "\n	-n[FILE]	Enter network namespace"
//usage:     "\n	-p[FILE]	Enter pid namespace"
//usage:     "\n	-U[FILE]	Enter user namespace"
//usage:     "\n	-S UID		Set uid in entered namespace"
//usage:     "\n	-G GID		Set gid in entered namespace"
//usage:	IF_LONG_OPTS(
//usage:     "\n	--preserve-credentials	Don't touch uids or gids"
//usage:	)
//usage:     "\n	-r[DIR]		Set root directory"
//usage:     "\n	-w[DIR]		Set working directory"
//usage:     "\n	-F		Don't fork before exec'ing PROG"

#include <sched.h>
#ifndef CLONE_NEWUTS
# define CLONE_NEWUTS  0x04000000
#endif
#ifndef CLONE_NEWIPC
# define CLONE_NEWIPC  0x08000000
#endif
#ifndef CLONE_NEWUSER
# define CLONE_NEWUSER 0x10000000
#endif
#ifndef CLONE_NEWPID
# define CLONE_NEWPID  0x20000000
#endif
#ifndef CLONE_NEWNET
# define CLONE_NEWNET  0x40000000
#endif

#include "libbb.h"

struct namespace_descr {
	int flag;		/* value passed to setns() */
	char ns_nsfile8[8];	/* "ns/" + namespace file in process' procfs entry */
};

struct namespace_ctx {
	char *path;		/* optional path to a custom ns file */
	int fd;			/* opened namespace file descriptor */
};

enum {
	OPT_user	= 1 << 0,
	OPT_ipc		= 1 << 1,
	OPT_uts		= 1 << 2,
	OPT_network	= 1 << 3,
	OPT_pid		= 1 << 4,
	OPT_mount	= 1 << 5,
	OPT_target	= 1 << 6,
	OPT_setuid	= 1 << 7,
	OPT_setgid	= 1 << 8,
	OPT_root	= 1 << 9,
	OPT_wd		= 1 << 10,
	OPT_nofork	= 1 << 11,
	OPT_prescred	= (1 << 12) * ENABLE_LONG_OPTS,
};
enum {
	NS_USR_POS = 0,
	NS_IPC_POS,
	NS_UTS_POS,
	NS_NET_POS,
	NS_PID_POS,
	NS_MNT_POS,
	NS_COUNT,
};
/*
 * The order is significant in nsenter.
 * The user namespace comes first, so that it is entered first.
 * This gives an unprivileged user the potential to enter other namespaces.
 */
static const struct namespace_descr ns_list[] = {
	{ CLONE_NEWUSER, "ns/user", },
	{ CLONE_NEWIPC,  "ns/ipc",  },
	{ CLONE_NEWUTS,  "ns/uts",  },
	{ CLONE_NEWNET,  "ns/net",  },
	{ CLONE_NEWPID,  "ns/pid",  },
	{ CLONE_NEWNS,   "ns/mnt",  },
};
/*
 * Upstream nsenter doesn't support the short option for --preserve-credentials
 */
static const char opt_str[] ALIGN1 = "U::i::u::n::p::m::""t:+S:+G:+r::w::F";

#if ENABLE_LONG_OPTS
static const char nsenter_longopts[] ALIGN1 =
	"user\0"			Optional_argument	"U"
	"ipc\0"				Optional_argument	"i"
	"uts\0"				Optional_argument	"u"
	"net\0"				Optional_argument	"n"
	"pid\0"				Optional_argument	"p"
	"mount\0"			Optional_argument	"m"
	"target\0"			Required_argument	"t"
	"setuid\0"			Required_argument	"S"
	"setgid\0"			Required_argument	"G"
	"root\0"			Optional_argument	"r"
	"wd\0"				Optional_argument	"w"
	"no-fork\0"			No_argument		"F"
	"preserve-credentials\0"	No_argument		"\xff"
	;
#endif

/*
 * Open a file and return the new descriptor. If a full path is provided in
 * fs_path, then the file to which it points is opened. Otherwise (fd_path is
 * NULL) the routine builds a path to a procfs file using the following
 * template: '/proc/<target_pid>/<target_file>'.
 */
static int open_by_path_or_target(const char *path,
				  pid_t target_pid, const char *target_file)
{
	char proc_path_buf[sizeof("/proc/%u/1234567890") + sizeof(int)*3];

	if (!path) {
		if (target_pid == 0) {
			/* Example:
			 * "nsenter -p PROG" - neither -pFILE nor -tPID given.
			 */
			bb_show_usage();
		}
		snprintf(proc_path_buf, sizeof(proc_path_buf),
			 "/proc/%u/%s", (unsigned)target_pid, target_file);
		path = proc_path_buf;
	}

	return xopen(path, O_RDONLY);
}

int nsenter_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int nsenter_main(int argc UNUSED_PARAM, char **argv)
{
	int i;
	unsigned int opts;
	const char *root_dir_str = NULL;
	const char *wd_str = NULL;
	struct namespace_ctx ns_ctx_list[NS_COUNT];
	int setgroups_failed;
	int root_fd, wd_fd;
	int target_pid = 0;
	int uid = 0;
	int gid = 0;

	memset(ns_ctx_list, 0, sizeof(ns_ctx_list));

	opts = getopt32long(argv, opt_str, nsenter_longopts,
			&ns_ctx_list[NS_USR_POS].path,
			&ns_ctx_list[NS_IPC_POS].path,
			&ns_ctx_list[NS_UTS_POS].path,
			&ns_ctx_list[NS_NET_POS].path,
			&ns_ctx_list[NS_PID_POS].path,
			&ns_ctx_list[NS_MNT_POS].path,
			&target_pid, &uid, &gid,
			&root_dir_str, &wd_str
	);
	argv += optind;

	root_fd = wd_fd = -1;
	if (opts & OPT_root)
		root_fd = open_by_path_or_target(root_dir_str,
						 target_pid, "root");
	if (opts & OPT_wd)
		wd_fd = open_by_path_or_target(wd_str, target_pid, "cwd");

	for (i = 0; i < NS_COUNT; i++) {
		const struct namespace_descr *ns = &ns_list[i];
		struct namespace_ctx *ns_ctx = &ns_ctx_list[i];

		ns_ctx->fd = -1;
		if (opts & (1 << i))
			ns_ctx->fd = open_by_path_or_target(ns_ctx->path,
					target_pid, ns->ns_nsfile8);
	}

	/*
	 * Entering the user namespace without --preserve-credentials implies
	 * --setuid & --setgid and clearing root's groups.
	 */
	setgroups_failed = 0;
	if ((opts & OPT_user) && !(opts & OPT_prescred)) {
		opts |= (OPT_setuid | OPT_setgid);
		/*
		 * We call setgroups() before and after setns() and only
		 * bail-out if it fails twice.
		 */
		setgroups_failed = (setgroups(0, NULL) < 0);
	}

	for (i = 0; i < NS_COUNT; i++) {
		const struct namespace_descr *ns = &ns_list[i];
		struct namespace_ctx *ns_ctx = &ns_ctx_list[i];

		if (ns_ctx->fd < 0)
			continue;
		if (setns(ns_ctx->fd, ns->flag)) {
			bb_perror_msg_and_die(
				"setns(): can't reassociate to namespace '%s'",
				ns->ns_nsfile8 + 3 /* skip over "ns/" */
			);
		}
		close(ns_ctx->fd); /* should close fds, to not confuse exec'ed PROG */
		/*ns_ctx->fd = -1;*/
	}

	if (root_fd >= 0) {
		if (wd_fd < 0) {
			/*
			 * Save the current working directory if we're not
			 * changing it.
			 */
			wd_fd = xopen(".", O_RDONLY);
		}
		xfchdir(root_fd);
		xchroot(".");
		close(root_fd);
		/*root_fd = -1;*/
	}

	if (wd_fd >= 0) {
		xfchdir(wd_fd);
		close(wd_fd);
		/*wd_fd = -1;*/
	}

	/*
	 * Entering the pid namespace implies forking unless it's been
	 * explicitly requested by the user not to.
	 */
	if (!(opts & OPT_nofork) && (opts & OPT_pid)) {
		xvfork_parent_waits_and_exits();
		/* Child continues */
	}

	if (opts & OPT_setgid) {
		if (setgroups(0, NULL) < 0 && setgroups_failed)
			bb_perror_msg_and_die("setgroups");
		xsetgid(gid);
	}
	if (opts & OPT_setuid)
		xsetuid(uid);

	exec_prog_or_SHELL(argv);
}
