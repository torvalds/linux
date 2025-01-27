/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CPUID-related helpers/definitions
 */

#ifndef _ASM_X86_CPUID_H
#define _ASM_X86_CPUID_H

#include <linux/types.h>

#include <asm/string.h>

struct cpuid_regs {
	u32 eax, ebx, ecx, edx;
};

enum cpuid_regs_idx {
	CPUID_EAX = 0,
	CPUID_EBX,
	CPUID_ECX,
	CPUID_EDX,
};

#define CPUID_LEAF_MWAIT	0x5
#define CPUID_LEAF_DCA		0x9
#define CPUID_LEAF_XSTATE	0x0d
#define CPUID_LEAF_TSC		0x15
#define CPUID_LEAF_FREQ		0x16
#define CPUID_LEAF_TILE		0x1d

#ifdef CONFIG_X86_32
bool have_cpuid_p(void);
#else
static inline bool have_cpuid_p(void)
{
	return true;
}
#endif
static inline void native_cpuid(unsigned int *eax, unsigned int *ebx,
				unsigned int *ecx, unsigned int *edx)
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

#define native_cpuid_reg(reg)					\
static inline unsigned int native_cpuid_##reg(unsigned int op)	\
{								\
	unsigned int eax = op, ebx, ecx = 0, edx;		\
								\
	native_cpuid(&eax, &ebx, &ecx, &edx);			\
								\
	return reg;						\
}

/*
 * Native CPUID functions returning a single datum.
 */
native_cpuid_reg(eax)
native_cpuid_reg(ebx)
native_cpuid_reg(ecx)
native_cpuid_reg(edx)

#ifdef CONFIG_PARAVIRT_XXL
#include <asm/paravirt.h>
#else
#define __cpuid			native_cpuid
#endif

/*
 * Generic CPUID function
 * clear %ecx since some cpus (Cyrix MII) do not set or clear %ecx
 * resulting in stale register contents being returned.
 */
static inline void cpuid(unsigned int op,
			 unsigned int *eax, unsigned int *ebx,
			 unsigned int *ecx, unsigned int *edx)
{
	*eax = op;
	*ecx = 0;
	__cpuid(eax, ebx, ecx, edx);
}

/* Some CPUID calls want 'count' to be placed in ecx */
static inline void cpuid_count(unsigned int op, int count,
			       unsigned int *eax, unsigned int *ebx,
			       unsigned int *ecx, unsigned int *edx)
{
	*eax = op;
	*ecx = count;
	__cpuid(eax, ebx, ecx, edx);
}

/*
 * CPUID functions returning a single datum
 */
static inline unsigned int cpuid_eax(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return eax;
}

static inline unsigned int cpuid_ebx(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return ebx;
}

static inline unsigned int cpuid_ecx(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return ecx;
}

static inline unsigned int cpuid_edx(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);

	return edx;
}

static inline void __cpuid_read(unsigned int leaf, unsigned int subleaf, u32 *regs)
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

static inline void __cpuid_read_reg(unsigned int leaf, unsigned int subleaf,
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

#define for_each_possible_hypervisor_cpuid_base(function) \
	for (function = 0x40000000; function < 0x40010000; function += 0x100)

static inline uint32_t hypervisor_cpuid_base(const char *sig, uint32_t leaves)
{
	uint32_t base, eax, signature[3];

	for_each_possible_hypervisor_cpuid_base(base) {
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

#endif /* _ASM_X86_CPUID_H */
