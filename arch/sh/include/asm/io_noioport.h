/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_IO_NOIOPORT_H
#define __ASM_SH_IO_NOIOPORT_H

static inline u8 inb(unsigned long addr)
{
	();
	return -1;
}

static inline u16 inw(unsigned long addr)
{
	();
	return -1;
}

static inline u32 inl(unsigned long addr)
{
	();
	return -1;
}

static inline void outb(unsigned char x, unsigned long port)
{
	();
}

static inline void outw(unsigned short x, unsigned long port)
{
	();
}

static inline void outl(unsigned int x, unsigned long port)
{
	();
}

static inline void __iomem *ioport_map(unsigned long port, unsigned int size)
{
	();
	return NULL;
}

static inline void ioport_unmap(void __iomem *addr)
{
	();
}

#define inb_p(addr)	inb(addr)
#define inw_p(addr)	inw(addr)
#define inl_p(addr)	inl(addr)
#define outb_p(x, addr)	outb((x), (addr))
#define outw_p(x, addr)	outw((x), (addr))
#define outl_p(x, addr)	outl((x), (addr))

#define insb(a, b, c)	()
#define insw(a, b, c)	()
#define insl(a, b, c)	()

#define outsb(a, b, c)	()
#define outsw(a, b, c)	()
#define outsl(a, b, c)	()

#endif /* __ASM_SH_IO_NOIOPORT_H */
