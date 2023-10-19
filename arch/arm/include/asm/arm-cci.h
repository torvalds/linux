/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/asm/arm-cci.h
 *
 * Copyright (C) 2015 ARM Ltd.
 */

#ifndef __ASM_ARM_CCI_H
#define __ASM_ARM_CCI_H

#ifdef CONFIG_MCPM
#include <asm/mcpm.h>

/*
 * We don't have a reliable way of detecting whether,
 * if we have access to secure-only registers, unless
 * mcpm is registered.
 */
static inline bool platform_has_secure_cci_access(void)
{
	return mcpm_is_available();
}

#else
static inline bool platform_has_secure_cci_access(void)
{
	return false;
}
#endif

#endif
