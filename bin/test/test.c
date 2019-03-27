/*	$NetBSD: test.c,v 1.21 1999/04/05 09:48:38 kleink Exp $	*/

/*-
 * test(1); version 7-like  --  author Erik Baalbergen
 * modified by Eric Gisin to be used as built-in.
 * modified by Arnold Robbins to add SVR3 compatibility
 * (-x -c -b -p -u -g -k) plus Korn's -L -nt -ot -ef and new -S (socket).
 * modified by J.T. Conklin for NetBSD.
 *
 * This program is in the Public Domain.
 */
/*
 * Important: This file is used both as a standalone program /bin/test and
 * as a builtin for /bin/sh (#define SHELL).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef SHELL
#define main testcmd
#include "bltin/bltin.h"
#else
#include <locale.h>

static void error(const char *, ...) __dead2 __printf0like(1, 2);

static void
error(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	verrx(2, msg, ap);
	/*NOTREACHED*/
	va_end(ap);
}
#endif

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

	binary-operator ::= "="|"!="|"-eq"|"-ne"|"-ge"|"-gt"|"-le"|"-lt"|
			"-nt"|"-ot"|"-ef";
	operand ::= <any legal UNIX file name>
*/

enum token_types {
	UNOP = 0x100,
	BINOP = 0x200,
	BUNOP = 0x300,
	BBINOP = 0x400,
	PAREN = 0x500
};

enum token {
	EOI,
	OPERAND,
	FILRD = UNOP + 1,
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
	STREZ,
	STRNZ,
	FILUID,
	FILGID,
	FILNT = BINOP + 1,
	FILOT,
	FILEQ,
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
	UNOT = BUNOP + 1,
	BAND = BBINOP + 1,
	BOR,
	LPAREN = PAREN + 1,
	RPAREN
};

#define TOKEN_TYPE(token) ((token) & 0xff00)

static const struct t_op {
	char op_text[2];
	short op_num;
} ops1[] = {
	{"=",	STREQ},
	{"<",	STRLT},
	{">",	STRGT},
	{"!",	UNOT},
	{"(",	LPAREN},
	{")",	RPAREN},
}, opsm1[] = {
	{"r",	FILRD},
	{"w",	FILWR},
	{"x",	FILEX},
	{"e",	FILEXIST},
	{"f",	FILREG},
	{"d",	FILDIR},
	{"c",	FILCDEV},
	{"b",	FILBDEV},
	{"p",	FILFIFO},
	{"u",	FILSUID},
	{"g",	FILSGID},
	{"k",	FILSTCK},
	{"s",	FILGZ},
	{"t",	FILTT},
	{"z",	STREZ},
	{"n",	STRNZ},
	{"h",	FILSYM},		/* for backwards compat */
	{"O",	FILUID},
	{"G",	FILGID},
	{"L",	FILSYM},
	{"S",	FILSOCK},
	{"a",	BAND},
	{"o",	BOR},
}, ops2[] = {
	{"==",	STREQ},
	{"!=",	STRNE},
}, opsm2[] = {
	{"eq",	INTEQ},
	{"ne",	INTNE},
	{"ge",	INTGE},
	{"gt",	INTGT},
	{"le",	INTLE},
	{"lt",	INTLT},
	{"nt",	FILNT},
	{"ot",	FILOT},
	{"ef",	FILEQ},
};

static int nargc;
static char **t_wp;
static int parenlevel;

static int	aexpr(enum token);
static int	binop(enum token);
static int	equalf(const char *, const char *);
static int	filstat(char *, enum token);
static int	getn(const char *);
static intmax_t	getq(const char *);
static int	intcmp(const char *, const char *);
static int	isunopoperand(void);
static int	islparenoperand(void);
static int	isrparenoperand(void);
static int	newerf(const char *, const char *);
static int	nexpr(enum token);
static int	oexpr(enum token);
static int	olderf(const char *, const char *);
static int	primary(enum token);
static void	syntax(const char *, const char *);
static enum	token t_lex(char *);

int
main(int argc, char **argv)
{
	int	res;
	char	*p;

	if ((p = strrchr(argv[0], '/')) == NULL)
		p = argv[0];
	else
		p++;
	if (strcmp(p, "[") == 0) {
		if (strcmp(argv[--argc], "]") != 0)
			error("missing ]");
		argv[argc] = NULL;
	}

	/* no expression => false */
	if (--argc <= 0)
		return 1;

#ifndef SHELL
	(void)setlocale(LC_CTYPE, "");
#endif
	nargc = argc;
	t_wp = &argv[1];
	parenlevel = 0;
	if (nargc == 4 && strcmp(*t_wp, "!") == 0) {
		/* Things like ! "" -o x do not fit in the normal grammar. */
		--nargc;
		++t_wp;
		res = oexpr(t_lex(*t_wp));
	} else
		res = !oexpr(t_lex(*t_wp));

	if (--nargc > 0)
		syntax(*t_wp, "unexpected operator");

	return res;
}

static void
syntax(const char *op, const char *msg)
{

	if (op && *op)
		error("%s: %s", op, msg);
	else
		error("%s", msg);
}

static int
oexpr(enum token n)
{
	int res;

	res = aexpr(n);
	if (t_lex(nargc > 0 ? (--nargc, *++t_wp) : NULL) == BOR)
		return oexpr(t_lex(nargc > 0 ? (--nargc, *++t_wp) : NULL)) ||
		    res;
	t_wp--;
	nargc++;
	return res;
}

static int
aexpr(enum token n)
{
	int res;

	res = nexpr(n);
	if (t_lex(nargc > 0 ? (--nargc, *++t_wp) : NULL) == BAND)
		return aexpr(t_lex(nargc > 0 ? (--nargc, *++t_wp) : NULL)) &&
		    res;
	t_wp--;
	nargc++;
	return res;
}

static int
nexpr(enum token n)
{
	if (n == UNOT)
		return !nexpr(t_lex(nargc > 0 ? (--nargc, *++t_wp) : NULL));
	return primary(n);
}

static int
primary(enum token n)
{
	enum token nn;
	int res;

	if (n == EOI)
		return 0;		/* missing expression */
	if (n == LPAREN) {
		parenlevel++;
		if ((nn = t_lex(nargc > 0 ? (--nargc, *++t_wp) : NULL)) ==
		    RPAREN) {
			parenlevel--;
			return 0;	/* missing expression */
		}
		res = oexpr(nn);
		if (t_lex(nargc > 0 ? (--nargc, *++t_wp) : NULL) != RPAREN)
			syntax(NULL, "closing paren expected");
		parenlevel--;
		return res;
	}
	if (TOKEN_TYPE(n) == UNOP) {
		/* unary expression */
		if (--nargc == 0)
			syntax(NULL, "argument expected"); /* impossible */
		switch (n) {
		case STREZ:
			return strlen(*++t_wp) == 0;
		case STRNZ:
			return strlen(*++t_wp) != 0;
		case FILTT:
			return isatty(getn(*++t_wp));
		default:
			return filstat(*++t_wp, n);
		}
	}

	nn = t_lex(nargc > 0 ? t_wp[1] : NULL);
	if (TOKEN_TYPE(nn) == BINOP)
		return binop(nn);

	return strlen(*t_wp) > 0;
}

static int
binop(enum token n)
{
	const char *opnd1, *op, *opnd2;

	opnd1 = *t_wp;
	op = nargc > 0 ? (--nargc, *++t_wp) : NULL;

	if ((opnd2 = nargc > 0 ? (--nargc, *++t_wp) : NULL) == NULL)
		syntax(op, "argument expected");

	switch (n) {
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
		return newerf (opnd1, opnd2);
	case FILOT:
		return olderf (opnd1, opnd2);
	case FILEQ:
		return equalf (opnd1, opnd2);
	default:
		abort();
		/* NOTREACHED */
	}
}

static int
filstat(char *nm, enum token mode)
{
	struct stat s;

	if (mode == FILSYM ? lstat(nm, &s) : stat(nm, &s))
		return 0;

	switch (mode) {
	case FILRD:
		return (eaccess(nm, R_OK) == 0);
	case FILWR:
		return (eaccess(nm, W_OK) == 0);
	case FILEX:
		/* XXX work around eaccess(2) false positives for superuser */
		if (eaccess(nm, X_OK) != 0)
			return 0;
		if (S_ISDIR(s.st_mode) || geteuid() != 0)
			return 1;
		return (s.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
	case FILEXIST:
		return (eaccess(nm, F_OK) == 0);
	case FILREG:
		return S_ISREG(s.st_mode);
	case FILDIR:
		return S_ISDIR(s.st_mode);
	case FILCDEV:
		return S_ISCHR(s.st_mode);
	case FILBDEV:
		return S_ISBLK(s.st_mode);
	case FILFIFO:
		return S_ISFIFO(s.st_mode);
	case FILSOCK:
		return S_ISSOCK(s.st_mode);
	case FILSYM:
		return S_ISLNK(s.st_mode);
	case FILSUID:
		return (s.st_mode & S_ISUID) != 0;
	case FILSGID:
		return (s.st_mode & S_ISGID) != 0;
	case FILSTCK:
		return (s.st_mode & S_ISVTX) != 0;
	case FILGZ:
		return s.st_size > (off_t)0;
	case FILUID:
		return s.st_uid == geteuid();
	case FILGID:
		return s.st_gid == getegid();
	default:
		return 1;
	}
}

static int
find_op_1char(const struct t_op *op, const struct t_op *end, const char *s)
{
	char c;

	c = s[0];
	while (op != end) {
		if (c == *op->op_text)
			return op->op_num;
		op++;
	}
	return OPERAND;
}

static int
find_op_2char(const struct t_op *op, const struct t_op *end, const char *s)
{
	while (op != end) {
		if (s[0] == op->op_text[0] && s[1] == op->op_text[1])
			return op->op_num;
		op++;
	}
	return OPERAND;
}

static int
find_op(const char *s)
{
	if (s[0] == '\0')
		return OPERAND;
	else if (s[1] == '\0')
		return find_op_1char(ops1, (&ops1)[1], s);
	else if (s[2] == '\0')
		return s[0] == '-' ? find_op_1char(opsm1, (&opsm1)[1], s + 1) :
		    find_op_2char(ops2, (&ops2)[1], s);
	else if (s[3] == '\0')
		return s[0] == '-' ? find_op_2char(opsm2, (&opsm2)[1], s + 1) :
		    OPERAND;
	else
		return OPERAND;
}

static enum token
t_lex(char *s)
{
	int num;

	if (s == NULL) {
		return EOI;
	}
	num = find_op(s);
	if (((TOKEN_TYPE(num) == UNOP || TOKEN_TYPE(num) == BUNOP)
				&& isunopoperand()) ||
	    (num == LPAREN && islparenoperand()) ||
	    (num == RPAREN && isrparenoperand()))
		return OPERAND;
	return num;
}

static int
isunopoperand(void)
{
	char *s;
	char *t;
	int num;

	if (nargc == 1)
		return 1;
	s = *(t_wp + 1);
	if (nargc == 2)
		return parenlevel == 1 && strcmp(s, ")") == 0;
	t = *(t_wp + 2);
	num = find_op(s);
	return TOKEN_TYPE(num) == BINOP &&
	    (parenlevel == 0 || t[0] != ')' || t[1] != '\0');
}

static int
islparenoperand(void)
{
	char *s;
	int num;

	if (nargc == 1)
		return 1;
	s = *(t_wp + 1);
	if (nargc == 2)
		return parenlevel == 1 && strcmp(s, ")") == 0;
	if (nargc != 3)
		return 0;
	num = find_op(s);
	return TOKEN_TYPE(num) == BINOP;
}

static int
isrparenoperand(void)
{
	char *s;

	if (nargc == 1)
		return 0;
	s = *(t_wp + 1);
	if (nargc == 2)
		return parenlevel == 1 && strcmp(s, ")") == 0;
	return 0;
}

/* atoi with error detection */
static int
getn(const char *s)
{
	char *p;
	long r;

	errno = 0;
	r = strtol(s, &p, 10);

	if (s == p)
		error("%s: bad number", s);

	if (errno != 0)
		error((errno == EINVAL) ? "%s: bad number" :
					  "%s: out of range", s);

	while (isspace((unsigned char)*p))
		p++;

	if (*p)
		error("%s: bad number", s);

	return (int) r;
}

/* atoi with error detection and 64 bit range */
static intmax_t
getq(const char *s)
{
	char *p;
	intmax_t r;

	errno = 0;
	r = strtoimax(s, &p, 10);

	if (s == p)
		error("%s: bad number", s);

	if (errno != 0)
		error((errno == EINVAL) ? "%s: bad number" :
					  "%s: out of range", s);

	while (isspace((unsigned char)*p))
		p++;

	if (*p)
		error("%s: bad number", s);

	return r;
}

static int
intcmp (const char *s1, const char *s2)
{
	intmax_t q1, q2;


	q1 = getq(s1);
	q2 = getq(s2);

	if (q1 > q2)
		return 1;

	if (q1 < q2)
		return -1;

	return 0;
}

static int
newerf (const char *f1, const char *f2)
{
	struct stat b1, b2;

	if (stat(f1, &b1) != 0 || stat(f2, &b2) != 0)
		return 0;

	if (b1.st_mtim.tv_sec > b2.st_mtim.tv_sec)
		return 1;
	if (b1.st_mtim.tv_sec < b2.st_mtim.tv_sec)
		return 0;

       return (b1.st_mtim.tv_nsec > b2.st_mtim.tv_nsec);
}

static int
olderf (const char *f1, const char *f2)
{
	return (newerf(f2, f1));
}

static int
equalf (const char *f1, const char *f2)
{
	struct stat b1, b2;

	return (stat (f1, &b1) == 0 &&
		stat (f2, &b2) == 0 &&
		b1.st_dev == b2.st_dev &&
		b1.st_ino == b2.st_ino);
}
