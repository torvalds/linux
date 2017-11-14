/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_THERM_H__
#define __NVKM_THERM_H__
#include <core/subdev.h>

#include <subdev/bios.h>
#include <subdev/bios/therm.h>
#include <subdev/timer.h>

enum nvkm_therm_thrs_direction {
	NVKM_THERM_THRS_FALLING = 0,
	NVKM_THERM_THRS_RISING = 1
};

enum nvkm_therm_thrs_state {
	NVKM_THERM_THRS_LOWER = 0,
	NVKM_THERM_THRS_HIGHER = 1
};

enum nvkm_therm_thrs {
	NVKM_THERM_THRS_FANBOOST = 0,
	NVKM_THERM_THRS_DOWNCLOCK = 1,
	NVKM_THERM_THRS_CRITICAL = 2,
	NVKM_THERM_THRS_SHUTDOWN = 3,
	NVKM_THERM_THRS_NR
};

enum nvkm_therm_fan_mode {
	NVKM_THERM_CTRL_NONE = 0,
	NVKM_THERM_CTRL_MANUAL = 1,
	NVKM_THERM_CTRL_AUTO = 2,
};

enum nvkm_therm_attr_type {
	NVKM_THERM_ATTR_FAN_MIN_DUTY = 0,
	NVKM_THERM_ATTR_FAN_MAX_DUTY = 1,
	NVKM_THERM_ATTR_FAN_MODE = 2,

	NVKM_THERM_ATTR_THRS_FAN_BOOST = 10,
	NVKM_THERM_ATTR_THRS_FAN_BOOST_HYST = 11,
	NVKM_THERM_ATTR_THRS_DOWN_CLK = 12,
	NVKM_THERM_ATTR_THRS_DOWN_CLK_HYST = 13,
	NVKM_THERM_ATTR_THRS_CRITICAL = 14,
	NVKM_THERM_ATTR_THRS_CRITICAL_HYST = 15,
	NVKM_THERM_ATTR_THRS_SHUTDOWN = 16,
	NVKM_THERM_ATTR_THRS_SHUTDOWN_HYST = 17,
};

struct nvkm_therm {
	const struct nvkm_therm_func *func;
	struct nvkm_subdev subdev;

	/* automatic thermal management */
	struct nvkm_alarm alarm;
	spinlock_t lock;
	struct nvbios_therm_trip_point *last_trip;
	int mode;
	int cstate;
	int suspend;

	/* bios */
	struct nvbios_therm_sensor bios_sensor;

	/* fan priv */
	struct nvkm_fan *fan;

	/* alarms priv */
	struct {
		spinlock_t alarm_program_lock;
		struct nvkm_alarm therm_poll_alarm;
		enum nvkm_therm_thrs_state alarm_state[NVKM_THERM_THRS_NR];
	} sensor;

	/* what should be done if the card overheats */
	struct {
		void (*downclock)(struct nvkm_therm *, bool active);
		void (*pause)(struct nvkm_therm *, bool active);
	} emergency;

	/* ic */
	struct i2c_client *ic;

	int (*fan_get)(struct nvkm_therm *);
	int (*fan_set)(struct nvkm_therm *, int);

	int (*attr_get)(struct nvkm_therm *, enum nvkm_therm_attr_type);
	int (*attr_set)(struct nvkm_therm *, enum nvkm_therm_attr_type, int);
};

int nvkm_therm_temp_get(struct nvkm_therm *);
int nvkm_therm_fan_sense(struct nvkm_therm *);
int nvkm_therm_cstate(struct nvkm_therm *, int, int);

int nv40_therm_new(struct nvkm_device *, int, struct nvkm_therm **);
int nv50_therm_new(struct nvkm_device *, int, struct nvkm_therm **);
int g84_therm_new(struct nvkm_device *, int, struct nvkm_therm **);
int gt215_therm_new(struct nvkm_device *, int, struct nvkm_therm **);
int gf119_therm_new(struct nvkm_device *, int, struct nvkm_therm **);
int gm107_therm_new(struct nvkm_device *, int, struct nvkm_therm **);
int gm200_therm_new(struct nvkm_device *, int, struct nvkm_therm **);
#endif
