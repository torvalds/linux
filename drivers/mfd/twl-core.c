// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/init.h>
#include <linux/mutex.h>
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

#include <linux/mfd/core.h>
#include <linux/mfd/twl.h>

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
#define TWL6032_BASEADD_CHARGER		0x00DA
#define TWL6030_BASEADD_CHARGER		0x00E0
#define TWL6030_BASEADD_LED		0x00F4

/* subchip/slave 2 0x4A - DFT */
#define TWL6030_BASEADD_DIEID		0x00C0

/* subchip/slave 3 0x4B - AUDIO */
#define TWL6030_BASEADD_AUDIO		0x0000
#define TWL6030_BASEADD_RSV		0x0000
#define TWL6030_BASEADD_ZERO		0x0000

/* Some fields in TWL6030_PHOENIX_DEV_ON */
#define TWL6030_APP_DEVOFF		BIT(0)
#define TWL6030_CON_DEVOFF		BIT(1)
#define TWL6030_MOD_DEVOFF		BIT(2)

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
	 * <linux/mfd/twl.h> defines for TWL4030_MODULE_*
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
		.cache_type = REGCACHE_MAPLE,
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
	 * <linux/mfd/twl.h> defines for TWL4030_MODULE_*
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

	/* TWL6032 specific charger registers */
	{ 1, TWL6032_BASEADD_CHARGER },
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
 * Returns 0 on success or else a negative error code.
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
 * Returns 0 on success or else a negative error code.
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
 * twl_set_regcache_bypass - Configure the regcache bypass for the regmap associated
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

/*----------------------------------------------------------------------*/

/*
 * These three functions initialize the on-chip clock framework,
 * letting it generate the right frequencies for USB, MADC, and
 * other purposes.
 */
static inline int protect_pm_master(void)
{
	int e = 0;

	e = twl_i2c_write_u8(TWL_MODULE_PM_MASTER, 0,
			     TWL4030_PM_MASTER_PROTECT_KEY);
	return e;
}

static inline int unprotect_pm_master(void)
{
	int e = 0;

	e |= twl_i2c_write_u8(TWL_MODULE_PM_MASTER, TWL4030_PM_MASTER_KEY_CFG1,
			      TWL4030_PM_MASTER_PROTECT_KEY);
	e |= twl_i2c_write_u8(TWL_MODULE_PM_MASTER, TWL4030_PM_MASTER_KEY_CFG2,
			      TWL4030_PM_MASTER_PROTECT_KEY);

	return e;
}

static void clocks_init(struct device *dev)
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

	e |= unprotect_pm_master();
	/* effect->MADC+USB ck en */
	e |= twl_i2c_write_u8(TWL_MODULE_PM_MASTER, ctrl, R_CFG_BOOT);
	e |= protect_pm_master();

	if (e < 0)
		pr_err("%s: clock init err [%d]\n", DRIVER_NAME, e);
}

/*----------------------------------------------------------------------*/


static void twl_remove(struct i2c_client *client)
{
	unsigned i, num_slaves;

	if (twl_class_is_4030())
		twl4030_exit_irq();
	else
		twl6030_exit_irq();

	num_slaves = twl_get_num_slaves();
	for (i = 0; i < num_slaves; i++) {
		struct twl_client	*twl = &twl_priv->twl_modules[i];

		if (twl->client && twl->client != client)
			i2c_unregister_device(twl->client);
		twl->client = NULL;
	}
	twl_priv->ready = false;
}

static void twl6030_power_off(void)
{
	int err;
	u8 val;

	err = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &val, TWL6030_PHOENIX_DEV_ON);
	if (err)
		return;

	val |= TWL6030_APP_DEVOFF | TWL6030_CON_DEVOFF | TWL6030_MOD_DEVOFF;
	twl_i2c_write_u8(TWL_MODULE_PM_MASTER, val, TWL6030_PHOENIX_DEV_ON);
}


static struct of_dev_auxdata twl_auxdata_lookup[] = {
	OF_DEV_AUXDATA("ti,twl4030-gpio", 0, "twl4030-gpio", NULL),
	{ /* sentinel */ },
};

static const struct mfd_cell twl6030_cells[] = {
	{ .name = "twl6030-clk" },
};

static const struct mfd_cell twl6032_cells[] = {
	{ .name = "twl6032-clk" },
};

/* NOTE: This driver only handles a single twl4030/tps659x0 chip */
static int
twl_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct device_node		*node = client->dev.of_node;
	struct platform_device		*pdev;
	const struct regmap_config	*twl_regmap_config;
	int				irq_base = 0;
	int				status;
	unsigned			i, num_slaves;

	if (!node) {
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
		twl_regmap_config = twl6030_regmap_config;
	} else {
		twl_priv->twl_id = TWL4030_CLASS_ID;
		twl_priv->twl_map = &twl4030_map[0];
		twl_regmap_config = twl4030_regmap_config;
	}

	num_slaves = twl_get_num_slaves();
	twl_priv->twl_modules = devm_kcalloc(&client->dev,
					 num_slaves,
					 sizeof(struct twl_client),
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
			twl->client = i2c_new_dummy_device(client->adapter,
						    client->addr + i);
			if (IS_ERR(twl->client)) {
				dev_err(&client->dev,
					"can't attach client %d\n", i);
				status = PTR_ERR(twl->client);
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
	clocks_init(&client->dev);

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
	 * do anything over I2C4 for voltage scaling even if SmartReflex
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

	if (twl_class_is_6030()) {
		const struct mfd_cell *cells;
		int num_cells;

		if (id->driver_data & TWL6032_SUBCLASS) {
			cells = twl6032_cells;
			num_cells = ARRAY_SIZE(twl6032_cells);
		} else {
			cells = twl6030_cells;
			num_cells = ARRAY_SIZE(twl6030_cells);
		}

		status = devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_NONE,
					      cells, num_cells, NULL, 0, NULL);
		if (status < 0)
			goto free;

		if (of_device_is_system_power_controller(node)) {
			if (!pm_power_off)
				pm_power_off = twl6030_power_off;
			else
				dev_warn(&client->dev, "Poweroff callback already assigned\n");
		}
	}

	status = of_platform_populate(node, NULL, twl_auxdata_lookup,
				      &client->dev);

fail:
	if (status < 0)
		twl_remove(client);
free:
	if (status < 0)
		platform_device_unregister(pdev);

	return status;
}

static int __maybe_unused twl_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (client->irq)
		disable_irq(client->irq);

	return 0;
}

static int __maybe_unused twl_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (client->irq)
		enable_irq(client->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(twl_dev_pm_ops, twl_suspend, twl_resume);

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

/* One Client Driver , 4 Clients */
static struct i2c_driver twl_driver = {
	.driver.name	= DRIVER_NAME,
	.driver.pm	= &twl_dev_pm_ops,
	.id_table	= twl_ids,
	.probe		= twl_probe,
	.remove		= twl_remove,
};
builtin_i2c_driver(twl_driver);
