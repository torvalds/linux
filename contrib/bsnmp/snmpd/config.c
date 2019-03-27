/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: bsnmp/snmpd/config.c,v 1.25 2006/02/14 09:04:20 brandt_h Exp $
 *
 * Parse configuration file.
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>
#include <netdb.h>
#include <setjmp.h>
#include <inttypes.h>

#include "snmpmod.h"
#include "snmpd.h"
#include "tree.h"

/*
#define DEBUGGING
*/

/*
 * config_file: EMPTY | config_file line
 *
 * line: oid '=' value
 *     | '%' STRING
 *     | STRING := REST_OF_LINE
 *     | STRING ?= REST_OF_LINE
 *     | . INCLUDE STRING
 *
 * oid: STRING suboid
 *
 * suboid: EMPTY | suboid '.' subid
 *
 * subid: NUM | STRING | '[' STRING ']'
 *
 * value: EMPTY | STRING | NUM
 */

/*
 * Input context for macros and includes
 */
enum input_type {
	INPUT_FILE	= 1,
	INPUT_STRING
};
struct input {
	enum input_type	type;
	union {
	    struct {
		FILE	*fp;
		char	*filename;
		u_int	lno;
	    }		file;
	    struct {
		char	*macro;
		char	*str;
		char	*ptr;
		size_t	left;
	    }		str;
	} u;
	LIST_ENTRY(input) link;
};
static LIST_HEAD(, input) inputs;

#define input_fp	u.file.fp
#define input_filename	u.file.filename
#define input_lno	u.file.lno
#define input_macro	u.str.macro
#define input_str	u.str.str
#define input_ptr	u.str.ptr
#define input_left	u.str.left

static int input_push;
static int input_buf[2];

/*
 * Configuration data. The configuration file is handled as one single
 * SNMP transaction. So we need to keep the assignment data for the
 * commit or rollback pass. Note, that dependencies and finish functions
 * are NOT allowed here.
 */
struct assign {
	struct snmp_value value;
	struct snmp_scratch scratch;
	const char *node_name;

	TAILQ_ENTRY(assign) link;
};
static TAILQ_HEAD(assigns, assign) assigns = TAILQ_HEAD_INITIALIZER(assigns);


static struct snmp_context *snmp_ctx;

struct macro {
	char	*name;
	char	*value;
	size_t	length;
	LIST_ENTRY(macro) link;
	int	perm;
};
static LIST_HEAD(, macro) macros = LIST_HEAD_INITIALIZER(macros);

enum {
	TOK_EOF	= 0200,
	TOK_EOL,
	TOK_NUM,
	TOK_STR,
	TOK_HOST,
	TOK_ASSIGN,
	TOK_QASSIGN,
};

/* lexer values and last token */
static uint64_t	numval;
static char	strval[_POSIX2_LINE_MAX];
static size_t	strvallen;
static int	token;

/* error return */
static jmp_buf	errjmp[4];
static volatile int errstk;

# define ERRPUSH()	(setjmp(errjmp[errstk++]))
# define ERRPOP()	((void)(errstk--))
# define ERRNEXT()	(longjmp(errjmp[--errstk], 1))
# define ERR()		(longjmp(errjmp[--errstk], 1))

/* section context */
static int ignore;

/*
 * Report an error and jump to the error label
 */
static void report(const char *fmt, ...) __dead2 __printflike(1, 2);

static void
report(const char *fmt, ...)
{
	va_list ap;
	const struct input *input;

	va_start(ap, fmt);
	vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);

	LIST_FOREACH(input, &inputs, link) {
		switch (input->type) {

		  case INPUT_FILE:
			syslog(LOG_ERR, "  in file %s line %u",
			    input->input_filename, input->input_lno);
			break;

		  case INPUT_STRING:
			syslog(LOG_ERR, "  in macro %s pos %td",
			    input->input_macro,
			    input->input_ptr - input->input_str);
			break;
		}
	}
	ERR();
}

/*
 * Open a file for input
 */
static int
input_open_file(const char *fname, int sysdir)
{
	struct input *input;
	FILE *fp;
	char path[PATH_MAX + 1];
	const char *col;
	const char *ptr;

	if (sysdir) {
		ptr = syspath;
		fp = NULL;
		while (*ptr != '\0') {
			if ((col = strchr(ptr, ':')) == NULL) {
				snprintf(path, sizeof(path), "%s/%s",
				    ptr, fname);
				col = ptr + strlen(ptr) - 1;
			} else if (col == ptr)
				snprintf(path, sizeof(path), "./%s", fname);
			else
				snprintf(path, sizeof(path), "%.*s/%s",
				    (int)(col - ptr), ptr, fname);
			if ((fp = fopen(path, "r")) != NULL)
				break;
			ptr = col + 1;
		}
	} else
		fp = fopen(fname, "r");

	if (fp == NULL)
		report("%s: %m", fname);

	if ((input = malloc(sizeof(*input))) == NULL) {
		fclose(fp);
		return (-1);
	}
	if ((input->input_filename = malloc(strlen(fname) + 1)) == NULL) {
		fclose(fp);
		free(input);
		return (-1);
	}
	strcpy(input->input_filename, fname);
	input->input_fp = fp;
	input->input_lno = 1;
	input->type = INPUT_FILE;
	LIST_INSERT_HEAD(&inputs, input, link);
	return (0);
}

/*
 * Make a macro the next input
 */
static void
input_open_macro(struct macro *m)
{
	struct input *input;

	if ((input = malloc(sizeof(*input))) == NULL)
		report("%m");
	input->type = INPUT_STRING;
	input->input_macro = m->name;
	if ((input->input_str = malloc(m->length)) == NULL) {
		free(input);
		report("%m");
	}
	memcpy(input->input_str, m->value, m->length);
	input->input_ptr = input->input_str;
	input->input_left = m->length;
	LIST_INSERT_HEAD(&inputs, input, link);
}

/*
 * Close top input source
 */
static void
input_close(void)
{
	struct input *input;

	if ((input = LIST_FIRST(&inputs)) == NULL)
		abort();
	switch (input->type) {

	  case INPUT_FILE:
		fclose(input->input_fp);
		free(input->input_filename);
		break;

	  case INPUT_STRING:
		free(input->input_str);
		break;
	}
	LIST_REMOVE(input, link);
	free(input);
}

/*
 * Close all inputs
 */
static void
input_close_all(void)
{
	while (!LIST_EMPTY(&inputs))
		input_close();
}

/*
 * Push back one character
 */
static void
input_ungetc(int c)
{
	if (c == EOF)
		report("pushing EOF");
	if (input_push == 2)
		report("pushing third char");
	input_buf[input_push++] = c;
}


/*
 * Return next character from the input without preprocessing.
 */
static int
input_getc_raw(void)
{
	int c;
	struct input *input;

	if (input_push != 0) {
		c = input_buf[--input_push];
		goto ok;
	}
	while ((input = LIST_FIRST(&inputs)) != NULL) {
		switch (input->type) {

		  case INPUT_FILE:
			if ((c = getc(input->input_fp)) == EOF) {
				if (ferror(input->input_fp))
					report("read error: %m");
				input_close();
				break;
			}
			if (c == '\n')
				input->input_lno++;
			goto ok;

		  case INPUT_STRING:
			if (input->input_left-- == 0) {
				input_close();
				break;
			}
			c = *input->input_ptr++;
			goto ok;
		}
	}
# ifdef DEBUGGING
	fprintf(stderr, "EOF");
# endif
	return (EOF);

  ok:
# ifdef DEBUGGING
	if (!isascii(c) || !isprint(c))
		fprintf(stderr, "'%#2x'", c);
	else
		fprintf(stderr, "'%c'", c);
# endif
	return (c);
}

/*
 * Get character with and \\n -> processing.
 */
static int
input_getc_plain(void)
{
	int c;

  again:
	if ((c = input_getc_raw()) == '\\') {
		if ((c = input_getc_raw()) == '\n')
			goto again;
		if (c != EOF)
			input_ungetc(c);
		return ('\\');
	}
	return (c);
}

/*
 * Get next character with substitution of macros
 */
static int
input_getc(void)
{
	int c;
	struct macro *m;
	char	name[_POSIX2_LINE_MAX];
	size_t	namelen;

  again:
	if ((c = input_getc_plain()) != '$')
		return (c);

	if ((c = input_getc()) == EOF)
		report("unexpected EOF");
	if (c != '(')
		report("expecting '(' after '$'");

	namelen = 0;
	while ((c = input_getc()) != EOF && c != ')') {
		if (isalpha(c) || c == '_' || (namelen != 0 && isdigit(c)))
			name[namelen++] = c;
		else
			goto badchar;
	}
	if (c == EOF)
		report("unexpected EOF");
	name[namelen++] = '\0';

	LIST_FOREACH(m, &macros, link)
		if (strcmp(m->name, name) == 0)
			break;
	if (m == NULL)
		report("undefined macro '%s'", name);

	input_open_macro(m);
	goto again;

  badchar:
	if (!isascii(c) || !isprint(c))
		report("unexpected character %#2x", (u_int)c);
	else
		report("bad character '%c'", c);
}


static void
input_getnum(u_int base, u_int flen)
{
	int c;
	u_int cnt;

	cnt = 0;
	numval = 0;
	while (flen == 0 || cnt < flen) {
		if ((c = input_getc()) == EOF) {
			if (cnt == 0)
				report("bad number");
			return;
		}
		if (isdigit(c)) {
			if (base == 8 && (c == '8' || c == '9')) {
				input_ungetc(c);
				if (cnt == 0)
					report("bad number");
				return;
			}
			numval = numval * base + (c - '0');
		} else if (base == 16 && isxdigit(c)) {
			if (islower(c))
				numval = numval * base + (c - 'a' + 10);
			else
				numval = numval * base + (c - 'A' + 10);
		} else {
			input_ungetc(c);
			if (cnt == 0)
				report("bad number");
			return;
		}
		cnt++;
	}
}

static int
# ifdef DEBUGGING
_gettoken(void)
# else
gettoken(void)
# endif
{
	int c;
	char *end;
	static const char esc[] = "abfnrtv";
	static const char chr[] = "\a\b\f\n\r\t\v";

	/*
	 * Skip any whitespace before the next token
	 */
	while ((c = input_getc()) != EOF) {
		if (!isspace(c) || c == '\n')
			break;
	}
	if (c == EOF)
		return (token = TOK_EOF);
	if (!isascii(c))
		goto badchar;

	/*
	 * Skip comments
	 */
	if (c == '#') {
		while ((c = input_getc_plain()) != EOF) {
			if (c == '\n')
				return (token = TOK_EOL);
		}
		goto badeof;
	}

	/*
	 * Single character tokens
	 */
	if (c == '\n')
		return (token = TOK_EOL);
	if (c == '.' || c == '%' || c == '=' || c == '<' || c == '>')
		return (token = c);
	if (c == ':') {
		if ((c = input_getc()) == '=')
			return (token = TOK_ASSIGN);
		input_ungetc(c);
		return (token = ':');
	}
	if (c == '?') {
		if ((c = input_getc()) == '=')
			return (token = TOK_QASSIGN);
		input_ungetc(c);
		goto badchar;
	}

	/*
	 * Sort out numbers
	 */
	if (isdigit(c)) {
		if (c == '0') {
			if ((c = input_getc()) == 'x' || c == 'X') {
				input_getnum(16, 0);
			} else if (isdigit(c)) {
				input_ungetc(c);
				input_getnum(8, 0);
			} else {
				if (c != EOF)
					input_ungetc(c);
				numval = 0;
				c = 1;
			}
		} else {
			input_ungetc(c);
			input_getnum(10, 0);
		}
		return (token = TOK_NUM);
	}

	/*
	 * Must be a string then
	 */
	strvallen = 0;

# define GETC(C) do {							\
	if ((c = input_getc()) == EOF)					\
		goto badeof;						\
	if (!isascii(c) || (!isprint(c) && c != '\t')) 			\
		goto badchar;						\
} while(0)

	if (c == '"') {
		for(;;) {
			GETC(c);
			if (c == '"') {
				strval[strvallen] = '\0';
				break;
			}
			if (c != '\\') {
				strval[strvallen++] = c;
				continue;
			}
			GETC(c);
			if ((end = strchr(esc, c)) != NULL) {
				strval[strvallen++] = chr[end - esc];
				continue;
			}
			if (c == 'x') {
				input_getnum(16, 2);
				c = numval;
			} else if (c >= '0' && c <= '7') {
				input_ungetc(c);
				input_getnum(8, 3);
				c = numval;
			}
			strval[strvallen++] = c;
		}
# undef GETC

	} else if (c == '[') {
		/*
		 * Skip leading space
		 */
		while ((c = input_getc()) != EOF && isspace(c))
			;
		if (c == EOF)
			goto badeof;
		while (c != ']' && !isspace(c)) {
			if (!isalnum(c) && c != '.' && c != '-')
				goto badchar;
			strval[strvallen++] = c;
			if ((c = input_getc()) == EOF)
				goto badeof;
		}
		while (c != ']' && isspace(c)) {
			if ((c = input_getc()) == EOF)
				goto badeof;
		}
		if (c != ']')
			goto badchar;
		strval[strvallen] = '\0';
		return (token = TOK_HOST);

	} else if (!isalpha(c) && c != '_') {
		goto badchar;

	} else {
		for (;;) {
			strval[strvallen++] = c;
			if ((c = input_getc()) == EOF)
				goto badeof;
			if (!isalnum(c) && c != '_' && c != '-') {
				input_ungetc(c);
				strval[strvallen] = '\0';
				break;
			}
		}
	}

	return (token = TOK_STR);

  badeof:
	report("unexpected EOF");

  badchar:
	if (!isascii(c) || !isprint(c))
		report("unexpected character %#2x", (u_int)c);
	else
		report("bad character '%c'", c);
}

# ifdef DEBUGGING
static int
gettoken()
{
	_gettoken();
	if (isascii(token) && isprint(token))
		printf("(%c)", token);
	else {
		switch (token) {

		  case TOK_EOF:
			printf("(EOF)");
			break;
		  case TOK_EOL:
			printf("(EOL)");
			break;
		  case TOK_NUM:
			printf("(NUM %ju)", (uintmax_t)numval);
			break;
		  case TOK_STR:
			printf("(STR %.*s)", (int)strvallen, strval);
			break;
		  case TOK_HOST:
			printf("(HOST %s)", strval);
			break;
		  default:
			printf("(%#2x)", token);
			break;
		}
	}
	return (token);
}
#endif


/*
 * Try to execute the assignment.
 */
static void
handle_assignment(const struct snmp_node *node, struct asn_oid *vindex,
    const struct snmp_value *value)
{
	u_int i;
	int err;
	struct assign *tp;
	char nodename[100];

	if (node->type == SNMP_NODE_LEAF) {
		/* index must be one single zero or no index at all */
		if (vindex->len > 1 || (vindex->len == 1 &&
		    vindex->subs[0] != 0))
			report("bad index on leaf node");
		vindex->len = 1;
		vindex->subs[0] = 0;
	} else {
		/* resulting oid must not be too long */
		if (node->oid.len + vindex->len > ASN_MAXOIDLEN)
			report("resulting OID too long");
	}

	/*
	 * Get the next assignment entry for the transaction.
	 */
	if ((tp = malloc(sizeof(*tp))) == NULL)
		report("%m");

	tp->value = *value;
	tp->node_name = node->name;

	/*
	 * Build the OID
	 */
	tp->value.var = node->oid;
	for (i = 0; i < vindex->len; i++)
		tp->value.var.subs[tp->value.var.len++] = vindex->subs[i];

	/*
	 * Puzzle together the variables for the call and call the
	 * set routine. The set routine may make our node pointer
	 * invalid (if we happend to call the module loader) so
	 * get a copy of the node name beforehands.
	 */
	snprintf(nodename, sizeof(nodename), "%s", node->name);
	snmp_ctx->scratch = &tp->scratch;
	snmp_ctx->var_index = 0;
	err = (*node->op)(snmp_ctx, &tp->value, node->oid.len, node->index,
	    SNMP_OP_SET);
	if (err != 0) {
		free(tp);
		report("assignment to %s.%s returns %d", nodename,
		    asn_oid2str(vindex), err);
	}

	TAILQ_INSERT_TAIL(&assigns, tp, link);
}


/*
 * Parse the section statement
 */
static void
parse_section(const struct lmodule *mod)
{
	if (token != TOK_STR)
		report("expecting section name");

	if (strcmp(strval, "snmpd") == 0) {
		if (mod != NULL)
			/* loading a module - ignore common stuff */
			ignore = 1;
		else
			/* global configuration - don't ignore */
			ignore = 0;
	} else {
		if (mod == NULL) {
			/* global configuration - ignore module stuff */
			ignore = 1;
		} else {
			/* loading module - check if it's our section */
			ignore = (strcmp(strval, mod->section) != 0);
		}
	}
	gettoken();
}

/*
 * Convert a hostname to four u_chars
 */
static void
gethost(const char *host, u_char *ip)
{
	struct addrinfo hints, *res;
	int error;
	struct sockaddr_in *sain;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_PASSIVE;
	error = getaddrinfo(host, NULL, &hints, &res);
	if (error != 0)
		report("%s: %s", host, gai_strerror(error));
	if (res == NULL)
		report("%s: unknown hostname", host);

	sain = (struct sockaddr_in *)(void *)res->ai_addr;
	sain->sin_addr.s_addr = ntohl(sain->sin_addr.s_addr);
	ip[0] = sain->sin_addr.s_addr >> 24;
	ip[1] = sain->sin_addr.s_addr >> 16;
	ip[2] = sain->sin_addr.s_addr >>  8;
	ip[3] = sain->sin_addr.s_addr >>  0;

	freeaddrinfo(res);
}

/*
 * Parse the left hand side of a config line.
 */
static const struct snmp_node *
parse_oid(const char *varname, struct asn_oid *oid)
{
	struct snmp_node *node;
	u_int i;
	u_char ip[4];
	struct asn_oid str_oid;

	for (node = tree; node < &tree[tree_size]; node++)
		if (strcmp(varname, node->name) == 0)
			break;
	if (node == &tree[tree_size])
		node = NULL;

	oid->len = 0;
	while (token == '.') {
		if (gettoken() == TOK_NUM) {
			if (numval > ASN_MAXID)
				report("subid too large %#jx",
				    (uintmax_t)numval);
			if (oid->len == ASN_MAXOIDLEN)
				report("index too long");
			if (gettoken() != ':')
				oid->subs[oid->len++] = numval;
			else {
				str_oid.len = 0;
				str_oid.subs[str_oid.len++] = numval;
				while (gettoken() == TOK_NUM) {
					str_oid.subs[str_oid.len++] = numval;
					if (gettoken() != ':')
						break;
				}
				oid->subs[oid->len++] = str_oid.len;
				asn_append_oid(oid, &str_oid);
			}

		} else if (token == TOK_STR) {
			if (strvallen + oid->len + 1 > ASN_MAXOIDLEN)
				report("oid too long");
			oid->subs[oid->len++] = strvallen;
			for (i = 0; i < strvallen; i++)
				oid->subs[oid->len++] = strval[i];
			gettoken();

		} else if (token == TOK_HOST) {
			gethost(strval, ip);
			if (oid->len + 4 > ASN_MAXOIDLEN)
				report("index too long");
			for (i = 0; i < 4; i++)
				oid->subs[oid->len++] = ip[i];
			gettoken();
		} else
			report("bad token in index");
	}

	return (node);
}

/*
 * Parse the value for an assignment.
 */
static void
parse_syntax_null(struct snmp_value *value __unused)
{
	if (token != TOK_EOL)
		report("bad NULL syntax");
}

static void
parse_syntax_integer(struct snmp_value *value)
{
	if (token != TOK_NUM)
		report("bad INTEGER syntax");
	if (numval > 0x7fffffff)
		report("INTEGER too large %ju", (uintmax_t)numval);

	value->v.integer = numval;
	gettoken();
}

static void
parse_syntax_counter64(struct snmp_value *value)
{
	if (token != TOK_NUM)
		report("bad COUNTER64 syntax");

	value->v.counter64 = numval;
	gettoken();
}

static void
parse_syntax_octetstring(struct snmp_value *value)
{
	u_long alloc;
	u_char *noct;

	if (token == TOK_STR) {
		value->v.octetstring.len = strvallen;
		value->v.octetstring.octets = malloc(strvallen);
		(void)memcpy(value->v.octetstring.octets, strval, strvallen);
		gettoken();
		return;
	}

	/* XX:XX:XX syntax */
	value->v.octetstring.octets = NULL;
	value->v.octetstring.len = 0;

	if (token != TOK_NUM)
		/* empty string is allowed */
		return;

	if (ERRPUSH()) {
		free(value->v.octetstring.octets);
		ERRNEXT();
	}

	alloc = 0;
	for (;;) {
		if (token != TOK_NUM)
			report("bad OCTETSTRING syntax");
		if (numval > 0xff)
			report("byte value too large");
		if (alloc == value->v.octetstring.len) {
			alloc += 100;
			noct = realloc(value->v.octetstring.octets, alloc);
			if (noct == NULL)
				report("%m");
			value->v.octetstring.octets = noct;
		}
		value->v.octetstring.octets[value->v.octetstring.len++]
		    = numval;
		if (gettoken() != ':')
			break;
		gettoken();
	}
	ERRPOP();
}

static void
parse_syntax_oid(struct snmp_value *value)
{
	value->v.oid.len = 0;

	if (token != TOK_NUM)
		return;

	for (;;) {
		if (token != TOK_NUM)
			report("bad OID syntax");
		if (numval > ASN_MAXID)
			report("subid too large");
		if (value->v.oid.len == ASN_MAXOIDLEN)
			report("OID too long");
		value->v.oid.subs[value->v.oid.len++] = numval;
		if (gettoken() != '.')
			break;
		gettoken();
	}
}

static void
parse_syntax_ipaddress(struct snmp_value *value)
{
	int i;
	u_char ip[4];

	if (token == TOK_NUM) {
		/* numerical address */
		i = 0;
		for (;;) {
			if (numval >= 256)
				report("ip address part too large");
			value->v.ipaddress[i++] = numval;
			if (i == 4)
				break;
			if (gettoken() != '.')
				report("expecting '.' in ip address");
		}
		gettoken();

	} else if (token == TOK_HOST) {
		/* host name */
		gethost(strval, ip);
		for (i = 0; i < 4; i++)
			value->v.ipaddress[i] = ip[i];
		gettoken();

	} else
		report("bad ip address syntax");
}

static void
parse_syntax_uint32(struct snmp_value *value)
{

	if (token != TOK_NUM)
		report("bad number syntax");
	if (numval > 0xffffffff)
		report("number too large");
	value->v.uint32 = numval;
	gettoken();
}

/*
 * Parse an assignement line
 */
static void
parse_assign(const char *varname)
{
	struct snmp_value value;
	struct asn_oid vindex;
	const struct snmp_node *node;

	node = parse_oid(varname, &vindex);
	if (token != '=')
		report("'=' expected, got '%c'", token);
	gettoken();

	if (ignore) {
		/* skip rest of line */
		while (token != TOK_EOL && token != TOK_EOF)
			gettoken();
		return;
	}
	if (node == NULL)
		report("unknown variable");

	switch (value.syntax = node->syntax) {

	  case SNMP_SYNTAX_NULL:
		parse_syntax_null(&value);
		break;

	  case SNMP_SYNTAX_INTEGER:
		parse_syntax_integer(&value);
		break;

	  case SNMP_SYNTAX_COUNTER64:
		parse_syntax_counter64(&value);
		break;

	  case SNMP_SYNTAX_OCTETSTRING:
		parse_syntax_octetstring(&value);
		break;

	  case SNMP_SYNTAX_OID:
		parse_syntax_oid(&value);
		break;

	  case SNMP_SYNTAX_IPADDRESS:
		parse_syntax_ipaddress(&value);
		break;

	  case SNMP_SYNTAX_COUNTER:
	  case SNMP_SYNTAX_GAUGE:
	  case SNMP_SYNTAX_TIMETICKS:
		parse_syntax_uint32(&value);
		break;

	  case SNMP_SYNTAX_NOSUCHOBJECT:
	  case SNMP_SYNTAX_NOSUCHINSTANCE:
	  case SNMP_SYNTAX_ENDOFMIBVIEW:
		abort();
	}

	if (ERRPUSH()) {
		snmp_value_free(&value);
		ERRNEXT();
	}

	handle_assignment(node, &vindex, &value);

	ERRPOP();
}

/*
 * Handle macro definition line
 * We have already seen the := and the input now stands at the character
 * after the =. Skip whitespace and then call the input routine directly to
 * eat up characters.
 */
static void
parse_define(const char *varname)
{
	char *volatile string;
	char *new;
	volatile size_t alloc, length;
	int c;
	struct macro *m;
	int t = token;

	alloc = 100;
	length = 0;
	if ((string = malloc(alloc)) == NULL)
		report("%m");

	if (ERRPUSH()) {
		free(string);
		ERRNEXT();
	}

	while ((c = input_getc_plain()) != EOF) {
		if (c == '\n' || !isspace(c))
			break;
	}

	while (c != EOF && c != '#' && c != '\n') {
		if (alloc == length) {
			alloc *= 2;
			if ((new = realloc(string, alloc)) == NULL)
				report("%m");
			string = new;
		}
		string[length++] = c;
		c = input_getc_plain();
	}
	if (c == '#') {
		while ((c = input_getc_plain()) != EOF && c != '\n')
			;
	}
	if (c == EOF)
		report("EOF in macro definition");

	LIST_FOREACH(m, &macros, link)
		if (strcmp(m->name, varname) == 0)
			break;

	if (m == NULL) {
		if ((m = malloc(sizeof(*m))) == NULL)
			report("%m");
		if ((m->name = malloc(strlen(varname) + 1)) == NULL) {
			free(m);
			report("%m");
		}
		strcpy(m->name, varname);
		m->perm = 0;
		LIST_INSERT_HEAD(&macros, m, link);

		m->value = string;
		m->length = length;
	} else {
		if (t == TOK_ASSIGN) {
			free(m->value);
			m->value = string;
			m->length = length;
		} else
			free(string);
	}

	token = TOK_EOL;

	ERRPOP();
}

/*
 * Free all macros
 */
static void
macro_free_all(void)
{
	static struct macro *m, *m1;

	m = LIST_FIRST(&macros);
	while (m != NULL) {
		m1 = LIST_NEXT(m, link);
		if (!m->perm) {
			free(m->name);
			free(m->value);
			LIST_REMOVE(m, link);
			free(m);
		}
		m = m1;
	}
}

/*
 * Parse an include directive and switch to the new file
 */
static void
parse_include(void)
{
	int sysdir = 0;
	char fname[_POSIX2_LINE_MAX];

	if (gettoken() == '<') {
		sysdir = 1;
		if (gettoken() != TOK_STR)
			report("expecting filename after in .include");
	} else if (token != TOK_STR)
		report("expecting filename after in .include");

	strcpy(fname, strval);
	if (sysdir && gettoken() != '>')
		report("expecting '>'");
	gettoken();
	if (input_open_file(fname, sysdir) == -1)
		report("%s: %m", fname);
}

/*
 * Parse the configuration file
 */
static void
parse_file(const struct lmodule *mod)
{
	char varname[_POSIX2_LINE_MAX];

	while (gettoken() != TOK_EOF) {
		if (token == TOK_EOL)
			/* empty line */
			continue;
		if (token == '%') {
			gettoken();
			parse_section(mod);
		} else if (token == '.') {
			if (gettoken() != TOK_STR)
				report("keyword expected after '.'");
			if (strcmp(strval, "include") == 0)
				parse_include();
			else
				report("unknown keyword '%s'", strval);
		} else if (token == TOK_STR) {
			strcpy(varname, strval);
			if (gettoken() == TOK_ASSIGN || token == TOK_QASSIGN)
				parse_define(varname);
			else
				parse_assign(varname);
		}
		if (token != TOK_EOL)
			report("eol expected");
	}
}

/*
 * Do rollback on errors
 */
static void
do_rollback(void)
{
	struct assign *tp;
	struct snmp_node *node;

	while ((tp = TAILQ_LAST(&assigns, assigns)) != NULL) {
		TAILQ_REMOVE(&assigns, tp, link);
		for (node = tree; node < &tree[tree_size]; node++)
			if (node->name == tp->node_name) {
				snmp_ctx->scratch = &tp->scratch;
				(void)(*node->op)(snmp_ctx, &tp->value,
				    node->oid.len, node->index,
				    SNMP_OP_ROLLBACK);
				break;
			}
		if (node == &tree[tree_size])
			syslog(LOG_ERR, "failed to find node for "
			    "rollback");
		snmp_value_free(&tp->value);
		free(tp);
	}
}

/*
 * Do commit
 */
static void
do_commit(void)
{
	struct assign *tp;
	struct snmp_node *node;

	while ((tp = TAILQ_FIRST(&assigns)) != NULL) {
		TAILQ_REMOVE(&assigns, tp, link);
		for (node = tree; node < &tree[tree_size]; node++)
			if (node->name == tp->node_name) {
				snmp_ctx->scratch = &tp->scratch;
				(void)(*node->op)(snmp_ctx, &tp->value,
				    node->oid.len, node->index, SNMP_OP_COMMIT);
				break;
			}
		if (node == &tree[tree_size])
			syslog(LOG_ERR, "failed to find node for commit");
		snmp_value_free(&tp->value);
		free(tp);
	}
}

/*
 * Read the configuration file. Handle the entire file as one transaction.
 *
 * If lodmod is NULL, the sections for 'snmpd' and all loaded modules are
 * executed. If it is not NULL, only the sections for that module are handled.
 */
int
read_config(const char *fname, struct lmodule *lodmod)
{
	int err;
	char objbuf[ASN_OIDSTRLEN];
	char idxbuf[ASN_OIDSTRLEN];

	ignore = 0;

	input_push = 0;

	if (ERRPUSH())
		return (-1);
	if (input_open_file(fname, 0) == -1) {
		syslog(LOG_ERR, "%s: %m", fname);
		return (-1);
	}
	ERRPOP();
	community = COMM_INITIALIZE;

	if ((snmp_ctx = snmp_init_context()) == NULL) {
		input_close_all();
		syslog(LOG_ERR, "%m");
		return (-1);
	}

	if (ERRPUSH()) {
		do_rollback();
		input_close_all();
		macro_free_all();
		free(snmp_ctx);
		return (-1);
	}
	parse_file(lodmod);
	ERRPOP();

	if ((err = snmp_dep_commit(snmp_ctx)) != SNMP_ERR_NOERROR) {
		syslog(LOG_ERR, "init dep failed: %u %s %s", err,
		    asn_oid2str_r(&snmp_ctx->dep->obj, objbuf),
		    asn_oid2str_r(&snmp_ctx->dep->idx, idxbuf));
		snmp_dep_rollback(snmp_ctx);
		do_rollback();
		input_close_all();
		macro_free_all();
		free(snmp_ctx);
		return (-1);
	}

	do_commit();
	snmp_dep_finish(snmp_ctx);

	macro_free_all();

	free(snmp_ctx);

	return (0);
}

/*
 * Define a permanent macro
 */
int
define_macro(const char *name, const char *value)
{
	struct macro *m;

	if ((m = malloc(sizeof(*m))) == NULL)
		return (-1);
	if ((m->name = malloc(strlen(name) + 1)) == NULL) {
		free(m);
		return (-1);
	}
	strcpy(m->name, name);
	if ((m->value = malloc(strlen(value) + 1)) == NULL) {
		free(m->name);
		free(m);
		return (-1);
	}
	strcpy(m->value, value);
	m->length = strlen(value);
	m->perm = 1;
	LIST_INSERT_HEAD(&macros, m, link);
	return (0);
}
