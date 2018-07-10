/* vi: set sw=4 ts=4: */
/*
 * 'time' utility to display resource usage of processes.
 * Copyright (C) 1990, 91, 92, 93, 96 Free Software Foundation, Inc.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
/* Originally written by David Keppel <pardo@cs.washington.edu>.
 * Heavily modified by David MacKenzie <djm@gnu.ai.mit.edu>.
 * Heavily modified for busybox by Erik Andersen <andersen@codepoet.org>
 */
//config:config TIME
//config:	bool "time (7 kb)"
//config:	default y
//config:	help
//config:	The time command runs the specified program with the given arguments.
//config:	When the command finishes, time writes a message to standard output
//config:	giving timing statistics about this program run.

//applet:IF_TIME(APPLET(time, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_TIME) += time.o

//usage:#define time_trivial_usage
//usage:       "[-vpa] [-o FILE] PROG ARGS"
//usage:#define time_full_usage "\n\n"
//usage:       "Run PROG, display resource usage when it exits\n"
//usage:     "\n	-v	Verbose"
//usage:     "\n	-p	POSIX output format"
//usage:     "\n	-f FMT	Custom format"
//usage:     "\n	-o FILE	Write result to FILE"
//usage:     "\n	-a	Append (else overwrite)"

#include "libbb.h"

/* Information on the resources used by a child process.  */
typedef struct {
	int waitstatus;
	struct rusage ru;
	unsigned elapsed_ms;	/* Wallclock time of process.  */
} resource_t;

/* msec = milliseconds = 1/1,000 (1*10e-3) second.
   usec = microseconds = 1/1,000,000 (1*10e-6) second.  */

#define UL unsigned long

static const char default_format[] ALIGN1 = "real\t%E\nuser\t%u\nsys\t%T";

/* The output format for the -p option .*/
static const char posix_format[] ALIGN1 = "real %e\nuser %U\nsys %S";

/* Format string for printing all statistics verbosely.
   Keep this output to 24 lines so users on terminals can see it all.*/
static const char long_format[] ALIGN1 =
	"\tCommand being timed: \"%C\"\n"
	"\tUser time (seconds): %U\n"
	"\tSystem time (seconds): %S\n"
	"\tPercent of CPU this job got: %P\n"
	"\tElapsed (wall clock) time (h:mm:ss or m:ss): %E\n"
	"\tAverage shared text size (kbytes): %X\n"
	"\tAverage unshared data size (kbytes): %D\n"
	"\tAverage stack size (kbytes): %p\n"
	"\tAverage total size (kbytes): %K\n"
	"\tMaximum resident set size (kbytes): %M\n"
	"\tAverage resident set size (kbytes): %t\n"
	"\tMajor (requiring I/O) page faults: %F\n"
	"\tMinor (reclaiming a frame) page faults: %R\n"
	"\tVoluntary context switches: %w\n"
	"\tInvoluntary context switches: %c\n"
	"\tSwaps: %W\n"
	"\tFile system inputs: %I\n"
	"\tFile system outputs: %O\n"
	"\tSocket messages sent: %s\n"
	"\tSocket messages received: %r\n"
	"\tSignals delivered: %k\n"
	"\tPage size (bytes): %Z\n"
	"\tExit status: %x";

/* Wait for and fill in data on child process PID.
   Return 0 on error, 1 if ok.  */
/* pid_t is short on BSDI, so don't try to promote it.  */
static void resuse_end(pid_t pid, resource_t *resp)
{
	pid_t caught;

	/* Ignore signals, but don't ignore the children.  When wait3
	 * returns the child process, set the time the command finished. */
	while ((caught = wait3(&resp->waitstatus, 0, &resp->ru)) != pid) {
		if (caught == -1 && errno != EINTR) {
			bb_perror_msg("wait");
			return;
		}
	}
	resp->elapsed_ms = monotonic_ms() - resp->elapsed_ms;
}

static void printargv(char *const *argv)
{
	const char *fmt = " %s" + 1;
	do {
		printf(fmt, *argv);
		fmt = " %s";
	} while (*++argv);
}

/* Return the number of kilobytes corresponding to a number of pages PAGES.
   (Actually, we use it to convert pages*ticks into kilobytes*ticks.)

   Try to do arithmetic so that the risk of overflow errors is minimized.
   This is funky since the pagesize could be less than 1K.
   Note: Some machines express getrusage statistics in terms of K,
   others in terms of pages.  */
static unsigned long ptok(const unsigned pagesize, const unsigned long pages)
{
	unsigned long tmp;

	/* Conversion.  */
	if (pages > (LONG_MAX / pagesize)) { /* Could overflow.  */
		tmp = pages / 1024;     /* Smaller first, */
		return tmp * pagesize;  /* then larger.  */
	}
	/* Could underflow.  */
	tmp = pages * pagesize; /* Larger first, */
	return tmp / 1024;      /* then smaller.  */
}

/* summarize: Report on the system use of a command.

   Print the FMT argument except that '%' sequences
   have special meaning, and '\n' and '\t' are translated into
   newline and tab, respectively, and '\\' is translated into '\'.

   The character following a '%' can be:
   (* means the tcsh time builtin also recognizes it)
   % == a literal '%'
   C == command name and arguments
*  D == average unshared data size in K (ru_idrss+ru_isrss)
*  E == elapsed real (wall clock) time in [hour:]min:sec
*  F == major page faults (required physical I/O) (ru_majflt)
*  I == file system inputs (ru_inblock)
*  K == average total mem usage (ru_idrss+ru_isrss+ru_ixrss)
*  M == maximum resident set size in K (ru_maxrss)
*  O == file system outputs (ru_oublock)
*  P == percent of CPU this job got (total cpu time / elapsed time)
*  R == minor page faults (reclaims; no physical I/O involved) (ru_minflt)
*  S == system (kernel) time (seconds) (ru_stime)
*  T == system time in [hour:]min:sec
*  U == user time (seconds) (ru_utime)
*  u == user time in [hour:]min:sec
*  W == times swapped out (ru_nswap)
*  X == average amount of shared text in K (ru_ixrss)
   Z == page size
*  c == involuntary context switches (ru_nivcsw)
   e == elapsed real time in seconds
*  k == signals delivered (ru_nsignals)
   p == average unshared stack size in K (ru_isrss)
*  r == socket messages received (ru_msgrcv)
*  s == socket messages sent (ru_msgsnd)
   t == average resident set size in K (ru_idrss)
*  w == voluntary context switches (ru_nvcsw)
   x == exit status of command

   Various memory usages are found by converting from page-seconds
   to kbytes by multiplying by the page size, dividing by 1024,
   and dividing by elapsed real time.

   FMT is the format string, interpreted as described above.
   COMMAND is the command and args that are being summarized.
   RESP is resource information on the command.  */

#ifndef TICKS_PER_SEC
#define TICKS_PER_SEC 100
#endif

static void summarize(const char *fmt, char **command, resource_t *resp)
{
	unsigned vv_ms;     /* Elapsed virtual (CPU) milliseconds */
	unsigned cpu_ticks; /* Same, in "CPU ticks" */
	unsigned pagesize = getpagesize();

	/* Impossible: we do not use WUNTRACED flag in wait()...
	if (WIFSTOPPED(resp->waitstatus))
		printf("Command stopped by signal %u\n",
				WSTOPSIG(resp->waitstatus));
	else */
	if (WIFSIGNALED(resp->waitstatus))
		printf("Command terminated by signal %u\n",
				WTERMSIG(resp->waitstatus));
	else if (WIFEXITED(resp->waitstatus) && WEXITSTATUS(resp->waitstatus))
		printf("Command exited with non-zero status %u\n",
				WEXITSTATUS(resp->waitstatus));

	vv_ms = (resp->ru.ru_utime.tv_sec + resp->ru.ru_stime.tv_sec) * 1000
	      + (resp->ru.ru_utime.tv_usec + resp->ru.ru_stime.tv_usec) / 1000;

#if (1000 / TICKS_PER_SEC) * TICKS_PER_SEC == 1000
	/* 1000 is exactly divisible by TICKS_PER_SEC (typical) */
	cpu_ticks = vv_ms / (1000 / TICKS_PER_SEC);
#else
	cpu_ticks = vv_ms * (unsigned long long)TICKS_PER_SEC / 1000;
#endif
	if (!cpu_ticks) cpu_ticks = 1; /* we divide by it, must be nonzero */

	while (*fmt) {
		/* Handle leading literal part */
		int n = strcspn(fmt, "%\\");
		if (n) {
			printf("%.*s", n, fmt);
			fmt += n;
			continue;
		}

		switch (*fmt) {
#ifdef NOT_NEEDED
		/* Handle literal char */
		/* Usually we optimize for size, but there is a limit
		 * for everything. With this we do a lot of 1-byte writes */
		default:
			bb_putchar(*fmt);
			break;
#endif

		case '%':
			switch (*++fmt) {
#ifdef NOT_NEEDED_YET
		/* Our format strings do not have these */
		/* and we do not take format str from user */
			default:
				bb_putchar('%');
				/*FALLTHROUGH*/
			case '%':
				if (!*fmt) goto ret;
				bb_putchar(*fmt);
				break;
#endif
			case 'C':	/* The command that got timed.  */
				printargv(command);
				break;
			case 'D':	/* Average unshared data size.  */
				printf("%lu",
					(ptok(pagesize, (UL) resp->ru.ru_idrss) +
					 ptok(pagesize, (UL) resp->ru.ru_isrss)) / cpu_ticks);
				break;
			case 'E': {	/* Elapsed real (wall clock) time.  */
				unsigned seconds = resp->elapsed_ms / 1000;
				if (seconds >= 3600)	/* One hour -> h:m:s.  */
					printf("%uh %um %02us",
							seconds / 3600,
							(seconds % 3600) / 60,
							seconds % 60);
				else
					printf("%um %u.%02us",	/* -> m:s.  */
							seconds / 60,
							seconds % 60,
							(unsigned)(resp->elapsed_ms / 10) % 100);
				break;
			}
			case 'F':	/* Major page faults.  */
				printf("%lu", resp->ru.ru_majflt);
				break;
			case 'I':	/* Inputs.  */
				printf("%lu", resp->ru.ru_inblock);
				break;
			case 'K':	/* Average mem usage == data+stack+text.  */
				printf("%lu",
					(ptok(pagesize, (UL) resp->ru.ru_idrss) +
					 ptok(pagesize, (UL) resp->ru.ru_isrss) +
					 ptok(pagesize, (UL) resp->ru.ru_ixrss)) / cpu_ticks);
				break;
			case 'M':	/* Maximum resident set size.  */
				printf("%lu", ptok(pagesize, (UL) resp->ru.ru_maxrss));
				break;
			case 'O':	/* Outputs.  */
				printf("%lu", resp->ru.ru_oublock);
				break;
			case 'P':	/* Percent of CPU this job got.  */
				/* % cpu is (total cpu time)/(elapsed time).  */
				if (resp->elapsed_ms > 0)
					printf("%u%%", (unsigned)(vv_ms * 100 / resp->elapsed_ms));
				else
					printf("?%%");
				break;
			case 'R':	/* Minor page faults (reclaims).  */
				printf("%lu", resp->ru.ru_minflt);
				break;
			case 'S':	/* System time.  */
				printf("%u.%02u",
						(unsigned)resp->ru.ru_stime.tv_sec,
						(unsigned)(resp->ru.ru_stime.tv_usec / 10000));
				break;
			case 'T':	/* System time.  */
				if (resp->ru.ru_stime.tv_sec >= 3600) /* One hour -> h:m:s.  */
					printf("%uh %um %02us",
							(unsigned)(resp->ru.ru_stime.tv_sec / 3600),
							(unsigned)(resp->ru.ru_stime.tv_sec % 3600) / 60,
							(unsigned)(resp->ru.ru_stime.tv_sec % 60));
				else
					printf("%um %u.%02us",	/* -> m:s.  */
							(unsigned)(resp->ru.ru_stime.tv_sec / 60),
							(unsigned)(resp->ru.ru_stime.tv_sec % 60),
							(unsigned)(resp->ru.ru_stime.tv_usec / 10000));
				break;
			case 'U':	/* User time.  */
				printf("%u.%02u",
						(unsigned)resp->ru.ru_utime.tv_sec,
						(unsigned)(resp->ru.ru_utime.tv_usec / 10000));
				break;
			case 'u':	/* User time.  */
				if (resp->ru.ru_utime.tv_sec >= 3600) /* One hour -> h:m:s.  */
					printf("%uh %um %02us",
							(unsigned)(resp->ru.ru_utime.tv_sec / 3600),
							(unsigned)(resp->ru.ru_utime.tv_sec % 3600) / 60,
							(unsigned)(resp->ru.ru_utime.tv_sec % 60));
				else
					printf("%um %u.%02us",	/* -> m:s.  */
							(unsigned)(resp->ru.ru_utime.tv_sec / 60),
							(unsigned)(resp->ru.ru_utime.tv_sec % 60),
							(unsigned)(resp->ru.ru_utime.tv_usec / 10000));
				break;
			case 'W':	/* Times swapped out.  */
				printf("%lu", resp->ru.ru_nswap);
				break;
			case 'X':	/* Average shared text size.  */
				printf("%lu", ptok(pagesize, (UL) resp->ru.ru_ixrss) / cpu_ticks);
				break;
			case 'Z':	/* Page size.  */
				printf("%u", pagesize);
				break;
			case 'c':	/* Involuntary context switches.  */
				printf("%lu", resp->ru.ru_nivcsw);
				break;
			case 'e':	/* Elapsed real time in seconds.  */
				printf("%u.%02u",
						(unsigned)resp->elapsed_ms / 1000,
						(unsigned)(resp->elapsed_ms / 10) % 100);
				break;
			case 'k':	/* Signals delivered.  */
				printf("%lu", resp->ru.ru_nsignals);
				break;
			case 'p':	/* Average stack segment.  */
				printf("%lu", ptok(pagesize, (UL) resp->ru.ru_isrss) / cpu_ticks);
				break;
			case 'r':	/* Incoming socket messages received.  */
				printf("%lu", resp->ru.ru_msgrcv);
				break;
			case 's':	/* Outgoing socket messages sent.  */
				printf("%lu", resp->ru.ru_msgsnd);
				break;
			case 't':	/* Average resident set size.  */
				printf("%lu", ptok(pagesize, (UL) resp->ru.ru_idrss) / cpu_ticks);
				break;
			case 'w':	/* Voluntary context switches.  */
				printf("%lu", resp->ru.ru_nvcsw);
				break;
			case 'x':	/* Exit status.  */
				printf("%u", WEXITSTATUS(resp->waitstatus));
				break;
			}
			break;

#ifdef NOT_NEEDED_YET
		case '\\':		/* Format escape.  */
			switch (*++fmt) {
			default:
				bb_putchar('\\');
				/*FALLTHROUGH*/
			case '\\':
				if (!*fmt) goto ret;
				bb_putchar(*fmt);
				break;
			case 't':
				bb_putchar('\t');
				break;
			case 'n':
				bb_putchar('\n');
				break;
			}
			break;
#endif
		}
		++fmt;
	}
 /* ret: */
	bb_putchar('\n');
}

/* Run command CMD and return statistics on it.
   Put the statistics in *RESP.  */
static void run_command(char *const *cmd, resource_t *resp)
{
	pid_t pid;
	void (*interrupt_signal)(int);
	void (*quit_signal)(int);

	resp->elapsed_ms = monotonic_ms();
	pid = xvfork();
	if (pid == 0) {
		/* Child */
		BB_EXECVP_or_die((char**)cmd);
	}

	/* Have signals kill the child but not self (if possible).  */
//TODO: just block all sigs? and re-enable them in the very end in main?
	interrupt_signal = signal(SIGINT, SIG_IGN);
	quit_signal = signal(SIGQUIT, SIG_IGN);

	resuse_end(pid, resp);

	/* Re-enable signals.  */
	signal(SIGINT, interrupt_signal);
	signal(SIGQUIT, quit_signal);
}

int time_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int time_main(int argc UNUSED_PARAM, char **argv)
{
	resource_t res;
	/* $TIME has lowest prio (-v,-p,-f FMT overrride it) */
	const char *output_format = getenv("TIME") ? : default_format;
	char *output_filename;
	int output_fd;
	int opt;
	int ex;
	enum {
		OPT_v = (1 << 0),
		OPT_p = (1 << 1),
		OPT_a = (1 << 2),
		OPT_o = (1 << 3),
		OPT_f = (1 << 4),
	};

	/* "+": stop on first non-option */
	opt = getopt32(argv, "^+" "vpao:f:" "\0" "-1"/*at least one arg*/,
				&output_filename, &output_format
	);
	argv += optind;
	if (opt & OPT_v)
		output_format = long_format;
	if (opt & OPT_p)
		output_format = posix_format;
	output_fd = STDERR_FILENO;
	if (opt & OPT_o) {
#ifndef O_CLOEXEC
# define O_CLOEXEC 0
#endif
		output_fd = xopen(output_filename,
			(opt & OPT_a) /* append? */
			? (O_CREAT | O_WRONLY | O_CLOEXEC | O_APPEND)
			: (O_CREAT | O_WRONLY | O_CLOEXEC | O_TRUNC)
		);
		if (!O_CLOEXEC)
			close_on_exec_on(output_fd);
	}

	run_command(argv, &res);

	/* Cheat. printf's are shorter :) */
	xdup2(output_fd, STDOUT_FILENO);
	summarize(output_format, argv, &res);

	ex = WEXITSTATUS(res.waitstatus);
	/* Impossible: we do not use WUNTRACED flag in wait()...
	if (WIFSTOPPED(res.waitstatus))
		ex = WSTOPSIG(res.waitstatus);
	*/
	if (WIFSIGNALED(res.waitstatus))
		ex = WTERMSIG(res.waitstatus);

	fflush_stdout_and_exit(ex);
}
