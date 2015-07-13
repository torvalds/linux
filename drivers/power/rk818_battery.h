/*
*rk818-battery.h - Battery fuel gauge driver structures
 *
 */
#ifndef RK818_BATTERY
#define  RK818_BATTERY

#define VB_MOD_REG					0x21
#define THERMAL_REG					0x22
#define DCDC_EN_REG					0x23
#define NT_STS_MSK_REG2				0x4f
#define DCDC_ILMAX_REG				0x90
#define CHRG_COMP_REG1				0x99
#define CHRG_COMP_REG2				0x9A
#define SUP_STS_REG					0xA0
#define USB_CTRL_REG				0xA1
#define CHRG_CTRL_REG1				0xA3
#define CHRG_CTRL_REG2				0xA4
#define CHRG_CTRL_REG3				0xA5
#define BAT_CTRL_REG				0xA6
#define BAT_HTS_TS1_REG			0xA8
#define BAT_LTS_TS1_REG			0xA9
#define BAT_HTS_TS2_REG			0xAA
#define BAT_LTS_TS2_REG			0xAB


#define TS_CTRL_REG					0xAC
#define ADC_CTRL_REG				0xAD

#define ON_SOURCE					0xAE
#define OFF_SOURCE					0xAF

#define GGCON						0xB0
#define GGSTS						0xB1
#define FRAME_SMP_INTERV_REG		0xB2
#define AUTO_SLP_CUR_THR_REG		0xB3

#define GASCNT_CAL_REG3			0xB4
#define GASCNT_CAL_REG2			0xB5
#define GASCNT_CAL_REG1			0xB6
#define GASCNT_CAL_REG0			0xB7
#define GASCNT3						0xB8
#define GASCNT2						0xB9
#define GASCNT1						0xBA
#define GASCNT0						0xBB

#define BAT_CUR_AVG_REGH			0xBC
#define BAT_CUR_AVG_REGL			0xBD

#define TS1_ADC_REGH				0xBE
#define TS1_ADC_REGL				0xBF
#define TS2_ADC_REGH				0xC0
#define TS2_ADC_REGL				0xC1

#define BAT_OCV_REGH				0xC2
#define BAT_OCV_REGL				0xC3
#define BAT_VOL_REGH				0xC4
#define BAT_VOL_REGL				0xC5

#define RELAX_ENTRY_THRES_REGH	0xC6
#define RELAX_ENTRY_THRES_REGL	0xC7
#define RELAX_EXIT_THRES_REGH		0xC8
#define RELAX_EXIT_THRES_REGL		0xC9

#define RELAX_VOL1_REGH			0xCA
#define RELAX_VOL1_REGL			0xCB
#define RELAX_VOL2_REGH			0xCC
#define RELAX_VOL2_REGL			0xCD

#define BAT_CUR_R_CALC_REGH		0xCE
#define BAT_CUR_R_CALC_REGL		0xCF
#define BAT_VOL_R_CALC_REGH		0xD0
#define BAT_VOL_R_CALC_REGL		0xD1

#define CAL_OFFSET_REGH			0xD2
#define CAL_OFFSET_REGL			0xD3

#define NON_ACT_TIMER_CNT_REG	0xD4

#define VCALIB0_REGH				0xD5
#define VCALIB0_REGL				0xD6
#define VCALIB1_REGH				0xD7
#define VCALIB1_REGL				0xD8

#define IOFFSET_REGH				0xDD
#define IOFFSET_REGL				0xDE


/*0xE0 ~0xF2  data register,*/
#define  SOC_REG						0xE0

#define  REMAIN_CAP_REG3			0xE1
#define  REMAIN_CAP_REG2			0xE2
#define  REMAIN_CAP_REG1			0xE3
#define  REMAIN_CAP_REG0			0xE4

#define UPDAT_LEVE_REG				0xE5

#define  NEW_FCC_REG3				0xE6
#define  NEW_FCC_REG2				0xE7
#define  NEW_FCC_REG1				0xE8
#define  NEW_FCC_REG0				0xE9

#define NON_ACT_TIMER_CNT_REG_SAVE		0xEA
#define OCV_VOL_VALID_REG			0xEB
#define REBOOT_CNT_REG				0xEC
#define PCB_IOFFSET_REG				0xED
#define MISC_MARK_REG				0xEE

#define PLUG_IN_INT				(0)
#define PLUG_OUT_INT				(1)
#define CHRG_CVTLMT_INT				(6)

#define CHRG_EN_MASK				(1 << 7)
#define CHRG_EN					(1 << 7)
#define CHRG_DIS				(0 << 7)

#define OTG_EN_MASK				(1 << 7)
#define OTG_EN					(1 << 7)
#define OTG_DIS					(0 << 7)

/* gasgauge module enable bit 0: disable  1:enabsle
TS_CTRL_REG  0xAC*/
#define GG_EN						(1<<7)

/*ADC_CTRL_REG*/
/*
if GG_EN = 0 , then the ADC of BAT voltage controlled by the
bit 0:diabsle 1:enable
*/
#define ADC_VOL_EN					(1<<7)
/*
if GG_EN = 0, then the ADC of BAT current controlled by the
bit  0: disable 1: enable
*/
#define ADC_CUR_EN					(1<<6)
/*the ADC of TS1 controlled by the bit 0:disabsle 1:enable */
#define ADC_TS1_EN					(1<<5)
/*the ADC of TS2 controlled by the bit 0:disabsle 1:enable */
#define ADC_TS2_EN					(1<<4)
/*ADC colock phase  0:normal 1:inverted*/
#define ADC_PHASE					(1<<3)
#define ADC_CLK_SEL					7
/*****************************************************
#define ADC_CLK_SEL_2M				0x000
#define ADC_CLK_SEL_1M				0x001
#define ADC_CLK_SEL_500K			0x002
#define ADC_CLK_SEL_250K			0x003
#define ADC_CLK_SEL_125K			0x004
******************************************************/
/*GGCON*/
/* ADC bat current continue sample times  00:8  01:16 10:32 11:64*/
#define CUR_SAMPL_CON_TIMES	        (3<<6)
/*ADC offset calibreation interval time 00:8min 01:16min 10:32min 11:48min*/
#define ADC_OFF_CAL_INTERV			(3<<4)
/*OCV sampling interval time 00:8min 01:16min 10:32min :11:48min*/
#define OCV_SAMPL_INTERV			(3<<2)

/*ADC working in current voltage collection mode*/
#define ADC_CUR_VOL_MODE			(1<<1)
/*ADC working in resistor calculation mode 0:disable 1:enable*/
#define ADC_RES_MODE				1

/*GGSTS*/
/*average current filter times 00:1/2  01:1/4 10:1/8 11:1/16*/
#define RES_CUR_AVG_SEL				(3<<5)
/*battery first connection,edge trigger 0:NOT  1:YES*/
#define BAT_CON						(1<<4)
/*battery voltage1 update in relax status 0: NOT 1:YE*/
#define RELAX_VOL1_UPD				(1<<3)
/*battery voltage2 update in relax status 0: NOT 1:YE*/
#define RELAX_VOL2_UPD				(1<<2)
/*battery coming into relax status  0: NOT 1:YE*/
#define RELAX_STS					(1<<1)
/*battery average voltage and current updated status 0: NOT 1:YES*/
#define IV_AVG_UPD_STS				(1<<0)

/*FRAME_SMP_INTERV_REG*/
#define AUTO_SLP_EN					(1<<5)
/* auto sleep mode 0:disable 1:enable*/
#define FRAME_SMP_INTERV_TIME		0x1F

/*VB_MOD_REG*/
#define PLUG_IN_STS					(1<<6)

/*SUP_STS_REG*/
#define BAT_EXS						(1<<7)
#define CHARGE_OFF					(0x00<<4)
#define DEAD_CHARGE					(0x01<<4)
#define TRICKLE_CHARGE				(0x02<<4)
#define CC_OR_CV						(0x03<<4)
#define CHARGE_FINISH				(0x04<<4)
#define USB_OVER_VOL				(0x05<<4)
#define BAT_TMP_ERR					(0x06<<4)
#define TIMER_ERR					(0x07<<4)
/* usb is exists*/
#define USB_EXIST					(1<<1)
/* usb is effective*/
#define USB_EFF						(1<<0)

/*USB_CTRL_REG*/
#define CHRG_CT_EN					(1<<7)
/* USB_VLIM_SEL*/
/*
#define VLIM_4000MV					(0x00<<4)
#define VLIM_4100MV					(0x01<<4)
#define VLIM_4200MV					(0x02<<4)
#define VLIM_4300MV					(0x03<<4)
#define VLIM_4400MV					(0x04<<4)
#define VLIM_4500MV					(0x05<<4)
#define VLIM_4600MV					(0x06<<4)
#define VLIM_4700MV					(0x07<<4)
*/

/*USB_ILIM_SEL*/
#define ILIM_450MA					(0x00)
#define ILIM_800MA					(0x01)
#define ILIM_850MA					(0x02)
#define ILIM_1000MA					(0x03)
#define ILIM_1250MA					(0x04)
#define ILIM_1500MA					(0x05)
#define ILIM_1750MA					(0x06)
#define ILIM_2000MA					(0x07)
#define ILIM_2250MA					(0x08)
#define ILIM_2500MA					(0x09)
#define ILIM_2750MA					(0x0A)
#define ILIM_3000MA					(0x0B)

/*CHRG_VOL_SEL*/
#define CHRG_VOL4050				(0x00<<4)
#define CHRG_VOL4100				(0x01<<4)
#define CHRG_VOL4150				(0x02<<4)
#define CHRG_VOL4200				(0x03<<4)
#define CHRG_VOL4300				(0x04<<4)
#define CHRG_VOL4350				(0x05<<4)

/*CHRG_CUR_SEL*/
#define CHRG_CUR1000mA			(0x00)
#define CHRG_CUR1200mA			(0x01)
#define CHRG_CUR1400mA			(0x02)
#define CHRG_CUR1600mA			(0x03)
#define CHRG_CUR1800mA			(0x04)
#define CHRG_CUR2000mA			(0x05)
#define CHRG_CUR2200mA			(0x06)
#define CHRG_CUR2400mA			(0x07)
#define CHRG_CUR2600mA			(0x08)
#define CHRG_CUR2800mA			(0x09)
#define CHRG_CUR3000mA			(0x0A)

/*CHRG_CTRL_REG2*/
#define FINISH_100MA				(0x00<<6)
#define FINISH_150MA				(0x01<<6)
#define FINISH_200MA				(0x02<<6)
#define FINISH_250MA				(0x03<<6)

/*temp feed back degree*/
#define TEMP_85C			(0x00 << 2)
#define TEMP_95C			(0x01 << 2)
#define TEMP_105C			(0x02 << 2)
#define TEMP_115C			(0x03 << 2)


/* CHRG_CTRL_REG3*/
#define CHRG_TERM_ANA_SIGNAL 		(0 << 5)
#define CHRG_TERM_DIG_SIGNAL 		(1 << 5)
#define CHRG_TIMER_CCCV_EN  		(1 << 2)

/*CHRG_CTRL_REG2*/
#define CHG_CCCV_4HOUR			(0x00)
#define CHG_CCCV_5HOUR			(0x01)
#define CHG_CCCV_6HOUR			(0x02)
#define CHG_CCCV_8HOUR			(0x03)
#define CHG_CCCV_10HOUR			(0x04)
#define CHG_CCCV_12HOUR			(0x05)
#define CHG_CCCV_14HOUR			(0x06)
#define CHG_CCCV_16HOUR			(0x07)

/*GGCON*/
#define SAMP_TIME_8MIN				(0X00<<4)
#define SAMP_TIME_16MIN				(0X01<<4)
#define SAMP_TIME_32MIN				(0X02<<4)
#define SAMP_TIME_48MIN				(0X03<<4)

#define ADC_CURRENT_MODE			(1 << 1)
#define ADC_VOLTAGE_MODE			(0 << 1)

#define DRIVER_VERSION				"4.0.0"
#define ROLEX_SPEED					(100 * 1000)

#define CHARGING					0x01
#define DISCHARGING					0x00

#define TIMER_MS_COUNTS			1000
#define MAX_CHAR					0x7F
#define MAX_UNSIGNED_CHAR			0xFF
#define MAX_INT						0x7FFF
#define MAX_UNSIGNED_INT			0xFFFF
#define MAX_INT8					0x7F
#define MAX_UINT8					0xFF

/* Gas Gauge Constatnts */
#define TEMP_0C			2732
#define MAX_CAPACITY		0x7fff
#define MAX_SOC			100
#define MAX_PERCENTAGE		100

/* Num, cycles with no Learning, after this many cycles, the gauge
   start adjusting FCC, based on Estimated Cell Degradation */
#define NO_LEARNING_CYCLES	25
/* Size of the OCV Lookup table */
#define OCV_TABLE_SIZE		21
/*
 * OCV Config
 */
struct ocv_config {
	/*voltage_diff, current_diff: Maximal allowed deviation
	of the voltage and the current from one reading to the
	next that allows the fuel gauge to apply an OCV correction.
	The main purpose of these thresholds is to filter current
	and voltage spikes. Recommended value: these value are
	highly depend on the load nature. if the load creates a lot
	of current spikes .the value may need to be increase*/
	uint8_t voltage_diff;
	uint8_t current_diff;
	/* sleep_enter_current: if the current remains under
	this threshold for [sleep_enter_samples]
	consecutive samples. the gauge enters the SLEEP MODE*/
	uint16_t sleep_enter_current;
	/*sleep_enter_samples: the number of samples that
	satis fy asleep enter or exit condition in order
	to actually enter of exit SLEEP mode*/
	uint8_t sleep_enter_samples;
	/*sleep_exit_samples: to exit SLEEP mode , average
	current should pass this threshold first. then
	current should remain above this threshold for
	[sleep_exit_samples] consecutive samples*/
	uint16_t sleep_exit_current;
	/*sleep_exit_samples: to exit SLEEP mode, average
	current should pass this threshold first, then current
	should remain above this threshold for [sleep_exit_samples]
	consecutive samples.*/
	uint8_t sleep_exit_samples;
	/*relax_period: defines the number of seconds the
	fuel gauge should spend in the SLEEP mode
	before entering the OCV mode, this setting makes
	the gauge wait for a cell voltage recovery after
	a charge or discharge operation*/
	uint16_t relax_period;
	/* flat_zone_low : flat_zone_high :if soc falls into
	the flat zone low% - flat zone high %.the fuel gauge
	wait for a cell voltage recovery after a charge or
	discharge operation.*/
	uint8_t flat_zone_low;
	uint8_t flat_zone_high;
	/*FCC leaning is disqualified if the discharge capacity
	in the OCV mode is greater than this threshold*/
	uint16_t max_ocv_discharge;
	/*the 21-point OCV table*/
	uint16_t table[OCV_TABLE_SIZE];
	/*uint16_t *table;*/
};

/* EDV Point */
struct edv_point {
	int16_t voltage;
	uint8_t percent;
};

/* EDV Point tracking data */
struct edv_state {
	int16_t voltage;
	uint8_t percent;
	int16_t min_capacity;
	uint8_t edv_cmp;
};

/* EDV Configuration */
struct edv_config {
	/*avieraging: True = evokes averaging on voltage
	reading to detect an EDV condition.
	False = no averaging of voltage readings to detect an
	EDV conditation.*/
	bool averaging;
	/*sequential_edv: the sequential_edv setting defines
	how many times in a row the battery should
	pass the EDV threshold to detect an EDV condition.
	this setting is intended to fiter short voltage spikes
	cause by current spikes*/
	uint8_t sequential_edv;
	/*filter_light: difine the calculated EDV voltage
	recovery IIR filter strength
	light-lsetting : for light load (below Qmax/5)
	heavy setting : for ligh load (above Qmax/5)
	the filter is applied only if the load is greater than
	Qmax/3. if average = True. then the Qmax/5 threshold
	is compared to averge current.otherwise it is compared
	to current.
	Recommended value: 15-255. 255---disabsle the filter
	*/
	uint8_t filter_light;
	uint8_t filter_heavy;
	/*overload_current: the current level above which an
	EDV condition will not be detected and
	capacity not reconciled*/
	int16_t overload_current;

	struct edv_point edv[3];
	/*edv: the end-of-discharge voltage-to-capactiy
	correlation points.*/
	/*struct edv_point *edv;*/
};

/* General Battery Cell Gauging Configuration */
struct cell_config {
	bool cc_polarity;  /*To Be Determined*/
	bool cc_out;
	/*ocv_below_edv1: if set (True), OCV correction allowed
	bellow EDV1 point*/
	bool ocv_below_edv1;
	/*cc_voltage: the charge complete voltage threshold(e.g. 4.2v)
	of the battery. charge cannot be considered complete if the
	battery voltage is below this threshold*/
	int16_t cc_voltage;
	/*cc_current:the charge complete current threshold(e.g. c/20).
	charge cannot  be considered complete when charge
	current and average current are greater than this threshold*/
	int16_t cc_current;
	/*design_capacity: design capacity of the battery.
	the battery datasheet should provide this value*/
	uint16_t design_capacity;
	/*design_qmax: the calculated discharge capacity of
	the OCV discharge curve*/
	int16_t design_qmax;
	/*r_sense: the resistance of the current sence element.
	the sense resistor needs to be slelected to
	ensure accurate current measuremen and integration
	at currents >OFF consumption*/
	uint8_t r_sense;
	/*qmax_adjust: the value decremented from QMAX
	every cycle for aging compensation.*/
	uint8_t qmax_adjust;
	/*fcc_adjust: the value decremented from the FCC
	when no learning happen for 25 cycles in a row*/
	uint8_t fcc_adjust;
	/*max_overcharge: the fuel gauge tracks the capacity
	that goes into the battery after a termination
	condition is detected. this improve gauging accuracy
	if the charger's charge termination condition does't
	match to the fuel gauge charge termination condition.*/
	uint16_t max_overcharge;
	/*electronics_load: the current that the system consumes
	int the OFF mode(MPU low power, screen  OFF)*/
	uint16_t electronics_load;
	/*max_increment: the maximum increment of FCC if the
	learned capacity is much greater than the exiting
	FCC. recommentded value 150mAh*/
	int16_t max_increment;
	/*max_decrement: the maximum increment of FCC if the
	learned capacity is much lower than the exiting FCC*/
	int16_t max_decrement;
	/*low_temp: the correlation between voltage and remaining
	capacity is considered inaccurate below this temperature.
	any leaning will be disqualified, if the battery temperature
	is below this threshold
	*/
	uint8_t low_temp;
	/*deep_dsg_voltage:in order to qualify capacity learning on
	the discharge, the battery voltage should
	be within EDV-deep-dsg_voltage and EDV.*/
	uint16_t deep_dsg_voltage;
	/*
	max_dsg_voltage:limits the amount of the estimated
	discharge when learning is in progress. if the amount of
	the capacity estimation get greater than this threshold,
	the learning gets disqualified
	*/
	uint16_t max_dsg_estimate;
	/*
	light_load: FCC learning on discharge disqualifies if
	the load is below this threshold when the
	when EDV2 is reached.
	*/
	uint8_t light_load;
	/*
	near_full: this defines a capacity zone from FCC
	to FCC - near_full. A discharge cycles start
	from this capacity zone qualifies for FCC larning.
	*/
	uint16_t near_full;
	/*
	cycle_threshold: the amount of capacity that should
	be dicharged from the battery to increment the cycle
	count by 1.cycle counting happens on the discharge only.
	*/
	uint16_t cycle_threshold;
	/*recharge: the voltage of recharge.*/
	uint16_t recharge;
	/*
	mode_swtich_capacity: this defines how much capacity
	should pass through the coulomb counter to cause a cycle
	count start condition (either charge or discharge). the gauge
	support 2 cycle typeds.charge and discharge. a cycle starts
	when mode_switch_capacity passes through the coulomb counter
	the cycle get canceled and switches to the opposite direciton
	if mode_switch_capacity passes though
	the coulomb counter in oppositer direciton.
	*/
	uint8_t mode_switch_capacity;
	/*call_period: approximate time between fuel gauge calls.*/
	uint8_t call_period;

	struct ocv_config *ocv;
	struct edv_config *edv;
};

/* Cell State */
/*
light-load: ( < C/40)

*/
struct cell_state {
	/*
	SOC : state-of-charge of the battery in %,it represents
	the % full of the battery from the system empty voltage.
	SOC = NAC/FCC,  SOC = 1 -DOD
	*/
	int16_t	soc;
	/*
	nac :nominal avaiable charge of the battery in mAh.
	it represents the present remain capacity of the battery
	to the system empty voltage under nominal conditions
	*/
	int16_t	nac;
	/*
	fcc: full battery capacity .this represents the discharge capacity
	of the battery from the defined full condition to the system empty
	voltage(EDV0) under nominal conditions.the value is learned by
	the algorithm on qualified charge and discharge cycleds
	*/
	int16_t fcc;
	/* qmax: the battery capacity(mAh) at the OCV curve discharge rate*/
	int16_t qmax;

	int16_t voltage;
	int16_t av_voltage;
	int16_t cur;
	int16_t av_current;

	int16_t temperature;
	/*
	cycle_count: it represents how many charge or discharge
	cycles a battery has experience. this is used to estimate the
	change of impedance of the battery due to "aging"
	*/
	int16_t cycle_count;
	/*
	sleep : in this mode ,the battery fuel gauge is counting
	discharge with the coulomb counter and checking for the
	battery relaxed condition, if a relaxed battery is destected
	the fuel gauge enters OCV mode
	*/
	bool sleep;
	bool relax;

	bool chg;
	bool dsg;

	bool edv0;
	bool edv1;
	bool edv2;
	bool ocv;
	bool cc;
	bool full;

	bool eocl;
	bool vcq;
	bool vdq;
	bool init;

	struct timeval sleep_timer;
	struct timeval el_sleep_timer;
	uint16_t cumulative_sleep;

	int16_t prev_soc;
	int16_t learn_q;
	uint16_t dod_eoc;
	int16_t learn_offset;
	uint16_t learned_cycle;
	int16_t new_fcc;
	int16_t	ocv_total_q;
	int16_t	ocv_enter_q;
	int16_t	negative_q;
	int16_t	overcharge_q;
	int16_t	charge_cycle_q;
	int16_t	discharge_cycle_q;
	int16_t	cycle_q;
	uint8_t	sequential_cc;
	uint8_t	sleep_samples;
	uint8_t	sequential_edvs;

	uint16_t electronics_load;
	uint16_t cycle_dsg_estimate;

	struct edv_state edv;

	bool updated;
	bool calibrate;

	struct cell_config *config;
};

struct battery_platform_data {
	int *battery_tmp_tbl;
	unsigned int tblsize;
	u32 *battery_ocv;
	unsigned int  ocv_size;

	unsigned int monitoring_interval;
	unsigned int max_charger_ilimitmA;
	unsigned int max_charger_currentmA;
	unsigned int max_charger_voltagemV;
	unsigned int termination_currentmA;

	unsigned int max_bat_voltagemV;
	unsigned int low_bat_voltagemV;
	unsigned int chrg_diff_vol;
	unsigned int power_off_thresd;
	unsigned int sense_resistor_mohm;

	/* twl6032 */
	unsigned long features;
	unsigned long errata;

	struct cell_config *cell_cfg;
};

enum fg_mode {
	FG_NORMAL_MODE = 0,/*work normally*/
	TEST_POWER_MODE,   /*work without battery*/
};

enum hw_support_adp {
	HW_ADP_TYPE_USB = 0,/*'HW' means:hardware*/
	HW_ADP_TYPE_DC,
	HW_ADP_TYPE_DUAL
};


/* don't change the following ID, they depend on usb check
 * interface: dwc_otg_check_dpdm()
 */
enum charger_type {
	NO_CHARGER = 0,
	USB_CHARGER,
	AC_CHARGER,
	DC_CHARGER,
	DUAL_CHARGER
};

enum charger_state {
	OFFLINE = 0,
	ONLINE
};

void kernel_power_off(void);
#if defined(CONFIG_ARCH_ROCKCHIP)
int dwc_vbus_status(void);
int get_gadget_connect_flag(void);
void rk_send_wakeup_key(void);
#else

static inline int get_gadget_connect_flag(void)
{
	return 0;
}

static inline int dwc_otg_check_dpdm(bool wait)
{
	return 0;
}

static inline void rk_send_wakeup_key(void)
{
}
#endif

#endif
