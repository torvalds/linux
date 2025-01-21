/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 */

#ifndef __SP_LOCAL_H_INCLUDED__
#define __SP_LOCAL_H_INCLUDED__

#include <type_support.h>
#include "sp_global.h"

#define sp_address_of(var)	(HIVE_ADDR_ ## var)

/*
 * deprecated
 */
#define store_sp_int(var, value) \
	sp_dmem_store_uint32(SP0_ID, (unsigned int)sp_address_of(var), \
		(uint32_t)(value))

#define store_sp_ptr(var, value) \
	sp_dmem_store_uint32(SP0_ID, (unsigned int)sp_address_of(var), \
		(uint32_t)(value))

#define load_sp_uint(var) \
	sp_dmem_load_uint32(SP0_ID, (unsigned int)sp_address_of(var))

#define load_sp_array_uint8(array_name, index) \
	sp_dmem_load_uint8(SP0_ID, (unsigned int)sp_address_of(array_name) + \
		(index) * sizeof(uint8_t))

#define load_sp_array_uint16(array_name, index) \
	sp_dmem_load_uint16(SP0_ID, (unsigned int)sp_address_of(array_name) + \
		(index) * sizeof(uint16_t))

#define load_sp_array_uint(array_name, index) \
	sp_dmem_load_uint32(SP0_ID, (unsigned int)sp_address_of(array_name) + \
		(index) * sizeof(uint32_t))

#define store_sp_var(var, data, bytes) \
	sp_dmem_store(SP0_ID, (unsigned int)sp_address_of(var), data, bytes)

#define store_sp_array_uint8(array_name, index, value) \
	sp_dmem_store_uint8(SP0_ID, (unsigned int)sp_address_of(array_name) + \
		(index) * sizeof(uint8_t), value)

#define store_sp_array_uint16(array_name, index, value) \
	sp_dmem_store_uint16(SP0_ID, (unsigned int)sp_address_of(array_name) + \
		(index) * sizeof(uint16_t), value)

#define store_sp_array_uint(array_name, index, value) \
	sp_dmem_store_uint32(SP0_ID, (unsigned int)sp_address_of(array_name) + \
		(index) * sizeof(uint32_t), value)

#define store_sp_var_with_offset(var, offset, data, bytes) \
	sp_dmem_store(SP0_ID, (unsigned int)sp_address_of(var) + \
		offset, data, bytes)

#define load_sp_var(var, data, bytes) \
	sp_dmem_load(SP0_ID, (unsigned int)sp_address_of(var), data, bytes)

#define load_sp_var_with_offset(var, offset, data, bytes) \
	sp_dmem_load(SP0_ID, (unsigned int)sp_address_of(var) + offset, \
		data, bytes)

#endif /* __SP_LOCAL_H_INCLUDED__ */
