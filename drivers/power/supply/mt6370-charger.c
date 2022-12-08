// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Richtek Technology Corp.
 *
 * Author: ChiaEn Wu <chiaen_wu@richtek.com>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/devm-helpers.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/linear_range.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/workqueue.h>

#define MT6370_REG_CHG_CTRL1		0x111
#define MT6370_REG_CHG_CTRL2		0x112
#define MT6370_REG_CHG_CTRL3		0x113
#define MT6370_REG_CHG_CTRL4		0x114
#define MT6370_REG_CHG_CTRL5		0x115
#define MT6370_REG_CHG_CTRL6		0x116
#define MT6370_REG_CHG_CTRL7		0x117
#define MT6370_REG_CHG_CTRL8		0x118
#define MT6370_REG_CHG_CTRL9		0x119
#define MT6370_REG_CHG_CTRL10		0x11A
#define MT6370_REG_DEVICE_TYPE		0x122
#define MT6370_REG_USB_STATUS1		0x127
#define MT6370_REG_CHG_STAT		0x14A
#define MT6370_REG_FLED_EN		0x17E
#define MT6370_REG_CHG_STAT1		0X1D0
#define MT6370_REG_OVPCTRL_STAT		0x1D8

#define MT6370_VOBST_MASK		GENMASK(7, 2)
#define MT6370_OTG_PIN_EN_MASK		BIT(1)
#define MT6370_OPA_MODE_MASK		BIT(0)
#define MT6370_OTG_OC_MASK		GENMASK(2, 0)

#define MT6370_MIVR_IBUS_TH_100_mA	100000
#define MT6370_ADC_CHAN_IBUS		5
#define MT6370_ADC_CHAN_MAX		9

enum mt6370_chg_reg_field {
	/* MT6370_REG_CHG_CTRL2 */
	F_IINLMTSEL, F_CFO_EN, F_CHG_EN,
	/* MT6370_REG_CHG_CTRL3 */
	F_IAICR, F_AICR_EN, F_ILIM_EN,
	/* MT6370_REG_CHG_CTRL4 */
	F_VOREG,
	/* MT6370_REG_CHG_CTRL6 */
	F_VMIVR,
	/* MT6370_REG_CHG_CTRL7 */
	F_ICHG,
	/* MT6370_REG_CHG_CTRL8 */
	F_IPREC,
	/* MT6370_REG_CHG_CTRL9 */
	F_IEOC,
	/* MT6370_REG_DEVICE_TYPE */
	F_USBCHGEN,
	/* MT6370_REG_USB_STATUS1 */
	F_USB_STAT, F_CHGDET,
	/* MT6370_REG_CHG_STAT */
	F_CHG_STAT, F_BOOST_STAT, F_VBAT_LVL,
	/* MT6370_REG_FLED_EN */
	F_FL_STROBE,
	/* MT6370_REG_CHG_STAT1 */
	F_CHG_MIVR_STAT,
	/* MT6370_REG_OVPCTRL_STAT */
	F_UVP_D_STAT,
	F_MAX
};

enum mt6370_irq {
	MT6370_IRQ_ATTACH_I = 0,
	MT6370_IRQ_UVP_D_EVT,
	MT6370_IRQ_MIVR,
	MT6370_IRQ_MAX
};

struct mt6370_priv {
	struct device *dev;
	struct iio_channel *iio_adcs;
	struct mutex attach_lock;
	struct power_supply *psy;
	struct regmap *regmap;
	struct regmap_field *rmap_fields[F_MAX];
	struct regulator_dev *rdev;
	struct workqueue_struct *wq;
	struct work_struct bc12_work;
	struct delayed_work mivr_dwork;
	unsigned int irq_nums[MT6370_IRQ_MAX];
	int attach;
	int psy_usb_type;
	bool pwr_rdy;
};

enum mt6370_usb_status {
	MT6370_USB_STAT_NO_VBUS = 0,
	MT6370_USB_STAT_VBUS_FLOW_IS_UNDER_GOING,
	MT6370_USB_STAT_SDP,
	MT6370_USB_STAT_SDP_NSTD,
	MT6370_USB_STAT_DCP,
	MT6370_USB_STAT_CDP,
	MT6370_USB_STAT_MAX
};

struct mt6370_chg_field {
	const char *name;
	const struct linear_range *range;
	struct reg_field field;
};

enum {
	MT6370_RANGE_F_IAICR = 0,
	MT6370_RANGE_F_VOREG,
	MT6370_RANGE_F_VMIVR,
	MT6370_RANGE_F_ICHG,
	MT6370_RANGE_F_IPREC,
	MT6370_RANGE_F_IEOC,
	MT6370_RANGE_F_MAX
};

static const struct linear_range mt6370_chg_ranges[MT6370_RANGE_F_MAX] = {
	LINEAR_RANGE_IDX(MT6370_RANGE_F_IAICR, 100000, 0x0, 0x3F, 50000),
	LINEAR_RANGE_IDX(MT6370_RANGE_F_VOREG, 3900000, 0x0, 0x51, 10000),
	LINEAR_RANGE_IDX(MT6370_RANGE_F_VMIVR, 3900000, 0x0, 0x5F, 100000),
	LINEAR_RANGE_IDX(MT6370_RANGE_F_ICHG, 900000, 0x08, 0x31, 100000),
	LINEAR_RANGE_IDX(MT6370_RANGE_F_IPREC, 100000, 0x0, 0x0F, 50000),
	LINEAR_RANGE_IDX(MT6370_RANGE_F_IEOC, 100000, 0x0, 0x0F, 50000),
};

#define MT6370_CHG_FIELD(_fd, _reg, _lsb, _msb)				\
[_fd] = {								\
	.name = #_fd,							\
	.range = NULL,							\
	.field = REG_FIELD(_reg, _lsb, _msb),				\
}

#define MT6370_CHG_FIELD_RANGE(_fd, _reg, _lsb, _msb)			\
[_fd] = {								\
	.name = #_fd,							\
	.range = &mt6370_chg_ranges[MT6370_RANGE_##_fd],		\
	.field = REG_FIELD(_reg, _lsb, _msb),				\
}

static const struct mt6370_chg_field mt6370_chg_fields[F_MAX] = {
	MT6370_CHG_FIELD(F_IINLMTSEL, MT6370_REG_CHG_CTRL2, 2, 3),
	MT6370_CHG_FIELD(F_CFO_EN, MT6370_REG_CHG_CTRL2, 1, 1),
	MT6370_CHG_FIELD(F_CHG_EN, MT6370_REG_CHG_CTRL2, 0, 0),
	MT6370_CHG_FIELD_RANGE(F_IAICR, MT6370_REG_CHG_CTRL3, 2, 7),
	MT6370_CHG_FIELD(F_AICR_EN, MT6370_REG_CHG_CTRL3, 1, 1),
	MT6370_CHG_FIELD(F_ILIM_EN, MT6370_REG_CHG_CTRL3, 0, 0),
	MT6370_CHG_FIELD_RANGE(F_VOREG, MT6370_REG_CHG_CTRL4, 1, 7),
	MT6370_CHG_FIELD_RANGE(F_VMIVR, MT6370_REG_CHG_CTRL6, 1, 7),
	MT6370_CHG_FIELD_RANGE(F_ICHG, MT6370_REG_CHG_CTRL7, 2, 7),
	MT6370_CHG_FIELD_RANGE(F_IPREC, MT6370_REG_CHG_CTRL8, 0, 3),
	MT6370_CHG_FIELD_RANGE(F_IEOC, MT6370_REG_CHG_CTRL9, 4, 7),
	MT6370_CHG_FIELD(F_USBCHGEN, MT6370_REG_DEVICE_TYPE, 7, 7),
	MT6370_CHG_FIELD(F_USB_STAT, MT6370_REG_USB_STATUS1, 4, 6),
	MT6370_CHG_FIELD(F_CHGDET, MT6370_REG_USB_STATUS1, 3, 3),
	MT6370_CHG_FIELD(F_CHG_STAT, MT6370_REG_CHG_STAT, 6, 7),
	MT6370_CHG_FIELD(F_BOOST_STAT, MT6370_REG_CHG_STAT, 3, 3),
	MT6370_CHG_FIELD(F_VBAT_LVL, MT6370_REG_CHG_STAT, 5, 5),
	MT6370_CHG_FIELD(F_FL_STROBE, MT6370_REG_FLED_EN, 2, 2),
	MT6370_CHG_FIELD(F_CHG_MIVR_STAT, MT6370_REG_CHG_STAT1, 6, 6),
	MT6370_CHG_FIELD(F_UVP_D_STAT, MT6370_REG_OVPCTRL_STAT, 4, 4),
};

static inline int mt6370_chg_field_get(struct mt6370_priv *priv,
				       enum mt6370_chg_reg_field fd,
				       unsigned int *val)
{
	int ret;
	unsigned int reg_val;

	ret = regmap_field_read(priv->rmap_fields[fd], &reg_val);
	if (ret)
		return ret;

	if (mt6370_chg_fields[fd].range)
		return linear_range_get_value(mt6370_chg_fields[fd].range,
					       reg_val, val);

	*val = reg_val;
	return 0;
}

static inline int mt6370_chg_field_set(struct mt6370_priv *priv,
				       enum mt6370_chg_reg_field fd,
				       unsigned int val)
{
	int ret;
	bool f;
	const struct linear_range *r;

	if (mt6370_chg_fields[fd].range) {
		r = mt6370_chg_fields[fd].range;

		if (fd == F_VMIVR) {
			ret = linear_range_get_selector_high(r, val, &val, &f);
			if (ret)
				val = r->max_sel;
		} else {
			linear_range_get_selector_within(r, val, &val);
		}
	}

	return regmap_field_write(priv->rmap_fields[fd], val);
}

enum {
	MT6370_CHG_STAT_READY = 0,
	MT6370_CHG_STAT_CHARGE_IN_PROGRESS,
	MT6370_CHG_STAT_DONE,
	MT6370_CHG_STAT_FAULT,
	MT6370_CHG_STAT_MAX
};

enum {
	MT6370_ATTACH_STAT_DETACH = 0,
	MT6370_ATTACH_STAT_ATTACH_WAIT_FOR_BC12,
	MT6370_ATTACH_STAT_ATTACH_BC12_DONE,
	MT6370_ATTACH_STAT_ATTACH_MAX
};

static int mt6370_chg_otg_of_parse_cb(struct device_node *of,
				      const struct regulator_desc *rdesc,
				      struct regulator_config *rcfg)
{
	struct mt6370_priv *priv = rcfg->driver_data;

	rcfg->ena_gpiod = fwnode_gpiod_get_index(of_fwnode_handle(of),
						 "enable", 0, GPIOD_OUT_LOW |
						 GPIOD_FLAGS_BIT_NONEXCLUSIVE,
						 rdesc->name);
	if (IS_ERR(rcfg->ena_gpiod)) {
		rcfg->ena_gpiod = NULL;
		return 0;
	}

	return regmap_update_bits(priv->regmap, MT6370_REG_CHG_CTRL1,
				  MT6370_OTG_PIN_EN_MASK,
				  MT6370_OTG_PIN_EN_MASK);
}

static void mt6370_chg_bc12_work_func(struct work_struct *work)
{
	struct mt6370_priv *priv = container_of(work, struct mt6370_priv,
						bc12_work);
	int ret;
	bool rpt_psy = false;
	unsigned int attach, usb_stat;

	mutex_lock(&priv->attach_lock);
	attach = priv->attach;

	switch (attach) {
	case MT6370_ATTACH_STAT_DETACH:
		usb_stat = 0;
		break;
	case MT6370_ATTACH_STAT_ATTACH_WAIT_FOR_BC12:
		ret = mt6370_chg_field_set(priv, F_USBCHGEN, attach);
		if (ret)
			dev_err(priv->dev, "Failed to enable USB CHG EN\n");
		goto bc12_work_func_out;
	case MT6370_ATTACH_STAT_ATTACH_BC12_DONE:
		ret = mt6370_chg_field_get(priv, F_USB_STAT, &usb_stat);
		if (ret) {
			dev_err(priv->dev, "Failed to get USB status\n");
			goto bc12_work_func_out;
		}
		break;
	default:
		dev_err(priv->dev, "Invalid attach state\n");
		goto bc12_work_func_out;
	}

	rpt_psy = true;

	switch (usb_stat) {
	case MT6370_USB_STAT_SDP:
	case MT6370_USB_STAT_SDP_NSTD:
		priv->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case MT6370_USB_STAT_DCP:
		priv->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case MT6370_USB_STAT_CDP:
		priv->psy_usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		break;
	case MT6370_USB_STAT_NO_VBUS:
	case MT6370_USB_STAT_VBUS_FLOW_IS_UNDER_GOING:
	default:
		priv->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		break;
	}

bc12_work_func_out:
	mutex_unlock(&priv->attach_lock);

	if (rpt_psy)
		power_supply_changed(priv->psy);
}

static int mt6370_chg_toggle_cfo(struct mt6370_priv *priv)
{
	int ret;
	unsigned int fl_strobe;

	/* check if flash led in strobe mode */
	ret = mt6370_chg_field_get(priv, F_FL_STROBE, &fl_strobe);
	if (ret) {
		dev_err(priv->dev, "Failed to get FL_STROBE_EN\n");
		return ret;
	}

	if (fl_strobe) {
		dev_err(priv->dev, "Flash led is still in strobe mode\n");
		return ret;
	}

	/* cfo off */
	ret = mt6370_chg_field_set(priv, F_CFO_EN, 0);
	if (ret) {
		dev_err(priv->dev, "Failed to disable CFO_EN\n");
		return ret;
	}

	/* cfo on */
	ret = mt6370_chg_field_set(priv, F_CFO_EN, 1);
	if (ret)
		dev_err(priv->dev, "Failed to enable CFO_EN\n");

	return ret;
}

static int mt6370_chg_read_adc_chan(struct mt6370_priv *priv, unsigned int chan,
				    int *val)
{
	int ret;

	if (chan >= MT6370_ADC_CHAN_MAX)
		return -EINVAL;

	ret = iio_read_channel_processed(&priv->iio_adcs[chan], val);
	if (ret)
		dev_err(priv->dev, "Failed to read ADC\n");

	return ret;
}

static void mt6370_chg_mivr_dwork_func(struct work_struct *work)
{
	struct mt6370_priv *priv = container_of(work, struct mt6370_priv,
						mivr_dwork.work);
	int ret;
	unsigned int mivr_stat, ibus;

	ret = mt6370_chg_field_get(priv, F_CHG_MIVR_STAT, &mivr_stat);
	if (ret) {
		dev_err(priv->dev, "Failed to get mivr state\n");
		goto mivr_handler_out;
	}

	if (!mivr_stat)
		goto mivr_handler_out;

	ret = mt6370_chg_read_adc_chan(priv, MT6370_ADC_CHAN_IBUS, &ibus);
	if (ret) {
		dev_err(priv->dev, "Failed to get ibus\n");
		goto mivr_handler_out;
	}

	if (ibus < MT6370_MIVR_IBUS_TH_100_mA) {
		ret = mt6370_chg_toggle_cfo(priv);
		if (ret)
			dev_err(priv->dev, "Failed to toggle cfo\n");
	}

mivr_handler_out:
	enable_irq(priv->irq_nums[MT6370_IRQ_MIVR]);
	pm_relax(priv->dev);
}

static void mt6370_chg_pwr_rdy_check(struct mt6370_priv *priv)
{
	int ret;
	unsigned int opposite_pwr_rdy, otg_en;
	union power_supply_propval val;

	/* Check in OTG mode or not */
	ret = mt6370_chg_field_get(priv, F_BOOST_STAT, &otg_en);
	if (ret) {
		dev_err(priv->dev, "Failed to get OTG state\n");
		return;
	}

	if (otg_en)
		return;

	ret = mt6370_chg_field_get(priv, F_UVP_D_STAT, &opposite_pwr_rdy);
	if (ret) {
		dev_err(priv->dev, "Failed to get opposite power ready state\n");
		return;
	}

	val.intval = opposite_pwr_rdy ?
		     MT6370_ATTACH_STAT_DETACH :
		     MT6370_ATTACH_STAT_ATTACH_WAIT_FOR_BC12;

	ret = power_supply_set_property(priv->psy, POWER_SUPPLY_PROP_ONLINE,
					&val);
	if (ret)
		dev_err(priv->dev, "Failed to start attach/detach flow\n");
}

static int mt6370_chg_get_online(struct mt6370_priv *priv,
				 union power_supply_propval *val)
{
	mutex_lock(&priv->attach_lock);
	val->intval = !!priv->attach;
	mutex_unlock(&priv->attach_lock);

	return 0;
}

static int mt6370_chg_get_status(struct mt6370_priv *priv,
				 union power_supply_propval *val)
{
	int ret;
	unsigned int chg_stat;
	union power_supply_propval online;

	ret = power_supply_get_property(priv->psy, POWER_SUPPLY_PROP_ONLINE,
					&online);
	if (ret) {
		dev_err(priv->dev, "Failed to get online status\n");
		return ret;
	}

	if (!online.intval) {
		val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	}

	ret = mt6370_chg_field_get(priv, F_CHG_STAT, &chg_stat);
	if (ret)
		return ret;

	switch (chg_stat) {
	case MT6370_CHG_STAT_READY:
	case MT6370_CHG_STAT_FAULT:
		val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return ret;
	case MT6370_CHG_STAT_CHARGE_IN_PROGRESS:
		val->intval = POWER_SUPPLY_STATUS_CHARGING;
		return ret;
	case MT6370_CHG_STAT_DONE:
		val->intval = POWER_SUPPLY_STATUS_FULL;
		return ret;
	default:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		return ret;
	}
}

static int mt6370_chg_get_charge_type(struct mt6370_priv *priv,
				      union power_supply_propval *val)
{
	int type, ret;
	unsigned int chg_stat, vbat_lvl;

	ret = mt6370_chg_field_get(priv, F_CHG_STAT, &chg_stat);
	if (ret)
		return ret;

	ret = mt6370_chg_field_get(priv, F_VBAT_LVL, &vbat_lvl);
	if (ret)
		return ret;

	switch (chg_stat) {
	case MT6370_CHG_STAT_CHARGE_IN_PROGRESS:
		if (vbat_lvl)
			type = POWER_SUPPLY_CHARGE_TYPE_FAST;
		else
			type = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	case MT6370_CHG_STAT_READY:
	case MT6370_CHG_STAT_DONE:
	case MT6370_CHG_STAT_FAULT:
	default:
		type = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	}

	val->intval = type;

	return 0;
}

static int mt6370_chg_set_online(struct mt6370_priv *priv,
				 const union power_supply_propval *val)
{
	bool pwr_rdy = !!val->intval;

	mutex_lock(&priv->attach_lock);
	if (pwr_rdy == !!priv->attach) {
		dev_err(priv->dev, "pwr_rdy is same(%d)\n", pwr_rdy);
		mutex_unlock(&priv->attach_lock);
		return 0;
	}

	priv->attach = pwr_rdy;
	mutex_unlock(&priv->attach_lock);

	if (!queue_work(priv->wq, &priv->bc12_work))
		dev_err(priv->dev, "bc12 work has already queued\n");

	return 0;
}

static int mt6370_chg_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct mt6370_priv *priv = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		return mt6370_chg_get_online(priv, val);
	case POWER_SUPPLY_PROP_STATUS:
		return mt6370_chg_get_status(priv, val);
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		return mt6370_chg_get_charge_type(priv, val);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return mt6370_chg_field_get(priv, F_ICHG, &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = linear_range_get_max_value(&mt6370_chg_ranges[MT6370_RANGE_F_ICHG]);
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return mt6370_chg_field_get(priv, F_VOREG, &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = linear_range_get_max_value(&mt6370_chg_ranges[MT6370_RANGE_F_VOREG]);
		return 0;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return mt6370_chg_field_get(priv, F_IAICR, &val->intval);
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		return mt6370_chg_field_get(priv, F_VMIVR, &val->intval);
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		return mt6370_chg_field_get(priv, F_IPREC, &val->intval);
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return mt6370_chg_field_get(priv, F_IEOC, &val->intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = priv->psy_usb_type;
		return 0;
	default:
		return -EINVAL;
	}
}

static int mt6370_chg_set_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	struct mt6370_priv *priv = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		return mt6370_chg_set_online(priv, val);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return mt6370_chg_field_set(priv, F_ICHG, val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return mt6370_chg_field_set(priv, F_VOREG, val->intval);
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return mt6370_chg_field_set(priv, F_IAICR, val->intval);
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		return mt6370_chg_field_set(priv, F_VMIVR, val->intval);
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		return mt6370_chg_field_set(priv, F_IPREC, val->intval);
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return mt6370_chg_field_set(priv, F_IEOC, val->intval);
	default:
		return -EINVAL;
	}
}

static int mt6370_chg_property_is_writeable(struct power_supply *psy,
					    enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return 1;
	default:
		return 0;
	}
}

static enum power_supply_property mt6370_chg_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static enum power_supply_usb_type mt6370_chg_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_DCP,
};

static const struct power_supply_desc mt6370_chg_psy_desc = {
	.name = "mt6370-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = mt6370_chg_properties,
	.num_properties = ARRAY_SIZE(mt6370_chg_properties),
	.get_property = mt6370_chg_get_property,
	.set_property = mt6370_chg_set_property,
	.property_is_writeable = mt6370_chg_property_is_writeable,
	.usb_types = mt6370_chg_usb_types,
	.num_usb_types = ARRAY_SIZE(mt6370_chg_usb_types),
};

static const struct regulator_ops mt6370_chg_otg_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_current_limit = regulator_set_current_limit_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
};

static const u32 mt6370_chg_otg_oc_ma[] = {
	500000, 700000, 1100000, 1300000, 1800000, 2100000, 2400000,
};

static const struct regulator_desc mt6370_chg_otg_rdesc = {
	.of_match = "usb-otg-vbus-regulator",
	.of_parse_cb = mt6370_chg_otg_of_parse_cb,
	.name = "mt6370-usb-otg-vbus",
	.ops = &mt6370_chg_otg_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.min_uV = 4425000,
	.uV_step = 25000,
	.n_voltages = 57,
	.vsel_reg = MT6370_REG_CHG_CTRL5,
	.vsel_mask = MT6370_VOBST_MASK,
	.enable_reg = MT6370_REG_CHG_CTRL1,
	.enable_mask = MT6370_OPA_MODE_MASK,
	.curr_table = mt6370_chg_otg_oc_ma,
	.n_current_limits = ARRAY_SIZE(mt6370_chg_otg_oc_ma),
	.csel_reg = MT6370_REG_CHG_CTRL10,
	.csel_mask = MT6370_OTG_OC_MASK,
};

static int mt6370_chg_init_rmap_fields(struct mt6370_priv *priv)
{
	int i;
	const struct mt6370_chg_field *fds = mt6370_chg_fields;

	for (i = 0; i < F_MAX; i++) {
		priv->rmap_fields[i] = devm_regmap_field_alloc(priv->dev,
							       priv->regmap,
							       fds[i].field);
		if (IS_ERR(priv->rmap_fields[i]))
			return dev_err_probe(priv->dev,
					PTR_ERR(priv->rmap_fields[i]),
					"Failed to allocate regmapfield[%s]\n",
					fds[i].name);
	}

	return 0;
}

static int mt6370_chg_init_setting(struct mt6370_priv *priv)
{
	int ret;

	/* Disable usb_chg_en */
	ret = mt6370_chg_field_set(priv, F_USBCHGEN, 0);
	if (ret) {
		dev_err(priv->dev, "Failed to disable usb_chg_en\n");
		return ret;
	}

	/* Disable input current limit */
	ret = mt6370_chg_field_set(priv, F_ILIM_EN, 0);
	if (ret) {
		dev_err(priv->dev, "Failed to disable input current limit\n");
		return ret;
	}

	/* ICHG/IEOC Workaround, ICHG can not be set less than 900mA */
	ret = mt6370_chg_field_set(priv, F_ICHG, 900000);
	if (ret) {
		dev_err(priv->dev, "Failed to set ICHG to 900mA");
		return ret;
	}

	/* Change input current limit selection to using IAICR results */
	ret = mt6370_chg_field_set(priv, F_IINLMTSEL, 2);
	if (ret) {
		dev_err(priv->dev, "Failed to set IINLMTSEL\n");
		return ret;
	}

	return 0;
}

#define MT6370_CHG_DT_PROP_DECL(_name, _type, _field)	\
{							\
	.name = "mediatek,chg-" #_name,			\
	.type = MT6370_PARSE_TYPE_##_type,		\
	.fd = _field,					\
}

static int mt6370_chg_init_otg_regulator(struct mt6370_priv *priv)
{
	struct regulator_config rcfg = {
		.dev = priv->dev,
		.regmap = priv->regmap,
		.driver_data = priv,
	};

	priv->rdev = devm_regulator_register(priv->dev, &mt6370_chg_otg_rdesc,
					     &rcfg);

	return PTR_ERR_OR_ZERO(priv->rdev);
}

static int mt6370_chg_init_psy(struct mt6370_priv *priv)
{
	struct power_supply_config cfg = {
		.drv_data = priv,
		.of_node = dev_of_node(priv->dev),
	};

	priv->psy = devm_power_supply_register(priv->dev, &mt6370_chg_psy_desc,
					       &cfg);

	return PTR_ERR_OR_ZERO(priv->psy);
}

static void mt6370_chg_destroy_attach_lock(void *data)
{
	struct mutex *attach_lock = data;

	mutex_destroy(attach_lock);
}

static void mt6370_chg_destroy_wq(void *data)
{
	struct workqueue_struct *wq = data;

	flush_workqueue(wq);
	destroy_workqueue(wq);
}

static irqreturn_t mt6370_attach_i_handler(int irq, void *data)
{
	struct mt6370_priv *priv = data;
	unsigned int otg_en;
	int ret;

	/* Check in OTG mode or not */
	ret = mt6370_chg_field_get(priv, F_BOOST_STAT, &otg_en);
	if (ret) {
		dev_err(priv->dev, "Failed to get OTG state\n");
		return IRQ_NONE;
	}

	if (otg_en)
		return IRQ_HANDLED;

	mutex_lock(&priv->attach_lock);
	priv->attach = MT6370_ATTACH_STAT_ATTACH_BC12_DONE;
	mutex_unlock(&priv->attach_lock);

	if (!queue_work(priv->wq, &priv->bc12_work))
		dev_err(priv->dev, "bc12 work has already queued\n");

	return IRQ_HANDLED;
}

static irqreturn_t mt6370_uvp_d_evt_handler(int irq, void *data)
{
	struct mt6370_priv *priv = data;

	mt6370_chg_pwr_rdy_check(priv);

	return IRQ_HANDLED;
}

static irqreturn_t mt6370_mivr_handler(int irq, void *data)
{
	struct mt6370_priv *priv = data;

	pm_stay_awake(priv->dev);
	disable_irq_nosync(priv->irq_nums[MT6370_IRQ_MIVR]);
	schedule_delayed_work(&priv->mivr_dwork, msecs_to_jiffies(200));

	return IRQ_HANDLED;
}

#define MT6370_CHG_IRQ(_name)						\
{									\
	.name = #_name,							\
	.handler = mt6370_##_name##_handler,				\
}

static int mt6370_chg_init_irq(struct mt6370_priv *priv)
{
	int i, ret;
	const struct {
		char *name;
		irq_handler_t handler;
	} mt6370_chg_irqs[] = {
		MT6370_CHG_IRQ(attach_i),
		MT6370_CHG_IRQ(uvp_d_evt),
		MT6370_CHG_IRQ(mivr),
	};

	for (i = 0; i < ARRAY_SIZE(mt6370_chg_irqs); i++) {
		ret = platform_get_irq_byname(to_platform_device(priv->dev),
					      mt6370_chg_irqs[i].name);
		if (ret < 0)
			return dev_err_probe(priv->dev, ret,
					     "Failed to get irq %s\n",
					     mt6370_chg_irqs[i].name);

		priv->irq_nums[i] = ret;
		ret = devm_request_threaded_irq(priv->dev, ret, NULL,
						mt6370_chg_irqs[i].handler,
						IRQF_TRIGGER_FALLING,
						dev_name(priv->dev), priv);
		if (ret)
			return dev_err_probe(priv->dev, ret,
					     "Failed to request irq %s\n",
					     mt6370_chg_irqs[i].name);
	}

	return 0;
}

static int mt6370_chg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mt6370_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;

	priv->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!priv->regmap)
		return dev_err_probe(dev, -ENODEV, "Failed to get regmap\n");

	ret = mt6370_chg_init_rmap_fields(priv);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init regmap fields\n");

	platform_set_drvdata(pdev, priv);

	priv->iio_adcs = devm_iio_channel_get_all(priv->dev);
	if (IS_ERR(priv->iio_adcs))
		return dev_err_probe(dev, PTR_ERR(priv->iio_adcs),
				     "Failed to get iio adc\n");

	ret = mt6370_chg_init_otg_regulator(priv);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init OTG regulator\n");

	ret = mt6370_chg_init_psy(priv);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init psy\n");

	mutex_init(&priv->attach_lock);
	ret = devm_add_action_or_reset(dev, mt6370_chg_destroy_attach_lock,
				       &priv->attach_lock);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init attach lock\n");

	priv->attach = MT6370_ATTACH_STAT_DETACH;

	priv->wq = create_singlethread_workqueue(dev_name(priv->dev));
	if (!priv->wq)
		return dev_err_probe(dev, -ENOMEM,
				     "Failed to create workqueue\n");

	ret = devm_add_action_or_reset(dev, mt6370_chg_destroy_wq, priv->wq);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init wq\n");

	ret = devm_work_autocancel(dev, &priv->bc12_work, mt6370_chg_bc12_work_func);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init bc12 work\n");

	ret = devm_delayed_work_autocancel(dev, &priv->mivr_dwork, mt6370_chg_mivr_dwork_func);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init mivr delayed work\n");

	ret = mt6370_chg_init_setting(priv);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to init mt6370 charger setting\n");

	ret = mt6370_chg_init_irq(priv);
	if (ret)
		return ret;

	mt6370_chg_pwr_rdy_check(priv);

	return 0;
}

static const struct of_device_id mt6370_chg_of_match[] = {
	{ .compatible = "mediatek,mt6370-charger", },
	{}
};
MODULE_DEVICE_TABLE(of, mt6370_chg_of_match);

static struct platform_driver mt6370_chg_driver = {
	.probe = mt6370_chg_probe,
	.driver = {
		.name = "mt6370-charger",
		.of_match_table = mt6370_chg_of_match,
	},
};
module_platform_driver(mt6370_chg_driver);

MODULE_AUTHOR("ChiaEn Wu <chiaen_wu@richtek.com>");
MODULE_DESCRIPTION("MediaTek MT6370 Charger Driver");
MODULE_LICENSE("GPL v2");
