// SPDX-License-Identifier: GPL-2.0-only
/*
 * Low-level idle sequences
 */

#include <linux/cpu.h>
#include <linux/irqflags.h>

#include <asm/arch_gicv3.h>
#include <asm/barrier.h>
#include <asm/cpufeature.h>
#include <asm/sysreg.h>

static void noinstr __cpu_do_idle(void)
{
	dsb(sy);
	wfi();
}

static void noinstr __cpu_do_idle_irqprio(void)
{
	unsigned long pmr;
	unsigned long daif_bits;

	daif_bits = read_sysreg(daif);
	write_sysreg(daif_bits | PSR_I_BIT | PSR_F_BIT, daif);

	/*
	 * Unmask PMR before going idle to make sure interrupts can
	 * be raised.
	 */
	pmr = gic_read_pmr();
	gic_write_pmr(GIC_PRIO_IRQON | GIC_PRIO_PSR_I_SET);

	__cpu_do_idle();

	gic_write_pmr(pmr);
	write_sysreg(daif_bits, daif);
}

/*
 *	cpu_do_idle()
 *
 *	Idle the processor (wait for interrupt).
 *
 *	If the CPU supports priority masking we must do additional work to
 *	ensure that interrupts are not masked at the PMR (because the core will
 *	not wake up if we block the wake up signal in the interrupt controller).
 */
void noinstr cpu_do_idle(void)
{
	if (system_uses_irq_prio_masking())
		__cpu_do_idle_irqprio();
	else
		__cpu_do_idle();
}

/*
 * This is our default idle handler.
 */
void noinstr arch_cpu_idle(void)
{
	/*
	 * This should do all the clock switching and wait for interrupt
	 * tricks
	 */
	cpu_do_idle();
	raw_local_irq_enable();
}
