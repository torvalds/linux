// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 * Copyright (c) 2016 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define MAX_NUM_CHANNEL 64
#define MAX_INPUT_MUX 256

#define REG_EDGE_POL	0x00
#define REG_PIN_03_SEL	0x04
#define REG_PIN_47_SEL	0x08
#define REG_FILTER_SEL	0x0c

/* use for A1 like chips */
#define REG_PIN_A1_SEL	0x04

/*
 * Note: The S905X3 datasheet reports that BOTH_EDGE is controlled by
 * bits 24 to 31. Tests on the actual HW show that these bits are
 * stuck at 0. Bits 8 to 15 are responsive and have the expected
 * effect.
 */
#define REG_EDGE_POL_EDGE(params, x)	BIT((params)->edge_single_offset + (x))
#define REG_EDGE_POL_LOW(params, x)	BIT((params)->pol_low_offset + (x))
#define REG_BOTH_EDGE(params, x)	BIT((params)->edge_both_offset + (x))
#define REG_EDGE_POL_MASK(params, x)    (	\
		REG_EDGE_POL_EDGE(params, x) |	\
		REG_EDGE_POL_LOW(params, x)  |	\
		REG_BOTH_EDGE(params, x))
#define REG_PIN_SEL_SHIFT(x)	(((x) % 4) * 8)
#define REG_FILTER_SEL_SHIFT(x)	((x) * 4)

struct meson_gpio_irq_controller;
static void meson8_gpio_irq_sel_pin(struct meson_gpio_irq_controller *ctl,
				    unsigned int channel, unsigned long hwirq);
static void meson_gpio_irq_init_dummy(struct meson_gpio_irq_controller *ctl);
static void meson_a1_gpio_irq_sel_pin(struct meson_gpio_irq_controller *ctl,
				      unsigned int channel,
				      unsigned long hwirq);
static void meson_a1_gpio_irq_init(struct meson_gpio_irq_controller *ctl);
static int meson8_gpio_irq_set_type(struct meson_gpio_irq_controller *ctl,
				    unsigned int type, u32 *channel_hwirq);
static int meson_s4_gpio_irq_set_type(struct meson_gpio_irq_controller *ctl,
				      unsigned int type, u32 *channel_hwirq);

struct irq_ctl_ops {
	void (*gpio_irq_sel_pin)(struct meson_gpio_irq_controller *ctl,
				 unsigned int channel, unsigned long hwirq);
	void (*gpio_irq_init)(struct meson_gpio_irq_controller *ctl);
	int (*gpio_irq_set_type)(struct meson_gpio_irq_controller *ctl,
				 unsigned int type, u32 *channel_hwirq);
};

struct meson_gpio_irq_params {
	unsigned int nr_hwirq;
	unsigned int nr_channels;
	bool support_edge_both;
	unsigned int edge_both_offset;
	unsigned int edge_single_offset;
	unsigned int edge_pol_reg;
	unsigned int pol_low_offset;
	unsigned int pin_sel_mask;
	struct irq_ctl_ops ops;
};

#define INIT_MESON_COMMON(irqs, init, sel, type)		\
	.nr_hwirq = irqs,					\
	.ops = {						\
		.gpio_irq_init = init,				\
		.gpio_irq_sel_pin = sel,			\
		.gpio_irq_set_type = type,			\
	},

#define INIT_MESON8_COMMON_DATA(irqs)				\
	INIT_MESON_COMMON(irqs, meson_gpio_irq_init_dummy,	\
			  meson8_gpio_irq_sel_pin,		\
			  meson8_gpio_irq_set_type)		\
	.edge_single_offset = 0,				\
	.pol_low_offset = 16,					\
	.pin_sel_mask = 0xff,					\
	.nr_channels = 8,					\

#define INIT_MESON_A1_COMMON_DATA(irqs)				\
	INIT_MESON_COMMON(irqs, meson_a1_gpio_irq_init,		\
			  meson_a1_gpio_irq_sel_pin,		\
			  meson8_gpio_irq_set_type)		\
	.support_edge_both = true,				\
	.edge_both_offset = 16,					\
	.edge_single_offset = 8,				\
	.pol_low_offset = 0,					\
	.pin_sel_mask = 0x7f,					\
	.nr_channels = 8,					\

#define INIT_MESON_A4_AO_COMMON_DATA(irqs)			\
	INIT_MESON_COMMON(irqs, meson_a1_gpio_irq_init,		\
			  meson_a1_gpio_irq_sel_pin,		\
			  meson_s4_gpio_irq_set_type)		\
	.support_edge_both = true,				\
	.edge_both_offset = 0,					\
	.edge_single_offset = 12,				\
	.edge_pol_reg = 0x8,					\
	.pol_low_offset = 0,					\
	.pin_sel_mask = 0xff,					\
	.nr_channels = 2,					\

#define INIT_MESON_S4_COMMON_DATA(irqs)				\
	INIT_MESON_COMMON(irqs, meson_a1_gpio_irq_init,		\
			  meson_a1_gpio_irq_sel_pin,		\
			  meson_s4_gpio_irq_set_type)		\
	.support_edge_both = true,				\
	.edge_both_offset = 0,					\
	.edge_single_offset = 12,				\
	.edge_pol_reg = 0x1c,					\
	.pol_low_offset = 0,					\
	.pin_sel_mask = 0xff,					\
	.nr_channels = 12,					\

static const struct meson_gpio_irq_params meson8_params = {
	INIT_MESON8_COMMON_DATA(134)
};

static const struct meson_gpio_irq_params meson8b_params = {
	INIT_MESON8_COMMON_DATA(119)
};

static const struct meson_gpio_irq_params gxbb_params = {
	INIT_MESON8_COMMON_DATA(133)
};

static const struct meson_gpio_irq_params gxl_params = {
	INIT_MESON8_COMMON_DATA(110)
};

static const struct meson_gpio_irq_params axg_params = {
	INIT_MESON8_COMMON_DATA(100)
};

static const struct meson_gpio_irq_params sm1_params = {
	INIT_MESON8_COMMON_DATA(100)
	.support_edge_both = true,
	.edge_both_offset = 8,
};

static const struct meson_gpio_irq_params a1_params = {
	INIT_MESON_A1_COMMON_DATA(62)
};

static const struct meson_gpio_irq_params a4_params = {
	INIT_MESON_S4_COMMON_DATA(81)
};

static const struct meson_gpio_irq_params a4_ao_params = {
	INIT_MESON_A4_AO_COMMON_DATA(8)
};

static const struct meson_gpio_irq_params a5_params = {
	INIT_MESON_S4_COMMON_DATA(99)
};

static const struct meson_gpio_irq_params s4_params = {
	INIT_MESON_S4_COMMON_DATA(82)
};

static const struct meson_gpio_irq_params c3_params = {
	INIT_MESON_S4_COMMON_DATA(55)
};

static const struct meson_gpio_irq_params t7_params = {
	INIT_MESON_S4_COMMON_DATA(157)
};

static const struct of_device_id meson_irq_gpio_matches[] __maybe_unused = {
	{ .compatible = "amlogic,meson8-gpio-intc", .data = &meson8_params },
	{ .compatible = "amlogic,meson8b-gpio-intc", .data = &meson8b_params },
	{ .compatible = "amlogic,meson-gxbb-gpio-intc", .data = &gxbb_params },
	{ .compatible = "amlogic,meson-gxl-gpio-intc", .data = &gxl_params },
	{ .compatible = "amlogic,meson-axg-gpio-intc", .data = &axg_params },
	{ .compatible = "amlogic,meson-g12a-gpio-intc", .data = &axg_params },
	{ .compatible = "amlogic,meson-sm1-gpio-intc", .data = &sm1_params },
	{ .compatible = "amlogic,meson-a1-gpio-intc", .data = &a1_params },
	{ .compatible = "amlogic,meson-s4-gpio-intc", .data = &s4_params },
	{ .compatible = "amlogic,a4-gpio-ao-intc", .data = &a4_ao_params },
	{ .compatible = "amlogic,a4-gpio-intc", .data = &a4_params },
	{ .compatible = "amlogic,a5-gpio-intc", .data = &a5_params },
	{ .compatible = "amlogic,c3-gpio-intc", .data = &c3_params },
	{ .compatible = "amlogic,t7-gpio-intc", .data = &t7_params },
	{ }
};

struct meson_gpio_irq_controller {
	const struct meson_gpio_irq_params *params;
	void __iomem *base;
	u32 channel_irqs[MAX_NUM_CHANNEL];
	DECLARE_BITMAP(channel_map, MAX_NUM_CHANNEL);
	raw_spinlock_t lock;
};

static void meson_gpio_irq_update_bits(struct meson_gpio_irq_controller *ctl,
				       unsigned int reg, u32 mask, u32 val)
{
	unsigned long flags;
	u32 tmp;

	raw_spin_lock_irqsave(&ctl->lock, flags);

	tmp = readl_relaxed(ctl->base + reg);
	tmp &= ~mask;
	tmp |= val;
	writel_relaxed(tmp, ctl->base + reg);

	raw_spin_unlock_irqrestore(&ctl->lock, flags);
}

static void meson_gpio_irq_init_dummy(struct meson_gpio_irq_controller *ctl)
{
}

static void meson8_gpio_irq_sel_pin(struct meson_gpio_irq_controller *ctl,
				    unsigned int channel, unsigned long hwirq)
{
	unsigned int reg_offset;
	unsigned int bit_offset;

	reg_offset = (channel < 4) ? REG_PIN_03_SEL : REG_PIN_47_SEL;
	bit_offset = REG_PIN_SEL_SHIFT(channel);

	meson_gpio_irq_update_bits(ctl, reg_offset,
				   ctl->params->pin_sel_mask << bit_offset,
				   hwirq << bit_offset);
}

static void meson_a1_gpio_irq_sel_pin(struct meson_gpio_irq_controller *ctl,
				      unsigned int channel,
				      unsigned long hwirq)
{
	unsigned int reg_offset;
	unsigned int bit_offset;

	bit_offset = ((channel % 2) == 0) ? 0 : 16;
	reg_offset = REG_PIN_A1_SEL + ((channel / 2) << 2);

	meson_gpio_irq_update_bits(ctl, reg_offset,
				   ctl->params->pin_sel_mask << bit_offset,
				   hwirq << bit_offset);
}

/* For a1 or later chips like a1 there is a switch to enable/disable irq */
static void meson_a1_gpio_irq_init(struct meson_gpio_irq_controller *ctl)
{
	meson_gpio_irq_update_bits(ctl, REG_EDGE_POL, BIT(31), BIT(31));
}

static int
meson_gpio_irq_request_channel(struct meson_gpio_irq_controller *ctl,
			       unsigned long  hwirq,
			       u32 **channel_hwirq)
{
	unsigned long flags;
	unsigned int idx;

	raw_spin_lock_irqsave(&ctl->lock, flags);

	/* Find a free channel */
	idx = find_first_zero_bit(ctl->channel_map, ctl->params->nr_channels);
	if (idx >= ctl->params->nr_channels) {
		raw_spin_unlock_irqrestore(&ctl->lock, flags);
		pr_err("No channel available\n");
		return -ENOSPC;
	}

	/* Mark the channel as used */
	set_bit(idx, ctl->channel_map);

	raw_spin_unlock_irqrestore(&ctl->lock, flags);

	/*
	 * Setup the mux of the channel to route the signal of the pad
	 * to the appropriate input of the GIC
	 */
	ctl->params->ops.gpio_irq_sel_pin(ctl, idx, hwirq);

	/*
	 * Get the hwirq number assigned to this channel through
	 * a pointer the channel_irq table. The added benefit of this
	 * method is that we can also retrieve the channel index with
	 * it, using the table base.
	 */
	*channel_hwirq = &(ctl->channel_irqs[idx]);

	pr_debug("hwirq %lu assigned to channel %d - irq %u\n",
		 hwirq, idx, **channel_hwirq);

	return 0;
}

static unsigned int
meson_gpio_irq_get_channel_idx(struct meson_gpio_irq_controller *ctl,
			       u32 *channel_hwirq)
{
	return channel_hwirq - ctl->channel_irqs;
}

static void
meson_gpio_irq_release_channel(struct meson_gpio_irq_controller *ctl,
			       u32 *channel_hwirq)
{
	unsigned int idx;

	idx = meson_gpio_irq_get_channel_idx(ctl, channel_hwirq);
	clear_bit(idx, ctl->channel_map);
}

static int meson8_gpio_irq_set_type(struct meson_gpio_irq_controller *ctl,
				    unsigned int type, u32 *channel_hwirq)
{
	const struct meson_gpio_irq_params *params = ctl->params;
	unsigned int idx;
	u32 val = 0;

	idx = meson_gpio_irq_get_channel_idx(ctl, channel_hwirq);

	/*
	 * The controller has a filter block to operate in either LEVEL or
	 * EDGE mode, then signal is sent to the GIC. To enable LEVEL_LOW and
	 * EDGE_FALLING support (which the GIC does not support), the filter
	 * block is also able to invert the input signal it gets before
	 * providing it to the GIC.
	 */
	type &= IRQ_TYPE_SENSE_MASK;

	/*
	 * New controller support EDGE_BOTH trigger. This setting takes
	 * precedence over the other edge/polarity settings
	 */
	if (type == IRQ_TYPE_EDGE_BOTH) {
		if (!params->support_edge_both)
			return -EINVAL;

		val |= REG_BOTH_EDGE(params, idx);
	} else {
		if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING))
			val |= REG_EDGE_POL_EDGE(params, idx);

		if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_EDGE_FALLING))
			val |= REG_EDGE_POL_LOW(params, idx);
	}

	meson_gpio_irq_update_bits(ctl, REG_EDGE_POL,
				   REG_EDGE_POL_MASK(params, idx), val);

	return 0;
}

/*
 * gpio irq relative registers for s4
 * -PADCTRL_GPIO_IRQ_CTRL0
 * bit[31]:    enable/disable all the irq lines
 * bit[12-23]: single edge trigger
 * bit[0-11]:  polarity trigger
 *
 * -PADCTRL_GPIO_IRQ_CTRL[X]
 * bit[0-16]: 7 bits to choose gpio source for irq line 2*[X] - 2
 * bit[16-22]:7 bits to choose gpio source for irq line 2*[X] - 1
 * where X = 1-6
 *
 * -PADCTRL_GPIO_IRQ_CTRL[7]
 * bit[0-11]: both edge trigger
 */
static int meson_s4_gpio_irq_set_type(struct meson_gpio_irq_controller *ctl,
				      unsigned int type, u32 *channel_hwirq)
{
	const struct meson_gpio_irq_params *params = ctl->params;
	unsigned int idx;
	u32 val = 0;

	idx = meson_gpio_irq_get_channel_idx(ctl, channel_hwirq);

	type &= IRQ_TYPE_SENSE_MASK;

	meson_gpio_irq_update_bits(ctl, params->edge_pol_reg, BIT(idx), 0);

	if (type == IRQ_TYPE_EDGE_BOTH) {
		val = BIT(ctl->params->edge_both_offset + idx);
		meson_gpio_irq_update_bits(ctl, params->edge_pol_reg, val, val);
		return 0;
	}

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_EDGE_FALLING))
		val |= BIT(ctl->params->pol_low_offset + idx);

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING))
		val |= BIT(ctl->params->edge_single_offset + idx);

	meson_gpio_irq_update_bits(ctl, params->edge_pol_reg,
				   BIT(idx) | BIT(12 + idx), val);
	return 0;
};

static unsigned int meson_gpio_irq_type_output(unsigned int type)
{
	unsigned int sense = type & IRQ_TYPE_SENSE_MASK;

	type &= ~IRQ_TYPE_SENSE_MASK;

	/*
	 * The polarity of the signal provided to the GIC should always
	 * be high.
	 */
	if (sense & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW))
		type |= IRQ_TYPE_LEVEL_HIGH;
	else
		type |= IRQ_TYPE_EDGE_RISING;

	return type;
}

static int meson_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct meson_gpio_irq_controller *ctl = data->domain->host_data;
	u32 *channel_hwirq = irq_data_get_irq_chip_data(data);
	int ret;

	ret = ctl->params->ops.gpio_irq_set_type(ctl, type, channel_hwirq);
	if (ret)
		return ret;

	return irq_chip_set_type_parent(data,
					meson_gpio_irq_type_output(type));
}

static struct irq_chip meson_gpio_irq_chip = {
	.name			= "meson-gpio-irqchip",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_type		= meson_gpio_irq_set_type,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
#ifdef CONFIG_SMP
	.irq_set_affinity	= irq_chip_set_affinity_parent,
#endif
	.flags			= IRQCHIP_SET_TYPE_MASKED,
};

static int meson_gpio_irq_domain_translate(struct irq_domain *domain,
					   struct irq_fwspec *fwspec,
					   unsigned long *hwirq,
					   unsigned int *type)
{
	if (is_of_node(fwspec->fwnode) && fwspec->param_count == 2) {
		*hwirq	= fwspec->param[0];
		*type	= fwspec->param[1];
		return 0;
	}

	return -EINVAL;
}

static int meson_gpio_irq_allocate_gic_irq(struct irq_domain *domain,
					   unsigned int virq,
					   u32 hwirq,
					   unsigned int type)
{
	struct irq_fwspec fwspec;

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 3;
	fwspec.param[0] = 0;	/* SPI */
	fwspec.param[1] = hwirq;
	fwspec.param[2] = meson_gpio_irq_type_output(type);

	return irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
}

static int meson_gpio_irq_domain_alloc(struct irq_domain *domain,
				       unsigned int virq,
				       unsigned int nr_irqs,
				       void *data)
{
	struct irq_fwspec *fwspec = data;
	struct meson_gpio_irq_controller *ctl = domain->host_data;
	unsigned long hwirq;
	u32 *channel_hwirq;
	unsigned int type;
	int ret;

	if (WARN_ON(nr_irqs != 1))
		return -EINVAL;

	ret = meson_gpio_irq_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	ret = meson_gpio_irq_request_channel(ctl, hwirq, &channel_hwirq);
	if (ret)
		return ret;

	ret = meson_gpio_irq_allocate_gic_irq(domain, virq,
					      *channel_hwirq, type);
	if (ret < 0) {
		pr_err("failed to allocate gic irq %u\n", *channel_hwirq);
		meson_gpio_irq_release_channel(ctl, channel_hwirq);
		return ret;
	}

	irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
				      &meson_gpio_irq_chip, channel_hwirq);

	return 0;
}

static void meson_gpio_irq_domain_free(struct irq_domain *domain,
				       unsigned int virq,
				       unsigned int nr_irqs)
{
	struct meson_gpio_irq_controller *ctl = domain->host_data;
	struct irq_data *irq_data;
	u32 *channel_hwirq;

	if (WARN_ON(nr_irqs != 1))
		return;

	irq_domain_free_irqs_parent(domain, virq, 1);

	irq_data = irq_domain_get_irq_data(domain, virq);
	channel_hwirq = irq_data_get_irq_chip_data(irq_data);

	meson_gpio_irq_release_channel(ctl, channel_hwirq);
}

static const struct irq_domain_ops meson_gpio_irq_domain_ops = {
	.alloc		= meson_gpio_irq_domain_alloc,
	.free		= meson_gpio_irq_domain_free,
	.translate	= meson_gpio_irq_domain_translate,
};

static int meson_gpio_irq_parse_dt(struct device_node *node, struct meson_gpio_irq_controller *ctl)
{
	const struct of_device_id *match;
	int ret;

	match = of_match_node(meson_irq_gpio_matches, node);
	if (!match)
		return -ENODEV;

	ctl->params = match->data;

	ret = of_property_read_variable_u32_array(node,
						  "amlogic,channel-interrupts",
						  ctl->channel_irqs,
						  ctl->params->nr_channels,
						  ctl->params->nr_channels);
	if (ret < 0) {
		pr_err("can't get %d channel interrupts\n", ctl->params->nr_channels);
		return ret;
	}

	ctl->params->ops.gpio_irq_init(ctl);

	return 0;
}

static int meson_gpio_irq_of_init(struct device_node *node, struct device_node *parent)
{
	struct irq_domain *domain, *parent_domain;
	struct meson_gpio_irq_controller *ctl;
	int ret;

	if (!parent) {
		pr_err("missing parent interrupt node\n");
		return -ENODEV;
	}

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("unable to obtain parent domain\n");
		return -ENXIO;
	}

	ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;

	raw_spin_lock_init(&ctl->lock);

	ctl->base = of_iomap(node, 0);
	if (!ctl->base) {
		ret = -ENOMEM;
		goto free_ctl;
	}

	ret = meson_gpio_irq_parse_dt(node, ctl);
	if (ret)
		goto free_channel_irqs;

	domain = irq_domain_create_hierarchy(parent_domain, 0,
					     ctl->params->nr_hwirq,
					     of_node_to_fwnode(node),
					     &meson_gpio_irq_domain_ops,
					     ctl);
	if (!domain) {
		pr_err("failed to add domain\n");
		ret = -ENODEV;
		goto free_channel_irqs;
	}

	pr_info("%d to %d gpio interrupt mux initialized\n",
		ctl->params->nr_hwirq, ctl->params->nr_channels);

	return 0;

free_channel_irqs:
	iounmap(ctl->base);
free_ctl:
	kfree(ctl);

	return ret;
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(meson_gpio_intc)
IRQCHIP_MATCH("amlogic,meson-gpio-intc", meson_gpio_irq_of_init)
IRQCHIP_PLATFORM_DRIVER_END(meson_gpio_intc)

MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_DESCRIPTION("Meson GPIO Interrupt Multiplexer driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:meson-gpio-intc");
