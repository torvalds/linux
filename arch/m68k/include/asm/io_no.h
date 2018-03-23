/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68KNOMMU_IO_H
#define _M68KNOMMU_IO_H

/*
 * The non-MMU m68k and ColdFire IO and memory mapped hardware access
 * functions have always worked in CPU native endian. We need to define
 * that behavior here first before we include asm-generic/io.h.
 */
#define readb(addr) \
    ({ unsigned char __v = (*(volatile unsigned char *) (addr)); __v; })
#define readw(addr) \
    ({ unsigned short __v = (*(volatile unsigned short *) (addr)); __v; })
#define readl(addr) \
    ({ unsigned int __v = (*(volatile unsigned int *) (addr)); __v; })

#define writeb(b,addr) (void)((*(volatile unsigned char *) (addr)) = (b))
#define writew(b,addr) (void)((*(volatile unsigned short *) (addr)) = (b))
#define writel(b,addr) (void)((*(volatile unsigned int *) (addr)) = (b))

#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel

#include <asm/virtconvert.h>
#include <asm-generic/io.h>

#endif /* _M68KNOMMU_IO_H */
