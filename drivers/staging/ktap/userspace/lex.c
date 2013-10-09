/*
 * lex.c - ktap lexical analyzer
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Copyright (C) 1994-2013 Lua.org, PUC-Rio.
 *  - The part of code in this file is copied from lua initially.
 *  - lua's MIT license is compatible with GPL.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include "../include/ktap_types.h"
#include "ktapc.h"

#define next(ls) (ls->current = *ls->ptr++)

#define currIsNewline(ls)	(ls->current == '\n' || ls->current == '\r')

#define KTAP_MINBUFFER   32

/* ORDER RESERVED */
static const char *const ktap_tokens [] = {
	"trace", "trace_end", "argevent", "argname",
	"arg1", "arg2", "arg3", "arg4", "arg5", "arg6", "arg7", "arg9", "arg9",
	"profile", "tick",
	"and", "break", "do", "else", "elseif",
	"end", "false", "for", "function", "goto", "if",
	"in", "local", "nil", "not", "or", "repeat",
	"return", "then", "true", "until", "while",
	"..", "...", "==", ">=", "<=", "!=", "+=", "::", "<eof>",
	"<number>", "<name>", "<string>"
};

#define save_and_next(ls) (save(ls, ls->current), next(ls))

static void lexerror(ktap_lexstate *ls, const char *msg, int token);

static void save(ktap_lexstate *ls, int c)
{
	ktap_mbuffer *b = ls->buff;
	if (mbuff_len(b) + 1 > mbuff_size(b)) {
		size_t newsize;
		if (mbuff_size(b) >= MAX_SIZET / 2)
			lexerror(ls, "lexical element too long", 0);
		newsize = mbuff_size(b) * 2;
		mbuff_resize(b, newsize);
	}
	b->buffer[mbuff_len(b)++] = (char)c;
}

void lex_init()
{
	int i;
	for (i = 0; i < NUM_RESERVED; i++) {
		ktap_string *ts = ktapc_ts_new(ktap_tokens[i]);
		ts->tsv.extra = (u8)(i+1);  /* reserved word */
	}
}

const char *lex_token2str(ktap_lexstate *ls, int token)
{
	if (token < FIRST_RESERVED) {
		ktap_assert(token == (unsigned char)token);
		return (isprint(token)) ? ktapc_sprintf(KTAP_QL("%c"), token) :
			ktapc_sprintf("char(%d)", token);
	} else {
		const char *s = ktap_tokens[token - FIRST_RESERVED];
		if (token < TK_EOS)
			return ktapc_sprintf(KTAP_QS, s);
		else
			return s;
	}
}

static const char *txtToken(ktap_lexstate *ls, int token)
{
	switch (token) {
	case TK_NAME:
	case TK_STRING:
	case TK_NUMBER:
		save(ls, '\0');
		return ktapc_sprintf(KTAP_QS, mbuff(ls->buff));
	default:
		return lex_token2str(ls, token);
	}
}

static void lexerror(ktap_lexstate *ls, const char *msg, int token)
{
	char buff[KTAP_IDSIZE];
	char *newmsg;

	ktapc_chunkid(buff, getstr(ls->source), KTAP_IDSIZE);
	newmsg = ktapc_sprintf("%s:%d: %s", buff, ls->linenumber, msg);
	if (token)
		newmsg = ktapc_sprintf("%s near %s", newmsg, txtToken(ls, token));
	printf("lexerror: %s\n", newmsg);
	exit(EXIT_FAILURE);
}

void lex_syntaxerror(ktap_lexstate *ls, const char *msg)
{
	lexerror(ls, msg, ls->t.token);
}

/*
 * creates a new string and anchors it in function's table so that
 * it will not be collected until the end of the function's compilation
 * (by that time it should be anchored in function's prototype)
 */
ktap_string *lex_newstring(ktap_lexstate *ls, const char *str, size_t l)
{
	const ktap_value *o;  /* entry for `str' */
	ktap_value val;  /* entry for `str' */
	ktap_value tsv;
	ktap_string *ts = ktapc_ts_newlstr(str, l);  /* create new string */
	setsvalue(&tsv, ts); 
	o = ktapc_table_get(ls->fs->h, &tsv);
	if (ttisnil(o)) {  /* not in use yet? (see 'addK') */
		/* boolean value does not need GC barrier;
		table has no metatable, so it does not need to invalidate cache */
		setbvalue(&val, 1);  /* t[string] = true */
		ktapc_table_setvalue(ls->fs->h, &tsv, &val);
	}
	return ts;
}

/*
 * increment line number and skips newline sequence (any of
 * \n, \r, \n\r, or \r\n)
 */
static void inclinenumber(ktap_lexstate *ls)
{
	int old = ls->current;
	ktap_assert(currIsNewline(ls));
	next(ls);  /* skip `\n' or `\r' */
	if (currIsNewline(ls) && ls->current != old)
		next(ls);  /* skip `\n\r' or `\r\n' */
	if (++ls->linenumber >= MAX_INT)
		lex_syntaxerror(ls, "chunk has too many lines");
}

void lex_setinput(ktap_lexstate *ls, char *ptr, ktap_string *source, int firstchar)
{
	ls->decpoint = '.';
	ls->current = firstchar;
	ls->lookahead.token = TK_EOS;  /* no look-ahead token */
	ls->ptr = ptr;
	ls->fs = NULL;
	ls->linenumber = 1;
	ls->lastline = 1;
	ls->source = source;
	ls->envn = ktapc_ts_new(KTAP_ENV);  /* create env name */
	mbuff_resize(ls->buff, KTAP_MINBUFFER);  /* initialize buffer */
}

/*
 * =======================================================
 * LEXICAL ANALYZER
 * =======================================================
 */
static int check_next(ktap_lexstate *ls, const char *set)
{
	if (ls->current == '\0' || !strchr(set, ls->current))
		return 0;
	save_and_next(ls);
	return 1;
}

/*
 * change all characters 'from' in buffer to 'to'
 */
static void buffreplace(ktap_lexstate *ls, char from, char to)
{
	size_t n = mbuff_len(ls->buff);
	char *p = mbuff(ls->buff);
	while (n--)
		if (p[n] == from) p[n] = to;
}

#if !defined(getlocaledecpoint)
#define getlocaledecpoint()	(localeconv()->decimal_point[0])
#endif

#define mbuff2d(b,e)	ktapc_str2d(mbuff(b), mbuff_len(b) - 1, e)

/*
 * in case of format error, try to change decimal point separator to
 * the one defined in the current locale and check again
 */
static void trydecpoint(ktap_lexstate *ls, ktap_seminfo *seminfo)
{
	char old = ls->decpoint;
	ls->decpoint = getlocaledecpoint();
	buffreplace(ls, old, ls->decpoint);  /* try new decimal separator */
	if (!mbuff2d(ls->buff, &seminfo->r)) {
		/* format error with correct decimal point: no more options */
		buffreplace(ls, ls->decpoint, '.');  /* undo change (for error message) */
		lexerror(ls, "malformed number", TK_NUMBER);
	}
}

/*
 * this function is quite liberal in what it accepts, as 'ktapc_str2d'
 * will reject ill-formed numerals.
 */
static void read_numeral(ktap_lexstate *ls, ktap_seminfo *seminfo)
{
	const char *expo = "Ee";
	int first = ls->current;

	ktap_assert(isdigit(ls->current));
	save_and_next(ls);
	if (first == '0' && check_next(ls, "Xx"))  /* hexadecimal? */
		expo = "Pp";
	for (;;) {
		if (check_next(ls, expo))  /* exponent part? */
			check_next(ls, "+-");  /* optional exponent sign */
		if (isxdigit(ls->current) || ls->current == '.')
			save_and_next(ls);
		else
			break;
	}
	save(ls, '\0');
	buffreplace(ls, '.', ls->decpoint);  /* follow locale for decimal point */
	if (!mbuff2d(ls->buff, &seminfo->r))  /* format error? */
		trydecpoint(ls, seminfo); /* try to update decimal point separator */
}

/*
 * skip a sequence '[=*[' or ']=*]' and return its number of '='s or
 * -1 if sequence is malformed
 */
static int skip_sep(ktap_lexstate *ls)
{
	int count = 0;
	int s = ls->current;

	ktap_assert(s == '[' || s == ']');
	save_and_next(ls);
	while (ls->current == '=') {
		save_and_next(ls);
		count++;
	}
	return (ls->current == s) ? count : (-count) - 1;
}

static void read_long_string(ktap_lexstate *ls, ktap_seminfo *seminfo, int sep)
{
	save_and_next(ls);  /* skip 2nd `[' */
	if (currIsNewline(ls))  /* string starts with a newline? */
		inclinenumber(ls);  /* skip it */
	for (;;) {
		switch (ls->current) {
		case EOZ:
			lexerror(ls, (seminfo) ? "unfinished long string" :
				"unfinished long comment", TK_EOS);
			break;  /* to avoid warnings */
		case ']': {
			if (skip_sep(ls) == sep) {
				save_and_next(ls);  /* skip 2nd `]' */
				goto endloop;
			}
			break;
		}
		case '\n':
		case '\r': {
			save(ls, '\n');
			inclinenumber(ls);
			/* avoid wasting space */
			if (!seminfo)
				mbuff_reset(ls->buff);
			break;
		}
		default: {
			if (seminfo)
				save_and_next(ls);
			else
				next(ls);
		}
		}
	}

 endloop:
	if (seminfo)
		seminfo->ts = lex_newstring(ls, mbuff(ls->buff) + (2 + sep),
			mbuff_len(ls->buff) - 2*(2 + sep));
}

static void escerror(ktap_lexstate *ls, int *c, int n, const char *msg)
{
	int i;
	mbuff_reset(ls->buff);  /* prepare error message */
	save(ls, '\\');
	for (i = 0; i < n && c[i] != EOZ; i++)
		save(ls, c[i]);
	lexerror(ls, msg, TK_STRING);
}

static int readhexaesc(ktap_lexstate *ls)
{
	int c[3], i;  /* keep input for error message */
	int r = 0;  /* result accumulator */
	c[0] = 'x';  /* for error message */
	for (i = 1; i < 3; i++) {  /* read two hexa digits */
		c[i] = next(ls);
		if (!isxdigit(c[i]))
			escerror(ls, c, i + 1, "hexadecimal digit expected");
		r = (r << 4) + ktapc_hexavalue(c[i]);
	}
	return r;
}

static int readdecesc(ktap_lexstate *ls)
{
	int c[3], i;
	int r = 0;  /* result accumulator */
	for (i = 0; i < 3 && isdigit(ls->current); i++) {  /* read up to 3 digits */
		c[i] = ls->current;
		r = 10*r + c[i] - '0';
		next(ls);
	}
	if (r > UCHAR_MAX)
		escerror(ls, c, i, "decimal escape too large");
	return r;
}

static void read_string(ktap_lexstate *ls, int del, ktap_seminfo *seminfo)
{
	save_and_next(ls);  /* keep delimiter (for error messages) */
	while (ls->current != del) {
		switch (ls->current) {
		case EOZ:
			lexerror(ls, "unfinished string", TK_EOS);
			break;  /* to avoid warnings */
		case '\n':
		case '\r':
			lexerror(ls, "unfinished string", TK_STRING);
			break;  /* to avoid warnings */
		case '\\': {  /* escape sequences */
			int c;  /* final character to be saved */
			next(ls);  /* do not save the `\' */
			switch (ls->current) {
			case 'a': c = '\a'; goto read_save;
			case 'b': c = '\b'; goto read_save;
			case 'f': c = '\f'; goto read_save;
			case 'n': c = '\n'; goto read_save;
			case 'r': c = '\r'; goto read_save;
			case 't': c = '\t'; goto read_save;
			case 'v': c = '\v'; goto read_save;
			case 'x': c = readhexaesc(ls); goto read_save;
			case '\n': case '\r':
				inclinenumber(ls); c = '\n'; goto only_save;
			case '\\': case '\"': case '\'':
				c = ls->current; goto read_save;
			case EOZ: goto no_save;  /* will raise an error next loop */
			case 'z': {  /* zap following span of spaces */
				next(ls);  /* skip the 'z' */
				while (isspace(ls->current)) {
					if (currIsNewline(ls))
						inclinenumber(ls);
					else
						next(ls);
				}
				goto no_save;
			}
			default: {
				if (!isdigit(ls->current))
					escerror(ls, &ls->current, 1, "invalid escape sequence");
				/* digital escape \ddd */
				c = readdecesc(ls);
				goto only_save;
			}
			}
 read_save:
			next(ls);  /* read next character */
 only_save:
			save(ls, c);  /* save 'c' */
 no_save:
			break;
		}
		default:
			save_and_next(ls);
		}
	}
	save_and_next(ls);  /* skip delimiter */
	seminfo->ts = lex_newstring(ls, mbuff(ls->buff) + 1, mbuff_len(ls->buff) - 2);
}

static int llex(ktap_lexstate *ls, ktap_seminfo *seminfo)
{
	mbuff_reset(ls->buff);

	for (;;) {
		switch (ls->current) {
		case '\n': case '\r': {  /* line breaks */
			inclinenumber(ls);
			break;
		}
		case ' ': case '\f': case '\t': case '\v': {  /* spaces */
			next(ls);
			break;
		}
		case '#': {
			while (!currIsNewline(ls) && ls->current != EOZ)
				next(ls);  /* skip until end of line (or end of file) */
			break;
		}
		#if 0
		case '-': {  /* '-' or '--' (comment) */
			next(ls);
			if (ls->current != '-')
				return '-';
			/* else is a comment */
			next(ls);
			if (ls->current == '[') {  /* long comment? */
				int sep = skip_sep(ls);
				mbuff_reset(ls->buff);  /* `skip_sep' may dirty the buffer */
				if (sep >= 0) {
					read_long_string(ls, NULL, sep);  /* skip long comment */
					mbuff_reset(ls->buff);  /* previous call may dirty the buff. */
					break;
				}
			}
			/* else short comment */
			while (!currIsNewline(ls) && ls->current != EOZ)
				next(ls);  /* skip until end of line (or end of file) */
			break;
		}
		#endif
		case '[': {  /* long string or simply '[' */
			int sep = skip_sep(ls);
			if (sep >= 0) {
				read_long_string(ls, seminfo, sep);
				return TK_STRING;
			}
			else if (sep == -1)
				return '[';
			else
				lexerror(ls, "invalid long string delimiter", TK_STRING);
		}
		case '+': {
			next(ls);
			if (ls->current != '=')
				return '+';
			else {
				next(ls);
				return TK_INCR;
			}
		}
		case '=': {
			next(ls);
			if (ls->current != '=')
				return '=';
			else {
				next(ls);
				return TK_EQ;
			}
		}
		case '<': {
			next(ls);
			if (ls->current != '=')
				return '<';
			else {
				next(ls);
				return TK_LE;
			}
		}
		case '>': {
			next(ls);
			if (ls->current != '=')
				return '>';
			else {
				next(ls);
				return TK_GE;
			}
		}
		case '!': {
			next(ls);
			if (ls->current != '=')
				return TK_NOT;
			else {
				next(ls);
				return TK_NE;
			}
		}
		case ':': {
			next(ls);
			if (ls->current != ':')
				return ':';
			else {
				next(ls);
				return TK_DBCOLON;
			}
		}
		case '"': case '\'': {  /* short literal strings */
			read_string(ls, ls->current, seminfo);
			return TK_STRING;
		}
		case '.': {  /* '.', '..', '...', or number */
			save_and_next(ls);
			if (check_next(ls, ".")) {
				if (check_next(ls, "."))
					return TK_DOTS;   /* '...' */
				else
					return TK_CONCAT;   /* '..' */
			}
			else if (!isdigit(ls->current))
				return '.';
			/* else go through */
		}
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9': {
			read_numeral(ls, seminfo);
			return TK_NUMBER;
		}
		case EOZ: {
			return TK_EOS;
		}
		case '&': {
			next(ls);
			if (ls->current != '&')
				return '&';
			else {
				next(ls);
				return TK_AND;
			}
		} 
		case '|': {
			next(ls);
			if (ls->current != '|')
				return '|';
			else {
				next(ls);
				return TK_OR;
			}
		} 
		default: {
			if (islalpha(ls->current)) {
				/* identifier or reserved word? */
				ktap_string *ts;
				do {
					save_and_next(ls);
				} while (islalnum(ls->current));
				ts = lex_newstring(ls, mbuff(ls->buff),
							mbuff_len(ls->buff));
				seminfo->ts = ts;
				if (isreserved(ts))  /* reserved word? */
					return ts->tsv.extra - 1 +
						FIRST_RESERVED;
				else {
					return TK_NAME;
				}
			} else {  /* single-char tokens (+ - / ...) */
				int c = ls->current;
				next(ls);
				return c;
			}
		}
		}
	}
}

void lex_read_string_until(ktap_lexstate *ls, int c)
{
	ktap_string *ts;
	char errmsg[32];

	mbuff_reset(ls->buff);

	while (ls->current == ' ')
		next(ls);

	do {
		save_and_next(ls);
	} while (ls->current != c && ls->current != EOZ);

	if (ls->current != c) {
		sprintf(errmsg, "expect %c", c);
		lexerror(ls, errmsg, 0);
	}

	ts = lex_newstring(ls, mbuff(ls->buff), mbuff_len(ls->buff));
	ls->t.seminfo.ts = ts;
	ls->t.token = TK_STRING;
}

void lex_next(ktap_lexstate *ls)
{
	ls->lastline = ls->linenumber;
	if (ls->lookahead.token != TK_EOS) {  /* is there a look-ahead token? */
		ls->t = ls->lookahead;  /* use this one */
		ls->lookahead.token = TK_EOS;  /* and discharge it */
	} else
		ls->t.token = llex(ls, &ls->t.seminfo);  /* read next token */
}

int lex_lookahead(ktap_lexstate *ls)
{
	ktap_assert(ls->lookahead.token == TK_EOS);
	ls->lookahead.token = llex(ls, &ls->lookahead.seminfo);
	return ls->lookahead.token;
}

