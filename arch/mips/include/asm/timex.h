/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999, 2003 by Ralf Baechle
 * Copyright (C) 2014 by Maciej W. Rozycki
 */
#ifndef _ASM_TIMEX_H
#define _ASM_TIMEX_H

#ifdef __KERNEL__

#include <linux/compiler.h>

#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/mipsregs.h>
#include <asm/cpu-type.h>

/*
 * This is the clock rate of the i8253 PIT.  A MIPS system may not have
 * a PIT by the symbol is used all over the kernel including some APIs.
 * So keeping it defined to the number for the PIT is the only sane thing
 * for now.
 */
#define CLOCK_TICK_RATE 1193182

/*
 * Standard way to access the cycle counter.
 * Currently only used on SMP for scheduling.
 *
 * Only the low 32 bits are available as a continuously counting entity.
 * But this only means we'll force a reschedule every 8 seconds or so,
 * which isn't an evil thing.
 *
 * We know that all SMP capable CPUs have cycle counters.
 */

typedef unsigned int cycles_t;

/*
 * On R4000/R4400 an erratum exists such that if the cycle counter is
 * read in the exact moment that it is matching the compare register,
 * no interrupt will be generated.
 *
 * There is a suggested workaround and also the erratum can't strike if
 * the compare interrupt isn't being used as the clock source device.
 * However for now the implementaton of this function doesn't get these
 * fine details right.
 */
static inline int can_use_mips_counter(unsigned int prid)
{
	int comp = (prid & PRID_COMP_MASK) != PRID_COMP_LEGACY;

	if (__builtin_constant_p(cpu_has_counter) && !cpu_has_counter)
		return 0;
	else if (__builtin_constant_p(cpu_has_mips_r) && cpu_has_mips_r)
		return 1;
	else if (likely(!__builtin_constant_p(cpu_has_mips_r) && comp))
		return 1;
	/* Make sure we don't peek at cpu_data[0].options in the fast path! */
	if (!__builtin_constant_p(cpu_has_counter))
		asm volatile("" : "=m" (cpu_data[0].options));
	if (likely(cpu_has_counter &&
		   prid > (PRID_IMP_R4000 | PRID_REV_ENCODE_44(15, 15))))
		return 1;
	else
		return 0;
}

static inline cycles_t get_cycles(void)
{
	if (can_use_mips_counter(read_c0_prid()))
		return read_c0_count();
	else
		return 0;	/* no usable counter */
}
#define get_cycles get_cycles

/*
 * Like get_cycles - but where c0_count is not available we desperately
 * use c0_random in an attempt to get at least a little bit of entropy.
 */
static inline unsigned long random_get_entropy(void)
{
	unsigned int c0_random;

	if (can_use_mips_counter(read_c0_prid()))
		return read_c0_count();

	if (cpu_has_3kex)
		c0_random = (read_c0_random() >> 8) & 0x3f;
	else
		c0_random = read_c0_random() & 0x3f;
	return (random_get_entropy_fallback() << 6) | (0x3f - c0_random);
}
#define random_get_entropy random_get_entropy

#endif /* __KERNEL__ */

#endif /*  _ASM_TIMEX_H */
