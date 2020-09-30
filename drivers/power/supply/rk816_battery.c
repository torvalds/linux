/*
 * rk816 battery driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd
 * Author: chenjh <chenjh@rock-chips.com>
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

#include <linux/delay.h>
#include <linux/extcon.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/mfd/rk808.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/rk_usbbc.h>
#include <linux/regmap.h>
#include <linux/rk_keys.h>
#include <linux/rtc.h>
#include <linux/timer.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include "rk816_battery.h"

static int dbg_enable = 0;
module_param_named(dbg_level, dbg_enable, int, 0644);

#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

#define BAT_INFO(fmt, args...) pr_info("rk816-bat: "fmt, ##args)

/* default param */
#define DEFAULT_BAT_RES			135
#define DEFAULT_SLP_ENTER_CUR		300
#define DEFAULT_SLP_EXIT_CUR		300
#define DEFAULT_SLP_FILTER_CUR		100
#define DEFAULT_PWROFF_VOL_THRESD	3400
#define DEFAULT_MONITOR_SEC		5
#define DEFAULT_ALGR_VOL_THRESD1	3850
#define DEFAULT_ALGR_VOL_THRESD2	3950
#define DEFAULT_CHRG_VOL_SEL		CHRG_VOL4200MV
#define DEFAULT_CHRG_CUR_SEL		CHRG_CUR1400MA
#define DEFAULT_CHRG_CUR_INPUT		INPUT_CUR2000MA
#define DEFAULT_POFFSET			42
#define DEFAULT_MAX_SOC_OFFSET		60
#define DEFAULT_FB_TEMP			TEMP_115C
#define DEFAULT_ENERGY_MODE		0
#define DEFAULT_ZERO_RESERVE_DSOC	10
#define DEFAULT_SAMPLE_RES		20

/*MODE_VIRTUAL params*/
#define VIRTUAL_CURRENT			1000
#define VIRTUAL_VOLTAGE			3888
#define VIRTUAL_SOC			66
#define VIRTUAL_STATUS			POWER_SUPPLY_STATUS_CHARGING
#define VIRTUAL_PRESET			1
#define VIRTUAL_AC_ONLINE		1
#define VIRTUAL_USB_ONLINE		0
#define VIRTUAL_TEMPERATURE		188

/* dsoc calib param */
#define FINISH_CHRG_CUR1		1000
#define FINISH_CHRG_CUR2		1500
#define FINISH_MAX_SOC_DELAY		20
#define TERM_CHRG_DSOC			88
#define TERM_CHRG_CURR			600
#define TERM_CHRG_K			650

#define SIMULATE_CHRG_INTV		8
#define SIMULATE_CHRG_CURR		400
#define SIMULATE_CHRG_K			1500

#define FULL_CHRG_K			400

/* zero algorithm */
#define PWROFF_THRESD			3400
#define MIN_ZERO_DSOC_ACCURACY		10	/*0.01%*/
#define MIN_ZERO_OVERCNT		100
#define MIN_ACCURACY			1
#define DEF_PWRPATH_RES			50
#define	WAIT_DSOC_DROP_SEC		15
#define	WAIT_SHTD_DROP_SEC		30
#define MIN_ZERO_GAP_XSOC1		10
#define MIN_ZERO_GAP_XSOC2		5
#define MIN_ZERO_GAP_XSOC3		3
#define MIN_ZERO_GAP_CALIB		5

#define ADC_CALIB_THRESHOLD		4
#define ADC_CALIB_LMT_MIN		3
#define ADC_CALIB_CNT			5

/* TS detect battery temperature */
#define ADC_CUR_MSK			0x03
#define ADC_CUR_20UA			0x00
#define ADC_CUR_40UA			0x01
#define ADC_CUR_60UA			0x02
#define ADC_CUR_80UA			0x03

#define NTC_CALC_FACTOR_80UA		80
#define NTC_CALC_FACTOR_60UA		60
#define NTC_CALC_FACTOR_40UA		40
#define NTC_CALC_FACTOR_20UA		20
#define NTC_80UA_MAX_MEASURE		27500
#define NTC_60UA_MAX_MEASURE		36666
#define NTC_40UA_MAX_MEASURE		55000
#define NTC_20UA_MAX_MEASURE		110000

/* time */
#define	POWER_ON_SEC_BASE		1
#define MINUTE(x)				((x) * 60)

/* sleep */
#define SLP_CURR_MAX			40
#define SLP_CURR_MIN			6
#define DISCHRG_TIME_STEP1		MINUTE(10)
#define DISCHRG_TIME_STEP2		MINUTE(60)
#define SLP_DSOC_VOL_THRESD		3600
#define REBOOT_PERIOD_SEC		180
#define REBOOT_MAX_CNT			80

#define ZERO_LOAD_LVL1			1400
#define ZERO_LOAD_LVL2			600

/* fcc */
#define MIN_FCC				500

/* DC ADC */
#define DC_ADC_TRIGGER			150

#define TEMP_RECORD_NUM			30

static const char *bat_status[] = {
	"charge off", "dead charge", "trickle charge", "cc cv",
	"finish", "usb over vol", "bat temp error", "timer error",
};

struct rk816_battery {
	struct platform_device		*pdev;
	struct rk808			*rk816;
	struct regmap			*regmap;
	struct device			*dev;
	struct power_supply		*bat;
	struct power_supply		*usb;
	struct power_supply		*ac;
	struct battery_platform_data	*pdata;
	struct workqueue_struct		*bat_monitor_wq;
	struct workqueue_struct		*usb_charger_wq;
	struct delayed_work		bat_delay_work;
	struct delayed_work		dc_delay_work;
	struct delayed_work		calib_delay_work;
	struct wake_lock		wake_lock;
	struct notifier_block           fb_nb;
	struct timer_list		caltimer;
	time_t				rtc_base;
	struct iio_channel		*iio_chan;
	struct notifier_block		cable_cg_nb;
	struct notifier_block		cable_host_nb;
	struct notifier_block		cable_discnt_nb;
	struct delayed_work		usb_work;
	struct delayed_work		host_work;
	struct delayed_work		discnt_work;
	struct extcon_dev		*cable_edev;
	int				charger_changed;
	int				bat_res;
	int				chrg_status;
	int				res_fac;
	int				over_20mR;
	bool				is_initialized;
	bool				bat_first_power_on;
	u8				ac_in;
	u8				usb_in;
	u8				otg_in;		/* OTG device attached status */
	u8				otg_pmic5v;	/* OTG device power supply from PMIC */
	u8				dc_in;
	u8				prop_status;
	int				cvtlmt_irq;
	int				current_avg;
	int				current_relax;
	int				voltage_avg;
	int				voltage_ocv;
	int				voltage_relax;
	int				voltage_k;/* VCALIB0 VCALIB1 */
	int				voltage_b;
	int				remain_cap;
	int				design_cap;
	int				nac;
	int				fcc;
	int				lock_fcc;
	int				qmax;
	int				dsoc;
	int				rsoc;
	int				poffset;
	int				fake_offline;
	int				age_ocv_soc;
	bool				age_allow_update;
	int				age_level;
	int				age_ocv_cap;
	int				age_voltage;
	int				age_adjust_cap;
	unsigned long			age_keep_sec;
	int				zero_timeout_cnt;
	int				zero_remain_cap;
	int				zero_dsoc;
	int				zero_linek;
	u64				zero_drop_sec;
	u64				shtd_drop_sec;
	int				sm_remain_cap;
	int				sm_linek;
	int				sm_chrg_dsoc;
	int				sm_dischrg_dsoc;
	int				algo_rest_val;
	int				algo_rest_mode;
	int				sleep_sum_cap;
	int				sleep_remain_cap;
	unsigned long			sleep_dischrg_sec;
	unsigned long			sleep_sum_sec;
	bool				sleep_chrg_online;
	u8				sleep_chrg_status;
	bool				adc_allow_update;
	int                             fb_blank;
	bool				s2r; /*suspend to resume*/
	u32				work_mode;
	int				temperature;
	int				chrg_cur_lp_input;
	int				chrg_vol_sel;
	int				chrg_cur_input;
	int				chrg_cur_sel;
	u32				monitor_ms;
	u32				pwroff_min;
	u32				adc_calib_cnt;
	unsigned long			chrg_finish_base;
	unsigned long			boot_base;
	unsigned long			flat_match_sec;
	unsigned long			plug_in_base;
	unsigned long			plug_out_base;
	u8				halt_cnt;
	bool				is_halt;
	bool				is_max_soc_offset;
	bool				is_sw_reset;
	bool				is_ocv_calib;
	bool				is_first_on;
	bool				is_force_calib;
	int				last_dsoc;
	u8				cvtlmt_int_event;
	u8				slp_dcdc_en_reg;
	int				ocv_pre_dsoc;
	int				ocv_new_dsoc;
	int				max_pre_dsoc;
	int				max_new_dsoc;
	int				force_pre_dsoc;
	int				force_new_dsoc;
	int				dbg_cap_low0;
	int				dbg_pwr_dsoc;
	int				dbg_pwr_rsoc;
	int				dbg_pwr_vol;
	int				dbg_chrg_min[10];
	int				dbg_meet_soc;
	int				dbg_calc_dsoc;
	int				dbg_calc_rsoc;
	bool				is_charging;
	unsigned long			charge_count;
	int				current_max;
	int				voltage_max;
};

struct led_ops {
	void (*led_init)(struct rk816_battery *di);
	void (*led_charging)(struct rk816_battery *di);
	void (*led_discharging)(struct rk816_battery *di);
	void (*led_charging_full)(struct rk816_battery *di);
};

static struct led_ops *rk816_led_ops;

#define DIV(x)	((x) ? (x) : 1)

/* 'res_fac' has been *10, so we need divide 10 */
#define RES_FAC_MUX(value, res_fac)	((value) * res_fac / 10)

/* 'res_fac' has been *10, so we need 'value * 10' before divide 'res_fac' */
#define RES_FAC_DIV(value, res_fac)	((value) * 10 / res_fac)

static u64 get_boot_sec(void)
{
	struct timespec ts;

	get_monotonic_boottime(&ts);

	return ts.tv_sec;
}

static unsigned long base2sec(unsigned long x)
{
	if (x)
		return (get_boot_sec() > x) ? (get_boot_sec() - x) : 0;
	else
		return 0;
}

static unsigned long base2min(unsigned long x)
{
	return base2sec(x) / 60;
}

static u32 interpolate(int value, u32 *table, int size)
{
	u8 i;
	u16 d;

	for (i = 0; i < size; i++) {
		if (value < table[i])
			break;
	}

	if ((i > 0) && (i < size)) {
		d = (value - table[i - 1]) * (MAX_INTERPOLATE / (size - 1));
		d /= table[i] - table[i - 1];
		d = d + (i - 1) * (MAX_INTERPOLATE / (size - 1));
	} else {
		d = i * ((MAX_INTERPOLATE + size / 2) / size);
	}

	if (d > 1000)
		d = 1000;

	return d;
}

/* (a*b)/c */
static int32_t ab_div_c(u32 a, u32 b, u32 c)
{
	bool sign;
	u32 ans = MAX_INT;
	int32_t tmp;

	sign = ((((a ^ b) ^ c) & 0x80000000) != 0);
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

static int rk816_bat_read(struct rk816_battery *di, u8 reg)
{
	int ret, val;

	ret = regmap_read(di->regmap, reg, &val);
	if (ret)
		dev_err(di->dev, "read reg:0x%x failed\n", reg);

	return val;
}

static int rk816_bat_write(struct rk816_battery *di, u8 reg, u8 buf)
{
	int ret;

	ret = regmap_write(di->regmap, reg, buf);
	if (ret)
		dev_err(di->dev, "i2c write reg: 0x%2x error\n", reg);

	return ret;
}

static int rk816_bat_set_bits(struct rk816_battery *di, u8 reg, u8 mask, u8 buf)
{
	int ret;

	ret = regmap_update_bits(di->regmap, reg, mask, buf);
	if (ret)
		dev_err(di->dev, "write reg:0x%x failed\n", reg);

	return ret;
}

static int rk816_bat_clear_bits(struct rk816_battery *di, u8 reg, u8 mask)
{
	int ret;

	ret = regmap_update_bits(di->regmap, reg, mask, 0);
	if (ret)
		dev_err(di->dev, "clr reg:0x%02x failed\n", reg);

	return ret;
}

static void rk816_bat_dump_regs(struct rk816_battery *di, u8 start, u8 end)
{
	int i;

	if (!dbg_enable)
		return;

	DBG("dump regs from: 0x%x-->0x%x\n", start, end);
	for (i = start; i < end; i++)
		DBG("0x%x: 0x%0x\n", i, rk816_bat_read(di, i));
}

static bool rk816_bat_chrg_online(struct rk816_battery *di)
{
	return (di->usb_in || di->ac_in || di->dc_in) ? true : false;
}

static int rk816_bat_get_coulomb_cap(struct rk816_battery *di)
{
	int cap, val = 0;

	val |= rk816_bat_read(di, RK816_GASCNT_REG3) << 24;
	val |= rk816_bat_read(di, RK816_GASCNT_REG2) << 16;
	val |= rk816_bat_read(di, RK816_GASCNT_REG1) << 8;
	val |= rk816_bat_read(di, RK816_GASCNT_REG0) << 0;

	if (!di->over_20mR)
		cap = RES_FAC_MUX(val / 2390, di->res_fac);
	else
		cap = RES_FAC_DIV(val / 2390, di->res_fac);

	return cap;
}

static int rk816_bat_get_rsoc(struct rk816_battery *di)
{
	int remain_cap;

	remain_cap = rk816_bat_get_coulomb_cap(di);
	return (remain_cap + di->fcc / 200) * 100 / DIV(di->fcc);
}

static ssize_t bat_info_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int ret;
	char cmd = 0;
	struct rk816_battery *di = dev_get_drvdata(dev);

	ret = sscanf(buf, "%c", &cmd);
	if (ret != 1) {
		dev_err(di->dev, "error! cmd require only one args\n");
		return count;
	}

	if (cmd == 'n')
		rk816_bat_set_bits(di, RK816_MISC_MARK_REG,
				   FG_RESET_NOW, FG_RESET_NOW);
	else if (cmd == 'm')
		rk816_bat_set_bits(di, RK816_MISC_MARK_REG,
				   FG_RESET_LATE, FG_RESET_LATE);
	else if (cmd == 'c')
		rk816_bat_clear_bits(di, RK816_MISC_MARK_REG,
				     FG_RESET_LATE | FG_RESET_NOW);
	else if (cmd == 'r')
		BAT_INFO("0x%2x\n", rk816_bat_read(di, RK816_MISC_MARK_REG));
	else
		BAT_INFO("command error\n");

	return count;
}

static struct device_attribute rk816_bat_attr[] = {
	__ATTR(bat, 0664, NULL, bat_info_store),
};

static void rk816_bat_enable_input_current(struct rk816_battery *di)
{
	u8 buf;

	buf = rk816_bat_read(di, RK816_BAT_CTRL_REG);
	buf |= USB_SYS_EN;
	rk816_bat_write(di, RK816_BAT_CTRL_REG, buf);
}

static void rk816_bat_disable_input_current(struct rk816_battery *di)
{
	u8 buf;

	buf = rk816_bat_read(di, RK816_BAT_CTRL_REG);
	buf &= ~USB_SYS_EN;
	rk816_bat_write(di, RK816_BAT_CTRL_REG, buf);
}

static int rk816_bat_is_input_enabled(struct rk816_battery *di)
{
	u8 buf;

	buf = rk816_bat_read(di, RK816_BAT_CTRL_REG);
	return !!(buf & USB_SYS_EN);
}

static void rk816_bat_enable_gauge(struct rk816_battery *di)
{
	u8 buf;

	buf = rk816_bat_read(di, RK816_TS_CTRL_REG);
	buf |= GG_EN;
	rk816_bat_write(di, RK816_TS_CTRL_REG, buf);
}

static void rk816_bat_save_age_level(struct rk816_battery *di, u8 level)
{
	rk816_bat_write(di, RK816_UPDATE_LEVE_REG, level);
}

static u8 rk816_bat_get_age_level(struct  rk816_battery *di)
{
	return rk816_bat_read(di, RK816_UPDATE_LEVE_REG);
}

static int rk816_bat_get_vcalib0(struct rk816_battery *di)
{
	int val = 0;

	val |= rk816_bat_read(di, RK816_VCALIB0_REGL) << 0;
	val |= rk816_bat_read(di, RK816_VCALIB0_REGH) << 8;

	DBG("<%s>. voffset0: 0x%x\n", __func__, val);
	return val;
}

static int rk816_bat_get_vcalib1(struct rk816_battery *di)
{
	int val = 0;

	val |= rk816_bat_read(di, RK816_VCALIB1_REGL) << 0;
	val |= rk816_bat_read(di, RK816_VCALIB1_REGH) << 8;

	DBG("<%s>. voffset1: 0x%x\n", __func__, val);
	return val;
}

static int rk816_bat_get_ioffset(struct rk816_battery *di)
{
	int val = 0;

	val |= rk816_bat_read(di, RK816_IOFFSET_REGL) << 0;
	val |= rk816_bat_read(di, RK816_IOFFSET_REGH) << 8;

	DBG("<%s>. ioffset: 0x%x\n", __func__, val);
	return val;
}

static int rk816_bat_get_coffset(struct rk816_battery *di)
{
	int val = 0;

	val |= rk816_bat_read(di, RK816_CAL_OFFSET_REGL) << 0;
	val |= rk816_bat_read(di, RK816_CAL_OFFSET_REGH) << 8;

	DBG("<%s>. coffset: 0x%x\n", __func__, val);
	return val;
}

static void rk816_bat_set_coffset(struct rk816_battery *di, int val)
{
	u8 buf;

	buf = (val >> 8) & 0xff;
	rk816_bat_write(di, RK816_CAL_OFFSET_REGH, buf);
	buf = (val >> 0) & 0xff;
	rk816_bat_write(di, RK816_CAL_OFFSET_REGL, buf);
	DBG("<%s>. coffset: 0x%x\n", __func__, val);
}

static void rk816_bat_init_voltage_kb(struct rk816_battery *di)
{
	int vcalib0, vcalib1;

	vcalib0 = rk816_bat_get_vcalib0(di);
	vcalib1 = rk816_bat_get_vcalib1(di);
	di->voltage_k = (4200 - 3000) * 1000 / DIV(vcalib1 - vcalib0);
	di->voltage_b = 4200 - (di->voltage_k * vcalib1) / 1000;

	DBG("voltage_k=%d(*1000),voltage_b=%d\n", di->voltage_k, di->voltage_b);
}

static int rk816_bat_get_ocv_voltage(struct rk816_battery *di)
{
	int vol, val = 0;

	val |= rk816_bat_read(di, RK816_BAT_OCV_REGL) << 0;
	val |= rk816_bat_read(di, RK816_BAT_OCV_REGH) << 8;
	vol = di->voltage_k * val / 1000 + di->voltage_b;

	return (vol * 1100 / 1000);
}

static int rk816_bat_get_avg_voltage(struct rk816_battery *di)
{
	int vol, val = 0;

	val |= rk816_bat_read(di, RK816_BAT_VOL_REGL) << 0;
	val |= rk816_bat_read(di, RK816_BAT_VOL_REGH) << 8;
	vol = di->voltage_k * val / 1000 + di->voltage_b;

	return (vol * 1100 / 1000);
}

static int rk816_bat_get_usb_voltage(struct rk816_battery *di)
{
	int vol, val = 0;

	val |= rk816_bat_read(di, RK816_USB_ADC_REGL) << 0;
	val |= rk816_bat_read(di, RK816_USB_ADC_REGH) << 8;
	vol = di->voltage_k * val / 1000 + di->voltage_b;

	return (vol * 1400 / 1100);
}

static bool is_rk816_bat_relax_mode(struct rk816_battery *di)
{
	u8 status;

	status = rk816_bat_read(di, RK816_GGSTS_REG);
	if (!(status & RELAX_VOL1_UPD) || !(status & RELAX_VOL2_UPD))
		return false;
	else
		return true;
}

static u16 rk816_bat_get_relax_vol1(struct rk816_battery *di)
{
	u16 vol, val = 0;

	val |= rk816_bat_read(di, RK816_RELAX_VOL1_REGL) << 0;
	val |= rk816_bat_read(di, RK816_RELAX_VOL1_REGH) << 8;
	vol = di->voltage_k * val / 1000 + di->voltage_b;

	return (vol * 1100 / 1000);
}

static u16 rk816_bat_get_relax_vol2(struct rk816_battery *di)
{
	u16 vol, val = 0;

	val |= rk816_bat_read(di, RK816_RELAX_VOL2_REGL) << 0;
	val |= rk816_bat_read(di, RK816_RELAX_VOL2_REGH) << 8;
	vol = di->voltage_k * val / 1000 + di->voltage_b;

	return (vol * 1100 / 1000);
}

static u16 rk816_bat_get_relax_voltage(struct rk816_battery *di)
{
	u16 relax_vol1, relax_vol2;

	if (!is_rk816_bat_relax_mode(di))
		return 0;

	relax_vol1 = rk816_bat_get_relax_vol1(di);
	relax_vol2 = rk816_bat_get_relax_vol2(di);

	return relax_vol1 > relax_vol2 ? relax_vol1 : relax_vol2;
}

static int rk816_bat_get_avg_current(struct rk816_battery *di)
{
	int cur, val = 0;

	val |= rk816_bat_read(di, RK816_BAT_CUR_AVG_REGL) << 0;
	val |= rk816_bat_read(di, RK816_BAT_CUR_AVG_REGH) << 8;
	if (val & 0x800)
		val -= 4096;

	if (!di->over_20mR)
		cur = RES_FAC_MUX(val * 1506, di->res_fac) / 1000;
	else
		cur = RES_FAC_DIV(val * 1506, di->res_fac) / 1000;

	return cur;
}

static int rk816_bat_get_relax_cur1(struct rk816_battery *di)
{
	int val = 0;

	val |= rk816_bat_read(di, RK816_RELAX_CUR1_REGL) << 0;
	val |= rk816_bat_read(di, RK816_RELAX_CUR1_REGH) << 8;
	if (val & 0x800)
		val -= 4096;

	return (val * 1506 / 1000);
}

static int rk816_bat_get_relax_cur2(struct rk816_battery *di)
{
	int val = 0;

	val |= rk816_bat_read(di, RK816_RELAX_CUR2_REGL) << 0;
	val |= rk816_bat_read(di, RK816_RELAX_CUR2_REGH) << 8;
	if (val & 0x800)
		val -= 4096;

	return (val * 1506 / 1000);
}

static int rk816_bat_get_relax_current(struct rk816_battery *di)
{
	int relax_cur1, relax_cur2;

	if (!is_rk816_bat_relax_mode(di))
		return 0;

	relax_cur1 = rk816_bat_get_relax_cur1(di);
	relax_cur2 = rk816_bat_get_relax_cur2(di);

	return (relax_cur1 < relax_cur2) ? relax_cur1 : relax_cur2;
}

static int rk816_bat_vol_to_ocvsoc(struct rk816_battery *di, int voltage)
{
	u32 *ocv_table, temp;
	int ocv_size, ocv_soc;

	ocv_table = di->pdata->ocv_table;
	ocv_size = di->pdata->ocv_size;
	temp = interpolate(voltage, ocv_table, ocv_size);
	ocv_soc = ab_div_c(temp, MAX_PERCENTAGE, MAX_INTERPOLATE);

	return ocv_soc;
}

static int rk816_bat_vol_to_ocvcap(struct rk816_battery *di, int voltage)
{
	u32 *ocv_table, temp;
	int ocv_size, cap;

	ocv_table = di->pdata->ocv_table;
	ocv_size = di->pdata->ocv_size;
	temp = interpolate(voltage, ocv_table, ocv_size);
	cap = ab_div_c(temp, di->fcc, MAX_INTERPOLATE);

	return cap;
}

static int rk816_bat_vol_to_zerosoc(struct rk816_battery *di, int voltage)
{
	u32 *ocv_table, temp;
	int ocv_size, ocv_soc;

	ocv_table = di->pdata->zero_table;
	ocv_size = di->pdata->ocv_size;
	temp = interpolate(voltage, ocv_table, ocv_size);
	ocv_soc = ab_div_c(temp, MAX_PERCENTAGE, MAX_INTERPOLATE);

	return ocv_soc;
}

static int rk816_bat_vol_to_zerocap(struct rk816_battery *di, int voltage)
{
	u32 *ocv_table, temp;
	int ocv_size, cap;

	ocv_table = di->pdata->zero_table;
	ocv_size = di->pdata->ocv_size;
	temp = interpolate(voltage, ocv_table, ocv_size);
	cap = ab_div_c(temp, di->fcc, MAX_INTERPOLATE);

	return cap;
}

static int rk816_bat_get_iadc(struct rk816_battery *di)
{
	int val = 0;

	val |= rk816_bat_read(di, RK816_BAT_CUR_AVG_REGL) << 0;
	val |= rk816_bat_read(di, RK816_BAT_CUR_AVG_REGH) << 8;
	if (val > 2047)
		val -= 4096;

	return val;
}

static bool is_rk816_bat_st_cvtlim(struct rk816_battery *di)
{
	return (rk816_bat_read(di, RK816_INT_STS_REG1) & 0x80) ? true : false;
}

static bool rk816_bat_adc_calib(struct rk816_battery *di)
{
	int i, ioffset, coffset, adc, save_coffset;

	if ((di->chrg_status != CHARGE_FINISH) ||
	    (di->adc_calib_cnt > ADC_CALIB_CNT) ||
	    (base2min(di->boot_base) < ADC_CALIB_LMT_MIN) ||
	    (abs(di->current_avg) < ADC_CALIB_THRESHOLD) ||
	    (is_rk816_bat_st_cvtlim(di)))
		return false;

	di->adc_calib_cnt++;
	save_coffset = rk816_bat_get_coffset(di);
	for (i = 0; i < 5; i++) {
		if (!rk816_bat_chrg_online(di)) {
			rk816_bat_set_coffset(di, save_coffset);
			BAT_INFO("quit, charger plugout when calib adc\n");
			return false;
		}

		/* check status and int cvtlmt */
		if (is_rk816_bat_st_cvtlim(di)) {
			rk816_bat_set_coffset(di, save_coffset);
			BAT_INFO("1 cvtlmt(st) when calib adc\n");
			return false;
		}
		enable_irq(di->cvtlmt_irq);
		msleep(2000);
		disable_irq(di->cvtlmt_irq);
		if (di->cvtlmt_int_event) {
			di->cvtlmt_int_event = 0;
			rk816_bat_set_coffset(di, save_coffset);
			BAT_INFO("1 cvtlmt(int) when calib adc\n");
			return false;
		}

		/* it's ok to update coffset */
		adc = rk816_bat_get_iadc(di);
		coffset = rk816_bat_get_coffset(di);
		rk816_bat_set_coffset(di, coffset + adc);

		/* check status and int cvtlmt again */
		if (is_rk816_bat_st_cvtlim(di)) {
			rk816_bat_set_coffset(di, save_coffset);
			BAT_INFO("2 cvtlmt(st) when calib adc\n");
			return false;
		}
		enable_irq(di->cvtlmt_irq);
		msleep(2000);
		disable_irq(di->cvtlmt_irq);
		if (di->cvtlmt_int_event) {
			di->cvtlmt_int_event = 0;
			rk816_bat_set_coffset(di, save_coffset);
			BAT_INFO("2 cvtlmt(int) when calib adc\n");
			return false;
		}

		/* it's ok to check calib adc result */
		adc = rk816_bat_get_iadc(di);
		if (abs(adc) < ADC_CALIB_THRESHOLD) {
			coffset = rk816_bat_get_coffset(di);
			ioffset = rk816_bat_get_ioffset(di);
			di->poffset = coffset - ioffset;
			rk816_bat_write(di, RK816_PCB_IOFFSET_REG, di->poffset);
			BAT_INFO("new offset:c=0x%x, i=0x%x, p=0x%x\n",
				 coffset, ioffset, di->poffset);
			return true;
		} else {
			BAT_INFO("coffset calib again %d.., max_cnt=%d\n",
				 i, di->adc_calib_cnt);
			rk816_bat_set_coffset(di, coffset);
		}
	}

	rk816_bat_set_coffset(di, save_coffset);

	return false;
}

static void rk816_bat_set_ioffset_sample(struct rk816_battery *di)
{
	u8 ggcon;

	ggcon = rk816_bat_read(di, RK816_GGCON_REG);
	ggcon &= ~ADC_CAL_MIN_MSK;
	ggcon |= ADC_CAL_8MIN;
	rk816_bat_write(di, RK816_GGCON_REG, ggcon);
}

static void rk816_bat_set_ocv_sample(struct rk816_battery *di)
{
	u8 ggcon;

	ggcon = rk816_bat_read(di, RK816_GGCON_REG);
	ggcon &= ~OCV_SAMP_MIN_MSK;
	ggcon |= OCV_SAMP_8MIN;
	rk816_bat_write(di, RK816_GGCON_REG, ggcon);
}

static void rk816_bat_restart_relax(struct rk816_battery *di)
{
	u8 ggsts;

	ggsts = rk816_bat_read(di, RK816_GGSTS_REG);
	ggsts &= ~RELAX_VOL12_UPD_MSK;
	rk816_bat_write(di, RK816_GGSTS_REG, ggsts);
}

static void rk816_bat_set_relax_sample(struct rk816_battery *di)
{
	u8 buf;
	int enter_thres, exit_thres, filter_thres;
	struct battery_platform_data *pdata = di->pdata;

	filter_thres = pdata->sleep_filter_current * 1000 / 1506;

	if (!di->over_20mR) {
		enter_thres = RES_FAC_DIV(pdata->sleep_enter_current * 1000,
					  di->res_fac) / 1506;
		exit_thres = RES_FAC_DIV(pdata->sleep_exit_current * 1000,
					 di->res_fac) / 1506;
	} else {
		enter_thres = RES_FAC_MUX(pdata->sleep_enter_current * 1000,
					  di->res_fac) / 1506;
		exit_thres = RES_FAC_MUX(pdata->sleep_exit_current * 1000,
					 di->res_fac) / 1506;
	}

	/* set relax enter and exit threshold */
	buf = enter_thres & 0xff;
	rk816_bat_write(di, RK816_RELAX_ENTRY_THRES_REGL, buf);
	buf = (enter_thres >> 8) & 0xff;
	rk816_bat_write(di, RK816_RELAX_ENTRY_THRES_REGH, buf);

	buf = exit_thres & 0xff;
	rk816_bat_write(di, RK816_RELAX_EXIT_THRES_REGL, buf);
	buf = (exit_thres >> 8) & 0xff;
	rk816_bat_write(di, RK816_RELAX_EXIT_THRES_REGH, buf);

	/* set sample current threshold */
	buf = filter_thres & 0xff;
	rk816_bat_write(di, RK816_SLEEP_CON_SAMP_CUR_REG, buf);

	/* reset relax update state */
	rk816_bat_restart_relax(di);
	DBG("<%s>. sleep_enter_current = %d, sleep_exit_current = %d\n",
	    __func__, pdata->sleep_enter_current, pdata->sleep_exit_current);
}

/* high load: current < 0 with charger in.
 * System will not shutdown while dsoc=0% with charging state(ac_in),
 * which will cause over discharge, so oppose status before report states.
 */
static void rk816_bat_lowpwr_check(struct rk816_battery *di)
{
	static u64 time;
	int pwr_off_thresd = di->pdata->pwroff_vol;

	if (di->current_avg < 0 && di->voltage_avg < pwr_off_thresd) {
		if (!time)
			time = get_boot_sec();

		if ((base2sec(time) > MINUTE(1)) ||
		    (di->voltage_avg <= pwr_off_thresd - 50)) {
			di->fake_offline = 1;
			if (di->voltage_avg <= pwr_off_thresd - 50)
				di->dsoc--;
			BAT_INFO("low power, soc=%d, current=%d\n",
				 di->dsoc, di->current_avg);
		}
	} else {
		time = 0;
		di->fake_offline = 0;
	}

	DBG("<%s>. t=%lu, dsoc=%d, current=%d, fake_offline=%d\n",
	    __func__, base2sec(time), di->dsoc,
	    di->current_avg, di->fake_offline);
}

static bool is_rk816_bat_exist(struct rk816_battery *di)
{
	return (rk816_bat_read(di, RK816_SUP_STS_REG) & BAT_EXS) ? true : false;
}

static bool is_rk816_bat_first_pwron(struct rk816_battery *di)
{
	u8 buf;

	buf = rk816_bat_read(di, RK816_GGSTS_REG);
	if (buf & BAT_CON) {
		buf &= ~BAT_CON;
		rk816_bat_write(di, RK816_GGSTS_REG, buf);
		return true;
	}

	return false;
}

static u8 rk816_bat_get_pwroff_min(struct rk816_battery *di)
{
	u8 now_min, last_min;

	now_min = rk816_bat_read(di, RK816_NON_ACT_TIMER_CNT_REG);
	last_min = rk816_bat_read(di, RK816_NON_ACT_TIMER_CNT_REG_SAVE);
	rk816_bat_write(di, RK816_NON_ACT_TIMER_CNT_REG_SAVE, now_min);

	return (now_min != last_min) ? now_min : 0;
}

static u8 is_rk816_bat_initialized(struct rk816_battery *di)
{
	u8 val = rk816_bat_read(di, RK816_MISC_MARK_REG);

	if (val & FG_INIT) {
		val &= ~FG_INIT;
		rk816_bat_write(di, RK816_MISC_MARK_REG, val);
		return true;
	} else {
		return false;
	}
}

static bool is_rk816_bat_ocv_valid(struct rk816_battery *di)
{
	return (!di->is_initialized && di->pwroff_min >= 30) ? true : false;
}

static void rk816_bat_init_age_algorithm(struct rk816_battery *di)
{
	int age_level, ocv_soc, ocv_cap, ocv_vol;

	if (di->bat_first_power_on || is_rk816_bat_ocv_valid(di)) {
		DBG("<%s> enter.\n", __func__);
		ocv_vol = rk816_bat_get_ocv_voltage(di);
		ocv_soc = rk816_bat_vol_to_ocvsoc(di, ocv_vol);
		ocv_cap = rk816_bat_vol_to_ocvcap(di, ocv_vol);
		if (ocv_soc < 20) {
			di->age_voltage = ocv_vol;
			di->age_ocv_cap = ocv_cap;
			di->age_ocv_soc = ocv_soc;
			di->age_adjust_cap = 0;

			if (ocv_soc <= 0)
				di->age_level = 100;
			else if (ocv_soc < 5)
				di->age_level = 95;
			else if (ocv_soc < 10)
				di->age_level = 90;
			else
				di->age_level = 80;

			age_level = rk816_bat_get_age_level(di);
			if (age_level > di->age_level) {
				di->age_allow_update = false;
				age_level -= 5;
				if (age_level <= 80)
					age_level = 80;
				rk816_bat_save_age_level(di, age_level);
			} else {
				di->age_allow_update = true;
				di->age_keep_sec = get_boot_sec();
			}

			BAT_INFO("init_age_algorithm: age_vol:%d, age_ocv_cap:%d, age_ocv_soc:%d, old_age_level:%d, age_allow_update:%d, new_age_level:%d\n",
				 di->age_voltage, di->age_ocv_cap,
				 ocv_soc, age_level, di->age_allow_update,
				 di->age_level);
		}
	}
}

static enum power_supply_property rk816_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
};

static int rk816_bat_ac_set_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     const union power_supply_propval *val)
{
	struct rk816_battery *di = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (val->intval)
			rk816_bat_enable_input_current(di);
		else
			rk816_bat_disable_input_current(di);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk816_bat_usb_set_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      const union power_supply_propval *val)
{
	struct rk816_battery *di = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (val->intval)
			rk816_bat_enable_input_current(di);
		else
			rk816_bat_disable_input_current(di);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk816_get_capacity_leve(struct rk816_battery *di)
{
	if (di->pdata->bat_mode == MODE_VIRTUAL)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;

	if (di->dsoc < 1)
		return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	else if (di->dsoc <= 20)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (di->dsoc <= 70)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (di->dsoc <= 90)
		return POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	else
		return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
}

static int rk816_battery_time_to_full(struct rk816_battery *di)
{
	int time_sec;
	int cap_temp;

	if (di->pdata->bat_mode == MODE_VIRTUAL) {
		time_sec = 3600;
	} else if (di->voltage_avg > 0) {
		cap_temp = di->pdata->design_capacity - di->remain_cap;
		if (cap_temp < 0)
			cap_temp = 0;
		time_sec = (3600 * cap_temp) / di->voltage_avg;
	} else {
		time_sec = 3600 * 24; /* One day */
	}

	return time_sec;
}

static int rk816_battery_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct rk816_battery *di = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = di->current_avg * 1000;/*uA*/
		if (di->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_CURRENT * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = di->voltage_avg * 1000;/*uV*/
		if (di->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_VOLTAGE * 1000;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = is_rk816_bat_exist(di);
		if (di->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_PRESET;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = di->dsoc;
		if (di->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_SOC;
		DBG("<%s>. report dsoc: %d\n", __func__, val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = rk816_get_capacity_leve(di);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = di->prop_status;
		if (di->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_STATUS;
		if (!rk816_bat_is_input_enabled(di))
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = di->temperature;
		if (di->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_TEMPERATURE;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = di->charge_count;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = di->pdata->design_capacity * 1000;/* uAh */
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = rk816_battery_time_to_full(di);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property rk816_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
};

static enum power_supply_property rk816_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
};

static int rk816_bat_ac_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	int ret = 0;
	struct rk816_battery *di = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (di->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_AC_ONLINE;
		else if (di->fake_offline)
			val->intval = 0;
		else
			val->intval = di->ac_in | di->dc_in;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = di->voltage_max;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = di->current_max;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = rk816_bat_is_input_enabled(di);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rk816_bat_usb_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	int ret = 0;
	struct rk816_battery *di = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (di->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_USB_ONLINE;
		else if (di->fake_offline)
			val->intval = 0;
		else
			val->intval = di->usb_in;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = di->voltage_max;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = di->current_max;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = rk816_bat_is_input_enabled(di);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rk816_bat_writable_property(struct power_supply *psy,
				       enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		return 0;
	}

	return 0;
}

static const struct power_supply_desc rk816_bat_desc = {
	.name		= "battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= rk816_bat_props,
	.num_properties	= ARRAY_SIZE(rk816_bat_props),
	.get_property	= rk816_battery_get_property,
};

static const struct power_supply_desc rk816_ac_desc = {
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = rk816_ac_props,
	.num_properties = ARRAY_SIZE(rk816_ac_props),
	.get_property = rk816_bat_ac_get_property,
	.set_property = rk816_bat_ac_set_property,
	.property_is_writeable = rk816_bat_writable_property,
};

static const struct power_supply_desc rk816_usb_desc = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = rk816_usb_props,
	.num_properties = ARRAY_SIZE(rk816_usb_props),
	.get_property = rk816_bat_usb_get_property,
	.set_property = rk816_bat_usb_set_property,
	.property_is_writeable = rk816_bat_writable_property,
};

static int rk816_bat_init_power_supply(struct rk816_battery *di)
{
	struct power_supply_config psy_cfg = { .drv_data = di, };

	di->bat = devm_power_supply_register(di->dev,
					     &rk816_bat_desc, &psy_cfg);
	if (IS_ERR(di->bat)) {
		dev_err(di->dev, "register bat power supply fail\n");
		return PTR_ERR(di->bat);
	}

	di->ac = devm_power_supply_register(di->dev,
					    &rk816_ac_desc, &psy_cfg);
	if (IS_ERR(di->ac)) {
		dev_err(di->dev, "register ac power supply fail\n");
		return PTR_ERR(di->ac);
	}

	di->usb = devm_power_supply_register(di->dev,
					     &rk816_usb_desc, &psy_cfg);
	if (IS_ERR(di->usb)) {
		dev_err(di->dev, "register usb power supply fail\n");
		return PTR_ERR(di->usb);
	}

	return 0;
}

static void rk816_bat_save_cap(struct rk816_battery *di, int capacity)
{
	u8 buf;
	static u32 old_cap;

	if (capacity >= di->qmax)
		capacity = di->qmax;
	if (capacity <= 0)
		capacity = 0;
	if (old_cap == capacity)
		return;

	old_cap = capacity;
	buf = (capacity >> 24) & 0xff;
	rk816_bat_write(di, RK816_REMAIN_CAP_REG3, buf);
	buf = (capacity >> 16) & 0xff;
	rk816_bat_write(di, RK816_REMAIN_CAP_REG2, buf);
	buf = (capacity >> 8) & 0xff;
	rk816_bat_write(di, RK816_REMAIN_CAP_REG1, buf);
	buf = (capacity >> 0) & 0xff;
	rk816_bat_write(di, RK816_REMAIN_CAP_REG0, buf);
}

static int rk816_bat_get_prev_cap(struct rk816_battery *di)
{
	int val = 0;

	val |= rk816_bat_read(di, RK816_REMAIN_CAP_REG3) << 24;
	val |= rk816_bat_read(di, RK816_REMAIN_CAP_REG2) << 16;
	val |= rk816_bat_read(di, RK816_REMAIN_CAP_REG1) << 8;
	val |= rk816_bat_read(di, RK816_REMAIN_CAP_REG0) << 0;

	return val;
}

static void rk816_bat_save_fcc(struct rk816_battery *di, u32 fcc)
{
	u8 buf;

	buf = (fcc >> 24) & 0xff;
	rk816_bat_write(di, RK816_NEW_FCC_REG3, buf);
	buf = (fcc >> 16) & 0xff;
	rk816_bat_write(di, RK816_NEW_FCC_REG2, buf);
	buf = (fcc >> 8) & 0xff;
	rk816_bat_write(di, RK816_NEW_FCC_REG1, buf);
	buf = (fcc >> 0) & 0xff;
	rk816_bat_write(di, RK816_NEW_FCC_REG0, buf);

	BAT_INFO("save fcc: %d\n", fcc);
}

static int rk816_bat_get_fcc(struct rk816_battery *di)
{
	u32 fcc = 0;

	fcc |= rk816_bat_read(di, RK816_NEW_FCC_REG3) << 24;
	fcc |= rk816_bat_read(di, RK816_NEW_FCC_REG2) << 16;
	fcc |= rk816_bat_read(di, RK816_NEW_FCC_REG1) << 8;
	fcc |= rk816_bat_read(di, RK816_NEW_FCC_REG0) << 0;

	if (fcc < MIN_FCC) {
		BAT_INFO("invalid fcc(%d), use design cap", fcc);
		fcc = di->pdata->design_capacity;
		rk816_bat_save_fcc(di, fcc);
	} else if (fcc > di->pdata->design_qmax) {
		BAT_INFO("invalid fcc(%d), use qmax", fcc);
		fcc = di->pdata->design_qmax;
		rk816_bat_save_fcc(di, fcc);
	}

	return fcc;
}

static int rk816_bat_get_lock_fcc(struct rk816_battery *di)
{
	u8 reg;
	int fcc, val = 0;

	/* check lock flag, 1: yes, 0: no */
	reg = rk816_bat_read(di, RK816_GGSTS_REG);
	if ((reg & FCC_LOCK) == 0)
		return 0;

	val |= rk816_bat_read(di, RK816_FCC_GASCNT_REG3) << 24;
	val |= rk816_bat_read(di, RK816_FCC_GASCNT_REG2) << 16;
	val |= rk816_bat_read(di, RK816_FCC_GASCNT_REG1) << 8;
	val |= rk816_bat_read(di, RK816_FCC_GASCNT_REG0) << 0;
	fcc = val / 2390;

	/* clear lock flag */
	reg &= ~FCC_LOCK;
	rk816_bat_write(di, RK816_GGSTS_REG, reg);
	BAT_INFO("lock fcc = %d\n", fcc);

	return fcc;
}

static void rk816_bat_save_dsoc(struct rk816_battery *di, u8 save_soc)
{
	static int last_soc = -1;

	if (last_soc != save_soc) {
		rk816_bat_write(di, RK816_SOC_REG, save_soc);
		last_soc = save_soc;
	}
}

static int rk816_bat_get_prev_dsoc(struct rk816_battery *di)
{
	return rk816_bat_read(di, RK816_SOC_REG);
}

static void rk816_bat_save_reboot_cnt(struct rk816_battery *di, u8 save_cnt)
{
	rk816_bat_write(di, RK816_REBOOT_CNT_REG, save_cnt);
}

static void rk816_bat_init_leds(struct rk816_battery *di)
{
	if (rk816_led_ops && rk816_led_ops->led_init) {
		rk816_led_ops->led_init(di);
		BAT_INFO("leds initialized\n");
	}
}

static void rk816_bat_update_leds(struct rk816_battery *di, int prop)
{
	static int old_prop = -1;

	if (prop == old_prop)
		return;

	old_prop = prop;
	switch (prop) {
	case POWER_SUPPLY_STATUS_FULL:
		if (rk816_led_ops && rk816_led_ops->led_charging_full) {
			rk816_led_ops->led_charging_full(di);
			BAT_INFO("charging full led on\n");
		}
		break;
	case POWER_SUPPLY_STATUS_CHARGING:
		if (rk816_led_ops && rk816_led_ops->led_charging) {
			rk816_led_ops->led_charging(di);
			BAT_INFO("charging led on\n");
		}
		break;
	case POWER_SUPPLY_STATUS_DISCHARGING:
		if (rk816_led_ops && rk816_led_ops->led_discharging) {
			rk816_led_ops->led_discharging(di);
			BAT_INFO("discharging led on\n");
		}
		break;
	default:
		BAT_INFO("Unknown led update\n");
		break;
	}
}

static void rk816_bat_set_chrg_current(struct rk816_battery *di,
				       u8 chrg_current)
{
	u8 chrg_ctrl_reg1;

	chrg_ctrl_reg1 = rk816_bat_read(di, RK816_CHRG_CTRL_REG1);
	chrg_ctrl_reg1 &= ~CHRG_CUR_MSK;
	chrg_ctrl_reg1 |= (chrg_current);
	rk816_bat_write(di, RK816_CHRG_CTRL_REG1, chrg_ctrl_reg1);
}

static void rk816_bat_set_input_current(struct rk816_battery *di,
					int input_current)
{
	u8 usb_ctrl;

	if (di->pdata->bat_mode == MODE_VIRTUAL) {
		BAT_INFO("virtual power test mode, set max input current\n");
		input_current = di->chrg_cur_input;
	}

	usb_ctrl = rk816_bat_read(di, RK816_USB_CTRL_REG);
	usb_ctrl &= ~INPUT_CUR_MSK;
	usb_ctrl |= (input_current);
	rk816_bat_write(di, RK816_USB_CTRL_REG, usb_ctrl);
}

static void rk816_bat_set_chrg_param(struct rk816_battery *di,
				     enum charger_t charger_type)
{
	u8 buf, usb_ctrl, chrg_ctrl1;
	const char *charger_name[] = {"NONE", "NONE USB", "USB", "AC",
				      "CDP1.5A", "DC", "NONE DC"};

	switch (charger_type) {
	case USB_TYPE_UNKNOWN_CHARGER:
		di->usb_in = 0;
		di->ac_in = 0;
		di->dc_in = 0;
		di->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
		rk816_bat_set_chrg_current(di, di->chrg_cur_sel);
		rk816_bat_set_input_current(di, INPUT_CUR450MA);
		power_supply_changed(di->bat);
		power_supply_changed(di->usb);
		power_supply_changed(di->ac);
		break;
	case USB_TYPE_NONE_CHARGER:
		di->usb_in = 0;
		di->ac_in = 0;
		if (di->dc_in == 0) {
			di->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
			rk816_bat_set_chrg_current(di, di->chrg_cur_sel);
			rk816_bat_set_input_current(di, INPUT_CUR450MA);
		}
		power_supply_changed(di->usb);
		power_supply_changed(di->ac);
		break;
	case USB_TYPE_USB_CHARGER:
		di->usb_in = 1;
		di->ac_in = 0;
		di->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		if (di->dc_in == 0) {
			rk816_bat_set_chrg_current(di, di->chrg_cur_sel);
			rk816_bat_set_input_current(di, INPUT_CUR450MA);
		}
		power_supply_changed(di->usb);
		break;
	case USB_TYPE_CDP_CHARGER:
		di->usb_in = 1;
		di->ac_in = 0;
		di->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		if (di->dc_in == 0) {
			rk816_bat_set_chrg_current(di, di->chrg_cur_sel);
			rk816_bat_set_input_current(di, INPUT_CUR1500MA);
		}
		power_supply_changed(di->usb);
		break;
	case USB_TYPE_AC_CHARGER:
		di->ac_in = 1;
		di->usb_in = 0;
		di->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		rk816_bat_set_chrg_current(di, di->chrg_cur_sel);
		rk816_bat_set_input_current(di, di->chrg_cur_input);
		power_supply_changed(di->ac);
		break;
	case DC_TYPE_DC_CHARGER:
		di->dc_in = 1;
		di->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		rk816_bat_set_chrg_current(di, di->chrg_cur_sel);
		rk816_bat_set_input_current(di, di->chrg_cur_input);
		power_supply_changed(di->ac);
		break;
	case DC_TYPE_NONE_CHARGER:
		di->dc_in = 0;
		/*
		 * check by pmic int avoid usb error notify:
		 * when plug in dc, usb may error notify usb/ac plug in,
		 * while dc plug out, the "ac/usb_in" still hold
		 */
		buf = rk816_bat_read(di, RK816_VB_MON_REG);
		if ((buf & PLUG_IN_STS) == 0) {
			di->ac_in = 0;
			di->usb_in = 0;
			di->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
			rk816_bat_set_chrg_current(di, di->chrg_cur_sel);
			rk816_bat_set_input_current(di, INPUT_CUR450MA);
		} else if (di->usb_in) {
			rk816_bat_set_chrg_current(di, di->chrg_cur_sel);
			rk816_bat_set_input_current(di, INPUT_CUR450MA);
			di->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		}
		power_supply_changed(di->usb);
		power_supply_changed(di->ac);
		break;
	default:
		di->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	}

	di->charger_changed = 1;

	usb_ctrl = rk816_bat_read(di, RK816_USB_CTRL_REG);
	chrg_ctrl1 = rk816_bat_read(di, RK816_CHRG_CTRL_REG1);
	BAT_INFO("set charger type: %s, current: input=%d, chrg=%d\n",
		 charger_name[charger_type],
		 CHRG_CUR_INPUT[usb_ctrl & 0x0f],
		 CHRG_CUR_SEL[chrg_ctrl1 & 0x0f]);

	if (di->dsoc == 100 && rk816_bat_chrg_online(di))
		di->prop_status = POWER_SUPPLY_STATUS_FULL;

	rk816_bat_update_leds(di, di->prop_status);
}

static void rk816_bat_set_otg_in(struct rk816_battery *di, int online)
{
	di->otg_in = online;
}

/*
 * -----: VBUS-5V
 * #####: PMIC_INT
 *
 *
 *		A	140ms	   D
 *		|------------------>>>>>>>>>>>>>>>
 *		|	B   C
 * ##########################
 *		|	    #
 *		|   100ms   #   F    E
 * --------------	    ##############
 *
 * [PMIC]
 *	A: charger plugin event(vbus-5v on);
 *	C: pmic reaction time finish, [A~C] = 100ms;
 *	D: pmic switch to charging mode, start charging, [A~D] = 140ms;
 *
 * [Software]
 *	B: PLUG_IN_STS=0, we think it's not charging mode, so enable otg+boost,
 *	   but actually, PLUG_IN_STS is not effective now.
 *	F: pmic reaction finish, PLUG_IN_STS is effective and we do check again.
 *	E: output-5v mode really works(enable boost+otg)
 *
 * [Mistake detail]
 *	1. Charger plugin at spot-A and switch to charing mode at spot-D.
 *	2. Software check PLUG_IN_STS=0 at spot-B, so we think it's not
 *	   charging mode and we enable boost+otg, and this really works at
 *	   spot-E(because delay of i2c transfer or other).
 *	3. It's a pity that pmic has been changed to charing mode at spot-D
 *	   earlier than spot-E.
 *
 * After above mistake, we enable otg+boost in charing mode. Then, boost will
 * burn off if we plugout charger.
 *
 * [Solution]
 *	we should abey the rule: Don't enable boost while in charging mode.
 * We should enable otg first at spot-B, trying to switch to output-5v mode,
 * then delay 140ms(pmic reaction and other) to check effective PLUG_IN_STS
 * again at spot-F, if PLUG_IN_STS=1, means it's charging mode now, we abandont
 * enable boost and disable otg. Otherwise, we can turn on boost safely.
 */
static void rk816_bat_set_otg_power(struct rk816_battery *di, int power)
{
	u8 buf;

	switch (power) {
	case USB_OTG_POWER_ON:
		if (di->otg_pmic5v) {
			BAT_INFO("otg5v is on yet, ignore..\n");
			break;
		}

		/* (spot-B). for safe, detect vbus-5v by pmic self */
		buf = rk816_bat_read(di, RK816_VB_MON_REG);
		if (buf & PLUG_IN_STS) {
			BAT_INFO("detect vbus-5v suppling, deny otg on..\n");
			break;
		}

		/* (spot-B). enable otg, try to switch to output-5v mode */
		rk816_bat_set_bits(di, RK816_DCDC_EN_REG2,
				   BOOST_OTG_MASK, BOOST_OFF_OTG_ON);

		/*
		 * pmic need about 140ms to switch to charging mode, so wait
		 * 140ms and check charger again. if still check vbus-5v online,
		 * that means it's charger mode now, we should turn off boost
		 * and otg, then return.
		 */
		msleep(140);
		/* spot-F */
		buf = rk816_bat_read(di, RK816_VB_MON_REG);
		if (buf & PLUG_IN_STS) {
			rk816_bat_set_bits(di, RK816_DCDC_EN_REG2,
					   BOOST_OTG_MASK, BOOST_OTG_OFF);
			BAT_INFO("detect vbus-5v suppling too, deny otg on\n");
			break;
		}

		/*
		 * reach here, means pmic switch to output-5v mode ok, it's
		 * safe to enable boost-5v on output mode.
		 */
		rk816_bat_set_bits(di, RK816_DCDC_EN_REG2,
				   BOOST_OTG_MASK, BOOST_OTG_ON);
		di->otg_pmic5v = 1;
		break;

	case USB_OTG_POWER_OFF:
		if (!di->otg_pmic5v) {
			BAT_INFO("otg5v is off yet, ignore..\n");
		} else {
			rk816_bat_set_bits(di, RK816_DCDC_EN_REG2,
					   BOOST_OTG_MASK, BOOST_OTG_OFF);
			di->otg_pmic5v = 0;
		}
		break;

	default:
		break;
	}
}

static enum charger_t rk816_bat_get_adc_dc_state(struct rk816_battery *di)
{
	int val = 0;

	if (!di->iio_chan) {
		di->iio_chan = iio_channel_get(&di->rk816->i2c->dev, NULL);
		if (IS_ERR(di->iio_chan)) {
			di->iio_chan = NULL;
			return DC_TYPE_NONE_CHARGER;
		}
	}

	if (iio_read_channel_raw(di->iio_chan, &val) < 0) {
		pr_err("read channel error\n");
		return DC_TYPE_NONE_CHARGER;
	}

	return (val >= DC_ADC_TRIGGER) ?
		DC_TYPE_DC_CHARGER : DC_TYPE_NONE_CHARGER;
}

static enum charger_t rk816_bat_get_gpio_dc_state(struct rk816_battery *di)
{
	int level;

	if (!gpio_is_valid(di->pdata->dc_det_pin))
		return DC_TYPE_NONE_CHARGER;

	level = gpio_get_value(di->pdata->dc_det_pin);

	return (level == di->pdata->dc_det_level) ?
		DC_TYPE_DC_CHARGER : DC_TYPE_NONE_CHARGER;
}

static enum charger_t rk816_bat_get_dc_state(struct rk816_battery *di)
{
	enum charger_t type;

	if (di->pdata->dc_det_adc)
		type = rk816_bat_get_adc_dc_state(di);
	else
		type = rk816_bat_get_gpio_dc_state(di);

	return type;
}

static void rk816_bat_dc_delay_work(struct work_struct *work)
{
	enum charger_t type;
	static enum charger_t old_type = USB_TYPE_UNKNOWN_CHARGER;
	struct rk816_battery *di = container_of(work,
				struct rk816_battery, dc_delay_work.work);

	type = rk816_bat_get_dc_state(di);
	if (old_type == type)
		goto out;

	old_type = type;
	if (type == DC_TYPE_DC_CHARGER) {
		BAT_INFO("detect dc charger in..\n");
		rk816_bat_set_chrg_param(di, DC_TYPE_DC_CHARGER);
		/* check otg supply */
		if (di->otg_in && di->pdata->power_dc2otg) {
			BAT_INFO("otg power from dc adapter\n");
			rk816_bat_set_otg_power(di, USB_OTG_POWER_OFF);
		}
	} else {
		BAT_INFO("detect dc charger out..\n");
		rk816_bat_set_chrg_param(di, DC_TYPE_NONE_CHARGER);
		/* check otg supply, power on anyway */
		if (di->otg_in) {
			BAT_INFO("charge disable, enable otg\n");
			/*
			 * must wait 200ms to wait 5v-input fade away before
			 * enable boost
			 */
			msleep(200);
			rk816_bat_set_otg_power(di, USB_OTG_POWER_ON);
		}
	}
out:
	/* adc need check all the time */
	if (di->pdata->dc_det_adc)
		queue_delayed_work(di->usb_charger_wq,
				   &di->dc_delay_work,
				   msecs_to_jiffies(1000));
}

static int rk816_bat_fb_notifier(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	struct rk816_battery *di;
	struct fb_event *evdata = data;

	di = container_of(nb, struct rk816_battery, fb_nb);

	if (event == FB_EVENT_BLANK || event == FB_EARLY_EVENT_BLANK ||
	    event == FB_R_EARLY_EVENT_BLANK)
		di->fb_blank = *(int *)evdata->data;
	else
		di->fb_blank = 1;

	return 0;
}

static int rk816_bat_register_fb_notify(struct rk816_battery *di)
{
	memset(&di->fb_nb, 0, sizeof(di->fb_nb));
	di->fb_nb.notifier_call = rk816_bat_fb_notifier;

	return fb_register_client(&di->fb_nb);
}

static int rk816_bat_unregister_fb_notify(struct rk816_battery *di)
{
	return fb_unregister_client(&di->fb_nb);
}

static void rk816_bat_init_coulomb_cap(struct rk816_battery *di, u32 capacity)
{
	u8 buf;
	u32 cap;

	if (!di->over_20mR)
		cap = RES_FAC_DIV(capacity * 2390, di->res_fac);
	else
		cap = RES_FAC_MUX(capacity * 2390, di->res_fac);

	buf = (cap >> 24) & 0xff;
	rk816_bat_write(di, RK816_GASCNT_CAL_REG3, buf);
	buf = (cap >> 16) & 0xff;
	rk816_bat_write(di, RK816_GASCNT_CAL_REG2, buf);
	buf = (cap >> 8) & 0xff;
	rk816_bat_write(di, RK816_GASCNT_CAL_REG1, buf);
	buf = (cap >> 0) & 0xff;
	rk816_bat_write(di, RK816_GASCNT_CAL_REG0, buf);

	di->remain_cap = capacity;
	di->rsoc = rk816_bat_get_rsoc(di);
}

static u8 rk816_bat_get_halt_cnt(struct rk816_battery *di)
{
	return rk816_bat_read(di, RK816_HALT_CNT_REG);
}

static void rk816_bat_inc_halt_cnt(struct rk816_battery *di)
{
	u8 cnt;

	cnt = rk816_bat_read(di, RK816_HALT_CNT_REG);
	rk816_bat_write(di, RK816_HALT_CNT_REG, ++cnt);
}

static bool is_rk816_bat_last_halt(struct rk816_battery *di)
{
	int pre_cap = rk816_bat_get_prev_cap(di);
	int now_cap = rk816_bat_get_coulomb_cap(di);

	/* over 10%: system halt last time */
	if (abs(now_cap - pre_cap) > (di->fcc / 10)) {
		rk816_bat_inc_halt_cnt(di);
		return true;
	} else {
		return false;
	}
}

static void rk816_bat_first_pwron(struct rk816_battery *di)
{
	int ocv_vol;

	rk816_bat_save_fcc(di, di->design_cap);
	ocv_vol = rk816_bat_get_ocv_voltage(di);
	di->fcc = rk816_bat_get_fcc(di);
	di->nac = rk816_bat_vol_to_ocvcap(di, ocv_vol);
	di->rsoc = rk816_bat_vol_to_ocvsoc(di, ocv_vol);
	di->dsoc = di->rsoc;
	di->is_first_on = true;

	BAT_INFO("first on: dsoc=%d, rsoc=%d cap=%d, fcc=%d, ov=%d\n",
		 di->dsoc, di->rsoc, di->nac, di->fcc, ocv_vol);
}

static void rk816_bat_not_first_pwron(struct rk816_battery *di)
{
	int now_cap, pre_soc, pre_cap, ocv_cap, ocv_soc, ocv_vol;

	di->fcc = rk816_bat_get_fcc(di);
	pre_soc = rk816_bat_get_prev_dsoc(di);
	pre_cap = rk816_bat_get_prev_cap(di);
	now_cap = rk816_bat_get_coulomb_cap(di);
	di->is_halt = is_rk816_bat_last_halt(di);
	di->halt_cnt = rk816_bat_get_halt_cnt(di);
	di->is_initialized = is_rk816_bat_initialized(di);
	di->is_ocv_calib = is_rk816_bat_ocv_valid(di);

	if (di->is_initialized) {
		BAT_INFO("initialized yet..\n");
		goto finish;
	} else if (di->is_halt) {
		BAT_INFO("system halt last time... cap: pre=%d, now=%d\n",
			 pre_cap, now_cap);
		if (now_cap < 0)
			now_cap = 0;
		rk816_bat_init_coulomb_cap(di, now_cap);
		pre_cap = now_cap;
		pre_soc = di->rsoc;
		goto finish;
	} else if (di->is_ocv_calib) {
		ocv_vol = rk816_bat_get_ocv_voltage(di);
		ocv_soc = rk816_bat_vol_to_ocvsoc(di, ocv_vol);
		ocv_cap = rk816_bat_vol_to_ocvcap(di, ocv_vol);
		pre_cap = ocv_cap;
		di->ocv_pre_dsoc = pre_soc;
		di->ocv_new_dsoc = ocv_soc;
		if (abs(ocv_soc - pre_soc) >= di->pdata->max_soc_offset) {
			di->ocv_pre_dsoc = pre_soc;
			di->ocv_new_dsoc = ocv_soc;
			di->is_max_soc_offset = true;
			BAT_INFO("trigger max soc offset, dsoc: %d -> %d\n",
				 pre_soc, ocv_soc);
			pre_soc = ocv_soc;
		}
		BAT_INFO("OCV calib: cap=%d, rsoc=%d\n", ocv_cap, ocv_soc);
	} else if (di->pwroff_min > 0) {
		ocv_vol = rk816_bat_get_ocv_voltage(di);
		ocv_soc = rk816_bat_vol_to_ocvsoc(di, ocv_vol);
		ocv_cap = rk816_bat_vol_to_ocvcap(di, ocv_vol);
		di->force_pre_dsoc = pre_soc;
		di->force_new_dsoc = ocv_soc;
		if (abs(ocv_soc - pre_soc) >= 80) {
			di->is_force_calib = true;
			BAT_INFO("dsoc force calib: %d -> %d\n",
				 pre_soc, ocv_soc);
			pre_soc = ocv_soc;
			pre_cap = ocv_cap;
		}
	}

finish:
	di->dsoc = pre_soc;
	di->nac = pre_cap;
	if (di->nac < 0)
		di->nac = 0;

	BAT_INFO("dsoc=%d cap=%d v=%d ov=%d rv=%d min=%d psoc=%d pcap=%d\n",
		 di->dsoc, di->nac, rk816_bat_get_avg_voltage(di),
		 rk816_bat_get_ocv_voltage(di), rk816_bat_get_relax_voltage(di),
		 di->pwroff_min, rk816_bat_get_prev_dsoc(di),
		 rk816_bat_get_prev_cap(di));
}

static bool rk816_bat_ocv_sw_reset(struct rk816_battery *di)
{
	u8 buf;

	buf = rk816_bat_read(di, RK816_MISC_MARK_REG);
	if (((buf & FG_RESET_LATE) && di->pwroff_min >= 30) ||
	    (buf & FG_RESET_NOW)) {
		buf &= ~FG_RESET_LATE;
		buf &= ~FG_RESET_NOW;
		rk816_bat_write(di, RK816_MISC_MARK_REG, buf);
		BAT_INFO("manual reset fuel gauge\n");
		return true;
	} else {
		return false;
	}
}

static void rk816_bat_setup_ocv_table(struct rk816_battery *di, int temp)
{
	int i, idx = 0;
	int temp_h, temp_l, percent, volt_htemp, volt_ltemp;
	int *temp_t = di->pdata->temp_t;
	int temp_t_num = di->pdata->temp_t_num;

	if (temp_t_num < 2)
		return;

	DBG("<%s>. temperature=%d\n", __func__, temp);

	/* Out of MIN, select MIN */
	if (temp < temp_t[0]) {
		DBG("<%s>. Out MIN\n", __func__);
		di->pdata->ocv_table = di->pdata->table_t[0];
		return;
	}

	/* Out of MAX, select MAX */
	if (temp > temp_t[temp_t_num - 1]) {
		DBG("<%s>. Out MAX\n", __func__);
		di->pdata->ocv_table = di->pdata->table_t[temp_t_num - 1];
		return;
	}

	/* Exactly match some one */
	for (i = 0; i < temp_t_num; i++) {
		if (temp == temp_t[i]) {
			DBG("<%s>. Match: %d'C\n", __func__, temp_t[i]);
			di->pdata->ocv_table = di->pdata->table_t[i];
			return;
		}
	}

	/* Find position of current temperature, must be fond */
	for (i = 0; i < temp_t_num - 1; i++) {
		if ((temp > temp_t[i]) && (temp < temp_t[i + 1])) {
			idx = i;
			break;
		}
	}

	DBG("<%s>. found! idx = %d\n", __func__, idx);

	/* calculate percent */
	temp_l = temp_t[idx];
	temp_h = temp_t[idx + 1];
	percent = (temp - temp_l) * 100 / DIV(temp_h - temp_l);

	/* Fill in new ocv table members */
	for (i = 0; i < di->pdata->ocv_size; i++) {
		volt_ltemp = di->pdata->table_t[idx][i];
		volt_htemp = di->pdata->table_t[idx + 1][i];

		di->pdata->ocv_table[i] = volt_ltemp +
			(volt_htemp - volt_ltemp) * percent / 100;

		DBG("#low=%d'C[%dmv], me=%d'C[%dmv], high=%d'C[%dmv]. percent=%d, delta=%dmv\n",
		    temp_l, volt_ltemp, temp, di->pdata->ocv_table[i],
		    temp_h, volt_htemp, percent,
		    (volt_htemp - volt_ltemp) * percent / 100);
	}
}

static void rk816_bat_init_rsoc(struct rk816_battery *di)
{
	di->bat_first_power_on = is_rk816_bat_first_pwron(di);
	di->is_sw_reset = rk816_bat_ocv_sw_reset(di);
	di->pwroff_min = rk816_bat_get_pwroff_min(di);

	if (di->bat_first_power_on || di->is_sw_reset)
		rk816_bat_first_pwron(di);
	else
		rk816_bat_not_first_pwron(di);
}

static u8 rk816_bat_get_chrg_status(struct rk816_battery *di)
{
	u8 status;

	status = rk816_bat_read(di, RK816_SUP_STS_REG) & CHRG_STATUS_MSK;
	switch (status) {
	case CHARGE_OFF:
		DBG("CHARGE-OFF ...\n");
		break;
	case DEAD_CHARGE:
		BAT_INFO("DEAD CHARGE...\n");
		break;
	case TRICKLE_CHARGE:
		BAT_INFO("TRICKLE CHARGE...\n ");
		break;
	case CC_OR_CV:
		DBG("CC or CV...\n");
		break;
	case CHARGE_FINISH:
		DBG("CHARGE FINISH...\n");
		break;
	case USB_OVER_VOL:
		BAT_INFO("USB OVER VOL...\n");
		break;
	case BAT_TMP_ERR:
		BAT_INFO("BAT TMP ERROR...\n");
		break;
	case TIMER_ERR:
		BAT_INFO("TIMER ERROR...\n");
		break;
	case USB_EXIST:
		BAT_INFO("USB EXIST...\n");
		break;
	case USB_EFF:
		BAT_INFO("USB EFF...\n");
		break;
	default:
		BAT_INFO("UNKNOWN STATUS...\n");
		break;
	}

	return status;
}

static u8 rk816_bat_fb_temp(struct rk816_battery *di)
{
	u8 reg;
	int index, fb_temp;

	reg = DEFAULT_FB_TEMP;
	fb_temp = di->pdata->fb_temp;
	for (index = 0; index < ARRAY_SIZE(FEED_BACK_TEMP); index++) {
		if (fb_temp < FEED_BACK_TEMP[index])
			break;
		reg = (index << FB_TEMP_SHIFT);
	}

	return reg;
}

static void rk816_bat_select_sample_res(struct rk816_battery *di)
{
	if (di->pdata->sample_res == 20) {
		di->over_20mR = 0;
		di->res_fac = 10;
	} else if (di->pdata->sample_res > 20) {
		di->over_20mR = 1;
		di->res_fac = di->pdata->sample_res * 10 / 20;
	} else {
		di->over_20mR = 0;
		di->res_fac = 20 * 10 / di->pdata->sample_res;
	}
}

static u8 rk816_bat_decode_input_current(struct rk816_battery *di,
					u32 input_current)
{
	u8 val = DEFAULT_CHRG_CUR_INPUT;
	u8 index;

	for (index = 2; index < ARRAY_SIZE(CHRG_CUR_INPUT); index++) {
		if (input_current < 850 && input_current > 80) {
			val = 0x0;/* 450mA */
			break;
		} else if (input_current <= 80) {
			val = 0x1;/* 80mA */
			break;
		} else {
			if (input_current < CHRG_CUR_INPUT[index])
				break;
			val = (index << CHRG_CRU_INPUT_SHIFT);
		}
	}

	return val;
}

static u8 rk816_bat_decode_chrg_current(struct rk816_battery *di,
				       u32 chrg_current)
{
	u8 val = DEFAULT_CHRG_CUR_SEL;
	u8 index;

	if (di->pdata->sample_res < 20) {
		if (chrg_current > 2000)
			chrg_current = RES_FAC_DIV(chrg_current, di->res_fac);
		else
			chrg_current = 1000;
	} else if (di->pdata->sample_res > 20) {
		chrg_current = RES_FAC_MUX(chrg_current, di->res_fac);
		if (chrg_current > 2400)
			chrg_current = 2400;
		if (chrg_current < 1000)
			chrg_current = 1000;
	}

	for (index = 0; index < ARRAY_SIZE(CHRG_CUR_SEL); index++) {
		if (chrg_current < CHRG_CUR_SEL[index])
			break;
		val = (index << CHRG_CRU_SEL_SHIFT);
	}

	return val;
}

static u8 rk816_bat_decode_chrg_vol(struct rk816_battery *di,
				    u32 chrg_vol)
{
	u8 val = DEFAULT_CHRG_VOL_SEL;
	u8 index;

	for (index = 0; index < ARRAY_SIZE(CHRG_VOL_SEL); index++) {
		if (chrg_vol < CHRG_VOL_SEL[index])
			break;
		val = (index << CHRG_VOL_SEL_SHIFT);
	}

	return val;
}

static void rk816_bat_select_chrg_cv(struct rk816_battery *di)
{
	di->chrg_vol_sel = rk816_bat_decode_chrg_vol(di,
					di->pdata->max_chrg_voltage);
	di->chrg_cur_input = rk816_bat_decode_input_current(di,
					di->pdata->max_input_current);
	di->chrg_cur_sel = rk816_bat_decode_chrg_current(di,
					di->pdata->max_chrg_current);

	DBG("<%s>. vol = 0x%x, input = 0x%x, sel = 0x%x\n",
	    __func__, di->chrg_vol_sel, di->chrg_cur_input, di->chrg_cur_sel);
}

static u8 rk816_bat_finish_ma(struct rk816_battery *di, int fcc)
{
	u8 ma;

	if (fcc > 5000)
		ma = FINISH_250MA;
	else if (fcc >= 4000)
		ma = FINISH_200MA;
	else if (fcc >= 3000)
		ma = FINISH_150MA;
	else
		ma = FINISH_100MA;

	/* adjust ma according to sample resistor */
	if (di->pdata->sample_res < 20) {
		/* ma should div 2 */
		if (ma == FINISH_200MA)
			ma = FINISH_100MA;
		else if (ma == FINISH_250MA)
			ma = FINISH_150MA;
	} else if (di->pdata->sample_res > 20) {
		/* ma should mux 2 */
		if (ma == FINISH_100MA)
			ma = FINISH_200MA;
		else if (ma == FINISH_150MA)
			ma = FINISH_250MA;
	}

	return ma;
}

static void rk816_bat_init_chrg_config(struct rk816_battery *di)
{
	u8 chrg_ctrl1, usb_ctrl, chrg_ctrl2, chrg_ctrl3;
	u8 sup_sts, thermal, ggcon, finish_ma, fb_temp;

	rk816_bat_select_chrg_cv(di);
	finish_ma = rk816_bat_finish_ma(di, di->fcc);
	fb_temp = rk816_bat_fb_temp(di);

	ggcon = rk816_bat_read(di, RK816_GGCON_REG);
	sup_sts = rk816_bat_read(di, RK816_SUP_STS_REG);
	thermal = rk816_bat_read(di, RK816_THERMAL_REG);
	usb_ctrl = rk816_bat_read(di, RK816_USB_CTRL_REG);
	chrg_ctrl1 = rk816_bat_read(di, RK816_CHRG_CTRL_REG1);
	chrg_ctrl2 = rk816_bat_read(di, RK816_CHRG_CTRL_REG2);
	chrg_ctrl3 = rk816_bat_read(di, RK816_CHRG_CTRL_REG3);

	/* set charge current and voltage */
	usb_ctrl &= ~INPUT_CUR_MSK;
	usb_ctrl |= di->chrg_cur_input;
	chrg_ctrl1 = (CHRG_EN) | (di->chrg_vol_sel | di->chrg_cur_sel);

	/* set charge finish current */
	chrg_ctrl3 |= CHRG_TERM_DIG_SIGNAL;
	chrg_ctrl2 &= ~FINISH_CUR_MSK;
	chrg_ctrl2 |= finish_ma;

	/* disable cccv mode */
	chrg_ctrl3 &= ~CHRG_TIMER_CCCV_EN;

	/* enable voltage limit and enable input current limit */
	sup_sts |= USB_VLIMIT_EN;
	sup_sts |= USB_CLIMIT_EN;

	/* set feed back temperature */
	if (di->pdata->fb_temp)
		usb_ctrl |= CHRG_CT_EN;
	else
		usb_ctrl &= ~CHRG_CT_EN;
	thermal &= ~FB_TEMP_MSK;
	thermal |= fb_temp;

	/* adc current mode */
	ggcon |= ADC_CUR_MODE;
	ggcon |= AVG_CUR_MODE;

	rk816_bat_write(di, RK816_GGCON_REG, ggcon);
	rk816_bat_write(di, RK816_SUP_STS_REG, sup_sts);
	rk816_bat_write(di, RK816_THERMAL_REG, thermal);
	rk816_bat_write(di, RK816_USB_CTRL_REG, usb_ctrl);
	rk816_bat_write(di, RK816_CHRG_CTRL_REG1, chrg_ctrl1);
	rk816_bat_write(di, RK816_CHRG_CTRL_REG2, chrg_ctrl2);
	rk816_bat_write(di, RK816_CHRG_CTRL_REG3, chrg_ctrl3);
}

static void rk816_bat_init_poffset(struct rk816_battery *di)
{
	int coffset, ioffset;

	coffset = rk816_bat_get_coffset(di);
	ioffset = rk816_bat_get_ioffset(di);
	di->poffset = coffset - ioffset;
}

static void rk816_bat_caltimer_isr(struct timer_list *t)
{
	struct rk816_battery *di = from_timer(di, t, caltimer);

	mod_timer(&di->caltimer, jiffies + MINUTE(8) * HZ);
	queue_delayed_work(di->bat_monitor_wq, &di->calib_delay_work,
			   msecs_to_jiffies(10));
}

static void rk816_bat_internal_calib(struct work_struct *work)
{
	int ioffset;
	struct rk816_battery *di = container_of(work,
			struct rk816_battery, calib_delay_work.work);

	ioffset = rk816_bat_get_ioffset(di);
	rk816_bat_set_coffset(di, di->poffset + ioffset);
	rk816_bat_init_voltage_kb(di);
	BAT_INFO("caltimer: ioffset=0x%x, coffset=0x%x\n",
		 ioffset, rk816_bat_get_coffset(di));
}

static void rk816_bat_init_caltimer(struct rk816_battery *di)
{
	timer_setup(&di->caltimer, rk816_bat_caltimer_isr, 0);
	di->caltimer.expires = jiffies + MINUTE(8) * HZ;
	add_timer(&di->caltimer);
	INIT_DELAYED_WORK(&di->calib_delay_work, rk816_bat_internal_calib);
}

static void rk816_bat_init_zero_table(struct rk816_battery *di)
{
	int i, diff, min, max;
	size_t ocv_size, length;

	ocv_size = di->pdata->ocv_size;
	length = sizeof(di->pdata->zero_table) * ocv_size;
	di->pdata->zero_table =
			devm_kzalloc(di->dev, length, GFP_KERNEL);
	if (!di->pdata->zero_table) {
		di->pdata->zero_table = di->pdata->ocv_table;
		dev_err(di->dev, "malloc zero table fail\n");
		return;
	}

	min = di->pdata->pwroff_vol,
	max = di->pdata->ocv_table[ocv_size - 4];
	diff = (max - min) / DIV(ocv_size - 1);
	for (i = 0; i < ocv_size; i++)
		di->pdata->zero_table[i] = min + (i * diff);

	if (!dbg_enable)
		return;

	for (i = 0; i < ocv_size; i++)
		DBG("zero[%d] = %d\n", i, di->pdata->zero_table[i]);

	for (i = 0; i < ocv_size; i++)
		DBG("ocv[%d] = %d\n", i, di->pdata->ocv_table[i]);
}

static void rk816_bat_calc_sm_linek(struct rk816_battery *di)
{
	int linek, current_avg;
	u8 diff, delta;

	delta = abs(di->dsoc - di->rsoc);
	diff = delta * 3;/* speed:3/4 */
	current_avg = rk816_bat_get_avg_current(di);
	if (current_avg >= 0) {
		if (di->dsoc < di->rsoc)
			linek = 1000 * (delta + diff) / DIV(diff);
		else if (di->dsoc > di->rsoc)
			linek = 1000 * diff / DIV(delta + diff);
		else
			linek = 1000;
		di->dbg_meet_soc = (di->dsoc >= di->rsoc) ?
				   (di->dsoc + diff) : (di->rsoc + diff);
	} else {
		if (di->dsoc < di->rsoc)
			linek = -1000 * diff / DIV(delta + diff);
		else if (di->dsoc > di->rsoc)
			linek = -1000 * (delta + diff) / DIV(diff);
		else
			linek = -1000;
		di->dbg_meet_soc = (di->dsoc >= di->rsoc) ?
				   (di->dsoc - diff) : (di->rsoc - diff);
	}

	di->sm_linek = linek;
	di->sm_remain_cap = di->remain_cap;
	di->dbg_calc_dsoc = di->dsoc;
	di->dbg_calc_rsoc = di->rsoc;

	DBG("<%s>.diff=%d, k=%d, cur=%d\n", __func__, diff, linek, current_avg);
}

static void rk816_bat_calc_zero_linek(struct rk816_battery *di)
{
	int dead_voltage, ocv_voltage;
	int voltage_avg, current_avg, vsys;
	int ocv_cap, dead_cap, xsoc;
	int ocv_soc, dead_soc;
	int pwroff_vol, org_linek = 0;
	int min_gap_xsoc;

	if ((abs(di->current_avg) < 400) && (di->dsoc > 5))
		pwroff_vol = di->pdata->pwroff_vol + 50;
	else
		pwroff_vol = di->pdata->pwroff_vol;

	/* calc estimate ocv voltage */
	voltage_avg = rk816_bat_get_avg_voltage(di);
	current_avg = rk816_bat_get_avg_current(di);
	vsys = voltage_avg + (current_avg * DEF_PWRPATH_RES) / 1000;

	DBG("ZERO0: shtd_vol: org = %d, now = %d, zero_reserve_dsoc = %d\n",
	    di->pdata->pwroff_vol, pwroff_vol, di->pdata->zero_reserve_dsoc);

	dead_voltage = pwroff_vol - current_avg *
				(di->bat_res + DEF_PWRPATH_RES) / 1000;
	ocv_voltage = voltage_avg - (current_avg * di->bat_res) / 1000;
	DBG("ZERO0: dead_voltage(shtd) = %d, ocv_voltage(now) = %d\n",
	    dead_voltage, ocv_voltage);

	/* calc estimate soc and cap */
	dead_soc = rk816_bat_vol_to_zerosoc(di, dead_voltage);
	dead_cap = rk816_bat_vol_to_zerocap(di, dead_voltage);
	DBG("ZERO0: dead_soc = %d, dead_cap = %d\n",
	    dead_soc, dead_cap);

	ocv_soc = rk816_bat_vol_to_zerosoc(di, ocv_voltage);
	ocv_cap = rk816_bat_vol_to_zerocap(di, ocv_voltage);
	DBG("ZERO0: ocv_soc = %d, ocv_cap = %d\n",
	    ocv_soc, ocv_cap);

	/* xsoc: available rsoc */
	xsoc = ocv_soc - dead_soc;

	/* min_gap_xsoc: reserve xsoc */
	if (abs(current_avg) > ZERO_LOAD_LVL1)
		min_gap_xsoc = MIN_ZERO_GAP_XSOC3;
	else if (abs(current_avg) > ZERO_LOAD_LVL2)
		min_gap_xsoc = MIN_ZERO_GAP_XSOC2;
	else
		min_gap_xsoc = MIN_ZERO_GAP_XSOC1;

	if ((xsoc <= 30) && (di->dsoc >= di->pdata->zero_reserve_dsoc))
		min_gap_xsoc = min_gap_xsoc + MIN_ZERO_GAP_CALIB;

	di->zero_remain_cap = di->remain_cap;
	di->zero_timeout_cnt = 0;
	if ((di->dsoc <= 1) && (xsoc > 0)) {
		di->zero_linek = 400;
		di->zero_drop_sec = 0;
	} else if (xsoc >= 0) {
		di->zero_drop_sec = 0;
		di->zero_linek = (di->zero_dsoc + xsoc / 2) / DIV(xsoc);
		org_linek = di->zero_linek;
		/* battery energy mode to use up voltage */
		if ((di->pdata->energy_mode) &&
		    (xsoc - di->dsoc >= MIN_ZERO_GAP_XSOC3) &&
		    (di->dsoc <= 10) && (di->zero_linek < 300)) {
			di->zero_linek = 300;
			DBG("ZERO-new: zero_linek adjust step0...\n");
		/* reserve enough power yet, slow down any way */
		} else if ((xsoc - di->dsoc >= min_gap_xsoc) ||
			   ((xsoc - di->dsoc >= MIN_ZERO_GAP_XSOC2) &&
			    (di->dsoc <= 10) && (xsoc > 15))) {
			if (xsoc <= 20 &&
			    di->dsoc >= di->pdata->zero_reserve_dsoc)
				di->zero_linek = 1200;
			else if (xsoc - di->dsoc >= 2 * min_gap_xsoc)
				di->zero_linek = 400;
			else if (xsoc - di->dsoc >= 3 + min_gap_xsoc)
				di->zero_linek = 600;
			else
				di->zero_linek = 800;
			DBG("ZERO-new: zero_linek adjust step1...\n");
		/* control zero mode beginning enter */
		} else if ((di->zero_linek > 1800) && (di->dsoc > 70)) {
			di->zero_linek = 1800;
			DBG("ZERO-new: zero_linek adjust step2...\n");
		/* dsoc close to xsoc: it must reserve power */
		} else if ((di->zero_linek > 1000) && (di->zero_linek < 1200)) {
			di->zero_linek = 1200;
			DBG("ZERO-new: zero_linek adjust step3...\n");
		/* dsoc[5~15], dsoc < xsoc */
		} else if ((di->dsoc <= 15 && di->dsoc > 5) &&
			   (di->zero_linek <= 1200)) {
			/* slow down */
			if ((xsoc - di->dsoc) >= min_gap_xsoc)
				di->zero_linek = 800;
			/* reserve power */
			else
				di->zero_linek = 1200;
			DBG("ZERO-new: zero_linek adjust step4...\n");
		/* dsoc[5, 100], dsoc < xsoc */
		} else if ((di->zero_linek < 1000) && (di->dsoc >= 5)) {
			if ((xsoc - di->dsoc) < min_gap_xsoc) {
				/* reserve power */
				di->zero_linek = 1200;
			} else {
				if (abs(di->current_avg) > 500)/* heavy */
					di->zero_linek = 900;
				else
					di->zero_linek = 1000;
			}
			DBG("ZERO-new: zero_linek adjust step5...\n");
		/* dsoc[0~5], dsoc < xsoc */
		} else if ((di->zero_linek < 1000) && (di->dsoc <= 5)) {
			if ((xsoc - di->dsoc) <= 3)
				di->zero_linek = 1200;
			else
				di->zero_linek = 800;
			DBG("ZERO-new: zero_linek adjust step6...\n");
		}
	} else {
		/* xsoc < 0 */
		di->zero_linek = 1000;
		if (!di->zero_drop_sec)
			di->zero_drop_sec = get_boot_sec();
		if (base2sec(di->zero_drop_sec) >= WAIT_DSOC_DROP_SEC) {
			DBG("ZERO0: t=%lu\n", base2sec(di->zero_drop_sec));
			di->zero_drop_sec = 0;
			di->dsoc--;
			di->zero_dsoc = (di->dsoc + 1) * 1000 -
						MIN_ACCURACY;
		}
	}

	if (voltage_avg < pwroff_vol - 70) {
		if (!di->shtd_drop_sec)
			di->shtd_drop_sec = get_boot_sec();
		if (base2sec(di->shtd_drop_sec) > WAIT_SHTD_DROP_SEC) {
			BAT_INFO("voltage extreme low...soc:%d->0\n", di->dsoc);
			di->shtd_drop_sec = 0;
			di->dsoc = 0;
		}
	} else {
		di->shtd_drop_sec = 0;
	}

	DBG("ZERO-new: org_linek=%d, zero_linek=%d, dsoc=%d, Xsoc=%d, rsoc=%d, gap=%d, v=%d, vsys=%d\n"
	    "ZERO-new: di->zero_dsoc=%d, zero_remain_cap=%d, zero_drop=%ld, sht_drop=%ld\n\n",
	    org_linek, di->zero_linek, di->dsoc, xsoc, di->rsoc,
	    min_gap_xsoc, voltage_avg, vsys, di->zero_dsoc, di->zero_remain_cap,
	    base2sec(di->zero_drop_sec), base2sec(di->shtd_drop_sec));
}

static void rk816_bat_finish_algo_prepare(struct rk816_battery *di)
{
	di->chrg_finish_base = get_boot_sec();
	if (!di->chrg_finish_base)
		di->chrg_finish_base = 1;
}

static void rk816_bat_smooth_algo_prepare(struct rk816_battery *di)
{
	int tmp_soc;

	tmp_soc = di->sm_chrg_dsoc / 1000;
	if (tmp_soc != di->dsoc)
		di->sm_chrg_dsoc = di->dsoc * 1000;

	tmp_soc = di->sm_dischrg_dsoc / 1000;
	if (tmp_soc != di->dsoc)
		di->sm_dischrg_dsoc =
		(di->dsoc + 1) * 1000 - MIN_ACCURACY;

	DBG("<%s>. tmp_soc=%d, dsoc=%d, dsoc:sm_dischrg=%d, sm_chrg=%d\n",
	    __func__, tmp_soc, di->dsoc, di->sm_dischrg_dsoc, di->sm_chrg_dsoc);

	rk816_bat_calc_sm_linek(di);
}

static void rk816_bat_zero_algo_prepare(struct rk816_battery *di)
{
	int tmp_dsoc;

	di->zero_timeout_cnt = 0;
	tmp_dsoc = di->zero_dsoc / 1000;
	if (tmp_dsoc != di->dsoc)
		di->zero_dsoc = (di->dsoc + 1) * 1000 - MIN_ACCURACY;

	DBG("<%s>. first calc, reinit linek\n", __func__);

	rk816_bat_calc_zero_linek(di);
}

static void rk816_bat_calc_zero_algorithm(struct rk816_battery *di)
{
	int tmp_soc = 0, sm_delta_dsoc = 0;

	tmp_soc = di->zero_dsoc / 1000;
	if (tmp_soc == di->dsoc)
		goto out;

	DBG("<%s>. enter: dsoc=%d, rsoc=%d\n", __func__, di->dsoc, di->rsoc);
	/* when discharge slow down, take sm chrg into calc */
	if (di->dsoc < di->rsoc) {
		/* take sm charge rest into calc */
		tmp_soc = di->sm_chrg_dsoc / 1000;
		if (tmp_soc == di->dsoc) {
			sm_delta_dsoc = di->sm_chrg_dsoc - di->dsoc * 1000;
			di->sm_chrg_dsoc = di->dsoc * 1000;
			di->zero_dsoc += sm_delta_dsoc;
			DBG("ZERO1: take sm chrg,delta=%d\n", sm_delta_dsoc);
		}
	}

	/* when discharge speed up, take sm dischrg into calc */
	if (di->dsoc > di->rsoc) {
		/* take sm discharge rest into calc */
		tmp_soc = di->sm_dischrg_dsoc / 1000;
		if (tmp_soc == di->dsoc) {
			sm_delta_dsoc = di->sm_dischrg_dsoc -
				((di->dsoc + 1) * 1000 - MIN_ACCURACY);
			di->sm_dischrg_dsoc = (di->dsoc + 1) * 1000 -
								MIN_ACCURACY;
			di->zero_dsoc += sm_delta_dsoc;
			DBG("ZERO1: take sm dischrg,delta=%d\n", sm_delta_dsoc);
		}
	}

	/* check overflow */
	if (di->zero_dsoc > (di->dsoc + 1) * 1000 - MIN_ACCURACY) {
		DBG("ZERO1: zero dsoc overflow: %d\n", di->zero_dsoc);
		di->zero_dsoc = (di->dsoc + 1) * 1000 - MIN_ACCURACY;
	}

	/* check new dsoc */
	tmp_soc = di->zero_dsoc / 1000;
	if (tmp_soc != di->dsoc) {
		/* avoid dsoc jump when heavy load */
		if ((di->dsoc - tmp_soc) > 1) {
			di->dsoc--;
			di->zero_dsoc = (di->dsoc + 1) * 1000 - MIN_ACCURACY;
			DBG("ZERO1: heavy load...\n");
		} else {
			di->dsoc = tmp_soc;
		}
		di->zero_drop_sec = 0;
	}

out:
	DBG("ZERO1: zero_dsoc(Y0)=%d, dsoc=%d, rsoc=%d, tmp_soc=%d\n",
	    di->zero_dsoc, di->dsoc, di->rsoc, tmp_soc);
	DBG("ZERO1: sm_dischrg_dsoc=%d, sm_chrg_dsoc=%d\n",
	    di->sm_dischrg_dsoc, di->sm_chrg_dsoc);
}

static void rk816_bat_zero_algorithm(struct rk816_battery *di)
{
	int delta_cap = 0, delta_soc = 0;

	di->zero_timeout_cnt++;
	delta_cap = di->zero_remain_cap - di->remain_cap;
	delta_soc = di->zero_linek * (delta_cap * 100) / DIV(di->fcc);

	DBG("ZERO1: zero_linek=%d, zero_dsoc(Y0)=%d, dsoc=%d, rsoc=%d\n"
	    "ZERO1: delta_soc(X0)=%d, delta_cap=%d, zero_remain_cap = %d\n"
	    "ZERO1: timeout_cnt=%d, sm_dischrg=%d, sm_chrg=%d\n\n",
	    di->zero_linek, di->zero_dsoc, di->dsoc, di->rsoc,
	    delta_soc, delta_cap, di->zero_remain_cap,
	    di->zero_timeout_cnt, di->sm_dischrg_dsoc, di->sm_chrg_dsoc);

	if ((delta_soc >= MIN_ZERO_DSOC_ACCURACY) ||
	    (di->zero_timeout_cnt > MIN_ZERO_OVERCNT) ||
	    (di->zero_linek == 0)) {
		DBG("ZERO1:--------- enter calc -----------\n");
		di->zero_timeout_cnt = 0;
		di->zero_dsoc -= delta_soc;
		rk816_bat_calc_zero_algorithm(di);
		rk816_bat_calc_zero_linek(di);
	}
}

static void rk816_bat_dump_time_table(struct rk816_battery *di)
{
	u8 i;
	static int old_index;
	static int old_min;
	u32 time;
	int mod = di->dsoc % 10;
	int index = di->dsoc / 10;

	if (rk816_bat_chrg_online(di))
		time = base2min(di->plug_in_base);
	else
		time = base2min(di->plug_out_base);

	if ((mod == 0) && (index > 0) && (old_index != index)) {
		di->dbg_chrg_min[index - 1] = time - old_min;
		old_min = time;
		old_index = index;
	}

	for (i = 1; i < 11; i++)
		DBG("Time[%d]=%d, ", (i * 10), di->dbg_chrg_min[i - 1]);
	DBG("\n");
}

static void rk816_bat_debug_info(struct rk816_battery *di)
{
	u8 sup_tst, ggcon, ggsts, vb_mod, ts_ctrl, reboot_cnt;
	u8 usb_ctrl, chrg_ctrl1, thermal;
	u8 int_sts1, int_sts2, int_sts3;
	u8 int_msk1, int_msk2, int_msk3;
	u8 chrg_ctrl2, chrg_ctrl3, rtc, misc, dcdc_en2;
	u32 chrg_sel;
	const char *work_mode[] = {"ZERO", "FINISH", "UN", "UN", "SMOOTH"};
	const char *bat_mode[] = {"BAT", "VIRTUAL"};

	if (rk816_bat_chrg_online(di))
		di->plug_out_base = get_boot_sec();
	else
		di->plug_in_base = get_boot_sec();

	rk816_bat_dump_time_table(di);

	if (!dbg_enable)
		return;

	reboot_cnt = rk816_bat_read(di, RK816_REBOOT_CNT_REG);
	ts_ctrl = rk816_bat_read(di, RK816_TS_CTRL_REG);
	misc = rk816_bat_read(di, RK816_MISC_MARK_REG);
	ggcon = rk816_bat_read(di, RK816_GGCON_REG);
	ggsts = rk816_bat_read(di, RK816_GGSTS_REG);
	sup_tst = rk816_bat_read(di, RK816_SUP_STS_REG);
	vb_mod = rk816_bat_read(di, RK816_VB_MON_REG);
	usb_ctrl = rk816_bat_read(di, RK816_USB_CTRL_REG);
	chrg_ctrl1 = rk816_bat_read(di, RK816_CHRG_CTRL_REG1);
	chrg_ctrl2 = rk816_bat_read(di, RK816_CHRG_CTRL_REG2);
	chrg_ctrl3 = rk816_bat_read(di, RK816_CHRG_CTRL_REG3);
	rtc = rk816_bat_read(di, RK808_SECONDS_REG);
	thermal = rk816_bat_read(di, RK816_THERMAL_REG);
	int_sts1 = rk816_bat_read(di, RK816_INT_STS_REG1);
	int_sts2 = rk816_bat_read(di, RK816_INT_STS_REG2);
	int_sts3 = rk816_bat_read(di, RK816_INT_STS_REG3);
	int_msk1 = rk816_bat_read(di, RK816_INT_STS_MSK_REG1);
	int_msk2 = rk816_bat_read(di, RK816_INT_STS_MSK_REG2);
	int_msk3 = rk816_bat_read(di, RK816_INT_STS_MSK_REG3);
	dcdc_en2 = rk816_bat_read(di, RK816_DCDC_EN_REG2);
	chrg_sel = CHRG_CUR_SEL[chrg_ctrl1 & 0x0f];
	if (!di->over_20mR)
		chrg_sel = RES_FAC_MUX(chrg_sel, di->res_fac);
	else
		chrg_sel = RES_FAC_DIV(chrg_sel, di->res_fac);

	DBG("\n------- DEBUG REGS, [Ver: %s] -------------------\n"
	    "GGCON=0x%2x, GGSTS=0x%2x, RTC=0x%2x, DCDC_EN2=0x%2x\n"
	    "SUP_STS= 0x%2x, VB_MOD=0x%2x, USB_CTRL=0x%2x\n"
	    "THERMAL=0x%2x, MISC_MARK=0x%2x, TS_CTRL=0x%2x\n"
	    "CHRG_CTRL:REG1=0x%2x, REG2=0x%2x, REG3=0x%2x\n"
	    "INT_STS:  REG1=0x%2x, REG2=0x%2x, REG3=0x%2x\n"
	    "INT_MSK:  REG1=0x%2x, REG2=0x%2x, REG3=0x%2x\n",
	    DRIVER_VERSION, ggcon, ggsts, rtc, dcdc_en2,
	    sup_tst, vb_mod, usb_ctrl,
	    thermal, misc, ts_ctrl,
	    chrg_ctrl1, chrg_ctrl2, chrg_ctrl3,
	    int_sts1, int_sts2, int_sts3,
	    int_msk1, int_msk2, int_msk3
	   );

	DBG("###############################################################\n"
	    "Dsoc=%d, Rsoc=%d, Vavg=%d, Iavg=%d, Cap=%d, Fcc=%d, d=%d\n"
	    "K=%d, Mode=%s, Oldcap=%d, Is=%d, Ip=%d, Vs=%d, Vusb=%d\n"
	    "AC=%d, USB=%d, DC=%d, OTG=%d, 5V=%d, PROP=%d, Tfb=%d, Tbat=%d\n"
	    "off:i=0x%x, c=0x%x, p=%d, Rbat=%d, age_ocv_cap=%d, fb=%d, hot=%d\n"
	    "adp:in=%lu, out=%lu, finish=%lu, LFcc=%d, boot_min=%lu, sleep_min=%lu, adc=%d, Rfac=%d\n"
	    "bat:%s, meet: soc=%d, calc: dsoc=%d, rsoc=%d, Vocv=%d, Rsam=%d\n"
	    "pwr: dsoc=%d, rsoc=%d, vol=%d, halt: st=%d, cnt=%d, reboot=%d\n"
	    "ocv_c=%d: %d -> %d; max_c=%d: %d -> %d; force_c=%d: %d -> %d\n"
	    "min=%d, init=%d, sw=%d, below0=%d, first=%d, changed=%d\n"
	    "###############################################################\n",
	    di->dsoc, di->rsoc, di->voltage_avg, di->current_avg,
	    di->remain_cap, di->fcc, di->dsoc - di->rsoc,
	    di->sm_linek, work_mode[di->work_mode], di->sm_remain_cap,
	    chrg_sel,
	    CHRG_CUR_INPUT[usb_ctrl & 0x0f],
	    CHRG_VOL_SEL[(chrg_ctrl1 & 0x70) >> 4],
	    rk816_bat_get_usb_voltage(di),
	    di->ac_in, di->usb_in, di->dc_in, di->otg_in, di->otg_pmic5v,
	    di->prop_status,
	    FEED_BACK_TEMP[(thermal & 0x0c) >> 2], di->temperature,
	    rk816_bat_get_ioffset(di), rk816_bat_get_coffset(di),
	    di->poffset, di->bat_res, di->age_adjust_cap, di->fb_blank,
	    !!(thermal & HOTDIE_STS),
	    base2min(di->plug_in_base), base2min(di->plug_out_base),
	    base2min(di->chrg_finish_base), di->lock_fcc,
	    base2min(di->boot_base), di->sleep_sum_sec / 60,
	    di->adc_allow_update, di->res_fac,
	    bat_mode[di->pdata->bat_mode], di->dbg_meet_soc,
	    di->dbg_calc_dsoc, di->dbg_calc_rsoc, di->voltage_ocv,
	    di->pdata->sample_res,
	    di->dbg_pwr_dsoc, di->dbg_pwr_rsoc, di->dbg_pwr_vol, di->is_halt,
	    di->halt_cnt, reboot_cnt,
	    di->is_ocv_calib, di->ocv_pre_dsoc, di->ocv_new_dsoc,
	    di->is_max_soc_offset, di->max_pre_dsoc, di->max_new_dsoc,
	    di->is_force_calib, di->force_pre_dsoc, di->force_new_dsoc,
	    di->pwroff_min, di->is_initialized, di->is_sw_reset,
	    di->dbg_cap_low0, di->is_first_on, di->last_dsoc
	   );
}

static void rk816_bat_init_capacity(struct rk816_battery *di, u32 cap)
{
	int delta_cap;

	delta_cap = cap - di->remain_cap;
	if (!delta_cap)
		return;

	di->age_adjust_cap += delta_cap;
	rk816_bat_init_coulomb_cap(di, cap);
	rk816_bat_smooth_algo_prepare(di);
	rk816_bat_zero_algo_prepare(di);
}

static void rk816_bat_update_age_fcc(struct rk816_battery *di)
{
	int fcc;
	int remain_cap;
	int age_keep_min;

	di->lock_fcc = rk816_bat_get_lock_fcc(di);
	if (di->lock_fcc == 0)
		return;

	fcc = di->lock_fcc;
	remain_cap = fcc - di->age_ocv_cap - di->age_adjust_cap;
	age_keep_min = base2min(di->age_keep_sec);

	DBG("%s: lock_fcc=%d, age_ocv_cap=%d, age_adjust_cap=%d, remain_cap=%d, age_allow_update=%d, age_keep_min=%d\n",
	    __func__, fcc, di->age_ocv_cap, di->age_adjust_cap, remain_cap,
	    di->age_allow_update, age_keep_min);

	if ((di->chrg_status == CHARGE_FINISH) && (di->age_allow_update) &&
	    (age_keep_min < 1200)) {
		di->age_allow_update = false;
		fcc = remain_cap * 100 / DIV(100 - di->age_ocv_soc);
		BAT_INFO("lock_fcc=%d, calc_cap=%d, age: soc=%d, cap=%d, level=%d, fcc:%d->%d?\n",
			 di->lock_fcc, remain_cap, di->age_ocv_soc,
			 di->age_ocv_cap, di->age_level, di->fcc, fcc);

		if ((fcc < di->qmax) && (fcc > MIN_FCC)) {
			BAT_INFO("fcc:%d->%d!\n", di->fcc, fcc);
			di->fcc = fcc;
			rk816_bat_init_capacity(di, di->fcc);
			rk816_bat_save_fcc(di, di->fcc);
			rk816_bat_save_age_level(di, di->age_level);
		}
	}
}

static void rk816_bat_wait_finish_sig(struct rk816_battery *di)
{
	int chrg_finish_vol = di->pdata->max_chrg_voltage;

	if (!rk816_bat_chrg_online(di))
		return;

	if ((di->chrg_status == CHARGE_FINISH) &&
	    (!is_rk816_bat_st_cvtlim(di)) &&
	    (di->voltage_avg > chrg_finish_vol - 150) && di->adc_allow_update) {
		rk816_bat_update_age_fcc(di);/* save new fcc*/
		if (rk816_bat_adc_calib(di))
			di->adc_allow_update = false;
	}
}

static void rk816_bat_finish_algorithm(struct rk816_battery *di)
{
	unsigned long finish_sec, soc_sec;
	int plus_soc, finish_current, rest = 0;

	/* rsoc */
	if ((di->remain_cap != di->fcc) &&
	    (rk816_bat_get_chrg_status(di) == CHARGE_FINISH)) {
		di->age_adjust_cap += (di->fcc - di->remain_cap);
		rk816_bat_init_coulomb_cap(di, di->fcc);
	}

	/* dsoc */
	if (di->dsoc < 100) {
		if (!di->chrg_finish_base)
			di->chrg_finish_base = get_boot_sec();

		finish_current = (di->rsoc - di->dsoc) >  FINISH_MAX_SOC_DELAY ?
					FINISH_CHRG_CUR2 : FINISH_CHRG_CUR1;
		finish_sec = base2sec(di->chrg_finish_base);
		soc_sec = di->fcc * 3600 / 100 / DIV(finish_current);
		plus_soc = finish_sec / DIV(soc_sec);
		if (finish_sec > soc_sec) {
			rest = finish_sec % soc_sec;
			di->dsoc += plus_soc;
			di->chrg_finish_base = get_boot_sec();
			if (di->chrg_finish_base > rest)
				di->chrg_finish_base = get_boot_sec() - rest;
		}
		DBG("<%s>.CHARGE_FINISH:dsoc<100,dsoc=%d\n"
		    "soc_time=%lu, sec_finish=%lu, plus_soc=%d, rest=%d\n",
		    __func__, di->dsoc, soc_sec, finish_sec, plus_soc, rest);
	}
}

static void rk816_bat_calc_smooth_dischrg(struct rk816_battery *di)
{
	int tmp_soc = 0, sm_delta_dsoc = 0, zero_delta_dsoc = 0;

	tmp_soc = di->sm_dischrg_dsoc / 1000;
	if (tmp_soc == di->dsoc)
		goto out;

	DBG("<%s>. enter: dsoc=%d, rsoc=%d\n", __func__, di->dsoc, di->rsoc);
	/* when dischrge slow down, take sm charge rest into calc */
	if (di->dsoc < di->rsoc) {
		tmp_soc = di->sm_chrg_dsoc / 1000;
		if (tmp_soc == di->dsoc) {
			sm_delta_dsoc = di->sm_chrg_dsoc - di->dsoc * 1000;
			di->sm_chrg_dsoc = di->dsoc * 1000;
			di->sm_dischrg_dsoc += sm_delta_dsoc;
			DBG("<%s>. take sm dischrg, delta=%d\n",
			    __func__, sm_delta_dsoc);
		}
	}

	/* when discharge speed up, take zero discharge rest into calc */
	if (di->dsoc > di->rsoc) {
		tmp_soc = di->zero_dsoc / 1000;
		if (tmp_soc == di->dsoc) {
			zero_delta_dsoc = di->zero_dsoc - ((di->dsoc + 1) *
						1000 - MIN_ACCURACY);
			di->zero_dsoc = (di->dsoc + 1) * 1000 - MIN_ACCURACY;
			di->sm_dischrg_dsoc += zero_delta_dsoc;
			DBG("<%s>. take zero schrg, delta=%d\n",
			    __func__, zero_delta_dsoc);
		}
	}

	/* check up overflow */
	if ((di->sm_dischrg_dsoc) > ((di->dsoc + 1) * 1000 - MIN_ACCURACY)) {
		DBG("<%s>. dischrg_dsoc up overflow\n", __func__);
		di->sm_dischrg_dsoc = (di->dsoc + 1) *
					1000 - MIN_ACCURACY;
	}

	/* check new dsoc */
	tmp_soc = di->sm_dischrg_dsoc / 1000;
	if (tmp_soc != di->dsoc) {
		di->dsoc = tmp_soc;
		di->sm_chrg_dsoc = di->dsoc * 1000;
	}
out:
	DBG("<%s>. dsoc=%d, rsoc=%d, dsoc:sm_dischrg=%d, sm_chrg=%d, zero=%d\n",
	    __func__, di->dsoc, di->rsoc, di->sm_dischrg_dsoc, di->sm_chrg_dsoc,
	    di->zero_dsoc);
}

static void rk816_bat_calc_smooth_chrg(struct rk816_battery *di)
{
	int tmp_soc = 0, sm_delta_dsoc = 0, zero_delta_dsoc = 0;

	tmp_soc = di->sm_chrg_dsoc / 1000;
	if (tmp_soc == di->dsoc)
		goto out;

	DBG("<%s>. enter: dsoc=%d, rsoc=%d\n", __func__, di->dsoc, di->rsoc);
	/* when charge slow down, take zero & sm dischrg into calc */
	if (di->dsoc > di->rsoc) {
		/* take sm discharge rest into calc */
		tmp_soc = di->sm_dischrg_dsoc / 1000;
		if (tmp_soc == di->dsoc) {
			sm_delta_dsoc = di->sm_dischrg_dsoc -
					((di->dsoc + 1) * 1000 - MIN_ACCURACY);
			di->sm_dischrg_dsoc = (di->dsoc + 1) * 1000 -
							MIN_ACCURACY;
			di->sm_chrg_dsoc += sm_delta_dsoc;
			DBG("<%s>. take sm dischrg, delta=%d\n",
			    __func__, sm_delta_dsoc);
		}

		/* take zero discharge rest into calc */
		tmp_soc = di->zero_dsoc / 1000;
		if (tmp_soc == di->dsoc) {
			zero_delta_dsoc = di->zero_dsoc -
			((di->dsoc + 1) * 1000 - MIN_ACCURACY);
			di->zero_dsoc = (di->dsoc + 1) * 1000 - MIN_ACCURACY;
			di->sm_chrg_dsoc += zero_delta_dsoc;
			DBG("<%s>. take zero dischrg, delta=%d\n",
			    __func__, zero_delta_dsoc);
		}
	}

	/* check down overflow */
	if (di->sm_chrg_dsoc < di->dsoc * 1000) {
		DBG("<%s>. chrg_dsoc down overflow\n", __func__);
		di->sm_chrg_dsoc = di->dsoc * 1000;
	}

	/* check new dsoc */
	tmp_soc = di->sm_chrg_dsoc / 1000;
	if (tmp_soc != di->dsoc) {
		di->dsoc = tmp_soc;
		di->sm_dischrg_dsoc = (di->dsoc + 1) * 1000 - MIN_ACCURACY;
	}
out:
	DBG("<%s>.dsoc=%d, rsoc=%d, dsoc: sm_dischrg=%d, sm_chrg=%d, zero=%d\n",
	    __func__, di->dsoc, di->rsoc, di->sm_dischrg_dsoc, di->sm_chrg_dsoc,
	    di->zero_dsoc);
}

static void rk816_bat_smooth_algorithm(struct rk816_battery *di)
{
	int ydsoc = 0, delta_cap = 0, old_cap = 0;
	unsigned long tgt_sec = 0;

	di->remain_cap = rk816_bat_get_coulomb_cap(di);

	/* full charge: slow down */
	if ((di->dsoc == 99) && (di->chrg_status == CC_OR_CV) &&
	    (di->current_avg > 0)) {
		di->sm_linek = FULL_CHRG_K;
	/* terminal charge, slow down */
	} else if ((di->current_avg >= TERM_CHRG_CURR) &&
	    (di->chrg_status == CC_OR_CV) && (di->dsoc >= TERM_CHRG_DSOC)) {
		di->sm_linek = TERM_CHRG_K;
		DBG("<%s>. terminal mode..\n", __func__);
	/* simulate charge, speed up */
	} else if ((di->current_avg <= SIMULATE_CHRG_CURR) &&
		   (di->current_avg > 0) && (di->chrg_status == CC_OR_CV) &&
		   (di->dsoc < TERM_CHRG_DSOC) &&
		   ((di->rsoc - di->dsoc) >= SIMULATE_CHRG_INTV)) {
		di->sm_linek = SIMULATE_CHRG_K;
		DBG("<%s>. simulate mode..\n", __func__);
	} else {
		/* charge and discharge switch */
		if ((di->sm_linek * di->current_avg <= 0) ||
		    (di->sm_linek == TERM_CHRG_K) ||
		    (di->sm_linek == FULL_CHRG_K) ||
		    (di->sm_linek == SIMULATE_CHRG_K)) {
			DBG("<%s>. linek mode, retinit sm linek..\n", __func__);
			rk816_bat_calc_sm_linek(di);
		}
	}

	old_cap = di->sm_remain_cap;
	/*
	 * when dsoc equal rsoc(not include full, term, simulate case),
	 * sm_linek should change to -1000/1000 smoothly to avoid dsoc+1/-1
	 * right away, so change it after flat seconds
	 */
	if ((di->dsoc == di->rsoc) && (abs(di->sm_linek) != 1000) &&
	    (di->sm_linek != FULL_CHRG_K && di->sm_linek != TERM_CHRG_K &&
	     di->sm_linek != SIMULATE_CHRG_K)) {
		if (!di->flat_match_sec)
			di->flat_match_sec = get_boot_sec();
		tgt_sec = di->fcc * 3600 / 100 / DIV(abs(di->current_avg)) / 3;
		if (base2sec(di->flat_match_sec) >= tgt_sec) {
			di->flat_match_sec = 0;
			di->sm_linek = (di->current_avg >= 0) ? 1000 : -1000;
		}
		DBG("<%s>. flat_sec=%ld, tgt_sec=%ld, sm_k=%d\n", __func__,
		    base2sec(di->flat_match_sec), tgt_sec, di->sm_linek);
	} else {
		di->flat_match_sec = 0;
	}

	/* abs(k)=1000 or dsoc=100, stop calc */
	if ((abs(di->sm_linek) == 1000) || (di->current_avg >= 0 &&
	     di->chrg_status == CC_OR_CV && di->dsoc >= 100)) {
		DBG("<%s>. sm_linek=%d\n", __func__, di->sm_linek);
		if (abs(di->sm_linek) == 1000) {
			di->dsoc = di->rsoc;
			di->sm_linek = (di->sm_linek > 0) ? 1000 : -1000;
			DBG("<%s>. dsoc == rsoc, sm_linek=%d\n",
			    __func__, di->sm_linek);
		}
		di->sm_remain_cap = di->remain_cap;
		di->sm_chrg_dsoc = di->dsoc * 1000;
		di->sm_dischrg_dsoc = (di->dsoc + 1) * 1000 - MIN_ACCURACY;
		DBG("<%s>. sm_dischrg_dsoc=%d, sm_chrg_dsoc=%d\n",
		    __func__, di->sm_dischrg_dsoc, di->sm_chrg_dsoc);
	} else {
		delta_cap = di->remain_cap - di->sm_remain_cap;
		if (delta_cap == 0) {
			DBG("<%s>. delta_cap = 0\n", __func__);
			return;
		}
		ydsoc = di->sm_linek * abs(delta_cap) * 100 / DIV(di->fcc);
		if (ydsoc == 0) {
			DBG("<%s>. ydsoc = 0\n", __func__);
			return;
		}
		di->sm_remain_cap = di->remain_cap;

		DBG("<%s>. k=%d, ydsoc=%d; cap:old=%d, new:%d; delta_cap=%d\n",
		    __func__, di->sm_linek, ydsoc, old_cap,
		    di->sm_remain_cap, delta_cap);

		/* discharge mode */
		if (ydsoc < 0) {
			di->sm_dischrg_dsoc += ydsoc;
			rk816_bat_calc_smooth_dischrg(di);
		/* charge mode */
		} else {
			di->sm_chrg_dsoc += ydsoc;
			rk816_bat_calc_smooth_chrg(di);
		}

		if (di->s2r) {
			di->s2r = false;
			rk816_bat_calc_sm_linek(di);
		}
	}
}

static bool rk816_bat_fake_finish_mode(struct rk816_battery *di)
{
	if ((di->rsoc == 100) && (rk816_bat_get_chrg_status(di) == CC_OR_CV) &&
	    (abs(di->current_avg) <= 100))
		return true;
	else
		return false;
}

static void rk816_bat_display_smooth(struct rk816_battery *di)
{
	/* discharge: reinit "zero & smooth" algorithm to avoid handling dsoc */
	if (di->s2r && !di->sleep_chrg_online) {
		DBG("s2r: discharge, reset algorithm...\n");
		di->s2r = false;
		rk816_bat_zero_algo_prepare(di);
		rk816_bat_smooth_algo_prepare(di);
		return;
	}

	if (di->work_mode == MODE_FINISH) {
		DBG("step1: charge finish...\n");
		rk816_bat_finish_algorithm(di);
		if ((rk816_bat_get_chrg_status(di) != CHARGE_FINISH) &&
		    !rk816_bat_fake_finish_mode(di)) {
			if ((di->current_avg < 0) &&
			    (di->voltage_avg < di->pdata->zero_algorithm_vol)) {
				DBG("step1: change to zero mode...\n");
				rk816_bat_zero_algo_prepare(di);
				di->work_mode = MODE_ZERO;
			} else {
				DBG("step1: change to smooth mode...\n");
				rk816_bat_smooth_algo_prepare(di);
				di->work_mode = MODE_SMOOTH;
			}
		}
	} else if (di->work_mode == MODE_ZERO) {
		DBG("step2: zero algorithm...\n");
		rk816_bat_zero_algorithm(di);
		if ((di->voltage_avg >= di->pdata->zero_algorithm_vol + 50) ||
		    (di->current_avg >= 0)) {
			DBG("step2: change to smooth mode...\n");
			rk816_bat_smooth_algo_prepare(di);
			di->work_mode = MODE_SMOOTH;
		} else if ((rk816_bat_get_chrg_status(di) == CHARGE_FINISH) ||
			   rk816_bat_fake_finish_mode(di)) {
			DBG("step2: change to finish mode...\n");
			rk816_bat_finish_algo_prepare(di);
			di->work_mode = MODE_FINISH;
		}
	} else {
		DBG("step3: smooth algorithm...\n");
		rk816_bat_smooth_algorithm(di);
		if ((di->current_avg < 0) &&
		    (di->voltage_avg < di->pdata->zero_algorithm_vol)) {
			DBG("step3: change to zero mode...\n");
			rk816_bat_zero_algo_prepare(di);
			di->work_mode = MODE_ZERO;
		} else if ((rk816_bat_get_chrg_status(di) == CHARGE_FINISH) ||
			   rk816_bat_fake_finish_mode(di)) {
			DBG("step3: change to finish mode...\n");
			rk816_bat_finish_algo_prepare(di);
			di->work_mode = MODE_FINISH;
		}
	}
}

static void rk816_bat_relax_vol_calib(struct rk816_battery *di)
{
	int soc, cap, vol;

	vol = di->voltage_relax - (di->current_relax * di->bat_res) / 1000;
	soc = rk816_bat_vol_to_ocvsoc(di, vol);
	cap = rk816_bat_vol_to_ocvcap(di, vol);
	rk816_bat_init_capacity(di, cap);
	BAT_INFO("sleep ocv calib: rsoc=%d, cap=%d\n", soc, cap);
}

static void rk816_bat_relife_age_flag(struct rk816_battery *di)
{
	u8 ocv_soc, ocv_cap, soc_level;

	if (di->voltage_relax <= 0)
		return;

	ocv_soc = rk816_bat_vol_to_ocvsoc(di, di->voltage_relax);
	ocv_cap = rk816_bat_vol_to_ocvcap(di, di->voltage_relax);
	DBG("<%s>. ocv_soc=%d, min=%lu, vol=%d\n", __func__,
	    ocv_soc, di->sleep_dischrg_sec / 60, di->voltage_relax);

	/* sleep enough time and ocv_soc enough low */
	if (!di->age_allow_update && ocv_soc <= 10) {
		di->age_voltage = di->voltage_relax;
		di->age_ocv_cap = ocv_cap;
		di->age_ocv_soc = ocv_soc;
		di->age_adjust_cap = 0;

		if (ocv_soc <= 1)
			di->age_level = 100;
		else if (ocv_soc < 5)
			di->age_level = 90;
		else
			di->age_level = 80;

		soc_level = rk816_bat_get_age_level(di);
		if (soc_level > di->age_level) {
			di->age_allow_update = false;
		} else {
			di->age_allow_update = true;
			di->age_keep_sec = get_boot_sec();
		}

		BAT_INFO("resume: age_vol:%d, age_ocv_cap:%d, age_ocv_soc:%d, age_soc_level:%d, age_allow_update:%d, age_level:%d\n",
			 di->age_voltage, di->age_ocv_cap, ocv_soc, soc_level,
			 di->age_allow_update, di->age_level);
	}
}

static int rk816_bat_sleep_dischrg(struct rk816_battery *di)
{
	bool ocv_soc_updated = false;
	int tgt_dsoc, gap_soc, sleep_soc = 0;
	int pwroff_vol = di->pdata->pwroff_vol;
	unsigned long sleep_sec = di->sleep_dischrg_sec;

	DBG("<%s>. enter: dsoc=%d, rsoc=%d, rv=%d, v=%d, sleep_min=%lu\n",
	    __func__, di->dsoc, di->rsoc, di->voltage_relax,
	    di->voltage_avg, sleep_sec / 60);

	if (di->voltage_relax >= di->voltage_avg) {
		rk816_bat_relax_vol_calib(di);
		rk816_bat_restart_relax(di);
		rk816_bat_relife_age_flag(di);
		ocv_soc_updated = true;
	}

	/*handle dsoc*/
	if (di->dsoc <= di->rsoc) {
		di->sleep_sum_cap = (SLP_CURR_MIN * sleep_sec / 3600);
		sleep_soc = di->sleep_sum_cap * 100 / DIV(di->fcc);
		tgt_dsoc = di->dsoc - sleep_soc;
		if (sleep_soc > 0) {
			BAT_INFO("calib0: rl=%d, dl=%d, intval=%d\n",
				 di->rsoc, di->dsoc, sleep_soc);
			if (di->dsoc < 5) {
				di->dsoc--;
			} else if ((tgt_dsoc < 5) && (di->dsoc >= 5)) {
				if (di->dsoc == 5)
					di->dsoc--;
				else
					di->dsoc = 5;
			} else if (tgt_dsoc > 5) {
				di->dsoc = tgt_dsoc;
			}
		}

		DBG("%s: dsoc<=rsoc, sum_cap=%d==>sleep_soc=%d, tgt_dsoc=%d\n",
		    __func__, di->sleep_sum_cap, sleep_soc, tgt_dsoc);
	} else {
		/*di->dsoc > di->rsoc*/
		di->sleep_sum_cap = (SLP_CURR_MAX * sleep_sec / 3600);
		sleep_soc = di->sleep_sum_cap / DIV(di->fcc / 100);
		gap_soc = di->dsoc - di->rsoc;

		BAT_INFO("calib1: rsoc=%d, dsoc=%d, intval=%d\n",
			 di->rsoc, di->dsoc, sleep_soc);
		if (gap_soc > sleep_soc) {
			if ((gap_soc - 5) > (sleep_soc * 2))
				di->dsoc -= (sleep_soc * 2);
			else
				di->dsoc -= sleep_soc;
		} else {
			di->dsoc = di->rsoc;
		}

		DBG("%s: dsoc>rsoc, sum_cap=%d=>sleep_soc=%d, gap_soc=%d\n",
		    __func__, di->sleep_sum_cap, sleep_soc, gap_soc);
	}

	if (di->voltage_avg <= pwroff_vol - 70) {
		di->dsoc = 0;
		rk_send_wakeup_key();
		BAT_INFO("low power sleeping, shutdown... %d\n", di->dsoc);
	}

	if (ocv_soc_updated && sleep_soc && (di->rsoc - di->dsoc) < 5 &&
	    di->dsoc < 40) {
		di->dsoc--;
		BAT_INFO("low power sleeping, reserved... %d\n", di->dsoc);
	}

	if (di->dsoc <= 0) {
		di->dsoc = 0;
		rk_send_wakeup_key();
		BAT_INFO("sleep dsoc is %d...\n", di->dsoc);
	}

	DBG("<%s>. out: dsoc=%d, rsoc=%d, sum_cap=%d\n",
	    __func__, di->dsoc, di->rsoc, di->sleep_sum_cap);

	return sleep_soc;
}

static void rk816_bat_power_supply_changed(struct rk816_battery *di)
{
	u8 status, thermal;
	static int old_soc = -1;

	/* check dsoc */
	if (di->dsoc > 100)
		di->dsoc = 100;
	else if (di->dsoc < 0)
		di->dsoc = 0;

	/* update prop and leds */
	if (rk816_bat_chrg_online(di)) {
		if (di->dsoc == 100)
			di->prop_status = POWER_SUPPLY_STATUS_FULL;
		else
			di->prop_status = POWER_SUPPLY_STATUS_CHARGING;
		rk816_bat_update_leds(di, di->prop_status);
	}

	if (di->dsoc == old_soc)
		return;

	/* report changed dsoc */
	thermal = rk816_bat_read(di, RK816_THERMAL_REG);
	status = rk816_bat_read(di, RK816_SUP_STS_REG);
	status = (status & CHRG_STATUS_MSK) >> 4;
	old_soc = di->dsoc;
	di->last_dsoc = di->dsoc;
	power_supply_changed(di->bat);
	BAT_INFO("changed: dsoc=%d, rsoc=%d, v=%d, ov=%d c=%d, cap=%d, f=%d, st=%s, hotdie=%d\n",
		 di->dsoc, di->rsoc, di->voltage_avg, di->voltage_ocv,
		 di->current_avg, di->remain_cap, di->fcc, bat_status[status],
		 !!(thermal & HOTDIE_STS));

	BAT_INFO("dl=%d, rl=%d, v=%d, halt=%d, halt_n=%d, max=%d, init=%d, sw=%d, calib=%d, below0=%d, force=%d\n",
		 di->dbg_pwr_dsoc, di->dbg_pwr_rsoc, di->dbg_pwr_vol,
		 di->is_halt, di->halt_cnt, di->is_max_soc_offset,
		 di->is_initialized, di->is_sw_reset, di->is_ocv_calib,
		 di->dbg_cap_low0, di->is_force_calib);
}

static u8 rk816_bat_check_reboot(struct rk816_battery *di)
{
	u8 cnt;

	cnt = rk816_bat_read(di, RK816_REBOOT_CNT_REG);
	cnt++;

	if (cnt >= REBOOT_MAX_CNT) {
		BAT_INFO("reboot: %d --> %d\n", di->dsoc, di->rsoc);
		di->dsoc = di->rsoc;
		if (di->dsoc > 100)
			di->dsoc = 100;
		else if (di->dsoc < 0)
			di->dsoc = 0;
		rk816_bat_save_dsoc(di, di->dsoc);
		cnt = REBOOT_MAX_CNT;
	}

	rk816_bat_save_reboot_cnt(di, cnt);
	DBG("reboot cnt: %d\n", cnt);

	return cnt;
}

static void rk816_bat_check_charger(struct rk816_battery *di)
{
	u8 buf;

	buf = rk816_bat_read(di, RK816_VB_MON_REG);
	/* pmic detect plug in, but ac/usb/dc_in offline, do check */
	if ((buf & PLUG_IN_STS) != 0 && !rk816_bat_chrg_online(di)) {
		rk816_bat_set_chrg_param(di, USB_TYPE_USB_CHARGER);
		BAT_INFO("pmic detect charger.. USB\n");
	/* pmic not detect plug in, but one of ac/usb/dc_in online, reset */
	} else if ((buf & PLUG_IN_STS) == 0 && rk816_bat_chrg_online(di)) {
		rk816_bat_set_chrg_param(di, USB_TYPE_UNKNOWN_CHARGER);
		BAT_INFO("pmic not detect charger..\n");
	}
}

static void rk816_bat_rsoc_daemon(struct rk816_battery *di)
{
	int est_vol, remain_cap;
	static unsigned long sec;

	if ((di->remain_cap < 0) && (di->fb_blank != 0)) {
		if (!sec)
			sec = get_boot_sec();
		wake_lock_timeout(&di->wake_lock,
				  (di->pdata->monitor_sec + 1) * HZ);
		DBG("sec=%ld, hold_sec=%ld\n", sec, base2sec(sec));
		if (base2sec(sec) >= 60) {
			sec = 0;
			di->dbg_cap_low0++;
			est_vol = di->voltage_avg -
					(di->bat_res * di->current_avg) / 1000;
			remain_cap = rk816_bat_vol_to_ocvcap(di, est_vol);
			rk816_bat_init_capacity(di, remain_cap);
			BAT_INFO("adjust cap below 0 --> %d, rsoc=%d\n",
				 di->remain_cap, di->rsoc);
			wake_unlock(&di->wake_lock);
		}
	} else {
		sec = 0;
	}
}

static void rk816_bat_update_info(struct rk816_battery *di)
{
	bool is_charging;

	di->voltage_avg = rk816_bat_get_avg_voltage(di);
	di->current_avg = rk816_bat_get_avg_current(di);
	di->chrg_status = rk816_bat_get_chrg_status(di);
	di->voltage_relax = rk816_bat_get_relax_voltage(di);
	di->rsoc = rk816_bat_get_rsoc(di);
	di->remain_cap = rk816_bat_get_coulomb_cap(di);
	is_charging = rk816_bat_chrg_online(di);
	if (is_charging != di->is_charging) {
		di->is_charging = is_charging;
		if (is_charging)
			di->charge_count++;
	}
	if (di->voltage_avg > di->voltage_max)
		di->voltage_max = di->voltage_avg;
	if (di->current_avg > di->current_max)
		di->current_max = di->current_avg;

	/* smooth charge */
	if (di->remain_cap > di->fcc) {
		di->sm_remain_cap -= (di->remain_cap - di->fcc);
		DBG("<%s>. cap: remain=%d, sm_remain=%d\n",
		    __func__, di->remain_cap, di->sm_remain_cap);
		rk816_bat_init_coulomb_cap(di, di->fcc);
	}

	if (di->chrg_status != CHARGE_FINISH)
		di->chrg_finish_base = get_boot_sec();

	/*
	 * we need update fcc in continuous charging state, if discharge state
	 * keep at least 2 hour, we decide not to update fcc, so clear the
	 * fcc update flag: age_allow_update.
	 */
	if (base2min(di->plug_out_base) > 120)
		di->age_allow_update = false;
	/* do adc calib: status must from cccv mode to finish mode */
	if (di->chrg_status == CC_OR_CV) {
		di->adc_allow_update = true;
		di->adc_calib_cnt = 0;
	}
}

static void rk816_bat_init_dsoc_algorithm(struct rk816_battery *di)
{
	u8 buf;
	int16_t rest = 0;
	unsigned long soc_sec;
	const char *mode_name[] = { "MODE_ZERO", "MODE_FINISH",
		"MODE_SMOOTH_CHRG", "MODE_SMOOTH_DISCHRG", "MODE_SMOOTH", };

	/* get rest */
	rest |= rk816_bat_read(di, RK816_CALC_REST_REGH) << 8;
	rest |= rk816_bat_read(di, RK816_CALC_REST_REGL) << 0;

	/* get mode */
	buf = rk816_bat_read(di, RK816_MISC_MARK_REG);
	di->algo_rest_mode = (buf & ALGO_REST_MODE_MSK) >> ALGO_REST_MODE_SHIFT;

	if (rk816_bat_get_chrg_status(di) == CHARGE_FINISH) {
		if (di->algo_rest_mode == MODE_FINISH) {
			soc_sec = di->fcc * 3600 / 100 / FINISH_CHRG_CUR1;
			if ((rest / DIV(soc_sec)) > 0) {
				if (di->dsoc < 100) {
					di->dsoc++;
					di->algo_rest_val = rest % soc_sec;
					BAT_INFO("algorithm rest(%d) dsoc inc: %d\n",
						 rest, di->dsoc);
				} else {
					di->algo_rest_val = 0;
				}
			} else {
				di->algo_rest_val = rest;
			}
		} else {
			di->algo_rest_val = rest;
		}
	} else {
		buf = rk816_bat_read(di, RK816_VB_MON_REG);
		/* charge speed up */
		if ((rest / 1000) > 0 && (buf & PLUG_IN_STS)) {
			if (di->dsoc < di->rsoc) {
				di->dsoc++;
				di->algo_rest_val = rest % 1000;
				BAT_INFO("algorithm rest(%d) dsoc inc: %d\n",
					 rest, di->dsoc);
			} else {
				di->algo_rest_val = 0;
			}
		/* discharge speed up */
		} else if (((rest / 1000) < 0) && !(buf & PLUG_IN_STS)) {
			if (di->dsoc > di->rsoc) {
				di->dsoc--;
				di->algo_rest_val = rest % 1000;
				BAT_INFO("algorithm rest(%d) dsoc sub: %d\n",
					 rest, di->dsoc);
			} else {
				di->algo_rest_val = 0;
			}
		} else {
			di->algo_rest_val = rest;
		}
	}

	if (di->dsoc >= 100)
		di->dsoc = 100;
	else if (di->dsoc <= 0)
		di->dsoc = 0;

	/* init current mode */
	di->voltage_avg = rk816_bat_get_avg_voltage(di);
	di->current_avg = rk816_bat_get_avg_current(di);
	if (rk816_bat_get_chrg_status(di) == CHARGE_FINISH) {
		rk816_bat_finish_algo_prepare(di);
		di->work_mode = MODE_FINISH;
	} else {
		rk816_bat_smooth_algo_prepare(di);
		di->work_mode = MODE_SMOOTH;
	}

	DBG("<%s>. init: org_rest=%d, rest=%d, mode=%s; "
	    "doc(x1000): zero=%d, chrg=%d, dischrg=%d, finish=%lu\n",
	    __func__, rest, di->algo_rest_val, mode_name[di->algo_rest_mode],
	    di->zero_dsoc, di->sm_chrg_dsoc, di->sm_dischrg_dsoc,
	    di->chrg_finish_base);
}

static void rk816_bat_save_algo_rest(struct rk816_battery *di)
{
	u8 buf, mode;
	int16_t algo_rest = 0;
	int tmp_soc;
	int zero_rest = 0, sm_chrg_rest = 0;
	int sm_dischrg_rest = 0, finish_rest = 0;
	static const char *mode_name[] = { "MODE_ZERO", "MODE_FINISH",
		"MODE_SMOOTH_CHRG", "MODE_SMOOTH_DISCHRG", "MODE_SMOOTH", };

	/* zero dischrg */
	tmp_soc = (di->zero_dsoc) / 1000;
	if (tmp_soc == di->dsoc)
		zero_rest = di->zero_dsoc - ((di->dsoc + 1) * 1000 -
				MIN_ACCURACY);

	/* sm chrg */
	tmp_soc = di->sm_chrg_dsoc / 1000;
	if (tmp_soc == di->dsoc)
		sm_chrg_rest = di->sm_chrg_dsoc - di->dsoc * 1000;

	/* sm dischrg */
	tmp_soc = (di->sm_dischrg_dsoc) / 1000;
	if (tmp_soc == di->dsoc)
		sm_dischrg_rest = di->sm_dischrg_dsoc - ((di->dsoc + 1) * 1000 -
				MIN_ACCURACY);

	/* last time is also finish chrg, then add last rest */
	if (di->algo_rest_mode == MODE_FINISH && di->algo_rest_val)
		finish_rest = base2sec(di->chrg_finish_base) +
			      di->algo_rest_val;
	else
		finish_rest = base2sec(di->chrg_finish_base);

	/* total calc */
	if ((rk816_bat_chrg_online(di) && (di->dsoc > di->rsoc)) ||
	    (!rk816_bat_chrg_online(di) && (di->dsoc < di->rsoc)) ||
	    (di->dsoc == di->rsoc)) {
		di->algo_rest_val = 0;
		algo_rest = 0;
		DBG("<%s>. step1..\n", __func__);
	} else if (di->work_mode == MODE_FINISH) {
		algo_rest = finish_rest;
		DBG("<%s>. step2..\n", __func__);
	} else if (di->algo_rest_mode == MODE_FINISH) {
		algo_rest = zero_rest + sm_dischrg_rest + sm_chrg_rest;
		DBG("<%s>. step3..\n", __func__);
	} else {
		if (rk816_bat_chrg_online(di) && (di->dsoc < di->rsoc))
			algo_rest = sm_chrg_rest + di->algo_rest_val;
		else if (!rk816_bat_chrg_online(di) && (di->dsoc > di->rsoc))
			algo_rest = zero_rest + sm_dischrg_rest +
				    di->algo_rest_val;
		else
			algo_rest = zero_rest + sm_dischrg_rest + sm_chrg_rest +
				    di->algo_rest_val;
		DBG("<%s>. step4..\n", __func__);
	}

	/* check mode */
	if ((di->work_mode == MODE_FINISH) || (di->work_mode == MODE_ZERO)) {
		mode = di->work_mode;
	} else {/* MODE_SMOOTH */
		if (di->sm_linek > 0)
			mode = MODE_SMOOTH_CHRG;
		else
			mode = MODE_SMOOTH_DISCHRG;
	}

	/* save mode */
	buf = rk816_bat_read(di, RK816_MISC_MARK_REG);
	buf &= ~ALGO_REST_MODE_MSK;
	buf |= (mode << ALGO_REST_MODE_SHIFT);
	rk816_bat_write(di, RK816_MISC_MARK_REG, buf);

	/* save rest */
	buf = (algo_rest >> 8) & 0xff;
	rk816_bat_write(di, RK816_CALC_REST_REGH, buf);
	buf = (algo_rest >> 0) & 0xff;
	rk816_bat_write(di, RK816_CALC_REST_REGL, buf);

	DBG("<%s>. rest: algo=%d, mode=%s, last_rest=%d; zero=%d, chrg=%d, dischrg=%d, finish=%lu\n",
	    __func__, algo_rest, mode_name[mode], di->algo_rest_val, zero_rest,
	    sm_chrg_rest, sm_dischrg_rest, base2sec(di->chrg_finish_base));
}

static void rk816_bat_save_data(struct rk816_battery *di)
{
	rk816_bat_save_dsoc(di, di->dsoc);
	rk816_bat_save_cap(di, di->remain_cap);
	rk816_bat_save_algo_rest(di);
}

/*get ntc resistance*/
static int rk816_bat_get_ntc_res(struct rk816_battery *di)
{
	int res, val = 0;

	val |= rk816_bat_read(di, RK816_TS_ADC_REGL) << 0;
	val |= rk816_bat_read(di, RK816_TS_ADC_REGH) << 8;

	res = ((di->voltage_k * val) / 1000 + di->voltage_b) * 1000 / 2200;
	res = res * 1000 / di->pdata->ntc_factor;

	DBG("<%s>. val=%d, ntc_res=%d, factor=%d\n",
	    __func__, val, res, di->pdata->ntc_factor);

	DBG("<%s>. t=[%d'C(%d) ~ %dC(%d)]\n", __func__,
	    di->pdata->ntc_degree_from, di->pdata->ntc_table[0],
	    di->pdata->ntc_degree_from + di->pdata->ntc_size - 1,
	    di->pdata->ntc_table[di->pdata->ntc_size - 1]);

	return res;
}

static int rk816_bat_temperature_chrg(struct rk816_battery *di, int temp)
{
	static int temp_triggered, config_index = -1;
	int i, up_temp, down_temp, cfg_current;
	u8 usb_ctrl, chrg_ctrl1;
	int now_temp = temp;
	int cur;

	for (i = 0; i < di->pdata->tc_count; i++) {
		up_temp = di->pdata->tc_table[i].temp_up;
		down_temp = di->pdata->tc_table[i].temp_down;
		cfg_current = di->pdata->tc_table[i].chrg_current;

		if (now_temp >= down_temp && now_temp <= up_temp) {
			/* Temp range or charger are not update, return */
			if (config_index == i && !di->charger_changed)
				return 0;

			config_index = i;
			di->charger_changed = 0;
			temp_triggered = 1;

			if (di->pdata->tc_table[i].set_chrg_current) {
				rk816_bat_set_chrg_current(di, cfg_current);
				if (!di->over_20mR)
					cur =
					  RES_FAC_MUX(CHRG_CUR_SEL[cfg_current],
						      di->res_fac);
				else
					cur =
					  RES_FAC_DIV(CHRG_CUR_SEL[cfg_current],
						      di->res_fac);
				BAT_INFO("temperature = %d'C[%d~%d'C], chrg current = %d\n",
					 now_temp, down_temp, up_temp, cur);
			} else {
				rk816_bat_set_input_current(di, cfg_current);
				BAT_INFO("temperature = %d'C[%d~%d'C], input current = %d\n",
					 now_temp, down_temp, up_temp,
					 CHRG_CUR_INPUT[cfg_current]);
			}
			return 0;	/* return after configure */
		}
	}

	/*
	 * means: current temperature not covers above case, temperature rolls
	 * back to normal range, so restore default value
	 */
	if (temp_triggered) {
		temp_triggered = 0;
		config_index = -1;
		rk816_bat_set_chrg_current(di, di->chrg_cur_sel);
		if (di->ac_in || di->dc_in)
			rk816_bat_set_input_current(di, di->chrg_cur_input);
		else
			rk816_bat_set_input_current(di, INPUT_CUR450MA);
		usb_ctrl = rk816_bat_read(di, RK816_USB_CTRL_REG);
		chrg_ctrl1 = rk816_bat_read(di, RK816_CHRG_CTRL_REG1);
		cfg_current = chrg_ctrl1 & 0x0f;
		if (!di->over_20mR)
			cur =
			  RES_FAC_MUX(CHRG_CUR_SEL[cfg_current], di->res_fac);
		else
			cur =
			  RES_FAC_DIV(CHRG_CUR_SEL[cfg_current], di->res_fac);
		BAT_INFO("roll back temp %d'C, current chrg = %d, input = %d\n",
			 now_temp, cur, CHRG_CUR_INPUT[(usb_ctrl & 0x0f)]);
	}

	return 0;
}

static void rk816_bat_update_temperature(struct rk816_battery *di)
{
	u32 ntc_size, *ntc_table;
	int i, res;

	ntc_table = di->pdata->ntc_table;
	ntc_size = di->pdata->ntc_size;
	di->temperature = VIRTUAL_TEMPERATURE;

	if (ntc_size) {
		res = rk816_bat_get_ntc_res(di);
		if (res < ntc_table[ntc_size - 1]) {
			BAT_INFO("bat ntc upper max degree: R=%d\n", res);
			rk816_bat_set_input_current(di, INPUT_CUR80MA);
		} else if (res > ntc_table[0]) {
			BAT_INFO("bat ntc lower min degree: R=%d\n", res);
			rk816_bat_set_input_current(di, INPUT_CUR80MA);
		} else {
			for (i = 0; i < ntc_size; i++) {
				if (res >= ntc_table[i])
					break;
			}

			di->temperature = (i + di->pdata->ntc_degree_from) * 10;
			rk816_bat_temperature_chrg(di, di->temperature / 10);
		}
	}
}

static void rk816_bat_update_ocv_table(struct rk816_battery *di)
{
	static bool initialized;
	static int temp_idx, temperature_sum, last_avg_temp, curr_avg_temp;
	static int temp_record_table[TEMP_RECORD_NUM];
	int i, curr_temp = di->temperature / 10;

	if (di->pdata->temp_t_num < 2)
		return;

	/* only run once for initialize */
	if (!initialized) {
		for (i = 0; i < TEMP_RECORD_NUM; i++)
			temp_record_table[i] = curr_temp;

		temperature_sum = curr_temp * TEMP_RECORD_NUM;
		last_avg_temp = curr_temp;
		initialized = true;
	}

	/* pick out earliest temperature from sum */
	temperature_sum -= temp_record_table[temp_idx];

	/* add current temperature into sum */
	temp_record_table[temp_idx] = curr_temp;
	temperature_sum += curr_temp;

	/* new avg temperature currently */
	curr_avg_temp = temperature_sum / TEMP_RECORD_NUM;

	/* move to next idx */
	temp_idx = (temp_idx + 1) % TEMP_RECORD_NUM;

	DBG("<%s>: temp_idx=%d, curr_temp=%d, last_avg=%d, curr_avg=%d\n",
	    __func__, temp_idx, curr_temp, last_avg_temp, curr_avg_temp);

	/* tempearture changed, update ocv table */
	if (curr_avg_temp != last_avg_temp) {
		BAT_INFO("OCV table update, temperature now=%d, last=%d\n",
			 curr_avg_temp, last_avg_temp);
		rk816_bat_setup_ocv_table(di, curr_avg_temp);
		last_avg_temp = curr_avg_temp;

		if (!dbg_enable)
			return;

		for (i = 0; i < di->pdata->ocv_size; i++)
			DBG("* ocv_table[%d]=%d\n", i, di->pdata->ocv_table[i]);
	}
}

static void rk816_battery_work(struct work_struct *work)
{
	struct rk816_battery *di =
		container_of(work, struct rk816_battery, bat_delay_work.work);

	rk816_bat_update_info(di);
	rk816_bat_wait_finish_sig(di);
	rk816_bat_rsoc_daemon(di);
	rk816_bat_check_charger(di);
	rk816_bat_update_temperature(di);
	rk816_bat_update_ocv_table(di);
	rk816_bat_lowpwr_check(di);
	rk816_bat_display_smooth(di);
	rk816_bat_power_supply_changed(di);
	rk816_bat_save_data(di);
	rk816_bat_debug_info(di);

	queue_delayed_work(di->bat_monitor_wq, &di->bat_delay_work,
			   msecs_to_jiffies(di->monitor_ms));
}

static void rk816_bat_discnt_evt_worker(struct work_struct *work)
{
	struct rk816_battery *di = container_of(work,
			struct rk816_battery, discnt_work.work);

	if (extcon_get_state(di->cable_edev, EXTCON_USB) == 0) {
		BAT_INFO("receive extcon notifier event: DISCNT...\n");
		rk816_bat_set_chrg_param(di, USB_TYPE_NONE_CHARGER);
	}
}

static void rk816_bat_host_evt_worker(struct work_struct *work)
{
	struct rk816_battery *di = container_of(work,
			struct rk816_battery, host_work.work);
	struct extcon_dev *edev = di->cable_edev;

	/* Determine charger type */
	if (extcon_get_state(edev, EXTCON_USB_VBUS_EN) > 0) {
		rk816_bat_set_otg_in(di, ONLINE);
		BAT_INFO("receive extcon notifier event: OTG ON...\n");
		if (di->dc_in && di->pdata->power_dc2otg)
			BAT_INFO("otg power from dc adapter\n");
		else
			rk816_bat_set_otg_power(di, USB_OTG_POWER_ON);
	} else if (extcon_get_state(edev, EXTCON_USB_VBUS_EN) == 0) {
		BAT_INFO("receive extcon notifier event: OTG OFF...\n");
		rk816_bat_set_otg_in(di, OFFLINE);
		rk816_bat_set_otg_power(di, USB_OTG_POWER_OFF);
	}
}

static void rk816_bat_charger_evt_worker(struct work_struct *work)
{
	struct rk816_battery *di = container_of(work,
				struct rk816_battery, usb_work.work);
	struct extcon_dev *edev = di->cable_edev;
	enum charger_t charger = USB_TYPE_UNKNOWN_CHARGER;
	static const char *event[] = {"UN", "NONE", "USB", "AC", "CDP1.5A"};

	/* Determine charger type */
	if (extcon_get_state(edev, EXTCON_CHG_USB_SDP) > 0)
		charger = USB_TYPE_USB_CHARGER;
	else if (extcon_get_state(edev, EXTCON_CHG_USB_DCP) > 0)
		charger = USB_TYPE_AC_CHARGER;
	else if (extcon_get_state(edev, EXTCON_CHG_USB_CDP) > 0)
		charger = USB_TYPE_CDP_CHARGER;
	else
		charger = USB_TYPE_NONE_CHARGER;

	if (charger != USB_TYPE_UNKNOWN_CHARGER) {
		BAT_INFO("receive extcon notifier event: %s...\n",
			 event[charger]);
		rk816_bat_set_chrg_param(di, charger);
	}
}

static int rk816_bat_charger_evt_notifier(struct notifier_block *nb,
					  unsigned long event, void *ptr)
{
	struct rk816_battery *di =
		container_of(nb, struct rk816_battery, cable_cg_nb);

	queue_delayed_work(di->usb_charger_wq, &di->usb_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int rk816_bat_discnt_evt_notfier(struct notifier_block *nb,
					unsigned long event, void *ptr)
{
	struct rk816_battery *di =
		container_of(nb, struct rk816_battery, cable_discnt_nb);

	queue_delayed_work(di->usb_charger_wq, &di->discnt_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int rk816_bat_host_evt_notifier(struct notifier_block *nb,
				       unsigned long event, void *ptr)
{
	struct rk816_battery *di =
		container_of(nb, struct rk816_battery, cable_host_nb);

	queue_delayed_work(di->usb_charger_wq, &di->host_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static irqreturn_t rk816_vb_low_irq(int irq, void *bat)
{
	struct rk816_battery *di = (struct rk816_battery *)bat;

	BAT_INFO("lower power yet, power off system! v=%d\n",
		 di->voltage_avg);
	di->dsoc = 0;
	rk_send_wakeup_key();
	power_supply_changed(di->bat);

	return IRQ_HANDLED;
}

static irqreturn_t rk816_plug_in(int irq, void *bat)
{
	rk_send_wakeup_key();
	BAT_INFO("pmic: plug in\n");

	return IRQ_HANDLED;
}

static irqreturn_t rk816_cvtlmt(int irq, void  *bat)
{
	struct rk816_battery *di = (struct rk816_battery *)bat;

	di->cvtlmt_int_event = 1;
	BAT_INFO("pmic: cvtlmt irq\n");

	return IRQ_HANDLED;
}

static irqreturn_t rk816_plug_out(int irq, void  *bat)
{
	rk_send_wakeup_key();
	BAT_INFO("pmic: plug out\n");

	return IRQ_HANDLED;
}

static irqreturn_t rk816_vbat_dc_det(int irq, void *bat)
{
	struct rk816_battery *di = (struct rk816_battery *)bat;

	if (gpio_get_value(di->pdata->dc_det_pin))
		irq_set_irq_type(irq, IRQF_TRIGGER_LOW);
	else
		irq_set_irq_type(irq, IRQF_TRIGGER_HIGH);

	BAT_INFO("dc det in/out\n");
	queue_delayed_work(di->usb_charger_wq,
			   &di->dc_delay_work, msecs_to_jiffies(500));
	rk_send_wakeup_key();

	return IRQ_HANDLED;
}

static void rk816_bat_init_sysfs(struct rk816_battery *di)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(rk816_bat_attr); i++) {
		ret = sysfs_create_file(&di->dev->kobj,
					&rk816_bat_attr[i].attr);
		if (ret)
			dev_err(di->dev, "create bat node(%s) error\n",
				rk816_bat_attr[i].attr.name);
	}
}

static int rk816_bat_init_irqs(struct rk816_battery *di)
{
	int ret;
	int plug_in_irq, plug_out_irq, vb_lo_irq, cvtlmt_irq;
	struct rk808 *rk816 = di->rk816;
	struct platform_device *pdev = di->pdev;

	vb_lo_irq = regmap_irq_get_virq(rk816->irq_data, RK816_IRQ_VB_LOW);
	if (vb_lo_irq < 0) {
		dev_err(&pdev->dev, "find vb_lo_irq error\n");
		return vb_lo_irq;
	}

	plug_in_irq = regmap_irq_get_virq(rk816->battery_irq_data,
					  RK816_IRQ_PLUG_IN);
	if (plug_in_irq < 0) {
		dev_err(&pdev->dev, "find plug_in_irq error\n");
		return plug_in_irq;
	}

	plug_out_irq = regmap_irq_get_virq(rk816->battery_irq_data,
					   RK816_IRQ_PLUG_OUT);
	if (plug_out_irq < 0) {
		dev_err(&pdev->dev, "find plug_out_irq error\n");
		return plug_out_irq;
	}

	cvtlmt_irq = regmap_irq_get_virq(rk816->battery_irq_data,
					 RK816_IRQ_CHG_CVTLIM);
	if (cvtlmt_irq < 0) {
		dev_err(&pdev->dev, "find cvtlmt_irq error\n");
		return cvtlmt_irq;
	}

	/* low power */
	ret = devm_request_threaded_irq(di->dev, vb_lo_irq, NULL,
					rk816_vb_low_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"rk816_vb_low", di);
	if (ret) {
		dev_err(di->dev, "vb low irq request failed!\n");
		return ret;
	}

	enable_irq_wake(vb_lo_irq);

	/* plug in */
	ret = devm_request_threaded_irq(di->dev, plug_in_irq, NULL,
					rk816_plug_in,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"rk816_plug_in", di);
	if (ret) {
		dev_err(di->dev, "plug in irq request failed!\n");
		return ret;
	}

	/* plug out */
	ret = devm_request_threaded_irq(di->dev, plug_out_irq, NULL,
					rk816_plug_out,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"rk816_plug_out", di);
	if (ret) {
		dev_err(di->dev, "plug out irq request failed!\n");
		return ret;
	}

	/* cvtlmt */
	ret = devm_request_threaded_irq(di->dev, cvtlmt_irq, NULL,
					rk816_cvtlmt,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"rk816_cvtlmt", di);
	if (ret) {
		dev_err(di->dev, "cvtlmt irq request failed!\n");
		return ret;
	}
	disable_irq(cvtlmt_irq);

	di->cvtlmt_irq = cvtlmt_irq;

	return 0;
}

static void rk816_bat_init_info(struct rk816_battery *di)
{
	di->design_cap = di->pdata->design_capacity;
	di->qmax = di->pdata->design_qmax;
	di->bat_res = di->pdata->bat_res;
	di->sleep_chrg_status = rk816_bat_get_chrg_status(di);
	di->monitor_ms = di->pdata->monitor_sec * TIMER_MS_COUNTS;
	di->prop_status = POWER_SUPPLY_STATUS_DISCHARGING;
	di->boot_base = POWER_ON_SEC_BASE;
	di->chrg_finish_base = 0;
	di->plug_in_base = 0;
	di->plug_out_base = 0;
}

static enum charger_t rk816_bat_init_adc_dc_det(struct rk816_battery *di)
{
	return rk816_bat_get_adc_dc_state(di);
}

static enum charger_t rk816_bat_init_gpio_dc_det(struct rk816_battery *di)
{
	int ret, level;
	unsigned long irq_flags;
	unsigned int dc_det_irq;
	enum charger_t type = DC_TYPE_NONE_CHARGER;

	if (gpio_is_valid(di->pdata->dc_det_pin)) {
		ret = devm_gpio_request(di->dev, di->pdata->dc_det_pin,
					"rk816_dc_det");
		if (ret < 0) {
			dev_err(di->dev, "Failed to request gpio %d\n",
				di->pdata->dc_det_pin);
			goto out;
		}

		ret = gpio_direction_input(di->pdata->dc_det_pin);
		if (ret) {
			dev_err(di->dev, "failed to set gpio input\n");
			goto out;
		}

		level = gpio_get_value(di->pdata->dc_det_pin);
		if (level == di->pdata->dc_det_level)
			type = DC_TYPE_DC_CHARGER;
		else
			type = DC_TYPE_NONE_CHARGER;

		if (level)
			irq_flags = IRQF_TRIGGER_LOW;
		else
			irq_flags = IRQF_TRIGGER_HIGH;

		dc_det_irq = gpio_to_irq(di->pdata->dc_det_pin);
		ret = devm_request_irq(di->dev, dc_det_irq, rk816_vbat_dc_det,
				       irq_flags, "rk816_dc_det", di);
		if (ret != 0) {
			dev_err(di->dev, "rk816_dc_det_irq request failed!\n");
			goto out;
		}

		enable_irq_wake(dc_det_irq);
	}
out:
	return type;
}

static enum charger_t rk816_bat_init_dc_det(struct rk816_battery *di)
{
	enum charger_t type;

	if (di->pdata->dc_det_adc)
		type = rk816_bat_init_adc_dc_det(di);
	else
		type = rk816_bat_init_gpio_dc_det(di);

	return type;
}

static int rk816_bat_init_charger(struct rk816_battery *di)
{
	enum charger_t dc_charger;
	struct device *dev = di->dev;
	struct extcon_dev *edev;
	int ret;

	di->usb_charger_wq = alloc_ordered_workqueue("%s",
				WQ_MEM_RECLAIM | WQ_FREEZABLE,
				"rk816-bat-charger-wq");
	INIT_DELAYED_WORK(&di->dc_delay_work, rk816_bat_dc_delay_work);

	/* Find extcon phandle */
	edev = extcon_get_edev_by_phandle(dev->parent, 0);
	if (IS_ERR(edev)) {
		if (PTR_ERR(edev) != -EPROBE_DEFER)
			dev_err(dev, "Invalid or missing extcon\n");
		return PTR_ERR(edev);
	}

	/* Register chargers */
	INIT_DELAYED_WORK(&di->usb_work, rk816_bat_charger_evt_worker);
	di->cable_cg_nb.notifier_call = rk816_bat_charger_evt_notifier;
	ret = extcon_register_notifier(edev, EXTCON_CHG_USB_SDP,
				       &di->cable_cg_nb);
	if (ret < 0) {
		dev_err(dev, "failed to register notifier for SDP\n");
		return ret;
	}

	ret = extcon_register_notifier(edev, EXTCON_CHG_USB_DCP,
				       &di->cable_cg_nb);
	if (ret < 0) {
		dev_err(dev, "failed to register notifier for DCP\n");
		extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
					   &di->cable_cg_nb);
		return ret;
	}

	ret = extcon_register_notifier(edev, EXTCON_CHG_USB_CDP,
				       &di->cable_cg_nb);
	if (ret < 0) {
		dev_err(dev, "failed to register notifier for CDP\n");
		extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
					   &di->cable_cg_nb);
		extcon_unregister_notifier(edev, EXTCON_CHG_USB_DCP,
					   &di->cable_cg_nb);
		return ret;
	}

	/* Register host */
	INIT_DELAYED_WORK(&di->host_work, rk816_bat_host_evt_worker);
	di->cable_host_nb.notifier_call = rk816_bat_host_evt_notifier;
	ret = extcon_register_notifier(edev, EXTCON_USB_VBUS_EN,
				       &di->cable_host_nb);
	if (ret < 0) {
		dev_err(dev, "failed to register notifier for HOST\n");
		extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
					   &di->cable_cg_nb);
		extcon_unregister_notifier(edev, EXTCON_CHG_USB_DCP,
					   &di->cable_cg_nb);
		extcon_unregister_notifier(edev, EXTCON_CHG_USB_CDP,
					   &di->cable_cg_nb);

		return ret;
	}

	/* Register discnt usb */
	INIT_DELAYED_WORK(&di->discnt_work, rk816_bat_discnt_evt_worker);
	di->cable_discnt_nb.notifier_call = rk816_bat_discnt_evt_notfier;
	ret = extcon_register_notifier(edev, EXTCON_USB,
				       &di->cable_discnt_nb);
	if (ret < 0) {
		dev_err(dev, "failed to register notifier for HOST\n");
		extcon_unregister_notifier(edev, EXTCON_CHG_USB_SDP,
					   &di->cable_cg_nb);
		extcon_unregister_notifier(edev, EXTCON_CHG_USB_DCP,
					   &di->cable_cg_nb);
		extcon_unregister_notifier(edev, EXTCON_CHG_USB_CDP,
					   &di->cable_cg_nb);
		extcon_unregister_notifier(edev, EXTCON_USB_VBUS_EN,
					   &di->cable_host_nb);
		return ret;
	}

	di->cable_edev = edev;

	/* Check usb and otg state */
	schedule_delayed_work(&di->host_work, 0);
	schedule_delayed_work(&di->usb_work, 0);

	BAT_INFO("register extcon evt notifier\n");

	/* adc dc need poll every 1s */
	if (di->pdata->dc_det_adc)
		queue_delayed_work(di->usb_charger_wq, &di->dc_delay_work,
				   msecs_to_jiffies(1000));

	dc_charger = rk816_bat_init_dc_det(di);
	rk816_bat_set_chrg_param(di, dc_charger);
	if (di->dc_in && di->otg_in && di->pdata->power_dc2otg) {
		BAT_INFO("otg power from dc adapter\n");
		rk816_bat_set_otg_power(di, USB_OTG_POWER_OFF);
	}

	return 0;
}

static time_t rk816_get_rtc_sec(void)
{
	int err;
	struct rtc_time tm;
	struct timespec tv = { .tv_nsec = NSEC_PER_SEC >> 1, };
	struct rtc_device *rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	time_t sec;

	err = rtc_read_time(rtc, &tm);
	if (err) {
		dev_err(rtc->dev.parent, "read hardware clk failed\n");
		return 0;
	}

	err = rtc_valid_tm(&tm);
	if (err) {
		dev_err(rtc->dev.parent, "invalid date time\n");
		return 0;
	}

	rtc_tm_to_time(&tm, &tv.tv_sec);
	sec = tv.tv_sec;

	return sec;
}

static int rk816_bat_rtc_sleep_sec(struct rk816_battery *di)
{
	int interval_sec;

	interval_sec = rk816_get_rtc_sec() - di->rtc_base;

	return (interval_sec > 0) ? interval_sec : 0;
}

static void rk816_bat_init_ts_detect(struct rk816_battery *di)
{
	u8 buf;

	if (!di->pdata->ntc_size)
		return;

	/* Pin func: ts */
	buf = rk816_bat_read(di, RK816_GPIO_IO_POL_REG);
	buf &= ~BIT(2);
	rk816_bat_write(di, RK816_GPIO_IO_POL_REG, buf);

	/* External temperature monitoring */
	buf = rk816_bat_read(di, RK816_TS_CTRL_REG);
	buf &= ~BIT(4);
	rk816_bat_write(di, RK816_TS_CTRL_REG, buf);

	/* select ua */
	buf = rk816_bat_read(di, RK816_TS_CTRL_REG);
	buf &= ~ADC_CUR_MSK;
	if (di->pdata->ntc_factor == NTC_CALC_FACTOR_80UA)
		buf |= ADC_CUR_80UA;
	else if (di->pdata->ntc_factor == NTC_CALC_FACTOR_60UA)
		buf |= ADC_CUR_60UA;
	else if (di->pdata->ntc_factor == NTC_CALC_FACTOR_40UA)
		buf |= ADC_CUR_40UA;
	else
		buf |= ADC_CUR_20UA;
	rk816_bat_write(di, RK816_TS_CTRL_REG, buf);

	/* ADC_TS_EN */
	buf = rk816_bat_read(di, RK816_ADC_CTRL_REG);
	buf |= BIT(5);
	rk816_bat_write(di, RK816_ADC_CTRL_REG, buf);
}

static void rk816_bat_init_fg(struct rk816_battery *di)
{
	rk816_bat_enable_input_current(di);
	rk816_bat_enable_gauge(di);
	rk816_bat_init_voltage_kb(di);
	rk816_bat_init_poffset(di);
	rk816_bat_select_sample_res(di);
	rk816_bat_set_relax_sample(di);
	rk816_bat_set_ioffset_sample(di);
	rk816_bat_set_ocv_sample(di);
	rk816_bat_init_ts_detect(di);
	rk816_bat_update_temperature(di);
	rk816_bat_setup_ocv_table(di, di->temperature / 10);
	rk816_bat_init_rsoc(di);
	rk816_bat_init_coulomb_cap(di, di->nac);
	rk816_bat_init_age_algorithm(di);
	rk816_bat_init_chrg_config(di);
	rk816_bat_init_zero_table(di);
	rk816_bat_init_caltimer(di);
	rk816_bat_init_dsoc_algorithm(di);

	di->voltage_avg = rk816_bat_get_avg_voltage(di);
	di->voltage_ocv = rk816_bat_get_ocv_voltage(di);
	di->voltage_relax = rk816_bat_get_relax_voltage(di);
	di->current_avg = rk816_bat_get_avg_current(di);
	di->current_relax = rk816_bat_get_relax_current(di);
	di->remain_cap = rk816_bat_get_coulomb_cap(di);
	di->dbg_pwr_dsoc = di->dsoc;
	di->dbg_pwr_rsoc = di->rsoc;
	di->dbg_pwr_vol = di->voltage_avg;

	rk816_bat_dump_regs(di, 0x99, 0xee);
	DBG("nac=%d cap=%d ov=%d v=%d rv=%d dl=%d rl=%d c=%d\n",
	    di->nac, di->remain_cap, di->voltage_ocv, di->voltage_avg,
	    di->voltage_relax, di->dsoc, di->rsoc, di->current_avg);
}

static int rk816_bat_read_ocv_tables(struct rk816_battery *di,
				     struct device_node *np)
{
	struct battery_platform_data *pdata = di->pdata;
	u32 negative, value;
	int length, i, j;
	int idx = 0;

	/* t0 */
	if (of_find_property(np, "table_t0", &length) &&
	    of_find_property(np, "temp_t0", &length)) {
		DBG("%s: read table_t0\n", __func__);

		if (of_property_read_u32_array(np, "table_t0",
					       pdata->table_t[idx],
					       pdata->ocv_size)) {
			dev_err(di->dev, "invalid table_t0\n");
			return -EINVAL;
		}

		if (of_property_read_u32_index(np, "temp_t0", 1, &value) ||
		    of_property_read_u32_index(np, "temp_t0", 0, &negative)) {
			dev_err(di->dev, "invalid temp_t0\n");
			return -EINVAL;
		}
		if (negative)
			pdata->temp_t[idx] = -value;
		else
			pdata->temp_t[idx] = value;
		idx++;
	}

	/* t1 */
	if (of_find_property(np, "table_t1", &length) &&
	    of_find_property(np, "temp_t1", &length)) {
		DBG("%s: read table_t1\n", __func__);

		if (of_property_read_u32_array(np, "table_t1",
					       pdata->table_t[idx],
					       pdata->ocv_size)) {
			dev_err(di->dev, "invalid table_t1\n");
			return -EINVAL;
		}

		if (of_property_read_u32_index(np, "temp_t1", 1, &value) ||
		    of_property_read_u32_index(np, "temp_t1", 0, &negative)) {
			dev_err(di->dev, "invalid temp_t1\n");
			return -EINVAL;
		}
		if (negative)
			pdata->temp_t[idx] = -value;
		else
			pdata->temp_t[idx] = value;
		idx++;
	}

	/* t2 */
	if (of_find_property(np, "table_t2", &length) &&
	    of_find_property(np, "temp_t2", &length)) {
		DBG("%s: read table_t2\n", __func__);

		if (of_property_read_u32_array(np, "table_t2",
					       pdata->table_t[idx],
					       pdata->ocv_size)) {
			dev_err(di->dev, "invalid table_t2\n");
			return -EINVAL;
		}

		if (of_property_read_u32_index(np, "temp_t2", 1, &value) ||
		    of_property_read_u32_index(np, "temp_t2", 0, &negative)) {
			dev_err(di->dev, "invalid temp_t2\n");
			return -EINVAL;
		}
		if (negative)
			pdata->temp_t[idx] = -value;
		else
			pdata->temp_t[idx] = value;
		idx++;
	}

	/* t3 */
	if (of_find_property(np, "table_t3", &length) &&
	    of_find_property(np, "temp_t3", &length)) {
		DBG("%s: read table_t3\n", __func__);

		if (of_property_read_u32_array(np, "table_t3",
					       pdata->table_t[idx],
					       pdata->ocv_size)) {
			dev_err(di->dev, "invalid table_t3\n");
			return -EINVAL;
		}

		if (of_property_read_u32_index(np, "temp_t3", 1, &value) ||
		    of_property_read_u32_index(np, "temp_t3", 0, &negative)) {
			dev_err(di->dev, "invalid temp_t3\n");
			return -EINVAL;
		}
		if (negative)
			pdata->temp_t[idx] = -value;
		else
			pdata->temp_t[idx] = value;
		idx++;
	}

	di->pdata->temp_t_num = idx;

	DBG("realtime ocv table nums=%d\n", di->pdata->temp_t_num);

	if (dbg_enable) {
		for (j = 0; j < pdata->temp_t_num; j++) {
			DBG("\n\ntemperature[%d]=%d\n", j, pdata->temp_t[j]);
			for (i = 0; i < di->pdata->ocv_size; i++)
				DBG("table_t%d[%d]=%d\n",
				    j, i, pdata->table_t[j][i]);
		}
	}

	return 0;
}

static int parse_temperature_chrg_table(struct rk816_battery *di,
					struct device_node *np)
{
	int size, count;
	int i, chrg_current;
	const __be32 *list;

	if (!of_find_property(np, "temperature_chrg_table_v2", &size))
		return 0;

	list = of_get_property(np, "temperature_chrg_table_v2", &size);
	size /= sizeof(u32);
	if (!size || (size % 3)) {
		dev_err(di->dev,
			"invalid temperature_chrg_table: size=%d\n", size);
		return -EINVAL;
	}

	count = size / 3;
	di->pdata->tc_count = count;
	di->pdata->tc_table = devm_kzalloc(di->dev,
					   count * sizeof(*di->pdata->tc_table),
					   GFP_KERNEL);
	if (!di->pdata->tc_table)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		/* temperature */
		di->pdata->tc_table[i].temp_down = be32_to_cpu(*list++);
		di->pdata->tc_table[i].temp_up = be32_to_cpu(*list++);

		/*
		 * because charge current lowest level is 1000mA:
		 * higher than or equal 1000ma, select charge current;
		 * lower than 1000ma, must select input current.
		 */
		chrg_current = be32_to_cpu(*list++);
		if (chrg_current >= 1000) {
			di->pdata->tc_table[i].set_chrg_current = 1;
			di->pdata->tc_table[i].chrg_current =
				rk816_bat_decode_chrg_current(di, chrg_current);
		} else {
			di->pdata->tc_table[i].chrg_current =
				rk816_bat_decode_input_current(di, chrg_current);
		}

		DBG("temp%d: [%d, %d], chrg_current=%d\n",
		    i, di->pdata->tc_table[i].temp_down,
		    di->pdata->tc_table[i].temp_up,
		    di->pdata->tc_table[i].chrg_current);
	}

	return 0;
}


static int rk816_bat_parse_dt(struct rk816_battery *di)
{
	u32 out_value;
	int length, ret;
	size_t size;
	struct device_node *np;
	struct battery_platform_data *pdata;
	struct device *dev = di->dev;
	enum of_gpio_flags flags;

	np = of_find_node_by_name(di->rk816->i2c->dev.of_node, "battery");
	if (!np) {
		dev_err(dev, "battery node not found!\n");
		return -ENODEV;
	}

	pdata = devm_kzalloc(di->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	di->pdata = pdata;
	/* init default param */
	pdata->bat_res = DEFAULT_BAT_RES;
	pdata->monitor_sec = DEFAULT_MONITOR_SEC;
	pdata->pwroff_vol = DEFAULT_PWROFF_VOL_THRESD;
	pdata->sleep_exit_current = DEFAULT_SLP_EXIT_CUR;
	pdata->sleep_enter_current = DEFAULT_SLP_ENTER_CUR;
	pdata->sleep_filter_current = DEFAULT_SLP_FILTER_CUR;
	pdata->bat_mode = MODE_BATTARY;
	pdata->max_soc_offset = DEFAULT_MAX_SOC_OFFSET;
	pdata->fb_temp = DEFAULT_FB_TEMP;
	pdata->energy_mode = DEFAULT_ENERGY_MODE;
	pdata->zero_reserve_dsoc = DEFAULT_ZERO_RESERVE_DSOC;
	pdata->sample_res = DEFAULT_SAMPLE_RES;

	/* parse necessary param */
	if (!of_find_property(np, "ocv_table", &length)) {
		dev_err(dev, "ocv_table not found!\n");
		return -EINVAL;
	}

	pdata->ocv_size = length / sizeof(u32);
	if (pdata->ocv_size <= 0) {
		dev_err(dev, "invalid ocv table\n");
		return -EINVAL;
	}

	size = sizeof(*pdata->ocv_table) * pdata->ocv_size;
	pdata->ocv_table = devm_kzalloc(di->dev, size, GFP_KERNEL);
	if (!pdata->ocv_table)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "ocv_table", pdata->ocv_table,
					 pdata->ocv_size);
	if (ret < 0)
		return ret;

	ret = rk816_bat_read_ocv_tables(di, np);
	if (ret < 0) {
		di->pdata->temp_t_num = 0;
		dev_err(dev, "read table_t error\n");
		return ret;
	}

	ret = of_property_read_u32(np, "design_capacity", &out_value);
	if (ret < 0) {
		dev_err(dev, "design_capacity not found!\n");
		return ret;
	}
	pdata->design_capacity = out_value;

	ret = of_property_read_u32(np, "design_qmax", &out_value);
	if (ret < 0) {
		dev_err(dev, "design_qmax not found!\n");
		return ret;
	}
	pdata->design_qmax = out_value;

	ret = of_property_read_u32(np, "max_chrg_current", &out_value);
	if (ret < 0) {
		dev_err(dev, "max_chrg_current missing!\n");
		return ret;
	}
	pdata->max_chrg_current = out_value;

	ret = of_property_read_u32(np, "max_input_current", &out_value);
	if (ret < 0) {
		dev_err(dev, "max_input_current missing!\n");
		return ret;
	}
	pdata->max_input_current = out_value;

	ret = of_property_read_u32(np, "max_chrg_voltage", &out_value);
	if (ret < 0) {
		dev_err(dev, "max_chrg_voltage missing!\n");
		return ret;
	}
	pdata->max_chrg_voltage = out_value;
	if (out_value >= 4300)
		pdata->zero_algorithm_vol = DEFAULT_ALGR_VOL_THRESD2;
	else
		pdata->zero_algorithm_vol = DEFAULT_ALGR_VOL_THRESD1;

	pdata->extcon = device_property_read_bool(dev->parent, "extcon");
	if (!pdata->extcon) {
		dev_err(dev, "Can't find extcon node under rk816 node\n");
		return -EINVAL;
	}

	/* parse unnecessary param */
	of_property_read_u32(np, "sample_res", &pdata->sample_res);

	ret = of_property_read_u32(np, "fb_temperature", &pdata->fb_temp);
	if (ret < 0)
		dev_err(dev, "fb_temperature missing!\n");

	ret = of_property_read_u32(np, "energy_mode", &pdata->energy_mode);
	if (ret < 0)
		dev_err(dev, "energy_mode missing!\n");

	ret = of_property_read_u32(np, "max_soc_offset",
				   &pdata->max_soc_offset);
	if (ret < 0)
		dev_err(dev, "max_soc_offset missing!\n");

	ret = of_property_read_u32(np, "monitor_sec", &pdata->monitor_sec);
	if (ret < 0)
		dev_err(dev, "monitor_sec missing!\n");

	ret = of_property_read_u32(np, "zero_algorithm_vol",
				   &pdata->zero_algorithm_vol);
	if (ret < 0)
		dev_err(dev, "zero_algorithm_vol missing!\n");

	ret = of_property_read_u32(np, "zero_reserve_dsoc",
				   &pdata->zero_reserve_dsoc);

	ret = of_property_read_u32(np, "virtual_power", &pdata->bat_mode);
	if (ret < 0)
		dev_err(dev, "virtual_power missing!\n");

	ret = of_property_read_u32(np, "power_dc2otg", &pdata->power_dc2otg);
	if (ret < 0)
		dev_err(dev, "power_dc2otg missing!\n");

	ret = of_property_read_u32(np, "bat_res", &pdata->bat_res);
	if (ret < 0)
		dev_err(dev, "bat_res missing!\n");

	ret = of_property_read_u32(np, "sleep_enter_current",
				   &pdata->sleep_enter_current);
	if (ret < 0)
		dev_err(dev, "sleep_enter_current missing!\n");

	ret = of_property_read_u32(np, "sleep_exit_current",
				   &pdata->sleep_exit_current);
	if (ret < 0)
		dev_err(dev, "sleep_exit_current missing!\n");

	ret = of_property_read_u32(np, "sleep_filter_current",
				   &pdata->sleep_filter_current);
	if (ret < 0)
		dev_err(dev, "sleep_filter_current missing!\n");

	ret = of_property_read_u32(np, "power_off_thresd", &pdata->pwroff_vol);
	if (ret < 0)
		dev_err(dev, "power_off_thresd missing!\n");

	ret = of_property_read_u32(np, "otg5v_suspend_enable",
				   &pdata->otg5v_suspend_enable);
	if (ret < 0)
		pdata->otg5v_suspend_enable = 1;

	if (!of_find_property(np, "dc_det_gpio", &length)) {
		pdata->dc_det_pin = -1;
		of_property_read_u32(np, "dc_det_adc", &pdata->dc_det_adc);
		if (!pdata->dc_det_adc)
			BAT_INFO("not support dc\n");
		else
			BAT_INFO("support adc dc\n");
	} else {
		BAT_INFO("support gpio dc\n");
		pdata->dc_det_pin = of_get_named_gpio_flags(np, "dc_det_gpio",
							    0, &flags);
		if (gpio_is_valid(pdata->dc_det_pin)) {
			pdata->dc_det_level =
					(flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
			/* if support dc, default set power_dc2otg = 1 */
			pdata->power_dc2otg = 1;
		}
	}

	if (!of_find_property(np, "ntc_table", &length)) {
		pdata->ntc_size = 0;
	} else {
		/* get ntc degree base value */
		ret = of_property_read_s32(np, "ntc_degree_from_v2",
					   &pdata->ntc_degree_from);
		if (ret) {
			dev_err(dev, "invalid ntc_degree_from_v2\n");
			return -EINVAL;
		}

		pdata->ntc_size = length / sizeof(u32);
	}

	if (pdata->ntc_size) {
		size = sizeof(*pdata->ntc_table) * pdata->ntc_size;
		pdata->ntc_table = devm_kzalloc(di->dev, size, GFP_KERNEL);
		if (!pdata->ntc_table)
			return -ENOMEM;

		ret = of_property_read_u32_array(np, "ntc_table",
						 pdata->ntc_table,
						 pdata->ntc_size);
		if (ret < 0)
			return ret;

		if (pdata->ntc_table[0] < NTC_80UA_MAX_MEASURE)
			pdata->ntc_factor = NTC_CALC_FACTOR_80UA;
		else if (pdata->ntc_table[0] < NTC_60UA_MAX_MEASURE)
			pdata->ntc_factor = NTC_CALC_FACTOR_60UA;
		else if (pdata->ntc_table[0] < NTC_40UA_MAX_MEASURE)
			pdata->ntc_factor = NTC_CALC_FACTOR_40UA;
		else
			pdata->ntc_factor = NTC_CALC_FACTOR_20UA;
	}

	ret = parse_temperature_chrg_table(di, np);
	if (ret)
		return ret;

	DBG("the battery dts info dump:\n"
	    "bat_res:%d\n"
	    "res_sample:%d\n"
	    "max_input_currentmA:%d\n"
	    "max_chrg_current:%d\n"
	    "max_chrg_voltage:%d\n"
	    "design_capacity:%d\n"
	    "design_qmax :%d\n"
	    "sleep_enter_current:%d\n"
	    "sleep_exit_current:%d\n"
	    "sleep_filter_current:%d\n"
	    "zero_algorithm_vol:%d\n"
	    "zero_reserve_dsoc:%d\n"
	    "monitor_sec:%d\n"
	    "power_dc2otg:%d\n"
	    "max_soc_offset:%d\n"
	    "virtual_power:%d\n"
	    "pwroff_vol:%d\n"
	    "dc_det_adc:%d\n"
	    "ntc_factor:%d\n"
	    "ntc_size=%d\n"
	    "ntc_degree_from_v2:%d\n"
	    "ntc_degree_to:%d\n",
	    pdata->bat_res, pdata->sample_res, pdata->max_input_current,
	    pdata->max_chrg_current, pdata->max_chrg_voltage,
	    pdata->design_capacity, pdata->design_qmax,
	    pdata->sleep_enter_current, pdata->sleep_exit_current,
	    pdata->sleep_filter_current, pdata->zero_algorithm_vol,
	    pdata->zero_reserve_dsoc, pdata->monitor_sec, pdata->power_dc2otg,
	    pdata->max_soc_offset, pdata->bat_mode, pdata->pwroff_vol,
	    pdata->dc_det_adc, pdata->ntc_factor,
	    pdata->ntc_size, pdata->ntc_degree_from,
	    pdata->ntc_degree_from + pdata->ntc_size - 1
	    );

	return 0;
}

static const struct of_device_id rk816_battery_of_match[] = {
	{.compatible = "rk816-battery",},
	{ },
};

static int rk816_battery_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(rk816_battery_of_match, &pdev->dev);
	struct rk816_battery *di;
	struct rk808 *rk816 = dev_get_drvdata(pdev->dev.parent);
	int ret;

	if (!of_id) {
		dev_err(&pdev->dev, "Failed to find matching dt id\n");
		return -ENODEV;
	}

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->rk816 = rk816;
	di->pdev = pdev;
	di->dev = &pdev->dev;
	di->regmap = rk816->regmap;
	platform_set_drvdata(pdev, di);

	ret = rk816_bat_parse_dt(di);
	if (ret < 0) {
		dev_err(&pdev->dev, "rk816 battery parse dt failed!\n");
		return ret;
	}

	if (!is_rk816_bat_exist(di)) {
		di->pdata->bat_mode = MODE_VIRTUAL;
		dev_err(&pdev->dev, "no battery, virtual power mode\n");
	}

	ret = rk816_bat_init_power_supply(di);
	if (ret) {
		dev_err(&pdev->dev, "rk816 power supply register failed!\n");
		return ret;
	}

	rk816_bat_init_info(di);
	rk816_bat_init_fg(di);
	rk816_bat_init_leds(di);
	rk816_bat_init_charger(di);
	rk816_bat_init_sysfs(di);
	rk816_bat_register_fb_notify(di);
	wake_lock_init(&di->wake_lock, WAKE_LOCK_SUSPEND, "rk816_bat_lock");
	di->bat_monitor_wq = alloc_ordered_workqueue("%s",
			WQ_MEM_RECLAIM | WQ_FREEZABLE, "rk816-bat-monitor-wq");
	INIT_DELAYED_WORK(&di->bat_delay_work, rk816_battery_work);

	ret = rk816_bat_init_irqs(di);
	if (ret) {
		dev_err(&pdev->dev, "rk816 bat irq init failed!\n");
		goto irq_fail;
	}

	queue_delayed_work(di->bat_monitor_wq, &di->bat_delay_work,
			   msecs_to_jiffies(TIMER_MS_COUNTS * 5));

	BAT_INFO("driver version %s\n", DRIVER_VERSION);

	return 0;

irq_fail:
	cancel_delayed_work(&di->dc_delay_work);
	cancel_delayed_work(&di->bat_delay_work);
	cancel_delayed_work(&di->calib_delay_work);
	destroy_workqueue(di->bat_monitor_wq);
	destroy_workqueue(di->usb_charger_wq);
	rk816_bat_unregister_fb_notify(di);
	del_timer(&di->caltimer);
	wake_lock_destroy(&di->wake_lock);

	return ret;
}

static int rk816_battery_suspend(struct platform_device *dev,
				 pm_message_t state)
{
	struct rk816_battery *di = platform_get_drvdata(dev);
	u8 st;

	cancel_delayed_work_sync(&di->bat_delay_work);
	di->s2r = false;
	di->sleep_chrg_online = rk816_bat_chrg_online(di);
	di->sleep_chrg_status = rk816_bat_get_chrg_status(di);
	di->current_avg = rk816_bat_get_avg_current(di);
	di->remain_cap = rk816_bat_get_coulomb_cap(di);
	di->rsoc = rk816_bat_get_rsoc(di);
	di->rtc_base = rk816_get_rtc_sec();
	rk816_bat_save_data(di);
	st = (rk816_bat_read(di, RK816_SUP_STS_REG) & CHRG_STATUS_MSK) >> 4;
	di->slp_dcdc_en_reg = rk816_bat_read(di, RK816_SLP_DCDC_EN_REG);

	/* enable sleep boost5v and otg5v */
	if (di->pdata->otg5v_suspend_enable) {
		if ((di->otg_in && !di->dc_in) ||
		    (di->otg_in && di->dc_in && !di->pdata->power_dc2otg)) {
			rk816_bat_set_bits(di, RK816_SLP_DCDC_EN_REG,
					   OTG_BOOST_SLP_ON, OTG_BOOST_SLP_ON);
			BAT_INFO("suspend: otg 5v on\n");
		} else {
			/* disable sleep otg5v */
			rk816_bat_set_bits(di, RK816_SLP_DCDC_EN_REG,
					   OTG_BOOST_SLP_ON, 0);
			BAT_INFO("suspend: otg 5v off\n");
		}
	} else {
		/* disable sleep otg5v */
		rk816_bat_set_bits(di, RK816_SLP_DCDC_EN_REG,
				   OTG_BOOST_SLP_ON, 0);
		BAT_INFO("suspend: otg 5v off\n");
	}

	/* if not CHARGE_FINISH, reinit chrg_finish_base.
	 * avoid sleep loop in suspend and resume all the time
	 */
	if (di->sleep_chrg_status != CHARGE_FINISH)
		di->chrg_finish_base = get_boot_sec();

	/* avoid: enter suspend from MODE_ZERO: load from heavy to light */
	if ((di->work_mode == MODE_ZERO) &&
	    (di->sleep_chrg_online) && (di->current_avg >= 0)) {
		DBG("suspend: MODE_ZERO exit...\n");
		/* it need't do prepare for mode finish and smooth, it will
		 * be done in display_smooth
		 */
		if (di->sleep_chrg_status == CHARGE_FINISH) {
			di->work_mode = MODE_FINISH;
			di->chrg_finish_base = get_boot_sec();
		} else {
			di->work_mode = MODE_SMOOTH;
			rk816_bat_smooth_algo_prepare(di);
		}
	}

	BAT_INFO("suspend: dl=%d rl=%d c=%d v=%d cap=%d at=%ld ch=%d st=%s\n",
		 di->dsoc, di->rsoc, di->current_avg,
		 rk816_bat_get_avg_voltage(di), rk816_bat_get_coulomb_cap(di),
		 di->sleep_dischrg_sec, di->sleep_chrg_online, bat_status[st]);

	return 0;
}

static int rk816_battery_resume(struct platform_device *dev)
{
	int interval_sec, pwroff_vol, time_step = DISCHRG_TIME_STEP1;
	struct rk816_battery *di = platform_get_drvdata(dev);
	u8 st;

	di->s2r = true;
	di->voltage_avg = rk816_bat_get_avg_voltage(di);
	di->current_avg = rk816_bat_get_avg_current(di);
	di->voltage_relax = rk816_bat_get_relax_voltage(di);
	di->current_relax = rk816_bat_get_relax_current(di);
	di->remain_cap = rk816_bat_get_coulomb_cap(di);
	di->rsoc = rk816_bat_get_rsoc(di);
	interval_sec = rk816_bat_rtc_sleep_sec(di);
	di->sleep_sum_sec += interval_sec;
	pwroff_vol = di->pdata->pwroff_vol;
	st = (rk816_bat_read(di, RK816_SUP_STS_REG) & CHRG_STATUS_MSK) >> 4;
	/* resume sleep boost5v and otg5v */
	rk816_bat_set_bits(di, RK816_SLP_DCDC_EN_REG,
			   OTG_BOOST_SLP_ON, di->slp_dcdc_en_reg);

	if (!di->sleep_chrg_online) {
		/* only add up discharge sleep seconds */
		di->sleep_dischrg_sec += interval_sec;
		if (di->voltage_avg <= pwroff_vol + 50)
			time_step = DISCHRG_TIME_STEP1;
		else
			time_step = DISCHRG_TIME_STEP2;
	}

	BAT_INFO("resume: dl=%d rl=%d c=%d v=%d rv=%d cap=%d dt=%d at=%ld ch=%d st=%s\n",
		 di->dsoc, di->rsoc, di->current_avg, di->voltage_avg,
		 di->voltage_relax, rk816_bat_get_coulomb_cap(di), interval_sec,
		 di->sleep_dischrg_sec, di->sleep_chrg_online, bat_status[st]);

	/* sleep: enough time and discharge */
	if ((di->sleep_dischrg_sec > time_step) && (!di->sleep_chrg_online)) {
		if (rk816_bat_sleep_dischrg(di))
			di->sleep_dischrg_sec = 0;
	}

	rk816_bat_save_data(di);

	/* charge/lowpower lock: for battery work to update dsoc and rsoc */
	if ((di->sleep_chrg_online) ||
	    (!di->sleep_chrg_online && di->voltage_avg <= pwroff_vol))
		wake_lock_timeout(&di->wake_lock, msecs_to_jiffies(2000));

	queue_delayed_work(di->bat_monitor_wq, &di->bat_delay_work,
			   msecs_to_jiffies(1000));

	return 0;
}

static void rk816_battery_shutdown(struct platform_device *dev)
{
	u8 cnt = 0;
	struct rk816_battery *di = platform_get_drvdata(dev);

	extcon_unregister_notifier(di->cable_edev, EXTCON_CHG_USB_SDP,
				   &di->cable_cg_nb);
	extcon_unregister_notifier(di->cable_edev, EXTCON_CHG_USB_DCP,
				   &di->cable_cg_nb);
	extcon_unregister_notifier(di->cable_edev, EXTCON_CHG_USB_CDP,
				   &di->cable_cg_nb);
	extcon_unregister_notifier(di->cable_edev, EXTCON_USB_VBUS_EN,
				   &di->cable_host_nb);
	extcon_unregister_notifier(di->cable_edev, EXTCON_USB,
				   &di->cable_discnt_nb);

	rk816_bat_unregister_fb_notify(di);
	cancel_delayed_work_sync(&di->dc_delay_work);
	cancel_delayed_work_sync(&di->bat_delay_work);
	cancel_delayed_work_sync(&di->calib_delay_work);
	cancel_delayed_work_sync(&di->usb_work);
	cancel_delayed_work_sync(&di->host_work);
	cancel_delayed_work_sync(&di->discnt_work);
	destroy_workqueue(di->bat_monitor_wq);
	destroy_workqueue(di->usb_charger_wq);

	del_timer(&di->caltimer);
	rk816_bat_set_otg_power(di, USB_OTG_POWER_OFF);

	if (base2sec(di->boot_base) < REBOOT_PERIOD_SEC)
		cnt = rk816_bat_check_reboot(di);
	else
		rk816_bat_save_reboot_cnt(di, 0);

	BAT_INFO("shutdown: dl=%d rl=%d c=%d v=%d cap=%d f=%d ch=%d otg=%d 5v=%d n=%d mode=%d rest=%d\n",
		 di->dsoc, di->rsoc, di->current_avg, di->voltage_avg,
		 di->remain_cap, di->fcc, rk816_bat_chrg_online(di),
		 di->otg_in, di->otg_pmic5v, cnt,
		 di->algo_rest_mode, di->algo_rest_val);
}

static struct platform_driver rk816_battery_driver = {
	.probe = rk816_battery_probe,
	.suspend = rk816_battery_suspend,
	.resume = rk816_battery_resume,
	.shutdown = rk816_battery_shutdown,
	.driver = {
		.name = "rk816-battery",
		.of_match_table = rk816_battery_of_match,
	},
};

static int __init battery_init(void)
{
	return platform_driver_register(&rk816_battery_driver);
}
fs_initcall_sync(battery_init);

static void __exit battery_exit(void)
{
	platform_driver_unregister(&rk816_battery_driver);
}
module_exit(battery_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk816-battery");
MODULE_AUTHOR("chenjh<chenjh@rock-chips.com>");
