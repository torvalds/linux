
/* ntp_scanner.c
 *
 * The source code for a simple lexical analyzer. 
 *
 * Written By:	Sachin Kamboj
 *		University of Delaware
 *		Newark, DE 19711
 * Copyright (c) 2006
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "ntpd.h"
#include "ntp_config.h"
#include "ntpsim.h"
#include "ntp_scanner.h"
#include "ntp_parser.h"

/* ntp_keyword.h declares finite state machine and token text */
#include "ntp_keyword.h"



/* SCANNER GLOBAL VARIABLES 
 * ------------------------
 */

#define MAX_LEXEME (1024 + 1)	/* The maximum size of a lexeme */
char yytext[MAX_LEXEME];	/* Buffer for storing the input text/lexeme */
u_int32 conf_file_sum;		/* Simple sum of characters read */

static struct FILE_INFO * lex_stack = NULL;



/* CONSTANTS 
 * ---------
 */


/* SCANNER GLOBAL VARIABLES 
 * ------------------------
 */
const char special_chars[] = "{}(),;|=";


/* FUNCTIONS
 * ---------
 */

static int is_keyword(char *lexeme, follby *pfollowedby);


/*
 * keyword() - Return the keyword associated with token T_ identifier.
 *	       See also token_name() for the string-ized T_ identifier.
 *	       Example: keyword(T_Server) returns "server"
 *			token_name(T_Server) returns "T_Server"
 */
const char *
keyword(
	int token
	)
{
	size_t i;
	const char *text;

	i = token - LOWEST_KEYWORD_ID;

	if (i < COUNTOF(keyword_text))
		text = keyword_text[i];
	else
		text = NULL;

	return (text != NULL)
		   ? text
		   : "(keyword not found)";
}


/* FILE & STRING BUFFER INTERFACE
 * ------------------------------
 *
 * This set out as a couple of wrapper functions around the standard C
 * fgetc and ungetc functions in order to include positional
 * bookkeeping. Alas, this is no longer a good solution with nested
 * input files and the possibility to send configuration commands via
 * 'ntpdc' and 'ntpq'.
 *
 * Now there are a few functions to maintain a stack of nested input
 * sources (though nesting is only allowd for disk files) and from the
 * scanner / parser point of view there's no difference between both
 * types of sources.
 *
 * The 'fgetc()' / 'ungetc()' replacements now operate on a FILE_INFO
 * structure. Instead of trying different 'ungetc()' strategies for file
 * and buffer based parsing, we keep the backup char in our own
 * FILE_INFO structure. This is sufficient, as the parser does *not*
 * jump around via 'seek' or the like, and there's no need to
 * check/clear the backup store in other places than 'lex_getch()'.
 */

/*
 * Allocate an info structure and attach it to a file.
 *
 * Note: When 'mode' is NULL, then the INFO block will be set up to
 * contain a NULL file pointer, as suited for remote config command
 * parsing. Otherwise having a NULL file pointer is considered an error,
 * and a NULL info block pointer is returned to indicate failure!
 *
 * Note: We use a variable-sized structure to hold a copy of the file
 * name (or, more proper, the input source description). This is more
 * secure than keeping a reference to some other storage that might go
 * out of scope.
 */
static struct FILE_INFO *
lex_open(
	const char *path,
	const char *mode
	)
{
	struct FILE_INFO *stream;
	size_t            nnambuf;

	nnambuf = strlen(path);
	stream = emalloc_zero(sizeof(*stream) + nnambuf);
	stream->curpos.nline = 1;
	stream->backch = EOF;
	/* copy name with memcpy -- trailing NUL already there! */
	memcpy(stream->fname, path, nnambuf);

	if (NULL != mode) {
		stream->fpi = fopen(path, mode);
		if (NULL == stream->fpi) {
			free(stream);
			stream = NULL;
		}
	}
	return stream;
}

/* get next character from buffer or file. This will return any putback
 * character first; it will also make sure the last line is at least
 * virtually terminated with a '\n'.
 */
static int
lex_getch(
	struct FILE_INFO *stream
	)
{
	int ch;

	if (NULL == stream || stream->force_eof)
		return EOF;

	if (EOF != stream->backch) {
		ch = stream->backch;
		stream->backch = EOF;
		if (stream->fpi)
			conf_file_sum += ch;
		stream->curpos.ncol++;
	} else if (stream->fpi) {
		/* fetch next 7-bit ASCII char (or EOF) from file */
		while ((ch = fgetc(stream->fpi)) != EOF && ch > SCHAR_MAX)
			stream->curpos.ncol++;
		if (EOF != ch) {
			conf_file_sum += ch;
			stream->curpos.ncol++;
		}
	} else {
		/* fetch next 7-bit ASCII char from buffer */
		const char * scan;
		scan = &remote_config.buffer[remote_config.pos];
		while ((ch = (u_char)*scan) > SCHAR_MAX) {
			scan++;
			stream->curpos.ncol++;
		}
		if ('\0' != ch) {
			scan++;
			stream->curpos.ncol++;
		} else {
			ch = EOF;
		}
		remote_config.pos = (int)(scan - remote_config.buffer);
	}

	/* If the last line ends without '\n', generate one. This
	 * happens most likely on Windows, where editors often have a
	 * sloppy concept of a line.
	 */
	if (EOF == ch && stream->curpos.ncol != 0)
		ch = '\n';

	/* update scan position tallies */
	if (ch == '\n') {
		stream->bakpos = stream->curpos;
		stream->curpos.nline++;
		stream->curpos.ncol = 0;
	}

	return ch;
}

/* Note: lex_ungetch will fail to track more than one line of push
 * back. But since it guarantees only one char of back storage anyway,
 * this should not be a problem.
 */
static int
lex_ungetch(
	int ch,
	struct FILE_INFO *stream
	)
{
	/* check preconditions */
	if (NULL == stream || stream->force_eof)
		return EOF;
	if (EOF != stream->backch || EOF == ch)
		return EOF;

	/* keep for later reference and update checksum */
	stream->backch = (u_char)ch;
	if (stream->fpi)
		conf_file_sum -= stream->backch;

	/* update position */
	if (stream->backch == '\n') {
	    stream->curpos = stream->bakpos;
	    stream->bakpos.ncol = -1;
	}
	stream->curpos.ncol--;
	return stream->backch;
}

/* dispose of an input structure. If the file pointer is not NULL, close
 * the file. This function does not check the result of 'fclose()'.
 */
static void
lex_close(
	struct FILE_INFO *stream
	)
{
	if (NULL != stream) {
		if (NULL != stream->fpi)
			fclose(stream->fpi);		
		free(stream);
	}
}

/* INPUT STACK
 * -----------
 *
 * Nested input sources are a bit tricky at first glance. We deal with
 * this problem using a stack of input sources, that is, a forward
 * linked list of FILE_INFO structs.
 *
 * This stack is never empty during parsing; while an encounter with EOF
 * can and will remove nested input sources, removing the last element
 * in the stack will not work during parsing, and the EOF condition of
 * the outermost input file remains until the parser folds up.
 */

static struct FILE_INFO *
_drop_stack_do(
	struct FILE_INFO * head
	)
{
	struct FILE_INFO * tail;
	while (NULL != head) {
		tail = head->st_next;
		lex_close(head);
		head = tail;
	}
	return head;
}



/* Create a singleton input source on an empty lexer stack. This will
 * fail if there is already an input source, or if the underlying disk
 * file cannot be opened.
 *
 * Returns TRUE if a new input object was successfully created.
 */
int/*BOOL*/
lex_init_stack(
	const char * path,
	const char * mode
	)
{
	if (NULL != lex_stack || NULL == path)
		return FALSE;

	lex_stack = lex_open(path, mode);
	return (NULL != lex_stack);
}

/* This removes *all* input sources from the stack, leaving the head
 * pointer as NULL. Any attempt to parse in that state is likely to bomb
 * with segmentation faults or the like.
 *
 * In other words: Use this to clean up after parsing, and do not parse
 * anything until the next 'lex_init_stack()' succeeded.
 */
void
lex_drop_stack()
{
	lex_stack = _drop_stack_do(lex_stack);
}

/* Flush the lexer input stack: This will nip all input objects on the
 * stack (but keeps the current top-of-stack) and marks the top-of-stack
 * as inactive. Any further calls to lex_getch yield only EOF, and it's
 * no longer possible to push something back.
 *
 * Returns TRUE if there is a head element (top-of-stack) that was not
 * in the force-eof mode before this call.
 */
int/*BOOL*/
lex_flush_stack()
{
	int retv = FALSE;

	if (NULL != lex_stack) {
		retv = !lex_stack->force_eof;
		lex_stack->force_eof = TRUE;
		lex_stack->st_next = _drop_stack_do(
					lex_stack->st_next);
	}
	return retv;
}

/* Push another file on the parsing stack. If the mode is NULL, create a
 * FILE_INFO suitable for in-memory parsing; otherwise, create a
 * FILE_INFO that is bound to a local/disc file. Note that 'path' must
 * not be NULL, or the function will fail.
 *
 * Returns TRUE if a new info record was pushed onto the stack.
 */
int/*BOOL*/ lex_push_file(
	const char * path,
	const char * mode
	)
{
	struct FILE_INFO * next = NULL;

	if (NULL != path) {
		next = lex_open(path, mode);
		if (NULL != next) {
			next->st_next = lex_stack;
			lex_stack = next;
		}
	}
	return (NULL != next);
}

/* Pop, close & free the top of the include stack, unless the stack
 * contains only a singleton input object. In that case the function
 * fails, because the parser does not expect the input stack to be
 * empty.
 *
 * Returns TRUE if an object was successfuly popped from the stack.
 */
int/*BOOL*/
lex_pop_file(void)
{
	struct FILE_INFO * head = lex_stack;
	struct FILE_INFO * tail = NULL; 
	
	if (NULL != head) {
		tail = head->st_next;
		if (NULL != tail) {
			lex_stack = tail;
			lex_close(head);
		}
	}
	return (NULL != tail);
}

/* Get include nesting level. This currently loops over the stack and
 * counts elements; but since this is of concern only with an include
 * statement and the nesting depth has a small limit, there's no
 * bottleneck expected here.
 *
 * Returns the nesting level of includes, that is, the current depth of
 * the lexer input stack.
 *
 * Note: 
 */
size_t
lex_level(void)
{
	size_t            cnt = 0;
	struct FILE_INFO *ipf = lex_stack;

	while (NULL != ipf) {
		cnt++;
		ipf = ipf->st_next;
	}
	return cnt;
}

/* check if the current input is from a file */	
int/*BOOL*/
lex_from_file(void)
{
	return (NULL != lex_stack) && (NULL != lex_stack->fpi);
}

struct FILE_INFO *
lex_current()
{
	/* this became so simple, it could be a macro. But then,
	 * lex_stack needed to be global...
	 */
	return lex_stack;
}


/* STATE MACHINES 
 * --------------
 */

/* Keywords */
static int
is_keyword(
	char *lexeme,
	follby *pfollowedby
	)
{
	follby fb;
	int curr_s;		/* current state index */
	int token;
	int i;

	curr_s = SCANNER_INIT_S;
	token = 0;

	for (i = 0; lexeme[i]; i++) {
		while (curr_s && (lexeme[i] != SS_CH(sst[curr_s])))
			curr_s = SS_OTHER_N(sst[curr_s]);

		if (curr_s && (lexeme[i] == SS_CH(sst[curr_s]))) {
			if ('\0' == lexeme[i + 1]
			    && FOLLBY_NON_ACCEPTING 
			       != SS_FB(sst[curr_s])) {
				fb = SS_FB(sst[curr_s]);
				*pfollowedby = fb;
				token = curr_s;
				break;
			}
			curr_s = SS_MATCH_N(sst[curr_s]);
		} else
			break;
	}

	return token;
}


/* Integer */
static int
is_integer(
	char *lexeme
	)
{
	int	i;
	int	is_neg;
	u_int	u_val;
	
	i = 0;

	/* Allow a leading minus sign */
	if (lexeme[i] == '-') {
		i++;
		is_neg = TRUE;
	} else {
		is_neg = FALSE;
	}

	/* Check that all the remaining characters are digits */
	for (; lexeme[i] != '\0'; i++) {
		if (!isdigit((u_char)lexeme[i]))
			return FALSE;
	}

	if (is_neg)
		return TRUE;

	/* Reject numbers that fit in unsigned but not in signed int */
	if (1 == sscanf(lexeme, "%u", &u_val))
		return (u_val <= INT_MAX);
	else
		return FALSE;
}


/* U_int -- assumes is_integer() has returned FALSE */
static int
is_u_int(
	char *lexeme
	)
{
	int	i;
	int	is_hex;
	
	i = 0;
	if ('0' == lexeme[i] && 'x' == tolower((u_char)lexeme[i + 1])) {
		i += 2;
		is_hex = TRUE;
	} else {
		is_hex = FALSE;
	}

	/* Check that all the remaining characters are digits */
	for (; lexeme[i] != '\0'; i++) {
		if (is_hex && !isxdigit((u_char)lexeme[i]))
			return FALSE;
		if (!is_hex && !isdigit((u_char)lexeme[i]))
			return FALSE;
	}

	return TRUE;
}


/* Double */
static int
is_double(
	char *lexeme
	)
{
	u_int num_digits = 0;  /* Number of digits read */
	u_int i;

	i = 0;

	/* Check for an optional '+' or '-' */
	if ('+' == lexeme[i] || '-' == lexeme[i])
		i++;

	/* Read the integer part */
	for (; lexeme[i] && isdigit((u_char)lexeme[i]); i++)
		num_digits++;

	/* Check for the optional decimal point */
	if ('.' == lexeme[i]) {
		i++;
		/* Check for any digits after the decimal point */
		for (; lexeme[i] && isdigit((u_char)lexeme[i]); i++)
			num_digits++;
	}

	/*
	 * The number of digits in both the decimal part and the
	 * fraction part must not be zero at this point 
	 */
	if (!num_digits)
		return 0;

	/* Check if we are done */
	if (!lexeme[i])
		return 1;

	/* There is still more input, read the exponent */
	if ('e' == tolower((u_char)lexeme[i]))
		i++;
	else
		return 0;

	/* Read an optional Sign */
	if ('+' == lexeme[i] || '-' == lexeme[i])
		i++;

	/* Now read the exponent part */
	while (lexeme[i] && isdigit((u_char)lexeme[i]))
		i++;

	/* Check if we are done */
	if (!lexeme[i])
		return 1;
	else
		return 0;
}


/* is_special() - Test whether a character is a token */
static inline int
is_special(
	int ch
	)
{
	return strchr(special_chars, ch) != NULL;
}


static int
is_EOC(
	int ch
	)
{
	if ((old_config_style && (ch == '\n')) ||
	    (!old_config_style && (ch == ';')))
		return 1;
	return 0;
}


char *
quote_if_needed(char *str)
{
	char *ret;
	size_t len;
	size_t octets;

	len = strlen(str);
	octets = len + 2 + 1;
	ret = emalloc(octets);
	if ('"' != str[0] 
	    && (strcspn(str, special_chars) < len 
		|| strchr(str, ' ') != NULL)) {
		snprintf(ret, octets, "\"%s\"", str);
	} else
		strlcpy(ret, str, octets);

	return ret;
}


static int
create_string_token(
	char *lexeme
	)
{
	char *pch;

	/*
	 * ignore end of line whitespace
	 */
	pch = lexeme;
	while (*pch && isspace((u_char)*pch))
		pch++;

	if (!*pch) {
		yylval.Integer = T_EOC;
		return yylval.Integer;
	}

	yylval.String = estrdup(lexeme);
	return T_String;
}


/*
 * yylex() - function that does the actual scanning.
 * Bison expects this function to be called yylex and for it to take no
 * input and return an int.
 * Conceptually yylex "returns" yylval as well as the actual return
 * value representing the token or type.
 */
int
yylex(void)
{
	static follby	followedby = FOLLBY_TOKEN;
	size_t		i;
	int		instring;
	int		yylval_was_set;
	int		converted;
	int		token;		/* The return value */
	int		ch;

	instring = FALSE;
	yylval_was_set = FALSE;

	do {
		/* Ignore whitespace at the beginning */
		while (EOF != (ch = lex_getch(lex_stack)) &&
		       isspace(ch) &&
		       !is_EOC(ch))

			; /* Null Statement */

		if (EOF == ch) {

			if ( ! lex_pop_file())
				return 0;
			token = T_EOC;
			goto normal_return;

		} else if (is_EOC(ch)) {

			/* end FOLLBY_STRINGS_TO_EOC effect */
			followedby = FOLLBY_TOKEN;
			token = T_EOC;
			goto normal_return;

		} else if (is_special(ch) && FOLLBY_TOKEN == followedby) {
			/* special chars are their own token values */
			token = ch;
			/*
			 * '=' outside simulator configuration implies
			 * a single string following as in:
			 * setvar Owner = "The Boss" default
			 */
			if ('=' == ch && old_config_style)
				followedby = FOLLBY_STRING;
			yytext[0] = (char)ch;
			yytext[1] = '\0';
			goto normal_return;
		} else
			lex_ungetch(ch, lex_stack);

		/* save the position of start of the token */
		lex_stack->tokpos = lex_stack->curpos;

		/* Read in the lexeme */
		i = 0;
		while (EOF != (ch = lex_getch(lex_stack))) {

			yytext[i] = (char)ch;

			/* Break on whitespace or a special character */
			if (isspace(ch) || is_EOC(ch) 
			    || '"' == ch
			    || (FOLLBY_TOKEN == followedby
				&& is_special(ch)))
				break;

			/* Read the rest of the line on reading a start
			   of comment character */
			if ('#' == ch) {
				while (EOF != (ch = lex_getch(lex_stack))
				       && '\n' != ch)
					; /* Null Statement */
				break;
			}

			i++;
			if (i >= COUNTOF(yytext))
				goto lex_too_long;
		}
		/* Pick up all of the string inside between " marks, to
		 * end of line.  If we make it to EOL without a
		 * terminating " assume it for them.
		 *
		 * XXX - HMS: I'm not sure we want to assume the closing "
		 */
		if ('"' == ch) {
			instring = TRUE;
			while (EOF != (ch = lex_getch(lex_stack)) &&
			       ch != '"' && ch != '\n') {
				yytext[i++] = (char)ch;
				if (i >= COUNTOF(yytext))
					goto lex_too_long;
			}
			/*
			 * yytext[i] will be pushed back as not part of
			 * this lexeme, but any closing quote should
			 * not be pushed back, so we read another char.
			 */
			if ('"' == ch)
				ch = lex_getch(lex_stack);
		}
		/* Pushback the last character read that is not a part
		 * of this lexeme. This fails silently if ch is EOF,
		 * but then the EOF condition persists and is handled on
		 * the next turn by the include stack mechanism.
		 */
		lex_ungetch(ch, lex_stack);

		yytext[i] = '\0';
	} while (i == 0);

	/* Now return the desired token */
	
	/* First make sure that the parser is *not* expecting a string
	 * as the next token (based on the previous token that was
	 * returned) and that we haven't read a string.
	 */
	
	if (followedby == FOLLBY_TOKEN && !instring) {
		token = is_keyword(yytext, &followedby);
		if (token) {
			/*
			 * T_Server is exceptional as it forces the
			 * following token to be a string in the
			 * non-simulator parts of the configuration,
			 * but in the simulator configuration section,
			 * "server" is followed by "=" which must be
			 * recognized as a token not a string.
			 */
			if (T_Server == token && !old_config_style)
				followedby = FOLLBY_TOKEN;
			goto normal_return;
		} else if (is_integer(yytext)) {
			yylval_was_set = TRUE;
			errno = 0;
			if ((yylval.Integer = strtol(yytext, NULL, 10)) == 0
			    && ((errno == EINVAL) || (errno == ERANGE))) {
				msyslog(LOG_ERR, 
					"Integer cannot be represented: %s",
					yytext);
				if (lex_from_file()) {
					exit(1);
				} else {
					/* force end of parsing */
					yylval.Integer = 0;
					return 0;
				}
			}
			token = T_Integer;
			goto normal_return;
		} else if (is_u_int(yytext)) {
			yylval_was_set = TRUE;
			if ('0' == yytext[0] &&
			    'x' == tolower((unsigned long)yytext[1]))
				converted = sscanf(&yytext[2], "%x",
						   &yylval.U_int);
			else
				converted = sscanf(yytext, "%u",
						   &yylval.U_int);
			if (1 != converted) {
				msyslog(LOG_ERR, 
					"U_int cannot be represented: %s",
					yytext);
				if (lex_from_file()) {
					exit(1);
				} else {
					/* force end of parsing */
					yylval.Integer = 0;
					return 0;
				}
			}
			token = T_U_int;
			goto normal_return;
		} else if (is_double(yytext)) {
			yylval_was_set = TRUE;
			errno = 0;
			if ((yylval.Double = atof(yytext)) == 0 && errno == ERANGE) {
				msyslog(LOG_ERR,
					"Double too large to represent: %s",
					yytext);
				exit(1);
			} else {
				token = T_Double;
				goto normal_return;
			}
		} else {
			/* Default: Everything is a string */
			yylval_was_set = TRUE;
			token = create_string_token(yytext);
			goto normal_return;
		}
	}

	/*
	 * Either followedby is not FOLLBY_TOKEN or this lexeme is part
	 * of a string.  Hence, we need to return T_String.
	 * 
	 * _Except_ we might have a -4 or -6 flag on a an association
	 * configuration line (server, peer, pool, etc.).
	 *
	 * This is a terrible hack, but the grammar is ambiguous so we
	 * don't have a choice.  [SK]
	 *
	 * The ambiguity is in the keyword scanner, not ntp_parser.y.
	 * We do not require server addresses be quoted in ntp.conf,
	 * complicating the scanner's job.  To avoid trying (and
	 * failing) to match an IP address or DNS name to a keyword,
	 * the association keywords use FOLLBY_STRING in the keyword
	 * table, which tells the scanner to force the next token to be
	 * a T_String, so it does not try to match a keyword but rather
	 * expects a string when -4/-6 modifiers to server, peer, etc.
	 * are encountered.
	 * restrict -4 and restrict -6 parsing works correctly without
	 * this hack, as restrict uses FOLLBY_TOKEN.  [DH]
	 */
	if ('-' == yytext[0]) {
		if ('4' == yytext[1]) {
			token = T_Ipv4_flag;
			goto normal_return;
		} else if ('6' == yytext[1]) {
			token = T_Ipv6_flag;
			goto normal_return;
		}
	}

	if (FOLLBY_STRING == followedby)
		followedby = FOLLBY_TOKEN;

	yylval_was_set = TRUE;
	token = create_string_token(yytext);

normal_return:
	if (T_EOC == token)
		DPRINTF(4,("\t<end of command>\n"));
	else
		DPRINTF(4, ("yylex: lexeme '%s' -> %s\n", yytext,
			    token_name(token)));

	if (!yylval_was_set)
		yylval.Integer = token;

	return token;

lex_too_long:
	yytext[min(sizeof(yytext) - 1, 50)] = 0;
	msyslog(LOG_ERR, 
		"configuration item on line %d longer than limit of %lu, began with '%s'",
		lex_stack->curpos.nline, (u_long)min(sizeof(yytext) - 1, 50),
		yytext);

	/*
	 * If we hit the length limit reading the startup configuration
	 * file, abort.
	 */
	if (lex_from_file())
		exit(sizeof(yytext) - 1);

	/*
	 * If it's runtime configuration via ntpq :config treat it as
	 * if the configuration text ended before the too-long lexeme,
	 * hostname, or string.
	 */
	yylval.Integer = 0;
	return 0;
}
