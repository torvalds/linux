/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 *
 */

#ifndef _TTM_BO_INTERNAL_H_
#define _TTM_BO_INTERNAL_H_

#include <drm/ttm/ttm_bo.h>

/**
 * ttm_bo_get - reference a struct ttm_buffer_object
 *
 * @bo: The buffer object.
 */
static inline void ttm_bo_get(struct ttm_buffer_object *bo)
{
	kref_get(&bo->kref);
}

/**
 * ttm_bo_get_unless_zero - reference a struct ttm_buffer_object unless
 * its refcount has already reached zero.
 * @bo: The buffer object.
 *
 * Used to reference a TTM buffer object in lookups where the object is removed
 * from the lookup structure during the destructor and for RCU lookups.
 *
 * Returns: @bo if the referencing was successful, NULL otherwise.
 */
static inline __must_check struct ttm_buffer_object *
ttm_bo_get_unless_zero(struct ttm_buffer_object *bo)
{
	if (!kref_get_unless_zero(&bo->kref))
		return NULL;
	return bo;
}

#endif
