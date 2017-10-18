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

#ifndef __GP_DEVICE_PUBLIC_H_INCLUDED__
#define __GP_DEVICE_PUBLIC_H_INCLUDED__

#include "system_types.h"

typedef struct gp_device_state_s		gp_device_state_t;

/*! Read the state of GP_DEVICE[ID]
 
 \param	ID[in]				GP_DEVICE identifier
 \param	state[out]			gp device state structure

 \return none, state = GP_DEVICE[ID].state
 */
extern void gp_device_get_state(
	const gp_device_ID_t		ID,
	gp_device_state_t			*state);

/*! Write to a control register of GP_DEVICE[ID]

 \param	ID[in]				GP_DEVICE identifier
 \param	reg_addr[in]		register byte address
 \param value[in]			The data to be written

 \return none, GP_DEVICE[ID].ctrl[reg] = value
 */
STORAGE_CLASS_GP_DEVICE_H void gp_device_reg_store(
	const gp_device_ID_t	ID,
	const unsigned int		reg_addr,
	const hrt_data			value);

/*! Read from a control register of GP_DEVICE[ID]
 
 \param	ID[in]				GP_DEVICE identifier
 \param	reg_addr[in]		register byte address
 \param value[in]			The data to be written

 \return GP_DEVICE[ID].ctrl[reg]
 */
STORAGE_CLASS_GP_DEVICE_H hrt_data gp_device_reg_load(
	const gp_device_ID_t	ID,
	const hrt_address	reg_addr);

#endif /* __GP_DEVICE_PUBLIC_H_INCLUDED__ */
