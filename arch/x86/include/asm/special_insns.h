/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SPECIAL_INSNS_H
#define _ASM_X86_SPECIAL_INSNS_H


#ifdef __KERNEL__

#include <asm/nops.h>
#include <asm/processor-flags.h>
#include <linux/irqflags.h>
#include <linux/jump_label.h>

/*
 * The compiler should not reorder volatile asm statements with respect to each
 * other: they should execute in program order. However GCC 4.9.x and 5.x have
 * a bug (which was fixed in 8.1, 7.3 and 6.5) where they might reorder
 * volatile asm. The write functions are not affected since they have memory
 * clobbers preventing reordering. To prevent reads from being reordered with
 * respect to writes, use a dummy memory operand.
 */

#define __FORCE_ORDER "m"(*(unsigned int *)0x1000UL)

void native_write_cr0(unsigned long val);

static inline unsigned long native_read_cr0(void)
{
	unsigned long val;
	asm volatile("mov %%cr0,%0\n\t" : "=r" (val) : __FORCE_ORDER);
	return val;
}

static __always_inline unsigned long native_read_cr2(void)
{
	unsigned long val;
	asm volatile("mov %%cr2,%0\n\t" : "=r" (val) : __FORCE_ORDER);
	return val;
}

static __always_inline void native_write_cr2(unsigned long val)
{
	asm volatile("mov %0,%%cr2": : "r" (val) : "memory");
}

static inline unsigned long __native_read_cr3(void)
{
	unsigned long val;
	asm volatile("mov %%cr3,%0\n\t" : "=r" (val) : __FORCE_ORDER);
	return val;
}

static inline void native_write_cr3(unsigned long val)
{
	asm volatile("mov %0,%%cr3": : "r" (val) : "memory");
}

static inline unsigned long native_read_cr4(void)
{
	unsigned long val;
#ifdef CONFIG_X86_32
	/*
	 * This could fault if CR4 does not exist.  Non-existent CR4
	 * is functionally equivalent to CR4 == 0.  Keep it simple and pretend
	 * that CR4 == 0 on CPUs that don't have CR4.
	 */
	asm volatile("1: mov %%cr4, %0\n"
		     "2:\n"
		     _ASM_EXTABLE(1b, 2b)
		     : "=r" (val) : "0" (0), __FORCE_ORDER);
#else
	/* CR4 always exists on x86_64. */
	asm volatile("mov %%cr4,%0\n\t" : "=r" (val) : __FORCE_ORDER);
#endif
	return val;
}

void native_write_cr4(unsigned long val);

#ifdef CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
static inline u32 rdpkru(void)
{
	u32 ecx = 0;
	u32 edx, pkru;

	/*
	 * "rdpkru" instruction.  Places PKRU contents in to EAX,
	 * clears EDX and requires that ecx=0.
	 */
	asm volatile(".byte 0x0f,0x01,0xee\n\t"
		     : "=a" (pkru), "=d" (edx)
		     : "c" (ecx));
	return pkru;
}

static inline void wrpkru(u32 pkru)
{
	u32 ecx = 0, edx = 0;

	/*
	 * "wrpkru" instruction.  Loads contents in EAX to PKRU,
	 * requires that ecx = edx = 0.
	 */
	asm volatile(".byte 0x0f,0x01,0xef\n\t"
		     : : "a" (pkru), "c"(ecx), "d"(edx));
}

#else
static inline u32 rdpkru(void)
{
	return 0;
}

static inline void wrpkru(u32 pkru)
{
}
#endif

static inline void native_wbinvd(void)
{
	asm volatile("wbinvd": : :"memory");
}

extern asmlinkage void asm_load_gs_index(unsigned int selector);

static inline void native_load_gs_index(unsigned int selector)
{
	unsigned long flags;

	local_irq_save(flags);
	asm_load_gs_index(selector);
	local_irq_restore(flags);
}

static inline unsigned long __read_cr4(void)
{
	return native_read_cr4();
}

#ifdef CONFIG_PARAVIRT_XXL
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

static __always_inline unsigned long read_cr2(void)
{
	return native_read_cr2();
}

static __always_inline void write_cr2(unsigned long x)
{
	native_write_cr2(x);
}

/*
 * Careful!  CR3 contains more than just an address.  You probably want
 * read_cr3_pa() instead.
 */
static inline unsigned long __read_cr3(void)
{
	return __native_read_cr3();
}

static inline void write_cr3(unsigned long x)
{
	native_write_cr3(x);
}

static inline void __write_cr4(unsigned long x)
{
	native_write_cr4(x);
}

static inline void wbinvd(void)
{
	native_wbinvd();
}


static inline void load_gs_index(unsigned int selector)
{
#ifdef CONFIG_X86_64
	native_load_gs_index(selector);
#else
	loadsegment(gs, selector);
#endif
}

#endif /* CONFIG_PARAVIRT_XXL */

static inline void clflush(volatile void *__p)
{
	asm volatile("clflush %0" : "+m" (*(volatile char __force *)__p));
}

static inline void clflushopt(volatile void *__p)
{
	alternative_io(".byte 0x3e; clflush %P0",
		       ".byte 0x66; clflush %P0",
		       X86_FEATURE_CLFLUSHOPT,
		       "+m" (*(volatile char __force *)__p));
}

static inline void clwb(volatile void *__p)
{
	volatile struct { char x[64]; } *p = __p;

	asm volatile(ALTERNATIVE_2(
		".byte 0x3e; clflush (%[pax])",
		".byte 0x66; clflush (%[pax])", /* clflushopt (%%rax) */
		X86_FEATURE_CLFLUSHOPT,
		".byte 0x66, 0x0f, 0xae, 0x30",  /* clwb (%%rax) */
		X86_FEATURE_CLWB)
		: [p] "+m" (*p)
		: [pax] "a" (p));
}

#define nop() asm volatile ("nop")

static inline void serialize(void)
{
	/* Instruction opcode for SERIALIZE; supported in binutils >= 2.35. */
	asm volatile(".byte 0xf, 0x1, 0xe8" ::: "memory");
}

/* The dst parameter must be 64-bytes aligned */
static inline void movdir64b(void __iomem *dst, const void *src)
{
	const struct { char _[64]; } *__src = src;
	struct { char _[64]; } __iomem *__dst = dst;

	/*
	 * MOVDIR64B %(rdx), rax.
	 *
	 * Both __src and __dst must be memory constraints in order to tell the
	 * compiler that no other memory accesses should be reordered around
	 * this one.
	 *
	 * Also, both must be supplied as lvalues because this tells
	 * the compiler what the object is (its size) the instruction accesses.
	 * I.e., not the pointers but what they point to, thus the deref'ing '*'.
	 */
	asm volatile(".byte 0x66, 0x0f, 0x38, 0xf8, 0x02"
		     : "+m" (*__dst)
		     :  "m" (*__src), "a" (__dst), "d" (__src));
}

/**
 * enqcmds - Enqueue a command in supervisor (CPL0) mode
 * @dst: destination, in MMIO space (must be 512-bit aligned)
 * @src: 512 bits memory operand
 *
 * The ENQCMDS instruction allows software to write a 512-bit command to
 * a 512-bit-aligned special MMIO region that supports the instruction.
 * A return status is loaded into the ZF flag in the RFLAGS register.
 * ZF = 0 equates to success, and ZF = 1 indicates retry or error.
 *
 * This function issues the ENQCMDS instruction to submit data from
 * kernel space to MMIO space, in a unit of 512 bits. Order of data access
 * is not guaranteed, nor is a memory barrier performed afterwards. It
 * returns 0 on success and -EAGAIN on failure.
 *
 * Warning: Do not use this helper unless your driver has checked that the
 * ENQCMDS instruction is supported on the platform and the device accepts
 * ENQCMDS.
 */
static inline int enqcmds(void __iomem *dst, const void *src)
{
	const struct { char _[64]; } *__src = src;
	struct { char _[64]; } __iomem *__dst = dst;
	bool zf;

	/*
	 * ENQCMDS %(rdx), rax
	 *
	 * See movdir64b()'s comment on operand specification.
	 */
	asm volatile(".byte 0xf3, 0x0f, 0x38, 0xf8, 0x02, 0x66, 0x90"
		     CC_SET(z)
		     : CC_OUT(z) (zf), "+m" (*__dst)
		     : "m" (*__src), "a" (__dst), "d" (__src));

	/* Submission failure is indicated via EFLAGS.ZF=1 */
	if (zf)
		return -EAGAIN;

	return 0;
}

static __always_inline void tile_release(void)
{
	/*
	 * Instruction opcode for TILERELEASE; supported in binutils
	 * version >= 2.36.
	 */
	asm volatile(".byte 0xc4, 0xe2, 0x78, 0x49, 0xc0");
}

#endif /* __KERNEL__ */

#endif /* _ASM_X86_SPECIAL_INSNS_H */
