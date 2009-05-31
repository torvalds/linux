/*
 * Freescale STMP378X/STMP378X Pin Multiplexing
 *
 * Author: Vladislav Buzov <vbuzov@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#define DEBUG
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/sysdev.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/sysdev.h>
#include <linux/irq.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include <mach/regs-pinctrl.h>
#include <mach/pins.h>
#include <mach/pinmux.h>

#define NR_BANKS ARRAY_SIZE(pinmux_banks)
static struct stmp3xxx_pinmux_bank pinmux_banks[] = {
	[0] = {
		.hw_muxsel = {
			REGS_PINCTRL_BASE + HW_PINCTRL_MUXSEL0,
			REGS_PINCTRL_BASE + HW_PINCTRL_MUXSEL1,
		},
		.hw_drive = {
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE0,
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE1,
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE2,
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE3,
		},
		.hw_pull = REGS_PINCTRL_BASE + HW_PINCTRL_PULL0,
		.functions = { 0x0, 0x1, 0x2, 0x3 },
		.strengths = { 0x0, 0x1, 0x2, 0x3, 0xff },

		.hw_gpio_in = REGS_PINCTRL_BASE + HW_PINCTRL_DIN0,
		.hw_gpio_out = REGS_PINCTRL_BASE + HW_PINCTRL_DOUT0,
		.hw_gpio_doe = REGS_PINCTRL_BASE + HW_PINCTRL_DOE0,
		.irq = IRQ_GPIO0,

		.pin2irq = REGS_PINCTRL_BASE + HW_PINCTRL_PIN2IRQ0,
		.irqstat = REGS_PINCTRL_BASE + HW_PINCTRL_IRQSTAT0,
		.irqlevel = REGS_PINCTRL_BASE + HW_PINCTRL_IRQLEVEL0,
		.irqpolarity = REGS_PINCTRL_BASE + HW_PINCTRL_IRQPOL0,
		.irqen = REGS_PINCTRL_BASE + HW_PINCTRL_IRQEN0,
	},
	[1] = {
		.hw_muxsel = {
			REGS_PINCTRL_BASE + HW_PINCTRL_MUXSEL2,
			REGS_PINCTRL_BASE + HW_PINCTRL_MUXSEL3,
		},
		.hw_drive = {
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE4,
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE5,
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE6,
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE7,
		},
		.hw_pull = REGS_PINCTRL_BASE + HW_PINCTRL_PULL1,
		.functions = { 0x0, 0x1, 0x2, 0x3 },
		.strengths = { 0x0, 0x1, 0x2, 0x3, 0xff },

		.hw_gpio_in = REGS_PINCTRL_BASE + HW_PINCTRL_DIN1,
		.hw_gpio_out = REGS_PINCTRL_BASE + HW_PINCTRL_DOUT1,
		.hw_gpio_doe = REGS_PINCTRL_BASE + HW_PINCTRL_DOE1,
		.irq = IRQ_GPIO1,

		.pin2irq = REGS_PINCTRL_BASE + HW_PINCTRL_PIN2IRQ1,
		.irqstat = REGS_PINCTRL_BASE + HW_PINCTRL_IRQSTAT1,
		.irqlevel = REGS_PINCTRL_BASE + HW_PINCTRL_IRQLEVEL1,
		.irqpolarity = REGS_PINCTRL_BASE + HW_PINCTRL_IRQPOL1,
		.irqen = REGS_PINCTRL_BASE + HW_PINCTRL_IRQEN1,
	},
	[2] = {
	       .hw_muxsel = {
			REGS_PINCTRL_BASE + HW_PINCTRL_MUXSEL4,
			REGS_PINCTRL_BASE + HW_PINCTRL_MUXSEL5,
		},
		.hw_drive = {
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE8,
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE9,
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE10,
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE11,
		},
		.hw_pull = REGS_PINCTRL_BASE + HW_PINCTRL_PULL2,
		.functions = { 0x0, 0x1, 0x2, 0x3 },
		.strengths = { 0x0, 0x1, 0x2, 0x1, 0x2 },

		.hw_gpio_in = REGS_PINCTRL_BASE + HW_PINCTRL_DIN2,
		.hw_gpio_out = REGS_PINCTRL_BASE + HW_PINCTRL_DOUT2,
		.hw_gpio_doe = REGS_PINCTRL_BASE + HW_PINCTRL_DOE2,
		.irq = IRQ_GPIO2,

		.pin2irq = REGS_PINCTRL_BASE + HW_PINCTRL_PIN2IRQ2,
		.irqstat = REGS_PINCTRL_BASE + HW_PINCTRL_IRQSTAT2,
		.irqlevel = REGS_PINCTRL_BASE + HW_PINCTRL_IRQLEVEL2,
		.irqpolarity = REGS_PINCTRL_BASE + HW_PINCTRL_IRQPOL2,
		.irqen = REGS_PINCTRL_BASE + HW_PINCTRL_IRQEN2,
	},
	[3] = {
	       .hw_muxsel = {
		       REGS_PINCTRL_BASE + HW_PINCTRL_MUXSEL6,
		       REGS_PINCTRL_BASE + HW_PINCTRL_MUXSEL7,
	       },
	       .hw_drive = {
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE12,
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE13,
			REGS_PINCTRL_BASE + HW_PINCTRL_DRIVE14,
			NULL,
	       },
	       .hw_pull = REGS_PINCTRL_BASE + HW_PINCTRL_PULL3,
	       .functions = {0x0, 0x1, 0x2, 0x3},
	       .strengths = {0x0, 0x1, 0x2, 0x3, 0xff},
	},
};

static inline struct stmp3xxx_pinmux_bank *
stmp3xxx_pinmux_bank(unsigned id, unsigned *bank, unsigned *pin)
{
	unsigned b, p;

	b = STMP3XXX_PINID_TO_BANK(id);
	p = STMP3XXX_PINID_TO_PINNUM(id);
	BUG_ON(b >= NR_BANKS);
	if (bank)
		*bank = b;
	if (pin)
		*pin = p;
	return &pinmux_banks[b];
}

/* Check if requested pin is owned by caller */
static int stmp3xxx_check_pin(unsigned id, const char *label)
{
	unsigned pin;
	struct stmp3xxx_pinmux_bank *pm = stmp3xxx_pinmux_bank(id, NULL, &pin);

	if (!test_bit(pin, &pm->pin_map)) {
		printk(KERN_WARNING
		       "%s: Accessing free pin %x, caller %s\n",
		       __func__, id, label);

		return -EINVAL;
	}

	if (label && pm->pin_labels[pin] &&
	    strcmp(label, pm->pin_labels[pin])) {
		printk(KERN_WARNING
		       "%s: Wrong pin owner %x, caller %s owner %s\n",
		       __func__, id, label, pm->pin_labels[pin]);

		return -EINVAL;
	}
	return 0;
}

void stmp3xxx_pin_strength(unsigned id, enum pin_strength strength,
		const char *label)
{
	struct stmp3xxx_pinmux_bank *pbank;
	void __iomem *hwdrive;
	u32 shift, val;
	u32 bank, pin;

	pbank = stmp3xxx_pinmux_bank(id, &bank, &pin);
	pr_debug("%s: label %s bank %d pin %d strength %d\n", __func__, label,
		 bank, pin, strength);

	hwdrive = pbank->hw_drive[pin / HW_DRIVE_PIN_NUM];
	shift = (pin % HW_DRIVE_PIN_NUM) * HW_DRIVE_PIN_LEN;
	val = pbank->strengths[strength];
	if (val == 0xff) {
		printk(KERN_WARNING
		       "%s: strength is not supported for bank %d, caller %s",
		       __func__, bank, label);
		return;
	}

	if (stmp3xxx_check_pin(id, label))
		return;

	pr_debug("%s: writing 0x%x to 0x%p register\n", __func__,
			val << shift, hwdrive);
	stmp3xxx_clearl(HW_DRIVE_PINDRV_MASK << shift, hwdrive);
	stmp3xxx_setl(val << shift, hwdrive);
}

void stmp3xxx_pin_voltage(unsigned id, enum pin_voltage voltage,
			  const char *label)
{
	struct stmp3xxx_pinmux_bank *pbank;
	void __iomem *hwdrive;
	u32 shift;
	u32 bank, pin;

	pbank = stmp3xxx_pinmux_bank(id, &bank, &pin);
	pr_debug("%s: label %s bank %d pin %d voltage %d\n", __func__, label,
		 bank, pin, voltage);

	hwdrive = pbank->hw_drive[pin / HW_DRIVE_PIN_NUM];
	shift = (pin % HW_DRIVE_PIN_NUM) * HW_DRIVE_PIN_LEN;

	if (stmp3xxx_check_pin(id, label))
		return;

	pr_debug("%s: changing 0x%x bit in 0x%p register\n",
			__func__, HW_DRIVE_PINV_MASK << shift, hwdrive);
	if (voltage == PIN_1_8V)
		stmp3xxx_clearl(HW_DRIVE_PINV_MASK << shift, hwdrive);
	else
		stmp3xxx_setl(HW_DRIVE_PINV_MASK << shift, hwdrive);
}

void stmp3xxx_pin_pullup(unsigned id, int enable, const char *label)
{
	struct stmp3xxx_pinmux_bank *pbank;
	void __iomem *hwpull;
	u32 bank, pin;

	pbank = stmp3xxx_pinmux_bank(id, &bank, &pin);
	pr_debug("%s: label %s bank %d pin %d enable %d\n", __func__, label,
		 bank, pin, enable);

	hwpull = pbank->hw_pull;

	if (stmp3xxx_check_pin(id, label))
		return;

	pr_debug("%s: changing 0x%x bit in 0x%p register\n",
			__func__, 1 << pin, hwpull);
	if (enable)
		stmp3xxx_setl(1 << pin, hwpull);
	else
		stmp3xxx_clearl(1 << pin, hwpull);
}

int stmp3xxx_request_pin(unsigned id, enum pin_fun fun, const char *label)
{
	struct stmp3xxx_pinmux_bank *pbank;
	u32 bank, pin;
	int ret = 0;

	pbank = stmp3xxx_pinmux_bank(id, &bank, &pin);
	pr_debug("%s: label %s bank %d pin %d fun %d\n", __func__, label,
		 bank, pin, fun);

	if (test_bit(pin, &pbank->pin_map)) {
		printk(KERN_WARNING
		       "%s: CONFLICT DETECTED pin %d:%d caller %s owner %s\n",
		       __func__, bank, pin, label, pbank->pin_labels[pin]);
		return -EBUSY;
	}

	set_bit(pin, &pbank->pin_map);
	pbank->pin_labels[pin] = label;

	stmp3xxx_set_pin_type(id, fun);

	return ret;
}

void stmp3xxx_set_pin_type(unsigned id, enum pin_fun fun)
{
	struct stmp3xxx_pinmux_bank *pbank;
	void __iomem *hwmux;
	u32 shift, val;
	u32 bank, pin;

	pbank = stmp3xxx_pinmux_bank(id, &bank, &pin);

	hwmux = pbank->hw_muxsel[pin / HW_MUXSEL_PIN_NUM];
	shift = (pin % HW_MUXSEL_PIN_NUM) * HW_MUXSEL_PIN_LEN;

	val = pbank->functions[fun];
	shift = (pin % HW_MUXSEL_PIN_NUM) * HW_MUXSEL_PIN_LEN;
	pr_debug("%s: writing 0x%x to 0x%p register\n",
			__func__, val << shift, hwmux);
	stmp3xxx_clearl(HW_MUXSEL_PINFUN_MASK << shift, hwmux);
	stmp3xxx_setl(val << shift, hwmux);
}

void stmp3xxx_release_pin(unsigned id, const char *label)
{
	struct stmp3xxx_pinmux_bank *pbank;
	u32 bank, pin;

	pbank = stmp3xxx_pinmux_bank(id, &bank, &pin);
	pr_debug("%s: label %s bank %d pin %d\n", __func__, label, bank, pin);

	if (stmp3xxx_check_pin(id, label))
		return;

	clear_bit(pin, &pbank->pin_map);
	pbank->pin_labels[pin] = NULL;
}

int stmp3xxx_request_pin_group(struct pin_group *pin_group, const char *label)
{
	struct pin_desc *pin;
	int p;
	int err = 0;

	/* Allocate and configure pins */
	for (p = 0; p < pin_group->nr_pins; p++) {
		pr_debug("%s: #%d\n", __func__, p);
		pin = &pin_group->pins[p];

		err = stmp3xxx_request_pin(pin->id, pin->fun, label);
		if (err)
			goto out_err;

		stmp3xxx_pin_strength(pin->id, pin->strength, label);
		stmp3xxx_pin_voltage(pin->id, pin->voltage, label);
		stmp3xxx_pin_pullup(pin->id, pin->pullup, label);
	}

	return 0;

out_err:
	/* Release allocated pins in case of error */
	while (--p >= 0) {
		pr_debug("%s: releasing #%d\n", __func__, p);
		stmp3xxx_release_pin(pin_group->pins[p].id, label);
	}
	return err;
}
EXPORT_SYMBOL(stmp3xxx_request_pin_group);

void stmp3xxx_release_pin_group(struct pin_group *pin_group, const char *label)
{
	struct pin_desc *pin;
	int p;

	for (p = 0; p < pin_group->nr_pins; p++) {
		pin = &pin_group->pins[p];
		stmp3xxx_release_pin(pin->id, label);
	}
}
EXPORT_SYMBOL(stmp3xxx_release_pin_group);

static int stmp3xxx_irq_to_gpio(int irq,
	struct stmp3xxx_pinmux_bank **bank, unsigned *gpio)
{
	struct stmp3xxx_pinmux_bank *pm;

	for (pm = pinmux_banks; pm < pinmux_banks + NR_BANKS; pm++)
		if (pm->virq <= irq && irq < pm->virq + 32) {
			*bank = pm;
			*gpio = irq - pm->virq;
			return 0;
		}
	return -ENOENT;
}

static int stmp3xxx_set_irqtype(unsigned irq, unsigned type)
{
	struct stmp3xxx_pinmux_bank *pm;
	unsigned gpio;
	int l, p;

	stmp3xxx_irq_to_gpio(irq, &pm, &gpio);
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		l = 0; p = 1; break;
	case IRQ_TYPE_EDGE_FALLING:
		l = 0; p = 0; break;
	case IRQ_TYPE_LEVEL_HIGH:
		l = 1; p = 1; break;
	case IRQ_TYPE_LEVEL_LOW:
		l = 1; p = 0; break;
	default:
		pr_debug("%s: Incorrect GPIO interrupt type 0x%x\n",
				__func__, type);
		return -ENXIO;
	}

	if (l)
		stmp3xxx_setl(1 << gpio, pm->irqlevel);
	else
		stmp3xxx_clearl(1 << gpio, pm->irqlevel);
	if (p)
		stmp3xxx_setl(1 << gpio, pm->irqpolarity);
	else
		stmp3xxx_clearl(1 << gpio, pm->irqpolarity);
	return 0;
}

static void stmp3xxx_pin_ack_irq(unsigned irq)
{
	u32 stat;
	struct stmp3xxx_pinmux_bank *pm;
	unsigned gpio;

	stmp3xxx_irq_to_gpio(irq, &pm, &gpio);
	stat = __raw_readl(pm->irqstat) & (1 << gpio);
	stmp3xxx_clearl(stat, pm->irqstat);
}

static void stmp3xxx_pin_mask_irq(unsigned irq)
{
	struct stmp3xxx_pinmux_bank *pm;
	unsigned gpio;

	stmp3xxx_irq_to_gpio(irq, &pm, &gpio);
	stmp3xxx_clearl(1 << gpio, pm->irqen);
	stmp3xxx_clearl(1 << gpio, pm->pin2irq);
}

static void stmp3xxx_pin_unmask_irq(unsigned irq)
{
	struct stmp3xxx_pinmux_bank *pm;
	unsigned gpio;

	stmp3xxx_irq_to_gpio(irq, &pm, &gpio);
	stmp3xxx_setl(1 << gpio, pm->irqen);
	stmp3xxx_setl(1 << gpio, pm->pin2irq);
}

static inline
struct stmp3xxx_pinmux_bank *to_pinmux_bank(struct gpio_chip *chip)
{
	return container_of(chip, struct stmp3xxx_pinmux_bank, chip);
}

static int stmp3xxx_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct stmp3xxx_pinmux_bank *pm = to_pinmux_bank(chip);
	return pm->virq + offset;
}

static int stmp3xxx_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct stmp3xxx_pinmux_bank *pm = to_pinmux_bank(chip);
	unsigned v;

	v = __raw_readl(pm->hw_gpio_in) & (1 << offset);
	return v ? 1 : 0;
}

static void stmp3xxx_gpio_set(struct gpio_chip *chip, unsigned offset, int v)
{
	struct stmp3xxx_pinmux_bank *pm = to_pinmux_bank(chip);

	if (v)
		stmp3xxx_setl(1 << offset, pm->hw_gpio_out);
	else
		stmp3xxx_clearl(1 << offset, pm->hw_gpio_out);
}

static int stmp3xxx_gpio_output(struct gpio_chip *chip, unsigned offset, int v)
{
	struct stmp3xxx_pinmux_bank *pm = to_pinmux_bank(chip);

	stmp3xxx_setl(1 << offset, pm->hw_gpio_doe);
	stmp3xxx_gpio_set(chip, offset, v);
	return 0;
}

static int stmp3xxx_gpio_input(struct gpio_chip *chip, unsigned offset)
{
	struct stmp3xxx_pinmux_bank *pm = to_pinmux_bank(chip);

	stmp3xxx_clearl(1 << offset, pm->hw_gpio_doe);
	return 0;
}

static int stmp3xxx_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return stmp3xxx_request_pin(chip->base + offset, PIN_GPIO, "gpio");
}

static void stmp3xxx_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	stmp3xxx_release_pin(chip->base + offset, "gpio");
}

static void stmp3xxx_gpio_irq(u32 irq, struct irq_desc *desc)
{
	struct stmp3xxx_pinmux_bank *pm = get_irq_data(irq);
	int gpio_irq = pm->virq;
	u32 stat = __raw_readl(pm->irqstat);

	while (stat) {
		if (stat & 1)
			irq_desc[gpio_irq].handle_irq(gpio_irq,
				&irq_desc[gpio_irq]);
		gpio_irq++;
		stat >>= 1;
	}
}

static struct irq_chip gpio_irq_chip = {
	.ack	= stmp3xxx_pin_ack_irq,
	.mask	= stmp3xxx_pin_mask_irq,
	.unmask	= stmp3xxx_pin_unmask_irq,
	.set_type = stmp3xxx_set_irqtype,
};

int __init stmp3xxx_pinmux_init(int virtual_irq_start)
{
	int b, r = 0;
	struct stmp3xxx_pinmux_bank *pm;
	int virq;

	for (b = 0; b < 3; b++) {
		/* only banks 0,1,2 are allowed to GPIO */
		pm = pinmux_banks + b;
		pm->chip.base = 32 * b;
		pm->chip.ngpio = 32;
		pm->chip.owner = THIS_MODULE;
		pm->chip.can_sleep = 1;
		pm->chip.exported = 1;
		pm->chip.to_irq = stmp3xxx_gpio_to_irq;
		pm->chip.direction_input = stmp3xxx_gpio_input;
		pm->chip.direction_output = stmp3xxx_gpio_output;
		pm->chip.get = stmp3xxx_gpio_get;
		pm->chip.set = stmp3xxx_gpio_set;
		pm->chip.request = stmp3xxx_gpio_request;
		pm->chip.free = stmp3xxx_gpio_free;
		pm->virq = virtual_irq_start + b * 32;

		for (virq = pm->virq; virq < pm->virq; virq++) {
			gpio_irq_chip.mask(virq);
			set_irq_chip(virq, &gpio_irq_chip);
			set_irq_handler(virq, handle_level_irq);
			set_irq_flags(virq, IRQF_VALID);
		}
		r = gpiochip_add(&pm->chip);
		if (r < 0)
			break;
		set_irq_chained_handler(pm->irq, stmp3xxx_gpio_irq);
		set_irq_data(pm->irq, pm);
	}
	return r;
}

MODULE_AUTHOR("Vladislav Buzov");
MODULE_LICENSE("GPL");
