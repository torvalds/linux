/*
 * Copyright (c) 1997-2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "gen_locl.h"
#include <getarg.h>
#include "lex.h"

RCSID("$Id$");

extern FILE *yyin;

static getarg_strings preserve;
static getarg_strings seq;

int
preserve_type(const char *p)
{
    int i;
    for (i = 0; i < preserve.num_strings; i++)
	if (strcmp(preserve.strings[i], p) == 0)
	    return 1;
    return 0;
}

int
seq_type(const char *p)
{
    int i;
    for (i = 0; i < seq.num_strings; i++)
	if (strcmp(seq.strings[i], p) == 0)
	    return 1;
    return 0;
}

int support_ber;
int template_flag;
int rfc1510_bitstring;
int one_code_file;
char *option_file;
int version_flag;
int help_flag;
struct getargs args[] = {
    { "template", 0, arg_flag, &template_flag },
    { "encode-rfc1510-bit-string", 0, arg_flag, &rfc1510_bitstring },
    { "decode-dce-ber", 0, arg_flag, &support_ber },
    { "support-ber", 0, arg_flag, &support_ber },
    { "preserve-binary", 0, arg_strings, &preserve },
    { "sequence", 0, arg_strings, &seq },
    { "one-code-file", 0, arg_flag, &one_code_file },
    { "option-file", 0, arg_string, &option_file },
    { "version", 0, arg_flag, &version_flag },
    { "help", 0, arg_flag, &help_flag }
};
int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(int code)
{
    arg_printusage(args, num_args, NULL, "[asn1-file [name]]");
    exit(code);
}

int error_flag;

int
main(int argc, char **argv)
{
    int ret;
    const char *file;
    const char *name = NULL;
    int optidx = 0;
    char **arg = NULL;
    size_t len = 0, i;

    setprogname(argv[0]);
    if(getarg(args, num_args, argc, argv, &optidx))
	usage(1);
    if(help_flag)
	usage(0);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }
    if (argc == optidx) {
	file = "stdin";
	name = "stdin";
	yyin = stdin;
    } else {
	file = argv[optidx];
	yyin = fopen (file, "r");
	if (yyin == NULL)
	    err (1, "open %s", file);
	if (argc == optidx + 1) {
	    char *p;
	    name = estrdup(file);
	    p = strrchr(name, '.');
	    if (p)
		*p = '\0';
	} else
	    name = argv[optidx + 1];
    }

    /*
     * Parse extra options file
     */
    if (option_file) {
	char buf[1024];
	FILE *opt;

	opt = fopen(option_file, "r");
	if (opt == NULL) {
	    perror("open");
	    exit(1);
	}

	arg = calloc(2, sizeof(arg[0]));
	if (arg == NULL) {
	    perror("calloc");
	    exit(1);
	}
	arg[0] = option_file;
	arg[1] = NULL;
	len = 1;

	while (fgets(buf, sizeof(buf), opt) != NULL) {
	    buf[strcspn(buf, "\n\r")] = '\0';

	    arg = realloc(arg, (len + 2) * sizeof(arg[0]));
	    if (arg == NULL) {
		perror("malloc");
		exit(1);
	    }
	    arg[len] = strdup(buf);
	    if (arg[len] == NULL) {
		perror("strdup");
		exit(1);
	    }
	    arg[len + 1] = NULL;
	    len++;
	}
	fclose(opt);

	optidx = 0;
	if(getarg(args, num_args, len, arg, &optidx))
	    usage(1);

	if (len != optidx) {
	    fprintf(stderr, "extra args");
	    exit(1);
	}
    }


    init_generate (file, name);

    if (one_code_file)
	generate_header_of_codefile(name);

    initsym ();
    ret = yyparse ();
    if(ret != 0 || error_flag != 0)
	exit(1);
    close_generate ();
    if (argc != optidx)
	fclose(yyin);

    if (one_code_file)
	close_codefile();

    if (arg) {
	for (i = 1; i < len; i++)
	    free(arg[i]);
	free(arg);
    }

    return 0;
}
