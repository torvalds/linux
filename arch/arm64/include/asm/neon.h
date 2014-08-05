/*
 * linux/arch/arm64/include/asm/neon.h
 *
 * Copyright (C) 2013 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>

#define cpu_has_neon()		(1)

#define kernel_neon_begin()	kernel_neon_begin_partial(32)

void kernel_neon_begin_partial(u32 num_regs);
void kernel_neon_end(void);
