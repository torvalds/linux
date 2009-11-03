#ifndef __ASM_SH_TYPES_H
#define __ASM_SH_TYPES_H

#include <asm-generic/types.h>

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#ifdef CONFIG_SUPERH32
typedef u16 insn_size_t;
typedef u32 reg_size_t;
#else
typedef u32 insn_size_t;
typedef u64 reg_size_t;
#endif

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */

#endif /* __ASM_SH_TYPES_H */
