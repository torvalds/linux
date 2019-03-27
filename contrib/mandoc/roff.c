/*	$Id: roff.c,v 1.329 2018/08/01 15:40:17 schwarze Exp $ */
/*
 * Copyright (c) 2008-2012, 2014 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2010-2015, 2017, 2018 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc.h"
#include "mandoc_aux.h"
#include "mandoc_ohash.h"
#include "roff.h"
#include "libmandoc.h"
#include "roff_int.h"
#include "libroff.h"

/* Maximum number of string expansions per line, to break infinite loops. */
#define	EXPAND_LIMIT	1000

/* Types of definitions of macros and strings. */
#define	ROFFDEF_USER	(1 << 1)  /* User-defined. */
#define	ROFFDEF_PRE	(1 << 2)  /* Predefined. */
#define	ROFFDEF_REN	(1 << 3)  /* Renamed standard macro. */
#define	ROFFDEF_STD	(1 << 4)  /* mdoc(7) or man(7) macro. */
#define	ROFFDEF_ANY	(ROFFDEF_USER | ROFFDEF_PRE | \
			 ROFFDEF_REN | ROFFDEF_STD)
#define	ROFFDEF_UNDEF	(1 << 5)  /* Completely undefined. */

/* --- data types --------------------------------------------------------- */

/*
 * An incredibly-simple string buffer.
 */
struct	roffstr {
	char		*p; /* nil-terminated buffer */
	size_t		 sz; /* saved strlen(p) */
};

/*
 * A key-value roffstr pair as part of a singly-linked list.
 */
struct	roffkv {
	struct roffstr	 key;
	struct roffstr	 val;
	struct roffkv	*next; /* next in list */
};

/*
 * A single number register as part of a singly-linked list.
 */
struct	roffreg {
	struct roffstr	 key;
	int		 val;
	int		 step;
	struct roffreg	*next;
};

/*
 * Association of request and macro names with token IDs.
 */
struct	roffreq {
	enum roff_tok	 tok;
	char		 name[];
};

struct	roff {
	struct mparse	*parse; /* parse point */
	struct roff_man	*man; /* mdoc or man parser */
	struct roffnode	*last; /* leaf of stack */
	int		*rstack; /* stack of inverted `ie' values */
	struct ohash	*reqtab; /* request lookup table */
	struct roffreg	*regtab; /* number registers */
	struct roffkv	*strtab; /* user-defined strings & macros */
	struct roffkv	*rentab; /* renamed strings & macros */
	struct roffkv	*xmbtab; /* multi-byte trans table (`tr') */
	struct roffstr	*xtab; /* single-byte trans table (`tr') */
	const char	*current_string; /* value of last called user macro */
	struct tbl_node	*first_tbl; /* first table parsed */
	struct tbl_node	*last_tbl; /* last table parsed */
	struct tbl_node	*tbl; /* current table being parsed */
	struct eqn_node	*last_eqn; /* equation parser */
	struct eqn_node	*eqn; /* active equation parser */
	int		 eqn_inline; /* current equation is inline */
	int		 options; /* parse options */
	int		 rstacksz; /* current size limit of rstack */
	int		 rstackpos; /* position in rstack */
	int		 format; /* current file in mdoc or man format */
	int		 argc; /* number of args of the last macro */
	char		 control; /* control character */
	char		 escape; /* escape character */
};

struct	roffnode {
	enum roff_tok	 tok; /* type of node */
	struct roffnode	*parent; /* up one in stack */
	int		 line; /* parse line */
	int		 col; /* parse col */
	char		*name; /* node name, e.g. macro name */
	char		*end; /* end-rules: custom token */
	int		 endspan; /* end-rules: next-line or infty */
	int		 rule; /* current evaluation rule */
};

#define	ROFF_ARGS	 struct roff *r, /* parse ctx */ \
			 enum roff_tok tok, /* tok of macro */ \
			 struct buf *buf, /* input buffer */ \
			 int ln, /* parse line */ \
			 int ppos, /* original pos in buffer */ \
			 int pos, /* current pos in buffer */ \
			 int *offs /* reset offset of buffer data */

typedef	enum rofferr (*roffproc)(ROFF_ARGS);

struct	roffmac {
	roffproc	 proc; /* process new macro */
	roffproc	 text; /* process as child text of macro */
	roffproc	 sub; /* process as child of macro */
	int		 flags;
#define	ROFFMAC_STRUCT	(1 << 0) /* always interpret */
};

struct	predef {
	const char	*name; /* predefined input name */
	const char	*str; /* replacement symbol */
};

#define	PREDEF(__name, __str) \
	{ (__name), (__str) },

/* --- function prototypes ------------------------------------------------ */

static	void		 roffnode_cleanscope(struct roff *);
static	void		 roffnode_pop(struct roff *);
static	void		 roffnode_push(struct roff *, enum roff_tok,
				const char *, int, int);
static	void		 roff_addtbl(struct roff_man *, struct tbl_node *);
static	enum rofferr	 roff_als(ROFF_ARGS);
static	enum rofferr	 roff_block(ROFF_ARGS);
static	enum rofferr	 roff_block_text(ROFF_ARGS);
static	enum rofferr	 roff_block_sub(ROFF_ARGS);
static	enum rofferr	 roff_br(ROFF_ARGS);
static	enum rofferr	 roff_cblock(ROFF_ARGS);
static	enum rofferr	 roff_cc(ROFF_ARGS);
static	void		 roff_ccond(struct roff *, int, int);
static	enum rofferr	 roff_cond(ROFF_ARGS);
static	enum rofferr	 roff_cond_text(ROFF_ARGS);
static	enum rofferr	 roff_cond_sub(ROFF_ARGS);
static	enum rofferr	 roff_ds(ROFF_ARGS);
static	enum rofferr	 roff_ec(ROFF_ARGS);
static	enum rofferr	 roff_eo(ROFF_ARGS);
static	enum rofferr	 roff_eqndelim(struct roff *, struct buf *, int);
static	int		 roff_evalcond(struct roff *r, int, char *, int *);
static	int		 roff_evalnum(struct roff *, int,
				const char *, int *, int *, int);
static	int		 roff_evalpar(struct roff *, int,
				const char *, int *, int *, int);
static	int		 roff_evalstrcond(const char *, int *);
static	void		 roff_free1(struct roff *);
static	void		 roff_freereg(struct roffreg *);
static	void		 roff_freestr(struct roffkv *);
static	size_t		 roff_getname(struct roff *, char **, int, int);
static	int		 roff_getnum(const char *, int *, int *, int);
static	int		 roff_getop(const char *, int *, char *);
static	int		 roff_getregn(struct roff *,
				const char *, size_t, char);
static	int		 roff_getregro(const struct roff *,
				const char *name);
static	const char	*roff_getstrn(struct roff *,
				const char *, size_t, int *);
static	int		 roff_hasregn(const struct roff *,
				const char *, size_t);
static	enum rofferr	 roff_insec(ROFF_ARGS);
static	enum rofferr	 roff_it(ROFF_ARGS);
static	enum rofferr	 roff_line_ignore(ROFF_ARGS);
static	void		 roff_man_alloc1(struct roff_man *);
static	void		 roff_man_free1(struct roff_man *);
static	enum rofferr	 roff_manyarg(ROFF_ARGS);
static	enum rofferr	 roff_nr(ROFF_ARGS);
static	enum rofferr	 roff_onearg(ROFF_ARGS);
static	enum roff_tok	 roff_parse(struct roff *, char *, int *,
				int, int);
static	enum rofferr	 roff_parsetext(struct roff *, struct buf *,
				int, int *);
static	enum rofferr	 roff_renamed(ROFF_ARGS);
static	enum rofferr	 roff_res(struct roff *, struct buf *, int, int);
static	enum rofferr	 roff_rm(ROFF_ARGS);
static	enum rofferr	 roff_rn(ROFF_ARGS);
static	enum rofferr	 roff_rr(ROFF_ARGS);
static	void		 roff_setregn(struct roff *, const char *,
				size_t, int, char, int);
static	void		 roff_setstr(struct roff *,
				const char *, const char *, int);
static	void		 roff_setstrn(struct roffkv **, const char *,
				size_t, const char *, size_t, int);
static	enum rofferr	 roff_so(ROFF_ARGS);
static	enum rofferr	 roff_tr(ROFF_ARGS);
static	enum rofferr	 roff_Dd(ROFF_ARGS);
static	enum rofferr	 roff_TE(ROFF_ARGS);
static	enum rofferr	 roff_TS(ROFF_ARGS);
static	enum rofferr	 roff_EQ(ROFF_ARGS);
static	enum rofferr	 roff_EN(ROFF_ARGS);
static	enum rofferr	 roff_T_(ROFF_ARGS);
static	enum rofferr	 roff_unsupp(ROFF_ARGS);
static	enum rofferr	 roff_userdef(ROFF_ARGS);

/* --- constant data ------------------------------------------------------ */

#define	ROFFNUM_SCALE	(1 << 0)  /* Honour scaling in roff_getnum(). */
#define	ROFFNUM_WHITE	(1 << 1)  /* Skip whitespace in roff_evalnum(). */

const char *__roff_name[MAN_MAX + 1] = {
	"br",		"ce",		"ft",		"ll",
	"mc",		"po",		"rj",		"sp",
	"ta",		"ti",		NULL,
	"ab",		"ad",		"af",		"aln",
	"als",		"am",		"am1",		"ami",
	"ami1",		"as",		"as1",		"asciify",
	"backtrace",	"bd",		"bleedat",	"blm",
        "box",		"boxa",		"bp",		"BP",
	"break",	"breakchar",	"brnl",		"brp",
	"brpnl",	"c2",		"cc",
	"cf",		"cflags",	"ch",		"char",
	"chop",		"class",	"close",	"CL",
	"color",	"composite",	"continue",	"cp",
	"cropat",	"cs",		"cu",		"da",
	"dch",		"Dd",		"de",		"de1",
	"defcolor",	"dei",		"dei1",		"device",
	"devicem",	"di",		"do",		"ds",
	"ds1",		"dwh",		"dt",		"ec",
	"ecr",		"ecs",		"el",		"em",
	"EN",		"eo",		"EP",		"EQ",
	"errprint",	"ev",		"evc",		"ex",
	"fallback",	"fam",		"fc",		"fchar",
	"fcolor",	"fdeferlig",	"feature",	"fkern",
	"fl",		"flig",		"fp",		"fps",
	"fschar",	"fspacewidth",	"fspecial",	"ftr",
	"fzoom",	"gcolor",	"hc",		"hcode",
	"hidechar",	"hla",		"hlm",		"hpf",
	"hpfa",		"hpfcode",	"hw",		"hy",
	"hylang",	"hylen",	"hym",		"hypp",
	"hys",		"ie",		"if",		"ig",
	"index",	"it",		"itc",		"IX",
	"kern",		"kernafter",	"kernbefore",	"kernpair",
	"lc",		"lc_ctype",	"lds",		"length",
	"letadj",	"lf",		"lg",		"lhang",
	"linetabs",	"lnr",		"lnrf",		"lpfx",
	"ls",		"lsm",		"lt",
	"mediasize",	"minss",	"mk",		"mso",
	"na",		"ne",		"nh",		"nhychar",
	"nm",		"nn",		"nop",		"nr",
	"nrf",		"nroff",	"ns",		"nx",
	"open",		"opena",	"os",		"output",
	"padj",		"papersize",	"pc",		"pev",
	"pi",		"PI",		"pl",		"pm",
	"pn",		"pnr",		"ps",
	"psbb",		"pshape",	"pso",		"ptr",
	"pvs",		"rchar",	"rd",		"recursionlimit",
	"return",	"rfschar",	"rhang",
	"rm",		"rn",		"rnn",		"rr",
	"rs",		"rt",		"schar",	"sentchar",
	"shc",		"shift",	"sizes",	"so",
	"spacewidth",	"special",	"spreadwarn",	"ss",
	"sty",		"substring",	"sv",		"sy",
	"T&",		"tc",		"TE",
	"TH",		"tkf",		"tl",
	"tm",		"tm1",		"tmc",		"tr",
	"track",	"transchar",	"trf",		"trimat",
	"trin",		"trnt",		"troff",	"TS",
	"uf",		"ul",		"unformat",	"unwatch",
	"unwatchn",	"vpt",		"vs",		"warn",
	"warnscale",	"watch",	"watchlength",	"watchn",
	"wh",		"while",	"write",	"writec",
	"writem",	"xflag",	".",		NULL,
	NULL,		"text",
	"Dd",		"Dt",		"Os",		"Sh",
	"Ss",		"Pp",		"D1",		"Dl",
	"Bd",		"Ed",		"Bl",		"El",
	"It",		"Ad",		"An",		"Ap",
	"Ar",		"Cd",		"Cm",		"Dv",
	"Er",		"Ev",		"Ex",		"Fa",
	"Fd",		"Fl",		"Fn",		"Ft",
	"Ic",		"In",		"Li",		"Nd",
	"Nm",		"Op",		"Ot",		"Pa",
	"Rv",		"St",		"Va",		"Vt",
	"Xr",		"%A",		"%B",		"%D",
	"%I",		"%J",		"%N",		"%O",
	"%P",		"%R",		"%T",		"%V",
	"Ac",		"Ao",		"Aq",		"At",
	"Bc",		"Bf",		"Bo",		"Bq",
	"Bsx",		"Bx",		"Db",		"Dc",
	"Do",		"Dq",		"Ec",		"Ef",
	"Em",		"Eo",		"Fx",		"Ms",
	"No",		"Ns",		"Nx",		"Ox",
	"Pc",		"Pf",		"Po",		"Pq",
	"Qc",		"Ql",		"Qo",		"Qq",
	"Re",		"Rs",		"Sc",		"So",
	"Sq",		"Sm",		"Sx",		"Sy",
	"Tn",		"Ux",		"Xc",		"Xo",
	"Fo",		"Fc",		"Oo",		"Oc",
	"Bk",		"Ek",		"Bt",		"Hf",
	"Fr",		"Ud",		"Lb",		"Lp",
	"Lk",		"Mt",		"Brq",		"Bro",
	"Brc",		"%C",		"Es",		"En",
	"Dx",		"%Q",		"%U",		"Ta",
	NULL,
	"TH",		"SH",		"SS",		"TP",
	"LP",		"PP",		"P",		"IP",
	"HP",		"SM",		"SB",		"BI",
	"IB",		"BR",		"RB",		"R",
	"B",		"I",		"IR",		"RI",
	"nf",		"fi",
	"RE",		"RS",		"DT",		"UC",
	"PD",		"AT",		"in",
	"OP",		"EX",		"EE",		"UR",
	"UE",		"MT",		"ME",		NULL
};
const	char *const *roff_name = __roff_name;

static	struct roffmac	 roffs[TOKEN_NONE] = {
	{ roff_br, NULL, NULL, 0 },  /* br */
	{ roff_onearg, NULL, NULL, 0 },  /* ce */
	{ roff_onearg, NULL, NULL, 0 },  /* ft */
	{ roff_onearg, NULL, NULL, 0 },  /* ll */
	{ roff_onearg, NULL, NULL, 0 },  /* mc */
	{ roff_onearg, NULL, NULL, 0 },  /* po */
	{ roff_onearg, NULL, NULL, 0 },  /* rj */
	{ roff_onearg, NULL, NULL, 0 },  /* sp */
	{ roff_manyarg, NULL, NULL, 0 },  /* ta */
	{ roff_onearg, NULL, NULL, 0 },  /* ti */
	{ NULL, NULL, NULL, 0 },  /* ROFF_MAX */
	{ roff_unsupp, NULL, NULL, 0 },  /* ab */
	{ roff_line_ignore, NULL, NULL, 0 },  /* ad */
	{ roff_line_ignore, NULL, NULL, 0 },  /* af */
	{ roff_unsupp, NULL, NULL, 0 },  /* aln */
	{ roff_als, NULL, NULL, 0 },  /* als */
	{ roff_block, roff_block_text, roff_block_sub, 0 },  /* am */
	{ roff_block, roff_block_text, roff_block_sub, 0 },  /* am1 */
	{ roff_block, roff_block_text, roff_block_sub, 0 },  /* ami */
	{ roff_block, roff_block_text, roff_block_sub, 0 },  /* ami1 */
	{ roff_ds, NULL, NULL, 0 },  /* as */
	{ roff_ds, NULL, NULL, 0 },  /* as1 */
	{ roff_unsupp, NULL, NULL, 0 },  /* asciify */
	{ roff_line_ignore, NULL, NULL, 0 },  /* backtrace */
	{ roff_line_ignore, NULL, NULL, 0 },  /* bd */
	{ roff_line_ignore, NULL, NULL, 0 },  /* bleedat */
	{ roff_unsupp, NULL, NULL, 0 },  /* blm */
	{ roff_unsupp, NULL, NULL, 0 },  /* box */
	{ roff_unsupp, NULL, NULL, 0 },  /* boxa */
	{ roff_line_ignore, NULL, NULL, 0 },  /* bp */
	{ roff_unsupp, NULL, NULL, 0 },  /* BP */
	{ roff_unsupp, NULL, NULL, 0 },  /* break */
	{ roff_line_ignore, NULL, NULL, 0 },  /* breakchar */
	{ roff_line_ignore, NULL, NULL, 0 },  /* brnl */
	{ roff_br, NULL, NULL, 0 },  /* brp */
	{ roff_line_ignore, NULL, NULL, 0 },  /* brpnl */
	{ roff_unsupp, NULL, NULL, 0 },  /* c2 */
	{ roff_cc, NULL, NULL, 0 },  /* cc */
	{ roff_insec, NULL, NULL, 0 },  /* cf */
	{ roff_line_ignore, NULL, NULL, 0 },  /* cflags */
	{ roff_line_ignore, NULL, NULL, 0 },  /* ch */
	{ roff_unsupp, NULL, NULL, 0 },  /* char */
	{ roff_unsupp, NULL, NULL, 0 },  /* chop */
	{ roff_line_ignore, NULL, NULL, 0 },  /* class */
	{ roff_insec, NULL, NULL, 0 },  /* close */
	{ roff_unsupp, NULL, NULL, 0 },  /* CL */
	{ roff_line_ignore, NULL, NULL, 0 },  /* color */
	{ roff_unsupp, NULL, NULL, 0 },  /* composite */
	{ roff_unsupp, NULL, NULL, 0 },  /* continue */
	{ roff_line_ignore, NULL, NULL, 0 },  /* cp */
	{ roff_line_ignore, NULL, NULL, 0 },  /* cropat */
	{ roff_line_ignore, NULL, NULL, 0 },  /* cs */
	{ roff_line_ignore, NULL, NULL, 0 },  /* cu */
	{ roff_unsupp, NULL, NULL, 0 },  /* da */
	{ roff_unsupp, NULL, NULL, 0 },  /* dch */
	{ roff_Dd, NULL, NULL, 0 },  /* Dd */
	{ roff_block, roff_block_text, roff_block_sub, 0 },  /* de */
	{ roff_block, roff_block_text, roff_block_sub, 0 },  /* de1 */
	{ roff_line_ignore, NULL, NULL, 0 },  /* defcolor */
	{ roff_block, roff_block_text, roff_block_sub, 0 },  /* dei */
	{ roff_block, roff_block_text, roff_block_sub, 0 },  /* dei1 */
	{ roff_unsupp, NULL, NULL, 0 },  /* device */
	{ roff_unsupp, NULL, NULL, 0 },  /* devicem */
	{ roff_unsupp, NULL, NULL, 0 },  /* di */
	{ roff_unsupp, NULL, NULL, 0 },  /* do */
	{ roff_ds, NULL, NULL, 0 },  /* ds */
	{ roff_ds, NULL, NULL, 0 },  /* ds1 */
	{ roff_unsupp, NULL, NULL, 0 },  /* dwh */
	{ roff_unsupp, NULL, NULL, 0 },  /* dt */
	{ roff_ec, NULL, NULL, 0 },  /* ec */
	{ roff_unsupp, NULL, NULL, 0 },  /* ecr */
	{ roff_unsupp, NULL, NULL, 0 },  /* ecs */
	{ roff_cond, roff_cond_text, roff_cond_sub, ROFFMAC_STRUCT },  /* el */
	{ roff_unsupp, NULL, NULL, 0 },  /* em */
	{ roff_EN, NULL, NULL, 0 },  /* EN */
	{ roff_eo, NULL, NULL, 0 },  /* eo */
	{ roff_unsupp, NULL, NULL, 0 },  /* EP */
	{ roff_EQ, NULL, NULL, 0 },  /* EQ */
	{ roff_line_ignore, NULL, NULL, 0 },  /* errprint */
	{ roff_unsupp, NULL, NULL, 0 },  /* ev */
	{ roff_unsupp, NULL, NULL, 0 },  /* evc */
	{ roff_unsupp, NULL, NULL, 0 },  /* ex */
	{ roff_line_ignore, NULL, NULL, 0 },  /* fallback */
	{ roff_line_ignore, NULL, NULL, 0 },  /* fam */
	{ roff_unsupp, NULL, NULL, 0 },  /* fc */
	{ roff_unsupp, NULL, NULL, 0 },  /* fchar */
	{ roff_line_ignore, NULL, NULL, 0 },  /* fcolor */
	{ roff_line_ignore, NULL, NULL, 0 },  /* fdeferlig */
	{ roff_line_ignore, NULL, NULL, 0 },  /* feature */
	{ roff_line_ignore, NULL, NULL, 0 },  /* fkern */
	{ roff_line_ignore, NULL, NULL, 0 },  /* fl */
	{ roff_line_ignore, NULL, NULL, 0 },  /* flig */
	{ roff_line_ignore, NULL, NULL, 0 },  /* fp */
	{ roff_line_ignore, NULL, NULL, 0 },  /* fps */
	{ roff_unsupp, NULL, NULL, 0 },  /* fschar */
	{ roff_line_ignore, NULL, NULL, 0 },  /* fspacewidth */
	{ roff_line_ignore, NULL, NULL, 0 },  /* fspecial */
	{ roff_line_ignore, NULL, NULL, 0 },  /* ftr */
	{ roff_line_ignore, NULL, NULL, 0 },  /* fzoom */
	{ roff_line_ignore, NULL, NULL, 0 },  /* gcolor */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hc */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hcode */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hidechar */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hla */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hlm */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hpf */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hpfa */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hpfcode */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hw */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hy */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hylang */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hylen */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hym */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hypp */
	{ roff_line_ignore, NULL, NULL, 0 },  /* hys */
	{ roff_cond, roff_cond_text, roff_cond_sub, ROFFMAC_STRUCT },  /* ie */
	{ roff_cond, roff_cond_text, roff_cond_sub, ROFFMAC_STRUCT },  /* if */
	{ roff_block, roff_block_text, roff_block_sub, 0 },  /* ig */
	{ roff_unsupp, NULL, NULL, 0 },  /* index */
	{ roff_it, NULL, NULL, 0 },  /* it */
	{ roff_unsupp, NULL, NULL, 0 },  /* itc */
	{ roff_line_ignore, NULL, NULL, 0 },  /* IX */
	{ roff_line_ignore, NULL, NULL, 0 },  /* kern */
	{ roff_line_ignore, NULL, NULL, 0 },  /* kernafter */
	{ roff_line_ignore, NULL, NULL, 0 },  /* kernbefore */
	{ roff_line_ignore, NULL, NULL, 0 },  /* kernpair */
	{ roff_unsupp, NULL, NULL, 0 },  /* lc */
	{ roff_unsupp, NULL, NULL, 0 },  /* lc_ctype */
	{ roff_unsupp, NULL, NULL, 0 },  /* lds */
	{ roff_unsupp, NULL, NULL, 0 },  /* length */
	{ roff_line_ignore, NULL, NULL, 0 },  /* letadj */
	{ roff_insec, NULL, NULL, 0 },  /* lf */
	{ roff_line_ignore, NULL, NULL, 0 },  /* lg */
	{ roff_line_ignore, NULL, NULL, 0 },  /* lhang */
	{ roff_unsupp, NULL, NULL, 0 },  /* linetabs */
	{ roff_unsupp, NULL, NULL, 0 },  /* lnr */
	{ roff_unsupp, NULL, NULL, 0 },  /* lnrf */
	{ roff_unsupp, NULL, NULL, 0 },  /* lpfx */
	{ roff_line_ignore, NULL, NULL, 0 },  /* ls */
	{ roff_unsupp, NULL, NULL, 0 },  /* lsm */
	{ roff_line_ignore, NULL, NULL, 0 },  /* lt */
	{ roff_line_ignore, NULL, NULL, 0 },  /* mediasize */
	{ roff_line_ignore, NULL, NULL, 0 },  /* minss */
	{ roff_line_ignore, NULL, NULL, 0 },  /* mk */
	{ roff_insec, NULL, NULL, 0 },  /* mso */
	{ roff_line_ignore, NULL, NULL, 0 },  /* na */
	{ roff_line_ignore, NULL, NULL, 0 },  /* ne */
	{ roff_line_ignore, NULL, NULL, 0 },  /* nh */
	{ roff_line_ignore, NULL, NULL, 0 },  /* nhychar */
	{ roff_unsupp, NULL, NULL, 0 },  /* nm */
	{ roff_unsupp, NULL, NULL, 0 },  /* nn */
	{ roff_unsupp, NULL, NULL, 0 },  /* nop */
	{ roff_nr, NULL, NULL, 0 },  /* nr */
	{ roff_unsupp, NULL, NULL, 0 },  /* nrf */
	{ roff_line_ignore, NULL, NULL, 0 },  /* nroff */
	{ roff_line_ignore, NULL, NULL, 0 },  /* ns */
	{ roff_insec, NULL, NULL, 0 },  /* nx */
	{ roff_insec, NULL, NULL, 0 },  /* open */
	{ roff_insec, NULL, NULL, 0 },  /* opena */
	{ roff_line_ignore, NULL, NULL, 0 },  /* os */
	{ roff_unsupp, NULL, NULL, 0 },  /* output */
	{ roff_line_ignore, NULL, NULL, 0 },  /* padj */
	{ roff_line_ignore, NULL, NULL, 0 },  /* papersize */
	{ roff_line_ignore, NULL, NULL, 0 },  /* pc */
	{ roff_line_ignore, NULL, NULL, 0 },  /* pev */
	{ roff_insec, NULL, NULL, 0 },  /* pi */
	{ roff_unsupp, NULL, NULL, 0 },  /* PI */
	{ roff_line_ignore, NULL, NULL, 0 },  /* pl */
	{ roff_line_ignore, NULL, NULL, 0 },  /* pm */
	{ roff_line_ignore, NULL, NULL, 0 },  /* pn */
	{ roff_line_ignore, NULL, NULL, 0 },  /* pnr */
	{ roff_line_ignore, NULL, NULL, 0 },  /* ps */
	{ roff_unsupp, NULL, NULL, 0 },  /* psbb */
	{ roff_unsupp, NULL, NULL, 0 },  /* pshape */
	{ roff_insec, NULL, NULL, 0 },  /* pso */
	{ roff_line_ignore, NULL, NULL, 0 },  /* ptr */
	{ roff_line_ignore, NULL, NULL, 0 },  /* pvs */
	{ roff_unsupp, NULL, NULL, 0 },  /* rchar */
	{ roff_line_ignore, NULL, NULL, 0 },  /* rd */
	{ roff_line_ignore, NULL, NULL, 0 },  /* recursionlimit */
	{ roff_unsupp, NULL, NULL, 0 },  /* return */
	{ roff_unsupp, NULL, NULL, 0 },  /* rfschar */
	{ roff_line_ignore, NULL, NULL, 0 },  /* rhang */
	{ roff_rm, NULL, NULL, 0 },  /* rm */
	{ roff_rn, NULL, NULL, 0 },  /* rn */
	{ roff_unsupp, NULL, NULL, 0 },  /* rnn */
	{ roff_rr, NULL, NULL, 0 },  /* rr */
	{ roff_line_ignore, NULL, NULL, 0 },  /* rs */
	{ roff_line_ignore, NULL, NULL, 0 },  /* rt */
	{ roff_unsupp, NULL, NULL, 0 },  /* schar */
	{ roff_line_ignore, NULL, NULL, 0 },  /* sentchar */
	{ roff_line_ignore, NULL, NULL, 0 },  /* shc */
	{ roff_unsupp, NULL, NULL, 0 },  /* shift */
	{ roff_line_ignore, NULL, NULL, 0 },  /* sizes */
	{ roff_so, NULL, NULL, 0 },  /* so */
	{ roff_line_ignore, NULL, NULL, 0 },  /* spacewidth */
	{ roff_line_ignore, NULL, NULL, 0 },  /* special */
	{ roff_line_ignore, NULL, NULL, 0 },  /* spreadwarn */
	{ roff_line_ignore, NULL, NULL, 0 },  /* ss */
	{ roff_line_ignore, NULL, NULL, 0 },  /* sty */
	{ roff_unsupp, NULL, NULL, 0 },  /* substring */
	{ roff_line_ignore, NULL, NULL, 0 },  /* sv */
	{ roff_insec, NULL, NULL, 0 },  /* sy */
	{ roff_T_, NULL, NULL, 0 },  /* T& */
	{ roff_unsupp, NULL, NULL, 0 },  /* tc */
	{ roff_TE, NULL, NULL, 0 },  /* TE */
	{ roff_Dd, NULL, NULL, 0 },  /* TH */
	{ roff_line_ignore, NULL, NULL, 0 },  /* tkf */
	{ roff_unsupp, NULL, NULL, 0 },  /* tl */
	{ roff_line_ignore, NULL, NULL, 0 },  /* tm */
	{ roff_line_ignore, NULL, NULL, 0 },  /* tm1 */
	{ roff_line_ignore, NULL, NULL, 0 },  /* tmc */
	{ roff_tr, NULL, NULL, 0 },  /* tr */
	{ roff_line_ignore, NULL, NULL, 0 },  /* track */
	{ roff_line_ignore, NULL, NULL, 0 },  /* transchar */
	{ roff_insec, NULL, NULL, 0 },  /* trf */
	{ roff_line_ignore, NULL, NULL, 0 },  /* trimat */
	{ roff_unsupp, NULL, NULL, 0 },  /* trin */
	{ roff_unsupp, NULL, NULL, 0 },  /* trnt */
	{ roff_line_ignore, NULL, NULL, 0 },  /* troff */
	{ roff_TS, NULL, NULL, 0 },  /* TS */
	{ roff_line_ignore, NULL, NULL, 0 },  /* uf */
	{ roff_line_ignore, NULL, NULL, 0 },  /* ul */
	{ roff_unsupp, NULL, NULL, 0 },  /* unformat */
	{ roff_line_ignore, NULL, NULL, 0 },  /* unwatch */
	{ roff_line_ignore, NULL, NULL, 0 },  /* unwatchn */
	{ roff_line_ignore, NULL, NULL, 0 },  /* vpt */
	{ roff_line_ignore, NULL, NULL, 0 },  /* vs */
	{ roff_line_ignore, NULL, NULL, 0 },  /* warn */
	{ roff_line_ignore, NULL, NULL, 0 },  /* warnscale */
	{ roff_line_ignore, NULL, NULL, 0 },  /* watch */
	{ roff_line_ignore, NULL, NULL, 0 },  /* watchlength */
	{ roff_line_ignore, NULL, NULL, 0 },  /* watchn */
	{ roff_unsupp, NULL, NULL, 0 },  /* wh */
	{ roff_unsupp, NULL, NULL, 0 },  /* while */
	{ roff_insec, NULL, NULL, 0 },  /* write */
	{ roff_insec, NULL, NULL, 0 },  /* writec */
	{ roff_insec, NULL, NULL, 0 },  /* writem */
	{ roff_line_ignore, NULL, NULL, 0 },  /* xflag */
	{ roff_cblock, NULL, NULL, 0 },  /* . */
	{ roff_renamed, NULL, NULL, 0 },
	{ roff_userdef, NULL, NULL, 0 }
};

/* Array of injected predefined strings. */
#define	PREDEFS_MAX	 38
static	const struct predef predefs[PREDEFS_MAX] = {
#include "predefs.in"
};

static	int	 roffce_lines;	/* number of input lines to center */
static	struct roff_node *roffce_node;  /* active request */
static	int	 roffit_lines;  /* number of lines to delay */
static	char	*roffit_macro;  /* nil-terminated macro line */


/* --- request table ------------------------------------------------------ */

struct ohash *
roffhash_alloc(enum roff_tok mintok, enum roff_tok maxtok)
{
	struct ohash	*htab;
	struct roffreq	*req;
	enum roff_tok	 tok;
	size_t		 sz;
	unsigned int	 slot;

	htab = mandoc_malloc(sizeof(*htab));
	mandoc_ohash_init(htab, 8, offsetof(struct roffreq, name));

	for (tok = mintok; tok < maxtok; tok++) {
		if (roff_name[tok] == NULL)
			continue;
		sz = strlen(roff_name[tok]);
		req = mandoc_malloc(sizeof(*req) + sz + 1);
		req->tok = tok;
		memcpy(req->name, roff_name[tok], sz + 1);
		slot = ohash_qlookup(htab, req->name);
		ohash_insert(htab, slot, req);
	}
	return htab;
}

void
roffhash_free(struct ohash *htab)
{
	struct roffreq	*req;
	unsigned int	 slot;

	if (htab == NULL)
		return;
	for (req = ohash_first(htab, &slot); req != NULL;
	     req = ohash_next(htab, &slot))
		free(req);
	ohash_delete(htab);
	free(htab);
}

enum roff_tok
roffhash_find(struct ohash *htab, const char *name, size_t sz)
{
	struct roffreq	*req;
	const char	*end;

	if (sz) {
		end = name + sz;
		req = ohash_find(htab, ohash_qlookupi(htab, name, &end));
	} else
		req = ohash_find(htab, ohash_qlookup(htab, name));
	return req == NULL ? TOKEN_NONE : req->tok;
}

/* --- stack of request blocks -------------------------------------------- */

/*
 * Pop the current node off of the stack of roff instructions currently
 * pending.
 */
static void
roffnode_pop(struct roff *r)
{
	struct roffnode	*p;

	assert(r->last);
	p = r->last;

	r->last = r->last->parent;
	free(p->name);
	free(p->end);
	free(p);
}

/*
 * Push a roff node onto the instruction stack.  This must later be
 * removed with roffnode_pop().
 */
static void
roffnode_push(struct roff *r, enum roff_tok tok, const char *name,
		int line, int col)
{
	struct roffnode	*p;

	p = mandoc_calloc(1, sizeof(struct roffnode));
	p->tok = tok;
	if (name)
		p->name = mandoc_strdup(name);
	p->parent = r->last;
	p->line = line;
	p->col = col;
	p->rule = p->parent ? p->parent->rule : 0;

	r->last = p;
}

/* --- roff parser state data management ---------------------------------- */

static void
roff_free1(struct roff *r)
{
	struct tbl_node	*tbl;
	int		 i;

	while (NULL != (tbl = r->first_tbl)) {
		r->first_tbl = tbl->next;
		tbl_free(tbl);
	}
	r->first_tbl = r->last_tbl = r->tbl = NULL;

	if (r->last_eqn != NULL)
		eqn_free(r->last_eqn);
	r->last_eqn = r->eqn = NULL;

	while (r->last)
		roffnode_pop(r);

	free (r->rstack);
	r->rstack = NULL;
	r->rstacksz = 0;
	r->rstackpos = -1;

	roff_freereg(r->regtab);
	r->regtab = NULL;

	roff_freestr(r->strtab);
	roff_freestr(r->rentab);
	roff_freestr(r->xmbtab);
	r->strtab = r->rentab = r->xmbtab = NULL;

	if (r->xtab)
		for (i = 0; i < 128; i++)
			free(r->xtab[i].p);
	free(r->xtab);
	r->xtab = NULL;
}

void
roff_reset(struct roff *r)
{
	roff_free1(r);
	r->format = r->options & (MPARSE_MDOC | MPARSE_MAN);
	r->control = '\0';
	r->escape = '\\';
	roffce_lines = 0;
	roffce_node = NULL;
	roffit_lines = 0;
	roffit_macro = NULL;
}

void
roff_free(struct roff *r)
{
	roff_free1(r);
	roffhash_free(r->reqtab);
	free(r);
}

struct roff *
roff_alloc(struct mparse *parse, int options)
{
	struct roff	*r;

	r = mandoc_calloc(1, sizeof(struct roff));
	r->parse = parse;
	r->reqtab = roffhash_alloc(0, ROFF_RENAMED);
	r->options = options;
	r->format = options & (MPARSE_MDOC | MPARSE_MAN);
	r->rstackpos = -1;
	r->escape = '\\';
	return r;
}

/* --- syntax tree state data management ---------------------------------- */

static void
roff_man_free1(struct roff_man *man)
{

	if (man->first != NULL)
		roff_node_delete(man, man->first);
	free(man->meta.msec);
	free(man->meta.vol);
	free(man->meta.os);
	free(man->meta.arch);
	free(man->meta.title);
	free(man->meta.name);
	free(man->meta.date);
}

static void
roff_man_alloc1(struct roff_man *man)
{

	memset(&man->meta, 0, sizeof(man->meta));
	man->first = mandoc_calloc(1, sizeof(*man->first));
	man->first->type = ROFFT_ROOT;
	man->last = man->first;
	man->last_es = NULL;
	man->flags = 0;
	man->macroset = MACROSET_NONE;
	man->lastsec = man->lastnamed = SEC_NONE;
	man->next = ROFF_NEXT_CHILD;
}

void
roff_man_reset(struct roff_man *man)
{

	roff_man_free1(man);
	roff_man_alloc1(man);
}

void
roff_man_free(struct roff_man *man)
{

	roff_man_free1(man);
	free(man);
}

struct roff_man *
roff_man_alloc(struct roff *roff, struct mparse *parse,
	const char *os_s, int quick)
{
	struct roff_man *man;

	man = mandoc_calloc(1, sizeof(*man));
	man->parse = parse;
	man->roff = roff;
	man->os_s = os_s;
	man->quick = quick;
	roff_man_alloc1(man);
	roff->man = man;
	return man;
}

/* --- syntax tree handling ----------------------------------------------- */

struct roff_node *
roff_node_alloc(struct roff_man *man, int line, int pos,
	enum roff_type type, int tok)
{
	struct roff_node	*n;

	n = mandoc_calloc(1, sizeof(*n));
	n->line = line;
	n->pos = pos;
	n->tok = tok;
	n->type = type;
	n->sec = man->lastsec;

	if (man->flags & MDOC_SYNOPSIS)
		n->flags |= NODE_SYNPRETTY;
	else
		n->flags &= ~NODE_SYNPRETTY;
	if (man->flags & MDOC_NEWLINE)
		n->flags |= NODE_LINE;
	man->flags &= ~MDOC_NEWLINE;

	return n;
}

void
roff_node_append(struct roff_man *man, struct roff_node *n)
{

	switch (man->next) {
	case ROFF_NEXT_SIBLING:
		if (man->last->next != NULL) {
			n->next = man->last->next;
			man->last->next->prev = n;
		} else
			man->last->parent->last = n;
		man->last->next = n;
		n->prev = man->last;
		n->parent = man->last->parent;
		break;
	case ROFF_NEXT_CHILD:
		if (man->last->child != NULL) {
			n->next = man->last->child;
			man->last->child->prev = n;
		} else
			man->last->last = n;
		man->last->child = n;
		n->parent = man->last;
		break;
	default:
		abort();
	}
	man->last = n;

	switch (n->type) {
	case ROFFT_HEAD:
		n->parent->head = n;
		break;
	case ROFFT_BODY:
		if (n->end != ENDBODY_NOT)
			return;
		n->parent->body = n;
		break;
	case ROFFT_TAIL:
		n->parent->tail = n;
		break;
	default:
		return;
	}

	/*
	 * Copy over the normalised-data pointer of our parent.  Not
	 * everybody has one, but copying a null pointer is fine.
	 */

	n->norm = n->parent->norm;
	assert(n->parent->type == ROFFT_BLOCK);
}

void
roff_word_alloc(struct roff_man *man, int line, int pos, const char *word)
{
	struct roff_node	*n;

	n = roff_node_alloc(man, line, pos, ROFFT_TEXT, TOKEN_NONE);
	n->string = roff_strdup(man->roff, word);
	roff_node_append(man, n);
	n->flags |= NODE_VALID | NODE_ENDED;
	man->next = ROFF_NEXT_SIBLING;
}

void
roff_word_append(struct roff_man *man, const char *word)
{
	struct roff_node	*n;
	char			*addstr, *newstr;

	n = man->last;
	addstr = roff_strdup(man->roff, word);
	mandoc_asprintf(&newstr, "%s %s", n->string, addstr);
	free(addstr);
	free(n->string);
	n->string = newstr;
	man->next = ROFF_NEXT_SIBLING;
}

void
roff_elem_alloc(struct roff_man *man, int line, int pos, int tok)
{
	struct roff_node	*n;

	n = roff_node_alloc(man, line, pos, ROFFT_ELEM, tok);
	roff_node_append(man, n);
	man->next = ROFF_NEXT_CHILD;
}

struct roff_node *
roff_block_alloc(struct roff_man *man, int line, int pos, int tok)
{
	struct roff_node	*n;

	n = roff_node_alloc(man, line, pos, ROFFT_BLOCK, tok);
	roff_node_append(man, n);
	man->next = ROFF_NEXT_CHILD;
	return n;
}

struct roff_node *
roff_head_alloc(struct roff_man *man, int line, int pos, int tok)
{
	struct roff_node	*n;

	n = roff_node_alloc(man, line, pos, ROFFT_HEAD, tok);
	roff_node_append(man, n);
	man->next = ROFF_NEXT_CHILD;
	return n;
}

struct roff_node *
roff_body_alloc(struct roff_man *man, int line, int pos, int tok)
{
	struct roff_node	*n;

	n = roff_node_alloc(man, line, pos, ROFFT_BODY, tok);
	roff_node_append(man, n);
	man->next = ROFF_NEXT_CHILD;
	return n;
}

static void
roff_addtbl(struct roff_man *man, struct tbl_node *tbl)
{
	struct roff_node	*n;
	const struct tbl_span	*span;

	if (man->macroset == MACROSET_MAN)
		man_breakscope(man, ROFF_TS);
	while ((span = tbl_span(tbl)) != NULL) {
		n = roff_node_alloc(man, tbl->line, 0, ROFFT_TBL, TOKEN_NONE);
		n->span = span;
		roff_node_append(man, n);
		n->flags |= NODE_VALID | NODE_ENDED;
		man->next = ROFF_NEXT_SIBLING;
	}
}

void
roff_node_unlink(struct roff_man *man, struct roff_node *n)
{

	/* Adjust siblings. */

	if (n->prev)
		n->prev->next = n->next;
	if (n->next)
		n->next->prev = n->prev;

	/* Adjust parent. */

	if (n->parent != NULL) {
		if (n->parent->child == n)
			n->parent->child = n->next;
		if (n->parent->last == n)
			n->parent->last = n->prev;
	}

	/* Adjust parse point. */

	if (man == NULL)
		return;
	if (man->last == n) {
		if (n->prev == NULL) {
			man->last = n->parent;
			man->next = ROFF_NEXT_CHILD;
		} else {
			man->last = n->prev;
			man->next = ROFF_NEXT_SIBLING;
		}
	}
	if (man->first == n)
		man->first = NULL;
}

void
roff_node_free(struct roff_node *n)
{

	if (n->args != NULL)
		mdoc_argv_free(n->args);
	if (n->type == ROFFT_BLOCK || n->type == ROFFT_ELEM)
		free(n->norm);
	if (n->eqn != NULL)
		eqn_box_free(n->eqn);
	free(n->string);
	free(n);
}

void
roff_node_delete(struct roff_man *man, struct roff_node *n)
{

	while (n->child != NULL)
		roff_node_delete(man, n->child);
	roff_node_unlink(man, n);
	roff_node_free(n);
}

void
deroff(char **dest, const struct roff_node *n)
{
	char	*cp;
	size_t	 sz;

	if (n->type != ROFFT_TEXT) {
		for (n = n->child; n != NULL; n = n->next)
			deroff(dest, n);
		return;
	}

	/* Skip leading whitespace. */

	for (cp = n->string; *cp != '\0'; cp++) {
		if (cp[0] == '\\' && cp[1] != '\0' &&
		    strchr(" %&0^|~", cp[1]) != NULL)
			cp++;
		else if ( ! isspace((unsigned char)*cp))
			break;
	}

	/* Skip trailing backslash. */

	sz = strlen(cp);
	if (sz > 0 && cp[sz - 1] == '\\')
		sz--;

	/* Skip trailing whitespace. */

	for (; sz; sz--)
		if ( ! isspace((unsigned char)cp[sz-1]))
			break;

	/* Skip empty strings. */

	if (sz == 0)
		return;

	if (*dest == NULL) {
		*dest = mandoc_strndup(cp, sz);
		return;
	}

	mandoc_asprintf(&cp, "%s %*s", *dest, (int)sz, cp);
	free(*dest);
	*dest = cp;
}

/* --- main functions of the roff parser ---------------------------------- */

/*
 * In the current line, expand escape sequences that tend to get
 * used in numerical expressions and conditional requests.
 * Also check the syntax of the remaining escape sequences.
 */
static enum rofferr
roff_res(struct roff *r, struct buf *buf, int ln, int pos)
{
	char		 ubuf[24]; /* buffer to print the number */
	struct roff_node *n;	/* used for header comments */
	const char	*start;	/* start of the string to process */
	char		*stesc;	/* start of an escape sequence ('\\') */
	char		*ep;	/* end of comment string */
	const char	*stnam;	/* start of the name, after "[(*" */
	const char	*cp;	/* end of the name, e.g. before ']' */
	const char	*res;	/* the string to be substituted */
	char		*nbuf;	/* new buffer to copy buf->buf to */
	size_t		 maxl;  /* expected length of the escape name */
	size_t		 naml;	/* actual length of the escape name */
	enum mandoc_esc	 esc;	/* type of the escape sequence */
	int		 inaml;	/* length returned from mandoc_escape() */
	int		 expand_count;	/* to avoid infinite loops */
	int		 npos;	/* position in numeric expression */
	int		 arg_complete; /* argument not interrupted by eol */
	int		 done;	/* no more input available */
	int		 deftype; /* type of definition to paste */
	int		 rcsid;	/* kind of RCS id seen */
	char		 sign;	/* increment number register */
	char		 term;	/* character terminating the escape */

	/* Search forward for comments. */

	done = 0;
	start = buf->buf + pos;
	for (stesc = buf->buf + pos; *stesc != '\0'; stesc++) {
		if (stesc[0] != r->escape || stesc[1] == '\0')
			continue;
		stesc++;
		if (*stesc != '"' && *stesc != '#')
			continue;

		/* Comment found, look for RCS id. */

		rcsid = 0;
		if ((cp = strstr(stesc, "$" "OpenBSD")) != NULL) {
			rcsid = 1 << MANDOC_OS_OPENBSD;
			cp += 8;
		} else if ((cp = strstr(stesc, "$" "NetBSD")) != NULL) {
			rcsid = 1 << MANDOC_OS_NETBSD;
			cp += 7;
		}
		if (cp != NULL &&
		    isalnum((unsigned char)*cp) == 0 &&
		    strchr(cp, '$') != NULL) {
			if (r->man->meta.rcsids & rcsid)
				mandoc_msg(MANDOCERR_RCS_REP, r->parse,
				    ln, stesc + 1 - buf->buf, stesc + 1);
			r->man->meta.rcsids |= rcsid;
		}

		/* Handle trailing whitespace. */

		ep = strchr(stesc--, '\0') - 1;
		if (*ep == '\n') {
			done = 1;
			ep--;
		}
		if (*ep == ' ' || *ep == '\t')
			mandoc_msg(MANDOCERR_SPACE_EOL, r->parse,
			    ln, ep - buf->buf, NULL);

		/*
		 * Save comments preceding the title macro
		 * in the syntax tree.
		 */

		if (r->format == 0) {
			while (*ep == ' ' || *ep == '\t')
				ep--;
			ep[1] = '\0';
			n = roff_node_alloc(r->man,
			    ln, stesc + 1 - buf->buf,
			    ROFFT_COMMENT, TOKEN_NONE);
			n->string = mandoc_strdup(stesc + 2);
			roff_node_append(r->man, n);
			n->flags |= NODE_VALID | NODE_ENDED;
			r->man->next = ROFF_NEXT_SIBLING;
		}

		/* Discard comments. */

		while (stesc > start && stesc[-1] == ' ')
			stesc--;
		*stesc = '\0';
		break;
	}
	if (stesc == start)
		return ROFF_CONT;
	stesc--;

	/* Notice the end of the input. */

	if (*stesc == '\n') {
		*stesc-- = '\0';
		done = 1;
	}

	expand_count = 0;
	while (stesc >= start) {

		/* Search backwards for the next backslash. */

		if (*stesc != r->escape) {
			if (*stesc == '\\') {
				*stesc = '\0';
				buf->sz = mandoc_asprintf(&nbuf, "%s\\e%s",
				    buf->buf, stesc + 1) + 1;
				start = nbuf + pos;
				stesc = nbuf + (stesc - buf->buf);
				free(buf->buf);
				buf->buf = nbuf;
			}
			stesc--;
			continue;
		}

		/* If it is escaped, skip it. */

		for (cp = stesc - 1; cp >= start; cp--)
			if (*cp != r->escape)
				break;

		if ((stesc - cp) % 2 == 0) {
			while (stesc > cp)
				*stesc-- = '\\';
			continue;
		} else if (stesc[1] != '\0') {
			*stesc = '\\';
		} else {
			*stesc-- = '\0';
			if (done)
				continue;
			else
				return ROFF_APPEND;
		}

		/* Decide whether to expand or to check only. */

		term = '\0';
		cp = stesc + 1;
		switch (*cp) {
		case '*':
			res = NULL;
			break;
		case 'B':
		case 'w':
			term = cp[1];
			/* FALLTHROUGH */
		case 'n':
			sign = cp[1];
			if (sign == '+' || sign == '-')
				cp++;
			res = ubuf;
			break;
		default:
			esc = mandoc_escape(&cp, &stnam, &inaml);
			if (esc == ESCAPE_ERROR ||
			    (esc == ESCAPE_SPECIAL &&
			     mchars_spec2cp(stnam, inaml) < 0))
				mandoc_vmsg(MANDOCERR_ESC_BAD,
				    r->parse, ln, (int)(stesc - buf->buf),
				    "%.*s", (int)(cp - stesc), stesc);
			stesc--;
			continue;
		}

		if (EXPAND_LIMIT < ++expand_count) {
			mandoc_msg(MANDOCERR_ROFFLOOP, r->parse,
			    ln, (int)(stesc - buf->buf), NULL);
			return ROFF_IGN;
		}

		/*
		 * The third character decides the length
		 * of the name of the string or register.
		 * Save a pointer to the name.
		 */

		if (term == '\0') {
			switch (*++cp) {
			case '\0':
				maxl = 0;
				break;
			case '(':
				cp++;
				maxl = 2;
				break;
			case '[':
				cp++;
				term = ']';
				maxl = 0;
				break;
			default:
				maxl = 1;
				break;
			}
		} else {
			cp += 2;
			maxl = 0;
		}
		stnam = cp;

		/* Advance to the end of the name. */

		naml = 0;
		arg_complete = 1;
		while (maxl == 0 || naml < maxl) {
			if (*cp == '\0') {
				mandoc_msg(MANDOCERR_ESC_BAD, r->parse,
				    ln, (int)(stesc - buf->buf), stesc);
				arg_complete = 0;
				break;
			}
			if (maxl == 0 && *cp == term) {
				cp++;
				break;
			}
			if (*cp++ != '\\' || stesc[1] != 'w') {
				naml++;
				continue;
			}
			switch (mandoc_escape(&cp, NULL, NULL)) {
			case ESCAPE_SPECIAL:
			case ESCAPE_UNICODE:
			case ESCAPE_NUMBERED:
			case ESCAPE_OVERSTRIKE:
				naml++;
				break;
			default:
				break;
			}
		}

		/*
		 * Retrieve the replacement string; if it is
		 * undefined, resume searching for escapes.
		 */

		switch (stesc[1]) {
		case '*':
			if (arg_complete) {
				deftype = ROFFDEF_USER | ROFFDEF_PRE;
				res = roff_getstrn(r, stnam, naml, &deftype);
			}
			break;
		case 'B':
			npos = 0;
			ubuf[0] = arg_complete &&
			    roff_evalnum(r, ln, stnam, &npos,
			      NULL, ROFFNUM_SCALE) &&
			    stnam + npos + 1 == cp ? '1' : '0';
			ubuf[1] = '\0';
			break;
		case 'n':
			if (arg_complete)
				(void)snprintf(ubuf, sizeof(ubuf), "%d",
				    roff_getregn(r, stnam, naml, sign));
			else
				ubuf[0] = '\0';
			break;
		case 'w':
			/* use even incomplete args */
			(void)snprintf(ubuf, sizeof(ubuf), "%d",
			    24 * (int)naml);
			break;
		}

		if (res == NULL) {
			mandoc_vmsg(MANDOCERR_STR_UNDEF,
			    r->parse, ln, (int)(stesc - buf->buf),
			    "%.*s", (int)naml, stnam);
			res = "";
		} else if (buf->sz + strlen(res) > SHRT_MAX) {
			mandoc_msg(MANDOCERR_ROFFLOOP, r->parse,
			    ln, (int)(stesc - buf->buf), NULL);
			return ROFF_IGN;
		}

		/* Replace the escape sequence by the string. */

		*stesc = '\0';
		buf->sz = mandoc_asprintf(&nbuf, "%s%s%s",
		    buf->buf, res, cp) + 1;

		/* Prepare for the next replacement. */

		start = nbuf + pos;
		stesc = nbuf + (stesc - buf->buf) + strlen(res);
		free(buf->buf);
		buf->buf = nbuf;
	}
	return ROFF_CONT;
}

/*
 * Process text streams.
 */
static enum rofferr
roff_parsetext(struct roff *r, struct buf *buf, int pos, int *offs)
{
	size_t		 sz;
	const char	*start;
	char		*p;
	int		 isz;
	enum mandoc_esc	 esc;

	/* Spring the input line trap. */

	if (roffit_lines == 1) {
		isz = mandoc_asprintf(&p, "%s\n.%s", buf->buf, roffit_macro);
		free(buf->buf);
		buf->buf = p;
		buf->sz = isz + 1;
		*offs = 0;
		free(roffit_macro);
		roffit_lines = 0;
		return ROFF_REPARSE;
	} else if (roffit_lines > 1)
		--roffit_lines;

	if (roffce_node != NULL && buf->buf[pos] != '\0') {
		if (roffce_lines < 1) {
			r->man->last = roffce_node;
			r->man->next = ROFF_NEXT_SIBLING;
			roffce_lines = 0;
			roffce_node = NULL;
		} else
			roffce_lines--;
	}

	/* Convert all breakable hyphens into ASCII_HYPH. */

	start = p = buf->buf + pos;

	while (*p != '\0') {
		sz = strcspn(p, "-\\");
		p += sz;

		if (*p == '\0')
			break;

		if (*p == '\\') {
			/* Skip over escapes. */
			p++;
			esc = mandoc_escape((const char **)&p, NULL, NULL);
			if (esc == ESCAPE_ERROR)
				break;
			while (*p == '-')
				p++;
			continue;
		} else if (p == start) {
			p++;
			continue;
		}

		if (isalpha((unsigned char)p[-1]) &&
		    isalpha((unsigned char)p[1]))
			*p = ASCII_HYPH;
		p++;
	}
	return ROFF_CONT;
}

enum rofferr
roff_parseln(struct roff *r, int ln, struct buf *buf, int *offs)
{
	enum roff_tok	 t;
	enum rofferr	 e;
	int		 pos;	/* parse point */
	int		 spos;	/* saved parse point for messages */
	int		 ppos;	/* original offset in buf->buf */
	int		 ctl;	/* macro line (boolean) */

	ppos = pos = *offs;

	/* Handle in-line equation delimiters. */

	if (r->tbl == NULL &&
	    r->last_eqn != NULL && r->last_eqn->delim &&
	    (r->eqn == NULL || r->eqn_inline)) {
		e = roff_eqndelim(r, buf, pos);
		if (e == ROFF_REPARSE)
			return e;
		assert(e == ROFF_CONT);
	}

	/* Expand some escape sequences. */

	e = roff_res(r, buf, ln, pos);
	if (e == ROFF_IGN || e == ROFF_APPEND)
		return e;
	assert(e == ROFF_CONT);

	ctl = roff_getcontrol(r, buf->buf, &pos);

	/*
	 * First, if a scope is open and we're not a macro, pass the
	 * text through the macro's filter.
	 * Equations process all content themselves.
	 * Tables process almost all content themselves, but we want
	 * to warn about macros before passing it there.
	 */

	if (r->last != NULL && ! ctl) {
		t = r->last->tok;
		e = (*roffs[t].text)(r, t, buf, ln, pos, pos, offs);
		if (e == ROFF_IGN)
			return e;
		assert(e == ROFF_CONT);
	}
	if (r->eqn != NULL && strncmp(buf->buf + ppos, ".EN", 3)) {
		eqn_read(r->eqn, buf->buf + ppos);
		return ROFF_IGN;
	}
	if (r->tbl != NULL && (ctl == 0 || buf->buf[pos] == '\0')) {
		tbl_read(r->tbl, ln, buf->buf, ppos);
		roff_addtbl(r->man, r->tbl);
		return ROFF_IGN;
	}
	if ( ! ctl)
		return roff_parsetext(r, buf, pos, offs);

	/* Skip empty request lines. */

	if (buf->buf[pos] == '"') {
		mandoc_msg(MANDOCERR_COMMENT_BAD, r->parse,
		    ln, pos, NULL);
		return ROFF_IGN;
	} else if (buf->buf[pos] == '\0')
		return ROFF_IGN;

	/*
	 * If a scope is open, go to the child handler for that macro,
	 * as it may want to preprocess before doing anything with it.
	 * Don't do so if an equation is open.
	 */

	if (r->last) {
		t = r->last->tok;
		return (*roffs[t].sub)(r, t, buf, ln, ppos, pos, offs);
	}

	/* No scope is open.  This is a new request or macro. */

	spos = pos;
	t = roff_parse(r, buf->buf, &pos, ln, ppos);

	/* Tables ignore most macros. */

	if (r->tbl != NULL && (t == TOKEN_NONE || t == ROFF_TS ||
	    t == ROFF_br || t == ROFF_ce || t == ROFF_rj || t == ROFF_sp)) {
		mandoc_msg(MANDOCERR_TBLMACRO, r->parse,
		    ln, pos, buf->buf + spos);
		if (t != TOKEN_NONE)
			return ROFF_IGN;
		while (buf->buf[pos] != '\0' && buf->buf[pos] != ' ')
			pos++;
		while (buf->buf[pos] == ' ')
			pos++;
		tbl_read(r->tbl, ln, buf->buf, pos);
		roff_addtbl(r->man, r->tbl);
		return ROFF_IGN;
	}

	/* For now, let high level macros abort .ce mode. */

	if (ctl && roffce_node != NULL &&
	    (t == TOKEN_NONE || t == ROFF_Dd || t == ROFF_EQ ||
	     t == ROFF_TH || t == ROFF_TS)) {
		r->man->last = roffce_node;
		r->man->next = ROFF_NEXT_SIBLING;
		roffce_lines = 0;
		roffce_node = NULL;
	}

	/*
	 * This is neither a roff request nor a user-defined macro.
	 * Let the standard macro set parsers handle it.
	 */

	if (t == TOKEN_NONE)
		return ROFF_CONT;

	/* Execute a roff request or a user defined macro. */

	return (*roffs[t].proc)(r, t, buf, ln, spos, pos, offs);
}

void
roff_endparse(struct roff *r)
{
	if (r->last != NULL)
		mandoc_msg(MANDOCERR_BLK_NOEND, r->parse,
		    r->last->line, r->last->col,
		    roff_name[r->last->tok]);

	if (r->eqn != NULL) {
		mandoc_msg(MANDOCERR_BLK_NOEND, r->parse,
		    r->eqn->node->line, r->eqn->node->pos, "EQ");
		eqn_parse(r->eqn);
		r->eqn = NULL;
	}

	if (r->tbl != NULL) {
		mandoc_msg(MANDOCERR_BLK_NOEND, r->parse,
		    r->tbl->line, r->tbl->pos, "TS");
		tbl_end(r->tbl);
		r->tbl = NULL;
	}
}

/*
 * Parse a roff node's type from the input buffer.  This must be in the
 * form of ".foo xxx" in the usual way.
 */
static enum roff_tok
roff_parse(struct roff *r, char *buf, int *pos, int ln, int ppos)
{
	char		*cp;
	const char	*mac;
	size_t		 maclen;
	int		 deftype;
	enum roff_tok	 t;

	cp = buf + *pos;

	if ('\0' == *cp || '"' == *cp || '\t' == *cp || ' ' == *cp)
		return TOKEN_NONE;

	mac = cp;
	maclen = roff_getname(r, &cp, ln, ppos);

	deftype = ROFFDEF_USER | ROFFDEF_REN;
	r->current_string = roff_getstrn(r, mac, maclen, &deftype);
	switch (deftype) {
	case ROFFDEF_USER:
		t = ROFF_USERDEF;
		break;
	case ROFFDEF_REN:
		t = ROFF_RENAMED;
		break;
	default:
		t = roffhash_find(r->reqtab, mac, maclen);
		break;
	}
	if (t != TOKEN_NONE)
		*pos = cp - buf;
	else if (deftype == ROFFDEF_UNDEF) {
		/* Using an undefined macro defines it to be empty. */
		roff_setstrn(&r->strtab, mac, maclen, "", 0, 0);
		roff_setstrn(&r->rentab, mac, maclen, NULL, 0, 0);
	}
	return t;
}

/* --- handling of request blocks ----------------------------------------- */

static enum rofferr
roff_cblock(ROFF_ARGS)
{

	/*
	 * A block-close `..' should only be invoked as a child of an
	 * ignore macro, otherwise raise a warning and just ignore it.
	 */

	if (r->last == NULL) {
		mandoc_msg(MANDOCERR_BLK_NOTOPEN, r->parse,
		    ln, ppos, "..");
		return ROFF_IGN;
	}

	switch (r->last->tok) {
	case ROFF_am:
		/* ROFF_am1 is remapped to ROFF_am in roff_block(). */
	case ROFF_ami:
	case ROFF_de:
		/* ROFF_de1 is remapped to ROFF_de in roff_block(). */
	case ROFF_dei:
	case ROFF_ig:
		break;
	default:
		mandoc_msg(MANDOCERR_BLK_NOTOPEN, r->parse,
		    ln, ppos, "..");
		return ROFF_IGN;
	}

	if (buf->buf[pos] != '\0')
		mandoc_vmsg(MANDOCERR_ARG_SKIP, r->parse, ln, pos,
		    ".. %s", buf->buf + pos);

	roffnode_pop(r);
	roffnode_cleanscope(r);
	return ROFF_IGN;

}

static void
roffnode_cleanscope(struct roff *r)
{

	while (r->last) {
		if (--r->last->endspan != 0)
			break;
		roffnode_pop(r);
	}
}

static void
roff_ccond(struct roff *r, int ln, int ppos)
{

	if (NULL == r->last) {
		mandoc_msg(MANDOCERR_BLK_NOTOPEN, r->parse,
		    ln, ppos, "\\}");
		return;
	}

	switch (r->last->tok) {
	case ROFF_el:
	case ROFF_ie:
	case ROFF_if:
		break;
	default:
		mandoc_msg(MANDOCERR_BLK_NOTOPEN, r->parse,
		    ln, ppos, "\\}");
		return;
	}

	if (r->last->endspan > -1) {
		mandoc_msg(MANDOCERR_BLK_NOTOPEN, r->parse,
		    ln, ppos, "\\}");
		return;
	}

	roffnode_pop(r);
	roffnode_cleanscope(r);
	return;
}

static enum rofferr
roff_block(ROFF_ARGS)
{
	const char	*name, *value;
	char		*call, *cp, *iname, *rname;
	size_t		 csz, namesz, rsz;
	int		 deftype;

	/* Ignore groff compatibility mode for now. */

	if (tok == ROFF_de1)
		tok = ROFF_de;
	else if (tok == ROFF_dei1)
		tok = ROFF_dei;
	else if (tok == ROFF_am1)
		tok = ROFF_am;
	else if (tok == ROFF_ami1)
		tok = ROFF_ami;

	/* Parse the macro name argument. */

	cp = buf->buf + pos;
	if (tok == ROFF_ig) {
		iname = NULL;
		namesz = 0;
	} else {
		iname = cp;
		namesz = roff_getname(r, &cp, ln, ppos);
		iname[namesz] = '\0';
	}

	/* Resolve the macro name argument if it is indirect. */

	if (namesz && (tok == ROFF_dei || tok == ROFF_ami)) {
		deftype = ROFFDEF_USER;
		name = roff_getstrn(r, iname, namesz, &deftype);
		if (name == NULL) {
			mandoc_vmsg(MANDOCERR_STR_UNDEF,
			    r->parse, ln, (int)(iname - buf->buf),
			    "%.*s", (int)namesz, iname);
			namesz = 0;
		} else
			namesz = strlen(name);
	} else
		name = iname;

	if (namesz == 0 && tok != ROFF_ig) {
		mandoc_msg(MANDOCERR_REQ_EMPTY, r->parse,
		    ln, ppos, roff_name[tok]);
		return ROFF_IGN;
	}

	roffnode_push(r, tok, name, ln, ppos);

	/*
	 * At the beginning of a `de' macro, clear the existing string
	 * with the same name, if there is one.  New content will be
	 * appended from roff_block_text() in multiline mode.
	 */

	if (tok == ROFF_de || tok == ROFF_dei) {
		roff_setstrn(&r->strtab, name, namesz, "", 0, 0);
		roff_setstrn(&r->rentab, name, namesz, NULL, 0, 0);
	} else if (tok == ROFF_am || tok == ROFF_ami) {
		deftype = ROFFDEF_ANY;
		value = roff_getstrn(r, iname, namesz, &deftype);
		switch (deftype) {  /* Before appending, ... */
		case ROFFDEF_PRE: /* copy predefined to user-defined. */
			roff_setstrn(&r->strtab, name, namesz,
			    value, strlen(value), 0);
			break;
		case ROFFDEF_REN: /* call original standard macro. */
			csz = mandoc_asprintf(&call, ".%.*s \\$* \\\"\n",
			    (int)strlen(value), value);
			roff_setstrn(&r->strtab, name, namesz, call, csz, 0);
			roff_setstrn(&r->rentab, name, namesz, NULL, 0, 0);
			free(call);
			break;
		case ROFFDEF_STD:  /* rename and call standard macro. */
			rsz = mandoc_asprintf(&rname, "__%s_renamed", name);
			roff_setstrn(&r->rentab, rname, rsz, name, namesz, 0);
			csz = mandoc_asprintf(&call, ".%.*s \\$* \\\"\n",
			    (int)rsz, rname);
			roff_setstrn(&r->strtab, name, namesz, call, csz, 0);
			free(call);
			free(rname);
			break;
		default:
			break;
		}
	}

	if (*cp == '\0')
		return ROFF_IGN;

	/* Get the custom end marker. */

	iname = cp;
	namesz = roff_getname(r, &cp, ln, ppos);

	/* Resolve the end marker if it is indirect. */

	if (namesz && (tok == ROFF_dei || tok == ROFF_ami)) {
		deftype = ROFFDEF_USER;
		name = roff_getstrn(r, iname, namesz, &deftype);
		if (name == NULL) {
			mandoc_vmsg(MANDOCERR_STR_UNDEF,
			    r->parse, ln, (int)(iname - buf->buf),
			    "%.*s", (int)namesz, iname);
			namesz = 0;
		} else
			namesz = strlen(name);
	} else
		name = iname;

	if (namesz)
		r->last->end = mandoc_strndup(name, namesz);

	if (*cp != '\0')
		mandoc_vmsg(MANDOCERR_ARG_EXCESS, r->parse,
		    ln, pos, ".%s ... %s", roff_name[tok], cp);

	return ROFF_IGN;
}

static enum rofferr
roff_block_sub(ROFF_ARGS)
{
	enum roff_tok	t;
	int		i, j;

	/*
	 * First check whether a custom macro exists at this level.  If
	 * it does, then check against it.  This is some of groff's
	 * stranger behaviours.  If we encountered a custom end-scope
	 * tag and that tag also happens to be a "real" macro, then we
	 * need to try interpreting it again as a real macro.  If it's
	 * not, then return ignore.  Else continue.
	 */

	if (r->last->end) {
		for (i = pos, j = 0; r->last->end[j]; j++, i++)
			if (buf->buf[i] != r->last->end[j])
				break;

		if (r->last->end[j] == '\0' &&
		    (buf->buf[i] == '\0' ||
		     buf->buf[i] == ' ' ||
		     buf->buf[i] == '\t')) {
			roffnode_pop(r);
			roffnode_cleanscope(r);

			while (buf->buf[i] == ' ' || buf->buf[i] == '\t')
				i++;

			pos = i;
			if (roff_parse(r, buf->buf, &pos, ln, ppos) !=
			    TOKEN_NONE)
				return ROFF_RERUN;
			return ROFF_IGN;
		}
	}

	/*
	 * If we have no custom end-query or lookup failed, then try
	 * pulling it out of the hashtable.
	 */

	t = roff_parse(r, buf->buf, &pos, ln, ppos);

	if (t != ROFF_cblock) {
		if (tok != ROFF_ig)
			roff_setstr(r, r->last->name, buf->buf + ppos, 2);
		return ROFF_IGN;
	}

	return (*roffs[t].proc)(r, t, buf, ln, ppos, pos, offs);
}

static enum rofferr
roff_block_text(ROFF_ARGS)
{

	if (tok != ROFF_ig)
		roff_setstr(r, r->last->name, buf->buf + pos, 2);

	return ROFF_IGN;
}

static enum rofferr
roff_cond_sub(ROFF_ARGS)
{
	enum roff_tok	 t;
	char		*ep;
	int		 rr;

	rr = r->last->rule;
	roffnode_cleanscope(r);

	/*
	 * If `\}' occurs on a macro line without a preceding macro,
	 * drop the line completely.
	 */

	ep = buf->buf + pos;
	if (ep[0] == '\\' && ep[1] == '}')
		rr = 0;

	/* Always check for the closing delimiter `\}'. */

	while ((ep = strchr(ep, '\\')) != NULL) {
		switch (ep[1]) {
		case '}':
			memmove(ep, ep + 2, strlen(ep + 2) + 1);
			roff_ccond(r, ln, ep - buf->buf);
			break;
		case '\0':
			++ep;
			break;
		default:
			ep += 2;
			break;
		}
	}

	/*
	 * Fully handle known macros when they are structurally
	 * required or when the conditional evaluated to true.
	 */

	t = roff_parse(r, buf->buf, &pos, ln, ppos);
	return t != TOKEN_NONE && (rr || roffs[t].flags & ROFFMAC_STRUCT)
	    ? (*roffs[t].proc)(r, t, buf, ln, ppos, pos, offs) : rr
	    ? ROFF_CONT : ROFF_IGN;
}

static enum rofferr
roff_cond_text(ROFF_ARGS)
{
	char		*ep;
	int		 rr;

	rr = r->last->rule;
	roffnode_cleanscope(r);

	ep = buf->buf + pos;
	while ((ep = strchr(ep, '\\')) != NULL) {
		if (*(++ep) == '}') {
			*ep = '&';
			roff_ccond(r, ln, ep - buf->buf - 1);
		}
		if (*ep != '\0')
			++ep;
	}
	return rr ? ROFF_CONT : ROFF_IGN;
}

/* --- handling of numeric and conditional expressions -------------------- */

/*
 * Parse a single signed integer number.  Stop at the first non-digit.
 * If there is at least one digit, return success and advance the
 * parse point, else return failure and let the parse point unchanged.
 * Ignore overflows, treat them just like the C language.
 */
static int
roff_getnum(const char *v, int *pos, int *res, int flags)
{
	int	 myres, scaled, n, p;

	if (NULL == res)
		res = &myres;

	p = *pos;
	n = v[p] == '-';
	if (n || v[p] == '+')
		p++;

	if (flags & ROFFNUM_WHITE)
		while (isspace((unsigned char)v[p]))
			p++;

	for (*res = 0; isdigit((unsigned char)v[p]); p++)
		*res = 10 * *res + v[p] - '0';
	if (p == *pos + n)
		return 0;

	if (n)
		*res = -*res;

	/* Each number may be followed by one optional scaling unit. */

	switch (v[p]) {
	case 'f':
		scaled = *res * 65536;
		break;
	case 'i':
		scaled = *res * 240;
		break;
	case 'c':
		scaled = *res * 240 / 2.54;
		break;
	case 'v':
	case 'P':
		scaled = *res * 40;
		break;
	case 'm':
	case 'n':
		scaled = *res * 24;
		break;
	case 'p':
		scaled = *res * 10 / 3;
		break;
	case 'u':
		scaled = *res;
		break;
	case 'M':
		scaled = *res * 6 / 25;
		break;
	default:
		scaled = *res;
		p--;
		break;
	}
	if (flags & ROFFNUM_SCALE)
		*res = scaled;

	*pos = p + 1;
	return 1;
}

/*
 * Evaluate a string comparison condition.
 * The first character is the delimiter.
 * Succeed if the string up to its second occurrence
 * matches the string up to its third occurence.
 * Advance the cursor after the third occurrence
 * or lacking that, to the end of the line.
 */
static int
roff_evalstrcond(const char *v, int *pos)
{
	const char	*s1, *s2, *s3;
	int		 match;

	match = 0;
	s1 = v + *pos;		/* initial delimiter */
	s2 = s1 + 1;		/* for scanning the first string */
	s3 = strchr(s2, *s1);	/* for scanning the second string */

	if (NULL == s3)		/* found no middle delimiter */
		goto out;

	while ('\0' != *++s3) {
		if (*s2 != *s3) {  /* mismatch */
			s3 = strchr(s3, *s1);
			break;
		}
		if (*s3 == *s1) {  /* found the final delimiter */
			match = 1;
			break;
		}
		s2++;
	}

out:
	if (NULL == s3)
		s3 = strchr(s2, '\0');
	else if (*s3 != '\0')
		s3++;
	*pos = s3 - v;
	return match;
}

/*
 * Evaluate an optionally negated single character, numerical,
 * or string condition.
 */
static int
roff_evalcond(struct roff *r, int ln, char *v, int *pos)
{
	char	*cp, *name;
	size_t	 sz;
	int	 deftype, number, savepos, istrue, wanttrue;

	if ('!' == v[*pos]) {
		wanttrue = 0;
		(*pos)++;
	} else
		wanttrue = 1;

	switch (v[*pos]) {
	case '\0':
		return 0;
	case 'n':
	case 'o':
		(*pos)++;
		return wanttrue;
	case 'c':
	case 'e':
	case 't':
	case 'v':
		(*pos)++;
		return !wanttrue;
	case 'd':
	case 'r':
		cp = v + *pos + 1;
		while (*cp == ' ')
			cp++;
		name = cp;
		sz = roff_getname(r, &cp, ln, cp - v);
		if (sz == 0)
			istrue = 0;
		else if (v[*pos] == 'r')
			istrue = roff_hasregn(r, name, sz);
		else {
			deftype = ROFFDEF_ANY;
		        roff_getstrn(r, name, sz, &deftype);
			istrue = !!deftype;
		}
		*pos = cp - v;
		return istrue == wanttrue;
	default:
		break;
	}

	savepos = *pos;
	if (roff_evalnum(r, ln, v, pos, &number, ROFFNUM_SCALE))
		return (number > 0) == wanttrue;
	else if (*pos == savepos)
		return roff_evalstrcond(v, pos) == wanttrue;
	else
		return 0;
}

static enum rofferr
roff_line_ignore(ROFF_ARGS)
{

	return ROFF_IGN;
}

static enum rofferr
roff_insec(ROFF_ARGS)
{

	mandoc_msg(MANDOCERR_REQ_INSEC, r->parse,
	    ln, ppos, roff_name[tok]);
	return ROFF_IGN;
}

static enum rofferr
roff_unsupp(ROFF_ARGS)
{

	mandoc_msg(MANDOCERR_REQ_UNSUPP, r->parse,
	    ln, ppos, roff_name[tok]);
	return ROFF_IGN;
}

static enum rofferr
roff_cond(ROFF_ARGS)
{

	roffnode_push(r, tok, NULL, ln, ppos);

	/*
	 * An `.el' has no conditional body: it will consume the value
	 * of the current rstack entry set in prior `ie' calls or
	 * defaults to DENY.
	 *
	 * If we're not an `el', however, then evaluate the conditional.
	 */

	r->last->rule = tok == ROFF_el ?
	    (r->rstackpos < 0 ? 0 : r->rstack[r->rstackpos--]) :
	    roff_evalcond(r, ln, buf->buf, &pos);

	/*
	 * An if-else will put the NEGATION of the current evaluated
	 * conditional into the stack of rules.
	 */

	if (tok == ROFF_ie) {
		if (r->rstackpos + 1 == r->rstacksz) {
			r->rstacksz += 16;
			r->rstack = mandoc_reallocarray(r->rstack,
			    r->rstacksz, sizeof(int));
		}
		r->rstack[++r->rstackpos] = !r->last->rule;
	}

	/* If the parent has false as its rule, then so do we. */

	if (r->last->parent && !r->last->parent->rule)
		r->last->rule = 0;

	/*
	 * Determine scope.
	 * If there is nothing on the line after the conditional,
	 * not even whitespace, use next-line scope.
	 */

	if (buf->buf[pos] == '\0') {
		r->last->endspan = 2;
		goto out;
	}

	while (buf->buf[pos] == ' ')
		pos++;

	/* An opening brace requests multiline scope. */

	if (buf->buf[pos] == '\\' && buf->buf[pos + 1] == '{') {
		r->last->endspan = -1;
		pos += 2;
		while (buf->buf[pos] == ' ')
			pos++;
		goto out;
	}

	/*
	 * Anything else following the conditional causes
	 * single-line scope.  Warn if the scope contains
	 * nothing but trailing whitespace.
	 */

	if (buf->buf[pos] == '\0')
		mandoc_msg(MANDOCERR_COND_EMPTY, r->parse,
		    ln, ppos, roff_name[tok]);

	r->last->endspan = 1;

out:
	*offs = pos;
	return ROFF_RERUN;
}

static enum rofferr
roff_ds(ROFF_ARGS)
{
	char		*string;
	const char	*name;
	size_t		 namesz;

	/* Ignore groff compatibility mode for now. */

	if (tok == ROFF_ds1)
		tok = ROFF_ds;
	else if (tok == ROFF_as1)
		tok = ROFF_as;

	/*
	 * The first word is the name of the string.
	 * If it is empty or terminated by an escape sequence,
	 * abort the `ds' request without defining anything.
	 */

	name = string = buf->buf + pos;
	if (*name == '\0')
		return ROFF_IGN;

	namesz = roff_getname(r, &string, ln, pos);
	if (name[namesz] == '\\')
		return ROFF_IGN;

	/* Read past the initial double-quote, if any. */
	if (*string == '"')
		string++;

	/* The rest is the value. */
	roff_setstrn(&r->strtab, name, namesz, string, strlen(string),
	    ROFF_as == tok);
	roff_setstrn(&r->rentab, name, namesz, NULL, 0, 0);
	return ROFF_IGN;
}

/*
 * Parse a single operator, one or two characters long.
 * If the operator is recognized, return success and advance the
 * parse point, else return failure and let the parse point unchanged.
 */
static int
roff_getop(const char *v, int *pos, char *res)
{

	*res = v[*pos];

	switch (*res) {
	case '+':
	case '-':
	case '*':
	case '/':
	case '%':
	case '&':
	case ':':
		break;
	case '<':
		switch (v[*pos + 1]) {
		case '=':
			*res = 'l';
			(*pos)++;
			break;
		case '>':
			*res = '!';
			(*pos)++;
			break;
		case '?':
			*res = 'i';
			(*pos)++;
			break;
		default:
			break;
		}
		break;
	case '>':
		switch (v[*pos + 1]) {
		case '=':
			*res = 'g';
			(*pos)++;
			break;
		case '?':
			*res = 'a';
			(*pos)++;
			break;
		default:
			break;
		}
		break;
	case '=':
		if ('=' == v[*pos + 1])
			(*pos)++;
		break;
	default:
		return 0;
	}
	(*pos)++;

	return *res;
}

/*
 * Evaluate either a parenthesized numeric expression
 * or a single signed integer number.
 */
static int
roff_evalpar(struct roff *r, int ln,
	const char *v, int *pos, int *res, int flags)
{

	if ('(' != v[*pos])
		return roff_getnum(v, pos, res, flags);

	(*pos)++;
	if ( ! roff_evalnum(r, ln, v, pos, res, flags | ROFFNUM_WHITE))
		return 0;

	/*
	 * Omission of the closing parenthesis
	 * is an error in validation mode,
	 * but ignored in evaluation mode.
	 */

	if (')' == v[*pos])
		(*pos)++;
	else if (NULL == res)
		return 0;

	return 1;
}

/*
 * Evaluate a complete numeric expression.
 * Proceed left to right, there is no concept of precedence.
 */
static int
roff_evalnum(struct roff *r, int ln, const char *v,
	int *pos, int *res, int flags)
{
	int		 mypos, operand2;
	char		 operator;

	if (NULL == pos) {
		mypos = 0;
		pos = &mypos;
	}

	if (flags & ROFFNUM_WHITE)
		while (isspace((unsigned char)v[*pos]))
			(*pos)++;

	if ( ! roff_evalpar(r, ln, v, pos, res, flags))
		return 0;

	while (1) {
		if (flags & ROFFNUM_WHITE)
			while (isspace((unsigned char)v[*pos]))
				(*pos)++;

		if ( ! roff_getop(v, pos, &operator))
			break;

		if (flags & ROFFNUM_WHITE)
			while (isspace((unsigned char)v[*pos]))
				(*pos)++;

		if ( ! roff_evalpar(r, ln, v, pos, &operand2, flags))
			return 0;

		if (flags & ROFFNUM_WHITE)
			while (isspace((unsigned char)v[*pos]))
				(*pos)++;

		if (NULL == res)
			continue;

		switch (operator) {
		case '+':
			*res += operand2;
			break;
		case '-':
			*res -= operand2;
			break;
		case '*':
			*res *= operand2;
			break;
		case '/':
			if (operand2 == 0) {
				mandoc_msg(MANDOCERR_DIVZERO,
					r->parse, ln, *pos, v);
				*res = 0;
				break;
			}
			*res /= operand2;
			break;
		case '%':
			if (operand2 == 0) {
				mandoc_msg(MANDOCERR_DIVZERO,
					r->parse, ln, *pos, v);
				*res = 0;
				break;
			}
			*res %= operand2;
			break;
		case '<':
			*res = *res < operand2;
			break;
		case '>':
			*res = *res > operand2;
			break;
		case 'l':
			*res = *res <= operand2;
			break;
		case 'g':
			*res = *res >= operand2;
			break;
		case '=':
			*res = *res == operand2;
			break;
		case '!':
			*res = *res != operand2;
			break;
		case '&':
			*res = *res && operand2;
			break;
		case ':':
			*res = *res || operand2;
			break;
		case 'i':
			if (operand2 < *res)
				*res = operand2;
			break;
		case 'a':
			if (operand2 > *res)
				*res = operand2;
			break;
		default:
			abort();
		}
	}
	return 1;
}

/* --- register management ------------------------------------------------ */

void
roff_setreg(struct roff *r, const char *name, int val, char sign)
{
	roff_setregn(r, name, strlen(name), val, sign, INT_MIN);
}

static void
roff_setregn(struct roff *r, const char *name, size_t len,
    int val, char sign, int step)
{
	struct roffreg	*reg;

	/* Search for an existing register with the same name. */
	reg = r->regtab;

	while (reg != NULL && (reg->key.sz != len ||
	    strncmp(reg->key.p, name, len) != 0))
		reg = reg->next;

	if (NULL == reg) {
		/* Create a new register. */
		reg = mandoc_malloc(sizeof(struct roffreg));
		reg->key.p = mandoc_strndup(name, len);
		reg->key.sz = len;
		reg->val = 0;
		reg->step = 0;
		reg->next = r->regtab;
		r->regtab = reg;
	}

	if ('+' == sign)
		reg->val += val;
	else if ('-' == sign)
		reg->val -= val;
	else
		reg->val = val;
	if (step != INT_MIN)
		reg->step = step;
}

/*
 * Handle some predefined read-only number registers.
 * For now, return -1 if the requested register is not predefined;
 * in case a predefined read-only register having the value -1
 * were to turn up, another special value would have to be chosen.
 */
static int
roff_getregro(const struct roff *r, const char *name)
{

	switch (*name) {
	case '$':  /* Number of arguments of the last macro evaluated. */
		return r->argc;
	case 'A':  /* ASCII approximation mode is always off. */
		return 0;
	case 'g':  /* Groff compatibility mode is always on. */
		return 1;
	case 'H':  /* Fixed horizontal resolution. */
		return 24;
	case 'j':  /* Always adjust left margin only. */
		return 0;
	case 'T':  /* Some output device is always defined. */
		return 1;
	case 'V':  /* Fixed vertical resolution. */
		return 40;
	default:
		return -1;
	}
}

int
roff_getreg(struct roff *r, const char *name)
{
	return roff_getregn(r, name, strlen(name), '\0');
}

static int
roff_getregn(struct roff *r, const char *name, size_t len, char sign)
{
	struct roffreg	*reg;
	int		 val;

	if ('.' == name[0] && 2 == len) {
		val = roff_getregro(r, name + 1);
		if (-1 != val)
			return val;
	}

	for (reg = r->regtab; reg; reg = reg->next) {
		if (len == reg->key.sz &&
		    0 == strncmp(name, reg->key.p, len)) {
			switch (sign) {
			case '+':
				reg->val += reg->step;
				break;
			case '-':
				reg->val -= reg->step;
				break;
			default:
				break;
			}
			return reg->val;
		}
	}

	roff_setregn(r, name, len, 0, '\0', INT_MIN);
	return 0;
}

static int
roff_hasregn(const struct roff *r, const char *name, size_t len)
{
	struct roffreg	*reg;
	int		 val;

	if ('.' == name[0] && 2 == len) {
		val = roff_getregro(r, name + 1);
		if (-1 != val)
			return 1;
	}

	for (reg = r->regtab; reg; reg = reg->next)
		if (len == reg->key.sz &&
		    0 == strncmp(name, reg->key.p, len))
			return 1;

	return 0;
}

static void
roff_freereg(struct roffreg *reg)
{
	struct roffreg	*old_reg;

	while (NULL != reg) {
		free(reg->key.p);
		old_reg = reg;
		reg = reg->next;
		free(old_reg);
	}
}

static enum rofferr
roff_nr(ROFF_ARGS)
{
	char		*key, *val, *step;
	size_t		 keysz;
	int		 iv, is, len;
	char		 sign;

	key = val = buf->buf + pos;
	if (*key == '\0')
		return ROFF_IGN;

	keysz = roff_getname(r, &val, ln, pos);
	if (key[keysz] == '\\')
		return ROFF_IGN;

	sign = *val;
	if (sign == '+' || sign == '-')
		val++;

	len = 0;
	if (roff_evalnum(r, ln, val, &len, &iv, ROFFNUM_SCALE) == 0)
		return ROFF_IGN;

	step = val + len;
	while (isspace((unsigned char)*step))
		step++;
	if (roff_evalnum(r, ln, step, NULL, &is, 0) == 0)
		is = INT_MIN;

	roff_setregn(r, key, keysz, iv, sign, is);
	return ROFF_IGN;
}

static enum rofferr
roff_rr(ROFF_ARGS)
{
	struct roffreg	*reg, **prev;
	char		*name, *cp;
	size_t		 namesz;

	name = cp = buf->buf + pos;
	if (*name == '\0')
		return ROFF_IGN;
	namesz = roff_getname(r, &cp, ln, pos);
	name[namesz] = '\0';

	prev = &r->regtab;
	while (1) {
		reg = *prev;
		if (reg == NULL || !strcmp(name, reg->key.p))
			break;
		prev = &reg->next;
	}
	if (reg != NULL) {
		*prev = reg->next;
		free(reg->key.p);
		free(reg);
	}
	return ROFF_IGN;
}

/* --- handler functions for roff requests -------------------------------- */

static enum rofferr
roff_rm(ROFF_ARGS)
{
	const char	 *name;
	char		 *cp;
	size_t		  namesz;

	cp = buf->buf + pos;
	while (*cp != '\0') {
		name = cp;
		namesz = roff_getname(r, &cp, ln, (int)(cp - buf->buf));
		roff_setstrn(&r->strtab, name, namesz, NULL, 0, 0);
		roff_setstrn(&r->rentab, name, namesz, NULL, 0, 0);
		if (name[namesz] == '\\')
			break;
	}
	return ROFF_IGN;
}

static enum rofferr
roff_it(ROFF_ARGS)
{
	int		 iv;

	/* Parse the number of lines. */

	if ( ! roff_evalnum(r, ln, buf->buf, &pos, &iv, 0)) {
		mandoc_msg(MANDOCERR_IT_NONUM, r->parse,
		    ln, ppos, buf->buf + 1);
		return ROFF_IGN;
	}

	while (isspace((unsigned char)buf->buf[pos]))
		pos++;

	/*
	 * Arm the input line trap.
	 * Special-casing "an-trap" is an ugly workaround to cope
	 * with DocBook stupidly fiddling with man(7) internals.
	 */

	roffit_lines = iv;
	roffit_macro = mandoc_strdup(iv != 1 ||
	    strcmp(buf->buf + pos, "an-trap") ?
	    buf->buf + pos : "br");
	return ROFF_IGN;
}

static enum rofferr
roff_Dd(ROFF_ARGS)
{
	int		 mask;
	enum roff_tok	 t, te;

	switch (tok) {
	case ROFF_Dd:
		tok = MDOC_Dd;
		te = MDOC_MAX;
		if (r->format == 0)
			r->format = MPARSE_MDOC;
		mask = MPARSE_MDOC | MPARSE_QUICK;
		break;
	case ROFF_TH:
		tok = MAN_TH;
		te = MAN_MAX;
		if (r->format == 0)
			r->format = MPARSE_MAN;
		mask = MPARSE_QUICK;
		break;
	default:
		abort();
	}
	if ((r->options & mask) == 0)
		for (t = tok; t < te; t++)
			roff_setstr(r, roff_name[t], NULL, 0);
	return ROFF_CONT;
}

static enum rofferr
roff_TE(ROFF_ARGS)
{
	if (r->tbl == NULL) {
		mandoc_msg(MANDOCERR_BLK_NOTOPEN, r->parse,
		    ln, ppos, "TE");
		return ROFF_IGN;
	}
	if (tbl_end(r->tbl) == 0) {
		r->tbl = NULL;
		free(buf->buf);
		buf->buf = mandoc_strdup(".sp");
		buf->sz = 4;
		*offs = 0;
		return ROFF_REPARSE;
	}
	r->tbl = NULL;
	return ROFF_IGN;
}

static enum rofferr
roff_T_(ROFF_ARGS)
{

	if (NULL == r->tbl)
		mandoc_msg(MANDOCERR_BLK_NOTOPEN, r->parse,
		    ln, ppos, "T&");
	else
		tbl_restart(ln, ppos, r->tbl);

	return ROFF_IGN;
}

/*
 * Handle in-line equation delimiters.
 */
static enum rofferr
roff_eqndelim(struct roff *r, struct buf *buf, int pos)
{
	char		*cp1, *cp2;
	const char	*bef_pr, *bef_nl, *mac, *aft_nl, *aft_pr;

	/*
	 * Outside equations, look for an opening delimiter.
	 * If we are inside an equation, we already know it is
	 * in-line, or this function wouldn't have been called;
	 * so look for a closing delimiter.
	 */

	cp1 = buf->buf + pos;
	cp2 = strchr(cp1, r->eqn == NULL ?
	    r->last_eqn->odelim : r->last_eqn->cdelim);
	if (cp2 == NULL)
		return ROFF_CONT;

	*cp2++ = '\0';
	bef_pr = bef_nl = aft_nl = aft_pr = "";

	/* Handle preceding text, protecting whitespace. */

	if (*buf->buf != '\0') {
		if (r->eqn == NULL)
			bef_pr = "\\&";
		bef_nl = "\n";
	}

	/*
	 * Prepare replacing the delimiter with an equation macro
	 * and drop leading white space from the equation.
	 */

	if (r->eqn == NULL) {
		while (*cp2 == ' ')
			cp2++;
		mac = ".EQ";
	} else
		mac = ".EN";

	/* Handle following text, protecting whitespace. */

	if (*cp2 != '\0') {
		aft_nl = "\n";
		if (r->eqn != NULL)
			aft_pr = "\\&";
	}

	/* Do the actual replacement. */

	buf->sz = mandoc_asprintf(&cp1, "%s%s%s%s%s%s%s", buf->buf,
	    bef_pr, bef_nl, mac, aft_nl, aft_pr, cp2) + 1;
	free(buf->buf);
	buf->buf = cp1;

	/* Toggle the in-line state of the eqn subsystem. */

	r->eqn_inline = r->eqn == NULL;
	return ROFF_REPARSE;
}

static enum rofferr
roff_EQ(ROFF_ARGS)
{
	struct roff_node	*n;

	if (r->man->macroset == MACROSET_MAN)
		man_breakscope(r->man, ROFF_EQ);
	n = roff_node_alloc(r->man, ln, ppos, ROFFT_EQN, TOKEN_NONE);
	if (ln > r->man->last->line)
		n->flags |= NODE_LINE;
	n->eqn = mandoc_calloc(1, sizeof(*n->eqn));
	n->eqn->expectargs = UINT_MAX;
	roff_node_append(r->man, n);
	r->man->next = ROFF_NEXT_SIBLING;

	assert(r->eqn == NULL);
	if (r->last_eqn == NULL)
		r->last_eqn = eqn_alloc(r->parse);
	else
		eqn_reset(r->last_eqn);
	r->eqn = r->last_eqn;
	r->eqn->node = n;

	if (buf->buf[pos] != '\0')
		mandoc_vmsg(MANDOCERR_ARG_SKIP, r->parse, ln, pos,
		    ".EQ %s", buf->buf + pos);

	return ROFF_IGN;
}

static enum rofferr
roff_EN(ROFF_ARGS)
{
	if (r->eqn != NULL) {
		eqn_parse(r->eqn);
		r->eqn = NULL;
	} else
		mandoc_msg(MANDOCERR_BLK_NOTOPEN, r->parse, ln, ppos, "EN");
	if (buf->buf[pos] != '\0')
		mandoc_vmsg(MANDOCERR_ARG_SKIP, r->parse, ln, pos,
		    "EN %s", buf->buf + pos);
	return ROFF_IGN;
}

static enum rofferr
roff_TS(ROFF_ARGS)
{
	if (r->tbl != NULL) {
		mandoc_msg(MANDOCERR_BLK_BROKEN, r->parse,
		    ln, ppos, "TS breaks TS");
		tbl_end(r->tbl);
	}
	r->tbl = tbl_alloc(ppos, ln, r->parse);
	if (r->last_tbl)
		r->last_tbl->next = r->tbl;
	else
		r->first_tbl = r->tbl;
	r->last_tbl = r->tbl;
	return ROFF_IGN;
}

static enum rofferr
roff_onearg(ROFF_ARGS)
{
	struct roff_node	*n;
	char			*cp;
	int			 npos;

	if (r->man->flags & (MAN_BLINE | MAN_ELINE) &&
	    (tok == ROFF_ce || tok == ROFF_rj || tok == ROFF_sp ||
	     tok == ROFF_ti))
		man_breakscope(r->man, tok);

	if (roffce_node != NULL && (tok == ROFF_ce || tok == ROFF_rj)) {
		r->man->last = roffce_node;
		r->man->next = ROFF_NEXT_SIBLING;
	}

	roff_elem_alloc(r->man, ln, ppos, tok);
	n = r->man->last;

	cp = buf->buf + pos;
	if (*cp != '\0') {
		while (*cp != '\0' && *cp != ' ')
			cp++;
		while (*cp == ' ')
			*cp++ = '\0';
		if (*cp != '\0')
			mandoc_vmsg(MANDOCERR_ARG_EXCESS,
			    r->parse, ln, cp - buf->buf,
			    "%s ... %s", roff_name[tok], cp);
		roff_word_alloc(r->man, ln, pos, buf->buf + pos);
	}

	if (tok == ROFF_ce || tok == ROFF_rj) {
		if (r->man->last->type == ROFFT_ELEM) {
			roff_word_alloc(r->man, ln, pos, "1");
			r->man->last->flags |= NODE_NOSRC;
		}
		npos = 0;
		if (roff_evalnum(r, ln, r->man->last->string, &npos,
		    &roffce_lines, 0) == 0) {
			mandoc_vmsg(MANDOCERR_CE_NONUM,
			    r->parse, ln, pos, "ce %s", buf->buf + pos);
			roffce_lines = 1;
		}
		if (roffce_lines < 1) {
			r->man->last = r->man->last->parent;
			roffce_node = NULL;
			roffce_lines = 0;
		} else
			roffce_node = r->man->last->parent;
	} else {
		n->flags |= NODE_VALID | NODE_ENDED;
		r->man->last = n;
	}
	n->flags |= NODE_LINE;
	r->man->next = ROFF_NEXT_SIBLING;
	return ROFF_IGN;
}

static enum rofferr
roff_manyarg(ROFF_ARGS)
{
	struct roff_node	*n;
	char			*sp, *ep;

	roff_elem_alloc(r->man, ln, ppos, tok);
	n = r->man->last;

	for (sp = ep = buf->buf + pos; *sp != '\0'; sp = ep) {
		while (*ep != '\0' && *ep != ' ')
			ep++;
		while (*ep == ' ')
			*ep++ = '\0';
		roff_word_alloc(r->man, ln, sp - buf->buf, sp);
	}

	n->flags |= NODE_LINE | NODE_VALID | NODE_ENDED;
	r->man->last = n;
	r->man->next = ROFF_NEXT_SIBLING;
	return ROFF_IGN;
}

static enum rofferr
roff_als(ROFF_ARGS)
{
	char		*oldn, *newn, *end, *value;
	size_t		 oldsz, newsz, valsz;

	newn = oldn = buf->buf + pos;
	if (*newn == '\0')
		return ROFF_IGN;

	newsz = roff_getname(r, &oldn, ln, pos);
	if (newn[newsz] == '\\' || *oldn == '\0')
		return ROFF_IGN;

	end = oldn;
	oldsz = roff_getname(r, &end, ln, oldn - buf->buf);
	if (oldsz == 0)
		return ROFF_IGN;

	valsz = mandoc_asprintf(&value, ".%.*s \\$*\\\"\n",
	    (int)oldsz, oldn);
	roff_setstrn(&r->strtab, newn, newsz, value, valsz, 0);
	roff_setstrn(&r->rentab, newn, newsz, NULL, 0, 0);
	free(value);
	return ROFF_IGN;
}

static enum rofferr
roff_br(ROFF_ARGS)
{
	if (r->man->flags & (MAN_BLINE | MAN_ELINE))
		man_breakscope(r->man, ROFF_br);
	roff_elem_alloc(r->man, ln, ppos, ROFF_br);
	if (buf->buf[pos] != '\0')
		mandoc_vmsg(MANDOCERR_ARG_SKIP, r->parse, ln, pos,
		    "%s %s", roff_name[tok], buf->buf + pos);
	r->man->last->flags |= NODE_LINE | NODE_VALID | NODE_ENDED;
	r->man->next = ROFF_NEXT_SIBLING;
	return ROFF_IGN;
}

static enum rofferr
roff_cc(ROFF_ARGS)
{
	const char	*p;

	p = buf->buf + pos;

	if (*p == '\0' || (r->control = *p++) == '.')
		r->control = '\0';

	if (*p != '\0')
		mandoc_vmsg(MANDOCERR_ARG_EXCESS, r->parse,
		    ln, p - buf->buf, "cc ... %s", p);

	return ROFF_IGN;
}

static enum rofferr
roff_ec(ROFF_ARGS)
{
	const char	*p;

	p = buf->buf + pos;
	if (*p == '\0')
		r->escape = '\\';
	else {
		r->escape = *p;
		if (*++p != '\0')
			mandoc_vmsg(MANDOCERR_ARG_EXCESS, r->parse,
			    ln, p - buf->buf, "ec ... %s", p);
	}
	return ROFF_IGN;
}

static enum rofferr
roff_eo(ROFF_ARGS)
{
	r->escape = '\0';
	if (buf->buf[pos] != '\0')
		mandoc_vmsg(MANDOCERR_ARG_SKIP, r->parse,
		    ln, pos, "eo %s", buf->buf + pos);
	return ROFF_IGN;
}

static enum rofferr
roff_tr(ROFF_ARGS)
{
	const char	*p, *first, *second;
	size_t		 fsz, ssz;
	enum mandoc_esc	 esc;

	p = buf->buf + pos;

	if (*p == '\0') {
		mandoc_msg(MANDOCERR_REQ_EMPTY, r->parse, ln, ppos, "tr");
		return ROFF_IGN;
	}

	while (*p != '\0') {
		fsz = ssz = 1;

		first = p++;
		if (*first == '\\') {
			esc = mandoc_escape(&p, NULL, NULL);
			if (esc == ESCAPE_ERROR) {
				mandoc_msg(MANDOCERR_ESC_BAD, r->parse,
				    ln, (int)(p - buf->buf), first);
				return ROFF_IGN;
			}
			fsz = (size_t)(p - first);
		}

		second = p++;
		if (*second == '\\') {
			esc = mandoc_escape(&p, NULL, NULL);
			if (esc == ESCAPE_ERROR) {
				mandoc_msg(MANDOCERR_ESC_BAD, r->parse,
				    ln, (int)(p - buf->buf), second);
				return ROFF_IGN;
			}
			ssz = (size_t)(p - second);
		} else if (*second == '\0') {
			mandoc_vmsg(MANDOCERR_TR_ODD, r->parse,
			    ln, first - buf->buf, "tr %s", first);
			second = " ";
			p--;
		}

		if (fsz > 1) {
			roff_setstrn(&r->xmbtab, first, fsz,
			    second, ssz, 0);
			continue;
		}

		if (r->xtab == NULL)
			r->xtab = mandoc_calloc(128,
			    sizeof(struct roffstr));

		free(r->xtab[(int)*first].p);
		r->xtab[(int)*first].p = mandoc_strndup(second, ssz);
		r->xtab[(int)*first].sz = ssz;
	}

	return ROFF_IGN;
}

static enum rofferr
roff_rn(ROFF_ARGS)
{
	const char	*value;
	char		*oldn, *newn, *end;
	size_t		 oldsz, newsz;
	int		 deftype;

	oldn = newn = buf->buf + pos;
	if (*oldn == '\0')
		return ROFF_IGN;

	oldsz = roff_getname(r, &newn, ln, pos);
	if (oldn[oldsz] == '\\' || *newn == '\0')
		return ROFF_IGN;

	end = newn;
	newsz = roff_getname(r, &end, ln, newn - buf->buf);
	if (newsz == 0)
		return ROFF_IGN;

	deftype = ROFFDEF_ANY;
	value = roff_getstrn(r, oldn, oldsz, &deftype);
	switch (deftype) {
	case ROFFDEF_USER:
		roff_setstrn(&r->strtab, newn, newsz, value, strlen(value), 0);
		roff_setstrn(&r->strtab, oldn, oldsz, NULL, 0, 0);
		roff_setstrn(&r->rentab, newn, newsz, NULL, 0, 0);
		break;
	case ROFFDEF_PRE:
		roff_setstrn(&r->strtab, newn, newsz, value, strlen(value), 0);
		roff_setstrn(&r->rentab, newn, newsz, NULL, 0, 0);
		break;
	case ROFFDEF_REN:
		roff_setstrn(&r->rentab, newn, newsz, value, strlen(value), 0);
		roff_setstrn(&r->rentab, oldn, oldsz, NULL, 0, 0);
		roff_setstrn(&r->strtab, newn, newsz, NULL, 0, 0);
		break;
	case ROFFDEF_STD:
		roff_setstrn(&r->rentab, newn, newsz, oldn, oldsz, 0);
		roff_setstrn(&r->strtab, newn, newsz, NULL, 0, 0);
		break;
	default:
		roff_setstrn(&r->strtab, newn, newsz, NULL, 0, 0);
		roff_setstrn(&r->rentab, newn, newsz, NULL, 0, 0);
		break;
	}
	return ROFF_IGN;
}

static enum rofferr
roff_so(ROFF_ARGS)
{
	char *name, *cp;

	name = buf->buf + pos;
	mandoc_vmsg(MANDOCERR_SO, r->parse, ln, ppos, "so %s", name);

	/*
	 * Handle `so'.  Be EXTREMELY careful, as we shouldn't be
	 * opening anything that's not in our cwd or anything beneath
	 * it.  Thus, explicitly disallow traversing up the file-system
	 * or using absolute paths.
	 */

	if (*name == '/' || strstr(name, "../") || strstr(name, "/..")) {
		mandoc_vmsg(MANDOCERR_SO_PATH, r->parse, ln, ppos,
		    ".so %s", name);
		buf->sz = mandoc_asprintf(&cp,
		    ".sp\nSee the file %s.\n.sp", name) + 1;
		free(buf->buf);
		buf->buf = cp;
		*offs = 0;
		return ROFF_REPARSE;
	}

	*offs = pos;
	return ROFF_SO;
}

/* --- user defined strings and macros ------------------------------------ */

static enum rofferr
roff_userdef(ROFF_ARGS)
{
	const char	 *arg[16], *ap;
	char		 *cp, *n1, *n2;
	int		  expand_count, i, ib, ie;
	size_t		  asz, rsz;

	/*
	 * Collect pointers to macro argument strings
	 * and NUL-terminate them.
	 */

	r->argc = 0;
	cp = buf->buf + pos;
	for (i = 0; i < 16; i++) {
		if (*cp == '\0')
			arg[i] = "";
		else {
			arg[i] = mandoc_getarg(r->parse, &cp, ln, &pos);
			r->argc = i + 1;
		}
	}

	/*
	 * Expand macro arguments.
	 */

	buf->sz = strlen(r->current_string) + 1;
	n1 = n2 = cp = mandoc_malloc(buf->sz);
	memcpy(n1, r->current_string, buf->sz);
	expand_count = 0;
	while (*cp != '\0') {

		/* Scan ahead for the next argument invocation. */

		if (*cp++ != '\\')
			continue;
		if (*cp++ != '$')
			continue;
		if (*cp == '*') {  /* \\$* inserts all arguments */
			ib = 0;
			ie = r->argc - 1;
		} else {  /* \\$1 .. \\$9 insert one argument */
			ib = ie = *cp - '1';
			if (ib < 0 || ib > 8)
				continue;
		}
		cp -= 2;

		/*
		 * Prevent infinite recursion.
		 */

		if (cp >= n2)
			expand_count = 1;
		else if (++expand_count > EXPAND_LIMIT) {
			mandoc_msg(MANDOCERR_ROFFLOOP, r->parse,
			    ln, (int)(cp - n1), NULL);
			free(buf->buf);
			buf->buf = n1;
			*offs = 0;
			return ROFF_IGN;
		}

		/*
		 * Determine the size of the expanded argument,
		 * taking escaping of quotes into account.
		 */

		asz = ie > ib ? ie - ib : 0;  /* for blanks */
		for (i = ib; i <= ie; i++) {
			for (ap = arg[i]; *ap != '\0'; ap++) {
				asz++;
				if (*ap == '"')
					asz += 3;
			}
		}
		if (asz != 3) {

			/*
			 * Determine the size of the rest of the
			 * unexpanded macro, including the NUL.
			 */

			rsz = buf->sz - (cp - n1) - 3;

			/*
			 * When shrinking, move before
			 * releasing the storage.
			 */

			if (asz < 3)
				memmove(cp + asz, cp + 3, rsz);

			/*
			 * Resize the storage for the macro
			 * and readjust the parse pointer.
			 */

			buf->sz += asz - 3;
			n2 = mandoc_realloc(n1, buf->sz);
			cp = n2 + (cp - n1);
			n1 = n2;

			/*
			 * When growing, make room
			 * for the expanded argument.
			 */

			if (asz > 3)
				memmove(cp + asz, cp + 3, rsz);
		}

		/* Copy the expanded argument, escaping quotes. */

		n2 = cp;
		for (i = ib; i <= ie; i++) {
			for (ap = arg[i]; *ap != '\0'; ap++) {
				if (*ap == '"') {
					memcpy(n2, "\\(dq", 4);
					n2 += 4;
				} else
					*n2++ = *ap;
			}
			if (i < ie)
				*n2++ = ' ';
		}
	}

	/*
	 * Replace the macro invocation
	 * by the expanded macro.
	 */

	free(buf->buf);
	buf->buf = n1;
	*offs = 0;

	return buf->sz > 1 && buf->buf[buf->sz - 2] == '\n' ?
	   ROFF_REPARSE : ROFF_APPEND;
}

/*
 * Calling a high-level macro that was renamed with .rn.
 * r->current_string has already been set up by roff_parse().
 */
static enum rofferr
roff_renamed(ROFF_ARGS)
{
	char	*nbuf;

	buf->sz = mandoc_asprintf(&nbuf, ".%s%s%s", r->current_string,
	    buf->buf[pos] == '\0' ? "" : " ", buf->buf + pos) + 1;
	free(buf->buf);
	buf->buf = nbuf;
	*offs = 0;
	return ROFF_CONT;
}

static size_t
roff_getname(struct roff *r, char **cpp, int ln, int pos)
{
	char	 *name, *cp;
	size_t	  namesz;

	name = *cpp;
	if ('\0' == *name)
		return 0;

	/* Read until end of name and terminate it with NUL. */
	for (cp = name; 1; cp++) {
		if ('\0' == *cp || ' ' == *cp) {
			namesz = cp - name;
			break;
		}
		if ('\\' != *cp)
			continue;
		namesz = cp - name;
		if ('{' == cp[1] || '}' == cp[1])
			break;
		cp++;
		if ('\\' == *cp)
			continue;
		mandoc_vmsg(MANDOCERR_NAMESC, r->parse, ln, pos,
		    "%.*s", (int)(cp - name + 1), name);
		mandoc_escape((const char **)&cp, NULL, NULL);
		break;
	}

	/* Read past spaces. */
	while (' ' == *cp)
		cp++;

	*cpp = cp;
	return namesz;
}

/*
 * Store *string into the user-defined string called *name.
 * To clear an existing entry, call with (*r, *name, NULL, 0).
 * append == 0: replace mode
 * append == 1: single-line append mode
 * append == 2: multiline append mode, append '\n' after each call
 */
static void
roff_setstr(struct roff *r, const char *name, const char *string,
	int append)
{
	size_t	 namesz;

	namesz = strlen(name);
	roff_setstrn(&r->strtab, name, namesz, string,
	    string ? strlen(string) : 0, append);
	roff_setstrn(&r->rentab, name, namesz, NULL, 0, 0);
}

static void
roff_setstrn(struct roffkv **r, const char *name, size_t namesz,
		const char *string, size_t stringsz, int append)
{
	struct roffkv	*n;
	char		*c;
	int		 i;
	size_t		 oldch, newch;

	/* Search for an existing string with the same name. */
	n = *r;

	while (n && (namesz != n->key.sz ||
			strncmp(n->key.p, name, namesz)))
		n = n->next;

	if (NULL == n) {
		/* Create a new string table entry. */
		n = mandoc_malloc(sizeof(struct roffkv));
		n->key.p = mandoc_strndup(name, namesz);
		n->key.sz = namesz;
		n->val.p = NULL;
		n->val.sz = 0;
		n->next = *r;
		*r = n;
	} else if (0 == append) {
		free(n->val.p);
		n->val.p = NULL;
		n->val.sz = 0;
	}

	if (NULL == string)
		return;

	/*
	 * One additional byte for the '\n' in multiline mode,
	 * and one for the terminating '\0'.
	 */
	newch = stringsz + (1 < append ? 2u : 1u);

	if (NULL == n->val.p) {
		n->val.p = mandoc_malloc(newch);
		*n->val.p = '\0';
		oldch = 0;
	} else {
		oldch = n->val.sz;
		n->val.p = mandoc_realloc(n->val.p, oldch + newch);
	}

	/* Skip existing content in the destination buffer. */
	c = n->val.p + (int)oldch;

	/* Append new content to the destination buffer. */
	i = 0;
	while (i < (int)stringsz) {
		/*
		 * Rudimentary roff copy mode:
		 * Handle escaped backslashes.
		 */
		if ('\\' == string[i] && '\\' == string[i + 1])
			i++;
		*c++ = string[i++];
	}

	/* Append terminating bytes. */
	if (1 < append)
		*c++ = '\n';

	*c = '\0';
	n->val.sz = (int)(c - n->val.p);
}

static const char *
roff_getstrn(struct roff *r, const char *name, size_t len,
    int *deftype)
{
	const struct roffkv	*n;
	int			 found, i;
	enum roff_tok		 tok;

	found = 0;
	for (n = r->strtab; n != NULL; n = n->next) {
		if (strncmp(name, n->key.p, len) != 0 ||
		    n->key.p[len] != '\0' || n->val.p == NULL)
			continue;
		if (*deftype & ROFFDEF_USER) {
			*deftype = ROFFDEF_USER;
			return n->val.p;
		} else {
			found = 1;
			break;
		}
	}
	for (n = r->rentab; n != NULL; n = n->next) {
		if (strncmp(name, n->key.p, len) != 0 ||
		    n->key.p[len] != '\0' || n->val.p == NULL)
			continue;
		if (*deftype & ROFFDEF_REN) {
			*deftype = ROFFDEF_REN;
			return n->val.p;
		} else {
			found = 1;
			break;
		}
	}
	for (i = 0; i < PREDEFS_MAX; i++) {
		if (strncmp(name, predefs[i].name, len) != 0 ||
		    predefs[i].name[len] != '\0')
			continue;
		if (*deftype & ROFFDEF_PRE) {
			*deftype = ROFFDEF_PRE;
			return predefs[i].str;
		} else {
			found = 1;
			break;
		}
	}
	if (r->man->macroset != MACROSET_MAN) {
		for (tok = MDOC_Dd; tok < MDOC_MAX; tok++) {
			if (strncmp(name, roff_name[tok], len) != 0 ||
			    roff_name[tok][len] != '\0')
				continue;
			if (*deftype & ROFFDEF_STD) {
				*deftype = ROFFDEF_STD;
				return NULL;
			} else {
				found = 1;
				break;
			}
		}
	}
	if (r->man->macroset != MACROSET_MDOC) {
		for (tok = MAN_TH; tok < MAN_MAX; tok++) {
			if (strncmp(name, roff_name[tok], len) != 0 ||
			    roff_name[tok][len] != '\0')
				continue;
			if (*deftype & ROFFDEF_STD) {
				*deftype = ROFFDEF_STD;
				return NULL;
			} else {
				found = 1;
				break;
			}
		}
	}

	if (found == 0 && *deftype != ROFFDEF_ANY) {
		if (*deftype & ROFFDEF_REN) {
			/*
			 * This might still be a request,
			 * so do not treat it as undefined yet.
			 */
			*deftype = ROFFDEF_UNDEF;
			return NULL;
		}

		/* Using an undefined string defines it to be empty. */

		roff_setstrn(&r->strtab, name, len, "", 0, 0);
		roff_setstrn(&r->rentab, name, len, NULL, 0, 0);
	}

	*deftype = 0;
	return NULL;
}

static void
roff_freestr(struct roffkv *r)
{
	struct roffkv	 *n, *nn;

	for (n = r; n; n = nn) {
		free(n->key.p);
		free(n->val.p);
		nn = n->next;
		free(n);
	}
}

/* --- accessors and utility functions ------------------------------------ */

/*
 * Duplicate an input string, making the appropriate character
 * conversations (as stipulated by `tr') along the way.
 * Returns a heap-allocated string with all the replacements made.
 */
char *
roff_strdup(const struct roff *r, const char *p)
{
	const struct roffkv *cp;
	char		*res;
	const char	*pp;
	size_t		 ssz, sz;
	enum mandoc_esc	 esc;

	if (NULL == r->xmbtab && NULL == r->xtab)
		return mandoc_strdup(p);
	else if ('\0' == *p)
		return mandoc_strdup("");

	/*
	 * Step through each character looking for term matches
	 * (remember that a `tr' can be invoked with an escape, which is
	 * a glyph but the escape is multi-character).
	 * We only do this if the character hash has been initialised
	 * and the string is >0 length.
	 */

	res = NULL;
	ssz = 0;

	while ('\0' != *p) {
		assert((unsigned int)*p < 128);
		if ('\\' != *p && r->xtab && r->xtab[(unsigned int)*p].p) {
			sz = r->xtab[(int)*p].sz;
			res = mandoc_realloc(res, ssz + sz + 1);
			memcpy(res + ssz, r->xtab[(int)*p].p, sz);
			ssz += sz;
			p++;
			continue;
		} else if ('\\' != *p) {
			res = mandoc_realloc(res, ssz + 2);
			res[ssz++] = *p++;
			continue;
		}

		/* Search for term matches. */
		for (cp = r->xmbtab; cp; cp = cp->next)
			if (0 == strncmp(p, cp->key.p, cp->key.sz))
				break;

		if (NULL != cp) {
			/*
			 * A match has been found.
			 * Append the match to the array and move
			 * forward by its keysize.
			 */
			res = mandoc_realloc(res,
			    ssz + cp->val.sz + 1);
			memcpy(res + ssz, cp->val.p, cp->val.sz);
			ssz += cp->val.sz;
			p += (int)cp->key.sz;
			continue;
		}

		/*
		 * Handle escapes carefully: we need to copy
		 * over just the escape itself, or else we might
		 * do replacements within the escape itself.
		 * Make sure to pass along the bogus string.
		 */
		pp = p++;
		esc = mandoc_escape(&p, NULL, NULL);
		if (ESCAPE_ERROR == esc) {
			sz = strlen(pp);
			res = mandoc_realloc(res, ssz + sz + 1);
			memcpy(res + ssz, pp, sz);
			break;
		}
		/*
		 * We bail out on bad escapes.
		 * No need to warn: we already did so when
		 * roff_res() was called.
		 */
		sz = (int)(p - pp);
		res = mandoc_realloc(res, ssz + sz + 1);
		memcpy(res + ssz, pp, sz);
		ssz += sz;
	}

	res[(int)ssz] = '\0';
	return res;
}

int
roff_getformat(const struct roff *r)
{

	return r->format;
}

/*
 * Find out whether a line is a macro line or not.
 * If it is, adjust the current position and return one; if it isn't,
 * return zero and don't change the current position.
 * If the control character has been set with `.cc', then let that grain
 * precedence.
 * This is slighly contrary to groff, where using the non-breaking
 * control character when `cc' has been invoked will cause the
 * non-breaking macro contents to be printed verbatim.
 */
int
roff_getcontrol(const struct roff *r, const char *cp, int *ppos)
{
	int		pos;

	pos = *ppos;

	if (r->control != '\0' && cp[pos] == r->control)
		pos++;
	else if (r->control != '\0')
		return 0;
	else if ('\\' == cp[pos] && '.' == cp[pos + 1])
		pos += 2;
	else if ('.' == cp[pos] || '\'' == cp[pos])
		pos++;
	else
		return 0;

	while (' ' == cp[pos] || '\t' == cp[pos])
		pos++;

	*ppos = pos;
	return 1;
}
