#ifndef __NVIF_DEVICE_H__
#define __NVIF_DEVICE_H__

#include <nvif/object.h>
#include <nvif/class.h>

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
#include <subdev/mmu.h>
#include <subdev/bar.h>
#include <subdev/gpio.h>
#include <subdev/clk.h>
#include <subdev/i2c.h>
#include <subdev/timer.h>
#include <subdev/therm.h>

#define nvxx_device(a) ({                                                      \
	struct nvif_device *_device = (a);                                     \
	nv_device(_device->object.priv);                                       \
})
#define nvxx_bios(a) nvkm_bios(nvxx_device(a))
#define nvxx_fb(a) nvkm_fb(nvxx_device(a))
#define nvxx_mmu(a) nvkm_mmu(nvxx_device(a))
#define nvxx_bar(a) nvkm_bar(nvxx_device(a))
#define nvxx_gpio(a) nvkm_gpio(nvxx_device(a))
#define nvxx_clk(a) nvkm_clk(nvxx_device(a))
#define nvxx_i2c(a) nvkm_i2c(nvxx_device(a))
#define nvxx_timer(a) nvkm_timer(nvxx_device(a))
#define nvxx_therm(a) nvkm_therm(nvxx_device(a))

#include <core/device.h>
#include <engine/fifo.h>
#include <engine/gr.h>
#include <engine/sw.h>

#define nvxx_fifo(a) nvkm_fifo(nvxx_device(a))
#define nvxx_fifo_chan(a) ((struct nvkm_fifo_chan *)nvxx_object(a))
#define nvxx_gr(a) nvkm_gr(nvxx_device(a))
#endif
