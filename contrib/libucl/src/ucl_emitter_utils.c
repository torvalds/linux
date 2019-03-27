/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ucl.h"
#include "ucl_internal.h"
#include "ucl_chartable.h"

#ifdef HAVE_FLOAT_H
#include <float.h>
#endif
#ifdef HAVE_MATH_H
#include <math.h>
#endif

extern const struct ucl_emitter_operations ucl_standartd_emitter_ops[];

static const struct ucl_emitter_context ucl_standard_emitters[] = {
	[UCL_EMIT_JSON] = {
		.name = "json",
		.id = UCL_EMIT_JSON,
		.func = NULL,
		.ops = &ucl_standartd_emitter_ops[UCL_EMIT_JSON]
	},
	[UCL_EMIT_JSON_COMPACT] = {
		.name = "json_compact",
		.id = UCL_EMIT_JSON_COMPACT,
		.func = NULL,
		.ops = &ucl_standartd_emitter_ops[UCL_EMIT_JSON_COMPACT]
	},
	[UCL_EMIT_CONFIG] = {
		.name = "config",
		.id = UCL_EMIT_CONFIG,
		.func = NULL,
		.ops = &ucl_standartd_emitter_ops[UCL_EMIT_CONFIG]
	},
	[UCL_EMIT_YAML] = {
		.name = "yaml",
		.id = UCL_EMIT_YAML,
		.func = NULL,
		.ops = &ucl_standartd_emitter_ops[UCL_EMIT_YAML]
	},
	[UCL_EMIT_MSGPACK] = {
		.name = "msgpack",
		.id = UCL_EMIT_MSGPACK,
		.func = NULL,
		.ops = &ucl_standartd_emitter_ops[UCL_EMIT_MSGPACK]
	}
};

/**
 * Get standard emitter context for a specified emit_type
 * @param emit_type type of emitter
 * @return context or NULL if input is invalid
 */
const struct ucl_emitter_context *
ucl_emit_get_standard_context (enum ucl_emitter emit_type)
{
	if (emit_type >= UCL_EMIT_JSON && emit_type < UCL_EMIT_MAX) {
		return &ucl_standard_emitters[emit_type];
	}

	return NULL;
}

/**
 * Serialise string
 * @param str string to emit
 * @param buf target buffer
 */
void
ucl_elt_string_write_json (const char *str, size_t size,
		struct ucl_emitter_context *ctx)
{
	const char *p = str, *c = str;
	size_t len = 0;
	const struct ucl_emitter_functions *func = ctx->func;

	func->ucl_emitter_append_character ('"', 1, func->ud);

	while (size) {
		if (ucl_test_character (*p, UCL_CHARACTER_JSON_UNSAFE|UCL_CHARACTER_DENIED)) {
			if (len > 0) {
				func->ucl_emitter_append_len (c, len, func->ud);
			}
			switch (*p) {
			case '\n':
				func->ucl_emitter_append_len ("\\n", 2, func->ud);
				break;
			case '\r':
				func->ucl_emitter_append_len ("\\r", 2, func->ud);
				break;
			case '\b':
				func->ucl_emitter_append_len ("\\b", 2, func->ud);
				break;
			case '\t':
				func->ucl_emitter_append_len ("\\t", 2, func->ud);
				break;
			case '\f':
				func->ucl_emitter_append_len ("\\f", 2, func->ud);
				break;
			case '\\':
				func->ucl_emitter_append_len ("\\\\", 2, func->ud);
				break;
			case '"':
				func->ucl_emitter_append_len ("\\\"", 2, func->ud);
				break;
			default:
				/* Emit unicode unknown character */
				func->ucl_emitter_append_len ("\\uFFFD", 5, func->ud);
				break;
			}
			len = 0;
			c = ++p;
		}
		else {
			p ++;
			len ++;
		}
		size --;
	}

	if (len > 0) {
		func->ucl_emitter_append_len (c, len, func->ud);
	}

	func->ucl_emitter_append_character ('"', 1, func->ud);
}

void
ucl_elt_string_write_multiline (const char *str, size_t size,
		struct ucl_emitter_context *ctx)
{
	const struct ucl_emitter_functions *func = ctx->func;

	func->ucl_emitter_append_len ("<<EOD\n", sizeof ("<<EOD\n") - 1, func->ud);
	func->ucl_emitter_append_len (str, size, func->ud);
	func->ucl_emitter_append_len ("\nEOD", sizeof ("\nEOD") - 1, func->ud);
}

/*
 * Generic utstring output
 */
static int
ucl_utstring_append_character (unsigned char c, size_t len, void *ud)
{
	UT_string *buf = ud;

	if (len == 1) {
		utstring_append_c (buf, c);
	}
	else {
		utstring_reserve (buf, len + 1);
		memset (&buf->d[buf->i], c, len);
		buf->i += len;
		buf->d[buf->i] = '\0';
	}

	return 0;
}

static int
ucl_utstring_append_len (const unsigned char *str, size_t len, void *ud)
{
	UT_string *buf = ud;

	utstring_append_len (buf, str, len);

	return 0;
}

static int
ucl_utstring_append_int (int64_t val, void *ud)
{
	UT_string *buf = ud;

	utstring_printf (buf, "%jd", (intmax_t)val);
	return 0;
}

static int
ucl_utstring_append_double (double val, void *ud)
{
	UT_string *buf = ud;
	const double delta = 0.0000001;

	if (val == (double)(int)val) {
		utstring_printf (buf, "%.1lf", val);
	}
	else if (fabs (val - (double)(int)val) < delta) {
		/* Write at maximum precision */
		utstring_printf (buf, "%.*lg", DBL_DIG, val);
	}
	else {
		utstring_printf (buf, "%lf", val);
	}

	return 0;
}

/*
 * Generic file output
 */
static int
ucl_file_append_character (unsigned char c, size_t len, void *ud)
{
	FILE *fp = ud;

	while (len --) {
		fputc (c, fp);
	}

	return 0;
}

static int
ucl_file_append_len (const unsigned char *str, size_t len, void *ud)
{
	FILE *fp = ud;

	fwrite (str, len, 1, fp);

	return 0;
}

static int
ucl_file_append_int (int64_t val, void *ud)
{
	FILE *fp = ud;

	fprintf (fp, "%jd", (intmax_t)val);

	return 0;
}

static int
ucl_file_append_double (double val, void *ud)
{
	FILE *fp = ud;
	const double delta = 0.0000001;

	if (val == (double)(int)val) {
		fprintf (fp, "%.1lf", val);
	}
	else if (fabs (val - (double)(int)val) < delta) {
		/* Write at maximum precision */
		fprintf (fp, "%.*lg", DBL_DIG, val);
	}
	else {
		fprintf (fp, "%lf", val);
	}

	return 0;
}

/*
 * Generic file descriptor writing functions
 */
static int
ucl_fd_append_character (unsigned char c, size_t len, void *ud)
{
	int fd = *(int *)ud;
	unsigned char *buf;

	if (len == 1) {
		return write (fd, &c, 1);
	}
	else {
		buf = malloc (len);
		if (buf == NULL) {
			/* Fallback */
			while (len --) {
				if (write (fd, &c, 1) == -1) {
					return -1;
				}
			}
		}
		else {
			memset (buf, c, len);
			if (write (fd, buf, len) == -1) {
				free(buf);
				return -1;
			}
			free (buf);
		}
	}

	return 0;
}

static int
ucl_fd_append_len (const unsigned char *str, size_t len, void *ud)
{
	int fd = *(int *)ud;

	return write (fd, str, len);
}

static int
ucl_fd_append_int (int64_t val, void *ud)
{
	int fd = *(int *)ud;
	char intbuf[64];

	snprintf (intbuf, sizeof (intbuf), "%jd", (intmax_t)val);
	return write (fd, intbuf, strlen (intbuf));
}

static int
ucl_fd_append_double (double val, void *ud)
{
	int fd = *(int *)ud;
	const double delta = 0.0000001;
	char nbuf[64];

	if (val == (double)(int)val) {
		snprintf (nbuf, sizeof (nbuf), "%.1lf", val);
	}
	else if (fabs (val - (double)(int)val) < delta) {
		/* Write at maximum precision */
		snprintf (nbuf, sizeof (nbuf), "%.*lg", DBL_DIG, val);
	}
	else {
		snprintf (nbuf, sizeof (nbuf), "%lf", val);
	}

	return write (fd, nbuf, strlen (nbuf));
}

struct ucl_emitter_functions*
ucl_object_emit_memory_funcs (void **pmem)
{
	struct ucl_emitter_functions *f;
	UT_string *s;

	f = calloc (1, sizeof (*f));

	if (f != NULL) {
		f->ucl_emitter_append_character = ucl_utstring_append_character;
		f->ucl_emitter_append_double = ucl_utstring_append_double;
		f->ucl_emitter_append_int = ucl_utstring_append_int;
		f->ucl_emitter_append_len = ucl_utstring_append_len;
		f->ucl_emitter_free_func = free;
		utstring_new (s);
		f->ud = s;
		*pmem = s->d;
		s->pd = pmem;
	}

	return f;
}

struct ucl_emitter_functions*
ucl_object_emit_file_funcs (FILE *fp)
{
	struct ucl_emitter_functions *f;

	f = calloc (1, sizeof (*f));

	if (f != NULL) {
		f->ucl_emitter_append_character = ucl_file_append_character;
		f->ucl_emitter_append_double = ucl_file_append_double;
		f->ucl_emitter_append_int = ucl_file_append_int;
		f->ucl_emitter_append_len = ucl_file_append_len;
		f->ucl_emitter_free_func = NULL;
		f->ud = fp;
	}

	return f;
}

struct ucl_emitter_functions*
ucl_object_emit_fd_funcs (int fd)
{
	struct ucl_emitter_functions *f;
	int *ip;

	f = calloc (1, sizeof (*f));

	if (f != NULL) {
		ip = malloc (sizeof (fd));
		if (ip == NULL) {
			free (f);
			return NULL;
		}

		memcpy (ip, &fd, sizeof (fd));
		f->ucl_emitter_append_character = ucl_fd_append_character;
		f->ucl_emitter_append_double = ucl_fd_append_double;
		f->ucl_emitter_append_int = ucl_fd_append_int;
		f->ucl_emitter_append_len = ucl_fd_append_len;
		f->ucl_emitter_free_func = free;
		f->ud = ip;
	}

	return f;
}

void
ucl_object_emit_funcs_free (struct ucl_emitter_functions *f)
{
	if (f != NULL) {
		if (f->ucl_emitter_free_func != NULL) {
			f->ucl_emitter_free_func (f->ud);
		}
		free (f);
	}
}


unsigned char *
ucl_object_emit_single_json (const ucl_object_t *obj)
{
	UT_string *buf = NULL;
	unsigned char *res = NULL;

	if (obj == NULL) {
		return NULL;
	}

	utstring_new (buf);

	if (buf != NULL) {
		switch (obj->type) {
		case UCL_OBJECT:
			ucl_utstring_append_len ("object", 6, buf);
			break;
		case UCL_ARRAY:
			ucl_utstring_append_len ("array", 5, buf);
			break;
		case UCL_INT:
			ucl_utstring_append_int (obj->value.iv, buf);
			break;
		case UCL_FLOAT:
		case UCL_TIME:
			ucl_utstring_append_double (obj->value.dv, buf);
			break;
		case UCL_NULL:
			ucl_utstring_append_len ("null", 4, buf);
			break;
		case UCL_BOOLEAN:
			if (obj->value.iv) {
				ucl_utstring_append_len ("true", 4, buf);
			}
			else {
				ucl_utstring_append_len ("false", 5, buf);
			}
			break;
		case UCL_STRING:
			ucl_utstring_append_len (obj->value.sv, obj->len, buf);
			break;
		case UCL_USERDATA:
			ucl_utstring_append_len ("userdata", 8, buf);
			break;
		}
		res = utstring_body (buf);
		free (buf);
	}

	return res;
}

#define LONG_STRING_LIMIT 80

bool
ucl_maybe_long_string (const ucl_object_t *obj)
{
	if (obj->len > LONG_STRING_LIMIT || (obj->flags & UCL_OBJECT_MULTILINE)) {
		/* String is long enough, so search for newline characters in it */
		if (memchr (obj->value.sv, '\n', obj->len) != NULL) {
			return true;
		}
	}

	return false;
}
