/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVIF_DEVICE_H__
#define __NVIF_DEVICE_H__

#include <nvif/object.h>
#include <nvif/cl0080.h>

struct nvif_device {
	struct nvif_object object;
	struct nv_device_info_v0 info;
};

int  nvif_device_init(struct nvif_object *, u32 handle, s32 oclass, void *, u32,
		      struct nvif_device *);
void nvif_device_fini(struct nvif_device *);
u64  nvif_device_time(struct nvif_device *);

/* Delay based on GPU time (ie. PTIMER).
 *
 * Will return -ETIMEDOUT unless the loop was terminated with 'break',
 * where it will return the number of nanoseconds taken instead.
 */
#define nvif_nsec(d,n,cond...) ({                                              \
	struct nvif_device *_device = (d);                                     \
	u64 _nsecs = (n), _time0 = nvif_device_time(_device);                  \
	s64 _taken = 0;                                                        \
                                                                               \
	do {                                                                   \
		cond                                                           \
	} while (_taken = nvif_device_time(_device) - _time0, _taken < _nsecs);\
                                                                               \
	if (_taken >= _nsecs)                                                  \
		_taken = -ETIMEDOUT;                                           \
	_taken;                                                                \
})
#define nvif_usec(d,u,cond...) nvif_nsec((d), (u) * 1000, ##cond)
#define nvif_msec(d,m,cond...) nvif_usec((d), (m) * 1000, ##cond)

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

#define nvxx_fifo(a) nvxx_device(a)->fifo
#define nvxx_gr(a) nvxx_device(a)->gr
#endif
