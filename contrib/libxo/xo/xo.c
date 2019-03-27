/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "xo_config.h"
#include "xo.h"

#include <getopt.h>		/* Include after xo.h for testing */

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif /* UNUSED */

static int opt_warn;		/* Enable warnings */

static char **save_argv;
static char **checkpoint_argv;

static char *
next_arg (void)
{
    char *cp = *save_argv;

    if (cp == NULL)
	xo_errx(1, "missing argument");

    save_argv += 1;
    return cp;
}

static void
prep_arg (char *fmt)
{
    char *cp, *fp;

    for (cp = fp = fmt; *cp; cp++, fp++) {
	if (*cp != '\\') {
	    if (cp != fp)
		*fp = *cp;
	    continue;
	}

	switch (*++cp) {
	case 'n':
	    *fp = '\n';
	    break;

	case 'r':
	    *fp = '\r';
	    break;

	case 'b':
	    *fp = '\b';
	    break;

	case 'e':
	    *fp = '\e';
	    break;

	default:
	    *fp = *cp;
	}
    }

    *fp = '\0';
}

static void
checkpoint (xo_handle_t *xop UNUSED, va_list vap UNUSED, int restore)
{
    if (restore)
	save_argv = checkpoint_argv;
    else
	checkpoint_argv = save_argv;
}

/*
 * Our custom formatter is responsible for combining format string pieces
 * with our command line arguments to build strings.  This involves faking
 * some printf-style logic.
 */
static xo_ssize_t
formatter (xo_handle_t *xop, char *buf, xo_ssize_t bufsiz,
	   const char *fmt, va_list vap UNUSED)
{
    int lflag UNUSED = 0;	/* Parse long flag, though currently ignored */
    int hflag = 0, jflag = 0, tflag = 0,
	zflag = 0, qflag = 0, star1 = 0, star2 = 0;
    int rc = 0;
    int w1 = 0, w2 = 0;
    const char *cp;

    for (cp = fmt + 1; *cp; cp++) {
	if (*cp == 'l')
	    lflag += 1;
	else if (*cp == 'h')
	    hflag += 1;
	else if (*cp == 'j')
	    jflag += 1;
	else if (*cp == 't')
	    tflag += 1;
	else if (*cp == 'z')
	    zflag += 1;
	else if (*cp == 'q')
	    qflag += 1;
	else if (*cp == '*') {
	    if (star1 == 0)
		star1 = 1;
	    else
		star2 = 1;
	} else if (strchr("diouxXDOUeEfFgGaAcCsSp", *cp) != NULL)
	    break;
	else if (*cp == 'n' || *cp == 'v') {
	    if (opt_warn)
		xo_error_h(xop, "unsupported format: '%s'", fmt);
	    return -1;
	}
    }

    char fc = *cp;

    /* Handle "%*.*s" */
    if (star1)
	w1 = strtol(next_arg(), NULL, 0);
    if (star2 > 1)
	w2 = strtol(next_arg(), NULL, 0);

    if (fc == 'D' || fc == 'O' || fc == 'U')
	lflag = 1;

    if (strchr("diD", fc) != NULL) {
	long long value = strtoll(next_arg(), NULL, 0);
	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);

    } else if (strchr("ouxXOUp", fc) != NULL) {
	unsigned long long value = strtoull(next_arg(), NULL, 0);
	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);

    } else if (strchr("eEfFgGaA", fc) != NULL) {
	double value = strtold(next_arg(), NULL);
	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);

    } else if (fc == 'C' || fc == 'c' || fc == 'S' || fc == 's') {
	char *value = next_arg();
	if (star1 && star2)
	    rc = snprintf(buf, bufsiz, fmt, w1, w2, value);
	else if (star1)
	    rc = snprintf(buf, bufsiz, fmt, w1, value);
	else
	    rc = snprintf(buf, bufsiz, fmt, value);
    }

    return rc;
}

static void
print_version (void)
{
    fprintf(stderr, "libxo version %s%s\n",
	    xo_version, xo_version_extra);
    fprintf(stderr, "xo version %s%s\n",
	    LIBXO_VERSION, LIBXO_VERSION_EXTRA);
}

static void
print_help (void)
{
    fprintf(stderr,
"Usage: xo [options] format [fields]\n"
"    --close <path>        Close tags for the given path\n"
"    --depth <num>         Set the depth for pretty printing\n"
"    --help                Display this help text\n"
"    --html OR -H          Generate HTML output\n"
"    --json OR -J          Generate JSON output\n"
"    --leading-xpath <path> OR -l <path> "
	    "Add a prefix to generated XPaths (HTML)\n"
"    --open <path>         Open tags for the given path\n"
"    --option <opts> -or -O <opts>  Give formatting options\n"
"    --pretty OR -p        Make 'pretty' output (add indent, newlines)\n"
"    --style <style> OR -s <style>  "
	    "Generate given style (xml, json, text, html)\n"
"    --text OR -T          Generate text output (the default style)\n"
"    --version             Display version information\n"
"    --warn OR -W          Display warnings in text on stderr\n"
"    --warn-xml            Display warnings in xml on stdout\n"
"    --wrap <path>         Wrap output in a set of containers\n"
"    --xml OR -X           Generate XML output\n"
"    --xpath               Add XPath data to HTML output\n");
}

static struct opts {
    int o_depth;
    int o_help;
    int o_not_first;
    int o_xpath;
    int o_version;
    int o_warn_xml;
    int o_wrap;
} opts;

static struct option long_opts[] = {
    { "close", required_argument, NULL, 'c' },
    { "depth", required_argument, &opts.o_depth, 1 },
    { "help", no_argument, &opts.o_help, 1 },
    { "html", no_argument, NULL, 'H' },
    { "json", no_argument, NULL, 'J' },
    { "leading-xpath", required_argument, NULL, 'l' },
    { "not-first", no_argument, &opts.o_not_first, 1 },
    { "open", required_argument, NULL, 'o' },
    { "option", required_argument, NULL, 'O' },
    { "pretty", no_argument, NULL, 'p' },
    { "style", required_argument, NULL, 's' },
    { "text", no_argument, NULL, 'T' },
    { "xml", no_argument, NULL, 'X' },
    { "xpath", no_argument, &opts.o_xpath, 1 },
    { "version", no_argument, &opts.o_version, 1 },
    { "warn", no_argument, NULL, 'W' },
    { "warn-xml", no_argument, &opts.o_warn_xml, 1 },
    { "wrap", required_argument, &opts.o_wrap, 1 },
    { NULL, 0, NULL, 0 }
};

int
main (int argc UNUSED, char **argv)
{
    char *fmt = NULL, *cp, *np;
    char *opt_opener = NULL, *opt_closer = NULL, *opt_wrapper = NULL;
    char *opt_options = NULL;
    int opt_depth = 0;
    int opt_not_first = 0;
    int rc;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    while ((rc = getopt_long(argc, argv, "c:HJl:O:o:ps:TXW",
				long_opts, NULL)) != -1) {
	switch (rc) {
	case 'c':
	    opt_closer = optarg;
	    xo_set_flags(NULL, XOF_IGNORE_CLOSE);
	    break;

	case 'H':
	    xo_set_style(NULL, XO_STYLE_HTML);
	    break;

	case 'J':
	    xo_set_style(NULL, XO_STYLE_JSON);
	    break;

	case 'l':
	    xo_set_leading_xpath(NULL, optarg);
	    break;

	case 'O':
	    opt_options = optarg;
	    break;

	case 'o':
	    opt_opener = optarg;
	    break;

	case 'p':
	    xo_set_flags(NULL, XOF_PRETTY);
	    break;

	case 's':
	    if (xo_set_style_name(NULL, optarg) < 0)
		xo_errx(1, "unknown style: %s", optarg);
	    break;

	case 'T':
	    xo_set_style(NULL, XO_STYLE_TEXT);
	    break;

	case 'X':
	    xo_set_style(NULL, XO_STYLE_XML);
	    break;

	case 'W':
	    opt_warn = 1;
	    xo_set_flags(NULL, XOF_WARN);
	    break;

	case ':':
	    xo_errx(1, "missing argument");
	    break;

	case 0:
	    if (opts.o_depth) {
		opt_depth = atoi(optarg);
		
	    } else if (opts.o_help) {
		print_help();
		return 1;

	    } else if (opts.o_not_first) {
		opt_not_first = 1;

	    } else if (opts.o_xpath) {
		xo_set_flags(NULL, XOF_XPATH);

	    } else if (opts.o_version) {
		print_version();
		return 0;

	    } else if (opts.o_warn_xml) {
		opt_warn = 1;
		xo_set_flags(NULL, XOF_WARN | XOF_WARN_XML);

	    } else if (opts.o_wrap) {
		opt_wrapper = optarg;

	    } else {
		print_help();
		return 1;
	    }

	    bzero(&opts, sizeof(opts)); /* Reset all the options */
	    break;

	default:
	    print_help();
	    return 1;
	}
    }

    argc -= optind;
    argv += optind;

    if (opt_options) {
	rc = xo_set_options(NULL, opt_options);
	if (rc < 0)
	    xo_errx(1, "invalid options: %s", opt_options);
    }

    xo_set_formatter(NULL, formatter, checkpoint);
    xo_set_flags(NULL, XOF_NO_VA_ARG | XOF_NO_TOP | XOF_NO_CLOSE);

    fmt = *argv++;
    if (opt_opener == NULL && opt_closer == NULL && fmt == NULL) {
	print_help();
	return 1;
    }

    if (opt_not_first)
	xo_set_flags(NULL, XOF_NOT_FIRST);

    if (opt_closer) {
	opt_depth += 1;
	for (cp = opt_closer; cp && *cp; cp = np) {
	    np = strchr(cp, '/');
	    if (np == NULL)
		break;
	    np += 1;
	    opt_depth += 1;
	}
    }

    if (opt_depth > 0)
	xo_set_depth(NULL, opt_depth);

    if (opt_opener) {
	for (cp = opt_opener; cp && *cp; cp = np) {
	    np = strchr(cp, '/');
	    if (np)
		*np = '\0';
	    xo_open_container(cp);
	    if (np)
		*np++ = '/';
	}
    }

    if (opt_wrapper) {
	for (cp = opt_wrapper; cp && *cp; cp = np) {
	    np = strchr(cp, '/');
	    if (np)
		*np = '\0';
	    xo_open_container(cp);
	    if (np)
		*np++ = '/';
	}
    }

    if (fmt && *fmt) {
	save_argv = argv;
	prep_arg(fmt);
	xo_emit(fmt);
    }

    while (opt_wrapper) {
	np = strrchr(opt_wrapper, '/');
	xo_close_container(np ? np + 1 : opt_wrapper);
	if (np)
	    *np = '\0';
	else
	    opt_wrapper = NULL;
    }

    while (opt_closer) {
	np = strrchr(opt_closer, '/');
	xo_close_container(np ? np + 1 : opt_closer);
	if (np)
	    *np = '\0';
	else
	    opt_closer = NULL;
    }

    xo_finish();

    return 0;
}
