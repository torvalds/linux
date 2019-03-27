/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dtrace.h>
#include <sys/lockstat.h>
#include <alloca.h>
#include <signal.h>
#include <assert.h>

#ifdef illumos
#define	GETOPT_EOF	EOF
#else
#include <sys/time.h>
#include <sys/resource.h>

#define	mergesort(a, b, c, d)	lsmergesort(a, b, c, d)
#define	GETOPT_EOF		(-1)

typedef	uintptr_t	pc_t;
#endif

#define	LOCKSTAT_OPTSTR	"x:bths:n:d:i:l:f:e:ckwWgCHEATID:RpPo:V"

#define	LS_MAX_STACK_DEPTH	50
#define	LS_MAX_EVENTS		64

typedef struct lsrec {
	struct lsrec	*ls_next;	/* next in hash chain */
#ifdef illumos
	uintptr_t	ls_lock;	/* lock address */
#else
	char		*ls_lock;	/* lock name */
#endif
	uintptr_t	ls_caller;	/* caller address */
	uint32_t	ls_count;	/* cumulative event count */
	uint32_t	ls_event;	/* type of event */
	uintptr_t	ls_refcnt;	/* cumulative reference count */
	uint64_t	ls_time;	/* cumulative event duration */
	uint32_t	ls_hist[64];	/* log2(duration) histogram */
	uintptr_t	ls_stack[LS_MAX_STACK_DEPTH];
} lsrec_t;

typedef struct lsdata {
	struct lsrec	*lsd_next;	/* next available */
	int		lsd_count;	/* number of records */
} lsdata_t;

/*
 * Definitions for the types of experiments which can be run.  They are
 * listed in increasing order of memory cost and processing time cost.
 * The numerical value of each type is the number of bytes needed per record.
 */
#define	LS_BASIC	offsetof(lsrec_t, ls_time)
#define	LS_TIME		offsetof(lsrec_t, ls_hist[0])
#define	LS_HIST		offsetof(lsrec_t, ls_stack[0])
#define	LS_STACK(depth)	offsetof(lsrec_t, ls_stack[depth])

static void report_stats(FILE *, lsrec_t **, size_t, uint64_t, uint64_t);
static void report_trace(FILE *, lsrec_t **);

extern int symtab_init(void);
extern char *addr_to_sym(uintptr_t, uintptr_t *, size_t *);
extern uintptr_t sym_to_addr(char *name);
extern size_t sym_size(char *name);
extern char *strtok_r(char *, const char *, char **);

#define	DEFAULT_NRECS	10000
#define	DEFAULT_HZ	97
#define	MAX_HZ		1000
#define	MIN_AGGSIZE	(16 * 1024)
#define	MAX_AGGSIZE	(32 * 1024 * 1024)

static int g_stkdepth;
static int g_topn = INT_MAX;
static hrtime_t g_elapsed;
static int g_rates = 0;
static int g_pflag = 0;
static int g_Pflag = 0;
static int g_wflag = 0;
static int g_Wflag = 0;
static int g_cflag = 0;
static int g_kflag = 0;
static int g_gflag = 0;
static int g_Vflag = 0;
static int g_tracing = 0;
static size_t g_recsize;
static size_t g_nrecs;
static int g_nrecs_used;
static uchar_t g_enabled[LS_MAX_EVENTS];
static hrtime_t g_min_duration[LS_MAX_EVENTS];
static dtrace_hdl_t *g_dtp;
static char *g_predicate;
static char *g_ipredicate;
static char *g_prog;
static int g_proglen;
static int g_dropped;

typedef struct ls_event_info {
	char	ev_type;
	char	ev_lhdr[20];
	char	ev_desc[80];
	char	ev_units[10];
	char	ev_name[DTRACE_NAMELEN];
	char	*ev_predicate;
	char	*ev_acquire;
} ls_event_info_t;

static ls_event_info_t g_event_info[LS_MAX_EVENTS] = {
	{ 'C',	"Lock",	"Adaptive mutex spin",			"nsec",
	    "lockstat:::adaptive-spin" },
	{ 'C',	"Lock",	"Adaptive mutex block",			"nsec",
	    "lockstat:::adaptive-block" },
	{ 'C',	"Lock",	"Spin lock spin",			"nsec",
	    "lockstat:::spin-spin" },
	{ 'C',	"Lock",	"Thread lock spin",			"nsec",
	    "lockstat:::thread-spin" },
	{ 'C',	"Lock",	"R/W writer blocked by writer",		"nsec",
	    "lockstat:::rw-block", "arg2 == 0 && arg3 == 1" },
	{ 'C',	"Lock",	"R/W writer blocked by readers",	"nsec",
	    "lockstat:::rw-block", "arg2 == 0 && arg3 == 0 && arg4" },
	{ 'C',	"Lock",	"R/W reader blocked by writer",		"nsec",
	    "lockstat:::rw-block", "arg2 != 0 && arg3 == 1" },
	{ 'C',	"Lock",	"R/W reader blocked by write wanted",	"nsec",
	    "lockstat:::rw-block", "arg2 != 0 && arg3 == 0 && arg4" },
	{ 'C',	"Lock",	"R/W writer spin on writer",		"nsec",
	    "lockstat:::rw-spin", "arg2 == 0 && arg3 == 1" },
	{ 'C',	"Lock",	"R/W writer spin on readers",		"nsec",
	    "lockstat:::rw-spin", "arg2 == 0 && arg3 == 0 && arg4" },
	{ 'C',	"Lock",	"R/W reader spin on writer",		"nsec",
	    "lockstat:::rw-spin", "arg2 != 0 && arg3 == 1" },
	{ 'C',	"Lock",	"R/W reader spin on write wanted",	"nsec",
	    "lockstat:::rw-spin", "arg2 != 0 && arg3 == 0 && arg4" },
	{ 'C',	"Lock",	"SX exclusive block",			"nsec",
	    "lockstat:::sx-block", "arg2 == 0" },
	{ 'C',	"Lock",	"SX shared block",			"nsec",
	    "lockstat:::sx-block", "arg2 != 0" },
	{ 'C',	"Lock",	"SX exclusive spin",			"nsec",
	    "lockstat:::sx-spin", "arg2 == 0" },
	{ 'C',	"Lock",	"SX shared spin",			"nsec",
	    "lockstat:::sx-spin", "arg2 != 0" },
	{ 'C',	"Lock",	"Unknown event (type 16)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 17)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 18)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 19)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 20)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 21)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 22)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 23)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 24)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 25)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 26)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 27)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 28)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 29)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 30)",		"units"	},
	{ 'C',	"Lock",	"Unknown event (type 31)",		"units"	},
	{ 'H',	"Lock",	"Adaptive mutex hold",			"nsec",
	    "lockstat:::adaptive-release", NULL,
	    "lockstat:::adaptive-acquire" },
	{ 'H',	"Lock",	"Spin lock hold",			"nsec",
	    "lockstat:::spin-release", NULL,
	    "lockstat:::spin-acquire" },
	{ 'H',	"Lock",	"R/W writer hold",			"nsec",
	    "lockstat:::rw-release", "arg1 == 0",
	    "lockstat:::rw-acquire" },
	{ 'H',	"Lock",	"R/W reader hold",			"nsec",
	    "lockstat:::rw-release", "arg1 == 1",
	    "lockstat:::rw-acquire" },
	{ 'H',	"Lock",	"SX shared hold",			"nsec",
	    "lockstat:::sx-release", "arg1 == 0",
	    "lockstat:::sx-acquire" },
	{ 'H',	"Lock",	"SX exclusive hold",			"nsec",
	    "lockstat:::sx-release", "arg1 == 1",
	    "lockstat:::sx-acquire" },
	{ 'H',	"Lock",	"Unknown event (type 38)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 39)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 40)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 41)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 42)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 43)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 44)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 45)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 46)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 47)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 48)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 49)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 50)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 51)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 52)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 53)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 54)",		"units"	},
	{ 'H',	"Lock",	"Unknown event (type 55)",		"units"	},
#ifdef illumos
	{ 'I',	"CPU+PIL", "Profiling interrupt",		"nsec",
#else
	{ 'I',	"CPU+Pri_Class", "Profiling interrupt",		"nsec",
#endif
	    "profile:::profile-97", NULL },
	{ 'I',	"Lock",	"Unknown event (type 57)",		"units"	},
	{ 'I',	"Lock",	"Unknown event (type 58)",		"units"	},
	{ 'I',	"Lock",	"Unknown event (type 59)",		"units"	},
	{ 'E',	"Lock",	"Recursive lock entry detected",	"(N/A)",
	    "lockstat:::rw-release", NULL, "lockstat:::rw-acquire" },
	{ 'E',	"Lock",	"Lockstat enter failure",		"(N/A)"	},
	{ 'E',	"Lock",	"Lockstat exit failure",		"nsec"	},
	{ 'E',	"Lock",	"Lockstat record failure",		"(N/A)"	},
};

#ifndef illumos
static char *g_pri_class[] = {
	"",
	"Intr",
	"RealT",
	"TShar",
	"Idle"
};
#endif

static void
fail(int do_perror, const char *message, ...)
{
	va_list args;
	int save_errno = errno;

	va_start(args, message);
	(void) fprintf(stderr, "lockstat: ");
	(void) vfprintf(stderr, message, args);
	va_end(args);
	if (do_perror)
		(void) fprintf(stderr, ": %s", strerror(save_errno));
	(void) fprintf(stderr, "\n");
	exit(2);
}

static void
dfail(const char *message, ...)
{
	va_list args;

	va_start(args, message);
	(void) fprintf(stderr, "lockstat: ");
	(void) vfprintf(stderr, message, args);
	va_end(args);
	(void) fprintf(stderr, ": %s\n",
	    dtrace_errmsg(g_dtp, dtrace_errno(g_dtp)));

	exit(2);
}

static void
show_events(char event_type, char *desc)
{
	int i, first = -1, last;

	for (i = 0; i < LS_MAX_EVENTS; i++) {
		ls_event_info_t *evp = &g_event_info[i];
		if (evp->ev_type != event_type ||
		    strncmp(evp->ev_desc, "Unknown event", 13) == 0)
			continue;
		if (first == -1)
			first = i;
		last = i;
	}

	(void) fprintf(stderr,
	    "\n%s events (lockstat -%c or lockstat -e %d-%d):\n\n",
	    desc, event_type, first, last);

	for (i = first; i <= last; i++)
		(void) fprintf(stderr,
		    "%4d = %s\n", i, g_event_info[i].ev_desc);
}

static void
usage(void)
{
	(void) fprintf(stderr,
	    "Usage: lockstat [options] command [args]\n"
	    "\nGeneral options:\n\n"
	    "  -V              print the corresponding D program\n"
	    "\nEvent selection options:\n\n"
	    "  -C              watch contention events [on by default]\n"
	    "  -E              watch error events [off by default]\n"
	    "  -H              watch hold events [off by default]\n"
	    "  -I              watch interrupt events [off by default]\n"
	    "  -A              watch all lock events [equivalent to -CH]\n"
	    "  -e event_list   only watch the specified events (shown below);\n"
	    "                  <event_list> is a comma-separated list of\n"
	    "                  events or ranges of events, e.g. 1,4-7,35\n"
	    "  -i rate         interrupt rate for -I [default: %d Hz]\n"
	    "\nData gathering options:\n\n"
	    "  -b              basic statistics (lock, caller, event count)\n"
	    "  -t              timing for all events [default]\n"
	    "  -h              histograms for event times\n"
	    "  -s depth        stack traces <depth> deep\n"
	    "  -x opt[=val]    enable or modify DTrace options\n"
	    "\nData filtering options:\n\n"
	    "  -n nrecords     maximum number of data records [default: %d]\n"
	    "  -l lock[,size]  only watch <lock>, which can be specified as a\n"
	    "                  symbolic name or hex address; <size> defaults\n"
	    "                  to the ELF symbol size if available, 1 if not\n"
	    "  -f func[,size]  only watch events generated by <func>\n"
	    "  -d duration     only watch events longer than <duration>\n"
	    "  -T              trace (rather than sample) events\n"
	    "\nData reporting options:\n\n"
#ifdef illumos
	    "  -c              coalesce lock data for arrays like pse_mutex[]\n"
#endif
	    "  -k              coalesce PCs within functions\n"
	    "  -g              show total events generated by function\n"
	    "  -w              wherever: don't distinguish events by caller\n"
	    "  -W              whichever: don't distinguish events by lock\n"
	    "  -R              display rates rather than counts\n"
	    "  -p              parsable output format (awk(1)-friendly)\n"
	    "  -P              sort lock data by (count * avg_time) product\n"
	    "  -D n            only display top <n> events of each type\n"
	    "  -o filename     send output to <filename>\n",
	    DEFAULT_HZ, DEFAULT_NRECS);

	show_events('C', "Contention");
	show_events('H', "Hold-time");
	show_events('I', "Interrupt");
	show_events('E', "Error");
	(void) fprintf(stderr, "\n");

	exit(1);
}

static int
lockcmp(lsrec_t *a, lsrec_t *b)
{
	int i;

	if (a->ls_event < b->ls_event)
		return (-1);
	if (a->ls_event > b->ls_event)
		return (1);

	for (i = g_stkdepth - 1; i >= 0; i--) {
		if (a->ls_stack[i] < b->ls_stack[i])
			return (-1);
		if (a->ls_stack[i] > b->ls_stack[i])
			return (1);
	}

	if (a->ls_caller < b->ls_caller)
		return (-1);
	if (a->ls_caller > b->ls_caller)
		return (1);

#ifdef illumos
	if (a->ls_lock < b->ls_lock)
		return (-1);
	if (a->ls_lock > b->ls_lock)
		return (1);

	return (0);
#else
	return (strcmp(a->ls_lock, b->ls_lock));
#endif
}

static int
countcmp(lsrec_t *a, lsrec_t *b)
{
	if (a->ls_event < b->ls_event)
		return (-1);
	if (a->ls_event > b->ls_event)
		return (1);

	return (b->ls_count - a->ls_count);
}

static int
timecmp(lsrec_t *a, lsrec_t *b)
{
	if (a->ls_event < b->ls_event)
		return (-1);
	if (a->ls_event > b->ls_event)
		return (1);

	if (a->ls_time < b->ls_time)
		return (1);
	if (a->ls_time > b->ls_time)
		return (-1);

	return (0);
}

static int
lockcmp_anywhere(lsrec_t *a, lsrec_t *b)
{
	if (a->ls_event < b->ls_event)
		return (-1);
	if (a->ls_event > b->ls_event)
		return (1);

#ifdef illumos
	if (a->ls_lock < b->ls_lock)
		return (-1);
	if (a->ls_lock > b->ls_lock)
		return (1);

	return (0);
#else
	return (strcmp(a->ls_lock, b->ls_lock));
#endif
}

static int
lock_and_count_cmp_anywhere(lsrec_t *a, lsrec_t *b)
{
#ifndef illumos
	int cmp;
#endif

	if (a->ls_event < b->ls_event)
		return (-1);
	if (a->ls_event > b->ls_event)
		return (1);

#ifdef illumos
	if (a->ls_lock < b->ls_lock)
		return (-1);
	if (a->ls_lock > b->ls_lock)
		return (1);
#else
	cmp = strcmp(a->ls_lock, b->ls_lock);
	if (cmp != 0)
		return (cmp);
#endif

	return (b->ls_count - a->ls_count);
}

static int
sitecmp_anylock(lsrec_t *a, lsrec_t *b)
{
	int i;

	if (a->ls_event < b->ls_event)
		return (-1);
	if (a->ls_event > b->ls_event)
		return (1);

	for (i = g_stkdepth - 1; i >= 0; i--) {
		if (a->ls_stack[i] < b->ls_stack[i])
			return (-1);
		if (a->ls_stack[i] > b->ls_stack[i])
			return (1);
	}

	if (a->ls_caller < b->ls_caller)
		return (-1);
	if (a->ls_caller > b->ls_caller)
		return (1);

	return (0);
}

static int
site_and_count_cmp_anylock(lsrec_t *a, lsrec_t *b)
{
	int i;

	if (a->ls_event < b->ls_event)
		return (-1);
	if (a->ls_event > b->ls_event)
		return (1);

	for (i = g_stkdepth - 1; i >= 0; i--) {
		if (a->ls_stack[i] < b->ls_stack[i])
			return (-1);
		if (a->ls_stack[i] > b->ls_stack[i])
			return (1);
	}

	if (a->ls_caller < b->ls_caller)
		return (-1);
	if (a->ls_caller > b->ls_caller)
		return (1);

	return (b->ls_count - a->ls_count);
}

static void
lsmergesort(int (*cmp)(lsrec_t *, lsrec_t *), lsrec_t **a, lsrec_t **b, int n)
{
	int m = n / 2;
	int i, j;

	if (m > 1)
		lsmergesort(cmp, a, b, m);
	if (n - m > 1)
		lsmergesort(cmp, a + m, b + m, n - m);
	for (i = m; i > 0; i--)
		b[i - 1] = a[i - 1];
	for (j = m - 1; j < n - 1; j++)
		b[n + m - j - 2] = a[j + 1];
	while (i < j)
		*a++ = cmp(b[i], b[j]) < 0 ? b[i++] : b[j--];
	*a = b[i];
}

static void
coalesce(int (*cmp)(lsrec_t *, lsrec_t *), lsrec_t **lock, int n)
{
	int i, j;
	lsrec_t *target, *current;

	target = lock[0];

	for (i = 1; i < n; i++) {
		current = lock[i];
		if (cmp(current, target) != 0) {
			target = current;
			continue;
		}
		current->ls_event = LS_MAX_EVENTS;
		target->ls_count += current->ls_count;
		target->ls_refcnt += current->ls_refcnt;
		if (g_recsize < LS_TIME)
			continue;
		target->ls_time += current->ls_time;
		if (g_recsize < LS_HIST)
			continue;
		for (j = 0; j < 64; j++)
			target->ls_hist[j] += current->ls_hist[j];
	}
}

static void
coalesce_symbol(uintptr_t *addrp)
{
	uintptr_t symoff;
	size_t symsize;

	if (addr_to_sym(*addrp, &symoff, &symsize) != NULL && symoff < symsize)
		*addrp -= symoff;
}

static void
predicate_add(char **pred, char *what, char *cmp, uintptr_t value)
{
	char *new;
	int len, newlen;

	if (what == NULL)
		return;

	if (*pred == NULL) {
		*pred = malloc(1);
		*pred[0] = '\0';
	}

	len = strlen(*pred);
	newlen = len + strlen(what) + 32 + strlen("( && )");
	new = malloc(newlen);

	if (*pred[0] != '\0') {
		if (cmp != NULL) {
			(void) sprintf(new, "(%s) && (%s %s 0x%p)",
			    *pred, what, cmp, (void *)value);
		} else {
			(void) sprintf(new, "(%s) && (%s)", *pred, what);
		}
	} else {
		if (cmp != NULL) {
			(void) sprintf(new, "%s %s 0x%p",
			    what, cmp, (void *)value);
		} else {
			(void) sprintf(new, "%s", what);
		}
	}

	free(*pred);
	*pred = new;
}

static void
predicate_destroy(char **pred)
{
	free(*pred);
	*pred = NULL;
}

static void
filter_add(char **filt, char *what, uintptr_t base, uintptr_t size)
{
	char buf[256], *c = buf, *new;
	int len, newlen;

	if (*filt == NULL) {
		*filt = malloc(1);
		*filt[0] = '\0';
	}

#ifdef illumos
	(void) sprintf(c, "%s(%s >= 0x%p && %s < 0x%p)", *filt[0] != '\0' ?
	    " || " : "", what, (void *)base, what, (void *)(base + size));
#else
	(void) sprintf(c, "%s(%s >= %p && %s < %p)", *filt[0] != '\0' ?
	    " || " : "", what, (void *)base, what, (void *)(base + size));
#endif

	newlen = (len = strlen(*filt) + 1) + strlen(c);
	new = malloc(newlen);
	bcopy(*filt, new, len);
	(void) strcat(new, c);
	free(*filt);
	*filt = new;
}

static void
filter_destroy(char **filt)
{
	free(*filt);
	*filt = NULL;
}

static void
dprog_add(const char *fmt, ...)
{
	va_list args;
	int size, offs;
	char c;

	va_start(args, fmt);
	size = vsnprintf(&c, 1, fmt, args) + 1;
	va_end(args);

	if (g_proglen == 0) {
		offs = 0;
	} else {
		offs = g_proglen - 1;
	}

	g_proglen = offs + size;

	if ((g_prog = realloc(g_prog, g_proglen)) == NULL)
		fail(1, "failed to reallocate program text");

	va_start(args, fmt);
	(void) vsnprintf(&g_prog[offs], size, fmt, args);
	va_end(args);
}

/*
 * This function may read like an open sewer, but keep in mind that programs
 * that generate other programs are rarely pretty.  If one has the unenviable
 * task of maintaining or -- worse -- extending this code, use the -V option
 * to examine the D program as generated by this function.
 */
static void
dprog_addevent(int event)
{
	ls_event_info_t *info = &g_event_info[event];
	char *pred = NULL;
	char stack[20];
	const char *arg0, *caller;
	char *arg1 = "arg1";
	char buf[80];
	hrtime_t dur;
	int depth;

	if (info->ev_name[0] == '\0')
		return;

	if (info->ev_type == 'I') {
		/*
		 * For interrupt events, arg0 (normally the lock pointer) is
		 * the CPU address plus the current pil, and arg1 (normally
		 * the number of nanoseconds) is the number of nanoseconds
		 * late -- and it's stored in arg2.
		 */
#ifdef illumos
		arg0 = "(uintptr_t)curthread->t_cpu + \n"
		    "\t    curthread->t_cpu->cpu_profile_pil";
#else
		arg0 = "(uintptr_t)(curthread->td_oncpu << 16) + \n"
		    "\t    0x01000000 + curthread->td_pri_class";
#endif
		caller = "(uintptr_t)arg0";
		arg1 = "arg2";
	} else {
#ifdef illumos
		arg0 = "(uintptr_t)arg0";
#else
		arg0 = "stringof(args[0]->lock_object.lo_name)";
#endif
		caller = "caller";
	}

	if (g_recsize > LS_HIST) {
		for (depth = 0; g_recsize > LS_STACK(depth); depth++)
			continue;

		if (g_tracing) {
			(void) sprintf(stack, "\tstack(%d);\n", depth);
		} else {
			(void) sprintf(stack, ", stack(%d)", depth);
		}
	} else {
		(void) sprintf(stack, "");
	}

	if (info->ev_acquire != NULL) {
		/*
		 * If this is a hold event, we need to generate an additional
		 * clause for the acquire; the clause for the release will be
		 * generated with the aggregating statement, below.
		 */
		dprog_add("%s\n", info->ev_acquire);
		predicate_add(&pred, info->ev_predicate, NULL, 0);
		predicate_add(&pred, g_predicate, NULL, 0);
		if (pred != NULL)
			dprog_add("/%s/\n", pred);

		dprog_add("{\n");
		(void) sprintf(buf, "self->ev%d[(uintptr_t)arg0]", event);

		if (info->ev_type == 'H') {
			dprog_add("\t%s = timestamp;\n", buf);
		} else {
			/*
			 * If this isn't a hold event, it's the recursive
			 * error event.  For this, we simply bump the
			 * thread-local, per-lock count.
			 */
			dprog_add("\t%s++;\n", buf);
		}

		dprog_add("}\n\n");
		predicate_destroy(&pred);
		pred = NULL;

		if (info->ev_type == 'E') {
			/*
			 * If this is the recursive lock error event, we need
			 * to generate an additional clause to decrement the
			 * thread-local, per-lock count.  This assures that we
			 * only execute the aggregating clause if we have
			 * recursive entry.
			 */
			dprog_add("%s\n", info->ev_name);
			dprog_add("/%s/\n{\n\t%s--;\n}\n\n", buf, buf);
		}

		predicate_add(&pred, buf, NULL, 0);

		if (info->ev_type == 'H') {
			(void) sprintf(buf, "timestamp -\n\t    "
			    "self->ev%d[(uintptr_t)arg0]", event);
		}

		arg1 = buf;
	} else {
		predicate_add(&pred, info->ev_predicate, NULL, 0);
		if (info->ev_type != 'I')
			predicate_add(&pred, g_predicate, NULL, 0);
		else
			predicate_add(&pred, g_ipredicate, NULL, 0);
	}

	if ((dur = g_min_duration[event]) != 0)
		predicate_add(&pred, arg1, ">=", dur);

	dprog_add("%s\n", info->ev_name);

	if (pred != NULL)
		dprog_add("/%s/\n", pred);
	predicate_destroy(&pred);

	dprog_add("{\n");

	if (g_tracing) {
		dprog_add("\ttrace(%dULL);\n", event);
		dprog_add("\ttrace(%s);\n", arg0);
		dprog_add("\ttrace(%s);\n", caller);
		dprog_add(stack);
	} else {
		/*
		 * The ordering here is important:  when we process the
		 * aggregate, we count on the fact that @avg appears before
		 * @hist in program order to assure that @avg is assigned the
		 * first aggregation variable ID and @hist assigned the
		 * second; see the comment in process_aggregate() for details.
		 */
		dprog_add("\t@avg[%dULL, %s, %s%s] = avg(%s);\n",
		    event, arg0, caller, stack, arg1);

		if (g_recsize >= LS_HIST) {
			dprog_add("\t@hist[%dULL, %s, %s%s] = quantize"
			    "(%s);\n", event, arg0, caller, stack, arg1);
		}
	}

	if (info->ev_acquire != NULL)
		dprog_add("\tself->ev%d[arg0] = 0;\n", event);

	dprog_add("}\n\n");
}

static void
dprog_compile()
{
	dtrace_prog_t *prog;
	dtrace_proginfo_t info;

	if (g_Vflag) {
		(void) fprintf(stderr, "lockstat: vvvv D program vvvv\n");
		(void) fputs(g_prog, stderr);
		(void) fprintf(stderr, "lockstat: ^^^^ D program ^^^^\n");
	}

	if ((prog = dtrace_program_strcompile(g_dtp, g_prog,
	    DTRACE_PROBESPEC_NAME, 0, 0, NULL)) == NULL)
		dfail("failed to compile program");

	if (dtrace_program_exec(g_dtp, prog, &info) == -1)
		dfail("failed to enable probes");

	if (dtrace_go(g_dtp) != 0)
		dfail("couldn't start tracing");
}

static void
#ifdef illumos
status_fire(void)
#else
status_fire(int i)
#endif
{}

static void
status_init(void)
{
	dtrace_optval_t val, status, agg;
	struct sigaction act;
	struct itimerspec ts;
	struct sigevent ev;
	timer_t tid;

	if (dtrace_getopt(g_dtp, "statusrate", &status) == -1)
		dfail("failed to get 'statusrate'");

	if (dtrace_getopt(g_dtp, "aggrate", &agg) == -1)
		dfail("failed to get 'statusrate'");

	/*
	 * We would want to awaken at a rate that is the GCD of the statusrate
	 * and the aggrate -- but that seems a bit absurd.  Instead, we'll
	 * simply awaken at a rate that is the more frequent of the two, which
	 * assures that we're never later than the interval implied by the
	 * more frequent rate.
	 */
	val = status < agg ? status : agg;

	(void) sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = status_fire;
	(void) sigaction(SIGUSR1, &act, NULL);

	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = SIGUSR1;

	if (timer_create(CLOCK_REALTIME, &ev, &tid) == -1)
		dfail("cannot create CLOCK_REALTIME timer");

	ts.it_value.tv_sec = val / NANOSEC;
	ts.it_value.tv_nsec = val % NANOSEC;
	ts.it_interval = ts.it_value;

	if (timer_settime(tid, TIMER_RELTIME, &ts, NULL) == -1)
		dfail("cannot set time on CLOCK_REALTIME timer");
}

static void
status_check(void)
{
	if (!g_tracing && dtrace_aggregate_snap(g_dtp) != 0)
		dfail("failed to snap aggregate");

	if (dtrace_status(g_dtp) == -1)
		dfail("dtrace_status()");
}

static void
lsrec_fill(lsrec_t *lsrec, const dtrace_recdesc_t *rec, int nrecs, caddr_t data)
{
	bzero(lsrec, g_recsize);
	lsrec->ls_count = 1;

	if ((g_recsize > LS_HIST && nrecs < 4) || (nrecs < 3))
		fail(0, "truncated DTrace record");

	if (rec->dtrd_size != sizeof (uint64_t))
		fail(0, "bad event size in first record");

	/* LINTED - alignment */
	lsrec->ls_event = (uint32_t)*((uint64_t *)(data + rec->dtrd_offset));
	rec++;

#ifdef illumos
	if (rec->dtrd_size != sizeof (uintptr_t))
		fail(0, "bad lock address size in second record");

	/* LINTED - alignment */
	lsrec->ls_lock = *((uintptr_t *)(data + rec->dtrd_offset));
	rec++;
#else
	lsrec->ls_lock = strdup((const char *)(data + rec->dtrd_offset));
	rec++;
#endif

	if (rec->dtrd_size != sizeof (uintptr_t))
		fail(0, "bad caller size in third record");

	/* LINTED - alignment */
	lsrec->ls_caller = *((uintptr_t *)(data + rec->dtrd_offset));
	rec++;

	if (g_recsize > LS_HIST) {
		int frames, i;
		pc_t *stack;

		frames = rec->dtrd_size / sizeof (pc_t);
		/* LINTED - alignment */
		stack = (pc_t *)(data + rec->dtrd_offset);

		for (i = 1; i < frames; i++)
			lsrec->ls_stack[i - 1] = stack[i];
	}
}

/*ARGSUSED*/
static int
count_aggregate(const dtrace_aggdata_t *agg, void *arg)
{
	*((size_t *)arg) += 1;

	return (DTRACE_AGGWALK_NEXT);
}

static int
process_aggregate(const dtrace_aggdata_t *agg, void *arg)
{
	const dtrace_aggdesc_t *aggdesc = agg->dtada_desc;
	caddr_t data = agg->dtada_data;
	lsdata_t *lsdata = arg;
	lsrec_t *lsrec = lsdata->lsd_next;
	const dtrace_recdesc_t *rec;
	uint64_t *avg, *quantized;
	int i, j;

	assert(lsdata->lsd_count < g_nrecs);

	/*
	 * Aggregation variable IDs are guaranteed to be generated in program
	 * order, and they are guaranteed to start from DTRACE_AGGVARIDNONE
	 * plus one.  As "avg" appears before "hist" in program order, we know
	 * that "avg" will be allocated the first aggregation variable ID, and
	 * "hist" will be allocated the second aggregation variable ID -- and
	 * we therefore use the aggregation variable ID to differentiate the
	 * cases.
	 */
	if (aggdesc->dtagd_varid > DTRACE_AGGVARIDNONE + 1) {
		/*
		 * If this is the histogram entry.  We'll copy the quantized
		 * data into lc_hist, and jump over the rest.
		 */
		rec = &aggdesc->dtagd_rec[aggdesc->dtagd_nrecs - 1];

		if (aggdesc->dtagd_varid != DTRACE_AGGVARIDNONE + 2)
			fail(0, "bad variable ID in aggregation record");

		if (rec->dtrd_size !=
		    DTRACE_QUANTIZE_NBUCKETS * sizeof (uint64_t))
			fail(0, "bad quantize size in aggregation record");

		/* LINTED - alignment */
		quantized = (uint64_t *)(data + rec->dtrd_offset);

		for (i = DTRACE_QUANTIZE_ZEROBUCKET, j = 0;
		    i < DTRACE_QUANTIZE_NBUCKETS; i++, j++)
			lsrec->ls_hist[j] = quantized[i];

		goto out;
	}

	lsrec_fill(lsrec, &aggdesc->dtagd_rec[1],
	    aggdesc->dtagd_nrecs - 1, data);

	rec = &aggdesc->dtagd_rec[aggdesc->dtagd_nrecs - 1];

	if (rec->dtrd_size != 2 * sizeof (uint64_t))
		fail(0, "bad avg size in aggregation record");

	/* LINTED - alignment */
	avg = (uint64_t *)(data + rec->dtrd_offset);
	lsrec->ls_count = (uint32_t)avg[0];
	lsrec->ls_time = (uintptr_t)avg[1];

	if (g_recsize >= LS_HIST)
		return (DTRACE_AGGWALK_NEXT);

out:
	lsdata->lsd_next = (lsrec_t *)((uintptr_t)lsrec + g_recsize);
	lsdata->lsd_count++;

	return (DTRACE_AGGWALK_NEXT);
}

static int
process_trace(const dtrace_probedata_t *pdata, void *arg)
{
	lsdata_t *lsdata = arg;
	lsrec_t *lsrec = lsdata->lsd_next;
	dtrace_eprobedesc_t *edesc = pdata->dtpda_edesc;
	caddr_t data = pdata->dtpda_data;

	if (lsdata->lsd_count >= g_nrecs)
		return (DTRACE_CONSUME_NEXT);

	lsrec_fill(lsrec, edesc->dtepd_rec, edesc->dtepd_nrecs, data);

	lsdata->lsd_next = (lsrec_t *)((uintptr_t)lsrec + g_recsize);
	lsdata->lsd_count++;

	return (DTRACE_CONSUME_NEXT);
}

static int
process_data(FILE *out, char *data)
{
	lsdata_t lsdata;

	/* LINTED - alignment */
	lsdata.lsd_next = (lsrec_t *)data;
	lsdata.lsd_count = 0;

	if (g_tracing) {
		if (dtrace_consume(g_dtp, out,
		    process_trace, NULL, &lsdata) != 0)
			dfail("failed to consume buffer");

		return (lsdata.lsd_count);
	}

	if (dtrace_aggregate_walk_keyvarsorted(g_dtp,
	    process_aggregate, &lsdata) != 0)
		dfail("failed to walk aggregate");

	return (lsdata.lsd_count);
}

/*ARGSUSED*/
static int
drophandler(const dtrace_dropdata_t *data, void *arg)
{
	g_dropped++;
	(void) fprintf(stderr, "lockstat: warning: %s", data->dtdda_msg);
	return (DTRACE_HANDLE_OK);
}

int
main(int argc, char **argv)
{
	char *data_buf;
	lsrec_t *lsp, **current, **first, **sort_buf, **merge_buf;
	FILE *out = stdout;
	int c;
	pid_t child;
	int status;
	int i, j;
	hrtime_t duration;
	char *addrp, *offp, *sizep, *evp, *lastp, *p;
	uintptr_t addr;
	size_t size, off;
	int events_specified = 0;
	int exec_errno = 0;
	uint32_t event;
	char *filt = NULL, *ifilt = NULL;
	static uint64_t ev_count[LS_MAX_EVENTS + 1];
	static uint64_t ev_time[LS_MAX_EVENTS + 1];
	dtrace_optval_t aggsize;
	char aggstr[10];
	long ncpus;
	int dynvar = 0;
	int err;

	if ((g_dtp = dtrace_open(DTRACE_VERSION, 0, &err)) == NULL) {
		fail(0, "cannot open dtrace library: %s",
		    dtrace_errmsg(NULL, err));
	}

	if (dtrace_handle_drop(g_dtp, &drophandler, NULL) == -1)
		dfail("couldn't establish drop handler");

	if (symtab_init() == -1)
		fail(1, "can't load kernel symbols");

	g_nrecs = DEFAULT_NRECS;

	while ((c = getopt(argc, argv, LOCKSTAT_OPTSTR)) != GETOPT_EOF) {
		switch (c) {
		case 'b':
			g_recsize = LS_BASIC;
			break;

		case 't':
			g_recsize = LS_TIME;
			break;

		case 'h':
			g_recsize = LS_HIST;
			break;

		case 's':
			if (!isdigit(optarg[0]))
				usage();
			g_stkdepth = atoi(optarg);
			if (g_stkdepth > LS_MAX_STACK_DEPTH)
				fail(0, "max stack depth is %d",
				    LS_MAX_STACK_DEPTH);
			g_recsize = LS_STACK(g_stkdepth);
			break;

		case 'n':
			if (!isdigit(optarg[0]))
				usage();
			g_nrecs = atoi(optarg);
			break;

		case 'd':
			if (!isdigit(optarg[0]))
				usage();
			duration = atoll(optarg);

			/*
			 * XXX -- durations really should be per event
			 * since the units are different, but it's hard
			 * to express this nicely in the interface.
			 * Not clear yet what the cleanest solution is.
			 */
			for (i = 0; i < LS_MAX_EVENTS; i++)
				if (g_event_info[i].ev_type != 'E')
					g_min_duration[i] = duration;

			break;

		case 'i':
			if (!isdigit(optarg[0]))
				usage();
			i = atoi(optarg);
			if (i <= 0)
				usage();
			if (i > MAX_HZ)
				fail(0, "max interrupt rate is %d Hz", MAX_HZ);

			for (j = 0; j < LS_MAX_EVENTS; j++)
				if (strcmp(g_event_info[j].ev_desc,
				    "Profiling interrupt") == 0)
					break;

			(void) sprintf(g_event_info[j].ev_name,
			    "profile:::profile-%d", i);
			break;

		case 'l':
		case 'f':
			addrp = strtok(optarg, ",");
			sizep = strtok(NULL, ",");
			addrp = strtok(optarg, ",+");
			offp = strtok(NULL, ",");

			size = sizep ? strtoul(sizep, NULL, 0) : 1;
			off = offp ? strtoul(offp, NULL, 0) : 0;

			if (addrp[0] == '0') {
				addr = strtoul(addrp, NULL, 16) + off;
			} else {
				addr = sym_to_addr(addrp) + off;
				if (sizep == NULL)
					size = sym_size(addrp) - off;
				if (addr - off == 0)
					fail(0, "symbol '%s' not found", addrp);
				if (size == 0)
					size = 1;
			}


			if (c == 'l') {
				filter_add(&filt, "arg0", addr, size);
			} else {
				filter_add(&filt, "caller", addr, size);
				filter_add(&ifilt, "arg0", addr, size);
			}
			break;

		case 'e':
			evp = strtok_r(optarg, ",", &lastp);
			while (evp) {
				int ev1, ev2;
				char *evp2;

				(void) strtok(evp, "-");
				evp2 = strtok(NULL, "-");
				ev1 = atoi(evp);
				ev2 = evp2 ? atoi(evp2) : ev1;
				if ((uint_t)ev1 >= LS_MAX_EVENTS ||
				    (uint_t)ev2 >= LS_MAX_EVENTS || ev1 > ev2)
					fail(0, "-e events out of range");
				for (i = ev1; i <= ev2; i++)
					g_enabled[i] = 1;
				evp = strtok_r(NULL, ",", &lastp);
			}
			events_specified = 1;
			break;

#ifdef illumos
		case 'c':
			g_cflag = 1;
			break;
#endif

		case 'k':
			g_kflag = 1;
			break;

		case 'w':
			g_wflag = 1;
			break;

		case 'W':
			g_Wflag = 1;
			break;

		case 'g':
			g_gflag = 1;
			break;

		case 'C':
		case 'E':
		case 'H':
		case 'I':
			for (i = 0; i < LS_MAX_EVENTS; i++)
				if (g_event_info[i].ev_type == c)
					g_enabled[i] = 1;
			events_specified = 1;
			break;

		case 'A':
			for (i = 0; i < LS_MAX_EVENTS; i++)
				if (strchr("CH", g_event_info[i].ev_type))
					g_enabled[i] = 1;
			events_specified = 1;
			break;

		case 'T':
			g_tracing = 1;
			break;

		case 'D':
			if (!isdigit(optarg[0]))
				usage();
			g_topn = atoi(optarg);
			break;

		case 'R':
			g_rates = 1;
			break;

		case 'p':
			g_pflag = 1;
			break;

		case 'P':
			g_Pflag = 1;
			break;

		case 'o':
			if ((out = fopen(optarg, "w")) == NULL)
				fail(1, "error opening file");
			break;

		case 'V':
			g_Vflag = 1;
			break;

		default:
			if (strchr(LOCKSTAT_OPTSTR, c) == NULL)
				usage();
		}
	}

	if (filt != NULL) {
		predicate_add(&g_predicate, filt, NULL, 0);
		filter_destroy(&filt);
	}

	if (ifilt != NULL) {
		predicate_add(&g_ipredicate, ifilt, NULL, 0);
		filter_destroy(&ifilt);
	}

	if (g_recsize == 0) {
		if (g_gflag) {
			g_stkdepth = LS_MAX_STACK_DEPTH;
			g_recsize = LS_STACK(g_stkdepth);
		} else {
			g_recsize = LS_TIME;
		}
	}

	if (g_gflag && g_recsize <= LS_STACK(0))
		fail(0, "'-g' requires at least '-s 1' data gathering");

	/*
	 * Make sure the alignment is reasonable
	 */
	g_recsize = -(-g_recsize & -sizeof (uint64_t));

	for (i = 0; i < LS_MAX_EVENTS; i++) {
		/*
		 * If no events were specified, enable -C.
		 */
		if (!events_specified && g_event_info[i].ev_type == 'C')
			g_enabled[i] = 1;
	}

	for (i = 0; i < LS_MAX_EVENTS; i++) {
		if (!g_enabled[i])
			continue;

		if (g_event_info[i].ev_acquire != NULL) {
			/*
			 * If we've enabled a hold event, we must explicitly
			 * allocate dynamic variable space.
			 */
			dynvar = 1;
		}

		dprog_addevent(i);
	}

	/*
	 * Make sure there are remaining arguments to specify a child command
	 * to execute.
	 */
	if (argc <= optind)
		usage();

	if ((ncpus = sysconf(_SC_NPROCESSORS_ONLN)) == -1)
		dfail("couldn't determine number of online CPUs");

	/*
	 * By default, we set our data buffer size to be the number of records
	 * multiplied by the size of the record, doubled to account for some
	 * DTrace slop and divided by the number of CPUs.  We silently clamp
	 * the aggregation size at both a minimum and a maximum to prevent
	 * absurdly low or high values.
	 */
	if ((aggsize = (g_nrecs * g_recsize * 2) / ncpus) < MIN_AGGSIZE)
		aggsize = MIN_AGGSIZE;

	if (aggsize > MAX_AGGSIZE)
		aggsize = MAX_AGGSIZE;

	(void) sprintf(aggstr, "%lld", (long long)aggsize);

	if (!g_tracing) {
		if (dtrace_setopt(g_dtp, "bufsize", "4k") == -1)
			dfail("failed to set 'bufsize'");

		if (dtrace_setopt(g_dtp, "aggsize", aggstr) == -1)
			dfail("failed to set 'aggsize'");

		if (dynvar) {
			/*
			 * If we're using dynamic variables, we set our
			 * dynamic variable size to be one megabyte per CPU,
			 * with a hard-limit of 32 megabytes.  This may still
			 * be too small in some cases, but it can be tuned
			 * manually via -x if need be.
			 */
			(void) sprintf(aggstr, "%ldm", ncpus < 32 ? ncpus : 32);

			if (dtrace_setopt(g_dtp, "dynvarsize", aggstr) == -1)
				dfail("failed to set 'dynvarsize'");
		}
	} else {
		if (dtrace_setopt(g_dtp, "bufsize", aggstr) == -1)
			dfail("failed to set 'bufsize'");
	}

	if (dtrace_setopt(g_dtp, "statusrate", "10sec") == -1)
		dfail("failed to set 'statusrate'");

	optind = 1;
	while ((c = getopt(argc, argv, LOCKSTAT_OPTSTR)) != GETOPT_EOF) {
		switch (c) {
		case 'x':
			if ((p = strchr(optarg, '=')) != NULL)
				*p++ = '\0';

			if (dtrace_setopt(g_dtp, optarg, p) != 0)
				dfail("failed to set -x %s", optarg);
			break;
		}
	}

	argc -= optind;
	argv += optind;

	dprog_compile();
	status_init();

	g_elapsed = -gethrtime();

	/*
	 * Spawn the specified command and wait for it to complete.
	 */
	child = fork();
	if (child == -1)
		fail(1, "cannot fork");
	if (child == 0) {
		(void) dtrace_close(g_dtp);
		(void) execvp(argv[0], &argv[0]);
		exec_errno = errno;
		exit(127);
	}

#ifdef illumos
	while (waitpid(child, &status, WEXITED) != child)
#else
	while (waitpid(child, &status, 0) != child)
#endif
		status_check();

	g_elapsed += gethrtime();

	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != 0) {
			if (exec_errno != 0) {
				errno = exec_errno;
				fail(1, "could not execute %s", argv[0]);
			}
			(void) fprintf(stderr,
			    "lockstat: warning: %s exited with code %d\n",
			    argv[0], WEXITSTATUS(status));
		}
	} else {
		(void) fprintf(stderr,
		    "lockstat: warning: %s died on signal %d\n",
		    argv[0], WTERMSIG(status));
	}

	if (dtrace_stop(g_dtp) == -1)
		dfail("failed to stop dtrace");

	/*
	 * Before we read out the results, we need to allocate our buffer.
	 * If we're tracing, then we'll just use the precalculated size.  If
	 * we're not, then we'll take a snapshot of the aggregate, and walk
	 * it to count the number of records.
	 */
	if (!g_tracing) {
		if (dtrace_aggregate_snap(g_dtp) != 0)
			dfail("failed to snap aggregate");

		g_nrecs = 0;

		if (dtrace_aggregate_walk(g_dtp,
		    count_aggregate, &g_nrecs) != 0)
			dfail("failed to walk aggregate");
	}

#ifdef illumos
	if ((data_buf = memalign(sizeof (uint64_t),
	    (g_nrecs + 1) * g_recsize)) == NULL)
#else
	if (posix_memalign((void **)&data_buf, sizeof (uint64_t),  
	    (g_nrecs + 1) * g_recsize) )
#endif
		fail(1, "Memory allocation failed");

	/*
	 * Read out the DTrace data.
	 */
	g_nrecs_used = process_data(out, data_buf);

	if (g_nrecs_used > g_nrecs || g_dropped)
		(void) fprintf(stderr, "lockstat: warning: "
		    "ran out of data records (use -n for more)\n");

	/* LINTED - alignment */
	for (i = 0, lsp = (lsrec_t *)data_buf; i < g_nrecs_used; i++,
	    /* LINTED - alignment */
	    lsp = (lsrec_t *)((char *)lsp + g_recsize)) {
		ev_count[lsp->ls_event] += lsp->ls_count;
		ev_time[lsp->ls_event] += lsp->ls_time;
	}

	/*
	 * If -g was specified, convert stacks into individual records.
	 */
	if (g_gflag) {
		lsrec_t *newlsp, *oldlsp;

#ifdef illumos
		newlsp = memalign(sizeof (uint64_t),
		    g_nrecs_used * LS_TIME * (g_stkdepth + 1));
#else
		posix_memalign((void **)&newlsp, sizeof (uint64_t), 
		    g_nrecs_used * LS_TIME * (g_stkdepth + 1));
#endif
		if (newlsp == NULL)
			fail(1, "Cannot allocate space for -g processing");
		lsp = newlsp;
		/* LINTED - alignment */
		for (i = 0, oldlsp = (lsrec_t *)data_buf; i < g_nrecs_used; i++,
		    /* LINTED - alignment */
		    oldlsp = (lsrec_t *)((char *)oldlsp + g_recsize)) {
			int fr;
			int caller_in_stack = 0;

			if (oldlsp->ls_count == 0)
				continue;

			for (fr = 0; fr < g_stkdepth; fr++) {
				if (oldlsp->ls_stack[fr] == 0)
					break;
				if (oldlsp->ls_stack[fr] == oldlsp->ls_caller)
					caller_in_stack = 1;
				bcopy(oldlsp, lsp, LS_TIME);
				lsp->ls_caller = oldlsp->ls_stack[fr];
#ifndef illumos
				lsp->ls_lock = strdup(oldlsp->ls_lock);
#endif
				/* LINTED - alignment */
				lsp = (lsrec_t *)((char *)lsp + LS_TIME);
			}
			if (!caller_in_stack) {
				bcopy(oldlsp, lsp, LS_TIME);
				/* LINTED - alignment */
				lsp = (lsrec_t *)((char *)lsp + LS_TIME);
			}
#ifndef illumos
			free(oldlsp->ls_lock);
#endif
		}
		g_nrecs = g_nrecs_used =
		    ((uintptr_t)lsp - (uintptr_t)newlsp) / LS_TIME;
		g_recsize = LS_TIME;
		g_stkdepth = 0;
		free(data_buf);
		data_buf = (char *)newlsp;
	}

	if ((sort_buf = calloc(2 * (g_nrecs + 1),
	    sizeof (void *))) == NULL)
		fail(1, "Sort buffer allocation failed");
	merge_buf = sort_buf + (g_nrecs + 1);

	/*
	 * Build the sort buffer, discarding zero-count records along the way.
	 */
	/* LINTED - alignment */
	for (i = 0, lsp = (lsrec_t *)data_buf; i < g_nrecs_used; i++,
	    /* LINTED - alignment */
	    lsp = (lsrec_t *)((char *)lsp + g_recsize)) {
		if (lsp->ls_count == 0)
			lsp->ls_event = LS_MAX_EVENTS;
		sort_buf[i] = lsp;
	}

	if (g_nrecs_used == 0)
		exit(0);

	/*
	 * Add a sentinel after the last record
	 */
	sort_buf[i] = lsp;
	lsp->ls_event = LS_MAX_EVENTS;

	if (g_tracing) {
		report_trace(out, sort_buf);
		return (0);
	}

	/*
	 * Application of -g may have resulted in multiple records
	 * with the same signature; coalesce them.
	 */
	if (g_gflag) {
		mergesort(lockcmp, sort_buf, merge_buf, g_nrecs_used);
		coalesce(lockcmp, sort_buf, g_nrecs_used);
	}

	/*
	 * Coalesce locks within the same symbol if -c option specified.
	 * Coalesce PCs within the same function if -k option specified.
	 */
	if (g_cflag || g_kflag) {
		for (i = 0; i < g_nrecs_used; i++) {
			int fr;
			lsp = sort_buf[i];
#ifdef illumos
			if (g_cflag)
				coalesce_symbol(&lsp->ls_lock);
#endif
			if (g_kflag) {
				for (fr = 0; fr < g_stkdepth; fr++)
					coalesce_symbol(&lsp->ls_stack[fr]);
				coalesce_symbol(&lsp->ls_caller);
			}
		}
		mergesort(lockcmp, sort_buf, merge_buf, g_nrecs_used);
		coalesce(lockcmp, sort_buf, g_nrecs_used);
	}

	/*
	 * Coalesce callers if -w option specified
	 */
	if (g_wflag) {
		mergesort(lock_and_count_cmp_anywhere,
		    sort_buf, merge_buf, g_nrecs_used);
		coalesce(lockcmp_anywhere, sort_buf, g_nrecs_used);
	}

	/*
	 * Coalesce locks if -W option specified
	 */
	if (g_Wflag) {
		mergesort(site_and_count_cmp_anylock,
		    sort_buf, merge_buf, g_nrecs_used);
		coalesce(sitecmp_anylock, sort_buf, g_nrecs_used);
	}

	/*
	 * Sort data by contention count (ls_count) or total time (ls_time),
	 * depending on g_Pflag.  Override g_Pflag if time wasn't measured.
	 */
	if (g_recsize < LS_TIME)
		g_Pflag = 0;

	if (g_Pflag)
		mergesort(timecmp, sort_buf, merge_buf, g_nrecs_used);
	else
		mergesort(countcmp, sort_buf, merge_buf, g_nrecs_used);

	/*
	 * Display data by event type
	 */
	first = &sort_buf[0];
	while ((event = (*first)->ls_event) < LS_MAX_EVENTS) {
		current = first;
		while ((lsp = *current)->ls_event == event)
			current++;
		report_stats(out, first, current - first, ev_count[event],
		    ev_time[event]);
		first = current;
	}

#ifndef illumos
	/*
	 * Free lock name buffers
	 */
	for (i = 0, lsp = (lsrec_t *)data_buf; i < g_nrecs_used; i++,
	    lsp = (lsrec_t *)((char *)lsp + g_recsize))
		free(lsp->ls_lock);
#endif

	return (0);
}

static char *
format_symbol(char *buf, uintptr_t addr, int show_size)
{
	uintptr_t symoff;
	char *symname;
	size_t symsize;

	symname = addr_to_sym(addr, &symoff, &symsize);

	if (show_size && symoff == 0)
		(void) sprintf(buf, "%s[%ld]", symname, (long)symsize);
	else if (symoff == 0)
		(void) sprintf(buf, "%s", symname);
	else if (symoff < 16 && bcmp(symname, "cpu[", 4) == 0)	/* CPU+PIL */
#ifdef illumos
		(void) sprintf(buf, "%s+%ld", symname, (long)symoff);
#else
		(void) sprintf(buf, "%s+%s", symname, g_pri_class[(int)symoff]);
#endif
	else if (symoff <= symsize || (symoff < 256 && addr != symoff))
		(void) sprintf(buf, "%s+0x%llx", symname,
		    (unsigned long long)symoff);
	else
		(void) sprintf(buf, "0x%llx", (unsigned long long)addr);
	return (buf);
}

static void
report_stats(FILE *out, lsrec_t **sort_buf, size_t nrecs, uint64_t total_count,
	uint64_t total_time)
{
	uint32_t event = sort_buf[0]->ls_event;
	lsrec_t *lsp;
	double ptotal = 0.0;
	double percent;
	int i, j, fr;
	int displayed;
	int first_bin, last_bin, max_bin_count, total_bin_count;
	int rectype;
	char buf[256];
	char lhdr[80], chdr[80];

	rectype = g_recsize;

	if (g_topn == 0) {
		(void) fprintf(out, "%20llu %s\n",
		    g_rates == 0 ? total_count :
		    ((unsigned long long)total_count * NANOSEC) / g_elapsed,
		    g_event_info[event].ev_desc);
		return;
	}

	(void) sprintf(lhdr, "%s%s",
	    g_Wflag ? "Hottest " : "", g_event_info[event].ev_lhdr);
	(void) sprintf(chdr, "%s%s",
	    g_wflag ? "Hottest " : "", "Caller");

	if (!g_pflag)
		(void) fprintf(out,
		    "\n%s: %.0f events in %.3f seconds (%.0f events/sec)\n\n",
		    g_event_info[event].ev_desc, (double)total_count,
		    (double)g_elapsed / NANOSEC,
		    (double)total_count * NANOSEC / g_elapsed);

	if (!g_pflag && rectype < LS_HIST) {
		(void) sprintf(buf, "%s", g_event_info[event].ev_units);
		(void) fprintf(out, "%5s %4s %4s %4s %8s %-22s %-24s\n",
		    g_rates ? "ops/s" : "Count",
		    g_gflag ? "genr" : "indv",
		    "cuml", "rcnt", rectype >= LS_TIME ? buf : "", lhdr, chdr);
		(void) fprintf(out, "---------------------------------"
		    "----------------------------------------------\n");
	}

	displayed = 0;
	for (i = 0; i < nrecs; i++) {
		lsp = sort_buf[i];

		if (displayed++ >= g_topn)
			break;

		if (g_pflag) {
			int j;

			(void) fprintf(out, "%u %u",
			    lsp->ls_event, lsp->ls_count);
#ifdef illumos
			(void) fprintf(out, " %s",
			    format_symbol(buf, lsp->ls_lock, g_cflag));
#else
			(void) fprintf(out, " %s", lsp->ls_lock);
#endif
			(void) fprintf(out, " %s",
			    format_symbol(buf, lsp->ls_caller, 0));
			(void) fprintf(out, " %f",
			    (double)lsp->ls_refcnt / lsp->ls_count);
			if (rectype >= LS_TIME)
				(void) fprintf(out, " %llu",
				    (unsigned long long)lsp->ls_time);
			if (rectype >= LS_HIST) {
				for (j = 0; j < 64; j++)
					(void) fprintf(out, " %u",
					    lsp->ls_hist[j]);
			}
			for (j = 0; j < LS_MAX_STACK_DEPTH; j++) {
				if (rectype <= LS_STACK(j) ||
				    lsp->ls_stack[j] == 0)
					break;
				(void) fprintf(out, " %s",
				    format_symbol(buf, lsp->ls_stack[j], 0));
			}
			(void) fprintf(out, "\n");
			continue;
		}

		if (rectype >= LS_HIST) {
			(void) fprintf(out, "---------------------------------"
			    "----------------------------------------------\n");
			(void) sprintf(buf, "%s",
			    g_event_info[event].ev_units);
			(void) fprintf(out, "%5s %4s %4s %4s %8s %-22s %-24s\n",
			    g_rates ? "ops/s" : "Count",
			    g_gflag ? "genr" : "indv",
			    "cuml", "rcnt", buf, lhdr, chdr);
		}

		if (g_Pflag && total_time != 0)
			percent = (lsp->ls_time * 100.00) / total_time;
		else
			percent = (lsp->ls_count * 100.00) / total_count;

		ptotal += percent;

		if (rectype >= LS_TIME)
			(void) sprintf(buf, "%llu",
			    (unsigned long long)(lsp->ls_time / lsp->ls_count));
		else
			buf[0] = '\0';

		(void) fprintf(out, "%5llu ",
		    g_rates == 0 ? lsp->ls_count :
		    ((uint64_t)lsp->ls_count * NANOSEC) / g_elapsed);

		(void) fprintf(out, "%3.0f%% ", percent);

		if (g_gflag)
			(void) fprintf(out, "---- ");
		else
			(void) fprintf(out, "%3.0f%% ", ptotal);

		(void) fprintf(out, "%4.2f %8s ",
		    (double)lsp->ls_refcnt / lsp->ls_count, buf);

#ifdef illumos
		(void) fprintf(out, "%-22s ",
		    format_symbol(buf, lsp->ls_lock, g_cflag));
#else
		(void) fprintf(out, "%-22s ", lsp->ls_lock);
#endif

		(void) fprintf(out, "%-24s\n",
		    format_symbol(buf, lsp->ls_caller, 0));

		if (rectype < LS_HIST)
			continue;

		(void) fprintf(out, "\n");
		(void) fprintf(out, "%10s %31s %-9s %-24s\n",
		    g_event_info[event].ev_units,
		    "------ Time Distribution ------",
		    g_rates ? "ops/s" : "count",
		    rectype > LS_STACK(0) ? "Stack" : "");

		first_bin = 0;
		while (lsp->ls_hist[first_bin] == 0)
			first_bin++;

		last_bin = 63;
		while (lsp->ls_hist[last_bin] == 0)
			last_bin--;

		max_bin_count = 0;
		total_bin_count = 0;
		for (j = first_bin; j <= last_bin; j++) {
			total_bin_count += lsp->ls_hist[j];
			if (lsp->ls_hist[j] > max_bin_count)
				max_bin_count = lsp->ls_hist[j];
		}

		/*
		 * If we went a few frames below the caller, ignore them
		 */
		for (fr = 3; fr > 0; fr--)
			if (lsp->ls_stack[fr] == lsp->ls_caller)
				break;

		for (j = first_bin; j <= last_bin; j++) {
			uint_t depth = (lsp->ls_hist[j] * 30) / total_bin_count;
			(void) fprintf(out, "%10llu |%s%s %-9u ",
			    1ULL << j,
			    "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" + 30 - depth,
			    "                              " + depth,
			    g_rates == 0 ? lsp->ls_hist[j] :
			    (uint_t)(((uint64_t)lsp->ls_hist[j] * NANOSEC) /
			    g_elapsed));
			if (rectype <= LS_STACK(fr) || lsp->ls_stack[fr] == 0) {
				(void) fprintf(out, "\n");
				continue;
			}
			(void) fprintf(out, "%-24s\n",
			    format_symbol(buf, lsp->ls_stack[fr], 0));
			fr++;
		}
		while (rectype > LS_STACK(fr) && lsp->ls_stack[fr] != 0) {
			(void) fprintf(out, "%15s %-36s %-24s\n", "", "",
			    format_symbol(buf, lsp->ls_stack[fr], 0));
			fr++;
		}
	}

	if (!g_pflag)
		(void) fprintf(out, "---------------------------------"
		    "----------------------------------------------\n");

	(void) fflush(out);
}

static void
report_trace(FILE *out, lsrec_t **sort_buf)
{
	lsrec_t *lsp;
	int i, fr;
	int rectype;
	char buf[256], buf2[256];

	rectype = g_recsize;

	if (!g_pflag) {
		(void) fprintf(out, "%5s  %7s  %11s  %-24s  %-24s\n",
		    "Event", "Time", "Owner", "Lock", "Caller");
		(void) fprintf(out, "---------------------------------"
		    "----------------------------------------------\n");
	}

	for (i = 0; i < g_nrecs_used; i++) {

		lsp = sort_buf[i];

		if (lsp->ls_event >= LS_MAX_EVENTS || lsp->ls_count == 0)
			continue;

		(void) fprintf(out, "%2d  %10llu  %11p  %-24s  %-24s\n",
		    lsp->ls_event, (unsigned long long)lsp->ls_time,
		    (void *)lsp->ls_next,
#ifdef illumos
		    format_symbol(buf, lsp->ls_lock, 0),
#else
		    lsp->ls_lock,
#endif
		    format_symbol(buf2, lsp->ls_caller, 0));

		if (rectype <= LS_STACK(0))
			continue;

		/*
		 * If we went a few frames below the caller, ignore them
		 */
		for (fr = 3; fr > 0; fr--)
			if (lsp->ls_stack[fr] == lsp->ls_caller)
				break;

		while (rectype > LS_STACK(fr) && lsp->ls_stack[fr] != 0) {
			(void) fprintf(out, "%53s  %-24s\n", "",
			    format_symbol(buf, lsp->ls_stack[fr], 0));
			fr++;
		}
		(void) fprintf(out, "\n");
	}

	(void) fflush(out);
}
