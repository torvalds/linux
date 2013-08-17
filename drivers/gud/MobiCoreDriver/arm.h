/*
 * MobiCore driver module.(interface to the secure world SWD)
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 * <-- Copyright Trustonic Limited 2013 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MC_ARM_H_
#define _MC_ARM_H_

#include "debug.h"

/*
 * ARM Trustzone specific masks and modes
 * Vanilla Linux is unaware of TrustZone extension.
 * I.e. arch/arm/include/asm/ptrace.h does not define monitor mode.
 * Also TZ bits in cpuid are not defined, ARM port uses magic numbers,
 * see arch/arm/kernel/setup.c
 */
#define ARM_MONITOR_MODE		(0b10110)
#define ARM_SECURITY_EXTENSION_MASK	(0x30)

/* check if CPU supports the ARM TrustZone Security Extensions */
inline bool has_security_extensions(void)
{
	u32 fea = 0;
	asm volatile(
		"mrc p15, 0, %[fea], cr0, cr1, 0" :
		[fea]"=r" (fea));

	MCDRV_DBG_VERBOSE(mcd, "CPU Features: 0x%X", fea);

	/*
	 * If the CPU features ID has 0 for security features then the CPU
	 * doesn't support TrustZone at all!
	 */
	if ((fea & ARM_SECURITY_EXTENSION_MASK) == 0)
		return false;

	return true;
}

/* check if running in secure mode */
inline bool is_secure_mode(void)
{
	u32 cpsr = 0;
	u32 nsacr = 0;

	asm volatile(
		"mrc	p15, 0, %[nsacr], cr1, cr1, 2\n"
		"mrs %[cpsr], cpsr\n" :
		[nsacr]"=r" (nsacr),
		[cpsr]"=r"(cpsr));

	MCDRV_DBG_VERBOSE(mcd, "CPRS.M = set to 0x%X\n", cpsr & MODE_MASK);
	MCDRV_DBG_VERBOSE(mcd, "SCR.NS = set to 0x%X\n", nsacr);

	/*
	 * If the NSACR contains the reset value(=0) then most likely we are
	 * running in Secure MODE.
	 * If the cpsr mode is set to monitor mode then we cannot load!
	 */
	if (nsacr == 0 || ((cpsr & MODE_MASK) == ARM_MONITOR_MODE))
		return true;

	return false;
}

#endif /* _MC_ARM_H_ */
