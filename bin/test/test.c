/*	$OpenBSD: test.c,v 1.23 2025/03/24 20:15:08 millert Exp $	*/
/*	$NetBSD: test.c,v 1.15 1995/03/21 07:04:06 cgd Exp $	*/

/*
 * test(1); version 7-like  --  author Erik Baalbergen
 * modified by Eric Gisin to be used as built-in.
 * modified by Arnold Robbins to add SVR3 compatibility
 * (-x -c -b -p -u -g -k) plus Korn's -L -nt -ot -ef and new -S (socket).
 * modified by J.T. Conklin for NetBSD.
 *
 * This program is in the Public Domain.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

/* test(1) accepts the following grammar:
	oexpr	::= aexpr | aexpr "-o" oexpr ;
	aexpr	::= nexpr | nexpr "-a" aexpr ;
	nexpr	::= primary | "!" primary
	primary	::= unary-operator operand
		| operand binary-operator operand
		| operand
		| "(" oexpr ")"
		;
	unary-operator ::= "-r"|"-w"|"-x"|"-f"|"-d"|"-c"|"-b"|"-p"|
		"-u"|"-g"|"-k"|"-s"|"-t"|"-z"|"-n"|"-o"|"-O"|"-G"|"-L"|"-S";

	binary-operator ::= "="|"!="|"<"|">"|"-eq"|"-ne"|"-ge"|"-gt"|
			"-le"|"-lt"|"-nt"|"-ot"|"-ef";
	operand ::= <any legal UNIX file name>
*/

enum token {
	EOI,
	FILRD,
	FILWR,
	FILEX,
	FILEXIST,
	FILREG,
	FILDIR,
	FILCDEV,
	FILBDEV,
	FILFIFO,
	FILSOCK,
	FILSYM,
	FILGZ,
	FILTT,
	FILSUID,
	FILSGID,
	FILSTCK,
	FILNT,
	FILOT,
	FILEQ,
	FILUID,
	FILGID,
	STREZ,
	STRNZ,
	STREQ,
	STRNE,
	STRLT,
	STRGT,
	INTEQ,
	INTNE,
	INTGE,
	INTGT,
	INTLE,
	INTLT,
	UNOT,
	BAND,
	BOR,
	LPAREN,
	RPAREN,
	OPERAND
};

enum token_types {
	UNOP,
	BINOP,
	BUNOP,
	BBINOP,
	PAREN
};

struct t_op {
	const char *op_text;
	short op_num, op_type;
} const ops [] = {
	{"-r",	FILRD,	UNOP},
	{"-w",	FILWR,	UNOP},
	{"-x",	FILEX,	UNOP},
	{"-e",	FILEXIST,UNOP},
	{"-f",	FILREG,	UNOP},
	{"-d",	FILDIR,	UNOP},
	{"-c",	FILCDEV,UNOP},
	{"-b",	FILBDEV,UNOP},
	{"-p",	FILFIFO,UNOP},
	{"-u",	FILSUID,UNOP},
	{"-g",	FILSGID,UNOP},
	{"-k",	FILSTCK,UNOP},
	{"-s",	FILGZ,	UNOP},
	{"-t",	FILTT,	UNOP},
	{"-z",	STREZ,	UNOP},
	{"-n",	STRNZ,	UNOP},
	{"-h",	FILSYM,	UNOP},
	{"-O",	FILUID,	UNOP},
	{"-G",	FILGID,	UNOP},
	{"-L",	FILSYM,	UNOP},
	{"-S",	FILSOCK,UNOP},
	{"=",	STREQ,	BINOP},
	{"!=",	STRNE,	BINOP},
	{"<",	STRLT,	BINOP},
	{">",	STRGT,	BINOP},
	{"-eq",	INTEQ,	BINOP},
	{"-ne",	INTNE,	BINOP},
	{"-ge",	INTGE,	BINOP},
	{"-gt",	INTGT,	BINOP},
	{"-le",	INTLE,	BINOP},
	{"-lt",	INTLT,	BINOP},
	{"-nt",	FILNT,	BINOP},
	{"-ot",	FILOT,	BINOP},
	{"-ef",	FILEQ,	BINOP},
	{"!",	UNOT,	BUNOP},
	{"-a",	BAND,	BBINOP},
	{"-o",	BOR,	BBINOP},
	{"(",	LPAREN,	PAREN},
	{")",	RPAREN,	PAREN},
	{0,	0,	0}
};

char **t_wp;
struct t_op const *t_wp_op;

static enum token t_lex(char *);
static enum token_types t_lex_type(char *);
static int oexpr(enum token n);
static int aexpr(enum token n);
static int nexpr(enum token n);
static int binop(void);
static int primary(enum token n);
static const char *getnstr(const char *, int *, size_t *);
static int intcmp(const char *, const char *);
static int filstat(char *nm, enum token mode);
static int getn(const char *s);
static int newerf(const char *, const char *);
static int olderf(const char *, const char *);
static int equalf(const char *, const char *);
static __dead void syntax(const char *op, char *msg);

int
main(int argc, char *argv[])
{
	extern char *__progname;
	int	res;

	if (pledge("stdio rpath", NULL) == -1)
		err(2, "pledge");

	if (strcmp(__progname, "[") == 0) {
		if (strcmp(argv[--argc], "]"))
			errx(2, "missing ]");
		argv[argc] = NULL;
	}

	/* Implement special cases from POSIX.2, section 4.62.4 */
	switch (argc) {
	case 1:
		return 1;
	case 2:
		return (*argv[1] == '\0');
	case 3:
		if (argv[1][0] == '!' && argv[1][1] == '\0') {
			return !(*argv[2] == '\0');
		}
		break;
	case 4:
		if (argv[1][0] != '!' || argv[1][1] != '\0') {
			if (t_lex(argv[2]),
			    t_wp_op && t_wp_op->op_type == BINOP) {
				t_wp = &argv[1];
				return (binop() == 0);
			}
		}
		break;
	case 5:
		if (argv[1][0] == '!' && argv[1][1] == '\0') {
			if (t_lex(argv[3]),
			    t_wp_op && t_wp_op->op_type == BINOP) {
				t_wp = &argv[2];
				return !(binop() == 0);
			}
		}
		break;
	}

	t_wp = &argv[1];
	res = !oexpr(t_lex(*t_wp));

	if (*t_wp != NULL && *++t_wp != NULL)
		syntax(*t_wp, "unknown operand");

	return res;
}

static __dead void
syntax(const char *op, char *msg)
{
	if (op && *op)
		errx(2, "%s: %s", op, msg);
	else
		errx(2, "%s", msg);
}

static int
oexpr(enum token n)
{
	int res;

	res = aexpr(n);
	if (t_lex(*++t_wp) == BOR)
		return oexpr(t_lex(*++t_wp)) || res;
	t_wp--;
	return res;
}

static int
aexpr(enum token n)
{
	int res;

	res = nexpr(n);
	if (t_lex(*++t_wp) == BAND)
		return aexpr(t_lex(*++t_wp)) && res;
	t_wp--;
	return res;
}

static int
nexpr(enum token n)
{
	if (n == UNOT)
		return !nexpr(t_lex(*++t_wp));
	return primary(n);
}

static int
primary(enum token n)
{
	int res;

	if (n == EOI)
		syntax(NULL, "argument expected");
	if (n == LPAREN) {
		res = oexpr(t_lex(*++t_wp));
		if (t_lex(*++t_wp) != RPAREN)
			syntax(NULL, "closing paren expected");
		return res;
	}
	/*
	 * We need this, if not binary operations with more than 4
	 * arguments will always fall into unary.
	 */
	if(t_lex_type(t_wp[1]) == BINOP) {
		t_lex(t_wp[1]);
		if (t_wp_op && t_wp_op->op_type == BINOP)
			return binop();
	}

	if (t_wp_op && t_wp_op->op_type == UNOP) {
		/* unary expression */
		if (*++t_wp == NULL)
			syntax(t_wp_op->op_text, "argument expected");
		switch (n) {
		case STREZ:
			return strlen(*t_wp) == 0;
		case STRNZ:
			return strlen(*t_wp) != 0;
		case FILTT:
			return isatty(getn(*t_wp));
		default:
			return filstat(*t_wp, n);
		}
	}

	return strlen(*t_wp) > 0;
}

static const char *
getnstr(const char *s, int *signum, size_t *len)
{
	const char *p, *start;

	/* skip leading whitespaces */
	p = s;
	while (isspace((unsigned char)*p))
		p++;

	/* accept optional sign */
	if (*p == '-') {
		*signum = -1;
		p++;
	} else {
		*signum = 1;
		if (*p == '+')
			p++;
	}

	/* skip leading zeros */
	while (*p == '0' && isdigit((unsigned char)p[1]))
		p++;

	/* turn 0 always positive */
	if (*p == '0')
		*signum = 1;

	start = p;
	while (isdigit((unsigned char)*p))
		p++;
	*len = p - start;

	/* allow trailing whitespaces */
	while (isspace((unsigned char)*p))
		p++;

	/* validate number */
	if (*p != '\0' || *start == '\0')
		errx(2, "%s: invalid", s);

	return start;
}

static int
intcmp(const char *opnd1, const char *opnd2)
{
	const char *p1, *p2;
	size_t len1, len2;
	int c, sig1, sig2;

	p1 = getnstr(opnd1, &sig1, &len1);
	p2 = getnstr(opnd2, &sig2, &len2);

	if (sig1 != sig2)
		c = sig1;
	else if (len1 != len2)
		c = (len1 < len2) ? -sig1 : sig1;
	else
		c = strncmp(p1, p2, len1) * sig1;

	return c;
}

static int
binop(void)
{
	const char *opnd1, *opnd2;
	struct t_op const *op;

	opnd1 = *t_wp;
	(void) t_lex(*++t_wp);
	op = t_wp_op;

	if ((opnd2 = *++t_wp) == NULL)
		syntax(op->op_text, "argument expected");

	switch (op->op_num) {
	case STREQ:
		return strcmp(opnd1, opnd2) == 0;
	case STRNE:
		return strcmp(opnd1, opnd2) != 0;
	case STRLT:
		return strcmp(opnd1, opnd2) < 0;
	case STRGT:
		return strcmp(opnd1, opnd2) > 0;
	case INTEQ:
		return intcmp(opnd1, opnd2) == 0;
	case INTNE:
		return intcmp(opnd1, opnd2) != 0;
	case INTGE:
		return intcmp(opnd1, opnd2) >= 0;
	case INTGT:
		return intcmp(opnd1, opnd2) > 0;
	case INTLE:
		return intcmp(opnd1, opnd2) <= 0;
	case INTLT:
		return intcmp(opnd1, opnd2) < 0;
	case FILNT:
		return newerf(opnd1, opnd2);
	case FILOT:
		return olderf(opnd1, opnd2);
	case FILEQ:
		return equalf(opnd1, opnd2);
	}

	syntax(op->op_text, "not a binary operator");
}

static enum token_types
t_lex_type(char *s)
{
	struct t_op const *op = ops;

	if (s == NULL)
		return -1;

	while (op->op_text) {
		if (strcmp(s, op->op_text) == 0)
			return op->op_type;
		op++;
	}
	return -1;
}

static int
filstat(char *nm, enum token mode)
{
	struct stat s;
	mode_t i;

	switch (mode) {
	case FILRD:
		return access(nm, R_OK) == 0;
	case FILWR:
		return access(nm, W_OK) == 0;
	case FILEX:
		return access(nm, X_OK) == 0;
	case FILEXIST:
		return access(nm, F_OK) == 0;
	default:
		break;
	}

	if (mode == FILSYM) {
		if (lstat(nm, &s) == 0) {
			i = S_IFLNK;
			goto filetype;
		}
		return 0;
	}

	if (stat(nm, &s) != 0)
		return 0;

	switch (mode) {
	case FILREG:
		i = S_IFREG;
		goto filetype;
	case FILDIR:
		i = S_IFDIR;
		goto filetype;
	case FILCDEV:
		i = S_IFCHR;
		goto filetype;
	case FILBDEV:
		i = S_IFBLK;
		goto filetype;
	case FILFIFO:
		i = S_IFIFO;
		goto filetype;
	case FILSOCK:
		i = S_IFSOCK;
		goto filetype;
	case FILSUID:
		i = S_ISUID;
		goto filebit;
	case FILSGID:
		i = S_ISGID;
		goto filebit;
	case FILSTCK:
		i = S_ISVTX;
		goto filebit;
	case FILGZ:
		return s.st_size > 0L;
	case FILUID:
		return s.st_uid == geteuid();
	case FILGID:
		return s.st_gid == getegid();
	default:
		return 1;
	}

filetype:
	return ((s.st_mode & S_IFMT) == i);

filebit:
	return ((s.st_mode & i) != 0);
}

static enum token
t_lex(char *s)
{
	struct t_op const *op = ops;

	if (s == 0) {
		t_wp_op = NULL;
		return EOI;
	}
	while (op->op_text) {
		if (strcmp(s, op->op_text) == 0) {
			t_wp_op = op;
			return op->op_num;
		}
		op++;
	}
	t_wp_op = NULL;
	return OPERAND;
}

/* atoi with error detection */
static int
getn(const char *s)
{
	char buf[32];
	const char *errstr, *p;
	size_t len;
	int r, sig;

	p = getnstr(s, &sig, &len);
	if (sig != 1)
		errstr = "too small";
	else if (len >= sizeof(buf))
		errstr = "too large";
	else {
		strlcpy(buf, p, sizeof(buf));
		buf[len] = '\0';
		r = strtonum(buf, 0, INT_MAX, &errstr);
	}

	if (errstr != NULL)
		errx(2, "%s: %s", s, errstr);

	return r;
}

static int
newerf(const char *f1, const char *f2)
{
	struct stat b1, b2;

	return (stat(f1, &b1) == 0 &&
	    stat(f2, &b2) == 0 &&
	    timespeccmp(&b1.st_mtim, &b2.st_mtim, >));
}

static int
olderf(const char *f1, const char *f2)
{
	struct stat b1, b2;

	return (stat(f1, &b1) == 0 &&
	    stat(f2, &b2) == 0 &&
	    timespeccmp(&b1.st_mtim, &b2.st_mtim, <));
}

static int
equalf(const char *f1, const char *f2)
{
	struct stat b1, b2;

	return (stat(f1, &b1) == 0 &&
	    stat(f2, &b2) == 0 &&
	    b1.st_dev == b2.st_dev &&
	    b1.st_ino == b2.st_ino);
}
