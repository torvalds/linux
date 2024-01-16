/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/arm/include/asm/neon.h
 *
 * Copyright (C) 2013 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/hwcap.h>

#define cpu_has_neon()		(!!(elf_hwcap & HWCAP_NEON))

#ifdef __ARM_NEON__

/*
 * If you are affected by the BUILD_BUG below, it probably means that you are
 * using NEON code /and/ calling the kernel_neon_begin() function from the same
 * compilation unit. To prevent issues that may arise from GCC reordering or
 * generating(1) NEON instructions outside of these begin/end functions, the
 * only supported way of using NEON code in the kernel is by isolating it in a
 * separate compilation unit, and calling it from another unit from inside a
 * kernel_neon_begin/kernel_neon_end pair.
 *
 * (1) Current GCC (4.7) might generate NEON instructions at O3 level if
 *     -mpfu=neon is set.
 */

#define kernel_neon_begin() \
	BUILD_BUG_ON_MSG(1, "kernel_neon_begin() called from NEON code")

#else
void kernel_neon_begin(void);
#endif
void kernel_neon_end(void);
