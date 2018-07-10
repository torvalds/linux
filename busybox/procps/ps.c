/* vi: set sw=4 ts=4: */
/*
 * Mini ps implementation(s) for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Fix for SELinux Support:(c)2007 Hiroshi Shinji <shiroshi@my.email.ne.jp>
 *                         (c)2007 Yuichi Nakamura <ynakam@hitachisoft.jp>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config PS
//config:	bool "ps (11 kb)"
//config:	default y
//config:	help
//config:	ps gives a snapshot of the current processes.
//config:
//config:config FEATURE_PS_WIDE
//config:	bool "Enable wide output (-w)"
//config:	default y
//config:	depends on (PS || MINIPS) && !DESKTOP
//config:	help
//config:	Support argument 'w' for wide output.
//config:	If given once, 132 chars are printed, and if given more
//config:	than once, the length is unlimited.
//config:
//config:config FEATURE_PS_LONG
//config:	bool "Enable long output (-l)"
//config:	default y
//config:	depends on (PS || MINIPS) && !DESKTOP
//config:	help
//config:	Support argument 'l' for long output.
//config:	Adds fields PPID, RSS, START, TIME & TTY
//config:
//config:config FEATURE_PS_TIME
//config:	bool "Enable -o time and -o etime specifiers"
//config:	default y
//config:	depends on (PS || MINIPS) && DESKTOP
//config:	select PLATFORM_LINUX
//config:
//config:config FEATURE_PS_UNUSUAL_SYSTEMS
//config:	bool "Support Linux prior to 2.4.0 and non-ELF systems"
//config:	default n
//config:	depends on FEATURE_PS_TIME
//config:	help
//config:	Include support for measuring HZ on old kernels and non-ELF systems
//config:	(if you are on Linux 2.4.0+ and use ELF, you don't need this)
//config:
//config:config FEATURE_PS_ADDITIONAL_COLUMNS
//config:	bool "Enable -o rgroup, -o ruser, -o nice specifiers"
//config:	default y
//config:	depends on (PS || MINIPS) && DESKTOP

//                 APPLET_NOEXEC:name    main location    suid_type     help
//applet:IF_PS(    APPLET_NOEXEC(ps,     ps,  BB_DIR_BIN, BB_SUID_DROP, ps))
//applet:IF_MINIPS(APPLET_NOEXEC(minips, ps,  BB_DIR_BIN, BB_SUID_DROP, ps))

//kbuild:lib-$(CONFIG_PS) += ps.o
//kbuild:lib-$(CONFIG_MINIPS) += ps.o

//usage:#if ENABLE_DESKTOP
//usage:
//usage:#define ps_trivial_usage
//usage:       "[-o COL1,COL2=HEADER]" IF_FEATURE_SHOW_THREADS(" [-T]")
//usage:#define ps_full_usage "\n\n"
//usage:       "Show list of processes\n"
//usage:     "\n	-o COL1,COL2=HEADER	Select columns for display"
//usage:	IF_FEATURE_SHOW_THREADS(
//usage:     "\n	-T			Show threads"
//usage:	)
//usage:
//usage:#else /* !ENABLE_DESKTOP */
//usage:
//usage:#if !ENABLE_SELINUX && !ENABLE_FEATURE_PS_WIDE
//usage:#define USAGE_PS "\nThis version of ps accepts no options"
//usage:#else
//usage:#define USAGE_PS ""
//usage:#endif
//usage:
//usage:#define ps_trivial_usage
//usage:       ""
//usage:#define ps_full_usage "\n\n"
//usage:       "Show list of processes\n"
//usage:	USAGE_PS
//usage:	IF_SELINUX(
//usage:     "\n	-Z	Show selinux context"
//usage:	)
//usage:	IF_FEATURE_PS_WIDE(
//usage:     "\n	w	Wide output"
//usage:	)
//usage:	IF_FEATURE_PS_LONG(
//usage:     "\n	l	Long output"
//usage:	)
//usage:	IF_FEATURE_SHOW_THREADS(
//usage:     "\n	T	Show threads"
//usage:	)
//usage:
//usage:#endif /* ENABLE_DESKTOP */
//usage:
//usage:#define ps_example_usage
//usage:       "$ ps\n"
//usage:       "  PID  Uid      Gid State Command\n"
//usage:       "    1 root     root     S init\n"
//usage:       "    2 root     root     S [kflushd]\n"
//usage:       "    3 root     root     S [kupdate]\n"
//usage:       "    4 root     root     S [kpiod]\n"
//usage:       "    5 root     root     S [kswapd]\n"
//usage:       "  742 andersen andersen S [bash]\n"
//usage:       "  743 andersen andersen S -bash\n"
//usage:       "  745 root     root     S [getty]\n"
//usage:       " 2990 andersen andersen R ps\n"

#include "libbb.h"
#include "common_bufsiz.h"
#ifdef __linux__
# include <sys/sysinfo.h>
#endif

/* Absolute maximum on output line length */
enum { MAX_WIDTH = 2*1024 };

#if ENABLE_FEATURE_PS_TIME || ENABLE_FEATURE_PS_LONG
static unsigned long get_uptime(void)
{
#ifdef __linux__
	struct sysinfo info;
	if (sysinfo(&info) < 0)
		return 0;
	return info.uptime;
#elif 1
	unsigned long uptime;
	char buf[sizeof(uptime)*3 + 2];
	/* /proc/uptime is "UPTIME_SEC.NN IDLE_SEC.NN\n"
	 * (where IDLE is cumulative over all CPUs)
	 */
	if (open_read_close("/proc/uptime", buf, sizeof(buf)) <= 0)
		bb_perror_msg_and_die("can't read '%s'", "/proc/uptime");
	buf[sizeof(buf)-1] = '\0';
	sscanf(buf, "%lu", &uptime);
	return uptime;
#else
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
		return 0;
	return ts.tv_sec;
#endif
}
#endif

#if ENABLE_DESKTOP
/* TODO:
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ps.html
 * specifies (for XSI-conformant systems) following default columns
 * (l and f mark columns shown with -l and -f respectively):
 * F     l   Flags (octal and additive) associated with the process (??)
 * S     l   The state of the process
 * UID   f,l The user ID; the login name is printed with -f
 * PID       The process ID
 * PPID  f,l The parent process
 * C     f,l Processor utilization
 * PRI   l   The priority of the process; higher numbers mean lower priority
 * NI    l   Nice value
 * ADDR  l   The address of the process
 * SZ    l   The size in blocks of the core image of the process
 * WCHAN l   The event for which the process is waiting or sleeping
 * STIME f   Starting time of the process
 * TTY       The controlling terminal for the process
 * TIME      The cumulative execution time for the process
 * CMD       The command name; the full command line is shown with -f
 */
typedef struct {
	uint16_t width;
	char name6[6];
	const char *header;
	void (*f)(char *buf, int size, const procps_status_t *ps);
	int ps_flags;
} ps_out_t;

struct globals {
	ps_out_t* out;
	int out_cnt;
	int print_header;
	int need_flags;
	char *buffer;
	unsigned terminal_width;
#if ENABLE_FEATURE_PS_TIME
# if ENABLE_FEATURE_PS_UNUSUAL_SYSTEMS || !defined(__linux__)
	unsigned kernel_HZ;
# endif
	unsigned long seconds_since_boot;
#endif
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define out                (G.out               )
#define out_cnt            (G.out_cnt           )
#define print_header       (G.print_header      )
#define need_flags         (G.need_flags        )
#define buffer             (G.buffer            )
#define terminal_width     (G.terminal_width    )
#define INIT_G() do { setup_common_bufsiz(); } while (0)

#if ENABLE_FEATURE_PS_TIME
# if ENABLE_FEATURE_PS_UNUSUAL_SYSTEMS || !defined(__linux__)
#  define get_kernel_HZ() (G.kernel_HZ)
# else
    /* non-ancient Linux standardized on 100 for "times" freq */
#  define get_kernel_HZ() ((unsigned)100)
# endif
#endif

/* Print value to buf, max size+1 chars (including trailing '\0') */

static void func_user(char *buf, int size, const procps_status_t *ps)
{
#if 1
	safe_strncpy(buf, get_cached_username(ps->uid), size+1);
#else
	/* "compatible" version, but it's larger */
	/* procps 2.18 shows numeric UID if name overflows the field */
	/* TODO: get_cached_username() returns numeric string if
	 * user has no passwd record, we will display it
	 * left-justified here; too long usernames are shown
	 * as _right-justified_ IDs. Is it worth fixing? */
	const char *user = get_cached_username(ps->uid);
	if (strlen(user) <= size)
		safe_strncpy(buf, user, size+1);
	else
		sprintf(buf, "%*u", size, (unsigned)ps->uid);
#endif
}

static void func_group(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, get_cached_groupname(ps->gid), size+1);
}

static void func_comm(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, ps->comm, size+1);
}

static void func_state(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, ps->state, size+1);
}

static void func_args(char *buf, int size, const procps_status_t *ps)
{
	read_cmdline(buf, size+1, ps->pid, ps->comm);
}

static void func_pid(char *buf, int size, const procps_status_t *ps)
{
	sprintf(buf, "%*u", size, ps->pid);
}

static void func_ppid(char *buf, int size, const procps_status_t *ps)
{
	sprintf(buf, "%*u", size, ps->ppid);
}

static void func_pgid(char *buf, int size, const procps_status_t *ps)
{
	sprintf(buf, "%*u", size, ps->pgid);
}

static void func_sid(char *buf, int size, const procps_status_t *ps)
{
	sprintf(buf, "%*u", size, ps->sid);
}

static void put_lu(char *buf, int size, unsigned long u)
{
	char buf4[5];

	/* see http://en.wikipedia.org/wiki/Tera */
	smart_ulltoa4(u, buf4, " mgtpezy")[0] = '\0';
	sprintf(buf, "%.*s", size, buf4);
}

static void func_vsz(char *buf, int size, const procps_status_t *ps)
{
	put_lu(buf, size, ps->vsz);
}

static void func_rss(char *buf, int size, const procps_status_t *ps)
{
	put_lu(buf, size, ps->rss);
}

static void func_tty(char *buf, int size, const procps_status_t *ps)
{
	buf[0] = '?';
	buf[1] = '\0';
	if (ps->tty_major) /* tty field of "0" means "no tty" */
		snprintf(buf, size+1, "%u,%u", ps->tty_major, ps->tty_minor);
}

#if ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS
static void func_rgroup(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, get_cached_groupname(ps->rgid), size+1);
}
static void func_ruser(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, get_cached_username(ps->ruid), size+1);
}
static void func_nice(char *buf, int size, const procps_status_t *ps)
{
	sprintf(buf, "%*d", size, ps->niceness);
}
#endif

#if ENABLE_FEATURE_PS_TIME
static void format_time(char *buf, int size, unsigned long tt)
{
	unsigned ff;

	/* Used to show "14453:50" if tt is large. Ugly.
	 * procps-ng 3.3.10 uses "[[dd-]hh:]mm:ss" format.
	 * TODO: switch to that?
	 */

	/* Formatting for 5-char TIME column.
	 * NB: "size" is not always 5: ELAPSED is wider (7),
	 * not taking advantage of that (yet?).
	 */
	ff = tt % 60;
	tt /= 60;
	if (tt < 60) {
		snprintf(buf, size+1, "%2u:%02u", (unsigned)tt, ff);
		return;
	}
	ff = tt % 60;
	tt /= 60;
	if (tt < 24) {
		snprintf(buf, size+1, "%2uh%02u", (unsigned)tt, ff);
		return;
	}
	ff = tt % 24;
	tt /= 24;
	if (tt < 100) {
		snprintf(buf, size+1, "%2ud%02u", (unsigned)tt, ff);
		return;
	}
	snprintf(buf, size+1, "%4lud", tt);
}
static void func_etime(char *buf, int size, const procps_status_t *ps)
{
	/* elapsed time [[dd-]hh:]mm:ss; here only mm:ss */
	unsigned long mm;

	mm = ps->start_time / get_kernel_HZ();
	mm = G.seconds_since_boot - mm;
	format_time(buf, size, mm);
}
static void func_time(char *buf, int size, const procps_status_t *ps)
{
	/* cumulative time [[dd-]hh:]mm:ss; here only mm:ss */
	unsigned long mm;

	mm = (ps->utime + ps->stime) / get_kernel_HZ();
	format_time(buf, size, mm);
}
#endif

#if ENABLE_SELINUX
static void func_label(char *buf, int size, const procps_status_t *ps)
{
	safe_strncpy(buf, ps->context ? ps->context : "unknown", size+1);
}
#endif

/*
static void func_pcpu(char *buf, int size, const procps_status_t *ps)
{
}
*/

static const ps_out_t out_spec[] = {
/* Mandated by http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ps.html: */
	{ 8                  , "user"  ,"USER"   ,func_user  ,PSSCAN_UIDGID  },
	{ 8                  , "group" ,"GROUP"  ,func_group ,PSSCAN_UIDGID  },
	{ 16                 , "comm"  ,"COMMAND",func_comm  ,PSSCAN_COMM    },
	{ MAX_WIDTH          , "args"  ,"COMMAND",func_args  ,PSSCAN_COMM    },
	{ 5                  , "pid"   ,"PID"    ,func_pid   ,PSSCAN_PID     },
	{ 5                  , "ppid"  ,"PPID"   ,func_ppid  ,PSSCAN_PPID    },
	{ 5                  , "pgid"  ,"PGID"   ,func_pgid  ,PSSCAN_PGID    },
#if ENABLE_FEATURE_PS_TIME
	{ sizeof("ELAPSED")-1, "etime" ,"ELAPSED",func_etime ,PSSCAN_START_TIME },
#endif
#if ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS
	{ 5                  , "nice"  ,"NI"     ,func_nice  ,PSSCAN_NICE    },
	{ 8                  , "rgroup","RGROUP" ,func_rgroup,PSSCAN_RUIDGID },
	{ 8                  , "ruser" ,"RUSER"  ,func_ruser ,PSSCAN_RUIDGID },
//	{ 5                  , "pcpu"  ,"%CPU"   ,func_pcpu  ,PSSCAN_        },
#endif
#if ENABLE_FEATURE_PS_TIME
	{ 5                  , "time"  ,"TIME"   ,func_time  ,PSSCAN_STIME | PSSCAN_UTIME },
#endif
	{ 6                  , "tty"   ,"TT"     ,func_tty   ,PSSCAN_TTY     },
	{ 4                  , "vsz"   ,"VSZ"    ,func_vsz   ,PSSCAN_VSZ     },
/* Not mandated, but useful: */
	{ 5                  , "sid"   ,"SID"    ,func_sid   ,PSSCAN_SID     },
	{ 4                  , "stat"  ,"STAT"   ,func_state ,PSSCAN_STATE   },
	{ 4                  , "rss"   ,"RSS"    ,func_rss   ,PSSCAN_RSS     },
#if ENABLE_SELINUX
	{ 35                 , "label" ,"LABEL"  ,func_label ,PSSCAN_CONTEXT },
#endif
};

static ps_out_t* new_out_t(void)
{
	out = xrealloc_vector(out, 2, out_cnt);
	return &out[out_cnt++];
}

static const ps_out_t* find_out_spec(const char *name)
{
	unsigned i;
	char buf[ARRAY_SIZE(out_spec)*7 + 1];
	char *p = buf;

	for (i = 0; i < ARRAY_SIZE(out_spec); i++) {
		if (strncmp(name, out_spec[i].name6, 6) == 0)
			return &out_spec[i];
		p += sprintf(p, "%.6s,", out_spec[i].name6);
	}
	p[-1] = '\0';
	bb_error_msg_and_die("bad -o argument '%s', supported arguments: %s", name, buf);
}

static void parse_o(char* opt)
{
	ps_out_t* new;
	// POSIX: "-o is blank- or comma-separated list" (FIXME)
	char *comma, *equal;
	while (1) {
		comma = strchr(opt, ',');
		equal = strchr(opt, '=');
		if (comma && (!equal || equal > comma)) {
			*comma = '\0';
			*new_out_t() = *find_out_spec(opt);
			*comma = ',';
			opt = comma + 1;
			continue;
		}
		break;
	}
	// opt points to last spec in comma separated list.
	// This one can have =HEADER part.
	new = new_out_t();
	if (equal)
		*equal = '\0';
	*new = *find_out_spec(opt);
	if (equal) {
		*equal = '=';
		new->header = equal + 1;
		// POSIX: the field widths shall be ... at least as wide as
		// the header text (default or overridden value).
		// If the header text is null, such as -o user=,
		// the field width shall be at least as wide as the
		// default header text
		if (new->header[0]) {
			new->width = strlen(new->header);
			print_header = 1;
		}
	} else
		print_header = 1;
}

static void alloc_line_buffer(void)
{
	int i;
	int width = 0;
	for (i = 0; i < out_cnt; i++) {
		need_flags |= out[i].ps_flags;
		if (out[i].header[0]) {
			print_header = 1;
		}
		width += out[i].width + 1; /* "FIELD " */
		if ((int)(width - terminal_width) > 0) {
			/* The rest does not fit on the screen */
			//out[i].width -= (width - terminal_width - 1);
			out_cnt = i + 1;
			break;
		}
	}
#if ENABLE_SELINUX
	if (!is_selinux_enabled())
		need_flags &= ~PSSCAN_CONTEXT;
#endif
	buffer = xmalloc(width + 1); /* for trailing \0 */
}

static void format_header(void)
{
	int i;
	ps_out_t* op;
	char *p;

	if (!print_header)
		return;
	p = buffer;
	i = 0;
	if (out_cnt) {
		while (1) {
			op = &out[i];
			if (++i == out_cnt) /* do not pad last field */
				break;
			p += sprintf(p, "%-*s ", op->width, op->header);
		}
		strcpy(p, op->header);
	}
	printf("%.*s\n", terminal_width, buffer);
}

static void format_process(const procps_status_t *ps)
{
	int i, len;
	char *p = buffer;
	i = 0;
	if (out_cnt) while (1) {
		out[i].f(p, out[i].width, ps);
		// POSIX: Any field need not be meaningful in all
		// implementations. In such a case a hyphen ( '-' )
		// should be output in place of the field value.
		if (!p[0]) {
			p[0] = '-';
			p[1] = '\0';
		}
		len = strlen(p);
		p += len;
		len = out[i].width - len + 1;
		if (++i == out_cnt) /* do not pad last field */
			break;
		p += sprintf(p, "%*s", len, "");
	}
	printf("%.*s\n", terminal_width, buffer);
}

#if ENABLE_SELINUX
# define SELINUX_O_PREFIX "label,"
# define DEFAULT_O_STR    (SELINUX_O_PREFIX "pid,user" IF_FEATURE_PS_TIME(",time") ",args")
#else
# define DEFAULT_O_STR    ("pid,user" IF_FEATURE_PS_TIME(",time") ",args")
#endif

int ps_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ps_main(int argc UNUSED_PARAM, char **argv)
{
	procps_status_t *p;
	llist_t* opt_o = NULL;
	char default_o[sizeof(DEFAULT_O_STR)];
#if ENABLE_SELINUX || ENABLE_FEATURE_SHOW_THREADS
	int opt;
#endif
	enum {
		OPT_Z = (1 << 0),
		OPT_o = (1 << 1),
		OPT_a = (1 << 2),
		OPT_A = (1 << 3),
		OPT_d = (1 << 4),
		OPT_e = (1 << 5),
		OPT_f = (1 << 6),
		OPT_l = (1 << 7),
		OPT_T = (1 << 8) * ENABLE_FEATURE_SHOW_THREADS,
	};

	INIT_G();
#if ENABLE_FEATURE_PS_TIME
	G.seconds_since_boot = get_uptime();
# if ENABLE_FEATURE_PS_UNUSUAL_SYSTEMS || !defined(__linux__)
	G.kernel_HZ = bb_clk_tck(); /* this is sysconf(_SC_CLK_TCK) */
# endif
#endif

	// POSIX:
	// -a  Write information for all processes associated with terminals
	//     Implementations may omit session leaders from this list
	// -A  Write information for all processes
	// -d  Write information for all processes, except session leaders
	// -e  Write information for all processes (equivalent to -A)
	// -f  Generate a full listing
	// -l  Generate a long listing
	// -o col1,col2,col3=header
	//     Select which columns to display
	/* We allow (and ignore) most of the above. FIXME.
	 * -T is picked for threads (POSIX hasn't standardized it).
	 * procps v3.2.7 supports -T and shows tids as SPID column,
	 * it also supports -L where it shows tids as LWP column.
	 */
#if ENABLE_SELINUX || ENABLE_FEATURE_SHOW_THREADS
	opt =
#endif
		getopt32(argv, "Zo:*aAdefl"IF_FEATURE_SHOW_THREADS("T"), &opt_o);

	if (opt_o) {
		do {
			parse_o(llist_pop(&opt_o));
		} while (opt_o);
	} else {
		/* Below: parse_o() needs char*, NOT const char*,
		 * can't pass it constant string. Need to make a copy first.
		 */
#if ENABLE_SELINUX
		if (!(opt & OPT_Z) || !is_selinux_enabled()) {
			/* no -Z or no SELinux: do not show LABEL */
			strcpy(default_o, DEFAULT_O_STR + sizeof(SELINUX_O_PREFIX)-1);
		} else
#endif
		{
			strcpy(default_o, DEFAULT_O_STR);
		}
		parse_o(default_o);
	}
#if ENABLE_FEATURE_SHOW_THREADS
	if (opt & OPT_T)
		need_flags |= PSSCAN_TASKS;
#endif

	/* Was INT_MAX, but some libc's go belly up with printf("%.*s")
	 * and such large widths */
	terminal_width = MAX_WIDTH;
	if (isatty(1)) {
		terminal_width = get_terminal_width(0);
		if (--terminal_width > MAX_WIDTH)
			terminal_width = MAX_WIDTH;
	}
	alloc_line_buffer();
	format_header();

	p = NULL;
	while ((p = procps_scan(p, need_flags)) != NULL) {
		format_process(p);
	}

	return EXIT_SUCCESS;
}


#else /* !ENABLE_DESKTOP */


int ps_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ps_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	procps_status_t *p;
	int psscan_flags = PSSCAN_PID | PSSCAN_UIDGID
			| PSSCAN_STATE | PSSCAN_VSZ | PSSCAN_COMM;
	unsigned terminal_width IF_NOT_FEATURE_PS_WIDE(= 79);
	enum {
		OPT_Z = (1 << 0) * ENABLE_SELINUX,
		OPT_T = (1 << ENABLE_SELINUX) * ENABLE_FEATURE_SHOW_THREADS,
		OPT_l = (1 << ENABLE_SELINUX) * (1 << ENABLE_FEATURE_SHOW_THREADS) * ENABLE_FEATURE_PS_LONG,
	};
#if ENABLE_FEATURE_PS_LONG
	time_t now = now; /* for compiler */
	unsigned long uptime = uptime;
#endif
	/* If we support any options, parse argv */
#if ENABLE_SELINUX || ENABLE_FEATURE_SHOW_THREADS || ENABLE_FEATURE_PS_WIDE || ENABLE_FEATURE_PS_LONG
	int opts = 0;
# if ENABLE_FEATURE_PS_WIDE
	/* -w is a bit complicated */
	int w_count = 0;
	make_all_argv_opts(argv);
	opts = getopt32(argv, "^"
		IF_SELINUX("Z")IF_FEATURE_SHOW_THREADS("T")IF_FEATURE_PS_LONG("l")"w"
		"\0" "ww",
		&w_count
	);
	/* if w is given once, GNU ps sets the width to 132,
	 * if w is given more than once, it is "unlimited"
	 */
	if (w_count) {
		terminal_width = (w_count == 1) ? 132 : MAX_WIDTH;
	} else {
		terminal_width = get_terminal_width(0);
		/* Go one less... */
		if (--terminal_width > MAX_WIDTH)
			terminal_width = MAX_WIDTH;
	}
# else
	/* -w is not supported, only -Z and/or -T */
	make_all_argv_opts(argv);
	opts = getopt32(argv, IF_SELINUX("Z")IF_FEATURE_SHOW_THREADS("T")IF_FEATURE_PS_LONG("l"));
# endif

# if ENABLE_SELINUX
	if ((opts & OPT_Z) && is_selinux_enabled()) {
		psscan_flags = PSSCAN_PID | PSSCAN_CONTEXT
				| PSSCAN_STATE | PSSCAN_COMM;
		puts("  PID CONTEXT                          STAT COMMAND");
	} else
# endif
	if (opts & OPT_l) {
		psscan_flags = PSSCAN_STATE | PSSCAN_UIDGID | PSSCAN_PID | PSSCAN_PPID
			| PSSCAN_TTY | PSSCAN_STIME | PSSCAN_UTIME | PSSCAN_COMM
			| PSSCAN_VSZ | PSSCAN_RSS;
/* http://pubs.opengroup.org/onlinepubs/9699919799/utilities/ps.html
 * mandates for -l:
 * -F     Flags (?)
 * S      State
 * UID,PID,PPID
 * -C     CPU usage
 * -PRI   The priority of the process; higher numbers mean lower priority
 * -NI    Nice value
 * -ADDR  The address of the process (?)
 * SZ     The size in blocks of the core image
 * -WCHAN The event for which the process is waiting or sleeping
 * TTY
 * TIME   The cumulative execution time
 * CMD
 * We don't show fields marked with '-'.
 * We show VSZ and RSS instead of SZ.
 * We also show STIME (standard says that -f shows it, -l doesn't).
 */
		puts("S   UID   PID  PPID   VSZ   RSS TTY   STIME TIME     CMD");
# if ENABLE_FEATURE_PS_LONG
		now = time(NULL);
		uptime = get_uptime();
# endif
	}
	else {
		puts("  PID USER       VSZ STAT COMMAND");
	}
	if (opts & OPT_T) {
		psscan_flags |= PSSCAN_TASKS;
	}
#endif

	p = NULL;
	while ((p = procps_scan(p, psscan_flags)) != NULL) {
		int len;
#if ENABLE_SELINUX
		if (psscan_flags & PSSCAN_CONTEXT) {
			len = printf("%5u %-32.32s %s  ",
					p->pid,
					p->context ? p->context : "unknown",
					p->state);
		} else
#endif
		{
			char buf6[6];
			smart_ulltoa5(p->vsz, buf6, " mgtpezy")[0] = '\0';
#if ENABLE_FEATURE_PS_LONG
			if (opts & OPT_l) {
				char bufr[6], stime_str[6];
				char tty[2 * sizeof(int)*3 + 2];
				char *endp;
				unsigned sut = (p->stime + p->utime) / 100;
				unsigned elapsed = uptime - (p->start_time / 100);
				time_t start = now - elapsed;
				struct tm *tm = localtime(&start);

				smart_ulltoa5(p->rss, bufr, " mgtpezy")[0] = '\0';

				if (p->tty_major == 136)
					/* It should be pts/N, not ptsN, but N > 9
					 * will overflow field width...
					 */
					endp = stpcpy(tty, "pts");
				else
				if (p->tty_major == 4) {
					endp = stpcpy(tty, "tty");
					if (p->tty_minor >= 64) {
						p->tty_minor -= 64;
						*endp++ = 'S';
					}
				}
				else
					endp = tty + sprintf(tty, "%d:", p->tty_major);
				strcpy(endp, utoa(p->tty_minor));

				strftime(stime_str, 6, (elapsed >= (24 * 60 * 60)) ? "%b%d" : "%H:%M", tm);
				stime_str[5] = '\0';
				//            S  UID PID PPID VSZ RSS TTY STIME TIME        CMD
				len = printf("%c %5u %5u %5u %5s %5s %-5s %s %02u:%02u:%02u ",
					p->state[0], p->uid, p->pid, p->ppid, buf6, bufr, tty,
					stime_str, sut / 3600, (sut % 3600) / 60, sut % 60);
			} else
#endif
			{
				const char *user = get_cached_username(p->uid);
				len = printf("%5u %-8.8s %s %s  ",
					p->pid, user, buf6, p->state);
			}
		}

		{
			int sz = terminal_width - len;
			if (sz >= 0) {
				char buf[sz + 1];
				read_cmdline(buf, sz, p->pid, p->comm);
				puts(buf);
			}
		}
	}
	if (ENABLE_FEATURE_CLEAN_UP)
		clear_username_cache();
	return EXIT_SUCCESS;
}

#endif /* !ENABLE_DESKTOP */
