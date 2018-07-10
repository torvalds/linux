/* vi: set sw=4 ts=4: */
/*
 * Mini sulogin implementation for busybox
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config SULOGIN
//config:	bool "sulogin (17 kb)"
//config:	default y
//config:	select FEATURE_SYSLOG
//config:	help
//config:	sulogin is invoked when the system goes into single user
//config:	mode (this is done through an entry in inittab).

//applet:IF_SULOGIN(APPLET_NOEXEC(sulogin, sulogin, BB_DIR_SBIN, BB_SUID_DROP, sulogin))

//kbuild:lib-$(CONFIG_SULOGIN) += sulogin.o

//usage:#define sulogin_trivial_usage
//usage:       "[-t N] [TTY]"
//usage:#define sulogin_full_usage "\n\n"
//usage:       "Single user login\n"
//usage:     "\n	-t N	Timeout"

#include "libbb.h"
#include <syslog.h>

int sulogin_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int sulogin_main(int argc UNUSED_PARAM, char **argv)
{
	int timeout = 0;
	struct passwd *pwd;
	const char *shell;

	/* Note: sulogin is not a suid app. It is meant to be run by init
	 * for single user / emergency mode. init starts it as root.
	 * Normal users (potentially malicious ones) can only run it under
	 * their UID, therefore no paranoia here is warranted:
	 * $LD_LIBRARY_PATH in env, TTY = /dev/sda
	 * are no more dangerous here than in e.g. cp applet.
	 */

	logmode = LOGMODE_BOTH;
	openlog(applet_name, 0, LOG_AUTH);

	getopt32(argv, "t:+", &timeout);
	argv += optind;

	if (argv[0]) {
		close(0);
		close(1);
		dup(xopen(argv[0], O_RDWR));
		close(2);
		dup(0);
	}

	pwd = getpwuid(0);
	if (!pwd) {
		bb_error_msg_and_die("no password entry for root");
	}

	while (1) {
		int r;

		r = ask_and_check_password_extended(pwd, timeout,
			"Give root password for system maintenance\n"
			"(or type Control-D for normal startup):"
		);
		if (r < 0) {
			/* ^D, ^C, timeout, or read error */
			bb_error_msg("normal startup");
			return 0;
		}
		if (r > 0) {
			break;
		}
		bb_do_delay(LOGIN_FAIL_DELAY);
		bb_error_msg("Login incorrect");
	}

	bb_error_msg("starting shell for system maintenance");

	IF_SELINUX(renew_current_security_context());

	shell = getenv("SUSHELL");
	if (!shell)
		shell = getenv("sushell");
	if (!shell)
		shell = pwd->pw_shell;

	/* Exec login shell with no additional parameters. Never returns. */
	run_shell(shell, 1, NULL);
}
