/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Kevin D. Kissell
 */

/*
 * Definitions used for common event timer implementation
 * for MIPS 4K-type processors and their MIPS MT variants.
 * Avoids unsightly extern declarations in C files.
 */
#ifndef __ASM_CEVT_R4K_H
#define __ASM_CEVT_R4K_H

DECLARE_PER_CPU(struct clock_event_device, mips_clockevent_device);

void mips_event_handler(struct clock_event_device *dev);
int c0_compare_int_usable(void);
void mips_set_clock_mode(enum clock_event_mode, struct clock_event_device *);
irqreturn_t c0_compare_interrupt(int, void *);

extern struct irqaction c0_compare_irqaction;
extern int cp0_timer_irq_installed;

/*
 * Possibly handle a performance counter interrupt.
 * Return true if the timer interrupt should not be checked
 */

static inline int handle_perf_irq(int r2)
{
	/*
	 * The performance counter overflow interrupt may be shared with the
	 * timer interrupt (cp0_perfcount_irq < 0). If it is and a
	 * performance counter has overflowed (perf_irq() == IRQ_HANDLED)
	 * and we can't reliably determine if a counter interrupt has also
	 * happened (!r2) then don't check for a timer interrupt.
	 */
	return (cp0_perfcount_irq < 0) &&
		perf_irq() == IRQ_HANDLED &&
		!r2;
}

#endif /* __ASM_CEVT_R4K_H */
