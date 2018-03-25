/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68KNOMMU_IO_H
#define _M68KNOMMU_IO_H

/*
 * The non-MMU m68k and ColdFire IO and memory mapped hardware access
 * functions have always worked in CPU native endian. We need to define
 * that behavior here first before we include asm-generic/io.h.
 */
#define __raw_readb(addr) \
    ({ unsigned char __v = (*(volatile unsigned char *) (addr)); __v; })
#define __raw_readw(addr) \
    ({ unsigned short __v = (*(volatile unsigned short *) (addr)); __v; })
#define __raw_readl(addr) \
    ({ unsigned int __v = (*(volatile unsigned int *) (addr)); __v; })

#define __raw_writeb(b, addr) (void)((*(volatile unsigned char *) (addr)) = (b))
#define __raw_writew(b, addr) (void)((*(volatile unsigned short *) (addr)) = (b))
#define __raw_writel(b, addr) (void)((*(volatile unsigned int *) (addr)) = (b))

#if defined(CONFIG_PCI) && defined(CONFIG_COLDFIRE)
/*
 * Support for PCI bus access uses the asm-generic access functions.
 * We need to supply the base address and masks for the normal memory
 * and IO address space mappings.
 */
#include <asm/byteorder.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>

#define PCI_MEM_PA	0xf0000000		/* Host physical address */
#define PCI_MEM_BA	0xf0000000		/* Bus physical address */
#define PCI_MEM_SIZE	0x08000000		/* 128 MB */
#define PCI_MEM_MASK	(PCI_MEM_SIZE - 1)

#define PCI_IO_PA	0xf8000000		/* Host physical address */
#define PCI_IO_BA	0x00000000		/* Bus physical address */
#define PCI_IO_SIZE	0x00010000		/* 64k */
#define PCI_IO_MASK	(PCI_IO_SIZE - 1)

#define HAVE_ARCH_PIO_SIZE
#define PIO_OFFSET	0
#define PIO_MASK	0xffff
#define PIO_RESERVED	0x10000
#define PCI_IOBASE	((void __iomem *) PCI_IO_PA)
#define PCI_SPACE_LIMIT	PCI_IO_MASK

#else

#define readb __raw_readb
#define readw __raw_readw
#define readl __raw_readl
#define writeb __raw_writeb
#define writew __raw_writew
#define writel __raw_writel

#endif /* CONFIG_PCI && CONFIG_COLDFIRE */

/*
 * These are defined in kmap.h as static inline functions. To maintain
 * previous behavior we put these define guards here so io_mm.h doesn't
 * see them.
 */
#ifdef CONFIG_MMU
#define memset_io memset_io
#define memcpy_fromio memcpy_fromio
#define memcpy_toio memcpy_toio
#endif

#include <asm/kmap.h>
#include <asm/virtconvert.h>
#include <asm-generic/io.h>

#endif /* _M68KNOMMU_IO_H */
