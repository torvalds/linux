/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_TYPES_H
#define __ASM_SH_TYPES_H

#include <uapi/asm/types.h>

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifndef __ASSEMBLY__

#ifdef CONFIG_SUPERH32
typedef u16 insn_size_t;
typedef u32 reg_size_t;
#else
typedef u32 insn_size_t;
typedef u64 reg_size_t;
#endif

#endif /* __ASSEMBLY__ */
#endif /* __ASM_SH_TYPES_H */
