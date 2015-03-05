/*
 * Copyright (C) 2012-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_DLBU_H__
#define __MALI_DLBU_H__

#define MALI_DLBU_VIRT_ADDR 0xFFF00000 /* master tile virtual address fixed at this value and mapped into every session */

#include "mali_osk.h"

struct mali_pp_job;
struct mali_group;
struct mali_dlbu_core;

extern mali_dma_addr mali_dlbu_phys_addr;

_mali_osk_errcode_t mali_dlbu_initialize(void);
void mali_dlbu_terminate(void);

struct mali_dlbu_core *mali_dlbu_create(const _mali_osk_resource_t *resource);
void mali_dlbu_delete(struct mali_dlbu_core *dlbu);

_mali_osk_errcode_t mali_dlbu_reset(struct mali_dlbu_core *dlbu);

void mali_dlbu_add_group(struct mali_dlbu_core *dlbu, struct mali_group *group);
void mali_dlbu_remove_group(struct mali_dlbu_core *dlbu, struct mali_group *group);

/** @brief Called to update HW after DLBU state changed
 *
 * This function must be called after \a mali_dlbu_add_group or \a
 * mali_dlbu_remove_group to write the updated mask to hardware, unless the
 * same is accomplished by calling \a mali_dlbu_reset.
 */
void mali_dlbu_update_mask(struct mali_dlbu_core *dlbu);

void mali_dlbu_config_job(struct mali_dlbu_core *dlbu, struct mali_pp_job *job);

#endif /* __MALI_DLBU_H__ */
