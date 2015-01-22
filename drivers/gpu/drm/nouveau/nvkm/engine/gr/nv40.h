#ifndef __NV40_GR_H__
#define __NV40_GR_H__
#include <engine/gr.h>

#include <core/device.h>
struct nvkm_gpuobj;

/* returns 1 if device is one of the nv4x using the 0x4497 object class,
 * helpful to determine a number of other hardware features
 */
static inline int
nv44_gr_class(void *priv)
{
	struct nvkm_device *device = nv_device(priv);

	if ((device->chipset & 0xf0) == 0x60)
		return 1;

	return !(0x0baf & (1 << (device->chipset & 0x0f)));
}

int  nv40_grctx_init(struct nvkm_device *, u32 *size);
void nv40_grctx_fill(struct nvkm_device *, struct nvkm_gpuobj *);
#endif
