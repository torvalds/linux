/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DAL_VECTOR_H__
#define __DAL_VECTOR_H__

struct vector {
	uint8_t *container;
	uint32_t struct_size;
	uint32_t count;
	uint32_t capacity;
	struct dc_context *ctx;
};

bool dal_vector_construct(
	struct vector *vector,
	struct dc_context *ctx,
	uint32_t capacity,
	uint32_t struct_size);

struct vector *dal_vector_create(
	struct dc_context *ctx,
	uint32_t capacity,
	uint32_t struct_size);

/* 'initial_value' is optional. If initial_value not supplied,
 * each "structure" in the vector will contain zeros by default. */
struct vector *dal_vector_presized_create(
	struct dc_context *ctx,
	uint32_t size,
	void *initial_value,
	uint32_t struct_size);

void dal_vector_destruct(
	struct vector *vector);

void dal_vector_destroy(
	struct vector **vector);

uint32_t dal_vector_get_count(
	const struct vector *vector);

/* dal_vector_insert_at
 * reallocate container if necessary
 * then shell items at right and insert
 * return if the container modified
 * do not check that index belongs to container
 * since the function is private and index is going to be calculated
 * either with by function or as get_count+1 */
bool dal_vector_insert_at(
	struct vector *vector,
	const void *what,
	uint32_t position);

bool dal_vector_append(
	struct vector *vector,
	const void *item);

/* operator[] */
void *dal_vector_at_index(
	const struct vector *vector,
	uint32_t index);

void dal_vector_set_at_index(
	const struct vector *vector,
	const void *what,
	uint32_t index);

/* create a clone (copy) of a vector */
struct vector *dal_vector_clone(
	const struct vector *vector_other);

/* dal_vector_remove_at_index
 * Shifts elements on the right from remove position to the left,
 * removing an element at position by overwrite means*/
bool dal_vector_remove_at_index(
	struct vector *vector,
	uint32_t index);

uint32_t dal_vector_capacity(const struct vector *vector);

bool dal_vector_reserve(struct vector *vector, uint32_t capacity);

void dal_vector_clear(struct vector *vector);

/***************************************************************************
 * Macro definitions of TYPE-SAFE versions of vector set/get functions.
 ***************************************************************************/

#define DAL_VECTOR_INSERT_AT(vector_type, type_t) \
	static bool vector_type##_vector_insert_at( \
		struct vector *vector, \
		type_t what, \
		uint32_t position) \
{ \
	return dal_vector_insert_at(vector, what, position); \
}

#define DAL_VECTOR_APPEND(vector_type, type_t) \
	static bool vector_type##_vector_append( \
		struct vector *vector, \
		type_t item) \
{ \
	return dal_vector_append(vector, item); \
}

/* Note: "type_t" is the ONLY token accepted by "checkpatch.pl" and by
 * "checkcommit" as *return type*.
 * For uniformity reasons "type_t" is used for all type-safe macro
 * definitions here. */
#define DAL_VECTOR_AT_INDEX(vector_type, type_t) \
	static type_t vector_type##_vector_at_index( \
		const struct vector *vector, \
		uint32_t index) \
{ \
	return dal_vector_at_index(vector, index); \
}

#define DAL_VECTOR_SET_AT_INDEX(vector_type, type_t) \
	static void vector_type##_vector_set_at_index( \
		const struct vector *vector, \
		type_t what, \
		uint32_t index) \
{ \
	dal_vector_set_at_index(vector, what, index); \
}

#endif /* __DAL_VECTOR_H__ */
