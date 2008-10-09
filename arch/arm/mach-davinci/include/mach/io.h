/*
 * DaVinci IO address definitions
 *
 * Copied from include/asm/arm/arch-omap/io.h
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_ARCH_IO_H
#define __ASM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

/*
 * ----------------------------------------------------------------------------
 * I/O mapping
 * ----------------------------------------------------------------------------
 */
#define IO_PHYS		0x01c00000
#define IO_OFFSET	0xfd000000 /* Virtual IO = 0xfec00000 */
#define IO_SIZE		0x00400000
#define IO_VIRT		(IO_PHYS + IO_OFFSET)
#define io_v2p(va)	((va) - IO_OFFSET)
#define __IO_ADDRESS(x)	((x) + IO_OFFSET)

/*
 * We don't actually have real ISA nor PCI buses, but there is so many
 * drivers out there that might just work if we fake them...
 */
#define PCIO_BASE               0
#define __io(a)			((void __iomem *)(PCIO_BASE + (a)))
#define __mem_pci(a)		(a)
#define __mem_isa(a)		(a)

#define IO_ADDRESS(pa)          IOMEM(__IO_ADDRESS(pa))

#ifdef __ASSEMBLER__
#define IOMEM(x)                x
#else
#define IOMEM(x)                ((void __force __iomem *)(x))

/*
 * Functions to access the DaVinci IO region
 *
 * NOTE: - Use davinci_read/write[bwl] for physical register addresses
 *	 - Use __raw_read/write[bwl]() for virtual register addresses
 *	 - Use IO_ADDRESS(phys_addr) to convert registers to virtual addresses
 *	 - DO NOT use hardcoded virtual addresses to allow changing the
 *	   IO address space again if needed
 */
#define davinci_readb(a)	__raw_readb(IO_ADDRESS(a))
#define davinci_readw(a)	__raw_readw(IO_ADDRESS(a))
#define davinci_readl(a)	__raw_readl(IO_ADDRESS(a))

#define davinci_writeb(v, a)	__raw_writeb(v, IO_ADDRESS(a))
#define davinci_writew(v, a)	__raw_writew(v, IO_ADDRESS(a))
#define davinci_writel(v, a)	__raw_writel(v, IO_ADDRESS(a))

#endif /* __ASSEMBLER__ */
#endif /* __ASM_ARCH_IO_H */
