/*
 * linux/arch/arm64/include/asm/neon.h
 *
 * Copyright (C) 2013 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_NEON_H
#define __ASM_NEON_H

#include <linux/types.h>
#include <asm/fpsimd.h>

#define cpu_has_neon()		system_supports_fpsimd()

void kernel_neon_begin(void);
void kernel_neon_end(void);

/*
 * Temporary macro to allow the crypto code to compile. Note that the
 * semantics of kernel_neon_begin_partial() are now different from the
 * original as it does not allow being called in an interrupt context.
 */
#define kernel_neon_begin_partial(num_regs)	kernel_neon_begin()

#endif /* ! __ASM_NEON_H */
