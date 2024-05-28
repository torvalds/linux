/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_FB_H
#define _ASM_X86_FB_H

#include <asm/page.h>

struct fb_info;

pgprot_t pgprot_framebuffer(pgprot_t prot,
			    unsigned long vm_start, unsigned long vm_end,
			    unsigned long offset);
#define pgprot_framebuffer pgprot_framebuffer

int fb_is_primary_device(struct fb_info *info);
#define fb_is_primary_device fb_is_primary_device

#include <asm-generic/fb.h>

#endif /* _ASM_X86_FB_H */
