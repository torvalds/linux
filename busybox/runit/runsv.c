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

//config:config RUNSV
//config:	bool "runsv (7.2 kb)"
//config:	default y
//config:	help
//config:	runsv starts and monitors a service and optionally an appendant log
//config:	service.

//applet:IF_RUNSV(APPLET(runsv, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_RUNSV) += runsv.o

//usage:#define runsv_trivial_usage
//usage:       "DIR"
//usage:#define runsv_full_usage "\n\n"
//usage:       "Start and monitor a service and optionally an appendant log service"

#include <sys/file.h>
#include "libbb.h"
#include "common_bufsiz.h"
#include "runit_lib.h"

#if ENABLE_MONOTONIC_SYSCALL
#include <sys/syscall.h>

/* libc has incredibly messy way of doing this,
 * typically requiring -lrt. We just skip all this mess */
static void gettimeofday_ns(struct timespec *ts)
{
	syscall(__NR_clock_gettime, CLOCK_REALTIME, ts);
}
#else
static void gettimeofday_ns(struct timespec *ts)
{
	BUILD_BUG_ON(sizeof(struct timeval) != sizeof(struct timespec));
	BUILD_BUG_ON(sizeof(((struct timeval*)ts)->tv_usec) != sizeof(ts->tv_nsec));
	/* Cheat */
	gettimeofday((void*)ts, NULL);
	ts->tv_nsec *= 1000;
}
#endif

/* Compare possibly overflowing unsigned counters */
#define LESS(a,b) ((int)((unsigned)(b) - (unsigned)(a)) > 0)

/* state */
#define S_DOWN 0
#define S_RUN 1
#define S_FINISH 2
/* ctrl */
#define C_NOOP 0
#define C_TERM 1
#define C_PAUSE 2
/* want */
#define W_UP 0
#define W_DOWN 1
#define W_EXIT 2

struct svdir {
	int pid;
	smallint state;
	smallint ctrl;
	smallint sd_want;
	smallint islog;
	struct timespec start;
	int fdlock;
	int fdcontrol;
	int fdcontrolwrite;
	int wstat;
};

struct globals {
	smallint haslog;
	smallint sigterm;
	smallint pidchanged;
	struct fd_pair selfpipe;
	struct fd_pair logpipe;
	char *dir;
	struct svdir svd[2];
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define haslog       (G.haslog      )
#define sigterm      (G.sigterm     )
#define pidchanged   (G.pidchanged  )
#define selfpipe     (G.selfpipe    )
#define logpipe      (G.logpipe     )
#define dir          (G.dir         )
#define svd          (G.svd         )
#define INIT_G() do { \
	setup_common_bufsiz(); \
	pidchanged = 1; \
} while (0)

static void fatal2_cannot(const char *m1, const char *m2)
{
	bb_perror_msg_and_die("%s: fatal: can't %s%s", dir, m1, m2);
	/* was exiting 111 */
}
static void fatal_cannot(const char *m)
{
	fatal2_cannot(m, "");
	/* was exiting 111 */
}
static void fatal2x_cannot(const char *m1, const char *m2)
{
	bb_error_msg_and_die("%s: fatal: can't %s%s", dir, m1, m2);
	/* was exiting 111 */
}
static void warn2_cannot(const char *m1, const char *m2)
{
	bb_perror_msg("%s: warning: can't %s%s", dir, m1, m2);
}
static void warn_cannot(const char *m)
{
	warn2_cannot(m, "");
}

static void s_child(int sig_no UNUSED_PARAM)
{
	write(selfpipe.wr, "", 1);
}

static void s_term(int sig_no UNUSED_PARAM)
{
	sigterm = 1;
	write(selfpipe.wr, "", 1); /* XXX */
}

static int open_trunc_or_warn(const char *name)
{
	/* Why O_NDELAY? */
	int fd = open(name, O_WRONLY | O_NDELAY | O_TRUNC | O_CREAT, 0644);
	if (fd < 0)
		bb_perror_msg("%s: warning: cannot open %s",
				dir, name);
	return fd;
}

static void update_status(struct svdir *s)
{
	ssize_t sz;
	int fd;
	svstatus_t status;
	const char *fstatus ="log/supervise/status";
	const char *fstatusnew ="log/supervise/status.new";
	const char *f_stat ="log/supervise/stat";
	const char *fstatnew ="log/supervise/stat.new";
	const char *fpid ="log/supervise/pid";
	const char *fpidnew ="log/supervise/pid.new";

	if (!s->islog) {
		fstatus += 4;
		fstatusnew += 4;
		f_stat += 4;
		fstatnew += 4;
		fpid += 4;
		fpidnew += 4;
	}

	/* pid */
	if (pidchanged) {
		fd = open_trunc_or_warn(fpidnew);
		if (fd < 0)
			return;
		if (s->pid) {
			char spid[sizeof(int)*3 + 2];
			int size = sprintf(spid, "%u\n", (unsigned)s->pid);
			write(fd, spid, size);
		}
		close(fd);
		if (rename_or_warn(fpidnew, fpid))
			return;
		pidchanged = 0;
	}

	/* stat */
	fd = open_trunc_or_warn(fstatnew);
	if (fd < -1)
		return;

	{
		char stat_buf[sizeof("finish, paused, got TERM, want down\n")];
		char *p = stat_buf;
		switch (s->state) {
		case S_DOWN:
			p = stpcpy(p, "down");
			break;
		case S_RUN:
			p = stpcpy(p, "run");
			break;
		case S_FINISH:
			p = stpcpy(p, "finish");
			break;
		}
		if (s->ctrl & C_PAUSE)
			p = stpcpy(p, ", paused");
		if (s->ctrl & C_TERM)
			p = stpcpy(p, ", got TERM");
		if (s->state != S_DOWN)
			switch (s->sd_want) {
			case W_DOWN:
				p = stpcpy(p, ", want down");
				break;
			case W_EXIT:
				p = stpcpy(p, ", want exit");
				break;
			}
		*p++ = '\n';
		write(fd, stat_buf, p - stat_buf);
		close(fd);
	}

	rename_or_warn(fstatnew, f_stat);

	/* supervise compatibility */
	memset(&status, 0, sizeof(status));
	status.time_be64 = SWAP_BE64(s->start.tv_sec + 0x400000000000000aULL);
	status.time_nsec_be32 = SWAP_BE32(s->start.tv_nsec);
	status.pid_le32 = SWAP_LE32(s->pid);
	if (s->ctrl & C_PAUSE)
		status.paused = 1;
	if (s->sd_want == W_UP)
		status.want = 'u';
	else
		status.want = 'd';
	if (s->ctrl & C_TERM)
		status.got_term = 1;
	status.run_or_finish = s->state;
	fd = open_trunc_or_warn(fstatusnew);
	if (fd < 0)
		return;
	sz = write(fd, &status, sizeof(status));
	close(fd);
	if (sz != sizeof(status)) {
		warn2_cannot("write ", fstatusnew);
		unlink(fstatusnew);
		return;
	}
	rename_or_warn(fstatusnew, fstatus);
}

static unsigned custom(struct svdir *s, char c)
{
	pid_t pid;
	int w;
	char a[10];
	struct stat st;

	if (s->islog)
		return 0;
	strcpy(a, "control/?");
	a[8] = c; /* replace '?' */
	if (stat(a, &st) == 0) {
		if (st.st_mode & S_IXUSR) {
			pid = vfork();
			if (pid == -1) {
				warn2_cannot("vfork for ", a);
				return 0;
			}
			if (pid == 0) {
				/* child */
				if (haslog && dup2(logpipe.wr, 1) == -1)
					warn2_cannot("setup stdout for ", a);
				execl(a, a, (char *) NULL);
				fatal2_cannot("run ", a);
			}
			/* parent */
			if (safe_waitpid(pid, &w, 0) == -1) {
				warn2_cannot("wait for child ", a);
				return 0;
			}
			return WEXITSTATUS(w) == 0;
		}
	} else {
		if (errno != ENOENT)
			warn2_cannot("stat ", a);
	}
	return 0;
}

static void stopservice(struct svdir *s)
{
	if (s->pid && !custom(s, 't')) {
		kill(s->pid, SIGTERM);
		s->ctrl |= C_TERM;
		update_status(s);
	}
	if (s->sd_want == W_DOWN) {
		kill(s->pid, SIGCONT);
		custom(s, 'd');
		return;
	}
	if (s->sd_want == W_EXIT) {
		kill(s->pid, SIGCONT);
		custom(s, 'x');
	}
}

static void startservice(struct svdir *s)
{
	int p;
	const char *arg[4];
	char exitcode[sizeof(int)*3 + 2];

	if (s->state == S_FINISH) {
/* Two arguments are given to ./finish. The first one is ./run exit code,
 * or -1 if ./run didnt exit normally. The second one is
 * the least significant byte of the exit status as determined by waitpid;
 * for instance it is 0 if ./run exited normally, and the signal number
 * if ./run was terminated by a signal. If runsv cannot start ./run
 * for some reason, the exit code is 111 and the status is 0.
 */
		arg[0] = "./finish";
		arg[1] = "-1";
		if (WIFEXITED(s->wstat)) {
			*utoa_to_buf(WEXITSTATUS(s->wstat), exitcode, sizeof(exitcode)) = '\0';
			arg[1] = exitcode;
		}
		//arg[2] = "0";
		//if (WIFSIGNALED(s->wstat)) {
			arg[2] = utoa(WTERMSIG(s->wstat));
		//}
		arg[3] = NULL;
	} else {
		arg[0] = "./run";
		arg[1] = NULL;
		custom(s, 'u');
	}

	if (s->pid != 0)
		stopservice(s); /* should never happen */
	while ((p = vfork()) == -1) {
		warn_cannot("vfork, sleeping");
		sleep(5);
	}
	if (p == 0) {
		/* child */
		if (haslog) {
			/* NB: bug alert! right order is close, then dup2 */
			if (s->islog) {
				xchdir("./log");
				close(logpipe.wr);
				xdup2(logpipe.rd, 0);
			} else {
				close(logpipe.rd);
				xdup2(logpipe.wr, 1);
			}
		}
		/* Non-ignored signals revert to SIG_DFL on exec anyway */
		/*bb_signals(0
			+ (1 << SIGCHLD)
			+ (1 << SIGTERM)
			, SIG_DFL);*/
		sig_unblock(SIGCHLD);
		sig_unblock(SIGTERM);
		execv(arg[0], (char**) arg);
		fatal2_cannot(s->islog ? "start log/" : "start ", arg[0]);
	}
	/* parent */
	if (s->state != S_FINISH) {
		gettimeofday_ns(&s->start);
		s->state = S_RUN;
	}
	s->pid = p;
	pidchanged = 1;
	s->ctrl = C_NOOP;
	update_status(s);
}

static int ctrl(struct svdir *s, char c)
{
	int sig;

	switch (c) {
	case 'd': /* down */
		s->sd_want = W_DOWN;
		update_status(s);
		if (s->state == S_RUN)
			stopservice(s);
		break;
	case 'u': /* up */
		s->sd_want = W_UP;
		update_status(s);
		if (s->state == S_DOWN)
			startservice(s);
		break;
	case 'x': /* exit */
		if (s->islog)
			break;
		s->sd_want = W_EXIT;
		update_status(s);
		/* FALLTHROUGH */
	case 't': /* sig term */
		if (s->state == S_RUN)
			stopservice(s);
		break;
	case 'k': /* sig kill */
		if ((s->state == S_RUN) && !custom(s, c))
			kill(s->pid, SIGKILL);
		s->state = S_DOWN;
		break;
	case 'p': /* sig pause */
		if ((s->state == S_RUN) && !custom(s, c))
			kill(s->pid, SIGSTOP);
		s->ctrl |= C_PAUSE;
		update_status(s);
		break;
	case 'c': /* sig cont */
		if ((s->state == S_RUN) && !custom(s, c))
			kill(s->pid, SIGCONT);
		s->ctrl &= ~C_PAUSE;
		update_status(s);
		break;
	case 'o': /* once */
		s->sd_want = W_DOWN;
		update_status(s);
		if (s->state == S_DOWN)
			startservice(s);
		break;
	case 'a': /* sig alarm */
		sig = SIGALRM;
		goto sendsig;
	case 'h': /* sig hup */
		sig = SIGHUP;
		goto sendsig;
	case 'i': /* sig int */
		sig = SIGINT;
		goto sendsig;
	case 'q': /* sig quit */
		sig = SIGQUIT;
		goto sendsig;
	case '1': /* sig usr1 */
		sig = SIGUSR1;
		goto sendsig;
	case '2': /* sig usr2 */
		sig = SIGUSR2;
		goto sendsig;
	}
	return 1;
 sendsig:
	if ((s->state == S_RUN) && !custom(s, c))
		kill(s->pid, sig);
	return 1;
}

static void open_control(const char *f, struct svdir *s)
{
	struct stat st;
	mkfifo(f, 0600);
	if (stat(f, &st) == -1)
		fatal2_cannot("stat ", f);
	if (!S_ISFIFO(st.st_mode))
		bb_error_msg_and_die("%s: fatal: %s exists but is not a fifo", dir, f);
	s->fdcontrol = xopen(f, O_RDONLY|O_NDELAY);
	close_on_exec_on(s->fdcontrol);
	s->fdcontrolwrite = xopen(f, O_WRONLY|O_NDELAY);
	close_on_exec_on(s->fdcontrolwrite);
	update_status(s);
}

int runsv_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int runsv_main(int argc UNUSED_PARAM, char **argv)
{
	struct stat s;
	int fd;
	int r;
	char buf[256];

	INIT_G();

	dir = single_argv(argv);

	xpiped_pair(selfpipe);
	close_on_exec_on(selfpipe.rd);
	close_on_exec_on(selfpipe.wr);
	ndelay_on(selfpipe.rd);
	ndelay_on(selfpipe.wr);

	sig_block(SIGCHLD);
	bb_signals_recursive_norestart(1 << SIGCHLD, s_child);
	sig_block(SIGTERM);
	bb_signals_recursive_norestart(1 << SIGTERM, s_term);

	xchdir(dir);
	/* bss: svd[0].pid = 0; */
	if (S_DOWN) svd[0].state = S_DOWN; /* otherwise already 0 (bss) */
	if (C_NOOP) svd[0].ctrl = C_NOOP;
	if (W_UP) svd[0].sd_want = W_UP;
	/* bss: svd[0].islog = 0; */
	/* bss: svd[1].pid = 0; */
	gettimeofday_ns(&svd[0].start);
	if (stat("down", &s) != -1)
		svd[0].sd_want = W_DOWN;

	if (stat("log", &s) == -1) {
		if (errno != ENOENT)
			warn_cannot("stat ./log");
	} else {
		if (!S_ISDIR(s.st_mode)) {
			errno = 0;
			warn_cannot("stat log/down: log is not a directory");
		} else {
			haslog = 1;
			svd[1].state = S_DOWN;
			svd[1].ctrl = C_NOOP;
			svd[1].sd_want = W_UP;
			svd[1].islog = 1;
			gettimeofday_ns(&svd[1].start);
			if (stat("log/down", &s) != -1)
				svd[1].sd_want = W_DOWN;
			xpiped_pair(logpipe);
			close_on_exec_on(logpipe.rd);
			close_on_exec_on(logpipe.wr);
		}
	}

	if (mkdir("supervise", 0700) == -1) {
		r = readlink("supervise", buf, sizeof(buf));
		if (r != -1) {
			if (r == sizeof(buf))
				fatal2x_cannot("readlink ./supervise", ": name too long");
			buf[r] = 0;
			mkdir(buf, 0700);
		} else {
			if ((errno != ENOENT) && (errno != EINVAL))
				fatal_cannot("readlink ./supervise");
		}
	}
	svd[0].fdlock = xopen3("log/supervise/lock"+4,
			O_WRONLY|O_NDELAY|O_APPEND|O_CREAT, 0600);
	if (flock(svd[0].fdlock, LOCK_EX | LOCK_NB) == -1)
		fatal_cannot("lock supervise/lock");
	close_on_exec_on(svd[0].fdlock);
	if (haslog) {
		if (mkdir("log/supervise", 0700) == -1) {
			r = readlink("log/supervise", buf, 256);
			if (r != -1) {
				if (r == 256)
					fatal2x_cannot("readlink ./log/supervise", ": name too long");
				buf[r] = 0;
				fd = xopen(".", O_RDONLY|O_NDELAY);
				xchdir("./log");
				mkdir(buf, 0700);
				if (fchdir(fd) == -1)
					fatal_cannot("change back to service directory");
				close(fd);
			}
			else {
				if ((errno != ENOENT) && (errno != EINVAL))
					fatal_cannot("readlink ./log/supervise");
			}
		}
		svd[1].fdlock = xopen3("log/supervise/lock",
				O_WRONLY|O_NDELAY|O_APPEND|O_CREAT, 0600);
		if (flock(svd[1].fdlock, LOCK_EX) == -1)
			fatal_cannot("lock log/supervise/lock");
		close_on_exec_on(svd[1].fdlock);
	}

	open_control("log/supervise/control"+4, &svd[0]);
	if (haslog) {
		open_control("log/supervise/control", &svd[1]);
	}
	mkfifo("log/supervise/ok"+4, 0600);
	fd = xopen("log/supervise/ok"+4, O_RDONLY|O_NDELAY);
	close_on_exec_on(fd);
	if (haslog) {
		mkfifo("log/supervise/ok", 0600);
		fd = xopen("log/supervise/ok", O_RDONLY|O_NDELAY);
		close_on_exec_on(fd);
	}
	for (;;) {
		struct pollfd x[3];
		unsigned deadline;
		char ch;

		if (haslog)
			if (!svd[1].pid && svd[1].sd_want == W_UP)
				startservice(&svd[1]);
		if (!svd[0].pid)
			if (svd[0].sd_want == W_UP || svd[0].state == S_FINISH)
				startservice(&svd[0]);

		x[0].fd = selfpipe.rd;
		x[0].events = POLLIN;
		x[1].fd = svd[0].fdcontrol;
		x[1].events = POLLIN;
		/* x[2] is used only if haslog == 1 */
		x[2].fd = svd[1].fdcontrol;
		x[2].events = POLLIN;
		sig_unblock(SIGTERM);
		sig_unblock(SIGCHLD);
		poll(x, 2 + haslog, 3600*1000);
		sig_block(SIGTERM);
		sig_block(SIGCHLD);

		while (read(selfpipe.rd, &ch, 1) == 1)
			continue;

		for (;;) {
			pid_t child;
			int wstat;

			child = wait_any_nohang(&wstat);
			if (!child)
				break;
			if ((child == -1) && (errno != EINTR))
				break;
			if (child == svd[0].pid) {
				svd[0].wstat = wstat;
				svd[0].pid = 0;
				pidchanged = 1;
				svd[0].ctrl &= ~C_TERM;
				if (svd[0].state != S_FINISH) {
					fd = open("finish", O_RDONLY|O_NDELAY);
					if (fd != -1) {
						close(fd);
						svd[0].state = S_FINISH;
						update_status(&svd[0]);
						continue;
					}
				}
				svd[0].state = S_DOWN;
				deadline = svd[0].start.tv_sec + 1;
				gettimeofday_ns(&svd[0].start);
				update_status(&svd[0]);
				if (LESS(svd[0].start.tv_sec, deadline))
					sleep(1);
			}
			if (haslog) {
				if (child == svd[1].pid) {
					svd[0].wstat = wstat;
					svd[1].pid = 0;
					pidchanged = 1;
					svd[1].state = S_DOWN;
					svd[1].ctrl &= ~C_TERM;
					deadline = svd[1].start.tv_sec + 1;
					gettimeofday_ns(&svd[1].start);
					update_status(&svd[1]);
					if (LESS(svd[1].start.tv_sec, deadline))
						sleep(1);
				}
			}
		} /* for (;;) */
		if (read(svd[0].fdcontrol, &ch, 1) == 1)
			ctrl(&svd[0], ch);
		if (haslog)
			if (read(svd[1].fdcontrol, &ch, 1) == 1)
				ctrl(&svd[1], ch);

		if (sigterm) {
			ctrl(&svd[0], 'x');
			sigterm = 0;
		}

		if (svd[0].sd_want == W_EXIT && svd[0].state == S_DOWN) {
			if (svd[1].pid == 0)
				_exit(EXIT_SUCCESS);
			if (svd[1].sd_want != W_EXIT) {
				svd[1].sd_want = W_EXIT;
				/* stopservice(&svd[1]); */
				update_status(&svd[1]);
				close(logpipe.wr);
				close(logpipe.rd);
			}
		}
	} /* for (;;) */
	/* not reached */
	return 0;
}
