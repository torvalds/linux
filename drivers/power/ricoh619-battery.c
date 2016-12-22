/*
 * drivers/power/ricoh619-battery.c
 *
 * Charger driver for RICOH RC5T619 power management chip.
 *
 * Copyright (C) 2012-2013 RICOH COMPANY,LTD
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#define RICOH619_BATTERY_VERSION "RICOH619_BATTERY_VERSION: 2014.05.06"


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/power_supply.h>
#include <linux/mfd/ricoh619.h>
#include <linux/power/ricoh619_battery.h>
#include <linux/power/ricoh61x_battery_init.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/rk_keys.h>
#include <linux/rtc.h>

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>


/* define for function */
#define ENABLE_FUEL_GAUGE_FUNCTION
#define ENABLE_LOW_BATTERY_DETECTION
//#define ENABLE_FACTORY_MODE
#define DISABLE_CHARGER_TIMER
/* #define ENABLE_FG_KEEP_ON_MODE */
/* #define ENABLE_OCV_TABLE_CALIB */
//#define SUPPORT_USB_CONNECT_TO_ADP


/* FG setting */
#define RICOH619_REL1_SEL_VALUE		64
#define RICOH619_REL2_SEL_VALUE		0

enum int_type {
	SYS_INT  = 0x01,
	DCDC_INT = 0x02,
	ADC_INT  = 0x08,
	GPIO_INT = 0x10,
	CHG_INT	 = 0x40,
};

//for debug   #ifdef ENABLE_FUEL_GAUGE_FUNCTION
/* define for FG delayed time */
#define RICOH619_MONITOR_START_TIME		15
#define RICOH619_FG_RESET_TIME			6
#define RICOH619_FG_STABLE_TIME		120
#define RICOH619_DISPLAY_UPDATE_TIME		15
#define RICOH619_LOW_VOL_DOWN_TIME		10
#define RICOH619_CHARGE_MONITOR_TIME		20
#define RICOH619_CHARGE_RESUME_TIME		1
#define RICOH619_CHARGE_CALC_TIME		1
#define RICOH619_JEITA_UPDATE_TIME		60
#define RICOH619_DELAY_TIME				60
/* define for FG parameter */
#define RICOH619_MAX_RESET_SOC_DIFF		5
#define RICOH619_GET_CHARGE_NUM		10
#define RICOH619_UPDATE_COUNT_DISP		4
#define RICOH619_UPDATE_COUNT_FULL		4
#define RICOH619_UPDATE_COUNT_FULL_RESET 	7
#define RICOH619_CHARGE_UPDATE_TIME		3
#define RICOH619_FULL_WAIT_TIME			4
#define RE_CAP_GO_DOWN				10	/* 40 */
#define RICOH619_ENTER_LOW_VOL			70
#define RICOH619_TAH_SEL2			5
#define RICOH619_TAL_SEL2			6

#define RICOH619_OCV_OFFSET_BOUND	3
#define RICOH619_OCV_OFFSET_RATIO	2

#define RICOH619_VADP_DROP_WORK
#define RICOH619_TIME_CHG_STEP	(1*HZ)// unit:secound
#define RICOH619_TIME_CHG_COUNT	15*60//only for test //15*60 

/* define for FG status */
enum {
	RICOH619_SOCA_START,
	RICOH619_SOCA_UNSTABLE,
	RICOH619_SOCA_FG_RESET,
	RICOH619_SOCA_DISP,
	RICOH619_SOCA_STABLE,
	RICOH619_SOCA_ZERO,
	RICOH619_SOCA_FULL,
	RICOH619_SOCA_LOW_VOL,
};
//#endif

#ifdef ENABLE_LOW_BATTERY_DETECTION
#define LOW_BATTERY_DETECTION_TIME		10
#endif

struct ricoh619_soca_info {
	int Rbat;
	int n_cap;
	int ocv_table_def[11];
	int ocv_table[11];
	int ocv_table_low[11];
	int soc;		/* Latest FG SOC value */
	int displayed_soc;
	int suspend_soc;
	int status;		/* SOCA status 0: Not initial; 5: Finished */
	int stable_count;
	int chg_status;		/* chg_status */
	int soc_delta;		/* soc delta for status3(DISP) */
	int cc_delta;
	int cc_cap_offset;
	int last_soc;
	int last_displayed_soc;
	int ready_fg;
	int reset_count;
	int reset_soc[3];
	int chg_cmp_times;
	int dischg_state;
	int Vbat[RICOH619_GET_CHARGE_NUM];
	int Vsys[RICOH619_GET_CHARGE_NUM];
	int Ibat[RICOH619_GET_CHARGE_NUM];
	int Vbat_ave;
	int Vbat_old;
	int Vsys_ave;
	int Ibat_ave;
	int chg_count;
	int full_reset_count;
	int soc_full;
	int fc_cap;
	/* for LOW VOL state */
	int target_use_cap;
	int hurry_up_flg;
	int zero_flg;
	int re_cap_old;
	int cutoff_ocv;
	int Rsys;
	int target_vsys;
	int target_ibat;
	int jt_limit;
	int OCV100_min;
	int OCV100_max;
	int R_low;
	int rsoc_ready_flag;
	int init_pswr;
	int last_cc_sum;
};

struct ricoh619_battery_info {
	struct device      *dev;
	struct power_supply	battery;
	struct delayed_work	monitor_work;
	struct delayed_work	displayed_work;
	struct delayed_work	charge_stable_work;
	struct delayed_work	changed_work;
#ifdef ENABLE_LOW_BATTERY_DETECTION
	struct delayed_work	low_battery_work;
#endif
	struct delayed_work	charge_monitor_work;
	struct delayed_work	get_charge_work;
	struct delayed_work	jeita_work;
	struct delayed_work	charge_complete_ready;

	struct work_struct	irq_work;	/* for Charging & VUSB/VADP */
	struct work_struct	usb_irq_work;	/* for ADC_VUSB */
	#ifdef RICOH619_VADP_DROP_WORK
	struct delayed_work	vadp_drop_work;
	#endif
	struct workqueue_struct *monitor_wqueue;
	struct workqueue_struct *workqueue;	/* for Charging & VUSB/VADP */
	struct workqueue_struct *usb_workqueue;	/* for ADC_VUSB */

#ifdef ENABLE_FACTORY_MODE
	struct delayed_work	factory_mode_work;
	struct workqueue_struct *factory_mode_wqueue;
#endif

	struct mutex		lock;
	unsigned long		monitor_time;
	int		adc_vdd_mv;
	int		multiple;
	int		alarm_vol_mv;
	int		status;
	int		min_voltage;
	int		max_voltage;
	int		cur_voltage;
	int		capacity;
	int		battery_temp;
	int		time_to_empty;
	int		time_to_full;
	int		chg_ctr;
	int		chg_stat1;
	unsigned	present:1;
	u16		delay;
	struct		ricoh619_soca_info *soca;
	int		first_pwon;
	bool		entry_factory_mode;
	int		ch_vfchg;
	int		ch_vrchg;
	int		ch_vbatovset;
	int		ch_ichg;
	int		ch_ilim_adp;
	int		ch_ilim_usb;
	int		ch_icchg;
	int		fg_target_vsys;
	int		fg_target_ibat;
	int		fg_poff_vbat;
	int		jt_en;
	int		jt_hw_sw;
	int		jt_temp_h;
	int		jt_temp_l;
	int		jt_vfchg_h;
	int		jt_vfchg_l;
	int		jt_ichg_h;
	int		jt_ichg_l;

	int 	chg_complete_rd_flag;
	int 	chg_complete_rd_cnt;
	int		chg_complete_tm_ov_flag;
	int		chg_complete_sleep_flag;
	int		chg_old_dsoc;

	int 		num;
	};

struct power_supply powerac;
struct power_supply powerusb;

int g_full_flag;
int charger_irq;
/* this value is for mfd fucntion */
int g_soc;
int g_fg_on_mode;
int type_n;
extern int dwc_otg_check_dpdm(bool wait);
/*This is for full state*/
static int BatteryTableFlagDef=0;
static int BatteryTypeDef=0;
static int Battery_Type(void)
{
	return BatteryTypeDef;
}

static int Battery_Table(void)
{
	return BatteryTableFlagDef;
}

static void ricoh619_battery_work(struct work_struct *work)
{
	struct ricoh619_battery_info *info = container_of(work,
		struct ricoh619_battery_info, monitor_work.work);

	RICOH_FG_DBG("PMU: %s\n", __func__);
	power_supply_changed(&info->battery);
	queue_delayed_work(info->monitor_wqueue, &info->monitor_work,
			   info->monitor_time);
}

#ifdef ENABLE_FUEL_GAUGE_FUNCTION
static int measure_vbatt_FG(struct ricoh619_battery_info *info, int *data);
static int measure_Ibatt_FG(struct ricoh619_battery_info *info, int *data);
static int calc_capacity(struct ricoh619_battery_info *info);
static int calc_capacity_2(struct ricoh619_battery_info *info);
static int get_OCV_init_Data(struct ricoh619_battery_info *info, int index);
static int get_OCV_voltage(struct ricoh619_battery_info *info, int index);
static int get_check_fuel_gauge_reg(struct ricoh619_battery_info *info,
					 int Reg_h, int Reg_l, int enable_bit);
static int calc_capacity_in_period(struct ricoh619_battery_info *info,
				 int *cc_cap, bool *is_charging, bool cc_rst);
//static int get_charge_priority(struct ricoh619_battery_info *info, bool *data);
//static int set_charge_priority(struct ricoh619_battery_info *info, bool *data);
static int get_power_supply_status(struct ricoh619_battery_info *info);
static int get_power_supply_Android_status(struct ricoh619_battery_info *info);
static int measure_vsys_ADC(struct ricoh619_battery_info *info, int *data);
static int Calc_Linear_Interpolation(int x0, int y0, int x1, int y1, int y);
static int get_battery_temp(struct ricoh619_battery_info *info);
static int get_battery_temp_2(struct ricoh619_battery_info *info);
static int check_jeita_status(struct ricoh619_battery_info *info, bool *is_jeita_updated);
static void ricoh619_scaling_OCV_table(struct ricoh619_battery_info *info, int cutoff_vol, int full_vol, int *start_per, int *end_per);
//static int ricoh619_Check_OCV_Offset(struct ricoh619_battery_info *info);

static int calc_ocv(struct ricoh619_battery_info *info)
{
	int Vbat = 0;
	int Ibat = 0;
	int ret;
	int ocv;

	ret = measure_vbatt_FG(info, &Vbat);
	ret = measure_Ibatt_FG(info, &Ibat);

	ocv = Vbat - Ibat * info->soca->Rbat;

	return ocv;
}

#if 0
static int set_Rlow(struct ricoh619_battery_info *info)
{
	int err;
	int Rbat_low_max;
	uint8_t val;
	int Vocv;
	int temp;

	if (info->soca->Rbat == 0)
			info->soca->Rbat = get_OCV_init_Data(info, 12) * 1000 / 512
							 * 5000 / 4095;
	
	Vocv = calc_ocv(info);
	Rbat_low_max = info->soca->Rbat * 1.5;

	if (Vocv < get_OCV_voltage(info,3))
	{
		info->soca->R_low = Calc_Linear_Interpolation(info->soca->Rbat,get_OCV_voltage(info,3),
			Rbat_low_max, get_OCV_voltage(info,0), Vocv);
		RICOH_FG_DBG("PMU: Modify RBAT from %d to %d ", info->soca->Rbat, info->soca->R_low);
		temp = info->soca->R_low *4095/5000*512/1000;
		
		val = info->soca->R_low>>8;
		err = ricoh619_write_bank1(info->dev->parent, 0xD4, val);
		if (err < 0) {
			dev_err(info->dev, "batterry initialize error\n");
			return err;
		}

		val = info->soca->R_low & 0xff;
		err = ricoh619_write_bank1(info->dev->parent, 0xD5, val);
		if (err < 0) {
			dev_err(info->dev, "batterry initialize error\n");
			return err;
		}
	}
	else  info->soca->R_low = 0;
		

	return err;
}

static int Set_back_ocv_table(struct ricoh619_battery_info *info)
{
	int err;
	uint8_t val;
	int temp;
	int i;
	uint8_t debug_disp[22];

	/* Modify back ocv table */

	if (0 != info->soca->ocv_table_low[0])
	{
		for (i = 0 ; i < 11; i++){
			battery_init_para[info->num][i*2 + 1] = info->soca->ocv_table_low[i];
			battery_init_para[info->num][i*2] = info->soca->ocv_table_low[i] >> 8;
		}
		err = ricoh619_clr_bits(info->dev->parent, FG_CTRL_REG, 0x01);

		err = ricoh619_bulk_writes_bank1(info->dev->parent,
			BAT_INIT_TOP_REG, 22, battery_init_para[info->num]);

		err = ricoh619_set_bits(info->dev->parent, FG_CTRL_REG, 0x01);

		/* debug comment start*/
		err = ricoh619_bulk_reads_bank1(info->dev->parent,
			BAT_INIT_TOP_REG, 22, debug_disp);
		for (i = 0; i < 11; i++){
			RICOH_FG_DBG("PMU : %s : after OCV table %d 0x%x\n",__func__, i * 10, (debug_disp[i*2] << 8 | debug_disp[i*2+1]));
		}
		/* end */
		/* clear table*/
		for(i = 0; i < 11; i++)
		{
			info->soca->ocv_table_low[i] = 0;
		}
	}
	
	/* Modify back Rbat */
	if (0!=info->soca->R_low)
	{		
		RICOH_FG_DBG("PMU: Modify back RBAT from %d to %d ",  info->soca->R_low,info->soca->Rbat);
		temp = info->soca->Rbat*4095/5000*512/1000;
		
		val = info->soca->R_low>>8;
		err = ricoh619_write_bank1(info->dev->parent, 0xD4, val);
		if (err < 0) {
			dev_err(info->dev, "batterry initialize error\n");
			return err;
		}

		val = info->soca->R_low & 0xff;
		err = ricoh619_write_bank1(info->dev->parent, 0xD5, val);
		if (err < 0) {
			dev_err(info->dev, "batterry initialize error\n");
			return err;
		}

		info->soca->R_low = 0;
	}
	return 0;
}

/**
**/

static int ricoh619_Check_OCV_Offset(struct ricoh619_battery_info *info)
{
	int ocv_table[11]; // HEX value
	int i;
	int temp;
	int ret;
	uint8_t debug_disp[22];
	uint8_t val = 0;

	RICOH_FG_DBG("PMU : %s : calc ocv %d get OCV %d\n",__func__,calc_ocv(info),get_OCV_voltage(info, RICOH619_OCV_OFFSET_BOUND));

	/* check adp/usb status */
	ret = ricoh619_read(info->dev->parent, CHGSTATE_REG, &val);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the control register\n");
		return ret;
	}

	val = (val & 0xC0) >> 6;

	if (val != 0){ /* connect adp or usb */
		if (calc_ocv(info) < get_OCV_voltage(info, RICOH619_OCV_OFFSET_BOUND) )
		{
			if(0 == info->soca->ocv_table_low[0]){
				for (i = 0 ; i < 11; i++){
				ocv_table[i] = (battery_init_para[info->num][i*2]<<8) | (battery_init_para[info->num][i*2+1]);
				RICOH_FG_DBG("PMU : %s : OCV table %d 0x%x\n",__func__,i * 10, ocv_table[i]);
				info->soca->ocv_table_low[i] = ocv_table[i];
				}

				for (i = 0 ; i < 11; i++){
					temp = ocv_table[i] * (100 + RICOH619_OCV_OFFSET_RATIO) / 100;

					battery_init_para[info->num][i*2 + 1] = temp;
					battery_init_para[info->num][i*2] = temp >> 8;
				}
				ret = ricoh619_clr_bits(info->dev->parent, FG_CTRL_REG, 0x01);

				ret = ricoh619_bulk_writes_bank1(info->dev->parent,
					BAT_INIT_TOP_REG, 22, battery_init_para[info->num]);

				ret = ricoh619_set_bits(info->dev->parent, FG_CTRL_REG, 0x01);

				/* debug comment start*/
				ret = ricoh619_bulk_reads_bank1(info->dev->parent,
					BAT_INIT_TOP_REG, 22, debug_disp);
				for (i = 0; i < 11; i++){
					RICOH_FG_DBG("PMU : %s : after OCV table %d 0x%x\n",__func__, i * 10, (debug_disp[i*2] << 8 | debug_disp[i*2+1]));
				}
				/* end */
			}
		}
	}
	
	return 0;
}
#endif
static int reset_FG_process(struct ricoh619_battery_info *info)
{
	int err;

	//err = set_Rlow(info);
	//err = ricoh619_Check_OCV_Offset(info);
	err = ricoh619_write(info->dev->parent,
					 FG_CTRL_REG, 0x51);
	info->soca->ready_fg = 0;
	return err;
}


static int check_charge_status_2(struct ricoh619_battery_info *info, int displayed_soc_temp)
{
	if (displayed_soc_temp < 0)
			displayed_soc_temp = 0;
	
	get_power_supply_status(info);
	info->soca->soc = calc_capacity(info) * 100;

	if (POWER_SUPPLY_STATUS_FULL == info->soca->chg_status) {
		if ((info->first_pwon == 1)
			&& (RICOH619_SOCA_START == info->soca->status)) {
				g_full_flag = 1;
				info->soca->soc_full = info->soca->soc;
				info->soca->displayed_soc = 100*100;
				info->soca->full_reset_count = 0;
		} else {
			if ( (displayed_soc_temp > 97*100)
				&& (calc_ocv(info) > (get_OCV_voltage(info, 9) + (get_OCV_voltage(info, 10) - get_OCV_voltage(info, 9))*7/10)  )){
				g_full_flag = 1;
				info->soca->soc_full = info->soca->soc;
				info->soca->displayed_soc = 100*100;
				info->soca->full_reset_count = 0;
			} else {
				g_full_flag = 0;
				info->soca->displayed_soc = displayed_soc_temp;
			}

		}
	}
	if (info->soca->Ibat_ave >= 0) {
		if (g_full_flag == 1) {
			info->soca->displayed_soc = 100*100;
		} else {
			if (info->soca->displayed_soc/100 < 99) {
				info->soca->displayed_soc = displayed_soc_temp;
			} else {
				info->soca->displayed_soc = 99 * 100;
			}
		}
	}
	if (info->soca->Ibat_ave < 0) {
		if (g_full_flag == 1) {
			if (calc_ocv(info) < (get_OCV_voltage(info, 9) + (get_OCV_voltage(info, 10) - get_OCV_voltage(info, 9))*7/10)  ) {
				g_full_flag = 0;
				//info->soca->displayed_soc = 100*100;
				info->soca->displayed_soc = displayed_soc_temp;
			} else {
				info->soca->displayed_soc = 100*100;
			}
		} else {
			g_full_flag = 0;
			info->soca->displayed_soc = displayed_soc_temp;
		}
	}

	return info->soca->displayed_soc;
}

/**
* Calculate Capacity in a period
* - read CC_SUM & FA_CAP from Coulom Counter
* -  and calculate Capacity.
* @cc_cap: capacity in a period, unit 0.01%
* @is_charging: Flag of charging current direction
*               TRUE : charging (plus)
*               FALSE: discharging (minus)
* @cc_rst: reset CC_SUM or not
*               TRUE : reset
*               FALSE: not reset
**/
static int calc_capacity_in_period(struct ricoh619_battery_info *info,
				 int *cc_cap, bool *is_charging, bool cc_rst)
{
	int err;
	uint8_t 	cc_sum_reg[4];
	uint8_t 	cc_clr[4] = {0, 0, 0, 0};
	uint8_t 	fa_cap_reg[2];
	uint16_t 	fa_cap;
	uint32_t 	cc_sum;
	int		cc_stop_flag;
	uint8_t 	status;
	uint8_t 	charge_state;
	int 		Ocv;
	uint32_t 	cc_cap_temp;
	uint32_t 	cc_cap_min;
	int		cc_cap_res;

	*is_charging = true;	/* currrent state initialize -> charging */

	if (info->entry_factory_mode)
		return 0;

	//check need charge stop or not
	/* get  power supply status */
	err = ricoh619_read(info->dev->parent, CHGSTATE_REG, &status);
	if (err < 0)
		goto out;
	charge_state = (status & 0x1F);
	Ocv = calc_ocv(info);
	if (charge_state == CHG_STATE_CHG_COMPLETE) {
		/* Check CHG status is complete or not */
		cc_stop_flag = 0;
	} else if (calc_capacity(info) == 100) {
		/* Check HW soc is 100 or not */
		cc_stop_flag = 0;
	} else if (Ocv/1000 < get_OCV_voltage(info, 9)) {
		/* Check VBAT is high level or not */
		cc_stop_flag = 0;
	} else {
		cc_stop_flag = 1;
	}

	if (cc_stop_flag == 1)
	{
		/* Disable Charging/Completion Interrupt */
		err = ricoh619_set_bits(info->dev->parent,
						RICOH619_INT_MSK_CHGSTS1, 0x01);
		if (err < 0)
			goto out;

		/* disable charging */
		err = ricoh619_clr_bits(info->dev->parent, RICOH619_CHG_CTL1, 0x03);
		if (err < 0)
			goto out;
	}

	/* CC_pause enter */
	err = ricoh619_write(info->dev->parent, CC_CTRL_REG, 0x01);
	if (err < 0)
		goto out;

	/* Read CC_SUM */
	err = ricoh619_bulk_reads(info->dev->parent,
					CC_SUMREG3_REG, 4, cc_sum_reg);
	if (err < 0)
		goto out;

	if (cc_rst == true) {
		/* CC_SUM <- 0 */
		err = ricoh619_bulk_writes(info->dev->parent,
						CC_SUMREG3_REG, 4, cc_clr);
		if (err < 0)
			goto out;
	}

	/* CC_pause exist */
	err = ricoh619_write(info->dev->parent, CC_CTRL_REG, 0);
	if (err < 0)
		goto out;
	if (cc_stop_flag == 1)
	{
	
		/* Enable charging */
		err = ricoh619_set_bits(info->dev->parent, RICOH619_CHG_CTL1, 0x03);
		if (err < 0)
			goto out;

		udelay(1000);

		/* Clear Charging Interrupt status */
		err = ricoh619_clr_bits(info->dev->parent,
					RICOH619_INT_IR_CHGSTS1, 0x01);
		if (err < 0)
			goto out;

		/* ricoh619_read(info->dev->parent, RICOH619_INT_IR_CHGSTS1, &val);
		RICOH_FG_DBG("INT_IR_CHGSTS1 = 0x%x\n",val); */

		/* Enable Charging Interrupt */
		err = ricoh619_clr_bits(info->dev->parent,
						RICOH619_INT_MSK_CHGSTS1, 0x01);
		if (err < 0)
			goto out;
	}
	/* Read FA_CAP */
	err = ricoh619_bulk_reads(info->dev->parent,
				 FA_CAP_H_REG, 2, fa_cap_reg);
	if (err < 0)
		goto out;

	/* fa_cap = *(uint16_t*)fa_cap_reg & 0x7fff; */
	fa_cap = (fa_cap_reg[0] << 8 | fa_cap_reg[1]) & 0x7fff;

	/* cc_sum = *(uint32_t*)cc_sum_reg; */
	cc_sum = cc_sum_reg[0] << 24 | cc_sum_reg[1] << 16 |
				cc_sum_reg[2] << 8 | cc_sum_reg[3];

	/* calculation  two's complement of CC_SUM */
	if (cc_sum & 0x80000000) {
		cc_sum = (cc_sum^0xffffffff)+0x01;
		*is_charging = false;		/* discharge */
	}
	/* (CC_SUM x 10000)/3600/FA_CAP */
	*cc_cap = cc_sum*25/9/fa_cap;		/* unit is 0.01% */

	//////////////////////////////////////////////////////////////////	
	cc_cap_min = fa_cap*3600/100/100/100;	/* Unit is 0.0001% */
	cc_cap_temp = cc_sum / cc_cap_min;
		
	cc_cap_res = cc_cap_temp % 100;
	
	RICOH_FG_DBG("PMU: cc_sum = %d: cc_cap_res= %d: \n", cc_sum, cc_cap_res);
	
	
	if(*is_charging) {
		info->soca->cc_cap_offset += cc_cap_res;
		if (info->soca->cc_cap_offset >= 100) {
			*cc_cap += 1;
			info->soca->cc_cap_offset %= 100;
		}
	} else {
		info->soca->cc_cap_offset -= cc_cap_res;
		if (info->soca->cc_cap_offset <= -100) {
			*cc_cap += 1;
			info->soca->cc_cap_offset %= 100;
		}
	}
	RICOH_FG_DBG("PMU: cc_cap_offset= %d: \n", info->soca->cc_cap_offset);
	
	//////////////////////////////////////////////////////////////////
	return 0;
out:
	dev_err(info->dev, "Error !!-----\n");
	return err;
}
/**
* Calculate target using capacity
**/
static int get_target_use_cap(struct ricoh619_battery_info *info)
{
	int i,j;
	int ocv_table[11];
	int temp;
//	int Target_Cutoff_Vol = 0;
	int Ocv_ZeroPer_now;
	int Ibat_now;
	int fa_cap,use_cap;
	int FA_CAP_now;
	int start_per = 0;
	int RE_CAP_now;
	int CC_OnePer_step;
	int Ibat_min;

//	int Ocv_now;
	int Ocv_now_table;
//	int soc_per;
//	int use_cap_now;
	int Rsys_now;

	/* get const value */
	Ibat_min = -1 * info->soca->target_ibat;
	if (info->soca->Ibat_ave > Ibat_min) /* I bat is minus */
	{
		Ibat_now = Ibat_min;
	} else {
		Ibat_now = info->soca->Ibat_ave;
	}
	fa_cap = get_check_fuel_gauge_reg(info, FA_CAP_H_REG, FA_CAP_L_REG,
								0x7fff);
	use_cap = fa_cap - info->soca->re_cap_old;

	/* get OCV table % */
	for (i = 0; i <= 10; i = i+1) {
		temp = (battery_init_para[info->num][i*2]<<8)
			 | (battery_init_para[info->num][i*2+1]);
		/* conversion unit 1 Unit is 1.22mv (5000/4095 mv) */
		temp = ((temp * 50000 * 10 / 4095) + 5) / 10;
		ocv_table[i] = temp;
		RICOH_FG_DBG("PMU : %s : ocv_table %d is %d v\n",__func__, i, ocv_table[i]);
	}

	/* Find out Current OCV */
	i = info->soca->soc/1000;
	j = info->soca->soc - info->soca->soc/1000*1000;
	Ocv_now_table = ocv_table[i]*100+(ocv_table[i+1]-ocv_table[i])*j/10;

	Rsys_now = (info->soca->Vsys_ave - Ocv_now_table) / info->soca->Ibat_ave;
	if (((abs(info->soca->soc - info->soca->displayed_soc)) > 10)
		&& (info->soca->Ibat_ave > -250)) {
		if (Rsys_now < 0)
			Rsys_now = max(-info->soca->Rbat, Rsys_now);
		else
			Rsys_now = min(info->soca->Rbat, Rsys_now);
	}

	Ocv_ZeroPer_now = info->soca->target_vsys * 1000 - Ibat_now * Rsys_now;

	RICOH_FG_DBG("PMU: -------  Ocv_now_table= %d: Rsys_now= %d =======\n",
	       Ocv_now_table, Rsys_now);

	RICOH_FG_DBG("PMU: -------  Rsys= %d: cutoff_ocv= %d: Ocv_ZeroPer_now= %d =======\n",
	       info->soca->Rsys, info->soca->cutoff_ocv, Ocv_ZeroPer_now);

	/* get FA_CAP_now */

	
	for (i = 1; i < 11; i++) {
		RICOH_FG_DBG("PMU : %s : ocv_table %d is %d v Ocv_ZerPernow is %d\n",__func__, i, ocv_table[i],(Ocv_ZeroPer_now / 100));
		if (ocv_table[i] >= Ocv_ZeroPer_now / 100) {
			/* unit is 0.001% */
			start_per = Calc_Linear_Interpolation(
				(i-1)*1000, ocv_table[i-1], i*1000,
				 ocv_table[i], (Ocv_ZeroPer_now / 100));
			i = 11;
		}
	}

	start_per = max(0, start_per);

	FA_CAP_now = fa_cap * ((10000 - start_per) / 100 ) / 100;

	RICOH_FG_DBG("PMU: -------Ocv_ZeroPer_now= %d: start_per= %d =======\n",
		Ocv_ZeroPer_now, start_per);

	/* get RE_CAP_now */
	RE_CAP_now = FA_CAP_now - use_cap;
	
	if (RE_CAP_now < RE_CAP_GO_DOWN) {
		info->soca->hurry_up_flg = 1;
	} else if (info->soca->Vsys_ave < info->soca->target_vsys*1000) {
		info->soca->hurry_up_flg = 1;
	} else if (info->fg_poff_vbat != 0) {
		if (info->soca->Vbat_ave < info->fg_poff_vbat*1000) {
			info->soca->hurry_up_flg = 1;
		} else {
			info->soca->hurry_up_flg = 0;
		}
	} else {
		info->soca->hurry_up_flg = 0;
	}

	/* get CC_OnePer_step */
	if (info->soca->displayed_soc > 0) { /* avoid divide-by-0 */
		CC_OnePer_step = RE_CAP_now / (info->soca->displayed_soc / 100 + 1);
	} else {
		CC_OnePer_step = 0;
	}
	/* get info->soca->target_use_cap */
	info->soca->target_use_cap = use_cap + CC_OnePer_step;
	
	RICOH_FG_DBG("PMU: ------- FA_CAP_now= %d: RE_CAP_now= %d: CC_OnePer_step= %d: target_use_cap= %d: hurry_up_flg= %d -------\n",
	       FA_CAP_now, RE_CAP_now, CC_OnePer_step, info->soca->target_use_cap, info->soca->hurry_up_flg);
	
	return 0;
}
#ifdef ENABLE_OCV_TABLE_CALIB
/**
* Calibration OCV Table
* - Update the value of VBAT on 100% in OCV table 
*    if battery is Full charged.
* - int vbat_ocv <- unit is uV
**/
static int calib_ocvTable(struct ricoh619_battery_info *info, int vbat_ocv)
{
	int ret;
	int cutoff_ocv;
	int i;
	int ocv100_new;
	int start_per = 0;
	int end_per = 0;
	
	RICOH_FG_DBG("PMU: %s\n", __func__);
	
	if (info->soca->Ibat_ave > RICOH619_REL1_SEL_VALUE) {
		RICOH_FG_DBG("PMU: %s IBAT > 64mA -- Not Calibration --\n", __func__);
		return 0;
	}
	
	if (vbat_ocv < info->soca->OCV100_max) {
		if (vbat_ocv < info->soca->OCV100_min)
			ocv100_new = info->soca->OCV100_min;
		else
			ocv100_new = vbat_ocv;
	} else {
		ocv100_new = info->soca->OCV100_max;
	}
	RICOH_FG_DBG("PMU : %s :max %d min %d current %d\n",__func__,info->soca->OCV100_max,info->soca->OCV100_min,vbat_ocv);
	RICOH_FG_DBG("PMU : %s : New OCV 100% = 0x%x\n",__func__,ocv100_new);
	
	/* FG_En Off */
	ret = ricoh619_clr_bits(info->dev->parent, FG_CTRL_REG, 0x01);
	if (ret < 0) {
		dev_err("PMU: %s Error in FG_En OFF\n", __func__);
		goto err;
	}


	//cutoff_ocv = (battery_init_para[info->num][0]<<8) | (battery_init_para[info->num][1]);
	cutoff_ocv = get_OCV_voltage(info, 0);

	info->soca->ocv_table_def[10] = info->soca->OCV100_max;

	ricoh619_scaling_OCV_table(info, cutoff_ocv/1000, ocv100_new/1000, &start_per, &end_per);

	ret = ricoh619_bulk_writes_bank1(info->dev->parent,
				BAT_INIT_TOP_REG, 22, battery_init_para[info->num]);
	if (ret < 0) {
		dev_err(info->dev, "batterry initialize error\n");
		goto err;
	}

	for (i = 0; i <= 10; i = i+1) {
		info->soca->ocv_table[i] = get_OCV_voltage(info, i);
		RICOH_FG_DBG("PMU: %s : * %d0%% voltage = %d uV\n",
				 __func__, i, info->soca->ocv_table[i]);
	}
	
	/* FG_En on & Reset*/
	ret = reset_FG_process(info);
	if (ret < 0) {
		dev_err("PMU: %s Error in FG_En On & Reset\n", __func__);
		goto err;
	}

	RICOH_FG_DBG("PMU: %s Exit \n", __func__);
	return 0;
err:
	return ret;

}
#endif

static void ricoh619_displayed_work(struct work_struct *work)
{
	int err;
	uint8_t val;
	uint8_t val2;
	int soc_round;
	int last_soc_round;
	int last_disp_round;
	int displayed_soc_temp;
	int disp_dec;
	int cc_cap = 0;
	bool is_charging = true;
	int re_cap,fa_cap,use_cap;
	bool is_jeita_updated;
	uint8_t reg_val;
	int delay_flag = 0;
	int Vbat = 0;
	int Ibat = 0;
	int Vsys = 0;
	int temp_ocv;
	int fc_delta = 0;

	struct ricoh619_battery_info *info = container_of(work,
	struct ricoh619_battery_info, displayed_work.work);

	if (info->entry_factory_mode) {
		info->soca->status = RICOH619_SOCA_STABLE;
		info->soca->displayed_soc = -EINVAL;
		info->soca->ready_fg = 0;
		return;
	}

	mutex_lock(&info->lock);
	
	is_jeita_updated = false;

	if ((RICOH619_SOCA_START == info->soca->status)
		 || (RICOH619_SOCA_STABLE == info->soca->status)
		 || (RICOH619_SOCA_FULL == info->soca->status))
		info->soca->ready_fg = 1;

	/* judge Full state or Moni Vsys state */
	if ((RICOH619_SOCA_DISP == info->soca->status)
		 || (RICOH619_SOCA_STABLE == info->soca->status))
	{
		/* caluc 95% ocv */
		temp_ocv = get_OCV_voltage(info, 10) -
					(get_OCV_voltage(info, 10) - get_OCV_voltage(info, 9))/2;
		
		if(g_full_flag == 1){	/* for issue 1 solution start*/
			info->soca->status = RICOH619_SOCA_FULL;
		}else if ((POWER_SUPPLY_STATUS_FULL == info->soca->chg_status)
			&& (calc_ocv(info) > temp_ocv)) {
			info->soca->status = RICOH619_SOCA_FULL;
			g_full_flag = 0;
		} else if (info->soca->Ibat_ave >= -20) {
			/* for issue1 solution end */
			/* check Full state or not*/
			if ((calc_ocv(info) > (get_OCV_voltage(info, 9) + (get_OCV_voltage(info, 10) - get_OCV_voltage(info, 9))*7/10))
				|| (POWER_SUPPLY_STATUS_FULL == info->soca->chg_status)
				|| (info->soca->displayed_soc > 9850))
			{
				info->soca->status = RICOH619_SOCA_FULL;
				g_full_flag = 0;
			} else if ((calc_ocv(info) > (get_OCV_voltage(info, 9)))
				&& (info->soca->Ibat_ave < 300))
			{
				info->soca->status = RICOH619_SOCA_FULL;
				g_full_flag = 0;
			}
		} else { /* dis-charging */
			if (info->soca->displayed_soc/100 < RICOH619_ENTER_LOW_VOL) {
				info->soca->target_use_cap = 0;
				info->soca->status = RICOH619_SOCA_LOW_VOL;
			}
		}
	}

	if (RICOH619_SOCA_STABLE == info->soca->status) {
		info->soca->soc = calc_capacity_2(info);
		info->soca->soc_delta = info->soca->soc - info->soca->last_soc;

		if (info->soca->soc_delta >= -100 && info->soca->soc_delta <= 100) {
			info->soca->displayed_soc = info->soca->soc;
		} else {
			info->soca->status = RICOH619_SOCA_DISP;
		}
		info->soca->last_soc = info->soca->soc;
		info->soca->soc_delta = 0;
	} else if (RICOH619_SOCA_FULL == info->soca->status) {
		err = check_jeita_status(info, &is_jeita_updated);
		if (err < 0) {
			dev_err(info->dev, "Error in updating JEITA %d\n", err);
			goto end_flow;
		}
		info->soca->soc = calc_capacity(info) * 100;
		info->soca->last_soc = calc_capacity_2(info);	/* for DISP */

		if (info->soca->Ibat_ave >= -20) { /* charging */
			if (0 == info->soca->jt_limit) {
				if (g_full_flag == 1) {
					
					if (POWER_SUPPLY_STATUS_FULL == info->soca->chg_status) {
						if(info->soca->full_reset_count < RICOH619_UPDATE_COUNT_FULL_RESET) {
							info->soca->full_reset_count++;
						} else if (info->soca->full_reset_count < (RICOH619_UPDATE_COUNT_FULL_RESET + 1)) {
							err = reset_FG_process(info);
							if (err < 0)
								dev_err(info->dev, "Error in writing the control register\n");
							info->soca->full_reset_count++;
							goto end_flow;
						} else if(info->soca->full_reset_count < (RICOH619_UPDATE_COUNT_FULL_RESET + 2)) {
							info->soca->full_reset_count++;
							info->soca->fc_cap = 0;
							info->soca->soc_full = info->soca->soc;
						}
					} else {
						if(info->soca->fc_cap < -1 * 200) {
							g_full_flag = 0;
							info->soca->displayed_soc = 99 * 100;
						}
						info->soca->full_reset_count = 0;
					}
					

					info->soca->chg_cmp_times = 0;
					err = calc_capacity_in_period(info, &cc_cap, &is_charging, true);
					if (err < 0)
					dev_err(info->dev, "Read cc_sum Error !!-----\n");

					fc_delta = (is_charging == true) ? cc_cap : -cc_cap;

					info->soca->fc_cap = info->soca->fc_cap + fc_delta;

					if (g_full_flag == 1){
						info->soca->displayed_soc = 100*100;
					}
				} else {
					if (calc_ocv(info) < (get_OCV_voltage(info, 8))) { /* fail safe*/
						g_full_flag = 0;
						info->soca->status = RICOH619_SOCA_DISP;
						info->soca->soc_delta = 0;
					} else if ((POWER_SUPPLY_STATUS_FULL == info->soca->chg_status) 
						&& (info->soca->displayed_soc >= 9890)){
						if(info->soca->chg_cmp_times > RICOH619_FULL_WAIT_TIME) {
							info->soca->displayed_soc = 100*100;
							g_full_flag = 1;
							info->soca->full_reset_count = 0;
							info->soca->soc_full = info->soca->soc;
							info->soca->fc_cap = 0;
#ifdef ENABLE_OCV_TABLE_CALIB
							err = calib_ocvTable(info,calc_ocv(info));
							if (err < 0)
								dev_err(info->dev, "Calibration OCV Error !!\n");
#endif
						} else {
							info->soca->chg_cmp_times++;
						}
					} else {
						fa_cap = get_check_fuel_gauge_reg(info, FA_CAP_H_REG, FA_CAP_L_REG,
							0x7fff);
						
						if (info->soca->displayed_soc >= 9950) {
							if((info->soca->soc_full - info->soca->soc) < 200) {
								goto end_flow;
							}
						}
						info->soca->chg_cmp_times = 0;

						err = calc_capacity_in_period(info, &cc_cap, &is_charging, true);
						if (err < 0)
						dev_err(info->dev, "Read cc_sum Error !!-----\n");
						info->soca->cc_delta
							 = (is_charging == true) ? cc_cap : -cc_cap;

						if((POWER_SUPPLY_STATUS_FULL == info->soca->chg_status)
						//	|| (info->soca->Ibat_ave > 200))
						|| (info->soca->Ibat_ave < info->ch_icchg*50 + 100) || (info->soca->displayed_soc<9700))
						{
							info->soca->displayed_soc += 13 * 3000 / fa_cap;
						}
						else {
							info->soca->displayed_soc
						       = info->soca->displayed_soc + info->soca->cc_delta*8/10;
						}
						
						info->soca->displayed_soc
							 = min(10000, info->soca->displayed_soc);
						info->soca->displayed_soc = max(0, info->soca->displayed_soc);

						if (info->soca->displayed_soc >= 9890) {
							info->soca->displayed_soc = 99 * 100;
						}
					}
				}
			} else {
				info->soca->full_reset_count = 0;
			}
		} else { /* discharging */
			if (info->soca->displayed_soc >= 9950) {
				if (info->soca->Ibat_ave <= -1 * RICOH619_REL1_SEL_VALUE) {
					if ((calc_ocv(info) < (get_OCV_voltage(info, 9) + (get_OCV_voltage(info, 10) - get_OCV_voltage(info, 9))*3/10))
						|| ((info->soca->soc_full - info->soca->soc) > 200)) {

						g_full_flag = 0;
						info->soca->full_reset_count = 0;
						info->soca->displayed_soc = 100 * 100;
						info->soca->status = RICOH619_SOCA_DISP;
						info->soca->last_soc = info->soca->soc;
						info->soca->soc_delta = 0;
					} else {
						info->soca->displayed_soc = 100 * 100;
					}
				} else { /* into relaxation state */
					ricoh619_read(info->dev->parent, CHGSTATE_REG, &reg_val);
					if (reg_val & 0xc0) {
						info->soca->displayed_soc = 100 * 100;
					} else {
						g_full_flag = 0;
						info->soca->full_reset_count = 0;
						info->soca->displayed_soc = 100 * 100;
						info->soca->status = RICOH619_SOCA_DISP;
						info->soca->last_soc = info->soca->soc;
						info->soca->soc_delta = 0;
					}
				}
			} else {
				g_full_flag = 0;
				info->soca->status = RICOH619_SOCA_DISP;
				info->soca->soc_delta = 0;
				info->soca->full_reset_count = 0;
				info->soca->last_soc = info->soca->soc;
			}
		}
	} else if (RICOH619_SOCA_LOW_VOL == info->soca->status) {
		if(info->soca->Ibat_ave >= 0) {
			info->soca->soc = calc_capacity(info) * 100;
			info->soca->status = RICOH619_SOCA_DISP;
			info->soca->last_soc = info->soca->soc;
			info->soca->soc_delta = 0;
		} else {
			re_cap = get_check_fuel_gauge_reg(info, RE_CAP_H_REG, RE_CAP_L_REG,
								0x7fff);
			fa_cap = get_check_fuel_gauge_reg(info, FA_CAP_H_REG, FA_CAP_L_REG,
								0x7fff);
			use_cap = fa_cap - re_cap;
			
			if (info->soca->target_use_cap == 0) {
				info->soca->re_cap_old = re_cap;
				get_target_use_cap(info);
			}
			
			if(use_cap >= info->soca->target_use_cap) {
				info->soca->displayed_soc = info->soca->displayed_soc - 100;
				info->soca->displayed_soc = max(0, info->soca->displayed_soc);
				info->soca->re_cap_old = re_cap;
			} else if (info->soca->hurry_up_flg == 1) {
				info->soca->displayed_soc = info->soca->displayed_soc - 100;
				info->soca->displayed_soc = max(0, info->soca->displayed_soc);
				info->soca->re_cap_old = re_cap;
			}
			get_target_use_cap(info);
			info->soca->soc = calc_capacity(info) * 100;
		}
	}
	else if (RICOH619_SOCA_DISP == info->soca->status) {

		info->soca->soc = calc_capacity_2(info);

		soc_round = (info->soca->soc + 50) / 100;
		last_soc_round = (info->soca->last_soc + 50) / 100;
		last_disp_round = (info->soca->displayed_soc + 50) / 100;

		info->soca->soc_delta =
			info->soca->soc_delta + (info->soca->soc - info->soca->last_soc);

		info->soca->last_soc = info->soca->soc;
		/* six case */
		if (last_disp_round == soc_round) {
			/* if SOC == DISPLAY move to stable */
			info->soca->displayed_soc = info->soca->soc ;
			info->soca->status = RICOH619_SOCA_STABLE;
			delay_flag = 1;
		} else if (info->soca->Ibat_ave > 0) {
			if ((0 == info->soca->jt_limit) || 
			(POWER_SUPPLY_STATUS_FULL != info->soca->chg_status)) {
				/* Charge */
				if (last_disp_round < soc_round) {
					/* Case 1 : Charge, Display < SOC */
					if (info->soca->soc_delta >= 100) {
						info->soca->displayed_soc
							= last_disp_round * 100 + 50;
	 					info->soca->soc_delta -= 100;
						if (info->soca->soc_delta >= 100)
		 					delay_flag = 1;
					} else {
						info->soca->displayed_soc += 25;
						disp_dec = info->soca->displayed_soc % 100;
						if ((50 <= disp_dec) && (disp_dec <= 74))
							info->soca->soc_delta = 0;
					}
					if ((info->soca->displayed_soc + 50)/100
								 >= soc_round) {
						info->soca->displayed_soc
							= info->soca->soc ;
						info->soca->status
							= RICOH619_SOCA_STABLE;
						delay_flag = 1;
					}
				} else if (last_disp_round > soc_round) {
					/* Case 2 : Charge, Display > SOC */
					if (info->soca->soc_delta >= 300) {
						info->soca->displayed_soc += 100;
						info->soca->soc_delta -= 300;
					}
					if ((info->soca->displayed_soc + 50)/100
								 <= soc_round) {
						info->soca->displayed_soc
							= info->soca->soc ;
						info->soca->status
						= RICOH619_SOCA_STABLE;
						delay_flag = 1;
					}
				}
			} else {
				info->soca->soc_delta = 0;
			}
		} else {
			/* Dis-Charge */
			if (last_disp_round > soc_round) {
				/* Case 3 : Dis-Charge, Display > SOC */
				if (info->soca->soc_delta <= -100) {
					info->soca->displayed_soc
						= last_disp_round * 100 - 75;
					info->soca->soc_delta += 100;
					if (info->soca->soc_delta <= -100)
						delay_flag = 1;
				} else {
					info->soca->displayed_soc -= 25;
					disp_dec = info->soca->displayed_soc % 100;
					if ((25 <= disp_dec) && (disp_dec <= 49))
						info->soca->soc_delta = 0;
				}
				if ((info->soca->displayed_soc + 50)/100
							 <= soc_round) {
					info->soca->displayed_soc
						= info->soca->soc ;
					info->soca->status
						= RICOH619_SOCA_STABLE;
					delay_flag = 1;
				}
			} else if (last_disp_round < soc_round) {
				/* Case 4 : Dis-Charge, Display < SOC */
				if (info->soca->soc_delta <= -300) {
					info->soca->displayed_soc -= 100;
					info->soca->soc_delta += 300;
				}
				if ((info->soca->displayed_soc + 50)/100
							 >= soc_round) {
					info->soca->displayed_soc
						= info->soca->soc ;
					info->soca->status
						= RICOH619_SOCA_STABLE;
					delay_flag = 1;
				}
			}
		}
	} else if (RICOH619_SOCA_UNSTABLE == info->soca->status) {
		/* caluc 95% ocv */
		temp_ocv = get_OCV_voltage(info, 10) -
					(get_OCV_voltage(info, 10) - get_OCV_voltage(info, 9))/2;
		
		if(g_full_flag == 1){	/* for issue 1 solution start*/
			info->soca->status = RICOH619_SOCA_FULL;
			err = reset_FG_process(info);
			if (err < 0)
				dev_err(info->dev, "Error in writing the control register\n");
			
			goto end_flow;
		}else if ((POWER_SUPPLY_STATUS_FULL == info->soca->chg_status)
			&& (calc_ocv(info) > temp_ocv)) {
			info->soca->status = RICOH619_SOCA_FULL;
			g_full_flag = 0;
			err = reset_FG_process(info);
			if (err < 0)
				dev_err(info->dev, "Error in writing the control register\n");
			goto end_flow;
		} else if (info->soca->Ibat_ave >= -20) {
			/* for issue1 solution end */
			/* check Full state or not*/
			if ((calc_ocv(info) > (get_OCV_voltage(info, 9) + (get_OCV_voltage(info, 10) - get_OCV_voltage(info, 9))*7/10))
				|| (POWER_SUPPLY_STATUS_FULL == info->soca->chg_status)
				|| (info->soca->displayed_soc > 9850))
			{
				info->soca->status = RICOH619_SOCA_FULL;
				g_full_flag = 0;
				err = reset_FG_process(info);
				if (err < 0)
					dev_err(info->dev, "Error in writing the control register\n");
				goto end_flow;
			} else if ((calc_ocv(info) > (get_OCV_voltage(info, 9)))
				&& (info->soca->Ibat_ave < 300))
			{
				info->soca->status = RICOH619_SOCA_FULL;
				g_full_flag = 0;
				err = reset_FG_process(info);
				if (err < 0)
					dev_err(info->dev, "Error in writing the control register\n");				
				goto end_flow;
			}
		}

		err = ricoh619_read(info->dev->parent, PSWR_REG, &val);
		val &= 0x7f;
		info->soca->soc = val * 100;
		if (err < 0) {
			dev_err(info->dev,
				 "Error in reading PSWR_REG %d\n", err);
			info->soca->soc
				 = calc_capacity(info) * 100;
		}

		err = calc_capacity_in_period(info, &cc_cap,
						 &is_charging, false);
		if (err < 0)
			dev_err(info->dev, "Read cc_sum Error !!-----\n");

		info->soca->cc_delta
			 = (is_charging == true) ? cc_cap : -cc_cap;

		displayed_soc_temp
		       = info->soca->soc + info->soca->cc_delta;
		if (displayed_soc_temp < 0)
			displayed_soc_temp = 0;
		displayed_soc_temp
			 = min(9850, displayed_soc_temp);
		displayed_soc_temp = max(0, displayed_soc_temp);

		info->soca->displayed_soc = displayed_soc_temp;

	} else if (RICOH619_SOCA_FG_RESET == info->soca->status) {
		/* No update */
	} else if (RICOH619_SOCA_START == info->soca->status) {

		err = measure_Ibatt_FG(info, &Ibat);
		err = measure_vbatt_FG(info, &Vbat);
		err = measure_vsys_ADC(info, &Vsys);

		info->soca->Ibat_ave = Ibat;
		info->soca->Vbat_ave = Vbat;
		info->soca->Vsys_ave = Vsys;

		err = check_jeita_status(info, &is_jeita_updated);
		is_jeita_updated = false;
		if (err < 0) {
			dev_err(info->dev, "Error in updating JEITA %d\n", err);
		}
		err = ricoh619_read(info->dev->parent, PSWR_REG, &val);
		val &= 0x7f;
		if (info->first_pwon) {
			info->soca->soc = calc_capacity(info) * 100;
			val = (info->soca->soc + 50)/100;
			val &= 0x7f;
			err = ricoh619_write(info->dev->parent, PSWR_REG, val);
			if (err < 0)
				dev_err(info->dev, "Error in writing PSWR_REG\n");
			g_soc = val;

			if ((info->soca->soc == 0) && (calc_ocv(info)
					< get_OCV_voltage(info, 0))) {
				info->soca->displayed_soc = 0;
				info->soca->status = RICOH619_SOCA_ZERO;
			} else {
				if (0 == info->soca->jt_limit) {
					check_charge_status_2(info, info->soca->soc);
				} else {
					info->soca->displayed_soc = info->soca->soc;
				}
				if (Ibat < 0) {
					if (info->soca->displayed_soc < 300) {
						info->soca->target_use_cap = 0;
						info->soca->status = RICOH619_SOCA_LOW_VOL;
					} else {
						if ((info->fg_poff_vbat != 0)
						      && (Vbat < info->fg_poff_vbat * 1000) ){
							  info->soca->target_use_cap = 0;
							  info->soca->status = RICOH619_SOCA_LOW_VOL;
						  } else { 
							  info->soca->status = RICOH619_SOCA_UNSTABLE;
						  }
					}
				} else {
					info->soca->status = RICOH619_SOCA_UNSTABLE;
				}
			}
		} else if (g_fg_on_mode && (val == 0x7f)) {
			info->soca->soc = calc_capacity(info) * 100;
			if ((info->soca->soc == 0) && (calc_ocv(info)
					< get_OCV_voltage(info, 0))) {
				info->soca->displayed_soc = 0;
				info->soca->status = RICOH619_SOCA_ZERO;
			} else {
				if (0 == info->soca->jt_limit) {
					check_charge_status_2(info, info->soca->soc);
				} else {
					info->soca->displayed_soc = info->soca->soc;
				}
				info->soca->last_soc = info->soca->soc;
				info->soca->status = RICOH619_SOCA_STABLE;
			}
		} else {
			info->soca->soc = val * 100;
			if (err < 0) {
				dev_err(info->dev,
					 "Error in reading PSWR_REG %d\n", err);
				info->soca->soc
					 = calc_capacity(info) * 100;
			}

			err = calc_capacity_in_period(info, &cc_cap,
							 &is_charging, false);
			if (err < 0)
				dev_err(info->dev, "Read cc_sum Error !!-----\n");

			info->soca->cc_delta
				 = (is_charging == true) ? cc_cap : -cc_cap;
			if (calc_ocv(info) < get_OCV_voltage(info, 0)) {
				info->soca->displayed_soc = 0;
				info->soca->status = RICOH619_SOCA_ZERO;
			} else {
				displayed_soc_temp
				       = info->soca->soc + info->soca->cc_delta;
				if (displayed_soc_temp < 0)
					displayed_soc_temp = 0;
				displayed_soc_temp
					 = min(10000, displayed_soc_temp);
				displayed_soc_temp = max(0, displayed_soc_temp);
				if (0 == info->soca->jt_limit) {
					check_charge_status_2(info, displayed_soc_temp);
				} else {
					info->soca->displayed_soc = displayed_soc_temp;
				}
				info->soca->last_soc = calc_capacity(info) * 100;
				if (Ibat < 0) {
					if (info->soca->displayed_soc < 300) {
						info->soca->target_use_cap = 0;
						info->soca->status = RICOH619_SOCA_LOW_VOL;
					} else {
						if ((info->fg_poff_vbat != 0)
						      && (Vbat < info->fg_poff_vbat * 1000)){
							  info->soca->target_use_cap = 0;
							  info->soca->status = RICOH619_SOCA_LOW_VOL;
						  } else { 
							  info->soca->status = RICOH619_SOCA_UNSTABLE;
						  }
					}
				} else {
					if(info->soca->displayed_soc >= 9850)
					{
						info->soca->displayed_soc = 10000;
						info->chg_complete_tm_ov_flag = 1;
					}
					info->soca->status = RICOH619_SOCA_UNSTABLE;
				}
			}
		}
	} else if (RICOH619_SOCA_ZERO == info->soca->status) {
		if (calc_ocv(info) > get_OCV_voltage(info, 0)) {
			err = reset_FG_process(info);
			if (err < 0)
				dev_err(info->dev, "Error in writing the control register\n");
			info->soca->last_soc = calc_capacity_2(info);
			info->soca->status = RICOH619_SOCA_STABLE;
		}
		info->soca->displayed_soc = 0;
	}
end_flow:
	/* keep DSOC = 1 when Vbat is over 3.4V*/
	if( info->fg_poff_vbat != 0) {
		if (info->soca->zero_flg == 1) {
			if(info->soca->Ibat_ave >= 0) {
				info->soca->zero_flg = 0;
			}
			info->soca->displayed_soc = 0;
		} else if (info->soca->displayed_soc < 50) {
			if (info->soca->Vbat_ave < 2000*1000) { /* error value */
				info->soca->displayed_soc = 100;
			} else if (info->soca->Vbat_ave < info->fg_poff_vbat*1000) {
				info->soca->displayed_soc = 0;
				info->soca->zero_flg = 1;
			} else {
				info->soca->displayed_soc = 100;
			}
		}
	}

	if (g_fg_on_mode
		 && (info->soca->status == RICOH619_SOCA_STABLE)) {
		err = ricoh619_write(info->dev->parent, PSWR_REG, 0x7f);
		if (err < 0)
			dev_err(info->dev, "Error in writing PSWR_REG\n");
		g_soc = 0x7F;
		err = calc_capacity_in_period(info, &cc_cap,
							&is_charging, true);
		if (err < 0)
			dev_err(info->dev, "Read cc_sum Error !!-----\n");

	} else if (RICOH619_SOCA_UNSTABLE != info->soca->status) {
		if ((info->soca->displayed_soc + 50) / 100 <= 1) {
			val = 1;
		} else {
			val = (info->soca->displayed_soc + 50)/100;
			val &= 0x7f;
		}
		err = ricoh619_write(info->dev->parent, PSWR_REG, val);
		if (err < 0)
			dev_err(info->dev, "Error in writing PSWR_REG\n");

		g_soc = val;

		err = calc_capacity_in_period(info, &cc_cap,
							 &is_charging, true);
		if (err < 0)
			dev_err(info->dev, "Read cc_sum Error !!-----\n");
	}
	
	RICOH_FG_DBG("PMU: ------- STATUS= %d: IBAT= %d: VSYS= %d: VBAT= %d: DSOC= %d: RSOC= %d: -------\n",
	       info->soca->status, info->soca->Ibat_ave, info->soca->Vsys_ave, info->soca->Vbat_ave,
	info->soca->displayed_soc, info->soca->soc);

#ifdef DISABLE_CHARGER_TIMER
	/* clear charger timer */
	if ( info->soca->chg_status == POWER_SUPPLY_STATUS_CHARGING ) {
		err = ricoh619_read(info->dev->parent, TIMSET_REG, &val);
		if (err < 0)
			dev_err(info->dev,
			"Error in read TIMSET_REG%d\n", err);
		/* to check bit 0-1 */
		val2 = val & 0x03;

		if (val2 == 0x02){
			/* set rapid timer 240 -> 300 */
			err = ricoh619_set_bits(info->dev->parent, TIMSET_REG, 0x03);
			if (err < 0) {
				dev_err(info->dev, "Error in writing the control register\n");
			}
		} else {
			/* set rapid timer 300 -> 240 */
			err = ricoh619_clr_bits(info->dev->parent, TIMSET_REG, 0x01);
			err = ricoh619_set_bits(info->dev->parent, TIMSET_REG, 0x02);
			if (err < 0) {
				dev_err(info->dev, "Error in writing the control register\n");
			}
		}
	}
#endif

	if (0 == info->soca->ready_fg)
		queue_delayed_work(info->monitor_wqueue, &info->displayed_work,
					 RICOH619_FG_RESET_TIME * HZ);
	else if (delay_flag == 1)
		queue_delayed_work(info->monitor_wqueue, &info->displayed_work,
					 RICOH619_DELAY_TIME * HZ);
	else if (RICOH619_SOCA_DISP == info->soca->status)
		queue_delayed_work(info->monitor_wqueue, &info->displayed_work,
					 RICOH619_DISPLAY_UPDATE_TIME * HZ);
	else if (info->soca->hurry_up_flg == 1)
		queue_delayed_work(info->monitor_wqueue, &info->displayed_work,
					 RICOH619_LOW_VOL_DOWN_TIME * HZ);
	else
		queue_delayed_work(info->monitor_wqueue, &info->displayed_work,
					 RICOH619_DISPLAY_UPDATE_TIME * HZ);

	mutex_unlock(&info->lock);

	if((true == is_jeita_updated)
	|| (info->soca->last_displayed_soc/100 != (info->soca->displayed_soc+50)/100))
		power_supply_changed(&info->battery);

	info->soca->last_displayed_soc = info->soca->displayed_soc+50;

	if ((info->soca->displayed_soc >= 9850) && (info->soca->Ibat_ave > -20) && (info->capacity < 100)
		&& (info->soca->chg_status == POWER_SUPPLY_STATUS_CHARGING))
	{
		if(info->chg_complete_rd_flag == 0)
		{
			info->chg_complete_rd_flag = 1;
			info->chg_complete_rd_cnt = 0;
			queue_delayed_work(info->monitor_wqueue, &info->charge_complete_ready, 0);
		}
	}
	else
	{
		info->chg_complete_rd_flag = 0;
	}

	if(info->chg_complete_tm_ov_flag == 1)
	{
		if(info->soca->displayed_soc < 9850 || info->soca->Ibat_ave < -20)
		{
			info->chg_complete_tm_ov_flag = 0;
			power_supply_changed(&info->battery);
		}
	}
	return;
}

static void ricoh619_stable_charge_countdown_work(struct work_struct *work)
{
	int ret;
	int max = 0;
	int min = 100;
	int i;
	struct ricoh619_battery_info *info = container_of(work,
		struct ricoh619_battery_info, charge_stable_work.work);

	if (info->entry_factory_mode)
		return;

	mutex_lock(&info->lock);
	if (RICOH619_SOCA_FG_RESET == info->soca->status)
		info->soca->ready_fg = 1;

	if (2 <= info->soca->stable_count) {
		if (3 == info->soca->stable_count
			&& RICOH619_SOCA_FG_RESET == info->soca->status) {
			ret = reset_FG_process(info);
			if (ret < 0)
				dev_err(info->dev, "Error in writing the control register\n");
		}
		info->soca->stable_count = info->soca->stable_count - 1;
		queue_delayed_work(info->monitor_wqueue,
					 &info->charge_stable_work,
					 RICOH619_FG_STABLE_TIME * HZ / 10);
	} else if (0 >= info->soca->stable_count) {
		/* Finished queue, ignore */
	} else if (1 == info->soca->stable_count) {
		if (RICOH619_SOCA_UNSTABLE == info->soca->status) {
			/* Judge if FG need reset or Not */
			info->soca->soc = calc_capacity(info) * 100;
			if (info->chg_ctr != 0) {
				queue_delayed_work(info->monitor_wqueue,
					 &info->charge_stable_work,
					 RICOH619_FG_STABLE_TIME * HZ / 10);
				mutex_unlock(&info->lock);
				return;
			}
			/* Do reset setting */
			ret = reset_FG_process(info);
			if (ret < 0)
				dev_err(info->dev, "Error in writing the control register\n");

			info->soca->status = RICOH619_SOCA_FG_RESET;

			/* Delay for addition Reset Time (6s) */
			queue_delayed_work(info->monitor_wqueue,
					 &info->charge_stable_work,
					 RICOH619_FG_RESET_TIME*HZ);
		} else if (RICOH619_SOCA_FG_RESET == info->soca->status) {
			info->soca->reset_soc[2] = info->soca->reset_soc[1];
			info->soca->reset_soc[1] = info->soca->reset_soc[0];
			info->soca->reset_soc[0] = calc_capacity(info) * 100;
			info->soca->reset_count++;

			if (info->soca->reset_count > 10) {
				/* Reset finished; */
				info->soca->soc = info->soca->reset_soc[0];
				info->soca->stable_count = 0;
				goto adjust;
			}

			for (i = 0; i < 3; i++) {
				if (max < info->soca->reset_soc[i]/100)
					max = info->soca->reset_soc[i]/100;
				if (min > info->soca->reset_soc[i]/100)
					min = info->soca->reset_soc[i]/100;
			}

			if ((info->soca->reset_count > 3) && ((max - min)
					< RICOH619_MAX_RESET_SOC_DIFF)) {
				/* Reset finished; */
				info->soca->soc = info->soca->reset_soc[0];
				info->soca->stable_count = 0;
				goto adjust;
			} else {
				/* Do reset setting */
				ret = reset_FG_process(info);
				if (ret < 0)
					dev_err(info->dev, "Error in writing the control register\n");

				/* Delay for addition Reset Time (6s) */
				queue_delayed_work(info->monitor_wqueue,
						 &info->charge_stable_work,
						 RICOH619_FG_RESET_TIME*HZ);
			}
		/* Finished queue From now, select FG as result; */
		} else if (RICOH619_SOCA_START == info->soca->status) {
			/* Normal condition */
		} else { /* other state ZERO/DISP/STABLE */
			info->soca->stable_count = 0;
		}

		mutex_unlock(&info->lock);
		return;

adjust:
		info->soca->last_soc = info->soca->soc;
		info->soca->status = RICOH619_SOCA_DISP;
		info->soca->soc_delta = 0;

	}
	mutex_unlock(&info->lock);
	return;
}

static void ricoh619_charge_monitor_work(struct work_struct *work)
{
	struct ricoh619_battery_info *info = container_of(work,
		struct ricoh619_battery_info, charge_monitor_work.work);

	get_power_supply_status(info);

	if (POWER_SUPPLY_STATUS_DISCHARGING == info->soca->chg_status
		|| POWER_SUPPLY_STATUS_NOT_CHARGING == info->soca->chg_status) {
		switch (info->soca->dischg_state) {
		case	0:
			info->soca->dischg_state = 1;
			break;
		case	1:
			info->soca->dischg_state = 2;
			break;
	
		case	2:
		default:
			break;
		}
	} else {
		info->soca->dischg_state = 0;
	}

	queue_delayed_work(info->monitor_wqueue, &info->charge_monitor_work,
					 RICOH619_CHARGE_MONITOR_TIME * HZ);

	return;
}

static void ricoh619_get_charge_work(struct work_struct *work)
{
	struct ricoh619_battery_info *info = container_of(work,
		struct ricoh619_battery_info, get_charge_work.work);

	int Vbat_temp, Vsys_temp, Ibat_temp;
	int Vbat_sort[RICOH619_GET_CHARGE_NUM];
	int Vsys_sort[RICOH619_GET_CHARGE_NUM];
	int Ibat_sort[RICOH619_GET_CHARGE_NUM];
	int i, j;
	int ret;

	mutex_lock(&info->lock);

	for (i = RICOH619_GET_CHARGE_NUM-1; i > 0; i--) {
		if (0 == info->soca->chg_count) {
			info->soca->Vbat[i] = 0;
			info->soca->Vsys[i] = 0;
			info->soca->Ibat[i] = 0;
		} else {
			info->soca->Vbat[i] = info->soca->Vbat[i-1];
			info->soca->Vsys[i] = info->soca->Vsys[i-1];
			info->soca->Ibat[i] = info->soca->Ibat[i-1];
		}
	}

	ret = measure_vbatt_FG(info, &info->soca->Vbat[0]);
	ret = measure_vsys_ADC(info, &info->soca->Vsys[0]);
	ret = measure_Ibatt_FG(info, &info->soca->Ibat[0]);

	info->soca->chg_count++;

	if (RICOH619_GET_CHARGE_NUM != info->soca->chg_count) {
		queue_delayed_work(info->monitor_wqueue, &info->get_charge_work,
					 RICOH619_CHARGE_CALC_TIME * HZ);
		mutex_unlock(&info->lock);
		return ;
	}

	for (i = 0; i < RICOH619_GET_CHARGE_NUM; i++) {
		Vbat_sort[i] = info->soca->Vbat[i];
		Vsys_sort[i] = info->soca->Vsys[i];
		Ibat_sort[i] = info->soca->Ibat[i];
	}

	Vbat_temp = 0;
	Vsys_temp = 0;
	Ibat_temp = 0;
	for (i = 0; i < RICOH619_GET_CHARGE_NUM - 1; i++) {
		for (j = RICOH619_GET_CHARGE_NUM - 1; j > i; j--) {
			if (Vbat_sort[j - 1] > Vbat_sort[j]) {
				Vbat_temp = Vbat_sort[j];
				Vbat_sort[j] = Vbat_sort[j - 1];
				Vbat_sort[j - 1] = Vbat_temp;
			}
			if (Vsys_sort[j - 1] > Vsys_sort[j]) {
				Vsys_temp = Vsys_sort[j];
				Vsys_sort[j] = Vsys_sort[j - 1];
				Vsys_sort[j - 1] = Vsys_temp;
			}
			if (Ibat_sort[j - 1] > Ibat_sort[j]) {
				Ibat_temp = Ibat_sort[j];
				Ibat_sort[j] = Ibat_sort[j - 1];
				Ibat_sort[j - 1] = Ibat_temp;
			}
		}
	}

	Vbat_temp = 0;
	Vsys_temp = 0;
	Ibat_temp = 0;
	for (i = 3; i < RICOH619_GET_CHARGE_NUM-3; i++) {
		Vbat_temp = Vbat_temp + Vbat_sort[i];
		Vsys_temp = Vsys_temp + Vsys_sort[i];
		Ibat_temp = Ibat_temp + Ibat_sort[i];
	}
	Vbat_temp = Vbat_temp / (RICOH619_GET_CHARGE_NUM - 6);
	Vsys_temp = Vsys_temp / (RICOH619_GET_CHARGE_NUM - 6);
	Ibat_temp = Ibat_temp / (RICOH619_GET_CHARGE_NUM - 6);

	if (0 == info->soca->chg_count) {
		queue_delayed_work(info->monitor_wqueue, &info->get_charge_work,
				 RICOH619_CHARGE_UPDATE_TIME * HZ);
		mutex_unlock(&info->lock);
		return;
	} else {
		info->soca->Vbat_ave = Vbat_temp;
		info->soca->Vsys_ave = Vsys_temp;
		info->soca->Ibat_ave = Ibat_temp;
	}

	info->soca->chg_count = 0;
	queue_delayed_work(info->monitor_wqueue, &info->get_charge_work,
				 RICOH619_CHARGE_UPDATE_TIME * HZ);
	mutex_unlock(&info->lock);
	return;
}

/* Initial setting of FuelGauge SOCA function */
static int ricoh619_init_fgsoca(struct ricoh619_battery_info *info)
{
	int i;
	int err;
	uint8_t val;
	int cc_cap = 0;
	bool is_charging = true;

	for (i = 0; i <= 10; i = i+1) {
		info->soca->ocv_table[i] = get_OCV_voltage(info, i);
		RICOH_FG_DBG("PMU: %s : * %d0%% voltage = %d uV\n",
				 __func__, i, info->soca->ocv_table[i]);
	}

	for (i = 0; i < 3; i = i+1)
		info->soca->reset_soc[i] = 0;
	info->soca->reset_count = 0;

	if (info->first_pwon) {

		err = ricoh619_read(info->dev->parent, CHGISET_REG, &val);
		if (err < 0)
			dev_err(info->dev,
			"Error in read CHGISET_REG%d\n", err);

		err = ricoh619_write(info->dev->parent, CHGISET_REG, 0);
		if (err < 0)
			dev_err(info->dev,
			"Error in writing CHGISET_REG%d\n", err);
		msleep(1000);

		if (!info->entry_factory_mode) {
			err = ricoh619_write(info->dev->parent,
							FG_CTRL_REG, 0x51);
			if (err < 0)
				dev_err(info->dev, "Error in writing the control register\n");
		}

		err = calc_capacity_in_period(info, &cc_cap, &is_charging, true);

		msleep(6000);

		err = ricoh619_write(info->dev->parent, CHGISET_REG, val);
		if (err < 0)
			dev_err(info->dev,
			"Error in writing CHGISET_REG%d\n", err);
	}
	
	/* Rbat : Transfer */
	info->soca->Rbat = get_OCV_init_Data(info, 12) * 1000 / 512
							 * 5000 / 4095;
	info->soca->n_cap = get_OCV_init_Data(info, 11);


	info->soca->displayed_soc = 0;
	info->soca->last_displayed_soc = 0;
	info->soca->suspend_soc = 0;
	info->soca->ready_fg = 0;
	info->soca->soc_delta = 0;
	info->soca->full_reset_count = 0;
	info->soca->soc_full = 0;
	info->soca->fc_cap = 0;
	info->soca->status = RICOH619_SOCA_START;
	/* stable count down 11->2, 1: reset; 0: Finished; */
	info->soca->stable_count = 11;
	info->soca->chg_cmp_times = 0;
	info->soca->dischg_state = 0;
	info->soca->Vbat_ave = 0;
	info->soca->Vbat_old = 0;
	info->soca->Vsys_ave = 0;
	info->soca->Ibat_ave = 0;
	info->soca->chg_count = 0;
	info->soca->target_use_cap = 0;
	info->soca->hurry_up_flg = 0;
	info->soca->re_cap_old = 0;
	info->soca->jt_limit = 0;
	info->soca->zero_flg = 0;
	info->soca->cc_cap_offset = 0;

	for (i = 0; i < 11; i++) {
		info->soca->ocv_table_low[i] = 0;
	}

	for (i = 0; i < RICOH619_GET_CHARGE_NUM; i++) {
		info->soca->Vbat[i] = 0;
		info->soca->Vsys[i] = 0;
		info->soca->Ibat[i] = 0;
	}

	g_full_flag = 0;
	
#ifdef ENABLE_FG_KEEP_ON_MODE
	g_fg_on_mode = 1;
#else
	g_fg_on_mode = 0;
#endif

	/* Start first Display job */
	queue_delayed_work(info->monitor_wqueue, &info->displayed_work,
						   RICOH619_FG_RESET_TIME*HZ);

	/* Start first Waiting stable job */
	queue_delayed_work(info->monitor_wqueue, &info->charge_stable_work,
		   RICOH619_FG_STABLE_TIME*HZ/10);

	queue_delayed_work(info->monitor_wqueue, &info->charge_monitor_work,
					 RICOH619_CHARGE_MONITOR_TIME * HZ);

	queue_delayed_work(info->monitor_wqueue, &info->get_charge_work,
					 RICOH619_CHARGE_MONITOR_TIME * HZ);
	if (info->jt_en) {
		if (info->jt_hw_sw) {
			/* Enable JEITA function supported by H/W */
			err = ricoh619_set_bits(info->dev->parent, CHGCTL1_REG, 0x04);
			if (err < 0)
				dev_err(info->dev, "Error in writing the control register\n");
		} else {
		 	/* Disable JEITA function supported by H/W */
			err = ricoh619_clr_bits(info->dev->parent, CHGCTL1_REG, 0x04);
			if (err < 0)
				dev_err(info->dev, "Error in writing the control register\n");
			queue_delayed_work(info->monitor_wqueue, &info->jeita_work,
						 	 RICOH619_FG_RESET_TIME * HZ);
		}
	} else {
		/* Disable JEITA function supported by H/W */
		err = ricoh619_clr_bits(info->dev->parent, CHGCTL1_REG, 0x04);
		if (err < 0)
			dev_err(info->dev, "Error in writing the control register\n");
	}

	RICOH_FG_DBG("PMU: %s : * Rbat = %d mOhm   n_cap = %d mAH\n",
			 __func__, info->soca->Rbat, info->soca->n_cap);
	return 1;
}
#endif

static void ricoh619_charging_complete_work(struct work_struct *work)
{
	struct ricoh619_battery_info *info = container_of(work,
		struct ricoh619_battery_info, charge_complete_ready.work);

	uint8_t time_ov_flag;
	RICOH_FG_DBG("PMU: %s\n", __func__);
	RICOH_FG_DBG("info->chg_complete_rd_cnt = %d\n", info->chg_complete_rd_cnt);
	RICOH_FG_DBG("info->chg_complete_rd_flag = %d\n", info->chg_complete_rd_flag);
	RICOH_FG_DBG("info->chg_complete_tm_ov_flag = %d\n", info->chg_complete_tm_ov_flag);
	
	if(info->chg_complete_rd_flag == 1)
	{
		// start chg 99per to 100per timer
		time_ov_flag = 0;
		info->chg_complete_rd_flag = 2;
		info->chg_complete_tm_ov_flag = 0;
	}
	else
	{
		if(info->capacity == 100)
		{
			// battery arriver to 100% earlier than time ov
			time_ov_flag = 1;
			info->chg_complete_rd_cnt = 0;
			info->chg_complete_tm_ov_flag = 1;
		}
		else if(info->chg_complete_rd_cnt > RICOH619_TIME_CHG_COUNT)
		{
			// chg timer ov before cap arrive to 100%
			time_ov_flag = 1;
			info->chg_complete_tm_ov_flag = 1;
			info->chg_complete_rd_cnt = 0;
			info->soca->status = RICOH619_SOCA_FULL;
			power_supply_changed(&info->battery);
		}
		else
		{
			time_ov_flag = 0;
			info->chg_complete_tm_ov_flag = 0;
		}
	}

	if ((time_ov_flag == 0) && (info->soca->chg_status == POWER_SUPPLY_STATUS_CHARGING))
	{
		info->chg_complete_rd_cnt++;
		queue_delayed_work(info->monitor_wqueue, &info->charge_complete_ready, 
			RICOH619_TIME_CHG_STEP);
	}
	else
	{
		info->chg_complete_rd_flag = 0;
	}

	RICOH_FG_DBG("PMU2: %s return\n", __func__);
	RICOH_FG_DBG("info->chg_complete_rd_cnt = %d\n", info->chg_complete_rd_cnt);
	RICOH_FG_DBG("info->chg_complete_rd_flag = %d\n", info->chg_complete_rd_flag);
	RICOH_FG_DBG("info->chg_complete_tm_ov_flag = %d\n", info->chg_complete_tm_ov_flag);
	RICOH_FG_DBG("time_ov_flag = %d\n", time_ov_flag);

}
static void ricoh619_changed_work(struct work_struct *work)
{
	struct ricoh619_battery_info *info = container_of(work,
		struct ricoh619_battery_info, changed_work.work);

	RICOH_FG_DBG("PMU: %s\n", __func__);
	power_supply_changed(&info->battery);

	return;
}

static int check_jeita_status(struct ricoh619_battery_info *info, bool *is_jeita_updated)
/*  JEITA Parameter settings
*
*          VCHG  
*            |     
* jt_vfchg_h~+~~~~~~~~~~~~~~~~~~~+
*            |                   |
* jt_vfchg_l-| - - - - - - - - - +~~~~~~~~~~+
*            |    Charge area    +          |               
*  -------0--+-------------------+----------+--- Temp
*            !                   +
*          ICHG     
*            |                   +
*  jt_ichg_h-+ - -+~~~~~~~~~~~~~~+~~~~~~~~~~+
*            +    |              +          |
*  jt_ichg_l-+~~~~+   Charge area           |
*            |    +              +          |
*         0--+----+--------------+----------+--- Temp
*            0   jt_temp_l      jt_temp_h   55
*/
{
	int temp;
	int err = 0;
	int vfchg;
	uint8_t chgiset_org;
	uint8_t batset2_org;
	uint8_t set_vchg_h, set_vchg_l;
	uint8_t set_ichg_h, set_ichg_l;

	*is_jeita_updated = false;
	
	/* No execute if JEITA disabled */
	if (!info->jt_en || info->jt_hw_sw)
		return 0;

	/* Check FG Reset */
	if (info->soca->ready_fg) {
		temp = get_battery_temp_2(info) / 10;
	} else {
		RICOH_FG_DBG(KERN_INFO "JEITA: %s *** cannot update by resetting FG ******\n", __func__);
		goto out;
	}

	/* Read BATSET2 */
	err = ricoh619_read(info->dev->parent, BATSET2_REG, &batset2_org);
	if (err < 0) {
		dev_err(info->dev, "Error in readng the battery setting register\n");
		goto out;
	}
	vfchg = (batset2_org & 0x70) >> 4;
	batset2_org &= 0x8F;
	
	/* Read CHGISET */
	err = ricoh619_read(info->dev->parent, CHGISET_REG, &chgiset_org);
	if (err < 0) {
		dev_err(info->dev, "Error in readng the chrage setting register\n");
		goto out;
	}
	chgiset_org &= 0xC0;

	set_ichg_h = (uint8_t)(chgiset_org | info->jt_ichg_h);
	set_ichg_l = (uint8_t)(chgiset_org | info->jt_ichg_l);
		
	set_vchg_h = (uint8_t)((info->jt_vfchg_h << 4) | batset2_org);
	set_vchg_l = (uint8_t)((info->jt_vfchg_l << 4) | batset2_org);

	RICOH_FG_DBG(KERN_INFO "PMU: %s *** Temperature: %d, vfchg: %d, SW status: %d, chg_status: %d ******\n",
		 __func__, temp, vfchg, info->soca->status, info->soca->chg_status);

	if (temp <= 0 || 55 <= temp) {
		/* 1st and 5th temperature ranges (~0, 55~) */
		RICOH_FG_DBG(KERN_INFO "PMU: %s *** Temp(%d) is out of 0-55 ******\n", __func__, temp);
		err = ricoh619_clr_bits(info->dev->parent, CHGCTL1_REG, 0x03);
		if (err < 0) {
			dev_err(info->dev, "Error in writing the control register\n");
			goto out;
		}
		info->soca->jt_limit = 0;
		*is_jeita_updated = true;
	} else if (temp < info->jt_temp_l) {
		/* 2nd temperature range (0~12) */
		if (vfchg != info->jt_vfchg_h) {
			RICOH_FG_DBG(KERN_INFO "PMU: %s *** 0<Temp<12, update to vfchg=%d ******\n", 
									__func__, info->jt_vfchg_h);
			err = ricoh619_clr_bits(info->dev->parent, CHGCTL1_REG, 0x03);
			if (err < 0) {
				dev_err(info->dev, "Error in writing the control register\n");
				goto out;
			}

			/* set VFCHG/VRCHG */
			err = ricoh619_write(info->dev->parent,
							 BATSET2_REG, set_vchg_h);
			if (err < 0) {
				dev_err(info->dev, "Error in writing the battery setting register\n");
				goto out;
			}
			info->soca->jt_limit = 0;
			*is_jeita_updated = true;
		} else
			RICOH_FG_DBG(KERN_INFO "PMU: %s *** 0<Temp<50, already set vfchg=%d, so no need to update ******\n",
					__func__, info->jt_vfchg_h);

		/* set ICHG */
		err = ricoh619_write(info->dev->parent, CHGISET_REG, set_ichg_l);
		if (err < 0) {
			dev_err(info->dev, "Error in writing the battery setting register\n");
			goto out;
		}
		err = ricoh619_set_bits(info->dev->parent, CHGCTL1_REG, 0x03);
		if (err < 0) {
			dev_err(info->dev, "Error in writing the control register\n");
			goto out;
		}
	} else if (temp < info->jt_temp_h) {
		/* 3rd temperature range (12~50) */
		if (vfchg != info->jt_vfchg_h) {
			RICOH_FG_DBG(KERN_INFO "PMU: %s *** 12<Temp<50, update to vfchg==%d ******\n", __func__, info->jt_vfchg_h);

			err = ricoh619_clr_bits(info->dev->parent, CHGCTL1_REG, 0x03);
			if (err < 0) {
				dev_err(info->dev, "Error in writing the control register\n");
				goto out;
			}
			/* set VFCHG/VRCHG */
			err = ricoh619_write(info->dev->parent,
							 BATSET2_REG, set_vchg_h);
			if (err < 0) {
				dev_err(info->dev, "Error in writing the battery setting register\n");
				goto out;
			}
			info->soca->jt_limit = 0;
			*is_jeita_updated = true;
		} else
			RICOH_FG_DBG(KERN_INFO "PMU: %s *** 12<Temp<50, already set vfchg==%d, so no need to update ******\n", 
					__func__, info->jt_vfchg_h);
		
		/* set ICHG */
		err = ricoh619_write(info->dev->parent, CHGISET_REG, set_ichg_h);
		if (err < 0) {
			dev_err(info->dev, "Error in writing the battery setting register\n");
			goto out;
		}
		err = ricoh619_set_bits(info->dev->parent, CHGCTL1_REG, 0x03);
		if (err < 0) {
			dev_err(info->dev, "Error in writing the control register\n");
			goto out;
		}
	} else if (temp < 55) {
		/* 4th temperature range (50~55) */
		if (vfchg != info->jt_vfchg_l) {
			RICOH_FG_DBG(KERN_INFO "PMU: %s *** 50<Temp<55, update to vfchg==%d ******\n", __func__, info->jt_vfchg_l);
			
			err = ricoh619_clr_bits(info->dev->parent, CHGCTL1_REG, 0x03);
			if (err < 0) {
				dev_err(info->dev, "Error in writing the control register\n");
				goto out;
			}
			/* set VFCHG/VRCHG */
			err = ricoh619_write(info->dev->parent,
							 BATSET2_REG, set_vchg_l);
			if (err < 0) {
				dev_err(info->dev, "Error in writing the battery setting register\n");
				goto out;
			}
			info->soca->jt_limit = 1;
			*is_jeita_updated = true;
		} else
			RICOH_FG_DBG(KERN_INFO "JEITA: %s *** 50<Temp<55, already set vfchg==%d, so no need to update ******\n", 
					__func__, info->jt_vfchg_l);

		/* set ICHG */
		err = ricoh619_write(info->dev->parent, CHGISET_REG, set_ichg_h);
		if (err < 0) {
			dev_err(info->dev, "Error in writing the battery setting register\n");
			goto out;
		}
		err = ricoh619_set_bits(info->dev->parent, CHGCTL1_REG, 0x03);
		if (err < 0) {
			dev_err(info->dev, "Error in writing the control register\n");
			goto out;
		}
	}

	get_power_supply_status(info);
	RICOH_FG_DBG(KERN_INFO "PMU: %s *** Hope updating value in this timing after checking jeita, chg_status: %d, is_jeita_updated: %d ******\n",
		 __func__, info->soca->chg_status, *is_jeita_updated);

	return 0;
	
out:
	RICOH_FG_DBG(KERN_INFO "PMU: %s ERROR ******\n", __func__);
	return err;
}

static void ricoh619_jeita_work(struct work_struct *work)
{
	int ret;
	bool is_jeita_updated = false;
	struct ricoh619_battery_info *info = container_of(work,
		struct ricoh619_battery_info, jeita_work.work);

	mutex_lock(&info->lock);

	ret = check_jeita_status(info, &is_jeita_updated);
	if (0 == ret) {
		queue_delayed_work(info->monitor_wqueue, &info->jeita_work,
					 RICOH619_JEITA_UPDATE_TIME * HZ);
	} else {
		RICOH_FG_DBG(KERN_INFO "PMU: %s *** Call check_jeita_status() in jeita_work, err:%d ******\n", 
							__func__, ret);
		queue_delayed_work(info->monitor_wqueue, &info->jeita_work,
					 RICOH619_FG_RESET_TIME * HZ);
	}

	mutex_unlock(&info->lock);

	if(true == is_jeita_updated)
		power_supply_changed(&info->battery);

	return;
}

#ifdef ENABLE_FACTORY_MODE
/*------------------------------------------------------*/
/* Factory Mode						*/
/*    Check Battery exist or not			*/
/*    If not, disabled Rapid to Complete State change	*/
/*------------------------------------------------------*/
static int ricoh619_factory_mode(struct ricoh619_battery_info *info)
{
	int ret = 0;
	uint8_t val = 0;

	ret = ricoh619_read(info->dev->parent, RICOH619_INT_MON_CHGCTR, &val);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the control register\n");
		return ret;
	}
	if (!(val & 0x01)) /* No Adapter connected */
		return ret;

	/* Rapid to Complete State change disable */
	ret = ricoh619_set_bits(info->dev->parent, RICOH619_CHG_CTL1, 0x40);
	if (ret < 0) {
		dev_err(info->dev, "Error in writing the control register\n");
		return ret;
	}

	/* Wait 1s for checking Charging State */
	queue_delayed_work(info->factory_mode_wqueue, &info->factory_mode_work,
			 1*HZ);

	return ret;
}

static void check_charging_state_work(struct work_struct *work)
{
	struct ricoh619_battery_info *info = container_of(work,
		struct ricoh619_battery_info, factory_mode_work.work);

	int ret = 0;
	uint8_t val = 0;
	int chargeCurrent = 0;

	ret = ricoh619_read(info->dev->parent, CHGSTATE_REG, &val);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the control register\n");
		return;
	}


	chargeCurrent = get_check_fuel_gauge_reg(info, CC_AVERAGE1_REG,
						 CC_AVERAGE0_REG, 0x3fff);
	if (chargeCurrent < 0) {
		dev_err(info->dev, "Error in reading the FG register\n");
		return;
	}

	/* Repid State && Charge Current about 0mA */
	if (((chargeCurrent >= 0x3ffc && chargeCurrent <= 0x3fff)
		|| chargeCurrent < 0x05) && val == 0x43) {
		RICOH_FG_DBG("PMU:%s --- No battery !! Enter Factory mode ---\n"
				, __func__);
		info->entry_factory_mode = true;
		/* clear FG_ACC bit */
		ret = ricoh619_clr_bits(info->dev->parent, RICOH619_FG_CTRL, 0x10);
		if (ret < 0)
			dev_err(info->dev, "Error in writing FG_CTRL\n");
		
		return;	/* Factory Mode */
	}

	/* Return Normal Mode --> Rapid to Complete State change enable */
	ret = ricoh619_clr_bits(info->dev->parent, RICOH619_CHG_CTL1, 0x40);
	if (ret < 0) {
		dev_err(info->dev, "Error in writing the control register\n");
		return;
	}
	RICOH_FG_DBG("PMU:%s --- Battery exist !! Return Normal mode ---0x%2x\n"
			, __func__, val);

	return;
}
#endif /* ENABLE_FACTORY_MODE */

static int Calc_Linear_Interpolation(int x0, int y0, int x1, int y1, int y)
{
	int	alpha;
	int x;

	alpha = (y - y0)*100 / (y1 - y0);

	x = ((100 - alpha) * x0 + alpha * x1) / 100;

	return x;
}

static void ricoh619_scaling_OCV_table(struct ricoh619_battery_info *info, int cutoff_vol, int full_vol, int *start_per, int *end_per)
{
	int		i, j;
	int		temp;
	int		percent_step;
	int		OCV_percent_new[11];

	/* get ocv table. this table is calculated by Apprication */
	//RICOH_FG_DBG("PMU : %s : original table\n",__func__);
	for (i = 0; i <= 10; i = i+1) {
		RICOH_FG_DBG(KERN_INFO "PMU: %s : %d0%% voltage = %d uV\n",
				 __func__, i, info->soca->ocv_table_def[i]);
	}
	//RICOH_FG_DBG("PMU: %s : cutoff_vol %d full_vol %d\n",
	//			 __func__, cutoff_vol,full_vol);

	/* Check Start % */
	if (info->soca->ocv_table_def[0] > cutoff_vol * 1000) {
		*start_per = 0;
		RICOH_FG_DBG("PMU : %s : setting value of cuttoff_vol(%d) is out of range(%d) \n",__func__, cutoff_vol, info->soca->ocv_table_def[0]);
	} else {
		for (i = 1; i < 11; i++) {
			if (info->soca->ocv_table_def[i] >= cutoff_vol * 1000) {
				/* unit is 0.001% */
				*start_per = Calc_Linear_Interpolation(
					(i-1)*1000, info->soca->ocv_table_def[i-1], i*1000,
					info->soca->ocv_table_def[i], (cutoff_vol * 1000));
				break;
			}
		}
	}

	/* Check End % */
	for (i = 1; i < 11; i++) {
		if (info->soca->ocv_table_def[i] >= full_vol * 1000) {
			/* unit is 0.001% */
			*end_per = Calc_Linear_Interpolation(
				(i-1)*1000, info->soca->ocv_table_def[i-1], i*1000,
				 info->soca->ocv_table_def[i], (full_vol * 1000));
			break;
		}
	}

	/* calc new ocv percent */
	percent_step = ( *end_per - *start_per) / 10;
	//RICOH_FG_DBG("PMU : %s : percent_step is %d end per is %d start per is %d\n",__func__, percent_step, *end_per, *start_per);

	for (i = 0; i < 11; i++) {
		OCV_percent_new[i]
			 = *start_per + percent_step*(i - 0);
	}

	/* calc new ocv voltage */
	for (i = 0; i < 11; i++) {
		for (j = 1; j < 11; j++) {
			if (1000*j >= OCV_percent_new[i]) {
				temp = Calc_Linear_Interpolation(
					info->soca->ocv_table_def[j-1], (j-1)*1000,
					info->soca->ocv_table_def[j] , j*1000,
					 OCV_percent_new[i]);

				temp = ( (temp/1000) * 4095 ) / 5000;

				battery_init_para[info->num][i*2 + 1] = temp;
				battery_init_para[info->num][i*2] = temp >> 8;

				break;
			}
		}
	}
	RICOH_FG_DBG("PMU : %s : new table\n",__func__);
	for (i = 0; i <= 10; i = i+1) {
		temp = (battery_init_para[info->num][i*2]<<8)
			 | (battery_init_para[info->num][i*2+1]);
		/* conversion unit 1 Unit is 1.22mv (5000/4095 mv) */
		temp = ((temp * 50000 * 10 / 4095) + 5) / 10;
		RICOH_FG_DBG("PMU : %s : ocv_table %d is %d v\n",__func__, i, temp);
	}

}

static int ricoh619_set_OCV_table(struct ricoh619_battery_info *info)
{
	int		ret = 0;
	int		i;
	int		full_ocv;
	int		available_cap;
	int		available_cap_ori;
	int		temp;
	int		temp1;
	int		start_per = 0;
	int		end_per = 0;
	int		Rbat;
	int		Ibat_min;
	uint8_t val;
	uint8_t val2;
	uint8_t val_temp;


	//get ocv table 
	for (i = 0; i <= 10; i = i+1) {
		info->soca->ocv_table_def[i] = get_OCV_voltage(info, i);
		RICOH_FG_DBG(KERN_INFO "PMU: %s : %d0%% voltage = %d uV\n",
			 __func__, i, info->soca->ocv_table_def[i]);
	}

	temp =  (battery_init_para[info->num][24]<<8) | (battery_init_para[info->num][25]);
	Rbat = temp * 1000 / 512 * 5000 / 4095;
	info->soca->Rsys = Rbat + 55;

	if ((info->fg_target_ibat == 0) || (info->fg_target_vsys == 0)) {	/* normal version */

		temp =  (battery_init_para[info->num][22]<<8) | (battery_init_para[info->num][23]);
		//fa_cap = get_check_fuel_gauge_reg(info, FA_CAP_H_REG, FA_CAP_L_REG,
		//				0x7fff);

		info->soca->target_ibat = temp*2/10; /* calc 0.2C*/
		temp1 =  (battery_init_para[info->num][0]<<8) | (battery_init_para[info->num][1]);

		info->soca->target_vsys = temp1 + ( info->soca->target_ibat * info->soca->Rsys ) / 1000;
		

	} else {
		info->soca->target_ibat = info->fg_target_ibat;
		/* calc min vsys value */
		temp1 =  (battery_init_para[info->num][0]<<8) | (battery_init_para[info->num][1]);
		temp = temp1 + ( info->soca->target_ibat * info->soca->Rsys ) / 1000;
		if( temp < info->fg_target_vsys) {
			info->soca->target_vsys = info->fg_target_vsys;
		} else {
			info->soca->target_vsys = temp;
			RICOH_FG_DBG("PMU : %s : setting value of target vsys(%d) is out of range(%d)\n",__func__, info->fg_target_vsys, temp);
		}
	}

	//for debug
	RICOH_FG_DBG("PMU : %s : target_vsys is %d target_ibat is %d\n",__func__,info->soca->target_vsys,info->soca->target_ibat);
	
	if ((info->soca->target_ibat == 0) || (info->soca->target_vsys == 0)) {	/* normal version */
	} else {	/*Slice cutoff voltage version. */

		Ibat_min = -1 * info->soca->target_ibat;
		info->soca->cutoff_ocv = info->soca->target_vsys - Ibat_min * info->soca->Rsys / 1000;
		
		full_ocv = (battery_init_para[info->num][20]<<8) | (battery_init_para[info->num][21]);
		full_ocv = full_ocv * 5000 / 4095;

		ricoh619_scaling_OCV_table(info, info->soca->cutoff_ocv, full_ocv, &start_per, &end_per);

		/* calc available capacity */
		/* get avilable capacity */
		/* battery_init_para23-24 is designe capacity */
		available_cap = (battery_init_para[info->num][22]<<8)
					 | (battery_init_para[info->num][23]);

		available_cap = available_cap
			 * ((10000 - start_per) / 100) / 100 ;


		battery_init_para[info->num][23] =  available_cap;
		battery_init_para[info->num][22] =  available_cap >> 8;

	}
	ret = ricoh619_clr_bits(info->dev->parent, FG_CTRL_REG, 0x01);
	if (ret < 0) {
		dev_err(info->dev, "error in FG_En off\n");
		goto err;
	}
	/////////////////////////////////
	ret = ricoh619_read_bank1(info->dev->parent, 0xDC, &val);
	if (ret < 0) {
		dev_err(info->dev, "batterry initialize error\n");
		goto err;
	}

	val_temp = val;
	val	&= 0x0F; //clear bit 4-7
	val	|= 0x10; //set bit 4
	
	ret = ricoh619_write_bank1(info->dev->parent, 0xDC, val);
	if (ret < 0) {
		dev_err(info->dev, "batterry initialize error\n");
		goto err;
	}
	
	ret = ricoh619_read_bank1(info->dev->parent, 0xDC, &val2);
	if (ret < 0) {
		dev_err(info->dev, "batterry initialize error\n");
		goto err;
	}

	ret = ricoh619_write_bank1(info->dev->parent, 0xDC, val_temp);
	if (ret < 0) {
		dev_err(info->dev, "batterry initialize error\n");
		goto err;
	}

	RICOH_FG_DBG("PMU : %s : original 0x%x, before 0x%x, after 0x%x\n",__func__, val_temp, val, val2);
	
	if (val != val2) {
		ret = ricoh619_bulk_writes_bank1(info->dev->parent,
				BAT_INIT_TOP_REG, 30, battery_init_para[info->num]);
		if (ret < 0) {
			dev_err(info->dev, "batterry initialize error\n");
			goto err;
		}
	} else {
		ret = ricoh619_read_bank1(info->dev->parent, 0xD2, &val);
		if (ret < 0) {
		dev_err(info->dev, "batterry initialize error\n");
		goto err;
		}
	
		ret = ricoh619_read_bank1(info->dev->parent, 0xD3, &val2);
		if (ret < 0) {
			dev_err(info->dev, "batterry initialize error\n");
			goto err;
		}
		
		available_cap_ori = val2 + (val << 8);
		available_cap = battery_init_para[info->num][23]
						+ (battery_init_para[info->num][22] << 8);

		if (available_cap_ori == available_cap) {
			ret = ricoh619_bulk_writes_bank1(info->dev->parent,
				BAT_INIT_TOP_REG, 22, battery_init_para[info->num]);
			if (ret < 0) {
				dev_err(info->dev, "batterry initialize error\n");
				return ret;
			}
			
			for (i = 0; i < 6; i++) {
				ret = ricoh619_write_bank1(info->dev->parent, 0xD4+i, battery_init_para[info->num][24+i]);
				if (ret < 0) {
					dev_err(info->dev, "batterry initialize error\n");
					return ret;
				}
			}
		} else {
			ret = ricoh619_bulk_writes_bank1(info->dev->parent,
				BAT_INIT_TOP_REG, 30, battery_init_para[info->num]);
			if (ret < 0) {
				dev_err(info->dev, "batterry initialize error\n");
				goto err;
			}
		}
	}

	////////////////////////////////

	return 0;
err:
	return ret;
}

/* Initial setting of battery */
static int ricoh619_init_battery(struct ricoh619_battery_info *info)
{
	int ret = 0;
	uint8_t val;
	uint8_t val2;
	/* Need to implement initial setting of batery and error */
	/* -------------------------- */
#ifdef ENABLE_FUEL_GAUGE_FUNCTION

	/* set relaxation state */
	if (RICOH619_REL1_SEL_VALUE > 240)
		val = 0x0F;
	else
		val = RICOH619_REL1_SEL_VALUE / 16 ;

	/* set relaxation state */
	if (RICOH619_REL2_SEL_VALUE > 120)
		val2 = 0x0F;
	else
		val2 = RICOH619_REL2_SEL_VALUE / 8 ;

	val =  val + (val2 << 4);

	ret = ricoh619_write_bank1(info->dev->parent, BAT_REL_SEL_REG, val);
	if (ret < 0) {
		dev_err(info->dev, "Error in writing BAT_REL_SEL_REG\n");
		return ret;
	}

	ret = ricoh619_read_bank1(info->dev->parent, BAT_REL_SEL_REG, &val);
	RICOH_FG_DBG("PMU: -------  BAT_REL_SEL= %xh: =======\n",
		val);

	ret = ricoh619_write_bank1(info->dev->parent, BAT_TA_SEL_REG, 0x00);
	if (ret < 0) {
		dev_err(info->dev, "Error in writing BAT_TA_SEL_REG\n");
		return ret;
	}

//	ret = ricoh619_read(info->dev->parent, FG_CTRL_REG, &val);
//	if (ret < 0) {
//		dev_err(info->dev, "Error in reading the control register\n");
//		return ret;
//	}

//	val = (val & 0x10) >> 4;
//	info->first_pwon = (val == 0) ? 1 : 0;
	ret = ricoh619_read(info->dev->parent, PSWR_REG, &val);
	if (ret < 0) {
		dev_err(info->dev,"Error in reading PSWR_REG %d\n", ret);
		return ret;
	}
	info->first_pwon = (val == 0) ? 1 : 0;
	g_soc = val & 0x7f;
	
	ret = ricoh619_set_OCV_table(info);
	if (ret < 0) {
		dev_err(info->dev, "Error in writing the OCV Tabler\n");
		return ret;
	}

	ret = ricoh619_write(info->dev->parent, FG_CTRL_REG, 0x11);
	if (ret < 0) {
		dev_err(info->dev, "Error in writing the control register\n");
		return ret;
	}

#endif

	ret = ricoh619_write(info->dev->parent, VINDAC_REG, 0x03);
	if (ret < 0) {
		dev_err(info->dev, "Error in writing the control register\n");
		return ret;
	}

	if (info->alarm_vol_mv < 2700 || info->alarm_vol_mv > 3400) {
		dev_err(info->dev, "alarm_vol_mv is out of range!\n");
		return -1;
	}

	return ret;
}

/* Initial setting of charger */
static int ricoh619_init_charger(struct ricoh619_battery_info *info)
{
	int err;
	uint8_t val;
	uint8_t val2;
	uint8_t val3;
	int charge_status;
	int	vfchg_val;
	int	icchg_val;
	int	rbat;
	int	temp;

	info->chg_ctr = 0;
	info->chg_stat1 = 0;

	err = ricoh619_set_bits(info->dev->parent, RICOH619_PWR_FUNC, 0x20);
	if (err < 0) {
		dev_err(info->dev, "Error in writing the PWR FUNC register\n");
		goto free_device;
	}

	charge_status = get_power_supply_status(info);

	if (charge_status != POWER_SUPPLY_STATUS_FULL)
	{
		/* Disable charging */
		err = ricoh619_clr_bits(info->dev->parent,CHGCTL1_REG, 0x03);
		if (err < 0) {
			dev_err(info->dev, "Error in writing the control register\n");
			goto free_device;
		}
	}

	//debug messeage
	err = ricoh619_read(info->dev->parent, REGISET1_REG,&val);
	RICOH_FG_DBG("PMU : %s : before REGISET1_REG (0x%x) is 0x%x info->ch_ilim_adp is 0x%x\n",__func__,REGISET1_REG,val,info->ch_ilim_adp);

	/* REGISET1:(0xB6) setting */
	if ((info->ch_ilim_adp != 0xFF) || (info->ch_ilim_adp <= 0x1D)) {
		val = info->ch_ilim_adp;

		err = ricoh619_write(info->dev->parent, REGISET1_REG,val);
		if (err < 0) {
			dev_err(info->dev, "Error in writing REGISET1_REG %d\n",
										 err);
			goto free_device;
		}
	}

	//debug messeage
	err = ricoh619_read(info->dev->parent, REGISET1_REG,&val);
	RICOH_FG_DBG("PMU : %s : after REGISET1_REG (0x%x) is 0x%x info->ch_ilim_adp is 0x%x\n",__func__,REGISET1_REG,val,info->ch_ilim_adp);
	
		//debug messeage
	err = ricoh619_read(info->dev->parent, REGISET2_REG,&val);
	RICOH_FG_DBG("PMU : %s : before REGISET2_REG (0x%x) is 0x%x info->ch_ilim_usb is 0x%x\n",__func__,REGISET2_REG,val,info->ch_ilim_usb);

	/* REGISET2:(0xB7) setting */
	err = ricoh619_read(info->dev->parent, REGISET2_REG, &val);
	if (err < 0) {
		dev_err(info->dev,
	 	"Error in read REGISET2_REG %d\n", err);
		goto free_device;
	}
	
	if ((info->ch_ilim_usb != 0xFF) || (info->ch_ilim_usb <= 0x1D)) {
		val2 = info->ch_ilim_usb;
	} else {/* Keep OTP value */
		val2 = (val & 0x1F);
	}

		/* keep bit 5-7 */
	val &= 0xE0;
	
	val = val + val2;
	
	err = ricoh619_write(info->dev->parent, REGISET2_REG,val);
	if (err < 0) {
		dev_err(info->dev, "Error in writing REGISET2_REG %d\n",
									 err);
		goto free_device;
	}

		//debug messeage
	err = ricoh619_read(info->dev->parent, REGISET2_REG,&val);
	RICOH_FG_DBG("PMU : %s : after REGISET2_REG (0x%x) is 0x%x info->ch_ilim_usb is 0x%x\n",__func__,REGISET2_REG,val,info->ch_ilim_usb);

	/* CHGISET_REG(0xB8) setting */
		//debug messeage
	err = ricoh619_read(info->dev->parent, CHGISET_REG,&val);
	RICOH_FG_DBG("PMU : %s : before CHGISET_REG (0x%x) is 0x%x info->ch_ichg is 0x%x info->ch_icchg is 0x%x\n",__func__,CHGISET_REG,val,info->ch_ichg,info->ch_icchg);

	err = ricoh619_read(info->dev->parent, CHGISET_REG, &val);
	if (err < 0) {
		dev_err(info->dev,
	 	"Error in read CHGISET_REG %d\n", err);
		goto free_device;
	}

		/* Define Current settings value for charging (bit 4~0)*/
	if ((info->ch_ichg != 0xFF) || (info->ch_ichg <= 0x1D)) {
		val2 = info->ch_ichg;
	} else { /* Keep OTP value */
		val2 = (val & 0x1F);
	}

		/* Define Current settings at the charge completion (bit 7~6)*/
	if ((info->ch_icchg != 0xFF) || (info->ch_icchg <= 0x03)) {
		val3 = info->ch_icchg << 6;
	} else { /* Keep OTP value */
		val3 = (val & 0xC0);
	}

	val = val2 + val3;

	err = ricoh619_write(info->dev->parent, CHGISET_REG, val);
	if (err < 0) {
		dev_err(info->dev, "Error in writing CHGISET_REG %d\n",
									 err);
		goto free_device;
	}

		//debug messeage
	err = ricoh619_read(info->dev->parent, CHGISET_REG,&val);
	RICOH_FG_DBG("PMU : %s : after CHGISET_REG (0x%x) is 0x%x info->ch_ichg is 0x%x info->ch_icchg is 0x%x\n",__func__,CHGISET_REG,val,info->ch_ichg,info->ch_icchg);

		//debug messeage
	err = ricoh619_read(info->dev->parent, BATSET1_REG,&val);
	RICOH_FG_DBG("PMU : %s : before BATSET1_REG (0x%x) is 0x%x info->ch_vbatovset is 0x%x\n",__func__,BATSET1_REG,val,info->ch_vbatovset);
	
	/* BATSET1_REG(0xBA) setting */
	err = ricoh619_read(info->dev->parent, BATSET1_REG, &val);
	if (err < 0) {
		dev_err(info->dev,
	 	"Error in read BATSET1 register %d\n", err);
		goto free_device;
	}

		/* Define Battery overvoltage  (bit 4)*/
	if ((info->ch_vbatovset != 0xFF) || (info->ch_vbatovset <= 0x1)) {
		val2 = info->ch_vbatovset;
		val2 = val2 << 4;
	} else { /* Keep OTP value */
		val2 = (val & 0x10);
	}
	
		/* keep bit 0-3 and bit 5-7 */
	val = (val & 0xEF);
	
	val = val + val2;

	err = ricoh619_write(info->dev->parent, BATSET1_REG, val);
	if (err < 0) {
		dev_err(info->dev, "Error in writing BAT1_REG %d\n",
									 err);
		goto free_device;
	}
		//debug messeage
	err = ricoh619_read(info->dev->parent, BATSET1_REG,&val);
	RICOH_FG_DBG("PMU : %s : after BATSET1_REG (0x%x) is 0x%x info->ch_vbatovset is 0x%x\n",__func__,BATSET1_REG,val,info->ch_vbatovset);
	
		//debug messeage
	err = ricoh619_read(info->dev->parent, BATSET2_REG,&val);
	RICOH_FG_DBG("PMU : %s : before BATSET2_REG (0x%x) is 0x%x info->ch_vrchg is 0x%x info->ch_vfchg is 0x%x \n",__func__,BATSET2_REG,val,info->ch_vrchg,info->ch_vfchg);

	
	/* BATSET2_REG(0xBB) setting */
	err = ricoh619_read(info->dev->parent, BATSET2_REG, &val);
	if (err < 0) {
		dev_err(info->dev,
	 	"Error in read BATSET2 register %d\n", err);
		goto free_device;
	}

		/* Define Re-charging voltage (bit 2~0)*/
	if ((info->ch_vrchg != 0xFF) || (info->ch_vrchg <= 0x04)) {
		val2 = info->ch_vrchg;
	} else { /* Keep OTP value */
		val2 = (val & 0x07);
	}

		/* Define FULL charging voltage (bit 6~4)*/
	if ((info->ch_vfchg != 0xFF) || (info->ch_vfchg <= 0x04)) {
		val3 = info->ch_vfchg;
		val3 = val3 << 4;
	} else {	/* Keep OTP value */
		val3 = (val & 0x70);
	}

		/* keep bit 3 and bit 7 */
	val = (val & 0x88);
	
	val = val + val2 + val3;

	err = ricoh619_write(info->dev->parent, BATSET2_REG, val);
	if (err < 0) {
		dev_err(info->dev, "Error in writing RICOH619_RE_CHARGE_VOLTAGE %d\n",
									 err);
		goto free_device;
	}

		//debug messeage
	err = ricoh619_read(info->dev->parent, BATSET2_REG,&val);
	RICOH_FG_DBG("PMU : %s : after BATSET2_REG (0x%x) is 0x%x info->ch_vrchg is 0x%x info->ch_vfchg is 0x%x  \n",__func__,BATSET2_REG,val,info->ch_vrchg,info->ch_vfchg);

	/* Set rising edge setting ([1:0]=01b)for INT in charging */
	/*  and rising edge setting ([3:2]=01b)for charge completion */
	err = ricoh619_read(info->dev->parent, RICOH619_CHG_STAT_DETMOD1, &val);
	if (err < 0) {
		dev_err(info->dev, "Error in reading CHG_STAT_DETMOD1 %d\n",
								 err);
		goto free_device;
	}
	val &= 0xf0;
	val |= 0x05;
	err = ricoh619_write(info->dev->parent, RICOH619_CHG_STAT_DETMOD1, val);
	if (err < 0) {
		dev_err(info->dev, "Error in writing CHG_STAT_DETMOD1 %d\n",
								 err);
		goto free_device;
	}

	/* Unmask In charging/charge completion */
	err = ricoh619_write(info->dev->parent, RICOH619_INT_MSK_CHGSTS1, 0xfc);
	if (err < 0) {
		dev_err(info->dev, "Error in writing INT_MSK_CHGSTS1 %d\n",
								 err);
		goto free_device;
	}

	/* Set both edge for VUSB([3:2]=11b)/VADP([1:0]=11b) detect */
	err = ricoh619_read(info->dev->parent, RICOH619_CHG_CTRL_DETMOD1, &val);
	if (err < 0) {
		dev_err(info->dev, "Error in reading CHG_CTRL_DETMOD1 %d\n",
								 err);
		goto free_device;
	}
	val &= 0xf0;
	val |= 0x0f;
	err = ricoh619_write(info->dev->parent, RICOH619_CHG_CTRL_DETMOD1, val);
	if (err < 0) {
		dev_err(info->dev, "Error in writing CHG_CTRL_DETMOD1 %d\n",
								 err);
		goto free_device;
	}

	/* Unmask In VUSB/VADP completion */
	err = ricoh619_write(info->dev->parent, RICOH619_INT_MSK_CHGCTR, 0xfc);
	if (err < 0) {
		dev_err(info->dev, "Error in writing INT_MSK_CHGSTS1 %d\n",
									 err);
		goto free_device;
	}
	
	if (charge_status != POWER_SUPPLY_STATUS_FULL)
	{
		/* Enable charging */
		err = ricoh619_set_bits(info->dev->parent,CHGCTL1_REG, 0x03);
		if (err < 0) {
			dev_err(info->dev, "Error in writing the control register\n");
			goto free_device;
		}
	}
	/* get OCV100_min, OCV100_min*/
	temp = (battery_init_para[info->num][24]<<8) | (battery_init_para[info->num][25]);
	rbat = temp * 1000 / 512 * 5000 / 4095;
	
	/* get vfchg value */
	err = ricoh619_read(info->dev->parent, BATSET2_REG, &val);
	if (err < 0) {
		dev_err(info->dev, "Error in reading the batset2reg\n");
		goto free_device;
	}
	val &= 0x70;
	val2 = val >> 4;
	if (val2 <= 3) {
		vfchg_val = 4050 + val2 * 50;
	} else {
		vfchg_val = 4350;
	}
	RICOH_FG_DBG("PMU : %s : test test val %d, val2 %d vfchg %d\n", __func__, val, val2, vfchg_val);

	/* get  value */
	err = ricoh619_read(info->dev->parent, CHGISET_REG, &val);
	if (err < 0) {
		dev_err(info->dev, "Error in reading the chgisetreg\n");
		goto free_device;
	}
	val &= 0xC0;
	val2 = val >> 6;
	icchg_val = 50 + val2 * 50;
	RICOH_FG_DBG("PMU : %s : test test val %d, val2 %d icchg %d\n", __func__, val, val2, icchg_val);

	info->soca->OCV100_min = ( vfchg_val * 99 / 100 - (icchg_val * (rbat +20))/1000 - 20 ) * 1000;
	info->soca->OCV100_max = ( vfchg_val * 101 / 100 - (icchg_val * (rbat +20))/1000 + 20 ) * 1000;
	
	RICOH_FG_DBG("PMU : %s : 100 min %d, 100 max %d vfchg %d icchg %d rbat %d\n",__func__,
	info->soca->OCV100_min,info->soca->OCV100_max,vfchg_val,icchg_val,rbat);

#ifdef ENABLE_LOW_BATTERY_DETECTION
	/* Set ADRQ=00 to stop ADC */
	ricoh619_write(info->dev->parent, RICOH619_ADC_CNT3, 0x0);
	/* Enable VSYS threshold Low interrupt */
	ricoh619_write(info->dev->parent, RICOH619_INT_EN_ADC1, 0x10);
	/* Set ADC auto conversion interval 250ms */
	ricoh619_write(info->dev->parent, RICOH619_ADC_CNT2, 0x0);
	/* Enable VSYS pin conversion in auto-ADC */
//	ricoh619_write(info->dev->parent, RICOH619_ADC_CNT1, 0x10);
	ricoh619_write(info->dev->parent, RICOH619_ADC_CNT1, 0x16);
	/* Set VSYS threshold low voltage value = (voltage(V)*255)/(3*2.5) */
	val = info->alarm_vol_mv * 255 / 7500;
	ricoh619_write(info->dev->parent, RICOH619_ADC_VSYS_THL, val);
	/* Start auto-mode & average 4-time conversion mode for ADC */
	ricoh619_write(info->dev->parent, RICOH619_ADC_CNT3, 0x28);
	/* Enable master ADC INT */
	ricoh619_set_bits(info->dev->parent, RICOH619_INTC_INTEN, ADC_INT);
#endif

free_device:
	return err;
}


static int get_power_supply_status(struct ricoh619_battery_info *info)
{
	uint8_t status;
	uint8_t supply_state;
	uint8_t charge_state;
	int ret = 0;

	/* get  power supply status */
	ret = ricoh619_read(info->dev->parent, CHGSTATE_REG, &status);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the control register\n");
		return ret;
	}

	charge_state = (status & 0x1F);
	supply_state = ((status & 0xC0) >> 6);

	if (info->entry_factory_mode)
			return POWER_SUPPLY_STATUS_NOT_CHARGING;

	if (supply_state == SUPPLY_STATE_BAT) {
		info->soca->chg_status = POWER_SUPPLY_STATUS_DISCHARGING;
	} else {
		switch (charge_state) {
		case	CHG_STATE_CHG_OFF:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_DISCHARGING;
				break;
		case	CHG_STATE_CHG_READY_VADP:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_NOT_CHARGING;
				break;
		case	CHG_STATE_CHG_TRICKLE:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_CHARGING;
				break;
		case	CHG_STATE_CHG_RAPID:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_CHARGING;
				break;
		case	CHG_STATE_CHG_COMPLETE:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_FULL;
				break;
		case	CHG_STATE_SUSPEND:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_DISCHARGING;
				break;
		case	CHG_STATE_VCHG_OVER_VOL:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_DISCHARGING;
				break;
		case	CHG_STATE_BAT_ERROR:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_NOT_CHARGING;
				break;
		case	CHG_STATE_NO_BAT:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_NOT_CHARGING;
				break;
		case	CHG_STATE_BAT_OVER_VOL:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_NOT_CHARGING;
				break;
		case	CHG_STATE_BAT_TEMP_ERR:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_NOT_CHARGING;
				break;
		case	CHG_STATE_DIE_ERR:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_NOT_CHARGING;
				break;
		case	CHG_STATE_DIE_SHUTDOWN:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_DISCHARGING;
				break;
		case	CHG_STATE_NO_BAT2:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_NOT_CHARGING;
				break;
		case	CHG_STATE_CHG_READY_VUSB:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_NOT_CHARGING;
				break;
		default:
				info->soca->chg_status
					= POWER_SUPPLY_STATUS_UNKNOWN;
				break;
		}
	}

	return info->soca->chg_status;
}

static int get_power_supply_Android_status(struct ricoh619_battery_info *info)
{

	get_power_supply_status(info);

	/* get  power supply status */
	if (info->entry_factory_mode)
			return POWER_SUPPLY_STATUS_NOT_CHARGING;

	switch (info->soca->chg_status) {
		case	POWER_SUPPLY_STATUS_UNKNOWN:
				return POWER_SUPPLY_STATUS_UNKNOWN;
				break;

		case	POWER_SUPPLY_STATUS_NOT_CHARGING:
				return POWER_SUPPLY_STATUS_NOT_CHARGING;
				break;

		case	POWER_SUPPLY_STATUS_DISCHARGING:
				return POWER_SUPPLY_STATUS_DISCHARGING;
				break;

		case	POWER_SUPPLY_STATUS_CHARGING:
				return POWER_SUPPLY_STATUS_CHARGING;
				break;

		case	POWER_SUPPLY_STATUS_FULL:
				if(info->soca->displayed_soc == 100 * 100) {
					return POWER_SUPPLY_STATUS_FULL;
				} else {
					return POWER_SUPPLY_STATUS_CHARGING;
				}
				break;
		default:
				return POWER_SUPPLY_STATUS_UNKNOWN;
				break;
	}

	return POWER_SUPPLY_STATUS_UNKNOWN;
}
extern struct ricoh619 *g_ricoh619;
static void charger_irq_work(struct work_struct *work)
{
	struct ricoh619_battery_info *info
		 = container_of(work, struct ricoh619_battery_info, irq_work);
	int ret = 0;
	uint8_t reg_val;
	RICOH_FG_DBG("PMU:%s In\n", __func__);

	power_supply_changed(&info->battery);
	power_supply_changed(&powerac);
	power_supply_changed(&powerusb);

//	mutex_lock(&info->lock);
	
	if (info->chg_stat1 & 0x01) {
		ricoh619_read(info->dev->parent, CHGSTATE_REG, &reg_val);
		if (reg_val & 0x40) { /* USE ADP */	
			#ifdef SUPPORT_USB_CONNECT_TO_ADP
				int i;
				for(i =0;i<60;i++){
				RICOH_FG_DBG("PMU:%s usb det dwc_otg_check_dpdm =%d\n", __func__,dwc_otg_check_dpdm(0));
				if(2 == dwc_otg_check_dpdm(0)){
				/* set adp limit current 2A */
				ricoh619_write(info->dev->parent, REGISET1_REG, 0x16);
				/* set charge current 2A */
				ricoh619_write(info->dev->parent, CHGISET_REG, 0xD3); 
				}
				else {
				/* set adp limit current 500ma */
				ricoh619_write(info->dev->parent, REGISET1_REG, 0x04);
				/* set charge current 500ma */
				ricoh619_write(info->dev->parent, CHGISET_REG, 0xc4); 
				}
				
				power_supply_changed(&info->battery);
				power_supply_changed(&powerac);
				power_supply_changed(&powerusb);
				msleep(100);
				}
			#else //support adp and usb chag
			if (gpio_is_valid(g_ricoh619->dc_det)){
				ret = gpio_request(g_ricoh619->dc_det, "ricoh619_dc_det");
				if (ret < 0) {
					RICOH_FG_DBG("Failed to request gpio %d with ret:""%d\n",g_ricoh619->dc_det, ret);
				}
				gpio_direction_input(g_ricoh619->dc_det);
				ret = gpio_get_value(g_ricoh619->dc_det);

				if (ret ==0){
					/* set adp limit current 2A */
					ricoh619_write(info->dev->parent, REGISET1_REG, 0x16);
					/* set charge current 2A */
					ricoh619_write(info->dev->parent, CHGISET_REG, 0xD3);
 				}
				else {
					/* set adp limit current 500ma */
					ricoh619_write(info->dev->parent, REGISET1_REG, 0x04);
					/* set charge current 500ma */
					ricoh619_write(info->dev->parent, CHGISET_REG, 0xc4); 
				}
				gpio_free(g_ricoh619->dc_det);
			}
			else{
				/* set adp limit current 2A */
				ricoh619_write(info->dev->parent, REGISET1_REG, 0x16);
				/* set charge current 2A */
				ricoh619_write(info->dev->parent, CHGISET_REG, 0xD3); 
			}
			#endif
		} else if (reg_val & 0x80) { /* USE ONLY USB */
			queue_work(info->usb_workqueue, &info->usb_irq_work);
		}
	}
	info->chg_ctr = 0;
	info->chg_stat1 = 0;
	
	/* Enable Interrupt for VADP/USB */
	ret = ricoh619_write(info->dev->parent, RICOH619_INT_MSK_CHGCTR, 0xfc);
	if (ret < 0)
		dev_err(info->dev,
			 "%s(): Error in enable charger mask INT %d\n",
			 __func__, ret);

	/* Enable Interrupt for Charging & complete */
	ret = ricoh619_write(info->dev->parent, RICOH619_INT_MSK_CHGSTS1, 0xfc);
	if (ret < 0)
		dev_err(info->dev,
			 "%s(): Error in enable charger mask INT %d\n",
			 __func__, ret);

//	mutex_unlock(&info->lock);
	RICOH_FG_DBG("PMU:%s Out\n", __func__);
}

#ifdef ENABLE_LOW_BATTERY_DETECTION
static void low_battery_irq_work(struct work_struct *work)
{
	struct ricoh619_battery_info *info = container_of(work,
		 struct ricoh619_battery_info, low_battery_work.work);

	int ret = 0;

	RICOH_FG_DBG("PMU:%s In\n", __func__);

	power_supply_changed(&info->battery);

	/* Enable VADP threshold Low interrupt */
	ricoh619_write(info->dev->parent, RICOH619_INT_EN_ADC1, 0x10);
	if (ret < 0)
		dev_err(info->dev,
			 "%s(): Error in enable adc mask INT %d\n",
			 __func__, ret);
}
#endif


static void ricoh619_usb_charge_det(void)
{
	struct ricoh619 *ricoh619 = g_ricoh619;
	ricoh619_set_bits(ricoh619->dev,REGISET2_REG,(1 << 7));  //set usb limit current  when SDP or other mode
	RICOH_FG_DBG("PMU:%s usb det dwc_otg_check_dpdm =%d\n", __func__,dwc_otg_check_dpdm(0));
	if(2 == dwc_otg_check_dpdm(0)){
	ricoh619_write(ricoh619->dev,REGISET2_REG,0x16);  //set usb limit current  2A
	ricoh619_write(ricoh619->dev,CHGISET_REG,0xD3);  //set charge current  2A
	}
	else {
	ricoh619_write(ricoh619->dev,REGISET2_REG,0x04);  //set usb limit current  500ma
	ricoh619_write(ricoh619->dev,CHGISET_REG,0xC4);  //set charge current	500ma
	}
	power_supply_changed(&powerac);
	power_supply_changed(&powerusb);
}

static void usb_det_irq_work(struct work_struct *work)
{
	struct ricoh619_battery_info *info = container_of(work,
		 struct ricoh619_battery_info, usb_irq_work);
	int ret = 0;
	uint8_t sts;

	RICOH_FG_DBG("PMU:%s In\n", __func__);

	power_supply_changed(&info->battery);
	power_supply_changed(&powerac);
	power_supply_changed(&powerusb);

	mutex_lock(&info->lock);

	/* Enable Interrupt for VUSB */
	ret = ricoh619_clr_bits(info->dev->parent,
					 RICOH619_INT_MSK_CHGCTR, 0x02);
	if (ret < 0)
		dev_err(info->dev,
			 "%s(): Error in enable charger mask INT %d\n",
			 __func__, ret);

	mutex_unlock(&info->lock);
	ret = ricoh619_read(info->dev->parent, RICOH619_INT_MON_CHGCTR, &sts);
	if (ret < 0)
		dev_err(info->dev, "Error in reading the control register\n");

	sts &= 0x02;
	if (sts)
		ricoh619_usb_charge_det();
	
	RICOH_FG_DBG("PMU:%s Out\n", __func__);
}

static irqreturn_t charger_in_isr(int irq, void *battery_info)
{
	struct ricoh619_battery_info *info = battery_info;
	RICOH_FG_DBG("PMU:%s\n", __func__); 

	info->chg_stat1 |= 0x01;

	queue_work(info->workqueue, &info->irq_work);
//	rk_send_wakeup_key();

	return IRQ_HANDLED;
}

static irqreturn_t charger_complete_isr(int irq, void *battery_info)
{
	struct ricoh619_battery_info *info = battery_info;
	RICOH_FG_DBG("PMU:%s\n", __func__);

	info->chg_stat1 |= 0x02;
	queue_work(info->workqueue, &info->irq_work);
//	rk_send_wakeup_key();
	
	return IRQ_HANDLED;
}

static irqreturn_t charger_usb_isr(int irq, void *battery_info)
{
	struct ricoh619_battery_info *info = battery_info;
	RICOH_FG_DBG("PMU:%s\n", __func__);

	info->chg_ctr |= 0x02;
	
	queue_work(info->workqueue, &info->irq_work);
	
	info->soca->dischg_state = 0;
	info->soca->chg_count = 0;

//	queue_work(info->usb_workqueue, &info->usb_irq_work);
	rk_send_wakeup_key(); 
	 
	if (RICOH619_SOCA_UNSTABLE == info->soca->status
		|| RICOH619_SOCA_FG_RESET == info->soca->status)
		info->soca->stable_count = 11;
	
	return IRQ_HANDLED;
}

static irqreturn_t charger_adp_isr(int irq, void *battery_info)
{
	struct ricoh619_battery_info *info = battery_info;
	RICOH_FG_DBG("PMU:%s\n", __func__);

	info->chg_ctr |= 0x01;
	queue_work(info->workqueue, &info->irq_work);
	rk_send_wakeup_key(); 

	info->soca->dischg_state = 0;
	info->soca->chg_count = 0;
	if (RICOH619_SOCA_UNSTABLE == info->soca->status
		|| RICOH619_SOCA_FG_RESET == info->soca->status)
		info->soca->stable_count = 11;

	return IRQ_HANDLED;
}


#ifdef ENABLE_LOW_BATTERY_DETECTION
/*************************************************************/
/* for Detecting Low Battery                                 */
/*************************************************************/

static irqreturn_t adc_vsysl_isr(int irq, void *battery_info)
{

	struct ricoh619_battery_info *info = battery_info;

#if 1
	RICOH_FG_DBG("PMU:%s\n", __func__);

	queue_delayed_work(info->monitor_wqueue, &info->low_battery_work,
					LOW_BATTERY_DETECTION_TIME*HZ);

#endif

	RICOH_FG_DBG("PMU:%s\n", __func__);
//	ricoh619_write(info->dev->parent, RICOH619_INT_EN_ADC1, 0x10);
	rk_send_wakeup_key(); 

	return IRQ_HANDLED;
}
#endif
#ifdef RICOH619_VADP_DROP_WORK
static void vadp_drop_irq_work(struct work_struct *work)
{
	struct ricoh619_battery_info *info = container_of(work,
		 struct ricoh619_battery_info, vadp_drop_work.work);

	int ret = 0;
	uint8_t data[6];
	u16 reg[2];

	RICOH_FG_DBG("PMU vadp_drop_work:%s In\n", __func__);
	mutex_lock(&info->lock);	
	ret = ricoh619_read(info->dev->parent, 0x6a, &data[0]);
	ret = ricoh619_read(info->dev->parent, 0x6b, &data[1]);
	ret = ricoh619_read(info->dev->parent, 0x6c, &data[2]);
	ret = ricoh619_read(info->dev->parent, 0x6d, &data[3]);
	ret = ricoh619_read(info->dev->parent, CHGSTATE_REG,&data[4]);
	reg[0]= (data[0]<<4) |data[1];
	reg[1]= (data[2]<<4) |data[3];

//	printk("PMU vadp_drop:%s In %08x %08x %08x %08x %08x %08x %d\n", __func__,data[0],data[1],data[2],data[3],reg[0],reg[1],ret);	
	if ((2*(reg[0] +82)) > 3*reg[1]){
		ricoh619_write(info->dev->parent, 0xb3, 0x28);
//		printk("PMU vadp_drop charger disable:%s In  %08x %08x\n", __func__,reg[0],reg[1]); 
	}
	else if(data[4] & 0xc0){
		ret = ricoh619_read(info->dev->parent, 0xb3, &data[5]);
//		 printk("PMU charger is disabled:%s data[4]= %08x data[5]=%08x\n", __func__,data[4],data[5]);
		if(((data[5] & 0x03) ==0)|| ((data[5] & 0x08)==0)){
			ricoh619_write(info->dev->parent, 0xb3, 0x23);
			 ret = ricoh619_read(info->dev->parent, 0xb3, &data[5]);
//			printk("PMU charger enable:%s data[4]= %08x data[5]=%08x\n", __func__,data[4],data[5]);
		}
	}
	power_supply_changed(&info->battery);
	power_supply_changed(&powerac);
	power_supply_changed(&powerusb);
	mutex_unlock(&info->lock);
	queue_delayed_work(info->monitor_wqueue, &info->vadp_drop_work,3*HZ);

}
#endif
/*
 * Get Charger Priority
 * - get higher-priority between VADP and VUSB
 * @ data: higher-priority is stored
 *         true : VUSB
 *         false: VADP
 */
 /*
static int get_charge_priority(struct ricoh619_battery_info *info, bool *data)
{
	int ret = 0;
	uint8_t val = 0;

	ret = ricoh619_read(info->dev->parent, CHGCTL1_REG, &val);
	val = val >> 7;
	*data = (bool)val;

	return ret;
}
*/

/*
 * Set Charger Priority
 * - set higher-priority between VADP and VUSB
 * - data: higher-priority is stored
 *         true : VUSB
 *         false: VADP
 */
 /*
static int set_charge_priority(struct ricoh619_battery_info *info, bool *data)
{
	int ret = 0;
	uint8_t val = 0x80;

	if (*data == 1)
		ret = ricoh619_set_bits(info->dev->parent, CHGCTL1_REG, val);
	else
		ret = ricoh619_clr_bits(info->dev->parent, CHGCTL1_REG, val);

	return ret;
}
*/
#ifdef	ENABLE_FUEL_GAUGE_FUNCTION
static int get_check_fuel_gauge_reg(struct ricoh619_battery_info *info,
					 int Reg_h, int Reg_l, int enable_bit)
{
	uint8_t get_data_h, get_data_l;
	int old_data, current_data;
	int i;
	int ret = 0;

	old_data = 0;

	for (i = 0; i < 5 ; i++) {
		ret = ricoh619_read(info->dev->parent, Reg_h, &get_data_h);
		if (ret < 0) {
			dev_err(info->dev, "Error in reading the control register\n");
			return ret;
		}

		ret = ricoh619_read(info->dev->parent, Reg_l, &get_data_l);
		if (ret < 0) {
			dev_err(info->dev, "Error in reading the control register\n");
			return ret;
		}

		current_data = ((get_data_h & 0xff) << 8) | (get_data_l & 0xff);
		current_data = (current_data & enable_bit);

		if (current_data == old_data)
			return current_data;
		else
			old_data = current_data;
	}

	return current_data;
}

static int calc_capacity(struct ricoh619_battery_info *info)
{
	uint8_t capacity;
	int temp;
	int ret = 0;
	int nt;
	int temperature;

	temperature = get_battery_temp_2(info) / 10; /* unit 0.1 degree -> 1 degree */

	if (temperature >= 25) {
		nt = 0;
	} else if (temperature >= 5) {
		nt = (25 - temperature) * RICOH619_TAH_SEL2 * 625 / 100;
	} else {
		nt = (625  + (5 - temperature) * RICOH619_TAL_SEL2 * 625 / 100);
	}

	/* get remaining battery capacity from fuel gauge */
	ret = ricoh619_read(info->dev->parent, SOC_REG, &capacity);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the control register\n");
		return ret;
	}

	temp = capacity * 100 * 100 / (10000 - nt);

	temp = min(100, temp);
	temp = max(0, temp);
	
	return temp;		/* Unit is 1% */
}

static int calc_capacity_2(struct ricoh619_battery_info *info)
{
	uint8_t val;
	long capacity;
	int re_cap, fa_cap;
	int temp;
	int ret = 0;
	int nt;
	int temperature;

	temperature = get_battery_temp_2(info) / 10; /* unit 0.1 degree -> 1 degree */

	if (temperature >= 25) {
		nt = 0;
	} else if (temperature >= 5) {
		nt = (25 - temperature) * RICOH619_TAH_SEL2 * 625 / 100;
	} else {
		nt = (625  + (5 - temperature) * RICOH619_TAL_SEL2 * 625 / 100);
	}

	re_cap = get_check_fuel_gauge_reg(info, RE_CAP_H_REG, RE_CAP_L_REG,
						0x7fff);
	fa_cap = get_check_fuel_gauge_reg(info, FA_CAP_H_REG, FA_CAP_L_REG,
						0x7fff);

	if (fa_cap != 0) {
		capacity = ((long)re_cap * 100 * 100 / fa_cap);
		capacity = (long)(min(10000, (int)capacity));
		capacity = (long)(max(0, (int)capacity));
	} else {
		ret = ricoh619_read(info->dev->parent, SOC_REG, &val);
		if (ret < 0) {
			dev_err(info->dev, "Error in reading the control register\n");
			return ret;
		}
		capacity = (long)val * 100;
	}
	

	temp = (int)(capacity * 100 * 100 / (10000 - nt));

	temp = min(10000, temp);
	temp = max(0, temp);

	return temp;		/* Unit is 0.01% */
}

static int get_battery_temp(struct ricoh619_battery_info *info)
{
	int ret = 0;
	int sign_bit;

	ret = get_check_fuel_gauge_reg(info, TEMP_1_REG, TEMP_2_REG, 0x0fff);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the fuel gauge control register\n");
		return ret;
	}

	/* bit3 of 0xED(TEMP_1) is sign_bit */
	sign_bit = ((ret & 0x0800) >> 11);

	ret = (ret & 0x07ff);

	if (sign_bit == 0)	/* positive value part */
		/* conversion unit */
		/* 1 unit is 0.0625 degree and retun unit
		 * should be 0.1 degree,
		 */
		ret = ret * 625  / 1000;
	else {	/*negative value part */
		ret = (~ret + 1) & 0x7ff;
		ret = -1 * ret * 625 / 1000;
	}

	return ret;
}

static int get_battery_temp_2(struct ricoh619_battery_info *info)
{
	uint8_t reg_buff[2];
	long temp, temp_off, temp_gain;
	bool temp_sign, temp_off_sign, temp_gain_sign;
	int Vsns = 0;
	int Iout = 0;
	int Vthm, Rthm;
	int reg_val = 0;
	int new_temp;
	long R_ln1, R_ln2;
	int ret = 0;

	/* Calculate TEMP */
	ret = get_check_fuel_gauge_reg(info, TEMP_1_REG, TEMP_2_REG, 0x0fff);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the fuel gauge register\n");
		goto out;
	}

	reg_val = ret;
	temp_sign = (reg_val & 0x0800) >> 11;
	reg_val = (reg_val & 0x07ff);

	if (temp_sign == 0)	/* positive value part */
		/* the unit is 0.0001 degree */
		temp = (long)reg_val * 625;
	else {	/*negative value part */
		reg_val = (~reg_val + 1) & 0x7ff;
		temp = -1 * (long)reg_val * 625;
	}

	/* Calculate TEMP_OFF */
	ret = ricoh619_bulk_reads_bank1(info->dev->parent,
					TEMP_OFF_H_REG, 2, reg_buff);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the fuel gauge register\n");
		goto out;
	}

	reg_val = reg_buff[0] << 8 | reg_buff[1];
	temp_off_sign = (reg_val & 0x0800) >> 11;
	reg_val = (reg_val & 0x07ff);

	if (temp_off_sign == 0)	/* positive value part */
		/* the unit is 0.0001 degree */
		temp_off = (long)reg_val * 625;
	else {	/*negative value part */
		reg_val = (~reg_val + 1) & 0x7ff;
		temp_off = -1 * (long)reg_val * 625;
	}

	/* Calculate TEMP_GAIN */
	ret = ricoh619_bulk_reads_bank1(info->dev->parent,
					TEMP_GAIN_H_REG, 2, reg_buff);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the fuel gauge register\n");
		goto out;
	}

	reg_val = reg_buff[0] << 8 | reg_buff[1];
	temp_gain_sign = (reg_val & 0x0800) >> 11;
	reg_val = (reg_val & 0x07ff);

	if (temp_gain_sign == 0)	/* positive value part */
		/* 1 unit is 0.000488281. the result is 0.01 */
		temp_gain = (long)reg_val * 488281 / 100000;
	else {	/*negative value part */
		reg_val = (~reg_val + 1) & 0x7ff;
		temp_gain = -1 * (long)reg_val * 488281 / 100000;
	}

	/* Calculate VTHM */
	if (0 != temp_gain)
		Vthm = (int)((temp - temp_off) / 4095 * 2500 / temp_gain);
	else {
		RICOH_FG_DBG("PMU %s Skip to compensate temperature\n", __func__);
		goto out;
	}

	ret = measure_Ibatt_FG(info, &Iout);
	Vsns = Iout * 2 / 100;

	if (temp < -120000) {
		/* Low Temperature */
		if (0 != (2500 - Vthm)) {
			Rthm = 10 * 10 * (Vthm - Vsns) / (2500 - Vthm);
		} else {
			RICOH_FG_DBG("PMU %s Skip to compensate temperature\n", __func__);
			goto out;
		}

		R_ln1 = Rthm / 10;
		R_ln2 =  (R_ln1 * R_ln1 * R_ln1 * R_ln1 * R_ln1 / 100000
			- R_ln1 * R_ln1 * R_ln1 * R_ln1 * 2 / 100
			+ R_ln1 * R_ln1 * R_ln1 * 11
			- R_ln1 * R_ln1 * 2980
			+ R_ln1 * 449800
			- 784000) / 10000;

		/* the unit of new_temp is 0.1 degree */
		new_temp = (int)((100 * 1000 * B_VALUE / (R_ln2 + B_VALUE * 100 * 1000 / 29815) - 27315) / 10);
		RICOH_FG_DBG("PMU %s low temperature %d\n", __func__, new_temp/10);  
	} else if (temp > 520000) {
		/* High Temperature */
		if (0 != (2500 - Vthm)) {
			Rthm = 100 * 10 * (Vthm - Vsns) / (2500 - Vthm);
		} else {
			RICOH_FG_DBG("PMU %s Skip to compensate temperature\n", __func__);
			goto out;
		}
		RICOH_FG_DBG("PMU %s [Rthm] Rthm %d[ohm]\n", __func__, Rthm);

		R_ln1 = Rthm / 10;
		R_ln2 =  (R_ln1 * R_ln1 * R_ln1 * R_ln1 * R_ln1 / 100000 * 15652 / 100
			- R_ln1 * R_ln1 * R_ln1 * R_ln1 / 1000 * 23103 / 100
			+ R_ln1 * R_ln1 * R_ln1 * 1298 / 100
			- R_ln1 * R_ln1 * 35089 / 100
			+ R_ln1 * 50334 / 10
			- 48569) / 100;
		/* the unit of new_temp is 0.1 degree */
		new_temp = (int)((100 * 100 * B_VALUE / (R_ln2 + B_VALUE * 100 * 100 / 29815) - 27315) / 10);
		RICOH_FG_DBG("PMU %s high temperature %d\n", __func__, new_temp/10);  
	} else {
		/* the unit of new_temp is 0.1 degree */
		new_temp = temp / 1000;
	}

	return new_temp;

out:
	new_temp = get_battery_temp(info);
	return new_temp;
}
#if 0
static int get_time_to_empty(struct ricoh619_battery_info *info)
{
	int ret = 0;

	ret = get_check_fuel_gauge_reg(info, TT_EMPTY_H_REG, TT_EMPTY_L_REG,
								0xffff);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the fuel gauge control register\n");
		return ret;
	}

	/* conversion unit */
	/* 1unit is 1miniute and return nnit should be 1 second */
	ret = ret * 60;

	return ret;
}

static int get_time_to_full(struct ricoh619_battery_info *info)
{
	int ret = 0;

	ret = get_check_fuel_gauge_reg(info, TT_FULL_H_REG, TT_FULL_L_REG,
								0xffff);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the fuel gauge control register\n");
		return ret;
	}

	ret = ret * 60;

	return  ret;
}
#endif
/* battery voltage is get from Fuel gauge */
static int measure_vbatt_FG(struct ricoh619_battery_info *info, int *data)
{
	int ret = 0;

	if(info->soca->ready_fg == 1) {
		ret = get_check_fuel_gauge_reg(info, VOLTAGE_1_REG, VOLTAGE_2_REG,
									0x0fff);
		if (ret < 0) {
			dev_err(info->dev, "Error in reading the fuel gauge control register\n");
			return ret;
		}

		*data = ret;
		/* conversion unit 1 Unit is 1.22mv (5000/4095 mv) */
		*data = *data * 50000 / 4095;
		/* return unit should be 1uV */
		*data = *data * 100;
		info->soca->Vbat_old = *data;
	} else {
		*data = info->soca->Vbat_old;
	}

	return ret;
}

static int measure_Ibatt_FG(struct ricoh619_battery_info *info, int *data)
{
	int ret = 0;

	ret =  get_check_fuel_gauge_reg(info, CC_AVERAGE1_REG,
						 CC_AVERAGE0_REG, 0x3fff);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the fuel gauge control register\n");
		return ret;
	}

	*data = (ret > 0x1fff) ? (ret - 0x4000) : ret;
	return ret;
}

static int get_OCV_init_Data(struct ricoh619_battery_info *info, int index)
{
	int ret = 0;
	ret =  (battery_init_para[info->num][index*2]<<8) | (battery_init_para[info->num][index*2+1]);
	return ret;
}

static int get_OCV_voltage(struct ricoh619_battery_info *info, int index)
{
	int ret = 0;
	ret =  get_OCV_init_Data(info, index);
	/* conversion unit 1 Unit is 1.22mv (5000/4095 mv) */
	ret = ret * 50000 / 4095;
	/* return unit should be 1uV */
	ret = ret * 100;
	return ret;
}

#else
/* battery voltage is get from ADC */
static int measure_vbatt_ADC(struct ricoh619_battery_info *info, int *data)
{
	int	i;
	uint8_t data_l = 0, data_h = 0;
	int ret;

	/* ADC interrupt enable */
	ret = ricoh619_set_bits(info->dev->parent, INTEN_REG, 0x08);
	if (ret < 0) {
		dev_err(info->dev, "Error in setting the control register bit\n");
		goto err;
	}

	/* enable interrupt request of single mode */
	ret = ricoh619_set_bits(info->dev->parent, EN_ADCIR3_REG, 0x01);
	if (ret < 0) {
		dev_err(info->dev, "Error in setting the control register bit\n");
		goto err;
	}

	/* single request */
	ret = ricoh619_write(info->dev->parent, ADCCNT3_REG, 0x10);
	if (ret < 0) {
		dev_err(info->dev, "Error in writing the control register\n");
		goto err;
	}

	for (i = 0; i < 5; i++) {
	usleep(1000);
		RICOH_FG_DBG("ADC conversion times: %d\n", i);
		/* read completed flag of ADC */
		ret = ricoh619_read(info->dev->parent, EN_ADCIR3_REG, &data_h);
		if (ret < 0) {
			dev_err(info->dev, "Error in reading the control register\n");
			goto err;
		}

		if (data_h & 0x01)
			goto	done;
	}

	dev_err(info->dev, "ADC conversion too long!\n");
	goto err;

done:
	ret = ricoh619_read(info->dev->parent, VBATDATAH_REG, &data_h);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the control register\n");
		goto err;
	}

	ret = ricoh619_read(info->dev->parent, VBATDATAL_REG, &data_l);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the control register\n");
		goto err;
	}

	*data = ((data_h & 0xff) << 4) | (data_l & 0x0f);
	/* conversion unit 1 Unit is 1.22mv (5000/4095 mv) */
	*data = *data * 5000 / 4095;
	/* return unit should be 1uV */
	*data = *data * 1000;

	return 0;

err:
	return -1;
} 
#endif

static int measure_vsys_ADC(struct ricoh619_battery_info *info, int *data)
{
	uint8_t data_l = 0, data_h = 0;
	int ret;

	ret = ricoh619_read(info->dev->parent, VSYSDATAH_REG, &data_h);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the control register\n");
	}

	ret = ricoh619_read(info->dev->parent, VSYSDATAL_REG, &data_l);
	if (ret < 0) {
		dev_err(info->dev, "Error in reading the control register\n");
	}

	*data = ((data_h & 0xff) << 4) | (data_l & 0x0f);
	*data = *data * 1000 * 3 * 5 / 2 / 4095;
	/* return unit should be 1uV */
	*data = *data * 1000;

	return 0;
}
/*
static void ricoh619_external_power_changed(struct power_supply *psy)
{
	struct ricoh619_battery_info *info;

	info = container_of(psy, struct ricoh619_battery_info, battery);
	queue_delayed_work(info->monitor_wqueue,
			   &info->changed_work, HZ / 2);
	return;
}
*/

static int ricoh619_batt_get_prop(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct ricoh619_battery_info *info = dev_get_drvdata(psy->dev->parent);
	int data = 0;
	int ret = 0;
	uint8_t status;

	mutex_lock(&info->lock);

	switch (psp) {

	case POWER_SUPPLY_PROP_ONLINE:
		ret = ricoh619_read(info->dev->parent, CHGSTATE_REG, &status);
		if (ret < 0) {
			dev_err(info->dev, "Error in reading the control register\n");
			mutex_unlock(&info->lock);
			return ret;
		}
		#ifdef SUPPORT_USB_CONNECT_TO_ADP
			if (psy->type == POWER_SUPPLY_TYPE_MAINS){
				if((2 == dwc_otg_check_dpdm(0)) && (status & 0x40))
					val->intval =1;
				else 
					val->intval =0;
			}
			else if (psy->type == POWER_SUPPLY_TYPE_USB){
				if((1 == dwc_otg_check_dpdm(0)) && (status & 0x40))
					val->intval =1;
				else 
					val->intval =0;
			}
		#else
			if (psy->type == POWER_SUPPLY_TYPE_MAINS)
				val->intval = (status & 0x40 ? 1 : 0);
			else if (psy->type == POWER_SUPPLY_TYPE_USB)
				val->intval = (status & 0x80 ? 1 : 0);
		#endif
		break;
	/* this setting is same as battery driver of 584 */
	case POWER_SUPPLY_PROP_STATUS:
		if(info->chg_complete_tm_ov_flag == 0)
		{
			ret = get_power_supply_Android_status(info);
			val->intval = ret;
			info->status = ret;
			/* RICOH_FG_DBG("Power Supply Status is %d\n",
							info->status); */
		}
		else
		{
			val->intval = POWER_SUPPLY_STATUS_FULL;
		}
		break;

	/* this setting is same as battery driver of 584 */
	case POWER_SUPPLY_PROP_PRESENT:
	//	val->intval = info->present;
		val->intval = 1;
		break;

	/* current voltage is get from fuel gauge */
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		/* return real vbatt Voltage */
#ifdef	ENABLE_FUEL_GAUGE_FUNCTION
		if (info->soca->ready_fg)
			ret = measure_vbatt_FG(info, &data);
		else {
			//val->intval = -EINVAL;
			data = info->cur_voltage * 1000;
			 RICOH_FG_DBG( "battery voltage is not ready\n"); 
		}
#else
		ret = measure_vbatt_ADC(info, &data);
#endif
		val->intval = data;
		/* convert unit uV -> mV */
		info->cur_voltage = data / 1000;
		
		RICOH_FG_DBG( "battery voltage is %d mV\n",
						info->cur_voltage);
		break;

#ifdef	ENABLE_FUEL_GAUGE_FUNCTION
	/* current battery capacity is get from fuel gauge */
	case POWER_SUPPLY_PROP_CAPACITY:
		if (info->entry_factory_mode){
			val->intval = 100;
			info->capacity = 100;
		} else if ((info->soca->displayed_soc < 0) || (info->cur_voltage == 0)) {
			val->intval = 50;
			info->capacity = 50;
		} else {
			if(info->chg_complete_tm_ov_flag == 1)
			{
				info->capacity = 100;
				val->intval = info->capacity;
			}
			else
			{
				info->capacity = (info->soca->displayed_soc + 50)/100;
				val->intval = info->capacity;
			}
		}
		RICOH_FG_DBG("battery capacity is %d%%\n", info->capacity); 
		break;

	/* current temperature of battery */
	case POWER_SUPPLY_PROP_TEMP:
		if (info->soca->ready_fg) {
			ret = 0;
			val->intval = get_battery_temp_2(info);
			info->battery_temp = val->intval/10;
			RICOH_FG_DBG( "battery temperature is %d degree\n", info->battery_temp);
		} else {
			val->intval = info->battery_temp * 10;
			/* RICOH_FG_DBG("battery temperature is not ready\n"); */
		}
		break;

	#if 0
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		if (info->soca->ready_fg) {
			ret = get_time_to_empty(info);
			val->intval = ret;
			info->time_to_empty = ret/60;
			RICOH_FG_DBG("time of empty battery is %d minutes\n", info->time_to_empty);
		} else {
			//val->intval = -EINVAL;
			val->intval = info->time_to_empty * 60;
			RICOH_FG_DBG("time of empty battery is %d minutes\n", info->time_to_empty);
			/* RICOH_FG_DBG( "time of empty battery is not ready\n"); */
		}
		break;

	 case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		if (info->soca->ready_fg) {
			ret = get_time_to_full(info);
			val->intval = ret;
			info->time_to_full = ret/60;
			RICOH_FG_DBG( "time of full battery is %d minutes\n", info->time_to_full);
		} else {
			//val->intval = -EINVAL;
			val->intval = info->time_to_full * 60;
			RICOH_FG_DBG( "time of full battery is %d minutes\n", info->time_to_full);
			/* RICOH_FG_DBG("time of full battery is not ready\n"); */
		}
		break;

	#endif
#endif
	 case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		ret = 0;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
 		measure_Ibatt_FG(info, &data);
		//RICOH_FG_DBG("average current xxxxxxxxxxxxxx %d \n", data);
		break;
	default:
		mutex_unlock(&info->lock);
		return -ENODEV;
	}

	mutex_unlock(&info->lock);

	return ret;
}

static enum power_supply_property ricoh619_batt_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,

#ifdef	ENABLE_FUEL_GAUGE_FUNCTION
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	//POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	//POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
#endif
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_HEALTH,
};

static enum power_supply_property ricoh619_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

struct power_supply	powerac = {
		.name = "acpwr",
		.type = POWER_SUPPLY_TYPE_MAINS,
		.properties = ricoh619_power_props,
		.num_properties = ARRAY_SIZE(ricoh619_power_props),
		.get_property = ricoh619_batt_get_prop,
};

struct power_supply	powerusb = {
		.name = "usbpwr",
		.type = POWER_SUPPLY_TYPE_USB,
		.properties = ricoh619_power_props,
		.num_properties = ARRAY_SIZE(ricoh619_power_props),
		.get_property = ricoh619_batt_get_prop,
};

#ifdef CONFIG_OF
static struct ricoh619_battery_platform_data *
ricoh619_battery_dt_init(struct platform_device *pdev)
{
	struct device_node *nproot = pdev->dev.parent->of_node;
	struct device_node *np;
	struct ricoh619_battery_platform_data *pdata;

	if (!nproot)
		return pdev->dev.platform_data;

	np = of_find_node_by_name(nproot, "battery");
	if (!np) {
		dev_err(&pdev->dev, "failed to find battery node\n");
		return NULL;
	}

	pdata = devm_kzalloc(&pdev->dev,
			sizeof(struct ricoh619_battery_platform_data),
			GFP_KERNEL);

	of_property_read_u32(np, "ricoh,monitor-time", &pdata->monitor_time);
	of_property_read_u32(np, "ricoh,alarm-vol-mv", &pdata->alarm_vol_mv);

	/* check rage of b,.attery type */
	type_n = Battery_Type();
	RICOH_FG_DBG("%s type_n=%d\n", __func__, type_n);

	switch (type_n) {
	case (0):
		of_property_read_u32(np, "ricoh,ch-vfchg", &pdata->type[0].ch_vfchg);
		of_property_read_u32(np, "ricoh,ch-vrchg", &pdata->type[0].ch_vrchg);
		of_property_read_u32(np, "ricoh,ch-vbatovset", &pdata->type[0].ch_vbatovset);
		of_property_read_u32(np, "ricoh,ch-ichg", &pdata->type[0].ch_ichg);
		of_property_read_u32(np, "ricoh,ch-ilim-adp", &pdata->type[0].ch_ilim_adp);
		of_property_read_u32(np, "ricoh,ch-ilim-usb", &pdata->type[0].ch_ilim_usb);
		of_property_read_u32(np, "ricoh,ch-icchg", &pdata->type[0].ch_icchg);
		of_property_read_u32(np, "ricoh,fg-target-vsys", &pdata->type[0].fg_target_vsys);
		of_property_read_u32(np, "ricoh,fg-target-ibat", &pdata->type[0].fg_target_ibat);
		of_property_read_u32(np, "ricoh,fg-poff-vbat", &pdata->type[0].fg_poff_vbat);
		of_property_read_u32(np, "ricoh,jt-en", &pdata->type[0].jt_en);
		of_property_read_u32(np, "ricoh,jt-hw-sw", &pdata->type[0].jt_hw_sw);
		of_property_read_u32(np, "ricoh,jt-temp-h", &pdata->type[0].jt_temp_h);
		of_property_read_u32(np, "ricoh,jt-temp-l", &pdata->type[0].jt_temp_l);
		of_property_read_u32(np, "ricoh,jt-vfchg-h", &pdata->type[0].jt_vfchg_h);
		of_property_read_u32(np, "ricoh,jt-vfchg-l", &pdata->type[0].jt_vfchg_l);
		of_property_read_u32(np, "ricoh,jt-ichg-h", &pdata->type[0].jt_ichg_h);
		of_property_read_u32(np, "ricoh,jt-ichg-l", &pdata->type[0].jt_ichg_l);
		break;
#if 0
	case (1):
		of_property_read_u32(np, "ricoh,ch-vfchg-1", &pdata->type[1].ch_vfchg);
		of_property_read_u32(np, "ricoh,ch-vrchg-1", &pdata->type[1].ch_vrchg);
		of_property_read_u32(np, "ricoh,ch-vbatovset-1", &pdata->type[1].ch_vbatovset);
		of_property_read_u32(np, "ricoh,ch-ichg-1", &pdata->type[1].ch_ichg);
		of_property_read_u32(np, "ricoh,ch-ilim-adp-1", &pdata->type[1].ch_ilim_adp);
		of_property_read_u32(np, "ricoh,ch-ilim-usb-1", &pdata->type[1].ch_ilim_usb);
		of_property_read_u32(np, "ricoh,ch-icchg-1", &pdata->type[1].ch_icchg);
		of_property_read_u32(np, "ricoh,fg-target-vsys-1", &pdata->type[1].fg_target_vsys);
		of_property_read_u32(np, "ricoh,fg-target-ibat-1", &pdata->type[1].fg_target_ibat);
		of_property_read_u32(np, "ricoh,fg-poff-vbat-1", &pdata->type[1].fg_poff_vbat);
		of_property_read_u32(np, "ricoh,jt-en-1", &pdata->type[1].jt_en);
		of_property_read_u32(np, "ricoh,jt-hw-sw-1", &pdata->type[1].jt_hw_sw);
		of_property_read_u32(np, "ricoh,jt-temp-h-1", &pdata->type[1].jt_temp_h);
		of_property_read_u32(np, "ricoh,jt-temp-l-1", &pdata->type[1].jt_temp_l);
		of_property_read_u32(np, "ricoh,jt-vfchg-h-1", &pdata->type[1].jt_vfchg_h);
		of_property_read_u32(np, "ricoh,jt-vfchg-l-1", &pdata->type[1].jt_vfchg_l);
		of_property_read_u32(np, "ricoh,jt-ichg-h-1", &pdata->type[1].jt_ichg_h);
		of_property_read_u32(np, "ricoh,jt-ichg-l-1", &pdata->type[1].jt_ichg_l);
		break;
#endif
	default:
		of_node_put(np);
		return 0;
	}

	of_node_put(np);

	return pdata;
}
#else
static struct ricoh619_battery_platform_data *
ricoh619_battery_dt_init(struct platform_device *pdev)
{
	return pdev->dev.platform_data;
}
#endif

static int ricoh619_battery_probe(struct platform_device *pdev)
{
	struct ricoh619_battery_info *info;
	struct ricoh619_battery_platform_data *pdata;
	struct ricoh619 *ricoh619 = dev_get_drvdata(pdev->dev.parent);
	int ret, temp;

	RICOH_FG_DBG(KERN_INFO "PMU: %s : version is %s\n", __func__,RICOH619_BATTERY_VERSION);

	pdata = ricoh619_battery_dt_init(pdev);
	if (!pdata) {
		dev_err(&pdev->dev, "platform data isn't assigned to "
			"power supply\n");
		return -EINVAL;
	}
	info = devm_kzalloc(ricoh619->dev,sizeof(struct ricoh619_battery_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->soca = devm_kzalloc(ricoh619->dev,sizeof(struct ricoh619_soca_info), GFP_KERNEL);
		if (!info->soca)
			return -ENOMEM;

	info->dev = &pdev->dev;
	info->status = POWER_SUPPLY_STATUS_CHARGING;
	info->monitor_time = pdata->monitor_time * HZ;
	info->alarm_vol_mv = pdata->alarm_vol_mv;

	/* check rage of battery num */
	info->num = Battery_Table();
	temp = sizeof(battery_init_para)/(sizeof(uint8_t)*32);
	if(info->num >= (sizeof(battery_init_para)/(sizeof(uint8_t)*32)))
	{
		RICOH_FG_DBG("%s : Battery num is out of range\n", __func__);
		info->num = 0;
	}
	RICOH_FG_DBG("%s info->num=%d,temp is %d\n", __func__, info->num,temp);

	/* these valuse are set in platform */
	info->ch_vfchg = pdata->type[type_n].ch_vfchg;
	info->ch_vrchg = pdata->type[type_n].ch_vrchg;
	info->ch_vbatovset = pdata->type[type_n].ch_vbatovset;
	info->ch_ichg = pdata->type[type_n].ch_ichg;
	info->ch_ilim_adp = pdata->type[type_n].ch_ilim_adp;
	info->ch_ilim_usb = pdata->type[type_n].ch_ilim_usb;
	info->ch_icchg = pdata->type[type_n].ch_icchg;
	info->fg_target_vsys = pdata->type[type_n].fg_target_vsys;
	info->fg_target_ibat = pdata->type[type_n].fg_target_ibat;
	info->fg_poff_vbat = pdata->type[type_n].fg_poff_vbat;
	info->jt_en = pdata->type[type_n].jt_en;
	info->jt_hw_sw = pdata->type[type_n].jt_hw_sw;
	info->jt_temp_h = pdata->type[type_n].jt_temp_h;
	info->jt_temp_l = pdata->type[type_n].jt_temp_l;
	info->jt_vfchg_h = pdata->type[type_n].jt_vfchg_h;
	info->jt_vfchg_l = pdata->type[type_n].jt_vfchg_l;
	info->jt_ichg_h = pdata->type[type_n].jt_ichg_h;
	info->jt_ichg_l = pdata->type[type_n].jt_ichg_l;

	info->adc_vdd_mv = ADC_VDD_MV;		/* 2800; */
	info->min_voltage = MIN_VOLTAGE;	/* 3100; */
	info->max_voltage = MAX_VOLTAGE;	/* 4200; */
	info->delay = 500;
	info->entry_factory_mode = false;

	info->chg_complete_rd_flag = 0;
	info->chg_complete_rd_cnt = 0;
	info->chg_complete_tm_ov_flag = 0;
	info->chg_complete_sleep_flag = 0;

	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);

	info->battery.name = "battery";
	info->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	info->battery.properties = ricoh619_batt_props;
	info->battery.num_properties = ARRAY_SIZE(ricoh619_batt_props);
	info->battery.get_property = ricoh619_batt_get_prop;
	info->battery.set_property = NULL;
/*	info->battery.external_power_changed
		 = ricoh619_external_power_changed; */

	/* Disable Charger/ADC interrupt */
	ret = ricoh619_clr_bits(info->dev->parent, RICOH619_INTC_INTEN,
							 CHG_INT | ADC_INT);
	if (ret<0)
		goto out;

	ret = ricoh619_init_battery(info);
	if (ret<0)
		goto out;
/*
#ifdef ENABLE_FACTORY_MODE
	info->factory_mode_wqueue
		= create_singlethread_workqueue("ricoh619_factory_mode");
	INIT_DEFERRABLE_WORK(&info->factory_mode_work,
					 check_charging_state_work);

	ret = ricoh619_factory_mode(info);
	if (ret<0)
		goto out;

#endif
*/
	ret = power_supply_register(&pdev->dev, &info->battery);

	if (ret<0)
		info->battery.dev->parent = &pdev->dev;

	ret = power_supply_register(&pdev->dev, &powerac);
	ret = power_supply_register(&pdev->dev, &powerusb);

	info->monitor_wqueue
		= create_singlethread_workqueue("ricoh619_battery_monitor");

	info->workqueue = create_singlethread_workqueue("rc5t619_charger_in");
	INIT_WORK(&info->irq_work, charger_irq_work);

	info->usb_workqueue
		= create_singlethread_workqueue("rc5t619_usb_det");
	INIT_WORK(&info->usb_irq_work, usb_det_irq_work);

	INIT_DEFERRABLE_WORK(&info->monitor_work,
					 ricoh619_battery_work);
	INIT_DEFERRABLE_WORK(&info->displayed_work,
					 ricoh619_displayed_work);
	INIT_DEFERRABLE_WORK(&info->charge_stable_work,
					 ricoh619_stable_charge_countdown_work);
	INIT_DEFERRABLE_WORK(&info->charge_monitor_work,
					 ricoh619_charge_monitor_work);
	INIT_DEFERRABLE_WORK(&info->get_charge_work,
					 ricoh619_get_charge_work);
	INIT_DEFERRABLE_WORK(&info->jeita_work, ricoh619_jeita_work);
	INIT_DELAYED_WORK(&info->changed_work, ricoh619_changed_work);

	INIT_DELAYED_WORK(&info->charge_complete_ready, ricoh619_charging_complete_work);

	/* Charger IRQ workqueue settings */

	ret = request_threaded_irq( irq_create_mapping(ricoh619->irq_domain, RICOH619_IRQ_FONCHGINT),NULL, charger_in_isr, IRQF_ONESHOT,
						"rc5t619_charger_in", info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Can't get CHG_INT IRQ for chrager: %d\n",ret);
		goto out;
	}

	ret = request_threaded_irq(irq_create_mapping(ricoh619->irq_domain, RICOH619_IRQ_FCHGCMPINT),NULL, charger_complete_isr,
					IRQF_ONESHOT, "rc5t619_charger_comp",info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Can't get CHG_COMP IRQ for chrager: %d\n",ret);
		goto out;
	}

	ret = request_threaded_irq(irq_create_mapping(ricoh619->irq_domain, RICOH619_IRQ_FVUSBDETSINT) ,NULL, charger_usb_isr, IRQF_ONESHOT,
						"rc5t619_usb_det", info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Can't get USB_DET IRQ for chrager: %d\n",ret);
		goto out;
	}

	ret = request_threaded_irq(irq_create_mapping(ricoh619->irq_domain, RICOH619_IRQ_FVADPDETSINT),NULL, charger_adp_isr, IRQF_ONESHOT,
						"rc5t619_adp_det", info);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Can't get ADP_DET IRQ for chrager: %d\n", ret);
		goto out;
	}

#ifdef ENABLE_LOW_BATTERY_DETECTION
	ret = request_threaded_irq(irq_create_mapping(ricoh619->irq_domain, RICOH619_IRQ_VSYSLIR) ,NULL, adc_vsysl_isr, IRQF_ONESHOT,
						"rc5t619_adc_vsysl", info);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Can't get ADC_VSYSL IRQ for chrager: %d\n", ret);
		goto out;
	}
	INIT_DEFERRABLE_WORK(&info->low_battery_work,
					 low_battery_irq_work);
#endif
#ifdef RICOH619_VADP_DROP_WORK
	INIT_DEFERRABLE_WORK(&info->vadp_drop_work,vadp_drop_irq_work);
	queue_delayed_work(info->monitor_wqueue, &info->vadp_drop_work,0);
#endif
	/* Charger init and IRQ setting */
	ret = ricoh619_init_charger(info);
	if (ret<0)
		goto out;

#ifdef	ENABLE_FUEL_GAUGE_FUNCTION
	ret = ricoh619_init_fgsoca(info);
#endif
	queue_delayed_work(info->monitor_wqueue, &info->monitor_work,
					RICOH619_MONITOR_START_TIME*HZ);

	/* Enable Charger/ADC interrupt */
	ricoh619_set_bits(info->dev->parent, RICOH619_INTC_INTEN, CHG_INT | ADC_INT);

	return 0;

out:
	return ret;
}

static int ricoh619_battery_remove(struct platform_device *pdev)
{
	struct ricoh619_battery_info *info = platform_get_drvdata(pdev);
	struct ricoh619 *ricoh619 = dev_get_drvdata(pdev->dev.parent);
	uint8_t val;
	int ret;
	int err;
	int cc_cap = 0;
	bool is_charging = true;
#ifdef ENABLE_FUEL_GAUGE_FUNCTION
	if (g_fg_on_mode
		 && (info->soca->status == RICOH619_SOCA_STABLE)) {
		err = ricoh619_write(info->dev->parent, PSWR_REG, 0x7f);
		if (err < 0)
			dev_err(info->dev, "Error in writing PSWR_REG\n");
		g_soc = 0x7f;
	} else if (info->soca->status != RICOH619_SOCA_START
		&& info->soca->status != RICOH619_SOCA_UNSTABLE) {
		if (info->soca->displayed_soc <= 0) {
			val = 1;
		} else {
			val = (info->soca->displayed_soc + 50)/100;
			val &= 0x7f;
		}
		ret = ricoh619_write(info->dev->parent, PSWR_REG, val);
		if (ret < 0)
			dev_err(info->dev, "Error in writing PSWR_REG\n");

		g_soc = val;

		ret = calc_capacity_in_period(info, &cc_cap,
							 &is_charging, true);
		if (ret < 0)
			dev_err(info->dev, "Read cc_sum Error !!-----\n");
	}

	if (g_fg_on_mode == 0) {
		ret = ricoh619_clr_bits(info->dev->parent,
					 FG_CTRL_REG, 0x01);
		if (ret < 0)
			dev_err(info->dev, "Error in clr FG EN\n");
	}
	
	/* set rapid timer 300 min */
	err = ricoh619_set_bits(info->dev->parent, TIMSET_REG, 0x03);
	if (err < 0) {
		dev_err(info->dev, "Error in writing the control register\n");
	}

	if(info->capacity == 100)
	{
		ret = ricoh619_write(info->dev->parent, PSWR_REG, 100);
		if (ret < 0)
			dev_err(info->dev, "Error in writing PSWR_REG\n");
	}
	
	free_irq(irq_create_mapping(ricoh619->irq_domain, RICOH619_IRQ_FONCHGINT), &info);
	free_irq(irq_create_mapping(ricoh619->irq_domain, RICOH619_IRQ_FCHGCMPINT), &info);
	free_irq(irq_create_mapping(ricoh619->irq_domain, RICOH619_IRQ_FVUSBDETSINT), &info);
	free_irq(irq_create_mapping(ricoh619->irq_domain, RICOH619_IRQ_FVADPDETSINT) , &info);
#ifdef ENABLE_LOW_BATTERY_DETECTION
	free_irq(irq_create_mapping(ricoh619->irq_domain, RICOH619_IRQ_VSYSLIR), &info);
#endif

	cancel_delayed_work(&info->monitor_work);
	cancel_delayed_work(&info->charge_stable_work);
	cancel_delayed_work(&info->charge_monitor_work);
	cancel_delayed_work(&info->get_charge_work);
	cancel_delayed_work(&info->displayed_work);
#endif
	cancel_delayed_work(&info->changed_work);
#ifdef ENABLE_LOW_BATTERY_DETECTION
	cancel_delayed_work(&info->low_battery_work);
#endif
#ifdef RICOH619_VADP_DROP_WORK
	cancel_delayed_work(&info->vadp_drop_work);
#endif
#ifdef ENABLE_FACTORY_MODE
	cancel_delayed_work(&info->factory_mode_work);
#endif
	cancel_delayed_work(&info->jeita_work);
	cancel_delayed_work(&info->charge_complete_ready);
	
	cancel_work_sync(&info->irq_work);
	cancel_work_sync(&info->usb_irq_work);

	flush_workqueue(info->monitor_wqueue);
	flush_workqueue(info->workqueue);
	flush_workqueue(info->usb_workqueue);
#ifdef ENABLE_FACTORY_MODE
	flush_workqueue(info->factory_mode_wqueue);
#endif
	destroy_workqueue(info->monitor_wqueue);
	destroy_workqueue(info->workqueue);
	destroy_workqueue(info->usb_workqueue);
#ifdef ENABLE_FACTORY_MODE
	destroy_workqueue(info->factory_mode_wqueue);
#endif

	power_supply_unregister(&info->battery);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_PM
struct timeval  ts_suspend;
static int ricoh619_battery_suspend(struct device *dev)
{
	struct ricoh619_battery_info *info = dev_get_drvdata(dev);
	uint8_t val;
	int ret;
	int err;
	int cc_cap = 0;
	bool is_charging = true;
	int displayed_soc_temp;
	do_gettimeofday(&ts_suspend);

	if (g_fg_on_mode
		 && (info->soca->status == RICOH619_SOCA_STABLE)) {
		err = ricoh619_write(info->dev->parent, PSWR_REG, 0x7f);
		if (err < 0)
			dev_err(info->dev, "Error in writing PSWR_REG\n");
		 g_soc = 0x7F;
		info->soca->suspend_soc = info->soca->displayed_soc;
		ret = calc_capacity_in_period(info, &cc_cap,
							 &is_charging, true);
		if (ret < 0)
			dev_err(info->dev, "Read cc_sum Error !!-----\n");

	} else if (info->soca->status != RICOH619_SOCA_START
		&& info->soca->status != RICOH619_SOCA_UNSTABLE) {
		if (info->soca->displayed_soc <= 0) {
			val = 1;
		} else {
			val = (info->soca->displayed_soc + 50)/100;
			val &= 0x7f;
		}
		ret = ricoh619_write(info->dev->parent, PSWR_REG, val);
		if (ret < 0)
			dev_err(info->dev, "Error in writing PSWR_REG\n");

		g_soc = val;

		ret = calc_capacity_in_period(info, &cc_cap,
							 &is_charging, true);
		if (ret < 0)
			dev_err(info->dev, "Read cc_sum Error !!-----\n");
			
		if (info->soca->status != RICOH619_SOCA_STABLE) {
			info->soca->cc_delta
				 = (is_charging == true) ? cc_cap : -cc_cap;

			displayed_soc_temp
			       = info->soca->displayed_soc + info->soca->cc_delta;
			displayed_soc_temp = min(10000, displayed_soc_temp);
			displayed_soc_temp = max(0, displayed_soc_temp);
			info->soca->displayed_soc = displayed_soc_temp;
		}
		info->soca->suspend_soc = info->soca->displayed_soc;
					
	} else if (info->soca->status == RICOH619_SOCA_START
		|| info->soca->status == RICOH619_SOCA_UNSTABLE) {

		ret = ricoh619_read(info->dev->parent, PSWR_REG, &val);
		if (ret < 0)
			dev_err(info->dev, "Error in reading the pswr register\n");
		val &= 0x7f;

		info->soca->suspend_soc = val * 100;
	}

	if (info->soca->status == RICOH619_SOCA_DISP
		|| info->soca->status == RICOH619_SOCA_STABLE
		|| info->soca->status == RICOH619_SOCA_FULL) {
		info->soca->soc = calc_capacity_2(info);
		info->soca->soc_delta =
			info->soca->soc_delta + (info->soca->soc - info->soca->last_soc);

	} else {
		info->soca->soc_delta = 0;
	}

	if (info->soca->status == RICOH619_SOCA_STABLE
		|| info->soca->status == RICOH619_SOCA_FULL)
		info->soca->status = RICOH619_SOCA_DISP;
	/* set rapid timer 300 min */
	err = ricoh619_set_bits(info->dev->parent, TIMSET_REG, 0x03);
	if (err < 0) {
		dev_err(info->dev, "Error in writing the control register\n");
	}

//	disable_irq(charger_irq + RICOH619_IRQ_FONCHGINT);
//	disable_irq(charger_irq + RICOH619_IRQ_FCHGCMPINT);
//	disable_irq(charger_irq + RICOH619_IRQ_FVUSBDETSINT);
//	disable_irq(charger_irq + RICOH619_IRQ_FVADPDETSINT);
#ifdef ENABLE_LOW_BATTERY_DETECTION
//	disable_irq(charger_irq + RICOH619_IRQ_VSYSLIR);
#endif
#if 0
	flush_delayed_work(&info->monitor_work);
	flush_delayed_work(&info->displayed_work);
	flush_delayed_work(&info->charge_stable_work);
	flush_delayed_work(&info->charge_monitor_work);
	flush_delayed_work(&info->get_charge_work);
	flush_delayed_work(&info->changed_work);
#ifdef ENABLE_LOW_BATTERY_DETECTION
	flush_delayed_work(&info->low_battery_work);
#endif
	flush_delayed_work(&info->factory_mode_work);
	flush_delayed_work(&info->jeita_work);
#ifdef RICOH619_VADP_DROP_WORK
	flush_delayed_work(&info->vadp_drop_work);
#endif
	
//	flush_work(&info->irq_work);
//	flush_work(&info->usb_irq_work);
#else
	cancel_delayed_work(&info->monitor_work);
	cancel_delayed_work(&info->displayed_work);
	cancel_delayed_work(&info->charge_stable_work);
	cancel_delayed_work(&info->charge_monitor_work);
	cancel_delayed_work(&info->get_charge_work);
	cancel_delayed_work(&info->changed_work);
#ifdef ENABLE_LOW_BATTERY_DETECTION
	cancel_delayed_work(&info->low_battery_work);
#endif
/*	cancel_delayed_work(&info->charge_complete_ready);*/
#ifdef ENABLE_FACTORY_MODE
	cancel_delayed_work(&info->factory_mode_work);
#endif
	cancel_delayed_work(&info->jeita_work);
#ifdef RICOH619_VADP_DROP_WORK
	cancel_delayed_work(&info->vadp_drop_work);
#endif
/*	info->chg_complete_rd_cnt = 0;*/
/*	info->chg_complete_rd_flag = 0;*/

	if(info->capacity == 100)
	{
		ret = ricoh619_write(info->dev->parent, PSWR_REG, 100);
		if (ret < 0)
			dev_err(info->dev, "Error in writing PSWR_REG\n");
		if(info->chg_complete_tm_ov_flag != 1)
		{
			info->chg_complete_tm_ov_flag = 0;
			info->chg_complete_sleep_flag = 1;
		}
	}
//	flush_work(&info->irq_work);
//	flush_work(&info->usb_irq_work);
#endif

	return 0;
}

static int ricoh619_battery_resume(struct device *dev)
{
	struct ricoh619_battery_info *info = dev_get_drvdata(dev);
	uint8_t val;
	int ret;
	int displayed_soc_temp;
	int cc_cap = 0;
	bool is_charging = true;
	bool is_jeita_updated;
	int i;
	int err;
	struct rtc_time tm;
	struct timespec tv = {
			.tv_nsec = NSEC_PER_SEC >> 1,
	};
	struct rtc_device *rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);

	err = rtc_read_time(rtc, &tm);
	if (err) {
			dev_err(rtc->dev.parent,
					"hctosys: unable to read the hardware clock\n");	
	}

	err = rtc_valid_tm(&tm);
	if (err) {
			dev_err(rtc->dev.parent,
					"hctosys: invalid date/time\n");
	}

	rtc_tm_to_time(&tm, &tv.tv_sec);
	
	/*printk("suspend time: %d sec\n", ts_suspend.tv_sec);*/
	/*printk("resume  time: %d sec\n", tv.tv_sec);*/

	if(info->chg_complete_rd_flag == 2){
		printk("chg_complete_rd_cnt suspend: %d\n", info->chg_complete_rd_cnt);
		info->chg_complete_rd_cnt += (tv.tv_sec - ts_suspend.tv_sec);
		printk("chg_complete_rd_cnt resume: %d\n", info->chg_complete_rd_cnt);
		flush_work(&info->charge_complete_ready.work);
	}

	RICOH_FG_DBG(KERN_INFO "PMU: %s: \n", __func__);

	ret = check_jeita_status(info, &is_jeita_updated);
	if (ret < 0) {
		dev_err(info->dev, "Error in updating JEITA %d\n", ret);
	}

	if (info->entry_factory_mode) {
		info->soca->displayed_soc = -EINVAL;
	} else if (RICOH619_SOCA_ZERO == info->soca->status) {
		if (calc_ocv(info) > get_OCV_voltage(info, 0)) {
			RICOH_FG_DBG(KERN_INFO "PMU: %s: RICOH619_SOCA_ZERO if ()...\n", __func__);
			ret = ricoh619_read(info->dev->parent, PSWR_REG, &val);
			val &= 0x7f;
			info->soca->soc = val * 100;
			if (ret < 0) {
				dev_err(info->dev,
					 "Error in reading PSWR_REG %d\n", ret);
				info->soca->soc
					 = calc_capacity(info) * 100;
			}

			ret = calc_capacity_in_period(info, &cc_cap,
							 &is_charging, true);
			if (ret < 0)
				dev_err(info->dev, "Read cc_sum Error !!-----\n");

			info->soca->cc_delta
				 = (is_charging == true) ? cc_cap : -cc_cap;

			displayed_soc_temp
				 = info->soca->soc + info->soca->cc_delta;
			if (displayed_soc_temp < 0)
				displayed_soc_temp = 0;
			displayed_soc_temp = min(10000, displayed_soc_temp);
			displayed_soc_temp = max(0, displayed_soc_temp);
			info->soca->displayed_soc = displayed_soc_temp;

			ret = reset_FG_process(info);

			if (ret < 0)
				dev_err(info->dev, "Error in writing the control register\n");
			info->soca->status = RICOH619_SOCA_FG_RESET;

		} else {
			RICOH_FG_DBG(KERN_INFO "PMU: %s: RICOH619_SOCA_ZERO else()...\n", __func__);
			/*info->soca->displayed_soc = 0;*/
			info->soca->displayed_soc  = info->soca->suspend_soc;
		}
	} else {
		info->soca->soc = info->soca->suspend_soc;

		if (RICOH619_SOCA_START == info->soca->status
			|| RICOH619_SOCA_UNSTABLE == info->soca->status) {
			ret = calc_capacity_in_period(info, &cc_cap,
							 &is_charging, false);
		} else { 
			ret = calc_capacity_in_period(info, &cc_cap,
							 &is_charging, true);
		}

		if (ret < 0)
			dev_err(info->dev, "Read cc_sum Error !!-----\n");

		info->soca->cc_delta = (is_charging == true) ? cc_cap : -cc_cap;

		displayed_soc_temp = info->soca->soc + info->soca->cc_delta;
		if (info->soca->zero_flg == 1) {
			if((info->soca->Ibat_ave >= 0) 
			|| (displayed_soc_temp >= 100)){
				info->soca->zero_flg = 0;
			} else {
				displayed_soc_temp = 0;
			}
		} else if (displayed_soc_temp < 100) {
			/* keep DSOC = 1 when Vbat is over 3.4V*/
			if( info->fg_poff_vbat != 0) {
				if (info->soca->Vbat_ave < 2000*1000) { /* error value */
					displayed_soc_temp = 100;
				} else if (info->soca->Vbat_ave < info->fg_poff_vbat*1000) {
					displayed_soc_temp = 0;
					info->soca->zero_flg = 1;
				} else {
					displayed_soc_temp = 100;
				}
			}
		}
		displayed_soc_temp = min(10000, displayed_soc_temp);
		displayed_soc_temp = max(0, displayed_soc_temp);

		if (0 == info->soca->jt_limit) {
			check_charge_status_2(info, displayed_soc_temp);
		} else {
			info->soca->displayed_soc = displayed_soc_temp;
		}

		if (RICOH619_SOCA_DISP == info->soca->status) {
			info->soca->last_soc = calc_capacity_2(info);
		}
	}

	ret = measure_vbatt_FG(info, &info->soca->Vbat_ave);
	ret = measure_vsys_ADC(info, &info->soca->Vsys_ave);
	ret = measure_Ibatt_FG(info, &info->soca->Ibat_ave);

	if(info->chg_complete_sleep_flag == 1)
	{
		info->chg_complete_tm_ov_flag = 1;
		info->chg_complete_sleep_flag = 0;
	}

	power_supply_changed(&info->battery);
	queue_delayed_work(info->monitor_wqueue, &info->displayed_work, HZ);

	if (RICOH619_SOCA_UNSTABLE == info->soca->status) {
		info->soca->stable_count = 10;
		queue_delayed_work(info->monitor_wqueue,
					 &info->charge_stable_work,
					 RICOH619_FG_STABLE_TIME*HZ/10);
	} else if (RICOH619_SOCA_FG_RESET == info->soca->status) {
		info->soca->stable_count = 1;

		for (i = 0; i < 3; i = i+1)
			info->soca->reset_soc[i] = 0;
		info->soca->reset_count = 0;

		queue_delayed_work(info->monitor_wqueue,
					 &info->charge_stable_work,
					 RICOH619_FG_RESET_TIME*HZ);
	}

	queue_delayed_work(info->monitor_wqueue, &info->monitor_work,
						 info->monitor_time);

	queue_delayed_work(info->monitor_wqueue, &info->charge_monitor_work,
					 RICOH619_CHARGE_RESUME_TIME * HZ);

	info->soca->chg_count = 0;
	queue_delayed_work(info->monitor_wqueue, &info->get_charge_work,
					 RICOH619_CHARGE_RESUME_TIME * HZ);
	#ifdef RICOH619_VADP_DROP_WORK
	queue_delayed_work(info->monitor_wqueue, &info->vadp_drop_work,1 * HZ);
	#endif
	if (info->jt_en) {
		if (!info->jt_hw_sw) {
			queue_delayed_work(info->monitor_wqueue, &info->jeita_work,
					 RICOH619_JEITA_UPDATE_TIME * HZ);
		}
	}
//	ricoh619_write(info->dev->parent, 0x9d, 0x00);
//	enable_irq(charger_irq + RICOH619_IRQ_FONCHGINT);
//	enable_irq(charger_irq + RICOH619_IRQ_FCHGCMPINT);
//	enable_irq(charger_irq + RICOH619_IRQ_FVUSBDETSINT);
//	enable_irq(charger_irq + RICOH619_IRQ_FVADPDETSINT);
#ifdef ENABLE_LOW_BATTERY_DETECTION
//	enable_irq(charger_irq + RICOH619_IRQ_VSYSLIR);
#endif
	ricoh619_write(info->dev->parent, 0x9d, 0x4d);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ricoh619_battery_dt_match[] = {
	{ .compatible = "ricoh,ricoh619-battery", },
	{},
};
MODULE_DEVICE_TABLE(of, ricoh619_battery_dt_match);
#endif


static const struct dev_pm_ops ricoh619_battery_pm_ops = {
	.suspend	= ricoh619_battery_suspend,
	.resume		= ricoh619_battery_resume,
};
#endif

static struct platform_driver ricoh619_battery_driver = {
	.driver	= {
				.name	= "ricoh619-battery",
				.owner	= THIS_MODULE,
				.of_match_table = of_match_ptr(ricoh619_battery_dt_match),
#ifdef CONFIG_PM
				.pm	= &ricoh619_battery_pm_ops,
#endif
	},
	.probe	= ricoh619_battery_probe,
	.remove	= ricoh619_battery_remove,
};

static int __init ricoh619_battery_init(void)
{
	RICOH_FG_DBG("PMU: %s\n", __func__);
	return platform_driver_register(&ricoh619_battery_driver);
}
fs_initcall_sync(ricoh619_battery_init);

static void __exit ricoh619_battery_exit(void)
{
	platform_driver_unregister(&ricoh619_battery_driver);
}
module_exit(ricoh619_battery_exit);

MODULE_DESCRIPTION("RICOH619 Battery driver");
MODULE_ALIAS("platform:ricoh619-battery");
MODULE_LICENSE("GPL");
