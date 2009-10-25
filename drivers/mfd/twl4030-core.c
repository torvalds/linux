/*
 * twl4030_core.c - driver for TWL4030/TPS659x0 PM and audio CODEC devices
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
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <linux/regulator/machine.h>

#include <linux/i2c.h>
#include <linux/i2c/twl4030.h>

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
#include <mach/cpu.h>
#endif

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

#define DRIVER_NAME			"twl4030"

#if defined(CONFIG_TWL4030_BCI_BATTERY) || \
	defined(CONFIG_TWL4030_BCI_BATTERY_MODULE)
#define twl_has_bci()		true
#else
#define twl_has_bci()		false
#endif

#if defined(CONFIG_KEYBOARD_TWL4030) || defined(CONFIG_KEYBOARD_TWL4030_MODULE)
#define twl_has_keypad()	true
#else
#define twl_has_keypad()	false
#endif

#if defined(CONFIG_GPIO_TWL4030) || defined(CONFIG_GPIO_TWL4030_MODULE)
#define twl_has_gpio()	true
#else
#define twl_has_gpio()	false
#endif

#if defined(CONFIG_REGULATOR_TWL4030) \
	|| defined(CONFIG_REGULATOR_TWL4030_MODULE)
#define twl_has_regulator()	true
#else
#define twl_has_regulator()	false
#endif

#if defined(CONFIG_TWL4030_MADC) || defined(CONFIG_TWL4030_MADC_MODULE)
#define twl_has_madc()	true
#else
#define twl_has_madc()	false
#endif

#ifdef CONFIG_TWL4030_POWER
#define twl_has_power()        true
#else
#define twl_has_power()        false
#endif

#if defined(CONFIG_RTC_DRV_TWL4030) || defined(CONFIG_RTC_DRV_TWL4030_MODULE)
#define twl_has_rtc()	true
#else
#define twl_has_rtc()	false
#endif

#if defined(CONFIG_TWL4030_USB) || defined(CONFIG_TWL4030_USB_MODULE)
#define twl_has_usb()	true
#else
#define twl_has_usb()	false
#endif

#if defined(CONFIG_TWL4030_WATCHDOG) || \
	defined(CONFIG_TWL4030_WATCHDOG_MODULE)
#define twl_has_watchdog()        true
#else
#define twl_has_watchdog()        false
#endif

/* Triton Core internal information (BEGIN) */

/* Last - for index max*/
#define TWL4030_MODULE_LAST		TWL4030_MODULE_SECURED_REG

#define TWL4030_NUM_SLAVES		4

#if defined(CONFIG_INPUT_TWL4030_PWRBUTTON) \
	|| defined(CONFIG_INPUT_TWL4030_PWBUTTON_MODULE)
#define twl_has_pwrbutton()	true
#else
#define twl_has_pwrbutton()	false
#endif

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

/* subchip/slave 3 - POWER ID */
#define TWL4030_BASEADD_BACKUP		0x0014
#define TWL4030_BASEADD_INT		0x002E
#define TWL4030_BASEADD_PM_MASTER	0x0036
#define TWL4030_BASEADD_PM_RECEIVER	0x005B
#define TWL4030_BASEADD_RTC		0x001C
#define TWL4030_BASEADD_SECURED_REG	0x0000

/* Triton Core internal information (END) */


/* Few power values */
#define R_CFG_BOOT			0x05
#define R_PROTECT_KEY			0x0E

/* access control values for R_PROTECT_KEY */
#define KEY_UNLOCK1			0xce
#define KEY_UNLOCK2			0xec
#define KEY_LOCK			0x00

/* some fields in R_CFG_BOOT */
#define HFCLK_FREQ_19p2_MHZ		(1 << 0)
#define HFCLK_FREQ_26_MHZ		(2 << 0)
#define HFCLK_FREQ_38p4_MHZ		(3 << 0)
#define HIGH_PERF_SQ			(1 << 3)


/* chip-specific feature flags, for i2c_device_id.driver_data */
#define TWL4030_VAUX2		BIT(0)	/* pre-5030 voltage ranges */
#define TPS_SUBSET		BIT(1)	/* tps659[23]0 have fewer LDOs */

/*----------------------------------------------------------------------*/

/* is driver active, bound to a chip? */
static bool inuse;

/* Structure for each TWL4030 Slave */
struct twl4030_client {
	struct i2c_client *client;
	u8 address;

	/* max numb of i2c_msg required is for read =2 */
	struct i2c_msg xfer_msg[2];

	/* To lock access to xfer_msg */
	struct mutex xfer_lock;
};

static struct twl4030_client twl4030_modules[TWL4030_NUM_SLAVES];


/* mapping the module id to slave id and base address */
struct twl4030mapping {
	unsigned char sid;	/* Slave ID */
	unsigned char base;	/* base address */
};

static struct twl4030mapping twl4030_map[TWL4030_MODULE_LAST + 1] = {
	/*
	 * NOTE:  don't change this table without updating the
	 * <linux/i2c/twl4030.h> defines for TWL4030_MODULE_*
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

	{ 3, TWL4030_BASEADD_BACKUP },
	{ 3, TWL4030_BASEADD_INT },
	{ 3, TWL4030_BASEADD_PM_MASTER },
	{ 3, TWL4030_BASEADD_PM_RECEIVER },
	{ 3, TWL4030_BASEADD_RTC },
	{ 3, TWL4030_BASEADD_SECURED_REG },
};

/*----------------------------------------------------------------------*/

/* Exported Functions */

/**
 * twl4030_i2c_write - Writes a n bit register in TWL4030
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
int twl4030_i2c_write(u8 mod_no, u8 *value, u8 reg, unsigned num_bytes)
{
	int ret;
	int sid;
	struct twl4030_client *twl;
	struct i2c_msg *msg;

	if (unlikely(mod_no > TWL4030_MODULE_LAST)) {
		pr_err("%s: invalid module number %d\n", DRIVER_NAME, mod_no);
		return -EPERM;
	}
	sid = twl4030_map[mod_no].sid;
	twl = &twl4030_modules[sid];

	if (unlikely(!inuse)) {
		pr_err("%s: client %d is not initialized\n", DRIVER_NAME, sid);
		return -EPERM;
	}
	mutex_lock(&twl->xfer_lock);
	/*
	 * [MSG1]: fill the register address data
	 * fill the data Tx buffer
	 */
	msg = &twl->xfer_msg[0];
	msg->addr = twl->address;
	msg->len = num_bytes + 1;
	msg->flags = 0;
	msg->buf = value;
	/* over write the first byte of buffer with the register address */
	*value = twl4030_map[mod_no].base + reg;
	ret = i2c_transfer(twl->client->adapter, twl->xfer_msg, 1);
	mutex_unlock(&twl->xfer_lock);

	/* i2cTransfer returns num messages.translate it pls.. */
	if (ret >= 0)
		ret = 0;
	return ret;
}
EXPORT_SYMBOL(twl4030_i2c_write);

/**
 * twl4030_i2c_read - Reads a n bit register in TWL4030
 * @mod_no: module number
 * @value: an array of num_bytes containing data to be read
 * @reg: register address (just offset will do)
 * @num_bytes: number of bytes to transfer
 *
 * Returns result of operation - num_bytes is success else failure.
 */
int twl4030_i2c_read(u8 mod_no, u8 *value, u8 reg, unsigned num_bytes)
{
	int ret;
	u8 val;
	int sid;
	struct twl4030_client *twl;
	struct i2c_msg *msg;

	if (unlikely(mod_no > TWL4030_MODULE_LAST)) {
		pr_err("%s: invalid module number %d\n", DRIVER_NAME, mod_no);
		return -EPERM;
	}
	sid = twl4030_map[mod_no].sid;
	twl = &twl4030_modules[sid];

	if (unlikely(!inuse)) {
		pr_err("%s: client %d is not initialized\n", DRIVER_NAME, sid);
		return -EPERM;
	}
	mutex_lock(&twl->xfer_lock);
	/* [MSG1] fill the register address data */
	msg = &twl->xfer_msg[0];
	msg->addr = twl->address;
	msg->len = 1;
	msg->flags = 0;	/* Read the register value */
	val = twl4030_map[mod_no].base + reg;
	msg->buf = &val;
	/* [MSG2] fill the data rx buffer */
	msg = &twl->xfer_msg[1];
	msg->addr = twl->address;
	msg->flags = I2C_M_RD;	/* Read the register value */
	msg->len = num_bytes;	/* only n bytes */
	msg->buf = value;
	ret = i2c_transfer(twl->client->adapter, twl->xfer_msg, 2);
	mutex_unlock(&twl->xfer_lock);

	/* i2cTransfer returns num messages.translate it pls.. */
	if (ret >= 0)
		ret = 0;
	return ret;
}
EXPORT_SYMBOL(twl4030_i2c_read);

/**
 * twl4030_i2c_write_u8 - Writes a 8 bit register in TWL4030
 * @mod_no: module number
 * @value: the value to be written 8 bit
 * @reg: register address (just offset will do)
 *
 * Returns result of operation - 0 is success
 */
int twl4030_i2c_write_u8(u8 mod_no, u8 value, u8 reg)
{

	/* 2 bytes offset 1 contains the data offset 0 is used by i2c_write */
	u8 temp_buffer[2] = { 0 };
	/* offset 1 contains the data */
	temp_buffer[1] = value;
	return twl4030_i2c_write(mod_no, temp_buffer, reg, 1);
}
EXPORT_SYMBOL(twl4030_i2c_write_u8);

/**
 * twl4030_i2c_read_u8 - Reads a 8 bit register from TWL4030
 * @mod_no: module number
 * @value: the value read 8 bit
 * @reg: register address (just offset will do)
 *
 * Returns result of operation - 0 is success
 */
int twl4030_i2c_read_u8(u8 mod_no, u8 *value, u8 reg)
{
	return twl4030_i2c_read(mod_no, value, reg, 1);
}
EXPORT_SYMBOL(twl4030_i2c_read_u8);

/*----------------------------------------------------------------------*/

static struct device *
add_numbered_child(unsigned chip, const char *name, int num,
		void *pdata, unsigned pdata_len,
		bool can_wakeup, int irq0, int irq1)
{
	struct platform_device	*pdev;
	struct twl4030_client	*twl = &twl4030_modules[chip];
	int			status;

	pdev = platform_device_alloc(name, num);
	if (!pdev) {
		dev_dbg(&twl->client->dev, "can't alloc dev\n");
		status = -ENOMEM;
		goto err;
	}

	device_init_wakeup(&pdev->dev, can_wakeup);
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
		unsigned num_consumers)
{
	/* regulator framework demands init_data ... */
	if (!pdata)
		return NULL;

	if (consumers) {
		pdata->consumer_supplies = consumers;
		pdata->num_consumer_supplies = num_consumers;
	}

	/* NOTE:  we currently ignore regulator IRQs, e.g. for short circuits */
	return add_numbered_child(3, "twl4030_reg", num,
		pdata, sizeof(*pdata), false, 0, 0);
}

static struct device *
add_regulator(int num, struct regulator_init_data *pdata)
{
	return add_regulator_linked(num, pdata, NULL, 0);
}

/*
 * NOTE:  We know the first 8 IRQs after pdata->base_irq are
 * for the PIH, and the next are for the PWR_INT SIH, since
 * that's how twl_init_irq() sets things up.
 */

static int
add_children(struct twl4030_platform_data *pdata, unsigned long features)
{
	struct device	*child;

	if (twl_has_bci() && pdata->bci && !(features & TPS_SUBSET)) {
		child = add_child(3, "twl4030_bci",
				pdata->bci, sizeof(*pdata->bci),
				false,
				/* irq0 = CHG_PRES, irq1 = BCI */
				pdata->irq_base + 8 + 1, pdata->irq_base + 2);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (twl_has_gpio() && pdata->gpio) {
		child = add_child(1, "twl4030_gpio",
				pdata->gpio, sizeof(*pdata->gpio),
				false, pdata->irq_base + 0, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (twl_has_keypad() && pdata->keypad) {
		child = add_child(2, "twl4030_keypad",
				pdata->keypad, sizeof(*pdata->keypad),
				true, pdata->irq_base + 1, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (twl_has_madc() && pdata->madc) {
		child = add_child(2, "twl4030_madc",
				pdata->madc, sizeof(*pdata->madc),
				true, pdata->irq_base + 3, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (twl_has_rtc()) {
		/*
		 * REVISIT platform_data here currently might expose the
		 * "msecure" line ... but for now we just expect board
		 * setup to tell the chip "it's always ok to SET_TIME".
		 * Eventually, Linux might become more aware of such
		 * HW security concerns, and "least privilege".
		 */
		child = add_child(3, "twl4030_rtc",
				NULL, 0,
				true, pdata->irq_base + 8 + 3, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (twl_has_usb() && pdata->usb) {

		static struct regulator_consumer_supply usb1v5 = {
			.supply =	"usb1v5",
		};
		static struct regulator_consumer_supply usb1v8 = {
			.supply =	"usb1v8",
		};
		static struct regulator_consumer_supply usb3v1 = {
			.supply =	"usb3v1",
		};

	/* First add the regulators so that they can be used by transceiver */
		if (twl_has_regulator()) {
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
						      &usb_fixed, &usb1v5, 1);
			if (IS_ERR(child))
				return PTR_ERR(child);

			child = add_regulator_linked(TWL4030_REG_VUSB1V8,
						      &usb_fixed, &usb1v8, 1);
			if (IS_ERR(child))
				return PTR_ERR(child);

			child = add_regulator_linked(TWL4030_REG_VUSB3V1,
						      &usb_fixed, &usb3v1, 1);
			if (IS_ERR(child))
				return PTR_ERR(child);

		}

		child = add_child(0, "twl4030_usb",
				pdata->usb, sizeof(*pdata->usb),
				true,
				/* irq0 = USB_PRES, irq1 = USB */
				pdata->irq_base + 8 + 2, pdata->irq_base + 4);

		if (IS_ERR(child))
			return PTR_ERR(child);

		/* we need to connect regulators to this transceiver */
		if (twl_has_regulator() && child) {
			usb1v5.dev = child;
			usb1v8.dev = child;
			usb3v1.dev = child;
		}
	}

	if (twl_has_watchdog()) {
		child = add_child(0, "twl4030_wdt", NULL, 0, false, 0, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (twl_has_pwrbutton()) {
		child = add_child(1, "twl4030_pwrbutton",
				NULL, 0, true, pdata->irq_base + 8 + 0, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (twl_has_regulator()) {
		/*
		child = add_regulator(TWL4030_REG_VPLL1, pdata->vpll1);
		if (IS_ERR(child))
			return PTR_ERR(child);
		*/

		child = add_regulator(TWL4030_REG_VMMC1, pdata->vmmc1);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VDAC, pdata->vdac);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator((features & TWL4030_VAUX2)
					? TWL4030_REG_VAUX2_4030
					: TWL4030_REG_VAUX2,
				pdata->vaux2);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	/* maybe add LDOs that are omitted on cost-reduced parts */
	if (twl_has_regulator() && !(features & TPS_SUBSET)) {
		child = add_regulator(TWL4030_REG_VPLL2, pdata->vpll2);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VMMC2, pdata->vmmc2);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VSIM, pdata->vsim);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VAUX1, pdata->vaux1);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VAUX3, pdata->vaux3);
		if (IS_ERR(child))
			return PTR_ERR(child);

		child = add_regulator(TWL4030_REG_VAUX4, pdata->vaux4);
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

	e = twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, KEY_LOCK,
			R_PROTECT_KEY);
	return e;
}

static inline int __init unprotect_pm_master(void)
{
	int e = 0;

	e |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, KEY_UNLOCK1,
			R_PROTECT_KEY);
	e |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, KEY_UNLOCK2,
			R_PROTECT_KEY);
	return e;
}

static void clocks_init(struct device *dev)
{
	int e = 0;
	struct clk *osc;
	u32 rate;
	u8 ctrl = HFCLK_FREQ_26_MHZ;

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	if (cpu_is_omap2430())
		osc = clk_get(dev, "osc_ck");
	else
		osc = clk_get(dev, "osc_sys_ck");

	if (IS_ERR(osc)) {
		printk(KERN_WARNING "Skipping twl4030 internal clock init and "
				"using bootloader value (unknown osc rate)\n");
		return;
	}

	rate = clk_get_rate(osc);
	clk_put(osc);

#else
	/* REVISIT for non-OMAP systems, pass the clock rate from
	 * board init code, using platform_data.
	 */
	osc = ERR_PTR(-EIO);

	printk(KERN_WARNING "Skipping twl4030 internal clock init and "
	       "using bootloader value (unknown osc rate)\n");

	return;
#endif

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
	e |= unprotect_pm_master();
	/* effect->MADC+USB ck en */
	e |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_MASTER, ctrl, R_CFG_BOOT);
	e |= protect_pm_master();

	if (e < 0)
		pr_err("%s: clock init err [%d]\n", DRIVER_NAME, e);
}

/*----------------------------------------------------------------------*/

int twl_init_irq(int irq_num, unsigned irq_base, unsigned irq_end);
int twl_exit_irq(void);

static int twl4030_remove(struct i2c_client *client)
{
	unsigned i;
	int status;

	status = twl_exit_irq();
	if (status < 0)
		return status;

	for (i = 0; i < TWL4030_NUM_SLAVES; i++) {
		struct twl4030_client	*twl = &twl4030_modules[i];

		if (twl->client && twl->client != client)
			i2c_unregister_device(twl->client);
		twl4030_modules[i].client = NULL;
	}
	inuse = false;
	return 0;
}

/* NOTE:  this driver only handles a single twl4030/tps659x0 chip */
static int
twl4030_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int				status;
	unsigned			i;
	struct twl4030_platform_data	*pdata = client->dev.platform_data;

	if (!pdata) {
		dev_dbg(&client->dev, "no platform data?\n");
		return -EINVAL;
	}

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C) == 0) {
		dev_dbg(&client->dev, "can't talk I2C?\n");
		return -EIO;
	}

	if (inuse) {
		dev_dbg(&client->dev, "driver is already in use\n");
		return -EBUSY;
	}

	for (i = 0; i < TWL4030_NUM_SLAVES; i++) {
		struct twl4030_client	*twl = &twl4030_modules[i];

		twl->address = client->addr + i;
		if (i == 0)
			twl->client = client;
		else {
			twl->client = i2c_new_dummy(client->adapter,
					twl->address);
			if (!twl->client) {
				dev_err(&twl->client->dev,
					"can't attach client %d\n", i);
				status = -ENOMEM;
				goto fail;
			}
			strlcpy(twl->client->name, id->name,
					sizeof(twl->client->name));
		}
		mutex_init(&twl->xfer_lock);
	}
	inuse = true;

	/* setup clock framework */
	clocks_init(&client->dev);

	/* load power event scripts */
	if (twl_has_power() && pdata->power)
		twl4030_power_init(pdata->power);

	/* Maybe init the T2 Interrupt subsystem */
	if (client->irq
			&& pdata->irq_base
			&& pdata->irq_end > pdata->irq_base) {
		status = twl_init_irq(client->irq, pdata->irq_base, pdata->irq_end);
		if (status < 0)
			goto fail;
	}

	status = add_children(pdata, id->driver_data);
fail:
	if (status < 0)
		twl4030_remove(client);
	return status;
}

static const struct i2c_device_id twl4030_ids[] = {
	{ "twl4030", TWL4030_VAUX2 },	/* "Triton 2" */
	{ "twl5030", 0 },		/* T2 updated */
	{ "tps65950", 0 },		/* catalog version of twl5030 */
	{ "tps65930", TPS_SUBSET },	/* fewer LDOs and DACs; no charger */
	{ "tps65920", TPS_SUBSET },	/* fewer LDOs; no codec or charger */
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(i2c, twl4030_ids);

/* One Client Driver , 4 Clients */
static struct i2c_driver twl4030_driver = {
	.driver.name	= DRIVER_NAME,
	.id_table	= twl4030_ids,
	.probe		= twl4030_probe,
	.remove		= twl4030_remove,
};

static int __init twl4030_init(void)
{
	return i2c_add_driver(&twl4030_driver);
}
subsys_initcall(twl4030_init);

static void __exit twl4030_exit(void)
{
	i2c_del_driver(&twl4030_driver);
}
module_exit(twl4030_exit);

MODULE_AUTHOR("Texas Instruments, Inc.");
MODULE_DESCRIPTION("I2C Core interface for TWL4030");
MODULE_LICENSE("GPL");
