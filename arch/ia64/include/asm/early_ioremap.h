/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_EARLY_IOREMAP_H
#define _ASM_IA64_EARLY_IOREMAP_H

extern void __iomem * early_ioremap (unsigned long phys_addr, unsigned long size);
#define early_memremap(phys_addr, size)        early_ioremap(phys_addr, size)

extern void early_iounmap (volatile void __iomem *addr, unsigned long size);
#define early_memunmap(addr, size)             early_iounmap(addr, size)

#endif
