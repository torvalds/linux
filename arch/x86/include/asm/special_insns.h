#ifndef _ASM_X86_SPECIAL_INSNS_H
#define _ASM_X86_SPECIAL_INSNS_H


#ifdef __KERNEL__

#include <asm/nops.h>

static inline void native_clts(void)
{
	asm volatile("clts");
}

/*
 * Volatile isn't enough to prevent the compiler from reordering the
 * read/write functions for the control registers and messing everything up.
 * A memory clobber would solve the problem, but would prevent reordering of
 * all loads stores around it, which can hurt performance. Solution is to
 * use a variable and mimic reads and writes to it to enforce serialization
 */
extern unsigned long __force_order;

static inline unsigned long native_read_cr0(void)
{
	unsigned long val;
	asm volatile("mov %%cr0,%0\n\t" : "=r" (val), "=m" (__force_order));
	return val;
}

static inline void native_write_cr0(unsigned long val)
{
	asm volatile("mov %0,%%cr0": : "r" (val), "m" (__force_order));
}

static inline unsigned long native_read_cr2(void)
{
	unsigned long val;
	asm volatile("mov %%cr2,%0\n\t" : "=r" (val), "=m" (__force_order));
	return val;
}

static inline void native_write_cr2(unsigned long val)
{
	asm volatile("mov %0,%%cr2": : "r" (val), "m" (__force_order));
}

static inline unsigned long native_read_cr3(void)
{
	unsigned long val;
	asm volatile("mov %%cr3,%0\n\t" : "=r" (val), "=m" (__force_order));
	return val;
}

static inline void native_write_cr3(unsigned long val)
{
	asm volatile("mov %0,%%cr3": : "r" (val), "m" (__force_order));
}

static inline unsigned long native_read_cr4(void)
{
	unsigned long val;
	asm volatile("mov %%cr4,%0\n\t" : "=r" (val), "=m" (__force_order));
	return val;
}

static inline unsigned long native_read_cr4_safe(void)
{
	unsigned long val;
	/* This could fault if %cr4 does not exist. In x86_64, a cr4 always
	 * exists, so it will never fail. */
#ifdef CONFIG_X86_32
	asm volatile("1: mov %%cr4, %0\n"
		     "2:\n"
		     _ASM_EXTABLE(1b, 2b)
		     : "=r" (val), "=m" (__force_order) : "0" (0));
#else
	val = native_read_cr4();
#endif
	return val;
}

static inline void native_write_cr4(unsigned long val)
{
	asm volatile("mov %0,%%cr4": : "r" (val), "m" (__force_order));
}

#ifdef CONFIG_X86_64
static inline unsigned long native_read_cr8(void)
{
	unsigned long cr8;
	asm volatile("movq %%cr8,%0" : "=r" (cr8));
	return cr8;
}

static inline void native_write_cr8(unsigned long val)
{
	asm volatile("movq %0,%%cr8" :: "r" (val) : "memory");
}
#endif

static inline void native_wbinvd(void)
{
	asm volatile("wbinvd": : :"memory");
}

extern asmlinkage void native_load_gs_index(unsigned);

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else

static inline unsigned long read_cr0(void)
{
	return native_read_cr0();
}

static inline void write_cr0(unsigned long x)
{
	native_write_cr0(x);
}

static inline unsigned long read_cr2(void)
{
	return native_read_cr2();
}

static inline void write_cr2(unsigned long x)
{
	native_write_cr2(x);
}

static inline unsigned long read_cr3(void)
{
	return native_read_cr3();
}

static inline void write_cr3(unsigned long x)
{
	native_write_cr3(x);
}

static inline unsigned long __read_cr4(void)
{
	return native_read_cr4();
}

static inline unsigned long __read_cr4_safe(void)
{
	return native_read_cr4_safe();
}

static inline void __write_cr4(unsigned long x)
{
	native_write_cr4(x);
}

static inline void wbinvd(void)
{
	native_wbinvd();
}

#ifdef CONFIG_X86_64

static inline unsigned long read_cr8(void)
{
	return native_read_cr8();
}

static inline void write_cr8(unsigned long x)
{
	native_write_cr8(x);
}

static inline void load_gs_index(unsigned selector)
{
	native_load_gs_index(selector);
}

#endif

/* Clear the 'TS' bit */
static inline void clts(void)
{
	native_clts();
}

#endif/* CONFIG_PARAVIRT */

#define stts() write_cr0(read_cr0() | X86_CR0_TS)

static inline void clflush(volatile void *__p)
{
	asm volatile("clflush %0" : "+m" (*(volatile char __force *)__p));
}

static inline void clflushopt(volatile void *__p)
{
	alternative_io(".byte " __stringify(NOP_DS_PREFIX) "; clflush %P0",
		       ".byte 0x66; clflush %P0",
		       X86_FEATURE_CLFLUSHOPT,
		       "+m" (*(volatile char __force *)__p));
}

static inline void clwb(volatile void *__p)
{
	volatile struct { char x[64]; } *p = __p;

	asm volatile(ALTERNATIVE_2(
		".byte " __stringify(NOP_DS_PREFIX) "; clflush (%[pax])",
		".byte 0x66; clflush (%[pax])", /* clflushopt (%%rax) */
		X86_FEATURE_CLFLUSHOPT,
		".byte 0x66, 0x0f, 0xae, 0x30",  /* clwb (%%rax) */
		X86_FEATURE_CLWB)
		: [p] "+m" (*p)
		: [pax] "a" (p));
}

/**
 * pcommit_sfence() - persistent commit and fence
 *
 * The PCOMMIT instruction ensures that data that has been flushed from the
 * processor's cache hierarchy with CLWB, CLFLUSHOPT or CLFLUSH is accepted to
 * memory and is durable on the DIMM.  The primary use case for this is
 * persistent memory.
 *
 * This function shows how to properly use CLWB/CLFLUSHOPT/CLFLUSH and PCOMMIT
 * with appropriate fencing.
 *
 * Example:
 * void flush_and_commit_buffer(void *vaddr, unsigned int size)
 * {
 *         unsigned long clflush_mask = boot_cpu_data.x86_clflush_size - 1;
 *         void *vend = vaddr + size;
 *         void *p;
 *
 *         for (p = (void *)((unsigned long)vaddr & ~clflush_mask);
 *              p < vend; p += boot_cpu_data.x86_clflush_size)
 *                 clwb(p);
 *
 *         // SFENCE to order CLWB/CLFLUSHOPT/CLFLUSH cache flushes
 *         // MFENCE via mb() also works
 *         wmb();
 *
 *         // PCOMMIT and the required SFENCE for ordering
 *         pcommit_sfence();
 * }
 *
 * After this function completes the data pointed to by 'vaddr' has been
 * accepted to memory and will be durable if the 'vaddr' points to persistent
 * memory.
 *
 * PCOMMIT must always be ordered by an MFENCE or SFENCE, so to help simplify
 * things we include both the PCOMMIT and the required SFENCE in the
 * alternatives generated by pcommit_sfence().
 */
static inline void pcommit_sfence(void)
{
	alternative(ASM_NOP7,
		    ".byte 0x66, 0x0f, 0xae, 0xf8\n\t" /* pcommit */
		    "sfence",
		    X86_FEATURE_PCOMMIT);
}

#define nop() asm volatile ("nop")


#endif /* __KERNEL__ */

#endif /* _ASM_X86_SPECIAL_INSNS_H */
