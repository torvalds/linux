// SPDX-License-Identifier: GPL-2.0+
/*
 * GPIO interface for Winbond Super I/O chips
 * Currently, only W83627UHG (Nuvoton NCT6627UD) is supported.
 *
 * Author: Maciej S. Szmigiero <mail@maciej.szmigiero.name>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/gpio/driver.h>
#include <linux/ioport.h>
#include <linux/isa.h>
#include <linux/module.h>

#define WB_GPIO_DRIVER_NAME		KBUILD_MODNAME

#define WB_SIO_BASE			0x2e
#define WB_SIO_BASE_HIGH		0x4e

#define WB_SIO_EXT_ENTER_KEY		0x87
#define WB_SIO_EXT_EXIT_KEY		0xaa

/* global chip registers */

#define WB_SIO_REG_LOGICAL		0x07

#define WB_SIO_REG_CHIP_MSB		0x20
#define WB_SIO_REG_CHIP_LSB		0x21

#define WB_SIO_CHIP_ID_W83627UHG	0xa230
#define WB_SIO_CHIP_ID_W83627UHG_MASK	GENMASK(15, 4)

#define WB_SIO_REG_DPD			0x22
#define WB_SIO_REG_DPD_UARTA		4
#define WB_SIO_REG_DPD_UARTB		5

#define WB_SIO_REG_IDPD		0x23
#define WB_SIO_REG_IDPD_UARTC		4
#define WB_SIO_REG_IDPD_UARTD		5
#define WB_SIO_REG_IDPD_UARTE		6
#define WB_SIO_REG_IDPD_UARTF		7

#define WB_SIO_REG_GLOBAL_OPT		0x24
#define WB_SIO_REG_GO_ENFDC		1

#define WB_SIO_REG_OVTGPIO3456		0x29
#define WB_SIO_REG_OG3456_G3PP		3
#define WB_SIO_REG_OG3456_G4PP		4
#define WB_SIO_REG_OG3456_G5PP		5
#define WB_SIO_REG_OG3456_G6PP		7

#define WB_SIO_REG_I2C_PS		0x2a
#define WB_SIO_REG_I2CPS_I2CFS		1

#define WB_SIO_REG_GPIO1_MF		0x2c
#define WB_SIO_REG_G1MF_G1PP		6
#define WB_SIO_REG_G1MF_G2PP		7
#define WB_SIO_REG_G1MF_FS_MASK	GENMASK(1, 0)
#define WB_SIO_REG_G1MF_FS_IR_OFF	0
#define WB_SIO_REG_G1MF_FS_IR		1
#define WB_SIO_REG_G1MF_FS_GPIO1	2
#define WB_SIO_REG_G1MF_FS_UARTB	3

/* not an actual device number, just a value meaning 'no device' */
#define WB_SIO_DEV_NONE		0xff

/* registers with offsets >= 0x30 are specific for a particular device */

/* UART B logical device */
#define WB_SIO_DEV_UARTB		0x03
#define WB_SIO_UARTB_REG_ENABLE	0x30
#define WB_SIO_UARTB_ENABLE_ON		0

/* UART C logical device */
#define WB_SIO_DEV_UARTC		0x06
#define WB_SIO_UARTC_REG_ENABLE	0x30
#define WB_SIO_UARTC_ENABLE_ON		0

/* GPIO3, GPIO4 logical device */
#define WB_SIO_DEV_GPIO34		0x07
#define WB_SIO_GPIO34_REG_ENABLE	0x30
#define WB_SIO_GPIO34_ENABLE_3		0
#define WB_SIO_GPIO34_ENABLE_4		1
#define WB_SIO_GPIO34_REG_IO3		0xe0
#define WB_SIO_GPIO34_REG_DATA3	0xe1
#define WB_SIO_GPIO34_REG_INV3		0xe2
#define WB_SIO_GPIO34_REG_IO4		0xe4
#define WB_SIO_GPIO34_REG_DATA4	0xe5
#define WB_SIO_GPIO34_REG_INV4		0xe6

/* WDTO, PLED, GPIO5, GPIO6 logical device */
#define WB_SIO_DEV_WDGPIO56		0x08
#define WB_SIO_WDGPIO56_REG_ENABLE	0x30
#define WB_SIO_WDGPIO56_ENABLE_5	1
#define WB_SIO_WDGPIO56_ENABLE_6	2
#define WB_SIO_WDGPIO56_REG_IO5	0xe0
#define WB_SIO_WDGPIO56_REG_DATA5	0xe1
#define WB_SIO_WDGPIO56_REG_INV5	0xe2
#define WB_SIO_WDGPIO56_REG_IO6	0xe4
#define WB_SIO_WDGPIO56_REG_DATA6	0xe5
#define WB_SIO_WDGPIO56_REG_INV6	0xe6

/* GPIO1, GPIO2, SUSLED logical device */
#define WB_SIO_DEV_GPIO12		0x09
#define WB_SIO_GPIO12_REG_ENABLE	0x30
#define WB_SIO_GPIO12_ENABLE_1		0
#define WB_SIO_GPIO12_ENABLE_2		1
#define WB_SIO_GPIO12_REG_IO1		0xe0
#define WB_SIO_GPIO12_REG_DATA1	0xe1
#define WB_SIO_GPIO12_REG_INV1		0xe2
#define WB_SIO_GPIO12_REG_IO2		0xe4
#define WB_SIO_GPIO12_REG_DATA2	0xe5
#define WB_SIO_GPIO12_REG_INV2		0xe6

/* UART D logical device */
#define WB_SIO_DEV_UARTD		0x0d
#define WB_SIO_UARTD_REG_ENABLE	0x30
#define WB_SIO_UARTD_ENABLE_ON		0

/* UART E logical device */
#define WB_SIO_DEV_UARTE		0x0e
#define WB_SIO_UARTE_REG_ENABLE	0x30
#define WB_SIO_UARTE_ENABLE_ON		0

/*
 * for a description what a particular field of this struct means please see
 * a description of the relevant module parameter at the bottom of this file
 */
struct winbond_gpio_params {
	unsigned long base;
	unsigned long gpios;
	unsigned long ppgpios;
	unsigned long odgpios;
	bool pledgpio;
	bool beepgpio;
	bool i2cgpio;
};

static struct winbond_gpio_params params;

static int winbond_sio_enter(unsigned long base)
{
	if (!request_muxed_region(base, 2, WB_GPIO_DRIVER_NAME))
		return -EBUSY;

	/*
	 * datasheet says two successive writes of the "key" value are needed
	 * in order for chip to enter the "Extended Function Mode"
	 */
	outb(WB_SIO_EXT_ENTER_KEY, base);
	outb(WB_SIO_EXT_ENTER_KEY, base);

	return 0;
}

static void winbond_sio_select_logical(unsigned long base, u8 dev)
{
	outb(WB_SIO_REG_LOGICAL, base);
	outb(dev, base + 1);
}

static void winbond_sio_leave(unsigned long base)
{
	outb(WB_SIO_EXT_EXIT_KEY, base);

	release_region(base, 2);
}

static void winbond_sio_reg_write(unsigned long base, u8 reg, u8 data)
{
	outb(reg, base);
	outb(data, base + 1);
}

static u8 winbond_sio_reg_read(unsigned long base, u8 reg)
{
	outb(reg, base);
	return inb(base + 1);
}

static void winbond_sio_reg_bset(unsigned long base, u8 reg, u8 bit)
{
	u8 val;

	val = winbond_sio_reg_read(base, reg);
	val |= BIT(bit);
	winbond_sio_reg_write(base, reg, val);
}

static void winbond_sio_reg_bclear(unsigned long base, u8 reg, u8 bit)
{
	u8 val;

	val = winbond_sio_reg_read(base, reg);
	val &= ~BIT(bit);
	winbond_sio_reg_write(base, reg, val);
}

static bool winbond_sio_reg_btest(unsigned long base, u8 reg, u8 bit)
{
	return winbond_sio_reg_read(base, reg) & BIT(bit);
}

/**
 * struct winbond_gpio_port_conflict - possibly conflicting device information
 * @name:	device name (NULL means no conflicting device defined)
 * @dev:	Super I/O logical device number where the testreg register
 *		is located (or WB_SIO_DEV_NONE - don't select any
 *		logical device)
 * @testreg:	register number where the testbit bit is located
 * @testbit:	index of a bit to check whether an actual conflict exists
 * @warnonly:	if set then a conflict isn't fatal (just warn about it),
 *		otherwise disable the particular GPIO port if a conflict
 *		is detected
 */
struct winbond_gpio_port_conflict {
	const char *name;
	u8 dev;
	u8 testreg;
	u8 testbit;
	bool warnonly;
};

/**
 * struct winbond_gpio_info - information about a particular GPIO port (device)
 * @dev:		Super I/O logical device number of the registers
 *			specified below
 * @enablereg:		port enable bit register number
 * @enablebit:		index of a port enable bit
 * @outputreg:		output driver mode bit register number
 * @outputppbit:	index of a push-pull output driver mode bit
 * @ioreg:		data direction register number
 * @invreg:		pin data inversion register number
 * @datareg:		pin data register number
 * @conflict:		description of a device that possibly conflicts with
 *			this port
 */
struct winbond_gpio_info {
	u8 dev;
	u8 enablereg;
	u8 enablebit;
	u8 outputreg;
	u8 outputppbit;
	u8 ioreg;
	u8 invreg;
	u8 datareg;
	struct winbond_gpio_port_conflict conflict;
};

static const struct winbond_gpio_info winbond_gpio_infos[6] = {
	{ /* 0 */
		.dev = WB_SIO_DEV_GPIO12,
		.enablereg = WB_SIO_GPIO12_REG_ENABLE,
		.enablebit = WB_SIO_GPIO12_ENABLE_1,
		.outputreg = WB_SIO_REG_GPIO1_MF,
		.outputppbit = WB_SIO_REG_G1MF_G1PP,
		.ioreg = WB_SIO_GPIO12_REG_IO1,
		.invreg = WB_SIO_GPIO12_REG_INV1,
		.datareg = WB_SIO_GPIO12_REG_DATA1,
		.conflict = {
			.name = "UARTB",
			.dev = WB_SIO_DEV_UARTB,
			.testreg = WB_SIO_UARTB_REG_ENABLE,
			.testbit = WB_SIO_UARTB_ENABLE_ON,
			.warnonly = true
		}
	},
	{ /* 1 */
		.dev = WB_SIO_DEV_GPIO12,
		.enablereg = WB_SIO_GPIO12_REG_ENABLE,
		.enablebit = WB_SIO_GPIO12_ENABLE_2,
		.outputreg = WB_SIO_REG_GPIO1_MF,
		.outputppbit = WB_SIO_REG_G1MF_G2PP,
		.ioreg = WB_SIO_GPIO12_REG_IO2,
		.invreg = WB_SIO_GPIO12_REG_INV2,
		.datareg = WB_SIO_GPIO12_REG_DATA2
		/* special conflict handling so doesn't use conflict data */
	},
	{ /* 2 */
		.dev = WB_SIO_DEV_GPIO34,
		.enablereg = WB_SIO_GPIO34_REG_ENABLE,
		.enablebit = WB_SIO_GPIO34_ENABLE_3,
		.outputreg = WB_SIO_REG_OVTGPIO3456,
		.outputppbit = WB_SIO_REG_OG3456_G3PP,
		.ioreg = WB_SIO_GPIO34_REG_IO3,
		.invreg = WB_SIO_GPIO34_REG_INV3,
		.datareg = WB_SIO_GPIO34_REG_DATA3,
		.conflict = {
			.name = "UARTC",
			.dev = WB_SIO_DEV_UARTC,
			.testreg = WB_SIO_UARTC_REG_ENABLE,
			.testbit = WB_SIO_UARTC_ENABLE_ON,
			.warnonly = true
		}
	},
	{ /* 3 */
		.dev = WB_SIO_DEV_GPIO34,
		.enablereg = WB_SIO_GPIO34_REG_ENABLE,
		.enablebit = WB_SIO_GPIO34_ENABLE_4,
		.outputreg = WB_SIO_REG_OVTGPIO3456,
		.outputppbit = WB_SIO_REG_OG3456_G4PP,
		.ioreg = WB_SIO_GPIO34_REG_IO4,
		.invreg = WB_SIO_GPIO34_REG_INV4,
		.datareg = WB_SIO_GPIO34_REG_DATA4,
		.conflict = {
			.name = "UARTD",
			.dev = WB_SIO_DEV_UARTD,
			.testreg = WB_SIO_UARTD_REG_ENABLE,
			.testbit = WB_SIO_UARTD_ENABLE_ON,
			.warnonly = true
		}
	},
	{ /* 4 */
		.dev = WB_SIO_DEV_WDGPIO56,
		.enablereg = WB_SIO_WDGPIO56_REG_ENABLE,
		.enablebit = WB_SIO_WDGPIO56_ENABLE_5,
		.outputreg = WB_SIO_REG_OVTGPIO3456,
		.outputppbit = WB_SIO_REG_OG3456_G5PP,
		.ioreg = WB_SIO_WDGPIO56_REG_IO5,
		.invreg = WB_SIO_WDGPIO56_REG_INV5,
		.datareg = WB_SIO_WDGPIO56_REG_DATA5,
		.conflict = {
			.name = "UARTE",
			.dev = WB_SIO_DEV_UARTE,
			.testreg = WB_SIO_UARTE_REG_ENABLE,
			.testbit = WB_SIO_UARTE_ENABLE_ON,
			.warnonly = true
		}
	},
	{ /* 5 */
		.dev = WB_SIO_DEV_WDGPIO56,
		.enablereg = WB_SIO_WDGPIO56_REG_ENABLE,
		.enablebit = WB_SIO_WDGPIO56_ENABLE_6,
		.outputreg = WB_SIO_REG_OVTGPIO3456,
		.outputppbit = WB_SIO_REG_OG3456_G6PP,
		.ioreg = WB_SIO_WDGPIO56_REG_IO6,
		.invreg = WB_SIO_WDGPIO56_REG_INV6,
		.datareg = WB_SIO_WDGPIO56_REG_DATA6,
		.conflict = {
			.name = "FDC",
			.dev = WB_SIO_DEV_NONE,
			.testreg = WB_SIO_REG_GLOBAL_OPT,
			.testbit = WB_SIO_REG_GO_ENFDC,
			.warnonly = false
		}
	}
};

/* returns whether changing a pin is allowed */
static bool winbond_gpio_get_info(unsigned int *gpio_num,
				  const struct winbond_gpio_info **info)
{
	bool allow_changing = true;
	unsigned long i;

	for_each_set_bit(i, &params.gpios, BITS_PER_LONG) {
		if (*gpio_num < 8)
			break;

		*gpio_num -= 8;
	}

	*info = &winbond_gpio_infos[i];

	/*
	 * GPIO2 (the second port) shares some pins with a basic PC
	 * functionality, which is very likely controlled by the firmware.
	 * Don't allow changing these pins by default.
	 */
	if (i == 1) {
		if (*gpio_num == 0 && !params.pledgpio)
			allow_changing = false;
		else if (*gpio_num == 1 && !params.beepgpio)
			allow_changing = false;
		else if ((*gpio_num == 5 || *gpio_num == 6) && !params.i2cgpio)
			allow_changing = false;
	}

	return allow_changing;
}

static int winbond_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	unsigned long *base = gpiochip_get_data(gc);
	const struct winbond_gpio_info *info;
	bool val;
	int ret;

	winbond_gpio_get_info(&offset, &info);

	ret = winbond_sio_enter(*base);
	if (ret)
		return ret;

	winbond_sio_select_logical(*base, info->dev);

	val = winbond_sio_reg_btest(*base, info->datareg, offset);
	if (winbond_sio_reg_btest(*base, info->invreg, offset))
		val = !val;

	winbond_sio_leave(*base);

	return val;
}

static int winbond_gpio_direction_in(struct gpio_chip *gc, unsigned int offset)
{
	unsigned long *base = gpiochip_get_data(gc);
	const struct winbond_gpio_info *info;
	int ret;

	if (!winbond_gpio_get_info(&offset, &info))
		return -EACCES;

	ret = winbond_sio_enter(*base);
	if (ret)
		return ret;

	winbond_sio_select_logical(*base, info->dev);

	winbond_sio_reg_bset(*base, info->ioreg, offset);

	winbond_sio_leave(*base);

	return 0;
}

static int winbond_gpio_direction_out(struct gpio_chip *gc,
				      unsigned int offset,
				      int val)
{
	unsigned long *base = gpiochip_get_data(gc);
	const struct winbond_gpio_info *info;
	int ret;

	if (!winbond_gpio_get_info(&offset, &info))
		return -EACCES;

	ret = winbond_sio_enter(*base);
	if (ret)
		return ret;

	winbond_sio_select_logical(*base, info->dev);

	winbond_sio_reg_bclear(*base, info->ioreg, offset);

	if (winbond_sio_reg_btest(*base, info->invreg, offset))
		val = !val;

	if (val)
		winbond_sio_reg_bset(*base, info->datareg, offset);
	else
		winbond_sio_reg_bclear(*base, info->datareg, offset);

	winbond_sio_leave(*base);

	return 0;
}

static int winbond_gpio_set(struct gpio_chip *gc, unsigned int offset,
			    int val)
{
	unsigned long *base = gpiochip_get_data(gc);
	const struct winbond_gpio_info *info;
	int ret;

	if (!winbond_gpio_get_info(&offset, &info))
		return -EACCES;

	ret = winbond_sio_enter(*base);
	if (ret)
		return ret;

	winbond_sio_select_logical(*base, info->dev);

	if (winbond_sio_reg_btest(*base, info->invreg, offset))
		val = !val;

	if (val)
		winbond_sio_reg_bset(*base, info->datareg, offset);
	else
		winbond_sio_reg_bclear(*base, info->datareg, offset);

	winbond_sio_leave(*base);

	return 0;
}

static struct gpio_chip winbond_gpio_chip = {
	.base			= -1,
	.label			= WB_GPIO_DRIVER_NAME,
	.owner			= THIS_MODULE,
	.can_sleep		= true,
	.get			= winbond_gpio_get,
	.direction_input	= winbond_gpio_direction_in,
	.set			= winbond_gpio_set,
	.direction_output	= winbond_gpio_direction_out,
};

static void winbond_gpio_configure_port0_pins(unsigned long base)
{
	unsigned int val;

	val = winbond_sio_reg_read(base, WB_SIO_REG_GPIO1_MF);
	if ((val & WB_SIO_REG_G1MF_FS_MASK) == WB_SIO_REG_G1MF_FS_GPIO1)
		return;

	pr_warn("GPIO1 pins were connected to something else (%.2x), fixing\n",
		val);

	val &= ~WB_SIO_REG_G1MF_FS_MASK;
	val |= WB_SIO_REG_G1MF_FS_GPIO1;

	winbond_sio_reg_write(base, WB_SIO_REG_GPIO1_MF, val);
}

static void winbond_gpio_configure_port1_check_i2c(unsigned long base)
{
	params.i2cgpio = !winbond_sio_reg_btest(base, WB_SIO_REG_I2C_PS,
						WB_SIO_REG_I2CPS_I2CFS);
	if (!params.i2cgpio)
		pr_warn("disabling GPIO2.5 and GPIO2.6 as I2C is enabled\n");
}

static bool winbond_gpio_configure_port(unsigned long base, unsigned int idx)
{
	const struct winbond_gpio_info *info = &winbond_gpio_infos[idx];
	const struct winbond_gpio_port_conflict *conflict = &info->conflict;

	/* is there a possible conflicting device defined? */
	if (conflict->name != NULL) {
		if (conflict->dev != WB_SIO_DEV_NONE)
			winbond_sio_select_logical(base, conflict->dev);

		if (winbond_sio_reg_btest(base, conflict->testreg,
					  conflict->testbit)) {
			if (conflict->warnonly)
				pr_warn("enabled GPIO%u share pins with active %s\n",
					idx + 1, conflict->name);
			else {
				pr_warn("disabling GPIO%u as %s is enabled\n",
					idx + 1, conflict->name);
				return false;
			}
		}
	}

	/* GPIO1 and GPIO2 need some (additional) special handling */
	if (idx == 0)
		winbond_gpio_configure_port0_pins(base);
	else if (idx == 1)
		winbond_gpio_configure_port1_check_i2c(base);

	winbond_sio_select_logical(base, info->dev);

	winbond_sio_reg_bset(base, info->enablereg, info->enablebit);

	if (params.ppgpios & BIT(idx))
		winbond_sio_reg_bset(base, info->outputreg,
				     info->outputppbit);
	else if (params.odgpios & BIT(idx))
		winbond_sio_reg_bclear(base, info->outputreg,
				       info->outputppbit);
	else
		pr_notice("GPIO%u pins are %s\n", idx + 1,
			  winbond_sio_reg_btest(base, info->outputreg,
						info->outputppbit) ?
			  "push-pull" :
			  "open drain");

	return true;
}

static int winbond_gpio_configure(unsigned long base)
{
	unsigned long i;

	for_each_set_bit(i, &params.gpios, BITS_PER_LONG)
		if (!winbond_gpio_configure_port(base, i))
			__clear_bit(i, &params.gpios);

	if (!params.gpios) {
		pr_err("please use 'gpios' module parameter to select some active GPIO ports to enable\n");
		return -EINVAL;
	}

	return 0;
}

static int winbond_gpio_check_chip(unsigned long base)
{
	int ret;
	unsigned int chip;

	ret = winbond_sio_enter(base);
	if (ret)
		return ret;

	chip = winbond_sio_reg_read(base, WB_SIO_REG_CHIP_MSB) << 8;
	chip |= winbond_sio_reg_read(base, WB_SIO_REG_CHIP_LSB);

	pr_notice("chip ID at %lx is %.4x\n", base, chip);

	if ((chip & WB_SIO_CHIP_ID_W83627UHG_MASK) !=
	    WB_SIO_CHIP_ID_W83627UHG) {
		pr_err("not an our chip\n");
		ret = -ENODEV;
	}

	winbond_sio_leave(base);

	return ret;
}

static int winbond_gpio_imatch(struct device *dev, unsigned int id)
{
	unsigned long gpios_rem;
	int ret;

	gpios_rem = params.gpios & ~GENMASK(ARRAY_SIZE(winbond_gpio_infos) - 1,
					    0);
	if (gpios_rem) {
		pr_warn("unknown ports (%lx) enabled in GPIO ports bitmask\n",
			gpios_rem);
		params.gpios &= ~gpios_rem;
	}

	if (params.ppgpios & params.odgpios) {
		pr_err("some GPIO ports are set both to push-pull and open drain mode at the same time\n");
		return 0;
	}

	if (params.base != 0)
		return winbond_gpio_check_chip(params.base) == 0;

	/*
	 * if the 'base' module parameter is unset probe two chip default
	 * I/O port bases
	 */
	params.base = WB_SIO_BASE;
	ret = winbond_gpio_check_chip(params.base);
	if (ret == 0)
		return 1;
	if (ret != -ENODEV && ret != -EBUSY)
		return 0;

	params.base = WB_SIO_BASE_HIGH;
	return winbond_gpio_check_chip(params.base) == 0;
}

static int winbond_gpio_iprobe(struct device *dev, unsigned int id)
{
	int ret;

	if (params.base == 0)
		return -EINVAL;

	ret = winbond_sio_enter(params.base);
	if (ret)
		return ret;

	ret = winbond_gpio_configure(params.base);

	winbond_sio_leave(params.base);

	if (ret)
		return ret;

	/*
	 * Add 8 gpios for every GPIO port that was enabled in gpios
	 * module parameter (that wasn't disabled earlier in
	 * winbond_gpio_configure() & co. due to, for example, a pin conflict).
	 */
	winbond_gpio_chip.ngpio = hweight_long(params.gpios) * 8;

	/*
	 * GPIO6 port has only 5 pins, so if it is enabled we have to adjust
	 * the total count appropriately
	 */
	if (params.gpios & BIT(5))
		winbond_gpio_chip.ngpio -= (8 - 5);

	winbond_gpio_chip.parent = dev;

	return devm_gpiochip_add_data(dev, &winbond_gpio_chip, &params.base);
}

static struct isa_driver winbond_gpio_idriver = {
	.driver = {
		.name	= WB_GPIO_DRIVER_NAME,
	},
	.match	= winbond_gpio_imatch,
	.probe	= winbond_gpio_iprobe,
};

module_isa_driver(winbond_gpio_idriver, 1);

module_param_named(base, params.base, ulong, 0444);
MODULE_PARM_DESC(base,
		 "I/O port base (when unset - probe chip default ones)");

/* This parameter sets which GPIO devices (ports) we enable */
module_param_named(gpios, params.gpios, ulong, 0444);
MODULE_PARM_DESC(gpios,
		 "bitmask of GPIO ports to enable (bit 0 - GPIO1, bit 1 - GPIO2, etc.");

/*
 * These two parameters below set how we configure GPIO ports output drivers.
 * It can't be a one bitmask since we need three values per port: push-pull,
 * open-drain and keep as-is (this is the default).
 */
module_param_named(ppgpios, params.ppgpios, ulong, 0444);
MODULE_PARM_DESC(ppgpios,
		 "bitmask of GPIO ports to set to push-pull mode (bit 0 - GPIO1, bit 1 - GPIO2, etc.");

module_param_named(odgpios, params.odgpios, ulong, 0444);
MODULE_PARM_DESC(odgpios,
		 "bitmask of GPIO ports to set to open drain mode (bit 0 - GPIO1, bit 1 - GPIO2, etc.");

/*
 * GPIO2.0 and GPIO2.1 control a basic PC functionality that we
 * don't allow tinkering with by default (it is very likely that the
 * firmware owns these pins).
 * These two parameters below allow overriding these prohibitions.
 */
module_param_named(pledgpio, params.pledgpio, bool, 0644);
MODULE_PARM_DESC(pledgpio,
		 "enable changing value of GPIO2.0 bit (Power LED), default no.");

module_param_named(beepgpio, params.beepgpio, bool, 0644);
MODULE_PARM_DESC(beepgpio,
		 "enable changing value of GPIO2.1 bit (BEEP), default no.");

MODULE_AUTHOR("Maciej S. Szmigiero <mail@maciej.szmigiero.name>");
MODULE_DESCRIPTION("GPIO interface for Winbond Super I/O chips");
MODULE_LICENSE("GPL");
