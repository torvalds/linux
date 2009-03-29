/*
 * arch/arm/mach-ixp4xx/include/mach/cpu.h
 *
 * IXP4XX cpu type detection
 *
 * Copyright (C) 2007 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ASM_ARCH_CPU_H__
#define __ASM_ARCH_CPU_H__

#include <asm/cputype.h>

/* Processor id value in CP15 Register 0 */
#define IXP42X_PROCESSOR_ID_VALUE	0x690541c0 /* including unused 0x690541Ex */
#define IXP42X_PROCESSOR_ID_MASK	0xffffffc0

#define IXP43X_PROCESSOR_ID_VALUE	0x69054040
#define IXP43X_PROCESSOR_ID_MASK	0xfffffff0

#define IXP46X_PROCESSOR_ID_VALUE	0x69054200 /* including IXP455 */
#define IXP46X_PROCESSOR_ID_MASK	0xfffffff0

#define cpu_is_ixp42x()	((read_cpuid_id() & IXP42X_PROCESSOR_ID_MASK) == \
			 IXP42X_PROCESSOR_ID_VALUE)
#define cpu_is_ixp43x()	((read_cpuid_id() & IXP43X_PROCESSOR_ID_MASK) == \
			 IXP43X_PROCESSOR_ID_VALUE)
#define cpu_is_ixp46x()	((read_cpuid_id() & IXP46X_PROCESSOR_ID_MASK) == \
			 IXP46X_PROCESSOR_ID_VALUE)

static inline u32 ixp4xx_read_feature_bits(void)
{
	unsigned int val = ~*IXP4XX_EXP_CFG2;

	if (cpu_is_ixp42x())
		return val & IXP42X_FEATURE_MASK;
	if (cpu_is_ixp43x())
		return val & IXP43X_FEATURE_MASK;
	return val & IXP46X_FEATURE_MASK;
}

static inline void ixp4xx_write_feature_bits(u32 value)
{
	*IXP4XX_EXP_CFG2 = ~value;
}

#endif  /* _ASM_ARCH_CPU_H */
