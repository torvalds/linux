#ifndef __NVKM_THERM_H__
#define __NVKM_THERM_H__
#include <core/subdev.h>

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
	struct nvkm_subdev base;

	int (*pwm_ctrl)(struct nvkm_therm *, int line, bool);
	int (*pwm_get)(struct nvkm_therm *, int line, u32 *, u32 *);
	int (*pwm_set)(struct nvkm_therm *, int line, u32, u32);
	int (*pwm_clock)(struct nvkm_therm *, int line);

	int (*fan_get)(struct nvkm_therm *);
	int (*fan_set)(struct nvkm_therm *, int);
	int (*fan_sense)(struct nvkm_therm *);

	int (*temp_get)(struct nvkm_therm *);

	int (*attr_get)(struct nvkm_therm *, enum nvkm_therm_attr_type);
	int (*attr_set)(struct nvkm_therm *, enum nvkm_therm_attr_type, int);
};

static inline struct nvkm_therm *
nvkm_therm(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_THERM);
}

#define nvkm_therm_create(p,e,o,d)                                          \
	nvkm_therm_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_therm_destroy(p) ({                                            \
	struct nvkm_therm *therm = (p);                                     \
        _nvkm_therm_dtor(nv_object(therm));                                 \
})
#define nvkm_therm_init(p) ({                                               \
	struct nvkm_therm *therm = (p);                                     \
        _nvkm_therm_init(nv_object(therm));                                 \
})
#define nvkm_therm_fini(p,s) ({                                             \
	struct nvkm_therm *therm = (p);                                     \
        _nvkm_therm_init(nv_object(therm), (s));                            \
})

int  nvkm_therm_create_(struct nvkm_object *, struct nvkm_object *,
			   struct nvkm_oclass *, int, void **);
void _nvkm_therm_dtor(struct nvkm_object *);
int  _nvkm_therm_init(struct nvkm_object *);
int  _nvkm_therm_fini(struct nvkm_object *, bool);

int  nvkm_therm_cstate(struct nvkm_therm *, int, int);

extern struct nvkm_oclass nv40_therm_oclass;
extern struct nvkm_oclass nv50_therm_oclass;
extern struct nvkm_oclass g84_therm_oclass;
extern struct nvkm_oclass gt215_therm_oclass;
extern struct nvkm_oclass gf110_therm_oclass;
extern struct nvkm_oclass gm107_therm_oclass;
#endif
