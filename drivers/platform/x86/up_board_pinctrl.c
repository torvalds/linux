/*
 * UP Board header pin GPIO driver.
 *
 * Copyright (c) 2016, Emutex Ltd.  All rights reserved.
 *
 * Author: Dan O'Donovan <dan@emutex.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

/*
 * The UP Board features an external 40-pin header for I/O functions including
 * GPIO, I2C, UART, SPI, PWM and I2S, similar in layout to the Raspberry Pi 2.
 * At the heart of the UP Board is an Intel X5-Z8350 "Cherry Trail" SoC, which
 * provides the I/O functions for these pins at 1.8V logic levels.
 *
 * Additional buffers and mux switches are used between the SoC and the I/O pin
 * header to convert between the 1.8V SoC I/O and the 3.3V levels required at
 * the pin header, with sufficient current source/sink capability for
 * LV-TTL/LV-CMOS compatibility.  These buffers and mux switches require
 * run-time configuration based on the pin function or GPIO direction selected
 * by the user.
 *
 * The purpose of this driver is to manage the complexity of the buffer
 * configuration so that application code can transparently access the I/O
 * functions on the external pins through standard kernel interfaces.  It
 * instantiates a gpio and pinctrl device, and effectively acts as a "shim"
 * between application code and the underlying Cherry Trail GPIO driver.
 */

/* References to Cherry Trail GPIO chip driver */
struct up_soc_gpiochip_info {
	char *name;
	struct gpio_chip *chip;
};

/* References to Cherry Trail GPIO pins */
struct up_soc_gpio_info {
	struct up_soc_gpiochip_info *ci;
	struct gpio_desc *desc;
	unsigned offset;
	int gpio;
	int irq;
	int flags;
};

/* Information for a single I/O pin on the UP board */
struct up_pin_info {
	struct up_soc_gpio_info soc_gpio;
	int irq;
	int dir_ctrl_pin;
	int dir_in;
	int dir_out;
	int func_dir;
	int mux_ctrl_pin;
	int mux_gpio;
	int mux_func;
	bool func_enabled;
};

struct up_cpld_led_info {
	unsigned offset;
	const char *name;
	struct up_cpld_info *cpld;
	struct led_classdev cdev;
};

/* Information for the CPLD featured on later UP board revisions */
struct up_cpld_info {
	struct up_soc_gpio_info strobe_gpio;
	struct up_soc_gpio_info reset_gpio;
	struct up_soc_gpio_info data_in_gpio;
	struct up_soc_gpio_info data_out_gpio;
	struct up_soc_gpio_info oe_gpio;
	u64 dir_reg;
	bool do_verify;
	bool do_strobe_after_write;
	unsigned dir_reg_size;
	struct up_cpld_led_info *leds;
	unsigned num_leds;
	spinlock_t lock;
};

struct up_board_info {
	struct up_pin_info *pins;
	struct up_cpld_info *cpld;
};

/* Context variables for this driver */
struct up_pctrl {
	struct up_board_info *board;
	struct gpio_chip chip;
	struct pinctrl_desc pctldesc;
	struct pinctrl_dev *pctldev;
};

/* Pin group information */
struct up_pingroup {
	const char *name;
	const unsigned *pins;
	size_t npins;
};

/* Pin function information */
struct up_function {
	const char *name;
	const char * const *groups;
	size_t ngroups;
};

/* The Cherry Trail SoC has 4 independent GPIO controllers */
static struct up_soc_gpiochip_info chip_cht_SW = { .name = "INT33FF:00" };
static struct up_soc_gpiochip_info chip_cht_N  = { .name = "INT33FF:01" };
static struct up_soc_gpiochip_info chip_cht_E  = { .name = "INT33FF:02" };
static struct up_soc_gpiochip_info chip_cht_SE = { .name = "INT33FF:03" };

#define SOC_GPIO(c, o, f)		\
	{				\
		.ci	= (c),		\
		.offset	= (o),		\
		.flags	= (f),		\
	}
#define SOC_GPIO_INPUT(c, o) SOC_GPIO(c, o, GPIOF_IN)
#define SOC_GPIO_OUTPUT(c, o) SOC_GPIO(c, o, GPIOF_OUT_INIT_LOW)
#define GPIO_PIN(c, o, dpin, din, dout, dfunc, mpin, mgpio, mfunc) \
	{						\
		.soc_gpio.ci		= (c),		\
		.soc_gpio.offset	= (o),		\
		.dir_ctrl_pin		= (dpin),	\
		.dir_in			= (din),	\
		.dir_out		= (dout),	\
		.func_dir		= (dfunc),	\
		.mux_ctrl_pin		= (mpin),	\
		.mux_gpio		= (mgpio),	\
		.mux_func		= (mfunc),	\
		.func_enabled		= false,	\
	}

#define FDIR_NONE -1
#define FDIR_OUT  1
#define FDIR_IN   0

#define NONE -1

#define PIN_GROUP(n, p)				\
	{					\
		.name = (n),			\
		.pins = (p),			\
		.npins = ARRAY_SIZE((p)),	\
	}

#define FUNCTION(n, g)				\
	{					\
		.name = (n),			\
		.groups = (g),			\
		.ngroups = ARRAY_SIZE((g)),	\
	}

#define GPIO_PINRANGE(start, end)		\
	{					\
		.base = (start),		\
		.npins = (end) - (start) + 1,	\
	}

#define N_GPIO 28

/* Initial configuration assumes all pins as GPIO inputs */
#define CPLD_DIR_REG_INIT	(0x00FFFFFFFULL)

/* Convenience macros to populate the pin info tables below */
#define GPIO_PIN_UP(c, o, dpin, dfunc, mpin, mgpio, mfunc)	\
	GPIO_PIN(c, o, dpin, 1, 0, dfunc, mpin, mgpio, mfunc)
#define GPIO_PIN_UP_NO_MUX(c, o, dpin, dfunc)			\
	GPIO_PIN_UP(c, o, dpin, dfunc, NONE, -1, -1)

/*
 * Table of I/O pins on the 40-pin header of the UP Board (version-specific)
 */
/* UP Board uses a CPLD to provide I/O signal buffers and mux switching */
static struct up_pin_info up_pins[N_GPIO] = {
	GPIO_PIN_UP(&chip_cht_SW, 33,  9, FDIR_OUT, 28, 0, 1),	/*  0 */
	GPIO_PIN_UP(&chip_cht_SW, 37, 23, FDIR_OUT, 28, 0, 1),	/*  1 */
	GPIO_PIN_UP(&chip_cht_SW, 32,  0, FDIR_OUT, 29, 0, 1),	/*  2 */
	GPIO_PIN_UP(&chip_cht_SW, 35,  1, FDIR_OUT, 29, 0, 1),	/*  3 */
	GPIO_PIN_UP(&chip_cht_E,  18,  2, FDIR_IN,  30, 0, 1),	/*  4 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_E,  21, 10, FDIR_NONE),		/*  5 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_E,  12, 11, FDIR_NONE),		/*  6 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE, 48, 22, FDIR_NONE),		/*  7 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE,  7, 21, FDIR_OUT),		/*  8 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE,  3,  7, FDIR_IN),		/*  9 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE,  6,  6, FDIR_OUT),		/* 10 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE,  4,  8, FDIR_OUT),		/* 11 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE,  5, 24, FDIR_OUT),		/* 12 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE,  1, 12, FDIR_OUT),		/* 13 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SW, 13, 15, FDIR_OUT),		/* 14 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SW,  9, 16, FDIR_IN),		/* 15 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SW, 11, 25, FDIR_IN),		/* 16 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SW,  8,  3, FDIR_OUT),		/* 17 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SW, 50, 17, FDIR_OUT),		/* 18 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SW, 54, 13, FDIR_OUT),		/* 19 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SW, 52, 26, FDIR_IN),		/* 20 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SW, 55, 27, FDIR_OUT),		/* 21 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE, 12,  5, FDIR_OUT),		/* 22 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE, 15, 18, FDIR_OUT),		/* 23 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE, 18, 19, FDIR_OUT),		/* 24 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE, 11, 20, FDIR_OUT),		/* 25 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE, 14, 14, FDIR_OUT),		/* 26 */
	GPIO_PIN_UP_NO_MUX(&chip_cht_SE,  8,  4, FDIR_OUT),		/* 27 */
};

static struct up_cpld_led_info up_cpld_leds[] = {
	{ .offset = 31, .name = "yellow", },
	{ .offset = 32, .name = "green", },
	{ .offset = 33, .name = "red", },
};

static struct up_cpld_info up_cpld = {
	.strobe_gpio		= SOC_GPIO_OUTPUT(&chip_cht_N, 21),
	.reset_gpio		= SOC_GPIO_OUTPUT(&chip_cht_E, 15),
	.data_in_gpio		= SOC_GPIO_OUTPUT(&chip_cht_E, 13),
	.data_out_gpio		= SOC_GPIO_INPUT(&chip_cht_E, 23),
	.oe_gpio		= SOC_GPIO_OUTPUT(&chip_cht_SW, 43),
	.dir_reg		= CPLD_DIR_REG_INIT,
	.do_verify		= true,
	.do_strobe_after_write	= false,
	.dir_reg_size		= 34,
	.leds			= up_cpld_leds,
	.num_leds		= ARRAY_SIZE(up_cpld_leds),
};

static struct up_board_info up_board = {
	.pins = up_pins,
	.cpld = &up_cpld,
};

/* The layout and numbering is designed to emulate the Raspberry Pi 2 */
static const struct pinctrl_pin_desc up_pins_pi[] = {
	PINCTRL_PIN(0,  "I2C0_SDA"),
	PINCTRL_PIN(1,  "I2C0_SCL"),
	PINCTRL_PIN(2,  "I2C1_SDA"),
	PINCTRL_PIN(3,  "I2C1_SCL"),
	PINCTRL_PIN(4,  "GPIO4"),
	PINCTRL_PIN(5,  "GPIO5"),
	PINCTRL_PIN(6,  "GPIO6"),
	PINCTRL_PIN(7,  "SPI_CS1"),
	PINCTRL_PIN(8,  "SPI_CS0"),
	PINCTRL_PIN(9,  "SPI_MISO"),
	PINCTRL_PIN(10, "SPI_MOSI"),
	PINCTRL_PIN(11, "SPI_CLK"),
	PINCTRL_PIN(12, "PWM0"),
	PINCTRL_PIN(13, "PWM1"),
	PINCTRL_PIN(14, "UART1_TX"),
	PINCTRL_PIN(15, "UART1_RX"),
	PINCTRL_PIN(16, "GPIO16"),
	PINCTRL_PIN(17, "GPIO17"),
	PINCTRL_PIN(18, "I2S_CLK"),
	PINCTRL_PIN(19, "I2S_FRM"),
	PINCTRL_PIN(20, "I2S_DIN"),
	PINCTRL_PIN(21, "I2S_DOUT"),
	PINCTRL_PIN(22, "GPIO22"),
	PINCTRL_PIN(23, "GPIO23"),
	PINCTRL_PIN(24, "GPIO24"),
	PINCTRL_PIN(25, "GPIO25"),
	PINCTRL_PIN(26, "GPIO26"),
	PINCTRL_PIN(27, "GPIO27"),
};

static const unsigned uart1_pins[] = { 14, 15, 16, 17 };
static const unsigned uart2_pins[] = { 25, 27 };
static const unsigned i2c0_pins[]  = { 0, 1 };
static const unsigned i2c1_pins[]  = { 2, 3 };
static const unsigned spi2_pins[]  = { 8, 9, 10, 11 };
static const unsigned i2s0_pins[]  = { 18, 19, 20, 21 };
static const unsigned pwm0_pins[]  = { 12 };
static const unsigned pwm1_pins[]  = { 13 };
static const unsigned adc0_pins[]  = { 4 };

static const struct up_pingroup pin_groups[] = {
	PIN_GROUP("uart1_grp", uart1_pins),
	PIN_GROUP("uart2_grp", uart2_pins),
	PIN_GROUP("i2c0_grp", i2c0_pins),
	PIN_GROUP("i2c1_grp", i2c1_pins),
	PIN_GROUP("spi2_grp", spi2_pins),
	PIN_GROUP("i2s0_grp", i2s0_pins),
	PIN_GROUP("pwm0_grp", pwm0_pins),
	PIN_GROUP("pwm1_grp", pwm1_pins),
	PIN_GROUP("adc0_grp", adc0_pins),
};

static const char * const uart1_groups[] = { "uart1_grp" };
static const char * const uart2_groups[] = { "uart2_grp" };
static const char * const i2c0_groups[]  = { "i2c0_grp" };
static const char * const i2c1_groups[]  = { "i2c1_grp" };
static const char * const spi2_groups[]  = { "spi2_grp" };
static const char * const i2s0_groups[]  = { "i2s0_grp" };
static const char * const pwm0_groups[]  = { "pwm0_grp" };
static const char * const pwm1_groups[]  = { "pwm1_grp" };
static const char * const adc0_groups[]  = { "adc0_grp" };

static const struct up_function pin_functions[] = {
	FUNCTION("uart1", uart1_groups),
	FUNCTION("uart2", uart2_groups),
	FUNCTION("i2c0",  i2c0_groups),
	FUNCTION("i2c1",  i2c1_groups),
	FUNCTION("spi2",  spi2_groups),
	FUNCTION("i2s0",  i2s0_groups),
	FUNCTION("pwm0",  pwm0_groups),
	FUNCTION("pwm1",  pwm1_groups),
	FUNCTION("adc0",  adc0_groups),
};

/* On the UP board, the header pin level shifting and mux switching is
 * controlled by a dedicated CPLD with proprietary firmware
 *
 * The CPLD is responsible for connecting and translating 1.8V GPIO signals from
 * the SoC to the 28 GPIO header pins at 3.3V, and for this it needs to be
 * configured with direction (input/output) for each GPIO.  In addition, it
 * manages 3 mux switches (2 for I2C bus pins, 1 for ADC pin) which need to be
 * configured on/off, and 3 LEDs.
 *
 * A register value is loaded into the CPLD at run-time to configure the
 * 28 GPIO level shifters, 3 mux switches and 3 LEDs.  This register value is
 * loaded via a 2-wire data interface consisting of a strobe and data line.  The
 * data line is sampled on each rising edge that appears on the strobe line.  A
 * reset signal (active low) is used to reset internal counters and state prior
 * to loading a new register value.  An output-enable signal is provided,
 * initially disabled which puts all header pins in a HiZ state until a valid
 * pin configuration is loaded by this driver.
 *
 * The register value is clocked into the CPLD bit-by-bit, and then read back.
 * A total of 69 rising edges on the strobe signal are required, following the
 * reset pulse, before the new register value is "latched" by the CPLD.
 */
static int cpld_configure(struct up_cpld_info *cpld)
{
	u64 dir_reg_verify = 0;
	int i;

	/* Reset the CPLD internal counters */
	gpiod_set_value(cpld->reset_gpio.desc, 0);
	gpiod_set_value(cpld->reset_gpio.desc, 1);

	/* Update the CPLD dir register */
	for (i = cpld->dir_reg_size - 1; i >= 0; i--) {
		/* Bring STB low initially */
		gpiod_set_value(cpld->strobe_gpio.desc, 0);
		/* Load the next bit value, MSb first */
		gpiod_set_value(cpld->data_in_gpio.desc,
				(cpld->dir_reg >> i) & 0x1);
		/* Bring STB high to latch the bit value */
		gpiod_set_value(cpld->strobe_gpio.desc, 1);
	}

	if (cpld->do_strobe_after_write) {
		/* Issue a dummy STB cycle after writing the register value */
		gpiod_set_value(cpld->strobe_gpio.desc, 0);
		gpiod_set_value(cpld->strobe_gpio.desc, 1);
	}

	/* Read back the value */
	for (i = cpld->dir_reg_size - 1; i >= 0; i--) {
		/* Cycle the strobe and read the data pin */
		gpiod_set_value(cpld->strobe_gpio.desc, 0);
		gpiod_set_value(cpld->strobe_gpio.desc, 1);
		dir_reg_verify |=
			(u64)gpiod_get_value(cpld->data_out_gpio.desc) << i;
	}

	/* Verify that the CPLD dir register was written successfully
	 * In some hardware revisions, data_out_gpio isn't actually
	 * connected so we skip this step if do_verify is not set
	 */
	if (cpld->do_verify && (dir_reg_verify != cpld->dir_reg)) {
		pr_err("CPLD verify error (expected: %llX, actual: %llX)\n",
		       cpld->dir_reg, dir_reg_verify);
		return -EIO;
	}

	/* Issue a dummy STB cycle to latch the dir register updates */
	gpiod_set_value(cpld->strobe_gpio.desc, 0);
	gpiod_set_value(cpld->strobe_gpio.desc, 1);

	return 0;
}

static int cpld_set_value(struct up_cpld_info *cpld, unsigned int offset,
			  int value)
{
	u64 old_regval;
	int ret = 0;

	spin_lock(&cpld->lock);

	old_regval = cpld->dir_reg;

	if (value)
		cpld->dir_reg |= 1ULL << offset;
	else
		cpld->dir_reg &= ~(1ULL << offset);

	/* Only update the CPLD register if it has changed */
	if (cpld->dir_reg != old_regval)
		ret = cpld_configure(cpld);

	spin_unlock(&cpld->lock);

	return ret;
}

static int up_pincfg_set(struct up_board_info *board, int offset, int value)
{
	if (board->cpld)
		return cpld_set_value(board->cpld, offset, value);

	gpio_set_value_cansleep(offset, value);
	return 0;
}

static inline struct up_pctrl *gc_to_up_pctrl(struct gpio_chip *gc)
{
	return container_of(gc, struct up_pctrl, chip);
}

static int up_gpiochip_match(struct gpio_chip *chip, void *data)
{
	return !strcmp(chip->label, data);
}

static int up_soc_gpio_resolve(struct platform_device *pdev,
			       struct up_soc_gpio_info *gpio_info)
{
	struct up_soc_gpiochip_info *ci = gpio_info->ci;

	if (!ci->chip) {
		ci->chip = gpiochip_find(ci->name, up_gpiochip_match);
		if (!ci->chip)
			return -EPROBE_DEFER;
	}
	gpio_info->gpio = ci->chip->base + gpio_info->offset;
	gpio_info->desc = gpio_to_desc(gpio_info->gpio);
	if (!gpio_info->desc) {
		dev_err(&pdev->dev, "Failed to get descriptor for gpio %d\n",
			gpio_info->gpio);
		return -EINVAL;
	}

	return 0;
}

static int up_gpio_pincfg_cpld(struct platform_device *pdev,
			       struct up_board_info *board)
{
	struct up_cpld_info *cpld = board->cpld;
	struct up_soc_gpio_info *cpld_gpios[] = {
		&cpld->strobe_gpio,
		&cpld->reset_gpio,
		&cpld->data_in_gpio,
		&cpld->data_out_gpio,
		&cpld->oe_gpio,
	};
	int i, ret;

	spin_lock_init(&cpld->lock);

	/* Initialise the CPLD config input GPIOs as outputs, initially low */
	for (i = 0; i < ARRAY_SIZE(cpld_gpios); i++) {
		struct up_soc_gpio_info *gpio_info = cpld_gpios[i];

		ret = up_soc_gpio_resolve(pdev, gpio_info);
		if (ret)
			return ret;

		ret = devm_gpio_request_one(&pdev->dev, gpio_info->gpio,
					    gpio_info->flags,
					    dev_name(&pdev->dev));
		if (ret)
			return ret;
	}

	/* Load initial CPLD configuration (all pins set for GPIO input) */
	ret = cpld_configure(board->cpld);
	if (ret) {
		dev_err(&pdev->dev, "CPLD initialisation failed\n");
		return ret;
	}

	/* Enable the CPLD outputs after a valid configuration has been set */
	gpiod_set_value(cpld->oe_gpio.desc, 1);

	return 0;
}

static int up_gpio_pincfg_init(struct platform_device *pdev,
			       struct up_board_info *board)
{
	unsigned i;
	int ret;

	/* Find the Cherry Trail GPIO descriptors corresponding
	 * with each GPIO pin on the UP Board I/O header
	 */
	for (i = 0; i < N_GPIO; i++) {
		struct up_pin_info *pin = &board->pins[i];

		ret = up_soc_gpio_resolve(pdev, &pin->soc_gpio);
		if (ret)
			return ret;

		/* Ensure the GPIO pins are configured as inputs initially */
		ret = gpiod_direction_input(pin->soc_gpio.desc);
		if (ret) {
			dev_err(&pdev->dev, "GPIO direction init failed\n");
			return ret;
		}
	}

	return up_gpio_pincfg_cpld(pdev, board);
}

static irqreturn_t up_gpio_irq_handler(int irq, void *data)
{
	struct up_pin_info *pin = (struct up_pin_info *)data;

	generic_handle_irq(pin->irq);
	return IRQ_HANDLED;
}

static unsigned int up_gpio_irq_startup(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct up_pctrl *up_pctrl = gc_to_up_pctrl(gc);
	unsigned offset = irqd_to_hwirq(data);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];

	return request_irq(pin->soc_gpio.irq, up_gpio_irq_handler,
			   IRQF_ONESHOT, dev_name(gc->parent), pin);
}

static void up_gpio_irq_shutdown(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct up_pctrl *up_pctrl = gc_to_up_pctrl(gc);
	unsigned offset = irqd_to_hwirq(data);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];

	free_irq(pin->soc_gpio.irq, pin);
}

static struct irq_chip up_gpio_irqchip = {
	.name = "up-gpio",
	.irq_startup = up_gpio_irq_startup,
	.irq_shutdown = up_gpio_irq_shutdown,
	.irq_enable = irq_chip_enable_parent,
	.irq_disable = irq_chip_disable_parent,
	.irq_mask = irq_chip_mask_parent,
	.irq_unmask = irq_chip_unmask_parent,
	.irq_ack = irq_chip_ack_parent,
	.irq_set_type = irq_chip_set_type_parent,
};

static int up_gpio_dir_in(struct gpio_chip *gc, unsigned offset)
{
	struct up_pctrl *up_pctrl = gc_to_up_pctrl(gc);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];
	struct gpio_desc *desc = pin->soc_gpio.desc;
	int ret;

	ret = gpiod_direction_input(desc);
	if (ret)
		return ret;

	return pinctrl_gpio_direction_input(gc->base + offset);
}

static int up_gpio_dir_out(struct gpio_chip *gc, unsigned offset, int value)
{
	struct up_pctrl *up_pctrl = gc_to_up_pctrl(gc);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];
	struct gpio_desc *desc = pin->soc_gpio.desc;
	int ret;

	ret = pinctrl_gpio_direction_output(gc->base + offset);
	if (ret)
		return ret;

	return gpiod_direction_output(desc, value);
}

static int up_gpio_get_dir(struct gpio_chip *gc, unsigned offset)
{
	struct up_pctrl *up_pctrl = gc_to_up_pctrl(gc);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];
	struct gpio_desc *desc = pin->soc_gpio.desc;

	return gpiod_get_direction(desc);
}

static int up_gpio_request(struct gpio_chip *gc, unsigned offset)
{
	struct up_pctrl *up_pctrl = gc_to_up_pctrl(gc);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];
	struct gpio_desc *desc = pin->soc_gpio.desc;

	pinctrl_request_gpio(gc->base + offset);
	return gpio_request(desc_to_gpio(desc), gc->label);
}

static void up_gpio_free(struct gpio_chip *gc, unsigned offset)
{
	struct up_pctrl *up_pctrl = gc_to_up_pctrl(gc);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];
	struct gpio_desc *desc = pin->soc_gpio.desc;

	pinctrl_free_gpio(gc->base + offset);
	gpio_free(desc_to_gpio(desc));
}

static int up_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct up_pctrl *up_pctrl = gc_to_up_pctrl(gc);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];
	struct gpio_desc *desc = pin->soc_gpio.desc;

	return gpiod_get_value(desc);
}

static void up_gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct up_pctrl *up_pctrl = gc_to_up_pctrl(gc);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];
	struct gpio_desc *desc = pin->soc_gpio.desc;

	gpiod_set_value(desc, value);
}

static struct gpio_chip up_gpio_chip = {
	.owner			= THIS_MODULE,
	.ngpio			= N_GPIO,
	.request		= up_gpio_request,
	.free			= up_gpio_free,
	.get_direction		= up_gpio_get_dir,
	.direction_input	= up_gpio_dir_in,
	.direction_output	= up_gpio_dir_out,
	.get			= up_gpio_get,
	.set			= up_gpio_set,
};

static int up_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(pin_groups);
}

static const char *up_get_group_name(struct pinctrl_dev *pctldev,
				     unsigned group)
{
	return pin_groups[group].name;
}

static int up_get_group_pins(struct pinctrl_dev *pctldev, unsigned group,
			     const unsigned **pins, unsigned *npins)
{
	*pins = pin_groups[group].pins;
	*npins = pin_groups[group].npins;
	return 0;
}

static const struct pinctrl_ops up_pinctrl_ops = {
	.get_groups_count = up_get_groups_count,
	.get_group_name = up_get_group_name,
	.get_group_pins = up_get_group_pins,
};

static int up_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(pin_functions);
}

static const char *up_get_function_name(struct pinctrl_dev *pctldev,
					unsigned function)
{
	return pin_functions[function].name;
}

static int up_get_function_groups(struct pinctrl_dev *pctldev,
				  unsigned function,
				  const char * const **groups,
				  unsigned * const ngroups)
{
	*groups = pin_functions[function].groups;
	*ngroups = pin_functions[function].ngroups;
	return 0;
}

static int up_pinmux_set_mux(struct pinctrl_dev *pctldev, unsigned function,
			     unsigned group)
{
	struct up_pctrl *up_pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct up_pingroup *grp = &pin_groups[group];
	int i;

	for (i = 0; i < grp->npins; i++) {
		int offset = grp->pins[i];
		struct up_pin_info *pin = &up_pctrl->board->pins[offset];

		if ((pin->dir_ctrl_pin != NONE) && (pin->func_dir != FDIR_NONE))
			up_pincfg_set(up_pctrl->board, pin->dir_ctrl_pin,
				      pin->func_dir == FDIR_IN ?
				      pin->dir_in : pin->dir_out);
		if (pin->mux_ctrl_pin != NONE)
			up_pincfg_set(up_pctrl->board, pin->mux_ctrl_pin,
				      pin->mux_func);
		pin->func_enabled = true;
	}

	return 0;
}

static int up_gpio_set_direction(struct pinctrl_dev *pctldev,
				 struct pinctrl_gpio_range *range,
				 unsigned offset, bool input)
{
	struct up_pctrl *up_pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];

	if (pin->dir_ctrl_pin != NONE)
		up_pincfg_set(up_pctrl->board, pin->dir_ctrl_pin,
			      input ? pin->dir_in : pin->dir_out);

	return 0;
}

static int up_gpio_request_enable(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned offset)
{
	struct up_pctrl *up_pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];

	if (pin->mux_ctrl_pin != NONE)
		up_pincfg_set(up_pctrl->board, pin->mux_ctrl_pin,
			      pin->mux_gpio);
	if (pin->dir_ctrl_pin != NONE)
		up_pincfg_set(up_pctrl->board, pin->dir_ctrl_pin,
			      gpiod_get_direction(pin->soc_gpio.desc)
			      ? pin->dir_in : pin->dir_out);

	return 0;
}

static void up_gpio_disable_free(struct pinctrl_dev *pctldev,
				 struct pinctrl_gpio_range *range,
				 unsigned offset)
{
	struct up_pctrl *up_pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct up_pin_info *pin = &up_pctrl->board->pins[offset];

	if (pin->func_enabled) {
		if ((pin->dir_ctrl_pin != NONE) && (pin->func_dir != FDIR_NONE))
			up_pincfg_set(up_pctrl->board, pin->dir_ctrl_pin,
				      pin->func_dir == FDIR_IN ?
				      pin->dir_in : pin->dir_out);
		if (pin->mux_ctrl_pin != NONE)
			up_pincfg_set(up_pctrl->board, pin->mux_ctrl_pin,
				      pin->mux_func);
	}
}

static const struct pinmux_ops up_pinmux_ops = {
	.get_functions_count = up_get_functions_count,
	.get_function_name = up_get_function_name,
	.get_function_groups = up_get_function_groups,
	.set_mux = up_pinmux_set_mux,
	.gpio_request_enable = up_gpio_request_enable,
	.gpio_disable_free = up_gpio_disable_free,
	.gpio_set_direction = up_gpio_set_direction,
};

static int up_config_get(struct pinctrl_dev *pctldev, unsigned pin,
			 unsigned long *config)
{
	return -ENOTSUPP;
}

static int up_config_set(struct pinctrl_dev *pctldev, unsigned pin,
			 unsigned long *configs, unsigned nconfigs)
{
	return 0;
}

static const struct pinconf_ops up_pinconf_ops = {
	.is_generic = true,
	.pin_config_set = up_config_set,
	.pin_config_get = up_config_get,
};

static struct pinctrl_desc up_pinctrl_desc = {
	.pins = up_pins_pi,
	.npins = ARRAY_SIZE(up_pins_pi),
	.pctlops = &up_pinctrl_ops,
	.pmxops = &up_pinmux_ops,
	.confops = &up_pinconf_ops,
	.owner = THIS_MODULE,
};

static void up_led_brightness_set(struct led_classdev *cdev,
				  enum led_brightness value)
{
	struct up_cpld_led_info *led = container_of(cdev,
						    struct up_cpld_led_info,
						    cdev);

	cpld_set_value(led->cpld, led->offset, value != LED_OFF);
}

static const struct dmi_system_id up_board_id_table[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "AAEON"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "UP-CHT01"),
			DMI_EXACT_MATCH(DMI_BOARD_VERSION, "V0.4"),
		},
		.driver_data = (void *)&up_board
	},
	{}
};

static int up_pinctrl_probe(struct platform_device *pdev)
{
	struct up_pctrl *up_pctrl;
	struct up_board_info *board;
	const struct dmi_system_id *system_id;
	unsigned offset;
	int ret;

	system_id = dmi_first_match(up_board_id_table);
	if (!system_id)
		return -ENXIO;

	board = system_id->driver_data;

	ret = up_gpio_pincfg_init(pdev, board);
	if (ret)
		return ret;

	up_pctrl = devm_kzalloc(&pdev->dev, sizeof(*up_pctrl), GFP_KERNEL);
	if (!up_pctrl)
		return -ENOMEM;

	platform_set_drvdata(pdev, up_pctrl);

	up_pctrl->pctldesc = up_pinctrl_desc;
	up_pctrl->pctldesc.name = dev_name(&pdev->dev);
	up_pctrl->pctldev = pinctrl_register(&up_pctrl->pctldesc,
					     &pdev->dev, up_pctrl);
	if (IS_ERR(up_pctrl->pctldev)) {
		dev_err(&pdev->dev, "failed to register pinctrl driver\n");
		return PTR_ERR(up_pctrl->pctldev);
	}

	up_pctrl->board = board;
	up_pctrl->chip = up_gpio_chip;
	up_pctrl->chip.label = dev_name(&pdev->dev);
	up_pctrl->chip.parent = &pdev->dev;

	ret = gpiochip_add(&up_pctrl->chip);
	if (ret) {
		dev_err(&pdev->dev, "failed to add %s chip\n",
			up_pctrl->chip.label);
		return ret;
	}

	ret = gpiochip_add_pin_range(&up_pctrl->chip, dev_name(&pdev->dev),
				     0, 0, N_GPIO);
	if (ret) {
		dev_err(&pdev->dev, "failed to add GPIO pin range\n");
		goto fail_add_pin_range;
	}

	ret = gpiochip_irqchip_add(&up_pctrl->chip, &up_gpio_irqchip, 0,
				   handle_simple_irq, IRQ_TYPE_NONE);
	if (ret) {
		dev_err(&pdev->dev, "failed to add IRQ chip\n");
		goto fail_irqchip_add;
	}

	for (offset = 0; offset < up_pctrl->chip.ngpio; offset++) {
		struct up_pin_info *pin = &board->pins[offset];
		struct irq_data *irq_data;

		pin->irq = irq_find_mapping(up_pctrl->chip.irqdomain, offset);
		pin->soc_gpio.irq = gpiod_to_irq(pin->soc_gpio.desc);
		irq_set_parent(pin->irq, pin->soc_gpio.irq);
		irq_data = irq_get_irq_data(pin->irq);
		irq_data->parent_data = irq_get_irq_data(pin->soc_gpio.irq);
	}

	/* Make sure the board has a CPLD */
	if (board->cpld) {
		struct up_cpld_info *cpld = board->cpld;
		size_t i;

		for (i = 0; i < cpld->num_leds; i++) {
			struct up_cpld_led_info *led = &cpld->leds[i];

			led->cpld = cpld;
			led->cdev.brightness_set = up_led_brightness_set;
			led->cdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
							"upboard:%s:",
							led->name);
			if (!led->cdev.name) {
				ret = -ENOMEM;
				goto fail_cpld_led;
			}

			ret = devm_led_classdev_register(&pdev->dev,
							 &led->cdev);
			if (ret)
				goto fail_cpld_led;
		}
	}

	return 0;

fail_cpld_led:
fail_irqchip_add:
fail_add_pin_range:
	gpiochip_remove(&up_pctrl->chip);

	return ret;
}

static int up_pinctrl_remove(struct platform_device *pdev)
{
	struct up_pctrl *up_pctrl = platform_get_drvdata(pdev);

	gpiochip_remove(&up_pctrl->chip);
	pinctrl_unregister(up_pctrl->pctldev);

	/* Disable the CPLD outputs */
	if (up_pctrl->board->cpld)
		gpiod_set_value(up_pctrl->board->cpld->oe_gpio.desc, 0);

	return 0;
}

static struct platform_driver up_pinctrl_driver = {
	.driver.name	= "up-pinctrl",
	.driver.owner	= THIS_MODULE,
	.probe		= up_pinctrl_probe,
	.remove		= up_pinctrl_remove,
};

static int __init up_pinctrl_init(void)
{
	return platform_driver_register(&up_pinctrl_driver);
}
subsys_initcall(up_pinctrl_init);

static void __exit up_pinctrl_exit(void)
{
	platform_driver_unregister(&up_pinctrl_driver);
}
module_exit(up_pinctrl_exit);

MODULE_AUTHOR("Dan O'Donovan <dan@emutex.com>");
MODULE_DESCRIPTION("Pin Control driver for UP Board I/O pin header");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:up-pinctrl");
