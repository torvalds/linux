/*
 * Copyright (c) 2014, 2015, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2015
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/queue.h>

#include "xo_config.h"
#include "xo.h"

#include <getopt.h>		/* Include after xo.h for testing */

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif /* UNUSED */

static int opt_warn;		/* Enable warnings */
static int opt_numbers;		/* Number our fields */

typedef struct xopo_msg_s {
    TAILQ_ENTRY(xopo_msg_s) xm_link;
    char *xm_plural;		/* If plural, points to the second part */
    char xm_data[0];		/* Start of data */
} xopo_msg_t;

typedef TAILQ_HEAD(xopo_msg_list_s, xopo_msg_s) xopo_msg_list_t;

static xopo_msg_list_t field_list;

static void
xopo_msg_cb (const char *str, unsigned len, int plural)
{
    int sz = sizeof(xopo_msg_t) + len + 1;
    xopo_msg_t *xmp = malloc(sz);
    if (xmp == NULL)
	return;

    bzero(xmp, sz);
    memcpy(xmp->xm_data, str, len);
    xmp->xm_data[len] = '\0';

    if (plural) {
	char *cp = strchr(xmp->xm_data, ',');
	if (cp) {
	    *cp++ = '\0';
	    xmp->xm_plural = cp;
	}
    }

    xopo_msg_t *xmp2;

    TAILQ_FOREACH(xmp2, &field_list, xm_link) {
	if (strcmp(xmp->xm_data, xmp2->xm_data) == 0) {
	    /* Houston, we have a negative on that trajectory */
	    free(xmp);
	    return;
	}
    }

    TAILQ_INSERT_TAIL(&field_list, xmp, xm_link);
}

static void
print_version (void)
{
    fprintf(stderr, "libxo version %s%s\n",
	    xo_version, xo_version_extra);
    fprintf(stderr, "xopo version %s%s\n",
	    LIBXO_VERSION, LIBXO_VERSION_EXTRA);
}

static void
print_help (void)
{
    fprintf(stderr,
"Usage: xopo [options] format [fields]\n"
"    --help                Display this help text\n"
"    --option <opts> -or -O <opts> Give formatting options\n"
"    --output <file> -or -o <file> Use file as output destination\n"
"    --po <file> or -f <file> Generate new msgid's for a po file\n"
"    --simplify <text> OR -s <text> Show simplified form of the format string\n"
"    --version             Display version information\n"
"    --warn OR -W          Display warnings in text on stderr\n"
);
}

static struct opts {
    int o_help;
    int o_version;
} opts;

static struct option long_opts[] = {
    { "help", no_argument, &opts.o_help, 1 },
    { "number", no_argument, NULL, 'n' },
    { "option", required_argument, NULL, 'O' },
    { "output", required_argument, NULL, 'o' },
    { "po", required_argument, NULL, 'f' },
    { "simplify", no_argument, NULL, 'S' },
    { "warn", no_argument, NULL, 'W' },
    { NULL, 0, NULL, 0 }
};

int
main (int argc UNUSED, char **argv)
{
    char *opt_options = NULL;
    char *opt_input = NULL;
    char *opt_output = NULL;
    char *opt_simplify = NULL;
    int rc;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    while ((rc = getopt_long(argc, argv, "f:no:O:s:W",
				long_opts, NULL)) != -1) {
	switch (rc) {
	case 'f':
	    opt_input = optarg;
	    break;

	case 'n':
	    opt_numbers = 1;
	    break;

	case 'o':
	    opt_output = optarg;
	    break;

	case 'O':
	    opt_options = optarg;
	    break;

	case 's':
	    opt_simplify = optarg;
	    break;

	case 'W':
	    opt_warn = 1;
	    xo_set_flags(NULL, XOF_WARN);
	    break;

	case ':':
	    xo_errx(1, "missing argument");
	    break;

	case 0:
	    if (opts.o_help) {
		print_help();
		return 1;

	    } else if (opts.o_version) {
		print_version();
		return 0;

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

    if (opt_simplify) {
	char *fmt = xo_simplify_format(NULL, opt_simplify, opt_numbers, NULL);
	if (fmt) {
	    xo_emit("{:format}\n", fmt);
	    free(fmt);
	}
	exit(0);
    }

    static char msgid[] = "msgid ";
    char buf[BUFSIZ], *cp, *ep;
    FILE *infile;
    FILE *outfile;
    TAILQ_INIT(&field_list);

    if (opt_input) {
	infile = fopen(opt_input, "r");
	if (infile == NULL)
	    xo_emit_err(1, "count not open input file: '{:filename}'",
			opt_input);
    } else
	infile = stdin;

    if (opt_output) {
	unlink(opt_output);
	outfile = fopen(opt_output, "w");
	if (outfile == NULL)
	    xo_emit_err(1, "count not open output file: '{:filename}'",
			opt_output);
    } else
	outfile = stdout;

    int blank = 0, line;

    for (line = 1;; line++) {
	if (fgets(buf, sizeof(buf), infile) == NULL)
	    break;

	if (buf[0] == '#' && buf[1] == '\n')
	    continue;

	blank = (buf[0] == '\n' && buf[1] == '\0');

	if (strncmp(buf, msgid, sizeof(msgid) - 1) != 0) {
	    fprintf(outfile, "%s", buf);
	    continue;
	}

	for (cp = buf + sizeof(msgid); *cp; cp++)
	    if (!isspace((int) *cp))
		break;

	if (*cp == '"')
	    cp += 1;

	ep = cp + strlen(cp);
	if (ep > cp)
	    ep -= 1;

	while (isspace((int) *ep) && ep > cp)
	    ep -= 1;

	if (*ep != '"')
	    *ep += 1;

	*ep = '\0';

	cp = xo_simplify_format(NULL, cp, opt_numbers, xopo_msg_cb);
	if (cp) {
	    fprintf(outfile, "msgid \"%s\"\n", cp);
	    free(cp);
	}
    }

    if (!blank)
	fprintf(outfile, "\n");

    xopo_msg_t *xmp;
    TAILQ_FOREACH(xmp, &field_list, xm_link) {
	if (xmp->xm_plural) {
	    fprintf(outfile, "msgid \"%s\"\n"
		    "msgid_plural \"%s\"\n"
		    "msgstr[0] \"\"\n"
		    "msgstr[1] \"\"\n\n",
		    xmp->xm_data, xmp->xm_plural);
	} else {
	    fprintf(outfile, "msgid \"%s\"\nmsgstr \"\"\n\n", xmp->xm_data);
	}
    }

    if (infile != stdin)
	fclose(infile);
    if (outfile != stdout)
	fclose(outfile);

    xo_finish();

    return 0;
}
