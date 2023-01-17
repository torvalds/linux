// SPDX-License-Identifier: GPL-2.0-only
/*
 * CY8C95X0 20/40/60 pin I2C GPIO port expander with interrupt support
 *
 * Copyright (C) 2022 9elements GmbH
 * Authors: Patrick Rudolph <patrick.rudolph@9elements.com>
 *	    Naresh Solanki <Naresh.Solanki@9elements.com>
 */

#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <linux/dmi.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/seq_file.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

/* Fast access registers */
#define CY8C95X0_INPUT		0x00
#define CY8C95X0_OUTPUT		0x08
#define CY8C95X0_INTSTATUS	0x10

#define CY8C95X0_INPUT_(x)	(CY8C95X0_INPUT + (x))
#define CY8C95X0_OUTPUT_(x)	(CY8C95X0_OUTPUT + (x))
#define CY8C95X0_INTSTATUS_(x)	(CY8C95X0_INTSTATUS + (x))

/* Port Select configures the port */
#define CY8C95X0_PORTSEL	0x18
/* Port settings, write PORTSEL first */
#define CY8C95X0_INTMASK	0x19
#define CY8C95X0_PWMSEL		0x1A
#define CY8C95X0_INVERT		0x1B
#define CY8C95X0_DIRECTION	0x1C
/* Drive mode register change state on writing '1' */
#define CY8C95X0_DRV_PU		0x1D
#define CY8C95X0_DRV_PD		0x1E
#define CY8C95X0_DRV_ODH	0x1F
#define CY8C95X0_DRV_ODL	0x20
#define CY8C95X0_DRV_PP_FAST	0x21
#define CY8C95X0_DRV_PP_SLOW	0x22
#define CY8C95X0_DRV_HIZ	0x23
#define CY8C95X0_DEVID		0x2E
#define CY8C95X0_WATCHDOG	0x2F
#define CY8C95X0_COMMAND	0x30

#define CY8C95X0_PIN_TO_OFFSET(x) (((x) >= 20) ? ((x) + 4) : (x))

static const struct i2c_device_id cy8c95x0_id[] = {
	{ "cy8c9520", 20, },
	{ "cy8c9540", 40, },
	{ "cy8c9560", 60, },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cy8c95x0_id);

#define OF_CY8C95X(__nrgpio) ((void *)(__nrgpio))

static const struct of_device_id cy8c95x0_dt_ids[] = {
	{ .compatible = "cypress,cy8c9520", .data = OF_CY8C95X(20), },
	{ .compatible = "cypress,cy8c9540", .data = OF_CY8C95X(40), },
	{ .compatible = "cypress,cy8c9560", .data = OF_CY8C95X(60), },
	{ }
};
MODULE_DEVICE_TABLE(of, cy8c95x0_dt_ids);

static const struct acpi_gpio_params cy8c95x0_irq_gpios = { 0, 0, true };

static const struct acpi_gpio_mapping cy8c95x0_acpi_irq_gpios[] = {
	{ "irq-gpios", &cy8c95x0_irq_gpios, 1, ACPI_GPIO_QUIRK_ABSOLUTE_NUMBER },
	{ }
};

static int cy8c95x0_acpi_get_irq(struct device *dev)
{
	int ret;

	ret = devm_acpi_dev_add_driver_gpios(dev, cy8c95x0_acpi_irq_gpios);
	if (ret)
		dev_warn(dev, "can't add GPIO ACPI mapping\n");

	ret = acpi_dev_gpio_irq_get_by(ACPI_COMPANION(dev), "irq-gpios", 0);
	if (ret < 0)
		return ret;

	dev_info(dev, "ACPI interrupt quirk (IRQ %d)\n", ret);
	return ret;
}

static const struct dmi_system_id cy8c95x0_dmi_acpi_irq_info[] = {
	{
		/*
		 * On Intel Galileo Gen 1 board the IRQ pin is provided
		 * as an absolute number instead of being relative.
		 * Since first controller (gpio-sch.c) and second
		 * (gpio-dwapb.c) are at the fixed bases, we may safely
		 * refer to the number in the global space to get an IRQ
		 * out of it.
		 */
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "Galileo"),
		},
	},
	{}
};

#define MAX_BANK 8
#define BANK_SZ 8
#define MAX_LINE	(MAX_BANK * BANK_SZ)

#define CY8C95X0_GPIO_MASK		GENMASK(7, 0)

/**
 * struct cy8c95x0_pinctrl - driver data
 * @regmap:         Device's regmap
 * @irq_lock:       IRQ bus lock
 * @i2c_lock:       Mutex for the device internal mux register
 * @irq_mask:       I/O bits affected by interrupts
 * @irq_trig_raise: I/O bits affected by raising voltage level
 * @irq_trig_fall:  I/O bits affected by falling voltage level
 * @irq_trig_low:   I/O bits affected by a low voltage level
 * @irq_trig_high:  I/O bits affected by a high voltage level
 * @push_pull:      I/O bits configured as push pull driver
 * @shiftmask:      Mask used to compensate for Gport2 width
 * @nport:          Number of Gports in this chip
 * @gpio_chip:      gpiolib chip
 * @driver_data:    private driver data
 * @regulator:      Pointer to the regulator for the IC
 * @dev:            struct device
 * @pctldev:        pin controller device
 * @pinctrl_desc:   pin controller description
 * @name:           Chip controller name
 * @tpin:           Total number of pins
 */
struct cy8c95x0_pinctrl {
	struct regmap *regmap;
	struct mutex irq_lock;
	struct mutex i2c_lock;
	DECLARE_BITMAP(irq_mask, MAX_LINE);
	DECLARE_BITMAP(irq_trig_raise, MAX_LINE);
	DECLARE_BITMAP(irq_trig_fall, MAX_LINE);
	DECLARE_BITMAP(irq_trig_low, MAX_LINE);
	DECLARE_BITMAP(irq_trig_high, MAX_LINE);
	DECLARE_BITMAP(push_pull, MAX_LINE);
	DECLARE_BITMAP(shiftmask, MAX_LINE);
	int nport;
	struct gpio_chip gpio_chip;
	unsigned long driver_data;
	struct regulator *regulator;
	struct device *dev;
	struct pinctrl_dev *pctldev;
	struct pinctrl_desc pinctrl_desc;
	char name[32];
	unsigned int tpin;
};

static const struct pinctrl_pin_desc cy8c9560_pins[] = {
	PINCTRL_PIN(0, "gp00"),
	PINCTRL_PIN(1, "gp01"),
	PINCTRL_PIN(2, "gp02"),
	PINCTRL_PIN(3, "gp03"),
	PINCTRL_PIN(4, "gp04"),
	PINCTRL_PIN(5, "gp05"),
	PINCTRL_PIN(6, "gp06"),
	PINCTRL_PIN(7, "gp07"),

	PINCTRL_PIN(8, "gp10"),
	PINCTRL_PIN(9, "gp11"),
	PINCTRL_PIN(10, "gp12"),
	PINCTRL_PIN(11, "gp13"),
	PINCTRL_PIN(12, "gp14"),
	PINCTRL_PIN(13, "gp15"),
	PINCTRL_PIN(14, "gp16"),
	PINCTRL_PIN(15, "gp17"),

	PINCTRL_PIN(16, "gp20"),
	PINCTRL_PIN(17, "gp21"),
	PINCTRL_PIN(18, "gp22"),
	PINCTRL_PIN(19, "gp23"),

	PINCTRL_PIN(20, "gp30"),
	PINCTRL_PIN(21, "gp31"),
	PINCTRL_PIN(22, "gp32"),
	PINCTRL_PIN(23, "gp33"),
	PINCTRL_PIN(24, "gp34"),
	PINCTRL_PIN(25, "gp35"),
	PINCTRL_PIN(26, "gp36"),
	PINCTRL_PIN(27, "gp37"),

	PINCTRL_PIN(28, "gp40"),
	PINCTRL_PIN(29, "gp41"),
	PINCTRL_PIN(30, "gp42"),
	PINCTRL_PIN(31, "gp43"),
	PINCTRL_PIN(32, "gp44"),
	PINCTRL_PIN(33, "gp45"),
	PINCTRL_PIN(34, "gp46"),
	PINCTRL_PIN(35, "gp47"),

	PINCTRL_PIN(36, "gp50"),
	PINCTRL_PIN(37, "gp51"),
	PINCTRL_PIN(38, "gp52"),
	PINCTRL_PIN(39, "gp53"),
	PINCTRL_PIN(40, "gp54"),
	PINCTRL_PIN(41, "gp55"),
	PINCTRL_PIN(42, "gp56"),
	PINCTRL_PIN(43, "gp57"),

	PINCTRL_PIN(44, "gp60"),
	PINCTRL_PIN(45, "gp61"),
	PINCTRL_PIN(46, "gp62"),
	PINCTRL_PIN(47, "gp63"),
	PINCTRL_PIN(48, "gp64"),
	PINCTRL_PIN(49, "gp65"),
	PINCTRL_PIN(50, "gp66"),
	PINCTRL_PIN(51, "gp67"),

	PINCTRL_PIN(52, "gp70"),
	PINCTRL_PIN(53, "gp71"),
	PINCTRL_PIN(54, "gp72"),
	PINCTRL_PIN(55, "gp73"),
	PINCTRL_PIN(56, "gp74"),
	PINCTRL_PIN(57, "gp75"),
	PINCTRL_PIN(58, "gp76"),
	PINCTRL_PIN(59, "gp77"),
};

static const char * const cy8c95x0_groups[] = {
	"gp00",
	"gp01",
	"gp02",
	"gp03",
	"gp04",
	"gp05",
	"gp06",
	"gp07",

	"gp10",
	"gp11",
	"gp12",
	"gp13",
	"gp14",
	"gp15",
	"gp16",
	"gp17",

	"gp20",
	"gp21",
	"gp22",
	"gp23",

	"gp30",
	"gp31",
	"gp32",
	"gp33",
	"gp34",
	"gp35",
	"gp36",
	"gp37",

	"gp40",
	"gp41",
	"gp42",
	"gp43",
	"gp44",
	"gp45",
	"gp46",
	"gp47",

	"gp50",
	"gp51",
	"gp52",
	"gp53",
	"gp54",
	"gp55",
	"gp56",
	"gp57",

	"gp60",
	"gp61",
	"gp62",
	"gp63",
	"gp64",
	"gp65",
	"gp66",
	"gp67",

	"gp70",
	"gp71",
	"gp72",
	"gp73",
	"gp74",
	"gp75",
	"gp76",
	"gp77",
};

static inline u8 cypress_get_port(struct cy8c95x0_pinctrl *chip, unsigned int pin)
{
	/* Account for GPORT2 which only has 4 bits */
	return CY8C95X0_PIN_TO_OFFSET(pin) / BANK_SZ;
}

static int cypress_get_pin_mask(struct cy8c95x0_pinctrl *chip, unsigned int pin)
{
	/* Account for GPORT2 which only has 4 bits */
	return BIT(CY8C95X0_PIN_TO_OFFSET(pin) % BANK_SZ);
}

static bool cy8c95x0_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x24 ... 0x27:
		return false;
	default:
		return true;
	}
}

static bool cy8c95x0_writeable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CY8C95X0_INPUT_(0) ... CY8C95X0_INPUT_(7):
		return false;
	case CY8C95X0_DEVID:
		return false;
	case 0x24 ... 0x27:
		return false;
	default:
		return true;
	}
}

static bool cy8c95x0_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CY8C95X0_INPUT_(0) ... CY8C95X0_INPUT_(7):
	case CY8C95X0_INTSTATUS_(0) ... CY8C95X0_INTSTATUS_(7):
	case CY8C95X0_INTMASK:
	case CY8C95X0_INVERT:
	case CY8C95X0_PWMSEL:
	case CY8C95X0_DIRECTION:
	case CY8C95X0_DRV_PU:
	case CY8C95X0_DRV_PD:
	case CY8C95X0_DRV_ODH:
	case CY8C95X0_DRV_ODL:
	case CY8C95X0_DRV_PP_FAST:
	case CY8C95X0_DRV_PP_SLOW:
	case CY8C95X0_DRV_HIZ:
		return true;
	default:
		return false;
	}
}

static bool cy8c95x0_precious_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CY8C95X0_INTSTATUS_(0) ... CY8C95X0_INTSTATUS_(7):
		return true;
	default:
		return false;
	}
}

static const struct reg_default cy8c95x0_reg_defaults[] = {
	{ CY8C95X0_OUTPUT_(0), GENMASK(7, 0) },
	{ CY8C95X0_OUTPUT_(1), GENMASK(7, 0) },
	{ CY8C95X0_OUTPUT_(2), GENMASK(7, 0) },
	{ CY8C95X0_OUTPUT_(3), GENMASK(7, 0) },
	{ CY8C95X0_OUTPUT_(4), GENMASK(7, 0) },
	{ CY8C95X0_OUTPUT_(5), GENMASK(7, 0) },
	{ CY8C95X0_OUTPUT_(6), GENMASK(7, 0) },
	{ CY8C95X0_OUTPUT_(7), GENMASK(7, 0) },
	{ CY8C95X0_PORTSEL, 0 },
	{ CY8C95X0_PWMSEL, 0 },
};

static const struct regmap_config cy8c95x0_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.reg_defaults = cy8c95x0_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cy8c95x0_reg_defaults),

	.readable_reg = cy8c95x0_readable_register,
	.writeable_reg = cy8c95x0_writeable_register,
	.volatile_reg = cy8c95x0_volatile_register,
	.precious_reg = cy8c95x0_precious_register,

	.cache_type = REGCACHE_FLAT,
	.max_register = CY8C95X0_COMMAND,
};

static int cy8c95x0_write_regs_mask(struct cy8c95x0_pinctrl *chip, int reg,
				    unsigned long *val, unsigned long *mask)
{
	DECLARE_BITMAP(tmask, MAX_LINE);
	DECLARE_BITMAP(tval, MAX_LINE);
	int write_val;
	int ret = 0;
	int i, off = 0;
	u8 bits;

	/* Add the 4 bit gap of Gport2 */
	bitmap_andnot(tmask, mask, chip->shiftmask, MAX_LINE);
	bitmap_shift_left(tmask, tmask, 4, MAX_LINE);
	bitmap_replace(tmask, tmask, mask, chip->shiftmask, BANK_SZ * 3);

	bitmap_andnot(tval, val, chip->shiftmask, MAX_LINE);
	bitmap_shift_left(tval, tval, 4, MAX_LINE);
	bitmap_replace(tval, tval, val, chip->shiftmask, BANK_SZ * 3);

	mutex_lock(&chip->i2c_lock);
	for (i = 0; i < chip->nport; i++) {
		/* Skip over unused banks */
		bits = bitmap_get_value8(tmask, i * BANK_SZ);
		if (!bits)
			continue;

		switch (reg) {
		/* Muxed registers */
		case CY8C95X0_INTMASK:
		case CY8C95X0_PWMSEL:
		case CY8C95X0_INVERT:
		case CY8C95X0_DIRECTION:
		case CY8C95X0_DRV_PU:
		case CY8C95X0_DRV_PD:
		case CY8C95X0_DRV_ODH:
		case CY8C95X0_DRV_ODL:
		case CY8C95X0_DRV_PP_FAST:
		case CY8C95X0_DRV_PP_SLOW:
		case CY8C95X0_DRV_HIZ:
			ret = regmap_write(chip->regmap, CY8C95X0_PORTSEL, i);
			if (ret < 0)
				goto out;
			off = reg;
			break;
		/* Direct access registers */
		case CY8C95X0_INPUT:
		case CY8C95X0_OUTPUT:
		case CY8C95X0_INTSTATUS:
			off = reg + i;
			break;
		default:
			ret = -EINVAL;
			goto out;
		}

		write_val = bitmap_get_value8(tval, i * BANK_SZ);

		ret = regmap_update_bits(chip->regmap, off, bits, write_val);
		if (ret < 0)
			goto out;
	}
out:
	mutex_unlock(&chip->i2c_lock);

	if (ret < 0)
		dev_err(chip->dev, "failed writing register %d: err %d\n", off, ret);

	return ret;
}

static int cy8c95x0_read_regs_mask(struct cy8c95x0_pinctrl *chip, int reg,
				   unsigned long *val, unsigned long *mask)
{
	DECLARE_BITMAP(tmask, MAX_LINE);
	DECLARE_BITMAP(tval, MAX_LINE);
	DECLARE_BITMAP(tmp, MAX_LINE);
	int read_val;
	int ret = 0;
	int i, off = 0;
	u8 bits;

	/* Add the 4 bit gap of Gport2 */
	bitmap_andnot(tmask, mask, chip->shiftmask, MAX_LINE);
	bitmap_shift_left(tmask, tmask, 4, MAX_LINE);
	bitmap_replace(tmask, tmask, mask, chip->shiftmask, BANK_SZ * 3);

	bitmap_andnot(tval, val, chip->shiftmask, MAX_LINE);
	bitmap_shift_left(tval, tval, 4, MAX_LINE);
	bitmap_replace(tval, tval, val, chip->shiftmask, BANK_SZ * 3);

	mutex_lock(&chip->i2c_lock);
	for (i = 0; i < chip->nport; i++) {
		/* Skip over unused banks */
		bits = bitmap_get_value8(tmask, i * BANK_SZ);
		if (!bits)
			continue;

		switch (reg) {
		/* Muxed registers */
		case CY8C95X0_INTMASK:
		case CY8C95X0_PWMSEL:
		case CY8C95X0_INVERT:
		case CY8C95X0_DIRECTION:
		case CY8C95X0_DRV_PU:
		case CY8C95X0_DRV_PD:
		case CY8C95X0_DRV_ODH:
		case CY8C95X0_DRV_ODL:
		case CY8C95X0_DRV_PP_FAST:
		case CY8C95X0_DRV_PP_SLOW:
		case CY8C95X0_DRV_HIZ:
			ret = regmap_write(chip->regmap, CY8C95X0_PORTSEL, i);
			if (ret < 0)
				goto out;
			off = reg;
			break;
		/* Direct access registers */
		case CY8C95X0_INPUT:
		case CY8C95X0_OUTPUT:
		case CY8C95X0_INTSTATUS:
			off = reg + i;
			break;
		default:
			ret = -EINVAL;
			goto out;
		}

		ret = regmap_read(chip->regmap, off, &read_val);
		if (ret < 0)
			goto out;

		read_val &= bits;
		read_val |= bitmap_get_value8(tval, i * BANK_SZ) & ~bits;
		bitmap_set_value8(tval, read_val, i * BANK_SZ);
	}

	/* Fill the 4 bit gap of Gport2 */
	bitmap_shift_right(tmp, tval, 4, MAX_LINE);
	bitmap_replace(val, tmp, tval, chip->shiftmask, MAX_LINE);

out:
	mutex_unlock(&chip->i2c_lock);

	if (ret < 0)
		dev_err(chip->dev, "failed reading register %d: err %d\n", off, ret);

	return ret;
}

static int cy8c95x0_gpio_direction_input(struct gpio_chip *gc, unsigned int off)
{
	return pinctrl_gpio_direction_input(gc->base + off);
}

static int cy8c95x0_gpio_direction_output(struct gpio_chip *gc,
					  unsigned int off, int val)
{
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);
	u8 port = cypress_get_port(chip, off);
	u8 outreg = CY8C95X0_OUTPUT_(port);
	u8 bit = cypress_get_pin_mask(chip, off);
	int ret;

	/* Set output level */
	ret = regmap_write_bits(chip->regmap, outreg, bit, val ? bit : 0);
	if (ret)
		return ret;

	return pinctrl_gpio_direction_output(gc->base + off);
}

static int cy8c95x0_gpio_get_value(struct gpio_chip *gc, unsigned int off)
{
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);
	u8 inreg = CY8C95X0_INPUT_(cypress_get_port(chip, off));
	u8 bit = cypress_get_pin_mask(chip, off);
	u32 reg_val;
	int ret;

	ret = regmap_read(chip->regmap, inreg, &reg_val);
	if (ret < 0) {
		/*
		 * NOTE:
		 * Diagnostic already emitted; that's all we should
		 * do unless gpio_*_value_cansleep() calls become different
		 * from their nonsleeping siblings (and report faults).
		 */
		return 0;
	}

	return !!(reg_val & bit);
}

static void cy8c95x0_gpio_set_value(struct gpio_chip *gc, unsigned int off,
				    int val)
{
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);
	u8 outreg = CY8C95X0_OUTPUT_(cypress_get_port(chip, off));
	u8 bit = cypress_get_pin_mask(chip, off);

	regmap_write_bits(chip->regmap, outreg, bit, val ? bit : 0);
}

static int cy8c95x0_gpio_get_direction(struct gpio_chip *gc, unsigned int off)
{
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);
	u8 port = cypress_get_port(chip, off);
	u8 bit = cypress_get_pin_mask(chip, off);
	u32 reg_val;
	int ret;

	mutex_lock(&chip->i2c_lock);

	ret = regmap_write(chip->regmap, CY8C95X0_PORTSEL, port);
	if (ret < 0)
		goto out;

	ret = regmap_read(chip->regmap, CY8C95X0_DIRECTION, &reg_val);
	if (ret < 0)
		goto out;

	mutex_unlock(&chip->i2c_lock);

	if (reg_val & bit)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
out:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

static int cy8c95x0_gpio_get_pincfg(struct cy8c95x0_pinctrl *chip,
				    unsigned int off,
				    unsigned long *config)
{
	enum pin_config_param param = pinconf_to_config_param(*config);
	u8 port = cypress_get_port(chip, off);
	u8 bit = cypress_get_pin_mask(chip, off);
	unsigned int reg;
	u32 reg_val;
	u16 arg = 0;
	int ret;

	mutex_lock(&chip->i2c_lock);

	/* Select port */
	ret = regmap_write(chip->regmap, CY8C95X0_PORTSEL, port);
	if (ret < 0)
		goto out;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
		reg = CY8C95X0_DRV_PU;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		reg = CY8C95X0_DRV_PD;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		reg = CY8C95X0_DRV_HIZ;
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		reg = CY8C95X0_DRV_ODL;
		break;
	case PIN_CONFIG_DRIVE_OPEN_SOURCE:
		reg = CY8C95X0_DRV_ODH;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		reg = CY8C95X0_DRV_PP_FAST;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		reg = CY8C95X0_DIRECTION;
		break;
	case PIN_CONFIG_MODE_PWM:
		reg = CY8C95X0_PWMSEL;
		break;
	case PIN_CONFIG_OUTPUT:
		reg = CY8C95X0_OUTPUT_(port);
		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
		reg = CY8C95X0_DIRECTION;
		break;

	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
	case PIN_CONFIG_BIAS_BUS_HOLD:
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
	case PIN_CONFIG_DRIVE_STRENGTH:
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
	case PIN_CONFIG_INPUT_DEBOUNCE:
	case PIN_CONFIG_INPUT_SCHMITT:
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
	case PIN_CONFIG_MODE_LOW_POWER:
	case PIN_CONFIG_PERSIST_STATE:
	case PIN_CONFIG_POWER_SOURCE:
	case PIN_CONFIG_SKEW_DELAY:
	case PIN_CONFIG_SLEEP_HARDWARE_STATE:
	case PIN_CONFIG_SLEW_RATE:
	default:
		ret = -ENOTSUPP;
		goto out;
	}
	/*
	 * Writing 1 to one of the drive mode registers will automatically
	 * clear conflicting set bits in the other drive mode registers.
	 */
	ret = regmap_read(chip->regmap, reg, &reg_val);
	if (reg_val & bit)
		arg = 1;

	*config = pinconf_to_config_packed(param, (u16)arg);
out:
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static int cy8c95x0_gpio_set_pincfg(struct cy8c95x0_pinctrl *chip,
				    unsigned int off,
				    unsigned long config)
{
	u8 port = cypress_get_port(chip, off);
	u8 bit = cypress_get_pin_mask(chip, off);
	unsigned long param = pinconf_to_config_param(config);
	unsigned int reg;
	int ret;

	mutex_lock(&chip->i2c_lock);

	/* Select port */
	ret = regmap_write(chip->regmap, CY8C95X0_PORTSEL, port);
	if (ret < 0)
		goto out;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
		__clear_bit(off, chip->push_pull);
		reg = CY8C95X0_DRV_PU;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		__clear_bit(off, chip->push_pull);
		reg = CY8C95X0_DRV_PD;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		__clear_bit(off, chip->push_pull);
		reg = CY8C95X0_DRV_HIZ;
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		__clear_bit(off, chip->push_pull);
		reg = CY8C95X0_DRV_ODL;
		break;
	case PIN_CONFIG_DRIVE_OPEN_SOURCE:
		__clear_bit(off, chip->push_pull);
		reg = CY8C95X0_DRV_ODH;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		__set_bit(off, chip->push_pull);
		reg = CY8C95X0_DRV_PP_FAST;
		break;
	case PIN_CONFIG_MODE_PWM:
		reg = CY8C95X0_PWMSEL;
		break;
	default:
		ret = -ENOTSUPP;
		goto out;
	}
	/*
	 * Writing 1 to one of the drive mode registers will automatically
	 * clear conflicting set bits in the other drive mode registers.
	 */
	ret = regmap_write_bits(chip->regmap, reg, bit, bit);

out:
	mutex_unlock(&chip->i2c_lock);
	return ret;
}

static int cy8c95x0_gpio_get_multiple(struct gpio_chip *gc,
				      unsigned long *mask, unsigned long *bits)
{
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);

	return cy8c95x0_read_regs_mask(chip, CY8C95X0_INPUT, bits, mask);
}

static void cy8c95x0_gpio_set_multiple(struct gpio_chip *gc,
				       unsigned long *mask, unsigned long *bits)
{
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);

	cy8c95x0_write_regs_mask(chip, CY8C95X0_OUTPUT, bits, mask);
}

static int cy8c95x0_add_pin_ranges(struct gpio_chip *gc)
{
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);
	struct device *dev = chip->dev;
	int ret;

	ret = gpiochip_add_pin_range(gc, dev_name(dev), 0, 0, chip->tpin);
	if (ret)
		dev_err(dev, "failed to add GPIO pin range\n");

	return ret;
}

static int cy8c95x0_setup_gpiochip(struct cy8c95x0_pinctrl *chip)
{
	struct gpio_chip *gc = &chip->gpio_chip;

	gc->request = gpiochip_generic_request;
	gc->free = gpiochip_generic_free;
	gc->direction_input  = cy8c95x0_gpio_direction_input;
	gc->direction_output = cy8c95x0_gpio_direction_output;
	gc->get = cy8c95x0_gpio_get_value;
	gc->set = cy8c95x0_gpio_set_value;
	gc->get_direction = cy8c95x0_gpio_get_direction;
	gc->get_multiple = cy8c95x0_gpio_get_multiple;
	gc->set_multiple = cy8c95x0_gpio_set_multiple;
	gc->set_config = gpiochip_generic_config,
	gc->can_sleep = true;
	gc->add_pin_ranges = cy8c95x0_add_pin_ranges;

	gc->base = -1;
	gc->ngpio = chip->tpin;

	gc->parent = chip->dev;
	gc->owner = THIS_MODULE;
	gc->names = NULL;

	gc->label = dev_name(chip->dev);

	return devm_gpiochip_add_data(chip->dev, gc, chip);
}

static void cy8c95x0_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	set_bit(hwirq, chip->irq_mask);
	gpiochip_disable_irq(gc, hwirq);
}

static void cy8c95x0_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gc, hwirq);
	clear_bit(hwirq, chip->irq_mask);
}

static void cy8c95x0_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);

	mutex_lock(&chip->irq_lock);
}

static void cy8c95x0_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);
	DECLARE_BITMAP(ones, MAX_LINE);
	DECLARE_BITMAP(irq_mask, MAX_LINE);
	DECLARE_BITMAP(reg_direction, MAX_LINE);

	bitmap_fill(ones, MAX_LINE);

	cy8c95x0_write_regs_mask(chip, CY8C95X0_INTMASK, chip->irq_mask, ones);

	/* Switch direction to input if needed */
	cy8c95x0_read_regs_mask(chip, CY8C95X0_DIRECTION, reg_direction, chip->irq_mask);
	bitmap_or(irq_mask, chip->irq_mask, reg_direction, MAX_LINE);
	bitmap_complement(irq_mask, irq_mask, MAX_LINE);

	/* Look for any newly setup interrupt */
	cy8c95x0_write_regs_mask(chip, CY8C95X0_DIRECTION, ones, irq_mask);

	mutex_unlock(&chip->irq_lock);
}

static int cy8c95x0_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	unsigned int trig_type;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		trig_type = type;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		trig_type = IRQ_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		trig_type = IRQ_TYPE_EDGE_FALLING;
		break;
	default:
		dev_err(chip->dev, "irq %d: unsupported type %d\n", d->irq, type);
		return -EINVAL;
	}

	assign_bit(hwirq, chip->irq_trig_fall, trig_type & IRQ_TYPE_EDGE_FALLING);
	assign_bit(hwirq, chip->irq_trig_raise, trig_type & IRQ_TYPE_EDGE_RISING);
	assign_bit(hwirq, chip->irq_trig_low, type == IRQ_TYPE_LEVEL_LOW);
	assign_bit(hwirq, chip->irq_trig_high, type == IRQ_TYPE_LEVEL_HIGH);

	return 0;
}

static void cy8c95x0_irq_shutdown(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct cy8c95x0_pinctrl *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	clear_bit(hwirq, chip->irq_trig_raise);
	clear_bit(hwirq, chip->irq_trig_fall);
	clear_bit(hwirq, chip->irq_trig_low);
	clear_bit(hwirq, chip->irq_trig_high);
}

static const struct irq_chip cy8c95x0_irqchip = {
	.name = "cy8c95x0-irq",
	.irq_mask = cy8c95x0_irq_mask,
	.irq_unmask = cy8c95x0_irq_unmask,
	.irq_bus_lock = cy8c95x0_irq_bus_lock,
	.irq_bus_sync_unlock = cy8c95x0_irq_bus_sync_unlock,
	.irq_set_type = cy8c95x0_irq_set_type,
	.irq_shutdown = cy8c95x0_irq_shutdown,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static bool cy8c95x0_irq_pending(struct cy8c95x0_pinctrl *chip, unsigned long *pending)
{
	DECLARE_BITMAP(ones, MAX_LINE);
	DECLARE_BITMAP(cur_stat, MAX_LINE);
	DECLARE_BITMAP(new_stat, MAX_LINE);
	DECLARE_BITMAP(trigger, MAX_LINE);

	bitmap_fill(ones, MAX_LINE);

	/* Read the current interrupt status from the device */
	if (cy8c95x0_read_regs_mask(chip, CY8C95X0_INTSTATUS, trigger, ones))
		return false;

	/* Check latched inputs */
	if (cy8c95x0_read_regs_mask(chip, CY8C95X0_INPUT, cur_stat, trigger))
		return false;

	/* Apply filter for rising/falling edge selection */
	bitmap_replace(new_stat, chip->irq_trig_fall, chip->irq_trig_raise,
		       cur_stat, MAX_LINE);

	bitmap_and(pending, new_stat, trigger, MAX_LINE);

	return !bitmap_empty(pending, MAX_LINE);
}

static irqreturn_t cy8c95x0_irq_handler(int irq, void *devid)
{
	struct cy8c95x0_pinctrl *chip = devid;
	struct gpio_chip *gc = &chip->gpio_chip;
	DECLARE_BITMAP(pending, MAX_LINE);
	int nested_irq, level;
	bool ret;

	ret = cy8c95x0_irq_pending(chip, pending);
	if (!ret)
		return IRQ_RETVAL(0);

	ret = 0;
	for_each_set_bit(level, pending, MAX_LINE) {
		/* Already accounted for 4bit gap in GPort2 */
		nested_irq = irq_find_mapping(gc->irq.domain, level);

		if (unlikely(nested_irq <= 0)) {
			dev_warn_ratelimited(gc->parent, "unmapped interrupt %d\n", level);
			continue;
		}

		if (test_bit(level, chip->irq_trig_low))
			while (!cy8c95x0_gpio_get_value(gc, level))
				handle_nested_irq(nested_irq);
		else if (test_bit(level, chip->irq_trig_high))
			while (cy8c95x0_gpio_get_value(gc, level))
				handle_nested_irq(nested_irq);
		else
			handle_nested_irq(nested_irq);

		ret = 1;
	}

	return IRQ_RETVAL(ret);
}

static int cy8c95x0_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct cy8c95x0_pinctrl *chip = pinctrl_dev_get_drvdata(pctldev);

	return chip->tpin;
}

static const char *cy8c95x0_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						   unsigned int group)
{
	return cy8c95x0_groups[group];
}

static int cy8c95x0_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					   unsigned int group,
					   const unsigned int **pins,
					   unsigned int *num_pins)
{
	*pins = &cy8c9560_pins[group].number;
	*num_pins = 1;
	return 0;
}

static const char *cy8c95x0_get_fname(unsigned int selector)
{
	if (selector == 0)
		return "gpio";
	else
		return "pwm";
}

static void cy8c95x0_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
				  unsigned int pin)
{
	struct cy8c95x0_pinctrl *chip = pinctrl_dev_get_drvdata(pctldev);
	DECLARE_BITMAP(mask, MAX_LINE);
	DECLARE_BITMAP(pwm, MAX_LINE);

	bitmap_zero(mask, MAX_LINE);
	__set_bit(pin, mask);

	if (cy8c95x0_read_regs_mask(chip, CY8C95X0_PWMSEL, pwm, mask)) {
		seq_puts(s, "not available");
		return;
	}

	seq_printf(s, "MODE:%s", cy8c95x0_get_fname(test_bit(pin, pwm)));
}

static const struct pinctrl_ops cy8c95x0_pinctrl_ops = {
	.get_groups_count = cy8c95x0_pinctrl_get_groups_count,
	.get_group_name = cy8c95x0_pinctrl_get_group_name,
	.get_group_pins = cy8c95x0_pinctrl_get_group_pins,
#ifdef CONFIG_OF
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinconf_generic_dt_free_map,
#endif
	.pin_dbg_show = cy8c95x0_pin_dbg_show,
};

static const char *cy8c95x0_get_function_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	return cy8c95x0_get_fname(selector);
}

static int cy8c95x0_get_functions_count(struct pinctrl_dev *pctldev)
{
	return 2;
}

static int cy8c95x0_get_function_groups(struct pinctrl_dev *pctldev, unsigned int selector,
					const char * const **groups,
					unsigned int * const num_groups)
{
	struct cy8c95x0_pinctrl *chip = pinctrl_dev_get_drvdata(pctldev);

	*groups = cy8c95x0_groups;
	*num_groups = chip->tpin;
	return 0;
}

static int cy8c95x0_set_mode(struct cy8c95x0_pinctrl *chip, unsigned int off, bool mode)
{
	u8 port = cypress_get_port(chip, off);
	u8 bit = cypress_get_pin_mask(chip, off);
	int ret;

	/* Select port */
	ret = regmap_write(chip->regmap, CY8C95X0_PORTSEL, port);
	if (ret < 0)
		return ret;

	return regmap_write_bits(chip->regmap, CY8C95X0_PWMSEL, bit, mode ? bit : 0);
}

static int cy8c95x0_pinmux_mode(struct cy8c95x0_pinctrl *chip,
				unsigned int selector, unsigned int group)
{
	u8 port = cypress_get_port(chip, group);
	u8 bit = cypress_get_pin_mask(chip, group);
	int ret;

	ret = cy8c95x0_set_mode(chip, group, selector);
	if (ret < 0)
		return ret;

	if (selector == 0)
		return 0;

	/* Set direction to output & set output to 1 so that PWM can work */
	ret = regmap_write_bits(chip->regmap, CY8C95X0_DIRECTION, bit, bit);
	if (ret < 0)
		return ret;

	return regmap_write_bits(chip->regmap, CY8C95X0_OUTPUT_(port), bit, bit);
}

static int cy8c95x0_set_mux(struct pinctrl_dev *pctldev, unsigned int selector,
			    unsigned int group)
{
	struct cy8c95x0_pinctrl *chip = pinctrl_dev_get_drvdata(pctldev);
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = cy8c95x0_pinmux_mode(chip, selector, group);
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static int cy8c95x0_gpio_request_enable(struct pinctrl_dev *pctldev,
					struct pinctrl_gpio_range *range,
					unsigned int pin)
{
	struct cy8c95x0_pinctrl *chip = pinctrl_dev_get_drvdata(pctldev);
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = cy8c95x0_set_mode(chip, pin, false);
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static int cy8c95x0_pinmux_direction(struct cy8c95x0_pinctrl *chip,
				     unsigned int pin, bool input)
{
	u8 port = cypress_get_port(chip, pin);
	u8 bit = cypress_get_pin_mask(chip, pin);
	int ret;

	/* Select port... */
	ret = regmap_write(chip->regmap, CY8C95X0_PORTSEL, port);
	if (ret)
		return ret;

	/* ...then direction */
	ret = regmap_write_bits(chip->regmap, CY8C95X0_DIRECTION, bit, input ? bit : 0);
	if (ret)
		return ret;

	/*
	 * Disable driving the pin by forcing it to HighZ. Only setting
	 * the direction register isn't sufficient in Push-Pull mode.
	 */
	if (input && test_bit(pin, chip->push_pull)) {
		ret = regmap_write_bits(chip->regmap, CY8C95X0_DRV_HIZ, bit, bit);
		if (ret)
			return ret;

		__clear_bit(pin, chip->push_pull);
	}

	return 0;
}

static int cy8c95x0_gpio_set_direction(struct pinctrl_dev *pctldev,
				       struct pinctrl_gpio_range *range,
				       unsigned int pin, bool input)
{
	struct cy8c95x0_pinctrl *chip = pinctrl_dev_get_drvdata(pctldev);
	int ret;

	mutex_lock(&chip->i2c_lock);
	ret = cy8c95x0_pinmux_direction(chip, pin, input);
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static const struct pinmux_ops cy8c95x0_pmxops = {
	.get_functions_count = cy8c95x0_get_functions_count,
	.get_function_name = cy8c95x0_get_function_name,
	.get_function_groups = cy8c95x0_get_function_groups,
	.set_mux = cy8c95x0_set_mux,
	.gpio_request_enable = cy8c95x0_gpio_request_enable,
	.gpio_set_direction = cy8c95x0_gpio_set_direction,
	.strict = true,
};

static int cy8c95x0_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *config)
{
	struct cy8c95x0_pinctrl *chip = pinctrl_dev_get_drvdata(pctldev);

	return cy8c95x0_gpio_get_pincfg(chip, pin, config);
}

static int cy8c95x0_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *configs, unsigned int num_configs)
{
	struct cy8c95x0_pinctrl *chip = pinctrl_dev_get_drvdata(pctldev);
	int ret = 0;
	int i;

	for (i = 0; i < num_configs; i++) {
		ret = cy8c95x0_gpio_set_pincfg(chip, pin, configs[i]);
		if (ret)
			return ret;
	}

	return ret;
}

static const struct pinconf_ops cy8c95x0_pinconf_ops = {
	.pin_config_get = cy8c95x0_pinconf_get,
	.pin_config_set = cy8c95x0_pinconf_set,
	.is_generic = true,
};

static int cy8c95x0_irq_setup(struct cy8c95x0_pinctrl *chip, int irq)
{
	struct gpio_irq_chip *girq = &chip->gpio_chip.irq;
	DECLARE_BITMAP(pending_irqs, MAX_LINE);
	int ret;

	mutex_init(&chip->irq_lock);

	bitmap_zero(pending_irqs, MAX_LINE);

	/* Read IRQ status register to clear all pending interrupts */
	ret = cy8c95x0_irq_pending(chip, pending_irqs);
	if (ret) {
		dev_err(chip->dev, "failed to clear irq status register\n");
		return ret;
	}

	/* Mask all interrupts */
	bitmap_fill(chip->irq_mask, MAX_LINE);

	gpio_irq_chip_set_chip(girq, &cy8c95x0_irqchip);

	/* This will let us handle the parent IRQ in the driver */
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_simple_irq;
	girq->threaded = true;

	ret = devm_request_threaded_irq(chip->dev, irq,
					NULL, cy8c95x0_irq_handler,
					IRQF_ONESHOT | IRQF_SHARED | IRQF_TRIGGER_HIGH,
					dev_name(chip->dev), chip);
	if (ret) {
		dev_err(chip->dev, "failed to request irq %d\n", irq);
		return ret;
	}
	dev_info(chip->dev, "Registered threaded IRQ\n");

	return 0;
}

static int cy8c95x0_setup_pinctrl(struct cy8c95x0_pinctrl *chip)
{
	struct pinctrl_desc *pd = &chip->pinctrl_desc;

	pd->pctlops = &cy8c95x0_pinctrl_ops;
	pd->confops = &cy8c95x0_pinconf_ops;
	pd->pmxops = &cy8c95x0_pmxops;
	pd->name = dev_name(chip->dev);
	pd->pins = cy8c9560_pins;
	pd->npins = chip->tpin;
	pd->owner = THIS_MODULE;

	chip->pctldev = devm_pinctrl_register(chip->dev, pd, chip);
	if (IS_ERR(chip->pctldev))
		return dev_err_probe(chip->dev, PTR_ERR(chip->pctldev),
			"can't register controller\n");

	return 0;
}

static int cy8c95x0_detect(struct i2c_client *client,
			   struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int ret;
	const char *name;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_byte_data(client, CY8C95X0_DEVID);
	if (ret < 0)
		return ret;
	switch (ret & GENMASK(7, 4)) {
	case 0x20:
		name = cy8c95x0_id[0].name;
		break;
	case 0x40:
		name = cy8c95x0_id[1].name;
		break;
	case 0x60:
		name = cy8c95x0_id[2].name;
		break;
	default:
		return -ENODEV;
	}

	dev_info(&client->dev, "Found a %s chip at 0x%02x.\n", name, client->addr);
	strscpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static int cy8c95x0_probe(struct i2c_client *client)
{
	struct cy8c95x0_pinctrl *chip;
	struct regulator *reg;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &client->dev;

	/* Set the device type */
	chip->driver_data = (unsigned long)device_get_match_data(&client->dev);
	if (!chip->driver_data)
		chip->driver_data = i2c_match_id(cy8c95x0_id, client)->driver_data;
	if (!chip->driver_data)
		return -ENODEV;

	i2c_set_clientdata(client, chip);

	chip->tpin = chip->driver_data & CY8C95X0_GPIO_MASK;
	chip->nport = DIV_ROUND_UP(CY8C95X0_PIN_TO_OFFSET(chip->tpin), BANK_SZ);

	switch (chip->tpin) {
	case 20:
		strscpy(chip->name, cy8c95x0_id[0].name, I2C_NAME_SIZE);
		break;
	case 40:
		strscpy(chip->name, cy8c95x0_id[1].name, I2C_NAME_SIZE);
		break;
	case 60:
		strscpy(chip->name, cy8c95x0_id[2].name, I2C_NAME_SIZE);
		break;
	default:
		return -ENODEV;
	}

	reg = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(reg)) {
		if (PTR_ERR(reg) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	} else {
		ret = regulator_enable(reg);
		if (ret) {
			dev_err(&client->dev, "failed to enable regulator vdd: %d\n", ret);
			return ret;
		}
		chip->regulator = reg;
	}

	chip->regmap = devm_regmap_init_i2c(client, &cy8c95x0_i2c_regmap);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		goto err_exit;
	}

	bitmap_zero(chip->push_pull, MAX_LINE);
	bitmap_zero(chip->shiftmask, MAX_LINE);
	bitmap_set(chip->shiftmask, 0, 20);
	mutex_init(&chip->i2c_lock);

	if (dmi_first_match(cy8c95x0_dmi_acpi_irq_info)) {
		ret = cy8c95x0_acpi_get_irq(&client->dev);
		if (ret > 0)
			client->irq = ret;
	}

	if (client->irq) {
		ret = cy8c95x0_irq_setup(chip, client->irq);
		if (ret)
			goto err_exit;
	}

	ret = cy8c95x0_setup_pinctrl(chip);
	if (ret)
		goto err_exit;

	ret = cy8c95x0_setup_gpiochip(chip);
	if (ret)
		goto err_exit;

	return 0;

err_exit:
	if (!IS_ERR_OR_NULL(chip->regulator))
		regulator_disable(chip->regulator);
	return ret;
}

static void cy8c95x0_remove(struct i2c_client *client)
{
	struct cy8c95x0_pinctrl *chip = i2c_get_clientdata(client);

	if (!IS_ERR_OR_NULL(chip->regulator))
		regulator_disable(chip->regulator);
}

static const struct acpi_device_id cy8c95x0_acpi_ids[] = {
	{ "INT3490", 40, },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cy8c95x0_acpi_ids);

static struct i2c_driver cy8c95x0_driver = {
	.driver = {
		.name	= "cy8c95x0-pinctrl",
		.of_match_table = cy8c95x0_dt_ids,
		.acpi_match_table = cy8c95x0_acpi_ids,
	},
	.probe_new	= cy8c95x0_probe,
	.remove		= cy8c95x0_remove,
	.id_table	= cy8c95x0_id,
	.detect		= cy8c95x0_detect,
};
module_i2c_driver(cy8c95x0_driver);

MODULE_AUTHOR("Patrick Rudolph <patrick.rudolph@9elements.com>");
MODULE_AUTHOR("Naresh Solanki <naresh.solanki@9elements.com>");
MODULE_DESCRIPTION("Pinctrl driver for CY8C95X0");
MODULE_LICENSE("GPL");
