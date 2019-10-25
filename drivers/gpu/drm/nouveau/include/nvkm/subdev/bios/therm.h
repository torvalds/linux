/* SPDX-License-Identifier: MIT */
#ifndef __NVBIOS_THERM_H__
#define __NVBIOS_THERM_H__
struct nvbios_therm_threshold {
	u8 temp;
	u8 hysteresis;
};

struct nvbios_therm_sensor {
	/* diode */
	s16 slope_mult;
	s16 slope_div;
	s16 offset_num;
	s16 offset_den;
	s8 offset_constant;

	/* thresholds */
	struct nvbios_therm_threshold thrs_fan_boost;
	struct nvbios_therm_threshold thrs_down_clock;
	struct nvbios_therm_threshold thrs_critical;
	struct nvbios_therm_threshold thrs_shutdown;
};

enum nvbios_therm_fan_type {
	NVBIOS_THERM_FAN_UNK = 0,
	NVBIOS_THERM_FAN_TOGGLE = 1,
	NVBIOS_THERM_FAN_PWM = 2,
};

/* no vbios have more than 6 */
#define NVKM_TEMP_FAN_TRIP_MAX 10
struct nvbios_therm_trip_point {
	int fan_duty;
	int temp;
	int hysteresis;
};

enum nvbios_therm_fan_mode {
	NVBIOS_THERM_FAN_TRIP = 0,
	NVBIOS_THERM_FAN_LINEAR = 1,
	NVBIOS_THERM_FAN_OTHER = 2,
};

struct nvbios_therm_fan {
	enum nvbios_therm_fan_type type;

	u32 pwm_freq;

	u8 min_duty;
	u8 max_duty;

	u16 bump_period;
	u16 slow_down_period;

	enum nvbios_therm_fan_mode fan_mode;
	struct nvbios_therm_trip_point trip[NVKM_TEMP_FAN_TRIP_MAX];
	u8 nr_fan_trip;
	u8 linear_min_temp;
	u8 linear_max_temp;
};

enum nvbios_therm_domain {
	NVBIOS_THERM_DOMAIN_CORE,
	NVBIOS_THERM_DOMAIN_AMBIENT,
};

int
nvbios_therm_sensor_parse(struct nvkm_bios *, enum nvbios_therm_domain,
			  struct nvbios_therm_sensor *);

int
nvbios_therm_fan_parse(struct nvkm_bios *, struct nvbios_therm_fan *);
#endif
