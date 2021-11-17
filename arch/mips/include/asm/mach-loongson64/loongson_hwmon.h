/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LOONGSON_HWMON_H_
#define __LOONGSON_HWMON_H_

#include <linux/types.h>

#define MIN_TEMP	0
#define MAX_TEMP	255
#define NOT_VALID_TEMP	999

typedef int (*get_temp_fun)(int);
extern int loongson3_cpu_temp(int);

/* 0:Max speed, 1:Manual, 2:Auto */
enum fan_control_mode {
	FAN_FULL_MODE = 0,
	FAN_MANUAL_MODE = 1,
	FAN_AUTO_MODE = 2,
	FAN_MODE_END
};

struct temp_range {
	u8 low;
	u8 high;
	u8 level;
};

#define CONSTANT_SPEED_POLICY	0  /* at constant speed */
#define STEP_SPEED_POLICY	1  /* use up/down arrays to describe policy */
#define KERNEL_HELPER_POLICY	2  /* kernel as a helper to fan control */

#define MAX_STEP_NUM	16
#define MAX_FAN_LEVEL	255

/* loongson_fan_policy works when fan work at FAN_AUTO_MODE */
struct loongson_fan_policy {
	u8	type;

	/* percent only used when type is CONSTANT_SPEED_POLICY */
	u8	percent;

	/* period between two check. (Unit: S) */
	u8	adjust_period;

	/* fan adjust usually depend on a temprature input */
	get_temp_fun	depend_temp;

	/* up_step/down_step used when type is STEP_SPEED_POLICY */
	u8	up_step_num;
	u8	down_step_num;
	struct temp_range up_step[MAX_STEP_NUM];
	struct temp_range down_step[MAX_STEP_NUM];
	struct delayed_work work;
};

#endif /* __LOONGSON_HWMON_H_*/
