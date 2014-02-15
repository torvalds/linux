/*
 * Copyright (C) 2010-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
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
