/*
 * rk818/rk819 battery driver
 *
 *  Copyright (C) 2014 Rockchip Electronics Co., Ltd
 *  Author: zhangqing <zhangqing@rock-chips.com>
 *	    chenjh    <chenjh@rock-chips.com>
 *          Andy Yan  <andy.yan@rock-chips.com>
 *
 *  Copyright (C) 2008-2009 Texas Instruments, Inc.
 *  Author: Texas Instruments, Inc.
 *
 * Copyright (C) 2008-2009 Texas Instruments, Inc.
 * Author: Texas Instruments, Inc.
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Author: zhangqing <zhangqing@rock-chips.com>
 * Copyright (C) 2014-2015 Intel Mobile Communications GmbH
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/mfd/rk818.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/wakelock.h>
#include <linux/of_gpio.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/usb/phy.h>
#include <linux/fb.h>

#if defined(CONFIG_X86_INTEL_SOFIA)
#include <linux/usb/phy-intel.h>
#else
#include <linux/power/rk_usbbc.h>
#endif
#include "rk818_battery.h"

/* if you  want to disable, don't set it as 0,
just be: "static int dbg_enable;" is ok*/

static int dbg_enable;
#define RK818_SYS_DBG 1

module_param_named(dbg_level, dbg_enable, int, 0644);

#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

#define DEFAULT_BAT_RES			135
#define DEFAULT_CHRG_VOL		4200
#define DEFAULT_CHRG_CUR		1000
#define DEFAULT_INPUT_CUR		1400
#define DEFAULT_SLP_ENTER_CUR		600
#define DEFAULT_SLP_EXIT_CUR		600

#define DSOC_DISCHRG_EMU_CURR		1200
#define DSOC_DISCHRG_FAST_DEC_SEC	120	/*seconds*/
#define DSOC_DISCHRG_FAST_EER_RANGE	10
#define DSOC_CHRG_FAST_CALIB_CURR_MAX	400	/*mA*/
#define DSOC_CHRG_FAST_INC_SEC		120	/*seconds*/
#define DSOC_CHRG_FAST_EER_RANGE	10
#define DSOC_CHRG_EMU_CURR		1200
#define DSOC_CHRG_TERM_CURR		600
#define DSOC_CHRG_TERM_VOL		4100
#define	CHRG_FINISH_VOL			4100

/*realtime RSOC calib param*/
#define RSOC_DISCHRG_ERR_LOWER	40
#define RSOC_DISCHRG_ERR_UPPER	50
#define RSOC_ERR_CHCK_CNT	15
#define RSOC_COMPS		20	/*compensation*/
#define RSOC_CALIB_CURR_MAX	900	/*mA*/
#define RSOC_CALIB_DISCHRGR_TIME	3	/*min*/

#define RSOC_RESUME_ERR		10
#define REBOOT_INTER_MIN	1

#define INTERPOLATE_MAX		1000
#define MAX_INT			0x7FFF
#define TIME_10MIN_SEC		600

#define CHRG_VOL_SHIFT		4
#define CHRG_ILIM_SHIFT		0
#define CHRG_ICUR_SHIFT		0
#define DEF_CHRG_VOL		CHRG_VOL4200
#define DEF_CHRG_CURR_SEL	CHRG_CUR1400mA
#define DEF_CHRG_CURR_LMT	ILIM_2000MA

/*TEST_POWER_MODE params*/
#define TEST_CURRENT		1000
#define TEST_VOLTAGE		3800
#define TEST_SOC		66
#define TEST_STATUS		POWER_SUPPLY_STATUS_CHARGING
#define TEST_PRESET		1
#define TEST_AC_ONLINE		1
#define TEST_USB_ONLINE		0

#define ZERO_ALGOR_THRESD	3800
#define DISCHRG_ZERO_MODE	1
#define DISCHRG_NORMAL_MODE	0
#define DEF_LAST_ZERO_MODE_SOC	-1

#define	DISCHRG_MODE		0
#define	CHRG_MODE		1

#define	TREND_STAT_FLAT		0
#define	TREND_STAT_DOWN		-1
#define	TREND_STAT_UP		1
#define	TREND_CAP_DIFF		5

#define	POWER_ON_SEC_BASE	1
#define MINUTE			60

#define SLP_CURR_MAX		40
#define SLP_CURR_MIN		6
#define WAKEUP_SEC_THRESD	40
#define CHRG_TIME_STEP		(60)
#define DISCHRG_TIME_STEP_0	(30 * 60)
#define DISCHRG_TIME_STEP_1	(60 * 60)

#define DEF_PCB_OFFSET		42
#define DEF_CAL_OFFSET		0x832
#define DEF_PWRPATH_RES		50
#define SEC_TO_EMPTY		300
#define DSOC_CHRG_FINISH_CURR	1100
#define SLP_CHRG_CURR		1000
#define SLP_DSOC_VOL_THRESD	3600
/*if voltage is lower than this thresd,
   we consider it as invalid
 */
#define INVALID_VOL_THRESD	2500
#define PWR_OFF_THRESD		3400
#define MIN_ZERO_ACCURACY	5	/*0.01%*/
#define MIN_ROUND_ACCURACY	1

#define MAX_FCC			10000
#define MIN_FCC			500
/*
 * the following table value depends on datasheet
 */
int CHRG_V_LMT[] = {4050, 4100, 4150, 4200, 4250, 4300, 4350};

int CHRG_I_CUR[] = {1000, 1200, 1400, 1600, 1800, 2000,
		   2250, 2400, 2600, 2800, 3000};

int CHRG_I_LMT[] = {450, 800, 850, 1000, 1250, 1500, 1750,
		   2000, 2250, 2500, 2750, 3000};

u8 CHRG_CVCC_HOUR[] = {4, 5, 6, 8, 10, 12, 14, 16};

#define RK818_DC_IN		0
#define RK818_DC_OUT		1

#define	OCV_VALID_SHIFT		(0)
#define	OCV_CALIB_SHIFT		(1)
#define FIRST_PWRON_SHIFT	(2)

#define SEC_TO_MIN(x)		((x) / 60)

struct rk81x_battery {
	struct device			*dev;
	struct cell_state		cell;
	struct power_supply		bat;
	struct power_supply		ac;
	struct power_supply		usb;
	struct delayed_work		work;
	struct rk818			*rk818;
	struct pinctrl                  *pinctrl;
	struct pinctrl_state            *pins_default;

	struct battery_platform_data	*pdata;

	int				dc_det_pin;
	int				dc_det_level;
	int				dc_det_irq;
	int				irq;
	int				ac_online;
	int				usb_online;
	int				otg_online;
	int				dc_online;
	int				psy_status;
	int				current_avg;
	int				current_offset;

	uint16_t			voltage;
	uint16_t			voltage_ocv;
	uint16_t			relax_voltage;
	u8				chrg_status;
	u8				slp_chrg_status;

	u8				otg_status;
	int				pcb_ioffset;
	bool				pcb_ioffset_updated;

	int				design_capacity;
	int				fcc;
	int				qmax;
	int				remain_capacity;
	int				nac;
	int				temp_nac;
	int				dsoc;
	int				display_soc;
	int				rsoc;
	int				trend_start_cap;

	int				est_ocv_vol;
	int				est_ocv_soc;
	u8				err_chck_cnt;
	int				err_soc_sum;
	int				bat_res_update_cnt;
	int				soc_counter;
	int				dod0;
	int				dod0_status;
	int				dod0_voltage;
	int				dod0_capacity;
	unsigned long			dod0_time;
	u8				dod0_level;
	int				adjust_cap;

	int				enter_flatzone;
	int				exit_flatzone;

	int				time2empty;
	int				time2full;

	int				*ocv_table;
	int				*res_table;

	int				current_k;/* (ICALIB0, ICALIB1) */
	int				current_b;

	int				voltage_k;/* VCALIB0 VCALIB1 */
	int				voltage_b;
	bool				enter_finish;
	int				zero_timeout_cnt;
	int				zero_old_remain_cap;

	int				line_k;
	u8				check_count;

	int				charge_smooth_time;
	int				sum_suspend_cap;
	int				suspend_cap;

	unsigned long			suspend_time_sum;

	int				suspend_rsoc;
	int				slp_psy_status;
	int				suspend_charge_current;
	int				resume_soc;
	int				bat_res;
	bool				charge_smooth_status;
	bool				discharge_smooth_status;

	u32                             plug_in_min;
	u32                             plug_out_min;
	u32                             finish_sig_min;

	struct notifier_block		battery_nb;
	struct usb_phy			*usb_phy;
	struct notifier_block		usb_nb;
	struct notifier_block           fb_nb;
	int                             fb_blank;
	int				early_resume;
	int				s2r; /*suspend to resume*/
	struct workqueue_struct		*wq;
	struct delayed_work		battery_monitor_work;
	struct delayed_work		otg_check_work;
	struct delayed_work             usb_phy_delay_work;
	struct delayed_work		chrg_term_mode_switch_work;
	struct delayed_work		ac_usb_check_work;
	struct delayed_work		dc_det_check_work;
	enum bc_port_type		charge_otg;
	int				ma;

	struct wake_lock		resume_wake_lock;
	unsigned long			plug_in_base;
	unsigned long			plug_out_base;
	unsigned long			finish_sig_base;
	unsigned long			power_on_base;

	int				chrg_time2full;
	int				chrg_cap2full;

	bool				is_first_poweron;

	int				fg_drv_mode;
	int				debug_finish_real_soc;
	int				debug_finish_temp_soc;
	int				chrg_min[10];
	int				chrg_v_lmt;
	int				chrg_i_lmt;
	int				chrg_i_cur;
	uint16_t			pwroff_min;
	unsigned long			wakeup_sec;
	u32				delta_vol_smooth;
	unsigned long			dischrg_normal_base;
	unsigned long			dischrg_emu_base;
	unsigned long			chrg_normal_base;
	unsigned long			chrg_term_base;
	unsigned long			chrg_emu_base;
	unsigned long			chrg_finish_base;
	unsigned long			fcc_update_sec;
	int				loader_charged;
	u8				dischrg_algorithm_mode;
	int				last_zero_mode_dsoc;
	u8				current_mode;
	unsigned long			dischrg_save_sec;
	unsigned long			chrg_save_sec;
	struct timeval			suspend_rtc_base;
};

u32 support_usb_adp, support_dc_adp, power_dc2otg;

#define to_device_info(x) container_of((x), \
				struct rk81x_battery, bat)

#define to_ac_device_info(x) container_of((x), \
				struct rk81x_battery, ac)

#define to_usb_device_info(x) container_of((x), \
				struct rk81x_battery, usb)

static int loader_charged;

static int __init rk81x_bat_loader_charged(char *__unused)
{
	loader_charged = 1;

	pr_info("battery charged in loader\n");

	return 0;
}
__setup("loader_charged", rk81x_bat_loader_charged);

static u64 get_runtime_sec(void)
{
	struct timespec ts;

	get_monotonic_boottime(&ts);
	return ts.tv_sec;
}

static inline unsigned long  BASE_TO_SEC(unsigned long x)
{
	if (x)
		return (get_runtime_sec() > x) ? (get_runtime_sec() - x) : 0;
	else
		return 0;
}

static inline unsigned long BASE_TO_MIN(unsigned long x)
{
	return  BASE_TO_SEC(x) / 60;
}

static bool rk81x_bat_support_adp_type(enum hw_support_adp type)
{
	bool bl = false;

	switch (type) {
	case HW_ADP_TYPE_USB:
		if (support_usb_adp)
			bl = true;
		break;
	case HW_ADP_TYPE_DC:
		if (support_dc_adp)
			bl = true;
		break;
	case HW_ADP_TYPE_DUAL:
		if (support_usb_adp && support_dc_adp)
			bl = true;
		break;
	default:
			break;
	}

	return bl;
}

static bool rk81x_chrg_online(struct rk81x_battery *di)
{
	return di->usb_online || di->ac_online;
}

static u32 interpolate(int value, u32 *table, int size)
{
	uint8_t i;
	uint16_t d;

	for (i = 0; i < size; i++) {
		if (value < table[i])
			break;
	}

	if ((i > 0) && (i < size)) {
		d = (value - table[i-1]) * (INTERPOLATE_MAX / (size - 1));
		d /= table[i] - table[i-1];
		d = d + (i-1) * (INTERPOLATE_MAX / (size - 1));
	} else {
		d = i * ((INTERPOLATE_MAX + size / 2) / size);
	}

	if (d > 1000)
		d = 1000;

	return d;
}

/* Returns (a * b) / c */
static int32_t ab_div_c(u32 a, u32 b, u32 c)
{
	bool sign;
	u32 ans = MAX_INT;
	int32_t tmp;

	sign = ((((a^b)^c) & 0x80000000) != 0);

	if (c != 0) {
		if (sign)
			c = -c;

		tmp = (a * b + (c >> 1)) / c;

		if (tmp < MAX_INT)
			ans = tmp;
	}

	if (sign)
		ans = -ans;

	return ans;
}

static int div(int val)
{
	return (val == 0) ? 1 : val;
}

static int rk81x_bat_read(struct rk81x_battery *di, u8 reg,
			  u8 buf[], unsigned len)
{
	int ret = -1;
	int i;

	for (i = 0; ret < 0 && i < 3; i++) {
		ret = rk818_i2c_read(di->rk818, reg, len, buf);
		if (ret < 0)
			dev_err(di->dev, "read reg:0x%02x failed\n", reg);
	}

	return (ret < 0) ? ret : 0;
}

static int rk81x_bat_write(struct rk81x_battery *di, u8 reg,
			   u8 const buf[], unsigned len)
{
	int ret = -1;
	int i;

	for (i = 0; ret < 0 && i < 3; i++) {
		ret = rk818_i2c_write(di->rk818, reg, (int)len, *buf);
		if (ret < 0)
			dev_err(di->dev, "write reg:0x%02x failed\n", reg);
	}

	return (ret < 0) ? ret : 0;
}

static int rk81x_bat_set_bit(struct rk81x_battery *di, u8 reg, u8 shift)
{
	int ret = -1;
	int i;

	for (i = 0; ret < 0 && i < 3; i++) {
		ret = rk818_set_bits(di->rk818, reg, 1 << shift, 1 << shift);
		if (ret < 0)
			dev_err(di->dev, "set reg:0x%02x failed\n", reg);
	}

	return ret;
}

static int rk81x_bat_clr_bit(struct rk81x_battery *di, u8 reg, u8 shift)
{
	int ret = -1;
	int i;

	for (i = 0; ret < 0 && i < 3; i++) {
		ret = rk818_set_bits(di->rk818, reg, 1 << shift, 0 << shift);
		if (ret < 0)
			dev_err(di->dev, "set reg:0x%02x failed\n", reg);
	}

	return ret;
}

static u8 rk81x_bat_read_bit(struct rk81x_battery *di, u8 reg, u8 shift)
{
	u8 buf;
	u8 val;

	rk81x_bat_read(di, reg, &buf, 1);
	val = (buf & BIT(shift)) >> shift;
	return val;
}

static void rk81x_dbg_dmp_gauge_regs(struct rk81x_battery *di)
{
	int i = 0;
	u8 buf;

	DBG("%s dump charger register start:\n", __func__);
	for (i = 0xAC; i < 0xEE; i++) {
		rk81x_bat_read(di, i, &buf, 1);
		DBG("0x%02x : 0x%02x\n", i, buf);
	}
	DBG("demp end!\n");
}

static void rk81x_dbg_dmp_charger_regs(struct rk81x_battery *di)
{
	int i = 0;
	char buf;

	DBG("%s dump the register start:\n", __func__);
	for (i = 0x99; i < 0xAB; i++) {
		rk81x_bat_read(di, i, &buf, 1);
		DBG(" the register is  0x%02x, the value is 0x%02x\n", i, buf);
	}
	DBG("demp end!\n");
}

static void rk81x_bat_reset_zero_var(struct rk81x_battery *di)
{
	di->dischrg_algorithm_mode = DISCHRG_NORMAL_MODE;
	di->last_zero_mode_dsoc = DEF_LAST_ZERO_MODE_SOC;
}

static void rk81x_bat_capacity_init_post(struct rk81x_battery *di)
{
	rk81x_bat_reset_zero_var(di);
	di->trend_start_cap = di->remain_capacity;
}

static void rk81x_bat_capacity_init(struct rk81x_battery *di, u32 capacity)
{
	u8 buf;
	u32 capacity_ma;
	int delta_cap;

	delta_cap = capacity - di->remain_capacity;
	if (!delta_cap)
		return;

	di->adjust_cap += delta_cap;

	capacity_ma = capacity * 2390;/* 2134;//36*14/900*4096/521*500; */
	do {
		buf = (capacity_ma >> 24) & 0xff;
		rk81x_bat_write(di, GASCNT_CAL_REG3, &buf, 1);
		buf = (capacity_ma >> 16) & 0xff;
		rk81x_bat_write(di, GASCNT_CAL_REG2, &buf, 1);
		buf = (capacity_ma >> 8) & 0xff;
		rk81x_bat_write(di, GASCNT_CAL_REG1, &buf, 1);
		buf = (capacity_ma & 0xff) | 0x01;
		rk81x_bat_write(di, GASCNT_CAL_REG0, &buf, 1);
		rk81x_bat_read(di, GASCNT_CAL_REG0, &buf, 1);

	} while (buf == 0);

	if (di->chrg_status != CHARGE_FINISH || di->dod0_status == 1)
		dev_dbg(di->dev, "update capacity :%d--remain_cap:%d\n",
			capacity, di->remain_capacity);
}

#if RK818_SYS_DBG
/*
 * interface for debug: do rk81x_bat_first_pwron() without unloading battery
 */
static ssize_t bat_calib_read(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct power_supply *psy_bat = dev_get_drvdata(dev);
	struct rk81x_battery *di = to_device_info(psy_bat);
	int val;

	val = rk81x_bat_read_bit(di, MISC_MARK_REG, OCV_CALIB_SHIFT);

	return sprintf(buf, "%d\n", val);
}

static ssize_t bat_calib_write(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	u8 val;
	int ret;
	struct power_supply *psy_bat = dev_get_drvdata(dev);
	struct rk81x_battery *di = to_device_info(psy_bat);

	ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val)
		rk81x_bat_set_bit(di, MISC_MARK_REG, OCV_CALIB_SHIFT);
	else
		rk81x_bat_clr_bit(di, MISC_MARK_REG, OCV_CALIB_SHIFT);
	return count;
}

/*
 * interface for debug: force battery to over discharge
 */
static ssize_t bat_test_power_read(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct power_supply *psy_bat = dev_get_drvdata(dev);
	struct rk81x_battery *di = to_device_info(psy_bat);

	return sprintf(buf, "%d\n", di->fg_drv_mode);
}

static ssize_t bat_test_power_write(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	u8 val;
	int ret;
	struct power_supply *psy_bat = dev_get_drvdata(dev);
	struct rk81x_battery *di = to_device_info(psy_bat);

	ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val == 1)
		di->fg_drv_mode = TEST_POWER_MODE;
	else
		di->fg_drv_mode = FG_NORMAL_MODE;

	return count;
}

static ssize_t bat_fcc_read(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct power_supply *psy_bat = dev_get_drvdata(dev);
	struct rk81x_battery *di = to_device_info(psy_bat);

	return sprintf(buf, "%d\n", di->fcc);
}

static ssize_t bat_fcc_write(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	u16 val;
	int ret;
	struct power_supply *psy_bat = dev_get_drvdata(dev);
	struct rk81x_battery *di = to_device_info(psy_bat);

	ret = kstrtou16(buf, 0, &val);
	if (ret < 0)
		return ret;

	di->fcc = val;

	return count;
}

static ssize_t bat_dsoc_read(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct power_supply *psy_bat = dev_get_drvdata(dev);
	struct rk81x_battery *di = to_device_info(psy_bat);

	return sprintf(buf, "%d\n", di->dsoc);
}

static ssize_t bat_dsoc_write(struct device *dev,
			      struct device_attribute *attr,
			     const char *buf, size_t count)
{
	u8 val;
	int ret;
	struct power_supply *psy_bat = dev_get_drvdata(dev);
	struct rk81x_battery *di = to_device_info(psy_bat);

	ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
		return ret;

	di->dsoc = val;

	return count;
}

static ssize_t bat_rsoc_read(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct power_supply *psy_bat = dev_get_drvdata(dev);
	struct rk81x_battery *di = to_device_info(psy_bat);

	return sprintf(buf, "%d\n", di->rsoc);
}

static ssize_t bat_rsoc_write(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	u8 val;
	int ret;
	u32 capacity;
	struct power_supply *psy_bat = dev_get_drvdata(dev);
	struct rk81x_battery *di = to_device_info(psy_bat);

	ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
		return ret;

	capacity = di->fcc * val / 100;
	rk81x_bat_capacity_init(di, capacity);
	rk81x_bat_capacity_init_post(di);

	return count;
}

static ssize_t bat_remain_cap_read(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct power_supply *psy_bat = dev_get_drvdata(dev);
	struct rk81x_battery *di = to_device_info(psy_bat);

	return sprintf(buf, "%d\n", di->remain_capacity);
}

static struct device_attribute rk818_bat_attr[] = {
	__ATTR(fcc, 0664, bat_fcc_read, bat_fcc_write),
	__ATTR(dsoc, 0664, bat_dsoc_read, bat_dsoc_write),
	__ATTR(rsoc, 0664, bat_rsoc_read, bat_rsoc_write),
	__ATTR(remain_capacity, 0664, bat_remain_cap_read, NULL),
	__ATTR(test_power, 0664, bat_test_power_read, bat_test_power_write),
	__ATTR(calib, 0664, bat_calib_read, bat_calib_write),
};
#endif

static int rk81x_bat_gauge_enable(struct rk81x_battery *di)
{
	int ret;
	u8 buf;

	ret = rk81x_bat_read(di, TS_CTRL_REG, &buf, 1);
	if (ret < 0) {
		dev_err(di->dev, "error reading TS_CTRL_REG");
		return ret;
	}

	buf |= GG_EN;
	rk81x_bat_write(di, TS_CTRL_REG, &buf, 1);

	return 0;
}

static void rk81x_bat_save_level(struct  rk81x_battery *di, u8 save_soc)
{
	rk81x_bat_write(di, UPDAT_LEVE_REG, &save_soc, 1);
}

static u8 rk81x_bat_get_level(struct  rk81x_battery *di)
{
	u8 soc;

	rk81x_bat_read(di, UPDAT_LEVE_REG, &soc, 1);

	return soc;
}

static int rk81x_bat_get_vcalib0(struct rk81x_battery *di)
{
	int ret;
	int temp = 0;
	u8 buf;

	ret = rk81x_bat_read(di, VCALIB0_REGL, &buf, 1);
	temp = buf;
	ret = rk81x_bat_read(di, VCALIB0_REGH, &buf, 1);
	temp |= buf << 8;

	DBG("%s voltage0 offset vale is %d\n", __func__, temp);
	return temp;
}

static int rk81x_bat_get_vcalib1(struct  rk81x_battery *di)
{
	int ret;
	int temp = 0;
	u8 buf;

	ret = rk81x_bat_read(di, VCALIB1_REGL, &buf, 1);
	temp = buf;
	ret = rk81x_bat_read(di, VCALIB1_REGH, &buf, 1);
	temp |= buf << 8;

	DBG("%s voltage1 offset vale is %d\n", __func__, temp);
	return temp;
}

static int rk81x_bat_get_ioffset(struct rk81x_battery *di)
{
	int ret;
	int temp = 0;
	u8 buf;

	ret = rk81x_bat_read(di, IOFFSET_REGL, &buf, 1);
	temp = buf;
	ret = rk81x_bat_read(di, IOFFSET_REGH, &buf, 1);
	temp |= buf << 8;

	return temp;
}

static uint16_t rk81x_bat_get_cal_offset(struct rk81x_battery *di)
{
	int ret;
	uint16_t temp = 0;
	u8 buf;

	ret = rk81x_bat_read(di, CAL_OFFSET_REGL, &buf, 1);
	temp = buf;
	ret = rk81x_bat_read(di, CAL_OFFSET_REGH, &buf, 1);
	temp |= buf << 8;

	return temp;
}

static int rk81x_bat_set_cal_offset(struct rk81x_battery *di, u32 value)
{
	int ret;
	u8 buf;

	buf = value & 0xff;
	ret = rk81x_bat_write(di, CAL_OFFSET_REGL, &buf, 1);
	buf = (value >> 8) & 0xff;
	ret = rk81x_bat_write(di, CAL_OFFSET_REGH, &buf, 1);

	return 0;
}

static void rk81x_bat_get_vol_offset(struct rk81x_battery *di)
{
	int vcalib0, vcalib1;

	vcalib0 = rk81x_bat_get_vcalib0(di);
	vcalib1 = rk81x_bat_get_vcalib1(di);

	di->voltage_k = (4200 - 3000) * 1000 / div((vcalib1 - vcalib0));
	di->voltage_b = 4200 - (di->voltage_k * vcalib1) / 1000;
	DBG("voltage_k=%d(x1000),voltage_b=%d\n", di->voltage_k, di->voltage_b);
}

static uint16_t rk81x_bat_get_ocv_vol(struct rk81x_battery *di)
{
	int ret;
	u8 buf;
	uint16_t temp;
	uint16_t voltage_now = 0;
	int i;
	int val[3];

	for (i = 0; i < 3; i++) {
		ret = rk81x_bat_read(di, BAT_OCV_REGL, &buf, 1);
		val[i] = buf;
		ret = rk81x_bat_read(di, BAT_OCV_REGH, &buf, 1);
		val[i] |= buf << 8;

		if (ret < 0) {
			dev_err(di->dev, "error read BAT_OCV_REGH");
			return ret;
		}
	}

	if (val[0] == val[1])
		temp = val[0];
	else
		temp = val[2];

	voltage_now = di->voltage_k * temp / 1000 + di->voltage_b;

	return voltage_now;
}

static int rk81x_bat_get_vol(struct rk81x_battery *di)
{
	int ret;
	int vol;
	u8 buf;
	int temp;
	int val[3];
	int i;

	for (i = 0; i < 3; i++) {
		ret = rk81x_bat_read(di, BAT_VOL_REGL, &buf, 1);
		val[i] = buf;
		ret = rk81x_bat_read(di, BAT_VOL_REGH, &buf, 1);
		val[i] |= buf << 8;

		if (ret < 0) {
			dev_err(di->dev, "error read BAT_VOL_REGH");
			return ret;
		}
	}
	/*check value*/
	if (val[0] == val[1])
		temp = val[0];
	else
		temp = val[2];

	vol = di->voltage_k * temp / 1000 + di->voltage_b;

	return vol;
}

static bool is_rk81x_bat_relax_mode(struct rk81x_battery *di)
{
	int ret;
	u8 status;

	ret = rk81x_bat_read(di, GGSTS, &status, 1);

	if ((!(status & RELAX_VOL1_UPD)) || (!(status & RELAX_VOL2_UPD)))
		return false;
	else
		return true;
}

static uint16_t rk81x_bat_get_relax_vol1(struct rk81x_battery *di)
{
	int ret;
	u8 buf;
	uint16_t temp = 0, voltage_now;

	ret = rk81x_bat_read(di, RELAX_VOL1_REGL, &buf, 1);
	temp = buf;
	ret = rk81x_bat_read(di, RELAX_VOL1_REGH, &buf, 1);
	temp |= (buf << 8);

	voltage_now = di->voltage_k * temp / 1000 + di->voltage_b;

	return voltage_now;
}

static uint16_t rk81x_bat_get_relax_vol2(struct rk81x_battery *di)
{
	int ret;
	u8 buf;
	uint16_t temp = 0, voltage_now;

	ret = rk81x_bat_read(di, RELAX_VOL2_REGL, &buf, 1);
	temp = buf;
	ret = rk81x_bat_read(di, RELAX_VOL2_REGH, &buf, 1);
	temp |= (buf << 8);

	voltage_now = di->voltage_k * temp / 1000 + di->voltage_b;

	return voltage_now;
}

static uint16_t rk81x_bat_get_relax_vol(struct rk81x_battery *di)
{
	int ret;
	u8 status;
	uint16_t relax_vol1, relax_vol2;
	u8 ggcon;

	ret = rk81x_bat_read(di, GGSTS, &status, 1);
	ret = rk81x_bat_read(di, GGCON, &ggcon, 1);

	relax_vol1 = rk81x_bat_get_relax_vol1(di);
	relax_vol2 = rk81x_bat_get_relax_vol2(di);
	DBG("<%s>. GGSTS=0x%x, GGCON=0x%x, relax_vol1=%d, relax_vol2=%d\n",
	    __func__, status, ggcon, relax_vol1, relax_vol2);

	if (is_rk81x_bat_relax_mode(di))
		return relax_vol1 > relax_vol2 ? relax_vol1 : relax_vol2;
	else
		return 0;
}

/* OCV Lookup table
 * Open Circuit Voltage (OCV) correction routine. This function estimates SOC,
 * based on the voltage.
 */
static int rk81x_bat_vol_to_capacity(struct rk81x_battery *di, int voltage)
{
	u32 *ocv_table;
	int ocv_size;
	u32 tmp;
	int ocv_soc;

	ocv_table = di->pdata->battery_ocv;
	ocv_size = di->pdata->ocv_size;
	tmp = interpolate(voltage, ocv_table, ocv_size);
	ocv_soc = ab_div_c(tmp, MAX_PERCENTAGE, INTERPOLATE_MAX);
	di->temp_nac = ab_div_c(tmp, di->fcc, INTERPOLATE_MAX);

	return ocv_soc;
}

static int rk81x_bat_get_raw_adc_current(struct rk81x_battery *di)
{
	u8 buf;
	int ret;
	int val;

	ret = rk81x_bat_read(di, BAT_CUR_AVG_REGL, &buf, 1);
	if (ret < 0) {
		dev_err(di->dev, "error reading BAT_CUR_AVG_REGL");
		return ret;
	}
	val = buf;
	ret = rk81x_bat_read(di, BAT_CUR_AVG_REGH, &buf, 1);
	if (ret < 0) {
		dev_err(di->dev, "error reading BAT_CUR_AVG_REGH");
		return ret;
	}
	val |= (buf << 8);

	if (ret < 0) {
		dev_err(di->dev, "error reading BAT_CUR_AVG_REGH");
		return ret;
	}

	if (val > 2047)
		val -= 4096;

	return val;
}

static void rk81x_bat_ioffset_sample_set(struct rk81x_battery *di, int time)
{
	u8 ggcon;

	rk81x_bat_read(di, GGCON, &ggcon, 1);
	ggcon &= ~(0x30); /*clear <5:4>*/
	ggcon |= time;
	rk81x_bat_write(di, GGCON, &ggcon, 1);
}

/*
 * when charger finish signal comes, we need calibrate the current, make it
 * close to 0.
 */
static bool rk81x_bat_zero_current_calib(struct rk81x_battery *di)
{
	int adc_value;
	uint16_t C0;
	uint16_t C1;
	int ioffset;
	u8 pcb_offset = 0;
	u8 retry = 0;
	bool ret = true;

	if ((di->chrg_status == CHARGE_FINISH) &&
	    (BASE_TO_MIN(di->power_on_base) >= 3) &&
	    (abs(di->current_avg) > 4)) {
		for (retry = 0; retry < 5; retry++) {
			adc_value = rk81x_bat_get_raw_adc_current(di);
			if (!rk81x_chrg_online(di) || abs(adc_value) > 30) {
				dev_warn(di->dev, "charger plugout\n");
				ret = true;
				break;
			}

			DBG("<%s>. adc_value = %d\n", __func__, adc_value);
			C0 = rk81x_bat_get_cal_offset(di);
			C1 = adc_value + C0;
			DBG("<%s>. C0(cal_offset) = %d, C1 = %d\n",
			    __func__, C0, C1);
			rk81x_bat_set_cal_offset(di, C1);
			DBG("<%s>. new cal_offset = %d\n",
			    __func__, rk81x_bat_get_cal_offset(di));
			msleep(3000);
			adc_value = rk81x_bat_get_raw_adc_current(di);
			DBG("<%s>. adc_value = %d\n", __func__, adc_value);
			if (abs(adc_value) < 4) {
				if (rk81x_bat_get_cal_offset(di) < 0x7ff) {
					ioffset = rk81x_bat_get_ioffset(di);
					rk81x_bat_set_cal_offset(di,
								 ioffset + 42);
				} else {
					ioffset = rk81x_bat_get_ioffset(di);
					pcb_offset = C1 - ioffset;
					di->pcb_ioffset = pcb_offset;
					di->pcb_ioffset_updated  = true;
					rk81x_bat_write(di,
							PCB_IOFFSET_REG,
							&pcb_offset, 1);
				}
				DBG("<%s>. update the cal_offset, C1 = %d\n"
				    "i_offset = %d, pcb_offset = %d\n",
					__func__, C1, ioffset, pcb_offset);
				ret = false;
				break;
			} else {
				dev_dbg(di->dev, "ioffset cal failed\n");
				rk81x_bat_set_cal_offset(di, C0);
			}

			di->pcb_ioffset_updated  = false;
		}
	}

	return ret;
}

static void rk81x_bat_set_relax_thres(struct rk81x_battery *di)
{
	u8 buf;
	int enter_thres, exit_thres;
	struct cell_state *cell = &di->cell;

	enter_thres = (cell->config->ocv->sleep_enter_current) * 1000 / 1506;
	exit_thres = (cell->config->ocv->sleep_exit_current) * 1000 / 1506;
	DBG("<%s>. sleep_enter_current = %d, sleep_exit_current = %d\n",
	    __func__, cell->config->ocv->sleep_enter_current,
	cell->config->ocv->sleep_exit_current);

	buf  = enter_thres & 0xff;
	rk81x_bat_write(di, RELAX_ENTRY_THRES_REGL, &buf, 1);
	buf = (enter_thres >> 8) & 0xff;
	rk81x_bat_write(di, RELAX_ENTRY_THRES_REGH, &buf, 1);

	buf  = exit_thres & 0xff;
	rk81x_bat_write(di, RELAX_EXIT_THRES_REGL, &buf, 1);
	buf = (exit_thres >> 8) & 0xff;
	rk81x_bat_write(di, RELAX_EXIT_THRES_REGH, &buf, 1);

	/* set sample time */
	rk81x_bat_read(di, GGCON, &buf, 1);
	buf &= ~(3 << 2);/*8min*/
	buf &= ~0x01; /* clear bat_res calc*/
	rk81x_bat_write(di, GGCON, &buf, 1);
}

static void rk81x_bat_restart_relax(struct rk81x_battery *di)
{
	u8 ggcon;
	u8 ggsts;

	rk81x_bat_read(di, GGCON, &ggcon, 1);
	ggcon &= ~0x0c;
	rk81x_bat_write(di, GGCON, &ggcon, 1);

	rk81x_bat_read(di, GGSTS, &ggsts, 1);
	ggsts &= ~0x0c;
	rk81x_bat_write(di, GGSTS, &ggsts, 1);
}

static int rk81x_bat_get_avg_current(struct rk81x_battery *di)
{
	u8  buf;
	int ret;
	int current_now;
	int temp;
	int val[3];
	int i;

	for (i = 0; i < 3; i++) {
		ret = rk81x_bat_read(di, BAT_CUR_AVG_REGL, &buf, 1);
		if (ret < 0) {
			dev_err(di->dev, "error read BAT_CUR_AVG_REGL");
			return ret;
		}
		val[i] = buf;

		ret = rk81x_bat_read(di, BAT_CUR_AVG_REGH, &buf, 1);
		if (ret < 0) {
			dev_err(di->dev, "error read BAT_CUR_AVG_REGH");
			return ret;
		}
		val[i] |= (buf<<8);
	}
	/*check value*/
	if (val[0] == val[1])
		current_now = val[0];
	else
		current_now = val[2];

	if (current_now & 0x800)
		current_now -= 4096;

	temp = current_now * 1506 / 1000;/*1000*90/14/4096*500/521;*/

	return temp;
}

static void rk81x_bat_set_power_supply_state(struct rk81x_battery *di,
					     enum charger_type  charger_type)
{
	di->usb_online = OFFLINE;
	di->ac_online = OFFLINE;
	di->dc_online = OFFLINE;

	switch (charger_type) {
	case NO_CHARGER:
		di->psy_status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case USB_CHARGER:
		di->usb_online = ONLINE;
		di->psy_status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case DC_CHARGER:/*treat dc as ac*/
		di->dc_online = ONLINE;
	case AC_CHARGER:
		di->ac_online = ONLINE;
		di->psy_status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	default:
		di->psy_status = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	if (di->wq)
		queue_delayed_work(di->wq, &di->chrg_term_mode_switch_work,
				   msecs_to_jiffies(1000));
}

/* high load: current < 0 with charger in.
 * System will not shutdown while dsoc=0% with charging state(ac_online),
 * which will cause over discharge, so oppose status before report states.
 */
static void rk81x_bat_lowpwr_check(struct rk81x_battery *di)
{
	static u64 time;
	int pwr_off_thresd = di->pdata->power_off_thresd;

	if (di->current_avg < 0 &&  di->voltage < pwr_off_thresd) {
		if (!time)
			time = get_runtime_sec();

		if (BASE_TO_SEC(time) > (MINUTE)) {
			rk81x_bat_set_power_supply_state(di, NO_CHARGER);
			dev_info(di->dev, "low power....\n");
		}

		if (di->voltage <= pwr_off_thresd - 50) {
			di->dsoc--;
			rk81x_bat_set_power_supply_state(di, NO_CHARGER);
		}
	} else {
		time = 0;
	}
}

static int is_rk81x_bat_exist(struct  rk81x_battery *di)
{
	u8 buf;

	rk81x_bat_read(di, SUP_STS_REG, &buf, 1);

	return (buf & 0x80) ? 1 : 0;
}

static bool is_rk81x_bat_first_poweron(struct  rk81x_battery *di)
{
	u8 buf;
	u8 temp;

	rk81x_bat_read(di, GGSTS, &buf, 1);
	DBG("%s GGSTS value is 0x%2x\n", __func__, buf);
	/*di->pwron_bat_con = buf;*/
	if (buf&BAT_CON) {
		buf &= ~(BAT_CON);
		do {
			rk81x_bat_write(di, GGSTS, &buf, 1);
			rk81x_bat_read(di, GGSTS, &temp, 1);
		} while (temp & BAT_CON);
		return true;
	}

	return false;
}

static void rk81x_bat_flatzone_vol_init(struct rk81x_battery *di)
{
	u32 *ocv_table;
	int ocv_size;
	int temp_table[21];
	int i, j;

	ocv_table = di->pdata->battery_ocv;
	ocv_size = di->pdata->ocv_size;

	for (j = 0; j < 21; j++)
		temp_table[j] = 0;

	j = 0;
	for (i = 1; i < ocv_size-1; i++) {
		if (ocv_table[i+1] < ocv_table[i] + 20)
			temp_table[j++] = i;
	}

	temp_table[j] = temp_table[j-1] + 1;
	i = temp_table[0];
	di->enter_flatzone = ocv_table[i];
	j = 0;

	for (i = 0; i < 20; i++) {
		if (temp_table[i] < temp_table[i+1])
			j = i + 1;
	}

	i = temp_table[j];
	di->exit_flatzone = ocv_table[i];

	DBG("enter_flatzone = %d exit_flatzone = %d\n",
	    di->enter_flatzone, di->exit_flatzone);
}

static void rk81x_bat_power_on_save(struct rk81x_battery *di, int ocv_voltage)
{
	u8 ocv_valid, first_pwron;
	u8 soc_level;
	u8 ocv_soc;

	/*buf==1: OCV_VOL is valid*/
	ocv_valid = rk81x_bat_read_bit(di, MISC_MARK_REG, OCV_VALID_SHIFT);
	first_pwron = rk81x_bat_read_bit(di, MISC_MARK_REG, FIRST_PWRON_SHIFT);
	DBG("readbit: ocv_valid=%d, first_pwron=%d\n", ocv_valid, first_pwron);

	if (first_pwron == 1 || ocv_valid == 1) {
		DBG("<%s> enter.\n", __func__);
		ocv_soc = rk81x_bat_vol_to_capacity(di, ocv_voltage);
		if ((ocv_soc < 20) && (ocv_voltage > 2750)) {
			di->dod0_voltage = ocv_voltage;
			di->dod0_capacity = di->temp_nac;
			di->adjust_cap = 0;
			di->dod0 = ocv_soc;

			if (ocv_soc <= 0)
				di->dod0_level = 100;
			else if (ocv_soc < 5)
				di->dod0_level = 95;
			else if (ocv_soc < 10)
				di->dod0_level = 90;
			else
				di->dod0_level = 80;
			/* save_soc = di->dod0_level; */
			soc_level = rk81x_bat_get_level(di);
			if (soc_level >  di->dod0_level) {
				di->dod0_status = 0;
				soc_level -= 5;
				if (soc_level <= 80)
					soc_level = 80;
				rk81x_bat_save_level(di, soc_level);
			} else {
				di->dod0_status = 1;
				/*time start*/
				di->fcc_update_sec = get_runtime_sec();
			}

			dev_info(di->dev, "dod0_vol:%d, dod0_cap:%d\n"
				 "dod0:%d, soc_level:%d: dod0_status:%d\n"
				 "dod0_level:%d",
				 di->dod0_voltage, di->dod0_capacity,
				 ocv_soc, soc_level, di->dod0_status,
				 di->dod0_level);
		}
	}
}

static int rk81x_bat_get_rsoc(struct   rk81x_battery *di)
{
	return (di->remain_capacity + di->fcc / 200) * 100 / div(di->fcc);
}

static enum power_supply_property rk_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CAPACITY,
};

static int rk81x_battery_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct rk81x_battery *di = to_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = di->current_avg * 1000;/*uA*/
		if (di->fg_drv_mode == TEST_POWER_MODE)
			val->intval = TEST_CURRENT * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = di->voltage * 1000;/*uV*/
		if (di->fg_drv_mode == TEST_POWER_MODE)
			val->intval = TEST_VOLTAGE * 1000;

		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = is_rk81x_bat_exist(di);
		if (di->fg_drv_mode == TEST_POWER_MODE)
			val->intval = TEST_PRESET;

		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = di->dsoc;
		if (di->fg_drv_mode == TEST_POWER_MODE)
			val->intval = TEST_SOC;

		DBG("<%s>, report dsoc: %d\n", __func__, val->intval);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = di->psy_status;
		if (di->fg_drv_mode == TEST_POWER_MODE)
			val->intval = TEST_STATUS;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property rk_battery_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property rk_battery_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int rk81x_battery_ac_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	int ret = 0;
	struct rk81x_battery *di = to_ac_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (rk81x_chrg_online(di))
			rk81x_bat_lowpwr_check(di);
		val->intval = di->ac_online;	/*discharging*/
		if (di->fg_drv_mode == TEST_POWER_MODE)
			val->intval = TEST_AC_ONLINE;

		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int rk81x_battery_usb_get_property(struct power_supply *psy,
					  enum power_supply_property psp,
					  union power_supply_propval *val)
{
	int ret = 0;
	struct rk81x_battery *di = to_usb_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (rk81x_chrg_online(di))
			rk81x_bat_lowpwr_check(di);
		val->intval = di->usb_online;
		if (di->fg_drv_mode == TEST_POWER_MODE)
			val->intval = TEST_USB_ONLINE;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rk81x_bat_power_supply_init(struct rk81x_battery *di)
{
	int ret;

	di->bat.name = "BATTERY";
	di->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties = rk_battery_props;
	di->bat.num_properties = ARRAY_SIZE(rk_battery_props);
	di->bat.get_property = rk81x_battery_get_property;

	di->ac.name = "AC";
	di->ac.type = POWER_SUPPLY_TYPE_MAINS;
	di->ac.properties = rk_battery_ac_props;
	di->ac.num_properties = ARRAY_SIZE(rk_battery_ac_props);
	di->ac.get_property = rk81x_battery_ac_get_property;

	di->usb.name = "USB";
	di->usb.type = POWER_SUPPLY_TYPE_USB;
	di->usb.properties = rk_battery_usb_props;
	di->usb.num_properties = ARRAY_SIZE(rk_battery_usb_props);
	di->usb.get_property = rk81x_battery_usb_get_property;

	ret = power_supply_register(di->dev, &di->bat);
	if (ret) {
		dev_err(di->dev, "failed to register main battery\n");
		goto batt_failed;
	}
	ret = power_supply_register(di->dev, &di->usb);
	if (ret) {
		dev_err(di->dev, "failed to register usb power supply\n");
		goto usb_failed;
	}
	ret = power_supply_register(di->dev, &di->ac);
	if (ret) {
		dev_err(di->dev, "failed to register ac power supply\n");
		goto ac_failed;
	}

	return 0;

ac_failed:
	power_supply_unregister(&di->ac);
usb_failed:
	power_supply_unregister(&di->usb);
batt_failed:
	power_supply_unregister(&di->bat);

	return ret;
}

static void rk81x_bat_save_remain_capacity(struct rk81x_battery *di,
					   int capacity)
{
	u8 buf;
	static u32 capacity_ma;

	if (capacity >= di->qmax)
		capacity = di->qmax;

	if (capacity <= 0)
		capacity = 0;

	if (capacity_ma == capacity)
		return;

	capacity_ma = capacity;

	buf = (capacity_ma >> 24) & 0xff;
	rk81x_bat_write(di, REMAIN_CAP_REG3, &buf, 1);
	buf = (capacity_ma >> 16) & 0xff;
	rk81x_bat_write(di, REMAIN_CAP_REG2, &buf, 1);
	buf = (capacity_ma >> 8) & 0xff;
	rk81x_bat_write(di, REMAIN_CAP_REG1, &buf, 1);
	buf = (capacity_ma & 0xff) | 0x01;
	rk81x_bat_write(di, REMAIN_CAP_REG0, &buf, 1);
}

static int rk81x_bat_get_remain_capacity(struct rk81x_battery *di)
{
	int ret;
	u8 buf;
	u32 capacity;
	int i;
	int val[3];

	for (i = 0; i < 3; i++) {
		ret = rk81x_bat_read(di, REMAIN_CAP_REG3, &buf, 1);
		val[i] = buf << 24;
		ret = rk81x_bat_read(di, REMAIN_CAP_REG2, &buf, 1);
		val[i] |= buf << 16;
		ret = rk81x_bat_read(di, REMAIN_CAP_REG1, &buf, 1);
		val[i] |= buf << 8;
		ret = rk81x_bat_read(di, REMAIN_CAP_REG0, &buf, 1);
		val[i] |= buf;
	}

	if (val[0] == val[1])
		capacity = val[0];
	else
		capacity = val[2];

	return capacity;
}

static void rk81x_bat_save_fcc(struct rk81x_battery *di, u32 capacity)
{
	u8 buf;
	u32 capacity_ma;

	capacity_ma = capacity;
	buf = (capacity_ma >> 24) & 0xff;
	rk81x_bat_write(di, NEW_FCC_REG3, &buf, 1);
	buf = (capacity_ma >> 16) & 0xff;
	rk81x_bat_write(di, NEW_FCC_REG2, &buf, 1);
	buf = (capacity_ma >> 8) & 0xff;
	rk81x_bat_write(di, NEW_FCC_REG1, &buf, 1);
	buf = (capacity_ma & 0xff) | 0x01;
	rk81x_bat_write(di, NEW_FCC_REG0, &buf, 1);

	dev_info(di->dev, "update fcc : %d\n", capacity);
}

static int rk81x_bat_get_fcc(struct rk81x_battery *di)
{
	u8 buf;
	u32 capacity;

	rk81x_bat_read(di, NEW_FCC_REG3, &buf, 1);
	capacity = buf << 24;
	rk81x_bat_read(di, NEW_FCC_REG2, &buf, 1);
	capacity |= buf << 16;
	rk81x_bat_read(di, NEW_FCC_REG1, &buf, 1);
	capacity |= buf << 8;
	rk81x_bat_read(di, NEW_FCC_REG0, &buf, 1);
	capacity |= buf;

	if (capacity < MIN_FCC) {
		dev_warn(di->dev, "invalid fcc(0x%x), use design capacity",
			 capacity);
		capacity = di->design_capacity;
		rk81x_bat_save_fcc(di, capacity);
	} else if (capacity > di->qmax) {
		dev_warn(di->dev, "invalid fcc(0x%x), use qmax", capacity);
		capacity = di->qmax;
		rk81x_bat_save_fcc(di, capacity);
	}

	return capacity;
}

static int rk81x_bat_get_realtime_capacity(struct rk81x_battery *di)
{
	int ret;
	int temp = 0;
	u8 buf;
	u32 capacity;
	int i;
	int val[3];

	for (i = 0; i < 3; i++) {
		ret = rk81x_bat_read(di, GASCNT3, &buf, 1);
		val[i] = buf << 24;
		ret = rk81x_bat_read(di, GASCNT2, &buf, 1);
		val[i] |= buf << 16;
		ret = rk81x_bat_read(di, GASCNT1, &buf, 1);
		val[i] |= buf << 8;
		ret = rk81x_bat_read(di, GASCNT0, &buf, 1);
		val[i] |= buf;
	}
	if (val[0] == val[1])
		temp = val[0];
	else
		temp = val[2];

	capacity = temp / 2390;/* 4096*900/14/36*500/521; */

	return capacity;
}

static int rk81x_bat_save_dsoc(struct  rk81x_battery *di, u8 save_soc)
{
	static u8 last_soc;

	if (last_soc != save_soc) {
		rk81x_bat_write(di, SOC_REG, &save_soc, 1);
		last_soc = save_soc;
	}

	return 0;
}

static int rk81x_bat_save_reboot_cnt(struct  rk81x_battery *di, u8 save_cnt)
{
	u8 cnt;

	cnt = save_cnt;
	rk81x_bat_write(di, REBOOT_CNT_REG, &cnt, 1);
	return 0;
}

static void rk81x_bat_set_current(struct rk81x_battery *di, int charge_current)
{
	u8 usb_ctrl_reg;

	rk81x_bat_read(di, USB_CTRL_REG, &usb_ctrl_reg, 1);
	usb_ctrl_reg &= (~0x0f);/* (VLIM_4400MV | ILIM_1200MA) |(0x01 << 7); */
	usb_ctrl_reg |= (charge_current | CHRG_CT_EN);
	rk81x_bat_write(di, USB_CTRL_REG, &usb_ctrl_reg, 1);
}

static void rk81x_bat_set_chrg_current(struct rk81x_battery *di,
				       enum charger_type charger_type)
{
	switch (charger_type) {
	case NO_CHARGER:
	case USB_CHARGER:
		rk81x_bat_set_current(di, ILIM_450MA);
		break;
	case AC_CHARGER:
	case DC_CHARGER:
		rk81x_bat_set_current(di, di->chrg_i_lmt);
		break;
	default:
		rk81x_bat_set_current(di, ILIM_450MA);
	}
}

#if defined(CONFIG_ARCH_ROCKCHIP)

static void rk81x_bat_set_charger_param(struct rk81x_battery *di,
					enum charger_type charger_type)
{
	rk81x_bat_set_chrg_current(di, charger_type);
	rk81x_bat_set_power_supply_state(di, charger_type);

	switch (charger_type) {
	case NO_CHARGER:
		power_supply_changed(&di->bat);
		break;
	case USB_CHARGER:
	case AC_CHARGER:
		power_supply_changed(&di->usb);
		break;
	case DC_CHARGER:
		power_supply_changed(&di->ac);
		break;
	default:
		break;
	}
}

static void rk81x_bat_set_otg_state(struct rk81x_battery *di, int state)
{
	switch (state) {
	case USB_OTG_POWER_ON:
		rk81x_bat_set_bit(di, NT_STS_MSK_REG2, PLUG_IN_INT);
		rk81x_bat_set_bit(di, NT_STS_MSK_REG2, PLUG_OUT_INT);
		rk818_set_bits(di->rk818, DCDC_EN_REG, OTG_EN_MASK, OTG_EN);
		break;
	case USB_OTG_POWER_OFF:
		rk81x_bat_clr_bit(di, NT_STS_MSK_REG2, PLUG_IN_INT);
		rk81x_bat_clr_bit(di, NT_STS_MSK_REG2, PLUG_OUT_INT);
		rk818_set_bits(di->rk818, DCDC_EN_REG, OTG_EN_MASK, OTG_DIS);
		break;
	default:
		break;
	}
}

static enum charger_type rk81x_bat_get_dc_state(struct rk81x_battery *di)
{
	int ret;
	enum charger_type charger_type = NO_CHARGER;

	if (di->fg_drv_mode == TEST_POWER_MODE) {
		charger_type = DC_CHARGER;
		goto out;
	}
	/*
	if (di->otg_online)
		goto out;
	*/
	if (!gpio_is_valid(di->dc_det_pin))
		goto out;

	ret = gpio_request(di->dc_det_pin, "rk818_dc_det");
	if (ret < 0) {
		pr_err("Failed to request gpio %d with ret:""%d\n",
		       di->dc_det_pin, ret);
		goto out;
	}

	gpio_direction_input(di->dc_det_pin);
	ret = gpio_get_value(di->dc_det_pin);
	if (ret == di->dc_det_level)
		charger_type = DC_CHARGER;
	else
		charger_type = NO_CHARGER;
	gpio_free(di->dc_det_pin);
out:
	return charger_type;
}

static void rk81x_battery_dc_delay_work(struct work_struct *work)
{
	enum charger_type charger_type;
	struct rk81x_battery *di = container_of(work,
				struct rk81x_battery, dc_det_check_work.work);

	charger_type = rk81x_bat_get_dc_state(di);

	if (charger_type == DC_CHARGER) {
		rk81x_bat_set_charger_param(di, DC_CHARGER);
		if (power_dc2otg && di->otg_online)
			rk81x_bat_set_otg_state(di, USB_OTG_POWER_OFF);
	} else {
		if (di->otg_online) {
			rk81x_bat_set_otg_state(di, USB_OTG_POWER_ON);
			rk81x_bat_set_charger_param(di, NO_CHARGER);
		} else {
			queue_delayed_work(di->wq,
					   &di->ac_usb_check_work,
					   msecs_to_jiffies(10));
		}
	}
}

static void rk81x_battery_acusb_delay_work(struct work_struct *work)
{
	u8 buf;
	int gadget_flag, usb_id;
	struct rk81x_battery *di = container_of(work,
			struct rk81x_battery, ac_usb_check_work.work);

	rk81x_bat_read(di, VB_MOD_REG, &buf, 1);
	usb_id = dwc_otg_check_dpdm(0);
	switch (usb_id) {
	case 0:
		if ((buf & PLUG_IN_STS) != 0)
			rk81x_bat_set_charger_param(di, DC_CHARGER);
		else
			rk81x_bat_set_charger_param(di, NO_CHARGER);
		break;
	case 1:
	case 3:
		rk81x_bat_set_charger_param(di, USB_CHARGER);
		break;
	case 2:
		rk81x_bat_set_charger_param(di, AC_CHARGER);
		break;
	default:
		break;
	}
	/*check unstanderd charger*/
	if (usb_id == 1 || usb_id == 3) {
		gadget_flag = get_gadget_connect_flag();
		if (0 == gadget_flag) {
			di->check_count++;
			if (di->check_count >= 5) {
				di->check_count = 0;
				rk81x_bat_set_charger_param(di, AC_CHARGER);
			} else {
				queue_delayed_work(di->wq,
						   &di->ac_usb_check_work,
						   msecs_to_jiffies(1000));
			}
		} else {/*confirm: USB_CHARGER*/
			di->check_count = 0;
		}
	}
}
#endif

#if defined(CONFIG_X86_INTEL_SOFIA)
static int rk81x_get_chrg_type_by_usb_phy(struct rk81x_battery *di, int ma)
{
	enum charger_type charger_type;

	if (ma > 500)
		charger_type =  AC_CHARGER;
	else if (ma >= 100)
		charger_type = USB_CHARGER;
	else
		charger_type = NO_CHARGER;

	di->ma = ma;

	dev_info(di->dev, "limit current:%d\n", ma);

	return charger_type;
}

static void rk81x_battery_usb_notifier_delayed_work(struct work_struct *work)
{
	struct rk81x_battery *di;
	enum charger_type type;

	di = container_of(work, struct rk81x_battery, usb_phy_delay_work.work);
	type = rk81x_get_chrg_type_by_usb_phy(di, di->ma);

	rk81x_bat_set_chrg_current(di, type);
	power_supply_changed(&di->usb);
}

static int rk81x_battery_usb_notifier(struct notifier_block *nb,
				      unsigned long event, void *data)
{
	struct rk81x_battery *di;
	struct power_supply_cable_props *cable_props;
	enum charger_type type;

	di = container_of(nb, struct rk81x_battery, usb_nb);

	if (!data)
		return NOTIFY_BAD;

	switch (event) {
	case USB_EVENT_CHARGER:
		cable_props = (struct power_supply_cable_props *)data;
		type = rk81x_get_chrg_type_by_usb_phy(di, cable_props->ma);
		rk81x_bat_set_power_supply_state(di, type);
		queue_delayed_work(di->wq, &di->usb_phy_delay_work,
				   msecs_to_jiffies(50));
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}
#endif

static int rk81x_battery_fb_notifier(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct rk81x_battery *di;
	struct fb_event *evdata = data;
	int blank;

	di = container_of(nb, struct rk81x_battery, fb_nb);

	if (event != FB_EVENT_BLANK && event != FB_EVENT_CONBLANK)
		return 0;

	blank = *(int *)evdata->data;

	if (di->fb_blank != blank)
		di->fb_blank = blank;
	else
		return 0;

	if (blank == FB_BLANK_UNBLANK)
		di->early_resume = 1;

	return 0;
}

static int rk81x_battery_register_fb_notify(struct rk81x_battery *di)
{
	memset(&di->fb_nb, 0, sizeof(di->fb_nb));
	di->fb_nb.notifier_call = rk81x_battery_fb_notifier;

	return fb_register_client(&di->fb_nb);
}

/*
 * it is first time for battery to be weld, init by ocv table
 */
static void rk81x_bat_first_pwron(struct rk81x_battery *di)
{
	rk81x_bat_save_fcc(di, di->design_capacity);
	di->fcc = rk81x_bat_get_fcc(di);

	di->rsoc = rk81x_bat_vol_to_capacity(di, di->voltage_ocv);
	di->dsoc = di->rsoc;
	di->nac  = di->temp_nac;

	rk81x_bat_set_bit(di, MISC_MARK_REG, OCV_VALID_SHIFT);
	rk81x_bat_set_bit(di, MISC_MARK_REG, FIRST_PWRON_SHIFT);/*save*/
	DBG("<%s>.this is first poweron: OCV-SOC:%d, OCV-CAP:%d, FCC:%d\n",
	    __func__, di->dsoc, di->nac, di->fcc);
}

static int rk81x_bat_get_calib_vol(struct rk81x_battery *di)
{
	int calib_vol;
	int init_cur, diff;
	int est_vol;
	int relax_vol = di->relax_voltage;
	int ocv_vol = di->voltage_ocv;

	init_cur = rk81x_bat_get_avg_current(di);
	diff = (di->bat_res + di->pdata->chrg_diff_vol) * init_cur;
	diff /= 1000;
	est_vol = di->voltage - diff;

	if (di->loader_charged) {
		calib_vol = est_vol;
		return calib_vol;
	}

	if (di->pwroff_min > 8) {
		if (abs(relax_vol - ocv_vol) < 100) {
			calib_vol = ocv_vol;
		} else {
			if (abs(relax_vol - est_vol) > abs(ocv_vol - est_vol))
				calib_vol = ocv_vol;
			else
				calib_vol = relax_vol;
		}
	} else if (di->pwroff_min > 2) {
		calib_vol = ocv_vol;
	} else {
		calib_vol = -1;
	}

	dev_info(di->dev, "c=%d, v=%d, relax=%d, ocv=%d, est=%d, calib=%d\n",
		 init_cur, di->voltage, relax_vol, ocv_vol, est_vol, calib_vol);

	return calib_vol;
}

/*
 * it is not first time for battery to be weld, init by last record info
 */
static void rk81x_bat_not_first_pwron(struct rk81x_battery *di)
{
	u8 pwron_soc;
	u8 init_soc;
	int remain_capacity;
	int ocv_soc;
	int calib_vol, calib_soc, calib_capacity;

	rk81x_bat_clr_bit(di, MISC_MARK_REG, FIRST_PWRON_SHIFT);
	rk81x_bat_read(di, SOC_REG, &pwron_soc, 1);
	init_soc = pwron_soc;
	remain_capacity = rk81x_bat_get_remain_capacity(di);

	/* check if support uboot charge,
	 * if support, uboot charge driver should have done init work,
	 * so here we should skip init work
	 */
#if defined(CONFIG_ARCH_ROCKCHIP)
	if (di->loader_charged) {
		dev_info(di->dev, "loader charged\n");
		goto out;
	}
#endif
	calib_vol = rk81x_bat_get_calib_vol(di);
	if (calib_vol > 0) {
		calib_soc = rk81x_bat_vol_to_capacity(di, calib_vol);
		calib_capacity = di->temp_nac;

		if (abs(calib_soc - init_soc) >= 70 || di->loader_charged) {
			init_soc = calib_soc;
			remain_capacity = calib_capacity;
		}
		dev_info(di->dev, "calib_vol %d, init soc %d, remain_cap %d\n",
			 calib_vol, init_soc, remain_capacity);
	}

	ocv_soc = rk81x_bat_vol_to_capacity(di, di->voltage_ocv);
	DBG("<%s>, Not first pwron, real_remain_cap = %d, ocv-remain_cp=%d\n",
	    __func__, remain_capacity, di->temp_nac);

	if (di->pwroff_min > 0) {
		if (di->pwroff_min > 30) {
			rk81x_bat_set_bit(di, MISC_MARK_REG, OCV_VALID_SHIFT);

			remain_capacity = di->temp_nac;
			DBG("<%s>pwroff > 30 minute, remain_cap = %d\n",
			    __func__, remain_capacity);

		} else if ((di->pwroff_min > 5) &&
				(abs(ocv_soc - init_soc) >= 10)) {
			if (remain_capacity >= di->temp_nac * 120/100)
				remain_capacity = di->temp_nac * 110/100;
			else if (remain_capacity < di->temp_nac * 8/10)
				remain_capacity = di->temp_nac * 9/10;
			DBG("<%s> pwroff > 5 minute, remain_cap = %d\n",
			    __func__, remain_capacity);
		}
	} else {
		rk81x_bat_clr_bit(di, MISC_MARK_REG, OCV_VALID_SHIFT);
	}
out:
	di->dsoc = init_soc;
	di->nac = remain_capacity;
	if (di->nac <= 0)
		di->nac = 0;
	dev_info(di->dev, "reg soc=%d, init soc = %d, init cap=%d\n",
		 pwron_soc, di->dsoc, di->nac);
}

static u8 rk81x_bat_get_pwroff_min(struct rk81x_battery *di)
{
	u8 curr_pwroff_min, last_pwroff_min;

	rk81x_bat_read(di, NON_ACT_TIMER_CNT_REG,
		       &curr_pwroff_min, 1);
	rk81x_bat_read(di, NON_ACT_TIMER_CNT_REG_SAVE,
		       &last_pwroff_min, 1);

	rk81x_bat_write(di, NON_ACT_TIMER_CNT_REG_SAVE,
			&curr_pwroff_min, 1);

	return (curr_pwroff_min != last_pwroff_min) ? curr_pwroff_min : 0;
}

static int rk81x_bat_rsoc_init(struct rk81x_battery *di)
{
	u8 calib_en;/*debug*/

	di->voltage  = rk81x_bat_get_vol(di);
	di->voltage_ocv = rk81x_bat_get_ocv_vol(di);
	di->pwroff_min = rk81x_bat_get_pwroff_min(di);
	di->relax_voltage = rk81x_bat_get_relax_vol(di);
	di->current_avg = rk81x_bat_get_avg_current(di);

	dev_info(di->dev, "v=%d, ov=%d, rv=%d, c=%d, pwroff_min=%d\n",
		 di->voltage, di->voltage_ocv, di->relax_voltage,
		 di->current_avg, di->pwroff_min);

	calib_en = rk81x_bat_read_bit(di, MISC_MARK_REG, OCV_CALIB_SHIFT);
	DBG("readbit: calib_en=%d\n", calib_en);
	if (is_rk81x_bat_first_poweron(di) ||
	    ((di->pwroff_min >= 30) && (calib_en == 1))) {
		rk81x_bat_first_pwron(di);
		rk81x_bat_clr_bit(di, MISC_MARK_REG, OCV_CALIB_SHIFT);

	} else {
		rk81x_bat_not_first_pwron(di);
	}

	return 0;
}

static u8 rk81x_bat_get_chrg_status(struct rk81x_battery *di)
{
	u8 status;
	u8 ret = 0;

	rk81x_bat_read(di, SUP_STS_REG, &status, 1);
	status &= (0x70);
	switch (status) {
	case CHARGE_OFF:
		ret = CHARGE_OFF;
		DBG("  CHARGE-OFF ...\n");
		break;
	case DEAD_CHARGE:
		ret = DEAD_CHARGE;
		DBG("  DEAD CHARGE ...\n");
		break;
	case  TRICKLE_CHARGE:
		ret = DEAD_CHARGE;
		DBG("  TRICKLE CHARGE ...\n ");
		break;
	case  CC_OR_CV:
		ret = CC_OR_CV;
		DBG("  CC or CV ...\n");
		break;
	case  CHARGE_FINISH:
		ret = CHARGE_FINISH;
		DBG("  CHARGE FINISH ...\n");
		break;
	case  USB_OVER_VOL:
		ret = USB_OVER_VOL;
		DBG("  USB OVER VOL ...\n");
		break;
	case  BAT_TMP_ERR:
		ret = BAT_TMP_ERR;
		DBG("  BAT TMP ERROR ...\n");
		break;
	case  TIMER_ERR:
		ret = TIMER_ERR;
		DBG("  TIMER ERROR ...\n");
		break;
	case  USB_EXIST:
		ret = USB_EXIST;
		DBG("  USB EXIST ...\n");
		break;
	case  USB_EFF:
		ret = USB_EFF;
		DBG("  USB EFF...\n");
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static void rk81x_bat_match_param(struct rk81x_battery *di, int chrg_vol,
				  int chrg_ilim, int chrg_cur)
{
	int i;

	di->chrg_v_lmt = DEF_CHRG_VOL;
	di->chrg_i_lmt = DEF_CHRG_CURR_LMT;
	di->chrg_i_cur = DEF_CHRG_CURR_SEL;

	for (i = 0; i < ARRAY_SIZE(CHRG_V_LMT); i++) {
		if (chrg_vol < CHRG_V_LMT[i])
			break;

		di->chrg_v_lmt = (i << CHRG_VOL_SHIFT);
	}

	for (i = 0; i < ARRAY_SIZE(CHRG_I_LMT); i++) {
		if (chrg_ilim < CHRG_I_LMT[i])
			break;

		di->chrg_i_lmt = (i << CHRG_ILIM_SHIFT);
	}

	for (i = 0; i < ARRAY_SIZE(CHRG_I_CUR); i++) {
		if (chrg_cur < CHRG_I_CUR[i])
			break;

		di->chrg_i_cur = (i << CHRG_ICUR_SHIFT);
	}
	DBG("<%s>. vol = 0x%x, i_lim = 0x%x, cur=0x%x\n",
	    __func__, di->chrg_v_lmt, di->chrg_i_lmt, di->chrg_i_cur);
}

static u8 rk81x_bat_select_finish_ma(int fcc)
{
	u8 ma = FINISH_150MA;

	if (fcc > 5000)
		ma = FINISH_250MA;

	else if (fcc >= 4000)
		ma = FINISH_200MA;

	else if (fcc >= 3000)
		ma = FINISH_150MA;

	else
		ma = FINISH_100MA;

	return ma;
}
#if 0
/*
 * there is a timer inside rk81x to calc how long the battery is in charging
 * state. rk81x will close PowerPath inside IC when timer reach, which will
 * stop the charging work. we have to reset the corresponding bits to restart
 * the timer to avoid that case.
 */
static void rk81x_bat_init_chrg_timer(struct rk81x_battery *di)
{
	u8 buf;

	rk81x_bat_read(di, CHRG_CTRL_REG3, &buf, 1);
	buf &= ~CHRG_TIMER_CCCV_EN;
	rk81x_bat_write(di, CHRG_CTRL_REG3, &buf, 1);
	udelay(40);
	rk81x_bat_read(di, CHRG_CTRL_REG3, &buf, 1);
	buf |= CHRG_TIMER_CCCV_EN;
	rk81x_bat_write(di, CHRG_CTRL_REG3, &buf, 1);
	dev_info(di->dev, "reset cccv charge timer\n");
}
#endif

static void rk81x_bat_charger_init(struct  rk81x_battery *di)
{
	u8 chrg_ctrl_reg1, usb_ctrl_reg, chrg_ctrl_reg2, chrg_ctrl_reg3;
	u8 sup_sts_reg, thremal_reg, ggcon;
	int chrg_vol, chrg_cur, chrg_ilim;
	u8 finish_ma;

	chrg_vol = di->pdata->max_charger_voltagemV;
	chrg_cur = di->pdata->max_charger_currentmA;
	chrg_ilim = di->pdata->max_charger_ilimitmA;

	rk81x_bat_match_param(di, chrg_vol, chrg_ilim, chrg_cur);
	finish_ma = rk81x_bat_select_finish_ma(di->fcc);

	/*rk81x_bat_init_chrg_timer(di);*/

	rk81x_bat_read(di, THERMAL_REG, &thremal_reg, 1);
	rk81x_bat_read(di, USB_CTRL_REG, &usb_ctrl_reg, 1);
	rk81x_bat_read(di, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
	rk81x_bat_read(di, CHRG_CTRL_REG2, &chrg_ctrl_reg2, 1);
	rk81x_bat_read(di, SUP_STS_REG, &sup_sts_reg, 1);
	rk81x_bat_read(di, CHRG_CTRL_REG3, &chrg_ctrl_reg3, 1);
	rk81x_bat_read(di, GGCON, &ggcon, 1);

	usb_ctrl_reg &= (~0x0f);

	if (rk81x_bat_support_adp_type(HW_ADP_TYPE_USB))
		usb_ctrl_reg |= (CHRG_CT_EN | ILIM_450MA);/*en temp feed back*/
	else
		usb_ctrl_reg |= (CHRG_CT_EN | di->chrg_i_lmt);

	if (di->fg_drv_mode == TEST_POWER_MODE)
		usb_ctrl_reg |= (CHRG_CT_EN | di->chrg_i_lmt);

	chrg_ctrl_reg1 &= (0x00);
	chrg_ctrl_reg1 |= (CHRG_EN) | (di->chrg_v_lmt | di->chrg_i_cur);

	chrg_ctrl_reg3 |= CHRG_TERM_DIG_SIGNAL;/* digital finish mode*/
	chrg_ctrl_reg3 &= ~CHRG_TIMER_CCCV_EN;/*disable*/

	chrg_ctrl_reg2 &= ~(0xc7);
	chrg_ctrl_reg2 |= finish_ma | CHG_CCCV_6HOUR;

	sup_sts_reg &= ~(0x01 << 3);
	sup_sts_reg |= (0x01 << 2);

	thremal_reg &= (~0x0c);
	thremal_reg |= TEMP_105C;/*temp feed back: 105c*/
	ggcon |= ADC_CURRENT_MODE;

	rk81x_bat_write(di, THERMAL_REG, &thremal_reg, 1);
	rk81x_bat_write(di, CHRG_CTRL_REG3, &chrg_ctrl_reg3, 1);
	/*don't touch charge  setting when boot int loader charge mode*/
	if (!di->loader_charged)
		rk81x_bat_write(di, USB_CTRL_REG, &usb_ctrl_reg, 1);
	rk81x_bat_write(di, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
	rk81x_bat_write(di, CHRG_CTRL_REG2, &chrg_ctrl_reg2, 1);
	rk81x_bat_write(di, SUP_STS_REG, &sup_sts_reg, 1);
	rk81x_bat_write(di, GGCON, &ggcon, 1);
}

static void rk81x_bat_fg_init(struct rk81x_battery *di)
{
	u8 pcb_offset;
	int cal_offset;
	u8 val;

	val = 0x30;
	rk81x_bat_write(di, ADC_CTRL_REG, &val, 1);

	rk81x_bat_gauge_enable(di);
	/* get the volatege offset */
	rk81x_bat_get_vol_offset(di);
	rk81x_bat_charger_init(di);
	rk81x_bat_set_relax_thres(di);

	/* get the current offset , the value write to the CAL_OFFSET */
	di->current_offset = rk81x_bat_get_ioffset(di);
	rk81x_bat_read(di, PCB_IOFFSET_REG, &pcb_offset, 1);
	DBG("<%s>. pcb_offset = 0x%x, io_offset = 0x%x\n",
	    __func__, pcb_offset, di->current_offset);
	if (!pcb_offset)
		pcb_offset = DEF_PCB_OFFSET;
	cal_offset = pcb_offset + di->current_offset;
	if (cal_offset < 0x7ff || cal_offset > 0x8ff)
		cal_offset = DEF_CAL_OFFSET;
	rk81x_bat_set_cal_offset(di, cal_offset);
	/* set sample time for cal_offset interval*/
	rk81x_bat_ioffset_sample_set(di, SAMP_TIME_8MIN);

	rk81x_bat_rsoc_init(di);
	rk81x_bat_capacity_init(di, di->nac);
	rk81x_bat_capacity_init_post(di);

	di->remain_capacity = rk81x_bat_get_realtime_capacity(di);
	di->current_avg = rk81x_bat_get_avg_current(di);

	rk81x_bat_restart_relax(di);
	rk81x_bat_power_on_save(di, di->voltage_ocv);
	val = 0;
	rk81x_bat_write(di, OCV_VOL_VALID_REG, &val, 1);

	rk81x_dbg_dmp_gauge_regs(di);
	rk81x_dbg_dmp_charger_regs(di);

	DBG("<%s> :\n"
	    "nac = %d , remain_capacity = %d\n"
	    "OCV_voltage = %d, voltage = %d\n"
	    "SOC = %d, fcc = %d\n, current=%d\n"
	    "cal_offset = 0x%x\n",
	    __func__,
	    di->nac, di->remain_capacity,
	    di->voltage_ocv, di->voltage,
	    di->dsoc, di->fcc, di->current_avg,
	    cal_offset);
}

static void rk81x_bat_zero_calc_linek(struct rk81x_battery *di)
{
	int dead_voltage, ocv_voltage;
	int voltage, voltage_old, voltage_now;
	int i, rsoc;
	int q_ocv, q_dead;
	int count_num = 0;
	int currentnow;
	int ocv_soc, dead_soc;
	int power_off_thresd = di->pdata->power_off_thresd;

	do {
		voltage_old = rk81x_bat_get_vol(di);
		msleep(100);
		voltage_now = rk81x_bat_get_vol(di);
		count_num++;
	} while ((voltage_old == voltage_now) && (count_num < 11));
	DBG("<%s>. current calc count=%d\n", __func__, count_num);

	voltage = 0;
	for (i = 0; i < 10; i++) {
		voltage += rk81x_bat_get_vol(di);
		msleep(100);
	}
	voltage /= 10;

	currentnow = rk81x_bat_get_avg_current(di);

	/* 50 mo power-path mos */
	dead_voltage = power_off_thresd - currentnow *
				(di->bat_res + DEF_PWRPATH_RES) / 1000;

	ocv_voltage = voltage - (currentnow * di->bat_res) / 1000;
	DBG("ZERO0: dead_voltage(shtd) = %d, ocv_voltage(now) = %d\n",
	    dead_voltage, ocv_voltage);

	dead_soc = rk81x_bat_vol_to_capacity(di, dead_voltage);
	q_dead = di->temp_nac;
	DBG("ZERO0: dead_voltage_soc = %d, q_dead = %d\n",
	    dead_soc, q_dead);

	ocv_soc = rk81x_bat_vol_to_capacity(di, ocv_voltage);
	q_ocv = di->temp_nac;
	DBG("ZERO0: ocv_voltage_soc = %d, q_ocv = %d\n",
	    ocv_soc, q_ocv);

	rsoc = ocv_soc - dead_soc;
	if ((di->dsoc == 1) && (rsoc > 0)) {/*discharge*/
		di->line_k = 1000;
	} else if (rsoc > 0) {
		di->line_k = (di->display_soc + rsoc / 2) / div(rsoc);
	} else {
		di->dsoc--;
		di->display_soc = di->dsoc * 1000;
	}

	di->zero_old_remain_cap = di->remain_capacity;

	DBG("ZERO-new: new-line_k=%d, dsoc=%d, X0soc=%d\n"
	    "ZERO-new: di->display_soc=%d, old_remain_cap=%d\n\n",
	    di->line_k, di->dsoc, rsoc,
	    di->display_soc, di->zero_old_remain_cap);
}

static void rk81x_bat_zero_algorithm(struct rk81x_battery *di)
{
	int delta_cap, delta_soc;
	int tmp_dsoc;

	di->zero_timeout_cnt++;
	delta_cap = di->zero_old_remain_cap - di->remain_capacity;
	delta_soc = di->line_k * (delta_cap * 100) / div(di->fcc);

	DBG("ZERO1: line_k=%d, display_soc(Y0)=%d, dsoc=%d, rsoc=%d\n"
	    "ZERO1: delta_soc(X0)=%d, delta_cap=%d, old_remain_cap = %d\n"
	    "ZERO1: timeout_cnt=%d\n\n",
	    di->line_k, di->display_soc, di->dsoc, di->rsoc,
	    delta_soc, delta_cap, di->zero_old_remain_cap,
	    di->zero_timeout_cnt);

	if ((delta_soc >= MIN_ZERO_ACCURACY) ||
	    (di->zero_timeout_cnt > 500)) {
		DBG("ZERO1:--------- enter calc -----------\n");
		di->zero_timeout_cnt = 0;
		di->display_soc -= delta_soc;
		tmp_dsoc = (di->display_soc + MIN_ROUND_ACCURACY) / 1000;
		di->dsoc = tmp_dsoc;
		/* need to be init, otherwise when switch between discharge and
		 * charge display_soc will be init as: dsoc * 1000
		 */
		di->last_zero_mode_dsoc = tmp_dsoc;
		DBG("ZERO1: display_soc(Y0)=%d, dsoc=%d, rsoc=%d, tmp_soc=%d",
		    di->display_soc, di->dsoc, di->rsoc, tmp_dsoc);

		rk81x_bat_zero_calc_linek(di);
	}
}

static int rk81x_bat_est_ocv_vol(struct rk81x_battery *di)
{
	return (di->voltage -
				(di->bat_res * di->current_avg) / 1000);
}

static int rk81x_bat_est_ocv_soc(struct rk81x_battery *di)
{
	int ocv_soc, ocv_voltage;

	ocv_voltage = rk81x_bat_est_ocv_vol(di);
	ocv_soc = rk81x_bat_vol_to_capacity(di, ocv_voltage);

	return ocv_soc;
}

/* we will estimate a ocv voltage to get a ocv soc.
 * if there is a big offset between ocv_soc and rsoc,
 * we will decide whether we should reinit capacity or not
 */
static void rk81x_bat_rsoc_dischrg_check(struct rk81x_battery *di)
{
	int ocv_soc = di->est_ocv_soc;
	int ocv_volt = di->est_ocv_vol;
	int rsoc = rk81x_bat_get_rsoc(di);
	int max_volt = di->pdata->max_charger_voltagemV;

	if (ocv_volt > max_volt)
		goto out;

	if (di->plug_out_min >= RSOC_CALIB_DISCHRGR_TIME) {
		if ((ocv_soc-rsoc >= RSOC_DISCHRG_ERR_LOWER) ||
		    (di->rsoc == 0) ||
		    (rsoc-ocv_soc >= RSOC_DISCHRG_ERR_UPPER)) {
			di->err_chck_cnt++;
			di->err_soc_sum += ocv_soc;
		} else {
			goto out;
		}
		DBG("<%s>. rsoc err_chck_cnt = %d, err_soc_sum = %d\n",
		    __func__, di->err_chck_cnt, di->err_soc_sum);

		if (di->err_chck_cnt >= RSOC_ERR_CHCK_CNT) {
			ocv_soc = di->err_soc_sum / RSOC_ERR_CHCK_CNT;
			if (rsoc-ocv_soc >= RSOC_DISCHRG_ERR_UPPER)
				ocv_soc += RSOC_COMPS;

			di->temp_nac = ocv_soc * di->fcc / 100;
			rk81x_bat_capacity_init(di, di->temp_nac);
			rk81x_bat_capacity_init_post(di);
			di->rsoc = rk81x_bat_get_rsoc(di);
			di->remain_capacity =
					rk81x_bat_get_realtime_capacity(di);
			di->err_soc_sum = 0;
			di->err_chck_cnt = 0;
			DBG("<%s>. update: rsoc = %d\n", __func__, ocv_soc);
		}
	 } else {
out:
		di->err_chck_cnt = 0;
		di->err_soc_sum = 0;
	}
}

static void rk81x_bat_rsoc_check(struct rk81x_battery *di)
{
	u8 status = di->psy_status;

	if ((status == POWER_SUPPLY_STATUS_CHARGING) ||
	    (status == POWER_SUPPLY_STATUS_FULL)) {
		if ((di->current_avg < 0) &&
		    (di->chrg_status != CHARGE_FINISH))
			rk81x_bat_rsoc_dischrg_check(di);
		/*
		else
			rsoc_chrg_calib(di);
		*/

	} else if (status == POWER_SUPPLY_STATUS_DISCHARGING) {
		rk81x_bat_rsoc_dischrg_check(di);
	}
}

static void rk81x_bat_emulator_dischrg(struct rk81x_battery *di)
{
	u32 temp, soc_time = 0;
	unsigned long sec_unit;

	if (!di->dischrg_emu_base)
		di->dischrg_emu_base = get_runtime_sec();

	sec_unit = BASE_TO_SEC(di->dischrg_emu_base) + di->dischrg_save_sec;

	temp = di->fcc * 3600 / 100;

	if (abs(di->current_avg) < DSOC_DISCHRG_EMU_CURR)
		soc_time = temp / div(abs(DSOC_DISCHRG_EMU_CURR));
	else
		soc_time = temp / div(abs(di->current_avg));

	if  (sec_unit > soc_time) {
		di->dsoc--;
		di->dischrg_emu_base = get_runtime_sec();
		di->dischrg_save_sec = 0;
	}

	DBG("<%s> soc_time=%d, sec_unit=%lu\n",
	    __func__, soc_time, sec_unit);
}

/*
 * when there is a big offset between dsoc and rsoc, dsoc needs to
 * speed up to keep pace witch rsoc.
 */
static void rk81x_bat_emulator_chrg(struct rk81x_battery *di)
{
	u32 soc_time = 0, temp;
	int plus_soc;
	unsigned long chrg_emu_sec;

	if (!di->chrg_emu_base)
		di->chrg_emu_base = get_runtime_sec();

	chrg_emu_sec = BASE_TO_SEC(di->chrg_emu_base) + di->chrg_save_sec;
	temp = di->fcc * 3600 / 100;

	if (di->ac_online) {
		if (di->current_avg < DSOC_CHRG_EMU_CURR)
			soc_time = temp / abs(DSOC_CHRG_EMU_CURR);
		else
			soc_time = temp / div(abs(di->current_avg));
	} else {
		soc_time = temp / 450;
	}

	plus_soc = chrg_emu_sec / soc_time;
	if  (chrg_emu_sec > soc_time) {
		di->dsoc += plus_soc;
		di->chrg_emu_base = get_runtime_sec();
		di->chrg_save_sec = 0;
	}

	DBG("<%s>. soc_time=%d, chrg_emu_sec=%lu, plus_soc=%d\n",
	    __func__, soc_time, chrg_emu_sec, plus_soc);
}

/* check voltage and current when dsoc is close to full.
 * we will do a fake charge to adjust charing speed which
 * aims to make battery full charged and match finish signal.
 */
static void rk81x_bat_terminal_chrg(struct rk81x_battery *di)
{
	u32 soc_time;
	int plus_soc;
	unsigned long chrg_term_sec;

	if (!di->chrg_term_base)
		di->chrg_term_base = get_runtime_sec();

	chrg_term_sec = BASE_TO_SEC(di->chrg_term_base) + di->chrg_save_sec;
	/*check current and voltage*/

	soc_time = di->fcc * 3600 / 100 / (abs(DSOC_CHRG_TERM_CURR));

	plus_soc = chrg_term_sec / soc_time;
	if  (chrg_term_sec > soc_time) {
		di->dsoc += plus_soc;
		di->chrg_term_base = get_runtime_sec();
		di->chrg_save_sec = 0;
	}
	DBG("<%s>. soc_time=%d, chrg_term_sec=%lu, plus_soc=%d\n",
	    __func__, soc_time, chrg_term_sec, plus_soc);
}

static void rk81x_bat_normal_dischrg(struct rk81x_battery *di)
{
	int soc_time = 0;
	int now_current = di->current_avg;
	unsigned long dischrg_normal_sec;

	if (!di->dischrg_normal_base)
		di->dischrg_normal_base = get_runtime_sec();

	dischrg_normal_sec = BASE_TO_SEC(di->dischrg_normal_base) +
						di->dischrg_save_sec;

	soc_time = di->fcc * 3600 / 100 / div(abs(now_current));
	DBG("<%s>. rsoc=%d, dsoc=%d, dischrg_st=%d\n",
	    __func__, di->rsoc, di->dsoc, di->discharge_smooth_status);

	if (di->rsoc == di->dsoc) {
		DBG("<%s>. rsoc == dsoc\n", __func__);
		di->dsoc = di->rsoc;
		di->dischrg_normal_base = get_runtime_sec();
		di->dischrg_save_sec = 0;
		/*di->discharge_smooth_status = false;*/
	} else if (di->rsoc > di->dsoc - 1) {
		DBG("<%s>. rsoc > dsoc - 1\n", __func__);
		if (dischrg_normal_sec > soc_time * 3 / 2) {
			di->dsoc--;
			di->dischrg_normal_base = get_runtime_sec();
			di->dischrg_save_sec = 0;
		}
		di->discharge_smooth_status = true;

	} else if (di->rsoc < di->dsoc - 1) {
		DBG("<%s>. rsoc < dsoc - 1\n", __func__);
		if (dischrg_normal_sec > soc_time * 3 / 4) {
			di->dsoc--;
			di->dischrg_normal_base = get_runtime_sec();
			di->dischrg_save_sec = 0;
		}
		di->discharge_smooth_status = true;

	} else if (di->rsoc == di->dsoc - 1) {
		DBG("<%s>. rsoc == dsoc - 1\n", __func__);
		if (di->discharge_smooth_status) {
			if (dischrg_normal_sec > soc_time * 3 / 4) {
				di->dsoc--;
				di->dischrg_normal_base = get_runtime_sec();
				di->dischrg_save_sec = 0;
				di->discharge_smooth_status = false;
			}
		} else {
			di->dsoc--;
			di->dischrg_normal_base = get_runtime_sec();
			di->dischrg_save_sec = 0;
			di->discharge_smooth_status = false;
		}
	}

	DBG("<%s>, rsoc = %d, dsoc = %d, discharge_smooth_status = %d\n"
	    "dischrg_normal_sec = %lu, soc_time = %d, delta_vol=%d\n",
	    __func__, di->rsoc, di->dsoc, di->discharge_smooth_status,
	    dischrg_normal_sec, soc_time, di->delta_vol_smooth);
}

static void rk81x_bat_dischrg_smooth(struct rk81x_battery *di)
{
	int delta_soc;
	int tmp_dsoc;

	/* first resume from suspend: we don't run this,
	 * the sleep_dischrg will handle dsoc, and what
	 * ever this is fake wakeup or not, we should clean
	 * zero algorithm mode, or it will handle the dsoc.
	 */
	if (di->s2r) {
		rk81x_bat_reset_zero_var(di);
		return;
	}

	di->rsoc = rk81x_bat_get_rsoc(di);

	DBG("<%s>. rsoc = %d, dsoc = %d, dischrg_algorithm_mode=%d\n",
	    __func__, di->rsoc, di->dsoc, di->dischrg_algorithm_mode);

	if (di->dischrg_algorithm_mode == DISCHRG_NORMAL_MODE) {
		delta_soc = di->dsoc - di->rsoc;

		if (delta_soc > DSOC_DISCHRG_FAST_EER_RANGE) {
			di->dischrg_normal_base = 0;
			rk81x_bat_emulator_dischrg(di);
		} else {
			di->chrg_emu_base = 0;
			rk81x_bat_normal_dischrg(di);
		}

		if (di->voltage < ZERO_ALGOR_THRESD) {
			di->dischrg_normal_base = 0;
			di->chrg_emu_base = 0;
			di->dischrg_algorithm_mode = DISCHRG_ZERO_MODE;
			di->zero_timeout_cnt = 0;

			DBG("<%s>. dsoc=%d, last_zero_mode_dsoc=%d\n",
			    __func__, di->dsoc, di->last_zero_mode_dsoc);
			if (di->dsoc != di->last_zero_mode_dsoc) {
				tmp_dsoc = (di->display_soc +
						MIN_ROUND_ACCURACY) / 1000;
				/* if last display_soc invalid, recalc.
				 * otherwise keep this value(in case: plugin and
				 * plugout quickly or wakeup from deep sleep,
				 * we need't init display_soc)
				 */
				if (tmp_dsoc != di->dsoc)
					/* first init value should round up,
					 * other wise dsoc will quickly turn to
					 * dsoc-- if MIN_ROUND_ACCURACY value is
					 * small,eg:1.(in case: power on system)
					 */
					di->display_soc = (di->dsoc + 1) *
						1000 - MIN_ROUND_ACCURACY;
				di->last_zero_mode_dsoc = di->dsoc;
				rk81x_bat_zero_calc_linek(di);
				DBG("<%s>. first calc, init linek\n", __func__);
			}
		}
	} else {
		rk81x_bat_zero_algorithm(di);

		if (di->voltage > ZERO_ALGOR_THRESD + 50) {
			di->dischrg_algorithm_mode = DISCHRG_NORMAL_MODE;
			di->zero_timeout_cnt = 0;
			DBG("<%s>. exit zero_algorithm\n", __func__);
		}
	}
}

static void rk81x_bat_dbg_time_table(struct rk81x_battery *di)
{
	u8 i;
	static int old_index;
	static int old_min;
	u32 time;
	int mod = di->dsoc % 10;
	int index = di->dsoc / 10;

	if (rk81x_chrg_online(di))
		time = di->plug_in_min;
	else
		time = di->plug_out_min;

	if ((mod == 0) && (index > 0) && (old_index != index)) {
		di->chrg_min[index-1] = time - old_min;
		old_min = time;
		old_index = index;
	}

	for (i = 1; i < 11; i++)
		DBG("Time[%d]=%d, ", (i * 10), di->chrg_min[i-1]);
	DBG("\n");
}

static void rk81x_bat_dbg_dmp_info(struct rk81x_battery *di)
{
	u8 sup_tst_reg, ggcon_reg, ggsts_reg, vb_mod_reg;
	u8 usb_ctrl_reg, chrg_ctrl_reg1, thremal_reg;
	u8 chrg_ctrl_reg2, chrg_ctrl_reg3, rtc_val, misc_reg;

	if (dbg_enable) {
		rk81x_bat_read(di, MISC_MARK_REG, &misc_reg, 1);
		rk81x_bat_read(di, GGCON, &ggcon_reg, 1);
		rk81x_bat_read(di, GGSTS, &ggsts_reg, 1);
		rk81x_bat_read(di, SUP_STS_REG, &sup_tst_reg, 1);
		rk81x_bat_read(di, VB_MOD_REG, &vb_mod_reg, 1);
		rk81x_bat_read(di, USB_CTRL_REG, &usb_ctrl_reg, 1);
		rk81x_bat_read(di, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
		rk81x_bat_read(di, CHRG_CTRL_REG2, &chrg_ctrl_reg2, 1);
		rk81x_bat_read(di, CHRG_CTRL_REG3, &chrg_ctrl_reg3, 1);
		rk81x_bat_read(di, 0x00, &rtc_val, 1);
		rk81x_bat_read(di, THERMAL_REG, &thremal_reg, 1);
	}

	DBG("\n------------- dump_debug_regs -----------------\n"
	    "GGCON = 0x%2x, GGSTS = 0x%2x, RTC	= 0x%2x\n"
	    "SUP_STS_REG  = 0x%2x, VB_MOD_REG	= 0x%2x\n"
	    "USB_CTRL_REG  = 0x%2x, CHRG_CTRL_REG1 = 0x%2x\n"
	    "THERMAL_REG = 0x%2x, MISC_MARK_REG = 0x%x\n"
	    "CHRG_CTRL_REG2 = 0x%2x, CHRG_CTRL_REG3 = 0x%2x\n\n",
	    ggcon_reg, ggsts_reg, rtc_val,
	    sup_tst_reg, vb_mod_reg,
	    usb_ctrl_reg, chrg_ctrl_reg1,
	    thremal_reg, misc_reg,
	    chrg_ctrl_reg2, chrg_ctrl_reg3
	   );

	DBG("#######################################################\n"
	    "voltage = %d, current-avg = %d\n"
	    "fcc = %d, remain_capacity = %d, ocv_volt = %d\n"
	    "check_ocv = %d, check_soc = %d, bat_res = %d\n"
	    "display_soc = %d, cpapacity_soc = %d\n"
	    "AC-ONLINE = %d, USB-ONLINE = %d, charging_status = %d\n"
	    "i_offset=0x%x, cal_offset=0x%x, adjust_cap=%d\n"
	    "plug_in = %d, plug_out = %d, finish_sig = %d, finish_chrg=%lu\n"
	    "sec: chrg=%lu, dischrg=%lu, term_chrg=%lu, emu_chrg=%lu\n"
	    "emu_dischrg = %lu, power_on_sec = %lu\n"
	    "mode:%d, save_chrg_sec = %lu, save_dischrg_sec = %lu\n"
	    "#########################################################\n",
	    di->voltage, di->current_avg,
	    di->fcc, di->remain_capacity, di->voltage_ocv,
	    di->est_ocv_vol, di->est_ocv_soc, di->bat_res,
	    di->dsoc, di->rsoc,
	    di->ac_online, di->usb_online, di->psy_status,
	    rk81x_bat_get_ioffset(di), rk81x_bat_get_cal_offset(di),
	    di->adjust_cap, di->plug_in_min, di->plug_out_min,
	    di->finish_sig_min, BASE_TO_SEC(di->chrg_finish_base),
	    BASE_TO_SEC(di->chrg_normal_base),
	    BASE_TO_SEC(di->dischrg_normal_base),
	    BASE_TO_SEC(di->chrg_term_base),
	    BASE_TO_SEC(di->chrg_emu_base),
	    BASE_TO_SEC(di->dischrg_emu_base),
	    BASE_TO_SEC(di->power_on_base),
	    di->current_mode, di->chrg_save_sec, di->dischrg_save_sec
	   );
}

static void rk81x_bat_update_fcc(struct rk81x_battery *di)
{
	int fcc0;
	int remain_cap;
	int dod0_to_soc100_min;

	remain_cap = di->remain_capacity - di->dod0_capacity - di->adjust_cap;
	dod0_to_soc100_min = BASE_TO_MIN(di->fcc_update_sec);

	DBG("%s: remain_cap:%d, ajust_cap:%d, dod0_status=%d\n"
	    "dod0_capacity:%d, dod0_to_soc100_min:%d\n",
	    __func__, remain_cap, di->adjust_cap, di->dod0_status,
	    di->dod0_capacity, dod0_to_soc100_min);

	if ((di->chrg_status == CHARGE_FINISH) && (di->dod0_status == 1) &&
	    (dod0_to_soc100_min < 1200)) {
		DBG("%s: dod0:%d, dod0_cap:%d, dod0_level:%d\n",
		    __func__, di->dod0, di->dod0_capacity, di->dod0_level);

		fcc0 = remain_cap * 100 / div(100 - di->dod0);

		dev_info(di->dev, "%s: fcc0:%d, fcc:%d\n",
			 __func__, fcc0, di->fcc);

		if ((fcc0 < di->qmax) && (fcc0 > 1000)) {
			di->dod0_status = 0;
			di->fcc = fcc0;
			rk81x_bat_capacity_init(di, di->fcc);
			rk81x_bat_capacity_init_post(di);
			rk81x_bat_save_fcc(di, di->fcc);
			rk81x_bat_save_level(di, di->dod0_level);
			DBG("%s: new fcc0:%d\n", __func__, di->fcc);
		}

		di->dod0_status = 0;
	}
}

static void rk81x_bat_dbg_get_finish_soc(struct rk81x_battery *di)
{
	if (di->chrg_status == CHARGE_FINISH) {
		di->debug_finish_real_soc = di->dsoc;
		di->debug_finish_temp_soc = di->rsoc;
	}
}

static void rk81x_bat_wait_finish_sig(struct rk81x_battery *di)
{
	int chrg_finish_vol = di->pdata->max_charger_voltagemV;
	bool ret;

	if ((di->chrg_status == CHARGE_FINISH) &&
	    (di->voltage > chrg_finish_vol - 150) &&  di->enter_finish) {
		rk81x_bat_update_fcc(di);/* save new fcc*/
		ret = rk81x_bat_zero_current_calib(di);
		if (ret)
			di->enter_finish = false;
		/* debug msg*/
		rk81x_bat_dbg_get_finish_soc(di);
	}
}

static void rk81x_bat_finish_chrg(struct rk81x_battery *di)
{
	unsigned long sec_finish;
	int soc_time = 0, plus_soc;
	int temp;

	if (di->dsoc < 100) {
		if (!di->chrg_finish_base)
			di->chrg_finish_base = get_runtime_sec();

		sec_finish = BASE_TO_SEC(di->chrg_finish_base) +
						di->chrg_save_sec;
		temp = di->fcc * 3600 / 100;
		if (di->ac_online)
			soc_time = temp / DSOC_CHRG_FINISH_CURR;
		else
			soc_time = temp / 450;

		plus_soc = sec_finish / soc_time;
		if (sec_finish > soc_time) {
			di->dsoc += plus_soc;
			di->chrg_finish_base = get_runtime_sec();
			di->chrg_save_sec = 0;
		}
		DBG("<%s>,CHARGE_FINISH:dsoc<100,dsoc=%d\n"
		    "soc_time=%d, sec_finish=%lu, plus_soc=%d\n",
		    __func__, di->dsoc, soc_time, sec_finish, plus_soc);
	}
}

static u8 rk81x_bat_get_valid_soc(unsigned long soc)
{
	return (soc <= 100) ? soc : 0;
}

static void rk81x_bat_normal_chrg(struct rk81x_battery *di)
{
	int now_current;
	u32 soc_time, unit_sec;
	int plus_soc = 0;
	unsigned long chrg_normal_sec;

	now_current = rk81x_bat_get_avg_current(di);
	soc_time = di->fcc * 3600 / 100 / div(abs(now_current)); /*1% time*/

	if (!di->chrg_normal_base)
		di->chrg_normal_base = get_runtime_sec();

	chrg_normal_sec = BASE_TO_SEC(di->chrg_normal_base) + di->chrg_save_sec;
	di->rsoc = rk81x_bat_get_rsoc(di);

	DBG("<%s>. rsoc=%d, dsoc=%d, chrg_st=%d\n",
	    __func__, di->rsoc, di->dsoc, di->charge_smooth_status);

	if (di->dsoc == di->rsoc) {
		DBG("<%s>. rsoc == dsoc + 1\n", __func__);
		di->rsoc = rk81x_bat_get_rsoc(di);
		di->chrg_normal_base = get_runtime_sec();
		di->chrg_save_sec = 0;
		/*di->charge_smooth_status = false;*/
	} else if (di->rsoc < di->dsoc + 1) {
		DBG("<%s>. rsoc < dsoc + 1\n", __func__);
		unit_sec = soc_time * 3 / 2;
		plus_soc = rk81x_bat_get_valid_soc(chrg_normal_sec / unit_sec);
		if  (chrg_normal_sec > unit_sec) {
			di->dsoc += plus_soc;
			di->chrg_normal_base = get_runtime_sec();
			di->chrg_save_sec = 0;
		}
		di->charge_smooth_status = true;
	} else if (di->rsoc > di->dsoc + 1) {
		DBG("<%s>. rsoc > dsoc + 1\n", __func__);
		unit_sec = soc_time * 3 / 4;
		plus_soc = rk81x_bat_get_valid_soc(chrg_normal_sec / unit_sec);
		if  (chrg_normal_sec > unit_sec) {
			di->dsoc += plus_soc;
			di->chrg_normal_base = get_runtime_sec();
			di->chrg_save_sec = 0;
		}
		di->charge_smooth_status = true;
	} else if (di->rsoc == di->dsoc + 1) {
		DBG("<%s>. rsoc == dsoc + 1\n", __func__);
		if (di->charge_smooth_status) {
			unit_sec = soc_time * 3 / 4;
			if (chrg_normal_sec > unit_sec) {
				di->dsoc = di->rsoc;
				di->chrg_normal_base = get_runtime_sec();
				di->charge_smooth_status = false;
				di->chrg_save_sec = 0;
			}
		} else {
			di->dsoc = di->rsoc;
			di->chrg_normal_base = get_runtime_sec();
			di->charge_smooth_status = false;
			di->chrg_save_sec = 0;
		}
	}

	DBG("<%s>, rsoc = %d, dsoc = %d, charge_smooth_status = %d\n"
	    "chrg_normal_sec = %lu, soc_time = %d, plus_soc=%d\n",
	    __func__, di->rsoc, di->dsoc, di->charge_smooth_status,
	    chrg_normal_sec, soc_time, plus_soc);
}

static void rk81x_bat_update_time(struct rk81x_battery *di)
{
	u64 runtime_sec;

	runtime_sec = get_runtime_sec();

	/*update by charger type*/
	if (rk81x_chrg_online(di))
		di->plug_out_base = runtime_sec;
	else
		di->plug_in_base = runtime_sec;

	/*update by current*/
	if (di->chrg_status != CHARGE_FINISH) {
		di->finish_sig_base = runtime_sec;
		di->chrg_finish_base = runtime_sec;
	}

	di->plug_in_min = BASE_TO_MIN(di->plug_in_base);
	di->plug_out_min = BASE_TO_MIN(di->plug_out_base);
	di->finish_sig_min = BASE_TO_MIN(di->finish_sig_base);

	rk81x_bat_dbg_time_table(di);
}

static int rk81x_bat_get_rsoc_trend(struct rk81x_battery *di, int *trend_mult)
{
	int trend_start_cap = di->trend_start_cap;
	int remain_cap = di->remain_capacity;
	int diff_cap;
	int state;

	if (di->s2r && !di->slp_psy_status)
		di->trend_start_cap = di->remain_capacity;

	diff_cap = remain_cap - trend_start_cap;
	DBG("<%s>. trend_start_cap = %d, diff_cap = %d\n",
	    __func__, trend_start_cap, diff_cap);
	*trend_mult = abs(diff_cap) / TREND_CAP_DIFF;

	if (abs(diff_cap) >= TREND_CAP_DIFF) {
		di->trend_start_cap = di->remain_capacity;
		state = (diff_cap > 0) ? TREND_STAT_UP : TREND_STAT_DOWN;
		DBG("<%s>. new trend_start_cap=%d", __func__, trend_start_cap);
	} else {
		state = TREND_STAT_FLAT;
	}

	return state;
}

static void rk81x_bat_arbitrate_rsoc_trend(struct rk81x_battery *di)
{
	int state, soc_time;
	static int trend_down_cnt, trend_up_cnt;
	int trend_cnt_thresd;
	int now_current = di->current_avg;
	int trend_mult = 0;

	trend_cnt_thresd = di->fcc / 100 / TREND_CAP_DIFF;
	state = rk81x_bat_get_rsoc_trend(di, &trend_mult);
	DBG("<%s>. TREND_STAT = %d, trend_mult = %d\n",
	    __func__, state, trend_mult);
	if (di->chrg_status == CHARGE_FINISH)
		return;

	if (state == TREND_STAT_UP) {
		rk81x_bat_reset_zero_var(di);
		trend_down_cnt = 0;
		trend_up_cnt += trend_mult;
		if (trend_up_cnt >= trend_cnt_thresd) {
			trend_up_cnt = 0;
			di->dischrg_save_sec = 0;
		}
	} else if (state == TREND_STAT_DOWN) {
		trend_up_cnt = 0;
		trend_down_cnt += trend_mult;
		if (trend_down_cnt >= trend_cnt_thresd) {
			trend_down_cnt = 0;
			di->chrg_save_sec = 0;
		}
	}

	soc_time = di->fcc * 3600 / 100 / div(abs(now_current));
	if ((di->chrg_save_sec * 3 / 4 > soc_time) &&
	    (trend_up_cnt <= trend_cnt_thresd / 2) &&
	    (now_current >= 0))
		di->chrg_save_sec = 0;

	else if ((di->dischrg_save_sec * 3 / 4 > soc_time) &&
		 (trend_down_cnt <= trend_cnt_thresd / 2) &&
		 (now_current < 0))
		di->dischrg_save_sec = 0;

	DBG("<%s>. state=%d, cnt_thresd=%d, soc_time=%d\n"
	    "up_cnt=%d, down_cnt=%d\n",
	    __func__, state, trend_cnt_thresd, soc_time,
	    trend_up_cnt, trend_down_cnt);
}

static void rk81x_bat_chrg_smooth(struct rk81x_battery *di)
{
	u32 *ocv_table = di->pdata->battery_ocv;
	int delta_soc = di->rsoc - di->dsoc;

	if (di->chrg_status == CHARGE_FINISH ||
	    di->slp_chrg_status == CHARGE_FINISH) {
		/*clear sleep charge status*/
		di->slp_chrg_status = rk81x_bat_get_chrg_status(di);
		di->chrg_emu_base = 0;
		di->chrg_normal_base = 0;
		di->chrg_term_base = 0;
		rk81x_bat_finish_chrg(di);
		rk81x_bat_capacity_init(di, di->fcc);
		rk81x_bat_capacity_init_post(di);
	} else if ((di->ac_online == ONLINE && di->dsoc >= 90) &&
		   ((di->current_avg > DSOC_CHRG_TERM_CURR) ||
		    (di->voltage < ocv_table[18] + 20))) {
		di->chrg_emu_base = 0;
		di->chrg_normal_base = 0;
		di->chrg_finish_base = 0;
		rk81x_bat_terminal_chrg(di);
	} else if (di->chrg_status != CHARGE_FINISH &&
		   delta_soc >= DSOC_CHRG_FAST_EER_RANGE) {
		di->chrg_term_base = 0;
		di->chrg_normal_base = 0;
		di->chrg_finish_base = 0;
		rk81x_bat_emulator_chrg(di);
	} else {
		di->chrg_emu_base = 0;
		di->chrg_term_base = 0;
		di->chrg_finish_base = 0;
		rk81x_bat_normal_chrg(di);
	}
}

static unsigned long rk81x_bat_save_dischrg_sec(struct rk81x_battery *di)
{
	unsigned long dischrg_normal_sec = BASE_TO_SEC(di->dischrg_normal_base);
	unsigned long dischrg_emu_sec = BASE_TO_SEC(di->dischrg_emu_base);

	DBG("dischrg_normal_sec=%lu, dischrg_emu_sec=%lu\n",
	    dischrg_normal_sec, dischrg_emu_sec);

	return (dischrg_normal_sec > dischrg_emu_sec) ?
		dischrg_normal_sec : dischrg_emu_sec;
}

static unsigned long rk81x_bat_save_chrg_sec(struct rk81x_battery *di)
{
	unsigned long sec1, sec2;
	unsigned long chrg_normal_sec = BASE_TO_SEC(di->chrg_normal_base);
	unsigned long chrg_term_sec = BASE_TO_SEC(di->chrg_term_base);
	unsigned long chrg_emu_sec = BASE_TO_SEC(di->chrg_emu_base);
	unsigned long chrg_finish_sec = BASE_TO_SEC(di->chrg_finish_base);

	sec1 = (chrg_normal_sec > chrg_term_sec) ?
		chrg_normal_sec : chrg_term_sec;

	sec2 = (chrg_emu_sec > chrg_finish_sec) ?
		chrg_emu_sec : chrg_finish_sec;
	DBG("chrg_normal_sec=%lu, chrg_term_sec=%lu\n"
	    "chrg_emu_sec=%lu, chrg_finish_sec=%lu\n",
	    chrg_normal_sec, chrg_term_sec,
	    chrg_emu_sec, chrg_finish_sec);

	return (sec1 > sec2) ? sec1 : sec2;
}

static void rk81x_bat_display_smooth(struct rk81x_battery *di)
{
	if ((di->current_avg >= 0) || (di->chrg_status == CHARGE_FINISH)) {
		if (di->current_mode == DISCHRG_MODE) {
			di->current_mode = CHRG_MODE;
			di->dischrg_save_sec += rk81x_bat_save_dischrg_sec(di);
			di->dischrg_normal_base = 0;
			di->dischrg_emu_base = 0;
			if (di->chrg_status == CHARGE_FINISH)
				di->dischrg_save_sec = 0;
			if ((di->chrg_status == CHARGE_FINISH) &&
			    (di->dsoc >= 100))
				di->chrg_save_sec = 0;

			DBG("<%s>---dischrg_save_sec = %lu\n",
			    __func__, di->dischrg_save_sec);
		}

		if (!rk81x_chrg_online(di)) {
			dev_err(di->dev, "discharge, current error:%d\n",
				di->current_avg);
		} else {
			rk81x_bat_chrg_smooth(di);
			di->discharge_smooth_status = true;
		}
	} else {
		if (di->current_mode == CHRG_MODE) {
			di->current_mode = DISCHRG_MODE;
			di->chrg_save_sec += rk81x_bat_save_chrg_sec(di);
			di->chrg_normal_base = 0;
			di->chrg_emu_base = 0;
			di->chrg_term_base = 0;
			di->chrg_finish_base = 0;
			DBG("<%s>---chrg_save_sec = %lu\n",
			    __func__, di->chrg_save_sec);
		}
		rk81x_bat_dischrg_smooth(di);
		di->charge_smooth_status = true;
	}
}

/*
 * update rsoc by relax voltage
 */
static void rk81x_bat_relax_vol_calib(struct rk81x_battery *di)
{
	int relax_vol = di->relax_voltage;
	int ocv_soc, capacity;

	ocv_soc = rk81x_bat_vol_to_capacity(di, relax_vol);
	capacity = (ocv_soc * di->fcc / 100);
	rk81x_bat_capacity_init(di, capacity);
	di->remain_capacity = rk81x_bat_get_realtime_capacity(di);
	di->rsoc = rk81x_bat_get_rsoc(di);
	rk81x_bat_capacity_init_post(di);
	DBG("%s, RSOC=%d, CAP=%d\n", __func__, ocv_soc, capacity);
}

/* condition:
 * 1: must do it, 0: when necessary
 */
static void rk81x_bat_vol_calib(struct rk81x_battery *di, int condition)
{
	int ocv_vol = di->est_ocv_vol;
	int ocv_soc = 0, capacity = 0;

	ocv_soc = rk81x_bat_vol_to_capacity(di, ocv_vol);
	capacity = (ocv_soc * di->fcc / 100);
	if (condition || (abs(ocv_soc-di->rsoc) >= RSOC_RESUME_ERR)) {
		rk81x_bat_capacity_init(di, capacity);
		di->remain_capacity = rk81x_bat_get_realtime_capacity(di);
		di->rsoc = rk81x_bat_get_rsoc(di);
		rk81x_bat_capacity_init_post(di);
		DBG("<%s>, rsoc updated!\n", __func__);
	}
	DBG("<%s>, OCV_VOL=%d,OCV_SOC=%d, CAP=%d\n",
	    __func__, ocv_vol, ocv_soc, capacity);
}

static int  rk81x_bat_sleep_dischrg(struct rk81x_battery *di)
{
	int delta_soc = 0;
	int temp_dsoc;
	unsigned long sleep_sec = di->suspend_time_sum;
	int power_off_thresd = di->pdata->power_off_thresd;

	DBG("<%s>, enter: dsoc=%d, rsoc=%d\n"
	    "relax_vol=%d, vol=%d, sleep_min=%lu\n",
	    __func__, di->dsoc, di->rsoc,
	    di->relax_voltage, di->voltage, sleep_sec / 60);

	if (di->relax_voltage >= di->voltage) {
		rk81x_bat_relax_vol_calib(di);
		rk81x_bat_restart_relax(di);

	/* current_avg < 0: make sure the system is not
	 * wakeup by charger plugin.
	 */
	/* even if relax voltage is not caught rightly, realtime voltage
	 * is quite close to relax voltage, we should not do nothing after
	 * sleep 30min
	 */
	} else  {
		rk81x_bat_vol_calib(di, 1);
	}

	/*handle dsoc*/
	if (di->dsoc <= di->rsoc) {
		di->sum_suspend_cap = (SLP_CURR_MIN * sleep_sec / 3600);
		delta_soc = di->sum_suspend_cap * 100 / di->fcc;
		temp_dsoc = di->dsoc - delta_soc;

		pr_info("battery calib0: rl=%d, dl=%d, intl=%d\n",
			di->rsoc, di->dsoc, delta_soc);

		if (delta_soc > 0) {
			if ((temp_dsoc < di->dsoc) && (di->dsoc < 5))
				di->dsoc--;
			else if ((temp_dsoc < 5) && (di->dsoc >= 5))
				di->dsoc = 5;
			else if (temp_dsoc > 5)
				di->dsoc = temp_dsoc;
		}

		DBG("%s: dsoc<=rsoc, sum_cap=%d==>delta_soc=%d,temp_dsoc=%d\n",
		    __func__, di->sum_suspend_cap, delta_soc, temp_dsoc);
	} else {
		/*di->dsoc > di->rsoc*/
		di->sum_suspend_cap = (SLP_CURR_MAX * sleep_sec / 3600);
		delta_soc = di->sum_suspend_cap / (di->fcc / 100);
		temp_dsoc = di->dsoc - di->rsoc;

		pr_info("battery calib1: rsoc=%d, dsoc=%d, intsoc=%d\n",
			di->rsoc, di->dsoc, delta_soc);

		if ((di->est_ocv_vol > SLP_DSOC_VOL_THRESD) &&
		    (temp_dsoc > delta_soc))
			di->dsoc -= delta_soc;
		else
			di->dsoc = di->rsoc;

		DBG("%s: dsoc > rsoc, sum_cap=%d==>delta_soc=%d,temp_dsoc=%d\n",
		    __func__, di->sum_suspend_cap, delta_soc, temp_dsoc);
	}

	if (!di->relax_voltage && di->voltage <= power_off_thresd)
		di->dsoc = 0;

	if (di->dsoc <= 0)
		di->dsoc = 0;

	DBG("<%s>, out: dsoc=%d, rsoc=%d, sum_cap=%d\n",
	    __func__, di->dsoc, di->rsoc, di->sum_suspend_cap);

	return delta_soc;
}

static int rk81x_bat_sleep_chrg(struct rk81x_battery *di)
{
	int sleep_soc = 0;
	unsigned long sleep_sec;

	sleep_sec = di->suspend_time_sum;
	if (((di->suspend_charge_current < 800) &&
	     (di->ac_online == ONLINE)) ||
	     (di->chrg_status == CHARGE_FINISH)) {
		DBG("<%s>,sleep: ac online current < 800\n", __func__);
		if (sleep_sec > 0) {
			/*default charge current: 1000mA*/
			sleep_soc = SLP_CHRG_CURR * sleep_sec * 100
						/ 3600 / div(di->fcc);
		}
	} else {
		DBG("<%s>, usb charge\n", __func__);
	}

	return sleep_soc;
}

/*
 * only do report when there is a change.
 *
 * if ((di->dsoc == 0) && (di->fg_drv_mode == FG_NORMAL_MODE)):
 * when dsoc == 0, we must do report. But it will generate too much android
 * info when we enter test_power mode without battery, so we add a fg_drv_mode
 * ajudgement.
 */
static void rk81x_bat_power_supply_changed(struct rk81x_battery *di)
{
	static u32 old_soc;
	static u32 old_ac_status;
	static u32 old_usb_status;
	static u32 old_charge_status;
	bool state_changed;

	state_changed = false;
	if ((di->dsoc == 0) && (di->fg_drv_mode == FG_NORMAL_MODE))
		state_changed = true;
	else if (di->dsoc != old_soc)
		state_changed = true;
	else if (di->ac_online != old_ac_status)
		state_changed = true;
	else if (di->usb_online != old_usb_status)
		state_changed = true;
	else if (old_charge_status != di->psy_status)
		state_changed = true;

	if (rk81x_chrg_online(di)) {
		if (di->dsoc == 100)
			di->psy_status = POWER_SUPPLY_STATUS_FULL;
		else
			di->psy_status = POWER_SUPPLY_STATUS_CHARGING;
	}

	if (state_changed) {
		power_supply_changed(&di->bat);
		power_supply_changed(&di->usb);
		power_supply_changed(&di->ac);
		old_soc = di->dsoc;
		old_ac_status = di->ac_online;
		old_usb_status = di->usb_online;
		old_charge_status = di->psy_status;
		dev_info(di->dev, "changed: dsoc=%d, rsoc=%d\n",
			 di->dsoc, di->rsoc);
	}
}

#if 0
static u8 rk81x_bat_get_cvcc_chrg_hour(struct rk81x_battery *di)
{
	u8 hour, buf;

	rk81x_bat_read(di, CHRG_CTRL_REG2, &buf, 1);
	hour = buf & 0x07;

	return CHRG_CVCC_HOUR[hour];
}

/* we have to estimate the charging finish time from now, to decide
 * whether we should reset the timer or not.
 */
static void rk81x_bat_chrg_over_time_check(struct rk81x_battery *di)
{
	u8 cvcc_hour;
	int remain_capacity;

	cvcc_hour = rk81x_bat_get_cvcc_chrg_hour(di);
	if (di->dsoc < di->rsoc)
		remain_capacity = di->dsoc * di->fcc / 100;
	else
		remain_capacity = di->remain_capacity;

	DBG("CHRG_TIME(min): %ld, cvcc hour: %d",
	    BASE_TO_MIN(di->plug_in_base), cvcc_hour);

	if (BASE_TO_MIN(di->plug_in_base) >= (cvcc_hour - 2) * 60) {
		di->chrg_cap2full = di->fcc - remain_capacity;
		if (di->current_avg <= 0)
			di->current_avg = 1;

		di->chrg_time2full = di->chrg_cap2full * 3600 /
					div(abs(di->current_avg));

		DBG("CHRG_TIME2FULL(min):%d, chrg_cap2full=%d, current=%d\n",
		    SEC_TO_MIN(di->chrg_time2full), di->chrg_cap2full,
		    di->current_avg);

		if (SEC_TO_MIN(di->chrg_time2full) > 60) {
			/*rk81x_bat_init_chrg_timer(di);*/
			di->plug_in_base = get_runtime_sec();
			DBG("%s: reset charge timer\n", __func__);
		}
	}
}
#endif

/*
 * in case that we will do reboot stress test, we need a special way
 * to ajust the dsoc.
 */
static void rk81x_bat_check_reboot(struct rk81x_battery *di)
{
	u8 rsoc = di->rsoc;
	u8 dsoc = di->dsoc;
	u8 cnt;
	int unit_time;
	int smooth_time;

	rk81x_bat_read(di, REBOOT_CNT_REG, &cnt, 1);
	cnt++;

	unit_time = di->fcc * 3600 / 100 / 1200;/*1200mA default*/
	smooth_time = cnt * BASE_TO_SEC(di->power_on_base);

	DBG("%s: cnt:%d, unit:%d, sm:%d, sec:%lu, dsoc:%d, rsoc:%d\n",
	    __func__, cnt, unit_time, smooth_time,
	    BASE_TO_SEC(di->power_on_base), dsoc, rsoc);

	if (di->current_avg >= 0 || di->chrg_status == CHARGE_FINISH) {
		DBG("chrg, sm:%d, aim:%d\n", smooth_time, unit_time * 3 / 5);
		if ((dsoc < rsoc - 1) && (smooth_time > unit_time * 3 / 5)) {
			cnt = 0;
			dsoc++;
			if (dsoc >= 100)
				dsoc = 100;
			rk81x_bat_save_dsoc(di, dsoc);
		}
	} else {
		DBG("dischrg, sm:%d, aim:%d\n", smooth_time, unit_time * 3 / 5);
		if ((dsoc > rsoc) && (smooth_time > unit_time * 3 / 5)) {
			cnt = 0;
			dsoc--;
			if (dsoc <= 0)
				dsoc = 0;
			rk81x_bat_save_dsoc(di, dsoc);
		}
	}

	rk81x_bat_save_reboot_cnt(di, cnt);
}

static void rk81x_bat_update_calib_param(struct rk81x_battery *di)
{
	static u32 old_min;
	u32 min;
	int current_offset;
	uint16_t cal_offset;
	u8 pcb_offset = DEF_PCB_OFFSET;

	min = BASE_TO_MIN(di->power_on_base);
	if ((min % 8) && (old_min != min)) {
		old_min = min;
		rk81x_bat_get_vol_offset(di);
		if (di->pcb_ioffset_updated)
			rk81x_bat_read(di, PCB_IOFFSET_REG, &pcb_offset, 1);

		current_offset = rk81x_bat_get_ioffset(di);
		rk81x_bat_set_cal_offset(di, current_offset + pcb_offset);
		cal_offset = rk81x_bat_get_cal_offset(di);
		if (cal_offset < 0x7ff)
			rk81x_bat_set_cal_offset(di, di->current_offset +
						 DEF_PCB_OFFSET);
		DBG("<%s>. k=%d, b=%d, cal_offset=%d, i_offset=%d\n",
		    __func__, di->voltage_k, di->voltage_b, cal_offset,
		    rk81x_bat_get_ioffset(di));
	}
}

static void rk81x_bat_update_info(struct rk81x_battery *di)
{
	if (di->dsoc > 100)
		di->dsoc = 100;
	else if (di->dsoc < 0)
		di->dsoc = 0;

	/*
	 * we need update fcc in continuous charging state, if discharge state
	 * keep at least 2 hour, we decide not to update fcc, so clear the
	 * fcc update flag: dod0_status.
	 */
	if (BASE_TO_MIN(di->plug_out_base) > 120)
		di->dod0_status = 0;

	di->voltage  = rk81x_bat_get_vol(di);
	di->current_avg = rk81x_bat_get_avg_current(di);
	di->chrg_status = rk81x_bat_get_chrg_status(di);
	di->relax_voltage = rk81x_bat_get_relax_vol(di);
	di->est_ocv_vol = rk81x_bat_est_ocv_vol(di);
	di->est_ocv_soc = rk81x_bat_est_ocv_soc(di);
	/*rk81x_bat_chrg_over_time_check(di);*/
	rk81x_bat_update_calib_param(di);
	if (di->chrg_status == CC_OR_CV)
		di->enter_finish = true;

	if (!rk81x_chrg_online(di) && di->s2r)
		return;

	di->remain_capacity = rk81x_bat_get_realtime_capacity(di);
	if (di->remain_capacity > di->fcc) {
		rk81x_bat_capacity_init(di, di->fcc);
		rk81x_bat_capacity_init_post(di);
		di->remain_capacity = di->fcc;
	}

	di->rsoc = rk81x_bat_get_rsoc(di);
}

static int rk81x_bat_update_resume_state(struct rk81x_battery *di)
{
	if (di->slp_psy_status)
		return rk81x_bat_sleep_chrg(di);
	else
		return rk81x_bat_sleep_dischrg(di);
}

static void rk81x_bat_fcc_flag_check(struct rk81x_battery *di)
{
	u8 ocv_soc, soc_level;
	int relax_vol = di->relax_voltage;

	if (relax_vol <= 0)
		return;

	ocv_soc = rk81x_bat_vol_to_capacity(di, relax_vol);
	DBG("<%s>. ocv_soc=%d, min=%lu, vol=%d\n", __func__,
	    ocv_soc, SEC_TO_MIN(di->suspend_time_sum), relax_vol);

	if ((SEC_TO_MIN(di->suspend_time_sum) > 30) &&
	    (di->dod0_status == 0) &&
	    (ocv_soc <= 10)) {
		di->dod0_voltage = relax_vol;
		di->dod0_capacity = di->temp_nac;
		di->adjust_cap = 0;
		di->dod0 = ocv_soc;

		if (ocv_soc <= 1)
			di->dod0_level = 100;
		else if (ocv_soc < 5)
			di->dod0_level = 90;
		else
			di->dod0_level = 80;

		/* save_soc = di->dod0_level; */
		soc_level = rk81x_bat_get_level(di);
		if (soc_level >  di->dod0_level) {
			di->dod0_status = 0;
		} else {
			di->dod0_status = 1;
			/*time start*/
			di->fcc_update_sec = get_runtime_sec();
		}

		dev_info(di->dev, "resume: relax_vol:%d, dod0_cap:%d\n"
			 "dod0:%d, soc_level:%d: dod0_status:%d\n"
			 "dod0_level:%d",
			 di->dod0_voltage, di->dod0_capacity,
			 ocv_soc, soc_level, di->dod0_status,
			 di->dod0_level);
	}
}

static void rk81x_chrg_term_mode_set(struct rk81x_battery *di, int mode)
{
	u8 buf;
	u8 mask = 0x20;

	rk81x_bat_read(di, CHRG_CTRL_REG3, &buf, 1);
	buf &= ~mask;
	buf |= mode;
	rk81x_bat_write(di, CHRG_CTRL_REG3, &buf, 1);

	dev_info(di->dev, "set charge to %s termination mode\n",
		 mode ? "digital" : "analog");
}

static void rk81x_chrg_term_mode_switch_work(struct work_struct *work)
{
	struct rk81x_battery *di;

	di = container_of(work, struct rk81x_battery,
			  chrg_term_mode_switch_work.work);

	if (rk81x_chrg_online(di))
		rk81x_chrg_term_mode_set(di, CHRG_TERM_DIG_SIGNAL);
	else
		rk81x_chrg_term_mode_set(di, CHRG_TERM_ANA_SIGNAL);
}

static void rk81x_battery_work(struct work_struct *work)
{
	struct rk81x_battery *di;
	int ms = TIMER_MS_COUNTS;

	di = container_of(work, struct rk81x_battery,
			  battery_monitor_work.work);
	if (rk81x_chrg_online(di)) {
		rk81x_bat_wait_finish_sig(di);
		/*rk81x_bat_chrg_finish_routine(di);*/
	}
	rk81x_bat_fcc_flag_check(di);
	rk81x_bat_arbitrate_rsoc_trend(di);
	rk81x_bat_display_smooth(di);
	rk81x_bat_update_time(di);
	rk81x_bat_update_info(di);
	rk81x_bat_rsoc_check(di);
	rk81x_bat_power_supply_changed(di);
	rk81x_bat_save_dsoc(di, di->dsoc);
	rk81x_bat_save_remain_capacity(di, di->remain_capacity);

	rk81x_bat_dbg_dmp_info(di);

	if (!di->early_resume && di->s2r && !di->slp_psy_status)
		ms = 30 * TIMER_MS_COUNTS;
	else
		di->early_resume = 0;

	di->s2r = 0;

	queue_delayed_work(di->wq, &di->battery_monitor_work,
			   msecs_to_jiffies(ms));
}

#if defined(CONFIG_ARCH_ROCKCHIP)
static void rk81x_battery_otg_delay_work(struct work_struct *work)
{
	struct rk81x_battery *di = container_of(work,
			struct rk81x_battery, otg_check_work.work);

	enum bc_port_type event = di->charge_otg;

	/* do not touch CHRG_CTRL_REG1[7]: CHRG_EN, hardware can
	 * recognize otg plugin and will auto ajust this bit
	 */
	switch (event) {
	case USB_OTG_POWER_ON:
		di->otg_online = ONLINE;
		if (power_dc2otg && di->dc_online) {
			dev_info(di->dev, "otg power from dc adapter\n");
			return;
		}
		dev_info(di->dev, "charge disable, otg enable\n");
		rk81x_bat_set_otg_state(di, USB_OTG_POWER_ON);
		break;

	case USB_OTG_POWER_OFF:
		dev_info(di->dev, "charge enable, otg disable\n");
		di->otg_online = OFFLINE;
		rk81x_bat_set_otg_state(di, USB_OTG_POWER_OFF);
		/*maybe dc still plugin*/
		queue_delayed_work(di->wq, &di->dc_det_check_work,
				   msecs_to_jiffies(10));
		break;

	default:
		break;
	}
}

static BLOCKING_NOTIFIER_HEAD(battery_chain_head);

int register_battery_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&battery_chain_head, nb);
}
EXPORT_SYMBOL_GPL(register_battery_notifier);

int unregister_battery_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&battery_chain_head, nb);
}
EXPORT_SYMBOL_GPL(unregister_battery_notifier);

int battery_notifier_call_chain(unsigned long val)
{
	return (blocking_notifier_call_chain(&battery_chain_head, val, NULL)
		== NOTIFY_BAD) ? -EINVAL : 0;
}
EXPORT_SYMBOL_GPL(battery_notifier_call_chain);

static int rk81x_bat_usb_notifier_call(struct notifier_block *nb,
				       unsigned long event, void *data)
{
	enum charger_type charger_type;
	struct rk81x_battery *di =
	    container_of(nb, struct rk81x_battery, battery_nb);

	if (di->fg_drv_mode == TEST_POWER_MODE)
		return NOTIFY_OK;

	/*if dc is pluging, ignore usb*/
	charger_type = rk81x_bat_get_dc_state(di);
	if ((charger_type == DC_CHARGER) &&
	    (event != USB_OTG_POWER_OFF) &&
	    (event != USB_OTG_POWER_ON))
		return NOTIFY_OK;

	switch (event) {
	case USB_BC_TYPE_DISCNT:/*maybe dc still plugin*/
		queue_delayed_work(di->wq, &di->dc_det_check_work,
				   msecs_to_jiffies(10));
		break;
	case USB_BC_TYPE_SDP:
	case USB_BC_TYPE_CDP:/*nonstandard charger*/
	case USB_BC_TYPE_DCP:/*standard charger*/
		queue_delayed_work(di->wq, &di->ac_usb_check_work,
				   msecs_to_jiffies(10));
		break;
	case USB_OTG_POWER_ON:/*otg on*/
		di->charge_otg	= USB_OTG_POWER_ON;
		queue_delayed_work(di->wq, &di->otg_check_work,
				   msecs_to_jiffies(10));
		break;
	case USB_OTG_POWER_OFF:/*otg off*/
		di->charge_otg = USB_OTG_POWER_OFF;
		queue_delayed_work(di->wq, &di->otg_check_work,
				   msecs_to_jiffies(10));
		break;
	default:
		return NOTIFY_OK;
	}
	return NOTIFY_OK;
}
#endif
static irqreturn_t rk81x_vbat_lo_irq(int irq, void *bat)
{
	pr_info("\n------- %s:lower power warning!\n", __func__);

	rk_send_wakeup_key();
	kernel_power_off();
	return IRQ_HANDLED;
}

static irqreturn_t rk81x_vbat_plug_in(int irq, void *bat)
{
	pr_info("\n------- %s:irq = %d\n", __func__, irq);
	rk_send_wakeup_key();
	return IRQ_HANDLED;
}

static irqreturn_t rk81x_vbat_plug_out(int irq, void  *bat)
{
	pr_info("\n-------- %s:irq = %d\n", __func__, irq);
	rk_send_wakeup_key();
	return IRQ_HANDLED;
}

static irqreturn_t rk81x_vbat_charge_ok(int irq, void  *bat)
{
	struct rk81x_battery *di = (struct rk81x_battery *)bat;

	pr_info("\n---------- %s:irq = %d\n", __func__, irq);
	di->finish_sig_base = get_runtime_sec();
	rk_send_wakeup_key();
	return IRQ_HANDLED;
}

static irqreturn_t rk81x_vbat_dc_det(int irq, void *bat)
{
	struct rk81x_battery *di = (struct rk81x_battery *)bat;

	queue_delayed_work(di->wq,
			   &di->dc_det_check_work,
			   msecs_to_jiffies(10));
	rk_send_wakeup_key();

	return IRQ_HANDLED;
}

static int rk81x_bat_sysfs_init(struct rk81x_battery *di)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(rk818_bat_attr); i++) {
		ret = sysfs_create_file(&di->bat.dev->kobj,
					&rk818_bat_attr[i].attr);
		if (ret != 0)
			dev_err(di->dev, "create battery node(%s) error\n",
				rk818_bat_attr[i].attr.name);
	}

	return ret;
}

static void rk81x_bat_irq_init(struct rk81x_battery *di)
{
	int plug_in_irq, plug_out_irq, chrg_ok_irq, vb_lo_irq;
	int ret;
	struct rk818 *chip = di->rk818;

#if defined(CONFIG_X86_INTEL_SOFIA)
	vb_lo_irq = chip->irq_base + RK818_IRQ_VB_LO;
	chrg_ok_irq = chip->irq_base + RK818_IRQ_CHG_OK;
	plug_in_irq = chip->irq_base + RK818_IRQ_PLUG_IN;
	plug_out_irq = chip->irq_base + RK818_IRQ_PLUG_OUT;
#else
	vb_lo_irq = irq_create_mapping(chip->irq_domain, RK818_IRQ_VB_LO);
	plug_in_irq = irq_create_mapping(chip->irq_domain, RK818_IRQ_PLUG_IN);
	plug_out_irq = irq_create_mapping(chip->irq_domain, RK818_IRQ_PLUG_OUT);
	chrg_ok_irq = irq_create_mapping(chip->irq_domain, RK818_IRQ_CHG_OK);
#endif

	ret = request_threaded_irq(vb_lo_irq, NULL, rk81x_vbat_lo_irq,
				   IRQF_TRIGGER_HIGH, "rk818_vbatlow", di);
	if (ret != 0)
		dev_err(chip->dev, "vb_lo_irq request failed!\n");

	di->irq = vb_lo_irq;
	enable_irq_wake(di->irq);

	ret = request_threaded_irq(plug_in_irq, NULL, rk81x_vbat_plug_in,
				   IRQF_TRIGGER_RISING, "rk81x_vbat_plug_in",
				   di);
	if (ret != 0)
		dev_err(chip->dev, "plug_in_irq request failed!\n");

	ret = request_threaded_irq(plug_out_irq, NULL, rk81x_vbat_plug_out,
				   IRQF_TRIGGER_FALLING, "rk81x_vbat_plug_out",
				   di);
	if (ret != 0)
		dev_err(chip->dev, "plug_out_irq request failed!\n");

	ret = request_threaded_irq(chrg_ok_irq, NULL, rk81x_vbat_charge_ok,
				   IRQF_TRIGGER_RISING, "rk81x_vbat_charge_ok",
				   di);
	if (ret != 0)
		dev_err(chip->dev, "chrg_ok_irq request failed!\n");
}

static void rk81x_bat_info_init(struct rk81x_battery *di,
				struct rk818 *chip)
{
	u8 val;
	unsigned long time_base = POWER_ON_SEC_BASE;

	rk81x_bat_read(di, RK818_VB_MON_REG, &val, 1);
	if (val & PLUG_IN_STS)
		rk81x_bat_set_power_supply_state(di, USB_CHARGER);

	di->cell.config = di->pdata->cell_cfg;
	di->design_capacity = di->pdata->cell_cfg->design_capacity;
	di->qmax = di->pdata->cell_cfg->design_qmax;
	di->early_resume = 1;
	di->psy_status = POWER_SUPPLY_STATUS_DISCHARGING;
	di->bat_res = di->pdata->sense_resistor_mohm;
	di->dischrg_algorithm_mode = DISCHRG_NORMAL_MODE;
	di->last_zero_mode_dsoc = DEF_LAST_ZERO_MODE_SOC;
	di->slp_chrg_status = rk81x_bat_get_chrg_status(di);
	di->loader_charged = loader_charged;
	di->chrg_finish_base = time_base;
	di->power_on_base = time_base;
	di->plug_in_base = time_base;
	di->plug_out_base = time_base;
	di->finish_sig_base = time_base;
	di->fcc = rk81x_bat_get_fcc(di);
}

static void rk81x_bat_dc_det_init(struct rk81x_battery *di,
				  struct device_node *np)
{
	struct device *dev = di->dev;
	enum of_gpio_flags flags;
	int ret;

	di->dc_det_pin = of_get_named_gpio_flags(np, "dc_det_gpio", 0, &flags);
	if (di->dc_det_pin == -EPROBE_DEFER) {
		dev_err(dev, "dc_det_gpio error\n");
		return;
	}

	if (gpio_is_valid(di->dc_det_pin)) {
		di->dc_det_level = (flags & OF_GPIO_ACTIVE_LOW) ?
						RK818_DC_IN : RK818_DC_OUT;
		di->dc_det_irq = gpio_to_irq(di->dc_det_pin);

		ret = request_irq(di->dc_det_irq, rk81x_vbat_dc_det,
				  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				  "rk81x_dc_det", di);
		if (ret != 0) {
			dev_err(di->dev, "rk818_dc_det_irq request failed!\n");
			goto err;
		}
		enable_irq_wake(di->dc_det_irq);
	}

	return;
err:
	gpio_free(di->dc_det_pin);
}

static int rk81x_bat_get_suspend_sec(struct rk81x_battery *di)
{
	int err;
	int delta_sec = 0;
	struct rtc_time tm;
	struct timespec tv = {
		.tv_nsec = NSEC_PER_SEC >> 1,
	};
	struct rtc_device *rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);

	err = rtc_read_time(rtc, &tm);
	if (err) {
		dev_err(rtc->dev.parent,
			"hctosys: unable to read the hardware clock\n");
		goto out;
	}
	err = rtc_valid_tm(&tm);
	if (err) {
		dev_err(rtc->dev.parent,
			"hctosys: invalid date/time\n");
		goto out;
	}

	rtc_tm_to_time(&tm, &tv.tv_sec);
	delta_sec = tv.tv_sec - di->suspend_rtc_base.tv_sec;
out:
	return (delta_sec > 0) ? delta_sec : 0;
}

#ifdef CONFIG_OF
static int rk81x_bat_parse_dt(struct rk81x_battery *di)
{
	struct device_node *np;
	struct battery_platform_data *pdata;
	struct cell_config *cell_cfg;
	struct ocv_config *ocv_cfg;
	struct property *prop;
	struct rk818 *rk818 = di->rk818;
	struct device *dev = di->dev;
	u32 out_value;
	int length, ret;
	size_t size;

	np = of_find_node_by_name(rk818->dev->of_node, "battery");
	if (!np) {
		dev_err(dev, "battery node not found!\n");
		return -EINVAL;
	}

	pdata = devm_kzalloc(rk818->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	cell_cfg = devm_kzalloc(rk818->dev, sizeof(*cell_cfg), GFP_KERNEL);
	if (!cell_cfg)
		return -ENOMEM;

	ocv_cfg = devm_kzalloc(rk818->dev, sizeof(*ocv_cfg), GFP_KERNEL);
	if (!ocv_cfg)
		return -ENOMEM;

	prop = of_find_property(np, "ocv_table", &length);
	if (!prop) {
		dev_err(dev, "ocv_table not found!\n");
		return -EINVAL;
	}
	pdata->ocv_size = length / sizeof(u32);
	if (pdata->ocv_size <= 0) {
		dev_err(dev, "invalid ocv table\n");
		return -EINVAL;
	}

	size = sizeof(*pdata->battery_ocv) * pdata->ocv_size;

	pdata->battery_ocv = devm_kzalloc(rk818->dev, size, GFP_KERNEL);
	if (!pdata->battery_ocv)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "ocv_table", pdata->battery_ocv,
					 pdata->ocv_size);
	if (ret < 0)
		return ret;

	/******************** charger param  ****************************/
	ret = of_property_read_u32(np, "max_chrg_currentmA", &out_value);
	if (ret < 0) {
		dev_err(dev, "max_chrg_currentmA not found!\n");
		out_value = DEFAULT_CHRG_CUR;
	}
	pdata->max_charger_currentmA = out_value;

	ret = of_property_read_u32(np, "max_input_currentmA", &out_value);
	if (ret < 0) {
		dev_err(dev, "max_charger_ilimitmA not found!\n");
		out_value = DEFAULT_INPUT_CUR;
	}
	pdata->max_charger_ilimitmA = out_value;

	ret = of_property_read_u32(np, "bat_res", &out_value);
	if (ret < 0) {
		dev_err(dev, "bat_res not found!\n");
		out_value = DEFAULT_BAT_RES;
	}
	pdata->sense_resistor_mohm = out_value;

	ret = of_property_read_u32(np, "max_charge_voltagemV", &out_value);
	if (ret < 0) {
		dev_err(dev, "max_charge_voltagemV not found!\n");
		out_value = DEFAULT_CHRG_VOL;
	}
	pdata->max_charger_voltagemV = out_value;

	ret = of_property_read_u32(np, "design_capacity", &out_value);
	if (ret < 0) {
		dev_err(dev, "design_capacity not found!\n");
		return ret;
	}
	cell_cfg->design_capacity  = out_value;

	ret = of_property_read_u32(np, "design_qmax", &out_value);
	if (ret < 0) {
		dev_err(dev, "design_qmax not found!\n");
		return ret;
	}
	cell_cfg->design_qmax = out_value;

	ret = of_property_read_u32(np, "sleep_enter_current", &out_value);
	if (ret < 0) {
		dev_err(dev, "sleep_enter_current not found!\n");
		out_value = DEFAULT_SLP_ENTER_CUR;
	}
	ocv_cfg->sleep_enter_current = out_value;

	ret = of_property_read_u32(np, "sleep_exit_current", &out_value);
	if (ret < 0) {
		dev_err(dev, "sleep_exit_current not found!\n");
		out_value = DEFAULT_SLP_EXIT_CUR;
	}
	ocv_cfg->sleep_exit_current = out_value;

	ret = of_property_read_u32(np, "power_off_thresd", &out_value);
	if (ret < 0) {
		dev_warn(dev, "power_off_thresd not found!\n");
		out_value = PWR_OFF_THRESD;
	}
	pdata->power_off_thresd = out_value;

	of_property_read_u32(np, "chrg_diff_voltagemV", &pdata->chrg_diff_vol);
	of_property_read_u32(np, "virtual_power", &di->fg_drv_mode);
	di->fg_drv_mode = di->fg_drv_mode ? TEST_POWER_MODE : FG_NORMAL_MODE;

	/*************  charger support adp types **********************/
	ret = of_property_read_u32(np, "support_usb_adp", &support_usb_adp);
	ret = of_property_read_u32(np, "support_dc_adp", &support_dc_adp);
	ret = of_property_read_u32(np, "power_dc2otg", &power_dc2otg);

	if (!support_usb_adp && !support_dc_adp) {
		dev_err(dev, "miss both: usb_adp and dc_adp,default:usb_adp!\n");
		support_usb_adp = 1;
	}

	/*if (support_dc_adp)*/
	rk81x_bat_dc_det_init(di, np);

	cell_cfg->ocv = ocv_cfg;
	pdata->cell_cfg = cell_cfg;
	di->pdata = pdata;

	DBG("\nthe battery dts info dump:\n"
	    "bat_res:%d\n"
	    "max_input_currentmA:%d\n"
	    "max_chrg_currentmA:%d\n"
	    "max_charge_voltagemV:%d\n"
	    "design_capacity:%d\n"
	    "design_qmax :%d\n"
	    "sleep_enter_current:%d\n"
	    "sleep_exit_current:%d\n"
	    "support_usb_adp:%d\n"
	    "support_dc_adp:%d\n"
	    "power_off_thresd:%d\n",
	    pdata->sense_resistor_mohm, pdata->max_charger_ilimitmA,
	    pdata->max_charger_currentmA, pdata->max_charger_voltagemV,
	    cell_cfg->design_capacity, cell_cfg->design_qmax,
	    cell_cfg->ocv->sleep_enter_current,
	    cell_cfg->ocv->sleep_exit_current,
	    support_usb_adp, support_dc_adp, pdata->power_off_thresd);

	return 0;
}

#else
static int rk81x_bat_parse_dt(struct rk81x_battery *di)
{
	return -ENODEV;
}
#endif

static int rk81x_battery_probe(struct platform_device *pdev)
{
	struct rk818 *chip = dev_get_drvdata(pdev->dev.parent);
	struct rk81x_battery *di;
	int ret;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;
	di->rk818 = chip;
	di->dev = &pdev->dev;
	platform_set_drvdata(pdev, di);

	ret = rk81x_bat_parse_dt(di);
	if (ret < 0) {
		dev_err(&pdev->dev, "rk81x battery parse dt failed!\n");
		return ret;
	}

	rk81x_bat_info_init(di, chip);
	if (!is_rk81x_bat_exist(di)) {
		dev_info(di->dev, "not battery, enter test power mode\n");
		di->fg_drv_mode = TEST_POWER_MODE;
	}

	ret = rk81x_bat_power_supply_init(di);
	if (ret) {
		dev_err(&pdev->dev, "rk81x power supply register failed!\n");
		return ret;
	}

	rk81x_bat_irq_init(di);
	rk81x_bat_sysfs_init(di);

	rk81x_bat_fg_init(di);
	wake_lock_init(&di->resume_wake_lock, WAKE_LOCK_SUSPEND,
		       "resume_charging");
	rk81x_bat_flatzone_vol_init(di);

#if defined(CONFIG_X86_INTEL_SOFIA)
	di->usb_phy = usb_get_phy(USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(di->usb_phy)) {
		dev_err(di->dev, "get usb phy failed\n");
		return PTR_ERR(di->usb_phy);
	}
	di->usb_nb.notifier_call = rk81x_battery_usb_notifier;
	ret = usb_register_notifier(di->usb_phy, &di->usb_nb);
	if (ret)
		dev_err(di->dev, "registr usb phy notification failed\n");
	INIT_DELAYED_WORK(&di->usb_phy_delay_work,
			  rk81x_battery_usb_notifier_delayed_work);
#endif

	rk81x_battery_register_fb_notify(di);
	di->wq = alloc_ordered_workqueue("%s", WQ_MEM_RECLAIM | WQ_FREEZABLE,
					 "rk81x-battery-work");
	INIT_DELAYED_WORK(&di->battery_monitor_work, rk81x_battery_work);
	INIT_DELAYED_WORK(&di->chrg_term_mode_switch_work,
			  rk81x_chrg_term_mode_switch_work);

	queue_delayed_work(di->wq, &di->battery_monitor_work,
			   msecs_to_jiffies(TIMER_MS_COUNTS * 5));

#if defined(CONFIG_ARCH_ROCKCHIP)
	INIT_DELAYED_WORK(&di->otg_check_work,
			  rk81x_battery_otg_delay_work);
	INIT_DELAYED_WORK(&di->ac_usb_check_work,
			  rk81x_battery_acusb_delay_work);
	INIT_DELAYED_WORK(&di->dc_det_check_work,
			  rk81x_battery_dc_delay_work);
	/*power on check*/
	queue_delayed_work(di->wq, &di->dc_det_check_work,
			   msecs_to_jiffies(TIMER_MS_COUNTS * 5));

	di->battery_nb.notifier_call = rk81x_bat_usb_notifier_call;
	rk_bc_detect_notifier_register(&di->battery_nb, &di->charge_otg);
#endif
	dev_info(di->dev, "battery driver version %s\n", DRIVER_VERSION);

	return ret;
}

static int rk81x_battery_suspend(struct platform_device *dev,
				 pm_message_t state)
{
	struct rk81x_battery *di = platform_get_drvdata(dev);

	/*while otg and dc both plugin*/
	rk81x_bat_set_bit(di, NT_STS_MSK_REG2, CHRG_CVTLMT_INT);

	di->slp_psy_status = rk81x_chrg_online(di);
	di->chrg_status = rk81x_bat_get_chrg_status(di);
	di->slp_chrg_status = rk81x_bat_get_chrg_status(di);
	di->suspend_charge_current = rk81x_bat_get_avg_current(di);
	di->dischrg_save_sec += rk81x_bat_save_dischrg_sec(di);
	di->dischrg_normal_base = 0;
	di->dischrg_emu_base = 0;
	do_gettimeofday(&di->suspend_rtc_base);

	if (!rk81x_chrg_online(di)) {
		di->chrg_save_sec += rk81x_bat_save_chrg_sec(di);
		di->chrg_normal_base = 0;
		di->chrg_emu_base = 0;
		di->chrg_term_base = 0;
		di->chrg_finish_base = 0;
	}

	di->s2r = 0;

	pr_info("battery suspend dl=%d rl=%d c=%d v=%d at=%ld st=0x%x chg=%d\n",
		di->dsoc, di->rsoc, di->suspend_charge_current, di->voltage,
		di->suspend_time_sum, di->chrg_status, di->slp_psy_status);

	return 0;
}

static int rk81x_battery_resume(struct platform_device *dev)
{
	struct rk81x_battery *di = platform_get_drvdata(dev);
	int pwroff_thresd = di->pdata->power_off_thresd;
	int delta_time;
	int time_step;
	int delta_soc;
	int vol;

	/*while otg and dc both plugin*/
	rk81x_bat_clr_bit(di, NT_STS_MSK_REG2, CHRG_CVTLMT_INT);

	di->discharge_smooth_status = true;
	di->charge_smooth_status = true;
	di->s2r = 1;
	vol  = rk81x_bat_get_vol(di);
	if (vol < INVALID_VOL_THRESD) {
		dev_err(di->dev, "invalid voltage :%d", vol);
		vol = di->voltage;
		dbg_enable = 1;
	}
	di->voltage = vol;
	di->current_avg = rk81x_bat_get_avg_current(di);
	di->relax_voltage = rk81x_bat_get_relax_vol(di);
	di->est_ocv_vol = rk81x_bat_est_ocv_vol(di);
	di->est_ocv_soc = rk81x_bat_est_ocv_soc(di);
	delta_time = rk81x_bat_get_suspend_sec(di);
	di->suspend_time_sum += delta_time;
#if defined(CONFIG_ARCH_ROCKCHIP)
	di->remain_capacity = rk81x_bat_get_realtime_capacity(di);
#endif

	if (di->slp_psy_status) {
		time_step = CHRG_TIME_STEP;
	} else {
		if (di->voltage <= pwroff_thresd + 50)
			time_step = DISCHRG_TIME_STEP_0;
		else
			time_step = DISCHRG_TIME_STEP_1;
	}

	pr_info("battery resume c=%d v=%d ev=%d rv=%d dt=%d at=%ld chg=%d\n",
		di->current_avg, di->voltage, di->est_ocv_vol,
		di->relax_voltage, delta_time, di->suspend_time_sum,
		di->slp_psy_status);

	if (di->suspend_time_sum > time_step) {
		delta_soc = rk81x_bat_update_resume_state(di);
		if (delta_soc)
			di->suspend_time_sum = 0;
	}

	if ((!rk81x_chrg_online(di) && di->voltage <= pwroff_thresd) ||
	    rk81x_chrg_online(di))
		wake_lock_timeout(&di->resume_wake_lock, 5 * HZ);
	return 0;
}

static int rk81x_battery_remove(struct platform_device *dev)
{
	struct rk81x_battery *di = platform_get_drvdata(dev);

	cancel_delayed_work_sync(&di->battery_monitor_work);
	return 0;
}

static void rk81x_battery_shutdown(struct platform_device *dev)
{
	struct rk81x_battery *di = platform_get_drvdata(dev);

	cancel_delayed_work_sync(&di->battery_monitor_work);
	rk_bc_detect_notifier_unregister(&di->battery_nb);

	if (BASE_TO_MIN(di->power_on_base) <= REBOOT_INTER_MIN)
		rk81x_bat_check_reboot(di);
	else
		rk81x_bat_save_reboot_cnt(di, 0);
	rk81x_chrg_term_mode_set(di, CHRG_TERM_ANA_SIGNAL);
}

static struct platform_driver rk81x_battery_driver = {
	.driver     = {
		.name   = "rk818-battery",
		.owner  = THIS_MODULE,
	},

	.probe      = rk81x_battery_probe,
	.remove     = rk81x_battery_remove,
	.suspend    = rk81x_battery_suspend,
	.resume     = rk81x_battery_resume,
	.shutdown   = rk81x_battery_shutdown,
};

static int __init battery_init(void)
{
	return platform_driver_register(&rk81x_battery_driver);
}

fs_initcall_sync(battery_init);
static void __exit battery_exit(void)
{
	platform_driver_unregister(&rk81x_battery_driver);
}
module_exit(battery_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk818-battery");
MODULE_AUTHOR("ROCKCHIP");
