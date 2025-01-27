/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 */

#ifndef __GPIO_PRIVATE_H_INCLUDED__
#define __GPIO_PRIVATE_H_INCLUDED__

#include "assert_support.h"
#include "device_access.h"

static inline void gpio_reg_store(
    const gpio_ID_t	ID,
    const unsigned int		reg,
    const hrt_data			value)
{
	OP___assert(ID < N_GPIO_ID);
	OP___assert(GPIO_BASE[ID] != (hrt_address) - 1);
	ia_css_device_store_uint32(GPIO_BASE[ID] + reg * sizeof(hrt_data), value);
	return;
}

static inline hrt_data gpio_reg_load(
    const gpio_ID_t	ID,
    const unsigned int		reg)
{
	OP___assert(ID < N_GPIO_ID);
	OP___assert(GPIO_BASE[ID] != (hrt_address) - 1);
	return ia_css_device_load_uint32(GPIO_BASE[ID] + reg * sizeof(hrt_data));
}

#endif /* __GPIO_PRIVATE_H_INCLUDED__ */
