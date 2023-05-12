/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC_FB_H_
#define _SPARC_FB_H_

#include <linux/io.h>

struct fb_info;
struct file;
struct vm_area_struct;

#ifdef CONFIG_SPARC32
static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{ }
#define fb_pgprotect fb_pgprotect
#endif

int fb_is_primary_device(struct fb_info *info);
#define fb_is_primary_device fb_is_primary_device

static inline void fb_memcpy_fromfb(void *to, const volatile void __iomem *from, size_t n)
{
	sbus_memcpy_fromio(to, from, n);
}
#define fb_memcpy_fromfb fb_memcpy_fromfb

static inline void fb_memcpy_tofb(volatile void __iomem *to, const void *from, size_t n)
{
	sbus_memcpy_toio(to, from, n);
}
#define fb_memcpy_tofb fb_memcpy_tofb

static inline void fb_memset(volatile void __iomem *addr, int c, size_t n)
{
	sbus_memset_io(addr, c, n);
}
#define fb_memset fb_memset

#include <asm-generic/fb.h>

#endif /* _SPARC_FB_H_ */
