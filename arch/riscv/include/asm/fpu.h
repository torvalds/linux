/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 SiFive
 */

#ifndef _ASM_RISCV_FPU_H
#define _ASM_RISCV_FPU_H

#include <asm/switch_to.h>

#define kernel_fpu_available()	has_fpu()

void kernel_fpu_begin(void);
void kernel_fpu_end(void);

#endif /* ! _ASM_RISCV_FPU_H */
