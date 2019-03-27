/* Copyright (c) 2013, Vsevolod Stakhov
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

#include "ucl.h"
#include "ucl_internal.h"
#include "ucl_chartable.h"

/**
 * @file ucl_parser.c
 * The implementation of ucl parser
 */

struct ucl_parser_saved_state {
	unsigned int line;
	unsigned int column;
	size_t remain;
	const unsigned char *pos;
};

/**
 * Move up to len characters
 * @param parser
 * @param begin
 * @param len
 * @return new position in chunk
 */
#define ucl_chunk_skipc(chunk, p)    do{					\
    if (*(p) == '\n') {										\
        (chunk)->line ++;									\
        (chunk)->column = 0;								\
    }														\
    else (chunk)->column ++;								\
    (p++);													\
    (chunk)->pos ++;										\
    (chunk)->remain --;										\
    } while (0)

static inline void
ucl_set_err (struct ucl_parser *parser, int code, const char *str, UT_string **err)
{
	const char *fmt_string, *filename;
	struct ucl_chunk *chunk = parser->chunks;

	if (parser->cur_file) {
		filename = parser->cur_file;
	}
	else {
		filename = "<unknown>";
	}

	if (chunk->pos < chunk->end) {
		if (isgraph (*chunk->pos)) {
			fmt_string = "error while parsing %s: "
					"line: %d, column: %d - '%s', character: '%c'";
		}
		else {
			fmt_string = "error while parsing %s: "
					"line: %d, column: %d - '%s', character: '0x%02x'";
		}
		ucl_create_err (err, fmt_string,
			filename, chunk->line, chunk->column,
			str, *chunk->pos);
	}
	else {
		ucl_create_err (err, "error while parsing %s: at the end of chunk: %s",
			filename, str);
	}

	parser->err_code = code;
}

static void
ucl_save_comment (struct ucl_parser *parser, const char *begin, size_t len)
{
	ucl_object_t *nobj;

	if (len > 0 && begin != NULL) {
		nobj = ucl_object_fromstring_common (begin, len, 0);

		if (parser->last_comment) {
			/* We need to append data to an existing object */
			DL_APPEND (parser->last_comment, nobj);
		}
		else {
			parser->last_comment = nobj;
		}
	}
}

static void
ucl_attach_comment (struct ucl_parser *parser, ucl_object_t *obj, bool before)
{
	if (parser->last_comment) {
		ucl_object_insert_key (parser->comments, parser->last_comment,
				(const char *)&obj, sizeof (void *), true);

		if (before) {
			parser->last_comment->flags |= UCL_OBJECT_INHERITED;
		}

		parser->last_comment = NULL;
	}
}

/**
 * Skip all comments from the current pos resolving nested and multiline comments
 * @param parser
 * @return
 */
static bool
ucl_skip_comments (struct ucl_parser *parser)
{
	struct ucl_chunk *chunk = parser->chunks;
	const unsigned char *p, *beg = NULL;
	int comments_nested = 0;
	bool quoted = false;

	p = chunk->pos;

start:
	if (chunk->remain > 0 && *p == '#') {
		if (parser->state != UCL_STATE_SCOMMENT &&
				parser->state != UCL_STATE_MCOMMENT) {
			beg = p;

			while (p < chunk->end) {
				if (*p == '\n') {
					if (parser->flags & UCL_PARSER_SAVE_COMMENTS) {
						ucl_save_comment (parser, beg, p - beg);
						beg = NULL;
					}

					ucl_chunk_skipc (chunk, p);

					goto start;
				}
				ucl_chunk_skipc (chunk, p);
			}
		}
	}
	else if (chunk->remain >= 2 && *p == '/') {
		if (p[1] == '*') {
			beg = p;
			ucl_chunk_skipc (chunk, p);
			comments_nested ++;
			ucl_chunk_skipc (chunk, p);

			while (p < chunk->end) {
				if (*p == '"' && *(p - 1) != '\\') {
					quoted = !quoted;
				}

				if (!quoted) {
					if (*p == '*') {
						ucl_chunk_skipc (chunk, p);
						if (*p == '/') {
							comments_nested --;
							if (comments_nested == 0) {
								if (parser->flags & UCL_PARSER_SAVE_COMMENTS) {
									ucl_save_comment (parser, beg, p - beg + 1);
									beg = NULL;
								}

								ucl_chunk_skipc (chunk, p);
								goto start;
							}
						}
						ucl_chunk_skipc (chunk, p);
					}
					else if (p[0] == '/' && chunk->remain >= 2 && p[1] == '*') {
						comments_nested ++;
						ucl_chunk_skipc (chunk, p);
						ucl_chunk_skipc (chunk, p);
						continue;
					}
				}

				ucl_chunk_skipc (chunk, p);
			}
			if (comments_nested != 0) {
				ucl_set_err (parser, UCL_ENESTED,
						"unfinished multiline comment", &parser->err);
				return false;
			}
		}
	}

	if (beg && p > beg && (parser->flags & UCL_PARSER_SAVE_COMMENTS)) {
		ucl_save_comment (parser, beg, p - beg);
	}

	return true;
}

/**
 * Return multiplier for a character
 * @param c multiplier character
 * @param is_bytes if true use 1024 multiplier
 * @return multiplier
 */
static inline unsigned long
ucl_lex_num_multiplier (const unsigned char c, bool is_bytes) {
	const struct {
		char c;
		long mult_normal;
		long mult_bytes;
	} multipliers[] = {
			{'m', 1000 * 1000, 1024 * 1024},
			{'k', 1000, 1024},
			{'g', 1000 * 1000 * 1000, 1024 * 1024 * 1024}
	};
	int i;

	for (i = 0; i < 3; i ++) {
		if (tolower (c) == multipliers[i].c) {
			if (is_bytes) {
				return multipliers[i].mult_bytes;
			}
			return multipliers[i].mult_normal;
		}
	}

	return 1;
}


/**
 * Return multiplier for time scaling
 * @param c
 * @return
 */
static inline double
ucl_lex_time_multiplier (const unsigned char c) {
	const struct {
		char c;
		double mult;
	} multipliers[] = {
			{'m', 60},
			{'h', 60 * 60},
			{'d', 60 * 60 * 24},
			{'w', 60 * 60 * 24 * 7},
			{'y', 60 * 60 * 24 * 365}
	};
	int i;

	for (i = 0; i < 5; i ++) {
		if (tolower (c) == multipliers[i].c) {
			return multipliers[i].mult;
		}
	}

	return 1;
}

/**
 * Return true if a character is a end of an atom
 * @param c
 * @return
 */
static inline bool
ucl_lex_is_atom_end (const unsigned char c)
{
	return ucl_test_character (c, UCL_CHARACTER_VALUE_END);
}

static inline bool
ucl_lex_is_comment (const unsigned char c1, const unsigned char c2)
{
	if (c1 == '/') {
		if (c2 == '*') {
			return true;
		}
	}
	else if (c1 == '#') {
		return true;
	}
	return false;
}

/**
 * Check variable found
 * @param parser
 * @param ptr
 * @param remain
 * @param out_len
 * @param strict
 * @param found
 * @return
 */
static inline const char *
ucl_check_variable_safe (struct ucl_parser *parser, const char *ptr, size_t remain,
		size_t *out_len, bool strict, bool *found)
{
	struct ucl_variable *var;
	unsigned char *dst;
	size_t dstlen;
	bool need_free = false;

	LL_FOREACH (parser->variables, var) {
		if (strict) {
			if (remain == var->var_len) {
				if (memcmp (ptr, var->var, var->var_len) == 0) {
					*out_len += var->value_len;
					*found = true;
					return (ptr + var->var_len);
				}
			}
		}
		else {
			if (remain >= var->var_len) {
				if (memcmp (ptr, var->var, var->var_len) == 0) {
					*out_len += var->value_len;
					*found = true;
					return (ptr + var->var_len);
				}
			}
		}
	}

	/* XXX: can only handle ${VAR} */
	if (!(*found) && parser->var_handler != NULL && strict) {
		/* Call generic handler */
		if (parser->var_handler (ptr, remain, &dst, &dstlen, &need_free,
				parser->var_data)) {
			*out_len += dstlen;
			*found = true;
			if (need_free) {
				free (dst);
			}
			return (ptr + remain);
		}
	}

	return ptr;
}

/**
 * Check for a variable in a given string
 * @param parser
 * @param ptr
 * @param remain
 * @param out_len
 * @param vars_found
 * @return
 */
static const char *
ucl_check_variable (struct ucl_parser *parser, const char *ptr,
		size_t remain, size_t *out_len, bool *vars_found)
{
	const char *p, *end, *ret = ptr;
	bool found = false;

	if (*ptr == '{') {
		/* We need to match the variable enclosed in braces */
		p = ptr + 1;
		end = ptr + remain;
		while (p < end) {
			if (*p == '}') {
				ret = ucl_check_variable_safe (parser, ptr + 1, p - ptr - 1,
						out_len, true, &found);
				if (found) {
					/* {} must be excluded actually */
					ret ++;
					if (!*vars_found) {
						*vars_found = true;
					}
				}
				else {
					*out_len += 2;
				}
				break;
			}
			p ++;
		}
	}
	else if (*ptr != '$') {
		/* Not count escaped dollar sign */
		ret = ucl_check_variable_safe (parser, ptr, remain, out_len, false, &found);
		if (found && !*vars_found) {
			*vars_found = true;
		}
		if (!found) {
			(*out_len) ++;
		}
	}
	else {
		ret ++;
		(*out_len) ++;
	}

	return ret;
}

/**
 * Expand a single variable
 * @param parser
 * @param ptr
 * @param remain
 * @param dest
 * @return
 */
static const char *
ucl_expand_single_variable (struct ucl_parser *parser, const char *ptr,
		size_t remain, unsigned char **dest)
{
	unsigned char *d = *dest, *dst;
	const char *p = ptr + 1, *ret;
	struct ucl_variable *var;
	size_t dstlen;
	bool need_free = false;
	bool found = false;
	bool strict = false;

	ret = ptr + 1;
	remain --;

	if (*p == '$') {
		*d++ = *p++;
		*dest = d;
		return p;
	}
	else if (*p == '{') {
		p ++;
		strict = true;
		ret += 2;
		remain -= 2;
	}

	LL_FOREACH (parser->variables, var) {
		if (remain >= var->var_len) {
			if (memcmp (p, var->var, var->var_len) == 0) {
				memcpy (d, var->value, var->value_len);
				ret += var->var_len;
				d += var->value_len;
				found = true;
				break;
			}
		}
	}
	if (!found) {
		if (strict && parser->var_handler != NULL) {
			size_t var_len = 0;
			while (var_len < remain && p[var_len] != '}')
				var_len ++;

			if (parser->var_handler (p, var_len, &dst, &dstlen, &need_free,
							parser->var_data)) {
				memcpy (d, dst, dstlen);
				ret += var_len;
				d += dstlen;
				if (need_free) {
					free (dst);
				}
				found = true;
			}
		}

		/* Leave variable as is */
		if (!found) {
			if (strict) {
				/* Copy '${' */
				memcpy (d, ptr, 2);
				d += 2;
				ret --;
			}
			else {
				memcpy (d, ptr, 1);
				d ++;
			}
		}
	}

	*dest = d;
	return ret;
}

/**
 * Expand variables in string
 * @param parser
 * @param dst
 * @param src
 * @param in_len
 * @return
 */
static ssize_t
ucl_expand_variable (struct ucl_parser *parser, unsigned char **dst,
		const char *src, size_t in_len)
{
	const char *p, *end = src + in_len;
	unsigned char *d;
	size_t out_len = 0;
	bool vars_found = false;

	if (parser->flags & UCL_PARSER_DISABLE_MACRO) {
		*dst = NULL;
		return in_len;
	}

	p = src;
	while (p != end) {
		if (*p == '$') {
			p = ucl_check_variable (parser, p + 1, end - p - 1, &out_len, &vars_found);
		}
		else {
			p ++;
			out_len ++;
		}
	}

	if (!vars_found) {
		/* Trivial case */
		*dst = NULL;
		return in_len;
	}

	*dst = UCL_ALLOC (out_len + 1);
	if (*dst == NULL) {
		return in_len;
	}

	d = *dst;
	p = src;
	while (p != end) {
		if (*p == '$') {
			p = ucl_expand_single_variable (parser, p, end - p, &d);
		}
		else {
			*d++ = *p++;
		}
	}

	*d = '\0';

	return out_len;
}

/**
 * Store or copy pointer to the trash stack
 * @param parser parser object
 * @param src src string
 * @param dst destination buffer (trash stack pointer)
 * @param dst_const const destination pointer (e.g. value of object)
 * @param in_len input length
 * @param need_unescape need to unescape source (and copy it)
 * @param need_lowercase need to lowercase value (and copy)
 * @param need_expand need to expand variables (and copy as well)
 * @return output length (excluding \0 symbol)
 */
static inline ssize_t
ucl_copy_or_store_ptr (struct ucl_parser *parser,
		const unsigned char *src, unsigned char **dst,
		const char **dst_const, size_t in_len,
		bool need_unescape, bool need_lowercase, bool need_expand)
{
	ssize_t ret = -1, tret;
	unsigned char *tmp;

	if (need_unescape || need_lowercase ||
			(need_expand && parser->variables != NULL) ||
			!(parser->flags & UCL_PARSER_ZEROCOPY)) {
		/* Copy string */
		*dst = UCL_ALLOC (in_len + 1);
		if (*dst == NULL) {
			ucl_set_err (parser, UCL_EINTERNAL, "cannot allocate memory for a string",
					&parser->err);
			return false;
		}
		if (need_lowercase) {
			ret = ucl_strlcpy_tolower (*dst, src, in_len + 1);
		}
		else {
			ret = ucl_strlcpy_unsafe (*dst, src, in_len + 1);
		}

		if (need_unescape) {
			ret = ucl_unescape_json_string (*dst, ret);
		}
		if (need_expand) {
			tmp = *dst;
			tret = ret;
			ret = ucl_expand_variable (parser, dst, tmp, ret);
			if (*dst == NULL) {
				/* Nothing to expand */
				*dst = tmp;
				ret = tret;
			}
			else {
				/* Free unexpanded value */
				UCL_FREE (in_len + 1, tmp);
			}
		}
		*dst_const = *dst;
	}
	else {
		*dst_const = src;
		ret = in_len;
	}

	return ret;
}

/**
 * Create and append an object at the specified level
 * @param parser
 * @param is_array
 * @param level
 * @return
 */
static inline ucl_object_t *
ucl_parser_add_container (ucl_object_t *obj, struct ucl_parser *parser,
		bool is_array, int level)
{
	struct ucl_stack *st;

	if (!is_array) {
		if (obj == NULL) {
			obj = ucl_object_new_full (UCL_OBJECT, parser->chunks->priority);
		}
		else {
			obj->type = UCL_OBJECT;
		}
		if (obj->value.ov == NULL) {
			obj->value.ov = ucl_hash_create (parser->flags & UCL_PARSER_KEY_LOWERCASE);
		}
		parser->state = UCL_STATE_KEY;
	}
	else {
		if (obj == NULL) {
			obj = ucl_object_new_full (UCL_ARRAY, parser->chunks->priority);
		}
		else {
			obj->type = UCL_ARRAY;
		}
		parser->state = UCL_STATE_VALUE;
	}

	st = UCL_ALLOC (sizeof (struct ucl_stack));

	if (st == NULL) {
		ucl_set_err (parser, UCL_EINTERNAL, "cannot allocate memory for an object",
				&parser->err);
		ucl_object_unref (obj);
		return NULL;
	}

	st->obj = obj;
	st->level = level;
	LL_PREPEND (parser->stack, st);
	parser->cur_obj = obj;

	return obj;
}

int
ucl_maybe_parse_number (ucl_object_t *obj,
		const char *start, const char *end, const char **pos,
		bool allow_double, bool number_bytes, bool allow_time)
{
	const char *p = start, *c = start;
	char *endptr;
	bool got_dot = false, got_exp = false, need_double = false,
			is_time = false, valid_start = false, is_hex = false,
			is_neg = false;
	double dv = 0;
	int64_t lv = 0;

	if (*p == '-') {
		is_neg = true;
		c ++;
		p ++;
	}
	while (p < end) {
		if (is_hex && isxdigit (*p)) {
			p ++;
		}
		else if (isdigit (*p)) {
			valid_start = true;
			p ++;
		}
		else if (!is_hex && (*p == 'x' || *p == 'X')) {
			is_hex = true;
			allow_double = false;
			c = p + 1;
		}
		else if (allow_double) {
			if (p == c) {
				/* Empty digits sequence, not a number */
				*pos = start;
				return EINVAL;
			}
			else if (*p == '.') {
				if (got_dot) {
					/* Double dots, not a number */
					*pos = start;
					return EINVAL;
				}
				else {
					got_dot = true;
					need_double = true;
					p ++;
				}
			}
			else if (*p == 'e' || *p == 'E') {
				if (got_exp) {
					/* Double exp, not a number */
					*pos = start;
					return EINVAL;
				}
				else {
					got_exp = true;
					need_double = true;
					p ++;
					if (p >= end) {
						*pos = start;
						return EINVAL;
					}
					if (!isdigit (*p) && *p != '+' && *p != '-') {
						/* Wrong exponent sign */
						*pos = start;
						return EINVAL;
					}
					else {
						p ++;
					}
				}
			}
			else {
				/* Got the end of the number, need to check */
				break;
			}
		}
		else {
			break;
		}
	}

	if (!valid_start) {
		*pos = start;
		return EINVAL;
	}

	errno = 0;
	if (need_double) {
		dv = strtod (c, &endptr);
	}
	else {
		if (is_hex) {
			lv = strtoimax (c, &endptr, 16);
		}
		else {
			lv = strtoimax (c, &endptr, 10);
		}
	}
	if (errno == ERANGE) {
		*pos = start;
		return ERANGE;
	}

	/* Now check endptr */
	if (endptr == NULL || ucl_lex_is_atom_end (*endptr) || *endptr == '\0') {
		p = endptr;
		goto set_obj;
	}

	if (endptr < end && endptr != start) {
		p = endptr;
		switch (*p) {
		case 'm':
		case 'M':
		case 'g':
		case 'G':
		case 'k':
		case 'K':
			if (end - p >= 2) {
				if (p[1] == 's' || p[1] == 'S') {
					/* Milliseconds */
					if (!need_double) {
						need_double = true;
						dv = lv;
					}
					is_time = true;
					if (p[0] == 'm' || p[0] == 'M') {
						dv /= 1000.;
					}
					else {
						dv *= ucl_lex_num_multiplier (*p, false);
					}
					p += 2;
					goto set_obj;
				}
				else if (number_bytes || (p[1] == 'b' || p[1] == 'B')) {
					/* Bytes */
					if (need_double) {
						need_double = false;
						lv = dv;
					}
					lv *= ucl_lex_num_multiplier (*p, true);
					p += 2;
					goto set_obj;
				}
				else if (ucl_lex_is_atom_end (p[1])) {
					if (need_double) {
						dv *= ucl_lex_num_multiplier (*p, false);
					}
					else {
						lv *= ucl_lex_num_multiplier (*p, number_bytes);
					}
					p ++;
					goto set_obj;
				}
				else if (allow_time && end - p >= 3) {
					if (tolower (p[0]) == 'm' &&
							tolower (p[1]) == 'i' &&
							tolower (p[2]) == 'n') {
						/* Minutes */
						if (!need_double) {
							need_double = true;
							dv = lv;
						}
						is_time = true;
						dv *= 60.;
						p += 3;
						goto set_obj;
					}
				}
			}
			else {
				if (need_double) {
					dv *= ucl_lex_num_multiplier (*p, false);
				}
				else {
					lv *= ucl_lex_num_multiplier (*p, number_bytes);
				}
				p ++;
				goto set_obj;
			}
			break;
		case 'S':
		case 's':
			if (allow_time &&
					(p == end - 1 || ucl_lex_is_atom_end (p[1]))) {
				if (!need_double) {
					need_double = true;
					dv = lv;
				}
				p ++;
				is_time = true;
				goto set_obj;
			}
			break;
		case 'h':
		case 'H':
		case 'd':
		case 'D':
		case 'w':
		case 'W':
		case 'Y':
		case 'y':
			if (allow_time &&
					(p == end - 1 || ucl_lex_is_atom_end (p[1]))) {
				if (!need_double) {
					need_double = true;
					dv = lv;
				}
				is_time = true;
				dv *= ucl_lex_time_multiplier (*p);
				p ++;
				goto set_obj;
			}
			break;
		case '\t':
		case ' ':
			while (p < end && ucl_test_character(*p, UCL_CHARACTER_WHITESPACE)) {
				p++;
			}
			if (ucl_lex_is_atom_end(*p))
				goto set_obj;
			break;
		}
	}
	else if (endptr == end) {
		/* Just a number at the end of chunk */
		p = endptr;
		goto set_obj;
	}

	*pos = c;
	return EINVAL;

set_obj:
	if (obj != NULL) {
		if (allow_double && (need_double || is_time)) {
			if (!is_time) {
				obj->type = UCL_FLOAT;
			}
			else {
				obj->type = UCL_TIME;
			}
			obj->value.dv = is_neg ? (-dv) : dv;
		}
		else {
			obj->type = UCL_INT;
			obj->value.iv = is_neg ? (-lv) : lv;
		}
	}
	*pos = p;
	return 0;
}

/**
 * Parse possible number
 * @param parser
 * @param chunk
 * @param obj
 * @return true if a number has been parsed
 */
static bool
ucl_lex_number (struct ucl_parser *parser,
		struct ucl_chunk *chunk, ucl_object_t *obj)
{
	const unsigned char *pos;
	int ret;

	ret = ucl_maybe_parse_number (obj, chunk->pos, chunk->end, (const char **)&pos,
			true, false, ((parser->flags & UCL_PARSER_NO_TIME) == 0));

	if (ret == 0) {
		chunk->remain -= pos - chunk->pos;
		chunk->column += pos - chunk->pos;
		chunk->pos = pos;
		return true;
	}
	else if (ret == ERANGE) {
		ucl_set_err (parser, UCL_ESYNTAX, "numeric value out of range",
				&parser->err);
	}

	return false;
}

/**
 * Parse quoted string with possible escapes
 * @param parser
 * @param chunk
 * @param need_unescape
 * @param ucl_escape
 * @param var_expand
 * @return true if a string has been parsed
 */
static bool
ucl_lex_json_string (struct ucl_parser *parser,
		struct ucl_chunk *chunk, bool *need_unescape, bool *ucl_escape, bool *var_expand)
{
	const unsigned char *p = chunk->pos;
	unsigned char c;
	int i;

	while (p < chunk->end) {
		c = *p;
		if (c < 0x1F) {
			/* Unmasked control character */
			if (c == '\n') {
				ucl_set_err (parser, UCL_ESYNTAX, "unexpected newline",
						&parser->err);
			}
			else {
				ucl_set_err (parser, UCL_ESYNTAX, "unexpected control character",
						&parser->err);
			}
			return false;
		}
		else if (c == '\\') {
			ucl_chunk_skipc (chunk, p);
			c = *p;
			if (p >= chunk->end) {
				ucl_set_err (parser, UCL_ESYNTAX, "unfinished escape character",
						&parser->err);
				return false;
			}
			else if (ucl_test_character (c, UCL_CHARACTER_ESCAPE)) {
				if (c == 'u') {
					ucl_chunk_skipc (chunk, p);
					for (i = 0; i < 4 && p < chunk->end; i ++) {
						if (!isxdigit (*p)) {
							ucl_set_err (parser, UCL_ESYNTAX, "invalid utf escape",
									&parser->err);
							return false;
						}
						ucl_chunk_skipc (chunk, p);
					}
					if (p >= chunk->end) {
						ucl_set_err (parser, UCL_ESYNTAX, "unfinished escape character",
								&parser->err);
						return false;
					}
				}
				else {
					ucl_chunk_skipc (chunk, p);
				}
			}
			*need_unescape = true;
			*ucl_escape = true;
			continue;
		}
		else if (c == '"') {
			ucl_chunk_skipc (chunk, p);
			return true;
		}
		else if (ucl_test_character (c, UCL_CHARACTER_UCL_UNSAFE)) {
			*ucl_escape = true;
		}
		else if (c == '$') {
			*var_expand = true;
		}
		ucl_chunk_skipc (chunk, p);
	}

	ucl_set_err (parser, UCL_ESYNTAX, "no quote at the end of json string",
			&parser->err);
	return false;
}

static void
ucl_parser_append_elt (struct ucl_parser *parser, ucl_hash_t *cont,
		ucl_object_t *top,
		ucl_object_t *elt)
{
	ucl_object_t *nobj;

	if ((parser->flags & UCL_PARSER_NO_IMPLICIT_ARRAYS) == 0) {
		/* Implicit array */
		top->flags |= UCL_OBJECT_MULTIVALUE;
		DL_APPEND (top, elt);
		parser->stack->obj->len ++;
	}
	else {
		if ((top->flags & UCL_OBJECT_MULTIVALUE) != 0) {
			/* Just add to the explicit array */
			ucl_array_append (top, elt);
		}
		else {
			/* Convert to an array */
			nobj = ucl_object_typed_new (UCL_ARRAY);
			nobj->key = top->key;
			nobj->keylen = top->keylen;
			nobj->flags |= UCL_OBJECT_MULTIVALUE;
			ucl_array_append (nobj, top);
			ucl_array_append (nobj, elt);
			ucl_hash_replace (cont, top, nobj);
		}
	}
}

bool
ucl_parser_process_object_element (struct ucl_parser *parser, ucl_object_t *nobj)
{
	ucl_hash_t *container;
	ucl_object_t *tobj;
	char errmsg[256];

	container = parser->stack->obj->value.ov;

	tobj = __DECONST (ucl_object_t *, ucl_hash_search_obj (container, nobj));
	if (tobj == NULL) {
		container = ucl_hash_insert_object (container, nobj,
				parser->flags & UCL_PARSER_KEY_LOWERCASE);
		nobj->prev = nobj;
		nobj->next = NULL;
		parser->stack->obj->len ++;
	}
	else {
		unsigned priold = ucl_object_get_priority (tobj),
				prinew = ucl_object_get_priority (nobj);
		switch (parser->chunks->strategy) {

		case UCL_DUPLICATE_APPEND:
			/*
			 * The logic here is the following:
			 *
			 * - if we have two objects with the same priority, then we form an
			 * implicit or explicit array
			 * - if a new object has bigger priority, then we overwrite an old one
			 * - if a new object has lower priority, then we ignore it
			 */


			/* Special case for inherited objects */
			if (tobj->flags & UCL_OBJECT_INHERITED) {
				prinew = priold + 1;
			}

			if (priold == prinew) {
				ucl_parser_append_elt (parser, container, tobj, nobj);
			}
			else if (priold > prinew) {
				/*
				 * We add this new object to a list of trash objects just to ensure
				 * that it won't come to any real object
				 * XXX: rather inefficient approach
				 */
				DL_APPEND (parser->trash_objs, nobj);
			}
			else {
				ucl_hash_replace (container, tobj, nobj);
				ucl_object_unref (tobj);
			}

			break;

		case UCL_DUPLICATE_REWRITE:
			/* We just rewrite old values regardless of priority */
			ucl_hash_replace (container, tobj, nobj);
			ucl_object_unref (tobj);

			break;

		case UCL_DUPLICATE_ERROR:
			snprintf(errmsg, sizeof(errmsg),
					"duplicate element for key '%s' found",
					nobj->key);
			ucl_set_err (parser, UCL_EMERGE, errmsg, &parser->err);
			return false;

		case UCL_DUPLICATE_MERGE:
			/*
			 * Here we do have some old object so we just push it on top of objects stack
			 * Check priority and then perform the merge on the remaining objects
			 */
			if (tobj->type == UCL_OBJECT || tobj->type == UCL_ARRAY) {
				ucl_object_unref (nobj);
				nobj = tobj;
			}
			else if (priold == prinew) {
				ucl_parser_append_elt (parser, container, tobj, nobj);
			}
			else if (priold > prinew) {
				/*
				 * We add this new object to a list of trash objects just to ensure
				 * that it won't come to any real object
				 * XXX: rather inefficient approach
				 */
				DL_APPEND (parser->trash_objs, nobj);
			}
			else {
				ucl_hash_replace (container, tobj, nobj);
				ucl_object_unref (tobj);
			}
			break;
		}
	}

	parser->stack->obj->value.ov = container;
	parser->cur_obj = nobj;
	ucl_attach_comment (parser, nobj, false);

	return true;
}

/**
 * Parse a key in an object
 * @param parser
 * @param chunk
 * @param next_key
 * @param end_of_object
 * @return true if a key has been parsed
 */
static bool
ucl_parse_key (struct ucl_parser *parser, struct ucl_chunk *chunk,
		bool *next_key, bool *end_of_object)
{
	const unsigned char *p, *c = NULL, *end, *t;
	const char *key = NULL;
	bool got_quote = false, got_eq = false, got_semicolon = false,
			need_unescape = false, ucl_escape = false, var_expand = false,
			got_content = false, got_sep = false;
	ucl_object_t *nobj;
	ssize_t keylen;

	p = chunk->pos;

	if (*p == '.') {
		/* It is macro actually */
		if (!(parser->flags & UCL_PARSER_DISABLE_MACRO)) {
			ucl_chunk_skipc (chunk, p);
		}

		parser->prev_state = parser->state;
		parser->state = UCL_STATE_MACRO_NAME;
		*end_of_object = false;
		return true;
	}
	while (p < chunk->end) {
		/*
		 * A key must start with alpha, number, '/' or '_' and end with space character
		 */
		if (c == NULL) {
			if (chunk->remain >= 2 && ucl_lex_is_comment (p[0], p[1])) {
				if (!ucl_skip_comments (parser)) {
					return false;
				}
				p = chunk->pos;
			}
			else if (ucl_test_character (*p, UCL_CHARACTER_WHITESPACE_UNSAFE)) {
				ucl_chunk_skipc (chunk, p);
			}
			else if (ucl_test_character (*p, UCL_CHARACTER_KEY_START)) {
				/* The first symbol */
				c = p;
				ucl_chunk_skipc (chunk, p);
				got_content = true;
			}
			else if (*p == '"') {
				/* JSON style key */
				c = p + 1;
				got_quote = true;
				got_content = true;
				ucl_chunk_skipc (chunk, p);
			}
			else if (*p == '}') {
				/* We have actually end of an object */
				*end_of_object = true;
				return true;
			}
			else if (*p == '.') {
				ucl_chunk_skipc (chunk, p);
				parser->prev_state = parser->state;
				parser->state = UCL_STATE_MACRO_NAME;
				return true;
			}
			else {
				/* Invalid identifier */
				ucl_set_err (parser, UCL_ESYNTAX, "key must begin with a letter",
						&parser->err);
				return false;
			}
		}
		else {
			/* Parse the body of a key */
			if (!got_quote) {
				if (ucl_test_character (*p, UCL_CHARACTER_KEY)) {
					got_content = true;
					ucl_chunk_skipc (chunk, p);
				}
				else if (ucl_test_character (*p, UCL_CHARACTER_KEY_SEP)) {
					end = p;
					break;
				}
				else {
					ucl_set_err (parser, UCL_ESYNTAX, "invalid character in a key",
							&parser->err);
					return false;
				}
			}
			else {
				/* We need to parse json like quoted string */
				if (!ucl_lex_json_string (parser, chunk, &need_unescape, &ucl_escape, &var_expand)) {
					return false;
				}
				/* Always escape keys obtained via json */
				end = chunk->pos - 1;
				p = chunk->pos;
				break;
			}
		}
	}

	if (p >= chunk->end && got_content) {
		ucl_set_err (parser, UCL_ESYNTAX, "unfinished key", &parser->err);
		return false;
	}
	else if (!got_content) {
		return true;
	}
	*end_of_object = false;
	/* We are now at the end of the key, need to parse the rest */
	while (p < chunk->end) {
		if (ucl_test_character (*p, UCL_CHARACTER_WHITESPACE)) {
			ucl_chunk_skipc (chunk, p);
		}
		else if (*p == '=') {
			if (!got_eq && !got_semicolon) {
				ucl_chunk_skipc (chunk, p);
				got_eq = true;
			}
			else {
				ucl_set_err (parser, UCL_ESYNTAX, "unexpected '=' character",
						&parser->err);
				return false;
			}
		}
		else if (*p == ':') {
			if (!got_eq && !got_semicolon) {
				ucl_chunk_skipc (chunk, p);
				got_semicolon = true;
			}
			else {
				ucl_set_err (parser, UCL_ESYNTAX, "unexpected ':' character",
						&parser->err);
				return false;
			}
		}
		else if (chunk->remain >= 2 && ucl_lex_is_comment (p[0], p[1])) {
			/* Check for comment */
			if (!ucl_skip_comments (parser)) {
				return false;
			}
			p = chunk->pos;
		}
		else {
			/* Start value */
			break;
		}
	}

	if (p >= chunk->end && got_content) {
		ucl_set_err (parser, UCL_ESYNTAX, "unfinished key", &parser->err);
		return false;
	}

	got_sep = got_semicolon || got_eq;

	if (!got_sep) {
		/*
		 * Maybe we have more keys nested, so search for termination character.
		 * Possible choices:
		 * 1) key1 key2 ... keyN [:=] value <- we treat that as error
		 * 2) key1 ... keyN {} or [] <- we treat that as nested objects
		 * 3) key1 value[;,\n] <- we treat that as linear object
		 */
		t = p;
		*next_key = false;
		while (ucl_test_character (*t, UCL_CHARACTER_WHITESPACE)) {
			t ++;
		}
		/* Check first non-space character after a key */
		if (*t != '{' && *t != '[') {
			while (t < chunk->end) {
				if (*t == ',' || *t == ';' || *t == '\n' || *t == '\r') {
					break;
				}
				else if (*t == '{' || *t == '[') {
					*next_key = true;
					break;
				}
				t ++;
			}
		}
	}

	/* Create a new object */
	nobj = ucl_object_new_full (UCL_NULL, parser->chunks->priority);
	keylen = ucl_copy_or_store_ptr (parser, c, &nobj->trash_stack[UCL_TRASH_KEY],
			&key, end - c, need_unescape, parser->flags & UCL_PARSER_KEY_LOWERCASE, false);
	if (keylen == -1) {
		ucl_object_unref (nobj);
		return false;
	}
	else if (keylen == 0) {
		ucl_set_err (parser, UCL_ESYNTAX, "empty keys are not allowed", &parser->err);
		ucl_object_unref (nobj);
		return false;
	}

	nobj->key = key;
	nobj->keylen = keylen;

	if (!ucl_parser_process_object_element (parser, nobj)) {
		return false;
	}

	if (ucl_escape) {
		nobj->flags |= UCL_OBJECT_NEED_KEY_ESCAPE;
	}


	return true;
}

/**
 * Parse a cl string
 * @param parser
 * @param chunk
 * @param var_expand
 * @param need_unescape
 * @return true if a key has been parsed
 */
static bool
ucl_parse_string_value (struct ucl_parser *parser,
		struct ucl_chunk *chunk, bool *var_expand, bool *need_unescape)
{
	const unsigned char *p;
	enum {
		UCL_BRACE_ROUND = 0,
		UCL_BRACE_SQUARE,
		UCL_BRACE_FIGURE
	};
	int braces[3][2] = {{0, 0}, {0, 0}, {0, 0}};

	p = chunk->pos;

	while (p < chunk->end) {

		/* Skip pairs of figure braces */
		if (*p == '{') {
			braces[UCL_BRACE_FIGURE][0] ++;
		}
		else if (*p == '}') {
			braces[UCL_BRACE_FIGURE][1] ++;
			if (braces[UCL_BRACE_FIGURE][1] <= braces[UCL_BRACE_FIGURE][0]) {
				/* This is not a termination symbol, continue */
				ucl_chunk_skipc (chunk, p);
				continue;
			}
		}
		/* Skip pairs of square braces */
		else if (*p == '[') {
			braces[UCL_BRACE_SQUARE][0] ++;
		}
		else if (*p == ']') {
			braces[UCL_BRACE_SQUARE][1] ++;
			if (braces[UCL_BRACE_SQUARE][1] <= braces[UCL_BRACE_SQUARE][0]) {
				/* This is not a termination symbol, continue */
				ucl_chunk_skipc (chunk, p);
				continue;
			}
		}
		else if (*p == '$') {
			*var_expand = true;
		}
		else if (*p == '\\') {
			*need_unescape = true;
			ucl_chunk_skipc (chunk, p);
			if (p < chunk->end) {
				ucl_chunk_skipc (chunk, p);
			}
			continue;
		}

		if (ucl_lex_is_atom_end (*p) || (chunk->remain >= 2 && ucl_lex_is_comment (p[0], p[1]))) {
			break;
		}
		ucl_chunk_skipc (chunk, p);
	}

	return true;
}

/**
 * Parse multiline string ending with \n{term}\n
 * @param parser
 * @param chunk
 * @param term
 * @param term_len
 * @param beg
 * @param var_expand
 * @return size of multiline string or 0 in case of error
 */
static int
ucl_parse_multiline_string (struct ucl_parser *parser,
		struct ucl_chunk *chunk, const unsigned char *term,
		int term_len, unsigned char const **beg,
		bool *var_expand)
{
	const unsigned char *p, *c, *tend;
	bool newline = false;
	int len = 0;

	p = chunk->pos;

	c = p;

	while (p < chunk->end) {
		if (newline) {
			if (chunk->end - p < term_len) {
				return 0;
			}
			else if (memcmp (p, term, term_len) == 0) {
				tend = p + term_len;
				if (*tend != '\n' && *tend != ';' && *tend != ',') {
					/* Incomplete terminator */
					ucl_chunk_skipc (chunk, p);
					continue;
				}
				len = p - c;
				chunk->remain -= term_len;
				chunk->pos = p + term_len;
				chunk->column = term_len;
				*beg = c;
				break;
			}
		}
		if (*p == '\n') {
			newline = true;
		}
		else {
			if (*p == '$') {
				*var_expand = true;
			}
			newline = false;
		}
		ucl_chunk_skipc (chunk, p);
	}

	return len;
}

static inline ucl_object_t*
ucl_parser_get_container (struct ucl_parser *parser)
{
	ucl_object_t *t, *obj = NULL;

	if (parser == NULL || parser->stack == NULL || parser->stack->obj == NULL) {
		return NULL;
	}

	if (parser->stack->obj->type == UCL_ARRAY) {
		/* Object must be allocated */
		obj = ucl_object_new_full (UCL_NULL, parser->chunks->priority);
		t = parser->stack->obj;

		if (!ucl_array_append (t, obj)) {
			ucl_object_unref (obj);
			return NULL;
		}

		parser->cur_obj = obj;
		ucl_attach_comment (parser, obj, false);
	}
	else {
		/* Object has been already allocated */
		obj = parser->cur_obj;
	}

	return obj;
}

/**
 * Handle value data
 * @param parser
 * @param chunk
 * @return
 */
static bool
ucl_parse_value (struct ucl_parser *parser, struct ucl_chunk *chunk)
{
	const unsigned char *p, *c;
	ucl_object_t *obj = NULL;
	unsigned int stripped_spaces;
	int str_len;
	bool need_unescape = false, ucl_escape = false, var_expand = false;

	p = chunk->pos;

	/* Skip any spaces and comments */
	if (ucl_test_character (*p, UCL_CHARACTER_WHITESPACE_UNSAFE) ||
			(chunk->remain >= 2 && ucl_lex_is_comment (p[0], p[1]))) {
		while (p < chunk->end && ucl_test_character (*p, UCL_CHARACTER_WHITESPACE_UNSAFE)) {
			ucl_chunk_skipc (chunk, p);
		}
		if (!ucl_skip_comments (parser)) {
			return false;
		}
		p = chunk->pos;
	}

	while (p < chunk->end) {
		c = p;
		switch (*p) {
		case '"':
			ucl_chunk_skipc (chunk, p);

			if (!ucl_lex_json_string (parser, chunk, &need_unescape, &ucl_escape,
					&var_expand)) {
				return false;
			}

			obj = ucl_parser_get_container (parser);
			if (!obj) {
				return false;
			}

			str_len = chunk->pos - c - 2;
			obj->type = UCL_STRING;
			if ((str_len = ucl_copy_or_store_ptr (parser, c + 1,
					&obj->trash_stack[UCL_TRASH_VALUE],
					&obj->value.sv, str_len, need_unescape, false,
					var_expand)) == -1) {
				return false;
			}
			obj->len = str_len;

			parser->state = UCL_STATE_AFTER_VALUE;
			p = chunk->pos;

			return true;
			break;
		case '{':
			obj = ucl_parser_get_container (parser);
			/* We have a new object */
			obj = ucl_parser_add_container (obj, parser, false, parser->stack->level);
			if (obj == NULL) {
				return false;
			}

			ucl_chunk_skipc (chunk, p);

			return true;
			break;
		case '[':
			obj = ucl_parser_get_container (parser);
			/* We have a new array */
			obj = ucl_parser_add_container (obj, parser, true, parser->stack->level);
			if (obj == NULL) {
				return false;
			}

			ucl_chunk_skipc (chunk, p);

			return true;
			break;
		case ']':
			/* We have the array ending */
			if (parser->stack && parser->stack->obj->type == UCL_ARRAY) {
				parser->state = UCL_STATE_AFTER_VALUE;
				return true;
			}
			else {
				goto parse_string;
			}
			break;
		case '<':
			obj = ucl_parser_get_container (parser);
			/* We have something like multiline value, which must be <<[A-Z]+\n */
			if (chunk->end - p > 3) {
				if (memcmp (p, "<<", 2) == 0) {
					p += 2;
					/* We allow only uppercase characters in multiline definitions */
					while (p < chunk->end && *p >= 'A' && *p <= 'Z') {
						p ++;
					}
					if (*p =='\n') {
						/* Set chunk positions and start multiline parsing */
						c += 2;
						chunk->remain -= p - c;
						chunk->pos = p + 1;
						chunk->column = 0;
						chunk->line ++;
						if ((str_len = ucl_parse_multiline_string (parser, chunk, c,
								p - c, &c, &var_expand)) == 0) {
							ucl_set_err (parser, UCL_ESYNTAX,
									"unterminated multiline value", &parser->err);
							return false;
						}

						obj->type = UCL_STRING;
						obj->flags |= UCL_OBJECT_MULTILINE;
						if ((str_len = ucl_copy_or_store_ptr (parser, c,
								&obj->trash_stack[UCL_TRASH_VALUE],
								&obj->value.sv, str_len - 1, false,
								false, var_expand)) == -1) {
							return false;
						}
						obj->len = str_len;

						parser->state = UCL_STATE_AFTER_VALUE;

						return true;
					}
				}
			}
			/* Fallback to ordinary strings */
		default:
parse_string:
			if (obj == NULL) {
				obj = ucl_parser_get_container (parser);
			}

			/* Parse atom */
			if (ucl_test_character (*p, UCL_CHARACTER_VALUE_DIGIT_START)) {
				if (!ucl_lex_number (parser, chunk, obj)) {
					if (parser->state == UCL_STATE_ERROR) {
						return false;
					}
				}
				else {
					parser->state = UCL_STATE_AFTER_VALUE;
					return true;
				}
				/* Fallback to normal string */
			}

			if (!ucl_parse_string_value (parser, chunk, &var_expand,
					&need_unescape)) {
				return false;
			}
			/* Cut trailing spaces */
			stripped_spaces = 0;
			while (ucl_test_character (*(chunk->pos - 1 - stripped_spaces),
					UCL_CHARACTER_WHITESPACE)) {
				stripped_spaces ++;
			}
			str_len = chunk->pos - c - stripped_spaces;
			if (str_len <= 0) {
				ucl_set_err (parser, UCL_ESYNTAX, "string value must not be empty",
						&parser->err);
				return false;
			}
			else if (str_len == 4 && memcmp (c, "null", 4) == 0) {
				obj->len = 0;
				obj->type = UCL_NULL;
			}
			else if (!ucl_maybe_parse_boolean (obj, c, str_len)) {
				obj->type = UCL_STRING;
				if ((str_len = ucl_copy_or_store_ptr (parser, c,
						&obj->trash_stack[UCL_TRASH_VALUE],
						&obj->value.sv, str_len, need_unescape,
						false, var_expand)) == -1) {
					return false;
				}
				obj->len = str_len;
			}
			parser->state = UCL_STATE_AFTER_VALUE;
			p = chunk->pos;

			return true;
			break;
		}
	}

	return true;
}

/**
 * Handle after value data
 * @param parser
 * @param chunk
 * @return
 */
static bool
ucl_parse_after_value (struct ucl_parser *parser, struct ucl_chunk *chunk)
{
	const unsigned char *p;
	bool got_sep = false;
	struct ucl_stack *st;

	p = chunk->pos;

	while (p < chunk->end) {
		if (ucl_test_character (*p, UCL_CHARACTER_WHITESPACE)) {
			/* Skip whitespaces */
			ucl_chunk_skipc (chunk, p);
		}
		else if (chunk->remain >= 2 && ucl_lex_is_comment (p[0], p[1])) {
			/* Skip comment */
			if (!ucl_skip_comments (parser)) {
				return false;
			}
			/* Treat comment as a separator */
			got_sep = true;
			p = chunk->pos;
		}
		else if (ucl_test_character (*p, UCL_CHARACTER_VALUE_END)) {
			if (*p == '}' || *p == ']') {
				if (parser->stack == NULL) {
					ucl_set_err (parser, UCL_ESYNTAX,
							"end of array or object detected without corresponding start",
							&parser->err);
					return false;
				}
				if ((*p == '}' && parser->stack->obj->type == UCL_OBJECT) ||
						(*p == ']' && parser->stack->obj->type == UCL_ARRAY)) {

					/* Pop all nested objects from a stack */
					st = parser->stack;
					parser->stack = st->next;
					UCL_FREE (sizeof (struct ucl_stack), st);

					if (parser->cur_obj) {
						ucl_attach_comment (parser, parser->cur_obj, true);
					}

					while (parser->stack != NULL) {
						st = parser->stack;

						if (st->next == NULL || st->next->level == st->level) {
							break;
						}

						parser->stack = st->next;
						parser->cur_obj = st->obj;
						UCL_FREE (sizeof (struct ucl_stack), st);
					}
				}
				else {
					ucl_set_err (parser, UCL_ESYNTAX,
							"unexpected terminating symbol detected",
							&parser->err);
					return false;
				}

				if (parser->stack == NULL) {
					/* Ignore everything after a top object */
					return true;
				}
				else {
					ucl_chunk_skipc (chunk, p);
				}
				got_sep = true;
			}
			else {
				/* Got a separator */
				got_sep = true;
				ucl_chunk_skipc (chunk, p);
			}
		}
		else {
			/* Anything else */
			if (!got_sep) {
				ucl_set_err (parser, UCL_ESYNTAX, "delimiter is missing",
						&parser->err);
				return false;
			}
			return true;
		}
	}

	return true;
}

static bool
ucl_skip_macro_as_comment (struct ucl_parser *parser,
		struct ucl_chunk *chunk)
{
	const unsigned char *p, *c;
	enum {
		macro_skip_start = 0,
		macro_has_symbols,
		macro_has_obrace,
		macro_has_quote,
		macro_has_backslash,
		macro_has_sqbrace,
		macro_save
	} state = macro_skip_start, prev_state = macro_skip_start;

	p = chunk->pos;
	c = chunk->pos;

	while (p < chunk->end) {
		switch (state) {
		case macro_skip_start:
			if (!ucl_test_character (*p, UCL_CHARACTER_WHITESPACE)) {
				state = macro_has_symbols;
			}
			else if (ucl_test_character (*p, UCL_CHARACTER_WHITESPACE_UNSAFE)) {
				state = macro_save;
				continue;
			}

			ucl_chunk_skipc (chunk, p);
			break;

		case macro_has_symbols:
			if (*p == '{') {
				state = macro_has_sqbrace;
			}
			else if (*p == '(') {
				state = macro_has_obrace;
			}
			else if (*p == '"') {
				state = macro_has_quote;
			}
			else if (*p == '\n') {
				state = macro_save;
				continue;
			}

			ucl_chunk_skipc (chunk, p);
			break;

		case macro_has_obrace:
			if (*p == '\\') {
				prev_state = state;
				state = macro_has_backslash;
			}
			else if (*p == ')') {
				state = macro_has_symbols;
			}

			ucl_chunk_skipc (chunk, p);
			break;

		case macro_has_sqbrace:
			if (*p == '\\') {
				prev_state = state;
				state = macro_has_backslash;
			}
			else if (*p == '}') {
				state = macro_save;
			}

			ucl_chunk_skipc (chunk, p);
			break;

		case macro_has_quote:
			if (*p == '\\') {
				prev_state = state;
				state = macro_has_backslash;
			}
			else if (*p == '"') {
				state = macro_save;
			}

			ucl_chunk_skipc (chunk, p);
			break;

		case macro_has_backslash:
			state = prev_state;
			ucl_chunk_skipc (chunk, p);
			break;

		case macro_save:
			if (parser->flags & UCL_PARSER_SAVE_COMMENTS) {
				ucl_save_comment (parser, c, p - c);
			}

			return true;
		}
	}

	return false;
}

/**
 * Handle macro data
 * @param parser
 * @param chunk
 * @param marco
 * @param macro_start
 * @param macro_len
 * @return
 */
static bool
ucl_parse_macro_value (struct ucl_parser *parser,
		struct ucl_chunk *chunk, struct ucl_macro *macro,
		unsigned char const **macro_start, size_t *macro_len)
{
	const unsigned char *p, *c;
	bool need_unescape = false, ucl_escape = false, var_expand = false;

	p = chunk->pos;

	switch (*p) {
	case '"':
		/* We have macro value encoded in quotes */
		c = p;
		ucl_chunk_skipc (chunk, p);
		if (!ucl_lex_json_string (parser, chunk, &need_unescape, &ucl_escape, &var_expand)) {
			return false;
		}

		*macro_start = c + 1;
		*macro_len = chunk->pos - c - 2;
		p = chunk->pos;
		break;
	case '{':
		/* We got a multiline macro body */
		ucl_chunk_skipc (chunk, p);
		/* Skip spaces at the beginning */
		while (p < chunk->end) {
			if (ucl_test_character (*p, UCL_CHARACTER_WHITESPACE_UNSAFE)) {
				ucl_chunk_skipc (chunk, p);
			}
			else {
				break;
			}
		}
		c = p;
		while (p < chunk->end) {
			if (*p == '}') {
				break;
			}
			ucl_chunk_skipc (chunk, p);
		}
		*macro_start = c;
		*macro_len = p - c;
		ucl_chunk_skipc (chunk, p);
		break;
	default:
		/* Macro is not enclosed in quotes or braces */
		c = p;
		while (p < chunk->end) {
			if (ucl_lex_is_atom_end (*p)) {
				break;
			}
			ucl_chunk_skipc (chunk, p);
		}
		*macro_start = c;
		*macro_len = p - c;
		break;
	}

	/* We are at the end of a macro */
	/* Skip ';' and space characters and return to previous state */
	while (p < chunk->end) {
		if (!ucl_test_character (*p, UCL_CHARACTER_WHITESPACE_UNSAFE) && *p != ';') {
			break;
		}
		ucl_chunk_skipc (chunk, p);
	}
	return true;
}

/**
 * Parse macro arguments as UCL object
 * @param parser parser structure
 * @param chunk the current data chunk
 * @return
 */
static ucl_object_t *
ucl_parse_macro_arguments (struct ucl_parser *parser,
		struct ucl_chunk *chunk)
{
	ucl_object_t *res = NULL;
	struct ucl_parser *params_parser;
	int obraces = 1, ebraces = 0, state = 0;
	const unsigned char *p, *c;
	size_t args_len = 0;
	struct ucl_parser_saved_state saved;

	saved.column = chunk->column;
	saved.line = chunk->line;
	saved.pos = chunk->pos;
	saved.remain = chunk->remain;
	p = chunk->pos;

	if (*p != '(' || chunk->remain < 2) {
		return NULL;
	}

	/* Set begin and start */
	ucl_chunk_skipc (chunk, p);
	c = p;

	while ((p) < (chunk)->end) {
		switch (state) {
		case 0:
			/* Parse symbols and check for '(', ')' and '"' */
			if (*p == '(') {
				obraces ++;
			}
			else if (*p == ')') {
				ebraces ++;
			}
			else if (*p == '"') {
				state = 1;
			}
			/* Check pairing */
			if (obraces == ebraces) {
				state = 99;
			}
			else {
				args_len ++;
			}
			/* Check overflow */
			if (chunk->remain == 0) {
				goto restore_chunk;
			}
			ucl_chunk_skipc (chunk, p);
			break;
		case 1:
			/* We have quote character, so skip all but quotes */
			if (*p == '"' && *(p - 1) != '\\') {
				state = 0;
			}
			if (chunk->remain == 0) {
				goto restore_chunk;
			}
			args_len ++;
			ucl_chunk_skipc (chunk, p);
			break;
		case 99:
			/*
			 * We have read the full body of arguments, so we need to parse and set
			 * object from that
			 */
			params_parser = ucl_parser_new (parser->flags);
			if (!ucl_parser_add_chunk (params_parser, c, args_len)) {
				ucl_set_err (parser, UCL_ESYNTAX, "macro arguments parsing error",
						&parser->err);
			}
			else {
				res = ucl_parser_get_object (params_parser);
			}
			ucl_parser_free (params_parser);

			return res;

			break;
		}
	}

	return res;

restore_chunk:
	chunk->column = saved.column;
	chunk->line = saved.line;
	chunk->pos = saved.pos;
	chunk->remain = saved.remain;

	return NULL;
}

#define SKIP_SPACES_COMMENTS(parser, chunk, p) do {								\
	while ((p) < (chunk)->end) {												\
		if (!ucl_test_character (*(p), UCL_CHARACTER_WHITESPACE_UNSAFE)) {		\
			if ((chunk)->remain >= 2 && ucl_lex_is_comment ((p)[0], (p)[1])) {	\
				if (!ucl_skip_comments (parser)) {								\
					return false;												\
				}																\
				p = (chunk)->pos;												\
			}																	\
			break;																\
		}																		\
		ucl_chunk_skipc (chunk, p);												\
	}																			\
} while(0)

/**
 * Handle the main states of rcl parser
 * @param parser parser structure
 * @return true if chunk has been parsed and false in case of error
 */
static bool
ucl_state_machine (struct ucl_parser *parser)
{
	ucl_object_t *obj, *macro_args;
	struct ucl_chunk *chunk = parser->chunks;
	const unsigned char *p, *c = NULL, *macro_start = NULL;
	unsigned char *macro_escaped;
	size_t macro_len = 0;
	struct ucl_macro *macro = NULL;
	bool next_key = false, end_of_object = false, ret;

	if (parser->top_obj == NULL) {
		parser->state = UCL_STATE_INIT;
	}

	p = chunk->pos;
	while (chunk->pos < chunk->end) {
		switch (parser->state) {
		case UCL_STATE_INIT:
			/*
			 * At the init state we can either go to the parse array or object
			 * if we got [ or { correspondingly or can just treat new data as
			 * a key of newly created object
			 */
			if (!ucl_skip_comments (parser)) {
				parser->prev_state = parser->state;
				parser->state = UCL_STATE_ERROR;
				return false;
			}
			else {
				/* Skip any spaces */
				while (p < chunk->end && ucl_test_character (*p,
						UCL_CHARACTER_WHITESPACE_UNSAFE)) {
					ucl_chunk_skipc (chunk, p);
				}

				p = chunk->pos;

				if (*p == '[') {
					parser->state = UCL_STATE_VALUE;
					ucl_chunk_skipc (chunk, p);
				}
				else {
					parser->state = UCL_STATE_KEY;
					if (*p == '{') {
						ucl_chunk_skipc (chunk, p);
					}
				}

				if (parser->top_obj == NULL) {
					if (parser->state == UCL_STATE_VALUE) {
						obj = ucl_parser_add_container (NULL, parser, true, 0);
					}
					else {
						obj = ucl_parser_add_container (NULL, parser, false, 0);
					}

					if (obj == NULL) {
						return false;
					}

					parser->top_obj = obj;
					parser->cur_obj = obj;
				}

			}
			break;
		case UCL_STATE_KEY:
			/* Skip any spaces */
			while (p < chunk->end && ucl_test_character (*p, UCL_CHARACTER_WHITESPACE_UNSAFE)) {
				ucl_chunk_skipc (chunk, p);
			}
			if (p == chunk->end || *p == '}') {
				/* We have the end of an object */
				parser->state = UCL_STATE_AFTER_VALUE;
				continue;
			}
			if (parser->stack == NULL) {
				/* No objects are on stack, but we want to parse a key */
				ucl_set_err (parser, UCL_ESYNTAX, "top object is finished but the parser "
						"expects a key", &parser->err);
				parser->prev_state = parser->state;
				parser->state = UCL_STATE_ERROR;
				return false;
			}
			if (!ucl_parse_key (parser, chunk, &next_key, &end_of_object)) {
				parser->prev_state = parser->state;
				parser->state = UCL_STATE_ERROR;
				return false;
			}
			if (end_of_object) {
				p = chunk->pos;
				parser->state = UCL_STATE_AFTER_VALUE;
				continue;
			}
			else if (parser->state != UCL_STATE_MACRO_NAME) {
				if (next_key && parser->stack->obj->type == UCL_OBJECT) {
					/* Parse more keys and nest objects accordingly */
					obj = ucl_parser_add_container (parser->cur_obj, parser, false,
							parser->stack->level + 1);
					if (obj == NULL) {
						return false;
					}
				}
				else {
					parser->state = UCL_STATE_VALUE;
				}
			}
			else {
				c = chunk->pos;
			}
			p = chunk->pos;
			break;
		case UCL_STATE_VALUE:
			/* We need to check what we do have */
			if (!parser->cur_obj || !ucl_parse_value (parser, chunk)) {
				parser->prev_state = parser->state;
				parser->state = UCL_STATE_ERROR;
				return false;
			}
			/* State is set in ucl_parse_value call */
			p = chunk->pos;
			break;
		case UCL_STATE_AFTER_VALUE:
			if (!ucl_parse_after_value (parser, chunk)) {
				parser->prev_state = parser->state;
				parser->state = UCL_STATE_ERROR;
				return false;
			}

			if (parser->stack != NULL) {
				if (parser->stack->obj->type == UCL_OBJECT) {
					parser->state = UCL_STATE_KEY;
				}
				else {
					/* Array */
					parser->state = UCL_STATE_VALUE;
				}
			}
			else {
				/* Skip everything at the end */
				return true;
			}

			p = chunk->pos;
			break;
		case UCL_STATE_MACRO_NAME:
			if (parser->flags & UCL_PARSER_DISABLE_MACRO) {
				if (!ucl_skip_macro_as_comment (parser, chunk)) {
					/* We have invalid macro */
					ucl_create_err (&parser->err,
							"error on line %d at column %d: invalid macro",
							chunk->line,
							chunk->column);
					parser->state = UCL_STATE_ERROR;
					return false;
				}
				else {
					p = chunk->pos;
					parser->state = parser->prev_state;
				}
			}
			else {
				if (!ucl_test_character (*p, UCL_CHARACTER_WHITESPACE_UNSAFE) &&
						*p != '(') {
					ucl_chunk_skipc (chunk, p);
				}
				else {
					if (c != NULL && p - c > 0) {
						/* We got macro name */
						macro_len = (size_t) (p - c);
						HASH_FIND (hh, parser->macroes, c, macro_len, macro);
						if (macro == NULL) {
							ucl_create_err (&parser->err,
									"error on line %d at column %d: "
									"unknown macro: '%.*s', character: '%c'",
									chunk->line,
									chunk->column,
									(int) (p - c),
									c,
									*chunk->pos);
							parser->state = UCL_STATE_ERROR;
							return false;
						}
						/* Now we need to skip all spaces */
						SKIP_SPACES_COMMENTS(parser, chunk, p);
						parser->state = UCL_STATE_MACRO;
					}
					else {
						/* We have invalid macro name */
						ucl_create_err (&parser->err,
								"error on line %d at column %d: invalid macro name",
								chunk->line,
								chunk->column);
						parser->state = UCL_STATE_ERROR;
						return false;
					}
				}
			}
			break;
		case UCL_STATE_MACRO:
			if (*chunk->pos == '(') {
				macro_args = ucl_parse_macro_arguments (parser, chunk);
				p = chunk->pos;
				if (macro_args) {
					SKIP_SPACES_COMMENTS(parser, chunk, p);
				}
			}
			else {
				macro_args = NULL;
			}
			if (!ucl_parse_macro_value (parser, chunk, macro,
					&macro_start, &macro_len)) {
				parser->prev_state = parser->state;
				parser->state = UCL_STATE_ERROR;
				return false;
			}
			macro_len = ucl_expand_variable (parser, &macro_escaped,
					macro_start, macro_len);
			parser->state = parser->prev_state;

			if (macro_escaped == NULL && macro != NULL) {
				if (macro->is_context) {
					ret = macro->h.context_handler (macro_start, macro_len,
							macro_args,
							parser->top_obj,
							macro->ud);
				}
				else {
					ret = macro->h.handler (macro_start, macro_len, macro_args,
							macro->ud);
				}
			}
			else if (macro != NULL) {
				if (macro->is_context) {
					ret = macro->h.context_handler (macro_escaped, macro_len,
							macro_args,
							parser->top_obj,
							macro->ud);
				}
				else {
					ret = macro->h.handler (macro_escaped, macro_len, macro_args,
						macro->ud);
				}

				UCL_FREE (macro_len + 1, macro_escaped);
			}
			else {
				ret = false;
				ucl_set_err (parser, UCL_EINTERNAL,
						"internal error: parser has macro undefined", &parser->err);
			}

			/*
			 * Chunk can be modified within macro handler
			 */
			chunk = parser->chunks;
			p = chunk->pos;

			if (macro_args) {
				ucl_object_unref (macro_args);
			}

			if (!ret) {
				return false;
			}
			break;
		default:
			ucl_set_err (parser, UCL_EINTERNAL,
					"internal error: parser is in an unknown state", &parser->err);
			parser->state = UCL_STATE_ERROR;
			return false;
		}
	}

	if (parser->last_comment) {
		if (parser->cur_obj) {
			ucl_attach_comment (parser, parser->cur_obj, true);
		}
		else if (parser->stack && parser->stack->obj) {
			ucl_attach_comment (parser, parser->stack->obj, true);
		}
		else if (parser->top_obj) {
			ucl_attach_comment (parser, parser->top_obj, true);
		}
		else {
			ucl_object_unref (parser->last_comment);
		}
	}

	return true;
}

struct ucl_parser*
ucl_parser_new (int flags)
{
	struct ucl_parser *parser;

	parser = UCL_ALLOC (sizeof (struct ucl_parser));
	if (parser == NULL) {
		return NULL;
	}

	memset (parser, 0, sizeof (struct ucl_parser));

	ucl_parser_register_macro (parser, "include", ucl_include_handler, parser);
	ucl_parser_register_macro (parser, "try_include", ucl_try_include_handler, parser);
	ucl_parser_register_macro (parser, "includes", ucl_includes_handler, parser);
	ucl_parser_register_macro (parser, "priority", ucl_priority_handler, parser);
	ucl_parser_register_macro (parser, "load", ucl_load_handler, parser);
	ucl_parser_register_context_macro (parser, "inherit", ucl_inherit_handler, parser);

	parser->flags = flags;
	parser->includepaths = NULL;

	if (flags & UCL_PARSER_SAVE_COMMENTS) {
		parser->comments = ucl_object_typed_new (UCL_OBJECT);
	}

	if (!(flags & UCL_PARSER_NO_FILEVARS)) {
		/* Initial assumption about filevars */
		ucl_parser_set_filevars (parser, NULL, false);
	}

	return parser;
}

bool
ucl_parser_set_default_priority (struct ucl_parser *parser, unsigned prio)
{
	if (parser == NULL) {
		return false;
	}

	parser->default_priority = prio;

	return true;
}

void
ucl_parser_register_macro (struct ucl_parser *parser, const char *macro,
		ucl_macro_handler handler, void* ud)
{
	struct ucl_macro *new;

	if (macro == NULL || handler == NULL) {
		return;
	}

	new = UCL_ALLOC (sizeof (struct ucl_macro));
	if (new == NULL) {
		return;
	}

	memset (new, 0, sizeof (struct ucl_macro));
	new->h.handler = handler;
	new->name = strdup (macro);
	new->ud = ud;
	HASH_ADD_KEYPTR (hh, parser->macroes, new->name, strlen (new->name), new);
}

void
ucl_parser_register_context_macro (struct ucl_parser *parser, const char *macro,
		ucl_context_macro_handler handler, void* ud)
{
	struct ucl_macro *new;

	if (macro == NULL || handler == NULL) {
		return;
	}

	new = UCL_ALLOC (sizeof (struct ucl_macro));
	if (new == NULL) {
		return;
	}

	memset (new, 0, sizeof (struct ucl_macro));
	new->h.context_handler = handler;
	new->name = strdup (macro);
	new->ud = ud;
	new->is_context = true;
	HASH_ADD_KEYPTR (hh, parser->macroes, new->name, strlen (new->name), new);
}

void
ucl_parser_register_variable (struct ucl_parser *parser, const char *var,
		const char *value)
{
	struct ucl_variable *new = NULL, *cur;

	if (var == NULL) {
		return;
	}

	/* Find whether a variable already exists */
	LL_FOREACH (parser->variables, cur) {
		if (strcmp (cur->var, var) == 0) {
			new = cur;
			break;
		}
	}

	if (value == NULL) {

		if (new != NULL) {
			/* Remove variable */
			DL_DELETE (parser->variables, new);
			free (new->var);
			free (new->value);
			UCL_FREE (sizeof (struct ucl_variable), new);
		}
		else {
			/* Do nothing */
			return;
		}
	}
	else {
		if (new == NULL) {
			new = UCL_ALLOC (sizeof (struct ucl_variable));
			if (new == NULL) {
				return;
			}
			memset (new, 0, sizeof (struct ucl_variable));
			new->var = strdup (var);
			new->var_len = strlen (var);
			new->value = strdup (value);
			new->value_len = strlen (value);

			DL_APPEND (parser->variables, new);
		}
		else {
			free (new->value);
			new->value = strdup (value);
			new->value_len = strlen (value);
		}
	}
}

void
ucl_parser_set_variables_handler (struct ucl_parser *parser,
		ucl_variable_handler handler, void *ud)
{
	parser->var_handler = handler;
	parser->var_data = ud;
}

bool
ucl_parser_add_chunk_full (struct ucl_parser *parser, const unsigned char *data,
		size_t len, unsigned priority, enum ucl_duplicate_strategy strat,
		enum ucl_parse_type parse_type)
{
	struct ucl_chunk *chunk;

	if (parser == NULL) {
		return false;
	}

	if (data == NULL && len != 0) {
		ucl_create_err (&parser->err, "invalid chunk added");
		return false;
	}

	if (parser->state != UCL_STATE_ERROR) {
		chunk = UCL_ALLOC (sizeof (struct ucl_chunk));
		if (chunk == NULL) {
			ucl_create_err (&parser->err, "cannot allocate chunk structure");
			return false;
		}

		if (parse_type == UCL_PARSE_AUTO && len > 0) {
			/* We need to detect parse type by the first symbol */
			if ((*data & 0x80) == 0x80 && (*data >= 0xdc && *data <= 0xdf)) {
				parse_type = UCL_PARSE_MSGPACK;
			}
			else if (*data == '(') {
				parse_type = UCL_PARSE_CSEXP;
			}
			else {
				parse_type = UCL_PARSE_UCL;
			}
		}

		chunk->begin = data;
		chunk->remain = len;
		chunk->pos = chunk->begin;
		chunk->end = chunk->begin + len;
		chunk->line = 1;
		chunk->column = 0;
		chunk->priority = priority;
		chunk->strategy = strat;
		chunk->parse_type = parse_type;
		LL_PREPEND (parser->chunks, chunk);
		parser->recursion ++;

		if (parser->recursion > UCL_MAX_RECURSION) {
			ucl_create_err (&parser->err, "maximum include nesting limit is reached: %d",
					parser->recursion);
			return false;
		}

		if (len > 0) {
			/* Need to parse something */
			switch (parse_type) {
			default:
			case UCL_PARSE_UCL:
				return ucl_state_machine (parser);
			case UCL_PARSE_MSGPACK:
				return ucl_parse_msgpack (parser);
			case UCL_PARSE_CSEXP:
				return ucl_parse_csexp (parser);
			}
		}
		else {
			/* Just add empty chunk and go forward */
			if (parser->top_obj == NULL) {
				/*
				 * In case of empty object, create one to indicate that we've
				 * read something
				 */
				parser->top_obj = ucl_object_new_full (UCL_OBJECT, priority);
			}

			return true;
		}
	}

	ucl_create_err (&parser->err, "a parser is in an invalid state");

	return false;
}

bool
ucl_parser_add_chunk_priority (struct ucl_parser *parser,
		const unsigned char *data, size_t len, unsigned priority)
{
	/* We dereference parser, so this check is essential */
	if (parser == NULL) {
		return false;
	}

	return ucl_parser_add_chunk_full (parser, data, len,
				priority, UCL_DUPLICATE_APPEND, UCL_PARSE_UCL);
}

bool
ucl_parser_add_chunk (struct ucl_parser *parser, const unsigned char *data,
		size_t len)
{
	if (parser == NULL) {
		return false;
	}

	return ucl_parser_add_chunk_full (parser, data, len,
			parser->default_priority, UCL_DUPLICATE_APPEND, UCL_PARSE_UCL);
}

bool
ucl_parser_add_string_priority (struct ucl_parser *parser, const char *data,
		size_t len, unsigned priority)
{
	if (data == NULL) {
		ucl_create_err (&parser->err, "invalid string added");
		return false;
	}
	if (len == 0) {
		len = strlen (data);
	}

	return ucl_parser_add_chunk_priority (parser,
			(const unsigned char *)data, len, priority);
}

bool
ucl_parser_add_string (struct ucl_parser *parser, const char *data,
		size_t len)
{
	if (parser == NULL) {
		return false;
	}

	return ucl_parser_add_string_priority (parser,
			(const unsigned char *)data, len, parser->default_priority);
}

bool
ucl_set_include_path (struct ucl_parser *parser, ucl_object_t *paths)
{
	if (parser == NULL || paths == NULL) {
		return false;
	}

	if (parser->includepaths == NULL) {
		parser->includepaths = ucl_object_copy (paths);
	}
	else {
		ucl_object_unref (parser->includepaths);
		parser->includepaths = ucl_object_copy (paths);
	}

	if (parser->includepaths == NULL) {
		return false;
	}

	return true;
}
