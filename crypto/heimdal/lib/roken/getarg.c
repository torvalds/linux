/*
 * Copyright (c) 1997 - 2002 Kungliga Tekniska Högskolan
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roken.h"
#include "getarg.h"

#define ISFLAG(X) ((X).type == arg_flag || (X).type == arg_negative_flag)

static size_t
print_arg (char *string,
	   size_t len,
	   int mdoc,
	   int longp,
	   struct getargs *arg,
	   char *(i18n)(const char *))
{
    const char *s;

    *string = '\0';

    if (ISFLAG(*arg) || (!longp && arg->type == arg_counter))
	return 0;

    if(mdoc){
	if(longp)
	    strlcat(string, "= Ns", len);
	strlcat(string, " Ar ", len);
    } else {
	if (longp)
	    strlcat (string, "=", len);
	else
	    strlcat (string, " ", len);
    }

    if (arg->arg_help)
	s = (*i18n)(arg->arg_help);
    else if (arg->type == arg_integer || arg->type == arg_counter)
	s = "integer";
    else if (arg->type == arg_string)
	s = "string";
    else if (arg->type == arg_strings)
	s = "strings";
    else if (arg->type == arg_double)
	s = "float";
    else
	s = "<undefined>";

    strlcat(string, s, len);
    return 1 + strlen(s);
}

static void
mandoc_template(struct getargs *args,
		size_t num_args,
		const char *progname,
		const char *extra_string,
		char *(i18n)(const char *))
{
    size_t i;
    char timestr[64], cmd[64];
    char buf[128];
    const char *p;
    time_t t;

    printf(".\\\" Things to fix:\n");
    printf(".\\\"   * correct section, and operating system\n");
    printf(".\\\"   * remove Op from mandatory flags\n");
    printf(".\\\"   * use better macros for arguments (like .Pa for files)\n");
    printf(".\\\"\n");
    t = time(NULL);
    strftime(timestr, sizeof(timestr), "%B %e, %Y", localtime(&t));
    printf(".Dd %s\n", timestr);
    p = strrchr(progname, '/');
    if(p) p++; else p = progname;
    strlcpy(cmd, p, sizeof(cmd));
    strupr(cmd);

    printf(".Dt %s SECTION\n", cmd);
    printf(".Os OPERATING_SYSTEM\n");
    printf(".Sh NAME\n");
    printf(".Nm %s\n", p);
    printf(".Nd in search of a description\n");
    printf(".Sh SYNOPSIS\n");
    printf(".Nm\n");
    for(i = 0; i < num_args; i++){
	/* we seem to hit a limit on number of arguments if doing
           short and long flags with arguments -- split on two lines */
	if(ISFLAG(args[i]) ||
	   args[i].short_name == 0 || args[i].long_name == NULL) {
	    printf(".Op ");

	    if(args[i].short_name) {
		print_arg(buf, sizeof(buf), 1, 0, args + i, i18n);
		printf("Fl %c%s", args[i].short_name, buf);
		if(args[i].long_name)
		    printf(" | ");
	    }
	    if(args[i].long_name) {
		print_arg(buf, sizeof(buf), 1, 1, args + i, i18n);
		printf("Fl Fl %s%s%s",
		       args[i].type == arg_negative_flag ? "no-" : "",
		       args[i].long_name, buf);
	    }
	    printf("\n");
	} else {
	    print_arg(buf, sizeof(buf), 1, 0, args + i, i18n);
	    printf(".Oo Fl %c%s \\*(Ba Xo\n", args[i].short_name, buf);
	    print_arg(buf, sizeof(buf), 1, 1, args + i, i18n);
	    printf(".Fl Fl %s%s\n.Xc\n.Oc\n", args[i].long_name, buf);
	}
    /*
	    if(args[i].type == arg_strings)
		fprintf (stderr, "...");
		*/
    }
    if (extra_string && *extra_string)
	printf (".Ar %s\n", extra_string);
    printf(".Sh DESCRIPTION\n");
    printf("Supported options:\n");
    printf(".Bl -tag -width Ds\n");
    for(i = 0; i < num_args; i++){
	printf(".It Xo\n");
	if(args[i].short_name){
	    printf(".Fl %c", args[i].short_name);
	    print_arg(buf, sizeof(buf), 1, 0, args + i, i18n);
	    printf("%s", buf);
	    if(args[i].long_name)
		printf(" ,");
	    printf("\n");
	}
	if(args[i].long_name){
	    printf(".Fl Fl %s%s",
		   args[i].type == arg_negative_flag ? "no-" : "",
		   args[i].long_name);
	    print_arg(buf, sizeof(buf), 1, 1, args + i, i18n);
	    printf("%s\n", buf);
	}
	printf(".Xc\n");
	if(args[i].help)
	    printf("%s\n", args[i].help);
    /*
	    if(args[i].type == arg_strings)
		fprintf (stderr, "...");
		*/
    }
    printf(".El\n");
    printf(".\\\".Sh ENVIRONMENT\n");
    printf(".\\\".Sh FILES\n");
    printf(".\\\".Sh EXAMPLES\n");
    printf(".\\\".Sh DIAGNOSTICS\n");
    printf(".\\\".Sh SEE ALSO\n");
    printf(".\\\".Sh STANDARDS\n");
    printf(".\\\".Sh HISTORY\n");
    printf(".\\\".Sh AUTHORS\n");
    printf(".\\\".Sh BUGS\n");
}

static int
check_column(FILE *f, int col, int len, int columns)
{
    if(col + len > columns) {
	fprintf(f, "\n");
	col = fprintf(f, "  ");
    }
    return col;
}

static char *
builtin_i18n(const char *str)
{
    return rk_UNCONST(str);
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
arg_printusage (struct getargs *args,
		size_t num_args,
		const char *progname,
		const char *extra_string)
{
    arg_printusage_i18n(args, num_args, "Usage",
			progname, extra_string, builtin_i18n);
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
arg_printusage_i18n (struct getargs *args,
		     size_t num_args,
		     const char *usage,
		     const char *progname,
		     const char *extra_string,
		     char *(*i18n)(const char *))
{
    size_t i, max_len = 0;
    char buf[128];
    int col = 0, columns;

    if (progname == NULL)
	progname = getprogname();

    if (i18n == NULL)
	i18n = builtin_i18n;

    if(getenv("GETARGMANDOC")){
	mandoc_template(args, num_args, progname, extra_string, i18n);
	return;
    }
    if(get_window_size(2, NULL, &columns) == -1)
	columns = 80;
    col = 0;
    col += fprintf (stderr, "%s: %s", usage, progname);
    buf[0] = '\0';
    for (i = 0; i < num_args; ++i) {
	if(args[i].short_name && ISFLAG(args[i])) {
	    char s[2];
	    if(buf[0] == '\0')
		strlcpy(buf, "[-", sizeof(buf));
	    s[0] = args[i].short_name;
	    s[1] = '\0';
	    strlcat(buf, s, sizeof(buf));
	}
    }
    if(buf[0] != '\0') {
	strlcat(buf, "]", sizeof(buf));
	col = check_column(stderr, col, strlen(buf) + 1, columns);
	col += fprintf(stderr, " %s", buf);
    }

    for (i = 0; i < num_args; ++i) {
	size_t len = 0;

	if (args[i].long_name) {
	    buf[0] = '\0';
	    strlcat(buf, "[--", sizeof(buf));
	    len += 2;
	    if(args[i].type == arg_negative_flag) {
		strlcat(buf, "no-", sizeof(buf));
		len += 3;
	    }
	    strlcat(buf, args[i].long_name, sizeof(buf));
	    len += strlen(args[i].long_name);
	    len += print_arg(buf + strlen(buf), sizeof(buf) - strlen(buf),
			     0, 1, &args[i], i18n);
	    strlcat(buf, "]", sizeof(buf));
	    if(args[i].type == arg_strings)
		strlcat(buf, "...", sizeof(buf));
	    col = check_column(stderr, col, strlen(buf) + 1, columns);
	    col += fprintf(stderr, " %s", buf);
	}
	if (args[i].short_name && !ISFLAG(args[i])) {
	    snprintf(buf, sizeof(buf), "[-%c", args[i].short_name);
	    len += 2;
	    len += print_arg(buf + strlen(buf), sizeof(buf) - strlen(buf),
			     0, 0, &args[i], i18n);
	    strlcat(buf, "]", sizeof(buf));
	    if(args[i].type == arg_strings)
		strlcat(buf, "...", sizeof(buf));
	    col = check_column(stderr, col, strlen(buf) + 1, columns);
	    col += fprintf(stderr, " %s", buf);
	}
	if (args[i].long_name && args[i].short_name)
	    len += 2; /* ", " */
	max_len = max(max_len, len);
    }
    if (extra_string) {
	check_column(stderr, col, strlen(extra_string) + 1, columns);
	fprintf (stderr, " %s\n", extra_string);
    } else
	fprintf (stderr, "\n");
    for (i = 0; i < num_args; ++i) {
	if (args[i].help) {
	    size_t count = 0;

	    if (args[i].short_name) {
		count += fprintf (stderr, "-%c", args[i].short_name);
		print_arg (buf, sizeof(buf), 0, 0, &args[i], i18n);
		count += fprintf(stderr, "%s", buf);
	    }
	    if (args[i].short_name && args[i].long_name)
		count += fprintf (stderr, ", ");
	    if (args[i].long_name) {
		count += fprintf (stderr, "--");
		if (args[i].type == arg_negative_flag)
		    count += fprintf (stderr, "no-");
		count += fprintf (stderr, "%s", args[i].long_name);
		print_arg (buf, sizeof(buf), 0, 1, &args[i], i18n);
		count += fprintf(stderr, "%s", buf);
	    }
	    while(count++ <= max_len)
		putc (' ', stderr);
	    fprintf (stderr, "%s\n", (*i18n)(args[i].help));
	}
    }
}

static int
add_string(getarg_strings *s, char *value)
{
    char **strings;

    strings = realloc(s->strings, (s->num_strings + 1) * sizeof(*s->strings));
    if (strings == NULL) {
	free(s->strings);
	s->strings = NULL;
	s->num_strings = 0;
	return ENOMEM;
    }
    s->strings = strings;
    s->strings[s->num_strings] = value;
    s->num_strings++;
    return 0;
}

static int
arg_match_long(struct getargs *args, size_t num_args,
	       char *argv, int argc, char **rargv, int *goptind)
{
    size_t i;
    char *goptarg = NULL;
    int negate = 0;
    int partial_match = 0;
    struct getargs *partial = NULL;
    struct getargs *current = NULL;
    int argv_len;
    char *p;
    int p_len;

    argv_len = strlen(argv);
    p = strchr (argv, '=');
    if (p != NULL)
	argv_len = p - argv;

    for (i = 0; i < num_args; ++i) {
	if(args[i].long_name) {
	    int len = strlen(args[i].long_name);
	    p = argv;
	    p_len = argv_len;
	    negate = 0;

	    for (;;) {
		if (strncmp (args[i].long_name, p, p_len) == 0) {
		    if(p_len == len)
			current = &args[i];
		    else {
			++partial_match;
			partial = &args[i];
		    }
		    goptarg  = p + p_len;
		} else if (ISFLAG(args[i]) && strncmp (p, "no-", 3) == 0) {
		    negate = !negate;
		    p += 3;
		    p_len -= 3;
		    continue;
		}
		break;
	    }
	    if (current)
		break;
	}
    }
    if (current == NULL) {
	if (partial_match == 1)
	    current = partial;
	else
	    return ARG_ERR_NO_MATCH;
    }

    if(*goptarg == '\0'
       && !ISFLAG(*current)
       && current->type != arg_collect
       && current->type != arg_counter)
	return ARG_ERR_NO_MATCH;
    switch(current->type){
    case arg_integer:
    {
	int tmp;
	if(sscanf(goptarg + 1, "%d", &tmp) != 1)
	    return ARG_ERR_BAD_ARG;
	*(int*)current->value = tmp;
	return 0;
    }
    case arg_string:
    {
	*(char**)current->value = goptarg + 1;
	return 0;
    }
    case arg_strings:
    {
	return add_string((getarg_strings*)current->value, goptarg + 1);
    }
    case arg_flag:
    case arg_negative_flag:
    {
	int *flag = current->value;
	if(*goptarg == '\0' ||
	   strcmp(goptarg + 1, "yes") == 0 ||
	   strcmp(goptarg + 1, "true") == 0){
	    *flag = !negate;
	    return 0;
	} else if (*goptarg && strcmp(goptarg + 1, "maybe") == 0) {
	    *flag = rk_random() & 1;
	} else {
	    *flag = negate;
	    return 0;
	}
	return ARG_ERR_BAD_ARG;
    }
    case arg_counter :
    {
	int val;

	if (*goptarg == '\0')
	    val = 1;
	else if(sscanf(goptarg + 1, "%d", &val) != 1)
	    return ARG_ERR_BAD_ARG;
	*(int *)current->value += val;
	return 0;
    }
    case arg_double:
    {
	double tmp;
	if(sscanf(goptarg + 1, "%lf", &tmp) != 1)
	    return ARG_ERR_BAD_ARG;
	*(double*)current->value = tmp;
	return 0;
    }
    case arg_collect:{
	struct getarg_collect_info *c = current->value;
	int o = argv - rargv[*goptind];
	return (*c->func)(FALSE, argc, rargv, goptind, &o, c->data);
    }

    default:
	abort ();
	UNREACHABLE(return 0);
    }
}

static int
arg_match_short (struct getargs *args, size_t num_args,
		 char *argv, int argc, char **rargv, int *goptind)
{
    size_t j, k;

    for(j = 1; j > 0 && j < strlen(rargv[*goptind]); j++) {
	for(k = 0; k < num_args; k++) {
	    char *goptarg;

	    if(args[k].short_name == 0)
		continue;
	    if(argv[j] == args[k].short_name) {
		if(args[k].type == arg_flag) {
		    *(int*)args[k].value = 1;
		    break;
		}
		if(args[k].type == arg_negative_flag) {
		    *(int*)args[k].value = 0;
		    break;
		}
		if(args[k].type == arg_counter) {
		    ++*(int *)args[k].value;
		    break;
		}
		if(args[k].type == arg_collect) {
		    struct getarg_collect_info *c = args[k].value;
		    int a = (int)j;

		    if((*c->func)(TRUE, argc, rargv, goptind, &a, c->data))
			return ARG_ERR_BAD_ARG;
		    j = a;
		    break;
		}

		if(argv[j + 1])
		    goptarg = &argv[j + 1];
		else {
		    ++*goptind;
		    goptarg = rargv[*goptind];
		}
		if(goptarg == NULL) {
		    --*goptind;
		    return ARG_ERR_NO_ARG;
		}
		if(args[k].type == arg_integer) {
		    int tmp;
		    if(sscanf(goptarg, "%d", &tmp) != 1)
			return ARG_ERR_BAD_ARG;
		    *(int*)args[k].value = tmp;
		    return 0;
		} else if(args[k].type == arg_string) {
		    *(char**)args[k].value = goptarg;
		    return 0;
		} else if(args[k].type == arg_strings) {
		    return add_string((getarg_strings*)args[k].value, goptarg);
		} else if(args[k].type == arg_double) {
		    double tmp;
		    if(sscanf(goptarg, "%lf", &tmp) != 1)
			return ARG_ERR_BAD_ARG;
		    *(double*)args[k].value = tmp;
		    return 0;
		}
		return ARG_ERR_BAD_ARG;
	    }
	}
	if (k == num_args)
	    return ARG_ERR_NO_MATCH;
    }
    return 0;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
getarg(struct getargs *args, size_t num_args,
       int argc, char **argv, int *goptind)
{
    int i;
    int ret = 0;

    rk_random_init();
    (*goptind)++;
    for(i = *goptind; i < argc; i++) {
	if(argv[i][0] != '-')
	    break;
	if(argv[i][1] == '-'){
	    if(argv[i][2] == 0){
		i++;
		break;
	    }
	    ret = arg_match_long (args, num_args, argv[i] + 2,
				  argc, argv, &i);
	} else {
	    ret = arg_match_short (args, num_args, argv[i],
				   argc, argv, &i);
	}
	if(ret)
	    break;
    }
    *goptind = i;
    return ret;
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
free_getarg_strings (getarg_strings *s)
{
    free (s->strings);
}

#if TEST
int foo_flag = 2;
int flag1 = 0;
int flag2 = 0;
int bar_int;
char *baz_string;

struct getargs args[] = {
    { NULL, '1', arg_flag, &flag1, "one", NULL },
    { NULL, '2', arg_flag, &flag2, "two", NULL },
    { "foo", 'f', arg_negative_flag, &foo_flag, "foo", NULL },
    { "bar", 'b', arg_integer, &bar_int, "bar", "seconds"},
    { "baz", 'x', arg_string, &baz_string, "baz", "name" },
};

int main(int argc, char **argv)
{
    int goptind = 0;
    while(getarg(args, 5, argc, argv, &goptind))
	printf("Bad arg: %s\n", argv[goptind]);
    printf("flag1 = %d\n", flag1);
    printf("flag2 = %d\n", flag2);
    printf("foo_flag = %d\n", foo_flag);
    printf("bar_int = %d\n", bar_int);
    printf("baz_flag = %s\n", baz_string);
    arg_printusage (args, 5, argv[0], "nothing here");
}
#endif
