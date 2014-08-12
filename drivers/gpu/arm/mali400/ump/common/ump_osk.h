/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump_osk.h
 * Defines the OS abstraction layer for the UMP kernel device driver (OSK)
 */

#ifndef __UMP_OSK_H__
#define __UMP_OSK_H__

#include <mali_osk.h>
#include <ump_kernel_memory_backend.h>
#include "ump_uk_types.h"
#include "ump_kernel_common.h"

#ifdef __cplusplus
extern "C" {
#endif

_mali_osk_errcode_t _ump_osk_init( void );

_mali_osk_errcode_t _ump_osk_term( void );

int _ump_osk_atomic_inc_and_read( _mali_osk_atomic_t *atom );

int _ump_osk_atomic_dec_and_read( _mali_osk_atomic_t *atom );

_mali_osk_errcode_t _ump_osk_mem_mapregion_init( ump_memory_allocation *descriptor );

_mali_osk_errcode_t _ump_osk_mem_mapregion_map( ump_memory_allocation * descriptor, u32 offset, u32 * phys_addr, unsigned long size );

void _ump_osk_mem_mapregion_term( ump_memory_allocation * descriptor );

void _ump_osk_msync( ump_dd_mem * mem, void * virt, u32 offset, u32 size, ump_uk_msync_op op, ump_session_data * session_data );

#ifdef __cplusplus
}
#endif

#endif
