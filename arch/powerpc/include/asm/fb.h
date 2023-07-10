/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FB_H_
#define _ASM_FB_H_

#include <linux/fs.h>

#include <asm/page.h>

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
	vma->vm_page_prot = phys_mem_access_prot(file, off >> PAGE_SHIFT,
						 vma->vm_end - vma->vm_start,
						 vma->vm_page_prot);
}
#define fb_pgprotect fb_pgprotect

#include <asm-generic/fb.h>

#endif /* _ASM_FB_H_ */
