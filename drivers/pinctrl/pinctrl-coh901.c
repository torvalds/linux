/*
 * U300 GPIO module.
 *
 * Copyright (C) 2007-2011 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * This can driver either of the two basic GPIO cores
 * available in the U300 platforms:
 * COH 901 335   - Used in DB3150 (U300 1.0) and DB3200 (U330 1.0)
 * COH 901 571/3 - Used in DB3210 (U365 2.0) and DB3350 (U335 1.0)
 * Author: Linus Walleij <linus.walleij@linaro.org>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <mach/gpio-u300.h>
#include "pinctrl-coh901.h"

/*
 * Register definitions for COH 901 335 variant
 */
#define U300_335_PORT_STRIDE				(0x1C)
/* Port X Pin Data Register 32bit, this is both input and output (R/W) */
#define U300_335_PXPDIR					(0x00)
#define U300_335_PXPDOR					(0x00)
/* Port X Pin Config Register 32bit (R/W) */
#define U300_335_PXPCR					(0x04)
/* This register layout is the same in both blocks */
#define U300_GPIO_PXPCR_ALL_PINS_MODE_MASK		(0x0000FFFFUL)
#define U300_GPIO_PXPCR_PIN_MODE_MASK			(0x00000003UL)
#define U300_GPIO_PXPCR_PIN_MODE_SHIFT			(0x00000002UL)
#define U300_GPIO_PXPCR_PIN_MODE_INPUT			(0x00000000UL)
#define U300_GPIO_PXPCR_PIN_MODE_OUTPUT_PUSH_PULL	(0x00000001UL)
#define U300_GPIO_PXPCR_PIN_MODE_OUTPUT_OPEN_DRAIN	(0x00000002UL)
#define U300_GPIO_PXPCR_PIN_MODE_OUTPUT_OPEN_SOURCE	(0x00000003UL)
/* Port X Interrupt Event Register 32bit (R/W) */
#define U300_335_PXIEV					(0x08)
/* Port X Interrupt Enable Register 32bit (R/W) */
#define U300_335_PXIEN					(0x0C)
/* Port X Interrupt Force Register 32bit (R/W) */
#define U300_335_PXIFR					(0x10)
/* Port X Interrupt Config Register 32bit (R/W) */
#define U300_335_PXICR					(0x14)
/* This register layout is the same in both blocks */
#define U300_GPIO_PXICR_ALL_IRQ_CONFIG_MASK		(0x000000FFUL)
#define U300_GPIO_PXICR_IRQ_CONFIG_MASK			(0x00000001UL)
#define U300_GPIO_PXICR_IRQ_CONFIG_FALLING_EDGE		(0x00000000UL)
#define U300_GPIO_PXICR_IRQ_CONFIG_RISING_EDGE		(0x00000001UL)
/* Port X Pull-up Enable Register 32bit (R/W) */
#define U300_335_PXPER					(0x18)
/* This register layout is the same in both blocks */
#define U300_GPIO_PXPER_ALL_PULL_UP_DISABLE_MASK	(0x000000FFUL)
#define U300_GPIO_PXPER_PULL_UP_DISABLE			(0x00000001UL)
/* Control Register 32bit (R/W) */
#define U300_335_CR					(0x54)
#define U300_335_CR_BLOCK_CLOCK_ENABLE			(0x00000001UL)

/*
 * Register definitions for COH 901 571 / 3 variant
 */
#define U300_571_PORT_STRIDE				(0x30)
/*
 * Control Register 32bit (R/W)
 * bit 15-9 (mask 0x0000FE00) contains the number of cores. 8*cores
 * gives the number of GPIO pins.
 * bit 8-2  (mask 0x000001FC) contains the core version ID.
 */
#define U300_571_CR					(0x00)
#define U300_571_CR_SYNC_SEL_ENABLE			(0x00000002UL)
#define U300_571_CR_BLOCK_CLKRQ_ENABLE			(0x00000001UL)
/*
 * These registers have the same layout and function as the corresponding
 * COH 901 335 registers, just at different offset.
 */
#define U300_571_PXPDIR					(0x04)
#define U300_571_PXPDOR					(0x08)
#define U300_571_PXPCR					(0x0C)
#define U300_571_PXPER					(0x10)
#define U300_571_PXIEV					(0x14)
#define U300_571_PXIEN					(0x18)
#define U300_571_PXIFR					(0x1C)
#define U300_571_PXICR					(0x20)

/* 8 bits per port, no version has more than 7 ports */
#define U300_GPIO_PINS_PER_PORT 8
#define U300_GPIO_MAX (U300_GPIO_PINS_PER_PORT * 7)

struct u300_gpio {
	struct gpio_chip chip;
	struct list_head port_list;
	struct clk *clk;
	struct resource *memres;
	void __iomem *base;
	struct device *dev;
	int irq_base;
	u32 stride;
	/* Register offsets */
	u32 pcr;
	u32 dor;
	u32 dir;
	u32 per;
	u32 icr;
	u32 ien;
	u32 iev;
};

struct u300_gpio_port {
	struct list_head node;
	struct u300_gpio *gpio;
	char name[8];
	int irq;
	int number;
	u8 toggle_edge_mode;
};

/*
 * Macro to expand to read a specific register found in the "gpio"
 * struct. It requires the struct u300_gpio *gpio variable to exist in
 * its context. It calculates the port offset from the given pin
 * offset, muliplies by the port stride and adds the register offset
 * so it provides a pointer to the desired register.
 */
#define U300_PIN_REG(pin, reg) \
	(gpio->base + (pin >> 3) * gpio->stride + gpio->reg)

/*
 * Provides a bitmask for a specific gpio pin inside an 8-bit GPIO
 * register.
 */
#define U300_PIN_BIT(pin) \
	(1 << (pin & 0x07))

struct u300_gpio_confdata {
	u16 bias_mode;
	bool output;
	int outval;
};

/* BS335 has seven ports of 8 bits each = GPIO pins 0..55 */
#define BS335_GPIO_NUM_PORTS 7
/* BS365 has five ports of 8 bits each = GPIO pins 0..39 */
#define BS365_GPIO_NUM_PORTS 5

#define U300_FLOATING_INPUT { \
	.bias_mode = PIN_CONFIG_BIAS_HIGH_IMPEDANCE, \
	.output = false, \
}

#define U300_PULL_UP_INPUT { \
	.bias_mode = PIN_CONFIG_BIAS_PULL_UP, \
	.output = false, \
}

#define U300_OUTPUT_LOW { \
	.output = true, \
	.outval = 0, \
}

#define U300_OUTPUT_HIGH { \
	.output = true, \
	.outval = 1, \
}


/* Initial configuration */
static const struct __initdata u300_gpio_confdata
bs335_gpio_config[BS335_GPIO_NUM_PORTS][U300_GPIO_PINS_PER_PORT] = {
	/* Port 0, pins 0-7 */
	{
		U300_FLOATING_INPUT,
		U300_OUTPUT_HIGH,
		U300_FLOATING_INPUT,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
	},
	/* Port 1, pins 0-7 */
	{
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
		U300_PULL_UP_INPUT,
		U300_FLOATING_INPUT,
		U300_OUTPUT_HIGH,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
	},
	/* Port 2, pins 0-7 */
	{
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_OUTPUT_LOW,
		U300_PULL_UP_INPUT,
		U300_OUTPUT_LOW,
		U300_PULL_UP_INPUT,
	},
	/* Port 3, pins 0-7 */
	{
		U300_PULL_UP_INPUT,
		U300_OUTPUT_LOW,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
	},
	/* Port 4, pins 0-7 */
	{
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
	},
	/* Port 5, pins 0-7 */
	{
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
	},
	/* Port 6, pind 0-7 */
	{
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
	}
};

static const struct __initdata u300_gpio_confdata
bs365_gpio_config[BS365_GPIO_NUM_PORTS][U300_GPIO_PINS_PER_PORT] = {
	/* Port 0, pins 0-7 */
	{
		U300_FLOATING_INPUT,
		U300_OUTPUT_LOW,
		U300_FLOATING_INPUT,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
		U300_PULL_UP_INPUT,
		U300_FLOATING_INPUT,
	},
	/* Port 1, pins 0-7 */
	{
		U300_OUTPUT_LOW,
		U300_FLOATING_INPUT,
		U300_OUTPUT_LOW,
		U300_FLOATING_INPUT,
		U300_FLOATING_INPUT,
		U300_OUTPUT_HIGH,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
	},
	/* Port 2, pins 0-7 */
	{
		U300_FLOATING_INPUT,
		U300_PULL_UP_INPUT,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
	},
	/* Port 3, pins 0-7 */
	{
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
	},
	/* Port 4, pins 0-7 */
	{
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
		U300_PULL_UP_INPUT,
		/* These 4 pins doesn't exist on DB3210 */
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
		U300_OUTPUT_LOW,
	}
};

/**
 * to_u300_gpio() - get the pointer to u300_gpio
 * @chip: the gpio chip member of the structure u300_gpio
 */
static inline struct u300_gpio *to_u300_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct u300_gpio, chip);
}

static int u300_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	/*
	 * Map back to global GPIO space and request muxing, the direction
	 * parameter does not matter for this controller.
	 */
	int gpio = chip->base + offset;

	return pinctrl_request_gpio(gpio);
}

static void u300_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;

	pinctrl_free_gpio(gpio);
}

static int u300_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct u300_gpio *gpio = to_u300_gpio(chip);

	return readl(U300_PIN_REG(offset, dir)) & U300_PIN_BIT(offset);
}

static void u300_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct u300_gpio *gpio = to_u300_gpio(chip);
	unsigned long flags;
	u32 val;

	local_irq_save(flags);

	val = readl(U300_PIN_REG(offset, dor));
	if (value)
		writel(val | U300_PIN_BIT(offset), U300_PIN_REG(offset, dor));
	else
		writel(val & ~U300_PIN_BIT(offset), U300_PIN_REG(offset, dor));

	local_irq_restore(flags);
}

static int u300_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct u300_gpio *gpio = to_u300_gpio(chip);
	unsigned long flags;
	u32 val;

	local_irq_save(flags);
	val = readl(U300_PIN_REG(offset, pcr));
	/* Mask out this pin, note 2 bits per setting */
	val &= ~(U300_GPIO_PXPCR_PIN_MODE_MASK << ((offset & 0x07) << 1));
	writel(val, U300_PIN_REG(offset, pcr));
	local_irq_restore(flags);
	return 0;
}

static int u300_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				      int value)
{
	struct u300_gpio *gpio = to_u300_gpio(chip);
	unsigned long flags;
	u32 oldmode;
	u32 val;

	local_irq_save(flags);
	val = readl(U300_PIN_REG(offset, pcr));
	/*
	 * Drive mode must be set by the special mode set function, set
	 * push/pull mode by default if no mode has been selected.
	 */
	oldmode = val & (U300_GPIO_PXPCR_PIN_MODE_MASK <<
			 ((offset & 0x07) << 1));
	/* mode = 0 means input, else some mode is already set */
	if (oldmode == 0) {
		val &= ~(U300_GPIO_PXPCR_PIN_MODE_MASK <<
			 ((offset & 0x07) << 1));
		val |= (U300_GPIO_PXPCR_PIN_MODE_OUTPUT_PUSH_PULL
			<< ((offset & 0x07) << 1));
		writel(val, U300_PIN_REG(offset, pcr));
	}
	u300_gpio_set(chip, offset, value);
	local_irq_restore(flags);
	return 0;
}

static int u300_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct u300_gpio *gpio = to_u300_gpio(chip);
	int retirq = gpio->irq_base + offset;

	dev_dbg(gpio->dev, "request IRQ for GPIO %d, return %d\n", offset,
		retirq);
	return retirq;
}

/* Returning -EINVAL means "supported but not available" */
int u300_gpio_config_get(struct gpio_chip *chip,
			 unsigned offset,
			 unsigned long *config)
{
	struct u300_gpio *gpio = to_u300_gpio(chip);
	enum pin_config_param param = (enum pin_config_param) *config;
	bool biasmode;
	u32 drmode;

	/* One bit per pin, clamp to bool range */
	biasmode = !!(readl(U300_PIN_REG(offset, per)) & U300_PIN_BIT(offset));

	/* Mask out the two bits for this pin and shift to bits 0,1 */
	drmode = readl(U300_PIN_REG(offset, pcr));
	drmode &= (U300_GPIO_PXPCR_PIN_MODE_MASK << ((offset & 0x07) << 1));
	drmode >>= ((offset & 0x07) << 1);

	switch(param) {
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		*config = 0;
		if (biasmode)
			return 0;
		else
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		*config = 0;
		if (!biasmode)
			return 0;
		else
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		*config = 0;
		if (drmode == U300_GPIO_PXPCR_PIN_MODE_OUTPUT_PUSH_PULL)
			return 0;
		else
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		*config = 0;
		if (drmode == U300_GPIO_PXPCR_PIN_MODE_OUTPUT_OPEN_DRAIN)
			return 0;
		else
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_OPEN_SOURCE:
		*config = 0;
		if (drmode == U300_GPIO_PXPCR_PIN_MODE_OUTPUT_OPEN_SOURCE)
			return 0;
		else
			return -EINVAL;
		break;
	default:
		break;
	}
	return -ENOTSUPP;
}

int u300_gpio_config_set(struct gpio_chip *chip, unsigned offset,
			 enum pin_config_param param)
{
	struct u300_gpio *gpio = to_u300_gpio(chip);
	unsigned long flags;
	u32 val;

	local_irq_save(flags);
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		val = readl(U300_PIN_REG(offset, per));
		writel(val | U300_PIN_BIT(offset), U300_PIN_REG(offset, per));
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		val = readl(U300_PIN_REG(offset, per));
		writel(val & ~U300_PIN_BIT(offset), U300_PIN_REG(offset, per));
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		val = readl(U300_PIN_REG(offset, pcr));
		val &= ~(U300_GPIO_PXPCR_PIN_MODE_MASK
			 << ((offset & 0x07) << 1));
		val |= (U300_GPIO_PXPCR_PIN_MODE_OUTPUT_PUSH_PULL
			<< ((offset & 0x07) << 1));
		writel(val, U300_PIN_REG(offset, pcr));
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		val = readl(U300_PIN_REG(offset, pcr));
		val &= ~(U300_GPIO_PXPCR_PIN_MODE_MASK
			 << ((offset & 0x07) << 1));
		val |= (U300_GPIO_PXPCR_PIN_MODE_OUTPUT_OPEN_DRAIN
			<< ((offset & 0x07) << 1));
		writel(val, U300_PIN_REG(offset, pcr));
		break;
	case PIN_CONFIG_DRIVE_OPEN_SOURCE:
		val = readl(U300_PIN_REG(offset, pcr));
		val &= ~(U300_GPIO_PXPCR_PIN_MODE_MASK
			 << ((offset & 0x07) << 1));
		val |= (U300_GPIO_PXPCR_PIN_MODE_OUTPUT_OPEN_SOURCE
			<< ((offset & 0x07) << 1));
		writel(val, U300_PIN_REG(offset, pcr));
		break;
	default:
		local_irq_restore(flags);
		dev_err(gpio->dev, "illegal configuration requested\n");
		return -EINVAL;
	}
	local_irq_restore(flags);
	return 0;
}

static struct gpio_chip u300_gpio_chip = {
	.label			= "u300-gpio-chip",
	.owner			= THIS_MODULE,
	.request		= u300_gpio_request,
	.free			= u300_gpio_free,
	.get			= u300_gpio_get,
	.set			= u300_gpio_set,
	.direction_input	= u300_gpio_direction_input,
	.direction_output	= u300_gpio_direction_output,
	.to_irq			= u300_gpio_to_irq,
};

static void u300_toggle_trigger(struct u300_gpio *gpio, unsigned offset)
{
	u32 val;

	val = readl(U300_PIN_REG(offset, icr));
	/* Set mode depending on state */
	if (u300_gpio_get(&gpio->chip, offset)) {
		/* High now, let's trigger on falling edge next then */
		writel(val & ~U300_PIN_BIT(offset), U300_PIN_REG(offset, icr));
		dev_dbg(gpio->dev, "next IRQ on falling edge on pin %d\n",
			offset);
	} else {
		/* Low now, let's trigger on rising edge next then */
		writel(val | U300_PIN_BIT(offset), U300_PIN_REG(offset, icr));
		dev_dbg(gpio->dev, "next IRQ on rising edge on pin %d\n",
			offset);
	}
}

static int u300_gpio_irq_type(struct irq_data *d, unsigned trigger)
{
	struct u300_gpio_port *port = irq_data_get_irq_chip_data(d);
	struct u300_gpio *gpio = port->gpio;
	int offset = d->irq - gpio->irq_base;
	u32 val;

	if ((trigger & IRQF_TRIGGER_RISING) &&
	    (trigger & IRQF_TRIGGER_FALLING)) {
		/*
		 * The GPIO block can only trigger on falling OR rising edges,
		 * not both. So we need to toggle the mode whenever the pin
		 * goes from one state to the other with a special state flag
		 */
		dev_dbg(gpio->dev,
			"trigger on both rising and falling edge on pin %d\n",
			offset);
		port->toggle_edge_mode |= U300_PIN_BIT(offset);
		u300_toggle_trigger(gpio, offset);
	} else if (trigger & IRQF_TRIGGER_RISING) {
		dev_dbg(gpio->dev, "trigger on rising edge on pin %d\n",
			offset);
		val = readl(U300_PIN_REG(offset, icr));
		writel(val | U300_PIN_BIT(offset), U300_PIN_REG(offset, icr));
		port->toggle_edge_mode &= ~U300_PIN_BIT(offset);
	} else if (trigger & IRQF_TRIGGER_FALLING) {
		dev_dbg(gpio->dev, "trigger on falling edge on pin %d\n",
			offset);
		val = readl(U300_PIN_REG(offset, icr));
		writel(val & ~U300_PIN_BIT(offset), U300_PIN_REG(offset, icr));
		port->toggle_edge_mode &= ~U300_PIN_BIT(offset);
	}

	return 0;
}

static void u300_gpio_irq_enable(struct irq_data *d)
{
	struct u300_gpio_port *port = irq_data_get_irq_chip_data(d);
	struct u300_gpio *gpio = port->gpio;
	int offset = d->irq - gpio->irq_base;
	u32 val;
	unsigned long flags;

	local_irq_save(flags);
	val = readl(U300_PIN_REG(offset, ien));
	writel(val | U300_PIN_BIT(offset), U300_PIN_REG(offset, ien));
	local_irq_restore(flags);
}

static void u300_gpio_irq_disable(struct irq_data *d)
{
	struct u300_gpio_port *port = irq_data_get_irq_chip_data(d);
	struct u300_gpio *gpio = port->gpio;
	int offset = d->irq - gpio->irq_base;
	u32 val;
	unsigned long flags;

	local_irq_save(flags);
	val = readl(U300_PIN_REG(offset, ien));
	writel(val & ~U300_PIN_BIT(offset), U300_PIN_REG(offset, ien));
	local_irq_restore(flags);
}

static struct irq_chip u300_gpio_irqchip = {
	.name			= "u300-gpio-irqchip",
	.irq_enable		= u300_gpio_irq_enable,
	.irq_disable		= u300_gpio_irq_disable,
	.irq_set_type		= u300_gpio_irq_type,

};

static void u300_gpio_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct u300_gpio_port *port = irq_get_handler_data(irq);
	struct u300_gpio *gpio = port->gpio;
	int pinoffset = port->number << 3; /* get the right stride */
	unsigned long val;

	desc->irq_data.chip->irq_ack(&desc->irq_data);
	/* Read event register */
	val = readl(U300_PIN_REG(pinoffset, iev));
	/* Mask relevant bits */
	val &= 0xFFU; /* 8 bits per port */
	/* ACK IRQ (clear event) */
	writel(val, U300_PIN_REG(pinoffset, iev));

	/* Call IRQ handler */
	if (val != 0) {
		int irqoffset;

		for_each_set_bit(irqoffset, &val, U300_GPIO_PINS_PER_PORT) {
			int pin_irq = gpio->irq_base + (port->number << 3)
				+ irqoffset;
			int offset = pinoffset + irqoffset;

			dev_dbg(gpio->dev, "GPIO IRQ %d on pin %d\n",
				pin_irq, offset);
			generic_handle_irq(pin_irq);
			/*
			 * Triggering IRQ on both rising and falling edge
			 * needs mockery
			 */
			if (port->toggle_edge_mode & U300_PIN_BIT(offset))
				u300_toggle_trigger(gpio, offset);
		}
	}

	desc->irq_data.chip->irq_unmask(&desc->irq_data);
}

static void __init u300_gpio_init_pin(struct u300_gpio *gpio,
				      int offset,
				      const struct u300_gpio_confdata *conf)
{
	/* Set mode: input or output */
	if (conf->output) {
		u300_gpio_direction_output(&gpio->chip, offset, conf->outval);

		/* Deactivate bias mode for output */
		u300_gpio_config_set(&gpio->chip, offset,
				     PIN_CONFIG_BIAS_HIGH_IMPEDANCE);

		/* Set drive mode for output */
		u300_gpio_config_set(&gpio->chip, offset,
				     PIN_CONFIG_DRIVE_PUSH_PULL);

		dev_dbg(gpio->dev, "set up pin %d as output, value: %d\n",
			offset, conf->outval);
	} else {
		u300_gpio_direction_input(&gpio->chip, offset);

		/* Always set output low on input pins */
		u300_gpio_set(&gpio->chip, offset, 0);

		/* Set bias mode for input */
		u300_gpio_config_set(&gpio->chip, offset, conf->bias_mode);

		dev_dbg(gpio->dev, "set up pin %d as input, bias: %04x\n",
			offset, conf->bias_mode);
	}
}

static void __init u300_gpio_init_coh901571(struct u300_gpio *gpio,
				     struct u300_gpio_platform *plat)
{
	int i, j;

	/* Write default config and values to all pins */
	for (i = 0; i < plat->ports; i++) {
		for (j = 0; j < 8; j++) {
			const struct u300_gpio_confdata *conf;
			int offset = (i*8) + j;

			if (plat->variant == U300_GPIO_COH901571_3_BS335)
				conf = &bs335_gpio_config[i][j];
			else if (plat->variant == U300_GPIO_COH901571_3_BS365)
				conf = &bs365_gpio_config[i][j];
			else
				break;

			u300_gpio_init_pin(gpio, offset, conf);
		}
	}
}

static inline void u300_gpio_free_ports(struct u300_gpio *gpio)
{
	struct u300_gpio_port *port;
	struct list_head *p, *n;

	list_for_each_safe(p, n, &gpio->port_list) {
		port = list_entry(p, struct u300_gpio_port, node);
		list_del(&port->node);
		kfree(port);
	}
}

static int __init u300_gpio_probe(struct platform_device *pdev)
{
	struct u300_gpio_platform *plat = dev_get_platdata(&pdev->dev);
	struct u300_gpio *gpio;
	int err = 0;
	int portno;
	u32 val;
	u32 ifr;
	int i;

	gpio = kzalloc(sizeof(struct u300_gpio), GFP_KERNEL);
	if (gpio == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	gpio->chip = u300_gpio_chip;
	gpio->chip.ngpio = plat->ports * U300_GPIO_PINS_PER_PORT;
	gpio->irq_base = plat->gpio_irq_base;
	gpio->chip.dev = &pdev->dev;
	gpio->chip.base = plat->gpio_base;
	gpio->dev = &pdev->dev;

	/* Get GPIO clock */
	gpio->clk = clk_get(gpio->dev, NULL);
	if (IS_ERR(gpio->clk)) {
		err = PTR_ERR(gpio->clk);
		dev_err(gpio->dev, "could not get GPIO clock\n");
		goto err_no_clk;
	}
	err = clk_enable(gpio->clk);
	if (err) {
		dev_err(gpio->dev, "could not enable GPIO clock\n");
		goto err_no_clk_enable;
	}

	gpio->memres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!gpio->memres) {
		dev_err(gpio->dev, "could not get GPIO memory resource\n");
		err = -ENODEV;
		goto err_no_resource;
	}

	if (!request_mem_region(gpio->memres->start,
				resource_size(gpio->memres),
				"GPIO Controller")) {
		err = -ENODEV;
		goto err_no_ioregion;
	}

	gpio->base = ioremap(gpio->memres->start, resource_size(gpio->memres));
	if (!gpio->base) {
		err = -ENOMEM;
		goto err_no_ioremap;
	}

	if (plat->variant == U300_GPIO_COH901335) {
		dev_info(gpio->dev,
			 "initializing GPIO Controller COH 901 335\n");
		gpio->stride = U300_335_PORT_STRIDE;
		gpio->pcr = U300_335_PXPCR;
		gpio->dor = U300_335_PXPDOR;
		gpio->dir = U300_335_PXPDIR;
		gpio->per = U300_335_PXPER;
		gpio->icr = U300_335_PXICR;
		gpio->ien = U300_335_PXIEN;
		gpio->iev = U300_335_PXIEV;
		ifr = U300_335_PXIFR;

		/* Turn on the GPIO block */
		writel(U300_335_CR_BLOCK_CLOCK_ENABLE,
		       gpio->base + U300_335_CR);
	} else if (plat->variant == U300_GPIO_COH901571_3_BS335 ||
		   plat->variant == U300_GPIO_COH901571_3_BS365) {
		dev_info(gpio->dev,
			 "initializing GPIO Controller COH 901 571/3\n");
		gpio->stride = U300_571_PORT_STRIDE;
		gpio->pcr = U300_571_PXPCR;
		gpio->dor = U300_571_PXPDOR;
		gpio->dir = U300_571_PXPDIR;
		gpio->per = U300_571_PXPER;
		gpio->icr = U300_571_PXICR;
		gpio->ien = U300_571_PXIEN;
		gpio->iev = U300_571_PXIEV;
		ifr = U300_571_PXIFR;

		val = readl(gpio->base + U300_571_CR);
		dev_info(gpio->dev, "COH901571/3 block version: %d, " \
			 "number of cores: %d totalling %d pins\n",
			 ((val & 0x000001FC) >> 2),
			 ((val & 0x0000FE00) >> 9),
			 ((val & 0x0000FE00) >> 9) * 8);
		writel(U300_571_CR_BLOCK_CLKRQ_ENABLE,
		       gpio->base + U300_571_CR);
		u300_gpio_init_coh901571(gpio, plat);
	} else {
		dev_err(gpio->dev, "unknown block variant\n");
		err = -ENODEV;
		goto err_unknown_variant;
	}

	/* Add each port with its IRQ separately */
	INIT_LIST_HEAD(&gpio->port_list);
	for (portno = 0 ; portno < plat->ports; portno++) {
		struct u300_gpio_port *port =
			kmalloc(sizeof(struct u300_gpio_port), GFP_KERNEL);

		if (!port) {
			dev_err(gpio->dev, "out of memory\n");
			err = -ENOMEM;
			goto err_no_port;
		}

		snprintf(port->name, 8, "gpio%d", portno);
		port->number = portno;
		port->gpio = gpio;

		port->irq = platform_get_irq_byname(pdev,
						    port->name);

		dev_dbg(gpio->dev, "register IRQ %d for %s\n", port->irq,
			port->name);

		irq_set_chained_handler(port->irq, u300_gpio_irq_handler);
		irq_set_handler_data(port->irq, port);

		/* For each GPIO pin set the unique IRQ handler */
		for (i = 0; i < U300_GPIO_PINS_PER_PORT; i++) {
			int irqno = gpio->irq_base + (portno << 3) + i;

			dev_dbg(gpio->dev, "handler for IRQ %d on %s\n",
				irqno, port->name);
			irq_set_chip_and_handler(irqno, &u300_gpio_irqchip,
						 handle_simple_irq);
			set_irq_flags(irqno, IRQF_VALID);
			irq_set_chip_data(irqno, port);
		}

		/* Turns off irq force (test register) for this port */
		writel(0x0, gpio->base + portno * gpio->stride + ifr);

		list_add_tail(&port->node, &gpio->port_list);
	}
	dev_dbg(gpio->dev, "initialized %d GPIO ports\n", portno);

	err = gpiochip_add(&gpio->chip);
	if (err) {
		dev_err(gpio->dev, "unable to add gpiochip: %d\n", err);
		goto err_no_chip;
	}

	/* Spawn pin controller device as child of the GPIO, pass gpio chip */
	plat->pinctrl_device->dev.platform_data = &gpio->chip;
	err = platform_device_register(plat->pinctrl_device);
	if (err)
		goto err_no_pinctrl;

	platform_set_drvdata(pdev, gpio);

	return 0;

err_no_pinctrl:
	err = gpiochip_remove(&gpio->chip);
err_no_chip:
err_no_port:
	u300_gpio_free_ports(gpio);
err_unknown_variant:
	iounmap(gpio->base);
err_no_ioremap:
	release_mem_region(gpio->memres->start, resource_size(gpio->memres));
err_no_ioregion:
err_no_resource:
	clk_disable(gpio->clk);
err_no_clk_enable:
	clk_put(gpio->clk);
err_no_clk:
	kfree(gpio);
	dev_info(&pdev->dev, "module ERROR:%d\n", err);
	return err;
}

static int __exit u300_gpio_remove(struct platform_device *pdev)
{
	struct u300_gpio_platform *plat = dev_get_platdata(&pdev->dev);
	struct u300_gpio *gpio = platform_get_drvdata(pdev);
	int err;

	/* Turn off the GPIO block */
	if (plat->variant == U300_GPIO_COH901335)
		writel(0x00000000U, gpio->base + U300_335_CR);
	if (plat->variant == U300_GPIO_COH901571_3_BS335 ||
	    plat->variant == U300_GPIO_COH901571_3_BS365)
		writel(0x00000000U, gpio->base + U300_571_CR);

	err = gpiochip_remove(&gpio->chip);
	if (err < 0) {
		dev_err(gpio->dev, "unable to remove gpiochip: %d\n", err);
		return err;
	}
	u300_gpio_free_ports(gpio);
	iounmap(gpio->base);
	release_mem_region(gpio->memres->start,
			   resource_size(gpio->memres));
	clk_disable(gpio->clk);
	clk_put(gpio->clk);
	platform_set_drvdata(pdev, NULL);
	kfree(gpio);
	return 0;
}

static struct platform_driver u300_gpio_driver = {
	.driver		= {
		.name	= "u300-gpio",
	},
	.remove		= __exit_p(u300_gpio_remove),
};

static int __init u300_gpio_init(void)
{
	return platform_driver_probe(&u300_gpio_driver, u300_gpio_probe);
}

static void __exit u300_gpio_exit(void)
{
	platform_driver_unregister(&u300_gpio_driver);
}

arch_initcall(u300_gpio_init);
module_exit(u300_gpio_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij@stericsson.com>");
MODULE_DESCRIPTION("ST-Ericsson AB COH 901 335/COH 901 571/3 GPIO driver");
MODULE_LICENSE("GPL");
