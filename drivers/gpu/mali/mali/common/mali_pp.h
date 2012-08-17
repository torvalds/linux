/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_PP_H__
#define __MALI_PP_H__

#include "mali_osk.h"
#include "mali_pp_job.h"

struct mali_pp_core;
struct mali_group;

_mali_osk_errcode_t mali_pp_initialize(void);
void mali_pp_terminate(void);

struct mali_pp_core *mali_pp_create(const _mali_osk_resource_t * resource, struct mali_group *group);
void mali_pp_delete(struct mali_pp_core *core);

void mali_pp_stop_bus(struct mali_pp_core *core);
_mali_osk_errcode_t mali_pp_stop_bus_wait(struct mali_pp_core *core);
_mali_osk_errcode_t mali_pp_reset(struct mali_pp_core *core);
_mali_osk_errcode_t mali_pp_hard_reset(struct mali_pp_core *core);

void mali_pp_job_start(struct mali_pp_core *core, struct mali_pp_job *job, u32 sub_job);

u32 mali_pp_core_get_version(struct mali_pp_core *core);

u32 mali_pp_core_get_id(struct mali_pp_core *core);

mali_bool mali_pp_core_set_counter_src0(struct mali_pp_core *core, u32 counter);
mali_bool mali_pp_core_set_counter_src1(struct mali_pp_core *core, u32 counter);
u32 mali_pp_core_get_counter_src0(struct mali_pp_core *core);
u32 mali_pp_core_get_counter_src1(struct mali_pp_core *core);
struct mali_pp_core* mali_pp_get_global_pp_core(u32 index);
u32 mali_pp_get_glob_num_pp_cores(void);
u32 mali_pp_get_max_num_pp_cores(void);
/* Debug */
u32 mali_pp_dump_state(struct mali_pp_core *core, char *buf, u32 size);

#endif /* __MALI_PP_H__ */
