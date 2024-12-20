/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 */

#ifndef __GP_DEVICE_PRIVATE_H_INCLUDED__
#define __GP_DEVICE_PRIVATE_H_INCLUDED__

#include "gp_device_public.h"

#include "device_access.h"

#include "assert_support.h"

STORAGE_CLASS_GP_DEVICE_C void gp_device_reg_store(
    const gp_device_ID_t	ID,
    const unsigned int		reg_addr,
    const hrt_data			value)
{
	assert(ID < N_GP_DEVICE_ID);
	assert(GP_DEVICE_BASE[ID] != (hrt_address) - 1);
	assert((reg_addr % sizeof(hrt_data)) == 0);
	ia_css_device_store_uint32(GP_DEVICE_BASE[ID] + reg_addr, value);
	return;
}

STORAGE_CLASS_GP_DEVICE_C hrt_data gp_device_reg_load(
    const gp_device_ID_t	ID,
    const hrt_address	reg_addr)
{
	assert(ID < N_GP_DEVICE_ID);
	assert(GP_DEVICE_BASE[ID] != (hrt_address)-1);
	assert((reg_addr % sizeof(hrt_data)) == 0);
	return ia_css_device_load_uint32(GP_DEVICE_BASE[ID] + reg_addr);
}

#endif /* __GP_DEVICE_PRIVATE_H_INCLUDED__ */
