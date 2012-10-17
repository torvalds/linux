#ifndef __NOUVEAU_THERM_H__
#define __NOUVEAU_THERM_H__

#include <core/device.h>
#include <core/subdev.h>

enum nouveau_therm_fan_mode {
	FAN_CONTROL_NONE = 0,
	FAN_CONTROL_MANUAL = 1,
	FAN_CONTROL_NR,
};

enum nouveau_therm_attr_type {
	NOUVEAU_THERM_ATTR_FAN_MIN_DUTY = 0,
	NOUVEAU_THERM_ATTR_FAN_MAX_DUTY = 1,
	NOUVEAU_THERM_ATTR_FAN_MODE = 2,

	NOUVEAU_THERM_ATTR_THRS_FAN_BOOST = 10,
	NOUVEAU_THERM_ATTR_THRS_FAN_BOOST_HYST = 11,
	NOUVEAU_THERM_ATTR_THRS_DOWN_CLK = 12,
	NOUVEAU_THERM_ATTR_THRS_DOWN_CLK_HYST = 13,
	NOUVEAU_THERM_ATTR_THRS_CRITICAL = 14,
	NOUVEAU_THERM_ATTR_THRS_CRITICAL_HYST = 15,
	NOUVEAU_THERM_ATTR_THRS_SHUTDOWN = 16,
	NOUVEAU_THERM_ATTR_THRS_SHUTDOWN_HYST = 17,
};

struct nouveau_therm {
	struct nouveau_subdev base;

	int (*fan_get)(struct nouveau_therm *);
	int (*fan_set)(struct nouveau_therm *, int);
	int (*fan_sense)(struct nouveau_therm *);

	int (*temp_get)(struct nouveau_therm *);

	int (*attr_get)(struct nouveau_therm *, enum nouveau_therm_attr_type);
	int (*attr_set)(struct nouveau_therm *,
			enum nouveau_therm_attr_type, int);
};

static inline struct nouveau_therm *
nouveau_therm(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_THERM];
}

#define nouveau_therm_create(p,e,o,d)                                          \
	nouveau_subdev_create((p), (e), (o), 0, "THERM", "therm", d)
#define nouveau_therm_destroy(p)                                               \
	nouveau_subdev_destroy(&(p)->base)

#define _nouveau_therm_dtor _nouveau_subdev_dtor

extern struct nouveau_oclass nv40_therm_oclass;
extern struct nouveau_oclass nv50_therm_oclass;

#endif
