/*
 * intel_mid_battery.c - Intel MID PMIC Battery Driver
 *
 * Copyright (C) 2009 Intel Corporation
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author: Nithish Mahalingam <nithish.mahalingam@intel.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/param.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

#include <asm/intel_scu_ipc.h>

#define DRIVER_NAME "pmic_battery"

/*********************************************************************
 *		Generic defines
 *********************************************************************/

static int debug;
module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, "Flag to enable PMIC Battery debug messages.");

#define PMIC_BATT_DRV_INFO_UPDATED	1
#define PMIC_BATT_PRESENT		1
#define PMIC_BATT_NOT_PRESENT		0
#define PMIC_USB_PRESENT		PMIC_BATT_PRESENT
#define PMIC_USB_NOT_PRESENT		PMIC_BATT_NOT_PRESENT

/* pmic battery register related */
#define PMIC_BATT_CHR_SCHRGINT_ADDR	0xD2
#define PMIC_BATT_CHR_SBATOVP_MASK	(1 << 1)
#define PMIC_BATT_CHR_STEMP_MASK	(1 << 2)
#define PMIC_BATT_CHR_SCOMP_MASK	(1 << 3)
#define PMIC_BATT_CHR_SUSBDET_MASK	(1 << 4)
#define PMIC_BATT_CHR_SBATDET_MASK	(1 << 5)
#define PMIC_BATT_CHR_SDCLMT_MASK	(1 << 6)
#define PMIC_BATT_CHR_SUSBOVP_MASK	(1 << 7)
#define PMIC_BATT_CHR_EXCPT_MASK	0xC6
#define PMIC_BATT_ADC_ACCCHRG_MASK	(1 << 31)
#define PMIC_BATT_ADC_ACCCHRGVAL_MASK	0x7FFFFFFF

/* pmic ipc related */
#define PMIC_BATT_CHR_IPC_FCHRG_SUBID	0x4
#define PMIC_BATT_CHR_IPC_TCHRG_SUBID	0x6

/* types of battery charging */
enum batt_charge_type {
	BATT_USBOTG_500MA_CHARGE,
	BATT_USBOTG_TRICKLE_CHARGE,
};

/* valid battery events */
enum batt_event {
	BATT_EVENT_BATOVP_EXCPT,
	BATT_EVENT_USBOVP_EXCPT,
	BATT_EVENT_TEMP_EXCPT,
	BATT_EVENT_DCLMT_EXCPT,
	BATT_EVENT_EXCPT
};


/*********************************************************************
 *		Battery properties
 *********************************************************************/

/*
 * pmic battery info
 */
struct pmic_power_module_info {
	bool is_dev_info_updated;
	struct device *dev;
	/* pmic battery data */
	unsigned long update_time;		/* jiffies when data read */
	unsigned int usb_is_present;
	unsigned int batt_is_present;
	unsigned int batt_health;
	unsigned int usb_health;
	unsigned int batt_status;
	unsigned int batt_charge_now;		/* in mAS */
	unsigned int batt_prev_charge_full;	/* in mAS */
	unsigned int batt_charge_rate;		/* in units per second */

	struct power_supply usb;
	struct power_supply batt;
	int irq;				/* GPE_ID or IRQ# */
	struct workqueue_struct *monitor_wqueue;
	struct delayed_work monitor_battery;
	struct work_struct handler;
};

static unsigned int delay_time = 2000;	/* in ms */

/*
 * pmic ac properties
 */
static enum power_supply_property pmic_usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
};

/*
 * pmic battery properties
 */
static enum power_supply_property pmic_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
};


/*
 * Glue functions for talking to the IPC
 */

struct battery_property {
	u32 capacity;	/* Charger capacity */
	u8  crnt;	/* Quick charge current value*/
	u8  volt;	/* Fine adjustment of constant charge voltage */
	u8  prot;	/* CHRGPROT register value */
	u8  prot2;	/* CHRGPROT1 register value */
	u8  timer;	/* Charging timer */
};

#define IPCMSG_BATTERY		0xEF

/* Battery coulomb counter accumulator commands */
#define IPC_CMD_CC_WR		  0 /* Update coulomb counter value */
#define IPC_CMD_CC_RD		  1 /* Read coulomb counter value */
#define IPC_CMD_BATTERY_PROPERTY  2 /* Read Battery property */

/**
 *	pmic_scu_ipc_battery_cc_read	-	read battery cc
 *	@value: battery coulomb counter read
 *
 *	Reads the battery couloumb counter value, returns 0 on success, or
 *	an error code
 *
 *	This function may sleep. Locking for SCU accesses is handled for
 *	the caller.
 */
static int pmic_scu_ipc_battery_cc_read(u32 *value)
{
	return intel_scu_ipc_command(IPCMSG_BATTERY, IPC_CMD_CC_RD,
					NULL, 0, value, 1);
}

/**
 *	pmic_scu_ipc_battery_property_get	-	fetch properties
 *	@prop: battery properties
 *
 *	Retrieve the battery properties from the power management
 *
 *	This function may sleep. Locking for SCU accesses is handled for
 *	the caller.
 */
static int pmic_scu_ipc_battery_property_get(struct battery_property *prop)
{
	u32 data[3];
	u8 *p = (u8 *)&data[1];
	int err = intel_scu_ipc_command(IPCMSG_BATTERY,
				IPC_CMD_BATTERY_PROPERTY, NULL, 0, data, 3);

	prop->capacity = data[0];
	prop->crnt = *p++;
	prop->volt = *p++;
	prop->prot = *p++;
	prop->prot2 = *p++;
	prop->timer = *p++;

	return err;
}

/**
 *	pmic_scu_ipc_set_charger	-	set charger
 *	@charger: charger to select
 *
 *	Switch the charging mode for the SCU
 */

static int pmic_scu_ipc_set_charger(int charger)
{
	return intel_scu_ipc_simple_command(IPCMSG_BATTERY, charger);
}

/**
 * pmic_battery_log_event - log battery events
 * @event: battery event to be logged
 * Context: can sleep
 *
 * There are multiple battery events which may be of interest to users;
 * this battery function logs the different battery events onto the
 * kernel log messages.
 */
static void pmic_battery_log_event(enum batt_event event)
{
	printk(KERN_WARNING "pmic-battery: ");
	switch (event) {
	case BATT_EVENT_BATOVP_EXCPT:
		printk(KERN_CONT "battery overvoltage condition\n");
		break;
	case BATT_EVENT_USBOVP_EXCPT:
		printk(KERN_CONT "usb charger overvoltage condition\n");
		break;
	case BATT_EVENT_TEMP_EXCPT:
		printk(KERN_CONT "high battery temperature condition\n");
		break;
	case BATT_EVENT_DCLMT_EXCPT:
		printk(KERN_CONT "over battery charge current condition\n");
		break;
	default:
		printk(KERN_CONT "charger/battery exception %d\n", event);
		break;
	}
}

/**
 * pmic_battery_read_status - read battery status information
 * @pbi: device info structure to update the read information
 * Context: can sleep
 *
 * PMIC power source information need to be updated based on the data read
 * from the PMIC battery registers.
 *
 */
static void pmic_battery_read_status(struct pmic_power_module_info *pbi)
{
	unsigned int update_time_intrvl;
	unsigned int chrg_val;
	u32 ccval;
	u8 r8;
	struct battery_property batt_prop;
	int batt_present = 0;
	int usb_present = 0;
	int batt_exception = 0;

	/* make sure the last batt_status read happened delay_time before */
	if (pbi->update_time && time_before(jiffies, pbi->update_time +
						msecs_to_jiffies(delay_time)))
		return;

	update_time_intrvl = jiffies_to_msecs(jiffies -	pbi->update_time);
	pbi->update_time = jiffies;

	/* read coulomb counter registers and schrgint register */
	if (pmic_scu_ipc_battery_cc_read(&ccval)) {
		dev_warn(pbi->dev, "%s(): ipc config cmd failed\n",
								__func__);
		return;
	}

	if (intel_scu_ipc_ioread8(PMIC_BATT_CHR_SCHRGINT_ADDR, &r8)) {
		dev_warn(pbi->dev, "%s(): ipc pmic read failed\n",
								__func__);
		return;
	}

	/*
	 * set pmic_power_module_info members based on pmic register values
	 * read.
	 */

	/* set batt_is_present */
	if (r8 & PMIC_BATT_CHR_SBATDET_MASK) {
		pbi->batt_is_present = PMIC_BATT_PRESENT;
		batt_present = 1;
	} else {
		pbi->batt_is_present = PMIC_BATT_NOT_PRESENT;
		pbi->batt_health = POWER_SUPPLY_HEALTH_UNKNOWN;
		pbi->batt_status = POWER_SUPPLY_STATUS_UNKNOWN;
	}

	/* set batt_health */
	if (batt_present) {
		if (r8 & PMIC_BATT_CHR_SBATOVP_MASK) {
			pbi->batt_health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
			pbi->batt_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			pmic_battery_log_event(BATT_EVENT_BATOVP_EXCPT);
			batt_exception = 1;
		} else if (r8 & PMIC_BATT_CHR_SDCLMT_MASK) {
			pbi->batt_health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
			pbi->batt_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			pmic_battery_log_event(BATT_EVENT_DCLMT_EXCPT);
			batt_exception = 1;
		} else if (r8 & PMIC_BATT_CHR_STEMP_MASK) {
			pbi->batt_health = POWER_SUPPLY_HEALTH_OVERHEAT;
			pbi->batt_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			pmic_battery_log_event(BATT_EVENT_TEMP_EXCPT);
			batt_exception = 1;
		} else {
			pbi->batt_health = POWER_SUPPLY_HEALTH_GOOD;
		}
	}

	/* set usb_is_present */
	if (r8 & PMIC_BATT_CHR_SUSBDET_MASK) {
		pbi->usb_is_present = PMIC_USB_PRESENT;
		usb_present = 1;
	} else {
		pbi->usb_is_present = PMIC_USB_NOT_PRESENT;
		pbi->usb_health = POWER_SUPPLY_HEALTH_UNKNOWN;
	}

	if (usb_present) {
		if (r8 & PMIC_BATT_CHR_SUSBOVP_MASK) {
			pbi->usb_health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
			pmic_battery_log_event(BATT_EVENT_USBOVP_EXCPT);
		} else {
			pbi->usb_health = POWER_SUPPLY_HEALTH_GOOD;
		}
	}

	chrg_val = ccval & PMIC_BATT_ADC_ACCCHRGVAL_MASK;

	/* set batt_prev_charge_full to battery capacity the first time */
	if (!pbi->is_dev_info_updated) {
		if (pmic_scu_ipc_battery_property_get(&batt_prop)) {
			dev_warn(pbi->dev, "%s(): ipc config cmd failed\n",
								__func__);
			return;
		}
		pbi->batt_prev_charge_full = batt_prop.capacity;
	}

	/* set batt_status */
	if (batt_present && !batt_exception) {
		if (r8 & PMIC_BATT_CHR_SCOMP_MASK) {
			pbi->batt_status = POWER_SUPPLY_STATUS_FULL;
			pbi->batt_prev_charge_full = chrg_val;
		} else if (ccval & PMIC_BATT_ADC_ACCCHRG_MASK) {
			pbi->batt_status = POWER_SUPPLY_STATUS_DISCHARGING;
		} else {
			pbi->batt_status = POWER_SUPPLY_STATUS_CHARGING;
		}
	}

	/* set batt_charge_rate */
	if (pbi->is_dev_info_updated && batt_present && !batt_exception) {
		if (pbi->batt_status == POWER_SUPPLY_STATUS_DISCHARGING) {
			if (pbi->batt_charge_now - chrg_val) {
				pbi->batt_charge_rate = ((pbi->batt_charge_now -
					chrg_val) * 1000 * 60) /
					update_time_intrvl;
			}
		} else if (pbi->batt_status == POWER_SUPPLY_STATUS_CHARGING) {
			if (chrg_val - pbi->batt_charge_now) {
				pbi->batt_charge_rate = ((chrg_val -
					pbi->batt_charge_now) * 1000 * 60) /
					update_time_intrvl;
			}
		} else
			pbi->batt_charge_rate = 0;
	} else {
		pbi->batt_charge_rate = -1;
	}

	/* batt_charge_now */
	if (batt_present && !batt_exception)
		pbi->batt_charge_now = chrg_val;
	else
		pbi->batt_charge_now = -1;

	pbi->is_dev_info_updated = PMIC_BATT_DRV_INFO_UPDATED;
}

/**
 * pmic_usb_get_property - usb power source get property
 * @psy: usb power supply context
 * @psp: usb power source property
 * @val: usb power source property value
 * Context: can sleep
 *
 * PMIC usb power source property needs to be provided to power_supply
 * subsytem for it to provide the information to users.
 */
static int pmic_usb_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct pmic_power_module_info *pbi = container_of(psy,
				struct pmic_power_module_info, usb);

	/* update pmic_power_module_info members */
	pmic_battery_read_status(pbi);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = pbi->usb_is_present;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = pbi->usb_health;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static inline unsigned long mAStouAh(unsigned long v)
{
	/* seconds to hours, mA to µA */
	return (v * 1000) / 3600;
}

/**
 * pmic_battery_get_property - battery power source get property
 * @psy: battery power supply context
 * @psp: battery power source property
 * @val: battery power source property value
 * Context: can sleep
 *
 * PMIC battery power source property needs to be provided to power_supply
 * subsytem for it to provide the information to users.
 */
static int pmic_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct pmic_power_module_info *pbi = container_of(psy,
				struct pmic_power_module_info, batt);

	/* update pmic_power_module_info members */
	pmic_battery_read_status(pbi);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = pbi->batt_status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = pbi->batt_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = pbi->batt_is_present;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = mAStouAh(pbi->batt_charge_now);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = mAStouAh(pbi->batt_prev_charge_full);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * pmic_battery_monitor - monitor battery status
 * @work: work structure
 * Context: can sleep
 *
 * PMIC battery status needs to be monitored for any change
 * and information needs to be frequently updated.
 */
static void pmic_battery_monitor(struct work_struct *work)
{
	struct pmic_power_module_info *pbi = container_of(work,
			struct pmic_power_module_info, monitor_battery.work);

	/* update pmic_power_module_info members */
	pmic_battery_read_status(pbi);
	queue_delayed_work(pbi->monitor_wqueue, &pbi->monitor_battery, HZ * 10);
}

/**
 * pmic_battery_set_charger - set battery charger
 * @pbi: device info structure
 * @chrg: charge mode to set battery charger in
 * Context: can sleep
 *
 * PMIC battery charger needs to be enabled based on the usb charge
 * capabilities connected to the platform.
 */
static int pmic_battery_set_charger(struct pmic_power_module_info *pbi,
						enum batt_charge_type chrg)
{
	int retval;

	/* set usblmt bits and chrgcntl register bits appropriately */
	switch (chrg) {
	case BATT_USBOTG_500MA_CHARGE:
		retval = pmic_scu_ipc_set_charger(PMIC_BATT_CHR_IPC_FCHRG_SUBID);
		break;
	case BATT_USBOTG_TRICKLE_CHARGE:
		retval = pmic_scu_ipc_set_charger(PMIC_BATT_CHR_IPC_TCHRG_SUBID);
		break;
	default:
		dev_warn(pbi->dev, "%s(): out of range usb charger "
						"charge detected\n", __func__);
		return -EINVAL;
	}

	if (retval) {
		dev_warn(pbi->dev, "%s(): ipc pmic read failed\n",
								__func__);
		return retval;
	}

	return 0;
}

/**
 * pmic_battery_interrupt_handler - pmic battery interrupt handler
 * Context: interrupt context
 *
 * PMIC battery interrupt handler which will be called with either
 * battery full condition occurs or usb otg & battery connect
 * condition occurs.
 */
static irqreturn_t pmic_battery_interrupt_handler(int id, void *dev)
{
	struct pmic_power_module_info *pbi = dev;

	schedule_work(&pbi->handler);

	return IRQ_HANDLED;
}

/**
 * pmic_battery_handle_intrpt - pmic battery service interrupt
 * @work: work structure
 * Context: can sleep
 *
 * PMIC battery needs to either update the battery status as full
 * if it detects battery full condition caused the interrupt or needs
 * to enable battery charger if it detects usb and battery detect
 * caused the source of interrupt.
 */
static void pmic_battery_handle_intrpt(struct work_struct *work)
{
	struct pmic_power_module_info *pbi = container_of(work,
				struct pmic_power_module_info, handler);
	enum batt_charge_type chrg;
	u8 r8;

	if (intel_scu_ipc_ioread8(PMIC_BATT_CHR_SCHRGINT_ADDR, &r8)) {
		dev_warn(pbi->dev, "%s(): ipc pmic read failed\n",
								__func__);
		return;
	}
	/* find the cause of the interrupt */
	if (r8 & PMIC_BATT_CHR_SBATDET_MASK) {
		pbi->batt_is_present = PMIC_BATT_PRESENT;
	} else {
		pbi->batt_is_present = PMIC_BATT_NOT_PRESENT;
		pbi->batt_health = POWER_SUPPLY_HEALTH_UNKNOWN;
		pbi->batt_status = POWER_SUPPLY_STATUS_UNKNOWN;
		return;
	}

	if (r8 & PMIC_BATT_CHR_EXCPT_MASK) {
		pbi->batt_health = POWER_SUPPLY_HEALTH_UNKNOWN;
		pbi->batt_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		pbi->usb_health = POWER_SUPPLY_HEALTH_UNKNOWN;
		pmic_battery_log_event(BATT_EVENT_EXCPT);
		return;
	} else {
		pbi->batt_health = POWER_SUPPLY_HEALTH_GOOD;
		pbi->usb_health = POWER_SUPPLY_HEALTH_GOOD;
	}

	if (r8 & PMIC_BATT_CHR_SCOMP_MASK) {
		u32 ccval;
		pbi->batt_status = POWER_SUPPLY_STATUS_FULL;

		if (pmic_scu_ipc_battery_cc_read(&ccval)) {
			dev_warn(pbi->dev, "%s(): ipc config cmd "
							"failed\n", __func__);
			return;
		}
		pbi->batt_prev_charge_full = ccval &
						PMIC_BATT_ADC_ACCCHRGVAL_MASK;
		return;
	}

	if (r8 & PMIC_BATT_CHR_SUSBDET_MASK) {
		pbi->usb_is_present = PMIC_USB_PRESENT;
	} else {
		pbi->usb_is_present = PMIC_USB_NOT_PRESENT;
		pbi->usb_health = POWER_SUPPLY_HEALTH_UNKNOWN;
		return;
	}

	/* setup battery charging */

#if 0
	/* check usb otg power capability and set charger accordingly */
	retval = langwell_udc_maxpower(&power);
	if (retval) {
		dev_warn(pbi->dev,
		    "%s(): usb otg power query failed with error code %d\n",
			__func__, retval);
		return;
	}

	if (power >= 500)
		chrg = BATT_USBOTG_500MA_CHARGE;
	else
#endif
		chrg = BATT_USBOTG_TRICKLE_CHARGE;

	/* enable battery charging */
	if (pmic_battery_set_charger(pbi, chrg)) {
		dev_warn(pbi->dev,
			"%s(): failed to set up battery charging\n", __func__);
		return;
	}

	dev_dbg(pbi->dev,
		"pmic-battery: %s() - setting up battery charger successful\n",
			__func__);
}

/**
 * pmic_battery_probe - pmic battery initialize
 * @irq: pmic battery device irq
 * @dev: pmic battery device structure
 * Context: can sleep
 *
 * PMIC battery initializes its internal data structue and other
 * infrastructure components for it to work as expected.
 */
static __devinit int probe(int irq, struct device *dev)
{
	int retval = 0;
	struct pmic_power_module_info *pbi;

	dev_dbg(dev, "pmic-battery: found pmic battery device\n");

	pbi = kzalloc(sizeof(*pbi), GFP_KERNEL);
	if (!pbi) {
		dev_err(dev, "%s(): memory allocation failed\n",
								__func__);
		return -ENOMEM;
	}

	pbi->dev = dev;
	pbi->irq = irq;
	dev_set_drvdata(dev, pbi);

	/* initialize all required framework before enabling interrupts */
	INIT_WORK(&pbi->handler, pmic_battery_handle_intrpt);
	INIT_DELAYED_WORK(&pbi->monitor_battery, pmic_battery_monitor);
	pbi->monitor_wqueue =
			create_singlethread_workqueue(dev_name(dev));
	if (!pbi->monitor_wqueue) {
		dev_err(dev, "%s(): wqueue init failed\n", __func__);
		retval = -ESRCH;
		goto wqueue_failed;
	}

	/* register interrupt */
	retval = request_irq(pbi->irq, pmic_battery_interrupt_handler,
							0, DRIVER_NAME, pbi);
	if (retval) {
		dev_err(dev, "%s(): cannot get IRQ\n", __func__);
		goto requestirq_failed;
	}

	/* register pmic-batt with power supply subsystem */
	pbi->batt.name = "pmic-batt";
	pbi->batt.type = POWER_SUPPLY_TYPE_BATTERY;
	pbi->batt.properties = pmic_battery_props;
	pbi->batt.num_properties = ARRAY_SIZE(pmic_battery_props);
	pbi->batt.get_property = pmic_battery_get_property;
	retval = power_supply_register(dev, &pbi->batt);
	if (retval) {
		dev_err(dev,
			"%s(): failed to register pmic battery device with power supply subsystem\n",
				__func__);
		goto power_reg_failed;
	}

	dev_dbg(dev, "pmic-battery: %s() - pmic battery device "
		"registration with power supply subsystem successful\n",
		__func__);

	queue_delayed_work(pbi->monitor_wqueue, &pbi->monitor_battery, HZ * 1);

	/* register pmic-usb with power supply subsystem */
	pbi->usb.name = "pmic-usb";
	pbi->usb.type = POWER_SUPPLY_TYPE_USB;
	pbi->usb.properties = pmic_usb_props;
	pbi->usb.num_properties = ARRAY_SIZE(pmic_usb_props);
	pbi->usb.get_property = pmic_usb_get_property;
	retval = power_supply_register(dev, &pbi->usb);
	if (retval) {
		dev_err(dev,
			"%s(): failed to register pmic usb device with power supply subsystem\n",
				__func__);
		goto power_reg_failed_1;
	}

	if (debug)
		printk(KERN_INFO "pmic-battery: %s() - pmic usb device "
			"registration with power supply subsystem successful\n",
			__func__);

	return retval;

power_reg_failed_1:
	power_supply_unregister(&pbi->batt);
power_reg_failed:
	cancel_delayed_work_sync(&pbi->monitor_battery);
requestirq_failed:
	destroy_workqueue(pbi->monitor_wqueue);
wqueue_failed:
	kfree(pbi);

	return retval;
}

static int __devinit platform_pmic_battery_probe(struct platform_device *pdev)
{
	return probe(pdev->id, &pdev->dev);
}

/**
 * pmic_battery_remove - pmic battery finalize
 * @dev: pmic battery device structure
 * Context: can sleep
 *
 * PMIC battery finalizes its internal data structue and other
 * infrastructure components that it initialized in
 * pmic_battery_probe.
 */

static int __devexit platform_pmic_battery_remove(struct platform_device *pdev)
{
	struct pmic_power_module_info *pbi = dev_get_drvdata(&pdev->dev);

	free_irq(pbi->irq, pbi);
	cancel_delayed_work_sync(&pbi->monitor_battery);
	destroy_workqueue(pbi->monitor_wqueue);

	power_supply_unregister(&pbi->usb);
	power_supply_unregister(&pbi->batt);

	cancel_work_sync(&pbi->handler);
	kfree(pbi);
	return 0;
}

static struct platform_driver platform_pmic_battery_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe = platform_pmic_battery_probe,
	.remove = __devexit_p(platform_pmic_battery_remove),
};

static int __init platform_pmic_battery_module_init(void)
{
	return platform_driver_register(&platform_pmic_battery_driver);
}

static void __exit platform_pmic_battery_module_exit(void)
{
	platform_driver_unregister(&platform_pmic_battery_driver);
}

module_init(platform_pmic_battery_module_init);
module_exit(platform_pmic_battery_module_exit);

MODULE_AUTHOR("Nithish Mahalingam <nithish.mahalingam@intel.com>");
MODULE_DESCRIPTION("Intel Moorestown PMIC Battery Driver");
MODULE_LICENSE("GPL");
