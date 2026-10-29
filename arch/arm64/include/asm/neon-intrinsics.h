/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 */

#ifndef __ASM_NEON_INTRINSICS_H
#define __ASM_NEON_INTRINSICS_H

/*
 * <arm_neon.h> includes <stdint.h>, which defines some C99 types differently
 * from <linux/types.h>, so the latter must not be included into code that uses
 * NEON intrinsics. The kernel's u8/u16/u32/u64 types can be used alongside the
 * C99 ones, so provide those instead.
 */
#include <asm-generic/int-ll64.h>

/*
 * genksyms chokes on the ARM NEON instrinsics system header, but we
 * don't export anything it defines anyway, so just disregard when
 * genksyms execute.
 */
#ifndef __GENKSYMS__
#include <arm_neon.h>
#endif

#endif /* __ASM_NEON_INTRINSICS_H */
