#ifndef _ASM_RISCV_FENCE_H
#define _ASM_RISCV_FENCE_H

#ifdef CONFIG_SMP
#define RISCV_ACQUIRE_BARRIER		"\tfence r , rw\n"
#define RISCV_RELEASE_BARRIER		"\tfence rw,  w\n"
#define RISCV_FULL_BARRIER		"\tfence rw, rw\n"
#else
#define RISCV_ACQUIRE_BARRIER
#define RISCV_RELEASE_BARRIER
#define RISCV_FULL_BARRIER
#endif

#endif	/* _ASM_RISCV_FENCE_H */
