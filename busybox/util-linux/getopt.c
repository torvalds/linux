/* vi: set sw=4 ts=4: */
/*
 * getopt.c - Enhanced implementation of BSD getopt(1)
 * Copyright (c) 1997, 1998, 1999, 2000  Frodo Looijaard <frodol@dds.nl>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/*
 * Version 1.0-b4: Tue Sep 23 1997. First public release.
 * Version 1.0: Wed Nov 19 1997.
 *   Bumped up the version number to 1.0
 *   Fixed minor typo (CSH instead of TCSH)
 * Version 1.0.1: Tue Jun 3 1998
 *   Fixed sizeof instead of strlen bug
 *   Bumped up the version number to 1.0.1
 * Version 1.0.2: Thu Jun 11 1998 (not present)
 *   Fixed gcc-2.8.1 warnings
 *   Fixed --version/-V option (not present)
 * Version 1.0.5: Tue Jun 22 1999
 *   Make -u option work (not present)
 * Version 1.0.6: Tue Jun 27 2000
 *   No important changes
 * Version 1.1.0: Tue Jun 30 2000
 *   Added NLS support (partly written by Arkadiusz Mickiewicz
 *     <misiek@misiek.eu.org>)
 * Ported to Busybox - Alfred M. Szmidt <ams@trillian.itslinux.org>
 *  Removed --version/-V and --help/-h
 *  Removed parse_error(), using bb_error_msg() from Busybox instead
 *  Replaced our_malloc with xmalloc and our_realloc with xrealloc
 */
//config:config GETOPT
//config:	bool "getopt (5.6 kb)"
//config:	default y
//config:	help
//config:	The getopt utility is used to break up (parse) options in command
//config:	lines to make it easy to write complex shell scripts that also check
//config:	for legal (and illegal) options. If you want to write horribly
//config:	complex shell scripts, or use some horribly complex shell script
//config:	written by others, this utility may be for you. Most people will
//config:	wisely leave this disabled.
//config:
//config:config FEATURE_GETOPT_LONG
//config:	bool "Support -l LONGOPTs"
//config:	default y
//config:	depends on GETOPT && LONG_OPTS
//config:	help
//config:	Enable support for long options (option -l).

//applet:IF_GETOPT(APPLET_NOEXEC(getopt, getopt, BB_DIR_BIN, BB_SUID_DROP, getopt))

//kbuild:lib-$(CONFIG_GETOPT) += getopt.o

//usage:#define getopt_trivial_usage
//usage:       "[OPTIONS] [--] OPTSTRING PARAMS"
//usage:#define getopt_full_usage "\n\n"
//usage:	IF_FEATURE_GETOPT_LONG(
//usage:       "	-a		Allow long options starting with single -\n"
//usage:       "	-l LOPT[,...]	Long options to recognize\n"
//usage:	)
//usage:       "	-n PROGNAME	The name under which errors are reported"
//usage:     "\n	-o OPTSTRING	Short options to recognize"
//usage:     "\n	-q		No error messages on unrecognized options"
//usage:     "\n	-Q		No normal output"
//usage:     "\n	-s SHELL	Set shell quoting conventions"
//usage:     "\n	-T		Version test (exits with 4)"
//usage:     "\n	-u		Don't quote output"
//usage:	IF_FEATURE_GETOPT_LONG( /* example uses -l, needs FEATURE_GETOPT_LONG */
//usage:     "\n"
//usage:     "\nExample:"
//usage:     "\n"
//usage:     "\nO=`getopt -l bb: -- ab:c:: \"$@\"` || exit 1"
//usage:     "\neval set -- \"$O\""
//usage:     "\nwhile true; do"
//usage:     "\n	case \"$1\" in"
//usage:     "\n	-a)	echo A; shift;;"
//usage:     "\n	-b|--bb) echo \"B:'$2'\"; shift 2;;"
//usage:     "\n	-c)	case \"$2\" in"
//usage:     "\n		\"\")	echo C; shift 2;;"
//usage:     "\n		*)	echo \"C:'$2'\"; shift 2;;"
//usage:     "\n		esac;;"
//usage:     "\n	--)	shift; break;;"
//usage:     "\n	*)	echo Error; exit 1;;"
//usage:     "\n	esac"
//usage:     "\ndone"
//usage:	)
//usage:
//usage:#define getopt_example_usage
//usage:       "$ cat getopt.test\n"
//usage:       "#!/bin/sh\n"
//usage:       "GETOPT=`getopt -o ab:c:: --long a-long,b-long:,c-long:: \\\n"
//usage:       "       -n 'example.busybox' -- \"$@\"`\n"
//usage:       "if [ $? != 0 ]; then exit 1; fi\n"
//usage:       "eval set -- \"$GETOPT\"\n"
//usage:       "while true; do\n"
//usage:       " case $1 in\n"
//usage:       "   -a|--a-long) echo \"Option a\"; shift;;\n"
//usage:       "   -b|--b-long) echo \"Option b, argument '$2'\"; shift 2;;\n"
//usage:       "   -c|--c-long)\n"
//usage:       "     case \"$2\" in\n"
//usage:       "       \"\") echo \"Option c, no argument\"; shift 2;;\n"
//usage:       "       *)  echo \"Option c, argument '$2'\"; shift 2;;\n"
//usage:       "     esac;;\n"
//usage:       "   --) shift; break;;\n"
//usage:       "   *) echo \"Internal error!\"; exit 1;;\n"
//usage:       " esac\n"
//usage:       "done\n"

#if ENABLE_FEATURE_GETOPT_LONG
# include <getopt.h>
#endif
#include "libbb.h"

/* NON_OPT is the code that is returned when a non-option is found in '+'
   mode */
enum {
	NON_OPT = 1,
#if ENABLE_FEATURE_GETOPT_LONG
/* LONG_OPT is the code that is returned when a long option is found. */
	LONG_OPT = 2
#endif
};

/* For finding activated option flags. Must match getopt32 call! */
enum {
	OPT_o	= 0x1,	// -o
	OPT_n	= 0x2,	// -n
	OPT_q	= 0x4,	// -q
	OPT_Q	= 0x8,	// -Q
	OPT_s	= 0x10,	// -s
	OPT_T	= 0x20,	// -T
	OPT_u	= 0x40,	// -u
#if ENABLE_FEATURE_GETOPT_LONG
	OPT_a	= 0x80,	// -a
	OPT_l	= 0x100, // -l
#endif
	SHELL_IS_TCSH = 0x8000, /* hijack this bit for other purposes */
};

/* 0 is getopt_long, 1 is getopt_long_only */
#define alternative  (option_mask32 & OPT_a)

#define quiet_errors (option_mask32 & OPT_q)
#define quiet_output (option_mask32 & OPT_Q)
#define quote        (!(option_mask32 & OPT_u))
#define shell_TCSH   (option_mask32 & SHELL_IS_TCSH)

/*
 * This function 'normalizes' a single argument: it puts single quotes around
 * it and escapes other special characters. If quote is false, it just
 * returns its argument.
 * Bash only needs special treatment for single quotes; tcsh also recognizes
 * exclamation marks within single quotes, and nukes whitespace.
 * This function returns a pointer to a buffer that is overwritten by
 * each call.
 */
static const char *normalize(const char *arg)
{
	char *bufptr;
#if ENABLE_FEATURE_CLEAN_UP
	static char *BUFFER = NULL;
	free(BUFFER);
#else
	char *BUFFER;
#endif

	if (!quote) { /* Just copy arg */
		BUFFER = xstrdup(arg);
		return BUFFER;
	}

	/* Each character in arg may take up to four characters in the result:
	   For a quote we need a closing quote, a backslash, a quote and an
	   opening quote! We need also the global opening and closing quote,
	   and one extra character for '\0'. */
	BUFFER = xmalloc(strlen(arg)*4 + 3);

	bufptr = BUFFER;
	*bufptr ++= '\'';

	while (*arg) {
		if (*arg == '\'') {
			/* Quote: replace it with: '\'' */
			*bufptr ++= '\'';
			*bufptr ++= '\\';
			*bufptr ++= '\'';
			*bufptr ++= '\'';
		} else if (shell_TCSH && *arg == '!') {
			/* Exclamation mark: replace it with: \! */
			*bufptr ++= '\'';
			*bufptr ++= '\\';
			*bufptr ++= '!';
			*bufptr ++= '\'';
		} else if (shell_TCSH && *arg == '\n') {
			/* Newline: replace it with: \n */
			*bufptr ++= '\\';
			*bufptr ++= 'n';
		} else if (shell_TCSH && isspace(*arg)) {
			/* Non-newline whitespace: replace it with \<ws> */
			*bufptr ++= '\'';
			*bufptr ++= '\\';
			*bufptr ++= *arg;
			*bufptr ++= '\'';
		} else
			/* Just copy */
			*bufptr ++= *arg;
		arg++;
	}
	*bufptr ++= '\'';
	*bufptr ++= '\0';
	return BUFFER;
}

/*
 * Generate the output. argv[0] is the program name (used for reporting errors).
 * argv[1..] contains the options to be parsed. argc must be the number of
 * elements in argv (ie. 1 if there are no options, only the program name),
 * optstr must contain the short options, and longopts the long options.
 * Other settings are found in global variables.
 */
#if !ENABLE_FEATURE_GETOPT_LONG
#define generate_output(argv,argc,optstr,longopts) \
	generate_output(argv,argc,optstr)
#endif
static int generate_output(char **argv, int argc, const char *optstr, const struct option *longopts)
{
	int exit_code = 0; /* We assume everything will be OK */

	if (quiet_errors) /* No error reporting from getopt(3) */
		opterr = 0;

	/* We used it already in main() in getopt32(),
	 * we *must* reset getopt(3): */
	GETOPT_RESET();

	while (1) {
#if ENABLE_FEATURE_GETOPT_LONG
		int longindex;
		int opt = alternative
			? getopt_long_only(argc, argv, optstr, longopts, &longindex)
			: getopt_long(argc, argv, optstr, longopts, &longindex)
		;
#else
		int opt = getopt(argc, argv, optstr);
#endif
		if (opt == -1)
			break;
		if (opt == '?' || opt == ':' )
			exit_code = 1;
		else if (!quiet_output) {
#if ENABLE_FEATURE_GETOPT_LONG
			if (opt == LONG_OPT) {
				printf(" --%s", longopts[longindex].name);
				if (longopts[longindex].has_arg)
					printf(" %s",
						normalize(optarg ? optarg : ""));
			} else
#endif
			if (opt == NON_OPT)
				printf(" %s", normalize(optarg));
			else {
				const char *charptr;
				printf(" -%c", opt);
				charptr = strchr(optstr, opt);
				if (charptr && *++charptr == ':')
					printf(" %s",
						normalize(optarg ? optarg : ""));
			}
		}
	}

	if (!quiet_output) {
		unsigned idx;
		printf(" --");
		idx = optind;
		while (argv[idx])
			printf(" %s", normalize(argv[idx++]));
		bb_putchar('\n');
	}
	return exit_code;
}

#if ENABLE_FEATURE_GETOPT_LONG
/*
 * Register several long options. options is a string of long options,
 * separated by commas or whitespace.
 * This nukes options!
 */
static struct option *add_long_options(struct option *long_options, char *options)
{
	int long_nr = 0;
	int arg_opt, tlen;
	char *tokptr = strtok(options, ", \t\n");

	if (long_options)
		while (long_options[long_nr].name)
			long_nr++;

	while (tokptr) {
		arg_opt = no_argument;
		tlen = strlen(tokptr);
		if (tlen) {
			tlen--;
			if (tokptr[tlen] == ':') {
				arg_opt = required_argument;
				if (tlen && tokptr[tlen-1] == ':') {
					tlen--;
					arg_opt = optional_argument;
				}
				tokptr[tlen] = '\0';
				if (tlen == 0)
					bb_error_msg_and_die("empty long option specified");
			}
			long_options = xrealloc_vector(long_options, 4, long_nr);
			long_options[long_nr].has_arg = arg_opt;
			/*long_options[long_nr].flag = NULL; - xrealloc_vector did it */
			long_options[long_nr].val = LONG_OPT;
			long_options[long_nr].name = xstrdup(tokptr);
			long_nr++;
			/*memset(&long_options[long_nr], 0, sizeof(long_options[0])); - xrealloc_vector did it */
		}
		tokptr = strtok(NULL, ", \t\n");
	}
	return long_options;
}
#endif

static void set_shell(const char *new_shell)
{
	if (strcmp(new_shell, "bash") == 0 || strcmp(new_shell, "sh") == 0)
		return;
	if (strcmp(new_shell, "tcsh") == 0 || strcmp(new_shell, "csh") == 0)
		option_mask32 |= SHELL_IS_TCSH;
	else
		bb_error_msg("unknown shell '%s', assuming bash", new_shell);
}


/* Exit codes:
 *   0) No errors, successful operation.
 *   1) getopt(3) returned an error.
 *   2) A problem with parameter parsing for getopt(1).
 *   3) Internal error, out of memory
 *   4) Returned for -T
 */

#if ENABLE_FEATURE_GETOPT_LONG
static const char getopt_longopts[] ALIGN1 =
	"options\0"      Required_argument "o"
	"longoptions\0"  Required_argument "l"
	"quiet\0"        No_argument       "q"
	"quiet-output\0" No_argument       "Q"
	"shell\0"        Required_argument "s"
	"test\0"         No_argument       "T"
	"unquoted\0"     No_argument       "u"
	"alternative\0"  No_argument       "a"
	"name\0"         Required_argument "n"
	;
#endif

int getopt_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int getopt_main(int argc, char **argv)
{
	int n;
	char *optstr = NULL;
	char *name = NULL;
	unsigned opt;
	const char *compatible;
	char *s_arg;
#if ENABLE_FEATURE_GETOPT_LONG
	struct option *long_options = NULL;
	llist_t *l_arg = NULL;
#endif

	compatible = getenv("GETOPT_COMPATIBLE"); /* used as yes/no flag */

	if (!argv[1]) {
		if (compatible) {
			/* For some reason, the original getopt gave no error
			 * when there were no arguments. */
			puts(" --");
			return 0;
		}
		bb_error_msg_and_die("missing optstring argument");
	}

	if (argv[1][0] != '-' || compatible) {
		char *s = argv[1];

		option_mask32 |= OPT_u; /* quoting off */
		s = xstrdup(s + strspn(s, "-+"));
		argv[1] = argv[0];
		return generate_output(argv+1, argc-1, s, long_options);
	}

#if !ENABLE_FEATURE_GETOPT_LONG
	opt = getopt32(argv, "+o:n:qQs:Tu", &optstr, &name, &s_arg);
#else
	opt = getopt32long(argv, "+o:n:qQs:Tual:*", getopt_longopts,
					&optstr, &name, &s_arg, &l_arg);
	/* Effectuate the read options for the applet itself */
	while (l_arg) {
		long_options = add_long_options(long_options, llist_pop(&l_arg));
	}
#endif

	if (opt & OPT_s) {
		set_shell(s_arg);
	}

	if (opt & OPT_T) {
		return 4;
	}

	/* All options controlling the applet have now been parsed */
	n = optind - 1;
	if (!optstr) {
		optstr = argv[++n];
		if (!optstr)
			bb_error_msg_and_die("missing optstring argument");
	}

	argv[n] = name ? name : argv[0];
	return generate_output(argv + n, argc - n, optstr, long_options);
}
