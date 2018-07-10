/* vi: set sw=4 ts=4: */
/*
 * Mini init implementation for busybox
 *
 * Copyright (C) 1995, 1996 by Bruce Perens <bruce@pixar.com>.
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Adjusted by so many folks, it's impossible to keep track.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config INIT
//config:	bool "init (9.3 kb)"
//config:	default y
//config:	select FEATURE_SYSLOG
//config:	help
//config:	init is the first program run when the system boots.
//config:
//config:config LINUXRC
//config:	bool "linuxrc: support running init from initrd (not initramfs)"
//config:	default y
//config:	select FEATURE_SYSLOG
//config:	help
//config:	Legacy support for running init under the old-style initrd. Allows
//config:	the name linuxrc to act as init, and it doesn't assume init is PID 1.
//config:
//config:	This does not apply to initramfs, which runs /init as PID 1 and
//config:	requires no special support.
//config:
//config:config FEATURE_USE_INITTAB
//config:	bool "Support reading an inittab file"
//config:	default y
//config:	depends on INIT || LINUXRC
//config:	help
//config:	Allow init to read an inittab file when the system boot.
//config:
//config:config FEATURE_KILL_REMOVED
//config:	bool "Support killing processes that have been removed from inittab"
//config:	default n
//config:	depends on FEATURE_USE_INITTAB
//config:	help
//config:	When respawn entries are removed from inittab and a SIGHUP is
//config:	sent to init, this option will make init kill the processes
//config:	that have been removed.
//config:
//config:config FEATURE_KILL_DELAY
//config:	int "How long to wait between TERM and KILL (0 - send TERM only)" if FEATURE_KILL_REMOVED
//config:	range 0 1024
//config:	default 0
//config:	depends on FEATURE_KILL_REMOVED
//config:	help
//config:	With nonzero setting, init sends TERM, forks, child waits N
//config:	seconds, sends KILL and exits. Setting it too high is unwise
//config:	(child will hang around for too long and could actually kill
//config:	the wrong process!)
//config:
//config:config FEATURE_INIT_SCTTY
//config:	bool "Run commands with leading dash with controlling tty"
//config:	default y
//config:	depends on INIT || LINUXRC
//config:	help
//config:	If this option is enabled, init will try to give a controlling
//config:	tty to any command which has leading hyphen (often it's "-/bin/sh").
//config:	More precisely, init will do "ioctl(STDIN_FILENO, TIOCSCTTY, 0)".
//config:	If device attached to STDIN_FILENO can be a ctty but is not yet
//config:	a ctty for other session, it will become this process' ctty.
//config:	This is not the traditional init behavour, but is often what you want
//config:	in an embedded system where the console is only accessed during
//config:	development or for maintenance.
//config:	NB: using cttyhack applet may work better.
//config:
//config:config FEATURE_INIT_SYSLOG
//config:	bool "Enable init to write to syslog"
//config:	default y
//config:	depends on INIT || LINUXRC
//config:	help
//config:	If selected, some init messages are sent to syslog.
//config:	Otherwise, they are sent to VT #5 if linux virtual tty is detected
//config:	(if not, no separate logging is done).
//config:
//config:config FEATURE_INIT_QUIET
//config:	bool "Be quiet on boot (no 'init started:' message)"
//config:	default y
//config:	depends on INIT || LINUXRC
//config:
//config:config FEATURE_INIT_COREDUMPS
//config:	bool "Support dumping core for child processes (debugging only)"
//config:	default n	# not Y because this is a debug option
//config:	depends on INIT || LINUXRC
//config:	help
//config:	If this option is enabled and the file /.init_enable_core
//config:	exists, then init will call setrlimit() to allow unlimited
//config:	core file sizes. If this option is disabled, processes
//config:	will not generate any core files.
//config:
//config:config INIT_TERMINAL_TYPE
//config:	string "Initial terminal type"
//config:	default "linux"
//config:	depends on INIT || LINUXRC
//config:	help
//config:	This is the initial value set by init for the TERM environment
//config:	variable. This variable is used by programs which make use of
//config:	extended terminal capabilities.
//config:
//config:	Note that on Linux, init attempts to detect serial terminal and
//config:	sets TERM to "vt102" if one is found.
//config:
//config:config FEATURE_INIT_MODIFY_CMDLINE
//config:	bool "Clear init's command line"
//config:	default y
//config:	depends on INIT || LINUXRC
//config:	help
//config:	When launched as PID 1 and after parsing its arguments, init
//config:	wipes all the arguments but argv[0] and rewrites argv[0] to
//config:	contain only "init", so that its command line appears solely as
//config:	"init" in tools such as ps.
//config:	If this option is set to Y, init will keep its original behavior,
//config:	otherwise, all the arguments including argv[0] will be preserved,
//config:	be they parsed or ignored by init.
//config:	The original command-line used to launch init can then be
//config:	retrieved in /proc/1/cmdline on Linux, for example.

//applet:IF_INIT(APPLET(init, BB_DIR_SBIN, BB_SUID_DROP))
//applet:IF_LINUXRC(APPLET_ODDNAME(linuxrc, init, BB_DIR_ROOT, BB_SUID_DROP, linuxrc))

//kbuild:lib-$(CONFIG_INIT) += init.o
//kbuild:lib-$(CONFIG_LINUXRC) += init.o

#define DEBUG_SEGV_HANDLER 0

#include "libbb.h"
#include "common_bufsiz.h"
#include <syslog.h>
#ifdef __linux__
# include <linux/vt.h>
# include <sys/sysinfo.h>
#endif
#include "reboot.h" /* reboot() constants */

#if DEBUG_SEGV_HANDLER
# undef _GNU_SOURCE
# define _GNU_SOURCE 1
# undef __USE_GNU
# define __USE_GNU 1
# include <execinfo.h>
# include <sys/ucontext.h>
#endif

/* Used only for sanitizing purposes in set_sane_term() below. On systems where
 * the baud rate is stored in a separate field, we can safely disable them. */
#ifndef CBAUD
# define CBAUD 0
# define CBAUDEX 0
#endif

/* Was a CONFIG_xxx option. A lot of people were building
 * not fully functional init by switching it on! */
#define DEBUG_INIT 0

#define CONSOLE_NAME_SIZE 32

/* Default sysinit script. */
#ifndef INIT_SCRIPT
# define INIT_SCRIPT  "/etc/init.d/rcS"
#endif

/* Each type of actions can appear many times. They will be
 * handled in order. RESTART is an exception, only 1st is used.
 */
/* Start these actions first and wait for completion */
#define SYSINIT     0x01
/* Start these after SYSINIT and wait for completion */
#define WAIT        0x02
/* Start these after WAIT and *dont* wait for completion */
#define ONCE        0x04
/*
 * NB: while SYSINIT/WAIT/ONCE are being processed,
 * SIGHUP ("reread /etc/inittab") will be processed only after
 * each group of actions. If new inittab adds, say, a SYSINIT action,
 * it will not be run, since init is already "past SYSINIT stage".
 */
/* Start these after ONCE are started, restart on exit */
#define RESPAWN     0x08
/* Like RESPAWN, but wait for <Enter> to be pressed on tty */
#define ASKFIRST    0x10
/*
 * Start these on SIGINT, and wait for completion.
 * Then go back to respawning RESPAWN and ASKFIRST actions.
 * NB: kernel sends SIGINT to us if Ctrl-Alt-Del was pressed.
 */
#define CTRLALTDEL  0x20
/*
 * Start these before killing all processes in preparation for
 * running RESTART actions or doing low-level halt/reboot/poweroff
 * (initiated by SIGUSR1/SIGTERM/SIGUSR2).
 * Wait for completion before proceeding.
 */
#define SHUTDOWN    0x40
/*
 * exec() on SIGQUIT. SHUTDOWN actions are started and waited for,
 * then all processes are killed, then init exec's 1st RESTART action,
 * replacing itself by it. If no RESTART action specified,
 * SIGQUIT has no effect.
 */
#define RESTART     0x80

/* A linked list of init_actions, to be read from inittab */
struct init_action {
	struct init_action *next;
	pid_t pid;
	uint8_t action_type;
	char terminal[CONSOLE_NAME_SIZE];
	char command[1];
};

struct globals {
	struct init_action *init_action_list;
#if !ENABLE_FEATURE_INIT_SYSLOG
	const char *log_console;
#endif
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	IF_NOT_FEATURE_INIT_SYSLOG(G.log_console = VC_5;) \
} while (0)

enum {
	L_LOG = 0x1,
	L_CONSOLE = 0x2,
};

/* Print a message to the specified device.
 * "where" may be bitwise-or'd from L_LOG | L_CONSOLE
 * NB: careful, we can be called after vfork!
 */
#define dbg_message(...) do { if (DEBUG_INIT) message(__VA_ARGS__); } while (0)
static void message(int where, const char *fmt, ...)
	__attribute__ ((format(printf, 2, 3)));
static void message(int where, const char *fmt, ...)
{
	va_list arguments;
	unsigned l;
	char msg[128];

	msg[0] = '\r';
	va_start(arguments, fmt);
	l = 1 + vsnprintf(msg + 1, sizeof(msg) - 2, fmt, arguments);
	if (l > sizeof(msg) - 2)
		l = sizeof(msg) - 2;
	va_end(arguments);

#if ENABLE_FEATURE_INIT_SYSLOG
	msg[l] = '\0';
	if (where & L_LOG) {
		/* Log the message to syslogd */
		openlog(applet_name, 0, LOG_DAEMON);
		/* don't print "\r" */
		syslog(LOG_INFO, "%s", msg + 1);
		closelog();
	}
	msg[l++] = '\n';
	msg[l] = '\0';
#else
	msg[l++] = '\n';
	msg[l] = '\0';
	if (where & L_LOG) {
		/* Take full control of the log tty, and never close it.
		 * It's mine, all mine!  Muhahahaha! */
		static int log_fd = -1;

		if (log_fd < 0) {
			log_fd = STDERR_FILENO;
			if (G.log_console) {
				log_fd = device_open(G.log_console, O_WRONLY | O_NONBLOCK | O_NOCTTY);
				if (log_fd < 0) {
					bb_error_msg("can't log to %s", G.log_console);
					where = L_CONSOLE;
				} else {
					close_on_exec_on(log_fd);
				}
			}
		}
		full_write(log_fd, msg, l);
		if (log_fd == STDERR_FILENO)
			return; /* don't print dup messages */
	}
#endif

	if (where & L_CONSOLE) {
		/* Send console messages to console so people will see them. */
		full_write(STDERR_FILENO, msg, l);
	}
}

static void console_init(void)
{
#ifdef VT_OPENQRY
	int vtno;
#endif
	char *s;

	s = getenv("CONSOLE");
	if (!s)
		s = getenv("console");
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
	/* BSD people say their kernels do not open fd 0,1,2; they need this: */
	if (!s)
		s = (char*)"/dev/console";
#endif
	if (s) {
		int fd = open(s, O_RDWR | O_NONBLOCK | O_NOCTTY);
		if (fd >= 0) {
			dup2(fd, STDIN_FILENO);
			dup2(fd, STDOUT_FILENO);
			xmove_fd(fd, STDERR_FILENO);
		}
		dbg_message(L_LOG, "console='%s'", s);
	} else {
		/* Make sure fd 0,1,2 are not closed
		 * (so that they won't be used by future opens) */
		bb_sanitize_stdio();
// Users report problems
//		/* Make sure init can't be blocked by writing to stderr */
//		fcntl(STDERR_FILENO, F_SETFL, fcntl(STDERR_FILENO, F_GETFL) | O_NONBLOCK);
	}

	s = getenv("TERM");
#ifdef VT_OPENQRY
	if (ioctl(STDIN_FILENO, VT_OPENQRY, &vtno) != 0) {
		/* Not a linux terminal, probably serial console.
		 * Force the TERM setting to vt102
		 * if TERM is set to linux (the default) */
		if (!s || strcmp(s, "linux") == 0)
			putenv((char*)"TERM=vt102");
# if !ENABLE_FEATURE_INIT_SYSLOG
		G.log_console = NULL;
# endif
	} else
#endif
	if (!s)
		putenv((char*)"TERM=" CONFIG_INIT_TERMINAL_TYPE);
}

/* Set terminal settings to reasonable defaults.
 * NB: careful, we can be called after vfork! */
static void set_sane_term(void)
{
	struct termios tty;

	tcgetattr(STDIN_FILENO, &tty);

	/* set control chars */
	tty.c_cc[VINTR] = 3;	/* C-c */
	tty.c_cc[VQUIT] = 28;	/* C-\ */
	tty.c_cc[VERASE] = 127;	/* C-? */
	tty.c_cc[VKILL] = 21;	/* C-u */
	tty.c_cc[VEOF] = 4;	/* C-d */
	tty.c_cc[VSTART] = 17;	/* C-q */
	tty.c_cc[VSTOP] = 19;	/* C-s */
	tty.c_cc[VSUSP] = 26;	/* C-z */

#ifdef __linux__
	/* use line discipline 0 */
	tty.c_line = 0;
#endif

	/* Make it be sane */
#ifndef CRTSCTS
# define CRTSCTS 0
#endif
	/* added CRTSCTS to fix Debian bug 528560 */
	tty.c_cflag &= CBAUD | CBAUDEX | CSIZE | CSTOPB | PARENB | PARODD | CRTSCTS;
	tty.c_cflag |= CREAD | HUPCL | CLOCAL;

	/* input modes */
	tty.c_iflag = ICRNL | IXON | IXOFF;

	/* output modes */
	tty.c_oflag = OPOST | ONLCR;

	/* local modes */
	tty.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;

	tcsetattr_stdin_TCSANOW(&tty);
}

/* Open the new terminal device.
 * NB: careful, we can be called after vfork! */
static int open_stdio_to_tty(const char* tty_name)
{
	/* empty tty_name means "use init's tty", else... */
	if (tty_name[0]) {
		int fd;

		close(STDIN_FILENO);
		/* fd can be only < 0 or 0: */
		fd = device_open(tty_name, O_RDWR);
		if (fd) {
			message(L_LOG | L_CONSOLE, "can't open %s: "STRERROR_FMT,
				tty_name
				STRERROR_ERRNO
			);
			return 0; /* failure */
		}
		dup2(STDIN_FILENO, STDOUT_FILENO);
		dup2(STDIN_FILENO, STDERR_FILENO);
	}
	set_sane_term();
	return 1; /* success */
}

static void reset_sighandlers_and_unblock_sigs(void)
{
	bb_signals(0
		+ (1 << SIGUSR1)
		+ (1 << SIGUSR2)
		+ (1 << SIGTERM)
		+ (1 << SIGQUIT)
		+ (1 << SIGINT)
		+ (1 << SIGHUP)
		+ (1 << SIGTSTP)
		+ (1 << SIGSTOP)
		, SIG_DFL);
	sigprocmask_allsigs(SIG_UNBLOCK);
}

/* Wrapper around exec:
 * Takes string.
 * If chars like '>' detected, execs '[-]/bin/sh -c "exec ......."'.
 * Otherwise splits words on whitespace, deals with leading dash,
 * and uses plain exec().
 * NB: careful, we can be called after vfork!
 */
static void init_exec(const char *command)
{
	/* +8 allows to write VLA sizes below more efficiently: */
	unsigned command_size = strlen(command) + 8;
	/* strlen(command) + strlen("exec ")+1: */
	char buf[command_size];
	/* strlen(command) / 2 + 4: */
	char *cmd[command_size / 2];
	int dash;

	dash = (command[0] == '-' /* maybe? && command[1] == '/' */);
	command += dash;

	/* See if any special /bin/sh requiring characters are present */
	if (strpbrk(command, "~`!$^&*()=|\\{}[];\"'<>?") != NULL) {
		sprintf(buf, "exec %s", command); /* excluding "-" */
		/* NB: LIBBB_DEFAULT_LOGIN_SHELL define has leading dash */
		cmd[0] = (char*)(LIBBB_DEFAULT_LOGIN_SHELL + !dash);
		cmd[1] = (char*)"-c";
		cmd[2] = buf;
		cmd[3] = NULL;
		command = LIBBB_DEFAULT_LOGIN_SHELL + 1;
	} else {
		/* Convert command (char*) into cmd (char**, one word per string) */
		char *word, *next;
		int i = 0;
		next = strcpy(buf, command - dash); /* command including "-" */
		command = next + dash;
		while ((word = strsep(&next, " \t")) != NULL) {
			if (*word != '\0') { /* not two spaces/tabs together? */
				cmd[i] = word;
				i++;
			}
		}
		cmd[i] = NULL;
	}
	/* If we saw leading "-", it is interactive shell.
	 * Try harder to give it a controlling tty.
	 */
	if (ENABLE_FEATURE_INIT_SCTTY && dash) {
		/* _Attempt_ to make stdin a controlling tty. */
		ioctl(STDIN_FILENO, TIOCSCTTY, 0 /*only try, don't steal*/);
	}
	/* Here command never contains the dash, cmd[0] might */
	BB_EXECVP(command, cmd);
	message(L_LOG | L_CONSOLE, "can't run '%s': "STRERROR_FMT, command STRERROR_ERRNO);
	/* returns if execvp fails */
}

/* Used only by run_actions */
static pid_t run(const struct init_action *a)
{
	pid_t pid;

	/* Careful: don't be affected by a signal in vforked child */
	sigprocmask_allsigs(SIG_BLOCK);
	if (BB_MMU && (a->action_type & ASKFIRST))
		pid = fork();
	else
		pid = vfork();
	if (pid < 0)
		message(L_LOG | L_CONSOLE, "can't fork");
	if (pid) {
		sigprocmask_allsigs(SIG_UNBLOCK);
		return pid; /* Parent or error */
	}

	/* Child */

	/* Reset signal handlers that were set by the parent process */
	reset_sighandlers_and_unblock_sigs();

	/* Create a new session and make ourself the process group leader */
	setsid();

	/* Open the new terminal device */
	if (!open_stdio_to_tty(a->terminal))
		_exit(EXIT_FAILURE);

	/* NB: on NOMMU we can't wait for input in child, so
	 * "askfirst" will work the same as "respawn". */
	if (BB_MMU && (a->action_type & ASKFIRST)) {
		static const char press_enter[] ALIGN1 =
#ifdef CUSTOMIZED_BANNER
#include CUSTOMIZED_BANNER
#endif
			"\nPlease press Enter to activate this console. ";
		char c;
		/*
		 * Save memory by not exec-ing anything large (like a shell)
		 * before the user wants it. This is critical if swap is not
		 * enabled and the system has low memory. Generally this will
		 * be run on the second virtual console, and the first will
		 * be allowed to start a shell or whatever an init script
		 * specifies.
		 */
		dbg_message(L_LOG, "waiting for enter to start '%s'"
					"(pid %d, tty '%s')\n",
				a->command, getpid(), a->terminal);
		full_write(STDOUT_FILENO, press_enter, sizeof(press_enter) - 1);
		while (safe_read(STDIN_FILENO, &c, 1) == 1 && c != '\n')
			continue;
	}

	/*
	 * When a file named /.init_enable_core exists, setrlimit is called
	 * before processes are spawned to set core file size as unlimited.
	 * This is for debugging only.  Don't use this is production, unless
	 * you want core dumps lying about....
	 */
	if (ENABLE_FEATURE_INIT_COREDUMPS) {
		if (access("/.init_enable_core", F_OK) == 0) {
			struct rlimit limit;
			limit.rlim_cur = RLIM_INFINITY;
			limit.rlim_max = RLIM_INFINITY;
			setrlimit(RLIMIT_CORE, &limit);
		}
	}

	/* Log the process name and args */
	message(L_LOG, "starting pid %u, tty '%s': '%s'",
			(int)getpid(), a->terminal, a->command);

	/* Now run it.  The new program will take over this PID,
	 * so nothing further in init.c should be run. */
	init_exec(a->command);
	/* We're still here?  Some error happened. */
	_exit(-1);
}

static struct init_action *mark_terminated(pid_t pid)
{
	struct init_action *a;

	if (pid > 0) {
		update_utmp_DEAD_PROCESS(pid);
		for (a = G.init_action_list; a; a = a->next) {
			if (a->pid == pid) {
				a->pid = 0;
				return a;
			}
		}
	}
	return NULL;
}

static void waitfor(pid_t pid)
{
	/* waitfor(run(x)): protect against failed fork inside run() */
	if (pid <= 0)
		return;

	/* Wait for any child (prevent zombies from exiting orphaned processes)
	 * but exit the loop only when specified one has exited. */
	while (1) {
		pid_t wpid = wait(NULL);
		mark_terminated(wpid);
		/* Unsafe. SIGTSTP handler might have wait'ed it already */
		/*if (wpid == pid) break;*/
		/* More reliable: */
		if (kill(pid, 0))
			break;
	}
}

/* Run all commands of a particular type */
static void run_actions(int action_type)
{
	struct init_action *a;

	for (a = G.init_action_list; a; a = a->next) {
		if (!(a->action_type & action_type))
			continue;

		if (a->action_type & (SYSINIT | WAIT | ONCE | CTRLALTDEL | SHUTDOWN)) {
			pid_t pid = run(a);
			if (a->action_type & (SYSINIT | WAIT | CTRLALTDEL | SHUTDOWN))
				waitfor(pid);
		}
		if (a->action_type & (RESPAWN | ASKFIRST)) {
			/* Only run stuff with pid == 0. If pid != 0,
			 * it is already running
			 */
			if (a->pid == 0)
				a->pid = run(a);
		}
	}
}

static void new_init_action(uint8_t action_type, const char *command, const char *cons)
{
	struct init_action *a, **nextp;

	/* Scenario:
	 * old inittab:
	 * ::shutdown:umount -a -r
	 * ::shutdown:swapoff -a
	 * new inittab:
	 * ::shutdown:swapoff -a
	 * ::shutdown:umount -a -r
	 * On reload, we must ensure entries end up in correct order.
	 * To achieve that, if we find a matching entry, we move it
	 * to the end.
	 */
	nextp = &G.init_action_list;
	while ((a = *nextp) != NULL) {
		/* Don't enter action if it's already in the list.
		 * This prevents losing running RESPAWNs.
		 */
		if (strcmp(a->command, command) == 0
		 && strcmp(a->terminal, cons) == 0
		) {
			/* Remove from list */
			*nextp = a->next;
			/* Find the end of the list */
			while (*nextp != NULL)
				nextp = &(*nextp)->next;
			a->next = NULL;
			goto append;
		}
		nextp = &a->next;
	}

	a = xzalloc(sizeof(*a) + strlen(command));

	/* Append to the end of the list */
 append:
	*nextp = a;
	a->action_type = action_type;
	strcpy(a->command, command);
	safe_strncpy(a->terminal, cons, sizeof(a->terminal));
	dbg_message(L_LOG | L_CONSOLE, "command='%s' action=%x tty='%s'\n",
		a->command, a->action_type, a->terminal);
}

/* NOTE that if CONFIG_FEATURE_USE_INITTAB is NOT defined,
 * then parse_inittab() simply adds in some default
 * actions (i.e., runs INIT_SCRIPT and then starts a pair
 * of "askfirst" shells).  If CONFIG_FEATURE_USE_INITTAB
 * _is_ defined, but /etc/inittab is missing, this
 * results in the same set of default behaviors.
 */
static void parse_inittab(void)
{
#if ENABLE_FEATURE_USE_INITTAB
	char *token[4];
	parser_t *parser = config_open2("/etc/inittab", fopen_for_read);

	if (parser == NULL)
#endif
	{
		/* No inittab file - set up some default behavior */
		/* Sysinit */
		new_init_action(SYSINIT, INIT_SCRIPT, "");
		/* Askfirst shell on tty1-4 */
		new_init_action(ASKFIRST, bb_default_login_shell, "");
//TODO: VC_1 instead of ""? "" is console -> ctty problems -> angry users
		new_init_action(ASKFIRST, bb_default_login_shell, VC_2);
		new_init_action(ASKFIRST, bb_default_login_shell, VC_3);
		new_init_action(ASKFIRST, bb_default_login_shell, VC_4);
		/* Reboot on Ctrl-Alt-Del */
		new_init_action(CTRLALTDEL, "reboot", "");
		/* Umount all filesystems on halt/reboot */
		new_init_action(SHUTDOWN, "umount -a -r", "");
		/* Swapoff on halt/reboot */
		new_init_action(SHUTDOWN, "swapoff -a", "");
		/* Restart init when a QUIT is received */
		new_init_action(RESTART, "init", "");
		return;
	}

#if ENABLE_FEATURE_USE_INITTAB
	/* optional_tty:ignored_runlevel:action:command
	 * Delims are not to be collapsed and need exactly 4 tokens
	 */
	while (config_read(parser, token, 4, 0, "#:",
				PARSE_NORMAL & ~(PARSE_TRIM | PARSE_COLLAPSE))) {
		/* order must correspond to SYSINIT..RESTART constants */
		static const char actions[] ALIGN1 =
			"sysinit\0""wait\0""once\0""respawn\0""askfirst\0"
			"ctrlaltdel\0""shutdown\0""restart\0";
		int action;
		char *tty = token[0];

		if (!token[3]) /* less than 4 tokens */
			goto bad_entry;
		action = index_in_strings(actions, token[2]);
		if (action < 0 || !token[3][0]) /* token[3]: command */
			goto bad_entry;
		/* turn .*TTY -> /dev/TTY */
		if (tty[0]) {
			tty = concat_path_file("/dev/", skip_dev_pfx(tty));
		}
		new_init_action(1 << action, token[3], tty);
		if (tty[0])
			free(tty);
		continue;
 bad_entry:
		message(L_LOG | L_CONSOLE, "Bad inittab entry at line %d",
				parser->lineno);
	}
	config_close(parser);
#endif
}

static void pause_and_low_level_reboot(unsigned magic) NORETURN;
static void pause_and_low_level_reboot(unsigned magic)
{
	pid_t pid;

	/* Allow time for last message to reach serial console, etc */
	sleep(1);

	/* We have to fork here, since the kernel calls do_exit(EXIT_SUCCESS)
	 * in linux/kernel/sys.c, which can cause the machine to panic when
	 * the init process exits... */
	pid = vfork();
	if (pid == 0) { /* child */
		reboot(magic);
		_exit(EXIT_SUCCESS);
	}
	while (1)
		sleep(1);
}

static void run_shutdown_and_kill_processes(void)
{
	/* Run everything to be run at "shutdown".  This is done _prior_
	 * to killing everything, in case people wish to use scripts to
	 * shut things down gracefully... */
	run_actions(SHUTDOWN);

	message(L_CONSOLE | L_LOG, "The system is going down NOW!");

	/* Send signals to every process _except_ pid 1 */
	kill(-1, SIGTERM);
	message(L_CONSOLE, "Sent SIG%s to all processes", "TERM");
	sync();
	sleep(1);

	kill(-1, SIGKILL);
	message(L_CONSOLE, "Sent SIG%s to all processes", "KILL");
	sync();
	/*sleep(1); - callers take care about making a pause */
}

/* Signal handling by init:
 *
 * For process with PID==1, on entry kernel sets all signals to SIG_DFL
 * and unmasks all signals. However, for process with PID==1,
 * default action (SIG_DFL) on any signal is to ignore it,
 * even for special signals SIGKILL and SIGCONT.
 * Also, any signal can be caught or blocked.
 * (but SIGSTOP is still handled specially, at least in 2.6.20)
 *
 * We install two kinds of handlers, "immediate" and "delayed".
 *
 * Immediate handlers execute at any time, even while, say, sysinit
 * is running.
 *
 * Delayed handlers just set a flag variable. The variable is checked
 * in the main loop and acted upon.
 *
 * halt/poweroff/reboot and restart have immediate handlers.
 * They only traverse linked list of struct action's, never modify it,
 * this should be safe to do even in signal handler. Also they
 * never return.
 *
 * SIGSTOP and SIGTSTP have immediate handlers. They just wait
 * for SIGCONT to happen.
 *
 * SIGHUP has a delayed handler, because modifying linked list
 * of struct action's from a signal handler while it is manipulated
 * by the program may be disastrous.
 *
 * Ctrl-Alt-Del has a delayed handler. Not a must, but allowing
 * it to happen even somewhere inside "sysinit" would be a bit awkward.
 *
 * There is a tiny probability that SIGHUP and Ctrl-Alt-Del will collide
 * and only one will be remembered and acted upon.
 */

/* The SIGPWR/SIGUSR[12]/SIGTERM handler */
static void halt_reboot_pwoff(int sig) NORETURN;
static void halt_reboot_pwoff(int sig)
{
	const char *m;
	unsigned rb;

	/* We may call run() and it unmasks signals,
	 * including the one masked inside this signal handler.
	 * Testcase which would start multiple reboot scripts:
	 *  while true; do reboot; done
	 * Preventing it:
	 */
	reset_sighandlers_and_unblock_sigs();

	run_shutdown_and_kill_processes();

	m = "halt";
	rb = RB_HALT_SYSTEM;
	if (sig == SIGTERM) {
		m = "reboot";
		rb = RB_AUTOBOOT;
	} else if (sig == SIGUSR2) {
		m = "poweroff";
		rb = RB_POWER_OFF;
	}
	message(L_CONSOLE, "Requesting system %s", m);
	pause_and_low_level_reboot(rb);
	/* not reached */
}

/* Handler for QUIT - exec "restart" action,
 * else (no such action defined) do nothing */
static void exec_restart_action(void)
{
	struct init_action *a;

	for (a = G.init_action_list; a; a = a->next) {
		if (!(a->action_type & RESTART))
			continue;

		/* Starting from here, we won't return.
		 * Thus don't need to worry about preserving errno
		 * and such.
		 */

		reset_sighandlers_and_unblock_sigs();

		run_shutdown_and_kill_processes();

#ifdef RB_ENABLE_CAD
		/* Allow Ctrl-Alt-Del to reboot the system.
		 * This is how kernel sets it up for init, we follow suit.
		 */
		reboot(RB_ENABLE_CAD); /* misnomer */
#endif

		if (open_stdio_to_tty(a->terminal)) {
			dbg_message(L_CONSOLE, "Trying to re-exec %s", a->command);
			/* Theoretically should be safe.
			 * But in practice, kernel bugs may leave
			 * unkillable processes, and wait() may block forever.
			 * Oh well. Hoping "new" init won't be too surprised
			 * by having children it didn't create.
			 */
			//while (wait(NULL) > 0)
			//	continue;
			init_exec(a->command);
		}
		/* Open or exec failed */
		pause_and_low_level_reboot(RB_HALT_SYSTEM);
		/* not reached */
	}
}

/* The SIGSTOP/SIGTSTP handler
 * NB: inside it, all signals except SIGCONT are masked
 * via appropriate setup in sigaction().
 */
static void stop_handler(int sig UNUSED_PARAM)
{
	smallint saved_bb_got_signal;
	int saved_errno;

	saved_bb_got_signal = bb_got_signal;
	saved_errno = errno;
	signal(SIGCONT, record_signo);

	while (1) {
		pid_t wpid;

		if (bb_got_signal == SIGCONT)
			break;
		/* NB: this can accidentally wait() for a process
		 * which we waitfor() elsewhere! waitfor() must have
		 * code which is resilient against this.
		 */
		wpid = wait_any_nohang(NULL);
		mark_terminated(wpid);
		sleep(1);
	}

	signal(SIGCONT, SIG_DFL);
	errno = saved_errno;
	bb_got_signal = saved_bb_got_signal;
}

#if ENABLE_FEATURE_USE_INITTAB
static void reload_inittab(void)
{
	struct init_action *a, **nextp;

	message(L_LOG, "reloading /etc/inittab");

	/* Disable old entries */
	for (a = G.init_action_list; a; a = a->next)
		a->action_type = 0;

	/* Append new entries, or modify existing entries
	 * (incl. setting a->action_type) if cmd and device name
	 * match new ones. End result: only entries with
	 * a->action_type == 0 are stale.
	 */
	parse_inittab();

#if ENABLE_FEATURE_KILL_REMOVED
	/* Kill stale entries */
	/* Be nice and send SIGTERM first */
	for (a = G.init_action_list; a; a = a->next)
		if (a->action_type == 0 && a->pid != 0)
			kill(a->pid, SIGTERM);
	if (CONFIG_FEATURE_KILL_DELAY) {
		/* NB: parent will wait in NOMMU case */
		if ((BB_MMU ? fork() : vfork()) == 0) { /* child */
			sleep(CONFIG_FEATURE_KILL_DELAY);
			for (a = G.init_action_list; a; a = a->next)
				if (a->action_type == 0 && a->pid != 0)
					kill(a->pid, SIGKILL);
			_exit(EXIT_SUCCESS);
		}
	}
#endif

	/* Remove stale entries and SYSINIT entries.
	 * We never rerun SYSINIT entries anyway,
	 * removing them too saves a few bytes
	 */
	nextp = &G.init_action_list;
	while ((a = *nextp) != NULL) {
		/*
		 * Why pid == 0 check?
		 * Process can be removed from inittab and added *later*.
		 * If we delete its entry but process still runs,
		 * duplicate is spawned when the entry is re-added.
		 */
		if ((a->action_type & ~SYSINIT) == 0 && a->pid == 0) {
			*nextp = a->next;
			free(a);
		} else {
			nextp = &a->next;
		}
	}

	/* Not needed: */
	/* run_actions(RESPAWN | ASKFIRST); */
	/* - we return to main loop, which does this automagically */
}
#endif

static int check_delayed_sigs(void)
{
	int sigs_seen = 0;

	while (1) {
		smallint sig = bb_got_signal;

		if (!sig)
			return sigs_seen;
		bb_got_signal = 0;
		sigs_seen = 1;
#if ENABLE_FEATURE_USE_INITTAB
		if (sig == SIGHUP)
			reload_inittab();
#endif
		if (sig == SIGINT)
			run_actions(CTRLALTDEL);
		if (sig == SIGQUIT) {
			exec_restart_action();
			/* returns only if no restart action defined */
		}
		if ((1 << sig) & (0
#ifdef SIGPWR
		    + (1 << SIGPWR)
#endif
		    + (1 << SIGUSR1)
		    + (1 << SIGUSR2)
		    + (1 << SIGTERM)
		)) {
			halt_reboot_pwoff(sig);
		}
	}
}

#if DEBUG_SEGV_HANDLER
static
void handle_sigsegv(int sig, siginfo_t *info, void *ucontext)
{
	long ip;
	ucontext_t *uc;

	uc = ucontext;
	ip = uc->uc_mcontext.gregs[REG_EIP];
	fdprintf(2, "signal:%d address:0x%lx ip:0x%lx\n",
			sig,
			/* this is void*, but using %p would print "(null)"
			 * even for ptrs which are not exactly 0, but, say, 0x123:
			 */
			(long)info->si_addr,
			ip);
	{
		/* glibc extension */
		void *array[50];
		int size;
		size = backtrace(array, 50);
		backtrace_symbols_fd(array, size, 2);
	}
	for (;;) sleep(9999);
}
#endif

static void sleep_much(void)
{
        sleep(30 * 24*60*60);
}

int init_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int init_main(int argc UNUSED_PARAM, char **argv)
{
	INIT_G();

	if (argv[1] && strcmp(argv[1], "-q") == 0) {
		return kill(1, SIGHUP);
	}

#if DEBUG_SEGV_HANDLER
	{
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = handle_sigsegv;
		sa.sa_flags = SA_SIGINFO;
		sigaction(SIGSEGV, &sa, NULL);
		sigaction(SIGILL, &sa, NULL);
		sigaction(SIGFPE, &sa, NULL);
		sigaction(SIGBUS, &sa, NULL);
	}
#endif

	if (!DEBUG_INIT) {
		/* Some users send poweroff signals to init VERY early.
		 * To handle this, mask signals early,
		 * and unmask them only after signal handlers are installed.
		 */
		sigprocmask_allsigs(SIG_BLOCK);

		/* Expect to be invoked as init with PID=1 or be invoked as linuxrc */
		if (getpid() != 1
		 && (!ENABLE_LINUXRC || applet_name[0] != 'l') /* not linuxrc? */
		) {
			bb_error_msg_and_die("must be run as PID 1");
		}
#ifdef RB_DISABLE_CAD
		/* Turn off rebooting via CTL-ALT-DEL - we get a
		 * SIGINT on CAD so we can shut things down gracefully... */
		reboot(RB_DISABLE_CAD); /* misnomer */
#endif
	}

	/* If, say, xmalloc would ever die, we don't want to oops kernel
	 * by exiting.
	 * NB: we set die_func *after* PID 1 check and bb_show_usage.
	 * Otherwise, for example, "init u" ("please rexec yourself"
	 * command for sysvinit) will show help text (which isn't too bad),
	 * *and sleep forever* (which is bad!)
	 */
	die_func = sleep_much;

	/* Figure out where the default console should be */
	console_init();
	set_sane_term();
	xchdir("/");
	setsid();

	/* Make sure environs is set to something sane */
	putenv((char *) "HOME=/");
	putenv((char *) bb_PATH_root_path);
	putenv((char *) "SHELL=/bin/sh");
	putenv((char *) "USER=root"); /* needed? why? */

	if (argv[1])
		xsetenv("RUNLEVEL", argv[1]);

#if !ENABLE_FEATURE_INIT_QUIET
	/* Hello world */
	message(L_CONSOLE | L_LOG, "init started: %s", bb_banner);
#endif

	/* Check if we are supposed to be in single user mode */
	if (argv[1]
	 && (strcmp(argv[1], "single") == 0 || strcmp(argv[1], "-s") == 0 || LONE_CHAR(argv[1], '1'))
	) {
		/* ??? shouldn't we set RUNLEVEL="b" here? */
		/* Start a shell on console */
		new_init_action(RESPAWN, bb_default_login_shell, "");
	} else {
		/* Not in single user mode - see what inittab says */

		/* NOTE that if CONFIG_FEATURE_USE_INITTAB is NOT defined,
		 * then parse_inittab() simply adds in some default
		 * actions (i.e., INIT_SCRIPT and a pair
		 * of "askfirst" shells) */
		parse_inittab();
	}

#if ENABLE_SELINUX
	if (getenv("SELINUX_INIT") == NULL) {
		int enforce = 0;

		putenv((char*)"SELINUX_INIT=YES");
		if (selinux_init_load_policy(&enforce) == 0) {
			BB_EXECVP(argv[0], argv);
		} else if (enforce > 0) {
			/* SELinux in enforcing mode but load_policy failed */
			message(L_CONSOLE, "can't load SELinux Policy. "
				"Machine is in enforcing mode. Halting now.");
			return EXIT_FAILURE;
		}
	}
#endif

	if (ENABLE_FEATURE_INIT_MODIFY_CMDLINE) {
		/* Make the command line just say "init"  - that's all, nothing else */
		strncpy(argv[0], "init", strlen(argv[0]));
		/* Wipe argv[1]-argv[N] so they don't clutter the ps listing */
		while (*++argv)
			nuke_str(*argv);
	}

	/* Set up signal handlers */
	if (!DEBUG_INIT) {
		struct sigaction sa;

		/* Stop handler must allow only SIGCONT inside itself */
		memset(&sa, 0, sizeof(sa));
		sigfillset(&sa.sa_mask);
		sigdelset(&sa.sa_mask, SIGCONT);
		sa.sa_handler = stop_handler;
		/* NB: sa_flags doesn't have SA_RESTART.
		 * It must be able to interrupt wait().
		 */
		sigaction_set(SIGTSTP, &sa); /* pause */
		/* Does not work as intended, at least in 2.6.20.
		 * SIGSTOP is simply ignored by init:
		 */
		sigaction_set(SIGSTOP, &sa); /* pause */

		/* These signals must interrupt wait(),
		 * setting handler without SA_RESTART flag.
		 */
		bb_signals_recursive_norestart(0
			+ (1 << SIGINT)  /* Ctrl-Alt-Del */
			+ (1 << SIGQUIT) /* re-exec another init */
#ifdef SIGPWR
			+ (1 << SIGPWR)  /* halt */
#endif
			+ (1 << SIGUSR1) /* halt */
			+ (1 << SIGTERM) /* reboot */
			+ (1 << SIGUSR2) /* poweroff */
#if ENABLE_FEATURE_USE_INITTAB
			+ (1 << SIGHUP)  /* reread /etc/inittab */
#endif
			, record_signo);

		sigprocmask_allsigs(SIG_UNBLOCK);
	}

	/* Now run everything that needs to be run */
	/* First run the sysinit command */
	run_actions(SYSINIT);
	check_delayed_sigs();
	/* Next run anything that wants to block */
	run_actions(WAIT);
	check_delayed_sigs();
	/* Next run anything to be run only once */
	run_actions(ONCE);

	/* Now run the looping stuff for the rest of forever.
	 */
	while (1) {
		int maybe_WNOHANG;

		maybe_WNOHANG = check_delayed_sigs();

		/* (Re)run the respawn/askfirst stuff */
		run_actions(RESPAWN | ASKFIRST);
		maybe_WNOHANG |= check_delayed_sigs();

		/* Don't consume all CPU time - sleep a bit */
		sleep(1);
		maybe_WNOHANG |= check_delayed_sigs();

		/* Wait for any child process(es) to exit.
		 *
		 * If check_delayed_sigs above reported that a signal
		 * was caught, wait will be nonblocking. This ensures
		 * that if SIGHUP has reloaded inittab, respawn and askfirst
		 * actions will not be delayed until next child death.
		 */
		if (maybe_WNOHANG)
			maybe_WNOHANG = WNOHANG;
		while (1) {
			pid_t wpid;
			struct init_action *a;

			/* If signals happen _in_ the wait, they interrupt it,
			 * bb_signals_recursive_norestart set them up that way
			 */
			wpid = waitpid(-1, NULL, maybe_WNOHANG);
			if (wpid <= 0)
				break;

			a = mark_terminated(wpid);
			if (a) {
				message(L_LOG, "process '%s' (pid %d) exited. "
						"Scheduling for restart.",
						a->command, wpid);
			}
			/* See if anyone else is waiting to be reaped */
			maybe_WNOHANG = WNOHANG;
		}
	} /* while (1) */
}

//usage:#define linuxrc_trivial_usage NOUSAGE_STR
//usage:#define linuxrc_full_usage ""

//usage:#define init_trivial_usage
//usage:       ""
//usage:#define init_full_usage "\n\n"
//usage:       "Init is the first process started during boot. It never exits."
//usage:	IF_FEATURE_USE_INITTAB(
//usage:   "\n""It (re)spawns children according to /etc/inittab."
//usage:	)
//usage:	IF_NOT_FEATURE_USE_INITTAB(
//usage:   "\n""This version of init doesn't use /etc/inittab,"
//usage:   "\n""has fixed set of processed to run."
//usage:	)
//usage:
//usage:#define init_notes_usage
//usage:	"This version of init is designed to be run only by the kernel.\n"
//usage:	"\n"
//usage:	"BusyBox init doesn't support multiple runlevels. The runlevels field of\n"
//usage:	"the /etc/inittab file is completely ignored by BusyBox init. If you want\n"
//usage:	"runlevels, use sysvinit.\n"
//usage:	"\n"
//usage:	"BusyBox init works just fine without an inittab. If no inittab is found,\n"
//usage:	"it has the following default behavior:\n"
//usage:	"\n"
//usage:	"	::sysinit:/etc/init.d/rcS\n"
//usage:	"	::askfirst:/bin/sh\n"
//usage:	"	::ctrlaltdel:/sbin/reboot\n"
//usage:	"	::shutdown:/sbin/swapoff -a\n"
//usage:	"	::shutdown:/bin/umount -a -r\n"
//usage:	"	::restart:/sbin/init\n"
//usage:	"	tty2::askfirst:/bin/sh\n"
//usage:	"	tty3::askfirst:/bin/sh\n"
//usage:	"	tty4::askfirst:/bin/sh\n"
//usage:	"\n"
//usage:	"If you choose to use an /etc/inittab file, the inittab entry format is as follows:\n"
//usage:	"\n"
//usage:	"	<id>:<runlevels>:<action>:<process>\n"
//usage:	"\n"
//usage:	"	<id>:\n"
//usage:	"\n"
//usage:	"		WARNING: This field has a non-traditional meaning for BusyBox init!\n"
//usage:	"		The id field is used by BusyBox init to specify the controlling tty for\n"
//usage:	"		the specified process to run on. The contents of this field are\n"
//usage:	"		appended to \"/dev/\" and used as-is. There is no need for this field to\n"
//usage:	"		be unique, although if it isn't you may have strange results. If this\n"
//usage:	"		field is left blank, then the init's stdin/out will be used.\n"
//usage:	"\n"
//usage:	"	<runlevels>:\n"
//usage:	"\n"
//usage:	"		The runlevels field is completely ignored.\n"
//usage:	"\n"
//usage:	"	<action>:\n"
//usage:	"\n"
//usage:	"		Valid actions include: sysinit, respawn, askfirst, wait,\n"
//usage:	"		once, restart, ctrlaltdel, and shutdown.\n"
//usage:	"\n"
//usage:	"		The available actions can be classified into two groups: actions\n"
//usage:	"		that are run only once, and actions that are re-run when the specified\n"
//usage:	"		process exits.\n"
//usage:	"\n"
//usage:	"		Run only-once actions:\n"
//usage:	"\n"
//usage:	"			'sysinit' is the first item run on boot. init waits until all\n"
//usage:	"			sysinit actions are completed before continuing. Following the\n"
//usage:	"			completion of all sysinit actions, all 'wait' actions are run.\n"
//usage:	"			'wait' actions, like 'sysinit' actions, cause init to wait until\n"
//usage:	"			the specified task completes. 'once' actions are asynchronous,\n"
//usage:	"			therefore, init does not wait for them to complete. 'restart' is\n"
//usage:	"			the action taken to restart the init process. By default this should\n"
//usage:	"			simply run /sbin/init, but can be a script which runs pivot_root or it\n"
//usage:	"			can do all sorts of other interesting things. The 'ctrlaltdel' init\n"
//usage:	"			actions are run when the system detects that someone on the system\n"
//usage:	"			console has pressed the CTRL-ALT-DEL key combination. Typically one\n"
//usage:	"			wants to run 'reboot' at this point to cause the system to reboot.\n"
//usage:	"			Finally the 'shutdown' action specifies the actions to taken when\n"
//usage:	"			init is told to reboot. Unmounting filesystems and disabling swap\n"
//usage:	"			is a very good here.\n"
//usage:	"\n"
//usage:	"		Run repeatedly actions:\n"
//usage:	"\n"
//usage:	"			'respawn' actions are run after the 'once' actions. When a process\n"
//usage:	"			started with a 'respawn' action exits, init automatically restarts\n"
//usage:	"			it. Unlike sysvinit, BusyBox init does not stop processes from\n"
//usage:	"			respawning out of control. The 'askfirst' actions acts just like\n"
//usage:	"			respawn, except that before running the specified process it\n"
//usage:	"			displays the line \"Please press Enter to activate this console.\"\n"
//usage:	"			and then waits for the user to press enter before starting the\n"
//usage:	"			specified process.\n"
//usage:	"\n"
//usage:	"		Unrecognized actions (like initdefault) will cause init to emit an\n"
//usage:	"		error message, and then go along with its business. All actions are\n"
//usage:	"		run in the order they appear in /etc/inittab.\n"
//usage:	"\n"
//usage:	"	<process>:\n"
//usage:	"\n"
//usage:	"		Specifies the process to be executed and its command line.\n"
//usage:	"\n"
//usage:	"Example /etc/inittab file:\n"
//usage:	"\n"
//usage:	"	# This is run first except when booting in single-user mode\n"
//usage:	"	#\n"
//usage:	"	::sysinit:/etc/init.d/rcS\n"
//usage:	"	\n"
//usage:	"	# /bin/sh invocations on selected ttys\n"
//usage:	"	#\n"
//usage:	"	# Start an \"askfirst\" shell on the console (whatever that may be)\n"
//usage:	"	::askfirst:-/bin/sh\n"
//usage:	"	# Start an \"askfirst\" shell on /dev/tty2-4\n"
//usage:	"	tty2::askfirst:-/bin/sh\n"
//usage:	"	tty3::askfirst:-/bin/sh\n"
//usage:	"	tty4::askfirst:-/bin/sh\n"
//usage:	"	\n"
//usage:	"	# /sbin/getty invocations for selected ttys\n"
//usage:	"	#\n"
//usage:	"	tty4::respawn:/sbin/getty 38400 tty4\n"
//usage:	"	tty5::respawn:/sbin/getty 38400 tty5\n"
//usage:	"	\n"
//usage:	"	\n"
//usage:	"	# Example of how to put a getty on a serial line (for a terminal)\n"
//usage:	"	#\n"
//usage:	"	#::respawn:/sbin/getty -L ttyS0 9600 vt100\n"
//usage:	"	#::respawn:/sbin/getty -L ttyS1 9600 vt100\n"
//usage:	"	#\n"
//usage:	"	# Example how to put a getty on a modem line\n"
//usage:	"	#::respawn:/sbin/getty 57600 ttyS2\n"
//usage:	"	\n"
//usage:	"	# Stuff to do when restarting the init process\n"
//usage:	"	::restart:/sbin/init\n"
//usage:	"	\n"
//usage:	"	# Stuff to do before rebooting\n"
//usage:	"	::ctrlaltdel:/sbin/reboot\n"
//usage:	"	::shutdown:/bin/umount -a -r\n"
//usage:	"	::shutdown:/sbin/swapoff -a\n"
