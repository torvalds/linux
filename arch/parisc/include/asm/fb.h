/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FB_H_
#define _ASM_FB_H_

struct fb_info;

#if defined(CONFIG_STI_CORE)
int fb_is_primary_device(struct fb_info *info);
#define fb_is_primary_device fb_is_primary_device
#endif

#include <asm-generic/fb.h>

#endif /* _ASM_FB_H_ */
