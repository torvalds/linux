/* vi: set sw=4 ts=4: */
/*
 * Adapted from ash applet code
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Copyright (c) 1989, 1991, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * Copyright (c) 1997-2005 Herbert Xu <herbert@gondor.apana.org.au>
 * was re-ported from NetBSD and debianized.
 *
 * Copyright (c) 2010 Denys Vlasenko
 * Split from ash.c
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "shell_common.h"

const char defifsvar[] ALIGN1 = "IFS= \t\n";
const char defoptindvar[] ALIGN1 = "OPTIND=1";


int FAST_FUNC is_well_formed_var_name(const char *s, char terminator)
{
	if (!s || !(isalpha(*s) || *s == '_'))
		return 0;

	do
		s++;
	while (isalnum(*s) || *s == '_');

	return *s == terminator;
}

/* read builtin */

/* Needs to be interruptible: shell must handle traps and shell-special signals
 * while inside read. To implement this, be sure to not loop on EINTR
 * and return errno == EINTR reliably.
 */
//TODO: use more efficient setvar() which takes a pointer to malloced "VAR=VAL"
//string. hush naturally has it, and ash has setvareq().
//Here we can simply store "VAR=" at buffer start and store read data directly
//after "=", then pass buffer to setvar() to consume.
const char* FAST_FUNC
shell_builtin_read(void FAST_FUNC (*setvar)(const char *name, const char *val),
	char       **argv,
	const char *ifs,
	int        read_flags,
	const char *opt_n,
	const char *opt_p,
	const char *opt_t,
	const char *opt_u,
	const char *opt_d
)
{
	struct pollfd pfd[1];
#define fd (pfd[0].fd) /* -u FD */
	unsigned err;
	unsigned end_ms; /* -t TIMEOUT */
	int nchars; /* -n NUM */
	char **pp;
	char *buffer;
	char delim;
	struct termios tty, old_tty;
	const char *retval;
	int bufpos; /* need to be able to hold -1 */
	int startword;
	smallint backslash;

	errno = err = 0;

	pp = argv;
	while (*pp) {
		if (!is_well_formed_var_name(*pp, '\0')) {
			/* Mimic bash message */
			bb_error_msg("read: '%s': not a valid identifier", *pp);
			return (const char *)(uintptr_t)1;
		}
		pp++;
	}

	nchars = 0; /* if != 0, -n is in effect */
	if (opt_n) {
		nchars = bb_strtou(opt_n, NULL, 10);
		if (nchars < 0 || errno)
			return "invalid count";
		/* note: "-n 0": off (bash 3.2 does this too) */
	}

	end_ms = 0;
	if (opt_t && !ENABLE_FEATURE_SH_READ_FRAC) {
		end_ms = bb_strtou(opt_t, NULL, 10);
		if (errno)
			return "invalid timeout";
		if (end_ms > UINT_MAX / 2048) /* be safely away from overflow */
			end_ms = UINT_MAX / 2048;
		end_ms *= 1000;
	}
	if (opt_t && ENABLE_FEATURE_SH_READ_FRAC) {
		/* bash 4.3 (maybe earlier) supports -t N.NNNNNN */
		char *p;
		/* Eat up to three fractional digits */
		int frac_digits = 3 + 1;

		end_ms = bb_strtou(opt_t, &p, 10);
		if (end_ms > UINT_MAX / 2048) /* be safely away from overflow */
			end_ms = UINT_MAX / 2048;

		if (errno) {
			/* EINVAL = number is ok, but not NUL terminated */
			if (errno != EINVAL || *p != '.')
				return "invalid timeout";
			/* Do not check the rest: bash allows "0.123456xyz" */
			while (*++p && --frac_digits) {
				end_ms *= 10;
				end_ms += (*p - '0');
				if ((unsigned char)(*p - '0') > 9)
					return "invalid timeout";
			}
		}
		while (--frac_digits > 0) {
			end_ms *= 10;
		}
	}

	fd = STDIN_FILENO;
	if (opt_u) {
		fd = bb_strtou(opt_u, NULL, 10);
		if (fd < 0 || errno)
			return "invalid file descriptor";
	}

	if (opt_t && end_ms == 0) {
		/* "If timeout is 0, read returns immediately, without trying
		 * to read any data. The exit status is 0 if input is available
		 * on the specified file descriptor, non-zero otherwise."
		 * bash seems to ignore -p PROMPT for this use case.
		 */
		int r;
		pfd[0].events = POLLIN;
		r = poll(pfd, 1, /*timeout:*/ 0);
		/* Return 0 only if poll returns 1 ("one fd ready"), else return 1: */
		return (const char *)(uintptr_t)(r <= 0);
	}

	if (opt_p && isatty(fd)) {
		fputs(opt_p, stderr);
		fflush_all();
	}

	if (ifs == NULL)
		ifs = defifs;

	if (nchars || (read_flags & BUILTIN_READ_SILENT)) {
		tcgetattr(fd, &tty);
		old_tty = tty;
		if (nchars) {
			tty.c_lflag &= ~ICANON;
			// Setting it to more than 1 breaks poll():
			// it blocks even if there's data. !??
			//tty.c_cc[VMIN] = nchars < 256 ? nchars : 255;
			/* reads will block only if < 1 char is available */
			tty.c_cc[VMIN] = 1;
			/* no timeout (reads block forever) */
			tty.c_cc[VTIME] = 0;
		}
		if (read_flags & BUILTIN_READ_SILENT) {
			tty.c_lflag &= ~(ECHO | ECHOK | ECHONL);
		}
		/* This forces execution of "restoring" tcgetattr later */
		read_flags |= BUILTIN_READ_SILENT;
		/* if tcgetattr failed, tcsetattr will fail too.
		 * Ignoring, it's harmless. */
		tcsetattr(fd, TCSANOW, &tty);
	}

	retval = (const char *)(uintptr_t)0;
	startword = 1;
	backslash = 0;
	if (opt_t)
		end_ms += (unsigned)monotonic_ms();
	buffer = NULL;
	bufpos = 0;
	delim = opt_d ? *opt_d : '\n';
	do {
		char c;
		int timeout;

		if ((bufpos & 0xff) == 0)
			buffer = xrealloc(buffer, bufpos + 0x101);

		timeout = -1;
		if (opt_t) {
			timeout = end_ms - (unsigned)monotonic_ms();
			/* ^^^^^^^^^^^^^ all values are unsigned,
			 * wrapping math is used here, good even if
			 * 32-bit unix time wrapped (year 2038+).
			 */
			if (timeout <= 0) { /* already late? */
				retval = (const char *)(uintptr_t)1;
				goto ret;
			}
		}

		/* We must poll even if timeout is -1:
		 * we want to be interrupted if signal arrives,
		 * regardless of SA_RESTART-ness of that signal!
		 */
		errno = 0;
		pfd[0].events = POLLIN;
		if (poll(pfd, 1, timeout) <= 0) {
			/* timed out, or EINTR */
			err = errno;
			retval = (const char *)(uintptr_t)1;
			goto ret;
		}
		if (read(fd, &buffer[bufpos], 1) != 1) {
			err = errno;
			retval = (const char *)(uintptr_t)1;
			break;
		}

		c = buffer[bufpos];
		if (c == '\0')
			continue;
		if (!(read_flags & BUILTIN_READ_RAW)) {
			if (backslash) {
				backslash = 0;
				if (c != '\n')
					goto put;
				continue;
			}
			if (c == '\\') {
				backslash = 1;
				continue;
			}
		}
		if (c == delim) /* '\n' or -d CHAR */
			break;

		/* $IFS splitting. NOT done if we run "read"
		 * without variable names (bash compat).
		 * Thus, "read" and "read REPLY" are not the same.
		 */
		if (!opt_d && argv[0]) {
/* http://www.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_06_05 */
			const char *is_ifs = strchr(ifs, c);
			if (startword && is_ifs) {
				if (isspace(c))
					continue;
				/* it is a non-space ifs char */
				startword--;
				if (startword == 1) /* first one? */
					continue; /* yes, it is not next word yet */
			}
			startword = 0;
			if (argv[1] != NULL && is_ifs) {
				buffer[bufpos] = '\0';
				bufpos = 0;
				setvar(*argv, buffer);
				argv++;
				/* can we skip one non-space ifs char? (2: yes) */
				startword = isspace(c) ? 2 : 1;
				continue;
			}
		}
 put:
		bufpos++;
	} while (--nchars);

	if (argv[0]) {
		/* Remove trailing space $IFS chars */
		while (--bufpos >= 0
		 && isspace(buffer[bufpos])
		 && strchr(ifs, buffer[bufpos]) != NULL
		) {
			continue;
		}
		buffer[bufpos + 1] = '\0';

		/* Last variable takes the entire remainder with delimiters
		 * (sans trailing whitespace $IFS),
		 * but ***only "if there are fewer vars than fields"(c)***!
		 * The "X:Y:" case below: there are two fields,
		 * and therefore last delimiter (:) is eaten:
		 * IFS=": "
		 * echo "X:Y:Z:"  | (read x y; echo "|$x|$y|") # |X|Y:Z:|
		 * echo "X:Y:Z"   | (read x y; echo "|$x|$y|") # |X|Y:Z|
		 * echo "X:Y:"    | (read x y; echo "|$x|$y|") # |X|Y|, not |X|Y:|
		 * echo "X:Y  : " | (read x y; echo "|$x|$y|") # |X|Y|
		 */
		if (bufpos >= 0
		 && strchr(ifs, buffer[bufpos]) != NULL
		) {
			/* There _is_ a non-whitespace IFS char */
			/* Skip whitespace IFS char before it */
			while (--bufpos >= 0
			 && isspace(buffer[bufpos])
			 && strchr(ifs, buffer[bufpos]) != NULL
			) {
				continue;
			}
			/* Are there $IFS chars? */
			if (strcspn(buffer, ifs) >= ++bufpos) {
				/* No: last var takes one field, not more */
				/* So, drop trailing IFS delims */
				buffer[bufpos] = '\0';
			}
		}

		/* Use the remainder as a value for the next variable */
		setvar(*argv, buffer);
		/* Set the rest to "" */
		while (*++argv)
			setvar(*argv, "");
	} else {
		/* Note: no $IFS removal */
		buffer[bufpos] = '\0';
		setvar("REPLY", buffer);
	}

 ret:
	free(buffer);
	if (read_flags & BUILTIN_READ_SILENT)
		tcsetattr(fd, TCSANOW, &old_tty);

	errno = err;
	return retval;
#undef fd
}

/* ulimit builtin */

struct limits {
	uint8_t cmd;            /* RLIMIT_xxx fit into it */
	uint8_t factor_shift;   /* shift by to get rlim_{cur,max} values */
	char option;
	const char *name;
};

static const struct limits limits_tbl[] = {
#ifdef RLIMIT_FSIZE
	{ RLIMIT_FSIZE,		9,	'f',	"file size (blocks)" },
#endif
#ifdef RLIMIT_CPU
	{ RLIMIT_CPU,		0,	't',	"cpu time (seconds)" },
#endif
#ifdef RLIMIT_DATA
	{ RLIMIT_DATA,		10,	'd',	"data seg size (kb)" },
#endif
#ifdef RLIMIT_STACK
	{ RLIMIT_STACK,		10,	's',	"stack size (kb)" },
#endif
#ifdef RLIMIT_CORE
	{ RLIMIT_CORE,		9,	'c',	"core file size (blocks)" },
#endif
#ifdef RLIMIT_RSS
	{ RLIMIT_RSS,		10,	'm',	"resident set size (kb)" },
#endif
#ifdef RLIMIT_MEMLOCK
	{ RLIMIT_MEMLOCK,	10,	'l',	"locked memory (kb)" },
#endif
#ifdef RLIMIT_NPROC
	{ RLIMIT_NPROC,		0,	'p',	"processes" },
#endif
#ifdef RLIMIT_NOFILE
	{ RLIMIT_NOFILE,	0,	'n',	"file descriptors" },
#endif
#ifdef RLIMIT_AS
	{ RLIMIT_AS,		10,	'v',	"address space (kb)" },
#endif
#ifdef RLIMIT_LOCKS
	{ RLIMIT_LOCKS,		0,	'w',	"locks" },
#endif
#ifdef RLIMIT_NICE
	{ RLIMIT_NICE,		0,	'e',	"scheduling priority" },
#endif
#ifdef RLIMIT_RTPRIO
	{ RLIMIT_RTPRIO,	0,	'r',	"real-time priority" },
#endif
};

enum {
	OPT_hard = (1 << 0),
	OPT_soft = (1 << 1),
};

/* "-": treat args as parameters of option with ASCII code 1 */
static const char ulimit_opt_string[] ALIGN1 = "-HSa"
#ifdef RLIMIT_FSIZE
			"f::"
#endif
#ifdef RLIMIT_CPU
			"t::"
#endif
#ifdef RLIMIT_DATA
			"d::"
#endif
#ifdef RLIMIT_STACK
			"s::"
#endif
#ifdef RLIMIT_CORE
			"c::"
#endif
#ifdef RLIMIT_RSS
			"m::"
#endif
#ifdef RLIMIT_MEMLOCK
			"l::"
#endif
#ifdef RLIMIT_NPROC
			"p::"
#endif
#ifdef RLIMIT_NOFILE
			"n::"
#endif
#ifdef RLIMIT_AS
			"v::"
#endif
#ifdef RLIMIT_LOCKS
			"w::"
#endif
#ifdef RLIMIT_NICE
			"e::"
#endif
#ifdef RLIMIT_RTPRIO
			"r::"
#endif
			;

static void printlim(unsigned opts, const struct rlimit *limit,
			const struct limits *l)
{
	rlim_t val;

	val = limit->rlim_max;
	if (!(opts & OPT_hard))
		val = limit->rlim_cur;

	if (val == RLIM_INFINITY)
		puts("unlimited");
	else {
		val >>= l->factor_shift;
		printf("%llu\n", (long long) val);
	}
}

int FAST_FUNC
shell_builtin_ulimit(char **argv)
{
	unsigned opts;
	unsigned argc;

	/* We can't use getopt32: need to handle commands like
	 * ulimit 123 -c2 -l 456
	 */

	/* In case getopt() was already called:
	 * reset libc getopt() internal state.
	 */
	GETOPT_RESET();

	argc = string_array_len(argv);

	opts = 0;
	while (1) {
		struct rlimit limit;
		const struct limits *l;
		int opt_char = getopt(argc, argv, ulimit_opt_string);

		if (opt_char == -1)
			break;
		if (opt_char == 'H') {
			opts |= OPT_hard;
			continue;
		}
		if (opt_char == 'S') {
			opts |= OPT_soft;
			continue;
		}

		if (opt_char == 'a') {
			for (l = limits_tbl; l != &limits_tbl[ARRAY_SIZE(limits_tbl)]; l++) {
				getrlimit(l->cmd, &limit);
				printf("-%c: %-30s ", l->option, l->name);
				printlim(opts, &limit, l);
			}
			continue;
		}

		if (opt_char == 1)
			opt_char = 'f';
		for (l = limits_tbl; l != &limits_tbl[ARRAY_SIZE(limits_tbl)]; l++) {
			if (opt_char == l->option) {
				char *val_str;

				getrlimit(l->cmd, &limit);

				val_str = optarg;
				if (!val_str && argv[optind] && argv[optind][0] != '-')
					val_str = argv[optind++]; /* ++ skips NN in "-c NN" case */
				if (val_str) {
					rlim_t val;

					if (strcmp(val_str, "unlimited") == 0)
						val = RLIM_INFINITY;
					else {
						if (sizeof(val) == sizeof(int))
							val = bb_strtou(val_str, NULL, 10);
						else if (sizeof(val) == sizeof(long))
							val = bb_strtoul(val_str, NULL, 10);
						else
							val = bb_strtoull(val_str, NULL, 10);
						if (errno) {
							bb_error_msg("invalid number '%s'", val_str);
							return EXIT_FAILURE;
						}
						val <<= l->factor_shift;
					}
//bb_error_msg("opt %c val_str:'%s' val:%lld", opt_char, val_str, (long long)val);
					/* from man bash: "If neither -H nor -S
					 * is specified, both the soft and hard
					 * limits are set. */
					if (!opts)
						opts = OPT_hard + OPT_soft;
					if (opts & OPT_hard)
						limit.rlim_max = val;
					if (opts & OPT_soft)
						limit.rlim_cur = val;
//bb_error_msg("setrlimit(%d, %lld, %lld)", l->cmd, (long long)limit.rlim_cur, (long long)limit.rlim_max);
					if (setrlimit(l->cmd, &limit) < 0) {
						bb_perror_msg("error setting limit");
						return EXIT_FAILURE;
					}
				} else {
					printlim(opts, &limit, l);
				}
				break;
			}
		} /* for (every possible opt) */

		if (l == &limits_tbl[ARRAY_SIZE(limits_tbl)]) {
			/* bad option. getopt already complained. */
			break;
		}
	} /* while (there are options) */

	return 0;
}
