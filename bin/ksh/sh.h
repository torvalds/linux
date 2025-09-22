/*	$OpenBSD: sh.h,v 1.77 2023/06/21 22:22:08 millert Exp $	*/

/*
 * Public Domain Bourne/Korn shell
 */

/* $From: sh.h,v 1.2 1994/05/19 18:32:40 michael Exp michael $ */

#include "config.h"	/* system and option configuration info */

/* Start of common headers */

#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <signal.h>
#include <stdbool.h>

/* end of common headers */

#define	NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define	BIT(i)	(1<<(i))	/* define bit in flag */

#define	NUFILE	32		/* Number of user-accessible files */
#define	FDBASE	10		/* First file usable by Shell */

#define BITS(t)	(CHAR_BIT * sizeof(t))

/* Make MAGIC a char that might be printed to make bugs more obvious, but
 * not a char that is used often.  Also, can't use the high bit as it causes
 * portability problems (calling strchr(x, 0x80|'x') is error prone).
 */
#define	MAGIC		(7)	/* prefix for *?[!{,} during expand */
#define ISMAGIC(c)	((unsigned char)(c) == MAGIC)

#define	LINE	4096		/* input line size */

extern	const char *kshname;	/* $0 */
extern	pid_t	kshpid;		/* $$, shell pid */
extern	pid_t	procpid;	/* pid of executing process */
extern	uid_t	ksheuid;	/* effective uid of shell */
extern	int	exstat;		/* exit status */
extern	int	subst_exstat;	/* exit status of last $(..)/`..` */
extern	const char *safe_prompt; /* safe prompt if PS1 substitution fails */
extern	char	username[];	/* username for \u prompt expansion */
extern	int	disable_subst;	/* disable substitution during evaluation */

/*
 * Area-based allocation built on malloc/free
 */
typedef struct Area {
	struct link *freelist;	/* free list */
} Area;

extern	Area	aperm;		/* permanent object space */
#define	APERM	&aperm
#define	ATEMP	&genv->area

#ifdef KSH_DEBUG
# define kshdebug_init()	kshdebug_init_()
# define kshdebug_printf(a)	kshdebug_printf_ a
# define kshdebug_dump(a)	kshdebug_dump_ a
#else /* KSH_DEBUG */
# define kshdebug_init()
# define kshdebug_printf(a)
# define kshdebug_dump(a)
#endif /* KSH_DEBUG */

/*
 * parsing & execution environment
 */
struct env {
	short	type;			/* environment type - see below */
	short	flags;			/* EF_* */
	Area	area;			/* temporary allocation area */
	struct	block *loc;		/* local variables and functions */
	short  *savefd;			/* original redirected fd's */
	struct	env *oenv;		/* link to previous environment */
	sigjmp_buf jbuf;		/* long jump back to env creator */
	struct temp *temps;		/* temp files */
};
extern	struct env	*genv;

/* struct env.type values */
#define	E_NONE	0		/* dummy environment */
#define	E_PARSE	1		/* parsing command # */
#define	E_FUNC	2		/* executing function # */
#define	E_INCL	3		/* including a file via . # */
#define	E_EXEC	4		/* executing command tree */
#define	E_LOOP	5		/* executing for/while # */
#define	E_ERRH	6		/* general error handler # */
/* # indicates env has valid jbuf (see unwind()) */

/* struct env.flag values */
#define EF_FUNC_PARSE	BIT(0)	/* function being parsed */
#define EF_BRKCONT_PASS	BIT(1)	/* set if E_LOOP must pass break/continue on */
#define EF_FAKE_SIGDIE	BIT(2)	/* hack to get info from unwind to quitenv */

/* Do breaks/continues stop at env type e? */
#define STOP_BRKCONT(t)	((t) == E_NONE || (t) == E_PARSE \
			 || (t) == E_FUNC || (t) == E_INCL)
/* Do returns stop at env type e? */
#define STOP_RETURN(t)	((t) == E_FUNC || (t) == E_INCL)

/* values for siglongjmp(e->jbuf, 0) */
#define LRETURN	1		/* return statement */
#define	LEXIT	2		/* exit statement */
#define LERROR	3		/* errorf() called */
#define LLEAVE	4		/* untrappable exit/error */
#define LINTR	5		/* ^C noticed */
#define	LBREAK	6		/* break statement */
#define	LCONTIN	7		/* continue statement */
#define LSHELL	8		/* return to interactive shell() */
#define LAEXPR	9		/* error in arithmetic expression */

/* option processing */
#define OF_CMDLINE	0x01	/* command line */
#define OF_SET		0x02	/* set builtin */
#define OF_SPECIAL	0x04	/* a special variable changing */
#define OF_INTERNAL	0x08	/* set internally by shell */
#define OF_ANY		(OF_CMDLINE | OF_SET | OF_SPECIAL | OF_INTERNAL)

struct option {
    const char	*name;	/* long name of option */
    char	c;	/* character flag (if any) */
    short	flags;	/* OF_* */
};
extern const struct option sh_options[];

/*
 * flags (the order of these enums MUST match the order in misc.c(options[]))
 */
enum sh_flag {
	FEXPORT = 0,	/* -a: export all */
	FBRACEEXPAND,	/* enable {} globbing */
	FBGNICE,	/* bgnice */
	FCOMMAND,	/* -c: (invocation) execute specified command */
	FCSHHISTORY,	/* csh-style history enabled */
#ifdef EMACS
	FEMACS,		/* emacs command editing */
#endif
	FERREXIT,	/* -e: quit on error */
#ifdef EMACS
	FGMACS,		/* gmacs command editing */
#endif
	FIGNOREEOF,	/* eof does not exit */
	FTALKING,	/* -i: interactive */
	FKEYWORD,	/* -k: name=value anywhere */
	FLOGIN,		/* -l: a login shell */
	FMARKDIRS,	/* mark dirs with / in file name completion */
	FMONITOR,	/* -m: job control monitoring */
	FNOCLOBBER,	/* -C: don't overwrite existing files */
	FNOEXEC,	/* -n: don't execute any commands */
	FNOGLOB,	/* -f: don't do file globbing */
	FNOHUP,		/* -H: don't kill running jobs when login shell exits */
	FNOLOG,		/* don't save functions in history (ignored) */
	FNOTIFY,	/* -b: asynchronous job completion notification */
	FNOUNSET,	/* -u: using an unset var is an error */
	FPHYSICAL,	/* -o physical: don't do logical cd's/pwd's */
	FPIPEFAIL,	/* -o pipefail: all commands in pipeline can affect $? */
	FPOSIX,		/* -o posix: be posixly correct */
	FPRIVILEGED,	/* -p: use suid_profile */
	FRESTRICTED,	/* -r: restricted shell */
	FSH,		/* -o sh: favor sh behaviour */
	FSTDIN,		/* -s: (invocation) parse stdin */
	FTRACKALL,	/* -h: create tracked aliases for all commands */
	FVERBOSE,	/* -v: echo input */
#ifdef VI
	FVI,		/* vi command editing */
	FVIRAW,		/* always read in raw mode (ignored) */
	FVISHOW8,	/* display chars with 8th bit set as is (versus M-) */
	FVITABCOMPLETE,	/* enable tab as file name completion char */
	FVIESCCOMPLETE,	/* enable ESC as file name completion in command mode */
#endif
	FXTRACE,	/* -x: execution trace */
	FTALKING_I,	/* (internal): initial shell was interactive */
	FNFLAGS /* (place holder: how many flags are there) */
};

#define Flag(f)	(shell_flags[(int) (f)])

extern	char shell_flags[FNFLAGS];

extern	char	null[];	/* null value for variable */

enum temp_type {
	TT_HEREDOC_EXP,	/* expanded heredoc */
	TT_HIST_EDIT	/* temp file used for history editing (fc -e) */
};
typedef enum temp_type Temp_type;
/* temp/heredoc files.  The file is removed when the struct is freed. */
struct temp {
	struct temp	*next;
	struct shf	*shf;
	int		pid;		/* pid of process parsed here-doc */
	Temp_type	type;
	char		*name;
};

/*
 * stdio and our IO routines
 */

#define shl_spare	(&shf_iob[0])	/* for c_read()/c_print() */
#define shl_stdout	(&shf_iob[1])
#define shl_out		(&shf_iob[2])
extern int shl_stdout_ok;

/*
 * trap handlers
 */
typedef struct trap {
	int	signal;		/* signal number */
	const char *name;	/* short name */
	const char *mess;	/* descriptive name */
	char   *trap;		/* trap command */
	volatile sig_atomic_t set; /* trap pending */
	int	flags;		/* TF_* */
	sig_t cursig;		/* current handler (valid if TF_ORIG_* set) */
	sig_t shtrap;		/* shell signal handler */
} Trap;

/* values for Trap.flags */
#define TF_SHELL_USES	BIT(0)	/* shell uses signal, user can't change */
#define TF_USER_SET	BIT(1)	/* user has (tried to) set trap */
#define TF_ORIG_IGN	BIT(2)	/* original action was SIG_IGN */
#define TF_ORIG_DFL	BIT(3)	/* original action was SIG_DFL */
#define TF_EXEC_IGN	BIT(4)	/* restore SIG_IGN just before exec */
#define TF_EXEC_DFL	BIT(5)	/* restore SIG_DFL just before exec */
#define TF_DFL_INTR	BIT(6)	/* when received, default action is LINTR */
#define TF_TTY_INTR	BIT(7)	/* tty generated signal (see j_waitj) */
#define TF_CHANGED	BIT(8)	/* used by runtrap() to detect trap changes */
#define TF_FATAL	BIT(9)	/* causes termination if not trapped */

/* values for setsig()/setexecsig() flags argument */
#define SS_RESTORE_MASK	0x3	/* how to restore a signal before an exec() */
#define SS_RESTORE_CURR	0	/* leave current handler in place */
#define SS_RESTORE_ORIG	1	/* restore original handler */
#define SS_RESTORE_DFL	2	/* restore to SIG_DFL */
#define SS_RESTORE_IGN	3	/* restore to SIG_IGN */
#define SS_FORCE	BIT(3)	/* set signal even if original signal ignored */
#define SS_USER		BIT(4)	/* user is doing the set (ie, trap command) */
#define SS_SHTRAP	BIT(5)	/* trap for internal use (CHLD,ALRM,WINCH) */

#define SIGEXIT_	0	/* for trap EXIT */
#define SIGERR_		NSIG	/* for trap ERR */

extern	volatile sig_atomic_t trap;	/* traps pending? */
extern	volatile sig_atomic_t intrsig;	/* pending trap interrupts command */
extern	volatile sig_atomic_t fatal_trap;	/* received a fatal signal */
extern	volatile sig_atomic_t got_sigwinch;
extern	Trap	sigtraps[NSIG+1];

/*
 * TMOUT support
 */
/* values for ksh_tmout_state */
enum tmout_enum {
	TMOUT_EXECUTING	= 0,	/* executing commands */
	TMOUT_READING,		/* waiting for input */
	TMOUT_LEAVING		/* have timed out */
};
extern unsigned int ksh_tmout;
extern enum tmout_enum ksh_tmout_state;

/* For "You have stopped jobs" message */
extern int really_exit;

/*
 * fast character classes
 */
#define	C_ALPHA	 BIT(0)		/* a-z_A-Z */
/* was	C_DIGIT */
#define	C_LEX1	 BIT(2)		/* \0 \t\n|&;<>() */
#define	C_VAR1	 BIT(3)		/* *@#!$-? */
#define	C_IFSWS	 BIT(4)		/* \t \n (IFS white space) */
#define	C_SUBOP1 BIT(5)		/* "=-+?" */
#define	C_SUBOP2 BIT(6)		/* "#%" */
#define	C_IFS	 BIT(7)		/* $IFS */
#define	C_QUOTE	 BIT(8)		/*  \n\t"#$&'()*;<>?[\`| (needing quoting) */

extern	short ctypes [];

#define	ctype(c, t)	!!(ctypes[(unsigned char)(c)]&(t))
#define	letter(c)	ctype(c, C_ALPHA)
#define	digit(c)	isdigit((unsigned char)(c))
#define	letnum(c)	(ctype(c, C_ALPHA) || isdigit((unsigned char)(c)))

extern int ifs0;	/* for "$*" */

/* Argument parsing for built-in commands and getopts command */

/* Values for Getopt.flags */
#define GF_ERROR	BIT(0)	/* call errorf() if there is an error */
#define GF_PLUSOPT	BIT(1)	/* allow +c as an option */
#define GF_NONAME	BIT(2)	/* don't print argv[0] in errors */

/* Values for Getopt.info */
#define GI_MINUS	BIT(0)	/* an option started with -... */
#define GI_PLUS		BIT(1)	/* an option started with +... */
#define GI_MINUSMINUS	BIT(2)	/* arguments were ended with -- */

typedef struct {
	int		optind;
	int		uoptind;/* what user sees in $OPTIND */
	char		*optarg;
	int		flags;	/* see GF_* */
	int		info;	/* see GI_* */
	unsigned int	p;	/* 0 or index into argv[optind - 1] */
	char		buf[2];	/* for bad option OPTARG value */
} Getopt;

extern Getopt builtin_opt;	/* for shell builtin commands */
extern Getopt user_opt;		/* parsing state for getopts builtin command */

/* This for co-processes */

typedef int Coproc_id; /* something that won't (realistically) wrap */
struct coproc {
	int	read;		/* pipe from co-process's stdout */
	int	readw;		/* other side of read (saved temporarily) */
	int	write;		/* pipe to co-process's stdin */
	Coproc_id id;		/* id of current output pipe */
	int	njobs;		/* number of live jobs using output pipe */
	void	*job;		/* 0 or job of co-process using input pipe */
};
extern struct coproc coproc;

/* Used in jobs.c and by coprocess stuff in exec.c */
extern sigset_t		sm_default, sm_sigchld;

extern const char ksh_version[];

/* name of called builtin function (used by error functions) */
extern char	*builtin_argv0;
extern int	builtin_flag;	/* flags of called builtin (SPEC_BI, etc.) */

/* current working directory, and size of memory allocated for same */
extern char	*current_wd;
extern int	current_wd_size;

/* Minimum required space to work with on a line - if the prompt leaves less
 * space than this on a line, the prompt is truncated.
 */
#define MIN_EDIT_SPACE	7
/* Minimum allowed value for x_cols: 2 for prompt, 3 for " < " at end of line
 */
#define MIN_COLS	(2 + MIN_EDIT_SPACE + 3)
extern	int	x_cols;	/* tty columns */

/* These to avoid bracket matching problems */
#define OPAREN	'('
#define CPAREN	')'
#define OBRACK	'['
#define CBRACK	']'
#define OBRACE	'{'
#define CBRACE	'}'

/* Determine the location of the system (common) profile */
#define KSH_SYSTEM_PROFILE "/etc/profile"

/* Used by v_evaluate() and setstr() to control action when error occurs */
#define KSH_UNWIND_ERROR	0x0	/* unwind the stack (longjmp) */
#define KSH_RETURN_ERROR	0x1	/* return 1/0 for success/failure */
#define KSH_IGNORE_RDONLY	0x4	/* ignore the read-only flag */

#include "shf.h"
#include "table.h"
#include "tree.h"
#include "expand.h"
#include "lex.h"

/* alloc.c */
Area *	ainit(Area *);
void	afreeall(Area *);
void *	alloc(size_t, Area *);
void *	areallocarray(void *, size_t, size_t, Area *);
void *	aresize(void *, size_t, Area *);
void	afree(void *, Area *);
/* c_ksh.c */
int	c_cd(char **);
int	c_pwd(char **);
int	c_print(char **);
int	c_whence(char **);
int	c_command(char **);
int	c_type(char **);
int	c_typeset(char **);
int	c_alias(char **);
int	c_unalias(char **);
int	c_let(char **);
int	c_jobs(char **);
int	c_fgbg(char **);
int	c_kill(char **);
void	getopts_reset(int);
int	c_getopts(char **);
int	c_bind(char **);
/* c_sh.c */
int	c_label(char **);
int	c_shift(char **);
int	c_umask(char **);
int	c_dot(char **);
int	c_wait(char **);
int	c_read(char **);
int	c_eval(char **);
int	c_trap(char **);
int	c_brkcont(char **);
int	c_exitreturn(char **);
int	c_set(char **);
int	c_unset(char **);
int	c_ulimit(char **);
int	c_times(char **);
int	timex(struct op *, int, volatile int *);
void	timex_hook(struct op *, char ** volatile *);
int	c_exec(char **);
int	c_builtin(char **);
/* c_test.c */
int	c_test(char **);
/* edit.c: most prototypes in edit.h */
void	x_init(void);
int	x_read(char *, size_t);
void	set_editmode(const char *);
/* emacs.c: most prototypes in edit.h */
int	x_bind(const char *, const char *, int, int);
/* eval.c */
char *	substitute(const char *, int);
char **	eval(char **, int);
char *	evalstr(char *cp, int);
char *	evalonestr(char *cp, int);
char	*debunk(char *, const char *, size_t);
void	expand(char *, XPtrV *, int);
int	glob_str(char *, XPtrV *, int);
/* exec.c */
int	execute(struct op * volatile, volatile int, volatile int *);
int	shcomexec(char **);
struct tbl * findfunc(const char *, unsigned int, int);
int	define(const char *, struct op *);
void	builtin(const char *, int (*)(char **));
struct tbl *	findcom(const char *, int);
void	flushcom(int);
char *	search(const char *, const char *, int, int *);
int	search_access(const char *, int, int *);
int	pr_menu(char *const *);
/* expr.c */
int	evaluate(const char *, int64_t *, int, bool);
int	v_evaluate(struct tbl *, const char *, volatile int, bool);
/* history.c */
void	init_histvec(void);
void	hist_init(Source *);
void	hist_finish(void);
void	histsave(int, const char *, int);
int	c_fc(char **);
void	c_fc_reset(void);
void	sethistcontrol(const char *);
void	sethistsize(int);
void	sethistfile(const char *);
char **	histpos(void);
int	histnum(int);
int	findhist(int, int, const char *, int);
int	findhistrel(const char *);
char  **hist_get_newest(int);

/* io.c */
void	errorf(const char *, ...)
	    __attribute__((__noreturn__, __format__ (printf, 1, 2)));
void	warningf(bool, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
void	bi_errorf(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	internal_errorf(const char *, ...)
	    __attribute__((__noreturn__, __format__ (printf, 1, 2)));
void	internal_warningf(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	error_prefix(int);
void	shellf(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	shprintf(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
#ifdef KSH_DEBUG
void	kshdebug_init_(void);
void	kshdebug_printf_(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	kshdebug_dump_(const char *, const void *, int);
#endif /* KSH_DEBUG */
int	can_seek(int);
void	initio(void);
int	ksh_dup2(int, int, int);
int	savefd(int);
void	restfd(int, int);
void	openpipe(int *);
void	closepipe(int *);
int	check_fd(char *, int, const char **);
void	coproc_init(void);
void	coproc_read_close(int);
void	coproc_readw_close(int);
void	coproc_write_close(int);
int	coproc_getfd(int, const char **);
void	coproc_cleanup(int);
struct temp *maketemp(Area *, Temp_type, struct temp **);
/* jobs.c */
void	j_init(int);
void	j_suspend(void);
void	j_exit(void);
void	j_change(void);
int	exchild(struct op *, int, volatile int *, int);
void	startlast(void);
int	waitlast(void);
int	waitfor(const char *, int *);
int	j_kill(const char *, int);
int	j_resume(const char *, int);
int	j_jobs(const char *, int, int);
int	j_njobs(void);
void	j_notify(void);
pid_t	j_async(void);
int	j_stopped_running(void);
/* mail.c */
void	mcheck(void);
void	mcset(int64_t);
void	mbset(char *);
void	mpset(char *);
/* main.c */
int	include(const char *, int, char **, int);
int	command(const char *, int);
int	shell(Source *volatile, int volatile);
void	unwind(int) __attribute__((__noreturn__));
void	newenv(int);
void	quitenv(struct shf *);
void	cleanup_parents_env(void);
void	cleanup_proc_env(void);
/* misc.c */
void	setctypes(const char *, int);
void	initctypes(void);
char *	u64ton(uint64_t, int);
char *	str_save(const char *, Area *);
char *	str_nsave(const char *, int, Area *);
int	option(const char *);
char *	getoptions(void);
void	change_flag(enum sh_flag, int, int);
int	parse_args(char **, int, int *);
int	getn(const char *, int *);
int	bi_getn(const char *, int *);
int	gmatch(const char *, const char *, int);
int	has_globbing(const char *, const char *);
const unsigned char *pat_scan(const unsigned char *, const unsigned char *,
    int);
void	qsortp(void **, size_t, int (*)(const void *, const void *));
int	xstrcmp(const void *, const void *);
void	ksh_getopt_reset(Getopt *, int);
int	ksh_getopt(char **, Getopt *, const char *);
void	print_value_quoted(const char *);
void	print_columns(struct shf *, int, char *(*)(void *, int, char *, int),
    void *, int, int prefcol);
int	strip_nuls(char *, int);
int	blocking_read(int, char *, int);
int	reset_nonblock(int);
char	*ksh_get_wd(char *, int);
/* path.c */
int	make_path(const char *, const char *, char **, XString *, int *);
void	simplify_path(char *);
char	*get_phys_path(const char *);
void	set_current_wd(char *);
/* syn.c */
void	initkeywords(void);
struct op * compile(Source *);
/* trap.c */
void	inittraps(void);
void	alarm_init(void);
Trap *	gettrap(const char *, int);
void	trapsig(int);
void	intrcheck(void);
int	fatal_trap_check(void);
int	trap_pending(void);
void	runtraps(int intr);
void	runtrap(Trap *);
void	cleartraps(void);
void	restoresigs(void);
void	settrap(Trap *, char *);
int	block_pipe(void);
void	restore_pipe(int);
int	setsig(Trap *, sig_t, int);
void	setexecsig(Trap *, int);
/* var.c */
void	newblock(void);
void	popblock(void);
void	initvar(void);
struct tbl *	global(const char *);
struct tbl *	local(const char *, bool);
char *	str_val(struct tbl *);
int64_t	intval(struct tbl *);
int	setstr(struct tbl *, const char *, int);
struct tbl *setint_v(struct tbl *, struct tbl *, bool);
void	setint(struct tbl *, int64_t);
int	getint(struct tbl *, int64_t *, bool);
struct tbl *typeset(const char *, int, int, int, int);
void	unset(struct tbl *, int);
char  * skip_varname(const char *, int);
char	*skip_wdvarname(const char *, int);
int	is_wdvarname(const char *, int);
int	is_wdvarassign(const char *);
char **	makenv(void);
void	change_random(void);
int	array_ref_len(const char *);
char *	arrayname(const char *);
void    set_array(const char *, int, char **);
/* vi.c: see edit.h */
