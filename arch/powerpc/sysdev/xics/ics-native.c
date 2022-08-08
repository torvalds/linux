// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ICS backend for OPAL managed interrupts.
 *
 * Copyright 2011 IBM Corp.
 */

//#define DEBUG

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
#include <linux/list.h>

#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/errno.h>
#include <asm/xics.h>
#include <asm/opal.h>
#include <asm/firmware.h>

struct ics_native {
	struct ics		ics;
	struct device_node	*node;
	void __iomem    	*base;
	u32             	ibase;
	u32             	icount;
};
#define to_ics_native(_ics)     container_of(_ics, struct ics_native, ics)

static void __iomem *ics_native_xive(struct ics_native *in, unsigned int vec)
{
	return in->base + 0x800 + ((vec - in->ibase) << 2);
}

static void ics_native_unmask_irq(struct irq_data *d)
{
	unsigned int vec = (unsigned int)irqd_to_hwirq(d);
	struct ics *ics = irq_data_get_irq_chip_data(d);
	struct ics_native *in = to_ics_native(ics);
	unsigned int server;

	pr_devel("ics-native: unmask virq %d [hw 0x%x]\n", d->irq, vec);

	if (vec < in->ibase || vec >= (in->ibase + in->icount))
		return;

	server = xics_get_irq_server(d->irq, irq_data_get_affinity_mask(d), 0);
	out_be32(ics_native_xive(in, vec), (server << 8) | DEFAULT_PRIORITY);
}

static unsigned int ics_native_startup(struct irq_data *d)
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
	ics_native_unmask_irq(d);
	return 0;
}

static void ics_native_do_mask(struct ics_native *in, unsigned int vec)
{
	out_be32(ics_native_xive(in, vec), 0xff);
}

static void ics_native_mask_irq(struct irq_data *d)
{
	unsigned int vec = (unsigned int)irqd_to_hwirq(d);
	struct ics *ics = irq_data_get_irq_chip_data(d);
	struct ics_native *in = to_ics_native(ics);

	pr_devel("ics-native: mask virq %d [hw 0x%x]\n", d->irq, vec);

	if (vec < in->ibase || vec >= (in->ibase + in->icount))
		return;
	ics_native_do_mask(in, vec);
}

static int ics_native_set_affinity(struct irq_data *d,
				   const struct cpumask *cpumask,
				   bool force)
{
	unsigned int vec = (unsigned int)irqd_to_hwirq(d);
	struct ics *ics = irq_data_get_irq_chip_data(d);
	struct ics_native *in = to_ics_native(ics);
	int server;
	u32 xive;

	if (vec < in->ibase || vec >= (in->ibase + in->icount))
		return -EINVAL;

	server = xics_get_irq_server(d->irq, cpumask, 1);
	if (server == -1) {
		pr_warn("%s: No online cpus in the mask %*pb for irq %d\n",
			__func__, cpumask_pr_args(cpumask), d->irq);
		return -1;
	}

	xive = in_be32(ics_native_xive(in, vec));
	xive = (xive & 0xff) | (server << 8);
	out_be32(ics_native_xive(in, vec), xive);

	return IRQ_SET_MASK_OK;
}

static struct irq_chip ics_native_irq_chip = {
	.name = "ICS",
	.irq_startup		= ics_native_startup,
	.irq_mask		= ics_native_mask_irq,
	.irq_unmask		= ics_native_unmask_irq,
	.irq_eoi		= NULL, /* Patched at init time */
	.irq_set_affinity 	= ics_native_set_affinity,
	.irq_set_type		= xics_set_irq_type,
	.irq_retrigger		= xics_retrigger,
};

static int ics_native_map(struct ics *ics, unsigned int virq)
{
	unsigned int vec = (unsigned int)virq_to_hw(virq);
	struct ics_native *in = to_ics_native(ics);

	pr_devel("%s: vec=0x%x\n", __func__, vec);

	if (vec < in->ibase || vec >= (in->ibase + in->icount))
		return -EINVAL;

	irq_set_chip_and_handler(virq, &ics_native_irq_chip, handle_fasteoi_irq);
	irq_set_chip_data(virq, ics);

	return 0;
}

static void ics_native_mask_unknown(struct ics *ics, unsigned long vec)
{
	struct ics_native *in = to_ics_native(ics);

	if (vec < in->ibase || vec >= (in->ibase + in->icount))
		return;

	ics_native_do_mask(in, vec);
}

static long ics_native_get_server(struct ics *ics, unsigned long vec)
{
	struct ics_native *in = to_ics_native(ics);
	u32 xive;

	if (vec < in->ibase || vec >= (in->ibase + in->icount))
		return -EINVAL;

	xive = in_be32(ics_native_xive(in, vec));
	return (xive >> 8) & 0xfff;
}

static int ics_native_host_match(struct ics *ics, struct device_node *node)
{
	struct ics_native *in = to_ics_native(ics);

	return in->node == node;
}

static struct ics ics_native_template = {
	.map		= ics_native_map,
	.mask_unknown	= ics_native_mask_unknown,
	.get_server	= ics_native_get_server,
	.host_match	= ics_native_host_match,
};

static int __init ics_native_add_one(struct device_node *np)
{
	struct ics_native *ics;
	u32 ranges[2];
	int rc, count;

	ics = kzalloc(sizeof(struct ics_native), GFP_KERNEL);
	if (!ics)
		return -ENOMEM;
	ics->node = of_node_get(np);
	memcpy(&ics->ics, &ics_native_template, sizeof(struct ics));

	ics->base = of_iomap(np, 0);
	if (!ics->base) {
		pr_err("Failed to map %pOFP\n", np);
		rc = -ENOMEM;
		goto fail;
	}

	count = of_property_count_u32_elems(np, "interrupt-ranges");
	if (count < 2 || count & 1) {
		pr_err("Failed to read interrupt-ranges of %pOFP\n", np);
		rc = -EINVAL;
		goto fail;
	}
	if (count > 2) {
		pr_warn("ICS %pOFP has %d ranges, only one supported\n",
			np, count >> 1);
	}
	rc = of_property_read_u32_array(np, "interrupt-ranges",
					ranges, 2);
	if (rc) {
		pr_err("Failed to read interrupt-ranges of %pOFP\n", np);
		goto fail;
	}
	ics->ibase = ranges[0];
	ics->icount = ranges[1];

	pr_info("ICS native initialized for sources %d..%d\n",
		ics->ibase, ics->ibase + ics->icount - 1);

	/* Register ourselves */
	xics_register_ics(&ics->ics);

	return 0;
fail:
	of_node_put(ics->node);
	kfree(ics);
	return rc;
}

int __init ics_native_init(void)
{
	struct device_node *ics;
	bool found_one = false;

	/* We need to patch our irq chip's EOI to point to the
	 * right ICP
	 */
	ics_native_irq_chip.irq_eoi = icp_ops->eoi;

	/* Find native ICS in the device-tree */
	for_each_compatible_node(ics, NULL, "openpower,xics-sources") {
		if (ics_native_add_one(ics) == 0)
			found_one = true;
	}

	if (found_one)
		pr_info("ICS native backend registered\n");

	return found_one ? 0 : -ENODEV;
}
