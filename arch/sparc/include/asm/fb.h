/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC_FB_H_
#define _SPARC_FB_H_

#include <linux/fs.h>

#include <asm/page.h>

struct fb_info;

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
#ifdef CONFIG_SPARC64
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#endif
}

int fb_is_primary_device(struct fb_info *info);

#endif /* _SPARC_FB_H_ */
