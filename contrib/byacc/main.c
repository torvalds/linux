/* $Id: main.c,v 1.60 2017/04/30 20:57:56 Julien.Ramseier Exp $ */

#include <signal.h>
#ifndef _WIN32
#include <unistd.h>		/* for _exit() */
#else
#include <stdlib.h>		/* for _exit() */
#endif

#include "defs.h"

#ifdef HAVE_MKSTEMP
# define USE_MKSTEMP 1
#elif defined(HAVE_FCNTL_H)
# define USE_MKSTEMP 1
# include <fcntl.h>		/* for open(), O_EXCL, etc. */
#else
# define USE_MKSTEMP 0
#endif

#if USE_MKSTEMP
#include <sys/types.h>
#include <sys/stat.h>

typedef struct _my_tmpfiles
{
    struct _my_tmpfiles *next;
    char *name;
}
MY_TMPFILES;

static MY_TMPFILES *my_tmpfiles;
#endif /* USE_MKSTEMP */

char dflag;
char gflag;
char iflag;
char lflag;
static char oflag;
char rflag;
char sflag;
char tflag;
char vflag;

const char *symbol_prefix;
const char *myname = "yacc";

int lineno;
int outline;

static char default_file_prefix[] = "y";

static char *file_prefix = default_file_prefix;

char *code_file_name;
char *input_file_name;
size_t input_file_name_len = 0;
char *defines_file_name;
char *externs_file_name;

static char *graph_file_name;
static char *output_file_name;
static char *verbose_file_name;

FILE *action_file;	/*  a temp file, used to save actions associated    */
			/*  with rules until the parser is written          */
FILE *code_file;	/*  y.code.c (used when the -r option is specified) */
FILE *defines_file;	/*  y.tab.h                                         */
FILE *externs_file;	/*  y.tab.i                                         */
FILE *input_file;	/*  the input file                                  */
FILE *output_file;	/*  y.tab.c                                         */
FILE *text_file;	/*  a temp file, used to save text until all        */
			/*  symbols have been defined                       */
FILE *union_file;	/*  a temp file, used to save the union             */
			/*  definition until all symbol have been           */
			/*  defined                                         */
FILE *verbose_file;	/*  y.output                                        */
FILE *graph_file;	/*  y.dot                                           */

Value_t nitems;
Value_t nrules;
Value_t nsyms;
Value_t ntokens;
Value_t nvars;

Value_t start_symbol;
char **symbol_name;
char **symbol_pname;
Value_t *symbol_value;
Value_t *symbol_prec;
char *symbol_assoc;

int pure_parser;
int token_table;
int error_verbose;

#if defined(YYBTYACC)
Value_t *symbol_pval;
char **symbol_destructor;
char **symbol_type_tag;
int locations = 0;	/* default to no position processing */
int backtrack = 0;	/* default is no backtracking */
char *initial_action = NULL;
#endif

int exit_code;

Value_t *ritem;
Value_t *rlhs;
Value_t *rrhs;
Value_t *rprec;
Assoc_t *rassoc;
Value_t **derives;
char *nullable;

/*
 * Since fclose() is called via the signal handler, it might die.  Don't loop
 * if there is a problem closing a file.
 */
#define DO_CLOSE(fp) \
	if (fp != 0) { \
	    FILE *use = fp; \
	    fp = 0; \
	    fclose(use); \
	}

static int got_intr = 0;

void
done(int k)
{
    DO_CLOSE(input_file);
    DO_CLOSE(output_file);
    if (iflag)
	DO_CLOSE(externs_file);
    if (rflag)
	DO_CLOSE(code_file);

    DO_CLOSE(action_file);
    DO_CLOSE(defines_file);
    DO_CLOSE(graph_file);
    DO_CLOSE(text_file);
    DO_CLOSE(union_file);
    DO_CLOSE(verbose_file);

    if (got_intr)
	_exit(EXIT_FAILURE);

#ifdef NO_LEAKS
    if (rflag)
	DO_FREE(code_file_name);

    if (dflag)
	DO_FREE(defines_file_name);

    if (iflag)
	DO_FREE(externs_file_name);

    if (oflag)
	DO_FREE(output_file_name);

    if (vflag)
	DO_FREE(verbose_file_name);

    if (gflag)
	DO_FREE(graph_file_name);

    lr0_leaks();
    lalr_leaks();
    mkpar_leaks();
    mstring_leaks();
    output_leaks();
    reader_leaks();
#endif

    exit(k);
}

static void
onintr(int sig GCC_UNUSED)
{
    got_intr = 1;
    done(EXIT_FAILURE);
}

static void
set_signals(void)
{
#ifdef SIGINT
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
	signal(SIGINT, onintr);
#endif
#ifdef SIGTERM
    if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
	signal(SIGTERM, onintr);
#endif
#ifdef SIGHUP
    if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
	signal(SIGHUP, onintr);
#endif
}

static void
usage(void)
{
    static const char *msg[] =
    {
	""
	,"Options:"
	,"  -b file_prefix        set filename prefix (default \"y.\")"
	,"  -B                    create a backtracking parser"
	,"  -d                    write definitions (" DEFINES_SUFFIX ")"
	,"  -i                    write interface (y.tab.i)"
	,"  -g                    write a graphical description"
	,"  -l                    suppress #line directives"
	,"  -L                    enable position processing, e.g., \"%locations\""
	,"  -o output_file        (default \"" OUTPUT_SUFFIX "\")"
	,"  -p symbol_prefix      set symbol prefix (default \"yy\")"
	,"  -P                    create a reentrant parser, e.g., \"%pure-parser\""
	,"  -r                    produce separate code and table files (y.code.c)"
	,"  -s                    suppress #define's for quoted names in %token lines"
	,"  -t                    add debugging support"
	,"  -v                    write description (y.output)"
	,"  -V                    show version information and exit"
    };
    unsigned n;

    fflush(stdout);
    fprintf(stderr, "Usage: %s [options] filename\n", myname);
    for (n = 0; n < sizeof(msg) / sizeof(msg[0]); ++n)
	fprintf(stderr, "%s\n", msg[n]);

    exit(1);
}

static void
setflag(int ch)
{
    switch (ch)
    {
    case 'B':
#if defined(YYBTYACC)
	backtrack = 1;
#else
	unsupported_flag_warning("-B", "reconfigure with --enable-btyacc");
#endif
	break;

    case 'd':
	dflag = 1;
	break;

    case 'g':
	gflag = 1;
	break;

    case 'i':
	iflag = 1;
	break;

    case 'l':
	lflag = 1;
	break;

    case 'L':
#if defined(YYBTYACC)
	locations = 1;
#else
	unsupported_flag_warning("-L", "reconfigure with --enable-btyacc");
#endif
	break;

    case 'P':
	pure_parser = 1;
	break;

    case 'r':
	rflag = 1;
	break;

    case 's':
	sflag = 1;
	break;

    case 't':
	tflag = 1;
	break;

    case 'v':
	vflag = 1;
	break;

    case 'V':
	printf("%s - %s\n", myname, VERSION);
	exit(EXIT_SUCCESS);

    case 'y':
	/* noop for bison compatibility. byacc is already designed to be posix
	 * yacc compatible. */
	break;

    default:
	usage();
    }
}

static void
getargs(int argc, char *argv[])
{
    int i;
    char *s;
    int ch;

    if (argc > 0)
	myname = argv[0];

    for (i = 1; i < argc; ++i)
    {
	s = argv[i];
	if (*s != '-')
	    break;
	switch (ch = *++s)
	{
	case '\0':
	    input_file = stdin;
	    if (i + 1 < argc)
		usage();
	    return;

	case '-':
	    ++i;
	    goto no_more_options;

	case 'b':
	    if (*++s)
		file_prefix = s;
	    else if (++i < argc)
		file_prefix = argv[i];
	    else
		usage();
	    continue;

	case 'o':
	    if (*++s)
		output_file_name = s;
	    else if (++i < argc)
		output_file_name = argv[i];
	    else
		usage();
	    continue;

	case 'p':
	    if (*++s)
		symbol_prefix = s;
	    else if (++i < argc)
		symbol_prefix = argv[i];
	    else
		usage();
	    continue;

	default:
	    setflag(ch);
	    break;
	}

	for (;;)
	{
	    switch (ch = *++s)
	    {
	    case '\0':
		goto end_of_option;

	    default:
		setflag(ch);
		break;
	    }
	}
      end_of_option:;
    }

  no_more_options:;
    if (i + 1 != argc)
	usage();
    input_file_name_len = strlen(argv[i]);
    input_file_name = TMALLOC(char, input_file_name_len + 1);
    NO_SPACE(input_file_name);
    strcpy(input_file_name, argv[i]);
}

void *
allocate(size_t n)
{
    void *p;

    p = NULL;
    if (n)
    {
	p = CALLOC(1, n);
	NO_SPACE(p);
    }
    return (p);
}

#define CREATE_FILE_NAME(dest, suffix) \
	dest = alloc_file_name(len, suffix)

static char *
alloc_file_name(size_t len, const char *suffix)
{
    char *result = TMALLOC(char, len + strlen(suffix) + 1);
    if (result == 0)
	no_space();
    strcpy(result, file_prefix);
    strcpy(result + len, suffix);
    return result;
}

static char *
find_suffix(char *name, const char *suffix)
{
    size_t len = strlen(name);
    size_t slen = strlen(suffix);
    if (len >= slen)
    {
	name += len - slen;
	if (strcmp(name, suffix) == 0)
	    return name;
    }
    return NULL;
}

static void
create_file_names(void)
{
    size_t len;
    const char *defines_suffix;
    const char *externs_suffix;
    char *suffix;

    suffix = NULL;
    defines_suffix = DEFINES_SUFFIX;
    externs_suffix = EXTERNS_SUFFIX;

    /* compute the file_prefix from the user provided output_file_name */
    if (output_file_name != 0)
    {
	if (!(suffix = find_suffix(output_file_name, OUTPUT_SUFFIX))
	    && (suffix = find_suffix(output_file_name, ".c")))
	{
	    defines_suffix = ".h";
	    externs_suffix = ".i";
	}
    }

    if (suffix != NULL)
    {
	len = (size_t) (suffix - output_file_name);
	file_prefix = TMALLOC(char, len + 1);
	NO_SPACE(file_prefix);
	strncpy(file_prefix, output_file_name, len)[len] = 0;
    }
    else
	len = strlen(file_prefix);

    /* if "-o filename" was not given */
    if (output_file_name == 0)
    {
	oflag = 1;
	CREATE_FILE_NAME(output_file_name, OUTPUT_SUFFIX);
    }

    if (rflag)
    {
	CREATE_FILE_NAME(code_file_name, CODE_SUFFIX);
    }
    else
	code_file_name = output_file_name;

    if (dflag)
    {
	CREATE_FILE_NAME(defines_file_name, defines_suffix);
    }

    if (iflag)
    {
	CREATE_FILE_NAME(externs_file_name, externs_suffix);
    }

    if (vflag)
    {
	CREATE_FILE_NAME(verbose_file_name, VERBOSE_SUFFIX);
    }

    if (gflag)
    {
	CREATE_FILE_NAME(graph_file_name, GRAPH_SUFFIX);
    }

    if (suffix != NULL)
    {
	FREE(file_prefix);
    }
}

#if USE_MKSTEMP
static void
close_tmpfiles(void)
{
    while (my_tmpfiles != 0)
    {
	MY_TMPFILES *next = my_tmpfiles->next;

	(void)chmod(my_tmpfiles->name, 0644);
	(void)unlink(my_tmpfiles->name);

	free(my_tmpfiles->name);
	free(my_tmpfiles);

	my_tmpfiles = next;
    }
}

#ifndef HAVE_MKSTEMP
static int
my_mkstemp(char *temp)
{
    int fd;
    char *dname;
    char *fname;
    char *name;

    /*
     * Split-up to use tempnam, rather than tmpnam; the latter (like
     * mkstemp) is unusable on Windows.
     */
    if ((fname = strrchr(temp, '/')) != 0)
    {
	dname = strdup(temp);
	dname[++fname - temp] = '\0';
    }
    else
    {
	dname = 0;
	fname = temp;
    }
    if ((name = tempnam(dname, fname)) != 0)
    {
	fd = open(name, O_CREAT | O_EXCL | O_RDWR);
	strcpy(temp, name);
    }
    else
    {
	fd = -1;
    }

    if (dname != 0)
	free(dname);

    return fd;
}
#define mkstemp(s) my_mkstemp(s)
#endif

#endif

/*
 * tmpfile() should be adequate, except that it may require special privileges
 * to use, e.g., MinGW and Windows 7 where it tries to use the root directory.
 */
static FILE *
open_tmpfile(const char *label)
{
#define MY_FMT "%s/%.*sXXXXXX"
    FILE *result;
#if USE_MKSTEMP
    int fd;
    const char *tmpdir;
    char *name;
    const char *mark;

    if ((tmpdir = getenv("TMPDIR")) == 0 || access(tmpdir, W_OK) != 0)
    {
#ifdef P_tmpdir
	tmpdir = P_tmpdir;
#else
	tmpdir = "/tmp";
#endif
	if (access(tmpdir, W_OK) != 0)
	    tmpdir = ".";
    }

    /* The size of the format is guaranteed to be longer than the result from
     * printing empty strings with it; this calculation accounts for the
     * string-lengths as well.
     */
    name = malloc(strlen(tmpdir) + sizeof(MY_FMT) + strlen(label));

    result = 0;
    if (name != 0)
    {
	mode_t save_umask = umask(0177);

	if ((mark = strrchr(label, '_')) == 0)
	    mark = label + strlen(label);

	sprintf(name, MY_FMT, tmpdir, (int)(mark - label), label);
	fd = mkstemp(name);
	if (fd >= 0)
	{
	    result = fdopen(fd, "w+");
	    if (result != 0)
	    {
		MY_TMPFILES *item;

		if (my_tmpfiles == 0)
		{
		    atexit(close_tmpfiles);
		}

		item = NEW(MY_TMPFILES);
		NO_SPACE(item);

		item->name = name;
		NO_SPACE(item->name);

		item->next = my_tmpfiles;
		my_tmpfiles = item;
	    }
	}
	(void)umask(save_umask);
    }
#else
    result = tmpfile();
#endif

    if (result == 0)
	open_error(label);
    return result;
#undef MY_FMT
}

static void
open_files(void)
{
    create_file_names();

    if (input_file == 0)
    {
	input_file = fopen(input_file_name, "r");
	if (input_file == 0)
	    open_error(input_file_name);
    }

    action_file = open_tmpfile("action_file");
    text_file = open_tmpfile("text_file");

    if (vflag)
    {
	verbose_file = fopen(verbose_file_name, "w");
	if (verbose_file == 0)
	    open_error(verbose_file_name);
    }

    if (gflag)
    {
	graph_file = fopen(graph_file_name, "w");
	if (graph_file == 0)
	    open_error(graph_file_name);
	fprintf(graph_file, "digraph %s {\n", file_prefix);
	fprintf(graph_file, "\tedge [fontsize=10];\n");
	fprintf(graph_file, "\tnode [shape=box,fontsize=10];\n");
	fprintf(graph_file, "\torientation=landscape;\n");
	fprintf(graph_file, "\trankdir=LR;\n");
	fprintf(graph_file, "\t/*\n");
	fprintf(graph_file, "\tmargin=0.2;\n");
	fprintf(graph_file, "\tpage=\"8.27,11.69\"; // for A4 printing\n");
	fprintf(graph_file, "\tratio=auto;\n");
	fprintf(graph_file, "\t*/\n");
    }

    if (dflag)
    {
	defines_file = fopen(defines_file_name, "w");
	if (defines_file == 0)
	    open_error(defines_file_name);
	union_file = open_tmpfile("union_file");
    }

    if (iflag)
    {
	externs_file = fopen(externs_file_name, "w");
	if (externs_file == 0)
	    open_error(externs_file_name);
    }

    output_file = fopen(output_file_name, "w");
    if (output_file == 0)
	open_error(output_file_name);

    if (rflag)
    {
	code_file = fopen(code_file_name, "w");
	if (code_file == 0)
	    open_error(code_file_name);
    }
    else
	code_file = output_file;
}

int
main(int argc, char *argv[])
{
    SRexpect = -1;
    RRexpect = -1;
    exit_code = EXIT_SUCCESS;

    set_signals();
    getargs(argc, argv);
    open_files();
    reader();
    lr0();
    lalr();
    make_parser();
    graph();
    finalize_closure();
    verbose();
    output();
    done(exit_code);
    /*NOTREACHED */
}
