#ifndef __ASM_SH_CPU_FEATURES_H
#define __ASM_SH_CPU_FEATURES_H

/*
 * Processor flags
 *
 * Note: When adding a new flag, keep cpu_flags[] in
 * arch/sh/kernel/setup.c in sync so symbolic name
 * mapping of the processor flags has a chance of being
 * reasonably accurate.
 *
 * These flags are also available through the ELF
 * auxiliary vector as AT_HWCAP.
 */
#define CPU_HAS_FPU		0x0001	/* Hardware FPU support */
#define CPU_HAS_P2_FLUSH_BUG	0x0002	/* Need to flush the cache in P2 area */
#define CPU_HAS_MMU_PAGE_ASSOC	0x0004	/* SH3: TLB way selection bit support */
#define CPU_HAS_DSP		0x0008	/* SH-DSP: DSP support */
#define CPU_HAS_PERF_COUNTER	0x0010	/* Hardware performance counters */
#define CPU_HAS_PTEA		0x0020	/* PTEA register */
#define CPU_HAS_LLSC		0x0040	/* movli.l/movco.l */
#define CPU_HAS_L2_CACHE	0x0080	/* Secondary cache / URAM */
#define CPU_HAS_OP32		0x0100	/* 32-bit instruction support */
#define CPU_HAS_PTEAEX		0x0200	/* PTE ASID Extension support */

#endif /* __ASM_SH_CPU_FEATURES_H */
