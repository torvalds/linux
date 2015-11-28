/*
 * calibrate.c: Sony PS4 TSC/LAPIC calibration
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#define pr_fmt(fmt) "ps4: " fmt

#include <linux/jiffies.h>
#include <asm/io.h>
#include <asm/msr.h>
#include <asm/ps4.h>
#include <asm/delay.h>
#include <asm/apic.h>

/* The PS4 southbridge (Aeolia) has an EMC timer that ticks at 32.768kHz,
 * which seems to be an appropriate clock reference for calibration. Both TSC
 * and the LAPIC timer are based on the core clock frequency and thus can be
 * calibrated together. */
static void __iomem *emc_timer = NULL;

static __init inline u32 emctimer_read32(unsigned int reg)
{
	return ioread32(emc_timer + reg);
}

static __init inline void emctimer_write32(unsigned int reg, u32 val)
{
	iowrite32(val, emc_timer + reg);
}

static __init inline u32 emctimer_read(void)
{
	u32 t1, t2;
	t1 = emctimer_read32(EMC_TIMER_VALUE);
	while (1) {
		t2 = emctimer_read32(EMC_TIMER_VALUE);
		if (t1 == t2)
			return t1;
		t1 = t2;
	}
}

static __init unsigned long ps4_measure_tsc_freq(void)
{
	unsigned long ret = 0;
	u32 t1, t2;
	u64 tsc1, tsc2;

	// This is part of the Aeolia pcie device, but it's too early to
	// do this in a driver.
	emc_timer = ioremap(EMC_TIMER_BASE, 0x100);
	if (!emc_timer)
		goto fail;

	// reset/start the timer
	emctimer_write32(0x84, emctimer_read32(0x84) & (~0x01));
	// udelay is not calibrated yet, so this is likely wildly off, but good
	// enough to work.
	udelay(300);
	emctimer_write32(0x00, emctimer_read32(0x00) | 0x01);
	emctimer_write32(0x84, emctimer_read32(0x84) | 0x01);

	t1 = emctimer_read();
	tsc1 = tsc2 = rdtsc();

	while (emctimer_read() == t1) {
		// 0.1s timeout should be enough
		tsc2 = rdtsc();
		if ((tsc2 - tsc1) > (PS4_DEFAULT_TSC_FREQ/10)) {
			pr_warn("EMC timer is broken.\n");
			goto fail;
		}
	}
	pr_info("EMC timer started in %lld TSC ticks\n", tsc2 - tsc1);

	// Wait for a tick boundary
	t1 = emctimer_read();
	while ((t2 = emctimer_read()) == t1);
	tsc1 = rdtsc();

	// Wait for 1024 ticks to elapse (31.25ms)
	// We don't need to wait very long, as we are looking for transitions.
	// At this value, a TSC uncertainty of ~50 ticks corresponds to 1ppm of
	// clock accuracy.
	while ((emctimer_read() - t2) < 1024);
	tsc2 = rdtsc();

	// TSC rate is 32 times the elapsed time
	ret = (tsc2 - tsc1) * 32;

	pr_info("Calibrated TSC frequency: %ld kHz\n", ret);
fail:
	if (emc_timer) {
		iounmap(emc_timer);
		emc_timer = NULL;
	}
	return ret;
}

unsigned long __init ps4_calibrate_tsc(void)
{
	unsigned long tsc_freq = ps4_measure_tsc_freq();

	if (!tsc_freq) {
		pr_warn("Unable to measure TSC frequency, assuming default.\n");
		tsc_freq = PS4_DEFAULT_TSC_FREQ;
	}

	lapic_timer_frequency = (tsc_freq + 8 * HZ) / (16 * HZ);

	return (tsc_freq + 500) / 1000;
}
