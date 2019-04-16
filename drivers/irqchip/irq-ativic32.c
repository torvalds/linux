// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <nds32_intrinsic.h>

unsigned long wake_mask;

static void ativic32_ack_irq(struct irq_data *data)
{
	__nds32__mtsr_dsb(BIT(data->hwirq), NDS32_SR_INT_PEND2);
}

static void ativic32_mask_irq(struct irq_data *data)
{
	unsigned long int_mask2 = __nds32__mfsr(NDS32_SR_INT_MASK2);
	__nds32__mtsr_dsb(int_mask2 & (~(BIT(data->hwirq))), NDS32_SR_INT_MASK2);
}

static void ativic32_unmask_irq(struct irq_data *data)
{
	unsigned long int_mask2 = __nds32__mfsr(NDS32_SR_INT_MASK2);
	__nds32__mtsr_dsb(int_mask2 | (BIT(data->hwirq)), NDS32_SR_INT_MASK2);
}

static int nointc_set_wake(struct irq_data *data, unsigned int on)
{
	unsigned long int_mask = __nds32__mfsr(NDS32_SR_INT_MASK);
	static unsigned long irq_orig_bit;
	u32 bit = 1 << data->hwirq;

	if (on) {
		if (int_mask & bit)
			__assign_bit(data->hwirq, &irq_orig_bit, true);
		else
			__assign_bit(data->hwirq, &irq_orig_bit, false);

		__assign_bit(data->hwirq, &int_mask, true);
		__assign_bit(data->hwirq, &wake_mask, true);

	} else {
		if (!(irq_orig_bit & bit))
			__assign_bit(data->hwirq, &int_mask, false);

		__assign_bit(data->hwirq, &wake_mask, false);
		__assign_bit(data->hwirq, &irq_orig_bit, false);
	}

	__nds32__mtsr_dsb(int_mask, NDS32_SR_INT_MASK);

	return 0;
}

static struct irq_chip ativic32_chip = {
	.name = "ativic32",
	.irq_ack = ativic32_ack_irq,
	.irq_mask = ativic32_mask_irq,
	.irq_unmask = ativic32_unmask_irq,
	.irq_set_wake = nointc_set_wake,
};

static unsigned int __initdata nivic_map[6] = { 6, 2, 10, 16, 24, 32 };

static struct irq_domain *root_domain;
static int ativic32_irq_domain_map(struct irq_domain *id, unsigned int virq,
				  irq_hw_number_t hw)
{

	unsigned long int_trigger_type;
	u32 type;
	struct irq_data *irq_data;
	int_trigger_type = __nds32__mfsr(NDS32_SR_INT_TRIGGER);
	irq_data = irq_get_irq_data(virq);
	if (!irq_data)
		return -EINVAL;

	if (int_trigger_type & (BIT(hw))) {
		irq_set_chip_and_handler(virq, &ativic32_chip, handle_edge_irq);
		type = IRQ_TYPE_EDGE_RISING;
	} else {
		irq_set_chip_and_handler(virq, &ativic32_chip, handle_level_irq);
		type = IRQ_TYPE_LEVEL_HIGH;
	}

	irqd_set_trigger_type(irq_data, type);
	return 0;
}

static struct irq_domain_ops ativic32_ops = {
	.map = ativic32_irq_domain_map,
	.xlate = irq_domain_xlate_onecell
};

static irq_hw_number_t get_intr_src(void)
{
	return ((__nds32__mfsr(NDS32_SR_ITYPE) & ITYPE_mskVECTOR) >> ITYPE_offVECTOR)
		- NDS32_VECTOR_offINTERRUPT;
}

asmlinkage void asm_do_IRQ(struct pt_regs *regs)
{
	irq_hw_number_t hwirq = get_intr_src();
	handle_domain_irq(root_domain, hwirq, regs);
}

int __init ativic32_init_irq(struct device_node *node, struct device_node *parent)
{
	unsigned long int_vec_base, nivic, nr_ints;

	if (WARN(parent, "non-root ativic32 are not supported"))
		return -EINVAL;

	int_vec_base = __nds32__mfsr(NDS32_SR_IVB);

	if (((int_vec_base & IVB_mskIVIC_VER) >> IVB_offIVIC_VER) == 0)
		panic("Unable to use atcivic32 for this cpu.\n");

	nivic = (int_vec_base & IVB_mskNIVIC) >> IVB_offNIVIC;
	if (nivic >= ARRAY_SIZE(nivic_map))
		panic("The number of input for ativic32 is not supported.\n");

	nr_ints = nivic_map[nivic];

	root_domain = irq_domain_add_linear(node, nr_ints,
			&ativic32_ops, NULL);

	if (!root_domain)
		panic("%s: unable to create IRQ domain\n", node->full_name);

	return 0;
}
IRQCHIP_DECLARE(ativic32, "andestech,ativic32", ativic32_init_irq);
