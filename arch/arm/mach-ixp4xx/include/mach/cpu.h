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

#include <linux/io.h>
#include <asm/cputype.h>

/* Processor id value in CP15 Register 0 */
#define IXP42X_PROCESSOR_ID_VALUE	0x690541c0 /* including unused 0x690541Ex */
#define IXP42X_PROCESSOR_ID_MASK	0xffffffc0

#define IXP43X_PROCESSOR_ID_VALUE	0x69054040
#define IXP43X_PROCESSOR_ID_MASK	0xfffffff0

#define IXP46X_PROCESSOR_ID_VALUE	0x69054200 /* including IXP455 */
#define IXP46X_PROCESSOR_ID_MASK	0xfffffff0

#define cpu_is_ixp42x_rev_a0() ((read_cpuid_id() & (IXP42X_PROCESSOR_ID_MASK | 0xF)) == \
				IXP42X_PROCESSOR_ID_VALUE)
#define cpu_is_ixp42x()	((read_cpuid_id() & IXP42X_PROCESSOR_ID_MASK) == \
			 IXP42X_PROCESSOR_ID_VALUE)
#define cpu_is_ixp43x()	((read_cpuid_id() & IXP43X_PROCESSOR_ID_MASK) == \
			 IXP43X_PROCESSOR_ID_VALUE)
#define cpu_is_ixp46x()	((read_cpuid_id() & IXP46X_PROCESSOR_ID_MASK) == \
			 IXP46X_PROCESSOR_ID_VALUE)

static inline u32 ixp4xx_read_feature_bits(void)
{
	u32 val = ~__raw_readl(IXP4XX_EXP_CFG2);

	if (cpu_is_ixp42x_rev_a0())
		return IXP42X_FEATURE_MASK & ~(IXP4XX_FEATURE_RCOMP |
					       IXP4XX_FEATURE_AES);
	if (cpu_is_ixp42x())
		return val & IXP42X_FEATURE_MASK;
	if (cpu_is_ixp43x())
		return val & IXP43X_FEATURE_MASK;
	return val & IXP46X_FEATURE_MASK;
}

static inline void ixp4xx_write_feature_bits(u32 value)
{
	__raw_writel(~value, IXP4XX_EXP_CFG2);
}

#endif  /* _ASM_ARCH_CPU_H */
