/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FB_H_
#define _ASM_FB_H_

#include <linux/fs.h>

#include <asm/page.h>

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
	/*
	 * PowerPC's implementation of phys_mem_access_prot() does
	 * not use the file argument. Set it to NULL in preparation
	 * of later updates to the interface.
	 */
	vma->vm_page_prot = phys_mem_access_prot(NULL, PHYS_PFN(off),
						 vma->vm_end - vma->vm_start,
						 vma->vm_page_prot);
}
#define fb_pgprotect fb_pgprotect

#include <asm-generic/fb.h>

#endif /* _ASM_FB_H_ */
