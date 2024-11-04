/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/io.h"
 */

#ifndef _S390_IO_H
#define _S390_IO_H

#include <linux/kernel.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pci_io.h>

#define xlate_dev_mem_ptr xlate_dev_mem_ptr
#define kc_xlate_dev_mem_ptr xlate_dev_mem_ptr
void *xlate_dev_mem_ptr(phys_addr_t phys);
#define unxlate_dev_mem_ptr unxlate_dev_mem_ptr
#define kc_unxlate_dev_mem_ptr unxlate_dev_mem_ptr
void unxlate_dev_mem_ptr(phys_addr_t phys, void *addr);

#define IO_SPACE_LIMIT 0

/*
 * I/O memory mapping functions.
 */
#define ioremap_prot ioremap_prot
#define iounmap iounmap

#define _PAGE_IOREMAP pgprot_val(PAGE_KERNEL)

#define ioremap_wc(addr, size)  \
	ioremap_prot((addr), (size), pgprot_val(pgprot_writecombine(PAGE_KERNEL)))
#define ioremap_wt(addr, size)  \
	ioremap_prot((addr), (size), pgprot_val(pgprot_writethrough(PAGE_KERNEL)))

static inline void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	return NULL;
}

static inline void ioport_unmap(void __iomem *p)
{
}

#ifdef CONFIG_PCI

/*
 * s390 needs a private implementation of pci_iomap since ioremap with its
 * offset parameter isn't sufficient. That's because BAR spaces are not
 * disjunctive on s390 so we need the bar parameter of pci_iomap to find
 * the corresponding device and create the mapping cookie.
 */
#define pci_iomap pci_iomap
#define pci_iomap_range pci_iomap_range
#define pci_iounmap pci_iounmap
#define pci_iomap_wc pci_iomap_wc
#define pci_iomap_wc_range pci_iomap_wc_range

#define memcpy_fromio(dst, src, count)	zpci_memcpy_fromio(dst, src, count)
#define memcpy_toio(dst, src, count)	zpci_memcpy_toio(dst, src, count)
#define memset_io(dst, val, count)	zpci_memset_io(dst, val, count)

#define mmiowb()	zpci_barrier()

#define __raw_readb	zpci_read_u8
#define __raw_readw	zpci_read_u16
#define __raw_readl	zpci_read_u32
#define __raw_readq	zpci_read_u64
#define __raw_writeb	zpci_write_u8
#define __raw_writew	zpci_write_u16
#define __raw_writel	zpci_write_u32
#define __raw_writeq	zpci_write_u64

/* combine single writes by using store-block insn */
static inline void __iowrite32_copy(void __iomem *to, const void *from,
				    size_t count)
{
	zpci_memcpy_toio(to, from, count * 4);
}
#define __iowrite32_copy __iowrite32_copy

static inline void __iowrite64_copy(void __iomem *to, const void *from,
				    size_t count)
{
	zpci_memcpy_toio(to, from, count * 8);
}
#define __iowrite64_copy __iowrite64_copy

#endif /* CONFIG_PCI */

#include <asm-generic/io.h>

#endif
