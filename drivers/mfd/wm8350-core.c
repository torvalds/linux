/*
 * wm8350-core.c  --  Device access for Wolfson WM8350
 *
 * Copyright 2007, 2008 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood, Mark Brown
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/bug.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#include <linux/mfd/wm8350/core.h>
#include <linux/mfd/wm8350/audio.h>
#include <linux/mfd/wm8350/comparator.h>
#include <linux/mfd/wm8350/gpio.h>
#include <linux/mfd/wm8350/pmic.h>
#include <linux/mfd/wm8350/rtc.h>
#include <linux/mfd/wm8350/supply.h>
#include <linux/mfd/wm8350/wdt.h>

#define WM8350_UNLOCK_KEY		0x0013
#define WM8350_LOCK_KEY			0x0000

#define WM8350_CLOCK_CONTROL_1		0x28
#define WM8350_AIF_TEST			0x74

/* debug */
#define WM8350_BUS_DEBUG 0
#if WM8350_BUS_DEBUG
#define dump(regs, src) do { \
	int i_; \
	u16 *src_ = src; \
	printk(KERN_DEBUG); \
	for (i_ = 0; i_ < regs; i_++) \
		printk(" 0x%4.4x", *src_++); \
	printk("\n"); \
} while (0);
#else
#define dump(bytes, src)
#endif

#define WM8350_LOCK_DEBUG 0
#if WM8350_LOCK_DEBUG
#define ldbg(format, arg...) printk(format, ## arg)
#else
#define ldbg(format, arg...)
#endif

/*
 * WM8350 Device IO
 */
static DEFINE_MUTEX(io_mutex);
static DEFINE_MUTEX(reg_lock_mutex);

/* Perform a physical read from the device.
 */
static int wm8350_phys_read(struct wm8350 *wm8350, u8 reg, int num_regs,
			    u16 *dest)
{
	int i, ret;
	int bytes = num_regs * 2;

	dev_dbg(wm8350->dev, "volatile read\n");
	ret = regmap_raw_read(wm8350->regmap, reg, dest, bytes);

	for (i = reg; i < reg + num_regs; i++) {
		/* Cache is CPU endian */
		dest[i - reg] = be16_to_cpu(dest[i - reg]);

		/* Mask out non-readable bits */
		dest[i - reg] &= wm8350_reg_io_map[i].readable;
	}

	dump(num_regs, dest);

	return ret;
}

static int wm8350_read(struct wm8350 *wm8350, u8 reg, int num_regs, u16 *dest)
{
	int i;
	int end = reg + num_regs;
	int ret = 0;
	int bytes = num_regs * 2;

	if ((reg + num_regs - 1) > WM8350_MAX_REGISTER) {
		dev_err(wm8350->dev, "invalid reg %x\n",
			reg + num_regs - 1);
		return -EINVAL;
	}

	dev_dbg(wm8350->dev,
		"%s R%d(0x%2.2x) %d regs\n", __func__, reg, reg, num_regs);

#if WM8350_BUS_DEBUG
	/* we can _safely_ read any register, but warn if read not supported */
	for (i = reg; i < end; i++) {
		if (!wm8350_reg_io_map[i].readable)
			dev_warn(wm8350->dev,
				"reg R%d is not readable\n", i);
	}
#endif

	/* if any volatile registers are required, then read back all */
	for (i = reg; i < end; i++)
		if (wm8350_reg_io_map[i].vol)
			return wm8350_phys_read(wm8350, reg, num_regs, dest);

	/* no volatiles, then cache is good */
	dev_dbg(wm8350->dev, "cache read\n");
	memcpy(dest, &wm8350->reg_cache[reg], bytes);
	dump(num_regs, dest);
	return ret;
}

static inline int is_reg_locked(struct wm8350 *wm8350, u8 reg)
{
	if (reg == WM8350_SECURITY ||
	    wm8350->reg_cache[WM8350_SECURITY] == WM8350_UNLOCK_KEY)
		return 0;

	if ((reg >= WM8350_GPIO_FUNCTION_SELECT_1 &&
	     reg <= WM8350_GPIO_FUNCTION_SELECT_4) ||
	    (reg >= WM8350_BATTERY_CHARGER_CONTROL_1 &&
	     reg <= WM8350_BATTERY_CHARGER_CONTROL_3))
		return 1;
	return 0;
}

static int wm8350_write(struct wm8350 *wm8350, u8 reg, int num_regs, u16 *src)
{
	int i;
	int end = reg + num_regs;
	int bytes = num_regs * 2;

	if ((reg + num_regs - 1) > WM8350_MAX_REGISTER) {
		dev_err(wm8350->dev, "invalid reg %x\n",
			reg + num_regs - 1);
		return -EINVAL;
	}

	/* it's generally not a good idea to write to RO or locked registers */
	for (i = reg; i < end; i++) {
		if (!wm8350_reg_io_map[i].writable) {
			dev_err(wm8350->dev,
				"attempted write to read only reg R%d\n", i);
			return -EINVAL;
		}

		if (is_reg_locked(wm8350, i)) {
			dev_err(wm8350->dev,
			       "attempted write to locked reg R%d\n", i);
			return -EINVAL;
		}

		src[i - reg] &= wm8350_reg_io_map[i].writable;

		wm8350->reg_cache[i] =
			(wm8350->reg_cache[i] & ~wm8350_reg_io_map[i].writable)
			| src[i - reg];

		src[i - reg] = cpu_to_be16(src[i - reg]);
	}

	/* Actually write it out */
	return regmap_raw_write(wm8350->regmap, reg, src, bytes);
}

/*
 * Safe read, modify, write methods
 */
int wm8350_clear_bits(struct wm8350 *wm8350, u16 reg, u16 mask)
{
	u16 data;
	int err;

	mutex_lock(&io_mutex);
	err = wm8350_read(wm8350, reg, 1, &data);
	if (err) {
		dev_err(wm8350->dev, "read from reg R%d failed\n", reg);
		goto out;
	}

	data &= ~mask;
	err = wm8350_write(wm8350, reg, 1, &data);
	if (err)
		dev_err(wm8350->dev, "write to reg R%d failed\n", reg);
out:
	mutex_unlock(&io_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(wm8350_clear_bits);

int wm8350_set_bits(struct wm8350 *wm8350, u16 reg, u16 mask)
{
	u16 data;
	int err;

	mutex_lock(&io_mutex);
	err = wm8350_read(wm8350, reg, 1, &data);
	if (err) {
		dev_err(wm8350->dev, "read from reg R%d failed\n", reg);
		goto out;
	}

	data |= mask;
	err = wm8350_write(wm8350, reg, 1, &data);
	if (err)
		dev_err(wm8350->dev, "write to reg R%d failed\n", reg);
out:
	mutex_unlock(&io_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(wm8350_set_bits);

u16 wm8350_reg_read(struct wm8350 *wm8350, int reg)
{
	u16 data;
	int err;

	mutex_lock(&io_mutex);
	err = wm8350_read(wm8350, reg, 1, &data);
	if (err)
		dev_err(wm8350->dev, "read from reg R%d failed\n", reg);

	mutex_unlock(&io_mutex);
	return data;
}
EXPORT_SYMBOL_GPL(wm8350_reg_read);

int wm8350_reg_write(struct wm8350 *wm8350, int reg, u16 val)
{
	int ret;
	u16 data = val;

	mutex_lock(&io_mutex);
	ret = wm8350_write(wm8350, reg, 1, &data);
	if (ret)
		dev_err(wm8350->dev, "write to reg R%d failed\n", reg);
	mutex_unlock(&io_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(wm8350_reg_write);

int wm8350_block_read(struct wm8350 *wm8350, int start_reg, int regs,
		      u16 *dest)
{
	int err = 0;

	mutex_lock(&io_mutex);
	err = wm8350_read(wm8350, start_reg, regs, dest);
	if (err)
		dev_err(wm8350->dev, "block read starting from R%d failed\n",
			start_reg);
	mutex_unlock(&io_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(wm8350_block_read);

int wm8350_block_write(struct wm8350 *wm8350, int start_reg, int regs,
		       u16 *src)
{
	int ret = 0;

	mutex_lock(&io_mutex);
	ret = wm8350_write(wm8350, start_reg, regs, src);
	if (ret)
		dev_err(wm8350->dev, "block write starting at R%d failed\n",
			start_reg);
	mutex_unlock(&io_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(wm8350_block_write);

/**
 * wm8350_reg_lock()
 *
 * The WM8350 has a hardware lock which can be used to prevent writes to
 * some registers (generally those which can cause particularly serious
 * problems if misused).  This function enables that lock.
 */
int wm8350_reg_lock(struct wm8350 *wm8350)
{
	u16 key = WM8350_LOCK_KEY;
	int ret;

	ldbg(__func__);
	mutex_lock(&io_mutex);
	ret = wm8350_write(wm8350, WM8350_SECURITY, 1, &key);
	if (ret)
		dev_err(wm8350->dev, "lock failed\n");
	mutex_unlock(&io_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(wm8350_reg_lock);

/**
 * wm8350_reg_unlock()
 *
 * The WM8350 has a hardware lock which can be used to prevent writes to
 * some registers (generally those which can cause particularly serious
 * problems if misused).  This function disables that lock so updates
 * can be performed.  For maximum safety this should be done only when
 * required.
 */
int wm8350_reg_unlock(struct wm8350 *wm8350)
{
	u16 key = WM8350_UNLOCK_KEY;
	int ret;

	ldbg(__func__);
	mutex_lock(&io_mutex);
	ret = wm8350_write(wm8350, WM8350_SECURITY, 1, &key);
	if (ret)
		dev_err(wm8350->dev, "unlock failed\n");
	mutex_unlock(&io_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(wm8350_reg_unlock);

int wm8350_read_auxadc(struct wm8350 *wm8350, int channel, int scale, int vref)
{
	u16 reg, result = 0;

	if (channel < WM8350_AUXADC_AUX1 || channel > WM8350_AUXADC_TEMP)
		return -EINVAL;
	if (channel >= WM8350_AUXADC_USB && channel <= WM8350_AUXADC_TEMP
	    && (scale != 0 || vref != 0))
		return -EINVAL;

	mutex_lock(&wm8350->auxadc_mutex);

	/* Turn on the ADC */
	reg = wm8350_reg_read(wm8350, WM8350_POWER_MGMT_5);
	wm8350_reg_write(wm8350, WM8350_POWER_MGMT_5, reg | WM8350_AUXADC_ENA);

	if (scale || vref) {
		reg = scale << 13;
		reg |= vref << 12;
		wm8350_reg_write(wm8350, WM8350_AUX1_READBACK + channel, reg);
	}

	reg = wm8350_reg_read(wm8350, WM8350_DIGITISER_CONTROL_1);
	reg |= 1 << channel | WM8350_AUXADC_POLL;
	wm8350_reg_write(wm8350, WM8350_DIGITISER_CONTROL_1, reg);

	/* If a late IRQ left the completion signalled then consume
	 * the completion. */
	try_wait_for_completion(&wm8350->auxadc_done);

	/* We ignore the result of the completion and just check for a
	 * conversion result, allowing us to soldier on if the IRQ
	 * infrastructure is not set up for the chip. */
	wait_for_completion_timeout(&wm8350->auxadc_done, msecs_to_jiffies(5));

	reg = wm8350_reg_read(wm8350, WM8350_DIGITISER_CONTROL_1);
	if (reg & WM8350_AUXADC_POLL)
		dev_err(wm8350->dev, "adc chn %d read timeout\n", channel);
	else
		result = wm8350_reg_read(wm8350,
					 WM8350_AUX1_READBACK + channel);

	/* Turn off the ADC */
	reg = wm8350_reg_read(wm8350, WM8350_POWER_MGMT_5);
	wm8350_reg_write(wm8350, WM8350_POWER_MGMT_5,
			 reg & ~WM8350_AUXADC_ENA);

	mutex_unlock(&wm8350->auxadc_mutex);

	return result & WM8350_AUXADC_DATA1_MASK;
}
EXPORT_SYMBOL_GPL(wm8350_read_auxadc);

static irqreturn_t wm8350_auxadc_irq(int irq, void *irq_data)
{
	struct wm8350 *wm8350 = irq_data;

	complete(&wm8350->auxadc_done);

	return IRQ_HANDLED;
}

/*
 * Cache is always host endian.
 */
static int wm8350_create_cache(struct wm8350 *wm8350, int type, int mode)
{
	int i, ret = 0;
	u16 value;
	const u16 *reg_map;

	switch (type) {
	case 0:
		switch (mode) {
#ifdef CONFIG_MFD_WM8350_CONFIG_MODE_0
		case 0:
			reg_map = wm8350_mode0_defaults;
			break;
#endif
#ifdef CONFIG_MFD_WM8350_CONFIG_MODE_1
		case 1:
			reg_map = wm8350_mode1_defaults;
			break;
#endif
#ifdef CONFIG_MFD_WM8350_CONFIG_MODE_2
		case 2:
			reg_map = wm8350_mode2_defaults;
			break;
#endif
#ifdef CONFIG_MFD_WM8350_CONFIG_MODE_3
		case 3:
			reg_map = wm8350_mode3_defaults;
			break;
#endif
		default:
			dev_err(wm8350->dev,
				"WM8350 configuration mode %d not supported\n",
				mode);
			return -EINVAL;
		}
		break;

	case 1:
		switch (mode) {
#ifdef CONFIG_MFD_WM8351_CONFIG_MODE_0
		case 0:
			reg_map = wm8351_mode0_defaults;
			break;
#endif
#ifdef CONFIG_MFD_WM8351_CONFIG_MODE_1
		case 1:
			reg_map = wm8351_mode1_defaults;
			break;
#endif
#ifdef CONFIG_MFD_WM8351_CONFIG_MODE_2
		case 2:
			reg_map = wm8351_mode2_defaults;
			break;
#endif
#ifdef CONFIG_MFD_WM8351_CONFIG_MODE_3
		case 3:
			reg_map = wm8351_mode3_defaults;
			break;
#endif
		default:
			dev_err(wm8350->dev,
				"WM8351 configuration mode %d not supported\n",
				mode);
			return -EINVAL;
		}
		break;

	case 2:
		switch (mode) {
#ifdef CONFIG_MFD_WM8352_CONFIG_MODE_0
		case 0:
			reg_map = wm8352_mode0_defaults;
			break;
#endif
#ifdef CONFIG_MFD_WM8352_CONFIG_MODE_1
		case 1:
			reg_map = wm8352_mode1_defaults;
			break;
#endif
#ifdef CONFIG_MFD_WM8352_CONFIG_MODE_2
		case 2:
			reg_map = wm8352_mode2_defaults;
			break;
#endif
#ifdef CONFIG_MFD_WM8352_CONFIG_MODE_3
		case 3:
			reg_map = wm8352_mode3_defaults;
			break;
#endif
		default:
			dev_err(wm8350->dev,
				"WM8352 configuration mode %d not supported\n",
				mode);
			return -EINVAL;
		}
		break;

	default:
		dev_err(wm8350->dev,
			"WM835x configuration mode %d not supported\n",
			mode);
		return -EINVAL;
	}

	wm8350->reg_cache =
		kmalloc(sizeof(u16) * (WM8350_MAX_REGISTER + 1), GFP_KERNEL);
	if (wm8350->reg_cache == NULL)
		return -ENOMEM;

	/* Read the initial cache state back from the device - this is
	 * a PMIC so the device many not be in a virgin state and we
	 * can't rely on the silicon values.
	 */
	ret = regmap_raw_read(wm8350->regmap, 0, wm8350->reg_cache,
			      sizeof(u16) * (WM8350_MAX_REGISTER + 1));
	if (ret < 0) {
		dev_err(wm8350->dev,
			"failed to read initial cache values\n");
		goto out;
	}

	/* Mask out uncacheable/unreadable bits and the audio. */
	for (i = 0; i < WM8350_MAX_REGISTER; i++) {
		if (wm8350_reg_io_map[i].readable &&
		    (i < WM8350_CLOCK_CONTROL_1 || i > WM8350_AIF_TEST)) {
			value = be16_to_cpu(wm8350->reg_cache[i]);
			value &= wm8350_reg_io_map[i].readable;
			wm8350->reg_cache[i] = value;
		} else
			wm8350->reg_cache[i] = reg_map[i];
	}

out:
	kfree(wm8350->reg_cache);
	return ret;
}

/*
 * Register a client device.  This is non-fatal since there is no need to
 * fail the entire device init due to a single platform device failing.
 */
static void wm8350_client_dev_register(struct wm8350 *wm8350,
				       const char *name,
				       struct platform_device **pdev)
{
	int ret;

	*pdev = platform_device_alloc(name, -1);
	if (*pdev == NULL) {
		dev_err(wm8350->dev, "Failed to allocate %s\n", name);
		return;
	}

	(*pdev)->dev.parent = wm8350->dev;
	platform_set_drvdata(*pdev, wm8350);
	ret = platform_device_add(*pdev);
	if (ret != 0) {
		dev_err(wm8350->dev, "Failed to register %s: %d\n", name, ret);
		platform_device_put(*pdev);
		*pdev = NULL;
	}
}

int wm8350_device_init(struct wm8350 *wm8350, int irq,
		       struct wm8350_platform_data *pdata)
{
	int ret;
	unsigned int id1, id2, mask_rev;
	unsigned int cust_id, mode, chip_rev;

	dev_set_drvdata(wm8350->dev, wm8350);

	/* get WM8350 revision and config mode */
	ret = regmap_read(wm8350->regmap, WM8350_RESET_ID, &id1);
	if (ret != 0) {
		dev_err(wm8350->dev, "Failed to read ID: %d\n", ret);
		goto err;
	}

	ret = regmap_read(wm8350->regmap, WM8350_ID, &id2);
	if (ret != 0) {
		dev_err(wm8350->dev, "Failed to read ID: %d\n", ret);
		goto err;
	}

	ret = regmap_read(wm8350->regmap, WM8350_REVISION, &mask_rev);
	if (ret != 0) {
		dev_err(wm8350->dev, "Failed to read revision: %d\n", ret);
		goto err;
	}

	if (id1 != 0x6143) {
		dev_err(wm8350->dev,
			"Device with ID %x is not a WM8350\n", id1);
		ret = -ENODEV;
		goto err;
	}

	mode = id2 & WM8350_CONF_STS_MASK >> 10;
	cust_id = id2 & WM8350_CUST_ID_MASK;
	chip_rev = (id2 & WM8350_CHIP_REV_MASK) >> 12;
	dev_info(wm8350->dev,
		 "CONF_STS %d, CUST_ID %d, MASK_REV %d, CHIP_REV %d\n",
		 mode, cust_id, mask_rev, chip_rev);

	if (cust_id != 0) {
		dev_err(wm8350->dev, "Unsupported CUST_ID\n");
		ret = -ENODEV;
		goto err;
	}

	switch (mask_rev) {
	case 0:
		wm8350->pmic.max_dcdc = WM8350_DCDC_6;
		wm8350->pmic.max_isink = WM8350_ISINK_B;

		switch (chip_rev) {
		case WM8350_REV_E:
			dev_info(wm8350->dev, "WM8350 Rev E\n");
			break;
		case WM8350_REV_F:
			dev_info(wm8350->dev, "WM8350 Rev F\n");
			break;
		case WM8350_REV_G:
			dev_info(wm8350->dev, "WM8350 Rev G\n");
			wm8350->power.rev_g_coeff = 1;
			break;
		case WM8350_REV_H:
			dev_info(wm8350->dev, "WM8350 Rev H\n");
			wm8350->power.rev_g_coeff = 1;
			break;
		default:
			/* For safety we refuse to run on unknown hardware */
			dev_err(wm8350->dev, "Unknown WM8350 CHIP_REV\n");
			ret = -ENODEV;
			goto err;
		}
		break;

	case 1:
		wm8350->pmic.max_dcdc = WM8350_DCDC_4;
		wm8350->pmic.max_isink = WM8350_ISINK_A;

		switch (chip_rev) {
		case 0:
			dev_info(wm8350->dev, "WM8351 Rev A\n");
			wm8350->power.rev_g_coeff = 1;
			break;

		case 1:
			dev_info(wm8350->dev, "WM8351 Rev B\n");
			wm8350->power.rev_g_coeff = 1;
			break;

		default:
			dev_err(wm8350->dev, "Unknown WM8351 CHIP_REV\n");
			ret = -ENODEV;
			goto err;
		}
		break;

	case 2:
		wm8350->pmic.max_dcdc = WM8350_DCDC_6;
		wm8350->pmic.max_isink = WM8350_ISINK_B;

		switch (chip_rev) {
		case 0:
			dev_info(wm8350->dev, "WM8352 Rev A\n");
			wm8350->power.rev_g_coeff = 1;
			break;

		default:
			dev_err(wm8350->dev, "Unknown WM8352 CHIP_REV\n");
			ret = -ENODEV;
			goto err;
		}
		break;

	default:
		dev_err(wm8350->dev, "Unknown MASK_REV\n");
		ret = -ENODEV;
		goto err;
	}

	ret = wm8350_create_cache(wm8350, mask_rev, mode);
	if (ret < 0) {
		dev_err(wm8350->dev, "Failed to create register cache\n");
		return ret;
	}

	mutex_init(&wm8350->auxadc_mutex);
	init_completion(&wm8350->auxadc_done);

	ret = wm8350_irq_init(wm8350, irq, pdata);
	if (ret < 0)
		goto err_free;

	if (wm8350->irq_base) {
		ret = request_threaded_irq(wm8350->irq_base +
					   WM8350_IRQ_AUXADC_DATARDY,
					   NULL, wm8350_auxadc_irq, 0,
					   "auxadc", wm8350);
		if (ret < 0)
			dev_warn(wm8350->dev,
				 "Failed to request AUXADC IRQ: %d\n", ret);
	}

	if (pdata && pdata->init) {
		ret = pdata->init(wm8350);
		if (ret != 0) {
			dev_err(wm8350->dev, "Platform init() failed: %d\n",
				ret);
			goto err_irq;
		}
	}

	wm8350_reg_write(wm8350, WM8350_SYSTEM_INTERRUPTS_MASK, 0x0);

	wm8350_client_dev_register(wm8350, "wm8350-codec",
				   &(wm8350->codec.pdev));
	wm8350_client_dev_register(wm8350, "wm8350-gpio",
				   &(wm8350->gpio.pdev));
	wm8350_client_dev_register(wm8350, "wm8350-hwmon",
				   &(wm8350->hwmon.pdev));
	wm8350_client_dev_register(wm8350, "wm8350-power",
				   &(wm8350->power.pdev));
	wm8350_client_dev_register(wm8350, "wm8350-rtc", &(wm8350->rtc.pdev));
	wm8350_client_dev_register(wm8350, "wm8350-wdt", &(wm8350->wdt.pdev));

	return 0;

err_irq:
	wm8350_irq_exit(wm8350);
err_free:
	kfree(wm8350->reg_cache);
err:
	return ret;
}
EXPORT_SYMBOL_GPL(wm8350_device_init);

void wm8350_device_exit(struct wm8350 *wm8350)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wm8350->pmic.led); i++)
		platform_device_unregister(wm8350->pmic.led[i].pdev);

	for (i = 0; i < ARRAY_SIZE(wm8350->pmic.pdev); i++)
		platform_device_unregister(wm8350->pmic.pdev[i]);

	platform_device_unregister(wm8350->wdt.pdev);
	platform_device_unregister(wm8350->rtc.pdev);
	platform_device_unregister(wm8350->power.pdev);
	platform_device_unregister(wm8350->hwmon.pdev);
	platform_device_unregister(wm8350->gpio.pdev);
	platform_device_unregister(wm8350->codec.pdev);

	if (wm8350->irq_base)
		free_irq(wm8350->irq_base + WM8350_IRQ_AUXADC_DATARDY, wm8350);

	wm8350_irq_exit(wm8350);

	kfree(wm8350->reg_cache);
}
EXPORT_SYMBOL_GPL(wm8350_device_exit);

MODULE_DESCRIPTION("WM8350 AudioPlus PMIC core driver");
MODULE_LICENSE("GPL");
