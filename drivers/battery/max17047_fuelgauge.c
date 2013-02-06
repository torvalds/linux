/*
 * max17047_fuelgauge.c
 *
 * Copyright (C) 2011 Samsung Electronics
 * SangYoung Son <hello.son@samsung.com>
 *
 * based on max17040_battery.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/battery/samsung_battery.h>
#include <linux/battery/max17047_fuelgauge.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <plat/gpio-cfg.h>
#include <linux/rtc.h>
#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif
#endif

/* TRIM ERROR DETECTION */
#define USE_TRIM_ERROR_DETECTION

/* MAX17047 Registers. */
#define MAX17047_REG_STATUS		0x00
#define MAX17047_REG_VALRT_TH		0x01
#define MAX17047_REG_TALRT_TH		0x02
#define MAX17047_REG_SALRT_TH		0x03
#define MAX17047_REG_VCELL		0x09
#define MAX17047_REG_TEMPERATURE	0x08
#define MAX17047_REG_AVGVCELL		0x19
#define MAX17047_REG_CONFIG		0x1D
#define MAX17047_REG_VERSION		0x21
#define MAX17047_REG_LEARNCFG		0x28
#define MAX17047_REG_FILTERCFG		0x29
#define MAX17047_REG_MISCCFG		0x2B
#define MAX17047_REG_CGAIN		0x2E
#define MAX17047_REG_RCOMP		0x38
#define MAX17047_REG_VFOCV		0xFB
#define MAX17047_REG_SOC_VF		0xFF

/* Polling work */
#undef	DEBUG_FUELGAUGE_POLLING
#define MAX17047_POLLING_INTERVAL	10000

/* adjust full soc */
#define FULL_SOC_DEFAULT	9850
#define FULL_SOC_LOW		9700
#define FULL_SOC_HIGH		10000
#define KEEP_SOC_DEFAULT	50 /* 0.5% */

struct max17047_fuelgauge_data {
	struct i2c_client		*client;
	struct max17047_platform_data	*pdata;

	struct power_supply		fuelgauge;

	/* workqueue */
	struct delayed_work		update_work;
#ifdef DEBUG_FUELGAUGE_POLLING
	struct delayed_work		polling_work;
#endif

	/* mutex */
	struct mutex			irq_lock;

	/* wakelock */
	struct wake_lock		update_wake_lock;

	unsigned int			irq;

	unsigned int			vcell;
	unsigned int			avgvcell;
	unsigned int			vfocv;
	unsigned int			soc;
	unsigned int			rawsoc;
	unsigned int			temperature;

	/* adjust full soc */
	int				full_soc;

#ifdef USE_TRIM_ERROR_DETECTION
	/* trim error state */
	bool				trim_err;
#endif

#ifdef CONFIG_DEBUG_FS
	struct dentry			*fg_debugfs_dir;
#endif

#ifdef CONFIG_HIBERNATION
	u8 *reg_dump;
#endif

};

static int max17047_i2c_read(struct i2c_client *client, int reg, u8 *buf)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, 2, buf);
	if (ret < 0)
		pr_err("%s: err %d, reg: 0x%02x\n", __func__, ret, reg);

	return ret;
}

static int max17047_i2c_write(struct i2c_client *client, int reg, u8 *buf)
{
	int ret;

	ret = i2c_smbus_write_i2c_block_data(client, reg, 2, buf);
	if (ret < 0)
		pr_err("%s: err %d, reg: 0x%02x, data: 0x%x%x\n", __func__,
				ret, reg, buf[0], buf[1]);

	return ret;
}

static void max17047_test_read(struct max17047_fuelgauge_data *fg_data)
{
	int reg;
	u8 data[2];
	int i;
	u8 buf[673];

	struct timespec ts;
	struct rtc_time tm;
	pr_info("%s\n", __func__);

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);

	pr_info("%s: %d/%d/%d %02d:%02d\n", __func__,
					tm.tm_mday,
					tm.tm_mon + 1,
					tm.tm_year + 1900,
					tm.tm_hour,
					tm.tm_min);

	i = 0;
	for (reg = 0; reg < 0x50; reg++) {
		if (!(reg & 0xf))
			i += sprintf(buf + i, "\n%02x| ", reg);
		max17047_i2c_read(fg_data->client, reg, data);
		i += sprintf(buf + i, "%02x%02x ", data[1], data[0]);
	}
	for (reg = 0xe0; reg < 0x100; reg++) {
		if (!(reg & 0xf))
			i += sprintf(buf + i, "\n%02x| ", reg);
		max17047_i2c_read(fg_data->client, reg, data);
		i += sprintf(buf + i, "%02x%02x ", data[1], data[0]);
	}

	pr_info("    0    1    2    3    4    5    6    7");
	pr_cont("    8    9    a    b    c    d    e    f");
	pr_cont("%s\n", buf);
}

static int max17047_get_temperature(struct i2c_client *client)
{
	/* If temperature gauging from fuelgauge, implement here */

	return 300;
}

/* max17047_get_XXX(); Return current value and update data value */
static int max17047_get_vfocv(struct i2c_client *client)
{
	struct max17047_fuelgauge_data *fg_data = i2c_get_clientdata(client);
	u8 data[2];
	int ret;
	u32 vfocv;
	pr_debug("%s\n", __func__);

	ret = max17047_i2c_read(client, MAX17047_REG_VFOCV, data);
	if (ret < 0)
		return ret;

	vfocv = fg_data->vfocv = ((data[0] >> 3) + (data[1] << 5)) * 625 / 1000;

	pr_debug("%s: VFOCV(0x%02x%02x, %d)\n", __func__,
		 data[1], data[0], vfocv);
	return vfocv * 1000;
}

static int max17047_get_vcell(struct i2c_client *client)
{
	struct max17047_fuelgauge_data *fg_data = i2c_get_clientdata(client);
	u8 data[2];
	int ret;
	u32 vcell;
	pr_debug("%s\n", __func__);

	ret = max17047_i2c_read(client, MAX17047_REG_VCELL, data);
	if (ret < 0)
		return ret;

	vcell = fg_data->vcell = ((data[0] >> 3) + (data[1] << 5)) * 625;

	pr_debug("%s: VCELL(0x%02x%02x, %d)\n", __func__,
		 data[1], data[0], vcell);
	return vcell;
}

static int max17047_get_avgvcell(struct i2c_client *client)
{
	struct max17047_fuelgauge_data *fg_data = i2c_get_clientdata(client);
	u8 data[2];
	int ret;
	u32 avgvcell;
	pr_debug("%s\n", __func__);

	ret = max17047_i2c_read(client, MAX17047_REG_AVGVCELL, data);
	if (ret < 0)
		return ret;

	avgvcell = fg_data->avgvcell = ((data[0] >> 3) + (data[1] << 5)) * 625;

	pr_debug("%s: AVGVCELL(0x%02x%02x, %d)\n", __func__,
		 data[1], data[0], avgvcell);
	return avgvcell;
}

static int max17047_get_rawsoc(struct i2c_client *client)
{
	struct max17047_fuelgauge_data *fg_data = i2c_get_clientdata(client);
	u8 data[2];
	int ret;
	int rawsoc;
	pr_debug("%s\n", __func__);

	ret = max17047_i2c_read(client, MAX17047_REG_SOC_VF, data);
	if (ret < 0)
		return ret;

	rawsoc = fg_data->rawsoc = (data[1] * 100) + (data[0] * 100 / 256);

	pr_debug("%s: RAWSOC(0x%02x%02x, %d)\n", __func__,
		 data[1], data[0], rawsoc);
	return rawsoc;
}

static int max17047_get_soc(struct i2c_client *client)
{
	struct max17047_fuelgauge_data *fg_data = i2c_get_clientdata(client);
	int rawsoc, soc, fullsoc, empty;
	pr_debug("%s\n", __func__);

	rawsoc = max17047_get_rawsoc(fg_data->client);

#if defined(CONFIG_MACH_C1)
	empty = 0;
#else	/* M0, T0,,, */
	empty = 29;
#endif

	if (fg_data->full_soc <= 0)
		fg_data->full_soc = FULL_SOC_DEFAULT;
	fullsoc = fg_data->full_soc - empty;
	rawsoc -= empty;

/* adjust fullsoc value for fast termination */
#if defined(USE_2STEP_TERM) && !defined(CONFIG_TARGET_LOCALE_KOR)
	fullsoc *= 99;
	fullsoc /= 100;
#endif

	soc = fg_data->soc =
		((rawsoc < empty) ? 0 : (min((rawsoc * 100 / fullsoc), 100)));

	pr_info("%s: SOC(%d, %d / %d)\n", __func__, soc, rawsoc, fullsoc);
	return soc;
}

static void max17047_reset_soc(struct i2c_client *client)
{
	struct max17047_fuelgauge_data *fg_data =
				i2c_get_clientdata(client);
	u8 data[2];
	pr_info("%s: Before quick-start - "
		"VFOCV(%d), VFSOC(%d), SOC(%d)\n", __func__,
				max17047_get_vfocv(client),
				max17047_get_rawsoc(client),
				max17047_get_soc(client));
	max17047_test_read(fg_data);

	if (max17047_i2c_read(client, MAX17047_REG_MISCCFG, data) < 0)
		return;

	/* Set bit10 makes quick start */
	data[1] |= (0x1 << 2);
	max17047_i2c_write(client, MAX17047_REG_MISCCFG, data);

	msleep(500);

	pr_info("%s: After quick-start - "
		"VFOCV(%d), VFSOC(%d), SOC(%d)\n", __func__,
				max17047_get_vfocv(client),
				max17047_get_rawsoc(client),
				max17047_get_soc(client));
	max17047_test_read(fg_data);

	return;
}

static void max17047_adjust_fullsoc(struct i2c_client *client)
{
	struct max17047_fuelgauge_data *fg_data =
		 i2c_get_clientdata(client);
	int prev_full_soc = fg_data->full_soc;
	int temp_soc = max17047_get_rawsoc(fg_data->client);
	int keep_soc = 0;

	if (temp_soc < 0) {
		pr_err("%s : fg data error!(%d)\n", __func__, temp_soc);
		fg_data->full_soc = FULL_SOC_DEFAULT;
		return;
	}

	if (temp_soc < FULL_SOC_LOW)
		fg_data->full_soc = FULL_SOC_LOW;
	else if (temp_soc > FULL_SOC_HIGH) {
		keep_soc = FULL_SOC_HIGH / 100;
		fg_data->full_soc = (FULL_SOC_HIGH - keep_soc);
	} else {
		keep_soc = temp_soc / 100;
		if (temp_soc > (FULL_SOC_LOW + keep_soc))
			fg_data->full_soc = temp_soc - keep_soc;
		else
			fg_data->full_soc = FULL_SOC_LOW;
	}

	if (prev_full_soc != fg_data->full_soc)
		pr_info("%s : p_full_soc(%d), full_soc(%d), keep_soc(%d)\n",
			__func__, prev_full_soc, fg_data->full_soc, keep_soc);
}

/* SOC% alert, disabled(0xFF00) */
static void max17047_set_salrt(struct max17047_fuelgauge_data *fg_data,
								u8 min, u8 max)
{
	struct i2c_client *client = fg_data->client;
	u8 i2c_data[2];
	pr_info("%s: min(%d%%), max(%d%%)\n", __func__, min, max);

	i2c_data[1] = max;
	i2c_data[0] = min;
	max17047_i2c_write(client, MAX17047_REG_SALRT_TH, i2c_data);

	max17047_i2c_read(client, MAX17047_REG_SALRT_TH, i2c_data);
	if ((i2c_data[0] != min) || (i2c_data[1] != max))
		pr_err("%s: SALRT_TH is not valid (0x%02d%02d ? 0x%02d%02d)\n",
			__func__, i2c_data[1], i2c_data[0], max, min);
}

/* Temperature alert, disabled(0x7F80) */
static void max17047_set_talrt(struct max17047_fuelgauge_data *fg_data,
								u8 min, u8 max)
{
	struct i2c_client *client = fg_data->client;
	u8 i2c_data[2];
	pr_info("%s: min(0x%02x), max(0x%02x)\n", __func__, min, max);

	i2c_data[1] = max;
	i2c_data[0] = min;
	max17047_i2c_write(client, MAX17047_REG_TALRT_TH, i2c_data);

	max17047_i2c_read(client, MAX17047_REG_TALRT_TH, i2c_data);
	if ((i2c_data[0] != min) || (i2c_data[1] != max))
		pr_err("%s: TALRT_TH is not valid (0x%02d%02d ? 0x%02d%02d)\n",
			__func__, i2c_data[1], i2c_data[0], max, min);
}

/* Voltage alert, disabled(0xFF00) */
static void max17047_set_valrt(struct max17047_fuelgauge_data *fg_data,
								u8 min, u8 max)
{
	struct i2c_client *client = fg_data->client;
	u8 i2c_data[2];
	pr_info("%s: min(%dmV), max(%dmV)\n", __func__, min * 20, max * 20);

	i2c_data[1] = max;
	i2c_data[0] = min;
	max17047_i2c_write(client, MAX17047_REG_VALRT_TH, i2c_data);

	max17047_i2c_read(client, MAX17047_REG_VALRT_TH, i2c_data);
	if ((i2c_data[0] != min) || (i2c_data[1] != max))
		pr_err("%s: VALRT_TH is not valid (0x%02d%02d ? 0x%02d%02d)\n",
			__func__, i2c_data[1], i2c_data[0], max, min);
}

static void max17047_alert_init(struct max17047_fuelgauge_data *fg_data)
{
	struct i2c_client *client = fg_data->client;
	u8 i2c_data[2];
	pr_debug("%s\n", __func__);

	/* SALRT Threshold setting */
	/* min 1%, max disable */
	max17047_set_salrt(fg_data, 0x01, 0xFF);

	/* TALRT Threshold setting */
	/* min disable, max disable */
	max17047_set_talrt(fg_data, 0x80, 0x7F);

	/* VALRT Threshold setting */
	/* min disable, max disable */
	max17047_set_valrt(fg_data, 0x00, 0xFF);

	/* Enable SOC alerts */
	max17047_i2c_read(client, MAX17047_REG_CONFIG, i2c_data);
	i2c_data[0] |= (0x1 << 2);
	max17047_i2c_write(client, MAX17047_REG_CONFIG, i2c_data);
}

static void max17047_reg_init(struct max17047_fuelgauge_data *fg_data)
{
	struct i2c_client *client = fg_data->client;
	u8 i2c_data[2];
	pr_debug("%s\n", __func__);

	i2c_data[1] = 0x00;
	i2c_data[0] = 0x00;
	max17047_i2c_write(client, MAX17047_REG_CGAIN, i2c_data);

	i2c_data[1] = 0x00;
	i2c_data[0] = 0x03;
	max17047_i2c_write(client, MAX17047_REG_MISCCFG, i2c_data);

	i2c_data[1] = 0x07;
	i2c_data[0] = 0x00;
	max17047_i2c_write(client, MAX17047_REG_LEARNCFG, i2c_data);
}

static void max17047_update_work(struct work_struct *work)
{
	struct max17047_fuelgauge_data *fg_data = container_of(work,
						struct max17047_fuelgauge_data,
						update_work.work);
	struct power_supply *battery_psy;
	struct i2c_client *client = fg_data->client;
	union power_supply_propval value;
	pr_debug("%s\n", __func__);

#ifdef CONFIG_SLP
	battery_psy = &fg_data->fuelgauge;
#else
	battery_psy = power_supply_get_by_name("battery");
#endif

	max17047_get_vcell(client);
	max17047_get_vfocv(client);
	max17047_get_avgvcell(client);
	max17047_get_rawsoc(client);
	max17047_get_soc(client);

	pr_info("%s: VCELL(%d), VFOCV(%d), AVGVCELL(%d), RAWSOC(%d), SOC(%d)\n",
					__func__, fg_data->vcell,
					fg_data->vfocv, fg_data->avgvcell,
					fg_data->rawsoc, fg_data->soc);

	max17047_test_read(fg_data);

	if (!battery_psy || !battery_psy->set_property) {
		pr_err("%s: fail to get battery power supply\n", __func__);
		return;
	}

	battery_psy->set_property(battery_psy,
				POWER_SUPPLY_PROP_STATUS,
				&value);

	wake_lock_timeout(&fg_data->update_wake_lock, HZ);
}

#ifdef DEBUG_FUELGAUGE_POLLING
static void max17047_polling_work(struct work_struct *work)
{
	struct max17047_fuelgauge_data *fg_data = container_of(work,
						struct max17047_fuelgauge_data,
						polling_work.work);
	int reg;
	int i;
	u8 data[2];
	u8 buf[512];

	max17047_get_vcell(fg_data->client);
	max17047_get_vfocv(fg_data->client);
	max17047_get_avgvcell(fg_data->client);
	max17047_get_rawsoc(fg_data->client);
	max17047_get_soc(fg_data->client);

	pr_info("%s: VCELL(%d), VFOCV(%d), AVGVCELL(%d), RAWSOC(%d), SOC(%d)\n",
					__func__, fg_data->vcell,
					fg_data->vfocv, fg_data->avgvcell,
					fg_data->rawsoc, fg_data->soc);

	max17047_test_read(fg_data);

	schedule_delayed_work(&fg_data->polling_work,
		msecs_to_jiffies(MAX17047_POLLING_INTERVAL));
}
#endif

static enum power_supply_property max17047_fuelgauge_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
};

#ifdef USE_TRIM_ERROR_DETECTION
/* Temp: Init max17047 sample has trim value error. For detecting that. */
#define TRIM_ERROR_DETECT_VOLTAGE1	2500000
#define TRIM_ERROR_DETECT_VOLTAGE2	3600000
static bool max17047_detect_trim_error(struct max17047_fuelgauge_data *fg_data)
{
	bool ret = false;
	int vcell, soc;

	vcell = max17047_get_vcell(fg_data->client);
	soc = max17047_get_soc(fg_data->client);

	if (((vcell < TRIM_ERROR_DETECT_VOLTAGE1) ||
		(vcell == TRIM_ERROR_DETECT_VOLTAGE2)) && (soc == 0)) {
		pr_err("%s: (maybe)It's a trim error version. "
			"VCELL(%d), SOC(%d)\n", __func__, vcell, soc);
		ret = true;
	}

	return ret;
}
#endif

static int max17047_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max17047_fuelgauge_data *fg_data = container_of(psy,
						  struct max17047_fuelgauge_data,
						  fuelgauge);

#ifdef USE_TRIM_ERROR_DETECTION
	if (fg_data->trim_err == true) {
		switch (psp) {
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		case POWER_SUPPLY_PROP_VOLTAGE_AVG:
			val->intval = 4200000;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = 99;
			break;
		case POWER_SUPPLY_PROP_TEMP:
			val->intval = 300;
			break;
		default:
			return -EINVAL;
		}

		return 0;
	}
#endif

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		switch (val->intval) {
		case VOLTAGE_TYPE_VCELL:
			val->intval = max17047_get_vcell(fg_data->client);
			break;
		case VOLTAGE_TYPE_VFOCV:
			val->intval = max17047_get_vfocv(fg_data->client);
			break;
		default:
			val->intval = max17047_get_vcell(fg_data->client);
			break;
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		val->intval = max17047_get_avgvcell(fg_data->client);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		switch (val->intval) {
		case SOC_TYPE_ADJUSTED:
			val->intval = max17047_get_soc(fg_data->client);
			break;
		case SOC_TYPE_RAW:
			val->intval = max17047_get_rawsoc(fg_data->client);
			break;
		case SOC_TYPE_FULL:
			val->intval = fg_data->full_soc;
			break;
		default:
			val->intval = max17047_get_soc(fg_data->client);
			break;
		}
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = max17047_get_temperature(fg_data->client);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int max17047_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct max17047_fuelgauge_data *fg_data = container_of(psy,
					  struct max17047_fuelgauge_data,
					  fuelgauge);

	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY:
		max17047_reset_soc(fg_data->client);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (val->intval != POWER_SUPPLY_STATUS_FULL)
			return -EINVAL;
		pr_info("%s: charger full state!\n", __func__);
		/* adjust full soc */
		max17047_adjust_fullsoc(fg_data->client);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static irqreturn_t max17047_fuelgauge_isr(int irq, void *data)
{
	struct max17047_fuelgauge_data *fg_data = data;
	struct i2c_client *client = fg_data->client;
	u8 i2c_data[2];
	pr_info("%s: irq(%d)\n", __func__, irq);
	mutex_lock(&fg_data->irq_lock);

	max17047_i2c_read(client, MAX17047_REG_STATUS, i2c_data);
	pr_info("%s: MAX17047_REG_STATUS(0x%02x%02x)\n", __func__,
					i2c_data[1], i2c_data[0]);

	cancel_delayed_work(&fg_data->update_work);
	wake_lock(&fg_data->update_wake_lock);
	schedule_delayed_work(&fg_data->update_work, msecs_to_jiffies(1000));

	mutex_unlock(&fg_data->irq_lock);
	return IRQ_HANDLED;
}

#if defined(CONFIG_TARGET_LOCALE_KOR)
#ifdef CONFIG_DEBUG_FS
static int max17047_debugfs_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t max17047_debugfs_read_registers(struct file *filp,
	char __user *buffer, size_t count, loff_t *ppos)
{
	struct max17047_fuelgauge_data *fg_data = filp->private_data;
	struct i2c_client *client = NULL;
	u8 i2c_data[2];
	int reg = 0;
	char *buf;
	size_t len = 0;
	ssize_t ret;

	if (!fg_data) {
		pr_err("%s : fg_data is null\n", __func__);
		return 0;
	}

	client = fg_data->client;

	if (*ppos != 0)
		return 0;

	if (count < sizeof(buf))
		return -ENOSPC;

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	reg = MAX17047_REG_STATUS;
	max17047_i2c_read(client, reg, i2c_data);
	len += snprintf(buf + len, PAGE_SIZE - len,
		"status(0x%x)=%02x%02x ", reg, i2c_data[1], i2c_data[0]);

	reg = MAX17047_REG_CONFIG;
	max17047_i2c_read(client, reg, i2c_data);
	len += snprintf(buf + len, PAGE_SIZE - len,
		"config(0x%x)=%02x%02x ", reg, i2c_data[1], i2c_data[0]);

	reg = MAX17047_REG_RCOMP;
	max17047_i2c_read(client, reg, i2c_data);
	len += snprintf(buf + len, PAGE_SIZE - len,
		"rcomp(0x%x)=%02x%02x ", reg, i2c_data[1], i2c_data[0]);

	reg = MAX17047_REG_CGAIN;
	max17047_i2c_read(client, reg, i2c_data);
	len += snprintf(buf + len, PAGE_SIZE - len,
		"cgain(0x%x)=%02x%02x ", reg, i2c_data[1], i2c_data[0]);

	reg = MAX17047_REG_SALRT_TH;
	max17047_i2c_read(client, reg, i2c_data);
	len += snprintf(buf + len, PAGE_SIZE - len,
		"salrt(0x%x)=%02x%02x ", reg, i2c_data[1], i2c_data[0]);

	reg = MAX17047_REG_MISCCFG;
	max17047_i2c_read(client, reg, i2c_data);
	len += snprintf(buf + len, PAGE_SIZE - len,
		"misc(0x%x)=%02x%02x ", reg, i2c_data[1], i2c_data[0]);

	reg = 0x39;
	max17047_i2c_read(client, reg, i2c_data);
	len += snprintf(buf + len, PAGE_SIZE - len,
		"tempc0(0x%x)=%02x%02x ", reg, i2c_data[1], i2c_data[0]);

	reg = 0x0F;
	max17047_i2c_read(client, reg, i2c_data);
	len += snprintf(buf + len, PAGE_SIZE - len,
		"remCap(0x%x)=%02x%02x ", reg, i2c_data[1], i2c_data[0]);

	reg = 0x10;
	max17047_i2c_read(client, reg, i2c_data);
	len += snprintf(buf + len, PAGE_SIZE - len,
		"fullCap(0x%x)=%02x%02x ", reg, i2c_data[1], i2c_data[0]);

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	ret = simple_read_from_buffer(buffer, len, ppos, buf, PAGE_SIZE);
	kfree(buf);

	return ret;
}

static const struct file_operations max17047_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = max17047_debugfs_open,
	.read = max17047_debugfs_read_registers,
};

static ssize_t max17047_debugfs_read_defaultdata(struct file *filp,
	char __user *buffer, size_t count, loff_t *ppos)
{
	struct max17047_fuelgauge_data *fg_data = filp->private_data;
	struct i2c_client *client = NULL;
	u8 i2c_data[2];
	int reg = 0;
	char *buf;
	size_t len = 0;
	ssize_t ret;

	if (!fg_data) {
		pr_err("%s : fg_data is null\n", __func__);
		return 0;
	}

	client = fg_data->client;

	if (*ppos != 0)
		return 0;

	if (count < sizeof(buf))
		return -ENOSPC;

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	reg = MAX17047_REG_RCOMP;
	max17047_i2c_read(client, reg, i2c_data);
	len += snprintf(buf + len, PAGE_SIZE - len,
		"rcomp=%02x%02x ", i2c_data[1], i2c_data[0]);

	len += snprintf(buf + len, PAGE_SIZE - len,
		"fsoc=%d", fg_data->full_soc);

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	ret = simple_read_from_buffer(buffer, len, ppos, buf, PAGE_SIZE);
	kfree(buf);

	return ret;
}

static const struct file_operations max17047_debugfs_fops2 = {
	.owner = THIS_MODULE,
	.open = max17047_debugfs_open,
	.read = max17047_debugfs_read_defaultdata,
};
#endif
#endif

static int __devinit max17047_fuelgauge_i2c_probe(struct i2c_client *client,
						  const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct max17047_fuelgauge_data *fg_data;
	struct max17047_platform_data *pdata = client->dev.platform_data;
	int ret = -ENODEV;
	int rawsoc, firstsoc;
	pr_info("%s: fuelgauge init\n", __func__);

	if (!pdata) {
		pr_err("%s: no platform data\n", __func__);
		return -ENODEV;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	fg_data = kzalloc(sizeof(struct max17047_fuelgauge_data), GFP_KERNEL);
	if (!fg_data)
		return -ENOMEM;

	fg_data->client = client;
	fg_data->pdata = pdata;

	i2c_set_clientdata(client, fg_data);

	mutex_init(&fg_data->irq_lock);

	wake_lock_init(&fg_data->update_wake_lock, WAKE_LOCK_SUSPEND,
							       "fuel-update");

#ifdef USE_TRIM_ERROR_DETECTION
	/* trim error detect */
	fg_data->trim_err = max17047_detect_trim_error(fg_data);
#endif

	/* Initialize full_soc, set this before fisrt SOC reading */
	fg_data->full_soc = FULL_SOC_DEFAULT;
	/* first full_soc update */
	rawsoc = max17047_get_rawsoc(fg_data->client);
	if (rawsoc > FULL_SOC_DEFAULT)
		max17047_adjust_fullsoc(client);
	firstsoc = max17047_get_soc(client);
	pr_info("%s: rsoc=%d, fsoc=%d, soc=%d\n", __func__,
			rawsoc, fg_data->full_soc, firstsoc);

	if (fg_data->pdata->psy_name)
		fg_data->fuelgauge.name =
			fg_data->pdata->psy_name;
	else
		fg_data->fuelgauge.name = "max17047-fuelgauge";

	fg_data->fuelgauge.type = POWER_SUPPLY_TYPE_BATTERY;
	fg_data->fuelgauge.properties = max17047_fuelgauge_props;
	fg_data->fuelgauge.num_properties =
				ARRAY_SIZE(max17047_fuelgauge_props);
	fg_data->fuelgauge.get_property = max17047_get_property;
	fg_data->fuelgauge.set_property = max17047_set_property;

	ret = power_supply_register(&client->dev, &fg_data->fuelgauge);
	if (ret) {
		pr_err("%s: failed power supply register\n", __func__);
		goto err_psy_reg_fg;
	}

	/* Initialize fuelgauge registers */
	max17047_reg_init(fg_data);

	/* Initialize fuelgauge alert */
	max17047_alert_init(fg_data);

	INIT_DELAYED_WORK_DEFERRABLE(&fg_data->update_work,
					max17047_update_work);

	/* Request IRQ */
	fg_data->irq = gpio_to_irq(fg_data->pdata->irq_gpio);
	ret = gpio_request(fg_data->pdata->irq_gpio, "fuelgauge-irq");
	if (ret) {
		pr_err("%s: failed requesting gpio %d\n", __func__,
				fg_data->pdata->irq_gpio);
		goto err_irq;
	}
	gpio_direction_input(fg_data->pdata->irq_gpio);
	gpio_free(fg_data->pdata->irq_gpio);

	ret = request_threaded_irq(fg_data->irq, NULL,
				max17047_fuelgauge_isr, IRQF_TRIGGER_FALLING,
				"max17047-alert", fg_data);
	if (ret < 0) {
		pr_err("%s: fail to request max17047 irq: %d: %d\n",
				__func__, fg_data->irq, ret);
		goto err_irq;
	}

	ret = enable_irq_wake(fg_data->irq);
	if (ret < 0) {
		pr_err("%s: failed enable irq wake %d\n", __func__,
						fg_data->irq);
		goto err_enable_irq;
	}

#ifdef DEBUG_FUELGAUGE_POLLING
	INIT_DELAYED_WORK_DEFERRABLE(&fg_data->polling_work,
					max17047_polling_work);
	schedule_delayed_work(&fg_data->polling_work, 0);
#else
	max17047_test_read(fg_data);
#endif

	pr_info("%s: probe complete\n", __func__);

#if defined(CONFIG_TARGET_LOCALE_KOR)
#ifdef CONFIG_DEBUG_FS
	fg_data->fg_debugfs_dir =
		debugfs_create_dir("fg_debug", NULL);
	if (fg_data->fg_debugfs_dir) {
		if (!debugfs_create_file("max17047_regs", 0644,
			fg_data->fg_debugfs_dir,
			fg_data, &max17047_debugfs_fops))
			pr_err("%s : debugfs_create_file, error\n", __func__);
		if (!debugfs_create_file("default_data", 0644,
			fg_data->fg_debugfs_dir,
			fg_data, &max17047_debugfs_fops2))
			pr_err("%s : debugfs_create_file2, error\n", __func__);
	} else
		pr_err("%s : debugfs_create_dir, error\n", __func__);
#endif
#endif

	return 0;

err_enable_irq:
	free_irq(fg_data->irq, fg_data);
err_irq:
	power_supply_unregister(&fg_data->fuelgauge);
err_psy_reg_fg:
	wake_lock_destroy(&fg_data->update_wake_lock);
	mutex_destroy(&fg_data->irq_lock);
	kfree(fg_data);
	return ret;
}

static int __devexit max17047_fuelgauge_remove(struct i2c_client *client)
{
	struct max17047_fuelgauge_data *fg_data = i2c_get_clientdata(client);

	wake_lock_destroy(&fg_data->update_wake_lock);
	free_irq(fg_data->irq, fg_data);
	power_supply_unregister(&fg_data->fuelgauge);
#ifdef DEBUG_FUELGAUGE_POLLING
	cancel_delayed_work(&fg_data->polling_work);
#endif
	cancel_delayed_work(&fg_data->update_work);
	mutex_destroy(&fg_data->irq_lock);
	kfree(fg_data);

	return 0;
}

#ifdef CONFIG_PM
static int max17047_fuelgauge_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max17047_fuelgauge_data *fg_data = i2c_get_clientdata(client);
	struct power_supply *psy = power_supply_get_by_name("battery");
	union power_supply_propval value;
	int charge_state, voltage_max, voltage_min;
	int valrt_vol;
	pr_info("%s\n", __func__);

#ifdef DEBUG_FUELGAUGE_POLLING
	cancel_delayed_work(&fg_data->polling_work);
#endif

#if !defined(CONFIG_SLP)
	/* default disable */
	valrt_vol = 0;

	/* voltage alert recharge voltage */
	if (!psy) {
		pr_err("%s: fail to get battery psy\n", __func__);
		return 0;
	}
	psy->get_property(psy, POWER_SUPPLY_PROP_STATUS, &value);
	charge_state = value.intval;

	if (charge_state == POWER_SUPPLY_STATUS_FULL) {
		psy->get_property(psy, POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
									&value);
		voltage_max = value.intval;

		/* valrt voltage set as recharge voltage */
		valrt_vol = voltage_max - RECHG_DROP_VALUE;
	} else {
		psy->get_property(psy, POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
									&value);
		voltage_min = value.intval;

		/* valrt voltage set as min voltage - 50mV */
		valrt_vol = voltage_min - 50000;
	}

	pr_info("%s: charge state(%d), vcell(%d), valrt(%d)\n", __func__,
				charge_state, fg_data->vcell, valrt_vol);

	/* set voltage alert */
	max17047_set_valrt(fg_data, (valrt_vol / 1000 / 20), 0xFF);
#endif

	return 0;
}

static int max17047_fuelgauge_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max17047_fuelgauge_data *fg_data = i2c_get_clientdata(client);
	pr_info("%s\n", __func__);

#if !defined(CONFIG_SLP)
	/* min disable, max disable */
	max17047_set_valrt(fg_data, 0x00, 0xFF);
#endif

#ifdef DEBUG_FUELGAUGE_POLLING
	schedule_delayed_work(&fg_data->polling_work, 0);
#endif

	return 0;
}
#else
#define max17047_fuelgauge_suspend NULL
#define max17047_fuelgauge_resume NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id max17047_fuelgauge_id[] = {
	{"max17047-fuelgauge", 0},
	{}
};

#ifdef CONFIG_HIBERNATION
static const u16 save_addr[] = {
	MAX17047_REG_VALRT_TH,
	MAX17047_REG_TALRT_TH,
	MAX17047_REG_SALRT_TH,

	MAX17047_REG_TEMPERATURE,
	MAX17047_REG_CONFIG,

	MAX17047_REG_LEARNCFG,
	MAX17047_REG_FILTERCFG,
	MAX17047_REG_MISCCFG,
	MAX17047_REG_CGAIN,
	MAX17047_REG_RCOMP,
	MAX17047_REG_SOC_VF,
};


static int max17047_freeze(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max17047_fuelgauge_data *fg_data
						= i2c_get_clientdata(client);
	int i, j;

	if (fg_data->reg_dump) {
		dev_err(dev, "Register dump is not clean.\n");
		return -EINVAL;
	}

	fg_data->reg_dump = kzalloc(sizeof(u16) * ARRAY_SIZE(save_addr),
				 GFP_KERNEL);
	if (!fg_data->reg_dump) {
		dev_err(dev, "Cannot allocate memory for hibernation dump.\n");
		return -ENOMEM;
	}

	for (i = 0, j = 0; i < ARRAY_SIZE(save_addr); i++, j += 2)
		max17047_i2c_read(client, save_addr[i]
					, &(fg_data->reg_dump[j]));

	return 0;
}

static int max17047_restore(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max17047_fuelgauge_data *fg_data
						= i2c_get_clientdata(client);
	int i, j;

	if (!fg_data->reg_dump) {
		dev_err(dev, "Cannot allocate memory for hibernation dump.\n");
		return -ENOMEM;
	}

	for (i = 0, j = 0; i < ARRAY_SIZE(save_addr); i++, j += 2)
		max17047_i2c_write(client, save_addr[i]
					, &(fg_data->reg_dump[j]));

	kfree(fg_data->reg_dump);
	fg_data->reg_dump = NULL;

	return 0;
}
#endif



#ifdef CONFIG_PM
const struct dev_pm_ops max17047_pm = {
	.suspend = max17047_fuelgauge_suspend,
	.resume = max17047_fuelgauge_resume,
#ifdef CONFIG_HIBERNATION
	.freeze = max17047_freeze,
	.thaw = max17047_restore,
	.restore = max17047_restore,
#endif
};
#endif


MODULE_DEVICE_TABLE(i2c, max17047_fuelgauge_id);

static struct i2c_driver max17047_i2c_driver = {
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "max17047-fuelgauge",
		.pm = &max17047_pm,

	},
	.probe		= max17047_fuelgauge_i2c_probe,
	.remove		= __devexit_p(max17047_fuelgauge_remove),
	.id_table	= max17047_fuelgauge_id,
};

static int __init max17047_fuelgauge_init(void)
{
	return i2c_add_driver(&max17047_i2c_driver);
}

static void __exit max17047_fuelgauge_exit(void)
{
	i2c_del_driver(&max17047_i2c_driver);
}

module_init(max17047_fuelgauge_init);
module_exit(max17047_fuelgauge_exit);

MODULE_AUTHOR("SangYoung Son <hello.son@samsung.com>");
MODULE_DESCRIPTION("max17047 Fuel gauge driver");
MODULE_LICENSE("GPL");
