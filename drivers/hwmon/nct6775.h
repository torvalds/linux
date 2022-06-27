/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __HWMON_NCT6775_H__
#define __HWMON_NCT6775_H__

#include <linux/types.h>

enum kinds { nct6106, nct6116, nct6775, nct6776, nct6779, nct6791, nct6792,
	     nct6793, nct6795, nct6796, nct6797, nct6798 };
enum pwm_enable { off, manual, thermal_cruise, speed_cruise, sf3, sf4 };

#define NUM_TEMP	10	/* Max number of temp attribute sets w/ limits*/
#define NUM_TEMP_FIXED	6	/* Max number of fixed temp attribute sets */
#define NUM_TSI_TEMP	8	/* Max number of TSI temp register pairs */

#define NUM_REG_ALARM	7	/* Max number of alarm registers */
#define NUM_REG_BEEP	5	/* Max number of beep registers */

#define NUM_FAN		7

struct nct6775_data {
	int addr;	/* IO base of hw monitor block */
	int sioreg;	/* SIO register address */
	enum kinds kind;
	const char *name;

	const struct attribute_group *groups[7];
	u8 num_groups;

	u16 reg_temp[5][NUM_TEMP]; /* 0=temp, 1=temp_over, 2=temp_hyst,
				    * 3=temp_crit, 4=temp_lcrit
				    */
	u8 temp_src[NUM_TEMP];
	u16 reg_temp_config[NUM_TEMP];
	const char * const *temp_label;
	u32 temp_mask;
	u32 virt_temp_mask;

	u16 REG_CONFIG;
	u16 REG_VBAT;
	u16 REG_DIODE;
	u8 DIODE_MASK;

	const s8 *ALARM_BITS;
	const s8 *BEEP_BITS;

	const u16 *REG_VIN;
	const u16 *REG_IN_MINMAX[2];

	const u16 *REG_TARGET;
	const u16 *REG_FAN;
	const u16 *REG_FAN_MODE;
	const u16 *REG_FAN_MIN;
	const u16 *REG_FAN_PULSES;
	const u16 *FAN_PULSE_SHIFT;
	const u16 *REG_FAN_TIME[3];

	const u16 *REG_TOLERANCE_H;

	const u8 *REG_PWM_MODE;
	const u8 *PWM_MODE_MASK;

	const u16 *REG_PWM[7];	/* [0]=pwm, [1]=pwm_start, [2]=pwm_floor,
				 * [3]=pwm_max, [4]=pwm_step,
				 * [5]=weight_duty_step, [6]=weight_duty_base
				 */
	const u16 *REG_PWM_READ;

	const u16 *REG_CRITICAL_PWM_ENABLE;
	u8 CRITICAL_PWM_ENABLE_MASK;
	const u16 *REG_CRITICAL_PWM;

	const u16 *REG_AUTO_TEMP;
	const u16 *REG_AUTO_PWM;

	const u16 *REG_CRITICAL_TEMP;
	const u16 *REG_CRITICAL_TEMP_TOLERANCE;

	const u16 *REG_TEMP_SOURCE;	/* temp register sources */
	const u16 *REG_TEMP_SEL;
	const u16 *REG_WEIGHT_TEMP_SEL;
	const u16 *REG_WEIGHT_TEMP[3];	/* 0=base, 1=tolerance, 2=step */

	const u16 *REG_TEMP_OFFSET;

	const u16 *REG_ALARM;
	const u16 *REG_BEEP;

	const u16 *REG_TSI_TEMP;

	unsigned int (*fan_from_reg)(u16 reg, unsigned int divreg);
	unsigned int (*fan_from_reg_min)(u16 reg, unsigned int divreg);

	struct mutex update_lock;
	bool valid;		/* true if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	/* Register values */
	u8 bank;		/* current register bank */
	u8 in_num;		/* number of in inputs we have */
	u8 in[15][3];		/* [0]=in, [1]=in_max, [2]=in_min */
	unsigned int rpm[NUM_FAN];
	u16 fan_min[NUM_FAN];
	u8 fan_pulses[NUM_FAN];
	u8 fan_div[NUM_FAN];
	u8 has_pwm;
	u8 has_fan;		/* some fan inputs can be disabled */
	u8 has_fan_min;		/* some fans don't have min register */
	bool has_fan_div;

	u8 num_temp_alarms;	/* 2, 3, or 6 */
	u8 num_temp_beeps;	/* 2, 3, or 6 */
	u8 temp_fixed_num;	/* 3 or 6 */
	u8 temp_type[NUM_TEMP_FIXED];
	s8 temp_offset[NUM_TEMP_FIXED];
	s16 temp[5][NUM_TEMP]; /* 0=temp, 1=temp_over, 2=temp_hyst,
				* 3=temp_crit, 4=temp_lcrit
				*/
	s16 tsi_temp[NUM_TSI_TEMP];
	u64 alarms;
	u64 beeps;

	u8 pwm_num;	/* number of pwm */
	u8 pwm_mode[NUM_FAN];	/* 0->DC variable voltage,
				 * 1->PWM variable duty cycle
				 */
	enum pwm_enable pwm_enable[NUM_FAN];
			/* 0->off
			 * 1->manual
			 * 2->thermal cruise mode (also called SmartFan I)
			 * 3->fan speed cruise mode
			 * 4->SmartFan III
			 * 5->enhanced variable thermal cruise (SmartFan IV)
			 */
	u8 pwm[7][NUM_FAN];	/* [0]=pwm, [1]=pwm_start, [2]=pwm_floor,
				 * [3]=pwm_max, [4]=pwm_step,
				 * [5]=weight_duty_step, [6]=weight_duty_base
				 */

	u8 target_temp[NUM_FAN];
	u8 target_temp_mask;
	u32 target_speed[NUM_FAN];
	u32 target_speed_tolerance[NUM_FAN];
	u8 speed_tolerance_limit;

	u8 temp_tolerance[2][NUM_FAN];
	u8 tolerance_mask;

	u8 fan_time[3][NUM_FAN]; /* 0 = stop_time, 1 = step_up, 2 = step_down */

	/* Automatic fan speed control registers */
	int auto_pwm_num;
	u8 auto_pwm[NUM_FAN][7];
	u8 auto_temp[NUM_FAN][7];
	u8 pwm_temp_sel[NUM_FAN];
	u8 pwm_weight_temp_sel[NUM_FAN];
	u8 weight_temp[3][NUM_FAN];	/* 0->temp_step, 1->temp_step_tol,
					 * 2->temp_base
					 */

	u8 vid;
	u8 vrm;

	bool have_vid;

	u16 have_temp;
	u16 have_temp_fixed;
	u16 have_tsi_temp;
	u16 have_in;

	/* Remember extra register values over suspend/resume */
	u8 vbat;
	u8 fandiv1;
	u8 fandiv2;
	u8 sio_reg_enable;

	struct regmap *regmap;
	bool read_only;

	/* driver-specific (platform, i2c) initialization hook and data */
	int (*driver_init)(struct nct6775_data *data);
	void *driver_data;
};

static inline int nct6775_read_value(struct nct6775_data *data, u16 reg, u16 *value)
{
	unsigned int tmp;
	int ret = regmap_read(data->regmap, reg, &tmp);

	if (!ret)
		*value = tmp;
	return ret;
}

static inline int nct6775_write_value(struct nct6775_data *data, u16 reg, u16 value)
{
	return regmap_write(data->regmap, reg, value);
}

bool nct6775_reg_is_word_sized(struct nct6775_data *data, u16 reg);
int nct6775_probe(struct device *dev, struct nct6775_data *data,
		  const struct regmap_config *regmapcfg);

ssize_t nct6775_show_alarm(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t nct6775_show_beep(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t nct6775_store_beep(struct device *dev, struct device_attribute *attr, const char *buf,
			   size_t count);

static inline int nct6775_write_temp(struct nct6775_data *data, u16 reg, u16 value)
{
	if (!nct6775_reg_is_word_sized(data, reg))
		value >>= 8;
	return nct6775_write_value(data, reg, value);
}

static inline umode_t nct6775_attr_mode(struct nct6775_data *data, struct attribute *attr)
{
	return data->read_only ? (attr->mode & ~0222) : attr->mode;
}

static inline int
nct6775_add_attr_group(struct nct6775_data *data, const struct attribute_group *group)
{
	/* Need to leave a NULL terminator at the end of data->groups */
	if (data->num_groups == ARRAY_SIZE(data->groups) - 1)
		return -ENOBUFS;

	data->groups[data->num_groups++] = group;
	return 0;
}

#define NCT6775_REG_BANK	0x4E
#define NCT6775_REG_CONFIG	0x40

#define NCT6775_REG_FANDIV1		0x506
#define NCT6775_REG_FANDIV2		0x507

#define NCT6791_REG_HM_IO_SPACE_LOCK_ENABLE	0x28

#define FAN_ALARM_BASE		16
#define TEMP_ALARM_BASE		24
#define INTRUSION_ALARM_BASE	30
#define BEEP_ENABLE_BASE	15

/*
 * Not currently used:
 * REG_MAN_ID has the value 0x5ca3 for all supported chips.
 * REG_CHIP_ID == 0x88/0xa1/0xc1 depending on chip model.
 * REG_MAN_ID is at port 0x4f
 * REG_CHIP_ID is at port 0x58
 */

#endif /* __HWMON_NCT6775_H__ */
