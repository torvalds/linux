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

#define __arch_ioremap __imx_ioremap
#define __arch_iounmap __iounmap

#define addr_in_module(addr, mod) \
	((unsigned long)(addr) - mod ## _BASE_ADDR < mod ## _SIZE)

extern void __iomem *(*imx_ioremap)(unsigned long, size_t, unsigned int);

static inline void __iomem *
__imx_ioremap(unsigned long phys_addr, size_t size, unsigned int mtype)
{
	if (imx_ioremap != NULL)
		return imx_ioremap(phys_addr, size, mtype);
	else
		return __arm_ioremap(phys_addr, size, mtype);
}

/* io address mapping macro */
#define __io(a)		__typesafe_io(a)

#define __mem_pci(a)	(a)

#endif
