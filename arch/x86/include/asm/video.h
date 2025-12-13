/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_VIDEO_H
#define _ASM_X86_VIDEO_H

#include <linux/types.h>

#include <asm/page.h>

struct device;

pgprot_t pgprot_framebuffer(pgprot_t prot,
			    unsigned long vm_start, unsigned long vm_end,
			    unsigned long offset);
#define pgprot_framebuffer pgprot_framebuffer

#ifdef CONFIG_VIDEO
bool video_is_primary_device(struct device *dev);
#define video_is_primary_device video_is_primary_device
#endif

#include <asm-generic/video.h>

#endif /* _ASM_X86_VIDEO_H */
