/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_FB_H_
#define __ASM_FB_H_

#include <linux/fb.h>
#include <linux/fs.h>
#include <asm/page.h>

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
}

static inline int fb_is_primary_device(struct fb_info *info)
{
	return 0;
}

#endif /* __ASM_FB_H_ */
