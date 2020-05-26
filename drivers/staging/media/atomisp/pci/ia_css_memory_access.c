/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015-2017, Intel Corporation.
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

#include <memory_access.h>

hrt_vaddress mmgr_alloc_attr(const size_t size, const uint16_t attrs)
{
	return hmm_alloc(size, HMM_BO_PRIVATE, 0, NULL, attrs);
}
