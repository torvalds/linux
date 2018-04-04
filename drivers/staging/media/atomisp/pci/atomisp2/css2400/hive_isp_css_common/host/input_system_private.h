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

#ifndef __INPUT_SYSTEM_PRIVATE_H_INCLUDED__
#define __INPUT_SYSTEM_PRIVATE_H_INCLUDED__

#include "input_system_public.h"

#include "device_access.h"

#include "assert_support.h"

STORAGE_CLASS_INPUT_SYSTEM_C void input_system_reg_store(
	const input_system_ID_t			ID,
	const hrt_address			reg,
	const hrt_data				value)
{
	assert(ID < N_INPUT_SYSTEM_ID);
	assert(INPUT_SYSTEM_BASE[ID] != (hrt_address)-1);
	ia_css_device_store_uint32(INPUT_SYSTEM_BASE[ID] + reg*sizeof(hrt_data), value);
	return;
}

STORAGE_CLASS_INPUT_SYSTEM_C hrt_data input_system_reg_load(
	const input_system_ID_t			ID,
	const hrt_address			reg)
{
	assert(ID < N_INPUT_SYSTEM_ID);
	assert(INPUT_SYSTEM_BASE[ID] != (hrt_address)-1);
	return ia_css_device_load_uint32(INPUT_SYSTEM_BASE[ID] + reg*sizeof(hrt_data));
}

STORAGE_CLASS_INPUT_SYSTEM_C void receiver_reg_store(
	const rx_ID_t				ID,
	const hrt_address			reg,
	const hrt_data				value)
{
	assert(ID < N_RX_ID);
	assert(RX_BASE[ID] != (hrt_address)-1);
	ia_css_device_store_uint32(RX_BASE[ID] + reg*sizeof(hrt_data), value);
	return;
}

STORAGE_CLASS_INPUT_SYSTEM_C hrt_data receiver_reg_load(
	const rx_ID_t				ID,
	const hrt_address			reg)
{
	assert(ID < N_RX_ID);
	assert(RX_BASE[ID] != (hrt_address)-1);
	return ia_css_device_load_uint32(RX_BASE[ID] + reg*sizeof(hrt_data));
}

STORAGE_CLASS_INPUT_SYSTEM_C void receiver_port_reg_store(
	const rx_ID_t				ID,
	const mipi_port_ID_t			port_ID,
	const hrt_address			reg,
	const hrt_data				value)
{
	assert(ID < N_RX_ID);
	assert(port_ID < N_MIPI_PORT_ID);
	assert(RX_BASE[ID] != (hrt_address)-1);
	assert(MIPI_PORT_OFFSET[port_ID] != (hrt_address)-1);
	ia_css_device_store_uint32(RX_BASE[ID] + MIPI_PORT_OFFSET[port_ID] + reg*sizeof(hrt_data), value);
	return;
}

STORAGE_CLASS_INPUT_SYSTEM_C hrt_data receiver_port_reg_load(
	const rx_ID_t				ID,
	const mipi_port_ID_t			port_ID,
	const hrt_address			reg)
{
	assert(ID < N_RX_ID);
	assert(port_ID < N_MIPI_PORT_ID);
	assert(RX_BASE[ID] != (hrt_address)-1);
	assert(MIPI_PORT_OFFSET[port_ID] != (hrt_address)-1);
	return ia_css_device_load_uint32(RX_BASE[ID] + MIPI_PORT_OFFSET[port_ID] + reg*sizeof(hrt_data));
}

STORAGE_CLASS_INPUT_SYSTEM_C void input_system_sub_system_reg_store(
	const input_system_ID_t			ID,
	const sub_system_ID_t			sub_ID,
	const hrt_address			reg,
	const hrt_data				value)
{
	assert(ID < N_INPUT_SYSTEM_ID);
	assert(sub_ID < N_SUB_SYSTEM_ID);
	assert(INPUT_SYSTEM_BASE[ID] != (hrt_address)-1);
	assert(SUB_SYSTEM_OFFSET[sub_ID] != (hrt_address)-1);
	ia_css_device_store_uint32(INPUT_SYSTEM_BASE[ID] + SUB_SYSTEM_OFFSET[sub_ID] + reg*sizeof(hrt_data), value);
	return;
}

STORAGE_CLASS_INPUT_SYSTEM_C hrt_data input_system_sub_system_reg_load(
	const input_system_ID_t			ID,
	const sub_system_ID_t			sub_ID,
	const hrt_address			reg)
{
	assert(ID < N_INPUT_SYSTEM_ID);
	assert(sub_ID < N_SUB_SYSTEM_ID);
	assert(INPUT_SYSTEM_BASE[ID] != (hrt_address)-1);
	assert(SUB_SYSTEM_OFFSET[sub_ID] != (hrt_address)-1);
	return ia_css_device_load_uint32(INPUT_SYSTEM_BASE[ID] + SUB_SYSTEM_OFFSET[sub_ID] + reg*sizeof(hrt_data));
}

#endif /* __INPUT_SYSTEM_PRIVATE_H_INCLUDED__ */
