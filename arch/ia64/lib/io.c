#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/io.h>

/*
 * Copy data from IO memory space to "real" memory space.
 * This needs to be optimized.
 */
void memcpy_fromio(void *to, const volatile void __iomem *from, long count)
{
	char *dst = to;

	while (count) {
		count--;
		*dst++ = readb(from++);
	}
}
EXPORT_SYMBOL(memcpy_fromio);

/*
 * Copy data from "real" memory space to IO memory space.
 * This needs to be optimized.
 */
void memcpy_toio(volatile void __iomem *to, const void *from, long count)
{
	const char *src = from;

	while (count) {
		count--;
		writeb(*src++, to++);
	}
}
EXPORT_SYMBOL(memcpy_toio);

/*
 * "memset" on IO memory space.
 * This needs to be optimized.
 */
void memset_io(volatile void __iomem *dst, int c, long count)
{
	unsigned char ch = (char)(c & 0xff);

	while (count) {
		count--;
		writeb(ch, dst);
		dst++;
	}
}
EXPORT_SYMBOL(memset_io);

#ifdef CONFIG_IA64_GENERIC

#undef __ia64_inb
#undef __ia64_inw
#undef __ia64_inl
#undef __ia64_outb
#undef __ia64_outw
#undef __ia64_outl
#undef __ia64_readb
#undef __ia64_readw
#undef __ia64_readl
#undef __ia64_readq
#undef __ia64_readb_relaxed
#undef __ia64_readw_relaxed
#undef __ia64_readl_relaxed
#undef __ia64_readq_relaxed
#undef __ia64_writeb
#undef __ia64_writew
#undef __ia64_writel
#undef __ia64_writeq
#undef __ia64_mmiowb

unsigned int
__ia64_inb (unsigned long port)
{
	return ___ia64_inb(port);
}

unsigned int
__ia64_inw (unsigned long port)
{
	return ___ia64_inw(port);
}

unsigned int
__ia64_inl (unsigned long port)
{
	return ___ia64_inl(port);
}

void
__ia64_outb (unsigned char val, unsigned long port)
{
	___ia64_outb(val, port);
}

void
__ia64_outw (unsigned short val, unsigned long port)
{
	___ia64_outw(val, port);
}

void
__ia64_outl (unsigned int val, unsigned long port)
{
	___ia64_outl(val, port);
}

unsigned char
__ia64_readb (void __iomem *addr)
{
	return ___ia64_readb (addr);
}

unsigned short
__ia64_readw (void __iomem *addr)
{
	return ___ia64_readw (addr);
}

unsigned int
__ia64_readl (void __iomem *addr)
{
	return ___ia64_readl (addr);
}

unsigned long
__ia64_readq (void __iomem *addr)
{
	return ___ia64_readq (addr);
}

unsigned char
__ia64_readb_relaxed (void __iomem *addr)
{
	return ___ia64_readb (addr);
}

unsigned short
__ia64_readw_relaxed (void __iomem *addr)
{
	return ___ia64_readw (addr);
}

unsigned int
__ia64_readl_relaxed (void __iomem *addr)
{
	return ___ia64_readl (addr);
}

unsigned long
__ia64_readq_relaxed (void __iomem *addr)
{
	return ___ia64_readq (addr);
}

void
__ia64_mmiowb(void)
{
	___ia64_mmiowb();
}

#endif /* CONFIG_IA64_GENERIC */
