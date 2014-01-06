#ifndef __NOUVEAU_BUS_H__
#define __NOUVEAU_BUS_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_bus_intr {
	u32 stat;
	u32 unit;
};

struct nouveau_bus {
	struct nouveau_subdev base;
	int (*hwsq_exec)(struct nouveau_bus *, u32 *, u32);
	u32 hwsq_size;
};

static inline struct nouveau_bus *
nouveau_bus(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_BUS];
}

#define nouveau_bus_create(p, e, o, d)                                         \
	nouveau_subdev_create_((p), (e), (o), 0, "PBUS", "master",             \
			       sizeof(**d), (void **)d)
#define nouveau_bus_destroy(p)                                                 \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_bus_init(p)                                                    \
	nouveau_subdev_init(&(p)->base)
#define nouveau_bus_fini(p, s)                                                 \
	nouveau_subdev_fini(&(p)->base, (s))

#define _nouveau_bus_dtor _nouveau_subdev_dtor
#define _nouveau_bus_init _nouveau_subdev_init
#define _nouveau_bus_fini _nouveau_subdev_fini

extern struct nouveau_oclass *nv04_bus_oclass;
extern struct nouveau_oclass *nv31_bus_oclass;
extern struct nouveau_oclass *nv50_bus_oclass;
extern struct nouveau_oclass *nv94_bus_oclass;
extern struct nouveau_oclass *nvc0_bus_oclass;

/* interface to sequencer */
struct nouveau_hwsq;
int  nouveau_hwsq_init(struct nouveau_bus *, struct nouveau_hwsq **);
int  nouveau_hwsq_fini(struct nouveau_hwsq **, bool exec);
void nouveau_hwsq_wr32(struct nouveau_hwsq *, u32 addr, u32 data);
void nouveau_hwsq_setf(struct nouveau_hwsq *, u8 flag, int data);
void nouveau_hwsq_wait(struct nouveau_hwsq *, u8 flag, u8 data);
void nouveau_hwsq_nsec(struct nouveau_hwsq *, u32 nsec);

#endif
