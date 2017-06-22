/*
 * axp288_fuel_gauge.c - Xpower AXP288 PMIC Fuel Gauge Driver
 *
 * Copyright (C) 2014 Intel Corporation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/mfd/axp20x.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/iio/consumer.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <asm/unaligned.h>

#define CHRG_STAT_BAT_SAFE_MODE		(1 << 3)
#define CHRG_STAT_BAT_VALID			(1 << 4)
#define CHRG_STAT_BAT_PRESENT		(1 << 5)
#define CHRG_STAT_CHARGING			(1 << 6)
#define CHRG_STAT_PMIC_OTP			(1 << 7)

#define CHRG_CCCV_CC_MASK			0xf     /* 4 bits */
#define CHRG_CCCV_CC_BIT_POS		0
#define CHRG_CCCV_CC_OFFSET			200     /* 200mA */
#define CHRG_CCCV_CC_LSB_RES		200     /* 200mA */
#define CHRG_CCCV_ITERM_20P			(1 << 4)    /* 20% of CC */
#define CHRG_CCCV_CV_MASK			0x60        /* 2 bits */
#define CHRG_CCCV_CV_BIT_POS		5
#define CHRG_CCCV_CV_4100MV			0x0     /* 4.10V */
#define CHRG_CCCV_CV_4150MV			0x1     /* 4.15V */
#define CHRG_CCCV_CV_4200MV			0x2     /* 4.20V */
#define CHRG_CCCV_CV_4350MV			0x3     /* 4.35V */
#define CHRG_CCCV_CHG_EN			(1 << 7)

#define FG_CNTL_OCV_ADJ_STAT		(1 << 2)
#define FG_CNTL_OCV_ADJ_EN			(1 << 3)
#define FG_CNTL_CAP_ADJ_STAT		(1 << 4)
#define FG_CNTL_CAP_ADJ_EN			(1 << 5)
#define FG_CNTL_CC_EN				(1 << 6)
#define FG_CNTL_GAUGE_EN			(1 << 7)

#define FG_15BIT_WORD_VALID			(1 << 15)
#define FG_15BIT_VAL_MASK			0x7fff

#define FG_REP_CAP_VALID			(1 << 7)
#define FG_REP_CAP_VAL_MASK			0x7F

#define FG_DES_CAP1_VALID			(1 << 7)
#define FG_DES_CAP_RES_LSB			1456    /* 1.456mAhr */

#define FG_DES_CC_RES_LSB			1456    /* 1.456mAhr */

#define FG_OCV_CAP_VALID			(1 << 7)
#define FG_OCV_CAP_VAL_MASK			0x7F
#define FG_CC_CAP_VALID				(1 << 7)
#define FG_CC_CAP_VAL_MASK			0x7F

#define FG_LOW_CAP_THR1_MASK		0xf0    /* 5% tp 20% */
#define FG_LOW_CAP_THR1_VAL			0xa0    /* 15 perc */
#define FG_LOW_CAP_THR2_MASK		0x0f    /* 0% to 15% */
#define FG_LOW_CAP_WARN_THR			14  /* 14 perc */
#define FG_LOW_CAP_CRIT_THR			4   /* 4 perc */
#define FG_LOW_CAP_SHDN_THR			0   /* 0 perc */

#define STATUS_MON_DELAY_JIFFIES    (HZ * 60)   /*60 sec */
#define NR_RETRY_CNT    3
#define DEV_NAME	"axp288_fuel_gauge"

/* 1.1mV per LSB expressed in uV */
#define VOLTAGE_FROM_ADC(a)			((a * 11) / 10)
/* properties converted to uV, uA */
#define PROP_VOLT(a)		((a) * 1000)
#define PROP_CURR(a)		((a) * 1000)

#define AXP288_FG_INTR_NUM	6
enum {
	QWBTU_IRQ = 0,
	WBTU_IRQ,
	QWBTO_IRQ,
	WBTO_IRQ,
	WL2_IRQ,
	WL1_IRQ,
};

struct axp288_fg_info {
	struct platform_device *pdev;
	struct regmap *regmap;
	struct regmap_irq_chip_data *regmap_irqc;
	int irq[AXP288_FG_INTR_NUM];
	struct power_supply *bat;
	struct mutex lock;
	int status;
	int max_volt;
	struct delayed_work status_monitor;
	struct dentry *debug_file;
};

static enum power_supply_property fuel_gauge_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
};

static int fuel_gauge_reg_readb(struct axp288_fg_info *info, int reg)
{
	int ret, i;
	unsigned int val;

	for (i = 0; i < NR_RETRY_CNT; i++) {
		ret = regmap_read(info->regmap, reg, &val);
		if (ret == -EBUSY)
			continue;
		else
			break;
	}

	if (ret < 0) {
		dev_err(&info->pdev->dev, "axp288 reg read err:%d\n", ret);
		return ret;
	}

	return val;
}

static int fuel_gauge_reg_writeb(struct axp288_fg_info *info, int reg, u8 val)
{
	int ret;

	ret = regmap_write(info->regmap, reg, (unsigned int)val);

	if (ret < 0)
		dev_err(&info->pdev->dev, "axp288 reg write err:%d\n", ret);

	return ret;
}

static int fuel_gauge_read_15bit_word(struct axp288_fg_info *info, int reg)
{
	unsigned char buf[2];
	int ret;

	ret = regmap_bulk_read(info->regmap, reg, buf, 2);
	if (ret < 0) {
		dev_err(&info->pdev->dev, "Error reading reg 0x%02x err: %d\n",
			reg, ret);
		return ret;
	}

	ret = get_unaligned_be16(buf);
	if (!(ret & FG_15BIT_WORD_VALID)) {
		dev_err(&info->pdev->dev, "Error reg 0x%02x contents not valid\n",
			reg);
		return -ENXIO;
	}

	return ret & FG_15BIT_VAL_MASK;
}

static int fuel_gauge_read_12bit_word(struct axp288_fg_info *info, int reg)
{
	unsigned char buf[2];
	int ret;

	ret = regmap_bulk_read(info->regmap, reg, buf, 2);
	if (ret < 0) {
		dev_err(&info->pdev->dev, "Error reading reg 0x%02x err: %d\n",
			reg, ret);
		return ret;
	}

	/* 12-bit data values have upper 8 bits in buf[0], lower 4 in buf[1] */
	return (buf[0] << 4) | ((buf[1] >> 4) & 0x0f);
}

static int pmic_read_adc_val(const char *name, int *raw_val,
		struct axp288_fg_info *info)
{
	int ret, val = 0;
	struct iio_channel *indio_chan;

	indio_chan = iio_channel_get(NULL, name);
	if (IS_ERR_OR_NULL(indio_chan)) {
		ret = PTR_ERR(indio_chan);
		goto exit;
	}
	ret = iio_read_channel_raw(indio_chan, &val);
	if (ret < 0) {
		dev_err(&info->pdev->dev,
			"IIO channel read error: %x, %x\n", ret, val);
		goto err_exit;
	}

	dev_dbg(&info->pdev->dev, "adc raw val=%x\n", val);
	*raw_val = val;

err_exit:
	iio_channel_release(indio_chan);
exit:
	return ret;
}

#ifdef CONFIG_DEBUG_FS
static int fuel_gauge_debug_show(struct seq_file *s, void *data)
{
	struct axp288_fg_info *info = s->private;
	int raw_val, ret;

	seq_printf(s, " PWR_STATUS[%02x] : %02x\n",
		AXP20X_PWR_INPUT_STATUS,
		fuel_gauge_reg_readb(info, AXP20X_PWR_INPUT_STATUS));
	seq_printf(s, "PWR_OP_MODE[%02x] : %02x\n",
		AXP20X_PWR_OP_MODE,
		fuel_gauge_reg_readb(info, AXP20X_PWR_OP_MODE));
	seq_printf(s, " CHRG_CTRL1[%02x] : %02x\n",
		AXP20X_CHRG_CTRL1,
		fuel_gauge_reg_readb(info, AXP20X_CHRG_CTRL1));
	seq_printf(s, "       VLTF[%02x] : %02x\n",
		AXP20X_V_LTF_DISCHRG,
		fuel_gauge_reg_readb(info, AXP20X_V_LTF_DISCHRG));
	seq_printf(s, "       VHTF[%02x] : %02x\n",
		AXP20X_V_HTF_DISCHRG,
		fuel_gauge_reg_readb(info, AXP20X_V_HTF_DISCHRG));
	seq_printf(s, "    CC_CTRL[%02x] : %02x\n",
		AXP20X_CC_CTRL,
		fuel_gauge_reg_readb(info, AXP20X_CC_CTRL));
	seq_printf(s, "BATTERY CAP[%02x] : %02x\n",
		AXP20X_FG_RES,
		fuel_gauge_reg_readb(info, AXP20X_FG_RES));
	seq_printf(s, "    FG_RDC1[%02x] : %02x\n",
		AXP288_FG_RDC1_REG,
		fuel_gauge_reg_readb(info, AXP288_FG_RDC1_REG));
	seq_printf(s, "    FG_RDC0[%02x] : %02x\n",
		AXP288_FG_RDC0_REG,
		fuel_gauge_reg_readb(info, AXP288_FG_RDC0_REG));
	seq_printf(s, "     FG_OCV[%02x] : %04x\n",
		AXP288_FG_OCVH_REG,
		fuel_gauge_read_12bit_word(info, AXP288_FG_OCVH_REG));
	seq_printf(s, " FG_DES_CAP[%02x] : %04x\n",
		AXP288_FG_DES_CAP1_REG,
		fuel_gauge_read_15bit_word(info, AXP288_FG_DES_CAP1_REG));
	seq_printf(s, "  FG_CC_MTR[%02x] : %04x\n",
		AXP288_FG_CC_MTR1_REG,
		fuel_gauge_read_15bit_word(info, AXP288_FG_CC_MTR1_REG));
	seq_printf(s, " FG_OCV_CAP[%02x] : %02x\n",
		AXP288_FG_OCV_CAP_REG,
		fuel_gauge_reg_readb(info, AXP288_FG_OCV_CAP_REG));
	seq_printf(s, "  FG_CC_CAP[%02x] : %02x\n",
		AXP288_FG_CC_CAP_REG,
		fuel_gauge_reg_readb(info, AXP288_FG_CC_CAP_REG));
	seq_printf(s, " FG_LOW_CAP[%02x] : %02x\n",
		AXP288_FG_LOW_CAP_REG,
		fuel_gauge_reg_readb(info, AXP288_FG_LOW_CAP_REG));
	seq_printf(s, "TUNING_CTL0[%02x] : %02x\n",
		AXP288_FG_TUNE0,
		fuel_gauge_reg_readb(info, AXP288_FG_TUNE0));
	seq_printf(s, "TUNING_CTL1[%02x] : %02x\n",
		AXP288_FG_TUNE1,
		fuel_gauge_reg_readb(info, AXP288_FG_TUNE1));
	seq_printf(s, "TUNING_CTL2[%02x] : %02x\n",
		AXP288_FG_TUNE2,
		fuel_gauge_reg_readb(info, AXP288_FG_TUNE2));
	seq_printf(s, "TUNING_CTL3[%02x] : %02x\n",
		AXP288_FG_TUNE3,
		fuel_gauge_reg_readb(info, AXP288_FG_TUNE3));
	seq_printf(s, "TUNING_CTL4[%02x] : %02x\n",
		AXP288_FG_TUNE4,
		fuel_gauge_reg_readb(info, AXP288_FG_TUNE4));
	seq_printf(s, "TUNING_CTL5[%02x] : %02x\n",
		AXP288_FG_TUNE5,
		fuel_gauge_reg_readb(info, AXP288_FG_TUNE5));

	ret = pmic_read_adc_val("axp288-batt-temp", &raw_val, info);
	if (ret >= 0)
		seq_printf(s, "axp288-batttemp : %d\n", raw_val);
	ret = pmic_read_adc_val("axp288-pmic-temp", &raw_val, info);
	if (ret >= 0)
		seq_printf(s, "axp288-pmictemp : %d\n", raw_val);
	ret = pmic_read_adc_val("axp288-system-temp", &raw_val, info);
	if (ret >= 0)
		seq_printf(s, "axp288-systtemp : %d\n", raw_val);
	ret = pmic_read_adc_val("axp288-chrg-curr", &raw_val, info);
	if (ret >= 0)
		seq_printf(s, "axp288-chrgcurr : %d\n", raw_val);
	ret = pmic_read_adc_val("axp288-chrg-d-curr", &raw_val, info);
	if (ret >= 0)
		seq_printf(s, "axp288-dchrgcur : %d\n", raw_val);
	ret = pmic_read_adc_val("axp288-batt-volt", &raw_val, info);
	if (ret >= 0)
		seq_printf(s, "axp288-battvolt : %d\n", raw_val);

	return 0;
}

static int debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, fuel_gauge_debug_show, inode->i_private);
}

static const struct file_operations fg_debug_fops = {
	.open       = debug_open,
	.read       = seq_read,
	.llseek     = seq_lseek,
	.release    = single_release,
};

static void fuel_gauge_create_debugfs(struct axp288_fg_info *info)
{
	info->debug_file = debugfs_create_file("fuelgauge", 0666, NULL,
		info, &fg_debug_fops);
}

static void fuel_gauge_remove_debugfs(struct axp288_fg_info *info)
{
	debugfs_remove(info->debug_file);
}
#else
static inline void fuel_gauge_create_debugfs(struct axp288_fg_info *info)
{
}
static inline void fuel_gauge_remove_debugfs(struct axp288_fg_info *info)
{
}
#endif

static void fuel_gauge_get_status(struct axp288_fg_info *info)
{
	int pwr_stat, ret;
	int charge, discharge;

	pwr_stat = fuel_gauge_reg_readb(info, AXP20X_PWR_INPUT_STATUS);
	if (pwr_stat < 0) {
		dev_err(&info->pdev->dev,
			"PWR STAT read failed:%d\n", pwr_stat);
		return;
	}
	ret = pmic_read_adc_val("axp288-chrg-curr", &charge, info);
	if (ret < 0) {
		dev_err(&info->pdev->dev,
			"ADC charge current read failed:%d\n", ret);
		return;
	}
	ret = pmic_read_adc_val("axp288-chrg-d-curr", &discharge, info);
	if (ret < 0) {
		dev_err(&info->pdev->dev,
			"ADC discharge current read failed:%d\n", ret);
		return;
	}

	if (charge > 0)
		info->status = POWER_SUPPLY_STATUS_CHARGING;
	else if (discharge > 0)
		info->status = POWER_SUPPLY_STATUS_DISCHARGING;
	else {
		if (pwr_stat & CHRG_STAT_BAT_PRESENT)
			info->status = POWER_SUPPLY_STATUS_FULL;
		else
			info->status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}
}

static int fuel_gauge_get_vbatt(struct axp288_fg_info *info, int *vbatt)
{
	int ret = 0, raw_val;

	ret = pmic_read_adc_val("axp288-batt-volt", &raw_val, info);
	if (ret < 0)
		goto vbatt_read_fail;

	*vbatt = VOLTAGE_FROM_ADC(raw_val);
vbatt_read_fail:
	return ret;
}

static int fuel_gauge_get_current(struct axp288_fg_info *info, int *cur)
{
	int ret, value = 0;
	int charge, discharge;

	ret = pmic_read_adc_val("axp288-chrg-curr", &charge, info);
	if (ret < 0)
		goto current_read_fail;
	ret = pmic_read_adc_val("axp288-chrg-d-curr", &discharge, info);
	if (ret < 0)
		goto current_read_fail;

	if (charge > 0)
		value = charge;
	else if (discharge > 0)
		value = -1 * discharge;

	*cur = value;
current_read_fail:
	return ret;
}

static int fuel_gauge_get_vocv(struct axp288_fg_info *info, int *vocv)
{
	int ret;

	ret = fuel_gauge_read_12bit_word(info, AXP288_FG_OCVH_REG);
	if (ret >= 0)
		*vocv = VOLTAGE_FROM_ADC(ret);

	return ret;
}

static int fuel_gauge_battery_health(struct axp288_fg_info *info)
{
	int ret, vocv, health = POWER_SUPPLY_HEALTH_UNKNOWN;

	ret = fuel_gauge_get_vocv(info, &vocv);
	if (ret < 0)
		goto health_read_fail;

	if (vocv > info->max_volt)
		health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else
		health = POWER_SUPPLY_HEALTH_GOOD;

health_read_fail:
	return health;
}

static int fuel_gauge_get_property(struct power_supply *ps,
		enum power_supply_property prop,
		union power_supply_propval *val)
{
	struct axp288_fg_info *info = power_supply_get_drvdata(ps);
	int ret = 0, value;

	mutex_lock(&info->lock);
	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		fuel_gauge_get_status(info);
		val->intval = info->status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = fuel_gauge_battery_health(info);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = fuel_gauge_get_vbatt(info, &value);
		if (ret < 0)
			goto fuel_gauge_read_err;
		val->intval = PROP_VOLT(value);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		ret = fuel_gauge_get_vocv(info, &value);
		if (ret < 0)
			goto fuel_gauge_read_err;
		val->intval = PROP_VOLT(value);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = fuel_gauge_get_current(info, &value);
		if (ret < 0)
			goto fuel_gauge_read_err;
		val->intval = PROP_CURR(value);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		ret = fuel_gauge_reg_readb(info, AXP20X_PWR_OP_MODE);
		if (ret < 0)
			goto fuel_gauge_read_err;

		if (ret & CHRG_STAT_BAT_PRESENT)
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = fuel_gauge_reg_readb(info, AXP20X_FG_RES);
		if (ret < 0)
			goto fuel_gauge_read_err;

		if (!(ret & FG_REP_CAP_VALID))
			dev_err(&info->pdev->dev,
				"capacity measurement not valid\n");
		val->intval = (ret & FG_REP_CAP_VAL_MASK);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN:
		ret = fuel_gauge_reg_readb(info, AXP288_FG_LOW_CAP_REG);
		if (ret < 0)
			goto fuel_gauge_read_err;
		val->intval = (ret & 0x0f);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = fuel_gauge_read_15bit_word(info, AXP288_FG_CC_MTR1_REG);
		if (ret < 0)
			goto fuel_gauge_read_err;

		val->intval = ret * FG_DES_CAP_RES_LSB;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = fuel_gauge_read_15bit_word(info, AXP288_FG_DES_CAP1_REG);
		if (ret < 0)
			goto fuel_gauge_read_err;

		val->intval = ret * FG_DES_CAP_RES_LSB;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = PROP_VOLT(info->max_volt);
		break;
	default:
		mutex_unlock(&info->lock);
		return -EINVAL;
	}

	mutex_unlock(&info->lock);
	return 0;

fuel_gauge_read_err:
	mutex_unlock(&info->lock);
	return ret;
}

static int fuel_gauge_set_property(struct power_supply *ps,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	struct axp288_fg_info *info = power_supply_get_drvdata(ps);
	int ret = 0;

	mutex_lock(&info->lock);
	switch (prop) {
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN:
		if ((val->intval < 0) || (val->intval > 15)) {
			ret = -EINVAL;
			break;
		}
		ret = fuel_gauge_reg_readb(info, AXP288_FG_LOW_CAP_REG);
		if (ret < 0)
			break;
		ret &= 0xf0;
		ret |= (val->intval & 0xf);
		ret = fuel_gauge_reg_writeb(info, AXP288_FG_LOW_CAP_REG, ret);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int fuel_gauge_property_is_writeable(struct power_supply *psy,
	enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN:
		ret = 1;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static void fuel_gauge_status_monitor(struct work_struct *work)
{
	struct axp288_fg_info *info = container_of(work,
		struct axp288_fg_info, status_monitor.work);

	fuel_gauge_get_status(info);
	power_supply_changed(info->bat);
	schedule_delayed_work(&info->status_monitor, STATUS_MON_DELAY_JIFFIES);
}

static irqreturn_t fuel_gauge_thread_handler(int irq, void *dev)
{
	struct axp288_fg_info *info = dev;
	int i;

	for (i = 0; i < AXP288_FG_INTR_NUM; i++) {
		if (info->irq[i] == irq)
			break;
	}

	if (i >= AXP288_FG_INTR_NUM) {
		dev_warn(&info->pdev->dev, "spurious interrupt!!\n");
		return IRQ_NONE;
	}

	switch (i) {
	case QWBTU_IRQ:
		dev_info(&info->pdev->dev,
			"Quit Battery under temperature in work mode IRQ (QWBTU)\n");
		break;
	case WBTU_IRQ:
		dev_info(&info->pdev->dev,
			"Battery under temperature in work mode IRQ (WBTU)\n");
		break;
	case QWBTO_IRQ:
		dev_info(&info->pdev->dev,
			"Quit Battery over temperature in work mode IRQ (QWBTO)\n");
		break;
	case WBTO_IRQ:
		dev_info(&info->pdev->dev,
			"Battery over temperature in work mode IRQ (WBTO)\n");
		break;
	case WL2_IRQ:
		dev_info(&info->pdev->dev, "Low Batt Warning(2) INTR\n");
		break;
	case WL1_IRQ:
		dev_info(&info->pdev->dev, "Low Batt Warning(1) INTR\n");
		break;
	default:
		dev_warn(&info->pdev->dev, "Spurious Interrupt!!!\n");
	}

	power_supply_changed(info->bat);
	return IRQ_HANDLED;
}

static void fuel_gauge_external_power_changed(struct power_supply *psy)
{
	struct axp288_fg_info *info = power_supply_get_drvdata(psy);

	power_supply_changed(info->bat);
}

static const struct power_supply_desc fuel_gauge_desc = {
	.name			= DEV_NAME,
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= fuel_gauge_props,
	.num_properties		= ARRAY_SIZE(fuel_gauge_props),
	.get_property		= fuel_gauge_get_property,
	.set_property		= fuel_gauge_set_property,
	.property_is_writeable	= fuel_gauge_property_is_writeable,
	.external_power_changed	= fuel_gauge_external_power_changed,
};

static void fuel_gauge_init_irq(struct axp288_fg_info *info)
{
	int ret, i, pirq;

	for (i = 0; i < AXP288_FG_INTR_NUM; i++) {
		pirq = platform_get_irq(info->pdev, i);
		info->irq[i] = regmap_irq_get_virq(info->regmap_irqc, pirq);
		if (info->irq[i] < 0) {
			dev_warn(&info->pdev->dev,
				"regmap_irq get virq failed for IRQ %d: %d\n",
				pirq, info->irq[i]);
			info->irq[i] = -1;
			goto intr_failed;
		}
		ret = request_threaded_irq(info->irq[i],
				NULL, fuel_gauge_thread_handler,
				IRQF_ONESHOT, DEV_NAME, info);
		if (ret) {
			dev_warn(&info->pdev->dev,
				"request irq failed for IRQ %d: %d\n",
				pirq, info->irq[i]);
			info->irq[i] = -1;
			goto intr_failed;
		} else {
			dev_info(&info->pdev->dev, "HW IRQ %d -> VIRQ %d\n",
				pirq, info->irq[i]);
		}
	}
	return;

intr_failed:
	for (; i > 0; i--) {
		free_irq(info->irq[i - 1], info);
		info->irq[i - 1] = -1;
	}
}

static int axp288_fuel_gauge_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct axp288_fg_info *info;
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	struct power_supply_config psy_cfg = {};

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->pdev = pdev;
	info->regmap = axp20x->regmap;
	info->regmap_irqc = axp20x->regmap_irqc;
	info->status = POWER_SUPPLY_STATUS_UNKNOWN;

	platform_set_drvdata(pdev, info);

	mutex_init(&info->lock);
	INIT_DELAYED_WORK(&info->status_monitor, fuel_gauge_status_monitor);

	ret = fuel_gauge_reg_readb(info, AXP288_FG_DES_CAP1_REG);
	if (ret < 0)
		return ret;

	if (!(ret & FG_DES_CAP1_VALID)) {
		dev_err(&pdev->dev, "axp288 not configured by firmware\n");
		return -ENODEV;
	}

	ret = fuel_gauge_reg_readb(info, AXP20X_CHRG_CTRL1);
	if (ret < 0)
		return ret;
	switch ((ret & CHRG_CCCV_CV_MASK) >> CHRG_CCCV_CV_BIT_POS) {
	case CHRG_CCCV_CV_4100MV:
		info->max_volt = 4100;
		break;
	case CHRG_CCCV_CV_4150MV:
		info->max_volt = 4150;
		break;
	case CHRG_CCCV_CV_4200MV:
		info->max_volt = 4200;
		break;
	case CHRG_CCCV_CV_4350MV:
		info->max_volt = 4350;
		break;
	}

	psy_cfg.drv_data = info;
	info->bat = power_supply_register(&pdev->dev, &fuel_gauge_desc, &psy_cfg);
	if (IS_ERR(info->bat)) {
		ret = PTR_ERR(info->bat);
		dev_err(&pdev->dev, "failed to register battery: %d\n", ret);
		return ret;
	}

	fuel_gauge_create_debugfs(info);
	fuel_gauge_init_irq(info);
	schedule_delayed_work(&info->status_monitor, STATUS_MON_DELAY_JIFFIES);

	return 0;
}

static const struct platform_device_id axp288_fg_id_table[] = {
	{ .name = DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(platform, axp288_fg_id_table);

static int axp288_fuel_gauge_remove(struct platform_device *pdev)
{
	struct axp288_fg_info *info = platform_get_drvdata(pdev);
	int i;

	cancel_delayed_work_sync(&info->status_monitor);
	power_supply_unregister(info->bat);
	fuel_gauge_remove_debugfs(info);

	for (i = 0; i < AXP288_FG_INTR_NUM; i++)
		if (info->irq[i] >= 0)
			free_irq(info->irq[i], info);

	return 0;
}

static struct platform_driver axp288_fuel_gauge_driver = {
	.probe = axp288_fuel_gauge_probe,
	.remove = axp288_fuel_gauge_remove,
	.id_table = axp288_fg_id_table,
	.driver = {
		.name = DEV_NAME,
	},
};

module_platform_driver(axp288_fuel_gauge_driver);

MODULE_AUTHOR("Ramakrishna Pallala <ramakrishna.pallala@intel.com>");
MODULE_AUTHOR("Todd Brandt <todd.e.brandt@linux.intel.com>");
MODULE_DESCRIPTION("Xpower AXP288 Fuel Gauge Driver");
MODULE_LICENSE("GPL");
