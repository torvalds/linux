/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_DEVICE_H__
#define __NVIF_DEVICE_H__

#include <nvif/object.h>
#include <nvif/cl0080.h>
#include <nvif/user.h>

struct nvif_device {
	struct nvif_object object;
	struct nv_device_info_v0 info;

	struct nvif_fifo_runlist {
		u64 engines;
	} *runlist;
	int runlists;

	struct nvif_user user;
};

int  nvif_device_ctor(struct nvif_object *, const char *name, u32 handle,
		      s32 oclass, void *, u32, struct nvif_device *);
void nvif_device_dtor(struct nvif_device *);
u64  nvif_device_time(struct nvif_device *);

/*XXX*/
#include <subdev/bios.h>
#include <subdev/fb.h>
#include <subdev/bar.h>
#include <subdev/gpio.h>
#include <subdev/clk.h>
#include <subdev/i2c.h>
#include <subdev/timer.h>
#include <subdev/therm.h>
#include <subdev/pci.h>

#define nvxx_device(a) ({                                                      \
	struct nvif_device *_device = (a);                                     \
	struct {                                                               \
		struct nvkm_object object;                                     \
		struct nvkm_device *device;                                    \
	} *_udevice = _device->object.priv;                                    \
	_udevice->device;                                                      \
})
#define nvxx_bios(a) nvxx_device(a)->bios
#define nvxx_fb(a) nvxx_device(a)->fb
#define nvxx_gpio(a) nvxx_device(a)->gpio
#define nvxx_clk(a) nvxx_device(a)->clk
#define nvxx_i2c(a) nvxx_device(a)->i2c
#define nvxx_iccsense(a) nvxx_device(a)->iccsense
#define nvxx_therm(a) nvxx_device(a)->therm
#define nvxx_volt(a) nvxx_device(a)->volt

#include <engine/fifo.h>
#include <engine/gr.h>

#define nvxx_gr(a) nvxx_device(a)->gr
#endif
