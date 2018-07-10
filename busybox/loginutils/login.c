/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config LOGIN
//config:	bool "login (24 kb)"
//config:	default y
//config:	select FEATURE_SYSLOG
//config:	help
//config:	login is used when signing onto a system.
//config:
//config:	Note that busybox binary must be setuid root for this applet to
//config:	work properly.
//config:
//config:config LOGIN_SESSION_AS_CHILD
//config:	bool "Run logged in session in a child process"
//config:	default y if PAM
//config:	depends on LOGIN
//config:	help
//config:	Run the logged in session in a child process.  This allows
//config:	login to clean up things such as utmp entries or PAM sessions
//config:	when the login session is complete.  If you use PAM, you
//config:	almost always would want this to be set to Y, else PAM session
//config:	will not be cleaned up.
//config:
//config:config LOGIN_SCRIPTS
//config:	bool "Support login scripts"
//config:	depends on LOGIN
//config:	default y
//config:	help
//config:	Enable this if you want login to execute $LOGIN_PRE_SUID_SCRIPT
//config:	just prior to switching from root to logged-in user.
//config:
//config:config FEATURE_NOLOGIN
//config:	bool "Support /etc/nologin"
//config:	default y
//config:	depends on LOGIN
//config:	help
//config:	The file /etc/nologin is used by (some versions of) login(1).
//config:	If it exists, non-root logins are prohibited.
//config:
//config:config FEATURE_SECURETTY
//config:	bool "Support /etc/securetty"
//config:	default y
//config:	depends on LOGIN
//config:	help
//config:	The file /etc/securetty is used by (some versions of) login(1).
//config:	The file contains the device names of tty lines (one per line,
//config:	without leading /dev/) on which root is allowed to login.

//applet:/* Needs to be run by root or be suid root - needs to change uid and gid: */
//applet:IF_LOGIN(APPLET(login, BB_DIR_BIN, BB_SUID_REQUIRE))

//kbuild:lib-$(CONFIG_LOGIN) += login.o

//usage:#define login_trivial_usage
//usage:       "[-p] [-h HOST] [[-f] USER]"
//usage:#define login_full_usage "\n\n"
//usage:       "Begin a new session on the system\n"
//usage:     "\n	-f	Don't authenticate (user already authenticated)"
//usage:     "\n	-h HOST	Host user came from (for network logins)"
//usage:     "\n	-p	Preserve environment"

#include "libbb.h"
#include "common_bufsiz.h"
#include <syslog.h>

#if ENABLE_SELINUX
# include <selinux/selinux.h>  /* for is_selinux_enabled()  */
# include <selinux/get_context_list.h> /* for get_default_context() */
# /* from deprecated <selinux/flask.h>: */
# undef  SECCLASS_CHR_FILE
# define SECCLASS_CHR_FILE 10
#endif

#if ENABLE_PAM
/* PAM may include <locale.h>. We may need to undefine bbox's stub define: */
# undef setlocale
/* For some obscure reason, PAM is not in pam/xxx, but in security/xxx.
 * Apparently they like to confuse people. */
# include <security/pam_appl.h>
# include <security/pam_misc.h>

# if 0
/* This supposedly can be used to avoid double password prompt,
 * if used instead of standard misc_conv():
 *
 * "When we want to authenticate first with local method and then with tacacs for example,
 *  the password is asked for local method and if not good is asked a second time for tacacs.
 *  So if we want to authenticate a user with tacacs, and the user exists localy, the password is
 *  asked two times before authentication is accepted."
 *
 * However, code looks shaky. For example, why misc_conv() return value is ignored?
 * Are msg[i] and resp[i] indexes handled correctly?
 */
static char *passwd = NULL;
static int my_conv(int num_msg, const struct pam_message **msg,
		struct pam_response **resp, void *data)
{
	int i;
	for (i = 0; i < num_msg; i++) {
		switch (msg[i]->msg_style) {
		case PAM_PROMPT_ECHO_OFF:
			if (passwd == NULL) {
				misc_conv(num_msg, msg, resp, data);
				passwd = xstrdup(resp[i]->resp);
				return PAM_SUCCESS;
			}

			resp[0] = xzalloc(sizeof(struct pam_response));
			resp[0]->resp = passwd;
			passwd = NULL;
			resp[0]->resp_retcode = PAM_SUCCESS;
			resp[1] = NULL;
			return PAM_SUCCESS;

		default:
			break;
		}
	}

	return PAM_SUCCESS;
}
# endif

static const struct pam_conv conv = {
	misc_conv,
	NULL
};
#endif

enum {
	TIMEOUT = 60,
	EMPTY_USERNAME_COUNT = 10,
	/* Some users found 32 chars limit to be too low: */
	USERNAME_SIZE = 64,
	TTYNAME_SIZE = 32,
};

struct globals {
	struct termios tty_attrs;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)


#if ENABLE_FEATURE_NOLOGIN
static void die_if_nologin(void)
{
	FILE *fp;
	int c;
	int empty = 1;

	fp = fopen_for_read("/etc/nologin");
	if (!fp) /* assuming it does not exist */
		return;

	while ((c = getc(fp)) != EOF) {
		if (c == '\n')
			bb_putchar('\r');
		bb_putchar(c);
		empty = 0;
	}
	if (empty)
		puts("\r\nSystem closed for routine maintenance\r");

	fclose(fp);
	fflush_all();
	/* Users say that they do need this prior to exit: */
	tcdrain(STDOUT_FILENO);
	exit(EXIT_FAILURE);
}
#else
# define die_if_nologin() ((void)0)
#endif

#if ENABLE_SELINUX
static void initselinux(char *username, char *full_tty,
						security_context_t *user_sid)
{
	security_context_t old_tty_sid, new_tty_sid;

	if (!is_selinux_enabled())
		return;

	if (get_default_context(username, NULL, user_sid)) {
		bb_error_msg_and_die("can't get SID for %s", username);
	}
	if (getfilecon(full_tty, &old_tty_sid) < 0) {
		bb_perror_msg_and_die("getfilecon(%s) failed", full_tty);
	}
	if (security_compute_relabel(*user_sid, old_tty_sid,
				SECCLASS_CHR_FILE, &new_tty_sid) != 0) {
		bb_perror_msg_and_die("security_change_sid(%s) failed", full_tty);
	}
	if (setfilecon(full_tty, new_tty_sid) != 0) {
		bb_perror_msg_and_die("chsid(%s, %s) failed", full_tty, new_tty_sid);
	}
}
#endif

#if ENABLE_LOGIN_SCRIPTS
static void run_login_script(struct passwd *pw, char *full_tty)
{
	char *t_argv[2];

	t_argv[0] = getenv("LOGIN_PRE_SUID_SCRIPT");
	if (t_argv[0]) {
		t_argv[1] = NULL;
		xsetenv("LOGIN_TTY", full_tty);
		xsetenv("LOGIN_USER", pw->pw_name);
		xsetenv("LOGIN_UID", utoa(pw->pw_uid));
		xsetenv("LOGIN_GID", utoa(pw->pw_gid));
		xsetenv("LOGIN_SHELL", pw->pw_shell);
		spawn_and_wait(t_argv); /* NOMMU-friendly */
		unsetenv("LOGIN_TTY");
		unsetenv("LOGIN_USER");
		unsetenv("LOGIN_UID");
		unsetenv("LOGIN_GID");
		unsetenv("LOGIN_SHELL");
	}
}
#else
void run_login_script(struct passwd *pw, char *full_tty);
#endif

#if ENABLE_LOGIN_SESSION_AS_CHILD && ENABLE_PAM
static void login_pam_end(pam_handle_t *pamh)
{
	int pamret;

	pamret = pam_setcred(pamh, PAM_DELETE_CRED);
	if (pamret != PAM_SUCCESS) {
		bb_error_msg("pam_%s failed: %s (%d)", "setcred",
			pam_strerror(pamh, pamret), pamret);
	}
	pamret = pam_close_session(pamh, 0);
	if (pamret != PAM_SUCCESS) {
		bb_error_msg("pam_%s failed: %s (%d)", "close_session",
			pam_strerror(pamh, pamret), pamret);
	}
	pamret = pam_end(pamh, pamret);
	if (pamret != PAM_SUCCESS) {
		bb_error_msg("pam_%s failed: %s (%d)", "end",
			pam_strerror(pamh, pamret), pamret);
	}
}
#endif /* ENABLE_PAM */

static void get_username_or_die(char *buf, int size_buf)
{
	int c, cntdown;

	cntdown = EMPTY_USERNAME_COUNT;
 prompt:
	print_login_prompt();
	/* skip whitespace */
	do {
		c = getchar();
		if (c == EOF)
			exit(EXIT_FAILURE);
		if (c == '\n') {
			if (!--cntdown)
				exit(EXIT_FAILURE);
			goto prompt;
		}
	} while (isspace(c)); /* maybe isblank? */

	*buf++ = c;
	if (!fgets(buf, size_buf-2, stdin))
		exit(EXIT_FAILURE);
	if (!strchr(buf, '\n'))
		exit(EXIT_FAILURE);
	while ((unsigned char)*buf > ' ')
		buf++;
	*buf = '\0';
}

static void motd(void)
{
	int fd;

	fd = open(bb_path_motd_file, O_RDONLY);
	if (fd >= 0) {
		fflush_all();
		bb_copyfd_eof(fd, STDOUT_FILENO);
		close(fd);
	}
}

static void alarm_handler(int sig UNUSED_PARAM)
{
	/* This is the escape hatch! Poor serial line users and the like
	 * arrive here when their connection is broken.
	 * We don't want to block here */
	ndelay_on(STDOUT_FILENO);
	/* Test for correct attr restoring:
	 * run "getty 0 -" from a shell, enter bogus username, stop at
	 * password prompt, let it time out. Without the tcsetattr below,
	 * when you are back at shell prompt, echo will be still off.
	 */
	tcsetattr_stdin_TCSANOW(&G.tty_attrs);
	printf("\r\nLogin timed out after %u seconds\r\n", TIMEOUT);
	fflush_all();
	/* unix API is brain damaged regarding O_NONBLOCK,
	 * we should undo it, or else we can affect other processes */
	ndelay_off(STDOUT_FILENO);
	_exit(EXIT_SUCCESS);
}

int login_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int login_main(int argc UNUSED_PARAM, char **argv)
{
	enum {
		LOGIN_OPT_f = (1<<0),
		LOGIN_OPT_h = (1<<1),
		LOGIN_OPT_p = (1<<2),
	};
	char *fromhost;
	char username[USERNAME_SIZE];
	int run_by_root;
	unsigned opt;
	int count = 0;
	struct passwd *pw;
	char *opt_host = NULL;
	char *opt_user = opt_user; /* for compiler */
	char *full_tty;
	char *short_tty;
	IF_SELINUX(security_context_t user_sid = NULL;)
#if ENABLE_PAM
	int pamret;
	pam_handle_t *pamh;
	const char *pamuser;
	const char *failed_msg;
	struct passwd pwdstruct;
	char pwdbuf[256];
	char **pamenv;
#endif
#if ENABLE_LOGIN_SESSION_AS_CHILD
	pid_t child_pid;
#endif

	INIT_G();

	/* More of suid paranoia if called by non-root: */
	/* Clear dangerous stuff, set PATH */
	run_by_root = !sanitize_env_if_suid();

	/* Mandatory paranoia for suid applet:
	 * ensure that fd# 0,1,2 are opened (at least to /dev/null)
	 * and any extra open fd's are closed.
	 */
	bb_daemon_helper(DAEMON_CLOSE_EXTRA_FDS);

	username[0] = '\0';
	opt = getopt32(argv, "f:h:p", &opt_user, &opt_host);
	if (opt & LOGIN_OPT_f) {
		if (!run_by_root)
			bb_error_msg_and_die("-f is for root only");
		safe_strncpy(username, opt_user, sizeof(username));
	}
	argv += optind;
	if (argv[0]) /* user from command line (getty) */
		safe_strncpy(username, argv[0], sizeof(username));

	/* Save tty attributes - and by doing it, check that it's indeed a tty */
	if (tcgetattr(STDIN_FILENO, &G.tty_attrs) < 0
	 || !isatty(STDOUT_FILENO)
	 /*|| !isatty(STDERR_FILENO) - no, guess some people might want to redirect this */
	) {
		return EXIT_FAILURE;  /* Must be a terminal */
	}

	/* We install timeout handler only _after_ we saved G.tty_attrs */
	signal(SIGALRM, alarm_handler);
	alarm(TIMEOUT);

	/* Find out and memorize our tty name */
	full_tty = xmalloc_ttyname(STDIN_FILENO);
	if (!full_tty)
		full_tty = xstrdup("UNKNOWN");
	short_tty = skip_dev_pfx(full_tty);

	if (opt_host) {
		fromhost = xasprintf(" on '%s' from '%s'", short_tty, opt_host);
	} else {
		fromhost = xasprintf(" on '%s'", short_tty);
	}

	/* Was breaking "login <username>" from shell command line: */
	/*bb_setpgrp();*/

	openlog(applet_name, LOG_PID | LOG_CONS, LOG_AUTH);

	while (1) {
		/* flush away any type-ahead (as getty does) */
		tcflush(0, TCIFLUSH);

		if (!username[0])
			get_username_or_die(username, sizeof(username));

#if ENABLE_PAM
		pamret = pam_start("login", username, &conv, &pamh);
		if (pamret != PAM_SUCCESS) {
			failed_msg = "start";
			goto pam_auth_failed;
		}
		/* set TTY (so things like securetty work) */
		pamret = pam_set_item(pamh, PAM_TTY, short_tty);
		if (pamret != PAM_SUCCESS) {
			failed_msg = "set_item(TTY)";
			goto pam_auth_failed;
		}
		/* set RHOST */
		if (opt_host) {
			pamret = pam_set_item(pamh, PAM_RHOST, opt_host);
			if (pamret != PAM_SUCCESS) {
				failed_msg = "set_item(RHOST)";
				goto pam_auth_failed;
			}
		}
		if (!(opt & LOGIN_OPT_f)) {
			pamret = pam_authenticate(pamh, 0);
			if (pamret != PAM_SUCCESS) {
				failed_msg = "authenticate";
				goto pam_auth_failed;
				/* TODO: or just "goto auth_failed"
				 * since user seems to enter wrong password
				 * (in this case pamret == 7)
				 */
			}
		}
		/* check that the account is healthy */
		pamret = pam_acct_mgmt(pamh, 0);
		if (pamret != PAM_SUCCESS) {
			failed_msg = "acct_mgmt";
			goto pam_auth_failed;
		}
		/* read user back */
		pamuser = NULL;
		/* gcc: "dereferencing type-punned pointer breaks aliasing rules..."
		 * thus we cast to (void*) */
		if (pam_get_item(pamh, PAM_USER, (void*)&pamuser) != PAM_SUCCESS) {
			failed_msg = "get_item(USER)";
			goto pam_auth_failed;
		}
		if (!pamuser || !pamuser[0])
			goto auth_failed;
		safe_strncpy(username, pamuser, sizeof(username));
		/* Don't use "pw = getpwnam(username);",
		 * PAM is said to be capable of destroying static storage
		 * used by getpwnam(). We are using safe(r) function */
		pw = NULL;
		getpwnam_r(username, &pwdstruct, pwdbuf, sizeof(pwdbuf), &pw);
		if (!pw)
			goto auth_failed;
		pamret = pam_open_session(pamh, 0);
		if (pamret != PAM_SUCCESS) {
			failed_msg = "open_session";
			goto pam_auth_failed;
		}
		pamret = pam_setcred(pamh, PAM_ESTABLISH_CRED);
		if (pamret != PAM_SUCCESS) {
			failed_msg = "setcred";
			goto pam_auth_failed;
		}
		break; /* success, continue login process */

 pam_auth_failed:
		/* syslog, because we don't want potential attacker
		 * to know _why_ login failed */
		syslog(LOG_WARNING, "pam_%s call failed: %s (%d)", failed_msg,
					pam_strerror(pamh, pamret), pamret);
		safe_strncpy(username, "UNKNOWN", sizeof(username));
#else /* not PAM */
		pw = getpwnam(username);
		if (!pw) {
			strcpy(username, "UNKNOWN");
			goto fake_it;
		}

		if (pw->pw_passwd[0] == '!' || pw->pw_passwd[0] == '*')
			goto auth_failed;

		if (opt & LOGIN_OPT_f)
			break; /* -f USER: success without asking passwd */

		if (pw->pw_uid == 0 && !is_tty_secure(short_tty))
			goto auth_failed;

		/* Don't check the password if password entry is empty (!) */
		if (!pw->pw_passwd[0])
			break;
 fake_it:
		/* Password reading and authorization takes place here.
		 * Note that reads (in no-echo mode) trash tty attributes.
		 * If we get interrupted by SIGALRM, we need to restore attrs.
		 */
		if (ask_and_check_password(pw) > 0)
			break;
#endif /* ENABLE_PAM */
 auth_failed:
		opt &= ~LOGIN_OPT_f;
		bb_do_delay(LOGIN_FAIL_DELAY);
		/* TODO: doesn't sound like correct English phrase to me */
		puts("Login incorrect");
		if (++count == 3) {
			syslog(LOG_WARNING, "invalid password for '%s'%s",
						username, fromhost);

			if (ENABLE_FEATURE_CLEAN_UP)
				free(fromhost);

			return EXIT_FAILURE;
		}
		username[0] = '\0';
	} /* while (1) */

	alarm(0);
	/* We can ignore /etc/nologin if we are logging in as root,
	 * it doesn't matter whether we are run by root or not */
	if (pw->pw_uid != 0)
		die_if_nologin();

#if ENABLE_LOGIN_SESSION_AS_CHILD
	child_pid = vfork();
	if (child_pid != 0) {
		if (child_pid < 0)
			bb_perror_msg("vfork");
		else {
			if (safe_waitpid(child_pid, NULL, 0) == -1)
				bb_perror_msg("waitpid");
			update_utmp_DEAD_PROCESS(child_pid);
		}
		IF_PAM(login_pam_end(pamh);)
		return 0;
	}
#endif

	IF_SELINUX(initselinux(username, full_tty, &user_sid);)

	/* Try these, but don't complain if they fail.
	 * _f_chown is safe wrt race t=ttyname(0);...;chown(t); */
	fchown(0, pw->pw_uid, pw->pw_gid);
	fchmod(0, 0600);

	update_utmp(getpid(), USER_PROCESS, short_tty, username, run_by_root ? opt_host : NULL);

	/* We trust environment only if we run by root */
	if (ENABLE_LOGIN_SCRIPTS && run_by_root)
		run_login_script(pw, full_tty);

	change_identity(pw);
	setup_environment(pw->pw_shell,
			(!(opt & LOGIN_OPT_p) * SETUP_ENV_CLEARENV) + SETUP_ENV_CHANGEENV,
			pw);

#if ENABLE_PAM
	/* Modules such as pam_env will setup the PAM environment,
	 * which should be copied into the new environment. */
	pamenv = pam_getenvlist(pamh);
	if (pamenv) while (*pamenv) {
		putenv(*pamenv);
		pamenv++;
	}
#endif

	if (access(".hushlogin", F_OK) != 0)
		motd();

	if (pw->pw_uid == 0)
		syslog(LOG_INFO, "root login%s", fromhost);

	if (ENABLE_FEATURE_CLEAN_UP)
		free(fromhost);

	/* well, a simple setexeccon() here would do the job as well,
	 * but let's play the game for now */
	IF_SELINUX(set_current_security_context(user_sid);)

	// util-linux login also does:
	// /* start new session */
	// setsid();
	// /* TIOCSCTTY: steal tty from other process group */
	// if (ioctl(0, TIOCSCTTY, 1)) error_msg...
	// BBox login used to do this (see above):
	// bb_setpgrp();
	// If this stuff is really needed, add it and explain why!

	/* Set signals to defaults */
	/* Non-ignored signals revert to SIG_DFL on exec anyway */
	/*signal(SIGALRM, SIG_DFL);*/

	/* Is this correct? This way user can ctrl-c out of /etc/profile,
	 * potentially creating security breach (tested with bash 3.0).
	 * But without this, bash 3.0 will not enable ctrl-c either.
	 * Maybe bash is buggy?
	 * Need to find out what standards say about /bin/login -
	 * should we leave SIGINT etc enabled or disabled? */
	signal(SIGINT, SIG_DFL);

	/* Exec login shell with no additional parameters */
	run_shell(pw->pw_shell, 1, NULL);

	/* return EXIT_FAILURE; - not reached */
}
