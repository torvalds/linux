/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */

#ifndef _LINUX_ASM_VGA_H_
#define _LINUX_ASM_VGA_H_

#include <asm/io.h>

#define VT_BUF_HAVE_RW
#define VT_BUF_HAVE_MEMSETW
#define VT_BUF_HAVE_MEMCPYW
#define VT_BUF_HAVE_MEMMOVEW

static inline void scr_writew(u16 val, volatile u16 *addr)
{
	if (__is_ioaddr(addr))
		__raw_writew(val, (volatile u16 __iomem *) addr);
	else
		*addr = val;
}

static inline u16 scr_readw(volatile const u16 *addr)
{
	if (__is_ioaddr(addr))
		return __raw_readw((volatile const u16 __iomem *) addr);
	else
		return *addr;
}

static inline void scr_memsetw(u16 *s, u16 c, unsigned int count)
{
	if (__is_ioaddr(s))
		memsetw_io((u16 __iomem *) s, c, count);
	else
		memset16(s, c, count / 2);
}

/* Do not trust that the usage will be correct; analyze the arguments.  */
extern void scr_memcpyw(u16 *d, const u16 *s, unsigned int count);
extern void scr_memmovew(u16 *d, const u16 *s, unsigned int count);

/* ??? These are currently only used for downloading character sets.  As
   such, they don't need memory barriers.  Is this all they are intended
   to be used for?  */
#define vga_readb(a)	readb((u8 __iomem *)(a))
#define vga_writeb(v,a)	writeb(v, (u8 __iomem *)(a))

#ifdef CONFIG_VGA_HOSE
#include <linux/ioport.h>
#include <linux/pci.h>

extern struct pci_controller *pci_vga_hose;

# define __is_port_vga(a)       \
	(((a) >= 0x3b0) && ((a) < 0x3e0) && \
	 ((a) != 0x3b3) && ((a) != 0x3d3))

# define __is_mem_vga(a) \
	(((a) >= 0xa0000) && ((a) <= 0xc0000))

# define FIXUP_IOADDR_VGA(a) do {                       \
	if (pci_vga_hose && __is_port_vga(a))     \
		(a) += pci_vga_hose->io_space->start;	  \
 } while(0)

# define FIXUP_MEMADDR_VGA(a) do {                       \
	if (pci_vga_hose && __is_mem_vga(a))     \
		(a) += pci_vga_hose->mem_space->start; \
 } while(0)

#else /* CONFIG_VGA_HOSE */
# define pci_vga_hose 0
# define __is_port_vga(a) 0
# define __is_mem_vga(a) 0
# define FIXUP_IOADDR_VGA(a)
# define FIXUP_MEMADDR_VGA(a)
#endif /* CONFIG_VGA_HOSE */

#define VGA_MAP_MEM(x,s)	((unsigned long) ioremap(x, s))

#endif
