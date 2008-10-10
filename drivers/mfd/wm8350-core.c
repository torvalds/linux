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
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <linux/mfd/wm8350/core.h>
#include <linux/mfd/wm8350/audio.h>
#include <linux/mfd/wm8350/gpio.h>
#include <linux/mfd/wm8350/pmic.h>
#include <linux/mfd/wm8350/supply.h>

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
static DEFINE_MUTEX(auxadc_mutex);

/* Perform a physical read from the device.
 */
static int wm8350_phys_read(struct wm8350 *wm8350, u8 reg, int num_regs,
			    u16 *dest)
{
	int i, ret;
	int bytes = num_regs * 2;

	dev_dbg(wm8350->dev, "volatile read\n");
	ret = wm8350->read_dev(wm8350, reg, bytes, (char *)dest);

	for (i = reg; i < reg + num_regs; i++) {
		/* Cache is CPU endian */
		dest[i - reg] = be16_to_cpu(dest[i - reg]);

		/* Satisfy non-volatile bits from cache */
		dest[i - reg] &= wm8350_reg_io_map[i].vol;
		dest[i - reg] |= wm8350->reg_cache[i];

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

	if (wm8350->read_dev == NULL)
		return -ENODEV;

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

	if ((reg == WM8350_GPIO_CONFIGURATION_I_O) ||
	    (reg >= WM8350_GPIO_FUNCTION_SELECT_1 &&
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

	if (wm8350->write_dev == NULL)
		return -ENODEV;

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
	return wm8350->write_dev(wm8350, reg, bytes, (char *)src);
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

/*
 * Cache is always host endian.
 */
static int wm8350_create_cache(struct wm8350 *wm8350, int mode)
{
	int i, ret = 0;
	u16 value;
	const u16 *reg_map;

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
		dev_err(wm8350->dev, "Configuration mode %d not supported\n",
			mode);
		return -EINVAL;
	}

	wm8350->reg_cache =
	    kzalloc(sizeof(u16) * (WM8350_MAX_REGISTER + 1), GFP_KERNEL);
	if (wm8350->reg_cache == NULL)
		return -ENOMEM;

	/* Read the initial cache state back from the device - this is
	 * a PMIC so the device many not be in a virgin state and we
	 * can't rely on the silicon values.
	 */
	for (i = 0; i < WM8350_MAX_REGISTER; i++) {
		/* audio register range */
		if (wm8350_reg_io_map[i].readable &&
		    (i < WM8350_CLOCK_CONTROL_1 || i > WM8350_AIF_TEST)) {
			ret = wm8350->read_dev(wm8350, i, 2, (char *)&value);
			if (ret < 0) {
				dev_err(wm8350->dev,
				       "failed to read initial cache value\n");
				goto out;
			}
			value = be16_to_cpu(value);
			value &= wm8350_reg_io_map[i].readable;
			wm8350->reg_cache[i] = value;
		} else
			wm8350->reg_cache[i] = reg_map[i];
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(wm8350_create_cache);

int wm8350_device_init(struct wm8350 *wm8350)
{
	int ret = -EINVAL;
	u16 id1, id2, mask, mode;

	/* get WM8350 revision and config mode */
	wm8350->read_dev(wm8350, WM8350_RESET_ID, sizeof(id1), &id1);
	wm8350->read_dev(wm8350, WM8350_ID, sizeof(id2), &id2);

	id1 = be16_to_cpu(id1);
	id2 = be16_to_cpu(id2);

	if (id1 == 0x0)
		dev_info(wm8350->dev, "Found Rev C device\n");
	else if (id1 == 0x6143) {
		switch ((id2 & WM8350_CHIP_REV_MASK) >> 12) {
		case WM8350_REV_E:
			dev_info(wm8350->dev, "Found Rev E device\n");
			wm8350->rev = WM8350_REV_E;
			break;
		case WM8350_REV_F:
			dev_info(wm8350->dev, "Found Rev F device\n");
			wm8350->rev = WM8350_REV_F;
			break;
		case WM8350_REV_G:
			dev_info(wm8350->dev, "Found Rev G device\n");
			wm8350->rev = WM8350_REV_G;
			break;
		default:
			/* For safety we refuse to run on unknown hardware */
			dev_info(wm8350->dev, "Found unknown rev\n");
			ret = -ENODEV;
			goto err;
		}
	} else {
		dev_info(wm8350->dev, "Device with ID %x is not a WM8350\n",
			 id1);
		ret = -ENODEV;
		goto err;
	}

	mode = id2 & WM8350_CONF_STS_MASK >> 10;
	mask = id2 & WM8350_CUST_ID_MASK;
	dev_info(wm8350->dev, "Config mode %d, ROM mask %d\n", mode, mask);

	ret = wm8350_create_cache(wm8350, mode);
	if (ret < 0) {
		printk(KERN_ERR "wm8350: failed to create register cache\n");
		return ret;
	}

	return 0;

err:
	kfree(wm8350->reg_cache);
	return ret;
}
EXPORT_SYMBOL_GPL(wm8350_device_init);

void wm8350_device_exit(struct wm8350 *wm8350)
{
	kfree(wm8350->reg_cache);
}
EXPORT_SYMBOL_GPL(wm8350_device_exit);

MODULE_LICENSE("GPL");
