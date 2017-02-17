/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#ifndef __MEMORY_REALLOC_H_INCLUDED__
#define __MEMORY_REALLOC_H_INCLUDED__

/*!
 * \brief
 * Define the internal reallocation of private css memory
 *
 */

#include <type_support.h>
/*
 * User provided file that defines the (sub)system address types:
 *	- hrt_vaddress	a type that can hold the (sub)system virtual address range
 */
#include "system_types.h"
#include "ia_css_err.h"

bool reallocate_buffer(
	hrt_vaddress *curr_buf,
	size_t *curr_size,
	size_t needed_size,
	bool force,
	enum ia_css_err *err);

#endif /*__MEMORY_REALLOC_H_INCLUDED__*/
