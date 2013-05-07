#ifndef __NOUVEAU_THERM_H__
#define __NOUVEAU_THERM_H__

#include <core/device.h>
#include <core/subdev.h>

enum nouveau_therm_fan_mode {
	NOUVEAU_THERM_CTRL_NONE = 0,
	NOUVEAU_THERM_CTRL_MANUAL = 1,
	NOUVEAU_THERM_CTRL_AUTO = 2,
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

	int (*pwm_ctrl)(struct nouveau_therm *, int line, bool);
	int (*pwm_get)(struct nouveau_therm *, int line, u32 *, u32 *);
	int (*pwm_set)(struct nouveau_therm *, int line, u32, u32);
	int (*pwm_clock)(struct nouveau_therm *);

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
	nouveau_therm_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_therm_destroy(p) ({                                            \
	struct nouveau_therm *therm = (p);                                     \
        _nouveau_therm_dtor(nv_object(therm));                                 \
})
#define nouveau_therm_init(p) ({                                               \
	struct nouveau_therm *therm = (p);                                     \
        _nouveau_therm_init(nv_object(therm));                                 \
})
#define nouveau_therm_fini(p,s) ({                                             \
	struct nouveau_therm *therm = (p);                                     \
        _nouveau_therm_init(nv_object(therm), (s));                            \
})

int  nouveau_therm_create_(struct nouveau_object *, struct nouveau_object *,
			   struct nouveau_oclass *, int, void **);
void _nouveau_therm_dtor(struct nouveau_object *);
int  _nouveau_therm_init(struct nouveau_object *);
int  _nouveau_therm_fini(struct nouveau_object *, bool);

extern struct nouveau_oclass nv40_therm_oclass;
extern struct nouveau_oclass nv50_therm_oclass;
extern struct nouveau_oclass nva3_therm_oclass;
extern struct nouveau_oclass nvd0_therm_oclass;

#endif
