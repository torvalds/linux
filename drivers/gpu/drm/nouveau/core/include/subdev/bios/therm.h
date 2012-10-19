#ifndef __NVBIOS_THERM_H__
#define __NVBIOS_THERM_H__

struct nouveau_bios;

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

struct nvbios_therm_fan {
	u16 pwm_freq;

	u8 min_duty;
	u8 max_duty;
};

enum nvbios_therm_domain {
	NVBIOS_THERM_DOMAIN_CORE,
	NVBIOS_THERM_DOMAIN_AMBIENT,
};

int
nvbios_therm_sensor_parse(struct nouveau_bios *, enum nvbios_therm_domain,
			  struct nvbios_therm_sensor *);

int
nvbios_therm_fan_parse(struct nouveau_bios *, struct nvbios_therm_fan *);


#endif
