// SPDX-License-Identifier: GPL-2.0-only
/*
 *  PCA953x 4/8/16/24/40 bit I/O ports
 *
 *  Copyright (C) 2005 Ben Gardner <bgardner@wabtec.com>
 *  Copyright (C) 2007 Marvell International Ltd.
 *
 *  Derived from drivers/i2c/chips/pca9539.c
 */

#include <linux/acpi.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_data/pca953x.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <asm/unaligned.h>

#define PCA953X_INPUT		0x00
#define PCA953X_OUTPUT		0x01
#define PCA953X_INVERT		0x02
#define PCA953X_DIRECTION	0x03

#define REG_ADDR_MASK		0x3f
#define REG_ADDR_EXT		0x40
#define REG_ADDR_AI		0x80

#define PCA957X_IN		0x00
#define PCA957X_INVRT		0x01
#define PCA957X_BKEN		0x02
#define PCA957X_PUPD		0x03
#define PCA957X_CFG		0x04
#define PCA957X_OUT		0x05
#define PCA957X_MSK		0x06
#define PCA957X_INTS		0x07

#define PCAL953X_OUT_STRENGTH	0x20
#define PCAL953X_IN_LATCH	0x22
#define PCAL953X_PULL_EN	0x23
#define PCAL953X_PULL_SEL	0x24
#define PCAL953X_INT_MASK	0x25
#define PCAL953X_INT_STAT	0x26
#define PCAL953X_OUT_CONF	0x27

#define PCAL6524_INT_EDGE	0x28
#define PCAL6524_INT_CLR	0x2a
#define PCAL6524_IN_STATUS	0x2b
#define PCAL6524_OUT_INDCONF	0x2c
#define PCAL6524_DEBOUNCE	0x2d

#define PCA_GPIO_MASK		0x00FF

#define PCAL_GPIO_MASK		0x1f
#define PCAL_PINCTRL_MASK	0x60

#define PCA_INT			0x0100
#define PCA_PCAL		0x0200
#define PCA_LATCH_INT		(PCA_PCAL | PCA_INT)
#define PCA953X_TYPE		0x1000
#define PCA957X_TYPE		0x2000
#define PCA_TYPE_MASK		0xF000

#define PCA_CHIP_TYPE(x)	((x) & PCA_TYPE_MASK)

static const struct i2c_device_id pca953x_id[] = {
	{ "pca6416", 16 | PCA953X_TYPE | PCA_INT, },
	{ "pca9505", 40 | PCA953X_TYPE | PCA_INT, },
	{ "pca9534", 8  | PCA953X_TYPE | PCA_INT, },
	{ "pca9535", 16 | PCA953X_TYPE | PCA_INT, },
	{ "pca9536", 4  | PCA953X_TYPE, },
	{ "pca9537", 4  | PCA953X_TYPE | PCA_INT, },
	{ "pca9538", 8  | PCA953X_TYPE | PCA_INT, },
	{ "pca9539", 16 | PCA953X_TYPE | PCA_INT, },
	{ "pca9554", 8  | PCA953X_TYPE | PCA_INT, },
	{ "pca9555", 16 | PCA953X_TYPE | PCA_INT, },
	{ "pca9556", 8  | PCA953X_TYPE, },
	{ "pca9557", 8  | PCA953X_TYPE, },
	{ "pca9574", 8  | PCA957X_TYPE | PCA_INT, },
	{ "pca9575", 16 | PCA957X_TYPE | PCA_INT, },
	{ "pca9698", 40 | PCA953X_TYPE, },

	{ "pcal6416", 16 | PCA953X_TYPE | PCA_LATCH_INT, },
	{ "pcal6524", 24 | PCA953X_TYPE | PCA_LATCH_INT, },
	{ "pcal9555a", 16 | PCA953X_TYPE | PCA_LATCH_INT, },

	{ "max7310", 8  | PCA953X_TYPE, },
	{ "max7312", 16 | PCA953X_TYPE | PCA_INT, },
	{ "max7313", 16 | PCA953X_TYPE | PCA_INT, },
	{ "max7315", 8  | PCA953X_TYPE | PCA_INT, },
	{ "max7318", 16 | PCA953X_TYPE | PCA_INT, },
	{ "pca6107", 8  | PCA953X_TYPE | PCA_INT, },
	{ "tca6408", 8  | PCA953X_TYPE | PCA_INT, },
	{ "tca6416", 16 | PCA953X_TYPE | PCA_INT, },
	{ "tca6424", 24 | PCA953X_TYPE | PCA_INT, },
	{ "tca9539", 16 | PCA953X_TYPE | PCA_INT, },
	{ "tca9554", 8  | PCA953X_TYPE | PCA_INT, },
	{ "xra1202", 8  | PCA953X_TYPE },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca953x_id);

static const struct acpi_device_id pca953x_acpi_ids[] = {
	{ "INT3491", 16 | PCA953X_TYPE | PCA_LATCH_INT, },
	{ }
};
MODULE_DEVICE_TABLE(acpi, pca953x_acpi_ids);

#define MAX_BANK 5
#define BANK_SZ 8

#define NBANK(chip) DIV_ROUND_UP(chip->gpio_chip.ngpio, BANK_SZ)

struct pca953x_reg_config {
	int direction;
	int output;
	int input;
	int invert;
};

static const struct pca953x_reg_config pca953x_regs = {
	.direction = PCA953X_DIRECTION,
	.output = PCA953X_OUTPUT,
	.input = PCA953X_INPUT,
	.invert = PCA953X_INVERT,
};

static const struct pca953x_reg_config pca957x_regs = {
	.direction = PCA957X_CFG,
	.output = PCA957X_OUT,
	.input = PCA957X_IN,
	.invert = PCA957X_INVRT,
};

struct pca953x_chip {
	unsigned gpio_start;
	struct mutex i2c_lock;
	struct regmap *regmap;

#ifdef CONFIG_GPIO_PCA953X_IRQ
	struct mutex irq_lock;
	u8 irq_mask[MAX_BANK];
	u8 irq_stat[MAX_BANK];
	u8 irq_trig_raise[MAX_BANK];
	u8 irq_trig_fall[MAX_BANK];
	struct irq_chip irq_chip;
#endif
	atomic_t wakeup_path;

	struct i2c_client *client;
	struct gpio_chip gpio_chip;
	const char *const *names;
	unsigned long driver_data;
	struct regulator *regulator;

	const struct pca953x_reg_config *regs;
};

static int pca953x_bank_shift(struct pca953x_chip *chip)
{
	return fls((chip->gpio_chip.ngpio - 1) / BANK_SZ);
}

#define PCA953x_BANK_INPUT	BIT(0)
#define PCA953x_BANK_OUTPUT	BIT(1)
#define PCA953x_BANK_POLARITY	BIT(2)
#define PCA953x_BANK_CONFIG	BIT(3)

#define PCA957x_BANK_INPUT	BIT(0)
#define PCA957x_BANK_POLARITY	BIT(1)
#define PCA957x_BANK_BUSHOLD	BIT(2)
#define PCA957x_BANK_CONFIG	BIT(4)
#define PCA957x_BANK_OUTPUT	BIT(5)

#define PCAL9xxx_BANK_IN_LATCH	BIT(8 + 2)
#define PCAL9xxx_BANK_PULL_EN	BIT(8 + 3)
#define PCAL9xxx_BANK_PULL_SEL	BIT(8 + 4)
#define PCAL9xxx_BANK_IRQ_MASK	BIT(8 + 5)
#define PCAL9xxx_BANK_IRQ_STAT	BIT(8 + 6)

/*
 * We care about the following registers:
 * - Standard set, below 0x40, each port can be replicated up to 8 times
 *   - PCA953x standard
 *     Input port			0x00 + 0 * bank_size	R
 *     Output port			0x00 + 1 * bank_size	RW
 *     Polarity Inversion port		0x00 + 2 * bank_size	RW
 *     Configuration port		0x00 + 3 * bank_size	RW
 *   - PCA957x with mixed up registers
 *     Input port			0x00 + 0 * bank_size	R
 *     Polarity Inversion port		0x00 + 1 * bank_size	RW
 *     Bus hold port			0x00 + 2 * bank_size	RW
 *     Configuration port		0x00 + 4 * bank_size	RW
 *     Output port			0x00 + 5 * bank_size	RW
 *
 * - Extended set, above 0x40, often chip specific.
 *   - PCAL6524/PCAL9555A with custom PCAL IRQ handling:
 *     Input latch register		0x40 + 2 * bank_size	RW
 *     Pull-up/pull-down enable reg	0x40 + 3 * bank_size    RW
 *     Pull-up/pull-down select reg	0x40 + 4 * bank_size    RW
 *     Interrupt mask register		0x40 + 5 * bank_size	RW
 *     Interrupt status register	0x40 + 6 * bank_size	R
 *
 * - Registers with bit 0x80 set, the AI bit
 *   The bit is cleared and the registers fall into one of the
 *   categories above.
 */

static bool pca953x_check_register(struct pca953x_chip *chip, unsigned int reg,
				   u32 checkbank)
{
	int bank_shift = pca953x_bank_shift(chip);
	int bank = (reg & REG_ADDR_MASK) >> bank_shift;
	int offset = reg & (BIT(bank_shift) - 1);

	/* Special PCAL extended register check. */
	if (reg & REG_ADDR_EXT) {
		if (!(chip->driver_data & PCA_PCAL))
			return false;
		bank += 8;
	}

	/* Register is not in the matching bank. */
	if (!(BIT(bank) & checkbank))
		return false;

	/* Register is not within allowed range of bank. */
	if (offset >= NBANK(chip))
		return false;

	return true;
}

static bool pca953x_readable_register(struct device *dev, unsigned int reg)
{
	struct pca953x_chip *chip = dev_get_drvdata(dev);
	u32 bank;

	if (PCA_CHIP_TYPE(chip->driver_data) == PCA953X_TYPE) {
		bank = PCA953x_BANK_INPUT | PCA953x_BANK_OUTPUT |
		       PCA953x_BANK_POLARITY | PCA953x_BANK_CONFIG;
	} else {
		bank = PCA957x_BANK_INPUT | PCA957x_BANK_OUTPUT |
		       PCA957x_BANK_POLARITY | PCA957x_BANK_CONFIG |
		       PCA957x_BANK_BUSHOLD;
	}

	if (chip->driver_data & PCA_PCAL) {
		bank |= PCAL9xxx_BANK_IN_LATCH | PCAL9xxx_BANK_PULL_EN |
			PCAL9xxx_BANK_PULL_SEL | PCAL9xxx_BANK_IRQ_MASK |
			PCAL9xxx_BANK_IRQ_STAT;
	}

	return pca953x_check_register(chip, reg, bank);
}

static bool pca953x_writeable_register(struct device *dev, unsigned int reg)
{
	struct pca953x_chip *chip = dev_get_drvdata(dev);
	u32 bank;

	if (PCA_CHIP_TYPE(chip->driver_data) == PCA953X_TYPE) {
		bank = PCA953x_BANK_OUTPUT | PCA953x_BANK_POLARITY |
			PCA953x_BANK_CONFIG;
	} else {
		bank = PCA957x_BANK_OUTPUT | PCA957x_BANK_POLARITY |
			PCA957x_BANK_CONFIG | PCA957x_BANK_BUSHOLD;
	}

	if (chip->driver_data & PCA_PCAL)
		bank |= PCAL9xxx_BANK_IN_LATCH | PCAL9xxx_BANK_PULL_EN |
			PCAL9xxx_BANK_PULL_SEL | PCAL9xxx_BANK_IRQ_MASK;

	return pca953x_check_register(chip, reg, bank);
}

static bool pca953x_volatile_register(struct device *dev, unsigned int reg)
{
	struct pca953x_chip *chip = dev_get_drvdata(dev);
	u32 bank;

	if (PCA_CHIP_TYPE(chip->driver_data) == PCA953X_TYPE)
		bank = PCA953x_BANK_INPUT;
	else
		bank = PCA957x_BANK_INPUT;

	if (chip->driver_data & PCA_PCAL)
		bank |= PCAL9xxx_BANK_IRQ_STAT;

	return pca953x_check_register(chip, reg, bank);
}

static const struct regmap_config pca953x_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = pca953x_readable_register,
	.writeable_reg = pca953x_writeable_register,
	.volatile_reg = pca953x_volatile_register,

	.cache_type = REGCACHE_RBTREE,
	/* REVISIT: should be 0x7f but some 24 bit chips use REG_ADDR_AI */
	.max_register = 0xff,
};

static u8 pca953x_recalc_addr(struct pca953x_chip *chip, int reg, int off,
			      bool write, bool addrinc)
{
	int bank_shift = pca953x_bank_shift(chip);
	int addr = (reg & PCAL_GPIO_MASK) << bank_shift;
	int pinctrl = (reg & PCAL_PINCTRL_MASK) << 1;
	u8 regaddr = pinctrl | addr | (off / BANK_SZ);

	/* Single byte read doesn't need AI bit set. */
	if (!addrinc)
		return regaddr;

	/* Chips with 24 and more GPIOs always support Auto Increment */
	if (write && NBANK(chip) > 2)
		regaddr |= REG_ADDR_AI;

	/* PCA9575 needs address-increment on multi-byte writes */
	if (PCA_CHIP_TYPE(chip->driver_data) == PCA957X_TYPE)
		regaddr |= REG_ADDR_AI;

	return regaddr;
}

static int pca953x_write_regs(struct pca953x_chip *chip, int reg, u8 *val)
{
	u8 regaddr = pca953x_recalc_addr(chip, reg, 0, true, true);
	int ret;

	ret = regmap_bulk_write(chip->regmap, regaddr, val, NBANK(chip));
	if (ret < 0) {
		dev_err(&chip->client->dev, "failed writing register\n");
		return ret;
	}

	return 0;
}

static int pca953x_read_regs(struct pca953x_chip *chip, int reg, u8 *val)
{
	u8 regaddr = pca953x_recalc_addr(chip, reg, 0, false, true);
	int ret;

	ret = regmap_bulk_read(chip->regmap, regaddr, val, NBANK(chip));
	if (ret < 0) {
		dev_err(&chip->client->dev, "failed reading register\n");
		return ret;
	}

	return 0;
}

static int pca953x_gpio_direction_input(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 dirreg = pca953x_recalc_addr(chip, chip->regs->direction, off,
					true, false);
	u8 bit = BIT(off % BANK_SZ);
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = regmap_write_bits(chip->regmap, dirreg, bit, bit);
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

static int pca953x_gpio_direction_output(struct gpio_chip *gc,
		unsigned off, int val)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 dirreg = pca953x_recalc_addr(chip, chip->regs->direction, off,
					true, false);
	u8 outreg = pca953x_recalc_addr(chip, chip->regs->output, off,
					true, false);
	u8 bit = BIT(off % BANK_SZ);
	int ret;

	mutex_lock(&chip->i2c_lock);
	/* set output level */
	ret = regmap_write_bits(chip->regmap, outreg, bit, val ? bit : 0);
	if (ret)
		goto exit;

	/* then direction */
	ret = regmap_write_bits(chip->regmap, dirreg, bit, 0);
exit:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

static int pca953x_gpio_get_value(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 inreg = pca953x_recalc_addr(chip, chip->regs->input, off,
				       true, false);
	u8 bit = BIT(off % BANK_SZ);
	u32 reg_val;
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = regmap_read(chip->regmap, inreg, &reg_val);
	mutex_unlock(&chip->i2c_lock);
	if (ret < 0) {
		/* NOTE:  diagnostic already emitted; that's all we should
		 * do unless gpio_*_value_cansleep() calls become different
		 * from their nonsleeping siblings (and report faults).
		 */
		return 0;
	}

	return !!(reg_val & bit);
}

static void pca953x_gpio_set_value(struct gpio_chip *gc, unsigned off, int val)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 outreg = pca953x_recalc_addr(chip, chip->regs->output, off,
					true, false);
	u8 bit = BIT(off % BANK_SZ);

	mutex_lock(&chip->i2c_lock);
	regmap_write_bits(chip->regmap, outreg, bit, val ? bit : 0);
	mutex_unlock(&chip->i2c_lock);
}

static int pca953x_gpio_get_direction(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 dirreg = pca953x_recalc_addr(chip, chip->regs->direction, off,
					true, false);
	u8 bit = BIT(off % BANK_SZ);
	u32 reg_val;
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = regmap_read(chip->regmap, dirreg, &reg_val);
	mutex_unlock(&chip->i2c_lock);
	if (ret < 0)
		return ret;

	return !!(reg_val & bit);
}

static void pca953x_gpio_set_multiple(struct gpio_chip *gc,
				      unsigned long *mask, unsigned long *bits)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	unsigned int bank_mask, bank_val;
	int bank;
	u8 reg_val[MAX_BANK];
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = pca953x_read_regs(chip, chip->regs->output, reg_val);
	if (ret)
		goto exit;

	for (bank = 0; bank < NBANK(chip); bank++) {
		bank_mask = mask[bank / sizeof(*mask)] >>
			   ((bank % sizeof(*mask)) * 8);
		if (bank_mask) {
			bank_val = bits[bank / sizeof(*bits)] >>
				  ((bank % sizeof(*bits)) * 8);
			bank_val &= bank_mask;
			reg_val[bank] = (reg_val[bank] & ~bank_mask) | bank_val;
		}
	}

	pca953x_write_regs(chip, chip->regs->output, reg_val);
exit:
	mutex_unlock(&chip->i2c_lock);
}

static int pca953x_gpio_set_pull_up_down(struct pca953x_chip *chip,
					 unsigned int offset,
					 unsigned long config)
{
	u8 pull_en_reg = pca953x_recalc_addr(chip, PCAL953X_PULL_EN, offset,
					     true, false);
	u8 pull_sel_reg = pca953x_recalc_addr(chip, PCAL953X_PULL_SEL, offset,
					      true, false);
	u8 bit = BIT(offset % BANK_SZ);
	int ret;

	/*
	 * pull-up/pull-down configuration requires PCAL extended
	 * registers
	 */
	if (!(chip->driver_data & PCA_PCAL))
		return -ENOTSUPP;

	mutex_lock(&chip->i2c_lock);

	/* Disable pull-up/pull-down */
	ret = regmap_write_bits(chip->regmap, pull_en_reg, bit, 0);
	if (ret)
		goto exit;

	/* Configure pull-up/pull-down */
	if (config == PIN_CONFIG_BIAS_PULL_UP)
		ret = regmap_write_bits(chip->regmap, pull_sel_reg, bit, bit);
	else if (config == PIN_CONFIG_BIAS_PULL_DOWN)
		ret = regmap_write_bits(chip->regmap, pull_sel_reg, bit, 0);
	if (ret)
		goto exit;

	/* Enable pull-up/pull-down */
	ret = regmap_write_bits(chip->regmap, pull_en_reg, bit, bit);

exit:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

static int pca953x_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
				   unsigned long config)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);

	switch (config) {
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		return pca953x_gpio_set_pull_up_down(chip, offset, config);
	default:
		return -ENOTSUPP;
	}
}

static void pca953x_setup_gpio(struct pca953x_chip *chip, int gpios)
{
	struct gpio_chip *gc;

	gc = &chip->gpio_chip;

	gc->direction_input  = pca953x_gpio_direction_input;
	gc->direction_output = pca953x_gpio_direction_output;
	gc->get = pca953x_gpio_get_value;
	gc->set = pca953x_gpio_set_value;
	gc->get_direction = pca953x_gpio_get_direction;
	gc->set_multiple = pca953x_gpio_set_multiple;
	gc->set_config = pca953x_gpio_set_config;
	gc->can_sleep = true;

	gc->base = chip->gpio_start;
	gc->ngpio = gpios;
	gc->label = dev_name(&chip->client->dev);
	gc->parent = &chip->client->dev;
	gc->owner = THIS_MODULE;
	gc->names = chip->names;
}

#ifdef CONFIG_GPIO_PCA953X_IRQ
static void pca953x_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);

	chip->irq_mask[d->hwirq / BANK_SZ] &= ~(1 << (d->hwirq % BANK_SZ));
}

static void pca953x_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);

	chip->irq_mask[d->hwirq / BANK_SZ] |= 1 << (d->hwirq % BANK_SZ);
}

static int pca953x_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);

	if (on)
		atomic_inc(&chip->wakeup_path);
	else
		atomic_dec(&chip->wakeup_path);

	return irq_set_irq_wake(chip->client->irq, on);
}

static void pca953x_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);

	mutex_lock(&chip->irq_lock);
}

static void pca953x_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 new_irqs;
	int level, i;
	u8 invert_irq_mask[MAX_BANK];
	u8 reg_direction[MAX_BANK];

	pca953x_read_regs(chip, chip->regs->direction, reg_direction);

	if (chip->driver_data & PCA_PCAL) {
		/* Enable latch on interrupt-enabled inputs */
		pca953x_write_regs(chip, PCAL953X_IN_LATCH, chip->irq_mask);

		for (i = 0; i < NBANK(chip); i++)
			invert_irq_mask[i] = ~chip->irq_mask[i];

		/* Unmask enabled interrupts */
		pca953x_write_regs(chip, PCAL953X_INT_MASK, invert_irq_mask);
	}

	/* Look for any newly setup interrupt */
	for (i = 0; i < NBANK(chip); i++) {
		new_irqs = chip->irq_trig_fall[i] | chip->irq_trig_raise[i];
		new_irqs &= reg_direction[i];

		while (new_irqs) {
			level = __ffs(new_irqs);
			pca953x_gpio_direction_input(&chip->gpio_chip,
							level + (BANK_SZ * i));
			new_irqs &= ~(1 << level);
		}
	}

	mutex_unlock(&chip->irq_lock);
}

static int pca953x_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	int bank_nb = d->hwirq / BANK_SZ;
	u8 mask = 1 << (d->hwirq % BANK_SZ);

	if (!(type & IRQ_TYPE_EDGE_BOTH)) {
		dev_err(&chip->client->dev, "irq %d: unsupported type %d\n",
			d->irq, type);
		return -EINVAL;
	}

	if (type & IRQ_TYPE_EDGE_FALLING)
		chip->irq_trig_fall[bank_nb] |= mask;
	else
		chip->irq_trig_fall[bank_nb] &= ~mask;

	if (type & IRQ_TYPE_EDGE_RISING)
		chip->irq_trig_raise[bank_nb] |= mask;
	else
		chip->irq_trig_raise[bank_nb] &= ~mask;

	return 0;
}

static void pca953x_irq_shutdown(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 mask = 1 << (d->hwirq % BANK_SZ);

	chip->irq_trig_raise[d->hwirq / BANK_SZ] &= ~mask;
	chip->irq_trig_fall[d->hwirq / BANK_SZ] &= ~mask;
}

static bool pca953x_irq_pending(struct pca953x_chip *chip, u8 *pending)
{
	u8 cur_stat[MAX_BANK];
	u8 old_stat[MAX_BANK];
	bool pending_seen = false;
	bool trigger_seen = false;
	u8 trigger[MAX_BANK];
	u8 reg_direction[MAX_BANK];
	int ret, i;

	if (chip->driver_data & PCA_PCAL) {
		/* Read the current interrupt status from the device */
		ret = pca953x_read_regs(chip, PCAL953X_INT_STAT, trigger);
		if (ret)
			return false;

		/* Check latched inputs and clear interrupt status */
		ret = pca953x_read_regs(chip, PCA953X_INPUT, cur_stat);
		if (ret)
			return false;

		for (i = 0; i < NBANK(chip); i++) {
			/* Apply filter for rising/falling edge selection */
			pending[i] = (~cur_stat[i] & chip->irq_trig_fall[i]) |
				(cur_stat[i] & chip->irq_trig_raise[i]);
			pending[i] &= trigger[i];
			if (pending[i])
				pending_seen = true;
		}

		return pending_seen;
	}

	ret = pca953x_read_regs(chip, chip->regs->input, cur_stat);
	if (ret)
		return false;

	/* Remove output pins from the equation */
	pca953x_read_regs(chip, chip->regs->direction, reg_direction);
	for (i = 0; i < NBANK(chip); i++)
		cur_stat[i] &= reg_direction[i];

	memcpy(old_stat, chip->irq_stat, NBANK(chip));

	for (i = 0; i < NBANK(chip); i++) {
		trigger[i] = (cur_stat[i] ^ old_stat[i]) & chip->irq_mask[i];
		if (trigger[i])
			trigger_seen = true;
	}

	if (!trigger_seen)
		return false;

	memcpy(chip->irq_stat, cur_stat, NBANK(chip));

	for (i = 0; i < NBANK(chip); i++) {
		pending[i] = (old_stat[i] & chip->irq_trig_fall[i]) |
			(cur_stat[i] & chip->irq_trig_raise[i]);
		pending[i] &= trigger[i];
		if (pending[i])
			pending_seen = true;
	}

	return pending_seen;
}

static irqreturn_t pca953x_irq_handler(int irq, void *devid)
{
	struct pca953x_chip *chip = devid;
	u8 pending[MAX_BANK];
	u8 level;
	unsigned nhandled = 0;
	int i;

	if (!pca953x_irq_pending(chip, pending))
		return IRQ_NONE;

	for (i = 0; i < NBANK(chip); i++) {
		while (pending[i]) {
			level = __ffs(pending[i]);
			handle_nested_irq(irq_find_mapping(chip->gpio_chip.irq.domain,
							level + (BANK_SZ * i)));
			pending[i] &= ~(1 << level);
			nhandled++;
		}
	}

	return (nhandled > 0) ? IRQ_HANDLED : IRQ_NONE;
}

static int pca953x_irq_setup(struct pca953x_chip *chip,
			     int irq_base)
{
	struct i2c_client *client = chip->client;
	struct irq_chip *irq_chip = &chip->irq_chip;
	u8 reg_direction[MAX_BANK];
	int ret, i;

	if (!client->irq)
		return 0;

	if (irq_base == -1)
		return 0;

	if (!(chip->driver_data & PCA_INT))
		return 0;

	ret = pca953x_read_regs(chip, chip->regs->input, chip->irq_stat);
	if (ret)
		return ret;

	/*
	 * There is no way to know which GPIO line generated the
	 * interrupt.  We have to rely on the previous read for
	 * this purpose.
	 */
	pca953x_read_regs(chip, chip->regs->direction, reg_direction);
	for (i = 0; i < NBANK(chip); i++)
		chip->irq_stat[i] &= reg_direction[i];
	mutex_init(&chip->irq_lock);

	ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, pca953x_irq_handler,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT |
					IRQF_SHARED,
					dev_name(&client->dev), chip);
	if (ret) {
		dev_err(&client->dev, "failed to request irq %d\n",
			client->irq);
		return ret;
	}

	irq_chip->name = dev_name(&chip->client->dev);
	irq_chip->irq_mask = pca953x_irq_mask;
	irq_chip->irq_unmask = pca953x_irq_unmask;
	irq_chip->irq_set_wake = pca953x_irq_set_wake;
	irq_chip->irq_bus_lock = pca953x_irq_bus_lock;
	irq_chip->irq_bus_sync_unlock = pca953x_irq_bus_sync_unlock;
	irq_chip->irq_set_type = pca953x_irq_set_type;
	irq_chip->irq_shutdown = pca953x_irq_shutdown;

	ret =  gpiochip_irqchip_add_nested(&chip->gpio_chip, irq_chip,
					   irq_base, handle_simple_irq,
					   IRQ_TYPE_NONE);
	if (ret) {
		dev_err(&client->dev,
			"could not connect irqchip to gpiochip\n");
		return ret;
	}

	gpiochip_set_nested_irqchip(&chip->gpio_chip, irq_chip, client->irq);

	return 0;
}

#else /* CONFIG_GPIO_PCA953X_IRQ */
static int pca953x_irq_setup(struct pca953x_chip *chip,
			     int irq_base)
{
	struct i2c_client *client = chip->client;

	if (client->irq && irq_base != -1 && (chip->driver_data & PCA_INT))
		dev_warn(&client->dev, "interrupt support not compiled in\n");

	return 0;
}
#endif

static int device_pca95xx_init(struct pca953x_chip *chip, u32 invert)
{
	int ret;
	u8 val[MAX_BANK];

	ret = regcache_sync_region(chip->regmap, chip->regs->output,
				   chip->regs->output + NBANK(chip));
	if (ret != 0)
		goto out;

	ret = regcache_sync_region(chip->regmap, chip->regs->direction,
				   chip->regs->direction + NBANK(chip));
	if (ret != 0)
		goto out;

	/* set platform specific polarity inversion */
	if (invert)
		memset(val, 0xFF, NBANK(chip));
	else
		memset(val, 0, NBANK(chip));

	ret = pca953x_write_regs(chip, chip->regs->invert, val);
out:
	return ret;
}

static int device_pca957x_init(struct pca953x_chip *chip, u32 invert)
{
	int ret;
	u8 val[MAX_BANK];

	ret = device_pca95xx_init(chip, invert);
	if (ret)
		goto out;

	/* To enable register 6, 7 to control pull up and pull down */
	memset(val, 0x02, NBANK(chip));
	ret = pca953x_write_regs(chip, PCA957X_BKEN, val);
	if (ret)
		goto out;

	return 0;
out:
	return ret;
}

static const struct of_device_id pca953x_dt_ids[];

static int pca953x_probe(struct i2c_client *client,
				   const struct i2c_device_id *i2c_id)
{
	struct pca953x_platform_data *pdata;
	struct pca953x_chip *chip;
	int irq_base = 0;
	int ret;
	u32 invert = 0;
	struct regulator *reg;

	chip = devm_kzalloc(&client->dev,
			sizeof(struct pca953x_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	pdata = dev_get_platdata(&client->dev);
	if (pdata) {
		irq_base = pdata->irq_base;
		chip->gpio_start = pdata->gpio_base;
		invert = pdata->invert;
		chip->names = pdata->names;
	} else {
		struct gpio_desc *reset_gpio;

		chip->gpio_start = -1;
		irq_base = 0;

		/*
		 * See if we need to de-assert a reset pin.
		 *
		 * There is no known ACPI-enabled platforms that are
		 * using "reset" GPIO. Otherwise any of those platform
		 * must use _DSD method with corresponding property.
		 */
		reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						     GPIOD_OUT_LOW);
		if (IS_ERR(reset_gpio))
			return PTR_ERR(reset_gpio);
	}

	chip->client = client;

	reg = devm_regulator_get(&client->dev, "vcc");
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "reg get err: %d\n", ret);
		return ret;
	}
	ret = regulator_enable(reg);
	if (ret) {
		dev_err(&client->dev, "reg en err: %d\n", ret);
		return ret;
	}
	chip->regulator = reg;

	if (i2c_id) {
		chip->driver_data = i2c_id->driver_data;
	} else {
		const struct acpi_device_id *acpi_id;
		struct device *dev = &client->dev;

		chip->driver_data = (uintptr_t)of_device_get_match_data(dev);
		if (!chip->driver_data) {
			acpi_id = acpi_match_device(pca953x_acpi_ids, dev);
			if (!acpi_id) {
				ret = -ENODEV;
				goto err_exit;
			}

			chip->driver_data = acpi_id->driver_data;
		}
	}

	i2c_set_clientdata(client, chip);

	chip->regmap = devm_regmap_init_i2c(client, &pca953x_i2c_regmap);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		goto err_exit;
	}

	regcache_mark_dirty(chip->regmap);

	mutex_init(&chip->i2c_lock);
	/*
	 * In case we have an i2c-mux controlled by a GPIO provided by an
	 * expander using the same driver higher on the device tree, read the
	 * i2c adapter nesting depth and use the retrieved value as lockdep
	 * subclass for chip->i2c_lock.
	 *
	 * REVISIT: This solution is not complete. It protects us from lockdep
	 * false positives when the expander controlling the i2c-mux is on
	 * a different level on the device tree, but not when it's on the same
	 * level on a different branch (in which case the subclass number
	 * would be the same).
	 *
	 * TODO: Once a correct solution is developed, a similar fix should be
	 * applied to all other i2c-controlled GPIO expanders (and potentially
	 * regmap-i2c).
	 */
	lockdep_set_subclass(&chip->i2c_lock,
			     i2c_adapter_depth(client->adapter));

	/* initialize cached registers from their original values.
	 * we can't share this chip with another i2c master.
	 */
	pca953x_setup_gpio(chip, chip->driver_data & PCA_GPIO_MASK);

	if (PCA_CHIP_TYPE(chip->driver_data) == PCA953X_TYPE) {
		chip->regs = &pca953x_regs;
		ret = device_pca95xx_init(chip, invert);
	} else {
		chip->regs = &pca957x_regs;
		ret = device_pca957x_init(chip, invert);
	}
	if (ret)
		goto err_exit;

	ret = devm_gpiochip_add_data(&client->dev, &chip->gpio_chip, chip);
	if (ret)
		goto err_exit;

	ret = pca953x_irq_setup(chip, irq_base);
	if (ret)
		goto err_exit;

	if (pdata && pdata->setup) {
		ret = pdata->setup(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0)
			dev_warn(&client->dev, "setup failed, %d\n", ret);
	}

	return 0;

err_exit:
	regulator_disable(chip->regulator);
	return ret;
}

static int pca953x_remove(struct i2c_client *client)
{
	struct pca953x_platform_data *pdata = dev_get_platdata(&client->dev);
	struct pca953x_chip *chip = i2c_get_clientdata(client);
	int ret;

	if (pdata && pdata->teardown) {
		ret = pdata->teardown(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0)
			dev_err(&client->dev, "%s failed, %d\n",
					"teardown", ret);
	} else {
		ret = 0;
	}

	regulator_disable(chip->regulator);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int pca953x_regcache_sync(struct device *dev)
{
	struct pca953x_chip *chip = dev_get_drvdata(dev);
	int ret;

	/*
	 * The ordering between direction and output is important,
	 * sync these registers first and only then sync the rest.
	 */
	ret = regcache_sync_region(chip->regmap, chip->regs->direction,
				   chip->regs->direction + NBANK(chip));
	if (ret != 0) {
		dev_err(dev, "Failed to sync GPIO dir registers: %d\n", ret);
		return ret;
	}

	ret = regcache_sync_region(chip->regmap, chip->regs->output,
				   chip->regs->output + NBANK(chip));
	if (ret != 0) {
		dev_err(dev, "Failed to sync GPIO out registers: %d\n", ret);
		return ret;
	}

#ifdef CONFIG_GPIO_PCA953X_IRQ
	if (chip->driver_data & PCA_PCAL) {
		ret = regcache_sync_region(chip->regmap, PCAL953X_IN_LATCH,
					   PCAL953X_IN_LATCH + NBANK(chip));
		if (ret != 0) {
			dev_err(dev, "Failed to sync INT latch registers: %d\n",
				ret);
			return ret;
		}

		ret = regcache_sync_region(chip->regmap, PCAL953X_INT_MASK,
					   PCAL953X_INT_MASK + NBANK(chip));
		if (ret != 0) {
			dev_err(dev, "Failed to sync INT mask registers: %d\n",
				ret);
			return ret;
		}
	}
#endif

	return 0;
}

static int pca953x_suspend(struct device *dev)
{
	struct pca953x_chip *chip = dev_get_drvdata(dev);

	regcache_cache_only(chip->regmap, true);

	if (atomic_read(&chip->wakeup_path))
		device_set_wakeup_path(dev);
	else
		regulator_disable(chip->regulator);

	return 0;
}

static int pca953x_resume(struct device *dev)
{
	struct pca953x_chip *chip = dev_get_drvdata(dev);
	int ret;

	if (!atomic_read(&chip->wakeup_path)) {
		ret = regulator_enable(chip->regulator);
		if (ret != 0) {
			dev_err(dev, "Failed to enable regulator: %d\n", ret);
			return 0;
		}
	}

	regcache_cache_only(chip->regmap, false);
	regcache_mark_dirty(chip->regmap);
	ret = pca953x_regcache_sync(dev);
	if (ret)
		return ret;

	ret = regcache_sync(chip->regmap);
	if (ret != 0) {
		dev_err(dev, "Failed to restore register map: %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

/* convenience to stop overlong match-table lines */
#define OF_953X(__nrgpio, __int) (void *)(__nrgpio | PCA953X_TYPE | __int)
#define OF_957X(__nrgpio, __int) (void *)(__nrgpio | PCA957X_TYPE | __int)

static const struct of_device_id pca953x_dt_ids[] = {
	{ .compatible = "nxp,pca6416", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "nxp,pca9505", .data = OF_953X(40, PCA_INT), },
	{ .compatible = "nxp,pca9534", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "nxp,pca9535", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "nxp,pca9536", .data = OF_953X( 4, 0), },
	{ .compatible = "nxp,pca9537", .data = OF_953X( 4, PCA_INT), },
	{ .compatible = "nxp,pca9538", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "nxp,pca9539", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "nxp,pca9554", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "nxp,pca9555", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "nxp,pca9556", .data = OF_953X( 8, 0), },
	{ .compatible = "nxp,pca9557", .data = OF_953X( 8, 0), },
	{ .compatible = "nxp,pca9574", .data = OF_957X( 8, PCA_INT), },
	{ .compatible = "nxp,pca9575", .data = OF_957X(16, PCA_INT), },
	{ .compatible = "nxp,pca9698", .data = OF_953X(40, 0), },

	{ .compatible = "nxp,pcal6416", .data = OF_953X(16, PCA_LATCH_INT), },
	{ .compatible = "nxp,pcal6524", .data = OF_953X(24, PCA_LATCH_INT), },
	{ .compatible = "nxp,pcal9555a", .data = OF_953X(16, PCA_LATCH_INT), },

	{ .compatible = "maxim,max7310", .data = OF_953X( 8, 0), },
	{ .compatible = "maxim,max7312", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "maxim,max7313", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "maxim,max7315", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "maxim,max7318", .data = OF_953X(16, PCA_INT), },

	{ .compatible = "ti,pca6107", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "ti,pca9536", .data = OF_953X( 4, 0), },
	{ .compatible = "ti,tca6408", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "ti,tca6416", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "ti,tca6424", .data = OF_953X(24, PCA_INT), },
	{ .compatible = "ti,tca9539", .data = OF_953X(16, PCA_INT), },

	{ .compatible = "onnn,cat9554", .data = OF_953X( 8, PCA_INT), },
	{ .compatible = "onnn,pca9654", .data = OF_953X( 8, PCA_INT), },

	{ .compatible = "exar,xra1202", .data = OF_953X( 8, 0), },
	{ }
};

MODULE_DEVICE_TABLE(of, pca953x_dt_ids);

static SIMPLE_DEV_PM_OPS(pca953x_pm_ops, pca953x_suspend, pca953x_resume);

static struct i2c_driver pca953x_driver = {
	.driver = {
		.name	= "pca953x",
		.pm	= &pca953x_pm_ops,
		.of_match_table = pca953x_dt_ids,
		.acpi_match_table = ACPI_PTR(pca953x_acpi_ids),
	},
	.probe		= pca953x_probe,
	.remove		= pca953x_remove,
	.id_table	= pca953x_id,
};

static int __init pca953x_init(void)
{
	return i2c_add_driver(&pca953x_driver);
}
/* register after i2c postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(pca953x_init);

static void __exit pca953x_exit(void)
{
	i2c_del_driver(&pca953x_driver);
}
module_exit(pca953x_exit);

MODULE_AUTHOR("eric miao <eric.miao@marvell.com>");
MODULE_DESCRIPTION("GPIO expander driver for PCA953x");
MODULE_LICENSE("GPL");
