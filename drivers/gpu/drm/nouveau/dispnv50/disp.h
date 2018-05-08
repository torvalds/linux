#ifndef __NV50_KMS_H__
#define __NV50_KMS_H__
#include <nvif/mem.h>

#include "nouveau_display.h"

struct nv50_disp {
	struct nvif_disp *disp;
	struct nv50_core *core;

#define NV50_DISP_SYNC(c, o)                                ((c) * 0x040 + (o))
#define NV50_DISP_CORE_NTFY                       NV50_DISP_SYNC(0      , 0x00)
#define NV50_DISP_WNDW_SEM0(c)                    NV50_DISP_SYNC(1 + (c), 0x00)
#define NV50_DISP_WNDW_SEM1(c)                    NV50_DISP_SYNC(1 + (c), 0x10)
#define NV50_DISP_WNDW_NTFY(c)                    NV50_DISP_SYNC(1 + (c), 0x20)
#define NV50_DISP_BASE_SEM0(c)                    NV50_DISP_WNDW_SEM0(0 + (c))
#define NV50_DISP_BASE_SEM1(c)                    NV50_DISP_WNDW_SEM1(0 + (c))
#define NV50_DISP_BASE_NTFY(c)                    NV50_DISP_WNDW_NTFY(0 + (c))
	struct nouveau_bo *sync;

	struct mutex mutex;
};

static inline struct nv50_disp *
nv50_disp(struct drm_device *dev)
{
	return nouveau_display(dev)->priv;
}

struct nv50_chan {
	struct nvif_object user;
	struct nvif_device *device;
};

struct nv50_dmac {
	struct nv50_chan base;

	struct nvif_mem push;
	u32 *ptr;

	struct nvif_object sync;
	struct nvif_object vram;

	/* Protects against concurrent pushbuf access to this channel, lock is
	 * grabbed by evo_wait (if the pushbuf reservation is successful) and
	 * dropped again by evo_kick. */
	struct mutex lock;
};

int nv50_dmac_create(struct nvif_device *device, struct nvif_object *disp,
		     const s32 *oclass, u8 head, void *data, u32 size,
		     u64 syncbuf, struct nv50_dmac *dmac);
void nv50_dmac_destroy(struct nv50_dmac *);

u32 *evo_wait(struct nv50_dmac *, int nr);
void evo_kick(u32 *, struct nv50_dmac *);

#define evo_mthd(p, m, s) do {						\
	const u32 _m = (m), _s = (s);					\
	if (drm_debug & DRM_UT_KMS)					\
		pr_err("%04x %d %s\n", _m, _s, __func__);		\
	*((p)++) = ((_s << 18) | _m);					\
} while(0)

#define evo_data(p, d) do {						\
	const u32 _d = (d);						\
	if (drm_debug & DRM_UT_KMS)					\
		pr_err("\t%08x\n", _d);					\
	*((p)++) = _d;							\
} while(0)
#endif
