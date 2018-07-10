/* vi: set sw=4 ts=4: */
/*
 * printf - format and print data
 *
 * Copyright 1999 Dave Cinege
 * Portions copyright (C) 1990-1996 Free Software Foundation, Inc.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* Usage: printf format [argument...]
 *
 * A front end to the printf function that lets it be used from the shell.
 *
 * Backslash escapes:
 *
 * \" = double quote
 * \\ = backslash
 * \a = alert (bell)
 * \b = backspace
 * \c = produce no further output
 * \f = form feed
 * \n = new line
 * \r = carriage return
 * \t = horizontal tab
 * \v = vertical tab
 * \0ooo = octal number (ooo is 0 to 3 digits)
 * \xhhh = hexadecimal number (hhh is 1 to 3 digits)
 *
 * Additional directive:
 *
 * %b = print an argument string, interpreting backslash escapes
 *
 * The 'format' argument is re-used as many times as necessary
 * to convert all of the given arguments.
 *
 * David MacKenzie <djm@gnu.ai.mit.edu>
 */
/* 19990508 Busy Boxed! Dave Cinege */

//config:config PRINTF
//config:	bool "printf (3.3 kb)"
//config:	default y
//config:	help
//config:	printf is used to format and print specified strings.
//config:	It's similar to 'echo' except it has more options.

//applet:IF_PRINTF(APPLET_NOFORK(printf, printf, BB_DIR_USR_BIN, BB_SUID_DROP, printf))

//kbuild:lib-$(CONFIG_PRINTF) += printf.o
//kbuild:lib-$(CONFIG_ASH_PRINTF)  += printf.o
//kbuild:lib-$(CONFIG_HUSH_PRINTF) += printf.o

//usage:#define printf_trivial_usage
//usage:       "FORMAT [ARG]..."
//usage:#define printf_full_usage "\n\n"
//usage:       "Format and print ARG(s) according to FORMAT (a-la C printf)"
//usage:
//usage:#define printf_example_usage
//usage:       "$ printf \"Val=%d\\n\" 5\n"
//usage:       "Val=5\n"

#include "libbb.h"

/* A note on bad input: neither bash 3.2 nor coreutils 6.10 stop on it.
 * They report it:
 *  bash: printf: XXX: invalid number
 *  printf: XXX: expected a numeric value
 *  bash: printf: 123XXX: invalid number
 *  printf: 123XXX: value not completely converted
 * but then they use 0 (or partially converted numeric prefix) as a value
 * and continue. They exit with 1 in this case.
 * Both accept insane field width/precision (e.g. %9999999999.9999999999d).
 * Both print error message and assume 0 if %*.*f width/precision is "bad"
 *  (but negative numbers are not "bad").
 * Both accept negative numbers for %u specifier.
 *
 * We try to be compatible.
 */

typedef void FAST_FUNC (*converter)(const char *arg, void *result);

static int multiconvert(const char *arg, void *result, converter convert)
{
	if (*arg == '"' || *arg == '\'') {
		arg = utoa((unsigned char)arg[1]);
	}
	errno = 0;
	convert(arg, result);
	if (errno) {
		bb_error_msg("invalid number '%s'", arg);
		return 1;
	}
	return 0;
}

static void FAST_FUNC conv_strtoull(const char *arg, void *result)
{
	*(unsigned long long*)result = bb_strtoull(arg, NULL, 0);
	/* both coreutils 6.10 and bash 3.2:
	 * $ printf '%x\n' -2
	 * fffffffffffffffe
	 * Mimic that:
	 */
	if (errno) {
		*(unsigned long long*)result = bb_strtoll(arg, NULL, 0);
	}
}
static void FAST_FUNC conv_strtoll(const char *arg, void *result)
{
	*(long long*)result = bb_strtoll(arg, NULL, 0);
}
static void FAST_FUNC conv_strtod(const char *arg, void *result)
{
	char *end;
	/* Well, this one allows leading whitespace... so what? */
	/* What I like much less is that "-" accepted too! :( */
	*(double*)result = strtod(arg, &end);
	if (end[0]) {
		errno = ERANGE;
		*(double*)result = 0;
	}
}

/* Callers should check errno to detect errors */
static unsigned long long my_xstrtoull(const char *arg)
{
	unsigned long long result;
	if (multiconvert(arg, &result, conv_strtoull))
		result = 0;
	return result;
}
static long long my_xstrtoll(const char *arg)
{
	long long result;
	if (multiconvert(arg, &result, conv_strtoll))
		result = 0;
	return result;
}
static double my_xstrtod(const char *arg)
{
	double result;
	multiconvert(arg, &result, conv_strtod);
	return result;
}

/* Handles %b; return 1 if output is to be short-circuited by \c */
static int print_esc_string(const char *str)
{
	char c;
	while ((c = *str) != '\0') {
		str++;
		if (c == '\\') {
			/* %b also accepts 4-digit octals of the form \0### */
			if (*str == '0') {
				if ((unsigned char)(str[1] - '0') < 8) {
					/* 2nd char is 0..7: skip leading '0' */
					str++;
				}
			}
			else if (*str == 'c') {
				return 1;
			}
			{
				/* optimization: don't force arg to be on-stack,
				 * use another variable for that. */
				const char *z = str;
				c = bb_process_escape_sequence(&z);
				str = z;
			}
		}
		putchar(c);
	}

	return 0;
}

static void print_direc(char *format, unsigned fmt_length,
		int field_width, int precision,
		const char *argument)
{
	long long llv;
	double dv;
	char saved;
	char *have_prec, *have_width;

	saved = format[fmt_length];
	format[fmt_length] = '\0';

	have_prec = strstr(format, ".*");
	have_width = strchr(format, '*');
	if (have_width - 1 == have_prec)
		have_width = NULL;

	errno = 0;

	switch (format[fmt_length - 1]) {
	case 'c':
		printf(format, *argument);
		break;
	case 'd':
	case 'i':
		llv = my_xstrtoll(argument);
 print_long:
		if (!have_width) {
			if (!have_prec)
				printf(format, llv);
			else
				printf(format, precision, llv);
		} else {
			if (!have_prec)
				printf(format, field_width, llv);
			else
				printf(format, field_width, precision, llv);
		}
		break;
	case 'o':
	case 'u':
	case 'x':
	case 'X':
		llv = my_xstrtoull(argument);
		/* cheat: unsigned long and long have same width, so... */
		goto print_long;
	case 's':
		/* Are char* and long long the same? */
		if (sizeof(argument) == sizeof(llv)) {
			llv = (long long)(ptrdiff_t)argument;
			goto print_long;
		} else {
			/* Hope compiler will optimize it out by moving call
			 * instruction after the ifs... */
			if (!have_width) {
				if (!have_prec)
					printf(format, argument, /*unused:*/ argument, argument);
				else
					printf(format, precision, argument, /*unused:*/ argument);
			} else {
				if (!have_prec)
					printf(format, field_width, argument, /*unused:*/ argument);
				else
					printf(format, field_width, precision, argument);
			}
			break;
		}
	case 'f':
	case 'e':
	case 'E':
	case 'g':
	case 'G':
		dv = my_xstrtod(argument);
		if (!have_width) {
			if (!have_prec)
				printf(format, dv);
			else
				printf(format, precision, dv);
		} else {
			if (!have_prec)
				printf(format, field_width, dv);
			else
				printf(format, field_width, precision, dv);
		}
		break;
	} /* switch */

	format[fmt_length] = saved;
}

/* Handle params for "%*.*f". Negative numbers are ok (compat). */
static int get_width_prec(const char *str)
{
	int v = bb_strtoi(str, NULL, 10);
	if (errno) {
		bb_error_msg("invalid number '%s'", str);
		v = 0;
	}
	return v;
}

/* Print the text in FORMAT, using ARGV for arguments to any '%' directives.
   Return advanced ARGV.  */
static char **print_formatted(char *f, char **argv, int *conv_err)
{
	char *direc_start;      /* Start of % directive.  */
	unsigned direc_length;  /* Length of % directive.  */
	int field_width;        /* Arg to first '*' */
	int precision;          /* Arg to second '*' */
	char **saved_argv = argv;

	for (; *f; ++f) {
		switch (*f) {
		case '%':
			direc_start = f++;
			direc_length = 1;
			field_width = precision = 0;
			if (*f == '%') {
				bb_putchar('%');
				break;
			}
			if (*f == 'b') {
				if (*argv) {
					if (print_esc_string(*argv))
						return saved_argv; /* causes main() to exit */
					++argv;
				}
				break;
			}
			if (*f && strchr("-+ #", *f)) {
				++f;
				++direc_length;
			}
			if (*f == '*') {
				++f;
				++direc_length;
				if (*argv)
					field_width = get_width_prec(*argv++);
			} else {
				while (isdigit(*f)) {
					++f;
					++direc_length;
				}
			}
			if (*f == '.') {
				++f;
				++direc_length;
				if (*f == '*') {
					++f;
					++direc_length;
					if (*argv)
						precision = get_width_prec(*argv++);
				} else {
					while (isdigit(*f)) {
						++f;
						++direc_length;
					}
				}
			}

			/* Remove "lLhz" size modifiers, repeatedly.
			 * bash does not like "%lld", but coreutils
			 * happily takes even "%Llllhhzhhzd"!
			 * We are permissive like coreutils */
			while ((*f | 0x20) == 'l' || *f == 'h' || *f == 'z') {
				overlapping_strcpy(f, f + 1);
			}
			/* Add "ll" if integer modifier, then print */
			{
				static const char format_chars[] ALIGN1 = "diouxXfeEgGcs";
				char *p = strchr(format_chars, *f);
				/* needed - try "printf %" without it */
				if (p == NULL || *f == '\0') {
					bb_error_msg("%s: invalid format", direc_start);
					/* causes main() to exit with error */
					return saved_argv - 1;
				}
				++direc_length;
				if (p - format_chars <= 5) {
					/* it is one of "diouxX" */
					p = xmalloc(direc_length + 3);
					memcpy(p, direc_start, direc_length);
					p[direc_length + 1] = p[direc_length - 1];
					p[direc_length - 1] = 'l';
					p[direc_length] = 'l';
					//bb_error_msg("<%s>", p);
					direc_length += 2;
					direc_start = p;
				} else {
					p = NULL;
				}
				if (*argv) {
					print_direc(direc_start, direc_length, field_width,
								precision, *argv++);
				} else {
					print_direc(direc_start, direc_length, field_width,
								precision, "");
				}
				*conv_err |= errno;
				free(p);
			}
			break;
		case '\\':
			if (*++f == 'c') {
				return saved_argv; /* causes main() to exit */
			}
			bb_putchar(bb_process_escape_sequence((const char **)&f));
			f--;
			break;
		default:
			putchar(*f);
		}
	}

	return argv;
}

int printf_main(int argc UNUSED_PARAM, char **argv)
{
	int conv_err;
	char *format;
	char **argv2;

	/* We must check that stdout is not closed.
	 * The reason for this is highly non-obvious.
	 * printf_main is used from shell.
	 * Shell must correctly handle 'printf "%s" foo'
	 * if stdout is closed. With stdio, output gets shoveled into
	 * stdout buffer, and even fflush cannot clear it out. It seems that
	 * even if libc receives EBADF on write attempts, it feels determined
	 * to output data no matter what. So it will try later,
	 * and possibly will clobber future output. Not good. */
// TODO: check fcntl() & O_ACCMODE == O_WRONLY or O_RDWR?
	if (fcntl(1, F_GETFL) == -1)
		return 1; /* match coreutils 6.10 (sans error msg to stderr) */
	//if (dup2(1, 1) != 1) - old way
	//	return 1;

	/* bash builtin errors out on "printf '-%s-\n' foo",
	 * coreutils-6.9 works. Both work with "printf -- '-%s-\n' foo".
	 * We will mimic coreutils. */
	if (argv[1] && argv[1][0] == '-' && argv[1][1] == '-' && !argv[1][2])
		argv++;
	if (!argv[1]) {
		if (ENABLE_ASH_PRINTF
		 && applet_name[0] != 'p'
		) {
			bb_error_msg("usage: printf FORMAT [ARGUMENT...]");
			return 2; /* bash compat */
		}
		bb_show_usage();
	}

	format = argv[1];
	argv2 = argv + 2;

	conv_err = 0;
	do {
		argv = argv2;
		argv2 = print_formatted(format, argv, &conv_err);
	} while (argv2 > argv && *argv2);

	/* coreutils compat (bash doesn't do this):
	if (*argv)
		fprintf(stderr, "excess args ignored");
	*/

	return (argv2 < argv) /* if true, print_formatted errored out */
		|| conv_err; /* print_formatted saw invalid number */
}
