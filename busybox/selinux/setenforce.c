/*
 * setenforce
 *
 * Based on libselinux 1.33.1
 * Port to BusyBox  Hiroshi Shinji <shiroshi@my.email.ne.jp>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config SETENFORCE
//config:	bool "setenforce (2.1 kb)"
//config:	default n
//config:	depends on SELINUX
//config:	help
//config:	Enable support to modify the mode SELinux is running in.

//applet:IF_SETENFORCE(APPLET(setenforce, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SETENFORCE) += setenforce.o

//usage:#define setenforce_trivial_usage
//usage:       "[Enforcing | Permissive | 1 | 0]"
//usage:#define setenforce_full_usage ""

#include "libbb.h"

/* These strings are arranged so that odd ones
 * result in security_setenforce(1) being done,
 * the rest will do security_setenforce(0) */
static const char *const setenforce_cmd[] = {
	"0",
	"1",
	"permissive",
	"enforcing",
	NULL,
};

int setenforce_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setenforce_main(int argc UNUSED_PARAM, char **argv)
{
	int i, rc;

	if (!argv[1] || argv[2])
		bb_show_usage();

	selinux_or_die();

	for (i = 0; setenforce_cmd[i]; i++) {
		if (strcasecmp(argv[1], setenforce_cmd[i]) != 0)
			continue;
		rc = security_setenforce(i & 1);
		if (rc < 0)
			bb_perror_msg_and_die("setenforce() failed");
		return 0;
	}

	bb_show_usage();
}
