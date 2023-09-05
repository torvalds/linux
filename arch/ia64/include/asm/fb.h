/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FB_H_
#define _ASM_FB_H_

#include <linux/compiler.h>
#include <linux/efi.h>
#include <linux/string.h>

#include <asm/page.h>

struct file;

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
	if (efi_range_is_wc(vma->vm_start, vma->vm_end - vma->vm_start))
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
}
#define fb_pgprotect fb_pgprotect

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
