/*
 * arch/arm/include/asm/arm-cci.h
 *
 * Copyright (C) 2015 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
