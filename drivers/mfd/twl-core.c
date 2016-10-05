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

/* Register descriptions for audio */
#include <linux/mfd/twl4030-audio.h>

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
#define TWL4030_BASEADD_PWM		0x00F8
#define TWL4030_BASEADD_KEYPAD		0x00D2

#define TWL5031_BASEADD_ACCESSORY	0x0074 /* Replaces Main Charge */
#define TWL5031_BASEADD_INTERRUPTS	0x00B9 /* Different than TWL4030's
						  one */

/* subchip/slave 3 - POWER ID */
#define TWL4030_BASEADD_BACKUP		0x0014
#define TWL4030_BASEADD_INT		0x002E
#define TWL4030_BASEADD_PM_MASTER	0x0036

#define TWL4030_BASEADD_PM_RECEIVER	0x005B
#define TWL4030_DCDC_GLOBAL_CFG		0x06
#define SMARTREFLEX_ENABLE		BIT(3)

#define TWL4030_BASEADD_RTC		0x001C
#define TWL4030_BASEADD_SECURED_REG	0x0000

/* Triton Core internal information (END) */


/* subchip/slave 0 0x48 - POWER */
#define TWL6030_BASEADD_RTC		0x0000
#define TWL6030_BASEADD_SECURED_REG	0x0017
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
#define TWL6032_BASEADD_CHARGER		0x00DA
#define TWL6030_BASEADD_LED		0x00F4

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

/* Structure for each TWL4030/TWL6030 Slave */
struct twl_client {
	struct i2c_client *client;
	struct regmap *regmap;
};

/* mapping the module id to slave id and base address */
struct twl_mapping {
	unsigned char sid;	/* Slave ID */
	unsigned char base;	/* base address */
};

struct twl_private {
	bool ready; /* The core driver is ready to be used */
	u32 twl_idcode; /* TWL IDCODE Register value */
	unsigned int twl_id;

	struct twl_mapping *twl_map;
	struct twl_client *twl_modules;
};

static struct twl_private *twl_priv;

static struct twl_mapping twl4030_map[] = {
	/*
	 * NOTE:  don't change this table without updating the
	 * <linux/i2c/twl.h> defines for TWL4030_MODULE_*
	 * so they continue to match the order in this table.
	 */

	/* Common IPs */
	{ 0, TWL4030_BASEADD_USB },
	{ 1, TWL4030_BASEADD_PIH },
	{ 2, TWL4030_BASEADD_MAIN_CHARGE },
	{ 3, TWL4030_BASEADD_PM_MASTER },
	{ 3, TWL4030_BASEADD_PM_RECEIVER },

	{ 3, TWL4030_BASEADD_RTC },
	{ 2, TWL4030_BASEADD_PWM },
	{ 2, TWL4030_BASEADD_LED },
	{ 3, TWL4030_BASEADD_SECURED_REG },

	/* TWL4030 specific IPs */
	{ 1, TWL4030_BASEADD_AUDIO_VOICE },
	{ 1, TWL4030_BASEADD_GPIO },
	{ 1, TWL4030_BASEADD_INTBR },
	{ 1, TWL4030_BASEADD_TEST },
	{ 2, TWL4030_BASEADD_KEYPAD },

	{ 2, TWL4030_BASEADD_MADC },
	{ 2, TWL4030_BASEADD_INTERRUPTS },
	{ 2, TWL4030_BASEADD_PRECHARGE },
	{ 3, TWL4030_BASEADD_BACKUP },
	{ 3, TWL4030_BASEADD_INT },

	{ 2, TWL5031_BASEADD_ACCESSORY },
	{ 2, TWL5031_BASEADD_INTERRUPTS },
};

static const struct reg_default twl4030_49_defaults[] = {
	/* Audio Registers */
	{ 0x01, 0x00}, /* CODEC_MODE	*/
	{ 0x02, 0x00}, /* OPTION	*/
	/* 0x03  Unused	*/
	{ 0x04, 0x00}, /* MICBIAS_CTL	*/
	{ 0x05, 0x00}, /* ANAMICL	*/
	{ 0x06, 0x00}, /* ANAMICR	*/
	{ 0x07, 0x00}, /* AVADC_CTL	*/
	{ 0x08, 0x00}, /* ADCMICSEL	*/
	{ 0x09, 0x00}, /* DIGMIXING	*/
	{ 0x0a, 0x0f}, /* ATXL1PGA	*/
	{ 0x0b, 0x0f}, /* ATXR1PGA	*/
	{ 0x0c, 0x0f}, /* AVTXL2PGA	*/
	{ 0x0d, 0x0f}, /* AVTXR2PGA	*/
	{ 0x0e, 0x00}, /* AUDIO_IF	*/
	{ 0x0f, 0x00}, /* VOICE_IF	*/
	{ 0x10, 0x3f}, /* ARXR1PGA	*/
	{ 0x11, 0x3f}, /* ARXL1PGA	*/
	{ 0x12, 0x3f}, /* ARXR2PGA	*/
	{ 0x13, 0x3f}, /* ARXL2PGA	*/
	{ 0x14, 0x25}, /* VRXPGA	*/
	{ 0x15, 0x00}, /* VSTPGA	*/
	{ 0x16, 0x00}, /* VRX2ARXPGA	*/
	{ 0x17, 0x00}, /* AVDAC_CTL	*/
	{ 0x18, 0x00}, /* ARX2VTXPGA	*/
	{ 0x19, 0x32}, /* ARXL1_APGA_CTL*/
	{ 0x1a, 0x32}, /* ARXR1_APGA_CTL*/
	{ 0x1b, 0x32}, /* ARXL2_APGA_CTL*/
	{ 0x1c, 0x32}, /* ARXR2_APGA_CTL*/
	{ 0x1d, 0x00}, /* ATX2ARXPGA	*/
	{ 0x1e, 0x00}, /* BT_IF		*/
	{ 0x1f, 0x55}, /* BTPGA		*/
	{ 0x20, 0x00}, /* BTSTPGA	*/
	{ 0x21, 0x00}, /* EAR_CTL	*/
	{ 0x22, 0x00}, /* HS_SEL	*/
	{ 0x23, 0x00}, /* HS_GAIN_SET	*/
	{ 0x24, 0x00}, /* HS_POPN_SET	*/
	{ 0x25, 0x00}, /* PREDL_CTL	*/
	{ 0x26, 0x00}, /* PREDR_CTL	*/
	{ 0x27, 0x00}, /* PRECKL_CTL	*/
	{ 0x28, 0x00}, /* PRECKR_CTL	*/
	{ 0x29, 0x00}, /* HFL_CTL	*/
	{ 0x2a, 0x00}, /* HFR_CTL	*/
	{ 0x2b, 0x05}, /* ALC_CTL	*/
	{ 0x2c, 0x00}, /* ALC_SET1	*/
	{ 0x2d, 0x00}, /* ALC_SET2	*/
	{ 0x2e, 0x00}, /* BOOST_CTL	*/
	{ 0x2f, 0x00}, /* SOFTVOL_CTL	*/
	{ 0x30, 0x13}, /* DTMF_FREQSEL	*/
	{ 0x31, 0x00}, /* DTMF_TONEXT1H	*/
	{ 0x32, 0x00}, /* DTMF_TONEXT1L	*/
	{ 0x33, 0x00}, /* DTMF_TONEXT2H	*/
	{ 0x34, 0x00}, /* DTMF_TONEXT2L	*/
	{ 0x35, 0x79}, /* DTMF_TONOFF	*/
	{ 0x36, 0x11}, /* DTMF_WANONOFF	*/
	{ 0x37, 0x00}, /* I2S_RX_SCRAMBLE_H */
	{ 0x38, 0x00}, /* I2S_RX_SCRAMBLE_M */
	{ 0x39, 0x00}, /* I2S_RX_SCRAMBLE_L */
	{ 0x3a, 0x06}, /* APLL_CTL */
	{ 0x3b, 0x00}, /* DTMF_CTL */
	{ 0x3c, 0x44}, /* DTMF_PGA_CTL2	(0x3C) */
	{ 0x3d, 0x69}, /* DTMF_PGA_CTL1	(0x3D) */
	{ 0x3e, 0x00}, /* MISC_SET_1 */
	{ 0x3f, 0x00}, /* PCMBTMUX */
	/* 0x40 - 0x42  Unused */
	{ 0x43, 0x00}, /* RX_PATH_SEL */
	{ 0x44, 0x32}, /* VDL_APGA_CTL */
	{ 0x45, 0x00}, /* VIBRA_CTL */
	{ 0x46, 0x00}, /* VIBRA_SET */
	{ 0x47, 0x00}, /* VIBRA_PWM_SET	*/
	{ 0x48, 0x00}, /* ANAMIC_GAIN	*/
	{ 0x49, 0x00}, /* MISC_SET_2	*/
	/* End of Audio Registers */
};

static bool twl4030_49_nop_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00:
	case 0x03:
	case 0x40:
	case 0x41:
	case 0x42:
		return false;
	default:
		return true;
	}
}

static const struct regmap_range twl4030_49_volatile_ranges[] = {
	regmap_reg_range(TWL4030_BASEADD_TEST, 0xff),
};

static const struct regmap_access_table twl4030_49_volatile_table = {
	.yes_ranges = twl4030_49_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(twl4030_49_volatile_ranges),
};

static const struct regmap_config twl4030_regmap_config[4] = {
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

		.readable_reg = twl4030_49_nop_reg,
		.writeable_reg = twl4030_49_nop_reg,

		.volatile_table = &twl4030_49_volatile_table,

		.reg_defaults = twl4030_49_defaults,
		.num_reg_defaults = ARRAY_SIZE(twl4030_49_defaults),
		.cache_type = REGCACHE_RBTREE,
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

	/* Common IPs */
	{ 1, TWL6030_BASEADD_USB },
	{ 1, TWL6030_BASEADD_PIH },
	{ 1, TWL6030_BASEADD_CHARGER },
	{ 0, TWL6030_BASEADD_PM_MASTER },
	{ 0, TWL6030_BASEADD_PM_SLAVE_MISC },

	{ 0, TWL6030_BASEADD_RTC },
	{ 1, TWL6030_BASEADD_PWM },
	{ 1, TWL6030_BASEADD_LED },
	{ 0, TWL6030_BASEADD_SECURED_REG },

	/* TWL6030 specific IPs */
	{ 0, TWL6030_BASEADD_ZERO },
	{ 1, TWL6030_BASEADD_ZERO },
	{ 2, TWL6030_BASEADD_ZERO },
	{ 1, TWL6030_BASEADD_GPADC_CTRL },
	{ 1, TWL6030_BASEADD_GASGAUGE },
};

static const struct regmap_config twl6030_regmap_config[3] = {
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

static inline int twl_get_num_slaves(void)
{
	if (twl_class_is_4030())
		return 4; /* TWL4030 class have four slave address */
	else
		return 3; /* TWL6030 class have three slave address */
}

static inline int twl_get_last_module(void)
{
	if (twl_class_is_4030())
		return TWL4030_MODULE_LAST;
	else
		return TWL6030_MODULE_LAST;
}

/* Exported Functions */

unsigned int twl_rev(void)
{
	return twl_priv ? twl_priv->twl_id : 0;
}
EXPORT_SYMBOL(twl_rev);

/**
 * twl_get_regmap - Get the regmap associated with the given module
 * @mod_no: module number
 *
 * Returns the regmap pointer or NULL in case of failure.
 */
static struct regmap *twl_get_regmap(u8 mod_no)
{
	int sid;
	struct twl_client *twl;

	if (unlikely(!twl_priv || !twl_priv->ready)) {
		pr_err("%s: not initialized\n", DRIVER_NAME);
		return NULL;
	}
	if (unlikely(mod_no >= twl_get_last_module())) {
		pr_err("%s: invalid module number %d\n", DRIVER_NAME, mod_no);
		return NULL;
	}

	sid = twl_priv->twl_map[mod_no].sid;
	twl = &twl_priv->twl_modules[sid];

	return twl->regmap;
}

/**
 * twl_i2c_write - Writes a n bit register in TWL4030/TWL5030/TWL60X0
 * @mod_no: module number
 * @value: an array of num_bytes+1 containing data to write
 * @reg: register address (just offset will do)
 * @num_bytes: number of bytes to transfer
 *
 * Returns the result of operation - 0 is success
 */
int twl_i2c_write(u8 mod_no, u8 *value, u8 reg, unsigned num_bytes)
{
	struct regmap *regmap = twl_get_regmap(mod_no);
	int ret;

	if (!regmap)
		return -EPERM;

	ret = regmap_bulk_write(regmap, twl_priv->twl_map[mod_no].base + reg,
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
	struct regmap *regmap = twl_get_regmap(mod_no);
	int ret;

	if (!regmap)
		return -EPERM;

	ret = regmap_bulk_read(regmap, twl_priv->twl_map[mod_no].base + reg,
			       value, num_bytes);

	if (ret)
		pr_err("%s: Read failed (mod %d, reg 0x%02x count %d)\n",
		       DRIVER_NAME, mod_no, reg, num_bytes);

	return ret;
}
EXPORT_SYMBOL(twl_i2c_read);

/**
 * twl_regcache_bypass - Configure the regcache bypass for the regmap associated
 *			 with the module
 * @mod_no: module number
 * @enable: Regcache bypass state
 *
 * Returns 0 else failure.
 */
int twl_set_regcache_bypass(u8 mod_no, bool enable)
{
	struct regmap *regmap = twl_get_regmap(mod_no);

	if (!regmap)
		return -EPERM;

	regcache_cache_bypass(regmap, enable);

	return 0;
}
EXPORT_SYMBOL(twl_set_regcache_bypass);

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

	err = twl_i2c_read(TWL4030_MODULE_INTBR, (u8 *)(&twl_priv->twl_idcode),
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
	return TWL_SIL_TYPE(twl_priv->twl_idcode);
}
EXPORT_SYMBOL_GPL(twl_get_type);

/**
 * twl_get_version - API to get TWL Si version.
 *
 * Api to get the TWL Si version from IDCODE value.
 */
int twl_get_version(void)
{
	return TWL_SIL_REV(twl_priv->twl_idcode);
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
add_numbered_child(unsigned mod_no, const char *name, int num,
		void *pdata, unsigned pdata_len,
		bool can_wakeup, int irq0, int irq1)
{
	struct platform_device	*pdev;
	struct twl_client	*twl;
	int			status, sid;

	if (unlikely(mod_no >= twl_get_last_module())) {
		pr_err("%s: invalid module number %d\n", DRIVER_NAME, mod_no);
		return ERR_PTR(-EPERM);
	}
	sid = twl_priv->twl_map[mod_no].sid;
	twl = &twl_priv->twl_modules[sid];

	pdev = platform_device_alloc(name, num);
	if (!pdev)
		return ERR_PTR(-ENOMEM);

	pdev->dev.parent = &twl->client->dev;

	if (pdata) {
		status = platform_device_add_data(pdev, pdata, pdata_len);
		if (status < 0) {
			dev_dbg(&pdev->dev, "can't add platform_data\n");
			goto put_device;
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
			goto put_device;
		}
	}

	status = platform_device_add(pdev);
	if (status)
		goto put_device;

	device_init_wakeup(&pdev->dev, can_wakeup);

	return &pdev->dev;

put_device:
	platform_device_put(pdev);
	dev_err(&twl->client->dev, "failed to add device %s\n", name);
	return ERR_PTR(status);
}

static inline struct device *add_child(unsigned mod_no, const char *name,
		void *pdata, unsigned pdata_len,
		bool can_wakeup, int irq0, int irq1)
{
	return add_numbered_child(mod_no, name, -1, pdata, pdata_len,
		can_wakeup, irq0, irq1);
}

static struct device *
add_regulator_linked(int num, struct regulator_init_data *pdata,
		struct regulator_consumer_supply *consumers,
		unsigned num_consumers, unsigned long features)
{
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
	return add_numbered_child(TWL_MODULE_PM_MASTER, "twl_reg", num,
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

	if (IS_ENABLED(CONFIG_GPIO_TWL4030) && pdata->gpio) {
		child = add_child(TWL4030_MODULE_GPIO, "twl4030_gpio",
				pdata->gpio, sizeof(*pdata->gpio),
				false, irq_base + GPIO_INTR_OFFSET, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_KEYBOARD_TWL4030) && pdata->keypad) {
		child = add_child(TWL4030_MODULE_KEYPAD, "twl4030_keypad",
				pdata->keypad, sizeof(*pdata->keypad),
				true, irq_base + KEYPAD_INTR_OFFSET, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_TWL4030_MADC) && pdata->madc &&
	    twl_class_is_4030()) {
		child = add_child(TWL4030_MODULE_MADC, "twl4030_madc",
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
		child = add_child(TWL_MODULE_RTC, "twl_rtc", NULL, 0,
				true, irq_base + RTC_INTR_OFFSET, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_PWM_TWL)) {
		child = add_child(TWL_MODULE_PWM, "twl-pwm", NULL, 0,
				  false, 0, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_PWM_TWL_LED)) {
		child = add_child(TWL_MODULE_LED, "twl-pwmled", NULL, 0,
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
		static struct regulator_consumer_supply usb3v1 = {
			.supply =	"usb3v1",
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
						      &usb_fixed, &usb3v1, 1,
						      features);
			if (IS_ERR(child))
				return PTR_ERR(child);

		}

		child = add_child(TWL_MODULE_USB, "twl4030_usb",
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
			usb3v1.dev_name = dev_name(child);
		}
	}

	if (IS_ENABLED(CONFIG_TWL4030_WATCHDOG) && twl_class_is_4030()) {
		child = add_child(TWL_MODULE_PM_RECEIVER, "twl4030_wdt", NULL,
				  0, false, 0, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_INPUT_TWL4030_PWRBUTTON) && twl_class_is_4030()) {
		child = add_child(TWL_MODULE_PM_MASTER, "twl4030_pwrbutton",
				  NULL, 0, true, irq_base + 8 + 0, 0);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_MFD_TWL4030_AUDIO) && pdata->audio &&
	    twl_class_is_4030()) {
		child = add_child(TWL4030_MODULE_AUDIO_VOICE, "twl4030-audio",
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

	if (IS_ENABLED(CONFIG_CHARGER_TWL4030) && pdata->bci &&
			!(features & (TPS_SUBSET | TWL5031))) {
		child = add_child(TWL_MODULE_MAIN_CHARGE, "twl4030_bci",
				pdata->bci, sizeof(*pdata->bci), false,
				/* irq0 = CHG_PRES, irq1 = BCI */
				irq_base + BCI_PRES_INTR_OFFSET,
				irq_base + BCI_INTR_OFFSET);
		if (IS_ERR(child))
			return PTR_ERR(child);
	}

	if (IS_ENABLED(CONFIG_TWL4030_POWER) && pdata->power) {
		child = add_child(TWL_MODULE_PM_MASTER, "twl4030_power",
				  pdata->power, sizeof(*pdata->power), false,
				  0, 0);
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

	if (twl_class_is_4030())
		status = twl4030_exit_irq();
	else
		status = twl6030_exit_irq();

	if (status < 0)
		return status;

	num_slaves = twl_get_num_slaves();
	for (i = 0; i < num_slaves; i++) {
		struct twl_client	*twl = &twl_priv->twl_modules[i];

		if (twl->client && twl->client != client)
			i2c_unregister_device(twl->client);
		twl->client = NULL;
	}
	twl_priv->ready = false;
	return 0;
}

static struct of_dev_auxdata twl_auxdata_lookup[] = {
	OF_DEV_AUXDATA("ti,twl4030-gpio", 0, "twl4030-gpio", NULL),
	{ /* sentinel */ },
};

/* NOTE: This driver only handles a single twl4030/tps659x0 chip */
static int
twl_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct twl4030_platform_data	*pdata = dev_get_platdata(&client->dev);
	struct device_node		*node = client->dev.of_node;
	struct platform_device		*pdev;
	const struct regmap_config	*twl_regmap_config;
	int				irq_base = 0;
	int				status;
	unsigned			i, num_slaves;

	if (!node && !pdata) {
		dev_err(&client->dev, "no platform data\n");
		return -EINVAL;
	}

	if (twl_priv) {
		dev_dbg(&client->dev, "only one instance of %s allowed\n",
			DRIVER_NAME);
		return -EBUSY;
	}

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

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C) == 0) {
		dev_dbg(&client->dev, "can't talk I2C?\n");
		status = -EIO;
		goto free;
	}

	twl_priv = devm_kzalloc(&client->dev, sizeof(struct twl_private),
				GFP_KERNEL);
	if (!twl_priv) {
		status = -ENOMEM;
		goto free;
	}

	if ((id->driver_data) & TWL6030_CLASS) {
		twl_priv->twl_id = TWL6030_CLASS_ID;
		twl_priv->twl_map = &twl6030_map[0];
		/* The charger base address is different in twl6032 */
		if ((id->driver_data) & TWL6032_SUBCLASS)
			twl_priv->twl_map[TWL_MODULE_MAIN_CHARGE].base =
							TWL6032_BASEADD_CHARGER;
		twl_regmap_config = twl6030_regmap_config;
	} else {
		twl_priv->twl_id = TWL4030_CLASS_ID;
		twl_priv->twl_map = &twl4030_map[0];
		twl_regmap_config = twl4030_regmap_config;
	}

	num_slaves = twl_get_num_slaves();
	twl_priv->twl_modules = devm_kzalloc(&client->dev,
					 sizeof(struct twl_client) * num_slaves,
					 GFP_KERNEL);
	if (!twl_priv->twl_modules) {
		status = -ENOMEM;
		goto free;
	}

	for (i = 0; i < num_slaves; i++) {
		struct twl_client *twl = &twl_priv->twl_modules[i];

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

	twl_priv->ready = true;

	/* setup clock framework */
	clocks_init(&pdev->dev, pdata ? pdata->clock : NULL);

	/* read TWL IDCODE Register */
	if (twl_class_is_4030()) {
		status = twl_read_idcode_register();
		WARN(status < 0, "Error: reading twl_idcode register value\n");
	}

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
	 *
	 * Also, always enable SmartReflex bit as that's needed for omaps to
	 * to do anything over I2C4 for voltage scaling even if SmartReflex
	 * is disabled. Without the SmartReflex bit omap sys_clkreq idle
	 * signal will never trigger for retention idle.
	 */
	if (twl_class_is_4030()) {
		u8 temp;

		twl_i2c_read_u8(TWL4030_MODULE_INTBR, &temp, REG_GPPUPDCTR1);
		temp &= ~(SR_I2C_SDA_CTRL_PU | SR_I2C_SCL_CTRL_PU | \
			I2C_SDA_CTRL_PU | I2C_SCL_CTRL_PU);
		twl_i2c_write_u8(TWL4030_MODULE_INTBR, temp, REG_GPPUPDCTR1);

		twl_i2c_read_u8(TWL_MODULE_PM_RECEIVER, &temp,
				TWL4030_DCDC_GLOBAL_CFG);
		temp |= SMARTREFLEX_ENABLE;
		twl_i2c_write_u8(TWL_MODULE_PM_RECEIVER, temp,
				 TWL4030_DCDC_GLOBAL_CFG);
	}

	if (node) {
		if (pdata)
			twl_auxdata_lookup[0].platform_data = pdata->gpio;
		status = of_platform_populate(node, NULL, twl_auxdata_lookup,
					      &client->dev);
	} else {
		status = add_children(pdata, irq_base, id->driver_data);
	}

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
	{ "twl6032", TWL6030_CLASS | TWL6032_SUBCLASS }, /* "Phoenix lite" */
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

module_i2c_driver(twl_driver);

MODULE_AUTHOR("Texas Instruments, Inc.");
MODULE_DESCRIPTION("I2C Core interface for TWL");
MODULE_LICENSE("GPL");
