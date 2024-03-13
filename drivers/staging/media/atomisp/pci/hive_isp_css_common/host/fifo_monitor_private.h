/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __FIFO_MONITOR_PRIVATE_H_INCLUDED__
#define __FIFO_MONITOR_PRIVATE_H_INCLUDED__

#include "fifo_monitor_public.h"

#define __INLINE_GP_DEVICE__
#include "gp_device.h"

#include "device_access.h"

#include "assert_support.h"

#ifdef __INLINE_FIFO_MONITOR__
extern const unsigned int FIFO_SWITCH_ADDR[N_FIFO_SWITCH];
#endif

STORAGE_CLASS_FIFO_MONITOR_C void fifo_switch_set(
    const fifo_monitor_ID_t		ID,
    const fifo_switch_t			switch_id,
    const hrt_data				sel)
{
	assert(ID == FIFO_MONITOR0_ID);
	assert(FIFO_MONITOR_BASE[ID] != (hrt_address) - 1);
	assert(switch_id < N_FIFO_SWITCH);
	(void)ID;

	gp_device_reg_store(GP_DEVICE0_ID, FIFO_SWITCH_ADDR[switch_id], sel);

	return;
}

STORAGE_CLASS_FIFO_MONITOR_C hrt_data fifo_switch_get(
    const fifo_monitor_ID_t		ID,
    const fifo_switch_t			switch_id)
{
	assert(ID == FIFO_MONITOR0_ID);
	assert(FIFO_MONITOR_BASE[ID] != (hrt_address) - 1);
	assert(switch_id < N_FIFO_SWITCH);
	(void)ID;

	return gp_device_reg_load(GP_DEVICE0_ID, FIFO_SWITCH_ADDR[switch_id]);
}

STORAGE_CLASS_FIFO_MONITOR_C void fifo_monitor_reg_store(
    const fifo_monitor_ID_t		ID,
    const unsigned int			reg,
    const hrt_data				value)
{
	assert(ID < N_FIFO_MONITOR_ID);
	assert(FIFO_MONITOR_BASE[ID] != (hrt_address) - 1);
	ia_css_device_store_uint32(FIFO_MONITOR_BASE[ID] + reg * sizeof(hrt_data),
				   value);
	return;
}

STORAGE_CLASS_FIFO_MONITOR_C hrt_data fifo_monitor_reg_load(
    const fifo_monitor_ID_t		ID,
    const unsigned int			reg)
{
	assert(ID < N_FIFO_MONITOR_ID);
	assert(FIFO_MONITOR_BASE[ID] != (hrt_address) - 1);
	return ia_css_device_load_uint32(FIFO_MONITOR_BASE[ID] + reg * sizeof(
					     hrt_data));
}

#endif /* __FIFO_MONITOR_PRIVATE_H_INCLUDED__ */
