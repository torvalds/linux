/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CPUID_API_H
#define _ASM_X86_CPUID_API_H

#include <asm/cpuid/types.h>

#include <linux/build_bug.h>
#include <linux/types.h>

#include <asm/string.h>

/*
 * Raw CPUID accessors:
 */

#ifdef CONFIG_X86_32
bool cpuid_feature(void);
#else
static inline bool cpuid_feature(void)
{
	return true;
}
#endif

static inline void native_cpuid(u32 *eax, u32 *ebx,
				u32 *ecx, u32 *edx)
{
	/* ecx is often an input as well as an output. */
	asm volatile("cpuid"
	    : "=a" (*eax),
	      "=b" (*ebx),
	      "=c" (*ecx),
	      "=d" (*edx)
	    : "0" (*eax), "2" (*ecx)
	    : "memory");
}

#define NATIVE_CPUID_REG(reg)					\
static inline u32 native_cpuid_##reg(u32 op)			\
{								\
	u32 eax = op, ebx, ecx = 0, edx;			\
								\
	native_cpuid(&eax, &ebx, &ecx, &edx);			\
								\
	return reg;						\
}

/*
 * Native CPUID functions returning a single datum:
 */
NATIVE_CPUID_REG(eax)
NATIVE_CPUID_REG(ebx)
NATIVE_CPUID_REG(ecx)
NATIVE_CPUID_REG(edx)

#ifdef CONFIG_PARAVIRT_XXL
# include <asm/paravirt.h>
#else
# define __cpuid native_cpuid
#endif

/*
 * Generic CPUID function
 *
 * Clear ECX since some CPUs (Cyrix MII) do not set or clear ECX
 * resulting in stale register contents being returned.
 */
static inline void cpuid(u32 op,
			 u32 *eax, u32 *ebx,
			 u32 *ecx, u32 *edx)
{
	*eax = op;
	*ecx = 0;
	__cpuid(eax, ebx, ecx, edx);
}

/* Some CPUID calls want 'count' to be placed in ECX */
static inline void cpuid_count(u32 op, int count,
			       u32 *eax, u32 *ebx,
			       u32 *ecx, u32 *edx)
{
	*eax = op;
	*ecx = count;
	__cpuid(eax, ebx, ecx, edx);
}

/*
 * CPUID functions returning a single datum:
 */

static inline u32 cpuid_eax(u32 op)
{
	u32 eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return eax;
}

static inline u32 cpuid_ebx(u32 op)
{
	u32 eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return ebx;
}

static inline u32 cpuid_ecx(u32 op)
{
	u32 eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return ecx;
}

static inline u32 cpuid_edx(u32 op)
{
	u32 eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return edx;
}

static inline void __cpuid_read(u32 leaf, u32 subleaf, u32 *regs)
{
	regs[CPUID_EAX] = leaf;
	regs[CPUID_ECX] = subleaf;
	__cpuid(regs + CPUID_EAX, regs + CPUID_EBX, regs + CPUID_ECX, regs + CPUID_EDX);
}

#define cpuid_subleaf(leaf, subleaf, regs) {		\
	static_assert(sizeof(*(regs)) == 16);		\
	__cpuid_read(leaf, subleaf, (u32 *)(regs));	\
}

#define cpuid_leaf(leaf, regs) {			\
	static_assert(sizeof(*(regs)) == 16);		\
	__cpuid_read(leaf, 0, (u32 *)(regs));		\
}

static inline void __cpuid_read_reg(u32 leaf, u32 subleaf,
				    enum cpuid_regs_idx regidx, u32 *reg)
{
	u32 regs[4];

	__cpuid_read(leaf, subleaf, regs);
	*reg = regs[regidx];
}

#define cpuid_subleaf_reg(leaf, subleaf, regidx, reg) {		\
	static_assert(sizeof(*(reg)) == 4);			\
	__cpuid_read_reg(leaf, subleaf, regidx, (u32 *)(reg));	\
}

#define cpuid_leaf_reg(leaf, regidx, reg) {			\
	static_assert(sizeof(*(reg)) == 4);			\
	__cpuid_read_reg(leaf, 0, regidx, (u32 *)(reg));	\
}

/*
 * Hypervisor-related APIs:
 */

static __always_inline bool cpuid_function_is_indexed(u32 function)
{
	switch (function) {
	case 4:
	case 7:
	case 0xb:
	case 0xd:
	case 0xf:
	case 0x10:
	case 0x12:
	case 0x14:
	case 0x17:
	case 0x18:
	case 0x1d:
	case 0x1e:
	case 0x1f:
	case 0x24:
	case 0x8000001d:
		return true;
	}

	return false;
}

#define for_each_possible_cpuid_base_hypervisor(function) \
	for (function = 0x40000000; function < 0x40010000; function += 0x100)

static inline u32 cpuid_base_hypervisor(const char *sig, u32 leaves)
{
	u32 base, eax, signature[3];

	for_each_possible_cpuid_base_hypervisor(base) {
		cpuid(base, &eax, &signature[0], &signature[1], &signature[2]);

		/*
		 * This must not compile to "call memcmp" because it's called
		 * from PVH early boot code before instrumentation is set up
		 * and memcmp() itself may be instrumented.
		 */
		if (!__builtin_memcmp(sig, signature, 12) &&
		    (leaves == 0 || ((eax - base) >= leaves)))
			return base;
	}

	return 0;
}

/*
 * CPUID(0x2) parsing:
 */

/**
 * cpuid_leaf_0x2() - Return sanitized CPUID(0x2) register output
 * @regs:	Output parameter
 *
 * Query CPUID(0x2) and store its output in @regs.  Force set any
 * invalid 1-byte descriptor returned by the hardware to zero (the NULL
 * cache/TLB descriptor) before returning it to the caller.
 *
 * Use for_each_cpuid_0x2_desc() to iterate over the register output in
 * parsed form.
 */
static inline void cpuid_leaf_0x2(union leaf_0x2_regs *regs)
{
	cpuid_leaf(0x2, regs);

	/*
	 * All Intel CPUs must report an iteration count of 1.	In case
	 * of bogus hardware, treat all returned descriptors as NULL.
	 */
	if (regs->desc[0] != 0x01) {
		for (int i = 0; i < 4; i++)
			regs->regv[i] = 0;
		return;
	}

	/*
	 * The most significant bit (MSB) of each register must be clear.
	 * If a register is invalid, replace its descriptors with NULL.
	 */
	for (int i = 0; i < 4; i++) {
		if (regs->reg[i].invalid)
			regs->regv[i] = 0;
	}
}

/**
 * for_each_cpuid_0x2_desc() - Iterator for parsed CPUID(0x2) descriptors
 * @_regs:	CPUID(0x2) register output, as returned by cpuid_leaf_0x2()
 * @_ptr:	u8 pointer, for macro internal use only
 * @_desc:	Pointer to the parsed CPUID(0x2) descriptor at each iteration
 *
 * Loop over the 1-byte descriptors in the passed CPUID(0x2) output registers
 * @_regs.  Provide the parsed information for each descriptor through @_desc.
 *
 * To handle cache-specific descriptors, switch on @_desc->c_type.  For TLB
 * descriptors, switch on @_desc->t_type.
 *
 * Example usage for cache descriptors::
 *
 *	const struct leaf_0x2_table *desc;
 *	union leaf_0x2_regs regs;
 *	u8 *ptr;
 *
 *	cpuid_leaf_0x2(&regs);
 *	for_each_cpuid_0x2_desc(regs, ptr, desc) {
 *		switch (desc->c_type) {
 *			...
 *		}
 *	}
 */
#define for_each_cpuid_0x2_desc(_regs, _ptr, _desc)				\
	for (_ptr = &(_regs).desc[1];						\
	     _ptr < &(_regs).desc[16] && (_desc = &cpuid_0x2_table[*_ptr]);	\
	     _ptr++)

/*
 * CPUID(0x80000006) parsing:
 */

static inline bool cpuid_amd_hygon_has_l3_cache(void)
{
	return cpuid_edx(0x80000006);
}

#endif /* _ASM_X86_CPUID_API_H */
