/*
 * Copyright (C) 2013-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef __MALI_GPU_VMEM_H__
#define __MALI_GPU_VMEM_H__

#include "mali_osk.h"
#include "mali_session.h"
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include "mali_memory_types.h"
#include "mali_memory_os_alloc.h"
#include "mali_memory_manager.h"



int mali_vma_offset_add(struct mali_allocation_manager *mgr,
			struct mali_vma_node *node);

void mali_vma_offset_remove(struct mali_allocation_manager *mgr,
			    struct mali_vma_node *node);

struct mali_vma_node *mali_vma_offset_search(struct mali_allocation_manager *mgr,
		unsigned long start,    unsigned long pages);

#endif
