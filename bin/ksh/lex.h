/*	$OpenBSD: lex.h,v 1.21 2018/01/15 14:58:05 jca Exp $	*/

/*
 * Source input, lexer and parser
 */

/* $From: lex.h,v 1.4 1994/05/31 13:34:34 michael Exp $ */

#define	IDENT	64

typedef struct source Source;
struct source {
	const char *str;	/* input pointer */
	int	type;		/* input type */
	const char *start;	/* start of current buffer */
	union {
		char **strv;	/* string [] */
		struct shf *shf; /* shell file */
		struct tbl *tblp; /* alias (SALIAS) */
		char *freeme;	/* also for SREREAD */
	} u;
	char	ugbuf[2];	/* buffer for ungetsc() (SREREAD) and
				 * alias (SALIAS) */
	int	line;		/* line number */
	int	cmd_offset;	/* line number - command number */
	int	errline;	/* line the error occurred on (0 if not set) */
	const char *file;	/* input file name */
	int	flags;		/* SF_* */
	Area	*areap;
	XString	xs;		/* input buffer */
	Source *next;		/* stacked source */
};

/* Source.type values */
#define	SEOF		0	/* input EOF */
#define	SFILE		1	/* file input */
#define SSTDIN		2	/* read stdin */
#define	SSTRING		3	/* string */
#define	SWSTR		4	/* string without \n */
#define	SWORDS		5	/* string[] */
#define	SWORDSEP	6	/* string[] separator */
#define	SALIAS		7	/* alias expansion */
#define SREREAD		8	/* read ahead to be re-scanned */

/* Source.flags values */
#define SF_ECHO		BIT(0)	/* echo input to shlout */
#define SF_ALIAS	BIT(1)	/* faking space at end of alias */
#define SF_ALIASEND	BIT(2)	/* faking space at end of alias */
#define SF_TTY		BIT(3)	/* type == SSTDIN & it is a tty */

typedef union {
	int	i;
	char   *cp;
	char  **wp;
	struct op *o;
	struct ioword *iop;
} YYSTYPE;

/* If something is added here, add it to tokentab[] in syn.c as well */
#define	LWORD	256
#define	LOGAND	257		/* && */
#define	LOGOR	258		/* || */
#define	BREAK	259		/* ;; */
#define	IF	260
#define	THEN	261
#define	ELSE	262
#define	ELIF	263
#define	FI	264
#define	CASE	265
#define	ESAC	266
#define	FOR	267
#define SELECT	268
#define	WHILE	269
#define	UNTIL	270
#define	DO	271
#define	DONE	272
#define	IN	273
#define	FUNCTION 274
#define	TIME	275
#define	REDIR	276
#define MDPAREN	277		/* (( )) */
#define BANG	278		/* ! */
#define DBRACKET 279		/* [[ .. ]] */
#define COPROC	280		/* |& */
#define	YYERRCODE 300

/* flags to yylex */
#define	CONTIN	BIT(0)		/* skip new lines to complete command */
#define	ONEWORD	BIT(1)		/* single word for substitute() */
#define	ALIAS	BIT(2)		/* recognize alias */
#define	KEYWORD	BIT(3)		/* recognize keywords */
#define LETEXPR	BIT(4)		/* get expression inside (( )) */
#define VARASN	BIT(5)		/* check for var=word */
#define ARRAYVAR BIT(6)		/* parse x[1 & 2] as one word */
#define ESACONLY BIT(7)		/* only accept esac keyword */
#define CMDWORD BIT(8)		/* parsing simple command (alias related) */
#define HEREDELIM BIT(9)	/* parsing <<,<<- delimiter */
#define HEREDOC BIT(10)		/* parsing heredoc */
#define UNESCAPE BIT(11)	/* remove backslashes */

#define	HERES	10		/* max << in line */

extern Source  *source;		/* yyparse/yylex source */
extern YYSTYPE	yylval;		/* result from yylex */
extern struct ioword *heres[HERES], **herep;
extern char	ident[IDENT+1];

#define HISTORYSIZE	500	/* size of saved history */

extern char   **history;	/* saved commands */
extern char   **histptr;	/* last history item */
extern uint32_t	histsize;	/* history size */

int	yylex(int);
void	yyerror(const char *, ...)
	    __attribute__((__noreturn__, __format__ (printf, 1, 2)));
Source * pushs(int, Area *);
void	set_prompt(int);
void	pprompt(const char *, int);
