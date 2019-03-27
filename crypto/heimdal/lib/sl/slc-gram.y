%{
/*
 * Copyright (c) 2004-2006 Kungliga Tekniska Högskolan
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
#include <err.h>
#include <ctype.h>
#include <limits.h>
#include <getarg.h>
#include <vers.h>
#include <roken.h>

#include "slc.h"
extern FILE *yyin;
extern struct assignment *assignment;
extern int yyparse(void);

/* Declarations for Bison:
 */
#define YYMALLOC        malloc
#define YYFREE          free

%}

%union {
	char *string;
	struct assignment *assignment;
}

%token <string> LITERAL
%token <string> STRING
%type <assignment> assignment assignments

%start start

%%

start		: assignments
		{
			assignment = $1;
		}
		;

assignments	: assignment assignments
		{
			$1->next = $2;
			$$ = $1;
		}
		| assignment
		;

assignment	: LITERAL '=' STRING
		{
			$$ = malloc(sizeof(*$$));
			$$->name = $1;
			$$->type = a_value;
			$$->lineno = lineno;
			$$->u.value = $3;
			$$->next = NULL;
		}
		| LITERAL '=' '{' assignments '}'
		{
			$$ = malloc(sizeof(*$$));
			$$->name = $1;
			$$->type = a_assignment;
			$$->lineno = lineno;
			$$->u.assignment = $4;
			$$->next = NULL;
		}
		;

%%
char *filename;
FILE *cfile, *hfile;
int error_flag;
struct assignment *assignment;


static void
ex(struct assignment *a, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s:%d: ", a->name, a->lineno);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}



static int
check_option(struct assignment *as)
{
    struct assignment *a;
    int seen_long = 0;
    int seen_name = 0;
    int seen_short = 0;
    int seen_type = 0;
    int seen_argument = 0;
    int seen_help = 0;
    int seen_default = 0;
    int ret = 0;

    for(a = as; a != NULL; a = a->next) {
	if(strcmp(a->name, "long") == 0)
	    seen_long++;
	else if(strcmp(a->name, "short") == 0)
	    seen_short++;
	else if(strcmp(a->name, "name") == 0)
	    seen_name++;
	else if(strcmp(a->name, "type") == 0)
	    seen_type++;
	else if(strcmp(a->name, "argument") == 0)
	    seen_argument++;
	else if(strcmp(a->name, "help") == 0)
	    seen_help++;
	else if(strcmp(a->name, "default") == 0)
	    seen_default++;
	else {
	    ex(a, "unknown name %s", a->name);
	    ret++;
	}
    }
    if(seen_long == 0 && seen_short == 0) {
	ex(as, "neither long nor short option");
	ret++;
    }
    if (seen_long == 0 && seen_name == 0) {
	ex(as, "either of long or name option must be used");
	ret++;
    }
    if(seen_long > 1) {
	ex(as, "multiple long options");
	ret++;
    }
    if(seen_short > 1) {
	ex(as, "multiple short options");
	ret++;
    }
    if(seen_type > 1) {
	ex(as, "multiple types");
	ret++;
    }
    if(seen_argument > 1) {
	ex(as, "multiple arguments");
	ret++;
    }
    if(seen_help > 1) {
	ex(as, "multiple help strings");
	ret++;
    }
    if(seen_default > 1) {
	ex(as, "multiple default values");
	ret++;
    }
    return ret;
}

static int
check_command(struct assignment *as)
{
	struct assignment *a;
	int seen_name = 0;
	int seen_function = 0;
	int seen_help = 0;
	int seen_argument = 0;
	int seen_minargs = 0;
	int seen_maxargs = 0;
	int ret = 0;
	for(a = as; a != NULL; a = a->next) {
		if(strcmp(a->name, "name") == 0)
			seen_name++;
		else if(strcmp(a->name, "function") == 0) {
			seen_function++;
		} else if(strcmp(a->name, "option") == 0)
			ret += check_option(a->u.assignment);
		else if(strcmp(a->name, "help") == 0) {
			seen_help++;
		} else if(strcmp(a->name, "argument") == 0) {
			seen_argument++;
		} else if(strcmp(a->name, "min_args") == 0) {
			seen_minargs++;
		} else if(strcmp(a->name, "max_args") == 0) {
			seen_maxargs++;
		} else {
			ex(a, "unknown name: %s", a->name);
			ret++;
		}
	}
	if(seen_name == 0) {
		ex(as, "no command name");
		ret++;
	}
	if(seen_function > 1) {
		ex(as, "multiple function names");
		ret++;
	}
	if(seen_help > 1) {
		ex(as, "multiple help strings");
		ret++;
	}
	if(seen_argument > 1) {
		ex(as, "multiple argument strings");
		ret++;
	}
	if(seen_minargs > 1) {
		ex(as, "multiple min_args strings");
		ret++;
	}
	if(seen_maxargs > 1) {
		ex(as, "multiple max_args strings");
		ret++;
	}

	return ret;
}

static int
check(struct assignment *as)
{
    struct assignment *a;
    int ret = 0;
    for(a = as; a != NULL; a = a->next) {
	if(strcmp(a->name, "command")) {
	    fprintf(stderr, "unknown type %s line %d\n", a->name, a->lineno);
	    ret++;
	    continue;
	}
	if(a->type != a_assignment) {
	    fprintf(stderr, "bad command definition %s line %d\n", a->name, a->lineno);
	    ret++;
	    continue;
	}
	ret += check_command(a->u.assignment);
    }
    return ret;
}

static struct assignment *
find_next(struct assignment *as, const char *name)
{
    for(as = as->next; as != NULL; as = as->next) {
	if(strcmp(as->name, name) == 0)
	    return as;
    }
    return NULL;
}

static struct assignment *
find(struct assignment *as, const char *name)
{
    for(; as != NULL; as = as->next) {
	if(strcmp(as->name, name) == 0)
	    return as;
    }
    return NULL;
}

static void
space(FILE *f, int level)
{
    fprintf(f, "%*.*s", level * 4, level * 4, " ");
}

static void
cprint(int level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    space(cfile, level);
    vfprintf(cfile, fmt, ap);
    va_end(ap);
}

static void
hprint(int level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    space(hfile, level);
    vfprintf(hfile, fmt, ap);
    va_end(ap);
}

static void gen_name(char *str);

static void
gen_command(struct assignment *as)
{
    struct assignment *a, *b;
    char *f;
    a = find(as, "name");
    f = strdup(a->u.value);
    gen_name(f);
    cprint(1, "    { ");
    fprintf(cfile, "\"%s\", ", a->u.value);
    fprintf(cfile, "%s_wrap, ", f);
    b = find(as, "argument");
    if(b)
	fprintf(cfile, "\"%s %s\", ", a->u.value, b->u.value);
    else
	fprintf(cfile, "\"%s\", ", a->u.value);
    b = find(as, "help");
    if(b)
	fprintf(cfile, "\"%s\"", b->u.value);
    else
	fprintf(cfile, "NULL");
    fprintf(cfile, " },\n");
    for(a = a->next; a != NULL; a = a->next)
	if(strcmp(a->name, "name") == 0)
	    cprint(1, "    { \"%s\" },\n", a->u.value);
    cprint(0, "\n");
}

static void
gen_name(char *str)
{
    char *p;
    for(p = str; *p != '\0'; p++)
	if(!isalnum((unsigned char)*p))
	    *p = '_';
}

static char *
make_name(struct assignment *as)
{
    struct assignment *lopt;
    struct assignment *type;
    char *s;

    lopt = find(as, "long");
    if(lopt == NULL)
	lopt = find(as, "name");
    if(lopt == NULL)
	return NULL;

    type = find(as, "type");
    if(strcmp(type->u.value, "-flag") == 0)
	asprintf(&s, "%s_flag", lopt->u.value);
    else
	asprintf(&s, "%s_%s", lopt->u.value, type->u.value);
    gen_name(s);
    return s;
}


static void defval_int(const char *name, struct assignment *defval)
{
    if(defval != NULL)
	cprint(1, "opt.%s = %s;\n", name, defval->u.value);
    else
	cprint(1, "opt.%s = 0;\n", name);
}
static void defval_neg_flag(const char *name, struct assignment *defval)
{
    if(defval != NULL)
	cprint(1, "opt.%s = %s;\n", name, defval->u.value);
    else
	cprint(1, "opt.%s = 1;\n", name);
}
static void defval_string(const char *name, struct assignment *defval)
{
    if(defval != NULL)
	cprint(1, "opt.%s = (char *)(unsigned long)\"%s\";\n", name, defval->u.value);
    else
	cprint(1, "opt.%s = NULL;\n", name);
}
static void defval_strings(const char *name, struct assignment *defval)
{
    cprint(1, "opt.%s.num_strings = 0;\n", name);
    cprint(1, "opt.%s.strings = NULL;\n", name);
}

static void free_strings(const char *name)
{
    cprint(1, "free_getarg_strings (&opt.%s);\n", name);
}

struct type_handler {
    const char *typename;
    const char *c_type;
    const char *getarg_type;
    void (*defval)(const char*, struct assignment*);
    void (*free)(const char*);
} type_handlers[] = {
	{ "integer",
	  "int",
	  "arg_integer",
	  defval_int,
	  NULL
	},
	{ "string",
	  "char*",
	  "arg_string",
	  defval_string,
	  NULL
	},
	{ "strings",
	  "struct getarg_strings",
	  "arg_strings",
	  defval_strings,
	  free_strings
	},
	{ "flag",
	  "int",
	  "arg_flag",
	  defval_int,
	  NULL
	},
	{ "-flag",
	  "int",
	  "arg_negative_flag",
	  defval_neg_flag,
	  NULL
	},
	{ NULL }
};

static struct type_handler *find_handler(struct assignment *type)
{
    struct type_handler *th;
    for(th = type_handlers; th->typename != NULL; th++)
	if(strcmp(type->u.value, th->typename) == 0)
	    return th;
    ex(type, "unknown type \"%s\"", type->u.value);
    exit(1);
}

static void
gen_options(struct assignment *opt1, const char *name)
{
    struct assignment *tmp;

    hprint(0, "struct %s_options {\n", name);

    for(tmp = opt1;
	tmp != NULL;
	tmp = find_next(tmp, "option")) {
	struct assignment *type;
	struct type_handler *th;
	char *s;

	s = make_name(tmp->u.assignment);
	type = find(tmp->u.assignment, "type");
	th = find_handler(type);
	hprint(1, "%s %s;\n", th->c_type, s);
	free(s);
    }
    hprint(0, "};\n");
}

static void
gen_wrapper(struct assignment *as)
{
    struct assignment *name;
    struct assignment *arg;
    struct assignment *opt1;
    struct assignment *function;
    struct assignment *tmp;
    char *n, *f;
    int nargs = 0;
    int narguments = 0;

    name = find(as, "name");
    n = strdup(name->u.value);
    gen_name(n);
    arg = find(as, "argument");
    if (arg)
        narguments++;
    opt1 = find(as, "option");
    function = find(as, "function");
    if(function)
	f = function->u.value;
    else
	f = n;


    if(opt1 != NULL) {
	gen_options(opt1, n);
	hprint(0, "int %s(struct %s_options*, int, char **);\n", f, n);
    } else {
	hprint(0, "int %s(void*, int, char **);\n", f);
    }

    fprintf(cfile, "static int\n");
    fprintf(cfile, "%s_wrap(int argc, char **argv)\n", n);
    fprintf(cfile, "{\n");
    if(opt1 != NULL)
	cprint(1, "struct %s_options opt;\n", n);
    cprint(1, "int ret;\n");
    cprint(1, "int optidx = 0;\n");
    cprint(1, "struct getargs args[] = {\n");
    for(tmp = find(as, "option");
	tmp != NULL;
	tmp = find_next(tmp, "option")) {
	struct assignment *type = find(tmp->u.assignment, "type");
	struct assignment *lopt = find(tmp->u.assignment, "long");
	struct assignment *sopt = find(tmp->u.assignment, "short");
	struct assignment *aarg = find(tmp->u.assignment, "argument");
	struct assignment *help = find(tmp->u.assignment, "help");

	struct type_handler *th;

	cprint(2, "{ ");
	if(lopt)
	    fprintf(cfile, "\"%s\", ", lopt->u.value);
	else
	    fprintf(cfile, "NULL, ");
	if(sopt)
	    fprintf(cfile, "'%c', ", *sopt->u.value);
	else
	    fprintf(cfile, "0, ");
	th = find_handler(type);
	fprintf(cfile, "%s, ", th->getarg_type);
	fprintf(cfile, "NULL, ");
	if(help)
	    fprintf(cfile, "\"%s\", ", help->u.value);
	else
	    fprintf(cfile, "NULL, ");
	if(aarg) {
	    fprintf(cfile, "\"%s\"", aarg->u.value);
            narguments++;
	} else
	    fprintf(cfile, "NULL");
	fprintf(cfile, " },\n");
    }
    cprint(2, "{ \"help\", 'h', arg_flag, NULL, NULL, NULL }\n");
    cprint(1, "};\n");
    cprint(1, "int help_flag = 0;\n");

    for(tmp = find(as, "option");
	tmp != NULL;
	tmp = find_next(tmp, "option")) {
	char *s;
	struct assignment *type = find(tmp->u.assignment, "type");

	struct assignment *defval = find(tmp->u.assignment, "default");

	struct type_handler *th;

	s = make_name(tmp->u.assignment);
	th = find_handler(type);
	(*th->defval)(s, defval);
	free(s);
    }

    for(tmp = find(as, "option");
	tmp != NULL;
	tmp = find_next(tmp, "option")) {
	char *s;
	s = make_name(tmp->u.assignment);
	cprint(1, "args[%d].value = &opt.%s;\n", nargs++, s);
	free(s);
    }
    cprint(1, "args[%d].value = &help_flag;\n", nargs++);
    cprint(1, "if(getarg(args, %d, argc, argv, &optidx))\n", nargs);
    cprint(2, "goto usage;\n");

    {
	int min_args = -1;
	int max_args = -1;
	char *end;
	if(narguments == 0) {
	    max_args = 0;
	} else {
	    if((tmp = find(as, "min_args")) != NULL) {
		min_args = strtol(tmp->u.value, &end, 0);
		if(*end != '\0') {
		    ex(tmp, "min_args is not numeric");
		    exit(1);
		}
		if(min_args < 0) {
		    ex(tmp, "min_args must be non-negative");
		    exit(1);
		}
	    }
	    if((tmp = find(as, "max_args")) != NULL) {
		max_args = strtol(tmp->u.value, &end, 0);
		if(*end != '\0') {
		    ex(tmp, "max_args is not numeric");
		    exit(1);
		}
		if(max_args < 0) {
		    ex(tmp, "max_args must be non-negative");
		    exit(1);
		}
	    }
	}
	if(min_args != -1 || max_args != -1) {
	    if(min_args == max_args) {
		cprint(1, "if(argc - optidx != %d) {\n",
		       min_args);
		cprint(2, "fprintf(stderr, \"Need exactly %u parameters (%%u given).\\n\\n\", argc - optidx);\n", min_args);
		cprint(2, "goto usage;\n");
		cprint(1, "}\n");
	    } else {
		if(max_args != -1) {
		    cprint(1, "if(argc - optidx > %d) {\n", max_args);
		    cprint(2, "fprintf(stderr, \"Arguments given (%%u) are more than expected (%u).\\n\\n\", argc - optidx);\n", max_args);
		    cprint(2, "goto usage;\n");
		    cprint(1, "}\n");
		}
		if(min_args != -1) {
		    cprint(1, "if(argc - optidx < %d) {\n", min_args);
		    cprint(2, "fprintf(stderr, \"Arguments given (%%u) are less than expected (%u).\\n\\n\", argc - optidx);\n", min_args);
		    cprint(2, "goto usage;\n");
		    cprint(1, "}\n");
		}
	    }
	}
    }

    cprint(1, "if(help_flag)\n");
    cprint(2, "goto usage;\n");

    cprint(1, "ret = %s(%s, argc - optidx, argv + optidx);\n",
	   f, opt1 ? "&opt": "NULL");

    /* free allocated data */
    for(tmp = find(as, "option");
	tmp != NULL;
	tmp = find_next(tmp, "option")) {
	char *s;
	struct assignment *type = find(tmp->u.assignment, "type");
	struct type_handler *th;
	th = find_handler(type);
	if(th->free == NULL)
	    continue;
	s = make_name(tmp->u.assignment);
	(*th->free)(s);
	free(s);
    }
    cprint(1, "return ret;\n");

    cprint(0, "usage:\n");
    cprint(1, "arg_printusage (args, %d, \"%s\", \"%s\");\n", nargs,
	   name->u.value, arg ? arg->u.value : "");
    /* free allocated data */
    for(tmp = find(as, "option");
	tmp != NULL;
	tmp = find_next(tmp, "option")) {
	char *s;
	struct assignment *type = find(tmp->u.assignment, "type");
	struct type_handler *th;
	th = find_handler(type);
	if(th->free == NULL)
	    continue;
	s = make_name(tmp->u.assignment);
	(*th->free)(s);
	free(s);
    }
    cprint(1, "return 0;\n");
    cprint(0, "}\n");
    cprint(0, "\n");
}

char cname[PATH_MAX];
char hname[PATH_MAX];

static void
gen(struct assignment *as)
{
    struct assignment *a;
    cprint(0, "#include <stdio.h>\n");
    cprint(0, "#include <getarg.h>\n");
    cprint(0, "#include <sl.h>\n");
    cprint(0, "#include \"%s\"\n\n", hname);

    hprint(0, "#include <stdio.h>\n");
    hprint(0, "#include <sl.h>\n");
    hprint(0, "\n");


    for(a = as; a != NULL; a = a->next)
	gen_wrapper(a->u.assignment);

    cprint(0, "SL_cmd commands[] = {\n");
    for(a = as; a != NULL; a = a->next)
	gen_command(a->u.assignment);
    cprint(1, "{ NULL }\n");
    cprint(0, "};\n");

    hprint(0, "extern SL_cmd commands[];\n");
}

int version_flag;
int help_flag;
struct getargs args[] = {
    { "version", 0, arg_flag, &version_flag },
    { "help", 0, arg_flag, &help_flag }
};
int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(int code)
{
    arg_printusage(args, num_args, NULL, "command-table");
    exit(code);
}

int
main(int argc, char **argv)
{
    char *p;

    int optidx = 0;

    setprogname(argv[0]);
    if(getarg(args, num_args, argc, argv, &optidx))
	usage(1);
    if(help_flag)
	usage(0);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    if(argc == optidx)
	usage(1);

    filename = argv[optidx];
    yyin = fopen(filename, "r");
    if(yyin == NULL)
	err(1, "%s", filename);
    p = strrchr(filename, '/');
    if(p)
	strlcpy(cname, p + 1, sizeof(cname));
    else
	strlcpy(cname, filename, sizeof(cname));
    p = strrchr(cname, '.');
    if(p)
	*p = '\0';
    strlcpy(hname, cname, sizeof(hname));
    strlcat(cname, ".c", sizeof(cname));
    strlcat(hname, ".h", sizeof(hname));
    yyparse();
    if(error_flag)
	exit(1);
    if(check(assignment) == 0) {
	cfile = fopen(cname, "w");
	if(cfile == NULL)
	  err(1, "%s", cname);
	hfile = fopen(hname, "w");
	if(hfile == NULL)
	  err(1, "%s", hname);
	gen(assignment);
	fclose(cfile);
	fclose(hfile);
    }
    fclose(yyin);
    return 0;
}
