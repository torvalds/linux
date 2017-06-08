#ifndef _ASM_CRIS_IO_H
#define _ASM_CRIS_IO_H

#include <asm/page.h>   /* for __va, __pa */
#ifdef CONFIG_ETRAX_ARCH_V10
#include <arch/io.h>
#endif
#include <asm-generic/iomap.h>
#include <linux/kernel.h>

extern void __iomem * __ioremap(unsigned long offset, unsigned long size, unsigned long flags);
extern void __iomem * __ioremap_prot(unsigned long phys_addr, unsigned long size, pgprot_t prot);

static inline void __iomem * ioremap (unsigned long offset, unsigned long size)
{
	return __ioremap(offset, size, 0);
}

extern void iounmap(volatile void * __iomem addr);

extern void __iomem * ioremap_nocache(unsigned long offset, unsigned long size);

#include <asm-generic/io.h>

#endif
