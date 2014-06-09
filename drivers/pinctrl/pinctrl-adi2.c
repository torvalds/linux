/*
 * Pinctrl Driver for ADI GPIO2 controller
 *
 * Copyright 2007-2013 Analog Devices Inc.
 *
 * Licensed under the GPLv2 or later
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/irq.h>
#include <linux/platform_data/pinctrl-adi2.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/syscore_ops.h>
#include <linux/gpio.h>
#include <asm/portmux.h>
#include "pinctrl-adi2.h"
#include "core.h"

/*
According to the BF54x HRM, pint means "pin interrupt".
http://www.analog.com/static/imported-files/processor_manuals/ADSP-BF54x_hwr_rev1.2.pdf

ADSP-BF54x processor Blackfin processors have four SIC interrupt chan-
nels dedicated to pin interrupt purposes. These channels are managed by
four hardware blocks, called PINT0, PINT1, PINT2, and PINT3. Every PINTx
block can sense to up to 32 pins. While PINT0 and PINT1 can sense the
pins of port A and port B, PINT2 and PINT3 manage all the pins from port
C to port J as shown in Figure 9-2.

n BF54x HRM:
The ten GPIO ports are subdivided into 8-bit half ports, resulting in lower and
upper half 8-bit units. The PINTx_ASSIGN registers control the 8-bit multi-
plexers shown in Figure 9-3. Lower half units of eight pins can be
forwarded to either byte 0 or byte 2 of either associated PINTx block.
Upper half units can be forwarded to either byte 1 or byte 3 of the pin
interrupt blocks, without further restrictions.

All MMR registers in the pin interrupt module are 32 bits wide. To simply the
mapping logic, this driver only maps a 16-bit gpio port to the upper or lower
16 bits of a PINTx block. You can find the Figure 9-3 on page 583.

Each IRQ domain is binding to a GPIO bank device. 2 GPIO bank devices can map
to one PINT device. Two in "struct gpio_pint" are used to ease the PINT
interrupt handler.

The GPIO bank mapping to the lower 16 bits of the PINT device set its IRQ
domain pointer in domain[0]. The IRQ domain pointer of the other bank is set
to domain[1]. PINT interrupt handler adi_gpio_handle_pint_irq() finds out
the current domain pointer according to whether the interrupt request mask
is in lower 16 bits (domain[0]) or upper 16bits (domain[1]).

A PINT device is not part of a GPIO port device in Blackfin. Multiple GPIO
port devices can be mapped to the same PINT device.

*/

static LIST_HEAD(adi_pint_list);
static LIST_HEAD(adi_gpio_port_list);

#define DRIVER_NAME "pinctrl-adi2"

#define PINT_HI_OFFSET		16

/**
 * struct gpio_port_saved - GPIO port registers that should be saved between
 * power suspend and resume operations.
 *
 * @fer: PORTx_FER register
 * @data: PORTx_DATA register
 * @dir: PORTx_DIR register
 * @inen: PORTx_INEN register
 * @mux: PORTx_MUX register
 */
struct gpio_port_saved {
	u16 fer;
	u16 data;
	u16 dir;
	u16 inen;
	u32 mux;
};

/*
 * struct gpio_pint_saved - PINT registers saved in PM operations
 *
 * @assign: ASSIGN register
 * @edge_set: EDGE_SET register
 * @invert_set: INVERT_SET register
 */
struct gpio_pint_saved {
	u32 assign;
	u32 edge_set;
	u32 invert_set;
};

/**
 * struct gpio_pint - Pin interrupt controller device. Multiple ADI GPIO
 * banks can be mapped into one Pin interrupt controller.
 *
 * @node: All gpio_pint instances are added to a global list.
 * @base: PINT device register base address
 * @irq: IRQ of the PINT device, it is the parent IRQ of all
 *       GPIO IRQs mapping to this device.
 * @domain: [0] irq domain of the gpio port, whose hardware interrupts are
 *		mapping to the low 16-bit of the pint registers.
 *          [1] irq domain of the gpio port, whose hardware interrupts are
 *		mapping to the high 16-bit of the pint registers.
 * @regs: address pointer to the PINT device
 * @map_count: No more than 2 GPIO banks can be mapped to this PINT device.
 * @lock: This lock make sure the irq_chip operations to one PINT device
 *        for different GPIO interrrupts are atomic.
 * @pint_map_port: Set up the mapping between one PINT device and
 *                 multiple GPIO banks.
 */
struct gpio_pint {
	struct list_head node;
	void __iomem *base;
	int irq;
	struct irq_domain *domain[2];
	struct gpio_pint_regs *regs;
	struct gpio_pint_saved saved_data;
	int map_count;
	spinlock_t lock;

	int (*pint_map_port)(struct gpio_pint *pint, bool assign,
				u8 map, struct irq_domain *domain);
};

/**
 * ADI pin controller
 *
 * @dev: a pointer back to containing device
 * @pctl: the pinctrl device
 * @soc: SoC data for this specific chip
 */
struct adi_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	const struct adi_pinctrl_soc_data *soc;
};

/**
 * struct gpio_port - GPIO bank device. Multiple ADI GPIO banks can be mapped
 * into one pin interrupt controller.
 *
 * @node: All gpio_port instances are added to a list.
 * @base: GPIO bank device register base address
 * @irq_base: base IRQ of the GPIO bank device
 * @width: PIN number of the GPIO bank device
 * @regs: address pointer to the GPIO bank device
 * @saved_data: registers that should be saved between PM operations.
 * @dev: device structure of this GPIO bank
 * @pint: GPIO PINT device that this GPIO bank mapped to
 * @pint_map: GIOP bank mapping code in PINT device
 * @pint_assign: The 32-bit PINT registers can be divided into 2 parts. A
 *               GPIO bank can be mapped into either low 16 bits[0] or high 16
 *               bits[1] of each PINT register.
 * @lock: This lock make sure the irq_chip operations to one PINT device
 *        for different GPIO interrrupts are atomic.
 * @chip: abstract a GPIO controller
 * @domain: The irq domain owned by the GPIO port.
 * @rsvmap: Reservation map array for each pin in the GPIO bank
 */
struct gpio_port {
	struct list_head node;
	void __iomem *base;
	int irq_base;
	unsigned int width;
	struct gpio_port_t *regs;
	struct gpio_port_saved saved_data;
	struct device *dev;

	struct gpio_pint *pint;
	u8 pint_map;
	bool pint_assign;

	spinlock_t lock;
	struct gpio_chip chip;
	struct irq_domain *domain;
};

static inline u8 pin_to_offset(struct pinctrl_gpio_range *range, unsigned pin)
{
	return pin - range->pin_base;
}

static inline u32 hwirq_to_pintbit(struct gpio_port *port, int hwirq)
{
	return port->pint_assign ? BIT(hwirq) << PINT_HI_OFFSET : BIT(hwirq);
}

static struct gpio_pint *find_gpio_pint(unsigned id)
{
	struct gpio_pint *pint;
	int i = 0;

	list_for_each_entry(pint, &adi_pint_list, node) {
		if (id == i)
			return pint;
		i++;
	}

	return NULL;
}

static inline void port_setup(struct gpio_port *port, unsigned offset,
	bool use_for_gpio)
{
	struct gpio_port_t *regs = port->regs;

	if (use_for_gpio)
		writew(readw(&regs->port_fer) & ~BIT(offset),
			&regs->port_fer);
	else
		writew(readw(&regs->port_fer) | BIT(offset), &regs->port_fer);
}

static inline void portmux_setup(struct gpio_port *port, unsigned offset,
	unsigned short function)
{
	struct gpio_port_t *regs = port->regs;
	u32 pmux;

	pmux = readl(&regs->port_mux);

	/* The function field of each pin has 2 consecutive bits in
	 * the mux register.
	 */
	pmux &= ~(0x3 << (2 * offset));
	pmux |= (function & 0x3) << (2 * offset);

	writel(pmux, &regs->port_mux);
}

static inline u16 get_portmux(struct gpio_port *port, unsigned offset)
{
	struct gpio_port_t *regs = port->regs;
	u32 pmux = readl(&regs->port_mux);

	/* The function field of each pin has 2 consecutive bits in
	 * the mux register.
	 */
	return pmux >> (2 * offset) & 0x3;
}

static void adi_gpio_ack_irq(struct irq_data *d)
{
	unsigned long flags;
	struct gpio_port *port = irq_data_get_irq_chip_data(d);
	struct gpio_pint_regs *regs = port->pint->regs;
	unsigned pintbit = hwirq_to_pintbit(port, d->hwirq);

	spin_lock_irqsave(&port->lock, flags);
	spin_lock(&port->pint->lock);

	if (irqd_get_trigger_type(d) == IRQ_TYPE_EDGE_BOTH) {
		if (readl(&regs->invert_set) & pintbit)
			writel(pintbit, &regs->invert_clear);
		else
			writel(pintbit, &regs->invert_set);
	}

	writel(pintbit, &regs->request);

	spin_unlock(&port->pint->lock);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void adi_gpio_mask_ack_irq(struct irq_data *d)
{
	unsigned long flags;
	struct gpio_port *port = irq_data_get_irq_chip_data(d);
	struct gpio_pint_regs *regs = port->pint->regs;
	unsigned pintbit = hwirq_to_pintbit(port, d->hwirq);

	spin_lock_irqsave(&port->lock, flags);
	spin_lock(&port->pint->lock);

	if (irqd_get_trigger_type(d) == IRQ_TYPE_EDGE_BOTH) {
		if (readl(&regs->invert_set) & pintbit)
			writel(pintbit, &regs->invert_clear);
		else
			writel(pintbit, &regs->invert_set);
	}

	writel(pintbit, &regs->request);
	writel(pintbit, &regs->mask_clear);

	spin_unlock(&port->pint->lock);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void adi_gpio_mask_irq(struct irq_data *d)
{
	unsigned long flags;
	struct gpio_port *port = irq_data_get_irq_chip_data(d);
	struct gpio_pint_regs *regs = port->pint->regs;

	spin_lock_irqsave(&port->lock, flags);
	spin_lock(&port->pint->lock);

	writel(hwirq_to_pintbit(port, d->hwirq), &regs->mask_clear);

	spin_unlock(&port->pint->lock);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void adi_gpio_unmask_irq(struct irq_data *d)
{
	unsigned long flags;
	struct gpio_port *port = irq_data_get_irq_chip_data(d);
	struct gpio_pint_regs *regs = port->pint->regs;

	spin_lock_irqsave(&port->lock, flags);
	spin_lock(&port->pint->lock);

	writel(hwirq_to_pintbit(port, d->hwirq), &regs->mask_set);

	spin_unlock(&port->pint->lock);
	spin_unlock_irqrestore(&port->lock, flags);
}

static unsigned int adi_gpio_irq_startup(struct irq_data *d)
{
	unsigned long flags;
	struct gpio_port *port = irq_data_get_irq_chip_data(d);
	struct gpio_pint_regs *regs;

	if (!port) {
		pr_err("GPIO IRQ %d :Not exist\n", d->irq);
		/* FIXME: negative return code will be ignored */
		return -ENODEV;
	}

	regs = port->pint->regs;

	spin_lock_irqsave(&port->lock, flags);
	spin_lock(&port->pint->lock);

	port_setup(port, d->hwirq, true);
	writew(BIT(d->hwirq), &port->regs->dir_clear);
	writew(readw(&port->regs->inen) | BIT(d->hwirq), &port->regs->inen);

	writel(hwirq_to_pintbit(port, d->hwirq), &regs->mask_set);

	spin_unlock(&port->pint->lock);
	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static void adi_gpio_irq_shutdown(struct irq_data *d)
{
	unsigned long flags;
	struct gpio_port *port = irq_data_get_irq_chip_data(d);
	struct gpio_pint_regs *regs = port->pint->regs;

	spin_lock_irqsave(&port->lock, flags);
	spin_lock(&port->pint->lock);

	writel(hwirq_to_pintbit(port, d->hwirq), &regs->mask_clear);

	spin_unlock(&port->pint->lock);
	spin_unlock_irqrestore(&port->lock, flags);
}

static int adi_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	unsigned long flags;
	struct gpio_port *port = irq_data_get_irq_chip_data(d);
	struct gpio_pint_regs *pint_regs;
	unsigned pintmask;
	unsigned int irq = d->irq;
	int ret = 0;
	char buf[16];

	if (!port) {
		pr_err("GPIO IRQ %d :Not exist\n", d->irq);
		return -ENODEV;
	}

	pint_regs = port->pint->regs;

	pintmask = hwirq_to_pintbit(port, d->hwirq);

	spin_lock_irqsave(&port->lock, flags);
	spin_lock(&port->pint->lock);

	/* In case of interrupt autodetect, set irq type to edge sensitive. */
	if (type == IRQ_TYPE_PROBE)
		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING |
		    IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW)) {
		snprintf(buf, 16, "gpio-irq%d", irq);
		port_setup(port, d->hwirq, true);
	} else
		goto out;

	/* The GPIO interrupt is triggered only when its input value
	 * transfer from 0 to 1. So, invert the input value if the
	 * irq type is low or falling
	 */
	if ((type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_LEVEL_LOW)))
		writel(pintmask, &pint_regs->invert_set);
	else
		writel(pintmask, &pint_regs->invert_clear);

	/* In edge sensitive case, if the input value of the requested irq
	 * is already 1, invert it.
	 */
	if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH) {
		if (gpio_get_value(port->chip.base + d->hwirq))
			writel(pintmask, &pint_regs->invert_set);
		else
			writel(pintmask, &pint_regs->invert_clear);
	}

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)) {
		writel(pintmask, &pint_regs->edge_set);
		__irq_set_handler_locked(irq, handle_edge_irq);
	} else {
		writel(pintmask, &pint_regs->edge_clear);
		__irq_set_handler_locked(irq, handle_level_irq);
	}

out:
	spin_unlock(&port->pint->lock);
	spin_unlock_irqrestore(&port->lock, flags);

	return ret;
}

#ifdef CONFIG_PM
static int adi_gpio_set_wake(struct irq_data *d, unsigned int state)
{
	struct gpio_port *port = irq_data_get_irq_chip_data(d);

	if (!port || !port->pint || port->pint->irq != d->irq)
		return -EINVAL;

#ifndef SEC_GCTL
	adi_internal_set_wake(port->pint->irq, state);
#endif

	return 0;
}

static int adi_pint_suspend(void)
{
	struct gpio_pint *pint;

	list_for_each_entry(pint, &adi_pint_list, node) {
		writel(0xffffffff, &pint->regs->mask_clear);
		pint->saved_data.assign = readl(&pint->regs->assign);
		pint->saved_data.edge_set = readl(&pint->regs->edge_set);
		pint->saved_data.invert_set = readl(&pint->regs->invert_set);
	}

	return 0;
}

static void adi_pint_resume(void)
{
	struct gpio_pint *pint;

	list_for_each_entry(pint, &adi_pint_list, node) {
		writel(pint->saved_data.assign, &pint->regs->assign);
		writel(pint->saved_data.edge_set, &pint->regs->edge_set);
		writel(pint->saved_data.invert_set, &pint->regs->invert_set);
	}
}

static int adi_gpio_suspend(void)
{
	struct gpio_port *port;

	list_for_each_entry(port, &adi_gpio_port_list, node) {
		port->saved_data.fer = readw(&port->regs->port_fer);
		port->saved_data.mux = readl(&port->regs->port_mux);
		port->saved_data.data = readw(&port->regs->data);
		port->saved_data.inen = readw(&port->regs->inen);
		port->saved_data.dir = readw(&port->regs->dir_set);
	}

	return adi_pint_suspend();
}

static void adi_gpio_resume(void)
{
	struct gpio_port *port;

	adi_pint_resume();

	list_for_each_entry(port, &adi_gpio_port_list, node) {
		writel(port->saved_data.mux, &port->regs->port_mux);
		writew(port->saved_data.fer, &port->regs->port_fer);
		writew(port->saved_data.inen, &port->regs->inen);
		writew(port->saved_data.data & port->saved_data.dir,
					&port->regs->data_set);
		writew(port->saved_data.dir, &port->regs->dir_set);
	}

}

static struct syscore_ops gpio_pm_syscore_ops = {
	.suspend = adi_gpio_suspend,
	.resume = adi_gpio_resume,
};
#else /* CONFIG_PM */
#define adi_gpio_set_wake NULL
#endif /* CONFIG_PM */

#ifdef CONFIG_IRQ_PREFLOW_FASTEOI
static inline void preflow_handler(struct irq_desc *desc)
{
	if (desc->preflow_handler)
		desc->preflow_handler(&desc->irq_data);
}
#else
static inline void preflow_handler(struct irq_desc *desc) { }
#endif

static void adi_gpio_handle_pint_irq(unsigned int inta_irq,
			struct irq_desc *desc)
{
	u32 request;
	u32 level_mask, hwirq;
	bool umask = false;
	struct gpio_pint *pint = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct gpio_pint_regs *regs = pint->regs;
	struct irq_domain *domain;

	preflow_handler(desc);
	chained_irq_enter(chip, desc);

	request = readl(&regs->request);
	level_mask = readl(&regs->edge_set) & request;

	hwirq = 0;
	domain = pint->domain[0];
	while (request) {
		/* domain pointer need to be changed only once at IRQ 16 when
		 * we go through IRQ requests from bit 0 to bit 31.
		 */
		if (hwirq == PINT_HI_OFFSET)
			domain = pint->domain[1];

		if (request & 1) {
			if (level_mask & BIT(hwirq)) {
				umask = true;
				chained_irq_exit(chip, desc);
			}
			generic_handle_irq(irq_find_mapping(domain,
					hwirq % PINT_HI_OFFSET));
		}

		hwirq++;
		request >>= 1;
	}

	if (!umask)
		chained_irq_exit(chip, desc);
}

static struct irq_chip adi_gpio_irqchip = {
	.name = "GPIO",
	.irq_ack = adi_gpio_ack_irq,
	.irq_mask = adi_gpio_mask_irq,
	.irq_mask_ack = adi_gpio_mask_ack_irq,
	.irq_unmask = adi_gpio_unmask_irq,
	.irq_disable = adi_gpio_mask_irq,
	.irq_enable = adi_gpio_unmask_irq,
	.irq_set_type = adi_gpio_irq_type,
	.irq_startup = adi_gpio_irq_startup,
	.irq_shutdown = adi_gpio_irq_shutdown,
	.irq_set_wake = adi_gpio_set_wake,
};

static int adi_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct adi_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl->soc->ngroups;
}

static const char *adi_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned selector)
{
	struct adi_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl->soc->groups[selector].name;
}

static int adi_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			       const unsigned **pins,
			       unsigned *num_pins)
{
	struct adi_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pinctrl->soc->groups[selector].pins;
	*num_pins = pinctrl->soc->groups[selector].num;
	return 0;
}

static struct pinctrl_ops adi_pctrl_ops = {
	.get_groups_count = adi_get_groups_count,
	.get_group_name = adi_get_group_name,
	.get_group_pins = adi_get_group_pins,
};

static int adi_pinmux_enable(struct pinctrl_dev *pctldev, unsigned func_id,
	unsigned group_id)
{
	struct adi_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_port *port;
	struct pinctrl_gpio_range *range;
	unsigned long flags;
	unsigned short *mux, pin;

	mux = (unsigned short *)pinctrl->soc->groups[group_id].mux;

	while (*mux) {
		pin = P_IDENT(*mux);

		range = pinctrl_find_gpio_range_from_pin(pctldev, pin);
		if (range == NULL) /* should not happen */
			return -ENODEV;

		port = container_of(range->gc, struct gpio_port, chip);

		spin_lock_irqsave(&port->lock, flags);

		portmux_setup(port, pin_to_offset(range, pin),
				P_FUNCT2MUX(*mux));
		port_setup(port, pin_to_offset(range, pin), false);
		mux++;

		spin_unlock_irqrestore(&port->lock, flags);
	}

	return 0;
}

static int adi_pinmux_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct adi_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl->soc->nfunctions;
}

static const char *adi_pinmux_get_func_name(struct pinctrl_dev *pctldev,
					  unsigned selector)
{
	struct adi_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl->soc->functions[selector].name;
}

static int adi_pinmux_get_groups(struct pinctrl_dev *pctldev, unsigned selector,
			       const char * const **groups,
			       unsigned * const num_groups)
{
	struct adi_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pinctrl->soc->functions[selector].groups;
	*num_groups = pinctrl->soc->functions[selector].num_groups;
	return 0;
}

static int adi_pinmux_request_gpio(struct pinctrl_dev *pctldev,
	struct pinctrl_gpio_range *range, unsigned pin)
{
	struct gpio_port *port;
	unsigned long flags;
	u8 offset;

	port = container_of(range->gc, struct gpio_port, chip);
	offset = pin_to_offset(range, pin);

	spin_lock_irqsave(&port->lock, flags);

	port_setup(port, offset, true);

	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static struct pinmux_ops adi_pinmux_ops = {
	.enable = adi_pinmux_enable,
	.get_functions_count = adi_pinmux_get_funcs_count,
	.get_function_name = adi_pinmux_get_func_name,
	.get_function_groups = adi_pinmux_get_groups,
	.gpio_request_enable = adi_pinmux_request_gpio,
};


static struct pinctrl_desc adi_pinmux_desc = {
	.name = DRIVER_NAME,
	.pctlops = &adi_pctrl_ops,
	.pmxops = &adi_pinmux_ops,
	.owner = THIS_MODULE,
};

static int adi_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void adi_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(chip->base + offset);
}

static int adi_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_port *port;
	unsigned long flags;

	port = container_of(chip, struct gpio_port, chip);

	spin_lock_irqsave(&port->lock, flags);

	writew(BIT(offset), &port->regs->dir_clear);
	writew(readw(&port->regs->inen) | BIT(offset), &port->regs->inen);

	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static void adi_gpio_set_value(struct gpio_chip *chip, unsigned offset,
	int value)
{
	struct gpio_port *port = container_of(chip, struct gpio_port, chip);
	struct gpio_port_t *regs = port->regs;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	if (value)
		writew(BIT(offset), &regs->data_set);
	else
		writew(BIT(offset), &regs->data_clear);

	spin_unlock_irqrestore(&port->lock, flags);
}

static int adi_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
	int value)
{
	struct gpio_port *port = container_of(chip, struct gpio_port, chip);
	struct gpio_port_t *regs = port->regs;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	writew(readw(&regs->inen) & ~BIT(offset), &regs->inen);
	if (value)
		writew(BIT(offset), &regs->data_set);
	else
		writew(BIT(offset), &regs->data_clear);
	writew(BIT(offset), &regs->dir_set);

	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static int adi_gpio_get_value(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_port *port = container_of(chip, struct gpio_port, chip);
	struct gpio_port_t *regs = port->regs;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&port->lock, flags);

	ret = !!(readw(&regs->data) & BIT(offset));

	spin_unlock_irqrestore(&port->lock, flags);

	return ret;
}

static int adi_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_port *port = container_of(chip, struct gpio_port, chip);

	if (port->irq_base >= 0)
		return irq_find_mapping(port->domain, offset);
	else
		return irq_create_mapping(port->domain, offset);
}

static int adi_pint_map_port(struct gpio_pint *pint, bool assign, u8 map,
	struct irq_domain *domain)
{
	struct gpio_pint_regs *regs = pint->regs;
	u32 map_mask;

	if (pint->map_count > 1)
		return -EINVAL;

	pint->map_count++;

	/* The map_mask of each gpio port is a 16-bit duplicate
	 * of the 8-bit map. It can be set to either high 16 bits or low
	 * 16 bits of the pint assignment register.
	 */
	map_mask = (map << 8) | map;
	if (assign) {
		map_mask <<= PINT_HI_OFFSET;
		writel((readl(&regs->assign) & 0xFFFF) | map_mask,
			&regs->assign);
	} else
		writel((readl(&regs->assign) & 0xFFFF0000) | map_mask,
			&regs->assign);

	pint->domain[assign] = domain;

	return 0;
}

static int adi_gpio_pint_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct gpio_pint *pint;

	pint = devm_kzalloc(dev, sizeof(struct gpio_pint), GFP_KERNEL);
	if (!pint) {
		dev_err(dev, "Memory alloc failed\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pint->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pint->base))
		return PTR_ERR(pint->base);

	pint->regs = (struct gpio_pint_regs *)pint->base;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev, "Invalid IRQ resource\n");
		return -ENODEV;
	}

	spin_lock_init(&pint->lock);

	pint->irq = res->start;
	pint->pint_map_port = adi_pint_map_port;
	platform_set_drvdata(pdev, pint);

	irq_set_chained_handler(pint->irq, adi_gpio_handle_pint_irq);
	irq_set_handler_data(pint->irq, pint);

	list_add_tail(&pint->node, &adi_pint_list);

	return 0;
}

static int adi_gpio_pint_remove(struct platform_device *pdev)
{
	struct gpio_pint *pint = platform_get_drvdata(pdev);

	list_del(&pint->node);
	irq_set_handler(pint->irq, handle_simple_irq);

	return 0;
}

static int adi_gpio_irq_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hwirq)
{
	struct gpio_port *port = d->host_data;

	if (!port)
		return -EINVAL;

	irq_set_chip_data(irq, port);
	irq_set_chip_and_handler(irq, &adi_gpio_irqchip,
				handle_level_irq);

	return 0;
}

static const struct irq_domain_ops adi_gpio_irq_domain_ops = {
	.map = adi_gpio_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

static int adi_gpio_init_int(struct gpio_port *port)
{
	struct device_node *node = port->dev->of_node;
	struct gpio_pint *pint = port->pint;
	int ret;

	port->domain = irq_domain_add_linear(node, port->width,
				&adi_gpio_irq_domain_ops, port);
	if (!port->domain) {
		dev_err(port->dev, "Failed to create irqdomain\n");
		return -ENOSYS;
	}

	/* According to BF54x and BF60x HRM, pin interrupt devices are not
	 * part of the GPIO port device. in GPIO interrupt mode, the GPIO
	 * pins of multiple port devices can be routed into one pin interrupt
	 * device. The mapping can be configured by setting pint assignment
	 * register with the mapping value of different GPIO port. This is
	 * done via function pint_map_port().
	 */
	ret = pint->pint_map_port(port->pint, port->pint_assign,
			port->pint_map,	port->domain);
	if (ret)
		return ret;

	if (port->irq_base >= 0) {
		ret = irq_create_strict_mappings(port->domain, port->irq_base,
					0, port->width);
		if (ret) {
			dev_err(port->dev, "Couldn't associate to domain\n");
			return ret;
		}
	}

	return 0;
}

#define DEVNAME_SIZE 16

static int adi_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct adi_pinctrl_gpio_platform_data *pdata;
	struct resource *res;
	struct gpio_port *port;
	char pinctrl_devname[DEVNAME_SIZE];
	static int gpio;
	int ret = 0, ret1;

	pdata = dev->platform_data;
	if (!pdata)
		return -EINVAL;

	port = devm_kzalloc(dev, sizeof(struct gpio_port), GFP_KERNEL);
	if (!port) {
		dev_err(dev, "Memory alloc failed\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	port->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(port->base))
		return PTR_ERR(port->base);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res)
		port->irq_base = -1;
	else
		port->irq_base = res->start;

	port->width = pdata->port_width;
	port->dev = dev;
	port->regs = (struct gpio_port_t *)port->base;
	port->pint_assign = pdata->pint_assign;
	port->pint_map = pdata->pint_map;

	port->pint = find_gpio_pint(pdata->pint_id);
	if (port->pint) {
		ret = adi_gpio_init_int(port);
		if (ret)
			return ret;
	}

	spin_lock_init(&port->lock);

	platform_set_drvdata(pdev, port);

	port->chip.label		= "adi-gpio";
	port->chip.direction_input	= adi_gpio_direction_input;
	port->chip.get			= adi_gpio_get_value;
	port->chip.direction_output	= adi_gpio_direction_output;
	port->chip.set			= adi_gpio_set_value;
	port->chip.request		= adi_gpio_request;
	port->chip.free			= adi_gpio_free;
	port->chip.to_irq		= adi_gpio_to_irq;
	if (pdata->port_gpio_base > 0)
		port->chip.base		= pdata->port_gpio_base;
	else
		port->chip.base		= gpio;
	port->chip.ngpio		= port->width;
	gpio = port->chip.base + port->width;

	ret = gpiochip_add(&port->chip);
	if (ret) {
		dev_err(&pdev->dev, "Fail to add GPIO chip.\n");
		goto out_remove_domain;
	}

	/* Add gpio pin range */
	snprintf(pinctrl_devname, DEVNAME_SIZE, "pinctrl-adi2.%d",
		pdata->pinctrl_id);
	pinctrl_devname[DEVNAME_SIZE - 1] = 0;
	ret = gpiochip_add_pin_range(&port->chip, pinctrl_devname,
		0, pdata->port_pin_base, port->width);
	if (ret) {
		dev_err(&pdev->dev, "Fail to add pin range to %s.\n",
				pinctrl_devname);
		goto out_remove_gpiochip;
	}

	list_add_tail(&port->node, &adi_gpio_port_list);

	return 0;

out_remove_gpiochip:
	ret1 = gpiochip_remove(&port->chip);
out_remove_domain:
	if (port->pint)
		irq_domain_remove(port->domain);

	return ret;
}

static int adi_gpio_remove(struct platform_device *pdev)
{
	struct gpio_port *port = platform_get_drvdata(pdev);
	int ret;
	u8 offset;

	list_del(&port->node);
	gpiochip_remove_pin_ranges(&port->chip);
	ret = gpiochip_remove(&port->chip);
	if (port->pint) {
		for (offset = 0; offset < port->width; offset++)
			irq_dispose_mapping(irq_find_mapping(port->domain,
				offset));
		irq_domain_remove(port->domain);
	}

	return ret;
}

static int adi_pinctrl_probe(struct platform_device *pdev)
{
	struct adi_pinctrl *pinctrl;

	pinctrl = devm_kzalloc(&pdev->dev, sizeof(*pinctrl), GFP_KERNEL);
	if (!pinctrl)
		return -ENOMEM;

	pinctrl->dev = &pdev->dev;

	adi_pinctrl_soc_init(&pinctrl->soc);

	adi_pinmux_desc.pins = pinctrl->soc->pins;
	adi_pinmux_desc.npins = pinctrl->soc->npins;

	/* Now register the pin controller and all pins it handles */
	pinctrl->pctl = pinctrl_register(&adi_pinmux_desc, &pdev->dev, pinctrl);
	if (!pinctrl->pctl) {
		dev_err(&pdev->dev, "could not register pinctrl ADI2 driver\n");
		return -EINVAL;
	}

	platform_set_drvdata(pdev, pinctrl);

	return 0;
}

static int adi_pinctrl_remove(struct platform_device *pdev)
{
	struct adi_pinctrl *pinctrl = platform_get_drvdata(pdev);

	pinctrl_unregister(pinctrl->pctl);

	return 0;
}

static struct platform_driver adi_pinctrl_driver = {
	.probe		= adi_pinctrl_probe,
	.remove		= adi_pinctrl_remove,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static struct platform_driver adi_gpio_pint_driver = {
	.probe		= adi_gpio_pint_probe,
	.remove		= adi_gpio_pint_remove,
	.driver		= {
		.name	= "adi-gpio-pint",
	},
};

static struct platform_driver adi_gpio_driver = {
	.probe		= adi_gpio_probe,
	.remove		= adi_gpio_remove,
	.driver		= {
		.name	= "adi-gpio",
	},
};

static int __init adi_pinctrl_setup(void)
{
	int ret;

	ret = platform_driver_register(&adi_pinctrl_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&adi_gpio_pint_driver);
	if (ret)
		goto pint_error;

	ret = platform_driver_register(&adi_gpio_driver);
	if (ret)
		goto gpio_error;

#ifdef CONFIG_PM
	register_syscore_ops(&gpio_pm_syscore_ops);
#endif
	return ret;
gpio_error:
	platform_driver_unregister(&adi_gpio_pint_driver);
pint_error:
	platform_driver_unregister(&adi_pinctrl_driver);

	return ret;
}
arch_initcall(adi_pinctrl_setup);

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("ADI gpio2 pin control driver");
MODULE_LICENSE("GPL");
