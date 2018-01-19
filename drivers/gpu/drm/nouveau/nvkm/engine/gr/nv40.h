/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NV40_GR_H__
#define __NV40_GR_H__
#define nv40_gr(p) container_of((p), struct nv40_gr, base)
#include "priv.h"

struct nv40_gr {
	struct nvkm_gr base;
	u32 size;
	struct list_head chan;
};

int nv40_gr_new_(const struct nvkm_gr_func *, struct nvkm_device *, int index,
		 struct nvkm_gr **);
int nv40_gr_init(struct nvkm_gr *);
void nv40_gr_intr(struct nvkm_gr *);
u64 nv40_gr_units(struct nvkm_gr *);

#define nv40_gr_chan(p) container_of((p), struct nv40_gr_chan, object)

struct nv40_gr_chan {
	struct nvkm_object object;
	struct nv40_gr *gr;
	struct nvkm_fifo_chan *fifo;
	u32 inst;
	struct list_head head;
};

int nv40_gr_chan_new(struct nvkm_gr *, struct nvkm_fifo_chan *,
		     const struct nvkm_oclass *, struct nvkm_object **);

extern const struct nvkm_object_func nv40_gr_object;

/* returns 1 if device is one of the nv4x using the 0x4497 object class,
 * helpful to determine a number of other hardware features
 */
static inline int
nv44_gr_class(struct nvkm_device *device)
{
	if ((device->chipset & 0xf0) == 0x60)
		return 1;

	return !(0x0aaf & (1 << (device->chipset & 0x0f)));
}

int  nv40_grctx_init(struct nvkm_device *, u32 *size);
void nv40_grctx_fill(struct nvkm_device *, struct nvkm_gpuobj *);
#endif
