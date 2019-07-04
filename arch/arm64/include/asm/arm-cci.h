/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm64/include/asm/arm-cci.h
 *
 * Copyright (C) 2015 ARM Ltd.
 */

#ifndef __ASM_ARM_CCI_H
#define __ASM_ARM_CCI_H

static inline bool platform_has_secure_cci_access(void)
{
	return false;
}

#endif
