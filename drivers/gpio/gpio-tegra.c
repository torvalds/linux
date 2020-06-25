// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-tegra/gpio.c
 *
 * Copyright (c) 2010 Google, Inc
 * Copyright (c) 2011-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author:
 *	Erik Gilling <konkers@google.com>
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio/driver.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm.h>

#define GPIO_BANK(x)		((x) >> 5)
#define GPIO_PORT(x)		(((x) >> 3) & 0x3)
#define GPIO_BIT(x)		((x) & 0x7)

#define GPIO_REG(tgi, x)	(GPIO_BANK(x) * tgi->soc->bank_stride + \
					GPIO_PORT(x) * 4)

#define GPIO_CNF(t, x)		(GPIO_REG(t, x) + 0x00)
#define GPIO_OE(t, x)		(GPIO_REG(t, x) + 0x10)
#define GPIO_OUT(t, x)		(GPIO_REG(t, x) + 0X20)
#define GPIO_IN(t, x)		(GPIO_REG(t, x) + 0x30)
#define GPIO_INT_STA(t, x)	(GPIO_REG(t, x) + 0x40)
#define GPIO_INT_ENB(t, x)	(GPIO_REG(t, x) + 0x50)
#define GPIO_INT_LVL(t, x)	(GPIO_REG(t, x) + 0x60)
#define GPIO_INT_CLR(t, x)	(GPIO_REG(t, x) + 0x70)
#define GPIO_DBC_CNT(t, x)	(GPIO_REG(t, x) + 0xF0)


#define GPIO_MSK_CNF(t, x)	(GPIO_REG(t, x) + t->soc->upper_offset + 0x00)
#define GPIO_MSK_OE(t, x)	(GPIO_REG(t, x) + t->soc->upper_offset + 0x10)
#define GPIO_MSK_OUT(t, x)	(GPIO_REG(t, x) + t->soc->upper_offset + 0X20)
#define GPIO_MSK_DBC_EN(t, x)	(GPIO_REG(t, x) + t->soc->upper_offset + 0x30)
#define GPIO_MSK_INT_STA(t, x)	(GPIO_REG(t, x) + t->soc->upper_offset + 0x40)
#define GPIO_MSK_INT_ENB(t, x)	(GPIO_REG(t, x) + t->soc->upper_offset + 0x50)
#define GPIO_MSK_INT_LVL(t, x)	(GPIO_REG(t, x) + t->soc->upper_offset + 0x60)

#define GPIO_INT_LVL_MASK		0x010101
#define GPIO_INT_LVL_EDGE_RISING	0x000101
#define GPIO_INT_LVL_EDGE_FALLING	0x000100
#define GPIO_INT_LVL_EDGE_BOTH		0x010100
#define GPIO_INT_LVL_LEVEL_HIGH		0x000001
#define GPIO_INT_LVL_LEVEL_LOW		0x000000

struct tegra_gpio_info;

struct tegra_gpio_bank {
	unsigned int bank;
	unsigned int irq;
	spinlock_t lvl_lock[4];
	spinlock_t dbc_lock[4];	/* Lock for updating debounce count register */
#ifdef CONFIG_PM_SLEEP
	u32 cnf[4];
	u32 out[4];
	u32 oe[4];
	u32 int_enb[4];
	u32 int_lvl[4];
	u32 wake_enb[4];
	u32 dbc_enb[4];
#endif
	u32 dbc_cnt[4];
	struct tegra_gpio_info *tgi;
};

struct tegra_gpio_soc_config {
	bool debounce_supported;
	u32 bank_stride;
	u32 upper_offset;
};

struct tegra_gpio_info {
	struct device				*dev;
	void __iomem				*regs;
	struct irq_domain			*irq_domain;
	struct tegra_gpio_bank			*bank_info;
	const struct tegra_gpio_soc_config	*soc;
	struct gpio_chip			gc;
	struct irq_chip				ic;
	u32					bank_count;
};

static inline void tegra_gpio_writel(struct tegra_gpio_info *tgi,
				     u32 val, u32 reg)
{
	writel_relaxed(val, tgi->regs + reg);
}

static inline u32 tegra_gpio_readl(struct tegra_gpio_info *tgi, u32 reg)
{
	return readl_relaxed(tgi->regs + reg);
}

static unsigned int tegra_gpio_compose(unsigned int bank, unsigned int port,
				       unsigned int bit)
{
	return (bank << 5) | ((port & 0x3) << 3) | (bit & 0x7);
}

static void tegra_gpio_mask_write(struct tegra_gpio_info *tgi, u32 reg,
				  unsigned int gpio, u32 value)
{
	u32 val;

	val = 0x100 << GPIO_BIT(gpio);
	if (value)
		val |= 1 << GPIO_BIT(gpio);
	tegra_gpio_writel(tgi, val, reg);
}

static void tegra_gpio_enable(struct tegra_gpio_info *tgi, unsigned int gpio)
{
	tegra_gpio_mask_write(tgi, GPIO_MSK_CNF(tgi, gpio), gpio, 1);
}

static void tegra_gpio_disable(struct tegra_gpio_info *tgi, unsigned int gpio)
{
	tegra_gpio_mask_write(tgi, GPIO_MSK_CNF(tgi, gpio), gpio, 0);
}

static int tegra_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	return pinctrl_gpio_request(chip->base + offset);
}

static void tegra_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	struct tegra_gpio_info *tgi = gpiochip_get_data(chip);

	pinctrl_gpio_free(chip->base + offset);
	tegra_gpio_disable(tgi, offset);
}

static void tegra_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct tegra_gpio_info *tgi = gpiochip_get_data(chip);

	tegra_gpio_mask_write(tgi, GPIO_MSK_OUT(tgi, offset), offset, value);
}

static int tegra_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct tegra_gpio_info *tgi = gpiochip_get_data(chip);
	unsigned int bval = BIT(GPIO_BIT(offset));

	/* If gpio is in output mode then read from the out value */
	if (tegra_gpio_readl(tgi, GPIO_OE(tgi, offset)) & bval)
		return !!(tegra_gpio_readl(tgi, GPIO_OUT(tgi, offset)) & bval);

	return !!(tegra_gpio_readl(tgi, GPIO_IN(tgi, offset)) & bval);
}

static int tegra_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct tegra_gpio_info *tgi = gpiochip_get_data(chip);
	int ret;

	tegra_gpio_mask_write(tgi, GPIO_MSK_OE(tgi, offset), offset, 0);
	tegra_gpio_enable(tgi, offset);

	ret = pinctrl_gpio_direction_input(chip->base + offset);
	if (ret < 0)
		dev_err(tgi->dev,
			"Failed to set pinctrl input direction of GPIO %d: %d",
			 chip->base + offset, ret);

	return ret;
}

static int tegra_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset,
				       int value)
{
	struct tegra_gpio_info *tgi = gpiochip_get_data(chip);
	int ret;

	tegra_gpio_set(chip, offset, value);
	tegra_gpio_mask_write(tgi, GPIO_MSK_OE(tgi, offset), offset, 1);
	tegra_gpio_enable(tgi, offset);

	ret = pinctrl_gpio_direction_output(chip->base + offset);
	if (ret < 0)
		dev_err(tgi->dev,
			"Failed to set pinctrl output direction of GPIO %d: %d",
			 chip->base + offset, ret);

	return ret;
}

static int tegra_gpio_get_direction(struct gpio_chip *chip,
				    unsigned int offset)
{
	struct tegra_gpio_info *tgi = gpiochip_get_data(chip);
	u32 pin_mask = BIT(GPIO_BIT(offset));
	u32 cnf, oe;

	cnf = tegra_gpio_readl(tgi, GPIO_CNF(tgi, offset));
	if (!(cnf & pin_mask))
		return -EINVAL;

	oe = tegra_gpio_readl(tgi, GPIO_OE(tgi, offset));

	if (oe & pin_mask)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int tegra_gpio_set_debounce(struct gpio_chip *chip, unsigned int offset,
				   unsigned int debounce)
{
	struct tegra_gpio_info *tgi = gpiochip_get_data(chip);
	struct tegra_gpio_bank *bank = &tgi->bank_info[GPIO_BANK(offset)];
	unsigned int debounce_ms = DIV_ROUND_UP(debounce, 1000);
	unsigned long flags;
	unsigned int port;

	if (!debounce_ms) {
		tegra_gpio_mask_write(tgi, GPIO_MSK_DBC_EN(tgi, offset),
				      offset, 0);
		return 0;
	}

	debounce_ms = min(debounce_ms, 255U);
	port = GPIO_PORT(offset);

	/* There is only one debounce count register per port and hence
	 * set the maximum of current and requested debounce time.
	 */
	spin_lock_irqsave(&bank->dbc_lock[port], flags);
	if (bank->dbc_cnt[port] < debounce_ms) {
		tegra_gpio_writel(tgi, debounce_ms, GPIO_DBC_CNT(tgi, offset));
		bank->dbc_cnt[port] = debounce_ms;
	}
	spin_unlock_irqrestore(&bank->dbc_lock[port], flags);

	tegra_gpio_mask_write(tgi, GPIO_MSK_DBC_EN(tgi, offset), offset, 1);

	return 0;
}

static int tegra_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				 unsigned long config)
{
	u32 debounce;

	if (pinconf_to_config_param(config) != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	debounce = pinconf_to_config_argument(config);
	return tegra_gpio_set_debounce(chip, offset, debounce);
}

static int tegra_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct tegra_gpio_info *tgi = gpiochip_get_data(chip);

	return irq_find_mapping(tgi->irq_domain, offset);
}

static void tegra_gpio_irq_ack(struct irq_data *d)
{
	struct tegra_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	struct tegra_gpio_info *tgi = bank->tgi;
	unsigned int gpio = d->hwirq;

	tegra_gpio_writel(tgi, 1 << GPIO_BIT(gpio), GPIO_INT_CLR(tgi, gpio));
}

static void tegra_gpio_irq_mask(struct irq_data *d)
{
	struct tegra_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	struct tegra_gpio_info *tgi = bank->tgi;
	unsigned int gpio = d->hwirq;

	tegra_gpio_mask_write(tgi, GPIO_MSK_INT_ENB(tgi, gpio), gpio, 0);
}

static void tegra_gpio_irq_unmask(struct irq_data *d)
{
	struct tegra_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	struct tegra_gpio_info *tgi = bank->tgi;
	unsigned int gpio = d->hwirq;

	tegra_gpio_mask_write(tgi, GPIO_MSK_INT_ENB(tgi, gpio), gpio, 1);
}

static int tegra_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	unsigned int gpio = d->hwirq, port = GPIO_PORT(gpio), lvl_type;
	struct tegra_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	struct tegra_gpio_info *tgi = bank->tgi;
	unsigned long flags;
	u32 val;
	int ret;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		lvl_type = GPIO_INT_LVL_EDGE_RISING;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		lvl_type = GPIO_INT_LVL_EDGE_FALLING;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		lvl_type = GPIO_INT_LVL_EDGE_BOTH;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		lvl_type = GPIO_INT_LVL_LEVEL_HIGH;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		lvl_type = GPIO_INT_LVL_LEVEL_LOW;
		break;

	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&bank->lvl_lock[port], flags);

	val = tegra_gpio_readl(tgi, GPIO_INT_LVL(tgi, gpio));
	val &= ~(GPIO_INT_LVL_MASK << GPIO_BIT(gpio));
	val |= lvl_type << GPIO_BIT(gpio);
	tegra_gpio_writel(tgi, val, GPIO_INT_LVL(tgi, gpio));

	spin_unlock_irqrestore(&bank->lvl_lock[port], flags);

	tegra_gpio_mask_write(tgi, GPIO_MSK_OE(tgi, gpio), gpio, 0);
	tegra_gpio_enable(tgi, gpio);

	ret = gpiochip_lock_as_irq(&tgi->gc, gpio);
	if (ret) {
		dev_err(tgi->dev,
			"unable to lock Tegra GPIO %u as IRQ\n", gpio);
		tegra_gpio_disable(tgi, gpio);
		return ret;
	}

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		irq_set_handler_locked(d, handle_level_irq);
	else if (type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static void tegra_gpio_irq_shutdown(struct irq_data *d)
{
	struct tegra_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	struct tegra_gpio_info *tgi = bank->tgi;
	unsigned int gpio = d->hwirq;

	tegra_gpio_irq_mask(d);
	gpiochip_unlock_as_irq(&tgi->gc, gpio);
}

static void tegra_gpio_irq_handler(struct irq_desc *desc)
{
	unsigned int port, pin, gpio;
	bool unmasked = false;
	u32 lvl;
	unsigned long sta;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct tegra_gpio_bank *bank = irq_desc_get_handler_data(desc);
	struct tegra_gpio_info *tgi = bank->tgi;

	chained_irq_enter(chip, desc);

	for (port = 0; port < 4; port++) {
		gpio = tegra_gpio_compose(bank->bank, port, 0);
		sta = tegra_gpio_readl(tgi, GPIO_INT_STA(tgi, gpio)) &
			tegra_gpio_readl(tgi, GPIO_INT_ENB(tgi, gpio));
		lvl = tegra_gpio_readl(tgi, GPIO_INT_LVL(tgi, gpio));

		for_each_set_bit(pin, &sta, 8) {
			tegra_gpio_writel(tgi, 1 << pin,
					  GPIO_INT_CLR(tgi, gpio));

			/* if gpio is edge triggered, clear condition
			 * before executing the handler so that we don't
			 * miss edges
			 */
			if (!unmasked && lvl & (0x100 << pin)) {
				unmasked = true;
				chained_irq_exit(chip, desc);
			}

			generic_handle_irq(irq_find_mapping(tgi->irq_domain,
							    gpio + pin));
		}
	}

	if (!unmasked)
		chained_irq_exit(chip, desc);

}

#ifdef CONFIG_PM_SLEEP
static int tegra_gpio_resume(struct device *dev)
{
	struct tegra_gpio_info *tgi = dev_get_drvdata(dev);
	unsigned int b, p;

	for (b = 0; b < tgi->bank_count; b++) {
		struct tegra_gpio_bank *bank = &tgi->bank_info[b];

		for (p = 0; p < ARRAY_SIZE(bank->oe); p++) {
			unsigned int gpio = (b << 5) | (p << 3);

			tegra_gpio_writel(tgi, bank->cnf[p],
					  GPIO_CNF(tgi, gpio));

			if (tgi->soc->debounce_supported) {
				tegra_gpio_writel(tgi, bank->dbc_cnt[p],
						  GPIO_DBC_CNT(tgi, gpio));
				tegra_gpio_writel(tgi, bank->dbc_enb[p],
						  GPIO_MSK_DBC_EN(tgi, gpio));
			}

			tegra_gpio_writel(tgi, bank->out[p],
					  GPIO_OUT(tgi, gpio));
			tegra_gpio_writel(tgi, bank->oe[p],
					  GPIO_OE(tgi, gpio));
			tegra_gpio_writel(tgi, bank->int_lvl[p],
					  GPIO_INT_LVL(tgi, gpio));
			tegra_gpio_writel(tgi, bank->int_enb[p],
					  GPIO_INT_ENB(tgi, gpio));
		}
	}

	return 0;
}

static int tegra_gpio_suspend(struct device *dev)
{
	struct tegra_gpio_info *tgi = dev_get_drvdata(dev);
	unsigned int b, p;

	for (b = 0; b < tgi->bank_count; b++) {
		struct tegra_gpio_bank *bank = &tgi->bank_info[b];

		for (p = 0; p < ARRAY_SIZE(bank->oe); p++) {
			unsigned int gpio = (b << 5) | (p << 3);

			bank->cnf[p] = tegra_gpio_readl(tgi,
							GPIO_CNF(tgi, gpio));
			bank->out[p] = tegra_gpio_readl(tgi,
							GPIO_OUT(tgi, gpio));
			bank->oe[p] = tegra_gpio_readl(tgi,
						       GPIO_OE(tgi, gpio));
			if (tgi->soc->debounce_supported) {
				bank->dbc_enb[p] = tegra_gpio_readl(tgi,
						GPIO_MSK_DBC_EN(tgi, gpio));
				bank->dbc_enb[p] = (bank->dbc_enb[p] << 8) |
							bank->dbc_enb[p];
			}

			bank->int_enb[p] = tegra_gpio_readl(tgi,
						GPIO_INT_ENB(tgi, gpio));
			bank->int_lvl[p] = tegra_gpio_readl(tgi,
						GPIO_INT_LVL(tgi, gpio));

			/* Enable gpio irq for wake up source */
			tegra_gpio_writel(tgi, bank->wake_enb[p],
					  GPIO_INT_ENB(tgi, gpio));
		}
	}

	return 0;
}

static int tegra_gpio_irq_set_wake(struct irq_data *d, unsigned int enable)
{
	struct tegra_gpio_bank *bank = irq_data_get_irq_chip_data(d);
	unsigned int gpio = d->hwirq;
	u32 port, bit, mask;
	int err;

	err = irq_set_irq_wake(bank->irq, enable);
	if (err)
		return err;

	port = GPIO_PORT(gpio);
	bit = GPIO_BIT(gpio);
	mask = BIT(bit);

	if (enable)
		bank->wake_enb[port] |= mask;
	else
		bank->wake_enb[port] &= ~mask;

	return 0;
}
#endif

#ifdef	CONFIG_DEBUG_FS

#include <linux/debugfs.h>
#include <linux/seq_file.h>

static int tegra_dbg_gpio_show(struct seq_file *s, void *unused)
{
	struct tegra_gpio_info *tgi = s->private;
	unsigned int i, j;

	for (i = 0; i < tgi->bank_count; i++) {
		for (j = 0; j < 4; j++) {
			unsigned int gpio = tegra_gpio_compose(i, j, 0);

			seq_printf(s,
				"%u:%u %02x %02x %02x %02x %02x %02x %06x\n",
				i, j,
				tegra_gpio_readl(tgi, GPIO_CNF(tgi, gpio)),
				tegra_gpio_readl(tgi, GPIO_OE(tgi, gpio)),
				tegra_gpio_readl(tgi, GPIO_OUT(tgi, gpio)),
				tegra_gpio_readl(tgi, GPIO_IN(tgi, gpio)),
				tegra_gpio_readl(tgi, GPIO_INT_STA(tgi, gpio)),
				tegra_gpio_readl(tgi, GPIO_INT_ENB(tgi, gpio)),
				tegra_gpio_readl(tgi, GPIO_INT_LVL(tgi, gpio)));
		}
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(tegra_dbg_gpio);

static void tegra_gpio_debuginit(struct tegra_gpio_info *tgi)
{
	debugfs_create_file("tegra_gpio", 0444, NULL, tgi,
			    &tegra_dbg_gpio_fops);
}

#else

static inline void tegra_gpio_debuginit(struct tegra_gpio_info *tgi)
{
}

#endif

static const struct dev_pm_ops tegra_gpio_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(tegra_gpio_suspend, tegra_gpio_resume)
};

static int tegra_gpio_probe(struct platform_device *pdev)
{
	struct tegra_gpio_info *tgi;
	struct tegra_gpio_bank *bank;
	unsigned int gpio, i, j;
	int ret;

	tgi = devm_kzalloc(&pdev->dev, sizeof(*tgi), GFP_KERNEL);
	if (!tgi)
		return -ENODEV;

	tgi->soc = of_device_get_match_data(&pdev->dev);
	tgi->dev = &pdev->dev;

	ret = platform_irq_count(pdev);
	if (ret < 0)
		return ret;

	tgi->bank_count = ret;

	if (!tgi->bank_count) {
		dev_err(&pdev->dev, "Missing IRQ resource\n");
		return -ENODEV;
	}

	tgi->gc.label			= "tegra-gpio";
	tgi->gc.request			= tegra_gpio_request;
	tgi->gc.free			= tegra_gpio_free;
	tgi->gc.direction_input		= tegra_gpio_direction_input;
	tgi->gc.get			= tegra_gpio_get;
	tgi->gc.direction_output	= tegra_gpio_direction_output;
	tgi->gc.set			= tegra_gpio_set;
	tgi->gc.get_direction		= tegra_gpio_get_direction;
	tgi->gc.to_irq			= tegra_gpio_to_irq;
	tgi->gc.base			= 0;
	tgi->gc.ngpio			= tgi->bank_count * 32;
	tgi->gc.parent			= &pdev->dev;
	tgi->gc.of_node			= pdev->dev.of_node;

	tgi->ic.name			= "GPIO";
	tgi->ic.irq_ack			= tegra_gpio_irq_ack;
	tgi->ic.irq_mask		= tegra_gpio_irq_mask;
	tgi->ic.irq_unmask		= tegra_gpio_irq_unmask;
	tgi->ic.irq_set_type		= tegra_gpio_irq_set_type;
	tgi->ic.irq_shutdown		= tegra_gpio_irq_shutdown;
#ifdef CONFIG_PM_SLEEP
	tgi->ic.irq_set_wake		= tegra_gpio_irq_set_wake;
#endif

	platform_set_drvdata(pdev, tgi);

	if (tgi->soc->debounce_supported)
		tgi->gc.set_config = tegra_gpio_set_config;

	tgi->bank_info = devm_kcalloc(&pdev->dev, tgi->bank_count,
				      sizeof(*tgi->bank_info), GFP_KERNEL);
	if (!tgi->bank_info)
		return -ENOMEM;

	tgi->irq_domain = irq_domain_add_linear(pdev->dev.of_node,
						tgi->gc.ngpio,
						&irq_domain_simple_ops, NULL);
	if (!tgi->irq_domain)
		return -ENODEV;

	for (i = 0; i < tgi->bank_count; i++) {
		ret = platform_get_irq(pdev, i);
		if (ret < 0)
			return ret;

		bank = &tgi->bank_info[i];
		bank->bank = i;
		bank->irq = ret;
		bank->tgi = tgi;
	}

	tgi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tgi->regs))
		return PTR_ERR(tgi->regs);

	for (i = 0; i < tgi->bank_count; i++) {
		for (j = 0; j < 4; j++) {
			int gpio = tegra_gpio_compose(i, j, 0);

			tegra_gpio_writel(tgi, 0x00, GPIO_INT_ENB(tgi, gpio));
		}
	}

	ret = devm_gpiochip_add_data(&pdev->dev, &tgi->gc, tgi);
	if (ret < 0) {
		irq_domain_remove(tgi->irq_domain);
		return ret;
	}

	for (gpio = 0; gpio < tgi->gc.ngpio; gpio++) {
		int irq = irq_create_mapping(tgi->irq_domain, gpio);
		/* No validity check; all Tegra GPIOs are valid IRQs */

		bank = &tgi->bank_info[GPIO_BANK(gpio)];

		irq_set_chip_data(irq, bank);
		irq_set_chip_and_handler(irq, &tgi->ic, handle_simple_irq);
	}

	for (i = 0; i < tgi->bank_count; i++) {
		bank = &tgi->bank_info[i];

		irq_set_chained_handler_and_data(bank->irq,
						 tegra_gpio_irq_handler, bank);

		for (j = 0; j < 4; j++) {
			spin_lock_init(&bank->lvl_lock[j]);
			spin_lock_init(&bank->dbc_lock[j]);
		}
	}

	tegra_gpio_debuginit(tgi);

	return 0;
}

static const struct tegra_gpio_soc_config tegra20_gpio_config = {
	.bank_stride = 0x80,
	.upper_offset = 0x800,
};

static const struct tegra_gpio_soc_config tegra30_gpio_config = {
	.bank_stride = 0x100,
	.upper_offset = 0x80,
};

static const struct tegra_gpio_soc_config tegra210_gpio_config = {
	.debounce_supported = true,
	.bank_stride = 0x100,
	.upper_offset = 0x80,
};

static const struct of_device_id tegra_gpio_of_match[] = {
	{ .compatible = "nvidia,tegra210-gpio", .data = &tegra210_gpio_config },
	{ .compatible = "nvidia,tegra30-gpio", .data = &tegra30_gpio_config },
	{ .compatible = "nvidia,tegra20-gpio", .data = &tegra20_gpio_config },
	{ },
};

static struct platform_driver tegra_gpio_driver = {
	.driver		= {
		.name	= "tegra-gpio",
		.pm	= &tegra_gpio_pm_ops,
		.of_match_table = tegra_gpio_of_match,
	},
	.probe		= tegra_gpio_probe,
};

static int __init tegra_gpio_init(void)
{
	return platform_driver_register(&tegra_gpio_driver);
}
subsys_initcall(tegra_gpio_init);
