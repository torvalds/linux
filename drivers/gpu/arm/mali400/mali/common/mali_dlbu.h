/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2012-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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
