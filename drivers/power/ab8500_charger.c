/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Charger driver for AB8500
 *
 * License Terms: GNU General Public License v2
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

#define MAIN_CH_INPUT_CURR_SHIFT	4
#define VBUS_IN_CURR_LIM_SHIFT		4

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

/* UsbLineStatus register bit masks */
#define AB8500_USB_LINK_STATUS		0x78
#define AB8500_STD_HOST_SUSP		0x18

/* Watchdog timeout constant */
#define WD_TIMER			0x30 /* 4min */
#define WD_KICK_INTERVAL		(60 * HZ)

/* Lowest charger voltage is 3.39V -> 0x4E */
#define LOW_VOLT_REG			0x4E

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
};

struct ab8500_charger_event_flags {
	bool mainextchnotok;
	bool main_thermal_prot;
	bool usb_thermal_prot;
	bool vbus_ovv;
	bool usbchargernotok;
	bool chgwdexp;
	bool vbus_collapse;
};

struct ab8500_charger_usb_state {
	bool usb_changed;
	int usb_current;
	enum ab8500_usb_state state;
	spinlock_t usb_lock;
};

/**
 * struct ab8500_charger - ab8500 Charger device information
 * @dev:		Pointer to the structure device
 * @max_usb_in_curr:	Max USB charger input current
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
 * @autopower		Indicate if we should have automatic pwron after pwrloss
 * @autopower_cfg	platform specific power config support for "pwron after pwrloss"
 * @parent:		Pointer to the struct ab8500
 * @gpadc:		Pointer to the struct gpadc
 * @bat:		Pointer to the abx500_bm platform data
 * @flags:		Structure for information about events triggered
 * @usb_state:		Structure for usb stack information
 * @ac_chg:		AC charger power supply
 * @usb_chg:		USB charger power supply
 * @ac:			Structure that holds the AC charger properties
 * @usb:		Structure that holds the USB charger properties
 * @regu:		Pointer to the struct regulator
 * @charger_wq:		Work queue for the IRQs and checking HW state
 * @check_vbat_work	Work for checking vbat threshold to adjust vbus current
 * @check_hw_failure_work:	Work for checking HW state
 * @check_usbchgnotok_work:	Work for checking USB charger not ok status
 * @kick_wd_work:		Work for kicking the charger watchdog in case
 *				of ABB rev 1.* due to the watchog logic bug
 * @ac_work:			Work for checking AC charger connection
 * @detect_usb_type_work:	Work for detecting the USB type connected
 * @usb_link_status_work:	Work for checking the new USB link status
 * @usb_state_changed_work:	Work for checking USB state
 * @check_main_thermal_prot_work:
 *				Work for checking Main thermal status
 * @check_usb_thermal_prot_work:
 *				Work for checking USB thermal status
 */
struct ab8500_charger {
	struct device *dev;
	int max_usb_in_curr;
	bool vbus_detected;
	bool vbus_detected_start;
	bool ac_conn;
	bool vddadc_en_ac;
	bool vddadc_en_usb;
	int vbat;
	int old_vbat;
	bool autopower;
	bool autopower_cfg;
	struct ab8500 *parent;
	struct ab8500_gpadc *gpadc;
	struct abx500_bm_data *bat;
	struct ab8500_charger_event_flags flags;
	struct ab8500_charger_usb_state usb_state;
	struct ux500_charger ac_chg;
	struct ux500_charger usb_chg;
	struct ab8500_charger_info ac;
	struct ab8500_charger_info usb;
	struct regulator *regu;
	struct workqueue_struct *charger_wq;
	struct delayed_work check_vbat_work;
	struct delayed_work check_hw_failure_work;
	struct delayed_work check_usbchgnotok_work;
	struct delayed_work kick_wd_work;
	struct work_struct ac_work;
	struct work_struct detect_usb_type_work;
	struct work_struct usb_link_status_work;
	struct work_struct usb_state_changed_work;
	struct work_struct check_main_thermal_prot_work;
	struct work_struct check_usb_thermal_prot_work;
	struct usb_phy *usb_phy;
	struct notifier_block nb;
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

/**
 * ab8500_power_loss_handling - set how we handle powerloss.
 * @di:		pointer to the ab8500_charger structure
 *
 * Magic nummbers are from STE HW department.
 */
static void ab8500_power_loss_handling(struct ab8500_charger *di)
{
	u8 reg;
	int ret;

	dev_dbg(di->dev, "Autopower : %d\n", di->autopower);

	/* read the autopower register */
	ret = abx500_get_register_interruptible(di->dev, 0x15, 0x00, &reg);
	if (ret) {
		dev_err(di->dev, "%d write failed\n", __LINE__);
		return;
	}

	/* enable the OPT emulation registers */
	ret = abx500_set_register_interruptible(di->dev, 0x11, 0x00, 0x2);
	if (ret) {
		dev_err(di->dev, "%d write failed\n", __LINE__);
		return;
	}

	if (di->autopower)
		reg |= 0x8;
	else
		reg &= ~0x8;

	/* write back the changed value to autopower reg */
	ret = abx500_set_register_interruptible(di->dev, 0x15, 0x00, reg);
	if (ret) {
		dev_err(di->dev, "%d write failed\n", __LINE__);
		return;
	}

	/* disable the set OTP registers again */
	ret = abx500_set_register_interruptible(di->dev, 0x11, 0x00, 0x0);
	if (ret) {
		dev_err(di->dev, "%d write failed\n", __LINE__);
		return;
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
			ab8500_power_loss_handling(di);
		} else if (!di->autopower &&
			   (di->ac.charger_connected ||
			    di->usb.charger_connected)) {
			di->autopower = true;
			ab8500_power_loss_handling(di);
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
		sysfs_notify(&di->usb_chg.psy.dev->kobj, NULL, "present");
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
static int ab8500_charger_detect_chargers(struct ab8500_charger *di)
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
	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CH_USBCH_STAT1_REG, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

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

	switch (link_status) {
	case USB_STAT_STD_HOST_NC:
	case USB_STAT_STD_HOST_C_NS:
	case USB_STAT_STD_HOST_C_S:
		dev_dbg(di->dev, "USB Type - Standard host is "
			"detected through USB driver\n");
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P09;
		break;
	case USB_STAT_HOST_CHG_HS_CHIRP:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P5;
		break;
	case USB_STAT_HOST_CHG_HS:
	case USB_STAT_ACA_RID_C_HS:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P9;
		break;
	case USB_STAT_ACA_RID_A:
		/*
		 * Dedicated charger level minus maximum current accessory
		 * can consume (300mA). Closest level is 1100mA
		 */
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_1P1;
		break;
	case USB_STAT_ACA_RID_B:
		/*
		 * Dedicated charger level minus 120mA (20mA for ACA and
		 * 100mA for potential accessory). Closest level is 1300mA
		 */
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_1P3;
		break;
	case USB_STAT_DEDICATED_CHG:
	case USB_STAT_HOST_CHG_NM:
	case USB_STAT_ACA_RID_C_HS_CHIRP:
	case USB_STAT_ACA_RID_C_NM:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_1P5;
		break;
	case USB_STAT_RESERVED:
		/*
		 * This state is used to indicate that VBUS has dropped below
		 * the detection level 4 times in a row. This is due to the
		 * charger output current is set to high making the charger
		 * voltage collapse. This have to be propagated through to
		 * chargalg. This is done using the property
		 * POWER_SUPPLY_PROP_CURRENT_AVG = 1
		 */
		di->flags.vbus_collapse = true;
		dev_dbg(di->dev, "USB Type - USB_STAT_RESERVED "
			"VBUS has collapsed\n");
		ret = -1;
		break;
	case USB_STAT_HM_IDGND:
	case USB_STAT_NOT_CONFIGURED:
	case USB_STAT_NOT_VALID_LINK:
		dev_err(di->dev, "USB Type - Charging not allowed\n");
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P05;
		ret = -ENXIO;
		break;
	default:
		dev_err(di->dev, "USB Type - Unknown\n");
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P05;
		ret = -ENXIO;
		break;
	};

	dev_dbg(di->dev, "USB Type - 0x%02x MaxCurr: %d",
		link_status, di->max_usb_in_curr);

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
	ret = abx500_get_register_interruptible(di->dev, AB8500_USB,
		AB8500_USB_LINE_STAT_REG, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

	/* get the USB type */
	val = (val & AB8500_USB_LINK_STATUS) >> 3;
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
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return ret;
		}
		ret = abx500_get_register_interruptible(di->dev, AB8500_USB,
			AB8500_USB_LINE_STAT_REG, &val);
		if (ret < 0) {
			dev_err(di->dev, "%s ab8500 read failed\n", __func__);
			return ret;
		}
		/*
		 * Until the IT source register is read the UsbLineStatus
		 * register is not updated, hence doing the same
		 * Revisit this:
		 */

		/* get the USB type */
		val = (val & AB8500_USB_LINK_STATUS) >> 3;
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

/*
 * This array maps the raw hex value to charger current used by the AB8500
 * Values taken from the UM0836
 */
static int ab8500_charger_current_map[] = {
	100 ,
	200 ,
	300 ,
	400 ,
	500 ,
	600 ,
	700 ,
	800 ,
	900 ,
	1000 ,
	1100 ,
	1200 ,
	1300 ,
	1400 ,
	1500 ,
};

/*
 * This array maps the raw hex value to VBUS input current used by the AB8500
 * Values taken from the UM0836
 */
static int ab8500_charger_vbus_in_curr_map[] = {
	USB_CH_IP_CUR_LVL_0P05,
	USB_CH_IP_CUR_LVL_0P09,
	USB_CH_IP_CUR_LVL_0P19,
	USB_CH_IP_CUR_LVL_0P29,
	USB_CH_IP_CUR_LVL_0P38,
	USB_CH_IP_CUR_LVL_0P45,
	USB_CH_IP_CUR_LVL_0P5,
	USB_CH_IP_CUR_LVL_0P6,
	USB_CH_IP_CUR_LVL_0P7,
	USB_CH_IP_CUR_LVL_0P8,
	USB_CH_IP_CUR_LVL_0P9,
	USB_CH_IP_CUR_LVL_1P0,
	USB_CH_IP_CUR_LVL_1P1,
	USB_CH_IP_CUR_LVL_1P3,
	USB_CH_IP_CUR_LVL_1P4,
	USB_CH_IP_CUR_LVL_1P5,
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

static int ab8500_current_to_regval(int curr)
{
	int i;

	if (curr < ab8500_charger_current_map[0])
		return 0;

	for (i = 0; i < ARRAY_SIZE(ab8500_charger_current_map); i++) {
		if (curr < ab8500_charger_current_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab8500_charger_current_map) - 1;
	if (curr == ab8500_charger_current_map[i])
		return i;
	else
		return -1;
}

static int ab8500_vbus_in_curr_to_regval(int curr)
{
	int i;

	if (curr < ab8500_charger_vbus_in_curr_map[0])
		return 0;

	for (i = 0; i < ARRAY_SIZE(ab8500_charger_vbus_in_curr_map); i++) {
		if (curr < ab8500_charger_vbus_in_curr_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab8500_charger_vbus_in_curr_map) - 1;
	if (curr == ab8500_charger_vbus_in_curr_map[i])
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
	switch (di->usb_state.usb_current) {
	case 100:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P09;
		break;
	case 200:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P19;
		break;
	case 300:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P29;
		break;
	case 400:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P38;
		break;
	case 500:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P5;
		break;
	default:
		di->max_usb_in_curr = USB_CH_IP_CUR_LVL_0P05;
		return -1;
		break;
	};
	return 0;
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
	int ret;
	int input_curr_index;
	int min_value;

	/* We should always use to lowest current limit */
	min_value = min(di->bat->chg_params->usb_curr_max, ich_in);

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

	input_curr_index = ab8500_vbus_in_curr_to_regval(min_value);
	if (input_curr_index < 0) {
		dev_err(di->dev, "VBUS input current limit too high\n");
		return -ENXIO;
	}

	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_USBCH_IPT_CRNTLVL_REG,
		input_curr_index << VBUS_IN_CURR_LIM_SHIFT);
	if (ret)
		dev_err(di->dev, "%s write failed\n", __func__);

	return ret;
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
			regulator_enable(di->regu);
			di->vddadc_en_ac = true;
		}

		/* Check if the requested voltage or current is valid */
		volt_index = ab8500_voltage_to_regval(vset);
		curr_index = ab8500_current_to_regval(iset);
		input_curr_index = ab8500_current_to_regval(
			di->bat->chg_params->ac_curr_max);
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
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_MCH_IPT_CURLVL_REG,
			input_curr_index << MAIN_CH_INPUT_CURR_SHIFT);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}
		/* ChOutputCurentLevel: protected output current */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CH_OPT_CRNTLVL_REG, (u8) curr_index);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		/* Check if VBAT overshoot control should be enabled */
		if (!di->bat->enable_overshoot)
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

			ret = abx500_set_register_interruptible(di->dev,
				AB8500_CHARGER,
				AB8500_CH_OPT_CRNTLVL_REG, CH_OP_CUR_LVL_0P1);
			if (ret) {
				dev_err(di->dev,
					"%s write failed\n", __func__);
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
	ab8500_power_supply_changed(di, &di->ac_chg.psy);

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
			regulator_enable(di->regu);
			di->vddadc_en_usb = true;
		}

		/* Enable USB charging */
		dev_dbg(di->dev, "Enable USB: %dmV %dmA\n", vset, ich_out);

		/* Check if the requested voltage or current is valid */
		volt_index = ab8500_voltage_to_regval(vset);
		curr_index = ab8500_current_to_regval(ich_out);
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
		/* USBChInputCurr: current that can be drawn from the usb */
		ret = ab8500_charger_set_vbus_in_curr(di, di->max_usb_in_curr);
		if (ret) {
			dev_err(di->dev, "setting USBChInputCurr failed\n");
			return ret;
		}
		/* ChOutputCurentLevel: protected output current */
		ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
			AB8500_CH_OPT_CRNTLVL_REG, (u8) curr_index);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}
		/* Check if VBAT overshoot control should be enabled */
		if (!di->bat->enable_overshoot)
			overshoot = USB_CHG_NO_OVERSHOOT_ENA_N;

		/* Enable USB Charger */
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

		queue_delayed_work(di->charger_wq, &di->check_vbat_work, HZ);

		di->usb.charger_online = 1;
	} else {
		/* Disable USB charging */
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

		di->usb.charger_online = 0;
		di->usb.wd_expired = false;

		/* Disable regulator if enabled */
		if (di->vddadc_en_usb) {
			regulator_disable(di->regu);
			di->vddadc_en_usb = false;
		}

		dev_dbg(di->dev, "%s Disabled USB charging\n", __func__);

		/* Cancel any pending Vbat check work */
		if (delayed_work_pending(&di->check_vbat_work))
			cancel_delayed_work(&di->check_vbat_work);

	}
	ab8500_power_supply_changed(di, &di->usb_chg.psy);

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

	if (charger->psy.type == POWER_SUPPLY_TYPE_MAINS)
		di = to_ab8500_charger_ac_device_info(charger);
	else if (charger->psy.type == POWER_SUPPLY_TYPE_USB)
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
	int curr_index;
	struct ab8500_charger *di;

	if (charger->psy.type == POWER_SUPPLY_TYPE_MAINS)
		di = to_ab8500_charger_ac_device_info(charger);
	else if (charger->psy.type == POWER_SUPPLY_TYPE_USB)
		di = to_ab8500_charger_usb_device_info(charger);
	else
		return -ENXIO;

	curr_index = ab8500_current_to_regval(ich_out);
	if (curr_index < 0) {
		dev_err(di->dev,
			"Charger current too high, "
			"charging not started\n");
		return -ENXIO;
	}

	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CH_OPT_CRNTLVL_REG, (u8) curr_index);
	if (ret) {
		dev_err(di->dev, "%s write failed\n", __func__);
		return ret;
	}

	/* Reset the main and usb drop input current measurement counter */
	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
				AB8500_CHARGER_CTRL,
				0x1);
	if (ret) {
		dev_err(di->dev, "%s write failed\n", __func__);
		return ret;
	}

	return ret;
}

static int ab8500_charger_get_ext_psy_data(struct device *dev, void *data)
{
	struct power_supply *psy;
	struct power_supply *ext;
	struct ab8500_charger *di;
	union power_supply_propval ret;
	int i, j;
	bool psy_found = false;
	struct ux500_charger *usb_chg;

	usb_chg = (struct ux500_charger *)data;
	psy = &usb_chg->psy;

	di = to_ab8500_charger_usb_device_info(usb_chg);

	ext = dev_get_drvdata(dev);

	/* For all psy where the driver name appears in any supplied_to */
	for (i = 0; i < ext->num_supplicants; i++) {
		if (!strcmp(ext->supplied_to[i], psy->name))
			psy_found = true;
	}

	if (!psy_found)
		return 0;

	/* Go through all properties for the psy */
	for (j = 0; j < ext->num_properties; j++) {
		enum power_supply_property prop;
		prop = ext->properties[j];

		if (ext->get_property(ext, prop, &ret))
			continue;

		switch (prop) {
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			switch (ext->type) {
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
		&di->usb_chg.psy, ab8500_charger_get_ext_psy_data);

	/* First run old_vbat is 0. */
	if (di->old_vbat == 0)
		di->old_vbat = di->vbat;

	if (!((di->old_vbat <= VBAT_TRESH_IP_CUR_RED &&
		di->vbat <= VBAT_TRESH_IP_CUR_RED) ||
		(di->old_vbat > VBAT_TRESH_IP_CUR_RED &&
		di->vbat > VBAT_TRESH_IP_CUR_RED))) {

		dev_dbg(di->dev, "Vbat did cross threshold, curr: %d, new: %d,"
			" old: %d\n", di->max_usb_in_curr, di->vbat,
			di->old_vbat);
		ab8500_charger_set_vbus_in_curr(di, di->max_usb_in_curr);
		power_supply_changed(&di->usb_chg.psy);
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
			ab8500_power_supply_changed(di, &di->ac_chg.psy);
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
			ab8500_power_supply_changed(di, &di->usb_chg.psy);
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
	ret = ab8500_charger_detect_chargers(di);
	if (ret < 0)
		return;

	if (ret & AC_PW_CONN) {
		di->ac.charger_connected = 1;
		di->ac_conn = true;
	} else {
		di->ac.charger_connected = 0;
	}

	ab8500_power_supply_changed(di, &di->ac_chg.psy);
	sysfs_notify(&di->ac_chg.psy.dev->kobj, NULL, "present");
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
	ret = ab8500_charger_detect_chargers(di);
	if (ret < 0)
		return;

	if (!(ret & USB_PW_CONN)) {
		di->vbus_detected = 0;
		ab8500_charger_set_usb_connected(di, false);
		ab8500_power_supply_changed(di, &di->usb_chg.psy);
	} else {
		di->vbus_detected = 1;

		if (is_ab8500_1p1_or_earlier(di->parent)) {
			ret = ab8500_charger_detect_usb_type(di);
			if (!ret) {
				ab8500_charger_set_usb_connected(di, true);
				ab8500_power_supply_changed(di,
							    &di->usb_chg.psy);
			}
		} else {
			/* For ABB cut2.0 and onwards we have an IRQ,
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
						&di->usb_chg.psy);
				}
			}
		}
	}
}

/**
 * ab8500_charger_usb_link_status_work() - work to detect USB type
 * @work:	pointer to the work_struct structure
 *
 * Detect the type of USB plugged
 */
static void ab8500_charger_usb_link_status_work(struct work_struct *work)
{
	int ret;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, usb_link_status_work);

	/*
	 * Since we can't be sure that the events are received
	 * synchronously, we have the check if  is
	 * connected by reading the status register
	 */
	ret = ab8500_charger_detect_chargers(di);
	if (ret < 0)
		return;

	if (!(ret & USB_PW_CONN)) {
		di->vbus_detected = 0;
		ab8500_charger_set_usb_connected(di, false);
		ab8500_power_supply_changed(di, &di->usb_chg.psy);
	} else {
		di->vbus_detected = 1;
		ret = ab8500_charger_read_usb_type(di);
		if (!ret) {
			/* Update maximum input current */
			ret = ab8500_charger_set_vbus_in_curr(di,
					di->max_usb_in_curr);
			if (ret)
				return;

			ab8500_charger_set_usb_connected(di, true);
			ab8500_power_supply_changed(di, &di->usb_chg.psy);
		} else if (ret == -ENXIO) {
			/* No valid charger type detected */
			ab8500_charger_set_usb_connected(di, false);
			ab8500_power_supply_changed(di, &di->usb_chg.psy);
		}
	}
}

static void ab8500_charger_usb_state_changed_work(struct work_struct *work)
{
	int ret;
	unsigned long flags;

	struct ab8500_charger *di = container_of(work,
		struct ab8500_charger, usb_state_changed_work);

	if (!di->vbus_detected)
		return;

	spin_lock_irqsave(&di->usb_state.usb_lock, flags);
	di->usb_state.usb_changed = false;
	spin_unlock_irqrestore(&di->usb_state.usb_lock, flags);

	/*
	 * wait for some time until you get updates from the usb stack
	 * and negotiations are completed
	 */
	msleep(250);

	if (di->usb_state.usb_changed)
		return;

	dev_dbg(di->dev, "%s USB state: 0x%02x mA: %d\n",
		__func__, di->usb_state.state, di->usb_state.usb_current);

	switch (di->usb_state.state) {
	case AB8500_BM_USB_STATE_RESET_HS:
	case AB8500_BM_USB_STATE_RESET_FS:
	case AB8500_BM_USB_STATE_SUSPEND:
	case AB8500_BM_USB_STATE_MAX:
		ab8500_charger_set_usb_connected(di, false);
		ab8500_power_supply_changed(di, &di->usb_chg.psy);
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
					di->max_usb_in_curr);
			if (ret)
				return;

			ab8500_charger_set_usb_connected(di, true);
			ab8500_power_supply_changed(di, &di->usb_chg.psy);
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
		ab8500_power_supply_changed(di, &di->usb_chg.psy);
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

	ab8500_power_supply_changed(di, &di->ac_chg.psy);
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

	ab8500_power_supply_changed(di, &di->usb_chg.psy);
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
	ab8500_power_supply_changed(di, &di->ac_chg.psy);

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
		ab8500_power_supply_changed(di, &di->ac_chg.psy);
	}
	if (di->usb.charger_online) {
		di->usb.wd_expired = true;
		ab8500_power_supply_changed(di, &di->usb_chg.psy);
	}

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
	ab8500_power_supply_changed(di, &di->usb_chg.psy);

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
		di->ac.charger_voltage = ab8500_charger_get_ac_voltage(di);
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
		val->intval = ab8500_charger_get_ac_current(di) * 1000;
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
		di->usb.charger_voltage = ab8500_charger_get_vbus_voltage(di);
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
		val->intval = ab8500_charger_get_usb_current(di) * 1000;
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
			AB8500_CHARGER,
			AB8500_CH_OPT_CRNTLVL_MAX_REG, CH_OP_CUR_LVL_1P6);
		if (ret) {
			dev_err(di->dev,
				"failed to set CH_OPT_CRNTLVL_MAX_REG\n");
			goto out;
		}
	}

	/* VBUS OVV set to 6.3V and enable automatic current limitiation */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_CHARGER,
		AB8500_USBCH_CTRL2_REG,
		VBUS_OVV_SELECT_6P3V | VBUS_AUTO_IN_CURR_LIM_ENA);
	if (ret) {
		dev_err(di->dev, "failed to set VBUS OVV\n");
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
		dev_err(di->dev, "faile to enable main watchdog\n");
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

	/* Backup battery voltage and current */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_RTC,
		AB8500_RTC_BACKUP_CHG_REG,
		di->bat->bkup_bat_v |
		di->bat->bkup_bat_i);
	if (ret) {
		dev_err(di->dev, "failed to setup backup battery charging\n");
		goto out;
	}

	/* Enable backup battery charging */
	abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_RTC, AB8500_RTC_CTRL_REG,
		RTC_BUP_CH_ENA, RTC_BUP_CH_ENA);
	if (ret < 0)
		dev_err(di->dev, "%s mask and set failed\n", __func__);

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
};

static int ab8500_charger_usb_notifier_call(struct notifier_block *nb,
		unsigned long event, void *power)
{
	struct ab8500_charger *di =
		container_of(nb, struct ab8500_charger, nb);
	enum ab8500_usb_state bm_usb_state;
	unsigned mA = *((unsigned *)power);

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
	di->usb_state.usb_changed = true;
	spin_unlock(&di->usb_state.usb_lock);

	di->usb_state.state = bm_usb_state;
	di->usb_state.usb_current = mA;

	queue_work(di->charger_wq, &di->usb_state_changed_work);

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
		if (!delayed_work_pending(
			&di->kick_wd_work)) {
			queue_delayed_work(di->charger_wq, &di->kick_wd_work,
				round_jiffies(WD_KICK_INTERVAL));
		}
	}

	/* If we still have a HW failure, schedule a new check */
	if (di->flags.mainextchnotok || di->flags.vbus_ovv) {
		queue_delayed_work(di->charger_wq,
			&di->check_hw_failure_work, 0);
	}

	return 0;
}

static int ab8500_charger_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ab8500_charger *di = platform_get_drvdata(pdev);

	/* Cancel any pending HW failure check */
	if (delayed_work_pending(&di->check_hw_failure_work))
		cancel_delayed_work(&di->check_hw_failure_work);

	return 0;
}
#else
#define ab8500_charger_suspend      NULL
#define ab8500_charger_resume       NULL
#endif

static int __devexit ab8500_charger_remove(struct platform_device *pdev)
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

	/* disable the regulator */
	regulator_put(di->regu);

	/* Backup battery voltage and current disable */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_RTC, AB8500_RTC_CTRL_REG, RTC_BUP_CH_ENA, 0);
	if (ret < 0)
		dev_err(di->dev, "%s mask and set failed\n", __func__);

	usb_unregister_notifier(di->usb_phy, &di->nb);
	usb_put_phy(di->usb_phy);

	/* Delete the work queue */
	destroy_workqueue(di->charger_wq);

	flush_scheduled_work();
	power_supply_unregister(&di->usb_chg.psy);
	power_supply_unregister(&di->ac_chg.psy);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static char *supply_interface[] = {
	"ab8500_chargalg",
	"ab8500_fg",
	"ab8500_btemp",
};

static int __devinit ab8500_charger_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ab8500_charger *di;
	int irq, i, charger_status, ret = 0;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&pdev->dev, "%s no mem for ab8500_charger\n", __func__);
		return -ENOMEM;
	}
	di->bat = pdev->mfd_cell->platform_data;
	if (!di->bat) {
		if (np) {
			ret = bmdevs_of_probe(&pdev->dev, np, &di->bat);
			if (ret) {
				dev_err(&pdev->dev,
					"failed to get battery information\n");
				return ret;
			}
			di->autopower_cfg = of_property_read_bool(np, "autopower_cfg");
		} else {
			dev_err(&pdev->dev, "missing dt node for ab8500_charger\n");
			return -EINVAL;
		}
	} else {
		dev_info(&pdev->dev, "falling back to legacy platform data\n");
		di->autopower_cfg = false;
	}

	/* get parent data */
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);
	di->gpadc = ab8500_gpadc_get("ab8500-gpadc.0");

	/* initialize lock */
	spin_lock_init(&di->usb_state.usb_lock);

	di->autopower = false;

	/* AC supply */
	/* power_supply base class */
	di->ac_chg.psy.name = "ab8500_ac";
	di->ac_chg.psy.type = POWER_SUPPLY_TYPE_MAINS;
	di->ac_chg.psy.properties = ab8500_charger_ac_props;
	di->ac_chg.psy.num_properties = ARRAY_SIZE(ab8500_charger_ac_props);
	di->ac_chg.psy.get_property = ab8500_charger_ac_get_property;
	di->ac_chg.psy.supplied_to = supply_interface;
	di->ac_chg.psy.num_supplicants = ARRAY_SIZE(supply_interface),
	/* ux500_charger sub-class */
	di->ac_chg.ops.enable = &ab8500_charger_ac_en;
	di->ac_chg.ops.kick_wd = &ab8500_charger_watchdog_kick;
	di->ac_chg.ops.update_curr = &ab8500_charger_update_charger_current;
	di->ac_chg.max_out_volt = ab8500_charger_voltage_map[
		ARRAY_SIZE(ab8500_charger_voltage_map) - 1];
	di->ac_chg.max_out_curr = ab8500_charger_current_map[
		ARRAY_SIZE(ab8500_charger_current_map) - 1];

	/* USB supply */
	/* power_supply base class */
	di->usb_chg.psy.name = "ab8500_usb";
	di->usb_chg.psy.type = POWER_SUPPLY_TYPE_USB;
	di->usb_chg.psy.properties = ab8500_charger_usb_props;
	di->usb_chg.psy.num_properties = ARRAY_SIZE(ab8500_charger_usb_props);
	di->usb_chg.psy.get_property = ab8500_charger_usb_get_property;
	di->usb_chg.psy.supplied_to = supply_interface;
	di->usb_chg.psy.num_supplicants = ARRAY_SIZE(supply_interface),
	/* ux500_charger sub-class */
	di->usb_chg.ops.enable = &ab8500_charger_usb_en;
	di->usb_chg.ops.kick_wd = &ab8500_charger_watchdog_kick;
	di->usb_chg.ops.update_curr = &ab8500_charger_update_charger_current;
	di->usb_chg.max_out_volt = ab8500_charger_voltage_map[
		ARRAY_SIZE(ab8500_charger_voltage_map) - 1];
	di->usb_chg.max_out_curr = ab8500_charger_current_map[
		ARRAY_SIZE(ab8500_charger_current_map) - 1];


	/* Create a work queue for the charger */
	di->charger_wq =
		create_singlethread_workqueue("ab8500_charger_wq");
	if (di->charger_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		return -ENOMEM;
	}

	/* Init work for HW failure check */
	INIT_DEFERRABLE_WORK(&di->check_hw_failure_work,
		ab8500_charger_check_hw_failure_work);
	INIT_DEFERRABLE_WORK(&di->check_usbchgnotok_work,
		ab8500_charger_check_usbchargernotok_work);

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

	/* Init work for charger detection */
	INIT_WORK(&di->usb_link_status_work,
		ab8500_charger_usb_link_status_work);
	INIT_WORK(&di->ac_work, ab8500_charger_ac_work);
	INIT_WORK(&di->detect_usb_type_work,
		ab8500_charger_detect_usb_type_work);

	INIT_WORK(&di->usb_state_changed_work,
		ab8500_charger_usb_state_changed_work);

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
	di->regu = regulator_get(di->dev, "vddadc");
	if (IS_ERR(di->regu)) {
		ret = PTR_ERR(di->regu);
		dev_err(di->dev, "failed to get vddadc regulator\n");
		goto free_charger_wq;
	}


	/* Initialize OVV, and other registers */
	ret = ab8500_charger_init_hw_registers(di);
	if (ret) {
		dev_err(di->dev, "failed to initialize ABB registers\n");
		goto free_regulator;
	}

	/* Register AC charger class */
	ret = power_supply_register(di->dev, &di->ac_chg.psy);
	if (ret) {
		dev_err(di->dev, "failed to register AC charger\n");
		goto free_regulator;
	}

	/* Register USB charger class */
	ret = power_supply_register(di->dev, &di->usb_chg.psy);
	if (ret) {
		dev_err(di->dev, "failed to register USB charger\n");
		goto free_ac;
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
	charger_status = ab8500_charger_detect_chargers(di);
	if (charger_status & AC_PW_CONN) {
		di->ac.charger_connected = 1;
		di->ac_conn = true;
		ab8500_power_supply_changed(di, &di->ac_chg.psy);
		sysfs_notify(&di->ac_chg.psy.dev->kobj, NULL, "present");
	}

	if (charger_status & USB_PW_CONN) {
		dev_dbg(di->dev, "VBUS Detect during startup\n");
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
	power_supply_unregister(&di->usb_chg.psy);
free_ac:
	power_supply_unregister(&di->ac_chg.psy);
free_regulator:
	regulator_put(di->regu);
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
	.remove = __devexit_p(ab8500_charger_remove),
	.suspend = ab8500_charger_suspend,
	.resume = ab8500_charger_resume,
	.driver = {
		.name = "ab8500-charger",
		.owner = THIS_MODULE,
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
