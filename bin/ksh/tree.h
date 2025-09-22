/*	$OpenBSD: tree.h,v 1.12 2015/10/15 22:53:50 mmcc Exp $	*/

/*
 * command trees for compile/execute
 */

/* $From: tree.h,v 1.3 1994/05/31 13:34:34 michael Exp $ */

/*
 * Description of a command or an operation on commands.
 */
struct op {
	short	type;			/* operation type, see below */
	union { /* WARNING: newtp(), tcopy() use evalflags = 0 to clear union */
		short	evalflags;	/* TCOM: arg expansion eval() flags */
		short	ksh_func;	/* TFUNC: function x (vs x()) */
	} u;
	char  **args;			/* arguments to a command */
	char  **vars;			/* variable assignments */
	struct ioword	**ioact;	/* IO actions (eg, < > >>) */
	struct op *left, *right;	/* descendents */
	char   *str;			/* word for case; identifier for for,
					 * select, and functions;
					 * path to execute for TEXEC;
					 * time hook for TCOM.
					 */
	int	lineno;			/* TCOM/TFUNC: LINENO for this */
};

/* Tree.type values */
#define	TEOF		0
#define	TCOM		1	/* command */
#define	TPAREN		2	/* (c-list) */
#define	TPIPE		3	/* a | b */
#define	TLIST		4	/* a ; b */
#define	TOR		5	/* || */
#define	TAND		6	/* && */
#define TBANG		7	/* ! */
#define TDBRACKET	8	/* [[ .. ]] */
#define	TFOR		9
#define TSELECT		10
#define	TCASE		11
#define	TIF		12
#define	TWHILE		13
#define	TUNTIL		14
#define	TELIF		15
#define	TPAT		16	/* pattern in case */
#define	TBRACE		17	/* {c-list} */
#define	TASYNC		18	/* c & */
#define	TFUNCT		19	/* function name { command; } */
#define	TTIME		20	/* time pipeline */
#define	TEXEC		21	/* fork/exec eval'd TCOM */
#define TCOPROC		22	/* coprocess |& */

/*
 * prefix codes for words in command tree
 */
#define	EOS	0		/* end of string */
#define	CHAR	1		/* unquoted character */
#define	QCHAR	2		/* quoted character */
#define	COMSUB	3		/* $() substitution (0 terminated) */
#define EXPRSUB	4		/* $(()) substitution (0 terminated) */
#define	OQUOTE	5		/* opening " or ' */
#define	CQUOTE	6		/* closing " or ' */
#define	OSUBST	7		/* opening ${ subst (followed by { or X) */
#define	CSUBST	8		/* closing } of above (followed by } or X) */
#define OPAT	9		/* open pattern: *(, @(, etc. */
#define SPAT	10		/* separate pattern: | */
#define CPAT	11		/* close pattern: ) */

/*
 * IO redirection
 */
struct ioword {
	int	unit;	/* unit affected */
	int	flag;	/* action (below) */
	char	*name;	/* file name (unused if heredoc) */
	char	*delim;	/* delimiter for <<,<<- */
	char	*heredoc;/* content of heredoc */
};

/* ioword.flag - type of redirection */
#define	IOTYPE	0xF		/* type: bits 0:3 */
#define	IOREAD	0x1		/* < */
#define	IOWRITE	0x2		/* > */
#define	IORDWR	0x3		/* <>: todo */
#define	IOHERE	0x4		/* << (here file) */
#define	IOCAT	0x5		/* >> */
#define	IODUP	0x6		/* <&/>& */
#define	IOEVAL	BIT(4)		/* expand in << */
#define	IOSKIP	BIT(5)		/* <<-, skip ^\t* */
#define	IOCLOB	BIT(6)		/* >|, override -o noclobber */
#define IORDUP	BIT(7)		/* x<&y (as opposed to x>&y) */
#define IONAMEXP BIT(8)		/* name has been expanded */

/* execute/exchild flags */
#define	XEXEC	BIT(0)		/* execute without forking */
#define	XFORK	BIT(1)		/* fork before executing */
#define	XBGND	BIT(2)		/* command & */
#define	XPIPEI	BIT(3)		/* input is pipe */
#define	XPIPEO	BIT(4)		/* output is pipe */
#define	XPIPE	(XPIPEI|XPIPEO)	/* member of pipe */
#define	XXCOM	BIT(5)		/* `...` command */
#define	XPCLOSE	BIT(6)		/* exchild: close close_fd in parent */
#define	XCCLOSE	BIT(7)		/* exchild: close close_fd in child */
#define XERROK	BIT(8)		/* non-zero exit ok (for set -e) */
#define XCOPROC BIT(9)		/* starting a co-process */
#define XTIME	BIT(10)		/* timing TCOM command */

/*
 * flags to control expansion of words (assumed by t->evalflags to fit
 * in a short)
 */
#define	DOBLANK	BIT(0)		/* perform blank interpretation */
#define	DOGLOB	BIT(1)		/* expand [?* */
#define	DOPAT	BIT(2)		/* quote *?[ */
#define	DOTILDE	BIT(3)		/* normal ~ expansion (first char) */
#define DONTRUNCOMMAND BIT(4)	/* do not run $(command) things */
#define DOASNTILDE BIT(5)	/* assignment ~ expansion (after =, :) */
#define DOBRACE_ BIT(6)		/* used by expand(): do brace expansion */
#define DOMAGIC_ BIT(7)		/* used by expand(): string contains MAGIC */
#define DOTEMP_	BIT(8)		/* ditto : in word part of ${..[%#=?]..} */
#define DOVACHECK BIT(9)	/* var assign check (for typeset, set, etc) */
#define DOMARKDIRS BIT(10)	/* force markdirs behaviour */

/*
 * The arguments of [[ .. ]] expressions are kept in t->args[] and flags
 * indicating how the arguments have been munged are kept in t->vars[].
 * The contents of t->vars[] are stuffed strings (so they can be treated
 * like all other t->vars[]) in which the second character is the one that
 * is examined.  The DB_* defines are the values for these second characters.
 */
#define DB_NORM	1		/* normal argument */
#define DB_OR	2		/* || -> -o conversion */
#define DB_AND	3		/* && -> -a conversion */
#define DB_BE	4		/* an inserted -BE */
#define DB_PAT	5		/* a pattern argument */

void	fptreef(struct shf *, int, const char *, ...);
char *	snptreef(char *, int, const char *, ...);
struct op *	tcopy(struct op *, Area *);
char *	wdcopy(const char *, Area *);
char *	wdscan(const char *, int);
char *	wdstrip(const char *);
void	tfree(struct op *, Area *);
