/*
 * rk818  battery driver
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * */

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
#include <linux/power/rk818_battery.h>
#include <linux/mfd/rk818.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/wakelock.h>
#include <linux/of_gpio.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>

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
#define DEFAULT_VLMT			4200
#define DEFAULT_ILMT			2000
#define DEFAULT_ICUR			1600

#define DEF_TEST_ILMT_MA		2000
#define DEF_TEST_CURRENT_MA		1800

#define DSOC_DISCHRG_FAST_DEC_SEC	120	/*seconds*/
#define DSOC_DISCHRG_FAST_EER_RANGE	25
#define DSOC_CHRG_FAST_CALIB_CURR_MAX	400	/*mA*/
#define DSOC_CHRG_FAST_INC_SEC		120	/*seconds*/
#define DSOC_CHRG_FAST_EER_RANGE	15
#define DSOC_CHRG_EMU_CURR		1200
#define DSOC_CHG_TERM_CURR		600
#define DSOC_CHG_TERM_VOL		4100
#define	CHG_FINISH_VOL			4100

/*realtime RSOC calib param*/
#define RSOC_DISCHG_ERR_LOWER	40
#define RSOC_DISCHG_ERR_UPPER	50
#define RSOC_ERR_CHCK_CNT	15
#define RSOC_COMPS		20	/*compensation*/
#define RSOC_CALIB_CURR_MAX	900	/*mA*/
#define RSOC_CALIB_DISCHGR_TIME	3	/*min*/

#define RSOC_RESUME_ERR		10
#define REBOOT_INTER_MIN	1

#define INTERPOLATE_MAX		1000
#define MAX_INT			0x7FFF
#define TIME_10MIN_SEC		600

#define CHG_VOL_SHIFT		4
#define CHG_ILIM_SHIFT		0
#define CHG_ICUR_SHIFT		0
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

/*
 * the following table value depends on datasheet
 */
int CHG_V_LMT[] = {4050, 4100, 4150, 4200, 4300, 4350};

int CHG_I_CUR[] = {1000, 1200, 1400, 1600, 1800, 2000,
		   2250, 2400, 2600, 2800, 3000};

int CHG_I_LMT[] = {450, 800, 850, 1000, 1250, 1500, 1750,
		   2000, 2250, 2500, 2750, 3000};

u8 CHG_CVCC_HOUR[] = {4, 5, 6, 8, 10, 12, 14, 16};

#define RK818_DC_IN		0
#define RK818_DC_OUT		1
#define SEC_TO_MIN(x)		((x)/60)
#define BASE_TO_MIN(x)		((get_seconds()-(x))/60)
#define BASE_TO_SEC(x)		(get_seconds()-(x))

#define	OCV_VALID_SHIFT		(0)
#define	OCV_CALIB_SHIFT		(1)
#define FIRST_PWRON_SHIFT	(2)


struct battery_info {
	struct device			*dev;
	struct cell_state		cell;
	struct power_supply		bat;
	struct power_supply		ac;
	struct power_supply		usb;
	struct delayed_work		work;
	/* struct i2c_client		*client; */
	struct rk818			*rk818;
	struct pinctrl			*pinctrl;
	struct pinctrl_state		*pins_default;


	struct battery_platform_data	*platform_data;

	int				dc_det_pin;
	int				dc_det_level;
	int				dc_det_pullup_inside;
	int				work_on;
	int				irq;
	int				ac_online;
	int				usb_online;
	int				dc_online;
	int				status;
	int				current_avg;
	int				current_offset;

	uint16_t			voltage;
	uint16_t			voltage_ocv;
	uint16_t			relax_voltage;
	u8				charge_status;
	u8				otg_status;
	int				pcb_ioffset;
	bool				pcb_ioffset_updated;
	unsigned long			queue_work_cnt;
	u32				term_chg_cnt;
	u32				emu_chg_cnt;

	uint16_t			warnning_voltage;

	int				design_capacity;
	int				fcc;
	int				qmax;
	int				remain_capacity;
	int				nac;
	int				temp_nac;
	int				real_soc;
	int				display_soc;
	int				odd_capacity;
	int				temp_soc;

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

	int				zero_updated;
	int				old_display_soc;
	int				zero_cycle;


	int				update_k;
	int				line_k;
	int				voltage_old;

	int				q_dead;
	int				q_err;
	int				q_shtd;

	u8				check_count;
	/* u32				status; */
	struct timeval			soc_timer;
	struct timeval			change_timer;

	int				vol_smooth_time;
	int				charge_smooth_time;
	int				sum_suspend_cap;
	int				suspend_cap;
	int				resume_capacity;
	struct timespec			suspend_time;
	struct timespec			resume_time;
	unsigned long			suspend_time_start;
	unsigned long			count_sleep_time;

	int				suspend_rsoc;
	int				sleep_status;
	int				suspend_charge_current;
	int				resume_soc;
	int				bat_res;
	bool				bat_res_updated;
	bool				charge_smooth_status;
	bool				resume;
	unsigned long			last_plugin_time;
	bool				sys_wakeup;

	unsigned long			charging_time;
	unsigned long			discharging_time;
	unsigned long			finish_time;

	u32				charge_min;
	u32				discharge_min;
	u32				finish_min;
	struct notifier_block		battery_nb;
	struct workqueue_struct		*wq;
	struct delayed_work		battery_monitor_work;
	struct delayed_work		charge_check_work;
	int				charge_otg;

	struct wake_lock		resume_wake_lock;
	unsigned long			sys_on_base;
	unsigned long			chrg_time_base;
	int				chrg_time2_full;
	int				chrg_cap2_full;

	bool				is_first_poweron;
	int				first_on_cap;


	int				fg_drv_mode;
	int				test_chrg_current;
	int				test_chrg_ilmt;
	int				debug_finish_real_soc;
	int				debug_finish_temp_soc;
	int				chrg_min[10];
	int				chg_v_lmt;
	int				chg_i_lmt;
	int				chg_i_cur;

};

struct battery_info *g_battery;
u32 support_uboot_chrg, support_usb_adp, support_dc_adp;
static void rk81x_update_battery_info(struct battery_info *di);

static bool rk81x_support_adp_type(enum hw_support_adp_t type)
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

static u32 interpolate(int value, u32 *table, int size)
{
	uint8_t i;
	uint16_t d;

	for (i = 0; i < size; i++) {
		if (value < table[i])
			break;
	}

	if ((i > 0) && (i < size)) {
		d = (value - table[i-1]) * (INTERPOLATE_MAX/(size-1));
		d /= table[i] - table[i-1];
		d = d + (i-1) * (INTERPOLATE_MAX/(size-1));
	} else {
		d = i * ((INTERPOLATE_MAX+size/2)/size);
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

		tmp = ((int32_t) a*b + (c>>1)) / c;

		if (tmp < MAX_INT)
			ans = tmp;
	}

	if (sign)
		ans = -ans;

	return ans;
}

static  int32_t abs_int(int32_t x)
{
	return (x > 0) ? x : -x;
}

static  int abs32_int(int x)
{
	return (x > 0) ? x : -x;
}

static int div(int val)
{
	return (val == 0) ? 1 : val;
}

static int battery_read(struct rk818 *rk818, u8 reg,
			u8 buf[], unsigned len)
{
	int ret;

	ret = rk818_i2c_read(rk818, reg, len, buf);
	return ret;
}

static int battery_write(struct rk818 *rk818, u8 reg,
			 u8 const buf[], unsigned len)
{
	int ret;

	ret = rk818_i2c_write(rk818, reg, (int)len, *buf);
	return ret;
}

static void rk81x_set_bit(struct battery_info *di, u8 reg, u8 shift)
{
	rk818_set_bits(di->rk818, reg, 1 << shift, 1 << shift);
}

static void rk81x_clr_bit(struct battery_info *di, u8 reg, u8 shift)
{
	rk818_set_bits(di->rk818, reg, 1 << shift, 0 << shift);
}

static u8 rk81x_read_bit(struct battery_info *di, u8 reg, u8 shift)
{
	u8 buf;
	u8 val;

	battery_read(di->rk818, reg, &buf, 1);
	val = (buf & BIT(shift)) >> shift;
	return val;
}

static void dump_gauge_register(struct battery_info *di)
{
	int i = 0;
	char buf;

	DBG("%s dump charger register start:\n", __func__);
	for (i = 0xAC; i < 0xDF; i++) {
		battery_read(di->rk818, i, &buf, 1);
		DBG(" the register is  0x%02x, the value is 0x%02x\n", i, buf);
	}
	DBG("demp end!\n");
}

static void dump_charger_register(struct battery_info *di)
{
	int i = 0;
	char buf;

	DBG("%s dump the register start:\n", __func__);
	for (i = 0x99; i < 0xAB; i++) {
		battery_read(di->rk818, i, &buf, 1);
		DBG(" the register is  0x%02x, the value is 0x%02x\n", i, buf);
	}
	DBG("demp end!\n");
}

#if RK818_SYS_DBG

static void  _capacity_init(struct battery_info *di, u32 capacity);

/*
 * interface for debug: do rsoc_first_poweron_init() without unloading battery
 */
static ssize_t bat_calib_read(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;
	int val;

	val = rk81x_read_bit(di, MISC_MARK_REG, OCV_CALIB_SHIFT);
	return sprintf(buf, "%d\n", val);
}

static ssize_t bat_calib_write(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	u8 val;
	int ret;
	struct battery_info *di = g_battery;

	ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val)
		rk81x_set_bit(di, MISC_MARK_REG, OCV_CALIB_SHIFT);
	else
		rk81x_clr_bit(di, MISC_MARK_REG, OCV_CALIB_SHIFT);
	return count;
}

/*
 * interface for debug: force battery to over discharge
 */
static ssize_t bat_test_power_read(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d\n", di->fg_drv_mode);
}

static ssize_t bat_test_power_write(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	u8 val;
	int ret;
	struct battery_info *di = g_battery;

	ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val == 1)
		di->fg_drv_mode = TEST_POWER_MODE;
	else
		di->fg_drv_mode = FG_NORMAL_MODE;

	return count;
}


static ssize_t bat_state_read(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "dsoc = %d, rsoc = %d\n",
				di->real_soc, di->temp_soc);
}

static ssize_t bat_fcc_read(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->fcc);
}

static ssize_t bat_fcc_write(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	u16 val;
	int ret;
	struct battery_info *di = g_battery;

	ret = kstrtou16(buf, 0, &val);
	if (ret < 0)
		return ret;

	di->fcc = val;

	return count;
}


static ssize_t bat_soc_read(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->real_soc);
}

static ssize_t bat_soc_write(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	u8 val;
	int ret;
	struct battery_info *di = g_battery;

	ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
		return ret;

	di->real_soc = val;

	return count;
}
static ssize_t bat_temp_soc_read(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->temp_soc);
}

static ssize_t bat_temp_soc_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	u8 val;
	int ret;
	u32 capacity;
	struct battery_info *di = g_battery;

	ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
		return ret;

	capacity = di->fcc*val/100;
	_capacity_init(di, capacity);

	return count;
}

static ssize_t bat_voltage_read(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->voltage);
}

static ssize_t bat_avr_current_read(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->current_avg);
}

static ssize_t bat_remain_cap_read(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->remain_capacity);
}

/*
 * interface for debug: debug info switch
 */
static ssize_t bat_debug_write(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	u8 val;
	int ret;

	ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
		return ret;

	dbg_enable = val;

	return count;
}

static ssize_t bat_regs_read(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	u32 i;
	u32 start_offset = 0x0;
	u32 end_offset = 0xf2;
	struct battery_info *di = g_battery;
	u8 val;
	char *str = buf;

	str += sprintf(str, "start from add=0x%x, offset=0x%x\n",
		       start_offset, end_offset);

	for (i = start_offset; i <= end_offset; ) {
		battery_read(di->rk818, i, &val, 1);
		str += sprintf(str, "0x%x=0x%x", i, val);

		if (i % 4 == 0) {
			str += sprintf(str, "\n");
		} else {
			if (i != end_offset)
				str += sprintf(str, "	");
			else
				str += sprintf(str, "\n");
		}
		i++;
	}
	return (str - buf);
}


static struct device_attribute rk818_bat_attr[] = {
	__ATTR(fcc, 0664, bat_fcc_read, bat_fcc_write),
	__ATTR(soc, 0664, bat_soc_read, bat_soc_write),
	__ATTR(temp_soc, 0664, bat_temp_soc_read, bat_temp_soc_write),
	__ATTR(voltage, 0664, bat_voltage_read, NULL),
	__ATTR(avr_current, 0664, bat_avr_current_read, NULL),
	__ATTR(remain_capacity, 0664, bat_remain_cap_read, NULL),
	__ATTR(debug, 0664, NULL, bat_debug_write),
	__ATTR(regs, 0664, bat_regs_read, NULL),
	__ATTR(state, 0664, bat_state_read, NULL),
	__ATTR(test_power, 0664, bat_test_power_read, bat_test_power_write),
	__ATTR(calib, 0664, bat_calib_read, bat_calib_write),
};

#endif

static uint16_t get_relax_voltage(struct battery_info *di);

static ssize_t show_state_attrs(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct battery_info *data = g_battery;

	if (0 == get_relax_voltage(data)) {
		return sprintf(buf,
			"voltage = %d, remain_capacity = %d, status = %d\n",
			data->voltage, data->remain_capacity,
			data->status);

	} else
		return sprintf(buf,
			"voltage = %d, remain_capacity = %d, status = %d\n",
			get_relax_voltage(data), data->remain_capacity,
			data->status);
}

static ssize_t restore_state_attrs(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	return size;
}
static struct device_attribute rkbatt_attrs[] = {
	__ATTR(state, 0664, show_state_attrs, restore_state_attrs),
};

static int create_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rkbatt_attrs); i++) {
		if (device_create_file(dev, rkbatt_attrs + i))
			goto error;
	}

	return 0;

error:
	for (; i >= 0; i--)
		device_remove_file(dev, rkbatt_attrs + i);

	dev_err(dev, "%s:Unable to create sysfs interface\n", __func__);
	return -1;
}

static int  _gauge_enable(struct battery_info *di)
{
	int ret;
	u8 buf;


	ret = battery_read(di->rk818, TS_CTRL_REG, &buf, 1);
	if (ret < 0) {
		dev_err(di->dev, "error reading TS_CTRL_REG");
		return ret;
	}
	if (!(buf & GG_EN)) {
		buf |= GG_EN;
		ret = battery_write(di->rk818, TS_CTRL_REG, &buf, 1);/*enable*/
		ret = battery_read(di->rk818, TS_CTRL_REG, &buf, 1);
		return 0;
	}

	DBG("%s, %d\n", __func__, buf);
	return 0;
}

static void save_level(struct  battery_info *di, u8 save_soc)
{
	u8 soc;

	soc = save_soc;
	battery_write(di->rk818, UPDAT_LEVE_REG, &soc, 1);
}
static u8 get_level(struct  battery_info *di)
{
	u8 soc;

	battery_read(di->rk818, UPDAT_LEVE_REG, &soc, 1);
	return soc;
}

static int _get_vcalib0(struct battery_info *di)
{
	int ret;
	int temp = 0;
	u8 buf;

	ret = battery_read(di->rk818, VCALIB0_REGL, &buf, 1);
	temp = buf;
	ret = battery_read(di->rk818, VCALIB0_REGH, &buf, 1);
	temp |= buf<<8;

	DBG("%s voltage0 offset vale is %d\n", __func__, temp);
	return temp;
}

static int _get_vcalib1(struct  battery_info *di)
{
	int ret;
	int temp = 0;
	u8 buf;

	ret = battery_read(di->rk818, VCALIB1_REGL, &buf, 1);
	temp = buf;
	ret = battery_read(di->rk818, VCALIB1_REGH, &buf, 1);
	temp |= buf<<8;

	DBG("%s voltage1 offset vale is %d\n", __func__, temp);
	return temp;
}

static int _get_ioffset(struct battery_info *di)
{
	int ret;
	int temp = 0;
	u8 buf;

	ret = battery_read(di->rk818, IOFFSET_REGL, &buf, 1);
	temp = buf;
	ret = battery_read(di->rk818, IOFFSET_REGH, &buf, 1);
	temp |= buf<<8;

	return temp;
}

static uint16_t  _get_cal_offset(struct battery_info *di)
{
	int ret;
	uint16_t temp = 0;
	u8 buf;

	ret = battery_read(di->rk818, CAL_OFFSET_REGL, &buf, 1);
	temp = buf;
	ret = battery_read(di->rk818, CAL_OFFSET_REGH, &buf, 1);
	temp |= buf<<8;

	return temp;
}
static int _set_cal_offset(struct battery_info *di, u32 value)
{
	int ret;
	u8 buf;

	buf = value&0xff;
	ret = battery_write(di->rk818, CAL_OFFSET_REGL, &buf, 1);
	buf = (value >> 8)&0xff;
	ret = battery_write(di->rk818, CAL_OFFSET_REGH, &buf, 1);

	return 0;
}
static void _get_voltage_offset_value(struct battery_info *di)
{
	int vcalib0, vcalib1;

	vcalib0 = _get_vcalib0(di);
	vcalib1 = _get_vcalib1(di);

	di->voltage_k = (4200 - 3000)*1000/div((vcalib1 - vcalib0));
	di->voltage_b = 4200 - (di->voltage_k*vcalib1)/1000;
	DBG("voltage_k=%d(x1000),voltage_b=%d\n", di->voltage_k, di->voltage_b);
}
static uint16_t _get_OCV_voltage(struct battery_info *di)
{
	int ret;
	u8 buf;
	uint16_t temp;
	uint16_t voltage_now = 0;
	int i;
	int val[3];

	for (i = 0; i < 3; i++) {
		ret = battery_read(di->rk818, BAT_OCV_REGL, &buf, 1);
		val[i] = buf;
		ret = battery_read(di->rk818, BAT_OCV_REGH, &buf, 1);
		val[i] |= buf<<8;

		if (ret < 0) {
			dev_err(di->dev, "error read BAT_OCV_REGH");
			return ret;
		}
	}

	if (val[0] == val[1])
		temp = val[0];
	else
		temp = val[2];

	voltage_now = di->voltage_k*temp/1000 + di->voltage_b;

	return voltage_now;
}

static int _get_battery_voltage(struct battery_info *di)
{
	int ret;
	int voltage_now = 0;
	u8 buf;
	int temp;
	int val[3];
	int i;

	for (i = 0; i < 3; i++) {
		ret = battery_read(di->rk818, BAT_VOL_REGL, &buf, 1);
		val[i] = buf;
		ret = battery_read(di->rk818, BAT_VOL_REGH, &buf, 1);
		val[i] |= buf<<8;

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

	voltage_now = di->voltage_k*temp/1000 + di->voltage_b;

	return voltage_now;
}

/* OCV Lookup table
 * Open Circuit Voltage (OCV) correction routine. This function estimates SOC,
 * based on the voltage.
 */
static int _voltage_to_capacity(struct battery_info *di, int voltage)
{
	u32 *ocv_table;
	int ocv_size;
	u32 tmp;
	int ocv_soc;

	ocv_table = di->platform_data->battery_ocv;
	ocv_size = di->platform_data->ocv_size;
	di->warnning_voltage = ocv_table[3];
	tmp = interpolate(voltage, ocv_table, ocv_size);
	ocv_soc = ab_div_c(tmp, MAX_PERCENTAGE, INTERPOLATE_MAX);
	di->temp_nac = ab_div_c(tmp, di->fcc, INTERPOLATE_MAX);

	return ocv_soc;
}

static uint16_t _get_relax_vol1(struct battery_info *di)
{
	int ret;
	u8 buf;
	uint16_t temp = 0, voltage_now;

	ret = battery_read(di->rk818, RELAX_VOL1_REGL, &buf, 1);
	temp = buf;
	ret = battery_read(di->rk818, RELAX_VOL1_REGH, &buf, 1);
	temp |= (buf<<8);

	voltage_now = di->voltage_k*temp/1000 + di->voltage_b;

	return voltage_now;
}

static uint16_t _get_relax_vol2(struct battery_info *di)
{
	int ret;
	uint16_t temp = 0, voltage_now;
	u8 buf;

	ret = battery_read(di->rk818, RELAX_VOL2_REGL, &buf, 1);
	temp = buf;
	ret = battery_read(di->rk818, RELAX_VOL2_REGH, &buf, 1);
	temp |= (buf<<8);

	voltage_now = di->voltage_k*temp/1000 + di->voltage_b;

	return voltage_now;
}

static int  _get_raw_adc_current(struct battery_info *di)
{
	u8 buf;
	int ret;
	int current_now;

	ret = battery_read(di->rk818, BAT_CUR_AVG_REGL, &buf, 1);
	if (ret < 0) {
		dev_err(di->dev, "error reading BAT_CUR_AVG_REGL");
		return ret;
	}
	current_now = buf;
	ret = battery_read(di->rk818, BAT_CUR_AVG_REGH, &buf, 1);
	if (ret < 0) {
		dev_err(di->dev, "error reading BAT_CUR_AVG_REGH");
		return ret;
	}
	current_now |= (buf<<8);

	if (ret < 0) {
		dev_err(di->dev, "error reading BAT_CUR_AVG_REGH");
		return ret;
	}

	return current_now;
}

static void reset_zero_var(struct battery_info *di)
{
	di->update_k = 0;
	di->q_err = 0;
	di->voltage_old = 0;
	di->display_soc = 0;
}

static void ioffset_sample_time(struct battery_info *di, int time)
{
	u8 ggcon;

	battery_read(di->rk818, GGCON, &ggcon, 1);
	ggcon &= ~(0x30); /*clear <5:4>*/
	ggcon |= time;
	battery_write(di->rk818, GGCON, &ggcon, 1);
}

static void update_cal_offset(struct battery_info *di)
{
	int mod = di->queue_work_cnt % TIME_10MIN_SEC;
	u8 pcb_offset;

	battery_read(di->rk818, PCB_IOFFSET_REG, &pcb_offset, 1);
	DBG("<%s>, queue_work_cnt = %lu, mod = %d\n",
	    __func__, di->queue_work_cnt, mod);
	if ((!mod) && (di->pcb_ioffset_updated)) {
		_set_cal_offset(di, _get_ioffset(di)+pcb_offset);
		DBG("<%s>. 10min update cal_offset = %d",
		    __func__, di->pcb_ioffset+_get_ioffset(di));
	}
}

/*
 * when charger finish signal comes, we need calibrate the current, make it
 * close to 0.
 */
static void zero_current_calib(struct battery_info *di)
{
	int adc_value;
	uint16_t C0;
	uint16_t C1;
	int ioffset;
	u8 pcb_offset;
	u8 retry = 0;

	if ((di->charge_status == CHARGE_FINISH) &&
	    (abs32_int(di->current_avg) > 4)) {
		for (retry = 0; retry < 5; retry++) {
			adc_value = _get_raw_adc_current(di);
			if (adc_value > 2047)
				adc_value -= 4096;

			DBG("<%s>. adc_value = %d\n", __func__, adc_value);
			C0 = _get_cal_offset(di);
			C1 = adc_value + C0;
			DBG("<%s>. C0(cal_offset) = %d, C1 = %d\n",
			    __func__, C0, C1);
			_set_cal_offset(di, C1);
			DBG("<%s>. new cal_offset = %d\n",
			    __func__, _get_cal_offset(di));
			msleep(2000);

			adc_value = _get_raw_adc_current(di);
			DBG("<%s>. adc_value = %d\n", __func__, adc_value);
			if (adc_value < 4) {
				if (_get_cal_offset(di) < 0x7ff)
					_set_cal_offset(di, di->current_offset+
							42);
				else {
					ioffset = _get_ioffset(di);
					pcb_offset = C1 - ioffset;
					di->pcb_ioffset = pcb_offset;
					di->pcb_ioffset_updated  = true;
					battery_write(di->rk818,
						      PCB_IOFFSET_REG,
						      &pcb_offset, 1);
				}
				DBG("<%s>. update the cal_offset, C1 = %d\n"
				    "i_offset = %d, pcb_offset = %d\n",
					__func__, C1, ioffset, pcb_offset);
				break;
			} else {
				di->pcb_ioffset_updated  = false;
			}
		}
	}
}


static bool  _is_relax_mode(struct battery_info *di)
{
	int ret;
	u8 status;

	ret = battery_read(di->rk818, GGSTS, &status, 1);

	if ((!(status&RELAX_VOL1_UPD)) || (!(status&RELAX_VOL2_UPD)))
		return false;
	else
		return true;
}

static uint16_t get_relax_voltage(struct battery_info *di)
{
	int ret;
	u8 status;
	uint16_t relax_vol1, relax_vol2;
	u8 ggcon;

	ret = battery_read(di->rk818, GGSTS, &status, 1);
	ret = battery_read(di->rk818, GGCON, &ggcon, 1);

	relax_vol1 = _get_relax_vol1(di);
	relax_vol2 = _get_relax_vol2(di);
	DBG("<%s>. GGSTS=0x%x, GGCON=0x%x, relax_vol1=%d, relax_vol2=%d\n",
	    __func__, status, ggcon, relax_vol1, relax_vol2);

	if (_is_relax_mode(di))
		return relax_vol1 > relax_vol2 ? relax_vol1 : relax_vol2;
	else
		return 0;
}

static void  _set_relax_thres(struct battery_info *di)
{
	u8 buf;
	int enter_thres, exit_thres;
	struct cell_state *cell = &di->cell;

	enter_thres = (cell->config->ocv->sleep_enter_current)*1000/1506;
	exit_thres = (cell->config->ocv->sleep_exit_current)*1000/1506;
	DBG("<%s>. sleep_enter_current = %d, sleep_exit_current = %d\n",
	    __func__, cell->config->ocv->sleep_enter_current,
	cell->config->ocv->sleep_exit_current);

	buf  = enter_thres&0xff;
	battery_write(di->rk818, RELAX_ENTRY_THRES_REGL, &buf, 1);
	buf = (enter_thres>>8)&0xff;
	battery_write(di->rk818, RELAX_ENTRY_THRES_REGH, &buf, 1);

	buf  = exit_thres&0xff;
	battery_write(di->rk818, RELAX_EXIT_THRES_REGL, &buf, 1);
	buf = (exit_thres>>8)&0xff;
	battery_write(di->rk818, RELAX_EXIT_THRES_REGH, &buf, 1);

	/* set sample time */
	battery_read(di->rk818, GGCON, &buf, 1);
	buf &= ~(3<<2);/*8min*/
	buf &= ~0x01; /* clear bat_res calc*/
	battery_write(di->rk818, GGCON, &buf, 1);
}

static void restart_relax(struct battery_info *di)
{
	u8 ggcon;/* chrg_ctrl_reg2;*/
	u8 ggsts;

	battery_read(di->rk818, GGCON, &ggcon, 1);
	ggcon &= ~0x0c;
	battery_write(di->rk818, GGCON, &ggcon, 1);

	battery_read(di->rk818, GGSTS, &ggsts, 1);
	ggsts &= ~0x0c;
	battery_write(di->rk818, GGSTS, &ggsts, 1);
}

static int  _get_average_current(struct battery_info *di)
{
	u8  buf;
	int ret;
	int current_now;
	int temp;
	int val[3];
	int i;

	for (i = 0; i < 3; i++) {
		ret = battery_read(di->rk818, BAT_CUR_AVG_REGL, &buf, 1);
		if (ret < 0) {
			dev_err(di->dev, "error read BAT_CUR_AVG_REGL");
			return ret;
		}
		val[i] = buf;

		ret = battery_read(di->rk818, BAT_CUR_AVG_REGH, &buf, 1);
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

	temp = current_now*1506/1000;/*1000*90/14/4096*500/521;*/

	return temp;
}

static int is_rk81x_bat_exist(struct  battery_info *di)
{
	u8 buf;

	battery_read(di->rk818, SUP_STS_REG, &buf, 1);
	return (buf & 0x80) ? 1 : 0;
}

static bool _is_first_poweron(struct  battery_info *di)
{
	u8 buf;
	u8 temp;

	battery_read(di->rk818, GGSTS, &buf, 1);
	DBG("%s GGSTS value is 0x%2x\n", __func__, buf);
	/*di->pwron_bat_con = buf;*/
	if (buf&BAT_CON) {
		buf &= ~(BAT_CON);
		do {
			battery_write(di->rk818, GGSTS, &buf, 1);
			battery_read(di->rk818, GGSTS, &temp, 1);
		} while (temp&BAT_CON);
		return true;
	}

	return false;
}
static void flatzone_voltage_init(struct battery_info *di)
{
	u32 *ocv_table;
	int ocv_size;
	int temp_table[21];
	int i, j;

	ocv_table = di->platform_data->battery_ocv;
	ocv_size = di->platform_data->ocv_size;

	for (j = 0; j < 21; j++)
		temp_table[j] = 0;

	j = 0;
	for (i = 1; i < ocv_size-1; i++) {
		if (ocv_table[i+1] < ocv_table[i] + 20)
			temp_table[j++] = i;
	}

	temp_table[j] = temp_table[j-1]+1;
	i = temp_table[0];
	di->enter_flatzone = ocv_table[i];
	j = 0;


	for (i = 0; i < 20; i++) {
		if (temp_table[i] < temp_table[i+1])
			j = i+1;
	}

	i = temp_table[j];
	di->exit_flatzone = ocv_table[i];

	DBG("enter_flatzone = %d exit_flatzone = %d\n",
	    di->enter_flatzone, di->exit_flatzone);
}

static void power_on_save(struct   battery_info *di, int ocv_voltage)
{
	u8 ocv_valid, first_pwron;
	u8 save_soc;
	u8 ocv_soc;

	/*buf==1: OCV_VOL is valid*/
	ocv_valid = rk81x_read_bit(di, MISC_MARK_REG, OCV_VALID_SHIFT);
	first_pwron = rk81x_read_bit(di, MISC_MARK_REG, FIRST_PWRON_SHIFT);
	DBG("readbit: ocv_valid=%d, first_pwron=%d\n", ocv_valid, first_pwron);

	if (first_pwron == 1 || ocv_valid == 1) {
		DBG("<%s> enter.\n", __func__);
		ocv_soc = _voltage_to_capacity(di, ocv_voltage);
		if (ocv_soc < 20) {
			di->dod0_voltage = ocv_voltage;
			di->dod0_capacity = di->nac;
			di->dod0_status = 1;
			di->dod0 = ocv_soc;
			di->dod0_level = 80;

			if (ocv_soc <= 0)
				di->dod0_level = 100;
			else if (ocv_soc < 5)
				di->dod0_level = 95;
			else if (ocv_soc < 10)
				di->dod0_level = 90;
			/* save_soc = di->dod0_level; */
			save_soc = get_level(di);
			if (save_soc <  di->dod0_level)
				save_soc = di->dod0_level;
			save_level(di, save_soc);
			DBG("<%s>: dod0_vol:%d, dod0_cap:%d, dod0:%d, level:%d",
			    __func__, di->dod0_voltage, di->dod0_capacity,
			    ocv_soc, save_soc);
		}
	}
}


static int _get_soc(struct   battery_info *di)
{
	return di->remain_capacity * 100 / div(di->fcc);
}

static enum power_supply_property rk_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CAPACITY,
};

#define to_device_info(x) container_of((x), \
				struct battery_info, bat)

static int rk81x_battery_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct battery_info *di = to_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = di->current_avg*1000;/*uA*/
		if (di->fg_drv_mode == TEST_POWER_MODE)
			val->intval = TEST_CURRENT*1000;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = di->voltage*1000;/*uV*/
		if (di->fg_drv_mode == TEST_POWER_MODE)
			val->intval = TEST_VOLTAGE*1000;

		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = is_rk81x_bat_exist(di);
		if (di->fg_drv_mode == TEST_POWER_MODE)
			val->intval = TEST_PRESET;

		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = di->real_soc;
		if (di->fg_drv_mode == TEST_POWER_MODE)
			val->intval = TEST_SOC;

		DBG("<%s>, report dsoc: %d\n", __func__, val->intval);
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = di->status;
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


#define to_ac_device_info(x) container_of((x), \
				struct battery_info, ac)

static int rk81x_battery_ac_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	int ret = 0;
	struct battery_info *di = to_ac_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
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

#define to_usb_device_info(x) container_of((x), \
				struct battery_info, usb)

static int rk81x_battery_usb_get_property(struct power_supply *psy,
					  enum power_supply_property psp,
					  union power_supply_propval *val)
{
	int ret = 0;
	struct battery_info *di = to_usb_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if ((strstr(saved_command_line, "charger") == NULL) &&
		    (di->real_soc == 0) && (di->work_on == 1))
			val->intval = 0;
		else
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


static void battery_power_supply_init(struct battery_info *di)
{
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
}

static int battery_power_supply_register(struct battery_info *di)
{
	int ret;
	struct device *dev = di->dev;

	ret = power_supply_register(dev, &di->bat);
	if (ret) {
		dev_err(dev, "failed to register main battery\n");
		goto batt_failed;
	}
	ret = power_supply_register(dev, &di->usb);
	if (ret) {
		dev_err(dev, "failed to register usb power supply\n");
		goto usb_failed;
	}
	ret = power_supply_register(dev, &di->ac);
	if (ret) {
		dev_err(dev, "failed to register ac power supply\n");
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

static void  _capacity_init(struct battery_info *di, u32 capacity)
{
	u8 buf;
	u32 capacity_ma;
	int delta_cap;

	delta_cap = capacity - di->remain_capacity;
	di->adjust_cap += delta_cap;

	reset_zero_var(di);

	capacity_ma = capacity*2390;/* 2134;//36*14/900*4096/521*500; */
	do {
		buf = (capacity_ma>>24)&0xff;
		battery_write(di->rk818, GASCNT_CAL_REG3, &buf, 1);
		buf = (capacity_ma>>16)&0xff;
		battery_write(di->rk818, GASCNT_CAL_REG2, &buf, 1);
		buf = (capacity_ma>>8)&0xff;
		battery_write(di->rk818, GASCNT_CAL_REG1, &buf, 1);
		buf = (capacity_ma&0xff) | 0x01;
		battery_write(di->rk818, GASCNT_CAL_REG0, &buf, 1);
		battery_read(di->rk818, GASCNT_CAL_REG0, &buf, 1);

	} while (buf == 0);
}


static void  _save_remain_capacity(struct battery_info *di, u32 capacity)
{
	u8 buf;
	u32 capacity_ma;

	if (capacity >= di->qmax)
		capacity = di->qmax;

	if (capacity <= 0)
		capacity = 0;

	capacity_ma = capacity;

	buf = (capacity_ma>>24)&0xff;
	battery_write(di->rk818, REMAIN_CAP_REG3, &buf, 1);
	buf = (capacity_ma>>16)&0xff;
	battery_write(di->rk818, REMAIN_CAP_REG2, &buf, 1);
	buf = (capacity_ma>>8)&0xff;
	battery_write(di->rk818, REMAIN_CAP_REG1, &buf, 1);
	buf = (capacity_ma&0xff) | 0x01;
	battery_write(di->rk818, REMAIN_CAP_REG0, &buf, 1);
}

static int _get_remain_capacity(struct battery_info *di)
{
	int ret;
	u8 buf;
	u32 capacity;
	int i;
	int val[3];

	for (i = 0; i < 3; i++) {
		ret = battery_read(di->rk818, REMAIN_CAP_REG3, &buf, 1);
		val[i] = buf << 24;
		ret = battery_read(di->rk818, REMAIN_CAP_REG2, &buf, 1);
		val[i] |= buf << 16;
		ret = battery_read(di->rk818, REMAIN_CAP_REG1, &buf, 1);
		val[i] |= buf << 8;
		ret = battery_read(di->rk818, REMAIN_CAP_REG0, &buf, 1);
		val[i] |= buf;
	}

	if (val[0] == val[1])
		capacity = val[0];
	else
		capacity = val[2];

	return capacity;
}


static void  _save_FCC_capacity(struct battery_info *di, u32 capacity)
{
	u8 buf;
	u32 capacity_ma;

	capacity_ma = capacity;
	buf = (capacity_ma>>24)&0xff;
	battery_write(di->rk818, NEW_FCC_REG3, &buf, 1);
	buf = (capacity_ma>>16)&0xff;
	battery_write(di->rk818, NEW_FCC_REG2, &buf, 1);
	buf = (capacity_ma>>8)&0xff;
	battery_write(di->rk818, NEW_FCC_REG1, &buf, 1);
	buf = (capacity_ma&0xff) | 0x01;
	battery_write(di->rk818, NEW_FCC_REG0, &buf, 1);
}

static int _get_FCC_capacity(struct battery_info *di)
{
	int ret;
	int temp = 0;
	u8 buf;
	u32 capacity;

	ret = battery_read(di->rk818, NEW_FCC_REG3, &buf, 1);
	temp = buf << 24;
	ret = battery_read(di->rk818, NEW_FCC_REG2, &buf, 1);
	temp |= buf << 16;
	ret = battery_read(di->rk818, NEW_FCC_REG1, &buf, 1);
	temp |= buf << 8;
	ret = battery_read(di->rk818, NEW_FCC_REG0, &buf, 1);
	temp |= buf;

	if (temp > 1)
		capacity = temp-1;/* 4096*900/14/36*500/521 */
	else
		capacity = temp;
	DBG("%s NEW_FCC_REG %d  capacity = %d\n", __func__, temp, capacity);

	return capacity;
}

static int _get_realtime_capacity(struct battery_info *di)
{
	int ret;
	int temp = 0;
	u8 buf;
	u32 capacity;
	int i;
	int val[3];

	for (i = 0; i < 3; i++) {
		ret = battery_read(di->rk818, GASCNT3, &buf, 1);
		val[i] = buf << 24;
		ret = battery_read(di->rk818, GASCNT2, &buf, 1);
		val[i] |= buf << 16;
		ret = battery_read(di->rk818, GASCNT1, &buf, 1);
		val[i] |= buf << 8;
		ret = battery_read(di->rk818, GASCNT0, &buf, 1);
		val[i] |= buf;
	}
	if (val[0] == val[1])
		temp = val[0];
	else
		temp = val[2];

	capacity = temp/2390;/* 4096*900/14/36*500/521; */

	return capacity;
}

static int _copy_soc(struct  battery_info *di, u8 save_soc)
{
	u8 soc;

	soc = save_soc;
	battery_write(di->rk818, SOC_REG, &soc, 1);
	return 0;
}

static int copy_reboot_cnt(struct  battery_info *di, u8 save_cnt)
{
	u8 cnt;

	cnt = save_cnt;
	battery_write(di->rk818, REBOOT_CNT_REG, &cnt, 1);
	return 0;
}

static bool support_uboot_charge(void)
{
	return support_uboot_chrg ? true : false;
}


/*
* There are three ways to detect dc_adp:
*	1. hardware only support dc_adp: by reg VB_MOD_REG of rk818,
*	   do not care about whether define dc_det_pin or not;
*	2. define de_det_pin: check gpio level;
*	3. support usb_adp and dc_adp: by VB_MOD_REG and usb interface.
*	   case that: gpio invalid or not define.
*/
static enum charger_type_t rk81x_get_dc_state(struct battery_info *di)
{
	enum charger_type_t charger_type;
	u8 buf;
	int ret;

	battery_read(di->rk818, VB_MOD_REG, &buf, 1);

	/*only HW_ADP_TYPE_DC: det by rk818 is easily and will be successful*/
	 if (!rk81x_support_adp_type(HW_ADP_TYPE_USB)) {
		if ((buf & PLUG_IN_STS) != 0)
			charger_type = DC_CHARGER;
		else
			charger_type = NO_CHARGER;

		return charger_type;
	 }

#if 1
	/*det by gpio level*/
	if (gpio_is_valid(di->dc_det_pin)) {
		ret = gpio_request(di->dc_det_pin, "rk818_dc_det");
		if (ret < 0) {
			pr_err("Failed to request gpio %d with ret:""%d\n",
			       di->dc_det_pin, ret);
			return NO_CHARGER;
		}

		gpio_direction_input(di->dc_det_pin);
		ret = gpio_get_value(di->dc_det_pin);
		if (ret == di->dc_det_level)
			charger_type = DC_CHARGER;
		else
			charger_type = NO_CHARGER;

		gpio_free(di->dc_det_pin);
		DBG("**********rk818 dc_det_pin=%d\n", ret);

		return charger_type;
	}
#endif
	/*HW_ADP_TYPE_DUAL: det by rk818 and usb*/
	else if (rk81x_support_adp_type(HW_ADP_TYPE_DUAL)) {
		if ((buf & PLUG_IN_STS) != 0) {
			charger_type = dwc_otg_check_dpdm();
			if (charger_type == 0)
				charger_type = DC_CHARGER;
			else
				charger_type = NO_CHARGER;
		}
	}

	return charger_type;
}

static enum charger_type_t rk81x_get_usbac_state(struct battery_info *di)
{
	enum charger_type_t charger_type;
	int usb_id, gadget_flag;

	usb_id = dwc_otg_check_dpdm();
	switch (usb_id) {
	case 0:
		charger_type = NO_CHARGER;
		break;
	case 1:
	case 3:
		charger_type = USB_CHARGER;
		break;
	case 2:
		charger_type = AC_CHARGER;
		break;
	default:
		charger_type = NO_CHARGER;
	}

	DBG("<%s>. DWC_OTG = %d\n", __func__, usb_id);
	if (charger_type == USB_CHARGER) {
		gadget_flag = get_gadget_connect_flag();
		DBG("<%s>. gadget_flag=%d, check_cnt=%d\n",
		    __func__, gadget_flag, di->check_count);

		if (0 == gadget_flag) {
			if (++di->check_count >= 5) {
				charger_type = AC_CHARGER;
				DBG("<%s>. turn to AC_CHARGER, check_cnt=%d\n",
				    __func__, di->check_count);
			} else {
				charger_type = USB_CHARGER;
			}
		} else {
			charger_type = USB_CHARGER;
		}
	} else {
		di->check_count = 0;
	}

	return charger_type;
}

/*
 * it is first time for battery to be weld, init by ocv table
 */
static void rsoc_first_poweron_init(struct battery_info *di)
{
	_save_FCC_capacity(di, di->design_capacity);
	di->fcc = _get_FCC_capacity(di);

	di->temp_soc = _voltage_to_capacity(di, di->voltage_ocv);
	di->real_soc = di->temp_soc;
	di->nac      = di->temp_nac;
	di->first_on_cap = di->nac;

	rk81x_set_bit(di, MISC_MARK_REG, OCV_VALID_SHIFT);
	rk81x_set_bit(di, MISC_MARK_REG, FIRST_PWRON_SHIFT);/*save*/
	DBG("<%s>.this is first poweron: OCV-SOC:%d, OCV-CAP:%d, FCC:%d\n",
	    __func__, di->real_soc, di->nac, di->fcc);
}

/*
 * it is not first time for battery to be weld, init by last record info
 */
static void rsoc_not_first_poweron_init(struct battery_info *di)
{
	u8 pwron_soc;
	u8 init_soc;
	u8 last_shtd_time;
	u8 curr_shtd_time;
	int remain_capacity;
	int ocv_soc;
	enum charger_type_t charger_type;

	rk81x_clr_bit(di, MISC_MARK_REG, FIRST_PWRON_SHIFT);
	battery_read(di->rk818, SOC_REG, &pwron_soc, 1);
	init_soc = pwron_soc;
	DBG("<%s> Not first pwron, SOC_REG = %d\n", __func__, pwron_soc);

	if (rk81x_support_adp_type(HW_ADP_TYPE_USB)) {
		charger_type = rk81x_get_usbac_state(di);
		if ((pwron_soc == 0) && (charger_type == USB_CHARGER)) {
			init_soc = 1;
			battery_write(di->rk818, SOC_REG, &init_soc, 1);
		}
	}

	remain_capacity = _get_remain_capacity(di);
	/* check if support uboot charge,
	 * if support, uboot charge driver should have done init work,
	 * so here we should skip init work
	 */
	if (support_uboot_charge())
		goto out;

	battery_read(di->rk818, NON_ACT_TIMER_CNT_REG,
		     &curr_shtd_time, 1);
	battery_read(di->rk818, NON_ACT_TIMER_CNT_REG_SAVE,
		     &last_shtd_time, 1);
	battery_write(di->rk818, NON_ACT_TIMER_CNT_REG_SAVE,
		      &curr_shtd_time, 1);
	DBG("<%s>, now_shtd_time = %d, last_shtd_time = %d, otg_status = %d\n",
	    __func__, curr_shtd_time, last_shtd_time, charger_type);

	ocv_soc = _voltage_to_capacity(di, di->voltage_ocv);
	DBG("<%s>, Not first pwron, real_remain_cap = %d, ocv-remain_cp=%d\n",
	    __func__, remain_capacity, di->temp_nac);

	/* if plugin, make sure current shtd_time diff from last_shtd_time.*/
	if (last_shtd_time != curr_shtd_time) {
		if (curr_shtd_time > 30) {
			rk81x_set_bit(di, MISC_MARK_REG, OCV_VALID_SHIFT);

			remain_capacity = di->temp_nac;
			di->first_on_cap = remain_capacity;
			DBG("<%s>pwroff > 30 minute, remain_cap = %d\n",
			    __func__, remain_capacity);

		} else if ((curr_shtd_time > 5) &&
				(abs32_int(ocv_soc - init_soc) >= 10)) {
			if (remain_capacity >= di->temp_nac*120/100)
				remain_capacity = di->temp_nac*110/100;
			else if (remain_capacity < di->temp_nac*8/10)
				remain_capacity = di->temp_nac*9/10;
			DBG("<%s> pwroff > 3 minute, remain_cap = %d\n",
			    __func__, remain_capacity);
		}
	} else {
		rk81x_clr_bit(di, MISC_MARK_REG, OCV_VALID_SHIFT);
	}
out:
	di->real_soc = init_soc;
	di->nac = remain_capacity;
	if (di->nac <= 0)
		di->nac = 0;
	DBG("<%s> init_soc = %d, init_capacity=%d\n",
	    __func__, di->real_soc, di->nac);
}

static u8 get_sys_pwroff_min(struct battery_info *di)
{
	u8 curr_shtd_time, last_shtd_time;

	battery_read(di->rk818, NON_ACT_TIMER_CNT_REG,
		     &curr_shtd_time, 1);
	battery_read(di->rk818, NON_ACT_TIMER_CNT_REG_SAVE,
		     &last_shtd_time, 1);

	return (curr_shtd_time != last_shtd_time) ? curr_shtd_time : 0;
}

static int _rsoc_init(struct battery_info *di)
{
	u8 pwroff_min;
	u8 calib_en;/*debug*/

	di->voltage  = _get_battery_voltage(di);
	di->voltage_ocv = _get_OCV_voltage(di);
	pwroff_min = get_sys_pwroff_min(di);

	DBG("OCV voltage=%d, voltage=%d, pwroff_min=%d\n",
	    di->voltage_ocv, di->voltage, pwroff_min);

	calib_en = rk81x_read_bit(di, MISC_MARK_REG, OCV_CALIB_SHIFT);
	DBG("readbit: calib_en=%d\n", calib_en);
	if (_is_first_poweron(di) ||
	    ((pwroff_min >= 30) && (calib_en == 1))) {
		rsoc_first_poweron_init(di);
		rk81x_clr_bit(di, MISC_MARK_REG, OCV_CALIB_SHIFT);

	} else {
		rsoc_not_first_poweron_init(di);
	}

	return 0;
}


static u8 rk81x_get_charge_status(struct battery_info *di)
{
	u8 status;
	u8 ret = 0;

	battery_read(di->rk818, SUP_STS_REG, &status, 1);
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

static void set_charge_current(struct battery_info *di, int charge_current)
{
	u8 usb_ctrl_reg;

	battery_read(di->rk818, USB_CTRL_REG, &usb_ctrl_reg, 1);
	usb_ctrl_reg &= (~0x0f);/* (VLIM_4400MV | ILIM_1200MA) |(0x01 << 7); */
	usb_ctrl_reg |= (charge_current | CHRG_CT_EN);
	battery_write(di->rk818, USB_CTRL_REG, &usb_ctrl_reg, 1);
}

static void rk81x_fg_match_param(struct battery_info *di, int chg_vol,
				 int chg_ilim, int chg_cur)
{
	int i;

	di->chg_v_lmt = DEF_CHRG_VOL;
	di->chg_i_lmt = DEF_CHRG_CURR_LMT;
	di->chg_i_cur = DEF_CHRG_CURR_SEL;

	for (i = 0; i < ARRAY_SIZE(CHG_V_LMT); i++) {
		if (chg_vol < CHG_V_LMT[i])
			break;
		else
			di->chg_v_lmt = (i << CHG_VOL_SHIFT);
	}

	for (i = 0; i < ARRAY_SIZE(CHG_I_LMT); i++) {
		if (chg_ilim < CHG_I_LMT[i])
			break;
		else
			di->chg_i_lmt = (i << CHG_ILIM_SHIFT);
	}

	for (i = 0; i < ARRAY_SIZE(CHG_I_CUR); i++) {
		if (chg_cur < CHG_I_CUR[i])
			break;
		else
			di->chg_i_cur = (i << CHG_ICUR_SHIFT);
	}
	DBG("<%s>. vol = 0x%x, i_lim = 0x%x, cur=0x%x\n",
	    __func__, di->chg_v_lmt, di->chg_i_lmt, di->chg_i_cur);
}

static u8 rk81x_chose_finish_ma(int fcc)
{
	u8 ma = FINISH_150MA;

	if (fcc < 3000)
		ma = FINISH_100MA;

	else if (fcc >= 3000 && fcc <= 4000)
		ma = FINISH_150MA;

	else if (fcc > 4000 && fcc <= 5000)
		ma = FINISH_200MA;

	else/*fcc > 5000*/
		ma = FINISH_250MA;

	return ma;
}

static void rk81x_battery_charger_init(struct  battery_info *di)
{
	u8 chrg_ctrl_reg1, usb_ctrl_reg, chrg_ctrl_reg2, chrg_ctrl_reg3;
	u8 sup_sts_reg, thremal_reg;
	int chg_vol, chg_cur, chg_ilim;
	u8 finish_ma;

	chg_vol = di->rk818->battery_data->max_charger_voltagemV;

	if (di->fg_drv_mode == TEST_POWER_MODE) {
		chg_cur = di->test_chrg_current;
		chg_ilim = di->test_chrg_ilmt;
	} else {
		chg_cur = di->rk818->battery_data->max_charger_currentmA;
		chg_ilim = di->rk818->battery_data->max_charger_ilimitmA;
	}

	rk81x_fg_match_param(di, chg_vol, chg_ilim, chg_cur);
	finish_ma = rk81x_chose_finish_ma(di->fcc);

	battery_read(di->rk818, THERMAL_REG, &thremal_reg, 1);
	battery_read(di->rk818, USB_CTRL_REG, &usb_ctrl_reg, 1);
	battery_read(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
	battery_read(di->rk818, CHRG_CTRL_REG2, &chrg_ctrl_reg2, 1);
	battery_read(di->rk818, SUP_STS_REG, &sup_sts_reg, 1);
	battery_read(di->rk818, CHRG_CTRL_REG3, &chrg_ctrl_reg3, 1);


	usb_ctrl_reg &= (~0x0f);

	if (rk81x_support_adp_type(HW_ADP_TYPE_USB))
		usb_ctrl_reg |= (CHRG_CT_EN | ILIM_450MA);/*en temp feed back*/
	else
		usb_ctrl_reg |= (CHRG_CT_EN | di->chg_i_lmt);

	thremal_reg &= (~0x0c);
	thremal_reg |= TEMP_105C;/*temp feed back: 105c*/

	chrg_ctrl_reg1 &= (0x00);
	chrg_ctrl_reg1 |= (CHRG_EN) | (di->chg_v_lmt | di->chg_i_cur);

	chrg_ctrl_reg3 |= CHRG_TERM_DIG_SIGNAL;/* digital finish mode*/
	chrg_ctrl_reg2 &= ~(0xc7);
	chrg_ctrl_reg2 |= finish_ma | CHG_CCCV_6HOUR;

	sup_sts_reg &= ~(0x01 << 3);
	sup_sts_reg |= (0x01 << 2);

	thremal_reg &= (~0x0c);
	thremal_reg |= TEMP_105C;/*temp feed back: 105c*/

	battery_write(di->rk818, THERMAL_REG, &thremal_reg, 1);
	battery_write(di->rk818, CHRG_CTRL_REG3, &chrg_ctrl_reg3, 1);
	battery_write(di->rk818, USB_CTRL_REG, &usb_ctrl_reg, 1);
	battery_write(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
	battery_write(di->rk818, CHRG_CTRL_REG2, &chrg_ctrl_reg2, 1);
	battery_write(di->rk818, SUP_STS_REG, &sup_sts_reg, 1);
}

void charge_disable_open_otg(int value)
{
	struct  battery_info *di = g_battery;

	if (value == 1) {
		DBG("charge disable, enable OTG.\n");
		rk818_set_bits(di->rk818, CHRG_CTRL_REG1, 1 << 7, 0 << 7);
		rk818_set_bits(di->rk818, 0x23, 1 << 7, 1 << 7);
	}
	if (value == 0) {
		DBG("charge enable, disable OTG.\n");
		rk818_set_bits(di->rk818, 0x23, 1 << 7, 0 << 7);
		rk818_set_bits(di->rk818, CHRG_CTRL_REG1, 1 << 7, 1 << 7);
	}
}

static void rk81x_low_waring_init(struct battery_info *di)
{
	u8 vb_mon_reg;
	u8 vb_mon_reg_init;

	battery_read(di->rk818, VB_MOD_REG, &vb_mon_reg, 1);

	/* 3.4v: interrupt*/
	vb_mon_reg_init = (((vb_mon_reg | (1 << 4)) & (~0x07)) | 0x06);
	battery_write(di->rk818, VB_MOD_REG, &vb_mon_reg_init, 1);
}

static void rk81x_fg_init(struct battery_info *di)
{
	u8 adc_ctrl_val;
	u8 buf = 0;
	u8 pcb_offset;
	int cal_offset;

	adc_ctrl_val = 0x30;
	battery_write(di->rk818, ADC_CTRL_REG, &adc_ctrl_val, 1);

	_gauge_enable(di);
	/* get the volatege offset */
	_get_voltage_offset_value(di);
	rk81x_battery_charger_init(di);
	_set_relax_thres(di);

	/* get the current offset , the value write to the CAL_OFFSET */
	di->current_offset = _get_ioffset(di);
	battery_read(di->rk818, PCB_IOFFSET_REG, &pcb_offset, 1);
	DBG("<%s>. pcb_offset = 0x%x\n", __func__, pcb_offset);
	DBG("<%s>. io_offset = 0x%x\n", __func__, di->current_offset);

	_set_cal_offset(di, di->current_offset+pcb_offset);
	cal_offset = _get_cal_offset(di);
	if ((cal_offset < 0x7ff) || (pcb_offset == 0))
		_set_cal_offset(di, di->current_offset+42);

	_rsoc_init(di);
	_capacity_init(di, di->nac);

	di->remain_capacity = _get_realtime_capacity(di);
	di->current_avg = _get_average_current(di);

	rk81x_low_waring_init(di);
	restart_relax(di);
	power_on_save(di, di->voltage_ocv);
	battery_write(di->rk818, OCV_VOL_VALID_REG, &buf, 1);

	/* set sample time for cal_offset interval*/
	ioffset_sample_time(di, SAMP_TIME_8MIN);
	dump_gauge_register(di);
	dump_charger_register(di);

	DBG("<%s> :\n"
	    "nac = %d , remain_capacity = %d\n"
	    "OCV_voltage = %d, voltage = %d\n"
	    "SOC = %d, fcc = %d\n, current=%d\n"
	    "cal_offset = 0x%x\n",
	    __func__,
	    di->nac, di->remain_capacity,
	    di->voltage_ocv, di->voltage,
	    di->real_soc, di->fcc, di->current_avg,
	    cal_offset);
}
/*
 * this is a very important algorithm to avoid over discharge.
 */
/* int R_soc, D_soc, r_soc, zq, k, Q_err, Q_ocv; */
static void zero_get_soc(struct battery_info *di)
{
	int dead_voltage, ocv_voltage;
	int temp_soc = -1, real_soc;
	int currentold, currentnow, voltage;
	int i;
	int voltage_k;
	int count_num = 0;
	int q_ocv;
	int ocv_soc;

	DBG("\n\n+++++++zero mode++++++display soc+++++++++++\n");
	do {
		currentold = _get_average_current(di);
		_get_cal_offset(di);
		_get_ioffset(di);
		msleep(100);
		currentnow = _get_average_current(di);
		count_num++;
	} while ((currentold == currentnow) && (count_num < 11));

	voltage  = 0;
	for (i = 0; i < 10 ; i++)
		voltage += _get_battery_voltage(di);
	voltage /= 10;

	if (di->voltage_old == 0)
		di->voltage_old = voltage;
	voltage_k = voltage;
	voltage = (di->voltage_old*2 + 8*voltage)/10;
	di->voltage_old = voltage;
	currentnow = _get_average_current(di);

	dead_voltage = 3400 + abs32_int(currentnow)*(di->bat_res+65)/1000;
	/* 65 mo power-path mos */
	ocv_voltage = voltage + abs32_int(currentnow)*di->bat_res/1000;
	DBG("ZERO: dead_voltage(shtd) = %d, ocv_voltage(now) = %d\n",
	    dead_voltage, ocv_voltage);

	ocv_soc = _voltage_to_capacity(di, dead_voltage);
	di->q_dead = di->temp_nac;
	DBG("ZERO: dead_voltage_soc = %d, q_dead = %d\n",
	    ocv_soc, di->q_dead);

	ocv_soc = _voltage_to_capacity(di, ocv_voltage);
	q_ocv = di->temp_nac;
	DBG("ZERO: ocv_voltage_soc = %d, q_ocv = %d\n",
	    ocv_soc, q_ocv);

	/*[Q_err]: Qerr, [temp_nac]:check_voltage_nac*/
	di->q_err = di->remain_capacity - q_ocv;
	DBG("q_err=%d, [remain_capacity]%d - [q_ocv]%d",
	    di->q_err, di->remain_capacity, q_ocv);

	if (di->display_soc == 0)
		di->display_soc = di->real_soc*1000;
	real_soc = di->display_soc;

	DBG("remain_capacity = %d, q_dead = %d, q_err = %d\n",
	    di->remain_capacity, di->q_dead, di->q_err);
	/*[temp_nac]:dead_voltage*/
	if (q_ocv > di->q_dead) {
		DBG("first: q_ocv > di->q_dead\n");

		/*initical K0*/
		if ((di->update_k == 0) || (di->zero_cycle >= 500)) {
			DBG("[K == 0]\n");
			di->zero_cycle = 0;
			di->update_k = 1;
			/* ZQ = Q_ded +  Qerr */
			/*[temp_nac]:dead_voltage*/
			di->q_shtd = di->q_dead + di->q_err;
			temp_soc = (di->remain_capacity - di->q_shtd)*
					1000/div(di->fcc);
			if (temp_soc == 0)
				di->update_k = 0;
			else
				di->line_k = (real_soc + temp_soc/2)
						/div(temp_soc);
		/* recalc K0*/
		} else if (di->zero_updated && di->update_k >= 10) {
			DBG("[K >= 10].\n");
			di->update_k = 1;
			_voltage_to_capacity(di, dead_voltage);
			di->q_dead = di->temp_nac;
			di->q_shtd = di->q_dead + di->q_err;
			temp_soc = ((di->remain_capacity - di->q_shtd)*
				1000 + di->fcc/2)/div(di->fcc); /* z1 */
			if (temp_soc == 0)
				di->update_k = 0;
			else
				di->line_k = (real_soc + temp_soc/2)
						/div(temp_soc);

			DBG("[K >= 10]. new:line_k = %d\n", di->line_k);
			DBG("[K >= 10]. new:Y0(dis_soc)=%d\n", di->display_soc);
			DBG("[K >= 10]. new:X0(temp) = %d\n", temp_soc);

		} else { /*update_k[1~9]*/
			DBG("[K1~9]\n");
			di->zero_cycle++;
			di->update_k++;
			DBG("[K1~9]. (old)Y0=%d, Y0=%d\n",
			    di->old_display_soc, di->display_soc);
			if (di->update_k == 2)
				di->old_display_soc = di->display_soc;

			temp_soc = ((di->remain_capacity - di->q_shtd)*
				1000 + di->fcc/2)/div(di->fcc);
			real_soc = di->line_k*temp_soc;
			di->display_soc = real_soc;

			/* make sure display_soc change at least once*/
			if (di->display_soc >= di->old_display_soc)
				di->zero_updated = false;
			else
				di->zero_updated = true;

			DBG("[K1~9]. (temp_soc)X0 = %d\n", temp_soc);
			DBG("[K1~9]. line_k = %d\n", di->line_k);
			DBG("[K1~9]. (dis-soc)Y0=%d,real-soc=%d\n",
			    di->display_soc, di->real_soc);

			if ((di->display_soc+500)/1000 < di->real_soc) {
				/*special for 0%*/
				if ((di->real_soc == 1) &&
				    (di->display_soc < 100))
					di->real_soc--;
				else
					di->real_soc--;
				/*di->odd_capacity = 0;*/
			}
		}
	} else {
		DBG("second: q_ocv < di->q_dead\n");
		di->update_k++;

		if (di->voltage < 3400) {
			DBG("second: voltage < 3400\n");
			di->real_soc--;
		} else {
			if (di->update_k > 10) {
				di->update_k = 0;
				di->real_soc--;
				di->odd_capacity = 0;
			}
		}
	}

	if (di->line_k <= 0) {
		reset_zero_var(di);
		DBG("ZERO: line_k <= 0, Update line_k!\n");
	}

	DBG("ZERO: update_k=%d, odd_cap=%d\n", di->update_k, di->odd_capacity);
	DBG("ZERO: q_ocv - q_dead=%d\n", (q_ocv-di->q_dead));
	DBG("ZERO: remain_cap - q_shtd=%d\n",
	    (di->remain_capacity - di->q_shtd));
	DBG("ZERO: (line_k)K0 = %d,(disp-soc)Y0 = %d, (temp_soc)X0 = %d\n",
	    di->line_k, di->display_soc, temp_soc);
	DBG("ZERO: zero_cycle=%d,(old)Y0=%d, zero_updated=%d, update_k=%d\n",
	    di->zero_cycle, di->old_display_soc,
	    di->zero_updated, di->update_k);

	DBG("ZERO: remain_capacity=%d, q_shtd(nac)=%d, q_err(Q_rm-q_ocv)=%d\n",
	    di->remain_capacity, di->q_shtd, di->q_err);
	DBG("ZERO: Warn_voltage=%d,temp_soc=%d,real_soc=%d\n\n",
	    di->warnning_voltage, _get_soc(di), di->real_soc);
}


static int estimate_bat_ocv_vol(struct battery_info *di)
{
	return (di->voltage -
				(di->bat_res * di->current_avg) / 1000);
}

static int estimate_bat_ocv_soc(struct battery_info *di)
{
	int ocv_soc, ocv_voltage;

	ocv_voltage = estimate_bat_ocv_vol(di);
	ocv_soc = _voltage_to_capacity(di, ocv_voltage);

	return ocv_soc;
}

/* we will estimate a ocv voltage to get a ocv soc.
 * if there is a big offset between ocv_soc and rsoc,
 * we will decide whether we should reinit capacity or not
 */
static void rsoc_dischrg_calib(struct battery_info *di)
{
	int ocv_soc = di->est_ocv_soc;
	int ocv_volt = di->est_ocv_vol;
	int temp_soc = _get_soc(di);
	int max_volt = di->rk818->battery_data->max_charger_voltagemV;

	if (ocv_volt > max_volt)
		goto out;

	if (di->discharge_min >= RSOC_CALIB_DISCHGR_TIME) {
		if ((ocv_soc-temp_soc >= RSOC_DISCHG_ERR_LOWER) ||
		    (di->temp_soc == 0) ||
		    (temp_soc-ocv_soc >= RSOC_DISCHG_ERR_UPPER)) {
			di->err_chck_cnt++;
			di->err_soc_sum += ocv_soc;
		} else {
			goto out;
		}
		DBG("<%s>. rsoc err_chck_cnt = %d\n",
		    __func__, di->err_chck_cnt);
		DBG("<%s>. rsoc err_soc_sum = %d\n",
		    __func__, di->err_soc_sum);

		if (di->err_chck_cnt >= RSOC_ERR_CHCK_CNT) {
			ocv_soc = di->err_soc_sum / RSOC_ERR_CHCK_CNT;
			if (temp_soc-ocv_soc >= RSOC_DISCHG_ERR_UPPER)
				ocv_soc += RSOC_COMPS;

			di->temp_nac = ocv_soc * di->fcc / 100;
			_capacity_init(di, di->temp_nac);
			di->temp_soc = _get_soc(di);
			di->remain_capacity = _get_realtime_capacity(di);
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

static void rsoc_realtime_calib(struct battery_info *di)
{
	u8 status = di->status;

	if ((status == POWER_SUPPLY_STATUS_CHARGING) ||
	    (status == POWER_SUPPLY_STATUS_FULL)) {
		if ((di->current_avg < -10) &&
		    (di->charge_status != CHARGE_FINISH))
			rsoc_dischrg_calib(di);
		/*
		else
			rsoc_chrg_calib(di);
		*/

	} else if (status == POWER_SUPPLY_STATUS_DISCHARGING) {
		rsoc_dischrg_calib(di);
	}
}

/*
 * when there is a big offset between dsoc and rsoc, dsoc needs to
 * speed up to keep pace witch rsoc.
 */
static bool do_ac_charger_emulator(struct battery_info *di)
{
	int delta_soc = di->temp_soc - di->real_soc;
	u32 soc_time;

	if ((di->charge_status != CHARGE_FINISH) &&
	    (di->ac_online == ONLINE) &&
	    (delta_soc >= DSOC_CHRG_FAST_EER_RANGE)) {
		if (di->current_avg < DSOC_CHRG_EMU_CURR)
			soc_time = di->fcc*3600/100/
					(abs_int(DSOC_CHRG_EMU_CURR));
		else
			soc_time = di->fcc*3600/100/
					div(abs_int(di->current_avg));
		di->emu_chg_cnt++;
		if  (di->emu_chg_cnt > soc_time) {
			di->real_soc++;
			di->emu_chg_cnt = 0;
		}
		DBG("<%s>. soc_time=%d, emu_cnt=%d\n",
		    __func__, soc_time, di->emu_chg_cnt);

		return true;
	}

	return false;
}

/* check voltage and current when dsoc is close to full.
 * we will do a fake charge to adjust charing speed which
 * aims to make battery full charged and match finish signal.
 */
static bool do_term_chrg_calib(struct battery_info *di)
{
	u32 soc_time;
	u32 *ocv_table = di->platform_data->battery_ocv;

	/*check current and voltage*/
	if ((di->ac_online == ONLINE && di->real_soc >= 90) &&
	    ((di->current_avg > DSOC_CHG_TERM_CURR) ||
	     (di->voltage < ocv_table[18]+20))) {
		soc_time = di->fcc*3600/100/(abs32_int(DSOC_CHG_TERM_CURR));
		di->term_chg_cnt++;
		if  (di->term_chg_cnt > soc_time) {
			di->real_soc++;
			di->term_chg_cnt = 0;
		}
		DBG("<%s>. soc_time=%d, term_cnt=%d\n",
		    __func__, soc_time, di->term_chg_cnt);

		return true;
	}

	return false;
}

static void normal_discharge(struct battery_info *di)
{
	int soc_time = 0;
	int now_current = di->current_avg;
	int delta_soc = di->real_soc - di->temp_soc;

	if (delta_soc > DSOC_DISCHRG_FAST_EER_RANGE) {
		soc_time = DSOC_DISCHRG_FAST_DEC_SEC;
		DBG("<%s>. dsoc decrease fast! delta_soc = %d\n",
		    __func__, delta_soc);
	} else {
		soc_time = di->fcc*3600/100/div(abs_int(now_current));
	}

	if (di->temp_soc == di->real_soc) {
		DBG("<%s>. temp_soc == real_soc\n", __func__);

	} else if (di->temp_soc > di->real_soc) {
		DBG("<%s>. temp_soc > real_soc\n", __func__);
		di->vol_smooth_time++;
		if (di->vol_smooth_time > soc_time*3/2) {
			di->real_soc--;
			di->vol_smooth_time = 0;
		}

	} else {
		DBG("<%s>. temp_soc < real_soc\n", __func__);
		if (di->real_soc == (di->temp_soc + 1)) {
			di->change_timer = di->soc_timer;
			di->real_soc = di->temp_soc;
		} else {
			di->vol_smooth_time++;
			if (di->vol_smooth_time > soc_time*3/4) {
				di->real_soc--;
				di->vol_smooth_time  = 0;
			}
		}
	}
	reset_zero_var(di);
	DBG("<%s>, temp_soc = %d, real_soc = %d\n",
	    __func__, di->temp_soc, di->real_soc);
	DBG("<%s>, vol_smooth_time = %d, soc_time = %d\n",
	    __func__, di->vol_smooth_time, soc_time);
}

static void rk81x_battery_discharge_smooth(struct battery_info *di)
{
	int ocv_soc;

	ocv_soc = _voltage_to_capacity(di, 3800);
	di->temp_soc = _get_soc(di);

	DBG("<%s>. temp_soc = %d, real_soc = %d\n",
	    __func__, di->temp_soc, di->real_soc);

	if (di->voltage < 3800)

		zero_get_soc(di);
	else
		normal_discharge(di);
}

static int get_charging_time(struct battery_info *di)
{
	return (di->charging_time/60);
}

static int get_discharging_time(struct battery_info *di)
{
	return (di->discharging_time/60);
}

static int get_finish_time(struct battery_info *di)
{
	return (di->finish_time/60);
}

static void upd_time_table(struct battery_info *di);
static void collect_debug_info(struct battery_info *di)
{
	if ((di->ac_online == ONLINE) || (di->usb_online == ONLINE)) {
		di->charging_time++;
		di->discharging_time = 0;
	} else {
		di->charging_time = 0;
		if (di->voltage < 3800)
			di->discharging_time += 2;
		else
			di->discharging_time++;
	}
	if (di->charge_status == CHARGE_FINISH)
		di->finish_time++;
	else
		di->finish_time = 0;

	di->charge_min = get_charging_time(di);
	di->discharge_min = get_discharging_time(di);
	di->finish_min = get_finish_time(di);

	upd_time_table(di);
}

static void dump_debug_info(struct battery_info *di)
{
	u8 sup_tst_reg, ggcon_reg, ggsts_reg, vb_mod_reg;
	u8 usb_ctrl_reg, chrg_ctrl_reg1, thremal_reg;
	u8 chrg_ctrl_reg2, chrg_ctrl_reg3, rtc_val, misc_reg;

	collect_debug_info(di);

	battery_read(di->rk818, MISC_MARK_REG, &misc_reg, 1);
	battery_read(di->rk818, GGCON, &ggcon_reg, 1);
	battery_read(di->rk818, GGSTS, &ggsts_reg, 1);
	battery_read(di->rk818, SUP_STS_REG, &sup_tst_reg, 1);
	battery_read(di->rk818, VB_MOD_REG, &vb_mod_reg, 1);
	battery_read(di->rk818, USB_CTRL_REG, &usb_ctrl_reg, 1);
	battery_read(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
	battery_read(di->rk818, CHRG_CTRL_REG2, &chrg_ctrl_reg2, 1);
	battery_read(di->rk818, CHRG_CTRL_REG3, &chrg_ctrl_reg3, 1);
	battery_read(di->rk818, 0x00, &rtc_val, 1);
	battery_read(di->rk818, THERMAL_REG, &thremal_reg, 1);

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

	DBG(
	    "########################## [read] 3.0############################\n"
	    "--------------------------------------------------------------\n"
	    "realx-voltage = %d, voltage = %d, current-avg = %d\n"
	    "fcc = %d, remain_capacity = %d, ocv_volt = %d\n"
	    "check_ocv = %d, check_soc = %d, bat_res = %d\n"
	    "diplay_soc = %d, cpapacity_soc = %d, test_mode = %d\n"
	    "AC-ONLINE = %d, USB-ONLINE = %d, charging_status = %d\n"
	    "finish_real_soc = %d, finish_temp_soc = %d\n"
	    "i_offset=0x%x, cal_offset=0x%x, adjust_cap=%d\n"
	    "chrg_time = %d, dischrg_time = %d, finish_time = %d\n",
	    get_relax_voltage(di),
	    di->voltage, di->current_avg,
	    di->fcc, di->remain_capacity, _get_OCV_voltage(di),
	    di->est_ocv_vol, di->est_ocv_soc, di->bat_res,
	    di->real_soc, _get_soc(di), di->fg_drv_mode,
	    di->ac_online, di->usb_online, di->status,
	    di->debug_finish_real_soc, di->debug_finish_temp_soc,
	    _get_ioffset(di), _get_cal_offset(di), di->adjust_cap,
	    get_charging_time(di), get_discharging_time(di), get_finish_time(di)
	   );
	rk81x_get_charge_status(di);
	DBG("###########################################################\n");
}

static void update_fcc_capacity(struct battery_info *di)
{
	int fcc0;
	int remain_cap;

	remain_cap = di->remain_capacity + di->adjust_cap - di->first_on_cap;
	DBG("%s: remain_cap:%d, ajust_cap:%d, first_on_cap=%d\n",
	    __func__, remain_cap, di->adjust_cap, di->first_on_cap);

	if ((di->charge_status == CHARGE_FINISH) && (di->dod0_status == 1)) {
		DBG("%s: dod0:%d, dod0_cap:%d, dod0_level:%d\n",
		    __func__, di->dod0, di->dod0_capacity, di->dod0_level);

		if (get_level(di) >= di->dod0_level) {
			fcc0 = (remain_cap - di->dod0_capacity)*100
					/(100-di->dod0);
			if (fcc0 > di->qmax)
				fcc0 = di->qmax;

			DBG("%s: fcc0:%d, fcc:%d\n", __func__, fcc0, di->fcc);
			if ((fcc0 < di->fcc) && (fcc0 > 1000)) {
				di->fcc = fcc0;
				_capacity_init(di, di->fcc);
				_save_FCC_capacity(di, di->fcc);
				DBG("%s: new fcc0:%d\n", __func__, di->fcc);
			}
		}
		di->dod0_status = 0;
	}
}

static void debug_get_finish_soc(struct battery_info *di)
{
	if (di->charge_status == CHARGE_FINISH) {
		di->debug_finish_real_soc = di->real_soc;
		di->debug_finish_temp_soc = di->temp_soc;
	}
}

static void wait_charge_finish_signal(struct battery_info *di)
{
	if ((di->charge_status == CHARGE_FINISH) &&
	    (di->voltage > CHG_FINISH_VOL))
		update_fcc_capacity(di);/* save new fcc*/

	/* debug msg*/
	debug_get_finish_soc(di);
}

static void charge_finish_routine(struct battery_info *di)
{
	if ((di->charge_status == CHARGE_FINISH) &&
	    (di->voltage > CHG_FINISH_VOL)) {
		_capacity_init(di, di->fcc);
		zero_current_calib(di);

		if (di->real_soc < 100) {
			DBG("<%s>,CHARGE_FINISH:real_soc<100,real_soc=%d\n",
			    __func__, di->real_soc);

			if ((di->soc_counter < 80)) {
				di->soc_counter++;
			} else {
				di->soc_counter = 0;
				di->real_soc++;
			}
		}
	}
}

static void normal_charge(struct battery_info *di)
{
	int now_current, soc_time;

	now_current = _get_average_current(di);
	soc_time = di->fcc*3600/100/div(abs_int(now_current));   /* 1%  time; */
	di->temp_soc = _get_soc(di);

	DBG("<%s>. temp_soc = %d, real_soc = %d\n",
	    __func__, di->temp_soc, di->real_soc);

	if (di->real_soc == di->temp_soc) {
		DBG("<%s>. temp_soc == real_soc\n", __func__);
		    di->temp_soc = _get_soc(di);
	}
	if ((di->temp_soc != di->real_soc) && (now_current != 0)) {
		if (di->temp_soc < di->real_soc + 1) {
			DBG("<%s>. temp_soc < real_soc\n", __func__);
			di->charge_smooth_time++;
			if  (di->charge_smooth_time > soc_time*3/2) {
				di->real_soc++;
				di->charge_smooth_time  = 0;
			}
			di->charge_smooth_status = true;
		}

		else if (di->temp_soc > di->real_soc + 1) {
			DBG("<%s>. temp_soc > real_soc\n", __func__);
			di->charge_smooth_time++;
			if  (di->charge_smooth_time > soc_time*3/4) {
				di->real_soc++;
				di->charge_smooth_time  = 0;
			}
			di->charge_smooth_status = true;

		} else if (di->temp_soc == di->real_soc + 1) {
			DBG("<%s>. temp_soc == real_soc + 1\n", __func__);
			if (di->charge_smooth_status) {
				di->charge_smooth_time++;
				if (di->charge_smooth_time > soc_time*3/4) {
					di->real_soc = di->temp_soc;
					di->charge_smooth_time  = 0;
					di->charge_smooth_status = false;
				}

			} else {
				di->real_soc = di->temp_soc;
				di->charge_smooth_status = false;
			}
		}
	}

	DBG("<%s>, temp_soc = %d, real_soc = %d\n",
	    __func__, di->temp_soc, di->real_soc);
	DBG("<%s>, vol_smooth_time = %d, soc_time = %d\n",
	    __func__, di->charge_smooth_time, soc_time);
}



static void rk81x_battery_charge_smooth(struct battery_info *di)
{
	reset_zero_var(di);
	/*calibrate: aim to match finish signal*/
	if (do_term_chrg_calib(di))
		return;

	/*calibrate: aim to calib error*/
	di->term_chg_cnt = 0;
	if (do_ac_charger_emulator(di))
		return;

	normal_charge(di);
}

static void rk81x_battery_display_smooth(struct battery_info *di)
{
	int status;
	u8  charge_status;

	status = di->status;
	charge_status = di->charge_status;
	if ((status == POWER_SUPPLY_STATUS_CHARGING) ||
	    (status == POWER_SUPPLY_STATUS_FULL)) {
		if ((di->current_avg < -10) &&
		    (charge_status != CHARGE_FINISH))
			rk81x_battery_discharge_smooth(di);
		else
			rk81x_battery_charge_smooth(di);

	} else if (status == POWER_SUPPLY_STATUS_DISCHARGING) {
		rk81x_battery_discharge_smooth(di);
		if (di->real_soc == 1) {
			di->time2empty++;
			if (di->time2empty >= 300)
				di->real_soc = 0;
		} else {
			di->time2empty = 0;
		}
	}
}

/*
 * update rsoc by relax voltage
 */
static void resume_relax_calib(struct battery_info *di)
{
	int relax_vol = di->relax_voltage;
	int ocv_soc, capacity;

	ocv_soc = _voltage_to_capacity(di, relax_vol);
	capacity = (ocv_soc * di->fcc / 100);
	_capacity_init(di, capacity);
	di->remain_capacity = _get_realtime_capacity(di);
	di->temp_soc = _get_soc(di);
	DBG("%s, RSOC=%d, CAP=%d\n", __func__, ocv_soc, capacity);
}

/* condition:
 * 1: must do it
 * 0: when neccessary
 */
static void resume_vol_calib(struct battery_info *di, int condition)
{
	int ocv_vol = di->est_ocv_vol;
	int ocv_soc = 0, capacity = 0;

	ocv_soc = _voltage_to_capacity(di, ocv_vol);
	capacity = (ocv_soc * di->fcc / 100);
	if (condition || (abs(ocv_soc-di->temp_soc) >= RSOC_RESUME_ERR)) {
		_capacity_init(di, capacity);
		di->remain_capacity = _get_realtime_capacity(di);
		di->temp_soc = _get_soc(di);
		DBG("<%s>, rsoc updated!\n", __func__);
	}
	DBG("<%s>, OCV_VOL=%d,OCV_SOC=%d, CAP=%d\n",
	    __func__, ocv_vol, ocv_soc, capacity);
}

/*
 * when support HW_ADP_TYPE_DUAL, and at the moment that usb_adp
 * and dc_adp are plugined in together, the dc_apt has high priority.
 * so we check dc_apt first and return rigth away if it's found.
 */
static enum charger_type_t rk81x_get_adp_type(struct battery_info *di)
{
	u8 buf;
	enum charger_type_t charger_type = NO_CHARGER;

	/*check by ic hardware: this check make check work safer*/
	battery_read(di->rk818, VB_MOD_REG, &buf, 1);
	if ((buf & PLUG_IN_STS) == 0)
		return NO_CHARGER;

	/*check DC first*/
	if (rk81x_support_adp_type(HW_ADP_TYPE_DC)) {
		charger_type = rk81x_get_dc_state(di);
		if (charger_type == DC_CHARGER)
			return charger_type;
	}

	/*HW_ADP_TYPE_USB*/
	charger_type = rk81x_get_usbac_state(di);

	return charger_type;
}

static void rk81x_sleep_discharge(struct battery_info *di)
{
	int delta_cap;
	int delta_soc;
	int sleep_min;
	unsigned long sleep_sec;
	int enter_rsoc;

	enter_rsoc = di->real_soc;
	sleep_sec = BASE_TO_SEC(di->suspend_time_start);
	sleep_min = BASE_TO_MIN(di->suspend_time_start);
	delta_cap = di->suspend_cap - di->remain_capacity;
	delta_soc = di->suspend_rsoc - _get_soc(di);
	di->sum_suspend_cap += delta_cap;

	DBG("<%s>, slp_sec(s)=%lu, slp_min=%d\n"
	    "delta_cap(s)=%d, delta_soc=%d, sum_cap=%d\n"
	    "remain_cap=%d, rsoc=%d, dsoc=%d\n"
	    "relax_vol=%d, vol=%d, curr=%d\n",
	    __func__, sleep_sec, sleep_min,
	    delta_cap, delta_soc, di->sum_suspend_cap,
	    di->remain_capacity, _get_soc(di), di->real_soc,
	    di->relax_voltage, di->voltage, _get_average_current(di));

	/*handle rsoc*/
	if ((sleep_min >= 30) &&
	    (di->relax_voltage >= di->voltage)) {
		resume_relax_calib(di);
		restart_relax(di);

	/* current_avg < 0: make sure the system is not
	 * wakeup by charger plugin.
	 */

	/* even if relax voltage is not caught rightly, realtime voltage
	 * is quite close to relax voltage, we should not do nothing after
	 * sleep 30min
	 */
	} else if ((sleep_min >= 30) && (di->current_avg < 0)) {
		resume_vol_calib(di, 1);
	} else if ((sleep_min >= 3) && (di->current_avg < 0)) {
		resume_vol_calib(di, 0);
	}

	/*handle dsoc*/
	delta_soc = di->sum_suspend_cap/(di->fcc/100);

	DBG("<%s>. sum_cap ==> delta_soc = %d\n", __func__, delta_soc);
	if (delta_soc > 0) {
		if (di->real_soc-(delta_soc*1/3) <= di->temp_soc)
			di->real_soc -= (delta_soc*1/3);
		else if (di->real_soc-(delta_soc*1/2) < di->temp_soc)
			di->real_soc -= (delta_soc*1/2);
		else
			di->real_soc -= delta_soc;

		/*di->sum_suspend_cap %= (di->fcc/100);*/
		if (di->real_soc != enter_rsoc)
			di->sum_suspend_cap = 0;

	} else if (delta_soc < 0) {
		di->real_soc--;
	}
	DBG("<%s>, out: dsoc=%d, rsoc=%d, sum_cap=%d\n",
	    __func__, di->real_soc, di->temp_soc, di->sum_suspend_cap);
}

static void rk81x_sleep_charge(struct battery_info *di)
{
	unsigned long sleep_soc;
	unsigned long sleep_sec;
	int delta_cap;
	int delta_soc;
	int sleep_min;
	u8 charge_status = di->charge_status;

	if ((di->suspend_charge_current >= 0) ||
	    (rk81x_get_charge_status(di) == CHARGE_FINISH)) {
		sleep_sec = BASE_TO_SEC(di->suspend_time_start);
		sleep_min = BASE_TO_MIN(di->suspend_time_start);
		delta_cap = di->suspend_cap - di->remain_capacity;
		delta_soc = di->suspend_rsoc - _get_soc(di);

		DBG("<%s>, ac=%d, usb=%d, slp_curr=%d\n",
		    __func__, di->ac_online, di->usb_online,
		    di->suspend_charge_current);
		if (((di->suspend_charge_current < 800) &&
		     (di->ac_online == ONLINE)) ||
		     (charge_status == CHARGE_FINISH)) {
			DBG("<%s>,sleep: ac online current < 800\n", __func__);
			if (sleep_sec > 0) {
				/*default charge current: 1000mA*/
				di->count_sleep_time += sleep_sec;
				sleep_soc = 1000*di->count_sleep_time*100
							/3600/div(di->fcc);
				DBG("<%s> sleep_soc=%lu, real_soc=%d\n",
				    __func__, sleep_soc, di->real_soc);
				if (sleep_soc > 0)
					di->count_sleep_time = 0;
				di->real_soc += sleep_soc;
				if (di->real_soc > 100)
					di->real_soc = 100;
			}
		} else {
			DBG("<%s>, usb charge\n", __func__);
			if ((di->temp_soc - di->suspend_rsoc) > 0)
				di->real_soc +=
					(di->temp_soc - di->suspend_rsoc);
		}

		DBG("<%s>, out: dsoc=%d, rsoc=%d\n",
		    __func__, di->real_soc, di->temp_soc);
	}
}

/*
 * we need flag "sys_wakeup" to make sure that the system is reall power up.
 * because there is fake system power up which causes suspend param be cleaned.
 */
static void update_resume_state(struct battery_info *di)
{
	if (di->resume) {
		di->resume = false;
		di->sys_wakeup = true;
		/*update the info first*/
		rk81x_update_battery_info(di);
		reset_zero_var(di);

		if (di->sleep_status == POWER_SUPPLY_STATUS_DISCHARGING)
			rk81x_sleep_discharge(di);

		else if (di->sleep_status == POWER_SUPPLY_STATUS_CHARGING)
			rk81x_sleep_charge(di);
	}
}

static void rk81x_set_charger_current(struct battery_info *di,
				      enum charger_type_t charger_type)
{
	switch (charger_type) {
	case NO_CHARGER:
	case USB_CHARGER:
		set_charge_current(di, ILIM_450MA);
		break;

	case AC_CHARGER:
	case DC_CHARGER:
		set_charge_current(di, di->chg_i_lmt);
		break;
	default:
		set_charge_current(di, ILIM_450MA);
	}
}


static void rk81x_set_power_supply_state(struct battery_info *di,
					 enum charger_type_t  charger_type)
{
	di->usb_online = OFFLINE;
	di->ac_online = OFFLINE;
	di->dc_online = OFFLINE;

	switch (charger_type) {
	case NO_CHARGER:
		di->status = POWER_SUPPLY_STATUS_DISCHARGING;
		break;

	case USB_CHARGER:
		di->usb_online = ONLINE;
		di->status = POWER_SUPPLY_STATUS_CHARGING;
		break;

	case DC_CHARGER:/*treat dc as ac*/
		di->dc_online = ONLINE;
	case AC_CHARGER:
		di->ac_online = ONLINE;
		di->status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	default:
		di->status = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	if (di->real_soc >= 100)
		di->status = POWER_SUPPLY_STATUS_FULL;
}

static void rk81x_check_battery_status(struct battery_info *di)
{
	enum charger_type_t  charger_type;

	charger_type = rk81x_get_adp_type(di);
	rk81x_set_charger_current(di, charger_type);
	rk81x_set_power_supply_state(di, charger_type);
}


/* high load: current < 0 with charger in.
 * System will not shutdown while dsoc=0% with charging state(ac_online),
 * which will cause over discharge, so oppose status before report states.
 */
static void last_check_report(struct battery_info *di)
{
	static u32 time;

	if ((di->real_soc == 0) &&
	    (di->status == POWER_SUPPLY_STATUS_CHARGING) &&
	   di->current_avg < 0) {
		if (BASE_TO_SEC(time) > 60)
			rk81x_set_power_supply_state(di, NO_CHARGER);

		DBG("dsoc=0, time=%ld\n", get_seconds() - time);
		DBG("status=%d, ac_online=%d, usb_online=%d\n",
		    di->status, di->ac_online, di->usb_online);

	} else {
		time = get_seconds();
	}
}
/*
 * only do report when there is a change.
 *
 * if ((di->real_soc == 0) && (di->fg_drv_mode == FG_NORMAL_MODE)):
 * when real_soc == 0, we must do report. But it will generate too much android
 * info when we enter test_power mode without battery, so we add a fg_drv_mode
 * ajudgement.
 */
static void report_power_supply_changed(struct battery_info *di)
{
	static u32 old_soc;
	static u32 old_ac_status;
	static u32 old_usb_status;
	static u32 old_charge_status;
	bool state_changed;

	state_changed = false;
	if ((di->real_soc == 0) && (di->fg_drv_mode == FG_NORMAL_MODE))
		state_changed = true;
	else if (di->real_soc != old_soc)
		state_changed = true;
	else if (di->ac_online != old_ac_status)
		state_changed = true;
	else if (di->usb_online != old_usb_status)
		state_changed = true;
	else if (old_charge_status != di->status)
		state_changed = true;

	if (state_changed) {
		power_supply_changed(&di->bat);
		power_supply_changed(&di->usb);
		power_supply_changed(&di->ac);
		old_soc = di->real_soc;
		old_ac_status = di->ac_online;
		old_usb_status = di->usb_online;
		old_charge_status = di->status;
		DBG("<%s>. report: dsoc=%d, rsoc=%d\n",
		    __func__, di->real_soc, di->temp_soc);
	}
}

static void upd_time_table(struct battery_info *di)
{
	u8 i;
	static int old_index;
	static int old_min;
	u32 time;
	int mod = di->real_soc % 10;
	int index = di->real_soc / 10;

	if (di->ac_online == ONLINE || di->usb_online == ONLINE)
		time = di->charge_min;
	else
		time = di->discharge_min;

	if ((mod == 0) && (index > 0) && (old_index != index)) {
		di->chrg_min[index-1] = time - old_min;
		old_min = time;
		old_index = index;
	}

	for (i = 1; i < 11; i++)
		DBG("Time[%d]=%d, ", (i*10), di->chrg_min[i-1]);
	DBG("\n");
}

/*
 * there is a timer inside rk81x to calc how long the battery is in charging
 * state. rk81x will close PowerPath inside IC when timer reach, which will
 * stop the charging work. we have to reset the corresponding bits to restart
 * the timer to avoid that case.
 */
static void rk81x_init_chrg_timer(struct battery_info *di)
{
	u8 buf;

	battery_read(di->rk818, CHRG_CTRL_REG3, &buf, 1);
	buf &= ~(0x4);
	battery_write(di->rk818, CHRG_CTRL_REG3, &buf, 1);
	battery_read(di->rk818, CHRG_CTRL_REG3, &buf, 1);
	DBG("%s: clr: CHRG_CTRL_REG3<2> = 0x%x", __func__, buf);
	buf |= 0x04;
	battery_write(di->rk818, CHRG_CTRL_REG3, &buf, 1);
}

static u8 get_cvcc_charge_hour(struct battery_info *di)
{
	u8 hour, buf;

	battery_read(di->rk818, CHRG_CTRL_REG2, &buf, 1);
	hour = buf & 0x07;

	return CHG_CVCC_HOUR[hour];
}

/* we have to estimate the charging finish time from now, to decide
 * whether we should reset the timer or not.
 */
static void rk81x_check_chrg_over_time(struct battery_info *di)
{
	u8 cvcc_hour;

	cvcc_hour = get_cvcc_charge_hour(di);
	DBG("CHG_TIME(min): %ld, cvcc hour: %d",
	    BASE_TO_MIN(di->chrg_time_base), cvcc_hour);

	if (BASE_TO_MIN(di->chrg_time_base) >= (cvcc_hour-2)*60) {
		di->chrg_cap2_full = di->fcc - di->remain_capacity;
		if (di->current_avg <= 0)
			di->current_avg = 1;

		di->chrg_time2_full = di->chrg_cap2_full*3600/
					div(abs_int(di->current_avg));

		DBG("CHG_TIME2FULL(min):%d, chrg_cap2_full=%d, current=%d\n",
		    SEC_TO_MIN(di->chrg_time2_full), di->chrg_cap2_full,
		    di->current_avg);

		if (SEC_TO_MIN(di->chrg_time2_full) > 60) {
			rk81x_init_chrg_timer(di);
			di->chrg_time_base = get_seconds();
			DBG("%s: reset charge timer\n", __func__);
		}
	}
}

/*
 * in case that we will do reboot stress test, we need a special way
 * to ajust the dsoc.
 */
static void rk81x_check_reboot(struct battery_info *di)
{
	u8 rsoc = di->temp_soc;
	u8 dsoc = di->real_soc;
	u8 status = di->status;
	u8 cnt;
	int unit_time;
	int smooth_time;

	battery_read(di->rk818, REBOOT_CNT_REG, &cnt, 1);
	cnt++;

	unit_time = di->fcc*3600/100/1200;/*1200mA default*/
	smooth_time = cnt*BASE_TO_SEC(di->sys_on_base);

	DBG("%s: cnt:%d, unit:%d, sm:%d, sec:%lu, dsoc:%d, rsoc:%d\n",
	    __func__, cnt, unit_time, smooth_time,
	    BASE_TO_SEC(di->sys_on_base), dsoc, rsoc);

	if (((status == POWER_SUPPLY_STATUS_CHARGING) ||
	     (status == POWER_SUPPLY_STATUS_FULL)) && (di->current_avg > 0)) {
		DBG("chrg, sm:%d, aim:%d\n", smooth_time, unit_time*3/5);
		if ((dsoc < rsoc-1) && (smooth_time > unit_time*3/5)) {
			cnt = 0;
			dsoc++;
			if (dsoc >= 100)
				dsoc = 100;
			_copy_soc(di, dsoc);
		}
	} else {/*status == POWER_SUPPLY_STATUS_DISCHARGING*/

		DBG("dischrg, sm:%d, aim:%d\n", smooth_time, unit_time*3/5);
		if ((dsoc > rsoc) && (smooth_time > unit_time*3/5)) {
			cnt = 0;
			dsoc--;
			if (dsoc <= 0)
				dsoc = 0;
			_copy_soc(di, dsoc);
		}
	}

	copy_reboot_cnt(di, cnt);
}


static void rk81x_update_battery_info(struct battery_info *di)
{
	int round_off_dsoc;

	di->remain_capacity = _get_realtime_capacity(di);
	if (di->remain_capacity > di->fcc)
		_capacity_init(di, di->fcc);

	if (di->real_soc > 100)
		di->real_soc = 100;
	else if (di->real_soc < 0)
		di->real_soc = 0;

	if (di->chrg_time_base == 0)
		di->chrg_time_base = get_seconds();

	if (di->sys_on_base == 0)
		di->sys_on_base = get_seconds();

	if (di->status == POWER_SUPPLY_STATUS_DISCHARGING) {
		di->chrg_time_base = get_seconds();

		/*round off dsoc = 100*/
		round_off_dsoc = (di->remain_capacity+di->fcc/100/2)*
					100/div(di->fcc);
		if (round_off_dsoc >= 100 && di->real_soc >= 99)
			di->real_soc = 100;
		DBG("<%s>. round_off_dsoc = %d", __func__, round_off_dsoc);
	}

	di->work_on = 1;
	di->voltage  = _get_battery_voltage(di);
	di->current_avg = _get_average_current(di);
	di->remain_capacity = _get_realtime_capacity(di);
	di->voltage_ocv = _get_OCV_voltage(di);
	di->charge_status = rk81x_get_charge_status(di);
	di->relax_voltage = get_relax_voltage(di);
	di->temp_soc = _get_soc(di);
	di->est_ocv_vol = estimate_bat_ocv_vol(di);
	di->est_ocv_soc = estimate_bat_ocv_soc(di);
	rk81x_check_battery_status(di);/* ac_online, usb_online, status*/
	rk81x_check_chrg_over_time(di);
	update_cal_offset(di);
}

static void rk81x_battery_work(struct work_struct *work)
{
	struct battery_info *di = container_of(work,
			struct battery_info, battery_monitor_work.work);

	update_resume_state(di);
	wait_charge_finish_signal(di);
	charge_finish_routine(di);

	rk81x_battery_display_smooth(di);
	rk81x_update_battery_info(di);
	rsoc_realtime_calib(di);
	last_check_report(di);
	report_power_supply_changed(di);
	_copy_soc(di, di->real_soc);
	_save_remain_capacity(di, di->remain_capacity);

	dump_debug_info(di);
	di->queue_work_cnt++;
	queue_delayed_work(di->wq, &di->battery_monitor_work,
			   msecs_to_jiffies(TIMER_MS_COUNTS));
}

static void rk81x_battery_charge_check_work(struct work_struct *work)
{
	struct battery_info *di = container_of(work,
			struct battery_info, charge_check_work.work);

	DBG("rk81x_battery_charge_check_work\n");
	charge_disable_open_otg(di->charge_otg);
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

static void poweron_lowerpoer_handle(struct battery_info *di)
{
#ifdef CONFIG_LOGO_LOWERPOWER_WARNING
	if ((di->real_soc <= 2) &&
	    (di->status == POWER_SUPPLY_STATUS_DISCHARGING)) {
		mdelay(1500);
		/* kernel_power_off(); */
	}
#endif
}

static int battery_notifier_call(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	struct battery_info *di =
	    container_of(nb, struct battery_info, battery_nb);

	switch (event) {
	case 0:
		DBG(" CHARGE enable\n");
		di->charge_otg = 0;
		queue_delayed_work(di->wq, &di->charge_check_work,
				   msecs_to_jiffies(50));
		break;

	case 1:
		di->charge_otg  = 1;
		queue_delayed_work(di->wq, &di->charge_check_work,
				   msecs_to_jiffies(50));
		DBG("charge disable OTG enable\n");
		break;

	case 2:
		poweron_lowerpoer_handle(di);
		break;

	default:
		return NOTIFY_OK;
	}
	return NOTIFY_OK;
}

static irqreturn_t rk818_vbat_lo_irq(int irq, void *di)
{
	pr_info("<%s>lower power warning!\n", __func__);

	rk_send_wakeup_key();
	kernel_power_off();
	return IRQ_HANDLED;
}

static void disable_vbat_low_irq(struct battery_info *di)
{
	/* mask vbat low */
	rk818_set_bits(di->rk818, 0x4d, (0x1 << 1), (0x1 << 1));
	/*clr vbat low interrupt */
	/* rk818_set_bits(di->rk818, 0x4c, (0x1 << 1), (0x1 << 1));*/
}
static void enable_vbat_low_irq(struct battery_info *di)
{
	/* clr vbat low interrupt */
	rk818_set_bits(di->rk818, 0x4c, (0x1 << 1), (0x1 << 1));
	/* mask vbat low */
	rk818_set_bits(di->rk818, 0x4d, (0x1 << 1), (0x0 << 1));
}

static irqreturn_t rk818_vbat_plug_in(int irq, void *di)
{
	pr_info("\n------- %s:irq = %d\n", __func__, irq);
	g_battery->chrg_time_base = get_seconds();
	rk_send_wakeup_key();
	return IRQ_HANDLED;
}
static irqreturn_t rk818_vbat_plug_out(int irq, void  *di)
{
	pr_info("\n-------- %s:irq = %d\n", __func__, irq);
	charge_disable_open_otg(0);
	rk_send_wakeup_key();
	return IRQ_HANDLED;
}

static irqreturn_t rk818_vbat_charge_ok(int irq, void  *di)
{
	pr_info("---------- %s:irq = %d\n", __func__, irq);
	rk_send_wakeup_key();
	return IRQ_HANDLED;
}

static int rk81x_battery_sysfs_init(struct battery_info *di, struct device *dev)
{
	int ret;
	int i;
	struct kobject *rk818_fg_kobj;

	ret = create_sysfs_interfaces(dev);
	if (ret < 0) {
		ret = -EINVAL;
		dev_err(dev, "device RK818 battery sysfs register failed\n");
		goto err_sysfs;
	}

	rk818_fg_kobj = kobject_create_and_add("rk818_battery", NULL);
	if (!rk818_fg_kobj)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(rk818_bat_attr); i++) {
		ret = sysfs_create_file(rk818_fg_kobj, &rk818_bat_attr[i].attr);
		if (ret != 0) {
			dev_err(dev, "create rk818_battery node error\n");
			goto err_sysfs;
		}
	}

	return ret;

err_sysfs:
	power_supply_unregister(&di->ac);
	power_supply_unregister(&di->usb);
	power_supply_unregister(&di->bat);

	return ret;
}

static void rk81x_battery_irq_init(struct battery_info *di)
{
	int plug_in_irq, plug_out_irq, chg_ok_irq, vb_lo_irq;
	int ret;
	struct rk818 *chip = di->rk818;

	vb_lo_irq = irq_create_mapping(chip->irq_domain, RK818_IRQ_VB_LO);
	plug_in_irq = irq_create_mapping(chip->irq_domain, RK818_IRQ_PLUG_IN);
	plug_out_irq = irq_create_mapping(chip->irq_domain, RK818_IRQ_PLUG_OUT);
	chg_ok_irq = irq_create_mapping(chip->irq_domain, RK818_IRQ_CHG_OK);

	ret = request_threaded_irq(vb_lo_irq, NULL, rk818_vbat_lo_irq,
				   IRQF_TRIGGER_HIGH, "rk818_vbatlow", chip);
	if (ret != 0)
		dev_err(chip->dev, "vb_lo_irq request failed!\n");

	di->irq = vb_lo_irq;
	enable_irq_wake(di->irq);
	disable_vbat_low_irq(di);

	ret = request_threaded_irq(plug_in_irq, NULL, rk818_vbat_plug_in,
				   IRQF_TRIGGER_RISING, "rk818_vbat_plug_in",
				   chip);
	if (ret != 0)
		dev_err(chip->dev, "plug_in_irq request failed!\n");


	ret = request_threaded_irq(plug_out_irq, NULL, rk818_vbat_plug_out,
				   IRQF_TRIGGER_FALLING, "rk818_vbat_plug_out",
				   chip);
	if (ret != 0)
		dev_err(chip->dev, "plug_out_irq request failed!\n");


	ret = request_threaded_irq(chg_ok_irq, NULL, rk818_vbat_charge_ok,
				   IRQF_TRIGGER_RISING, "rk818_vbat_charge_ok",
				   chip);
	if (ret != 0)
		dev_err(chip->dev, "chg_ok_irq request failed!\n");
}


static void rk81x_battery_info_init(struct battery_info *di, struct rk818 *chip)
{
	int fcc_capacity;
	u8 i;

	g_battery = di;
	di->platform_data = chip->battery_data;
	di->cell.config = di->platform_data->cell_cfg;
	di->design_capacity = di->platform_data->cell_cfg->design_capacity;
	di->qmax = di->platform_data->cell_cfg->design_qmax;
	di->fcc = di->design_capacity;
	di->vol_smooth_time = 0;
	di->charge_smooth_time = 0;
	di->charge_smooth_status = false;
	di->sleep_status = 0;
	di->work_on = 0;
	di->sys_wakeup = true;
	di->pcb_ioffset = 0;
	di->pcb_ioffset_updated = false;
	di->queue_work_cnt = 0;
	di->update_k = 0;
	di->voltage_old = 0;
	di->display_soc = 0;
	di->bat_res = 0;
	di->resume = false;
	di->sys_wakeup = true;
	di->status = POWER_SUPPLY_STATUS_DISCHARGING;
	di->finish_min = 0;
	di->charge_min = 0;
	di->discharge_min = 0;
	di->charging_time = 0;
	di->discharging_time = 0;
	di->finish_time = 0;
	di->q_dead = 0;
	di->q_err = 0;
	di->q_shtd = 0;
	di->odd_capacity = 0;
	di->bat_res = di->rk818->battery_data->sense_resistor_mohm;
	di->term_chg_cnt = 0;
	di->emu_chg_cnt = 0;
	di->zero_cycle = 0;
	di->chrg_time_base = 0;
	di->sys_on_base = 0;
	di->sum_suspend_cap = 0;
	di->adjust_cap = 0;
	di->first_on_cap = 0;
	di->fg_drv_mode = FG_NORMAL_MODE;

	for (i = 0; i < 10; i++)
		di->chrg_min[i] = 0;

	di->debug_finish_real_soc = 0;
	di->debug_finish_temp_soc = 0;

	fcc_capacity = _get_FCC_capacity(di);
	if (fcc_capacity > 1000)
		di->fcc = fcc_capacity;
	else
		di->fcc = di->design_capacity;
}
/*
static struct of_device_id rk818_battery_of_match[] = {
{ .compatible = "rk818_battery" },
{ }
};

MODULE_DEVICE_TABLE(of, rk818_battery_of_match);
*/


/*
 * dc_det_pullup_inside:
 *
 *  0:  thers is resistance in the pcb to pull the pin up;
 *  1:  there is no resistance in the pcb to pull the pin up.
 *	we have to use inside pullup resistance function,
 *      so we have to define pinctrl info in DTS and analyze it
 */
static void rk81x_dc_det_init(struct battery_info *di,
			      struct device_node *np)
{
	struct device *dev = di->dev;
	struct rk818 *rk818 = di->rk818;
	enum of_gpio_flags flags;
	int ret;

	/*thers is resistance in the pcb to pull the pin up*/
	if (!di->dc_det_pullup_inside)
		goto out;

	/*there is no resistance in the pcb to pull the pin up.*/
	di->pinctrl = devm_pinctrl_get(rk818->dev);
	if (IS_ERR(di->pinctrl)) {
		dev_err(dev, "No pinctrl used!\n");
		return;
	}

	/* lookup default state */
	di->pins_default = pinctrl_lookup_state(di->pinctrl, "default");
	if (IS_ERR(di->pins_default)) {
		dev_err(dev, "No default pinctrl found!\n");
	} else {
		ret = pinctrl_select_state(di->pinctrl, di->pins_default);
		if (ret < 0) {
			dev_err(dev, "Default pinctrl setting failed!\n");
		} else {
out:
			di->dc_det_pin = of_get_named_gpio_flags(np,
						"dc_det_gpio", 0, &flags);
			if (di->dc_det_pin == -EPROBE_DEFER)
				dev_err(dev, "dc_det_gpio error\n");
			if (gpio_is_valid(di->dc_det_pin))
				di->dc_det_level =
					(flags & OF_GPIO_ACTIVE_LOW) ?
						RK818_DC_IN : RK818_DC_OUT;
		}
	}
}

#ifdef CONFIG_OF
static int rk81x_battery_parse_dt(struct battery_info *di)
{
	struct device_node *regs, *rk818_pmic_np, *test_np;
	struct battery_platform_data *data;
	struct cell_config *cell_cfg;
	struct ocv_config *ocv_cfg;
	struct property *prop;
	struct rk818 *rk818 = di->rk818;
	struct device *dev = di->dev;
	u32 out_value;
	int length, ret;

	rk818_pmic_np = of_node_get(rk818->dev->of_node);
	if (!rk818_pmic_np) {
		dev_err(dev, "could not find pmic sub-node\n");
		return -EINVAL;
	}

	regs = of_find_node_by_name(rk818_pmic_np, "battery");
	if (!regs) {
		dev_err(dev, "battery node not found!\n");
		return -EINVAL;
	}

	data = devm_kzalloc(rk818->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(dev, "kzalloc for battery_platform_data failed!\n");
		return -ENOMEM;
	}

	cell_cfg = devm_kzalloc(rk818->dev, sizeof(*cell_cfg), GFP_KERNEL);
	if (!cell_cfg) {
		dev_err(dev, "kzalloc for cell_config failed!\n");
		return -ENOMEM;
	}
	ocv_cfg = devm_kzalloc(rk818->dev, sizeof(*ocv_cfg), GFP_KERNEL);
	if (!ocv_cfg) {
		dev_err(dev, "kzalloc for ocv_config failed!\n");
		return -ENOMEM;
	}

	prop = of_find_property(regs, "ocv_table", &length);
	if (!prop) {
		dev_err(dev, "ocv_table not found!\n");
		return -EINVAL;
	}
	data->ocv_size = length / sizeof(u32);

	if (data->ocv_size > 0) {
		size_t size = sizeof(*data->battery_ocv) * data->ocv_size;

		data->battery_ocv = devm_kzalloc(rk818->dev, size, GFP_KERNEL);
		if (!data->battery_ocv) {
			dev_err(dev, "kzalloc for ocv_table failed!\n");
			return -ENOMEM;
		}
		ret = of_property_read_u32_array(regs, "ocv_table",
						 data->battery_ocv,
						 data->ocv_size);
		if (ret < 0)
			return ret;
	}

	/******************** charger param  ****************************/
	ret = of_property_read_u32(regs, "max_charge_currentmA", &out_value);
	if (ret < 0) {
		dev_err(dev, "max_charge_currentmA not found!\n");
		out_value = DEFAULT_ICUR;
	}
	data->max_charger_currentmA = out_value;

	ret = of_property_read_u32(regs, "max_charge_ilimitmA", &out_value);
	if (ret < 0) {
		dev_err(dev, "max_charger_ilimitmA not found!\n");
		out_value = DEFAULT_ILMT;
	}
	data->max_charger_ilimitmA = out_value;

	ret = of_property_read_u32(regs, "bat_res", &out_value);
	if (ret < 0) {
		dev_err(dev, "bat_res not found!\n");
		out_value = DEFAULT_BAT_RES;
	}
	data->sense_resistor_mohm = out_value;

	ret = of_property_read_u32(regs, "max_charge_voltagemV", &out_value);
	if (ret < 0) {
		dev_err(dev, "max_charge_voltagemV not found!\n");
		out_value = DEFAULT_VLMT;
	}
	data->max_charger_voltagemV = out_value;

	ret = of_property_read_u32(regs, "design_capacity", &out_value);
	if (ret < 0) {
		dev_err(dev, "design_capacity not found!\n");
		return ret;
	}
	cell_cfg->design_capacity  = out_value;

	ret = of_property_read_u32(regs, "design_qmax", &out_value);
	if (ret < 0) {
		dev_err(dev, "design_qmax not found!\n");
		return ret;
	}
	cell_cfg->design_qmax = out_value;

	ret = of_property_read_u32(regs, "sleep_enter_current", &out_value);
	if (ret < 0) {
		dev_err(dev, "sleep_enter_current not found!\n");
		return ret;
	}
	ocv_cfg->sleep_enter_current = out_value;

	ret = of_property_read_u32(regs, "sleep_exit_current", &out_value);
	if (ret < 0) {
		dev_err(dev, "sleep_exit_current not found!\n");
		return ret;
	}
	ocv_cfg->sleep_exit_current = out_value;

	/********************  test power param ****************************/
	test_np = of_find_node_by_name(regs, "test_power");
	if (!regs) {
		dev_err(dev, "test-power node not found!\n");
		di->test_chrg_current = DEF_TEST_CURRENT_MA;
		di->test_chrg_ilmt = DEF_TEST_ILMT_MA;
	} else {
		ret = of_property_read_u32(test_np, "test_charge_currentmA",
					   &out_value);
		if (ret < 0) {
			dev_err(dev, "test_charge_currentmA not found!\n");
			out_value = DEF_TEST_CURRENT_MA;
		}
		di->test_chrg_current = out_value;

		ret = of_property_read_u32(test_np, "test_charge_ilimitmA",
					   &out_value);
		if (ret < 0) {
			dev_err(dev, "test_charge_ilimitmA not found!\n");
			out_value = DEF_TEST_ILMT_MA;
		}
		di->test_chrg_ilmt = out_value;
	}

	/*************  charger support adp types **********************/
	ret = of_property_read_u32(regs, "support_uboot_chrg", &support_uboot_chrg);
	ret = of_property_read_u32(regs, "support_usb_adp", &support_usb_adp);
	ret = of_property_read_u32(regs, "support_dc_adp", &support_dc_adp);
	ret = of_property_read_u32(regs, "dc_det_pullup_inside", &out_value);
	if (ret < 0)
		out_value = 0;
	di->dc_det_pullup_inside = out_value;

	if (!support_usb_adp && !support_dc_adp) {
		dev_err(dev, "miss both: usb_adp and dc_adp,default:usb_adp!\n");
		support_usb_adp = 1;
	}

	if (support_dc_adp)
		rk81x_dc_det_init(di, regs);

	cell_cfg->ocv = ocv_cfg;
	data->cell_cfg = cell_cfg;
	rk818->battery_data = data;

	DBG("\n--------- the battery OCV TABLE dump:\n");
	DBG("bat_res :%d\n", data->sense_resistor_mohm);
	DBG("max_charge_ilimitmA :%d\n", data->max_charger_ilimitmA);
	DBG("max_charge_currentmA :%d\n", data->max_charger_currentmA);
	DBG("max_charge_voltagemV :%d\n", data->max_charger_voltagemV);
	DBG("design_capacity :%d\n", cell_cfg->design_capacity);
	DBG("design_qmax :%d\n", cell_cfg->design_qmax);
	DBG("sleep_enter_current :%d\n", cell_cfg->ocv->sleep_enter_current);
	DBG("sleep_exit_current :%d\n", cell_cfg->ocv->sleep_exit_current);
	DBG("support_uboot_chrg = %d\n", support_uboot_chrg);
	DBG("support_usb_adp = %d\n", support_usb_adp);
	DBG("support_dc_adp= %d\n", support_dc_adp);
	DBG("test_charge_currentmA = %d\n", di->test_chrg_current);
	DBG("test_charge_ilimitmA = %d\n", di->test_chrg_ilmt);
	DBG("dc_det_pullup_inside = %d\n", di->dc_det_pullup_inside);
	DBG("--------- rk818_battery dt_parse ok.\n");
	return 0;
}

#else
static int rk81x_battery_parse_dt(struct battery_info *di)
{
	return -ENODEV;
}
#endif


static int rk81x_battery_probe(struct platform_device *pdev)
{
	struct rk818 *chip = dev_get_drvdata(pdev->dev.parent);
	struct battery_info *di;
	int ret;

	DBG("battery driver version %s\n", DRIVER_VERSION);
	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&pdev->dev, "kzalloc di failed!\n");
		return -ENOMEM;
	}
	di->rk818 = chip;
	di->dev = &pdev->dev;
	platform_set_drvdata(pdev, di);

	ret = rk81x_battery_parse_dt(di);
	if (ret < 0) {
		dev_err(&pdev->dev, "rk81x battery parse dt failed!\n");
		return ret;
	}
	rk81x_battery_info_init(di, chip);
	if (!is_rk81x_bat_exist(di)) {
		pr_info("not find Li-ion battery, test power mode\n");
		rk81x_battery_charger_init(di);
		di->fg_drv_mode = TEST_POWER_MODE;
	}

	battery_power_supply_init(di);
	ret = battery_power_supply_register(di);
	if (ret) {
		dev_err(&pdev->dev, "rk81x power supply register failed!\n");
		return ret;
	}

	rk81x_battery_irq_init(di);
	rk81x_battery_sysfs_init(di, &pdev->dev);

	rk81x_fg_init(di);
	wake_lock_init(&di->resume_wake_lock, WAKE_LOCK_SUSPEND,
		       "resume_charging");
	flatzone_voltage_init(di);
	rk81x_check_battery_status(di);

	di->wq = create_singlethread_workqueue("rk81x-battery-work");
	INIT_DELAYED_WORK(&di->battery_monitor_work, rk81x_battery_work);
	queue_delayed_work(di->wq, &di->battery_monitor_work,
			   msecs_to_jiffies(TIMER_MS_COUNTS*5));
	INIT_DELAYED_WORK(&di->charge_check_work,
			  rk81x_battery_charge_check_work);
	di->battery_nb.notifier_call = battery_notifier_call;
	register_battery_notifier(&di->battery_nb);

	DBG("rk81x battery probe ok!\n");

	return ret;
}


#ifdef CONFIG_PM

static int rk81x_battery_suspend(struct platform_device *dev,
				 pm_message_t state)
{
	struct battery_info *di = platform_get_drvdata(dev);

	enable_vbat_low_irq(di);
	di->sleep_status = di->status;

	/* avoid abrupt wakeup which will clean the variable*/
	if (di->sys_wakeup) {
		di->suspend_cap = di->remain_capacity;
		di->suspend_rsoc = _get_soc(di);
		di->suspend_time_start = get_seconds();
		di->sys_wakeup = false;
	}

	pr_info("rk81x-battery suspend: v=%d ld=%d lr=%d c=%d chg=%d\n",
		_get_battery_voltage(di), di->real_soc, _get_soc(di),
		_get_average_current(di), di->status);

	cancel_delayed_work(&di->battery_monitor_work);

	return 0;
}

static int rk81x_battery_resume(struct platform_device *dev)
{
	struct battery_info *di = platform_get_drvdata(dev);

	di->resume = true;
	disable_vbat_low_irq(di);
	queue_delayed_work(di->wq, &di->battery_monitor_work,
			   msecs_to_jiffies(TIMER_MS_COUNTS/2));

	if (di->sleep_status == POWER_SUPPLY_STATUS_CHARGING ||
	    di->real_soc <= 5)
		wake_lock_timeout(&di->resume_wake_lock, 5*HZ);

	pr_info("rk81x-battery resume: v=%d  rv=%d ld=%d lr=%d c=%d chg=%d\n",
		_get_battery_voltage(di), get_relax_voltage(di),
		di->real_soc, _get_soc(di), _get_average_current(di),
		di->status);
	return 0;
}
static int rk81x_battery_remove(struct platform_device *dev)
{
	struct battery_info *di = platform_get_drvdata(dev);

	cancel_delayed_work_sync(&di->battery_monitor_work);
	return 0;
}
static void rk81x_battery_shutdown(struct platform_device *dev)
{
	struct battery_info *di = platform_get_drvdata(dev);

	cancel_delayed_work_sync(&di->battery_monitor_work);
	if (BASE_TO_MIN(di->sys_on_base) <= REBOOT_INTER_MIN)
		rk81x_check_reboot(di);
	else
		copy_reboot_cnt(di, 0);
	DBG("rk818 shutdown!");
}
#endif

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
