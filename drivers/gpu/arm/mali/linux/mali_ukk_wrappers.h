/*
 * Copyright (C) 2010-2016 ARM Limited. All rights reserved.
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
extern "C" {
#endif

int wait_for_notification_wrapper(struct mali_session_data *session_data, _mali_uk_wait_for_notification_s __user *uargs);
int get_api_version_wrapper(struct mali_session_data *session_data, _mali_uk_get_api_version_s __user *uargs);
int get_api_version_v2_wrapper(struct mali_session_data *session_data, _mali_uk_get_api_version_v2_s __user *uargs);
int get_user_settings_wrapper(struct mali_session_data *session_data, _mali_uk_get_user_settings_s __user *uargs);
int post_notification_wrapper(struct mali_session_data *session_data, _mali_uk_post_notification_s __user *uargs);
int request_high_priority_wrapper(struct mali_session_data *session_data, _mali_uk_request_high_priority_s __user *uargs);
int pending_submit_wrapper(struct mali_session_data *session_data, _mali_uk_pending_submit_s __user *uargs);

int mem_alloc_wrapper(struct mali_session_data *session_data, _mali_uk_alloc_mem_s __user *uargs);
int mem_free_wrapper(struct mali_session_data *session_data, _mali_uk_free_mem_s __user *uargs);
int mem_bind_wrapper(struct mali_session_data *session_data, _mali_uk_bind_mem_s __user *uargs);
int mem_unbind_wrapper(struct mali_session_data *session_data, _mali_uk_unbind_mem_s __user *uargs);
int mem_cow_wrapper(struct mali_session_data *session_data, _mali_uk_cow_mem_s __user *uargs);
int mem_cow_modify_range_wrapper(struct mali_session_data *session_data, _mali_uk_cow_modify_range_s __user *uargs);
int mem_resize_mem_wrapper(struct mali_session_data *session_data, _mali_uk_mem_resize_s __user *uargs);
int mem_write_safe_wrapper(struct mali_session_data *session_data, _mali_uk_mem_write_safe_s __user *uargs);
int mem_query_mmu_page_table_dump_size_wrapper(struct mali_session_data *session_data, _mali_uk_query_mmu_page_table_dump_size_s __user *uargs);
int mem_dump_mmu_page_table_wrapper(struct mali_session_data *session_data, _mali_uk_dump_mmu_page_table_s __user *uargs);
int mem_usage_get_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_memory_usage_get_s __user *uargs);

int timeline_get_latest_point_wrapper(struct mali_session_data *session, _mali_uk_timeline_get_latest_point_s __user *uargs);
int timeline_wait_wrapper(struct mali_session_data *session, _mali_uk_timeline_wait_s __user *uargs);
int timeline_create_sync_fence_wrapper(struct mali_session_data *session, _mali_uk_timeline_create_sync_fence_s __user *uargs);
int soft_job_start_wrapper(struct mali_session_data *session, _mali_uk_soft_job_start_s __user *uargs);
int soft_job_signal_wrapper(struct mali_session_data *session, _mali_uk_soft_job_signal_s __user *uargs);
int pp_start_job_wrapper(struct mali_session_data *session_data, _mali_uk_pp_start_job_s __user *uargs);
int pp_and_gp_start_job_wrapper(struct mali_session_data *session_data, _mali_uk_pp_and_gp_start_job_s __user *uargs);
int pp_get_number_of_cores_wrapper(struct mali_session_data *session_data, _mali_uk_get_pp_number_of_cores_s __user *uargs);
int pp_get_core_version_wrapper(struct mali_session_data *session_data, _mali_uk_get_pp_core_version_s __user *uargs);
int pp_disable_wb_wrapper(struct mali_session_data *session_data, _mali_uk_pp_disable_wb_s __user *uargs);
int gp_start_job_wrapper(struct mali_session_data *session_data, _mali_uk_gp_start_job_s __user *uargs);
int gp_get_number_of_cores_wrapper(struct mali_session_data *session_data, _mali_uk_get_gp_number_of_cores_s __user *uargs);
int gp_get_core_version_wrapper(struct mali_session_data *session_data, _mali_uk_get_gp_core_version_s __user *uargs);
int gp_suspend_response_wrapper(struct mali_session_data *session_data, _mali_uk_gp_suspend_response_s __user *uargs);

int profiling_add_event_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_add_event_s __user *uargs);
int profiling_report_sw_counters_wrapper(struct mali_session_data *session_data, _mali_uk_sw_counters_report_s __user *uargs);
int profiling_get_stream_fd_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_stream_fd_get_s __user *uargs);
int profiling_control_set_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_control_set_s __user *uargs);

int vsync_event_report_wrapper(struct mali_session_data *session_data, _mali_uk_vsync_event_report_s __user *uargs);


int map_errcode(_mali_osk_errcode_t err);

#ifdef __cplusplus
}
#endif

#endif /* __MALI_UKK_WRAPPERS_H__ */
