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
#include <linux/bitmap.h>
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

#define REG_ADDR_MASK		GENMASK(5, 0)
#define REG_ADDR_EXT		BIT(6)
#define REG_ADDR_AI		BIT(7)

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

#define PCA_GPIO_MASK		GENMASK(7, 0)

#define PCAL_GPIO_MASK		GENMASK(4, 0)
#define PCAL_PINCTRL_MASK	GENMASK(6, 5)

#define PCA_INT			BIT(8)
#define PCA_PCAL		BIT(9)
#define PCA_LATCH_INT		(PCA_PCAL | PCA_INT)
#define PCA953X_TYPE		BIT(12)
#define PCA957X_TYPE		BIT(13)
#define PCAL653X_TYPE		BIT(14)
#define PCA_TYPE_MASK		GENMASK(15, 12)

#define PCA_CHIP_TYPE(x)	((x) & PCA_TYPE_MASK)

static const struct i2c_device_id pca953x_id[] = {
	{ "pca6408", 8  | PCA953X_TYPE | PCA_INT, },
	{ "pca6416", 16 | PCA953X_TYPE | PCA_INT, },
	{ "pca9505", 40 | PCA953X_TYPE | PCA_INT, },
	{ "pca9506", 40 | PCA953X_TYPE | PCA_INT, },
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

	{ "pcal6408", 8 | PCA953X_TYPE | PCA_LATCH_INT, },
	{ "pcal6416", 16 | PCA953X_TYPE | PCA_LATCH_INT, },
	{ "pcal6524", 24 | PCA953X_TYPE | PCA_LATCH_INT, },
	{ "pcal6534", 34 | PCAL653X_TYPE | PCA_LATCH_INT, },
	{ "pcal9535", 16 | PCA953X_TYPE | PCA_LATCH_INT, },
	{ "pcal9554b", 8  | PCA953X_TYPE | PCA_LATCH_INT, },
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

#ifdef CONFIG_GPIO_PCA953X_IRQ

#include <linux/dmi.h>

static const struct acpi_gpio_params pca953x_irq_gpios = { 0, 0, true };

static const struct acpi_gpio_mapping pca953x_acpi_irq_gpios[] = {
	{ "irq-gpios", &pca953x_irq_gpios, 1, ACPI_GPIO_QUIRK_ABSOLUTE_NUMBER },
	{ }
};

static int pca953x_acpi_get_irq(struct device *dev)
{
	int ret;

	ret = devm_acpi_dev_add_driver_gpios(dev, pca953x_acpi_irq_gpios);
	if (ret)
		dev_warn(dev, "can't add GPIO ACPI mapping\n");

	ret = acpi_dev_gpio_irq_get_by(ACPI_COMPANION(dev), "irq-gpios", 0);
	if (ret < 0)
		return ret;

	dev_info(dev, "ACPI interrupt quirk (IRQ %d)\n", ret);
	return ret;
}

static const struct dmi_system_id pca953x_dmi_acpi_irq_info[] = {
	{
		/*
		 * On Intel Galileo Gen 2 board the IRQ pin of one of
		 * the I²C GPIO expanders, which has GpioInt() resource,
		 * is provided as an absolute number instead of being
		 * relative. Since first controller (gpio-sch.c) and
		 * second (gpio-dwapb.c) are at the fixed bases, we may
		 * safely refer to the number in the global space to get
		 * an IRQ out of it.
		 */
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GalileoGen2"),
		},
	},
	{}
};
#endif

static const struct acpi_device_id pca953x_acpi_ids[] = {
	{ "INT3491", 16 | PCA953X_TYPE | PCA_LATCH_INT, },
	{ }
};
MODULE_DEVICE_TABLE(acpi, pca953x_acpi_ids);

#define MAX_BANK 5
#define BANK_SZ 8
#define MAX_LINE	(MAX_BANK * BANK_SZ)

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
	DECLARE_BITMAP(irq_mask, MAX_LINE);
	DECLARE_BITMAP(irq_stat, MAX_LINE);
	DECLARE_BITMAP(irq_trig_raise, MAX_LINE);
	DECLARE_BITMAP(irq_trig_fall, MAX_LINE);
#endif
	atomic_t wakeup_path;

	struct i2c_client *client;
	struct gpio_chip gpio_chip;
	const char *const *names;
	unsigned long driver_data;
	struct regulator *regulator;

	const struct pca953x_reg_config *regs;

	u8 (*recalc_addr)(struct pca953x_chip *chip, int reg, int off);
	bool (*check_reg)(struct pca953x_chip *chip, unsigned int reg,
			  u32 checkbank);
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

/*
 * Unfortunately, whilst the PCAL6534 chip (and compatibles) broadly follow the
 * same register layout as the PCAL6524, the spacing of the registers has been
 * fundamentally altered by compacting them and thus does not obey the same
 * rules, including being able to use bit shifting to determine bank. These
 * chips hence need special handling here.
 */
static bool pcal6534_check_register(struct pca953x_chip *chip, unsigned int reg,
				    u32 checkbank)
{
	int bank;
	int offset;

	if (reg >= 0x30) {
		/*
		 * Reserved block between 14h and 2Fh does not align on
		 * expected bank boundaries like other devices.
		 */
		int temp = reg - 0x30;

		bank = temp / NBANK(chip);
		offset = temp - (bank * NBANK(chip));
		bank += 8;
	} else if (reg >= 0x54) {
		/*
		 * Handle lack of reserved registers after output port
		 * configuration register to form a bank.
		 */
		int temp = reg - 0x54;

		bank = temp / NBANK(chip);
		offset = temp - (bank * NBANK(chip));
		bank += 16;
	} else {
		bank = reg / NBANK(chip);
		offset = reg - (bank * NBANK(chip));
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

	if (PCA_CHIP_TYPE(chip->driver_data) == PCA957X_TYPE) {
		bank = PCA957x_BANK_INPUT | PCA957x_BANK_OUTPUT |
		       PCA957x_BANK_POLARITY | PCA957x_BANK_CONFIG |
		       PCA957x_BANK_BUSHOLD;
	} else {
		bank = PCA953x_BANK_INPUT | PCA953x_BANK_OUTPUT |
		       PCA953x_BANK_POLARITY | PCA953x_BANK_CONFIG;
	}

	if (chip->driver_data & PCA_PCAL) {
		bank |= PCAL9xxx_BANK_IN_LATCH | PCAL9xxx_BANK_PULL_EN |
			PCAL9xxx_BANK_PULL_SEL | PCAL9xxx_BANK_IRQ_MASK |
			PCAL9xxx_BANK_IRQ_STAT;
	}

	return chip->check_reg(chip, reg, bank);
}

static bool pca953x_writeable_register(struct device *dev, unsigned int reg)
{
	struct pca953x_chip *chip = dev_get_drvdata(dev);
	u32 bank;

	if (PCA_CHIP_TYPE(chip->driver_data) == PCA957X_TYPE) {
		bank = PCA957x_BANK_OUTPUT | PCA957x_BANK_POLARITY |
			PCA957x_BANK_CONFIG | PCA957x_BANK_BUSHOLD;
	} else {
		bank = PCA953x_BANK_OUTPUT | PCA953x_BANK_POLARITY |
			PCA953x_BANK_CONFIG;
	}

	if (chip->driver_data & PCA_PCAL)
		bank |= PCAL9xxx_BANK_IN_LATCH | PCAL9xxx_BANK_PULL_EN |
			PCAL9xxx_BANK_PULL_SEL | PCAL9xxx_BANK_IRQ_MASK;

	return chip->check_reg(chip, reg, bank);
}

static bool pca953x_volatile_register(struct device *dev, unsigned int reg)
{
	struct pca953x_chip *chip = dev_get_drvdata(dev);
	u32 bank;

	if (PCA_CHIP_TYPE(chip->driver_data) == PCA957X_TYPE)
		bank = PCA957x_BANK_INPUT;
	else
		bank = PCA953x_BANK_INPUT;

	if (chip->driver_data & PCA_PCAL)
		bank |= PCAL9xxx_BANK_IRQ_STAT;

	return chip->check_reg(chip, reg, bank);
}

static const struct regmap_config pca953x_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.use_single_read = true,
	.use_single_write = true,

	.readable_reg = pca953x_readable_register,
	.writeable_reg = pca953x_writeable_register,
	.volatile_reg = pca953x_volatile_register,

	.disable_locking = true,
	.cache_type = REGCACHE_RBTREE,
	.max_register = 0x7f,
};

static const struct regmap_config pca953x_ai_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.read_flag_mask = REG_ADDR_AI,
	.write_flag_mask = REG_ADDR_AI,

	.readable_reg = pca953x_readable_register,
	.writeable_reg = pca953x_writeable_register,
	.volatile_reg = pca953x_volatile_register,

	.disable_locking = true,
	.cache_type = REGCACHE_RBTREE,
	.max_register = 0x7f,
};

static u8 pca953x_recalc_addr(struct pca953x_chip *chip, int reg, int off)
{
	int bank_shift = pca953x_bank_shift(chip);
	int addr = (reg & PCAL_GPIO_MASK) << bank_shift;
	int pinctrl = (reg & PCAL_PINCTRL_MASK) << 1;
	u8 regaddr = pinctrl | addr | (off / BANK_SZ);

	return regaddr;
}

/*
 * The PCAL6534 and compatible chips have altered bank alignment that doesn't
 * fit within the bit shifting scheme used for other devices.
 */
static u8 pcal6534_recalc_addr(struct pca953x_chip *chip, int reg, int off)
{
	int addr;
	int pinctrl;

	addr = (reg & PCAL_GPIO_MASK) * NBANK(chip);

	switch (reg) {
	case PCAL953X_OUT_STRENGTH:
	case PCAL953X_IN_LATCH:
	case PCAL953X_PULL_EN:
	case PCAL953X_PULL_SEL:
	case PCAL953X_INT_MASK:
	case PCAL953X_INT_STAT:
	case PCAL953X_OUT_CONF:
		pinctrl = ((reg & PCAL_PINCTRL_MASK) >> 1) + 0x20;
		break;
	case PCAL6524_INT_EDGE:
	case PCAL6524_INT_CLR:
	case PCAL6524_IN_STATUS:
	case PCAL6524_OUT_INDCONF:
	case PCAL6524_DEBOUNCE:
		pinctrl = ((reg & PCAL_PINCTRL_MASK) >> 1) + 0x1c;
		break;
	}

	return pinctrl + addr + (off / BANK_SZ);
}

static int pca953x_write_regs(struct pca953x_chip *chip, int reg, unsigned long *val)
{
	u8 regaddr = chip->recalc_addr(chip, reg, 0);
	u8 value[MAX_BANK];
	int i, ret;

	for (i = 0; i < NBANK(chip); i++)
		value[i] = bitmap_get_value8(val, i * BANK_SZ);

	ret = regmap_bulk_write(chip->regmap, regaddr, value, NBANK(chip));
	if (ret < 0) {
		dev_err(&chip->client->dev, "failed writing register\n");
		return ret;
	}

	return 0;
}

static int pca953x_read_regs(struct pca953x_chip *chip, int reg, unsigned long *val)
{
	u8 regaddr = chip->recalc_addr(chip, reg, 0);
	u8 value[MAX_BANK];
	int i, ret;

	ret = regmap_bulk_read(chip->regmap, regaddr, value, NBANK(chip));
	if (ret < 0) {
		dev_err(&chip->client->dev, "failed reading register\n");
		return ret;
	}

	for (i = 0; i < NBANK(chip); i++)
		bitmap_set_value8(val, value[i], i * BANK_SZ);

	return 0;
}

static int pca953x_gpio_direction_input(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 dirreg = chip->recalc_addr(chip, chip->regs->direction, off);
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
	u8 dirreg = chip->recalc_addr(chip, chip->regs->direction, off);
	u8 outreg = chip->recalc_addr(chip, chip->regs->output, off);
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
	u8 inreg = chip->recalc_addr(chip, chip->regs->input, off);
	u8 bit = BIT(off % BANK_SZ);
	u32 reg_val;
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = regmap_read(chip->regmap, inreg, &reg_val);
	mutex_unlock(&chip->i2c_lock);
	if (ret < 0)
		return ret;

	return !!(reg_val & bit);
}

static void pca953x_gpio_set_value(struct gpio_chip *gc, unsigned off, int val)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 outreg = chip->recalc_addr(chip, chip->regs->output, off);
	u8 bit = BIT(off % BANK_SZ);

	mutex_lock(&chip->i2c_lock);
	regmap_write_bits(chip->regmap, outreg, bit, val ? bit : 0);
	mutex_unlock(&chip->i2c_lock);
}

static int pca953x_gpio_get_direction(struct gpio_chip *gc, unsigned off)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	u8 dirreg = chip->recalc_addr(chip, chip->regs->direction, off);
	u8 bit = BIT(off % BANK_SZ);
	u32 reg_val;
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = regmap_read(chip->regmap, dirreg, &reg_val);
	mutex_unlock(&chip->i2c_lock);
	if (ret < 0)
		return ret;

	if (reg_val & bit)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int pca953x_gpio_get_multiple(struct gpio_chip *gc,
				     unsigned long *mask, unsigned long *bits)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	DECLARE_BITMAP(reg_val, MAX_LINE);
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = pca953x_read_regs(chip, chip->regs->input, reg_val);
	mutex_unlock(&chip->i2c_lock);
	if (ret)
		return ret;

	bitmap_replace(bits, bits, reg_val, mask, gc->ngpio);
	return 0;
}

static void pca953x_gpio_set_multiple(struct gpio_chip *gc,
				      unsigned long *mask, unsigned long *bits)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	DECLARE_BITMAP(reg_val, MAX_LINE);
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = pca953x_read_regs(chip, chip->regs->output, reg_val);
	if (ret)
		goto exit;

	bitmap_replace(reg_val, reg_val, bits, mask, gc->ngpio);

	pca953x_write_regs(chip, chip->regs->output, reg_val);
exit:
	mutex_unlock(&chip->i2c_lock);
}

static int pca953x_gpio_set_pull_up_down(struct pca953x_chip *chip,
					 unsigned int offset,
					 unsigned long config)
{
	enum pin_config_param param = pinconf_to_config_param(config);

	u8 pull_en_reg = chip->recalc_addr(chip, PCAL953X_PULL_EN, offset);
	u8 pull_sel_reg = chip->recalc_addr(chip, PCAL953X_PULL_SEL, offset);
	u8 bit = BIT(offset % BANK_SZ);
	int ret;

	/*
	 * pull-up/pull-down configuration requires PCAL extended
	 * registers
	 */
	if (!(chip->driver_data & PCA_PCAL))
		return -ENOTSUPP;

	mutex_lock(&chip->i2c_lock);

	/* Configure pull-up/pull-down */
	if (param == PIN_CONFIG_BIAS_PULL_UP)
		ret = regmap_write_bits(chip->regmap, pull_sel_reg, bit, bit);
	else if (param == PIN_CONFIG_BIAS_PULL_DOWN)
		ret = regmap_write_bits(chip->regmap, pull_sel_reg, bit, 0);
	else
		ret = 0;
	if (ret)
		goto exit;

	/* Disable/Enable pull-up/pull-down */
	if (param == PIN_CONFIG_BIAS_DISABLE)
		ret = regmap_write_bits(chip->regmap, pull_en_reg, bit, 0);
	else
		ret = regmap_write_bits(chip->regmap, pull_en_reg, bit, bit);

exit:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

static int pca953x_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
				   unsigned long config)
{
	struct pca953x_chip *chip = gpiochip_get_data(gc);

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_DISABLE:
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
	gc->get_multiple = pca953x_gpio_get_multiple;
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
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	clear_bit(hwirq, chip->irq_mask);
	gpiochip_disable_irq(gc, hwirq);
}

static void pca953x_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gc, hwirq);
	set_bit(hwirq, chip->irq_mask);
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
	DECLARE_BITMAP(irq_mask, MAX_LINE);
	DECLARE_BITMAP(reg_direction, MAX_LINE);
	int level;

	if (chip->driver_data & PCA_PCAL) {
		/* Enable latch on interrupt-enabled inputs */
		pca953x_write_regs(chip, PCAL953X_IN_LATCH, chip->irq_mask);

		bitmap_complement(irq_mask, chip->irq_mask, gc->ngpio);

		/* Unmask enabled interrupts */
		pca953x_write_regs(chip, PCAL953X_INT_MASK, irq_mask);
	}

	/* Switch direction to input if needed */
	pca953x_read_regs(chip, chip->regs->direction, reg_direction);

	bitmap_or(irq_mask, chip->irq_trig_fall, chip->irq_trig_raise, gc->ngpio);
	bitmap_complement(reg_direction, reg_direction, gc->ngpio);
	bitmap_and(irq_mask, irq_mask, reg_direction, gc->ngpio);

	/* Look for any newly setup interrupt */
	for_each_set_bit(level, irq_mask, gc->ngpio)
		pca953x_gpio_direction_input(&chip->gpio_chip, level);

	mutex_unlock(&chip->irq_lock);
}

static int pca953x_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	if (!(type & IRQ_TYPE_EDGE_BOTH)) {
		dev_err(&chip->client->dev, "irq %d: unsupported type %d\n",
			d->irq, type);
		return -EINVAL;
	}

	assign_bit(hwirq, chip->irq_trig_fall, type & IRQ_TYPE_EDGE_FALLING);
	assign_bit(hwirq, chip->irq_trig_raise, type & IRQ_TYPE_EDGE_RISING);

	return 0;
}

static void pca953x_irq_shutdown(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pca953x_chip *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	clear_bit(hwirq, chip->irq_trig_raise);
	clear_bit(hwirq, chip->irq_trig_fall);
}

static void pca953x_irq_print_chip(struct irq_data *data, struct seq_file *p)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);

	seq_printf(p, dev_name(gc->parent));
}

static const struct irq_chip pca953x_irq_chip = {
	.irq_mask		= pca953x_irq_mask,
	.irq_unmask		= pca953x_irq_unmask,
	.irq_set_wake		= pca953x_irq_set_wake,
	.irq_bus_lock		= pca953x_irq_bus_lock,
	.irq_bus_sync_unlock	= pca953x_irq_bus_sync_unlock,
	.irq_set_type		= pca953x_irq_set_type,
	.irq_shutdown		= pca953x_irq_shutdown,
	.irq_print_chip		= pca953x_irq_print_chip,
	.flags			= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static bool pca953x_irq_pending(struct pca953x_chip *chip, unsigned long *pending)
{
	struct gpio_chip *gc = &chip->gpio_chip;
	DECLARE_BITMAP(reg_direction, MAX_LINE);
	DECLARE_BITMAP(old_stat, MAX_LINE);
	DECLARE_BITMAP(cur_stat, MAX_LINE);
	DECLARE_BITMAP(new_stat, MAX_LINE);
	DECLARE_BITMAP(trigger, MAX_LINE);
	int ret;

	if (chip->driver_data & PCA_PCAL) {
		/* Read the current interrupt status from the device */
		ret = pca953x_read_regs(chip, PCAL953X_INT_STAT, trigger);
		if (ret)
			return false;

		/* Check latched inputs and clear interrupt status */
		ret = pca953x_read_regs(chip, chip->regs->input, cur_stat);
		if (ret)
			return false;

		/* Apply filter for rising/falling edge selection */
		bitmap_replace(new_stat, chip->irq_trig_fall, chip->irq_trig_raise, cur_stat, gc->ngpio);

		bitmap_and(pending, new_stat, trigger, gc->ngpio);

		return !bitmap_empty(pending, gc->ngpio);
	}

	ret = pca953x_read_regs(chip, chip->regs->input, cur_stat);
	if (ret)
		return false;

	/* Remove output pins from the equation */
	pca953x_read_regs(chip, chip->regs->direction, reg_direction);

	bitmap_copy(old_stat, chip->irq_stat, gc->ngpio);

	bitmap_and(new_stat, cur_stat, reg_direction, gc->ngpio);
	bitmap_xor(cur_stat, new_stat, old_stat, gc->ngpio);
	bitmap_and(trigger, cur_stat, chip->irq_mask, gc->ngpio);

	bitmap_copy(chip->irq_stat, new_stat, gc->ngpio);

	if (bitmap_empty(trigger, gc->ngpio))
		return false;

	bitmap_and(cur_stat, chip->irq_trig_fall, old_stat, gc->ngpio);
	bitmap_and(old_stat, chip->irq_trig_raise, new_stat, gc->ngpio);
	bitmap_or(new_stat, old_stat, cur_stat, gc->ngpio);
	bitmap_and(pending, new_stat, trigger, gc->ngpio);

	return !bitmap_empty(pending, gc->ngpio);
}

static irqreturn_t pca953x_irq_handler(int irq, void *devid)
{
	struct pca953x_chip *chip = devid;
	struct gpio_chip *gc = &chip->gpio_chip;
	DECLARE_BITMAP(pending, MAX_LINE);
	int level;
	bool ret;

	bitmap_zero(pending, MAX_LINE);

	mutex_lock(&chip->i2c_lock);
	ret = pca953x_irq_pending(chip, pending);
	mutex_unlock(&chip->i2c_lock);

	if (ret) {
		ret = 0;

		for_each_set_bit(level, pending, gc->ngpio) {
			int nested_irq = irq_find_mapping(gc->irq.domain, level);

			if (unlikely(nested_irq <= 0)) {
				dev_warn_ratelimited(gc->parent, "unmapped interrupt %d\n", level);
				continue;
			}

			handle_nested_irq(nested_irq);
			ret = 1;
		}
	}

	return IRQ_RETVAL(ret);
}

static int pca953x_irq_setup(struct pca953x_chip *chip, int irq_base)
{
	struct i2c_client *client = chip->client;
	DECLARE_BITMAP(reg_direction, MAX_LINE);
	DECLARE_BITMAP(irq_stat, MAX_LINE);
	struct gpio_irq_chip *girq;
	int ret;

	if (dmi_first_match(pca953x_dmi_acpi_irq_info)) {
		ret = pca953x_acpi_get_irq(&client->dev);
		if (ret > 0)
			client->irq = ret;
	}

	if (!client->irq)
		return 0;

	if (irq_base == -1)
		return 0;

	if (!(chip->driver_data & PCA_INT))
		return 0;

	ret = pca953x_read_regs(chip, chip->regs->input, irq_stat);
	if (ret)
		return ret;

	/*
	 * There is no way to know which GPIO line generated the
	 * interrupt.  We have to rely on the previous read for
	 * this purpose.
	 */
	pca953x_read_regs(chip, chip->regs->direction, reg_direction);
	bitmap_and(chip->irq_stat, irq_stat, reg_direction, chip->gpio_chip.ngpio);
	mutex_init(&chip->irq_lock);

	girq = &chip->gpio_chip.irq;
	gpio_irq_chip_set_chip(girq, &pca953x_irq_chip);
	/* This will let us handle the parent IRQ in the driver */
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_simple_irq;
	girq->threaded = true;
	girq->first = irq_base; /* FIXME: get rid of this */

	ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, pca953x_irq_handler,
					IRQF_ONESHOT | IRQF_SHARED,
					dev_name(&client->dev), chip);
	if (ret) {
		dev_err(&client->dev, "failed to request irq %d\n",
			client->irq);
		return ret;
	}

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
	DECLARE_BITMAP(val, MAX_LINE);
	u8 regaddr;
	int ret;

	regaddr = chip->recalc_addr(chip, chip->regs->output, 0);
	ret = regcache_sync_region(chip->regmap, regaddr,
				   regaddr + NBANK(chip) - 1);
	if (ret)
		goto out;

	regaddr = chip->recalc_addr(chip, chip->regs->direction, 0);
	ret = regcache_sync_region(chip->regmap, regaddr,
				   regaddr + NBANK(chip) - 1);
	if (ret)
		goto out;

	/* set platform specific polarity inversion */
	if (invert)
		bitmap_fill(val, MAX_LINE);
	else
		bitmap_zero(val, MAX_LINE);

	ret = pca953x_write_regs(chip, chip->regs->invert, val);
out:
	return ret;
}

static int device_pca957x_init(struct pca953x_chip *chip, u32 invert)
{
	DECLARE_BITMAP(val, MAX_LINE);
	unsigned int i;
	int ret;

	ret = device_pca95xx_init(chip, invert);
	if (ret)
		goto out;

	/* To enable register 6, 7 to control pull up and pull down */
	for (i = 0; i < NBANK(chip); i++)
		bitmap_set_value8(val, 0x02, i * BANK_SZ);

	ret = pca953x_write_regs(chip, PCA957X_BKEN, val);
	if (ret)
		goto out;

	return 0;
out:
	return ret;
}

static int pca953x_probe(struct i2c_client *client,
			 const struct i2c_device_id *i2c_id)
{
	struct pca953x_platform_data *pdata;
	struct pca953x_chip *chip;
	int irq_base = 0;
	int ret;
	u32 invert = 0;
	struct regulator *reg;
	const struct regmap_config *regmap_config;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
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
	if (IS_ERR(reg))
		return dev_err_probe(&client->dev, PTR_ERR(reg), "reg get err\n");

	ret = regulator_enable(reg);
	if (ret) {
		dev_err(&client->dev, "reg en err: %d\n", ret);
		return ret;
	}
	chip->regulator = reg;

	if (i2c_id) {
		chip->driver_data = i2c_id->driver_data;
	} else {
		const void *match;

		match = device_get_match_data(&client->dev);
		if (!match) {
			ret = -ENODEV;
			goto err_exit;
		}

		chip->driver_data = (uintptr_t)match;
	}

	i2c_set_clientdata(client, chip);

	pca953x_setup_gpio(chip, chip->driver_data & PCA_GPIO_MASK);

	if (NBANK(chip) > 2 || PCA_CHIP_TYPE(chip->driver_data) == PCA957X_TYPE) {
		dev_info(&client->dev, "using AI\n");
		regmap_config = &pca953x_ai_i2c_regmap;
	} else {
		dev_info(&client->dev, "using no AI\n");
		regmap_config = &pca953x_i2c_regmap;
	}

	if (PCA_CHIP_TYPE(chip->driver_data) == PCAL653X_TYPE) {
		chip->recalc_addr = pcal6534_recalc_addr;
		chip->check_reg = pcal6534_check_register;
	} else {
		chip->recalc_addr = pca953x_recalc_addr;
		chip->check_reg = pca953x_check_register;
	}

	chip->regmap = devm_regmap_init_i2c(client, regmap_config);
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
	if (PCA_CHIP_TYPE(chip->driver_data) == PCA957X_TYPE) {
		chip->regs = &pca957x_regs;
		ret = device_pca957x_init(chip, invert);
	} else {
		chip->regs = &pca953x_regs;
		ret = device_pca95xx_init(chip, invert);
	}
	if (ret)
		goto err_exit;

	ret = pca953x_irq_setup(chip, irq_base);
	if (ret)
		goto err_exit;

	ret = devm_gpiochip_add_data(&client->dev, &chip->gpio_chip, chip);
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

static void pca953x_remove(struct i2c_client *client)
{
	struct pca953x_platform_data *pdata = dev_get_platdata(&client->dev);
	struct pca953x_chip *chip = i2c_get_clientdata(client);

	if (pdata && pdata->teardown) {
		pdata->teardown(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
	}

	regulator_disable(chip->regulator);
}

#ifdef CONFIG_PM_SLEEP
static int pca953x_regcache_sync(struct device *dev)
{
	struct pca953x_chip *chip = dev_get_drvdata(dev);
	int ret;
	u8 regaddr;

	/*
	 * The ordering between direction and output is important,
	 * sync these registers first and only then sync the rest.
	 */
	regaddr = chip->recalc_addr(chip, chip->regs->direction, 0);
	ret = regcache_sync_region(chip->regmap, regaddr, regaddr + NBANK(chip) - 1);
	if (ret) {
		dev_err(dev, "Failed to sync GPIO dir registers: %d\n", ret);
		return ret;
	}

	regaddr = chip->recalc_addr(chip, chip->regs->output, 0);
	ret = regcache_sync_region(chip->regmap, regaddr, regaddr + NBANK(chip) - 1);
	if (ret) {
		dev_err(dev, "Failed to sync GPIO out registers: %d\n", ret);
		return ret;
	}

#ifdef CONFIG_GPIO_PCA953X_IRQ
	if (chip->driver_data & PCA_PCAL) {
		regaddr = chip->recalc_addr(chip, PCAL953X_IN_LATCH, 0);
		ret = regcache_sync_region(chip->regmap, regaddr,
					   regaddr + NBANK(chip) - 1);
		if (ret) {
			dev_err(dev, "Failed to sync INT latch registers: %d\n",
				ret);
			return ret;
		}

		regaddr = chip->recalc_addr(chip, PCAL953X_INT_MASK, 0);
		ret = regcache_sync_region(chip->regmap, regaddr,
					   regaddr + NBANK(chip) - 1);
		if (ret) {
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

	mutex_lock(&chip->i2c_lock);
	regcache_cache_only(chip->regmap, true);
	mutex_unlock(&chip->i2c_lock);

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
		if (ret) {
			dev_err(dev, "Failed to enable regulator: %d\n", ret);
			return 0;
		}
	}

	mutex_lock(&chip->i2c_lock);
	regcache_cache_only(chip->regmap, false);
	regcache_mark_dirty(chip->regmap);
	ret = pca953x_regcache_sync(dev);
	if (ret) {
		mutex_unlock(&chip->i2c_lock);
		return ret;
	}

	ret = regcache_sync(chip->regmap);
	mutex_unlock(&chip->i2c_lock);
	if (ret) {
		dev_err(dev, "Failed to restore register map: %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

/* convenience to stop overlong match-table lines */
#define OF_653X(__nrgpio, __int) ((void *)(__nrgpio | PCAL653X_TYPE | __int))
#define OF_953X(__nrgpio, __int) (void *)(__nrgpio | PCA953X_TYPE | __int)
#define OF_957X(__nrgpio, __int) (void *)(__nrgpio | PCA957X_TYPE | __int)

static const struct of_device_id pca953x_dt_ids[] = {
	{ .compatible = "nxp,pca6408", .data = OF_953X(8, PCA_INT), },
	{ .compatible = "nxp,pca6416", .data = OF_953X(16, PCA_INT), },
	{ .compatible = "nxp,pca9505", .data = OF_953X(40, PCA_INT), },
	{ .compatible = "nxp,pca9506", .data = OF_953X(40, PCA_INT), },
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

	{ .compatible = "nxp,pcal6408", .data = OF_953X(8, PCA_LATCH_INT), },
	{ .compatible = "nxp,pcal6416", .data = OF_953X(16, PCA_LATCH_INT), },
	{ .compatible = "nxp,pcal6524", .data = OF_953X(24, PCA_LATCH_INT), },
	{ .compatible = "nxp,pcal6534", .data = OF_653X(34, PCA_LATCH_INT), },
	{ .compatible = "nxp,pcal9535", .data = OF_953X(16, PCA_LATCH_INT), },
	{ .compatible = "nxp,pcal9554b", .data = OF_953X( 8, PCA_LATCH_INT), },
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
	{ .compatible = "onnn,pca9655", .data = OF_953X(16, PCA_INT), },

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
		.acpi_match_table = pca953x_acpi_ids,
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
