/* vi: set sw=4 ts=4: */
/*
 * simple inotify daemon
 * reports filesystem changes via userspace agent
 *
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/*
 * Use as follows:
 * # inotifyd /user/space/agent dir/or/file/being/watched[:mask] ...
 *
 * When a filesystem event matching the specified mask is occurred on specified file (or directory)
 * a userspace agent is spawned and given the following parameters:
 * $1. actual event(s)
 * $2. file (or directory) name
 * $3. name of subfile (if any), in case of watching a directory
 *
 * E.g. inotifyd ./dev-watcher /dev:n
 *
 * ./dev-watcher can be, say:
 * #!/bin/sh
 * echo "We have new device in here! Hello, $3!"
 *
 * See below for mask names explanation.
 */
//config:config INOTIFYD
//config:	bool "inotifyd (3.5 kb)"
//config:	default n  # doesn't build on Knoppix 5
//config:	help
//config:	Simple inotify daemon. Reports filesystem changes. Requires
//config:	kernel >= 2.6.13

//applet:IF_INOTIFYD(APPLET(inotifyd, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_INOTIFYD) += inotifyd.o

//usage:#define inotifyd_trivial_usage
//usage:	"PROG FILE1[:MASK]..."
//usage:#define inotifyd_full_usage "\n\n"
//usage:       "Run PROG on filesystem changes."
//usage:     "\nWhen a filesystem event matching MASK occurs on FILEn,"
//usage:     "\nPROG ACTUAL_EVENTS FILEn [SUBFILE] is run."
//usage:     "\nIf PROG is -, events are sent to stdout."
//usage:     "\nEvents:"
//usage:     "\n	a	File is accessed"
//usage:     "\n	c	File is modified"
//usage:     "\n	e	Metadata changed"
//usage:     "\n	w	Writable file is closed"
//usage:     "\n	0	Unwritable file is closed"
//usage:     "\n	r	File is opened"
//usage:     "\n	D	File is deleted"
//usage:     "\n	M	File is moved"
//usage:     "\n	u	Backing fs is unmounted"
//usage:     "\n	o	Event queue overflowed"
//usage:     "\n	x	File can't be watched anymore"
//usage:     "\nIf watching a directory:"
//usage:     "\n	y	Subfile is moved into dir"
//usage:     "\n	m	Subfile is moved out of dir"
//usage:     "\n	n	Subfile is created"
//usage:     "\n	d	Subfile is deleted"
//usage:     "\n"
//usage:     "\ninotifyd waits for PROG to exit."
//usage:     "\nWhen x event happens for all FILEs, inotifyd exits."

#include "libbb.h"
#include "common_bufsiz.h"
#include <sys/inotify.h>

static const char mask_names[] ALIGN1 =
	"a"	// 0x00000001	File was accessed
	"c"	// 0x00000002	File was modified
	"e"	// 0x00000004	Metadata changed
	"w"	// 0x00000008	Writable file was closed
	"0"	// 0x00000010	Unwritable file closed
	"r"	// 0x00000020	File was opened
	"m"	// 0x00000040	File was moved from X
	"y"	// 0x00000080	File was moved to Y
	"n"	// 0x00000100	Subfile was created
	"d"	// 0x00000200	Subfile was deleted
	"D"	// 0x00000400	Self was deleted
	"M"	// 0x00000800	Self was moved
	"\0"	// 0x00001000   (unused)
	// Kernel events, always reported:
	"u"	// 0x00002000   Backing fs was unmounted
	"o"	// 0x00004000   Event queued overflowed
	"x"	// 0x00008000   File is no longer watched (usually deleted)
;
enum {
	MASK_BITS = sizeof(mask_names) - 1
};

int inotifyd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int inotifyd_main(int argc, char **argv)
{
	int n;
	unsigned mask;
	struct pollfd pfd;
	char **watches; // names of files being watched
	const char *args[5];

	// sanity check: agent and at least one watch must be given
	if (!argv[1] || !argv[2])
		bb_show_usage();

	argv++;
	// inotify_add_watch will number watched files
	// starting from 1, thus watches[0] is unimportant,
	// and 1st file name is watches[1].
	watches = argv;
	args[0] = *argv;
	args[4] = NULL;
	argc -= 2; // number of files we watch

	// open inotify
	pfd.fd = inotify_init();
	if (pfd.fd < 0)
		bb_perror_msg_and_die("no kernel support");

	// setup watches
	while (*++argv) {
		char *path = *argv;
		char *masks = strchr(path, ':');

		mask = 0x0fff; // assuming we want all non-kernel events
		// if mask is specified ->
		if (masks) {
			*masks = '\0'; // split path and mask
			// convert mask names to mask bitset
			mask = 0;
			while (*++masks) {
				const char *found;
				found = memchr(mask_names, *masks, MASK_BITS);
				if (found)
					mask |= (1 << (found - mask_names));
			}
		}
		// add watch
		n = inotify_add_watch(pfd.fd, path, mask);
		if (n < 0)
			bb_perror_msg_and_die("add watch (%s) failed", path);
		//bb_error_msg("added %d [%s]:%4X", n, path, mask);
	}

	// setup signals
	bb_signals(BB_FATAL_SIGS, record_signo);

	// do watch
	pfd.events = POLLIN;
	while (1) {
		int len;
		void *buf;
		struct inotify_event *ie;
 again:
		if (bb_got_signal)
			break;
		n = poll(&pfd, 1, -1);
		// Signal interrupted us?
		if (n < 0 && errno == EINTR)
			goto again;
		// Under Linux, above if() is not necessary.
		// Non-fatal signals, e.g. SIGCHLD, when set to SIG_DFL,
		// are not interrupting poll().
		// Thus we can just break if n <= 0 (see below),
		// because EINTR will happen only on SIGTERM et al.
		// But this might be not true under other Unixes,
		// and is generally way too subtle to depend on.
		if (n <= 0) // strange error?
			break;

		// read out all pending events
		// (NB: len must be int, not ssize_t or long!)
#define eventbuf bb_common_bufsiz1
		setup_common_bufsiz();
		xioctl(pfd.fd, FIONREAD, &len);
		ie = buf = (len <= COMMON_BUFSIZE) ? eventbuf : xmalloc(len);
		len = full_read(pfd.fd, buf, len);
		// process events. N.B. events may vary in length
		while (len > 0) {
			int i;
			// cache relevant events mask
			unsigned m = ie->mask & ((1 << MASK_BITS) - 1);
			if (m) {
				char events[MASK_BITS + 1];
				char *s = events;
				for (i = 0; i < MASK_BITS; ++i, m >>= 1) {
					if ((m & 1) && (mask_names[i] != '\0'))
						*s++ = mask_names[i];
				}
				*s = '\0';
				if (LONE_CHAR(args[0], '-')) {
					/* "inotifyd - FILE": built-in echo */
					printf(ie->len ? "%s\t%s\t%s\n" : "%s\t%s\n", events,
							watches[ie->wd],
							ie->name);
					fflush(stdout);
				} else {
//					bb_error_msg("exec %s %08X\t%s\t%s\t%s", args[0],
//						ie->mask, events, watches[ie->wd], ie->len ? ie->name : "");
					args[1] = events;
					args[2] = watches[ie->wd];
					args[3] = ie->len ? ie->name : NULL;
					spawn_and_wait((char **)args);
				}
				// we are done if all files got final x event
				if (ie->mask & 0x8000) {
					if (--argc <= 0)
						goto done;
					inotify_rm_watch(pfd.fd, ie->wd);
				}
			}
			// next event
			i = sizeof(struct inotify_event) + ie->len;
			len -= i;
			ie = (void*)((char*)ie + i);
		}
		if (eventbuf != buf)
			free(buf);
	} // while (1)
 done:
	return bb_got_signal;
}
