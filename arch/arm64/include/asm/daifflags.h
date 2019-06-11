/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 ARM Ltd.
 */
#ifndef __ASM_DAIFFLAGS_H
#define __ASM_DAIFFLAGS_H

#include <linux/irqflags.h>

#include <asm/arch_gicv3.h>
#include <asm/cpufeature.h>

#define DAIF_PROCCTX		0
#define DAIF_PROCCTX_NOIRQ	PSR_I_BIT
#define DAIF_ERRCTX		(PSR_I_BIT | PSR_A_BIT)

/* mask/save/unmask/restore all exceptions, including interrupts. */
static inline void local_daif_mask(void)
{
	asm volatile(
		"msr	daifset, #0xf		// local_daif_mask\n"
		:
		:
		: "memory");

	/* Don't really care for a dsb here, we don't intend to enable IRQs */
	if (system_uses_irq_prio_masking())
		gic_write_pmr(GIC_PRIO_IRQON | GIC_PRIO_PSR_I_SET);

	trace_hardirqs_off();
}

static inline unsigned long local_daif_save(void)
{
	unsigned long flags;

	flags = read_sysreg(daif);

	if (system_uses_irq_prio_masking()) {
		/* If IRQs are masked with PMR, reflect it in the flags */
		if (read_sysreg_s(SYS_ICC_PMR_EL1) != GIC_PRIO_IRQON)
			flags |= PSR_I_BIT;
	}

	local_daif_mask();

	return flags;
}

static inline void local_daif_restore(unsigned long flags)
{
	bool irq_disabled = flags & PSR_I_BIT;

	if (!irq_disabled) {
		trace_hardirqs_on();

		if (system_uses_irq_prio_masking()) {
			gic_write_pmr(GIC_PRIO_IRQON);
			dsb(sy);
		}
	} else if (system_uses_irq_prio_masking()) {
		u64 pmr;

		if (!(flags & PSR_A_BIT)) {
			/*
			 * If interrupts are disabled but we can take
			 * asynchronous errors, we can take NMIs
			 */
			flags &= ~PSR_I_BIT;
			pmr = GIC_PRIO_IRQOFF;
		} else {
			pmr = GIC_PRIO_IRQON | GIC_PRIO_PSR_I_SET;
		}

		/*
		 * There has been concern that the write to daif
		 * might be reordered before this write to PMR.
		 * From the ARM ARM DDI 0487D.a, section D1.7.1
		 * "Accessing PSTATE fields":
		 *   Writes to the PSTATE fields have side-effects on
		 *   various aspects of the PE operation. All of these
		 *   side-effects are guaranteed:
		 *     - Not to be visible to earlier instructions in
		 *       the execution stream.
		 *     - To be visible to later instructions in the
		 *       execution stream
		 *
		 * Also, writes to PMR are self-synchronizing, so no
		 * interrupts with a lower priority than PMR is signaled
		 * to the PE after the write.
		 *
		 * So we don't need additional synchronization here.
		 */
		gic_write_pmr(pmr);
	}

	write_sysreg(flags, daif);

	if (irq_disabled)
		trace_hardirqs_off();
}

#endif
