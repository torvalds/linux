#ifndef _ASM_UM_IO_H
#define _ASM_UM_IO_H

#define ioremap ioremap
static inline void __iomem *ioremap(phys_addr_t offset, size_t size)
{
	return (void __iomem *)(unsigned long)offset;
}

#define iounmap iounmap
static inline void iounmap(void __iomem *addr)
{
}

#include <asm-generic/io.h>

#endif
