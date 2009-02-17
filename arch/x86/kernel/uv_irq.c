/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV IRQ functions
 *
 * Copyright (C) 2008 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/irq.h>

#include <asm/apic.h>
#include <asm/uv/uv_irq.h>

static void uv_noop(unsigned int irq)
{
}

static unsigned int uv_noop_ret(unsigned int irq)
{
	return 0;
}

static void uv_ack_apic(unsigned int irq)
{
	ack_APIC_irq();
}

struct irq_chip uv_irq_chip = {
	.name		= "UV-CORE",
	.startup	= uv_noop_ret,
	.shutdown	= uv_noop,
	.enable		= uv_noop,
	.disable	= uv_noop,
	.ack		= uv_noop,
	.mask		= uv_noop,
	.unmask		= uv_noop,
	.eoi		= uv_ack_apic,
	.end		= uv_noop,
};

/*
 * Set up a mapping of an available irq and vector, and enable the specified
 * MMR that defines the MSI that is to be sent to the specified CPU when an
 * interrupt is raised.
 */
int uv_setup_irq(char *irq_name, int cpu, int mmr_blade,
		 unsigned long mmr_offset)
{
	int irq;
	int ret;

	irq = create_irq();
	if (irq <= 0)
		return -EBUSY;

	ret = arch_enable_uv_irq(irq_name, irq, cpu, mmr_blade, mmr_offset);
	if (ret != irq)
		destroy_irq(irq);

	return ret;
}
EXPORT_SYMBOL_GPL(uv_setup_irq);

/*
 * Tear down a mapping of an irq and vector, and disable the specified MMR that
 * defined the MSI that was to be sent to the specified CPU when an interrupt
 * was raised.
 *
 * Set mmr_blade and mmr_offset to what was passed in on uv_setup_irq().
 */
void uv_teardown_irq(unsigned int irq, int mmr_blade, unsigned long mmr_offset)
{
	arch_disable_uv_irq(mmr_blade, mmr_offset);
	destroy_irq(irq);
}
EXPORT_SYMBOL_GPL(uv_teardown_irq);
