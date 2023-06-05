/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 SiFive
 */

#ifndef __ASM_RISCV_VECTOR_H
#define __ASM_RISCV_VECTOR_H

#include <linux/types.h>

#ifdef CONFIG_RISCV_ISA_V

#include <asm/hwcap.h>

static __always_inline bool has_vector(void)
{
	return riscv_has_extension_unlikely(RISCV_ISA_EXT_v);
}

#else /* ! CONFIG_RISCV_ISA_V  */

static __always_inline bool has_vector(void) { return false; }

#endif /* CONFIG_RISCV_ISA_V */

#endif /* ! __ASM_RISCV_VECTOR_H */
