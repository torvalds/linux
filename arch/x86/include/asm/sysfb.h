#ifndef _ARCH_X86_KERNEL_SYSFB_H
#define _ARCH_X86_KERNEL_SYSFB_H

/*
 * Generic System Framebuffers on x86
 * Copyright (c) 2012-2013 David Herrmann <dh.herrmann@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/kernel.h>
#include <linux/platform_data/simplefb.h>
#include <linux/screen_info.h>

#ifdef CONFIG_X86_SYSFB

bool parse_mode(const struct screen_info *si,
		struct simplefb_platform_data *mode);
int create_simplefb(const struct screen_info *si,
		    const struct simplefb_platform_data *mode);

#else /* CONFIG_X86_SYSFB */

static inline bool parse_mode(const struct screen_info *si,
			      struct simplefb_platform_data *mode)
{
	return false;
}

static inline int create_simplefb(const struct screen_info *si,
				  const struct simplefb_platform_data *mode)
{
	return -EINVAL;
}

#endif /* CONFIG_X86_SYSFB */

#endif /* _ARCH_X86_KERNEL_SYSFB_H */
