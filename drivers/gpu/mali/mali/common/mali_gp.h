/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_GP_H__
#define __MALI_GP_H__

#include "mali_osk.h"
#include "mali_gp_job.h"

struct mali_gp_core;
struct mali_group;

_mali_osk_errcode_t mali_gp_initialize(void);
void mali_gp_terminate(void);

struct mali_gp_core *mali_gp_create(const _mali_osk_resource_t * resource, struct mali_group *group);
void mali_gp_delete(struct mali_gp_core *core);

void mali_gp_stop_bus(struct mali_gp_core *core);
_mali_osk_errcode_t mali_gp_stop_bus_wait(struct mali_gp_core *core);
void mali_gp_hard_reset(struct mali_gp_core *core);
_mali_osk_errcode_t mali_gp_reset(struct mali_gp_core *core);

void mali_gp_job_start(struct mali_gp_core *core, struct mali_gp_job *job);
void mali_gp_resume_with_new_heap(struct mali_gp_core *core, u32 start_addr, u32 end_addr);

void mali_gp_abort_job(struct mali_gp_core *core);

u32 mali_gp_core_get_version(struct mali_gp_core *core);

mali_bool mali_gp_core_set_counter_src0(struct mali_gp_core *core, u32 counter);
mali_bool mali_gp_core_set_counter_src1(struct mali_gp_core *core, u32 counter);
u32 mali_gp_core_get_counter_src0(struct mali_gp_core *core);
u32 mali_gp_core_get_counter_src1(struct mali_gp_core *core);
struct mali_gp_core *mali_gp_get_global_gp_core(void);

u32 mali_gp_dump_state(struct mali_gp_core *core, char *buf, u32 size);

#endif /* __MALI_GP_H__ */
