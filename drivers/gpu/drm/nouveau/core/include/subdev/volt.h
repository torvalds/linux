#ifndef __NOUVEAU_VOLT_H__
#define __NOUVEAU_VOLT_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_voltage {
	u32 uv;
	u8  id;
};

struct nouveau_volt {
	struct nouveau_subdev base;

	int (*vid_get)(struct nouveau_volt *);
	int (*get)(struct nouveau_volt *);
	int (*vid_set)(struct nouveau_volt *, u8 vid);
	int (*set)(struct nouveau_volt *, u32 uv);
	int (*set_id)(struct nouveau_volt *, u8 id, int condition);

	u8 vid_mask;
	u8 vid_nr;
	struct {
		u32 uv;
		u8 vid;
	} vid[256];
};

static inline struct nouveau_volt *
nouveau_volt(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_VOLT];
}

#define nouveau_volt_create(p, e, o, d)                                        \
	nouveau_volt_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_volt_destroy(p) ({                                             \
	struct nouveau_volt *v = (p);                                          \
	_nouveau_volt_dtor(nv_object(v));                                      \
})
#define nouveau_volt_init(p) ({                                                \
	struct nouveau_volt *v = (p);                                          \
	_nouveau_volt_init(nv_object(v));                                      \
})
#define nouveau_volt_fini(p,s)                                                 \
	nouveau_subdev_fini((p), (s))

int  nouveau_volt_create_(struct nouveau_object *, struct nouveau_object *,
			  struct nouveau_oclass *, int, void **);
void _nouveau_volt_dtor(struct nouveau_object *);
int  _nouveau_volt_init(struct nouveau_object *);
#define _nouveau_volt_fini _nouveau_subdev_fini

extern struct nouveau_oclass nv40_volt_oclass;

int nouveau_voltgpio_init(struct nouveau_volt *);
int nouveau_voltgpio_get(struct nouveau_volt *);
int nouveau_voltgpio_set(struct nouveau_volt *, u8);

#endif
