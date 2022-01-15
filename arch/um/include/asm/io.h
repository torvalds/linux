/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UM_IO_H
#define _ASM_UM_IO_H
#include <linux/types.h>

/* get emulated iomem (if desired) */
#include <asm-generic/logic_io.h>

#ifndef ioremap
#define ioremap ioremap
static inline void __iomem *ioremap(phys_addr_t offset, size_t size)
{
	return NULL;
}
#endif /* ioremap */

#ifndef iounmap
#define iounmap iounmap
static inline void iounmap(void __iomem *addr)
{
}
#endif /* iounmap */

#include <asm-generic/io.h>

#endif
