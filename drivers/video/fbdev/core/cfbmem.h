/* SPDX-License-Identifier: GPL-2.0-only
 *
 *	I/O memory framebuffer access for drawing routines
 *
 *	Copyright (C) 2025 Zsolt Kajtar (soci@c64.rulez.org)
 */

/* keeps track of a bit address in framebuffer memory */
struct fb_address {
	void __iomem *address;
	int bits;
};

/* initialize the bit address pointer to the beginning of the frame buffer */
static inline struct fb_address fb_address_init(struct fb_info *p)
{
	void __iomem *base = p->screen_base;
	struct fb_address ptr;

	ptr.address = PTR_ALIGN_DOWN(base, BITS_PER_LONG / BITS_PER_BYTE);
	ptr.bits = (base - ptr.address) * BITS_PER_BYTE;
	return ptr;
}

/* framebuffer write access */
static inline void fb_write_offset(unsigned long val, int offset, const struct fb_address *dst)
{
#if BITS_PER_LONG == 32
	fb_writel(val, dst->address + offset * (BITS_PER_LONG / BITS_PER_BYTE));
#else
	fb_writeq(val, dst->address + offset * (BITS_PER_LONG / BITS_PER_BYTE));
#endif
}

/* framebuffer read access */
static inline unsigned long fb_read_offset(int offset, const struct fb_address *src)
{
#if BITS_PER_LONG == 32
	return fb_readl(src->address + offset * (BITS_PER_LONG / BITS_PER_BYTE));
#else
	return fb_readq(src->address + offset * (BITS_PER_LONG / BITS_PER_BYTE));
#endif
}
