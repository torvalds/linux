/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _IA_CSS_CIRCBUF_H
#define _IA_CSS_CIRCBUF_H

#include <sp.h>
#include <type_support.h>
#include <math_support.h>
#include <storage_class.h>
#include <assert_support.h>
#include <platform_support.h>
#include "ia_css_circbuf_comm.h"
#include "ia_css_circbuf_desc.h"
#ifdef __SP
#include "event_handler.sp.h"
/* We should not #define SP_FILE_ID here, because we are in a header file. */
#include "ia_css_sp_assert_level.sp.h"
#endif

/****************************************************************
 *
 * Data structures.
 *
 ****************************************************************/
/**
 * @brief Data structure for the circular buffer.
 */
typedef struct ia_css_circbuf_s ia_css_circbuf_t;
struct ia_css_circbuf_s {
	ia_css_circbuf_desc_t *desc;    /* Pointer to the descriptor of the circbuf */
	ia_css_circbuf_elem_t *elems;	/* an array of elements    */
};

/**
 * @brief Create the circular buffer.
 *
 * @param cb	The pointer to the circular buffer.
 * @param elems	An array of elements.
 * @param desc	The descriptor set to the size using ia_css_circbuf_desc_init().
 */
STORAGE_CLASS_EXTERN void ia_css_circbuf_create(
	ia_css_circbuf_t *cb,
	ia_css_circbuf_elem_t *elems,
	ia_css_circbuf_desc_t *desc);

/**
 * @brief Destroy the circular buffer.
 *
 * @param cb The pointer to the circular buffer.
 */
STORAGE_CLASS_EXTERN void ia_css_circbuf_destroy(
		ia_css_circbuf_t *cb);

/**
 * @brief Pop a value out of the circular buffer.
 * Get a value at the head of the circular buffer.
 * The user should call "ia_css_circbuf_is_empty()"
 * to avoid accessing to an empty buffer.
 *
 * @param cb	The pointer to the circular buffer.
 *
 * @return the pop-out value.
 */
STORAGE_CLASS_EXTERN uint32_t ia_css_circbuf_pop(
		ia_css_circbuf_t *cb);

/**
 * @brief Extract a value out of the circular buffer.
 * Get a value at an arbitrary poistion in the circular
 * buffer. The user should call "ia_css_circbuf_is_empty()"
 * to avoid accessing to an empty buffer.
 *
 * @param cb	 The pointer to the circular buffer.
 * @param offset The offset from "start" to the target position.
 *
 * @return the extracted value.
 */
STORAGE_CLASS_EXTERN uint32_t ia_css_circbuf_extract(
	ia_css_circbuf_t *cb,
	int offset);

/****************************************************************
 *
 * Inline functions.
 *
 ****************************************************************/
/**
 * @brief Set the "val" field in the element.
 *
 * @param elem The pointer to the element.
 * @param val  The value to be set.
 */
STORAGE_CLASS_INLINE void ia_css_circbuf_elem_set_val(
	ia_css_circbuf_elem_t *elem,
	uint32_t val)
{
	OP___assert(elem != NULL);

	elem->val = val;
}

/**
 * @brief Initialize the element.
 *
 * @param elem The pointer to the element.
 */
STORAGE_CLASS_INLINE void ia_css_circbuf_elem_init(
		ia_css_circbuf_elem_t *elem)
{
	OP___assert(elem != NULL);
	ia_css_circbuf_elem_set_val(elem, 0);
}

/**
 * @brief Copy an element.
 *
 * @param src  The element as the copy source.
 * @param dest The element as the copy destination.
 */
STORAGE_CLASS_INLINE void ia_css_circbuf_elem_cpy(
	ia_css_circbuf_elem_t *src,
	ia_css_circbuf_elem_t *dest)
{
	OP___assert(src != NULL);
	OP___assert(dest != NULL);

	ia_css_circbuf_elem_set_val(dest, src->val);
}

/**
 * @brief Get position in the circular buffer.
 *
 * @param cb		The pointer to the circular buffer.
 * @param base		The base position.
 * @param offset	The offset.
 *
 * @return the position at offset.
 */
STORAGE_CLASS_INLINE uint8_t ia_css_circbuf_get_pos_at_offset(
	ia_css_circbuf_t *cb,
	uint32_t base,
	int offset)
{
	uint8_t dest;

	OP___assert(cb != NULL);
	OP___assert(cb->desc != NULL);
	OP___assert(cb->desc->size > 0);

	/* step 1: adjudst the offset  */
	while (offset < 0) {
		offset += cb->desc->size;
	}

	/* step 2: shift and round by the upper limit */
	dest = OP_std_modadd(base, offset, cb->desc->size);

	return dest;
}

/**
 * @brief Get the offset between two positions in the circular buffer.
 * Get the offset from the source position to the terminal position,
 * along the direction in which the new elements come in.
 *
 * @param cb		The pointer to the circular buffer.
 * @param src_pos	The source position.
 * @param dest_pos	The terminal position.
 *
 * @return the offset.
 */
STORAGE_CLASS_INLINE int ia_css_circbuf_get_offset(
	ia_css_circbuf_t *cb,
	uint32_t src_pos,
	uint32_t dest_pos)
{
	int offset;

	OP___assert(cb != NULL);
	OP___assert(cb->desc != NULL);

	offset = (int)(dest_pos - src_pos);
	offset += (offset < 0) ? cb->desc->size : 0;

	return offset;
}

/**
 * @brief Get the maximum number of elements.
 *
 * @param cb The pointer to the circular buffer.
 *
 * @return the maximum number of elements.
 *
 * TODO: Test this API.
 */
STORAGE_CLASS_INLINE uint32_t ia_css_circbuf_get_size(
		ia_css_circbuf_t *cb)
{
	OP___assert(cb != NULL);
	OP___assert(cb->desc != NULL);

	return cb->desc->size;
}

/**
 * @brief Get the number of available elements.
 *
 * @param cb The pointer to the circular buffer.
 *
 * @return the number of available elements.
 */
STORAGE_CLASS_INLINE uint32_t ia_css_circbuf_get_num_elems(
		ia_css_circbuf_t *cb)
{
	int num;

	OP___assert(cb != NULL);
	OP___assert(cb->desc != NULL);

	num = ia_css_circbuf_get_offset(cb, cb->desc->start, cb->desc->end);

	return (uint32_t)num;
}

/**
 * @brief Test if the circular buffer is empty.
 *
 * @param cb	The pointer to the circular buffer.
 *
 * @return
 *	- true when it is empty.
 *	- false when it is not empty.
 */
STORAGE_CLASS_INLINE bool ia_css_circbuf_is_empty(
		ia_css_circbuf_t *cb)
{
	OP___assert(cb != NULL);
	OP___assert(cb->desc != NULL);

	return ia_css_circbuf_desc_is_empty(cb->desc);
}

/**
 * @brief Test if the circular buffer is full.
 *
 * @param cb	The pointer to the circular buffer.
 *
 * @return
 *	- true when it is full.
 *	- false when it is not full.
 */
STORAGE_CLASS_INLINE bool ia_css_circbuf_is_full(ia_css_circbuf_t *cb)
{
	OP___assert(cb != NULL);
	OP___assert(cb->desc != NULL);

	return ia_css_circbuf_desc_is_full(cb->desc);
}

/**
 * @brief Write a new element into the circular buffer.
 * Write a new element WITHOUT checking whether the
 * circular buffer is full or not. So it also overwrites
 * the oldest element when the buffer is full.
 *
 * @param cb	The pointer to the circular buffer.
 * @param elem	The new element.
 */
STORAGE_CLASS_INLINE void ia_css_circbuf_write(
	ia_css_circbuf_t *cb,
	ia_css_circbuf_elem_t elem)
{
	OP___assert(cb != NULL);
	OP___assert(cb->desc != NULL);

	/* Cannot continue as the queue is full*/
#ifdef __SP
	SP_ASSERT_FATAL(!ia_css_circbuf_is_full(cb));
#else
	assert(!ia_css_circbuf_is_full(cb));
#endif

	ia_css_circbuf_elem_cpy(&elem, &cb->elems[cb->desc->end]);

	cb->desc->end = ia_css_circbuf_get_pos_at_offset(cb, cb->desc->end, 1);
}

/**
 * @brief Push a value in the circular buffer.
 * Put a new value at the tail of the circular buffer.
 * The user should call "ia_css_circbuf_is_full()"
 * to avoid accessing to a full buffer.
 *
 * @param cb	The pointer to the circular buffer.
 * @param val	The value to be pushed in.
 */
STORAGE_CLASS_INLINE void ia_css_circbuf_push(
	ia_css_circbuf_t *cb,
	uint32_t val)
{
	ia_css_circbuf_elem_t elem;

	OP___assert(cb != NULL);

	/* set up an element */
	ia_css_circbuf_elem_init(&elem);
	ia_css_circbuf_elem_set_val(&elem, val);

	/* write the element into the buffer */
	ia_css_circbuf_write(cb, elem);
}

/**
 * @brief Get the number of free elements.
 *
 * @param cb The pointer to the circular buffer.
 *
 * @return: The number of free elements.
 */
STORAGE_CLASS_INLINE uint32_t ia_css_circbuf_get_free_elems(
		ia_css_circbuf_t *cb)
{
	OP___assert(cb != NULL);
	OP___assert(cb->desc != NULL);

	return ia_css_circbuf_desc_get_free_elems(cb->desc);
}

/**
 * @brief Peek an element in Circular Buffer.
 *
 * @param cb	 The pointer to the circular buffer.
 * @param offset Offset to the element.
 *
 * @return the elements value.
 */
STORAGE_CLASS_EXTERN uint32_t ia_css_circbuf_peek(
	ia_css_circbuf_t *cb,
	int offset);

/**
 * @brief Get an element in Circular Buffer.
 *
 * @param cb	 The pointer to the circular buffer.
 * @param offset Offset to the element.
 *
 * @return the elements value.
 */
STORAGE_CLASS_EXTERN uint32_t ia_css_circbuf_peek_from_start(
	ia_css_circbuf_t *cb,
	int offset);

/**
 * @brief Increase Size of a Circular Buffer.
 * Use 'CAUTION' before using this function, This was added to
 * support / fix issue with increasing size for tagger only
 *
 * @param cb The pointer to the circular buffer.
 * @param sz_delta delta increase for new size
 * @param elems (optional) pointers to new additional elements
 *		cb element array size will not be increased dynamically,
 * 		but new elements should be added at the end to existing
 * 		cb element array which if of max_size >= new size
 *
 * @return	true on succesfully increasing the size
 * 			false on failure
 */
STORAGE_CLASS_EXTERN bool ia_css_circbuf_increase_size(
		ia_css_circbuf_t *cb,
		unsigned int sz_delta,
		ia_css_circbuf_elem_t *elems);

#endif /*_IA_CSS_CIRCBUF_H */
