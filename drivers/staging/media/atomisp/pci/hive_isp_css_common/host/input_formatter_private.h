/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 */

#ifndef __INPUT_FORMATTER_PRIVATE_H_INCLUDED__
#define __INPUT_FORMATTER_PRIVATE_H_INCLUDED__

#include "input_formatter_public.h"

#include "device_access.h"

#include "assert_support.h"

STORAGE_CLASS_INPUT_FORMATTER_C void input_formatter_reg_store(
    const input_formatter_ID_t		ID,
    const hrt_address			reg_addr,
    const hrt_data				value)
{
	assert(ID < N_INPUT_FORMATTER_ID);
	assert(INPUT_FORMATTER_BASE[ID] != (hrt_address)-1);
	assert((reg_addr % sizeof(hrt_data)) == 0);
	ia_css_device_store_uint32(INPUT_FORMATTER_BASE[ID] + reg_addr, value);
	return;
}

STORAGE_CLASS_INPUT_FORMATTER_C hrt_data input_formatter_reg_load(
    const input_formatter_ID_t	ID,
    const unsigned int			reg_addr)
{
	assert(ID < N_INPUT_FORMATTER_ID);
	assert(INPUT_FORMATTER_BASE[ID] != (hrt_address)-1);
	assert((reg_addr % sizeof(hrt_data)) == 0);
	return ia_css_device_load_uint32(INPUT_FORMATTER_BASE[ID] + reg_addr);
}

#endif /* __INPUT_FORMATTER_PRIVATE_H_INCLUDED__ */
