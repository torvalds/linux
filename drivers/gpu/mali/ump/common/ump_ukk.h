/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file ump_ukk.h
 * Defines the kernel-side interface of the user-kernel interface
 */

#ifndef __UMP_UKK_H__
#define __UMP_UKK_H__

#include "mali_osk.h"
#include "ump_uk_types.h"


#ifdef __cplusplus
extern "C"
{
#endif


_mali_osk_errcode_t _ump_ukk_open( void** context );

_mali_osk_errcode_t _ump_ukk_close( void** context );

_mali_osk_errcode_t _ump_ukk_allocate( _ump_uk_allocate_s *user_interaction );

_mali_osk_errcode_t _ump_ukk_release( _ump_uk_release_s *release_info );

_mali_osk_errcode_t _ump_ukk_size_get( _ump_uk_size_get_s *user_interaction );

_mali_osk_errcode_t _ump_ukk_map_mem( _ump_uk_map_mem_s *args );

_mali_osk_errcode_t _ump_uku_get_api_version( _ump_uk_api_version_s *args );

void _ump_ukk_unmap_mem( _ump_uk_unmap_mem_s *args );

void _ump_ukk_msync( _ump_uk_msync_s *args );

void _ump_ukk_cache_operations_control(_ump_uk_cache_operations_control_s* args);

void _ump_ukk_switch_hw_usage(_ump_uk_switch_hw_usage_s *args );

void _ump_ukk_lock(_ump_uk_lock_s *args );

void _ump_ukk_unlock(_ump_uk_unlock_s *args );

u32 _ump_ukk_report_memory_usage( void );

#ifdef __cplusplus
}
#endif

#endif /* __UMP_UKK_H__ */
