/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_ukk_wrappers.h
 * Defines the wrapper functions for each user-kernel function
 */

#ifndef __MALI_UKK_WRAPPERS_H__
#define __MALI_UKK_WRAPPERS_H__

#include "mali_uk_types.h"
#include "mali_osk.h"

#ifdef __cplusplus
extern "C"
{
#endif

int wait_for_notification_wrapper(struct mali_session_data *session_data, _mali_uk_wait_for_notification_s __user *uargs);
int get_api_version_wrapper(struct mali_session_data *session_data, _mali_uk_get_api_version_s __user *uargs);
int get_user_settings_wrapper(struct mali_session_data *session_data, _mali_uk_get_user_settings_s __user *uargs);
#if defined(CONFIG_SYNC)
/* MALI_SEC */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
int stream_create_wrapper(struct mali_session_data *session_data, _mali_uk_stream_create_s __user *uargs);
int sync_fence_validate_wrapper(struct mali_session_data *session, _mali_uk_fence_validate_s __user *uargs);
#endif
#endif
int post_notification_wrapper(struct mali_session_data *session_data, _mali_uk_post_notification_s __user *uargs);
int mem_init_wrapper(struct mali_session_data *session_data, _mali_uk_init_mem_s __user *uargs);
int mem_term_wrapper(struct mali_session_data *session_data, _mali_uk_term_mem_s __user *uargs);
int mem_map_ext_wrapper(struct mali_session_data *session_data, _mali_uk_map_external_mem_s __user * argument);
int mem_unmap_ext_wrapper(struct mali_session_data *session_data, _mali_uk_unmap_external_mem_s __user * argument);
int mem_query_mmu_page_table_dump_size_wrapper(struct mali_session_data *session_data, _mali_uk_query_mmu_page_table_dump_size_s __user * uargs);
int mem_dump_mmu_page_table_wrapper(struct mali_session_data *session_data, _mali_uk_dump_mmu_page_table_s __user * uargs);

#if defined(CONFIG_MALI400_UMP)
int mem_attach_ump_wrapper(struct mali_session_data *session_data, _mali_uk_attach_ump_mem_s __user * argument);
int mem_release_ump_wrapper(struct mali_session_data *session_data, _mali_uk_release_ump_mem_s __user * argument);
#endif

int pp_start_job_wrapper(struct mali_session_data *session_data, _mali_uk_pp_start_job_s __user *uargs);
int pp_get_number_of_cores_wrapper(struct mali_session_data *session_data, _mali_uk_get_pp_number_of_cores_s __user *uargs);
int pp_get_core_version_wrapper(struct mali_session_data *session_data, _mali_uk_get_pp_core_version_s __user *uargs);
int pp_disable_wb_wrapper(struct mali_session_data *session_data, _mali_uk_pp_disable_wb_s __user *uargs);
int gp_start_job_wrapper(struct mali_session_data *session_data, _mali_uk_gp_start_job_s __user *uargs);
int gp_get_number_of_cores_wrapper(struct mali_session_data *session_data, _mali_uk_get_gp_number_of_cores_s __user *uargs);
int gp_get_core_version_wrapper(struct mali_session_data *session_data, _mali_uk_get_gp_core_version_s __user *uargs);
int gp_suspend_response_wrapper(struct mali_session_data *session_data, _mali_uk_gp_suspend_response_s __user *uargs);

int profiling_start_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_start_s __user *uargs);
int profiling_add_event_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_add_event_s __user *uargs);
int profiling_stop_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_stop_s __user *uargs);
int profiling_get_event_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_get_event_s __user *uargs);
int profiling_clear_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_clear_s __user *uargs);
int profiling_report_sw_counters_wrapper(struct mali_session_data *session_data, _mali_uk_sw_counters_report_s __user *uargs);

int vsync_event_report_wrapper(struct mali_session_data *session_data, _mali_uk_vsync_event_report_s __user *uargs);


int map_errcode( _mali_osk_errcode_t err );

#ifdef __cplusplus
}
#endif

#endif /* __MALI_UKK_WRAPPERS_H__ */
