/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FB_H_
#define _ASM_FB_H_

#include <linux/compiler.h>
#include <linux/efi.h>
#include <linux/string.h>

#include <asm/page.h>

static inline pgprot_t pgprot_framebuffer(pgprot_t prot,
					  unsigned long vm_start, unsigned long vm_end,
					  unsigned long offset)
{
	if (efi_range_is_wc(vm_start, vm_end - vm_start))
		return pgprot_writecombine(prot);
	else
		return pgprot_noncached(prot);
}
#define pgprot_framebuffer pgprot_framebuffer

static inline void fb_memcpy_fromio(void *to, const volatile void __iomem *from, size_t n)
{
	memcpy(to, (void __force *)from, n);
}
#define fb_memcpy_fromio fb_memcpy_fromio

static inline void fb_memcpy_toio(volatile void __iomem *to, const void *from, size_t n)
{
	memcpy((void __force *)to, from, n);
}
#define fb_memcpy_toio fb_memcpy_toio

static inline void fb_memset_io(volatile void __iomem *addr, int c, size_t n)
{
	memset((void __force *)addr, c, n);
}
#define fb_memset fb_memset_io

#include <asm-generic/fb.h>

#endif /* _ASM_FB_H_ */
