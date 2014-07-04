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
 * @file ump_ukk.h
 * Defines the kernel-side interface of the user-kernel interface
 */

#ifndef __UMP_UKK_H__
#define __UMP_UKK_H__

#include "mali_osk.h"
#include "ump_uk_types.h"


#ifdef __cplusplus
extern "C" {
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
