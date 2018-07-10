/* vi: set sw=4 ts=4: */
/*
 * Mini klogd implementation for busybox
 *
 * Copyright (C) 2001 by Gennady Feldman <gfeldman@gena01.com>.
 * Changes: Made this a standalone busybox module which uses standalone
 * syslog() client interface.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Copyright (C) 2000 by Karl M. Hegbloom <karlheg@debian.org>
 *
 * "circular buffer" Copyright (C) 2000 by Gennady Feldman <gfeldman@gena01.com>
 *
 * Maintainer: Gennady Feldman <gfeldman@gena01.com> as of Mar 12, 2001
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config KLOGD
//config:	bool "klogd (5.5 kb)"
//config:	default y
//config:	help
//config:	klogd is a utility which intercepts and logs all
//config:	messages from the Linux kernel and sends the messages
//config:	out to the 'syslogd' utility so they can be logged. If
//config:	you wish to record the messages produced by the kernel,
//config:	you should enable this option.
//config:
//config:comment "klogd should not be used together with syslog to kernel printk buffer"
//config:	depends on KLOGD && FEATURE_KMSG_SYSLOG
//config:
//config:config FEATURE_KLOGD_KLOGCTL
//config:	bool "Use the klogctl() interface"
//config:	default y
//config:	depends on KLOGD
//config:	select PLATFORM_LINUX
//config:	help
//config:	The klogd applet supports two interfaces for reading
//config:	kernel messages. Linux provides the klogctl() interface
//config:	which allows reading messages from the kernel ring buffer
//config:	independently from the file system.
//config:
//config:	If you answer 'N' here, klogd will use the more portable
//config:	approach of reading them from /proc or a device node.
//config:	However, this method requires the file to be available.
//config:
//config:	If in doubt, say 'Y'.

//applet:IF_KLOGD(APPLET(klogd, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_KLOGD) += klogd.o

//usage:#define klogd_trivial_usage
//usage:       "[-c N] [-n]"
//usage:#define klogd_full_usage "\n\n"
//usage:       "Kernel logger\n"
//usage:     "\n	-c N	Print to console messages more urgent than prio N (1-8)"
//usage:     "\n	-n	Run in foreground"

#include "libbb.h"
#include "common_bufsiz.h"
#include <syslog.h>


/* The Linux-specific klogctl(3) interface does not rely on the filesystem and
 * allows us to change the console loglevel. Alternatively, we read the
 * messages from _PATH_KLOG. */

#if ENABLE_FEATURE_KLOGD_KLOGCTL

# include <sys/klog.h>

static void klogd_open(void)
{
	/* "Open the log. Currently a NOP" */
	klogctl(1, NULL, 0);
}

static void klogd_setloglevel(int lvl)
{
	/* "printk() prints a message on the console only if it has a loglevel
	 * less than console_loglevel". Here we set console_loglevel = lvl. */
	klogctl(8, NULL, lvl);
}

static int klogd_read(char *bufp, int len)
{
	return klogctl(2, bufp, len);
}
# define READ_ERROR "klogctl(2) error"

static void klogd_close(void)
{
	/* FYI: cmd 7 is equivalent to setting console_loglevel to 7
	 * via klogctl(8, NULL, 7). */
	klogctl(7, NULL, 0); /* "7 -- Enable printk's to console" */
	klogctl(0, NULL, 0); /* "0 -- Close the log. Currently a NOP" */
}

#else

# ifndef _PATH_KLOG
#  ifdef __GNU__
#   define _PATH_KLOG "/dev/klog"
#  else
#   error "your system's _PATH_KLOG is unknown"
#  endif
# endif
# define PATH_PRINTK "/proc/sys/kernel/printk"

enum { klogfd = 3 };

static void klogd_open(void)
{
	int fd = xopen(_PATH_KLOG, O_RDONLY);
	xmove_fd(fd, klogfd);
}

static void klogd_setloglevel(int lvl)
{
	FILE *fp = fopen_or_warn(PATH_PRINTK, "w");
	if (fp) {
		/* This changes only first value:
		 * "messages with a higher priority than this
		 * [that is, with numerically lower value]
		 * will be printed to the console".
		 * The other three values in this pseudo-file aren't changed.
		 */
		fprintf(fp, "%u\n", lvl);
		fclose(fp);
	}
}

static int klogd_read(char *bufp, int len)
{
	return read(klogfd, bufp, len);
}
# define READ_ERROR "read error"

static void klogd_close(void)
{
	klogd_setloglevel(7);
	if (ENABLE_FEATURE_CLEAN_UP)
		close(klogfd);
}

#endif

#define log_buffer bb_common_bufsiz1
enum {
	KLOGD_LOGBUF_SIZE = COMMON_BUFSIZE,
	OPT_LEVEL      = (1 << 0),
	OPT_FOREGROUND = (1 << 1),
};

/* TODO: glibc openlog(LOG_KERN) reverts to LOG_USER instead,
 * because that's how they interpret word "default"
 * in the openlog() manpage:
 *      LOG_USER (default)
 *              generic user-level messages
 * and the fact that LOG_KERN is a constant 0.
 * glibc interprets it as "0 in openlog() call means 'use default'".
 * I think it means "if openlog wasn't called before syslog() is called,
 * use default".
 * Convincing glibc maintainers otherwise is, as usual, nearly impossible.
 * Should we open-code syslog() here to use correct facility?
 */

int klogd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int klogd_main(int argc UNUSED_PARAM, char **argv)
{
	int i = 0;
	char *opt_c;
	int opt;
	int used;

	setup_common_bufsiz();

	opt = getopt32(argv, "c:n", &opt_c);
	if (opt & OPT_LEVEL) {
		/* Valid levels are between 1 and 8 */
		i = xatou_range(opt_c, 1, 8);
	}
	if (!(opt & OPT_FOREGROUND)) {
		bb_daemonize_or_rexec(DAEMON_CHDIR_ROOT, argv);
	}

	logmode = LOGMODE_SYSLOG;

	/* klogd_open() before openlog(), since it might use fixed fd 3,
	 * and openlog() also may use the same fd 3 if we swap them:
	 */
	klogd_open();
	openlog("kernel", 0, LOG_KERN);
	/*
	 * glibc problem: for some reason, glibc changes LOG_KERN to LOG_USER
	 * above. The logic behind this is that standard
	 * http://pubs.opengroup.org/onlinepubs/9699919799/functions/syslog.html
	 * says the following about openlog and syslog:
	 * "LOG_USER
	 *  Messages generated by arbitrary processes.
	 *  This is the default facility identifier if none is specified."
	 *
	 * I believe glibc misinterpreted this text as "if openlog's
	 * third parameter is 0 (=LOG_KERN), treat it as LOG_USER".
	 * Whereas it was meant to say "if *syslog* is called with facility
	 * 0 in its 1st parameter without prior call to openlog, then perform
	 * implicit openlog(LOG_USER)".
	 *
	 * As a result of this, eh, feature, standard klogd was forced
	 * to open-code its own openlog and syslog implementation (!).
	 *
	 * Note that prohibiting openlog(LOG_KERN) on libc level does not
	 * add any security: any process can open a socket to "/dev/log"
	 * and write a string "<0>Voila, a LOG_KERN + LOG_EMERG message"
	 *
	 * Google code search tells me there is no widespread use of
	 * openlog("foo", 0, 0), thus fixing glibc won't break userspace.
	 *
	 * The bug against glibc was filed:
	 * bugzilla.redhat.com/show_bug.cgi?id=547000
	 */

	if (i)
		klogd_setloglevel(i);

	signal(SIGHUP, SIG_IGN);
	/* We want klogd_read to not be restarted, thus _norestart: */
	bb_signals_recursive_norestart(BB_FATAL_SIGS, record_signo);

	syslog(LOG_NOTICE, "klogd started: %s", bb_banner);

	write_pidfile(CONFIG_PID_FILE_PATH "/klogd.pid");

	used = 0;
	while (!bb_got_signal) {
		int n;
		int priority;
		char *start;

		/* "2 -- Read from the log." */
		start = log_buffer + used;
		n = klogd_read(start, KLOGD_LOGBUF_SIZE-1 - used);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			bb_perror_msg(READ_ERROR);
			break;
		}
		start[n] = '\0';

		/* Process each newline-terminated line in the buffer */
		start = log_buffer;
		while (1) {
			char *newline = strchrnul(start, '\n');

			if (*newline == '\0') {
				/* This line is incomplete */

				/* move it to the front of the buffer */
				overlapping_strcpy(log_buffer, start);
				used = newline - start;
				if (used < KLOGD_LOGBUF_SIZE-1) {
					/* buffer isn't full */
					break;
				}
				/* buffer is full, log it anyway */
				used = 0;
				newline = NULL;
			} else {
				*newline++ = '\0';
			}

			/* Extract the priority */
			priority = LOG_INFO;
			if (*start == '<') {
				start++;
				if (*start)
					priority = strtoul(start, &start, 10);
				if (*start == '>')
					start++;
			}
			/* Log (only non-empty lines) */
			if (*start)
				syslog(priority, "%s", start);

			if (!newline)
				break;
			start = newline;
		}
	}

	klogd_close();
	syslog(LOG_NOTICE, "klogd: exiting");
	remove_pidfile(CONFIG_PID_FILE_PATH "/klogd.pid");
	if (bb_got_signal)
		kill_myself_with_sig(bb_got_signal);
	return EXIT_FAILURE;
}
