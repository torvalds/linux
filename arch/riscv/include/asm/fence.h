#ifndef _ASM_RISCV_FENCE_H
#define _ASM_RISCV_FENCE_H

#define RISCV_FENCE_ASM(p, s)		"\tfence " #p "," #s "\n"
#define RISCV_FENCE(p, s) \
	({ __asm__ __volatile__ (RISCV_FENCE_ASM(p, s) : : : "memory"); })

#ifdef CONFIG_SMP
#define RISCV_ACQUIRE_BARRIER		RISCV_FENCE_ASM(r, rw)
#define RISCV_RELEASE_BARRIER		RISCV_FENCE_ASM(rw, w)
#define RISCV_FULL_BARRIER		RISCV_FENCE_ASM(rw, rw)
#else
#define RISCV_ACQUIRE_BARRIER
#define RISCV_RELEASE_BARRIER
#define RISCV_FULL_BARRIER
#endif

#endif	/* _ASM_RISCV_FENCE_H */
