/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
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

#ifndef __VAMEM_PRIVATE_H_INCLUDED__
#define __VAMEM_PRIVATE_H_INCLUDED__

#include "vamem_public.h"

#include <hrt/api.h>

#include "assert_support.h"


STORAGE_CLASS_ISP_C void isp_vamem_store(
	const vamem_ID_t	ID,
	vamem_data_t		*addr,
	const vamem_data_t	*data,
	const size_t		size) /* in vamem_data_t */
{
	assert(ID < N_VAMEM_ID);
	assert(ISP_VAMEM_BASE[ID] != (hrt_address)-1);
	hrt_master_port_store(ISP_VAMEM_BASE[ID] + (unsigned)addr, data, size * sizeof(vamem_data_t));
}


#endif /* __VAMEM_PRIVATE_H_INCLUDED__ */
