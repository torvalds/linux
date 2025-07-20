// SPDX-License-Identifier: GPL-2.0-only
/*
 * The R_INTC in Allwinner A31 and newer SoCs manages several types of
 * interrupts, as shown below:
 *
 *             NMI IRQ                DIRECT IRQs           MUXED IRQs
 *              bit 0                  bits 1-15^           bits 19-31
 *
 *   +---------+                      +---------+    +---------+  +---------+
 *   | NMI Pad |                      |  IRQ d  |    |  IRQ m  |  | IRQ m+7 |
 *   +---------+                      +---------+    +---------+  +---------+
 *        |                             |     |         |    |      |    |
 *        |                             |     |         |    |......|    |
 * +------V------+ +------------+       |     |         | +--V------V--+ |
 * |   Invert/   | | Write 1 to |       |     |         | |  AND with  | |
 * | Edge Detect | | PENDING[0] |       |     |         | |  MUX[m/8]  | |
 * +-------------+ +------------+       |     |         | +------------+ |
 *            |       |                 |     |         |       |        |
 *         +--V-------V--+           +--V--+  |      +--V--+    |     +--V--+
 *         | Set    Reset|           | GIC |  |      | GIC |    |     | GIC |
 *         |    Latch    |           | SPI |  |      | SPI |... |  ...| SPI |
 *         +-------------+           | N+d |  |      |  m  |    |     | m+7 |
 *             |     |               +-----+  |      +-----+    |     +-----+
 *             |     |                        |                 |
 *     +-------V-+ +-V----------+   +---------V--+     +--------V--------+
 *     | GIC SPI | |  AND with  |   |  AND with  |     |    AND with     |
 *     | N (=32) | |  ENABLE[0] |   |  ENABLE[d] |     |  ENABLE[19+m/8] |
 *     +---------+ +------------+   +------------+     +-----------------+
 *                        |                |                    |
 *                 +------V-----+   +------V-----+     +--------V--------+
 *                 |    Read    |   |    Read    |     |     Read        |
 *                 | PENDING[0] |   | PENDING[d] |     | PENDING[19+m/8] |
 *                 +------------+   +------------+     +-----------------+
 *
 * ^ bits 16-18 are direct IRQs for peripherals with banked interrupts, such as
 *   the MSGBOX. These IRQs do not map to any GIC SPI.
 *
 * The H6 variant adds two more (banked) direct IRQs and implements the full
 * set of 128 mux bits. This requires a second set of top-level registers.
 */

#include <linux/bitmap.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/interrupt-controller/arm-gic.h>

#define SUN6I_NMI_CTRL			(0x0c)
#define SUN6I_IRQ_PENDING(n)		(0x10 + 4 * (n))
#define SUN6I_IRQ_ENABLE(n)		(0x40 + 4 * (n))
#define SUN6I_MUX_ENABLE(n)		(0xc0 + 4 * (n))

#define SUN6I_NMI_SRC_TYPE_LEVEL_LOW	0
#define SUN6I_NMI_SRC_TYPE_EDGE_FALLING	1
#define SUN6I_NMI_SRC_TYPE_LEVEL_HIGH	2
#define SUN6I_NMI_SRC_TYPE_EDGE_RISING	3

#define SUN6I_NMI_BIT			BIT(0)

#define SUN6I_NMI_NEEDS_ACK		((void *)1)

#define SUN6I_NR_TOP_LEVEL_IRQS		64
#define SUN6I_NR_DIRECT_IRQS		16
#define SUN6I_NR_MUX_BITS		128

struct sun6i_r_intc_variant {
	u32		first_mux_irq;
	u32		nr_mux_irqs;
	u32		mux_valid[BITS_TO_U32(SUN6I_NR_MUX_BITS)];
};

static void __iomem *base;
static irq_hw_number_t nmi_hwirq;
static DECLARE_BITMAP(wake_irq_enabled, SUN6I_NR_TOP_LEVEL_IRQS);
static DECLARE_BITMAP(wake_mux_enabled, SUN6I_NR_MUX_BITS);
static DECLARE_BITMAP(wake_mux_valid, SUN6I_NR_MUX_BITS);

static void sun6i_r_intc_ack_nmi(void)
{
	writel_relaxed(SUN6I_NMI_BIT, base + SUN6I_IRQ_PENDING(0));
}

static void sun6i_r_intc_nmi_ack(struct irq_data *data)
{
	if (irqd_get_trigger_type(data) & IRQ_TYPE_EDGE_BOTH)
		sun6i_r_intc_ack_nmi();
	else
		data->chip_data = SUN6I_NMI_NEEDS_ACK;
}

static void sun6i_r_intc_nmi_eoi(struct irq_data *data)
{
	/* For oneshot IRQs, delay the ack until the IRQ is unmasked. */
	if (data->chip_data == SUN6I_NMI_NEEDS_ACK && !irqd_irq_masked(data)) {
		data->chip_data = NULL;
		sun6i_r_intc_ack_nmi();
	}

	irq_chip_eoi_parent(data);
}

static void sun6i_r_intc_nmi_unmask(struct irq_data *data)
{
	if (data->chip_data == SUN6I_NMI_NEEDS_ACK) {
		data->chip_data = NULL;
		sun6i_r_intc_ack_nmi();
	}

	irq_chip_unmask_parent(data);
}

static int sun6i_r_intc_nmi_set_type(struct irq_data *data, unsigned int type)
{
	u32 nmi_src_type;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		nmi_src_type = SUN6I_NMI_SRC_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		nmi_src_type = SUN6I_NMI_SRC_TYPE_EDGE_FALLING;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		nmi_src_type = SUN6I_NMI_SRC_TYPE_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		nmi_src_type = SUN6I_NMI_SRC_TYPE_LEVEL_LOW;
		break;
	default:
		return -EINVAL;
	}

	writel_relaxed(nmi_src_type, base + SUN6I_NMI_CTRL);

	/*
	 * The "External NMI" GIC input connects to a latch inside R_INTC, not
	 * directly to the pin. So the GIC trigger type does not depend on the
	 * NMI pin trigger type.
	 */
	return irq_chip_set_type_parent(data, IRQ_TYPE_LEVEL_HIGH);
}

static int sun6i_r_intc_nmi_set_irqchip_state(struct irq_data *data,
					      enum irqchip_irq_state which,
					      bool state)
{
	if (which == IRQCHIP_STATE_PENDING && !state)
		sun6i_r_intc_ack_nmi();

	return irq_chip_set_parent_state(data, which, state);
}

static int sun6i_r_intc_irq_set_wake(struct irq_data *data, unsigned int on)
{
	unsigned long offset_from_nmi = data->hwirq - nmi_hwirq;

	if (offset_from_nmi < SUN6I_NR_DIRECT_IRQS)
		assign_bit(offset_from_nmi, wake_irq_enabled, on);
	else if (test_bit(data->hwirq, wake_mux_valid))
		assign_bit(data->hwirq, wake_mux_enabled, on);
	else
		/* Not wakeup capable. */
		return -EPERM;

	return 0;
}

static struct irq_chip sun6i_r_intc_nmi_chip = {
	.name			= "sun6i-r-intc",
	.irq_ack		= sun6i_r_intc_nmi_ack,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= sun6i_r_intc_nmi_unmask,
	.irq_eoi		= sun6i_r_intc_nmi_eoi,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_set_type		= sun6i_r_intc_nmi_set_type,
	.irq_set_irqchip_state	= sun6i_r_intc_nmi_set_irqchip_state,
	.irq_set_wake		= sun6i_r_intc_irq_set_wake,
	.flags			= IRQCHIP_SET_TYPE_MASKED,
};

static struct irq_chip sun6i_r_intc_wakeup_chip = {
	.name			= "sun6i-r-intc",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_set_wake		= sun6i_r_intc_irq_set_wake,
	.flags			= IRQCHIP_SET_TYPE_MASKED,
};

static int sun6i_r_intc_domain_translate(struct irq_domain *domain,
					 struct irq_fwspec *fwspec,
					 unsigned long *hwirq,
					 unsigned int *type)
{
	/* Accept the old two-cell binding for the NMI only. */
	if (fwspec->param_count == 2 && fwspec->param[0] == 0) {
		*hwirq = nmi_hwirq;
		*type  = fwspec->param[1] & IRQ_TYPE_SENSE_MASK;
		return 0;
	}

	/* Otherwise this binding should match the GIC SPI binding. */
	if (fwspec->param_count < 3)
		return -EINVAL;
	if (fwspec->param[0] != GIC_SPI)
		return -EINVAL;

	*hwirq = fwspec->param[1];
	*type  = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static int sun6i_r_intc_domain_alloc(struct irq_domain *domain,
				     unsigned int virq,
				     unsigned int nr_irqs, void *arg)
{
	struct irq_fwspec *fwspec = arg;
	struct irq_fwspec gic_fwspec;
	unsigned long hwirq;
	unsigned int type;
	int i, ret;

	ret = sun6i_r_intc_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;
	if (hwirq + nr_irqs > SUN6I_NR_MUX_BITS)
		return -EINVAL;

	/* Construct a GIC-compatible fwspec from this fwspec. */
	gic_fwspec = (struct irq_fwspec) {
		.fwnode      = domain->parent->fwnode,
		.param_count = 3,
		.param       = { GIC_SPI, hwirq, type },
	};

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &gic_fwspec);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; ++i, ++hwirq, ++virq) {
		if (hwirq == nmi_hwirq) {
			irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
						      &sun6i_r_intc_nmi_chip,
						      NULL);
			irq_set_handler(virq, handle_fasteoi_ack_irq);
		} else {
			irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
						      &sun6i_r_intc_wakeup_chip,
						      NULL);
		}
	}

	return 0;
}

static const struct irq_domain_ops sun6i_r_intc_domain_ops = {
	.translate	= sun6i_r_intc_domain_translate,
	.alloc		= sun6i_r_intc_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static int sun6i_r_intc_suspend(void)
{
	u32 buf[BITS_TO_U32(MAX(SUN6I_NR_TOP_LEVEL_IRQS, SUN6I_NR_MUX_BITS))];
	int i;

	/* Wake IRQs are enabled during system sleep and shutdown. */
	bitmap_to_arr32(buf, wake_irq_enabled, SUN6I_NR_TOP_LEVEL_IRQS);
	for (i = 0; i < BITS_TO_U32(SUN6I_NR_TOP_LEVEL_IRQS); ++i)
		writel_relaxed(buf[i], base + SUN6I_IRQ_ENABLE(i));
	bitmap_to_arr32(buf, wake_mux_enabled, SUN6I_NR_MUX_BITS);
	for (i = 0; i < BITS_TO_U32(SUN6I_NR_MUX_BITS); ++i)
		writel_relaxed(buf[i], base + SUN6I_MUX_ENABLE(i));

	return 0;
}

static void sun6i_r_intc_resume(void)
{
	int i;

	/* Only the NMI is relevant during normal operation. */
	writel_relaxed(SUN6I_NMI_BIT, base + SUN6I_IRQ_ENABLE(0));
	for (i = 1; i < BITS_TO_U32(SUN6I_NR_TOP_LEVEL_IRQS); ++i)
		writel_relaxed(0, base + SUN6I_IRQ_ENABLE(i));
}

static void sun6i_r_intc_shutdown(void)
{
	sun6i_r_intc_suspend();
}

static struct syscore_ops sun6i_r_intc_syscore_ops = {
	.suspend	= sun6i_r_intc_suspend,
	.resume		= sun6i_r_intc_resume,
	.shutdown	= sun6i_r_intc_shutdown,
};

static int __init sun6i_r_intc_init(struct device_node *node,
				    struct device_node *parent,
				    const struct sun6i_r_intc_variant *v)
{
	struct irq_domain *domain, *parent_domain;
	struct of_phandle_args nmi_parent;
	int ret;

	/* Extract the NMI hwirq number from the OF node. */
	ret = of_irq_parse_one(node, 0, &nmi_parent);
	if (ret)
		return ret;
	if (nmi_parent.args_count < 3 ||
	    nmi_parent.args[0] != GIC_SPI ||
	    nmi_parent.args[2] != IRQ_TYPE_LEVEL_HIGH)
		return -EINVAL;
	nmi_hwirq = nmi_parent.args[1];

	bitmap_set(wake_irq_enabled, v->first_mux_irq, v->nr_mux_irqs);
	bitmap_from_arr32(wake_mux_valid, v->mux_valid, SUN6I_NR_MUX_BITS);

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("%pOF: Failed to obtain parent domain\n", node);
		return -ENXIO;
	}

	base = of_io_request_and_map(node, 0, NULL);
	if (IS_ERR(base)) {
		pr_err("%pOF: Failed to map MMIO region\n", node);
		return PTR_ERR(base);
	}

	domain = irq_domain_create_hierarchy(parent_domain, 0, 0, of_fwnode_handle(node),
					     &sun6i_r_intc_domain_ops, NULL);
	if (!domain) {
		pr_err("%pOF: Failed to allocate domain\n", node);
		iounmap(base);
		return -ENOMEM;
	}

	register_syscore_ops(&sun6i_r_intc_syscore_ops);

	sun6i_r_intc_ack_nmi();
	sun6i_r_intc_resume();

	return 0;
}

static const struct sun6i_r_intc_variant sun6i_a31_r_intc_variant __initconst = {
	.first_mux_irq	= 19,
	.nr_mux_irqs	= 13,
	.mux_valid	= { 0xffffffff, 0xfff80000, 0xffffffff, 0x0000000f },
};

static int __init sun6i_a31_r_intc_init(struct device_node *node,
					struct device_node *parent)
{
	return sun6i_r_intc_init(node, parent, &sun6i_a31_r_intc_variant);
}
IRQCHIP_DECLARE(sun6i_a31_r_intc, "allwinner,sun6i-a31-r-intc", sun6i_a31_r_intc_init);

static const struct sun6i_r_intc_variant sun50i_h6_r_intc_variant __initconst = {
	.first_mux_irq	= 21,
	.nr_mux_irqs	= 16,
	.mux_valid	= { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff },
};

static int __init sun50i_h6_r_intc_init(struct device_node *node,
					struct device_node *parent)
{
	return sun6i_r_intc_init(node, parent, &sun50i_h6_r_intc_variant);
}
IRQCHIP_DECLARE(sun50i_h6_r_intc, "allwinner,sun50i-h6-r-intc", sun50i_h6_r_intc_init);
