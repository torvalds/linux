/*
 *  Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_IO_H__
#define __ASM_ARCH_MXC_IO_H__

/* Allow IO space to be anywhere in the memory */
#define IO_SPACE_LIMIT 0xffffffff

#if defined(CONFIG_SOC_IMX31) || defined(CONFIG_SOC_IMX35)
#include <mach/hardware.h>

#define __arch_ioremap __imx_ioremap
#define __arch_iounmap __iounmap

#define addr_in_module(addr, mod) \
	((unsigned long)(addr) - mod ## _BASE_ADDR < mod ## _SIZE)

static inline void __iomem *
__imx_ioremap(unsigned long phys_addr, size_t size, unsigned int mtype)
{
	if (mtype == MT_DEVICE && (cpu_is_mx31() || cpu_is_mx35())) {
		/*
		 * Access all peripherals below 0x80000000 as nonshared device
		 * on mx3, but leave l2cc alone.  Otherwise cache corruptions
		 * can occur.
		 */
		if (phys_addr < 0x80000000 &&
				!addr_in_module(phys_addr, MX3x_L2CC))
			mtype = MT_DEVICE_NONSHARED;
	}

	return __arm_ioremap(phys_addr, size, mtype);
}
#endif

/* io address mapping macro */
#define __io(a)		__typesafe_io(a)

#define __mem_pci(a)	(a)

#endif
