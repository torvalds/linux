/*
 *  drivers/power/rt5025-charger.c
 *  Driver for Richtek RT5025 PMIC Charger driver
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
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif /* #ifdef CONFIG_HAS_EARLYSUSPEND */

#include <linux/mfd/rt5025.h>
#include <linux/power/rt5025-charger.h>
#ifdef CONFIG_RT_POWER
#include <linux/power/rt-power.h>
#endif /* #ifdef CONFIG_RT_POWER */

static unsigned char chg_init_regval[] = {
	0x12, /*REG 0x02*/
	0x8C, /*REG 0x03*/
	0x03, /*REG 0x04*/
	0x20, /*REG 0x05*/
	0x14, /*REG 0x06*/
	0x40, /*REG 0x07*/
	0xDA, /*REG 0x30*/
	0xDA, /*REG 0x32*/
	0x43, /*REG 0x34*/
};

static char *rtdef_chg_name = "rt-charger";

static char *rt_charger_supply_list[] = {
	"none",
};

static enum power_supply_property rt_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

static void rt_charger_set_batt_status(struct rt5025_charger_info *ci);

static int rt_charger_get_property(struct power_supply *psy, enum power_supply_property psp, \
			union power_supply_propval *val)
{
	struct rt5025_charger_info *ci = dev_get_drvdata(psy->dev->parent);
	int ret = 0;
	int regval = 0;
	#ifdef CONFIG_BATTERY_RT5025
	struct power_supply *bat_psy = power_supply_get_by_name(RT_BATT_NAME);
	union power_supply_propval pval;
	#endif

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = ci->online;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = ci->chg_status;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		regval = rt5025_reg_read(ci->i2c, RT5025_REG_CHGCTL7);
		if (regval < 0) {
			ret = -EINVAL;
		} else {
			if (regval & RT5025_CHGCEN_MASK)
				val->intval = 1;
			else
				val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = 0;
		#ifdef CONFIG_BATTERY_RT5025
			if (bat_psy) {
				ret = bat_psy->get_property(bat_psy, POWER_SUPPLY_PROP_TEMP_AMBIENT,\
					&pval);
				if (ret < 0)
					dev_err(ci->dev, "get ic temp fail\n");
				else
					val->intval = pval.intval;
			}
		#endif /* #ifdef CONFIG_BATTERY_RT5025 */
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = ci->charger_cable;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = 2000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		regval = rt5025_reg_read(ci->i2c, RT5025_REG_CHGCTL4);
		if (regval < 0) {
			ret = -EINVAL;
		} else {
			regval &= RT5025_CHGAICR_MASK;
			regval >>= RT5025_CHGAICR_SHFT;
			switch (regval) {
			case 0:
				val->intval = 100;
				break;
			case 1:
				val->intval = 500;
				break;
			case 2:
				val->intval = 1000;
				break;
			case 3:
				val->intval = 0;
				break;
			default:
				ret = -EINVAL;
				break;
			}
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		regval = rt5025_reg_read(ci->i2c, RT5025_REG_CHGCTL4);
		if (regval < 0) {
			ret = -EINVAL;
		} else {
			regval &= RT5025_CHGICC_MASK;
			regval >>= RT5025_CHGICC_SHFT;
			val->intval = 500 + regval * 100;
			}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		regval = rt5025_reg_read(ci->i2c, RT5025_REG_CHGCTL3);
		if (regval < 0) {
				ret = -EINVAL;
		} else {
				regval &= RT5025_CHGCV_MASK;
				regval >>= RT5025_CHGCV_SHFT;
				val->intval = regval * 20 + 3500;
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = 3500;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = 4440;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int rt_charger_set_property(struct power_supply *psy, enum power_supply_property psp, \
				const union power_supply_propval *val)
{
	struct rt5025_charger_info *ci = dev_get_drvdata(psy->dev->parent);
	int ret = 0;
	int regval = 0;
	RTINFO("prop = %d, val->intval = %d\n", psp, val->intval);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ci->online = val->intval;
		if (ci->online) {
			if (ci->te_en) {
				ret = rt5025_set_bits(ci->i2c, RT5025_REG_CHGCTL2, RT5025_CHGTEEN_MASK);
				/*charger workaround*/
				mdelay(150);
				/*turn  on recharge irq enable*/
				chg_init_regval[8] |= RT5025_CHRCHGI_MASK;
				rt5025_reg_write(ci->i2c, RT5025_REG_IRQEN3, chg_init_regval[8]);
				/*turn on chterm irq enable*/
				chg_init_regval[7] |= RT5025_CHTERMI_MASK;
				rt5025_reg_write(ci->i2c, RT5025_REG_IRQEN2, chg_init_regval[7]);
			}
			ci->chg_status = POWER_SUPPLY_STATUS_CHARGING;
			rt_charger_set_batt_status(ci);
		} else {
			if (ci->te_en) {
				ret = rt5025_clr_bits(ci->i2c, RT5025_REG_CHGCTL2, RT5025_CHGTEEN_MASK);
				/*charger workaround*/
				/*turn off chterm irq enable*/
				chg_init_regval[7] &= ~RT5025_CHTERMI_MASK;
				rt5025_reg_write(ci->i2c, RT5025_REG_IRQEN2, chg_init_regval[7]);
				/*turn  off recharge irq enable*/
				chg_init_regval[8] &= ~RT5025_CHRCHGI_MASK;
				rt5025_reg_write(ci->i2c, RT5025_REG_IRQEN3, chg_init_regval[8]);
			}
			ci->chg_status = POWER_SUPPLY_STATUS_DISCHARGING;
			rt_charger_set_batt_status(ci);
		}
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		if (ci->online && val->intval) {
			int icc;
			int battemp_icc;
			int inttemp_icc;
			union power_supply_propval pval;

			if (ci->charger_cable == POWER_SUPPLY_TYPE_MAINS)
				battemp_icc = inttemp_icc = (ci->screen_on ?\
					ci->screenon_icc:ci->acchg_icc);
			else if (ci->charger_cable == POWER_SUPPLY_TYPE_USB_DCP)
				battemp_icc = inttemp_icc = (ci->screen_on ?\
					ci->screenon_icc:ci->usbtachg_icc);
			else
				battemp_icc = inttemp_icc = (ci->screen_on ?\
					ci->screenon_icc:ci->usbchg_icc);
			if (ci->battemp_region == RT5025_BATTEMP_COLD ||\
				ci->battemp_region == RT5025_BATTEMP_HOT)
				battemp_icc = 10;
			else if (ci->battemp_region == RT5025_BATTEMP_COOL ||\
				ci->battemp_region == RT5025_BATTEMP_WARM)
				battemp_icc /= 2;

			if (ci->inttemp_region == RT5025_INTTEMP_WARM)
				inttemp_icc -= 300;
			else if (ci->inttemp_region == RT5025_INTTEMP_HOT)
				inttemp_icc -= 800;

			if (inttemp_icc < 0)
				inttemp_icc = 10;

			icc = min(battemp_icc, inttemp_icc);
			pval.intval = icc;
			ret = psy->set_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &pval);
			if (ret < 0)
				dev_err(ci->dev, "set final icc fail\n");
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ci->charger_cable = val->intval;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		if (0 < val->intval && val->intval <= 100)
			regval = 0;
		else if (val->intval <= 500)
			regval = 1;
		else if (val->intval <= 1000)
			regval = 2;
		else
			regval = 3;
		if (!ci->batabs)
			ret = rt5025_assign_bits(ci->i2c, RT5025_REG_CHGCTL4, \
				RT5025_CHGAICR_MASK, regval << RT5025_CHGAICR_SHFT);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (val->intval < 0) {
			ci->otg_en = 1;
			regval = -1;
			#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
			ret = rt5025_set_bits(ci->i2c, RT5025_REG_CHGCTL2, RT5025_VBUSHZ_MASK);
			if (ret < 0)
				dev_err(ci->dev, "set vbus hz fail\n");
			#else
			ret = rt5025_clr_bits(ci->i2c, RT5025_REG_CHGCTL2, RT5025_CHGBCEN_MASK);
			if (ret < 0)
				dev_err(ci->dev, "shutdown chg buck fail\n");
			#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
		} else if (val->intval == 0) {
			ci->otg_en = 0;
			regval = -1;
			#ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN
			ret = rt5025_clr_bits(ci->i2c, RT5025_REG_CHGCTL2, RT5025_VBUSHZ_MASK);
			if (ret < 0)
				dev_err(ci->dev, "clear vbus hz fail\n");
			#else
			ret = rt5025_set_bits(ci->i2c, RT5025_REG_CHGCTL2, RT5025_CHGBCEN_MASK);
			if (ret < 0)
				dev_err(ci->dev, "turnon chg buck fail\n");
			#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
		} else if (val->intval > 0 && val->intval < 500) {
				regval = 0;
		} else if (val->intval > 2000) {
				regval = 15;
		} else {
				regval = (val->intval - 500) / 100;
		}

		if (regval >= 0)
			ret = rt5025_assign_bits(ci->i2c, RT5025_REG_CHGCTL4, RT5025_CHGICC_MASK, \
					regval<<RT5025_CHGICC_SHFT);

		if (val->intval > 0 && val->intval < 500)
			rt5025_clr_bits(ci->i2c, RT5025_REG_CHGCTL7, RT5025_CHGCEN_MASK);
		else
			rt5025_set_bits(ci->i2c, RT5025_REG_CHGCTL7, RT5025_CHGCEN_MASK);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (val->intval < 3500)
			regval = 0;
		else if (val->intval > 4440)
			regval = 0x3A;
		else
			regval = (val->intval - 3500) / 20;
		ret = rt5025_assign_bits(ci->i2c, RT5025_REG_CHGCTL3, RT5025_CHGCV_MASK, \
					regval << RT5025_CHGCV_SHFT);
		break;
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_STATUS:
	default:
		ret = -EINVAL;
	}
	return ret;
}

#ifdef CONFIG_BATTERY_RT5025
static int rt5025_set_tempalrt(struct rt5025_charger_info *ci)
{
	int rc = 0;

	rt5025_assign_bits(ci->i2c, RT5025_REG_IRQCTL,\
		RT5025_TALRTMX_MASK|RT5025_TALRTMN_MASK, 0x00);
	if (ci->battemp_region == RT5025_BATTEMP_HOT) {
		rt5025_reg_write(ci->i2c, RT5025_REG_TALRTMAX, ci->temp_scalar[6]);
		rt5025_assign_bits(ci->i2c, RT5025_REG_IRQCTL,\
			RT5025_TALRTMX_MASK, 0xFF);
	} else if (ci->battemp_region == RT5025_BATTEMP_WARM) {
		rt5025_reg_write(ci->i2c, RT5025_REG_TALRTMAX, ci->temp_scalar[4]);
		rt5025_reg_write(ci->i2c, RT5025_REG_TALRTMIN, ci->temp_scalar[7]);
		rt5025_assign_bits(ci->i2c, RT5025_REG_IRQCTL,\
			RT5025_TALRTMX_MASK|RT5025_TALRTMN_MASK, 0xFF);
	} else if (ci->battemp_region == RT5025_BATTEMP_NORMAL) {
		rt5025_reg_write(ci->i2c, RT5025_REG_TALRTMAX, ci->temp_scalar[2]);
		rt5025_reg_write(ci->i2c, RT5025_REG_TALRTMIN, ci->temp_scalar[5]);
		rt5025_assign_bits(ci->i2c, RT5025_REG_IRQCTL,\
			RT5025_TALRTMX_MASK|RT5025_TALRTMN_MASK, 0xFF);
	} else if (ci->battemp_region == RT5025_BATTEMP_COOL) {
		rt5025_reg_write(ci->i2c, RT5025_REG_TALRTMAX, ci->temp_scalar[0]);
		rt5025_reg_write(ci->i2c, RT5025_REG_TALRTMIN, ci->temp_scalar[3]);
		rt5025_assign_bits(ci->i2c, RT5025_REG_IRQCTL,\
			RT5025_TALRTMX_MASK|RT5025_TALRTMN_MASK, 0xFF);
	} else {
		rt5025_reg_write(ci->i2c, RT5025_REG_TALRTMIN, ci->temp_scalar[1]);
		rt5025_assign_bits(ci->i2c, RT5025_REG_IRQCTL,\
			RT5025_TALRTMN_MASK, 0xFF);
	}
	return rc;
}
#endif /* #ifdef CONFIG_BATTERY_RT5025 */

static void rt_charger_set_batt_status(struct rt5025_charger_info *ci)
{
	#ifdef CONFIG_BATTERY_RT5025
	struct power_supply *psy = power_supply_get_by_name(RT_BATT_NAME);
	union power_supply_propval pval;
	int rc = 0;

	if (!psy) {
		dev_err(ci->dev, "can't get battery supply\n");
		return;
	}
	pval.intval = ci->chg_status;
	rc = psy->set_property(psy, POWER_SUPPLY_PROP_STATUS, &pval);
	if (rc < 0)
		dev_err(ci->dev, "set battery status fail\n");
	power_supply_changed(psy);
	#endif /* #ifdef CONFIG_BATTERY_RT5025 */
}

static int rt_charger_check_battery_present(struct rt5025_charger_info *ci)
{
	int rc = 1;
	#ifdef CONFIG_BATTERY_RT5025
	struct power_supply *psy = power_supply_get_by_name(RT_BATT_NAME);
	union power_supply_propval pval;

	if (!psy) {
		dev_err(ci->dev, "can't get battery supply\n");
		return rc;
	}
	rc = psy->get_property(psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
	if (rc < 0) {
		dev_err(ci->dev, "get battery voltage fail\n");
	} else {
		if (pval.intval < (ci->chg_volt - 200) * 1000)
			rc = 0;
		else
			rc = 1;
	}
	#endif /* #ifdef CONFIG_BATTERY_RT5025 */
	return rc;
}

static void rt5025_batabs_irq_handler(void *info, int eventno)
{
	struct rt5025_charger_info *ci = info;
	struct power_supply *psy = &ci->psy;
	union power_supply_propval pval;
	int rc = 0;

	#ifdef CONFIG_BATTERY_RT5025
		struct power_supply *bat_psy = power_supply_get_by_name(RT_BATT_NAME);

		pval.intval = 0;
		if (!bat_psy) {
			dev_err(ci->dev, "get rt-battery supply fail\n");
		} else {
			rc = bat_psy->set_property(bat_psy,
				POWER_SUPPLY_PROP_PRESENT, &pval);
			if (rc < 0)
				dev_err(ci->dev, "set battery not present fail\n");
			power_supply_changed(bat_psy);
		}
	#endif /* #ifdef CONFIG_BATTERY_RT5025 */
	/*set aicr to disable*/
	pval.intval = 2000;
	rc = psy->set_property(psy, POWER_SUPPLY_PROP_CURRENT_AVG, &pval);
	if (rc < 0)
		dev_err(ci->dev, "set aicr to disable fail\n");
	/*set icc to 2000*/
	pval.intval = 2000;
	rc = psy->set_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &pval);
	if (rc < 0)
		dev_err(ci->dev, "set icc to 2000 fail\n");
	/*set online = 0, due to bat absense*/
	pval.intval = 0;
	rc = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval);
	if (rc < 0)
		dev_err(ci->dev, "set charger offline fail\n");
	ci->batabs = 1;
	ci->te_en = 0;
}

static void rt5025_acin_irq_handler(void *info, int eventno)
{
	#ifdef CONFIG_RT_POWER
	struct rt5025_charger_info *ci = info;
	#ifndef CONFIG_RT_SUPPORT_ACUSB_DUALIN
	struct power_supply *psy = power_supply_get_by_name(RT_USB_NAME);
	#else
	struct power_supply *psy = power_supply_get_by_name(RT_AC_NAME);
	#endif /* #ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
	union power_supply_propval pval;
	int rc = 0;

	if (!psy) {
		dev_err(ci->dev, "could not get psy supply\n");
		return;
	}
	pval.intval = 1;
	rc = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval);
	if (rc < 0)
		dev_err(ci->dev, "set ac online fail\n");
	power_supply_changed(psy);
	dev_info(ci->dev, "%s\n", __func__);
	#endif /* #ifdef CONFIG_RT_POWER */
}

static void rt5025_acout_irq_handler(void *info, int eventno)
{
	#ifdef CONFIG_RT_POWER
	struct rt5025_charger_info *ci = info;
	#ifndef CONFIG_RT_SUPPORT_ACUSB_DUALIN
	struct power_supply *psy = power_supply_get_by_name(RT_USB_NAME);
	#else
	struct power_supply *psy = power_supply_get_by_name(RT_AC_NAME);
	#endif /* ifdef CONFIG_RT_SUPPORT_ACUSB_DUALIN */
	union power_supply_propval pval;
	int rc = 0;

	if (!psy) {
		dev_err(ci->dev, "could not get rt-usb supply\n");
		return;
	}
	pval.intval = 0;
	rc = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval);
	if (rc < 0)
		dev_err(ci->dev, "set ac offline fail\n");
	power_supply_changed(psy);
	dev_info(ci->dev, "%s\n", __func__);
	#endif /* #ifdef CONFIG_RT_POWER */
}

static void rt5025_usbin_irq_handler(void *info, int eventno)
{
	#ifdef CONFIG_RT_POWER
	struct rt5025_charger_info *ci = info;
	struct power_supply *psy = power_supply_get_by_name(RT_USB_NAME);
	union power_supply_propval pval;
	int rc = 0;

	if (!psy) {
		dev_err(ci->dev, "could not get rt-usb supply\n");
		return;
	}
	if (!ci->otg_en) {
		pval.intval = 1;
		rc = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval);
		if (rc < 0)
			dev_err(ci->dev, "set ac online fail\n");
		power_supply_changed(psy);
	}
	dev_info(ci->dev, "%s\n", __func__);
	#endif /* #ifdef CONFIG_RT_POWER */
}

static void rt5025_usbout_irq_handler(void *info, int eventno)
{
	#ifdef CONFIG_RT_POWER
	struct rt5025_charger_info *ci = info;
	struct power_supply *psy = power_supply_get_by_name(RT_USB_NAME);
	union power_supply_propval pval;
	int rc = 0;

	if (!psy) {
		dev_err(ci->dev, "could not get rt-usb supply\n");
		return;
	}
	if (!ci->otg_en) {
		pval.intval = 0;
		rc = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval);
		if (rc < 0)
			dev_err(ci->dev, "set ac offline fail\n");
		power_supply_changed(psy);
	}
	dev_info(ci->dev, "%s\n", __func__);
	#endif /* #ifdef CONFIG_RT_POWER */
}

static void rt5025_talrtmax_irq_handler(void *info, int eventno)
{
	#ifdef CONFIG_BATTERY_RT5025
	struct rt5025_charger_info *ci = info;
	union power_supply_propval pval;
	int rc = 0;

	switch (ci->battemp_region) {
	case RT5025_BATTEMP_COLD:
		dev_warn(ci->dev, "cold than cold???\n");
		break;
	case RT5025_BATTEMP_COOL:
		dev_info(ci->dev, "cool-> cold\n");
		ci->battemp_region = RT5025_BATTEMP_COLD;
		rt5025_set_tempalrt(ci);
		pval.intval = 1;
		rc = ci->psy.set_property(&ci->psy, POWER_SUPPLY_PROP_PRESENT,\
			&pval);
		if (rc < 0)
			dev_err(ci->dev, "set present fail\n");
		break;
	case RT5025_BATTEMP_NORMAL:
		dev_info(ci->dev, "normal-> cool\n");
		ci->battemp_region = RT5025_BATTEMP_COOL;
		rt5025_set_tempalrt(ci);
		pval.intval = 1;
		rc = ci->psy.set_property(&ci->psy, POWER_SUPPLY_PROP_PRESENT,\
			&pval);
		if (rc < 0)
			dev_err(ci->dev, "set present fail\n");
		break;
	case RT5025_BATTEMP_WARM:
		dev_info(ci->dev, "warm-> normal\n");
		ci->battemp_region = RT5025_BATTEMP_NORMAL;
		rt5025_set_tempalrt(ci);
		pval.intval = 1;
		rc = ci->psy.set_property(&ci->psy, POWER_SUPPLY_PROP_PRESENT,\
			&pval);
		if (rc < 0)
			dev_err(ci->dev, "set present fail\n");
		break;
	case RT5025_BATTEMP_HOT:
		dev_info(ci->dev, "hot-> warm\n");
		ci->battemp_region = RT5025_BATTEMP_WARM;
		rt5025_set_tempalrt(ci);
		pval.intval = 1;
		rc = ci->psy.set_property(&ci->psy, POWER_SUPPLY_PROP_PRESENT,\
			&pval);
		if (rc < 0)
			dev_err(ci->dev, "set present fail\n");
		break;
	default:
		break;
	}
	#endif /* #ifdef CONFIG_BATTERY_RT5025 */
}

static void rt5025_talrtmin_irq_handler(void *info, int eventno)
{
	#ifdef CONFIG_BATTERY_RT5025
	struct rt5025_charger_info *ci = info;
	union power_supply_propval pval;
	int rc = 0;

	switch (ci->battemp_region) {
	case RT5025_BATTEMP_COLD:
		dev_info(ci->dev, "cold-> cool\n");
		ci->battemp_region = RT5025_BATTEMP_COOL;
		rt5025_set_tempalrt(ci);
		pval.intval = 1;
		rc = ci->psy.set_property(&ci->psy, POWER_SUPPLY_PROP_PRESENT,\
			&pval);
		if (rc < 0)
			dev_err(ci->dev, "set present fail\n");
		break;
	case RT5025_BATTEMP_COOL:
		dev_info(ci->dev, "cool-> normal\n");
		ci->battemp_region = RT5025_BATTEMP_NORMAL;
		rt5025_set_tempalrt(ci);
		pval.intval = 1;
		rc = ci->psy.set_property(&ci->psy, POWER_SUPPLY_PROP_PRESENT,\
			&pval);
		if (rc < 0)
			dev_err(ci->dev, "set present fail\n");
		break;
	case RT5025_BATTEMP_NORMAL:
			dev_info(ci->dev, "normal-> warm\n");
			ci->battemp_region = RT5025_BATTEMP_WARM;
			rt5025_set_tempalrt(ci);
			pval.intval = 1;
			rc = ci->psy.set_property(&ci->psy,
				POWER_SUPPLY_PROP_PRESENT,\
				&pval);
			if (rc < 0)
				dev_err(ci->dev, "set present fail\n");
			break;
	case RT5025_BATTEMP_WARM:
		dev_info(ci->dev, "warm-> hot\n");
		ci->battemp_region = RT5025_BATTEMP_HOT;
		rt5025_set_tempalrt(ci);
		pval.intval = 1;
		rc = ci->psy.set_property(&ci->psy, POWER_SUPPLY_PROP_PRESENT,\
			&pval);
		if (rc < 0)
			dev_err(ci->dev, "set present fail\n");
		break;
	case RT5025_BATTEMP_HOT:
		dev_warn(ci->dev, "hot than hot???\n");
		break;
	default:
		break;
	}
	#endif /* #ifdef CONFIG_BATTERY_RT5025 */
}

static void rt5025_general_irq_handler(void *info, int eventno)
{
	struct rt5025_charger_info *ci = info;

	RTINFO("eventno=%02d\n", eventno);
	switch (eventno) {
	case CHGEVENT_CHRCHGI:
		if (!ci->online) {
			dev_warn(ci->dev, "recharge false alarm\n");
		} else {
			union power_supply_propval pval;

			dev_info(ci->dev, "recharge occur\n");
			ci->chg_status = POWER_SUPPLY_STATUS_CHARGING;
			pval.intval = ci->chg_volt;
			rt_charger_set_batt_status(ci);
			ci->psy.set_property(&ci->psy,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
		}
		break;
	case CHGEVENT_CHTERMI:
		if (!ci->online) {
				dev_warn(ci->dev, "eoc false alarm\n");
		} else {
			if (rt_charger_check_battery_present(ci)) {
				union power_supply_propval pval;

				if (ci->chg_status == POWER_SUPPLY_STATUS_FULL)
					return;
				dev_info(ci->dev, "eoc really occur\n");
				ci->chg_status = POWER_SUPPLY_STATUS_FULL;
				rt_charger_set_batt_status(ci);
				pval.intval = ci->chg_volt-50;
				ci->psy.set_property(&ci->psy,
					POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
			} else {
				dev_info(ci->dev, "no battery condition\n");
				rt5025_batabs_irq_handler(ci, eventno);
			}
		}
		break;
	case CHGEVENT_TALRTMAX:
		#ifdef CONFIG_BATTERY_RT5025
		rt5025_set_tempalrt(ci);
		#endif /* #ifdef CONFIG_BATTERY_RT5025 */
		break;
	case CHGEVENT_TALRTMIN:
		#ifdef CONFIG_BATTERY_RT5025
		rt5025_set_tempalrt(ci);
		#endif /* #ifdef CONFIG_BATTERY_RT5025 */
		break;
	default:
		break;
	}
}

static rt_irq_handler rt_chgirq_handler[CHGEVENT_MAX] = {
	[CHGEVENT_TIMEOUT_CC] = rt5025_general_irq_handler,
	[CHGEVENT_TIMEOUT_PC] = rt5025_general_irq_handler,
	[CHGEVENT_CHVSREGI] = rt5025_general_irq_handler,
	[CHGEVENT_CHTREGI] = rt5025_general_irq_handler,
	[CHGEVENT_CHRCHGI] = rt5025_general_irq_handler,
	[CHGEVENT_CHTERMI] = rt5025_general_irq_handler,
	[CHGEVENT_CHBATOVI] = rt5025_general_irq_handler,
	[CHGEVENT_CHGOODI_INUSB] = rt5025_general_irq_handler,
	[CHGEVENT_CHBADI_INUSB] = rt5025_general_irq_handler,
	[CHGEVENT_CHSLPI_INUSB] = rt5025_usbout_irq_handler,
	[CHGEVENT_CHGOODI_INAC] = rt5025_general_irq_handler,
	[CHGEVENT_CHBADI_INAC] = rt5025_general_irq_handler,
	[CHGEVENT_CHSLPI_INAC] = rt5025_acout_irq_handler,
	[CHGEVENT_BATABS] = rt5025_batabs_irq_handler,
	[CHGEVENT_INUSB_PLUGIN] = rt5025_usbin_irq_handler,
	[CHGEVENT_INUSBOVP] = rt5025_general_irq_handler,
	[CHGEVENT_INAC_PLUGIN] = rt5025_acin_irq_handler,
	[CHGEVENT_INACOVP] = rt5025_general_irq_handler,
	[CHGEVENT_TALRTMIN] = rt5025_talrtmin_irq_handler,
	[CHGEVENT_TALRTMAX] = rt5025_talrtmax_irq_handler,
};

void rt5025_charger_irq_handler(struct rt5025_charger_info *ci, unsigned int irqevent)
{
	int i;
	#ifdef CONFIG_BATTERY_RT5025
	unsigned int enable_irq_event = (RT5025_TALRTMX_MASK | RT5025_TALRTMN_MASK)<<24 | \
			(chg_init_regval[6] << 16) | (chg_init_regval[7] << 8) |  chg_init_regval[8];
	#else
	unsigned int enable_irq_event = (chg_init_regval[6] << 16)
		| (chg_init_regval[7] << 8)| \
				chg_init_regval[8];
	#endif /* #ifdef CONFIG_BATTERY_RT5025 */
	unsigned int final_irq_event = irqevent&enable_irq_event;

	/*charger workaround (TE+RECHARGE)*/
	if (final_irq_event & (1 << CHGEVENT_CHTERMI) && \
		final_irq_event & (1 << CHGEVENT_CHRCHGI))
		final_irq_event &= ~((1 << CHGEVENT_CHTERMI) | (1 << CHGEVENT_CHRCHGI));
	i = rt5025_reg_read(ci->i2c, RT5025_REG_CHGCTL1);
	if (i < 0) {
		dev_err(ci->dev, "read CHGCTL1 fail\n");
		i = 0;
	}
	/*acin+acout*/
	if (final_irq_event & (1 << CHGEVENT_INAC_PLUGIN) && \
		final_irq_event & (1 << CHGEVENT_CHSLPI_INAC)) {
		if (i & RT5025_ACUSABLE_MASK)
			final_irq_event &= ~(1<<CHGEVENT_CHSLPI_INAC);
		else
			final_irq_event &= ~(1<<CHGEVENT_INAC_PLUGIN);
	}
	/*usbin+usbout*/
	if (final_irq_event & (1 << CHGEVENT_INUSB_PLUGIN) && \
		final_irq_event & (1 << CHGEVENT_CHSLPI_INUSB)) {
		if (i & RT5025_USBUSABLE_MASK)
			final_irq_event &= ~(1 << CHGEVENT_CHSLPI_INUSB);
		else
			final_irq_event &= ~(1 << CHGEVENT_INUSB_PLUGIN);
	}
	for (i = 0; i < CHGEVENT_MAX; i++) {
		if ((final_irq_event & (1 << i)) && rt_chgirq_handler[i])
			rt_chgirq_handler[i](ci, i);
	}
}
EXPORT_SYMBOL(rt5025_charger_irq_handler);

static void rt5025_tempmon_work(struct work_struct *work)
{
	struct rt5025_charger_info *ci = container_of(work, \
		struct rt5025_charger_info, tempmon_work.work);
	#ifdef CONFIG_BATTERY_RT5025
	struct power_supply *psy = power_supply_get_by_name(RT_BATT_NAME);
	union power_supply_propval pval;
	int inttemp_region;
	#endif

	RTINFO("\n");
	#ifdef CONFIG_BATTERY_RT5025
		if (!psy) {
			dev_err(ci->dev, "could not get rt-battery psy\n");
			return;
		}
		if (!ci->init_once) {
			/*battemp init*/
			int i = 0;

			pval.intval = 23; /* magic code*/
			psy->get_property(psy, POWER_SUPPLY_PROP_TEMP, &pval);
			for (i = 3; i >= 0; i--)
				if (pval.intval > ci->temp[i])
					break;
			if (i == 3)
				ci->battemp_region = RT5025_BATTEMP_HOT;
			else if (i == 2)
				ci->battemp_region = RT5025_BATTEMP_WARM;
			else if (i == 1)
				ci->battemp_region = RT5025_BATTEMP_NORMAL;
			else if (i == 0)
				ci->battemp_region = RT5025_BATTEMP_COOL;
			else
				ci->battemp_region = RT5025_BATTEMP_COLD;
			rt5025_set_tempalrt(ci);
			ci->init_once = 1;
		}

		psy->get_property(psy, POWER_SUPPLY_PROP_TEMP_AMBIENT, &pval);
		if (pval.intval > 1000)
			inttemp_region = RT5025_INTTEMP_HOT;
		else if (pval.intval > 750)
			inttemp_region = RT5025_INTTEMP_WARM;
		else
			inttemp_region = RT5025_INTTEMP_NORMAL;

		if (inttemp_region != ci->inttemp_region) {
			ci->inttemp_region = inttemp_region;
			pval.intval = 1;
			rt_charger_set_property(&ci->psy,
				POWER_SUPPLY_PROP_PRESENT, &pval);
		}
	#endif /* #ifdef CONFIG_BATTERY_RT5025 */
	if (!ci->suspend)
		schedule_delayed_work(&ci->tempmon_work, 5*HZ);
}

static int rt5025_charger_reginit(struct i2c_client *client)
{
	rt5025_reg_block_write(client, RT5025_REG_CHGCTL2, 6, chg_init_regval);
	/*set all to be masked*/
	rt5025_reg_write(client, RT5025_REG_IRQEN1, 0x00);
	rt5025_reg_write(client, RT5025_REG_IRQEN2, 0x00);
	rt5025_reg_write(client, RT5025_REG_IRQEN3, 0x00);
	/*just clear the old irq event*/
	rt5025_reg_read(client, RT5025_REG_IRQSTAT1);
	rt5025_reg_read(client, RT5025_REG_IRQSTAT2);
	rt5025_reg_read(client, RT5025_REG_IRQSTAT3);
	/*set enable irqs as we want*/
	rt5025_reg_write(client, RT5025_REG_IRQEN1, chg_init_regval[6]);
	rt5025_reg_write(client, RT5025_REG_IRQEN2, chg_init_regval[7]);
	rt5025_reg_write(client, RT5025_REG_IRQEN3, chg_init_regval[8]);
	RTINFO("\n");
	return 0;
}

static int rt_parse_dt(struct rt5025_charger_info *ci, struct device *dev)
{
	#ifdef CONFIG_OF
	struct device_node *np = dev->of_node;
	u32 val;

	if (of_property_read_bool(np, "rt,te_en"))
		ci->te_en = 1;

	if (of_property_read_u32(np, "rt,iprec", &val)) {
		dev_info(dev, "no iprec property, use default value\n");
	} else{
		if (val > RT5025_IPREC_MAX)
			val = RT5025_IPREC_MAX;
		chg_init_regval[4] &= (~RT5025_CHGIPREC_MASK);
		chg_init_regval[4] |= (val << RT5025_CHGIPREC_SHFT);
	}

	if (of_property_read_u32(np, "rt,ieoc", &val)) {
		dev_info(dev, "no ieoc property, use the default value\n");
	} else {
		if (val  > RT5025_IEOC_MAX)
			val = RT5025_IEOC_MAX;
		chg_init_regval[4] &= (~RT5025_CHGIEOC_MASK);
		chg_init_regval[4] |= (val << RT5025_CHGIEOC_SHFT);
	}

	if (of_property_read_u32(np, "rt,vprec", &val)) {
		dev_info(dev, "no vprec property, use the default value\n");
	} else {
		if (val > RT5025_VPREC_MAX)
			val = RT5025_VPREC_MAX;
		chg_init_regval[4] &= (~RT5025_CHGVPREC_MASK);
		chg_init_regval[4] |= (val << RT5025_CHGVPREC_SHFT);
	}

	if (of_property_read_u32(np, "rt,vdpm", &val)) {
		dev_info(dev, "no vdpm property, use the default value\n");
	} else {
		if (val > RT5025_VDPM_MAX)
			val = RT5025_VDPM_MAX;
		chg_init_regval[3] &= (~RT5025_CHGVDPM_MASK);
		chg_init_regval[3] |= (val << RT5025_CHGVDPM_SHFT);
	}

	if (of_property_read_u32(np, "rt,chg_volt", &val)) {
		dev_info(dev, "no chg_volt property, use 4200 as the default value\n");
		ci->chg_volt = 4200;
	} else {
		ci->chg_volt = val;
	}

	if (of_property_read_u32(np, "rt,acchg_icc", &val)) {
		dev_info(dev, "no acchg_icc property, use 2000 as the default value\n");
		ci->acchg_icc = 2000;
	} else {
		ci->acchg_icc = val;
	}

	if (of_property_read_u32(np, "rt,usbtachg_icc", &val)) {
		dev_info(dev, "no usbtachg_icc property, use 2000 as the default value\n");
		ci->usbtachg_icc = 2000;
	} else {
		ci->usbtachg_icc = val;
	}

	if (of_property_read_u32(np, "rt,usbchg_icc", &val)) {
		dev_info(dev, "no usbchg_icc property, use 500 as the default value\n");
		ci->usbchg_icc = 500;
	} else {
		ci->usbchg_icc = val;
	}

	if (of_property_read_u32(np, "rt,screenon_icc", &val)) {
		dev_info(dev, "no screenon_icc property, use 500 as the default value\n");
		ci->screenon_icc = 500;
	} else {
		ci->screenon_icc = val;
	}

	if (of_property_read_bool(np, "rt,screenon_adjust")) {
		ci->screenon_adjust = 1;
		ci->screen_on = 1;
	}

	if (of_property_read_u32_array(np, "rt,temp",
		ci->temp, 4)) {
		dev_info(dev, "no temperature property, use default value\n");
		ci->temp[0] = 0;
		ci->temp[1] = 150;
		ci->temp[2] = 500;
		ci->temp[3] = 600;
	}

	if (of_property_read_u32_array(np, "rt,temp_scalar",
		ci->temp_scalar, 8)) {
		dev_info(dev, "no temp_scalar property, use default value\n");
		ci->temp_scalar[0] = 0x30;
		ci->temp_scalar[1] = 0x2B;
		ci->temp_scalar[2] = 0x28;
		ci->temp_scalar[3] = 0x22;
		ci->temp_scalar[4] = 0x15;
		ci->temp_scalar[5] = 0x10;
		ci->temp_scalar[6] = 0x10;
		ci->temp_scalar[7] = 0x0D;
	}
	#endif /* #ifdef CONFIG_OF */
	rt5025_charger_reginit(ci->i2c);
	RTINFO("\n");
	return 0;
}

static int rt_parse_pdata(struct rt5025_charger_info *ci, struct device *dev)
{
	struct rt5025_charger_data *pdata = dev->platform_data;
	int i = 0;

	if (pdata->te_en)
		ci->te_en = 1;

	chg_init_regval[4] &= (~RT5025_CHGIPREC_MASK);
	chg_init_regval[4] |= (pdata->iprec << RT5025_CHGIPREC_SHFT);

	chg_init_regval[4] &= (~RT5025_CHGIEOC_MASK);
	chg_init_regval[4] |= (pdata->ieoc << RT5025_CHGIEOC_SHFT);

	chg_init_regval[4] &= (~RT5025_CHGVPREC_MASK);
	chg_init_regval[4] |= (pdata->vprec << RT5025_CHGVPREC_SHFT);

	chg_init_regval[3] &= (~RT5025_CHGVDPM_MASK);
	chg_init_regval[3] |= (pdata->vdpm << RT5025_CHGVDPM_SHFT);

	ci->chg_volt = pdata->chg_volt;
	ci->acchg_icc = pdata->acchg_icc;
	ci->usbtachg_icc = pdata->usbtachg_icc;
	ci->usbchg_icc = pdata->usbchg_icc;
	ci->screenon_icc = pdata->screenon_icc;
	if (pdata->screenon_adjust) {
		ci->screenon_adjust = 1;
		/*default probe screen will on*/
		ci->screen_on = 1;
	}
	for (i = 0; i < 4; i++)
		ci->temp[i] = pdata->temp[i];
	for (i = 0; i < 8; i++)
		ci->temp_scalar[i] = pdata->temp_scalar[i];
	rt5025_charger_reginit(ci->i2c);
	RTINFO("\n");
	return 0;
}

#ifdef CONFIG_RT_POWER
static struct platform_device rt_power_dev = {
	.name = "rt-power",
	.id = -1,
};
#endif /* #ifdef CONFIG_RT_POWER */

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rt5025_charger_earlysuspend(struct early_suspend *handler)
{
	struct rt5025_charger_info *ci = container_of(handler, \
		struct rt5025_charger_info, early_suspend);
	union power_supply_propval pval;
	int rc = 0;

	if (ci->screenon_adjust) {
		ci->screen_on = 0;
		pval.intval = 1;
		rc = ci->psy.set_property(&ci->psy,
			POWER_SUPPLY_PROP_PRESENT, &pval);
		if (rc < 0)
			dev_err(ci->dev, "set charger present property fail\n");
	}
}

static void rt5025_charger_earlyresume(struct early_suspend *handler)
{
	struct rt5025_charger_info *ci = container_of(handler, \
		struct rt5025_charger_info, early_suspend);
	union power_supply_propval pval;
	int rc = 0;

	if (ci->screenon_adjust) {
		ci->screen_on = 1;
		pval.intval = 1;
		rc = ci->psy.set_property(&ci->psy,
			POWER_SUPPLY_PROP_PRESENT, &pval);
		if (rc < 0)
			dev_err(ci->dev, "set charger present property fail\n");
	}
}
#endif /* #ifdef CONFIG_HAS_EARLYSUSPEND */

static int rt5025_charger_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_platform_data *pdata =
		(pdev->dev.parent)->platform_data;
	#ifdef CONFIG_RT_POWER
	struct rt_power_data *rt_power_pdata;
	#endif /* #ifdef CONFIG_RT_POWER */
	struct rt5025_charger_info *ci;
	bool use_dt = pdev->dev.of_node;
	int ret = 0;

	ci = devm_kzalloc(chip->dev, sizeof(struct rt5025_charger_info), GFP_KERNEL);
	if (!ci)
		return -ENOMEM;

	ci->i2c = chip->i2c;
	ci->dev = &pdev->dev;
	ci->chg_status = POWER_SUPPLY_STATUS_DISCHARGING;
	ci->battemp_region = RT5025_BATTEMP_NORMAL;
	ci->inttemp_region = RT5025_INTTEMP_NORMAL;
	#ifdef CONFIG_RT_JEITA_REMOVE
	ci->init_once = 1;
	#endif /* #ifdef RT_JEITA_REMOVE */

	if (use_dt) {
		rt_parse_dt(ci, &pdev->dev);
	} else {
		if (!pdata) {
			ret = -EINVAL;
			goto out_dev;
		}
		pdev->dev.platform_data = pdata->chg_pdata;
		rt_parse_pdata(ci, &pdev->dev);
	}
	INIT_DELAYED_WORK(&ci->tempmon_work, rt5025_tempmon_work);

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
		dev_err(&pdev->dev, "couldn't create power supply for rt-charger\n");
		goto out_dev;
	}

	#ifdef CONFIG_HAS_EARLYSUSPEND
	ci->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1;
	ci->early_suspend.suspend = rt5025_charger_earlysuspend;
	ci->early_suspend.resume = rt5025_charger_earlyresume;
	register_early_suspend(&ci->early_suspend);
	#endif /* CONFIG_HAS_EARLYSUSPEND */

	#ifdef CONFIG_RT_POWER
	rt_power_pdata = devm_kzalloc(&pdev->dev,
		sizeof(*rt_power_pdata), GFP_KERNEL);
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

	chip->charger_info = ci;
	schedule_delayed_work(&ci->tempmon_work, 1*HZ);
	dev_info(&pdev->dev, "driver successfully loaded\n");
	return 0;
#ifdef CONFIG_RT_POWER
out_psy:
#endif /* #ifdef CONFIG_RT_POWER */
	#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ci->early_suspend);
	#endif /* #ifdef CONFIG_HAS_EARLYSUSPEND */
	power_supply_unregister(&ci->psy);
out_dev:
	return ret;
}

static int rt5025_charger_remove(struct platform_device *pdev)
{
	struct rt5025_charger_info *ci = platform_get_drvdata(pdev);

	power_supply_unregister(&ci->psy);
	#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ci->early_suspend);
	#endif /* #ifdef CONFIG_HAS_EARLYSUSPEND */
	#ifdef CONFIG_RT_POWER
	platform_device_unregister(&rt_power_dev);
	#endif /* #ifdef CONFIG_RT_POWER */
	return 0;
}

static int rt5025_charger_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct rt5025_charger_info *ci = platform_get_drvdata(pdev);
	union power_supply_propval pval;

	ci->suspend = 1;
	cancel_delayed_work_sync(&ci->tempmon_work);
	/*force inttemp to normal temp*/
	ci->inttemp_region = RT5025_INTTEMP_NORMAL;
	pval.intval = 1;
	rt_charger_set_property(&ci->psy, POWER_SUPPLY_PROP_PRESENT, &pval);
	return 0;
}

static int rt5025_charger_resume(struct platform_device *pdev)
{
	struct rt5025_charger_info *ci = platform_get_drvdata(pdev);

	ci->suspend = 0;
	schedule_delayed_work(&ci->tempmon_work, msecs_to_jiffies(50));
	return 0;
}

static const struct of_device_id rt_match_table[] = {
	{ .compatible = "rt,rt5025-charger",},
	{},
};

static struct platform_driver rt5025_charger_driver = {
	.driver = {
		.name = RT5025_DEV_NAME "-charger",
		.owner = THIS_MODULE,
		.of_match_table = rt_match_table,
	},
	.probe = rt5025_charger_probe,
	.remove = rt5025_charger_remove,
	.suspend = rt5025_charger_suspend,
	.resume = rt5025_charger_resume,
};

static int rt5025_charger_init(void)
{
	return platform_driver_register(&rt5025_charger_driver);
}
fs_initcall_sync(rt5025_charger_init);

static void rt5025_charger_exit(void)
{
	platform_driver_unregister(&rt5025_charger_driver);
}

module_exit(rt5025_charger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Charger driver for RT5025");
MODULE_ALIAS("platform:"RT5025_DEV_NAME "-charger");
MODULE_VERSION(RT5025_DRV_VER);
