/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FB_H_
#define _ASM_FB_H_

#include <linux/efi.h>

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

#include <asm-generic/fb.h>

#endif /* _ASM_FB_H_ */
