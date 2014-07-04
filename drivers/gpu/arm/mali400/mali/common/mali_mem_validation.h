/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef __MALI_MEM_VALIDATION_H__
#define __MALI_MEM_VALIDATION_H__

#include "mali_osk.h"

_mali_osk_errcode_t mali_mem_validation_add_range(u32 start, u32 size);
_mali_osk_errcode_t mali_mem_validation_check(u32 phys_addr, u32 size);

#endif /* __MALI_MEM_VALIDATION_H__ */
