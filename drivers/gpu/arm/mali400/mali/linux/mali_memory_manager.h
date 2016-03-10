/*
 * Copyright (C) 2013-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_MEMORY_MANAGER_H__
#define __MALI_MEMORY_MANAGER_H__

#include "mali_osk.h"
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include "mali_memory_types.h"
#include "mali_memory_os_alloc.h"
#include "mali_uk_types.h"

struct mali_allocation_manager {
	rwlock_t vm_lock;
	struct rb_root allocation_mgr_rb;
	struct list_head head;
	struct mutex list_mutex;
	u32 mali_allocation_num;
};

extern struct idr mali_backend_idr;
extern struct mutex mali_idr_mutex;

int mali_memory_manager_init(struct mali_allocation_manager *mgr);
void mali_memory_manager_uninit(struct mali_allocation_manager *mgr);

void  mali_mem_allocation_struct_destory(mali_mem_allocation *alloc);
_mali_osk_errcode_t mali_mem_add_mem_size(struct mali_session_data *session, u32 mali_addr, u32 add_size);
mali_mem_backend *mali_mem_backend_struct_search(struct mali_session_data *session, u32 mali_address);
_mali_osk_errcode_t _mali_ukk_mem_allocate(_mali_uk_alloc_mem_s *args);
_mali_osk_errcode_t _mali_ukk_mem_free(_mali_uk_free_mem_s *args);
_mali_osk_errcode_t _mali_ukk_mem_bind(_mali_uk_bind_mem_s *args);
_mali_osk_errcode_t _mali_ukk_mem_unbind(_mali_uk_unbind_mem_s *args);
_mali_osk_errcode_t _mali_ukk_mem_cow(_mali_uk_cow_mem_s *args);
_mali_osk_errcode_t _mali_ukk_mem_cow_modify_range(_mali_uk_cow_modify_range_s *args);
_mali_osk_errcode_t _mali_ukk_mem_usage_get(_mali_uk_profiling_memory_usage_get_s *args);
_mali_osk_errcode_t _mali_ukk_mem_resize(_mali_uk_mem_resize_s *args);

#endif

