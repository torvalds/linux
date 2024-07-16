/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_VIDEO_H_
#define _ASM_VIDEO_H_

#include <linux/compiler.h>
#include <linux/string.h>

static inline void fb_memcpy_fromio(void *to, const volatile void __iomem *from, size_t n)
{
	memcpy(to, (void __force *)from, n);
}
#define fb_memcpy_fromio fb_memcpy_fromio

static inline void fb_memcpy_toio(volatile void __iomem *to, const void *from, size_t n)
{
	memcpy((void __force *)to, from, n);
}
#define fb_memcpy_toio fb_memcpy_toio

static inline void fb_memset_io(volatile void __iomem *addr, int c, size_t n)
{
	memset((void __force *)addr, c, n);
}
#define fb_memset fb_memset_io

#include <asm-generic/video.h>

#endif /* _ASM_VIDEO_H_ */
