/*-
 * Copyright (c) 2008 Hyogeol Lee <hyogeollee@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <libelftc.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "_libelftc.h"

ELFTC_VCSID("$Id: libelftc_dem_arm.c 3513 2016-12-29 07:04:22Z kaiwang27 $");

/**
 * @file cpp_demangle_arm.c
 * @brief Decode function name encoding in ARM.
 *
 * Function name encoding in "The Annotated C++ Reference Manual".
 *
 * Ref : "The Annotated C++ Reference Manual", Margaet A.Ellis,
 *  Bjarne Stroustrup, AT&T Bell Laboratories 1990, pp 122-126.
 */

enum encode_type {
	ENCODE_FUNC, ENCODE_OP, ENCODE_OP_CT, ENCODE_OP_DT, ENCODE_OP_USER
};

struct cstring {
	char	*buf;
	size_t	size;
};

struct demangle_data {
	bool	ptr, ref, cnst, array;
	struct cstring array_str;
	const char *p;
	enum encode_type type;
	struct vector_str vec;
	struct vector_str arg;
};

#define SIMPLE_HASH(x,y)	(64 * x + y)
#define VEC_PUSH_STR(d,s)	vector_str_push((d), (s), strlen((s)))
#define	CPP_DEMANGLE_ARM_TRY	128

static void	dest_cstring(struct cstring *);
static void	dest_demangle_data(struct demangle_data *);
static bool	init_cstring(struct cstring *, size_t);
static bool	init_demangle_data(struct demangle_data *);
static bool	push_CTDT(const char *, size_t, struct vector_str *);
static bool	read_array(struct demangle_data *);
static bool	read_class(struct demangle_data *);
static bool	read_func(struct demangle_data *);
static bool	read_func_name(struct demangle_data *);
static bool	read_func_ptr(struct demangle_data *);
static bool	read_memptr(struct demangle_data *);
static bool	read_op(struct demangle_data *);
static bool	read_op_user(struct demangle_data *);
static bool	read_qual_name(struct demangle_data *);
static int	read_subst(struct demangle_data *);
static int	read_subst_iter(struct demangle_data *);
static bool	read_type(struct demangle_data *);

/**
 * @brief Decode the input string by the ARM style.
 *
 * @return New allocated demangled string or NULL if failed.
 */
char *
cpp_demangle_ARM(const char *org)
{
	struct demangle_data d;
	size_t arg_begin, arg_len;
	unsigned int try;
	char *rtn, *arg;

	if (org == NULL)
		return (NULL);

	if (init_demangle_data(&d) == false)
		return (NULL);

	try = 0;
	rtn = NULL;

	d.p = org;
	if (read_func_name(&d) == false)
		goto clean;

	if (d.type == ENCODE_OP_CT) {
		if (push_CTDT("::", 2, &d.vec) == false)
			goto clean;

		goto flat;
	}

	if (d.type == ENCODE_OP_DT) {
		if (push_CTDT("::~", 3, &d.vec) == false)
			goto clean;

		goto flat;
	}

	if (d.type == ENCODE_OP_USER)
		goto flat;

	/* function type */
	if (*d.p != 'F')
		goto clean;
	++d.p;

	/* start argument types */
	if (VEC_PUSH_STR(&d.vec, "(") == false)
		goto clean;

	for (;;) {
		if (*d.p == 'T') {
			const int rtn_subst = read_subst(&d);

			if (rtn_subst == -1)
				goto clean;
			else if (rtn_subst == 1)
				break;

			continue;
		}

		if (*d.p == 'N') {
			const int rtn_subst_iter = read_subst_iter(&d);

			if (rtn_subst_iter == -1)
				goto clean;
			else if(rtn_subst_iter == 1)
				break;

			continue;
		}

		arg_begin = d.vec.size;

		if (read_type(&d) == false)
			goto clean;

		if (d.ptr == true) {
			if (VEC_PUSH_STR(&d.vec, "*") == false)
				goto clean;

			d.ptr = false;
		}

		if (d.ref == true) {
			if (VEC_PUSH_STR(&d.vec, "&") == false)
				goto clean;

			d.ref = false;
		}

		if (d.cnst == true) {
			if (VEC_PUSH_STR(&d.vec, " const") == false)
				goto clean;

			d.cnst = false;
		}

		if (d.array == true) {
			if (vector_str_push(&d.vec, d.array_str.buf,
				d.array_str.size) == false)
				goto clean;

			dest_cstring(&d.array_str);
			d.array = false;
		}

		if (*d.p == '\0')
			break;

		if ((arg = vector_str_substr(&d.vec, arg_begin, d.vec.size - 1,
			    &arg_len)) == NULL)
			goto clean;

		if (vector_str_push(&d.arg, arg, arg_len) == false)
			goto clean;

		free(arg);

		if (VEC_PUSH_STR(&d.vec, ", ") == false)
			goto clean;

		if (++try > CPP_DEMANGLE_ARM_TRY)
			goto clean;
	}

	/* end argument types */
	if (VEC_PUSH_STR(&d.vec, ")") == false)
		goto clean;

flat:
	rtn = vector_str_get_flat(&d.vec, NULL);
clean:
	dest_demangle_data(&d);

	return (rtn);
}

/**
 * @brief Test input string is encoded by the ARM style.
 *
 * @return True if input string is encoded by the ARM style.
 */
bool
is_cpp_mangled_ARM(const char *org)
{

	if (org == NULL)
		return (false);

	return (strstr(org, "__") != NULL);
}

static void
dest_cstring(struct cstring *s)
{

	if (s == NULL)
		return;

	free(s->buf);
	s->buf = NULL;
	s->size = 0;
}

static void
dest_demangle_data(struct demangle_data *d)
{

	if (d != NULL) {
		vector_str_dest(&d->arg);
		vector_str_dest(&d->vec);

		dest_cstring(&d->array_str);
	}
}

static bool
init_cstring(struct cstring *s, size_t len)
{

	if (s == NULL || len <= 1)
		return (false);

	if ((s->buf = malloc(sizeof(char) * len)) == NULL)
		return (false);

	s->size = len - 1;

	return (true);
}

static bool
init_demangle_data(struct demangle_data *d)
{

	if (d == NULL)
		return (false);

	d->ptr = false;
	d->ref = false;
	d->cnst = false;
	d->array = false;

	d->array_str.buf = NULL;
	d->array_str.size = 0;

	d->type = ENCODE_FUNC;

	if (vector_str_init(&d->vec) == false)
		return (false);

	if (vector_str_init(&d->arg) == false) {
		vector_str_dest(&d->vec);

		return (false);
	}

	return (true);
}

static bool
push_CTDT(const char *s, size_t l, struct vector_str *v)
{

	if (s == NULL || l == 0 || v == NULL)
		return (false);

	if (vector_str_push(v, s, l) == false)
		return (false);

	assert(v->size > 1);
	if (VEC_PUSH_STR(v, v->container[v->size - 2]) == false)
		return (false);

	if (VEC_PUSH_STR(v, "()") == false)
		return (false);

	return (true);
}

static bool
read_array(struct demangle_data *d)
{
	size_t len;
	const char *end;

	if (d == NULL || d->p == NULL)
		return (false);

	end = d->p;
	assert(end != NULL);

	for (;;) {
		if (*end == '\0')
			return (false);

		if (ELFTC_ISDIGIT(*end) == 0)
			break;

		++end;
	}

	if (*end != '_')
		return (false);

	len = end - d->p;
	assert(len > 0);

	dest_cstring(&d->array_str);
	if (init_cstring(&d->array_str, len + 3) == false)
		return (false);

	strncpy(d->array_str.buf + 1, d->p, len);
	*d->array_str.buf = '[';
	*(d->array_str.buf + len + 1) = ']';

	d->array = true;
	d->p = end + 1;

	return (true);
}

static bool
read_class(struct demangle_data *d)
{
	size_t len;
	char *str;

	if (d == NULL)
		return (false);

	len = strtol(d->p, &str, 10);
	if (len == 0 && (errno == EINVAL || errno == ERANGE))
		return (false);

	assert(len > 0);
	assert(str != NULL);

	if (vector_str_push(&d->vec, str, len) == false)
		return (false);

	d->p = str + len;

	return (true);
}

static bool
read_func(struct demangle_data *d)
{
	size_t len;
	const char *name;
	char *delim;

	if (d == NULL)
		return (false);

	assert(d->p != NULL && "d->p (org str) is NULL");
	if ((delim = strstr(d->p, "__")) == NULL)
		return (false);

	len = delim - d->p;
	assert(len != 0);

	name = d->p;

	d->p = delim + 2;

	if (*d->p == 'Q' && ELFTC_ISDIGIT(*(d->p + 1))) {
		++d->p;

		if (read_qual_name(d) == false)
			return (false);
	} else if (ELFTC_ISDIGIT(*d->p)) {
		if (read_class(d) == false)
			return (false);

		if (VEC_PUSH_STR(&d->vec, "::") == false)
			return (false);
	}

	if (vector_str_push(&d->vec, name, len) == false)
		return (false);

	return (true);
}

static bool
read_func_name(struct demangle_data *d)
{
	size_t len;
	bool rtn;
	char *op_name;

	if (d == NULL)
		return (false);

	rtn = false;
	op_name = NULL;

	assert(d->p != NULL && "d->p (org str) is NULL");

	if (*d->p == '_' && *(d->p + 1) == '_') {
		d->p += 2;

		d->type = ENCODE_OP;
		if (read_op(d) == false)
			return (false);

		if (d->type == ENCODE_OP_CT || d->type == ENCODE_OP_DT ||
		    d->type == ENCODE_OP_USER)
			return (true);

		/* skip "__" */
		d->p += 2;

		/* assume delimiter is removed */
		if (*d->p == 'Q' && ELFTC_ISDIGIT(*(d->p + 1))) {
			++d->p;

			assert(d->vec.size > 0);

			len = strlen(d->vec.container[d->vec.size - 1]);
			if ((op_name = malloc(sizeof(char) * (len + 1)))
			    == NULL)
				return (false);

			snprintf(op_name, len + 1, "%s",
			    d->vec.container[d->vec.size - 1]);
			vector_str_pop(&d->vec);

			if (read_qual_name(d) == false)
				goto clean;

			if (VEC_PUSH_STR(&d->vec, "::") == false)
				goto clean;

			if (vector_str_push(&d->vec, op_name, len) == false)
				goto clean;

			rtn = true;
		} else if (ELFTC_ISDIGIT(*d->p)) {
			assert(d->vec.size > 0);

			len = strlen(d->vec.container[d->vec.size - 1]);
			if ((op_name = malloc(sizeof(char) * (len + 1)))
			    == NULL)
				return (false);

			snprintf(op_name, len + 1, "%s",
			    d->vec.container[d->vec.size - 1]);
			vector_str_pop(&d->vec);

			if (read_class(d) == false)
				goto clean;

			if (VEC_PUSH_STR(&d->vec, "::") == false)
				goto clean;

			if (vector_str_push(&d->vec, op_name, len) == false)
				goto clean;

			rtn = true;
		}
	} else
		return (read_func(d));

clean:
	free(op_name);

	return (rtn);
}

/* Read function ptr type */
static bool
read_func_ptr(struct demangle_data *d)
{
	struct demangle_data fptr;
	size_t arg_len, rtn_len;
	char *arg_type, *rtn_type;
	int lim;

	if (d == NULL)
		return (false);

	if (init_demangle_data(&fptr) == false)
		return (false);

	fptr.p = d->p + 1;
	lim = 0;
	arg_type = NULL;
	rtn_type = NULL;

	for (;;) {
		if (read_type(&fptr) == false) {
			dest_demangle_data(&fptr);

			return (false);
		}

		if (fptr.ptr == true) {
			if (VEC_PUSH_STR(&fptr.vec, "*") == false) {
				dest_demangle_data(&fptr);

				return (false);
			}

			fptr.ptr = false;
		}

		if (fptr.ref == true) {
			if (VEC_PUSH_STR(&fptr.vec, "&") == false) {
				dest_demangle_data(&fptr);

				return (false);
			}

			fptr.ref = false;
		}

		if (fptr.cnst == true) {
			if (VEC_PUSH_STR(&fptr.vec, " const") == false) {
				dest_demangle_data(&fptr);

				return (false);
			}

			fptr.cnst = false;
		}

		if (*fptr.p == '_')
			break;

		if (VEC_PUSH_STR(&fptr.vec, ", ") == false) {
			dest_demangle_data(&fptr);

			return (false);
		}

		if (++lim > CPP_DEMANGLE_ARM_TRY) {

			dest_demangle_data(&fptr);

			return (false);
		}
	}

	arg_type = vector_str_get_flat(&fptr.vec, &arg_len);
	/* skip '_' */
	d->p = fptr.p + 1;

	dest_demangle_data(&fptr);

	if (init_demangle_data(&fptr) == false) {
		free(arg_type);

		return (false);
	}

	fptr.p = d->p;
	lim = 0;

	if (read_type(&fptr) == false) {
		free(arg_type);
		dest_demangle_data(&fptr);

		return (false);
	}

	rtn_type = vector_str_get_flat(&fptr.vec, &rtn_len);
	d->p = fptr.p;


	dest_demangle_data(&fptr);

	if (vector_str_push(&d->vec, rtn_type, rtn_len) == false) {
		free(rtn_type);
		free(arg_type);

		return (false);
	}

	free(rtn_type);

	if (VEC_PUSH_STR(&d->vec, " (*)(") == false) {
		free(arg_type);

		return (false);
	}

	if (vector_str_push(&d->vec, arg_type, arg_len) == false) {
		free(arg_type);

		return (false);
	}

	free(arg_type);

	return (VEC_PUSH_STR(&d->vec, ")"));
}

static bool
read_memptr(struct demangle_data *d)
{
	struct demangle_data mptr;
	size_t len;
	bool rtn;
	char *mptr_str;

	if (d == NULL || d->p == NULL)
		return (false);

	if (init_demangle_data(&mptr) == false)
		return (false);

	rtn = false;
	mptr_str = NULL;

	mptr.p = d->p;
	if (*mptr.p == 'Q') {
		++mptr.p;

		if (read_qual_name(&mptr) == false)
			goto clean;
	} else {
		if (read_class(&mptr) == false)
			goto clean;
	}

	d->p = mptr.p;

	if ((mptr_str = vector_str_get_flat(&mptr.vec, &len)) == NULL)
		goto clean;

	if (vector_str_push(&d->vec, mptr_str, len) == false)
		goto clean;

	if (VEC_PUSH_STR(&d->vec, "::*") == false)
		goto clean;

	rtn = true;
clean:
	free(mptr_str);
	dest_demangle_data(&mptr);

	return (rtn);
}

static bool
read_op(struct demangle_data *d)
{

	if (d == NULL)
		return (false);

	assert(d->p != NULL && "d->p (org str) is NULL");

	switch (SIMPLE_HASH(*(d->p), *(d->p+1))) {
	case SIMPLE_HASH('m', 'l') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator*"));
	case SIMPLE_HASH('d', 'v') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator/"));
	case SIMPLE_HASH('m', 'd') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator%"));
	case SIMPLE_HASH('p', 'l') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator+"));
	case SIMPLE_HASH('m', 'i') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator-"));
	case SIMPLE_HASH('l', 's') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator<<"));
	case SIMPLE_HASH('r', 's') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator>>"));
	case SIMPLE_HASH('e', 'q') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator=="));
	case SIMPLE_HASH('n', 'e') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator!="));
	case SIMPLE_HASH('l', 't') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator<"));
	case SIMPLE_HASH('g', 't') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator>"));
	case SIMPLE_HASH('l', 'e') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator<="));
	case SIMPLE_HASH('g', 'e') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator>="));
	case SIMPLE_HASH('a', 'd') :
		d->p += 2;
		if (*d->p == 'v') {
			++d->p;
			return (VEC_PUSH_STR(&d->vec, "operator/="));
		} else
			return (VEC_PUSH_STR(&d->vec, "operator&"));
	case SIMPLE_HASH('o', 'r') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator|"));
	case SIMPLE_HASH('e', 'r') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator^"));
	case SIMPLE_HASH('a', 'a') :
		d->p += 2;
		if (*d->p == 'd') {
			++d->p;
			return (VEC_PUSH_STR(&d->vec, "operator&="));
		} else
			return (VEC_PUSH_STR(&d->vec, "operator&&"));
	case SIMPLE_HASH('o', 'o') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator||"));
	case SIMPLE_HASH('n', 't') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator!"));
	case SIMPLE_HASH('c', 'o') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator~"));
	case SIMPLE_HASH('p', 'p') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator++"));
	case SIMPLE_HASH('m', 'm') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator--"));
	case SIMPLE_HASH('a', 's') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator="));
	case SIMPLE_HASH('r', 'f') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator->"));
	case SIMPLE_HASH('a', 'p') :
		/* apl */
		if (*(d->p + 2) != 'l')
			return (false);

		d->p += 3;
		return (VEC_PUSH_STR(&d->vec, "operator+="));
	case SIMPLE_HASH('a', 'm') :
		d->p += 2;
		if (*d->p == 'i') {
			++d->p;
			return (VEC_PUSH_STR(&d->vec, "operator-="));
		} else if (*d->p == 'u') {
			++d->p;
			return (VEC_PUSH_STR(&d->vec, "operator*="));
		} else if (*d->p == 'd') {
			++d->p;
			return (VEC_PUSH_STR(&d->vec, "operator%="));
		}

		return (false);
	case SIMPLE_HASH('a', 'l') :
		/* als */
		if (*(d->p + 2) != 's')
			return (false);

		d->p += 3;
		return (VEC_PUSH_STR(&d->vec, "operator<<="));
	case SIMPLE_HASH('a', 'r') :
		/* ars */
		if (*(d->p + 2) != 's')
			return (false);

		d->p += 3;
		return (VEC_PUSH_STR(&d->vec, "operator>>="));
	case SIMPLE_HASH('a', 'o') :
		/* aor */
		if (*(d->p + 2) != 'r')
			return (false);

		d->p += 3;
		return (VEC_PUSH_STR(&d->vec, "operator|="));
	case SIMPLE_HASH('a', 'e') :
		/* aer */
		if (*(d->p + 2) != 'r')
			return (false);

		d->p += 3;
		return (VEC_PUSH_STR(&d->vec, "operator^="));
	case SIMPLE_HASH('c', 'm') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator,"));
	case SIMPLE_HASH('r', 'm') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator->*"));
	case SIMPLE_HASH('c', 'l') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "()"));
	case SIMPLE_HASH('v', 'c') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "[]"));
	case SIMPLE_HASH('c', 't') :
		d->p += 4;
		d->type = ENCODE_OP_CT;

		if (*d->p == 'Q' && ELFTC_ISDIGIT(*(d->p + 1))) {
			++d->p;

			return (read_qual_name(d));
		} else if (ELFTC_ISDIGIT(*d->p))
			return (read_class(d));

		return (false);
	case SIMPLE_HASH('d', 't') :
		d->p += 4;
		d->type = ENCODE_OP_DT;

		if (*d->p == 'Q' && ELFTC_ISDIGIT(*(d->p + 1))) {
			++d->p;

			return (read_qual_name(d));
		} else if (ELFTC_ISDIGIT(*d->p))
			return (read_class(d));

		return (false);
	case SIMPLE_HASH('n', 'w') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator new()"));
	case SIMPLE_HASH('d', 'l') :
		d->p += 2;
		return (VEC_PUSH_STR(&d->vec, "operator delete()"));
	case SIMPLE_HASH('o', 'p') :
		/* __op<TO_TYPE>__<FROM_TYPE> */
		d->p += 2;

		d->type = ENCODE_OP_USER;

		return (read_op_user(d));
	default :
		return (false);
	};
}

static bool
read_op_user(struct demangle_data *d)
{
	struct demangle_data from, to;
	size_t from_len, to_len;
	bool rtn;
	char *from_str, *to_str;

	if (d == NULL)
		return (false);

	if (init_demangle_data(&from) == false)
		return (false);

	rtn = false;
	from_str = NULL;
	to_str = NULL;
	if (init_demangle_data(&to) == false)
		goto clean;

	to.p = d->p;
	if (*to.p == 'Q') {
		++to.p;

		if (read_qual_name(&to) == false)
			goto clean;

		/* pop last '::' */
		if (vector_str_pop(&to.vec) == false)
			goto clean;
	} else {
		if (read_class(&to) == false)
			goto clean;

		/* skip '__' */
		to.p += 2;
	}

	if ((to_str = vector_str_get_flat(&to.vec, &to_len)) == NULL)
		goto clean;

	from.p = to.p;
	if (*from.p == 'Q') {
		++from.p;

		if (read_qual_name(&from) == false)
			goto clean;

		/* pop last '::' */
		if (vector_str_pop(&from.vec) == false)
			goto clean;
	} else {
		if (read_class(&from) == false)
			goto clean;
	}

	if ((from_str = vector_str_get_flat(&from.vec, &from_len)) == NULL)
		goto clean;

	if (vector_str_push(&d->vec, from_str, from_len) == false)
		goto clean;

	if (VEC_PUSH_STR(&d->vec, "::operator ") == false)
		return (false);

	if (vector_str_push(&d->vec, to_str, to_len) == false)
		goto clean;

	rtn = VEC_PUSH_STR(&d->vec, "()");
clean:
	free(to_str);
	free(from_str);
	dest_demangle_data(&to);
	dest_demangle_data(&from);

	return (rtn);
}

/* single digit + class names */
static bool
read_qual_name(struct demangle_data *d)
{
	int i;
	char num;

	if (d == NULL)
		return (false);

	assert(d->p != NULL && "d->p (org str) is NULL");
	assert(*d->p > 48 && *d->p < 58 && "*d->p not in ASCII numeric range");

	num = *d->p - 48;

	assert(num > 0);

	++d->p;
	for (i = 0; i < num ; ++i) {
		if (read_class(d) == false)
			return (false);

		if (VEC_PUSH_STR(&d->vec, "::") == false)
			return (false);
	}

	if (*d->p != '\0')
		d->p = d->p + 2;

	return (true);
}

/* Return -1 at fail, 0 at success, and 1 at end */
static int
read_subst(struct demangle_data *d)
{
	size_t idx;
	char *str;

	if (d == NULL)
		return (-1);

	idx = strtol(d->p + 1, &str, 10);
	if (idx == 0 && (errno == EINVAL || errno == ERANGE))
		return (-1);

	assert(idx > 0);
	assert(str != NULL);

	d->p = str;

	if (VEC_PUSH_STR(&d->vec, d->arg.container[idx - 1]) == false)
		return (-1);

	if (VEC_PUSH_STR(&d->arg, d->arg.container[idx - 1]) == false)
		return (-1);

	if (*d->p == '\0')
		return (1);

	return (0);
}

static int
read_subst_iter(struct demangle_data *d)
{
	int i;
	size_t idx;
	char repeat;
	char *str;

	if (d == NULL)
		return (-1);

	++d->p;
	assert(*d->p > 48 && *d->p < 58 && "*d->p not in ASCII numeric range");

	repeat = *d->p - 48;

	assert(repeat > 1);

	++d->p;

	idx = strtol(d->p, &str, 10);
	if (idx == 0 && (errno == EINVAL || errno == ERANGE))
		return (-1);

	assert(idx > 0);
	assert(str != NULL);

	d->p = str;

	for (i = 0; i < repeat ; ++i) {
		if (VEC_PUSH_STR(&d->vec, d->arg.container[idx - 1]) == false)
			return (-1);

		if (VEC_PUSH_STR(&d->arg, d->arg.container[idx - 1]) == false)
			return (-1);

		if (i != repeat - 1 &&
		    VEC_PUSH_STR(&d->vec, ", ") == false)
			return (-1);
	}

	if (*d->p == '\0')
		return (1);

	return (0);
}

static bool
read_type(struct demangle_data *d)
{

	if (d == NULL)
		return (false);

	assert(d->p != NULL && "d->p (org str) is NULL");

	while (*d->p == 'U' || *d->p == 'C' || *d->p == 'V' || *d->p == 'S' ||
	       *d->p == 'P' || *d->p == 'R' || *d->p == 'A' || *d->p == 'F' ||
	       *d->p == 'M') {
		switch (*d->p) {
		case 'U' :
			++d->p;

			if (VEC_PUSH_STR(&d->vec, "unsigned ") == false)
				return (false);

			break;
		case 'C' :
			++d->p;

			if (*d->p == 'P')
				d->cnst = true;
			else {
				if (VEC_PUSH_STR(&d->vec, "const ") ==
				    false)
					return (false);
			}

			break;
		case 'V' :
			++d->p;

			if (VEC_PUSH_STR(&d->vec, "volatile ") == false)
				return (false);

			break;
		case 'S' :
			++d->p;

			if (VEC_PUSH_STR(&d->vec, "signed ") == false)
				return (false);

			break;
		case 'P' :
			++d->p;

			if (*d->p == 'F')
				return (read_func_ptr(d));
			else
				d->ptr = true;

			break;
		case 'R' :
			++d->p;

			d->ref = true;

			break;
		case 'F' :
			break;
		case 'A' :
			++d->p;

			if (read_array(d) == false)
				return (false);

			break;
		case 'M' :
			++d->p;

			if (read_memptr(d) == false)
				return (false);

			break;
		default :
			break;
		}
	}

	if (ELFTC_ISDIGIT(*d->p))
		return (read_class(d));

	switch (*d->p) {
	case 'Q' :
		++d->p;

		return (read_qual_name(d));
	case 'v' :
		++d->p;

		return (VEC_PUSH_STR(&d->vec, "void"));
	case 'c' :
		++d->p;

		return (VEC_PUSH_STR(&d->vec, "char"));
	case 's' :
		++d->p;

		return (VEC_PUSH_STR(&d->vec, "short"));
	case 'i' :
		++d->p;

		return (VEC_PUSH_STR(&d->vec, "int"));
	case 'l' :
		++d->p;

		return (VEC_PUSH_STR(&d->vec, "long"));
	case 'f' :
		++d->p;

		return (VEC_PUSH_STR(&d->vec, "float"));
	case 'd':
		++d->p;

		return (VEC_PUSH_STR(&d->vec, "double"));
	case 'r':
		++d->p;

		return (VEC_PUSH_STR(&d->vec, "long double"));
	case 'e':
		++d->p;

		return (VEC_PUSH_STR(&d->vec, "..."));
	default:
		return (false);
	};

	/* NOTREACHED */
	return (false);
}
