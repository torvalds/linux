/* vi: set sw=4 ts=4: */
/*
 * chpasswd.c
 *
 * Written for SLIND (from passwd.c) by Alexander Shishkin <virtuoso@slind.org>
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config CHPASSWD
//config:	bool "chpasswd (18 kb)"
//config:	default y
//config:	help
//config:	Reads a file of user name and password pairs from standard input
//config:	and uses this information to update a group of existing users.
//config:
//config:config FEATURE_DEFAULT_PASSWD_ALGO
//config:	string "Default encryption method (passwd -a, cryptpw -m, chpasswd -c ALG)"
//config:	default "des"
//config:	depends on PASSWD || CRYPTPW || CHPASSWD
//config:	help
//config:	Possible choices are "d[es]", "m[d5]", "s[ha256]" or "sha512".

//applet:IF_CHPASSWD(APPLET(chpasswd, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_CHPASSWD) += chpasswd.o

//usage:#define chpasswd_trivial_usage
//usage:	IF_LONG_OPTS("[--md5|--encrypted|--crypt-method]") IF_NOT_LONG_OPTS("[-m|-e|-c]")
//usage:#define chpasswd_full_usage "\n\n"
//usage:       "Read user:password from stdin and update /etc/passwd\n"
//usage:	IF_LONG_OPTS(
//usage:     "\n	-e,--encrypted		Supplied passwords are in encrypted form"
//usage:     "\n	-m,--md5		Eencrypt using md5, not des"
//usage:     "\n	-c,--crypt-method ALG	"CRYPT_METHODS_HELP_STR
//usage:	)
//usage:	IF_NOT_LONG_OPTS(
//usage:     "\n	-e	Supplied passwords are in encrypted form"
//usage:     "\n	-m	Eencrypt using md5, not des"
//usage:     "\n	-c ALG	"CRYPT_METHODS_HELP_STR
//usage:	)

#include "libbb.h"

#if ENABLE_LONG_OPTS
static const char chpasswd_longopts[] ALIGN1 =
	"encrypted\0"    No_argument       "e"
	"md5\0"          No_argument       "m"
	"crypt-method\0" Required_argument "c"
	;
#endif

#define OPT_ENC  1
#define OPT_MD5  2

int chpasswd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int chpasswd_main(int argc UNUSED_PARAM, char **argv)
{
	char *name;
	const char *algo = CONFIG_FEATURE_DEFAULT_PASSWD_ALGO;
	int opt;

	if (getuid() != 0)
		bb_error_msg_and_die(bb_msg_perm_denied_are_you_root);

	opt = getopt32long(argv, "^" "emc:" "\0" "m--ec:e--mc:c--em",
			chpasswd_longopts,
			&algo
	);

	while ((name = xmalloc_fgetline(stdin)) != NULL) {
		char *free_me;
		char *pass;
		int rc;

		pass = strchr(name, ':');
		if (!pass)
			bb_error_msg_and_die("missing new password");
		*pass++ = '\0';

		xuname2uid(name); /* dies if there is no such user */

		free_me = NULL;
		if (!(opt & OPT_ENC)) {
			char salt[MAX_PW_SALT_LEN];

			if (opt & OPT_MD5) {
				/* Force MD5 if the -m flag is set */
				algo = "md5";
			}

			crypt_make_pw_salt(salt, algo);
			free_me = pass = pw_encrypt(pass, salt, 0);
		}

		/* This is rather complex: if user is not found in /etc/shadow,
		 * we try to find & change his passwd in /etc/passwd */
#if ENABLE_FEATURE_SHADOWPASSWDS
		rc = update_passwd(bb_path_shadow_file, name, pass, NULL);
		if (rc > 0) /* password in /etc/shadow was updated */
			pass = (char*)"x";
		if (rc >= 0)
			/* 0 = /etc/shadow missing (not an error), >0 = passwd changed in /etc/shadow */
#endif
			rc = update_passwd(bb_path_passwd_file, name, pass, NULL);
		/* LOGMODE_BOTH logs to syslog also */
		logmode = LOGMODE_BOTH;
		if (rc < 0)
			bb_error_msg_and_die("an error occurred updating password for %s", name);
		if (rc)
			bb_error_msg("password for '%s' changed", name);
		logmode = LOGMODE_STDIO;
		free(name);
		free(free_me);
	}
	return EXIT_SUCCESS;
}
