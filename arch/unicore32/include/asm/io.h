/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/io.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __UNICORE_IO_H__
#define __UNICORE_IO_H__

#ifdef __KERNEL__

#include <asm/byteorder.h>
#include <asm/memory.h>

#define PCI_IOBASE	PKUNITY_PCILIO_BASE
#include <asm-generic/io.h>

/*
 * __uc32_ioremap and __uc32_ioremap_cached takes CPU physical address.
 */
extern void __iomem *__uc32_ioremap(unsigned long, size_t);
extern void __iomem *__uc32_ioremap_cached(unsigned long, size_t);
extern void __uc32_iounmap(volatile void __iomem *addr);

/*
 * ioremap and friends.
 *
 * ioremap takes a PCI memory address, as specified in
 * Documentation/io-mapping.txt.
 *
 */
#define ioremap(cookie, size)		__uc32_ioremap(cookie, size)
#define ioremap_cached(cookie, size)	__uc32_ioremap_cached(cookie, size)
#define ioremap_nocache(cookie, size)	__uc32_ioremap(cookie, size)
#define iounmap(cookie)			__uc32_iounmap(cookie)

#define readb_relaxed readb
#define readw_relaxed readw
#define readl_relaxed readl

#define HAVE_ARCH_PIO_SIZE
#define PIO_OFFSET		(unsigned int)(PCI_IOBASE)
#define PIO_MASK		(unsigned int)(IO_SPACE_LIMIT)
#define PIO_RESERVED		(PIO_OFFSET + PIO_MASK + 1)

#ifdef CONFIG_STRICT_DEVMEM

#include <linux/ioport.h>
#include <linux/mm.h>

/*
 * devmem_is_allowed() checks to see if /dev/mem access to a certain
 * address is valid. The argument is a physical page number.
 * We mimic x86 here by disallowing access to system RAM as well as
 * device-exclusive MMIO regions. This effectively disable read()/write()
 * on /dev/mem.
 */
static inline int devmem_is_allowed(unsigned long pfn)
{
	if (iomem_is_exclusive(pfn << PAGE_SHIFT))
		return 0;
	if (!page_is_ram(pfn))
		return 1;
	return 0;
}

#endif /* CONFIG_STRICT_DEVMEM */

#endif	/* __KERNEL__ */
#endif	/* __UNICORE_IO_H__ */
