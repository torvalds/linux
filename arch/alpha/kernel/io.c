// SPDX-License-Identifier: GPL-2.0
/*
 * Alpha IO and memory functions.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/module.h>
#include <asm/io.h>

/* Out-of-line versions of the i/o routines that redirect into the 
   platform-specific version.  Note that "platform-specific" may mean
   "generic", which bumps through the machine vector.  */

unsigned int
ioread8(const void __iomem *addr)
{
	unsigned int ret;
	mb();
	ret = IO_CONCAT(__IO_PREFIX,ioread8)(addr);
	mb();
	return ret;
}

unsigned int ioread16(const void __iomem *addr)
{
	unsigned int ret;
	mb();
	ret = IO_CONCAT(__IO_PREFIX,ioread16)(addr);
	mb();
	return ret;
}

unsigned int ioread32(const void __iomem *addr)
{
	unsigned int ret;
	mb();
	ret = IO_CONCAT(__IO_PREFIX,ioread32)(addr);
	mb();
	return ret;
}

u64 ioread64(const void __iomem *addr)
{
	unsigned int ret;
	mb();
	ret = IO_CONCAT(__IO_PREFIX,ioread64)(addr);
	mb();
	return ret;
}

void iowrite8(u8 b, void __iomem *addr)
{
	mb();
	IO_CONCAT(__IO_PREFIX,iowrite8)(b, addr);
}

void iowrite16(u16 b, void __iomem *addr)
{
	mb();
	IO_CONCAT(__IO_PREFIX,iowrite16)(b, addr);
}

void iowrite32(u32 b, void __iomem *addr)
{
	mb();
	IO_CONCAT(__IO_PREFIX,iowrite32)(b, addr);
}

void iowrite64(u64 b, void __iomem *addr)
{
	mb();
	IO_CONCAT(__IO_PREFIX,iowrite64)(b, addr);
}

EXPORT_SYMBOL(ioread8);
EXPORT_SYMBOL(ioread16);
EXPORT_SYMBOL(ioread32);
EXPORT_SYMBOL(ioread64);
EXPORT_SYMBOL(iowrite8);
EXPORT_SYMBOL(iowrite16);
EXPORT_SYMBOL(iowrite32);
EXPORT_SYMBOL(iowrite64);

u8 inb(unsigned long port)
{
	return ioread8(ioport_map(port, 1));
}

u16 inw(unsigned long port)
{
	return ioread16(ioport_map(port, 2));
}

u32 inl(unsigned long port)
{
	return ioread32(ioport_map(port, 4));
}

void outb(u8 b, unsigned long port)
{
	iowrite8(b, ioport_map(port, 1));
}

void outw(u16 b, unsigned long port)
{
	iowrite16(b, ioport_map(port, 2));
}

void outl(u32 b, unsigned long port)
{
	iowrite32(b, ioport_map(port, 4));
}

EXPORT_SYMBOL(inb);
EXPORT_SYMBOL(inw);
EXPORT_SYMBOL(inl);
EXPORT_SYMBOL(outb);
EXPORT_SYMBOL(outw);
EXPORT_SYMBOL(outl);

u8 __raw_readb(const volatile void __iomem *addr)
{
	return IO_CONCAT(__IO_PREFIX,readb)(addr);
}

u16 __raw_readw(const volatile void __iomem *addr)
{
	return IO_CONCAT(__IO_PREFIX,readw)(addr);
}

u32 __raw_readl(const volatile void __iomem *addr)
{
	return IO_CONCAT(__IO_PREFIX,readl)(addr);
}

u64 __raw_readq(const volatile void __iomem *addr)
{
	return IO_CONCAT(__IO_PREFIX,readq)(addr);
}

void __raw_writeb(u8 b, volatile void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,writeb)(b, addr);
}

void __raw_writew(u16 b, volatile void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,writew)(b, addr);
}

void __raw_writel(u32 b, volatile void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,writel)(b, addr);
}

void __raw_writeq(u64 b, volatile void __iomem *addr)
{
	IO_CONCAT(__IO_PREFIX,writeq)(b, addr);
}

EXPORT_SYMBOL(__raw_readb); 
EXPORT_SYMBOL(__raw_readw); 
EXPORT_SYMBOL(__raw_readl); 
EXPORT_SYMBOL(__raw_readq); 
EXPORT_SYMBOL(__raw_writeb); 
EXPORT_SYMBOL(__raw_writew); 
EXPORT_SYMBOL(__raw_writel); 
EXPORT_SYMBOL(__raw_writeq); 

u8 readb(const volatile void __iomem *addr)
{
	u8 ret;
	mb();
	ret = __raw_readb(addr);
	mb();
	return ret;
}

u16 readw(const volatile void __iomem *addr)
{
	u16 ret;
	mb();
	ret = __raw_readw(addr);
	mb();
	return ret;
}

u32 readl(const volatile void __iomem *addr)
{
	u32 ret;
	mb();
	ret = __raw_readl(addr);
	mb();
	return ret;
}

u64 readq(const volatile void __iomem *addr)
{
	u64 ret;
	mb();
	ret = __raw_readq(addr);
	mb();
	return ret;
}

void writeb(u8 b, volatile void __iomem *addr)
{
	mb();
	__raw_writeb(b, addr);
}

void writew(u16 b, volatile void __iomem *addr)
{
	mb();
	__raw_writew(b, addr);
}

void writel(u32 b, volatile void __iomem *addr)
{
	mb();
	__raw_writel(b, addr);
}

void writeq(u64 b, volatile void __iomem *addr)
{
	mb();
	__raw_writeq(b, addr);
}

EXPORT_SYMBOL(readb);
EXPORT_SYMBOL(readw);
EXPORT_SYMBOL(readl);
EXPORT_SYMBOL(readq);
EXPORT_SYMBOL(writeb);
EXPORT_SYMBOL(writew);
EXPORT_SYMBOL(writel);
EXPORT_SYMBOL(writeq);

/*
 * The _relaxed functions must be ordered w.r.t. each other, but they don't
 * have to be ordered w.r.t. other memory accesses.
 */
u8 readb_relaxed(const volatile void __iomem *addr)
{
	mb();
	return __raw_readb(addr);
}

u16 readw_relaxed(const volatile void __iomem *addr)
{
	mb();
	return __raw_readw(addr);
}

u32 readl_relaxed(const volatile void __iomem *addr)
{
	mb();
	return __raw_readl(addr);
}

u64 readq_relaxed(const volatile void __iomem *addr)
{
	mb();
	return __raw_readq(addr);
}

EXPORT_SYMBOL(readb_relaxed);
EXPORT_SYMBOL(readw_relaxed);
EXPORT_SYMBOL(readl_relaxed);
EXPORT_SYMBOL(readq_relaxed);

/*
 * Read COUNT 8-bit bytes from port PORT into memory starting at SRC.
 */
void ioread8_rep(const void __iomem *port, void *dst, unsigned long count)
{
	while ((unsigned long)dst & 0x3) {
		if (!count)
			return;
		count--;
		*(unsigned char *)dst = ioread8(port);
		dst += 1;
	}

	while (count >= 4) {
		unsigned int w;
		count -= 4;
		w = ioread8(port);
		w |= ioread8(port) << 8;
		w |= ioread8(port) << 16;
		w |= ioread8(port) << 24;
		*(unsigned int *)dst = w;
		dst += 4;
	}

	while (count) {
		--count;
		*(unsigned char *)dst = ioread8(port);
		dst += 1;
	}
}

void insb(unsigned long port, void *dst, unsigned long count)
{
	ioread8_rep(ioport_map(port, 1), dst, count);
}

EXPORT_SYMBOL(ioread8_rep);
EXPORT_SYMBOL(insb);

/*
 * Read COUNT 16-bit words from port PORT into memory starting at
 * SRC.  SRC must be at least short aligned.  This is used by the
 * IDE driver to read disk sectors.  Performance is important, but
 * the interfaces seems to be slow: just using the inlined version
 * of the inw() breaks things.
 */
void ioread16_rep(const void __iomem *port, void *dst, unsigned long count)
{
	if (unlikely((unsigned long)dst & 0x3)) {
		if (!count)
			return;
		BUG_ON((unsigned long)dst & 0x1);
		count--;
		*(unsigned short *)dst = ioread16(port);
		dst += 2;
	}

	while (count >= 2) {
		unsigned int w;
		count -= 2;
		w = ioread16(port);
		w |= ioread16(port) << 16;
		*(unsigned int *)dst = w;
		dst += 4;
	}

	if (count) {
		*(unsigned short*)dst = ioread16(port);
	}
}

void insw(unsigned long port, void *dst, unsigned long count)
{
	ioread16_rep(ioport_map(port, 2), dst, count);
}

EXPORT_SYMBOL(ioread16_rep);
EXPORT_SYMBOL(insw);


/*
 * Read COUNT 32-bit words from port PORT into memory starting at
 * SRC. Now works with any alignment in SRC. Performance is important,
 * but the interfaces seems to be slow: just using the inlined version
 * of the inl() breaks things.
 */
void ioread32_rep(const void __iomem *port, void *dst, unsigned long count)
{
	if (unlikely((unsigned long)dst & 0x3)) {
		while (count--) {
			struct S { int x __attribute__((packed)); };
			((struct S *)dst)->x = ioread32(port);
			dst += 4;
		}
	} else {
		/* Buffer 32-bit aligned.  */
		while (count--) {
			*(unsigned int *)dst = ioread32(port);
			dst += 4;
		}
	}
}

void insl(unsigned long port, void *dst, unsigned long count)
{
	ioread32_rep(ioport_map(port, 4), dst, count);
}

EXPORT_SYMBOL(ioread32_rep);
EXPORT_SYMBOL(insl);


/*
 * Like insb but in the opposite direction.
 * Don't worry as much about doing aligned memory transfers:
 * doing byte reads the "slow" way isn't nearly as slow as
 * doing byte writes the slow way (no r-m-w cycle).
 */
void iowrite8_rep(void __iomem *port, const void *xsrc, unsigned long count)
{
	const unsigned char *src = xsrc;
	while (count--)
		iowrite8(*src++, port);
}

void outsb(unsigned long port, const void *src, unsigned long count)
{
	iowrite8_rep(ioport_map(port, 1), src, count);
}

EXPORT_SYMBOL(iowrite8_rep);
EXPORT_SYMBOL(outsb);


/*
 * Like insw but in the opposite direction.  This is used by the IDE
 * driver to write disk sectors.  Performance is important, but the
 * interfaces seems to be slow: just using the inlined version of the
 * outw() breaks things.
 */
void iowrite16_rep(void __iomem *port, const void *src, unsigned long count)
{
	if (unlikely((unsigned long)src & 0x3)) {
		if (!count)
			return;
		BUG_ON((unsigned long)src & 0x1);
		iowrite16(*(unsigned short *)src, port);
		src += 2;
		--count;
	}

	while (count >= 2) {
		unsigned int w;
		count -= 2;
		w = *(unsigned int *)src;
		src += 4;
		iowrite16(w >>  0, port);
		iowrite16(w >> 16, port);
	}

	if (count) {
		iowrite16(*(unsigned short *)src, port);
	}
}

void outsw(unsigned long port, const void *src, unsigned long count)
{
	iowrite16_rep(ioport_map(port, 2), src, count);
}

EXPORT_SYMBOL(iowrite16_rep);
EXPORT_SYMBOL(outsw);


/*
 * Like insl but in the opposite direction.  This is used by the IDE
 * driver to write disk sectors.  Works with any alignment in SRC.
 * Performance is important, but the interfaces seems to be slow:
 * just using the inlined version of the outl() breaks things.
 */
void iowrite32_rep(void __iomem *port, const void *src, unsigned long count)
{
	if (unlikely((unsigned long)src & 0x3)) {
		while (count--) {
			struct S { int x __attribute__((packed)); };
			iowrite32(((struct S *)src)->x, port);
			src += 4;
		}
	} else {
		/* Buffer 32-bit aligned.  */
		while (count--) {
			iowrite32(*(unsigned int *)src, port);
			src += 4;
		}
	}
}

void outsl(unsigned long port, const void *src, unsigned long count)
{
	iowrite32_rep(ioport_map(port, 4), src, count);
}

EXPORT_SYMBOL(iowrite32_rep);
EXPORT_SYMBOL(outsl);


/*
 * Copy data from IO memory space to "real" memory space.
 * This needs to be optimized.
 */
void memcpy_fromio(void *to, const volatile void __iomem *from, long count)
{
	/* Optimize co-aligned transfers.  Everything else gets handled
	   a byte at a time. */

	if (count >= 8 && ((u64)to & 7) == ((u64)from & 7)) {
		count -= 8;
		do {
			*(u64 *)to = __raw_readq(from);
			count -= 8;
			to += 8;
			from += 8;
		} while (count >= 0);
		count += 8;
	}

	if (count >= 4 && ((u64)to & 3) == ((u64)from & 3)) {
		count -= 4;
		do {
			*(u32 *)to = __raw_readl(from);
			count -= 4;
			to += 4;
			from += 4;
		} while (count >= 0);
		count += 4;
	}

	if (count >= 2 && ((u64)to & 1) == ((u64)from & 1)) {
		count -= 2;
		do {
			*(u16 *)to = __raw_readw(from);
			count -= 2;
			to += 2;
			from += 2;
		} while (count >= 0);
		count += 2;
	}

	while (count > 0) {
		*(u8 *) to = __raw_readb(from);
		count--;
		to++;
		from++;
	}
	mb();
}

EXPORT_SYMBOL(memcpy_fromio);


/*
 * Copy data from "real" memory space to IO memory space.
 * This needs to be optimized.
 */
void memcpy_toio(volatile void __iomem *to, const void *from, long count)
{
	/* Optimize co-aligned transfers.  Everything else gets handled
	   a byte at a time. */
	/* FIXME -- align FROM.  */

	if (count >= 8 && ((u64)to & 7) == ((u64)from & 7)) {
		count -= 8;
		do {
			__raw_writeq(*(const u64 *)from, to);
			count -= 8;
			to += 8;
			from += 8;
		} while (count >= 0);
		count += 8;
	}

	if (count >= 4 && ((u64)to & 3) == ((u64)from & 3)) {
		count -= 4;
		do {
			__raw_writel(*(const u32 *)from, to);
			count -= 4;
			to += 4;
			from += 4;
		} while (count >= 0);
		count += 4;
	}

	if (count >= 2 && ((u64)to & 1) == ((u64)from & 1)) {
		count -= 2;
		do {
			__raw_writew(*(const u16 *)from, to);
			count -= 2;
			to += 2;
			from += 2;
		} while (count >= 0);
		count += 2;
	}

	while (count > 0) {
		__raw_writeb(*(const u8 *) from, to);
		count--;
		to++;
		from++;
	}
	mb();
}

EXPORT_SYMBOL(memcpy_toio);


/*
 * "memset" on IO memory space.
 */
void _memset_c_io(volatile void __iomem *to, unsigned long c, long count)
{
	/* Handle any initial odd byte */
	if (count > 0 && ((u64)to & 1)) {
		__raw_writeb(c, to);
		to++;
		count--;
	}

	/* Handle any initial odd halfword */
	if (count >= 2 && ((u64)to & 2)) {
		__raw_writew(c, to);
		to += 2;
		count -= 2;
	}

	/* Handle any initial odd word */
	if (count >= 4 && ((u64)to & 4)) {
		__raw_writel(c, to);
		to += 4;
		count -= 4;
	}

	/* Handle all full-sized quadwords: we're aligned
	   (or have a small count) */
	count -= 8;
	if (count >= 0) {
		do {
			__raw_writeq(c, to);
			to += 8;
			count -= 8;
		} while (count >= 0);
	}
	count += 8;

	/* The tail is word-aligned if we still have count >= 4 */
	if (count >= 4) {
		__raw_writel(c, to);
		to += 4;
		count -= 4;
	}

	/* The tail is half-word aligned if we have count >= 2 */
	if (count >= 2) {
		__raw_writew(c, to);
		to += 2;
		count -= 2;
	}

	/* And finally, one last byte.. */
	if (count) {
		__raw_writeb(c, to);
	}
	mb();
}

EXPORT_SYMBOL(_memset_c_io);

#if IS_ENABLED(CONFIG_VGA_CONSOLE) || IS_ENABLED(CONFIG_MDA_CONSOLE)

#include <asm/vga.h>

/* A version of memcpy used by the vga console routines to move data around
   arbitrarily between screen and main memory.  */

void
scr_memcpyw(u16 *d, const u16 *s, unsigned int count)
{
	const u16 __iomem *ios = (const u16 __iomem *) s;
	u16 __iomem *iod = (u16 __iomem *) d;
	int s_isio = __is_ioaddr(s);
	int d_isio = __is_ioaddr(d);

	if (s_isio) {
		if (d_isio) {
			/* FIXME: Should handle unaligned ops and
			   operation widening.  */

			count /= 2;
			while (count--) {
				u16 tmp = __raw_readw(ios++);
				__raw_writew(tmp, iod++);
			}
		}
		else
			memcpy_fromio(d, ios, count);
	} else {
		if (d_isio)
			memcpy_toio(iod, s, count);
		else
			memcpy(d, s, count);
	}
}

EXPORT_SYMBOL(scr_memcpyw);

void scr_memmovew(u16 *d, const u16 *s, unsigned int count)
{
	if (d < s)
		scr_memcpyw(d, s, count);
	else {
		count /= 2;
		d += count;
		s += count;
		while (count--)
			scr_writew(scr_readw(--s), --d);
	}
}
EXPORT_SYMBOL(scr_memmovew);
#endif

void __iomem *ioport_map(unsigned long port, unsigned int size)
{
	return IO_CONCAT(__IO_PREFIX,ioportmap) (port);
}

void ioport_unmap(void __iomem *addr)
{
}

EXPORT_SYMBOL(ioport_map);
EXPORT_SYMBOL(ioport_unmap);
