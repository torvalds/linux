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

#include <linux/i2c.h>
#include <linux/i2c/twl4030.h>


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

#if defined(CONFIG_TWL4030_MADC) || defined(CONFIG_TWL4030_MADC_MODULE)
#define twl_has_madc()	true
#else
#define twl_has_madc()	false
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


/* Triton Core internal information (BEGIN) */

/* Last - for index max*/
#define TWL4030_MODULE_LAST		TWL4030_MODULE_SECURED_REG

#define TWL4030_NUM_SLAVES		4


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
int twl4030_i2c_write(u8 mod_no, u8 *value, u8 reg, u8 num_bytes)
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
int twl4030_i2c_read(u8 mod_no, u8 *value, u8 reg, u8 num_bytes)
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

/*
 * NOTE:  We know the first 8 IRQs after pdata->base_irq are
 * for the PIH, and the next are for the PWR_INT SIH, since
 * that's how twl_init_irq() sets things up.
 */

static int add_children(struct twl4030_platform_data *pdata)
{
	struct platform_device	*pdev = NULL;
	struct twl4030_client	*twl = NULL;
	int			status = 0;

	if (twl_has_bci() && pdata->bci) {
		twl = &twl4030_modules[3];

		pdev = platform_device_alloc("twl4030_bci", -1);
		if (!pdev) {
			pr_debug("%s: can't alloc bci dev\n", DRIVER_NAME);
			status = -ENOMEM;
			goto err;
		}

		if (status == 0) {
			pdev->dev.parent = &twl->client->dev;
			status = platform_device_add_data(pdev, pdata->bci,
					sizeof(*pdata->bci));
			if (status < 0) {
				dev_dbg(&twl->client->dev,
					"can't add bci data, %d\n",
					status);
				goto err;
			}
		}

		if (status == 0) {
			struct resource r = {
				.start = pdata->irq_base + 8 + 1,
				.flags = IORESOURCE_IRQ,
			};

			status = platform_device_add_resources(pdev, &r, 1);
		}

		if (status == 0)
			status = platform_device_add(pdev);

		if (status < 0) {
			platform_device_put(pdev);
			dev_dbg(&twl->client->dev,
					"can't create bci dev, %d\n",
					status);
			goto err;
		}
	}

	if (twl_has_gpio() && pdata->gpio) {
		twl = &twl4030_modules[1];

		pdev = platform_device_alloc("twl4030_gpio", -1);
		if (!pdev) {
			pr_debug("%s: can't alloc gpio dev\n", DRIVER_NAME);
			status = -ENOMEM;
			goto err;
		}

		/* more driver model init */
		if (status == 0) {
			pdev->dev.parent = &twl->client->dev;
			/* device_init_wakeup(&pdev->dev, 1); */

			status = platform_device_add_data(pdev, pdata->gpio,
					sizeof(*pdata->gpio));
			if (status < 0) {
				dev_dbg(&twl->client->dev,
					"can't add gpio data, %d\n",
					status);
				goto err;
			}
		}

		/* GPIO module IRQ */
		if (status == 0) {
			struct resource	r = {
				.start = pdata->irq_base + 0,
				.flags = IORESOURCE_IRQ,
			};

			status = platform_device_add_resources(pdev, &r, 1);
		}

		if (status == 0)
			status = platform_device_add(pdev);

		if (status < 0) {
			platform_device_put(pdev);
			dev_dbg(&twl->client->dev,
					"can't create gpio dev, %d\n",
					status);
			goto err;
		}
	}

	if (twl_has_keypad() && pdata->keypad) {
		pdev = platform_device_alloc("twl4030_keypad", -1);
		if (pdev) {
			twl = &twl4030_modules[2];
			pdev->dev.parent = &twl->client->dev;
			device_init_wakeup(&pdev->dev, 1);
			status = platform_device_add_data(pdev, pdata->keypad,
					sizeof(*pdata->keypad));
			if (status < 0) {
				dev_dbg(&twl->client->dev,
					"can't add keypad data, %d\n",
					status);
				platform_device_put(pdev);
				goto err;
			}
			status = platform_device_add(pdev);
			if (status < 0) {
				platform_device_put(pdev);
				dev_dbg(&twl->client->dev,
						"can't create keypad dev, %d\n",
						status);
				goto err;
			}
		} else {
			pr_debug("%s: can't alloc keypad dev\n", DRIVER_NAME);
			status = -ENOMEM;
			goto err;
		}
	}

	if (twl_has_madc() && pdata->madc) {
		pdev = platform_device_alloc("twl4030_madc", -1);
		if (pdev) {
			twl = &twl4030_modules[2];
			pdev->dev.parent = &twl->client->dev;
			device_init_wakeup(&pdev->dev, 1);
			status = platform_device_add_data(pdev, pdata->madc,
					sizeof(*pdata->madc));
			if (status < 0) {
				platform_device_put(pdev);
				dev_dbg(&twl->client->dev,
					"can't add madc data, %d\n",
					status);
				goto err;
			}
			status = platform_device_add(pdev);
			if (status < 0) {
				platform_device_put(pdev);
				dev_dbg(&twl->client->dev,
						"can't create madc dev, %d\n",
						status);
				goto err;
			}
		} else {
			pr_debug("%s: can't alloc madc dev\n", DRIVER_NAME);
			status = -ENOMEM;
			goto err;
		}
	}

	if (twl_has_rtc()) {
		twl = &twl4030_modules[3];

		pdev = platform_device_alloc("twl4030_rtc", -1);
		if (!pdev) {
			pr_debug("%s: can't alloc rtc dev\n", DRIVER_NAME);
			status = -ENOMEM;
		} else {
			pdev->dev.parent = &twl->client->dev;
			device_init_wakeup(&pdev->dev, 1);
		}

		/*
		 * REVISIT platform_data here currently might use of
		 * "msecure" line ... but for now we just expect board
		 * setup to tell the chip "we are secure" at all times.
		 * Eventually, Linux might become more aware of such
		 * HW security concerns, and "least privilege".
		 */

		/* RTC module IRQ */
		if (status == 0) {
			struct resource	r = {
				.start = pdata->irq_base + 8 + 3,
				.flags = IORESOURCE_IRQ,
			};

			status = platform_device_add_resources(pdev, &r, 1);
		}

		if (status == 0)
			status = platform_device_add(pdev);

		if (status < 0) {
			platform_device_put(pdev);
			dev_dbg(&twl->client->dev,
					"can't create rtc dev, %d\n",
					status);
			goto err;
		}
	}

	if (twl_has_usb() && pdata->usb) {
		twl = &twl4030_modules[0];

		pdev = platform_device_alloc("twl4030_usb", -1);
		if (!pdev) {
			pr_debug("%s: can't alloc usb dev\n", DRIVER_NAME);
			status = -ENOMEM;
			goto err;
		}

		if (status == 0) {
			pdev->dev.parent = &twl->client->dev;
			device_init_wakeup(&pdev->dev, 1);
			status = platform_device_add_data(pdev, pdata->usb,
					sizeof(*pdata->usb));
			if (status < 0) {
				platform_device_put(pdev);
				dev_dbg(&twl->client->dev,
					"can't add usb data, %d\n",
					status);
				goto err;
			}
		}

		if (status == 0) {
			struct resource r = {
				.start = pdata->irq_base + 8 + 2,
				.flags = IORESOURCE_IRQ,
			};

			status = platform_device_add_resources(pdev, &r, 1);
		}

		if (status == 0)
			status = platform_device_add(pdev);

		if (status < 0) {
			platform_device_put(pdev);
			dev_dbg(&twl->client->dev,
					"can't create usb dev, %d\n",
					status);
		}
	}

err:
	if (status)
		pr_err("failed to add twl4030's children (status %d)\n", status);
	return status;
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

static void __init clocks_init(void)
{
	int e = 0;
	struct clk *osc;
	u32 rate;
	u8 ctrl = HFCLK_FREQ_26_MHZ;

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	if (cpu_is_omap2430())
		osc = clk_get(NULL, "osc_ck");
	else
		osc = clk_get(NULL, "osc_sys_ck");
#else
	/* REVISIT for non-OMAP systems, pass the clock rate from
	 * board init code, using platform_data.
	 */
	osc = ERR_PTR(-EIO);
#endif
	if (IS_ERR(osc)) {
		printk(KERN_WARNING "Skipping twl4030 internal clock init and "
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
	clocks_init();

	/* Maybe init the T2 Interrupt subsystem */
	if (client->irq
			&& pdata->irq_base
			&& pdata->irq_end > pdata->irq_base) {
		status = twl_init_irq(client->irq, pdata->irq_base, pdata->irq_end);
		if (status < 0)
			goto fail;
	}

	status = add_children(pdata);
fail:
	if (status < 0)
		twl4030_remove(client);
	return status;
}

static const struct i2c_device_id twl4030_ids[] = {
	{ "twl4030", 0 },	/* "Triton 2" */
	{ "tps65950", 0 },	/* catalog version of twl4030 */
	{ "tps65930", 0 },	/* fewer LDOs and DACs; no charger */
	{ "tps65920", 0 },	/* fewer LDOs; no codec or charger */
	{ "twl5030", 0 },	/* T2 updated */
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
