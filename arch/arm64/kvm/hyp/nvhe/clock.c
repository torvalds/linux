// SPDX-License-Identifier: GPL-2.0
#include <nvhe/clock.h>

#include <asm/arch_timer.h>
#include <asm/div64.h>

static struct kvm_nvhe_clock_data trace_clock_data;

/*
 * Update without any locks! This is fine because tracing, the sole user of this
 * clock is ordering the memory and protects from races between read and
 * updates.
 */
void trace_clock_update(struct kvm_nvhe_clock_data *data)
{
	trace_clock_data.mult = data->mult;
	trace_clock_data.shift = data->shift;
	trace_clock_data.epoch_ns = data->epoch_ns;
	trace_clock_data.epoch_cyc = data->epoch_cyc;
}

/*
 * This clock is relying on host provided slope and epoch values to return
 * something synchronized with the host. The downside is we can't trust the
 * output which must not be used for anything else than debugging.
 */
u64 trace_clock(void)
{
	u64 cyc = __arch_counter_get_cntpct() - trace_clock_data.epoch_cyc;
	__uint128_t ns;

	/*
	 * The host kernel can avoid the 64-bits overflow of the multiplication
	 * by updating the epoch value with a timer (see
	 * kernel/time/clocksource.c). The hypervisor doesn't have that option,
	 * so let's do a more costly 128-bits mult here.
	 */
	ns = (__uint128_t)cyc * trace_clock_data.mult;
	ns >>= trace_clock_data.shift;

	return (u64)ns + trace_clock_data.epoch_ns;
}
