/*
 * Copyright 2015 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config UEVENT
//config:	bool "uevent (3.2 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	uevent is a netlink listener for kernel uevent notifications
//config:	sent via netlink. It is usually used for dynamic device creation.

//applet:IF_UEVENT(APPLET(uevent, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_UEVENT) += uevent.o

//usage:#define uevent_trivial_usage
//usage:       "[PROG [ARGS]]"
//usage:#define uevent_full_usage "\n\n"
//usage:       "uevent runs PROG for every netlink notification."
//usage:   "\n""PROG's environment contains data passed from the kernel."
//usage:   "\n""Typical usage (daemon for dynamic device node creation):"
//usage:   "\n""	# uevent mdev & mdev -s"

#include "libbb.h"
#include "common_bufsiz.h"
#include <linux/netlink.h>

#define BUFFER_SIZE 16*1024

#define env ((char **)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)
enum {
	MAX_ENV = COMMON_BUFSIZE / sizeof(char*) - 1,
	/* sizeof(env[0]) instead of sizeof(char*)
	 * makes gcc-6.3.0 emit "strict-aliasing" warning.
	 */
};

#ifndef SO_RCVBUFFORCE
#define SO_RCVBUFFORCE 33
#endif
enum { RCVBUF = 2 * 1024 * 1024 };

int uevent_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int uevent_main(int argc UNUSED_PARAM, char **argv)
{
	struct sockaddr_nl sa;
	int fd;

	INIT_G();

	argv++;

	// Subscribe for UEVENT kernel messages
	sa.nl_family = AF_NETLINK;
	sa.nl_pad = 0;
	sa.nl_pid = getpid();
	sa.nl_groups = 1 << 0;
	fd = xsocket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	xbind(fd, (struct sockaddr *) &sa, sizeof(sa));
	close_on_exec_on(fd);

	// Without a sufficiently big RCVBUF, a ton of simultaneous events
	// can trigger ENOBUFS on read, which is unrecoverable.
	// Reproducer:
	//	uevent mdev &
	// 	find /sys -name uevent -exec sh -c 'echo add >"{}"' ';'
	//
	// SO_RCVBUFFORCE (root only) can go above net.core.rmem_max sysctl
	setsockopt_SOL_SOCKET_int(fd, SO_RCVBUF,      RCVBUF);
	setsockopt_SOL_SOCKET_int(fd, SO_RCVBUFFORCE, RCVBUF);
	if (0) {
		int z;
		socklen_t zl = sizeof(z);
		getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &z, &zl);
		bb_error_msg("SO_RCVBUF:%d", z);
	}

	for (;;) {
		char *netbuf;
		char *s, *end;
		ssize_t len;
		int idx;

		// In many cases, a system sits for *days* waiting
		// for a new uevent notification to come in.
		// We use a fresh mmap so that buffer is not allocated
		// until kernel actually starts filling it.
		netbuf = mmap(NULL, BUFFER_SIZE,
					PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANON,
					/* ignored: */ -1, 0);
		if (netbuf == MAP_FAILED)
			bb_perror_msg_and_die("mmap");

		// Here we block, possibly for a very long time
		len = safe_read(fd, netbuf, BUFFER_SIZE - 1);
		if (len < 0)
			bb_perror_msg_and_die("read");
		end = netbuf + len;
		*end = '\0';

		// Each netlink message starts with "ACTION@/path"
		// (which we currently ignore),
		// followed by environment variables.
		if (!argv[0])
			putchar('\n');
		idx = 0;
		s = netbuf;
		while (s < end) {
			if (!argv[0])
				puts(s);
			if (strchr(s, '=') && idx < MAX_ENV)
				env[idx++] = s;
			s += strlen(s) + 1;
		}
		env[idx] = NULL;

		idx = 0;
		while (env[idx])
			putenv(env[idx++]);
		if (argv[0])
			spawn_and_wait(argv);
		idx = 0;
		while (env[idx])
			bb_unsetenv(env[idx++]);
		munmap(netbuf, BUFFER_SIZE);
	}

	return 0; // not reached
}
