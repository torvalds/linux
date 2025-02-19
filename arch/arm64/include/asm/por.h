/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Arm Ltd.
 */

#ifndef _ASM_ARM64_POR_H
#define _ASM_ARM64_POR_H

#include <asm/sysreg.h>

#define POR_EL0_INIT	POR_ELx_PERM_PREP(0, POE_RWX)

static inline bool por_elx_allows_read(u64 por, u8 pkey)
{
	u8 perm = POR_ELx_PERM_GET(pkey, por);

	return perm & POE_R;
}

static inline bool por_elx_allows_write(u64 por, u8 pkey)
{
	u8 perm = POR_ELx_PERM_GET(pkey, por);

	return perm & POE_W;
}

static inline bool por_elx_allows_exec(u64 por, u8 pkey)
{
	u8 perm = POR_ELx_PERM_GET(pkey, por);

	return perm & POE_X;
}

#endif /* _ASM_ARM64_POR_H */
