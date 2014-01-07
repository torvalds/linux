#ifndef _ASM_X86_TIMER_H
#define _ASM_X86_TIMER_H
#include <linux/pm.h>
#include <linux/percpu.h>
#include <linux/interrupt.h>

#define TICK_SIZE (tick_nsec / 1000)

unsigned long long native_sched_clock(void);
extern int recalibrate_cpu_khz(void);

extern int no_timer_check;

/* Accelerators for sched_clock()
 * convert from cycles(64bits) => nanoseconds (64bits)
 *  basic equation:
 *		ns = cycles / (freq / ns_per_sec)
 *		ns = cycles * (ns_per_sec / freq)
 *		ns = cycles * (10^9 / (cpu_khz * 10^3))
 *		ns = cycles * (10^6 / cpu_khz)
 *
 *	Then we use scaling math (suggested by george@mvista.com) to get:
 *		ns = cycles * (10^6 * SC / cpu_khz) / SC
 *		ns = cycles * cyc2ns_scale / SC
 *
 *	And since SC is a constant power of two, we can convert the div
 *  into a shift.
 *
 *  We can use khz divisor instead of mhz to keep a better precision, since
 *  cyc2ns_scale is limited to 10^6 * 2^10, which fits in 32 bits.
 *  (mathieu.desnoyers@polymtl.ca)
 *
 *			-johnstul@us.ibm.com "math is hard, lets go shopping!"
 *
 * In:
 *
 * ns = cycles * cyc2ns_scale / SC
 *
 * Although we may still have enough bits to store the value of ns,
 * in some cases, we may not have enough bits to store cycles * cyc2ns_scale,
 * leading to an incorrect result.
 *
 * To avoid this, we can decompose 'cycles' into quotient and remainder
 * of division by SC.  Then,
 *
 * ns = (quot * SC + rem) * cyc2ns_scale / SC
 *    = quot * cyc2ns_scale + (rem * cyc2ns_scale) / SC
 *
 *			- sqazi@google.com
 */

DECLARE_PER_CPU(unsigned long, cyc2ns);
DECLARE_PER_CPU(unsigned long long, cyc2ns_offset);

#define CYC2NS_SCALE_FACTOR 10 /* 2^10, carefully chosen */

static inline unsigned long long __cycles_2_ns(unsigned long long cyc)
{
	int cpu = smp_processor_id();
	unsigned long long ns = per_cpu(cyc2ns_offset, cpu);
	ns += mult_frac(cyc, per_cpu(cyc2ns, cpu),
			(1UL << CYC2NS_SCALE_FACTOR));
	return ns;
}

static inline unsigned long long cycles_2_ns(unsigned long long cyc)
{
	unsigned long long ns;
	unsigned long flags;

	local_irq_save(flags);
	ns = __cycles_2_ns(cyc);
	local_irq_restore(flags);

	return ns;
}

#endif /* _ASM_X86_TIMER_H */
