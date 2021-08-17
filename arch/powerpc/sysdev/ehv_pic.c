/*
 *  Driver for ePAPR Embedded Hypervisor PIC
 *
 *  Copyright 2008-2011 Freescale Semiconductor, Inc.
 *
 *  Author: Ashish Kalra <ashish.kalra@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/ehv_pic.h>
#include <asm/fsl_hcalls.h>

static struct ehv_pic *global_ehv_pic;
static DEFINE_SPINLOCK(ehv_pic_lock);

static u32 hwirq_intspec[NR_EHV_PIC_INTS];
static u32 __iomem *mpic_percpu_base_vaddr;

#define IRQ_TYPE_MPIC_DIRECT 4
#define MPIC_EOI  0x00B0

/*
 * Linux descriptor level callbacks
 */

void ehv_pic_unmask_irq(struct irq_data *d)
{
	unsigned int src = virq_to_hw(d->irq);

	ev_int_set_mask(src, 0);
}

void ehv_pic_mask_irq(struct irq_data *d)
{
	unsigned int src = virq_to_hw(d->irq);

	ev_int_set_mask(src, 1);
}

void ehv_pic_end_irq(struct irq_data *d)
{
	unsigned int src = virq_to_hw(d->irq);

	ev_int_eoi(src);
}

void ehv_pic_direct_end_irq(struct irq_data *d)
{
	out_be32(mpic_percpu_base_vaddr + MPIC_EOI / 4, 0);
}

int ehv_pic_set_affinity(struct irq_data *d, const struct cpumask *dest,
			 bool force)
{
	unsigned int src = virq_to_hw(d->irq);
	unsigned int config, prio, cpu_dest;
	int cpuid = irq_choose_cpu(dest);
	unsigned long flags;

	spin_lock_irqsave(&ehv_pic_lock, flags);
	ev_int_get_config(src, &config, &prio, &cpu_dest);
	ev_int_set_config(src, config, prio, cpuid);
	spin_unlock_irqrestore(&ehv_pic_lock, flags);

	return IRQ_SET_MASK_OK;
}

static unsigned int ehv_pic_type_to_vecpri(unsigned int type)
{
	/* Now convert sense value */

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		return EHV_PIC_INFO(VECPRI_SENSE_EDGE) |
		       EHV_PIC_INFO(VECPRI_POLARITY_POSITIVE);

	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		return EHV_PIC_INFO(VECPRI_SENSE_EDGE) |
		       EHV_PIC_INFO(VECPRI_POLARITY_NEGATIVE);

	case IRQ_TYPE_LEVEL_HIGH:
		return EHV_PIC_INFO(VECPRI_SENSE_LEVEL) |
		       EHV_PIC_INFO(VECPRI_POLARITY_POSITIVE);

	case IRQ_TYPE_LEVEL_LOW:
	default:
		return EHV_PIC_INFO(VECPRI_SENSE_LEVEL) |
		       EHV_PIC_INFO(VECPRI_POLARITY_NEGATIVE);
	}
}

int ehv_pic_set_irq_type(struct irq_data *d, unsigned int flow_type)
{
	unsigned int src = virq_to_hw(d->irq);
	unsigned int vecpri, vold, vnew, prio, cpu_dest;
	unsigned long flags;

	if (flow_type == IRQ_TYPE_NONE)
		flow_type = IRQ_TYPE_LEVEL_LOW;

	irqd_set_trigger_type(d, flow_type);

	vecpri = ehv_pic_type_to_vecpri(flow_type);

	spin_lock_irqsave(&ehv_pic_lock, flags);
	ev_int_get_config(src, &vold, &prio, &cpu_dest);
	vnew = vold & ~(EHV_PIC_INFO(VECPRI_POLARITY_MASK) |
			EHV_PIC_INFO(VECPRI_SENSE_MASK));
	vnew |= vecpri;

	/*
	 * TODO : Add specific interface call for platform to set
	 * individual interrupt priorities.
	 * platform currently using static/default priority for all ints
	 */

	prio = 8;

	ev_int_set_config(src, vecpri, prio, cpu_dest);

	spin_unlock_irqrestore(&ehv_pic_lock, flags);
	return IRQ_SET_MASK_OK_NOCOPY;
}

static struct irq_chip ehv_pic_irq_chip = {
	.irq_mask	= ehv_pic_mask_irq,
	.irq_unmask	= ehv_pic_unmask_irq,
	.irq_eoi	= ehv_pic_end_irq,
	.irq_set_type	= ehv_pic_set_irq_type,
};

static struct irq_chip ehv_pic_direct_eoi_irq_chip = {
	.irq_mask	= ehv_pic_mask_irq,
	.irq_unmask	= ehv_pic_unmask_irq,
	.irq_eoi	= ehv_pic_direct_end_irq,
	.irq_set_type	= ehv_pic_set_irq_type,
};

/* Return an interrupt vector or 0 if no interrupt is pending. */
unsigned int ehv_pic_get_irq(void)
{
	int irq;

	BUG_ON(global_ehv_pic == NULL);

	if (global_ehv_pic->coreint_flag)
		irq = mfspr(SPRN_EPR); /* if core int mode */
	else
		ev_int_iack(0, &irq); /* legacy mode */

	if (irq == 0xFFFF)    /* 0xFFFF --> no irq is pending */
		return 0;

	/*
	 * this will also setup revmap[] in the slow path for the first
	 * time, next calls will always use fast path by indexing revmap
	 */
	return irq_linear_revmap(global_ehv_pic->irqhost, irq);
}

static int ehv_pic_host_match(struct irq_domain *h, struct device_node *node,
			      enum irq_domain_bus_token bus_token)
{
	/* Exact match, unless ehv_pic node is NULL */
	struct device_node *of_node = irq_domain_get_of_node(h);
	return of_node == NULL || of_node == node;
}

static int ehv_pic_host_map(struct irq_domain *h, unsigned int virq,
			 irq_hw_number_t hw)
{
	struct ehv_pic *ehv_pic = h->host_data;
	struct irq_chip *chip;

	/* Default chip */
	chip = &ehv_pic->hc_irq;

	if (mpic_percpu_base_vaddr)
		if (hwirq_intspec[hw] & IRQ_TYPE_MPIC_DIRECT)
			chip = &ehv_pic_direct_eoi_irq_chip;

	irq_set_chip_data(virq, chip);
	/*
	 * using handle_fasteoi_irq as our irq handler, this will
	 * only call the eoi callback and suitable for the MPIC
	 * controller which set ISR/IPR automatically and clear the
	 * highest priority active interrupt in ISR/IPR when we do
	 * a specific eoi
	 */
	irq_set_chip_and_handler(virq, chip, handle_fasteoi_irq);

	/* Set default irq type */
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static int ehv_pic_host_xlate(struct irq_domain *h, struct device_node *ct,
			   const u32 *intspec, unsigned int intsize,
			   irq_hw_number_t *out_hwirq, unsigned int *out_flags)

{
	/*
	 * interrupt sense values coming from the guest device tree
	 * interrupt specifiers can have four possible sense and
	 * level encoding information and they need to
	 * be translated between firmware type & linux type.
	 */

	static unsigned char map_of_senses_to_linux_irqtype[4] = {
		IRQ_TYPE_EDGE_FALLING,
		IRQ_TYPE_EDGE_RISING,
		IRQ_TYPE_LEVEL_LOW,
		IRQ_TYPE_LEVEL_HIGH,
	};

	*out_hwirq = intspec[0];
	if (intsize > 1) {
		hwirq_intspec[intspec[0]] = intspec[1];
		*out_flags = map_of_senses_to_linux_irqtype[intspec[1] &
							~IRQ_TYPE_MPIC_DIRECT];
	} else {
		*out_flags = IRQ_TYPE_NONE;
	}

	return 0;
}

static const struct irq_domain_ops ehv_pic_host_ops = {
	.match = ehv_pic_host_match,
	.map = ehv_pic_host_map,
	.xlate = ehv_pic_host_xlate,
};

void __init ehv_pic_init(void)
{
	struct device_node *np, *np2;
	struct ehv_pic *ehv_pic;
	int coreint_flag = 1;

	np = of_find_compatible_node(NULL, NULL, "epapr,hv-pic");
	if (!np) {
		pr_err("ehv_pic_init: could not find epapr,hv-pic node\n");
		return;
	}

	if (!of_find_property(np, "has-external-proxy", NULL))
		coreint_flag = 0;

	ehv_pic = kzalloc(sizeof(struct ehv_pic), GFP_KERNEL);
	if (!ehv_pic) {
		of_node_put(np);
		return;
	}

	ehv_pic->irqhost = irq_domain_add_linear(np, NR_EHV_PIC_INTS,
						 &ehv_pic_host_ops, ehv_pic);
	if (!ehv_pic->irqhost) {
		of_node_put(np);
		kfree(ehv_pic);
		return;
	}

	np2 = of_find_compatible_node(NULL, NULL, "fsl,hv-mpic-per-cpu");
	if (np2) {
		mpic_percpu_base_vaddr = of_iomap(np2, 0);
		if (!mpic_percpu_base_vaddr)
			pr_err("ehv_pic_init: of_iomap failed\n");

		of_node_put(np2);
	}

	ehv_pic->hc_irq = ehv_pic_irq_chip;
	ehv_pic->hc_irq.irq_set_affinity = ehv_pic_set_affinity;
	ehv_pic->coreint_flag = coreint_flag;

	global_ehv_pic = ehv_pic;
	irq_set_default_host(global_ehv_pic->irqhost);
}
