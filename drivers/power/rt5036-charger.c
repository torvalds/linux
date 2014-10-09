/*
 *  drivers/power/rt5036-charger.c
 *  Driver for Richtek RT5036 PMIC Charger driver
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
#include <linux/of_gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */

#include <linux/mfd/rt5036/rt5036.h>
#include <linux/power/rt5036-charger.h>
#ifdef CONFIG_RT_POWER
#include <linux/power/rt-power.h>
#endif /* #ifdef CONFIG_RT_POWER */
#ifdef CONFIG_RT_BATTERY
#include <linux/power/rt-battery.h>
#endif /* #ifdef CONFIG_RT_BATTERY */

#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
#define RT5036_ACIN_LEVEL	0
#define RT5036_USBIN_LEVEL	1
#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */

static unsigned char chg_init_regval[] = {
	0xb0,			/*REG 0x01*/
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
	0xDC,			/*REG 0x02*/
#else
	0x58,			/*REG 0x02*/
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
	0x00,			/*REG 0x03*/
	0xFE,			/*REG 0x04*/
	0x93,			/*REG 0x05*/
	0xAD,			/*REG 0x06*/
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
	0x94,			/*REG 0x07*/
	#else
	0xB4,			/*REG 0x07*/
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
	0x01,			/*REG 0x08*/
	0x0C,			/*REG 0x13*/
	0x80,			/*REG 0x14*/
	0x00,			/*REG 0x15*/
	0x70,			/*REG 0x18*/
};

static char *rtdef_chg_name = "rt-charger";

static char *rt_charger_supply_list[] = {
	"none",
};

static enum power_supply_property rt_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

static int rt_charger_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct rt5036_charger_info *ci = dev_get_drvdata(psy->dev->parent);
	int ret = 0;
	int regval = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = ci->online;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		regval = rt5036_reg_read(ci->i2c, RT5036_REG_CHGSTAT1);
		if (regval < 0) {
			ret = -EINVAL;
		} else {
			regval &= RT5036_CHGSTAT_MASK;
			regval >>= RT5036_CHGSTAT_SHIFT;
			switch (regval) {
			case 0:
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
				break;
			case 1:
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
				break;
			case 2:
				val->intval = POWER_SUPPLY_STATUS_FULL;
				break;
			case 3:
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
				break;
			}
		}
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		regval = rt5036_reg_read(ci->i2c, RT5036_REG_CHGSTAT1);
		if (regval < 0) {
			ret = -EINVAL;
		} else {
			if (regval & RT5036_CHGDIS_MASK)
				val->intval = 0;
			else
				val->intval = 1;
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = ci->charge_cable;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = 2000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		regval = rt5036_reg_read(ci->i2c, RT5036_REG_CHGCTL1);
		if (regval < 0) {
			ret = -EINVAL;
		} else {
			regval &= RT5036_CHGAICR_MASK;
			regval >>= RT5036_CHGAICR_SHIFT;
			switch (regval) {
			case 0:
				val->intval = 0;
				break;
			case 1:
				val->intval = 100;
				break;
			case 2:
				val->intval = 500;
				break;
			case 3:
				val->intval = 700;
				break;
			case 4:
				val->intval = 900;
				break;
			case 5:
				val->intval = 1000;
				break;
			case 6:
				val->intval = 1500;
				break;
			case 7:
				val->intval = 2000;
				break;
			}
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		regval = rt5036_reg_read(ci->i2c, RT5036_REG_CHGSTAT1);
		if (regval < 0) {
			ret = -EINVAL;
		} else {
			if (regval & RT5036_CHGOPASTAT_MASK) {
				val->intval = -1;
			} else {
				regval =
				    rt5036_reg_read(ci->i2c,
						    RT5036_REG_CHGCTL5);
				if (regval < 0) {
					ret = -EINVAL;
				} else {
					regval &= RT5036_CHGICC_MASK;
					regval >>= RT5036_CHGICC_SHIFT;
					val->intval = 500 + regval * 100;
				}
			}
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		regval = rt5036_reg_read(ci->i2c, RT5036_REG_CHGCTL2);
		if (regval < 0) {
			ret = -EINVAL;
		} else {
			regval &= RT5036_CHGCV_MASK;
			regval >>= RT5036_CHGCV_SHIFT;
			val->intval = regval * 25 + 3650;
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = 3650;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = 4400;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int rt_charger_set_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	struct rt5036_charger_info *ci = dev_get_drvdata(psy->dev->parent);
	int ret = 0;
	int regval = 0;

	RTINFO("prop = %d, val->intval = %d\n", psp, val->intval);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ci->online = val->intval;
		if (ci->online) {
#ifdef CONFIG_RT_BATTERY
			union power_supply_propval pval;
			struct power_supply *psy =
			    power_supply_get_by_name(RT_BATT_NAME);
			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_CHARGING;
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev, "couldn't get RT battery\n");
			}
#else
#ifdef CONFIG_RT9420_FUELGAUGE
			union power_supply_propval pval;
			struct power_supply *psy =
			    power_supply_get_by_name("rt-fuelgauge");
			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_CHARGING;
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev,
					"couldn't get rt fuelgauge battery\n");
				}
#else
			union power_supply_propval pval;
			struct power_supply *psy =
			    power_supply_get_by_name("battery");
			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_CHARGING;
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev, "couldn't get battery\n");
				}
#endif /* #ifdef CONFIG_RT9420_FUELGAUGE */
#endif /* #ifdef CONFIG_RT_BATTERY */
			if (ci->te_en)
				ret =
				    rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL1,
						    RT5036_CHGTEEN_MASK);
		} else {
#ifdef CONFIG_RT_BATTERY
			union power_supply_propval pval;
			struct power_supply *psy =
			    power_supply_get_by_name(RT_BATT_NAME);
			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_DISCHARGING;
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev, "couldn't get RT battery\n");
			}
#else
#ifdef CONFIG_RT9420_FUELGAUGE
			union power_supply_propval pval;
			struct power_supply *psy =
			    power_supply_get_by_name("rt-fuelgauge");
			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_DISCHARGING;
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev,
					"couldn't get rt fuelgauge battery\n");
			}
#else
			union power_supply_propval pval;
			struct power_supply *psy =
			    power_supply_get_by_name("battery");
			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_DISCHARGING;
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev, "couldn't get battery\n");
			}
#endif /* #ifdef CONFIG_RT9420_FUELGAUGE */
#endif /* #ifdef CONFIG_RT_BATTERY */
			if (ci->te_en) {
				ret =
				    rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL1,
						    RT5036_CHGTEEN_MASK);
				/* te rst workaround */
				rt5036_set_bits(ci->i2c, 0x20,
						RT5036_TERST_MASK);
				rt5036_clr_bits(ci->i2c, 0x20,
						RT5036_TERST_MASK);
			}
		}
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		if (ci->online && val->intval) {
			int icc = 0;
			union power_supply_propval pval;

			if (ci->charge_cable == POWER_SUPPLY_TYPE_MAINS)
				icc = ci->acchg_icc;
			else if (ci->charge_cable == POWER_SUPPLY_TYPE_USB_DCP)
				icc = ci->usbtachg_icc;
			else
				icc = ci->usbchg_icc;
			pval.intval = icc;
			ret =
			    psy->set_property(psy,
					      POWER_SUPPLY_PROP_CURRENT_NOW,
					      &pval);
			if (ret < 0)
				dev_err(ci->dev, "set final icc fail\n");
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ci->charge_cable = val->intval;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		if (val->intval <= 0)
			regval = 0;
		else if (val->intval <= 100)
			regval = 1;
		else if (val->intval <= 500)
			regval = 2;
		else if (val->intval <= 700)
			regval = 3;
		else if (val->intval <= 900)
			regval = 4;
		else if (val->intval <= 1000)
			regval = 5;
		else if (val->intval <= 1500)
			regval = 6;
		else if (val->intval <= 2000)
			regval = 7;
		else
			regval = 0;
		if (!ci->batabs)
			ret = rt5036_assign_bits(ci->i2c, RT5036_REG_CHGCTL1,
						 RT5036_CHGAICR_MASK,
						 regval <<
						 RT5036_CHGAICR_SHIFT);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (val->intval < 0) {
			union power_supply_propval pval;
#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
			struct power_supply *psy =
			    power_supply_get_by_name(RT_USB_NAME);
			pval.intval = 0;
			psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval);
			power_supply_changed(psy);
			ci->otg_en = 1;
			if (ci->charge_cable != POWER_SUPPLY_TYPE_MAINS) {
				/* otg drop fix */
				rt5036_set_bits(ci->i2c, 0x23, 0x3);
				pval.intval = ci->otg_volt;
				ci->psy.set_property(&ci->psy,
					POWER_SUPPLY_PROP_VOLTAGE_NOW,
					&pval);
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
				dev_info(ci->dev, "set UUG on\n");
				ret = rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL6, 0x20);
				rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL1,
					    RT5036_CHGOPAMODE_MASK);
#else
				ret =
				    rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL1,
						    RT5036_CHGOPAMODE_MASK);
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
				/* otg drop fix */
				mdelay(10);
				rt5036_clr_bits(ci->i2c, 0x23, 0x3);
			}
#else
			ci->otg_en = 1;
			/* otg drop fix */
			rt5036_set_bits(ci->i2c, 0x23, 0x3);
			pval.intval = ci->otg_volt;
			ci->psy.set_property(&ci->psy,
					     POWER_SUPPLY_PROP_VOLTAGE_NOW,
					     &pval);
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
			ret = rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL6, 0x20);
#else
			ret =
			    rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL1,
					    RT5036_CHGOPAMODE_MASK);
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
			/* otg drop fix */
			mdelay(10);
			rt5036_clr_bits(ci->i2c, 0x23, 0x3);
#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
		} else if (val->intval == 0) {
#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
			if (ci->charge_cable != POWER_SUPPLY_TYPE_MAINS) {
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
				dev_info(ci->dev, "set UUG off\n");
				ret = rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL6, 0x20);
				rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL1,
					    RT5036_CHGOPAMODE_MASK);
#else
				ret =
				    rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL1,
						    RT5036_CHGOPAMODE_MASK);
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
			}
#else
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
			ret = rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL6, 0x20);
#else
			ret =
			    rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL1,
					    RT5036_CHGOPAMODE_MASK);
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
			ci->otg_en = 0;
		} else if (val->intval < 500)
			regval = 0;
		else if (val->intval > 2000)
			regval = 15;
		else
			regval = (val->intval - 500) / 100;
		regval += 5;
		if (regval > 15)
			regval = 15;
		if (!ci->batabs && val->intval > 0)
			ret =
			    rt5036_assign_bits(ci->i2c, RT5036_REG_CHGCTL5,
					       RT5036_CHGICC_MASK,
					       regval << RT5036_CHGICC_SHIFT);
		rt5036_reg_write(ci->i2c, RT5036_REG_CHGCTL4,
				 chg_init_regval[4]);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (val->intval < 3650)
			regval = 0;
		else if (val->intval > 5225)
			regval = 0x3F;
		else
			regval = (val->intval - 3650) / 25;
		ret =
		    rt5036_assign_bits(ci->i2c, RT5036_REG_CHGCTL2,
				       RT5036_CHGCV_MASK,
				       regval << RT5036_CHGCV_SHIFT);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_STATUS:
	default:
		ret = -EINVAL;
	}
	return ret;
}

static void rt5036_stat2alrt_irq_handler(void *info, int eventno)
{
	struct rt5036_charger_info *ci = info;
	unsigned char old_stat, new_stat;
	int ret;

	old_stat = ci->stat2;
	ret = rt5036_reg_read(ci->i2c, RT5036_REG_CHGSTAT2);
	if (ret < 0) {
		dev_err(ci->dev, "read stat io fail\n");
		return;
	}
	new_stat = ret;
	/*cablein status change*/
	if ((old_stat ^ new_stat) & RT5036_PWRRDY_MASK) {
		if (new_stat & RT5036_PWRRDY_MASK) {
#ifndef CONFIG_RT_SUPPORT_ACUSB_DUALIN
			union power_supply_propval pval;
			struct power_supply *psy =
			    power_supply_get_by_name(RT_USB_NAME);
			if (psy) {
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
				rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL1,
					    RT5036_CHGOPAMODE_MASK);
				rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL6, 0x20);
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
				pval.intval = 1;
				psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev, "couldn't get RT usb\n");
			}
#endif /* #ifndef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
			dev_info(ci->dev, "cable in\n");
		} else {
#ifndef CONFIG_RT_SUPPORT_ACUSB_DUALIN
			union power_supply_propval pval;
			struct power_supply *psy =
			    power_supply_get_by_name(RT_USB_NAME);
			if (psy) {
				pval.intval = 0;
				psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE,
						  &pval);
				power_supply_changed(psy);
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
				rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL6, 0x20);
				pval.intval = ci->otg_volt;
				ci->psy.set_property(&ci->psy,
						     POWER_SUPPLY_PROP_VOLTAGE_NOW,
						     &pval);
				rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL1,
					    RT5036_CHGOPAMODE_MASK);
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
			} else
				dev_err(ci->dev, "couldn't get RT usb\n");
			}
#endif /* #ifndef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
			dev_info(ci->dev, "cable out\n");
		}
	/*jeita status change*/
	old_stat = new_stat & RT5036_TSEVENT_MASK;
	if (old_stat & RT5036_TSWC_MASK) {
		dev_info(ci->dev, "warm or cool parameter\n");
		rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL7,
				RT5036_CCJEITA_MASK);
	} else if (old_stat & RT5036_TSHC_MASK) {
		dev_info(ci->dev, "hot or cold temperature\n");
		rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL7,
				RT5036_CCJEITA_MASK);
	} else {
		dev_info(ci->dev, "normal temperature\n");
		rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL7,
				RT5036_CCJEITA_MASK);
	}
	ci->stat2 = new_stat;
}

static void rt5036_batabs_irq_handler(void *info, int eventno)
{
	struct rt5036_charger_info *ci = info;
	struct power_supply *psy = &ci->psy;
	union power_supply_propval val;
	int ret;
	/*disable battery detection*/
	ret = rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL6, RT5036_BATDEN_MASK);
	if (ret < 0)
		dev_err(ci->dev, "set battary detection disable fail\n");
	/*set aicr to 2000*/
	val.intval = 2000;
	ret = psy->set_property(psy, POWER_SUPPLY_PROP_CURRENT_AVG, &val);
	if (ret < 0)
		dev_err(ci->dev, "set aicr to 2000 fail\n");
	/*set icc to 2000*/
	val.intval = 2000;
	ret = psy->set_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (ret < 0)
		dev_err(ci->dev, "set icc to 2000 fail\n");
	/*set charger offline*/
	val.intval = 0;
	ret = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &val);
	if (ret < 0)
		dev_err(ci->dev, "set charger offline fail\n");
#ifdef CONFIG_RT_BATTERY
	psy = power_supply_get_by_name(RT_BATT_NAME);
	if (psy) {
		val.intval = 0;
		ret = psy->set_property(psy, POWER_SUPPLY_PROP_PRESENT, &val);
		if (ret < 0)
			dev_err(ci->dev, "set battery not present fail\n");
	} else {
		dev_err(ci->dev, "couldn't get batt psy\n");
		}
#else
#ifdef CONFIG_RT9420_FUELGAUGE
	psy = power_supply_get_by_name("rt-fuelgauge");
	if (psy) {
		val.intval = 0;
		ret = psy->set_property(psy, POWER_SUPPLY_PROP_PRESENT, &val);
		if (ret < 0)
			dev_err(ci->dev, "set battery not present fail\n");
	} else {
		dev_err(ci->dev, "couldn't get rt fuelgauge psy\n");
		}
#else
	psy = power_supply_get_by_name("battery");
	if (psy) {
		val.intval = 0;
		ret = psy->set_property(psy, POWER_SUPPLY_PROP_PRESENT, &val);
		if (ret < 0)
			dev_err(ci->dev, "set battery not present fail\n");
	} else {
		dev_err(ci->dev, "couldn't get battery psy\n");
		}
#endif /* #ifdef CONFIG_RT9420_FUELGAUGE */
#endif /* #ifdef CONFIG_RT_BATTERY */
	ci->batabs = 1;
}

static void rt5036_general_irq_handler(void *info, int eventno)
{
	struct rt5036_charger_info *ci = info;

	RTINFO("eventno=%02d\n", eventno);
	switch (eventno) {
	case CHGEVENT_CHTMRFI:
		rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL3,
				RT5036_CHGOTGEN_MASK);
		msleep(5);
		rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL3,
				RT5036_CHGOTGEN_MASK);
		break;
	case CHGEVENT_CHRCHGI:
		if (!ci->online) {
			dev_warn(ci->dev, "recharge false alarm\n");
		} else {
#ifdef CONFIG_RT_BATTERY
			struct power_supply *psy =
			    power_supply_get_by_name(RT_BATT_NAME);
			union power_supply_propval pval;

			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_CHARGING;
				/*set battery status*/
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev, "couldn't get batt psy\n");
				}
#else
#ifdef CONFIG_RT9420_FUELGAUGE
			struct power_supply *psy =
			    power_supply_get_by_name("rt-fuelgauge");
			union power_supply_propval pval;

			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_CHARGING;
				/*set battery status*/
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev,
					"couldn't get rt fuelgauge psy\n");
				}
#else
			struct power_supply *psy =
			    power_supply_get_by_name("battery");
			union power_supply_propval pval;

			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_CHARGING;
				/*set battery status*/
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev, "couldn't get battery psy\n");
				}
#endif /* #ifdef CONFIG_RT9420_FUELGAUGE */
#endif /* #ifdfef CONFIG_RT_BATTERY */
			dev_info(ci->dev, "recharge occur\n");
		}
		break;
	case CHGEVENT_IEOCI:
		if (!ci->online) {
			dev_warn(ci->dev, "eoc false alarm\n");
		} else {
#ifdef CONFIG_RT_BATTERY
			struct power_supply *psy =
			    power_supply_get_by_name(RT_BATT_NAME);
			union power_supply_propval pval;

			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_FULL;
				/*set battery status*/
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev, "couldn't get batt psy\n");
				}
#else
#ifdef CONFIG_RT9420_FUELGAUGE
			struct power_supply *psy =
			    power_supply_get_by_name("rt-fuelgauge");
			union power_supply_propval pval;

			if (psy) {
				/*set battery status*/
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev,
					"couldn't get rt fuelgauge psy\n");
				}
#else
			struct power_supply *psy =
			    power_supply_get_by_name("battery");
			union power_supply_propval pval;

			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_FULL;
				/*set battery status*/
				psy->set_property(psy, POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else {
				dev_err(ci->dev, "couldn't get battery psy\n");
				}
#endif /* #ifdef CONFIG_RT9420_FUELGAUGE */
#endif /* #ifdfef CONFIG_RT_BATTERY */
			dev_info(ci->dev, "eoc really occur\n");
		}
		break;
	default:
		break;
	}
}

static rt_irq_handler rt_chgirq_handler[CHGEVENT_MAX] = {
	[CHGEVENT_STAT2ALT] = rt5036_stat2alrt_irq_handler,
	[CHGEVENT_CHBSTLOWVI] = rt5036_general_irq_handler,
	[CHGEVENT_BSTOLI] = rt5036_general_irq_handler,
	[CHGEVENT_BSTVIMIDOVP] = rt5036_general_irq_handler,
	[CHGEVENT_CHTMRFI] = rt5036_general_irq_handler,
	[CHGEVENT_CHRCHGI] = rt5036_general_irq_handler,
	[CHGEVENT_CHTERMI] = rt5036_general_irq_handler,
	[CHGEVENT_CHBATOVI] = rt5036_general_irq_handler,
	[CHGEVENT_CHRVPI] = rt5036_general_irq_handler,
	[CHGEVENT_BATABSENSE] = rt5036_batabs_irq_handler,
	[CHGEVENT_CHBADADPI] = rt5036_general_irq_handler,
	[CHGEVENT_VINCHGPLUGOUT] = rt5036_general_irq_handler,
	[CHGEVENT_VINCHGPLUGIN] = rt5036_general_irq_handler,
	[CHGEVENT_PPBATLVI] = rt5036_general_irq_handler,
	[CHGEVENT_IEOCI] = rt5036_general_irq_handler,
	[CHGEVENT_VINOVPI] = rt5036_general_irq_handler,
};

void rt5036_charger_irq_handler(struct rt5036_charger_info *ci,
				unsigned int irqevent)
{
	int i;
	unsigned int masked_irq_event =
	    (chg_init_regval[8] << 16) | (chg_init_regval[9] << 8) |
	    chg_init_regval[10];
	unsigned int final_irq_event = irqevent & (~masked_irq_event);

	for (i = 0; i < CHGEVENT_MAX; i++) {
		if ((final_irq_event & (1 << i)) && rt_chgirq_handler[i])
			rt_chgirq_handler[i] (ci, i);
	}
}
EXPORT_SYMBOL(rt5036_charger_irq_handler);

#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
static irqreturn_t rt5036_acdet_irq_handler(int irqno, void *param)
{
	struct rt5036_charger_info *ci = param;

	if (RT5036_ACIN_LEVEL == gpio_get_value(ci->acdet_gpio)) {
		union power_supply_propval pval;
		struct power_supply *psy = power_supply_get_by_name(RT_AC_NAME);

		if (RT5036_ACIN_LEVEL)
			irq_set_irq_type(ci->acdet_irq, IRQF_TRIGGER_FALLING);
		else
			irq_set_irq_type(ci->acdet_irq, IRQF_TRIGGER_RISING);
		if (psy) {
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
			rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL1,
				    RT5036_CHGOPAMODE_MASK);
			rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL6, 0x20);
#else
			if (ci->otg_en)
				rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL1,
						RT5036_CHGOPAMODE_MASK);
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
			pval.intval = 1;
			psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval);
			power_supply_changed(psy);
		} else {
			dev_err(ci->dev, "couldn't get RT ac\n");
			}
		dev_info(ci->dev, "ac in\n");
	} else {
		union power_supply_propval pval;
		struct power_supply *psy = power_supply_get_by_name(RT_AC_NAME);

		if (RT5036_ACIN_LEVEL)
			irq_set_irq_type(ci->acdet_irq, IRQF_TRIGGER_RISING);
		else
			irq_set_irq_type(ci->acdet_irq, IRQF_TRIGGER_FALLING);
		if (psy) {
			pval.intval = 0;
			psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval);
			power_supply_changed(psy);
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
			if (ci->charge_cable == 0) {
				if (ci->otg_en)
					rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL6, 0x20);
				else
					rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL6, 0x20);
				pval.intval = ci->otg_volt;
				ci->psy.set_property(&ci->psy,
						     POWER_SUPPLY_PROP_VOLTAGE_NOW,
						     &pval);
				rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL1,
					    RT5036_CHGOPAMODE_MASK);
			}
#else
			if (ci->otg_en) {
				/*set otg voltage*/
				pval.intval = ci->otg_volt;
				ci->psy.set_property(&ci->psy,
					POWER_SUPPLY_PROP_VOLTAGE_NOW,
					&pval);
				rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL1,
						RT5036_CHGOPAMODE_MASK);
			}
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
		} else
			dev_err(ci->dev, "couldn't get RT ac\n");
			}
		dev_info(ci->dev, "ac out\n");
	}
	return IRQ_HANDLED;
}

static irqreturn_t rt5036_usbdet_irq_handler(int irqno, void *param)
{
	struct rt5036_charger_info *ci = param;

	if (ci->otg_en) {
		dev_info(ci->dev, "currently in otg mode\n");
		goto usb_out;
	}
	if (RT5036_USBIN_LEVEL == gpio_get_value(ci->usbdet_gpio)) {
		union power_supply_propval pval;
		struct power_supply *psy =
		    power_supply_get_by_name(RT_USB_NAME);
		if (RT5036_USBIN_LEVEL)
			irq_set_irq_type(ci->usbdet_irq, IRQF_TRIGGER_FALLING);
		else
			irq_set_irq_type(ci->usbdet_irq, IRQF_TRIGGER_RISING);
		if (psy) {
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
			rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL1,
				    RT5036_CHGOPAMODE_MASK);
			rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL6, 0x20);
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
			pval.intval = 1;
			psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval);
			power_supply_changed(psy);
		} else {
			dev_err(ci->dev, "couldn't get RT usb\n");
			}
		dev_info(ci->dev, "usb in\n");
	} else {
		union power_supply_propval pval;
		struct power_supply *psy =
		    power_supply_get_by_name(RT_USB_NAME);
		if (RT5036_USBIN_LEVEL)
			irq_set_irq_type(ci->usbdet_irq, IRQF_TRIGGER_RISING);
		else
			irq_set_irq_type(ci->usbdet_irq, IRQF_TRIGGER_FALLING);
		if (psy) {
			pval.intval = 0;
			psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval);
			power_supply_changed(psy);
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
			if (ci->charge_cable == 0) {
				rt5036_clr_bits(ci->i2c, RT5036_REG_CHGCTL6, 0x20);
				pval.intval = ci->otg_volt;
				ci->psy.set_property(&ci->psy,
						     POWER_SUPPLY_PROP_VOLTAGE_NOW,
						     &pval);
				rt5036_set_bits(ci->i2c, RT5036_REG_CHGCTL1,
					    RT5036_CHGOPAMODE_MASK);
			}
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
		} else
			dev_err(ci->dev, "couldn't get RT usb\n");
			}
		dev_info(ci->dev, "usb out\n");
	}
usb_out:
	return IRQ_HANDLED;
}

static int rt5036_acusb_irqinit(struct rt5036_charger_info *ci)
{
	int rc = 0;

	if (gpio_is_valid(ci->acdet_gpio) && gpio_is_valid(ci->usbdet_gpio)) {
		rc = gpio_request(ci->acdet_gpio, "rt5036_acdet_gpio");
		if (rc < 0) {
			dev_err(ci->dev, "request acdet gpio fail\n");
			goto irq_out;
		}
		rc = gpio_request(ci->usbdet_gpio, "rt5036_usbdet_gpio");
		if (rc < 0) {
			dev_err(ci->dev, "request usbdet gpio fail\n");
			goto irq_out;
		}
		gpio_direction_input(ci->acdet_gpio);
		gpio_direction_input(ci->usbdet_gpio);

		ci->acdet_irq = gpio_to_irq(ci->acdet_gpio);
		ci->usbdet_irq = gpio_to_irq(ci->usbdet_gpio);

		rc = devm_request_threaded_irq(ci->dev, ci->acdet_irq, NULL,
					       rt5036_acdet_irq_handler,
					       IRQF_TRIGGER_FALLING |
					       IRQF_DISABLED,
					       "rt5036_acdet_irq", ci);
		if (rc < 0) {
			dev_err(ci->dev, "request acdet irq fail\n");
			goto irq_out;
		}
		rc = devm_request_threaded_irq(ci->dev, ci->usbdet_irq, NULL,
					       rt5036_usbdet_irq_handler,
					       IRQF_TRIGGER_FALLING |
					       IRQF_DISABLED,
					       "rt5036_usbdet_irq", ci);
		if (rc < 0) {
			dev_err(ci->dev, "request usbdet irq fail\n");
			goto irq_out;
		}
		enable_irq_wake(ci->acdet_irq);
		enable_irq_wake(ci->usbdet_irq);
	} else {
		rc = -EINVAL;
		}
irq_out:
	return rc;
}

static void rt5036_acusb_irqdeinit(struct rt5036_charger_info *ci)
{
	devm_free_irq(ci->dev, ci->acdet_irq, ci);
	devm_free_irq(ci->dev, ci->usbdet_irq, ci);
	gpio_free(ci->acdet_gpio);
	gpio_free(ci->usbdet_gpio);
}
#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */

static void rt5036_chg_dwork_func(struct work_struct *work)
{
	struct rt5036_charger_info *ci =
	    container_of(work, struct rt5036_charger_info,
			 dwork.work);
	rt5036_stat2alrt_irq_handler(ci, 0);
#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
	rt5036_acdet_irq_handler(ci->acdet_irq, ci);
	rt5036_usbdet_irq_handler(ci->usbdet_irq, ci);
#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
}

static int rt5036_charger_reginit(struct i2c_client *client)
{
	/*thermal HGM*/
	rt5036_set_bits(client, 0x20, 0x40);
	/*charger fix in rev D IC*/
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
	rt5036_reg_write(client, 0x22, 0x60);
#else
	rt5036_reg_write(client, 0x22, 0xE0);
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
	/*write charger init val*/
	rt5036_reg_block_write(client, RT5036_REG_CHGCTL1, 8, chg_init_regval);
	rt5036_reg_block_write(client, RT5036_REG_CHGIRQMASK1, 3,
			       &chg_init_regval[8]);
	rt5036_reg_write(client, RT5036_REG_CHGSTAT2MASK, chg_init_regval[11]);
	/*always read at first time*/
	rt5036_reg_read(client, RT5036_REG_CHGIRQ1);
	rt5036_reg_read(client, RT5036_REG_CHGIRQ2);
	rt5036_reg_read(client, RT5036_REG_CHGIRQ3);
	rt5036_set_bits(client, 0x20, RT5036_TERST_MASK);
	rt5036_clr_bits(client, 0x20, RT5036_TERST_MASK);
#ifdef CONFIG_CHARGER_RT5036_VMID_HDMI
	rt5036_set_bits(client, RT5036_REG_CHGCTL1, RT5036_CHGOPAMODE_MASK);
#endif /* #ifdef CONFIG_CHARGER_RT5036_VMID_HDMI */
	RTINFO("\n");
	return 0;
}

static int rt_parse_dt(struct rt5036_charger_info *ci,
				 struct device *dev)
{
#ifdef CONFIG_OF
	struct device_node *np = dev->of_node;
	u32 val;

	if (of_property_read_bool(np, "rt,te_en"))
		ci->te_en = 1;

	if (of_property_read_u32(np, "rt,iprec", &val)) {
		dev_info(dev, "no iprec property, use the default value\n");
	} else {
		if (val > RT5036_IPREC_MAX)
			val = RT5036_IPREC_MAX;
		chg_init_regval[4] &= (~RT5036_CHGIPREC_MASK);
		chg_init_regval[4] |= (val << RT5036_CHGIPREC_SHIFT);
	}

	if (of_property_read_u32(np, "rt,ieoc", &val)) {
		dev_info(dev, "no ieoc property, use the default value\n");
	} else {
		if (val > RT5036_IEOC_MAX)
			val = RT5036_IEOC_MAX;
		chg_init_regval[4] &= (~RT5036_CHGIEOC_MASK);
		chg_init_regval[4] |= val;
	}

	if (of_property_read_u32(np, "rt,vprec", &val)) {
		dev_info(dev, "no vprec property, use the default value\n");
	} else {
		if (val > RT5036_VPREC_MAX)
			val = RT5036_VPREC_MAX;
		chg_init_regval[5] &= (~RT5036_CHGVPREC_MASK);
		chg_init_regval[5] |= val;
	}

	if (of_property_read_u32(np, "rt,batlv", &val)) {
		dev_info(dev, "no batlv property, use the default value\n");
	} else {
		if (val > RT5036_BATLV_MAX)
			val = RT5036_BATLV_MAX;
		chg_init_regval[6] &= (~RT5036_CHGBATLV_MASK);
		chg_init_regval[6] |= val;
	}

	if (of_property_read_u32(np, "rt,vrechg", &val)) {
		dev_info(dev, "no vrechg property, use the default value\n");
	} else {
		if (val > RT5036_VRECHG_MAX)
			val = RT5036_VRECHG_MAX;
		chg_init_regval[7] &= (~RT5036_CHGVRECHG_MASK);
		chg_init_regval[7] |= (val << RT5036_CHGVRECHG_SHIFT);
	}

	if (of_property_read_u32(np, "rt,chg_volt", &val)) {
		dev_info(dev,
			 "no chg_volt property, use 4200 as the default value\n");
		ci->chg_volt = 4200;
	} else {
		ci->chg_volt = val;
		}

	if (of_property_read_u32(np, "rt,otg_volt", &val)) {
		dev_info(dev,
			 "no otg_volt property, use 5025 as the default value\n");
		ci->otg_volt = 5025;
	} else {
		ci->otg_volt = val;
		}

	if (of_property_read_u32(np, "rt,acchg_icc", &val)) {
		dev_info(dev,
			 "no acchg_icc property, use 2000 as the default value\n");
		ci->acchg_icc = 2000;
	} else {
		ci->acchg_icc = val;
		}

	if (of_property_read_u32(np, "rt,usbtachg_icc", &val)) {
		dev_info(dev,
			 "no usbtachg_icc property, use 2000 as the default value\n");
		ci->usbtachg_icc = 2000;
	} else {
		ci->usbtachg_icc = val;
		}

	if (of_property_read_u32(np, "rt,usbchg_icc", &val)) {
		dev_info(dev,
			 "no usbchg_icc property, use 500 as the default value\n");
		ci->usbchg_icc = 500;
	} else {
		ci->usbchg_icc = val;
		}

#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
	ci->acdet_gpio = of_get_named_gpio(np, "rt,acdet_gpio", 0);
	ci->usbdet_gpio = of_get_named_gpio(np, "rt,usbdet_gpio", 0);
#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
#endif /* #ifdef CONFIG_OF */
	rt5036_charger_reginit(ci->i2c);
	RTINFO("\n");
	return 0;
}

static int rt_parse_pdata(struct rt5036_charger_info *ci,
				    struct device *dev)
{
	struct rt5036_chg_data *pdata = dev->platform_data;

	if (pdata->te_en)
		ci->te_en = 1;

	chg_init_regval[4] &= (~RT5036_CHGIPREC_MASK);
	chg_init_regval[4] |= (pdata->iprec << RT5036_CHGIPREC_SHIFT);

	chg_init_regval[4] &= (~RT5036_CHGIEOC_MASK);
	chg_init_regval[4] |= pdata->ieoc;

	chg_init_regval[5] &= (~RT5036_CHGVPREC_MASK);
	chg_init_regval[5] |= pdata->vprec;

	chg_init_regval[6] &= (~RT5036_CHGBATLV_MASK);
	chg_init_regval[6] |= pdata->batlv;

	chg_init_regval[7] &= (~RT5036_CHGVRECHG_MASK);
	chg_init_regval[7] |= (pdata->vrechg << RT5036_CHGVRECHG_SHIFT);

	ci->chg_volt = pdata->chg_volt;
	ci->otg_volt = pdata->otg_volt;
	ci->acchg_icc = pdata->acchg_icc;
	ci->usbtachg_icc = pdata->usbtachg_icc;
	ci->usbchg_icc = pdata->usbchg_icc;
#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
	ci->acdet_gpio = pdata->acdet_gpio;
	ci->usbdet_gpio = pdata->usbdet_gpio;
#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
	rt5036_charger_reginit(ci->i2c);
	RTINFO("\n");
	return 0;
}

#ifdef CONFIG_RT_POWER
static struct platform_device rt_power_dev = {
	.name = "rt-power",
	.id = -1,
};
#endif /* #ifdef CONFIG_RT_POWER */

#ifdef CONFIG_RT_BATTERY
static struct platform_device rt_battery_dev = {
	.name = "rt-battery",
	.id = -1,
};
#endif /* #ifdef CONFIG_RT_BATTERY */

static int rt5036_charger_probe(struct platform_device *pdev)
{
	struct rt5036_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5036_platform_data *pdata = (pdev->dev.parent)->platform_data;
#ifdef CONFIG_RT_POWER
	struct rt_power_data *rt_power_pdata;
#endif /* #ifdef CONFIG_RT_POWER */
	struct rt5036_charger_info *ci;
	bool use_dt = pdev->dev.of_node;
	int ret = 0;

	ci = devm_kzalloc(&pdev->dev, sizeof(*ci), GFP_KERNEL);
	if (!ci)
		return -ENOMEM;

	ci->i2c = chip->i2c;
	if (use_dt) {
		rt_parse_dt(ci, &pdev->dev);
	} else {
		if (!pdata) {
			dev_err(&pdev->dev, "platform data invalid\n");
			ret = -EINVAL;
			goto out_dev;
		}
		pdev->dev.platform_data = pdata->chg_pdata;
		rt_parse_pdata(ci, &pdev->dev);
	}

	ci->dev = &pdev->dev;
	INIT_DELAYED_WORK(&ci->dwork, rt5036_chg_dwork_func);

	platform_set_drvdata(pdev, ci);
	/*power supply register*/
	ci->psy.name = rtdef_chg_name;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
	ci->psy.type = POWER_SUPPLY_TYPE_UNKNOWN;
#else
	ci->psy.type = -1;
#endif /* #ifdef (LINUX_VERSION_CODE */
	ci->psy.supplied_to = rt_charger_supply_list;
	ci->psy.properties = rt_charger_props;
	ci->psy.num_properties = ARRAY_SIZE(rt_charger_props);
	ci->psy.get_property = rt_charger_get_property;
	ci->psy.set_property = rt_charger_set_property;
	ret = power_supply_register(&pdev->dev, &ci->psy);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"couldn't create power supply for rt-charger\n");
		goto out_dev;
	}
#ifdef CONFIG_RT_BATTERY
	rt_battery_dev.dev.parent = &pdev->dev;
	ret = platform_device_register(&rt_battery_dev);
	if (ret < 0)
		goto out_dev;
#endif /* #ifdef CONFIG_RT_BATTERY */

#ifdef CONFIG_RT_POWER
	rt_power_pdata =
	    devm_kzalloc(&pdev->dev, sizeof(*rt_power_pdata), GFP_KERNEL);
	if (!rt_power_pdata) {
		ret = -ENOMEM;
		goto out_psy;
	}
	rt_power_pdata->chg_volt = ci->chg_volt;
	rt_power_pdata->acchg_icc = ci->acchg_icc;
	rt_power_pdata->usbtachg_icc = ci->usbtachg_icc;
	rt_power_pdata->usbchg_icc = ci->usbchg_icc;

	rt_power_dev.dev.platform_data = rt_power_pdata;
	rt_power_dev.dev.parent = &pdev->dev;
	ret = platform_device_register(&rt_power_dev);
	if (ret < 0)
		goto out_psy;
#endif /* #ifdef CONFIG_RT_POWER */

#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
	ret = rt5036_acusb_irqinit(ci);
	if (ret < 0)
		goto out_all;
#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */

	schedule_delayed_work(&ci->dwork, msecs_to_jiffies(100));
	chip->chg_info = ci;
	dev_info(&pdev->dev, "driver successfully loaded\n");
	return 0;
#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
out_all:
	platform_device_unregister(&rt_power_dev);
	devm_kfree(&pdev->dev, rt_power_pdata);
#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
#ifdef CONFIG_RT_POWER
out_psy:
#endif /* #ifdef CONFIG_RT_POEWR */
#ifdef CONFIG_RT_BATTERY
	platform_device_unregister(&rt_battery_dev);
#endif /* #ifdef CONFIG_RT_BATTERY */
	power_supply_unregister(&ci->psy);
out_dev:
	return ret;
}

static int rt5036_charger_remove(struct platform_device *pdev)
{
	struct rt5036_charger_info *ci = platform_get_drvdata(pdev);
#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
	rt5036_acusb_irqdeinit(ci);
#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
#ifdef CONFIG_RT_POWER
	platform_device_unregister(&rt_power_dev);
#endif /* #ifdef CONFIG_RT_POWER */
#ifdef CONFIG_RT_BATTERY
	platform_device_unregister(&rt_battery_dev);
#endif /* #ifdef CONFIG_RT_BATTERY */
	power_supply_unregister(&ci->psy);
	return 0;
}

static const struct of_device_id rt_match_table[] = {
	{.compatible = "rt,rt5036-charger",},
	{},
};

static struct platform_driver rt5036_charger_driver = {
	.driver = {
		   .name = RT5036_DEV_NAME "-charger",
		   .owner = THIS_MODULE,
		   .of_match_table = rt_match_table,
		   },
	.probe = rt5036_charger_probe,
	.remove = rt5036_charger_remove,
};

static int __init rt5036_charger_init(void)
{
	return platform_driver_register(&rt5036_charger_driver);
}
subsys_initcall(rt5036_charger_init);

static void __exit rt5036_charger_exit(void)
{
	platform_driver_unregister(&rt5036_charger_driver);
}
module_exit(rt5036_charger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("Charger driver for RT5036");
MODULE_ALIAS("platform:" RT5036_DEV_NAME "-charger");
MODULE_VERSION(RT5036_DRV_VER);
