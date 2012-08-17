/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_DLBU_H__
#define __MALI_DLBU_H__

#include "mali_osk.h"
#include "mali_group.h"

#define MALI_DLB_VIRT_ADDR 0xFFF00000 /* master tile virtual address fixed at this value and mapped into every session */

extern u32 mali_dlbu_phys_addr;

struct mali_dlbu_core;

_mali_osk_errcode_t mali_dlbu_initialize(void);
void mali_dlbu_terminate(void);

struct mali_dlbu_core *mali_dlbu_create(const _mali_osk_resource_t * resource);
void mali_dlbu_delete(struct mali_dlbu_core *dlbu);

void mali_dlbu_enable(struct mali_dlbu_core *dlbu);
void mali_dlbu_disable(struct mali_dlbu_core *dlbu);

_mali_osk_errcode_t mali_dlbu_enable_pp_core(struct mali_dlbu_core *dlbu, u32 pp_core_enable, u32 val);
void mali_dlbu_enable_all_pp_cores(struct mali_dlbu_core *dlbu);
void mali_dlbu_disable_all_pp_cores(struct mali_dlbu_core *dlbu);

_mali_osk_errcode_t mali_dlbu_reset(struct mali_dlbu_core *dlbu);
void mali_dlbu_setup(struct mali_dlbu_core *dlbu, u8 fb_xdim, u8 fb_ydim, u8 xtilesize, u8 ytilesize, u8 blocksize, u8 xgr0, u8 ygr0, u8 xgr1, u8 ygr1);

_mali_osk_errcode_t mali_dlbu_add_group(struct mali_dlbu_core *dlbu, struct mali_group *group);
void mali_dlbu_set_tllist_base_address(struct mali_dlbu_core *dlbu, u32 val);

void mali_dlbu_pp_jobs_stop(struct mali_dlbu_core *dlbu);
void mali_dlbu_pp_jobs_restart(struct mali_dlbu_core *dlbu);

#endif /* __MALI_DLBU_H__ */
