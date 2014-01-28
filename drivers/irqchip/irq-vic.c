/*
 *  linux/arch/arm/common/vic.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/syscore_ops.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/irqchip/arm-vic.h>

#include <asm/exception.h>
#include <asm/irq.h>

#include "irqchip.h"

#define VIC_IRQ_STATUS			0x00
#define VIC_FIQ_STATUS			0x04
#define VIC_INT_SELECT			0x0c	/* 1 = FIQ, 0 = IRQ */
#define VIC_INT_SOFT			0x18
#define VIC_INT_SOFT_CLEAR		0x1c
#define VIC_PROTECT			0x20
#define VIC_PL190_VECT_ADDR		0x30	/* PL190 only */
#define VIC_PL190_DEF_VECT_ADDR		0x34	/* PL190 only */

#define VIC_VECT_ADDR0			0x100	/* 0 to 15 (0..31 PL192) */
#define VIC_VECT_CNTL0			0x200	/* 0 to 15 (0..31 PL192) */
#define VIC_ITCR			0x300	/* VIC test control register */

#define VIC_VECT_CNTL_ENABLE		(1 << 5)

#define VIC_PL192_VECT_ADDR		0xF00

/**
 * struct vic_device - VIC PM device
 * @irq: The IRQ number for the base of the VIC.
 * @base: The register base for the VIC.
 * @valid_sources: A bitmask of valid interrupts
 * @resume_sources: A bitmask of interrupts for resume.
 * @resume_irqs: The IRQs enabled for resume.
 * @int_select: Save for VIC_INT_SELECT.
 * @int_enable: Save for VIC_INT_ENABLE.
 * @soft_int: Save for VIC_INT_SOFT.
 * @protect: Save for VIC_PROTECT.
 * @domain: The IRQ domain for the VIC.
 */
struct vic_device {
	void __iomem	*base;
	int		irq;
	u32		valid_sources;
	u32		resume_sources;
	u32		resume_irqs;
	u32		int_select;
	u32		int_enable;
	u32		soft_int;
	u32		protect;
	struct irq_domain *domain;
};

/* we cannot allocate memory when VICs are initially registered */
static struct vic_device vic_devices[CONFIG_ARM_VIC_NR];

static int vic_id;

static void vic_handle_irq(struct pt_regs *regs);

/**
 * vic_init2 - common initialisation code
 * @base: Base of the VIC.
 *
 * Common initialisation code for registration
 * and resume.
*/
static void vic_init2(void __iomem *base)
{
	int i;

	for (i = 0; i < 16; i++) {
		void __iomem *reg = base + VIC_VECT_CNTL0 + (i * 4);
		writel(VIC_VECT_CNTL_ENABLE | i, reg);
	}

	writel(32, base + VIC_PL190_DEF_VECT_ADDR);
}

#ifdef CONFIG_PM
static void resume_one_vic(struct vic_device *vic)
{
	void __iomem *base = vic->base;

	printk(KERN_DEBUG "%s: resuming vic at %p\n", __func__, base);

	/* re-initialise static settings */
	vic_init2(base);

	writel(vic->int_select, base + VIC_INT_SELECT);
	writel(vic->protect, base + VIC_PROTECT);

	/* set the enabled ints and then clear the non-enabled */
	writel(vic->int_enable, base + VIC_INT_ENABLE);
	writel(~vic->int_enable, base + VIC_INT_ENABLE_CLEAR);

	/* and the same for the soft-int register */

	writel(vic->soft_int, base + VIC_INT_SOFT);
	writel(~vic->soft_int, base + VIC_INT_SOFT_CLEAR);
}

static void vic_resume(void)
{
	int id;

	for (id = vic_id - 1; id >= 0; id--)
		resume_one_vic(vic_devices + id);
}

static void suspend_one_vic(struct vic_device *vic)
{
	void __iomem *base = vic->base;

	printk(KERN_DEBUG "%s: suspending vic at %p\n", __func__, base);

	vic->int_select = readl(base + VIC_INT_SELECT);
	vic->int_enable = readl(base + VIC_INT_ENABLE);
	vic->soft_int = readl(base + VIC_INT_SOFT);
	vic->protect = readl(base + VIC_PROTECT);

	/* set the interrupts (if any) that are used for
	 * resuming the system */

	writel(vic->resume_irqs, base + VIC_INT_ENABLE);
	writel(~vic->resume_irqs, base + VIC_INT_ENABLE_CLEAR);
}

static int vic_suspend(void)
{
	int id;

	for (id = 0; id < vic_id; id++)
		suspend_one_vic(vic_devices + id);

	return 0;
}

struct syscore_ops vic_syscore_ops = {
	.suspend	= vic_suspend,
	.resume		= vic_resume,
};

/**
 * vic_pm_init - initicall to register VIC pm
 *
 * This is called via late_initcall() to register
 * the resources for the VICs due to the early
 * nature of the VIC's registration.
*/
static int __init vic_pm_init(void)
{
	if (vic_id > 0)
		register_syscore_ops(&vic_syscore_ops);

	return 0;
}
late_initcall(vic_pm_init);
#endif /* CONFIG_PM */

static struct irq_chip vic_chip;

static int vic_irqdomain_map(struct irq_domain *d, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	struct vic_device *v = d->host_data;

	/* Skip invalid IRQs, only register handlers for the real ones */
	if (!(v->valid_sources & (1 << hwirq)))
		return -EPERM;
	irq_set_chip_and_handler(irq, &vic_chip, handle_level_irq);
	irq_set_chip_data(irq, v->base);
	set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	return 0;
}

/*
 * Handle each interrupt in a single VIC.  Returns non-zero if we've
 * handled at least one interrupt.  This reads the status register
 * before handling each interrupt, which is necessary given that
 * handle_IRQ may briefly re-enable interrupts for soft IRQ handling.
 */
static int handle_one_vic(struct vic_device *vic, struct pt_regs *regs)
{
	u32 stat, irq;
	int handled = 0;

	while ((stat = readl_relaxed(vic->base + VIC_IRQ_STATUS))) {
		irq = ffs(stat) - 1;
		handle_IRQ(irq_find_mapping(vic->domain, irq), regs);
		handled = 1;
	}

	return handled;
}

/*
 * Keep iterating over all registered VIC's until there are no pending
 * interrupts.
 */
static asmlinkage void __exception_irq_entry vic_handle_irq(struct pt_regs *regs)
{
	int i, handled;

	do {
		for (i = 0, handled = 0; i < vic_id; ++i)
			handled |= handle_one_vic(&vic_devices[i], regs);
	} while (handled);
}

static struct irq_domain_ops vic_irqdomain_ops = {
	.map = vic_irqdomain_map,
	.xlate = irq_domain_xlate_onetwocell,
};

/**
 * vic_register() - Register a VIC.
 * @base: The base address of the VIC.
 * @irq: The base IRQ for the VIC.
 * @valid_sources: bitmask of valid interrupts
 * @resume_sources: bitmask of interrupts allowed for resume sources.
 * @node: The device tree node associated with the VIC.
 *
 * Register the VIC with the system device tree so that it can be notified
 * of suspend and resume requests and ensure that the correct actions are
 * taken to re-instate the settings on resume.
 *
 * This also configures the IRQ domain for the VIC.
 */
static void __init vic_register(void __iomem *base, unsigned int irq,
				u32 valid_sources, u32 resume_sources,
				struct device_node *node)
{
	struct vic_device *v;
	int i;

	if (vic_id >= ARRAY_SIZE(vic_devices)) {
		printk(KERN_ERR "%s: too few VICs, increase CONFIG_ARM_VIC_NR\n", __func__);
		return;
	}

	v = &vic_devices[vic_id];
	v->base = base;
	v->valid_sources = valid_sources;
	v->resume_sources = resume_sources;
	v->irq = irq;
	set_handle_irq(vic_handle_irq);
	vic_id++;
	v->domain = irq_domain_add_simple(node, fls(valid_sources), irq,
					  &vic_irqdomain_ops, v);
	/* create an IRQ mapping for each valid IRQ */
	for (i = 0; i < fls(valid_sources); i++)
		if (valid_sources & (1 << i))
			irq_create_mapping(v->domain, i);
}

static void vic_ack_irq(struct irq_data *d)
{
	void __iomem *base = irq_data_get_irq_chip_data(d);
	unsigned int irq = d->hwirq;
	writel(1 << irq, base + VIC_INT_ENABLE_CLEAR);
	/* moreover, clear the soft-triggered, in case it was the reason */
	writel(1 << irq, base + VIC_INT_SOFT_CLEAR);
}

static void vic_mask_irq(struct irq_data *d)
{
	void __iomem *base = irq_data_get_irq_chip_data(d);
	unsigned int irq = d->hwirq;
	writel(1 << irq, base + VIC_INT_ENABLE_CLEAR);
}

static void vic_unmask_irq(struct irq_data *d)
{
	void __iomem *base = irq_data_get_irq_chip_data(d);
	unsigned int irq = d->hwirq;
	writel(1 << irq, base + VIC_INT_ENABLE);
}

#if defined(CONFIG_PM)
static struct vic_device *vic_from_irq(unsigned int irq)
{
        struct vic_device *v = vic_devices;
	unsigned int base_irq = irq & ~31;
	int id;

	for (id = 0; id < vic_id; id++, v++) {
		if (v->irq == base_irq)
			return v;
	}

	return NULL;
}

static int vic_set_wake(struct irq_data *d, unsigned int on)
{
	struct vic_device *v = vic_from_irq(d->irq);
	unsigned int off = d->hwirq;
	u32 bit = 1 << off;

	if (!v)
		return -EINVAL;

	if (!(bit & v->resume_sources))
		return -EINVAL;

	if (on)
		v->resume_irqs |= bit;
	else
		v->resume_irqs &= ~bit;

	return 0;
}
#else
#define vic_set_wake NULL
#endif /* CONFIG_PM */

static struct irq_chip vic_chip = {
	.name		= "VIC",
	.irq_ack	= vic_ack_irq,
	.irq_mask	= vic_mask_irq,
	.irq_unmask	= vic_unmask_irq,
	.irq_set_wake	= vic_set_wake,
};

static void __init vic_disable(void __iomem *base)
{
	writel(0, base + VIC_INT_SELECT);
	writel(0, base + VIC_INT_ENABLE);
	writel(~0, base + VIC_INT_ENABLE_CLEAR);
	writel(0, base + VIC_ITCR);
	writel(~0, base + VIC_INT_SOFT_CLEAR);
}

static void __init vic_clear_interrupts(void __iomem *base)
{
	unsigned int i;

	writel(0, base + VIC_PL190_VECT_ADDR);
	for (i = 0; i < 19; i++) {
		unsigned int value;

		value = readl(base + VIC_PL190_VECT_ADDR);
		writel(value, base + VIC_PL190_VECT_ADDR);
	}
}

/*
 * The PL190 cell from ARM has been modified by ST to handle 64 interrupts.
 * The original cell has 32 interrupts, while the modified one has 64,
 * replocating two blocks 0x00..0x1f in 0x20..0x3f. In that case
 * the probe function is called twice, with base set to offset 000
 *  and 020 within the page. We call this "second block".
 */
static void __init vic_init_st(void __iomem *base, unsigned int irq_start,
			       u32 vic_sources, struct device_node *node)
{
	unsigned int i;
	int vic_2nd_block = ((unsigned long)base & ~PAGE_MASK) != 0;

	/* Disable all interrupts initially. */
	vic_disable(base);

	/*
	 * Make sure we clear all existing interrupts. The vector registers
	 * in this cell are after the second block of general registers,
	 * so we can address them using standard offsets, but only from
	 * the second base address, which is 0x20 in the page
	 */
	if (vic_2nd_block) {
		vic_clear_interrupts(base);

		/* ST has 16 vectors as well, but we don't enable them by now */
		for (i = 0; i < 16; i++) {
			void __iomem *reg = base + VIC_VECT_CNTL0 + (i * 4);
			writel(0, reg);
		}

		writel(32, base + VIC_PL190_DEF_VECT_ADDR);
	}

	vic_register(base, irq_start, vic_sources, 0, node);
}

void __init __vic_init(void __iomem *base, int irq_start,
			      u32 vic_sources, u32 resume_sources,
			      struct device_node *node)
{
	unsigned int i;
	u32 cellid = 0;
	enum amba_vendor vendor;

	/* Identify which VIC cell this one is, by reading the ID */
	for (i = 0; i < 4; i++) {
		void __iomem *addr;
		addr = (void __iomem *)((u32)base & PAGE_MASK) + 0xfe0 + (i * 4);
		cellid |= (readl(addr) & 0xff) << (8 * i);
	}
	vendor = (cellid >> 12) & 0xff;
	printk(KERN_INFO "VIC @%p: id 0x%08x, vendor 0x%02x\n",
	       base, cellid, vendor);

	switch(vendor) {
	case AMBA_VENDOR_ST:
		vic_init_st(base, irq_start, vic_sources, node);
		return;
	default:
		printk(KERN_WARNING "VIC: unknown vendor, continuing anyways\n");
		/* fall through */
	case AMBA_VENDOR_ARM:
		break;
	}

	/* Disable all interrupts initially. */
	vic_disable(base);

	/* Make sure we clear all existing interrupts */
	vic_clear_interrupts(base);

	vic_init2(base);

	vic_register(base, irq_start, vic_sources, resume_sources, node);
}

/**
 * vic_init() - initialise a vectored interrupt controller
 * @base: iomem base address
 * @irq_start: starting interrupt number, must be muliple of 32
 * @vic_sources: bitmask of interrupt sources to allow
 * @resume_sources: bitmask of interrupt sources to allow for resume
 */
void __init vic_init(void __iomem *base, unsigned int irq_start,
		     u32 vic_sources, u32 resume_sources)
{
	__vic_init(base, irq_start, vic_sources, resume_sources, NULL);
}

#ifdef CONFIG_OF
int __init vic_of_init(struct device_node *node, struct device_node *parent)
{
	void __iomem *regs;
	u32 interrupt_mask = ~0;
	u32 wakeup_mask = ~0;

	if (WARN(parent, "non-root VICs are not supported"))
		return -EINVAL;

	regs = of_iomap(node, 0);
	if (WARN_ON(!regs))
		return -EIO;

	of_property_read_u32(node, "valid-mask", &interrupt_mask);
	of_property_read_u32(node, "valid-wakeup-mask", &wakeup_mask);

	/*
	 * Passing 0 as first IRQ makes the simple domain allocate descriptors
	 */
	__vic_init(regs, 0, interrupt_mask, wakeup_mask, node);

	return 0;
}
IRQCHIP_DECLARE(arm_pl190_vic, "arm,pl190-vic", vic_of_init);
IRQCHIP_DECLARE(arm_pl192_vic, "arm,pl192-vic", vic_of_init);
IRQCHIP_DECLARE(arm_versatile_vic, "arm,versatile-vic", vic_of_init);
#endif /* CONFIG OF */
