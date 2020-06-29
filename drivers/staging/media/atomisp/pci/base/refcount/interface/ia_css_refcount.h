/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _IA_CSS_REFCOUNT_H_
#define _IA_CSS_REFCOUNT_H_

#include <type_support.h>
#include <system_local.h>
#include <ia_css_err.h>
#include <ia_css_types.h>

typedef void (*clear_func)(ia_css_ptr ptr);

/*! \brief Function for initializing refcount list
 *
 * \param[in]	size		Size of the refcount list.
 * \return				ia_css_err
 */
int ia_css_refcount_init(uint32_t size);

/*! \brief Function for de-initializing refcount list
 *
 * \return				None
 */
void ia_css_refcount_uninit(void);

/*! \brief Function for increasing reference by 1.
 *
 * \param[in]	id		ID of the object.
 * \param[in]	ptr		Data of the object (ptr).
 * \return				ia_css_ptr (saved address)
 */
ia_css_ptr ia_css_refcount_increment(s32 id, ia_css_ptr ptr);

/*! \brief Function for decrease reference by 1.
 *
 * \param[in]	id		ID of the object.
 * \param[in]	ptr		Data of the object (ptr).
 *
 *	- true, if it is successful.
 *	- false, otherwise.
 */
bool ia_css_refcount_decrement(s32 id, ia_css_ptr ptr);

/*! \brief Function to check if reference count is 1.
 *
 * \param[in]	ptr		Data of the object (ptr).
 *
 *	- true, if it is successful.
 *	- false, otherwise.
 */
bool ia_css_refcount_is_single(ia_css_ptr ptr);

/*! \brief Function to clear reference list objects.
 *
 * \param[in]	id			ID of the object.
 * \param[in] clear_func	function to be run to free reference objects.
 *
 *  return				None
 */
void ia_css_refcount_clear(s32 id,
			   clear_func clear_func_ptr);

/*! \brief Function to verify if object is valid
 *
 * \param[in] ptr       Data of the object (ptr)
 *
 *      - true, if valid
 *      - false, if invalid
 */
bool ia_css_refcount_is_valid(ia_css_ptr ptr);

#endif /* _IA_CSS_REFCOUNT_H_ */
