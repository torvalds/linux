/*
 * linux/drivers/power/twl6030_bci_battery.c
 *
 * OMAP4:TWL6030 battery driver for Linux
 *
 * Copyright (C) 2008-2009 Texas Instruments, Inc.
 * Author: Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/i2c/twl.h>
#include <linux/power_supply.h>
#include <linux/i2c/twl6030-gpadc.h>
#include <linux/i2c/bq2415x.h>
#include <linux/wakelock.h>
#include <linux/usb/otg.h>

#include <mach/gpio.h>

#define CONTROLLER_INT_MASK	0x00
#define CONTROLLER_CTRL1	0x01
#define CONTROLLER_WDG		0x02
#define CONTROLLER_STAT1	0x03
#define CHARGERUSB_INT_STATUS	0x04
#define CHARGERUSB_INT_MASK	0x05
#define CHARGERUSB_STATUS_INT1	0x06
#define CHARGERUSB_STATUS_INT2	0x07
#define CHARGERUSB_CTRL1	0x08
#define CHARGERUSB_CTRL2	0x09
#define CHARGERUSB_CTRL3	0x0A
#define CHARGERUSB_STAT1	0x0B
#define CHARGERUSB_VOREG	0x0C
#define CHARGERUSB_VICHRG	0x0D
#define CHARGERUSB_CINLIMIT	0x0E
#define CHARGERUSB_CTRLLIMIT1	0x0F
#define CHARGERUSB_CTRLLIMIT2	0x10
#define ANTICOLLAPSE_CTRL1	0x11
#define ANTICOLLAPSE_CTRL2	0x12

/* TWL6032 registers 0xDA to 0xDE - TWL6032_MODULE_CHARGER */
#define CONTROLLER_CTRL2	0x00
#define CONTROLLER_VSEL_COMP	0x01
#define CHARGERUSB_VSYSREG	0x02
#define CHARGERUSB_VICHRG_PC	0x03
#define LINEAR_CHRG_STS		0x04

/* TWL6032 Charger Mode Register */
#define CHARGER_MODE_REG	0xD4
#define CHARGER_MODE_POWERPATH	BIT(3)
#define CHARGER_MODE_AUTOCHARGE	BIT(6)

#define LINEAR_CHRG_STS_CRYSTL_OSC_OK	0x40
#define LINEAR_CHRG_STS_END_OF_CHARGE	0x20
#define LINEAR_CHRG_STS_VBATOV		0x10
#define LINEAR_CHRG_STS_VSYSOV		0x08
#define LINEAR_CHRG_STS_DPPM_STS	0x04
#define LINEAR_CHRG_STS_CV_STS		0x02
#define LINEAR_CHRG_STS_CC_STS		0x01

#define FG_REG_00	0x00
#define FG_REG_01	0x01
#define FG_REG_02	0x02
#define FG_REG_03	0x03
#define FG_REG_04	0x04
#define FG_REG_05	0x05
#define FG_REG_06	0x06
#define FG_REG_07	0x07
#define FG_REG_08	0x08
#define FG_REG_09	0x09
#define FG_REG_10	0x0A
#define FG_REG_11	0x0B

/* CONTROLLER_INT_MASK */
#define MVAC_FAULT		(1 << 7)
#define MAC_EOC			(1 << 6)
#define LINCH_GATED		(1 << 5)
#define MBAT_REMOVED		(1 << 4)
#define MFAULT_WDG		(1 << 3)
#define MBAT_TEMP		(1 << 2)
#define MVBUS_DET		(1 << 1)
#define MVAC_DET		(1 << 0)

/* CONTROLLER_CTRL1 */
#define CONTROLLER_CTRL1_EN_LINCH	(1 << 5)
#define CONTROLLER_CTRL1_EN_CHARGER	(1 << 4)
#define CONTROLLER_CTRL1_SEL_CHARGER	(1 << 3)

/* CONTROLLER_STAT1 */
#define CONTROLLER_STAT1_EXTCHRG_STATZ	(1 << 7)
#define CONTROLLER_STAT1_LINCH_GATED	(1 << 6)
#define CONTROLLER_STAT1_CHRG_DET_N	(1 << 5)
#define CONTROLLER_STAT1_FAULT_WDG	(1 << 4)
#define CONTROLLER_STAT1_VAC_DET	(1 << 3)
#define VAC_DET	(1 << 3)
#define CONTROLLER_STAT1_VBUS_DET	(1 << 2)
#define VBUS_DET	(1 << 2)
#define CONTROLLER_STAT1_BAT_REMOVED	(1 << 1)
#define CONTROLLER_STAT1_BAT_TEMP_OVRANGE (1 << 0)

/* CHARGERUSB_INT_STATUS */
#define EN_LINCH		(1 << 4)
#define CURRENT_TERM_INT	(1 << 3)
#define CHARGERUSB_STAT		(1 << 2)
#define CHARGERUSB_THMREG	(1 << 1)
#define CHARGERUSB_FAULT	(1 << 0)

/* CHARGERUSB_INT_MASK */
#define MASK_MCURRENT_TERM		(1 << 3)
#define MASK_MCHARGERUSB_STAT		(1 << 2)
#define MASK_MCHARGERUSB_THMREG		(1 << 1)
#define MASK_MCHARGERUSB_FAULT		(1 << 0)

/* CHARGERUSB_STATUS_INT1 */
#define CHARGERUSB_STATUS_INT1_TMREG	(1 << 7)
#define CHARGERUSB_STATUS_INT1_NO_BAT	(1 << 6)
#define CHARGERUSB_STATUS_INT1_BST_OCP	(1 << 5)
#define CHARGERUSB_STATUS_INT1_TH_SHUTD	(1 << 4)
#define CHARGERUSB_STATUS_INT1_BAT_OVP	(1 << 3)
#define CHARGERUSB_STATUS_INT1_POOR_SRC	(1 << 2)
#define CHARGERUSB_STATUS_INT1_SLP_MODE	(1 << 1)
#define CHARGERUSB_STATUS_INT1_VBUS_OVP	(1 << 0)

/* CHARGERUSB_STATUS_INT2 */
#define ICCLOOP		(1 << 3)
#define CURRENT_TERM	(1 << 2)
#define CHARGE_DONE	(1 << 1)
#define ANTICOLLAPSE	(1 << 0)

/* CHARGERUSB_CTRL1 */
#define SUSPEND_BOOT	(1 << 7)
#define OPA_MODE	(1 << 6)
#define HZ_MODE		(1 << 5)
#define TERM		(1 << 4)

/* CHARGERUSB_CTRL2 */
#define CHARGERUSB_CTRL2_VITERM_50	(0 << 5)
#define CHARGERUSB_CTRL2_VITERM_100	(1 << 5)
#define CHARGERUSB_CTRL2_VITERM_150	(2 << 5)
#define CHARGERUSB_CTRL2_VITERM_400	(7 << 5)

/* CHARGERUSB_CTRL3 */
#define VBUSCHRG_LDO_OVRD	(1 << 7)
#define CHARGE_ONCE		(1 << 6)
#define BST_HW_PR_DIS		(1 << 5)
#define AUTOSUPPLY		(1 << 3)
#define BUCK_HSILIM		(1 << 0)

/* CHARGERUSB_VOREG */
#define CHARGERUSB_VOREG_3P52		0x01
#define CHARGERUSB_VOREG_4P0		0x19
#define CHARGERUSB_VOREG_4P2		0x23
#define CHARGERUSB_VOREG_4P76		0x3F

/* CHARGERUSB_VICHRG */
#define CHARGERUSB_VICHRG_300		0x0
#define CHARGERUSB_VICHRG_500		0x4
#define CHARGERUSB_VICHRG_1500		0xE

/* CHARGERUSB_CINLIMIT */
#define CHARGERUSB_CIN_LIMIT_100	0x1
#define CHARGERUSB_CIN_LIMIT_300	0x5
#define CHARGERUSB_CIN_LIMIT_500	0x9
#define CHARGERUSB_CIN_LIMIT_NONE	0xF

/* CHARGERUSB_CTRLLIMIT1 */
#define VOREGL_4P16			0x21
#define VOREGL_4P56			0x35

/* CHARGERUSB_CTRLLIMIT2 */
#define CHARGERUSB_CTRLLIMIT2_1500	0x0E
#define		LOCK_LIMIT		(1 << 4)

/* ANTICOLLAPSE_CTRL2 */
#define BUCK_VTH_SHIFT			5

/* FG_REG_00 */
#define CC_ACTIVE_MODE_SHIFT	6
#define CC_AUTOCLEAR		(1 << 2)
#define CC_CAL_EN		(1 << 1)
#define CC_PAUSE		(1 << 0)

#define REG_TOGGLE1		0x90
#define FGDITHS			(1 << 7)
#define FGDITHR			(1 << 6)
#define FGS			(1 << 5)
#define FGR			(1 << 4)

/* TWL6030_GPADC_CTRL */
#define GPADC_CTRL_TEMP1_EN	(1 << 0)    /* input ch 1 */
#define GPADC_CTRL_TEMP2_EN	(1 << 1)    /* input ch 4 */
#define GPADC_CTRL_SCALER_EN	(1 << 2)    /* input ch 2 */
#define GPADC_CTRL_SCALER_DIV4	(1 << 3)
#define GPADC_CTRL_SCALER_EN_CH11	(1 << 4)    /* input ch 11 */
#define GPADC_CTRL_TEMP1_EN_MONITOR	(1 << 5)
#define GPADC_CTRL_TEMP2_EN_MONITOR	(1 << 6)
#define GPADC_CTRL_ISOURCE_EN		(1 << 7)

#define GPADC_ISOURCE_22uA		22
#define GPADC_ISOURCE_7uA		7

/* TWL6030/6032 BATTERY VOLTAGE GPADC CHANNELS */

#define TWL6030_GPADC_VBAT_CHNL	0x07
#define TWL6032_GPADC_VBAT_CHNL	0x12

/* TWL6030_GPADC_CTRL2 */
#define GPADC_CTRL2_CH18_SCALER_EN	BIT(2)

#define ENABLE_ISOURCE		0x80

#define REG_MISC1		0xE4
#define VAC_MEAS		0x04
#define VBAT_MEAS		0x02
#define BB_MEAS			0x01

#define REG_USB_VBUS_CTRL_SET	0x04
#define VBUS_MEAS		0x01
#define REG_USB_ID_CTRL_SET	0x06
#define ID_MEAS			0x01

#define BBSPOR_CFG		0xE6
#define	BB_CHG_EN		(1 << 3)

#define STS_HW_CONDITIONS	0x21
#define STS_USB_ID		(1 << 2)	/* Level status of USB ID */

#define BATTERY_RESISTOR	10000
#define SIMULATOR_RESISTOR	5000
#define BATTERY_DETECT_THRESHOLD	((BATTERY_RESISTOR + SIMULATOR_RESISTOR) / 2)   //battery voltage threshold divided by 22uA
#define CHARGING_CAPACITY_UPDATE_PERIOD	(1000 * 60 * 1)



/************************************************************/
#define TIMER_MS_COUNTS     1000
#define  NUM_DISCHARGE_MIN_SAMPLE   30
#define NUM_CHARGE_MIN_SAMPLE   30
/**************************************************************/

/* To get VBUS input limit from twl6030_usb */
#if CONFIG_TWL6030_USB
extern unsigned int twl6030_get_usb_max_power(struct otg_transceiver *x);
#else
static inline unsigned int twl6030_get_usb_max_power(struct otg_transceiver *x)
{
	return 0;
};
#endif

/* Ptr to thermistor table */
static const unsigned int fuelgauge_rate[4] = {1, 4, 16, 64};
static struct wake_lock chrg_lock;


struct twl6030_bci_device_info {
	struct device		*dev;

	int			voltage_mV;
	int			bk_voltage_mV;
	int			current_uA;
	int			current_avg_uA;
	int			temp_C;
	int			charge_status;
	int			vac_priority;
	int			bat_health;
	int			charger_source;

	int			fuelgauge_mode;
	int			timer_n2;
	int			timer_n1;
	s32			charge_n1;
	s32			charge_n2;
	s16			cc_offset;
	u8			usb_online;
	u8			ac_online;
	u8			stat1;
	u8			linear_stat;
	u8			status_int1;
	u8			status_int2;

	u8			gpadc_vbat_chnl;
	u8			watchdog_duration;
	u16			current_avg_interval;
	u16			monitoring_interval;
	unsigned int		min_vbus;

	struct			twl4030_bci_platform_data *platform_data;

	unsigned int		charger_incurrentmA;
	unsigned int		charger_outcurrentmA;
	unsigned long		usb_max_power;
	unsigned long		event;

	unsigned int		capacity;
	unsigned int		capacity_debounce_count;
	unsigned long		ac_next_refresh;
	unsigned int		prev_capacity;
	unsigned int		wakelock_enabled;

	struct power_supply	bat;
	struct power_supply	usb;
	struct power_supply	ac;
	struct power_supply	bk_bat;

	struct otg_transceiver	*otg;
	struct notifier_block	nb;
	struct work_struct	usb_work;

	struct workqueue_struct	*freezable_work;

	struct delayed_work	twl6030_bci_monitor_work;
	struct delayed_work	twl6030_current_avg_work;

	unsigned long		features;

	int			use_hw_charger;
	int			use_power_path;

	/* max scale current based on sense resitor */
	int			current_max_scale;
	struct delayed_work work;
	unsigned int interval;

	int 	gBatCapacityDisChargeCnt;
	int           capacitytmp;
	int           usb_status;
	int           usb_old_satus;
	int 	gBatCapacityChargeCnt;
};

static BLOCKING_NOTIFIER_HEAD(notifier_list);
extern u32 wakeup_timer_seconds;

static void twl6030_config_min_vbus_reg(struct twl6030_bci_device_info *di,
						unsigned int value)
{
	u8 rd_reg = 0;
	int ret;

	/* not required on TWL6032 */
	if (di->features & TWL6032_SUBCLASS)
		return;

	if (value > 4760 || value < 4200) {
		dev_err(di->dev, "invalid min vbus\n");
		return;
	}

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &rd_reg,
					ANTICOLLAPSE_CTRL2);
	if (ret)
		goto err;
	rd_reg = rd_reg & 0x1F;
	rd_reg = rd_reg | (((value - 4200)/80) << BUCK_VTH_SHIFT);
	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, rd_reg,
					ANTICOLLAPSE_CTRL2);

	if (!ret)
		return;
err:
	pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
}

static void twl6030_config_iterm_reg(struct twl6030_bci_device_info *di,
						unsigned int term_currentmA)
{
	int ret;

	if ((term_currentmA > 400) || (term_currentmA < 50)) {
		dev_err(di->dev, "invalid termination current\n");
		return;
	}

	term_currentmA = ((term_currentmA - 50)/50) << 5;
	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, term_currentmA,
						CHARGERUSB_CTRL2);
	if (ret)
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
}

static unsigned int twl6030_get_iterm_reg(struct twl6030_bci_device_info *di)
{
	int ret;
	unsigned int currentmA;
	u8 val = 0;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &val, CHARGERUSB_CTRL2);
	if (ret) {
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
		currentmA = 0;
	} else
		currentmA = 50 + (val >> 5) * 50;

	return currentmA;
}

static void twl6030_config_voreg_reg(struct twl6030_bci_device_info *di,
							unsigned int voltagemV)
{
	int ret;

	if ((voltagemV < 3500) || (voltagemV > 4760)) {
		dev_err(di->dev, "invalid charger_voltagemV\n");
		return;
	}

	voltagemV = (voltagemV - 3500) / 20;
	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, voltagemV,
						CHARGERUSB_VOREG);
	if (ret)
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
}

static unsigned int twl6030_get_voreg_reg(struct twl6030_bci_device_info *di)
{
	int ret;
	unsigned int voltagemV;
	u8 val = 0;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &val, CHARGERUSB_VOREG);
	if (ret) {
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
		voltagemV = 0;
	} else
		voltagemV = 3500 + (val * 20);

	return voltagemV;
}

static void twl6030_config_vichrg_reg(struct twl6030_bci_device_info *di,
							unsigned int currentmA)
{
	int ret;

	if ((currentmA >= 300) && (currentmA <= 450))
		currentmA = (currentmA - 300) / 50;
	else if ((currentmA >= 500) && (currentmA <= 1500))
		currentmA = (currentmA - 500) / 100 + 4;
	else {
		dev_err(di->dev, "invalid charger_currentmA\n");
		return;
	}

	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, currentmA,
						CHARGERUSB_VICHRG);
	if (ret)
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
}

static void twl6030_config_cinlimit_reg(struct twl6030_bci_device_info *di,
							unsigned int currentmA)
{
	int ret;

	if ((currentmA >= 50) && (currentmA <= 750))
		currentmA = (currentmA - 50) / 50;
	else if ((currentmA > 750) && (currentmA <= 1500) &&
			(di->features & TWL6032_SUBCLASS)) {
			currentmA = ((currentmA % 100) ? 0x30 : 0x20) +
						((currentmA - 100) / 100);
	} else if (currentmA < 50) {
		dev_err(di->dev, "invalid input current limit\n");
		return;
	} else {
		/* This is no current limit */
		currentmA = 0x0F;
	}

	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, currentmA,
					CHARGERUSB_CINLIMIT);
	if (ret)
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
}

static void twl6030_config_limit1_reg(struct twl6030_bci_device_info *di,
							unsigned int voltagemV)
{
	int ret;

	if ((voltagemV < 3500) || (voltagemV > 4760)) {
		dev_err(di->dev, "invalid max_charger_voltagemV\n");
		return;
	}

	voltagemV = (voltagemV - 3500) / 20;
	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, voltagemV,
						CHARGERUSB_CTRLLIMIT1);
	if (ret)
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
}

static unsigned int twl6030_get_limit1_reg(struct twl6030_bci_device_info *di)
{
	int ret;
	unsigned int voltagemV;
	u8 val = 0;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &val,
				CHARGERUSB_CTRLLIMIT1);
	if (ret) {
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
		voltagemV = 0;
	} else
		voltagemV = 3500 + (val * 20);

	return voltagemV;
}

static void twl6030_config_limit2_reg(struct twl6030_bci_device_info *di,
							unsigned int currentmA)
{
	int ret;

	if ((currentmA >= 300) && (currentmA <= 450))
		currentmA = (currentmA - 300) / 50;
	else if ((currentmA >= 500) && (currentmA <= 1500))
		currentmA = (currentmA - 500) / 100 + 4;
	else {
		dev_err(di->dev, "invalid max_charger_currentmA\n");
		return;
	}

	currentmA |= LOCK_LIMIT;
	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, currentmA,
						CHARGERUSB_CTRLLIMIT2);
	if (ret)
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
}

static const int vichrg[] = {
	300, 350, 400, 450, 500, 600, 700, 800,
	900, 1000, 1100, 1200, 1300, 1400, 1500, 300
};

static unsigned int twl6030_get_limit2_reg(struct twl6030_bci_device_info *di)
{
	int ret;
	unsigned int currentmA;
	u8 val = 0;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &val,
					CHARGERUSB_CTRLLIMIT2);
	if (ret) {
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
		currentmA = 0;
	} else
		currentmA = vichrg[val & 0xF];

	return currentmA;
}

/*
 * Return channel value
 * Or < 0 on failure.
 */
static int twl6030_get_gpadc_conversion(struct twl6030_bci_device_info *di,
					int channel_no)
{
	struct twl6030_gpadc_request req;
	int temp = 0;
	int ret;

	req.channels = (1 << channel_no);
	req.method = TWL6030_GPADC_SW2;
	req.active = 0;
	req.func_cb = NULL;
	ret = twl6030_gpadc_conversion(&req);
	if (ret < 0)
		return ret;

	if (req.rbuf[channel_no] > 0)
		temp = req.rbuf[channel_no];

	return temp;
}

static int is_battery_present(struct twl6030_bci_device_info *di)
{
	int val;
	static unsigned int current_src_val;

	/*
	 * Prevent charging on batteries were id resistor is
	 * less than 5K.
	 */
	val = twl6030_get_gpadc_conversion(di,di->gpadc_vbat_chnl);

	/*
	 * twl6030_get_gpadc_conversion for
	 * 6030 return resistance, for 6032 - voltage and
	 * it should be converted to resistance before
	 * using.
	 */
	if (!current_src_val) {
		u8 reg = 0;

		if (twl_i2c_read_u8(TWL_MODULE_MADC, &reg,
					TWL6030_GPADC_CTRL))
			pr_err("%s: Error reading TWL6030_GPADC_CTRL\n",
				__func__);

		current_src_val = (reg & GPADC_CTRL_ISOURCE_EN) ?
					GPADC_ISOURCE_22uA :
					GPADC_ISOURCE_7uA;
	}

	val = (val * 1000) / current_src_val;

	if (val < BATTERY_DETECT_THRESHOLD)
		return 0;

	return 1;
}

static void twl6030_stop_usb_charger(struct twl6030_bci_device_info *di)
{
	int ret;
	u8 reg;
	//printk("*************twl6030_stop_usb_charger***************\n");
	if (di->use_hw_charger) {
		ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &reg,
				CHARGERUSB_CTRL1);
		if (ret)
			goto err;
		reg |= HZ_MODE;
		ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, reg,
				CHARGERUSB_CTRL1);
		if (ret)
			goto err;

		return;
	}

	di->charger_source = 0;
	di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;

	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, 0, CONTROLLER_CTRL1);

err:
	if (ret)
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
}

static void twl6030_start_usb_charger(struct twl6030_bci_device_info *di)
{
	int ret;
	u8 reg;


//printk("***************twl6030_start_usb_charger*****************************************\n");

	if (di->use_hw_charger) {
		ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &reg,
				CHARGERUSB_CTRL1);
		if (ret)
			goto err;

		reg &= ~HZ_MODE;
		ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, reg,
				CHARGERUSB_CTRL1);
		if (ret)
			goto err;

		return;
	}

	if (!is_battery_present(di)) {
		dev_info(di->dev, "BATTERY NOT DETECTED!\n");
		return;
	}

	if (di->charger_source == POWER_SUPPLY_TYPE_MAINS)
		return;

	dev_dbg(di->dev, "USB input current limit %dmA\n",
					di->charger_incurrentmA);
	if (di->charger_incurrentmA < 50) {
		ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER,
					0, CONTROLLER_CTRL1);
		if (ret)
			goto err;

		return;
	}

	twl6030_config_vichrg_reg(di, di->charger_outcurrentmA);
	twl6030_config_cinlimit_reg(di, di->charger_incurrentmA);
	twl6030_config_voreg_reg(di, di->platform_data->max_bat_voltagemV);
	twl6030_config_iterm_reg(di, di->platform_data->termination_currentmA);

	if (di->charger_incurrentmA >= 50) {
		reg = CONTROLLER_CTRL1_EN_CHARGER;

		if (di->use_power_path)
			reg |= CONTROLLER_CTRL1_EN_LINCH;

		ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, reg,
				CONTROLLER_CTRL1);
		if (ret)
			goto err;

		di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
	}
	return;
err:
	pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
}

static void twl6030_stop_ac_charger(struct twl6030_bci_device_info *di)
{
	long int events;
	int ret;

	di->charger_source = 0;
	di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;
	events = BQ2415x_STOP_CHARGING;

	if (di->use_hw_charger)
		return;

	blocking_notifier_call_chain(&notifier_list, events, NULL);

	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, 0, CONTROLLER_CTRL1);
	if (ret)
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);

	if (di->wakelock_enabled)
		wake_unlock(&chrg_lock);
}

static void twl6030_start_ac_charger(struct twl6030_bci_device_info *di)
{
	long int events;
	int ret;

	if (!is_battery_present(di)) {
		dev_info(di->dev, "BATTERY NOT DETECTED!\n");
		return;
	}
	dev_dbg(di->dev, "AC charger detected\n");
	di->charger_source = POWER_SUPPLY_TYPE_MAINS;
	di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
	events = BQ2415x_START_CHARGING;

	if (di->use_hw_charger)
		return;

	blocking_notifier_call_chain(&notifier_list, events, NULL);

	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER,
			CONTROLLER_CTRL1_EN_CHARGER |
			CONTROLLER_CTRL1_SEL_CHARGER,
			CONTROLLER_CTRL1);
	if (ret)
		pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);

	if (di->wakelock_enabled)
		wake_lock(&chrg_lock);
}

static void twl6030_stop_charger(struct twl6030_bci_device_info *di)
{
	//printk("$$$$$$$$$$$$twl6030_stop_charger$$$$$$$$$$$$\n");
	if (di->charger_source == POWER_SUPPLY_TYPE_MAINS)
		twl6030_stop_ac_charger(di);
	else if (di->charger_source == POWER_SUPPLY_TYPE_USB)
		twl6030_stop_usb_charger(di);
}

static void twl6032_charger_ctrl_interrupt(struct twl6030_bci_device_info *di)
{
	u8 stat_toggle, stat_reset, stat_set = 0;
	u8 present_state = 0, linear_state;
	u8 present_status = 0;
	int err;

	err = twl_i2c_read_u8(TWL6032_MODULE_CHARGER, &present_state,
			LINEAR_CHRG_STS);
	if (err < 0) {
		dev_err(di->dev, "%s: Error access to TWL6030 (%d)\n",
								__func__, err);
		return;
	}

	err = twl_i2c_read_u8(TWL6032_MODULE_CHARGER, &present_status,
			CHARGERUSB_INT_STATUS);
	if (err < 0) {
		dev_err(di->dev, "%s: Error access to TWL6030 (%d)\n",
								__func__, err);
		return;
	}

	linear_state = di->linear_stat;

	stat_toggle = linear_state ^ present_state;
	stat_set = stat_toggle & present_state;
	stat_reset = stat_toggle & linear_state;
	di->linear_stat = present_state;

	if (stat_set & LINEAR_CHRG_STS_CRYSTL_OSC_OK)
		dev_dbg(di->dev, "Linear status: CRYSTAL OSC OK\n");
	if (present_state & LINEAR_CHRG_STS_END_OF_CHARGE) {
		dev_dbg(di->dev, "Linear status: END OF CHARGE\n");
		di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}
	if (present_status & EN_LINCH) {
		dev_dbg(di->dev, "Linear status: START OF CHARGE\n");
		di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
	}

	if (stat_set & LINEAR_CHRG_STS_VBATOV) {
		dev_dbg(di->dev, "Linear Status: VBATOV\n");
		di->bat_health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	}
	if (stat_reset & LINEAR_CHRG_STS_VBATOV) {
		dev_dbg(di->dev, "Linear Status: VBATOV\n");
		di->bat_health = POWER_SUPPLY_HEALTH_GOOD;
	}

	if (stat_set & LINEAR_CHRG_STS_VSYSOV)
		dev_dbg(di->dev, "Linear Status: VSYSOV\n");
	if (stat_set & LINEAR_CHRG_STS_DPPM_STS)
		dev_dbg(di->dev, "Linear Status: DPPM STS\n");
	if (stat_set & LINEAR_CHRG_STS_CV_STS)
		dev_dbg(di->dev, "Linear Status: CV STS\n");
	if (stat_set & LINEAR_CHRG_STS_CC_STS)
		dev_dbg(di->dev, "Linear Status: CC STS\n");
}

/*
 * Interrupt service routine
 *
 * Attends to TWL 6030 power module interruptions events, specifically
 * USB_PRES (USB charger presence) CHG_PRES (AC charger presence) events
 *
 */
static irqreturn_t twl6030charger_ctrl_interrupt(int irq, void *_di)
{
//	printk("%s\n", __func__);

	struct twl6030_bci_device_info *di = _di;
	int ret;
	int charger_fault = 0;
	long int events;
	u8 stat_toggle, stat_reset, stat_set = 0;
	u8 charge_state = 0;
	u8 present_charge_state = 0;
	u8 ac_or_vbus, no_ac_and_vbus = 0;
	u8 hw_state = 0, temp = 0;

	/* read charger controller_stat1 */
	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &present_charge_state,
		CONTROLLER_STAT1);
	if (ret) {
		/*
		 * Since present state read failed, charger_state is no
		 * longer valid, reset to zero inorder to detect next events
		 */
		charge_state = 0;
		return IRQ_NONE;
	}

	ret = twl_i2c_read_u8(TWL6030_MODULE_ID0, &hw_state, STS_HW_CONDITIONS);
	if (ret)
		goto err;

	charge_state = di->stat1;

	stat_toggle = charge_state ^ present_charge_state;
	stat_set = stat_toggle & present_charge_state;
	stat_reset = stat_toggle & charge_state;

	no_ac_and_vbus = !((present_charge_state) & (VBUS_DET | VAC_DET));
	ac_or_vbus = charge_state & (VBUS_DET | VAC_DET);
	if (no_ac_and_vbus && ac_or_vbus) {
		di->charger_source = 0;
		dev_dbg(di->dev, "No Charging source\n");
		/* disable charging when no source present */
	}

	charge_state = present_charge_state;
	di->stat1 = present_charge_state;
	if ((charge_state & VAC_DET) &&
		(charge_state & CONTROLLER_STAT1_EXTCHRG_STATZ)) {
		events = BQ2415x_CHARGER_FAULT;
		blocking_notifier_call_chain(&notifier_list, events, NULL);
	}

	if (stat_reset & VBUS_DET) {
		/* On a USB detach, UNMASK VBUS OVP if masked*/
		ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &temp,
				CHARGERUSB_INT_MASK);
		if (ret)
			goto err;

		if (temp & MASK_MCHARGERUSB_FAULT) {
			ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER,
					(temp & ~MASK_MCHARGERUSB_FAULT),
					CHARGERUSB_INT_MASK);
			if (ret)
				goto err;
		}
		di->usb_online = 0;
		dev_info(di->dev, "usb removed\n");
		twl6030_stop_usb_charger(di);
		if (present_charge_state & VAC_DET)
			twl6030_start_ac_charger(di);

	}

	if (stat_set & VBUS_DET) {
		/* In HOST mode (ID GROUND) when a device is connected,
		 * Mask VBUS OVP interrupt and do no enable usb
		 * charging
		 */
		if (hw_state & STS_USB_ID) {
			ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER,
					&temp,
					CHARGERUSB_INT_MASK);
			if (ret)
				goto err;

			if (!(temp & MASK_MCHARGERUSB_FAULT)) {
				ret = twl_i2c_write_u8(
						TWL6030_MODULE_CHARGER,
						(temp | MASK_MCHARGERUSB_FAULT),
						CHARGERUSB_INT_MASK);
				if (ret)
					goto err;
			}
		} else {
			di->usb_online = POWER_SUPPLY_TYPE_USB;
			if ((present_charge_state & VAC_DET) &&
					(di->vac_priority == 2))
				dev_info(di->dev, "USB charger detected"
						", continue with VAC\n");
			else {
				di->charger_source =
						POWER_SUPPLY_TYPE_USB;
				di->charge_status =
						POWER_SUPPLY_STATUS_CHARGING;
			}
			dev_info(di->dev, "vbus detect\n");
		}
	}

	if (stat_reset & VAC_DET) {
		di->ac_online = 0;
		dev_info(di->dev, "vac removed\n");
		twl6030_stop_ac_charger(di);
		if (present_charge_state & VBUS_DET) {
			di->charger_source = POWER_SUPPLY_TYPE_USB;
			di->charge_status =
					POWER_SUPPLY_STATUS_CHARGING;
			twl6030_start_usb_charger(di);
		}
	}
	if (stat_set & VAC_DET) {
		di->ac_online = POWER_SUPPLY_TYPE_MAINS;
		if ((present_charge_state & VBUS_DET) &&
				(di->vac_priority == 3))
			dev_info(di->dev,
					"AC charger detected"
					", continue with VBUS\n");
		else
			twl6030_start_ac_charger(di);
	}

	if (stat_set & CONTROLLER_STAT1_FAULT_WDG) {
		charger_fault = 1;
		dev_info(di->dev, "Fault watchdog fired\n");
	}
	if (stat_reset & CONTROLLER_STAT1_FAULT_WDG)
		dev_err(di->dev, "Fault watchdog recovered\n");
	if (stat_set & CONTROLLER_STAT1_BAT_REMOVED)
		dev_err(di->dev, "Battery removed\n");
	if (stat_reset & CONTROLLER_STAT1_BAT_REMOVED)
		dev_err(di->dev, "Battery inserted\n");
	if (stat_set & CONTROLLER_STAT1_BAT_TEMP_OVRANGE) {
		dev_err(di->dev, "Battery temperature overrange\n");
		di->bat_health = POWER_SUPPLY_HEALTH_OVERHEAT;
	}
	if (stat_reset & CONTROLLER_STAT1_BAT_TEMP_OVRANGE) {
		dev_dbg(di->dev, "Battery temperature within range\n");
		di->bat_health = POWER_SUPPLY_HEALTH_GOOD;
	}
	if (di->features & TWL6032_SUBCLASS)
		twl6032_charger_ctrl_interrupt(di);

	if (charger_fault) {
		twl6030_stop_usb_charger(di);
		di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		dev_dbg(di->dev, "Charger Fault stop charging\n");
	}

	if (di->capacity != -1)
		power_supply_changed(&di->bat);
	else {
		cancel_delayed_work(&di->twl6030_bci_monitor_work);
		queue_delayed_work(di->freezable_work, &di->twl6030_bci_monitor_work, 0);
	}
err:
	return IRQ_HANDLED;
}

static irqreturn_t twl6030charger_fault_interrupt(int irq, void *_di)
{
	struct twl6030_bci_device_info *di = _di;
	int charger_fault = 0;
	int ret;

	u8 usb_charge_sts = 0, usb_charge_sts1 = 0, usb_charge_sts2 = 0;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &usb_charge_sts,
						CHARGERUSB_INT_STATUS);
	if (ret)
		goto err;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &usb_charge_sts1,
						CHARGERUSB_STATUS_INT1);
	if (ret)
		goto err;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &usb_charge_sts2,
						CHARGERUSB_STATUS_INT2);
	if (ret)
		goto err;

	di->status_int1 = usb_charge_sts1;
	di->status_int2 = usb_charge_sts2;
	if (usb_charge_sts & CURRENT_TERM_INT)
		dev_dbg(di->dev, "USB CURRENT_TERM_INT\n");
	if (usb_charge_sts & CHARGERUSB_THMREG)
		dev_dbg(di->dev, "USB CHARGERUSB_THMREG\n");
	if (usb_charge_sts & CHARGERUSB_FAULT)
		dev_dbg(di->dev, "USB CHARGERUSB_FAULT\n");

	if (usb_charge_sts1 & CHARGERUSB_STATUS_INT1_TMREG)
		dev_dbg(di->dev, "USB CHARGER Thermal regulation activated\n");
	if (usb_charge_sts1 & CHARGERUSB_STATUS_INT1_NO_BAT)
		dev_dbg(di->dev, "No Battery Present\n");
	if (usb_charge_sts1 & CHARGERUSB_STATUS_INT1_BST_OCP)
		dev_dbg(di->dev, "USB CHARGER Boost Over current protection\n");
	if (usb_charge_sts1 & CHARGERUSB_STATUS_INT1_TH_SHUTD) {
		charger_fault = 1;
		dev_dbg(di->dev, "USB CHARGER Thermal Shutdown\n");
	}
	if (usb_charge_sts1 & CHARGERUSB_STATUS_INT1_BAT_OVP)
		dev_dbg(di->dev, "USB CHARGER Bat Over Voltage Protection\n");
	if (usb_charge_sts1 & CHARGERUSB_STATUS_INT1_POOR_SRC)
		dev_dbg(di->dev, "USB CHARGER Poor input source\n");
	if (usb_charge_sts1 & CHARGERUSB_STATUS_INT1_SLP_MODE)
		dev_dbg(di->dev, "USB CHARGER Sleep mode\n");
	if (usb_charge_sts1 & CHARGERUSB_STATUS_INT1_VBUS_OVP)
		dev_dbg(di->dev, "USB CHARGER VBUS over voltage\n");

	if (usb_charge_sts2 & CHARGE_DONE) {
		di->charge_status = POWER_SUPPLY_STATUS_FULL;
		dev_dbg(di->dev, "USB charge done\n");
	}
	if (usb_charge_sts2 & CURRENT_TERM)
		dev_dbg(di->dev, "USB CURRENT_TERM\n");
	if (usb_charge_sts2 & ICCLOOP)
		dev_dbg(di->dev, "USB ICCLOOP\n");
	if (usb_charge_sts2 & ANTICOLLAPSE)
		dev_dbg(di->dev, "USB ANTICOLLAPSE\n");

	if (charger_fault) {
		twl6030_stop_usb_charger(di);
		di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		dev_dbg(di->dev, "Charger Fault stop charging\n");
	}
	dev_info(di->dev, "Charger fault detected STS, INT1, INT2 %x %x %x\n",
	    usb_charge_sts, usb_charge_sts1, usb_charge_sts2);

	power_supply_changed(&di->bat);
err:
	return IRQ_HANDLED;
}

/*
 * In HW charger mode on 6032 irq routines must only deal with updating
 * state of charger. The hardware deals with start/stop conditions
 * automatically.
 */
static irqreturn_t twl6032charger_ctrl_interrupt_hw(int irq, void *_di)
{
	struct twl6030_bci_device_info *di = _di;
	u8 stat1, linear;
	int charger_stop = 0, end_of_charge = 0;
	int ret;

	//printk("xxxxxxxxxxxxxxxxxxxxxxxx twl6032charger_ctrl_interrupt_hw   xxxxxxxxxxxxxx\n");
	/* read charger controller_stat1 */
	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &stat1,
			CONTROLLER_STAT1);
	if (ret)
		goto out;

	ret = twl_i2c_read_u8(TWL6032_MODULE_CHARGER, &linear,
			LINEAR_CHRG_STS);
	if (ret < 0)
		goto out;

	if (!(stat1 & (VBUS_DET | VAC_DET))) {
		charger_stop = 1;
		di->ac_online = di->usb_online = 0;
		//printk("%%%%%%%%%%%%%%charger_stop = 1%%%%%%%%%%%%%\n");
	}

	if (!(di->usb_online || di->ac_online)) {
		if (stat1 & VBUS_DET) {
			di->usb_online = 1;
			di->bat_health = POWER_SUPPLY_HEALTH_GOOD;
//			printk("%%%%%%%%%%%%%%di->usb_online = 1%%%%%%%%%%%%%\n");

			//schedule_work(&di->usb_work);
		} else if (stat1 & VAC_DET) {
			di->ac_online = 1;
			di->bat_health = POWER_SUPPLY_HEALTH_GOOD;
//			printk("%%%%%%%%%%%%%%di->ac_online = 1%%%%%%%%%%%%%\n");
		}
	}

	if (stat1 & CONTROLLER_STAT1_FAULT_WDG) {
		charger_stop = 1;
		di->bat_health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		 dev_err(di->dev, "Charger error : Fault watchdog\n");
	}
	if (stat1 & CONTROLLER_STAT1_BAT_REMOVED) {
		charger_stop = 1;
		di->bat_health = POWER_SUPPLY_HEALTH_DEAD;
		dev_info(di->dev, "Battery removed\n");
	}
	if (stat1 & CONTROLLER_STAT1_BAT_TEMP_OVRANGE) {
		charger_stop = 1;
		 dev_dbg(di->dev,
			"Charger error : Battery temperature overrange\n");
		di->bat_health = POWER_SUPPLY_HEALTH_OVERHEAT;
	}

	if ((stat1 & CONTROLLER_STAT1_LINCH_GATED) &&
			di->use_power_path) {

		charger_stop = 1;

		if (linear & LINEAR_CHRG_STS_CRYSTL_OSC_OK) {
			di->bat_health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			 dev_dbg(di->dev, "Charger error: CRYSTAL OSC OK\n");
		}

		if (linear & LINEAR_CHRG_STS_END_OF_CHARGE) {
			end_of_charge = 1;
			di->bat_health = POWER_SUPPLY_HEALTH_GOOD;
			dev_dbg(di->dev, "Charger: Full charge\n");
		}

		if (linear & LINEAR_CHRG_STS_VBATOV) {
			di->bat_health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
			 dev_dbg(di->dev,
				"Charger error : Linear Status: VBATOV\n");
		}

		if (linear & LINEAR_CHRG_STS_VSYSOV) {
			di->bat_health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			 dev_dbg(di->dev,
				"Charger error : Linear Status: VSYSOV\n");
		}
	}

	if (charger_stop) {
		if (!(stat1 & (VBUS_DET | VAC_DET))) {
			di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;
		} else {
			if (end_of_charge)
				di->charge_status =
					POWER_SUPPLY_STATUS_FULL;
			else
				di->charge_status =
					POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
	}
	//printk("%%%%%%%%%%%%%%%%%%twl6032charger_ctrl_interrupt_hw  end  xxxxxxxxxxxxxx\n");

	power_supply_changed(&di->bat);

out:
	return IRQ_HANDLED;
}

static irqreturn_t twl6032charger_fault_interrupt_hw(int irq, void *_di)
{
	struct twl6030_bci_device_info *di = _di;
	int charger_stop = 0, charger_start = 0;
	int ret;
	u8 sts, sts_int1, sts_int2, stat1;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &sts,
						CHARGERUSB_INT_STATUS);
	if (ret)
		goto out;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &sts_int1,
						CHARGERUSB_STATUS_INT1);
	if (ret)
		goto out;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &sts_int2,
						CHARGERUSB_STATUS_INT2);
	if (ret)
		goto out;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &stat1,
						CONTROLLER_STAT1);
	if (ret)
		goto out;

	if (sts & EN_LINCH) {
		charger_start = 1;
		dev_dbg(di->dev, "Charger: EN_LINCH\n");
		goto out;
	}

	if ((sts & CURRENT_TERM_INT) && !di->use_power_path) {
		dev_dbg(di->dev, "Charger: CURRENT_TERM_INT\n");

		if (sts_int2 & CURRENT_TERM) {
			charger_stop = 1;
			 dev_dbg(di->dev, "Charger error: CURRENT_TERM\n");
		}
	}

	if (sts & CHARGERUSB_STAT) {
		dev_dbg(di->dev, "Charger: CHARGEUSB_STAT\n");

		if (sts_int2 & ANTICOLLAPSE)
			 dev_dbg(di->dev, "Charger error: ANTICOLLAPSE\n");
	}

	if (sts & CHARGERUSB_THMREG) {
		dev_dbg(di->dev, "Charger: CHARGERUSB_THMREG\n");

		if (sts_int1 & CHARGERUSB_STATUS_INT1_TMREG)
			dev_dbg(di->dev, "Charger error: TMREG\n");
	}

	if (sts & CHARGERUSB_FAULT) {
		dev_dbg(di->dev, "Charger: CHARGERUSB_FAULT\n");

		charger_stop = 1;

		if (!di->use_power_path) {
			if (sts_int1 & CHARGERUSB_STATUS_INT1_NO_BAT) {
				di->bat_health = POWER_SUPPLY_HEALTH_DEAD;
				dev_err(di->dev,
					"Charger error : NO_BAT\n");
			}
			if (sts_int1 & CHARGERUSB_STATUS_INT1_BAT_OVP) {
				di->bat_health =
					POWER_SUPPLY_HEALTH_OVERVOLTAGE;
				dev_dbg(di->dev, "Charger error : BAT_OVP\n");
			}
		}

		if (sts_int1 & CHARGERUSB_STATUS_INT1_BST_OCP) {
			di->bat_health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			printk(KERN_ERR "Charger error : BST_OCP\n");
		}
		if (sts_int1 & CHARGERUSB_STATUS_INT1_TH_SHUTD) {
			di->bat_health = POWER_SUPPLY_HEALTH_OVERHEAT;
			printk(KERN_ERR "Charger error : TH_SHUTD\n");
		}
		if (sts_int1 & CHARGERUSB_STATUS_INT1_POOR_SRC) {
			di->bat_health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			printk(KERN_ERR "Charger error : POOR_SRC\n");
		}
		if (sts_int1 & CHARGERUSB_STATUS_INT1_SLP_MODE)
			dev_dbg(di->dev, "Charger error: SLP_MODE\n");

		if (sts_int1 & CHARGERUSB_STATUS_INT1_VBUS_OVP) {
			di->bat_health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			printk(KERN_ERR "Charger error : VBUS_OVP\n");
		}
	}

	if (charger_stop) {
		if (!(stat1 & (VBUS_DET | VAC_DET)))
			di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;
		else
			di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}

out:
	if (charger_start) {
		di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
		di->bat_health = POWER_SUPPLY_HEALTH_GOOD;
	}

	power_supply_changed(&di->bat);

	return IRQ_HANDLED;
}

static void twl6030battery_current(struct twl6030_bci_device_info *di)
{
	int ret = 0;
	u16 read_value = 0;
	s16 temp = 0;
	int current_now = 0;

	/* FG_REG_10, 11 is 14 bit signed instantaneous current sample value */
	ret = twl_i2c_read(TWL6030_MODULE_GASGAUGE, (u8 *)&read_value,
								FG_REG_10, 2);
	if (ret < 0) {
		dev_info(di->dev, "failed to read FG_REG_10: current_now\n");
		return;
	}

	temp = ((s16)(read_value << 2) >> 2);
	current_now = temp - di->cc_offset;

	/* current drawn per sec */
	current_now = current_now * fuelgauge_rate[di->fuelgauge_mode];
	/* current in mAmperes */
	current_now = (current_now * di->current_max_scale) >> 13;
	/* current in uAmperes */
	current_now = current_now * 1000;
	di->current_uA = current_now;

	return;
}

/*
 * Setup the twl6030 BCI module to enable backup
 * battery charging.
 */
static int twl6030backupbatt_setup(void)
{
	int ret;
	u8 rd_reg = 0;

	ret = twl_i2c_read_u8(TWL6030_MODULE_ID0, &rd_reg, BBSPOR_CFG);
	if (ret)
		return ret;

	rd_reg |= BB_CHG_EN;
	ret = twl_i2c_write_u8(TWL6030_MODULE_ID0, rd_reg, BBSPOR_CFG);

	return ret;
}

/*
 * Setup the twl6030 BCI module to measure battery
 * temperature
 */
static int twl6030battery_temp_setup(bool enable)
{
	int ret;
	u8 rd_reg = 0;

	ret = twl_i2c_read_u8(TWL_MODULE_MADC, &rd_reg, TWL6030_GPADC_CTRL);
	if (ret)
		return ret;

	if (enable)
		rd_reg |= (GPADC_CTRL_TEMP1_EN | GPADC_CTRL_TEMP2_EN |
			GPADC_CTRL_TEMP1_EN_MONITOR |
			GPADC_CTRL_TEMP2_EN_MONITOR | GPADC_CTRL_SCALER_DIV4);
	else
		rd_reg ^= (GPADC_CTRL_TEMP1_EN | GPADC_CTRL_TEMP2_EN |
			GPADC_CTRL_TEMP1_EN_MONITOR |
			GPADC_CTRL_TEMP2_EN_MONITOR | GPADC_CTRL_SCALER_DIV4);

	ret = twl_i2c_write_u8(TWL_MODULE_MADC, rd_reg, TWL6030_GPADC_CTRL);

	return ret;
}

static int twl6030battery_voltage_setup(struct twl6030_bci_device_info *di)
{
	int ret;
	u8 rd_reg = 0;

	ret = twl_i2c_read_u8(TWL6030_MODULE_ID0, &rd_reg, REG_MISC1);
	if (ret)
		return ret;

	rd_reg = rd_reg | VAC_MEAS | VBAT_MEAS | BB_MEAS;
	ret = twl_i2c_write_u8(TWL6030_MODULE_ID0, rd_reg, REG_MISC1);
	if (ret)
		return ret;

	ret = twl_i2c_read_u8(TWL_MODULE_USB, &rd_reg, REG_USB_VBUS_CTRL_SET);
	if (ret)
		return ret;

	rd_reg = rd_reg | VBUS_MEAS;
	ret = twl_i2c_write_u8(TWL_MODULE_USB, rd_reg, REG_USB_VBUS_CTRL_SET);
	if (ret)
		return ret;

	ret = twl_i2c_read_u8(TWL_MODULE_USB, &rd_reg, REG_USB_ID_CTRL_SET);
	if (ret)
		return ret;

	rd_reg = rd_reg | ID_MEAS;
	ret = twl_i2c_write_u8(TWL_MODULE_USB, rd_reg, REG_USB_ID_CTRL_SET);
	if (ret)
		return ret;

	if (di->features & TWL6032_SUBCLASS)
		ret = twl_i2c_write_u8(TWL_MODULE_MADC,
					GPADC_CTRL2_CH18_SCALER_EN,
					TWL6030_GPADC_CTRL2);

	return ret;
}

static int twl6030battery_current_setup(bool enable)
{
	int ret = 0;
	u8  reg = 0;

	/*
	 * Writing 0 to REG_TOGGLE1 has no effect, so
	 * can directly set/reset FG.
	 */
	if (enable)
		reg = FGDITHS | FGS;
	else
		reg = FGDITHR | FGR;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, reg, REG_TOGGLE1);
	if (ret)
		return ret;

	 ret = twl_i2c_write_u8(TWL6030_MODULE_GASGAUGE, CC_CAL_EN, FG_REG_00);

	return ret;
}

static enum power_supply_property twl6030_bci_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
};

static enum power_supply_property twl6030_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static enum power_supply_property twl6030_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static enum power_supply_property twl6030_bk_bci_battery_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static void twl6030_current_avg(struct work_struct *work)
{
//	printk("%s\n", __func__);

	s32 samples = 0;
	s16 cc_offset = 0;
	int current_avg_uA = 0;
	int ret;
	struct twl6030_bci_device_info *di = container_of(work,
		struct twl6030_bci_device_info,
		twl6030_current_avg_work.work);

	di->charge_n2 = di->charge_n1;
	di->timer_n2 = di->timer_n1;

	/* FG_REG_01, 02, 03 is 24 bit unsigned sample counter value */
	ret = twl_i2c_read(TWL6030_MODULE_GASGAUGE, (u8 *) &di->timer_n1,
							FG_REG_01, 3);
	if (ret < 0)
		goto err;
	/*
	 * FG_REG_04, 5, 6, 7 is 32 bit signed accumulator value
	 * accumulates instantaneous current value
	 */
	ret = twl_i2c_read(TWL6030_MODULE_GASGAUGE, (u8 *) &di->charge_n1,
							FG_REG_04, 4);
	if (ret < 0)
		goto err;
	/* FG_REG_08, 09 is 10 bit signed calibration offset value */
	ret = twl_i2c_read(TWL6030_MODULE_GASGAUGE, (u8 *) &cc_offset,
							FG_REG_08, 2);
	if (ret < 0)
		goto err;
	cc_offset = ((s16)(cc_offset << 6) >> 6);
	di->cc_offset = cc_offset;

	samples = di->timer_n1 - di->timer_n2;
	/* check for timer overflow */
	if (di->timer_n1 < di->timer_n2)
		samples = samples + (1 << 24);

	/* offset is accumulative over number of samples */
	cc_offset = cc_offset * samples;

	current_avg_uA = ((di->charge_n1 - di->charge_n2 - cc_offset)
					* di->current_max_scale) /
					fuelgauge_rate[di->fuelgauge_mode];
	/* clock is a fixed 32Khz */
	current_avg_uA >>= 15;

	/* Correct for the fuelguage sampling rate */
	samples /= fuelgauge_rate[di->fuelgauge_mode] * 4;

	/*
	 * Only update the current average if we have had a valid number
	 * of samples in the accumulation.
	 */
	if (samples) {
		current_avg_uA = current_avg_uA / samples;
		di->current_avg_uA = current_avg_uA * 1000;
	}

	queue_delayed_work(di->freezable_work, &di->twl6030_current_avg_work,
		msecs_to_jiffies(1000 * di->current_avg_interval));
	return;
err:
	pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
}
#define BATT_NUM  11
struct batt_vol_cal{
	u32 disp_cal;
	u32 dis_charge_vol;
	u32 charge_vol;
};
static struct batt_vol_cal  batt_table[BATT_NUM] = {
	{0,3571,3730},//{1,3420,3525},{2,3420,3575},{3,3475,3600},{5,3505,3620},{7,3525,3644},
	{9,3628,3770},//{11,3557,3670},{13,3570,3684},{15,3580,3700},{17,3610,3715},
	{19,3670,3878},//{21,3640,3748},{23,3652,3756},{25,3662,3775},{27,3672,3790},
	{29,3719,3895},//{31,3687,3814},{33,3693,3818},{35,3699,3822},{37,3705,3825},
	{39,3770,3941},//{41,3714,3832},{43,3718,3834},{45,3722,3836},{47,3726,3837},
	{49,3801,3980},//{51,3734,3841},{53,3738,3842},{55,3742,3844},{57,3746,3844},
	{59,3849,4010},//{61,3756,3860},{63,3764,3864},{65,3774,3871},{67,3786,3890},
	{69,3864,4024},//{71,3808,3930},{73,3817,3977},{75,3827,3977},{77,3845,3997},
	{79,3884,4040},//{81,3964,4047},{83,3982,4064},{85,4002,4080},{87,4026,4096},
	{89,4001,4050},//{91,4034,4144},{93,4055,4150},{95,4085,4195},{97,4085,4195},
	{100,4070,4090},
};
#if 0
static int capacity_changed(struct twl6030_bci_device_info *di)
{
	int curr_capacity = di->capacity;
	int charger_source = di->charger_source;
	int charging_disabled = 0;
	struct batt_vol_cal *p;
	int i=0;

	p = batt_table;
	/* Because system load is always greater than
	 * termination current, we will never get a CHARGE DONE
	 * int from BQ. And charging will alwys be in progress.
	 * We consider Vbat>3900 to be a full battery.
	 * Since Voltage measured during charging is Voreg ~4.2v,
	 * we dont update capacity if we are charging.
	 */

	/* if it has been more than 10 minutes since our last update
	 * and we are charging we force a update.
	 */
	if (time_after(jiffies, di->ac_next_refresh)
		&& (di->charger_source != POWER_SUPPLY_TYPE_BATTERY)) {
		charging_disabled = 1;
		di->ac_next_refresh = jiffies +
			msecs_to_jiffies(CHARGING_CAPACITY_UPDATE_PERIOD);
		di->capacity = -1;

		/* We have to disable charging to read correct
		 * voltages.
		 */
		twl6030_stop_charger(di);
		/*voltage setteling time*/
		msleep(200);
		di->voltage_mV = twl6030_get_gpadc_conversion(di,
						di->gpadc_vbat_chnl);
		
	}

	/* Setting the capacity level only makes sense when on
	 * the battery is powering the board.
	 */
	 if (di->charge_status == POWER_SUPPLY_STATUS_CHARGING){  //charge
		if(di->voltage_mV >= p[BATT_NUM - 1].charge_vol){
			curr_capacity = 100;
		}	
		else{
			if(di->voltage_mV <= p[0].charge_vol){
				curr_capacity = 0;
			}
			else{
				for(i = 0; i < BATT_NUM - 1; i++){

					if((p[i].charge_vol <= di->voltage_mV) && (di->voltage_mV < (p[i+1].charge_vol))){
						curr_capacity = p[i].disp_cal ;
						break;
					}
				}
			}  
		}

	}
	else if (di->charge_status == POWER_SUPPLY_STATUS_DISCHARGING){  //discharge
		if(di->voltage_mV >= p[BATT_NUM - 1].dis_charge_vol){
			curr_capacity = 100;
		}	
		else{
			if(di->voltage_mV <= p[0].dis_charge_vol){
				curr_capacity = 0;
			}
			else{
				for(i = 0; i < BATT_NUM - 1; i++){
					if(((p[i].dis_charge_vol) <= di->voltage_mV) && (di->voltage_mV < (p[i+1].dis_charge_vol))){
						curr_capacity = p[i].disp_cal ;
						break;
					}
				}
			}  

		}


	}
	
	/*	
	if (di->charge_status == POWER_SUPPLY_STATUS_CHARGING) {
		if (di->voltage_mV < 3520)
			curr_capacity = 0;
		else if (di->voltage_mV < 3600 && di->voltage_mV >= 3500)
			curr_capacity = 20;
		else if (di->voltage_mV < 3700 && di->voltage_mV >= 3600)
			curr_capacity = 50;
		else if (di->voltage_mV < 3800 && di->voltage_mV >= 3700)
			curr_capacity = 75;
		else if (di->voltage_mV < 3900 && di->voltage_mV >= 3800)
			curr_capacity = 90;
		else if (di->voltage_mV >= 3900)
				curr_capacity = 100;
	}
	
*/


	/* if we disabled charging to check capacity,
	 * enable it again after we read the
	 * correct voltage.
	 */
	if (charging_disabled) {
		if (charger_source == POWER_SUPPLY_TYPE_MAINS)
			twl6030_start_ac_charger(di);
		else if (charger_source == POWER_SUPPLY_TYPE_USB)
			twl6030_start_usb_charger(di);
	}

	/* if battery is not present we assume it is on battery simulator and
	 * current capacity is set to 100%
	 */
	if (!is_battery_present(di))
		curr_capacity = 100;
	 

       /* Debouncing of voltage change. */
	if (di->capacity == -1) {
		di->capacity = curr_capacity;
		di->capacity_debounce_count = 0;
		return 1;
	}
/*
	if (curr_capacity != di->prev_capacity) {
		di->prev_capacity = curr_capacity;
		di->capacity_debounce_count = 0;
	} else if (++di->capacity_debounce_count >= 4) {
		di->capacity = curr_capacity;
		di->capacity_debounce_count = 0;
		return 1;
	}
*/
	return 1;
}
#endif
static int  twl6030_batt_vol_to_capacity (struct twl6030_bci_device_info *di)

{

        int i = 0;
        int capacity = 0;
        int BatVoltage;

        struct batt_vol_cal *p;
        p = batt_table;

        BatVoltage = di ->voltage_mV;

        if (di->charge_status == POWER_SUPPLY_STATUS_CHARGING){  //charge
                if(BatVoltage >=  (p[BATT_NUM - 1].charge_vol)){
                        capacity = 100;
                }
                else{
                        if(BatVoltage <= (p[0].charge_vol)){
                                capacity = 0;
                        }
                        else{
                                for(i = 0; i < BATT_NUM - 1; i++){

                                        if(((p[i].charge_vol) <= BatVoltage) && (BatVoltage < (p[i+1].charge_vol))){
                                             //   capacity = p[i].disp_cal ;
			capacity = i * 10 + ((BatVoltage - p[i].charge_vol) * 10) / (p[i+1] .charge_vol- p[i].charge_vol);

					     
                                                break;
                                        }
                                }
                        }
                }

        }
     else{  //discharge
                if(BatVoltage >= (p[BATT_NUM - 1].dis_charge_vol)){
                        capacity = 100;
                }
                else{
                        if(BatVoltage <= (p[0].dis_charge_vol)){
                                capacity = 0;
                        }
                        else{
                                for(i = 0; i < BATT_NUM - 1; i++){
                                        if(((p[i].dis_charge_vol) <= BatVoltage) && (BatVoltage < (p[i+1].dis_charge_vol))){
                                        //        capacity = p[i].disp_cal ;
			capacity = i * 10 + ((BatVoltage - p[i].dis_charge_vol) * 10) / (p[i+1] .dis_charge_vol- p[i].dis_charge_vol);
                                                break;
                                        }
                                }
                        }

                }


        }

        return capacity;

}


                                       
void twl6030_batt_capacity_samples(struct twl6030_bci_device_info *di)
{
	int capacity = 0;
	capacity = twl6030_batt_vol_to_capacity (di);
	 twl6030battery_current(di);
	//printk("old-capactiyt == %d, now-capacity == %d , di->charge_status == %d,di->current_uA  = %d\n", di->capacity, capacity, di->charge_status,di->current_uA);
///////////////////////////////////////////////////////////////////////////////////////////////////////


//	capacity = twl6030_batt_vol_to_capacity (di);

	if (di->charge_status == POWER_SUPPLY_STATUS_FULL){
		if (capacity < di->capacity){
			if (di ->gBatCapacityChargeCnt >= NUM_CHARGE_MIN_SAMPLE){
				di ->gBatCapacityChargeCnt = 0;
				if (di -> capacity < 99){
					di -> capacity++;
				}
			}
			
		}
		
	}
		
	  
	if (di->charge_status == POWER_SUPPLY_STATUS_CHARGING){
		if (capacity > di->capacity){
			//实际采样到的容量比显示的容量大，逐级上升
			if (++(di->gBatCapacityChargeCnt) >= NUM_CHARGE_MIN_SAMPLE){
				di ->gBatCapacityChargeCnt  = 0;
				if (di ->  capacity < 99){
					di -> capacity++;
					
				}
			}
			di->gBatCapacityDisChargeCnt = 0;  //放电的计数值为0
		}

	}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
	if ((di->charge_status == POWER_SUPPLY_STATUS_DISCHARGING)||(di->charge_status == POWER_SUPPLY_STATUS_NOT_CHARGING)){

	//放电时,只允许电压下降
		if (capacity < di -> capacity){
			if (++(di->gBatCapacityDisChargeCnt) >= NUM_DISCHARGE_MIN_SAMPLE){
				di ->gBatCapacityDisChargeCnt = 0;
				if (di -> capacity > 0){
					di -> capacity-- ;
					//bat->bat_change  = 1;
				}
			}
		}
		else{
			di ->gBatCapacityDisChargeCnt = 0;
		}
		//di ->gBatCapacityChargeCnt = 0;
	}
		di ->capacitytmp = capacity;
}

static int twl6030_set_watchdog(struct twl6030_bci_device_info *di, int val)
{
	di->watchdog_duration = val;

	dev_dbg(di->dev, "Watchdog reset %d", val);

	return twl_i2c_write_u8(TWL6030_MODULE_CHARGER, val, CONTROLLER_WDG);

}
#if 0
static void twl6030_bci_adc_set(struct twl6030_bci_device_info *di)
{
	struct twl6030_gpadc_request req;
	int adc_code;
	int temp;
	int ret;
	//int level;

	/* Kick the charger watchdog */
	if (di->charge_status == POWER_SUPPLY_STATUS_CHARGING)
		twl6030_set_watchdog(di, di->watchdog_duration);
	
	req.method = TWL6030_GPADC_SW2;
	req.channels = (1 << 1) | (1 << di->gpadc_vbat_chnl) | (1 << 8);
	req.active = 0;
	req.func_cb = NULL;
	ret = twl6030_gpadc_conversion(&req);

	
	if (ret < 0) {
		dev_dbg(di->dev, "gpadc conversion failed: %d\n", ret);
		return;
	}
	
	if (req.rbuf[di->gpadc_vbat_chnl] > 0)
		di->voltage_mV = req.rbuf[di->gpadc_vbat_chnl];

	if (req.rbuf[8] > 0)
		di->bk_voltage_mV = req.rbuf[8];

	if (di->platform_data->battery_tmp_tbl == NULL)
		return;

	adc_code = req.rbuf[1];
	for (temp = 0; temp < di->platform_data->tblsize; temp++) {
		if (adc_code >= di->platform_data->
				battery_tmp_tbl[temp])
			break;
	}
	//printk("twl6030_bci_adc_set---di->voltage_mV-=-%d,di->capacity = %d \n",di->voltage_mV,di->capacity);
	/* first 2 values are for negative temperature */

}
#endif
static void twl6030_bci_battery_work(struct work_struct *work)
{

	struct twl6030_bci_device_info *di = container_of(work,
		struct twl6030_bci_device_info, twl6030_bci_monitor_work.work);
	struct twl6030_gpadc_request req;
	int adc_code;
	int temp;
	int ret;
//	int level;

	/* Kick the charger watchdog */
	if (di->charge_status == POWER_SUPPLY_STATUS_CHARGING)
		twl6030_set_watchdog(di, di->watchdog_duration);
	
	req.method = TWL6030_GPADC_SW2;
	req.channels = (1 << 1) | (1 << di->gpadc_vbat_chnl) | (1 << 8);
	req.active = 0;
	req.func_cb = NULL;
	ret = twl6030_gpadc_conversion(&req);

	queue_delayed_work(di->freezable_work, &di->twl6030_bci_monitor_work,
			msecs_to_jiffies(TIMER_MS_COUNTS)); //* di->monitoring_interval
	
	if (ret < 0) {
		dev_dbg(di->dev, "gpadc conversion failed: %d\n", ret);
		return;
	}
	
	if (req.rbuf[di->gpadc_vbat_chnl] > 0)
		di->voltage_mV = req.rbuf[di->gpadc_vbat_chnl];

	if (req.rbuf[8] > 0)
		di->bk_voltage_mV = req.rbuf[8];

	if (di->platform_data->battery_tmp_tbl == NULL)
		return;

	adc_code = req.rbuf[1];
	for (temp = 0; temp < di->platform_data->tblsize; temp++) {
		if (adc_code >= di->platform_data->
				battery_tmp_tbl[temp])
			break;
	}
	//printk("workxxxxx---di->voltage_mV-=-%d,di->capacity = %d \n",di->voltage_mV,di->capacity);
	/* first 2 values are for negative temperature */
	di->temp_C = (temp - 2); /* in degrees Celsius */
//	if (capacity_changed(di))  //xsf
	{
		twl6030_batt_capacity_samples(di);
		// di->capacity = level;
		power_supply_changed(&di->bat);
	}
}

static void twl6030_current_mode_changed(struct twl6030_bci_device_info *di)
{
	int ret;
	//printk("%s\n", __func__);

	/* FG_REG_01, 02, 03 is 24 bit unsigned sample counter value */
	ret = twl_i2c_read(TWL6030_MODULE_GASGAUGE, (u8 *) &di->timer_n1,
							FG_REG_01, 3);
	if (ret < 0)
		goto err;
	/*
	 * FG_REG_04, 5, 6, 7 is 32 bit signed accumulator value
	 * accumulates instantaneous current value
	 */
	ret = twl_i2c_read(TWL6030_MODULE_GASGAUGE, (u8 *) &di->charge_n1,
							FG_REG_04, 4);
	if (ret < 0)
		goto err;

	cancel_delayed_work(&di->twl6030_current_avg_work);
	queue_delayed_work(di->freezable_work, &di->twl6030_current_avg_work,
		msecs_to_jiffies(1000 * di->current_avg_interval));
	return;
err:
	pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
}

static void twl6030_work_interval_changed(struct twl6030_bci_device_info *di)
{
	//printk("%s\n", __func__);

	cancel_delayed_work(&di->twl6030_bci_monitor_work);
	queue_delayed_work(di->freezable_work, &di->twl6030_bci_monitor_work,
		msecs_to_jiffies(1000 * di->monitoring_interval));
}

#define to_twl6030_bci_device_info(x) container_of((x), \
			struct twl6030_bci_device_info, bat);
#if 0
static void twl6030_bci_battery_external_power_changed(struct power_supply *psy)
{//	printk("%s\n", __func__);

	struct twl6030_bci_device_info *di = to_twl6030_bci_device_info(psy);

	cancel_delayed_work(&di->twl6030_bci_monitor_work);
	queue_delayed_work(di->freezable_work, &di->twl6030_bci_monitor_work, 0);
}
#endif

#define to_twl6030_ac_device_info(x) container_of((x), \
		struct twl6030_bci_device_info, ac);

static int twl6030_ac_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	u8 stat1;
	struct twl6030_bci_device_info *di = to_twl6030_ac_device_info(psy);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &stat1,
					CONTROLLER_STAT1);
		
		if (stat1 & VAC_DET) {
			val->intval =  1;
		}
		else
			val->intval =  0;

		///printk("&&&&&&&&&&&&ac---charge  ac->intval = %d &&&&&&&&&&&&&&&\n" ,val->intval);

		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = twl6030_get_gpadc_conversion(di, 9) * 1000;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define to_twl6030_usb_device_info(x) container_of((x), \
		struct twl6030_bci_device_info, usb);

static int twl6030_usb_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	u8 stat1;
	struct twl6030_bci_device_info *di = to_twl6030_usb_device_info(psy);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:

		twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &stat1,
					CONTROLLER_STAT1);
		
		if (stat1 & VBUS_DET) {
			val->intval =  1;
		}
		else
			val->intval =  0;

		//val->intval = di->usb_online; //xsf
		
		//printk("&&&&&&&&&&&&usb---charge  val->intval = %d &&&&&&&&&&&&&&&\n" ,val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = twl6030_get_gpadc_conversion(di, 10) * 1000;
		//printk("&&&&&&&&&&&&usb-voltage = &&&&&&&&&&&&&&&%d\n", val->intval);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define to_twl6030_bk_bci_device_info(x) container_of((x), \
		struct twl6030_bci_device_info, bk_bat);

static int twl6030_bk_bci_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct twl6030_bci_device_info *di = to_twl6030_bk_bci_device_info(psy);
	val->intval = 0;	
	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = di->bk_voltage_mV * 1000;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int twl6030_bci_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct twl6030_bci_device_info *di;

	di = to_twl6030_bci_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = di->charge_status;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		di->voltage_mV = twl6030_get_gpadc_conversion(di,
						di->gpadc_vbat_chnl);
		val->intval = di->voltage_mV * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		twl6030battery_current(di);
		val->intval = di->current_uA;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = di->temp_C;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = di->charger_source;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = di->current_avg_uA;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = di->bat_health;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = di->capacity;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int twl6030_register_notifier(struct notifier_block *nb,
				unsigned int events)
{
	return blocking_notifier_chain_register(&notifier_list, nb);
}
EXPORT_SYMBOL_GPL(twl6030_register_notifier);

int twl6030_unregister_notifier(struct notifier_block *nb,
				unsigned int events)
{
	return blocking_notifier_chain_unregister(&notifier_list, nb);
}
EXPORT_SYMBOL_GPL(twl6030_unregister_notifier);

static void twl6030_usb_charger_work(struct work_struct *work)
{
	struct twl6030_bci_device_info	*di =
		container_of(work, struct twl6030_bci_device_info, usb_work);
	switch (di->event) {
	case USB_EVENT_CHARGER:
		/* POWER_SUPPLY_TYPE_USB_DCP */
		di->usb_online = POWER_SUPPLY_TYPE_USB_DCP;
		di->charger_incurrentmA = 1800;
		break;
	case USB_EVENT_VBUS:
		switch (di->usb_online) {
		case POWER_SUPPLY_TYPE_USB_CDP:
			/*
			 * Only 500mA here or high speed chirp
			 * handshaking may break
			 */
			di->charger_incurrentmA = 500;
		case POWER_SUPPLY_TYPE_USB:
			break;
		}
		break;
	case USB_EVENT_NONE:
		di->usb_online = 0;
		di->charger_incurrentmA = 0;
		break;
	case USB_EVENT_ENUMERATED:
		if (di->usb_online == POWER_SUPPLY_TYPE_USB_CDP)
			di->charger_incurrentmA = 560;
		else
			di->charger_incurrentmA = di->usb_max_power;
		break;
	default:
		return;
	}
	twl6030_start_usb_charger(di);

	power_supply_changed(&di->usb);
}

static int twl6030_usb_notifier_call(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct twl6030_bci_device_info *di =
		container_of(nb, struct twl6030_bci_device_info, nb);
	di->event = event;
	switch (event) {
	case USB_EVENT_VBUS:
		di->usb_online = *((unsigned int *) data);
		break;
	case USB_EVENT_ENUMERATED:
		di->usb_max_power = *((unsigned int *) data);
		break;
	case USB_EVENT_CHARGER:
	case USB_EVENT_NONE:
		break;
	case USB_EVENT_ID:
	default:
		return NOTIFY_OK;
	}

	schedule_work(&di->usb_work);

	return NOTIFY_OK;
}

static ssize_t set_fg_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	long val;
	int status = count;
	int ret;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val > 3))
		return -EINVAL;
	di->fuelgauge_mode = val;
	ret = twl_i2c_write_u8(TWL6030_MODULE_GASGAUGE, (val << 6) | CC_CAL_EN,
							FG_REG_00);
	if (ret)
		return -EIO;
	twl6030_current_mode_changed(di);
	return status;
}

static ssize_t show_fg_mode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->fuelgauge_mode;
	return sprintf(buf, "%d\n", val);
}

static ssize_t set_charge_src(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	long val;
	int status = count;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 2) || (val > 3))
		return -EINVAL;
	di->vac_priority = val;
	return status;
}

static ssize_t show_charge_src(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->vac_priority;
	return sprintf(buf, "%d\n", val);
}

static ssize_t show_vbus_voltage(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = twl6030_get_gpadc_conversion(di, 10);

	return sprintf(buf, "%d\n", val);
}

static ssize_t show_id_level(struct device *dev, struct device_attribute *attr,
							char *buf)
{
	int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = twl6030_get_gpadc_conversion(di, 14);

	return sprintf(buf, "%d\n", val);
}

static ssize_t set_watchdog(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	long val;
	int status = count;
	int ret;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 1) || (val > 127))
		return -EINVAL;
	ret = twl6030_set_watchdog(di, val);
	if (ret)
		return -EIO;

	return status;
}

static ssize_t show_watchdog(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->watchdog_duration;
	return sprintf(buf, "%d\n", val);
}

static ssize_t show_fg_counter(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int fg_counter = 0;
	int ret;

	ret = twl_i2c_read(TWL6030_MODULE_GASGAUGE, (u8 *) &fg_counter,
							FG_REG_01, 3);
	if (ret < 0)
		return -EIO;
	return sprintf(buf, "%d\n", fg_counter);
}

static ssize_t show_fg_accumulator(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	long fg_accum = 0;
	int ret;

	ret = twl_i2c_read(TWL6030_MODULE_GASGAUGE, (u8 *) &fg_accum,
							FG_REG_04, 4);
	if (ret > 0)
		return -EIO;

	return sprintf(buf, "%ld\n", fg_accum);
}

static ssize_t show_fg_offset(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	s16 fg_offset = 0;
	int ret;

	ret = twl_i2c_read(TWL6030_MODULE_GASGAUGE, (u8 *) &fg_offset,
							FG_REG_08, 2);
	if (ret < 0)
		return -EIO;
	fg_offset = ((s16)(fg_offset << 6) >> 6);

	return sprintf(buf, "%d\n", fg_offset);
}

static ssize_t set_fg_clear(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	long val;
	int status = count;
	int ret;

	if ((strict_strtol(buf, 10, &val) < 0) || (val != 1))
		return -EINVAL;
	ret = twl_i2c_write_u8(TWL6030_MODULE_GASGAUGE, CC_AUTOCLEAR,
							FG_REG_00);
	if (ret)
		return -EIO;

	return status;
}

static ssize_t set_fg_cal(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	long val;
	int status = count;
	int ret;

	if ((strict_strtol(buf, 10, &val) < 0) || (val != 1))
		return -EINVAL;
	ret = twl_i2c_write_u8(TWL6030_MODULE_GASGAUGE, CC_CAL_EN, FG_REG_00);
	if (ret)
		return -EIO;

	return status;
}

static ssize_t set_charging(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	int status = count;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if (strncmp(buf, "startac", 7) == 0) {
		if (di->charger_source == POWER_SUPPLY_TYPE_USB)
			twl6030_stop_usb_charger(di);
		twl6030_start_ac_charger(di);
	} else if (strncmp(buf, "startusb", 8) == 0) {
		if (di->charger_source == POWER_SUPPLY_TYPE_MAINS)
			twl6030_stop_ac_charger(di);
		di->charger_source = POWER_SUPPLY_TYPE_USB;
		di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
		twl6030_start_usb_charger(di);
	} else if (strncmp(buf, "stop" , 4) == 0)
		twl6030_stop_charger(di);
	else
		return -EINVAL;

	return status;
}

static ssize_t set_regulation_voltage(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	long val;
	int status = count;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 3500)
			|| (val > di->platform_data->max_charger_voltagemV))
		return -EINVAL;
	di->platform_data->max_bat_voltagemV = val;
	twl6030_config_voreg_reg(di, val);

	return status;
}

static ssize_t show_regulation_voltage(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	unsigned int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->platform_data->max_bat_voltagemV;
	return sprintf(buf, "%u\n", val);
}

static ssize_t set_termination_current(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	long val;
	int status = count;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 50) || (val > 400))
		return -EINVAL;
	di->platform_data->termination_currentmA = val;
	twl6030_config_iterm_reg(di, val);

	return status;
}

static ssize_t show_termination_current(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	unsigned int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->platform_data->termination_currentmA;
	return sprintf(buf, "%u\n", val);
}

static ssize_t set_cin_limit(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	long val;
	int status = count;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 50) || (val > 1500))
		return -EINVAL;
	di->charger_incurrentmA = val;
	twl6030_config_cinlimit_reg(di, val);

	return status;
}

static ssize_t show_cin_limit(struct device *dev, struct device_attribute *attr,
								  char *buf)
{
	unsigned int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->charger_incurrentmA;
	return sprintf(buf, "%u\n", val);
}

static ssize_t set_charge_current(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	long val;
	int status = count;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 300)
			|| (val > di->platform_data->max_charger_currentmA))
		return -EINVAL;
	di->charger_outcurrentmA = val;
	twl6030_config_vichrg_reg(di, val);

	return status;
}

static ssize_t show_charge_current(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->charger_outcurrentmA;
	return sprintf(buf, "%u\n", val);
}

static ssize_t set_min_vbus(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	long val;
	int status = count;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 4200) || (val > 4760))
		return -EINVAL;
	di->min_vbus = val;
	twl6030_config_min_vbus_reg(di, val);

	return status;
}

static ssize_t show_min_vbus(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	unsigned int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->min_vbus;
	return sprintf(buf, "%u\n", val);
}

static ssize_t set_current_avg_interval(struct device *dev,
	  struct device_attribute *attr, const char *buf, size_t count)
{
	long val;
	int status = count;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 10) || (val > 3600))
		return -EINVAL;
	di->current_avg_interval = val;
	twl6030_current_mode_changed(di);

	return status;
}

static ssize_t show_current_avg_interval(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	unsigned int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->current_avg_interval;
	return sprintf(buf, "%u\n", val);
}

static ssize_t set_wakelock_enable(struct device *dev,
	  struct device_attribute *attr, const char *buf, size_t count)
{
	long val;
	int status = count;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 0) || (val > 1))
		return -EINVAL;

	if ((val) && (di->charger_source == POWER_SUPPLY_TYPE_MAINS))
		wake_lock(&chrg_lock);
	else
		wake_unlock(&chrg_lock);

	di->wakelock_enabled = val;
	return status;
}

static ssize_t show_wakelock_enable(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	unsigned int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->wakelock_enabled;
	return sprintf(buf, "%u\n", val);
}

static ssize_t set_monitoring_interval(struct device *dev,
	  struct device_attribute *attr, const char *buf, size_t count)
{
	long val;
	int status = count;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 10) || (val > 3600))
		return -EINVAL;
	di->monitoring_interval = val;
	twl6030_work_interval_changed(di);

	return status;
}

static ssize_t show_monitoring_interval(struct device *dev,
		  struct device_attribute *attr, char *buf)
{
	unsigned int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->monitoring_interval;
	return sprintf(buf, "%u\n", val);
}

static ssize_t show_bsi(struct device *dev,
		  struct device_attribute *attr, char *buf)
{
	int val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = twl6030_get_gpadc_conversion(di, 0);
	return sprintf(buf, "%d\n", val);
}

static ssize_t show_stat1(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->stat1;
	return sprintf(buf, "%u\n", val);
}

static ssize_t show_status_int1(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->status_int1;
	return sprintf(buf, "%u\n", val);
}

static ssize_t show_status_int2(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned val;
	struct twl6030_bci_device_info *di = dev_get_drvdata(dev);

	val = di->status_int2;
	return sprintf(buf, "%u\n", val);
}

static DEVICE_ATTR(fg_mode, S_IWUSR | S_IRUGO, show_fg_mode, set_fg_mode);
static DEVICE_ATTR(charge_src, S_IWUSR | S_IRUGO, show_charge_src,
		set_charge_src);
static DEVICE_ATTR(vbus_voltage, S_IRUGO, show_vbus_voltage, NULL);
static DEVICE_ATTR(id_level, S_IRUGO, show_id_level, NULL);
static DEVICE_ATTR(watchdog, S_IWUSR | S_IRUGO, show_watchdog, set_watchdog);
static DEVICE_ATTR(fg_counter, S_IRUGO, show_fg_counter, NULL);
static DEVICE_ATTR(fg_accumulator, S_IRUGO, show_fg_accumulator, NULL);
static DEVICE_ATTR(fg_offset, S_IRUGO, show_fg_offset, NULL);
static DEVICE_ATTR(fg_clear, S_IWUSR, NULL, set_fg_clear);
static DEVICE_ATTR(fg_cal, S_IWUSR, NULL, set_fg_cal);
static DEVICE_ATTR(charging, S_IWUSR | S_IRUGO, NULL, set_charging);
static DEVICE_ATTR(regulation_voltage, S_IWUSR | S_IRUGO,
		show_regulation_voltage, set_regulation_voltage);
static DEVICE_ATTR(termination_current, S_IWUSR | S_IRUGO,
		show_termination_current, set_termination_current);
static DEVICE_ATTR(cin_limit, S_IWUSR | S_IRUGO, show_cin_limit,
		set_cin_limit);
static DEVICE_ATTR(charge_current, S_IWUSR | S_IRUGO, show_charge_current,
		set_charge_current);
static DEVICE_ATTR(min_vbus, S_IWUSR | S_IRUGO, show_min_vbus, set_min_vbus);
static DEVICE_ATTR(monitoring_interval, S_IWUSR | S_IRUGO,
		show_monitoring_interval, set_monitoring_interval);
static DEVICE_ATTR(current_avg_interval, S_IWUSR | S_IRUGO,
		show_current_avg_interval, set_current_avg_interval);
static DEVICE_ATTR(wakelock_enable, S_IWUSR | S_IRUGO,
		show_wakelock_enable, set_wakelock_enable);
static DEVICE_ATTR(bsi, S_IRUGO, show_bsi, NULL);
static DEVICE_ATTR(stat1, S_IRUGO, show_stat1, NULL);
static DEVICE_ATTR(status_int1, S_IRUGO, show_status_int1, NULL);
static DEVICE_ATTR(status_int2, S_IRUGO, show_status_int2, NULL);

static struct attribute *twl6030_bci_attributes[] = {
	&dev_attr_fg_mode.attr,
	&dev_attr_charge_src.attr,
	&dev_attr_vbus_voltage.attr,
	&dev_attr_id_level.attr,
	&dev_attr_watchdog.attr,
	&dev_attr_fg_counter.attr,
	&dev_attr_fg_accumulator.attr,
	&dev_attr_fg_offset.attr,
	&dev_attr_fg_clear.attr,
	&dev_attr_fg_cal.attr,
	&dev_attr_charging.attr,
	&dev_attr_regulation_voltage.attr,
	&dev_attr_termination_current.attr,
	&dev_attr_cin_limit.attr,
	&dev_attr_charge_current.attr,
	&dev_attr_min_vbus.attr,
	&dev_attr_monitoring_interval.attr,
	&dev_attr_current_avg_interval.attr,
	&dev_attr_bsi.attr,
	&dev_attr_stat1.attr,
	&dev_attr_status_int1.attr,
	&dev_attr_status_int2.attr,
	&dev_attr_wakelock_enable.attr,
	NULL,
};

static const struct attribute_group twl6030_bci_attr_group = {
	.attrs = twl6030_bci_attributes,
};

static char *twl6030_bci_supplied_to[] = {
	"twl6030_battery",
};

extern int dwc_vbus_status(void);

static void twl6030_battery_update_status(struct twl6030_bci_device_info *di)
{
	power_supply_changed(&di->bat);
	power_supply_changed(&di->usb);
	power_supply_changed(&di->ac);

}
static void twl6030_battery_work(struct work_struct *work)
{	

	struct twl6030_bci_device_info *di = container_of(work, struct twl6030_bci_device_info, work.work); 
	twl6030_battery_update_status(di);
	//set charging current
		twl_i2c_write_u8(TWL6030_MODULE_CHARGER,0x00,CHARGERUSB_CTRL1);
		if(2 == dwc_vbus_status()){
			twl_i2c_write_u8(TWL6030_MODULE_CHARGER, 0x2e,CHARGERUSB_CINLIMIT);	//set vbus input current is 1.5A
			twl_i2c_write_u8(TWL6030_MODULE_CHARGER, 0x0b,CHARGERUSB_VICHRG);	//set mos output current is 1A
		}
		else if(1 == dwc_vbus_status()){
			twl_i2c_write_u8(TWL6030_MODULE_CHARGER, 0x2e,CHARGERUSB_CINLIMIT);//set vbus input current is 500ma
			twl_i2c_write_u8(TWL6030_MODULE_CHARGER, 0x09,CHARGERUSB_VICHRG);	//set mos output current is 500ma
		}
	/* reschedule for the next time */
	queue_delayed_work(di->freezable_work, &di->work, di->interval);
}

static int __devinit twl6030_bci_battery_probe(struct platform_device *pdev)
{
	struct twl4030_bci_platform_data *pdata = pdev->dev.platform_data;
	struct twl6030_bci_device_info *di;
	int irq;
	int ret;
	u8 controller_stat = 0;
	u8 chargerusb_ctrl1 = 0;
	u8 hw_state = 0;
	u8 reg = 0;
	//printk("%s\n", __func__);
	if (!pdata) {
		dev_info(&pdev->dev, "platform_data not available\n");
		return -EINVAL;
	}

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->platform_data = kmemdup(pdata, sizeof(*pdata), GFP_KERNEL);
	if (!di->platform_data) {
		kfree(di);
		return -ENOMEM;
	}

	if (pdata->monitoring_interval == 0) {
		di->monitoring_interval = 10;
		di->current_avg_interval = 10;
	} else {
		di->monitoring_interval = pdata->monitoring_interval;
		di->current_avg_interval = pdata->monitoring_interval;
	}

	di->platform_data = pdata;
	di->features = pdata->features;
	di->dev = &pdev->dev;


//	printk("di->feature = %d, TWL6032_SUBCLASS = %d\n",di->features,TWL6032_SUBCLASS);
	if (di->features & TWL6032_SUBCLASS) {
		ret = twl_i2c_read_u8(TWL_MODULE_RTC, &reg, CHARGER_MODE_REG);
		if (ret)
			goto temp_setup_fail;

		if (reg & CHARGER_MODE_POWERPATH) {
			dev_dbg(di->dev, "Charger: PowerPath\n");
			di->use_power_path = 1;
		} else {
			dev_dbg(di->dev, "Charger: NON PowerPath\n");
			di->use_power_path = 0;
		}

		if (reg & CHARGER_MODE_AUTOCHARGE) {
			dev_dbg(di->dev, "Charger: AutoCharge\n");
			di->use_hw_charger = 1;
		} else {
			dev_dbg(di->dev, "Charger: NON AutoCharge\n");
			di->use_hw_charger = 0;
		}
	} else {
		di->use_power_path = 0;
		di->use_hw_charger = 0;
	}
//	printk("di->feature = %d, TWL6032_SUBCLASS = %d,di->use_hw_charger =%d \n",di->features,TWL6032_SUBCLASS,di->use_hw_charger );

	
	if (di->use_hw_charger) {
		di->platform_data->max_charger_currentmA =
				twl6030_get_limit2_reg(di);
		di->platform_data->max_charger_voltagemV =
				twl6030_get_limit1_reg(di);
		di->platform_data->termination_currentmA =
				twl6030_get_iterm_reg(di);
		di->platform_data->max_bat_voltagemV =
				twl6030_get_voreg_reg(di);
	}
//	printk("max_charger_currentmA = %d, max_charger_voltagemV = %d\n,termination_currentmA =%d, max_bat_voltagemV =%d \n",di->platform_data->max_charger_currentmA ,
//		di->platform_data->max_charger_voltagemV,di->platform_data->termination_currentmA, di->platform_data->max_bat_voltagemV);
	di->bat.name = "twl6030_battery";
	di->bat.supplied_to = twl6030_bci_supplied_to;
	di->bat.num_supplicants = ARRAY_SIZE(twl6030_bci_supplied_to);
	di->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties = twl6030_bci_battery_props;
	di->bat.num_properties = ARRAY_SIZE(twl6030_bci_battery_props);
	di->bat.get_property = twl6030_bci_battery_get_property;
	//di->bat.external_power_changed =
	//		twl6030_bci_battery_external_power_changed;
	di->bat_health = POWER_SUPPLY_HEALTH_GOOD;

	di->usb.name = "twl6030_usb";
	di->usb.type = POWER_SUPPLY_TYPE_USB;
	di->usb.properties = twl6030_usb_props;
	di->usb.num_properties = ARRAY_SIZE(twl6030_usb_props);
	di->usb.get_property = twl6030_usb_get_property;

	di->ac.name = "twl6030_ac";
	di->ac.type = POWER_SUPPLY_TYPE_MAINS;
	di->ac.properties = twl6030_ac_props;
	di->ac.num_properties = ARRAY_SIZE(twl6030_ac_props);
	di->ac.get_property = twl6030_ac_get_property;

	di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;

	di->bk_bat.name = "twl6030_bk_battery";
	di->bk_bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bk_bat.properties = twl6030_bk_bci_battery_props;
	di->bk_bat.num_properties = ARRAY_SIZE(twl6030_bk_bci_battery_props);
	di->bk_bat.get_property = twl6030_bk_bci_battery_get_property;

	di->vac_priority = 2;
	di->capacity = -1;
	di->capacity_debounce_count = 0;
	di->ac_next_refresh = jiffies - 1;
	platform_set_drvdata(pdev, di);

	/* calculate current max scale from sense */
	if (pdata->sense_resistor_mohm) {
		di->current_max_scale = (62000) / pdata->sense_resistor_mohm;
	} else {
		/* Set sensible defaults if platform data is missing */
		if (di->features & TWL6032_SUBCLASS)
			di->current_max_scale = 3100;
		else
			di->current_max_scale = 6200;
	}

	wake_lock_init(&chrg_lock, WAKE_LOCK_SUSPEND, "ac_chrg_wake_lock");
	/* settings for temperature sensing */
	ret = twl6030battery_temp_setup(true);
	if (ret)
		goto temp_setup_fail;

	/* request charger fault interruption choosing between sw/hw mode */
	irq = platform_get_irq(pdev, 1);
	if (!di->use_hw_charger)
		ret = request_threaded_irq(irq, NULL,
				twl6030charger_fault_interrupt,
				0, "twl_bci_fault", di);
	else
		ret = request_threaded_irq(irq, NULL,
				twl6032charger_fault_interrupt_hw,
				0, "twl_bci_fault", di);

	if (ret) {
		dev_info(&pdev->dev, "could not request irq %d, status %d\n",
			irq, ret);
		goto temp_setup_fail;
	}

	/* request charger ctrl interruption choosing between sw/hw mode */
	irq = platform_get_irq(pdev, 0);
	if (!di->use_hw_charger)
		ret = request_threaded_irq(irq, NULL,
				twl6030charger_ctrl_interrupt,
				0, "twl_bci_ctrl", di);
	else
		ret = request_threaded_irq(irq, NULL,
				twl6032charger_ctrl_interrupt_hw,  
				0, "twl_bci_ctrl", di);  // 一直在检测电池的状态

	if (ret) {
		dev_info(&pdev->dev, "could not request irq %d, status %d\n",
			irq, ret);
		goto chg_irq_fail;
	}

	ret = power_supply_register(&pdev->dev, &di->bat);
	if (ret) {
		dev_info(&pdev->dev, "failed to register main battery\n");
		goto batt_failed;
	}

	ret = power_supply_register(&pdev->dev, &di->usb);
	if (ret) {
		dev_info(&pdev->dev, "failed to register usb power supply\n");
		goto usb_failed;
	}

	ret = power_supply_register(&pdev->dev, &di->ac);
	if (ret) {
		dev_info(&pdev->dev, "failed to register ac power supply\n");
		goto ac_failed;
	}

	ret = power_supply_register(&pdev->dev, &di->bk_bat);
	if (ret) {
		dev_info(&pdev->dev, "failed to register backup battery\n");
		goto bk_batt_failed;
	}
	di->charge_n1 = 0;
	di->timer_n1 = 0;

	//di->freezable_work = create_freezable_workqueue("battery");
#if 1
	if (di->features & TWL6032_SUBCLASS) {
		di->charger_incurrentmA = 1000;
		di->gpadc_vbat_chnl = TWL6032_GPADC_VBAT_CHNL;
	} else {
		di->charger_incurrentmA = twl6030_get_usb_max_power(di->otg);
		di->gpadc_vbat_chnl = TWL6030_GPADC_VBAT_CHNL;
	}
#endif
#if 1

	ret = twl6030battery_voltage_setup(di);
	if (ret)
		dev_info(&pdev->dev, "voltage measurement setup failed\n");

	ret = twl6030battery_current_setup(true);
	if (ret)
		dev_info(&pdev->dev, "current measurement setup failed\n");


	di->voltage_mV = twl6030_get_gpadc_conversion(di, di->gpadc_vbat_chnl);
	di->voltage_mV = twl6030_get_gpadc_conversion(di, di->gpadc_vbat_chnl);
	di->capacity = twl6030_batt_vol_to_capacity( di);
	twl6030battery_current(di);
#endif
	di->freezable_work = system_freezable_wq;
	INIT_DELAYED_WORK(&di->twl6030_bci_monitor_work,
				twl6030_bci_battery_work);
	queue_delayed_work(di->freezable_work, &di->twl6030_bci_monitor_work, 0);
#if 1
	INIT_DELAYED_WORK_DEFERRABLE(&di->twl6030_current_avg_work,
						twl6030_current_avg);
	queue_delayed_work(di->freezable_work, &di->twl6030_current_avg_work, 500);
#endif
#if 0
	ret = twl6030battery_voltage_setup(di);
	if (ret)
		dev_info(&pdev->dev, "voltage measurement setup failed\n");

	ret = twl6030battery_current_setup(true);
	if (ret)
		dev_info(&pdev->dev, "current measurement setup failed\n");
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/* initialize for USB charging */
	if (!di->use_hw_charger) {
		twl6030_config_limit1_reg(di, pdata->max_charger_voltagemV);
		twl6030_config_limit2_reg(di,
				di->platform_data->max_charger_currentmA);
	}
	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, MBAT_TEMP,
						CONTROLLER_INT_MASK);
	if (ret)
		goto bk_batt_failed;

	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, MASK_MCHARGERUSB_THMREG,
						CHARGERUSB_INT_MASK);
	if (ret)
		goto bk_batt_failed;

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &controller_stat,
		CONTROLLER_STAT1);
	if (ret)
		goto bk_batt_failed;

	di->stat1 = controller_stat;
	di->charger_outcurrentmA = di->platform_data->max_charger_currentmA;

	twl6030_set_watchdog(di, 32);

	INIT_WORK(&di->usb_work, twl6030_usb_charger_work);
	di->nb.notifier_call = twl6030_usb_notifier_call;
	di->otg = otg_get_transceiver();
	if (di->otg) {
		ret = otg_register_notifier(di->otg, &di->nb);
		if (ret)
			dev_err(&pdev->dev, "otg register notifier"
						" failed %d\n", ret);
	} else
		dev_err(&pdev->dev, "otg_get_transceiver failed %d\n", ret);


	if (di->features & TWL6032_SUBCLASS) {
		di->charger_incurrentmA = 1000;
		di->gpadc_vbat_chnl = TWL6032_GPADC_VBAT_CHNL;
	} else {
		di->charger_incurrentmA = twl6030_get_usb_max_power(di->otg);
		di->gpadc_vbat_chnl = TWL6030_GPADC_VBAT_CHNL;
	}

	di->voltage_mV = twl6030_get_gpadc_conversion(di, di->gpadc_vbat_chnl);
	dev_info(&pdev->dev, "Battery Voltage at Bootup is %d mV\n",
							di->voltage_mV);

	ret = twl_i2c_read_u8(TWL6030_MODULE_ID0, &hw_state, STS_HW_CONDITIONS);
	if (ret)
		goto  bk_batt_failed;
	if (!is_battery_present(di)) {
		if (!(hw_state & STS_USB_ID)) {
			dev_dbg(di->dev, "Put USB in HZ mode\n");
			ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER,
					&chargerusb_ctrl1, CHARGERUSB_CTRL1);
			if (ret)
				goto  bk_batt_failed;

			chargerusb_ctrl1 |= HZ_MODE;  // high-impedance
			ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER,
					 chargerusb_ctrl1, CHARGERUSB_CTRL1);
			if (ret)
				goto  bk_batt_failed;
		}
	} else if (!di->use_hw_charger) {
		if (controller_stat & VAC_DET) {
			di->ac_online = POWER_SUPPLY_TYPE_MAINS;
			twl6030_start_ac_charger(di);
		} else if (controller_stat & VBUS_DET) {
			/*
			 * In HOST mode (ID GROUND) with a device connected,
			 * do no enable usb charging
			 */
			if (!(hw_state & STS_USB_ID)) {
				di->usb_online = POWER_SUPPLY_TYPE_USB;
				di->charger_source = POWER_SUPPLY_TYPE_USB;
				di->charge_status =
						POWER_SUPPLY_STATUS_CHARGING;
				di->event = USB_EVENT_VBUS;
				schedule_work(&di->usb_work);
			}
		}
	} else {
		int fault, charge_usb, charge_ac;

		twl_i2c_read_u8(TWL6032_MODULE_CHARGER, &reg,
				CHARGERUSB_INT_STATUS);

		fault = !(di->stat1 & CONTROLLER_STAT1_LINCH_GATED) &&
				!(di->stat1 & CONTROLLER_STAT1_FAULT_WDG);
		charge_usb = (di->stat1 & VBUS_DET) &&
				!(reg & CHARGERUSB_FAULT);
		charge_ac = (di->stat1 & VAC_DET) &&
				!(di->stat1 & CONTROLLER_STAT1_EXTCHRG_STATZ);

		dev_dbg(di->dev, "boot charge state fault %d, usb %d, ac %d\n",
				fault, charge_usb, charge_ac);

		if (fault && (charge_usb || charge_ac))
			di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
		else {
			if (di->stat1 & (VBUS_DET | VAC_DET))
				di->charge_status =
					POWER_SUPPLY_STATUS_NOT_CHARGING;
			else
				di->charge_status =
					POWER_SUPPLY_STATUS_DISCHARGING;
		}
	}
#if 1	
	di->interval = msecs_to_jiffies(1 * 1000);
	INIT_DELAYED_WORK(&di->work, twl6030_battery_work);
	queue_delayed_work(di->freezable_work, &di->work, 1*HZ);
#endif
	ret = twl6030backupbatt_setup();
	if (ret)
		dev_info(&pdev->dev, "Backup Bat charging setup failed\n");

	twl6030_interrupt_unmask(TWL6030_CHARGER_CTRL_INT_MASK,
						REG_INT_MSK_LINE_C);
	twl6030_interrupt_unmask(TWL6030_CHARGER_CTRL_INT_MASK,
						REG_INT_MSK_STS_C);
	twl6030_interrupt_unmask(TWL6030_CHARGER_FAULT_INT_MASK,
						REG_INT_MSK_LINE_C);
	twl6030_interrupt_unmask(TWL6030_CHARGER_FAULT_INT_MASK,
						REG_INT_MSK_STS_C);

	ret = sysfs_create_group(&pdev->dev.kobj, &twl6030_bci_attr_group);
	if (ret)
		dev_info(&pdev->dev, "could not create sysfs files\n");

	return 0;

bk_batt_failed:
	cancel_delayed_work(&di->twl6030_bci_monitor_work);
	power_supply_unregister(&di->ac);
ac_failed:
	power_supply_unregister(&di->usb);
usb_failed:
	power_supply_unregister(&di->bat);
batt_failed:
	free_irq(irq, di);
chg_irq_fail:
	irq = platform_get_irq(pdev, 1);
	free_irq(irq, di);
temp_setup_fail:
	wake_lock_destroy(&chrg_lock);
	platform_set_drvdata(pdev, NULL);
	kfree(di);

	return ret;
}

static int __devexit twl6030_bci_battery_remove(struct platform_device *pdev)
{
	struct twl6030_bci_device_info *di = platform_get_drvdata(pdev);
	int irq;

	twl6030_interrupt_mask(TWL6030_CHARGER_CTRL_INT_MASK,
						REG_INT_MSK_LINE_C);
	twl6030_interrupt_mask(TWL6030_CHARGER_CTRL_INT_MASK,
						REG_INT_MSK_STS_C);
	twl6030_interrupt_mask(TWL6030_CHARGER_FAULT_INT_MASK,
						REG_INT_MSK_LINE_C);
	twl6030_interrupt_mask(TWL6030_CHARGER_FAULT_INT_MASK,
						REG_INT_MSK_STS_C);

	irq = platform_get_irq(pdev, 0);
	free_irq(irq, di);

	irq = platform_get_irq(pdev, 1);
	free_irq(irq, di);

	otg_unregister_notifier(di->otg, &di->nb);
	sysfs_remove_group(&pdev->dev.kobj, &twl6030_bci_attr_group);
	cancel_delayed_work(&di->twl6030_bci_monitor_work);
	cancel_delayed_work(&di->twl6030_current_avg_work);
	flush_scheduled_work();
	power_supply_unregister(&di->bat);
	power_supply_unregister(&di->usb);
	power_supply_unregister(&di->ac);
	power_supply_unregister(&di->bk_bat);
	wake_lock_destroy(&chrg_lock);
	platform_set_drvdata(pdev, NULL);
	kfree(di->platform_data);
	kfree(di);

	return 0;
}

#ifdef CONFIG_PM
static int twl6030_bci_battery_suspend(struct device *dev)
{
//	struct platform_device *pdev = to_platform_device(dev);
//	struct twl6030_bci_device_info *di = platform_get_drvdata(pdev);
	long int events;
	u8 rd_reg = 0;
	int ret;

	/* mask to prevent wakeup due to 32s timeout from External charger */
	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &rd_reg,
						CONTROLLER_INT_MASK);
	if (ret)
		goto err;

	rd_reg |= MVAC_FAULT;
	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, MBAT_TEMP,
						CONTROLLER_INT_MASK);
	if (ret)
		goto err;

        //cancel_delayed_work(&di->work);
	//cancel_delayed_work(&di->twl6030_bci_monitor_work);
	//cancel_delayed_work(&di->twl6030_current_avg_work);

	/* We cannot tolarate a sleep longer than 30 seconds
	 * while on ac charging we have to reset the BQ watchdog timer.
	 */
//	if ((di->charger_source == POWER_SUPPLY_TYPE_MAINS) &&
//		((wakeup_timer_seconds > 25) || !wakeup_timer_seconds)) {
//		wakeup_timer_seconds = 25;
//	}

	/*reset the BQ watch dog*/
	events = BQ2415x_RESET_TIMER;
	blocking_notifier_call_chain(&notifier_list, events, NULL);

	ret = twl6030battery_temp_setup(false);
	if (ret) {
		pr_err("%s: Temp measurement setup failed (%d)!\n",
				__func__, ret);
		return ret;
	}

	ret = twl6030battery_current_setup(false);
	if (ret) {
		pr_err("%s: Current measurement setup failed (%d)!\n",
				__func__, ret);
		return ret;
	}

	return 0;
err:
	pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
	return ret;
}

static int twl6030_bci_battery_resume(struct device *dev)
{
//	struct platform_device *pdev = to_platform_device(dev);
//	struct twl6030_bci_device_info *di = platform_get_drvdata(pdev);
	long int events;
	u8 rd_reg = 0;
	int ret;

	ret = twl6030battery_temp_setup(true);
	if (ret) {
		pr_err("%s: Temp measurement setup failed (%d)!\n",
				__func__, ret);
		return ret;
	}

	ret = twl6030battery_current_setup(true);
	if (ret) {
		pr_err("%s: Current measurement setup failed (%d)!\n",
				__func__, ret);
		return ret;
	}

	ret = twl_i2c_read_u8(TWL6030_MODULE_CHARGER, &rd_reg, CONTROLLER_INT_MASK);
	if (ret)
		goto err;

	rd_reg &= ~(0xFF & MVAC_FAULT);
	ret = twl_i2c_write_u8(TWL6030_MODULE_CHARGER, MBAT_TEMP,
						CONTROLLER_INT_MASK);
	if (ret)
		goto err;

	//queue_delayed_work(di->freezable_work, &di->twl6030_bci_monitor_work, 0);
	//queue_delayed_work(di->freezable_work, &di->twl6030_current_avg_work, 50);
	//queue_delayed_work(di->freezable_work, &di->work, di->interval);

	events = BQ2415x_RESET_TIMER;
	blocking_notifier_call_chain(&notifier_list, events, NULL);

	return 0;
err:
	pr_err("%s: Error access to TWL6030 (%d)\n", __func__, ret);
	return ret;
}
#else
#define twl6030_bci_battery_suspend	NULL
#define twl6030_bci_battery_resume	NULL
#endif /* CONFIG_PM */

static const struct dev_pm_ops pm_ops = {
	.suspend	= twl6030_bci_battery_suspend,
	.resume		= twl6030_bci_battery_resume,
};

static struct platform_driver twl6030_bci_battery_driver = {
	.probe		= twl6030_bci_battery_probe,
	.remove		= __devexit_p(twl6030_bci_battery_remove),
	.driver		= {
		.name	= "twl6030_bci",
		.pm	= &pm_ops,
	},
};

static int __init twl6030_battery_init(void)
{
	return platform_driver_register(&twl6030_bci_battery_driver);
}
module_init(twl6030_battery_init);

static void __exit twl6030_battery_exit(void)
{
	platform_driver_unregister(&twl6030_bci_battery_driver);
}
module_exit(twl6030_battery_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:twl6030_bci");
MODULE_AUTHOR("Texas Instruments Inc");
