#ifndef _H8300_IO_H
#define _H8300_IO_H

#ifdef __KERNEL__

#include <asm-generic/io.h>

/* H8/300 internal I/O functions */
static inline unsigned char ctrl_inb(unsigned long addr)
{
	return *(volatile unsigned char *)addr;
}

static inline unsigned short ctrl_inw(unsigned long addr)
{
	return *(volatile unsigned short *)addr;
}

static inline unsigned long ctrl_inl(unsigned long addr)
{
	return *(volatile unsigned long *)addr;
}

static inline void ctrl_outb(unsigned char b, unsigned long addr)
{
	*(volatile unsigned char *)addr = b;
}

static inline void ctrl_outw(unsigned short b, unsigned long addr)
{
	*(volatile unsigned short *)addr = b;
}

static inline void ctrl_outl(unsigned long b, unsigned long addr)
{
	*(volatile unsigned long *)addr = b;
}

static inline void ctrl_bclr(int b, unsigned char *addr)
{
	if (__builtin_constant_p(b))
		__asm__("bclr %1,%0" : "+WU"(*addr): "i"(b));
	else
		__asm__("bclr %w1,%0" : "+WU"(*addr): "r"(b));
}

static inline void ctrl_bset(int b, unsigned char *addr)
{
	if (__builtin_constant_p(b))
		__asm__("bset %1,%0" : "+WU"(*addr): "i"(b));
	else
		__asm__("bset %w1,%0" : "+WU"(*addr): "r"(b));
}

#endif /* __KERNEL__ */

#endif /* _H8300_IO_H */
