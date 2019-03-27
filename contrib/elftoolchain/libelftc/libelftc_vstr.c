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
#include <libelftc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "_libelftc.h"

ELFTC_VCSID("$Id: libelftc_vstr.c 3531 2017-06-05 05:08:43Z kaiwang27 $");

/**
 * @file vector_str.c
 * @brief Dynamic vector data for string implementation.
 *
 * Resemble to std::vector<std::string> in C++.
 */

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
void
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
int
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
char *
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
bool
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
bool
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
bool
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
bool
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
 * @brief Push org vector to the tail of det vector.
 * @return false at failed, true at success.
 */
bool
vector_str_push_vector(struct vector_str *dst, struct vector_str *org)
{
	size_t i, j, tmp_cap;
	char **tmp_ctn;

	if (dst == NULL || org == NULL)
		return (false);

	tmp_cap = (dst->size + org->size) * BUFFER_GROWFACTOR;

	if ((tmp_ctn = malloc(sizeof(char *) * tmp_cap)) == NULL)
		return (false);

	for (i = 0; i < dst->size; ++i)
		tmp_ctn[i] = dst->container[i];

	for (i = 0; i < org->size; ++i)
		if ((tmp_ctn[i + dst->size] = strdup(org->container[i])) ==
		    NULL) {
			for (j = 0; j < i + dst->size; ++j)
				free(tmp_ctn[j]);

			free(tmp_ctn);

			return (false);
		}

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
char *
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
