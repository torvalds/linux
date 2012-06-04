/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_IOCTL_H__
#define __MALI_KERNEL_IOCTL_H__

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/fs.h>       /* file system operations */

#ifdef __cplusplus
extern "C"
{
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

#define MALI_IOC_GET_SYSTEM_INFO_SIZE       _IOR (MALI_IOC_CORE_BASE, _MALI_UK_GET_SYSTEM_INFO_SIZE, _mali_uk_get_system_info_s *)
#define MALI_IOC_GET_SYSTEM_INFO            _IOR (MALI_IOC_CORE_BASE, _MALI_UK_GET_SYSTEM_INFO, _mali_uk_get_system_info_s *)
#define MALI_IOC_WAIT_FOR_NOTIFICATION      _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_WAIT_FOR_NOTIFICATION, _mali_uk_wait_for_notification_s *)
#define MALI_IOC_GET_API_VERSION            _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_GET_API_VERSION, _mali_uk_get_api_version_s *)
#define MALI_IOC_POST_NOTIFICATION          _IOWR(MALI_IOC_CORE_BASE, _MALI_UK_POST_NOTIFICATION, _mali_uk_post_notification_s *)
#define MALI_IOC_MEM_GET_BIG_BLOCK          _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_GET_BIG_BLOCK, _mali_uk_get_big_block_s *)
#define MALI_IOC_MEM_FREE_BIG_BLOCK         _IOW (MALI_IOC_MEMORY_BASE, _MALI_UK_FREE_BIG_BLOCK, _mali_uk_free_big_block_s *)
#define MALI_IOC_MEM_INIT                   _IOR (MALI_IOC_MEMORY_BASE, _MALI_UK_INIT_MEM, _mali_uk_init_mem_s *)
#define MALI_IOC_MEM_TERM                   _IOW (MALI_IOC_MEMORY_BASE, _MALI_UK_TERM_MEM, _mali_uk_term_mem_s *)
#define MALI_IOC_MEM_MAP_EXT                _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_MAP_EXT_MEM, _mali_uk_map_external_mem_s *)
#define MALI_IOC_MEM_UNMAP_EXT              _IOW (MALI_IOC_MEMORY_BASE, _MALI_UK_UNMAP_EXT_MEM, _mali_uk_unmap_external_mem_s *)
#define MALI_IOC_MEM_QUERY_MMU_PAGE_TABLE_DUMP_SIZE _IOR (MALI_IOC_MEMORY_BASE, _MALI_UK_QUERY_MMU_PAGE_TABLE_DUMP_SIZE, _mali_uk_query_mmu_page_table_dump_size_s *)
#define MALI_IOC_MEM_DUMP_MMU_PAGE_TABLE    _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_DUMP_MMU_PAGE_TABLE, _mali_uk_dump_mmu_page_table_s *)
#define MALI_IOC_MEM_ATTACH_UMP             _IOWR(MALI_IOC_MEMORY_BASE, _MALI_UK_ATTACH_UMP_MEM, _mali_uk_attach_ump_mem_s *)
#define MALI_IOC_MEM_RELEASE_UMP            _IOW(MALI_IOC_MEMORY_BASE, _MALI_UK_RELEASE_UMP_MEM, _mali_uk_release_ump_mem_s *)
#define MALI_IOC_PP_START_JOB               _IOWR(MALI_IOC_PP_BASE, _MALI_UK_PP_START_JOB, _mali_uk_pp_start_job_s *)
#define MALI_IOC_PP_NUMBER_OF_CORES_GET	    _IOR (MALI_IOC_PP_BASE, _MALI_UK_GET_PP_NUMBER_OF_CORES, _mali_uk_get_pp_number_of_cores_s *)
#define MALI_IOC_PP_CORE_VERSION_GET	    _IOR (MALI_IOC_PP_BASE, _MALI_UK_GET_PP_CORE_VERSION, _mali_uk_get_pp_core_version_s * )
#define MALI_IOC_PP_ABORT_JOB	            _IOW (MALI_IOC_PP_BASE, _MALI_UK_PP_ABORT_JOB, _mali_uk_pp_abort_job_s * )
#define MALI_IOC_GP2_START_JOB              _IOWR(MALI_IOC_GP_BASE, _MALI_UK_GP_START_JOB, _mali_uk_gp_start_job_s *)
#define MALI_IOC_GP2_ABORT_JOB              _IOWR(MALI_IOC_GP_BASE, _MALI_UK_GP_ABORT_JOB, _mali_uk_gp_abort_job_s *)
#define MALI_IOC_GP2_NUMBER_OF_CORES_GET    _IOR (MALI_IOC_GP_BASE, _MALI_UK_GET_GP_NUMBER_OF_CORES, _mali_uk_get_gp_number_of_cores_s *)
#define MALI_IOC_GP2_CORE_VERSION_GET	    _IOR (MALI_IOC_GP_BASE, _MALI_UK_GET_GP_CORE_VERSION, _mali_uk_get_gp_core_version_s *)
#define MALI_IOC_GP2_SUSPEND_RESPONSE	    _IOW (MALI_IOC_GP_BASE, _MALI_UK_GP_SUSPEND_RESPONSE,_mali_uk_gp_suspend_response_s *)
#define MALI_IOC_PROFILING_START            _IOWR(MALI_IOC_PROFILING_BASE, _MALI_UK_PROFILING_START, _mali_uk_profiling_start_s *)
#define MALI_IOC_PROFILING_ADD_EVENT        _IOWR(MALI_IOC_PROFILING_BASE, _MALI_UK_PROFILING_ADD_EVENT, _mali_uk_profiling_add_event_s*)
#define MALI_IOC_PROFILING_STOP             _IOWR(MALI_IOC_PROFILING_BASE, _MALI_UK_PROFILING_STOP, _mali_uk_profiling_stop_s *)
#define MALI_IOC_PROFILING_GET_EVENT        _IOWR(MALI_IOC_PROFILING_BASE, _MALI_UK_PROFILING_GET_EVENT, _mali_uk_profiling_get_event_s *)
#define MALI_IOC_PROFILING_CLEAR            _IOWR(MALI_IOC_PROFILING_BASE, _MALI_UK_PROFILING_CLEAR, _mali_uk_profiling_clear_s *)
#define MALI_IOC_PROFILING_GET_CONFIG       _IOWR(MALI_IOC_PROFILING_BASE, _MALI_UK_PROFILING_GET_CONFIG, _mali_uk_profiling_get_config_s *)
#define MALI_IOC_VSYNC_EVENT_REPORT         _IOW (MALI_IOC_VSYNC_BASE, _MALI_UK_VSYNC_EVENT_REPORT, _mali_uk_vsync_event_report_s *)

#ifdef __cplusplus
}
#endif

#endif /* __MALI_KERNEL_IOCTL_H__ */
