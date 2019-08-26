// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Charger driver for AB8500
 *
 * Author:
 *	Johan Palsson <johan.palsson@stericsson.com>
 *	Karl Komierowski <karl.komierowski@stericsson.com>
 *	Arun R Murthy <arun.murthy@stericsson.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/of.h>
#include <linux/mfd/core.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/mfd/abx500/ux500_chargalg.h>
#include <linux/usb/otg.h>
#include <linux/mutex.h>

/* Charger constants */
#define NO_PW_CONN			0
#define AC_PW_CONN			1
#define USB_PW_CONN			2

#define MAIN_WDOG_ENA			0x01
#define MAIN_WDOG_KICK			0x02
#define MAIN_WDOG_DIS			0x00
#define CHARG_WD_KICK			0x01
#define MAIN_CH_ENA			0x01
#define MAIN_CH_NO_OVERSHOOT_ENA_N	0x02
#define USB_CH_ENA			0x01
#define USB_CHG_NO_OVERSHOOT_ENA_N	0x02
#define MAIN_CH_DET			0x01
#define MAIN_CH_CV_ON			0x04
#define USB_CH_CV_ON			0x08
#define VBUS_DET_DBNC100		0x02
#define VBUS_DET_DBNC1			0x01
#define OTP_ENABLE_WD			0x01
#define DROP_COUNT_RESET		0x01
#define USB_CH_DET			0x01

#define MAIN_CH_INPUT_CURR_SHIFT	4
#define VBUS_IN_CURR_LIM_SHIFT		4
#define AUTO_VBUS_IN_CURR_LIM_SHIFT	4
#define VBUS_IN_CURR_LIM_RETRY_SET_TIME	30 /* seconds */

#define LED_INDICATOR_PWM_ENA		0x01
#define LED_INDICATOR_PWM_DIS		0x00
#define LED_IND_CUR_5MA			0x04
#define LED_INDICATOR_PWM_DUTY_252_256	0xBF

/* HW failure constants */
#define MAIN_CH_TH_PROT			0x02
#define VBUS_CH_NOK			0x08
#define USB_CH_TH_PROT			0x02
#define VBUS_OVV_TH			0x01
#define MAIN_CH_NOK			0x01
#define VBUS_DET			0x80

#define MAIN_CH_STATUS2_MAINCHGDROP		0x80
#define MAIN_CH_STATUS2_MAINCHARGERDETDBNC	0x40
#define USB_CH_VBUSDROP				0x40
#define USB_CH_VBUSDETDBNC			0x01

/* UsbLineStatus register bit masks */
#define AB8500_USB_LINK_STATUS		0x78
#define AB8505_USB_LINK_STATUS		0xF8
#define AB8500_STD_HOST_SUSP		0x18
#define USB_LINK_STATUS_SHIFT		3

/* Watchdog timeout constant */
#define WD_TIMER			0x30 /* 4min */
#define WD_KICK_INTERVAL		(60 * HZ)

/* Lowest charger voltage is 3.39V -> 0x4E */
#define LOW_VOLT_REG			0x4E

/* Step up/down delay in us */
#define STEP_UDELAY			1000

#define CHARGER_STATUS_POLL 10 /* in ms */

#define CHG_WD_INTERVAL			(60 * HZ)

#define AB8500_SW_CONTROL_FALLBACK	0x03
/* Wait for enumeration before charing in us */
#define WAIT_ACA_RID_ENUMERATION	(5 * 1000)
/*External charger control*/
#define AB8500_SYS_CHARGER_CONTROL_REG		0x52
#define EXTERNAL_CHARGER_DISABLE_REG_VAL	0x03
#define EXTERNAL_CHARGER_ENABLE_REG_VAL		0x07

/* UsbLineStatus register - usb types */
enum ab8500_charger_link_status {
	USB_STAT_NOT_CONFIGURED,
	USB_STAT_STD_HOST_NC,
	USB_STAT_STD_HOST_C_NS,
	USB_STAT_STD_HOST_C_S,
	USB_STAT_HOST_CHG_NM,
	USB_STAT_HOST_CHG_HS,
	USB_STAT_HOST_CHG_HS_CHIRP,
	USB_STAT_DEDICATED_CHG,
	USB_STAT_ACA_RID_A,
	USB_STAT_ACA_RID_B,
	USB_STAT_ACA_RID_C_NM,
	USB_STAT_ACA_RID_C_HS,
	USB_STAT_ACA_RID_C_HS_CHIRP,
	USB_STAT_HM_IDGND,
	USB_STAT_RESERVED,
	USB_STAT_NOT_VALID_LINK,
	USB_STAT_PHY_EN,
	USB_STAT_SUP_NO_IDGND_VBUS,
	USB_STAT_SUP_IDGND_VBUS,
	USB_STAT_CHARGER_LINE_1,
	USB_STAT_CARKIT_1,
	USB_STAT_CARKIT_2,
	USB_STAT_ACA_DOCK_CHARGER,
};

enum ab8500_usb_state {
	AB8500_BM_USB_STATE_RESET_HS,	/* HighSpeed Reset */
	AB8500_BM_USB_STATE_RESET_FS,	/* FullSpeed/LowSpeed Reset */
	AB8500_BM_USB_STATE_CONFIGURED,
	AB8500_BM_USB_STATE_SUSPEND,
	AB8500_BM_USB_STATE_RESUME,
	AB8500_BM_USB_STATE_MAX,
};

/* VBUS input current limits supported in AB8500 in mA */
#define USB_CH_IP_CUR_LVL_0P05		50
#define USB_CH_IP_CUR_LVL_0P09		98
#define USB_CH_IP_CUR_LVL_0P19		193
#define USB_CH_IP_CUR_LVL_0P29		290
#define USB_CH_IP_CUR_LVL_0P38		380
#define USB_CH_IP_CUR_LVL_0P45		450
#define USB_CH_IP_CUR_LVL_0P5		500
#define USB_CH_IP_CUR_LVL_0P6		600
#define USB_CH_IP_CUR_LVL_0P7		700
#define USB_CH_IP_CUR_LVL_0P8		800
#define USB_CH_IP_CUR_LVL_0P9		900
#define USB_CH_IP_CUR_LVL_1P0		1000
#define USB_CH_IP_CUR_LVL_1P1		1100
#define USB_CH_IP_CUR_LVL_1P3		1300
#define USB_CH_IP_CUR_LVL_1P4		1400
#define USB_CH_IP_CUR_LVL_1P5		1500

#define VBAT_TRESH_IP_CUR_RED		3800

#define to_ab8500_charger_usb_device_info(x) container_of((x), \
	struct ab8500_charger, usb_chg)
#define to_ab8500_charger_ac_device_info(x) container_of((x), \
	struct ab8500_charger, ac_chg)

/**
 * struct ab8500_charger_interrupts - ab8500 interupts
 * @name:	name of the interrupt
 * @isr		function pointer to the isr
 */
struct ab8500_charger_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

struct ab8500_charger_info {
	int charger_connected;
	int charger_online;
	int charger_voltage;
	int cv_active;
	bool wd_expired;
	int charger_current;
};

struct ab8500_charger_event_flags {
	bool mainextchnotok;
	bool main_thermal_prot;
	bool usb_thermal_prot;
	bool vbus_ovv;
	bool usbchargernotok;
	bool chgwdexp;
	bool vbus_collapse;
	bool vbus_drop_end;
};

struct ab8500_charger_usb_state {
	int usb_current;
	int usb_current_tmp;
	enum ab8500_usb_state state;
	enum ab8500_usb_state state_tmp;
	spinlock_t usb_lock;
};

struct ab8500_charger_max_usb_in_curr {
	int usb_type_max;
	int set_max;
	int calculated_max;
};

/**
 * struct ab8500_charger - ab8500 Charger device information
 * @dev:		Pointer to the structure device
 * @vbus_detected:	VBUS detected
 * @vbus_detected_start:
 *			VBUS detected during startup
 * @ac_conn:		This will be true when the AC charger has been plugged
 * @vddadc_en_ac:	Indicate if VDD ADC supply is enabled because AC
 *			charger is enabled
 * @vddadc_en_usb:	Indicate if VDD ADC supply is enabled because USB
 *			charger is enabled
 * @vbat		Battery voltage
 * @old_vbat		Previously measured battery voltage
 * @usb_device_is_unrecognised	USB device is unrecognised by the hardware
 * @autopower		Indicate if we should have automatic pwron after pwrloss
 * @autopower_cfg	platform specific power config support for "pwron after pwrloss"
 * @invalid_charger_detect_state State when forcing AB to use invalid charger
 * @is_aca_rid:		Incicate if accessory is ACA type
 * @current_stepping_sessions:
 *			Counter for current stepping sessions
 * @parent:		Pointer to the struct ab8500
 * @gpadc:		Pointer to the struct gpadc
 * @bm:           	Platform specific battery management information
 * @flags:		Structure for information about events triggered
 * @usb_state:		Structure for usb stack information
 * @max_usb_in_curr:	Max USB charger input current
 * @ac_chg:		AC charger power supply
 * @usb_chg:		USB charger power supply
 * @ac:			Structure that holds the AC charger properties
 * @usb:		Structure that holds the USB charger properties
 * @regu:		Pointer to the struct regulator
 * @charger_wq:		Work queue for the IRQs and checking HW state
 * @usb_ipt_crnt_lock:	Lock to protect VBUS input current setting from mutuals
 * @pm_lock:		Lock to prevent system to suspend
 * @check_vbat_work	Work for checking vbat threshold to adjust vbus current
 * @check_hw_failure_work:	Work for checking HW state
 * @check_usbchgnotok_work:	Work for checking USB charger not ok status
 * @kick_wd_work:		Work for kicking the charger watchdog in case
 *				of ABB rev 1.* due to the watchog logic bug
 * @ac_charger_attached_work:	Work for checking if AC charger is still
 *				connected
 * @usb_charger_attached_work:	Work for checking if USB charger is still
 *				connected
 * @ac_work:			Work for checking AC charger connection
 * @detect_usb_type_work:	Work for detecting the USB type connected
 * @usb_link_status_work:	Work for checking the new USB link status
 * @usb_state_changed_work:	Work for checking USB state
 * @attach_work:		Work for detecting USB type
 * @vbus_drop_end_work:		Work for detecting VBUS drop end
 * @check_main_thermal_prot_work:
 *				Work for checking Main thermal status
 * @check_usb_thermal_prot_work:
 *				Work for checking USB thermal status
 * @charger_attached_mutex:	For controlling the wakelock
 */
struct ab8500_charger {
	struct device *dev;
	bool vbus_detected;
	bool vbus_detected_start;
	bool ac_conn;
	bool vddadc_en_ac;
	bool vddadc_en_usb;
	int vbat;
	int old_vbat;
	bool usb_device_is_unrecognised;
	bool autopower;
	bool autopower_cfg;
	int invalid_charger_detect_state;
	int is_aca_rid;
	atomic_t current_stepping_sessions;
	struct ab8500 *parent;
	struct ab8500_gpadc *gpadc;
	struct abx500_bm_data *bm;
	struct ab8500_charger_event_flags flags;
	struct ab8500_charger_usb_state usb_state;
	struct ab8500_charger_max_usb_in_curr max_usb_in_curr;
	struct ux500_charger ac_chg;
	struct ux500_charger usb_chg;
	struct ab8500_charger_info ac;
	struct ab8500_charger_info usb;
	struct regulator *regu;
	struct workqueue_struct *charger_wq;
	struct mutex usb_ipt_crnt_lock;
	struct delayed_work check_vbat_work;
	struct delayed_work check_hw_failure_work;
	struct delayed_work check_usbchgnotok_work;
	struct delayed_work kick_wd_work;
	struct delayed_work usb_state_changed_work;
	struct delayed_work attach_work;
	struct delayed_work ac_charger_attached_work;
	struct delayed_work usb_charger_attached_work;
	struct delayed_work vbus_drop_end_work;
	struct work_struct ac_work;
	struct work_struct detect_usb_type_work;
	struct work_struct usb_link_status_work;
	struct work_struct check_main_thermal_prot_work;
	struct work_struct check_usb_thermal_prot_work;
	struct usb_phy *usb_phy;
	struct notifier_block nb;
	struct mutex charger_attached_mutex;
};

/* AC properties */
static enum power_supply_property ab8500_charger_ac_props[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

/* USB properties */
static enum power_supply_property ab8500_charger_usb_props[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

/*
 * Function for enabling and disabling sw fallback mode
 * should always be disabled when no charger is connected.
 */
static void ab8500_enable_disable_sw_fallback(struct ab8500_charger *di,
		bool fallback)
{
	u8 val;
	u8 reg;
	u8 bank;
	u8 bit;
	int ret;

	dev_dbg(di->dev, "SW Fallback: %d\n", fallback);

	if (is_ab8500(di->parent)) {
		bank = 0x15;
		reg = 0x0;
		bit = 3;
	} else {
		bank = AB8500_SYS_CTRL1_BLOCK;
		reg = AB8500_SW_CONTROL_FALLBACK;
		bit = 0;
	}

	/* read the register containing fallback bit */
	ret = abx500_get_register_interruptible(di->dev, bank, reg, &val);
	if (ret < 0) {
		dev_err(di->dev, "%d read failed\n", __LINE__);
		return;
	}

	if (is_ab8500(di->parent)) {
		/* enable the OPT emulation registers */
		ret = abx500_set_register_interruptible(di->dev, 0x11, 0x00, 0x2);
		if (ret) {
			dev_err(di->dev, "%d write failed\n", __LINE__);
			goto disable_otp;
		}
	}

	if (fallback)
		val |= (1 << bit);
	else
		val &= ~(1 << bit);

	/* write back the changed fallback bit value to register */
	ret = abx500_set_register_interruptible(di->dev, bank, reg, val);
	if (ret) {
		dev_err(di->dev, "%d write failed\n", __LINE__);
	}

disable_otp:
	if (is_ab8500(di->parent)) {
		/* disable the set OTP registers again */
		ret = abx500_set_register_interruptible(di->dev, 0x11, 0x00, 0x0);
		if (ret) {
			dev_err(di->dev, "%d write failed\n", __LINE__);
		}
	}
}

/**
 * ab8500_power_supply_changed - a wrapper with local extentions for
 * power_supply_changed
 * @di:	  pointer to the ab8500_charger structure
 * @psy:  pointer to power_supply_that have changed.
 *
 */
static void ab8500_power_supply_changed(struct ab8500_charger *di,
					struct power_supply *psy)
{
	if (di->autopower_cfg) {
		if (!di->usb.charger_connected &&
		    !di->ac.charger_connected &&
		    di->autopower) {
			di->autopower = false;
			ab8500_enable_disable_sw_fallback(di, false);
		} else if (!di->autopower &&
			   (di->ac.charger_connected ||
			    di->usb.charger_connected)) {
			di->autopower = true;
			ab8500_enable_disable_sw_fallback(di, true);
		}
	}
	power_supply_changed(psy);
}

static void ab8500_charger_set_usb_connected(struct ab8500_charger *di,
	bool connected)
{
	if (connected != di->usb.charger_connected) {
		dev_dbg(di->dev, "USB connected:%i\n", connected);
		di->usb.charger_connected = connected;

		if (!connected)
			di->flags.vbus_drop_end = false;

		sysfs_notify(&di->usb_chg.psy->dev.kobj, NULL, "present");

		if (connected) {
			mutex_lock(&di->charger_attached_mutex);
			mutex_unlock(&di->charger_attached_mutex);

			if (is_ab8500(di->parent))
				queue_delayed_work(di->charger_wq,
					   &di->usb_charger_attached_work,
					   HZ);
		} else {
			cancel_delayed_work_sync(&di->usb_charger_attached_work);
			mutex_lock(&di->charger_attached_mutex);
			mutex_unlock(&di->charger_attached_mutex);
		}
	}
}

/**
 * ab8500_charger_get_ac_voltage() - get ac charger voltage
 * @di:		pointer to the ab8500_charger structure
 *
 * Returns ac charger voltage (on success)
 */
static int ab8500_charger_get_ac_voltage(struct ab8500_charger *di)
{
	int vch;

	/* Only measure voltage if the charger is connected */
	if (di->ac.charger_connected) {
		vch = ab8500_gpadc_convert(di->gpadc, MAIN_CHARGER_V);
		if (vch < 0)
			dev_err(di->dev, "%s gpadc conv failed,\n", __func__);
	} else {
		vch = 0;
	}
	return vch;
}

/**
 * ab8500_charger_ac_cv() - check if the main charger is in CV mode
 * @di:		pointer to the ab8500_charger structure
 *
 * Returns ac charger CV mode (on success) else error code
 */
static int ab8500_charger_ac_cv(struct ab8500_charger *di)
{
	u8 val;
	int ret = 0;

	/* Only check CV mode if the charger is online */
	if (di->ac.charger_online) {
		ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CH_STATUS1_REG, &val);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return 0;
		}

		if (val & MAIN_CH_CV_ON)
			ret = 1;
		else
			ret = 0;
	}

	return ret;
}

/**
 * ab8500_charger_get_vbus_voltage() - get vbus voltage
 * @di:		pointer to the ab8500_charger structure
 *
 * This function returns the vbus voltage.
 * Returns vbus voltage (on success)
 */
static int ab8500_charger_get_vbus_voltage(struct ab8500_charger *di)
{
	int vch;

	/* Only measure voltage if the charger is connected */
	if (di->usb.charger_connected) {
		vch = ab8500_gpadc_convert(di->gpadc, VBUS_V);
		if (vch < 0)
			dev_err(di->dev, "%s gpadc conv failed\n", __func__);
	} else {
		vch = 0;
	}
	return vch;
}

/**
 * ab8500_charger_get_usb_current() - get usb charger current
 * @di:		pointer to the ab8500_charger structure
 *
 * This function returns the usb charger current.
 * Returns usb current (on success) and error code on failure
 */
static int ab8500_charger_get_usb_current(struct ab8500_charger *di)
{
	int ich;

	/* Only measure current if the charger is online */
	if (di->usb.charger_online) {
		ich = ab8500_gpadc_convert(di->gpadc, USB_CHARGER_C);
		if (ich < 0)
			dev_err(di->dev, "%s gpadc conv failed\n", __func__);
	} else {
		ich = 0;
	}
	return ich;
}

/**
 * ab8500_charger_get_ac_current() - get ac charger current
 * @di:		pointer to the ab8500_charger structure
 *
 * This function returns the ac charger current.
 * Returns ac current (on success) and error code on failure.
 */
static int ab8500_charger_get_ac_current(struct ab8500_charger *di)
{
	int ich;

	/* Only measure current if the charger is online */
	if (di->ac.charger_online) {
		ich = ab8500_gpadc_convert(di->gpadc, MAIN_CHARGER_C);
		if (ich < 0)
			dev_err(di->dev, "%s gpadc conv failed\n", __func__);
	} else {
		ich = 0;
	}
	return ich;
}

/**
 * ab8500_charger_usb_cv() - check if the usb charger is in CV mode
 * @di:		pointer to the ab8500_charger structure
 *
 * Returns ac charger CV mode (on success) else error code
 */
static int ab8500_charger_usb_cv(struct ab8500_charger *di)
{
	int ret;
	u8 val;

	/* Only check CV mode if the charger is online */
	if (di->usb.charger_online) {
		ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CH_USBCH_STAT1_REG, &val);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return 0;
		}

		if (val & USB_CH_CV_ON)
			ret = 1;
		else
			ret = 0;
	} else {
		ret = 0;
	}

	return ret;
}

/**
 * ab8500_charger_detect_chargers() - Detect the connected chargers
 * @di:		pointer to the ab8500_charger structure
 * @probe:	if probe, don't delay and wait for HW
 *
 * Returns the type of charger connected.
 * For USB it will not mean we can actually charge from it
 * but that there is a USB cable connected that we have to
 * identify. This is used during startup when we don't get
 * interrupts of the charger detection
 *
 * Returns an integer value, that means,
 * NO_PW_CONN  no power supply is connected
 * AC_PW_CONN  if the AC power supply is connected
 * USB_PW_CONN  if the USB power supply is connected
 * AC_PW_CONN + USB_PW_CONN if USB and AC power supplies are both connected
 */
static int ab8500_charger_detect_chargers(struct ab8500_charger *di, bool probe)
{
	int result = NO_PW_CONN;
	int ret;
	u8 val;

	/* Check for AC charger */
	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CH_STATUS1_REG, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

	if (val & MAIN_CH_DET)
		result = AC_PW_CONN;

	/* Check for USB charger */

	if (!probe) {
		/*
		 * AB8500 says VBUS_DET_DBNC1 & VBUS_DET_DBNC100
		 * when disconnecting ACA even though no
		 * charger was connected. Try waiting a little
		 * longer than the 100 ms of VBUS_DET_DBNC100...
		 */
		msleep(110);
	}
	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CH_USBCH_STAT1_REG, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}
	dev_dbg(di->dev,
		"%s AB8500_CH_USBCH_STAT1_REG %x\n", __func__,
		val);
	if ((val & VBUS_DET_DBNC1) && (val & VBUS_DET_DBNC100))
		result |= USB_PW_CONN;

	return result;
}

/**
 * ab8500_charger_max_usb_curr() - get the max curr for the USB type
 * @di:			pointer to the ab8500_charger structure
 * @link_status:	the identified USB type
 *
 * Get the maximum current that is allowed to be drawn from the host
 * based on the USB type.
 * Returns error code in case of failure else 0 on success
 */
static int ab8500_charger_max_usb_curr(struct ab8500_charger *di,
		enum ab8500_charger_link_status link_status)
{
	int ret = 0;

	di->usb_device_is_unrecognised = false;

	/*
	 * Platform only supports USB 2.0.
	 * This means that charging current from USB source
	 * is maximum 500 mA. Every occurence of USB_STAT_*_HOST_*
	 * should set USB_CH_IP_CUR_LVL_0P5.
	 */

	switch (link_status) {
	case USB_STAT_STD_HOST_NC:
	case USB_STAT_STD_HOST_C_NS:
	case USB_STAT_STD_HOST_C_S:
		dev_dbg(di->dev, "USB Type - Standard host is "
			"detected through USB driver\n");
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P5;
		di->is_aca_rid = 0;
		break;
	case USB_STAT_HOST_CHG_HS_CHIRP:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P5;
		di->is_aca_rid = 0;
		break;
	case USB_STAT_HOST_CHG_HS:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P5;
		di->is_aca_rid = 0;
		break;
	case USB_STAT_ACA_RID_C_HS:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P9;
		di->is_aca_rid = 0;
		break;
	case USB_STAT_ACA_RID_A:
		/*
		 * Dedicated charger level minus maximum current accessory
		 * can consume (900mA). Closest level is 500mA
		 */
		dev_dbg(di->dev, "USB_STAT_ACA_RID_A detected\n");
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P5;
		di->is_aca_rid = 1;
		break;
	case USB_STAT_ACA_RID_B:
		/*
		 * Dedicated charger level minus 120mA (20mA for ACA and
		 * 100mA for potential accessory). Closest level is 1300mA
		 */
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_1P3;
		dev_dbg(di->dev, "USB Type - 0x%02x MaxCurr: %d", link_status,
				di->max_usb_in_curr.usb_type_max);
		di->is_aca_rid = 1;
		break;
	case USB_STAT_HOST_CHG_NM:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P5;
		di->is_aca_rid = 0;
		break;
	case USB_STAT_DEDICATED_CHG:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_1P5;
		di->is_aca_rid = 0;
		break;
	case USB_STAT_ACA_RID_C_HS_CHIRP:
	case USB_STAT_ACA_RID_C_NM:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_1P5;
		di->is_aca_rid = 1;
		break;
	case USB_STAT_NOT_CONFIGURED:
		if (di->vbus_detected) {
			di->usb_device_is_unrecognised = true;
			dev_dbg(di->dev, "USB Type - Legacy charger.\n");
			di->max_usb_in_curr.usb_type_max =
						USB_CH_IP_CUR_LVL_1P5;
			break;
		}
		/* Else, fall through */
	case USB_STAT_HM_IDGND:
		dev_err(di->dev, "USB Type - Charging not allowed\n");
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P05;
		ret = -ENXIO;
		break;
	case USB_STAT_RESERVED:
		if (is_ab8500(di->parent)) {
			di->flags.vbus_collapse = true;
			dev_err(di->dev, "USB Type - USB_STAT_RESERVED "
						"VBUS has collapsed\n");
			ret = -ENXIO;
			break;
		} else {
			dev_dbg(di->dev, "USB Type - Charging not allowed\n");
			di->max_usb_in_curr.usb_type_max =
						USB_CH_IP_CUR_LVL_0P05;
			dev_dbg(di->dev, "USB Type - 0x%02x MaxCurr: %d",
				link_status,
				di->max_usb_in_curr.usb_type_max);
			ret = -ENXIO;
			break;
		}
	case USB_STAT_CARKIT_1:
	case USB_STAT_CARKIT_2:
	case USB_STAT_ACA_DOCK_CHARGER:
	case USB_STAT_CHARGER_LINE_1:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P5;
		dev_dbg(di->dev, "USB Type - 0x%02x MaxCurr: %d", link_status,
				di->max_usb_in_curr.usb_type_max);
		break;
	case USB_STAT_NOT_VALID_LINK:
		dev_err(di->dev, "USB Type invalid - try charging anyway\n");
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P5;
		break;

	default:
		dev_err(di->dev, "USB Type - Unknown\n");
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P05;
		ret = -ENXIO;
		break;
	};

	di->max_usb_in_curr.set_max = di->max_usb_in_curr.usb_type_max;
	dev_dbg(di->dev, "USB Type - 0x%02x MaxCurr: %d",
		link_status, di->max_usb_in_curr.set_max);

	return ret;
}

/**
 * ab8500_charger_read_usb_type() - read the type of usb connected
 * @di:		pointer to the ab8500_charger structure
 *
 * Detect the type of the plugged USB
 * Returns error code in case of failure else 0 on success
 */
static int ab8500_charger_read_usb_type(struct ab8500_charger *di)
{
	int ret;
	u8 val;

	ret = abx500_get_register_interruptible(di->dev,
		AB8500_INTERRUPT, AB8500_IT_SOURCE21_REG, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}
	if (is_ab8500(di->parent))
		ret = abx500_get_register_interruptible(di->dev, AB8500_USB,
			AB8500_USB_LINE_STAT_REG, &val);
	else
		ret = abx500_get_register_interruptible(di->dev,
			AB8500_USB, AB8500_USB_LINK1_STAT_REG, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

	/* get the USB type */
	if (is_ab8500(di->parent))
		val = (val & AB8500_USB_LINK_STATUS) >> USB_LINK_STATUS_SHIFT;
	else
		val = (val & AB8505_USB_LINK_STATUS) >> USB_LINK_STATUS_SHIFT;
	ret = ab8500_charger_max_usb_curr(di,
		(enum ab8500_charger_link_status) val);

	return ret;
}

/**
 * ab8500_charger_detect_usb_type() - get the type of usb connected
 * @di:		pointer to the ab8500_charger structure
 *
 * Detect the type of the plugged USB
 * Returns error code in case of failure else 0 on success
 */
static int ab8500_charger_detect_usb_type(struct ab8500_charger *di)
{
	int i, ret;
	u8 val;

	/*
	 * On getting the VBUS rising edge detect interrupt there
	 * is a 250ms delay after which the register UsbLineStatus
	 * is filled with valid data.
	 */
	for (i = 0; i < 10; i++) {
		msleep(250);
		ret = abx500_get_register_interruptible(di->dev,
			AB8500_INTERRUPT, AB8500_IT_SOURCE21_REG,
			&val);
		dev_dbg(di->dev, "%s AB8500_IT_SOURCE21_REG %x\n",
			__func__, val);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return ret;
		}

		if (is_ab8500(di->parent))
			ret = abx500_get_register_interruptible(di->dev,
				AB8500_USB, AB8500_USB_LINE_STAT_REG, &val);
		else
			ret = abx500_get_register_interruptible(di->dev,
				AB8500_USB, AB8500_USB_LINK1_STAT_REG, &val);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return ret;
		}
		dev_dbg(di->dev, "%s AB8500_USB_LINE_STAT_REG %x\n", __func__,
			val);
		/*
		 * Until the IT source register is read the UsbLineStatus
		 * register is not updated, hence doing the same
		 * Revisit this:
		 */

		/* get the USB type */
		if (is_ab8500(di->parent))
			val = (val & AB8500_USB_LINK_STATUS) >>
							USB_LINK_STATUS_SHIFT;
		else
			val = (val & AB8505_USB_LINK_STATUS) >>
							USB_LINK_STATUS_SHIFT;
		if (val)
			break;
	}
	ret = ab8500_charger_max_usb_curr(di,
		(enum ab8500_charger_link_status) val);

	return ret;
}

/*
 * This array maps the raw hex value to charger voltage used by the AB8500
 * Values taken from the UM0836
 */
static int ab8500_charger_voltage_map[] = {
	3500 ,
	3525 ,
	3550 ,
	3575 ,
	3600 ,
	3625 ,
	3650 ,
	3675 ,
	3700 ,
	3725 ,
	3750 ,
	3775 ,
	3800 ,
	3825 ,
	3850 ,
	3875 ,
	3900 ,
	3925 ,
	3950 ,
	3975 ,
	4000 ,
	4025 ,
	4050 ,
	4060 ,
	4070 ,
	4080 ,
	4090 ,
	4100 ,
	4110 ,
	4120 ,
	4130 ,
	4140 ,
	4150 ,
	4160 ,
	4170 ,
	4180 ,
	4190 ,
	4200 ,
	4210 ,
	4220 ,
	4230 ,
	4240 ,
	4250 ,
	4260 ,
	4270 ,
	4280 ,
	4290 ,
	4300 ,
	4310 ,
	4320 ,
	4330 ,
	4340 ,
	4350 ,
	4360 ,
	4370 ,
	4380 ,
	4390 ,
	4400 ,
	4410 ,
	4420 ,
	4430 ,
	4440 ,
	4450 ,
	4460 ,
	4470 ,
	4480 ,
	4490 ,
	4500 ,
	4510 ,
	4520 ,
	4530 ,
	4540 ,
	4550 ,
	4560 ,
	4570 ,
	4580 ,
	4590 ,
	4600 ,
};

static int ab8500_voltage_to_regval(int voltage)
{
	int i;

	/* Special case for voltage below 3.5V */
	if (voltage < ab8500_charger_voltage_map[0])
		return LOW_VOLT_REG;

	for (i = 1; i < ARRAY_SIZE(ab8500_charger_voltage_map); i++) {
		if (voltage < ab8500_charger_voltage_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab8500_charger_voltage_map) - 1;
	if (voltage == ab8500_charger_voltage_map[i])
		return i;
	else
		return -1;
}

static int ab8500_current_to_regval(struct ab8500_charger *di, int curr)
{
	int i;

	if (curr < di->bm->chg_output_curr[0])
		return 0;

	for (i = 0; i < di->bm->n_chg_out_curr; i++) {
		if (curr < di->bm->chg_output_curr[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = di->bm->n_chg_out_curr - 1;
	if (curr == di->bm->chg_output_curr[i])
		return i;
	else
		return -1;
}

static int ab8500_vbus_in_curr_to_regval(struct ab8500_charger *di, int curr)
{
	int i;

	if (curr < di->bm->chg_input_curr[0])
		return 0;

	for (i = 0; i < di->bm->n_chg_in_curr; i++) {
		if (curr < di->bm->chg_input_curr[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = di->bm->n_chg_in_curr - 1;
	if (curr == di->bm->chg_input_curr[i])
		return i;
	else
		return -1;
}

/**
 * ab8500_charger_get_usb_cur() - get usb current
 * @di:		pointer to the ab8500_charger structre
 *
 * The usb stack provides the maximum current that can be drawn from
 * the standard usb host. This will be in mA.
 * This function converts current in mA to a value that can be written
 * to the register. Returns -1 if charging is not allowed
 */
static int ab8500_charger_get_usb_cur(struct ab8500_charger *di)
{
	int ret = 0;
	switch (di->usb_state.usb_current) {
	case 100:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P09;
		break;
	case 200:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P19;
		break;
	case 300:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P29;
		break;
	case 400:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P38;
		break;
	case 500:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P5;
		break;
	default:
		di->max_usb_in_curr.usb_type_max = USB_CH_IP_CUR_LVL_0P05;
		ret = -EPERM;
		break;
	};
	di->max_usb_in_curr.set_max = di->max_usb_in_curr.usb_type_max;
	return ret;
}

/**
 * ab8500_charger_check_continue_stepping() - Check to allow stepping
 * @di:		pointer to the ab8500_charger structure
 * @reg:	select what charger register to check
 *
 * Check if current stepping should be allowed to continue.
 * Checks if charger source has not collapsed. If it has, further stepping
 * is not allowed.
 */
static bool ab8500_charger_check_continue_stepping(struct ab8500_charger *di,
						   int reg)
{
	if (reg == AB8500_USBCH_IPT_CRNTLVL_REG)
		return !di->flags.vbus_drop_end;
	else
		return true;
}

/**
 * ab8500_charger_set_current() - set charger current
 * @di:		pointer to the ab8500_charger structure
 * @ich:	charger current, in mA
 * @reg:	select what charger register to set
 *
 * Set charger current.
 * There is no state machine in the AB to step up/down the charger
 * current to avoid dips and spikes on MAIN, VBUS and VBAT when
 * charging is started. Instead we need to implement
 * this charger current step-up/down here.
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_set_current(struct ab8500_charger *di,
	int ich, int reg)
{
	int ret = 0;
	int curr_index, prev_curr_index, shift_value, i;
	u8 reg_value;
	u32 step_udelay;
	bool no_stepping = false;

	atomic_inc(&di->current_stepping_sessions);

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
		reg, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s read failed\n", __func__);
		goto exit_set_current;
	}

	switch (reg) {
	case AB8500_MCH_IPT_CURLVL_REG:
		shift_value = MAIN_CH_INPUT_CURR_SHIFT;
		prev_curr_index = (reg_value >> shift_value);
		curr_index = ab8500_current_to_regval(di, ich);
		step_udelay = STEP_UDELAY;
		if (!di->ac.charger_connected)
			no_stepping = true;
		break;
	case AB8500_USBCH_IPT_CRNTLVL_REG:
		shift_value = VBUS_IN_CURR_LIM_SHIFT;
		prev_curr_index = (reg_value >> shift_value);
		curr_index = ab8500_vbus_in_curr_to_regval(di, ich);
		step_udelay = STEP_UDELAY * 100;

		if (!di->usb.charger_connected)
			no_stepping = true;
		break;
	case AB8500_CH_OPT_CRNTLVL_REG:
		shift_value = 0;
		prev_curr_index = (reg_value >> shift_value);
		curr_index = ab8500_current_to_regval(di, ich);
		step_udelay = STEP_UDELAY;
		if (curr_index && (curr_index - prev_curr_index) > 1)
			step_udelay *= 100;

		if (!di->usb.charger_connected && !di->ac.charger_connected)
			no_stepping = true;

		break;
	default:
		dev_err(di->dev, "%s current register not valid\n", __func__);
		ret = -ENXIO;
		goto exit_set_current;
	}

	if (curr_index < 0) {
		dev_err(di->dev, "requested current limit out-of-range\n");
		ret = -ENXIO;
		goto exit_set_current;
	}

	/* only update current if it's been changed */
	if (prev_curr_index == curr_index) {
		dev_dbg(di->dev, "%s current not changed for reg: 0x%02x\n",
			__func__, reg);
		ret = 0;
		goto exit_set_current;
	}

	dev_dbg(di->dev, "%s set charger current: %d mA for reg: 0x%02x\n",
		__func__, ich, reg);

	if (no_stepping) {
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
					reg, (u8)curr_index << shift_value);
		if (ret)
			dev_err(di->dev, "%s write failed\n", __func__);
	} else if (prev_curr_index > curr_index) {
		for (i = prev_curr_index - 1; i >= curr_index; i--) {
			dev_dbg(di->dev, "curr change_1 to: %x for 0x%02x\n",
				(u8) i << shift_value, reg);
			ret = abx500_set_register_interruptible(di->dev,
				AB8500_CHARGER, reg, (u8)i << shift_value);
			if (ret) {
				dev_err(di->dev, "%s write failed\n", __func__);
				goto exit_set_current;
			}
			if (i != curr_index)
				usleep_range(step_udelay, step_udelay * 2);
		}
	} else {
		bool allow = true;
		for (i = prev_curr_index + 1; i <= curr_index && allow; i++) {
			dev_dbg(di->dev, "curr change_2 to: %x for 0x%02x\n",
				(u8)i << shift_value, reg);
			ret = abx500_set_register_interruptible(di->dev,
				AB8500_CHARGER, reg, (u8)i << shift_value);
			if (ret) {
				dev_err(di->dev, "%s write failed\n", __func__);
				goto exit_set_current;
			}
			if (i != curr_index)
				usleep_range(step_udelay, step_udelay * 2);

			allow = ab8500_charger_check_continue_stepping(di, reg);
		}
	}

exit_set_current:
	atomic_dec(&di->current_stepping_sessions);

	return ret;
}

/**
 * ab8500_charger_set_vbus_in_curr() - set VBUS input current limit
 * @di:		pointer to the ab8500_charger structure
 * @ich_in:	charger input current limit
 *
 * Sets the current that can be drawn from the USB host
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_set_vbus_in_curr(struct ab8500_charger *di,
		int ich_in)
{
	int min_value;
	int ret;

	/* We should always use to lowest current limit */
	min_value = min(di->bm->chg_params->usb_curr_max, ich_in);
	if (di->max_usb_in_curr.set_max > 0)
		min_value = min(di->max_usb_in_curr.set_max, min_value);

	if (di->usb_state.usb_current >= 0)
		min_value = min(di->usb_state.usb_current, min_value);

	switch (min_value) {
	case 100:
		if (di->vbat < VBAT_TRESH_IP_CUR_RED)
			min_value = USB_CH_IP_CUR_LVL_0P05;
		break;
	case 500:
		if (di->vbat < VBAT_TRESH_IP_CUR_RED)
			min_value = USB_CH_IP_CUR_LVL_0P45;
		break;
	default:
		break;
	}

	dev_info(di->dev, "VBUS input current limit set to %d mA\n", min_value);

	mutex_lock(&di->usb_ipt_crnt_lock);
	ret = ab8500_charger_set_current(di, min_value,
		AB8500_USBCH_IPT_CRNTLVL_REG);
	mutex_unlock(&di->usb_ipt_crnt_lock);

	return ret;
}

/**
 * ab8500_charger_set_main_in_curr() - set main charger input current
 * @di:		pointer to the ab8500_charger structure
 * @ich_in:	input charger current, in mA
 *
 * Set main charger input current.
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_set_main_in_curr(struct ab8500_charger *di,
	int ich_in)
{
	return ab8500_charger_set_current(di, ich_in,
		AB8500_MCH_IPT_CURLVL_REG);
}

/**
 * ab8500_charger_set_output_curr() - set charger output current
 * @di:		pointer to the ab8500_charger structure
 * @ich_out:	output charger current, in mA
 *
 * Set charger output current.
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_set_output_curr(struct ab8500_charger *di,
	int ich_out)
{
	return ab8500_charger_set_current(di, ich_out,
		AB8500_CH_OPT_CRNTLVL_REG);
}

/**
 * ab8500_charger_led_en() - turn on/off chargign led
 * @di:		pointer to the ab8500_charger structure
 * @on:		flag to turn on/off the chargign led
 *
 * Power ON/OFF charging LED indication
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_led_en(struct ab8500_charger *di, int on)
{
	int ret;

	if (on) {
		/* Power ON charging LED indicator, set LED current to 5mA */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_LED_INDICATOR_PWM_CTRL,
			(LED_IND_CUR_5MA | LED_INDICATOR_PWM_ENA));
		if (ret) {
			dev_err(di->dev, "Power ON LED failed\n");
			return ret;
		}
		/* LED indicator PWM duty cycle 252/256 */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_LED_INDICATOR_PWM_DUTY,
			LED_INDICATOR_PWM_DUTY_252_256);
		if (ret) {
			dev_err(di->dev, "Set LED PWM duty cycle failed\n");
			return ret;
		}
	} else {
		/* Power off charging LED indicator */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_LED_INDICATOR_PWM_CTRL,
			LED_INDICATOR_PWM_DIS);
		if (ret) {
			dev_err(di->dev, "Power-off LED failed\n");
			return ret;
		}
	}

	return ret;
}

/**
 * ab8500_charger_ac_en() - enable or disable ac charging
 * @di:		pointer to the ab8500_charger structure
 * @enable:	enable/disable flag
 * @vset:	charging voltage
 * @iset:	charging current
 *
 * Enable/Disable AC/Mains charging and turns on/off the charging led
 * respectively.
 **/
static int ab8500_charger_ac_en(struct ux500_charger *charger,
	int enable, int vset, int iset)
{
	int ret;
	int volt_index;
	int curr_index;
	int input_curr_index;
	u8 overshoot = 0;

	struct ab8500_charger *di = to_ab8500_charger_ac_device_info(charger);

	if (enable) {
		/* Check if AC is connected */
		if (!di->ac.charger_connected) {
			dev_err(di->dev, "AC charger not connected\n");
			return -ENXIO;
		}

		/* Enable AC charging */
		dev_dbg(di->dev, "Enable AC: %dmV %dmA\n", vset, iset);

		/*
		 * Due to a bug in AB8500, BTEMP_HIGH/LOW interrupts
		 * will be triggered everytime we enable the VDD ADC supply.
		 * This will turn off charging for a short while.
		 * It can be avoided by having the supply on when
		 * there is a charger enabled. Normally the VDD ADC supply
		 * is enabled everytime a GPADC conversion is triggered. We will
		 * force it to be enabled from this driver to have
		 * the GPADC module independant of the AB8500 chargers
		 */
		if (!di->vddadc_en_ac) {
			ret = regulator_enable(di->regu);
			if (ret)
				dev_warn(di->dev,
					"Failed to enable regulator\n");
			else
				di->vddadc_en_ac = true;
		}

		/* Check if the requested voltage or current is valid */
		volt_index = ab8500_voltage_to_regval(vset);
		curr_index = ab8500_current_to_regval(di, iset);
		input_curr_index = ab8500_current_to_regval(di,
			di->bm->chg_params->ac_curr_max);
		if (volt_index < 0 || curr_index < 0 || input_curr_index < 0) {
			dev_err(di->dev,
				"Charger voltage or current too high, "
				"charging not started\n");
			return -ENXIO;
		}

		/* ChVoltLevel: maximum battery charging voltage */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CH_VOLT_LVL_REG, (u8) volt_index);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}
		/* MainChInputCurr: current that can be drawn from the charger*/
		ret = ab8500_charger_set_main_in_curr(di,
			di->bm->chg_params->ac_curr_max);
		if (ret) {
			dev_err(di->dev, "%s Failed to set MainChInputCurr\n",
				__func__);
			return ret;
		}
		/* ChOutputCurentLevel: protected output current */
		ret = ab8500_charger_set_output_curr(di, iset);
		if (ret) {
			dev_err(di->dev, "%s "
				"Failed to set ChOutputCurentLevel\n",
				__func__);
			return ret;
		}

		/* Check if VBAT overshoot control should be enabled */
		if (!di->bm->enable_overshoot)
			overshoot = MAIN_CH_NO_OVERSHOOT_ENA_N;

		/* Enable Main Charger */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_MCH_CTRL1, MAIN_CH_ENA | overshoot);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		/* Power on charging LED indication */
		ret = ab8500_charger_led_en(di, true);
		if (ret < 0)
			dev_err(di->dev, "failed to enable LED\n");

		di->ac.charger_online = 1;
	} else {
		/* Disable AC charging */
		if (is_ab8500_1p1_or_earlier(di->parent)) {
			/*
			 * For ABB revision 1.0 and 1.1 there is a bug in the
			 * watchdog logic. That means we have to continously
			 * kick the charger watchdog even when no charger is
			 * connected. This is only valid once the AC charger
			 * has been enabled. This is a bug that is not handled
			 * by the algorithm and the watchdog have to be kicked
			 * by the charger driver when the AC charger
			 * is disabled
			 */
			if (di->ac_conn) {
				queue_delayed_work(di->charger_wq,
					&di->kick_wd_work,
					round_jiffies(WD_KICK_INTERVAL));
			}

			/*
			 * We can't turn off charging completely
			 * due to a bug in AB8500 cut1.
			 * If we do, charging will not start again.
			 * That is why we set the lowest voltage
			 * and current possible
			 */
			ret = abx500_set_register_interruptible(di->dev,
				AB8500_CHARGER,
				AB8500_CH_VOLT_LVL_REG, CH_VOL_LVL_3P5);
			if (ret) {
				dev_err(di->dev,
					"%s write failed\n", __func__);
				return ret;
			}

			ret = ab8500_charger_set_output_curr(di, 0);
			if (ret) {
				dev_err(di->dev, "%s "
					"Failed to set ChOutputCurentLevel\n",
					__func__);
				return ret;
			}
		} else {
			ret = abx500_set_register_interruptible(di->dev,
				AB8500_CHARGER,
				AB8500_MCH_CTRL1, 0);
			if (ret) {
				dev_err(di->dev,
					"%s write failed\n", __func__);
				return ret;
			}
		}

		ret = ab8500_charger_led_en(di, false);
		if (ret < 0)
			dev_err(di->dev, "failed to disable LED\n");

		di->ac.charger_online = 0;
		di->ac.wd_expired = false;

		/* Disable regulator if enabled */
		if (di->vddadc_en_ac) {
			regulator_disable(di->regu);
			di->vddadc_en_ac = false;
		}

		dev_dbg(di->dev, "%s Disabled AC charging\n", __func__);
	}
	ab8500_power_supply_changed(di, di->ac_chg.psy);

	return ret;
}

/**
 * ab8500_charger_usb_en() - enable usb charging
 * @di:		pointer to the ab8500_charger structure
 * @enable:	enable/disable flag
 * @vset:	charging voltage
 * @ich_out:	charger output current
 *
 * Enable/Disable USB charging and turns on/off the charging led respectively.
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_usb_en(struct ux500_charger *charger,
	int enable, int vset, int ich_out)
{
	int ret;
	int volt_index;
	int curr_index;
	u8 overshoot = 0;

	struct ab8500_charger *di = to_ab8500_charger_usb_device_info(charger);

	if (enable) {
		/* Check if USB is connected */
		if (!di->usb.charger_connected) {
			dev_err(di->dev, "USB charger not connected\n");
			return -ENXIO;
		}

		/*
		 * Due to a bug in AB8500, BTEMP_HIGH/LOW interrupts
		 * will be triggered everytime we enable the VDD ADC supply.
		 * This will turn off charging for a short while.
		 * It can be avoided by having the supply on when
		 * there is a charger enabled. Normally the VDD ADC supply
		 * is enabled everytime a GPADC conversion is triggered. We will
		 * force it to be enabled from this driver to have
		 * the GPADC module independant of the AB8500 chargers
		 */
		if (!di->vddadc_en_usb) {
			ret = regulator_enable(di->regu);
			if (ret)
				dev_warn(di->dev,
					"Failed to enable regulator\n");
			else
				di->vddadc_en_usb = true;
		}

		/* Enable USB charging */
		dev_dbg(di->dev, "Enable USB: %dmV %dmA\n", vset, ich_out);

		/* Check if the requested voltage or current is valid */
		volt_index = ab8500_voltage_to_regval(vset);
		curr_index = ab8500_current_to_regval(di, ich_out);
		if (volt_index < 0 || curr_index < 0) {
			dev_err(di->dev,
				"Charger voltage or current too high, "
				"charging not started\n");
			return -ENXIO;
		}

		/* ChVoltLevel: max voltage upto which battery can be charged */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CH_VOLT_LVL_REG, (u8) volt_index);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}
		/* Check if VBAT overshoot control should be enabled */
		if (!di->bm->enable_overshoot)
			overshoot = USB_CHG_NO_OVERSHOOT_ENA_N;

		/* Enable USB Charger */
		dev_dbg(di->dev,
			"Enabling USB with write to AB8500_USBCH_CTRL1_REG\n");
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_USBCH_CTRL1_REG, USB_CH_ENA | overshoot);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		/* If success power on charging LED indication */
		ret = ab8500_charger_led_en(di, true);
		if (ret < 0)
			dev_err(di->dev, "failed to enable LED\n");

		di->usb.charger_online = 1;

		/* USBChInputCurr: current that can be drawn from the usb */
		ret = ab8500_charger_set_vbus_in_curr(di,
					di->max_usb_in_curr.usb_type_max);
		if (ret) {
			dev_err(di->dev, "setting USBChInputCurr failed\n");
			return ret;
		}

		/* ChOutputCurentLevel: protected output current */
		ret = ab8500_charger_set_output_curr(di, ich_out);
		if (ret) {
			dev_err(di->dev, "%s "
				"Failed to set ChOutputCurentLevel\n",
				__func__);
			return ret;
		}

		queue_delayed_work(di->charger_wq, &di->check_vbat_work, HZ);

	} else {
		/* Disable USB charging */
		dev_dbg(di->dev, "%s Disabled USB charging\n", __func__);
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_USBCH_CTRL1_REG, 0);
		if (ret) {
			dev_err(di->dev,
				"%s write failed\n", __func__);
			return ret;
		}

		ret = ab8500_charger_led_en(di, false);
		if (ret < 0)
			dev_err(di->dev, "failed to disable LED\n");
		/* USBChInputCurr: current that can be drawn from the usb */
		ret = ab8500_charger_set_vbus_in_curr(di, 0);
		if (ret) {
			dev_err(di->dev, "setting USBChInputCurr failed\n");
			return ret;
		}

		/* ChOutputCurentLevel: protected output current */
		ret = ab8500_charger_set_output_curr(di, 0);
		if (ret) {
			dev_err(di->dev, "%s "
				"Failed to reset ChOutputCurentLevel\n",
				__func__);
			return ret;
		}
		di->usb.charger_online = 0;
		di->usb.wd_expired = false;

		/* Disable regulator if enabled */
		if (di->vddadc_en_usb) {
			regulator_disable(di->regu);
			di->vddadc_en_usb = false;
		}

		dev_dbg(di->dev, "%s Disabled USB charging\n", __func__);

		/* Cancel any pending Vbat check work */
		cancel_delayed_work(&di->check_vbat_work);

	}
	ab8500_power_supply_changed(di, di->usb_chg.psy);

	return ret;
}

static int ab8500_external_charger_prepare(struct notifier_block *charger_nb,
				unsigned long event, void *data)
{
	int ret;
	struct device *dev = data;
	/*Toggle External charger control pin*/
	ret = abx500_set_register_interruptible(dev, AB8500_SYS_CTRL1_BLOCK,
				  AB8500_SYS_CHARGER_CONTROL_REG,
				  EXTERNAL_CHARGER_DISABLE_REG_VAL);
	if (ret < 0) {
		dev_err(dev, "write reg failed %d\n", ret);
		goto out;
	}
	ret = abx500_set_register_interruptible(dev, AB8500_SYS_CTRL1_BLOCK,
				  AB8500_SYS_CHARGER_CONTROL_REG,
				  EXTERNAL_CHARGER_ENABLE_REG_VAL);
	if (ret < 0)
		dev_err(dev, "Write reg failed %d\n", ret);

out:
	return ret;
}

/**
 * ab8500_charger_usb_check_enable() - enable usb charging
 * @charger:	pointer to the ux500_charger structure
 * @vset:	charging voltage
 * @iset:	charger output current
 *
 * Check if the VBUS charger has been disconnected and reconnected without
 * AB8500 rising an interrupt. Returns 0 on success.
 */
static int ab8500_charger_usb_check_enable(struct ux500_charger *charger,
	int vset, int iset)
{
	u8 usbch_ctrl1 = 0;
	int ret = 0;

	struct ab8500_charger *di = to_ab8500_charger_usb_device_info(charger);

	if (!di->usb.charger_connected)
		return ret;

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
				AB8500_USBCH_CTRL1_REG, &usbch_ctrl1);
	if (ret < 0) {
		dev_err(di->dev, "ab8500 read failed %d\n", __LINE__);
		return ret;
	}
	dev_dbg(di->dev, "USB charger ctrl: 0x%02x\n", usbch_ctrl1);

	if (!(usbch_ctrl1 & USB_CH_ENA)) {
		dev_info(di->dev, "Charging has been disabled abnormally and will be re-enabled\n");

		ret = abx500_mask_and_set_register_interruptible(di->dev,
					AB8500_CHARGER, AB8500_CHARGER_CTRL,
					DROP_COUNT_RESET, DROP_COUNT_RESET);
		if (ret < 0) {
			dev_err(di->dev, "ab8500 write failed %d\n", __LINE__);
			return ret;
		}

		ret = ab8500_charger_usb_en(&di->usb_chg, true, vset, iset);
		if (ret < 0) {
			dev_err(di->dev, "Failed to enable VBUS charger %d\n",
					__LINE__);
			return ret;
		}
	}
	return ret;
}

/**
 * ab8500_charger_ac_check_enable() - enable usb charging
 * @charger:	pointer to the ux500_charger structure
 * @vset:	charging voltage
 * @iset:	charger output current
 *
 * Check if the AC charger has been disconnected and reconnected without
 * AB8500 rising an interrupt. Returns 0 on success.
 */
static int ab8500_charger_ac_check_enable(struct ux500_charger *charger,
	int vset, int iset)
{
	u8 mainch_ctrl1 = 0;
	int ret = 0;

	struct ab8500_charger *di = to_ab8500_charger_ac_device_info(charger);

	if (!di->ac.charger_connected)
		return ret;

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
				AB8500_MCH_CTRL1, &mainch_ctrl1);
	if (ret < 0) {
		dev_err(di->dev, "ab8500 read failed %d\n", __LINE__);
		return ret;
	}
	dev_dbg(di->dev, "AC charger ctrl: 0x%02x\n", mainch_ctrl1);

	if (!(mainch_ctrl1 & MAIN_CH_ENA)) {
		dev_info(di->dev, "Charging has been disabled abnormally and will be re-enabled\n");

		ret = abx500_mask_and_set_register_interruptible(di->dev,
					AB8500_CHARGER, AB8500_CHARGER_CTRL,
					DROP_COUNT_RESET, DROP_COUNT_RESET);

		if (ret < 0) {
			dev_err(di->dev, "ab8500 write failed %d\n", __LINE__);
			return ret;
		}

		ret = ab8500_charger_ac_en(&di->usb_chg, true, vset, iset);
		if (ret < 0) {
			dev_err(di->dev, "failed to enable AC charger %d\n",
				__LINE__);
			return ret;
		}
	}
	return ret;
}

/**
 * ab8500_charger_watchdog_kick() - kick charger watchdog
 * @di:		pointer to the ab8500_charger structure
 *
 * Kick charger watchdog
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_watchdog_kick(struct ux500_charger *charger)
{
	int ret;
	struct ab8500_charger *di;

	if (charger->psy->desc->type == POWER_SUPPLY_TYPE_MAINS)
		di = to_ab8500_charger_ac_device_info(charger);
	else if (charger->psy->desc->type == POWER_SUPPLY_TYPE_USB)
		di = to_ab8500_charger_usb_device_info(charger);
	else
		return -ENXIO;

	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CHARG_WD_CTRL, CHARG_WD_KICK);
	if (ret)
		dev_err(di->dev, "Failed to kick WD!\n");

	return ret;
}

/**
 * ab8500_charger_update_charger_current() - update charger current
 * @di:		pointer to the ab8500_charger structure
 *
 * Update the charger output current for the specified charger
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_update_charger_current(struct ux500_charger *charger,
		int ich_out)
{
	int ret;
	struct ab8500_charger *di;

	if (charger->psy->desc->type == POWER_SUPPLY_TYPE_MAINS)
		di = to_ab8500_charger_ac_device_info(charger);
	else if (charger->psy->desc->type == POWER_SUPPLY_TYPE_USB)
		di = to_ab8500_charger_usb_device_info(charger);
	else
		return -ENXIO;

	ret = ab8500_charger_set_output_curr(di, ich_out);
	if (ret) {
		dev_err(di->dev, "%s "
			"Failed to set ChOutputCurentLevel\n",
			__func__);
		return ret;
	}

	/* Reset the main and usb drop input current measurement counter */
	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
				AB8500_CHARGER_CTRL, DROP_COUNT_RESET);
	if (ret) {
		dev_err(di->dev, "%s write failed\n", __func__);
		return ret;
	}

	return ret;
}

static int ab8500_charger_get_ext_psy_data(struct device *dev, void *data)
{
	struct power_supply *psy;
	struct power_supply *ext = dev_get_drvdata(dev);
	const char **supplicants = (const char **)ext->supplied_to;
	struct ab8500_charger *di;
	union power_supply_propval ret;
	int j;
	struct ux500_charger *usb_chg;

	usb_chg = (struct ux500_charger *)data;
	psy = usb_chg->psy;

	di = to_ab8500_charger_usb_device_info(usb_chg);

	/* For all psy where the driver name appears in any supplied_to */
	j = match_string(supplicants, ext->num_supplicants, psy->desc->name);
	if (j < 0)
		return 0;

	/* Go through all properties for the psy */
	for (j = 0; j < ext->desc->num_properties; j++) {
		enum power_supply_property prop;
		prop = ext->desc->properties[j];

		if (power_supply_get_property(ext, prop, &ret))
			continue;

		switch (prop) {
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				di->vbat = ret.intval / 1000;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}
	return 0;
}

/**
 * ab8500_charger_check_vbat_work() - keep vbus current within spec
 * @work	pointer to the work_struct structure
 *
 * Due to a asic bug it is necessary to lower the input current to the vbus
 * charger when charging with at some specific levels. This issue is only valid
 * for below a certain battery voltage. This function makes sure that the
 * the allowed current limit isn't exceeded.
 */
static void ab8500_charger_check_vbat_work(struct work_struct *work)
{
	int t = 10;
	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, check_vbat_work.work);

	class_for_each_device(power_supply_class, NULL,
		di->usb_chg.psy, ab8500_charger_get_ext_psy_data);

	/* First run old_vbat is 0. */
	if (di->old_vbat == 0)
		di->old_vbat = di->vbat;

	if (!((di->old_vbat <= VBAT_TRESH_IP_CUR_RED &&
		di->vbat <= VBAT_TRESH_IP_CUR_RED) ||
		(di->old_vbat > VBAT_TRESH_IP_CUR_RED &&
		di->vbat > VBAT_TRESH_IP_CUR_RED))) {

		dev_dbg(di->dev, "Vbat did cross threshold, curr: %d, new: %d,"
			" old: %d\n", di->max_usb_in_curr.usb_type_max,
			di->vbat, di->old_vbat);
		ab8500_charger_set_vbus_in_curr(di,
					di->max_usb_in_curr.usb_type_max);
		power_supply_changed(di->usb_chg.psy);
	}

	di->old_vbat = di->vbat;

	/*
	 * No need to check the battery voltage every second when not close to
	 * the threshold.
	 */
	if (di->vbat < (VBAT_TRESH_IP_CUR_RED + 100) &&
		(di->vbat > (VBAT_TRESH_IP_CUR_RED - 100)))
			t = 1;

	queue_delayed_work(di->charger_wq, &di->check_vbat_work, t * HZ);
}

/**
 * ab8500_charger_check_hw_failure_work() - check main charger failure
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the main charger status
 */
static void ab8500_charger_check_hw_failure_work(struct work_struct *work)
{
	int ret;
	u8 reg_value;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, check_hw_failure_work.work);

	/* Check if the status bits for HW failure is still active */
	if (di->flags.mainextchnotok) {
		ret = abx500_get_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_CH_STATUS2_REG, &reg_value);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return;
		}
		if (!(reg_value & MAIN_CH_NOK)) {
			di->flags.mainextchnotok = false;
			ab8500_power_supply_changed(di, di->ac_chg.psy);
		}
	}
	if (di->flags.vbus_ovv) {
		ret = abx500_get_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_CH_USBCH_STAT2_REG,
			&reg_value);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return;
		}
		if (!(reg_value & VBUS_OVV_TH)) {
			di->flags.vbus_ovv = false;
			ab8500_power_supply_changed(di, di->usb_chg.psy);
		}
	}
	/* If we still have a failure, schedule a new check */
	if (di->flags.mainextchnotok || di->flags.vbus_ovv) {
		queue_delayed_work(di->charger_wq,
			&di->check_hw_failure_work, round_jiffies(HZ));
	}
}

/**
 * ab8500_charger_kick_watchdog_work() - kick the watchdog
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for kicking the charger watchdog.
 *
 * For ABB revision 1.0 and 1.1 there is a bug in the watchdog
 * logic. That means we have to continously kick the charger
 * watchdog even when no charger is connected. This is only
 * valid once the AC charger has been enabled. This is
 * a bug that is not handled by the algorithm and the
 * watchdog have to be kicked by the charger driver
 * when the AC charger is disabled
 */
static void ab8500_charger_kick_watchdog_work(struct work_struct *work)
{
	int ret;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, kick_wd_work.work);

	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CHARG_WD_CTRL, CHARG_WD_KICK);
	if (ret)
		dev_err(di->dev, "Failed to kick WD!\n");

	/* Schedule a new watchdog kick */
	queue_delayed_work(di->charger_wq,
		&di->kick_wd_work, round_jiffies(WD_KICK_INTERVAL));
}

/**
 * ab8500_charger_ac_work() - work to get and set main charger status
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the main charger status
 */
static void ab8500_charger_ac_work(struct work_struct *work)
{
	int ret;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, ac_work);

	/*
	 * Since we can't be sure that the events are received
	 * synchronously, we have the check if the main charger is
	 * connected by reading the status register
	 */
	ret = ab8500_charger_detect_chargers(di, false);
	if (ret < 0)
		return;

	if (ret & AC_PW_CONN) {
		di->ac.charger_connected = 1;
		di->ac_conn = true;
	} else {
		di->ac.charger_connected = 0;
	}

	ab8500_power_supply_changed(di, di->ac_chg.psy);
	sysfs_notify(&di->ac_chg.psy->dev.kobj, NULL, "present");
}

static void ab8500_charger_usb_attached_work(struct work_struct *work)
{
	struct ab8500_charger *di = container_of(work,
						 struct ab8500_charger,
						 usb_charger_attached_work.work);
	int usbch = (USB_CH_VBUSDROP | USB_CH_VBUSDETDBNC);
	int ret, i;
	u8 statval;

	for (i = 0; i < 10; i++) {
		ret = abx500_get_register_interruptible(di->dev,
							AB8500_CHARGER,
							AB8500_CH_USBCH_STAT1_REG,
							&statval);
		if (ret < 0) {
			dev_err(di->dev, "ab8500 read failed %d\n", __LINE__);
			goto reschedule;
		}
		if ((statval & usbch) != usbch)
			goto reschedule;

		msleep(CHARGER_STATUS_POLL);
	}

	ab8500_charger_usb_en(&di->usb_chg, 0, 0, 0);

	mutex_lock(&di->charger_attached_mutex);
	mutex_unlock(&di->charger_attached_mutex);

	return;

reschedule:
	queue_delayed_work(di->charger_wq,
			   &di->usb_charger_attached_work,
			   HZ);
}

static void ab8500_charger_ac_attached_work(struct work_struct *work)
{

	struct ab8500_charger *di = container_of(work,
						 struct ab8500_charger,
						 ac_charger_attached_work.work);
	int mainch = (MAIN_CH_STATUS2_MAINCHGDROP |
		      MAIN_CH_STATUS2_MAINCHARGERDETDBNC);
	int ret, i;
	u8 statval;

	for (i = 0; i < 10; i++) {
		ret = abx500_get_register_interruptible(di->dev,
							AB8500_CHARGER,
							AB8500_CH_STATUS2_REG,
							&statval);
		if (ret < 0) {
			dev_err(di->dev, "ab8500 read failed %d\n", __LINE__);
			goto reschedule;
		}

		if ((statval & mainch) != mainch)
			goto reschedule;

		msleep(CHARGER_STATUS_POLL);
	}

	ab8500_charger_ac_en(&di->ac_chg, 0, 0, 0);
	queue_work(di->charger_wq, &di->ac_work);

	mutex_lock(&di->charger_attached_mutex);
	mutex_unlock(&di->charger_attached_mutex);

	return;

reschedule:
	queue_delayed_work(di->charger_wq,
			   &di->ac_charger_attached_work,
			   HZ);
}

/**
 * ab8500_charger_detect_usb_type_work() - work to detect USB type
 * @work:	Pointer to the work_struct structure
 *
 * Detect the type of USB plugged
 */
static void ab8500_charger_detect_usb_type_work(struct work_struct *work)
{
	int ret;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, detect_usb_type_work);

	/*
	 * Since we can't be sure that the events are received
	 * synchronously, we have the check if is
	 * connected by reading the status register
	 */
	ret = ab8500_charger_detect_chargers(di, false);
	if (ret < 0)
		return;

	if (!(ret & USB_PW_CONN)) {
		dev_dbg(di->dev, "%s di->vbus_detected = false\n", __func__);
		di->vbus_detected = false;
		ab8500_charger_set_usb_connected(di, false);
		ab8500_power_supply_changed(di, di->usb_chg.psy);
	} else {
		dev_dbg(di->dev, "%s di->vbus_detected = true\n", __func__);
		di->vbus_detected = true;

		if (is_ab8500_1p1_or_earlier(di->parent)) {
			ret = ab8500_charger_detect_usb_type(di);
			if (!ret) {
				ab8500_charger_set_usb_connected(di, true);
				ab8500_power_supply_changed(di,
							    di->usb_chg.psy);
			}
		} else {
			/*
			 * For ABB cut2.0 and onwards we have an IRQ,
			 * USB_LINK_STATUS that will be triggered when the USB
			 * link status changes. The exception is USB connected
			 * during startup. Then we don't get a
			 * USB_LINK_STATUS IRQ
			 */
			if (di->vbus_detected_start) {
				di->vbus_detected_start = false;
				ret = ab8500_charger_detect_usb_type(di);
				if (!ret) {
					ab8500_charger_set_usb_connected(di,
						true);
					ab8500_power_supply_changed(di,
						di->usb_chg.psy);
				}
			}
		}
	}
}

/**
 * ab8500_charger_usb_link_attach_work() - work to detect USB type
 * @work:	pointer to the work_struct structure
 *
 * Detect the type of USB plugged
 */
static void ab8500_charger_usb_link_attach_work(struct work_struct *work)
{
	struct ab8500_charger *di =
		container_of(work, struct ab8500_charger, attach_work.work);
	int ret;

	/* Update maximum input current if USB enumeration is not detected */
	if (!di->usb.charger_online) {
		ret = ab8500_charger_set_vbus_in_curr(di,
					di->max_usb_in_curr.usb_type_max);
		if (ret)
			return;
	}

	ab8500_charger_set_usb_connected(di, true);
	ab8500_power_supply_changed(di, di->usb_chg.psy);
}

/**
 * ab8500_charger_usb_link_status_work() - work to detect USB type
 * @work:	pointer to the work_struct structure
 *
 * Detect the type of USB plugged
 */
static void ab8500_charger_usb_link_status_work(struct work_struct *work)
{
	int detected_chargers;
	int ret;
	u8 val;
	u8 link_status;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, usb_link_status_work);

	/*
	 * Since we can't be sure that the events are received
	 * synchronously, we have the check if  is
	 * connected by reading the status register
	 */
	detected_chargers = ab8500_charger_detect_chargers(di, false);
	if (detected_chargers < 0)
		return;

	/*
	 * Some chargers that breaks the USB spec is
	 * identified as invalid by AB8500 and it refuse
	 * to start the charging process. but by jumping
	 * thru a few hoops it can be forced to start.
	 */
	if (is_ab8500(di->parent))
		ret = abx500_get_register_interruptible(di->dev, AB8500_USB,
					AB8500_USB_LINE_STAT_REG, &val);
	else
		ret = abx500_get_register_interruptible(di->dev, AB8500_USB,
					AB8500_USB_LINK1_STAT_REG, &val);

	if (ret >= 0)
		dev_dbg(di->dev, "UsbLineStatus register = 0x%02x\n", val);
	else
		dev_dbg(di->dev, "Error reading USB link status\n");

	if (is_ab8500(di->parent))
		link_status = AB8500_USB_LINK_STATUS;
	else
		link_status = AB8505_USB_LINK_STATUS;

	if (detected_chargers & USB_PW_CONN) {
		if (((val & link_status) >> USB_LINK_STATUS_SHIFT) ==
				USB_STAT_NOT_VALID_LINK &&
				di->invalid_charger_detect_state == 0) {
			dev_dbg(di->dev,
					"Invalid charger detected, state= 0\n");
			/*Enable charger*/
			abx500_mask_and_set_register_interruptible(di->dev,
					AB8500_CHARGER, AB8500_USBCH_CTRL1_REG,
					USB_CH_ENA, USB_CH_ENA);
			/*Enable charger detection*/
			abx500_mask_and_set_register_interruptible(di->dev,
					AB8500_USB, AB8500_USB_LINE_CTRL2_REG,
					USB_CH_DET, USB_CH_DET);
			di->invalid_charger_detect_state = 1;
			/*exit and wait for new link status interrupt.*/
			return;

		}
		if (di->invalid_charger_detect_state == 1) {
			dev_dbg(di->dev,
					"Invalid charger detected, state= 1\n");
			/*Stop charger detection*/
			abx500_mask_and_set_register_interruptible(di->dev,
					AB8500_USB, AB8500_USB_LINE_CTRL2_REG,
					USB_CH_DET, 0x00);
			/*Check link status*/
			if (is_ab8500(di->parent))
				ret = abx500_get_register_interruptible(di->dev,
					AB8500_USB, AB8500_USB_LINE_STAT_REG,
					&val);
			else
				ret = abx500_get_register_interruptible(di->dev,
					AB8500_USB, AB8500_USB_LINK1_STAT_REG,
					&val);

			dev_dbg(di->dev, "USB link status= 0x%02x\n",
				(val & link_status) >> USB_LINK_STATUS_SHIFT);
			di->invalid_charger_detect_state = 2;
		}
	} else {
		di->invalid_charger_detect_state = 0;
	}

	if (!(detected_chargers & USB_PW_CONN)) {
		di->vbus_detected = false;
		ab8500_charger_set_usb_connected(di, false);
		ab8500_power_supply_changed(di, di->usb_chg.psy);
		return;
	}

	dev_dbg(di->dev,"%s di->vbus_detected = true\n",__func__);
	di->vbus_detected = true;
	ret = ab8500_charger_read_usb_type(di);
	if (ret) {
		if (ret == -ENXIO) {
			/* No valid charger type detected */
			ab8500_charger_set_usb_connected(di, false);
			ab8500_power_supply_changed(di, di->usb_chg.psy);
		}
		return;
	}

	if (di->usb_device_is_unrecognised) {
		dev_dbg(di->dev,
			"Potential Legacy Charger device. "
			"Delay work for %d msec for USB enum "
			"to finish",
			WAIT_ACA_RID_ENUMERATION);
		queue_delayed_work(di->charger_wq,
				   &di->attach_work,
				   msecs_to_jiffies(WAIT_ACA_RID_ENUMERATION));
	} else if (di->is_aca_rid == 1) {
		/* Only wait once */
		di->is_aca_rid++;
		dev_dbg(di->dev,
			"%s Wait %d msec for USB enum to finish",
			__func__, WAIT_ACA_RID_ENUMERATION);
		queue_delayed_work(di->charger_wq,
				   &di->attach_work,
				   msecs_to_jiffies(WAIT_ACA_RID_ENUMERATION));
	} else {
		queue_delayed_work(di->charger_wq,
				   &di->attach_work,
				   0);
	}
}

static void ab8500_charger_usb_state_changed_work(struct work_struct *work)
{
	int ret;
	unsigned long flags;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, usb_state_changed_work.work);

	if (!di->vbus_detected)	{
		dev_dbg(di->dev,
			"%s !di->vbus_detected\n",
			__func__);
		return;
	}

	spin_lock_irqsave(&di->usb_state.usb_lock, flags);
	di->usb_state.state = di->usb_state.state_tmp;
	di->usb_state.usb_current = di->usb_state.usb_current_tmp;
	spin_unlock_irqrestore(&di->usb_state.usb_lock, flags);

	dev_dbg(di->dev, "%s USB state: 0x%02x mA: %d\n",
		__func__, di->usb_state.state, di->usb_state.usb_current);

	switch (di->usb_state.state) {
	case AB8500_BM_USB_STATE_RESET_HS:
	case AB8500_BM_USB_STATE_RESET_FS:
	case AB8500_BM_USB_STATE_SUSPEND:
	case AB8500_BM_USB_STATE_MAX:
		ab8500_charger_set_usb_connected(di, false);
		ab8500_power_supply_changed(di, di->usb_chg.psy);
		break;

	case AB8500_BM_USB_STATE_RESUME:
		/*
		 * when suspend->resume there should be delay
		 * of 1sec for enabling charging
		 */
		msleep(1000);
		/* Intentional fall through */
	case AB8500_BM_USB_STATE_CONFIGURED:
		/*
		 * USB is configured, enable charging with the charging
		 * input current obtained from USB driver
		 */
		if (!ab8500_charger_get_usb_cur(di)) {
			/* Update maximum input current */
			ret = ab8500_charger_set_vbus_in_curr(di,
					di->max_usb_in_curr.usb_type_max);
			if (ret)
				return;

			ab8500_charger_set_usb_connected(di, true);
			ab8500_power_supply_changed(di, di->usb_chg.psy);
		}
		break;

	default:
		break;
	};
}

/**
 * ab8500_charger_check_usbchargernotok_work() - check USB chg not ok status
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the USB charger Not OK status
 */
static void ab8500_charger_check_usbchargernotok_work(struct work_struct *work)
{
	int ret;
	u8 reg_value;
	bool prev_status;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, check_usbchgnotok_work.work);

	/* Check if the status bit for usbchargernotok is still active */
	ret = abx500_get_register_interruptible(di->dev,
		AB8500_CHARGER, AB8500_CH_USBCH_STAT2_REG, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return;
	}
	prev_status = di->flags.usbchargernotok;

	if (reg_value & VBUS_CH_NOK) {
		di->flags.usbchargernotok = true;
		/* Check again in 1sec */
		queue_delayed_work(di->charger_wq,
			&di->check_usbchgnotok_work, HZ);
	} else {
		di->flags.usbchargernotok = false;
		di->flags.vbus_collapse = false;
	}

	if (prev_status != di->flags.usbchargernotok)
		ab8500_power_supply_changed(di, di->usb_chg.psy);
}

/**
 * ab8500_charger_check_main_thermal_prot_work() - check main thermal status
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the Main thermal prot status
 */
static void ab8500_charger_check_main_thermal_prot_work(
	struct work_struct *work)
{
	int ret;
	u8 reg_value;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, check_main_thermal_prot_work);

	/* Check if the status bit for main_thermal_prot is still active */
	ret = abx500_get_register_interruptible(di->dev,
		AB8500_CHARGER, AB8500_CH_STATUS2_REG, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return;
	}
	if (reg_value & MAIN_CH_TH_PROT)
		di->flags.main_thermal_prot = true;
	else
		di->flags.main_thermal_prot = false;

	ab8500_power_supply_changed(di, di->ac_chg.psy);
}

/**
 * ab8500_charger_check_usb_thermal_prot_work() - check usb thermal status
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the USB thermal prot status
 */
static void ab8500_charger_check_usb_thermal_prot_work(
	struct work_struct *work)
{
	int ret;
	u8 reg_value;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, check_usb_thermal_prot_work);

	/* Check if the status bit for usb_thermal_prot is still active */
	ret = abx500_get_register_interruptible(di->dev,
		AB8500_CHARGER, AB8500_CH_USBCH_STAT2_REG, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return;
	}
	if (reg_value & USB_CH_TH_PROT)
		di->flags.usb_thermal_prot = true;
	else
		di->flags.usb_thermal_prot = false;

	ab8500_power_supply_changed(di, di->usb_chg.psy);
}

/**
 * ab8500_charger_mainchunplugdet_handler() - main charger unplugged
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_mainchunplugdet_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev, "Main charger unplugged\n");
	queue_work(di->charger_wq, &di->ac_work);

	cancel_delayed_work_sync(&di->ac_charger_attached_work);
	mutex_lock(&di->charger_attached_mutex);
	mutex_unlock(&di->charger_attached_mutex);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_mainchplugdet_handler() - main charger plugged
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_mainchplugdet_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev, "Main charger plugged\n");
	queue_work(di->charger_wq, &di->ac_work);

	mutex_lock(&di->charger_attached_mutex);
	mutex_unlock(&di->charger_attached_mutex);

	if (is_ab8500(di->parent))
		queue_delayed_work(di->charger_wq,
			   &di->ac_charger_attached_work,
			   HZ);
	return IRQ_HANDLED;
}

/**
 * ab8500_charger_mainextchnotok_handler() - main charger not ok
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_mainextchnotok_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev, "Main charger not ok\n");
	di->flags.mainextchnotok = true;
	ab8500_power_supply_changed(di, di->ac_chg.psy);

	/* Schedule a new HW failure check */
	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_mainchthprotr_handler() - Die temp is above main charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_mainchthprotr_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev,
		"Die temp above Main charger thermal protection threshold\n");
	queue_work(di->charger_wq, &di->check_main_thermal_prot_work);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_mainchthprotf_handler() - Die temp is below main charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_mainchthprotf_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev,
		"Die temp ok for Main charger thermal protection threshold\n");
	queue_work(di->charger_wq, &di->check_main_thermal_prot_work);

	return IRQ_HANDLED;
}

static void ab8500_charger_vbus_drop_end_work(struct work_struct *work)
{
	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, vbus_drop_end_work.work);
	int ret, curr;
	u8 reg_value;

	di->flags.vbus_drop_end = false;

	/* Reset the drop counter */
	abx500_set_register_interruptible(di->dev,
				  AB8500_CHARGER, AB8500_CHARGER_CTRL, 0x01);

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CH_USBCH_STAT2_REG, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s read failed\n", __func__);
		return;
	}

	curr = di->bm->chg_input_curr[
		reg_value >> AUTO_VBUS_IN_CURR_LIM_SHIFT];

	if (di->max_usb_in_curr.calculated_max != curr) {
		/* USB source is collapsing */
		di->max_usb_in_curr.calculated_max = curr;
		dev_dbg(di->dev,
			 "VBUS input current limiting to %d mA\n",
			 di->max_usb_in_curr.calculated_max);
	} else {
		/*
		 * USB source can not give more than this amount.
		 * Taking more will collapse the source.
		 */
		di->max_usb_in_curr.set_max =
			di->max_usb_in_curr.calculated_max;
		dev_dbg(di->dev,
			 "VBUS input current limited to %d mA\n",
			 di->max_usb_in_curr.set_max);
	}

	if (di->usb.charger_connected)
		ab8500_charger_set_vbus_in_curr(di,
					di->max_usb_in_curr.usb_type_max);
}

/**
 * ab8500_charger_vbusdetf_handler() - VBUS falling detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_vbusdetf_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	di->vbus_detected = false;
	dev_dbg(di->dev, "VBUS falling detected\n");
	queue_work(di->charger_wq, &di->detect_usb_type_work);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_vbusdetr_handler() - VBUS rising detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_vbusdetr_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	di->vbus_detected = true;
	dev_dbg(di->dev, "VBUS rising detected\n");

	queue_work(di->charger_wq, &di->detect_usb_type_work);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_usblinkstatus_handler() - USB link status has changed
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_usblinkstatus_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev, "USB link status changed\n");

	queue_work(di->charger_wq, &di->usb_link_status_work);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_usbchthprotr_handler() - Die temp is above usb charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_usbchthprotr_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev,
		"Die temp above USB charger thermal protection threshold\n");
	queue_work(di->charger_wq, &di->check_usb_thermal_prot_work);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_usbchthprotf_handler() - Die temp is below usb charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_usbchthprotf_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev,
		"Die temp ok for USB charger thermal protection threshold\n");
	queue_work(di->charger_wq, &di->check_usb_thermal_prot_work);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_usbchargernotokr_handler() - USB charger not ok detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_usbchargernotokr_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev, "Not allowed USB charger detected\n");
	queue_delayed_work(di->charger_wq, &di->check_usbchgnotok_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_chwdexp_handler() - Charger watchdog expired
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_chwdexp_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev, "Charger watchdog expired\n");

	/*
	 * The charger that was online when the watchdog expired
	 * needs to be restarted for charging to start again
	 */
	if (di->ac.charger_online) {
		di->ac.wd_expired = true;
		ab8500_power_supply_changed(di, di->ac_chg.psy);
	}
	if (di->usb.charger_online) {
		di->usb.wd_expired = true;
		ab8500_power_supply_changed(di, di->usb_chg.psy);
	}

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_vbuschdropend_handler() - VBUS drop removed
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_vbuschdropend_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev, "VBUS charger drop ended\n");
	di->flags.vbus_drop_end = true;

	/*
	 * VBUS might have dropped due to bad connection.
	 * Schedule a new input limit set to the value SW requests.
	 */
	queue_delayed_work(di->charger_wq, &di->vbus_drop_end_work,
			   round_jiffies(VBUS_IN_CURR_LIM_RETRY_SET_TIME * HZ));

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_vbusovv_handler() - VBUS overvoltage detected
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_charger structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_charger_vbusovv_handler(int irq, void *_di)
{
	struct ab8500_charger *di = _di;

	dev_dbg(di->dev, "VBUS overvoltage detected\n");
	di->flags.vbus_ovv = true;
	ab8500_power_supply_changed(di, di->usb_chg.psy);

	/* Schedule a new HW failure check */
	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_charger_ac_get_property() - get the ac/mains properties
 * @psy:       pointer to the power_supply structure
 * @psp:       pointer to the power_supply_property structure
 * @val:       pointer to the power_supply_propval union
 *
 * This function gets called when an application tries to get the ac/mains
 * properties by reading the sysfs files.
 * AC/Mains properties are online, present and voltage.
 * online:     ac/mains charging is in progress or not
 * present:    presence of the ac/mains
 * voltage:    AC/Mains voltage
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab8500_charger *di;
	int ret;

	di = to_ab8500_charger_ac_device_info(psy_to_ux500_charger(psy));

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		if (di->flags.mainextchnotok)
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		else if (di->ac.wd_expired || di->usb.wd_expired)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else if (di->flags.main_thermal_prot)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = di->ac.charger_online;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = di->ac.charger_connected;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = ab8500_charger_get_ac_voltage(di);
		if (ret >= 0)
			di->ac.charger_voltage = ret;
		/* On error, use previous value */
		val->intval = di->ac.charger_voltage * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		/*
		 * This property is used to indicate when CV mode is entered
		 * for the AC charger
		 */
		di->ac.cv_active = ab8500_charger_ac_cv(di);
		val->intval = di->ac.cv_active;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = ab8500_charger_get_ac_current(di);
		if (ret >= 0)
			di->ac.charger_current = ret;
		val->intval = di->ac.charger_current * 1000;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * ab8500_charger_usb_get_property() - get the usb properties
 * @psy:        pointer to the power_supply structure
 * @psp:        pointer to the power_supply_property structure
 * @val:        pointer to the power_supply_propval union
 *
 * This function gets called when an application tries to get the usb
 * properties by reading the sysfs files.
 * USB properties are online, present and voltage.
 * online:     usb charging is in progress or not
 * present:    presence of the usb
 * voltage:    vbus voltage
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_charger_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab8500_charger *di;
	int ret;

	di = to_ab8500_charger_usb_device_info(psy_to_ux500_charger(psy));

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		if (di->flags.usbchargernotok)
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		else if (di->ac.wd_expired || di->usb.wd_expired)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else if (di->flags.usb_thermal_prot)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (di->flags.vbus_ovv)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = di->usb.charger_online;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = di->usb.charger_connected;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = ab8500_charger_get_vbus_voltage(di);
		if (ret >= 0)
			di->usb.charger_voltage = ret;
		val->intval = di->usb.charger_voltage * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		/*
		 * This property is used to indicate when CV mode is entered
		 * for the USB charger
		 */
		di->usb.cv_active = ab8500_charger_usb_cv(di);
		val->intval = di->usb.cv_active;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = ab8500_charger_get_usb_current(di);
		if (ret >= 0)
			di->usb.charger_current = ret;
		val->intval = di->usb.charger_current * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		/*
		 * This property is used to indicate when VBUS has collapsed
		 * due to too high output current from the USB charger
		 */
		if (di->flags.vbus_collapse)
			val->intval = 1;
		else
			val->intval = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * ab8500_charger_init_hw_registers() - Set up charger related registers
 * @di:		pointer to the ab8500_charger structure
 *
 * Set up charger OVV, watchdog and maximum voltage registers as well as
 * charging of the backup battery
 */
static int ab8500_charger_init_hw_registers(struct ab8500_charger *di)
{
	int ret = 0;
	u8 bup_vch_range = 0, vbup33_vrtcn = 0;

	/* Setup maximum charger current and voltage for ABB cut2.0 */
	if (!is_ab8500_1p1_or_earlier(di->parent)) {
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_CH_VOLT_LVL_MAX_REG, CH_VOL_LVL_4P6);
		if (ret) {
			dev_err(di->dev,
				"failed to set CH_VOLT_LVL_MAX_REG\n");
			goto out;
		}

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_CH_OPT_CRNTLVL_MAX_REG,
			CH_OP_CUR_LVL_1P6);
		if (ret) {
			dev_err(di->dev,
				"failed to set CH_OPT_CRNTLVL_MAX_REG\n");
			goto out;
		}
	}

	if (is_ab8505_2p0(di->parent))
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_USBCH_CTRL2_REG,
			VBUS_AUTO_IN_CURR_LIM_ENA,
			VBUS_AUTO_IN_CURR_LIM_ENA);
	else
		/*
		 * VBUS OVV set to 6.3V and enable automatic current limitation
		 */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_USBCH_CTRL2_REG,
			VBUS_OVV_SELECT_6P3V | VBUS_AUTO_IN_CURR_LIM_ENA);
	if (ret) {
		dev_err(di->dev,
			"failed to set automatic current limitation\n");
		goto out;
	}

	/* Enable main watchdog in OTP */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_OTP_EMUL, AB8500_OTP_CONF_15, OTP_ENABLE_WD);
	if (ret) {
		dev_err(di->dev, "failed to enable main WD in OTP\n");
		goto out;
	}

	/* Enable main watchdog */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WDOG_CTRL_REG, MAIN_WDOG_ENA);
	if (ret) {
		dev_err(di->dev, "failed to enable main watchdog\n");
		goto out;
	}

	/*
	 * Due to internal synchronisation, Enable and Kick watchdog bits
	 * cannot be enabled in a single write.
	 * A minimum delay of 2*32 kHz period (62.5µs) must be inserted
	 * between writing Enable then Kick bits.
	 */
	udelay(63);

	/* Kick main watchdog */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WDOG_CTRL_REG,
		(MAIN_WDOG_ENA | MAIN_WDOG_KICK));
	if (ret) {
		dev_err(di->dev, "failed to kick main watchdog\n");
		goto out;
	}

	/* Disable main watchdog */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WDOG_CTRL_REG, MAIN_WDOG_DIS);
	if (ret) {
		dev_err(di->dev, "failed to disable main watchdog\n");
		goto out;
	}

	/* Set watchdog timeout */
	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CH_WD_TIMER_REG, WD_TIMER);
	if (ret) {
		dev_err(di->dev, "failed to set charger watchdog timeout\n");
		goto out;
	}

	ret = ab8500_charger_led_en(di, false);
	if (ret < 0) {
		dev_err(di->dev, "failed to disable LED\n");
		goto out;
	}

	/* Backup battery voltage and current */
	if (di->bm->bkup_bat_v > BUP_VCH_SEL_3P1V)
		bup_vch_range = BUP_VCH_RANGE;
	if (di->bm->bkup_bat_v == BUP_VCH_SEL_3P3V)
		vbup33_vrtcn = VBUP33_VRTCN;

	ret = abx500_set_register_interruptible(di->dev,
		AB8500_RTC,
		AB8500_RTC_BACKUP_CHG_REG,
		(di->bm->bkup_bat_v & 0x3) | di->bm->bkup_bat_i);
	if (ret) {
		dev_err(di->dev, "failed to setup backup battery charging\n");
		goto out;
	}

	/* Enable backup battery charging */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_RTC, AB8500_RTC_CTRL_REG,
		RTC_BUP_CH_ENA, RTC_BUP_CH_ENA);
	if (ret < 0) {
		dev_err(di->dev, "%s mask and set failed\n", __func__);
		goto out;
	}

out:
	return ret;
}

/*
 * ab8500 charger driver interrupts and their respective isr
 */
static struct ab8500_charger_interrupts ab8500_charger_irq[] = {
	{"MAIN_CH_UNPLUG_DET", ab8500_charger_mainchunplugdet_handler},
	{"MAIN_CHARGE_PLUG_DET", ab8500_charger_mainchplugdet_handler},
	{"MAIN_EXT_CH_NOT_OK", ab8500_charger_mainextchnotok_handler},
	{"MAIN_CH_TH_PROT_R", ab8500_charger_mainchthprotr_handler},
	{"MAIN_CH_TH_PROT_F", ab8500_charger_mainchthprotf_handler},
	{"VBUS_DET_F", ab8500_charger_vbusdetf_handler},
	{"VBUS_DET_R", ab8500_charger_vbusdetr_handler},
	{"USB_LINK_STATUS", ab8500_charger_usblinkstatus_handler},
	{"USB_CH_TH_PROT_R", ab8500_charger_usbchthprotr_handler},
	{"USB_CH_TH_PROT_F", ab8500_charger_usbchthprotf_handler},
	{"USB_CHARGER_NOT_OKR", ab8500_charger_usbchargernotokr_handler},
	{"VBUS_OVV", ab8500_charger_vbusovv_handler},
	{"CH_WD_EXP", ab8500_charger_chwdexp_handler},
	{"VBUS_CH_DROP_END", ab8500_charger_vbuschdropend_handler},
};

static int ab8500_charger_usb_notifier_call(struct notifier_block *nb,
		unsigned long event, void *power)
{
	struct ab8500_charger *di =
		container_of(nb, struct ab8500_charger, nb);
	enum ab8500_usb_state bm_usb_state;
	unsigned mA = *((unsigned *)power);

	if (!di)
		return NOTIFY_DONE;

	if (event != USB_EVENT_VBUS) {
		dev_dbg(di->dev, "not a standard host, returning\n");
		return NOTIFY_DONE;
	}

	/* TODO: State is fabricate  here. See if charger really needs USB
	 * state or if mA is enough
	 */
	if ((di->usb_state.usb_current == 2) && (mA > 2))
		bm_usb_state = AB8500_BM_USB_STATE_RESUME;
	else if (mA == 0)
		bm_usb_state = AB8500_BM_USB_STATE_RESET_HS;
	else if (mA == 2)
		bm_usb_state = AB8500_BM_USB_STATE_SUSPEND;
	else if (mA >= 8) /* 8, 100, 500 */
		bm_usb_state = AB8500_BM_USB_STATE_CONFIGURED;
	else /* Should never occur */
		bm_usb_state = AB8500_BM_USB_STATE_RESET_FS;

	dev_dbg(di->dev, "%s usb_state: 0x%02x mA: %d\n",
		__func__, bm_usb_state, mA);

	spin_lock(&di->usb_state.usb_lock);
	di->usb_state.state_tmp = bm_usb_state;
	di->usb_state.usb_current_tmp = mA;
	spin_unlock(&di->usb_state.usb_lock);

	/*
	 * wait for some time until you get updates from the usb stack
	 * and negotiations are completed
	 */
	queue_delayed_work(di->charger_wq, &di->usb_state_changed_work, HZ/2);

	return NOTIFY_OK;
}

#if defined(CONFIG_PM)
static int ab8500_charger_resume(struct platform_device *pdev)
{
	int ret;
	struct ab8500_charger *di = platform_get_drvdata(pdev);

	/*
	 * For ABB revision 1.0 and 1.1 there is a bug in the watchdog
	 * logic. That means we have to continously kick the charger
	 * watchdog even when no charger is connected. This is only
	 * valid once the AC charger has been enabled. This is
	 * a bug that is not handled by the algorithm and the
	 * watchdog have to be kicked by the charger driver
	 * when the AC charger is disabled
	 */
	if (di->ac_conn && is_ab8500_1p1_or_earlier(di->parent)) {
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CHARG_WD_CTRL, CHARG_WD_KICK);
		if (ret)
			dev_err(di->dev, "Failed to kick WD!\n");

		/* If not already pending start a new timer */
		queue_delayed_work(di->charger_wq, &di->kick_wd_work,
				   round_jiffies(WD_KICK_INTERVAL));
	}

	/* If we still have a HW failure, schedule a new check */
	if (di->flags.mainextchnotok || di->flags.vbus_ovv) {
		queue_delayed_work(di->charger_wq,
			&di->check_hw_failure_work, 0);
	}

	if (di->flags.vbus_drop_end)
		queue_delayed_work(di->charger_wq, &di->vbus_drop_end_work, 0);

	return 0;
}

static int ab8500_charger_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ab8500_charger *di = platform_get_drvdata(pdev);

	/* Cancel any pending jobs */
	cancel_delayed_work(&di->check_hw_failure_work);
	cancel_delayed_work(&di->vbus_drop_end_work);

	flush_delayed_work(&di->attach_work);
	flush_delayed_work(&di->usb_charger_attached_work);
	flush_delayed_work(&di->ac_charger_attached_work);
	flush_delayed_work(&di->check_usbchgnotok_work);
	flush_delayed_work(&di->check_vbat_work);
	flush_delayed_work(&di->kick_wd_work);

	flush_work(&di->usb_link_status_work);
	flush_work(&di->ac_work);
	flush_work(&di->detect_usb_type_work);

	if (atomic_read(&di->current_stepping_sessions))
		return -EAGAIN;

	return 0;
}
#else
#define ab8500_charger_suspend      NULL
#define ab8500_charger_resume       NULL
#endif

static struct notifier_block charger_nb = {
	.notifier_call = ab8500_external_charger_prepare,
};

static int ab8500_charger_remove(struct platform_device *pdev)
{
	struct ab8500_charger *di = platform_get_drvdata(pdev);
	int i, irq, ret;

	/* Disable AC charging */
	ab8500_charger_ac_en(&di->ac_chg, false, 0, 0);

	/* Disable USB charging */
	ab8500_charger_usb_en(&di->usb_chg, false, 0, 0);

	/* Disable interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_charger_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab8500_charger_irq[i].name);
		free_irq(irq, di);
	}

	/* Backup battery voltage and current disable */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_RTC, AB8500_RTC_CTRL_REG, RTC_BUP_CH_ENA, 0);
	if (ret < 0)
		dev_err(di->dev, "%s mask and set failed\n", __func__);

	usb_unregister_notifier(di->usb_phy, &di->nb);
	usb_put_phy(di->usb_phy);

	/* Delete the work queue */
	destroy_workqueue(di->charger_wq);

	/* Unregister external charger enable notifier */
	if (!di->ac_chg.enabled)
		blocking_notifier_chain_unregister(
			&charger_notifier_list, &charger_nb);

	flush_scheduled_work();
	if (di->usb_chg.enabled)
		power_supply_unregister(di->usb_chg.psy);

	if (di->ac_chg.enabled && !di->ac_chg.external)
		power_supply_unregister(di->ac_chg.psy);

	return 0;
}

static char *supply_interface[] = {
	"ab8500_chargalg",
	"ab8500_fg",
	"ab8500_btemp",
};

static const struct power_supply_desc ab8500_ac_chg_desc = {
	.name		= "ab8500_ac",
	.type		= POWER_SUPPLY_TYPE_MAINS,
	.properties	= ab8500_charger_ac_props,
	.num_properties	= ARRAY_SIZE(ab8500_charger_ac_props),
	.get_property	= ab8500_charger_ac_get_property,
};

static const struct power_supply_desc ab8500_usb_chg_desc = {
	.name		= "ab8500_usb",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= ab8500_charger_usb_props,
	.num_properties	= ARRAY_SIZE(ab8500_charger_usb_props),
	.get_property	= ab8500_charger_usb_get_property,
};

static int ab8500_charger_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct abx500_bm_data *plat = pdev->dev.platform_data;
	struct power_supply_config ac_psy_cfg = {}, usb_psy_cfg = {};
	struct ab8500_charger *di;
	int irq, i, charger_status, ret = 0, ch_stat;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&pdev->dev, "%s no mem for ab8500_charger\n", __func__);
		return -ENOMEM;
	}

	if (!plat) {
		dev_err(&pdev->dev, "no battery management data supplied\n");
		return -EINVAL;
	}
	di->bm = plat;

	if (np) {
		ret = ab8500_bm_of_probe(&pdev->dev, np, di->bm);
		if (ret) {
			dev_err(&pdev->dev, "failed to get battery information\n");
			return ret;
		}
		di->autopower_cfg = of_property_read_bool(np, "autopower_cfg");
	} else
		di->autopower_cfg = false;

	/* get parent data */
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);
	di->gpadc = ab8500_gpadc_get("ab8500-gpadc.0");

	/* initialize lock */
	spin_lock_init(&di->usb_state.usb_lock);
	mutex_init(&di->usb_ipt_crnt_lock);

	di->autopower = false;
	di->invalid_charger_detect_state = 0;

	/* AC and USB supply config */
	ac_psy_cfg.supplied_to = supply_interface;
	ac_psy_cfg.num_supplicants = ARRAY_SIZE(supply_interface);
	ac_psy_cfg.drv_data = &di->ac_chg;
	usb_psy_cfg.supplied_to = supply_interface;
	usb_psy_cfg.num_supplicants = ARRAY_SIZE(supply_interface);
	usb_psy_cfg.drv_data = &di->usb_chg;

	/* AC supply */
	/* ux500_charger sub-class */
	di->ac_chg.ops.enable = &ab8500_charger_ac_en;
	di->ac_chg.ops.check_enable = &ab8500_charger_ac_check_enable;
	di->ac_chg.ops.kick_wd = &ab8500_charger_watchdog_kick;
	di->ac_chg.ops.update_curr = &ab8500_charger_update_charger_current;
	di->ac_chg.max_out_volt = ab8500_charger_voltage_map[
		ARRAY_SIZE(ab8500_charger_voltage_map) - 1];
	di->ac_chg.max_out_curr =
		di->bm->chg_output_curr[di->bm->n_chg_out_curr - 1];
	di->ac_chg.wdt_refresh = CHG_WD_INTERVAL;
	di->ac_chg.enabled = di->bm->ac_enabled;
	di->ac_chg.external = false;

	/*notifier for external charger enabling*/
	if (!di->ac_chg.enabled)
		blocking_notifier_chain_register(
			&charger_notifier_list, &charger_nb);

	/* USB supply */
	/* ux500_charger sub-class */
	di->usb_chg.ops.enable = &ab8500_charger_usb_en;
	di->usb_chg.ops.check_enable = &ab8500_charger_usb_check_enable;
	di->usb_chg.ops.kick_wd = &ab8500_charger_watchdog_kick;
	di->usb_chg.ops.update_curr = &ab8500_charger_update_charger_current;
	di->usb_chg.max_out_volt = ab8500_charger_voltage_map[
		ARRAY_SIZE(ab8500_charger_voltage_map) - 1];
	di->usb_chg.max_out_curr =
		di->bm->chg_output_curr[di->bm->n_chg_out_curr - 1];
	di->usb_chg.wdt_refresh = CHG_WD_INTERVAL;
	di->usb_chg.enabled = di->bm->usb_enabled;
	di->usb_chg.external = false;
	di->usb_state.usb_current = -1;

	/* Create a work queue for the charger */
	di->charger_wq = alloc_ordered_workqueue("ab8500_charger_wq",
						 WQ_MEM_RECLAIM);
	if (di->charger_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		return -ENOMEM;
	}

	mutex_init(&di->charger_attached_mutex);

	/* Init work for HW failure check */
	INIT_DEFERRABLE_WORK(&di->check_hw_failure_work,
		ab8500_charger_check_hw_failure_work);
	INIT_DEFERRABLE_WORK(&di->check_usbchgnotok_work,
		ab8500_charger_check_usbchargernotok_work);

	INIT_DELAYED_WORK(&di->ac_charger_attached_work,
			  ab8500_charger_ac_attached_work);
	INIT_DELAYED_WORK(&di->usb_charger_attached_work,
			  ab8500_charger_usb_attached_work);

	/*
	 * For ABB revision 1.0 and 1.1 there is a bug in the watchdog
	 * logic. That means we have to continously kick the charger
	 * watchdog even when no charger is connected. This is only
	 * valid once the AC charger has been enabled. This is
	 * a bug that is not handled by the algorithm and the
	 * watchdog have to be kicked by the charger driver
	 * when the AC charger is disabled
	 */
	INIT_DEFERRABLE_WORK(&di->kick_wd_work,
		ab8500_charger_kick_watchdog_work);

	INIT_DEFERRABLE_WORK(&di->check_vbat_work,
		ab8500_charger_check_vbat_work);

	INIT_DELAYED_WORK(&di->attach_work,
		ab8500_charger_usb_link_attach_work);

	INIT_DELAYED_WORK(&di->usb_state_changed_work,
		ab8500_charger_usb_state_changed_work);

	INIT_DELAYED_WORK(&di->vbus_drop_end_work,
		ab8500_charger_vbus_drop_end_work);

	/* Init work for charger detection */
	INIT_WORK(&di->usb_link_status_work,
		ab8500_charger_usb_link_status_work);
	INIT_WORK(&di->ac_work, ab8500_charger_ac_work);
	INIT_WORK(&di->detect_usb_type_work,
		ab8500_charger_detect_usb_type_work);

	/* Init work for checking HW status */
	INIT_WORK(&di->check_main_thermal_prot_work,
		ab8500_charger_check_main_thermal_prot_work);
	INIT_WORK(&di->check_usb_thermal_prot_work,
		ab8500_charger_check_usb_thermal_prot_work);

	/*
	 * VDD ADC supply needs to be enabled from this driver when there
	 * is a charger connected to avoid erroneous BTEMP_HIGH/LOW
	 * interrupts during charging
	 */
	di->regu = devm_regulator_get(di->dev, "vddadc");
	if (IS_ERR(di->regu)) {
		ret = PTR_ERR(di->regu);
		dev_err(di->dev, "failed to get vddadc regulator\n");
		goto free_charger_wq;
	}


	/* Initialize OVV, and other registers */
	ret = ab8500_charger_init_hw_registers(di);
	if (ret) {
		dev_err(di->dev, "failed to initialize ABB registers\n");
		goto free_charger_wq;
	}

	/* Register AC charger class */
	if (di->ac_chg.enabled) {
		di->ac_chg.psy = power_supply_register(di->dev,
						       &ab8500_ac_chg_desc,
						       &ac_psy_cfg);
		if (IS_ERR(di->ac_chg.psy)) {
			dev_err(di->dev, "failed to register AC charger\n");
			ret = PTR_ERR(di->ac_chg.psy);
			goto free_charger_wq;
		}
	}

	/* Register USB charger class */
	if (di->usb_chg.enabled) {
		di->usb_chg.psy = power_supply_register(di->dev,
							&ab8500_usb_chg_desc,
							&usb_psy_cfg);
		if (IS_ERR(di->usb_chg.psy)) {
			dev_err(di->dev, "failed to register USB charger\n");
			ret = PTR_ERR(di->usb_chg.psy);
			goto free_ac;
		}
	}

	di->usb_phy = usb_get_phy(USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(di->usb_phy)) {
		dev_err(di->dev, "failed to get usb transceiver\n");
		ret = -EINVAL;
		goto free_usb;
	}
	di->nb.notifier_call = ab8500_charger_usb_notifier_call;
	ret = usb_register_notifier(di->usb_phy, &di->nb);
	if (ret) {
		dev_err(di->dev, "failed to register usb notifier\n");
		goto put_usb_phy;
	}

	/* Identify the connected charger types during startup */
	charger_status = ab8500_charger_detect_chargers(di, true);
	if (charger_status & AC_PW_CONN) {
		di->ac.charger_connected = 1;
		di->ac_conn = true;
		ab8500_power_supply_changed(di, di->ac_chg.psy);
		sysfs_notify(&di->ac_chg.psy->dev.kobj, NULL, "present");
	}

	if (charger_status & USB_PW_CONN) {
		di->vbus_detected = true;
		di->vbus_detected_start = true;
		queue_work(di->charger_wq,
			&di->detect_usb_type_work);
	}

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_charger_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab8500_charger_irq[i].name);
		ret = request_threaded_irq(irq, NULL, ab8500_charger_irq[i].isr,
			IRQF_SHARED | IRQF_NO_SUSPEND,
			ab8500_charger_irq[i].name, di);

		if (ret != 0) {
			dev_err(di->dev, "failed to request %s IRQ %d: %d\n"
				, ab8500_charger_irq[i].name, irq, ret);
			goto free_irq;
		}
		dev_dbg(di->dev, "Requested %s IRQ %d: %d\n",
			ab8500_charger_irq[i].name, irq, ret);
	}

	platform_set_drvdata(pdev, di);

	mutex_lock(&di->charger_attached_mutex);

	ch_stat = ab8500_charger_detect_chargers(di, false);

	if ((ch_stat & AC_PW_CONN) == AC_PW_CONN) {
		if (is_ab8500(di->parent))
			queue_delayed_work(di->charger_wq,
					   &di->ac_charger_attached_work,
					   HZ);
	}
	if ((ch_stat & USB_PW_CONN) == USB_PW_CONN) {
		if (is_ab8500(di->parent))
			queue_delayed_work(di->charger_wq,
					   &di->usb_charger_attached_work,
					   HZ);
	}

	mutex_unlock(&di->charger_attached_mutex);

	return ret;

free_irq:
	usb_unregister_notifier(di->usb_phy, &di->nb);

	/* We also have to free all successfully registered irqs */
	for (i = i - 1; i >= 0; i--) {
		irq = platform_get_irq_byname(pdev, ab8500_charger_irq[i].name);
		free_irq(irq, di);
	}
put_usb_phy:
	usb_put_phy(di->usb_phy);
free_usb:
	if (di->usb_chg.enabled)
		power_supply_unregister(di->usb_chg.psy);
free_ac:
	if (di->ac_chg.enabled)
		power_supply_unregister(di->ac_chg.psy);
free_charger_wq:
	destroy_workqueue(di->charger_wq);
	return ret;
}

static const struct of_device_id ab8500_charger_match[] = {
	{ .compatible = "stericsson,ab8500-charger", },
	{ },
};

static struct platform_driver ab8500_charger_driver = {
	.probe = ab8500_charger_probe,
	.remove = ab8500_charger_remove,
	.suspend = ab8500_charger_suspend,
	.resume = ab8500_charger_resume,
	.driver = {
		.name = "ab8500-charger",
		.of_match_table = ab8500_charger_match,
	},
};

static int __init ab8500_charger_init(void)
{
	return platform_driver_register(&ab8500_charger_driver);
}

static void __exit ab8500_charger_exit(void)
{
	platform_driver_unregister(&ab8500_charger_driver);
}

subsys_initcall_sync(ab8500_charger_init);
module_exit(ab8500_charger_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski, Arun R Murthy");
MODULE_ALIAS("platform:ab8500-charger");
MODULE_DESCRIPTION("AB8500 charger management driver");
