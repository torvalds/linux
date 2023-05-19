/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC_FB_H_
#define _SPARC_FB_H_

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

#include <asm-generic/fb.h>

#endif /* _SPARC_FB_H_ */
