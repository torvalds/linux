/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _AB8500_CHARGER_H_
#define _AB8500_CHARGER_H_

#include <linux/kernel.h>

/*
 * System control 2 register offsets.
 * bank = 0x02
 */
#define AB8500_MAIN_WDOG_CTRL_REG	0x01
#define AB8500_LOW_BAT_REG		0x03
#define AB8500_BATT_OK_REG		0x04
/*
 * USB/ULPI register offsets
 * Bank : 0x5
 */
#define AB8500_USB_LINE_STAT_REG	0x80
#define AB8500_USB_LINE_CTRL2_REG	0x82
#define AB8500_USB_LINK1_STAT_REG	0x94

/*
 * Charger / status register offfsets
 * Bank : 0x0B
 */
#define AB8500_CH_STATUS1_REG		0x00
#define AB8500_CH_STATUS2_REG		0x01
#define AB8500_CH_USBCH_STAT1_REG	0x02
#define AB8500_CH_USBCH_STAT2_REG	0x03
#define AB8540_CH_USBCH_STAT3_REG	0x04
#define AB8500_CH_STAT_REG		0x05

/*
 * Charger / control register offfsets
 * Bank : 0x0B
 */
#define AB8500_CH_VOLT_LVL_REG		0x40
#define AB8500_CH_VOLT_LVL_MAX_REG	0x41  /*Only in Cut2.0*/
#define AB8500_CH_OPT_CRNTLVL_REG	0x42
#define AB8500_CH_OPT_CRNTLVL_MAX_REG	0x43  /*Only in Cut2.0*/
#define AB8500_CH_WD_TIMER_REG		0x50
#define AB8500_CHARG_WD_CTRL		0x51
#define AB8500_BTEMP_HIGH_TH		0x52
#define AB8500_LED_INDICATOR_PWM_CTRL	0x53
#define AB8500_LED_INDICATOR_PWM_DUTY	0x54
#define AB8500_BATT_OVV			0x55
#define AB8500_CHARGER_CTRL		0x56
#define AB8500_BAT_CTRL_CURRENT_SOURCE	0x60  /*Only in Cut2.0*/

/*
 * Charger / main control register offsets
 * Bank : 0x0B
 */
#define AB8500_MCH_CTRL1		0x80
#define AB8500_MCH_CTRL2		0x81
#define AB8500_MCH_IPT_CURLVL_REG	0x82
#define AB8500_CH_WD_REG		0x83

/*
 * Charger / USB control register offsets
 * Bank : 0x0B
 */
#define AB8500_USBCH_CTRL1_REG		0xC0
#define AB8500_USBCH_CTRL2_REG		0xC1
#define AB8500_USBCH_IPT_CRNTLVL_REG	0xC2
#define AB8540_USB_PP_MODE_REG		0xC5
#define AB8540_USB_PP_CHR_REG		0xC6

/*
 * Gas Gauge register offsets
 * Bank : 0x0C
 */
#define AB8500_GASG_CC_CTRL_REG		0x00
#define AB8500_GASG_CC_ACCU1_REG	0x01
#define AB8500_GASG_CC_ACCU2_REG	0x02
#define AB8500_GASG_CC_ACCU3_REG	0x03
#define AB8500_GASG_CC_ACCU4_REG	0x04
#define AB8500_GASG_CC_SMPL_CNTRL_REG	0x05
#define AB8500_GASG_CC_SMPL_CNTRH_REG	0x06
#define AB8500_GASG_CC_SMPL_CNVL_REG	0x07
#define AB8500_GASG_CC_SMPL_CNVH_REG	0x08
#define AB8500_GASG_CC_CNTR_AVGOFF_REG	0x09
#define AB8500_GASG_CC_OFFSET_REG	0x0A
#define AB8500_GASG_CC_NCOV_ACCU	0x10
#define AB8500_GASG_CC_NCOV_ACCU_CTRL	0x11
#define AB8500_GASG_CC_NCOV_ACCU_LOW	0x12
#define AB8500_GASG_CC_NCOV_ACCU_MED	0x13
#define AB8500_GASG_CC_NCOV_ACCU_HIGH	0x14

/*
 * Interrupt register offsets
 * Bank : 0x0E
 */
#define AB8500_IT_SOURCE2_REG		0x01
#define AB8500_IT_SOURCE21_REG		0x14

/*
 * RTC register offsets
 * Bank: 0x0F
 */
#define AB8500_RTC_BACKUP_CHG_REG	0x0C
#define AB8500_RTC_CC_CONF_REG		0x01
#define AB8500_RTC_CTRL_REG		0x0B
#define AB8500_RTC_CTRL1_REG		0x11

/*
 * OTP register offsets
 * Bank : 0x15
 */
#define AB8500_OTP_CONF_15		0x0E

/* GPADC constants from AB8500 spec, UM0836 */
#define ADC_RESOLUTION			1024
#define ADC_CH_MAIN_MIN			0
#define ADC_CH_MAIN_MAX			20030
#define ADC_CH_VBUS_MIN			0
#define ADC_CH_VBUS_MAX			20030
#define ADC_CH_VBAT_MIN			2300
#define ADC_CH_VBAT_MAX			4800
#define ADC_CH_BKBAT_MIN		0
#define ADC_CH_BKBAT_MAX		3200

/* Main charge i/p current */
#define MAIN_CH_IP_CUR_0P9A		0x80
#define MAIN_CH_IP_CUR_1P0A		0x90
#define MAIN_CH_IP_CUR_1P1A		0xA0
#define MAIN_CH_IP_CUR_1P2A		0xB0
#define MAIN_CH_IP_CUR_1P3A		0xC0
#define MAIN_CH_IP_CUR_1P4A		0xD0
#define MAIN_CH_IP_CUR_1P5A		0xE0

/* ChVoltLevel */
#define CH_VOL_LVL_3P5			0x00
#define CH_VOL_LVL_4P0			0x14
#define CH_VOL_LVL_4P05			0x16
#define CH_VOL_LVL_4P1			0x1B
#define CH_VOL_LVL_4P15			0x20
#define CH_VOL_LVL_4P2			0x25
#define CH_VOL_LVL_4P6			0x4D

/* ChOutputCurrentLevel */
#define CH_OP_CUR_LVL_0P1		0x00
#define CH_OP_CUR_LVL_0P2		0x01
#define CH_OP_CUR_LVL_0P3		0x02
#define CH_OP_CUR_LVL_0P4		0x03
#define CH_OP_CUR_LVL_0P5		0x04
#define CH_OP_CUR_LVL_0P6		0x05
#define CH_OP_CUR_LVL_0P7		0x06
#define CH_OP_CUR_LVL_0P8		0x07
#define CH_OP_CUR_LVL_0P9		0x08
#define CH_OP_CUR_LVL_1P4		0x0D
#define CH_OP_CUR_LVL_1P5		0x0E
#define CH_OP_CUR_LVL_1P6		0x0F
#define CH_OP_CUR_LVL_2P		0x3F

/* BTEMP High thermal limits */
#define BTEMP_HIGH_TH_57_0		0x00
#define BTEMP_HIGH_TH_52		0x01
#define BTEMP_HIGH_TH_57_1		0x02
#define BTEMP_HIGH_TH_62		0x03

#define LOW_BAT_3P1V			0x20
#define LOW_BAT_2P3V			0x00
#define LOW_BAT_RESET			0x01
#define LOW_BAT_ENABLE			0x01

/* Backup battery constants */
#define BUP_ICH_SEL_50UA		0x00
#define BUP_ICH_SEL_150UA		0x04
#define BUP_ICH_SEL_300UA		0x08
#define BUP_ICH_SEL_700UA		0x0C

enum bup_vch_sel {
	BUP_VCH_SEL_2P5V,
	BUP_VCH_SEL_2P6V,
	BUP_VCH_SEL_2P8V,
	BUP_VCH_SEL_3P1V,
	/*
	 * Note that the following 5 values 2.7v, 2.9v, 3.0v, 3.2v, 3.3v
	 * are only available on ab8540. You can't choose these 5
	 * voltage on ab8500/ab8505/ab9540.
	 */
	BUP_VCH_SEL_2P7V,
	BUP_VCH_SEL_2P9V,
	BUP_VCH_SEL_3P0V,
	BUP_VCH_SEL_3P2V,
	BUP_VCH_SEL_3P3V,
};

#define BUP_VCH_RANGE		0x02
#define VBUP33_VRTCN		0x01

/* Battery OVV constants */
#define BATT_OVV_ENA			0x02
#define BATT_OVV_TH_3P7			0x00
#define BATT_OVV_TH_4P75		0x01

/* A value to indicate over voltage (microvolts) */
#define BATT_OVV_VALUE			4750000

/* VBUS OVV constants */
#define VBUS_OVV_SELECT_MASK		0x78
#define VBUS_OVV_SELECT_5P6V		0x00
#define VBUS_OVV_SELECT_5P7V		0x08
#define VBUS_OVV_SELECT_5P8V		0x10
#define VBUS_OVV_SELECT_5P9V		0x18
#define VBUS_OVV_SELECT_6P0V		0x20
#define VBUS_OVV_SELECT_6P1V		0x28
#define VBUS_OVV_SELECT_6P2V		0x30
#define VBUS_OVV_SELECT_6P3V		0x38

#define VBUS_AUTO_IN_CURR_LIM_ENA	0x04

/* Fuel Gauge constants */
#define RESET_ACCU			0x02
#define READ_REQ			0x01
#define CC_DEEP_SLEEP_ENA		0x02
#define CC_PWR_UP_ENA			0x01
#define CC_SAMPLES_40			0x28
#define RD_NCONV_ACCU_REQ		0x01
#define CC_CALIB			0x08
#define CC_INTAVGOFFSET_ENA		0x10
#define CC_MUXOFFSET			0x80
#define CC_INT_CAL_N_AVG_MASK		0x60
#define CC_INT_CAL_SAMPLES_16		0x40
#define CC_INT_CAL_SAMPLES_8		0x20
#define CC_INT_CAL_SAMPLES_4		0x00

/* RTC constants */
#define RTC_BUP_CH_ENA			0x10

/* BatCtrl Current Source Constants */
#define BAT_CTRL_7U_ENA			0x01
#define BAT_CTRL_20U_ENA		0x02
#define BAT_CTRL_18U_ENA		0x01
#define BAT_CTRL_16U_ENA		0x02
#define BAT_CTRL_CMP_ENA		0x04
#define FORCE_BAT_CTRL_CMP_HIGH		0x08
#define BAT_CTRL_PULL_UP_ENA		0x10

/* Battery type */
#define BATTERY_UNKNOWN			00

/* Registers for pcut feature in ab8505 and ab9540 */
#define AB8505_RTC_PCUT_CTL_STATUS_REG	0x12
#define AB8505_RTC_PCUT_TIME_REG	0x13
#define AB8505_RTC_PCUT_MAX_TIME_REG	0x14
#define AB8505_RTC_PCUT_FLAG_TIME_REG	0x15
#define AB8505_RTC_PCUT_RESTART_REG	0x16
#define AB8505_RTC_PCUT_DEBOUNCE_REG	0x17

/* USB Power Path constants for ab8540 */
#define BUS_VSYS_VOL_SELECT_MASK		0x06
#define BUS_VSYS_VOL_SELECT_3P6V		0x00
#define BUS_VSYS_VOL_SELECT_3P325V		0x02
#define BUS_VSYS_VOL_SELECT_3P9V		0x04
#define BUS_VSYS_VOL_SELECT_4P3V		0x06
#define BUS_POWER_PATH_MODE_ENA			0x01
#define BUS_PP_PRECHG_CURRENT_MASK		0x0E
#define BUS_POWER_PATH_PRECHG_ENA		0x01

/*
 * ADC for the battery thermistor.
 * When using the AB8500_ADC_THERM_BATCTRL the battery ID resistor is combined
 * with a NTC resistor to both identify the battery and to measure its
 * temperature. Different phone manufactures uses different techniques to both
 * identify the battery and to read its temperature.
 */
enum ab8500_adc_therm {
	AB8500_ADC_THERM_BATCTRL,
	AB8500_ADC_THERM_BATTEMP,
};

/**
 * struct ab8500_res_to_temp - defines one point in a temp to res curve. To
 * be used in battery packs that combines the identification resistor with a
 * NTC resistor.
 * @temp:			battery pack temperature in Celsius
 * @resist:			NTC resistor net total resistance
 */
struct ab8500_res_to_temp {
	int temp;
	int resist;
};

/* Forward declaration */
struct ab8500_fg;

/**
 * struct ab8500_fg_parameters - Fuel gauge algorithm parameters, in seconds
 * if not specified
 * @recovery_sleep_timer:	Time between measurements while recovering
 * @recovery_total_time:	Total recovery time
 * @init_timer:			Measurement interval during startup
 * @init_discard_time:		Time we discard voltage measurement at startup
 * @init_total_time:		Total init time during startup
 * @high_curr_time:		Time current has to be high to go to recovery
 * @accu_charging:		FG accumulation time while charging
 * @accu_high_curr_ua:		FG accumulation time in high current mode
 * @high_curr_threshold_ua:	High current threshold, in uA
 * @lowbat_threshold_uv:	Low battery threshold, in uV
 * @battok_falling_th_sel0	Threshold in mV for battOk signal sel0
 *				Resolution in 50 mV step.
 * @battok_raising_th_sel1	Threshold in mV for battOk signal sel1
 *				Resolution in 50 mV step.
 * @user_cap_limit		Capacity reported from user must be within this
 *				limit to be considered as sane, in percentage
 *				points.
 * @maint_thres			This is the threshold where we stop reporting
 *				battery full while in maintenance, in per cent
 * @pcut_enable:			Enable power cut feature in ab8505
 * @pcut_max_time:		Max time threshold
 * @pcut_flag_time:		Flagtime threshold
 * @pcut_max_restart:		Max number of restarts
 * @pcut_debounce_time:		Sets battery debounce time
 */
struct ab8500_fg_parameters {
	int recovery_sleep_timer;
	int recovery_total_time;
	int init_timer;
	int init_discard_time;
	int init_total_time;
	int high_curr_time;
	int accu_charging;
	int accu_high_curr;
	int high_curr_threshold_ua;
	int lowbat_threshold_uv;
	int battok_falling_th_sel0;
	int battok_raising_th_sel1;
	int user_cap_limit;
	int maint_thres;
	bool pcut_enable;
	u8 pcut_max_time;
	u8 pcut_flag_time;
	u8 pcut_max_restart;
	u8 pcut_debounce_time;
};

/**
 * struct ab8500_charger_maximization - struct used by the board config.
 * @use_maxi:		Enable maximization for this battery type
 * @maxi_chg_curr_ua:	Maximum charger current allowed in microampere
 * @maxi_wait_cycles:	cycles to wait before setting charger current
 * @charger_curr_step_ua: delta between two charger current settings (uA)
 */
struct ab8500_maxim_parameters {
	bool ena_maxi;
	int chg_curr_ua;
	int wait_cycles;
	int charger_curr_step_ua;
};

/**
 * struct ab8500_battery_type - different batteries supported
 * @resis_high:			battery upper resistance limit
 * @resis_low:			battery lower resistance limit
 * @maint_a_cur_lvl:		charger current in maintenance A state in mA
 * @maint_a_vol_lvl:		charger voltage in maintenance A state in mV
 * @maint_a_chg_timer_h:	charge time in maintenance A state
 * @maint_b_cur_lvl:		charger current in maintenance B state in mA
 * @maint_b_vol_lvl:		charger voltage in maintenance B state in mV
 * @maint_b_chg_timer_h:	charge time in maintenance B state
 * @low_high_cur_lvl:		charger current in temp low/high state in mA
 * @low_high_vol_lvl:		charger voltage in temp low/high state in mV'
 * @n_r_t_tbl_elements:		number of elements in r_to_t_tbl
 * @r_to_t_tbl:			table containing resistance to temp points
 */
struct ab8500_battery_type {
	int resis_high;
	int resis_low;
	int maint_a_cur_lvl;
	int maint_a_vol_lvl;
	int maint_a_chg_timer_h;
	int maint_b_cur_lvl;
	int maint_b_vol_lvl;
	int maint_b_chg_timer_h;
	int low_high_cur_lvl;
	int low_high_vol_lvl;
	int n_temp_tbl_elements;
	const struct ab8500_res_to_temp *r_to_t_tbl;
};

/**
 * struct ab8500_bm_capacity_levels - ab8500 capacity level data
 * @critical:		critical capacity level in percent
 * @low:		low capacity level in percent
 * @normal:		normal capacity level in percent
 * @high:		high capacity level in percent
 * @full:		full capacity level in percent
 */
struct ab8500_bm_capacity_levels {
	int critical;
	int low;
	int normal;
	int high;
	int full;
};

/**
 * struct ab8500_bm_charger_parameters - Charger specific parameters
 * @usb_volt_max_uv:	maximum allowed USB charger voltage in uV
 * @usb_curr_max_ua:	maximum allowed USB charger current in uA
 * @ac_volt_max_uv:	maximum allowed AC charger voltage in uV
 * @ac_curr_max_ua:	maximum allowed AC charger current in uA
 */
struct ab8500_bm_charger_parameters {
	int usb_volt_max_uv;
	int usb_curr_max_ua;
	int ac_volt_max_uv;
	int ac_curr_max_ua;
};

/**
 * struct ab8500_bm_data - ab8500 battery management data
 * @bi			battery info from device tree
 * @temp_now		present battery temperature
 * @temp_interval_chg	temperature measurement interval in s when charging
 * @temp_interval_nochg	temperature measurement interval in s when not charging
 * @main_safety_tmr_h	safety timer for main charger
 * @usb_safety_tmr_h	safety timer for usb charger
 * @bkup_bat_v		voltage which we charge the backup battery with
 * @bkup_bat_i		current which we charge the backup battery with
 * @no_maintenance	indicates that maintenance charging is disabled
 * @capacity_scaling    indicates whether capacity scaling is to be used
 * @ab8500_adc_therm	placement of thermistor, batctrl or battemp adc
 * @chg_unknown_bat	flag to enable charging of unknown batteries
 * @enable_overshoot	flag to enable VBAT overshoot control
 * @auto_trig		flag to enable auto adc trigger
 * @fg_res		resistance of FG resistor in 0.1mOhm
 * @interval_charging	charge alg cycle period time when charging (sec)
 * @interval_not_charging charge alg cycle period time when not charging (sec)
 * @temp_hysteresis	temperature hysteresis
 * @gnd_lift_resistance	Battery ground to phone ground resistance (mOhm)
 * @maxi		maximization parameters
 * @cap_levels		capacity in percent for the different capacity levels
 * @bat_type		table of supported battery types
 * @chg_params		charger parameters
 * @fg_params		fuel gauge parameters
 */
struct ab8500_bm_data {
	struct power_supply_battery_info *bi;
	int temp_now;
	int temp_interval_chg;
	int temp_interval_nochg;
	int main_safety_tmr_h;
	int usb_safety_tmr_h;
	int bkup_bat_v;
	int bkup_bat_i;
	bool no_maintenance;
	bool capacity_scaling;
	bool chg_unknown_bat;
	bool enable_overshoot;
	bool auto_trig;
	enum ab8500_adc_therm adc_therm;
	int fg_res;
	int interval_charging;
	int interval_not_charging;
	int temp_hysteresis;
	int gnd_lift_resistance;
	const struct ab8500_maxim_parameters *maxi;
	const struct ab8500_bm_capacity_levels *cap_levels;
	struct ab8500_battery_type *bat_type;
	const struct ab8500_bm_charger_parameters *chg_params;
	const struct ab8500_fg_parameters *fg_params;
};

enum {
	NTC_EXTERNAL = 0,
	NTC_INTERNAL,
};

/**
 * struct res_to_temp - defines one point in a temp to res curve. To
 * be used in battery packs that combines the identification resistor with a
 * NTC resistor.
 * @temp:			battery pack temperature in Celsius
 * @resist:			NTC resistor net total resistance
 */
struct res_to_temp {
	int temp;
	int resist;
};

/* Forward declaration */
struct ab8500_fg;

extern struct ab8500_bm_data ab8500_bm_data;

void ab8500_charger_usb_state_changed(u8 bm_usb_state, u16 mA);
struct ab8500_fg *ab8500_fg_get(void);
int ab8500_fg_inst_curr_blocking(struct ab8500_fg *dev);
int ab8500_fg_inst_curr_start(struct ab8500_fg *di);
int ab8500_fg_inst_curr_finalize(struct ab8500_fg *di, int *res);
int ab8500_fg_inst_curr_started(struct ab8500_fg *di);
int ab8500_fg_inst_curr_done(struct ab8500_fg *di);
int ab8500_bm_of_probe(struct power_supply *psy,
		       struct ab8500_bm_data *bm);
void ab8500_bm_of_remove(struct power_supply *psy,
			 struct ab8500_bm_data *bm);

extern struct platform_driver ab8500_fg_driver;
extern struct platform_driver ab8500_btemp_driver;
extern struct platform_driver ab8500_chargalg_driver;

#endif /* _AB8500_CHARGER_H_ */
