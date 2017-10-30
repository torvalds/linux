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

#ifndef __TIMED_CTRL_PUBLIC_H_INCLUDED__
#define __TIMED_CTRL_PUBLIC_H_INCLUDED__

#include "system_types.h"

/*! Write to a control register of TIMED_CTRL[ID]

 \param	ID[in]				TIMED_CTRL identifier
 \param	reg_addr[in]		register byte address
 \param value[in]			The data to be written

 \return none, TIMED_CTRL[ID].ctrl[reg] = value
 */
STORAGE_CLASS_TIMED_CTRL_H void timed_ctrl_reg_store(
	const timed_ctrl_ID_t	ID,
	const unsigned int		reg_addr,
	const hrt_data			value);

extern void timed_ctrl_snd_commnd(
	const timed_ctrl_ID_t				ID,
	hrt_data				mask,
	hrt_data				condition,
	hrt_data				counter,
	hrt_address				addr,
	hrt_data				value);

extern void timed_ctrl_snd_sp_commnd(
	const timed_ctrl_ID_t				ID,
	hrt_data				mask,
	hrt_data				condition,
	hrt_data				counter,
	const sp_ID_t			SP_ID,
	hrt_address				offset,
	hrt_data				value);

extern void timed_ctrl_snd_gpio_commnd(
	const timed_ctrl_ID_t				ID,
	hrt_data				mask,
	hrt_data				condition,
	hrt_data				counter,
	const gpio_ID_t			GPIO_ID,
	hrt_address				offset,
	hrt_data				value);

#endif /* __TIMED_CTRL_PUBLIC_H_INCLUDED__ */
