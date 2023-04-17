/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_FB_H
#define _ASM_X86_FB_H

#include <asm/page.h>

struct fb_info;
struct file;

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
	unsigned long prot;

	prot = pgprot_val(vma->vm_page_prot) & ~_PAGE_CACHE_MASK;
	if (boot_cpu_data.x86 > 3)
		pgprot_val(vma->vm_page_prot) =
			prot | cachemode2protval(_PAGE_CACHE_MODE_UC_MINUS);
}
#define fb_pgprotect fb_pgprotect

int fb_is_primary_device(struct fb_info *info);
#define fb_is_primary_device fb_is_primary_device

#include <asm-generic/fb.h>

#endif /* _ASM_X86_FB_H */
