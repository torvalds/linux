// SPDX-License-Identifier: GPL-2.0
/*
 * Common corrected MCE threshold handler code:
 */
#include <linux/interrupt.h>
#include <linux/kernel.h>

#include <asm/irq_vectors.h>
#include <asm/traps.h>
#include <asm/apic.h>
#include <asm/mce.h>
#include <asm/trace/irq_vectors.h>

#include "internal.h"

static void default_threshold_interrupt(void)
{
	pr_err("Unexpected threshold interrupt at vector %x\n",
		THRESHOLD_APIC_VECTOR);
}

void (*mce_threshold_vector)(void) = default_threshold_interrupt;

DEFINE_IDTENTRY_SYSVEC(sysvec_threshold)
{
	trace_threshold_apic_entry(THRESHOLD_APIC_VECTOR);
	inc_irq_stat(irq_threshold_count);
	mce_threshold_vector();
	trace_threshold_apic_exit(THRESHOLD_APIC_VECTOR);
	apic_eoi();
}

DEFINE_PER_CPU(struct mca_storm_desc, storm_desc);

void mce_inherit_storm(unsigned int bank)
{
	struct mca_storm_desc *storm = this_cpu_ptr(&storm_desc);

	/*
	 * Previous CPU owning this bank had put it into storm mode,
	 * but the precise history of that storm is unknown. Assume
	 * the worst (all recent polls of the bank found a valid error
	 * logged). This will avoid the new owner prematurely declaring
	 * the storm has ended.
	 */
	storm->banks[bank].history = ~0ull;
	storm->banks[bank].timestamp = jiffies;
}

bool mce_get_storm_mode(void)
{
	return __this_cpu_read(storm_desc.poll_mode);
}

void mce_set_storm_mode(bool storm)
{
	__this_cpu_write(storm_desc.poll_mode, storm);
}

static void mce_handle_storm(unsigned int bank, bool on)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		mce_intel_handle_storm(bank, on);
		break;
	}
}

void cmci_storm_begin(unsigned int bank)
{
	struct mca_storm_desc *storm = this_cpu_ptr(&storm_desc);

	__set_bit(bank, this_cpu_ptr(mce_poll_banks));
	storm->banks[bank].in_storm_mode = true;

	/*
	 * If this is the first bank on this CPU to enter storm mode
	 * start polling.
	 */
	if (++storm->stormy_bank_count == 1)
		mce_timer_kick(true);
}

void cmci_storm_end(unsigned int bank)
{
	struct mca_storm_desc *storm = this_cpu_ptr(&storm_desc);

	__clear_bit(bank, this_cpu_ptr(mce_poll_banks));
	storm->banks[bank].history = 0;
	storm->banks[bank].in_storm_mode = false;

	/* If no banks left in storm mode, stop polling. */
	if (!--storm->stormy_bank_count)
		mce_timer_kick(false);
}

void mce_track_storm(struct mce *mce)
{
	struct mca_storm_desc *storm = this_cpu_ptr(&storm_desc);
	unsigned long now = jiffies, delta;
	unsigned int shift = 1;
	u64 history = 0;

	/* No tracking needed for banks that do not support CMCI */
	if (storm->banks[mce->bank].poll_only)
		return;

	/*
	 * When a bank is in storm mode it is polled once per second and
	 * the history mask will record about the last minute of poll results.
	 * If it is not in storm mode, then the bank is only checked when
	 * there is a CMCI interrupt. Check how long it has been since
	 * this bank was last checked, and adjust the amount of "shift"
	 * to apply to history.
	 */
	if (!storm->banks[mce->bank].in_storm_mode) {
		delta = now - storm->banks[mce->bank].timestamp;
		shift = (delta + HZ) / HZ;
	}

	/* If it has been a long time since the last poll, clear history. */
	if (shift < NUM_HISTORY_BITS)
		history = storm->banks[mce->bank].history << shift;

	storm->banks[mce->bank].timestamp = now;

	/* History keeps track of corrected errors. VAL=1 && UC=0 */
	if ((mce->status & MCI_STATUS_VAL) && mce_is_correctable(mce))
		history |= 1;

	storm->banks[mce->bank].history = history;

	if (storm->banks[mce->bank].in_storm_mode) {
		if (history & GENMASK_ULL(STORM_END_POLL_THRESHOLD, 0))
			return;
		printk_deferred(KERN_NOTICE "CPU%d BANK%d CMCI storm subsided\n", smp_processor_id(), mce->bank);
		mce_handle_storm(mce->bank, false);
		cmci_storm_end(mce->bank);
	} else {
		if (hweight64(history) < STORM_BEGIN_THRESHOLD)
			return;
		printk_deferred(KERN_NOTICE "CPU%d BANK%d CMCI storm detected\n", smp_processor_id(), mce->bank);
		mce_handle_storm(mce->bank, true);
		cmci_storm_begin(mce->bank);
	}
}
