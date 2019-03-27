/*-
 * Copyright (c) 2007 Hyogeol Lee <hyogeollee@gmail.com>
 * Copyright (c) 2015-2017 Kai Wang <kaiwang27@gmail.com>
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

ELFTC_VCSID("$Id: libelftc_dem_gnu3.c 3583 2017-10-15 15:38:47Z emaste $");

/**
 * @file cpp_demangle.c
 * @brief Decode IA-64 C++ ABI style implementation.
 *
 * IA-64 standard ABI(Itanium C++ ABI) references.
 *
 * http://www.codesourcery.com/cxx-abi/abi.html#mangling \n
 * http://www.codesourcery.com/cxx-abi/abi-mangling.html
 */

enum type_qualifier {
	TYPE_PTR, TYPE_REF, TYPE_CMX, TYPE_IMG, TYPE_EXT, TYPE_RST, TYPE_VAT,
	TYPE_CST, TYPE_VEC, TYPE_RREF
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

struct read_cmd_item {
	enum read_cmd cmd;
	void *data;
};

struct vector_read_cmd {
	size_t size, capacity;
	struct read_cmd_item *r_container;
};

enum push_qualifier {
	PUSH_ALL_QUALIFIER,
	PUSH_CV_QUALIFIER,
	PUSH_NON_CV_QUALIFIER,
};

struct cpp_demangle_data {
	struct vector_str	 output;	/* output string vector */
	struct vector_str	 subst;		/* substitution string vector */
	struct vector_str	 tmpl;
	struct vector_str	 class_type;
	struct vector_str	*cur_output;	/* ptr to current output vec */
	struct vector_read_cmd	 cmd;
	bool			 mem_rst;	/* restrict member function */
	bool			 mem_vat;	/* volatile member function */
	bool			 mem_cst;	/* const member function */
	bool			 mem_ref;	/* lvalue-ref member func */
	bool			 mem_rref;	/* rvalue-ref member func */
	bool			 is_tmpl;	/* template args */
	bool			 is_functype;	/* function type */
	bool			 ref_qualifier; /* ref qualifier */
	enum type_qualifier	 ref_qualifier_type; /* ref qualifier type */
	enum push_qualifier	 push_qualifier; /* which qualifiers to push */
	int			 func_type;
	const char		*cur;		/* current mangled name ptr */
	const char		*last_sname;	/* last source name */
};

struct type_delimit {
	bool paren;
	bool firstp;
};

#define	CPP_DEMANGLE_TRY_LIMIT	128
#define	FLOAT_SPRINTF_TRY_LIMIT	5
#define	FLOAT_QUADRUPLE_BYTES	16
#define	FLOAT_EXTENED_BYTES	10

#define SIMPLE_HASH(x,y)	(64 * x + y)
#define DEM_PUSH_STR(d,s)	cpp_demangle_push_str((d), (s), strlen((s)))
#define VEC_PUSH_STR(d,s)	vector_str_push((d), (s), strlen((s)))

static void	cpp_demangle_data_dest(struct cpp_demangle_data *);
static int	cpp_demangle_data_init(struct cpp_demangle_data *,
		    const char *);
static int	cpp_demangle_get_subst(struct cpp_demangle_data *, size_t);
static int	cpp_demangle_get_tmpl_param(struct cpp_demangle_data *, size_t);
static int	cpp_demangle_push_fp(struct cpp_demangle_data *,
		    char *(*)(const char *, size_t));
static int	cpp_demangle_push_str(struct cpp_demangle_data *, const char *,
		    size_t);
static int	cpp_demangle_pop_str(struct cpp_demangle_data *);
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
static int	cpp_demangle_read_pointer_to_member(struct cpp_demangle_data *,
		    struct vector_type_qualifier *);
static int	cpp_demangle_read_sname(struct cpp_demangle_data *);
static int	cpp_demangle_read_subst(struct cpp_demangle_data *);
static int	cpp_demangle_read_subst_std(struct cpp_demangle_data *);
static int	cpp_demangle_read_subst_stdtmpl(struct cpp_demangle_data *,
		    const char *);
static int	cpp_demangle_read_tmpl_arg(struct cpp_demangle_data *);
static int	cpp_demangle_read_tmpl_args(struct cpp_demangle_data *);
static int	cpp_demangle_read_tmpl_param(struct cpp_demangle_data *);
static int	cpp_demangle_read_type(struct cpp_demangle_data *,
		    struct type_delimit *);
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
static struct read_cmd_item *vector_read_cmd_find(struct vector_read_cmd *,
		    enum read_cmd);
static int	vector_read_cmd_init(struct vector_read_cmd *);
static int	vector_read_cmd_pop(struct vector_read_cmd *);
static int	vector_read_cmd_push(struct vector_read_cmd *, enum read_cmd,
		    void *);
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
cpp_demangle_gnu3(const char *org)
{
	struct cpp_demangle_data ddata;
	struct vector_str ret_type;
	struct type_delimit td;
	ssize_t org_len;
	unsigned int limit;
	char *rtn;
	bool has_ret, more_type;

	if (org == NULL || (org_len = strlen(org)) < 2)
		return (NULL);

	if (org_len > 11 && !strncmp(org, "_GLOBAL__I_", 11)) {
		if ((rtn = malloc(org_len + 19)) == NULL)
			return (NULL);
		snprintf(rtn, org_len + 19,
		    "global constructors keyed to %s", org + 11);
		return (rtn);
	}

	if (org[0] != '_' || org[1] != 'Z')
		return (NULL);

	if (!cpp_demangle_data_init(&ddata, org + 2))
		return (NULL);

	rtn = NULL;
	has_ret = more_type = false;

	if (!cpp_demangle_read_encoding(&ddata))
		goto clean;

	/*
	 * Pop function name from substitution candidate list.
	 */
	if (*ddata.cur != 0 && ddata.subst.size >= 1) {
		if (!vector_str_pop(&ddata.subst))
			goto clean;
	}

	td.paren = false;
	td.firstp = true;
	limit = 0;

	/*
	 * The first type is a return type if we just demangled template
	 * args. (the template args is right next to the function name,
	 * which means it's a template function)
	 */
	if (ddata.is_tmpl) {
		ddata.is_tmpl = false;
		if (!vector_str_init(&ret_type))
			goto clean;
		ddata.cur_output = &ret_type;
		has_ret = true;
	}

	while (*ddata.cur != '\0') {
		/*
		 * Breaking at some gcc info at tail. e.g) @@GLIBCXX_3.4
		 */
		if (*ddata.cur == '@' && *(ddata.cur + 1) == '@')
			break;

		if (has_ret) {
			/* Read return type */
			if (!cpp_demangle_read_type(&ddata, NULL))
				goto clean;
		} else {
			/* Read function arg type */
			if (!cpp_demangle_read_type(&ddata, &td))
				goto clean;
		}

		if (has_ret) {
			/* Push return type to the beginning */
			if (!VEC_PUSH_STR(&ret_type, " "))
				goto clean;
			if (!vector_str_push_vector_head(&ddata.output,
			    &ret_type))
				goto clean;
			ddata.cur_output = &ddata.output;
			vector_str_dest(&ret_type);
			has_ret = false;
			more_type = true;
		} else if (more_type)
			more_type = false;
		if (limit++ > CPP_DEMANGLE_TRY_LIMIT)
			goto clean;
	}
	if (more_type)
		goto clean;

	if (ddata.output.size == 0)
		goto clean;
	if (td.paren && !VEC_PUSH_STR(&ddata.output, ")"))
		goto clean;
	if (ddata.mem_vat && !VEC_PUSH_STR(&ddata.output, " volatile"))
		goto clean;
	if (ddata.mem_cst && !VEC_PUSH_STR(&ddata.output, " const"))
		goto clean;
	if (ddata.mem_rst && !VEC_PUSH_STR(&ddata.output, " restrict"))
		goto clean;
	if (ddata.mem_ref && !VEC_PUSH_STR(&ddata.output, " &"))
		goto clean;
	if (ddata.mem_rref && !VEC_PUSH_STR(&ddata.output, " &&"))
		goto clean;

	rtn = vector_str_get_flat(&ddata.output, (size_t *) NULL);

clean:
	if (has_ret)
		vector_str_dest(&ret_type);

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
	vector_str_dest(&d->output);
}

static int
cpp_demangle_data_init(struct cpp_demangle_data *d, const char *cur)
{

	if (d == NULL || cur == NULL)
		return (0);

	if (!vector_str_init(&d->output))
		return (0);
	if (!vector_str_init(&d->subst))
		goto clean1;
	if (!vector_str_init(&d->tmpl))
		goto clean2;
	if (!vector_str_init(&d->class_type))
		goto clean3;
	if (!vector_read_cmd_init(&d->cmd))
		goto clean4;

	assert(d->output.container != NULL);
	assert(d->subst.container != NULL);
	assert(d->tmpl.container != NULL);
	assert(d->class_type.container != NULL);

	d->mem_rst = false;
	d->mem_vat = false;
	d->mem_cst = false;
	d->mem_ref = false;
	d->mem_rref = false;
	d->is_tmpl = false;
	d->is_functype = false;
	d->ref_qualifier = false;
	d->push_qualifier = PUSH_ALL_QUALIFIER;
	d->func_type = 0;
	d->cur = cur;
	d->cur_output = &d->output;
	d->last_sname = NULL;

	return (1);

clean4:
	vector_str_dest(&d->class_type);
clean3:
	vector_str_dest(&d->tmpl);
clean2:
	vector_str_dest(&d->subst);
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

	/*
	 * is_tmpl is used to check if the type (function arg) is right next
	 * to template args, and should always be cleared whenever new string
	 * pushed.
	 */
	ddata->is_tmpl = false;

	return (vector_str_push(ddata->cur_output, str, len));
}

static int
cpp_demangle_pop_str(struct cpp_demangle_data *ddata)
{

	if (ddata == NULL)
		return (0);

	return (vector_str_pop(ddata->cur_output));
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
	enum type_qualifier t;
	size_t idx, e_idx, e_len;
	char *buf;
	int rtn;
	bool cv;

	if (ddata == NULL || v == NULL)
		return (0);

	if ((idx = v->size) == 0)
		return (1);

	rtn = 0;
	if (type_str != NULL) {
		if (!vector_str_init(&subst_v))
			return (0);
		if (!VEC_PUSH_STR(&subst_v, type_str))
			goto clean;
	}

	cv = true;
	e_idx = 0;
	while (idx > 0) {
		switch (v->q_container[idx - 1]) {
		case TYPE_PTR:
			cv = false;
			if (ddata->push_qualifier == PUSH_CV_QUALIFIER)
				break;
			if (!DEM_PUSH_STR(ddata, "*"))
				goto clean;
			if (type_str != NULL) {
				if (!VEC_PUSH_STR(&subst_v, "*"))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_REF:
			cv = false;
			if (ddata->push_qualifier == PUSH_CV_QUALIFIER)
				break;
			if (!DEM_PUSH_STR(ddata, "&"))
				goto clean;
			if (type_str != NULL) {
				if (!VEC_PUSH_STR(&subst_v, "&"))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_RREF:
			cv = false;
			if (ddata->push_qualifier == PUSH_CV_QUALIFIER)
				break;
			if (!DEM_PUSH_STR(ddata, "&&"))
				goto clean;
			if (type_str != NULL) {
				if (!VEC_PUSH_STR(&subst_v, "&&"))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_CMX:
			cv = false;
			if (ddata->push_qualifier == PUSH_CV_QUALIFIER)
				break;
			if (!DEM_PUSH_STR(ddata, " complex"))
				goto clean;
			if (type_str != NULL) {
				if (!VEC_PUSH_STR(&subst_v, " complex"))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_IMG:
			cv = false;
			if (ddata->push_qualifier == PUSH_CV_QUALIFIER)
				break;
			if (!DEM_PUSH_STR(ddata, " imaginary"))
				goto clean;
			if (type_str != NULL) {
				if (!VEC_PUSH_STR(&subst_v, " imaginary"))
					goto clean;
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_EXT:
			cv = false;
			if (ddata->push_qualifier == PUSH_CV_QUALIFIER)
				break;
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

			if (!DEM_PUSH_STR(ddata, buf)) {
				free(buf);
				goto clean;
			}

			if (type_str != NULL) {
				if (!VEC_PUSH_STR(&subst_v, buf)) {
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
			if (ddata->push_qualifier == PUSH_NON_CV_QUALIFIER &&
			    cv)
				break;
			if (ddata->push_qualifier == PUSH_CV_QUALIFIER && !cv)
				break;
			if (!DEM_PUSH_STR(ddata, " restrict"))
				goto clean;
			if (type_str != NULL) {
				if (!VEC_PUSH_STR(&subst_v, " restrict"))
					goto clean;
				if (idx - 1 > 0) {
					t = v->q_container[idx - 2];
					if (t == TYPE_RST || t == TYPE_VAT ||
					    t == TYPE_CST)
						break;
				}
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_VAT:
			if (ddata->push_qualifier == PUSH_NON_CV_QUALIFIER &&
			    cv)
				break;
			if (ddata->push_qualifier == PUSH_CV_QUALIFIER && !cv)
				break;
			if (!DEM_PUSH_STR(ddata, " volatile"))
				goto clean;
			if (type_str != NULL) {
				if (!VEC_PUSH_STR(&subst_v, " volatile"))
					goto clean;
				if (idx - 1 > 0) {
					t = v->q_container[idx - 2];
					if (t == TYPE_RST || t == TYPE_VAT ||
					    t == TYPE_CST)
						break;
				}
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_CST:
			if (ddata->push_qualifier == PUSH_NON_CV_QUALIFIER &&
			    cv)
				break;
			if (ddata->push_qualifier == PUSH_CV_QUALIFIER && !cv)
				break;
			if (!DEM_PUSH_STR(ddata, " const"))
				goto clean;
			if (type_str != NULL) {
				if (!VEC_PUSH_STR(&subst_v, " const"))
					goto clean;
				if (idx - 1 > 0) {
					t = v->q_container[idx - 2];
					if (t == TYPE_RST || t == TYPE_VAT ||
					    t == TYPE_CST)
						break;
				}
				if (!cpp_demangle_push_subst_v(ddata,
				    &subst_v))
					goto clean;
			}
			break;

		case TYPE_VEC:
			cv = false;
			if (ddata->push_qualifier == PUSH_CV_QUALIFIER)
				break;
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
			if (!DEM_PUSH_STR(ddata, buf)) {
				free(buf);
				goto clean;
			}
			if (type_str != NULL) {
				if (!VEC_PUSH_STR(&subst_v, buf)) {
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

		if (!cpp_demangle_read_type(ddata, NULL))
			return (0);

		if (!DEM_PUSH_STR(ddata, "[]"))
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
			if (!cpp_demangle_read_type(ddata, NULL))
				return (0);
			if (!DEM_PUSH_STR(ddata, "["))
				return (0);
			if (!cpp_demangle_push_str(ddata, num, num_len))
				return (0);
			if (!DEM_PUSH_STR(ddata, "]"))
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
			if (!cpp_demangle_read_type(ddata, NULL)) {
				free(exp);
				return (0);
			}
			if (!DEM_PUSH_STR(ddata, "[")) {
				free(exp);
				return (0);
			}
			if (!cpp_demangle_push_str(ddata, exp, exp_len)) {
				free(exp);
				return (0);
			}
			if (!DEM_PUSH_STR(ddata, "]")) {
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
			return (DEM_PUSH_STR(ddata, "false"));
		case '1':
			ddata->cur += 2;
			return (DEM_PUSH_STR(ddata, "true"));
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
			if (!DEM_PUSH_STR(ddata, "-"))
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
		return (cpp_demangle_read_type(ddata, NULL));

	case SIMPLE_HASH('s', 'r'):
		ddata->cur += 2;
		if (!cpp_demangle_read_type(ddata, NULL))
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

	output = &ddata->output;

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
	struct type_delimit td;
	struct read_cmd_item *rc;
	size_t class_type_size, class_type_len, limit;
	const char *class_type;
	int i;
	bool paren, non_cv_qualifier;

	if (ddata == NULL || *ddata->cur != 'F' || v == NULL)
		return (0);

	++ddata->cur;
	if (*ddata->cur == 'Y') {
		if (ext_c != NULL)
			*ext_c = 1;
		++ddata->cur;
	}

	/* Return type */
	if (!cpp_demangle_read_type(ddata, NULL))
		return (0);

	if (*ddata->cur != 'E') {
		if (!DEM_PUSH_STR(ddata, " "))
			return (0);

		non_cv_qualifier = false;
		if (v->size > 0) {
			for (i = 0; (size_t) i < v->size; i++) {
				if (v->q_container[i] != TYPE_RST &&
				    v->q_container[i] != TYPE_VAT &&
				    v->q_container[i] != TYPE_CST) {
					non_cv_qualifier = true;
					break;
				}
			}
		}

		paren = false;
		rc = vector_read_cmd_find(&ddata->cmd, READ_PTRMEM);
		if (non_cv_qualifier || rc != NULL) {
			if (!DEM_PUSH_STR(ddata, "("))
				return (0);
			paren = true;
		}

		/* Push non-cv qualifiers. */
		ddata->push_qualifier = PUSH_NON_CV_QUALIFIER;
		if (!cpp_demangle_push_type_qualifier(ddata, v, NULL))
			return (0);

		if (rc) {
			if (non_cv_qualifier && !DEM_PUSH_STR(ddata, " "))
				return (0);
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
			if (!DEM_PUSH_STR(ddata, "::*"))
				return (0);
			/* Push pointer-to-member qualifiers. */
			ddata->push_qualifier = PUSH_ALL_QUALIFIER;
			if (!cpp_demangle_push_type_qualifier(ddata, rc->data,
			    NULL))
				return (0);
			++ddata->func_type;
		}

		if (paren) {
			if (!DEM_PUSH_STR(ddata, ")"))
				return (0);
			paren = false;
		}

		td.paren = false;
		td.firstp = true;
		limit = 0;
		ddata->is_functype = true;
		for (;;) {
			if (!cpp_demangle_read_type(ddata, &td))
				return (0);
			if (*ddata->cur == 'E')
				break;
			if (limit++ > CPP_DEMANGLE_TRY_LIMIT)
				return (0);
		}
		ddata->is_functype = false;
		if (td.paren) {
			if (!DEM_PUSH_STR(ddata, ")"))
				return (0);
			td.paren = false;
		}

		/* Push CV qualifiers. */
		ddata->push_qualifier = PUSH_CV_QUALIFIER;
		if (!cpp_demangle_push_type_qualifier(ddata, v, NULL))
			return (0);

		ddata->push_qualifier = PUSH_ALL_QUALIFIER;

		/* Release type qualifier vector. */
		vector_type_qualifier_dest(v);
		if (!vector_type_qualifier_init(v))
			return (0);

		/* Push ref-qualifiers. */
		if (ddata->ref_qualifier) {
			switch (ddata->ref_qualifier_type) {
			case TYPE_REF:
				if (!DEM_PUSH_STR(ddata, " &"))
					return (0);
				break;
			case TYPE_RREF:
				if (!DEM_PUSH_STR(ddata, " &&"))
					return (0);
				break;
			default:
				return (0);
			}
			ddata->ref_qualifier = false;
		}
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
		if (!DEM_PUSH_STR(ddata, "hidden alias for "))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_encoding(ddata));

	case SIMPLE_HASH('G', 'R'):
		if (!DEM_PUSH_STR(ddata, "reference temporary #"))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		if (!cpp_demangle_read_name_flat(ddata, &name))
			return (0);
		rtn = 0;
		if (!cpp_demangle_read_number_as_string(ddata, &num_str))
			goto clean1;
		if (!DEM_PUSH_STR(ddata, num_str))
			goto clean2;
		if (!DEM_PUSH_STR(ddata, " for "))
			goto clean2;
		if (!DEM_PUSH_STR(ddata, name))
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
			if (!DEM_PUSH_STR(ddata, "non-transaction clone for "))
				return (0);
			break;
		case 't':
		default:
			if (!DEM_PUSH_STR(ddata, "transaction clone for "))
				return (0);
			break;
		}
		++ddata->cur;
		return (cpp_demangle_read_encoding(ddata));

	case SIMPLE_HASH('G', 'V'):
		/* sentry object for 1 time init */
		if (!DEM_PUSH_STR(ddata, "guard variable for "))
			return (0);
		ddata->cur += 2;
		break;

	case SIMPLE_HASH('T', 'c'):
		/* virtual function covariant override thunk */
		if (!DEM_PUSH_STR(ddata,
		    "virtual function covariant override "))
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
		if (!DEM_PUSH_STR(ddata, "construction vtable for "))
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
		if (!cpp_demangle_read_type(ddata, NULL))
			goto clean3;
		if (!DEM_PUSH_STR(ddata, "-in-"))
			goto clean3;
		if (!DEM_PUSH_STR(ddata, type))
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
		if (!DEM_PUSH_STR(ddata, "typeinfo fn for "))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, NULL));

	case SIMPLE_HASH('T', 'h'):
		/* virtual function non-virtual override thunk */
		if (!DEM_PUSH_STR(ddata,
		    "virtual function non-virtual override "))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		if (!cpp_demangle_read_nv_offset(ddata))
			return (0);
		return (cpp_demangle_read_encoding(ddata));

	case SIMPLE_HASH('T', 'H'):
		/* TLS init function */
		if (!DEM_PUSH_STR(ddata, "TLS init function for "))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		break;

	case SIMPLE_HASH('T', 'I'):
		/* typeinfo structure */
		if (!DEM_PUSH_STR(ddata, "typeinfo for "))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, NULL));

	case SIMPLE_HASH('T', 'J'):
		/* java class */
		if (!DEM_PUSH_STR(ddata, "java Class for "))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, NULL));

	case SIMPLE_HASH('T', 'S'):
		/* RTTI name (NTBS) */
		if (!DEM_PUSH_STR(ddata, "typeinfo name for "))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, NULL));

	case SIMPLE_HASH('T', 'T'):
		/* VTT table */
		if (!DEM_PUSH_STR(ddata, "VTT for "))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, NULL));

	case SIMPLE_HASH('T', 'v'):
		/* virtual function virtual override thunk */
		if (!DEM_PUSH_STR(ddata, "virtual function virtual override "))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		if (!cpp_demangle_read_v_offset(ddata))
			return (0);
		return (cpp_demangle_read_encoding(ddata));

	case SIMPLE_HASH('T', 'V'):
		/* virtual table */
		if (!DEM_PUSH_STR(ddata, "vtable for "))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == '\0')
			return (0);
		return (cpp_demangle_read_type(ddata, NULL));

	case SIMPLE_HASH('T', 'W'):
		/* TLS wrapper function */
		if (!DEM_PUSH_STR(ddata, "TLS wrapper function for "))
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
	struct vector_str local_name;
	struct type_delimit td;
	size_t limit;
	bool  more_type;

	if (ddata == NULL)
		return (0);
	if (*(++ddata->cur) == '\0')
		return (0);

	vector_str_init(&local_name);
	ddata->cur_output = &local_name;

	if (!cpp_demangle_read_encoding(ddata)) {
		vector_str_dest(&local_name);
		return (0);
	}

	ddata->cur_output = &ddata->output;

	td.paren = false;
	td.firstp = true;
	more_type = false;
	limit = 0;

	/*
	 * The first type is a return type if we just demangled template
	 * args. (the template args is right next to the function name,
	 * which means it's a template function)
	 */
	if (ddata->is_tmpl) {
		ddata->is_tmpl = false;

		/* Read return type */
		if (!cpp_demangle_read_type(ddata, NULL)) {
			vector_str_dest(&local_name);
			return (0);
		}

		more_type = true;
	}

	/* Now we can push the name after possible return type is handled. */
	if (!vector_str_push_vector(&ddata->output, &local_name)) {
		vector_str_dest(&local_name);
		return (0);
	}
	vector_str_dest(&local_name);

	while (*ddata->cur != '\0') {
		if (!cpp_demangle_read_type(ddata, &td))
			return (0);
		if (more_type)
			more_type = false;
		if (*ddata->cur == 'E')
			break;
		if (limit++ > CPP_DEMANGLE_TRY_LIMIT)
			return (0);
	}
	if (more_type)
		return (0);

	if (*(++ddata->cur) == '\0')
		return (0);
	if (td.paren == true) {
		if (!DEM_PUSH_STR(ddata, ")"))
			return (0);
		td.paren = false;
	}
	if (*ddata->cur == 's')
		++ddata->cur;
	else {
		if (!DEM_PUSH_STR(ddata, "::"))
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

	output = ddata->cur_output;

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

	output = ddata->cur_output;

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

	do {
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
		case 'R':
			ddata->mem_ref = true;
			break;
		case 'O':
			ddata->mem_rref = true;
			break;
		default:
			goto next;
		}
	} while (*(++ddata->cur));

next:
	output = ddata->cur_output;
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

		if (p_idx == output->size)
			goto next_comp;
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

	next_comp:
		if (*ddata->cur == 'E')
			break;
		else if (*ddata->cur != 'I' && *ddata->cur != 'C' &&
		    *ddata->cur != 'D' && p_idx != output->size) {
			if (!DEM_PUSH_STR(ddata, "::"))
				goto clean;
			if (!VEC_PUSH_STR(&v, "::"))
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

	if (!DEM_PUSH_STR(ddata, "offset : "))
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

	if (negative && !DEM_PUSH_STR(ddata, "-"))
		return (0);

	assert(start != NULL);

	if (!cpp_demangle_push_str(ddata, start, ddata->cur - start))
		return (0);
	if (!DEM_PUSH_STR(ddata, " "))
		return (0);

	++ddata->cur;

	return (1);
}

static int
cpp_demangle_read_pointer_to_member(struct cpp_demangle_data *ddata,
    struct vector_type_qualifier *v)
{
	size_t class_type_len, i, idx, p_idx;
	int p_func_type, rtn;
	char *class_type;

	if (ddata == NULL || *ddata->cur != 'M' || *(++ddata->cur) == '\0')
		return (0);

	p_idx = ddata->output.size;
	if (!cpp_demangle_read_type(ddata, NULL))
		return (0);

	if ((class_type = vector_str_substr(&ddata->output, p_idx,
	    ddata->output.size - 1, &class_type_len)) == NULL)
		return (0);

	rtn = 0;
	idx = ddata->output.size;
	for (i = p_idx; i < idx; ++i)
		if (!vector_str_pop(&ddata->output))
			goto clean1;

	if (!vector_read_cmd_push(&ddata->cmd, READ_PTRMEM, v))
		goto clean1;

	if (!vector_str_push(&ddata->class_type, class_type, class_type_len))
		goto clean2;

	p_func_type = ddata->func_type;
	if (!cpp_demangle_read_type(ddata, NULL))
		goto clean3;

	if (p_func_type == ddata->func_type) {
		if (!DEM_PUSH_STR(ddata, " "))
			goto clean3;
		if (!cpp_demangle_push_str(ddata, class_type, class_type_len))
			goto clean3;
		if (!DEM_PUSH_STR(ddata, "::*"))
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

	vector_type_qualifier_dest(v);
	if (!vector_type_qualifier_init(v))
		return (0);

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
		err = DEM_PUSH_STR(ddata, "(anonymous namespace)");
	else
		err = cpp_demangle_push_str(ddata, ddata->cur, len);

	if (err == 0)
		return (0);

	assert(ddata->output.size > 0);
	if (vector_read_cmd_find(&ddata->cmd, READ_TMPL) == NULL)
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
		if (!DEM_PUSH_STR(ddata, "std::allocator"))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::allocator"));
		return (1);

	case SIMPLE_HASH('S', 'b'):
		/* std::basic_string */
		if (!DEM_PUSH_STR(ddata, "std::basic_string"))
			return (0);
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::basic_string"));
		return (1);

	case SIMPLE_HASH('S', 'd'):
		/* std::basic_iostream<char, std::char_traits<char> > */
		if (!DEM_PUSH_STR(ddata, "std::basic_iostream<char, "
		    "std::char_traits<char> >"))
			return (0);
		ddata->last_sname = "basic_iostream";
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::basic_iostream<char, std::char_traits"
				"<char> >"));
		return (1);

	case SIMPLE_HASH('S', 'i'):
		/* std::basic_istream<char, std::char_traits<char> > */
		if (!DEM_PUSH_STR(ddata, "std::basic_istream<char, "
		    "std::char_traits<char> >"))
			return (0);
		ddata->last_sname = "basic_istream";
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::basic_istream<char, std::char_traits"
				"<char> >"));
		return (1);

	case SIMPLE_HASH('S', 'o'):
		/* std::basic_ostream<char, std::char_traits<char> > */
		if (!DEM_PUSH_STR(ddata, "std::basic_ostream<char, "
		    "std::char_traits<char> >"))
			return (0);
		ddata->last_sname = "basic_ostream";
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::basic_ostream<char, std::char_traits"
				"<char> >"));
		return (1);

	case SIMPLE_HASH('S', 's'):
		/*
		 * std::basic_string<char, std::char_traits<char>,
		 * std::allocator<char> >
		 *
		 * a.k.a std::string
		 */
		if (!DEM_PUSH_STR(ddata, "std::basic_string<char, "
		    "std::char_traits<char>, std::allocator<char> >"))
			return (0);
		ddata->last_sname = "string";
		ddata->cur += 2;
		if (*ddata->cur == 'I')
			return (cpp_demangle_read_subst_stdtmpl(ddata,
			    "std::basic_string<char, std::char_traits<char>,"
				" std::allocator<char> >"));
		return (1);

	case SIMPLE_HASH('S', 't'):
		/* std:: */
		return (cpp_demangle_read_subst_std(ddata));
	}

	if (*(++ddata->cur) == '\0')
		return (0);

	/* Skip unknown substitution abbreviations. */
	if (!(*ddata->cur >= '0' && *ddata->cur <= '9') &&
	    !(*ddata->cur >= 'A' && *ddata->cur <= 'Z') &&
	    *ddata->cur != '_') {
		++ddata->cur;
		return (1);
	}

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
	if (!DEM_PUSH_STR(ddata, "std::"))
		goto clean;

	if (!VEC_PUSH_STR(&v, "std::"))
		goto clean;

	ddata->cur += 2;

	output = ddata->cur_output;

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
    const char *str)
{
	struct vector_str *output;
	size_t p_idx, substr_len, len;
	int rtn;
	char *subst_str, *substr;

	if (ddata == NULL || str == NULL)
		return (0);

	if ((len = strlen(str)) == 0)
		return (0);

	output = ddata->cur_output;

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
		++ddata->cur;
		if (!cpp_demangle_read_expression(ddata))
			return (0);
		return (*ddata->cur++ == 'E');
	}

	return (cpp_demangle_read_type(ddata, NULL));
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

	if (!vector_read_cmd_push(&ddata->cmd, READ_TMPL, NULL))
		return (0);

	if (!DEM_PUSH_STR(ddata, "<"))
		return (0);

	limit = 0;
	v = &ddata->output;
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
				if (!DEM_PUSH_STR(ddata, " >"))
					return (0);
			} else if (!DEM_PUSH_STR(ddata, ">"))
				return (0);
			ddata->is_tmpl = true;
			break;
		} else if (*ddata->cur != 'I' &&
		    !DEM_PUSH_STR(ddata, ", "))
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
cpp_demangle_read_type(struct cpp_demangle_data *ddata,
    struct type_delimit *td)
{
	struct vector_type_qualifier v;
	struct vector_str *output, sv;
	size_t p_idx, type_str_len, subst_str_len;
	int extern_c, is_builtin;
	long len;
	const char *p;
	char *type_str, *exp_str, *num_str, *subst_str;
	bool skip_ref_qualifier, omit_void;

	if (ddata == NULL)
		return (0);

	output = ddata->cur_output;
	if (td) {
		if (td->paren == false) {
			if (!DEM_PUSH_STR(ddata, "("))
				return (0);
			if (ddata->output.size < 2)
				return (0);
			td->paren = true;
		}

		if (!td->firstp) {
			if (*ddata->cur != 'I') {
				if (!DEM_PUSH_STR(ddata, ", "))
					return (0);
			}
		}
	}

	assert(output != NULL);
	/*
	 * [r, V, K] [P, R, O, C, G, U] builtin, function, class-enum, array
	 * pointer-to-member, template-param, template-template-param, subst
	 */

	if (!vector_type_qualifier_init(&v))
		return (0);

	extern_c = 0;
	is_builtin = 1;
	p_idx = output->size;
	type_str = exp_str = num_str = NULL;
	skip_ref_qualifier = false;

again:

	/* Clear ref-qualifier flag */
	if (*ddata->cur != 'R' && *ddata->cur != 'O' && *ddata->cur != 'E')
		ddata->ref_qualifier = false;

	/* builtin type */
	switch (*ddata->cur) {
	case 'a':
		/* signed char */
		if (!DEM_PUSH_STR(ddata, "signed char"))
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
		if (!DEM_PUSH_STR(ddata, "bool"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'C':
		/* complex pair */
		if (!vector_type_qualifier_push(&v, TYPE_CMX))
			goto clean;
		++ddata->cur;
		if (td)
			td->firstp = false;
		goto again;

	case 'c':
		/* char */
		if (!DEM_PUSH_STR(ddata, "char"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'd':
		/* double */
		if (!DEM_PUSH_STR(ddata, "double"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'D':
		++ddata->cur;
		switch (*ddata->cur) {
		case 'a':
			/* auto */
			if (!DEM_PUSH_STR(ddata, "auto"))
				goto clean;
			++ddata->cur;
			break;
		case 'c':
			/* decltype(auto) */
			if (!DEM_PUSH_STR(ddata, "decltype(auto)"))
				goto clean;
			++ddata->cur;
			break;
		case 'd':
			/* IEEE 754r decimal floating point (64 bits) */
			if (!DEM_PUSH_STR(ddata, "decimal64"))
				goto clean;
			++ddata->cur;
			break;
		case 'e':
			/* IEEE 754r decimal floating point (128 bits) */
			if (!DEM_PUSH_STR(ddata, "decimal128"))
				goto clean;
			++ddata->cur;
			break;
		case 'f':
			/* IEEE 754r decimal floating point (32 bits) */
			if (!DEM_PUSH_STR(ddata, "decimal32"))
				goto clean;
			++ddata->cur;
			break;
		case 'h':
			/* IEEE 754r half-precision floating point (16 bits) */
			if (!DEM_PUSH_STR(ddata, "half"))
				goto clean;
			++ddata->cur;
			break;
		case 'i':
			/* char32_t */
			if (!DEM_PUSH_STR(ddata, "char32_t"))
				goto clean;
			++ddata->cur;
			break;
		case 'n':
			/* std::nullptr_t (i.e., decltype(nullptr)) */
			if (!DEM_PUSH_STR(ddata, "decltype(nullptr)"))
				goto clean;
			++ddata->cur;
			break;
		case 's':
			/* char16_t */
			if (!DEM_PUSH_STR(ddata, "char16_t"))
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
				if (!VEC_PUSH_STR(&v.ext_name, exp_str))
					goto clean;
			} else {
				if (!cpp_demangle_read_number_as_string(ddata,
				    &num_str))
					goto clean;
				if (!VEC_PUSH_STR(&v.ext_name, num_str))
					goto clean;
			}
			if (*ddata->cur != '_')
				goto clean;
			++ddata->cur;
			if (!vector_type_qualifier_push(&v, TYPE_VEC))
				goto clean;
			if (td)
				td->firstp = false;
			goto again;
		default:
			goto clean;
		}
		goto rtn;

	case 'e':
		/* long double */
		if (!DEM_PUSH_STR(ddata, "long double"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'E':
		/* unexpected end except ref-qualifiers */
		if (ddata->ref_qualifier && ddata->is_functype) {
			skip_ref_qualifier = true;
			/* Pop the delimiter. */
			cpp_demangle_pop_str(ddata);
			goto rtn;
		}
		goto clean;

	case 'f':
		/* float */
		if (!DEM_PUSH_STR(ddata, "float"))
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
		if (!DEM_PUSH_STR(ddata, "__float128"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'G':
		/* imaginary */
		if (!vector_type_qualifier_push(&v, TYPE_IMG))
			goto clean;
		++ddata->cur;
		if (td)
			td->firstp = false;
		goto again;

	case 'h':
		/* unsigned char */
		if (!DEM_PUSH_STR(ddata, "unsigned char"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'i':
		/* int */
		if (!DEM_PUSH_STR(ddata, "int"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'I':
		/* template args. */
		/* handles <substitute><template-args> */
		p_idx = output->size;
		if (!cpp_demangle_read_tmpl_args(ddata))
			goto clean;
		if ((subst_str = vector_str_substr(output, p_idx,
		    output->size - 1, &subst_str_len)) == NULL)
			goto clean;
		if (!vector_str_init(&sv)) {
			free(subst_str);
			goto clean;
		}
		if (!vector_str_push(&sv, subst_str, subst_str_len)) {
			free(subst_str);
			vector_str_dest(&sv);
			goto clean;
		}
		free(subst_str);
		if (!cpp_demangle_push_subst_v(ddata, &sv)) {
			vector_str_dest(&sv);
			goto clean;
		}
		vector_str_dest(&sv);
		goto rtn;

	case 'j':
		/* unsigned int */
		if (!DEM_PUSH_STR(ddata, "unsigned int"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'K':
		/* const */
		if (!vector_type_qualifier_push(&v, TYPE_CST))
			goto clean;
		++ddata->cur;
		if (td)
			td->firstp = false;
		goto again;

	case 'l':
		/* long */
		if (!DEM_PUSH_STR(ddata, "long"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'm':
		/* unsigned long */
		if (!DEM_PUSH_STR(ddata, "unsigned long"))
			goto clean;

		++ddata->cur;

		goto rtn;
	case 'M':
		/* pointer to member */
		if (!cpp_demangle_read_pointer_to_member(ddata, &v))
			goto clean;
		is_builtin = 0;
		goto rtn;

	case 'n':
		/* __int128 */
		if (!DEM_PUSH_STR(ddata, "__int128"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'o':
		/* unsigned __int128 */
		if (!DEM_PUSH_STR(ddata, "unsigned __int128"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'O':
		/* rvalue reference */
		if (ddata->ref_qualifier)
			goto clean;
		if (!vector_type_qualifier_push(&v, TYPE_RREF))
			goto clean;
		ddata->ref_qualifier = true;
		ddata->ref_qualifier_type = TYPE_RREF;
		++ddata->cur;
		if (td)
			td->firstp = false;
		goto again;

	case 'P':
		/* pointer */
		if (!vector_type_qualifier_push(&v, TYPE_PTR))
			goto clean;
		++ddata->cur;
		if (td)
			td->firstp = false;
		goto again;

	case 'r':
		/* restrict */
		if (!vector_type_qualifier_push(&v, TYPE_RST))
			goto clean;
		++ddata->cur;
		if (td)
			td->firstp = false;
		goto again;

	case 'R':
		/* reference */
		if (ddata->ref_qualifier)
			goto clean;
		if (!vector_type_qualifier_push(&v, TYPE_REF))
			goto clean;
		ddata->ref_qualifier = true;
		ddata->ref_qualifier_type = TYPE_REF;
		++ddata->cur;
		if (td)
			td->firstp = false;
		goto again;

	case 's':
		/* short, local string */
		if (!DEM_PUSH_STR(ddata, "short"))
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
		if (!DEM_PUSH_STR(ddata, "unsigned short"))
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
		++ddata->cur;
		if (!cpp_demangle_read_number(ddata, &len))
			goto clean;
		if (len <= 0)
			goto clean;
		if (!vector_str_push(&v.ext_name, ddata->cur, len))
			return (0);
		ddata->cur += len;
		if (!vector_type_qualifier_push(&v, TYPE_EXT))
			goto clean;
		if (td)
			td->firstp = false;
		goto again;

	case 'v':
		/* void */
		omit_void = false;
		if (td && td->firstp) {
			/*
			 * peek into next bytes and see if we should omit
			 * the "void".
			 */
			omit_void = true;
			for (p = ddata->cur + 1; *p != '\0'; p++) {
				if (*p == 'E')
					break;
				if (*p != 'R' && *p != 'O') {
					omit_void = false;
					break;
				}
			}
		}
		if (!omit_void && !DEM_PUSH_STR(ddata, "void"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'V':
		/* volatile */
		if (!vector_type_qualifier_push(&v, TYPE_VAT))
			goto clean;
		++ddata->cur;
		if (td)
			td->firstp = false;
		goto again;

	case 'w':
		/* wchar_t */
		if (!DEM_PUSH_STR(ddata, "wchar_t"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'x':
		/* long long */
		if (!DEM_PUSH_STR(ddata, "long long"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'y':
		/* unsigned long long */
		if (!DEM_PUSH_STR(ddata, "unsigned long long"))
			goto clean;
		++ddata->cur;
		goto rtn;

	case 'z':
		/* ellipsis */
		if (!DEM_PUSH_STR(ddata, "..."))
			goto clean;
		++ddata->cur;
		goto rtn;
	}

	if (!cpp_demangle_read_name(ddata))
		goto clean;

	is_builtin = 0;
rtn:

	type_str = vector_str_substr(output, p_idx, output->size - 1,
	    &type_str_len);

	if (is_builtin == 0) {
		if (!vector_str_find(&ddata->subst, type_str, type_str_len) &&
		    !vector_str_push(&ddata->subst, type_str, type_str_len))
			goto clean;
	}

	if (!skip_ref_qualifier &&
	    !cpp_demangle_push_type_qualifier(ddata, &v, type_str))
		goto clean;

	if (td)
		td->firstp = false;

	free(type_str);
	free(exp_str);
	free(num_str);
	vector_type_qualifier_dest(&v);

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

	output = ddata->cur_output;

	p_idx = output->size;

	if (!cpp_demangle_read_type(ddata, NULL))
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
		if (!DEM_PUSH_STR(ddata, "operator&&"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('a', 'd'):
		/* operator & (unary) */
		if (!DEM_PUSH_STR(ddata, "operator&"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('a', 'n'):
		/* operator & */
		if (!DEM_PUSH_STR(ddata, "operator&"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('a', 'N'):
		/* operator &= */
		if (!DEM_PUSH_STR(ddata, "operator&="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('a', 'S'):
		/* operator = */
		if (!DEM_PUSH_STR(ddata, "operator="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('c', 'l'):
		/* operator () */
		if (!DEM_PUSH_STR(ddata, "operator()"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('c', 'm'):
		/* operator , */
		if (!DEM_PUSH_STR(ddata, "operator,"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('c', 'o'):
		/* operator ~ */
		if (!DEM_PUSH_STR(ddata, "operator~"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('c', 'v'):
		/* operator (cast) */
		if (!DEM_PUSH_STR(ddata, "operator(cast)"))
			return (0);
		ddata->cur += 2;
		return (cpp_demangle_read_type(ddata, NULL));

	case SIMPLE_HASH('d', 'a'):
		/* operator delete [] */
		if (!DEM_PUSH_STR(ddata, "operator delete []"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('d', 'e'):
		/* operator * (unary) */
		if (!DEM_PUSH_STR(ddata, "operator*"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('d', 'l'):
		/* operator delete */
		if (!DEM_PUSH_STR(ddata, "operator delete"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('d', 'v'):
		/* operator / */
		if (!DEM_PUSH_STR(ddata, "operator/"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('d', 'V'):
		/* operator /= */
		if (!DEM_PUSH_STR(ddata, "operator/="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('e', 'o'):
		/* operator ^ */
		if (!DEM_PUSH_STR(ddata, "operator^"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('e', 'O'):
		/* operator ^= */
		if (!DEM_PUSH_STR(ddata, "operator^="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('e', 'q'):
		/* operator == */
		if (!DEM_PUSH_STR(ddata, "operator=="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('g', 'e'):
		/* operator >= */
		if (!DEM_PUSH_STR(ddata, "operator>="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('g', 't'):
		/* operator > */
		if (!DEM_PUSH_STR(ddata, "operator>"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('i', 'x'):
		/* operator [] */
		if (!DEM_PUSH_STR(ddata, "operator[]"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('l', 'e'):
		/* operator <= */
		if (!DEM_PUSH_STR(ddata, "operator<="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('l', 's'):
		/* operator << */
		if (!DEM_PUSH_STR(ddata, "operator<<"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('l', 'S'):
		/* operator <<= */
		if (!DEM_PUSH_STR(ddata, "operator<<="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('l', 't'):
		/* operator < */
		if (!DEM_PUSH_STR(ddata, "operator<"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('m', 'i'):
		/* operator - */
		if (!DEM_PUSH_STR(ddata, "operator-"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('m', 'I'):
		/* operator -= */
		if (!DEM_PUSH_STR(ddata, "operator-="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('m', 'l'):
		/* operator * */
		if (!DEM_PUSH_STR(ddata, "operator*"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('m', 'L'):
		/* operator *= */
		if (!DEM_PUSH_STR(ddata, "operator*="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('m', 'm'):
		/* operator -- */
		if (!DEM_PUSH_STR(ddata, "operator--"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('n', 'a'):
		/* operator new[] */
		if (!DEM_PUSH_STR(ddata, "operator new []"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('n', 'e'):
		/* operator != */
		if (!DEM_PUSH_STR(ddata, "operator!="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('n', 'g'):
		/* operator - (unary) */
		if (!DEM_PUSH_STR(ddata, "operator-"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('n', 't'):
		/* operator ! */
		if (!DEM_PUSH_STR(ddata, "operator!"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('n', 'w'):
		/* operator new */
		if (!DEM_PUSH_STR(ddata, "operator new"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('o', 'o'):
		/* operator || */
		if (!DEM_PUSH_STR(ddata, "operator||"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('o', 'r'):
		/* operator | */
		if (!DEM_PUSH_STR(ddata, "operator|"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('o', 'R'):
		/* operator |= */
		if (!DEM_PUSH_STR(ddata, "operator|="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 'l'):
		/* operator + */
		if (!DEM_PUSH_STR(ddata, "operator+"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 'L'):
		/* operator += */
		if (!DEM_PUSH_STR(ddata, "operator+="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 'm'):
		/* operator ->* */
		if (!DEM_PUSH_STR(ddata, "operator->*"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 'p'):
		/* operator ++ */
		if (!DEM_PUSH_STR(ddata, "operator++"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 's'):
		/* operator + (unary) */
		if (!DEM_PUSH_STR(ddata, "operator+"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('p', 't'):
		/* operator -> */
		if (!DEM_PUSH_STR(ddata, "operator->"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('q', 'u'):
		/* operator ? */
		if (!DEM_PUSH_STR(ddata, "operator?"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('r', 'm'):
		/* operator % */
		if (!DEM_PUSH_STR(ddata, "operator%"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('r', 'M'):
		/* operator %= */
		if (!DEM_PUSH_STR(ddata, "operator%="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('r', 's'):
		/* operator >> */
		if (!DEM_PUSH_STR(ddata, "operator>>"))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('r', 'S'):
		/* operator >>= */
		if (!DEM_PUSH_STR(ddata, "operator>>="))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('r', 'z'):
		/* operator sizeof */
		if (!DEM_PUSH_STR(ddata, "operator sizeof "))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('s', 'r'):
		/* scope resolution operator */
		if (!DEM_PUSH_STR(ddata, "scope resolution operator "))
			return (0);
		ddata->cur += 2;
		return (1);

	case SIMPLE_HASH('s', 'v'):
		/* operator sizeof */
		if (!DEM_PUSH_STR(ddata, "operator sizeof "))
			return (0);
		ddata->cur += 2;
		return (1);
	}

	/* vendor extened operator */
	if (*ddata->cur == 'v' && ELFTC_ISDIGIT(*(ddata->cur + 1))) {
		if (!DEM_PUSH_STR(ddata, "vendor extened operator "))
			return (0);
		if (!cpp_demangle_push_str(ddata, ddata->cur + 1, 1))
			return (0);
		ddata->cur += 2;
		return (cpp_demangle_read_sname(ddata));
	}

	/* ctor-dtor-name */
	switch (SIMPLE_HASH(*ddata->cur, *(ddata->cur + 1))) {
	case SIMPLE_HASH('C', '1'):
	case SIMPLE_HASH('C', '2'):
	case SIMPLE_HASH('C', '3'):
		if (ddata->last_sname == NULL)
			return (0);
		if ((len = strlen(ddata->last_sname)) == 0)
			return (0);
		if (!DEM_PUSH_STR(ddata, "::"))
			return (0);
		if (!cpp_demangle_push_str(ddata, ddata->last_sname, len))
			return (0);
		ddata->cur +=2;
		return (1);

	case SIMPLE_HASH('D', '0'):
	case SIMPLE_HASH('D', '1'):
	case SIMPLE_HASH('D', '2'):
		if (ddata->last_sname == NULL)
			return (0);
		if ((len = strlen(ddata->last_sname)) == 0)
			return (0);
		if (!DEM_PUSH_STR(ddata, "::~"))
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

	if (!DEM_PUSH_STR(ddata, "offset : "))
		return (0);

	if (!cpp_demangle_read_offset_number(ddata))
		return (0);

	if (!DEM_PUSH_STR(ddata, "virtual offset : "))
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

/**
 * @brief Test input string is mangled by IA-64 C++ ABI style.
 *
 * Test string heads with "_Z" or "_GLOBAL__I_".
 * @return Return 0 at false.
 */
bool
is_cpp_mangled_gnu3(const char *org)
{
	size_t len;

	len = strlen(org);
	return ((len > 2 && *org == '_' && *(org + 1) == 'Z') ||
	    (len > 11 && !strncmp(org, "_GLOBAL__I_", 11)));
}

static void
vector_read_cmd_dest(struct vector_read_cmd *v)
{

	if (v == NULL)
		return;

	free(v->r_container);
}

static struct read_cmd_item *
vector_read_cmd_find(struct vector_read_cmd *v, enum read_cmd dst)
{
	int i;

	if (v == NULL || dst == READ_FAIL)
		return (NULL);

	for (i = (int) v->size - 1; i >= 0; i--)
		if (v->r_container[i].cmd == dst)
			return (&v->r_container[i]);

	return (NULL);
}

static int
vector_read_cmd_init(struct vector_read_cmd *v)
{

	if (v == NULL)
		return (0);

	v->size = 0;
	v->capacity = VECTOR_DEF_CAPACITY;

	if ((v->r_container = malloc(sizeof(*v->r_container) * v->capacity))
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
	v->r_container[v->size].cmd = READ_FAIL;
	v->r_container[v->size].data = NULL;

	return (1);
}

static int
vector_read_cmd_push(struct vector_read_cmd *v, enum read_cmd cmd, void *data)
{
	struct read_cmd_item *tmp_r_ctn;
	size_t tmp_cap;
	size_t i;

	if (v == NULL)
		return (0);

	if (v->size == v->capacity) {
		tmp_cap = v->capacity * BUFFER_GROWFACTOR;
		if ((tmp_r_ctn = malloc(sizeof(*tmp_r_ctn) * tmp_cap)) == NULL)
			return (0);
		for (i = 0; i < v->size; ++i)
			tmp_r_ctn[i] = v->r_container[i];
		free(v->r_container);
		v->r_container = tmp_r_ctn;
		v->capacity = tmp_cap;
	}

	v->r_container[v->size].cmd = cmd;
	v->r_container[v->size].data = data;
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
