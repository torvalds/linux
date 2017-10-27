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

#ifndef __GPIO_PRIVATE_H_INCLUDED__
#define __GPIO_PRIVATE_H_INCLUDED__

#include "gpio_public.h"

#include "device_access.h"

#include "assert_support.h"

STORAGE_CLASS_GPIO_C void gpio_reg_store(
	const gpio_ID_t	ID,
	const unsigned int		reg,
	const hrt_data			value)
{
OP___assert(ID < N_GPIO_ID);
OP___assert(GPIO_BASE[ID] != (hrt_address)-1);
	ia_css_device_store_uint32(GPIO_BASE[ID] + reg*sizeof(hrt_data), value);
return;
}

STORAGE_CLASS_GPIO_C hrt_data gpio_reg_load(
	const gpio_ID_t	ID,
	const unsigned int		reg)
{
OP___assert(ID < N_GPIO_ID);
OP___assert(GPIO_BASE[ID] != (hrt_address)-1);
return ia_css_device_load_uint32(GPIO_BASE[ID] + reg*sizeof(hrt_data));
}

#endif /* __GPIO_PRIVATE_H_INCLUDED__ */
