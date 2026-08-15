/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CPUID_API_H
#define _ASM_X86_CPUID_API_H

#include <asm/cpuid/types.h>

#include <linux/build_bug.h>
#include <linux/types.h>

#include <asm/processor.h>
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

#define cpuid_read_subleaf(leaf, subleaf, regs) {	\
	static_assert(sizeof(*(regs)) == 16);		\
	__cpuid_read(leaf, subleaf, (u32 *)(regs));	\
}

#define cpuid_read(leaf, regs) {			\
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
	cpuid_read(0x2, regs);

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

/*
 * 'struct cpuid_leaves' accessors (without sanity checks):
 *
 * For internal use by the CPUID parser.
 */

/* Return constified pointers for all call-site APIs */
#define __const_ptr(_ptr)							\
	((const __typeof__(*(_ptr)) *)(_ptr))

#define __cpuid_leaves_subleaf(_leaves, _leaf, _subleaf)			\
	__const_ptr(&((_leaves)->leaf_ ## _leaf ## _ ## _subleaf)[0])

#define __cpuid_leaves_subleaf_n(_leaves, _leaf, _index)			\
	__const_ptr(&((_leaves)->leaf_ ## _leaf ## _ ## n)[_index])

#define __cpuid_leaves_subleaf_info(_leaves, _leaf, _subleaf)			\
	__const_ptr(&((_leaves)->leaf_ ## _leaf ## _ ## _subleaf ## _ ## info))

/*
 * 'struct cpuid_table' accessors (with sanity checks):
 *
 * For internal use by the CPUID parser.
 */

#define __cpuid_table_nr_filled_subleaves(_table, _leaf, _subleaf)		\
	__cpuid_leaves_subleaf_info(&((_table)->leaves), _leaf, _subleaf)->nr_entries

#define __cpuid_table_subleaf_range_size(_table, _leaf)				\
	ARRAY_SIZE((_table)->leaves.leaf_ ## _leaf ## _n)

#define __cpuid_table_invalid_subleaf(_table, _leaf, _subleaf)			\
	(((_subleaf) < (__cpuid_leaf_first_subleaf(_leaf))) ||			\
	 ((_subleaf) > (__cpuid_leaf_first_subleaf(_leaf) +			\
			__cpuid_table_subleaf_range_size(_table, _leaf) - 1)))

/* Return NULL if the parser did not fill that leaf.  Check cpuid_subleaf(). */
#define __cpuid_table_subleaf(_table, _leaf, _subleaf)						\
({												\
	unsigned int ____f = __cpuid_table_nr_filled_subleaves(_table, _leaf, _subleaf);	\
												\
	(____f != 1) ? NULL : __cpuid_leaves_subleaf(&((_table)->leaves), _leaf, _subleaf);	\
})

/*
 * Return NULL if the CPUID parser did not fill this leaf, or if the given
 * dynamic subleaf value is out of range.  Check cpuid_subleaf_n().
 */
#define __cpuid_table_subleaf_n(_table, _leaf, _subleaf)					\
({												\
	unsigned int ____i = (_subleaf) - __cpuid_leaf_first_subleaf(_leaf);			\
	unsigned int ____f = __cpuid_table_nr_filled_subleaves(_table, _leaf, n);		\
												\
	/* CPUID parser might not have filled the entire subleaf range */			\
	((____i >= ____f) || __cpuid_table_invalid_subleaf(_table, _leaf, _subleaf)) ?		\
		NULL : __cpuid_leaves_subleaf_n(&((_table)->leaves), _leaf, ____i);		\
})

/*
 * Compile-time checks for leaves with a subleaf range:
 */

#define __cpuid_assert_subleaf_range(_cpuinfo, _leaf)						\
	static_assert(__cpuid_table_subleaf_range_size(&(_cpuinfo)->cpuid, _leaf) > 1)

#define __cpuid_assert_subleaf_within_range(_cpuinfo, _leaf, _subleaf)				\
	BUILD_BUG_ON(__builtin_constant_p(_subleaf) &&						\
		     __cpuid_table_invalid_subleaf(&(_cpuinfo)->cpuid, _leaf, _subleaf))

/*
 *                     CPUID Parser Call-site APIs
 *
 * Call sites should use below APIs instead of invoking direct CPUID queries.
 *
 * Benefits include:
 *
 * - Return CPUID output as typed C structures that are auto-generated from a
 *   centralized database (see <asm/cpuid/leaf_types.h).  Such data types have a
 *   full C99 bitfield layout per CPUID leaf/subleaf combination.  Call sites
 *   can thus avoid doing ugly and cryptic bitwise operations on raw CPUID data.
 *
 * - Return cached, per-CPU, CPUID output.  Below APIs do not invoke any CPUID
 *   queries, thus avoiding their side effects like serialization and VM exits.
 *   Call-site-specific hard coded constants and macros for caching CPUID query
 *   outputs can also be avoided.
 *
 * - Return sanitized CPUID data.  Below APIs return NULL if the given CPUID
 *   leaf/subleaf input is not supported by hardware, or if the hardware CPUID
 *   output was deemed invalid by the CPUID parser.  This centralizes all CPUID
 *   data sanitization in one place (the kernel's CPUID parser.)
 *
 * - A centralized global view of system CPUID data.  Below APIs will reflect
 *   any kernel-enforced feature masking or overrides, unlike ad hoc parsing of
 *   raw CPUID output by drivers and individual call sites.
 */

/*
 * Call-site APIs for CPUID leaves with a single subleaf:
 */

/**
 * cpuid_subleaf() - Access parsed CPUID
 * @_cpuinfo:	CPU capability structure reference ('struct cpuinfo_x86')
 * @_leaf:	CPUID leaf, in compile-time 0xN format; e.g. 0x7, 0xf
 * @_subleaf:	CPUID subleaf, in compile-time decimal format; e.g. 0, 1, 3
 *
 * Returns a pointer to parsed CPUID output, from the CPUID table inside
 * @_cpuinfo, as a <cpuid/leaf_types.h> data type: 'struct leaf_0xM_N', where
 * 0xM is the token provided at @_leaf, and N is the token provided at
 * @_subleaf; e.g. struct leaf_0x7_0.
 *
 * Returns NULL if the requested CPUID @_leaf/@_subleaf query output is not
 * present at the parsed CPUID table inside @_cpuinfo.  This can happen if:
 *
 * - The CPUID table inside @_cpuinfo has not yet been populated.
 * - The CPUID table inside @_cpuinfo was populated, but the CPU does not
 *   implement the requested CPUID @_leaf/@_subleaf combination.
 * - The CPUID table inside @_cpuinfo was populated, but the kernel's CPUID
 *   parser has predetermined that the requested CPUID @_leaf/@_subleaf
 *   hardware output is invalid or unsupported.
 *
 * Example usage::
 *
 *	const struct leaf_0x7_0 *l7_0 = cpuid_subleaf(c, 0x7, 0);
 *	if (!l7_0) {
 *		// Handle error
 *	}
 *
 *	const struct leaf_0x7_1 *l7_1 = cpuid_subleaf(c, 0x7, 1);
 *	if (!l7_1) {
 *		// Handle error
 *	}
 */
#define cpuid_subleaf(_cpuinfo, _leaf, _subleaf)				\
	__cpuid_table_subleaf(&(_cpuinfo)->cpuid, _leaf, _subleaf)		\

/**
 * cpuid_leaf() - Access parsed CPUID data
 * @_cpuinfo:	CPU capability structure reference ('struct cpuinfo_x86')
 * @_leaf:	CPUID leaf, in compile-time 0xN format; e.g. 0x0, 0x2, 0x80000000
 *
 * Similar to cpuid_subleaf(), but with a CPUID subleaf = 0.
 *
 * Example usage::
 *
 *	const struct leaf_0x0_0 *l0 = cpuid_leaf(c, 0x0);
 *	if (!l0) {
 *		// Handle error
 *	}
 *
 *	const struct leaf_0x80000000_0 *el0 = cpuid_leaf(c, 0x80000000);
 *	if (!el0) {
 *		// Handle error
 *	}
 */
#define cpuid_leaf(_cpuinfo, _leaf)						\
	cpuid_subleaf(_cpuinfo, _leaf, 0)

/**
 * cpuid_leaf_raw() - Access parsed CPUID data in raw format
 * @_cpuinfo:	CPU capability structure reference ('struct cpuinfo_x86')
 * @_leaf:	CPUID leaf, in compile-time 0xN format
 *
 * Similar to cpuid_leaf(), but returns a raw 'struct cpuid_regs' pointer to
 * the parsed CPUID data instead of a "typed" <asm/cpuid/leaf_types.h> pointer.
 */
#define cpuid_leaf_raw(_cpuinfo, _leaf)						\
	((const struct cpuid_regs *)(cpuid_leaf(_cpuinfo, _leaf)))

/*
 * Call-site APIs for CPUID leaves with a subleaf range:
 */

/**
 * cpuid_subleaf_n() - Access parsed CPUID data for leaf with a subleaf range
 * @_cpuinfo:	CPU capability structure reference ('struct cpuinfo_x86')
 * @_leaf:	CPUID leaf, in compile-time 0xN format; e.g. 0x4, 0x8000001d
 * @_subleaf:	Subleaf number, which can be passed dynamically.  It must be smaller
 *		than cpuid_subleaf_count(@_cpuinfo, @_leaf).
 *
 * Build-time errors will be emitted in the following cases:
 *
 * - @_leaf has no subleaf range.  Leaves with a subleaf range have an '_n' type
 *   suffix and are listed at <asm/cpuid/types.h> using the CPUID_LEAF_N() macro.
 *
 * - @_subleaf is known at compile-time but is out of range.
 *
 * Example usage::
 *
 *	const struct leaf_0x4_n *l4;
 *
 *	for (int i = 0; i < cpuid_subleaf_count(c, 0x4); i++) {
 *		l4 = cpuid_subleaf_n(c, 0x4, i);
 *		if (!l4) {
 *			// Handle error
 *		}
 *		...
 *	}
 *
 * Beside the standard error situations detailed at cpuid_subleaf(), this
 * macro will also return NULL if @_subleaf is out of the leaf's subleaf range.
 */
#define cpuid_subleaf_n(_cpuinfo, _leaf, _subleaf)				\
({										\
	__cpuid_assert_subleaf_range(_cpuinfo, _leaf);				\
	__cpuid_assert_subleaf_within_range(_cpuinfo, _leaf, _subleaf);		\
	__cpuid_table_subleaf_n(&(_cpuinfo)->cpuid, _leaf, _subleaf);		\
})

/**
 * cpuid_subleaf_n_raw() - Access parsed CPUID data for leaf with subleaf range
 * @_cpuinfo:	CPU capability structure reference ('struct cpuinfo_x86')
 * @_leaf:	CPUID leaf, in compile-time 0xN format; e.g. 0x4, 0x8000001d
 * @_subleaf:	Subleaf number, which can be passed dynamically.  It must be smaller
 *		than cpuid_subleaf_count(@_cpuinfo, @_leaf).
 *
 * Similar to cpuid_subleaf_n(), but returns a raw 'struct cpuid_regs' pointer to
 * the parsed CPUID data instead of a "typed" <asm/cpuid/leaf_types.h> pointer.
 */
#define cpuid_subleaf_n_raw(_cpuinfo, _leaf, _subleaf)				\
	((const struct cpuid_regs *)cpuid_subleaf_n(_cpuinfo, _leaf, _subleaf))

/**
 * cpuid_subleaf_count() - Number of filled subleaves for @_leaf
 * @_cpuinfo:	CPU capability structure reference ('struct cpuinfo_x86')
 * @_leaf:	CPUID leaf, in compile-time 0xN format; e.g. 0x4, 0x8000001d
 *
 * Return the number of subleaves filled by the CPUID parser for @_leaf.
 *
 * @_leaf must have subleaf range.  Leaves with a subleaf range have an '_n' type
 * suffix and are listed at <asm/cpuid/types.h> using the CPUID_LEAF_N() macro.
 */
#define cpuid_subleaf_count(_cpuinfo, _leaf)					\
({										\
	__cpuid_assert_subleaf_range(_cpuinfo, _leaf);				\
	__cpuid_table_nr_filled_subleaves(&(_cpuinfo)->cpuid, _leaf, n);	\
})

/*
 * CPUID parser exported APIs:
 */

void cpuid_scan_cpu(struct cpuinfo_x86 *c);
void cpuid_refresh_leaf(struct cpuinfo_x86 *c, u32 leaf);
void cpuid_refresh_range(struct cpuinfo_x86 *c, u32 start, u32 end);

#endif /* _ASM_X86_CPUID_API_H */
