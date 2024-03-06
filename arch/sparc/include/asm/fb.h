/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC_FB_H_
#define _SPARC_FB_H_

#include <linux/io.h>

#include <asm/page.h>

struct fb_info;

#ifdef CONFIG_SPARC32
static inline pgprot_t pgprot_framebuffer(pgprot_t prot,
					  unsigned long vm_start, unsigned long vm_end,
					  unsigned long offset)
{
	return prot;
}
#define pgprot_framebuffer pgprot_framebuffer
#endif

int fb_is_primary_device(struct fb_info *info);
#define fb_is_primary_device fb_is_primary_device

static inline void fb_memcpy_fromio(void *to, const volatile void __iomem *from, size_t n)
{
	sbus_memcpy_fromio(to, from, n);
}
#define fb_memcpy_fromio fb_memcpy_fromio

static inline void fb_memcpy_toio(volatile void __iomem *to, const void *from, size_t n)
{
	sbus_memcpy_toio(to, from, n);
}
#define fb_memcpy_toio fb_memcpy_toio

static inline void fb_memset_io(volatile void __iomem *addr, int c, size_t n)
{
	sbus_memset_io(addr, c, n);
}
#define fb_memset fb_memset_io

#include <asm-generic/fb.h>

#endif /* _SPARC_FB_H_ */
