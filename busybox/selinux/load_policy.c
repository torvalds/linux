/*
 * load_policy
 * Author: Yuichi Nakamura <ynakam@hitachisoft.jp>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config LOAD_POLICY
//config:	bool "load_policy (1.6 kb)"
//config:	default n
//config:	depends on SELINUX
//config:	help
//config:	Enable support to load SELinux policy.

//applet:IF_LOAD_POLICY(APPLET(load_policy, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_LOAD_POLICY) += load_policy.o

//usage:#define load_policy_trivial_usage NOUSAGE_STR
//usage:#define load_policy_full_usage ""

#include "libbb.h"

int load_policy_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int load_policy_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	int rc;

	if (argv[1]) {
		bb_show_usage();
	}

	rc = selinux_mkload_policy(1);
	if (rc < 0) {
		bb_perror_msg_and_die("can't load policy");
	}

	return 0;
}
