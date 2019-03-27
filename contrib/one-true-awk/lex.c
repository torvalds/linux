/****************************************************************
Copyright (C) Lucent Technologies 1997
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name Lucent Technologies or any of
its entities not be used in advertising or publicity pertaining
to distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "awk.h"
#include "ytab.h"

extern YYSTYPE	yylval;
extern int	infunc;

int	lineno	= 1;
int	bracecnt = 0;
int	brackcnt  = 0;
int	parencnt = 0;

typedef struct Keyword {
	const char *word;
	int	sub;
	int	type;
} Keyword;

Keyword keywords[] ={	/* keep sorted: binary searched */
	{ "BEGIN",	XBEGIN,		XBEGIN },
	{ "END",	XEND,		XEND },
	{ "NF",		VARNF,		VARNF },
	{ "and",	FAND,		BLTIN },
	{ "atan2",	FATAN,		BLTIN },
	{ "break",	BREAK,		BREAK },
	{ "close",	CLOSE,		CLOSE },
	{ "compl",	FCOMPL,		BLTIN },
	{ "continue",	CONTINUE,	CONTINUE },
	{ "cos",	FCOS,		BLTIN },
	{ "delete",	DELETE,		DELETE },
	{ "do",		DO,		DO },
	{ "else",	ELSE,		ELSE },
	{ "exit",	EXIT,		EXIT },
	{ "exp",	FEXP,		BLTIN },
	{ "fflush",	FFLUSH,		BLTIN },
	{ "for",	FOR,		FOR },
	{ "func",	FUNC,		FUNC },
	{ "function",	FUNC,		FUNC },
	{ "getline",	GETLINE,	GETLINE },
	{ "gsub",	GSUB,		GSUB },
	{ "if",		IF,		IF },
	{ "in",		IN,		IN },
	{ "index",	INDEX,		INDEX },
	{ "int",	FINT,		BLTIN },
	{ "length",	FLENGTH,	BLTIN },
	{ "log",	FLOG,		BLTIN },
	{ "lshift",	FLSHIFT,	BLTIN },
	{ "match",	MATCHFCN,	MATCHFCN },
	{ "next",	NEXT,		NEXT },
	{ "nextfile",	NEXTFILE,	NEXTFILE },
	{ "or",		FFOR,		BLTIN },
	{ "print",	PRINT,		PRINT },
	{ "printf",	PRINTF,		PRINTF },
	{ "rand",	FRAND,		BLTIN },
	{ "return",	RETURN,		RETURN },
	{ "rshift",	FRSHIFT,	BLTIN },
	{ "sin",	FSIN,		BLTIN },
	{ "split",	SPLIT,		SPLIT },
	{ "sprintf",	SPRINTF,	SPRINTF },
	{ "sqrt",	FSQRT,		BLTIN },
	{ "srand",	FSRAND,		BLTIN },
	{ "sub",	SUB,		SUB },
	{ "substr",	SUBSTR,		SUBSTR },
	{ "system",	FSYSTEM,	BLTIN },
	{ "tolower",	FTOLOWER,	BLTIN },
	{ "toupper",	FTOUPPER,	BLTIN },
	{ "while",	WHILE,		WHILE },
	{ "xor",	FXOR,		BLTIN },
};

#define	RET(x)	{ if(dbg)printf("lex %s\n", tokname(x)); return(x); }

int peek(void)
{
	int c = input();
	unput(c);
	return c;
}

int gettok(char **pbuf, int *psz)	/* get next input token */
{
	int c, retc;
	char *buf = *pbuf;
	int sz = *psz;
	char *bp = buf;

	c = input();
	if (c == 0)
		return 0;
	buf[0] = c;
	buf[1] = 0;
	if (!isalnum(c) && c != '.' && c != '_')
		return c;

	*bp++ = c;
	if (isalpha(c) || c == '_') {	/* it's a varname */
		for ( ; (c = input()) != 0; ) {
			if (bp-buf >= sz)
				if (!adjbuf(&buf, &sz, bp-buf+2, 100, &bp, "gettok"))
					FATAL( "out of space for name %.10s...", buf );
			if (isalnum(c) || c == '_')
				*bp++ = c;
			else {
				*bp = 0;
				unput(c);
				break;
			}
		}
		*bp = 0;
		retc = 'a';	/* alphanumeric */
	} else {	/* maybe it's a number, but could be . */
		char *rem;
		/* read input until can't be a number */
		for ( ; (c = input()) != 0; ) {
			if (bp-buf >= sz)
				if (!adjbuf(&buf, &sz, bp-buf+2, 100, &bp, "gettok"))
					FATAL( "out of space for number %.10s...", buf );
			if (isdigit(c) || c == 'e' || c == 'E' 
			  || c == '.' || c == '+' || c == '-')
				*bp++ = c;
			else {
				unput(c);
				break;
			}
		}
		*bp = 0;
		strtod(buf, &rem);	/* parse the number */
		if (rem == buf) {	/* it wasn't a valid number at all */
			buf[1] = 0;	/* return one character as token */
			retc = buf[0];	/* character is its own type */
			unputstr(rem+1); /* put rest back for later */
		} else {	/* some prefix was a number */
			unputstr(rem);	/* put rest back for later */
			rem[0] = 0;	/* truncate buf after number part */
			retc = '0';	/* type is number */
		}
	}
	*pbuf = buf;
	*psz = sz;
	return retc;
}

int	word(char *);
int	string(void);
int	regexpr(void);
int	sc	= 0;	/* 1 => return a } right now */
int	reg	= 0;	/* 1 => return a REGEXPR now */

int yylex(void)
{
	int c;
	static char *buf = NULL;
	static int bufsize = 5; /* BUG: setting this small causes core dump! */

	if (buf == NULL && (buf = (char *) malloc(bufsize)) == NULL)
		FATAL( "out of space in yylex" );
	if (sc) {
		sc = 0;
		RET('}');
	}
	if (reg) {
		reg = 0;
		return regexpr();
	}
	for (;;) {
		c = gettok(&buf, &bufsize);
		if (c == 0)
			return 0;
		if (isalpha(c) || c == '_')
			return word(buf);
		if (isdigit(c)) {
			yylval.cp = setsymtab(buf, tostring(buf), atof(buf), CON|NUM, symtab);
			/* should this also have STR set? */
			RET(NUMBER);
		}
	
		yylval.i = c;
		switch (c) {
		case '\n':	/* {EOL} */
			RET(NL);
		case '\r':	/* assume \n is coming */
		case ' ':	/* {WS}+ */
		case '\t':
			break;
		case '#':	/* #.* strip comments */
			while ((c = input()) != '\n' && c != 0)
				;
			unput(c);
			break;
		case ';':
			RET(';');
		case '\\':
			if (peek() == '\n') {
				input();
			} else if (peek() == '\r') {
				input(); input();	/* \n */
				lineno++;
			} else {
				RET(c);
			}
			break;
		case '&':
			if (peek() == '&') {
				input(); RET(AND);
			} else 
				RET('&');
		case '|':
			if (peek() == '|') {
				input(); RET(BOR);
			} else
				RET('|');
		case '!':
			if (peek() == '=') {
				input(); yylval.i = NE; RET(NE);
			} else if (peek() == '~') {
				input(); yylval.i = NOTMATCH; RET(MATCHOP);
			} else
				RET(NOT);
		case '~':
			yylval.i = MATCH;
			RET(MATCHOP);
		case '<':
			if (peek() == '=') {
				input(); yylval.i = LE; RET(LE);
			} else {
				yylval.i = LT; RET(LT);
			}
		case '=':
			if (peek() == '=') {
				input(); yylval.i = EQ; RET(EQ);
			} else {
				yylval.i = ASSIGN; RET(ASGNOP);
			}
		case '>':
			if (peek() == '=') {
				input(); yylval.i = GE; RET(GE);
			} else if (peek() == '>') {
				input(); yylval.i = APPEND; RET(APPEND);
			} else {
				yylval.i = GT; RET(GT);
			}
		case '+':
			if (peek() == '+') {
				input(); yylval.i = INCR; RET(INCR);
			} else if (peek() == '=') {
				input(); yylval.i = ADDEQ; RET(ASGNOP);
			} else
				RET('+');
		case '-':
			if (peek() == '-') {
				input(); yylval.i = DECR; RET(DECR);
			} else if (peek() == '=') {
				input(); yylval.i = SUBEQ; RET(ASGNOP);
			} else
				RET('-');
		case '*':
			if (peek() == '=') {	/* *= */
				input(); yylval.i = MULTEQ; RET(ASGNOP);
			} else if (peek() == '*') {	/* ** or **= */
				input();	/* eat 2nd * */
				if (peek() == '=') {
					input(); yylval.i = POWEQ; RET(ASGNOP);
				} else {
					RET(POWER);
				}
			} else
				RET('*');
		case '/':
			RET('/');
		case '%':
			if (peek() == '=') {
				input(); yylval.i = MODEQ; RET(ASGNOP);
			} else
				RET('%');
		case '^':
			if (peek() == '=') {
				input(); yylval.i = POWEQ; RET(ASGNOP);
			} else
				RET(POWER);

		case '$':
			/* BUG: awkward, if not wrong */
			c = gettok(&buf, &bufsize);
			if (isalpha(c)) {
				if (strcmp(buf, "NF") == 0) {	/* very special */
					unputstr("(NF)");
					RET(INDIRECT);
				}
				c = peek();
				if (c == '(' || c == '[' || (infunc && isarg(buf) >= 0)) {
					unputstr(buf);
					RET(INDIRECT);
				}
				yylval.cp = setsymtab(buf, "", 0.0, STR|NUM, symtab);
				RET(IVAR);
			} else if (c == 0) {	/*  */
				SYNTAX( "unexpected end of input after $" );
				RET(';');
			} else {
				unputstr(buf);
				RET(INDIRECT);
			}
	
		case '}':
			if (--bracecnt < 0)
				SYNTAX( "extra }" );
			sc = 1;
			RET(';');
		case ']':
			if (--brackcnt < 0)
				SYNTAX( "extra ]" );
			RET(']');
		case ')':
			if (--parencnt < 0)
				SYNTAX( "extra )" );
			RET(')');
		case '{':
			bracecnt++;
			RET('{');
		case '[':
			brackcnt++;
			RET('[');
		case '(':
			parencnt++;
			RET('(');
	
		case '"':
			return string();	/* BUG: should be like tran.c ? */
	
		default:
			RET(c);
		}
	}
}

int string(void)
{
	int c, n;
	char *s, *bp;
	static char *buf = NULL;
	static int bufsz = 500;

	if (buf == NULL && (buf = (char *) malloc(bufsz)) == NULL)
		FATAL("out of space for strings");
	for (bp = buf; (c = input()) != '"'; ) {
		if (!adjbuf(&buf, &bufsz, bp-buf+2, 500, &bp, "string"))
			FATAL("out of space for string %.10s...", buf);
		switch (c) {
		case '\n':
		case '\r':
		case 0:
			SYNTAX( "non-terminated string %.10s...", buf );
			lineno++;
			if (c == 0)	/* hopeless */
				FATAL( "giving up" );
			break;
		case '\\':
			c = input();
			switch (c) {
			case '"': *bp++ = '"'; break;
			case 'n': *bp++ = '\n'; break;	
			case 't': *bp++ = '\t'; break;
			case 'f': *bp++ = '\f'; break;
			case 'r': *bp++ = '\r'; break;
			case 'b': *bp++ = '\b'; break;
			case 'v': *bp++ = '\v'; break;
			case 'a': *bp++ = '\007'; break;
			case '\\': *bp++ = '\\'; break;

			case '0': case '1': case '2': /* octal: \d \dd \ddd */
			case '3': case '4': case '5': case '6': case '7':
				n = c - '0';
				if ((c = peek()) >= '0' && c < '8') {
					n = 8 * n + input() - '0';
					if ((c = peek()) >= '0' && c < '8')
						n = 8 * n + input() - '0';
				}
				*bp++ = n;
				break;

			case 'x':	/* hex  \x0-9a-fA-F + */
			    {	char xbuf[100], *px;
				for (px = xbuf; (c = input()) != 0 && px-xbuf < 100-2; ) {
					if (isdigit(c)
					 || (c >= 'a' && c <= 'f')
					 || (c >= 'A' && c <= 'F'))
						*px++ = c;
					else
						break;
				}
				*px = 0;
				unput(c);
	  			sscanf(xbuf, "%x", (unsigned int *) &n);
				*bp++ = n;
				break;
			    }

			default: 
				*bp++ = c;
				break;
			}
			break;
		default:
			*bp++ = c;
			break;
		}
	}
	*bp = 0; 
	s = tostring(buf);
	*bp++ = ' '; *bp++ = 0;
	yylval.cp = setsymtab(buf, s, 0.0, CON|STR|DONTFREE, symtab);
	RET(STRING);
}


int binsearch(char *w, Keyword *kp, int n)
{
	int cond, low, mid, high;

	low = 0;
	high = n - 1;
	while (low <= high) {
		mid = (low + high) / 2;
		if ((cond = strcmp(w, kp[mid].word)) < 0)
			high = mid - 1;
		else if (cond > 0)
			low = mid + 1;
		else
			return mid;
	}
	return -1;
}

int word(char *w) 
{
	Keyword *kp;
	int c, n;

	n = binsearch(w, keywords, sizeof(keywords)/sizeof(keywords[0]));
/* BUG: this ought to be inside the if; in theory could fault (daniel barrett) */
	kp = keywords + n;
	if (n != -1) {	/* found in table */
		yylval.i = kp->sub;
		switch (kp->type) {	/* special handling */
		case BLTIN:
			if (kp->sub == FSYSTEM && safe)
				SYNTAX( "system is unsafe" );
			RET(kp->type);
		case FUNC:
			if (infunc)
				SYNTAX( "illegal nested function" );
			RET(kp->type);
		case RETURN:
			if (!infunc)
				SYNTAX( "return not in function" );
			RET(kp->type);
		case VARNF:
			yylval.cp = setsymtab("NF", "", 0.0, NUM, symtab);
			RET(VARNF);
		default:
			RET(kp->type);
		}
	}
	c = peek();	/* look for '(' */
	if (c != '(' && infunc && (n=isarg(w)) >= 0) {
		yylval.i = n;
		RET(ARG);
	} else {
		yylval.cp = setsymtab(w, "", 0.0, STR|NUM|DONTFREE, symtab);
		if (c == '(') {
			RET(CALL);
		} else {
			RET(VAR);
		}
	}
}

void startreg(void)	/* next call to yylex will return a regular expression */
{
	reg = 1;
}

int regexpr(void)
{
	int c;
	static char *buf = NULL;
	static int bufsz = 500;
	char *bp;

	if (buf == NULL && (buf = (char *) malloc(bufsz)) == NULL)
		FATAL("out of space for rex expr");
	bp = buf;
	for ( ; (c = input()) != '/' && c != 0; ) {
		if (!adjbuf(&buf, &bufsz, bp-buf+3, 500, &bp, "regexpr"))
			FATAL("out of space for reg expr %.10s...", buf);
		if (c == '\n') {
			SYNTAX( "newline in regular expression %.10s...", buf ); 
			unput('\n');
			break;
		} else if (c == '\\') {
			*bp++ = '\\'; 
			*bp++ = input();
		} else {
			*bp++ = c;
		}
	}
	*bp = 0;
	if (c == 0)
		SYNTAX("non-terminated regular expression %.10s...", buf);
	yylval.s = tostring(buf);
	unput('/');
	RET(REGEXPR);
}

/* low-level lexical stuff, sort of inherited from lex */

char	ebuf[300];
char	*ep = ebuf;
char	yysbuf[100];	/* pushback buffer */
char	*yysptr = yysbuf;
FILE	*yyin = NULL;

int input(void)	/* get next lexical input character */
{
	int c;
	extern char *lexprog;

	if (yysptr > yysbuf)
		c = (uschar)*--yysptr;
	else if (lexprog != NULL) {	/* awk '...' */
		if ((c = (uschar)*lexprog) != 0)
			lexprog++;
	} else				/* awk -f ... */
		c = pgetc();
	if (c == '\n')
		lineno++;
	else if (c == EOF)
		c = 0;
	if (ep >= ebuf + sizeof ebuf)
		ep = ebuf;
	return *ep++ = c;
}

void unput(int c)	/* put lexical character back on input */
{
	if (c == '\n')
		lineno--;
	if (yysptr >= yysbuf + sizeof(yysbuf))
		FATAL("pushed back too much: %.20s...", yysbuf);
	*yysptr++ = c;
	if (--ep < ebuf)
		ep = ebuf + sizeof(ebuf) - 1;
}

void unputstr(const char *s)	/* put a string back on input */
{
	int i;

	for (i = strlen(s)-1; i >= 0; i--)
		unput(s[i]);
}
