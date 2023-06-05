/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 SiFive
 */

#ifndef __ASM_RISCV_VECTOR_H
#define __ASM_RISCV_VECTOR_H

#include <linux/types.h>
#include <uapi/asm-generic/errno.h>

#ifdef CONFIG_RISCV_ISA_V

#include <asm/hwcap.h>
#include <asm/csr.h>

extern unsigned long riscv_v_vsize;
int riscv_v_setup_vsize(void);

static __always_inline bool has_vector(void)
{
	return riscv_has_extension_unlikely(RISCV_ISA_EXT_v);
}

static __always_inline void riscv_v_enable(void)
{
	csr_set(CSR_SSTATUS, SR_VS);
}

static __always_inline void riscv_v_disable(void)
{
	csr_clear(CSR_SSTATUS, SR_VS);
}

#else /* ! CONFIG_RISCV_ISA_V  */

struct pt_regs;

static inline int riscv_v_setup_vsize(void) { return -EOPNOTSUPP; }
static __always_inline bool has_vector(void) { return false; }
#define riscv_v_vsize (0)

#endif /* CONFIG_RISCV_ISA_V */

#endif /* ! __ASM_RISCV_VECTOR_H */
