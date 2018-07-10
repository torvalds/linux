/*
 * getenforce
 *
 * Based on libselinux 1.33.1
 * Port to BusyBox  Hiroshi Shinji <shiroshi@my.email.ne.jp>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config GETENFORCE
//config:	bool "getenforce (1.7 kb)"
//config:	default n
//config:	depends on SELINUX
//config:	help
//config:	Enable support to get the current mode of SELinux.

//applet:IF_GETENFORCE(APPLET(getenforce, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_GETENFORCE) += getenforce.o

//usage:#define getenforce_trivial_usage NOUSAGE_STR
//usage:#define getenforce_full_usage ""

#include "libbb.h"

int getenforce_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int getenforce_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	int rc;

	rc = is_selinux_enabled();
	if (rc < 0)
		bb_error_msg_and_die("is_selinux_enabled() failed");

	if (rc == 1) {
		rc = security_getenforce();
		if (rc < 0)
			bb_error_msg_and_die("getenforce() failed");

		if (rc)
			puts("Enforcing");
		else
			puts("Permissive");
	} else {
		puts("Disabled");
	}

	return 0;
}
