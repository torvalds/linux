/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/asm-parisc/cache.h
 */

#ifndef __ARCH_PARISC_CACHE_H
#define __ARCH_PARISC_CACHE_H

#include <asm/alternative.h>

/*
 * PA 2.0 processors have 64 and 128-byte L2 cachelines; PA 1.1 processors
 * have 32-byte cachelines.  The L1 length appears to be 16 bytes but this
 * is not clearly documented.
 */
#define L1_CACHE_BYTES 16
#define L1_CACHE_SHIFT 4

#ifndef __ASSEMBLY__

#define SMP_CACHE_BYTES L1_CACHE_BYTES

#ifdef CONFIG_PA20
#define ARCH_DMA_MINALIGN	128
#else
#define ARCH_DMA_MINALIGN	32
#endif
#define ARCH_KMALLOC_MINALIGN	16	/* ldcw requires 16-byte alignment */

#define arch_slab_minalign()	((unsigned)dcache_stride)
#define cache_line_size()	dcache_stride
#define dma_get_cache_alignment cache_line_size

#define __read_mostly __section(".data..read_mostly")

void parisc_cache_init(void);	/* initializes cache-flushing */
void disable_sr_hashing_asm(int); /* low level support for above */
void disable_sr_hashing(void);   /* turns off space register hashing */
void free_sid(unsigned long);
unsigned long alloc_sid(void);

struct seq_file;
extern void show_cache_info(struct seq_file *m);

extern int split_tlb;
extern int dcache_stride;
extern int icache_stride;
extern struct pdc_cache_info cache_info;
extern struct pdc_btlb_info btlb_info;
void parisc_setup_cache_timing(void);

#define pdtlb(sr, addr)	asm volatile("pdtlb 0(%%sr%0,%1)" \
			ALTERNATIVE(ALT_COND_NO_SMP, INSN_PxTLB) \
			: : "i"(sr), "r" (addr) : "memory")
#define pitlb(sr, addr)	asm volatile("pitlb 0(%%sr%0,%1)" \
			ALTERNATIVE(ALT_COND_NO_SMP, INSN_PxTLB) \
			ALTERNATIVE(ALT_COND_NO_SPLIT_TLB, INSN_NOP) \
			: : "i"(sr), "r" (addr) : "memory")

#define asm_io_fdc(addr) asm volatile("fdc %%r0(%0)" \
			ALTERNATIVE(ALT_COND_NO_DCACHE, INSN_NOP) \
			ALTERNATIVE(ALT_COND_NO_IOC_FDC, INSN_NOP) \
			: : "r" (addr) : "memory")
#define asm_io_sync()	asm volatile("sync" \
			ALTERNATIVE(ALT_COND_NO_DCACHE, INSN_NOP) \
			ALTERNATIVE(ALT_COND_NO_IOC_FDC, INSN_NOP) :::"memory")
#define asm_syncdma()	asm volatile("syncdma" :::"memory")

#endif /* ! __ASSEMBLY__ */

/* Classes of processor wrt: disabling space register hashing */

#define SRHASH_PCXST    0   /* pcxs, pcxt, pcxt_ */
#define SRHASH_PCXL     1   /* pcxl */
#define SRHASH_PA20     2   /* pcxu, pcxu_, pcxw, pcxw_ */

#endif
