/*
 * Copyright (C) 2010-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_UTGARD_IOCTL_H__
#define __MALI_UTGARD_IOCTL_H__

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/fs.h>       /* file system operations */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file mali_kernel_ioctl.h
 * Interface to the Linux device driver.
 * This file describes the interface needed to use the Linux device driver.
 * Its interface is designed to used by the HAL implementation through a thin arch layer.
 */

/**
 * ioctl commands
 */

#define MALI_IOC_BASE           0x82
#define MALI_IOC_CORE_BASE      (_MALI_UK_CORE_SUBSYSTEM      + MALI_IOC_BASE)
#define MALI_IOC_MEMORY_BASE    (_MALI_UK_MEMORY_SUBSYSTEM    + MALI_IOC_BASE)
#define MALI_IOC_PP_BASE        (_MALI_UK_PP_SUBSYSTEM        + MALI_IOC_BASE)
#define MALI_IOC_GP_BASE        (_MALI_UK_GP_SUBSYSTEM        + MALI_IOC_BASE)
#define MALI_IOC_PROFILING_BASE (_MALI_UK_PROFILING_SUBSYSTEM + MALI_IOC_BASE)
#define MALI_IOC_VSYNC_BASE     (_MALI_UK_VSYNC_SUBSYSTEM + MALI_IOC_BASE)

#define MALI_IOC_WAIT_FOR_NOTIFICATION      _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_WAIT_FOR_NOTIFICATION, _mali_uk_wait_for_notification_s)
#define MALI_IOC_GET_API_VERSION            _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_GET_API_VERSION, u32)
#define MALI_IOC_GET_API_VERSION_V2         _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_GET_API_VERSION, _mali_uk_get_api_version_v2_s)
/* rk_ext. */
#define MALI_IOC_GET_RK_KO_VERSION          _IOWR(MALI_IOC_CORE_BASE, _MALI_GET_RK_KO_VERSION, _mali_rk_ko_version_s)
#define MALI_IOC_POST_NOTIFICATION          _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_POST_NOTIFICATION, _mali_uk_post_notification_s)
#define MALI_IOC_GET_USER_SETTING           _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_GET_USER_SETTING, _mali_uk_get_user_setting_s)
#define MALI_IOC_GET_USER_SETTINGS          _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_GET_USER_SETTINGS, _mali_uk_get_user_settings_s)
#define MALI_IOC_REQUEST_HIGH_PRIORITY      _IOW (MALI_IOC_CORE_BASE, _MALI_UK_REQUEST_HIGH_PRIORITY, _mali_uk_request_high_priority_s)
#define MALI_IOC_TIMELINE_GET_LATEST_POINT  _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_TIMELINE_GET_LATEST_POINT, _mali_uk_timeline_get_latest_point_s)
#define MALI_IOC_TIMELINE_WAIT              _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_TIMELINE_WAIT, _mali_uk_timeline_wait_s)
#define MALI_IOC_TIMELINE_CREATE_SYNC_FENCE _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_TIMELINE_CREATE_SYNC_FENCE, _mali_uk_timeline_create_sync_fence_s)
#define MALI_IOC_SOFT_JOB_START             _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_SOFT_JOB_START, _mali_uk_soft_job_start_s)
#define MALI_IOC_SOFT_JOB_SIGNAL            _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_SOFT_JOB_SIGNAL, _mali_uk_soft_job_signal_s)
#define MALI_IOC_PENDING_SUBMIT             _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_PENDING_SUBMIT, _mali_uk_pending_submit_s)

#define MALI_IOC_MEM_ALLOC                  _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_ALLOC_MEM, _mali_uk_alloc_mem_s)
#define MALI_IOC_MEM_FREE                   _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_FREE_MEM, _mali_uk_free_mem_s)
#define MALI_IOC_MEM_BIND                   _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_BIND_MEM, _mali_uk_bind_mem_s)
#define MALI_IOC_MEM_UNBIND                 _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_UNBIND_MEM, _mali_uk_unbind_mem_s)
#define MALI_IOC_MEM_COW                    _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_COW_MEM, _mali_uk_cow_mem_s)
#define MALI_IOC_MEM_COW_MODIFY_RANGE       _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_COW_MODIFY_RANGE, _mali_uk_cow_modify_range_s)
#define MALI_IOC_MEM_RESIZE                 _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_RESIZE_MEM, _mali_uk_mem_resize_s)
#define MALI_IOC_MEM_DMA_BUF_GET_SIZE       _IOR(MALI_IOC_MEMORY_BASE, _MALI_UK_DMA_BUF_GET_SIZE, _mali_uk_dma_buf_get_size_s)
#define MALI_IOC_MEM_QUERY_MMU_PAGE_TABLE_DUMP_SIZE _IOR (MALI_IOC_MEMORY_BASE, _MALI_UK_QUERY_MMU_PAGE_TABLE_DUMP_SIZE, _mali_uk_query_mmu_page_table_dump_size_s)
#define MALI_IOC_MEM_DUMP_MMU_PAGE_TABLE    _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_DUMP_MMU_PAGE_TABLE, _mali_uk_dump_mmu_page_table_s)
#define MALI_IOC_MEM_WRITE_SAFE             _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_MEM_WRITE_SAFE, _mali_uk_mem_write_safe_s)

#define MALI_IOC_PP_START_JOB               _IOWR(MALI_IOC_PP_BASE, _MALI_UK_PP_START_JOB, _mali_uk_pp_start_job_s)
#define MALI_IOC_PP_AND_GP_START_JOB        _IOWR(MALI_IOC_PP_BASE, _MALI_UK_PP_AND_GP_START_JOB, _mali_uk_pp_and_gp_start_job_s)
#define MALI_IOC_PP_NUMBER_OF_CORES_GET     _IOR (MALI_IOC_PP_BASE, _MALI_UK_GET_PP_NUMBER_OF_CORES, _mali_uk_get_pp_number_of_cores_s)
#define MALI_IOC_PP_CORE_VERSION_GET        _IOR (MALI_IOC_PP_BASE, _MALI_UK_GET_PP_CORE_VERSION, _mali_uk_get_pp_core_version_s)
#define MALI_IOC_PP_DISABLE_WB              _IOW (MALI_IOC_PP_BASE, _MALI_UK_PP_DISABLE_WB, _mali_uk_pp_disable_wb_s)

#define MALI_IOC_GP2_START_JOB              _IOWR(MALI_IOC_GP_BASE, _MALI_UK_GP_START_JOB, _mali_uk_gp_start_job_s)
#define MALI_IOC_GP2_NUMBER_OF_CORES_GET    _IOR (MALI_IOC_GP_BASE, _MALI_UK_GET_GP_NUMBER_OF_CORES, _mali_uk_get_gp_number_of_cores_s)
#define MALI_IOC_GP2_CORE_VERSION_GET       _IOR (MALI_IOC_GP_BASE, _MALI_UK_GET_GP_CORE_VERSION, _mali_uk_get_gp_core_version_s)
#define MALI_IOC_GP2_SUSPEND_RESPONSE       _IOW (MALI_IOC_GP_BASE, _MALI_UK_GP_SUSPEND_RESPONSE,_mali_uk_gp_suspend_response_s)

#define MALI_IOC_PROFILING_ADD_EVENT        _IOWR(MALI_IOC_PROFILING_BASE, _MALI_UK_PROFILING_ADD_EVENT, _mali_uk_profiling_add_event_s)
#define MALI_IOC_PROFILING_REPORT_SW_COUNTERS  _IOW (MALI_IOC_PROFILING_BASE, _MALI_UK_PROFILING_REPORT_SW_COUNTERS, _mali_uk_sw_counters_report_s)
#define MALI_IOC_PROFILING_MEMORY_USAGE_GET _IOR(MALI_IOC_PROFILING_BASE, _MALI_UK_PROFILING_MEMORY_USAGE_GET, _mali_uk_profiling_memory_usage_get_s)
#define MALI_IOC_PROFILING_STREAM_FD_GET        _IOR(MALI_IOC_PROFILING_BASE, _MALI_UK_PROFILING_STREAM_FD_GET, _mali_uk_profiling_stream_fd_get_s)
#define MALI_IOC_PROILING_CONTROL_SET   _IOR(MALI_IOC_PROFILING_BASE, _MALI_UK_PROFILING_CONTROL_SET, _mali_uk_profiling_control_set_s)

#define MALI_IOC_VSYNC_EVENT_REPORT         _IOW (MALI_IOC_VSYNC_BASE, _MALI_UK_VSYNC_EVENT_REPORT, _mali_uk_vsync_event_report_s)

/* rk_ext : 对 r5p0 集成之后, mali_so 不再使用下面的 ioctl, 而使用 MALI_IOC_GET_RK_KO_VERSION. */
#if 0
#define MALI_IOC_GET_MALI_VERSION_IN_RK30   _IOWR(MALI_IOC_CORE_BASE,_MALI_UK_GET_MALI_VERSION_IN_RK30,_mali_uk_get_mali_version_in_rk30_s *)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __MALI_UTGARD_IOCTL_H__ */
