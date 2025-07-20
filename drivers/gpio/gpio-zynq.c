// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Xilinx Zynq GPIO device driver
 *
 * Copyright (C) 2009 - 2014 Xilinx, Inc.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>

#define DRIVER_NAME "zynq-gpio"

/* Maximum banks */
#define ZYNQ_GPIO_MAX_BANK	4
#define ZYNQMP_GPIO_MAX_BANK	6
#define VERSAL_GPIO_MAX_BANK	4
#define PMC_GPIO_MAX_BANK	5
#define VERSAL_UNUSED_BANKS	2

#define ZYNQ_GPIO_BANK0_NGPIO	32
#define ZYNQ_GPIO_BANK1_NGPIO	22
#define ZYNQ_GPIO_BANK2_NGPIO	32
#define ZYNQ_GPIO_BANK3_NGPIO	32

#define ZYNQMP_GPIO_BANK0_NGPIO 26
#define ZYNQMP_GPIO_BANK1_NGPIO 26
#define ZYNQMP_GPIO_BANK2_NGPIO 26
#define ZYNQMP_GPIO_BANK3_NGPIO 32
#define ZYNQMP_GPIO_BANK4_NGPIO 32
#define ZYNQMP_GPIO_BANK5_NGPIO 32

#define	ZYNQ_GPIO_NR_GPIOS	118
#define	ZYNQMP_GPIO_NR_GPIOS	174

#define ZYNQ_GPIO_BANK0_PIN_MIN(str)	0
#define ZYNQ_GPIO_BANK0_PIN_MAX(str)	(ZYNQ_GPIO_BANK0_PIN_MIN(str) + \
					ZYNQ##str##_GPIO_BANK0_NGPIO - 1)
#define ZYNQ_GPIO_BANK1_PIN_MIN(str)	(ZYNQ_GPIO_BANK0_PIN_MAX(str) + 1)
#define ZYNQ_GPIO_BANK1_PIN_MAX(str)	(ZYNQ_GPIO_BANK1_PIN_MIN(str) + \
					ZYNQ##str##_GPIO_BANK1_NGPIO - 1)
#define ZYNQ_GPIO_BANK2_PIN_MIN(str)	(ZYNQ_GPIO_BANK1_PIN_MAX(str) + 1)
#define ZYNQ_GPIO_BANK2_PIN_MAX(str)	(ZYNQ_GPIO_BANK2_PIN_MIN(str) + \
					ZYNQ##str##_GPIO_BANK2_NGPIO - 1)
#define ZYNQ_GPIO_BANK3_PIN_MIN(str)	(ZYNQ_GPIO_BANK2_PIN_MAX(str) + 1)
#define ZYNQ_GPIO_BANK3_PIN_MAX(str)	(ZYNQ_GPIO_BANK3_PIN_MIN(str) + \
					ZYNQ##str##_GPIO_BANK3_NGPIO - 1)
#define ZYNQ_GPIO_BANK4_PIN_MIN(str)	(ZYNQ_GPIO_BANK3_PIN_MAX(str) + 1)
#define ZYNQ_GPIO_BANK4_PIN_MAX(str)	(ZYNQ_GPIO_BANK4_PIN_MIN(str) + \
					ZYNQ##str##_GPIO_BANK4_NGPIO - 1)
#define ZYNQ_GPIO_BANK5_PIN_MIN(str)	(ZYNQ_GPIO_BANK4_PIN_MAX(str) + 1)
#define ZYNQ_GPIO_BANK5_PIN_MAX(str)	(ZYNQ_GPIO_BANK5_PIN_MIN(str) + \
					ZYNQ##str##_GPIO_BANK5_NGPIO - 1)

/* Register offsets for the GPIO device */
/* LSW Mask & Data -WO */
#define ZYNQ_GPIO_DATA_LSW_OFFSET(BANK)	(0x000 + (8 * BANK))
/* MSW Mask & Data -WO */
#define ZYNQ_GPIO_DATA_MSW_OFFSET(BANK)	(0x004 + (8 * BANK))
/* Data Register-RW */
#define ZYNQ_GPIO_DATA_OFFSET(BANK)	(0x040 + (4 * BANK))
#define ZYNQ_GPIO_DATA_RO_OFFSET(BANK)	(0x060 + (4 * BANK))
/* Direction mode reg-RW */
#define ZYNQ_GPIO_DIRM_OFFSET(BANK)	(0x204 + (0x40 * BANK))
/* Output enable reg-RW */
#define ZYNQ_GPIO_OUTEN_OFFSET(BANK)	(0x208 + (0x40 * BANK))
/* Interrupt mask reg-RO */
#define ZYNQ_GPIO_INTMASK_OFFSET(BANK)	(0x20C + (0x40 * BANK))
/* Interrupt enable reg-WO */
#define ZYNQ_GPIO_INTEN_OFFSET(BANK)	(0x210 + (0x40 * BANK))
/* Interrupt disable reg-WO */
#define ZYNQ_GPIO_INTDIS_OFFSET(BANK)	(0x214 + (0x40 * BANK))
/* Interrupt status reg-RO */
#define ZYNQ_GPIO_INTSTS_OFFSET(BANK)	(0x218 + (0x40 * BANK))
/* Interrupt type reg-RW */
#define ZYNQ_GPIO_INTTYPE_OFFSET(BANK)	(0x21C + (0x40 * BANK))
/* Interrupt polarity reg-RW */
#define ZYNQ_GPIO_INTPOL_OFFSET(BANK)	(0x220 + (0x40 * BANK))
/* Interrupt on any, reg-RW */
#define ZYNQ_GPIO_INTANY_OFFSET(BANK)	(0x224 + (0x40 * BANK))

/* Disable all interrupts mask */
#define ZYNQ_GPIO_IXR_DISABLE_ALL	0xFFFFFFFF

/* Mid pin number of a bank */
#define ZYNQ_GPIO_MID_PIN_NUM 16

/* GPIO upper 16 bit mask */
#define ZYNQ_GPIO_UPPER_MASK 0xFFFF0000

/* set to differentiate zynq from zynqmp, 0=zynqmp, 1=zynq */
#define ZYNQ_GPIO_QUIRK_IS_ZYNQ	BIT(0)
#define GPIO_QUIRK_DATA_RO_BUG	BIT(1)
#define GPIO_QUIRK_VERSAL	BIT(2)

struct gpio_regs {
	u32 datamsw[ZYNQMP_GPIO_MAX_BANK];
	u32 datalsw[ZYNQMP_GPIO_MAX_BANK];
	u32 dirm[ZYNQMP_GPIO_MAX_BANK];
	u32 outen[ZYNQMP_GPIO_MAX_BANK];
	u32 int_en[ZYNQMP_GPIO_MAX_BANK];
	u32 int_dis[ZYNQMP_GPIO_MAX_BANK];
	u32 int_type[ZYNQMP_GPIO_MAX_BANK];
	u32 int_polarity[ZYNQMP_GPIO_MAX_BANK];
	u32 int_any[ZYNQMP_GPIO_MAX_BANK];
};

/**
 * struct zynq_gpio - gpio device private data structure
 * @chip:	instance of the gpio_chip
 * @base_addr:	base address of the GPIO device
 * @clk:	clock resource for this controller
 * @irq:	interrupt for the GPIO device
 * @p_data:	pointer to platform data
 * @context:	context registers
 * @dirlock:	lock used for direction in/out synchronization
 */
struct zynq_gpio {
	struct gpio_chip chip;
	void __iomem *base_addr;
	struct clk *clk;
	int irq;
	const struct zynq_platform_data *p_data;
	struct gpio_regs context;
	spinlock_t dirlock; /* lock */
};

/**
 * struct zynq_platform_data -  zynq gpio platform data structure
 * @label:	string to store in gpio->label
 * @quirks:	Flags is used to identify the platform
 * @ngpio:	max number of gpio pins
 * @max_bank:	maximum number of gpio banks
 * @bank_min:	this array represents bank's min pin
 * @bank_max:	this array represents bank's max pin
 */
struct zynq_platform_data {
	const char *label;
	u32 quirks;
	u16 ngpio;
	int max_bank;
	int bank_min[ZYNQMP_GPIO_MAX_BANK];
	int bank_max[ZYNQMP_GPIO_MAX_BANK];
};

static const struct irq_chip zynq_gpio_level_irqchip;
static const struct irq_chip zynq_gpio_edge_irqchip;

/**
 * zynq_gpio_is_zynq - test if HW is zynq or zynqmp
 * @gpio:	Pointer to driver data struct
 *
 * Return: 0 if zynqmp, 1 if zynq.
 */
static int zynq_gpio_is_zynq(struct zynq_gpio *gpio)
{
	return !!(gpio->p_data->quirks & ZYNQ_GPIO_QUIRK_IS_ZYNQ);
}

/**
 * gpio_data_ro_bug - test if HW bug exists or not
 * @gpio:       Pointer to driver data struct
 *
 * Return: 0 if bug doesnot exist, 1 if bug exists.
 */
static int gpio_data_ro_bug(struct zynq_gpio *gpio)
{
	return !!(gpio->p_data->quirks & GPIO_QUIRK_DATA_RO_BUG);
}

/**
 * zynq_gpio_get_bank_pin - Get the bank number and pin number within that bank
 * for a given pin in the GPIO device
 * @pin_num:	gpio pin number within the device
 * @bank_num:	an output parameter used to return the bank number of the gpio
 *		pin
 * @bank_pin_num: an output parameter used to return pin number within a bank
 *		  for the given gpio pin
 * @gpio:	gpio device data structure
 *
 * Returns the bank number and pin offset within the bank.
 */
static inline void zynq_gpio_get_bank_pin(unsigned int pin_num,
					  unsigned int *bank_num,
					  unsigned int *bank_pin_num,
					  struct zynq_gpio *gpio)
{
	int bank;

	for (bank = 0; bank < gpio->p_data->max_bank; bank++) {
		if ((pin_num >= gpio->p_data->bank_min[bank]) &&
		    (pin_num <= gpio->p_data->bank_max[bank])) {
			*bank_num = bank;
			*bank_pin_num = pin_num -
					gpio->p_data->bank_min[bank];
			return;
		}
		if (gpio->p_data->quirks & GPIO_QUIRK_VERSAL)
			bank = bank + VERSAL_UNUSED_BANKS;
	}

	/* default */
	WARN(true, "invalid GPIO pin number: %u", pin_num);
	*bank_num = 0;
	*bank_pin_num = 0;
}

/**
 * zynq_gpio_get_value - Get the state of the specified pin of GPIO device
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function reads the state of the specified pin of the GPIO device.
 *
 * Return: 0 if the pin is low, 1 if pin is high.
 */
static int zynq_gpio_get_value(struct gpio_chip *chip, unsigned int pin)
{
	u32 data;
	unsigned int bank_num, bank_pin_num;
	struct zynq_gpio *gpio = gpiochip_get_data(chip);

	zynq_gpio_get_bank_pin(pin, &bank_num, &bank_pin_num, gpio);

	if (gpio_data_ro_bug(gpio)) {
		if (zynq_gpio_is_zynq(gpio)) {
			if (bank_num <= 1) {
				data = readl_relaxed(gpio->base_addr +
					ZYNQ_GPIO_DATA_RO_OFFSET(bank_num));
			} else {
				data = readl_relaxed(gpio->base_addr +
					ZYNQ_GPIO_DATA_OFFSET(bank_num));
			}
		} else {
			if (bank_num <= 2) {
				data = readl_relaxed(gpio->base_addr +
					ZYNQ_GPIO_DATA_RO_OFFSET(bank_num));
			} else {
				data = readl_relaxed(gpio->base_addr +
					ZYNQ_GPIO_DATA_OFFSET(bank_num));
			}
		}
	} else {
		data = readl_relaxed(gpio->base_addr +
			ZYNQ_GPIO_DATA_RO_OFFSET(bank_num));
	}
	return (data >> bank_pin_num) & 1;
}

/**
 * zynq_gpio_set_value - Modify the state of the pin with specified value
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 * @state:	value used to modify the state of the specified pin
 *
 * This function calculates the register offset (i.e to lower 16 bits or
 * upper 16 bits) based on the given pin number and sets the state of a
 * gpio pin to the specified value. The state is either 0 or non-zero.
 */
static void zynq_gpio_set_value(struct gpio_chip *chip, unsigned int pin,
				int state)
{
	unsigned int reg_offset, bank_num, bank_pin_num;
	struct zynq_gpio *gpio = gpiochip_get_data(chip);

	zynq_gpio_get_bank_pin(pin, &bank_num, &bank_pin_num, gpio);

	if (bank_pin_num >= ZYNQ_GPIO_MID_PIN_NUM) {
		/* only 16 data bits in bit maskable reg */
		bank_pin_num -= ZYNQ_GPIO_MID_PIN_NUM;
		reg_offset = ZYNQ_GPIO_DATA_MSW_OFFSET(bank_num);
	} else {
		reg_offset = ZYNQ_GPIO_DATA_LSW_OFFSET(bank_num);
	}

	/*
	 * get the 32 bit value to be written to the mask/data register where
	 * the upper 16 bits is the mask and lower 16 bits is the data
	 */
	state = !!state;
	state = ~(1 << (bank_pin_num + ZYNQ_GPIO_MID_PIN_NUM)) &
		((state << bank_pin_num) | ZYNQ_GPIO_UPPER_MASK);

	writel_relaxed(state, gpio->base_addr + reg_offset);
}

/**
 * zynq_gpio_dir_in - Set the direction of the specified GPIO pin as input
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function uses the read-modify-write sequence to set the direction of
 * the gpio pin as input.
 *
 * Return: 0 always
 */
static int zynq_gpio_dir_in(struct gpio_chip *chip, unsigned int pin)
{
	u32 reg;
	unsigned int bank_num, bank_pin_num;
	unsigned long flags;
	struct zynq_gpio *gpio = gpiochip_get_data(chip);

	zynq_gpio_get_bank_pin(pin, &bank_num, &bank_pin_num, gpio);

	/*
	 * On zynq bank 0 pins 7 and 8 are special and cannot be used
	 * as inputs.
	 */
	if (zynq_gpio_is_zynq(gpio) && bank_num == 0 &&
	    (bank_pin_num == 7 || bank_pin_num == 8))
		return -EINVAL;

	/* clear the bit in direction mode reg to set the pin as input */
	spin_lock_irqsave(&gpio->dirlock, flags);
	reg = readl_relaxed(gpio->base_addr + ZYNQ_GPIO_DIRM_OFFSET(bank_num));
	reg &= ~BIT(bank_pin_num);
	writel_relaxed(reg, gpio->base_addr + ZYNQ_GPIO_DIRM_OFFSET(bank_num));
	spin_unlock_irqrestore(&gpio->dirlock, flags);

	return 0;
}

/**
 * zynq_gpio_dir_out - Set the direction of the specified GPIO pin as output
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 * @state:	value to be written to specified pin
 *
 * This function sets the direction of specified GPIO pin as output, configures
 * the Output Enable register for the pin and uses zynq_gpio_set to set
 * the state of the pin to the value specified.
 *
 * Return: 0 always
 */
static int zynq_gpio_dir_out(struct gpio_chip *chip, unsigned int pin,
			     int state)
{
	u32 reg;
	unsigned int bank_num, bank_pin_num;
	unsigned long flags;
	struct zynq_gpio *gpio = gpiochip_get_data(chip);

	zynq_gpio_get_bank_pin(pin, &bank_num, &bank_pin_num, gpio);

	/* set the GPIO pin as output */
	spin_lock_irqsave(&gpio->dirlock, flags);
	reg = readl_relaxed(gpio->base_addr + ZYNQ_GPIO_DIRM_OFFSET(bank_num));
	reg |= BIT(bank_pin_num);
	writel_relaxed(reg, gpio->base_addr + ZYNQ_GPIO_DIRM_OFFSET(bank_num));

	/* configure the output enable reg for the pin */
	reg = readl_relaxed(gpio->base_addr + ZYNQ_GPIO_OUTEN_OFFSET(bank_num));
	reg |= BIT(bank_pin_num);
	writel_relaxed(reg, gpio->base_addr + ZYNQ_GPIO_OUTEN_OFFSET(bank_num));
	spin_unlock_irqrestore(&gpio->dirlock, flags);

	/* set the state of the pin */
	zynq_gpio_set_value(chip, pin, state);
	return 0;
}

/**
 * zynq_gpio_get_direction - Read the direction of the specified GPIO pin
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function returns the direction of the specified GPIO.
 *
 * Return: GPIO_LINE_DIRECTION_OUT or GPIO_LINE_DIRECTION_IN
 */
static int zynq_gpio_get_direction(struct gpio_chip *chip, unsigned int pin)
{
	u32 reg;
	unsigned int bank_num, bank_pin_num;
	struct zynq_gpio *gpio = gpiochip_get_data(chip);

	zynq_gpio_get_bank_pin(pin, &bank_num, &bank_pin_num, gpio);

	reg = readl_relaxed(gpio->base_addr + ZYNQ_GPIO_DIRM_OFFSET(bank_num));

	if (reg & BIT(bank_pin_num))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

/**
 * zynq_gpio_irq_mask - Disable the interrupts for a gpio pin
 * @irq_data:	per irq and chip data passed down to chip functions
 *
 * This function calculates gpio pin number from irq number and sets the
 * bit in the Interrupt Disable register of the corresponding bank to disable
 * interrupts for that pin.
 */
static void zynq_gpio_irq_mask(struct irq_data *irq_data)
{
	unsigned int device_pin_num, bank_num, bank_pin_num;
	const unsigned long offset = irqd_to_hwirq(irq_data);
	struct gpio_chip *chip = irq_data_get_irq_chip_data(irq_data);
	struct zynq_gpio *gpio =
		gpiochip_get_data(irq_data_get_irq_chip_data(irq_data));

	gpiochip_disable_irq(chip, offset);
	device_pin_num = irq_data->hwirq;
	zynq_gpio_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num, gpio);
	writel_relaxed(BIT(bank_pin_num),
		       gpio->base_addr + ZYNQ_GPIO_INTDIS_OFFSET(bank_num));
}

/**
 * zynq_gpio_irq_unmask - Enable the interrupts for a gpio pin
 * @irq_data:	irq data containing irq number of gpio pin for the interrupt
 *		to enable
 *
 * This function calculates the gpio pin number from irq number and sets the
 * bit in the Interrupt Enable register of the corresponding bank to enable
 * interrupts for that pin.
 */
static void zynq_gpio_irq_unmask(struct irq_data *irq_data)
{
	unsigned int device_pin_num, bank_num, bank_pin_num;
	const unsigned long offset = irqd_to_hwirq(irq_data);
	struct gpio_chip *chip = irq_data_get_irq_chip_data(irq_data);
	struct zynq_gpio *gpio =
		gpiochip_get_data(irq_data_get_irq_chip_data(irq_data));

	gpiochip_enable_irq(chip, offset);
	device_pin_num = irq_data->hwirq;
	zynq_gpio_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num, gpio);
	writel_relaxed(BIT(bank_pin_num),
		       gpio->base_addr + ZYNQ_GPIO_INTEN_OFFSET(bank_num));
}

/**
 * zynq_gpio_irq_ack - Acknowledge the interrupt of a gpio pin
 * @irq_data:	irq data containing irq number of gpio pin for the interrupt
 *		to ack
 *
 * This function calculates gpio pin number from irq number and sets the bit
 * in the Interrupt Status Register of the corresponding bank, to ACK the irq.
 */
static void zynq_gpio_irq_ack(struct irq_data *irq_data)
{
	unsigned int device_pin_num, bank_num, bank_pin_num;
	struct zynq_gpio *gpio =
		gpiochip_get_data(irq_data_get_irq_chip_data(irq_data));

	device_pin_num = irq_data->hwirq;
	zynq_gpio_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num, gpio);
	writel_relaxed(BIT(bank_pin_num),
		       gpio->base_addr + ZYNQ_GPIO_INTSTS_OFFSET(bank_num));
}

/**
 * zynq_gpio_irq_enable - Enable the interrupts for a gpio pin
 * @irq_data:	irq data containing irq number of gpio pin for the interrupt
 *		to enable
 *
 * Clears the INTSTS bit and unmasks the given interrupt.
 */
static void zynq_gpio_irq_enable(struct irq_data *irq_data)
{
	/*
	 * The Zynq GPIO controller does not disable interrupt detection when
	 * the interrupt is masked and only disables the propagation of the
	 * interrupt. This means when the controller detects an interrupt
	 * condition while the interrupt is logically disabled it will propagate
	 * that interrupt event once the interrupt is enabled. This will cause
	 * the interrupt consumer to see spurious interrupts to prevent this
	 * first make sure that the interrupt is not asserted and then enable
	 * it.
	 */
	zynq_gpio_irq_ack(irq_data);
	zynq_gpio_irq_unmask(irq_data);
}

/**
 * zynq_gpio_set_irq_type - Set the irq type for a gpio pin
 * @irq_data:	irq data containing irq number of gpio pin
 * @type:	interrupt type that is to be set for the gpio pin
 *
 * This function gets the gpio pin number and its bank from the gpio pin number
 * and configures the INT_TYPE, INT_POLARITY and INT_ANY registers.
 *
 * Return: 0, negative error otherwise.
 * TYPE-EDGE_RISING,  INT_TYPE - 1, INT_POLARITY - 1,  INT_ANY - 0;
 * TYPE-EDGE_FALLING, INT_TYPE - 1, INT_POLARITY - 0,  INT_ANY - 0;
 * TYPE-EDGE_BOTH,    INT_TYPE - 1, INT_POLARITY - NA, INT_ANY - 1;
 * TYPE-LEVEL_HIGH,   INT_TYPE - 0, INT_POLARITY - 1,  INT_ANY - NA;
 * TYPE-LEVEL_LOW,    INT_TYPE - 0, INT_POLARITY - 0,  INT_ANY - NA
 */
static int zynq_gpio_set_irq_type(struct irq_data *irq_data, unsigned int type)
{
	u32 int_type, int_pol, int_any;
	unsigned int device_pin_num, bank_num, bank_pin_num;
	struct zynq_gpio *gpio =
		gpiochip_get_data(irq_data_get_irq_chip_data(irq_data));

	device_pin_num = irq_data->hwirq;
	zynq_gpio_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num, gpio);

	int_type = readl_relaxed(gpio->base_addr +
				 ZYNQ_GPIO_INTTYPE_OFFSET(bank_num));
	int_pol = readl_relaxed(gpio->base_addr +
				ZYNQ_GPIO_INTPOL_OFFSET(bank_num));
	int_any = readl_relaxed(gpio->base_addr +
				ZYNQ_GPIO_INTANY_OFFSET(bank_num));

	/*
	 * based on the type requested, configure the INT_TYPE, INT_POLARITY
	 * and INT_ANY registers
	 */
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		int_type |= BIT(bank_pin_num);
		int_pol |= BIT(bank_pin_num);
		int_any &= ~BIT(bank_pin_num);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		int_type |= BIT(bank_pin_num);
		int_pol &= ~BIT(bank_pin_num);
		int_any &= ~BIT(bank_pin_num);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		int_type |= BIT(bank_pin_num);
		int_any |= BIT(bank_pin_num);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		int_type &= ~BIT(bank_pin_num);
		int_pol |= BIT(bank_pin_num);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		int_type &= ~BIT(bank_pin_num);
		int_pol &= ~BIT(bank_pin_num);
		break;
	default:
		return -EINVAL;
	}

	writel_relaxed(int_type,
		       gpio->base_addr + ZYNQ_GPIO_INTTYPE_OFFSET(bank_num));
	writel_relaxed(int_pol,
		       gpio->base_addr + ZYNQ_GPIO_INTPOL_OFFSET(bank_num));
	writel_relaxed(int_any,
		       gpio->base_addr + ZYNQ_GPIO_INTANY_OFFSET(bank_num));

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_chip_handler_name_locked(irq_data,
						 &zynq_gpio_level_irqchip,
						 handle_fasteoi_irq, NULL);
	else
		irq_set_chip_handler_name_locked(irq_data,
						 &zynq_gpio_edge_irqchip,
						 handle_level_irq, NULL);

	return 0;
}

static int zynq_gpio_set_wake(struct irq_data *data, unsigned int on)
{
	struct zynq_gpio *gpio =
		gpiochip_get_data(irq_data_get_irq_chip_data(data));

	irq_set_irq_wake(gpio->irq, on);

	return 0;
}

static int zynq_gpio_irq_reqres(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	int ret;

	ret = pm_runtime_resume_and_get(chip->parent);
	if (ret < 0)
		return ret;

	return gpiochip_reqres_irq(chip, d->hwirq);
}

static void zynq_gpio_irq_relres(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);

	gpiochip_relres_irq(chip, d->hwirq);
	pm_runtime_put(chip->parent);
}

/* irq chip descriptor */
static const struct irq_chip zynq_gpio_level_irqchip = {
	.name		= DRIVER_NAME,
	.irq_enable	= zynq_gpio_irq_enable,
	.irq_eoi	= zynq_gpio_irq_ack,
	.irq_mask	= zynq_gpio_irq_mask,
	.irq_unmask	= zynq_gpio_irq_unmask,
	.irq_set_type	= zynq_gpio_set_irq_type,
	.irq_set_wake	= zynq_gpio_set_wake,
	.irq_request_resources = zynq_gpio_irq_reqres,
	.irq_release_resources = zynq_gpio_irq_relres,
	.flags		= IRQCHIP_EOI_THREADED | IRQCHIP_EOI_IF_HANDLED |
			  IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
};

static const struct irq_chip zynq_gpio_edge_irqchip = {
	.name		= DRIVER_NAME,
	.irq_enable	= zynq_gpio_irq_enable,
	.irq_ack	= zynq_gpio_irq_ack,
	.irq_mask	= zynq_gpio_irq_mask,
	.irq_unmask	= zynq_gpio_irq_unmask,
	.irq_set_type	= zynq_gpio_set_irq_type,
	.irq_set_wake	= zynq_gpio_set_wake,
	.irq_request_resources = zynq_gpio_irq_reqres,
	.irq_release_resources = zynq_gpio_irq_relres,
	.flags		= IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
};

static void zynq_gpio_handle_bank_irq(struct zynq_gpio *gpio,
				      unsigned int bank_num,
				      unsigned long pending)
{
	unsigned int bank_offset = gpio->p_data->bank_min[bank_num];
	struct irq_domain *irqdomain = gpio->chip.irq.domain;
	int offset;

	if (!pending)
		return;

	for_each_set_bit(offset, &pending, 32)
		generic_handle_domain_irq(irqdomain, offset + bank_offset);
}

/**
 * zynq_gpio_irqhandler - IRQ handler for the gpio banks of a gpio device
 * @desc:	irq descriptor instance of the 'irq'
 *
 * This function reads the Interrupt Status Register of each bank to get the
 * gpio pin number which has triggered an interrupt. It then acks the triggered
 * interrupt and calls the pin specific handler set by the higher layer
 * application for that pin.
 * Note: A bug is reported if no handler is set for the gpio pin.
 */
static void zynq_gpio_irqhandler(struct irq_desc *desc)
{
	u32 int_sts, int_enb;
	unsigned int bank_num;
	struct zynq_gpio *gpio =
		gpiochip_get_data(irq_desc_get_handler_data(desc));
	struct irq_chip *irqchip = irq_desc_get_chip(desc);

	chained_irq_enter(irqchip, desc);

	for (bank_num = 0; bank_num < gpio->p_data->max_bank; bank_num++) {
		int_sts = readl_relaxed(gpio->base_addr +
					ZYNQ_GPIO_INTSTS_OFFSET(bank_num));
		int_enb = readl_relaxed(gpio->base_addr +
					ZYNQ_GPIO_INTMASK_OFFSET(bank_num));
		zynq_gpio_handle_bank_irq(gpio, bank_num, int_sts & ~int_enb);
		if (gpio->p_data->quirks & GPIO_QUIRK_VERSAL)
			bank_num = bank_num + VERSAL_UNUSED_BANKS;
	}

	chained_irq_exit(irqchip, desc);
}

static void zynq_gpio_save_context(struct zynq_gpio *gpio)
{
	unsigned int bank_num;

	for (bank_num = 0; bank_num < gpio->p_data->max_bank; bank_num++) {
		gpio->context.datalsw[bank_num] =
				readl_relaxed(gpio->base_addr +
				ZYNQ_GPIO_DATA_LSW_OFFSET(bank_num));
		gpio->context.datamsw[bank_num] =
				readl_relaxed(gpio->base_addr +
				ZYNQ_GPIO_DATA_MSW_OFFSET(bank_num));
		gpio->context.dirm[bank_num] = readl_relaxed(gpio->base_addr +
				ZYNQ_GPIO_DIRM_OFFSET(bank_num));
		gpio->context.int_en[bank_num] = readl_relaxed(gpio->base_addr +
				ZYNQ_GPIO_INTMASK_OFFSET(bank_num));
		gpio->context.int_type[bank_num] =
				readl_relaxed(gpio->base_addr +
				ZYNQ_GPIO_INTTYPE_OFFSET(bank_num));
		gpio->context.int_polarity[bank_num] =
				readl_relaxed(gpio->base_addr +
				ZYNQ_GPIO_INTPOL_OFFSET(bank_num));
		gpio->context.int_any[bank_num] =
				readl_relaxed(gpio->base_addr +
				ZYNQ_GPIO_INTANY_OFFSET(bank_num));
		if (gpio->p_data->quirks & GPIO_QUIRK_VERSAL)
			bank_num = bank_num + VERSAL_UNUSED_BANKS;
	}
}

static void zynq_gpio_restore_context(struct zynq_gpio *gpio)
{
	unsigned int bank_num;

	for (bank_num = 0; bank_num < gpio->p_data->max_bank; bank_num++) {
		writel_relaxed(ZYNQ_GPIO_IXR_DISABLE_ALL, gpio->base_addr +
				ZYNQ_GPIO_INTDIS_OFFSET(bank_num));
		writel_relaxed(gpio->context.datalsw[bank_num],
			       gpio->base_addr +
			       ZYNQ_GPIO_DATA_LSW_OFFSET(bank_num));
		writel_relaxed(gpio->context.datamsw[bank_num],
			       gpio->base_addr +
			       ZYNQ_GPIO_DATA_MSW_OFFSET(bank_num));
		writel_relaxed(gpio->context.dirm[bank_num],
			       gpio->base_addr +
			       ZYNQ_GPIO_DIRM_OFFSET(bank_num));
		writel_relaxed(gpio->context.int_type[bank_num],
			       gpio->base_addr +
			       ZYNQ_GPIO_INTTYPE_OFFSET(bank_num));
		writel_relaxed(gpio->context.int_polarity[bank_num],
			       gpio->base_addr +
			       ZYNQ_GPIO_INTPOL_OFFSET(bank_num));
		writel_relaxed(gpio->context.int_any[bank_num],
			       gpio->base_addr +
			       ZYNQ_GPIO_INTANY_OFFSET(bank_num));
		writel_relaxed(~(gpio->context.int_en[bank_num]),
			       gpio->base_addr +
			       ZYNQ_GPIO_INTEN_OFFSET(bank_num));
		if (gpio->p_data->quirks & GPIO_QUIRK_VERSAL)
			bank_num = bank_num + VERSAL_UNUSED_BANKS;
	}
}

static int __maybe_unused zynq_gpio_suspend(struct device *dev)
{
	struct zynq_gpio *gpio = dev_get_drvdata(dev);
	struct irq_data *data = irq_get_irq_data(gpio->irq);

	if (!data) {
		dev_err(dev, "irq_get_irq_data() failed\n");
		return -EINVAL;
	}

	if (!device_may_wakeup(dev))
		disable_irq(gpio->irq);

	if (!irqd_is_wakeup_set(data)) {
		zynq_gpio_save_context(gpio);
		return pm_runtime_force_suspend(dev);
	}

	return 0;
}

static int __maybe_unused zynq_gpio_resume(struct device *dev)
{
	struct zynq_gpio *gpio = dev_get_drvdata(dev);
	struct irq_data *data = irq_get_irq_data(gpio->irq);
	int ret;

	if (!data) {
		dev_err(dev, "irq_get_irq_data() failed\n");
		return -EINVAL;
	}

	if (!device_may_wakeup(dev))
		enable_irq(gpio->irq);

	if (!irqd_is_wakeup_set(data)) {
		ret = pm_runtime_force_resume(dev);
		zynq_gpio_restore_context(gpio);
		return ret;
	}

	return 0;
}

static int __maybe_unused zynq_gpio_runtime_suspend(struct device *dev)
{
	struct zynq_gpio *gpio = dev_get_drvdata(dev);

	clk_disable_unprepare(gpio->clk);

	return 0;
}

static int __maybe_unused zynq_gpio_runtime_resume(struct device *dev)
{
	struct zynq_gpio *gpio = dev_get_drvdata(dev);

	return clk_prepare_enable(gpio->clk);
}

static int zynq_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	int ret;

	ret = pm_runtime_get_sync(chip->parent);

	/*
	 * If the device is already active pm_runtime_get() will return 1 on
	 * success, but gpio_request still needs to return 0.
	 */
	return ret < 0 ? ret : 0;
}

static void zynq_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pm_runtime_put(chip->parent);
}

static const struct dev_pm_ops zynq_gpio_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(zynq_gpio_suspend, zynq_gpio_resume)
	SET_RUNTIME_PM_OPS(zynq_gpio_runtime_suspend,
			   zynq_gpio_runtime_resume, NULL)
};

static const struct zynq_platform_data versal_gpio_def = {
	.label = "versal_gpio",
	.quirks = GPIO_QUIRK_VERSAL,
	.ngpio = 58,
	.max_bank = VERSAL_GPIO_MAX_BANK,
	.bank_min[0] = 0,
	.bank_max[0] = 25, /* 0 to 25 are connected to MIOs (26 pins) */
	.bank_min[3] = 26,
	.bank_max[3] = 57, /* Bank 3 is connected to FMIOs (32 pins) */
};

static const struct zynq_platform_data pmc_gpio_def = {
	.label = "pmc_gpio",
	.ngpio = 116,
	.max_bank = PMC_GPIO_MAX_BANK,
	.bank_min[0] = 0,
	.bank_max[0] = 25, /* 0 to 25 are connected to MIOs (26 pins) */
	.bank_min[1] = 26,
	.bank_max[1] = 51, /* Bank 1 are connected to MIOs (26 pins) */
	.bank_min[3] = 52,
	.bank_max[3] = 83, /* Bank 3 is connected to EMIOs (32 pins) */
	.bank_min[4] = 84,
	.bank_max[4] = 115, /* Bank 4 is connected to EMIOs (32 pins) */
};

static const struct zynq_platform_data zynqmp_gpio_def = {
	.label = "zynqmp_gpio",
	.quirks = GPIO_QUIRK_DATA_RO_BUG,
	.ngpio = ZYNQMP_GPIO_NR_GPIOS,
	.max_bank = ZYNQMP_GPIO_MAX_BANK,
	.bank_min[0] = ZYNQ_GPIO_BANK0_PIN_MIN(MP),
	.bank_max[0] = ZYNQ_GPIO_BANK0_PIN_MAX(MP),
	.bank_min[1] = ZYNQ_GPIO_BANK1_PIN_MIN(MP),
	.bank_max[1] = ZYNQ_GPIO_BANK1_PIN_MAX(MP),
	.bank_min[2] = ZYNQ_GPIO_BANK2_PIN_MIN(MP),
	.bank_max[2] = ZYNQ_GPIO_BANK2_PIN_MAX(MP),
	.bank_min[3] = ZYNQ_GPIO_BANK3_PIN_MIN(MP),
	.bank_max[3] = ZYNQ_GPIO_BANK3_PIN_MAX(MP),
	.bank_min[4] = ZYNQ_GPIO_BANK4_PIN_MIN(MP),
	.bank_max[4] = ZYNQ_GPIO_BANK4_PIN_MAX(MP),
	.bank_min[5] = ZYNQ_GPIO_BANK5_PIN_MIN(MP),
	.bank_max[5] = ZYNQ_GPIO_BANK5_PIN_MAX(MP),
};

static const struct zynq_platform_data zynq_gpio_def = {
	.label = "zynq_gpio",
	.quirks = ZYNQ_GPIO_QUIRK_IS_ZYNQ | GPIO_QUIRK_DATA_RO_BUG,
	.ngpio = ZYNQ_GPIO_NR_GPIOS,
	.max_bank = ZYNQ_GPIO_MAX_BANK,
	.bank_min[0] = ZYNQ_GPIO_BANK0_PIN_MIN(),
	.bank_max[0] = ZYNQ_GPIO_BANK0_PIN_MAX(),
	.bank_min[1] = ZYNQ_GPIO_BANK1_PIN_MIN(),
	.bank_max[1] = ZYNQ_GPIO_BANK1_PIN_MAX(),
	.bank_min[2] = ZYNQ_GPIO_BANK2_PIN_MIN(),
	.bank_max[2] = ZYNQ_GPIO_BANK2_PIN_MAX(),
	.bank_min[3] = ZYNQ_GPIO_BANK3_PIN_MIN(),
	.bank_max[3] = ZYNQ_GPIO_BANK3_PIN_MAX(),
};

static const struct of_device_id zynq_gpio_of_match[] = {
	{ .compatible = "xlnx,zynq-gpio-1.0", .data = &zynq_gpio_def },
	{ .compatible = "xlnx,zynqmp-gpio-1.0", .data = &zynqmp_gpio_def },
	{ .compatible = "xlnx,versal-gpio-1.0", .data = &versal_gpio_def },
	{ .compatible = "xlnx,pmc-gpio-1.0", .data = &pmc_gpio_def },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, zynq_gpio_of_match);

/**
 * zynq_gpio_probe - Initialization method for a zynq_gpio device
 * @pdev:	platform device instance
 *
 * This function allocates memory resources for the gpio device and registers
 * all the banks of the device. It will also set up interrupts for the gpio
 * pins.
 * Note: Interrupts are disabled for all the banks during initialization.
 *
 * Return: 0 on success, negative error otherwise.
 */
static int zynq_gpio_probe(struct platform_device *pdev)
{
	int ret, bank_num;
	struct zynq_gpio *gpio;
	struct gpio_chip *chip;
	struct gpio_irq_chip *girq;
	const struct of_device_id *match;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	match = of_match_node(zynq_gpio_of_match, pdev->dev.of_node);
	if (!match) {
		dev_err(&pdev->dev, "of_match_node() failed\n");
		return -EINVAL;
	}
	gpio->p_data = match->data;
	platform_set_drvdata(pdev, gpio);

	gpio->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpio->base_addr))
		return PTR_ERR(gpio->base_addr);

	gpio->irq = platform_get_irq(pdev, 0);
	if (gpio->irq < 0)
		return gpio->irq;

	/* configure the gpio chip */
	chip = &gpio->chip;
	chip->label = gpio->p_data->label;
	chip->owner = THIS_MODULE;
	chip->parent = &pdev->dev;
	chip->get = zynq_gpio_get_value;
	chip->set = zynq_gpio_set_value;
	chip->request = zynq_gpio_request;
	chip->free = zynq_gpio_free;
	chip->direction_input = zynq_gpio_dir_in;
	chip->direction_output = zynq_gpio_dir_out;
	chip->get_direction = zynq_gpio_get_direction;
	chip->base = of_alias_get_id(pdev->dev.of_node, "gpio");
	chip->ngpio = gpio->p_data->ngpio;

	/* Retrieve GPIO clock */
	gpio->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(gpio->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(gpio->clk), "input clock not found.\n");

	spin_lock_init(&gpio->dirlock);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		goto err_pm_dis;

	/* disable interrupts for all banks */
	for (bank_num = 0; bank_num < gpio->p_data->max_bank; bank_num++) {
		writel_relaxed(ZYNQ_GPIO_IXR_DISABLE_ALL, gpio->base_addr +
			       ZYNQ_GPIO_INTDIS_OFFSET(bank_num));
		if (gpio->p_data->quirks & GPIO_QUIRK_VERSAL)
			bank_num = bank_num + VERSAL_UNUSED_BANKS;
	}

	/* Set up the GPIO irqchip */
	girq = &chip->irq;
	gpio_irq_chip_set_chip(girq, &zynq_gpio_edge_irqchip);
	girq->parent_handler = zynq_gpio_irqhandler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(&pdev->dev, 1,
				     sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents) {
		ret = -ENOMEM;
		goto err_pm_put;
	}
	girq->parents[0] = gpio->irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;

	/* report a bug if gpio chip registration fails */
	ret = gpiochip_add_data(chip, gpio);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add gpio chip\n");
		goto err_pm_put;
	}

	irq_set_status_flags(gpio->irq, IRQ_DISABLE_UNLAZY);
	device_init_wakeup(&pdev->dev, 1);
	pm_runtime_put(&pdev->dev);

	return 0;

err_pm_put:
	pm_runtime_put(&pdev->dev);
err_pm_dis:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

/**
 * zynq_gpio_remove - Driver removal function
 * @pdev:	platform device instance
 *
 * Return: 0 always
 */
static void zynq_gpio_remove(struct platform_device *pdev)
{
	struct zynq_gpio *gpio = platform_get_drvdata(pdev);
	int ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0)
		dev_warn(&pdev->dev, "pm_runtime_get_sync() Failed\n");
	device_init_wakeup(&pdev->dev, 0);
	gpiochip_remove(&gpio->chip);
	device_set_wakeup_capable(&pdev->dev, 0);
	pm_runtime_disable(&pdev->dev);
}

static struct platform_driver zynq_gpio_driver = {
	.driver	= {
		.name = DRIVER_NAME,
		.pm = &zynq_gpio_dev_pm_ops,
		.of_match_table = zynq_gpio_of_match,
	},
	.probe = zynq_gpio_probe,
	.remove = zynq_gpio_remove,
};

module_platform_driver(zynq_gpio_driver);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Zynq GPIO driver");
MODULE_LICENSE("GPL");
