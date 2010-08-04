#ifndef __THERM_PMAC_7_2_H__
#define __THERM_PMAC_7_2_H__

typedef unsigned short fu16;
typedef int fs32;
typedef short fs16;

struct mpu_data
{
	u8	signature;		/* 0x00 - EEPROM sig. */
	u8	bytes_used;		/* 0x01 - Bytes used in eeprom (160 ?) */
	u8	size;			/* 0x02 - EEPROM size (256 ?) */
	u8	version;		/* 0x03 - EEPROM version */
	u32	data_revision;		/* 0x04 - Dataset revision */
	u8	processor_bin_code[3];	/* 0x08 - Processor BIN code */
	u8	bin_code_expansion;	/* 0x0b - ??? (padding ?) */
	u8	processor_num;		/* 0x0c - Number of CPUs on this MPU */
	u8	input_mul_bus_div;	/* 0x0d - Clock input multiplier/bus divider */
	u8	reserved1[2];		/* 0x0e - */
	u32	input_clk_freq_high;	/* 0x10 - Input clock frequency high */
	u8	cpu_nb_target_cycles;	/* 0x14 - ??? */
	u8	cpu_statlat;		/* 0x15 - ??? */
	u8	cpu_snooplat;		/* 0x16 - ??? */
	u8	cpu_snoopacc;		/* 0x17 - ??? */
	u8	nb_paamwin;		/* 0x18 - ??? */
	u8	nb_statlat;		/* 0x19 - ??? */
	u8	nb_snooplat;		/* 0x1a - ??? */
	u8	nb_snoopwin;		/* 0x1b - ??? */
	u8	api_bus_mode;		/* 0x1c - ??? */
	u8	reserved2[3];		/* 0x1d - */
	u32	input_clk_freq_low;	/* 0x20 - Input clock frequency low */
	u8	processor_card_slot;	/* 0x24 - Processor card slot number */
	u8	reserved3[2];		/* 0x25 - */
	u8	padjmax;       		/* 0x27 - Max power adjustment (Not in OF!) */
	u8	ttarget;		/* 0x28 - Target temperature */
	u8	tmax;			/* 0x29 - Max temperature */
	u8	pmaxh;			/* 0x2a - Max power */
	u8	tguardband;		/* 0x2b - Guardband temp ??? Hist. len in OSX */
	fs32	pid_gp;			/* 0x2c - PID proportional gain */
	fs32	pid_gr;			/* 0x30 - PID reset gain */
	fs32	pid_gd;			/* 0x34 - PID derivative gain */
	fu16	voph;			/* 0x38 - Vop High */
	fu16	vopl;			/* 0x3a - Vop Low */
	fs16	nactual_die;		/* 0x3c - nActual Die */
	fs16	nactual_heatsink;	/* 0x3e - nActual Heatsink */
	fs16	nactual_system;		/* 0x40 - nActual System */
	u16	calibration_flags;	/* 0x42 - Calibration flags */
	fu16	mdiode;			/* 0x44 - Diode M value (scaling factor) */
	fs16	bdiode;			/* 0x46 - Diode B value (offset) */
	fs32	theta_heat_sink;	/* 0x48 - Theta heat sink */
	u16	rminn_intake_fan;	/* 0x4c - Intake fan min RPM */
	u16	rmaxn_intake_fan;	/* 0x4e - Intake fan max RPM */
	u16	rminn_exhaust_fan;	/* 0x50 - Exhaust fan min RPM */
	u16	rmaxn_exhaust_fan;	/* 0x52 - Exhaust fan max RPM */
	u8	processor_part_num[8];	/* 0x54 - Processor part number XX pumps min/max */
	u32	processor_lot_num;	/* 0x5c - Processor lot number */
	u8	orig_card_sernum[0x10];	/* 0x60 - Card original serial number */
	u8	curr_card_sernum[0x10];	/* 0x70 - Card current serial number */
	u8	mlb_sernum[0x18];	/* 0x80 - MLB serial number */
	u32	checksum1;		/* 0x98 - */
	u32	checksum2;		/* 0x9c - */	
}; /* Total size = 0xa0 */

/* Display a 16.16 fixed point value */
#define FIX32TOPRINT(f)	((f) >> 16),((((f) & 0xffff) * 1000) >> 16)

/*
 * Maximum number of seconds to be in critical state (after a
 * normal shutdown attempt). If the machine isn't down after
 * this counter elapses, we force an immediate machine power
 * off.
 */
#define MAX_CRITICAL_STATE			30
static char * critical_overtemp_path = "/sbin/critical_overtemp";

/*
 * This option is "weird" :) Basically, if you define this to 1
 * the control loop for the RPMs fans (not PWMs) will apply the
 * correction factor obtained from the PID to the _actual_ RPM
 * speed read from the FCU.
 * If you define the below constant to 0, then it will be
 * applied to the setpoint RPM speed, that is basically the
 * speed we proviously "asked" for.
 *
 * I'm not sure which of these Apple's algorithm is supposed
 * to use
 */
#define RPM_PID_USE_ACTUAL_SPEED		0

/*
 * i2c IDs. Currently, we hard code those and assume that
 * the FCU is on U3 bus 1 while all sensors are on U3 bus
 * 0. This appear to be safe enough for this first version
 * of the driver, though I would accept any clean patch
 * doing a better use of the device-tree without turning the
 * while i2c registration mechanism into a racy mess
 *
 * Note: Xserve changed this. We have some bits on the K2 bus,
 * which I arbitrarily set to 0x200. Ultimately, we really want
 * too lookup these in the device-tree though
 */
#define FAN_CTRLER_ID		0x15e
#define SUPPLY_MONITOR_ID      	0x58
#define SUPPLY_MONITORB_ID     	0x5a
#define DRIVES_DALLAS_ID	0x94
#define BACKSIDE_MAX_ID		0x98
#define XSERVE_DIMMS_LM87	0x25a
#define XSERVE_SLOTS_LM75	0x290

/*
 * Some MAX6690, DS1775, LM87 register definitions
 */
#define MAX6690_INT_TEMP	0
#define MAX6690_EXT_TEMP	1
#define DS1775_TEMP		0
#define LM87_INT_TEMP		0x27

/*
 * Scaling factors for the AD7417 ADC converters (except
 * for the CPU diode which is obtained from the EEPROM).
 * Those values are obtained from the property list of
 * the darwin driver
 */
#define ADC_12V_CURRENT_SCALE	0x0320	/* _AD2 */
#define ADC_CPU_VOLTAGE_SCALE	0x00a0	/* _AD3 */
#define ADC_CPU_CURRENT_SCALE	0x1f40	/* _AD4 */

/*
 * PID factors for the U3/Backside fan control loop. We have 2 sets
 * of values here, one set for U3 and one set for U3H
 */
#define BACKSIDE_FAN_PWM_DEFAULT_ID	1
#define BACKSIDE_FAN_PWM_INDEX		0
#define BACKSIDE_PID_U3_G_d		0x02800000
#define BACKSIDE_PID_U3H_G_d		0x01400000
#define BACKSIDE_PID_RACK_G_d		0x00500000
#define BACKSIDE_PID_G_p		0x00500000
#define BACKSIDE_PID_RACK_G_p		0x0004cccc
#define BACKSIDE_PID_G_r		0x00000000
#define BACKSIDE_PID_U3_INPUT_TARGET	0x00410000
#define BACKSIDE_PID_U3H_INPUT_TARGET	0x004b0000
#define BACKSIDE_PID_RACK_INPUT_TARGET	0x00460000
#define BACKSIDE_PID_INTERVAL		5
#define BACKSIDE_PID_RACK_INTERVAL	1
#define BACKSIDE_PID_OUTPUT_MAX		100
#define BACKSIDE_PID_U3_OUTPUT_MIN	20
#define BACKSIDE_PID_U3H_OUTPUT_MIN	20
#define BACKSIDE_PID_HISTORY_SIZE	2

struct basckside_pid_params
{
	s32			G_d;
	s32			G_p;
	s32			G_r;
	s32			input_target;
	s32			output_min;
	s32			output_max;
	s32			interval;
	int			additive;
};

struct backside_pid_state
{
	int			ticks;
	struct i2c_client *	monitor;
	s32		       	sample_history[BACKSIDE_PID_HISTORY_SIZE];
	s32			error_history[BACKSIDE_PID_HISTORY_SIZE];
	int			cur_sample;
	s32			last_temp;
	int			pwm;
	int			first;
};

/*
 * PID factors for the Drive Bay fan control loop
 */
#define DRIVES_FAN_RPM_DEFAULT_ID	2
#define DRIVES_FAN_RPM_INDEX		1
#define DRIVES_PID_G_d			0x01e00000
#define DRIVES_PID_G_p			0x00500000
#define DRIVES_PID_G_r			0x00000000
#define DRIVES_PID_INPUT_TARGET		0x00280000
#define DRIVES_PID_INTERVAL    		5
#define DRIVES_PID_OUTPUT_MAX		4000
#define DRIVES_PID_OUTPUT_MIN		300
#define DRIVES_PID_HISTORY_SIZE		2

struct drives_pid_state
{
	int			ticks;
	struct i2c_client *	monitor;
	s32	       		sample_history[BACKSIDE_PID_HISTORY_SIZE];
	s32			error_history[BACKSIDE_PID_HISTORY_SIZE];
	int			cur_sample;
	s32			last_temp;
	int			rpm;
	int			first;
};

#define SLOTS_FAN_PWM_DEFAULT_ID	2
#define SLOTS_FAN_PWM_INDEX		2
#define	SLOTS_FAN_DEFAULT_PWM		40 /* Do better here ! */


/*
 * PID factors for the Xserve DIMM control loop
 */
#define DIMM_PID_G_d			0
#define DIMM_PID_G_p			0
#define DIMM_PID_G_r			0x06553600
#define DIMM_PID_INPUT_TARGET		3276800
#define DIMM_PID_INTERVAL    		1
#define DIMM_PID_OUTPUT_MAX		14000
#define DIMM_PID_OUTPUT_MIN		4000
#define DIMM_PID_HISTORY_SIZE		20

struct dimm_pid_state
{
	int			ticks;
	struct i2c_client *	monitor;
	s32	       		sample_history[DIMM_PID_HISTORY_SIZE];
	s32			error_history[DIMM_PID_HISTORY_SIZE];
	int			cur_sample;
	s32			last_temp;
	int			first;
	int			output;
};


/*
 * PID factors for the Xserve Slots control loop
 */
#define SLOTS_PID_G_d			0
#define SLOTS_PID_G_p			0
#define SLOTS_PID_G_r			0x00100000
#define SLOTS_PID_INPUT_TARGET		3200000
#define SLOTS_PID_INTERVAL    		1
#define SLOTS_PID_OUTPUT_MAX		100
#define SLOTS_PID_OUTPUT_MIN		20
#define SLOTS_PID_HISTORY_SIZE		20

struct slots_pid_state
{
	int			ticks;
	struct i2c_client *	monitor;
	s32	       		sample_history[SLOTS_PID_HISTORY_SIZE];
	s32			error_history[SLOTS_PID_HISTORY_SIZE];
	int			cur_sample;
	s32			last_temp;
	int			first;
	int			pwm;
};



/* Desktops */

#define CPUA_INTAKE_FAN_RPM_DEFAULT_ID	3
#define CPUA_EXHAUST_FAN_RPM_DEFAULT_ID	4
#define CPUB_INTAKE_FAN_RPM_DEFAULT_ID	5
#define CPUB_EXHAUST_FAN_RPM_DEFAULT_ID	6

#define CPUA_INTAKE_FAN_RPM_INDEX	3
#define CPUA_EXHAUST_FAN_RPM_INDEX	4
#define CPUB_INTAKE_FAN_RPM_INDEX	5
#define CPUB_EXHAUST_FAN_RPM_INDEX	6

#define CPU_INTAKE_SCALE		0x0000f852
#define CPU_TEMP_HISTORY_SIZE		2
#define CPU_POWER_HISTORY_SIZE		10
#define CPU_PID_INTERVAL		1
#define CPU_MAX_OVERTEMP		90

#define CPUA_PUMP_RPM_INDEX		7
#define CPUB_PUMP_RPM_INDEX		8
#define CPU_PUMP_OUTPUT_MAX		3200
#define CPU_PUMP_OUTPUT_MIN		1250

/* Xserve */
#define CPU_A1_FAN_RPM_INDEX		9
#define CPU_A2_FAN_RPM_INDEX		10
#define CPU_A3_FAN_RPM_INDEX		11
#define CPU_B1_FAN_RPM_INDEX		12
#define CPU_B2_FAN_RPM_INDEX		13
#define CPU_B3_FAN_RPM_INDEX		14


struct cpu_pid_state
{
	int			index;
	struct i2c_client *	monitor;
	struct mpu_data		mpu;
	int			overtemp;
	s32	       		temp_history[CPU_TEMP_HISTORY_SIZE];
	int			cur_temp;
	s32			power_history[CPU_POWER_HISTORY_SIZE];
	s32			error_history[CPU_POWER_HISTORY_SIZE];
	int			cur_power;
	int			count_power;
	int			rpm;
	int			intake_rpm;
	s32			voltage;
	s32			current_a;
	s32			last_temp;
	s32			last_power;
	int			first;
	u8			adc_config;
	s32			pump_min;
	s32			pump_max;
};

/* Tickle FCU every 10 seconds */
#define FCU_TICKLE_TICKS	10

/*
 * Driver state
 */
enum {
	state_detached,
	state_attaching,
	state_attached,
	state_detaching,
};


#endif /* __THERM_PMAC_7_2_H__ */
