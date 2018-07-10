/* vi: set sw=4 ts=4: */
/*
 * Mini su implementation for busybox
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config SU
//config:	bool "su (19 kb)"
//config:	default y
//config:	select FEATURE_SYSLOG
//config:	help
//config:	su is used to become another user during a login session.
//config:	Invoked without a username, su defaults to becoming the super user.
//config:	Note that busybox binary must be setuid root for this applet to
//config:	work properly.
//config:
//config:config FEATURE_SU_SYSLOG
//config:	bool "Log to syslog all attempts to use su"
//config:	default y
//config:	depends on SU
//config:
//config:config FEATURE_SU_CHECKS_SHELLS
//config:	bool "If user's shell is not in /etc/shells, disallow -s PROG"
//config:	default y
//config:	depends on SU
//config:
//config:config FEATURE_SU_BLANK_PW_NEEDS_SECURE_TTY
//config:	bool "Allow blank passwords only on TTYs in /etc/securetty"
//config:	default n
//config:	depends on SU

//applet:/* Needs to be run by root or be suid root - needs to change uid and gid: */
//applet:IF_SU(APPLET(su, BB_DIR_BIN, BB_SUID_REQUIRE))

//kbuild:lib-$(CONFIG_SU) += su.o

//usage:#define su_trivial_usage
//usage:       "[-lmp] [-] [-s SH] [USER [SCRIPT ARGS / -c 'CMD' ARG0 ARGS]]"
//usage:#define su_full_usage "\n\n"
//usage:       "Run shell under USER (by default, root)\n"
//usage:     "\n	-,-l	Clear environment, go to home dir, run shell as login shell"
//usage:     "\n	-p,-m	Do not set new $HOME, $SHELL, $USER, $LOGNAME"
//usage:     "\n	-c CMD	Command to pass to 'sh -c'"
//usage:     "\n	-s SH	Shell to use instead of user's default"

#include "libbb.h"
#include <syslog.h>

#if ENABLE_FEATURE_SU_CHECKS_SHELLS
/* Return 1 if SHELL is a restricted shell (one not returned by
 * getusershell), else 0, meaning it is a standard shell.  */
static int restricted_shell(const char *shell)
{
	char *line;
	int result = 1;

	/*setusershell(); - getusershell does it itself*/
	while ((line = getusershell()) != NULL) {
		if (/* *line != '#' && */ strcmp(line, shell) == 0) {
			result = 0;
			break;
		}
	}
	if (ENABLE_FEATURE_CLEAN_UP)
		endusershell();
	return result;
}
#endif

#define SU_OPT_mp (3)
#define SU_OPT_l  (4)

int su_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int su_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned flags;
	char *opt_shell = NULL;
	char *opt_command = NULL;
	const char *opt_username = "root";
	struct passwd *pw;
	uid_t cur_uid = getuid();
	const char *tty;
#if ENABLE_FEATURE_UTMP
	char user_buf[64];
#endif
	const char *old_user;
	int r;

	/* Note: we don't use "'+': stop at first non-option" idiom here.
	 * For su, "SCRIPT ARGS" or "-c CMD ARGS" do not stop option parsing:
	 * ARGS starting with dash will be treated as su options,
	 * not passed to shell. (Tested on util-linux 2.28).
	 */
	flags = getopt32(argv, "mplc:s:", &opt_command, &opt_shell);
	argv += optind;

	if (argv[0] && LONE_DASH(argv[0])) {
		flags |= SU_OPT_l;
		argv++;
	}

	/* get user if specified */
	if (argv[0]) {
		opt_username = argv[0];
		argv++;
	}

	tty = xmalloc_ttyname(STDIN_FILENO);
	if (!tty)
		tty = "none";
	tty = skip_dev_pfx(tty);

	if (ENABLE_FEATURE_SU_SYSLOG) {
		/* The utmp entry (via getlogin) is probably the best way to
		 * identify the user, especially if someone su's from a su-shell.
		 * But getlogin can fail -- usually due to lack of utmp entry.
		 * in this case resort to getpwuid.  */
#if ENABLE_FEATURE_UTMP
		old_user = user_buf;
		if (getlogin_r(user_buf, sizeof(user_buf)) != 0)
#endif
		{
			pw = getpwuid(cur_uid);
			old_user = pw ? xstrdup(pw->pw_name) : "";
		}
		openlog(applet_name, 0, LOG_AUTH);
	}

	pw = xgetpwnam(opt_username);

	r = 1;
	if (cur_uid != 0)
		r = ask_and_check_password(pw);
	if (r > 0) {
		if (ENABLE_FEATURE_SU_BLANK_PW_NEEDS_SECURE_TTY
		 && r == CHECKPASS_PW_HAS_EMPTY_PASSWORD
		 && !is_tty_secure(tty)
		) {
			goto fail;
		}
		if (ENABLE_FEATURE_SU_SYSLOG)
			syslog(LOG_NOTICE, "%c %s %s:%s",
				'+', tty, old_user, opt_username);
	} else {
 fail:
		if (ENABLE_FEATURE_SU_SYSLOG)
			syslog(LOG_NOTICE, "%c %s %s:%s",
				'-', tty, old_user, opt_username);
		bb_do_delay(LOGIN_FAIL_DELAY);
		bb_error_msg_and_die("incorrect password");
	}

	if (ENABLE_FEATURE_CLEAN_UP && ENABLE_FEATURE_SU_SYSLOG) {
		closelog();
	}

	if (!opt_shell && (flags & SU_OPT_mp)) {
		/* -s SHELL is not given, but "preserve env" opt is */
		opt_shell = getenv("SHELL");
	}

#if ENABLE_FEATURE_SU_CHECKS_SHELLS
	if (opt_shell && cur_uid != 0 && pw->pw_shell && restricted_shell(pw->pw_shell)) {
		/* The user being su'd to has a nonstandard shell, and so is
		 * probably a uucp account or has restricted access.  Don't
		 * compromise the account by allowing access with a standard
		 * shell.  */
		bb_error_msg("using restricted shell");
		opt_shell = NULL; /* ignore -s PROG */
	}
	/* else: user can run whatever he wants via "su -s PROG USER".
	 * This is safe since PROG is run under user's uid/gid. */
#endif
	if (!opt_shell)
		opt_shell = pw->pw_shell;

	change_identity(pw);
	setup_environment(opt_shell,
			((flags & SU_OPT_l) / SU_OPT_l * SETUP_ENV_CLEARENV)
			+ (!(flags & SU_OPT_mp) * SETUP_ENV_CHANGEENV)
			+ (!(flags & SU_OPT_l) * SETUP_ENV_NO_CHDIR),
			pw);
	IF_SELINUX(set_current_security_context(NULL);)

	if (opt_command) {
		*--argv = opt_command;
		*--argv = (char*)"-c";
	}

	/* A nasty ioctl exists which can stuff data into input queue:
	 * #include <sys/ioctl.h>
	 * int main() {
	 *	const char *msg = "echo $UID\n";
	 *	while (*msg) ioctl(0, TIOCSTI, *msg++);
	 *	return 0;
	 * }
	 * With "su USER -c EXPLOIT" run by root, exploit can make root shell
	 * read as input and execute arbitrary command.
	 * It's debatable whether we need to protect against this
	 * (root may hesitate to run unknown scripts interactively).
	 *
	 * Some versions of su run -c CMD in a different session:
	 * ioctl(TIOCSTI) works only on the controlling tty.
	 */

	/* Never returns */
	run_shell(opt_shell, flags & SU_OPT_l, (const char**)argv);

	/* return EXIT_FAILURE; - not reached */
}
