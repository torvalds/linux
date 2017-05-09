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
#include <system_types.h>
#include <ia_css_err.h>

typedef void (*clear_func)(hrt_vaddress ptr);

/*! \brief Function for initializing refcount list
 *
 * \param[in]	size		Size of the refcount list.
 * \return				ia_css_err
 */
extern enum ia_css_err ia_css_refcount_init(uint32_t size);

/*! \brief Function for de-initializing refcount list
 *
 * \return				None
 */
extern void ia_css_refcount_uninit(void);

/*! \brief Function for increasing reference by 1.
 *
 * \param[in]	id		ID of the object.
 * \param[in]	ptr		Data of the object (ptr).
 * \return				hrt_vaddress (saved address)
 */
extern hrt_vaddress ia_css_refcount_increment(int32_t id, hrt_vaddress ptr);

/*! \brief Function for decrease reference by 1.
 *
 * \param[in]	id		ID of the object.
 * \param[in]	ptr		Data of the object (ptr).
 *
 *	- true, if it is successful.
 *	- false, otherwise.
 */
extern bool ia_css_refcount_decrement(int32_t id, hrt_vaddress ptr);

/*! \brief Function to check if reference count is 1.
 *
 * \param[in]	ptr		Data of the object (ptr).
 *
 *	- true, if it is successful.
 *	- false, otherwise.
 */
extern bool ia_css_refcount_is_single(hrt_vaddress ptr);

/*! \brief Function to clear reference list objects.
 *
 * \param[in]	id			ID of the object.
 * \param[in] clear_func	function to be run to free reference objects.
 *
 *  return				None
 */
extern void ia_css_refcount_clear(int32_t id,
				  clear_func clear_func_ptr);

/*! \brief Function to verify if object is valid
 *
 * \param[in] ptr       Data of the object (ptr)
 *
 *      - true, if valid
 *      - false, if invalid
 */
extern bool ia_css_refcount_is_valid(hrt_vaddress ptr);

#endif /* _IA_CSS_REFCOUNT_H_ */
