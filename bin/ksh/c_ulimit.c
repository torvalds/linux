/*	$OpenBSD: c_ulimit.c,v 1.29 2019/06/28 13:34:59 deraadt Exp $	*/

/*
	ulimit -- handle "ulimit" builtin

	Reworked to use getrusage() and ulimit() at once (as needed on
	some schizophrenic systems, eg, HP-UX 9.01), made argument parsing
	conform to at&t ksh, added autoconf support.  Michael Rendell, May, '94

	Eric Gisin, September 1988
	Adapted to PD KornShell. Removed AT&T code.

	last edit:	06-Jun-1987	D A Gwyn

	This started out as the BRL UNIX System V system call emulation
	for 4.nBSD, and was later extended by Doug Kingston to handle
	the extended 4.nBSD resource limits.  It now includes the code
	that was originally under case SYSULIMIT in source file "xec.c".
*/

#include <sys/resource.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>

#include "sh.h"

#define SOFT	0x1
#define HARD	0x2

struct limits {
	const char *name;
	int	resource;	/* resource to get/set */
	int	factor;		/* multiply by to get rlim_{cur,max} values */
	char	option;		/* option character (-d, -f, ...) */
};

static void print_ulimit(const struct limits *, int);
static int set_ulimit(const struct limits *, const char *, int);

int
c_ulimit(char **wp)
{
	static const struct limits limits[] = {
		/* Do not use options -H, -S or -a or change the order. */
		{ "time(cpu-seconds)", RLIMIT_CPU, 1, 't' },
		{ "file(blocks)", RLIMIT_FSIZE, 512, 'f' },
		{ "coredump(blocks)", RLIMIT_CORE, 512, 'c' },
		{ "data(kbytes)", RLIMIT_DATA, 1024, 'd' },
		{ "stack(kbytes)", RLIMIT_STACK, 1024, 's' },
		{ "lockedmem(kbytes)", RLIMIT_MEMLOCK, 1024, 'l' },
		{ "memory(kbytes)", RLIMIT_RSS, 1024, 'm' },
		{ "nofiles(descriptors)", RLIMIT_NOFILE, 1, 'n' },
		{ "processes", RLIMIT_NPROC, 1, 'p' },
		{ NULL }
	};
	const char	*options = "HSat#f#c#d#s#l#m#n#p#";
	int		how = SOFT | HARD;
	const struct limits	*l;
	int		optc, all = 0;

	/* First check for -a, -H and -S. */
	while ((optc = ksh_getopt(wp, &builtin_opt, options)) != -1)
		switch (optc) {
		case 'H':
			how = HARD;
			break;
		case 'S':
			how = SOFT;
			break;
		case 'a':
			all = 1;
			break;
		case '?':
			return 1;
		default:
			break;
		}

	if (wp[builtin_opt.optind] != NULL) {
		bi_errorf("usage: ulimit [-acdfHlmnpSst] [value]");
		return 1;
	}

	/* Then parse and act on the actual limits, one at a time */
	ksh_getopt_reset(&builtin_opt, GF_ERROR);
	while ((optc = ksh_getopt(wp, &builtin_opt, options)) != -1)
		switch (optc) {
		case 'a':
		case 'H':
		case 'S':
			break;
		case '?':
			return 1;
		default:
			for (l = limits; l->name && l->option != optc; l++)
				;
			if (!l->name) {
				internal_warningf("%s: %c", __func__, optc);
				return 1;
			}
			if (builtin_opt.optarg) {
				if (set_ulimit(l, builtin_opt.optarg, how))
					return 1;
			} else
				print_ulimit(l, how);
			break;
		}

	wp += builtin_opt.optind;

	if (all) {
		for (l = limits; l->name; l++) {
			shprintf("%-20s ", l->name);
			print_ulimit(l, how);
		}
	} else if (builtin_opt.optind == 1) {
		/* No limit specified, use file size */
		l = &limits[1];
		if (wp[0] != NULL) {
			if (set_ulimit(l, wp[0], how))
				return 1;
			wp++;
		} else {
			print_ulimit(l, how);
		}
	}

	return 0;
}

static int
set_ulimit(const struct limits *l, const char *v, int how)
{
	rlim_t		val = 0;
	struct rlimit	limit;

	if (strcmp(v, "unlimited") == 0)
		val = RLIM_INFINITY;
	else {
		int64_t rval;

		if (!evaluate(v, &rval, KSH_RETURN_ERROR, false))
			return 1;
		/*
		 * Avoid problems caused by typos that evaluate misses due
		 * to evaluating unset parameters to 0...
		 * If this causes problems, will have to add parameter to
		 * evaluate() to control if unset params are 0 or an error.
		 */
		if (!rval && !digit(v[0])) {
			bi_errorf("invalid limit: %s", v);
			return 1;
		}
		val = (rlim_t)rval * l->factor;
	}

	getrlimit(l->resource, &limit);
	if (how & SOFT)
		limit.rlim_cur = val;
	if (how & HARD)
		limit.rlim_max = val;
	if (setrlimit(l->resource, &limit) == -1) {
		if (errno == EPERM)
			bi_errorf("-%c exceeds allowable limit", l->option);
		else
			bi_errorf("bad -%c limit: %s", l->option,
			    strerror(errno));
		return 1;
	}
	return 0;
}

static void
print_ulimit(const struct limits *l, int how)
{
	rlim_t		val = 0;
	struct rlimit	limit;

	getrlimit(l->resource, &limit);
	if (how & SOFT)
		val = limit.rlim_cur;
	else if (how & HARD)
		val = limit.rlim_max;
	if (val == RLIM_INFINITY)
		shprintf("unlimited\n");
	else {
		val /= l->factor;
		shprintf("%" PRIi64 "\n", (int64_t) val);
	}
}
