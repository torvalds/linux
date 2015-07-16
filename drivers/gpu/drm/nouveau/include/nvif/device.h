#ifndef __NVIF_DEVICE_H__
#define __NVIF_DEVICE_H__

#include <nvif/object.h>
#include <nvif/class.h>

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
#include <subdev/mmu.h>
#include <subdev/bar.h>
#include <subdev/gpio.h>
#include <subdev/clk.h>
#include <subdev/i2c.h>
#include <subdev/timer.h>
#include <subdev/therm.h>

#define nvxx_device(a) nv_device(nvxx_object((a)))
#define nvxx_bios(a) nvkm_bios(nvxx_device(a))
#define nvxx_fb(a) nvkm_fb(nvxx_device(a))
#define nvxx_mmu(a) nvkm_mmu(nvxx_device(a))
#define nvxx_bar(a) nvkm_bar(nvxx_device(a))
#define nvxx_gpio(a) nvkm_gpio(nvxx_device(a))
#define nvxx_clk(a) nvkm_clk(nvxx_device(a))
#define nvxx_i2c(a) nvkm_i2c(nvxx_device(a))
#define nvxx_timer(a) nvkm_timer(nvxx_device(a))
#define nvxx_wait(a,b,c,d) nv_wait(nvxx_timer(a), (b), (c), (d))
#define nvxx_wait_cb(a,b,c) nv_wait_cb(nvxx_timer(a), (b), (c))
#define nvxx_therm(a) nvkm_therm(nvxx_device(a))

#include <core/device.h>
#include <engine/fifo.h>
#include <engine/gr.h>
#include <engine/sw.h>

#define nvxx_fifo(a) nvkm_fifo(nvxx_device(a))
#define nvxx_fifo_chan(a) ((struct nvkm_fifo_chan *)nvxx_object(a))
#define nvxx_gr(a) ((struct nvkm_gr *)nvkm_engine(nvxx_object(a), NVDEV_ENGINE_GR))
#endif
