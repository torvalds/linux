#ifndef __NVIF_DEVICE_H__
#define __NVIF_DEVICE_H__

#include "object.h"
#include "class.h"

struct nvif_device {
	struct nvif_object base;
	struct nvif_object *object; /*XXX: hack for nvif_object() */
	struct nv_device_info_v0 info;
};

static inline struct nvif_device *
nvif_device(struct nvif_object *object)
{
	while (object && object->oclass != 0x0080 /*XXX: NV_DEVICE_CLASS*/ )
		object = object->parent;
	return (void *)object;
}

int  nvif_device_init(struct nvif_object *, void (*dtor)(struct nvif_device *),
		      u32 handle, u32 oclass, void *, u32,
		      struct nvif_device *);
void nvif_device_fini(struct nvif_device *);
int  nvif_device_new(struct nvif_object *, u32 handle, u32 oclass,
		     void *, u32, struct nvif_device **);
void nvif_device_ref(struct nvif_device *, struct nvif_device **);

/*XXX*/
#include <subdev/bios.h>
#include <subdev/fb.h>
#include <subdev/vm.h>
#include <subdev/bar.h>
#include <subdev/gpio.h>
#include <subdev/clock.h>
#include <subdev/i2c.h>
#include <subdev/timer.h>
#include <subdev/therm.h>

#define nvkm_device(a) nv_device(nvkm_object((a)))
#define nvkm_bios(a) nouveau_bios(nvkm_device(a))
#define nvkm_fb(a) nouveau_fb(nvkm_device(a))
#define nvkm_vmmgr(a) nouveau_vmmgr(nvkm_device(a))
#define nvkm_bar(a) nouveau_bar(nvkm_device(a))
#define nvkm_gpio(a) nouveau_gpio(nvkm_device(a))
#define nvkm_clock(a) nouveau_clock(nvkm_device(a))
#define nvkm_i2c(a) nouveau_i2c(nvkm_device(a))
#define nvkm_timer(a) nouveau_timer(nvkm_device(a))
#define nvkm_wait(a,b,c,d) nv_wait(nvkm_timer(a), (b), (c), (d))
#define nvkm_wait_cb(a,b,c) nv_wait_cb(nvkm_timer(a), (b), (c))
#define nvkm_therm(a) nouveau_therm(nvkm_device(a))

#include <engine/device.h>
#include <engine/fifo.h>
#include <engine/graph.h>
#include <engine/software.h>

#define nvkm_fifo(a) nouveau_fifo(nvkm_device(a))
#define nvkm_fifo_chan(a) ((struct nouveau_fifo_chan *)nvkm_object(a))
#define nvkm_gr(a) ((struct nouveau_graph *)nouveau_engine(nvkm_object(a), NVDEV_ENGINE_GR))

#endif
