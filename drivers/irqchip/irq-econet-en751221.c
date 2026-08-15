// SPDX-License-Identifier: GPL-2.0-only
/*
 * EN751221 Interrupt Controller Driver.
 *
 * The EcoNet EN751221 Interrupt Controller is a simple interrupt controller
 * designed for the MIPS 34Kc MT SMP processor with 2 VPEs. Each interrupt can
 * be routed to either VPE but not both, so to support per-CPU interrupts, a
 * secondary IRQ number is allocated to control masking/unmasking on VPE#1. In
 * this driver, these are called "shadow interrupts". The assignment of shadow
 * interrupts is defined by the SoC integrator when wiring the interrupt lines,
 * so they are configurable in the device tree.
 *
 * If an interrupt (say 30) needs per-CPU capability, the SoC integrator
 * allocates another IRQ number (say 29) to be its shadow. The device tree
 * reflects this by adding the pair <30 29> to the "econet,shadow-interrupts"
 * property.
 *
 * When VPE#1 requests IRQ 30, the driver manipulates the mask bit for IRQ 29,
 * telling the hardware to mask VPE#1's view of IRQ 30.
 *
 * Copyright (C) 2025 Caleb James DeLisle <cjd@cjdns.fr>
 */

#include <linux/cleanup.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>

#include <asm/setup.h>

#define IRQ_COUNT		40

#define NOT_PERCPU		0xff
#define IS_SHADOW		0xfe

#define REG_MASK0		0x04
#define REG_MASK1		0x50
#define REG_PENDING0		0x08
#define REG_PENDING1		0x54

/**
 * @membase:		Base address of the interrupt controller registers
 * @domain:		The irq_domain for direct dispatch
 * @ipi_domain:		The irq_domain for inter-process dispatch
 * @interrupt_shadows:	Array of all interrupts, for each value,
 *	- NOT_PERCPU:	This interrupt is not per-cpu, so it has no shadow
 *	- IS_SHADOW:	This interrupt is a shadow of another per-cpu interrupt
 *	- else:		This is a per-cpu interrupt whose shadow is the value
 */
static struct {
	void __iomem		*membase;
	struct irq_domain	*domain;
	struct irq_domain	*ipi_domain;
	u8			interrupt_shadows[IRQ_COUNT];
} econet_intc __ro_after_init;

static DEFINE_RAW_SPINLOCK(irq_lock);

/* IRQs must be disabled */
static void econet_wreg(u32 reg, u32 val, u32 mask)
{
	u32 v;

	guard(raw_spinlock)(&irq_lock);

	v = ioread32(econet_intc.membase + reg);
	v &= ~mask;
	v |= val & mask;
	iowrite32(v, econet_intc.membase + reg);
}

/* IRQs must be disabled */
static void econet_chmask(u32 hwirq, bool unmask)
{
	u32 reg, mask;
	u8 shadow;

	/*
	 * If the IRQ is a shadow, it should never be manipulated directly.
	 * It should only be masked/unmasked as a result of the "real" per-cpu
	 * irq being manipulated by a thread running on VPE#1.
	 * If it is per-cpu (has a shadow), and we're on VPE#1, the shadow is what we mask.
	 * This is single processor only, so smp_processor_id() never exceeds 1.
	 */
	shadow = econet_intc.interrupt_shadows[hwirq];
	if (WARN_ON_ONCE(shadow == IS_SHADOW))
		return;
	else if (shadow != NOT_PERCPU && smp_processor_id() == 1)
		hwirq = shadow;

	if (hwirq >= 32) {
		reg = REG_MASK1;
		mask = BIT(hwirq - 32);
	} else {
		reg = REG_MASK0;
		mask = BIT(hwirq);
	}

	econet_wreg(reg, unmask ? mask : 0, mask);
}

/* IRQs must be disabled */
static void econet_intc_mask(struct irq_data *d)
{
	econet_chmask(d->hwirq, false);
}

/* IRQs must be disabled */
static void econet_intc_unmask(struct irq_data *d)
{
	econet_chmask(d->hwirq, true);
}

static void econet_mask_all(void)
{
	/* IRQs are generally disabled during init, but guarding here makes it non-obligatory. */
	guard(irqsave)();
	econet_wreg(REG_MASK0, 0, ~0);
	econet_wreg(REG_MASK1, 0, ~0);
}

static void econet_intc_handle_pending(struct irq_domain *d, u32 pending, u32 offset)
{
	int hwirq;

	while (pending) {
		hwirq = fls(pending) - 1;
		generic_handle_domain_irq(d, hwirq + offset);
		pending &= ~BIT(hwirq);
	}
}

static void econet_intc_from_parent(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_domain *domain;
	u32 pending0, pending1;

	chained_irq_enter(chip, desc);

	pending0 = ioread32(econet_intc.membase + REG_PENDING0);
	pending1 = ioread32(econet_intc.membase + REG_PENDING1);

	if (unlikely(!(pending0 | pending1))) {
		spurious_interrupt();
	} else {
		domain = irq_desc_get_handler_data(desc);
		econet_intc_handle_pending(domain, pending0, 0);
		econet_intc_handle_pending(domain, pending1, 32);
	}

	chained_irq_exit(chip, desc);
}

/*
 * When in VEIC mode, the CPU jumps to a handler in the vector table.
 * The only way to know which interrupt is being triggered is from the vector table offset that
 * has been jumped to. Reading REG_PENDING(0|1) will tell you which interrupts are currently
 * pending in the intc, but that will not tell you which one the intc wants you to process
 * right now. And if you are not processing the exact interrupt that the intc wants you to be
 * processing, you might be on the wrong VPE. You can't tell which VPE any given REG_PENDING
 * interrupt is intended for (shadow IRQ numbers are for masking only, they never flag as
 * pending).
 *
 * Consequently, this little ritual of generating n handler functions and registering one per
 * interrupt is unavoidable.
 */
#define X(irq) \
	static void econet_irq_dispatch ## irq (void) \
	{ \
		do_domain_IRQ(econet_intc.domain, irq); \
	}

 X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  X(8)  X(9)
X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19)
X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29)
X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39)

#undef X
#define X(irq) econet_irq_dispatch ## irq,

static void (* const econet_irq_dispatchers[])(void) = {
	X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  X(8)  X(9)
	X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19)
	X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29)
	X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39)
};

/* Likewise, we do the same for the 2 IPI IRQs so that we can route them back */
static void econet_cpu_dispatch0(void)
{
	do_domain_IRQ(econet_intc.ipi_domain, 0);
}

static void econet_cpu_dispatch1(void)
{
	do_domain_IRQ(econet_intc.ipi_domain, 1);
}

static void (* const econet_cpu_dispatchers[])(void) = {
	econet_cpu_dispatch0,
	econet_cpu_dispatch1,
};

static const struct irq_chip econet_irq_chip;

static int econet_intc_map(struct irq_domain *d, u32 irq, irq_hw_number_t hwirq)
{
	int ret;

	if (hwirq >= IRQ_COUNT) {
		pr_err("%s: hwirq %lu out of range\n", __func__, hwirq);
		return -EINVAL;
	} else if (econet_intc.interrupt_shadows[hwirq] == IS_SHADOW) {
		pr_err("%s: can't map hwirq %lu, it is a shadow interrupt\n", __func__, hwirq);
		return -EINVAL;
	}

	if (econet_intc.interrupt_shadows[hwirq] == NOT_PERCPU) {
		irq_set_chip_and_handler(irq, &econet_irq_chip, handle_level_irq);
	} else {
		irq_set_chip_and_handler(irq, &econet_irq_chip, handle_percpu_devid_irq);
		ret = irq_set_percpu_devid(irq);
		if (ret)
			pr_warn("%s: Failed irq_set_percpu_devid for %u: %d\n", d->name, irq, ret);
	}

	irq_set_chip_data(irq, NULL);

	if (cpu_has_veic)
		set_vi_handler(hwirq + 1, econet_irq_dispatchers[hwirq]);

	return 0;
}

static const struct irq_chip econet_irq_chip = {
	.name		= "en751221-intc",
	.irq_unmask	= econet_intc_unmask,
	.irq_mask	= econet_intc_mask,
	.irq_mask_ack	= econet_intc_mask,
};

static const struct irq_domain_ops econet_domain_ops = {
	.xlate	= irq_domain_xlate_onecell,
	.map	= econet_intc_map
};

static int __init get_shadow_interrupts(struct device_node *node)
{
	const char *field = "econet,shadow-interrupts";
	int num_shadows;

	num_shadows = of_property_count_u32_elems(node, field);

	memset(econet_intc.interrupt_shadows, NOT_PERCPU,
	       sizeof(econet_intc.interrupt_shadows));

	if (num_shadows <= 0) {
		return 0;
	} else if (num_shadows % 2) {
		pr_err("%pOF: %s count is odd, ignoring\n", node, field);
		return 0;
	}

	u32 *shadows __free(kfree) = kmalloc_array(num_shadows, sizeof(u32), GFP_KERNEL);
	if (!shadows)
		return -ENOMEM;

	if (of_property_read_u32_array(node, field, shadows, num_shadows)) {
		pr_err("%pOF: Failed to read %s\n", node, field);
		return -EINVAL;
	}

	for (int i = 0; i < num_shadows; i += 2) {
		u32 shadow = shadows[i + 1];
		u32 target = shadows[i];

		if (shadow > IRQ_COUNT) {
			pr_err("%pOF: %s[%d] shadow(%d) out of range\n",
			       node, field, i + 1, shadow);
			continue;
		}

		if (target >= IRQ_COUNT) {
			pr_err("%pOF: %s[%d] target(%d) out of range\n", node, field, i, target);
			continue;
		}

		if (econet_intc.interrupt_shadows[target] != NOT_PERCPU) {
			pr_err("%pOF: %s[%d] target(%d) already has a shadow\n",
			       node, field, i, target);
			continue;
		}

		if (econet_intc.interrupt_shadows[shadow] != NOT_PERCPU) {
			pr_err("%pOF: %s[%d] shadow(%d) already has a target\n",
			       node, field, i + 1, shadow);
			continue;
		}

		econet_intc.interrupt_shadows[target] = shadow;
		econet_intc.interrupt_shadows[shadow] = IS_SHADOW;
	}

	return 0;
}

/**
 * econet_cpu_init() - configure routing of CPU interrupts to the correct domain.
 * @node: The devicetree node of this interrupt controller.
 *
 * Interrupts that originate from the CPU are unconditionally unmasked here and are re-routed back
 * to the IPI irq_domain in the CPU intc. Masking still takes place but the CPU intc is in charge
 * of it, using the mask bits of the c0_status register.
 *
 * Note that because IP2 ... IP7 are repurposed as Interrupt Priority Level, only the two IPI
 * interrupts are actually supported.
 */
static int __init econet_cpu_init(struct device_node *node)
{
	const char *field = "econet,cpu-interrupt-map";
	struct device_node *parent_intc;
	int map_size;
	u32 mask;

	map_size = of_property_count_u32_elems(node, field);

	if (map_size <= 0) {
		return 0;
	} else if (map_size % 2) {
		pr_err("%pOF: %s count is odd, ignoring\n", node, field);
		return 0;
	}

	u32 *maps __free(kfree) = kmalloc_array(map_size, sizeof(u32), GFP_KERNEL);
	if (!maps)
		return -ENOMEM;

	if (of_property_read_u32_array(node, field, maps, map_size)) {
		pr_err("%pOF: Failed to read %s\n", node, field);
		return -EINVAL;
	}

	/* Validation */
	for (int i = 0; i < map_size; i += 2) {
		u32 receive = maps[i];
		u32 dispatch = maps[i + 1];
		u8 shadow;

		if (receive >= IRQ_COUNT) {
			pr_err("%pOF: Entry %d:%d in %s (%u) is out of bounds\n",
			       node, i, 0, field, receive);
			return -EINVAL;
		}

		shadow = econet_intc.interrupt_shadows[receive];
		if (shadow != NOT_PERCPU && shadow >= IRQ_COUNT) {
			pr_err("%pOF: Entry %d:%d in %s (%u) has invalid shadow (%d)\n",
			       node, i, 0, field, receive, shadow);
			return -EINVAL;
		}

		if (dispatch >= ARRAY_SIZE(econet_cpu_dispatchers)) {
			pr_err("%pOF: Entry %d:%d in %s (%u) is out of bounds only IPI interrupts are supported\n",
			       node, i, 1, field, dispatch);
			return -EINVAL;
		}
	}

	parent_intc = of_irq_find_parent(node);
	if (!parent_intc) {
		pr_err("%pOF: Failed to find parent %s\n", node, "IRQ device");
		return -ENODEV;
	}

	econet_intc.ipi_domain = irq_find_matching_host(parent_intc, DOMAIN_BUS_IPI);
	if (!econet_intc.ipi_domain) {
		pr_err("%pOF: Failed to find parent %s\n", node, "IPI domain");
		return -ENODEV;
	}

	mask = 0;
	for (int i = 0; i < map_size; i += 2) {
		u32 receive = maps[i];
		u32 dispatch = maps[i + 1];
		u8 shadow;

		set_vi_handler(receive + 1, econet_cpu_dispatchers[dispatch]);

		mask |= BIT(receive);

		shadow = econet_intc.interrupt_shadows[receive];
		if (shadow != NOT_PERCPU)
			mask |= BIT(shadow);
	}

	econet_wreg(REG_MASK0, mask, mask);

	return 0;
}

static int __init econet_intc_of_init(struct device_node *node, struct device_node *parent)
{
	struct irq_domain *domain;
	struct resource res;
	int ret, irq;

	ret = get_shadow_interrupts(node);
	if (ret)
		return ret;

	irq = irq_of_parse_and_map(node, 0);
	if (!irq) {
		pr_err("%pOF: DT: Failed to get IRQ from 'interrupts'\n", node);
		return -EINVAL;
	}

	if (of_address_to_resource(node, 0, &res)) {
		pr_err("%pOF: DT: Failed to get 'reg'\n", node);
		ret = -EINVAL;
		goto err_dispose_mapping;
	}

	if (!request_mem_region(res.start, resource_size(&res), res.name)) {
		pr_err("%pOF: Failed to request memory\n", node);
		ret = -EBUSY;
		goto err_dispose_mapping;
	}

	econet_intc.membase = ioremap(res.start, resource_size(&res));
	if (!econet_intc.membase) {
		pr_err("%pOF: Failed to remap membase\n", node);
		ret = -ENOMEM;
		goto err_release;
	}

	econet_mask_all();

	domain = irq_domain_create_linear(of_fwnode_handle(node), IRQ_COUNT,
					  &econet_domain_ops, NULL);
	if (!domain) {
		pr_err("%pOF: Failed to add irqdomain\n", node);
		ret = -ENOMEM;
		goto err_unmap;
	}

	/*
	 * 34K Manual (MD00534) Section 6.3.1.3 rev 1.13 page 136:
	 * In VEIC mode, IP2 ... IP7 are repurposed as Interrupt Priority Level. The controller
	 * will filter incoming interrupts whose priority is lower than the IPL number. Therefore
	 * we must not set any of these bits. We avoid setting IP2 by not actually chaining this
	 * intc to the CPU intc.
	 */
	if (cpu_has_veic) {
		ret = econet_cpu_init(node);

		if (ret)
			return ret;
	} else {
		irq_set_chained_handler_and_data(irq, econet_intc_from_parent, domain);
	}

	econet_intc.domain = domain;

	return 0;

err_unmap:
	iounmap(econet_intc.membase);
err_release:
	release_mem_region(res.start, resource_size(&res));
err_dispose_mapping:
	irq_dispose_mapping(irq);
	return ret;
}

IRQCHIP_DECLARE(econet_en751221_intc, "econet,en751221-intc", econet_intc_of_init);
