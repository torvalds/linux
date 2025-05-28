/* SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-3-Clause) */
/*
 * Copyright (c) 2014-2025, Advanced Micro Devices, Inc.
 * Copyright (c) 2014, Synopsys, Inc.
 * All rights reserved
 *
 * Author: Raju Rangoju <Raju.Rangoju@amd.com>
 */

#ifndef __SMN_H__
#define __SMN_H__

#ifdef CONFIG_AMD_NB

#include <asm/amd/nb.h>

#else

static inline int amd_smn_write(u16 node, u32 address, u32 value)
{
	return -ENODEV;
}

static inline int amd_smn_read(u16 node, u32 address, u32 *value)
{
	return -ENODEV;
}

#endif
#endif
