/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_MMU_H__
#define __MALI_MMU_H__

#include "mali_osk.h"
#include "mali_mmu_page_directory.h"

/* Forward declaration from mali_group.h */
struct mali_group;

struct mali_mmu_core;

_mali_osk_errcode_t mali_mmu_initialize(void);

void mali_mmu_terminate(void);

struct mali_mmu_core *mali_mmu_create(_mali_osk_resource_t *resource);
void mali_mmu_set_group(struct mali_mmu_core *mmu, struct mali_group *group);
void mali_mmu_delete(struct mali_mmu_core *mmu);

_mali_osk_errcode_t mali_mmu_reset(struct mali_mmu_core *mmu);
mali_bool mali_mmu_zap_tlb(struct mali_mmu_core *mmu);
void mali_mmu_zap_tlb_without_stall(struct mali_mmu_core *mmu);
void mali_mmu_invalidate_page(struct mali_mmu_core *mmu, u32 mali_address);

mali_bool mali_mmu_activate_page_directory(struct mali_mmu_core* mmu, struct mali_page_directory *pagedir);
void mali_mmu_activate_empty_page_directory(struct mali_mmu_core* mmu);
void mali_mmu_activate_fault_flush_page_directory(struct mali_mmu_core* mmu);

/**
 * Issues the enable stall command to the MMU and waits for HW to complete the request
 * @param mmu The MMU to enable paging for
 * @return MALI_TRUE if HW stall was successfully engaged, otherwise MALI_FALSE (req timed out)
 */
mali_bool mali_mmu_enable_stall(struct mali_mmu_core *mmu);

/**
 * Issues the disable stall command to the MMU and waits for HW to complete the request
 * @param mmu The MMU to enable paging for
 */
void mali_mmu_disable_stall(struct mali_mmu_core *mmu);

void mali_mmu_page_fault_done(struct mali_mmu_core *mmu);


#endif /* __MALI_MMU_H__ */
