#ifndef __NVKM_BUS_H__
#define __NVKM_BUS_H__
#include <core/subdev.h>

struct nvkm_bus_intr {
	u32 stat;
	u32 unit;
};

struct nvkm_bus {
	struct nvkm_subdev base;
	int (*hwsq_exec)(struct nvkm_bus *, u32 *, u32);
	u32 hwsq_size;
};

static inline struct nvkm_bus *
nvkm_bus(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_BUS);
}

#define nvkm_bus_create(p, e, o, d)                                         \
	nvkm_subdev_create_((p), (e), (o), 0, "PBUS", "master",             \
			       sizeof(**d), (void **)d)
#define nvkm_bus_destroy(p)                                                 \
	nvkm_subdev_destroy(&(p)->base)
#define nvkm_bus_init(p)                                                    \
	nvkm_subdev_init(&(p)->base)
#define nvkm_bus_fini(p, s)                                                 \
	nvkm_subdev_fini(&(p)->base, (s))

#define _nvkm_bus_dtor _nvkm_subdev_dtor
#define _nvkm_bus_init _nvkm_subdev_init
#define _nvkm_bus_fini _nvkm_subdev_fini

extern struct nvkm_oclass *nv04_bus_oclass;
extern struct nvkm_oclass *nv31_bus_oclass;
extern struct nvkm_oclass *nv50_bus_oclass;
extern struct nvkm_oclass *g94_bus_oclass;
extern struct nvkm_oclass *gf100_bus_oclass;

/* interface to sequencer */
struct nvkm_hwsq;
int  nvkm_hwsq_init(struct nvkm_bus *, struct nvkm_hwsq **);
int  nvkm_hwsq_fini(struct nvkm_hwsq **, bool exec);
void nvkm_hwsq_wr32(struct nvkm_hwsq *, u32 addr, u32 data);
void nvkm_hwsq_setf(struct nvkm_hwsq *, u8 flag, int data);
void nvkm_hwsq_wait(struct nvkm_hwsq *, u8 flag, u8 data);
void nvkm_hwsq_nsec(struct nvkm_hwsq *, u32 nsec);
#endif
