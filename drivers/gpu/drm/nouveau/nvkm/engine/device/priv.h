#ifndef __NVKM_DEVICE_PRIV_H__
#define __NVKM_DEVICE_PRIV_H__
#include <core/device.h>

#include <subdev/bar.h>
#include <subdev/bios.h>
#include <subdev/bus.h>
#include <subdev/clk.h>
#include <subdev/devinit.h>
#include <subdev/fb.h>
#include <subdev/fuse.h>
#include <subdev/gpio.h>
#include <subdev/i2c.h>
#include <subdev/ibus.h>
#include <subdev/iccsense.h>
#include <subdev/instmem.h>
#include <subdev/ltc.h>
#include <subdev/mc.h>
#include <subdev/mmu.h>
#include <subdev/mxm.h>
#include <subdev/pci.h>
#include <subdev/pmu.h>
#include <subdev/therm.h>
#include <subdev/timer.h>
#include <subdev/volt.h>
#include <subdev/secboot.h>

#include <engine/bsp.h>
#include <engine/ce.h>
#include <engine/cipher.h>
#include <engine/disp.h>
#include <engine/dma.h>
#include <engine/fifo.h>
#include <engine/gr.h>
#include <engine/mpeg.h>
#include <engine/mspdec.h>
#include <engine/msppp.h>
#include <engine/msvld.h>
#include <engine/pm.h>
#include <engine/sec.h>
#include <engine/sw.h>
#include <engine/vp.h>

int  nvkm_device_ctor(const struct nvkm_device_func *,
		      const struct nvkm_device_quirk *,
		      struct device *, enum nvkm_device_type, u64 handle,
		      const char *name, const char *cfg, const char *dbg,
		      bool detect, bool mmio, u64 subdev_mask,
		      struct nvkm_device *);
int  nvkm_device_init(struct nvkm_device *);
int  nvkm_device_fini(struct nvkm_device *, bool suspend);
#endif
