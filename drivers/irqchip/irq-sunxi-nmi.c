/*
 * Allwinner A20/A31 SoCs NMI IRQ chip driver.
 *
 * Carlo Caione <carlo.caione@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define DRV_NAME	"sunxi-nmi"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>

#define SUNXI_NMI_SRC_TYPE_MASK	0x00000003

#define SUNXI_NMI_IRQ_BIT	BIT(0)

#define SUN6I_R_INTC_CTRL	0x0c
#define SUN6I_R_INTC_PENDING	0x10
#define SUN6I_R_INTC_ENABLE	0x40

/*
 * For deprecated sun6i-a31-sc-nmi compatible.
 * Registers are offset by 0x0c.
 */
#define SUN6I_R_INTC_NMI_OFFSET	0x0c
#define SUN6I_NMI_CTRL		(SUN6I_R_INTC_CTRL - SUN6I_R_INTC_NMI_OFFSET)
#define SUN6I_NMI_PENDING	(SUN6I_R_INTC_PENDING - SUN6I_R_INTC_NMI_OFFSET)
#define SUN6I_NMI_ENABLE	(SUN6I_R_INTC_ENABLE - SUN6I_R_INTC_NMI_OFFSET)

#define SUN7I_NMI_CTRL		0x00
#define SUN7I_NMI_PENDING	0x04
#define SUN7I_NMI_ENABLE	0x08

#define SUN9I_NMI_CTRL		0x00
#define SUN9I_NMI_ENABLE	0x04
#define SUN9I_NMI_PENDING	0x08

enum {
	SUNXI_SRC_TYPE_LEVEL_LOW = 0,
	SUNXI_SRC_TYPE_EDGE_FALLING,
	SUNXI_SRC_TYPE_LEVEL_HIGH,
	SUNXI_SRC_TYPE_EDGE_RISING,
};

struct sunxi_sc_nmi_reg_offs {
	u32 ctrl;
	u32 pend;
	u32 enable;
};

static const struct sunxi_sc_nmi_reg_offs sun6i_r_intc_reg_offs __initconst = {
	.ctrl	= SUN6I_R_INTC_CTRL,
	.pend	= SUN6I_R_INTC_PENDING,
	.enable	= SUN6I_R_INTC_ENABLE,
};

static const struct sunxi_sc_nmi_reg_offs sun6i_reg_offs __initconst = {
	.ctrl	= SUN6I_NMI_CTRL,
	.pend	= SUN6I_NMI_PENDING,
	.enable	= SUN6I_NMI_ENABLE,
};

static const struct sunxi_sc_nmi_reg_offs sun7i_reg_offs __initconst = {
	.ctrl	= SUN7I_NMI_CTRL,
	.pend	= SUN7I_NMI_PENDING,
	.enable	= SUN7I_NMI_ENABLE,
};

static const struct sunxi_sc_nmi_reg_offs sun9i_reg_offs __initconst = {
	.ctrl	= SUN9I_NMI_CTRL,
	.pend	= SUN9I_NMI_PENDING,
	.enable	= SUN9I_NMI_ENABLE,
};

static inline void sunxi_sc_nmi_write(struct irq_chip_generic *gc, u32 off,
				      u32 val)
{
	irq_reg_writel(gc, val, off);
}

static inline u32 sunxi_sc_nmi_read(struct irq_chip_generic *gc, u32 off)
{
	return irq_reg_readl(gc, off);
}

static void sunxi_sc_nmi_handle_irq(struct irq_desc *desc)
{
	struct irq_domain *domain = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int virq = irq_find_mapping(domain, 0);

	chained_irq_enter(chip, desc);
	generic_handle_irq(virq);
	chained_irq_exit(chip, desc);
}

static int sunxi_sc_nmi_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	struct irq_chip_type *ct = gc->chip_types;
	u32 src_type_reg;
	u32 ctrl_off = ct->regs.type;
	unsigned int src_type;
	unsigned int i;

	irq_gc_lock(gc);

	switch (flow_type & IRQF_TRIGGER_MASK) {
	case IRQ_TYPE_EDGE_FALLING:
		src_type = SUNXI_SRC_TYPE_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_RISING:
		src_type = SUNXI_SRC_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		src_type = SUNXI_SRC_TYPE_LEVEL_HIGH;
		break;
	case IRQ_TYPE_NONE:
	case IRQ_TYPE_LEVEL_LOW:
		src_type = SUNXI_SRC_TYPE_LEVEL_LOW;
		break;
	default:
		irq_gc_unlock(gc);
		pr_err("Cannot assign multiple trigger modes to IRQ %d.\n",
			data->irq);
		return -EBADR;
	}

	irqd_set_trigger_type(data, flow_type);
	irq_setup_alt_chip(data, flow_type);

	for (i = 0; i < gc->num_ct; i++, ct++)
		if (ct->type & flow_type)
			ctrl_off = ct->regs.type;

	src_type_reg = sunxi_sc_nmi_read(gc, ctrl_off);
	src_type_reg &= ~SUNXI_NMI_SRC_TYPE_MASK;
	src_type_reg |= src_type;
	sunxi_sc_nmi_write(gc, ctrl_off, src_type_reg);

	irq_gc_unlock(gc);

	return IRQ_SET_MASK_OK;
}

static int __init sunxi_sc_nmi_irq_init(struct device_node *node,
					const struct sunxi_sc_nmi_reg_offs *reg_offs)
{
	struct irq_domain *domain;
	struct irq_chip_generic *gc;
	unsigned int irq;
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	int ret;


	domain = irq_domain_add_linear(node, 1, &irq_generic_chip_ops, NULL);
	if (!domain) {
		pr_err("Could not register interrupt domain.\n");
		return -ENOMEM;
	}

	ret = irq_alloc_domain_generic_chips(domain, 1, 2, DRV_NAME,
					     handle_fasteoi_irq, clr, 0,
					     IRQ_GC_INIT_MASK_CACHE);
	if (ret) {
		pr_err("Could not allocate generic interrupt chip.\n");
		goto fail_irqd_remove;
	}

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0) {
		pr_err("unable to parse irq\n");
		ret = -EINVAL;
		goto fail_irqd_remove;
	}

	gc = irq_get_domain_generic_chip(domain, 0);
	gc->reg_base = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(gc->reg_base)) {
		pr_err("unable to map resource\n");
		ret = PTR_ERR(gc->reg_base);
		goto fail_irqd_remove;
	}

	gc->chip_types[0].type			= IRQ_TYPE_LEVEL_MASK;
	gc->chip_types[0].chip.irq_mask		= irq_gc_mask_clr_bit;
	gc->chip_types[0].chip.irq_unmask	= irq_gc_mask_set_bit;
	gc->chip_types[0].chip.irq_eoi		= irq_gc_ack_set_bit;
	gc->chip_types[0].chip.irq_set_type	= sunxi_sc_nmi_set_type;
	gc->chip_types[0].chip.flags		= IRQCHIP_EOI_THREADED | IRQCHIP_EOI_IF_HANDLED;
	gc->chip_types[0].regs.ack		= reg_offs->pend;
	gc->chip_types[0].regs.mask		= reg_offs->enable;
	gc->chip_types[0].regs.type		= reg_offs->ctrl;

	gc->chip_types[1].type			= IRQ_TYPE_EDGE_BOTH;
	gc->chip_types[1].chip.name		= gc->chip_types[0].chip.name;
	gc->chip_types[1].chip.irq_ack		= irq_gc_ack_set_bit;
	gc->chip_types[1].chip.irq_mask		= irq_gc_mask_clr_bit;
	gc->chip_types[1].chip.irq_unmask	= irq_gc_mask_set_bit;
	gc->chip_types[1].chip.irq_set_type	= sunxi_sc_nmi_set_type;
	gc->chip_types[1].regs.ack		= reg_offs->pend;
	gc->chip_types[1].regs.mask		= reg_offs->enable;
	gc->chip_types[1].regs.type		= reg_offs->ctrl;
	gc->chip_types[1].handler		= handle_edge_irq;

	/* Disable any active interrupts */
	sunxi_sc_nmi_write(gc, reg_offs->enable, 0);

	/* Clear any pending NMI interrupts */
	sunxi_sc_nmi_write(gc, reg_offs->pend, SUNXI_NMI_IRQ_BIT);

	irq_set_chained_handler_and_data(irq, sunxi_sc_nmi_handle_irq, domain);

	return 0;

fail_irqd_remove:
	irq_domain_remove(domain);

	return ret;
}

static int __init sun6i_r_intc_irq_init(struct device_node *node,
					struct device_node *parent)
{
	return sunxi_sc_nmi_irq_init(node, &sun6i_r_intc_reg_offs);
}
IRQCHIP_DECLARE(sun6i_r_intc, "allwinner,sun6i-a31-r-intc",
		sun6i_r_intc_irq_init);

static int __init sun6i_sc_nmi_irq_init(struct device_node *node,
					struct device_node *parent)
{
	return sunxi_sc_nmi_irq_init(node, &sun6i_reg_offs);
}
IRQCHIP_DECLARE(sun6i_sc_nmi, "allwinner,sun6i-a31-sc-nmi", sun6i_sc_nmi_irq_init);

static int __init sun7i_sc_nmi_irq_init(struct device_node *node,
					struct device_node *parent)
{
	return sunxi_sc_nmi_irq_init(node, &sun7i_reg_offs);
}
IRQCHIP_DECLARE(sun7i_sc_nmi, "allwinner,sun7i-a20-sc-nmi", sun7i_sc_nmi_irq_init);

static int __init sun9i_nmi_irq_init(struct device_node *node,
				     struct device_node *parent)
{
	return sunxi_sc_nmi_irq_init(node, &sun9i_reg_offs);
}
IRQCHIP_DECLARE(sun9i_nmi, "allwinner,sun9i-a80-nmi", sun9i_nmi_irq_init);
