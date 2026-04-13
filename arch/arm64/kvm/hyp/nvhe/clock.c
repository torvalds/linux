// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Google LLC
 * Author: Vincent Donnefort <vdonnefort@google.com>
 */

#include <nvhe/clock.h>

#include <asm/arch_timer.h>
#include <asm/div64.h>

static struct clock_data {
	struct {
		u32 mult;
		u32 shift;
		u64 epoch_ns;
		u64 epoch_cyc;
		u64 cyc_overflow64;
	} data[2];
	u64 cur;
} trace_clock_data;

static u64 __clock_mult_uint128(u64 cyc, u32 mult, u32 shift)
{
	__uint128_t ns = (__uint128_t)cyc * mult;

	ns >>= shift;

	return (u64)ns;
}

/* Does not guarantee no reader on the modified bank. */
void trace_clock_update(u32 mult, u32 shift, u64 epoch_ns, u64 epoch_cyc)
{
	struct clock_data *clock = &trace_clock_data;
	u64 bank = clock->cur ^ 1;

	clock->data[bank].mult			= mult;
	clock->data[bank].shift			= shift;
	clock->data[bank].epoch_ns		= epoch_ns;
	clock->data[bank].epoch_cyc		= epoch_cyc;
	clock->data[bank].cyc_overflow64	= ULONG_MAX / mult;

	smp_store_release(&clock->cur, bank);
}

/* Use untrusted host data */
u64 trace_clock(void)
{
	struct clock_data *clock = &trace_clock_data;
	u64 bank = smp_load_acquire(&clock->cur);
	u64 cyc, ns;

	cyc = __arch_counter_get_cntvct() - clock->data[bank].epoch_cyc;

	if (likely(cyc < clock->data[bank].cyc_overflow64)) {
		ns = cyc * clock->data[bank].mult;
		ns >>= clock->data[bank].shift;
	} else {
		ns = __clock_mult_uint128(cyc, clock->data[bank].mult,
					  clock->data[bank].shift);
	}

	return (u64)ns + clock->data[bank].epoch_ns;
}
