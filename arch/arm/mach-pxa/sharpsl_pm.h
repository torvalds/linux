/*
 * SharpSL Battery/PM Driver
 *
 * Copyright (c) 2004-2005 Richard Purdie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef _MACH_SHARPSL_PM
#define _MACH_SHARPSL_PM

struct sharpsl_charger_machinfo {
	void (*init)(void);
	void (*exit)(void);
	int gpio_acin;
	int gpio_batfull;
	int batfull_irq;
	int gpio_batlock;
	int gpio_fatal;
	void (*discharge)(int);
	void (*discharge1)(int);
	void (*charge)(int);
	void (*measure_temp)(int);
	void (*presuspend)(void);
	void (*postsuspend)(void);
	void (*earlyresume)(void);
	unsigned long (*read_devdata)(int);
#define SHARPSL_BATT_VOLT       1
#define SHARPSL_BATT_TEMP       2
#define SHARPSL_ACIN_VOLT       3
#define SHARPSL_STATUS_ACIN     4
#define SHARPSL_STATUS_LOCK     5
#define SHARPSL_STATUS_CHRGFULL 6
#define SHARPSL_STATUS_FATAL    7
	bool (*charger_wakeup)(void);
	int (*should_wakeup)(unsigned int resume_on_alarm);
	void (*backlight_limit)(int);
	int (*backlight_get_status) (void);
	int charge_on_volt;
	int charge_on_temp;
	int charge_acin_high;
	int charge_acin_low;
	int fatal_acin_volt;
	int fatal_noacin_volt;
	int bat_levels;
	struct battery_thresh *bat_levels_noac;
	struct battery_thresh *bat_levels_acin;
	struct battery_thresh *bat_levels_noac_bl;
	struct battery_thresh *bat_levels_acin_bl;
	int status_high_acin;
	int status_low_acin;
	int status_high_noac;
	int status_low_noac;
};

struct battery_thresh {
	int voltage;
	int percentage;
};

struct battery_stat {
	int ac_status;         /* APM AC Present/Not Present */
	int mainbat_status;    /* APM Main Battery Status */
	int mainbat_percent;   /* Main Battery Percentage Charge */
	int mainbat_voltage;   /* Main Battery Voltage */
};

struct sharpsl_pm_status {
	struct device *dev;
	struct timer_list ac_timer;
	struct timer_list chrg_full_timer;

	int charge_mode;
#define CHRG_ERROR    (-1)
#define CHRG_OFF      (0)
#define CHRG_ON       (1)
#define CHRG_DONE     (2)

	unsigned int flags;
#define SHARPSL_SUSPENDED       (1 << 0)  /* Device is Suspended */
#define SHARPSL_ALARM_ACTIVE    (1 << 1)  /* Alarm is for charging event (not user) */
#define SHARPSL_BL_LIMIT        (1 << 2)  /* Backlight Intensity Limited */
#define SHARPSL_APM_QUEUED      (1 << 3)  /* APM Event Queued */
#define SHARPSL_DO_OFFLINE_CHRG (1 << 4)  /* Trigger the offline charger */

	int full_count;
	unsigned long charge_start_time;
	struct sharpsl_charger_machinfo *machinfo;
	struct battery_stat battstat;
};

extern struct sharpsl_pm_status sharpsl_pm;

extern struct battery_thresh sharpsl_battery_levels_acin[];
extern struct battery_thresh sharpsl_battery_levels_noac[];

#define SHARPSL_LED_ERROR  2
#define SHARPSL_LED_ON     1
#define SHARPSL_LED_OFF    0

void sharpsl_battery_kick(void);
void sharpsl_pm_led(int val);

/* MAX1111 Channel Definitions */
#define MAX1111_BATT_VOLT   4u
#define MAX1111_BATT_TEMP   2u
#define MAX1111_ACIN_VOLT   6u
int sharpsl_pm_pxa_read_max1111(int channel);

void corgi_lcd_limit_intensity(int limit);
#endif
