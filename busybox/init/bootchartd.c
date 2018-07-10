/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config BOOTCHARTD
//config:	bool "bootchartd (10 kb)"
//config:	default y
//config:	help
//config:	bootchartd is commonly used to profile the boot process
//config:	for the purpose of speeding it up. In this case, it is started
//config:	by the kernel as the init process. This is configured by adding
//config:	the init=/sbin/bootchartd option to the kernel command line.
//config:
//config:	It can also be used to monitor the resource usage of a specific
//config:	application or the running system in general. In this case,
//config:	bootchartd is started interactively by running bootchartd start
//config:	and stopped using bootchartd stop.
//config:
//config:config FEATURE_BOOTCHARTD_BLOATED_HEADER
//config:	bool "Compatible, bloated header"
//config:	default y
//config:	depends on BOOTCHARTD
//config:	help
//config:	Create extended header file compatible with "big" bootchartd.
//config:	"Big" bootchartd is a shell script and it dumps some
//config:	"convenient" info int the header, such as:
//config:		title = Boot chart for `hostname` (`date`)
//config:		system.uname = `uname -srvm`
//config:		system.release = `cat /etc/DISTRO-release`
//config:		system.cpu = `grep '^model name' /proc/cpuinfo | head -1` ($cpucount)
//config:		system.kernel.options = `cat /proc/cmdline`
//config:	This data is not mandatory for bootchart graph generation,
//config:	and is considered bloat. Nevertheless, this option
//config:	makes bootchartd applet to dump a subset of it.
//config:
//config:config FEATURE_BOOTCHARTD_CONFIG_FILE
//config:	bool "Support bootchartd.conf"
//config:	default y
//config:	depends on BOOTCHARTD
//config:	help
//config:	Enable reading and parsing of $PWD/bootchartd.conf
//config:	and /etc/bootchartd.conf files.

//applet:IF_BOOTCHARTD(APPLET(bootchartd, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_BOOTCHARTD) += bootchartd.o

#include "libbb.h"
#include "common_bufsiz.h"
/* After libbb.h, since it needs sys/types.h on some systems */
#include <sys/utsname.h>

#ifdef __linux__
# include <sys/mount.h>
# ifndef MS_SILENT
#  define MS_SILENT      (1 << 15)
# endif
# ifndef MNT_DETACH
#  define MNT_DETACH 0x00000002
# endif
#endif

#if !ENABLE_TAR && !ENABLE_WERROR
# warning Note: bootchartd requires tar command, but you did not select it.
#elif !ENABLE_FEATURE_SEAMLESS_GZ && !ENABLE_WERROR
# warning Note: bootchartd requires tar -z support, but you did not select it.
#endif

#define BC_VERSION_STR "0.8"

/* For debugging, set to 0:
 * strace won't work with DO_SIGNAL_SYNC set to 1.
 */
#define DO_SIGNAL_SYNC 1


//$PWD/bootchartd.conf and /etc/bootchartd.conf:
//supported options:
//# Sampling period (in seconds)
//SAMPLE_PERIOD=0.2
//
//not yet supported:
//# tmpfs size
//# (32 MB should suffice for ~20 minutes worth of log data, but YMMV)
//TMPFS_SIZE=32m
//
//# Whether to enable and store BSD process accounting information.  The
//# kernel needs to be configured to enable v3 accounting
//# (CONFIG_BSD_PROCESS_ACCT_V3). accton from the GNU accounting utilities
//# is also required.
//PROCESS_ACCOUNTING="no"
//
//# Tarball for the various boot log files
//BOOTLOG_DEST=/var/log/bootchart.tgz
//
//# Whether to automatically stop logging as the boot process completes.
//# The logger will look for known processes that indicate bootup completion
//# at a specific runlevel (e.g. gdm-binary, mingetty, etc.).
//AUTO_STOP_LOGGER="yes"
//
//# Whether to automatically generate the boot chart once the boot logger
//# completes.  The boot chart will be generated in $AUTO_RENDER_DIR.
//# Note that the bootchart package must be installed.
//AUTO_RENDER="no"
//
//# Image format to use for the auto-generated boot chart
//# (choose between png, svg and eps).
//AUTO_RENDER_FORMAT="png"
//
//# Output directory for auto-generated boot charts
//AUTO_RENDER_DIR="/var/log"


/* Globals */
struct globals {
	char jiffy_line[COMMON_BUFSIZE];
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)

static void dump_file(FILE *fp, const char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd >= 0) {
		fputs(G.jiffy_line, fp);
		fflush(fp);
		bb_copyfd_eof(fd, fileno(fp));
		close(fd);
		fputc('\n', fp);
	}
}

static int dump_procs(FILE *fp, int look_for_login_process)
{
	struct dirent *entry;
	DIR *dir = opendir("/proc");
	int found_login_process = 0;

	fputs(G.jiffy_line, fp);
	while ((entry = readdir(dir)) != NULL) {
		char name[sizeof("/proc/%u/cmdline") + sizeof(int)*3];
		int stat_fd;
		unsigned pid = bb_strtou(entry->d_name, NULL, 10);
		if (errno)
			continue;

		/* Android's version reads /proc/PID/cmdline and extracts
		 * non-truncated process name. Do we want to do that? */

		sprintf(name, "/proc/%u/stat", pid);
		stat_fd = open(name, O_RDONLY);
		if (stat_fd >= 0) {
			char *p;
			char stat_line[4*1024];
			int rd = safe_read(stat_fd, stat_line, sizeof(stat_line)-2);

			close(stat_fd);
			if (rd < 0)
				continue;
			stat_line[rd] = '\0';
			p = strchrnul(stat_line, '\n');
			*p++ = '\n';
			*p = '\0';
			fputs(stat_line, fp);
			if (!look_for_login_process)
				continue;
			p = strchr(stat_line, '(');
			if (!p)
				continue;
			p++;
			strchrnul(p, ')')[0] = '\0';
			/* Is it gdm, kdm or a getty? */
			if (((p[0] == 'g' || p[0] == 'k' || p[0] == 'x')
			     && p[1] == 'd' && p[2] == 'm' && p[3] == '\0'
			    )
			 || strstr(p, "getty")
			) {
				found_login_process = 1;
			}
		}
	}
	closedir(dir);
	fputc('\n', fp);
	return found_login_process;
}

static char *make_tempdir(void)
{
	char template[] = "/tmp/bootchart.XXXXXX";
	char *tempdir = xstrdup(mkdtemp(template));
	if (!tempdir) {
#ifdef __linux__
		/* /tmp is not writable (happens when we are used as init).
		 * Try to mount a tmpfs, then cd and lazily unmount it.
		 * Since we unmount it at once, we can mount it anywhere.
		 * Try a few locations which are likely ti exist.
		 */
		static const char dirs[] ALIGN1 = "/mnt\0""/tmp\0""/boot\0""/proc\0";
		const char *try_dir = dirs;
		while (mount("none", try_dir, "tmpfs", MS_SILENT, "size=16m") != 0) {
			try_dir += strlen(try_dir) + 1;
			if (!try_dir[0])
				bb_perror_msg_and_die("can't %smount tmpfs", "");
		}
		//bb_error_msg("mounted tmpfs on %s", try_dir);
		xchdir(try_dir);
		if (umount2(try_dir, MNT_DETACH) != 0) {
			bb_perror_msg_and_die("can't %smount tmpfs", "un");
		}
#else
		bb_perror_msg_and_die("can't create temporary directory");
#endif
	} else {
		xchdir(tempdir);
	}
	return tempdir;
}

static void do_logging(unsigned sample_period_us, int process_accounting)
{
	FILE *proc_stat = xfopen("proc_stat.log", "w");
	FILE *proc_diskstats = xfopen("proc_diskstats.log", "w");
	//FILE *proc_netdev = xfopen("proc_netdev.log", "w");
	FILE *proc_ps = xfopen("proc_ps.log", "w");
	int look_for_login_process = (getppid() == 1);
	unsigned count = 60*1000*1000 / sample_period_us; /* ~1 minute */

	if (process_accounting) {
		close(xopen("kernel_pacct", O_WRONLY | O_CREAT | O_TRUNC));
		acct("kernel_pacct");
	}

	while (--count && !bb_got_signal) {
		char *p;
		int len = open_read_close("/proc/uptime", G.jiffy_line, sizeof(G.jiffy_line)-2);
		if (len < 0)
			goto wait_more;
		/* /proc/uptime has format "NNNNNN.MM NNNNNNN.MM" */
		/* we convert it to "NNNNNNMM\n" (using first value) */
		G.jiffy_line[len] = '\0';
		p = strchr(G.jiffy_line, '.');
		if (!p)
			goto wait_more;
		while (isdigit(*++p))
			p[-1] = *p;
		p[-1] = '\n';
		p[0] = '\0';

		dump_file(proc_stat, "/proc/stat");
		dump_file(proc_diskstats, "/proc/diskstats");
		//dump_file(proc_netdev, "/proc/net/dev");
		if (dump_procs(proc_ps, look_for_login_process)) {
			/* dump_procs saw a getty or {g,k,x}dm
			 * stop logging in 2 seconds:
			 */
			if (count > 2*1000*1000 / sample_period_us)
				count = 2*1000*1000 / sample_period_us;
		}
		fflush_all();
 wait_more:
		usleep(sample_period_us);
	}
}

static void finalize(char *tempdir, const char *prog, int process_accounting)
{
	//# Stop process accounting if configured
	//local pacct=
	//[ -e kernel_pacct ] && pacct=kernel_pacct

	FILE *header_fp = xfopen("header", "w");

	if (process_accounting)
		acct(NULL);

	if (prog)
		fprintf(header_fp, "profile.process = %s\n", prog);

	fputs("version = "BC_VERSION_STR"\n", header_fp);
	if (ENABLE_FEATURE_BOOTCHARTD_BLOATED_HEADER) {
		char *hostname;
		char *kcmdline;
		time_t t;
		struct tm tm_time;
		/* x2 for possible localized weekday/month names */
		char date_buf[sizeof("Mon Jun 21 05:29:03 CEST 2010") * 2];
		struct utsname unamebuf;

		hostname = safe_gethostname();
		time(&t);
		localtime_r(&t, &tm_time);
		strftime(date_buf, sizeof(date_buf), "%a %b %e %H:%M:%S %Z %Y", &tm_time);
		fprintf(header_fp, "title = Boot chart for %s (%s)\n", hostname, date_buf);
		if (ENABLE_FEATURE_CLEAN_UP)
			free(hostname);

		uname(&unamebuf); /* never fails */
		/* same as uname -srvm */
		fprintf(header_fp, "system.uname = %s %s %s %s\n",
				unamebuf.sysname,
				unamebuf.release,
				unamebuf.version,
				unamebuf.machine
		);

		//system.release = `cat /etc/DISTRO-release`
		//system.cpu = `grep '^model name' /proc/cpuinfo | head -1` ($cpucount)

		kcmdline = xmalloc_open_read_close("/proc/cmdline", NULL);
		/* kcmdline includes trailing "\n" */
		fprintf(header_fp, "system.kernel.options = %s", kcmdline);
		if (ENABLE_FEATURE_CLEAN_UP)
			free(kcmdline);
	}
	fclose(header_fp);

	/* Package log files */
	system(xasprintf("tar -zcf /var/log/bootlog.tgz header %s *.log", process_accounting ? "kernel_pacct" : ""));
	/* Clean up (if we are not in detached tmpfs) */
	if (tempdir) {
		unlink("header");
		unlink("proc_stat.log");
		unlink("proc_diskstats.log");
		//unlink("proc_netdev.log");
		unlink("proc_ps.log");
		if (process_accounting)
			unlink("kernel_pacct");
		rmdir(tempdir);
	}

	/* shell-based bootchartd tries to run /usr/bin/bootchart if $AUTO_RENDER=yes:
	 * /usr/bin/bootchart -o "$AUTO_RENDER_DIR" -f $AUTO_RENDER_FORMAT "$BOOTLOG_DEST"
	 */
}

//usage:#define bootchartd_trivial_usage
//usage:       "start [PROG ARGS]|stop|init"
//usage:#define bootchartd_full_usage "\n\n"
//usage:       "Create /var/log/bootchart.tgz with boot chart data\n"
//usage:     "\nstart: start background logging; with PROG, run PROG, then kill logging with USR1"
//usage:     "\nstop: send USR1 to all bootchartd processes"
//usage:     "\ninit: start background logging; stop when getty/xdm is seen (for init scripts)"
//usage:     "\nUnder PID 1: as init, then exec $bootchart_init, /init, /sbin/init"

int bootchartd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int bootchartd_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned sample_period_us;
	pid_t parent_pid, logger_pid;
	smallint cmd;
	int process_accounting;
	enum {
		CMD_STOP = 0,
		CMD_START,
		CMD_INIT,
		CMD_PID1, /* used to mark pid 1 case */
	};

	INIT_G();

	parent_pid = getpid();
	if (argv[1]) {
		cmd = index_in_strings("stop\0""start\0""init\0", argv[1]);
		if (cmd < 0)
			bb_show_usage();
		if (cmd == CMD_STOP) {
			pid_t *pidList = find_pid_by_name("bootchartd");
			while (*pidList != 0) {
				if (*pidList != parent_pid)
					kill(*pidList, SIGUSR1);
				pidList++;
			}
			return EXIT_SUCCESS;
		}
	} else {
		if (parent_pid != 1)
			bb_show_usage();
		cmd = CMD_PID1;
	}

	/* Here we are in START, INIT or CMD_PID1 state */

	/* Read config file: */
	sample_period_us = 200 * 1000;
	process_accounting = 0;
	if (ENABLE_FEATURE_BOOTCHARTD_CONFIG_FILE) {
		char* token[2];
		parser_t *parser = config_open2("/etc/bootchartd.conf" + 5, fopen_for_read);
		if (!parser)
			parser = config_open2("/etc/bootchartd.conf", fopen_for_read);
		while (config_read(parser, token, 2, 0, "#=", PARSE_NORMAL & ~PARSE_COLLAPSE)) {
			if (strcmp(token[0], "SAMPLE_PERIOD") == 0 && token[1])
				sample_period_us = atof(token[1]) * 1000000;
			if (strcmp(token[0], "PROCESS_ACCOUNTING") == 0 && token[1]
			 && (strcmp(token[1], "on") == 0 || strcmp(token[1], "yes") == 0)
			) {
				process_accounting = 1;
			}
		}
		config_close(parser);
		if ((int)sample_period_us <= 0)
			sample_period_us = 1; /* prevent division by 0 */
	}

	/* Create logger child: */
	logger_pid = fork_or_rexec(argv);

	if (logger_pid == 0) { /* child */
		char *tempdir;

		bb_signals(0
			+ (1 << SIGUSR1)
			+ (1 << SIGUSR2)
			+ (1 << SIGTERM)
			+ (1 << SIGQUIT)
			+ (1 << SIGINT)
			+ (1 << SIGHUP)
			, record_signo);

		if (DO_SIGNAL_SYNC)
			/* Inform parent that we are ready */
			raise(SIGSTOP);

		/* If we are started by kernel, PATH might be unset.
		 * In order to find "tar", let's set some sane PATH:
		 */
		if (cmd == CMD_PID1 && !getenv("PATH"))
			putenv((char*)bb_PATH_root_path);

		tempdir = make_tempdir();
		do_logging(sample_period_us, process_accounting);
		finalize(tempdir, cmd == CMD_START ? argv[2] : NULL, process_accounting);
		return EXIT_SUCCESS;
	}

	/* parent */

	USE_FOR_NOMMU(argv[0][0] &= 0x7f); /* undo fork_or_rexec() damage */

	if (DO_SIGNAL_SYNC) {
		/* Wait for logger child to set handlers, then unpause it.
		 * Otherwise with short-lived PROG (e.g. "bootchartd start true")
		 * we might send SIGUSR1 before logger sets its handler.
		 */
		waitpid(logger_pid, NULL, WUNTRACED);
		kill(logger_pid, SIGCONT);
	}

	if (cmd == CMD_PID1) {
		char *bootchart_init = getenv("bootchart_init");
		if (bootchart_init)
			execl(bootchart_init, bootchart_init, NULL);
		execl("/init", "init", NULL);
		execl("/sbin/init", "init", NULL);
		bb_perror_msg_and_die("can't execute '%s'", "/sbin/init");
	}

	if (cmd == CMD_START && argv[2]) { /* "start PROG ARGS" */
		pid_t pid = xvfork();
		if (pid == 0) { /* child */
			argv += 2;
			BB_EXECVP_or_die(argv);
		}
		/* parent */
		waitpid(pid, NULL, 0);
		kill(logger_pid, SIGUSR1);
	}

	return EXIT_SUCCESS;
}
