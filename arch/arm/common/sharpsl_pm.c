/*
 * Battery and Power Management code for the Sharp SL-C7xx and SL-Cxx00
 * series of PDAs
 *
 * Copyright (c) 2004-2005 Richard Purdie
 *
 * Based on code written by Sharp for 2.4 kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#undef DEBUG

#include <linux/module.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/apm_bios.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/leds.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/apm.h>
#include <asm/arch/pm.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/sharpsl.h>
#include <asm/hardware/sharpsl_pm.h>

/*
 * Constants
 */
#define SHARPSL_CHARGE_ON_TIME_INTERVAL        (msecs_to_jiffies(1*60*1000))  /* 1 min */
#define SHARPSL_CHARGE_FINISH_TIME             (msecs_to_jiffies(10*60*1000)) /* 10 min */
#define SHARPSL_BATCHK_TIME                    (msecs_to_jiffies(15*1000))    /* 15 sec */
#define SHARPSL_BATCHK_TIME_SUSPEND            (60*10)                        /* 10 min */

#define SHARPSL_WAIT_CO_TIME                   15  /* 15 sec */
#define SHARPSL_WAIT_DISCHARGE_ON              100 /* 100 msec */
#define SHARPSL_CHECK_BATTERY_WAIT_TIME_TEMP   10  /* 10 msec */
#define SHARPSL_CHECK_BATTERY_WAIT_TIME_VOLT   10  /* 10 msec */
#define SHARPSL_CHECK_BATTERY_WAIT_TIME_ACIN   10  /* 10 msec */
#define SHARPSL_CHARGE_WAIT_TIME               15  /* 15 msec */
#define SHARPSL_CHARGE_CO_CHECK_TIME           5   /* 5 msec */
#define SHARPSL_CHARGE_RETRY_CNT               1   /* eqv. 10 min */

/*
 * Prototypes
 */
static int sharpsl_off_charge_battery(void);
static int sharpsl_check_battery_temp(void);
static int sharpsl_check_battery_voltage(void);
static int sharpsl_ac_check(void);
static int sharpsl_fatal_check(void);
static int sharpsl_average_value(int ad);
static void sharpsl_average_clear(void);
static void sharpsl_charge_toggle(struct work_struct *private_);
static void sharpsl_battery_thread(struct work_struct *private_);


/*
 * Variables
 */
struct sharpsl_pm_status sharpsl_pm;
DECLARE_DELAYED_WORK(toggle_charger, sharpsl_charge_toggle);
DECLARE_DELAYED_WORK(sharpsl_bat, sharpsl_battery_thread);
DEFINE_LED_TRIGGER(sharpsl_charge_led_trigger);


static int get_percentage(int voltage)
{
	int i = sharpsl_pm.machinfo->bat_levels - 1;
	int bl_status = sharpsl_pm.machinfo->backlight_get_status ? sharpsl_pm.machinfo->backlight_get_status() : 0;
	struct battery_thresh *thresh;

	if (sharpsl_pm.charge_mode == CHRG_ON)
		thresh = bl_status ? sharpsl_pm.machinfo->bat_levels_acin_bl : sharpsl_pm.machinfo->bat_levels_acin;
	else
		thresh = bl_status ? sharpsl_pm.machinfo->bat_levels_noac_bl : sharpsl_pm.machinfo->bat_levels_noac;

	while (i > 0 && (voltage > thresh[i].voltage))
		i--;

	return thresh[i].percentage;
}

static int get_apm_status(int voltage)
{
	int low_thresh, high_thresh;

	if (sharpsl_pm.charge_mode == CHRG_ON) {
		high_thresh = sharpsl_pm.machinfo->status_high_acin;
		low_thresh = sharpsl_pm.machinfo->status_low_acin;
	} else {
		high_thresh = sharpsl_pm.machinfo->status_high_noac;
		low_thresh = sharpsl_pm.machinfo->status_low_noac;
	}

	if (voltage >= high_thresh)
		return APM_BATTERY_STATUS_HIGH;
	if (voltage >= low_thresh)
		return APM_BATTERY_STATUS_LOW;
	return APM_BATTERY_STATUS_CRITICAL;
}

void sharpsl_battery_kick(void)
{
	schedule_delayed_work(&sharpsl_bat, msecs_to_jiffies(125));
}
EXPORT_SYMBOL(sharpsl_battery_kick);


static void sharpsl_battery_thread(struct work_struct *private_)
{
	int voltage, percent, apm_status, i = 0;

	if (!sharpsl_pm.machinfo)
		return;

	sharpsl_pm.battstat.ac_status = (sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_ACIN) ? APM_AC_ONLINE : APM_AC_OFFLINE);

	/* Corgi cannot confirm when battery fully charged so periodically kick! */
	if (!sharpsl_pm.machinfo->batfull_irq && (sharpsl_pm.charge_mode == CHRG_ON)
			&& time_after(jiffies, sharpsl_pm.charge_start_time +  SHARPSL_CHARGE_ON_TIME_INTERVAL))
		schedule_delayed_work(&toggle_charger, 0);

	while(1) {
		voltage = sharpsl_pm.machinfo->read_devdata(SHARPSL_BATT_VOLT);

		if (voltage > 0) break;
		if (i++ > 5) {
			voltage = sharpsl_pm.machinfo->bat_levels_noac[0].voltage;
			dev_warn(sharpsl_pm.dev, "Warning: Cannot read main battery!\n");
			break;
		}
	}

	voltage = sharpsl_average_value(voltage);
	apm_status = get_apm_status(voltage);
	percent = get_percentage(voltage);

	/* At low battery voltages, the voltage has a tendency to start
           creeping back up so we try to avoid this here */
	if ((sharpsl_pm.battstat.ac_status == APM_AC_ONLINE) || (apm_status == APM_BATTERY_STATUS_HIGH) ||  percent <= sharpsl_pm.battstat.mainbat_percent) {
		sharpsl_pm.battstat.mainbat_voltage = voltage;
		sharpsl_pm.battstat.mainbat_status = apm_status;
		sharpsl_pm.battstat.mainbat_percent = percent;
	}

	dev_dbg(sharpsl_pm.dev, "Battery: voltage: %d, status: %d, percentage: %d, time: %d\n", voltage,
			sharpsl_pm.battstat.mainbat_status, sharpsl_pm.battstat.mainbat_percent, jiffies);

	/* If battery is low. limit backlight intensity to save power. */
	if ((sharpsl_pm.battstat.ac_status != APM_AC_ONLINE)
			&& ((sharpsl_pm.battstat.mainbat_status == APM_BATTERY_STATUS_LOW) ||
			(sharpsl_pm.battstat.mainbat_status == APM_BATTERY_STATUS_CRITICAL))) {
		if (!(sharpsl_pm.flags & SHARPSL_BL_LIMIT)) {
			sharpsl_pm.machinfo->backlight_limit(1);
			sharpsl_pm.flags |= SHARPSL_BL_LIMIT;
		}
	} else if (sharpsl_pm.flags & SHARPSL_BL_LIMIT) {
		sharpsl_pm.machinfo->backlight_limit(0);
		sharpsl_pm.flags &= ~SHARPSL_BL_LIMIT;
	}

	/* Suspend if critical battery level */
	if ((sharpsl_pm.battstat.ac_status != APM_AC_ONLINE)
			&& (sharpsl_pm.battstat.mainbat_status == APM_BATTERY_STATUS_CRITICAL)
			&& !(sharpsl_pm.flags & SHARPSL_APM_QUEUED)) {
		sharpsl_pm.flags |= SHARPSL_APM_QUEUED;
		dev_err(sharpsl_pm.dev, "Fatal Off\n");
		apm_queue_event(APM_CRITICAL_SUSPEND);
	}

	schedule_delayed_work(&sharpsl_bat, SHARPSL_BATCHK_TIME);
}

void sharpsl_pm_led(int val)
{
	if (val == SHARPSL_LED_ERROR) {
		dev_err(sharpsl_pm.dev, "Charging Error!\n");
	} else if (val == SHARPSL_LED_ON) {
		dev_dbg(sharpsl_pm.dev, "Charge LED On\n");
		led_trigger_event(sharpsl_charge_led_trigger, LED_FULL);
	} else {
		dev_dbg(sharpsl_pm.dev, "Charge LED Off\n");
		led_trigger_event(sharpsl_charge_led_trigger, LED_OFF);
	}
}

static void sharpsl_charge_on(void)
{
	dev_dbg(sharpsl_pm.dev, "Turning Charger On\n");

	sharpsl_pm.full_count = 0;
	sharpsl_pm.charge_mode = CHRG_ON;
	schedule_delayed_work(&toggle_charger, msecs_to_jiffies(250));
	schedule_delayed_work(&sharpsl_bat, msecs_to_jiffies(500));
}

static void sharpsl_charge_off(void)
{
	dev_dbg(sharpsl_pm.dev, "Turning Charger Off\n");

	sharpsl_pm.machinfo->charge(0);
	sharpsl_pm_led(SHARPSL_LED_OFF);
	sharpsl_pm.charge_mode = CHRG_OFF;

	schedule_delayed_work(&sharpsl_bat, 0);
}

static void sharpsl_charge_error(void)
{
	sharpsl_pm_led(SHARPSL_LED_ERROR);
	sharpsl_pm.machinfo->charge(0);
	sharpsl_pm.charge_mode = CHRG_ERROR;
}

static void sharpsl_charge_toggle(struct work_struct *private_)
{
	dev_dbg(sharpsl_pm.dev, "Toogling Charger at time: %lx\n", jiffies);

	if (!sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_ACIN)) {
		sharpsl_charge_off();
		return;
	} else if ((sharpsl_check_battery_temp() < 0) || (sharpsl_ac_check() < 0)) {
		sharpsl_charge_error();
		return;
	}

	sharpsl_pm_led(SHARPSL_LED_ON);
	sharpsl_pm.machinfo->charge(0);
	mdelay(SHARPSL_CHARGE_WAIT_TIME);
	sharpsl_pm.machinfo->charge(1);

	sharpsl_pm.charge_start_time = jiffies;
}

static void sharpsl_ac_timer(unsigned long data)
{
	int acin = sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_ACIN);

	dev_dbg(sharpsl_pm.dev, "AC Status: %d\n",acin);

	sharpsl_average_clear();
	if (acin && (sharpsl_pm.charge_mode != CHRG_ON))
		sharpsl_charge_on();
	else if (sharpsl_pm.charge_mode == CHRG_ON)
		sharpsl_charge_off();

	schedule_delayed_work(&sharpsl_bat, 0);
}


irqreturn_t sharpsl_ac_isr(int irq, void *dev_id)
{
	/* Delay the event slightly to debounce */
	/* Must be a smaller delay than the chrg_full_isr below */
	mod_timer(&sharpsl_pm.ac_timer, jiffies + msecs_to_jiffies(250));

	return IRQ_HANDLED;
}

static void sharpsl_chrg_full_timer(unsigned long data)
{
	dev_dbg(sharpsl_pm.dev, "Charge Full at time: %lx\n", jiffies);

	sharpsl_pm.full_count++;

	if (!sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_ACIN)) {
		dev_dbg(sharpsl_pm.dev, "Charge Full: AC removed - stop charging!\n");
		if (sharpsl_pm.charge_mode == CHRG_ON)
			sharpsl_charge_off();
	} else if (sharpsl_pm.full_count < 2) {
		dev_dbg(sharpsl_pm.dev, "Charge Full: Count too low\n");
		schedule_delayed_work(&toggle_charger, 0);
	} else if (time_after(jiffies, sharpsl_pm.charge_start_time + SHARPSL_CHARGE_FINISH_TIME)) {
		dev_dbg(sharpsl_pm.dev, "Charge Full: Interrupt generated too slowly - retry.\n");
		schedule_delayed_work(&toggle_charger, 0);
	} else {
		sharpsl_charge_off();
		sharpsl_pm.charge_mode = CHRG_DONE;
		dev_dbg(sharpsl_pm.dev, "Charge Full: Charging Finished\n");
	}
}

/* Charging Finished Interrupt (Not present on Corgi) */
/* Can trigger at the same time as an AC staus change so
   delay until after that has been processed */
irqreturn_t sharpsl_chrg_full_isr(int irq, void *dev_id)
{
	if (sharpsl_pm.flags & SHARPSL_SUSPENDED)
		return IRQ_HANDLED;

	/* delay until after any ac interrupt */
	mod_timer(&sharpsl_pm.chrg_full_timer, jiffies + msecs_to_jiffies(500));

	return IRQ_HANDLED;
}

irqreturn_t sharpsl_fatal_isr(int irq, void *dev_id)
{
	int is_fatal = 0;

	if (!sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_LOCK)) {
		dev_err(sharpsl_pm.dev, "Battery now Unlocked! Suspending.\n");
		is_fatal = 1;
	}

	if (!sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_FATAL)) {
		dev_err(sharpsl_pm.dev, "Fatal Batt Error! Suspending.\n");
		is_fatal = 1;
	}

	if (!(sharpsl_pm.flags & SHARPSL_APM_QUEUED) && is_fatal) {
		sharpsl_pm.flags |= SHARPSL_APM_QUEUED;
		apm_queue_event(APM_CRITICAL_SUSPEND);
	}

	return IRQ_HANDLED;
}

/*
 * Maintain an average of the last 10 readings
 */
#define SHARPSL_CNV_VALUE_NUM    10
static int sharpsl_ad_index;

static void sharpsl_average_clear(void)
{
	sharpsl_ad_index = 0;
}

static int sharpsl_average_value(int ad)
{
	int i, ad_val = 0;
	static int sharpsl_ad[SHARPSL_CNV_VALUE_NUM+1];

	if (sharpsl_pm.battstat.mainbat_status != APM_BATTERY_STATUS_HIGH) {
		sharpsl_ad_index = 0;
		return ad;
	}

	sharpsl_ad[sharpsl_ad_index] = ad;
	sharpsl_ad_index++;
	if (sharpsl_ad_index >= SHARPSL_CNV_VALUE_NUM) {
		for (i=0; i < (SHARPSL_CNV_VALUE_NUM-1); i++)
			sharpsl_ad[i] = sharpsl_ad[i+1];
		sharpsl_ad_index = SHARPSL_CNV_VALUE_NUM - 1;
	}
	for (i=0; i < sharpsl_ad_index; i++)
		ad_val += sharpsl_ad[i];

	return (ad_val / sharpsl_ad_index);
}

/*
 * Take an array of 5 integers, remove the maximum and minimum values
 * and return the average.
 */
static int get_select_val(int *val)
{
	int i, j, k, temp, sum = 0;

	/* Find MAX val */
	temp = val[0];
	j=0;
	for (i=1; i<5; i++) {
		if (temp < val[i]) {
			temp = val[i];
			j = i;
		}
	}

	/* Find MIN val */
	temp = val[4];
	k=4;
	for (i=3; i>=0; i--) {
		if (temp > val[i]) {
			temp = val[i];
			k = i;
		}
	}

	for (i=0; i<5; i++)
		if (i != j && i != k )
			sum += val[i];

	dev_dbg(sharpsl_pm.dev, "Average: %d from values: %d, %d, %d, %d, %d\n", sum/3, val[0], val[1], val[2], val[3], val[4]);

	return (sum/3);
}

static int sharpsl_check_battery_temp(void)
{
	int val, i, buff[5];

	/* Check battery temperature */
	for (i=0; i<5; i++) {
		mdelay(SHARPSL_CHECK_BATTERY_WAIT_TIME_TEMP);
		sharpsl_pm.machinfo->measure_temp(1);
		mdelay(SHARPSL_CHECK_BATTERY_WAIT_TIME_TEMP);
		buff[i] = sharpsl_pm.machinfo->read_devdata(SHARPSL_BATT_TEMP);
		sharpsl_pm.machinfo->measure_temp(0);
	}

	val = get_select_val(buff);

	dev_dbg(sharpsl_pm.dev, "Temperature: %d\n", val);
	if (val > sharpsl_pm.machinfo->charge_on_temp) {
		printk(KERN_WARNING "Not charging: temperature out of limits.\n");
		return -1;
	}

	return 0;
}

static int sharpsl_check_battery_voltage(void)
{
	int val, i, buff[5];

	/* disable charge, enable discharge */
	sharpsl_pm.machinfo->charge(0);
	sharpsl_pm.machinfo->discharge(1);
	mdelay(SHARPSL_WAIT_DISCHARGE_ON);

	if (sharpsl_pm.machinfo->discharge1)
		sharpsl_pm.machinfo->discharge1(1);

	/* Check battery voltage */
	for (i=0; i<5; i++) {
		buff[i] = sharpsl_pm.machinfo->read_devdata(SHARPSL_BATT_VOLT);
		mdelay(SHARPSL_CHECK_BATTERY_WAIT_TIME_VOLT);
	}

	if (sharpsl_pm.machinfo->discharge1)
		sharpsl_pm.machinfo->discharge1(0);

	sharpsl_pm.machinfo->discharge(0);

	val = get_select_val(buff);
	dev_dbg(sharpsl_pm.dev, "Battery Voltage: %d\n", val);

	if (val < sharpsl_pm.machinfo->charge_on_volt)
		return -1;

	return 0;
}

static int sharpsl_ac_check(void)
{
	int temp, i, buff[5];

	for (i=0; i<5; i++) {
		buff[i] = sharpsl_pm.machinfo->read_devdata(SHARPSL_ACIN_VOLT);
		mdelay(SHARPSL_CHECK_BATTERY_WAIT_TIME_ACIN);
	}

	temp = get_select_val(buff);
	dev_dbg(sharpsl_pm.dev, "AC Voltage: %d\n",temp);

	if ((temp > sharpsl_pm.machinfo->charge_acin_high) || (temp < sharpsl_pm.machinfo->charge_acin_low)) {
		dev_err(sharpsl_pm.dev, "Error: AC check failed.\n");
		return -1;
	}

	return 0;
}

#ifdef CONFIG_PM
static int sharpsl_pm_suspend(struct platform_device *pdev, pm_message_t state)
{
	sharpsl_pm.flags |= SHARPSL_SUSPENDED;
	flush_scheduled_work();

	if (sharpsl_pm.charge_mode == CHRG_ON)
		sharpsl_pm.flags |= SHARPSL_DO_OFFLINE_CHRG;
	else
		sharpsl_pm.flags &= ~SHARPSL_DO_OFFLINE_CHRG;

	return 0;
}

static int sharpsl_pm_resume(struct platform_device *pdev)
{
	/* Clear the reset source indicators as they break the bootloader upon reboot */
	RCSR = 0x0f;
	sharpsl_average_clear();
	sharpsl_pm.flags &= ~SHARPSL_APM_QUEUED;
	sharpsl_pm.flags &= ~SHARPSL_SUSPENDED;

	return 0;
}

static void corgi_goto_sleep(unsigned long alarm_time, unsigned int alarm_enable, suspend_state_t state)
{
	dev_dbg(sharpsl_pm.dev, "Time is: %08x\n",RCNR);

	dev_dbg(sharpsl_pm.dev, "Offline Charge Activate = %d\n",sharpsl_pm.flags & SHARPSL_DO_OFFLINE_CHRG);
	/* not charging and AC-IN! */

	if ((sharpsl_pm.flags & SHARPSL_DO_OFFLINE_CHRG) && (sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_ACIN))) {
		dev_dbg(sharpsl_pm.dev, "Activating Offline Charger...\n");
		sharpsl_pm.charge_mode = CHRG_OFF;
		sharpsl_pm.flags &= ~SHARPSL_DO_OFFLINE_CHRG;
		sharpsl_off_charge_battery();
	}

	sharpsl_pm.machinfo->presuspend();

	PEDR = 0xffffffff; /* clear it */

	sharpsl_pm.flags &= ~SHARPSL_ALARM_ACTIVE;
	if ((sharpsl_pm.charge_mode == CHRG_ON) && ((alarm_enable && ((alarm_time - RCNR) > (SHARPSL_BATCHK_TIME_SUSPEND + 30))) || !alarm_enable)) {
		RTSR &= RTSR_ALE;
		RTAR = RCNR + SHARPSL_BATCHK_TIME_SUSPEND;
		dev_dbg(sharpsl_pm.dev, "Charging alarm at: %08x\n",RTAR);
		sharpsl_pm.flags |= SHARPSL_ALARM_ACTIVE;
	} else if (alarm_enable) {
		RTSR &= RTSR_ALE;
		RTAR = alarm_time;
		dev_dbg(sharpsl_pm.dev, "User alarm at: %08x\n",RTAR);
	} else {
		dev_dbg(sharpsl_pm.dev, "No alarms set.\n");
	}

	pxa_pm_enter(state);

	sharpsl_pm.machinfo->postsuspend();

	dev_dbg(sharpsl_pm.dev, "Corgi woken up from suspend: %08x\n",PEDR);
}

static int corgi_enter_suspend(unsigned long alarm_time, unsigned int alarm_enable, suspend_state_t state)
{
	if (!sharpsl_pm.machinfo->should_wakeup(!(sharpsl_pm.flags & SHARPSL_ALARM_ACTIVE) && alarm_enable) )
	{
		if (!(sharpsl_pm.flags & SHARPSL_ALARM_ACTIVE)) {
			dev_dbg(sharpsl_pm.dev, "No user triggered wakeup events and not charging. Strange. Suspend.\n");
			corgi_goto_sleep(alarm_time, alarm_enable, state);
			return 1;
		}
		if(sharpsl_off_charge_battery()) {
			dev_dbg(sharpsl_pm.dev, "Charging. Suspend...\n");
			corgi_goto_sleep(alarm_time, alarm_enable, state);
			return 1;
		}
		dev_dbg(sharpsl_pm.dev, "User triggered wakeup in offline charger.\n");
	}

	if ((!sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_LOCK)) || (sharpsl_fatal_check() < 0) )
	{
		dev_err(sharpsl_pm.dev, "Fatal condition. Suspend.\n");
		corgi_goto_sleep(alarm_time, alarm_enable, state);
		return 1;
	}

	return 0;
}

static int corgi_pxa_pm_enter(suspend_state_t state)
{
	unsigned long alarm_time = RTAR;
	unsigned int alarm_status = ((RTSR & RTSR_ALE) != 0);

	dev_dbg(sharpsl_pm.dev, "SharpSL suspending for first time.\n");

	corgi_goto_sleep(alarm_time, alarm_status, state);

	while (corgi_enter_suspend(alarm_time,alarm_status,state))
		{}

	if (sharpsl_pm.machinfo->earlyresume)
		sharpsl_pm.machinfo->earlyresume();

	dev_dbg(sharpsl_pm.dev, "SharpSL resuming...\n");

	return 0;
}
#endif


/*
 * Check for fatal battery errors
 * Fatal returns -1
 */
static int sharpsl_fatal_check(void)
{
	int buff[5], temp, i, acin;

	dev_dbg(sharpsl_pm.dev, "sharpsl_fatal_check entered\n");

	/* Check AC-Adapter */
	acin = sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_ACIN);

	if (acin && (sharpsl_pm.charge_mode == CHRG_ON)) {
		sharpsl_pm.machinfo->charge(0);
		udelay(100);
		sharpsl_pm.machinfo->discharge(1);	/* enable discharge */
		mdelay(SHARPSL_WAIT_DISCHARGE_ON);
	}

	if (sharpsl_pm.machinfo->discharge1)
		sharpsl_pm.machinfo->discharge1(1);

	/* Check battery : check inserting battery ? */
	for (i=0; i<5; i++) {
		buff[i] = sharpsl_pm.machinfo->read_devdata(SHARPSL_BATT_VOLT);
		mdelay(SHARPSL_CHECK_BATTERY_WAIT_TIME_VOLT);
	}

	if (sharpsl_pm.machinfo->discharge1)
		sharpsl_pm.machinfo->discharge1(0);

	if (acin && (sharpsl_pm.charge_mode == CHRG_ON)) {
		udelay(100);
		sharpsl_pm.machinfo->charge(1);
		sharpsl_pm.machinfo->discharge(0);
	}

	temp = get_select_val(buff);
	dev_dbg(sharpsl_pm.dev, "sharpsl_fatal_check: acin: %d, discharge voltage: %d, no discharge: %d\n", acin, temp, sharpsl_pm.machinfo->read_devdata(SHARPSL_BATT_VOLT));

	if ((acin && (temp < sharpsl_pm.machinfo->fatal_acin_volt)) ||
			(!acin && (temp < sharpsl_pm.machinfo->fatal_noacin_volt)))
		return -1;
	return 0;
}

static int sharpsl_off_charge_error(void)
{
	dev_err(sharpsl_pm.dev, "Offline Charger: Error occured.\n");
	sharpsl_pm.machinfo->charge(0);
	sharpsl_pm_led(SHARPSL_LED_ERROR);
	sharpsl_pm.charge_mode = CHRG_ERROR;
	return 1;
}

/*
 * Charging Control while suspended
 * Return 1 - go straight to sleep
 * Return 0 - sleep or wakeup depending on other factors
 */
static int sharpsl_off_charge_battery(void)
{
	int time;

	dev_dbg(sharpsl_pm.dev, "Charge Mode: %d\n", sharpsl_pm.charge_mode);

	if (sharpsl_pm.charge_mode == CHRG_OFF) {
		dev_dbg(sharpsl_pm.dev, "Offline Charger: Step 1\n");

		/* AC Check */
		if ((sharpsl_ac_check() < 0) || (sharpsl_check_battery_temp() < 0))
			return sharpsl_off_charge_error();

		/* Start Charging */
		sharpsl_pm_led(SHARPSL_LED_ON);
		sharpsl_pm.machinfo->charge(0);
		mdelay(SHARPSL_CHARGE_WAIT_TIME);
		sharpsl_pm.machinfo->charge(1);

		sharpsl_pm.charge_mode = CHRG_ON;
		sharpsl_pm.full_count = 0;

		return 1;
	} else if (sharpsl_pm.charge_mode != CHRG_ON) {
		return 1;
	}

	if (sharpsl_pm.full_count == 0) {
		int time;

		dev_dbg(sharpsl_pm.dev, "Offline Charger: Step 2\n");

		if ((sharpsl_check_battery_temp() < 0) || (sharpsl_check_battery_voltage() < 0))
			return sharpsl_off_charge_error();

		sharpsl_pm.machinfo->charge(0);
		mdelay(SHARPSL_CHARGE_WAIT_TIME);
		sharpsl_pm.machinfo->charge(1);
		sharpsl_pm.charge_mode = CHRG_ON;

		mdelay(SHARPSL_CHARGE_CO_CHECK_TIME);

		time = RCNR;
		while(1) {
			/* Check if any wakeup event had occured */
			if (sharpsl_pm.machinfo->charger_wakeup() != 0)
				return 0;
			/* Check for timeout */
			if ((RCNR - time) > SHARPSL_WAIT_CO_TIME)
				return 1;
			if (sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_CHRGFULL)) {
				dev_dbg(sharpsl_pm.dev, "Offline Charger: Charge full occured. Retrying to check\n");
	   			sharpsl_pm.full_count++;
				sharpsl_pm.machinfo->charge(0);
				mdelay(SHARPSL_CHARGE_WAIT_TIME);
				sharpsl_pm.machinfo->charge(1);
				return 1;
			}
		}
	}

	dev_dbg(sharpsl_pm.dev, "Offline Charger: Step 3\n");

	mdelay(SHARPSL_CHARGE_CO_CHECK_TIME);

	time = RCNR;
	while(1) {
		/* Check if any wakeup event had occured */
		if (sharpsl_pm.machinfo->charger_wakeup() != 0)
			return 0;
		/* Check for timeout */
		if ((RCNR-time) > SHARPSL_WAIT_CO_TIME) {
			if (sharpsl_pm.full_count > SHARPSL_CHARGE_RETRY_CNT) {
				dev_dbg(sharpsl_pm.dev, "Offline Charger: Not charged sufficiently. Retrying.\n");
				sharpsl_pm.full_count = 0;
			}
			sharpsl_pm.full_count++;
			return 1;
		}
		if (sharpsl_pm.machinfo->read_devdata(SHARPSL_STATUS_CHRGFULL)) {
			dev_dbg(sharpsl_pm.dev, "Offline Charger: Charging complete.\n");
			sharpsl_pm_led(SHARPSL_LED_OFF);
			sharpsl_pm.machinfo->charge(0);
			sharpsl_pm.charge_mode = CHRG_DONE;
			return 1;
		}
	}
}


static ssize_t battery_percentage_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n",sharpsl_pm.battstat.mainbat_percent);
}

static ssize_t battery_voltage_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n",sharpsl_pm.battstat.mainbat_voltage);
}

static DEVICE_ATTR(battery_percentage, 0444, battery_percentage_show, NULL);
static DEVICE_ATTR(battery_voltage, 0444, battery_voltage_show, NULL);

extern void (*apm_get_power_status)(struct apm_power_info *);

static void sharpsl_apm_get_power_status(struct apm_power_info *info)
{
	info->ac_line_status = sharpsl_pm.battstat.ac_status;

	if (sharpsl_pm.charge_mode == CHRG_ON)
		info->battery_status = APM_BATTERY_STATUS_CHARGING;
	else
		info->battery_status = sharpsl_pm.battstat.mainbat_status;

	info->battery_flag = (1 << info->battery_status);
	info->battery_life = sharpsl_pm.battstat.mainbat_percent;
}

static struct pm_ops sharpsl_pm_ops = {
	.pm_disk_mode	= PM_DISK_FIRMWARE,
	.prepare	= pxa_pm_prepare,
	.enter		= corgi_pxa_pm_enter,
	.finish		= pxa_pm_finish,
};

static int __init sharpsl_pm_probe(struct platform_device *pdev)
{
	if (!pdev->dev.platform_data)
		return -EINVAL;

	sharpsl_pm.dev = &pdev->dev;
	sharpsl_pm.machinfo = pdev->dev.platform_data;
	sharpsl_pm.charge_mode = CHRG_OFF;
	sharpsl_pm.flags = 0;

	init_timer(&sharpsl_pm.ac_timer);
	sharpsl_pm.ac_timer.function = sharpsl_ac_timer;

	init_timer(&sharpsl_pm.chrg_full_timer);
	sharpsl_pm.chrg_full_timer.function = sharpsl_chrg_full_timer;

	led_trigger_register_simple("sharpsl-charge", &sharpsl_charge_led_trigger);

	sharpsl_pm.machinfo->init();

	device_create_file(&pdev->dev, &dev_attr_battery_percentage);
	device_create_file(&pdev->dev, &dev_attr_battery_voltage);

	apm_get_power_status = sharpsl_apm_get_power_status;

	pm_set_ops(&sharpsl_pm_ops);

	mod_timer(&sharpsl_pm.ac_timer, jiffies + msecs_to_jiffies(250));

	return 0;
}

static int sharpsl_pm_remove(struct platform_device *pdev)
{
	pm_set_ops(NULL);

	device_remove_file(&pdev->dev, &dev_attr_battery_percentage);
	device_remove_file(&pdev->dev, &dev_attr_battery_voltage);

	led_trigger_unregister_simple(sharpsl_charge_led_trigger);

	sharpsl_pm.machinfo->exit();

	del_timer_sync(&sharpsl_pm.chrg_full_timer);
	del_timer_sync(&sharpsl_pm.ac_timer);

	return 0;
}

static struct platform_driver sharpsl_pm_driver = {
	.probe		= sharpsl_pm_probe,
	.remove		= sharpsl_pm_remove,
	.suspend	= sharpsl_pm_suspend,
	.resume		= sharpsl_pm_resume,
	.driver		= {
		.name		= "sharpsl-pm",
	},
};

static int __devinit sharpsl_pm_init(void)
{
	return platform_driver_register(&sharpsl_pm_driver);
}

static void sharpsl_pm_exit(void)
{
 	platform_driver_unregister(&sharpsl_pm_driver);
}

late_initcall(sharpsl_pm_init);
module_exit(sharpsl_pm_exit);
