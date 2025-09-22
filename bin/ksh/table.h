/*	$OpenBSD: table.h,v 1.15 2018/06/18 17:03:58 millert Exp $	*/

/* $From: table.h,v 1.3 1994/05/31 13:34:34 michael Exp $ */

/*
 * generic hashed associative table for commands and variables.
 */

struct table {
	Area   *areap;		/* area to allocate entries */
	int	size, nfree;	/* hash size (always 2^^n), free entries */
	struct	tbl **tbls;	/* hashed table items */
};

struct tbl {			/* table item */
	int	flag;		/* flags */
	int	type;		/* command type (see below), base (if INTEGER),
				 * or offset from val.s of value (if EXPORT) */
	Area	*areap;		/* area to allocate from */
	union {
		char *s;	/* string */
		int64_t i;	/* integer */
		int (*f)(char **);	/* int function */
		struct op *t;	/* "function" tree */
	} val;			/* value */
	int	index;		/* index for an array */
	union {
	    int	field;		/* field with for -L/-R/-Z */
	    int errno_;		/* CEXEC/CTALIAS */
	} u2;
	union {
		struct tbl *array;	/* array values */
		char *fpath;		/* temporary path to undef function */
	} u;
	char	name[4];	/* name -- variable length */
};

/* common flag bits */
#define	ALLOC		BIT(0)	/* val.s has been allocated */
#define	DEFINED		BIT(1)	/* is defined in block */
#define	ISSET		BIT(2)	/* has value, vp->val.[si] */
#define	EXPORT		BIT(3)	/* exported variable/function */
#define	TRACE		BIT(4)	/* var: user flagged, func: execution tracing */
/* (start non-common flags at 8) */
/* flag bits used for variables */
#define	SPECIAL		BIT(8)	/* PATH, IFS, SECONDS, etc */
#define	INTEGER		BIT(9)	/* val.i contains integer value */
#define	RDONLY		BIT(10)	/* read-only variable */
#define	LOCAL		BIT(11)	/* for local typeset() */
#define ARRAY		BIT(13)	/* array */
#define LJUST		BIT(14)	/* left justify */
#define RJUST		BIT(15)	/* right justify */
#define ZEROFIL		BIT(16)	/* 0 filled if RJUSTIFY, strip 0s if LJUSTIFY */
#define LCASEV		BIT(17)	/* convert to lower case */
#define UCASEV_AL	BIT(18)/* convert to upper case / autoload function */
#define INT_U		BIT(19)	/* unsigned integer */
#define INT_L		BIT(20)	/* long integer (no-op) */
#define IMPORT		BIT(21)	/* flag to typeset(): no arrays, must have = */
#define LOCAL_COPY	BIT(22)	/* with LOCAL - copy attrs from existing var */
#define EXPRINEVAL	BIT(23)	/* contents currently being evaluated */
#define EXPRLVALUE	BIT(24)	/* useable as lvalue (temp flag) */
/* flag bits used for taliases/builtins/aliases/keywords/functions */
#define KEEPASN		BIT(8)	/* keep command assignments (eg, var=x cmd) */
#define FINUSE		BIT(9)	/* function being executed */
#define FDELETE		BIT(10)	/* function deleted while it was executing */
#define FKSH		BIT(11)	/* function defined with function x (vs x()) */
#define SPEC_BI		BIT(12)	/* a POSIX special builtin */
#define REG_BI		BIT(13)	/* a POSIX regular builtin */
/* Attributes that can be set by the user (used to decide if an unset param
 * should be repoted by set/typeset).  Does not include ARRAY or LOCAL.
 */
#define USERATTRIB	(EXPORT|INTEGER|RDONLY|LJUST|RJUST|ZEROFIL\
			 |LCASEV|UCASEV_AL|INT_U|INT_L)

/* command types */
#define	CNONE	0		/* undefined */
#define	CSHELL	1		/* built-in */
#define	CFUNC	2		/* function */
#define	CEXEC	4		/* executable command */
#define	CALIAS	5		/* alias */
#define	CKEYWD	6		/* keyword */
#define CTALIAS	7		/* tracked alias */

/* Flags for findcom()/comexec() */
#define FC_SPECBI	BIT(0)	/* special builtin */
#define FC_FUNC		BIT(1)	/* function builtin */
#define FC_REGBI	BIT(2)	/* regular builtin */
#define FC_UNREGBI	BIT(3)	/* un-regular builtin (!special,!regular) */
#define FC_BI		(FC_SPECBI|FC_REGBI|FC_UNREGBI)
#define FC_PATH		BIT(4)	/* do path search */
#define FC_DEFPATH	BIT(5)	/* use default path in path search */


#define AF_ARGV_ALLOC	0x1	/* argv[] array allocated */
#define AF_ARGS_ALLOCED	0x2	/* argument strings allocated */
#define AI_ARGV(a, i)	((i) == 0 ? (a).argv[0] : (a).argv[(i) - (a).skip])
#define AI_ARGC(a)	((a).argc_ - (a).skip)

/* Argument info.  Used for $#, $* for shell, functions, includes, etc. */
struct arg_info {
	int flags;	/* AF_* */
	char **argv;
	int argc_;
	int skip;	/* first arg is argv[0], second is argv[1 + skip] */
};

/*
 * activation record for function blocks
 */
struct block {
	Area	area;		/* area to allocate things */
	/*struct arg_info argi;*/
	char	**argv;
	int	argc;
	int	flags;		/* see BF_* */
	struct	table vars;	/* local variables */
	struct	table funs;	/* local functions */
	Getopt	getopts_state;
#if 1
	char *	error;		/* error handler */
	char *	exit;		/* exit handler */
#else
	Trap	error, exit;
#endif
	struct	block *next;	/* enclosing block */
};

/* Values for struct block.flags */
#define BF_DOGETOPTS	BIT(0)	/* save/restore getopts state */

/*
 * Used by ktwalk() and ktnext() routines.
 */
struct tstate {
	int left;
	struct tbl **next;
};

extern	struct table taliases;	/* tracked aliases */
extern	struct table builtins;	/* built-in commands */
extern	struct table aliases;	/* aliases */
extern	struct table keywords;	/* keywords */
extern	struct table homedirs;	/* homedir() cache */

struct builtin {
	const char   *name;
	int  (*func)(char **);
};

/* these really are externs! Look in table.c for them */
extern const struct builtin shbuiltins [], kshbuiltins [];

/* var spec values */
#define	V_NONE			0
#define	V_PATH			1
#define	V_IFS			2
#define	V_SECONDS		3
#define	V_OPTIND		4
#define	V_MAIL			5
#define	V_MAILPATH		6
#define	V_MAILCHECK		7
#define	V_RANDOM		8
#define	V_HISTCONTROL		9
#define	V_HISTSIZE		10
#define	V_HISTFILE		11
#define	V_VISUAL		12
#define	V_EDITOR		13
#define	V_COLUMNS		14
#define	V_POSIXLY_CORRECT	15
#define	V_TMOUT			16
#define	V_TMPDIR		17
#define	V_LINENO		18
#define	V_TERM			19

/* values for set_prompt() */
#define PS1	0		/* command */
#define PS2	1		/* command continuation */

extern char *search_path;	/* copy of either PATH or def_path */
extern const char *def_path;	/* path to use if PATH not set */
extern char *tmpdir;		/* TMPDIR value */
extern const char *prompt;
extern int cur_prompt;		/* PS1 or PS2 */
extern int current_lineno;	/* LINENO value */

unsigned int	hash(const char *);
void		ktinit(struct table *, Area *, int);
struct tbl *	ktsearch(struct table *, const char *, unsigned int);
struct tbl *	ktenter(struct table *, const char *, unsigned int);
void		ktdelete(struct tbl *);
void		ktwalk(struct tstate *, struct table *);
struct tbl *	ktnext(struct tstate *);
struct tbl **	ktsort(struct table *);
