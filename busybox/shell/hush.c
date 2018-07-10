/* vi: set sw=4 ts=4: */
/*
 * A prototype Bourne shell grammar parser.
 * Intended to follow the original Thompson and Ritchie
 * "small and simple is beautiful" philosophy, which
 * incidentally is a good match to today's BusyBox.
 *
 * Copyright (C) 2000,2001  Larry Doolittle <larry@doolittle.boa.org>
 * Copyright (C) 2008,2009  Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Credits:
 *      The parser routines proper are all original material, first
 *      written Dec 2000 and Jan 2001 by Larry Doolittle.  The
 *      execution engine, the builtins, and much of the underlying
 *      support has been adapted from busybox-0.49pre's lash, which is
 *      Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *      written by Erik Andersen <andersen@codepoet.org>.  That, in turn,
 *      is based in part on ladsh.c, by Michael K. Johnson and Erik W.
 *      Troan, which they placed in the public domain.  I don't know
 *      how much of the Johnson/Troan code has survived the repeated
 *      rewrites.
 *
 * Other credits:
 *      o_addchr derived from similar w_addchar function in glibc-2.2.
 *      parse_redirect, redirect_opt_num, and big chunks of main
 *      and many builtins derived from contributions by Erik Andersen.
 *      Miscellaneous bugfixes from Matt Kraai.
 *
 * There are two big (and related) architecture differences between
 * this parser and the lash parser.  One is that this version is
 * actually designed from the ground up to understand nearly all
 * of the Bourne grammar.  The second, consequential change is that
 * the parser and input reader have been turned inside out.  Now,
 * the parser is in control, and asks for input as needed.  The old
 * way had the input reader in control, and it asked for parsing to
 * take place as needed.  The new way makes it much easier to properly
 * handle the recursion implicit in the various substitutions, especially
 * across continuation lines.
 *
 * TODOs:
 *      grep for "TODO" and fix (some of them are easy)
 *      make complex ${var%...} constructs support optional
 *      make here documents optional
 *      special variables (done: PWD, PPID, RANDOM)
 *      follow IFS rules more precisely, including update semantics
 *      tilde expansion
 *      aliases
 *      "command" missing features:
 *          command -p CMD: run CMD using default $PATH
 *              (can use this to override standalone shell as well?)
 *          command BLTIN: disables special-ness (e.g. errors do not abort)
 *          command -V CMD1 CMD2 CMD3 (multiple args) (not in standard)
 *      builtins mandated by standards we don't support:
 *          [un]alias, fc:
 *          fc -l[nr] [BEG] [END]: list range of commands in history
 *          fc [-e EDITOR] [BEG] [END]: edit/rerun range of commands
 *          fc -s [PAT=REP] [CMD]: rerun CMD, replacing PAT with REP
 *
 * Bash compat TODO:
 *      redirection of stdout+stderr: &> and >&
 *      reserved words: function select
 *      advanced test: [[ ]]
 *      process substitution: <(list) and >(list)
 *      =~: regex operator
 *      let EXPR [EXPR...]
 *          Each EXPR is an arithmetic expression (ARITHMETIC EVALUATION)
 *          If the last arg evaluates to 0, let returns 1; 0 otherwise.
 *          NB: let `echo 'a=a + 1'` - error (IOW: multi-word expansion is used)
 *      ((EXPR))
 *          The EXPR is evaluated according to ARITHMETIC EVALUATION.
 *          This is exactly equivalent to let "EXPR".
 *      $[EXPR]: synonym for $((EXPR))
 *      indirect expansion: ${!VAR}
 *      substring op on @: ${@:n:m}
 *
 * Won't do:
 *      Some builtins mandated by standards:
 *          newgrp [GRP]: not a builtin in bash but a suid binary
 *              which spawns a new shell with new group ID
 *
 * Status of [[ support:
 * [[ args ]] are CMD_SINGLEWORD_NOGLOB:
 *   v='a b'; [[ $v = 'a b' ]]; echo 0:$?
 *   [[ /bin/n* ]]; echo 0:$?
 * TODO:
 * &&/|| are AND/OR ops, -a/-o are not
 * quoting needs to be considered (-f is an operator, "-f" and ""-f are not; etc)
 * = is glob match operator, not equality operator: STR = GLOB
 * (in GLOB, quoting is significant on char-by-char basis: a*cd"*")
 * == same as =
 * add =~ regex match operator: STR =~ REGEX
 */
//config:config HUSH
//config:	bool "hush (64 kb)"
//config:	default y
//config:	help
//config:	hush is a small shell. It handles the normal flow control
//config:	constructs such as if/then/elif/else/fi, for/in/do/done, while loops,
//config:	case/esac. Redirections, here documents, $((arithmetic))
//config:	and functions are supported.
//config:
//config:	It will compile and work on no-mmu systems.
//config:
//config:	It does not handle select, aliases, tilde expansion,
//config:	&>file and >&file redirection of stdout+stderr.
//config:
//config:config HUSH_BASH_COMPAT
//config:	bool "bash-compatible extensions"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_BRACE_EXPANSION
//config:	bool "Brace expansion"
//config:	default y
//config:	depends on HUSH_BASH_COMPAT
//config:	help
//config:	Enable {abc,def} extension.
//config:
//config:config HUSH_LINENO_VAR
//config:	bool "$LINENO variable"
//config:	default y
//config:	depends on HUSH_BASH_COMPAT
//config:
//config:config HUSH_BASH_SOURCE_CURDIR
//config:	bool "'source' and '.' builtins search current directory after $PATH"
//config:	default n   # do not encourage non-standard behavior
//config:	depends on HUSH_BASH_COMPAT
//config:	help
//config:	This is not compliant with standards. Avoid if possible.
//config:
//config:config HUSH_INTERACTIVE
//config:	bool "Interactive mode"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:	help
//config:	Enable interactive mode (prompt and command editing).
//config:	Without this, hush simply reads and executes commands
//config:	from stdin just like a shell script from a file.
//config:	No prompt, no PS1/PS2 magic shell variables.
//config:
//config:config HUSH_SAVEHISTORY
//config:	bool "Save command history to .hush_history"
//config:	default y
//config:	depends on HUSH_INTERACTIVE && FEATURE_EDITING_SAVEHISTORY
//config:
//config:config HUSH_JOB
//config:	bool "Job control"
//config:	default y
//config:	depends on HUSH_INTERACTIVE
//config:	help
//config:	Enable job control: Ctrl-Z backgrounds, Ctrl-C interrupts current
//config:	command (not entire shell), fg/bg builtins work. Without this option,
//config:	"cmd &" still works by simply spawning a process and immediately
//config:	prompting for next command (or executing next command in a script),
//config:	but no separate process group is formed.
//config:
//config:config HUSH_TICK
//config:	bool "Support process substitution"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:	help
//config:	Enable `command` and $(command).
//config:
//config:config HUSH_IF
//config:	bool "Support if/then/elif/else/fi"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_LOOPS
//config:	bool "Support for, while and until loops"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_CASE
//config:	bool "Support case ... esac statement"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:	help
//config:	Enable case ... esac statement. +400 bytes.
//config:
//config:config HUSH_FUNCTIONS
//config:	bool "Support funcname() { commands; } syntax"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:	help
//config:	Enable support for shell functions. +800 bytes.
//config:
//config:config HUSH_LOCAL
//config:	bool "local builtin"
//config:	default y
//config:	depends on HUSH_FUNCTIONS
//config:	help
//config:	Enable support for local variables in functions.
//config:
//config:config HUSH_RANDOM_SUPPORT
//config:	bool "Pseudorandom generator and $RANDOM variable"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:	help
//config:	Enable pseudorandom generator and dynamic variable "$RANDOM".
//config:	Each read of "$RANDOM" will generate a new pseudorandom value.
//config:
//config:config HUSH_MODE_X
//config:	bool "Support 'hush -x' option and 'set -x' command"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:	help
//config:	This instructs hush to print commands before execution.
//config:	Adds ~300 bytes.
//config:
//config:config HUSH_ECHO
//config:	bool "echo builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_PRINTF
//config:	bool "printf builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_TEST
//config:	bool "test builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_HELP
//config:	bool "help builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_EXPORT
//config:	bool "export builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_EXPORT_N
//config:	bool "Support 'export -n' option"
//config:	default y
//config:	depends on HUSH_EXPORT
//config:	help
//config:	export -n unexports variables. It is a bash extension.
//config:
//config:config HUSH_READONLY
//config:	bool "readonly builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:	help
//config:	Enable support for read-only variables.
//config:
//config:config HUSH_KILL
//config:	bool "kill builtin (supports kill %jobspec)"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_WAIT
//config:	bool "wait builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_COMMAND
//config:	bool "command builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_TRAP
//config:	bool "trap builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_TYPE
//config:	bool "type builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_TIMES
//config:	bool "times builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_READ
//config:	bool "read builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_SET
//config:	bool "set builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_UNSET
//config:	bool "unset builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_ULIMIT
//config:	bool "ulimit builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_UMASK
//config:	bool "umask builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_GETOPTS
//config:	bool "getopts builtin"
//config:	default y
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH
//config:
//config:config HUSH_MEMLEAK
//config:	bool "memleak builtin (debugging)"
//config:	default n
//config:	depends on HUSH || SH_IS_HUSH || BASH_IS_HUSH

//applet:IF_HUSH(APPLET(hush, BB_DIR_BIN, BB_SUID_DROP))
//                       APPLET_ODDNAME:name  main  location    suid_type     help
//applet:IF_SH_IS_HUSH(  APPLET_ODDNAME(sh,   hush, BB_DIR_BIN, BB_SUID_DROP, hush))
//applet:IF_BASH_IS_HUSH(APPLET_ODDNAME(bash, hush, BB_DIR_BIN, BB_SUID_DROP, hush))

//kbuild:lib-$(CONFIG_HUSH) += hush.o match.o shell_common.o
//kbuild:lib-$(CONFIG_SH_IS_HUSH) += hush.o match.o shell_common.o
//kbuild:lib-$(CONFIG_BASH_IS_HUSH) += hush.o match.o shell_common.o
//kbuild:lib-$(CONFIG_HUSH_RANDOM_SUPPORT) += random.o

/* -i (interactive) is also accepted,
 * but does nothing, therefore not shown in help.
 * NOMMU-specific options are not meant to be used by users,
 * therefore we don't show them either.
 */
//usage:#define hush_trivial_usage
//usage:	"[-enxl] [-c 'SCRIPT' [ARG0 [ARGS]] / FILE [ARGS] / -s [ARGS]]"
//usage:#define hush_full_usage "\n\n"
//usage:	"Unix shell interpreter"

#if !(defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) \
	|| defined(__APPLE__) \
    )
# include <malloc.h>   /* for malloc_trim */
#endif
#include <glob.h>
/* #include <dmalloc.h> */
#if ENABLE_HUSH_CASE
# include <fnmatch.h>
#endif
#include <sys/times.h>
#include <sys/utsname.h> /* for setting $HOSTNAME */

#include "busybox.h"  /* for APPLET_IS_NOFORK/NOEXEC */
#include "unicode.h"
#include "shell_common.h"
#include "math.h"
#include "match.h"
#if ENABLE_HUSH_RANDOM_SUPPORT
# include "random.h"
#else
# define CLEAR_RANDOM_T(rnd) ((void)0)
#endif
#ifndef F_DUPFD_CLOEXEC
# define F_DUPFD_CLOEXEC F_DUPFD
#endif
#ifndef PIPE_BUF
# define PIPE_BUF 4096  /* amount of buffering in a pipe */
#endif


/* So far, all bash compat is controlled by one config option */
/* Separate defines document which part of code implements what */
#define BASH_PATTERN_SUBST ENABLE_HUSH_BASH_COMPAT
#define BASH_SUBSTR        ENABLE_HUSH_BASH_COMPAT
#define BASH_SOURCE        ENABLE_HUSH_BASH_COMPAT
#define BASH_HOSTNAME_VAR  ENABLE_HUSH_BASH_COMPAT
#define BASH_TEST2         (ENABLE_HUSH_BASH_COMPAT && ENABLE_HUSH_TEST)
#define BASH_READ_D        ENABLE_HUSH_BASH_COMPAT


/* Build knobs */
#define LEAK_HUNTING 0
#define BUILD_AS_NOMMU 0
/* Enable/disable sanity checks. Ok to enable in production,
 * only adds a bit of bloat. Set to >1 to get non-production level verbosity.
 * Keeping 1 for now even in released versions.
 */
#define HUSH_DEBUG 1
/* Slightly bigger (+200 bytes), but faster hush.
 * So far it only enables a trick with counting SIGCHLDs and forks,
 * which allows us to do fewer waitpid's.
 * (we can detect a case where neither forks were done nor SIGCHLDs happened
 * and therefore waitpid will return the same result as last time)
 */
#define ENABLE_HUSH_FAST 0
/* TODO: implement simplified code for users which do not need ${var%...} ops
 * So far ${var%...} ops are always enabled:
 */
#define ENABLE_HUSH_DOLLAR_OPS 1


#if BUILD_AS_NOMMU
# undef BB_MMU
# undef USE_FOR_NOMMU
# undef USE_FOR_MMU
# define BB_MMU 0
# define USE_FOR_NOMMU(...) __VA_ARGS__
# define USE_FOR_MMU(...)
#endif

#include "NUM_APPLETS.h"
#if NUM_APPLETS == 1
/* STANDALONE does not make sense, and won't compile */
# undef CONFIG_FEATURE_SH_STANDALONE
# undef ENABLE_FEATURE_SH_STANDALONE
# undef IF_FEATURE_SH_STANDALONE
# undef IF_NOT_FEATURE_SH_STANDALONE
# define ENABLE_FEATURE_SH_STANDALONE 0
# define IF_FEATURE_SH_STANDALONE(...)
# define IF_NOT_FEATURE_SH_STANDALONE(...) __VA_ARGS__
#endif

#if !ENABLE_HUSH_INTERACTIVE
# undef ENABLE_FEATURE_EDITING
# define ENABLE_FEATURE_EDITING 0
# undef ENABLE_FEATURE_EDITING_FANCY_PROMPT
# define ENABLE_FEATURE_EDITING_FANCY_PROMPT 0
# undef ENABLE_FEATURE_EDITING_SAVE_ON_EXIT
# define ENABLE_FEATURE_EDITING_SAVE_ON_EXIT 0
#endif

/* Do we support ANY keywords? */
#if ENABLE_HUSH_IF || ENABLE_HUSH_LOOPS || ENABLE_HUSH_CASE
# define HAS_KEYWORDS 1
# define IF_HAS_KEYWORDS(...) __VA_ARGS__
# define IF_HAS_NO_KEYWORDS(...)
#else
# define HAS_KEYWORDS 0
# define IF_HAS_KEYWORDS(...)
# define IF_HAS_NO_KEYWORDS(...) __VA_ARGS__
#endif

/* If you comment out one of these below, it will be #defined later
 * to perform debug printfs to stderr: */
#define debug_printf(...)        do {} while (0)
/* Finer-grained debug switches */
#define debug_printf_parse(...)  do {} while (0)
#define debug_print_tree(a, b)   do {} while (0)
#define debug_printf_exec(...)   do {} while (0)
#define debug_printf_env(...)    do {} while (0)
#define debug_printf_jobs(...)   do {} while (0)
#define debug_printf_expand(...) do {} while (0)
#define debug_printf_varexp(...) do {} while (0)
#define debug_printf_glob(...)   do {} while (0)
#define debug_printf_redir(...)  do {} while (0)
#define debug_printf_list(...)   do {} while (0)
#define debug_printf_subst(...)  do {} while (0)
#define debug_printf_prompt(...) do {} while (0)
#define debug_printf_clean(...)  do {} while (0)

#define ERR_PTR ((void*)(long)1)

#define JOB_STATUS_FORMAT    "[%u] %-22s %.40s\n"

#define _SPECIAL_VARS_STR     "_*@$!?#"
#define SPECIAL_VARS_STR     ("_*@$!?#" + 1)
#define NUMERIC_SPECVARS_STR ("_*@$!?#" + 3)
#if BASH_PATTERN_SUBST
/* Support / and // replace ops */
/* Note that // is stored as \ in "encoded" string representation */
# define VAR_ENCODED_SUBST_OPS      "\\/%#:-=+?"
# define VAR_SUBST_OPS             ("\\/%#:-=+?" + 1)
# define MINUS_PLUS_EQUAL_QUESTION ("\\/%#:-=+?" + 5)
#else
# define VAR_ENCODED_SUBST_OPS      "%#:-=+?"
# define VAR_SUBST_OPS              "%#:-=+?"
# define MINUS_PLUS_EQUAL_QUESTION ("%#:-=+?" + 3)
#endif

#define SPECIAL_VAR_SYMBOL_STR "\3"
#define SPECIAL_VAR_SYMBOL       3
/* The "variable" with name "\1" emits string "\3". Testcase: "echo ^C" */
#define SPECIAL_VAR_QUOTED_SVS   1

struct variable;

static const char hush_version_str[] ALIGN1 = "HUSH_VERSION="BB_VER;

/* This supports saving pointers malloced in vfork child,
 * to be freed in the parent.
 */
#if !BB_MMU
typedef struct nommu_save_t {
	struct variable *old_vars;
	char **argv;
	char **argv_from_re_execing;
} nommu_save_t;
#endif

enum {
	RES_NONE  = 0,
#if ENABLE_HUSH_IF
	RES_IF    ,
	RES_THEN  ,
	RES_ELIF  ,
	RES_ELSE  ,
	RES_FI    ,
#endif
#if ENABLE_HUSH_LOOPS
	RES_FOR   ,
	RES_WHILE ,
	RES_UNTIL ,
	RES_DO    ,
	RES_DONE  ,
#endif
#if ENABLE_HUSH_LOOPS || ENABLE_HUSH_CASE
	RES_IN    ,
#endif
#if ENABLE_HUSH_CASE
	RES_CASE  ,
	/* three pseudo-keywords support contrived "case" syntax: */
	RES_CASE_IN,   /* "case ... IN", turns into RES_MATCH when IN is observed */
	RES_MATCH ,    /* "word)" */
	RES_CASE_BODY, /* "this command is inside CASE" */
	RES_ESAC  ,
#endif
	RES_XXXX  ,
	RES_SNTX
};

typedef struct o_string {
	char *data;
	int length; /* position where data is appended */
	int maxlen;
	int o_expflags;
	/* At least some part of the string was inside '' or "",
	 * possibly empty one: word"", wo''rd etc. */
	smallint has_quoted_part;
	smallint has_empty_slot;
} o_string;
enum {
	EXP_FLAG_SINGLEWORD     = 0x80, /* must be 0x80 */
	EXP_FLAG_GLOB           = 0x2,
	/* Protect newly added chars against globbing
	 * by prepending \ to *, ?, [, \ */
	EXP_FLAG_ESC_GLOB_CHARS = 0x1,
};
/* Used for initialization: o_string foo = NULL_O_STRING; */
#define NULL_O_STRING { NULL }

#ifndef debug_printf_parse
static const char *const assignment_flag[] = {
	"MAYBE_ASSIGNMENT",
	"DEFINITELY_ASSIGNMENT",
	"NOT_ASSIGNMENT",
	"WORD_IS_KEYWORD",
};
#endif

typedef struct in_str {
	const char *p;
	int peek_buf[2];
	int last_char;
	FILE *file;
} in_str;

/* The descrip member of this structure is only used to make
 * debugging output pretty */
static const struct {
	int mode;
	signed char default_fd;
	char descrip[3];
} redir_table[] = {
	{ O_RDONLY,                  0, "<"  },
	{ O_CREAT|O_TRUNC|O_WRONLY,  1, ">"  },
	{ O_CREAT|O_APPEND|O_WRONLY, 1, ">>" },
	{ O_CREAT|O_RDWR,            1, "<>" },
	{ O_RDONLY,                  0, "<<" },
/* Should not be needed. Bogus default_fd helps in debugging */
/*	{ O_RDONLY,                 77, "<<" }, */
};

struct redir_struct {
	struct redir_struct *next;
	char *rd_filename;          /* filename */
	int rd_fd;                  /* fd to redirect */
	/* fd to redirect to, or -3 if rd_fd is to be closed (n>&-) */
	int rd_dup;
	smallint rd_type;           /* (enum redir_type) */
	/* note: for heredocs, rd_filename contains heredoc delimiter,
	 * and subsequently heredoc itself; and rd_dup is a bitmask:
	 * bit 0: do we need to trim leading tabs?
	 * bit 1: is heredoc quoted (<<'delim' syntax) ?
	 */
};
typedef enum redir_type {
	REDIRECT_INPUT     = 0,
	REDIRECT_OVERWRITE = 1,
	REDIRECT_APPEND    = 2,
	REDIRECT_IO        = 3,
	REDIRECT_HEREDOC   = 4,
	REDIRECT_HEREDOC2  = 5, /* REDIRECT_HEREDOC after heredoc is loaded */

	REDIRFD_CLOSE      = -3,
	REDIRFD_SYNTAX_ERR = -2,
	REDIRFD_TO_FILE    = -1,
	/* otherwise, rd_fd is redirected to rd_dup */

	HEREDOC_SKIPTABS = 1,
	HEREDOC_QUOTED   = 2,
} redir_type;


struct command {
	pid_t pid;                  /* 0 if exited */
	unsigned assignment_cnt;    /* how many argv[i] are assignments? */
#if ENABLE_HUSH_LINENO_VAR
	unsigned lineno;
#endif
	smallint cmd_type;          /* CMD_xxx */
#define CMD_NORMAL   0
#define CMD_SUBSHELL 1
#if BASH_TEST2 || ENABLE_HUSH_LOCAL || ENABLE_HUSH_EXPORT || ENABLE_HUSH_READONLY
/* used for "[[ EXPR ]]", and to prevent word splitting and globbing in
 * "export v=t*"
 */
# define CMD_SINGLEWORD_NOGLOB 2
#endif
#if ENABLE_HUSH_FUNCTIONS
# define CMD_FUNCDEF 3
#endif

	smalluint cmd_exitcode;
	/* if non-NULL, this "command" is { list }, ( list ), or a compound statement */
	struct pipe *group;
#if !BB_MMU
	char *group_as_string;
#endif
#if ENABLE_HUSH_FUNCTIONS
	struct function *child_func;
/* This field is used to prevent a bug here:
 * while...do f1() {a;}; f1; f1() {b;}; f1; done
 * When we execute "f1() {a;}" cmd, we create new function and clear
 * cmd->group, cmd->group_as_string, cmd->argv[0].
 * When we execute "f1() {b;}", we notice that f1 exists,
 * and that its "parent cmd" struct is still "alive",
 * we put those fields back into cmd->xxx
 * (struct function has ->parent_cmd ptr to facilitate that).
 * When we loop back, we can execute "f1() {a;}" again and set f1 correctly.
 * Without this trick, loop would execute a;b;b;b;...
 * instead of correct sequence a;b;a;b;...
 * When command is freed, it severs the link
 * (sets ->child_func->parent_cmd to NULL).
 */
#endif
	char **argv;                /* command name and arguments */
/* argv vector may contain variable references (^Cvar^C, ^C0^C etc)
 * and on execution these are substituted with their values.
 * Substitution can make _several_ words out of one argv[n]!
 * Example: argv[0]=='.^C*^C.' here: echo .$*.
 * References of the form ^C`cmd arg^C are `cmd arg` substitutions.
 */
	struct redir_struct *redirects; /* I/O redirections */
};
/* Is there anything in this command at all? */
#define IS_NULL_CMD(cmd) \
	(!(cmd)->group && !(cmd)->argv && !(cmd)->redirects)

struct pipe {
	struct pipe *next;
	int num_cmds;               /* total number of commands in pipe */
	int alive_cmds;             /* number of commands running (not exited) */
	int stopped_cmds;           /* number of commands alive, but stopped */
#if ENABLE_HUSH_JOB
	unsigned jobid;             /* job number */
	pid_t pgrp;                 /* process group ID for the job */
	char *cmdtext;              /* name of job */
#endif
	struct command *cmds;       /* array of commands in pipe */
	smallint followup;          /* PIPE_BG, PIPE_SEQ, PIPE_OR, PIPE_AND */
	IF_HAS_KEYWORDS(smallint pi_inverted;) /* "! cmd | cmd" */
	IF_HAS_KEYWORDS(smallint res_word;) /* needed for if, for, while, until... */
};
typedef enum pipe_style {
	PIPE_SEQ = 0,
	PIPE_AND = 1,
	PIPE_OR  = 2,
	PIPE_BG  = 3,
} pipe_style;
/* Is there anything in this pipe at all? */
#define IS_NULL_PIPE(pi) \
	((pi)->num_cmds == 0 IF_HAS_KEYWORDS( && (pi)->res_word == RES_NONE))

/* This holds pointers to the various results of parsing */
struct parse_context {
	/* linked list of pipes */
	struct pipe *list_head;
	/* last pipe (being constructed right now) */
	struct pipe *pipe;
	/* last command in pipe (being constructed right now) */
	struct command *command;
	/* last redirect in command->redirects list */
	struct redir_struct *pending_redirect;
	o_string word;
#if !BB_MMU
	o_string as_string;
#endif
	smallint is_assignment; /* 0:maybe, 1:yes, 2:no, 3:keyword */
#if HAS_KEYWORDS
	smallint ctx_res_w;
	smallint ctx_inverted; /* "! cmd | cmd" */
#if ENABLE_HUSH_CASE
	smallint ctx_dsemicolon; /* ";;" seen */
#endif
	/* bitmask of FLAG_xxx, for figuring out valid reserved words */
	int old_flag;
	/* group we are enclosed in:
	 * example: "if pipe1; pipe2; then pipe3; fi"
	 * when we see "if" or "then", we malloc and copy current context,
	 * and make ->stack point to it. then we parse pipeN.
	 * when closing "then" / fi" / whatever is found,
	 * we move list_head into ->stack->command->group,
	 * copy ->stack into current context, and delete ->stack.
	 * (parsing of { list } and ( list ) doesn't use this method)
	 */
	struct parse_context *stack;
#endif
};
enum {
	MAYBE_ASSIGNMENT      = 0,
	DEFINITELY_ASSIGNMENT = 1,
	NOT_ASSIGNMENT        = 2,
	/* Not an assignment, but next word may be: "if v=xyz cmd;" */
	WORD_IS_KEYWORD       = 3,
};

/* On program start, environ points to initial environment.
 * putenv adds new pointers into it, unsetenv removes them.
 * Neither of these (de)allocates the strings.
 * setenv allocates new strings in malloc space and does putenv,
 * and thus setenv is unusable (leaky) for shell's purposes */
#define setenv(...) setenv_is_leaky_dont_use()
struct variable {
	struct variable *next;
	char *varstr;        /* points to "name=" portion */
	int max_len;         /* if > 0, name is part of initial env; else name is malloced */
	uint16_t var_nest_level;
	smallint flg_export; /* putenv should be done on this var */
	smallint flg_read_only;
};

enum {
	BC_BREAK = 1,
	BC_CONTINUE = 2,
};

#if ENABLE_HUSH_FUNCTIONS
struct function {
	struct function *next;
	char *name;
	struct command *parent_cmd;
	struct pipe *body;
# if !BB_MMU
	char *body_as_string;
# endif
};
#endif


/* set -/+o OPT support. (TODO: make it optional)
 * bash supports the following opts:
 * allexport       off
 * braceexpand     on
 * emacs           on
 * errexit         off
 * errtrace        off
 * functrace       off
 * hashall         on
 * histexpand      off
 * history         on
 * ignoreeof       off
 * interactive-comments    on
 * keyword         off
 * monitor         on
 * noclobber       off
 * noexec          off
 * noglob          off
 * nolog           off
 * notify          off
 * nounset         off
 * onecmd          off
 * physical        off
 * pipefail        off
 * posix           off
 * privileged      off
 * verbose         off
 * vi              off
 * xtrace          off
 */
static const char o_opt_strings[] ALIGN1 =
	"pipefail\0"
	"noexec\0"
	"errexit\0"
#if ENABLE_HUSH_MODE_X
	"xtrace\0"
#endif
	;
enum {
	OPT_O_PIPEFAIL,
	OPT_O_NOEXEC,
	OPT_O_ERREXIT,
#if ENABLE_HUSH_MODE_X
	OPT_O_XTRACE,
#endif
	NUM_OPT_O
};


struct FILE_list {
	struct FILE_list *next;
	FILE *fp;
	int fd;
};


/* "Globals" within this file */
/* Sorted roughly by size (smaller offsets == smaller code) */
struct globals {
	/* interactive_fd != 0 means we are an interactive shell.
	 * If we are, then saved_tty_pgrp can also be != 0, meaning
	 * that controlling tty is available. With saved_tty_pgrp == 0,
	 * job control still works, but terminal signals
	 * (^C, ^Z, ^Y, ^\) won't work at all, and background
	 * process groups can only be created with "cmd &".
	 * With saved_tty_pgrp != 0, hush will use tcsetpgrp()
	 * to give tty to the foreground process group,
	 * and will take it back when the group is stopped (^Z)
	 * or killed (^C).
	 */
#if ENABLE_HUSH_INTERACTIVE
	/* 'interactive_fd' is a fd# open to ctty, if we have one
	 * _AND_ if we decided to act interactively */
	int interactive_fd;
	const char *PS1;
	IF_FEATURE_EDITING_FANCY_PROMPT(const char *PS2;)
# define G_interactive_fd (G.interactive_fd)
#else
# define G_interactive_fd 0
#endif
#if ENABLE_FEATURE_EDITING
	line_input_t *line_input_state;
#endif
	pid_t root_pid;
	pid_t root_ppid;
	pid_t last_bg_pid;
#if ENABLE_HUSH_RANDOM_SUPPORT
	random_t random_gen;
#endif
#if ENABLE_HUSH_JOB
	int run_list_level;
	unsigned last_jobid;
	pid_t saved_tty_pgrp;
	struct pipe *job_list;
# define G_saved_tty_pgrp (G.saved_tty_pgrp)
#else
# define G_saved_tty_pgrp 0
#endif
	/* How deeply are we in context where "set -e" is ignored */
	int errexit_depth;
	/* "set -e" rules (do we follow them correctly?):
	 * Exit if pipe, list, or compound command exits with a non-zero status.
	 * Shell does not exit if failed command is part of condition in
	 * if/while, part of && or || list except the last command, any command
	 * in a pipe but the last, or if the command's return value is being
	 * inverted with !. If a compound command other than a subshell returns a
	 * non-zero status because a command failed while -e was being ignored, the
	 * shell does not exit. A trap on ERR, if set, is executed before the shell
	 * exits [ERR is a bashism].
	 *
	 * If a compound command or function executes in a context where -e is
	 * ignored, none of the commands executed within are affected by the -e
	 * setting. If a compound command or function sets -e while executing in a
	 * context where -e is ignored, that setting does not have any effect until
	 * the compound command or the command containing the function call completes.
	 */

	char o_opt[NUM_OPT_O];
#if ENABLE_HUSH_MODE_X
# define G_x_mode (G.o_opt[OPT_O_XTRACE])
#else
# define G_x_mode 0
#endif
#if ENABLE_HUSH_INTERACTIVE
	smallint promptmode; /* 0: PS1, 1: PS2 */
#endif
	smallint flag_SIGINT;
#if ENABLE_HUSH_LOOPS
	smallint flag_break_continue;
#endif
#if ENABLE_HUSH_FUNCTIONS
	/* 0: outside of a function (or sourced file)
	 * -1: inside of a function, ok to use return builtin
	 * 1: return is invoked, skip all till end of func
	 */
	smallint flag_return_in_progress;
# define G_flag_return_in_progress (G.flag_return_in_progress)
#else
# define G_flag_return_in_progress 0
#endif
	smallint exiting; /* used to prevent EXIT trap recursion */
	/* These support $?, $#, and $1 */
	smalluint last_exitcode;
	smalluint expand_exitcode;
	smalluint last_bg_pid_exitcode;
#if ENABLE_HUSH_SET
	/* are global_argv and global_argv[1..n] malloced? (note: not [0]) */
	smalluint global_args_malloced;
# define G_global_args_malloced (G.global_args_malloced)
#else
# define G_global_args_malloced 0
#endif
	/* how many non-NULL argv's we have. NB: $# + 1 */
	int global_argc;
	char **global_argv;
#if !BB_MMU
	char *argv0_for_re_execing;
#endif
#if ENABLE_HUSH_LOOPS
	unsigned depth_break_continue;
	unsigned depth_of_loop;
#endif
#if ENABLE_HUSH_GETOPTS
	unsigned getopt_count;
#endif
	const char *ifs;
	char *ifs_whitespace; /* = G.ifs or malloced */
	const char *cwd;
	struct variable *top_var;
	char **expanded_assignments;
	struct variable **shadowed_vars_pp;
	unsigned var_nest_level;
#if ENABLE_HUSH_FUNCTIONS
# if ENABLE_HUSH_LOCAL
	unsigned func_nest_level; /* solely to prevent "local v" in non-functions */
# endif
	struct function *top_func;
#endif
	/* Signal and trap handling */
#if ENABLE_HUSH_FAST
	unsigned count_SIGCHLD;
	unsigned handled_SIGCHLD;
	smallint we_have_children;
#endif
#if ENABLE_HUSH_LINENO_VAR
	unsigned lineno;
	char *lineno_var;
#endif
	struct FILE_list *FILE_list;
	/* Which signals have non-DFL handler (even with no traps set)?
	 * Set at the start to:
	 * (SIGQUIT + maybe SPECIAL_INTERACTIVE_SIGS + maybe SPECIAL_JOBSTOP_SIGS)
	 * SPECIAL_INTERACTIVE_SIGS are cleared after fork.
	 * The rest is cleared right before execv syscalls.
	 * Other than these two times, never modified.
	 */
	unsigned special_sig_mask;
#if ENABLE_HUSH_JOB
	unsigned fatal_sig_mask;
# define G_fatal_sig_mask (G.fatal_sig_mask)
#else
# define G_fatal_sig_mask 0
#endif
#if ENABLE_HUSH_TRAP
	char **traps; /* char *traps[NSIG] */
# define G_traps G.traps
#else
# define G_traps ((char**)NULL)
#endif
	sigset_t pending_set;
#if ENABLE_HUSH_MEMLEAK
	unsigned long memleak_value;
#endif
#if HUSH_DEBUG
	int debug_indent;
#endif
	struct sigaction sa;
#if ENABLE_FEATURE_EDITING
	char user_input_buf[CONFIG_FEATURE_EDITING_MAX_LEN];
#endif
};
#define G (*ptr_to_globals)
/* Not #defining name to G.name - this quickly gets unwieldy
 * (too many defines). Also, I actually prefer to see when a variable
 * is global, thus "G." prefix is a useful hint */
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
	/* memset(&G.sa, 0, sizeof(G.sa)); */  \
	sigfillset(&G.sa.sa_mask); \
	G.sa.sa_flags = SA_RESTART; \
} while (0)


/* Function prototypes for builtins */
static int builtin_cd(char **argv) FAST_FUNC;
#if ENABLE_HUSH_ECHO
static int builtin_echo(char **argv) FAST_FUNC;
#endif
static int builtin_eval(char **argv) FAST_FUNC;
static int builtin_exec(char **argv) FAST_FUNC;
static int builtin_exit(char **argv) FAST_FUNC;
#if ENABLE_HUSH_EXPORT
static int builtin_export(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_READONLY
static int builtin_readonly(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_JOB
static int builtin_fg_bg(char **argv) FAST_FUNC;
static int builtin_jobs(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_GETOPTS
static int builtin_getopts(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_HELP
static int builtin_help(char **argv) FAST_FUNC;
#endif
#if MAX_HISTORY && ENABLE_FEATURE_EDITING
static int builtin_history(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_LOCAL
static int builtin_local(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_MEMLEAK
static int builtin_memleak(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_PRINTF
static int builtin_printf(char **argv) FAST_FUNC;
#endif
static int builtin_pwd(char **argv) FAST_FUNC;
#if ENABLE_HUSH_READ
static int builtin_read(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_SET
static int builtin_set(char **argv) FAST_FUNC;
#endif
static int builtin_shift(char **argv) FAST_FUNC;
static int builtin_source(char **argv) FAST_FUNC;
#if ENABLE_HUSH_TEST || BASH_TEST2
static int builtin_test(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_TRAP
static int builtin_trap(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_TYPE
static int builtin_type(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_TIMES
static int builtin_times(char **argv) FAST_FUNC;
#endif
static int builtin_true(char **argv) FAST_FUNC;
#if ENABLE_HUSH_UMASK
static int builtin_umask(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_UNSET
static int builtin_unset(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_KILL
static int builtin_kill(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_WAIT
static int builtin_wait(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_LOOPS
static int builtin_break(char **argv) FAST_FUNC;
static int builtin_continue(char **argv) FAST_FUNC;
#endif
#if ENABLE_HUSH_FUNCTIONS
static int builtin_return(char **argv) FAST_FUNC;
#endif

/* Table of built-in functions.  They can be forked or not, depending on
 * context: within pipes, they fork.  As simple commands, they do not.
 * When used in non-forking context, they can change global variables
 * in the parent shell process.  If forked, of course they cannot.
 * For example, 'unset foo | whatever' will parse and run, but foo will
 * still be set at the end. */
struct built_in_command {
	const char *b_cmd;
	int (*b_function)(char **argv) FAST_FUNC;
#if ENABLE_HUSH_HELP
	const char *b_descr;
# define BLTIN(cmd, func, help) { cmd, func, help }
#else
# define BLTIN(cmd, func, help) { cmd, func }
#endif
};

static const struct built_in_command bltins1[] = {
	BLTIN("."        , builtin_source  , "Run commands in file"),
	BLTIN(":"        , builtin_true    , NULL),
#if ENABLE_HUSH_JOB
	BLTIN("bg"       , builtin_fg_bg   , "Resume job in background"),
#endif
#if ENABLE_HUSH_LOOPS
	BLTIN("break"    , builtin_break   , "Exit loop"),
#endif
	BLTIN("cd"       , builtin_cd      , "Change directory"),
#if ENABLE_HUSH_LOOPS
	BLTIN("continue" , builtin_continue, "Start new loop iteration"),
#endif
	BLTIN("eval"     , builtin_eval    , "Construct and run shell command"),
	BLTIN("exec"     , builtin_exec    , "Execute command, don't return to shell"),
	BLTIN("exit"     , builtin_exit    , NULL),
#if ENABLE_HUSH_EXPORT
	BLTIN("export"   , builtin_export  , "Set environment variables"),
#endif
#if ENABLE_HUSH_JOB
	BLTIN("fg"       , builtin_fg_bg   , "Bring job to foreground"),
#endif
#if ENABLE_HUSH_GETOPTS
	BLTIN("getopts"  , builtin_getopts , NULL),
#endif
#if ENABLE_HUSH_HELP
	BLTIN("help"     , builtin_help    , NULL),
#endif
#if MAX_HISTORY && ENABLE_FEATURE_EDITING
	BLTIN("history"  , builtin_history , "Show history"),
#endif
#if ENABLE_HUSH_JOB
	BLTIN("jobs"     , builtin_jobs    , "List jobs"),
#endif
#if ENABLE_HUSH_KILL
	BLTIN("kill"     , builtin_kill    , "Send signals to processes"),
#endif
#if ENABLE_HUSH_LOCAL
	BLTIN("local"    , builtin_local   , "Set local variables"),
#endif
#if ENABLE_HUSH_MEMLEAK
	BLTIN("memleak"  , builtin_memleak , NULL),
#endif
#if ENABLE_HUSH_READ
	BLTIN("read"     , builtin_read    , "Input into variable"),
#endif
#if ENABLE_HUSH_READONLY
	BLTIN("readonly" , builtin_readonly, "Make variables read-only"),
#endif
#if ENABLE_HUSH_FUNCTIONS
	BLTIN("return"   , builtin_return  , "Return from function"),
#endif
#if ENABLE_HUSH_SET
	BLTIN("set"      , builtin_set     , "Set positional parameters"),
#endif
	BLTIN("shift"    , builtin_shift   , "Shift positional parameters"),
#if BASH_SOURCE
	BLTIN("source"   , builtin_source  , NULL),
#endif
#if ENABLE_HUSH_TIMES
	BLTIN("times"    , builtin_times   , NULL),
#endif
#if ENABLE_HUSH_TRAP
	BLTIN("trap"     , builtin_trap    , "Trap signals"),
#endif
	BLTIN("true"     , builtin_true    , NULL),
#if ENABLE_HUSH_TYPE
	BLTIN("type"     , builtin_type    , "Show command type"),
#endif
#if ENABLE_HUSH_ULIMIT
	BLTIN("ulimit"   , shell_builtin_ulimit, "Control resource limits"),
#endif
#if ENABLE_HUSH_UMASK
	BLTIN("umask"    , builtin_umask   , "Set file creation mask"),
#endif
#if ENABLE_HUSH_UNSET
	BLTIN("unset"    , builtin_unset   , "Unset variables"),
#endif
#if ENABLE_HUSH_WAIT
	BLTIN("wait"     , builtin_wait    , "Wait for process to finish"),
#endif
};
/* These builtins won't be used if we are on NOMMU and need to re-exec
 * (it's cheaper to run an external program in this case):
 */
static const struct built_in_command bltins2[] = {
#if ENABLE_HUSH_TEST
	BLTIN("["        , builtin_test    , NULL),
#endif
#if BASH_TEST2
	BLTIN("[["       , builtin_test    , NULL),
#endif
#if ENABLE_HUSH_ECHO
	BLTIN("echo"     , builtin_echo    , NULL),
#endif
#if ENABLE_HUSH_PRINTF
	BLTIN("printf"   , builtin_printf  , NULL),
#endif
	BLTIN("pwd"      , builtin_pwd     , NULL),
#if ENABLE_HUSH_TEST
	BLTIN("test"     , builtin_test    , NULL),
#endif
};


/* Debug printouts.
 */
#if HUSH_DEBUG
/* prevent disasters with G.debug_indent < 0 */
# define indent() fdprintf(2, "%*s", (G.debug_indent * 2) & 0xff, "")
# define debug_enter() (G.debug_indent++)
# define debug_leave() (G.debug_indent--)
#else
# define indent()      ((void)0)
# define debug_enter() ((void)0)
# define debug_leave() ((void)0)
#endif

#ifndef debug_printf
# define debug_printf(...) (indent(), fdprintf(2, __VA_ARGS__))
#endif

#ifndef debug_printf_parse
# define debug_printf_parse(...) (indent(), fdprintf(2, __VA_ARGS__))
#endif

#ifndef debug_printf_exec
#define debug_printf_exec(...) (indent(), fdprintf(2, __VA_ARGS__))
#endif

#ifndef debug_printf_env
# define debug_printf_env(...) (indent(), fdprintf(2, __VA_ARGS__))
#endif

#ifndef debug_printf_jobs
# define debug_printf_jobs(...) (indent(), fdprintf(2, __VA_ARGS__))
# define DEBUG_JOBS 1
#else
# define DEBUG_JOBS 0
#endif

#ifndef debug_printf_expand
# define debug_printf_expand(...) (indent(), fdprintf(2, __VA_ARGS__))
# define DEBUG_EXPAND 1
#else
# define DEBUG_EXPAND 0
#endif

#ifndef debug_printf_varexp
# define debug_printf_varexp(...) (indent(), fdprintf(2, __VA_ARGS__))
#endif

#ifndef debug_printf_glob
# define debug_printf_glob(...) (indent(), fdprintf(2, __VA_ARGS__))
# define DEBUG_GLOB 1
#else
# define DEBUG_GLOB 0
#endif

#ifndef debug_printf_redir
# define debug_printf_redir(...) (indent(), fdprintf(2, __VA_ARGS__))
#endif

#ifndef debug_printf_list
# define debug_printf_list(...) (indent(), fdprintf(2, __VA_ARGS__))
#endif

#ifndef debug_printf_subst
# define debug_printf_subst(...) (indent(), fdprintf(2, __VA_ARGS__))
#endif

#ifndef debug_printf_prompt
# define debug_printf_prompt(...) (indent(), fdprintf(2, __VA_ARGS__))
#endif

#ifndef debug_printf_clean
# define debug_printf_clean(...) (indent(), fdprintf(2, __VA_ARGS__))
# define DEBUG_CLEAN 1
#else
# define DEBUG_CLEAN 0
#endif

#if DEBUG_EXPAND
static void debug_print_strings(const char *prefix, char **vv)
{
	indent();
	fdprintf(2, "%s:\n", prefix);
	while (*vv)
		fdprintf(2, " '%s'\n", *vv++);
}
#else
# define debug_print_strings(prefix, vv) ((void)0)
#endif


/* Leak hunting. Use hush_leaktool.sh for post-processing.
 */
#if LEAK_HUNTING
static void *xxmalloc(int lineno, size_t size)
{
	void *ptr = xmalloc((size + 0xff) & ~0xff);
	fdprintf(2, "line %d: malloc %p\n", lineno, ptr);
	return ptr;
}
static void *xxrealloc(int lineno, void *ptr, size_t size)
{
	ptr = xrealloc(ptr, (size + 0xff) & ~0xff);
	fdprintf(2, "line %d: realloc %p\n", lineno, ptr);
	return ptr;
}
static char *xxstrdup(int lineno, const char *str)
{
	char *ptr = xstrdup(str);
	fdprintf(2, "line %d: strdup %p\n", lineno, ptr);
	return ptr;
}
static void xxfree(void *ptr)
{
	fdprintf(2, "free %p\n", ptr);
	free(ptr);
}
# define xmalloc(s)     xxmalloc(__LINE__, s)
# define xrealloc(p, s) xxrealloc(__LINE__, p, s)
# define xstrdup(s)     xxstrdup(__LINE__, s)
# define free(p)        xxfree(p)
#endif


/* Syntax and runtime errors. They always abort scripts.
 * In interactive use they usually discard unparsed and/or unexecuted commands
 * and return to the prompt.
 * HUSH_DEBUG >= 2 prints line number in this file where it was detected.
 */
#if HUSH_DEBUG < 2
# define msg_and_die_if_script(lineno, ...)     msg_and_die_if_script(__VA_ARGS__)
# define syntax_error(lineno, msg)              syntax_error(msg)
# define syntax_error_at(lineno, msg)           syntax_error_at(msg)
# define syntax_error_unterm_ch(lineno, ch)     syntax_error_unterm_ch(ch)
# define syntax_error_unterm_str(lineno, s)     syntax_error_unterm_str(s)
# define syntax_error_unexpected_ch(lineno, ch) syntax_error_unexpected_ch(ch)
#endif

static void die_if_script(void)
{
	if (!G_interactive_fd) {
		if (G.last_exitcode) /* sometines it's 2, not 1 (bash compat) */
			xfunc_error_retval = G.last_exitcode;
		xfunc_die();
	}
}

static void msg_and_die_if_script(unsigned lineno, const char *fmt, ...)
{
	va_list p;

#if HUSH_DEBUG >= 2
	bb_error_msg("hush.c:%u", lineno);
#endif
	va_start(p, fmt);
	bb_verror_msg(fmt, p, NULL);
	va_end(p);
	die_if_script();
}

static void syntax_error(unsigned lineno UNUSED_PARAM, const char *msg)
{
	if (msg)
		bb_error_msg("syntax error: %s", msg);
	else
		bb_error_msg("syntax error");
	die_if_script();
}

static void syntax_error_at(unsigned lineno UNUSED_PARAM, const char *msg)
{
	bb_error_msg("syntax error at '%s'", msg);
	die_if_script();
}

static void syntax_error_unterm_str(unsigned lineno UNUSED_PARAM, const char *s)
{
	bb_error_msg("syntax error: unterminated %s", s);
//? source4.tests fails: in bash, echo ${^} in script does not terminate the script
//	die_if_script();
}

static void syntax_error_unterm_ch(unsigned lineno, char ch)
{
	char msg[2] = { ch, '\0' };
	syntax_error_unterm_str(lineno, msg);
}

static void syntax_error_unexpected_ch(unsigned lineno UNUSED_PARAM, int ch)
{
	char msg[2];
	msg[0] = ch;
	msg[1] = '\0';
#if HUSH_DEBUG >= 2
	bb_error_msg("hush.c:%u", lineno);
#endif
	bb_error_msg("syntax error: unexpected %s", ch == EOF ? "EOF" : msg);
	die_if_script();
}

#if HUSH_DEBUG < 2
# undef msg_and_die_if_script
# undef syntax_error
# undef syntax_error_at
# undef syntax_error_unterm_ch
# undef syntax_error_unterm_str
# undef syntax_error_unexpected_ch
#else
# define msg_and_die_if_script(...)     msg_and_die_if_script(__LINE__, __VA_ARGS__)
# define syntax_error(msg)              syntax_error(__LINE__, msg)
# define syntax_error_at(msg)           syntax_error_at(__LINE__, msg)
# define syntax_error_unterm_ch(ch)     syntax_error_unterm_ch(__LINE__, ch)
# define syntax_error_unterm_str(s)     syntax_error_unterm_str(__LINE__, s)
# define syntax_error_unexpected_ch(ch) syntax_error_unexpected_ch(__LINE__, ch)
#endif


#if ENABLE_HUSH_INTERACTIVE && ENABLE_FEATURE_EDITING_FANCY_PROMPT
static void cmdedit_update_prompt(void);
#else
# define cmdedit_update_prompt() ((void)0)
#endif


/* Utility functions
 */
/* Replace each \x with x in place, return ptr past NUL. */
static char *unbackslash(char *src)
{
	char *dst = src = strchrnul(src, '\\');
	while (1) {
		if (*src == '\\') {
			src++;
			if (*src != '\0') {
				/* \x -> x */
				*dst++ = *src++;
				continue;
			}
			/* else: "\<nul>". Do not delete this backslash.
			 * Testcase: eval 'echo ok\'
			 */
			*dst++ = '\\';
			/* fallthrough */
		}
		if ((*dst++ = *src++) == '\0')
			break;
	}
	return dst;
}

static char **add_strings_to_strings(char **strings, char **add, int need_to_dup)
{
	int i;
	unsigned count1;
	unsigned count2;
	char **v;

	v = strings;
	count1 = 0;
	if (v) {
		while (*v) {
			count1++;
			v++;
		}
	}
	count2 = 0;
	v = add;
	while (*v) {
		count2++;
		v++;
	}
	v = xrealloc(strings, (count1 + count2 + 1) * sizeof(char*));
	v[count1 + count2] = NULL;
	i = count2;
	while (--i >= 0)
		v[count1 + i] = (need_to_dup ? xstrdup(add[i]) : add[i]);
	return v;
}
#if LEAK_HUNTING
static char **xx_add_strings_to_strings(int lineno, char **strings, char **add, int need_to_dup)
{
	char **ptr = add_strings_to_strings(strings, add, need_to_dup);
	fdprintf(2, "line %d: add_strings_to_strings %p\n", lineno, ptr);
	return ptr;
}
#define add_strings_to_strings(strings, add, need_to_dup) \
	xx_add_strings_to_strings(__LINE__, strings, add, need_to_dup)
#endif

/* Note: takes ownership of "add" ptr (it is not strdup'ed) */
static char **add_string_to_strings(char **strings, char *add)
{
	char *v[2];
	v[0] = add;
	v[1] = NULL;
	return add_strings_to_strings(strings, v, /*dup:*/ 0);
}
#if LEAK_HUNTING
static char **xx_add_string_to_strings(int lineno, char **strings, char *add)
{
	char **ptr = add_string_to_strings(strings, add);
	fdprintf(2, "line %d: add_string_to_strings %p\n", lineno, ptr);
	return ptr;
}
#define add_string_to_strings(strings, add) \
	xx_add_string_to_strings(__LINE__, strings, add)
#endif

static void free_strings(char **strings)
{
	char **v;

	if (!strings)
		return;
	v = strings;
	while (*v) {
		free(*v);
		v++;
	}
	free(strings);
}

static int dup_CLOEXEC(int fd, int avoid_fd)
{
	int newfd;
 repeat:
	newfd = fcntl(fd, F_DUPFD_CLOEXEC, avoid_fd + 1);
	if (newfd >= 0) {
		if (F_DUPFD_CLOEXEC == F_DUPFD) /* if old libc (w/o F_DUPFD_CLOEXEC) */
			fcntl(newfd, F_SETFD, FD_CLOEXEC);
	} else { /* newfd < 0 */
		if (errno == EBUSY)
			goto repeat;
		if (errno == EINTR)
			goto repeat;
	}
	return newfd;
}

static int xdup_CLOEXEC_and_close(int fd, int avoid_fd)
{
	int newfd;
 repeat:
	newfd = fcntl(fd, F_DUPFD_CLOEXEC, avoid_fd + 1);
	if (newfd < 0) {
		if (errno == EBUSY)
			goto repeat;
		if (errno == EINTR)
			goto repeat;
		/* fd was not open? */
		if (errno == EBADF)
			return fd;
		xfunc_die();
	}
	if (F_DUPFD_CLOEXEC == F_DUPFD) /* if old libc (w/o F_DUPFD_CLOEXEC) */
		fcntl(newfd, F_SETFD, FD_CLOEXEC);
	close(fd);
	return newfd;
}


/* Manipulating the list of open FILEs */
static FILE *remember_FILE(FILE *fp)
{
	if (fp) {
		struct FILE_list *n = xmalloc(sizeof(*n));
		n->next = G.FILE_list;
		G.FILE_list = n;
		n->fp = fp;
		n->fd = fileno(fp);
		close_on_exec_on(n->fd);
	}
	return fp;
}
static void fclose_and_forget(FILE *fp)
{
	struct FILE_list **pp = &G.FILE_list;
	while (*pp) {
		struct FILE_list *cur = *pp;
		if (cur->fp == fp) {
			*pp = cur->next;
			free(cur);
			break;
		}
		pp = &cur->next;
	}
	fclose(fp);
}
static int save_FILEs_on_redirect(int fd, int avoid_fd)
{
	struct FILE_list *fl = G.FILE_list;
	while (fl) {
		if (fd == fl->fd) {
			/* We use it only on script files, they are all CLOEXEC */
			fl->fd = xdup_CLOEXEC_and_close(fd, avoid_fd);
			debug_printf_redir("redirect_fd %d: matches a script fd, moving it to %d\n", fd, fl->fd);
			return 1;
		}
		fl = fl->next;
	}
	return 0;
}
static void restore_redirected_FILEs(void)
{
	struct FILE_list *fl = G.FILE_list;
	while (fl) {
		int should_be = fileno(fl->fp);
		if (fl->fd != should_be) {
			debug_printf_redir("restoring script fd from %d to %d\n", fl->fd, should_be);
			xmove_fd(fl->fd, should_be);
			fl->fd = should_be;
		}
		fl = fl->next;
	}
}
#if ENABLE_FEATURE_SH_STANDALONE && BB_MMU
static void close_all_FILE_list(void)
{
	struct FILE_list *fl = G.FILE_list;
	while (fl) {
		/* fclose would also free FILE object.
		 * It is disastrous if we share memory with a vforked parent.
		 * I'm not sure we never come here after vfork.
		 * Therefore just close fd, nothing more.
		 */
		/*fclose(fl->fp); - unsafe */
		close(fl->fd);
		fl = fl->next;
	}
}
#endif
static int fd_in_FILEs(int fd)
{
	struct FILE_list *fl = G.FILE_list;
	while (fl) {
		if (fl->fd == fd)
			return 1;
		fl = fl->next;
	}
	return 0;
}


/* Helpers for setting new $n and restoring them back
 */
typedef struct save_arg_t {
	char *sv_argv0;
	char **sv_g_argv;
	int sv_g_argc;
	IF_HUSH_SET(smallint sv_g_malloced;)
} save_arg_t;

static void save_and_replace_G_args(save_arg_t *sv, char **argv)
{
	sv->sv_argv0 = argv[0];
	sv->sv_g_argv = G.global_argv;
	sv->sv_g_argc = G.global_argc;
	IF_HUSH_SET(sv->sv_g_malloced = G.global_args_malloced;)

	argv[0] = G.global_argv[0]; /* retain $0 */
	G.global_argv = argv;
	IF_HUSH_SET(G.global_args_malloced = 0;)

	G.global_argc = 1 + string_array_len(argv + 1);
}

static void restore_G_args(save_arg_t *sv, char **argv)
{
#if ENABLE_HUSH_SET
	if (G.global_args_malloced) {
		/* someone ran "set -- arg1 arg2 ...", undo */
		char **pp = G.global_argv;
		while (*++pp) /* note: does not free $0 */
			free(*pp);
		free(G.global_argv);
	}
#endif
	argv[0] = sv->sv_argv0;
	G.global_argv = sv->sv_g_argv;
	G.global_argc = sv->sv_g_argc;
	IF_HUSH_SET(G.global_args_malloced = sv->sv_g_malloced;)
}


/* Basic theory of signal handling in shell
 * ========================================
 * This does not describe what hush does, rather, it is current understanding
 * what it _should_ do. If it doesn't, it's a bug.
 * http://www.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#trap
 *
 * Signals are handled only after each pipe ("cmd | cmd | cmd" thing)
 * is finished or backgrounded. It is the same in interactive and
 * non-interactive shells, and is the same regardless of whether
 * a user trap handler is installed or a shell special one is in effect.
 * ^C or ^Z from keyboard seems to execute "at once" because it usually
 * backgrounds (i.e. stops) or kills all members of currently running
 * pipe.
 *
 * Wait builtin is interruptible by signals for which user trap is set
 * or by SIGINT in interactive shell.
 *
 * Trap handlers will execute even within trap handlers. (right?)
 *
 * User trap handlers are forgotten when subshell ("(cmd)") is entered,
 * except for handlers set to '' (empty string).
 *
 * If job control is off, backgrounded commands ("cmd &")
 * have SIGINT, SIGQUIT set to SIG_IGN.
 *
 * Commands which are run in command substitution ("`cmd`")
 * have SIGTTIN, SIGTTOU, SIGTSTP set to SIG_IGN.
 *
 * Ordinary commands have signals set to SIG_IGN/DFL as inherited
 * by the shell from its parent.
 *
 * Signals which differ from SIG_DFL action
 * (note: child (i.e., [v]forked) shell is not an interactive shell):
 *
 * SIGQUIT: ignore
 * SIGTERM (interactive): ignore
 * SIGHUP (interactive):
 *    send SIGCONT to stopped jobs, send SIGHUP to all jobs and exit
 * SIGTTIN, SIGTTOU, SIGTSTP (if job control is on): ignore
 *    Note that ^Z is handled not by trapping SIGTSTP, but by seeing
 *    that all pipe members are stopped. Try this in bash:
 *    while :; do :; done - ^Z does not background it
 *    (while :; do :; done) - ^Z backgrounds it
 * SIGINT (interactive): wait for last pipe, ignore the rest
 *    of the command line, show prompt. NB: ^C does not send SIGINT
 *    to interactive shell while shell is waiting for a pipe,
 *    since shell is bg'ed (is not in foreground process group).
 *    Example 1: this waits 5 sec, but does not execute ls:
 *    "echo $$; sleep 5; ls -l" + "kill -INT <pid>"
 *    Example 2: this does not wait and does not execute ls:
 *    "echo $$; sleep 5 & wait; ls -l" + "kill -INT <pid>"
 *    Example 3: this does not wait 5 sec, but executes ls:
 *    "sleep 5; ls -l" + press ^C
 *    Example 4: this does not wait and does not execute ls:
 *    "sleep 5 & wait; ls -l" + press ^C
 *
 * (What happens to signals which are IGN on shell start?)
 * (What happens with signal mask on shell start?)
 *
 * Old implementation
 * ==================
 * We use in-kernel pending signal mask to determine which signals were sent.
 * We block all signals which we don't want to take action immediately,
 * i.e. we block all signals which need to have special handling as described
 * above, and all signals which have traps set.
 * After each pipe execution, we extract any pending signals via sigtimedwait()
 * and act on them.
 *
 * unsigned special_sig_mask: a mask of such "special" signals
 * sigset_t blocked_set:  current blocked signal set
 *
 * "trap - SIGxxx":
 *    clear bit in blocked_set unless it is also in special_sig_mask
 * "trap 'cmd' SIGxxx":
 *    set bit in blocked_set (even if 'cmd' is '')
 * after [v]fork, if we plan to be a shell:
 *    unblock signals with special interactive handling
 *    (child shell is not interactive),
 *    unset all traps except '' (note: regardless of child shell's type - {}, (), etc)
 * after [v]fork, if we plan to exec:
 *    POSIX says fork clears pending signal mask in child - no need to clear it.
 *    Restore blocked signal set to one inherited by shell just prior to exec.
 *
 * Note: as a result, we do not use signal handlers much. The only uses
 * are to count SIGCHLDs
 * and to restore tty pgrp on signal-induced exit.
 *
 * Note 2 (compat):
 * Standard says "When a subshell is entered, traps that are not being ignored
 * are set to the default actions". bash interprets it so that traps which
 * are set to '' (ignore) are NOT reset to defaults. We do the same.
 *
 * Problem: the above approach makes it unwieldy to catch signals while
 * we are in read builtin, or while we read commands from stdin:
 * masked signals are not visible!
 *
 * New implementation
 * ==================
 * We record each signal we are interested in by installing signal handler
 * for them - a bit like emulating kernel pending signal mask in userspace.
 * We are interested in: signals which need to have special handling
 * as described above, and all signals which have traps set.
 * Signals are recorded in pending_set.
 * After each pipe execution, we extract any pending signals
 * and act on them.
 *
 * unsigned special_sig_mask: a mask of shell-special signals.
 * unsigned fatal_sig_mask: a mask of signals on which we restore tty pgrp.
 * char *traps[sig] if trap for sig is set (even if it's '').
 * sigset_t pending_set: set of sigs we received.
 *
 * "trap - SIGxxx":
 *    if sig is in special_sig_mask, set handler back to:
 *        record_pending_signo, or to IGN if it's a tty stop signal
 *    if sig is in fatal_sig_mask, set handler back to sigexit.
 *    else: set handler back to SIG_DFL
 * "trap 'cmd' SIGxxx":
 *    set handler to record_pending_signo.
 * "trap '' SIGxxx":
 *    set handler to SIG_IGN.
 * after [v]fork, if we plan to be a shell:
 *    set signals with special interactive handling to SIG_DFL
 *    (because child shell is not interactive),
 *    unset all traps except '' (note: regardless of child shell's type - {}, (), etc)
 * after [v]fork, if we plan to exec:
 *    POSIX says fork clears pending signal mask in child - no need to clear it.
 *
 * To make wait builtin interruptible, we handle SIGCHLD as special signal,
 * otherwise (if we leave it SIG_DFL) sigsuspend in wait builtin will not wake up on it.
 *
 * Note (compat):
 * Standard says "When a subshell is entered, traps that are not being ignored
 * are set to the default actions". bash interprets it so that traps which
 * are set to '' (ignore) are NOT reset to defaults. We do the same.
 */
enum {
	SPECIAL_INTERACTIVE_SIGS = 0
		| (1 << SIGTERM)
		| (1 << SIGINT)
		| (1 << SIGHUP)
		,
	SPECIAL_JOBSTOP_SIGS = 0
#if ENABLE_HUSH_JOB
		| (1 << SIGTTIN)
		| (1 << SIGTTOU)
		| (1 << SIGTSTP)
#endif
		,
};

static void record_pending_signo(int sig)
{
	sigaddset(&G.pending_set, sig);
#if ENABLE_HUSH_FAST
	if (sig == SIGCHLD) {
		G.count_SIGCHLD++;
//bb_error_msg("[%d] SIGCHLD_handler: G.count_SIGCHLD:%d G.handled_SIGCHLD:%d", getpid(), G.count_SIGCHLD, G.handled_SIGCHLD);
	}
#endif
}

static sighandler_t install_sighandler(int sig, sighandler_t handler)
{
	struct sigaction old_sa;

	/* We could use signal() to install handlers... almost:
	 * except that we need to mask ALL signals while handlers run.
	 * I saw signal nesting in strace, race window isn't small.
	 * SA_RESTART is also needed, but in Linux, signal()
	 * sets SA_RESTART too.
	 */
	/* memset(&G.sa, 0, sizeof(G.sa)); - already done */
	/* sigfillset(&G.sa.sa_mask);      - already done */
	/* G.sa.sa_flags = SA_RESTART;     - already done */
	G.sa.sa_handler = handler;
	sigaction(sig, &G.sa, &old_sa);
	return old_sa.sa_handler;
}

static void hush_exit(int exitcode) NORETURN;

static void restore_ttypgrp_and__exit(void) NORETURN;
static void restore_ttypgrp_and__exit(void)
{
	/* xfunc has failed! die die die */
	/* no EXIT traps, this is an escape hatch! */
	G.exiting = 1;
	hush_exit(xfunc_error_retval);
}

#if ENABLE_HUSH_JOB

/* Needed only on some libc:
 * It was observed that on exit(), fgetc'ed buffered data
 * gets "unwound" via lseek(fd, -NUM, SEEK_CUR).
 * With the net effect that even after fork(), not vfork(),
 * exit() in NOEXECed applet in "sh SCRIPT":
 *	noexec_applet_here
 *	echo END_OF_SCRIPT
 * lseeks fd in input FILE object from EOF to "e" in "echo END_OF_SCRIPT".
 * This makes "echo END_OF_SCRIPT" executed twice.
 * Similar problems can be seen with msg_and_die_if_script() -> xfunc_die()
 * and in `cmd` handling.
 * If set as die_func(), this makes xfunc_die() exit via _exit(), not exit():
 */
static void fflush_and__exit(void) NORETURN;
static void fflush_and__exit(void)
{
	fflush_all();
	_exit(xfunc_error_retval);
}

/* After [v]fork, in child: do not restore tty pgrp on xfunc death */
# define disable_restore_tty_pgrp_on_exit() (die_func = fflush_and__exit)
/* After [v]fork, in parent: restore tty pgrp on xfunc death */
# define enable_restore_tty_pgrp_on_exit()  (die_func = restore_ttypgrp_and__exit)

/* Restores tty foreground process group, and exits.
 * May be called as signal handler for fatal signal
 * (will resend signal to itself, producing correct exit state)
 * or called directly with -EXITCODE.
 * We also call it if xfunc is exiting.
 */
static void sigexit(int sig) NORETURN;
static void sigexit(int sig)
{
	/* Careful: we can end up here after [v]fork. Do not restore
	 * tty pgrp then, only top-level shell process does that */
	if (G_saved_tty_pgrp && getpid() == G.root_pid) {
		/* Disable all signals: job control, SIGPIPE, etc.
		 * Mostly paranoid measure, to prevent infinite SIGTTOU.
		 */
		sigprocmask_allsigs(SIG_BLOCK);
		tcsetpgrp(G_interactive_fd, G_saved_tty_pgrp);
	}

	/* Not a signal, just exit */
	if (sig <= 0)
		_exit(- sig);

	kill_myself_with_sig(sig); /* does not return */
}
#else

# define disable_restore_tty_pgrp_on_exit() ((void)0)
# define enable_restore_tty_pgrp_on_exit()  ((void)0)

#endif

static sighandler_t pick_sighandler(unsigned sig)
{
	sighandler_t handler = SIG_DFL;
	if (sig < sizeof(unsigned)*8) {
		unsigned sigmask = (1 << sig);

#if ENABLE_HUSH_JOB
		/* is sig fatal? */
		if (G_fatal_sig_mask & sigmask)
			handler = sigexit;
		else
#endif
		/* sig has special handling? */
		if (G.special_sig_mask & sigmask) {
			handler = record_pending_signo;
			/* TTIN/TTOU/TSTP can't be set to record_pending_signo
			 * in order to ignore them: they will be raised
			 * in an endless loop when we try to do some
			 * terminal ioctls! We do have to _ignore_ these.
			 */
			if (SPECIAL_JOBSTOP_SIGS & sigmask)
				handler = SIG_IGN;
		}
	}
	return handler;
}

/* Restores tty foreground process group, and exits. */
static void hush_exit(int exitcode)
{
#if ENABLE_FEATURE_EDITING_SAVE_ON_EXIT
	save_history(G.line_input_state);
#endif

	fflush_all();
	if (G.exiting <= 0 && G_traps && G_traps[0] && G_traps[0][0]) {
		char *argv[3];
		/* argv[0] is unused */
		argv[1] = xstrdup(G_traps[0]); /* copy, since EXIT trap handler may modify G_traps[0] */
		argv[2] = NULL;
		G.exiting = 1; /* prevent EXIT trap recursion */
		/* Note: G_traps[0] is not cleared!
		 * "trap" will still show it, if executed
		 * in the handler */
		builtin_eval(argv);
	}

#if ENABLE_FEATURE_CLEAN_UP
	{
		struct variable *cur_var;
		if (G.cwd != bb_msg_unknown)
			free((char*)G.cwd);
		cur_var = G.top_var;
		while (cur_var) {
			struct variable *tmp = cur_var;
			if (!cur_var->max_len)
				free(cur_var->varstr);
			cur_var = cur_var->next;
			free(tmp);
		}
	}
#endif

	fflush_all();
#if ENABLE_HUSH_JOB
	sigexit(- (exitcode & 0xff));
#else
	_exit(exitcode);
#endif
}


//TODO: return a mask of ALL handled sigs?
static int check_and_run_traps(void)
{
	int last_sig = 0;

	while (1) {
		int sig;

		if (sigisemptyset(&G.pending_set))
			break;
		sig = 0;
		do {
			sig++;
			if (sigismember(&G.pending_set, sig)) {
				sigdelset(&G.pending_set, sig);
				goto got_sig;
			}
		} while (sig < NSIG);
		break;
 got_sig:
		if (G_traps && G_traps[sig]) {
			debug_printf_exec("%s: sig:%d handler:'%s'\n", __func__, sig, G.traps[sig]);
			if (G_traps[sig][0]) {
				/* We have user-defined handler */
				smalluint save_rcode;
				char *argv[3];
				/* argv[0] is unused */
				argv[1] = xstrdup(G_traps[sig]);
				/* why strdup? trap can modify itself: trap 'trap "echo oops" INT' INT */
				argv[2] = NULL;
				save_rcode = G.last_exitcode;
				builtin_eval(argv);
				free(argv[1]);
//FIXME: shouldn't it be set to 128 + sig instead?
				G.last_exitcode = save_rcode;
				last_sig = sig;
			} /* else: "" trap, ignoring signal */
			continue;
		}
		/* not a trap: special action */
		switch (sig) {
		case SIGINT:
			debug_printf_exec("%s: sig:%d default SIGINT handler\n", __func__, sig);
			G.flag_SIGINT = 1;
			last_sig = sig;
			break;
#if ENABLE_HUSH_JOB
		case SIGHUP: {
//TODO: why are we doing this? ash and dash don't do this,
//they have no handler for SIGHUP at all,
//they rely on kernel to send SIGHUP+SIGCONT to orphaned process groups
			struct pipe *job;
			debug_printf_exec("%s: sig:%d default SIGHUP handler\n", __func__, sig);
			/* bash is observed to signal whole process groups,
			 * not individual processes */
			for (job = G.job_list; job; job = job->next) {
				if (job->pgrp <= 0)
					continue;
				debug_printf_exec("HUPing pgrp %d\n", job->pgrp);
				if (kill(- job->pgrp, SIGHUP) == 0)
					kill(- job->pgrp, SIGCONT);
			}
			sigexit(SIGHUP);
		}
#endif
#if ENABLE_HUSH_FAST
		case SIGCHLD:
			debug_printf_exec("%s: sig:%d default SIGCHLD handler\n", __func__, sig);
			G.count_SIGCHLD++;
//bb_error_msg("[%d] check_and_run_traps: G.count_SIGCHLD:%d G.handled_SIGCHLD:%d", getpid(), G.count_SIGCHLD, G.handled_SIGCHLD);
			/* Note:
			 * We don't do 'last_sig = sig' here -> NOT returning this sig.
			 * This simplifies wait builtin a bit.
			 */
			break;
#endif
		default: /* ignored: */
			debug_printf_exec("%s: sig:%d default handling is to ignore\n", __func__, sig);
			/* SIGTERM, SIGQUIT, SIGTTIN, SIGTTOU, SIGTSTP */
			/* Note:
			 * We don't do 'last_sig = sig' here -> NOT returning this sig.
			 * Example: wait is not interrupted by TERM
			 * in interactive shell, because TERM is ignored.
			 */
			break;
		}
	}
	return last_sig;
}


static const char *get_cwd(int force)
{
	if (force || G.cwd == NULL) {
		/* xrealloc_getcwd_or_warn(arg) calls free(arg),
		 * we must not try to free(bb_msg_unknown) */
		if (G.cwd == bb_msg_unknown)
			G.cwd = NULL;
		G.cwd = xrealloc_getcwd_or_warn((char *)G.cwd);
		if (!G.cwd)
			G.cwd = bb_msg_unknown;
	}
	return G.cwd;
}


/*
 * Shell and environment variable support
 */
static struct variable **get_ptr_to_local_var(const char *name, unsigned len)
{
	struct variable **pp;
	struct variable *cur;

	pp = &G.top_var;
	while ((cur = *pp) != NULL) {
		if (strncmp(cur->varstr, name, len) == 0 && cur->varstr[len] == '=')
			return pp;
		pp = &cur->next;
	}
	return NULL;
}

static const char* FAST_FUNC get_local_var_value(const char *name)
{
	struct variable **vpp;
	unsigned len = strlen(name);

	if (G.expanded_assignments) {
		char **cpp = G.expanded_assignments;
		while (*cpp) {
			char *cp = *cpp;
			if (strncmp(cp, name, len) == 0 && cp[len] == '=')
				return cp + len + 1;
			cpp++;
		}
	}

	vpp = get_ptr_to_local_var(name, len);
	if (vpp)
		return (*vpp)->varstr + len + 1;

	if (strcmp(name, "PPID") == 0)
		return utoa(G.root_ppid);
	// bash compat: UID? EUID?
#if ENABLE_HUSH_RANDOM_SUPPORT
	if (strcmp(name, "RANDOM") == 0)
		return utoa(next_random(&G.random_gen));
#endif
	return NULL;
}

static void handle_changed_special_names(const char *name, unsigned name_len)
{
	if (ENABLE_HUSH_INTERACTIVE && ENABLE_FEATURE_EDITING_FANCY_PROMPT
	 && name_len == 3 && name[0] == 'P' && name[1] == 'S'
	) {
		cmdedit_update_prompt();
		return;
	}

	if ((ENABLE_HUSH_LINENO_VAR || ENABLE_HUSH_GETOPTS)
	 && name_len == 6
	) {
#if ENABLE_HUSH_LINENO_VAR
		if (strncmp(name, "LINENO", 6) == 0) {
			G.lineno_var = NULL;
			return;
		}
#endif
#if ENABLE_HUSH_GETOPTS
		if (strncmp(name, "OPTIND", 6) == 0) {
			G.getopt_count = 0;
			return;
		}
#endif
	}
}

/* str holds "NAME=VAL" and is expected to be malloced.
 * We take ownership of it.
 */
#define SETFLAG_EXPORT   (1 << 0)
#define SETFLAG_UNEXPORT (1 << 1)
#define SETFLAG_MAKE_RO  (1 << 2)
#define SETFLAG_VARLVL_SHIFT   3
static int set_local_var(char *str, unsigned flags)
{
	struct variable **cur_pp;
	struct variable *cur;
	char *free_me = NULL;
	char *eq_sign;
	int name_len;
	unsigned local_lvl = (flags >> SETFLAG_VARLVL_SHIFT);

	eq_sign = strchr(str, '=');
	if (HUSH_DEBUG && !eq_sign)
		bb_error_msg_and_die("BUG in setvar");

	name_len = eq_sign - str + 1; /* including '=' */
	cur_pp = &G.top_var;
	while ((cur = *cur_pp) != NULL) {
		if (strncmp(cur->varstr, str, name_len) != 0) {
			cur_pp = &cur->next;
			continue;
		}

		/* We found an existing var with this name */
		if (cur->flg_read_only) {
			bb_error_msg("%s: readonly variable", str);
			free(str);
//NOTE: in bash, assignment in "export READONLY_VAR=Z" fails, and sets $?=1,
//but export per se succeeds (does put the var in env). We don't mimic that.
			return -1;
		}
		if (flags & SETFLAG_UNEXPORT) { // && cur->flg_export ?
			debug_printf_env("%s: unsetenv '%s'\n", __func__, str);
			*eq_sign = '\0';
			unsetenv(str);
			*eq_sign = '=';
		}
		if (cur->var_nest_level < local_lvl) {
			/* bash 3.2.33(1) and exported vars:
			 * # export z=z
			 * # f() { local z=a; env | grep ^z; }
			 * # f
			 * z=a
			 * # env | grep ^z
			 * z=z
			 */
			if (cur->flg_export)
				flags |= SETFLAG_EXPORT;
			/* New variable is local ("local VAR=VAL" or
			 * "VAR=VAL cmd")
			 * and existing one is global, or local
			 * on a lower level that new one.
			 * Remove it from global variable list:
			 */
			*cur_pp = cur->next;
			if (G.shadowed_vars_pp) {
				/* Save in "shadowed" list */
				debug_printf_env("shadowing %s'%s'/%u by '%s'/%u\n",
					cur->flg_export ? "exported " : "",
					cur->varstr, cur->var_nest_level, str, local_lvl
				);
				cur->next = *G.shadowed_vars_pp;
				*G.shadowed_vars_pp = cur;
			} else {
				/* Came from pseudo_exec_argv(), no need to save: delete it */
				debug_printf_env("shadow-deleting %s'%s'/%u by '%s'/%u\n",
					cur->flg_export ? "exported " : "",
					cur->varstr, cur->var_nest_level, str, local_lvl
				);
				if (cur->max_len == 0) /* allocated "VAR=VAL"? */
					free_me = cur->varstr; /* then free it later */
				free(cur);
			}
			break;
		}

		if (strcmp(cur->varstr + name_len, eq_sign + 1) == 0) {
			debug_printf_env("assignement '%s' does not change anything\n", str);
 free_and_exp:
			free(str);
			goto exp;
		}

		/* Replace the value in the found "struct variable" */
		if (cur->max_len != 0) {
			if (cur->max_len >= strnlen(str, cur->max_len + 1)) {
				/* This one is from startup env, reuse space */
				debug_printf_env("reusing startup env for '%s'\n", str);
				strcpy(cur->varstr, str);
				goto free_and_exp;
			}
			/* Can't reuse */
			cur->max_len = 0;
			goto set_str_and_exp;
		}
		/* max_len == 0 signifies "malloced" var, which we can
		 * (and have to) free. But we can't free(cur->varstr) here:
		 * if cur->flg_export is 1, it is in the environment.
		 * We should either unsetenv+free, or wait until putenv,
		 * then putenv(new)+free(old).
		 */
		free_me = cur->varstr;
		goto set_str_and_exp;
	}

	/* Not found or shadowed - create new variable struct */
	debug_printf_env("%s: alloc new var '%s'/%u\n", __func__, str, local_lvl);
	cur = xzalloc(sizeof(*cur));
	cur->var_nest_level = local_lvl;
	cur->next = *cur_pp;
	*cur_pp = cur;

 set_str_and_exp:
	cur->varstr = str;
 exp:
#if !BB_MMU || ENABLE_HUSH_READONLY
	if (flags & SETFLAG_MAKE_RO) {
		cur->flg_read_only = 1;
	}
#endif
	if (flags & SETFLAG_EXPORT)
		cur->flg_export = 1;
	if (cur->flg_export) {
		if (flags & SETFLAG_UNEXPORT) {
			cur->flg_export = 0;
			/* unsetenv was already done */
		} else {
			int i;
			debug_printf_env("%s: putenv '%s'/%u\n", __func__, cur->varstr, cur->var_nest_level);
			i = putenv(cur->varstr);
			/* only now we can free old exported malloced string */
			free(free_me);
			return i;
		}
	}
	free(free_me);

	handle_changed_special_names(cur->varstr, name_len - 1);

	return 0;
}

/* Used at startup and after each cd */
static void set_pwd_var(unsigned flag)
{
	set_local_var(xasprintf("PWD=%s", get_cwd(/*force:*/ 1)), flag);
}

#if ENABLE_HUSH_UNSET || ENABLE_HUSH_GETOPTS
static int unset_local_var_len(const char *name, int name_len)
{
	struct variable *cur;
	struct variable **cur_pp;

	cur_pp = &G.top_var;
	while ((cur = *cur_pp) != NULL) {
		if (strncmp(cur->varstr, name, name_len) == 0
		 && cur->varstr[name_len] == '='
		) {
			if (cur->flg_read_only) {
				bb_error_msg("%s: readonly variable", name);
				return EXIT_FAILURE;
			}

			*cur_pp = cur->next;
			debug_printf_env("%s: unsetenv '%s'\n", __func__, cur->varstr);
			bb_unsetenv(cur->varstr);
			if (!cur->max_len)
				free(cur->varstr);
			free(cur);

			break;
		}
		cur_pp = &cur->next;
	}

	/* Handle "unset PS1" et al even if did not find the variable to unset */
	handle_changed_special_names(name, name_len);

	return EXIT_SUCCESS;
}

static int unset_local_var(const char *name)
{
	return unset_local_var_len(name, strlen(name));
}
#endif

#if BASH_HOSTNAME_VAR || ENABLE_FEATURE_SH_MATH || ENABLE_HUSH_READ || ENABLE_HUSH_GETOPTS \
 || (ENABLE_HUSH_INTERACTIVE && ENABLE_FEATURE_EDITING_FANCY_PROMPT)
static void FAST_FUNC set_local_var_from_halves(const char *name, const char *val)
{
	char *var = xasprintf("%s=%s", name, val);
	set_local_var(var, /*flag:*/ 0);
}
#endif


/*
 * Helpers for "var1=val1 var2=val2 cmd" feature
 */
static void add_vars(struct variable *var)
{
	struct variable *next;

	while (var) {
		next = var->next;
		var->next = G.top_var;
		G.top_var = var;
		if (var->flg_export) {
			debug_printf_env("%s: restoring exported '%s'/%u\n", __func__, var->varstr, var->var_nest_level);
			putenv(var->varstr);
		} else {
			debug_printf_env("%s: restoring variable '%s'/%u\n", __func__, var->varstr, var->var_nest_level);
		}
		var = next;
	}
}

/* We put strings[i] into variable table and possibly putenv them.
 * If variable is read only, we can free the strings[i]
 * which attempts to overwrite it.
 * The strings[] vector itself is freed.
 */
static void set_vars_and_save_old(char **strings)
{
	char **s;

	if (!strings)
		return;

	s = strings;
	while (*s) {
		struct variable *var_p;
		struct variable **var_pp;
		char *eq;

		eq = strchr(*s, '=');
		if (eq) {
			var_pp = get_ptr_to_local_var(*s, eq - *s);
			if (var_pp) {
				var_p = *var_pp;
				if (var_p->flg_read_only) {
					char **p;
					bb_error_msg("%s: readonly variable", *s);
					/*
					 * "VAR=V BLTIN" unsets VARs after BLTIN completes.
					 * If VAR is readonly, leaving it in the list
					 * after asssignment error (msg above)
					 * causes doubled error message later, on unset.
					 */
					debug_printf_env("removing/freeing '%s' element\n", *s);
					free(*s);
					p = s;
					do { *p = p[1]; p++; } while (*p);
					goto next;
				}
				/* below, set_local_var() with nest level will
				 * "shadow" (remove) this variable from
				 * global linked list.
				 */
			}
			debug_printf_env("%s: env override '%s'/%u\n", __func__, *s, G.var_nest_level);
			set_local_var(*s, (G.var_nest_level << SETFLAG_VARLVL_SHIFT) | SETFLAG_EXPORT);
		} else if (HUSH_DEBUG) {
			bb_error_msg_and_die("BUG in varexp4");
		}
		s++;
 next: ;
	}
	free(strings);
}


/*
 * Unicode helper
 */
static void reinit_unicode_for_hush(void)
{
	/* Unicode support should be activated even if LANG is set
	 * _during_ shell execution, not only if it was set when
	 * shell was started. Therefore, re-check LANG every time:
	 */
	if (ENABLE_FEATURE_CHECK_UNICODE_IN_ENV
	 || ENABLE_UNICODE_USING_LOCALE
        ) {
		const char *s = get_local_var_value("LC_ALL");
		if (!s) s = get_local_var_value("LC_CTYPE");
		if (!s) s = get_local_var_value("LANG");
		reinit_unicode(s);
	}
}

/*
 * in_str support (strings, and "strings" read from files).
 */

#if ENABLE_HUSH_INTERACTIVE
/* To test correct lineedit/interactive behavior, type from command line:
 *	echo $P\
 *	\
 *	AT\
 *	H\
 *	\
 * It exercises a lot of corner cases.
 */
# if ENABLE_FEATURE_EDITING_FANCY_PROMPT
static void cmdedit_update_prompt(void)
{
	G.PS1 = get_local_var_value("PS1");
	if (G.PS1 == NULL)
		G.PS1 = "";
	G.PS2 = get_local_var_value("PS2");
	if (G.PS2 == NULL)
		G.PS2 = "";
}
# endif
static const char *setup_prompt_string(void)
{
	const char *prompt_str;

	debug_printf_prompt("%s promptmode:%d\n", __func__, G.promptmode);

	IF_FEATURE_EDITING_FANCY_PROMPT(    prompt_str = G.PS2;)
	IF_NOT_FEATURE_EDITING_FANCY_PROMPT(prompt_str = "> ";)
	if (G.promptmode == 0) { /* PS1 */
		if (!ENABLE_FEATURE_EDITING_FANCY_PROMPT) {
			/* No fancy prompts supported, (re)generate "CURDIR $ " by hand */
			free((char*)G.PS1);
			/* bash uses $PWD value, even if it is set by user.
			 * It uses current dir only if PWD is unset.
			 * We always use current dir. */
			G.PS1 = xasprintf("%s %c ", get_cwd(0), (geteuid() != 0) ? '$' : '#');
		}
		prompt_str = G.PS1;
	}
	debug_printf("prompt_str '%s'\n", prompt_str);
	return prompt_str;
}
static int get_user_input(struct in_str *i)
{
	int r;
	const char *prompt_str;

	prompt_str = setup_prompt_string();
# if ENABLE_FEATURE_EDITING
	for (;;) {
		reinit_unicode_for_hush();
		if (G.flag_SIGINT) {
			/* There was ^C'ed, make it look prettier: */
			bb_putchar('\n');
			G.flag_SIGINT = 0;
		}
		/* buglet: SIGINT will not make new prompt to appear _at once_,
		 * only after <Enter>. (^C works immediately) */
		r = read_line_input(G.line_input_state, prompt_str,
				G.user_input_buf, CONFIG_FEATURE_EDITING_MAX_LEN-1
		);
		/* read_line_input intercepts ^C, "convert" it to SIGINT */
		if (r == 0)
			raise(SIGINT);
		check_and_run_traps();
		if (r != 0 && !G.flag_SIGINT)
			break;
		/* ^C or SIGINT: repeat */
		/* bash prints ^C even on real SIGINT (non-kbd generated) */
		write(STDOUT_FILENO, "^C", 2);
		G.last_exitcode = 128 + SIGINT;
	}
	if (r < 0) {
		/* EOF/error detected */
		i->p = NULL;
		i->peek_buf[0] = r = EOF;
		return r;
	}
	i->p = G.user_input_buf;
	return (unsigned char)*i->p++;
# else
	for (;;) {
		G.flag_SIGINT = 0;
		if (i->last_char == '\0' || i->last_char == '\n') {
			/* Why check_and_run_traps here? Try this interactively:
			 * $ trap 'echo INT' INT; (sleep 2; kill -INT $$) &
			 * $ <[enter], repeatedly...>
			 * Without check_and_run_traps, handler never runs.
			 */
			check_and_run_traps();
			fputs(prompt_str, stdout);
		}
		fflush_all();
//FIXME: here ^C or SIGINT will have effect only after <Enter>
		r = fgetc(i->file);
		/* In !ENABLE_FEATURE_EDITING we don't use read_line_input,
		 * no ^C masking happens during fgetc, no special code for ^C:
		 * it generates SIGINT as usual.
		 */
		check_and_run_traps();
		if (G.flag_SIGINT)
			G.last_exitcode = 128 + SIGINT;
		if (r != '\0')
			break;
	}
	return r;
# endif
}
/* This is the magic location that prints prompts
 * and gets data back from the user */
static int fgetc_interactive(struct in_str *i)
{
	int ch;
	/* If it's interactive stdin, get new line. */
	if (G_interactive_fd && i->file == stdin) {
		/* Returns first char (or EOF), the rest is in i->p[] */
		ch = get_user_input(i);
		G.promptmode = 1; /* PS2 */
		debug_printf_prompt("%s promptmode=%d\n", __func__, G.promptmode);
	} else {
		/* Not stdin: script file, sourced file, etc */
		do ch = fgetc(i->file); while (ch == '\0');
	}
	return ch;
}
#else
static inline int fgetc_interactive(struct in_str *i)
{
	int ch;
	do ch = fgetc(i->file); while (ch == '\0');
	return ch;
}
#endif  /* INTERACTIVE */

static int i_getch(struct in_str *i)
{
	int ch;

	if (!i->file) {
		/* string-based in_str */
		ch = (unsigned char)*i->p;
		if (ch != '\0') {
			i->p++;
			i->last_char = ch;
			return ch;
		}
		return EOF;
	}

	/* FILE-based in_str */

#if ENABLE_FEATURE_EDITING
	/* This can be stdin, check line editing char[] buffer */
	if (i->p && *i->p != '\0') {
		ch = (unsigned char)*i->p++;
		goto out;
	}
#endif
	/* peek_buf[] is an int array, not char. Can contain EOF. */
	ch = i->peek_buf[0];
	if (ch != 0) {
		int ch2 = i->peek_buf[1];
		i->peek_buf[0] = ch2;
		if (ch2 == 0) /* very likely, avoid redundant write */
			goto out;
		i->peek_buf[1] = 0;
		goto out;
	}

	ch = fgetc_interactive(i);
 out:
	debug_printf("file_get: got '%c' %d\n", ch, ch);
	i->last_char = ch;
#if ENABLE_HUSH_LINENO_VAR
	if (ch == '\n') {
		G.lineno++;
		debug_printf_parse("G.lineno++ = %u\n", G.lineno);
	}
#endif
	return ch;
}

static int i_peek(struct in_str *i)
{
	int ch;

	if (!i->file) {
		/* string-based in_str */
		/* Doesn't report EOF on NUL. None of the callers care. */
		return (unsigned char)*i->p;
	}

	/* FILE-based in_str */

#if ENABLE_FEATURE_EDITING && ENABLE_HUSH_INTERACTIVE
	/* This can be stdin, check line editing char[] buffer */
	if (i->p && *i->p != '\0')
		return (unsigned char)*i->p;
#endif
	/* peek_buf[] is an int array, not char. Can contain EOF. */
	ch = i->peek_buf[0];
	if (ch != 0)
		return ch;

	/* Need to get a new char */
	ch = fgetc_interactive(i);
	debug_printf("file_peek: got '%c' %d\n", ch, ch);

	/* Save it by either rolling back line editing buffer, or in i->peek_buf[0] */
#if ENABLE_FEATURE_EDITING && ENABLE_HUSH_INTERACTIVE
	if (i->p) {
		i->p -= 1;
		return ch;
	}
#endif
	i->peek_buf[0] = ch;
	/*i->peek_buf[1] = 0; - already is */
	return ch;
}

/* Only ever called if i_peek() was called, and did not return EOF.
 * IOW: we know the previous peek saw an ordinary char, not EOF, not NUL,
 * not end-of-line. Therefore we never need to read a new editing line here.
 */
static int i_peek2(struct in_str *i)
{
	int ch;

	/* There are two cases when i->p[] buffer exists.
	 * (1) it's a string in_str.
	 * (2) It's a file, and we have a saved line editing buffer.
	 * In both cases, we know that i->p[0] exists and not NUL, and
	 * the peek2 result is in i->p[1].
	 */
	if (i->p)
		return (unsigned char)i->p[1];

	/* Now we know it is a file-based in_str. */

	/* peek_buf[] is an int array, not char. Can contain EOF. */
	/* Is there 2nd char? */
	ch = i->peek_buf[1];
	if (ch == 0) {
		/* We did not read it yet, get it now */
		do ch = fgetc(i->file); while (ch == '\0');
		i->peek_buf[1] = ch;
	}

	debug_printf("file_peek2: got '%c' %d\n", ch, ch);
	return ch;
}

static int i_getch_and_eat_bkslash_nl(struct in_str *input)
{
	for (;;) {
		int ch, ch2;

		ch = i_getch(input);
		if (ch != '\\')
			return ch;
		ch2 = i_peek(input);
		if (ch2 != '\n')
			return ch;
		/* backslash+newline, skip it */
		i_getch(input);
	}
}

/* Note: this function _eats_ \<newline> pairs, safe to use plain
 * i_getch() after it instead of i_getch_and_eat_bkslash_nl().
 */
static int i_peek_and_eat_bkslash_nl(struct in_str *input)
{
	for (;;) {
		int ch, ch2;

		ch = i_peek(input);
		if (ch != '\\')
			return ch;
		ch2 = i_peek2(input);
		if (ch2 != '\n')
			return ch;
		/* backslash+newline, skip it */
		i_getch(input);
		i_getch(input);
	}
}

static void setup_file_in_str(struct in_str *i, FILE *f)
{
	memset(i, 0, sizeof(*i));
	i->file = f;
	/* i->p = NULL; */
}

static void setup_string_in_str(struct in_str *i, const char *s)
{
	memset(i, 0, sizeof(*i));
	/*i->file = NULL */;
	i->p = s;
}


/*
 * o_string support
 */
#define B_CHUNK  (32 * sizeof(char*))

static void o_reset_to_empty_unquoted(o_string *o)
{
	o->length = 0;
	o->has_quoted_part = 0;
	if (o->data)
		o->data[0] = '\0';
}

static void o_free(o_string *o)
{
	free(o->data);
	memset(o, 0, sizeof(*o));
}

static ALWAYS_INLINE void o_free_unsafe(o_string *o)
{
	free(o->data);
}

static void o_grow_by(o_string *o, int len)
{
	if (o->length + len > o->maxlen) {
		o->maxlen += (2 * len) | (B_CHUNK-1);
		o->data = xrealloc(o->data, 1 + o->maxlen);
	}
}

static void o_addchr(o_string *o, int ch)
{
	debug_printf("o_addchr: '%c' o->length=%d o=%p\n", ch, o->length, o);
	if (o->length < o->maxlen) {
		/* likely. avoid o_grow_by() call */
 add:
		o->data[o->length] = ch;
		o->length++;
		o->data[o->length] = '\0';
		return;
	}
	o_grow_by(o, 1);
	goto add;
}

#if 0
/* Valid only if we know o_string is not empty */
static void o_delchr(o_string *o)
{
	o->length--;
	o->data[o->length] = '\0';
}
#endif

static void o_addblock(o_string *o, const char *str, int len)
{
	o_grow_by(o, len);
	((char*)mempcpy(&o->data[o->length], str, len))[0] = '\0';
	o->length += len;
}

static void o_addstr(o_string *o, const char *str)
{
	o_addblock(o, str, strlen(str));
}

#if !BB_MMU
static void nommu_addchr(o_string *o, int ch)
{
	if (o)
		o_addchr(o, ch);
}
#else
# define nommu_addchr(o, str) ((void)0)
#endif

static void o_addstr_with_NUL(o_string *o, const char *str)
{
	o_addblock(o, str, strlen(str) + 1);
}

/*
 * HUSH_BRACE_EXPANSION code needs corresponding quoting on variable expansion side.
 * Currently, "v='{q,w}'; echo $v" erroneously expands braces in $v.
 * Apparently, on unquoted $v bash still does globbing
 * ("v='*.txt'; echo $v" prints all .txt files),
 * but NOT brace expansion! Thus, there should be TWO independent
 * quoting mechanisms on $v expansion side: one protects
 * $v from brace expansion, and other additionally protects "$v" against globbing.
 * We have only second one.
 */

#if ENABLE_HUSH_BRACE_EXPANSION
# define MAYBE_BRACES "{}"
#else
# define MAYBE_BRACES ""
#endif

/* My analysis of quoting semantics tells me that state information
 * is associated with a destination, not a source.
 */
static void o_addqchr(o_string *o, int ch)
{
	int sz = 1;
	char *found = strchr("*?[\\" MAYBE_BRACES, ch);
	if (found)
		sz++;
	o_grow_by(o, sz);
	if (found) {
		o->data[o->length] = '\\';
		o->length++;
	}
	o->data[o->length] = ch;
	o->length++;
	o->data[o->length] = '\0';
}

static void o_addQchr(o_string *o, int ch)
{
	int sz = 1;
	if ((o->o_expflags & EXP_FLAG_ESC_GLOB_CHARS)
	 && strchr("*?[\\" MAYBE_BRACES, ch)
	) {
		sz++;
		o->data[o->length] = '\\';
		o->length++;
	}
	o_grow_by(o, sz);
	o->data[o->length] = ch;
	o->length++;
	o->data[o->length] = '\0';
}

static void o_addqblock(o_string *o, const char *str, int len)
{
	while (len) {
		char ch;
		int sz;
		int ordinary_cnt = strcspn(str, "*?[\\" MAYBE_BRACES);
		if (ordinary_cnt > len) /* paranoia */
			ordinary_cnt = len;
		o_addblock(o, str, ordinary_cnt);
		if (ordinary_cnt == len)
			return; /* NUL is already added by o_addblock */
		str += ordinary_cnt;
		len -= ordinary_cnt + 1; /* we are processing + 1 char below */

		ch = *str++;
		sz = 1;
		if (ch) { /* it is necessarily one of "*?[\\" MAYBE_BRACES */
			sz++;
			o->data[o->length] = '\\';
			o->length++;
		}
		o_grow_by(o, sz);
		o->data[o->length] = ch;
		o->length++;
	}
	o->data[o->length] = '\0';
}

static void o_addQblock(o_string *o, const char *str, int len)
{
	if (!(o->o_expflags & EXP_FLAG_ESC_GLOB_CHARS)) {
		o_addblock(o, str, len);
		return;
	}
	o_addqblock(o, str, len);
}

static void o_addQstr(o_string *o, const char *str)
{
	o_addQblock(o, str, strlen(str));
}

/* A special kind of o_string for $VAR and `cmd` expansion.
 * It contains char* list[] at the beginning, which is grown in 16 element
 * increments. Actual string data starts at the next multiple of 16 * (char*).
 * list[i] contains an INDEX (int!) into this string data.
 * It means that if list[] needs to grow, data needs to be moved higher up
 * but list[i]'s need not be modified.
 * NB: remembering how many list[i]'s you have there is crucial.
 * o_finalize_list() operation post-processes this structure - calculates
 * and stores actual char* ptrs in list[]. Oh, it NULL terminates it as well.
 */
#if DEBUG_EXPAND || DEBUG_GLOB
static void debug_print_list(const char *prefix, o_string *o, int n)
{
	char **list = (char**)o->data;
	int string_start = ((n + 0xf) & ~0xf) * sizeof(list[0]);
	int i = 0;

	indent();
	fdprintf(2, "%s: list:%p n:%d string_start:%d length:%d maxlen:%d glob:%d quoted:%d escape:%d\n",
			prefix, list, n, string_start, o->length, o->maxlen,
			!!(o->o_expflags & EXP_FLAG_GLOB),
			o->has_quoted_part,
			!!(o->o_expflags & EXP_FLAG_ESC_GLOB_CHARS));
	while (i < n) {
		indent();
		fdprintf(2, " list[%d]=%d '%s' %p\n", i, (int)(uintptr_t)list[i],
				o->data + (int)(uintptr_t)list[i] + string_start,
				o->data + (int)(uintptr_t)list[i] + string_start);
		i++;
	}
	if (n) {
		const char *p = o->data + (int)(uintptr_t)list[n - 1] + string_start;
		indent();
		fdprintf(2, " total_sz:%ld\n", (long)((p + strlen(p) + 1) - o->data));
	}
}
#else
# define debug_print_list(prefix, o, n) ((void)0)
#endif

/* n = o_save_ptr_helper(str, n) "starts new string" by storing an index value
 * in list[n] so that it points past last stored byte so far.
 * It returns n+1. */
static int o_save_ptr_helper(o_string *o, int n)
{
	char **list = (char**)o->data;
	int string_start;
	int string_len;

	if (!o->has_empty_slot) {
		string_start = ((n + 0xf) & ~0xf) * sizeof(list[0]);
		string_len = o->length - string_start;
		if (!(n & 0xf)) { /* 0, 0x10, 0x20...? */
			debug_printf_list("list[%d]=%d string_start=%d (growing)\n", n, string_len, string_start);
			/* list[n] points to string_start, make space for 16 more pointers */
			o->maxlen += 0x10 * sizeof(list[0]);
			o->data = xrealloc(o->data, o->maxlen + 1);
			list = (char**)o->data;
			memmove(list + n + 0x10, list + n, string_len);
			o->length += 0x10 * sizeof(list[0]);
		} else {
			debug_printf_list("list[%d]=%d string_start=%d\n",
					n, string_len, string_start);
		}
	} else {
		/* We have empty slot at list[n], reuse without growth */
		string_start = ((n+1 + 0xf) & ~0xf) * sizeof(list[0]); /* NB: n+1! */
		string_len = o->length - string_start;
		debug_printf_list("list[%d]=%d string_start=%d (empty slot)\n",
				n, string_len, string_start);
		o->has_empty_slot = 0;
	}
	o->has_quoted_part = 0;
	list[n] = (char*)(uintptr_t)string_len;
	return n + 1;
}

/* "What was our last o_save_ptr'ed position (byte offset relative o->data)?" */
static int o_get_last_ptr(o_string *o, int n)
{
	char **list = (char**)o->data;
	int string_start = ((n + 0xf) & ~0xf) * sizeof(list[0]);

	return ((int)(uintptr_t)list[n-1]) + string_start;
}

#if ENABLE_HUSH_BRACE_EXPANSION
/* There in a GNU extension, GLOB_BRACE, but it is not usable:
 * first, it processes even {a} (no commas), second,
 * I didn't manage to make it return strings when they don't match
 * existing files. Need to re-implement it.
 */

/* Helper */
static int glob_needed(const char *s)
{
	while (*s) {
		if (*s == '\\') {
			if (!s[1])
				return 0;
			s += 2;
			continue;
		}
		if (*s == '*' || *s == '[' || *s == '?' || *s == '{')
			return 1;
		s++;
	}
	return 0;
}
/* Return pointer to next closing brace or to comma */
static const char *next_brace_sub(const char *cp)
{
	unsigned depth = 0;
	cp++;
	while (*cp != '\0') {
		if (*cp == '\\') {
			if (*++cp == '\0')
				break;
			cp++;
			continue;
		}
		if ((*cp == '}' && depth-- == 0) || (*cp == ',' && depth == 0))
			break;
		if (*cp++ == '{')
			depth++;
	}

	return *cp != '\0' ? cp : NULL;
}
/* Recursive brace globber. Note: may garble pattern[]. */
static int glob_brace(char *pattern, o_string *o, int n)
{
	char *new_pattern_buf;
	const char *begin;
	const char *next;
	const char *rest;
	const char *p;
	size_t rest_len;

	debug_printf_glob("glob_brace('%s')\n", pattern);

	begin = pattern;
	while (1) {
		if (*begin == '\0')
			goto simple_glob;
		if (*begin == '{') {
			/* Find the first sub-pattern and at the same time
			 * find the rest after the closing brace */
			next = next_brace_sub(begin);
			if (next == NULL) {
				/* An illegal expression */
				goto simple_glob;
			}
			if (*next == '}') {
				/* "{abc}" with no commas - illegal
				 * brace expr, disregard and skip it */
				begin = next + 1;
				continue;
			}
			break;
		}
		if (*begin == '\\' && begin[1] != '\0')
			begin++;
		begin++;
	}
	debug_printf_glob("begin:%s\n", begin);
	debug_printf_glob("next:%s\n", next);

	/* Now find the end of the whole brace expression */
	rest = next;
	while (*rest != '}') {
		rest = next_brace_sub(rest);
		if (rest == NULL) {
			/* An illegal expression */
			goto simple_glob;
		}
		debug_printf_glob("rest:%s\n", rest);
	}
	rest_len = strlen(++rest) + 1;

	/* We are sure the brace expression is well-formed */

	/* Allocate working buffer large enough for our work */
	new_pattern_buf = xmalloc(strlen(pattern));

	/* We have a brace expression.  BEGIN points to the opening {,
	 * NEXT points past the terminator of the first element, and REST
	 * points past the final }.  We will accumulate result names from
	 * recursive runs for each brace alternative in the buffer using
	 * GLOB_APPEND.  */

	p = begin + 1;
	while (1) {
		/* Construct the new glob expression */
		memcpy(
			mempcpy(
				mempcpy(new_pattern_buf,
					/* We know the prefix for all sub-patterns */
					pattern, begin - pattern),
				p, next - p),
			rest, rest_len);

		/* Note: glob_brace() may garble new_pattern_buf[].
		 * That's why we re-copy prefix every time (1st memcpy above).
		 */
		n = glob_brace(new_pattern_buf, o, n);
		if (*next == '}') {
			/* We saw the last entry */
			break;
		}
		p = next + 1;
		next = next_brace_sub(next);
	}
	free(new_pattern_buf);
	return n;

 simple_glob:
	{
		int gr;
		glob_t globdata;

		memset(&globdata, 0, sizeof(globdata));
		gr = glob(pattern, 0, NULL, &globdata);
		debug_printf_glob("glob('%s'):%d\n", pattern, gr);
		if (gr != 0) {
			if (gr == GLOB_NOMATCH) {
				globfree(&globdata);
				/* NB: garbles parameter */
				unbackslash(pattern);
				o_addstr_with_NUL(o, pattern);
				debug_printf_glob("glob pattern '%s' is literal\n", pattern);
				return o_save_ptr_helper(o, n);
			}
			if (gr == GLOB_NOSPACE)
				bb_die_memory_exhausted();
			/* GLOB_ABORTED? Only happens with GLOB_ERR flag,
			 * but we didn't specify it. Paranoia again. */
			bb_error_msg_and_die("glob error %d on '%s'", gr, pattern);
		}
		if (globdata.gl_pathv && globdata.gl_pathv[0]) {
			char **argv = globdata.gl_pathv;
			while (1) {
				o_addstr_with_NUL(o, *argv);
				n = o_save_ptr_helper(o, n);
				argv++;
				if (!*argv)
					break;
			}
		}
		globfree(&globdata);
	}
	return n;
}
/* Performs globbing on last list[],
 * saving each result as a new list[].
 */
static int perform_glob(o_string *o, int n)
{
	char *pattern, *copy;

	debug_printf_glob("start perform_glob: n:%d o->data:%p\n", n, o->data);
	if (!o->data)
		return o_save_ptr_helper(o, n);
	pattern = o->data + o_get_last_ptr(o, n);
	debug_printf_glob("glob pattern '%s'\n", pattern);
	if (!glob_needed(pattern)) {
		/* unbackslash last string in o in place, fix length */
		o->length = unbackslash(pattern) - o->data;
		debug_printf_glob("glob pattern '%s' is literal\n", pattern);
		return o_save_ptr_helper(o, n);
	}

	copy = xstrdup(pattern);
	/* "forget" pattern in o */
	o->length = pattern - o->data;
	n = glob_brace(copy, o, n);
	free(copy);
	if (DEBUG_GLOB)
		debug_print_list("perform_glob returning", o, n);
	return n;
}

#else /* !HUSH_BRACE_EXPANSION */

/* Helper */
static int glob_needed(const char *s)
{
	while (*s) {
		if (*s == '\\') {
			if (!s[1])
				return 0;
			s += 2;
			continue;
		}
		if (*s == '*' || *s == '[' || *s == '?')
			return 1;
		s++;
	}
	return 0;
}
/* Performs globbing on last list[],
 * saving each result as a new list[].
 */
static int perform_glob(o_string *o, int n)
{
	glob_t globdata;
	int gr;
	char *pattern;

	debug_printf_glob("start perform_glob: n:%d o->data:%p\n", n, o->data);
	if (!o->data)
		return o_save_ptr_helper(o, n);
	pattern = o->data + o_get_last_ptr(o, n);
	debug_printf_glob("glob pattern '%s'\n", pattern);
	if (!glob_needed(pattern)) {
 literal:
		/* unbackslash last string in o in place, fix length */
		o->length = unbackslash(pattern) - o->data;
		debug_printf_glob("glob pattern '%s' is literal\n", pattern);
		return o_save_ptr_helper(o, n);
	}

	memset(&globdata, 0, sizeof(globdata));
	/* Can't use GLOB_NOCHECK: it does not unescape the string.
	 * If we glob "*.\*" and don't find anything, we need
	 * to fall back to using literal "*.*", but GLOB_NOCHECK
	 * will return "*.\*"!
	 */
	gr = glob(pattern, 0, NULL, &globdata);
	debug_printf_glob("glob('%s'):%d\n", pattern, gr);
	if (gr != 0) {
		if (gr == GLOB_NOMATCH) {
			globfree(&globdata);
			goto literal;
		}
		if (gr == GLOB_NOSPACE)
			bb_die_memory_exhausted();
		/* GLOB_ABORTED? Only happens with GLOB_ERR flag,
		 * but we didn't specify it. Paranoia again. */
		bb_error_msg_and_die("glob error %d on '%s'", gr, pattern);
	}
	if (globdata.gl_pathv && globdata.gl_pathv[0]) {
		char **argv = globdata.gl_pathv;
		/* "forget" pattern in o */
		o->length = pattern - o->data;
		while (1) {
			o_addstr_with_NUL(o, *argv);
			n = o_save_ptr_helper(o, n);
			argv++;
			if (!*argv)
				break;
		}
	}
	globfree(&globdata);
	if (DEBUG_GLOB)
		debug_print_list("perform_glob returning", o, n);
	return n;
}

#endif /* !HUSH_BRACE_EXPANSION */

/* If o->o_expflags & EXP_FLAG_GLOB, glob the string so far remembered.
 * Otherwise, just finish current list[] and start new */
static int o_save_ptr(o_string *o, int n)
{
	if (o->o_expflags & EXP_FLAG_GLOB) {
		/* If o->has_empty_slot, list[n] was already globbed
		 * (if it was requested back then when it was filled)
		 * so don't do that again! */
		if (!o->has_empty_slot)
			return perform_glob(o, n); /* o_save_ptr_helper is inside */
	}
	return o_save_ptr_helper(o, n);
}

/* "Please convert list[n] to real char* ptrs, and NULL terminate it." */
static char **o_finalize_list(o_string *o, int n)
{
	char **list;
	int string_start;

	n = o_save_ptr(o, n); /* force growth for list[n] if necessary */
	if (DEBUG_EXPAND)
		debug_print_list("finalized", o, n);
	debug_printf_expand("finalized n:%d\n", n);
	list = (char**)o->data;
	string_start = ((n + 0xf) & ~0xf) * sizeof(list[0]);
	list[--n] = NULL;
	while (n) {
		n--;
		list[n] = o->data + (int)(uintptr_t)list[n] + string_start;
	}
	return list;
}

static void free_pipe_list(struct pipe *pi);

/* Returns pi->next - next pipe in the list */
static struct pipe *free_pipe(struct pipe *pi)
{
	struct pipe *next;
	int i;

	debug_printf_clean("free_pipe (pid %d)\n", getpid());
	for (i = 0; i < pi->num_cmds; i++) {
		struct command *command;
		struct redir_struct *r, *rnext;

		command = &pi->cmds[i];
		debug_printf_clean("  command %d:\n", i);
		if (command->argv) {
			if (DEBUG_CLEAN) {
				int a;
				char **p;
				for (a = 0, p = command->argv; *p; a++, p++) {
					debug_printf_clean("   argv[%d] = %s\n", a, *p);
				}
			}
			free_strings(command->argv);
			//command->argv = NULL;
		}
		/* not "else if": on syntax error, we may have both! */
		if (command->group) {
			debug_printf_clean("   begin group (cmd_type:%d)\n",
					command->cmd_type);
			free_pipe_list(command->group);
			debug_printf_clean("   end group\n");
			//command->group = NULL;
		}
		/* else is crucial here.
		 * If group != NULL, child_func is meaningless */
#if ENABLE_HUSH_FUNCTIONS
		else if (command->child_func) {
			debug_printf_exec("cmd %p releases child func at %p\n", command, command->child_func);
			command->child_func->parent_cmd = NULL;
		}
#endif
#if !BB_MMU
		free(command->group_as_string);
		//command->group_as_string = NULL;
#endif
		for (r = command->redirects; r; r = rnext) {
			debug_printf_clean("   redirect %d%s",
					r->rd_fd, redir_table[r->rd_type].descrip);
			/* guard against the case >$FOO, where foo is unset or blank */
			if (r->rd_filename) {
				debug_printf_clean(" fname:'%s'\n", r->rd_filename);
				free(r->rd_filename);
				//r->rd_filename = NULL;
			}
			debug_printf_clean(" rd_dup:%d\n", r->rd_dup);
			rnext = r->next;
			free(r);
		}
		//command->redirects = NULL;
	}
	free(pi->cmds);   /* children are an array, they get freed all at once */
	//pi->cmds = NULL;
#if ENABLE_HUSH_JOB
	free(pi->cmdtext);
	//pi->cmdtext = NULL;
#endif

	next = pi->next;
	free(pi);
	return next;
}

static void free_pipe_list(struct pipe *pi)
{
	while (pi) {
#if HAS_KEYWORDS
		debug_printf_clean("pipe reserved word %d\n", pi->res_word);
#endif
		debug_printf_clean("pipe followup code %d\n", pi->followup);
		pi = free_pipe(pi);
	}
}


/*** Parsing routines ***/

#ifndef debug_print_tree
static void debug_print_tree(struct pipe *pi, int lvl)
{
	static const char *const PIPE[] = {
		[PIPE_SEQ] = "SEQ",
		[PIPE_AND] = "AND",
		[PIPE_OR ] = "OR" ,
		[PIPE_BG ] = "BG" ,
	};
	static const char *RES[] = {
		[RES_NONE ] = "NONE" ,
# if ENABLE_HUSH_IF
		[RES_IF   ] = "IF"   ,
		[RES_THEN ] = "THEN" ,
		[RES_ELIF ] = "ELIF" ,
		[RES_ELSE ] = "ELSE" ,
		[RES_FI   ] = "FI"   ,
# endif
# if ENABLE_HUSH_LOOPS
		[RES_FOR  ] = "FOR"  ,
		[RES_WHILE] = "WHILE",
		[RES_UNTIL] = "UNTIL",
		[RES_DO   ] = "DO"   ,
		[RES_DONE ] = "DONE" ,
# endif
# if ENABLE_HUSH_LOOPS || ENABLE_HUSH_CASE
		[RES_IN   ] = "IN"   ,
# endif
# if ENABLE_HUSH_CASE
		[RES_CASE ] = "CASE" ,
		[RES_CASE_IN ] = "CASE_IN" ,
		[RES_MATCH] = "MATCH",
		[RES_CASE_BODY] = "CASE_BODY",
		[RES_ESAC ] = "ESAC" ,
# endif
		[RES_XXXX ] = "XXXX" ,
		[RES_SNTX ] = "SNTX" ,
	};
	static const char *const CMDTYPE[] = {
		"{}",
		"()",
		"[noglob]",
# if ENABLE_HUSH_FUNCTIONS
		"func()",
# endif
	};

	int pin, prn;

	pin = 0;
	while (pi) {
		fdprintf(2, "%*spipe %d %sres_word=%s followup=%d %s\n",
				lvl*2, "",
				pin,
				(IF_HAS_KEYWORDS(pi->pi_inverted ? "! " :) ""),
				RES[pi->res_word],
				pi->followup, PIPE[pi->followup]
		);
		prn = 0;
		while (prn < pi->num_cmds) {
			struct command *command = &pi->cmds[prn];
			char **argv = command->argv;

			fdprintf(2, "%*s cmd %d assignment_cnt:%d",
					lvl*2, "", prn,
					command->assignment_cnt);
#if ENABLE_HUSH_LINENO_VAR
			fdprintf(2, " LINENO:%u", command->lineno);
#endif
			if (command->group) {
				fdprintf(2, " group %s: (argv=%p)%s%s\n",
						CMDTYPE[command->cmd_type],
						argv
# if !BB_MMU
						, " group_as_string:", command->group_as_string
# else
						, "", ""
# endif
				);
				debug_print_tree(command->group, lvl+1);
				prn++;
				continue;
			}
			if (argv) while (*argv) {
				fdprintf(2, " '%s'", *argv);
				argv++;
			}
			fdprintf(2, "\n");
			prn++;
		}
		pi = pi->next;
		pin++;
	}
}
#endif /* debug_print_tree */

static struct pipe *new_pipe(void)
{
	struct pipe *pi;
	pi = xzalloc(sizeof(struct pipe));
	/*pi->res_word = RES_NONE; - RES_NONE is 0 anyway */
	return pi;
}

/* Command (member of a pipe) is complete, or we start a new pipe
 * if ctx->command is NULL.
 * No errors possible here.
 */
static int done_command(struct parse_context *ctx)
{
	/* The command is really already in the pipe structure, so
	 * advance the pipe counter and make a new, null command. */
	struct pipe *pi = ctx->pipe;
	struct command *command = ctx->command;

#if 0	/* Instead we emit error message at run time */
	if (ctx->pending_redirect) {
		/* For example, "cmd >" (no filename to redirect to) */
		syntax_error("invalid redirect");
		ctx->pending_redirect = NULL;
	}
#endif

	if (command) {
		if (IS_NULL_CMD(command)) {
			debug_printf_parse("done_command: skipping null cmd, num_cmds=%d\n", pi->num_cmds);
			goto clear_and_ret;
		}
		pi->num_cmds++;
		debug_printf_parse("done_command: ++num_cmds=%d\n", pi->num_cmds);
		//debug_print_tree(ctx->list_head, 20);
	} else {
		debug_printf_parse("done_command: initializing, num_cmds=%d\n", pi->num_cmds);
	}

	/* Only real trickiness here is that the uncommitted
	 * command structure is not counted in pi->num_cmds. */
	pi->cmds = xrealloc(pi->cmds, sizeof(*pi->cmds) * (pi->num_cmds+1));
	ctx->command = command = &pi->cmds[pi->num_cmds];
 clear_and_ret:
	memset(command, 0, sizeof(*command));
#if ENABLE_HUSH_LINENO_VAR
	command->lineno = G.lineno;
	debug_printf_parse("command->lineno = G.lineno (%u)\n", G.lineno);
#endif
	return pi->num_cmds; /* used only for 0/nonzero check */
}

static void done_pipe(struct parse_context *ctx, pipe_style type)
{
	int not_null;

	debug_printf_parse("done_pipe entered, followup %d\n", type);
	/* Close previous command */
	not_null = done_command(ctx);
#if HAS_KEYWORDS
	ctx->pipe->pi_inverted = ctx->ctx_inverted;
	ctx->ctx_inverted = 0;
	ctx->pipe->res_word = ctx->ctx_res_w;
#endif
	if (type == PIPE_BG && ctx->list_head != ctx->pipe) {
		/* Necessary since && and || have precedence over &:
		 * "cmd1 && cmd2 &" must spawn both cmds, not only cmd2,
		 * in a backgrounded subshell.
		 */
		struct pipe *pi;
		struct command *command;

		/* Is this actually this construct, all pipes end with && or ||? */
		pi = ctx->list_head;
		while (pi != ctx->pipe) {
			if (pi->followup != PIPE_AND && pi->followup != PIPE_OR)
				goto no_conv;
			pi = pi->next;
		}

		debug_printf_parse("BG with more than one pipe, converting to { p1 &&...pN; } &\n");
		pi->followup = PIPE_SEQ; /* close pN _not_ with "&"! */
		pi = xzalloc(sizeof(*pi));
		pi->followup = PIPE_BG;
		pi->num_cmds = 1;
		pi->cmds = xzalloc(sizeof(pi->cmds[0]));
		command = &pi->cmds[0];
		if (CMD_NORMAL != 0) /* "if xzalloc didn't do that already" */
			command->cmd_type = CMD_NORMAL;
		command->group = ctx->list_head;
#if !BB_MMU
		command->group_as_string = xstrndup(
			    ctx->as_string.data,
			    ctx->as_string.length - 1 /* do not copy last char, "&" */
		);
#endif
		/* Replace all pipes in ctx with one newly created */
		ctx->list_head = ctx->pipe = pi;
	} else {
 no_conv:
		ctx->pipe->followup = type;
	}

	/* Without this check, even just <enter> on command line generates
	 * tree of three NOPs (!). Which is harmless but annoying.
	 * IOW: it is safe to do it unconditionally. */
	if (not_null
#if ENABLE_HUSH_IF
	 || ctx->ctx_res_w == RES_FI
#endif
#if ENABLE_HUSH_LOOPS
	 || ctx->ctx_res_w == RES_DONE
	 || ctx->ctx_res_w == RES_FOR
	 || ctx->ctx_res_w == RES_IN
#endif
#if ENABLE_HUSH_CASE
	 || ctx->ctx_res_w == RES_ESAC
#endif
	) {
		struct pipe *new_p;
		debug_printf_parse("done_pipe: adding new pipe: "
				"not_null:%d ctx->ctx_res_w:%d\n",
				not_null, ctx->ctx_res_w);
		new_p = new_pipe();
		ctx->pipe->next = new_p;
		ctx->pipe = new_p;
		/* RES_THEN, RES_DO etc are "sticky" -
		 * they remain set for pipes inside if/while.
		 * This is used to control execution.
		 * RES_FOR and RES_IN are NOT sticky (needed to support
		 * cases where variable or value happens to match a keyword):
		 */
#if ENABLE_HUSH_LOOPS
		if (ctx->ctx_res_w == RES_FOR
		 || ctx->ctx_res_w == RES_IN)
			ctx->ctx_res_w = RES_NONE;
#endif
#if ENABLE_HUSH_CASE
		if (ctx->ctx_res_w == RES_MATCH)
			ctx->ctx_res_w = RES_CASE_BODY;
		if (ctx->ctx_res_w == RES_CASE)
			ctx->ctx_res_w = RES_CASE_IN;
#endif
		ctx->command = NULL; /* trick done_command below */
		/* Create the memory for command, roughly:
		 * ctx->pipe->cmds = new struct command;
		 * ctx->command = &ctx->pipe->cmds[0];
		 */
		done_command(ctx);
		//debug_print_tree(ctx->list_head, 10);
	}
	debug_printf_parse("done_pipe return\n");
}

static void initialize_context(struct parse_context *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	if (MAYBE_ASSIGNMENT != 0)
		ctx->is_assignment = MAYBE_ASSIGNMENT;
	ctx->pipe = ctx->list_head = new_pipe();
	/* Create the memory for command, roughly:
	 * ctx->pipe->cmds = new struct command;
	 * ctx->command = &ctx->pipe->cmds[0];
	 */
	done_command(ctx);
}

/* If a reserved word is found and processed, parse context is modified
 * and 1 is returned.
 */
#if HAS_KEYWORDS
struct reserved_combo {
	char literal[6];
	unsigned char res;
	unsigned char assignment_flag;
	int flag;
};
enum {
	FLAG_END   = (1 << RES_NONE ),
# if ENABLE_HUSH_IF
	FLAG_IF    = (1 << RES_IF   ),
	FLAG_THEN  = (1 << RES_THEN ),
	FLAG_ELIF  = (1 << RES_ELIF ),
	FLAG_ELSE  = (1 << RES_ELSE ),
	FLAG_FI    = (1 << RES_FI   ),
# endif
# if ENABLE_HUSH_LOOPS
	FLAG_FOR   = (1 << RES_FOR  ),
	FLAG_WHILE = (1 << RES_WHILE),
	FLAG_UNTIL = (1 << RES_UNTIL),
	FLAG_DO    = (1 << RES_DO   ),
	FLAG_DONE  = (1 << RES_DONE ),
	FLAG_IN    = (1 << RES_IN   ),
# endif
# if ENABLE_HUSH_CASE
	FLAG_MATCH = (1 << RES_MATCH),
	FLAG_ESAC  = (1 << RES_ESAC ),
# endif
	FLAG_START = (1 << RES_XXXX ),
};

static const struct reserved_combo* match_reserved_word(o_string *word)
{
	/* Mostly a list of accepted follow-up reserved words.
	 * FLAG_END means we are done with the sequence, and are ready
	 * to turn the compound list into a command.
	 * FLAG_START means the word must start a new compound list.
	 */
	static const struct reserved_combo reserved_list[] = {
# if ENABLE_HUSH_IF
		{ "!",     RES_NONE,  NOT_ASSIGNMENT  , 0 },
		{ "if",    RES_IF,    MAYBE_ASSIGNMENT, FLAG_THEN | FLAG_START },
		{ "then",  RES_THEN,  MAYBE_ASSIGNMENT, FLAG_ELIF | FLAG_ELSE | FLAG_FI },
		{ "elif",  RES_ELIF,  MAYBE_ASSIGNMENT, FLAG_THEN },
		{ "else",  RES_ELSE,  MAYBE_ASSIGNMENT, FLAG_FI   },
		{ "fi",    RES_FI,    NOT_ASSIGNMENT  , FLAG_END  },
# endif
# if ENABLE_HUSH_LOOPS
		{ "for",   RES_FOR,   NOT_ASSIGNMENT  , FLAG_IN | FLAG_DO | FLAG_START },
		{ "while", RES_WHILE, MAYBE_ASSIGNMENT, FLAG_DO | FLAG_START },
		{ "until", RES_UNTIL, MAYBE_ASSIGNMENT, FLAG_DO | FLAG_START },
		{ "in",    RES_IN,    NOT_ASSIGNMENT  , FLAG_DO   },
		{ "do",    RES_DO,    MAYBE_ASSIGNMENT, FLAG_DONE },
		{ "done",  RES_DONE,  NOT_ASSIGNMENT  , FLAG_END  },
# endif
# if ENABLE_HUSH_CASE
		{ "case",  RES_CASE,  NOT_ASSIGNMENT  , FLAG_MATCH | FLAG_START },
		{ "esac",  RES_ESAC,  NOT_ASSIGNMENT  , FLAG_END  },
# endif
	};
	const struct reserved_combo *r;

	for (r = reserved_list; r < reserved_list + ARRAY_SIZE(reserved_list); r++) {
		if (strcmp(word->data, r->literal) == 0)
			return r;
	}
	return NULL;
}
/* Return NULL: not a keyword, else: keyword
 */
static const struct reserved_combo* reserved_word(struct parse_context *ctx)
{
# if ENABLE_HUSH_CASE
	static const struct reserved_combo reserved_match = {
		"",        RES_MATCH, NOT_ASSIGNMENT , FLAG_MATCH | FLAG_ESAC
	};
# endif
	const struct reserved_combo *r;

	if (ctx->word.has_quoted_part)
		return 0;
	r = match_reserved_word(&ctx->word);
	if (!r)
		return r; /* NULL */

	debug_printf("found reserved word %s, res %d\n", r->literal, r->res);
# if ENABLE_HUSH_CASE
	if (r->res == RES_IN && ctx->ctx_res_w == RES_CASE_IN) {
		/* "case word IN ..." - IN part starts first MATCH part */
		r = &reserved_match;
	} else
# endif
	if (r->flag == 0) { /* '!' */
		if (ctx->ctx_inverted) { /* bash doesn't accept '! ! true' */
			syntax_error("! ! command");
			ctx->ctx_res_w = RES_SNTX;
		}
		ctx->ctx_inverted = 1;
		return r;
	}
	if (r->flag & FLAG_START) {
		struct parse_context *old;

		old = xmemdup(ctx, sizeof(*ctx));
		debug_printf_parse("push stack %p\n", old);
		initialize_context(ctx);
		ctx->stack = old;
	} else if (/*ctx->ctx_res_w == RES_NONE ||*/ !(ctx->old_flag & (1 << r->res))) {
		syntax_error_at(ctx->word.data);
		ctx->ctx_res_w = RES_SNTX;
		return r;
	} else {
		/* "{...} fi" is ok. "{...} if" is not
		 * Example:
		 * if { echo foo; } then { echo bar; } fi */
		if (ctx->command->group)
			done_pipe(ctx, PIPE_SEQ);
	}

	ctx->ctx_res_w = r->res;
	ctx->old_flag = r->flag;
	ctx->is_assignment = r->assignment_flag;
	debug_printf_parse("ctx->is_assignment='%s'\n", assignment_flag[ctx->is_assignment]);

	if (ctx->old_flag & FLAG_END) {
		struct parse_context *old;

		done_pipe(ctx, PIPE_SEQ);
		debug_printf_parse("pop stack %p\n", ctx->stack);
		old = ctx->stack;
		old->command->group = ctx->list_head;
		old->command->cmd_type = CMD_NORMAL;
# if !BB_MMU
		/* At this point, the compound command's string is in
		 * ctx->as_string... except for the leading keyword!
		 * Consider this example: "echo a | if true; then echo a; fi"
		 * ctx->as_string will contain "true; then echo a; fi",
		 * with "if " remaining in old->as_string!
		 */
		{
			char *str;
			int len = old->as_string.length;
			/* Concatenate halves */
			o_addstr(&old->as_string, ctx->as_string.data);
			o_free_unsafe(&ctx->as_string);
			/* Find where leading keyword starts in first half */
			str = old->as_string.data + len;
			if (str > old->as_string.data)
				str--; /* skip whitespace after keyword */
			while (str > old->as_string.data && isalpha(str[-1]))
				str--;
			/* Ugh, we're done with this horrid hack */
			old->command->group_as_string = xstrdup(str);
			debug_printf_parse("pop, remembering as:'%s'\n",
					old->command->group_as_string);
		}
# endif
		*ctx = *old;   /* physical copy */
		free(old);
	}
	return r;
}
#endif /* HAS_KEYWORDS */

/* Word is complete, look at it and update parsing context.
 * Normal return is 0. Syntax errors return 1.
 * Note: on return, word is reset, but not o_free'd!
 */
static int done_word(struct parse_context *ctx)
{
	struct command *command = ctx->command;

	debug_printf_parse("done_word entered: '%s' %p\n", ctx->word.data, command);
	if (ctx->word.length == 0 && !ctx->word.has_quoted_part) {
		debug_printf_parse("done_word return 0: true null, ignored\n");
		return 0;
	}

	if (ctx->pending_redirect) {
		/* We do not glob in e.g. >*.tmp case. bash seems to glob here
		 * only if run as "bash", not "sh" */
		/* http://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html
		 * "2.7 Redirection
		 * If the redirection operator is "<<" or "<<-", the word
		 * that follows the redirection operator shall be
		 * subjected to quote removal; it is unspecified whether
		 * any of the other expansions occur. For the other
		 * redirection operators, the word that follows the
		 * redirection operator shall be subjected to tilde
		 * expansion, parameter expansion, command substitution,
		 * arithmetic expansion, and quote removal.
		 * Pathname expansion shall not be performed
		 * on the word by a non-interactive shell; an interactive
		 * shell may perform it, but shall do so only when
		 * the expansion would result in one word."
		 */
//bash does not do parameter/command substitution or arithmetic expansion
//for _heredoc_ redirection word: these constructs look for exact eof marker
// as written:
// <<EOF$t
// <<EOF$((1))
// <<EOF`true`  [this case also makes heredoc "quoted", a-la <<"EOF". Probably bash-4.3.43 bug]

		ctx->pending_redirect->rd_filename = xstrdup(ctx->word.data);
		/* Cater for >\file case:
		 * >\a creates file a; >\\a, >"\a", >"\\a" create file \a
		 * Same with heredocs:
		 * for <<\H delim is H; <<\\H, <<"\H", <<"\\H" - \H
		 */
		if (ctx->pending_redirect->rd_type == REDIRECT_HEREDOC) {
			unbackslash(ctx->pending_redirect->rd_filename);
			/* Is it <<"HEREDOC"? */
			if (ctx->word.has_quoted_part) {
				ctx->pending_redirect->rd_dup |= HEREDOC_QUOTED;
			}
		}
		debug_printf_parse("word stored in rd_filename: '%s'\n", ctx->word.data);
		ctx->pending_redirect = NULL;
	} else {
#if HAS_KEYWORDS
# if ENABLE_HUSH_CASE
		if (ctx->ctx_dsemicolon
		 && strcmp(ctx->word.data, "esac") != 0 /* not "... pattern) cmd;; esac" */
		) {
			/* already done when ctx_dsemicolon was set to 1: */
			/* ctx->ctx_res_w = RES_MATCH; */
			ctx->ctx_dsemicolon = 0;
		} else
# endif
		if (!command->argv /* if it's the first word... */
# if ENABLE_HUSH_LOOPS
		 && ctx->ctx_res_w != RES_FOR /* ...not after FOR or IN */
		 && ctx->ctx_res_w != RES_IN
# endif
# if ENABLE_HUSH_CASE
		 && ctx->ctx_res_w != RES_CASE
# endif
		) {
			const struct reserved_combo *reserved;
			reserved = reserved_word(ctx);
			debug_printf_parse("checking for reserved-ness: %d\n", !!reserved);
			if (reserved) {
# if ENABLE_HUSH_LINENO_VAR
/* Case:
 * "while ...; do
 *	cmd ..."
 * If we don't close the pipe _now_, immediately after "do", lineno logic
 * sees "cmd" as starting at "do" - i.e., at the previous line.
 */
				if (0
				 IF_HUSH_IF(|| reserved->res == RES_THEN)
				 IF_HUSH_IF(|| reserved->res == RES_ELIF)
				 IF_HUSH_IF(|| reserved->res == RES_ELSE)
				 IF_HUSH_LOOPS(|| reserved->res == RES_DO)
				) {
					done_pipe(ctx, PIPE_SEQ);
				}
# endif
				o_reset_to_empty_unquoted(&ctx->word);
				debug_printf_parse("done_word return %d\n",
						(ctx->ctx_res_w == RES_SNTX));
				return (ctx->ctx_res_w == RES_SNTX);
			}
# if defined(CMD_SINGLEWORD_NOGLOB)
			if (0
#  if BASH_TEST2
			 || strcmp(ctx->word.data, "[[") == 0
#  endif
			/* In bash, local/export/readonly are special, args
			 * are assignments and therefore expansion of them
			 * should be "one-word" expansion:
			 *  $ export i=`echo 'a  b'` # one arg: "i=a  b"
			 * compare with:
			 *  $ ls i=`echo 'a  b'`     # two args: "i=a" and "b"
			 *  ls: cannot access i=a: No such file or directory
			 *  ls: cannot access b: No such file or directory
			 * Note: bash 3.2.33(1) does this only if export word
			 * itself is not quoted:
			 *  $ export i=`echo 'aaa  bbb'`; echo "$i"
			 *  aaa  bbb
			 *  $ "export" i=`echo 'aaa  bbb'`; echo "$i"
			 *  aaa
			 */
			 IF_HUSH_LOCAL(   || strcmp(ctx->word.data, "local") == 0)
			 IF_HUSH_EXPORT(  || strcmp(ctx->word.data, "export") == 0)
			 IF_HUSH_READONLY(|| strcmp(ctx->word.data, "readonly") == 0)
			) {
				command->cmd_type = CMD_SINGLEWORD_NOGLOB;
			}
			/* fall through */
# endif
		}
#endif /* HAS_KEYWORDS */

		if (command->group) {
			/* "{ echo foo; } echo bar" - bad */
			syntax_error_at(ctx->word.data);
			debug_printf_parse("done_word return 1: syntax error, "
					"groups and arglists don't mix\n");
			return 1;
		}

		/* If this word wasn't an assignment, next ones definitely
		 * can't be assignments. Even if they look like ones. */
		if (ctx->is_assignment != DEFINITELY_ASSIGNMENT
		 && ctx->is_assignment != WORD_IS_KEYWORD
		) {
			ctx->is_assignment = NOT_ASSIGNMENT;
		} else {
			if (ctx->is_assignment == DEFINITELY_ASSIGNMENT) {
				command->assignment_cnt++;
				debug_printf_parse("++assignment_cnt=%d\n", command->assignment_cnt);
			}
			debug_printf_parse("ctx->is_assignment was:'%s'\n", assignment_flag[ctx->is_assignment]);
			ctx->is_assignment = MAYBE_ASSIGNMENT;
		}
		debug_printf_parse("ctx->is_assignment='%s'\n", assignment_flag[ctx->is_assignment]);
		command->argv = add_string_to_strings(command->argv, xstrdup(ctx->word.data));
		debug_print_strings("word appended to argv", command->argv);
	}

#if ENABLE_HUSH_LOOPS
	if (ctx->ctx_res_w == RES_FOR) {
		if (ctx->word.has_quoted_part
		 || !is_well_formed_var_name(command->argv[0], '\0')
		) {
			/* bash says just "not a valid identifier" */
			syntax_error("not a valid identifier in for");
			return 1;
		}
		/* Force FOR to have just one word (variable name) */
		/* NB: basically, this makes hush see "for v in ..."
		 * syntax as if it is "for v; in ...". FOR and IN become
		 * two pipe structs in parse tree. */
		done_pipe(ctx, PIPE_SEQ);
	}
#endif
#if ENABLE_HUSH_CASE
	/* Force CASE to have just one word */
	if (ctx->ctx_res_w == RES_CASE) {
		done_pipe(ctx, PIPE_SEQ);
	}
#endif

	o_reset_to_empty_unquoted(&ctx->word);

	debug_printf_parse("done_word return 0\n");
	return 0;
}


/* Peek ahead in the input to find out if we have a "&n" construct,
 * as in "2>&1", that represents duplicating a file descriptor.
 * Return:
 * REDIRFD_CLOSE if >&- "close fd" construct is seen,
 * REDIRFD_SYNTAX_ERR if syntax error,
 * REDIRFD_TO_FILE if no & was seen,
 * or the number found.
 */
#if BB_MMU
#define parse_redir_right_fd(as_string, input) \
	parse_redir_right_fd(input)
#endif
static int parse_redir_right_fd(o_string *as_string, struct in_str *input)
{
	int ch, d, ok;

	ch = i_peek(input);
	if (ch != '&')
		return REDIRFD_TO_FILE;

	ch = i_getch(input);  /* get the & */
	nommu_addchr(as_string, ch);
	ch = i_peek(input);
	if (ch == '-') {
		ch = i_getch(input);
		nommu_addchr(as_string, ch);
		return REDIRFD_CLOSE;
	}
	d = 0;
	ok = 0;
	while (ch != EOF && isdigit(ch)) {
		d = d*10 + (ch-'0');
		ok = 1;
		ch = i_getch(input);
		nommu_addchr(as_string, ch);
		ch = i_peek(input);
	}
	if (ok) return d;

//TODO: this is the place to catch ">&file" bashism (redirect both fd 1 and 2)

	bb_error_msg("ambiguous redirect");
	return REDIRFD_SYNTAX_ERR;
}

/* Return code is 0 normal, 1 if a syntax error is detected
 */
static int parse_redirect(struct parse_context *ctx,
		int fd,
		redir_type style,
		struct in_str *input)
{
	struct command *command = ctx->command;
	struct redir_struct *redir;
	struct redir_struct **redirp;
	int dup_num;

	dup_num = REDIRFD_TO_FILE;
	if (style != REDIRECT_HEREDOC) {
		/* Check for a '>&1' type redirect */
		dup_num = parse_redir_right_fd(&ctx->as_string, input);
		if (dup_num == REDIRFD_SYNTAX_ERR)
			return 1;
	} else {
		int ch = i_peek_and_eat_bkslash_nl(input);
		dup_num = (ch == '-'); /* HEREDOC_SKIPTABS bit is 1 */
		if (dup_num) { /* <<-... */
			ch = i_getch(input);
			nommu_addchr(&ctx->as_string, ch);
			ch = i_peek(input);
		}
	}

	if (style == REDIRECT_OVERWRITE && dup_num == REDIRFD_TO_FILE) {
		int ch = i_peek_and_eat_bkslash_nl(input);
		if (ch == '|') {
			/* >|FILE redirect ("clobbering" >).
			 * Since we do not support "set -o noclobber" yet,
			 * >| and > are the same for now. Just eat |.
			 */
			ch = i_getch(input);
			nommu_addchr(&ctx->as_string, ch);
		}
	}

	/* Create a new redir_struct and append it to the linked list */
	redirp = &command->redirects;
	while ((redir = *redirp) != NULL) {
		redirp = &(redir->next);
	}
	*redirp = redir = xzalloc(sizeof(*redir));
	/* redir->next = NULL; */
	/* redir->rd_filename = NULL; */
	redir->rd_type = style;
	redir->rd_fd = (fd == -1) ? redir_table[style].default_fd : fd;

	debug_printf_parse("redirect type %d %s\n", redir->rd_fd,
				redir_table[style].descrip);

	redir->rd_dup = dup_num;
	if (style != REDIRECT_HEREDOC && dup_num != REDIRFD_TO_FILE) {
		/* Erik had a check here that the file descriptor in question
		 * is legit; I postpone that to "run time"
		 * A "-" representation of "close me" shows up as a -3 here */
		debug_printf_parse("duplicating redirect '%d>&%d'\n",
				redir->rd_fd, redir->rd_dup);
	} else {
#if 0		/* Instead we emit error message at run time */
		if (ctx->pending_redirect) {
			/* For example, "cmd > <file" */
			syntax_error("invalid redirect");
		}
#endif
		/* Set ctx->pending_redirect, so we know what to do at the
		 * end of the next parsed word. */
		ctx->pending_redirect = redir;
	}
	return 0;
}

/* If a redirect is immediately preceded by a number, that number is
 * supposed to tell which file descriptor to redirect.  This routine
 * looks for such preceding numbers.  In an ideal world this routine
 * needs to handle all the following classes of redirects...
 *     echo 2>foo     # redirects fd  2 to file "foo", nothing passed to echo
 *     echo 49>foo    # redirects fd 49 to file "foo", nothing passed to echo
 *     echo -2>foo    # redirects fd  1 to file "foo",    "-2" passed to echo
 *     echo 49x>foo   # redirects fd  1 to file "foo",   "49x" passed to echo
 *
 * http://www.opengroup.org/onlinepubs/009695399/utilities/xcu_chap02.html
 * "2.7 Redirection
 * ... If n is quoted, the number shall not be recognized as part of
 * the redirection expression. For example:
 * echo \2>a
 * writes the character 2 into file a"
 * We are getting it right by setting ->has_quoted_part on any \<char>
 *
 * A -1 return means no valid number was found,
 * the caller should use the appropriate default for this redirection.
 */
static int redirect_opt_num(o_string *o)
{
	int num;

	if (o->data == NULL)
		return -1;
	num = bb_strtou(o->data, NULL, 10);
	if (errno || num < 0)
		return -1;
	o_reset_to_empty_unquoted(o);
	return num;
}

#if BB_MMU
#define fetch_till_str(as_string, input, word, skip_tabs) \
	fetch_till_str(input, word, skip_tabs)
#endif
static char *fetch_till_str(o_string *as_string,
		struct in_str *input,
		const char *word,
		int heredoc_flags)
{
	o_string heredoc = NULL_O_STRING;
	unsigned past_EOL;
	int prev = 0; /* not \ */
	int ch;

	goto jump_in;

	while (1) {
		ch = i_getch(input);
		if (ch != EOF)
			nommu_addchr(as_string, ch);
		if (ch == '\n' || ch == EOF) {
 check_heredoc_end:
			if ((heredoc_flags & HEREDOC_QUOTED) || prev != '\\') {
				if (strcmp(heredoc.data + past_EOL, word) == 0) {
					heredoc.data[past_EOL] = '\0';
					debug_printf_parse("parsed heredoc '%s'\n", heredoc.data);
					return heredoc.data;
				}
				if (ch == '\n') {
					/* This is a new line.
					 * Remember position and backslash-escaping status.
					 */
					o_addchr(&heredoc, ch);
					prev = ch;
 jump_in:
					past_EOL = heredoc.length;
					/* Get 1st char of next line, possibly skipping leading tabs */
					do {
						ch = i_getch(input);
						if (ch != EOF)
							nommu_addchr(as_string, ch);
					} while ((heredoc_flags & HEREDOC_SKIPTABS) && ch == '\t');
					/* If this immediately ended the line,
					 * go back to end-of-line checks.
					 */
					if (ch == '\n')
						goto check_heredoc_end;
				}
			}
		}
		if (ch == EOF) {
			o_free_unsafe(&heredoc);
			return NULL;
		}
		o_addchr(&heredoc, ch);
		nommu_addchr(as_string, ch);
		if (prev == '\\' && ch == '\\')
			/* Correctly handle foo\\<eol> (not a line cont.) */
			prev = 0; /* not \ */
		else
			prev = ch;
	}
}

/* Look at entire parse tree for not-yet-loaded REDIRECT_HEREDOCs
 * and load them all. There should be exactly heredoc_cnt of them.
 */
static int fetch_heredocs(int heredoc_cnt, struct parse_context *ctx, struct in_str *input)
{
	struct pipe *pi = ctx->list_head;

	while (pi && heredoc_cnt) {
		int i;
		struct command *cmd = pi->cmds;

		debug_printf_parse("fetch_heredocs: num_cmds:%d cmd argv0:'%s'\n",
				pi->num_cmds,
				cmd->argv ? cmd->argv[0] : "NONE");
		for (i = 0; i < pi->num_cmds; i++) {
			struct redir_struct *redir = cmd->redirects;

			debug_printf_parse("fetch_heredocs: %d cmd argv0:'%s'\n",
					i, cmd->argv ? cmd->argv[0] : "NONE");
			while (redir) {
				if (redir->rd_type == REDIRECT_HEREDOC) {
					char *p;

					redir->rd_type = REDIRECT_HEREDOC2;
					/* redir->rd_dup is (ab)used to indicate <<- */
					p = fetch_till_str(&ctx->as_string, input,
							redir->rd_filename, redir->rd_dup);
					if (!p) {
						syntax_error("unexpected EOF in here document");
						return 1;
					}
					free(redir->rd_filename);
					redir->rd_filename = p;
					heredoc_cnt--;
				}
				redir = redir->next;
			}
			cmd++;
		}
		pi = pi->next;
	}
#if 0
	/* Should be 0. If it isn't, it's a parse error */
	if (heredoc_cnt)
		bb_error_msg_and_die("heredoc BUG 2");
#endif
	return 0;
}


static int run_list(struct pipe *pi);
#if BB_MMU
#define parse_stream(pstring, input, end_trigger) \
	parse_stream(input, end_trigger)
#endif
static struct pipe *parse_stream(char **pstring,
		struct in_str *input,
		int end_trigger);


static int parse_group(struct parse_context *ctx,
	struct in_str *input, int ch)
{
	/* ctx->word contains characters seen prior to ( or {.
	 * Typically it's empty, but for function defs,
	 * it contains function name (without '()'). */
#if BB_MMU
# define as_string NULL
#else
	char *as_string = NULL;
#endif
	struct pipe *pipe_list;
	int endch;
	struct command *command = ctx->command;

	debug_printf_parse("parse_group entered\n");
#if ENABLE_HUSH_FUNCTIONS
	if (ch == '(' && !ctx->word.has_quoted_part) {
		if (ctx->word.length)
			if (done_word(ctx))
				return 1;
		if (!command->argv)
			goto skip; /* (... */
		if (command->argv[1]) { /* word word ... (... */
			syntax_error_unexpected_ch('(');
			return 1;
		}
		/* it is "word(..." or "word (..." */
		do
			ch = i_getch(input);
		while (ch == ' ' || ch == '\t');
		if (ch != ')') {
			syntax_error_unexpected_ch(ch);
			return 1;
		}
		nommu_addchr(&ctx->as_string, ch);
		do
			ch = i_getch(input);
		while (ch == ' ' || ch == '\t' || ch == '\n');
		if (ch != '{' && ch != '(') {
			syntax_error_unexpected_ch(ch);
			return 1;
		}
		nommu_addchr(&ctx->as_string, ch);
		command->cmd_type = CMD_FUNCDEF;
		goto skip;
	}
#endif

#if 0 /* Prevented by caller */
	if (command->argv /* word [word]{... */
	 || ctx->word.length /* word{... */
	 || ctx->word.has_quoted_part /* ""{... */
	) {
		syntax_error(NULL);
		debug_printf_parse("parse_group return 1: "
			"syntax error, groups and arglists don't mix\n");
		return 1;
	}
#endif

 IF_HUSH_FUNCTIONS(skip:)

	endch = '}';
	if (ch == '(') {
		endch = ')';
		IF_HUSH_FUNCTIONS(if (command->cmd_type != CMD_FUNCDEF))
			command->cmd_type = CMD_SUBSHELL;
	} else {
		/* bash does not allow "{echo...", requires whitespace */
		ch = i_peek(input);
		if (ch != ' ' && ch != '\t' && ch != '\n'
		 && ch != '('	/* but "{(..." is allowed (without whitespace) */
		) {
			syntax_error_unexpected_ch(ch);
			return 1;
		}
		if (ch != '(') {
			ch = i_getch(input);
			nommu_addchr(&ctx->as_string, ch);
		}
	}

	pipe_list = parse_stream(&as_string, input, endch);
#if !BB_MMU
	if (as_string)
		o_addstr(&ctx->as_string, as_string);
#endif

	/* empty ()/{} or parse error? */
	if (!pipe_list || pipe_list == ERR_PTR) {
		/* parse_stream already emitted error msg */
		if (!BB_MMU)
			free(as_string);
		debug_printf_parse("parse_group return 1: "
			"parse_stream returned %p\n", pipe_list);
		return 1;
	}
#if !BB_MMU
	as_string[strlen(as_string) - 1] = '\0'; /* plink ')' or '}' */
	command->group_as_string = as_string;
	debug_printf_parse("end of group, remembering as:'%s'\n",
			command->group_as_string);
#endif

#if ENABLE_HUSH_FUNCTIONS
	/* Convert "f() (cmds)" to "f() {(cmds)}" */
	if (command->cmd_type == CMD_FUNCDEF && endch == ')') {
		struct command *cmd2;

		cmd2 = xzalloc(sizeof(*cmd2));
		cmd2->cmd_type = CMD_SUBSHELL;
		cmd2->group = pipe_list;
# if !BB_MMU
//UNTESTED!
		cmd2->group_as_string = command->group_as_string;
		command->group_as_string = xasprintf("(%s)", command->group_as_string);
# endif

		pipe_list = new_pipe();
		pipe_list->cmds = cmd2;
		pipe_list->num_cmds = 1;
	}
#endif

	command->group = pipe_list;

	debug_printf_parse("parse_group return 0\n");
	return 0;
	/* command remains "open", available for possible redirects */
#undef as_string
}

#if ENABLE_HUSH_TICK || ENABLE_FEATURE_SH_MATH || ENABLE_HUSH_DOLLAR_OPS
/* Subroutines for copying $(...) and `...` things */
static int add_till_backquote(o_string *dest, struct in_str *input, int in_dquote);
/* '...' */
static int add_till_single_quote(o_string *dest, struct in_str *input)
{
	while (1) {
		int ch = i_getch(input);
		if (ch == EOF) {
			syntax_error_unterm_ch('\'');
			return 0;
		}
		if (ch == '\'')
			return 1;
		o_addchr(dest, ch);
	}
}
/* "...\"...`..`...." - do we need to handle "...$(..)..." too? */
static int add_till_double_quote(o_string *dest, struct in_str *input)
{
	while (1) {
		int ch = i_getch(input);
		if (ch == EOF) {
			syntax_error_unterm_ch('"');
			return 0;
		}
		if (ch == '"')
			return 1;
		if (ch == '\\') {  /* \x. Copy both chars. */
			o_addchr(dest, ch);
			ch = i_getch(input);
		}
		o_addchr(dest, ch);
		if (ch == '`') {
			if (!add_till_backquote(dest, input, /*in_dquote:*/ 1))
				return 0;
			o_addchr(dest, ch);
			continue;
		}
		//if (ch == '$') ...
	}
}
/* Process `cmd` - copy contents until "`" is seen. Complicated by
 * \` quoting.
 * "Within the backquoted style of command substitution, backslash
 * shall retain its literal meaning, except when followed by: '$', '`', or '\'.
 * The search for the matching backquote shall be satisfied by the first
 * backquote found without a preceding backslash; during this search,
 * if a non-escaped backquote is encountered within a shell comment,
 * a here-document, an embedded command substitution of the $(command)
 * form, or a quoted string, undefined results occur. A single-quoted
 * or double-quoted string that begins, but does not end, within the
 * "`...`" sequence produces undefined results."
 * Example                               Output
 * echo `echo '\'TEST\`echo ZZ\`BEST`    \TESTZZBEST
 */
static int add_till_backquote(o_string *dest, struct in_str *input, int in_dquote)
{
	while (1) {
		int ch = i_getch(input);
		if (ch == '`')
			return 1;
		if (ch == '\\') {
			/* \x. Copy both unless it is \`, \$, \\ and maybe \" */
			ch = i_getch(input);
			if (ch != '`'
			 && ch != '$'
			 && ch != '\\'
			 && (!in_dquote || ch != '"')
			) {
				o_addchr(dest, '\\');
			}
		}
		if (ch == EOF) {
			syntax_error_unterm_ch('`');
			return 0;
		}
		o_addchr(dest, ch);
	}
}
/* Process $(cmd) - copy contents until ")" is seen. Complicated by
 * quoting and nested ()s.
 * "With the $(command) style of command substitution, all characters
 * following the open parenthesis to the matching closing parenthesis
 * constitute the command. Any valid shell script can be used for command,
 * except a script consisting solely of redirections which produces
 * unspecified results."
 * Example                              Output
 * echo $(echo '(TEST)' BEST)           (TEST) BEST
 * echo $(echo 'TEST)' BEST)            TEST) BEST
 * echo $(echo \(\(TEST\) BEST)         ((TEST) BEST
 *
 * Also adapted to eat ${var%...} and $((...)) constructs, since ... part
 * can contain arbitrary constructs, just like $(cmd).
 * In bash compat mode, it needs to also be able to stop on ':' or '/'
 * for ${var:N[:M]} and ${var/P[/R]} parsing.
 */
#define DOUBLE_CLOSE_CHAR_FLAG 0x80
static int add_till_closing_bracket(o_string *dest, struct in_str *input, unsigned end_ch)
{
	int ch;
	char dbl = end_ch & DOUBLE_CLOSE_CHAR_FLAG;
# if BASH_SUBSTR || BASH_PATTERN_SUBST
	char end_char2 = end_ch >> 8;
# endif
	end_ch &= (DOUBLE_CLOSE_CHAR_FLAG - 1);

#if ENABLE_HUSH_INTERACTIVE
	G.promptmode = 1; /* PS2 */
#endif
	debug_printf_prompt("%s promptmode=%d\n", __func__, G.promptmode);

	while (1) {
		ch = i_getch(input);
		if (ch == EOF) {
			syntax_error_unterm_ch(end_ch);
			return 0;
		}
		if (ch == end_ch
# if BASH_SUBSTR || BASH_PATTERN_SUBST
		 || ch == end_char2
# endif
		) {
			if (!dbl)
				break;
			/* we look for closing )) of $((EXPR)) */
			if (i_peek_and_eat_bkslash_nl(input) == end_ch) {
				i_getch(input); /* eat second ')' */
				break;
			}
		}
		o_addchr(dest, ch);
		//bb_error_msg("%s:o_addchr('%c')", __func__, ch);
		if (ch == '(' || ch == '{') {
			ch = (ch == '(' ? ')' : '}');
			if (!add_till_closing_bracket(dest, input, ch))
				return 0;
			o_addchr(dest, ch);
			continue;
		}
		if (ch == '\'') {
			if (!add_till_single_quote(dest, input))
				return 0;
			o_addchr(dest, ch);
			continue;
		}
		if (ch == '"') {
			if (!add_till_double_quote(dest, input))
				return 0;
			o_addchr(dest, ch);
			continue;
		}
		if (ch == '`') {
			if (!add_till_backquote(dest, input, /*in_dquote:*/ 0))
				return 0;
			o_addchr(dest, ch);
			continue;
		}
		if (ch == '\\') {
			/* \x. Copy verbatim. Important for  \(, \) */
			ch = i_getch(input);
			if (ch == EOF) {
				syntax_error_unterm_ch(end_ch);
				return 0;
			}
#if 0
			if (ch == '\n') {
				/* "backslash+newline", ignore both */
				o_delchr(dest); /* undo insertion of '\' */
				continue;
			}
#endif
			o_addchr(dest, ch);
			//bb_error_msg("%s:o_addchr('%c') after '\\'", __func__, ch);
			continue;
		}
	}
	debug_printf_parse("%s return '%s' ch:'%c'\n", __func__, dest->data, ch);
	return ch;
}
#endif /* ENABLE_HUSH_TICK || ENABLE_FEATURE_SH_MATH || ENABLE_HUSH_DOLLAR_OPS */

/* Return code: 0 for OK, 1 for syntax error */
#if BB_MMU
#define parse_dollar(as_string, dest, input, quote_mask) \
	parse_dollar(dest, input, quote_mask)
#define as_string NULL
#endif
static int parse_dollar(o_string *as_string,
		o_string *dest,
		struct in_str *input, unsigned char quote_mask)
{
	int ch = i_peek_and_eat_bkslash_nl(input);  /* first character after the $ */

	debug_printf_parse("parse_dollar entered: ch='%c'\n", ch);
	if (isalpha(ch)) {
 make_var:
		ch = i_getch(input);
		nommu_addchr(as_string, ch);
 /*make_var1:*/
		o_addchr(dest, SPECIAL_VAR_SYMBOL);
		while (1) {
			debug_printf_parse(": '%c'\n", ch);
			o_addchr(dest, ch | quote_mask);
			quote_mask = 0;
			ch = i_peek_and_eat_bkslash_nl(input);
			if (!isalnum(ch) && ch != '_') {
				/* End of variable name reached */
				break;
			}
			ch = i_getch(input);
			nommu_addchr(as_string, ch);
		}
		o_addchr(dest, SPECIAL_VAR_SYMBOL);
	} else if (isdigit(ch)) {
 make_one_char_var:
		ch = i_getch(input);
		nommu_addchr(as_string, ch);
		o_addchr(dest, SPECIAL_VAR_SYMBOL);
		debug_printf_parse(": '%c'\n", ch);
		o_addchr(dest, ch | quote_mask);
		o_addchr(dest, SPECIAL_VAR_SYMBOL);
	} else switch (ch) {
	case '$': /* pid */
	case '!': /* last bg pid */
	case '?': /* last exit code */
	case '#': /* number of args */
	case '*': /* args */
	case '@': /* args */
		goto make_one_char_var;
	case '{': {
		char len_single_ch;

		o_addchr(dest, SPECIAL_VAR_SYMBOL);

		ch = i_getch(input); /* eat '{' */
		nommu_addchr(as_string, ch);

		ch = i_getch_and_eat_bkslash_nl(input); /* first char after '{' */
		/* It should be ${?}, or ${#var},
		 * or even ${?+subst} - operator acting on a special variable,
		 * or the beginning of variable name.
		 */
		if (ch == EOF
		 || (!strchr(_SPECIAL_VARS_STR, ch) && !isalnum(ch)) /* not one of those */
		) {
 bad_dollar_syntax:
			syntax_error_unterm_str("${name}");
			debug_printf_parse("parse_dollar return 0: unterminated ${name}\n");
			return 0;
		}
		nommu_addchr(as_string, ch);
		len_single_ch = ch;
		ch |= quote_mask;

		/* It's possible to just call add_till_closing_bracket() at this point.
		 * However, this regresses some of our testsuite cases
		 * which check invalid constructs like ${%}.
		 * Oh well... let's check that the var name part is fine... */

		while (1) {
			unsigned pos;

			o_addchr(dest, ch);
			debug_printf_parse(": '%c'\n", ch);

			ch = i_getch(input);
			nommu_addchr(as_string, ch);
			if (ch == '}')
				break;

			if (!isalnum(ch) && ch != '_') {
				unsigned end_ch;
				unsigned char last_ch;
				/* handle parameter expansions
				 * http://www.opengroup.org/onlinepubs/009695399/utilities/xcu_chap02.html#tag_02_06_02
				 */
				if (!strchr(VAR_SUBST_OPS, ch)) { /* ${var<bad_char>... */
					if (len_single_ch != '#'
					/*|| !strchr(SPECIAL_VARS_STR, ch) - disallow errors like ${#+} ? */
					 || i_peek(input) != '}'
					) {
						goto bad_dollar_syntax;
					}
					/* else: it's "length of C" ${#C} op,
					 * where C is a single char
					 * special var name, e.g. ${#!}.
					 */
				}
				/* Eat everything until closing '}' (or ':') */
				end_ch = '}';
				if (BASH_SUBSTR
				 && ch == ':'
				 && !strchr(MINUS_PLUS_EQUAL_QUESTION, i_peek(input))
				) {
					/* It's ${var:N[:M]} thing */
					end_ch = '}' * 0x100 + ':';
				}
				if (BASH_PATTERN_SUBST
				 && ch == '/'
				) {
					/* It's ${var/[/]pattern[/repl]} thing */
					if (i_peek(input) == '/') { /* ${var//pattern[/repl]}? */
						i_getch(input);
						nommu_addchr(as_string, '/');
						ch = '\\';
					}
					end_ch = '}' * 0x100 + '/';
				}
				o_addchr(dest, ch);
 again:
				if (!BB_MMU)
					pos = dest->length;
#if ENABLE_HUSH_DOLLAR_OPS
				last_ch = add_till_closing_bracket(dest, input, end_ch);
				if (last_ch == 0) /* error? */
					return 0;
#else
#error Simple code to only allow ${var} is not implemented
#endif
				if (as_string) {
					o_addstr(as_string, dest->data + pos);
					o_addchr(as_string, last_ch);
				}

				if ((BASH_SUBSTR || BASH_PATTERN_SUBST)
					 && (end_ch & 0xff00)
				) {
					/* close the first block: */
					o_addchr(dest, SPECIAL_VAR_SYMBOL);
					/* while parsing N from ${var:N[:M]}
					 * or pattern from ${var/[/]pattern[/repl]} */
					if ((end_ch & 0xff) == last_ch) {
						/* got ':' or '/'- parse the rest */
						end_ch = '}';
						goto again;
					}
					/* got '}' */
					if (BASH_SUBSTR && end_ch == '}' * 0x100 + ':') {
						/* it's ${var:N} - emulate :999999999 */
						o_addstr(dest, "999999999");
					} /* else: it's ${var/[/]pattern} */
				}
				break;
			}
			len_single_ch = 0; /* it can't be ${#C} op */
		}
		o_addchr(dest, SPECIAL_VAR_SYMBOL);
		break;
	}
#if ENABLE_FEATURE_SH_MATH || ENABLE_HUSH_TICK
	case '(': {
		unsigned pos;

		ch = i_getch(input);
		nommu_addchr(as_string, ch);
# if ENABLE_FEATURE_SH_MATH
		if (i_peek_and_eat_bkslash_nl(input) == '(') {
			ch = i_getch(input);
			nommu_addchr(as_string, ch);
			o_addchr(dest, SPECIAL_VAR_SYMBOL);
			o_addchr(dest, /*quote_mask |*/ '+');
			if (!BB_MMU)
				pos = dest->length;
			if (!add_till_closing_bracket(dest, input, ')' | DOUBLE_CLOSE_CHAR_FLAG))
				return 0; /* error */
			if (as_string) {
				o_addstr(as_string, dest->data + pos);
				o_addchr(as_string, ')');
				o_addchr(as_string, ')');
			}
			o_addchr(dest, SPECIAL_VAR_SYMBOL);
			break;
		}
# endif
# if ENABLE_HUSH_TICK
		o_addchr(dest, SPECIAL_VAR_SYMBOL);
		o_addchr(dest, quote_mask | '`');
		if (!BB_MMU)
			pos = dest->length;
		if (!add_till_closing_bracket(dest, input, ')'))
			return 0; /* error */
		if (as_string) {
			o_addstr(as_string, dest->data + pos);
			o_addchr(as_string, ')');
		}
		o_addchr(dest, SPECIAL_VAR_SYMBOL);
# endif
		break;
	}
#endif
	case '_':
		goto make_var;
#if 0
	/* TODO: $_ and $-: */
	/* $_ Shell or shell script name; or last argument of last command
	 * (if last command wasn't a pipe; if it was, bash sets $_ to "");
	 * but in command's env, set to full pathname used to invoke it */
	/* $- Option flags set by set builtin or shell options (-i etc) */
		ch = i_getch(input);
		nommu_addchr(as_string, ch);
		ch = i_peek_and_eat_bkslash_nl(input);
		if (isalnum(ch)) { /* it's $_name or $_123 */
			ch = '_';
			goto make_var1;
		}
		/* else: it's $_ */
#endif
	default:
		o_addQchr(dest, '$');
	}
	debug_printf_parse("parse_dollar return 1 (ok)\n");
	return 1;
#undef as_string
}

#if BB_MMU
# if BASH_PATTERN_SUBST
#define encode_string(as_string, dest, input, dquote_end, process_bkslash) \
	encode_string(dest, input, dquote_end, process_bkslash)
# else
/* only ${var/pattern/repl} (its pattern part) needs additional mode */
#define encode_string(as_string, dest, input, dquote_end, process_bkslash) \
	encode_string(dest, input, dquote_end)
# endif
#define as_string NULL

#else /* !MMU */

# if BASH_PATTERN_SUBST
/* all parameters are needed, no macro tricks */
# else
#define encode_string(as_string, dest, input, dquote_end, process_bkslash) \
	encode_string(as_string, dest, input, dquote_end)
# endif
#endif
static int encode_string(o_string *as_string,
		o_string *dest,
		struct in_str *input,
		int dquote_end,
		int process_bkslash)
{
#if !BASH_PATTERN_SUBST
	const int process_bkslash = 1;
#endif
	int ch;
	int next;

 again:
	ch = i_getch(input);
	if (ch != EOF)
		nommu_addchr(as_string, ch);
	if (ch == dquote_end) { /* may be only '"' or EOF */
		debug_printf_parse("encode_string return 1 (ok)\n");
		return 1;
	}
	/* note: can't move it above ch == dquote_end check! */
	if (ch == EOF) {
		syntax_error_unterm_ch('"');
		return 0; /* error */
	}
	next = '\0';
	if (ch != '\n') {
		next = i_peek(input);
	}
	debug_printf_parse("\" ch=%c (%d) escape=%d\n",
			ch, ch, !!(dest->o_expflags & EXP_FLAG_ESC_GLOB_CHARS));
	if (process_bkslash && ch == '\\') {
		if (next == EOF) {
			/* Testcase: in interactive shell a file with
			 *  echo "unterminated string\<eof>
			 * is sourced.
			 */
			syntax_error_unterm_ch('"');
			return 0; /* error */
		}
		/* bash:
		 * "The backslash retains its special meaning [in "..."]
		 * only when followed by one of the following characters:
		 * $, `, ", \, or <newline>.  A double quote may be quoted
		 * within double quotes by preceding it with a backslash."
		 * NB: in (unquoted) heredoc, above does not apply to ",
		 * therefore we check for it by "next == dquote_end" cond.
		 */
		if (next == dquote_end || strchr("$`\\\n", next)) {
			ch = i_getch(input); /* eat next */
			if (ch == '\n')
				goto again; /* skip \<newline> */
		} /* else: ch remains == '\\', and we double it below: */
		o_addqchr(dest, ch); /* \c if c is a glob char, else just c */
		nommu_addchr(as_string, ch);
		goto again;
	}
	if (ch == '$') {
		if (!parse_dollar(as_string, dest, input, /*quote_mask:*/ 0x80)) {
			debug_printf_parse("encode_string return 0: "
					"parse_dollar returned 0 (error)\n");
			return 0;
		}
		goto again;
	}
#if ENABLE_HUSH_TICK
	if (ch == '`') {
		//unsigned pos = dest->length;
		o_addchr(dest, SPECIAL_VAR_SYMBOL);
		o_addchr(dest, 0x80 | '`');
		if (!add_till_backquote(dest, input, /*in_dquote:*/ dquote_end == '"'))
			return 0; /* error */
		o_addchr(dest, SPECIAL_VAR_SYMBOL);
		//debug_printf_subst("SUBST RES3 '%s'\n", dest->data + pos);
		goto again;
	}
#endif
	o_addQchr(dest, ch);
	goto again;
#undef as_string
}

/*
 * Scan input until EOF or end_trigger char.
 * Return a list of pipes to execute, or NULL on EOF
 * or if end_trigger character is met.
 * On syntax error, exit if shell is not interactive,
 * reset parsing machinery and start parsing anew,
 * or return ERR_PTR.
 */
static struct pipe *parse_stream(char **pstring,
		struct in_str *input,
		int end_trigger)
{
	struct parse_context ctx;
	int heredoc_cnt;

	/* Single-quote triggers a bypass of the main loop until its mate is
	 * found.  When recursing, quote state is passed in via ctx.word.o_expflags.
	 */
	debug_printf_parse("parse_stream entered, end_trigger='%c'\n",
			end_trigger ? end_trigger : 'X');
	debug_enter();

	initialize_context(&ctx);

	/* If very first arg is "" or '', ctx.word.data may end up NULL.
	 * Preventing this:
	 */
	o_addchr(&ctx.word, '\0');
	ctx.word.length = 0;

	/* We used to separate words on $IFS here. This was wrong.
	 * $IFS is used only for word splitting when $var is expanded,
	 * here we should use blank chars as separators, not $IFS
	 */

	heredoc_cnt = 0;
	while (1) {
		const char *is_blank;
		const char *is_special;
		int ch;
		int next;
		int redir_fd;
		redir_type redir_style;

		ch = i_getch(input);
		debug_printf_parse(": ch=%c (%d) escape=%d\n",
				ch, ch, !!(ctx.word.o_expflags & EXP_FLAG_ESC_GLOB_CHARS));
		if (ch == EOF) {
			struct pipe *pi;

			if (heredoc_cnt) {
				syntax_error_unterm_str("here document");
				goto parse_error;
			}
			if (end_trigger == ')') {
				syntax_error_unterm_ch('(');
				goto parse_error;
			}
			if (end_trigger == '}') {
				syntax_error_unterm_ch('{');
				goto parse_error;
			}

			if (done_word(&ctx)) {
				goto parse_error;
			}
			o_free(&ctx.word);
			done_pipe(&ctx, PIPE_SEQ);
			pi = ctx.list_head;
			/* If we got nothing... */
			/* (this makes bare "&" cmd a no-op.
			 * bash says: "syntax error near unexpected token '&'") */
			if (pi->num_cmds == 0
			IF_HAS_KEYWORDS(&& pi->res_word == RES_NONE)
			) {
				free_pipe_list(pi);
				pi = NULL;
			}
#if !BB_MMU
			debug_printf_parse("as_string1 '%s'\n", ctx.as_string.data);
			if (pstring)
				*pstring = ctx.as_string.data;
			else
				o_free_unsafe(&ctx.as_string);
#endif
			debug_leave();
			debug_printf_parse("parse_stream return %p\n", pi);
			return pi;
		}

		/* Handle "'" and "\" first, as they won't play nice with
		 * i_peek_and_eat_bkslash_nl() anyway:
		 *   echo z\\
		 * and
		 *   echo '\
		 *   '
		 * would break.
		 */
		if (ch == '\\') {
			ch = i_getch(input);
			if (ch == '\n')
				continue; /* drop \<newline>, get next char */
			nommu_addchr(&ctx.as_string, '\\');
			o_addchr(&ctx.word, '\\');
			if (ch == EOF) {
				/* Testcase: eval 'echo Ok\' */
				/* bash-4.3.43 was removing backslash,
				 * but 4.4.19 retains it, most other shells too
				 */
				continue; /* get next char */
			}
			/* Example: echo Hello \2>file
			 * we need to know that word 2 is quoted
			 */
			ctx.word.has_quoted_part = 1;
			nommu_addchr(&ctx.as_string, ch);
			o_addchr(&ctx.word, ch);
			continue; /* get next char */
		}
		nommu_addchr(&ctx.as_string, ch);
		if (ch == '\'') {
			ctx.word.has_quoted_part = 1;
			next = i_getch(input);
			if (next == '\'' && !ctx.pending_redirect)
				goto insert_empty_quoted_str_marker;

			ch = next;
			while (1) {
				if (ch == EOF) {
					syntax_error_unterm_ch('\'');
					goto parse_error;
				}
				nommu_addchr(&ctx.as_string, ch);
				if (ch == '\'')
					break;
				if (ch == SPECIAL_VAR_SYMBOL) {
					/* Convert raw ^C to corresponding special variable reference */
					o_addchr(&ctx.word, SPECIAL_VAR_SYMBOL);
					o_addchr(&ctx.word, SPECIAL_VAR_QUOTED_SVS);
				}
				o_addqchr(&ctx.word, ch);
				ch = i_getch(input);
			}
			continue; /* get next char */
		}

		next = '\0';
		if (ch != '\n')
			next = i_peek_and_eat_bkslash_nl(input);

		is_special = "{}<>;&|()#" /* special outside of "str" */
				"$\"" IF_HUSH_TICK("`") /* always special */
				SPECIAL_VAR_SYMBOL_STR;
		/* Are { and } special here? */
		if (ctx.command->argv /* word [word]{... - non-special */
		 || ctx.word.length       /* word{... - non-special */
		 || ctx.word.has_quoted_part     /* ""{... - non-special */
		 || (next != ';'             /* }; - special */
		    && next != ')'           /* }) - special */
		    && next != '('           /* {( - special */
		    && next != '&'           /* }& and }&& ... - special */
		    && next != '|'           /* }|| ... - special */
		    && !strchr(defifs, next) /* {word - non-special */
		    )
		) {
			/* They are not special, skip "{}" */
			is_special += 2;
		}
		is_special = strchr(is_special, ch);
		is_blank = strchr(defifs, ch);

		if (!is_special && !is_blank) { /* ordinary char */
 ordinary_char:
			o_addQchr(&ctx.word, ch);
			if ((ctx.is_assignment == MAYBE_ASSIGNMENT
			    || ctx.is_assignment == WORD_IS_KEYWORD)
			 && ch == '='
			 && is_well_formed_var_name(ctx.word.data, '=')
			) {
				ctx.is_assignment = DEFINITELY_ASSIGNMENT;
				debug_printf_parse("ctx.is_assignment='%s'\n", assignment_flag[ctx.is_assignment]);
			}
			continue;
		}

		if (is_blank) {
#if ENABLE_HUSH_LINENO_VAR
/* Case:
 * "while ...; do<whitespace><newline>
 *	cmd ..."
 * would think that "cmd" starts in <whitespace> -
 * i.e., at the previous line.
 * We need to skip all whitespace before newlines.
 */
			while (ch != '\n') {
				next = i_peek(input);
				if (next != ' ' && next != '\t' && next != '\n')
					break; /* next char is not ws */
				ch = i_getch(input);
			}
			/* ch == last eaten whitespace char */
#endif
			if (done_word(&ctx)) {
				goto parse_error;
			}
			if (ch == '\n') {
				/* Is this a case when newline is simply ignored?
				 * Some examples:
				 * "cmd | <newline> cmd ..."
				 * "case ... in <newline> word) ..."
				 */
				if (IS_NULL_CMD(ctx.command)
				 && ctx.word.length == 0 && !ctx.word.has_quoted_part
				) {
					/* This newline can be ignored. But...
					 * Without check #1, interactive shell
					 * ignores even bare <newline>,
					 * and shows the continuation prompt:
					 * ps1_prompt$ <enter>
					 * ps2> _   <=== wrong, should be ps1
					 * Without check #2, "cmd & <newline>"
					 * is similarly mistreated.
					 * (BTW, this makes "cmd & cmd"
					 * and "cmd && cmd" non-orthogonal.
					 * Really, ask yourself, why
					 * "cmd && <newline>" doesn't start
					 * cmd but waits for more input?
					 * The only reason is that it might be
					 * a "cmd1 && <nl> cmd2 &" construct,
					 * cmd1 may need to run in BG).
					 */
					struct pipe *pi = ctx.list_head;
					if (pi->num_cmds != 0       /* check #1 */
					 && pi->followup != PIPE_BG /* check #2 */
					) {
						continue;
					}
				}
				/* Treat newline as a command separator. */
				done_pipe(&ctx, PIPE_SEQ);
				debug_printf_parse("heredoc_cnt:%d\n", heredoc_cnt);
				if (heredoc_cnt) {
					if (fetch_heredocs(heredoc_cnt, &ctx, input)) {
						goto parse_error;
					}
					heredoc_cnt = 0;
				}
				ctx.is_assignment = MAYBE_ASSIGNMENT;
				debug_printf_parse("ctx.is_assignment='%s'\n", assignment_flag[ctx.is_assignment]);
				ch = ';';
				/* note: if (is_blank) continue;
				 * will still trigger for us */
			}
		}

		/* "cmd}" or "cmd }..." without semicolon or &:
		 * } is an ordinary char in this case, even inside { cmd; }
		 * Pathological example: { ""}; } should exec "}" cmd
		 */
		if (ch == '}') {
			if (ctx.word.length != 0 /* word} */
			 || ctx.word.has_quoted_part    /* ""} */
			) {
				goto ordinary_char;
			}
			if (!IS_NULL_CMD(ctx.command)) { /* cmd } */
				/* Generally, there should be semicolon: "cmd; }"
				 * However, bash allows to omit it if "cmd" is
				 * a group. Examples:
				 * { { echo 1; } }
				 * {(echo 1)}
				 * { echo 0 >&2 | { echo 1; } }
				 * { while false; do :; done }
				 * { case a in b) ;; esac }
				 */
				if (ctx.command->group)
					goto term_group;
				goto ordinary_char;
			}
			if (!IS_NULL_PIPE(ctx.pipe)) /* cmd | } */
				/* Can't be an end of {cmd}, skip the check */
				goto skip_end_trigger;
			/* else: } does terminate a group */
		}
 term_group:
		if (end_trigger && end_trigger == ch
		 && (ch != ';' || heredoc_cnt == 0)
#if ENABLE_HUSH_CASE
		 && (ch != ')'
		    || ctx.ctx_res_w != RES_MATCH
		    || (!ctx.word.has_quoted_part && strcmp(ctx.word.data, "esac") == 0)
		    )
#endif
		) {
			if (heredoc_cnt) {
				/* This is technically valid:
				 * { cat <<HERE; }; echo Ok
				 * heredoc
				 * heredoc
				 * HERE
				 * but we don't support this.
				 * We require heredoc to be in enclosing {}/(),
				 * if any.
				 */
				syntax_error_unterm_str("here document");
				goto parse_error;
			}
			if (done_word(&ctx)) {
				goto parse_error;
			}
			done_pipe(&ctx, PIPE_SEQ);
			ctx.is_assignment = MAYBE_ASSIGNMENT;
			debug_printf_parse("ctx.is_assignment='%s'\n", assignment_flag[ctx.is_assignment]);
			/* Do we sit outside of any if's, loops or case's? */
			if (!HAS_KEYWORDS
			IF_HAS_KEYWORDS(|| (ctx.ctx_res_w == RES_NONE && ctx.old_flag == 0))
			) {
				o_free(&ctx.word);
#if !BB_MMU
				debug_printf_parse("as_string2 '%s'\n", ctx.as_string.data);
				if (pstring)
					*pstring = ctx.as_string.data;
				else
					o_free_unsafe(&ctx.as_string);
#endif
				if (ch != ';' && IS_NULL_PIPE(ctx.list_head)) {
					/* Example: bare "{ }", "()" */
					G.last_exitcode = 2; /* bash compat */
					syntax_error_unexpected_ch(ch);
					goto parse_error2;
				}
				debug_printf_parse("parse_stream return %p: "
						"end_trigger char found\n",
						ctx.list_head);
				debug_leave();
				return ctx.list_head;
			}
		}

		if (is_blank)
			continue;

		/* Catch <, > before deciding whether this word is
		 * an assignment. a=1 2>z b=2: b=2 is still assignment */
		switch (ch) {
		case '>':
			redir_fd = redirect_opt_num(&ctx.word);
			if (done_word(&ctx)) {
				goto parse_error;
			}
			redir_style = REDIRECT_OVERWRITE;
			if (next == '>') {
				redir_style = REDIRECT_APPEND;
				ch = i_getch(input);
				nommu_addchr(&ctx.as_string, ch);
			}
#if 0
			else if (next == '(') {
				syntax_error(">(process) not supported");
				goto parse_error;
			}
#endif
			if (parse_redirect(&ctx, redir_fd, redir_style, input))
				goto parse_error;
			continue; /* get next char */
		case '<':
			redir_fd = redirect_opt_num(&ctx.word);
			if (done_word(&ctx)) {
				goto parse_error;
			}
			redir_style = REDIRECT_INPUT;
			if (next == '<') {
				redir_style = REDIRECT_HEREDOC;
				heredoc_cnt++;
				debug_printf_parse("++heredoc_cnt=%d\n", heredoc_cnt);
				ch = i_getch(input);
				nommu_addchr(&ctx.as_string, ch);
			} else if (next == '>') {
				redir_style = REDIRECT_IO;
				ch = i_getch(input);
				nommu_addchr(&ctx.as_string, ch);
			}
#if 0
			else if (next == '(') {
				syntax_error("<(process) not supported");
				goto parse_error;
			}
#endif
			if (parse_redirect(&ctx, redir_fd, redir_style, input))
				goto parse_error;
			continue; /* get next char */
		case '#':
			if (ctx.word.length == 0 && !ctx.word.has_quoted_part) {
				/* skip "#comment" */
				/* note: we do not add it to &ctx.as_string */
/* TODO: in bash:
 * comment inside $() goes to the next \n, even inside quoted string (!):
 * cmd "$(cmd2 #comment)" - syntax error
 * cmd "`cmd2 #comment`" - ok
 * We accept both (comment ends where command subst ends, in both cases).
 */
				while (1) {
					ch = i_peek(input);
					if (ch == '\n') {
						nommu_addchr(&ctx.as_string, '\n');
						break;
					}
					ch = i_getch(input);
					if (ch == EOF)
						break;
				}
				continue; /* get next char */
			}
			break;
		}
 skip_end_trigger:

		if (ctx.is_assignment == MAYBE_ASSIGNMENT
		 /* check that we are not in word in "a=1 2>word b=1": */
		 && !ctx.pending_redirect
		) {
			/* ch is a special char and thus this word
			 * cannot be an assignment */
			ctx.is_assignment = NOT_ASSIGNMENT;
			debug_printf_parse("ctx.is_assignment='%s'\n", assignment_flag[ctx.is_assignment]);
		}

		/* Note: nommu_addchr(&ctx.as_string, ch) is already done */

		switch (ch) {
		case SPECIAL_VAR_SYMBOL:
			/* Convert raw ^C to corresponding special variable reference */
			o_addchr(&ctx.word, SPECIAL_VAR_SYMBOL);
			o_addchr(&ctx.word, SPECIAL_VAR_QUOTED_SVS);
			/* fall through */
		case '#':
			/* non-comment #: "echo a#b" etc */
			o_addchr(&ctx.word, ch);
			continue; /* get next char */
		case '$':
			if (!parse_dollar(&ctx.as_string, &ctx.word, input, /*quote_mask:*/ 0)) {
				debug_printf_parse("parse_stream parse error: "
					"parse_dollar returned 0 (error)\n");
				goto parse_error;
			}
			continue; /* get next char */
		case '"':
			ctx.word.has_quoted_part = 1;
			if (next == '"' && !ctx.pending_redirect) {
				i_getch(input); /* eat second " */
 insert_empty_quoted_str_marker:
				nommu_addchr(&ctx.as_string, next);
				o_addchr(&ctx.word, SPECIAL_VAR_SYMBOL);
				o_addchr(&ctx.word, SPECIAL_VAR_SYMBOL);
				continue; /* get next char */
			}
			if (ctx.is_assignment == NOT_ASSIGNMENT)
				ctx.word.o_expflags |= EXP_FLAG_ESC_GLOB_CHARS;
			if (!encode_string(&ctx.as_string, &ctx.word, input, '"', /*process_bkslash:*/ 1))
				goto parse_error;
			ctx.word.o_expflags &= ~EXP_FLAG_ESC_GLOB_CHARS;
			continue; /* get next char */
#if ENABLE_HUSH_TICK
		case '`': {
			USE_FOR_NOMMU(unsigned pos;)

			o_addchr(&ctx.word, SPECIAL_VAR_SYMBOL);
			o_addchr(&ctx.word, '`');
			USE_FOR_NOMMU(pos = ctx.word.length;)
			if (!add_till_backquote(&ctx.word, input, /*in_dquote:*/ 0))
				goto parse_error;
# if !BB_MMU
			o_addstr(&ctx.as_string, ctx.word.data + pos);
			o_addchr(&ctx.as_string, '`');
# endif
			o_addchr(&ctx.word, SPECIAL_VAR_SYMBOL);
			//debug_printf_subst("SUBST RES3 '%s'\n", ctx.word.data + pos);
			continue; /* get next char */
		}
#endif
		case ';':
#if ENABLE_HUSH_CASE
 case_semi:
#endif
			if (done_word(&ctx)) {
				goto parse_error;
			}
			done_pipe(&ctx, PIPE_SEQ);
#if ENABLE_HUSH_CASE
			/* Eat multiple semicolons, detect
			 * whether it means something special */
			while (1) {
				ch = i_peek_and_eat_bkslash_nl(input);
				if (ch != ';')
					break;
				ch = i_getch(input);
				nommu_addchr(&ctx.as_string, ch);
				if (ctx.ctx_res_w == RES_CASE_BODY) {
					ctx.ctx_dsemicolon = 1;
					ctx.ctx_res_w = RES_MATCH;
					break;
				}
			}
#endif
 new_cmd:
			/* We just finished a cmd. New one may start
			 * with an assignment */
			ctx.is_assignment = MAYBE_ASSIGNMENT;
			debug_printf_parse("ctx.is_assignment='%s'\n", assignment_flag[ctx.is_assignment]);
			continue; /* get next char */
		case '&':
			if (done_word(&ctx)) {
				goto parse_error;
			}
			if (next == '&') {
				ch = i_getch(input);
				nommu_addchr(&ctx.as_string, ch);
				done_pipe(&ctx, PIPE_AND);
			} else {
				done_pipe(&ctx, PIPE_BG);
			}
			goto new_cmd;
		case '|':
			if (done_word(&ctx)) {
				goto parse_error;
			}
#if ENABLE_HUSH_CASE
			if (ctx.ctx_res_w == RES_MATCH)
				break; /* we are in case's "word | word)" */
#endif
			if (next == '|') { /* || */
				ch = i_getch(input);
				nommu_addchr(&ctx.as_string, ch);
				done_pipe(&ctx, PIPE_OR);
			} else {
				/* we could pick up a file descriptor choice here
				 * with redirect_opt_num(), but bash doesn't do it.
				 * "echo foo 2| cat" yields "foo 2". */
				done_command(&ctx);
			}
			goto new_cmd;
		case '(':
#if ENABLE_HUSH_CASE
			/* "case... in [(]word)..." - skip '(' */
			if (ctx.ctx_res_w == RES_MATCH
			 && ctx.command->argv == NULL /* not (word|(... */
			 && ctx.word.length == 0 /* not word(... */
			 && ctx.word.has_quoted_part == 0 /* not ""(... */
			) {
				continue; /* get next char */
			}
#endif
		case '{':
			if (parse_group(&ctx, input, ch) != 0) {
				goto parse_error;
			}
			goto new_cmd;
		case ')':
#if ENABLE_HUSH_CASE
			if (ctx.ctx_res_w == RES_MATCH)
				goto case_semi;
#endif
		case '}':
			/* proper use of this character is caught by end_trigger:
			 * if we see {, we call parse_group(..., end_trigger='}')
			 * and it will match } earlier (not here). */
			G.last_exitcode = 2;
			syntax_error_unexpected_ch(ch);
			goto parse_error2;
		default:
			if (HUSH_DEBUG)
				bb_error_msg_and_die("BUG: unexpected %c", ch);
		}
	} /* while (1) */

 parse_error:
	G.last_exitcode = 1;
 parse_error2:
	{
		struct parse_context *pctx;
		IF_HAS_KEYWORDS(struct parse_context *p2;)

		/* Clean up allocated tree.
		 * Sample for finding leaks on syntax error recovery path.
		 * Run it from interactive shell, watch pmap `pidof hush`.
		 * while if false; then false; fi; do break; fi
		 * Samples to catch leaks at execution:
		 * while if (true | { true;}); then echo ok; fi; do break; done
		 * while if (true | { true;}); then echo ok; fi; do (if echo ok; break; then :; fi) | cat; break; done
		 */
		pctx = &ctx;
		do {
			/* Update pipe/command counts,
			 * otherwise freeing may miss some */
			done_pipe(pctx, PIPE_SEQ);
			debug_printf_clean("freeing list %p from ctx %p\n",
					pctx->list_head, pctx);
			debug_print_tree(pctx->list_head, 0);
			free_pipe_list(pctx->list_head);
			debug_printf_clean("freed list %p\n", pctx->list_head);
#if !BB_MMU
			o_free_unsafe(&pctx->as_string);
#endif
			IF_HAS_KEYWORDS(p2 = pctx->stack;)
			if (pctx != &ctx) {
				free(pctx);
			}
			IF_HAS_KEYWORDS(pctx = p2;)
		} while (HAS_KEYWORDS && pctx);

		o_free(&ctx.word);
#if !BB_MMU
		if (pstring)
			*pstring = NULL;
#endif
		debug_leave();
		return ERR_PTR;
	}
}


/*** Execution routines ***/

/* Expansion can recurse, need forward decls: */
#if !BASH_PATTERN_SUBST && !ENABLE_HUSH_CASE
#define expand_string_to_string(str, EXP_flags, do_unbackslash) \
	expand_string_to_string(str)
#endif
static char *expand_string_to_string(const char *str, int EXP_flags, int do_unbackslash);
#if ENABLE_HUSH_TICK
static int process_command_subs(o_string *dest, const char *s);
#endif

/* expand_strvec_to_strvec() takes a list of strings, expands
 * all variable references within and returns a pointer to
 * a list of expanded strings, possibly with larger number
 * of strings. (Think VAR="a b"; echo $VAR).
 * This new list is allocated as a single malloc block.
 * NULL-terminated list of char* pointers is at the beginning of it,
 * followed by strings themselves.
 * Caller can deallocate entire list by single free(list). */

/* A horde of its helpers come first: */

static void o_addblock_duplicate_backslash(o_string *o, const char *str, int len)
{
	while (--len >= 0) {
		char c = *str++;

#if ENABLE_HUSH_BRACE_EXPANSION
		if (c == '{' || c == '}') {
			/* { -> \{, } -> \} */
			o_addchr(o, '\\');
			/* And now we want to add { or } and continue:
			 *  o_addchr(o, c);
			 *  continue;
			 * luckily, just falling through achieves this.
			 */
		}
#endif
		o_addchr(o, c);
		if (c == '\\') {
			/* \z -> \\\z; \<eol> -> \\<eol> */
			o_addchr(o, '\\');
			if (len) {
				len--;
				o_addchr(o, '\\');
				o_addchr(o, *str++);
			}
		}
	}
}

/* Store given string, finalizing the word and starting new one whenever
 * we encounter IFS char(s). This is used for expanding variable values.
 * End-of-string does NOT finalize word: think about 'echo -$VAR-'.
 * Return in *ended_with_ifs:
 * 1 - ended with IFS char, else 0 (this includes case of empty str).
 */
static int expand_on_ifs(int *ended_with_ifs, o_string *output, int n, const char *str)
{
	int last_is_ifs = 0;

	while (1) {
		int word_len;

		if (!*str)  /* EOL - do not finalize word */
			break;
		word_len = strcspn(str, G.ifs);
		if (word_len) {
			/* We have WORD_LEN leading non-IFS chars */
			if (!(output->o_expflags & EXP_FLAG_GLOB)) {
				o_addblock(output, str, word_len);
			} else {
				/* Protect backslashes against globbing up :)
				 * Example: "v='\*'; echo b$v" prints "b\*"
				 * (and does not try to glob on "*")
				 */
				o_addblock_duplicate_backslash(output, str, word_len);
				/*/ Why can't we do it easier? */
				/*o_addblock(output, str, word_len); - WRONG: "v='\*'; echo Z$v" prints "Z*" instead of "Z\*" */
				/*o_addqblock(output, str, word_len); - WRONG: "v='*'; echo Z$v" prints "Z*" instead of Z* files */
			}
			last_is_ifs = 0;
			str += word_len;
			if (!*str)  /* EOL - do not finalize word */
				break;
		}

		/* We know str here points to at least one IFS char */
		last_is_ifs = 1;
		str += strspn(str, G.ifs_whitespace); /* skip IFS whitespace chars */
		if (!*str)  /* EOL - do not finalize word */
			break;

		if (G.ifs_whitespace != G.ifs /* usually false ($IFS is usually all whitespace), */
		 && strchr(G.ifs, *str)       /* the second check would fail */
		) {
			/* This is a non-whitespace $IFS char */
			/* Skip it and IFS whitespace chars, start new word */
			str++;
			str += strspn(str, G.ifs_whitespace);
			goto new_word;
		}

		/* Start new word... but not always! */
		/* Case "v=' a'; echo ''$v": we do need to finalize empty word: */
		if (output->has_quoted_part
		/* Case "v=' a'; echo $v":
		 * here nothing precedes the space in $v expansion,
		 * therefore we should not finish the word
		 * (IOW: if there *is* word to finalize, only then do it):
		 */
		 || (n > 0 && output->data[output->length - 1])
		) {
 new_word:
			o_addchr(output, '\0');
			debug_print_list("expand_on_ifs", output, n);
			n = o_save_ptr(output, n);
		}
	}

	if (ended_with_ifs)
		*ended_with_ifs = last_is_ifs;
	debug_print_list("expand_on_ifs[1]", output, n);
	return n;
}

/* Helper to expand $((...)) and heredoc body. These act as if
 * they are in double quotes, with the exception that they are not :).
 * Just the rules are similar: "expand only $var and `cmd`"
 *
 * Returns malloced string.
 * As an optimization, we return NULL if expansion is not needed.
 */
#if !BASH_PATTERN_SUBST
/* only ${var/pattern/repl} (its pattern part) needs additional mode */
#define encode_then_expand_string(str, process_bkslash, do_unbackslash) \
	encode_then_expand_string(str)
#endif
static char *encode_then_expand_string(const char *str, int process_bkslash, int do_unbackslash)
{
#if !BASH_PATTERN_SUBST
	enum { do_unbackslash = 1 };
#endif
	char *exp_str;
	struct in_str input;
	o_string dest = NULL_O_STRING;

	if (!strchr(str, '$')
	 && !strchr(str, '\\')
#if ENABLE_HUSH_TICK
	 && !strchr(str, '`')
#endif
	) {
		return NULL;
	}

	/* We need to expand. Example:
	 * echo $(($a + `echo 1`)) $((1 + $((2)) ))
	 */
	setup_string_in_str(&input, str);
	encode_string(NULL, &dest, &input, EOF, process_bkslash);
//TODO: error check (encode_string returns 0 on error)?
	//bb_error_msg("'%s' -> '%s'", str, dest.data);
	exp_str = expand_string_to_string(dest.data,
			do_unbackslash ? EXP_FLAG_ESC_GLOB_CHARS : 0,
			do_unbackslash
	);
	//bb_error_msg("'%s' -> '%s'", dest.data, exp_str);
	o_free_unsafe(&dest);
	return exp_str;
}

#if ENABLE_FEATURE_SH_MATH
static arith_t expand_and_evaluate_arith(const char *arg, const char **errmsg_p)
{
	arith_state_t math_state;
	arith_t res;
	char *exp_str;

	math_state.lookupvar = get_local_var_value;
	math_state.setvar = set_local_var_from_halves;
	//math_state.endofname = endofname;
	exp_str = encode_then_expand_string(arg, /*process_bkslash:*/ 1, /*unbackslash:*/ 1);
	res = arith(&math_state, exp_str ? exp_str : arg);
	free(exp_str);
	if (errmsg_p)
		*errmsg_p = math_state.errmsg;
	if (math_state.errmsg)
		msg_and_die_if_script(math_state.errmsg);
	return res;
}
#endif

#if BASH_PATTERN_SUBST
/* ${var/[/]pattern[/repl]} helpers */
static char *strstr_pattern(char *val, const char *pattern, int *size)
{
	while (1) {
		char *end = scan_and_match(val, pattern, SCAN_MOVE_FROM_RIGHT + SCAN_MATCH_LEFT_HALF);
		debug_printf_varexp("val:'%s' pattern:'%s' end:'%s'\n", val, pattern, end);
		if (end) {
			*size = end - val;
			return val;
		}
		if (*val == '\0')
			return NULL;
		/* Optimization: if "*pat" did not match the start of "string",
		 * we know that "tring", "ring" etc will not match too:
		 */
		if (pattern[0] == '*')
			return NULL;
		val++;
	}
}
static char *replace_pattern(char *val, const char *pattern, const char *repl, char exp_op)
{
	char *result = NULL;
	unsigned res_len = 0;
	unsigned repl_len = strlen(repl);

	/* Null pattern never matches, including if "var" is empty */
	if (!pattern[0])
		return result; /* NULL, no replaces happened */

	while (1) {
		int size;
		char *s = strstr_pattern(val, pattern, &size);
		if (!s)
			break;

		result = xrealloc(result, res_len + (s - val) + repl_len + 1);
		strcpy(mempcpy(result + res_len, val, s - val), repl);
		res_len += (s - val) + repl_len;
		debug_printf_varexp("val:'%s' s:'%s' result:'%s'\n", val, s, result);

		val = s + size;
		if (exp_op == '/')
			break;
	}
	if (*val && result) {
		result = xrealloc(result, res_len + strlen(val) + 1);
		strcpy(result + res_len, val);
		debug_printf_varexp("val:'%s' result:'%s'\n", val, result);
	}
	debug_printf_varexp("result:'%s'\n", result);
	return result;
}
#endif /* BASH_PATTERN_SUBST */

/* Helper:
 * Handles <SPECIAL_VAR_SYMBOL>varname...<SPECIAL_VAR_SYMBOL> construct.
 */
static NOINLINE const char *expand_one_var(char **to_be_freed_pp, char *arg, char **pp)
{
	const char *val;
	char *to_be_freed;
	char *p;
	char *var;
	char first_char;
	char exp_op;
	char exp_save = exp_save; /* for compiler */
	char *exp_saveptr; /* points to expansion operator */
	char *exp_word = exp_word; /* for compiler */
	char arg0;

	val = NULL;
	to_be_freed = NULL;
	p = *pp;
	*p = '\0'; /* replace trailing SPECIAL_VAR_SYMBOL */
	var = arg;
	exp_saveptr = arg[1] ? strchr(VAR_ENCODED_SUBST_OPS, arg[1]) : NULL;
	arg0 = arg[0];
	first_char = arg[0] = arg0 & 0x7f;
	exp_op = 0;

	if (first_char == '#' && arg[1] /* ${#...} but not ${#} */
	 && (!exp_saveptr               /* and ( not(${#<op_char>...}) */
	    || (arg[2] == '\0' && strchr(SPECIAL_VARS_STR, arg[1])) /* or ${#C} "len of $C" ) */
	    )		/* NB: skipping ^^^specvar check mishandles ${#::2} */
	) {
		/* It must be length operator: ${#var} */
		var++;
		exp_op = 'L';
	} else {
		/* Maybe handle parameter expansion */
		if (exp_saveptr /* if 2nd char is one of expansion operators */
		 && strchr(NUMERIC_SPECVARS_STR, first_char) /* 1st char is special variable */
		) {
			/* ${?:0}, ${#[:]%0} etc */
			exp_saveptr = var + 1;
		} else {
			/* ${?}, ${var}, ${var:0}, ${var[:]%0} etc */
			exp_saveptr = var+1 + strcspn(var+1, VAR_ENCODED_SUBST_OPS);
		}
		exp_op = exp_save = *exp_saveptr;
		if (exp_op) {
			exp_word = exp_saveptr + 1;
			if (exp_op == ':') {
				exp_op = *exp_word++;
//TODO: try ${var:} and ${var:bogus} in non-bash config
				if (BASH_SUBSTR
				 && (!exp_op || !strchr(MINUS_PLUS_EQUAL_QUESTION, exp_op))
				) {
					/* oops... it's ${var:N[:M]}, not ${var:?xxx} or some such */
					exp_op = ':';
					exp_word--;
				}
			}
			*exp_saveptr = '\0';
		} /* else: it's not an expansion op, but bare ${var} */
	}

	/* Look up the variable in question */
	if (isdigit(var[0])) {
		/* parse_dollar should have vetted var for us */
		int n = xatoi_positive(var);
		if (n < G.global_argc)
			val = G.global_argv[n];
		/* else val remains NULL: $N with too big N */
	} else {
		switch (var[0]) {
		case '$': /* pid */
			val = utoa(G.root_pid);
			break;
		case '!': /* bg pid */
			val = G.last_bg_pid ? utoa(G.last_bg_pid) : "";
			break;
		case '?': /* exitcode */
			val = utoa(G.last_exitcode);
			break;
		case '#': /* argc */
			val = utoa(G.global_argc ? G.global_argc-1 : 0);
			break;
		default:
			val = get_local_var_value(var);
		}
	}

	/* Handle any expansions */
	if (exp_op == 'L') {
		reinit_unicode_for_hush();
		debug_printf_expand("expand: length(%s)=", val);
		val = utoa(val ? unicode_strlen(val) : 0);
		debug_printf_expand("%s\n", val);
	} else if (exp_op) {
		if (exp_op == '%' || exp_op == '#') {
			/* Standard-mandated substring removal ops:
			 * ${parameter%word} - remove smallest suffix pattern
			 * ${parameter%%word} - remove largest suffix pattern
			 * ${parameter#word} - remove smallest prefix pattern
			 * ${parameter##word} - remove largest prefix pattern
			 *
			 * Word is expanded to produce a glob pattern.
			 * Then var's value is matched to it and matching part removed.
			 */
			if (val && val[0]) {
				char *t;
				char *exp_exp_word;
				char *loc;
				unsigned scan_flags = pick_scan(exp_op, *exp_word);
				if (exp_op == *exp_word)  /* ## or %% */
					exp_word++;
				debug_printf_expand("expand: exp_word:'%s'\n", exp_word);
				/*
				 * process_bkslash:1 unbackslash:1 breaks this:
				 * a='a\\'; echo ${a%\\\\} # correct output is: a
				 * process_bkslash:1 unbackslash:0 breaks this:
				 * a='a}'; echo ${a%\}}    # correct output is: a
				 */
				exp_exp_word = encode_then_expand_string(exp_word, /*process_bkslash:*/ 0, /*unbackslash:*/ 0);
				if (exp_exp_word)
					exp_word = exp_exp_word;
				debug_printf_expand("expand: exp_exp_word:'%s'\n", exp_word);
				/* HACK ALERT. We depend here on the fact that
				 * G.global_argv and results of utoa and get_local_var_value
				 * are actually in writable memory:
				 * scan_and_match momentarily stores NULs there. */
				t = (char*)val;
				loc = scan_and_match(t, exp_word, scan_flags);
				debug_printf_expand("op:%c str:'%s' pat:'%s' res:'%s'\n", exp_op, t, exp_word, loc);
				free(exp_exp_word);
				if (loc) { /* match was found */
					if (scan_flags & SCAN_MATCH_LEFT_HALF) /* #[#] */
						val = loc; /* take right part */
					else /* %[%] */
						val = to_be_freed = xstrndup(val, loc - val); /* left */
				}
			}
		}
#if BASH_PATTERN_SUBST
		else if (exp_op == '/' || exp_op == '\\') {
			/* It's ${var/[/]pattern[/repl]} thing.
			 * Note that in encoded form it has TWO parts:
			 * var/pattern<SPECIAL_VAR_SYMBOL>repl<SPECIAL_VAR_SYMBOL>
			 * and if // is used, it is encoded as \:
			 * var\pattern<SPECIAL_VAR_SYMBOL>repl<SPECIAL_VAR_SYMBOL>
			 */
			if (val && val[0]) {
				/* pattern uses non-standard expansion.
				 * repl should be unbackslashed and globbed
				 * by the usual expansion rules:
				 *  >az >bz
				 *  v='a bz'; echo "${v/a*z/a*z}" #prints "a*z"
				 *  v='a bz'; echo "${v/a*z/\z}"  #prints "z"
				 *  v='a bz'; echo ${v/a*z/a*z}   #prints "az"
				 *  v='a bz'; echo ${v/a*z/\z}    #prints "z"
				 * (note that a*z _pattern_ is never globbed!)
				 */
				char *pattern, *repl, *t;
				pattern = encode_then_expand_string(exp_word, /*process_bkslash:*/ 0, /*unbackslash:*/ 0);
				if (!pattern)
					pattern = xstrdup(exp_word);
				debug_printf_varexp("pattern:'%s'->'%s'\n", exp_word, pattern);
				*p++ = SPECIAL_VAR_SYMBOL;
				exp_word = p;
				p = strchr(p, SPECIAL_VAR_SYMBOL);
				*p = '\0';
				repl = encode_then_expand_string(exp_word, /*process_bkslash:*/ 0, /*unbackslash:*/ 1);
				debug_printf_varexp("repl:'%s'->'%s'\n", exp_word, repl);
				/* HACK ALERT. We depend here on the fact that
				 * G.global_argv and results of utoa and get_local_var_value
				 * are actually in writable memory:
				 * replace_pattern momentarily stores NULs there. */
				t = (char*)val;
				to_be_freed = replace_pattern(t,
						pattern,
						(repl ? repl : exp_word),
						exp_op);
				if (to_be_freed) /* at least one replace happened */
					val = to_be_freed;
				free(pattern);
				free(repl);
			} else {
				/* Empty variable always gives nothing */
				// "v=''; echo ${v/*/w}" prints "", not "w"
				/* Just skip "replace" part */
				*p++ = SPECIAL_VAR_SYMBOL;
				p = strchr(p, SPECIAL_VAR_SYMBOL);
				*p = '\0';
			}
		}
#endif /* BASH_PATTERN_SUBST */
		else if (exp_op == ':') {
#if BASH_SUBSTR && ENABLE_FEATURE_SH_MATH
			/* It's ${var:N[:M]} bashism.
			 * Note that in encoded form it has TWO parts:
			 * var:N<SPECIAL_VAR_SYMBOL>M<SPECIAL_VAR_SYMBOL>
			 */
			arith_t beg, len;
			const char *errmsg;

			beg = expand_and_evaluate_arith(exp_word, &errmsg);
			if (errmsg)
				goto arith_err;
			debug_printf_varexp("beg:'%s'=%lld\n", exp_word, (long long)beg);
			*p++ = SPECIAL_VAR_SYMBOL;
			exp_word = p;
			p = strchr(p, SPECIAL_VAR_SYMBOL);
			*p = '\0';
			len = expand_and_evaluate_arith(exp_word, &errmsg);
			if (errmsg)
				goto arith_err;
			debug_printf_varexp("len:'%s'=%lld\n", exp_word, (long long)len);
			if (beg < 0) {
				/* negative beg counts from the end */
				beg = (arith_t)strlen(val) + beg;
				if (beg < 0) /* ${v: -999999} is "" */
					beg = len = 0;
			}
			debug_printf_varexp("from val:'%s'\n", val);
			if (len < 0) {
				/* in bash, len=-n means strlen()-n */
				len = (arith_t)strlen(val) - beg + len;
				if (len < 0) /* bash compat */
					msg_and_die_if_script("%s: substring expression < 0", var);
			}
			if (len <= 0 || !val || beg >= strlen(val)) {
 arith_err:
				val = NULL;
			} else {
				/* Paranoia. What if user entered 9999999999999
				 * which fits in arith_t but not int? */
				if (len >= INT_MAX)
					len = INT_MAX;
				val = to_be_freed = xstrndup(val + beg, len);
			}
			debug_printf_varexp("val:'%s'\n", val);
#else /* not (HUSH_SUBSTR_EXPANSION && FEATURE_SH_MATH) */
			msg_and_die_if_script("malformed ${%s:...}", var);
			val = NULL;
#endif
		} else { /* one of "-=+?" */
			/* Standard-mandated substitution ops:
			 * ${var?word} - indicate error if unset
			 *      If var is unset, word (or a message indicating it is unset
			 *      if word is null) is written to standard error
			 *      and the shell exits with a non-zero exit status.
			 *      Otherwise, the value of var is substituted.
			 * ${var-word} - use default value
			 *      If var is unset, word is substituted.
			 * ${var=word} - assign and use default value
			 *      If var is unset, word is assigned to var.
			 *      In all cases, final value of var is substituted.
			 * ${var+word} - use alternative value
			 *      If var is unset, null is substituted.
			 *      Otherwise, word is substituted.
			 *
			 * Word is subjected to tilde expansion, parameter expansion,
			 * command substitution, and arithmetic expansion.
			 * If word is not needed, it is not expanded.
			 *
			 * Colon forms (${var:-word}, ${var:=word} etc) do the same,
			 * but also treat null var as if it is unset.
			 */
			int use_word = (!val || ((exp_save == ':') && !val[0]));
			if (exp_op == '+')
				use_word = !use_word;
			debug_printf_expand("expand: op:%c (null:%s) test:%i\n", exp_op,
					(exp_save == ':') ? "true" : "false", use_word);
			if (use_word) {
				to_be_freed = encode_then_expand_string(exp_word, /*process_bkslash:*/ 1, /*unbackslash:*/ 1);
				if (to_be_freed)
					exp_word = to_be_freed;
				if (exp_op == '?') {
					/* mimic bash message */
					msg_and_die_if_script("%s: %s",
						var,
						exp_word[0]
						? exp_word
						: "parameter null or not set"
						/* ash has more specific messages, a-la: */
						/*: (exp_save == ':' ? "parameter null or not set" : "parameter not set")*/
					);
//TODO: how interactive bash aborts expansion mid-command?
				} else {
					val = exp_word;
				}

				if (exp_op == '=') {
					/* ${var=[word]} or ${var:=[word]} */
					if (isdigit(var[0]) || var[0] == '#') {
						/* mimic bash message */
						msg_and_die_if_script("$%s: cannot assign in this way", var);
						val = NULL;
					} else {
						char *new_var = xasprintf("%s=%s", var, val);
						set_local_var(new_var, /*flag:*/ 0);
					}
				}
			}
		} /* one of "-=+?" */

		*exp_saveptr = exp_save;
	} /* if (exp_op) */

	arg[0] = arg0;

	*pp = p;
	*to_be_freed_pp = to_be_freed;
	return val;
}

/* Expand all variable references in given string, adding words to list[]
 * at n, n+1,... positions. Return updated n (so that list[n] is next one
 * to be filled). This routine is extremely tricky: has to deal with
 * variables/parameters with whitespace, $* and $@, and constructs like
 * 'echo -$*-'. If you play here, you must run testsuite afterwards! */
static NOINLINE int expand_vars_to_list(o_string *output, int n, char *arg)
{
	/* output->o_expflags & EXP_FLAG_SINGLEWORD (0x80) if we are in
	 * expansion of right-hand side of assignment == 1-element expand.
	 */
	char cant_be_null = 0; /* only bit 0x80 matters */
	int ended_in_ifs = 0;  /* did last unquoted expansion end with IFS chars? */
	char *p;

	debug_printf_expand("expand_vars_to_list: arg:'%s' singleword:%x\n", arg,
			!!(output->o_expflags & EXP_FLAG_SINGLEWORD));
	debug_print_list("expand_vars_to_list", output, n);
	n = o_save_ptr(output, n);
	debug_print_list("expand_vars_to_list[0]", output, n);

	while ((p = strchr(arg, SPECIAL_VAR_SYMBOL)) != NULL) {
		char first_ch;
		char *to_be_freed = NULL;
		const char *val = NULL;
#if ENABLE_HUSH_TICK
		o_string subst_result = NULL_O_STRING;
#endif
#if ENABLE_FEATURE_SH_MATH
		char arith_buf[sizeof(arith_t)*3 + 2];
#endif

		if (ended_in_ifs) {
			o_addchr(output, '\0');
			n = o_save_ptr(output, n);
			ended_in_ifs = 0;
		}

		o_addblock(output, arg, p - arg);
		debug_print_list("expand_vars_to_list[1]", output, n);
		arg = ++p;
		p = strchr(p, SPECIAL_VAR_SYMBOL);

		/* Fetch special var name (if it is indeed one of them)
		 * and quote bit, force the bit on if singleword expansion -
		 * important for not getting v=$@ expand to many words. */
		first_ch = arg[0] | (output->o_expflags & EXP_FLAG_SINGLEWORD);

		/* Is this variable quoted and thus expansion can't be null?
		 * "$@" is special. Even if quoted, it can still
		 * expand to nothing (not even an empty string),
		 * thus it is excluded. */
		if ((first_ch & 0x7f) != '@')
			cant_be_null |= first_ch;

		switch (first_ch & 0x7f) {
		/* Highest bit in first_ch indicates that var is double-quoted */
		case '*':
		case '@': {
			int i;
			if (!G.global_argv[1])
				break;
			i = 1;
			cant_be_null |= first_ch; /* do it for "$@" _now_, when we know it's not empty */
			if (!(first_ch & 0x80)) { /* unquoted $* or $@ */
				while (G.global_argv[i]) {
					n = expand_on_ifs(NULL, output, n, G.global_argv[i]);
					debug_printf_expand("expand_vars_to_list: argv %d (last %d)\n", i, G.global_argc - 1);
					if (G.global_argv[i++][0] && G.global_argv[i]) {
						/* this argv[] is not empty and not last:
						 * put terminating NUL, start new word */
						o_addchr(output, '\0');
						debug_print_list("expand_vars_to_list[2]", output, n);
						n = o_save_ptr(output, n);
						debug_print_list("expand_vars_to_list[3]", output, n);
					}
				}
			} else
			/* If EXP_FLAG_SINGLEWORD, we handle assignment 'a=....$@.....'
			 * and in this case should treat it like '$*' - see 'else...' below */
			if (first_ch == (char)('@'|0x80)  /* quoted $@ */
			 && !(output->o_expflags & EXP_FLAG_SINGLEWORD) /* not v="$@" case */
			) {
				while (1) {
					o_addQstr(output, G.global_argv[i]);
					if (++i >= G.global_argc)
						break;
					o_addchr(output, '\0');
					debug_print_list("expand_vars_to_list[4]", output, n);
					n = o_save_ptr(output, n);
				}
			} else { /* quoted $* (or v="$@" case): add as one word */
				while (1) {
					o_addQstr(output, G.global_argv[i]);
					if (!G.global_argv[++i])
						break;
					if (G.ifs[0])
						o_addchr(output, G.ifs[0]);
				}
				output->has_quoted_part = 1;
			}
			break;
		}
		case SPECIAL_VAR_SYMBOL: /* <SPECIAL_VAR_SYMBOL><SPECIAL_VAR_SYMBOL> */
			/* "Empty variable", used to make "" etc to not disappear */
			output->has_quoted_part = 1;
			arg++;
			cant_be_null = 0x80;
			break;
		case SPECIAL_VAR_QUOTED_SVS:
			/* <SPECIAL_VAR_SYMBOL><SPECIAL_VAR_QUOTED_SVS><SPECIAL_VAR_SYMBOL> */
			arg++;
			val = SPECIAL_VAR_SYMBOL_STR;
			break;
#if ENABLE_HUSH_TICK
		case '`': /* <SPECIAL_VAR_SYMBOL>`cmd<SPECIAL_VAR_SYMBOL> */
			*p = '\0'; /* replace trailing <SPECIAL_VAR_SYMBOL> */
			arg++;
			/* Can't just stuff it into output o_string,
			 * expanded result may need to be globbed
			 * and $IFS-split */
			debug_printf_subst("SUBST '%s' first_ch %x\n", arg, first_ch);
			G.last_exitcode = process_command_subs(&subst_result, arg);
			G.expand_exitcode = G.last_exitcode;
			debug_printf_subst("SUBST RES:%d '%s'\n", G.last_exitcode, subst_result.data);
			val = subst_result.data;
			goto store_val;
#endif
#if ENABLE_FEATURE_SH_MATH
		case '+': { /* <SPECIAL_VAR_SYMBOL>+cmd<SPECIAL_VAR_SYMBOL> */
			arith_t res;

			arg++; /* skip '+' */
			*p = '\0'; /* replace trailing <SPECIAL_VAR_SYMBOL> */
			debug_printf_subst("ARITH '%s' first_ch %x\n", arg, first_ch);
			res = expand_and_evaluate_arith(arg, NULL);
			debug_printf_subst("ARITH RES '"ARITH_FMT"'\n", res);
			sprintf(arith_buf, ARITH_FMT, res);
			val = arith_buf;
			break;
		}
#endif
		default:
			val = expand_one_var(&to_be_freed, arg, &p);
 IF_HUSH_TICK(store_val:)
			if (!(first_ch & 0x80)) { /* unquoted $VAR */
				debug_printf_expand("unquoted '%s', output->o_escape:%d\n", val,
						!!(output->o_expflags & EXP_FLAG_ESC_GLOB_CHARS));
				if (val && val[0]) {
					n = expand_on_ifs(&ended_in_ifs, output, n, val);
					val = NULL;
				}
			} else { /* quoted $VAR, val will be appended below */
				output->has_quoted_part = 1;
				debug_printf_expand("quoted '%s', output->o_escape:%d\n", val,
						!!(output->o_expflags & EXP_FLAG_ESC_GLOB_CHARS));
			}
			break;
		} /* switch (char after <SPECIAL_VAR_SYMBOL>) */

		if (val && val[0]) {
			o_addQstr(output, val);
		}
		free(to_be_freed);

		/* Restore NULL'ed SPECIAL_VAR_SYMBOL.
		 * Do the check to avoid writing to a const string. */
		if (*p != SPECIAL_VAR_SYMBOL)
			*p = SPECIAL_VAR_SYMBOL;

#if ENABLE_HUSH_TICK
		o_free(&subst_result);
#endif
		arg = ++p;
	} /* end of "while (SPECIAL_VAR_SYMBOL is found) ..." */

	if (arg[0]) {
		if (ended_in_ifs) {
			o_addchr(output, '\0');
			n = o_save_ptr(output, n);
		}
		debug_print_list("expand_vars_to_list[a]", output, n);
		/* this part is literal, and it was already pre-quoted
		 * if needed (much earlier), do not use o_addQstr here! */
		o_addstr_with_NUL(output, arg);
		debug_print_list("expand_vars_to_list[b]", output, n);
	} else if (output->length == o_get_last_ptr(output, n) /* expansion is empty */
	 && !(cant_be_null & 0x80) /* and all vars were not quoted. */
	) {
		n--;
		/* allow to reuse list[n] later without re-growth */
		output->has_empty_slot = 1;
	} else {
		o_addchr(output, '\0');
	}

	return n;
}

static char **expand_variables(char **argv, unsigned expflags)
{
	int n;
	char **list;
	o_string output = NULL_O_STRING;

	output.o_expflags = expflags;

	n = 0;
	while (*argv) {
		n = expand_vars_to_list(&output, n, *argv);
		argv++;
	}
	debug_print_list("expand_variables", &output, n);

	/* output.data (malloced in one block) gets returned in "list" */
	list = o_finalize_list(&output, n);
	debug_print_strings("expand_variables[1]", list);
	return list;
}

static char **expand_strvec_to_strvec(char **argv)
{
	return expand_variables(argv, EXP_FLAG_GLOB | EXP_FLAG_ESC_GLOB_CHARS);
}

#if defined(CMD_SINGLEWORD_NOGLOB)
static char **expand_strvec_to_strvec_singleword_noglob(char **argv)
{
	return expand_variables(argv, EXP_FLAG_SINGLEWORD);
}
#endif

/* Used for expansion of right hand of assignments,
 * $((...)), heredocs, variable expansion parts.
 *
 * NB: should NOT do globbing!
 * "export v=/bin/c*; env | grep ^v=" outputs "v=/bin/c*"
 */
static char *expand_string_to_string(const char *str, int EXP_flags, int do_unbackslash)
{
#if !BASH_PATTERN_SUBST && !ENABLE_HUSH_CASE
	const int do_unbackslash = 1;
	const int EXP_flags = EXP_FLAG_ESC_GLOB_CHARS;
#endif
	char *argv[2], **list;

	debug_printf_expand("string_to_string<='%s'\n", str);
	/* This is generally an optimization, but it also
	 * handles "", which otherwise trips over !list[0] check below.
	 * (is this ever happens that we actually get str="" here?)
	 */
	if (!strchr(str, SPECIAL_VAR_SYMBOL) && !strchr(str, '\\')) {
		//TODO: Can use on strings with \ too, just unbackslash() them?
		debug_printf_expand("string_to_string(fast)=>'%s'\n", str);
		return xstrdup(str);
	}

	argv[0] = (char*)str;
	argv[1] = NULL;
	list = expand_variables(argv, EXP_flags | EXP_FLAG_SINGLEWORD);
	if (HUSH_DEBUG)
		if (!list[0] || list[1])
			bb_error_msg_and_die("BUG in varexp2");
	/* actually, just move string 2*sizeof(char*) bytes back */
	overlapping_strcpy((char*)list, list[0]);
	if (do_unbackslash)
		unbackslash((char*)list);
	debug_printf_expand("string_to_string=>'%s'\n", (char*)list);
	return (char*)list;
}

#if 0
static char* expand_strvec_to_string(char **argv)
{
	char **list;

	list = expand_variables(argv, EXP_FLAG_SINGLEWORD);
	/* Convert all NULs to spaces */
	if (list[0]) {
		int n = 1;
		while (list[n]) {
			if (HUSH_DEBUG)
				if (list[n-1] + strlen(list[n-1]) + 1 != list[n])
					bb_error_msg_and_die("BUG in varexp3");
			/* bash uses ' ' regardless of $IFS contents */
			list[n][-1] = ' ';
			n++;
		}
	}
	overlapping_strcpy((char*)list, list[0] ? list[0] : "");
	debug_printf_expand("strvec_to_string='%s'\n", (char*)list);
	return (char*)list;
}
#endif

static char **expand_assignments(char **argv, int count)
{
	int i;
	char **p;

	G.expanded_assignments = p = NULL;
	/* Expand assignments into one string each */
	for (i = 0; i < count; i++) {
		p = add_string_to_strings(p,
			expand_string_to_string(argv[i],
				EXP_FLAG_ESC_GLOB_CHARS,
				/*unbackslash:*/ 1
			)
		);
		G.expanded_assignments = p;
	}
	G.expanded_assignments = NULL;
	return p;
}


static void switch_off_special_sigs(unsigned mask)
{
	unsigned sig = 0;
	while ((mask >>= 1) != 0) {
		sig++;
		if (!(mask & 1))
			continue;
#if ENABLE_HUSH_TRAP
		if (G_traps) {
			if (G_traps[sig] && !G_traps[sig][0])
				/* trap is '', has to remain SIG_IGN */
				continue;
			free(G_traps[sig]);
			G_traps[sig] = NULL;
		}
#endif
		/* We are here only if no trap or trap was not '' */
		install_sighandler(sig, SIG_DFL);
	}
}

#if BB_MMU
/* never called */
void re_execute_shell(char ***to_free, const char *s,
		char *g_argv0, char **g_argv,
		char **builtin_argv) NORETURN;

static void reset_traps_to_defaults(void)
{
	/* This function is always called in a child shell
	 * after fork (not vfork, NOMMU doesn't use this function).
	 */
	IF_HUSH_TRAP(unsigned sig;)
	unsigned mask;

	/* Child shells are not interactive.
	 * SIGTTIN/SIGTTOU/SIGTSTP should not have special handling.
	 * Testcase: (while :; do :; done) + ^Z should background.
	 * Same goes for SIGTERM, SIGHUP, SIGINT.
	 */
	mask = (G.special_sig_mask & SPECIAL_INTERACTIVE_SIGS) | G_fatal_sig_mask;
	if (!G_traps && !mask)
		return; /* already no traps and no special sigs */

	/* Switch off special sigs */
	switch_off_special_sigs(mask);
# if ENABLE_HUSH_JOB
	G_fatal_sig_mask = 0;
# endif
	G.special_sig_mask &= ~SPECIAL_INTERACTIVE_SIGS;
	/* SIGQUIT,SIGCHLD and maybe SPECIAL_JOBSTOP_SIGS
	 * remain set in G.special_sig_mask */

# if ENABLE_HUSH_TRAP
	if (!G_traps)
		return;

	/* Reset all sigs to default except ones with empty traps */
	for (sig = 0; sig < NSIG; sig++) {
		if (!G_traps[sig])
			continue; /* no trap: nothing to do */
		if (!G_traps[sig][0])
			continue; /* empty trap: has to remain SIG_IGN */
		/* sig has non-empty trap, reset it: */
		free(G_traps[sig]);
		G_traps[sig] = NULL;
		/* There is no signal for trap 0 (EXIT) */
		if (sig == 0)
			continue;
		install_sighandler(sig, pick_sighandler(sig));
	}
# endif
}

#else /* !BB_MMU */

static void re_execute_shell(char ***to_free, const char *s,
		char *g_argv0, char **g_argv,
		char **builtin_argv) NORETURN;
static void re_execute_shell(char ***to_free, const char *s,
		char *g_argv0, char **g_argv,
		char **builtin_argv)
{
# define NOMMU_HACK_FMT ("-$%x:%x:%x:%x:%x:%llx" IF_HUSH_LOOPS(":%x"))
	/* delims + 2 * (number of bytes in printed hex numbers) */
	char param_buf[sizeof(NOMMU_HACK_FMT) + 2 * (sizeof(int)*6 + sizeof(long long)*1)];
	char *heredoc_argv[4];
	struct variable *cur;
# if ENABLE_HUSH_FUNCTIONS
	struct function *funcp;
# endif
	char **argv, **pp;
	unsigned cnt;
	unsigned long long empty_trap_mask;

	if (!g_argv0) { /* heredoc */
		argv = heredoc_argv;
		argv[0] = (char *) G.argv0_for_re_execing;
		argv[1] = (char *) "-<";
		argv[2] = (char *) s;
		argv[3] = NULL;
		pp = &argv[3]; /* used as pointer to empty environment */
		goto do_exec;
	}

	cnt = 0;
	pp = builtin_argv;
	if (pp) while (*pp++)
		cnt++;

	empty_trap_mask = 0;
	if (G_traps) {
		int sig;
		for (sig = 1; sig < NSIG; sig++) {
			if (G_traps[sig] && !G_traps[sig][0])
				empty_trap_mask |= 1LL << sig;
		}
	}

	sprintf(param_buf, NOMMU_HACK_FMT
			, (unsigned) G.root_pid
			, (unsigned) G.root_ppid
			, (unsigned) G.last_bg_pid
			, (unsigned) G.last_exitcode
			, cnt
			, empty_trap_mask
			IF_HUSH_LOOPS(, G.depth_of_loop)
			);
# undef NOMMU_HACK_FMT
	/* 1:hush 2:-$<pid>:<pid>:<exitcode>:<etc...> <vars...> <funcs...>
	 * 3:-c 4:<cmd> 5:<arg0> <argN...> 6:NULL
	 */
	cnt += 6;
	for (cur = G.top_var; cur; cur = cur->next) {
		if (!cur->flg_export || cur->flg_read_only)
			cnt += 2;
	}
# if ENABLE_HUSH_FUNCTIONS
	for (funcp = G.top_func; funcp; funcp = funcp->next)
		cnt += 3;
# endif
	pp = g_argv;
	while (*pp++)
		cnt++;
	*to_free = argv = pp = xzalloc(sizeof(argv[0]) * cnt);
	*pp++ = (char *) G.argv0_for_re_execing;
	*pp++ = param_buf;
	for (cur = G.top_var; cur; cur = cur->next) {
		if (strcmp(cur->varstr, hush_version_str) == 0)
			continue;
		if (cur->flg_read_only) {
			*pp++ = (char *) "-R";
			*pp++ = cur->varstr;
		} else if (!cur->flg_export) {
			*pp++ = (char *) "-V";
			*pp++ = cur->varstr;
		}
	}
# if ENABLE_HUSH_FUNCTIONS
	for (funcp = G.top_func; funcp; funcp = funcp->next) {
		*pp++ = (char *) "-F";
		*pp++ = funcp->name;
		*pp++ = funcp->body_as_string;
	}
# endif
	/* We can pass activated traps here. Say, -Tnn:trap_string
	 *
	 * However, POSIX says that subshells reset signals with traps
	 * to SIG_DFL.
	 * I tested bash-3.2 and it not only does that with true subshells
	 * of the form ( list ), but with any forked children shells.
	 * I set trap "echo W" WINCH; and then tried:
	 *
	 * { echo 1; sleep 20; echo 2; } &
	 * while true; do echo 1; sleep 20; echo 2; break; done &
	 * true | { echo 1; sleep 20; echo 2; } | cat
	 *
	 * In all these cases sending SIGWINCH to the child shell
	 * did not run the trap. If I add trap "echo V" WINCH;
	 * _inside_ group (just before echo 1), it works.
	 *
	 * I conclude it means we don't need to pass active traps here.
	 */
	*pp++ = (char *) "-c";
	*pp++ = (char *) s;
	if (builtin_argv) {
		while (*++builtin_argv)
			*pp++ = *builtin_argv;
		*pp++ = (char *) "";
	}
	*pp++ = g_argv0;
	while (*g_argv)
		*pp++ = *g_argv++;
	/* *pp = NULL; - is already there */
	pp = environ;

 do_exec:
	debug_printf_exec("re_execute_shell pid:%d cmd:'%s'\n", getpid(), s);
	/* Don't propagate SIG_IGN to the child */
	if (SPECIAL_JOBSTOP_SIGS != 0)
		switch_off_special_sigs(G.special_sig_mask & SPECIAL_JOBSTOP_SIGS);
	execve(bb_busybox_exec_path, argv, pp);
	/* Fallback. Useful for init=/bin/hush usage etc */
	if (argv[0][0] == '/')
		execve(argv[0], argv, pp);
	xfunc_error_retval = 127;
	bb_error_msg_and_die("can't re-execute the shell");
}
#endif  /* !BB_MMU */


static int run_and_free_list(struct pipe *pi);

/* Executing from string: eval, sh -c '...'
 *          or from file: /etc/profile, . file, sh <script>, sh (intereactive)
 * end_trigger controls how often we stop parsing
 * NUL: parse all, execute, return
 * ';': parse till ';' or newline, execute, repeat till EOF
 */
static void parse_and_run_stream(struct in_str *inp, int end_trigger)
{
	/* Why we need empty flag?
	 * An obscure corner case "false; ``; echo $?":
	 * empty command in `` should still set $? to 0.
	 * But we can't just set $? to 0 at the start,
	 * this breaks "false; echo `echo $?`" case.
	 */
	bool empty = 1;
	while (1) {
		struct pipe *pipe_list;

#if ENABLE_HUSH_INTERACTIVE
		if (end_trigger == ';') {
			G.promptmode = 0; /* PS1 */
			debug_printf_prompt("%s promptmode=%d\n", __func__, G.promptmode);
		}
#endif
		pipe_list = parse_stream(NULL, inp, end_trigger);
		if (!pipe_list || pipe_list == ERR_PTR) { /* EOF/error */
			/* If we are in "big" script
			 * (not in `cmd` or something similar)...
			 */
			if (pipe_list == ERR_PTR && end_trigger == ';') {
				/* Discard cached input (rest of line) */
				int ch = inp->last_char;
				while (ch != EOF && ch != '\n') {
					//bb_error_msg("Discarded:'%c'", ch);
					ch = i_getch(inp);
				}
				/* Force prompt */
				inp->p = NULL;
				/* This stream isn't empty */
				empty = 0;
				continue;
			}
			if (!pipe_list && empty)
				G.last_exitcode = 0;
			break;
		}
		debug_print_tree(pipe_list, 0);
		debug_printf_exec("parse_and_run_stream: run_and_free_list\n");
		run_and_free_list(pipe_list);
		empty = 0;
		if (G_flag_return_in_progress == 1)
			break;
	}
}

static void parse_and_run_string(const char *s)
{
	struct in_str input;
	//IF_HUSH_LINENO_VAR(unsigned sv = G.lineno;)

	setup_string_in_str(&input, s);
	parse_and_run_stream(&input, '\0');
	//IF_HUSH_LINENO_VAR(G.lineno = sv;)
}

static void parse_and_run_file(FILE *f)
{
	struct in_str input;
	IF_HUSH_LINENO_VAR(unsigned sv = G.lineno;)

	IF_HUSH_LINENO_VAR(G.lineno = 1;)
	setup_file_in_str(&input, f);
	parse_and_run_stream(&input, ';');
	IF_HUSH_LINENO_VAR(G.lineno = sv;)
}

#if ENABLE_HUSH_TICK
static FILE *generate_stream_from_string(const char *s, pid_t *pid_p)
{
	pid_t pid;
	int channel[2];
# if !BB_MMU
	char **to_free = NULL;
# endif

	xpipe(channel);
	pid = BB_MMU ? xfork() : xvfork();
	if (pid == 0) { /* child */
		disable_restore_tty_pgrp_on_exit();
		/* Process substitution is not considered to be usual
		 * 'command execution'.
		 * SUSv3 says ctrl-Z should be ignored, ctrl-C should not.
		 */
		bb_signals(0
			+ (1 << SIGTSTP)
			+ (1 << SIGTTIN)
			+ (1 << SIGTTOU)
			, SIG_IGN);
		CLEAR_RANDOM_T(&G.random_gen); /* or else $RANDOM repeats in child */
		close(channel[0]); /* NB: close _first_, then move fd! */
		xmove_fd(channel[1], 1);
		/* Prevent it from trying to handle ctrl-z etc */
		IF_HUSH_JOB(G.run_list_level = 1;)
# if ENABLE_HUSH_TRAP
		/* Awful hack for `trap` or $(trap).
		 *
		 * http://www.opengroup.org/onlinepubs/009695399/utilities/trap.html
		 * contains an example where "trap" is executed in a subshell:
		 *
		 * save_traps=$(trap)
		 * ...
		 * eval "$save_traps"
		 *
		 * Standard does not say that "trap" in subshell shall print
		 * parent shell's traps. It only says that its output
		 * must have suitable form, but then, in the above example
		 * (which is not supposed to be normative), it implies that.
		 *
		 * bash (and probably other shell) does implement it
		 * (traps are reset to defaults, but "trap" still shows them),
		 * but as a result, "trap" logic is hopelessly messed up:
		 *
		 * # trap
		 * trap -- 'echo Ho' SIGWINCH  <--- we have a handler
		 * # (trap)        <--- trap is in subshell - no output (correct, traps are reset)
		 * # true | trap   <--- trap is in subshell - no output (ditto)
		 * # echo `true | trap`    <--- in subshell - output (but traps are reset!)
		 * trap -- 'echo Ho' SIGWINCH
		 * # echo `(trap)`         <--- in subshell in subshell - output
		 * trap -- 'echo Ho' SIGWINCH
		 * # echo `true | (trap)`  <--- in subshell in subshell in subshell - output!
		 * trap -- 'echo Ho' SIGWINCH
		 *
		 * The rules when to forget and when to not forget traps
		 * get really complex and nonsensical.
		 *
		 * Our solution: ONLY bare $(trap) or `trap` is special.
		 */
		s = skip_whitespace(s);
		if (is_prefixed_with(s, "trap")
		 && skip_whitespace(s + 4)[0] == '\0'
		) {
			static const char *const argv[] = { NULL, NULL };
			builtin_trap((char**)argv);
			fflush_all(); /* important */
			_exit(0);
		}
# endif
# if BB_MMU
		reset_traps_to_defaults();
		parse_and_run_string(s);
		_exit(G.last_exitcode);
# else
	/* We re-execute after vfork on NOMMU. This makes this script safe:
	 * yes "0123456789012345678901234567890" | dd bs=32 count=64k >BIG
	 * huge=`cat BIG` # was blocking here forever
	 * echo OK
	 */
		re_execute_shell(&to_free,
				s,
				G.global_argv[0],
				G.global_argv + 1,
				NULL);
# endif
	}

	/* parent */
	*pid_p = pid;
# if ENABLE_HUSH_FAST
	G.count_SIGCHLD++;
//bb_error_msg("[%d] fork in generate_stream_from_string:"
//		" G.count_SIGCHLD:%d G.handled_SIGCHLD:%d",
//		getpid(), G.count_SIGCHLD, G.handled_SIGCHLD);
# endif
	enable_restore_tty_pgrp_on_exit();
# if !BB_MMU
	free(to_free);
# endif
	close(channel[1]);
	return remember_FILE(xfdopen_for_read(channel[0]));
}

/* Return code is exit status of the process that is run. */
static int process_command_subs(o_string *dest, const char *s)
{
	FILE *fp;
	pid_t pid;
	int status, ch, eol_cnt;

	fp = generate_stream_from_string(s, &pid);

	/* Now send results of command back into original context */
	eol_cnt = 0;
	while ((ch = getc(fp)) != EOF) {
		if (ch == '\0')
			continue;
		if (ch == '\n') {
			eol_cnt++;
			continue;
		}
		while (eol_cnt) {
			o_addchr(dest, '\n');
			eol_cnt--;
		}
		o_addQchr(dest, ch);
	}

	debug_printf("done reading from `cmd` pipe, closing it\n");
	fclose_and_forget(fp);
	/* We need to extract exitcode. Test case
	 * "true; echo `sleep 1; false` $?"
	 * should print 1 */
	safe_waitpid(pid, &status, 0);
	debug_printf("child exited. returning its exitcode:%d\n", WEXITSTATUS(status));
	return WEXITSTATUS(status);
}
#endif /* ENABLE_HUSH_TICK */


static void setup_heredoc(struct redir_struct *redir)
{
	struct fd_pair pair;
	pid_t pid;
	int len, written;
	/* the _body_ of heredoc (misleading field name) */
	const char *heredoc = redir->rd_filename;
	char *expanded;
#if !BB_MMU
	char **to_free;
#endif

	expanded = NULL;
	if (!(redir->rd_dup & HEREDOC_QUOTED)) {
		expanded = encode_then_expand_string(heredoc, /*process_bkslash:*/ 1, /*unbackslash:*/ 1);
		if (expanded)
			heredoc = expanded;
	}
	len = strlen(heredoc);

	close(redir->rd_fd); /* often saves dup2+close in xmove_fd */
	xpiped_pair(pair);
	xmove_fd(pair.rd, redir->rd_fd);

	/* Try writing without forking. Newer kernels have
	 * dynamically growing pipes. Must use non-blocking write! */
	ndelay_on(pair.wr);
	while (1) {
		written = write(pair.wr, heredoc, len);
		if (written <= 0)
			break;
		len -= written;
		if (len == 0) {
			close(pair.wr);
			free(expanded);
			return;
		}
		heredoc += written;
	}
	ndelay_off(pair.wr);

	/* Okay, pipe buffer was not big enough */
	/* Note: we must not create a stray child (bastard? :)
	 * for the unsuspecting parent process. Child creates a grandchild
	 * and exits before parent execs the process which consumes heredoc
	 * (that exec happens after we return from this function) */
#if !BB_MMU
	to_free = NULL;
#endif
	pid = xvfork();
	if (pid == 0) {
		/* child */
		disable_restore_tty_pgrp_on_exit();
		pid = BB_MMU ? xfork() : xvfork();
		if (pid != 0)
			_exit(0);
		/* grandchild */
		close(redir->rd_fd); /* read side of the pipe */
#if BB_MMU
		full_write(pair.wr, heredoc, len); /* may loop or block */
		_exit(0);
#else
		/* Delegate blocking writes to another process */
		xmove_fd(pair.wr, STDOUT_FILENO);
		re_execute_shell(&to_free, heredoc, NULL, NULL, NULL);
#endif
	}
	/* parent */
#if ENABLE_HUSH_FAST
	G.count_SIGCHLD++;
//bb_error_msg("[%d] fork in setup_heredoc: G.count_SIGCHLD:%d G.handled_SIGCHLD:%d", getpid(), G.count_SIGCHLD, G.handled_SIGCHLD);
#endif
	enable_restore_tty_pgrp_on_exit();
#if !BB_MMU
	free(to_free);
#endif
	close(pair.wr);
	free(expanded);
	wait(NULL); /* wait till child has died */
}

struct squirrel {
	int orig_fd;
	int moved_to;
	/* moved_to = n: fd was moved to n; restore back to orig_fd after redir */
	/* moved_to = -1: fd was opened by redirect; close orig_fd after redir */
};

static struct squirrel *append_squirrel(struct squirrel *sq, int i, int orig, int moved)
{
	sq = xrealloc(sq, (i + 2) * sizeof(sq[0]));
	sq[i].orig_fd = orig;
	sq[i].moved_to = moved;
	sq[i+1].orig_fd = -1; /* end marker */
	return sq;
}

static struct squirrel *add_squirrel(struct squirrel *sq, int fd, int avoid_fd)
{
	int moved_to;
	int i;

	i = 0;
	if (sq) for (; sq[i].orig_fd >= 0; i++) {
		/* If we collide with an already moved fd... */
		if (fd == sq[i].moved_to) {
			sq[i].moved_to = dup_CLOEXEC(sq[i].moved_to, avoid_fd);
			debug_printf_redir("redirect_fd %d: already busy, moving to %d\n", fd, sq[i].moved_to);
			if (sq[i].moved_to < 0) /* what? */
				xfunc_die();
			return sq;
		}
		if (fd == sq[i].orig_fd) {
			/* Example: echo Hello >/dev/null 1>&2 */
			debug_printf_redir("redirect_fd %d: already moved\n", fd);
			return sq;
		}
	}

	/* If this fd is open, we move and remember it; if it's closed, moved_to = -1 */
	moved_to = dup_CLOEXEC(fd, avoid_fd);
	debug_printf_redir("redirect_fd %d: previous fd is moved to %d (-1 if it was closed)\n", fd, moved_to);
	if (moved_to < 0 && errno != EBADF)
		xfunc_die();
	return append_squirrel(sq, i, fd, moved_to);
}

static struct squirrel *add_squirrel_closed(struct squirrel *sq, int fd)
{
	int i;

	i = 0;
	if (sq) for (; sq[i].orig_fd >= 0; i++) {
		/* If we collide with an already moved fd... */
		if (fd == sq[i].orig_fd) {
			/* Examples:
			 * "echo 3>FILE 3>&- 3>FILE"
			 * "echo 3>&- 3>FILE"
			 * No need for last redirect to insert
			 * another "need to close 3" indicator.
			 */
			debug_printf_redir("redirect_fd %d: already moved or closed\n", fd);
			return sq;
		}
	}

	debug_printf_redir("redirect_fd %d: previous fd was closed\n", fd);
	return append_squirrel(sq, i, fd, -1);
}

/* fd: redirect wants this fd to be used (e.g. 3>file).
 * Move all conflicting internally used fds,
 * and remember them so that we can restore them later.
 */
static int save_fd_on_redirect(int fd, int avoid_fd, struct squirrel **sqp)
{
	if (avoid_fd < 9) /* the important case here is that it can be -1 */
		avoid_fd = 9;

#if ENABLE_HUSH_INTERACTIVE
	if (fd == G.interactive_fd) {
		/* Testcase: "ls -l /proc/$$/fd 255>&-" should work */
		G.interactive_fd = xdup_CLOEXEC_and_close(G.interactive_fd, avoid_fd);
		debug_printf_redir("redirect_fd %d: matches interactive_fd, moving it to %d\n", fd, G.interactive_fd);
		return 1; /* "we closed fd" */
	}
#endif
	/* Are we called from setup_redirects(squirrel==NULL)? Two cases:
	 * (1) Redirect in a forked child. No need to save FILEs' fds,
	 * we aren't going to use them anymore, ok to trash.
	 * (2) "exec 3>FILE". Bummer. We can save script FILEs' fds,
	 * but how are we doing to restore them?
	 * "fileno(fd) = new_fd" can't be done.
	 */
	if (!sqp)
		return 0;

	/* If this one of script's fds? */
	if (save_FILEs_on_redirect(fd, avoid_fd))
		return 1; /* yes. "we closed fd" */

	/* Check whether it collides with any open fds (e.g. stdio), save fds as needed */
	*sqp = add_squirrel(*sqp, fd, avoid_fd);
	return 0; /* "we did not close fd" */
}

static void restore_redirects(struct squirrel *sq)
{
	if (sq) {
		int i;
		for (i = 0; sq[i].orig_fd >= 0; i++) {
			if (sq[i].moved_to >= 0) {
				/* We simply die on error */
				debug_printf_redir("restoring redirected fd from %d to %d\n", sq[i].moved_to, sq[i].orig_fd);
				xmove_fd(sq[i].moved_to, sq[i].orig_fd);
			} else {
				/* cmd1 9>FILE; cmd2_should_see_fd9_closed */
				debug_printf_redir("restoring redirected fd %d: closing it\n", sq[i].orig_fd);
				close(sq[i].orig_fd);
			}
		}
		free(sq);
	}

	/* If moved, G.interactive_fd stays on new fd, not restoring it */

	restore_redirected_FILEs();
}

#if ENABLE_FEATURE_SH_STANDALONE && BB_MMU
static void close_saved_fds_and_FILE_fds(void)
{
	if (G_interactive_fd)
		close(G_interactive_fd);
	close_all_FILE_list();
}
#endif

static int internally_opened_fd(int fd, struct squirrel *sq)
{
	int i;

#if ENABLE_HUSH_INTERACTIVE
	if (fd == G.interactive_fd)
		return 1;
#endif
	/* If this one of script's fds? */
	if (fd_in_FILEs(fd))
		return 1;

	if (sq) for (i = 0; sq[i].orig_fd >= 0; i++) {
		if (fd == sq[i].moved_to)
			return 1;
	}
	return 0;
}

/* squirrel != NULL means we squirrel away copies of stdin, stdout,
 * and stderr if they are redirected. */
static int setup_redirects(struct command *prog, struct squirrel **sqp)
{
	struct redir_struct *redir;

	for (redir = prog->redirects; redir; redir = redir->next) {
		int newfd;
		int closed;

		if (redir->rd_type == REDIRECT_HEREDOC2) {
			/* "rd_fd<<HERE" case */
			save_fd_on_redirect(redir->rd_fd, /*avoid:*/ 0, sqp);
			/* for REDIRECT_HEREDOC2, rd_filename holds _contents_
			 * of the heredoc */
			debug_printf_parse("set heredoc '%s'\n",
					redir->rd_filename);
			setup_heredoc(redir);
			continue;
		}

		if (redir->rd_dup == REDIRFD_TO_FILE) {
			/* "rd_fd<*>file" case (<*> is <,>,>>,<>) */
			char *p;
			int mode;

			if (redir->rd_filename == NULL) {
				/*
				 * Examples:
				 * "cmd >" (no filename)
				 * "cmd > <file" (2nd redirect starts too early)
				 */
				syntax_error("invalid redirect");
				continue;
			}
			mode = redir_table[redir->rd_type].mode;
			p = expand_string_to_string(redir->rd_filename,
				EXP_FLAG_ESC_GLOB_CHARS, /*unbackslash:*/ 1);
			newfd = open_or_warn(p, mode);
			free(p);
			if (newfd < 0) {
				/* Error message from open_or_warn can be lost
				 * if stderr has been redirected, but bash
				 * and ash both lose it as well
				 * (though zsh doesn't!)
				 */
				return 1;
			}
			if (newfd == redir->rd_fd && sqp) {
				/* open() gave us precisely the fd we wanted.
				 * This means that this fd was not busy
				 * (not opened to anywhere).
				 * Remember to close it on restore:
				 */
				*sqp = add_squirrel_closed(*sqp, newfd);
				debug_printf_redir("redir to previously closed fd %d\n", newfd);
			}
		} else {
			/* "rd_fd>&rd_dup" or "rd_fd>&-" case */
			newfd = redir->rd_dup;
		}

		if (newfd == redir->rd_fd)
			continue;

		/* if "N>FILE": move newfd to redir->rd_fd */
		/* if "N>&M": dup newfd to redir->rd_fd */
		/* if "N>&-": close redir->rd_fd (newfd is REDIRFD_CLOSE) */

		closed = save_fd_on_redirect(redir->rd_fd, /*avoid:*/ newfd, sqp);
		if (newfd == REDIRFD_CLOSE) {
			/* "N>&-" means "close me" */
			if (!closed) {
				/* ^^^ optimization: saving may already
				 * have closed it. If not... */
				close(redir->rd_fd);
			}
			/* Sometimes we do another close on restore, getting EBADF.
			 * Consider "echo 3>FILE 3>&-"
			 * first redirect remembers "need to close 3",
			 * and second redirect closes 3! Restore code then closes 3 again.
			 */
		} else {
			/* if newfd is a script fd or saved fd, simulate EBADF */
			if (internally_opened_fd(newfd, sqp ? *sqp : NULL)) {
				//errno = EBADF;
				//bb_perror_msg_and_die("can't duplicate file descriptor");
				newfd = -1; /* same effect as code above */
			}
			xdup2(newfd, redir->rd_fd);
			if (redir->rd_dup == REDIRFD_TO_FILE)
				/* "rd_fd > FILE" */
				close(newfd);
			/* else: "rd_fd > rd_dup" */
		}
	}
	return 0;
}

static char *find_in_path(const char *arg)
{
	char *ret = NULL;
	const char *PATH = get_local_var_value("PATH");

	if (!PATH)
		return NULL;

	while (1) {
		const char *end = strchrnul(PATH, ':');
		int sz = end - PATH; /* must be int! */

		free(ret);
		if (sz != 0) {
			ret = xasprintf("%.*s/%s", sz, PATH, arg);
		} else {
			/* We have xxx::yyyy in $PATH,
			 * it means "use current dir" */
			ret = xstrdup(arg);
		}
		if (access(ret, F_OK) == 0)
			break;

		if (*end == '\0') {
			free(ret);
			return NULL;
		}
		PATH = end + 1;
	}

	return ret;
}

static const struct built_in_command *find_builtin_helper(const char *name,
		const struct built_in_command *x,
		const struct built_in_command *end)
{
	while (x != end) {
		if (strcmp(name, x->b_cmd) != 0) {
			x++;
			continue;
		}
		debug_printf_exec("found builtin '%s'\n", name);
		return x;
	}
	return NULL;
}
static const struct built_in_command *find_builtin1(const char *name)
{
	return find_builtin_helper(name, bltins1, &bltins1[ARRAY_SIZE(bltins1)]);
}
static const struct built_in_command *find_builtin(const char *name)
{
	const struct built_in_command *x = find_builtin1(name);
	if (x)
		return x;
	return find_builtin_helper(name, bltins2, &bltins2[ARRAY_SIZE(bltins2)]);
}

static void remove_nested_vars(void)
{
	struct variable *cur;
	struct variable **cur_pp;

	cur_pp = &G.top_var;
	while ((cur = *cur_pp) != NULL) {
		if (cur->var_nest_level <= G.var_nest_level) {
			cur_pp = &cur->next;
			continue;
		}
		/* Unexport */
		if (cur->flg_export) {
			debug_printf_env("unexporting nested '%s'/%u\n", cur->varstr, cur->var_nest_level);
			bb_unsetenv(cur->varstr);
		}
		/* Remove from global list */
		*cur_pp = cur->next;
		/* Free */
		if (!cur->max_len) {
			debug_printf_env("freeing nested '%s'/%u\n", cur->varstr, cur->var_nest_level);
			free(cur->varstr);
		}
		free(cur);
	}
}

static void enter_var_nest_level(void)
{
	G.var_nest_level++;
	debug_printf_env("var_nest_level++ %u\n", G.var_nest_level);

	/* Try:	f() { echo -n .; f; }; f
	 * struct variable::var_nest_level is uint16_t,
	 * thus limiting recursion to < 2^16.
	 * In any case, with 8 Mbyte stack SEGV happens
	 * not too long after 2^16 recursions anyway.
	 */
	if (G.var_nest_level > 0xff00)
		bb_error_msg_and_die("fatal recursion (depth %u)", G.var_nest_level);
}

static void leave_var_nest_level(void)
{
	G.var_nest_level--;
	debug_printf_env("var_nest_level-- %u\n", G.var_nest_level);
	if (HUSH_DEBUG && (int)G.var_nest_level < 0)
		bb_error_msg_and_die("BUG: nesting underflow");

	remove_nested_vars();
}

#if ENABLE_HUSH_FUNCTIONS
static struct function **find_function_slot(const char *name)
{
	struct function *funcp;
	struct function **funcpp = &G.top_func;

	while ((funcp = *funcpp) != NULL) {
		if (strcmp(name, funcp->name) == 0) {
			debug_printf_exec("found function '%s'\n", name);
			break;
		}
		funcpp = &funcp->next;
	}
	return funcpp;
}

static ALWAYS_INLINE const struct function *find_function(const char *name)
{
	const struct function *funcp = *find_function_slot(name);
	return funcp;
}

/* Note: takes ownership on name ptr */
static struct function *new_function(char *name)
{
	struct function **funcpp = find_function_slot(name);
	struct function *funcp = *funcpp;

	if (funcp != NULL) {
		struct command *cmd = funcp->parent_cmd;
		debug_printf_exec("func %p parent_cmd %p\n", funcp, cmd);
		if (!cmd) {
			debug_printf_exec("freeing & replacing function '%s'\n", funcp->name);
			free(funcp->name);
			/* Note: if !funcp->body, do not free body_as_string!
			 * This is a special case of "-F name body" function:
			 * body_as_string was not malloced! */
			if (funcp->body) {
				free_pipe_list(funcp->body);
# if !BB_MMU
				free(funcp->body_as_string);
# endif
			}
		} else {
			debug_printf_exec("reinserting in tree & replacing function '%s'\n", funcp->name);
			cmd->argv[0] = funcp->name;
			cmd->group = funcp->body;
# if !BB_MMU
			cmd->group_as_string = funcp->body_as_string;
# endif
		}
	} else {
		debug_printf_exec("remembering new function '%s'\n", name);
		funcp = *funcpp = xzalloc(sizeof(*funcp));
		/*funcp->next = NULL;*/
	}

	funcp->name = name;
	return funcp;
}

# if ENABLE_HUSH_UNSET
static void unset_func(const char *name)
{
	struct function **funcpp = find_function_slot(name);
	struct function *funcp = *funcpp;

	if (funcp != NULL) {
		debug_printf_exec("freeing function '%s'\n", funcp->name);
		*funcpp = funcp->next;
		/* funcp is unlinked now, deleting it.
		 * Note: if !funcp->body, the function was created by
		 * "-F name body", do not free ->body_as_string
		 * and ->name as they were not malloced. */
		if (funcp->body) {
			free_pipe_list(funcp->body);
			free(funcp->name);
#  if !BB_MMU
			free(funcp->body_as_string);
#  endif
		}
		free(funcp);
	}
}
# endif

# if BB_MMU
#define exec_function(to_free, funcp, argv) \
	exec_function(funcp, argv)
# endif
static void exec_function(char ***to_free,
		const struct function *funcp,
		char **argv) NORETURN;
static void exec_function(char ***to_free,
		const struct function *funcp,
		char **argv)
{
# if BB_MMU
	int n;

	argv[0] = G.global_argv[0];
	G.global_argv = argv;
	G.global_argc = n = 1 + string_array_len(argv + 1);

// Example when we are here: "cmd | func"
// func will run with saved-redirect fds open.
// $ f() { echo /proc/self/fd/*; }
// $ true | f
// /proc/self/fd/0 /proc/self/fd/1 /proc/self/fd/2 /proc/self/fd/255 /proc/self/fd/3
// stdio^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ G_interactive_fd^ DIR fd for glob
// Same in script:
// $ . ./SCRIPT
// /proc/self/fd/0 /proc/self/fd/1 /proc/self/fd/2 /proc/self/fd/255 /proc/self/fd/3 /proc/self/fd/4
// stdio^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ G_interactive_fd^ opened ./SCRIPT DIR fd for glob
// They are CLOEXEC so external programs won't see them, but
// for "more correctness" we might want to close those extra fds here:
//?	close_saved_fds_and_FILE_fds();

	/* "we are in a function, ok to use return" */
	G_flag_return_in_progress = -1;
	enter_var_nest_level();
	IF_HUSH_LOCAL(G.func_nest_level++;)

	/* On MMU, funcp->body is always non-NULL */
	n = run_list(funcp->body);
	fflush_all();
	_exit(n);
# else
//?	close_saved_fds_and_FILE_fds();

//TODO: check whether "true | func_with_return" works

	re_execute_shell(to_free,
			funcp->body_as_string,
			G.global_argv[0],
			argv + 1,
			NULL);
# endif
}

static int run_function(const struct function *funcp, char **argv)
{
	int rc;
	save_arg_t sv;
	smallint sv_flg;

	save_and_replace_G_args(&sv, argv);

	/* "We are in function, ok to use return" */
	sv_flg = G_flag_return_in_progress;
	G_flag_return_in_progress = -1;

	/* Make "local" variables properly shadow previous ones */
	IF_HUSH_LOCAL(enter_var_nest_level();)
	IF_HUSH_LOCAL(G.func_nest_level++;)

	/* On MMU, funcp->body is always non-NULL */
# if !BB_MMU
	if (!funcp->body) {
		/* Function defined by -F */
		parse_and_run_string(funcp->body_as_string);
		rc = G.last_exitcode;
	} else
# endif
	{
		rc = run_list(funcp->body);
	}

	IF_HUSH_LOCAL(G.func_nest_level--;)
	IF_HUSH_LOCAL(leave_var_nest_level();)

	G_flag_return_in_progress = sv_flg;

	restore_G_args(&sv, argv);

	return rc;
}
#endif /* ENABLE_HUSH_FUNCTIONS */


#if BB_MMU
#define exec_builtin(to_free, x, argv) \
	exec_builtin(x, argv)
#else
#define exec_builtin(to_free, x, argv) \
	exec_builtin(to_free, argv)
#endif
static void exec_builtin(char ***to_free,
		const struct built_in_command *x,
		char **argv) NORETURN;
static void exec_builtin(char ***to_free,
		const struct built_in_command *x,
		char **argv)
{
#if BB_MMU
	int rcode;
	fflush_all();
//?	close_saved_fds_and_FILE_fds();
	rcode = x->b_function(argv);
	fflush_all();
	_exit(rcode);
#else
	fflush_all();
	/* On NOMMU, we must never block!
	 * Example: { sleep 99 | read line; } & echo Ok
	 */
	re_execute_shell(to_free,
			argv[0],
			G.global_argv[0],
			G.global_argv + 1,
			argv);
#endif
}


static void execvp_or_die(char **argv) NORETURN;
static void execvp_or_die(char **argv)
{
	int e;
	debug_printf_exec("execing '%s'\n", argv[0]);
	/* Don't propagate SIG_IGN to the child */
	if (SPECIAL_JOBSTOP_SIGS != 0)
		switch_off_special_sigs(G.special_sig_mask & SPECIAL_JOBSTOP_SIGS);
	execvp(argv[0], argv);
	e = 2;
	if (errno == EACCES) e = 126;
	if (errno == ENOENT) e = 127;
	bb_perror_msg("can't execute '%s'", argv[0]);
	_exit(e);
}

#if ENABLE_HUSH_MODE_X
static void dump_cmd_in_x_mode(char **argv)
{
	if (G_x_mode && argv) {
		/* We want to output the line in one write op */
		char *buf, *p;
		int len;
		int n;

		len = 3;
		n = 0;
		while (argv[n])
			len += strlen(argv[n++]) + 1;
		buf = xmalloc(len);
		buf[0] = '+';
		p = buf + 1;
		n = 0;
		while (argv[n])
			p += sprintf(p, " %s", argv[n++]);
		*p++ = '\n';
		*p = '\0';
		fputs(buf, stderr);
		free(buf);
	}
}
#else
# define dump_cmd_in_x_mode(argv) ((void)0)
#endif

#if ENABLE_HUSH_COMMAND
static void if_command_vV_print_and_exit(char opt_vV, char *cmd, const char *explanation)
{
	char *to_free;

	if (!opt_vV)
		return;

	to_free = NULL;
	if (!explanation) {
		char *path = getenv("PATH");
		explanation = to_free = find_executable(cmd, &path); /* path == NULL is ok */
		if (!explanation)
			_exit(1); /* PROG was not found */
		if (opt_vV != 'V')
			cmd = to_free; /* -v PROG prints "/path/to/PROG" */
	}
	printf((opt_vV == 'V') ? "%s is %s\n" : "%s\n", cmd, explanation);
	free(to_free);
	fflush_all();
	_exit(0);
}
#else
# define if_command_vV_print_and_exit(a,b,c) ((void)0)
#endif

#if BB_MMU
#define pseudo_exec_argv(nommu_save, argv, assignment_cnt, argv_expanded) \
	pseudo_exec_argv(argv, assignment_cnt, argv_expanded)
#define pseudo_exec(nommu_save, command, argv_expanded) \
	pseudo_exec(command, argv_expanded)
#endif

/* Called after [v]fork() in run_pipe, or from builtin_exec.
 * Never returns.
 * Don't exit() here.  If you don't exec, use _exit instead.
 * The at_exit handlers apparently confuse the calling process,
 * in particular stdin handling. Not sure why? -- because of vfork! (vda)
 */
static void pseudo_exec_argv(nommu_save_t *nommu_save,
		char **argv, int assignment_cnt,
		char **argv_expanded) NORETURN;
static NOINLINE void pseudo_exec_argv(nommu_save_t *nommu_save,
		char **argv, int assignment_cnt,
		char **argv_expanded)
{
	const struct built_in_command *x;
	struct variable **sv_shadowed;
	char **new_env;
	IF_HUSH_COMMAND(char opt_vV = 0;)
	IF_HUSH_FUNCTIONS(const struct function *funcp;)

	new_env = expand_assignments(argv, assignment_cnt);
	dump_cmd_in_x_mode(new_env);

	if (!argv[assignment_cnt]) {
		/* Case when we are here: ... | var=val | ...
		 * (note that we do not exit early, i.e., do not optimize out
		 * expand_assignments(): think about ... | var=`sleep 1` | ...
		 */
		free_strings(new_env);
		_exit(EXIT_SUCCESS);
	}

	sv_shadowed = G.shadowed_vars_pp;
#if BB_MMU
	G.shadowed_vars_pp = NULL; /* "don't save, free them instead" */
#else
	G.shadowed_vars_pp = &nommu_save->old_vars;
	G.var_nest_level++;
#endif
	set_vars_and_save_old(new_env);
	G.shadowed_vars_pp = sv_shadowed;

	if (argv_expanded) {
		argv = argv_expanded;
	} else {
		argv = expand_strvec_to_strvec(argv + assignment_cnt);
#if !BB_MMU
		nommu_save->argv = argv;
#endif
	}
	dump_cmd_in_x_mode(argv);

#if ENABLE_FEATURE_SH_STANDALONE || BB_MMU
	if (strchr(argv[0], '/') != NULL)
		goto skip;
#endif

#if ENABLE_HUSH_FUNCTIONS
	/* Check if the command matches any functions (this goes before bltins) */
	funcp = find_function(argv[0]);
	if (funcp)
		exec_function(&nommu_save->argv_from_re_execing, funcp, argv);
#endif

#if ENABLE_HUSH_COMMAND
	/* "command BAR": run BAR without looking it up among functions
	 * "command -v BAR": print "BAR" or "/path/to/BAR"; or exit 1
	 * "command -V BAR": print "BAR is {a function,a shell builtin,/path/to/BAR}"
	 */
	while (strcmp(argv[0], "command") == 0 && argv[1]) {
		char *p;

		argv++;
		p = *argv;
		if (p[0] != '-' || !p[1])
			continue; /* bash allows "command command command [-OPT] BAR" */

		for (;;) {
			p++;
			switch (*p) {
			case '\0':
				argv++;
				p = *argv;
				if (p[0] != '-' || !p[1])
					goto after_opts;
				continue; /* next arg is also -opts, process it too */
			case 'v':
			case 'V':
				opt_vV = *p;
				continue;
			default:
				bb_error_msg_and_die("%s: %s: invalid option", "command", argv[0]);
			}
		}
	}
 after_opts:
# if ENABLE_HUSH_FUNCTIONS
	if (opt_vV && find_function(argv[0]))
		if_command_vV_print_and_exit(opt_vV, argv[0], "a function");
# endif
#endif

	/* Check if the command matches any of the builtins.
	 * Depending on context, this might be redundant.  But it's
	 * easier to waste a few CPU cycles than it is to figure out
	 * if this is one of those cases.
	 */
	/* Why "BB_MMU ? :" difference in logic? -
	 * On NOMMU, it is more expensive to re-execute shell
	 * just in order to run echo or test builtin.
	 * It's better to skip it here and run corresponding
	 * non-builtin later. */
	x = BB_MMU ? find_builtin(argv[0]) : find_builtin1(argv[0]);
	if (x) {
		if_command_vV_print_and_exit(opt_vV, argv[0], "a shell builtin");
		exec_builtin(&nommu_save->argv_from_re_execing, x, argv);
	}

#if ENABLE_FEATURE_SH_STANDALONE
	/* Check if the command matches any busybox applets */
	{
		int a = find_applet_by_name(argv[0]);
		if (a >= 0) {
			if_command_vV_print_and_exit(opt_vV, argv[0], "an applet");
# if BB_MMU /* see above why on NOMMU it is not allowed */
			if (APPLET_IS_NOEXEC(a)) {
				/* Do not leak open fds from opened script files etc.
				 * Testcase: interactive "ls -l /proc/self/fd"
				 * should not show tty fd open.
				 */
				close_saved_fds_and_FILE_fds();
//FIXME: should also close saved redir fds
//This casuses test failures in
//redir_children_should_not_see_saved_fd_2.tests
//redir_children_should_not_see_saved_fd_3.tests
//if you replace "busybox find" with just "find" in them
				/* Without this, "rm -i FILE" can't be ^C'ed: */
				switch_off_special_sigs(G.special_sig_mask);
				debug_printf_exec("running applet '%s'\n", argv[0]);
				run_noexec_applet_and_exit(a, argv[0], argv);
			}
# endif
			/* Re-exec ourselves */
			debug_printf_exec("re-execing applet '%s'\n", argv[0]);
			/* Don't propagate SIG_IGN to the child */
			if (SPECIAL_JOBSTOP_SIGS != 0)
				switch_off_special_sigs(G.special_sig_mask & SPECIAL_JOBSTOP_SIGS);
			execv(bb_busybox_exec_path, argv);
			/* If they called chroot or otherwise made the binary no longer
			 * executable, fall through */
		}
	}
#endif

#if ENABLE_FEATURE_SH_STANDALONE || BB_MMU
 skip:
#endif
	if_command_vV_print_and_exit(opt_vV, argv[0], NULL);
	execvp_or_die(argv);
}

/* Called after [v]fork() in run_pipe
 */
static void pseudo_exec(nommu_save_t *nommu_save,
		struct command *command,
		char **argv_expanded) NORETURN;
static void pseudo_exec(nommu_save_t *nommu_save,
		struct command *command,
		char **argv_expanded)
{
#if ENABLE_HUSH_FUNCTIONS
	if (command->cmd_type == CMD_FUNCDEF) {
		/* Ignore funcdefs in pipes:
		 * true | f() { cmd }
		 */
		_exit(0);
	}
#endif

	if (command->argv) {
		pseudo_exec_argv(nommu_save, command->argv,
				command->assignment_cnt, argv_expanded);
	}

	if (command->group) {
		/* Cases when we are here:
		 * ( list )
		 * { list } &
		 * ... | ( list ) | ...
		 * ... | { list } | ...
		 */
#if BB_MMU
		int rcode;
		debug_printf_exec("pseudo_exec: run_list\n");
		reset_traps_to_defaults();
		rcode = run_list(command->group);
		/* OK to leak memory by not calling free_pipe_list,
		 * since this process is about to exit */
		_exit(rcode);
#else
		re_execute_shell(&nommu_save->argv_from_re_execing,
				command->group_as_string,
				G.global_argv[0],
				G.global_argv + 1,
				NULL);
#endif
	}

	/* Case when we are here: ... | >file */
	debug_printf_exec("pseudo_exec'ed null command\n");
	_exit(EXIT_SUCCESS);
}

#if ENABLE_HUSH_JOB
static const char *get_cmdtext(struct pipe *pi)
{
	char **argv;
	char *p;
	int len;

	/* This is subtle. ->cmdtext is created only on first backgrounding.
	 * (Think "cat, <ctrl-z>, fg, <ctrl-z>, fg, <ctrl-z>...." here...)
	 * On subsequent bg argv is trashed, but we won't use it */
	if (pi->cmdtext)
		return pi->cmdtext;

	argv = pi->cmds[0].argv;
	if (!argv) {
		pi->cmdtext = xzalloc(1);
		return pi->cmdtext;
	}
	len = 0;
	do {
		len += strlen(*argv) + 1;
	} while (*++argv);
	p = xmalloc(len);
	pi->cmdtext = p;
	argv = pi->cmds[0].argv;
	do {
		p = stpcpy(p, *argv);
		*p++ = ' ';
	} while (*++argv);
	p[-1] = '\0';
	return pi->cmdtext;
}

static void remove_job_from_table(struct pipe *pi)
{
	struct pipe *prev_pipe;

	if (pi == G.job_list) {
		G.job_list = pi->next;
	} else {
		prev_pipe = G.job_list;
		while (prev_pipe->next != pi)
			prev_pipe = prev_pipe->next;
		prev_pipe->next = pi->next;
	}
	G.last_jobid = 0;
	if (G.job_list)
		G.last_jobid = G.job_list->jobid;
}

static void delete_finished_job(struct pipe *pi)
{
	remove_job_from_table(pi);
	free_pipe(pi);
}

static void clean_up_last_dead_job(void)
{
	if (G.job_list && !G.job_list->alive_cmds)
		delete_finished_job(G.job_list);
}

static void insert_job_into_table(struct pipe *pi)
{
	struct pipe *job, **jobp;
	int i;

	clean_up_last_dead_job();

	/* Find the end of the list, and find next job ID to use */
	i = 0;
	jobp = &G.job_list;
	while ((job = *jobp) != NULL) {
		if (job->jobid > i)
			i = job->jobid;
		jobp = &job->next;
	}
	pi->jobid = i + 1;

	/* Create a new job struct at the end */
	job = *jobp = xmemdup(pi, sizeof(*pi));
	job->next = NULL;
	job->cmds = xzalloc(sizeof(pi->cmds[0]) * pi->num_cmds);
	/* Cannot copy entire pi->cmds[] vector! This causes double frees */
	for (i = 0; i < pi->num_cmds; i++) {
		job->cmds[i].pid = pi->cmds[i].pid;
		/* all other fields are not used and stay zero */
	}
	job->cmdtext = xstrdup(get_cmdtext(pi));

	if (G_interactive_fd)
		printf("[%u] %u %s\n", job->jobid, (unsigned)job->cmds[0].pid, job->cmdtext);
	G.last_jobid = job->jobid;
}
#endif /* JOB */

static int job_exited_or_stopped(struct pipe *pi)
{
	int rcode, i;

	if (pi->alive_cmds != pi->stopped_cmds)
		return -1;

	/* All processes in fg pipe have exited or stopped */
	rcode = 0;
	i = pi->num_cmds;
	while (--i >= 0) {
		rcode = pi->cmds[i].cmd_exitcode;
		/* usually last process gives overall exitstatus,
		 * but with "set -o pipefail", last *failed* process does */
		if (G.o_opt[OPT_O_PIPEFAIL] == 0 || rcode != 0)
			break;
	}
	IF_HAS_KEYWORDS(if (pi->pi_inverted) rcode = !rcode;)
	return rcode;
}

static int process_wait_result(struct pipe *fg_pipe, pid_t childpid, int status)
{
#if ENABLE_HUSH_JOB
	struct pipe *pi;
#endif
	int i, dead;

	dead = WIFEXITED(status) || WIFSIGNALED(status);

#if DEBUG_JOBS
	if (WIFSTOPPED(status))
		debug_printf_jobs("pid %d stopped by sig %d (exitcode %d)\n",
				childpid, WSTOPSIG(status), WEXITSTATUS(status));
	if (WIFSIGNALED(status))
		debug_printf_jobs("pid %d killed by sig %d (exitcode %d)\n",
				childpid, WTERMSIG(status), WEXITSTATUS(status));
	if (WIFEXITED(status))
		debug_printf_jobs("pid %d exited, exitcode %d\n",
				childpid, WEXITSTATUS(status));
#endif
	/* Were we asked to wait for a fg pipe? */
	if (fg_pipe) {
		i = fg_pipe->num_cmds;

		while (--i >= 0) {
			int rcode;

			debug_printf_jobs("check pid %d\n", fg_pipe->cmds[i].pid);
			if (fg_pipe->cmds[i].pid != childpid)
				continue;
			if (dead) {
				int ex;
				fg_pipe->cmds[i].pid = 0;
				fg_pipe->alive_cmds--;
				ex = WEXITSTATUS(status);
				/* bash prints killer signal's name for *last*
				 * process in pipe (prints just newline for SIGINT/SIGPIPE).
				 * Mimic this. Example: "sleep 5" + (^\ or kill -QUIT)
				 */
				if (WIFSIGNALED(status)) {
					int sig = WTERMSIG(status);
					if (i == fg_pipe->num_cmds-1)
						/* TODO: use strsignal() instead for bash compat? but that's bloat... */
						puts(sig == SIGINT || sig == SIGPIPE ? "" : get_signame(sig));
					/* TODO: if (WCOREDUMP(status)) + " (core dumped)"; */
					/* TODO: MIPS has 128 sigs (1..128), what if sig==128 here?
					 * Maybe we need to use sig | 128? */
					ex = sig + 128;
				}
				fg_pipe->cmds[i].cmd_exitcode = ex;
			} else {
				fg_pipe->stopped_cmds++;
			}
			debug_printf_jobs("fg_pipe: alive_cmds %d stopped_cmds %d\n",
					fg_pipe->alive_cmds, fg_pipe->stopped_cmds);
			rcode = job_exited_or_stopped(fg_pipe);
			if (rcode >= 0) {
/* Note: *non-interactive* bash does not continue if all processes in fg pipe
 * are stopped. Testcase: "cat | cat" in a script (not on command line!)
 * and "killall -STOP cat" */
				if (G_interactive_fd) {
#if ENABLE_HUSH_JOB
					if (fg_pipe->alive_cmds != 0)
						insert_job_into_table(fg_pipe);
#endif
					return rcode;
				}
				if (fg_pipe->alive_cmds == 0)
					return rcode;
			}
			/* There are still running processes in the fg_pipe */
			return -1;
		}
		/* It wasn't in fg_pipe, look for process in bg pipes */
	}

#if ENABLE_HUSH_JOB
	/* We were asked to wait for bg or orphaned children */
	/* No need to remember exitcode in this case */
	for (pi = G.job_list; pi; pi = pi->next) {
		for (i = 0; i < pi->num_cmds; i++) {
			if (pi->cmds[i].pid == childpid)
				goto found_pi_and_prognum;
		}
	}
	/* Happens when shell is used as init process (init=/bin/sh) */
	debug_printf("checkjobs: pid %d was not in our list!\n", childpid);
	return -1; /* this wasn't a process from fg_pipe */

 found_pi_and_prognum:
	if (dead) {
		/* child exited */
		int rcode = WEXITSTATUS(status);
		if (WIFSIGNALED(status))
			rcode = 128 + WTERMSIG(status);
		pi->cmds[i].cmd_exitcode = rcode;
		if (G.last_bg_pid == pi->cmds[i].pid)
			G.last_bg_pid_exitcode = rcode;
		pi->cmds[i].pid = 0;
		pi->alive_cmds--;
		if (!pi->alive_cmds) {
			if (G_interactive_fd) {
				printf(JOB_STATUS_FORMAT, pi->jobid,
						"Done", pi->cmdtext);
				delete_finished_job(pi);
			} else {
/*
 * bash deletes finished jobs from job table only in interactive mode,
 * after "jobs" cmd, or if pid of a new process matches one of the old ones
 * (see cleanup_dead_jobs(), delete_old_job(), J_NOTIFIED in bash source).
 * Testcase script: "(exit 3) & sleep 1; wait %1; echo $?" prints 3 in bash.
 * We only retain one "dead" job, if it's the single job on the list.
 * This covers most of real-world scenarios where this is useful.
 */
				if (pi != G.job_list)
					delete_finished_job(pi);
			}
		}
	} else {
		/* child stopped */
		pi->stopped_cmds++;
	}
#endif
	return -1; /* this wasn't a process from fg_pipe */
}

/* Check to see if any processes have exited -- if they have,
 * figure out why and see if a job has completed.
 *
 * If non-NULL fg_pipe: wait for its completion or stop.
 * Return its exitcode or zero if stopped.
 *
 * Alternatively (fg_pipe == NULL, waitfor_pid != 0):
 * waitpid(WNOHANG), if waitfor_pid exits or stops, return exitcode+1,
 * else return <0 if waitpid errors out (e.g. ECHILD: nothing to wait for)
 * or 0 if no children changed status.
 *
 * Alternatively (fg_pipe == NULL, waitfor_pid == 0),
 * return <0 if waitpid errors out (e.g. ECHILD: nothing to wait for)
 * or 0 if no children changed status.
 */
static int checkjobs(struct pipe *fg_pipe, pid_t waitfor_pid)
{
	int attributes;
	int status;
	int rcode = 0;

	debug_printf_jobs("checkjobs %p\n", fg_pipe);

	attributes = WUNTRACED;
	if (fg_pipe == NULL)
		attributes |= WNOHANG;

	errno = 0;
#if ENABLE_HUSH_FAST
	if (G.handled_SIGCHLD == G.count_SIGCHLD) {
//bb_error_msg("[%d] checkjobs: G.count_SIGCHLD:%d G.handled_SIGCHLD:%d children?:%d fg_pipe:%p",
//getpid(), G.count_SIGCHLD, G.handled_SIGCHLD, G.we_have_children, fg_pipe);
		/* There was neither fork nor SIGCHLD since last waitpid */
		/* Avoid doing waitpid syscall if possible */
		if (!G.we_have_children) {
			errno = ECHILD;
			return -1;
		}
		if (fg_pipe == NULL) { /* is WNOHANG set? */
			/* We have children, but they did not exit
			 * or stop yet (we saw no SIGCHLD) */
			return 0;
		}
		/* else: !WNOHANG, waitpid will block, can't short-circuit */
	}
#endif

/* Do we do this right?
 * bash-3.00# sleep 20 | false
 * <ctrl-Z pressed>
 * [3]+  Stopped          sleep 20 | false
 * bash-3.00# echo $?
 * 1   <========== bg pipe is not fully done, but exitcode is already known!
 * [hush 1.14.0: yes we do it right]
 */
	while (1) {
		pid_t childpid;
#if ENABLE_HUSH_FAST
		int i;
		i = G.count_SIGCHLD;
#endif
		childpid = waitpid(-1, &status, attributes);
		if (childpid <= 0) {
			if (childpid && errno != ECHILD)
				bb_perror_msg("waitpid");
#if ENABLE_HUSH_FAST
			else { /* Until next SIGCHLD, waitpid's are useless */
				G.we_have_children = (childpid == 0);
				G.handled_SIGCHLD = i;
//bb_error_msg("[%d] checkjobs: waitpid returned <= 0, G.count_SIGCHLD:%d G.handled_SIGCHLD:%d", getpid(), G.count_SIGCHLD, G.handled_SIGCHLD);
			}
#endif
			/* ECHILD (no children), or 0 (no change in children status) */
			rcode = childpid;
			break;
		}
		rcode = process_wait_result(fg_pipe, childpid, status);
		if (rcode >= 0) {
			/* fg_pipe exited or stopped */
			break;
		}
		if (childpid == waitfor_pid) {
			debug_printf_exec("childpid==waitfor_pid:%d status:0x%08x\n", childpid, status);
			rcode = WEXITSTATUS(status);
			if (WIFSIGNALED(status))
				rcode = 128 + WTERMSIG(status);
			if (WIFSTOPPED(status))
				/* bash: "cmd & wait $!" and cmd stops: $? = 128 + stopsig */
				rcode = 128 + WSTOPSIG(status);
			rcode++;
			break; /* "wait PID" called us, give it exitcode+1 */
		}
		/* This wasn't one of our processes, or */
		/* fg_pipe still has running processes, do waitpid again */
	} /* while (waitpid succeeds)... */

	return rcode;
}

#if ENABLE_HUSH_JOB
static int checkjobs_and_fg_shell(struct pipe *fg_pipe)
{
	pid_t p;
	int rcode = checkjobs(fg_pipe, 0 /*(no pid to wait for)*/);
	if (G_saved_tty_pgrp) {
		/* Job finished, move the shell to the foreground */
		p = getpgrp(); /* our process group id */
		debug_printf_jobs("fg'ing ourself: getpgrp()=%d\n", (int)p);
		tcsetpgrp(G_interactive_fd, p);
	}
	return rcode;
}
#endif

/* Start all the jobs, but don't wait for anything to finish.
 * See checkjobs().
 *
 * Return code is normally -1, when the caller has to wait for children
 * to finish to determine the exit status of the pipe.  If the pipe
 * is a simple builtin command, however, the action is done by the
 * time run_pipe returns, and the exit code is provided as the
 * return value.
 *
 * Returns -1 only if started some children. IOW: we have to
 * mask out retvals of builtins etc with 0xff!
 *
 * The only case when we do not need to [v]fork is when the pipe
 * is single, non-backgrounded, non-subshell command. Examples:
 * cmd ; ...   { list } ; ...
 * cmd && ...  { list } && ...
 * cmd || ...  { list } || ...
 * If it is, then we can run cmd as a builtin, NOFORK,
 * or (if SH_STANDALONE) an applet, and we can run the { list }
 * with run_list. If it isn't one of these, we fork and exec cmd.
 *
 * Cases when we must fork:
 * non-single:   cmd | cmd
 * backgrounded: cmd &     { list } &
 * subshell:     ( list ) [&]
 */
#if !ENABLE_HUSH_MODE_X
#define redirect_and_varexp_helper(command, squirrel, argv_expanded) \
	redirect_and_varexp_helper(command, squirrel)
#endif
static int redirect_and_varexp_helper(
		struct command *command,
		struct squirrel **sqp,
		char **argv_expanded)
{
	/* Assignments occur before redirects. Try:
	 * a=`sleep 1` sleep 2 3>/qwe/rty
	 */

	char **new_env = expand_assignments(command->argv, command->assignment_cnt);
	dump_cmd_in_x_mode(new_env);
	dump_cmd_in_x_mode(argv_expanded);
	/* this takes ownership of new_env[i] elements, and frees new_env: */
	set_vars_and_save_old(new_env);

	/* setup_redirects acts on file descriptors, not FILEs.
	 * This is perfect for work that comes after exec().
	 * Is it really safe for inline use?  Experimentally,
	 * things seem to work. */
	return setup_redirects(command, sqp);
}
static NOINLINE int run_pipe(struct pipe *pi)
{
	static const char *const null_ptr = NULL;

	int cmd_no;
	int next_infd;
	struct command *command;
	char **argv_expanded;
	char **argv;
	struct squirrel *squirrel = NULL;
	int rcode;

	debug_printf_exec("run_pipe start: members:%d\n", pi->num_cmds);
	debug_enter();

	/* Testcase: set -- q w e; (IFS='' echo "$*"; IFS=''; echo "$*"); echo "$*"
	 * Result should be 3 lines: q w e, qwe, q w e
	 */
	if (G.ifs_whitespace != G.ifs)
		free(G.ifs_whitespace);
	G.ifs = get_local_var_value("IFS");
	if (G.ifs) {
		char *p;
		G.ifs_whitespace = (char*)G.ifs;
		p = skip_whitespace(G.ifs);
		if (*p) {
			/* Not all $IFS is whitespace */
			char *d;
			int len = p - G.ifs;
			p = skip_non_whitespace(p);
			G.ifs_whitespace = xmalloc(len + strlen(p) + 1); /* can overestimate */
			d = mempcpy(G.ifs_whitespace, G.ifs, len);
			while (*p) {
				if (isspace(*p))
					*d++ = *p;
				p++;
			}
			*d = '\0';
		}
	} else {
		G.ifs = defifs;
		G.ifs_whitespace = (char*)G.ifs;
	}

	IF_HUSH_JOB(pi->pgrp = -1;)
	pi->stopped_cmds = 0;
	command = &pi->cmds[0];
	argv_expanded = NULL;

	if (pi->num_cmds != 1
	 || pi->followup == PIPE_BG
	 || command->cmd_type == CMD_SUBSHELL
	) {
		goto must_fork;
	}

	pi->alive_cmds = 1;

	debug_printf_exec(": group:%p argv:'%s'\n",
		command->group, command->argv ? command->argv[0] : "NONE");

	if (command->group) {
#if ENABLE_HUSH_FUNCTIONS
		if (command->cmd_type == CMD_FUNCDEF) {
			/* "executing" func () { list } */
			struct function *funcp;

			funcp = new_function(command->argv[0]);
			/* funcp->name is already set to argv[0] */
			funcp->body = command->group;
# if !BB_MMU
			funcp->body_as_string = command->group_as_string;
			command->group_as_string = NULL;
# endif
			command->group = NULL;
			command->argv[0] = NULL;
			debug_printf_exec("cmd %p has child func at %p\n", command, funcp);
			funcp->parent_cmd = command;
			command->child_func = funcp;

			debug_printf_exec("run_pipe: return EXIT_SUCCESS\n");
			debug_leave();
			return EXIT_SUCCESS;
		}
#endif
		/* { list } */
		debug_printf("non-subshell group\n");
		rcode = 1; /* exitcode if redir failed */
		if (setup_redirects(command, &squirrel) == 0) {
			debug_printf_exec(": run_list\n");
//FIXME: we need to pass squirrel down into run_list()
//for SH_STANDALONE case, or else this construct:
// { find /proc/self/fd; true; } >FILE; cmd2
//has no way of closing saved fd#1 for "find",
//and in SH_STANDALONE mode, "find" is not execed,
//therefore CLOEXEC on saved fd does not help.
			rcode = run_list(command->group) & 0xff;
		}
		restore_redirects(squirrel);
		IF_HAS_KEYWORDS(if (pi->pi_inverted) rcode = !rcode;)
		debug_leave();
		debug_printf_exec("run_pipe: return %d\n", rcode);
		return rcode;
	}

	argv = command->argv ? command->argv : (char **) &null_ptr;
	{
		const struct built_in_command *x;
		IF_HUSH_FUNCTIONS(const struct function *funcp;)
		IF_NOT_HUSH_FUNCTIONS(enum { funcp = 0 };)
		struct variable **sv_shadowed;
		struct variable *old_vars;

#if ENABLE_HUSH_LINENO_VAR
		if (G.lineno_var)
			strcpy(G.lineno_var + sizeof("LINENO=")-1, utoa(command->lineno));
#endif

		if (argv[command->assignment_cnt] == NULL) {
			/* Assignments, but no command.
			 * Ensure redirects take effect (that is, create files).
			 * Try "a=t >file"
			 */
			unsigned i;
			G.expand_exitcode = 0;
 only_assignments:
			rcode = setup_redirects(command, &squirrel);
			restore_redirects(squirrel);

			/* Set shell variables */
			if (G_x_mode)
				bb_putchar_stderr('+');
			i = 0;
			while (i < command->assignment_cnt) {
				char *p = expand_string_to_string(argv[i],
						EXP_FLAG_ESC_GLOB_CHARS,
						/*unbackslash:*/ 1
				);
				if (G_x_mode)
					fprintf(stderr, " %s", p);
				debug_printf_env("set shell var:'%s'->'%s'\n", *argv, p);
				if (set_local_var(p, /*flag:*/ 0)) {
					/* assignment to readonly var / putenv error? */
					rcode = 1;
				}
				i++;
			}
			if (G_x_mode)
				bb_putchar_stderr('\n');
			/* Redirect error sets $? to 1. Otherwise,
			 * if evaluating assignment value set $?, retain it.
			 * Else, clear $?:
			 *  false; q=`exit 2`; echo $? - should print 2
			 *  false; x=1; echo $? - should print 0
			 * Because of the 2nd case, we can't just use G.last_exitcode.
			 */
			if (rcode == 0)
				rcode = G.expand_exitcode;
			IF_HAS_KEYWORDS(if (pi->pi_inverted) rcode = !rcode;)
			debug_leave();
			debug_printf_exec("run_pipe: return %d\n", rcode);
			return rcode;
		}

		/* Expand the rest into (possibly) many strings each */
#if defined(CMD_SINGLEWORD_NOGLOB)
		if (command->cmd_type == CMD_SINGLEWORD_NOGLOB)
			argv_expanded = expand_strvec_to_strvec_singleword_noglob(argv + command->assignment_cnt);
		else
#endif
			argv_expanded = expand_strvec_to_strvec(argv + command->assignment_cnt);

		/* If someone gives us an empty string: `cmd with empty output` */
		if (!argv_expanded[0]) {
			free(argv_expanded);
			/* `false` still has to set exitcode 1 */
			G.expand_exitcode = G.last_exitcode;
			goto only_assignments;
		}

		old_vars = NULL;
		sv_shadowed = G.shadowed_vars_pp;

		/* Check if argv[0] matches any functions (this goes before bltins) */
		IF_HUSH_FUNCTIONS(funcp = find_function(argv_expanded[0]);)
		IF_HUSH_FUNCTIONS(x = NULL;)
		IF_HUSH_FUNCTIONS(if (!funcp))
			x = find_builtin(argv_expanded[0]);
		if (x || funcp) {
			if (x && x->b_function == builtin_exec && argv_expanded[1] == NULL) {
				debug_printf("exec with redirects only\n");
				/*
				 * Variable assignments are executed, but then "forgotten":
				 *  a=`sleep 1;echo A` exec 3>&-; echo $a
				 * sleeps, but prints nothing.
				 */
				enter_var_nest_level();
				G.shadowed_vars_pp = &old_vars;
				rcode = redirect_and_varexp_helper(command, /*squirrel:*/ NULL, argv_expanded);
				G.shadowed_vars_pp = sv_shadowed;
				/* rcode=1 can be if redir file can't be opened */

				goto clean_up_and_ret1;
			}

			/* Bump var nesting, or this will leak exported $a:
			 * a=b true; env | grep ^a=
			 */
			enter_var_nest_level();
			/* Collect all variables "shadowed" by helper
			 * (IOW: old vars overridden by "var1=val1 var2=val2 cmd..." syntax)
			 * into old_vars list:
			 */
			G.shadowed_vars_pp = &old_vars;
			rcode = redirect_and_varexp_helper(command, &squirrel, argv_expanded);
			if (rcode == 0) {
				if (!funcp) {
					/* Do not collect *to old_vars list* vars shadowed
					 * by e.g. "local VAR" builtin (collect them
					 * in the previously nested list instead):
					 * don't want them to be restored immediately
					 * after "local" completes.
					 */
					G.shadowed_vars_pp = sv_shadowed;

					debug_printf_exec(": builtin '%s' '%s'...\n",
						x->b_cmd, argv_expanded[1]);
					fflush_all();
					rcode = x->b_function(argv_expanded) & 0xff;
					fflush_all();
				}
#if ENABLE_HUSH_FUNCTIONS
				else {
					debug_printf_exec(": function '%s' '%s'...\n",
						funcp->name, argv_expanded[1]);
					rcode = run_function(funcp, argv_expanded) & 0xff;
					/*
					 * But do collect *to old_vars list* vars shadowed
					 * within function execution. To that end, restore
					 * this pointer _after_ function run:
					 */
					G.shadowed_vars_pp = sv_shadowed;
				}
#endif
			}
		} else
		if (ENABLE_FEATURE_SH_NOFORK && NUM_APPLETS > 1) {
			int n = find_applet_by_name(argv_expanded[0]);
			if (n < 0 || !APPLET_IS_NOFORK(n))
				goto must_fork;

			enter_var_nest_level();
			/* Collect all variables "shadowed" by helper into old_vars list */
			G.shadowed_vars_pp = &old_vars;
			rcode = redirect_and_varexp_helper(command, &squirrel, argv_expanded);
			G.shadowed_vars_pp = sv_shadowed;

			if (rcode == 0) {
				debug_printf_exec(": run_nofork_applet '%s' '%s'...\n",
					argv_expanded[0], argv_expanded[1]);
				/*
				 * Note: signals (^C) can't interrupt here.
				 * We remember them and they will be acted upon
				 * after applet returns.
				 * This makes applets which can run for a long time
				 * and/or wait for user input ineligible for NOFORK:
				 * for example, "yes" or "rm" (rm -i waits for input).
				 */
				rcode = run_nofork_applet(n, argv_expanded);
			}
		} else
			goto must_fork;

		restore_redirects(squirrel);
 clean_up_and_ret1:
		leave_var_nest_level();
		add_vars(old_vars);

		/*
		 * Try "usleep 99999999" + ^C + "echo $?"
		 * with FEATURE_SH_NOFORK=y.
		 */
		if (!funcp) {
			/* It was builtin or nofork.
			 * if this would be a real fork/execed program,
			 * it should have died if a fatal sig was received.
			 * But OTOH, there was no separate process,
			 * the sig was sent to _shell_, not to non-existing
			 * child.
			 * Let's just handle ^C only, this one is obvious:
			 * we aren't ok with exitcode 0 when ^C was pressed
			 * during builtin/nofork.
			 */
			if (sigismember(&G.pending_set, SIGINT))
				rcode = 128 + SIGINT;
		}
		free(argv_expanded);
		IF_HAS_KEYWORDS(if (pi->pi_inverted) rcode = !rcode;)
		debug_leave();
		debug_printf_exec("run_pipe return %d\n", rcode);
		return rcode;
	}

 must_fork:
	/* NB: argv_expanded may already be created, and that
	 * might include `cmd` runs! Do not rerun it! We *must*
	 * use argv_expanded if it's non-NULL */

	/* Going to fork a child per each pipe member */
	pi->alive_cmds = 0;
	next_infd = 0;

	cmd_no = 0;
	while (cmd_no < pi->num_cmds) {
		struct fd_pair pipefds;
#if !BB_MMU
		int sv_var_nest_level = G.var_nest_level;
		volatile nommu_save_t nommu_save;
		nommu_save.old_vars = NULL;
		nommu_save.argv = NULL;
		nommu_save.argv_from_re_execing = NULL;
#endif
		command = &pi->cmds[cmd_no];
		cmd_no++;
		if (command->argv) {
			debug_printf_exec(": pipe member '%s' '%s'...\n",
					command->argv[0], command->argv[1]);
		} else {
			debug_printf_exec(": pipe member with no argv\n");
		}

		/* pipes are inserted between pairs of commands */
		pipefds.rd = 0;
		pipefds.wr = 1;
		if (cmd_no < pi->num_cmds)
			xpiped_pair(pipefds);

#if ENABLE_HUSH_LINENO_VAR
		if (G.lineno_var)
			strcpy(G.lineno_var + sizeof("LINENO=")-1, utoa(command->lineno));
#endif

		command->pid = BB_MMU ? fork() : vfork();
		if (!command->pid) { /* child */
#if ENABLE_HUSH_JOB
			disable_restore_tty_pgrp_on_exit();
			CLEAR_RANDOM_T(&G.random_gen); /* or else $RANDOM repeats in child */

			/* Every child adds itself to new process group
			 * with pgid == pid_of_first_child_in_pipe */
			if (G.run_list_level == 1 && G_interactive_fd) {
				pid_t pgrp;
				pgrp = pi->pgrp;
				if (pgrp < 0) /* true for 1st process only */
					pgrp = getpid();
				if (setpgid(0, pgrp) == 0
				 && pi->followup != PIPE_BG
				 && G_saved_tty_pgrp /* we have ctty */
				) {
					/* We do it in *every* child, not just first,
					 * to avoid races */
					tcsetpgrp(G_interactive_fd, pgrp);
				}
			}
#endif
			if (pi->alive_cmds == 0 && pi->followup == PIPE_BG) {
				/* 1st cmd in backgrounded pipe
				 * should have its stdin /dev/null'ed */
				close(0);
				if (open(bb_dev_null, O_RDONLY))
					xopen("/", O_RDONLY);
			} else {
				xmove_fd(next_infd, 0);
			}
			xmove_fd(pipefds.wr, 1);
			if (pipefds.rd > 1)
				close(pipefds.rd);
			/* Like bash, explicit redirects override pipes,
			 * and the pipe fd (fd#1) is available for dup'ing:
			 * "cmd1 2>&1 | cmd2": fd#1 is duped to fd#2, thus stderr
			 * of cmd1 goes into pipe.
			 */
			if (setup_redirects(command, NULL)) {
				/* Happens when redir file can't be opened:
				 * $ hush -c 'echo FOO >&2 | echo BAR 3>/qwe/rty; echo BAZ'
				 * FOO
				 * hush: can't open '/qwe/rty': No such file or directory
				 * BAZ
				 * (echo BAR is not executed, it hits _exit(1) below)
				 */
				_exit(1);
			}

			/* Stores to nommu_save list of env vars putenv'ed
			 * (NOMMU, on MMU we don't need that) */
			/* cast away volatility... */
			pseudo_exec((nommu_save_t*) &nommu_save, command, argv_expanded);
			/* pseudo_exec() does not return */
		}

		/* parent or error */
#if ENABLE_HUSH_FAST
		G.count_SIGCHLD++;
//bb_error_msg("[%d] fork in run_pipe: G.count_SIGCHLD:%d G.handled_SIGCHLD:%d", getpid(), G.count_SIGCHLD, G.handled_SIGCHLD);
#endif
		enable_restore_tty_pgrp_on_exit();
#if !BB_MMU
		/* Clean up after vforked child */
		free(nommu_save.argv);
		free(nommu_save.argv_from_re_execing);
		G.var_nest_level = sv_var_nest_level;
		remove_nested_vars();
		add_vars(nommu_save.old_vars);
#endif
		free(argv_expanded);
		argv_expanded = NULL;
		if (command->pid < 0) { /* [v]fork failed */
			/* Clearly indicate, was it fork or vfork */
			bb_perror_msg(BB_MMU ? "vfork"+1 : "vfork");
		} else {
			pi->alive_cmds++;
#if ENABLE_HUSH_JOB
			/* Second and next children need to know pid of first one */
			if (pi->pgrp < 0)
				pi->pgrp = command->pid;
#endif
		}

		if (cmd_no > 1)
			close(next_infd);
		if (cmd_no < pi->num_cmds)
			close(pipefds.wr);
		/* Pass read (output) pipe end to next iteration */
		next_infd = pipefds.rd;
	}

	if (!pi->alive_cmds) {
		debug_leave();
		debug_printf_exec("run_pipe return 1 (all forks failed, no children)\n");
		return 1;
	}

	debug_leave();
	debug_printf_exec("run_pipe return -1 (%u children started)\n", pi->alive_cmds);
	return -1;
}

/* NB: called by pseudo_exec, and therefore must not modify any
 * global data until exec/_exit (we can be a child after vfork!) */
static int run_list(struct pipe *pi)
{
#if ENABLE_HUSH_CASE
	char *case_word = NULL;
#endif
#if ENABLE_HUSH_LOOPS
	struct pipe *loop_top = NULL;
	char **for_lcur = NULL;
	char **for_list = NULL;
#endif
	smallint last_followup;
	smalluint rcode;
#if ENABLE_HUSH_IF || ENABLE_HUSH_CASE
	smalluint cond_code = 0;
#else
	enum { cond_code = 0 };
#endif
#if HAS_KEYWORDS
	smallint rword;      /* RES_foo */
	smallint last_rword; /* ditto */
#endif

	debug_printf_exec("run_list start lvl %d\n", G.run_list_level);
	debug_enter();

#if ENABLE_HUSH_LOOPS
	/* Check syntax for "for" */
	{
		struct pipe *cpipe;
		for (cpipe = pi; cpipe; cpipe = cpipe->next) {
			if (cpipe->res_word != RES_FOR && cpipe->res_word != RES_IN)
				continue;
			/* current word is FOR or IN (BOLD in comments below) */
			if (cpipe->next == NULL) {
				syntax_error("malformed for");
				debug_leave();
				debug_printf_exec("run_list lvl %d return 1\n", G.run_list_level);
				return 1;
			}
			/* "FOR v; do ..." and "for v IN a b; do..." are ok */
			if (cpipe->next->res_word == RES_DO)
				continue;
			/* next word is not "do". It must be "in" then ("FOR v in ...") */
			if (cpipe->res_word == RES_IN /* "for v IN a b; not_do..."? */
			 || cpipe->next->res_word != RES_IN /* FOR v not_do_and_not_in..."? */
			) {
				syntax_error("malformed for");
				debug_leave();
				debug_printf_exec("run_list lvl %d return 1\n", G.run_list_level);
				return 1;
			}
		}
	}
#endif

	/* Past this point, all code paths should jump to ret: label
	 * in order to return, no direct "return" statements please.
	 * This helps to ensure that no memory is leaked. */

#if ENABLE_HUSH_JOB
	G.run_list_level++;
#endif

#if HAS_KEYWORDS
	rword = RES_NONE;
	last_rword = RES_XXXX;
#endif
	last_followup = PIPE_SEQ;
	rcode = G.last_exitcode;

	/* Go through list of pipes, (maybe) executing them. */
	for (; pi; pi = IF_HUSH_LOOPS(rword == RES_DONE ? loop_top : ) pi->next) {
		int r;
		int sv_errexit_depth;

		if (G.flag_SIGINT)
			break;
		if (G_flag_return_in_progress == 1)
			break;

		IF_HAS_KEYWORDS(rword = pi->res_word;)
		debug_printf_exec(": rword=%d cond_code=%d last_rword=%d\n",
				rword, cond_code, last_rword);

		sv_errexit_depth = G.errexit_depth;
		if (
#if ENABLE_HUSH_IF
		    rword == RES_IF || rword == RES_ELIF ||
#endif
		    pi->followup != PIPE_SEQ
		) {
			G.errexit_depth++;
		}
#if ENABLE_HUSH_LOOPS
		if ((rword == RES_WHILE || rword == RES_UNTIL || rword == RES_FOR)
		 && loop_top == NULL /* avoid bumping G.depth_of_loop twice */
		) {
			/* start of a loop: remember where loop starts */
			loop_top = pi;
			G.depth_of_loop++;
		}
#endif
		/* Still in the same "if...", "then..." or "do..." branch? */
		if (IF_HAS_KEYWORDS(rword == last_rword &&) 1) {
			if ((rcode == 0 && last_followup == PIPE_OR)
			 || (rcode != 0 && last_followup == PIPE_AND)
			) {
				/* It is "<true> || CMD" or "<false> && CMD"
				 * and we should not execute CMD */
				debug_printf_exec("skipped cmd because of || or &&\n");
				last_followup = pi->followup;
				goto dont_check_jobs_but_continue;
			}
		}
		last_followup = pi->followup;
		IF_HAS_KEYWORDS(last_rword = rword;)
#if ENABLE_HUSH_IF
		if (cond_code) {
			if (rword == RES_THEN) {
				/* if false; then ... fi has exitcode 0! */
				G.last_exitcode = rcode = EXIT_SUCCESS;
				/* "if <false> THEN cmd": skip cmd */
				continue;
			}
		} else {
			if (rword == RES_ELSE || rword == RES_ELIF) {
				/* "if <true> then ... ELSE/ELIF cmd":
				 * skip cmd and all following ones */
				break;
			}
		}
#endif
#if ENABLE_HUSH_LOOPS
		if (rword == RES_FOR) { /* && pi->num_cmds - always == 1 */
			if (!for_lcur) {
				/* first loop through for */

				static const char encoded_dollar_at[] ALIGN1 = {
					SPECIAL_VAR_SYMBOL, '@' | 0x80, SPECIAL_VAR_SYMBOL, '\0'
				}; /* encoded representation of "$@" */
				static const char *const encoded_dollar_at_argv[] = {
					encoded_dollar_at, NULL
				}; /* argv list with one element: "$@" */
				char **vals;

				vals = (char**)encoded_dollar_at_argv;
				if (pi->next->res_word == RES_IN) {
					/* if no variable values after "in" we skip "for" */
					if (!pi->next->cmds[0].argv) {
						G.last_exitcode = rcode = EXIT_SUCCESS;
						debug_printf_exec(": null FOR: exitcode EXIT_SUCCESS\n");
						break;
					}
					vals = pi->next->cmds[0].argv;
				} /* else: "for var; do..." -> assume "$@" list */
				/* create list of variable values */
				debug_print_strings("for_list made from", vals);
				for_list = expand_strvec_to_strvec(vals);
				for_lcur = for_list;
				debug_print_strings("for_list", for_list);
			}
			if (!*for_lcur) {
				/* "for" loop is over, clean up */
				free(for_list);
				for_list = NULL;
				for_lcur = NULL;
				break;
			}
			/* Insert next value from for_lcur */
			/* note: *for_lcur already has quotes removed, $var expanded, etc */
			set_local_var(xasprintf("%s=%s", pi->cmds[0].argv[0], *for_lcur++), /*flag:*/ 0);
			continue;
		}
		if (rword == RES_IN) {
			continue; /* "for v IN list;..." - "in" has no cmds anyway */
		}
		if (rword == RES_DONE) {
			continue; /* "done" has no cmds too */
		}
#endif
#if ENABLE_HUSH_CASE
		if (rword == RES_CASE) {
			debug_printf_exec("CASE cond_code:%d\n", cond_code);
			case_word = expand_string_to_string(pi->cmds->argv[0],
				EXP_FLAG_ESC_GLOB_CHARS, /*unbackslash:*/ 1);
			debug_printf_exec("CASE word1:'%s'\n", case_word);
			//unbackslash(case_word);
			//debug_printf_exec("CASE word2:'%s'\n", case_word);
			continue;
		}
		if (rword == RES_MATCH) {
			char **argv;

			debug_printf_exec("MATCH cond_code:%d\n", cond_code);
			if (!case_word) /* "case ... matched_word) ... WORD)": we executed selected branch, stop */
				break;
			/* all prev words didn't match, does this one match? */
			argv = pi->cmds->argv;
			while (*argv) {
				char *pattern;
				debug_printf_exec("expand_string_to_string('%s')\n", *argv);
				pattern = expand_string_to_string(*argv,
						EXP_FLAG_ESC_GLOB_CHARS,
						/*unbackslash:*/ 0
				);
				/* TODO: which FNM_xxx flags to use? */
				cond_code = (fnmatch(pattern, case_word, /*flags:*/ 0) != 0);
				debug_printf_exec("fnmatch(pattern:'%s',str:'%s'):%d\n",
						pattern, case_word, cond_code);
				free(pattern);
				if (cond_code == 0) {
					/* match! we will execute this branch */
					free(case_word);
					case_word = NULL; /* make future "word)" stop */
					break;
				}
				argv++;
			}
			continue;
		}
		if (rword == RES_CASE_BODY) { /* inside of a case branch */
			debug_printf_exec("CASE_BODY cond_code:%d\n", cond_code);
			if (cond_code != 0)
				continue; /* not matched yet, skip this pipe */
		}
		if (rword == RES_ESAC) {
			debug_printf_exec("ESAC cond_code:%d\n", cond_code);
			if (case_word) {
				/* "case" did not match anything: still set $? (to 0) */
				G.last_exitcode = rcode = EXIT_SUCCESS;
			}
		}
#endif
		/* Just pressing <enter> in shell should check for jobs.
		 * OTOH, in non-interactive shell this is useless
		 * and only leads to extra job checks */
		if (pi->num_cmds == 0) {
			if (G_interactive_fd)
				goto check_jobs_and_continue;
			continue;
		}

		/* After analyzing all keywords and conditions, we decided
		 * to execute this pipe. NB: have to do checkjobs(NULL)
		 * after run_pipe to collect any background children,
		 * even if list execution is to be stopped. */
		debug_printf_exec(": run_pipe with %d members\n", pi->num_cmds);
#if ENABLE_HUSH_LOOPS
		G.flag_break_continue = 0;
#endif
		rcode = r = run_pipe(pi); /* NB: rcode is a smalluint, r is int */
		if (r != -1) {
			/* We ran a builtin, function, or group.
			 * rcode is already known
			 * and we don't need to wait for anything. */
			debug_printf_exec(": builtin/func exitcode %d\n", rcode);
			G.last_exitcode = rcode;
			check_and_run_traps();
#if ENABLE_HUSH_LOOPS
			/* Was it "break" or "continue"? */
			if (G.flag_break_continue) {
				smallint fbc = G.flag_break_continue;
				/* We might fall into outer *loop*,
				 * don't want to break it too */
				if (loop_top) {
					G.depth_break_continue--;
					if (G.depth_break_continue == 0)
						G.flag_break_continue = 0;
					/* else: e.g. "continue 2" should *break* once, *then* continue */
				} /* else: "while... do... { we are here (innermost list is not a loop!) };...done" */
				if (G.depth_break_continue != 0 || fbc == BC_BREAK) {
					checkjobs(NULL, 0 /*(no pid to wait for)*/);
					break;
				}
				/* "continue": simulate end of loop */
				rword = RES_DONE;
				continue;
			}
#endif
			if (G_flag_return_in_progress == 1) {
				checkjobs(NULL, 0 /*(no pid to wait for)*/);
				break;
			}
		} else if (pi->followup == PIPE_BG) {
			/* What does bash do with attempts to background builtins? */
			/* even bash 3.2 doesn't do that well with nested bg:
			 * try "{ { sleep 10; echo DEEP; } & echo HERE; } &".
			 * I'm NOT treating inner &'s as jobs */
#if ENABLE_HUSH_JOB
			if (G.run_list_level == 1)
				insert_job_into_table(pi);
#endif
			/* Last command's pid goes to $! */
			G.last_bg_pid = pi->cmds[pi->num_cmds - 1].pid;
			G.last_bg_pid_exitcode = 0;
			debug_printf_exec(": cmd&: exitcode EXIT_SUCCESS\n");
/* Check pi->pi_inverted? "! sleep 1 & echo $?": bash says 1. dash and ash say 0 */
			rcode = EXIT_SUCCESS;
			goto check_traps;
		} else {
#if ENABLE_HUSH_JOB
			if (G.run_list_level == 1 && G_interactive_fd) {
				/* Waits for completion, then fg's main shell */
				rcode = checkjobs_and_fg_shell(pi);
				debug_printf_exec(": checkjobs_and_fg_shell exitcode %d\n", rcode);
				goto check_traps;
			}
#endif
			/* This one just waits for completion */
			rcode = checkjobs(pi, 0 /*(no pid to wait for)*/);
			debug_printf_exec(": checkjobs exitcode %d\n", rcode);
 check_traps:
			G.last_exitcode = rcode;
			check_and_run_traps();
		}

		/* Handle "set -e" */
		if (rcode != 0 && G.o_opt[OPT_O_ERREXIT]) {
			debug_printf_exec("ERREXIT:1 errexit_depth:%d\n", G.errexit_depth);
			if (G.errexit_depth == 0)
				hush_exit(rcode);
		}
		G.errexit_depth = sv_errexit_depth;

		/* Analyze how result affects subsequent commands */
#if ENABLE_HUSH_IF
		if (rword == RES_IF || rword == RES_ELIF)
			cond_code = rcode;
#endif
 check_jobs_and_continue:
		checkjobs(NULL, 0 /*(no pid to wait for)*/);
 dont_check_jobs_but_continue: ;
#if ENABLE_HUSH_LOOPS
		/* Beware of "while false; true; do ..."! */
		if (pi->next
		 && (pi->next->res_word == RES_DO || pi->next->res_word == RES_DONE)
		 /* check for RES_DONE is needed for "while ...; do \n done" case */
		) {
			if (rword == RES_WHILE) {
				if (rcode) {
					/* "while false; do...done" - exitcode 0 */
					G.last_exitcode = rcode = EXIT_SUCCESS;
					debug_printf_exec(": while expr is false: breaking (exitcode:EXIT_SUCCESS)\n");
					break;
				}
			}
			if (rword == RES_UNTIL) {
				if (!rcode) {
					debug_printf_exec(": until expr is true: breaking\n");
					break;
				}
			}
		}
#endif
	} /* for (pi) */

#if ENABLE_HUSH_JOB
	G.run_list_level--;
#endif
#if ENABLE_HUSH_LOOPS
	if (loop_top)
		G.depth_of_loop--;
	free(for_list);
#endif
#if ENABLE_HUSH_CASE
	free(case_word);
#endif
	debug_leave();
	debug_printf_exec("run_list lvl %d return %d\n", G.run_list_level + 1, rcode);
	return rcode;
}

/* Select which version we will use */
static int run_and_free_list(struct pipe *pi)
{
	int rcode = 0;
	debug_printf_exec("run_and_free_list entered\n");
	if (!G.o_opt[OPT_O_NOEXEC]) {
		debug_printf_exec(": run_list: 1st pipe with %d cmds\n", pi->num_cmds);
		rcode = run_list(pi);
	}
	/* free_pipe_list has the side effect of clearing memory.
	 * In the long run that function can be merged with run_list,
	 * but doing that now would hobble the debugging effort. */
	free_pipe_list(pi);
	debug_printf_exec("run_and_free_list return %d\n", rcode);
	return rcode;
}


static void install_sighandlers(unsigned mask)
{
	sighandler_t old_handler;
	unsigned sig = 0;
	while ((mask >>= 1) != 0) {
		sig++;
		if (!(mask & 1))
			continue;
		old_handler = install_sighandler(sig, pick_sighandler(sig));
		/* POSIX allows shell to re-enable SIGCHLD
		 * even if it was SIG_IGN on entry.
		 * Therefore we skip IGN check for it:
		 */
		if (sig == SIGCHLD)
			continue;
		/* bash re-enables SIGHUP which is SIG_IGNed on entry.
		 * Try: "trap '' HUP; bash; echo RET" and type "kill -HUP $$"
		 */
		//if (sig == SIGHUP) continue; - TODO?
		if (old_handler == SIG_IGN) {
			/* oops... restore back to IGN, and record this fact */
			install_sighandler(sig, old_handler);
#if ENABLE_HUSH_TRAP
			if (!G_traps)
				G_traps = xzalloc(sizeof(G_traps[0]) * NSIG);
			free(G_traps[sig]);
			G_traps[sig] = xzalloc(1); /* == xstrdup(""); */
#endif
		}
	}
}

/* Called a few times only (or even once if "sh -c") */
static void install_special_sighandlers(void)
{
	unsigned mask;

	/* Which signals are shell-special? */
	mask = (1 << SIGQUIT) | (1 << SIGCHLD);
	if (G_interactive_fd) {
		mask |= SPECIAL_INTERACTIVE_SIGS;
		if (G_saved_tty_pgrp) /* we have ctty, job control sigs work */
			mask |= SPECIAL_JOBSTOP_SIGS;
	}
	/* Careful, do not re-install handlers we already installed */
	if (G.special_sig_mask != mask) {
		unsigned diff = mask & ~G.special_sig_mask;
		G.special_sig_mask = mask;
		install_sighandlers(diff);
	}
}

#if ENABLE_HUSH_JOB
/* helper */
/* Set handlers to restore tty pgrp and exit */
static void install_fatal_sighandlers(void)
{
	unsigned mask;

	/* We will restore tty pgrp on these signals */
	mask = 0
		/*+ (1 << SIGILL ) * HUSH_DEBUG*/
		/*+ (1 << SIGFPE ) * HUSH_DEBUG*/
		+ (1 << SIGBUS ) * HUSH_DEBUG
		+ (1 << SIGSEGV) * HUSH_DEBUG
		/*+ (1 << SIGTRAP) * HUSH_DEBUG*/
		+ (1 << SIGABRT)
	/* bash 3.2 seems to handle these just like 'fatal' ones */
		+ (1 << SIGPIPE)
		+ (1 << SIGALRM)
	/* if we are interactive, SIGHUP, SIGTERM and SIGINT are special sigs.
	 * if we aren't interactive... but in this case
	 * we never want to restore pgrp on exit, and this fn is not called
	 */
		/*+ (1 << SIGHUP )*/
		/*+ (1 << SIGTERM)*/
		/*+ (1 << SIGINT )*/
	;
	G_fatal_sig_mask = mask;

	install_sighandlers(mask);
}
#endif

static int set_mode(int state, char mode, const char *o_opt)
{
	int idx;
	switch (mode) {
	case 'n':
		G.o_opt[OPT_O_NOEXEC] = state;
		break;
	case 'x':
		IF_HUSH_MODE_X(G_x_mode = state;)
		break;
	case 'o':
		if (!o_opt) {
			/* "set -+o" without parameter.
			 * in bash, set -o produces this output:
			 *  pipefail        off
			 * and set +o:
			 *  set +o pipefail
			 * We always use the second form.
			 */
			const char *p = o_opt_strings;
			idx = 0;
			while (*p) {
				printf("set %co %s\n", (G.o_opt[idx] ? '-' : '+'), p);
				idx++;
				p += strlen(p) + 1;
			}
			break;
		}
		idx = index_in_strings(o_opt_strings, o_opt);
		if (idx >= 0) {
			G.o_opt[idx] = state;
			break;
		}
	case 'e':
		G.o_opt[OPT_O_ERREXIT] = state;
		break;
	default:
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int hush_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int hush_main(int argc, char **argv)
{
	enum {
		OPT_login = (1 << 0),
		OPT_s     = (1 << 1),
	};
	unsigned flags;
	unsigned builtin_argc;
	char **e;
	struct variable *cur_var;
	struct variable *shell_ver;

	INIT_G();
	if (EXIT_SUCCESS != 0) /* if EXIT_SUCCESS == 0, it is already done */
		G.last_exitcode = EXIT_SUCCESS;

#if ENABLE_HUSH_FAST
	G.count_SIGCHLD++; /* ensure it is != G.handled_SIGCHLD */
#endif
#if !BB_MMU
	G.argv0_for_re_execing = argv[0];
#endif

	/* Deal with HUSH_VERSION */
	debug_printf_env("unsetenv '%s'\n", "HUSH_VERSION");
	unsetenv("HUSH_VERSION"); /* in case it exists in initial env */
	shell_ver = xzalloc(sizeof(*shell_ver));
	shell_ver->flg_export = 1;
	shell_ver->flg_read_only = 1;
	/* Code which handles ${var<op>...} needs writable values for all variables,
	 * therefore we xstrdup: */
	shell_ver->varstr = xstrdup(hush_version_str);

	/* Create shell local variables from the values
	 * currently living in the environment */
	G.top_var = shell_ver;
	cur_var = G.top_var;
	e = environ;
	if (e) while (*e) {
		char *value = strchr(*e, '=');
		if (value) { /* paranoia */
			cur_var->next = xzalloc(sizeof(*cur_var));
			cur_var = cur_var->next;
			cur_var->varstr = *e;
			cur_var->max_len = strlen(*e);
			cur_var->flg_export = 1;
		}
		e++;
	}
	/* (Re)insert HUSH_VERSION into env (AFTER we scanned the env!) */
	debug_printf_env("putenv '%s'\n", shell_ver->varstr);
	putenv(shell_ver->varstr);

	/* Export PWD */
	set_pwd_var(SETFLAG_EXPORT);

#if ENABLE_HUSH_INTERACTIVE && ENABLE_FEATURE_EDITING_FANCY_PROMPT
	/* Set (but not export) PS1/2 unless already set */
	if (!get_local_var_value("PS1"))
		set_local_var_from_halves("PS1", "\\w \\$ ");
	if (!get_local_var_value("PS2"))
		set_local_var_from_halves("PS2", "> ");
#endif

#if BASH_HOSTNAME_VAR
	/* Set (but not export) HOSTNAME unless already set */
	if (!get_local_var_value("HOSTNAME")) {
		struct utsname uts;
		uname(&uts);
		set_local_var_from_halves("HOSTNAME", uts.nodename);
	}
	/* bash also exports SHLVL and _,
	 * and sets (but doesn't export) the following variables:
	 * BASH=/bin/bash
	 * BASH_VERSINFO=([0]="3" [1]="2" [2]="0" [3]="1" [4]="release" [5]="i386-pc-linux-gnu")
	 * BASH_VERSION='3.2.0(1)-release'
	 * HOSTTYPE=i386
	 * MACHTYPE=i386-pc-linux-gnu
	 * OSTYPE=linux-gnu
	 * PPID=<NNNNN> - we also do it elsewhere
	 * EUID=<NNNNN>
	 * UID=<NNNNN>
	 * GROUPS=()
	 * LINES=<NNN>
	 * COLUMNS=<NNN>
	 * BASH_ARGC=()
	 * BASH_ARGV=()
	 * BASH_LINENO=()
	 * BASH_SOURCE=()
	 * DIRSTACK=()
	 * PIPESTATUS=([0]="0")
	 * HISTFILE=/<xxx>/.bash_history
	 * HISTFILESIZE=500
	 * HISTSIZE=500
	 * MAILCHECK=60
	 * PATH=/usr/gnu/bin:/usr/local/bin:/bin:/usr/bin:.
	 * SHELL=/bin/bash
	 * SHELLOPTS=braceexpand:emacs:hashall:histexpand:history:interactive-comments:monitor
	 * TERM=dumb
	 * OPTERR=1
	 * OPTIND=1
	 * IFS=$' \t\n'
	 * PS4='+ '
	 */
#endif

#if ENABLE_HUSH_LINENO_VAR
	if (ENABLE_HUSH_LINENO_VAR) {
		char *p = xasprintf("LINENO=%*s", (int)(sizeof(int)*3), "");
		set_local_var(p, /*flags*/ 0);
		G.lineno_var = p; /* can't assign before set_local_var("LINENO=...") */
	}
#endif

#if ENABLE_FEATURE_EDITING
	G.line_input_state = new_line_input_t(FOR_SHELL);
#endif

	/* Initialize some more globals to non-zero values */
	cmdedit_update_prompt();

	die_func = restore_ttypgrp_and__exit;

	/* Shell is non-interactive at first. We need to call
	 * install_special_sighandlers() if we are going to execute "sh <script>",
	 * "sh -c <cmds>" or login shell's /etc/profile and friends.
	 * If we later decide that we are interactive, we run install_special_sighandlers()
	 * in order to intercept (more) signals.
	 */

	/* Parse options */
	/* http://www.opengroup.org/onlinepubs/9699919799/utilities/sh.html */
	flags = (argv[0] && argv[0][0] == '-') ? OPT_login : 0;
	builtin_argc = 0;
	while (1) {
		int opt = getopt(argc, argv, "+c:exinsl"
#if !BB_MMU
				"<:$:R:V:"
# if ENABLE_HUSH_FUNCTIONS
				"F:"
# endif
#endif
		);
		if (opt <= 0)
			break;
		switch (opt) {
		case 'c':
			/* Possibilities:
			 * sh ... -c 'script'
			 * sh ... -c 'script' ARG0 [ARG1...]
			 * On NOMMU, if builtin_argc != 0,
			 * sh ... -c 'builtin' BARGV... "" ARG0 [ARG1...]
			 * "" needs to be replaced with NULL
			 * and BARGV vector fed to builtin function.
			 * Note: the form without ARG0 never happens:
			 * sh ... -c 'builtin' BARGV... ""
			 */
			if (!G.root_pid) {
				G.root_pid = getpid();
				G.root_ppid = getppid();
			}
			G.global_argv = argv + optind;
			G.global_argc = argc - optind;
			if (builtin_argc) {
				/* -c 'builtin' [BARGV...] "" ARG0 [ARG1...] */
				const struct built_in_command *x;

				install_special_sighandlers();
				x = find_builtin(optarg);
				if (x) { /* paranoia */
					G.global_argc -= builtin_argc; /* skip [BARGV...] "" */
					G.global_argv += builtin_argc;
					G.global_argv[-1] = NULL; /* replace "" */
					fflush_all();
					G.last_exitcode = x->b_function(argv + optind - 1);
				}
				goto final_return;
			}
			if (!G.global_argv[0]) {
				/* -c 'script' (no params): prevent empty $0 */
				G.global_argv--; /* points to argv[i] of 'script' */
				G.global_argv[0] = argv[0];
				G.global_argc++;
			} /* else -c 'script' ARG0 [ARG1...]: $0 is ARG0 */
			install_special_sighandlers();
			parse_and_run_string(optarg);
			goto final_return;
		case 'i':
			/* Well, we cannot just declare interactiveness,
			 * we have to have some stuff (ctty, etc) */
			/* G_interactive_fd++; */
			break;
		case 's':
			flags |= OPT_s;
			break;
		case 'l':
			flags |= OPT_login;
			break;
#if !BB_MMU
		case '<': /* "big heredoc" support */
			full_write1_str(optarg);
			_exit(0);
		case '$': {
			unsigned long long empty_trap_mask;

			G.root_pid = bb_strtou(optarg, &optarg, 16);
			optarg++;
			G.root_ppid = bb_strtou(optarg, &optarg, 16);
			optarg++;
			G.last_bg_pid = bb_strtou(optarg, &optarg, 16);
			optarg++;
			G.last_exitcode = bb_strtou(optarg, &optarg, 16);
			optarg++;
			builtin_argc = bb_strtou(optarg, &optarg, 16);
			optarg++;
			empty_trap_mask = bb_strtoull(optarg, &optarg, 16);
			if (empty_trap_mask != 0) {
				IF_HUSH_TRAP(int sig;)
				install_special_sighandlers();
# if ENABLE_HUSH_TRAP
				G_traps = xzalloc(sizeof(G_traps[0]) * NSIG);
				for (sig = 1; sig < NSIG; sig++) {
					if (empty_trap_mask & (1LL << sig)) {
						G_traps[sig] = xzalloc(1); /* == xstrdup(""); */
						install_sighandler(sig, SIG_IGN);
					}
				}
# endif
			}
# if ENABLE_HUSH_LOOPS
			optarg++;
			G.depth_of_loop = bb_strtou(optarg, &optarg, 16);
# endif
# if ENABLE_HUSH_FUNCTIONS
			/* nommu uses re-exec trick for "... | func | ...",
			 * should allow "return".
			 * This accidentally allows returns in subshells.
			 */
			G_flag_return_in_progress = -1;
# endif
			break;
		}
		case 'R':
		case 'V':
			set_local_var(xstrdup(optarg), opt == 'R' ? SETFLAG_MAKE_RO : 0);
			break;
# if ENABLE_HUSH_FUNCTIONS
		case 'F': {
			struct function *funcp = new_function(optarg);
			/* funcp->name is already set to optarg */
			/* funcp->body is set to NULL. It's a special case. */
			funcp->body_as_string = argv[optind];
			optind++;
			break;
		}
# endif
#endif
		case 'n':
		case 'x':
		case 'e':
			if (set_mode(1, opt, NULL) == 0) /* no error */
				break;
		default:
#ifndef BB_VER
			fprintf(stderr, "Usage: sh [FILE]...\n"
					"   or: sh -c command [args]...\n\n");
			exit(EXIT_FAILURE);
#else
			bb_show_usage();
#endif
		}
	} /* option parsing loop */

	/* Skip options. Try "hush -l": $1 should not be "-l"! */
	G.global_argc = argc - (optind - 1);
	G.global_argv = argv + (optind - 1);
	G.global_argv[0] = argv[0];

	if (!G.root_pid) {
		G.root_pid = getpid();
		G.root_ppid = getppid();
	}

	/* If we are login shell... */
	if (flags & OPT_login) {
		FILE *input;
		debug_printf("sourcing /etc/profile\n");
		input = fopen_for_read("/etc/profile");
		if (input != NULL) {
			remember_FILE(input);
			install_special_sighandlers();
			parse_and_run_file(input);
			fclose_and_forget(input);
		}
		/* bash: after sourcing /etc/profile,
		 * tries to source (in the given order):
		 * ~/.bash_profile, ~/.bash_login, ~/.profile,
		 * stopping on first found. --noprofile turns this off.
		 * bash also sources ~/.bash_logout on exit.
		 * If called as sh, skips .bash_XXX files.
		 */
	}

	/* -s is: hush -s ARGV1 ARGV2 (no SCRIPT) */
	if (!(flags & OPT_s) && G.global_argv[1]) {
		FILE *input;
		/*
		 * "bash <script>" (which is never interactive (unless -i?))
		 * sources $BASH_ENV here (without scanning $PATH).
		 * If called as sh, does the same but with $ENV.
		 * Also NB, per POSIX, $ENV should undergo parameter expansion.
		 */
		G.global_argc--;
		G.global_argv++;
		debug_printf("running script '%s'\n", G.global_argv[0]);
		xfunc_error_retval = 127; /* for "hush /does/not/exist" case */
		input = xfopen_for_read(G.global_argv[0]);
		xfunc_error_retval = 1;
		remember_FILE(input);
		install_special_sighandlers();
		parse_and_run_file(input);
#if ENABLE_FEATURE_CLEAN_UP
		fclose_and_forget(input);
#endif
		goto final_return;
	}

	/* Up to here, shell was non-interactive. Now it may become one.
	 * NB: don't forget to (re)run install_special_sighandlers() as needed.
	 */

	/* A shell is interactive if the '-i' flag was given,
	 * or if all of the following conditions are met:
	 *    no -c command
	 *    no arguments remaining or the -s flag given
	 *    standard input is a terminal
	 *    standard output is a terminal
	 * Refer to Posix.2, the description of the 'sh' utility.
	 */
#if ENABLE_HUSH_JOB
	if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
		G_saved_tty_pgrp = tcgetpgrp(STDIN_FILENO);
		debug_printf("saved_tty_pgrp:%d\n", G_saved_tty_pgrp);
		if (G_saved_tty_pgrp < 0)
			G_saved_tty_pgrp = 0;

		/* try to dup stdin to high fd#, >= 255 */
		G_interactive_fd = dup_CLOEXEC(STDIN_FILENO, 254);
		if (G_interactive_fd < 0) {
			/* try to dup to any fd */
			G_interactive_fd = dup(STDIN_FILENO);
			if (G_interactive_fd < 0) {
				/* give up */
				G_interactive_fd = 0;
				G_saved_tty_pgrp = 0;
			}
		}
// TODO: track & disallow any attempts of user
// to (inadvertently) close/redirect G_interactive_fd
	}
	debug_printf("interactive_fd:%d\n", G_interactive_fd);
	if (G_interactive_fd) {
		close_on_exec_on(G_interactive_fd);

		if (G_saved_tty_pgrp) {
			/* If we were run as 'hush &', sleep until we are
			 * in the foreground (tty pgrp == our pgrp).
			 * If we get started under a job aware app (like bash),
			 * make sure we are now in charge so we don't fight over
			 * who gets the foreground */
			while (1) {
				pid_t shell_pgrp = getpgrp();
				G_saved_tty_pgrp = tcgetpgrp(G_interactive_fd);
				if (G_saved_tty_pgrp == shell_pgrp)
					break;
				/* send TTIN to ourself (should stop us) */
				kill(- shell_pgrp, SIGTTIN);
			}
		}

		/* Install more signal handlers */
		install_special_sighandlers();

		if (G_saved_tty_pgrp) {
			/* Set other signals to restore saved_tty_pgrp */
			install_fatal_sighandlers();
			/* Put ourselves in our own process group
			 * (bash, too, does this only if ctty is available) */
			bb_setpgrp(); /* is the same as setpgid(our_pid, our_pid); */
			/* Grab control of the terminal */
			tcsetpgrp(G_interactive_fd, getpid());
		}
		enable_restore_tty_pgrp_on_exit();

# if ENABLE_HUSH_SAVEHISTORY && MAX_HISTORY > 0
		{
			const char *hp = get_local_var_value("HISTFILE");
			if (!hp) {
				hp = get_local_var_value("HOME");
				if (hp)
					hp = concat_path_file(hp, ".hush_history");
			} else {
				hp = xstrdup(hp);
			}
			if (hp) {
				G.line_input_state->hist_file = hp;
				//set_local_var(xasprintf("HISTFILE=%s", ...));
			}
#  if ENABLE_FEATURE_SH_HISTFILESIZE
			hp = get_local_var_value("HISTFILESIZE");
			G.line_input_state->max_history = size_from_HISTFILESIZE(hp);
#  endif
		}
# endif
	} else {
		install_special_sighandlers();
	}
#elif ENABLE_HUSH_INTERACTIVE
	/* No job control compiled in, only prompt/line editing */
	if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
		G_interactive_fd = dup_CLOEXEC(STDIN_FILENO, 254);
		if (G_interactive_fd < 0) {
			/* try to dup to any fd */
			G_interactive_fd = dup_CLOEXEC(STDIN_FILENO, -1);
			if (G_interactive_fd < 0)
				/* give up */
				G_interactive_fd = 0;
		}
	}
	if (G_interactive_fd) {
		close_on_exec_on(G_interactive_fd);
	}
	install_special_sighandlers();
#else
	/* We have interactiveness code disabled */
	install_special_sighandlers();
#endif
	/* bash:
	 * if interactive but not a login shell, sources ~/.bashrc
	 * (--norc turns this off, --rcfile <file> overrides)
	 */

	if (!ENABLE_FEATURE_SH_EXTRA_QUIET && G_interactive_fd) {
		/* note: ash and hush share this string */
		printf("\n\n%s %s\n"
			IF_HUSH_HELP("Enter 'help' for a list of built-in commands.\n")
			"\n",
			bb_banner,
			"hush - the humble shell"
		);
	}

	parse_and_run_file(stdin);

 final_return:
	hush_exit(G.last_exitcode);
}


/*
 * Built-ins
 */
static int FAST_FUNC builtin_true(char **argv UNUSED_PARAM)
{
	return 0;
}

#if ENABLE_HUSH_TEST || ENABLE_HUSH_ECHO || ENABLE_HUSH_PRINTF || ENABLE_HUSH_KILL
static int run_applet_main(char **argv, int (*applet_main_func)(int argc, char **argv))
{
	int argc = string_array_len(argv);
	return applet_main_func(argc, argv);
}
#endif
#if ENABLE_HUSH_TEST || BASH_TEST2
static int FAST_FUNC builtin_test(char **argv)
{
	return run_applet_main(argv, test_main);
}
#endif
#if ENABLE_HUSH_ECHO
static int FAST_FUNC builtin_echo(char **argv)
{
	return run_applet_main(argv, echo_main);
}
#endif
#if ENABLE_HUSH_PRINTF
static int FAST_FUNC builtin_printf(char **argv)
{
	return run_applet_main(argv, printf_main);
}
#endif

#if ENABLE_HUSH_HELP
static int FAST_FUNC builtin_help(char **argv UNUSED_PARAM)
{
	const struct built_in_command *x;

	printf(
		"Built-in commands:\n"
		"------------------\n");
	for (x = bltins1; x != &bltins1[ARRAY_SIZE(bltins1)]; x++) {
		if (x->b_descr)
			printf("%-10s%s\n", x->b_cmd, x->b_descr);
	}
	return EXIT_SUCCESS;
}
#endif

#if MAX_HISTORY && ENABLE_FEATURE_EDITING
static int FAST_FUNC builtin_history(char **argv UNUSED_PARAM)
{
	show_history(G.line_input_state);
	return EXIT_SUCCESS;
}
#endif

static char **skip_dash_dash(char **argv)
{
	argv++;
	if (argv[0] && argv[0][0] == '-' && argv[0][1] == '-' && argv[0][2] == '\0')
		argv++;
	return argv;
}

static int FAST_FUNC builtin_cd(char **argv)
{
	const char *newdir;

	argv = skip_dash_dash(argv);
	newdir = argv[0];
	if (newdir == NULL) {
		/* bash does nothing (exitcode 0) if HOME is ""; if it's unset,
		 * bash says "bash: cd: HOME not set" and does nothing
		 * (exitcode 1)
		 */
		const char *home = get_local_var_value("HOME");
		newdir = home ? home : "/";
	}
	if (chdir(newdir)) {
		/* Mimic bash message exactly */
		bb_perror_msg("cd: %s", newdir);
		return EXIT_FAILURE;
	}
	/* Read current dir (get_cwd(1) is inside) and set PWD.
	 * Note: do not enforce exporting. If PWD was unset or unexported,
	 * set it again, but do not export. bash does the same.
	 */
	set_pwd_var(/*flag:*/ 0);
	return EXIT_SUCCESS;
}

static int FAST_FUNC builtin_pwd(char **argv UNUSED_PARAM)
{
	puts(get_cwd(0));
	return EXIT_SUCCESS;
}

static int FAST_FUNC builtin_eval(char **argv)
{
	int rcode = EXIT_SUCCESS;

	argv = skip_dash_dash(argv);
	if (argv[0]) {
		char *str = NULL;

		if (argv[1]) {
			/* "The eval utility shall construct a command by
			 * concatenating arguments together, separating
			 * each with a <space> character."
			 */
			char *p;
			unsigned len = 0;
			char **pp = argv;
			do
				len += strlen(*pp) + 1;
			while (*++pp);
			str = p = xmalloc(len);
			pp = argv;
			do {
				p = stpcpy(p, *pp);
				*p++ = ' ';
			} while (*++pp);
			p[-1] = '\0';
		}

		/* bash:
		 * eval "echo Hi; done" ("done" is syntax error):
		 * "echo Hi" will not execute too.
		 */
		parse_and_run_string(str ? str : argv[0]);
		free(str);
		rcode = G.last_exitcode;
	}
	return rcode;
}

static int FAST_FUNC builtin_exec(char **argv)
{
	argv = skip_dash_dash(argv);
	if (argv[0] == NULL)
		return EXIT_SUCCESS; /* bash does this */

	/* Careful: we can end up here after [v]fork. Do not restore
	 * tty pgrp then, only top-level shell process does that */
	if (G_saved_tty_pgrp && getpid() == G.root_pid)
		tcsetpgrp(G_interactive_fd, G_saved_tty_pgrp);

	/* Saved-redirect fds, script fds and G_interactive_fd are still
	 * open here. However, they are all CLOEXEC, and execv below
	 * closes them. Try interactive "exec ls -l /proc/self/fd",
	 * it should show no extra open fds in the "ls" process.
	 * If we'd try to run builtins/NOEXECs, this would need improving.
	 */
	//close_saved_fds_and_FILE_fds();

	/* TODO: if exec fails, bash does NOT exit! We do.
	 * We'll need to undo trap cleanup (it's inside execvp_or_die)
	 * and tcsetpgrp, and this is inherently racy.
	 */
	execvp_or_die(argv);
}

static int FAST_FUNC builtin_exit(char **argv)
{
	debug_printf_exec("%s()\n", __func__);

	/* interactive bash:
	 * # trap "echo EEE" EXIT
	 * # exit
	 * exit
	 * There are stopped jobs.
	 * (if there are _stopped_ jobs, running ones don't count)
	 * # exit
	 * exit
	 * EEE (then bash exits)
	 *
	 * TODO: we can use G.exiting = -1 as indicator "last cmd was exit"
	 */

	/* note: EXIT trap is run by hush_exit */
	argv = skip_dash_dash(argv);
	if (argv[0] == NULL)
		hush_exit(G.last_exitcode);
	/* mimic bash: exit 123abc == exit 255 + error msg */
	xfunc_error_retval = 255;
	/* bash: exit -2 == exit 254, no error msg */
	hush_exit(xatoi(argv[0]) & 0xff);
}

#if ENABLE_HUSH_TYPE
/* http://www.opengroup.org/onlinepubs/9699919799/utilities/type.html */
static int FAST_FUNC builtin_type(char **argv)
{
	int ret = EXIT_SUCCESS;

	while (*++argv) {
		const char *type;
		char *path = NULL;

		if (0) {} /* make conditional compile easier below */
		/*else if (find_alias(*argv))
			type = "an alias";*/
#if ENABLE_HUSH_FUNCTIONS
		else if (find_function(*argv))
			type = "a function";
#endif
		else if (find_builtin(*argv))
			type = "a shell builtin";
		else if ((path = find_in_path(*argv)) != NULL)
			type = path;
		else {
			bb_error_msg("type: %s: not found", *argv);
			ret = EXIT_FAILURE;
			continue;
		}

		printf("%s is %s\n", *argv, type);
		free(path);
	}

	return ret;
}
#endif

#if ENABLE_HUSH_READ
/* Interruptibility of read builtin in bash
 * (tested on bash-4.2.8 by sending signals (not by ^C)):
 *
 * Empty trap makes read ignore corresponding signal, for any signal.
 *
 * SIGINT:
 * - terminates non-interactive shell;
 * - interrupts read in interactive shell;
 * if it has non-empty trap:
 * - executes trap and returns to command prompt in interactive shell;
 * - executes trap and returns to read in non-interactive shell;
 * SIGTERM:
 * - is ignored (does not interrupt) read in interactive shell;
 * - terminates non-interactive shell;
 * if it has non-empty trap:
 * - executes trap and returns to read;
 * SIGHUP:
 * - terminates shell (regardless of interactivity);
 * if it has non-empty trap:
 * - executes trap and returns to read;
 * SIGCHLD from children:
 * - does not interrupt read regardless of interactivity:
 *   try: sleep 1 & read x; echo $x
 */
static int FAST_FUNC builtin_read(char **argv)
{
	const char *r;
	char *opt_n = NULL;
	char *opt_p = NULL;
	char *opt_t = NULL;
	char *opt_u = NULL;
	char *opt_d = NULL; /* optimized out if !BASH */
	const char *ifs;
	int read_flags;

	/* "!": do not abort on errors.
	 * Option string must start with "sr" to match BUILTIN_READ_xxx
	 */
	read_flags = getopt32(argv,
#if BASH_READ_D
		"!srn:p:t:u:d:", &opt_n, &opt_p, &opt_t, &opt_u, &opt_d
#else
		"!srn:p:t:u:", &opt_n, &opt_p, &opt_t, &opt_u
#endif
	);
	if (read_flags == (uint32_t)-1)
		return EXIT_FAILURE;
	argv += optind;
	ifs = get_local_var_value("IFS"); /* can be NULL */

 again:
	r = shell_builtin_read(set_local_var_from_halves,
		argv,
		ifs,
		read_flags,
		opt_n,
		opt_p,
		opt_t,
		opt_u,
		opt_d
	);

	if ((uintptr_t)r == 1 && errno == EINTR) {
		unsigned sig = check_and_run_traps();
		if (sig != SIGINT)
			goto again;
	}

	if ((uintptr_t)r > 1) {
		bb_error_msg("%s", r);
		r = (char*)(uintptr_t)1;
	}

	return (uintptr_t)r;
}
#endif

#if ENABLE_HUSH_UMASK
static int FAST_FUNC builtin_umask(char **argv)
{
	int rc;
	mode_t mask;

	rc = 1;
	mask = umask(0);
	argv = skip_dash_dash(argv);
	if (argv[0]) {
		mode_t old_mask = mask;

		/* numeric umasks are taken as-is */
		/* symbolic umasks are inverted: "umask a=rx" calls umask(222) */
		if (!isdigit(argv[0][0]))
			mask ^= 0777;
		mask = bb_parse_mode(argv[0], mask);
		if (!isdigit(argv[0][0]))
			mask ^= 0777;
		if ((unsigned)mask > 0777) {
			mask = old_mask;
			/* bash messages:
			 * bash: umask: 'q': invalid symbolic mode operator
			 * bash: umask: 999: octal number out of range
			 */
			bb_error_msg("%s: invalid mode '%s'", "umask", argv[0]);
			rc = 0;
		}
	} else {
		/* Mimic bash */
		printf("%04o\n", (unsigned) mask);
		/* fall through and restore mask which we set to 0 */
	}
	umask(mask);

	return !rc; /* rc != 0 - success */
}
#endif

#if ENABLE_HUSH_EXPORT || ENABLE_HUSH_TRAP
static void print_escaped(const char *s)
{
	if (*s == '\'')
		goto squote;
	do {
		const char *p = strchrnul(s, '\'');
		/* print 'xxxx', possibly just '' */
		printf("'%.*s'", (int)(p - s), s);
		if (*p == '\0')
			break;
		s = p;
 squote:
		/* s points to '; print "'''...'''" */
		putchar('"');
		do putchar('\''); while (*++s == '\'');
		putchar('"');
	} while (*s);
}
#endif

#if ENABLE_HUSH_EXPORT || ENABLE_HUSH_LOCAL || ENABLE_HUSH_READONLY
static int helper_export_local(char **argv, unsigned flags)
{
	do {
		char *name = *argv;
		char *name_end = strchrnul(name, '=');

		/* So far we do not check that name is valid (TODO?) */

		if (*name_end == '\0') {
			struct variable *var, **vpp;

			vpp = get_ptr_to_local_var(name, name_end - name);
			var = vpp ? *vpp : NULL;

			if (flags & SETFLAG_UNEXPORT) {
				/* export -n NAME (without =VALUE) */
				if (var) {
					var->flg_export = 0;
					debug_printf_env("%s: unsetenv '%s'\n", __func__, name);
					unsetenv(name);
				} /* else: export -n NOT_EXISTING_VAR: no-op */
				continue;
			}
			if (flags & SETFLAG_EXPORT) {
				/* export NAME (without =VALUE) */
				if (var) {
					var->flg_export = 1;
					debug_printf_env("%s: putenv '%s'\n", __func__, var->varstr);
					putenv(var->varstr);
					continue;
				}
			}
			if (flags & SETFLAG_MAKE_RO) {
				/* readonly NAME (without =VALUE) */
				if (var) {
					var->flg_read_only = 1;
					continue;
				}
			}
# if ENABLE_HUSH_LOCAL
			/* Is this "local" bltin? */
			if (!(flags & (SETFLAG_EXPORT|SETFLAG_UNEXPORT|SETFLAG_MAKE_RO))) {
				unsigned lvl = flags >> SETFLAG_VARLVL_SHIFT;
				if (var && var->var_nest_level == lvl) {
					/* "local x=abc; ...; local x" - ignore second local decl */
					continue;
				}
			}
# endif
			/* Exporting non-existing variable.
			 * bash does not put it in environment,
			 * but remembers that it is exported,
			 * and does put it in env when it is set later.
			 * We just set it to "" and export.
			 */
			/* Or, it's "local NAME" (without =VALUE).
			 * bash sets the value to "".
			 */
			/* Or, it's "readonly NAME" (without =VALUE).
			 * bash remembers NAME and disallows its creation
			 * in the future.
			 */
			name = xasprintf("%s=", name);
		} else {
			/* (Un)exporting/making local NAME=VALUE */
			name = xstrdup(name);
		}
		debug_printf_env("%s: set_local_var('%s')\n", __func__, name);
		if (set_local_var(name, flags))
			return EXIT_FAILURE;
	} while (*++argv);
	return EXIT_SUCCESS;
}
#endif

#if ENABLE_HUSH_EXPORT
static int FAST_FUNC builtin_export(char **argv)
{
	unsigned opt_unexport;

#if ENABLE_HUSH_EXPORT_N
	/* "!": do not abort on errors */
	opt_unexport = getopt32(argv, "!n");
	if (opt_unexport == (uint32_t)-1)
		return EXIT_FAILURE;
	argv += optind;
#else
	opt_unexport = 0;
	argv++;
#endif

	if (argv[0] == NULL) {
		char **e = environ;
		if (e) {
			while (*e) {
#if 0
				puts(*e++);
#else
				/* ash emits: export VAR='VAL'
				 * bash: declare -x VAR="VAL"
				 * we follow ash example */
				const char *s = *e++;
				const char *p = strchr(s, '=');

				if (!p) /* wtf? take next variable */
					continue;
				/* export var= */
				printf("export %.*s", (int)(p - s) + 1, s);
				print_escaped(p + 1);
				putchar('\n');
#endif
			}
			/*fflush_all(); - done after each builtin anyway */
		}
		return EXIT_SUCCESS;
	}

	return helper_export_local(argv, opt_unexport ? SETFLAG_UNEXPORT : SETFLAG_EXPORT);
}
#endif

#if ENABLE_HUSH_LOCAL
static int FAST_FUNC builtin_local(char **argv)
{
	if (G.func_nest_level == 0) {
		bb_error_msg("%s: not in a function", argv[0]);
		return EXIT_FAILURE; /* bash compat */
	}
	argv++;
	/* Since all builtins run in a nested variable level,
	 * need to use level - 1 here. Or else the variable will be removed at once
	 * after builtin returns.
	 */
	return helper_export_local(argv, (G.var_nest_level - 1) << SETFLAG_VARLVL_SHIFT);
}
#endif

#if ENABLE_HUSH_READONLY
static int FAST_FUNC builtin_readonly(char **argv)
{
	argv++;
	if (*argv == NULL) {
		/* bash: readonly [-p]: list all readonly VARs
		 * (-p has no effect in bash)
		 */
		struct variable *e;
		for (e = G.top_var; e; e = e->next) {
			if (e->flg_read_only) {
//TODO: quote value: readonly VAR='VAL'
				printf("readonly %s\n", e->varstr);
			}
		}
		return EXIT_SUCCESS;
	}
	return helper_export_local(argv, SETFLAG_MAKE_RO);
}
#endif

#if ENABLE_HUSH_UNSET
/* http://www.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#unset */
static int FAST_FUNC builtin_unset(char **argv)
{
	int ret;
	unsigned opts;

	/* "!": do not abort on errors */
	/* "+": stop at 1st non-option */
	opts = getopt32(argv, "!+vf");
	if (opts == (unsigned)-1)
		return EXIT_FAILURE;
	if (opts == 3) {
		bb_error_msg("unset: -v and -f are exclusive");
		return EXIT_FAILURE;
	}
	argv += optind;

	ret = EXIT_SUCCESS;
	while (*argv) {
		if (!(opts & 2)) { /* not -f */
			if (unset_local_var(*argv)) {
				/* unset <nonexistent_var> doesn't fail.
				 * Error is when one tries to unset RO var.
				 * Message was printed by unset_local_var. */
				ret = EXIT_FAILURE;
			}
		}
# if ENABLE_HUSH_FUNCTIONS
		else {
			unset_func(*argv);
		}
# endif
		argv++;
	}
	return ret;
}
#endif

#if ENABLE_HUSH_SET
/* http://www.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#set
 * built-in 'set' handler
 * SUSv3 says:
 * set [-abCefhmnuvx] [-o option] [argument...]
 * set [+abCefhmnuvx] [+o option] [argument...]
 * set -- [argument...]
 * set -o
 * set +o
 * Implementations shall support the options in both their hyphen and
 * plus-sign forms. These options can also be specified as options to sh.
 * Examples:
 * Write out all variables and their values: set
 * Set $1, $2, and $3 and set "$#" to 3: set c a b
 * Turn on the -x and -v options: set -xv
 * Unset all positional parameters: set --
 * Set $1 to the value of x, even if it begins with '-' or '+': set -- "$x"
 * Set the positional parameters to the expansion of x, even if x expands
 * with a leading '-' or '+': set -- $x
 *
 * So far, we only support "set -- [argument...]" and some of the short names.
 */
static int FAST_FUNC builtin_set(char **argv)
{
	int n;
	char **pp, **g_argv;
	char *arg = *++argv;

	if (arg == NULL) {
		struct variable *e;
		for (e = G.top_var; e; e = e->next)
			puts(e->varstr);
		return EXIT_SUCCESS;
	}

	do {
		if (strcmp(arg, "--") == 0) {
			++argv;
			goto set_argv;
		}
		if (arg[0] != '+' && arg[0] != '-')
			break;
		for (n = 1; arg[n]; ++n) {
			if (set_mode((arg[0] == '-'), arg[n], argv[1]))
				goto error;
			if (arg[n] == 'o' && argv[1])
				argv++;
		}
	} while ((arg = *++argv) != NULL);
	/* Now argv[0] is 1st argument */

	if (arg == NULL)
		return EXIT_SUCCESS;
 set_argv:

	/* NB: G.global_argv[0] ($0) is never freed/changed */
	g_argv = G.global_argv;
	if (G.global_args_malloced) {
		pp = g_argv;
		while (*++pp)
			free(*pp);
		g_argv[1] = NULL;
	} else {
		G.global_args_malloced = 1;
		pp = xzalloc(sizeof(pp[0]) * 2);
		pp[0] = g_argv[0]; /* retain $0 */
		g_argv = pp;
	}
	/* This realloc's G.global_argv */
	G.global_argv = pp = add_strings_to_strings(g_argv, argv, /*dup:*/ 1);

	G.global_argc = 1 + string_array_len(pp + 1);

	return EXIT_SUCCESS;

	/* Nothing known, so abort */
 error:
	bb_error_msg("%s: %s: invalid option", "set", arg);
	return EXIT_FAILURE;
}
#endif

static int FAST_FUNC builtin_shift(char **argv)
{
	int n = 1;
	argv = skip_dash_dash(argv);
	if (argv[0]) {
		n = bb_strtou(argv[0], NULL, 10);
		if (errno || n < 0) {
			/* shared string with ash.c */
			bb_error_msg("Illegal number: %s", argv[0]);
			/*
			 * ash aborts in this case.
			 * bash prints error message and set $? to 1.
			 * Interestingly, for "shift 99999" bash does not
			 * print error message, but does set $? to 1
			 * (and does no shifting at all).
			 */
		}
	}
	if (n >= 0 && n < G.global_argc) {
		if (G_global_args_malloced) {
			int m = 1;
			while (m <= n)
				free(G.global_argv[m++]);
		}
		G.global_argc -= n;
		memmove(&G.global_argv[1], &G.global_argv[n+1],
				G.global_argc * sizeof(G.global_argv[0]));
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}

#if ENABLE_HUSH_GETOPTS
static int FAST_FUNC builtin_getopts(char **argv)
{
/* http://pubs.opengroup.org/onlinepubs/9699919799/utilities/getopts.html

TODO:
If a required argument is not found, and getopts is not silent,
a question mark (?) is placed in VAR, OPTARG is unset, and a
diagnostic message is printed.  If getopts is silent, then a
colon (:) is placed in VAR and OPTARG is set to the option
character found.

Test that VAR is a valid variable name?

"Whenever the shell is invoked, OPTIND shall be initialized to 1"
*/
	char cbuf[2];
	const char *cp, *optstring, *var;
	int c, n, exitcode, my_opterr;
	unsigned count;

	optstring = *++argv;
	if (!optstring || !(var = *++argv)) {
		bb_error_msg("usage: getopts OPTSTRING VAR [ARGS]");
		return EXIT_FAILURE;
	}

	if (argv[1])
		argv[0] = G.global_argv[0]; /* for error messages in getopt() */
	else
		argv = G.global_argv;
	cbuf[1] = '\0';

	my_opterr = 0;
	if (optstring[0] != ':') {
		cp = get_local_var_value("OPTERR");
		/* 0 if "OPTERR=0", 1 otherwise */
		my_opterr = (!cp || NOT_LONE_CHAR(cp, '0'));
	}

	/* getopts stops on first non-option. Add "+" to force that */
	/*if (optstring[0] != '+')*/ {
		char *s = alloca(strlen(optstring) + 2);
		sprintf(s, "+%s", optstring);
		optstring = s;
	}

	/* Naively, now we should just
	 *	cp = get_local_var_value("OPTIND");
	 *	optind = cp ? atoi(cp) : 0;
	 *	optarg = NULL;
	 *	opterr = my_opterr;
	 *	c = getopt(string_array_len(argv), argv, optstring);
	 * and be done? Not so fast...
	 * Unlike normal getopt() usage in C programs, here
	 * each successive call will (usually) have the same argv[] CONTENTS,
	 * but not the ADDRESSES. Worse yet, it's possible that between
	 * invocations of "getopts", there will be calls to shell builtins
	 * which use getopt() internally. Example:
	 *	while getopts "abc" RES -a -bc -abc de; do
	 *		unset -ff func
	 *	done
	 * This would not work correctly: getopt() call inside "unset"
	 * modifies internal libc state which is tracking position in
	 * multi-option strings ("-abc"). At best, it can skip options
	 * or return the same option infinitely. With glibc implementation
	 * of getopt(), it would use outright invalid pointers and return
	 * garbage even _without_ "unset" mangling internal state.
	 *
	 * We resort to resetting getopt() state and calling it N times,
	 * until we get Nth result (or failure).
	 * (N == G.getopt_count is reset to 0 whenever OPTIND is [un]set).
	 */
	GETOPT_RESET();
	count = 0;
	n = string_array_len(argv);
	do {
		optarg = NULL;
		opterr = (count < G.getopt_count) ? 0 : my_opterr;
		c = getopt(n, argv, optstring);
		if (c < 0)
			break;
		count++;
	} while (count <= G.getopt_count);

	/* Set OPTIND. Prevent resetting of the magic counter! */
	set_local_var_from_halves("OPTIND", utoa(optind));
	G.getopt_count = count; /* "next time, give me N+1'th result" */
	GETOPT_RESET(); /* just in case */

	/* Set OPTARG */
	/* Always set or unset, never left as-is, even on exit/error:
	 * "If no option was found, or if the option that was found
	 * does not have an option-argument, OPTARG shall be unset."
	 */
	cp = optarg;
	if (c == '?') {
		/* If ":optstring" and unknown option is seen,
		 * it is stored to OPTARG.
		 */
		if (optstring[1] == ':') {
			cbuf[0] = optopt;
			cp = cbuf;
		}
	}
	if (cp)
		set_local_var_from_halves("OPTARG", cp);
	else
		unset_local_var("OPTARG");

	/* Convert -1 to "?" */
	exitcode = EXIT_SUCCESS;
	if (c < 0) { /* -1: end of options */
		exitcode = EXIT_FAILURE;
		c = '?';
	}

	/* Set VAR */
	cbuf[0] = c;
	set_local_var_from_halves(var, cbuf);

	return exitcode;
}
#endif

static int FAST_FUNC builtin_source(char **argv)
{
	char *arg_path, *filename;
	FILE *input;
	save_arg_t sv;
	char *args_need_save;
#if ENABLE_HUSH_FUNCTIONS
	smallint sv_flg;
#endif

	argv = skip_dash_dash(argv);
	filename = argv[0];
	if (!filename) {
		/* bash says: "bash: .: filename argument required" */
		return 2; /* bash compat */
	}
	arg_path = NULL;
	if (!strchr(filename, '/')) {
		arg_path = find_in_path(filename);
		if (arg_path)
			filename = arg_path;
		else if (!ENABLE_HUSH_BASH_SOURCE_CURDIR) {
			errno = ENOENT;
			bb_simple_perror_msg(filename);
			return EXIT_FAILURE;
		}
	}
	input = remember_FILE(fopen_or_warn(filename, "r"));
	free(arg_path);
	if (!input) {
		/* bb_perror_msg("%s", *argv); - done by fopen_or_warn */
		/* POSIX: non-interactive shell should abort here,
		 * not merely fail. So far no one complained :)
		 */
		return EXIT_FAILURE;
	}

#if ENABLE_HUSH_FUNCTIONS
	sv_flg = G_flag_return_in_progress;
	/* "we are inside sourced file, ok to use return" */
	G_flag_return_in_progress = -1;
#endif
	args_need_save = argv[1]; /* used as a boolean variable */
	if (args_need_save)
		save_and_replace_G_args(&sv, argv);

	/* "false; . ./empty_line; echo Zero:$?" should print 0 */
	G.last_exitcode = 0;
	parse_and_run_file(input);
	fclose_and_forget(input);

	if (args_need_save) /* can't use argv[1] instead: "shift" can mangle it */
		restore_G_args(&sv, argv);
#if ENABLE_HUSH_FUNCTIONS
	G_flag_return_in_progress = sv_flg;
#endif

	return G.last_exitcode;
}

#if ENABLE_HUSH_TRAP
static int FAST_FUNC builtin_trap(char **argv)
{
	int sig;
	char *new_cmd;

	if (!G_traps)
		G_traps = xzalloc(sizeof(G_traps[0]) * NSIG);

	argv++;
	if (!*argv) {
		int i;
		/* No args: print all trapped */
		for (i = 0; i < NSIG; ++i) {
			if (G_traps[i]) {
				printf("trap -- ");
				print_escaped(G_traps[i]);
				/* note: bash adds "SIG", but only if invoked
				 * as "bash". If called as "sh", or if set -o posix,
				 * then it prints short signal names.
				 * We are printing short names: */
				printf(" %s\n", get_signame(i));
			}
		}
		/*fflush_all(); - done after each builtin anyway */
		return EXIT_SUCCESS;
	}

	new_cmd = NULL;
	/* If first arg is a number: reset all specified signals */
	sig = bb_strtou(*argv, NULL, 10);
	if (errno == 0) {
		int ret;
 process_sig_list:
		ret = EXIT_SUCCESS;
		while (*argv) {
			sighandler_t handler;

			sig = get_signum(*argv++);
			if (sig < 0) {
				ret = EXIT_FAILURE;
				/* Mimic bash message exactly */
				bb_error_msg("trap: %s: invalid signal specification", argv[-1]);
				continue;
			}

			free(G_traps[sig]);
			G_traps[sig] = xstrdup(new_cmd);

			debug_printf("trap: setting SIG%s (%i) to '%s'\n",
				get_signame(sig), sig, G_traps[sig]);

			/* There is no signal for 0 (EXIT) */
			if (sig == 0)
				continue;

			if (new_cmd)
				handler = (new_cmd[0] ? record_pending_signo : SIG_IGN);
			else
				/* We are removing trap handler */
				handler = pick_sighandler(sig);
			install_sighandler(sig, handler);
		}
		return ret;
	}

	if (!argv[1]) { /* no second arg */
		bb_error_msg("trap: invalid arguments");
		return EXIT_FAILURE;
	}

	/* First arg is "-": reset all specified to default */
	/* First arg is "--": skip it, the rest is "handler SIGs..." */
	/* Everything else: set arg as signal handler
	 * (includes "" case, which ignores signal) */
	if (argv[0][0] == '-') {
		if (argv[0][1] == '\0') { /* "-" */
			/* new_cmd remains NULL: "reset these sigs" */
			goto reset_traps;
		}
		if (argv[0][1] == '-' && argv[0][2] == '\0') { /* "--" */
			argv++;
		}
		/* else: "-something", no special meaning */
	}
	new_cmd = *argv;
 reset_traps:
	argv++;
	goto process_sig_list;
}
#endif

#if ENABLE_HUSH_JOB
static struct pipe *parse_jobspec(const char *str)
{
	struct pipe *pi;
	unsigned jobnum;

	if (sscanf(str, "%%%u", &jobnum) != 1) {
		if (str[0] != '%'
		 || (str[1] != '%' && str[1] != '+' && str[1] != '\0')
		) {
			bb_error_msg("bad argument '%s'", str);
			return NULL;
		}
		/* It is "%%", "%+" or "%" - current job */
		jobnum = G.last_jobid;
		if (jobnum == 0) {
			bb_error_msg("no current job");
			return NULL;
		}
	}
	for (pi = G.job_list; pi; pi = pi->next) {
		if (pi->jobid == jobnum) {
			return pi;
		}
	}
	bb_error_msg("%u: no such job", jobnum);
	return NULL;
}

static int FAST_FUNC builtin_jobs(char **argv UNUSED_PARAM)
{
	struct pipe *job;
	const char *status_string;

	checkjobs(NULL, 0 /*(no pid to wait for)*/);
	for (job = G.job_list; job; job = job->next) {
		if (job->alive_cmds == job->stopped_cmds)
			status_string = "Stopped";
		else
			status_string = "Running";

		printf(JOB_STATUS_FORMAT, job->jobid, status_string, job->cmdtext);
	}

	clean_up_last_dead_job();

	return EXIT_SUCCESS;
}

/* built-in 'fg' and 'bg' handler */
static int FAST_FUNC builtin_fg_bg(char **argv)
{
	int i;
	struct pipe *pi;

	if (!G_interactive_fd)
		return EXIT_FAILURE;

	/* If they gave us no args, assume they want the last backgrounded task */
	if (!argv[1]) {
		for (pi = G.job_list; pi; pi = pi->next) {
			if (pi->jobid == G.last_jobid) {
				goto found;
			}
		}
		bb_error_msg("%s: no current job", argv[0]);
		return EXIT_FAILURE;
	}

	pi = parse_jobspec(argv[1]);
	if (!pi)
		return EXIT_FAILURE;
 found:
	/* TODO: bash prints a string representation
	 * of job being foregrounded (like "sleep 1 | cat") */
	if (argv[0][0] == 'f' && G_saved_tty_pgrp) {
		/* Put the job into the foreground.  */
		tcsetpgrp(G_interactive_fd, pi->pgrp);
	}

	/* Restart the processes in the job */
	debug_printf_jobs("reviving %d procs, pgrp %d\n", pi->num_cmds, pi->pgrp);
	for (i = 0; i < pi->num_cmds; i++) {
		debug_printf_jobs("reviving pid %d\n", pi->cmds[i].pid);
	}
	pi->stopped_cmds = 0;

	i = kill(- pi->pgrp, SIGCONT);
	if (i < 0) {
		if (errno == ESRCH) {
			delete_finished_job(pi);
			return EXIT_SUCCESS;
		}
		bb_perror_msg("kill (SIGCONT)");
	}

	if (argv[0][0] == 'f') {
		remove_job_from_table(pi); /* FG job shouldn't be in job table */
		return checkjobs_and_fg_shell(pi);
	}
	return EXIT_SUCCESS;
}
#endif

#if ENABLE_HUSH_KILL
static int FAST_FUNC builtin_kill(char **argv)
{
	int ret = 0;

# if ENABLE_HUSH_JOB
	if (argv[1] && strcmp(argv[1], "-l") != 0) {
		int i = 1;

		do {
			struct pipe *pi;
			char *dst;
			int j, n;

			if (argv[i][0] != '%')
				continue;
			/*
			 * "kill %N" - job kill
			 * Converting to pgrp / pid kill
			 */
			pi = parse_jobspec(argv[i]);
			if (!pi) {
				/* Eat bad jobspec */
				j = i;
				do {
					j++;
					argv[j - 1] = argv[j];
				} while (argv[j]);
				ret = 1;
				i--;
				continue;
			}
			/*
			 * In jobs started under job control, we signal
			 * entire process group by kill -PGRP_ID.
			 * This happens, f.e., in interactive shell.
			 *
			 * Otherwise, we signal each child via
			 * kill PID1 PID2 PID3.
			 * Testcases:
			 * sh -c 'sleep 1|sleep 1 & kill %1'
			 * sh -c 'true|sleep 2 & sleep 1; kill %1'
			 * sh -c 'true|sleep 1 & sleep 2; kill %1'
			 */
			n = G_interactive_fd ? 1 : pi->num_cmds;
			dst = alloca(n * sizeof(int)*4);
			argv[i] = dst;
			if (G_interactive_fd)
				dst += sprintf(dst, " -%u", (int)pi->pgrp);
			else for (j = 0; j < n; j++) {
				struct command *cmd = &pi->cmds[j];
				/* Skip exited members of the job */
				if (cmd->pid == 0)
					continue;
				/*
				 * kill_main has matching code to expect
				 * leading space. Needed to not confuse
				 * negative pids with "kill -SIGNAL_NO" syntax
				 */
				dst += sprintf(dst, " %u", (int)cmd->pid);
			}
			*dst = '\0';
		} while (argv[++i]);
	}
# endif

	if (argv[1] || ret == 0) {
		ret = run_applet_main(argv, kill_main);
	}
	/* else: ret = 1, "kill %bad_jobspec" case */
	return ret;
}
#endif

#if ENABLE_HUSH_WAIT
/* http://www.opengroup.org/onlinepubs/9699919799/utilities/wait.html */
#if !ENABLE_HUSH_JOB
# define wait_for_child_or_signal(pipe,pid) wait_for_child_or_signal(pid)
#endif
static int wait_for_child_or_signal(struct pipe *waitfor_pipe, pid_t waitfor_pid)
{
	int ret = 0;
	for (;;) {
		int sig;
		sigset_t oldset;

		if (!sigisemptyset(&G.pending_set))
			goto check_sig;

		/* waitpid is not interruptible by SA_RESTARTed
		 * signals which we use. Thus, this ugly dance:
		 */

		/* Make sure possible SIGCHLD is stored in kernel's
		 * pending signal mask before we call waitpid.
		 * Or else we may race with SIGCHLD, lose it,
		 * and get stuck in sigsuspend...
		 */
		sigfillset(&oldset); /* block all signals, remember old set */
		sigprocmask(SIG_SETMASK, &oldset, &oldset);

		if (!sigisemptyset(&G.pending_set)) {
			/* Crap! we raced with some signal! */
			goto restore;
		}

		/*errno = 0; - checkjobs does this */
/* Can't pass waitfor_pipe into checkjobs(): it won't be interruptible */
		ret = checkjobs(NULL, waitfor_pid); /* waitpid(WNOHANG) inside */
		debug_printf_exec("checkjobs:%d\n", ret);
#if ENABLE_HUSH_JOB
		if (waitfor_pipe) {
			int rcode = job_exited_or_stopped(waitfor_pipe);
			debug_printf_exec("job_exited_or_stopped:%d\n", rcode);
			if (rcode >= 0) {
				ret = rcode;
				sigprocmask(SIG_SETMASK, &oldset, NULL);
				break;
			}
		}
#endif
		/* if ECHILD, there are no children (ret is -1 or 0) */
		/* if ret == 0, no children changed state */
		/* if ret != 0, it's exitcode+1 of exited waitfor_pid child */
		if (errno == ECHILD || ret) {
			ret--;
			if (ret < 0) /* if ECHILD, may need to fix "ret" */
				ret = 0;
			sigprocmask(SIG_SETMASK, &oldset, NULL);
			break;
		}
		/* Wait for SIGCHLD or any other signal */
		/* It is vitally important for sigsuspend that SIGCHLD has non-DFL handler! */
		/* Note: sigsuspend invokes signal handler */
		sigsuspend(&oldset);
 restore:
		sigprocmask(SIG_SETMASK, &oldset, NULL);
 check_sig:
		/* So, did we get a signal? */
		sig = check_and_run_traps();
		if (sig /*&& sig != SIGCHLD - always true */) {
			/* Do this for any (non-ignored) signal, not only for ^C */
			ret = 128 + sig;
			break;
		}
		/* SIGCHLD, or no signal, or ignored one, such as SIGQUIT. Repeat */
	}
	return ret;
}

static int FAST_FUNC builtin_wait(char **argv)
{
	int ret;
	int status;

	argv = skip_dash_dash(argv);
	if (argv[0] == NULL) {
		/* Don't care about wait results */
		/* Note 1: must wait until there are no more children */
		/* Note 2: must be interruptible */
		/* Examples:
		 * $ sleep 3 & sleep 6 & wait
		 * [1] 30934 sleep 3
		 * [2] 30935 sleep 6
		 * [1] Done                   sleep 3
		 * [2] Done                   sleep 6
		 * $ sleep 3 & sleep 6 & wait
		 * [1] 30936 sleep 3
		 * [2] 30937 sleep 6
		 * [1] Done                   sleep 3
		 * ^C <-- after ~4 sec from keyboard
		 * $
		 */
		return wait_for_child_or_signal(NULL, 0 /*(no job and no pid to wait for)*/);
	}

	do {
		pid_t pid = bb_strtou(*argv, NULL, 10);
		if (errno || pid <= 0) {
#if ENABLE_HUSH_JOB
			if (argv[0][0] == '%') {
				struct pipe *wait_pipe;
				ret = 127; /* bash compat for bad jobspecs */
				wait_pipe = parse_jobspec(*argv);
				if (wait_pipe) {
					ret = job_exited_or_stopped(wait_pipe);
					if (ret < 0) {
						ret = wait_for_child_or_signal(wait_pipe, 0);
					} else {
						/* waiting on "last dead job" removes it */
						clean_up_last_dead_job();
					}
				}
				/* else: parse_jobspec() already emitted error msg */
				continue;
			}
#endif
			/* mimic bash message */
			bb_error_msg("wait: '%s': not a pid or valid job spec", *argv);
			ret = EXIT_FAILURE;
			continue; /* bash checks all argv[] */
		}

		/* Do we have such child? */
		ret = waitpid(pid, &status, WNOHANG);
		if (ret < 0) {
			/* No */
			ret = 127;
			if (errno == ECHILD) {
				if (pid == G.last_bg_pid) {
					/* "wait $!" but last bg task has already exited. Try:
					 * (sleep 1; exit 3) & sleep 2; echo $?; wait $!; echo $?
					 * In bash it prints exitcode 0, then 3.
					 * In dash, it is 127.
					 */
					ret = G.last_bg_pid_exitcode;
				} else {
					/* Example: "wait 1". mimic bash message */
					bb_error_msg("wait: pid %d is not a child of this shell", (int)pid);
				}
			} else {
				/* ??? */
				bb_perror_msg("wait %s", *argv);
			}
			continue; /* bash checks all argv[] */
		}
		if (ret == 0) {
			/* Yes, and it still runs */
			ret = wait_for_child_or_signal(NULL, pid);
		} else {
			/* Yes, and it just exited */
			process_wait_result(NULL, pid, status);
			ret = WEXITSTATUS(status);
			if (WIFSIGNALED(status))
				ret = 128 + WTERMSIG(status);
		}
	} while (*++argv);

	return ret;
}
#endif

#if ENABLE_HUSH_LOOPS || ENABLE_HUSH_FUNCTIONS
static unsigned parse_numeric_argv1(char **argv, unsigned def, unsigned def_min)
{
	if (argv[1]) {
		def = bb_strtou(argv[1], NULL, 10);
		if (errno || def < def_min || argv[2]) {
			bb_error_msg("%s: bad arguments", argv[0]);
			def = UINT_MAX;
		}
	}
	return def;
}
#endif

#if ENABLE_HUSH_LOOPS
static int FAST_FUNC builtin_break(char **argv)
{
	unsigned depth;
	if (G.depth_of_loop == 0) {
		bb_error_msg("%s: only meaningful in a loop", argv[0]);
		/* if we came from builtin_continue(), need to undo "= 1" */
		G.flag_break_continue = 0;
		return EXIT_SUCCESS; /* bash compat */
	}
	G.flag_break_continue++; /* BC_BREAK = 1, or BC_CONTINUE = 2 */

	G.depth_break_continue = depth = parse_numeric_argv1(argv, 1, 1);
	if (depth == UINT_MAX)
		G.flag_break_continue = BC_BREAK;
	if (G.depth_of_loop < depth)
		G.depth_break_continue = G.depth_of_loop;

	return EXIT_SUCCESS;
}

static int FAST_FUNC builtin_continue(char **argv)
{
	G.flag_break_continue = 1; /* BC_CONTINUE = 2 = 1+1 */
	return builtin_break(argv);
}
#endif

#if ENABLE_HUSH_FUNCTIONS
static int FAST_FUNC builtin_return(char **argv)
{
	int rc;

	if (G_flag_return_in_progress != -1) {
		bb_error_msg("%s: not in a function or sourced script", argv[0]);
		return EXIT_FAILURE; /* bash compat */
	}

	G_flag_return_in_progress = 1;

	/* bash:
	 * out of range: wraps around at 256, does not error out
	 * non-numeric param:
	 * f() { false; return qwe; }; f; echo $?
	 * bash: return: qwe: numeric argument required  <== we do this
	 * 255  <== we also do this
	 */
	rc = parse_numeric_argv1(argv, G.last_exitcode, 0);
	return rc;
}
#endif

#if ENABLE_HUSH_TIMES
static int FAST_FUNC builtin_times(char **argv UNUSED_PARAM)
{
	static const uint8_t times_tbl[] ALIGN1 = {
		' ',  offsetof(struct tms, tms_utime),
		'\n', offsetof(struct tms, tms_stime),
		' ',  offsetof(struct tms, tms_cutime),
		'\n', offsetof(struct tms, tms_cstime),
		0
	};
	const uint8_t *p;
	unsigned clk_tck;
	struct tms buf;

	clk_tck = bb_clk_tck();

	times(&buf);
	p = times_tbl;
	do {
		unsigned sec, frac;
		unsigned long t;
		t = *(clock_t *)(((char *) &buf) + p[1]);
		sec = t / clk_tck;
		frac = t % clk_tck;
		printf("%um%u.%03us%c",
			sec / 60, sec % 60,
			(frac * 1000) / clk_tck,
			p[0]);
		p += 2;
	} while (*p);

	return EXIT_SUCCESS;
}
#endif

#if ENABLE_HUSH_MEMLEAK
static int FAST_FUNC builtin_memleak(char **argv UNUSED_PARAM)
{
	void *p;
	unsigned long l;

# ifdef M_TRIM_THRESHOLD
	/* Optional. Reduces probability of false positives */
	malloc_trim(0);
# endif
	/* Crude attempt to find where "free memory" starts,
	 * sans fragmentation. */
	p = malloc(240);
	l = (unsigned long)p;
	free(p);
	p = malloc(3400);
	if (l < (unsigned long)p) l = (unsigned long)p;
	free(p);


# if 0  /* debug */
	{
		struct mallinfo mi = mallinfo();
		printf("top alloc:0x%lx malloced:%d+%d=%d\n", l,
			mi.arena, mi.hblkhd, mi.arena + mi.hblkhd);
	}
# endif

	if (!G.memleak_value)
		G.memleak_value = l;

	l -= G.memleak_value;
	if ((long)l < 0)
		l = 0;
	l /= 1024;
	if (l > 127)
		l = 127;

	/* Exitcode is "how many kilobytes we leaked since 1st call" */
	return l;
}
#endif
