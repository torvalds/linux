/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 SiFive
 */

#ifndef __ASM_FPU_H
#define __ASM_FPU_H

#include <asm/neon.h>

#define kernel_fpu_available()	cpu_has_neon()
#define kernel_fpu_begin()	kernel_neon_begin()
#define kernel_fpu_end()	kernel_neon_end()

#endif /* ! __ASM_FPU_H */
