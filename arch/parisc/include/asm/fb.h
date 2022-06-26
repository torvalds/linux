/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FB_H_
#define _ASM_FB_H_

#include <linux/fb.h>
#include <linux/fs.h>
#include <asm/page.h>

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
	pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE;
}

#if defined(CONFIG_FB_STI)
int fb_is_primary_device(struct fb_info *info);
#else
static inline int fb_is_primary_device(struct fb_info *info)
{
	return 0;
}
#endif

#endif /* _ASM_FB_H_ */
