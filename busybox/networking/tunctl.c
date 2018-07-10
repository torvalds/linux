/* vi: set sw=4 ts=4: */
/*
 * tun devices controller
 *
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Original code:
 *      Jeff Dike
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config TUNCTL
//config:	bool "tunctl (6.4 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	tunctl creates or deletes tun devices.
//config:
//config:config FEATURE_TUNCTL_UG
//config:	bool "Support owner:group assignment"
//config:	default y
//config:	depends on TUNCTL
//config:	help
//config:	Allow to specify owner and group of newly created interface.
//config:	340 bytes of pure bloat. Say no here.

//applet:IF_TUNCTL(APPLET_NOEXEC(tunctl, tunctl, BB_DIR_SBIN, BB_SUID_DROP, tunctl))

//kbuild:lib-$(CONFIG_TUNCTL) += tunctl.o

//usage:#define tunctl_trivial_usage
//usage:       "[-f device] ([-t name] | -d name)" IF_FEATURE_TUNCTL_UG(" [-u owner] [-g group] [-b]")
//usage:#define tunctl_full_usage "\n\n"
//usage:       "Create or delete tun interfaces\n"
//usage:     "\n	-f name		tun device (/dev/net/tun)"
//usage:     "\n	-t name		Create iface 'name'"
//usage:     "\n	-d name		Delete iface 'name'"
//usage:	IF_FEATURE_TUNCTL_UG(
//usage:     "\n	-u owner	Set iface owner"
//usage:     "\n	-g group	Set iface group"
//usage:     "\n	-b		Brief output"
//usage:	)
//usage:
//usage:#define tunctl_example_usage
//usage:       "# tunctl\n"
//usage:       "# tunctl -d tun0\n"

#include <netinet/in.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include "libbb.h"

/* TUNSETGROUP appeared in 2.6.23 */
#ifndef TUNSETGROUP
#define TUNSETGROUP _IOW('T', 206, int)
#endif

#define IOCTL(a, b, c) ioctl_or_perror_and_die(a, b, c, NULL)

#if 1

int tunctl_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int tunctl_main(int argc UNUSED_PARAM, char **argv)
{
	struct ifreq ifr;
	int fd;
	const char *opt_name = "tap%d";
	const char *opt_device = "/dev/net/tun";
#if ENABLE_FEATURE_TUNCTL_UG
	const char *opt_user, *opt_group;
	long user = -1, group = -1;
#endif
	unsigned opts;

	enum {
		OPT_f = 1 << 0, // control device name (/dev/net/tun)
		OPT_t = 1 << 1, // create named interface
		OPT_d = 1 << 2, // delete named interface
#if ENABLE_FEATURE_TUNCTL_UG
		OPT_u = 1 << 3, // set new interface owner
		OPT_g = 1 << 4, // set new interface group
		OPT_b = 1 << 5, // brief output
#endif
	};

	opts = getopt32(argv, "^"
			"f:t:d:" IF_FEATURE_TUNCTL_UG("u:g:b")
			"\0"
			"=0:t--d:d--t", // no arguments; t ^ d
			&opt_device, &opt_name, &opt_name
			IF_FEATURE_TUNCTL_UG(, &opt_user, &opt_group)
	);

	// select device
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strncpy_IFNAMSIZ(ifr.ifr_name, opt_name);

	// open device
	fd = xopen(opt_device, O_RDWR);
	IOCTL(fd, TUNSETIFF, (void *)&ifr);

	// delete?
	if (opts & OPT_d) {
		IOCTL(fd, TUNSETPERSIST, (void *)(uintptr_t)0);
		printf("Set '%s' nonpersistent\n", ifr.ifr_name);
		return EXIT_SUCCESS;
	}

	// create
#if ENABLE_FEATURE_TUNCTL_UG
	if (opts & OPT_g) {
		group = xgroup2gid(opt_group);
		IOCTL(fd, TUNSETGROUP, (void *)(uintptr_t)group);
	} else
		user = geteuid();
	if (opts & OPT_u)
		user = xuname2uid(opt_user);
	IOCTL(fd, TUNSETOWNER, (void *)(uintptr_t)user);
#endif
	IOCTL(fd, TUNSETPERSIST, (void *)(uintptr_t)1);

	// show info
#if ENABLE_FEATURE_TUNCTL_UG
	if (opts & OPT_b) {
		puts(ifr.ifr_name);
	} else {
		printf("Set '%s' %spersistent", ifr.ifr_name, "");
		printf(" and owned by uid %ld", user);
		if (group != -1)
			printf(" gid %ld", group);
		bb_putchar('\n');
	}
#else
	puts(ifr.ifr_name);
#endif
	return EXIT_SUCCESS;
}

#else

/* -210 bytes: */

int tunctl_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int tunctl_main(int argc UNUSED_PARAM, char **argv)
{
	struct ifreq ifr;
	int fd;
	const char *opt_name = "tap%d";
	const char *opt_device = "/dev/net/tun";
	unsigned opts;

	enum {
		OPT_f = 1 << 0, // control device name (/dev/net/tun)
		OPT_t = 1 << 1, // create named interface
		OPT_d = 1 << 2, // delete named interface
	};

	opts = getopt32(argv, "^"
			"f:t:d:u:g:b" // u, g, b accepted and ignored
			"\0"
			"=0:t--d:d--t", // no arguments; t ^ d
			&opt_device, &opt_name, &opt_name, NULL, NULL
	);

	// set interface name
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strncpy_IFNAMSIZ(ifr.ifr_name, opt_name);

	// open device
	fd = xopen(opt_device, O_RDWR);
	IOCTL(fd, TUNSETIFF, (void *)&ifr);

	// create or delete interface
	IOCTL(fd, TUNSETPERSIST, (void *)(uintptr_t)(0 == (opts & OPT_d)));

	return EXIT_SUCCESS;
}

#endif
