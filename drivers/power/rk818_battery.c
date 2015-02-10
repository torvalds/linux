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
#include <asm/uaccess.h>
#include <linux/power/rk818_battery.h>
#include <linux/mfd/rk818.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/wakelock.h>

/* if you  want to disable, don't set it as 0, just be: "static int dbg_enable;" is ok*/
static int dbg_enable;
#define RK818_SYS_DBG 1

module_param_named(dbg_level, dbg_enable, int, 0644);
#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)


#define INTERPOLATE_MAX				1000
#define MAX_INT 						0x7FFF
#define TIME_10MIN_SEC				600

struct battery_info {
	struct device 		*dev;
	struct cell_state	cell;
	struct power_supply	bat;
	struct power_supply	ac;
	struct power_supply	usb;
	struct delayed_work work;
	/* struct i2c_client	*client; */
	struct rk818 		*rk818;

	struct battery_platform_data *platform_data;

	int				work_on;
	int				irq;
	int				ac_online;
	int				usb_online;
	int				status;
	int				current_avg;
	int				current_offset;

	uint16_t 			voltage;
	uint16_t			voltage_ocv;
	uint16_t			relax_voltage;
	u8				charge_status;
	u8				otg_status;
	int				pcb_ioffset;
	bool				pcb_ioffset_updated;
	unsigned long	 	queue_work_cnt;
	uint16_t			warnning_voltage;

	int				design_capacity;
	int				fcc;
	int				qmax;
	int				remain_capacity;
	int				nac;
	int				temp_nac;

	int				real_soc;
	int				display_soc;
	int				temp_soc;

	int 				bat_res_update_cnt;
	int				soc_counter;

	int				dod0;
	int				dod0_status;
	int				dod0_voltage;
	int				dod0_capacity;
	unsigned long		dod0_time;
	u8				dod0_level;
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

	int 				update_k;
	int 				line_k;
	int 				line_q;
	int 				update_q;
	int 				voltage_old;

	u8				check_count;
	/* u32			status; */
	struct timeval 		soc_timer;
	struct timeval 		change_timer;

	int 				vol_smooth_time;
	int				charge_smooth_time;

	int				suspend_capacity;
	int 				resume_capacity;
	struct timespec	suspend_time;
	struct timespec 	resume_time;
	unsigned long		suspend_time_start;
	unsigned long		count_sleep_time;

	unsigned long		dischrg_sum_sleep_sec;
	unsigned long		dischrg_sum_sleep_capacity;
	int				suspend_temp_soc;
	int				sleep_status;
	int				suspend_charge_current;
	int				resume_soc;
	int				bat_res;
	bool				bat_res_updated;
	bool				charge_smooth_status;
	bool            		resume;
	unsigned long		last_plugin_time;
	bool 				sys_wakeup;

	unsigned long		charging_time;
	unsigned long		discharging_time;

	struct notifier_block battery_nb;
	struct workqueue_struct *wq;
	struct delayed_work	battery_monitor_work;
	struct delayed_work	charge_check_work;
	int					charge_otg;

	struct wake_lock  resume_wake_lock;

	int 	debug_finish_real_soc;
	int	debug_finish_temp_soc;
};

struct battery_info *g_battery;
u32 support_uboot_chrg;

extern int dwc_vbus_status(void);
extern int get_gadget_connect_flag(void);
extern int dwc_otg_check_dpdm(void);
extern void kernel_power_off(void);
extern int rk818_set_bits(struct rk818 *rk818, u8 reg, u8 mask, u8 val);
extern unsigned int irq_create_mapping(struct irq_domain *domain,
											irq_hw_number_t hwirq);
extern void rk_send_wakeup_key(void);
static void update_battery_info(struct battery_info *di);

#define	SUPPORT_USB_CHARGE


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


static int battery_read(struct rk818 *rk818, u8 reg, u8 buf[], unsigned len)
{
	int ret;

	ret = rk818_i2c_read(rk818, reg, len, buf);
	return ret;
}

static int battery_write(struct rk818 *rk818, u8 reg, u8 const buf[], unsigned len)
{
	int ret;
	ret = rk818_i2c_write(rk818, reg, (int)len, *buf);
	return ret;
}
static void dump_gauge_register(struct battery_info *di)
{
	int i = 0;
	char buf;
	DBG("%s dump charger register start: \n", __func__);
	for (i = 0xAC; i < 0xDF; i++) {
		battery_read(di->rk818, i, &buf, 1);
		DBG(" the register is  0x%02x, the value is 0x%02x\n ", i, buf);
	}
	DBG("demp end!\n");
}

static void dump_charger_register(struct battery_info *di)
{

	int i = 0;
	char buf;
	DBG("%s dump the register start: \n", __func__);
	for (i = 0x99; i < 0xAB; i++) {
		battery_read(di->rk818, i, &buf, 1);
		DBG(" the register is  0x%02x, the value is 0x%02x\n ", i, buf);
	}
	DBG("demp end!\n");

}

#if RK818_SYS_DBG

static uint16_t _get_OCV_voltage(struct battery_info *di);
static int _voltage_to_capacity(struct battery_info *di, int voltage);
static int _get_realtime_capacity(struct battery_info *di);
static void power_on_save(struct   battery_info *di, int voltage);
static void  _capacity_init(struct battery_info *di, u32 capacity);
static void battery_poweron_status_init(struct battery_info *di);
static void power_on_save(struct   battery_info *di, int voltage);
static void flatzone_voltage_init(struct battery_info *di);
static int _get_FCC_capacity(struct battery_info *di);
static void  _save_FCC_capacity(struct battery_info *di, u32 capacity);
static int _get_soc(struct   battery_info *di);
static int  _get_average_current(struct battery_info *di);
static int rk_battery_voltage(struct battery_info *di);
static uint16_t _get_relax_vol1(struct battery_info *di);
static uint16_t _get_relax_vol2(struct battery_info *di);
static void update_battery_info(struct battery_info *di);

static ssize_t bat_state_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;
	u8 status;
	u8 rtc_val;
	u8 soc_reg;
	u8 shtd_time;

	battery_read(di->rk818, SUP_STS_REG, &status, 1);
	battery_read(di->rk818, SOC_REG, &soc_reg, 1);
	battery_read(di->rk818, 0x00, &rtc_val, 1);
	di->voltage_ocv = _get_OCV_voltage(di);
	_voltage_to_capacity(di, di->voltage_ocv);
	battery_read(di->rk818, NON_ACT_TIMER_CNT_REG, &shtd_time, 1);

	return sprintf(buf, "-----------------------------------------------------------------------------\n"
			"volt = %d, ocv_volt = %d, avg_current = %d, remain_cap = %d, ocv_cap = %d\n"
			"real_soc = %d, temp_soc = %d\n"
			"fcc = %d, FCC_REG = %d, shutdown_time = %d\n"
			"usb_online = %d, ac_online = %d\n"
			"SUP_STS_REG(0xc7) = 0x%02x, RTC_REG = 0x%02x\n"
			"voltage_k = %d, voltage_b = %d, SOC_REG = 0x%02x\n"
			"relax_volt1 = %d, relax_volt2 = %d\n"
			"---------------------------------------------------------------------------\n",
			rk_battery_voltage(di), di->voltage_ocv, _get_average_current(di), _get_realtime_capacity(di), di->temp_nac,
			di->real_soc, _get_soc(di),
			di->fcc, _get_FCC_capacity(di), shtd_time,
			di->usb_online, di->ac_online,
			status, rtc_val,
			di->voltage_k, di->voltage_b, soc_reg,
			_get_relax_vol1(di), _get_relax_vol2(di));
}

static ssize_t bat_reg_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;
	u8 sup_tst_reg, ggcon_reg, ggsts_reg, vb_mod_reg;
	u8 usb_ctrl_reg, chrg_ctrl_reg1;
	u8 chrg_ctrl_reg2, chrg_ctrl_reg3, rtc_val;

	battery_read(di->rk818, GGCON, &ggcon_reg, 1);
	battery_read(di->rk818, GGSTS, &ggsts_reg, 1);
	battery_read(di->rk818, SUP_STS_REG, &sup_tst_reg, 1);
	battery_read(di->rk818, VB_MOD_REG, &vb_mod_reg, 1);
	battery_read(di->rk818, USB_CTRL_REG, &usb_ctrl_reg, 1);
	battery_read(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
	battery_read(di->rk818, CHRG_CTRL_REG2, &chrg_ctrl_reg2, 1);
	battery_read(di->rk818, CHRG_CTRL_REG3, &chrg_ctrl_reg3, 1);
	battery_read(di->rk818, 0x00, &rtc_val, 1);

	return sprintf(buf, "\n------------- dump_debug_regs -----------------\n"
	    "GGCON = 0x%2x, GGSTS = 0x%2x, RTC	= 0x%2x\n"
	    "SUP_STS_REG  = 0x%2x, VB_MOD_REG	= 0x%2x\n"
	    "USB_CTRL_REG  = 0x%2x, CHRG_CTRL_REG1 = 0x%2x\n"
	    "CHRG_CTRL_REG2 = 0x%2x, CHRG_CTRL_REG3 = 0x%2x\n"
	    "---------------------------------------------------------------------------\n",
	    ggcon_reg, ggsts_reg, rtc_val,
	    sup_tst_reg, vb_mod_reg,
	    usb_ctrl_reg, chrg_ctrl_reg1,
	    chrg_ctrl_reg2, chrg_ctrl_reg3
	   );
}
static ssize_t bat_fcc_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->fcc);
}
static ssize_t bat_soc_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->real_soc);
}

static ssize_t bat_temp_soc_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->temp_soc);
}

static ssize_t bat_voltage_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->voltage);
}

static ssize_t bat_avr_current_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->current_avg);
}

static ssize_t bat_remain_capacity_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct battery_info *di = g_battery;

	return sprintf(buf, "%d", di->remain_capacity);
}

static struct device_attribute rk818_bat_attr[] = {
	__ATTR(state, 0664, bat_state_read, NULL),
	__ATTR(regs, 0664, bat_reg_read, NULL),
	__ATTR(fcc, 0664, bat_fcc_read, NULL),
	__ATTR(soc, 0664, bat_soc_read, NULL),
	__ATTR(temp_soc, 0664, bat_temp_soc_read, NULL),
	__ATTR(voltage, 0664, bat_voltage_read, NULL),
	__ATTR(avr_current, 0664, bat_avr_current_read, NULL),
	__ATTR(remain_capacity, 0664, bat_remain_capacity_read, NULL),
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
				struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static struct device_attribute rkbatt_attrs[] = {
	__ATTR(state, 0664, show_state_attrs, restore_state_attrs),
};

static int create_sysfs_interfaces(struct device *dev)
{
	int liTmep;

	for (liTmep = 0; liTmep < ARRAY_SIZE(rkbatt_attrs); liTmep++)	{
		if (device_create_file(dev, rkbatt_attrs + liTmep))
			goto error;
	}

	return 0;

error:
	for (; liTmep >= 0; liTmep--)
		device_remove_file(dev, rkbatt_attrs + liTmep);

	dev_err(dev, "%s:Unable to create sysfs interface\n", __func__);
	return -1;
}

static int debug_reg(struct battery_info *di, u8 reg, char *reg_name)
{
	u8 val;

	battery_read(di->rk818, reg, &val, 1);
	DBG("<%s>: %s = 0x%2x\n", __func__, reg_name, val);
	return val;
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
		ret = battery_write(di->rk818, TS_CTRL_REG, &buf, 1);  /* enable */
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

	di->voltage_k = (4200 - 3000)*1000/(vcalib1 - vcalib0);
	di->voltage_b = 4200 - (di->voltage_k*vcalib1)/1000;
	DBG("voltage_k = %d(x1000) voltage_b = %d\n", di->voltage_k, di->voltage_b);
}
static uint16_t _get_OCV_voltage(struct battery_info *di)
{
	int ret;
	u8 buf;
	uint16_t temp;
	uint16_t voltage_now = 0;

	ret = battery_read(di->rk818, BAT_OCV_REGL, &buf, 1);
	temp = buf;
	ret = battery_read(di->rk818, BAT_OCV_REGH, &buf, 1);
	temp |= buf<<8;

	if (ret < 0) {
		dev_err(di->dev, "error read BAT_OCV_REGH");
		return ret;
	}

	voltage_now = di->voltage_k*temp/1000 + di->voltage_b;

	return voltage_now;
}

static int rk_battery_voltage(struct battery_info *di)
{
	int ret;
	int voltage_now = 0;
	u8 buf;
	int temp;

	ret = battery_read(di->rk818, BAT_VOL_REGL, &buf, 1);
	temp = buf;
	ret = battery_read(di->rk818, BAT_VOL_REGH, &buf, 1);
	temp |= buf<<8;

	if (ret < 0) {
		dev_err(di->dev, "error read BAT_VOL_REGH");
		return ret;
	}

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

	ocv_table = di->platform_data->battery_ocv;
	ocv_size = di->platform_data->ocv_size;
	di->warnning_voltage = ocv_table[3];
	tmp = interpolate(voltage, ocv_table, ocv_size);
	di->temp_soc = ab_div_c(tmp, MAX_PERCENTAGE, INTERPOLATE_MAX);
	di->temp_nac = ab_div_c(tmp, di->fcc, INTERPOLATE_MAX);

	return 0;
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


static void ioffset_sample_time(struct battery_info *di, int time)
{
	u8 ggcon;

	battery_read(di->rk818, GGCON, &ggcon, 1);
	ggcon &= ~(0x30); /*clear <5:4>*/
	ggcon |= time;
	battery_write(di->rk818, GGCON, &ggcon, 1);
	debug_reg(di, GGCON, "GGCON");
}

static void update_cal_offset(struct battery_info *di)
{
	int mod = di->queue_work_cnt % TIME_10MIN_SEC;

	DBG("<%s>, queue_work_cnt = %lu, mod = %d\n", __func__, di->queue_work_cnt, mod);
	if ((!mod) && (di->pcb_ioffset_updated)) {
		_set_cal_offset(di, di->pcb_ioffset+_get_ioffset(di));
		DBG("<%s>. 10min update cal_offset = %d", __func__, di->pcb_ioffset+_get_ioffset(di));
	}
}


static void zero_current_calibration(struct battery_info *di)
{
	int adc_value;
	uint16_t C0;
	uint16_t C1;
	int ioffset;
	int pcb_offset;
	u8 retry = 0;

	if ((di->charge_status == CHARGE_FINISH) && (abs32_int(di->current_avg) > 4)) {

		for (retry = 0; retry < 5; retry++) {
			adc_value = _get_raw_adc_current(di);
			DBG("<%s>. adc_value = %d\n", __func__, adc_value);

			C0 = _get_cal_offset(di);
			C1 = adc_value + C0;
			_set_cal_offset(di, C1);
			DBG("<%s>. C1 = %d\n", __func__, C1);
			msleep(2000);

			adc_value = _get_raw_adc_current(di);
			DBG("<%s>. adc_value = %d\n", __func__, adc_value);
			if (adc_value < 4) {

				ioffset = _get_ioffset(di);
				pcb_offset = C1 - ioffset;
				di->pcb_ioffset = pcb_offset;
				di->pcb_ioffset_updated  = true;
				DBG("<%s>. update the cal_offset, pcb_offset = %d\n", __func__, pcb_offset);
				break;
			} else
				di->pcb_ioffset_updated  = false;
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
	DBG("<%s>. GGSTS = 0x%x, GGCON = 0x%x, relax_vol1 = %d, relax_vol2 = %d\n", __func__, status, ggcon, relax_vol1, relax_vol2);
	if (_is_relax_mode(di))
		return relax_vol1 > relax_vol2?relax_vol1:relax_vol2;
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

	ret = battery_read(di->rk818, BAT_CUR_AVG_REGL, &buf, 1);
	if (ret < 0) {
		dev_err(di->dev, "error read BAT_CUR_AVG_REGL");
		return ret;
	}
	current_now = buf;
	ret = battery_read(di->rk818, BAT_CUR_AVG_REGH, &buf, 1);
	if (ret < 0) {
		dev_err(di->dev, "error read BAT_CUR_AVG_REGH");
		return ret;
	}
	current_now |= (buf<<8);

	if (current_now & 0x800)
		current_now -= 4096;

	temp = current_now*1506/1000;/*1000*90/14/4096*500/521;*/

	return temp;

}

static bool is_bat_exist(struct  battery_info *di)
{
	u8 buf;

	battery_read(di->rk818, SUP_STS_REG, &buf, 1);
	return (buf & 0x80) ? true : false;
}

static bool _is_first_poweron(struct  battery_info *di)
{
	u8 buf;
	u8 temp;

	battery_read(di->rk818, GGSTS, &buf, 1);
	DBG("%s GGSTS value is 0x%2x \n", __func__, buf);
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


	for (i = 0; i <= 20; i++) {
		if (temp_table[i] < temp_table[i+1])
			j = i+1;
	}

	i = temp_table[j];
	di->exit_flatzone = ocv_table[i];

	DBG("enter_flatzone = %d exit_flatzone = %d\n", di->enter_flatzone, di->exit_flatzone);

}

#if 0
static int is_not_flatzone(struct   battery_info *di, int voltage)
{
	if ((voltage >= di->enter_flatzone) && (voltage <= di->exit_flatzone)) {
		DBG("<%s>. is in flat zone\n", __func__);
		return 0;
	} else {
		DBG("<%s>. is not in flat zone\n", __func__);
		return 1;
	}
}
#endif
static void power_on_save(struct   battery_info *di, int voltage)
{
	u8 buf;
	u8 save_soc;

	battery_read(di->rk818, NON_ACT_TIMER_CNT_REG, &buf, 1);

	if (_is_first_poweron(di) || buf > 30) { /* first power-on or power off time > 30min */
		_voltage_to_capacity(di, voltage);
		if (di->temp_soc < 20) {
			di->dod0_voltage = voltage;
			di->dod0_capacity = di->nac;
			di->dod0_status = 1;
			di->dod0 = di->temp_soc;/* _voltage_to_capacity(di, voltage); */
			di->dod0_level = 80;

			if (di->temp_soc <= 0)
				di->dod0_level = 100;
			else if (di->temp_soc < 5)
				di->dod0_level = 95;
			else if (di->temp_soc < 10)
				di->dod0_level = 90;
			/* save_soc = di->dod0_level; */
			save_soc = get_level(di);
			if (save_soc <  di->dod0_level)
				save_soc = di->dod0_level;
			save_level(di, save_soc);
			DBG("<%s>UPDATE-FCC POWER ON : dod0_voltage = %d, dod0_capacity = %d ", __func__, di->dod0_voltage, di->dod0_capacity);
		}
	}

}


static int _get_soc(struct   battery_info *di)
{
	return di->remain_capacity * 100 / di->fcc;
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

static int rk_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	u8 buf;
	struct battery_info *di = to_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = di->current_avg*1000;/*uA*/
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = di->voltage*1000;/*uV*/
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		/*val->intval = val->intval <= 0 ? 0 : 1;*/
		battery_read(di->rk818, SUP_STS_REG, &buf, 1);
		val->intval = (buf >> 7); /*bit7:BAT_EX*/
		break;


	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = di->real_soc;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = di->status;
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

static int rk_battery_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int ret = 0;
	struct battery_info *di = to_ac_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = di->ac_online;	/*discharging*/
		break;

	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

#define to_usb_device_info(x) container_of((x), \
				struct battery_info, usb)

static int rk_battery_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int ret = 0;
	struct battery_info *di = to_usb_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if ((strstr(saved_command_line, "charger") == NULL) && (di->real_soc == 0) && (di->work_on == 1))
			val->intval = 0;
		else
			val->intval = di->usb_online;
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
	di->bat.get_property = rk_battery_get_property;

	di->ac.name = "AC";
	di->ac.type = POWER_SUPPLY_TYPE_MAINS;
	di->ac.properties = rk_battery_ac_props;
	di->ac.num_properties = ARRAY_SIZE(rk_battery_ac_props);
	di->ac.get_property = rk_battery_ac_get_property;

	di->usb.name = "USB";
	di->usb.type = POWER_SUPPLY_TYPE_USB;
	di->usb.properties = rk_battery_usb_props;
	di->usb.num_properties = ARRAY_SIZE(rk_battery_usb_props);
	di->usb.get_property = rk_battery_usb_get_property;
}

static int battery_power_supply_register(struct battery_info *di, struct device *dev)
{
	int ret;

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

	di->update_k = 0;
	di->update_q = 0;
	di->voltage_old = 0;
	di->display_soc = 0;

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
	int temp = 0;
	u8 buf;
	u32 capacity;

	ret = battery_read(di->rk818, REMAIN_CAP_REG3, &buf, 1);
	temp = buf << 24;
	ret = battery_read(di->rk818, REMAIN_CAP_REG2, &buf, 1);
	temp |= buf << 16;
	ret = battery_read(di->rk818, REMAIN_CAP_REG1, &buf, 1);
	temp |= buf << 8;
	ret = battery_read(di->rk818, REMAIN_CAP_REG0, &buf, 1);
	temp |= buf;

	capacity = temp;/* /4096*900/14/36*500/521; */

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

	ret = battery_read(di->rk818, GASCNT3, &buf, 1);
	temp = buf << 24;
	ret = battery_read(di->rk818, GASCNT2, &buf, 1);
	temp |= buf << 16;
	ret = battery_read(di->rk818, GASCNT1, &buf, 1);
	temp |= buf << 8;
	ret = battery_read(di->rk818, GASCNT0, &buf, 1);
	temp |= buf;

	capacity = temp/2390;/* 4096*900/14/36*500/521; */

	return capacity;
}

static void relax_volt_update_remain_capacity(struct battery_info *di, uint16_t relax_voltage, int sleep_min)
{
	int remain_capacity;
	int relax_capacity;
	int now_temp_soc;
	int relax_soc;
	int abs_soc;
	int min, soc_time;
	int now_current;

	now_temp_soc = _get_soc(di);
	_voltage_to_capacity(di, relax_voltage);
	relax_soc = di->temp_soc;
	relax_capacity = di->temp_nac;
	abs_soc = abs32_int(relax_soc - now_temp_soc);

	DBG("<%s>. suspend_temp_soc=%d, temp_soc=%d, ,real_soc = %d\n", __func__, di->suspend_temp_soc, now_temp_soc, di->real_soc);
	DBG("<%s>. relax_soc = %d, abs_soc = %d\n", __func__, relax_soc, abs_soc);

	/*handle temp_soc*/
	if (abs32_int(di->real_soc - relax_soc) <= 5) {
		remain_capacity = relax_capacity;
		DBG("<%s>. real-soc is close to relax-soc, set:  temp_soc = relax_soc\n", __func__);
	} else {
		if (abs_soc == 0)
			remain_capacity = _get_realtime_capacity(di);
		else if (abs_soc <= 10)
			remain_capacity = relax_capacity;
		else if (abs_soc <= 20)
			remain_capacity = relax_capacity*70/100+di->remain_capacity*30/100;
		else
			remain_capacity = relax_capacity*50/100+di->remain_capacity*50/100;
	}
	_capacity_init(di, remain_capacity);
	di->temp_soc = _get_soc(di);
	di->remain_capacity  = _get_realtime_capacity(di);

	/*handle real_soc*/
	DBG("<%s>. real_soc = %d, adjust delta = %d\n", __func__, di->real_soc, di->suspend_temp_soc - relax_soc);
	if (relax_soc < now_temp_soc) {
		if (di->suspend_temp_soc - relax_soc <= 5)
			di->real_soc = di->real_soc - (di->suspend_temp_soc - relax_soc);
		else if (di->suspend_temp_soc - relax_soc <= 10)
			di->real_soc = di->real_soc - 5;
		else
			di->real_soc = di->real_soc - (di->suspend_temp_soc - relax_soc)/2;
	} else {
		now_current = _get_average_current(di);
		soc_time = di->fcc*3600/100/(abs_int(now_current));/*1% time cost*/
		min = soc_time / 60;
		if (sleep_min > min)
			di->real_soc--;
	}

	DBG("<%s>. new_temp_soc=%d, new_real_soc=%d, new_remain_cap=%d\n", __func__, _get_soc(di), di->real_soc, di->remain_capacity);
}


static int _copy_soc(struct  battery_info *di, u8 save_soc)
{
	u8 soc;

	soc = save_soc;
	battery_write(di->rk818, SOC_REG, &soc, 1);
	return 0;
}

static bool support_uboot_charge(void)
{
	return support_uboot_chrg?true:false;
}

static int _rsoc_init(struct  battery_info *di)
{
	u8 pwron_soc;
	u8 init_soc;
	u32 remain_capacity;
	u8 last_shtd_time;
	u8 curr_shtd_time;
#ifdef SUPPORT_USB_CHARGE
	int otg_status;
#else
	u8 buf;
#endif
	di->voltage  = rk_battery_voltage(di);
	di->voltage_ocv = _get_OCV_voltage(di);
	DBG("OCV voltage = %d\n" , di->voltage_ocv);

	if (_is_first_poweron(di)) {
		_save_FCC_capacity(di, di->design_capacity);
		di->fcc = _get_FCC_capacity(di);

		_voltage_to_capacity(di, di->voltage_ocv);
		di->real_soc = di->temp_soc;
		di->nac      = di->temp_nac;
		DBG("<%s>.this is first poweron: OCV-SOC = %d, OCV-CAPACITY = %d, FCC = %d\n", __func__, di->real_soc, di->nac, di->fcc);

	} else {
		battery_read(di->rk818, SOC_REG, &pwron_soc, 1);
		init_soc = pwron_soc;
		DBG("<%s>this is NOT first poweron.SOC_REG = %d\n", __func__, pwron_soc);

#ifdef SUPPORT_USB_CHARGE
		otg_status = dwc_otg_check_dpdm();
		if ((pwron_soc == 0) && (otg_status == 1)) { /*usb charging*/
			init_soc = 1;
			battery_write(di->rk818, SOC_REG, &init_soc, 1);
		}
#else
		battery_read(di->rk818, VB_MOD_REG, &buf, 1);
		if ((pwron_soc == 0) && ((buf&PLUG_IN_STS) != 0)) {
			init_soc = 1;
			battery_write(di->rk818, SOC_REG, &init_soc, 1);
		}
#endif
		remain_capacity = _get_remain_capacity(di);

		battery_read(di->rk818, NON_ACT_TIMER_CNT_REG, &curr_shtd_time, 1);
		battery_read(di->rk818, NON_ACT_TIMER_CNT_REG_SAVE, &last_shtd_time, 1);
		battery_write(di->rk818, NON_ACT_TIMER_CNT_REG_SAVE, &curr_shtd_time, 1);
		DBG("<%s>, now_shtd_time = %d, last_shtd_time = %d, otg_status = %d\n", __func__, curr_shtd_time, last_shtd_time, otg_status);

		if (!support_uboot_charge()) {
			_voltage_to_capacity(di, di->voltage_ocv);
			DBG("<%s>Not first pwron, real_remain_cap = %d, ocv-remain_cp=%d\n", __func__, remain_capacity, di->temp_nac);

			/* if plugin, make sure current shtd_time different from last_shtd_time.*/
			if (((otg_status != 0) && (curr_shtd_time > 0) && (last_shtd_time != curr_shtd_time)) || ((curr_shtd_time > 0) && (otg_status == 0))) {

				if (curr_shtd_time > 30) {
					remain_capacity = di->temp_nac;
					DBG("<%s>shutdown_time > 30 minute,  remain_cap = %d\n", __func__, remain_capacity);

				} else if ((curr_shtd_time > 5) && (abs32_int(di->temp_soc - di->real_soc) >= 10)) {
					if (remain_capacity >= di->temp_nac*120/100)
						remain_capacity = di->temp_nac*110/100;
					else if (remain_capacity < di->temp_nac*8/10)
						remain_capacity = di->temp_nac*9/10;

					DBG("<%s> shutdown_time > 3 minute,  remain_cap = %d\n", __func__, remain_capacity);
				}
			}
		}

		di->real_soc = init_soc;
		di->nac = remain_capacity;
		if (di->nac <= 0)
			di->nac = 0;
		DBG("<%s> init_soc = %d, init_capacity=%d\n", __func__, di->real_soc, di->nac);
	}
	return 0;
}


static u8 get_charge_status(struct battery_info *di)
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

	case  TRICKLE_CHARGE:				/* (0x02 << 4) */
		ret = DEAD_CHARGE;
		DBG("  TRICKLE CHARGE ...\n ");
		break;

	case  CC_OR_CV:					/* (0x03 << 4) */
		ret = CC_OR_CV;
		DBG("  CC or CV ...\n");
		break;

	case  CHARGE_FINISH:				/* (0x04 << 4) */
		ret = CHARGE_FINISH;
		DBG("  CHARGE FINISH ...\n");
		break;

	case  USB_OVER_VOL:					/* (0x05 << 4) */
		ret = USB_OVER_VOL;
		DBG("  USB OVER VOL ...\n");
		break;

	case  BAT_TMP_ERR:					/* (0x06 << 4) */
		ret = BAT_TMP_ERR;
		DBG("  BAT TMP ERROR ...\n");
		break;

	case  TIMER_ERR:					/* (0x07 << 4) */
		ret = TIMER_ERR;
		DBG("  TIMER ERROR ...\n");
		break;

	case  USB_EXIST:					/* (1 << 1)// usb is exists */
		ret = USB_EXIST;
		DBG("  USB EXIST ...\n");
		break;

	case  USB_EFF:						/* (1 << 0)// usb is effective */
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

static void rk_battery_charger_init(struct  battery_info *di)
{
	u8 chrg_ctrl_reg1, usb_ctrl_reg, chrg_ctrl_reg2, chrg_ctrl_reg3;
	u8 sup_sts_reg;

	DBG("%s  start\n", __func__);
	battery_read(di->rk818, USB_CTRL_REG, &usb_ctrl_reg, 1);
	battery_read(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
	battery_read(di->rk818, CHRG_CTRL_REG2, &chrg_ctrl_reg2, 1);
	battery_read(di->rk818, SUP_STS_REG, &sup_sts_reg, 1);
	battery_read(di->rk818, CHRG_CTRL_REG3, &chrg_ctrl_reg3, 1);

	DBG("old usb_ctrl_reg = 0x%2x, CHRG_CTRL_REG1 = 0x%2x\n ", usb_ctrl_reg, chrg_ctrl_reg1);
	usb_ctrl_reg &= (~0x0f);
#ifdef SUPPORT_USB_CHARGE
	usb_ctrl_reg |= (ILIM_450MA | CHRG_CT_EN);
#else
	usb_ctrl_reg |= (ILIM_3000MA | CHRG_CT_EN);
#endif
	chrg_ctrl_reg1 &= (0x00);
	chrg_ctrl_reg1 |= (CHRG_EN) | (CHRG_VOL4200 | CHRG_CUR1400mA);

	chrg_ctrl_reg3 |= CHRG_TERM_DIG_SIGNAL;/* digital finish mode*/
	chrg_ctrl_reg2 &= ~(0xc0);
	chrg_ctrl_reg2 |= FINISH_100MA;

	sup_sts_reg &= ~(0x01 << 3);
	sup_sts_reg |= (0x01 << 2);

	battery_write(di->rk818, CHRG_CTRL_REG3, &chrg_ctrl_reg3, 1);
	battery_write(di->rk818, USB_CTRL_REG, &usb_ctrl_reg, 1);
	battery_write(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
	battery_write(di->rk818, CHRG_CTRL_REG2, &chrg_ctrl_reg2, 1);
	battery_write(di->rk818, SUP_STS_REG, &sup_sts_reg, 1);

	debug_reg(di, CHRG_CTRL_REG1, "CHRG_CTRL_REG1");
	debug_reg(di, SUP_STS_REG, "SUP_STS_REG");
	debug_reg(di, USB_CTRL_REG, "USB_CTRL_REG");
	debug_reg(di, CHRG_CTRL_REG1, "CHRG_CTRL_REG1");

	DBG("%s  end\n", __func__);
}

void charge_disable_open_otg(int value)
{
	struct  battery_info *di = g_battery;

	if (value == 1) {
		DBG("charge disable, enable OTG.\n");
		rk818_set_bits(di->rk818, CHRG_CTRL_REG1, 1 << 7, 0 << 7);
		rk818_set_bits(di->rk818, 0x23, 1 << 7, 1 << 7); /*  enable OTG */
	}
	if (value == 0) {
		DBG("charge enable, disable OTG.\n");
		rk818_set_bits(di->rk818, 0x23, 1 << 7, 0 << 7); /* disable OTG */
		rk818_set_bits(di->rk818, CHRG_CTRL_REG1, 1 << 7, 1 << 7);
	}
}

static void low_waring_init(struct battery_info *di)
{
	u8 vb_mon_reg;
	u8 vb_mon_reg_init;

	battery_read(di->rk818, VB_MOD_REG, &vb_mon_reg, 1);

	/* 2.8v~3.5v, interrupt */
	vb_mon_reg_init = (((vb_mon_reg | (1 << 4)) & (~0x07)) | 0x06);  /* 3400mV*/
	battery_write(di->rk818, VB_MOD_REG, &vb_mon_reg_init, 1);
}

static void  fg_init(struct battery_info *di)
{
	u8 adc_ctrl_val;

	adc_ctrl_val = 0x30;
	battery_write(di->rk818, ADC_CTRL_REG, &adc_ctrl_val, 1);

	_gauge_enable(di);
	/* get the volatege offset */
	_get_voltage_offset_value(di);
	rk_battery_charger_init(di);
	_set_relax_thres(di);
	/* get the current offset , the value write to the CAL_OFFSET */
	di->current_offset = _get_ioffset(di);
	_set_cal_offset(di, di->current_offset+42);
	_rsoc_init(di);
	_capacity_init(di, di->nac);

	di->remain_capacity = _get_realtime_capacity(di);
	di->current_avg = _get_average_current(di);

	low_waring_init(di);
	restart_relax(di);
	power_on_save(di, di->voltage_ocv);
	/* set sample time for cal_offset interval*/
	ioffset_sample_time(di, SAMP_TIME_8MIN);
	dump_gauge_register(di);
	dump_charger_register(di);

	DBG("<%s> :\n"
	    "nac = %d , remain_capacity = %d\n"
	    "OCV_voltage = %d, voltage = %d\n"
	    "SOC = %d, fcc = %d\n",
	    __func__,
	    di->nac, di->remain_capacity,
	    di->voltage_ocv, di->voltage,
	    di->real_soc, di->fcc);
}


/* int R_soc, D_soc, r_soc, zq, k, Q_err, Q_ocv; */
static void  zero_get_soc(struct   battery_info *di)
{
	int ocv_voltage, check_voltage;
	int temp_soc = -1, real_soc;
	int currentold, currentnow, voltage;
	int i;
	int voltage_k;
	int count_num = 0;

	DBG("\n\n+++++++zero mode++++++display soc+++++++++++\n");
	/* if (di->voltage <  3600)//di->warnning_voltage) */
	{
		/* DBG("+++++++zero mode++++++++displaysoc+++++++++\n"); */
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
			voltage += rk_battery_voltage(di);
		voltage /= 10;

		if (di->voltage_old == 0)
			di->voltage_old = voltage;
		voltage_k = voltage;
		voltage = (di->voltage_old*2 + 8*voltage)/10;
		di->voltage_old = voltage;
		/* DBG("Zero: voltage = %d\n", voltage); */

		currentnow = _get_average_current(di);
		/* DBG(" zero: current = %d, voltage = %d\n", currentnow, voltage); */

		ocv_voltage = 3400 + abs32_int(currentnow)*200/1000;
		check_voltage = voltage + abs32_int(currentnow)*(200 - 65)/1000;   /*  65 mo  power-path mos */
		_voltage_to_capacity(di, check_voltage);
		/* if ((di->remain_capacity > di->nac) && (update_q == 0)) */
		/* DBG(" xxx  Zerro: tui suan OCV cap :%d\n", di->temp_nac); */
		di->update_q = di->remain_capacity - di->temp_nac;
		/* update_q = di->temp_nac; */

		/* DBG("Zero: update_q = %d , remain_capacity = %d, temp_nac = %d\n ", di->update_q, di->remain_capacity, di->temp_nac); */
		/* relax_volt_update_remain_capacity(di, 3600 + abs32_int(di->current_avg)*200/1000); */

		_voltage_to_capacity(di, ocv_voltage);
		/*di->temp_nac;
		temp_soc = _get_soc(di); */
		if (di->display_soc == 0)
			di->display_soc = di->real_soc*1000;

		real_soc = di->display_soc;
		/* DBG(" Zerro: Q (err)   cap :%d\n", di->temp_nac);
		DBG(" ZERO : real-soc = %d\n ", di->real_soc); */
		DBG("ZERO : ocv_voltage = %d, check_voltage = %d\n ", ocv_voltage, check_voltage);
		if (di->remain_capacity > di->temp_nac + di->update_q) {

			if (di->update_k == 0 || di->update_k >= 10) {
				/* DBG("one..\n"); */
				if (di->update_k == 0) {
					di->line_q = di->temp_nac + di->update_q;  /* ZQ = Q_ded +  Qerr */
					/* line_q = update_q - di->temp_nac; */
					temp_soc = (di->remain_capacity - di->line_q)*1000/di->fcc;/* (RM - ZQ) / FCC  = r0 = R0 ; */
					/* temp_soc = (line_q)*1000/di->fcc;//(RM - ZQ) / FCC  = r0 = R0 ;*
					/di->line_k = (real_soc*1000 + temp_soc/2)/temp_soc;//k0 = y0/x0 */
					di->line_k = (real_soc + temp_soc/2)/temp_soc;/* k0 = y0/x0 */
					/* DBG("Zero: one  link = %d realsoc = %d , temp_soc = %d\n", di->line_k, di->real_soc, temp_soc); */


				} else {
					/*
					if (line_q == 0)
					line_q = di->temp_nac + update_q;
					*/
					/* DBG("two...\n"); */
					temp_soc = ((di->remain_capacity - di->line_q)*1000 + di->fcc/2)/di->fcc; /* x1  10 */
					/*
					temp_soc = (line_q)*1000/di->fcc;// x1
					real_soc = (di->line_k*temp_soc+500)/1000;  //y1 = k0*x1
					*/
					real_soc = (di->line_k*temp_soc); /*  y1 = k0*x1 */
					/* DBG("Zero: two  link = %d realsoc = %d , temp_soc = %d\n", di->line_k, real_soc, temp_soc); */
					di->display_soc = real_soc;
					/* if (real_soc != di->real_soc) */
					if ((real_soc+500)/1000 < di->real_soc)
						di->real_soc--;
					/*
					DBG("Zero two di->real_soc = %d\n", di->real_soc);
					DBG("Zero : temp_soc : %d\n", real_soc);
					*/
					_voltage_to_capacity(di, ocv_voltage);
					di->line_q = di->temp_nac + di->update_q; /* Q1 */
					/* line_q = update_q - di->temp_nac; */
					temp_soc = ((di->remain_capacity - di->line_q)*1000 +  di->fcc/2)/di->fcc; /* z1 */
					/*
					temp_soc = (line_q)*1000/di->fcc;
					di->line_k = (di->real_soc*1000 +  temp_soc/2)/temp_soc; //k1 = y1/z1
					*/
					di->line_k = (di->display_soc +  temp_soc/2)/temp_soc; /* k1 = y1/z1 */
					/* DBG("Zero: two  link = %d display_soc = %d , temp_soc = %d\n", di->line_k, di->display_soc, temp_soc); */
					/* line_q = di->temp_nac + update_q;// Q1 */


				}
				di->update_k = 0;

			}

			/* DBG("di->remain_capacity = %d, line_q = %d\n ", di->remain_capacity, di->line_q); */

			di->update_k++;
			if (di->update_k == 1 || di->update_k != 10) {
				temp_soc = ((di->remain_capacity - di->line_q)*1000 + di->fcc/2)/di->fcc;/* x */
				di->display_soc = di->line_k*temp_soc;
				/* if (((di->line_k*temp_soc+500)/1000) != di->real_soc), */
				DBG("ZERO : display-soc = %d, real-soc = %d\n", di->display_soc, di->real_soc);
				if ((di->display_soc+500)/1000 < di->real_soc)
					di->real_soc--;
				/* di->real_soc = (line_k*temp_soc+500)/1000 ;//y = k0*x */
			}
		} else {
			/* DBG("three..\n"); */
			di->update_k++;
			if (di->update_k > 10) {
				di->update_k = 0;
				di->real_soc--;
			}
		}

		DBG("ZERO : update_k = %d\n", di->update_k);
		DBG("ZERO : remain_capacity = %d , nac = %d, update_q = %d\n", di->remain_capacity, di->line_q, di->update_q);
		DBG("ZERO : Warnning_voltage = %d, line_k = %d, temp_soc = %d real_soc = %d\n\n", di->warnning_voltage, di->line_k, temp_soc, di->real_soc);
	}
}


static void voltage_to_soc_discharge_smooth(struct battery_info *di)
{
	int voltage;
	int now_current, soc_time = -1;
	int volt_to_soc;

	voltage = di->voltage;
	now_current = di->current_avg;
	if (now_current == 0)
		now_current = 1;
	soc_time = di->fcc*3600/100/(abs_int(now_current));
	_voltage_to_capacity(di, 3800);
	volt_to_soc = di->temp_soc;
	di->temp_soc = _get_soc(di);

	DBG("<%s>. 3.8v ocv_to_soc = %d\n", __func__, volt_to_soc);
	DBG("<%s>. di->temp_soc = %d, di->real_soc = %d\n", __func__, di->temp_soc, di->real_soc);
	if ((di->voltage < 3800) || (di->voltage > 3800 && di->real_soc < volt_to_soc)) {  /* di->warnning_voltage) */
		zero_get_soc(di);
		return;

	} else if (di->temp_soc == di->real_soc) {
		DBG("<%s>. di->temp_soc == di->real_soc\n", __func__);
	} else if (di->temp_soc > di->real_soc) {
		DBG("<%s>. di->temp_soc > di->real_soc\n", __func__);
		di->vol_smooth_time++;
		if (di->vol_smooth_time > soc_time*3) {
			di->real_soc--;
			di->vol_smooth_time = 0;
		}

	} else {
		DBG("<%s>. di->temp_soc < di->real_soc\n", __func__);
		if (di->real_soc == (di->temp_soc + 1)) {
			di->change_timer = di->soc_timer;
			di->real_soc = di->temp_soc;
		} else {
			di->vol_smooth_time++;
			if (di->vol_smooth_time > soc_time/3) {
				di->real_soc--;
				di->vol_smooth_time  = 0;
			}
		}
	}

	DBG("<%s>, di->temp_soc = %d, di->real_soc = %d\n", __func__, di->temp_soc, di->real_soc);
	DBG("<%s>, di->vol_smooth_time = %d, soc_time = %d\n", __func__, di->vol_smooth_time, soc_time);
}

static int get_charging_time(struct battery_info *di)
{
	return (di->charging_time/60);
}

static int get_discharging_time(struct battery_info *di)
{
	return (di->discharging_time/60);
}
static void dump_debug_info(struct battery_info *di)
{
	u8 sup_tst_reg, ggcon_reg, ggsts_reg, vb_mod_reg;
	u8 usb_ctrl_reg, chrg_ctrl_reg1;
	u8 chrg_ctrl_reg2, chrg_ctrl_reg3, rtc_val;

	battery_read(di->rk818, GGCON, &ggcon_reg, 1);
	battery_read(di->rk818, GGSTS, &ggsts_reg, 1);
	battery_read(di->rk818, SUP_STS_REG, &sup_tst_reg, 1);
	battery_read(di->rk818, VB_MOD_REG, &vb_mod_reg, 1);
	battery_read(di->rk818, USB_CTRL_REG, &usb_ctrl_reg, 1);
	battery_read(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
	battery_read(di->rk818, CHRG_CTRL_REG2, &chrg_ctrl_reg2, 1);
	battery_read(di->rk818, CHRG_CTRL_REG3, &chrg_ctrl_reg3, 1);
	battery_read(di->rk818, 0x00, &rtc_val, 1);

	DBG("\n------------- dump_debug_regs -----------------\n"
	    "GGCON = 0x%2x, GGSTS = 0x%2x, RTC	= 0x%2x\n"
	    "SUP_STS_REG  = 0x%2x, VB_MOD_REG	= 0x%2x\n"
	    "USB_CTRL_REG  = 0x%2x, CHRG_CTRL_REG1 = 0x%2x\n"
	    "CHRG_CTRL_REG2 = 0x%2x, CHRG_CTRL_REG3 = 0x%2x\n\n",
	    ggcon_reg, ggsts_reg, rtc_val,
	    sup_tst_reg, vb_mod_reg,
	    usb_ctrl_reg, chrg_ctrl_reg1,
	    chrg_ctrl_reg2, chrg_ctrl_reg3
	   );

	DBG(
	    "########################## [read] ################################\n"
	    "info: 3.4v low warning, digital 100mA finish,  4.2v, 1.6A\n"
	    "-----------------------------------------------------------------\n"
	    "realx-voltage = %d, voltage = %d, current-avg = %d\n"
	    "fcc = %d, remain_capacity = %d, ocv_volt = %d\n"
	    "diplay_soc = %d, cpapacity_soc = %d\n"
	    "AC-ONLINE = %d, USB-ONLINE = %d, charging_status = %d\n"
	    "finish_real_soc = %d, finish_temp_soc = %d\n"
	    "chrg_time = %d, dischrg_time = %d\n",
	    get_relax_voltage(di),
	    di->voltage, di->current_avg,
	    di->fcc, di->remain_capacity, _get_OCV_voltage(di),
	    di->real_soc, _get_soc(di),
	    di->ac_online, di->usb_online, di->status,
	    di->debug_finish_real_soc, di->debug_finish_temp_soc,
	    get_charging_time(di), get_discharging_time(di)
	   );
	get_charge_status(di);
	DBG("################################################################\n");

}

static void update_fcc_capacity(struct battery_info *di)
{
	if ((di->charge_status == CHARGE_FINISH) && (di->dod0_status == 1)) {
		if (get_level(di) >= di->dod0_level) {
			di->fcc = (di->remain_capacity - di->dod0_capacity)*100/(100-di->dod0);
			if (di->fcc > di->qmax)
				di->fcc = di->qmax;

			_capacity_init(di, di->fcc);
			_save_FCC_capacity(di, di->fcc);
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
	if (di->charge_status == CHARGE_FINISH)
		update_fcc_capacity(di);/* save new fcc*/

	/* debug msg*/
	debug_get_finish_soc(di);
}

static void charge_finish_routine(struct battery_info *di)
{
	if (di->charge_status == CHARGE_FINISH) {
		_capacity_init(di, di->fcc);
		zero_current_calibration(di);

		if (di->real_soc < 100) {
			DBG("<%s>,CHARGE_FINISH  di->real_soc < 100, real_soc=%d\n", __func__, di->real_soc);
			if ((di->soc_counter < 80)) {
				di->soc_counter++;
			} else {
				di->soc_counter = 0;
				di->real_soc++;
			}
		}
	}
}

static void voltage_to_soc_charge_smooth(struct battery_info *di)
{
	int now_current, soc_time;

	now_current = _get_average_current(di);
	if (now_current == 0)
		now_current = 1;
	soc_time = di->fcc*3600/100/(abs_int(now_current));   /* 1%  time; */
	di->temp_soc = _get_soc(di);

	DBG("<%s>. di->temp_soc = %d, di->real_soc = %d\n", __func__, di->temp_soc, di->real_soc);
	/*
	if ((di->temp_soc >= 85)&&(di->real_soc >= 85)){
		di->charge_smooth_time++;

		if  (di->charge_smooth_time > soc_time/3) {
			di->real_soc++;
			di->charge_smooth_time  = 0;
		}
		di->charge_smooth_status = true;
	}*/

	if (di->real_soc == di->temp_soc) {
		DBG("<%s>. di->temp_soc == di->real_soc\n", __func__);
		di->temp_soc = _get_soc(di);
	}
	if ((di->temp_soc != di->real_soc) && (now_current != 0)) {

		if (di->temp_soc < di->real_soc + 1) {
			DBG("<%s>. di->temp_soc < di->real_soc\n", __func__);
			di->charge_smooth_time++;
			if  (di->charge_smooth_time > soc_time*2) {
				di->real_soc++;
				di->charge_smooth_time  = 0;
			}
			di->charge_smooth_status = true;
		}

		else if (di->temp_soc > di->real_soc + 1) {
			DBG("<%s>. di->temp_soc > di->real_soc\n", __func__);
			di->charge_smooth_time++;
			if  (di->charge_smooth_time > soc_time/3) {
				di->real_soc++;
				di->charge_smooth_time  = 0;
			}
			di->charge_smooth_status = true;

		} else if (di->temp_soc == di->real_soc + 1) {
			DBG("<%s>. di->temp_soc == di->real_soc + 1\n", __func__);
			if (di->charge_smooth_status) {
				di->charge_smooth_time++;
				if (di->charge_smooth_time > soc_time/3) {
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

	DBG("<%s>, di->temp_soc = %d, di->real_soc = %d\n", __func__, di->temp_soc, di->real_soc);
	DBG("<%s>, di->vol_smooth_time = %d, soc_time = %d\n", __func__, di->charge_smooth_time, soc_time);
}

static void rk_battery_display_smooth(struct battery_info *di)
{
	int status;
	u8  charge_status;

	status = di->status;
	charge_status = di->charge_status;
	if ((status == POWER_SUPPLY_STATUS_CHARGING) || (status == POWER_SUPPLY_STATUS_FULL)) {

		if ((di->current_avg < -10) && (charge_status != CHARGE_FINISH))
			voltage_to_soc_discharge_smooth(di);
		else
			voltage_to_soc_charge_smooth(di);

	} else if (status == POWER_SUPPLY_STATUS_DISCHARGING) {
		voltage_to_soc_discharge_smooth(di);
		if (di->real_soc == 1) {
			di->time2empty++;
			if (di->time2empty >= 300)
				di->real_soc = 0;
		} else {
			di->time2empty = 0;
		}
	}

}

#if 0
static void software_recharge(struct battery_info *di, int max_cnt)
{
	static int recharge_cnt;
	u8 chrg_ctrl_reg1;

	if ((CHARGE_FINISH == get_charge_status(di)) && (rk_battery_voltage(di) < 4100) && (recharge_cnt < max_cnt)) {
		battery_read(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
		chrg_ctrl_reg1 &= ~(1 << 7);
		battery_write(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
		battery_read(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
		DBG("recharge, clear bit7, CHRG_CTRL_REG1 = 0x%x\n", chrg_ctrl_reg1);
		msleep(400);
		chrg_ctrl_reg1 |= (1 << 7);
		battery_write(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
		battery_read(di->rk818, CHRG_CTRL_REG1, &chrg_ctrl_reg1, 1);
		DBG("recharge, set bit7, CHRG_CTRL_REG1 = 0x%x\n", chrg_ctrl_reg1);

		recharge_cnt++;
	}
}
#endif

#if 0
static int estimate_battery_resister(struct battery_info *di)
{
	int i;
	int avr_voltage1 = 0, avr_current1;
	int avr_voltage2 = 0, avr_current2;
	u8 usb_ctrl_reg;
	int bat_res, ocv_votage;
	static unsigned long last_time;
	unsigned long delta_time;
	int charge_ocv_voltage1, charge_ocv_voltage2;
	int charge_ocv_soc1, charge_ocv_soc2;

	delta_time = get_seconds() - last_time;
	DBG("<%s>--- delta_time = %lu\n", __func__, delta_time);
	if (delta_time >= 20) {/*20s*/

		/*first sample*/
		set_charge_current(di, ILIM_450MA);/*450mA*/
		msleep(1000);
		for (i = 0; i < 10 ; i++) {
			msleep(100);
			avr_voltage1 += rk_battery_voltage(di);
		}
		avr_voltage1 /= 10;
		avr_current1 = _get_average_current(di);
		battery_read(di->rk818, USB_CTRL_REG, &usb_ctrl_reg, 1);
		DBG("------------------------------------------------------------------------------------------\n");
		DBG("avr_voltage1 = %d, avr_current1 = %d, USB_CTRL_REG = 0x%x\n", avr_voltage1, avr_current1, usb_ctrl_reg);

		/*second sample*/
		set_charge_current(di, ILIM_3000MA);
		msleep(1000);
		for (i = 0; i < 10 ; i++) {
			msleep(100);
			avr_voltage2 += rk_battery_voltage(di);
		}
		avr_voltage2 /= 10;
		avr_current2 = _get_average_current(di);
		battery_read(di->rk818, USB_CTRL_REG, &usb_ctrl_reg, 1);
		DBG("avr_voltage2 = %d, avr_current2 = %d, USB_CTRL_REG = 0x%x\n", avr_voltage2, avr_current2, usb_ctrl_reg);

		/*calc resister and ocv_votage ocv*/
		bat_res = (avr_voltage1 - avr_voltage2)*1000/(avr_current1 - avr_current2);
		ocv_votage = avr_voltage1 - (bat_res * avr_current1) / 1000;
		DBG("bat_res = %d, OCV = %d\n", bat_res, ocv_votage);

		/*calc sample voltage ocv*/
		charge_ocv_voltage1 = avr_voltage1 - avr_current1*200/1000;
		charge_ocv_voltage2 = avr_voltage2 - avr_current2*200/1000;
		_voltage_to_capacity(di, charge_ocv_voltage1);
		charge_ocv_soc1 = di->temp_soc;
		_voltage_to_capacity(di, charge_ocv_voltage2);
		charge_ocv_soc2 = di->temp_soc;

		DBG("charge_ocv_voltage1 = %d, charge_ocv_soc1 = %d\n", charge_ocv_voltage1, charge_ocv_soc1);
		DBG("charge_ocv_voltage2 = %d, charge_ocv_soc2 = %d\n", charge_ocv_voltage2, charge_ocv_soc2);
		DBG("------------------------------------------------------------------------------------------\n");
		last_time = get_seconds();

		return bat_res;
	}

	return 0;
}
#endif

#if 0
static int update_battery_resister(struct battery_info *di)
{
	int tmp_res;

	if ((get_charging_time(di) > 5) && (!di->bat_res_updated)) {/*charge at least 8min*/

		if ((di->temp_soc >= 80) && (di->bat_res_update_cnt < 10)) {
			tmp_res = estimate_battery_resister(di);
			if (tmp_res != 0)
				di->bat_res_update_cnt++;
			di->bat_res += tmp_res;
			DBG("<%s>. tmp_bat_res = %d, bat_res_update_cnt = %d\n", __func__, tmp_res, di->bat_res_update_cnt);
			if (di->bat_res_update_cnt == 10) {
				di->bat_res_updated = true;
				di->bat_res /= 10;
			}
			DBG("<%s>. bat_res = %d, bat_res_update_cnt = %d\n", __func__, di->bat_res, di->bat_res_update_cnt);
		}
	}

	return tmp_res;
}
#endif

#if 0
static void charge_soc_check_routine(struct battery_info *di)
{
	int min;
	int ocv_voltage;
	int old_temp_soc;
	int ocv_temp_soc;
	int remain_capcity;

	if (di->status == POWER_SUPPLY_STATUS_CHARGING) {
		min = get_charging_time(di);
		update_battery_resister(di);
	if (0)
		if ((min >= 30) && (di->bat_res_updated)) {

			old_temp_soc = di->temp_soc;
			ocv_voltage = di->voltage + di->bat_res*abs(di->current_avg);
			_voltage_to_capacity(di, ocv_voltage);
			ocv_temp_soc = di->temp_soc;

			DBG("<%s>. charge_soc_updated_point0 = %d, charge_soc_updated_point1 = %d\n", __func__, di->charge_soc_updated_point0, di->charge_soc_updated_point1);
			DBG("<%s>. ocv_voltage = %d, ocv_soc = %d\n", __func__, ocv_voltage, ocv_temp_soc);
			DBG("<%s>. voltage = %d, temp_soc = %d\n", __func__, di->voltage, old_temp_soc);

			if (abs32_int(ocv_temp_soc - old_temp_soc) > 10)
				di->temp_soc = ocv_temp_soc;
			else
				di->temp_soc = old_temp_soc*50/100 + ocv_temp_soc*50/100;

			remain_capcity = di->temp_soc * di->fcc / 100;
			_capacity_init(di, remain_capcity);
			di->remain_capacity = _get_realtime_capacity(di);
			DBG("<%s>. old_temp_soc = %d, updated_temp_soc = %d\n", __func__, old_temp_soc, di->temp_soc);
		}
	}

}
#endif

#if 1
static void update_resume_status_relax_voltage(struct battery_info *di)
{
	unsigned long sleep_soc;
	unsigned long sum_sleep_soc;
	unsigned long sleep_sec;
	int relax_voltage;
	u8 charge_status;
	int delta_capacity;
	int delta_soc;
	int sum_sleep_avr_current;
	int sleep_min;

	if (di->resume) {
		update_battery_info(di);
		di->resume = false;
		di->sys_wakeup = true;

		DBG("<%s>, resume----------checkstart\n", __func__);
		sleep_sec = get_seconds() - di->suspend_time_start;
		sleep_min = sleep_sec  / 60;

		DBG("<%s>, resume, sleep_sec(s) = %lu, sleep_min = %d\n",
			__func__, sleep_sec, sleep_min);

		if (di->sleep_status == POWER_SUPPLY_STATUS_DISCHARGING) {
			DBG("<%s>, resume, POWER_SUPPLY_STATUS_DISCHARGING\n", __func__);

			delta_capacity =  di->suspend_capacity - di->remain_capacity;
			delta_soc = di->suspend_temp_soc - _get_soc(di);
			di->dischrg_sum_sleep_capacity += delta_capacity;
			di->dischrg_sum_sleep_sec += sleep_sec;

			sum_sleep_soc = di->dischrg_sum_sleep_capacity * 100 / di->fcc;
			sum_sleep_avr_current = di->dischrg_sum_sleep_capacity * 3600 / di->dischrg_sum_sleep_sec;

			DBG("<%s>, resume, suspend_capacity=%d, resume_capacity=%d, real_soc = %d\n",
				__func__, di->suspend_capacity, di->remain_capacity, di->real_soc);
			DBG("<%s>, resume, delta_soc=%d, delta_capacity=%d, sum_sleep_avr_current=%d mA\n",
				__func__, delta_soc, delta_capacity, sum_sleep_avr_current);
			DBG("<%s>, resume, sum_sleep_soc=%lu, dischrg_sum_sleep_capacity=%lu, dischrg_sum_sleep_sec=%lu\n",
				__func__, sum_sleep_soc, di->dischrg_sum_sleep_capacity, di->dischrg_sum_sleep_sec);
			DBG("<%s>, relax_voltage=%d, voltage = %d\n", __func__, di->relax_voltage, di->voltage);

			/*large suspend current*/
			if (sum_sleep_avr_current > 20) {
				sum_sleep_soc = di->dischrg_sum_sleep_capacity * 100 / di->fcc;
				di->real_soc -= sum_sleep_soc;
				DBG("<%s>. resume, sleep_avr_current is Over 20mA, sleep_soc = %lu, updated real_soc = %d\n",
					__func__, sum_sleep_soc, di->real_soc);

			/* small suspend current*/
			} else if ((sum_sleep_avr_current >= 0) && (sum_sleep_avr_current <= 20)) {

				relax_voltage = get_relax_voltage(di);
				di->voltage  = rk_battery_voltage(di);

				if ((sleep_min >= 30) && (relax_voltage > di->voltage)) { /* sleep_min >= 30, update by relax voltage*/
					DBG("<%s>, resume, sleep_min > 30 min\n", __func__);
					relax_volt_update_remain_capacity(di, relax_voltage, sleep_sec);

				} else {
					DBG("<%s>, resume, sleep_min < 30 min\n", __func__);
					if (sum_sleep_soc > 0)
						di->real_soc -= sum_sleep_soc;
				}
			}

			if ((sum_sleep_soc > 0) || (sleep_min >= 30)) { /*完成了一次relax校准*/
				di->dischrg_sum_sleep_capacity = 0;
				di->dischrg_sum_sleep_sec = 0;
			}
			DBG("<%s>--------- resume DISCHARGE end\n", __func__);
			DBG("<%s>. dischrg_sum_sleep_capacity = %lu, dischrg_sum_sleep_sec = %lu\n", __func__, di->dischrg_sum_sleep_capacity, di->dischrg_sum_sleep_sec);
		}

		else if (di->sleep_status == POWER_SUPPLY_STATUS_CHARGING) {
			DBG("<%s>, resume, POWER_SUPPLY_STATUS_CHARGING\n", __func__);
			if ((di->suspend_charge_current >= 0) || (get_charge_status(di) == CHARGE_FINISH)) {
				di->temp_soc = _get_soc(di);
				charge_status = get_charge_status(di);

				DBG("<%s>, resume, ac-online = %d, usb-online = %d, sleep_current=%d\n", __func__, di->ac_online, di->usb_online, di->suspend_charge_current);
				if (((di->suspend_charge_current < 800) && (di->ac_online == 1)) || (charge_status == CHARGE_FINISH)) {
					DBG("resume, sleep : ac online charge current < 1000\n");
					if (sleep_sec > 0) {
						di->count_sleep_time += sleep_sec;
						sleep_soc = 1000*di->count_sleep_time*100/3600/di->fcc;
						DBG("<%s>, resume, sleep_soc=%lu, real_soc=%d\n", __func__, sleep_soc, di->real_soc);
						if (sleep_soc > 0)
							di->count_sleep_time = 0;
						di->real_soc += sleep_soc;
						if (di->real_soc > 100)
							di->real_soc = 100;
					}
				} else {

					DBG("<%s>, usb charging\n", __func__);
					if (di->suspend_temp_soc + 15 < di->temp_soc)
						di->real_soc += (di->temp_soc - di->suspend_temp_soc)*3/2;
					else
						di->real_soc += (di->temp_soc - di->suspend_temp_soc);
				}

				DBG("POWER_SUPPLY_STATUS_CHARGING: di->temp_soc  = %d, di->real_soc = %d, sleep_time = %ld\n ", di->temp_soc , di->real_soc, sleep_sec);
			}
		}
	}
}
#endif

#ifdef SUPPORT_USB_CHARGE
static int  get_charging_status_type(struct battery_info *di)
{
	int otg_status = dwc_otg_check_dpdm();

	if (0 == otg_status) {
		di->usb_online = 0;
		di->ac_online = 0;
		di->check_count = 0;

	} else if (1 == otg_status) {
		if (0 == get_gadget_connect_flag()) {
			if (++di->check_count >= 5) {
				di->ac_online = 1;
				di->usb_online = 0;
			} else {
				di->ac_online = 0;
				di->usb_online = 1;
			}
		} else {
			di->ac_online = 0;
			di->usb_online = 1;
		}

	} else if (2 == otg_status) {
		di->ac_online = 1;
		di->usb_online = 0;
		di->check_count = 0;
	}

	if (di->ac_online == 1)
		set_charge_current(di, ILIM_3000MA);
	else
		set_charge_current(di, ILIM_450MA);
	return otg_status;
}

#endif

static void battery_poweron_status_init(struct battery_info *di)
{
	int otg_status;

#ifndef SUPPORT_USB_CHARGE
	u8 buf;
#endif

#ifdef SUPPORT_USB_CHARGE

	otg_status = dwc_otg_check_dpdm();
	if (otg_status == 1) {
		di->usb_online = 1;
		di->ac_online = 0;
		set_charge_current(di, ILIM_450MA);
		di->status = POWER_SUPPLY_STATUS_CHARGING;
		DBG("++++++++ILIM_450MA++++++\n");

	} else if (otg_status == 2) {
		di->usb_online = 0;
		di->ac_online = 1;
		di->status = POWER_SUPPLY_STATUS_CHARGING;
		set_charge_current(di, ILIM_3000MA);
		DBG("++++++++ILIM_1000MA++++++\n");
	}
	DBG(" CHARGE: SUPPORT_USB_CHARGE. charge_status = %d\n", otg_status);

#else

	battery_read(di->rk818, VB_MOD_REG, &buf, 1);
	if (buf&PLUG_IN_STS) {
		di->ac_online = 1;
		di->usb_online = 0;
		di->status = POWER_SUPPLY_STATUS_CHARGING;
		if (di->real_soc == 100)
			di->status = POWER_SUPPLY_STATUS_FULL;
	} else {
		di->status = POWER_SUPPLY_STATUS_DISCHARGING;
		di->ac_online = 0;
		di->usb_online = 0;
	}
	DBG(" CHARGE: NOT SUPPORT_USB_CHARGE\n");
#endif
}


static void check_battery_status(struct battery_info *di)
{
	u8 buf;
	int ret;

	ret = battery_read(di->rk818, VB_MOD_REG, &buf, 1);
#ifdef SUPPORT_USB_CHARGE

	if (strstr(saved_command_line, "charger")) {
		if ((buf&PLUG_IN_STS) == 0) {
			di->status = POWER_SUPPLY_STATUS_DISCHARGING;
			di->ac_online = 0;
			di->usb_online = 0;
		}

	} else {
		if (buf&PLUG_IN_STS) {
			get_charging_status_type(di);

			di->status = POWER_SUPPLY_STATUS_CHARGING;
			if (di->real_soc == 100)
				di->status = POWER_SUPPLY_STATUS_FULL;
		} else {
			di->status = POWER_SUPPLY_STATUS_DISCHARGING;
			di->ac_online = 0;
			di->usb_online = 0;
		}
	}
#else

	if (buf & PLUG_IN_STS) {
		di->ac_online = 1;
		di->usb_online = 0;
		di->status = POWER_SUPPLY_STATUS_CHARGING;
		if (di->real_soc == 100)
			di->status = POWER_SUPPLY_STATUS_FULL;
	} else {
		di->status = POWER_SUPPLY_STATUS_DISCHARGING;
		di->ac_online = 0;
		di->usb_online = 0;
	}
#endif
}

static void report_power_supply_changed(struct battery_info *di)
{
	static u32 old_soc;
	static u32 old_ac_status;
	static u32 old_usb_status;
	static u32 old_charge_status;
	bool state_changed;

	state_changed = false;
	if (di->real_soc == 0)
		state_changed = true;
	else if (di->real_soc == 100)
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
	}
}

static void update_battery_info(struct battery_info *di)
{
	di->remain_capacity = _get_realtime_capacity(di);
	if (di->remain_capacity > di->fcc)
		_capacity_init(di, di->fcc);
	else if (di->remain_capacity < 0)
		_capacity_init(di, 0);

	if (di->real_soc > 100)
		di->real_soc = 100;
	else if (di->real_soc < 0)
		di->real_soc = 0;

	if ((di->ac_online) || (di->usb_online)) {/*charging*/
		di->charging_time++;
		di->discharging_time = 0;
	} else {
		di->discharging_time++;
		di->charging_time = 0;
	}

	di->work_on = 1;
	di->voltage  = rk_battery_voltage(di);
	di->current_avg = _get_average_current(di);
	di->remain_capacity = _get_realtime_capacity(di);
	di->voltage_ocv = _get_OCV_voltage(di);
	di->charge_status = get_charge_status(di);
	di->otg_status = dwc_otg_check_dpdm();
	di->relax_voltage = get_relax_voltage(di);
	di->temp_soc = _get_soc(di);
	di->remain_capacity = _get_realtime_capacity(di);
	check_battery_status(di);/* ac_online, usb_online, status*/
	update_cal_offset(di);
}

static void rk_battery_work(struct work_struct *work)
{
	struct battery_info *di = container_of(work,
			struct battery_info, battery_monitor_work.work);

	update_resume_status_relax_voltage(di);
	wait_charge_finish_signal(di);
	charge_finish_routine(di);

	rk_battery_display_smooth(di);
	update_battery_info(di);

	report_power_supply_changed(di);
	_copy_soc(di, di->real_soc);
	_save_remain_capacity(di, di->remain_capacity);

	dump_debug_info(di);
	di->queue_work_cnt++;
	queue_delayed_work(di->wq, &di->battery_monitor_work, msecs_to_jiffies(TIMER_MS_COUNTS));
}

static void rk_battery_charge_check_work(struct work_struct *work)
{
	struct battery_info *di = container_of(work,
			struct battery_info, charge_check_work.work);

	DBG("rk_battery_charge_check_work\n");
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
	if ((di->real_soc <= 2) && (di->status == POWER_SUPPLY_STATUS_DISCHARGING)) {
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
		queue_delayed_work(di->wq, &di->charge_check_work, msecs_to_jiffies(50));
		break;

	case 1:
		di->charge_otg  = 1;
		queue_delayed_work(di->wq, &di->charge_check_work, msecs_to_jiffies(50));
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

	_copy_soc(g_battery, 0);
	_capacity_init(g_battery, 0);
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



static int rk818_battery_sysfs_init(struct battery_info *di, struct device *dev)
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

static void rk818_battery_irq_init(struct battery_info *di)
{
	int plug_in_irq, plug_out_irq, chg_ok_irq, vb_lo_irq;
	int ret;
	struct rk818 *chip = di->rk818;

	vb_lo_irq		= irq_create_mapping(chip->irq_domain, RK818_IRQ_VB_LO);
	plug_in_irq	= irq_create_mapping(chip->irq_domain, RK818_IRQ_PLUG_IN);
	plug_out_irq	= irq_create_mapping(chip->irq_domain, RK818_IRQ_PLUG_OUT);
	chg_ok_irq	= irq_create_mapping(chip->irq_domain, RK818_IRQ_CHG_OK);

	ret = request_threaded_irq(vb_lo_irq, NULL, rk818_vbat_lo_irq,
					IRQF_TRIGGER_HIGH, "rk818_vbatlow", chip);
	if (ret != 0)
		dev_err(chip->dev, "vb_lo_irq request failed!\n");

	di->irq = vb_lo_irq;
	enable_irq_wake(di->irq);
	disable_vbat_low_irq(di);

	ret = request_threaded_irq(plug_in_irq, NULL, rk818_vbat_plug_in,
					IRQF_TRIGGER_RISING, "rk818_vbat_plug_in", chip);
	if (ret != 0)
		dev_err(chip->dev, "plug_in_irq request failed!\n");


	ret = request_threaded_irq(plug_out_irq, NULL, rk818_vbat_plug_out,
					IRQF_TRIGGER_FALLING, "rk818_vbat_plug_out", chip);
	if (ret != 0)
		dev_err(chip->dev, "plug_out_irq request failed!\n");


	ret = request_threaded_irq(chg_ok_irq, NULL, rk818_vbat_charge_ok,
					IRQF_TRIGGER_RISING, "rk818_vbat_charge_ok", chip);
	if (ret != 0)
		dev_err(chip->dev, "chg_ok_irq request failed!\n");
}

static void battery_info_init(struct battery_info *di, struct rk818 *chip)
{
	u32 fcc_capacity;

	di->rk818 = chip;
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
	di->update_q = 0;
	di->voltage_old = 0;
	di->display_soc = 0;
	di->bat_res = 0;
	di->bat_res_updated = false;
	di->resume = false;
	di->sys_wakeup = true;
	di->status = POWER_SUPPLY_STATUS_DISCHARGING;

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
#ifdef CONFIG_OF
static int rk_battery_parse_dt(struct rk818 *rk818, struct device *dev)
{
	struct device_node *regs, *rk818_pmic_np;
	struct battery_platform_data *data;
	struct cell_config *cell_cfg;
	struct ocv_config *ocv_cfg;
	struct property *prop;
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
		ret = of_property_read_u32_array(regs, "ocv_table", data->battery_ocv, data->ocv_size);
		if (ret < 0)
			return ret;
	}

	ret = of_property_read_u32(regs, "max_charge_currentmA", &out_value);
	if (ret < 0) {
		dev_err(dev, "max_charge_currentmA not found!\n");
		return ret;
	}
	data->max_charger_currentmA = out_value;

	ret = of_property_read_u32(regs, "max_charge_voltagemV", &out_value);
	if (ret < 0) {
		dev_err(dev, "max_charge_voltagemV not found!\n");
		return ret;
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

	ret = of_property_read_u32(regs, "support_uboot_chrg", &support_uboot_chrg);

	cell_cfg->ocv = ocv_cfg;
	data->cell_cfg = cell_cfg;
	rk818->battery_data = data;

	DBG("\n--------- the battery OCV TABLE dump:\n");
	DBG("max_charge_currentmA :%d\n", data->max_charger_currentmA);
	DBG("max_charge_voltagemV :%d\n", data->max_charger_voltagemV);
	DBG("design_capacity :%d\n", cell_cfg->design_capacity);
	DBG("design_qmax :%d\n", cell_cfg->design_qmax);
	DBG("sleep_enter_current :%d\n", cell_cfg->ocv->sleep_enter_current);
	DBG("sleep_exit_current :%d\n", cell_cfg->ocv->sleep_exit_current);
	DBG("uboot chrg = %d\n", support_uboot_chrg);
	DBG("\n--------- rk818_battery dt_parse ok.\n");
	return 0;
}

#else
static int rk_battery_parse_dt(struct rk818 *rk818, struct device *dev)
{
	return -ENODEV;
}
#endif


static int battery_probe(struct platform_device *pdev)
{
	struct rk818 *chip = dev_get_drvdata(pdev->dev.parent);
	struct battery_info *di;
	int ret;

	DBG("battery driver version %s\n", DRIVER_VERSION);
	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&pdev->dev, "kzalloc battery_info memory failed!\n");
		return -ENOMEM;
	}
	ret = rk_battery_parse_dt(chip, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "rk_battery_parse_dt failed!\n");
		return -EINVAL;
	}

	platform_set_drvdata(pdev, di);
	battery_info_init(di, chip);
	if (!is_bat_exist(di)) {
		dev_err(&pdev->dev, "could not find Li-ion battery!\n");
		return -ENODEV;
	}
	fg_init(di);

	wake_lock_init(&di->resume_wake_lock, WAKE_LOCK_SUSPEND, "resume_charging");

	flatzone_voltage_init(di);
	battery_poweron_status_init(di);
	battery_power_supply_init(di);
	ret = battery_power_supply_register(di, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "rk power supply register failed!\n");
		return ret;
	}
	di->wq = create_singlethread_workqueue("battery-work");
	INIT_DELAYED_WORK(&di->battery_monitor_work, rk_battery_work);
	queue_delayed_work(di->wq, &di->battery_monitor_work, msecs_to_jiffies(TIMER_MS_COUNTS*5));
	INIT_DELAYED_WORK(&di->charge_check_work, rk_battery_charge_check_work);

	di->battery_nb.notifier_call = battery_notifier_call;
	register_battery_notifier(&di->battery_nb);

	rk818_battery_irq_init(di);
	rk818_battery_sysfs_init(di, &pdev->dev);
	DBG("------ RK81x battery_probe ok!-------\n");
	return ret;
}


#ifdef CONFIG_PM

static int battery_suspend(struct platform_device *dev, pm_message_t state)
{
	struct battery_info *di = platform_get_drvdata(dev);

	enable_vbat_low_irq(di);
	di->sleep_status = di->status;
	di->suspend_charge_current = _get_average_current(di);

	/* avoid abrupt wakeup which will clean the variable*/
	if (di->sys_wakeup) {
		di->suspend_capacity = di->remain_capacity;
		di->suspend_temp_soc = _get_soc(di);
		di->suspend_time_start = get_seconds();
		di->sys_wakeup = false;
	}

	cancel_delayed_work(&di->battery_monitor_work);
	DBG("<%s>. suspend_temp_soc,=%d, suspend_charge_current=%d, suspend_cap=%d, sleep_status=%d\n",
	    __func__, di->suspend_temp_soc, di->suspend_charge_current,
	    di->suspend_capacity, di->sleep_status);

	return 0;
}

static int battery_resume(struct platform_device *dev)
{
	struct battery_info *di = platform_get_drvdata(dev);

	di->resume = true;
	DBG("<%s>\n", __func__);
	disable_vbat_low_irq(di);
	queue_delayed_work(di->wq, &di->battery_monitor_work,
					msecs_to_jiffies(TIMER_MS_COUNTS/2));

	if (di->sleep_status == POWER_SUPPLY_STATUS_CHARGING ||
			di->real_soc <= 5)
		wake_lock_timeout(&di->resume_wake_lock, 5*HZ);

	return 0;
}
static int battery_remove(struct platform_device *dev)
{
	struct battery_info *di = platform_get_drvdata(dev);

	cancel_delayed_work_sync(&di->battery_monitor_work);
	return 0;
}
static void battery_shutdown(struct platform_device *dev)
{
	struct battery_info *di = platform_get_drvdata(dev);

	cancel_delayed_work_sync(&di->battery_monitor_work);
	DBG("rk818 shutdown!");
}
#endif

static struct platform_driver battery_driver = {
	.driver     = {
		.name   = "rk818-battery",
		.owner  = THIS_MODULE,
	},

	.probe      = battery_probe,
	.remove     = battery_remove,
	.suspend    = battery_suspend,
	.resume     = battery_resume,
	.shutdown  = battery_shutdown,
};

static int __init battery_init(void)
{
	return platform_driver_register(&battery_driver);
}

fs_initcall_sync(battery_init);
static void __exit battery_exit(void)
{
	platform_driver_unregister(&battery_driver);
}
module_exit(battery_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk818-battery");
MODULE_AUTHOR("ROCKCHIP");

