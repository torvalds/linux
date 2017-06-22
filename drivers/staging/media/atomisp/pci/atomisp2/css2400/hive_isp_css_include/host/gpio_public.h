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

#ifndef __GPIO_PUBLIC_H_INCLUDED__
#define __GPIO_PUBLIC_H_INCLUDED__

#include "system_types.h"

/*! Write to a control register of GPIO[ID]

 \param	ID[in]				GPIO identifier
 \param	reg_addr[in]		register byte address
 \param value[in]			The data to be written

 \return none, GPIO[ID].ctrl[reg] = value
 */
STORAGE_CLASS_GPIO_H void gpio_reg_store(
	const gpio_ID_t	ID,
	const unsigned int		reg_addr,
	const hrt_data			value);

/*! Read from a control register of GPIO[ID]
 
 \param	ID[in]				GPIO identifier
 \param	reg_addr[in]		register byte address
 \param value[in]			The data to be written

 \return GPIO[ID].ctrl[reg]
 */
STORAGE_CLASS_GPIO_H hrt_data gpio_reg_load(
	const gpio_ID_t	ID,
	const unsigned int		reg_addr);

#endif /* __GPIO_PUBLIC_H_INCLUDED__ */
