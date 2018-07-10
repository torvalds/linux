/*
Copyright (c) 2001-2006, Gerrit Pape
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. The name of the author may not be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* Busyboxed by Denys Vlasenko <vda.linux@googlemail.com> */

//config:config RUNSVDIR
//config:	bool "runsvdir (6 kb)"
//config:	default y
//config:	help
//config:	runsvdir starts a runsv process for each subdirectory, or symlink to
//config:	a directory, in the services directory dir, up to a limit of 1000
//config:	subdirectories, and restarts a runsv process if it terminates.
//config:
//config:config FEATURE_RUNSVDIR_LOG
//config:	bool "Enable scrolling argument log"
//config:	depends on RUNSVDIR
//config:	default n
//config:	help
//config:	Enable feature where second parameter of runsvdir holds last error
//config:	message (viewable via top/ps). Otherwise (feature is off
//config:	or no parameter), error messages go to stderr only.

//applet:IF_RUNSVDIR(APPLET(runsvdir, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_RUNSVDIR) += runsvdir.o

//usage:#define runsvdir_trivial_usage
//usage:       "[-P] [-s SCRIPT] DIR"
//usage:#define runsvdir_full_usage "\n\n"
//usage:       "Start a runsv process for each subdirectory. If it exits, restart it.\n"
//usage:     "\n	-P		Put each runsv in a new session"
//usage:     "\n	-s SCRIPT	Run SCRIPT <signo> after signal is processed"

#include <sys/file.h>
#include "libbb.h"
#include "common_bufsiz.h"
#include "runit_lib.h"

#define MAXSERVICES 1000

/* Should be not needed - all dirs are on same FS, right? */
#define CHECK_DEVNO_TOO 0

struct service {
#if CHECK_DEVNO_TOO
	dev_t dev;
#endif
	ino_t ino;
	pid_t pid;
	smallint isgone;
};

struct globals {
	struct service *sv;
	char *svdir;
	int svnum;
#if ENABLE_FEATURE_RUNSVDIR_LOG
	char *rplog;
	struct fd_pair logpipe;
	struct pollfd pfd[1];
	unsigned stamplog;
#endif
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define sv          (G.sv          )
#define svdir       (G.svdir       )
#define svnum       (G.svnum       )
#define rplog       (G.rplog       )
#define logpipe     (G.logpipe     )
#define pfd         (G.pfd         )
#define stamplog    (G.stamplog    )
#define INIT_G() do { setup_common_bufsiz(); } while (0)

static void fatal2_cannot(const char *m1, const char *m2)
{
	bb_perror_msg_and_die("%s: fatal: can't %s%s", svdir, m1, m2);
	/* was exiting 100 */
}
static void warn3x(const char *m1, const char *m2, const char *m3)
{
	bb_error_msg("%s: warning: %s%s%s", svdir, m1, m2, m3);
}
static void warn2_cannot(const char *m1, const char *m2)
{
	warn3x("can't ", m1, m2);
}
#if ENABLE_FEATURE_RUNSVDIR_LOG
static void warnx(const char *m1)
{
	warn3x(m1, "", "");
}
#endif

/* inlining + vfork -> bigger code */
static NOINLINE pid_t runsv(const char *name)
{
	pid_t pid;

	/* If we got signaled, stop spawning children at once! */
	if (bb_got_signal)
		return 0;

	pid = vfork();
	if (pid == -1) {
		warn2_cannot("vfork", "");
		return 0;
	}
	if (pid == 0) {
		/* child */
		if (option_mask32 & 1) /* -P option? */
			setsid();
/* man execv:
 * "Signals set to be caught by the calling process image
 *  shall be set to the default action in the new process image."
 * Therefore, we do not need this: */
#if 0
		bb_signals(0
			| (1 << SIGHUP)
			| (1 << SIGTERM)
			, SIG_DFL);
#endif
		execlp("runsv", "runsv", name, (char *) NULL);
		fatal2_cannot("start runsv ", name);
	}
	return pid;
}

/* gcc 4.3.0 does better with NOINLINE */
static NOINLINE int do_rescan(void)
{
	DIR *dir;
	struct dirent *d;
	int i;
	struct stat s;
	int need_rescan = 0;

	dir = opendir(".");
	if (!dir) {
		warn2_cannot("open directory ", svdir);
		return 1; /* need to rescan again soon */
	}
	for (i = 0; i < svnum; i++)
		sv[i].isgone = 1;

	while (1) {
		errno = 0;
		d = readdir(dir);
		if (!d)
			break;
		if (d->d_name[0] == '.')
			continue;
		if (stat(d->d_name, &s) == -1) {
			warn2_cannot("stat ", d->d_name);
			continue;
		}
		if (!S_ISDIR(s.st_mode))
			continue;
		/* Do we have this service listed already? */
		for (i = 0; i < svnum; i++) {
			if (sv[i].ino == s.st_ino
#if CHECK_DEVNO_TOO
			 && sv[i].dev == s.st_dev
#endif
			) {
				if (sv[i].pid == 0) /* restart if it has died */
					goto run_ith_sv;
				sv[i].isgone = 0; /* "we still see you" */
				goto next_dentry;
			}
		}
		{ /* Not found, make new service */
			struct service *svnew = realloc(sv, (i+1) * sizeof(*sv));
			if (!svnew) {
				warn2_cannot("start runsv ", d->d_name);
				need_rescan = 1;
				continue;
			}
			sv = svnew;
			svnum++;
#if CHECK_DEVNO_TOO
			sv[i].dev = s.st_dev;
#endif
			sv[i].ino = s.st_ino;
 run_ith_sv:
			sv[i].pid = runsv(d->d_name);
			sv[i].isgone = 0;
		}
 next_dentry: ;
	}
	i = errno;
	closedir(dir);
	if (i) { /* readdir failed */
		warn2_cannot("read directory ", svdir);
		return 1; /* need to rescan again soon */
	}

	/* Send SIGTERM to runsv whose directories
	 * were no longer found (-> must have been removed) */
	for (i = 0; i < svnum; i++) {
		if (!sv[i].isgone)
			continue;
		if (sv[i].pid)
			kill(sv[i].pid, SIGTERM);
		svnum--;
		sv[i] = sv[svnum];
		i--; /* so that we don't skip new sv[i] (bug was here!) */
	}
	return need_rescan;
}

int runsvdir_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int runsvdir_main(int argc UNUSED_PARAM, char **argv)
{
	struct stat s;
	dev_t last_dev = last_dev; /* for gcc */
	ino_t last_ino = last_ino; /* for gcc */
	time_t last_mtime;
	int curdir;
	unsigned stampcheck;
	int i;
	int need_rescan;
	bool i_am_init;
	char *opt_s_argv[3];

	INIT_G();

	opt_s_argv[0] = NULL;
	opt_s_argv[2] = NULL;
	getopt32(argv, "^" "Ps:" "\0" "-1", &opt_s_argv[0]);
	argv += optind;

	i_am_init = (getpid() == 1);
	bb_signals(0
		| (1 << SIGTERM)
		| (1 << SIGHUP)
		/* For busybox's init, SIGTERM == reboot,
		 * SIGUSR1 == halt,
		 * SIGUSR2 == poweroff,
		 * Ctlr-ALt-Del sends SIGINT to init,
		 * so we need to intercept SIGUSRn and SIGINT too.
		 * Note that we do not implement actual reboot
		 * (killall(TERM) + umount, etc), we just pause
		 * respawing and avoid exiting (-> making kernel oops).
		 * The user is responsible for the rest.
		 */
		| (i_am_init ? ((1 << SIGUSR1) | (1 << SIGUSR2) | (1 << SIGINT)) : 0)
		, record_signo);
	svdir = *argv++;

#if ENABLE_FEATURE_RUNSVDIR_LOG
	/* setup log */
	if (*argv) {
		rplog = *argv;
		if (strlen(rplog) < 7) {
			warnx("log must have at least seven characters");
		} else if (piped_pair(logpipe)) {
			warnx("can't create pipe for log");
		} else {
			close_on_exec_on(logpipe.rd);
			close_on_exec_on(logpipe.wr);
			ndelay_on(logpipe.rd);
			ndelay_on(logpipe.wr);
			if (dup2(logpipe.wr, 2) == -1) {
				warnx("can't set filedescriptor for log");
			} else {
				pfd[0].fd = logpipe.rd;
				pfd[0].events = POLLIN;
				stamplog = monotonic_sec();
				goto run;
			}
		}
		rplog = NULL;
		warnx("log service disabled");
	}
 run:
#endif
	curdir = open(".", O_RDONLY|O_NDELAY);
	if (curdir == -1)
		fatal2_cannot("open current directory", "");
	close_on_exec_on(curdir);

	stampcheck = monotonic_sec();
	need_rescan = 1;
	last_mtime = 0;

	for (;;) {
		unsigned now;
		unsigned sig;

		/* collect children */
		for (;;) {
			pid_t pid = wait_any_nohang(NULL);
			if (pid <= 0)
				break;
			for (i = 0; i < svnum; i++) {
				if (pid == sv[i].pid) {
					/* runsv has died */
					sv[i].pid = 0;
					need_rescan = 1;
				}
			}
		}

		now = monotonic_sec();
		if ((int)(now - stampcheck) >= 0) {
			/* wait at least a second */
			stampcheck = now + 1;

			if (stat(svdir, &s) != -1) {
				if (need_rescan || s.st_mtime != last_mtime
				 || s.st_ino != last_ino || s.st_dev != last_dev
				) {
					/* svdir modified */
					if (chdir(svdir) != -1) {
						last_mtime = s.st_mtime;
						last_dev = s.st_dev;
						last_ino = s.st_ino;
						/* if the svdir changed this very second, wait until the
						 * next second, because we won't be able to detect more
						 * changes within this second */
						while (time(NULL) == last_mtime)
							usleep(100000);
						need_rescan = do_rescan();
						while (fchdir(curdir) == -1) {
							warn2_cannot("change directory, pausing", "");
							sleep(5);
						}
					} else {
						warn2_cannot("change directory to ", svdir);
					}
				}
			} else {
				warn2_cannot("stat ", svdir);
			}
		}

#if ENABLE_FEATURE_RUNSVDIR_LOG
		if (rplog) {
			if ((int)(now - stamplog) >= 0) {
				write(logpipe.wr, ".", 1);
				stamplog = now + 900;
			}
		}
		pfd[0].revents = 0;
#endif
		{
			unsigned deadline = (need_rescan ? 1 : 5);
#if ENABLE_FEATURE_RUNSVDIR_LOG
			if (rplog)
				poll(pfd, 1, deadline*1000);
			else
#endif
				sleep(deadline);
		}

#if ENABLE_FEATURE_RUNSVDIR_LOG
		if (pfd[0].revents & POLLIN) {
			char ch;
			while (read(logpipe.rd, &ch, 1) > 0) {
				if (ch < ' ')
					ch = ' ';
				for (i = 6; rplog[i] != '\0'; i++)
					rplog[i-1] = rplog[i];
				rplog[i-1] = ch;
			}
		}
#endif
		sig = bb_got_signal;
		if (!sig)
			continue;
		bb_got_signal = 0;

		/* -s SCRIPT: useful if we are init.
		 * In this case typically script never returns,
		 * it halts/powers off/reboots the system. */
		if (opt_s_argv[0]) {
			pid_t pid;

			/* Single parameter: signal# */
			opt_s_argv[1] = utoa(sig);
			pid = spawn(opt_s_argv);
			if (pid > 0) {
				/* Remembering to wait for _any_ children,
				 * not just pid */
				while (wait(NULL) != pid)
					continue;
			}
		}

		if (sig == SIGHUP) {
			for (i = 0; i < svnum; i++)
				if (sv[i].pid)
					kill(sv[i].pid, SIGTERM);
		}
		/* SIGHUP or SIGTERM (or SIGUSRn if we are init) */
		/* Exit unless we are init */
		if (!i_am_init)
			return (SIGHUP == sig) ? 111 : EXIT_SUCCESS;

		/* init continues to monitor services forever */
	} /* for (;;) */
}
