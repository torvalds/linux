/* vi: set sw=4 ts=4: */
/*
 * Mini unshare implementation for busybox.
 *
 * Copyright (C) 2016 by Bartosz Golaszewski <bartekgola@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config UNSHARE
//config:	bool "unshare (9.2 kb)"
//config:	default y
//config:	depends on !NOMMU
//config:	select PLATFORM_LINUX
//config:	select LONG_OPTS
//config:	help
//config:	Run program with some namespaces unshared from parent.

// needs LONG_OPTS: it is awkward to exclude code which handles --propagation
// and --setgroups based on LONG_OPTS, so instead applet requires LONG_OPTS.
// depends on !NOMMU: we need fork()

//applet:IF_UNSHARE(APPLET(unshare, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_UNSHARE) += unshare.o

//usage:#define unshare_trivial_usage
//usage:       "[OPTIONS] [PROG [ARGS]]"
//usage:#define unshare_full_usage "\n"
//usage:     "\n	-m,--mount[=FILE]	Unshare mount namespace"
//usage:     "\n	-u,--uts[=FILE]		Unshare UTS namespace (hostname etc.)"
//usage:     "\n	-i,--ipc[=FILE]		Unshare System V IPC namespace"
//usage:     "\n	-n,--net[=FILE]		Unshare network namespace"
//usage:     "\n	-p,--pid[=FILE]		Unshare PID namespace"
//usage:     "\n	-U,--user[=FILE]	Unshare user namespace"
//usage:     "\n	-f,--fork		Fork before execing PROG"
//usage:     "\n	-r,--map-root-user	Map current user to root (implies -U)"
//usage:     "\n	--mount-proc[=DIR]	Mount /proc filesystem first (implies -m)"
//usage:     "\n	--propagation slave|shared|private|unchanged"
//usage:     "\n				Modify mount propagation in mount namespace"
//usage:     "\n	--setgroups allow|deny	Control the setgroups syscall in user namespaces"

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

#include <sys/mount.h>
#ifndef MS_REC
# define MS_REC     (1 << 14)
#endif
#ifndef MS_PRIVATE
# define MS_PRIVATE (1 << 18)
#endif
#ifndef MS_SLAVE
# define MS_SLAVE   (1 << 19)
#endif
#ifndef MS_SHARED
# define MS_SHARED  (1 << 20)
#endif

#include "libbb.h"

static void mount_or_die(const char *source, const char *target,
                 const char *fstype, unsigned long mountflags)
{
	if (mount(source, target, fstype, mountflags, NULL)) {
		bb_perror_msg_and_die("can't mount %s on %s (flags:0x%lx)",
			source, target, mountflags);
		/* fstype is always either NULL or "proc".
		 * "proc" is only used to mount /proc.
		 * No need to clutter up error message with fstype,
		 * it is easily deductible.
		 */
	}
}

#define PATH_PROC_SETGROUPS	"/proc/self/setgroups"
#define PATH_PROC_UIDMAP	"/proc/self/uid_map"
#define PATH_PROC_GIDMAP	"/proc/self/gid_map"

struct namespace_descr {
	int flag;
	const char nsfile4[4];
};

struct namespace_ctx {
	char *path;
};

enum {
	OPT_mount	= 1 << 0,
	OPT_uts		= 1 << 1,
	OPT_ipc		= 1 << 2,
	OPT_net		= 1 << 3,
	OPT_pid		= 1 << 4,
	OPT_user	= 1 << 5, /* OPT_user, NS_USR_POS, and ns_list[] index must match! */
	OPT_fork	= 1 << 6,
	OPT_map_root	= 1 << 7,
	OPT_mount_proc	= 1 << 8,
	OPT_propagation	= 1 << 9,
	OPT_setgroups	= 1 << 10,
};
enum {
	NS_MNT_POS = 0,
	NS_UTS_POS,
	NS_IPC_POS,
	NS_NET_POS,
	NS_PID_POS,
	NS_USR_POS, /* OPT_user, NS_USR_POS, and ns_list[] index must match! */
	NS_COUNT,
};
static const struct namespace_descr ns_list[] = {
	{ CLONE_NEWNS,   "mnt"  },
	{ CLONE_NEWUTS,  "uts"  },
	{ CLONE_NEWIPC,  "ipc"  },
	{ CLONE_NEWNET,  "net"  },
	{ CLONE_NEWPID,  "pid"  },
	{ CLONE_NEWUSER, "user" }, /* OPT_user, NS_USR_POS, and ns_list[] index must match! */
};

/*
 * Upstream unshare doesn't support short options for --mount-proc,
 * --propagation, --setgroups.
 * Optional arguments (namespace mountpoints) exist only for long opts,
 * we are forced to use "fake" letters for them.
 * '+': stop at first non-option.
 */
#define OPT_STR "+muinpU""fr""\xfd::""\xfe:""\xff:"
static const char unshare_longopts[] ALIGN1 =
	"mount\0"		Optional_argument	"\xf0"
	"uts\0"			Optional_argument	"\xf1"
	"ipc\0"			Optional_argument	"\xf2"
	"net\0"			Optional_argument	"\xf3"
	"pid\0"			Optional_argument	"\xf4"
	"user\0"		Optional_argument	"\xf5"
	"fork\0"		No_argument		"f"
	"map-root-user\0"	No_argument		"r"
	"mount-proc\0"		Optional_argument	"\xfd"
	"propagation\0"		Required_argument	"\xfe"
	"setgroups\0"		Required_argument	"\xff"
;

/* Ugly-looking string reuse trick */
#define PRIVATE_STR   "private\0""unchanged\0""shared\0""slave\0"
#define PRIVATE_UNCHANGED_SHARED_SLAVE   PRIVATE_STR

static unsigned long parse_propagation(const char *prop_str)
{
	int i = index_in_strings(PRIVATE_UNCHANGED_SHARED_SLAVE, prop_str);
	if (i < 0)
		bb_error_msg_and_die("unrecognized: --%s=%s", "propagation", prop_str);
	if (i == 0)
		return MS_REC | MS_PRIVATE;
	if (i == 1)
		return 0;
	if (i == 2)
		return MS_REC | MS_SHARED;
	return MS_REC | MS_SLAVE;
}

static void mount_namespaces(pid_t pid, struct namespace_ctx *ns_ctx_list)
{
	const struct namespace_descr *ns;
	struct namespace_ctx *ns_ctx;
	int i;

	for (i = 0; i < NS_COUNT; i++) {
		char nsf[sizeof("/proc/%u/ns/AAAA") + sizeof(int)*3];

		ns = &ns_list[i];
		ns_ctx = &ns_ctx_list[i];
		if (!ns_ctx->path)
			continue;
		sprintf(nsf, "/proc/%u/ns/%.4s", (unsigned)pid, ns->nsfile4);
		mount_or_die(nsf, ns_ctx->path, NULL, MS_BIND);
	}
}

int unshare_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int unshare_main(int argc UNUSED_PARAM, char **argv)
{
	int i;
	unsigned int opts;
	int unsflags;
	uintptr_t need_mount;
	const char *proc_mnt_target;
	const char *prop_str;
	const char *setgrp_str;
	unsigned long prop_flags;
	uid_t reuid = geteuid();
	gid_t regid = getegid();
	struct fd_pair fdp;
	pid_t child = child; /* for compiler */
	struct namespace_ctx ns_ctx_list[NS_COUNT];

	memset(ns_ctx_list, 0, sizeof(ns_ctx_list));
	proc_mnt_target = "/proc";
	prop_str = PRIVATE_STR;
	setgrp_str = NULL;

	opts = getopt32long(argv, "^" OPT_STR "\0"
		"\xf0""m" /* long opts (via their "fake chars") imply short opts */
		":\xf1""u"
		":\xf2""i"
		":\xf3""n"
		":\xf4""p"
		":\xf5""U"
		":rU"	   /* --map-root-user or -r implies -U */
		":\xfd""m" /* --mount-proc implies -m */
		, unshare_longopts,
		&proc_mnt_target, &prop_str, &setgrp_str,
		&ns_ctx_list[NS_MNT_POS].path,
		&ns_ctx_list[NS_UTS_POS].path,
		&ns_ctx_list[NS_IPC_POS].path,
		&ns_ctx_list[NS_NET_POS].path,
		&ns_ctx_list[NS_PID_POS].path,
		&ns_ctx_list[NS_USR_POS].path
	);
	argv += optind;
	//bb_error_msg("opts:0x%x", opts);
	//bb_error_msg("mount:%s", ns_ctx_list[NS_MNT_POS].path);
	//bb_error_msg("proc_mnt_target:%s", proc_mnt_target);
	//bb_error_msg("prop_str:%s", prop_str);
	//bb_error_msg("setgrp_str:%s", setgrp_str);
	//exit(1);

	if (setgrp_str) {
		if (strcmp(setgrp_str, "allow") == 0) {
			if (opts & OPT_map_root) {
				bb_error_msg_and_die(
					"--setgroups=allow and --map-root-user "
					"are mutually exclusive"
				);
			}
		} else {
			/* It's not "allow", must be "deny" */
			if (strcmp(setgrp_str, "deny") != 0)
				bb_error_msg_and_die("unrecognized: --%s=%s",
					"setgroups", setgrp_str);
		}
	}

	unsflags = 0;
	need_mount = 0;
	for (i = 0; i < NS_COUNT; i++) {
		const struct namespace_descr *ns = &ns_list[i];
		struct namespace_ctx *ns_ctx = &ns_ctx_list[i];

		if (opts & (1 << i))
			unsflags |= ns->flag;

		need_mount |= (uintptr_t)(ns_ctx->path);
	}
	/* need_mount != 0 if at least one FILE was given */

	prop_flags = MS_REC | MS_PRIVATE;
	/* Silently ignore --propagation if --mount is not requested. */
	if (opts & OPT_mount)
		prop_flags = parse_propagation(prop_str);

	/*
	 * Special case: if we were requested to unshare the mount namespace
	 * AND to make any namespace persistent (by bind mounting it) we need
	 * to spawn a child process which will wait for the parent to call
	 * unshare(), then mount parent's namespaces while still in the
	 * previous namespace.
	 */
	fdp.wr = -1;
	if (need_mount && (opts & OPT_mount)) {
		/*
		 * Can't use getppid() in child, as we can be unsharing the
		 * pid namespace.
		 */
		pid_t ppid = getpid();

		xpiped_pair(fdp);

		child = xfork();
		if (child == 0) {
			/* Child */
			close(fdp.wr);

			/* Wait until parent calls unshare() */
			read(fdp.rd, ns_ctx_list, 1); /* ...using bogus buffer */
			/*close(fdp.rd);*/

			/* Mount parent's unshared namespaces. */
			mount_namespaces(ppid, ns_ctx_list);
			return EXIT_SUCCESS;
		}
		/* Parent continues */
	}

	if (unshare(unsflags) != 0)
		bb_perror_msg_and_die("unshare(0x%x)", unsflags);

	if (fdp.wr >= 0) {
		close(fdp.wr); /* Release child */
		close(fdp.rd); /* should close fd, to not confuse exec'ed PROG */
	}

	if (need_mount) {
		/* Wait for the child to finish mounting the namespaces. */
		if (opts & OPT_mount) {
			int exit_status = wait_for_exitstatus(child);
			if (WIFEXITED(exit_status) &&
			    WEXITSTATUS(exit_status) != EXIT_SUCCESS)
				return WEXITSTATUS(exit_status);
		} else {
			/*
			 * Regular way - we were requested to mount some other
			 * namespaces: mount them after the call to unshare().
			 */
			mount_namespaces(getpid(), ns_ctx_list);
		}
	}

	/*
	 * When we're unsharing the pid namespace, it's not the process that
	 * calls unshare() that is put into the new namespace, but its first
	 * child. The user may want to use this option to spawn a new process
	 * that'll become PID 1 in this new namespace.
	 */
	if (opts & OPT_fork) {
		xvfork_parent_waits_and_exits();
		/* Child continues */
	}

	if (opts & OPT_map_root) {
		char uidmap_buf[sizeof("0 %u 1") + sizeof(int)*3];

		/*
		 * Since Linux 3.19 unprivileged writing of /proc/self/gid_map
		 * has been disabled unless /proc/self/setgroups is written
		 * first to permanently disable the ability to call setgroups
		 * in that user namespace.
		 */
		xopen_xwrite_close(PATH_PROC_SETGROUPS, "deny");
		sprintf(uidmap_buf, "0 %u 1", (unsigned)reuid);
		xopen_xwrite_close(PATH_PROC_UIDMAP, uidmap_buf);
		sprintf(uidmap_buf, "0 %u 1", (unsigned)regid);
		xopen_xwrite_close(PATH_PROC_GIDMAP, uidmap_buf);
	} else
	if (setgrp_str) {
		/* Write "allow" or "deny" */
		xopen_xwrite_close(PATH_PROC_SETGROUPS, setgrp_str);
	}

	if (opts & OPT_mount) {
		mount_or_die("none", "/", NULL, prop_flags);
	}

	if (opts & OPT_mount_proc) {
		/*
		 * When creating a new pid namespace, we might want the pid
		 * subdirectories in /proc to remain consistent with the new
		 * process IDs. Without --mount-proc the pids in /proc would
		 * still reflect the old pid namespace. This is why we make
		 * /proc private here and then do a fresh mount.
		 */
		mount_or_die("none", proc_mnt_target, NULL, MS_PRIVATE | MS_REC);
		mount_or_die("proc", proc_mnt_target, "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV);
	}

	exec_prog_or_SHELL(argv);
}
