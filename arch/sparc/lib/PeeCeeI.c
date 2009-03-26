/*
 * PeeCeeI.c: The emerging standard...
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/module.h>

#include <asm/io.h>
#include <asm/byteorder.h>

void outsb(unsigned long __addr, const void *src, unsigned long count)
{
	void __iomem *addr = (void __iomem *) __addr;
	const u8 *p = src;

	while (count--)
		outb(*p++, addr);
}
EXPORT_SYMBOL(outsb);

void outsw(unsigned long __addr, const void *src, unsigned long count)
{
	void __iomem *addr = (void __iomem *) __addr;

	while (count--) {
		__raw_writew(*(u16 *)src, addr);
		src += sizeof(u16);
	}
}
EXPORT_SYMBOL(outsw);

void outsl(unsigned long __addr, const void *src, unsigned long count)
{
	void __iomem *addr = (void __iomem *) __addr;
	u32 l, l2;

	if (!count)
		return;

	switch (((unsigned long)src) & 0x3) {
	case 0x0:
		/* src is naturally aligned */
		while (count--) {
			__raw_writel(*(u32 *)src, addr);
			src += sizeof(u32);
		}
		break;
	case 0x2:
		/* 2-byte alignment */
		while (count--) {
			l = (*(u16 *)src) << 16;
			l |= *(u16 *)(src + sizeof(u16));
			__raw_writel(l, addr);
			src += sizeof(u32);
		}
		break;
	case 0x1:
		/* Hold three bytes in l each time, grab a byte from l2 */
		l = (*(u8 *)src) << 24;
		l |= (*(u16 *)(src + sizeof(u8))) << 8;
		src += sizeof(u8) + sizeof(u16);
		while (count--) {
			l2 = *(u32 *)src;
			l |= (l2 >> 24);
			__raw_writel(l, addr);
			l = l2 << 8;
			src += sizeof(u32);
		}
		break;
	case 0x3:
		/* Hold a byte in l each time, grab 3 bytes from l2 */
		l = (*(u8 *)src) << 24;
		src += sizeof(u8);
		while (count--) {
			l2 = *(u32 *)src;
			l |= (l2 >> 8);
			__raw_writel(l, addr);
			l = l2 << 24;
			src += sizeof(u32);
		}
		break;
	}
}
EXPORT_SYMBOL(outsl);

void insb(unsigned long __addr, void *dst, unsigned long count)
{
	void __iomem *addr = (void __iomem *) __addr;

	if (count) {
		u32 *pi;
		u8 *pb = dst;

		while ((((unsigned long)pb) & 0x3) && count--)
			*pb++ = inb(addr);
		pi = (u32 *)pb;
		while (count >= 4) {
			u32 w;

			w  = (inb(addr) << 24);
			w |= (inb(addr) << 16);
			w |= (inb(addr) << 8);
			w |= (inb(addr) << 0);
			*pi++ = w;
			count -= 4;
		}
		pb = (u8 *)pi;
		while (count--)
			*pb++ = inb(addr);
	}
}
EXPORT_SYMBOL(insb);

void insw(unsigned long __addr, void *dst, unsigned long count)
{
	void __iomem *addr = (void __iomem *) __addr;

	if (count) {
		u16 *ps = dst;
		u32 *pi;

		if (((unsigned long)ps) & 0x2) {
			*ps++ = le16_to_cpu(inw(addr));
			count--;
		}
		pi = (u32 *)ps;
		while (count >= 2) {
			u32 w;

			w  = (le16_to_cpu(inw(addr)) << 16);
			w |= (le16_to_cpu(inw(addr)) << 0);
			*pi++ = w;
			count -= 2;
		}
		ps = (u16 *)pi;
		if (count)
			*ps = le16_to_cpu(inw(addr));
	}
}
EXPORT_SYMBOL(insw);

void insl(unsigned long __addr, void *dst, unsigned long count)
{
	void __iomem *addr = (void __iomem *) __addr;

	if (count) {
		if ((((unsigned long)dst) & 0x3) == 0) {
			u32 *pi = dst;
			while (count--)
				*pi++ = le32_to_cpu(inl(addr));
		} else {
			u32 l = 0, l2, *pi;
			u16 *ps;
			u8 *pb;

			switch (((unsigned long)dst) & 3) {
			case 0x2:
				ps = dst;
				count -= 1;
				l = le32_to_cpu(inl(addr));
				*ps++ = l;
				pi = (u32 *)ps;
				while (count--) {
					l2 = le32_to_cpu(inl(addr));
					*pi++ = (l << 16) | (l2 >> 16);
					l = l2;
				}
				ps = (u16 *)pi;
				*ps = l;
				break;

			case 0x1:
				pb = dst;
				count -= 1;
				l = le32_to_cpu(inl(addr));
				*pb++ = l >> 24;
				ps = (u16 *)pb;
				*ps++ = ((l >> 8) & 0xffff);
				pi = (u32 *)ps;
				while (count--) {
					l2 = le32_to_cpu(inl(addr));
					*pi++ = (l << 24) | (l2 >> 8);
					l = l2;
				}
				pb = (u8 *)pi;
				*pb = l;
				break;

			case 0x3:
				pb = (u8 *)dst;
				count -= 1;
				l = le32_to_cpu(inl(addr));
				*pb++ = l >> 24;
				pi = (u32 *)pb;
				while (count--) {
					l2 = le32_to_cpu(inl(addr));
					*pi++ = (l << 8) | (l2 >> 24);
					l = l2;
				}
				ps = (u16 *)pi;
				*ps++ = ((l >> 8) & 0xffff);
				pb = (u8 *)ps;
				*pb = l;
				break;
			}
		}
	}
}
EXPORT_SYMBOL(insl);

