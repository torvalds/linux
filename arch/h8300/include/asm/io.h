#ifndef _H8300_IO_H
#define _H8300_IO_H

#ifdef __KERNEL__

/* H8/300 internal I/O functions */

#define __raw_readb __raw_readb
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	return *(volatile u8 *)addr;
}

#define __raw_readw __raw_readw
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	return *(volatile u16 *)addr;
}

#define __raw_readl __raw_readl
static inline u32  __raw_readl(const volatile void __iomem *addr)
{
	return *(volatile u32 *)addr;
}

#define __raw_writeb __raw_writeb
static inline void __raw_writeb(u8 b, const volatile void __iomem *addr)
{
	*(volatile u8 *)addr = b;
}

#define __raw_writew __raw_writew
static inline void __raw_writew(u16 b, const volatile void __iomem *addr)
{
	*(volatile u16 *)addr = b;
}

#define __raw_writel __raw_writel
static inline void __raw_writel(u32 b, const volatile void __iomem *addr)
{
	*(volatile u32 *)addr = b;
}

static inline void ctrl_bclr(int b, void __iomem *addr)
{
	if (__builtin_constant_p(b))
		__asm__("bclr %1,%0" : "+WU"(*(u8 *)addr): "i"(b));
	else
		__asm__("bclr %w1,%0" : "+WU"(*(u8 *)addr): "r"(b));
}

static inline void ctrl_bset(int b, void __iomem *addr)
{
	if (__builtin_constant_p(b))
		__asm__("bset %1,%0" : "+WU"(*(u8 *)addr): "i"(b));
	else
		__asm__("bset %w1,%0" : "+WU"(*(u8 *)addr): "r"(b));
}

#include <asm-generic/io.h>

#endif /* __KERNEL__ */

#endif /* _H8300_IO_H */
