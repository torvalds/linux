/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linker script macros to generate Image header fields.
 *
 * Copyright (C) 2014 ARM Ltd.
 */
#ifndef __ARM64_KERNEL_IMAGE_H
#define __ARM64_KERNEL_IMAGE_H

#ifndef LINKER_SCRIPT
#error This file should only be included in vmlinux.lds.S
#endif

#include <asm/image.h>

/*
 * There aren't any ELF relocations we can use to endian-swap values known only
 * at link time (e.g. the subtraction of two symbol addresses), so we must get
 * the linker to endian-swap certain values before emitting them.
 *
 * Note that, in order for this to work when building the ELF64 PIE executable
 * (for KASLR), these values should not be referenced via R_AARCH64_ABS64
 * relocations, since these are fixed up at runtime rather than at build time
 * when PIE is in effect. So we need to split them up in 32-bit high and low
 * words.
 */
#ifdef CONFIG_CPU_BIG_ENDIAN
#define DATA_LE32(data)				\
	((((data) & 0x000000ff) << 24) |	\
	 (((data) & 0x0000ff00) << 8)  |	\
	 (((data) & 0x00ff0000) >> 8)  |	\
	 (((data) & 0xff000000) >> 24))
#else
#define DATA_LE32(data) ((data) & 0xffffffff)
#endif

#define DEFINE_IMAGE_LE64(sym, data)				\
	sym##_lo32 = DATA_LE32((data) & 0xffffffff);		\
	sym##_hi32 = DATA_LE32((data) >> 32)

#define __HEAD_FLAG(field)	(__HEAD_FLAG_##field << \
					ARM64_IMAGE_FLAG_##field##_SHIFT)

#ifdef CONFIG_CPU_BIG_ENDIAN
#define __HEAD_FLAG_BE		ARM64_IMAGE_FLAG_BE
#else
#define __HEAD_FLAG_BE		ARM64_IMAGE_FLAG_LE
#endif

#define __HEAD_FLAG_PAGE_SIZE	((PAGE_SHIFT - 10) / 2)

#define __HEAD_FLAG_PHYS_BASE	1

#define __HEAD_FLAGS		(__HEAD_FLAG(BE)	| \
				 __HEAD_FLAG(PAGE_SIZE) | \
				 __HEAD_FLAG(PHYS_BASE))

/*
 * These will output as part of the Image header, which should be little-endian
 * regardless of the endianness of the kernel. While constant values could be
 * endian swapped in head.S, all are done here for consistency.
 */
#define HEAD_SYMBOLS						\
	DEFINE_IMAGE_LE64(_kernel_size_le, _end - _text);	\
	DEFINE_IMAGE_LE64(_kernel_offset_le, TEXT_OFFSET);	\
	DEFINE_IMAGE_LE64(_kernel_flags_le, __HEAD_FLAGS);

#endif /* __ARM64_KERNEL_IMAGE_H */
