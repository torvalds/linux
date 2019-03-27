/*-
 * Copyright (c) 2007, 2008 Hyogeol Lee <hyogeollee@gmail.com>
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
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @file cpp_demangle.c
 * @brief Decode IA-64 C++ ABI style implementation.
 *
 * IA-64 standard ABI(Itanium C++ ABI) references.
 *
 * http://www.codesourcery.com/cxx-abi/abi.html#mangling \n
 * http://www.codesourcery.com/cxx-abi/abi-mangling.html
 */

/** @brief Dynamic vector data for string. */
struct vector_str {
	/** Current size */
	size_t		size;
	/** Total capacity */
	size_t		capacity;
	/** String array */
	char		**container;
};

#define BUFFER_GROWFACTOR	1.618
#define VECTOR_DEF_CAPACITY	8
#define	ELFTC_ISDIGIT(C) 	(isdigit((C) & 0xFF))

enum type_qualifier {
	TYPE_PTR, TYPE_REF, TYPE_CMX, TYPE_IMG, TYPE_EXT, TYPE_RST, TYPE_VAT,
	TYPE_CST, TYPE_VEC
};

struct vector_type_qualifier {
	size_t size, capacity;
	enum type_qualifier *q_container;
	struct vector_str ext_name;
};

enum read_cmd {
	READ_FAIL, READ_NEST, READ_TMPL, READ_EXPR, READ_EXPL, READ_LOCAL,
	READ_TYPE, READ_FUNC, READ_PTRMEM
};

struct vector_read_cmd {
	size_t size, capacity;
	enum read_cmd *r_container;
};

struct cpp_demangle_data {
	struct vector_str	 output;	/* output string vector */
	struct vector_str	 output_tmp;
	struct vector_str	 subst;		/* substitution string vector */
	struct vector_str	 tmpl;
	struct vector_str	 class_type;
	struct vector_read_cmd	 cmd;
	bool			 paren;		/* parenthesis opened */
	bool			 pfirst;	/* first element of parameter */
	bool			 mem_rst;	/* restrict member function */
	bool			 mem_vat;	/* volatile member function */
	bool			 mem_cst;	/* const member function */
	int			 func_type;
	const char		*cur;		/* current mangled name ptr */
	const char		*last_sname;	/* last source name */
	int			 push_head;
};

#define	CPP_DEMANGLE_TRY_LIMIT	128
#define	FLOAT_SPRINTF_TRY_LIMIT	5
#define	FLOAT_QUADRUPLE_BYTES	16
#define	FLOAT_EXTENED_BYTES	10

#define SIMPLE_HASH(x,y)	(64 * x + y)

static size_t	get_strlen_sum(const struct vector_str *v);
static bool	vector_str_grow(struct vector_str *v);

static size_t
get_strlen_sum(const struct vector_str *v)
{
	size_t i, len = 0;

	if (v == NULL)
		return (0);

	assert(v->size > 0);

	for (i = 0; i < v->size; ++i)
		len += strlen(v->container[i]);

	return (len);
}

/**
 * @brief Deallocate resource in vector_str.
 */
static void
vector_str_dest(struct vector_str *v)
{
	size_t i;

	if (v == NULL)
		return;

	for (i = 0; i < v->size; ++i)
		free(v->container[i]);

	free(v->container);
}

/**
 * @brief Find string in vector_str.
 * @param v Destination vector.
 * @param o String to find.
 * @param l Length of the string.
 * @return -1 at failed, 0 at not found, 1 at found.
 */
static int
vector_str_find(const struct vector_str *v, const char *o, size_t l)
{
	size_t i;

	if (v == NULL || o == NULL)
		return (-1);

	for (i = 0; i < v->size; ++i)
		if (strncmp(v->container[i], o, l) == 0)
			return (1);

	return (0);
}

/**
 * @brief Get new allocated flat string from vector.
 *
 * If l is not NULL, return length of the string.
 * @param v Destination vector.
 * @param l Length of the string.
 * @return NULL at failed or NUL terminated new allocated string.
 */
static char *
vector_str_get_flat(const struct vector_str *v, size_t *l)
{
	ssize_t elem_pos, elem_size, rtn_size;
	size_t i;
	char *rtn;

	if (v == NULL || v->size == 0)
		return (NULL);

	if ((rtn_size = get_strlen_sum(v)) == 0)
		return (NULL);

	if ((rtn = malloc(sizeof(char) * (rtn_size + 1))) == NULL)
		return (NULL);

	elem_pos = 0;
	for (i = 0; i < v->size; ++i) {
		elem_size = strlen(v->container[i]);

		memcpy(rtn + elem_pos, v->container[i], elem_size);

		elem_pos += elem_size;
	}

	rtn[rtn_size] = '\0';

	if (l != NULL)
		*l = rtn_size;

	return (rtn);
}

static bool
vector_str_grow(struct vector_str *v)
{
	size_t i, tmp_cap;
	char **tmp_ctn;

	if (v == NULL)
		return (false);

	assert(v->capacity > 0);

	tmp_cap = v->capacity * BUFFER_GROWFACTOR;

	assert(tmp_cap > v->capacity);

	if ((tmp_ctn = malloc(sizeof(char *) * tmp_cap)) == NULL)
		return (false);

	for (i = 0; i < v->size; ++i)
		tmp_ctn[i] = v->container[i];

	free(v->container);

	v->container = tmp_ctn;
	v->capacity = tmp_cap;

	return (true);
}

/**
 * @brief Initialize vector_str.
 * @return false at failed, true at success.
 */
static bool
vector_str_init(struct vector_str *v)
{

	if (v == NULL)
		return (false);

	v->size = 0;
	v->capacity = VECTOR_DEF_CAPACITY;

	assert(v->capacity > 0);

	if ((v->container = malloc(sizeof(char *) * v->capacity)) == NULL)
		return (false);

	assert(v->container != NULL);

	return (true);
}

/**
 * @brief Remove last element in vector_str.
 * @return false at failed, true at success.
 */
static bool
vector_str_pop(struct vector_str *v)
{

	if (v == NULL)
		return (false);

	if (v->size == 0)
		return (true);

	--v->size;

	free(v->container[v->size]);
	v->container[v->size] = NULL;

	return (true);
}

/**
 * @brief Push back string to vector.
 * @return false at failed, true at success.
 */
static bool
vector_str_push(struct vector_str *v, const char *str, size_t len)
{

	if (v == NULL || str == NULL)
		return (false);

	if (v->size == v->capacity && vector_str_grow(v) == false)
		return (false);

	if ((v->container[v->size] = malloc(sizeof(char) * (len + 1))) == NULL)
		return (false);

	snprintf(v->container[v->size], len + 1, "%s", str);

	++v->size;

	return (true);
}

/**
 * @brief Push front org vector to det vector.
 * @return false at failed, true at success.
 */
static bool
vector_str_push_vector_head(struct vector_str *dst, struct vector_str *org)
{
	size_t i, j, tmp_cap;
	char **tmp_ctn;

	if (dst == NULL || org == NULL)
		return (false);

	tmp_cap = (dst->size + org->size) * BUFFER_GROWFACTOR;

	if ((tmp_ctn = malloc(sizeof(char *) * tmp_cap)) == NULL)
		return (false);

	for (i = 0; i < org->size; ++i)
		if ((tmp_ctn[i] = strdup(org->container[i])) == NULL) {
			for (j = 0; j < i; ++j)
				free(tmp_ctn[j]);

			free(tmp_ctn);

			return (false);
		}

	for (i = 0; i < dst->size; ++i)
		tmp_ctn[i + org->size] = dst->container[i];

	free(dst->container);

	dst->container = tmp_ctn;
	dst->capacity = tmp_cap;
	dst->size += org->size;

	return (true);
}

/**
 * @brief Get new allocated flat string from vector between begin and end.
 *
 * If r_len is not NULL, string length will be returned.
 * @return NULL at failed or NUL terminated new allocated string.
 */
static char *
vector_str_substr(const struct vector_str *v, size_t begin, size_t end,
    size_t *r_len)
{
	size_t cur, i, len;
	char *rtn;

	if (v == NULL || begin > end)
		return (NULL);

	len = 0;
	for (i = begin; i < end + 1; ++i)
		len += strlen(v->container[i]);

	if ((rtn = malloc(sizeof(char) * (len + 1))) == NULL)
		return (NULL);

	if (r_len != NULL)
		*r_len = len;

	cur = 0;
	for (i = begin; i < end + 1; ++i) {
		len = strlen(v->container[i]);
		memcpy(rtn + cur, v->container[i], len);
		cur += len;
	}
	rtn[cur] = '\0';

	return (rtn);
}

static void	cpp_demangle_data_dest(struct cpp_demangle_data *);
static int	cpp_demangle_data_init(struct cpp_demangle_data *,
		    const char *);
static int	cpp_demangle_get_subst(struct cpp_demangle_data *, size_t);
static int	cpp_demangle_get_tmpl_param(struct cpp_demangle_data *, size_t);
static int	cpp_demangle_push_fp(struct cpp_demangle_data *,
		    char *(*)(const char *, size_t));
static int	cpp_demangle_push_str(struct cpp_demangle_data *, const char *,
		    size_t);
static int	cpp_demangle_push_subst(struct cpp_demangle_data *,
		    const char *, size_t);
static int	cpp_demangle_push_subst_v(struct cpp_demangle_data *,
		    struct vector_str *);
static int	cpp_demangle_push_type_qualifier(struct cpp_demangle_data *,
		    struct vector_type_qualifier *, const char *);
static int	cpp_demangle_read_array(struct cpp_demangle_data *);
static int	cpp_demangle_read_encoding(struct cpp_demangle_data *);
static int	cpp_demangle_read_expr_primary(struct cpp_demangle_data *);
static int	cpp_demangle_read_expression(struct cpp_demangle_data *);
static int	cpp_demangle_read_expression_flat(struct cpp_demangle_data *,
		    char **);
static int	cpp_demangle_read_expression_binary(struct cpp_demangle_data *,
		    const char *, size_t);
static int	cpp_demangle_read_expression_unary(struct cpp_demangle_data *,
		    const char *, size_t);
static int	cpp_demangle_read_expression_trinary(struct cpp_demangle_data *,
		    const char *, size_t, const char *, size_t);
static int	cpp_demangle_read_function(struct cpp_demangle_data *, int *,
		    struct vector_type_qualifier *);
static int	cpp_demangle_local_source_name(struct cpp_demangle_data *ddata);
static int	cpp_demangle_read_local_name(struct cpp_demangle_data *);
static int	cpp_demangle_read_name(struct cpp_demangle_data *);
static int	cpp_demangle_read_name_flat(struct cpp_demangle_data *,
		    char**);
static int	cpp_demangle_read_nested_name(struct cpp_demangle_data *);
static int	cpp_demangle_read_number(struct cpp_demangle_data *, long *);
static int	cpp_demangle_read_number_as_string(struct cpp_demangle_data *,
		    char **);
static int	cpp_demangle_read_nv_offset(struct cpp_demangle_data *);
static int	cpp_demangle_read_offset(struct cpp_demangle_data *);
static int	cpp_demangle_read_offset_number(struct cpp_demangle_data *);
static int	cpp_demangle_read_pointer_to_member(struct cpp_demangle_data *);
static int	cpp_demangle_read_sname(struct cpp_demangle_data *);
static int	cpp_demangle_read_subst(struct cpp_demangle_data *);
static int	cpp_demangle_read_subst_std(struct cpp_demangle_data *);
static int	cpp_demangle_read_subst_stdtmpl(struct cpp_demangle_data *,
		    const char *, size_t);
static int	cpp_demangle_read_tmpl_arg(struct cpp_demangle_data *);
static int	cpp_demangle_read_tmpl_args(struct cpp_demangle_data *);
static int	cpp_demangle_read_tmpl_param(struct cpp_demangle_data *);
static int	cpp_demangle_read_type(struct cpp_demangle_data *, int);
static int	cpp_demangle_read_type_flat(struct cpp_demangle_data *,
		    char **);
static int	cpp_demangle_read_uqname(struct cpp_demangle_data *);
static int	cpp_demangle_read_v_offset(struct cpp_demangle_data *);
static char	*decode_fp_to_double(const char *, size_t);
static char	*decode_fp_to_float(const char *, size_t);
static char	*decode_fp_to_float128(const char *, size_t);
static char	*decode_fp_to_float80(const char *, size_t);
static char	*decode_fp_to_long_double(const char *, size_t);
static int	hex_to_dec(char);
static void	vector_read_cmd_dest(struct vector_read_cmd *);
static int	vector_read_cmd_find(struct vector_read_cmd *, enum read_cmd);
static int	vector_read_cmd_init(struct vector_read_cmd *);
static int	vector_read_cmd_pop(struct vector_read_cmd *);
static int	vector_read_cmd_push(struct vector_read_cmd *, enum read_cmd);
static void	vector_type_qualifier_dest(struct vector_type_qualifier *);
static int	vector_type_qualifier_init(struct vector_type_qualifier *);
static int	vector_type_qualifier_push(struct vector_type_qualifier *,
		    enum type_qualifier);

/**
 * @brief Decode the input string by IA-64 C++ ABI style.
 *
 * GNU GCC v3 use IA-64 standard ABI.
 * @return New allocated demangled string or NULL if failed.
 * @todo 1. Testing and more test case. 2. Code cleaning.
 */
char *
__cxa_demangle_gnu3(const char *org)
{
	struct cpp_demangle_data ddata;
	ssize_t org_len;
	unsigned int limit;
	char *rtn = NULL;

	if (org == NULL)
		return (NULL);

	org_len = strlen(org);
	if (org_len > 11 && !strncmp(org, "_GLOBAL__I_", 11)) {
		if ((rtn = malloc(org_len + 19)) == NULL)
			return (NULL);
		snprintf(rtn, org_len + 19,
		    "global constructors keyed to %s", org + 11);
		return (rtn);
	}

	// Try demangling as a type for short encodings
	if ((org_len < 2) || (org[0] != '_' || org[1] != 'Z' )) {
		if (!cpp_demangle_data_init(&ddata, org))
			return (NULL);
		if (!cpp_demangle_read_type(&ddata, 0))
			goto clean;
		rtn = vector_str_get_flat(&ddata.output, (size_t *) NULL);
		goto clean;
	}


	if (!cpp_demangle_data_init(&ddata, org + 2))
		return (NULL);

	rtn = NULL;

	if (!cpp_demangle_read_encoding(&ddata))
		goto clean;

	limit = 0;
	while (*ddata.cur != '\0') {
		/*
		 * Breaking at some gcc info at tail. e.g) @@GLIBCXX_3.4
		 */
		if (*ddata.cur == '@' && *(ddata.cur + 1) == '@')
			break;
		if (!cpp_demangle_read_type(&ddata, 1))
			goto clean;
		if (limit++ > CPP_DEMANGLE_TRY_LIMIT)
			goto clean;
	}

	if (ddata.output.size == 0)
		goto clean;
	if (ddata.paren && !vector_str_push(&ddata.output, ")", 1))
		goto clean;
	if (ddata.mem_vat && !vector_str_push(&ddata.output, " volatile", 9))
		goto clean;
	if (ddata.mem_cst && !vector_str_push(&ddata.output, " const", 6))
		goto clean;
	if (ddata.mem_rst && !vector_str_push(&ddata.output, " restrict", 9))
		goto clean;

	rtn = vector_str_get_flat(&ddata.output, (size_t *) NULL);

clean:
	cpp_demangle_data_dest(&ddata);

	return (rtn);
}

static void
cpp_demangle_data_dest(struct cpp_demangle_data *d)
{

	if (d == NULL)
		return;

	vector_read_cmd_dest(&d->cmd);
	vector_str_dest(&d->class_type);
	vector_str_dest(&d->tmpl);
	vector_str_dest(&d->subst);
	vector_str_dest(&d->output_tmp);
	vector_str_dest(&d->output);
}

static int
cpp_demangle_data_init(struct cpp_demangle_data *d, const char *cur)
{

	if (d == NULL || cur == NULL)
		return (0);

	if (!vector_str_init(&d->output))
		return (0);
	if (!vector_str_init(&d->output_tmp))
		goto clean1;
	if (!vector_str_init(&d->subst))
		goto clean2;
	if (!vector_str_init(&d->tmpl))
		goto clean3;
	if (!vector_str_init(&d->class_type))
		goto clean4;
	if (!vector_read_cmd_init(&d->cmd))
		goto clean5;

	assert(d->output.container != NULL);
	assert(d->output_tmp.container != NULL);
	assert(d->subst.container != NULL);
	assert(d->tmpl.container != NULL);
	assert(d->class_type.container != NULL);

	d->paren = false;
	d->pfirst = false;
	d->mem_rst = false;
	d->mem_vat = false;
	d->mem_cst = false;
	d->func_type = 0;
	d->cur = cur;
	d->last_sname = NULL;
	d->push_head = 0;

	return (1);

clean5:
	vector_str_dest(&d->class_type);
clean4:
	vector_str_dest(&d->tmpl);
clean3:
	vector_str_dest(&d->subst);
clean2:
	vector_str_dest(&d->output_tmp);
clean1:
	vector_str_dest(&d->output);

	return (0);
}

static int
cpp_demangle_push_fp(struct cpp_demangle_data *ddata,
    char *(*decoder)(const char *, size_t))
{
	size_t len;
	int rtn;
	const char *fp;
	char *f;

	if (ddata == NULL || decoder == NULL)
		return (0);

	fp = ddata->cur;
	while (*ddata->cur != 'E')
		++ddata->cur;

	if ((f = decoder(fp, ddata->cur - fp)) == NULL)
		return (0);

	rtn = 0;
	if ((len = strlen(f)) > 0)
		rtn = cpp_demangle_push_str(ddata, f, len);

	free(f);

	++ddata->cur;

	return (rtn);
}

static int
cpp_demangle_push_str(struct cpp_demangle_data *ddata, const char *str,
    size_t len)
{

	if (ddata == NULL || str == NULL || len == 0)
		return (0);

	if (ddata->push_head > 0)
		return (vector_str_push(&ddata->output_tmp, str, len));

	return (vector_str_push(&ddata->output, str, len));
}

static int
cpp_demangle_push_subst(struct cpp_demangle_data *ddata, const char *str,
    size_t len)
{

	if (ddata == NULL || str == NULL || len == 0)
		return (0);

	if (!vector_str_find(&ddata->subst, str, len))
		return (vector_str_push(&ddata->subst, str, len));

	return (1);
}

static int
cpp_demangle_push_subst_v(struct cpp_demangle_data *ddata, struct vector_str *v)
{
	size_t str_len;
	int rtn;
	char *str;

	if (ddata == NULL || v == NULL)
		return (0);

	if ((str = vector_str_get_flat(v, &str_len)) == NULL)
		return (0);

	rtn = cpp_demangle_push_subst(ddata, str, str_len);

	free(str);

	return (rtn);
}

static int
cpp_demangle_push_type_qualifier(struct cpp_demangle_data *ddata,
    struct vector_type_qualifier *v, const char *type_str)
{
	struct vector_str subst_v;
	size_t idx, e_idx, e_len;
	int rtn;
	char *buf;

	if (ddata == NULL || v == NULL)
		return (0);

	if ((idx = v->size) == 0)
		return (1);

	rtn = 0;
	if (type_str != NULL) {
		if (!vector_str_init(&subst_v))
			return (0);
		if (!vector_str_push(&subst_v, type_str, strlen(type_str)))
			goto clean;
	}

	e_idx = 0;
	while (idx > 0) {
		switch (v->q_container[idx - 1]) {
		case TYPE_PTR:
			if (!cpp_demangle_push_str(ddata, "*", 1))
				goto clean;
			if (type_str != NULL) {
				if (!vector_str_push(&subst_v, "*", 1))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_REF:
			if (!cpp_demangle_push_str(ddata, "&", 1))
				goto clean;
			if (type_str != NULL) {
				if (!vector_str_push(&subst_v, "&", 1))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_CMX:
			if (!cpp_demangle_push_str(ddata, " complex", 8))
				goto clean;
			if (type_str != NULL) {
				if (!vector_str_push(&subst_v, " complex", 8))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_IMG:
			if (!cpp_demangle_push_str(ddata, " imaginary", 10))
				goto clean;
			if (type_str != NULL) {
				if (!vector_str_push(&subst_v, " imaginary",
				    10))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_EXT:
			if (v->ext_name.size == 0 ||
			    e_idx > v->ext_name.size - 1)
				goto clean;
			if ((e_len = strlen(v->ext_name.container[e_idx])) ==
			    0)
				goto clean;
			if ((buf = malloc(e_len + 2)) == NULL)
				goto clean;
			snprintf(buf, e_len + 2, " %s",
			    v->ext_name.container[e_idx]);

			if (!cpp_demangle_push_str(ddata, buf, e_len + 1)) {
				free(buf);
				goto clean;
			}

			if (type_str != NULL) {
				if (!vector_str_push(&subst_v, buf,
				    e_len + 1)) {
					free(buf);
					goto clean;
				}
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v)) {
					free(buf);
					goto clean;
				}
			}
			free(buf);
			++e_idx;
			break;

		case TYPE_RST:
			if (!cpp_demangle_push_str(ddata, " restrict", 9))
				goto clean;
			if (type_str != NULL) {
				if (!vector_str_push(&subst_v, " restrict", 9))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_VAT:
			if (!cpp_demangle_push_str(ddata, " volatile", 9))
				goto clean;
			if (type_str != NULL) {
				if (!vector_str_push(&subst_v, " volatile", 9))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_CST:
			if (!cpp_demangle_push_str(ddata, " const", 6))
				goto clean;
			if (type_str != NULL) {
				if (!vector_str_push(&subst_v, " const", 6))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_VEC:
			if (v->ext_name.size == 0 ||
			    e_idx > v->ext_name.size - 1)
				goto clean;
			if ((e_len = strlen(v->ext_name.container[e_idx])) ==
			    0)
				goto clean;
			if ((buf = malloc(e_len + 12)) == NULL)
				goto clean;
			snprintf(buf, e_len + 12, " __vector(%s)",
			    v->ext_name.container[e_idx]);
			if (!cpp_demangle_push_str(ddata, buf, e_len + 11)) {
				free(buf);
				goto clean;
			}
			if (type_str != NULL) {
				if (!vector_str_push(&subst_v, buf,
				    e_len + 11)) {
					free(buf);
					goto clean;
				}
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v)) {
					free(buf);
					goto clean;
				}
			}
			free(buf);
			++e_idx;
			break;
		}
		--idx;
	}

	rtn = 1;
clean:
	if (type_str != NULL)
		vector_str_dest(&subst_v);

	return (rtn);
}

static int
cpp_demangle_get_subst(struct cpp_demangle_data *ddata, size_t idx)
{
	size_t len;

	if (ddata == NULL || ddata->subst.size <= idx)
		return (0);
	if ((len = strlen(ddata->subst.container[idx])) == 0)
		return (0);
	if (!cpp_demangle_push_str(ddata, ddata->subst.container[idx], len))
		return (0);

	/* skip '_' */
	++ddata->cur;

	return (1);
}

static int
cpp_demangle_get_tmpl_param(struct cpp_demangle_data *ddata, size_t idx)
{
	size_t len;

	if (ddata == NULL || ddata->tmpl.size <= idx)
		return (0);
	if ((len = strlen(ddata->tmpl.container[idx])) == 0)
		return (0);
	if (!cpp_demangle_push_str(ddata, ddata->tmpl.container[idx], len))
		return (0);

	++ddata->cur;

	return (1);
}

static int
cpp_demangle_read_array(struct cpp_demangle_data *ddata)
{
	size_t i, num_len, exp_len, p_idx, idx;
	const char *num;
	char *exp;

	if (ddata == NULL || *(++ddata->cur) == '\0')
		return (0);

	if (*ddata->cur == '_') {
		if (*(++ddata->cur) == '\0')
			return (0);

		if (!cpp_demangle_read_type(ddata, 0))
			return (0);

		if (!cpp_demangle_push_str(ddata, "[]", 2))
			return (0);
	} else {
		if (ELFTC_ISDIGIT(*ddata->cur) != 0) {
			num = ddata->cur;
			while (ELFTC_ISDIGIT(*ddata->cur) != 0)
				++ddata->cur;
			if (*ddata->cur != '_')
				return (0);
			num_len = ddata->cur - num;
			assert(num_len > 0);
			if (*(++ddata->cur) == '\0')
				return (0);
			if (!cpp_demangle_read_type(ddata, 0))
				return (0);
			if (!cpp_demangle_push_str(ddata, "[", 1))
				return (0);
			if (!cpp_demangle_push_str(ddata, num, num_len))
				return (0);
			if (!cpp_demangle_push_str(ddata, "]", 1))
				return (0);
		} else {
			p_idx = ddata->output.size;
			if (!cpp_demangle_read_expression(ddata))
				return (0);
			if ((exp = vector_str_substr(&ddata->output, p_idx,
				 ddata->output.size - 1, &exp_len)) == NULL)
				return (0);
			idx = ddata->output.size;
			for (i = p_idx; i < idx; ++i)
				if (!vector_str_pop(&ddata->output)) {
					free(exp);
					return (0);
				}
			if (*ddata->cur != '_') {
				free(exp);
				return (0);
			}
			++ddata->cur;
			if (*ddata->cur == '\0') {
				free(exp);
				return (0);
			}
			if (!cpp_demangle_read_type(ddata, 0)) {
				free(exp);
				return (0);
			}
			if (!cpp_demangle_push_str(ddata, "[", 1)) {
				free(exp);
				return (0);
			}
			if (!cpp_demangle_push_str(ddata, exp, exp_len)) {
				free(exp);
				return (0);
			}
			if (!cpp_demangle_push_str(ddata, "]", 1)) {
				free(exp);
				return (0);
			}
			free(exp);
		}
	}

	return (1);
}

static int
cpp_demangle_read_expr_primary(struct cpp_demangle_data *ddata)
{
	const char *num;

	if (ddata == NULL || *(++ddata->cur) == '\0')
		return (0);

	if (*ddata->cur == '_' && *(ddata->cur + 1) == 'Z') {
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		if (!cpp_demangle_read_encoding(ddata))
			return (0);
		++ddata->cur;
		return (1);
	}

	switch (*ddata->cur) {
	case 'b':
		if (*(ddata->cur + 2) != 'E')
			return (0);
		switch (*(++ddata->cur)) {
		case '0':
			ddata->cur += 2;
			return (cpp_demangle_push_str(ddata, "false", 5));
		case '1':
			ddata->cur += 2;
			return (cpp_demangle_push_str(ddata, "true", 4));
		default:
			return (0);
		}

	case 'd':
		++ddata->cur;
		return (cpp_demangle_push_fp(ddata, decode_fp_to_double));

	case 'e':
		++ddata->cur;
		if (sizeof(long double) == 10)
			return (cpp_demangle_push_fp(ddata,
			    decode_fp_to_double));
		return (cpp_demangle_push_fp(ddata, decode_fp_to_float80));

	case 'f':
		++ddata->cur;
		return (cpp_demangle_push_fp(ddata, decode_fp_to_float));

	case 'g':
		++ddata->cur;
		if (sizeof(long double) == 16)
			return (cpp_demangle_push_fp(ddata,
			    decode_fp_to_double));
		return (cpp_demangle_push_fp(ddata, decode_fp_to_float128));

	case 'i':
	case 'j':
	case 'l':
	case 'm':
	case 'n':
	case 's':
	case 't':
	case 'x':
	case 'y':
		if (*(++ddata->cur) == 'n') {
			if (!cpp_demangle_push_str(ddata, "-", 1))
				return (0);
			++ddata->cur;
		}
		num = ddata->cur;
		while (*ddata->cur != 'E') {
			if (!ELFTC_ISDIGIT(*ddata->cur))
				return (0);
			++ddata->cur;
		}
		++ddata->cur;
		return (cpp_demangle_push_str(ddata, num,
		    ddata->cur - num - 1));

	default:
		return (0);
	}
}

static int
cpp_demangle_read_expression(struct cpp_demangle_data *ddata)
{

	if (ddata == NULL || *ddata->cur == '\0')
		return (0);

	switch (SIMPLE_HASH(*ddata->cur, *(ddata->cur + 1))) {
	case SIMPLE_HASH('s', 't'):
		ddata->cur += 2;
		return (cpp_demangle_read_type(ddata, 0));

	case SIMPLE_HASH('s', 'r'):
		ddata->cur += 2;
		if (!cpp_demangle_read_type(ddata, 0))
			return (0);
		if (!cpp_demangle_read_uqname(ddata))
			return (0);
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_tmpl_args(ddata));
		return (1);

	case SIMPLE_HASH('a', 'a'):
		/* operator && */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "&&", 2));

	case SIMPLE_HASH('a', 'd'):
		/* operator & (unary) */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_unary(ddata, "&", 1));

	case SIMPLE_HASH('a', 'n'):
		/* operator & */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "&", 1));

	case SIMPLE_HASH('a', 'N'):
		/* operator &= */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "&=", 2));

	case SIMPLE_HASH('a', 'S'):
		/* operator = */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "=", 1));

	case SIMPLE_HASH('c', 'l'):
		/* operator () */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "()", 2));

	case SIMPLE_HASH('c', 'm'):
		/* operator , */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, ",", 1));

	case SIMPLE_HASH('c', 'o'):
		/* operator ~ */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "~", 1));

	case SIMPLE_HASH('c', 'v'):
		/* operator (cast) */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "(cast)", 6));

	case SIMPLE_HASH('d', 'a'):
		/* operator delete [] */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_unary(ddata, "delete []", 9));

	case SIMPLE_HASH('d', 'e'):
		/* operator * (unary) */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_unary(ddata, "*", 1));

	case SIMPLE_HASH('d', 'l'):
		/* operator delete */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_unary(ddata, "delete", 6));

	case SIMPLE_HASH('d', 'v'):
		/* operator / */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "/", 1));

	case SIMPLE_HASH('d', 'V'):
		/* operator /= */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "/=", 2));

	case SIMPLE_HASH('e', 'o'):
		/* operator ^ */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "^", 1));

	case SIMPLE_HASH('e', 'O'):
		/* operator ^= */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "^=", 2));

	case SIMPLE_HASH('e', 'q'):
		/* operator == */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "==", 2));

	case SIMPLE_HASH('g', 'e'):
		/* operator >= */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, ">=", 2));

	case SIMPLE_HASH('g', 't'):
		/* operator > */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, ">", 1));

	case SIMPLE_HASH('i', 'x'):
		/* operator [] */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "[]", 2));

	case SIMPLE_HASH('l', 'e'):
		/* operator <= */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "<=", 2));

	case SIMPLE_HASH('l', 's'):
		/* operator << */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "<<", 2));

	case SIMPLE_HASH('l', 'S'):
		/* operator <<= */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "<<=", 3));

	case SIMPLE_HASH('l', 't'):
		/* operator < */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "<", 1));

	case SIMPLE_HASH('m', 'i'):
		/* operator - */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "-", 1));

	case SIMPLE_HASH('m', 'I'):
		/* operator -= */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "-=", 2));

	case SIMPLE_HASH('m', 'l'):
		/* operator * */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "*", 1));

	case SIMPLE_HASH('m', 'L'):
		/* operator *= */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "*=", 2));

	case SIMPLE_HASH('m', 'm'):
		/* operator -- */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "--", 2));

	case SIMPLE_HASH('n', 'a'):
		/* operator new[] */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_unary(ddata, "new []", 6));

	case SIMPLE_HASH('n', 'e'):
		/* operator != */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "!=", 2));

	case SIMPLE_HASH('n', 'g'):
		/* operator - (unary) */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_unary(ddata, "-", 1));

	case SIMPLE_HASH('n', 't'):
		/* operator ! */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "!", 1));

	case SIMPLE_HASH('n', 'w'):
		/* operator new */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_unary(ddata, "new", 3));

	case SIMPLE_HASH('o', 'o'):
		/* operator || */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "||", 2));

	case SIMPLE_HASH('o', 'r'):
		/* operator | */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "|", 1));

	case SIMPLE_HASH('o', 'R'):
		/* operator |= */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "|=", 2));

	case SIMPLE_HASH('p', 'l'):
		/* operator + */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "+", 1));

	case SIMPLE_HASH('p', 'L'):
		/* operator += */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "+=", 2));

	case SIMPLE_HASH('p', 'm'):
		/* operator ->* */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "->*", 3));

	case SIMPLE_HASH('p', 'p'):
		/* operator ++ */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "++", 2));

	case SIMPLE_HASH('p', 's'):
		/* operator + (unary) */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_unary(ddata, "+", 1));

	case SIMPLE_HASH('p', 't'):
		/* operator -> */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "->", 2));

	case SIMPLE_HASH('q', 'u'):
		/* operator ? */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_trinary(ddata, "?", 1,
		    ":", 1));

	case SIMPLE_HASH('r', 'm'):
		/* operator % */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "%", 1));

	case SIMPLE_HASH('r', 'M'):
		/* operator %= */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, "%=", 2));

	case SIMPLE_HASH('r', 's'):
		/* operator >> */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, ">>", 2));

	case SIMPLE_HASH('r', 'S'):
		/* operator >>= */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_binary(ddata, ">>=", 3));

	case SIMPLE_HASH('r', 'z'):
		/* operator sizeof */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_unary(ddata, "sizeof", 6));

	case SIMPLE_HASH('s', 'v'):
		/* operator sizeof */
		ddata->cur += 2;
		return (cpp_demangle_read_expression_unary(ddata, "sizeof", 6));
	}

	switch (*ddata->cur) {
	case 'L':
		return (cpp_demangle_read_expr_primary(ddata));
	case 'T':
		return (cpp_demangle_read_tmpl_param(ddata));
	}

	return (0);
}

static int
cpp_demangle_read_expression_flat(struct cpp_demangle_data *ddata, char **str)
{
	struct vector_str *output;
	size_t i, p_idx, idx, exp_len;
	char *exp;

	output = ddata->push_head > 0 ? &ddata->output_tmp :
	    &ddata->output;

	p_idx = output->size;

	if (!cpp_demangle_read_expression(ddata))
		return (0);

	if ((exp = vector_str_substr(output, p_idx, output->size - 1,
	    &exp_len)) == NULL)
		return (0);

	idx = output->size;
	for (i = p_idx; i < idx; ++i) {
		if (!vector_str_pop(output)) {
			free(exp);
			return (0);
		}
	}

	*str = exp;

	return (1);
}

static int
cpp_demangle_read_expression_binary(struct cpp_demangle_data *ddata,
    const char *name, size_t len)
{

	if (ddata == NULL || name == NULL || len == 0)
		return (0);
	if (!cpp_demangle_read_expression(ddata))
		return (0);
	if (!cpp_demangle_push_str(ddata, name, len))
		return (0);

	return (cpp_demangle_read_expression(ddata));
}

static int
cpp_demangle_read_expression_unary(struct cpp_demangle_data *ddata,
    const char *name, size_t len)
{

	if (ddata == NULL || name == NULL || len == 0)
		return (0);
	if (!cpp_demangle_read_expression(ddata))
		return (0);

	return (cpp_demangle_push_str(ddata, name, len));
}

static int
cpp_demangle_read_expression_trinary(struct cpp_demangle_data *ddata,
    const char *name1, size_t len1, const char *name2, size_t len2)
{

	if (ddata == NULL || name1 == NULL || len1 == 0 || name2 == NULL ||
	    len2 == 0)
		return (0);

	if (!cpp_demangle_read_expression(ddata))
		return (0);
	if (!cpp_demangle_push_str(ddata, name1, len1))
		return (0);
	if (!cpp_demangle_read_expression(ddata))
		return (0);
	if (!cpp_demangle_push_str(ddata, name2, len2))
		return (0);

	return (cpp_demangle_read_expression(ddata));
}

static int
cpp_demangle_read_function(struct cpp_demangle_data *ddata, int *ext_c,
    struct vector_type_qualifier *v)
{
	size_t class_type_size, class_type_len, limit;
	const char *class_type;

	if (ddata == NULL || *ddata->cur != 'F' || v == NULL)
		return (0);

	++ddata->cur;
	if (*ddata->cur == 'Y') {
		if (ext_c != NULL)
			*ext_c = 1;
		++ddata->cur;
	}
	if (!cpp_demangle_read_type(ddata, 0))
		return (0);
	if (*ddata->cur != 'E') {
		if (!cpp_demangle_push_str(ddata, "(", 1))
			return (0);
		if (vector_read_cmd_find(&ddata->cmd, READ_PTRMEM)) {
			if ((class_type_size = ddata->class_type.size) == 0)
				return (0);
			class_type =
			    ddata->class_type.container[class_type_size - 1];
			if (class_type == NULL)
				return (0);
			if ((class_type_len = strlen(class_type)) == 0)
				return (0);
			if (!cpp_demangle_push_str(ddata, class_type,
			    class_type_len))
				return (0);
			if (!cpp_demangle_push_str(ddata, "::*", 3))
				return (0);
			++ddata->func_type;
		} else {
			if (!cpp_demangle_push_type_qualifier(ddata, v,
			    (const char *) NULL))
				return (0);
			vector_type_qualifier_dest(v);
			if (!vector_type_qualifier_init(v))
				return (0);
		}

		if (!cpp_demangle_push_str(ddata, ")(", 2))
			return (0);

		limit = 0;
		for (;;) {
			if (!cpp_demangle_read_type(ddata, 0))
				return (0);
			if (*ddata->cur == 'E')
				break;
			if (limit++ > CPP_DEMANGLE_TRY_LIMIT)
				return (0);
		}

		if (vector_read_cmd_find(&ddata->cmd, READ_PTRMEM) == 1) {
			if (!cpp_demangle_push_type_qualifier(ddata, v,
			    (const char *) NULL))
				return (0);
			vector_type_qualifier_dest(v);
			if (!vector_type_qualifier_init(v))
				return (0);
		}

		if (!cpp_demangle_push_str(ddata, ")", 1))
			return (0);
	}

	++ddata->cur;

	return (1);
}

/* read encoding, encoding are function name, data name, special-name */
static int
cpp_demangle_read_encoding(struct cpp_demangle_data *ddata)
{
	char *name, *type, *num_str;
	long offset;
	int rtn;

	if (ddata == NULL || *ddata->cur == '\0')
		return (0);

	/* special name */
	switch (SIMPLE_HASH(*ddata->cur, *(ddata->cur + 1))) {
	case SIMPLE_HASH('G', 'A'):
		if (!cpp_demangle_push_str(ddata, "hidden alias for ", 17))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_encoding(ddata));

	case SIMPLE_HASH('G', 'R'):
		if (!cpp_demangle_push_str(ddata, "reference temporary #", 21))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		if (!cpp_demangle_read_name_flat(ddata, &name))
			return (0);
		rtn = 0;
		if (!cpp_demangle_read_number_as_string(ddata, &num_str))
			goto clean1;
		if (!cpp_demangle_push_str(ddata, num_str, strlen(num_str)))
			goto clean2;
		if (!cpp_demangle_push_str(ddata, " for ", 5))
			goto clean2;
		if (!cpp_demangle_push_str(ddata, name, strlen(name)))
			goto clean2;
		rtn = 1;
	clean2:
		free(num_str);
	clean1:
		free(name);
		return (rtn);

	case SIMPLE_HASH('G', 'T'):
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		switch (*ddata->cur) {
		case 'n':
			if (!cpp_demangle_push_str(ddata,
			    "non-transaction clone for ", 26))
				return (0);
			break;
		case 't':
		default:
			if (!cpp_demangle_push_str(ddata,
			    "transaction clone for ", 22))
				return (0);
			break;
		}
		++ddata->cur;
		return (cpp_demangle_read_encoding(ddata));

	case SIMPLE_HASH('G', 'V'):
		/* sentry object for 1 time init */
		if (!cpp_demangle_push_str(ddata, "guard variable for ", 20))
			return (0);
		ddata->cur += 2;
		break;

	case SIMPLE_HASH('T', 'c'):
		/* virtual function covariant override thunk */
		if (!cpp_demangle_push_str(ddata,
		    "virtual function covariant override ", 36))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		if (!cpp_demangle_read_offset(ddata))
			return (0);
		if (!cpp_demangle_read_offset(ddata))
			return (0);
		return (cpp_demangle_read_encoding(ddata));

	case SIMPLE_HASH('T', 'C'):
		/* construction vtable */
		if (!cpp_demangle_push_str(ddata, "construction vtable for ",
		    24))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		if (!cpp_demangle_read_type_flat(ddata, &type))
			return (0);
		rtn = 0;
		if (!cpp_demangle_read_number(ddata, &offset))
			goto clean3;
		if (*ddata->cur++ != '_')
			goto clean3;
		if (!cpp_demangle_read_type(ddata, 0))
			goto clean3;
		if (!cpp_demangle_push_str(ddata, "-in-", 4))
			goto clean3;
		if (!cpp_demangle_push_str(ddata, type, strlen(type)))
			goto clean3;
		rtn = 1;
	clean3:
		free(type);
		return (rtn);

	case SIMPLE_HASH('T', 'D'):
		/* typeinfo common proxy */
		break;

	case SIMPLE_HASH('T', 'F'):
		/* typeinfo fn */
		if (!cpp_demangle_push_str(ddata, "typeinfo fn for ", 16))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, 0));

	case SIMPLE_HASH('T', 'h'):
		/* virtual function non-virtual override thunk */
		if (!cpp_demangle_push_str(ddata,
		    "virtual function non-virtual override ", 38))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		if (!cpp_demangle_read_nv_offset(ddata))
			return (0);
		return (cpp_demangle_read_encoding(ddata));

	case SIMPLE_HASH('T', 'H'):
		/* TLS init function */
		if (!cpp_demangle_push_str(ddata, "TLS init function for ",
		    22))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		break;

	case SIMPLE_HASH('T', 'I'):
		/* typeinfo structure */
		if (!cpp_demangle_push_str(ddata, "typeinfo for ", 13))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, 0));

	case SIMPLE_HASH('T', 'J'):
		/* java class */
		if (!cpp_demangle_push_str(ddata, "java Class for ", 15))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, 0));

	case SIMPLE_HASH('T', 'S'):
		/* RTTI name (NTBS) */
		if (!cpp_demangle_push_str(ddata, "typeinfo name for ", 18))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, 0));

	case SIMPLE_HASH('T', 'T'):
		/* VTT table */
		if (!cpp_demangle_push_str(ddata, "VTT for ", 8))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, 0));

	case SIMPLE_HASH('T', 'v'):
		/* virtual function virtual override thunk */
		if (!cpp_demangle_push_str(ddata,
		    "virtual function virtual override ", 34))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		if (!cpp_demangle_read_v_offset(ddata))
			return (0);
		return (cpp_demangle_read_encoding(ddata));

	case SIMPLE_HASH('T', 'V'):
		/* virtual table */
		if (!cpp_demangle_push_str(ddata, "vtable for ", 12))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, 0));

	case SIMPLE_HASH('T', 'W'):
		/* TLS wrapper function */
		if (!cpp_demangle_push_str(ddata, "TLS wrapper function for ",
		    25))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		break;
	}

	return (cpp_demangle_read_name(ddata));
}

static int
cpp_demangle_read_local_name(struct cpp_demangle_data *ddata)
{
	size_t limit;

	if (ddata == NULL)
		return (0);
	if (*(++ddata->cur) == '\0')
		return (0);
	if (!cpp_demangle_read_encoding(ddata))
		return (0);

	limit = 0;
	for (;;) {
		if (!cpp_demangle_read_type(ddata, 1))
			return (0);
		if (*ddata->cur == 'E')
			break;
		if (limit++ > CPP_DEMANGLE_TRY_LIMIT)
			return (0);
	}
	if (*(++ddata->cur) == '\0')
		return (0);
	if (ddata->paren == true) {
		if (!cpp_demangle_push_str(ddata, ")", 1))
			return (0);
		ddata->paren = false;
	}
	if (*ddata->cur == 's')
		++ddata->cur;
	else {
		if (!cpp_demangle_push_str(ddata, "::", 2))
			return (0);
		if (!cpp_demangle_read_name(ddata))
			return (0);
	}
	if (*ddata->cur == '_') {
		++ddata->cur;
		while (ELFTC_ISDIGIT(*ddata->cur) != 0)
			++ddata->cur;
	}

	return (1);
}

static int
cpp_demangle_read_name(struct cpp_demangle_data *ddata)
{
	struct vector_str *output, v;
	size_t p_idx, subst_str_len;
	int rtn;
	char *subst_str;

	if (ddata == NULL || *ddata->cur == '\0')
		return (0);

	output = ddata->push_head > 0 ? &ddata->output_tmp : &ddata->output;

	subst_str = NULL;

	switch (*ddata->cur) {
	case 'S':
		return (cpp_demangle_read_subst(ddata));
	case 'N':
		return (cpp_demangle_read_nested_name(ddata));
	case 'Z':
		return (cpp_demangle_read_local_name(ddata));
	}

	if (!vector_str_init(&v))
		return (0);

	p_idx = output->size;
	rtn = 0;
	if (!cpp_demangle_read_uqname(ddata))
		goto clean;
	if ((subst_str = vector_str_substr(output, p_idx, output->size - 1,
	    &subst_str_len)) == NULL)
		goto clean;
	if (subst_str_len > 8 && strstr(subst_str, "operator") != NULL) {
		rtn = 1;
		goto clean;
	}
	if (!vector_str_push(&v, subst_str, subst_str_len))
		goto clean;
	if (!cpp_demangle_push_subst_v(ddata, &v))
		goto clean;

	if (*ddata->cur == 'I') {
		p_idx = output->size;
		if (!cpp_demangle_read_tmpl_args(ddata))
			goto clean;
		free(subst_str);
		if ((subst_str = vector_str_substr(output, p_idx,
		    output->size - 1, &subst_str_len)) == NULL)
			goto clean;
		if (!vector_str_push(&v, subst_str, subst_str_len))
			goto clean;
		if (!cpp_demangle_push_subst_v(ddata, &v))
			goto clean;
	}

	rtn = 1;

clean:
	free(subst_str);
	vector_str_dest(&v);

	return (rtn);
}

static int
cpp_demangle_read_name_flat(struct cpp_demangle_data *ddata, char **str)
{
	struct vector_str *output;
	size_t i, p_idx, idx, name_len;
	char *name;

	output = ddata->push_head > 0 ? &ddata->output_tmp :
	    &ddata->output;

	p_idx = output->size;

	if (!cpp_demangle_read_name(ddata))
		return (0);

	if ((name = vector_str_substr(output, p_idx, output->size - 1,
	    &name_len)) == NULL)
		return (0);

	idx = output->size;
	for (i = p_idx; i < idx; ++i) {
		if (!vector_str_pop(output)) {
			free(name);
			return (0);
		}
	}

	*str = name;

	return (1);
}

static int
cpp_demangle_read_nested_name(struct cpp_demangle_data *ddata)
{
	struct vector_str *output, v;
	size_t limit, p_idx, subst_str_len;
	int rtn;
	char *subst_str;

	if (ddata == NULL || *ddata->cur != 'N')
		return (0);
	if (*(++ddata->cur) == '\0')
		return (0);

	while (*ddata->cur == 'r' || *ddata->cur == 'V' ||
	    *ddata->cur == 'K') {
		switch (*ddata->cur) {
		case 'r':
			ddata->mem_rst = true;
			break;
		case 'V':
			ddata->mem_vat = true;
			break;
		case 'K':
			ddata->mem_cst = true;
			break;
		}
		++ddata->cur;
	}

	output = ddata->push_head > 0 ? &ddata->output_tmp : &ddata->output;
	if (!vector_str_init(&v))
		return (0);

	rtn = 0;
	limit = 0;
	for (;;) {
		p_idx = output->size;
		switch (*ddata->cur) {
		case 'I':
			if (!cpp_demangle_read_tmpl_args(ddata))
				goto clean;
			break;
		case 'S':
			if (!cpp_demangle_read_subst(ddata))
				goto clean;
			break;
		case 'T':
			if (!cpp_demangle_read_tmpl_param(ddata))
				goto clean;
			break;
		default:
			if (!cpp_demangle_read_uqname(ddata))
				goto clean;
		}

		if ((subst_str = vector_str_substr(output, p_idx,
		    output->size - 1, &subst_str_len)) == NULL)
			goto clean;
		if (!vector_str_push(&v, subst_str, subst_str_len)) {
			free(subst_str);
			goto clean;
		}
		free(subst_str);

		if (!cpp_demangle_push_subst_v(ddata, &v))
			goto clean;
		if (*ddata->cur == 'E')
			break;
		else if (*ddata->cur != 'I' &&
		    *ddata->cur != 'C' && *ddata->cur != 'D') {
			if (!cpp_demangle_push_str(ddata, "::", 2))
				goto clean;
			if (!vector_str_push(&v, "::", 2))
				goto clean;
		}
		if (limit++ > CPP_DEMANGLE_TRY_LIMIT)
			goto clean;
	}

	++ddata->cur;
	rtn = 1;

clean:
	vector_str_dest(&v);

	return (rtn);
}

/*
 * read number
 * number ::= [n] <decimal>
 */
static int
cpp_demangle_read_number(struct cpp_demangle_data *ddata, long *rtn)
{
	long len, negative_factor;

	if (ddata == NULL || rtn == NULL)
		return (0);

	negative_factor = 1;
	if (*ddata->cur == 'n') {
		negative_factor = -1;

		++ddata->cur;
	}
	if (ELFTC_ISDIGIT(*ddata->cur) == 0)
		return (0);

	errno = 0;
	if ((len = strtol(ddata->cur, (char **) NULL, 10)) == 0 &&
	    errno != 0)
		return (0);

	while (ELFTC_ISDIGIT(*ddata->cur) != 0)
		++ddata->cur;

	assert(len >= 0);
	assert(negative_factor == 1 || negative_factor == -1);

	*rtn = len * negative_factor;

	return (1);
}

static int
cpp_demangle_read_number_as_string(struct cpp_demangle_data *ddata, char **str)
{
	long n;

	if (!cpp_demangle_read_number(ddata, &n)) {
		*str = NULL;
		return (0);
	}

	if (asprintf(str, "%ld", n) < 0) {
		*str = NULL;
		return (0);
	}

	return (1);
}

static int
cpp_demangle_read_nv_offset(struct cpp_demangle_data *ddata)
{

	if (ddata == NULL)
		return (0);

	if (!cpp_demangle_push_str(ddata, "offset : ", 9))
		return (0);

	return (cpp_demangle_read_offset_number(ddata));
}

/* read offset, offset are nv-offset, v-offset */
static int
cpp_demangle_read_offset(struct cpp_demangle_data *ddata)
{

	if (ddata == NULL)
		return (0);

	if (*ddata->cur == 'h') {
		++ddata->cur;
		return (cpp_demangle_read_nv_offset(ddata));
	} else if (*ddata->cur == 'v') {
		++ddata->cur;
		return (cpp_demangle_read_v_offset(ddata));
	}

	return (0);
}

static int
cpp_demangle_read_offset_number(struct cpp_demangle_data *ddata)
{
	bool negative;
	const char *start;

	if (ddata == NULL || *ddata->cur == '\0')
		return (0);

	/* offset could be negative */
	if (*ddata->cur == 'n') {
		negative = true;
		start = ddata->cur + 1;
	} else {
		negative = false;
		start = ddata->cur;
	}

	while (*ddata->cur != '_')
		++ddata->cur;

	if (negative && !cpp_demangle_push_str(ddata, "-", 1))
		return (0);

	assert(start != NULL);

	if (!cpp_demangle_push_str(ddata, start, ddata->cur - start))
		return (0);
	if (!cpp_demangle_push_str(ddata, " ", 1))
		return (0);

	++ddata->cur;

	return (1);
}

static int
cpp_demangle_read_pointer_to_member(struct cpp_demangle_data *ddata)
{
	size_t class_type_len, i, idx, p_idx;
	int p_func_type, rtn;
	char *class_type;

	if (ddata == NULL || *ddata->cur != 'M' || *(++ddata->cur) == '\0')
		return (0);

	p_idx = ddata->output.size;
	if (!cpp_demangle_read_type(ddata, 0))
		return (0);

	if ((class_type = vector_str_substr(&ddata->output, p_idx,
	    ddata->output.size - 1, &class_type_len)) == NULL)
		return (0);

	rtn = 0;
	idx = ddata->output.size;
	for (i = p_idx; i < idx; ++i)
		if (!vector_str_pop(&ddata->output))
			goto clean1;

	if (!vector_read_cmd_push(&ddata->cmd, READ_PTRMEM))
		goto clean1;

	if (!vector_str_push(&ddata->class_type, class_type, class_type_len))
		goto clean2;

	p_func_type = ddata->func_type;
	if (!cpp_demangle_read_type(ddata, 0))
		goto clean3;

	if (p_func_type == ddata->func_type) {
		if (!cpp_demangle_push_str(ddata, " ", 1))
			goto clean3;
		if (!cpp_demangle_push_str(ddata, class_type, class_type_len))
			goto clean3;
		if (!cpp_demangle_push_str(ddata, "::*", 3))
			goto clean3;
	}

	rtn = 1;
clean3:
	if (!vector_str_pop(&ddata->class_type))
		rtn = 0;
clean2:
	if (!vector_read_cmd_pop(&ddata->cmd))
		rtn = 0;
clean1:
	free(class_type);

	return (rtn);
}

/* read source-name, source-name is <len> <ID> */
static int
cpp_demangle_read_sname(struct cpp_demangle_data *ddata)
{
	long len;
	int err;

	if (ddata == NULL || cpp_demangle_read_number(ddata, &len) == 0 ||
	    len <= 0)
		return (0);

	if (len == 12 && (memcmp("_GLOBAL__N_1", ddata->cur, 12) == 0))
		err = cpp_demangle_push_str(ddata, "(anonymous namespace)", 21);
	else
		err = cpp_demangle_push_str(ddata, ddata->cur, len);

	if (err == 0)
		return (0);

	assert(ddata->output.size > 0);
	if (vector_read_cmd_find(&ddata->cmd, READ_TMPL) == 0)
		ddata->last_sname =
		    ddata->output.container[ddata->output.size - 1];

	ddata->cur += len;

	return (1);
}

static int
cpp_demangle_read_subst(struct cpp_demangle_data *ddata)
{
	long nth;

	if (ddata == NULL || *ddata->cur == '\0')
		return (0);

	/* abbreviations of the form Sx */
	switch (SIMPLE_HASH(*ddata->cur, *(ddata->cur + 1))) {
	case SIMPLE_HASH('S', 'a'):
		/* std::allocator */
		if (cpp_demangle_push_str(ddata, "std::allocator", 14) == 0)
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::allocator", 14));
		return (1);

	case SIMPLE_HASH('S', 'b'):
		/* std::basic_string */
		if (!cpp_demangle_push_str(ddata, "std::basic_string", 17))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::basic_string", 17));
		return (1);

	case SIMPLE_HASH('S', 'd'):
		/* std::basic_iostream<char, std::char_traits<char> > */
		if (!cpp_demangle_push_str(ddata, "std::basic_iostream", 19))
			return (0);
		ddata->last_sname = "basic_iostream";
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::basic_iostream", 19));
		return (1);

	case SIMPLE_HASH('S', 'i'):
		/* std::basic_istream<char, std::char_traits<char> > */
		if (!cpp_demangle_push_str(ddata, "std::basic_istream", 18))
			return (0);
		ddata->last_sname = "basic_istream";
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::basic_istream", 18));
		return (1);

	case SIMPLE_HASH('S', 'o'):
		/* std::basic_ostream<char, std::char_traits<char> > */
		if (!cpp_demangle_push_str(ddata, "std::basic_ostream", 18))
			return (0);
		ddata->last_sname = "basic_ostream";
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::basic_ostream", 18));
		return (1);

	case SIMPLE_HASH('S', 's'):
		/*
		 * std::basic_string<char, std::char_traits<char>,
		 * std::allocator<char> >
		 *
		 * a.k.a std::string
		 */
		if (!cpp_demangle_push_str(ddata, "std::string", 11))
			return (0);
		ddata->last_sname = "string";
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::string", 11));
		return (1);

	case SIMPLE_HASH('S', 't'):
		/* std:: */
		return (cpp_demangle_read_subst_std(ddata));
	}

	if (*(++ddata->cur) == '\0')
		return (0);

	/* substitution */
	if (*ddata->cur == '_')
		return (cpp_demangle_get_subst(ddata, 0));
	else {
		errno = 0;
		/* substitution number is base 36 */
		if ((nth = strtol(ddata->cur, (char **) NULL, 36)) == 0 &&
		    errno != 0)
			return (0);

		/* first was '_', so increase one */
		++nth;

		while (*ddata->cur != '_')
			++ddata->cur;

		assert(nth > 0);

		return (cpp_demangle_get_subst(ddata, nth));
	}

	/* NOTREACHED */
	return (0);
}

static int
cpp_demangle_read_subst_std(struct cpp_demangle_data *ddata)
{
	struct vector_str *output, v;
	size_t p_idx, subst_str_len;
	int rtn;
	char *subst_str;

	if (ddata == NULL)
		return (0);

	if (!vector_str_init(&v))
		return (0);

	subst_str = NULL;
	rtn = 0;
	if (!cpp_demangle_push_str(ddata, "std::", 5))
		goto clean;

	if (!vector_str_push(&v, "std::", 5))
		goto clean;

	ddata->cur += 2;

	output = ddata->push_head > 0 ? &ddata->output_tmp : &ddata->output;

	p_idx = output->size;
	if (!cpp_demangle_read_uqname(ddata))
		goto clean;

	if ((subst_str = vector_str_substr(output, p_idx, output->size - 1,
	    &subst_str_len)) == NULL)
		goto clean;

	if (!vector_str_push(&v, subst_str, subst_str_len))
		goto clean;

	if (!cpp_demangle_push_subst_v(ddata, &v))
		goto clean;

	if (*ddata->cur == 'I') {
		p_idx = output->size;
		if (!cpp_demangle_read_tmpl_args(ddata))
			goto clean;
		free(subst_str);
		if ((subst_str = vector_str_substr(output, p_idx,
		    output->size - 1, &subst_str_len)) == NULL)
			goto clean;
		if (!vector_str_push(&v, subst_str, subst_str_len))
			goto clean;
		if (!cpp_demangle_push_subst_v(ddata, &v))
			goto clean;
	}

	rtn = 1;
clean:
	free(subst_str);
	vector_str_dest(&v);

	return (rtn);
}

static int
cpp_demangle_read_subst_stdtmpl(struct cpp_demangle_data *ddata,
    const char *str, size_t len)
{
	struct vector_str *output;
	size_t p_idx, substr_len;
	int rtn;
	char *subst_str, *substr;

	if (ddata == NULL || str == NULL || len == 0)
		return (0);

	output = ddata->push_head > 0 ? &ddata->output_tmp : &ddata->output;

	p_idx = output->size;
	substr = NULL;
	subst_str = NULL;

	if (!cpp_demangle_read_tmpl_args(ddata))
		return (0);
	if ((substr = vector_str_substr(output, p_idx, output->size - 1,
	    &substr_len)) == NULL)
		return (0);

	rtn = 0;
	if ((subst_str = malloc(sizeof(char) * (substr_len + len + 1))) ==
	    NULL)
		goto clean;

	memcpy(subst_str, str, len);
	memcpy(subst_str + len, substr, substr_len);
	subst_str[substr_len + len] = '\0';

	if (!cpp_demangle_push_subst(ddata, subst_str, substr_len + len))
		goto clean;

	rtn = 1;
clean:
	free(subst_str);
	free(substr);

	return (rtn);
}

static int
cpp_demangle_read_tmpl_arg(struct cpp_demangle_data *ddata)
{

	if (ddata == NULL || *ddata->cur == '\0')
		return (0);

	switch (*ddata->cur) {
	case 'L':
		return (cpp_demangle_read_expr_primary(ddata));
	case 'X':
		return (cpp_demangle_read_expression(ddata));
	}

	return (cpp_demangle_read_type(ddata, 0));
}

static int
cpp_demangle_read_tmpl_args(struct cpp_demangle_data *ddata)
{
	struct vector_str *v;
	size_t arg_len, idx, limit, size;
	char *arg;

	if (ddata == NULL || *ddata->cur == '\0')
		return (0);

	++ddata->cur;

	if (!vector_read_cmd_push(&ddata->cmd, READ_TMPL))
		return (0);

	if (!cpp_demangle_push_str(ddata, "<", 1))
		return (0);

	limit = 0;
	v = ddata->push_head > 0 ? &ddata->output_tmp : &ddata->output;
	for (;;) {
		idx = v->size;
		if (!cpp_demangle_read_tmpl_arg(ddata))
			return (0);
		if ((arg = vector_str_substr(v, idx, v->size - 1, &arg_len)) ==
		    NULL)
			return (0);
		if (!vector_str_find(&ddata->tmpl, arg, arg_len) &&
		    !vector_str_push(&ddata->tmpl, arg, arg_len)) {
			free(arg);
			return (0);
		}

		free(arg);

		if (*ddata->cur == 'E') {
			++ddata->cur;
			size = v->size;
			assert(size > 0);
			if (!strncmp(v->container[size - 1], ">", 1)) {
				if (!cpp_demangle_push_str(ddata, " >", 2))
					return (0);
			} else if (!cpp_demangle_push_str(ddata, ">", 1))
				return (0);
			break;
		} else if (*ddata->cur != 'I' &&
		    !cpp_demangle_push_str(ddata, ", ", 2))
			return (0);

		if (limit++ > CPP_DEMANGLE_TRY_LIMIT)
			return (0);
	}

	return (vector_read_cmd_pop(&ddata->cmd));
}

/*
 * Read template parameter that forms in 'T[number]_'.
 * This function much like to read_subst but only for types.
 */
static int
cpp_demangle_read_tmpl_param(struct cpp_demangle_data *ddata)
{
	long nth;

	if (ddata == NULL || *ddata->cur != 'T')
		return (0);

	++ddata->cur;

	if (*ddata->cur == '_')
		return (cpp_demangle_get_tmpl_param(ddata, 0));
	else {

		errno = 0;
		if ((nth = strtol(ddata->cur, (char **) NULL, 36)) == 0 &&
		    errno != 0)
			return (0);

		/* T_ is first */
		++nth;

		while (*ddata->cur != '_')
			++ddata->cur;

		assert(nth > 0);

		return (cpp_demangle_get_tmpl_param(ddata, nth));
	}

	/* NOTREACHED */
	return (0);
}

static int
cpp_demangle_read_type(struct cpp_demangle_data *ddata, int delimit)
{
	struct vector_type_qualifier v;
	struct vector_str *output;
	size_t p_idx, type_str_len;
	int extern_c, is_builtin;
	long len;
	char *type_str, *exp_str, *num_str;

	if (ddata == NULL)
		return (0);

	output = &ddata->output;
	if (ddata->output.size > 0 && !strncmp(ddata->output.container[ddata->output.size - 1], ">", 1)) {
		ddata->push_head++;
		output = &ddata->output_tmp;
	} else if (delimit == 1) {
		if (ddata->paren == false) {
			if (!cpp_demangle_push_str(ddata, "(", 1))
				return (0);
			if (ddata->output.size < 2)
				return (0);
			ddata->paren = true;
			ddata->pfirst = true;
			/* Need pop function name */
			if (ddata->subst.size == 1 &&
			    !vector_str_pop(&ddata->subst))
				return (0);
		}

		if (ddata->pfirst)
			ddata->pfirst = false;
		else if (*ddata->cur != 'I' &&
		    !cpp_demangle_push_str(ddata, ", ", 2))
			return (0);
	}

	assert(output != NULL);
	/*
	 * [r, V, K] [P, R, C, G, U] builtin, function, class-enum, array
	 * pointer-to-member, template-param, template-template-param, subst
	 */

	if (!vector_type_qualifier_init(&v))
		return (0);

	extern_c = 0;
	is_builtin = 1;
	p_idx = output->size;
	type_str = exp_str = num_str = NULL;
again:
	/* builtin type */
	switch (*ddata->cur) {
	case 'a':
		/* signed char */
		if (!cpp_demangle_push_str(ddata, "signed char", 11))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'A':
		/* array type */
		if (!cpp_demangle_read_array(ddata))
			goto clean;
		is_builtin = 0;
		goto rtn;

	case 'b':
		/* bool */
		if (!cpp_demangle_push_str(ddata, "bool", 4))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'C':
		/* complex pair */
		if (!vector_type_qualifier_push(&v, TYPE_CMX))
			goto clean;
		++ddata->cur;
		goto again;

	case 'c':
		/* char */
		if (!cpp_demangle_push_str(ddata, "char", 4))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'd':
		/* double */
		if (!cpp_demangle_push_str(ddata, "double", 6))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'D':
		++ddata->cur;
		switch (*ddata->cur) {
		case 'd':
			/* IEEE 754r decimal floating point (64 bits) */
			if (!cpp_demangle_push_str(ddata, "decimal64", 9))
				goto clean;
			++ddata->cur;
			break;
		case 'e':
			/* IEEE 754r decimal floating point (128 bits) */
			if (!cpp_demangle_push_str(ddata, "decimal128", 10))
				goto clean;
			++ddata->cur;
			break;
		case 'f':
			/* IEEE 754r decimal floating point (32 bits) */
			if (!cpp_demangle_push_str(ddata, "decimal32", 9))
				goto clean;
			++ddata->cur;
			break;
		case 'h':
			/* IEEE 754r half-precision floating point (16 bits) */
			if (!cpp_demangle_push_str(ddata, "half", 4))
				goto clean;
			++ddata->cur;
			break;
		case 'i':
			/* char32_t */
			if (!cpp_demangle_push_str(ddata, "char32_t", 8))
				goto clean;
			++ddata->cur;
			break;
		case 'n':
			/* std::nullptr_t (i.e., decltype(nullptr)) */
			if (!cpp_demangle_push_str(ddata, "decltype(nullptr)",
			    17))
				goto clean;
			++ddata->cur;
			break;
		case 's':
			/* char16_t */
			if (!cpp_demangle_push_str(ddata, "char16_t", 8))
				goto clean;
			++ddata->cur;
			break;
		case 'v':
			/* gcc vector_size extension. */
			++ddata->cur;
			if (*ddata->cur == '_') {
				++ddata->cur;
				if (!cpp_demangle_read_expression_flat(ddata,
				    &exp_str))
					goto clean;
				if (!vector_str_push(&v.ext_name, exp_str,
				    strlen(exp_str)))
					goto clean;
			} else {
				if (!cpp_demangle_read_number_as_string(ddata,
				    &num_str))
					goto clean;
				if (!vector_str_push(&v.ext_name, num_str,
				    strlen(num_str)))
					goto clean;
			}
			if (*ddata->cur != '_')
				goto clean;
			++ddata->cur;
			if (!vector_type_qualifier_push(&v, TYPE_VEC))
				goto clean;
			goto again;
		default:
			goto clean;
		}
		goto rtn;

	case 'e':
		/* long double */
		if (!cpp_demangle_push_str(ddata, "long double", 11))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'f':
		/* float */
		if (!cpp_demangle_push_str(ddata, "float", 5))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'F':
		/* function */
		if (!cpp_demangle_read_function(ddata, &extern_c, &v))
			goto clean;
		is_builtin = 0;
		goto rtn;

	case 'g':
		/* __float128 */
		if (!cpp_demangle_push_str(ddata, "__float128", 10))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'G':
		/* imaginary */
		if (!vector_type_qualifier_push(&v, TYPE_IMG))
			goto clean;
		++ddata->cur;
		goto again;

	case 'h':
		/* unsigned char */
		if (!cpp_demangle_push_str(ddata, "unsigned char", 13))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'i':
		/* int */
		if (!cpp_demangle_push_str(ddata, "int", 3))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'j':
		/* unsigned int */
		if (!cpp_demangle_push_str(ddata, "unsigned int", 12))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'K':
		/* const */
		if (!vector_type_qualifier_push(&v, TYPE_CST))
			goto clean;
		++ddata->cur;
		goto again;

	case 'l':
		/* long */
		if (!cpp_demangle_push_str(ddata, "long", 4))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'm':
		/* unsigned long */
		if (!cpp_demangle_push_str(ddata, "unsigned long", 13))
			goto clean;

		++ddata->cur;

		goto rtn;
	case 'M':
		/* pointer to member */
		if (!cpp_demangle_read_pointer_to_member(ddata))
			goto clean;
		is_builtin = 0;
		goto rtn;

	case 'n':
		/* __int128 */
		if (!cpp_demangle_push_str(ddata, "__int128", 8))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'o':
		/* unsigned __int128 */
		if (!cpp_demangle_push_str(ddata, "unsigned __int128", 17))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'P':
		/* pointer */
		if (!vector_type_qualifier_push(&v, TYPE_PTR))
			goto clean;
		++ddata->cur;
		goto again;

	case 'r':
		/* restrict */
		if (!vector_type_qualifier_push(&v, TYPE_RST))
			goto clean;
		++ddata->cur;
		goto again;

	case 'R':
		/* reference */
		if (!vector_type_qualifier_push(&v, TYPE_REF))
			goto clean;
		++ddata->cur;
		goto again;

	case 's':
		/* short, local string */
		if (!cpp_demangle_push_str(ddata, "short", 5))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'S':
		/* substitution */
		if (!cpp_demangle_read_subst(ddata))
			goto clean;
		is_builtin = 0;
		goto rtn;

	case 't':
		/* unsigned short */
		if (!cpp_demangle_push_str(ddata, "unsigned short", 14))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'T':
		/* template parameter */
		if (!cpp_demangle_read_tmpl_param(ddata))
			goto clean;
		is_builtin = 0;
		goto rtn;

	case 'u':
		/* vendor extended builtin */
		++ddata->cur;
		if (!cpp_demangle_read_sname(ddata))
			goto clean;
		is_builtin = 0;
		goto rtn;

	case 'U':
		/* vendor extended type qualifier */
		if (!cpp_demangle_read_number(ddata, &len))
			goto clean;
		if (len <= 0)
			goto clean;
		if (!vector_str_push(&v.ext_name, ddata->cur, len))
			return (0);
		ddata->cur += len;
		if (!vector_type_qualifier_push(&v, TYPE_EXT))
			goto clean;
		goto again;

	case 'v':
		/* void */
		if (!cpp_demangle_push_str(ddata, "void", 4))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'V':
		/* volatile */
		if (!vector_type_qualifier_push(&v, TYPE_VAT))
			goto clean;
		++ddata->cur;
		goto again;

	case 'w':
		/* wchar_t */
		if (!cpp_demangle_push_str(ddata, "wchar_t", 7))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'x':
		/* long long */
		if (!cpp_demangle_push_str(ddata, "long long", 9))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'y':
		/* unsigned long long */
		if (!cpp_demangle_push_str(ddata, "unsigned long long", 18))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'z':
		/* ellipsis */
		if (!cpp_demangle_push_str(ddata, "...", 3))
			goto clean;
		++ddata->cur;
		goto rtn;
	}

	if (!cpp_demangle_read_name(ddata))
		goto clean;

	is_builtin = 0;
rtn:
	if ((type_str = vector_str_substr(output, p_idx, output->size - 1,
	    &type_str_len)) == NULL)
		goto clean;

	if (is_builtin == 0) {
		if (!vector_str_find(&ddata->subst, type_str, type_str_len) &&
		    !vector_str_push(&ddata->subst, type_str, type_str_len))
			goto clean;
	}

	if (!cpp_demangle_push_type_qualifier(ddata, &v, type_str))
		goto clean;

	free(type_str);
	free(exp_str);
	free(num_str);
	vector_type_qualifier_dest(&v);

	if (ddata->push_head > 0) {
		if (*ddata->cur == 'I' && cpp_demangle_read_tmpl_args(ddata)
		    == 0)
			return (0);

		if (--ddata->push_head > 0)
			return (1);

		if (!vector_str_push(&ddata->output_tmp, " ", 1))
			return (0);

		if (!vector_str_push_vector_head(&ddata->output,
		    &ddata->output_tmp))
			return (0);

		vector_str_dest(&ddata->output_tmp);
		if (!vector_str_init(&ddata->output_tmp))
			return (0);

		if (!cpp_demangle_push_str(ddata, "(", 1))
			return (0);

		ddata->paren = true;
		ddata->pfirst = true;
	}

	return (1);
clean:
	free(type_str);
	free(exp_str);
	free(num_str);
	vector_type_qualifier_dest(&v);

	return (0);
}

static int
cpp_demangle_read_type_flat(struct cpp_demangle_data *ddata, char **str)
{
	struct vector_str *output;
	size_t i, p_idx, idx, type_len;
	char *type;

	output = ddata->push_head > 0 ? &ddata->output_tmp :
	    &ddata->output;

	p_idx = output->size;

	if (!cpp_demangle_read_type(ddata, 0))
		return (0);

	if ((type = vector_str_substr(output, p_idx, output->size - 1,
	    &type_len)) == NULL)
		return (0);

	idx = output->size;
	for (i = p_idx; i < idx; ++i) {
		if (!vector_str_pop(output)) {
			free(type);
			return (0);
		}
	}

	*str = type;

	return (1);
}

/*
 * read unqualified-name, unqualified name are operator-name, ctor-dtor-name,
 * source-name
 */
static int
cpp_demangle_read_uqname(struct cpp_demangle_data *ddata)
{
	size_t len;

	if (ddata == NULL || *ddata->cur == '\0')
		return (0);

	/* operator name */
	switch (SIMPLE_HASH(*ddata->cur, *(ddata->cur + 1))) {
	case SIMPLE_HASH('a', 'a'):
		/* operator && */
		if (!cpp_demangle_push_str(ddata, "operator&&", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('a', 'd'):
		/* operator & (unary) */
		if (!cpp_demangle_push_str(ddata, "operator&", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('a', 'n'):
		/* operator & */
		if (!cpp_demangle_push_str(ddata, "operator&", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('a', 'N'):
		/* operator &= */
		if (!cpp_demangle_push_str(ddata, "operator&=", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('a', 'S'):
		/* operator = */
		if (!cpp_demangle_push_str(ddata, "operator=", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('c', 'l'):
		/* operator () */
		if (!cpp_demangle_push_str(ddata, "operator()", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('c', 'm'):
		/* operator , */
		if (!cpp_demangle_push_str(ddata, "operator,", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('c', 'o'):
		/* operator ~ */
		if (!cpp_demangle_push_str(ddata, "operator~", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('c', 'v'):
		/* operator (cast) */
		if (!cpp_demangle_push_str(ddata, "operator(cast)", 14))
			return (0);
		ddata->cur += 2;
		return (cpp_demangle_read_type(ddata, 1));

	case SIMPLE_HASH('d', 'a'):
		/* operator delete [] */
		if (!cpp_demangle_push_str(ddata, "operator delete []", 18))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('d', 'e'):
		/* operator * (unary) */
		if (!cpp_demangle_push_str(ddata, "operator*", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('d', 'l'):
		/* operator delete */
		if (!cpp_demangle_push_str(ddata, "operator delete", 15))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('d', 'v'):
		/* operator / */
		if (!cpp_demangle_push_str(ddata, "operator/", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('d', 'V'):
		/* operator /= */
		if (!cpp_demangle_push_str(ddata, "operator/=", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('e', 'o'):
		/* operator ^ */
		if (!cpp_demangle_push_str(ddata, "operator^", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('e', 'O'):
		/* operator ^= */
		if (!cpp_demangle_push_str(ddata, "operator^=", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('e', 'q'):
		/* operator == */
		if (!cpp_demangle_push_str(ddata, "operator==", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('g', 'e'):
		/* operator >= */
		if (!cpp_demangle_push_str(ddata, "operator>=", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('g', 't'):
		/* operator > */
		if (!cpp_demangle_push_str(ddata, "operator>", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('i', 'x'):
		/* operator [] */
		if (!cpp_demangle_push_str(ddata, "operator[]", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('l', 'e'):
		/* operator <= */
		if (!cpp_demangle_push_str(ddata, "operator<=", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('l', 's'):
		/* operator << */
		if (!cpp_demangle_push_str(ddata, "operator<<", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('l', 'S'):
		/* operator <<= */
		if (!cpp_demangle_push_str(ddata, "operator<<=", 11))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('l', 't'):
		/* operator < */
		if (!cpp_demangle_push_str(ddata, "operator<", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('m', 'i'):
		/* operator - */
		if (!cpp_demangle_push_str(ddata, "operator-", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('m', 'I'):
		/* operator -= */
		if (!cpp_demangle_push_str(ddata, "operator-=", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('m', 'l'):
		/* operator * */
		if (!cpp_demangle_push_str(ddata, "operator*", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('m', 'L'):
		/* operator *= */
		if (!cpp_demangle_push_str(ddata, "operator*=", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('m', 'm'):
		/* operator -- */
		if (!cpp_demangle_push_str(ddata, "operator--", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('n', 'a'):
		/* operator new[] */
		if (!cpp_demangle_push_str(ddata, "operator new []", 15))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('n', 'e'):
		/* operator != */
		if (!cpp_demangle_push_str(ddata, "operator!=", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('n', 'g'):
		/* operator - (unary) */
		if (!cpp_demangle_push_str(ddata, "operator-", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('n', 't'):
		/* operator ! */
		if (!cpp_demangle_push_str(ddata, "operator!", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('n', 'w'):
		/* operator new */
		if (!cpp_demangle_push_str(ddata, "operator new", 12))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('o', 'o'):
		/* operator || */
		if (!cpp_demangle_push_str(ddata, "operator||", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('o', 'r'):
		/* operator | */
		if (!cpp_demangle_push_str(ddata, "operator|", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('o', 'R'):
		/* operator |= */
		if (!cpp_demangle_push_str(ddata, "operator|=", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 'l'):
		/* operator + */
		if (!cpp_demangle_push_str(ddata, "operator+", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 'L'):
		/* operator += */
		if (!cpp_demangle_push_str(ddata, "operator+=", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 'm'):
		/* operator ->* */
		if (!cpp_demangle_push_str(ddata, "operator->*", 11))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 'p'):
		/* operator ++ */
		if (!cpp_demangle_push_str(ddata, "operator++", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 's'):
		/* operator + (unary) */
		if (!cpp_demangle_push_str(ddata, "operator+", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 't'):
		/* operator -> */
		if (!cpp_demangle_push_str(ddata, "operator->", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('q', 'u'):
		/* operator ? */
		if (!cpp_demangle_push_str(ddata, "operator?", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('r', 'm'):
		/* operator % */
		if (!cpp_demangle_push_str(ddata, "operator%", 9))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('r', 'M'):
		/* operator %= */
		if (!cpp_demangle_push_str(ddata, "operator%=", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('r', 's'):
		/* operator >> */
		if (!cpp_demangle_push_str(ddata, "operator>>", 10))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('r', 'S'):
		/* operator >>= */
		if (!cpp_demangle_push_str(ddata, "operator>>=", 11))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('r', 'z'):
		/* operator sizeof */
		if (!cpp_demangle_push_str(ddata, "operator sizeof ", 16))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('s', 'r'):
		/* scope resolution operator */
		if (!cpp_demangle_push_str(ddata, "scope resolution operator ",
		    26))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('s', 'v'):
		/* operator sizeof */
		if (!cpp_demangle_push_str(ddata, "operator sizeof ", 16))
			return (0);
		ddata->cur += 2;
		return (1);
	}

	/* vendor extened operator */
	if (*ddata->cur == 'v' && ELFTC_ISDIGIT(*(ddata->cur + 1))) {
		if (!cpp_demangle_push_str(ddata, "vendor extened operator ",
		    24))
			return (0);
		if (!cpp_demangle_push_str(ddata, ddata->cur + 1, 1))
			return (0);
		ddata->cur += 2;
		return (cpp_demangle_read_sname(ddata));
	}

	/* ctor-dtor-name */
	switch (SIMPLE_HASH(*ddata->cur, *(ddata->cur + 1))) {
	case SIMPLE_HASH('C', '1'):
		/* FALLTHROUGH */
	case SIMPLE_HASH('C', '2'):
		/* FALLTHROUGH */
	case SIMPLE_HASH('C', '3'):
		if (ddata->last_sname == NULL)
			return (0);
		if ((len = strlen(ddata->last_sname)) == 0)
			return (0);
		if (!cpp_demangle_push_str(ddata, "::", 2))
			return (0);
		if (!cpp_demangle_push_str(ddata, ddata->last_sname, len))
			return (0);
		ddata->cur +=2;
		return (1);

	case SIMPLE_HASH('D', '0'):
		/* FALLTHROUGH */
	case SIMPLE_HASH('D', '1'):
		/* FALLTHROUGH */
	case SIMPLE_HASH('D', '2'):
		if (ddata->last_sname == NULL)
			return (0);
		if ((len = strlen(ddata->last_sname)) == 0)
			return (0);
		if (!cpp_demangle_push_str(ddata, "::~", 3))
			return (0);
		if (!cpp_demangle_push_str(ddata, ddata->last_sname, len))
			return (0);
		ddata->cur +=2;
		return (1);
	}

	/* source name */
	if (ELFTC_ISDIGIT(*ddata->cur) != 0)
		return (cpp_demangle_read_sname(ddata));

	/* local source name */
	if (*ddata->cur == 'L')
		return (cpp_demangle_local_source_name(ddata));

	return (1);
}

/*
 * Read local source name.
 *
 * References:
 *   http://gcc.gnu.org/bugzilla/show_bug.cgi?id=31775
 *   http://gcc.gnu.org/viewcvs?view=rev&revision=124467
 */
static int
cpp_demangle_local_source_name(struct cpp_demangle_data *ddata)
{
	/* L */
	if (ddata == NULL || *ddata->cur != 'L')
		return (0);
	++ddata->cur;

	/* source name */
	if (!cpp_demangle_read_sname(ddata))
		return (0);

	/* discriminator */
	if (*ddata->cur == '_') {
		++ddata->cur;
		while (ELFTC_ISDIGIT(*ddata->cur) != 0)
			++ddata->cur;
	}

	return (1);
}

static int
cpp_demangle_read_v_offset(struct cpp_demangle_data *ddata)
{

	if (ddata == NULL)
		return (0);

	if (!cpp_demangle_push_str(ddata, "offset : ", 9))
		return (0);

	if (!cpp_demangle_read_offset_number(ddata))
		return (0);

	if (!cpp_demangle_push_str(ddata, "virtual offset : ", 17))
		return (0);

	return (!cpp_demangle_read_offset_number(ddata));
}

/*
 * Decode floating point representation to string
 * Return new allocated string or NULL
 *
 * Todo
 * Replace these functions to macro.
 */
static char *
decode_fp_to_double(const char *p, size_t len)
{
	double f;
	size_t rtn_len, limit, i;
	int byte;
	char *rtn;

	if (p == NULL || len == 0 || len % 2 != 0 || len / 2 > sizeof(double))
		return (NULL);

	memset(&f, 0, sizeof(double));

	for (i = 0; i < len / 2; ++i) {
		byte = hex_to_dec(p[len - i * 2 - 1]) +
		    hex_to_dec(p[len - i * 2 - 2]) * 16;

		if (byte < 0 || byte > 255)
			return (NULL);

#if ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN
		((unsigned char *)&f)[i] = (unsigned char)(byte);
#else /* ELFTC_BYTE_ORDER != ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
		((unsigned char *)&f)[sizeof(double) - i - 1] =
		    (unsigned char)(byte);
#endif /* ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
	}

	rtn_len = 64;
	limit = 0;
again:
	if ((rtn = malloc(sizeof(char) * rtn_len)) == NULL)
		return (NULL);

	if (snprintf(rtn, rtn_len, "%fld", f) >= (int)rtn_len) {
		free(rtn);
		if (limit++ > FLOAT_SPRINTF_TRY_LIMIT)
			return (NULL);
		rtn_len *= BUFFER_GROWFACTOR;
		goto again;
	}

	return rtn;
}

static char *
decode_fp_to_float(const char *p, size_t len)
{
	size_t i, rtn_len, limit;
	float f;
	int byte;
	char *rtn;

	if (p == NULL || len == 0 || len % 2 != 0 || len / 2 > sizeof(float))
		return (NULL);

	memset(&f, 0, sizeof(float));

	for (i = 0; i < len / 2; ++i) {
		byte = hex_to_dec(p[len - i * 2 - 1]) +
		    hex_to_dec(p[len - i * 2 - 2]) * 16;
		if (byte < 0 || byte > 255)
			return (NULL);
#if ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN
		((unsigned char *)&f)[i] = (unsigned char)(byte);
#else /* ELFTC_BYTE_ORDER != ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
		((unsigned char *)&f)[sizeof(float) - i - 1] =
		    (unsigned char)(byte);
#endif /* ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
	}

	rtn_len = 64;
	limit = 0;
again:
	if ((rtn = malloc(sizeof(char) * rtn_len)) == NULL)
		return (NULL);

	if (snprintf(rtn, rtn_len, "%ff", f) >= (int)rtn_len) {
		free(rtn);
		if (limit++ > FLOAT_SPRINTF_TRY_LIMIT)
			return (NULL);
		rtn_len *= BUFFER_GROWFACTOR;
		goto again;
	}

	return rtn;
}

static char *
decode_fp_to_float128(const char *p, size_t len)
{
	long double f;
	size_t rtn_len, limit, i;
	int byte;
	unsigned char buf[FLOAT_QUADRUPLE_BYTES];
	char *rtn;

	switch(sizeof(long double)) {
	case FLOAT_QUADRUPLE_BYTES:
		return (decode_fp_to_long_double(p, len));
	case FLOAT_EXTENED_BYTES:
		if (p == NULL || len == 0 || len % 2 != 0 ||
		    len / 2 > FLOAT_QUADRUPLE_BYTES)
			return (NULL);

		memset(buf, 0, FLOAT_QUADRUPLE_BYTES);

		for (i = 0; i < len / 2; ++i) {
			byte = hex_to_dec(p[len - i * 2 - 1]) +
			    hex_to_dec(p[len - i * 2 - 2]) * 16;
			if (byte < 0 || byte > 255)
				return (NULL);
#if ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN
			buf[i] = (unsigned char)(byte);
#else /* ELFTC_BYTE_ORDER != ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
			buf[FLOAT_QUADRUPLE_BYTES - i -1] =
			    (unsigned char)(byte);
#endif /* ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
		}
		memset(&f, 0, FLOAT_EXTENED_BYTES);

#if ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN
		memcpy(&f, buf, FLOAT_EXTENED_BYTES);
#else /* ELFTC_BYTE_ORDER != ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
		memcpy(&f, buf + 6, FLOAT_EXTENED_BYTES);
#endif /* ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN */

		rtn_len = 256;
		limit = 0;
again:
		if ((rtn = malloc(sizeof(char) * rtn_len)) == NULL)
			return (NULL);

		if (snprintf(rtn, rtn_len, "%Lfd", f) >= (int)rtn_len) {
			free(rtn);
			if (limit++ > FLOAT_SPRINTF_TRY_LIMIT)
				return (NULL);
			rtn_len *= BUFFER_GROWFACTOR;
			goto again;
		}

		return (rtn);
	default:
		return (NULL);
	}
}

static char *
decode_fp_to_float80(const char *p, size_t len)
{
	long double f;
	size_t rtn_len, limit, i;
	int byte;
	unsigned char buf[FLOAT_EXTENED_BYTES];
	char *rtn;

	switch(sizeof(long double)) {
	case FLOAT_QUADRUPLE_BYTES:
		if (p == NULL || len == 0 || len % 2 != 0 ||
		    len / 2 > FLOAT_EXTENED_BYTES)
			return (NULL);

		memset(buf, 0, FLOAT_EXTENED_BYTES);

		for (i = 0; i < len / 2; ++i) {
			byte = hex_to_dec(p[len - i * 2 - 1]) +
			    hex_to_dec(p[len - i * 2 - 2]) * 16;

			if (byte < 0 || byte > 255)
				return (NULL);

#if ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN
			buf[i] = (unsigned char)(byte);
#else /* ELFTC_BYTE_ORDER != ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
			buf[FLOAT_EXTENED_BYTES - i -1] =
			    (unsigned char)(byte);
#endif /* ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
		}

		memset(&f, 0, FLOAT_QUADRUPLE_BYTES);

#if ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN
		memcpy(&f, buf, FLOAT_EXTENED_BYTES);
#else /* ELFTC_BYTE_ORDER != ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
		memcpy((unsigned char *)(&f) + 6, buf, FLOAT_EXTENED_BYTES);
#endif /* ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN */

		rtn_len = 256;
		limit = 0;
again:
		if ((rtn = malloc(sizeof(char) * rtn_len)) == NULL)
			return (NULL);

		if (snprintf(rtn, rtn_len, "%Lfd", f) >= (int)rtn_len) {
			free(rtn);
			if (limit++ > FLOAT_SPRINTF_TRY_LIMIT)
				return (NULL);
			rtn_len *= BUFFER_GROWFACTOR;
			goto again;
		}

		return (rtn);
	case FLOAT_EXTENED_BYTES:
		return (decode_fp_to_long_double(p, len));
	default:
		return (NULL);
	}
}

static char *
decode_fp_to_long_double(const char *p, size_t len)
{
	long double f;
	size_t rtn_len, limit, i;
	int byte;
	char *rtn;

	if (p == NULL || len == 0 || len % 2 != 0 ||
	    len / 2 > sizeof(long double))
		return (NULL);

	memset(&f, 0, sizeof(long double));

	for (i = 0; i < len / 2; ++i) {
		byte = hex_to_dec(p[len - i * 2 - 1]) +
		    hex_to_dec(p[len - i * 2 - 2]) * 16;

		if (byte < 0 || byte > 255)
			return (NULL);

#if ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN
		((unsigned char *)&f)[i] = (unsigned char)(byte);
#else /* ELFTC_BYTE_ORDER != ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
		((unsigned char *)&f)[sizeof(long double) - i - 1] =
		    (unsigned char)(byte);
#endif /* ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_LITTLE_ENDIAN */
	}

	rtn_len = 256;
	limit = 0;
again:
	if ((rtn = malloc(sizeof(char) * rtn_len)) == NULL)
		return (NULL);

	if (snprintf(rtn, rtn_len, "%Lfd", f) >= (int)rtn_len) {
		free(rtn);
		if (limit++ > FLOAT_SPRINTF_TRY_LIMIT)
			return (NULL);
		rtn_len *= BUFFER_GROWFACTOR;
		goto again;
	}

	return (rtn);
}

/* Simple hex to integer function used by decode_to_* function. */
static int
hex_to_dec(char c)
{

	switch (c) {
	case '0':
		return (0);
	case '1':
		return (1);
	case '2':
		return (2);
	case '3':
		return (3);
	case '4':
		return (4);
	case '5':
		return (5);
	case '6':
		return (6);
	case '7':
		return (7);
	case '8':
		return (8);
	case '9':
		return (9);
	case 'a':
		return (10);
	case 'b':
		return (11);
	case 'c':
		return (12);
	case 'd':
		return (13);
	case 'e':
		return (14);
	case 'f':
		return (15);
	default:
		return (-1);
	}
}

static void
vector_read_cmd_dest(struct vector_read_cmd *v)
{

	if (v == NULL)
		return;

	free(v->r_container);
}

/* return -1 at failed, 0 at not found, 1 at found. */
static int
vector_read_cmd_find(struct vector_read_cmd *v, enum read_cmd dst)
{
	size_t i;

	if (v == NULL || dst == READ_FAIL)
		return (-1);

	for (i = 0; i < v->size; ++i)
		if (v->r_container[i] == dst)
			return (1);

	return (0);
}

static int
vector_read_cmd_init(struct vector_read_cmd *v)
{

	if (v == NULL)
		return (0);

	v->size = 0;
	v->capacity = VECTOR_DEF_CAPACITY;

	if ((v->r_container = malloc(sizeof(enum read_cmd) * v->capacity))
	    == NULL)
		return (0);

	return (1);
}

static int
vector_read_cmd_pop(struct vector_read_cmd *v)
{

	if (v == NULL || v->size == 0)
		return (0);

	--v->size;
	v->r_container[v->size] = READ_FAIL;

	return (1);
}

static int
vector_read_cmd_push(struct vector_read_cmd *v, enum read_cmd cmd)
{
	enum read_cmd *tmp_r_ctn;
	size_t tmp_cap;
	size_t i;

	if (v == NULL)
		return (0);

	if (v->size == v->capacity) {
		tmp_cap = v->capacity * BUFFER_GROWFACTOR;
		if ((tmp_r_ctn = malloc(sizeof(enum read_cmd) * tmp_cap))
		    == NULL)
			return (0);
		for (i = 0; i < v->size; ++i)
			tmp_r_ctn[i] = v->r_container[i];
		free(v->r_container);
		v->r_container = tmp_r_ctn;
		v->capacity = tmp_cap;
	}

	v->r_container[v->size] = cmd;
	++v->size;

	return (1);
}

static void
vector_type_qualifier_dest(struct vector_type_qualifier *v)
{

	if (v == NULL)
		return;

	free(v->q_container);
	vector_str_dest(&v->ext_name);
}

/* size, capacity, ext_name */
static int
vector_type_qualifier_init(struct vector_type_qualifier *v)
{

	if (v == NULL)
		return (0);

	v->size = 0;
	v->capacity = VECTOR_DEF_CAPACITY;

	if ((v->q_container = malloc(sizeof(enum type_qualifier) * v->capacity))
	    == NULL)
		return (0);

	assert(v->q_container != NULL);

	if (vector_str_init(&v->ext_name) == false) {
		free(v->q_container);
		return (0);
	}

	return (1);
}

static int
vector_type_qualifier_push(struct vector_type_qualifier *v,
    enum type_qualifier t)
{
	enum type_qualifier *tmp_ctn;
	size_t tmp_cap;
	size_t i;

	if (v == NULL)
		return (0);

	if (v->size == v->capacity) {
		tmp_cap = v->capacity * BUFFER_GROWFACTOR;
		if ((tmp_ctn = malloc(sizeof(enum type_qualifier) * tmp_cap))
		    == NULL)
			return (0);
		for (i = 0; i < v->size; ++i)
			tmp_ctn[i] = v->q_container[i];
		free(v->q_container);
		v->q_container = tmp_ctn;
		v->capacity = tmp_cap;
	}

	v->q_container[v->size] = t;
	++v->size;

	return (1);
}
