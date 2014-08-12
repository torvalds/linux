/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2010, 2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump_kernel_interface.h
 */

#ifndef __UMP_KERNEL_INTERFACE_REF_DRV_H__
#define __UMP_KERNEL_INTERFACE_REF_DRV_H__

#include "ump_kernel_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Turn specified physical memory into UMP memory. */
UMP_KERNEL_API_EXPORT ump_dd_handle ump_dd_handle_create_from_phys_blocks(ump_dd_physical_block *blocks, unsigned long num_blocks);

#ifdef __cplusplus
}
#endif

#endif  /* __UMP_KERNEL_INTERFACE_REF_DRV_H__ */
