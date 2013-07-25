/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#define MAX_NR_GPIO 300

/* Bits of interest in the GPIO_IN_OUT register.
 */
enum {
	GPIO_IN  = 0,
	GPIO_OUT = 1
};

/* Bits of interest in the GPIO_INTR_STATUS register.
 */
enum {
	INTR_STATUS = 0,
};

/* Bits of interest in the GPIO_CFG register.
 */
enum {
	GPIO_OE = 9,
};

/* Bits of interest in the GPIO_INTR_CFG register.
 * When a GPIO triggers, two separate decisions are made, controlled
 * by two separate flags.
 *
 * - First, INTR_RAW_STATUS_EN controls whether or not the GPIO_INTR_STATUS
 * register for that GPIO will be updated to reflect the triggering of that
 * gpio.  If this bit is 0, this register will not be updated.
 * - Second, INTR_ENABLE controls whether an interrupt is triggered.
 *
 * If INTR_ENABLE is set and INTR_RAW_STATUS_EN is NOT set, an interrupt
 * can be triggered but the status register will not reflect it.
 */
enum {
	INTR_ENABLE        = 0,
	INTR_POL_CTL       = 1,
	INTR_DECT_CTL      = 2,
	INTR_RAW_STATUS_EN = 3,
};

/* Codes of interest in GPIO_INTR_CFG_SU.
 */
enum {
	TARGET_PROC_SCORPION = 4,
	TARGET_PROC_NONE     = 7,
};

/**
 * struct msm_gpio_dev: the MSM8660 SoC GPIO device structure
 *
 * @enabled_irqs: a bitmap used to optimize the summary-irq handler.  By
 * keeping track of which gpios are unmasked as irq sources, we avoid
 * having to do readl calls on hundreds of iomapped registers each time
 * the summary interrupt fires in order to locate the active interrupts.
 *
 * @wake_irqs: a bitmap for tracking which interrupt lines are enabled
 * as wakeup sources.  When the device is suspended, interrupts which are
 * not wakeup sources are disabled.
 *
 * @dual_edge_irqs: a bitmap used to track which irqs are configured
 * as dual-edge, as this is not supported by the hardware and requires
 * some special handling in the driver.
 */
struct msm_gpio_dev {
	struct gpio_chip gpio_chip;
	DECLARE_BITMAP(enabled_irqs, MAX_NR_GPIO);
	DECLARE_BITMAP(wake_irqs, MAX_NR_GPIO);
	DECLARE_BITMAP(dual_edge_irqs, MAX_NR_GPIO);
	struct irq_domain *domain;
	unsigned int summary_irq;
	void __iomem *msm_tlmm_base;
};

struct msm_gpio_dev msm_gpio;

#define GPIO_INTR_CFG_SU(gpio)    (msm_gpio.msm_tlmm_base + 0x0400 + \
								(0x04 * (gpio)))
#define GPIO_CONFIG(gpio)         (msm_gpio.msm_tlmm_base + 0x1000 + \
								(0x10 * (gpio)))
#define GPIO_IN_OUT(gpio)         (msm_gpio.msm_tlmm_base + 0x1004 + \
								(0x10 * (gpio)))
#define GPIO_INTR_CFG(gpio)       (msm_gpio.msm_tlmm_base + 0x1008 + \
								(0x10 * (gpio)))
#define GPIO_INTR_STATUS(gpio)    (msm_gpio.msm_tlmm_base + 0x100c + \
								(0x10 * (gpio)))

static DEFINE_SPINLOCK(tlmm_lock);

static inline struct msm_gpio_dev *to_msm_gpio_dev(struct gpio_chip *chip)
{
	return container_of(chip, struct msm_gpio_dev, gpio_chip);
}

static inline void set_gpio_bits(unsigned n, void __iomem *reg)
{
	writel(readl(reg) | n, reg);
}

static inline void clear_gpio_bits(unsigned n, void __iomem *reg)
{
	writel(readl(reg) & ~n, reg);
}

static int msm_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return readl(GPIO_IN_OUT(offset)) & BIT(GPIO_IN);
}

static void msm_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	writel(val ? BIT(GPIO_OUT) : 0, GPIO_IN_OUT(offset));
}

static int msm_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	clear_gpio_bits(BIT(GPIO_OE), GPIO_CONFIG(offset));
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);
	return 0;
}

static int msm_gpio_direction_output(struct gpio_chip *chip,
				unsigned offset,
				int val)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	msm_gpio_set(chip, offset, val);
	set_gpio_bits(BIT(GPIO_OE), GPIO_CONFIG(offset));
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);
	return 0;
}

static int msm_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}

static void msm_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	return;
}

static int msm_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct msm_gpio_dev *g_dev = to_msm_gpio_dev(chip);
	struct irq_domain *domain = g_dev->domain;

	return irq_create_mapping(domain, offset);
}

static inline int msm_irq_to_gpio(struct gpio_chip *chip, unsigned irq)
{
	struct irq_data *irq_data = irq_get_irq_data(irq);

	return irq_data->hwirq;
}


/* For dual-edge interrupts in software, since the hardware has no
 * such support:
 *
 * At appropriate moments, this function may be called to flip the polarity
 * settings of both-edge irq lines to try and catch the next edge.
 *
 * The attempt is considered successful if:
 * - the status bit goes high, indicating that an edge was caught, or
 * - the input value of the gpio doesn't change during the attempt.
 * If the value changes twice during the process, that would cause the first
 * test to fail but would force the second, as two opposite
 * transitions would cause a detection no matter the polarity setting.
 *
 * The do-loop tries to sledge-hammer closed the timing hole between
 * the initial value-read and the polarity-write - if the line value changes
 * during that window, an interrupt is lost, the new polarity setting is
 * incorrect, and the first success test will fail, causing a retry.
 *
 * Algorithm comes from Google's msmgpio driver, see mach-msm/gpio.c.
 */
static void msm_gpio_update_dual_edge_pos(unsigned gpio)
{
	int loop_limit = 100;
	unsigned val, val2, intstat;

	do {
		val = readl(GPIO_IN_OUT(gpio)) & BIT(GPIO_IN);
		if (val)
			clear_gpio_bits(BIT(INTR_POL_CTL), GPIO_INTR_CFG(gpio));
		else
			set_gpio_bits(BIT(INTR_POL_CTL), GPIO_INTR_CFG(gpio));
		val2 = readl(GPIO_IN_OUT(gpio)) & BIT(GPIO_IN);
		intstat = readl(GPIO_INTR_STATUS(gpio)) & BIT(INTR_STATUS);
		if (intstat || val == val2)
			return;
	} while (loop_limit-- > 0);
	pr_err("%s: dual-edge irq failed to stabilize, "
	       "interrupts dropped. %#08x != %#08x\n",
	       __func__, val, val2);
}

static void msm_gpio_irq_ack(struct irq_data *d)
{
	int gpio = msm_irq_to_gpio(&msm_gpio.gpio_chip, d->irq);

	writel(BIT(INTR_STATUS), GPIO_INTR_STATUS(gpio));
	if (test_bit(gpio, msm_gpio.dual_edge_irqs))
		msm_gpio_update_dual_edge_pos(gpio);
}

static void msm_gpio_irq_mask(struct irq_data *d)
{
	int gpio = msm_irq_to_gpio(&msm_gpio.gpio_chip, d->irq);
	unsigned long irq_flags;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	writel(TARGET_PROC_NONE, GPIO_INTR_CFG_SU(gpio));
	clear_gpio_bits(INTR_RAW_STATUS_EN | INTR_ENABLE, GPIO_INTR_CFG(gpio));
	__clear_bit(gpio, msm_gpio.enabled_irqs);
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);
}

static void msm_gpio_irq_unmask(struct irq_data *d)
{
	int gpio = msm_irq_to_gpio(&msm_gpio.gpio_chip, d->irq);
	unsigned long irq_flags;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	__set_bit(gpio, msm_gpio.enabled_irqs);
	set_gpio_bits(INTR_RAW_STATUS_EN | INTR_ENABLE, GPIO_INTR_CFG(gpio));
	writel(TARGET_PROC_SCORPION, GPIO_INTR_CFG_SU(gpio));
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);
}

static int msm_gpio_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	int gpio = msm_irq_to_gpio(&msm_gpio.gpio_chip, d->irq);
	unsigned long irq_flags;
	uint32_t bits;

	spin_lock_irqsave(&tlmm_lock, irq_flags);

	bits = readl(GPIO_INTR_CFG(gpio));

	if (flow_type & IRQ_TYPE_EDGE_BOTH) {
		bits |= BIT(INTR_DECT_CTL);
		__irq_set_handler_locked(d->irq, handle_edge_irq);
		if ((flow_type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
			__set_bit(gpio, msm_gpio.dual_edge_irqs);
		else
			__clear_bit(gpio, msm_gpio.dual_edge_irqs);
	} else {
		bits &= ~BIT(INTR_DECT_CTL);
		__irq_set_handler_locked(d->irq, handle_level_irq);
		__clear_bit(gpio, msm_gpio.dual_edge_irqs);
	}

	if (flow_type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_LEVEL_HIGH))
		bits |= BIT(INTR_POL_CTL);
	else
		bits &= ~BIT(INTR_POL_CTL);

	writel(bits, GPIO_INTR_CFG(gpio));

	if ((flow_type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
		msm_gpio_update_dual_edge_pos(gpio);

	spin_unlock_irqrestore(&tlmm_lock, irq_flags);

	return 0;
}

/*
 * When the summary IRQ is raised, any number of GPIO lines may be high.
 * It is the job of the summary handler to find all those GPIO lines
 * which have been set as summary IRQ lines and which are triggered,
 * and to call their interrupt handlers.
 */
static void msm_summary_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned long i;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	for_each_set_bit(i, msm_gpio.enabled_irqs, MAX_NR_GPIO) {
		if (readl(GPIO_INTR_STATUS(i)) & BIT(INTR_STATUS))
			generic_handle_irq(irq_find_mapping(msm_gpio.domain,
								i));
	}

	chained_irq_exit(chip, desc);
}

static int msm_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	int gpio = msm_irq_to_gpio(&msm_gpio.gpio_chip, d->irq);

	if (on) {
		if (bitmap_empty(msm_gpio.wake_irqs, MAX_NR_GPIO))
			irq_set_irq_wake(msm_gpio.summary_irq, 1);
		set_bit(gpio, msm_gpio.wake_irqs);
	} else {
		clear_bit(gpio, msm_gpio.wake_irqs);
		if (bitmap_empty(msm_gpio.wake_irqs, MAX_NR_GPIO))
			irq_set_irq_wake(msm_gpio.summary_irq, 0);
	}

	return 0;
}

static struct irq_chip msm_gpio_irq_chip = {
	.name		= "msmgpio",
	.irq_mask	= msm_gpio_irq_mask,
	.irq_unmask	= msm_gpio_irq_unmask,
	.irq_ack	= msm_gpio_irq_ack,
	.irq_set_type	= msm_gpio_irq_set_type,
	.irq_set_wake	= msm_gpio_irq_set_wake,
};

static struct lock_class_key msm_gpio_lock_class;

static int msm_gpio_irq_domain_map(struct irq_domain *d, unsigned int irq,
				   irq_hw_number_t hwirq)
{
	irq_set_lockdep_class(irq, &msm_gpio_lock_class);
	irq_set_chip_and_handler(irq, &msm_gpio_irq_chip,
			handle_level_irq);
	set_irq_flags(irq, IRQF_VALID);

	return 0;
}

static const struct irq_domain_ops msm_gpio_irq_domain_ops = {
	.xlate = irq_domain_xlate_twocell,
	.map = msm_gpio_irq_domain_map,
};

static int msm_gpio_probe(struct platform_device *pdev)
{
	int ret, ngpio;
	struct resource *res;

	if (of_property_read_u32(pdev->dev.of_node, "ngpio", &ngpio)) {
		dev_err(&pdev->dev, "%s: ngpio property missing\n", __func__);
		return -EINVAL;
	}

	if (ngpio > MAX_NR_GPIO)
		WARN(1, "ngpio exceeds the MAX_NR_GPIO. Increase MAX_NR_GPIO\n");

	bitmap_zero(msm_gpio.enabled_irqs, MAX_NR_GPIO);
	bitmap_zero(msm_gpio.wake_irqs, MAX_NR_GPIO);
	bitmap_zero(msm_gpio.dual_edge_irqs, MAX_NR_GPIO);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	msm_gpio.msm_tlmm_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(msm_gpio.msm_tlmm_base))
		return PTR_ERR(msm_gpio.msm_tlmm_base);

	msm_gpio.gpio_chip.ngpio = ngpio;
	msm_gpio.gpio_chip.label = pdev->name;
	msm_gpio.gpio_chip.dev = &pdev->dev;
	msm_gpio.gpio_chip.base = 0;
	msm_gpio.gpio_chip.direction_input = msm_gpio_direction_input;
	msm_gpio.gpio_chip.direction_output = msm_gpio_direction_output;
	msm_gpio.gpio_chip.get = msm_gpio_get;
	msm_gpio.gpio_chip.set = msm_gpio_set;
	msm_gpio.gpio_chip.to_irq = msm_gpio_to_irq;
	msm_gpio.gpio_chip.request = msm_gpio_request;
	msm_gpio.gpio_chip.free = msm_gpio_free;

	ret = gpiochip_add(&msm_gpio.gpio_chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "gpiochip_add failed with error %d\n", ret);
		return ret;
	}

	msm_gpio.summary_irq = platform_get_irq(pdev, 0);
	if (msm_gpio.summary_irq < 0) {
		dev_err(&pdev->dev, "No Summary irq defined for msmgpio\n");
		return msm_gpio.summary_irq;
	}

	msm_gpio.domain = irq_domain_add_linear(pdev->dev.of_node, ngpio,
						&msm_gpio_irq_domain_ops,
						&msm_gpio);
	if (!msm_gpio.domain)
		return -ENODEV;

	irq_set_chained_handler(msm_gpio.summary_irq, msm_summary_irq_handler);

	return 0;
}

static struct of_device_id msm_gpio_of_match[] = {
	{ .compatible = "qcom,msm-gpio", },
	{ },
};

static int msm_gpio_remove(struct platform_device *dev)
{
	int ret = gpiochip_remove(&msm_gpio.gpio_chip);

	if (ret < 0)
		return ret;

	irq_set_handler(msm_gpio.summary_irq, NULL);

	return 0;
}

static struct platform_driver msm_gpio_driver = {
	.probe = msm_gpio_probe,
	.remove = msm_gpio_remove,
	.driver = {
		.name = "msmgpio",
		.owner = THIS_MODULE,
		.of_match_table = msm_gpio_of_match,
	},
};

module_platform_driver(msm_gpio_driver)

MODULE_AUTHOR("Gregory Bean <gbean@codeaurora.org>");
MODULE_DESCRIPTION("Driver for Qualcomm MSM TLMMv2 SoC GPIOs");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:msmgpio");
