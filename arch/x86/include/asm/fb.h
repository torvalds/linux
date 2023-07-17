/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_FB_H
#define _ASM_X86_FB_H

struct fb_info;
struct file;
struct vm_area_struct;

void fb_pgprotect(struct file *file, struct vm_area_struct *vma, unsigned long off);
#define fb_pgprotect fb_pgprotect

int fb_is_primary_device(struct fb_info *info);
#define fb_is_primary_device fb_is_primary_device

#include <asm-generic/fb.h>

#endif /* _ASM_X86_FB_H */
