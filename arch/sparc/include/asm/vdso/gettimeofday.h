/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2006 Andi Kleen, SUSE Labs.
 */

#ifndef _ASM_SPARC_VDSO_GETTIMEOFDAY_H
#define _ASM_SPARC_VDSO_GETTIMEOFDAY_H

#include <linux/types.h>
#include <asm/vvar.h>

#ifdef	CONFIG_SPARC64
static __always_inline u64 vdso_shift_ns(u64 val, u32 amt)
{
	return val >> amt;
}

static __always_inline u64 vread_tick(void)
{
	u64	ret;

	__asm__ __volatile__("rd %%tick, %0" : "=r" (ret));
	return ret;
}

static __always_inline u64 vread_tick_stick(void)
{
	u64	ret;

	__asm__ __volatile__("rd %%asr24, %0" : "=r" (ret));
	return ret;
}
#else
static __always_inline u64 vdso_shift_ns(u64 val, u32 amt)
{
	u64 ret;

	__asm__ __volatile__("sllx %H1, 32, %%g1\n\t"
			     "srl %L1, 0, %L1\n\t"
			     "or %%g1, %L1, %%g1\n\t"
			     "srlx %%g1, %2, %L0\n\t"
			     "srlx %L0, 32, %H0"
			     : "=r" (ret)
			     : "r" (val), "r" (amt)
			     : "g1");
	return ret;
}

static __always_inline u64 vread_tick(void)
{
	register unsigned long long ret asm("o4");

	__asm__ __volatile__("rd %%tick, %L0\n\t"
			     "srlx %L0, 32, %H0"
			     : "=r" (ret));
	return ret;
}

static __always_inline u64 vread_tick_stick(void)
{
	register unsigned long long ret asm("o4");

	__asm__ __volatile__("rd %%asr24, %L0\n\t"
			     "srlx %L0, 32, %H0"
			     : "=r" (ret));
	return ret;
}
#endif

static __always_inline u64 __arch_get_hw_counter(struct vvar_data *vvar)
{
	if (likely(vvar->vclock_mode == VCLOCK_STICK))
		return vread_tick_stick();
	else
		return vread_tick();
}

#endif /* _ASM_SPARC_VDSO_GETTIMEOFDAY_H */
