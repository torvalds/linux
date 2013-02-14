/*
 * twl_core.c - driver for TWL4030/TWL5030/TWL60X0/TPS659x0 PM
 * and audio CODEC devices
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * Modifications to defer interrupt handling to a kernel thread:
 * Copyright (C) 2006 MontaVista Software, Inc.
 *
 * Based on tlv320aic23.c:
 * Copyright (c) by Kai Svahn <kai.svahn@nokia.com>
 *
 * Code cleanup and modifications to IRQ handler.
 * by syed khasim <x0khasim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>

#include <linux/regulator/machine.h>

#include <linux/i2c.h>
#include <linux/i2c/twl.h>

#include "twl-core.h"

/*
 * The TWL4030 "Triton 2" is one of a family of a multi-function "Power
 * Management and System Companion Device" chips originally designed for
 * use in OMAP2 and OMAP 3 based systems.  Its control interfaces use I2C,
 * often at around 3 Mbit/sec, including for interrupt handling.
 *
 * This driver core provides genirq support for the interrupts emitted,
 * by the various modules, and exports register access primitives.
 *
 * FIXME this driver currently requires use of the first interrupt line
 * (and associated registers).
 */

#define DRIVER_NAME			"twl"

/* Triton Core internal information (BEGIN) */

#define TWL_NUM_SLAVES		4

#define SUB_CHIP_ID0 0
#define SUB_CHIP_ID1 1
#define SUB_CHIP_ID2 2
#define SUB_CHIP_ID3 3
#define SUB_CHIP_ID_INVAL 0xff

#define TWL_MODULE_LAST TWL4030_MODULE_LAST

/* Base Address defns for twl4030_map[] */

/* subchip/slave 0 - USB ID */
#define TWL4030_BASEADD_USB		0x0000

/* subchip/slave 1 - AUD ID */
#define TWL4030_BASEADD_AUDIO_VOICE	0x0000
#define TWL4030_BASEADD_GPIO		0x0098
#define TWL4030_BASEADD_INTBR		0x0085
#define TWL4030_BASEADD_PIH		0x0080
#define TWL4030_BASEADD_TEST		0x004C

/* subchip/slave 2 - AUX ID */
#define TWL4030_BASEADD_INTERRUPTS	0x00B9
#define TWL4030_BASEADD_LED		0x00EE
#define TWL4030_BASEADD_MADC		0x0000
#define TWL4030_BASEADD_MAIN_CHARGE	0x0074
#define TWL4030_BASEADD_PRECHARGE	0x00AA
#define TWL4030_BASEADD_PWM0		0x00F8
#define TWL4030_BASEADD_PWM1		0x00FB
#define TWL4030_BASEADD_PWMA		0x00EF
#define TWL4030_BASEADD_PWMB		0x00F1
#define TWL4030_BASEADD_KEYPAD		0x00D2

#define TWL5031_BASEADD_ACCESSORY	0x0074 /* Replaces Main Charge */
#define TWL5031_BASEADD_INTERRUPTS	0x00B9 /* Different than TWL4030's
						  one */

/* subchip/slave 3 - POWER ID */
#define TWL4030_BASEADD_BACKUP		0x0014
#define TWL4030_BASEADD_INT		0x002E
#define TWL4030_BASEADD_PM_MASTER	0x0036
#define TWL4030_BASEADD_PM_RECEIVER	0x005B
#define TWL4030_BASEADD_RTC		0x001C
#define TWL4030_BASEADD_SECURED_REG	0x0000

/* Triton Core internal information (END) */


/* subchip/slave 0 0x48 - POWER */
#define TWL6030_BASEADD_RTC		0x0000
#define TWL6030_BASEADD_MEM		0x0017
#define TWL6030_BASEADD_PM_MASTER	0x001F
#define TWL6030_BASEADD_PM_SLAVE_MISC	0x0030 /* PM_RECEIVER */
#define TWL6030_BASEADD_PM_MISC		0x00E2
#define TWL6030_BASEADD_PM_PUPD		0x00F0

/* subchip/slave 1 0x49 - FEATURE */
#define TWL6030_BASEADD_USB		0x0000
#define TWL6030_BASEADD_GPADC_CTRL	0x002E
#define TWL6030_BASEADD_AUX		0x0090
#define TWL6030_BASEADD_PWM		0x00BA
#define TWL6030_BASEADD_GASGAUGE	0x00C0
#define TWL6030_BASEADD_PIH		0x00D0
#define TWL6030_BASEADD_CHARGER		0x00E0
#define TWL6025_BASEADD_CHARGER		0x00DA

/* subchip/slave 2 0x4A - DFT */
#define TWL6030_BASEADD_DIEID		0x00C0

/* subchip/slave 3 0x4B - AUDIO */
#define TWL6030_BASEADD_AUDIO		0x0000
#define TWL6030_BASEADD_RSV		0x0000
#define TWL6030_BASEADD_ZERO		0x0000

/* Few power values */
#define R_CFG_BOOT			0x05

/* some fields in R_CFG_BOOT */
#define HFCLK_FREQ_19p2_MHZ		(1 << 0)
#define HFCLK_FREQ_26_MHZ		(2 << 0)
#define HFCLK_FREQ_38p4_MHZ		(3 << 0)
#define HIGH_PERF_SQ			(1 << 3)
#define CK32K_LOWPWR_EN			(1 << 7)

/*----------------------------------------------------------------------*/

/* is driver active, bound to a chip? */
static bool inuse;

/* TWL IDCODE Register value */
static u32 twl_idcode;

static unsigned int twl_id;
unsigned int twl_rev(void)
{
	return twl_id;
}
EXPORT_SYMBOL(twl_rev);

/* Structure for each TWL4030/TWL6030 Slave */
struct twl_client {
	struct i2c_client *client;
	struct regmap *regmap;
};

static struct twl_client twl_modules[TWL_NUM_SLAVES];

/* mapping the module id to slave id and base address */
struct twl_mapping {
	unsigned char sid;	/* Slave ID */
	unsigned char base;	/* base address */
};
static struct twl_mapping *twl_map;

static struct twl_mapping twl4030_map[] = {
	/*
	 * NOTE:  don't change this table without updating the
	 * <linux/i2c/twl.h> defines for TWL4030_MODULE_*
	 * so they continue to match the order in this table.
	 */

	{ 0, TWL4030_BASEADD_USB },
	{ 1, TWL4030_BASEADD_AUDIO_VOICE },
	{ 1, TWL4030_BASEADD_GPIO },
	{ 1, TWL4030_BASEADD_INTBR },
	{ 1, TWL4030_BASEADD_PIH },

	{ 1, TWL4030_BASEADD_TEST },
	{ 2, TWL4030_BASEADD_KEYPAD },
	{ 2, TWL4030_BASEADD_MADC },
	{ 2, TWL4030_BASEADD_INTERRUPTS },
	{ 2, TWL4030_BASEADD_LED },

	{ 2, TWL4030_BASEADD_MAIN_CHARGE },
	{ 2, TWL4030_BASEADD_PRECHARGE },
	{ 2, TWL4030_BASEADD_PWM0 },
	{ 2, TWL4030_BASEADD_PWM1 },
	{ 2, TWL4030_BASEADD_PWMA },

	{ 2, TWL4030_BASEADD_PWMB },
	{ 2, TWL5031_BASEADD_ACCESSORY },
	{ 2, TWL5031_BASEADD_INTERRUPTS },
	{ 3, TWL4030_BASEADD_BACKUP },
	{ 3, TWL4030_BASEADD_INT },

	{ 3, TWL4030_BASEADD_PM_MASTER },
	{ 3, TWL4030_BASEADD_PM_RECEIVER },
	{ 3, TWL4030_BASEADD_RTC },
	{ 3, TWL4030_BASEADD_SECURED_REG },
};

static struct regmap_config twl4030_regmap_config[4] = {
	{
		/* Address 0x48 */
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0xff,
	},
	{
		/* Address 0x49 */
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0xff,
	},
	{
		/* Address 0x4a */
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0xff,
	},
	{
		/* Address 0x4b */
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0xff,
	},
};

static struct twl_mapping twl6030_map[] = {
	/*
	 * NOTE:  don't change this table without updating the
	 * <linux/i2c/twl.h> defines for TWL4030_MODULE_*
	 * so they continue to match the order in this table.
	 */
	{ SUB_CHIP_ID1, TWL6030_BASEADD_USB },
	{ SUB_CHIP_ID_INVAL, TWL6030_BASEADD_AUDIO },
	{ SUB_CHIP_ID2, TWL6030_BASEADD_DIEID },
	{ SUB_CHIP_ID2, TWL6030_BASEADD_RSV },
	{ SUB_CHIP_ID1, TWL6030_BASEADD_PIH },

	{ SUB_CHIP_ID2, TWL6030_BASEADD_RSV },
	{ SUB_CHIP_ID2, TWL6030_BASEADD_RSV },
	{ SUB_CHIP_ID1, TWL6030_BASEADD_GPADC_CTRL },
	{ SUB_CHIP_ID2, TWL6030_BASEADD_RSV },
	{ SUB_CHIP_ID2, TWL6030_BASEADD_RSV },

	{ SUB_CHIP_ID1, TWL6030_BASEADD_CHARGER },
	{ SUB_CHIP_ID1, TWL6030_BASEADD_GASGAUGE },
	{ SUB_CHIP_ID1, TWL6030_BASEADD_PWM },
	{ SUB_CHIP_ID0, TWL6030_BASEADD_ZERO },
	{ SUB_CHIP_ID1, TWL6030_BASEADD_ZERO },

	{ SUB_CHIP_ID2, TWL6030_BASEADD_ZERO },
	{ SUB_CHIP_ID2, TWL6030_BASEADD_ZERO },
	{ SUB_CHIP_ID2, TWL6030_BASEADD_RSV },
	{ SUB_CHIP_ID2, TWL6030_BASEADD_RSV },
	{ SUB_CHIP_ID2, TWL6030_BASEADD_RSV },

	{ SUB_CHIP_ID0, TWL6030_BASEADD_PM_MASTER },
	{ SUB_CHIP_ID0, TWL6030_BASEADD_PM_SLAVE_MISC },
	{ SUB_CHIP_ID0, TWL6030_BASEADD_RTC },
	{ SUB_CHIP_ID0, TWL6030_BASEADD_MEM },
	{ SUB_CHIP_ID1, TWL6025_BASEADD_CHARGER },
};

static struct regmap_config twl6030_regmap_config[3] = {
	{
		/* Address 0x48 */
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0xff,
	},
	{
		/* Address 0x49 */
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0xff,
	},
	{
		/* Address 0x4a */
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0xff,
	},
};

/*----------------------------------------------------------------------*/

/* Exported Functions */

/**
 * twl_i2c_write - Writes a n bit register in TWL4030/TWL5030/TWL60X0
 * @mod_no: module number
 * @value: an array of num_bytes+1 containing data to write
 * @reg: register address (just offset will do)
 * @num_bytes: number of bytes to transfer
 *
 * IMPORTANT: for 'value' parameter: Allocate value num_bytes+1 and
 * valid data starts at Offset 1.
 *
 * Returns the result of operation - 0 is success
 */
int twl_i2c_write(u8 mod_no, u8 *value, u8 reg, unsigned num_bytes)
{
	int ret;
	int sid;
	struct twl_client *twl;

	if (unlikely(mod_no >= TWL_MODULE_LAST)) {
		pr_err("%s: invalid module number %d\n", DRIVER_NAME, mod_no);
		return -EPERM;
	}
	if (unlikely(!inuse)) {
		pr_err("%s: not initialized\n", DRIVER_NAME);
		return -EPERM;
	}
	sid = twl_map[mod_no].sid;
	if (unlikely(sid == SUB_CHIP_ID_INVAL)) {
		pr_err("%s: module %d is not part of the pmic\n",
		       DRIVER_NAME, mod_no);
		return -EINVAL;
	}
	twl = &twl_modules[sid];

	ret = regmap_bulk_write(twl->regmap, twl_map[mod_no].base + reg,
				value, num_bytes);

	if (ret)
		pr_err("%s: Write failed (mod %d, reg 0x%02x count %d)\n",
		       DRIVER_NAME, mod_no, reg, num_bytes);

	return ret;
}
EXPORT_SYMBOL(twl_i2c_write);

/**
 * twl_i2c_read - Reads a n bit register in TWL4030/TWL5030/TWL60X0
 * @mod_no: module number
 * @value: an array of num_bytes containing data to be read
 * @reg: register address (just offset will do)
 * @num_bytes: number of bytes to transfer
 *
 * Returns result of operation - num_bytes is success else failure.
 */
int twl_i2c_read(u8 mod_no, u8 *value, u8 reg, unsigned num_bytes)
{
	int ret;
	int sid;
	struct twl_client *twl;

	if (unlikely(mod_no >= TWL_MODULE_LAST)) {
		pr_err("%s: invalid module number %d\n", DRIVER_NAME, mod_no);
		return -EPERM;
	}
	if (unlikely(!inuse)) {
		pr_err("%s: not initialized\n", DRIVER_NAME);
		return -EPERM;
	}
	sid = twl_map[mod_no].sid;
	if (unlikely(sid == SUB_CHIP_ID_INVAL)) {
		pr_err("%s: module %d is not part of the pmic\n",
		       DRIVER_NAME, mod_no);
		return -EINVAL;
	}
	twl = &twl_modules[sid];

	ret = regmap_bulk_read(twl->regmap, twl_map[mod_no].base + reg,
			       value, num_bytes);

	if (ret)
		pr_err("%s: Read failed (mod %d, reg 0x%02x count %d)\n",
		       DRIVER_NAME, mod_no, reg, num_bytes);

	return ret;
}
EXPORT_SYMBOL(twl_i2c_read);

/**
 * twl_i2c_write_u8 - Writes a 8 bit register in TWL4030/TWL5030/TWL60X0
 * @mod_no: module number
 * @value: the value to be written 8 bit
 * @reg: register address (just offset will do)
 *
 * Returns result of operation - 0 is success
 */
int twl_i2c_write_u8(u8 mod_no, u8 value, u8 reg)
{
	return twl_i2c_write(mod_no, &value, reg, 1);
}
EXPORT_SYMBOL(twl_i2c_write_u8);

/**
 * twl_i2c_read_u8 - Reads a 8 bit register from TWL4030/TWL5030/TWL60X0
 * @mod_no: module number
 * @value: the value read 8 bit
 * @reg: register address (just offset will do)
 *
 * Returns result of operation - 0 is success
 */
int twl_i2c_read_u8(u8 mod_no, u8 *value, u8 reg)
{
	return twl_i2c_read(mod_no, value, reg, 1);
}
EXPORT_SYMBOL(twl_i2c_read_u8);

/*----------------------------------------------------------------------*/

/**
 * twl_read_idcode_register - API to read the IDCODE register.
 *
 * Unlocks the IDCODE register and read the 32 bit value.
 */
static int twl_read_idcode_register(void)
{
	int err;

	err = twl_i2c_write_u8(TWL4030_MODULE_INTBR, TWL_EEPROM_R_UNLOCK,
						REG_UNLOCK_TEST_REG);
	if (err) {
		pr_err("TWL4030 Unable to unlock IDCODE registers -%d\n", err);
		goto fail;
	}

	err = twl_i2c_read(TWL4030_MODULE_INTBR, (u8 *)(&twl_idcode),
						REG_IDCODE_7_0, 4);
	if (err) {
		pr_err("TWL4030: unable to read IDCODE -%d\n", err);
		goto fail;
	}

	err = twl_i2c_write_u8(TWL4030_MODULE_INTBR, 0x0, REG_UNLOCK_TEST_REG);
	if (err)
		pr_err("TWL4030 Unable to relock IDCODE registers -%d\n", err);
fail:
	return err;
}

/**
 * twl_get_type - API to get TWL Si type.
 *
 * Api to get the TWL Si type from IDCODE value.
 */
int twl_get_type(void)
{
	return TWL_SIL_TYPE(twl_idcode);
}
EXPORT_SYMBOL_GPL(twl_get_type);

/**
 * twl_get_version - API to get TWL Si version.
 *
 * Api to get the TWL Si version from IDCODE value.
 */
int twl_get_version(void)
{
	return TWL_SIL_REV(twl_idcode);
}
EXPORT_SYMBOL_GPL(twl_get_version);

/**
 * twl_get_hfclk_rate - API to get TWL external HFCLK clock rate.
 *
 * Api to get the TWL HFCLK rate based on BOOT_CFG register.
 */
int twl_get_hfclk_rate(void)
{
	u8 ctrl;
	int rate;

	twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &ctrl, R_CFG_BOOT);

	switch (ctrl & 0x3) {
	case HFCLK_FREQ_19p2_MHZ:
		rate = 19200000;
		break;
	case HFCLK_FREQ_26_MHZ:
		rate = 26000000;
		break;
	case HFCLK_FREQ_38p4_MHZ:
		rate = 38400000;
		break;
	default:
		pr_err("TWL4030: HFCLK is not configured\n");
		rate = -EINVAL;
		break;
	}

	return rate;
}
EXPORT_SYMBOL_GPL(twl_get_hfclk_rate);

static struct device *
add_numbered_child(unsigned chip, const char *name, int num,
		void *pdata, unsigned pdata_len,
		bool can_wakeup, int irq0, int irq1)
{
	struct platform_device	*pdev;
	struct twl_client	*twl = &twl_modules[chip];
	int			status;

	pdev = platform_device_alloc(name, num);
	if (!pdev) {
		dev_dbg(&twl->client->dev, "can't alloc dev\n");
		status = -ENOMEM;
		goto err;
	}

	pdev->dev.parent = &twl->client->dev;

	if (pdata) {
		status = platform_device_add_data(pdev, pdata, pdata_len);
		if (status < 0) {
			dev_dbg(&pdev->dev, "can't add platform_data\n");
			goto err;
		}
	}

	if (irq0) {
		struct resource r[2] = {
			{ .start = irq0, .flags = IORESOURCE_IRQ, },
			{ .start = irq1, .flags = IORESOURCE_IRQ, },
		};

		status = platform_device_add_resources(pdev, r, irq1 ? 2 : 1);
		if (status < 0) {
			dev_dbg(&pdev->dev, "can't add irqs\n");
			goto err;
		}
	}

	status = platform_device_add(pdev);
	if (status == 0)
		device_init_wakeup(&pdev->dev, can_wakeup);

err:
	if (status < 0) {
		platform_device_put(pdev);
		dev_err(&twl->client->dev, "can't add %s dev\n", name);
		return ERR_PTR(status);
	}
	return &pdev->dev;
}

static inline struct device *add_child(unsigned chip, const char *name,
		void *pdata, unsigned pdata_len,
		bool can_wakeup, int irq0, int irq1)
{
	return add_numbered_child(chip, name, -1, pdata, pdata_len,
		can_wakeup, irq0, irq1);
}

static struct device *
add_regulator_linked(int num, struct regulator_init_data *pdata,
		struct regulator_consumer_supply *consumers,
		unsigned num_consumers, unsigned long features)
{
	unsigned sub_chip_id;
	struct twl_regulator_driver_data drv_data;

	/* regulator framework demands init_data ... */
	if (!pdata)
		return NULL;

	if (consumers) {
		pdata->consumer_supplies = consumers;
		pdata->num_consumer_supplies = num_consumers;
	}

	if (pdata->driver_data) {
		/* If we have existing drv_data, just add the flags */
		struct twl_regulator_driver_data *tmp;
		tmp = pdata->driver_data;
		tmp->features |= features;
	} else {
		/* add new driver data struct, used only during init */
		drv_data.features = features;
		drv_data.set_voltage = NULL;
		drv_data.get_voltage = NULL;
		drv_data.data = NULL;
		pdata->driver_data = &drv_data;
	}

	/* NOTE:  we currently ignore regulator IRQs, e.g. for short circuits */
	sub_chip_id = twl_map[TWL_MODULE_PM_MASTER].sid;
	return add_numbered_child(sub_chip_id, "twl_reg", num,
		pdata, sizeof(*pdata), false, 0, 0);
}

static struct device *
add_regulator(int num, struct regulator_init_data *pdata,
		unsigned long features)
{
	return add_regulator_linked(num, pdata, NULL, 0, features);
}

/*
 * NOTE:  We know the first 8 IRQs after pdata->base_irq are
 * for the PIH, and the next are for the PWR_INT SIH, since
 * that's how twl_init_irq() sets things up.
 */

static int
add_children(struct twl4030_platform_data *pdata, unsigned irq_base,
		unsigned long features)
{
	struct device	*child;
	unsigned sub_chip_id;

	if (IS_ENABLED(CONFIG_GPIO_TWL4030) && pdata->gpio) {
		child = add_child(SUB_CHIP_ID1, "twl4030_gpio",
				pdata->gpio, sizeof(*pdata->gpio),
				false, irq_base + GPIO_INTR_OFFSET, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_KEYBOARD_TWL4030) && pdata->keypad) {
		child = add_child(SUB_CHIP_ID2, "twl4030_keypad",
				pdata->keypad, sizeof(*pdata->keypad),
				true, irq_base + KEYPAD_INTR_OFFSET, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_TWL4030_MADC) && pdata->madc &&
	    twl_class_is_4030()) {
		child = add_child(SUB_CHIP_ID2, "twl4030_madc",
				pdata->madc, sizeof(*pdata->madc),
				true, irq_base + MADC_INTR_OFFSET, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_RTC_DRV_TWL4030)) {
		/*
		 * REVISIT platform_data here currently might expose the
		 * "msecure" line ... but for now we just expect board
		 * setup to tell the chip "it's always ok to SET_TIME".
		 * Eventually, Linux might become more aware of such
		 * HW security concerns, and "least privilege".
		 */
		sub_chip_id = twl_map[TWL_MODULE_RTC].sid;
		child = add_child(sub_chip_id, "twl_rtc", NULL, 0,
				true, irq_base + RTC_INTR_OFFSET, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_PWM_TWL)) {
		child = add_child(SUB_CHIP_ID1, "twl-pwm", NULL, 0,
				  false, 0, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_PWM_TWL_LED)) {
		child = add_child(SUB_CHIP_ID1, "twl-pwmled", NULL, 0,
				  false, 0, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_TWL4030_USB) && pdata->usb &&
	    twl_class_is_4030()) {

		static struct regulator_consumer_supply usb1v5 = {
			.supply =	"usb1v5",
		};
		static struct regulator_consumer_supply usb1v8 = {
			.supply =	"usb1v8",
		};
		static struct regulator_consumer_supply usb3v1[] = {
			{ .supply =	"usb3v1" },
			{ .supply =	"bci3v1" },
		};

	/* First add the regulators so that they can be used by transceiver */
		if (IS_ENABLED(CONFIG_REGULATOR_TWL4030)) {
			/* this is a template that gets copied */
			struct regulator_init_data usb_fixed = {
				.constraints.valid_modes_mask =
					REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
				.constraints.valid_ops_mask =
					REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
			};

			child = add_regulator_linked(TWL4030_REG_VUSB1V5,
						      &usb_fixed, &usb1v5, 1,
						      features);
			if (IS_ERR(child))
				return PTR_ERR(child);

			child = add_regulator_linked(TWL4030_REG_VUSB1V8,
						      &usb_fixed, &usb1v8, 1,
						      features);
			if (IS_ERR(child))
				return PTR_ERR(child);

			child = add_regulator_linked(TWL4030_REG_VUSB3V1,
						      &usb_fixed, usb3v1, 2,
						      features);
			if (IS_ERR(child))
				return PTR_ERR(child);

		}

		child = add_child(SUB_CHIP_ID0, "twl4030_usb",
				pdata->usb, sizeof(*pdata->usb), true,
				/* irq0 = USB_PRES, irq1 = USB */
				irq_base + USB_PRES_INTR_OFFSET,
				irq_base + USB_INTR_OFFSET);

		if (IS_ERR(child))
			return PTR_ERR(child);

		/* we need to connect regulators to this transceiver */
		if (IS_ENABLED(CONFIG_REGULATOR_TWL4030) && child) {
			usb1v5.dev_name = dev_name(child);
			usb1v8.dev_name = dev_name(child);
			usb3v1[0].dev_name = dev_name(child);
		}
	}
	if (IS_ENABLED(CONFIG_TWL6030_USB) && pdata->usb &&
	    twl_class_is_6030()) {

		static struct regulator_consumer_supply usb3v3;
		int regulator;

		if (IS_ENABLED(CONFIG_REGULATOR_TWL4030)) {
			/* this is a template that gets copied */
			struct regulator_init_data usb_fixed = {
				.constraints.valid_modes_mask =
					REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
				.constraints.valid_ops_mask =
					REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
			};

			if (features & TWL6025_SUBCLASS) {
				usb3v3.supply =	"ldousb";
				regulator = TWL6025_REG_LDOUSB;
			} else {
				usb3v3.supply = "vusb";
				regulator = TWL6030_REG_VUSB;
			}
			child = add_regulator_linked(regulator, &usb_fixed,
							&usb3v3, 1,
							features);
			if (IS_ERR(child))
				return PTR_ERR(child);
		}

		pdata->usb->features = features;

		child = add_child(SUB_CHIP_ID0, "twl6030_usb",
			pdata->usb, sizeof(*pdata->usb), true,
			/* irq1 = VBUS_PRES, irq0 = USB ID */
			irq_base + USBOTG_INTR_OFFSET,
			irq_base + USB_PRES_INTR_OFFSET);

		if (IS_ERR(child))
			return PTR_ERR(child);
		/* we need to connect regulators to this transceiver */
		if (IS_ENABLED(CONFIG_REGULATOR_TWL4030) && child)
			usb3v3.dev_name = dev_name(child);
	} else if (IS_ENABLED(CONFIG_REGULATOR_TWL4030) &&
		   twl_class_is_6030()) {
		if (features & TWL6025_SUBCLASS)
			child = add_regulator(TWL6025_REG_LDOUSB,
						pdata->ldousb, features);
		else
			child = add_regulator(TWL6030_REG_VUSB,
						pdata->vusb, features);

			if (IS_ERR(child))
					return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_TWL4030_WATCHDOG) && twl_class_is_4030()) {
		child = add_child(SUB_CHIP_ID3, "twl4030_wdt", NULL, 0,
				  false, 0, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_INPUT_TWL4030_PWRBUTTON) && twl_class_is_4030()) {
		child = add_child(SUB_CHIP_ID3, "twl4030_pwrbutton", NULL, 0,
				  true, irq_base + 8 + 0, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_MFD_TWL4030_AUDIO) && pdata->audio &&
	    twl_class_is_4030()) {
		child = add_child(SUB_CHIP_ID1, "twl4030-audio",
				pdata->audio, sizeof(*pdata->audio),
				false, 0, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	/* twl4030 regulators */
	if (IS_ENABLED(CONFIG_REGULATOR_TWL4030) && twl_class_is_4030()) {
		child = add_regulator(TWL4030_REG_VPLL1, pdata->vpll1,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VIO, pdata->vio,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VDD1, pdata->vdd1,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VDD2, pdata->vdd2,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VMMC1, pdata->vmmc1,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VDAC, pdata->vdac,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator((features & TWL4030_VAUX2)
					? TWL4030_REG_VAUX2_4030
					: TWL4030_REG_VAUX2,
				pdata->vaux2, features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VINTANA1, pdata->vintana1,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VINTANA2, pdata->vintana2,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VINTDIG, pdata->vintdig,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	/* maybe add LDOs that are omitted on cost-reduced parts */
	if (IS_ENABLED(CONFIG_REGULATOR_TWL4030) && !(features & TPS_SUBSET)
	  && twl_class_is_4030()) {
		child = add_regulator(TWL4030_REG_VPLL2, pdata->vpll2,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VMMC2, pdata->vmmc2,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VSIM, pdata->vsim,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VAUX1, pdata->vaux1,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VAUX3, pdata->vaux3,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VAUX4, pdata->vaux4,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	/* twl6030 regulators */
	if (IS_ENABLED(CONFIG_REGULATOR_TWL4030) && twl_class_is_6030() &&
			!(features & TWL6025_SUBCLASS)) {
		child = add_regulator(TWL6030_REG_VDD1, pdata->vdd1,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_VDD2, pdata->vdd2,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_VDD3, pdata->vdd3,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_V1V8, pdata->v1v8,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_V2V1, pdata->v2v1,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_VMMC, pdata->vmmc,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_VPP, pdata->vpp,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_VUSIM, pdata->vusim,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_VCXIO, pdata->vcxio,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_VDAC, pdata->vdac,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_VAUX1_6030, pdata->vaux1,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_VAUX2_6030, pdata->vaux2,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_VAUX3_6030, pdata->vaux3,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6030_REG_CLK32KG, pdata->clk32kg,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	/* 6030 and 6025 share this regulator */
	if (IS_ENABLED(CONFIG_REGULATOR_TWL4030) && twl_class_is_6030()) {
		child = add_regulator(TWL6030_REG_VANA, pdata->vana,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	/* twl6025 regulators */
	if (IS_ENABLED(CONFIG_REGULATOR_TWL4030) && twl_class_is_6030() &&
			(features & TWL6025_SUBCLASS)) {
		child = add_regulator(TWL6025_REG_LDO5, pdata->ldo5,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6025_REG_LDO1, pdata->ldo1,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6025_REG_LDO7, pdata->ldo7,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6025_REG_LDO6, pdata->ldo6,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6025_REG_LDOLN, pdata->ldoln,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6025_REG_LDO2, pdata->ldo2,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6025_REG_LDO4, pdata->ldo4,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6025_REG_LDO3, pdata->ldo3,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6025_REG_SMPS3, pdata->smps3,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6025_REG_SMPS4, pdata->smps4,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL6025_REG_VIO, pdata->vio6025,
					features);
		if (IS_ERR(child))
			return PTR_ERR(child);

	}

	if (IS_ENABLED(CONFIG_CHARGER_TWL4030) && pdata->bci &&
			!(features & (TPS_SUBSET | TWL5031))) {
		child = add_child(SUB_CHIP_ID3, "twl4030_bci",
				pdata->bci, sizeof(*pdata->bci), false,
				/* irq0 = CHG_PRES, irq1 = BCI */
				irq_base + BCI_PRES_INTR_OFFSET,
				irq_base + BCI_INTR_OFFSET);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	return 0;
}

/*----------------------------------------------------------------------*/

/*
 * These three functions initialize the on-chip clock framework,
 * letting it generate the right frequencies for USB, MADC, and
 * other purposes.
 */
static inline int __init protect_pm_master(void)
{
	int e = 0;

	e = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, 0,
			     TWL4030_PM_MASTER_PROTECT_KEY);
	return e;
}

static inline int __init unprotect_pm_master(void)
{
	int e = 0;

	e |= twl_i2c_write_u8(TWL_MODULE_PM_MASTER, TWL4030_PM_MASTER_KEY_CFG1,
			      TWL4030_PM_MASTER_PROTECT_KEY);
	e |= twl_i2c_write_u8(TWL_MODULE_PM_MASTER, TWL4030_PM_MASTER_KEY_CFG2,
			      TWL4030_PM_MASTER_PROTECT_KEY);

	return e;
}

static void clocks_init(struct device *dev,
			struct twl4030_clock_init_data *clock)
{
	int e = 0;
	struct clk *osc;
	u32 rate;
	u8 ctrl = HFCLK_FREQ_26_MHZ;

	osc = clk_get(dev, "fck");
	if (IS_ERR(osc)) {
		printk(KERN_WARNING "Skipping twl internal clock init and "
				"using bootloader value (unknown osc rate)\n");
		return;
	}

	rate = clk_get_rate(osc);
	clk_put(osc);

	switch (rate) {
	case 19200000:
		ctrl = HFCLK_FREQ_19p2_MHZ;
		break;
	case 26000000:
		ctrl = HFCLK_FREQ_26_MHZ;
		break;
	case 38400000:
		ctrl = HFCLK_FREQ_38p4_MHZ;
		break;
	}

	ctrl |= HIGH_PERF_SQ;
	if (clock && clock->ck32k_lowpwr_enable)
		ctrl |= CK32K_LOWPWR_EN;

	e |= unprotect_pm_master();
	/* effect->MADC+USB ck en */
	e |= twl_i2c_write_u8(TWL_MODULE_PM_MASTER, ctrl, R_CFG_BOOT);
	e |= protect_pm_master();

	if (e < 0)
		pr_err("%s: clock init err [%d]\n", DRIVER_NAME, e);
}

/*----------------------------------------------------------------------*/


static int twl_remove(struct i2c_client *client)
{
	unsigned i, num_slaves;
	int status;

	if (twl_class_is_4030()) {
		status = twl4030_exit_irq();
		num_slaves = TWL_NUM_SLAVES;
	} else {
		status = twl6030_exit_irq();
		num_slaves = TWL_NUM_SLAVES - 1;
	}

	if (status < 0)
		return status;

	for (i = 0; i < num_slaves; i++) {
		struct twl_client	*twl = &twl_modules[i];

		if (twl->client && twl->client != client)
			i2c_unregister_device(twl->client);
		twl_modules[i].client = NULL;
	}
	inuse = false;
	return 0;
}

/* NOTE: This driver only handles a single twl4030/tps659x0 chip */
static int
twl_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct twl4030_platform_data	*pdata = client->dev.platform_data;
	struct device_node		*node = client->dev.of_node;
	struct platform_device		*pdev;
	struct regmap_config		*twl_regmap_config;
	int				irq_base = 0;
	int				status;
	unsigned			i, num_slaves;

	pdev = platform_device_alloc(DRIVER_NAME, -1);
	if (!pdev) {
		dev_err(&client->dev, "can't alloc pdev\n");
		return -ENOMEM;
	}

	status = platform_device_add(pdev);
	if (status) {
		platform_device_put(pdev);
		return status;
	}

	if (node && !pdata) {
		/*
		 * XXX: Temporary pdata until the information is correctly
		 * retrieved by every TWL modules from DT.
		 */
		pdata = devm_kzalloc(&client->dev,
				     sizeof(struct twl4030_platform_data),
				     GFP_KERNEL);
		if (!pdata) {
			status = -ENOMEM;
			goto free;
		}
	}

	if (!pdata) {
		dev_dbg(&client->dev, "no platform data?\n");
		status = -EINVAL;
		goto free;
	}

	platform_set_drvdata(pdev, pdata);

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C) == 0) {
		dev_dbg(&client->dev, "can't talk I2C?\n");
		status = -EIO;
		goto free;
	}

	if (inuse) {
		dev_dbg(&client->dev, "driver is already in use\n");
		status = -EBUSY;
		goto free;
	}

	if ((id->driver_data) & TWL6030_CLASS) {
		twl_id = TWL6030_CLASS_ID;
		twl_map = &twl6030_map[0];
		twl_regmap_config = twl6030_regmap_config;
		num_slaves = TWL_NUM_SLAVES - 1;
	} else {
		twl_id = TWL4030_CLASS_ID;
		twl_map = &twl4030_map[0];
		twl_regmap_config = twl4030_regmap_config;
		num_slaves = TWL_NUM_SLAVES;
	}

	for (i = 0; i < num_slaves; i++) {
		struct twl_client *twl = &twl_modules[i];

		if (i == 0) {
			twl->client = client;
		} else {
			twl->client = i2c_new_dummy(client->adapter,
						    client->addr + i);
			if (!twl->client) {
				dev_err(&client->dev,
					"can't attach client %d\n", i);
				status = -ENOMEM;
				goto fail;
			}
		}

		twl->regmap = devm_regmap_init_i2c(twl->client,
						   &twl_regmap_config[i]);
		if (IS_ERR(twl->regmap)) {
			status = PTR_ERR(twl->regmap);
			dev_err(&client->dev,
				"Failed to allocate regmap %d, err: %d\n", i,
				status);
			goto fail;
		}
	}

	inuse = true;

	/* setup clock framework */
	clocks_init(&pdev->dev, pdata->clock);

	/* read TWL IDCODE Register */
	if (twl_id == TWL4030_CLASS_ID) {
		status = twl_read_idcode_register();
		WARN(status < 0, "Error: reading twl_idcode register value\n");
	}

	/* load power event scripts */
	if (IS_ENABLED(CONFIG_TWL4030_POWER) && pdata->power)
		twl4030_power_init(pdata->power);

	/* Maybe init the T2 Interrupt subsystem */
	if (client->irq) {
		if (twl_class_is_4030()) {
			twl4030_init_chip_irq(id->name);
			irq_base = twl4030_init_irq(&client->dev, client->irq);
		} else {
			irq_base = twl6030_init_irq(&client->dev, client->irq);
		}

		if (irq_base < 0) {
			status = irq_base;
			goto fail;
		}
	}

	/*
	 * Disable TWL4030/TWL5030 I2C Pull-up on I2C1 and I2C4(SR) interface.
	 * Program I2C_SCL_CTRL_PU(bit 0)=0, I2C_SDA_CTRL_PU (bit 2)=0,
	 * SR_I2C_SCL_CTRL_PU(bit 4)=0 and SR_I2C_SDA_CTRL_PU(bit 6)=0.
	 */
	if (twl_class_is_4030()) {
		u8 temp;

		twl_i2c_read_u8(TWL4030_MODULE_INTBR, &temp, REG_GPPUPDCTR1);
		temp &= ~(SR_I2C_SDA_CTRL_PU | SR_I2C_SCL_CTRL_PU | \
			I2C_SDA_CTRL_PU | I2C_SCL_CTRL_PU);
		twl_i2c_write_u8(TWL4030_MODULE_INTBR, temp, REG_GPPUPDCTR1);
	}

	status = -ENODEV;
	if (node)
		status = of_platform_populate(node, NULL, NULL, &client->dev);
	if (status)
		status = add_children(pdata, irq_base, id->driver_data);

fail:
	if (status < 0)
		twl_remove(client);
free:
	if (status < 0)
		platform_device_unregister(pdev);

	return status;
}

static const struct i2c_device_id twl_ids[] = {
	{ "twl4030", TWL4030_VAUX2 },	/* "Triton 2" */
	{ "twl5030", 0 },		/* T2 updated */
	{ "twl5031", TWL5031 },		/* TWL5030 updated */
	{ "tps65950", 0 },		/* catalog version of twl5030 */
	{ "tps65930", TPS_SUBSET },	/* fewer LDOs and DACs; no charger */
	{ "tps65920", TPS_SUBSET },	/* fewer LDOs; no codec or charger */
	{ "tps65921", TPS_SUBSET },	/* fewer LDOs; no codec, no LED
					   and vibrator. Charger in USB module*/
	{ "twl6030", TWL6030_CLASS },	/* "Phoenix power chip" */
	{ "twl6025", TWL6030_CLASS | TWL6025_SUBCLASS }, /* "Phoenix lite" */
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(i2c, twl_ids);

/* One Client Driver , 4 Clients */
static struct i2c_driver twl_driver = {
	.driver.name	= DRIVER_NAME,
	.id_table	= twl_ids,
	.probe		= twl_probe,
	.remove		= twl_remove,
};

static int __init twl_init(void)
{
	return i2c_add_driver(&twl_driver);
}
subsys_initcall(twl_init);

static void __exit twl_exit(void)
{
	i2c_del_driver(&twl_driver);
}
module_exit(twl_exit);

MODULE_AUTHOR("Texas Instruments, Inc.");
MODULE_DESCRIPTION("I2C Core interface for TWL");
MODULE_LICENSE("GPL");
