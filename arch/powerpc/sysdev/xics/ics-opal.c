/*
 * ICS backend for OPAL managed interrupts.
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/msi.h>

#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/errno.h>
#include <asm/xics.h>
#include <asm/opal.h>
#include <asm/firmware.h>

static int ics_opal_mangle_server(int server)
{
	/* No link for now */
	return server << 2;
}

static int ics_opal_unmangle_server(int server)
{
	/* No link for now */
	return server >> 2;
}

static void ics_opal_unmask_irq(struct irq_data *d)
{
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);
	int64_t rc;
	int server;

	pr_devel("ics-hal: unmask virq %d [hw 0x%x]\n", d->irq, hw_irq);

	if (hw_irq == XICS_IPI || hw_irq == XICS_IRQ_SPURIOUS)
		return;

	server = xics_get_irq_server(d->irq, irq_data_get_affinity_mask(d), 0);
	server = ics_opal_mangle_server(server);

	rc = opal_set_xive(hw_irq, server, DEFAULT_PRIORITY);
	if (rc != OPAL_SUCCESS)
		pr_err("%s: opal_set_xive(irq=%d [hw 0x%x] server=%x)"
		       " error %lld\n",
		       __func__, d->irq, hw_irq, server, rc);
}

static unsigned int ics_opal_startup(struct irq_data *d)
{
#ifdef CONFIG_PCI_MSI
	/*
	 * The generic MSI code returns with the interrupt disabled on the
	 * card, using the MSI mask bits. Firmware doesn't appear to unmask
	 * at that level, so we do it here by hand.
	 */
	if (irq_data_get_msi_desc(d))
		pci_msi_unmask_irq(d);
#endif

	/* unmask it */
	ics_opal_unmask_irq(d);
	return 0;
}

static void ics_opal_mask_real_irq(unsigned int hw_irq)
{
	int server = ics_opal_mangle_server(xics_default_server);
	int64_t rc;

	if (hw_irq == XICS_IPI)
		return;

	/* Have to set XIVE to 0xff to be able to remove a slot */
	rc = opal_set_xive(hw_irq, server, 0xff);
	if (rc != OPAL_SUCCESS)
		pr_err("%s: opal_set_xive(0xff) irq=%u returned %lld\n",
		       __func__, hw_irq, rc);
}

static void ics_opal_mask_irq(struct irq_data *d)
{
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);

	pr_devel("ics-hal: mask virq %d [hw 0x%x]\n", d->irq, hw_irq);

	if (hw_irq == XICS_IPI || hw_irq == XICS_IRQ_SPURIOUS)
		return;
	ics_opal_mask_real_irq(hw_irq);
}

static int ics_opal_set_affinity(struct irq_data *d,
				 const struct cpumask *cpumask,
				 bool force)
{
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);
	__be16 oserver;
	int16_t server;
	int8_t priority;
	int64_t rc;
	int wanted_server;

	if (hw_irq == XICS_IPI || hw_irq == XICS_IRQ_SPURIOUS)
		return -1;

	rc = opal_get_xive(hw_irq, &oserver, &priority);
	if (rc != OPAL_SUCCESS) {
		pr_err("%s: opal_get_xive(irq=%d [hw 0x%x]) error %lld\n",
		       __func__, d->irq, hw_irq, rc);
		return -1;
	}
	server = be16_to_cpu(oserver);

	wanted_server = xics_get_irq_server(d->irq, cpumask, 1);
	if (wanted_server < 0) {
		pr_warning("%s: No online cpus in the mask %*pb for irq %d\n",
			   __func__, cpumask_pr_args(cpumask), d->irq);
		return -1;
	}
	server = ics_opal_mangle_server(wanted_server);

	pr_devel("ics-hal: set-affinity irq %d [hw 0x%x] server: 0x%x/0x%x\n",
		 d->irq, hw_irq, wanted_server, server);

	rc = opal_set_xive(hw_irq, server, priority);
	if (rc != OPAL_SUCCESS) {
		pr_err("%s: opal_set_xive(irq=%d [hw 0x%x] server=%x)"
		       " error %lld\n",
		       __func__, d->irq, hw_irq, server, rc);
		return -1;
	}
	return IRQ_SET_MASK_OK;
}

static struct irq_chip ics_opal_irq_chip = {
	.name = "OPAL ICS",
	.irq_startup = ics_opal_startup,
	.irq_mask = ics_opal_mask_irq,
	.irq_unmask = ics_opal_unmask_irq,
	.irq_eoi = NULL, /* Patched at init time */
	.irq_set_affinity = ics_opal_set_affinity,
	.irq_set_type = xics_set_irq_type,
	.irq_retrigger = xics_retrigger,
};

static int ics_opal_map(struct ics *ics, unsigned int virq);
static void ics_opal_mask_unknown(struct ics *ics, unsigned long vec);
static long ics_opal_get_server(struct ics *ics, unsigned long vec);

static int ics_opal_host_match(struct ics *ics, struct device_node *node)
{
	return 1;
}

/* Only one global & state struct ics */
static struct ics ics_hal = {
	.map		= ics_opal_map,
	.mask_unknown	= ics_opal_mask_unknown,
	.get_server	= ics_opal_get_server,
	.host_match	= ics_opal_host_match,
};

static int ics_opal_map(struct ics *ics, unsigned int virq)
{
	unsigned int hw_irq = (unsigned int)virq_to_hw(virq);
	int64_t rc;
	__be16 server;
	int8_t priority;

	if (WARN_ON(hw_irq == XICS_IPI || hw_irq == XICS_IRQ_SPURIOUS))
		return -EINVAL;

	/* Check if HAL knows about this interrupt */
	rc = opal_get_xive(hw_irq, &server, &priority);
	if (rc != OPAL_SUCCESS)
		return -ENXIO;

	irq_set_chip_and_handler(virq, &ics_opal_irq_chip, handle_fasteoi_irq);
	irq_set_chip_data(virq, &ics_hal);

	return 0;
}

static void ics_opal_mask_unknown(struct ics *ics, unsigned long vec)
{
	int64_t rc;
	__be16 server;
	int8_t priority;

	/* Check if HAL knows about this interrupt */
	rc = opal_get_xive(vec, &server, &priority);
	if (rc != OPAL_SUCCESS)
		return;

	ics_opal_mask_real_irq(vec);
}

static long ics_opal_get_server(struct ics *ics, unsigned long vec)
{
	int64_t rc;
	__be16 server;
	int8_t priority;

	/* Check if HAL knows about this interrupt */
	rc = opal_get_xive(vec, &server, &priority);
	if (rc != OPAL_SUCCESS)
		return -1;
	return ics_opal_unmangle_server(be16_to_cpu(server));
}

int __init ics_opal_init(void)
{
	if (!firmware_has_feature(FW_FEATURE_OPAL))
		return -ENODEV;

	/* We need to patch our irq chip's EOI to point to the
	 * right ICP
	 */
	ics_opal_irq_chip.irq_eoi = icp_ops->eoi;

	/* Register ourselves */
	xics_register_ics(&ics_hal);

	pr_info("ICS OPAL backend registered\n");

	return 0;
}
