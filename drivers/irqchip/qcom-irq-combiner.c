/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Driver for interrupt combiners in the Top-level Control and Status
 * Registers (TCSR) hardware block in Qualcomm Technologies chips.
 * An interrupt combiner in this block combines a set of interrupts by
 * OR'ing the individual interrupt signals into a summary interrupt
 * signal routed to a parent interrupt controller, and provides read-
 * only, 32-bit registers to query the status of individual interrupts.
 * The status bit for IRQ n is bit (n % 32) within register (n / 32)
 * of the given combiner. Thus, each combiner can be described as a set
 * of register offsets and the number of IRQs managed.
 */

#define pr_fmt(fmt) "QCOM80B1:" fmt

#include <linux/acpi.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/platform_device.h>

#define REG_SIZE 32

struct combiner_reg {
	void __iomem *addr;
	unsigned long enabled;
};

struct combiner {
	struct irq_domain   *domain;
	int                 parent_irq;
	u32                 nirqs;
	u32                 nregs;
	struct combiner_reg regs[0];
};

static inline int irq_nr(u32 reg, u32 bit)
{
	return reg * REG_SIZE + bit;
}

/*
 * Handler for the cascaded IRQ.
 */
static void combiner_handle_irq(struct irq_desc *desc)
{
	struct combiner *combiner = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 reg;

	chained_irq_enter(chip, desc);

	for (reg = 0; reg < combiner->nregs; reg++) {
		int virq;
		int hwirq;
		u32 bit;
		u32 status;

		bit = readl_relaxed(combiner->regs[reg].addr);
		status = bit & combiner->regs[reg].enabled;
		if (bit && !status)
			pr_warn_ratelimited("Unexpected IRQ on CPU%d: (%08x %08lx %p)\n",
					    smp_processor_id(), bit,
					    combiner->regs[reg].enabled,
					    combiner->regs[reg].addr);

		while (status) {
			bit = __ffs(status);
			status &= ~(1 << bit);
			hwirq = irq_nr(reg, bit);
			virq = irq_find_mapping(combiner->domain, hwirq);
			if (virq > 0)
				generic_handle_irq(virq);

		}
	}

	chained_irq_exit(chip, desc);
}

static void combiner_irq_chip_mask_irq(struct irq_data *data)
{
	struct combiner *combiner = irq_data_get_irq_chip_data(data);
	struct combiner_reg *reg = combiner->regs + data->hwirq / REG_SIZE;

	clear_bit(data->hwirq % REG_SIZE, &reg->enabled);
}

static void combiner_irq_chip_unmask_irq(struct irq_data *data)
{
	struct combiner *combiner = irq_data_get_irq_chip_data(data);
	struct combiner_reg *reg = combiner->regs + data->hwirq / REG_SIZE;

	set_bit(data->hwirq % REG_SIZE, &reg->enabled);
}

static struct irq_chip irq_chip = {
	.irq_mask = combiner_irq_chip_mask_irq,
	.irq_unmask = combiner_irq_chip_unmask_irq,
	.name = "qcom-irq-combiner"
};

static int combiner_irq_map(struct irq_domain *domain, unsigned int irq,
				   irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &irq_chip, handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);
	irq_set_noprobe(irq);
	return 0;
}

static void combiner_irq_unmap(struct irq_domain *domain, unsigned int irq)
{
	irq_domain_reset_irq_data(irq_get_irq_data(irq));
}

static int combiner_irq_translate(struct irq_domain *d, struct irq_fwspec *fws,
				  unsigned long *hwirq, unsigned int *type)
{
	struct combiner *combiner = d->host_data;

	if (is_acpi_node(fws->fwnode)) {
		if (WARN_ON((fws->param_count != 2) ||
			    (fws->param[0] >= combiner->nirqs) ||
			    (fws->param[1] & IORESOURCE_IRQ_LOWEDGE) ||
			    (fws->param[1] & IORESOURCE_IRQ_HIGHEDGE)))
			return -EINVAL;

		*hwirq = fws->param[0];
		*type = fws->param[1];
		return 0;
	}

	return -EINVAL;
}

static const struct irq_domain_ops domain_ops = {
	.map = combiner_irq_map,
	.unmap = combiner_irq_unmap,
	.translate = combiner_irq_translate
};

static acpi_status count_registers_cb(struct acpi_resource *ares, void *context)
{
	int *count = context;

	if (ares->type == ACPI_RESOURCE_TYPE_GENERIC_REGISTER)
		++(*count);
	return AE_OK;
}

static int count_registers(struct platform_device *pdev)
{
	acpi_handle ahandle = ACPI_HANDLE(&pdev->dev);
	acpi_status status;
	int count = 0;

	if (!acpi_has_method(ahandle, METHOD_NAME__CRS))
		return -EINVAL;

	status = acpi_walk_resources(ahandle, METHOD_NAME__CRS,
				     count_registers_cb, &count);
	if (ACPI_FAILURE(status))
		return -EINVAL;
	return count;
}

struct get_registers_context {
	struct device *dev;
	struct combiner *combiner;
	int err;
};

static acpi_status get_registers_cb(struct acpi_resource *ares, void *context)
{
	struct get_registers_context *ctx = context;
	struct acpi_resource_generic_register *reg;
	phys_addr_t paddr;
	void __iomem *vaddr;

	if (ares->type != ACPI_RESOURCE_TYPE_GENERIC_REGISTER)
		return AE_OK;

	reg = &ares->data.generic_reg;
	paddr = reg->address;
	if ((reg->space_id != ACPI_SPACE_MEM) ||
	    (reg->bit_offset != 0) ||
	    (reg->bit_width > REG_SIZE)) {
		dev_err(ctx->dev, "Bad register resource @%pa\n", &paddr);
		ctx->err = -EINVAL;
		return AE_ERROR;
	}

	vaddr = devm_ioremap(ctx->dev, reg->address, REG_SIZE);
	if (!vaddr) {
		dev_err(ctx->dev, "Can't map register @%pa\n", &paddr);
		ctx->err = -ENOMEM;
		return AE_ERROR;
	}

	ctx->combiner->regs[ctx->combiner->nregs].addr = vaddr;
	ctx->combiner->nirqs += reg->bit_width;
	ctx->combiner->nregs++;
	return AE_OK;
}

static int get_registers(struct platform_device *pdev, struct combiner *comb)
{
	acpi_handle ahandle = ACPI_HANDLE(&pdev->dev);
	acpi_status status;
	struct get_registers_context ctx;

	if (!acpi_has_method(ahandle, METHOD_NAME__CRS))
		return -EINVAL;

	ctx.dev = &pdev->dev;
	ctx.combiner = comb;
	ctx.err = 0;

	status = acpi_walk_resources(ahandle, METHOD_NAME__CRS,
				     get_registers_cb, &ctx);
	if (ACPI_FAILURE(status))
		return ctx.err;
	return 0;
}

static int __init combiner_probe(struct platform_device *pdev)
{
	struct combiner *combiner;
	size_t alloc_sz;
	int nregs;
	int err;

	nregs = count_registers(pdev);
	if (nregs <= 0) {
		dev_err(&pdev->dev, "Error reading register resources\n");
		return -EINVAL;
	}

	alloc_sz = sizeof(*combiner) + sizeof(struct combiner_reg) * nregs;
	combiner = devm_kzalloc(&pdev->dev, alloc_sz, GFP_KERNEL);
	if (!combiner)
		return -ENOMEM;

	err = get_registers(pdev, combiner);
	if (err < 0)
		return err;

	combiner->parent_irq = platform_get_irq(pdev, 0);
	if (combiner->parent_irq <= 0) {
		dev_err(&pdev->dev, "Error getting IRQ resource\n");
		return -EPROBE_DEFER;
	}

	combiner->domain = irq_domain_create_linear(pdev->dev.fwnode, combiner->nirqs,
						    &domain_ops, combiner);
	if (!combiner->domain)
		/* Errors printed by irq_domain_create_linear */
		return -ENODEV;

	irq_set_chained_handler_and_data(combiner->parent_irq,
					 combiner_handle_irq, combiner);

	dev_info(&pdev->dev, "Initialized with [p=%d,n=%d,r=%p]\n",
		 combiner->parent_irq, combiner->nirqs, combiner->regs[0].addr);
	return 0;
}

static const struct acpi_device_id qcom_irq_combiner_ids[] = {
	{ "QCOM80B1", },
	{ }
};

static struct platform_driver qcom_irq_combiner_probe = {
	.driver = {
		.name = "qcom-irq-combiner",
		.acpi_match_table = ACPI_PTR(qcom_irq_combiner_ids),
	},
	.probe = combiner_probe,
};
builtin_platform_driver(qcom_irq_combiner_probe);
