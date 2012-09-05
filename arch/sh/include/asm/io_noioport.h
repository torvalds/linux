#ifndef __ASM_SH_IO_NOIOPORT_H
#define __ASM_SH_IO_NOIOPORT_H

static inline u8 inb(unsigned long addr)
{
	BUG();
	return -1;
}

static inline u16 inw(unsigned long addr)
{
	BUG();
	return -1;
}

static inline u32 inl(unsigned long addr)
{
	BUG();
	return -1;
}

static inline void outb(unsigned char x, unsigned long port)
{
	BUG();
}

static inline void outw(unsigned short x, unsigned long port)
{
	BUG();
}

static inline void outl(unsigned int x, unsigned long port)
{
	BUG();
}

#define inb_p(addr)	inb(addr)
#define inw_p(addr)	inw(addr)
#define inl_p(addr)	inl(addr)
#define outb_p(x, addr)	outb((x), (addr))
#define outw_p(x, addr)	outw((x), (addr))
#define outl_p(x, addr)	outl((x), (addr))

#define insb(a, b, c)	BUG()
#define insw(a, b, c)	BUG()
#define insl(a, b, c)	BUG()

#define outsb(a, b, c)	BUG()
#define outsw(a, b, c)	BUG()
#define outsl(a, b, c)	BUG()

#endif /* __ASM_SH_IO_NOIOPORT_H */
