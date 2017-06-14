/*
 * Copyright 2011 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <linux/dma-mapping.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_plane_helper.h>

#include <nvif/class.h>
#include <nvif/cl0002.h>
#include <nvif/cl5070.h>
#include <nvif/cl507a.h>
#include <nvif/cl507b.h>
#include <nvif/cl507c.h>
#include <nvif/cl507d.h>
#include <nvif/cl507e.h>
#include <nvif/event.h>

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_gem.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"
#include "nouveau_fence.h"
#include "nouveau_fbcon.h"
#include "nv50_display.h"

#define EVO_DMA_NR 9

#define EVO_MASTER  (0x00)
#define EVO_FLIP(c) (0x01 + (c))
#define EVO_OVLY(c) (0x05 + (c))
#define EVO_OIMM(c) (0x09 + (c))
#define EVO_CURS(c) (0x0d + (c))

/* offsets in shared sync bo of various structures */
#define EVO_SYNC(c, o) ((c) * 0x0100 + (o))
#define EVO_MAST_NTFY     EVO_SYNC(      0, 0x00)
#define EVO_FLIP_SEM0(c)  EVO_SYNC((c) + 1, 0x00)
#define EVO_FLIP_SEM1(c)  EVO_SYNC((c) + 1, 0x10)
#define EVO_FLIP_NTFY0(c) EVO_SYNC((c) + 1, 0x20)
#define EVO_FLIP_NTFY1(c) EVO_SYNC((c) + 1, 0x30)

/******************************************************************************
 * Atomic state
 *****************************************************************************/
#define nv50_atom(p) container_of((p), struct nv50_atom, state)

struct nv50_atom {
	struct drm_atomic_state state;

	struct list_head outp;
	bool lock_core;
	bool flush_disable;
};

struct nv50_outp_atom {
	struct list_head head;

	struct drm_encoder *encoder;
	bool flush_disable;

	union {
		struct {
			bool ctrl:1;
		};
		u8 mask;
	} clr;

	union {
		struct {
			bool ctrl:1;
		};
		u8 mask;
	} set;
};

#define nv50_head_atom(p) container_of((p), struct nv50_head_atom, state)

struct nv50_head_atom {
	struct drm_crtc_state state;

	struct {
		u16 iW;
		u16 iH;
		u16 oW;
		u16 oH;
	} view;

	struct nv50_head_mode {
		bool interlace;
		u32 clock;
		struct {
			u16 active;
			u16 synce;
			u16 blanke;
			u16 blanks;
		} h;
		struct {
			u32 active;
			u16 synce;
			u16 blanke;
			u16 blanks;
			u16 blank2s;
			u16 blank2e;
			u16 blankus;
		} v;
	} mode;

	struct {
		u32 handle;
		u64 offset:40;
	} lut;

	struct {
		bool visible;
		u32 handle;
		u64 offset:40;
		u8  format;
		u8  kind:7;
		u8  layout:1;
		u8  block:4;
		u32 pitch:20;
		u16 x;
		u16 y;
		u16 w;
		u16 h;
	} core;

	struct {
		bool visible;
		u32 handle;
		u64 offset:40;
		u8  layout:1;
		u8  format:1;
	} curs;

	struct {
		u8  depth;
		u8  cpp;
		u16 x;
		u16 y;
		u16 w;
		u16 h;
	} base;

	struct {
		u8 cpp;
	} ovly;

	struct {
		bool enable:1;
		u8 bits:2;
		u8 mode:4;
	} dither;

	struct {
		struct {
			u16 cos:12;
			u16 sin:12;
		} sat;
	} procamp;

	union {
		struct {
			bool core:1;
			bool curs:1;
		};
		u8 mask;
	} clr;

	union {
		struct {
			bool core:1;
			bool curs:1;
			bool view:1;
			bool mode:1;
			bool base:1;
			bool ovly:1;
			bool dither:1;
			bool procamp:1;
		};
		u16 mask;
	} set;
};

static inline struct nv50_head_atom *
nv50_head_atom_get(struct drm_atomic_state *state, struct drm_crtc *crtc)
{
	struct drm_crtc_state *statec = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(statec))
		return (void *)statec;
	return nv50_head_atom(statec);
}

#define nv50_wndw_atom(p) container_of((p), struct nv50_wndw_atom, state)

struct nv50_wndw_atom {
	struct drm_plane_state state;
	u8 interval;

	struct drm_rect clip;

	struct {
		u32  handle;
		u16  offset:12;
		bool awaken:1;
	} ntfy;

	struct {
		u32 handle;
		u16 offset:12;
		u32 acquire;
		u32 release;
	} sema;

	struct {
		u8 enable:2;
	} lut;

	struct {
		u8  mode:2;
		u8  interval:4;

		u8  format;
		u8  kind:7;
		u8  layout:1;
		u8  block:4;
		u32 pitch:20;
		u16 w;
		u16 h;

		u32 handle;
		u64 offset;
	} image;

	struct {
		u16 x;
		u16 y;
	} point;

	union {
		struct {
			bool ntfy:1;
			bool sema:1;
			bool image:1;
		};
		u8 mask;
	} clr;

	union {
		struct {
			bool ntfy:1;
			bool sema:1;
			bool image:1;
			bool lut:1;
			bool point:1;
		};
		u8 mask;
	} set;
};

/******************************************************************************
 * EVO channel
 *****************************************************************************/

struct nv50_chan {
	struct nvif_object user;
	struct nvif_device *device;
};

static int
nv50_chan_create(struct nvif_device *device, struct nvif_object *disp,
		 const s32 *oclass, u8 head, void *data, u32 size,
		 struct nv50_chan *chan)
{
	struct nvif_sclass *sclass;
	int ret, i, n;

	chan->device = device;

	ret = n = nvif_object_sclass_get(disp, &sclass);
	if (ret < 0)
		return ret;

	while (oclass[0]) {
		for (i = 0; i < n; i++) {
			if (sclass[i].oclass == oclass[0]) {
				ret = nvif_object_init(disp, 0, oclass[0],
						       data, size, &chan->user);
				if (ret == 0)
					nvif_object_map(&chan->user);
				nvif_object_sclass_put(&sclass);
				return ret;
			}
		}
		oclass++;
	}

	nvif_object_sclass_put(&sclass);
	return -ENOSYS;
}

static void
nv50_chan_destroy(struct nv50_chan *chan)
{
	nvif_object_fini(&chan->user);
}

/******************************************************************************
 * PIO EVO channel
 *****************************************************************************/

struct nv50_pioc {
	struct nv50_chan base;
};

static void
nv50_pioc_destroy(struct nv50_pioc *pioc)
{
	nv50_chan_destroy(&pioc->base);
}

static int
nv50_pioc_create(struct nvif_device *device, struct nvif_object *disp,
		 const s32 *oclass, u8 head, void *data, u32 size,
		 struct nv50_pioc *pioc)
{
	return nv50_chan_create(device, disp, oclass, head, data, size,
				&pioc->base);
}

/******************************************************************************
 * Overlay Immediate
 *****************************************************************************/

struct nv50_oimm {
	struct nv50_pioc base;
};

static int
nv50_oimm_create(struct nvif_device *device, struct nvif_object *disp,
		 int head, struct nv50_oimm *oimm)
{
	struct nv50_disp_cursor_v0 args = {
		.head = head,
	};
	static const s32 oclass[] = {
		GK104_DISP_OVERLAY,
		GF110_DISP_OVERLAY,
		GT214_DISP_OVERLAY,
		G82_DISP_OVERLAY,
		NV50_DISP_OVERLAY,
		0
	};

	return nv50_pioc_create(device, disp, oclass, head, &args, sizeof(args),
				&oimm->base);
}

/******************************************************************************
 * DMA EVO channel
 *****************************************************************************/

struct nv50_dmac_ctxdma {
	struct list_head head;
	struct nvif_object object;
};

struct nv50_dmac {
	struct nv50_chan base;
	dma_addr_t handle;
	u32 *ptr;

	struct nvif_object sync;
	struct nvif_object vram;
	struct list_head ctxdma;

	/* Protects against concurrent pushbuf access to this channel, lock is
	 * grabbed by evo_wait (if the pushbuf reservation is successful) and
	 * dropped again by evo_kick. */
	struct mutex lock;
};

static void
nv50_dmac_ctxdma_del(struct nv50_dmac_ctxdma *ctxdma)
{
	nvif_object_fini(&ctxdma->object);
	list_del(&ctxdma->head);
	kfree(ctxdma);
}

static struct nv50_dmac_ctxdma *
nv50_dmac_ctxdma_new(struct nv50_dmac *dmac, struct nouveau_framebuffer *fb)
{
	struct nouveau_drm *drm = nouveau_drm(fb->base.dev);
	struct nv50_dmac_ctxdma *ctxdma;
	const u8    kind = (fb->nvbo->tile_flags & 0x0000ff00) >> 8;
	const u32 handle = 0xfb000000 | kind;
	struct {
		struct nv_dma_v0 base;
		union {
			struct nv50_dma_v0 nv50;
			struct gf100_dma_v0 gf100;
			struct gf119_dma_v0 gf119;
		};
	} args = {};
	u32 argc = sizeof(args.base);
	int ret;

	list_for_each_entry(ctxdma, &dmac->ctxdma, head) {
		if (ctxdma->object.handle == handle)
			return ctxdma;
	}

	if (!(ctxdma = kzalloc(sizeof(*ctxdma), GFP_KERNEL)))
		return ERR_PTR(-ENOMEM);
	list_add(&ctxdma->head, &dmac->ctxdma);

	args.base.target = NV_DMA_V0_TARGET_VRAM;
	args.base.access = NV_DMA_V0_ACCESS_RDWR;
	args.base.start  = 0;
	args.base.limit  = drm->client.device.info.ram_user - 1;

	if (drm->client.device.info.chipset < 0x80) {
		args.nv50.part = NV50_DMA_V0_PART_256;
		argc += sizeof(args.nv50);
	} else
	if (drm->client.device.info.chipset < 0xc0) {
		args.nv50.part = NV50_DMA_V0_PART_256;
		args.nv50.kind = kind;
		argc += sizeof(args.nv50);
	} else
	if (drm->client.device.info.chipset < 0xd0) {
		args.gf100.kind = kind;
		argc += sizeof(args.gf100);
	} else {
		args.gf119.page = GF119_DMA_V0_PAGE_LP;
		args.gf119.kind = kind;
		argc += sizeof(args.gf119);
	}

	ret = nvif_object_init(&dmac->base.user, handle, NV_DMA_IN_MEMORY,
			       &args, argc, &ctxdma->object);
	if (ret) {
		nv50_dmac_ctxdma_del(ctxdma);
		return ERR_PTR(ret);
	}

	return ctxdma;
}

static void
nv50_dmac_destroy(struct nv50_dmac *dmac, struct nvif_object *disp)
{
	struct nvif_device *device = dmac->base.device;
	struct nv50_dmac_ctxdma *ctxdma, *ctxtmp;

	list_for_each_entry_safe(ctxdma, ctxtmp, &dmac->ctxdma, head) {
		nv50_dmac_ctxdma_del(ctxdma);
	}

	nvif_object_fini(&dmac->vram);
	nvif_object_fini(&dmac->sync);

	nv50_chan_destroy(&dmac->base);

	if (dmac->ptr) {
		struct device *dev = nvxx_device(device)->dev;
		dma_free_coherent(dev, PAGE_SIZE, dmac->ptr, dmac->handle);
	}
}

static int
nv50_dmac_create(struct nvif_device *device, struct nvif_object *disp,
		 const s32 *oclass, u8 head, void *data, u32 size, u64 syncbuf,
		 struct nv50_dmac *dmac)
{
	struct nv50_disp_core_channel_dma_v0 *args = data;
	struct nvif_object pushbuf;
	int ret;

	mutex_init(&dmac->lock);

	dmac->ptr = dma_alloc_coherent(nvxx_device(device)->dev, PAGE_SIZE,
				       &dmac->handle, GFP_KERNEL);
	if (!dmac->ptr)
		return -ENOMEM;

	ret = nvif_object_init(&device->object, 0, NV_DMA_FROM_MEMORY,
			       &(struct nv_dma_v0) {
					.target = NV_DMA_V0_TARGET_PCI_US,
					.access = NV_DMA_V0_ACCESS_RD,
					.start = dmac->handle + 0x0000,
					.limit = dmac->handle + 0x0fff,
			       }, sizeof(struct nv_dma_v0), &pushbuf);
	if (ret)
		return ret;

	args->pushbuf = nvif_handle(&pushbuf);

	ret = nv50_chan_create(device, disp, oclass, head, data, size,
			       &dmac->base);
	nvif_object_fini(&pushbuf);
	if (ret)
		return ret;

	ret = nvif_object_init(&dmac->base.user, 0xf0000000, NV_DMA_IN_MEMORY,
			       &(struct nv_dma_v0) {
					.target = NV_DMA_V0_TARGET_VRAM,
					.access = NV_DMA_V0_ACCESS_RDWR,
					.start = syncbuf + 0x0000,
					.limit = syncbuf + 0x0fff,
			       }, sizeof(struct nv_dma_v0),
			       &dmac->sync);
	if (ret)
		return ret;

	ret = nvif_object_init(&dmac->base.user, 0xf0000001, NV_DMA_IN_MEMORY,
			       &(struct nv_dma_v0) {
					.target = NV_DMA_V0_TARGET_VRAM,
					.access = NV_DMA_V0_ACCESS_RDWR,
					.start = 0,
					.limit = device->info.ram_user - 1,
			       }, sizeof(struct nv_dma_v0),
			       &dmac->vram);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&dmac->ctxdma);
	return ret;
}

/******************************************************************************
 * Core
 *****************************************************************************/

struct nv50_mast {
	struct nv50_dmac base;
};

static int
nv50_core_create(struct nvif_device *device, struct nvif_object *disp,
		 u64 syncbuf, struct nv50_mast *core)
{
	struct nv50_disp_core_channel_dma_v0 args = {
		.pushbuf = 0xb0007d00,
	};
	static const s32 oclass[] = {
		GP102_DISP_CORE_CHANNEL_DMA,
		GP100_DISP_CORE_CHANNEL_DMA,
		GM200_DISP_CORE_CHANNEL_DMA,
		GM107_DISP_CORE_CHANNEL_DMA,
		GK110_DISP_CORE_CHANNEL_DMA,
		GK104_DISP_CORE_CHANNEL_DMA,
		GF110_DISP_CORE_CHANNEL_DMA,
		GT214_DISP_CORE_CHANNEL_DMA,
		GT206_DISP_CORE_CHANNEL_DMA,
		GT200_DISP_CORE_CHANNEL_DMA,
		G82_DISP_CORE_CHANNEL_DMA,
		NV50_DISP_CORE_CHANNEL_DMA,
		0
	};

	return nv50_dmac_create(device, disp, oclass, 0, &args, sizeof(args),
				syncbuf, &core->base);
}

/******************************************************************************
 * Base
 *****************************************************************************/

struct nv50_sync {
	struct nv50_dmac base;
	u32 addr;
	u32 data;
};

static int
nv50_base_create(struct nvif_device *device, struct nvif_object *disp,
		 int head, u64 syncbuf, struct nv50_sync *base)
{
	struct nv50_disp_base_channel_dma_v0 args = {
		.pushbuf = 0xb0007c00 | head,
		.head = head,
	};
	static const s32 oclass[] = {
		GK110_DISP_BASE_CHANNEL_DMA,
		GK104_DISP_BASE_CHANNEL_DMA,
		GF110_DISP_BASE_CHANNEL_DMA,
		GT214_DISP_BASE_CHANNEL_DMA,
		GT200_DISP_BASE_CHANNEL_DMA,
		G82_DISP_BASE_CHANNEL_DMA,
		NV50_DISP_BASE_CHANNEL_DMA,
		0
	};

	return nv50_dmac_create(device, disp, oclass, head, &args, sizeof(args),
				syncbuf, &base->base);
}

/******************************************************************************
 * Overlay
 *****************************************************************************/

struct nv50_ovly {
	struct nv50_dmac base;
};

static int
nv50_ovly_create(struct nvif_device *device, struct nvif_object *disp,
		 int head, u64 syncbuf, struct nv50_ovly *ovly)
{
	struct nv50_disp_overlay_channel_dma_v0 args = {
		.pushbuf = 0xb0007e00 | head,
		.head = head,
	};
	static const s32 oclass[] = {
		GK104_DISP_OVERLAY_CONTROL_DMA,
		GF110_DISP_OVERLAY_CONTROL_DMA,
		GT214_DISP_OVERLAY_CHANNEL_DMA,
		GT200_DISP_OVERLAY_CHANNEL_DMA,
		G82_DISP_OVERLAY_CHANNEL_DMA,
		NV50_DISP_OVERLAY_CHANNEL_DMA,
		0
	};

	return nv50_dmac_create(device, disp, oclass, head, &args, sizeof(args),
				syncbuf, &ovly->base);
}

struct nv50_head {
	struct nouveau_crtc base;
	struct nv50_ovly ovly;
	struct nv50_oimm oimm;
};

#define nv50_head(c) ((struct nv50_head *)nouveau_crtc(c))
#define nv50_ovly(c) (&nv50_head(c)->ovly)
#define nv50_oimm(c) (&nv50_head(c)->oimm)
#define nv50_chan(c) (&(c)->base.base)
#define nv50_vers(c) nv50_chan(c)->user.oclass

struct nv50_disp {
	struct nvif_object *disp;
	struct nv50_mast mast;

	struct nouveau_bo *sync;

	struct mutex mutex;
};

static struct nv50_disp *
nv50_disp(struct drm_device *dev)
{
	return nouveau_display(dev)->priv;
}

#define nv50_mast(d) (&nv50_disp(d)->mast)

/******************************************************************************
 * EVO channel helpers
 *****************************************************************************/
static u32 *
evo_wait(void *evoc, int nr)
{
	struct nv50_dmac *dmac = evoc;
	struct nvif_device *device = dmac->base.device;
	u32 put = nvif_rd32(&dmac->base.user, 0x0000) / 4;

	mutex_lock(&dmac->lock);
	if (put + nr >= (PAGE_SIZE / 4) - 8) {
		dmac->ptr[put] = 0x20000000;

		nvif_wr32(&dmac->base.user, 0x0000, 0x00000000);
		if (nvif_msec(device, 2000,
			if (!nvif_rd32(&dmac->base.user, 0x0004))
				break;
		) < 0) {
			mutex_unlock(&dmac->lock);
			pr_err("nouveau: evo channel stalled\n");
			return NULL;
		}

		put = 0;
	}

	return dmac->ptr + put;
}

static void
evo_kick(u32 *push, void *evoc)
{
	struct nv50_dmac *dmac = evoc;
	nvif_wr32(&dmac->base.user, 0x0000, (push - dmac->ptr) << 2);
	mutex_unlock(&dmac->lock);
}

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

/******************************************************************************
 * Plane
 *****************************************************************************/
#define nv50_wndw(p) container_of((p), struct nv50_wndw, plane)

struct nv50_wndw {
	const struct nv50_wndw_func *func;
	struct nv50_dmac *dmac;

	struct drm_plane plane;

	struct nvif_notify notify;
	u16 ntfy;
	u16 sema;
	u32 data;
};

struct nv50_wndw_func {
	void *(*dtor)(struct nv50_wndw *);
	int (*acquire)(struct nv50_wndw *, struct nv50_wndw_atom *asyw,
		       struct nv50_head_atom *asyh);
	void (*release)(struct nv50_wndw *, struct nv50_wndw_atom *asyw,
			struct nv50_head_atom *asyh);
	void (*prepare)(struct nv50_wndw *, struct nv50_head_atom *asyh,
			struct nv50_wndw_atom *asyw);

	void (*sema_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	void (*sema_clr)(struct nv50_wndw *);
	void (*ntfy_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	void (*ntfy_clr)(struct nv50_wndw *);
	int (*ntfy_wait_begun)(struct nv50_wndw *, struct nv50_wndw_atom *);
	void (*image_set)(struct nv50_wndw *, struct nv50_wndw_atom *);
	void (*image_clr)(struct nv50_wndw *);
	void (*lut)(struct nv50_wndw *, struct nv50_wndw_atom *);
	void (*point)(struct nv50_wndw *, struct nv50_wndw_atom *);

	u32 (*update)(struct nv50_wndw *, u32 interlock);
};

static int
nv50_wndw_wait_armed(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	if (asyw->set.ntfy)
		return wndw->func->ntfy_wait_begun(wndw, asyw);
	return 0;
}

static u32
nv50_wndw_flush_clr(struct nv50_wndw *wndw, u32 interlock, bool flush,
		    struct nv50_wndw_atom *asyw)
{
	if (asyw->clr.sema && (!asyw->set.sema || flush))
		wndw->func->sema_clr(wndw);
	if (asyw->clr.ntfy && (!asyw->set.ntfy || flush))
		wndw->func->ntfy_clr(wndw);
	if (asyw->clr.image && (!asyw->set.image || flush))
		wndw->func->image_clr(wndw);

	return flush ? wndw->func->update(wndw, interlock) : 0;
}

static u32
nv50_wndw_flush_set(struct nv50_wndw *wndw, u32 interlock,
		    struct nv50_wndw_atom *asyw)
{
	if (interlock) {
		asyw->image.mode = 0;
		asyw->image.interval = 1;
	}

	if (asyw->set.sema ) wndw->func->sema_set (wndw, asyw);
	if (asyw->set.ntfy ) wndw->func->ntfy_set (wndw, asyw);
	if (asyw->set.image) wndw->func->image_set(wndw, asyw);
	if (asyw->set.lut  ) wndw->func->lut      (wndw, asyw);
	if (asyw->set.point) wndw->func->point    (wndw, asyw);

	return wndw->func->update(wndw, interlock);
}

static void
nv50_wndw_atomic_check_release(struct nv50_wndw *wndw,
			       struct nv50_wndw_atom *asyw,
			       struct nv50_head_atom *asyh)
{
	struct nouveau_drm *drm = nouveau_drm(wndw->plane.dev);
	NV_ATOMIC(drm, "%s release\n", wndw->plane.name);
	wndw->func->release(wndw, asyw, asyh);
	asyw->ntfy.handle = 0;
	asyw->sema.handle = 0;
}

static int
nv50_wndw_atomic_check_acquire(struct nv50_wndw *wndw,
			       struct nv50_wndw_atom *asyw,
			       struct nv50_head_atom *asyh)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(asyw->state.fb);
	struct nouveau_drm *drm = nouveau_drm(wndw->plane.dev);
	int ret;

	NV_ATOMIC(drm, "%s acquire\n", wndw->plane.name);
	asyw->clip.x1 = 0;
	asyw->clip.y1 = 0;
	asyw->clip.x2 = asyh->state.mode.hdisplay;
	asyw->clip.y2 = asyh->state.mode.vdisplay;

	asyw->image.w = fb->base.width;
	asyw->image.h = fb->base.height;
	asyw->image.kind = (fb->nvbo->tile_flags & 0x0000ff00) >> 8;

	if (asyh->state.pageflip_flags & DRM_MODE_PAGE_FLIP_ASYNC)
		asyw->interval = 0;
	else
		asyw->interval = 1;

	if (asyw->image.kind) {
		asyw->image.layout = 0;
		if (drm->client.device.info.chipset >= 0xc0)
			asyw->image.block = fb->nvbo->tile_mode >> 4;
		else
			asyw->image.block = fb->nvbo->tile_mode;
		asyw->image.pitch = (fb->base.pitches[0] / 4) << 4;
	} else {
		asyw->image.layout = 1;
		asyw->image.block  = 0;
		asyw->image.pitch  = fb->base.pitches[0];
	}

	ret = wndw->func->acquire(wndw, asyw, asyh);
	if (ret)
		return ret;

	if (asyw->set.image) {
		if (!(asyw->image.mode = asyw->interval ? 0 : 1))
			asyw->image.interval = asyw->interval;
		else
			asyw->image.interval = 0;
	}

	return 0;
}

static int
nv50_wndw_atomic_check(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct nouveau_drm *drm = nouveau_drm(plane->dev);
	struct nv50_wndw *wndw = nv50_wndw(plane);
	struct nv50_wndw_atom *armw = nv50_wndw_atom(wndw->plane.state);
	struct nv50_wndw_atom *asyw = nv50_wndw_atom(state);
	struct nv50_head_atom *harm = NULL, *asyh = NULL;
	bool varm = false, asyv = false, asym = false;
	int ret;

	NV_ATOMIC(drm, "%s atomic_check\n", plane->name);
	if (asyw->state.crtc) {
		asyh = nv50_head_atom_get(asyw->state.state, asyw->state.crtc);
		if (IS_ERR(asyh))
			return PTR_ERR(asyh);
		asym = drm_atomic_crtc_needs_modeset(&asyh->state);
		asyv = asyh->state.active;
	}

	if (armw->state.crtc) {
		harm = nv50_head_atom_get(asyw->state.state, armw->state.crtc);
		if (IS_ERR(harm))
			return PTR_ERR(harm);
		varm = harm->state.crtc->state->active;
	}

	if (asyv) {
		asyw->point.x = asyw->state.crtc_x;
		asyw->point.y = asyw->state.crtc_y;
		if (memcmp(&armw->point, &asyw->point, sizeof(asyw->point)))
			asyw->set.point = true;

		ret = nv50_wndw_atomic_check_acquire(wndw, asyw, asyh);
		if (ret)
			return ret;
	} else
	if (varm) {
		nv50_wndw_atomic_check_release(wndw, asyw, harm);
	} else {
		return 0;
	}

	if (!asyv || asym) {
		asyw->clr.ntfy = armw->ntfy.handle != 0;
		asyw->clr.sema = armw->sema.handle != 0;
		if (wndw->func->image_clr)
			asyw->clr.image = armw->image.handle != 0;
		asyw->set.lut = wndw->func->lut && asyv;
	}

	return 0;
}

static void
nv50_wndw_cleanup_fb(struct drm_plane *plane, struct drm_plane_state *old_state)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(old_state->fb);
	struct nouveau_drm *drm = nouveau_drm(plane->dev);

	NV_ATOMIC(drm, "%s cleanup: %p\n", plane->name, old_state->fb);
	if (!old_state->fb)
		return;

	nouveau_bo_unpin(fb->nvbo);
}

static int
nv50_wndw_prepare_fb(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(state->fb);
	struct nouveau_drm *drm = nouveau_drm(plane->dev);
	struct nv50_wndw *wndw = nv50_wndw(plane);
	struct nv50_wndw_atom *asyw = nv50_wndw_atom(state);
	struct nv50_head_atom *asyh;
	struct nv50_dmac_ctxdma *ctxdma;
	int ret;

	NV_ATOMIC(drm, "%s prepare: %p\n", plane->name, state->fb);
	if (!asyw->state.fb)
		return 0;

	ret = nouveau_bo_pin(fb->nvbo, TTM_PL_FLAG_VRAM, true);
	if (ret)
		return ret;

	ctxdma = nv50_dmac_ctxdma_new(wndw->dmac, fb);
	if (IS_ERR(ctxdma)) {
		nouveau_bo_unpin(fb->nvbo);
		return PTR_ERR(ctxdma);
	}

	asyw->state.fence = reservation_object_get_excl_rcu(fb->nvbo->bo.resv);
	asyw->image.handle = ctxdma->object.handle;
	asyw->image.offset = fb->nvbo->bo.offset;

	if (wndw->func->prepare) {
		asyh = nv50_head_atom_get(asyw->state.state, asyw->state.crtc);
		if (IS_ERR(asyh))
			return PTR_ERR(asyh);

		wndw->func->prepare(wndw, asyh, asyw);
	}

	return 0;
}

static const struct drm_plane_helper_funcs
nv50_wndw_helper = {
	.prepare_fb = nv50_wndw_prepare_fb,
	.cleanup_fb = nv50_wndw_cleanup_fb,
	.atomic_check = nv50_wndw_atomic_check,
};

static void
nv50_wndw_atomic_destroy_state(struct drm_plane *plane,
			       struct drm_plane_state *state)
{
	struct nv50_wndw_atom *asyw = nv50_wndw_atom(state);
	__drm_atomic_helper_plane_destroy_state(&asyw->state);
	kfree(asyw);
}

static struct drm_plane_state *
nv50_wndw_atomic_duplicate_state(struct drm_plane *plane)
{
	struct nv50_wndw_atom *armw = nv50_wndw_atom(plane->state);
	struct nv50_wndw_atom *asyw;
	if (!(asyw = kmalloc(sizeof(*asyw), GFP_KERNEL)))
		return NULL;
	__drm_atomic_helper_plane_duplicate_state(plane, &asyw->state);
	asyw->interval = 1;
	asyw->sema = armw->sema;
	asyw->ntfy = armw->ntfy;
	asyw->image = armw->image;
	asyw->point = armw->point;
	asyw->lut = armw->lut;
	asyw->clr.mask = 0;
	asyw->set.mask = 0;
	return &asyw->state;
}

static void
nv50_wndw_reset(struct drm_plane *plane)
{
	struct nv50_wndw_atom *asyw;

	if (WARN_ON(!(asyw = kzalloc(sizeof(*asyw), GFP_KERNEL))))
		return;

	if (plane->state)
		plane->funcs->atomic_destroy_state(plane, plane->state);
	plane->state = &asyw->state;
	plane->state->plane = plane;
	plane->state->rotation = DRM_ROTATE_0;
}

static void
nv50_wndw_destroy(struct drm_plane *plane)
{
	struct nv50_wndw *wndw = nv50_wndw(plane);
	void *data;
	nvif_notify_fini(&wndw->notify);
	data = wndw->func->dtor(wndw);
	drm_plane_cleanup(&wndw->plane);
	kfree(data);
}

static const struct drm_plane_funcs
nv50_wndw = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = nv50_wndw_destroy,
	.reset = nv50_wndw_reset,
	.set_property = drm_atomic_helper_plane_set_property,
	.atomic_duplicate_state = nv50_wndw_atomic_duplicate_state,
	.atomic_destroy_state = nv50_wndw_atomic_destroy_state,
};

static void
nv50_wndw_fini(struct nv50_wndw *wndw)
{
	nvif_notify_put(&wndw->notify);
}

static void
nv50_wndw_init(struct nv50_wndw *wndw)
{
	nvif_notify_get(&wndw->notify);
}

static int
nv50_wndw_ctor(const struct nv50_wndw_func *func, struct drm_device *dev,
	       enum drm_plane_type type, const char *name, int index,
	       struct nv50_dmac *dmac, const u32 *format, int nformat,
	       struct nv50_wndw *wndw)
{
	int ret;

	wndw->func = func;
	wndw->dmac = dmac;

	ret = drm_universal_plane_init(dev, &wndw->plane, 0, &nv50_wndw, format,
				       nformat, type, "%s-%d", name, index);
	if (ret)
		return ret;

	drm_plane_helper_add(&wndw->plane, &nv50_wndw_helper);
	return 0;
}

/******************************************************************************
 * Cursor plane
 *****************************************************************************/
#define nv50_curs(p) container_of((p), struct nv50_curs, wndw)

struct nv50_curs {
	struct nv50_wndw wndw;
	struct nvif_object chan;
};

static u32
nv50_curs_update(struct nv50_wndw *wndw, u32 interlock)
{
	struct nv50_curs *curs = nv50_curs(wndw);
	nvif_wr32(&curs->chan, 0x0080, 0x00000000);
	return 0;
}

static void
nv50_curs_point(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nv50_curs *curs = nv50_curs(wndw);
	nvif_wr32(&curs->chan, 0x0084, (asyw->point.y << 16) | asyw->point.x);
}

static void
nv50_curs_prepare(struct nv50_wndw *wndw, struct nv50_head_atom *asyh,
		  struct nv50_wndw_atom *asyw)
{
	u32 handle = nv50_disp(wndw->plane.dev)->mast.base.vram.handle;
	u32 offset = asyw->image.offset;
	if (asyh->curs.handle != handle || asyh->curs.offset != offset) {
		asyh->curs.handle = handle;
		asyh->curs.offset = offset;
		asyh->set.curs = asyh->curs.visible;
	}
}

static void
nv50_curs_release(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		  struct nv50_head_atom *asyh)
{
	asyh->curs.visible = false;
}

static int
nv50_curs_acquire(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		  struct nv50_head_atom *asyh)
{
	int ret;

	ret = drm_plane_helper_check_state(&asyw->state, &asyw->clip,
					   DRM_PLANE_HELPER_NO_SCALING,
					   DRM_PLANE_HELPER_NO_SCALING,
					   true, true);
	asyh->curs.visible = asyw->state.visible;
	if (ret || !asyh->curs.visible)
		return ret;

	switch (asyw->state.fb->width) {
	case 32: asyh->curs.layout = 0; break;
	case 64: asyh->curs.layout = 1; break;
	default:
		return -EINVAL;
	}

	if (asyw->state.fb->width != asyw->state.fb->height)
		return -EINVAL;

	switch (asyw->state.fb->format->format) {
	case DRM_FORMAT_ARGB8888: asyh->curs.format = 1; break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	return 0;
}

static void *
nv50_curs_dtor(struct nv50_wndw *wndw)
{
	struct nv50_curs *curs = nv50_curs(wndw);
	nvif_object_fini(&curs->chan);
	return curs;
}

static const u32
nv50_curs_format[] = {
	DRM_FORMAT_ARGB8888,
};

static const struct nv50_wndw_func
nv50_curs = {
	.dtor = nv50_curs_dtor,
	.acquire = nv50_curs_acquire,
	.release = nv50_curs_release,
	.prepare = nv50_curs_prepare,
	.point = nv50_curs_point,
	.update = nv50_curs_update,
};

static int
nv50_curs_new(struct nouveau_drm *drm, struct nv50_head *head,
	      struct nv50_curs **pcurs)
{
	static const struct nvif_mclass curses[] = {
		{ GK104_DISP_CURSOR, 0 },
		{ GF110_DISP_CURSOR, 0 },
		{ GT214_DISP_CURSOR, 0 },
		{   G82_DISP_CURSOR, 0 },
		{  NV50_DISP_CURSOR, 0 },
		{}
	};
	struct nv50_disp_cursor_v0 args = {
		.head = head->base.index,
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct nv50_curs *curs;
	int cid, ret;

	cid = nvif_mclass(disp->disp, curses);
	if (cid < 0) {
		NV_ERROR(drm, "No supported cursor immediate class\n");
		return cid;
	}

	if (!(curs = *pcurs = kzalloc(sizeof(*curs), GFP_KERNEL)))
		return -ENOMEM;

	ret = nv50_wndw_ctor(&nv50_curs, drm->dev, DRM_PLANE_TYPE_CURSOR,
			     "curs", head->base.index, &disp->mast.base,
			     nv50_curs_format, ARRAY_SIZE(nv50_curs_format),
			     &curs->wndw);
	if (ret) {
		kfree(curs);
		return ret;
	}

	ret = nvif_object_init(disp->disp, 0, curses[cid].oclass, &args,
			       sizeof(args), &curs->chan);
	if (ret) {
		NV_ERROR(drm, "curs%04x allocation failed: %d\n",
			 curses[cid].oclass, ret);
		return ret;
	}

	return 0;
}

/******************************************************************************
 * Primary plane
 *****************************************************************************/
#define nv50_base(p) container_of((p), struct nv50_base, wndw)

struct nv50_base {
	struct nv50_wndw wndw;
	struct nv50_sync chan;
	int id;
};

static int
nv50_base_notify(struct nvif_notify *notify)
{
	return NVIF_NOTIFY_KEEP;
}

static void
nv50_base_lut(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nv50_base *base = nv50_base(wndw);
	u32 *push;
	if ((push = evo_wait(&base->chan, 2))) {
		evo_mthd(push, 0x00e0, 1);
		evo_data(push, asyw->lut.enable << 30);
		evo_kick(push, &base->chan);
	}
}

static void
nv50_base_image_clr(struct nv50_wndw *wndw)
{
	struct nv50_base *base = nv50_base(wndw);
	u32 *push;
	if ((push = evo_wait(&base->chan, 4))) {
		evo_mthd(push, 0x0084, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x00c0, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &base->chan);
	}
}

static void
nv50_base_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nv50_base *base = nv50_base(wndw);
	const s32 oclass = base->chan.base.base.user.oclass;
	u32 *push;
	if ((push = evo_wait(&base->chan, 10))) {
		evo_mthd(push, 0x0084, 1);
		evo_data(push, (asyw->image.mode << 8) |
			       (asyw->image.interval << 4));
		evo_mthd(push, 0x00c0, 1);
		evo_data(push, asyw->image.handle);
		if (oclass < G82_DISP_BASE_CHANNEL_DMA) {
			evo_mthd(push, 0x0800, 5);
			evo_data(push, asyw->image.offset >> 8);
			evo_data(push, 0x00000000);
			evo_data(push, (asyw->image.h << 16) | asyw->image.w);
			evo_data(push, (asyw->image.layout << 20) |
					asyw->image.pitch |
					asyw->image.block);
			evo_data(push, (asyw->image.kind << 16) |
				       (asyw->image.format << 8));
		} else
		if (oclass < GF110_DISP_BASE_CHANNEL_DMA) {
			evo_mthd(push, 0x0800, 5);
			evo_data(push, asyw->image.offset >> 8);
			evo_data(push, 0x00000000);
			evo_data(push, (asyw->image.h << 16) | asyw->image.w);
			evo_data(push, (asyw->image.layout << 20) |
					asyw->image.pitch |
					asyw->image.block);
			evo_data(push, asyw->image.format << 8);
		} else {
			evo_mthd(push, 0x0400, 5);
			evo_data(push, asyw->image.offset >> 8);
			evo_data(push, 0x00000000);
			evo_data(push, (asyw->image.h << 16) | asyw->image.w);
			evo_data(push, (asyw->image.layout << 24) |
					asyw->image.pitch |
					asyw->image.block);
			evo_data(push, asyw->image.format << 8);
		}
		evo_kick(push, &base->chan);
	}
}

static void
nv50_base_ntfy_clr(struct nv50_wndw *wndw)
{
	struct nv50_base *base = nv50_base(wndw);
	u32 *push;
	if ((push = evo_wait(&base->chan, 2))) {
		evo_mthd(push, 0x00a4, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &base->chan);
	}
}

static void
nv50_base_ntfy_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nv50_base *base = nv50_base(wndw);
	u32 *push;
	if ((push = evo_wait(&base->chan, 3))) {
		evo_mthd(push, 0x00a0, 2);
		evo_data(push, (asyw->ntfy.awaken << 30) | asyw->ntfy.offset);
		evo_data(push, asyw->ntfy.handle);
		evo_kick(push, &base->chan);
	}
}

static void
nv50_base_sema_clr(struct nv50_wndw *wndw)
{
	struct nv50_base *base = nv50_base(wndw);
	u32 *push;
	if ((push = evo_wait(&base->chan, 2))) {
		evo_mthd(push, 0x0094, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &base->chan);
	}
}

static void
nv50_base_sema_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nv50_base *base = nv50_base(wndw);
	u32 *push;
	if ((push = evo_wait(&base->chan, 5))) {
		evo_mthd(push, 0x0088, 4);
		evo_data(push, asyw->sema.offset);
		evo_data(push, asyw->sema.acquire);
		evo_data(push, asyw->sema.release);
		evo_data(push, asyw->sema.handle);
		evo_kick(push, &base->chan);
	}
}

static u32
nv50_base_update(struct nv50_wndw *wndw, u32 interlock)
{
	struct nv50_base *base = nv50_base(wndw);
	u32 *push;

	if (!(push = evo_wait(&base->chan, 2)))
		return 0;
	evo_mthd(push, 0x0080, 1);
	evo_data(push, interlock);
	evo_kick(push, &base->chan);

	if (base->chan.base.base.user.oclass < GF110_DISP_BASE_CHANNEL_DMA)
		return interlock ? 2 << (base->id * 8) : 0;
	return interlock ? 2 << (base->id * 4) : 0;
}

static int
nv50_base_ntfy_wait_begun(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nouveau_drm *drm = nouveau_drm(wndw->plane.dev);
	struct nv50_disp *disp = nv50_disp(wndw->plane.dev);
	if (nvif_msec(&drm->client.device, 2000ULL,
		u32 data = nouveau_bo_rd32(disp->sync, asyw->ntfy.offset / 4);
		if ((data & 0xc0000000) == 0x40000000)
			break;
		usleep_range(1, 2);
	) < 0)
		return -ETIMEDOUT;
	return 0;
}

static void
nv50_base_release(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		  struct nv50_head_atom *asyh)
{
	asyh->base.cpp = 0;
}

static int
nv50_base_acquire(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		  struct nv50_head_atom *asyh)
{
	const struct drm_framebuffer *fb = asyw->state.fb;
	int ret;

	if (!fb->format->depth)
		return -EINVAL;

	ret = drm_plane_helper_check_state(&asyw->state, &asyw->clip,
					   DRM_PLANE_HELPER_NO_SCALING,
					   DRM_PLANE_HELPER_NO_SCALING,
					   false, true);
	if (ret)
		return ret;

	asyh->base.depth = fb->format->depth;
	asyh->base.cpp = fb->format->cpp[0];
	asyh->base.x = asyw->state.src.x1 >> 16;
	asyh->base.y = asyw->state.src.y1 >> 16;
	asyh->base.w = asyw->state.fb->width;
	asyh->base.h = asyw->state.fb->height;

	switch (fb->format->format) {
	case DRM_FORMAT_C8         : asyw->image.format = 0x1e; break;
	case DRM_FORMAT_RGB565     : asyw->image.format = 0xe8; break;
	case DRM_FORMAT_XRGB1555   :
	case DRM_FORMAT_ARGB1555   : asyw->image.format = 0xe9; break;
	case DRM_FORMAT_XRGB8888   :
	case DRM_FORMAT_ARGB8888   : asyw->image.format = 0xcf; break;
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010: asyw->image.format = 0xd1; break;
	case DRM_FORMAT_XBGR8888   :
	case DRM_FORMAT_ABGR8888   : asyw->image.format = 0xd5; break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	asyw->lut.enable = 1;
	asyw->set.image = true;
	return 0;
}

static void *
nv50_base_dtor(struct nv50_wndw *wndw)
{
	struct nv50_disp *disp = nv50_disp(wndw->plane.dev);
	struct nv50_base *base = nv50_base(wndw);
	nv50_dmac_destroy(&base->chan.base, disp->disp);
	return base;
}

static const u32
nv50_base_format[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
};

static const struct nv50_wndw_func
nv50_base = {
	.dtor = nv50_base_dtor,
	.acquire = nv50_base_acquire,
	.release = nv50_base_release,
	.sema_set = nv50_base_sema_set,
	.sema_clr = nv50_base_sema_clr,
	.ntfy_set = nv50_base_ntfy_set,
	.ntfy_clr = nv50_base_ntfy_clr,
	.ntfy_wait_begun = nv50_base_ntfy_wait_begun,
	.image_set = nv50_base_image_set,
	.image_clr = nv50_base_image_clr,
	.lut = nv50_base_lut,
	.update = nv50_base_update,
};

static int
nv50_base_new(struct nouveau_drm *drm, struct nv50_head *head,
	      struct nv50_base **pbase)
{
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct nv50_base *base;
	int ret;

	if (!(base = *pbase = kzalloc(sizeof(*base), GFP_KERNEL)))
		return -ENOMEM;
	base->id = head->base.index;
	base->wndw.ntfy = EVO_FLIP_NTFY0(base->id);
	base->wndw.sema = EVO_FLIP_SEM0(base->id);
	base->wndw.data = 0x00000000;

	ret = nv50_wndw_ctor(&nv50_base, drm->dev, DRM_PLANE_TYPE_PRIMARY,
			     "base", base->id, &base->chan.base,
			     nv50_base_format, ARRAY_SIZE(nv50_base_format),
			     &base->wndw);
	if (ret) {
		kfree(base);
		return ret;
	}

	ret = nv50_base_create(&drm->client.device, disp->disp, base->id,
			       disp->sync->bo.offset, &base->chan);
	if (ret)
		return ret;

	return nvif_notify_init(&base->chan.base.base.user, nv50_base_notify,
				false,
				NV50_DISP_BASE_CHANNEL_DMA_V0_NTFY_UEVENT,
				&(struct nvif_notify_uevent_req) {},
				sizeof(struct nvif_notify_uevent_req),
				sizeof(struct nvif_notify_uevent_rep),
				&base->wndw.notify);
}

/******************************************************************************
 * Head
 *****************************************************************************/
static void
nv50_head_procamp(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x08a8 + (head->base.index * 0x400), 1);
		else
			evo_mthd(push, 0x0498 + (head->base.index * 0x300), 1);
		evo_data(push, (asyh->procamp.sat.sin << 20) |
			       (asyh->procamp.sat.cos << 8));
		evo_kick(push, core);
	}
}

static void
nv50_head_dither(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x08a0 + (head->base.index * 0x0400), 1);
		else
		if (core->base.user.oclass < GK104_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x0490 + (head->base.index * 0x0300), 1);
		else
			evo_mthd(push, 0x04a0 + (head->base.index * 0x0300), 1);
		evo_data(push, (asyh->dither.mode << 3) |
			       (asyh->dither.bits << 1) |
			        asyh->dither.enable);
		evo_kick(push, core);
	}
}

static void
nv50_head_ovly(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	u32 bounds = 0;
	u32 *push;

	if (asyh->base.cpp) {
		switch (asyh->base.cpp) {
		case 8: bounds |= 0x00000500; break;
		case 4: bounds |= 0x00000300; break;
		case 2: bounds |= 0x00000100; break;
		default:
			WARN_ON(1);
			break;
		}
		bounds |= 0x00000001;
	}

	if ((push = evo_wait(core, 2))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x0904 + head->base.index * 0x400, 1);
		else
			evo_mthd(push, 0x04d4 + head->base.index * 0x300, 1);
		evo_data(push, bounds);
		evo_kick(push, core);
	}
}

static void
nv50_head_base(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	u32 bounds = 0;
	u32 *push;

	if (asyh->base.cpp) {
		switch (asyh->base.cpp) {
		case 8: bounds |= 0x00000500; break;
		case 4: bounds |= 0x00000300; break;
		case 2: bounds |= 0x00000100; break;
		case 1: bounds |= 0x00000000; break;
		default:
			WARN_ON(1);
			break;
		}
		bounds |= 0x00000001;
	}

	if ((push = evo_wait(core, 2))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x0900 + head->base.index * 0x400, 1);
		else
			evo_mthd(push, 0x04d0 + head->base.index * 0x300, 1);
		evo_data(push, bounds);
		evo_kick(push, core);
	}
}

static void
nv50_head_curs_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	u32 *push;
	if ((push = evo_wait(core, 4))) {
		if (core->base.user.oclass < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + head->base.index * 0x400, 1);
			evo_data(push, 0x05000000);
		} else
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + head->base.index * 0x400, 1);
			evo_data(push, 0x05000000);
			evo_mthd(push, 0x089c + head->base.index * 0x400, 1);
			evo_data(push, 0x00000000);
		} else {
			evo_mthd(push, 0x0480 + head->base.index * 0x300, 1);
			evo_data(push, 0x05000000);
			evo_mthd(push, 0x048c + head->base.index * 0x300, 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, core);
	}
}

static void
nv50_head_curs_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	u32 *push;
	if ((push = evo_wait(core, 5))) {
		if (core->base.user.oclass < G82_DISP_BASE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + head->base.index * 0x400, 2);
			evo_data(push, 0x80000000 | (asyh->curs.layout << 26) |
						    (asyh->curs.format << 24));
			evo_data(push, asyh->curs.offset >> 8);
		} else
		if (core->base.user.oclass < GF110_DISP_BASE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + head->base.index * 0x400, 2);
			evo_data(push, 0x80000000 | (asyh->curs.layout << 26) |
						    (asyh->curs.format << 24));
			evo_data(push, asyh->curs.offset >> 8);
			evo_mthd(push, 0x089c + head->base.index * 0x400, 1);
			evo_data(push, asyh->curs.handle);
		} else {
			evo_mthd(push, 0x0480 + head->base.index * 0x300, 2);
			evo_data(push, 0x80000000 | (asyh->curs.layout << 26) |
						    (asyh->curs.format << 24));
			evo_data(push, asyh->curs.offset >> 8);
			evo_mthd(push, 0x048c + head->base.index * 0x300, 1);
			evo_data(push, asyh->curs.handle);
		}
		evo_kick(push, core);
	}
}

static void
nv50_head_core_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x0874 + head->base.index * 0x400, 1);
		else
			evo_mthd(push, 0x0474 + head->base.index * 0x300, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, core);
	}
}

static void
nv50_head_core_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	u32 *push;
	if ((push = evo_wait(core, 9))) {
		if (core->base.user.oclass < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0860 + head->base.index * 0x400, 1);
			evo_data(push, asyh->core.offset >> 8);
			evo_mthd(push, 0x0868 + head->base.index * 0x400, 4);
			evo_data(push, (asyh->core.h << 16) | asyh->core.w);
			evo_data(push, asyh->core.layout << 20 |
				       (asyh->core.pitch >> 8) << 8 |
				       asyh->core.block);
			evo_data(push, asyh->core.kind << 16 |
				       asyh->core.format << 8);
			evo_data(push, asyh->core.handle);
			evo_mthd(push, 0x08c0 + head->base.index * 0x400, 1);
			evo_data(push, (asyh->core.y << 16) | asyh->core.x);
			/* EVO will complain with INVALID_STATE if we have an
			 * active cursor and (re)specify HeadSetContextDmaIso
			 * without also updating HeadSetOffsetCursor.
			 */
			asyh->set.curs = asyh->curs.visible;
		} else
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0860 + head->base.index * 0x400, 1);
			evo_data(push, asyh->core.offset >> 8);
			evo_mthd(push, 0x0868 + head->base.index * 0x400, 4);
			evo_data(push, (asyh->core.h << 16) | asyh->core.w);
			evo_data(push, asyh->core.layout << 20 |
				       (asyh->core.pitch >> 8) << 8 |
				       asyh->core.block);
			evo_data(push, asyh->core.format << 8);
			evo_data(push, asyh->core.handle);
			evo_mthd(push, 0x08c0 + head->base.index * 0x400, 1);
			evo_data(push, (asyh->core.y << 16) | asyh->core.x);
		} else {
			evo_mthd(push, 0x0460 + head->base.index * 0x300, 1);
			evo_data(push, asyh->core.offset >> 8);
			evo_mthd(push, 0x0468 + head->base.index * 0x300, 4);
			evo_data(push, (asyh->core.h << 16) | asyh->core.w);
			evo_data(push, asyh->core.layout << 24 |
				       (asyh->core.pitch >> 8) << 8 |
				       asyh->core.block);
			evo_data(push, asyh->core.format << 8);
			evo_data(push, asyh->core.handle);
			evo_mthd(push, 0x04b0 + head->base.index * 0x300, 1);
			evo_data(push, (asyh->core.y << 16) | asyh->core.x);
		}
		evo_kick(push, core);
	}
}

static void
nv50_head_lut_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	u32 *push;
	if ((push = evo_wait(core, 4))) {
		if (core->base.user.oclass < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0840 + (head->base.index * 0x400), 1);
			evo_data(push, 0x40000000);
		} else
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0840 + (head->base.index * 0x400), 1);
			evo_data(push, 0x40000000);
			evo_mthd(push, 0x085c + (head->base.index * 0x400), 1);
			evo_data(push, 0x00000000);
		} else {
			evo_mthd(push, 0x0440 + (head->base.index * 0x300), 1);
			evo_data(push, 0x03000000);
			evo_mthd(push, 0x045c + (head->base.index * 0x300), 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, core);
	}
}

static void
nv50_head_lut_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	u32 *push;
	if ((push = evo_wait(core, 7))) {
		if (core->base.user.oclass < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0840 + (head->base.index * 0x400), 2);
			evo_data(push, 0xc0000000);
			evo_data(push, asyh->lut.offset >> 8);
		} else
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0840 + (head->base.index * 0x400), 2);
			evo_data(push, 0xc0000000);
			evo_data(push, asyh->lut.offset >> 8);
			evo_mthd(push, 0x085c + (head->base.index * 0x400), 1);
			evo_data(push, asyh->lut.handle);
		} else {
			evo_mthd(push, 0x0440 + (head->base.index * 0x300), 4);
			evo_data(push, 0x83000000);
			evo_data(push, asyh->lut.offset >> 8);
			evo_data(push, 0x00000000);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x045c + (head->base.index * 0x300), 1);
			evo_data(push, asyh->lut.handle);
		}
		evo_kick(push, core);
	}
}

static void
nv50_head_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	struct nv50_head_mode *m = &asyh->mode;
	u32 *push;
	if ((push = evo_wait(core, 14))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0804 + (head->base.index * 0x400), 2);
			evo_data(push, 0x00800000 | m->clock);
			evo_data(push, m->interlace ? 0x00000002 : 0x00000000);
			evo_mthd(push, 0x0810 + (head->base.index * 0x400), 7);
			evo_data(push, 0x00000000);
			evo_data(push, (m->v.active  << 16) | m->h.active );
			evo_data(push, (m->v.synce   << 16) | m->h.synce  );
			evo_data(push, (m->v.blanke  << 16) | m->h.blanke );
			evo_data(push, (m->v.blanks  << 16) | m->h.blanks );
			evo_data(push, (m->v.blank2e << 16) | m->v.blank2s);
			evo_data(push, asyh->mode.v.blankus);
			evo_mthd(push, 0x082c + (head->base.index * 0x400), 1);
			evo_data(push, 0x00000000);
		} else {
			evo_mthd(push, 0x0410 + (head->base.index * 0x300), 6);
			evo_data(push, 0x00000000);
			evo_data(push, (m->v.active  << 16) | m->h.active );
			evo_data(push, (m->v.synce   << 16) | m->h.synce  );
			evo_data(push, (m->v.blanke  << 16) | m->h.blanke );
			evo_data(push, (m->v.blanks  << 16) | m->h.blanks );
			evo_data(push, (m->v.blank2e << 16) | m->v.blank2s);
			evo_mthd(push, 0x042c + (head->base.index * 0x300), 2);
			evo_data(push, 0x00000000); /* ??? */
			evo_data(push, 0xffffff00);
			evo_mthd(push, 0x0450 + (head->base.index * 0x300), 3);
			evo_data(push, m->clock * 1000);
			evo_data(push, 0x00200000); /* ??? */
			evo_data(push, m->clock * 1000);
		}
		evo_kick(push, core);
	}
}

static void
nv50_head_view(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->mast.base;
	u32 *push;
	if ((push = evo_wait(core, 10))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x08a4 + (head->base.index * 0x400), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x08c8 + (head->base.index * 0x400), 1);
			evo_data(push, (asyh->view.iH << 16) | asyh->view.iW);
			evo_mthd(push, 0x08d8 + (head->base.index * 0x400), 2);
			evo_data(push, (asyh->view.oH << 16) | asyh->view.oW);
			evo_data(push, (asyh->view.oH << 16) | asyh->view.oW);
		} else {
			evo_mthd(push, 0x0494 + (head->base.index * 0x300), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x04b8 + (head->base.index * 0x300), 1);
			evo_data(push, (asyh->view.iH << 16) | asyh->view.iW);
			evo_mthd(push, 0x04c0 + (head->base.index * 0x300), 3);
			evo_data(push, (asyh->view.oH << 16) | asyh->view.oW);
			evo_data(push, (asyh->view.oH << 16) | asyh->view.oW);
			evo_data(push, (asyh->view.oH << 16) | asyh->view.oW);
		}
		evo_kick(push, core);
	}
}

static void
nv50_head_flush_clr(struct nv50_head *head, struct nv50_head_atom *asyh, bool y)
{
	if (asyh->clr.core && (!asyh->set.core || y))
		nv50_head_lut_clr(head);
	if (asyh->clr.core && (!asyh->set.core || y))
		nv50_head_core_clr(head);
	if (asyh->clr.curs && (!asyh->set.curs || y))
		nv50_head_curs_clr(head);
}

static void
nv50_head_flush_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	if (asyh->set.view   ) nv50_head_view    (head, asyh);
	if (asyh->set.mode   ) nv50_head_mode    (head, asyh);
	if (asyh->set.core   ) nv50_head_lut_set (head, asyh);
	if (asyh->set.core   ) nv50_head_core_set(head, asyh);
	if (asyh->set.curs   ) nv50_head_curs_set(head, asyh);
	if (asyh->set.base   ) nv50_head_base    (head, asyh);
	if (asyh->set.ovly   ) nv50_head_ovly    (head, asyh);
	if (asyh->set.dither ) nv50_head_dither  (head, asyh);
	if (asyh->set.procamp) nv50_head_procamp (head, asyh);
}

static void
nv50_head_atomic_check_procamp(struct nv50_head_atom *armh,
			       struct nv50_head_atom *asyh,
			       struct nouveau_conn_atom *asyc)
{
	const int vib = asyc->procamp.color_vibrance - 100;
	const int hue = asyc->procamp.vibrant_hue - 90;
	const int adj = (vib > 0) ? 50 : 0;
	asyh->procamp.sat.cos = ((vib * 2047 + adj) / 100) & 0xfff;
	asyh->procamp.sat.sin = ((hue * 2047) / 100) & 0xfff;
	asyh->set.procamp = true;
}

static void
nv50_head_atomic_check_dither(struct nv50_head_atom *armh,
			      struct nv50_head_atom *asyh,
			      struct nouveau_conn_atom *asyc)
{
	struct drm_connector *connector = asyc->state.connector;
	u32 mode = 0x00;

	if (asyc->dither.mode == DITHERING_MODE_AUTO) {
		if (asyh->base.depth > connector->display_info.bpc * 3)
			mode = DITHERING_MODE_DYNAMIC2X2;
	} else {
		mode = asyc->dither.mode;
	}

	if (asyc->dither.depth == DITHERING_DEPTH_AUTO) {
		if (connector->display_info.bpc >= 8)
			mode |= DITHERING_DEPTH_8BPC;
	} else {
		mode |= asyc->dither.depth;
	}

	asyh->dither.enable = mode;
	asyh->dither.bits = mode >> 1;
	asyh->dither.mode = mode >> 3;
	asyh->set.dither = true;
}

static void
nv50_head_atomic_check_view(struct nv50_head_atom *armh,
			    struct nv50_head_atom *asyh,
			    struct nouveau_conn_atom *asyc)
{
	struct drm_connector *connector = asyc->state.connector;
	struct drm_display_mode *omode = &asyh->state.adjusted_mode;
	struct drm_display_mode *umode = &asyh->state.mode;
	int mode = asyc->scaler.mode;
	struct edid *edid;

	if (connector->edid_blob_ptr)
		edid = (struct edid *)connector->edid_blob_ptr->data;
	else
		edid = NULL;

	if (!asyc->scaler.full) {
		if (mode == DRM_MODE_SCALE_NONE)
			omode = umode;
	} else {
		/* Non-EDID LVDS/eDP mode. */
		mode = DRM_MODE_SCALE_FULLSCREEN;
	}

	asyh->view.iW = umode->hdisplay;
	asyh->view.iH = umode->vdisplay;
	asyh->view.oW = omode->hdisplay;
	asyh->view.oH = omode->vdisplay;
	if (omode->flags & DRM_MODE_FLAG_DBLSCAN)
		asyh->view.oH *= 2;

	/* Add overscan compensation if necessary, will keep the aspect
	 * ratio the same as the backend mode unless overridden by the
	 * user setting both hborder and vborder properties.
	 */
	if ((asyc->scaler.underscan.mode == UNDERSCAN_ON ||
	    (asyc->scaler.underscan.mode == UNDERSCAN_AUTO &&
	     drm_detect_hdmi_monitor(edid)))) {
		u32 bX = asyc->scaler.underscan.hborder;
		u32 bY = asyc->scaler.underscan.vborder;
		u32 r = (asyh->view.oH << 19) / asyh->view.oW;

		if (bX) {
			asyh->view.oW -= (bX * 2);
			if (bY) asyh->view.oH -= (bY * 2);
			else    asyh->view.oH  = ((asyh->view.oW * r) + (r / 2)) >> 19;
		} else {
			asyh->view.oW -= (asyh->view.oW >> 4) + 32;
			if (bY) asyh->view.oH -= (bY * 2);
			else    asyh->view.oH  = ((asyh->view.oW * r) + (r / 2)) >> 19;
		}
	}

	/* Handle CENTER/ASPECT scaling, taking into account the areas
	 * removed already for overscan compensation.
	 */
	switch (mode) {
	case DRM_MODE_SCALE_CENTER:
		asyh->view.oW = min((u16)umode->hdisplay, asyh->view.oW);
		asyh->view.oH = min((u16)umode->vdisplay, asyh->view.oH);
		/* fall-through */
	case DRM_MODE_SCALE_ASPECT:
		if (asyh->view.oH < asyh->view.oW) {
			u32 r = (asyh->view.iW << 19) / asyh->view.iH;
			asyh->view.oW = ((asyh->view.oH * r) + (r / 2)) >> 19;
		} else {
			u32 r = (asyh->view.iH << 19) / asyh->view.iW;
			asyh->view.oH = ((asyh->view.oW * r) + (r / 2)) >> 19;
		}
		break;
	default:
		break;
	}

	asyh->set.view = true;
}

static void
nv50_head_atomic_check_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct drm_display_mode *mode = &asyh->state.adjusted_mode;
	u32 ilace   = (mode->flags & DRM_MODE_FLAG_INTERLACE) ? 2 : 1;
	u32 vscan   = (mode->flags & DRM_MODE_FLAG_DBLSCAN) ? 2 : 1;
	u32 hbackp  =  mode->htotal - mode->hsync_end;
	u32 vbackp  = (mode->vtotal - mode->vsync_end) * vscan / ilace;
	u32 hfrontp =  mode->hsync_start - mode->hdisplay;
	u32 vfrontp = (mode->vsync_start - mode->vdisplay) * vscan / ilace;
	u32 blankus;
	struct nv50_head_mode *m = &asyh->mode;

	m->h.active = mode->htotal;
	m->h.synce  = mode->hsync_end - mode->hsync_start - 1;
	m->h.blanke = m->h.synce + hbackp;
	m->h.blanks = mode->htotal - hfrontp - 1;

	m->v.active = mode->vtotal * vscan / ilace;
	m->v.synce  = ((mode->vsync_end - mode->vsync_start) * vscan / ilace) - 1;
	m->v.blanke = m->v.synce + vbackp;
	m->v.blanks = m->v.active - vfrontp - 1;

	/*XXX: Safe underestimate, even "0" works */
	blankus = (m->v.active - mode->vdisplay - 2) * m->h.active;
	blankus *= 1000;
	blankus /= mode->clock;
	m->v.blankus = blankus;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		m->v.blank2e =  m->v.active + m->v.synce + vbackp;
		m->v.blank2s =  m->v.blank2e + (mode->vdisplay * vscan / ilace);
		m->v.active  = (m->v.active * 2) + 1;
		m->interlace = true;
	} else {
		m->v.blank2e = 0;
		m->v.blank2s = 1;
		m->interlace = false;
	}
	m->clock = mode->clock;

	drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
	asyh->set.mode = true;
}

static int
nv50_head_atomic_check(struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	struct nouveau_drm *drm = nouveau_drm(crtc->dev);
	struct nv50_disp *disp = nv50_disp(crtc->dev);
	struct nv50_head *head = nv50_head(crtc);
	struct nv50_head_atom *armh = nv50_head_atom(crtc->state);
	struct nv50_head_atom *asyh = nv50_head_atom(state);
	struct nouveau_conn_atom *asyc = NULL;
	struct drm_connector_state *conns;
	struct drm_connector *conn;
	int i;

	NV_ATOMIC(drm, "%s atomic_check %d\n", crtc->name, asyh->state.active);
	if (asyh->state.active) {
		for_each_connector_in_state(asyh->state.state, conn, conns, i) {
			if (conns->crtc == crtc) {
				asyc = nouveau_conn_atom(conns);
				break;
			}
		}

		if (armh->state.active) {
			if (asyc) {
				if (asyh->state.mode_changed)
					asyc->set.scaler = true;
				if (armh->base.depth != asyh->base.depth)
					asyc->set.dither = true;
			}
		} else {
			if (asyc)
				asyc->set.mask = ~0;
			asyh->set.mask = ~0;
		}

		if (asyh->state.mode_changed)
			nv50_head_atomic_check_mode(head, asyh);

		if (asyc) {
			if (asyc->set.scaler)
				nv50_head_atomic_check_view(armh, asyh, asyc);
			if (asyc->set.dither)
				nv50_head_atomic_check_dither(armh, asyh, asyc);
			if (asyc->set.procamp)
				nv50_head_atomic_check_procamp(armh, asyh, asyc);
		}

		if ((asyh->core.visible = (asyh->base.cpp != 0))) {
			asyh->core.x = asyh->base.x;
			asyh->core.y = asyh->base.y;
			asyh->core.w = asyh->base.w;
			asyh->core.h = asyh->base.h;
		} else
		if ((asyh->core.visible = asyh->curs.visible)) {
			/*XXX: We need to either find some way of having the
			 *     primary base layer appear black, while still
			 *     being able to display the other layers, or we
			 *     need to allocate a dummy black surface here.
			 */
			asyh->core.x = 0;
			asyh->core.y = 0;
			asyh->core.w = asyh->state.mode.hdisplay;
			asyh->core.h = asyh->state.mode.vdisplay;
		}
		asyh->core.handle = disp->mast.base.vram.handle;
		asyh->core.offset = 0;
		asyh->core.format = 0xcf;
		asyh->core.kind = 0;
		asyh->core.layout = 1;
		asyh->core.block = 0;
		asyh->core.pitch = ALIGN(asyh->core.w, 64) * 4;
		asyh->lut.handle = disp->mast.base.vram.handle;
		asyh->lut.offset = head->base.lut.nvbo->bo.offset;
		asyh->set.base = armh->base.cpp != asyh->base.cpp;
		asyh->set.ovly = armh->ovly.cpp != asyh->ovly.cpp;
	} else {
		asyh->core.visible = false;
		asyh->curs.visible = false;
		asyh->base.cpp = 0;
		asyh->ovly.cpp = 0;
	}

	if (!drm_atomic_crtc_needs_modeset(&asyh->state)) {
		if (asyh->core.visible) {
			if (memcmp(&armh->core, &asyh->core, sizeof(asyh->core)))
				asyh->set.core = true;
		} else
		if (armh->core.visible) {
			asyh->clr.core = true;
		}

		if (asyh->curs.visible) {
			if (memcmp(&armh->curs, &asyh->curs, sizeof(asyh->curs)))
				asyh->set.curs = true;
		} else
		if (armh->curs.visible) {
			asyh->clr.curs = true;
		}
	} else {
		asyh->clr.core = armh->core.visible;
		asyh->clr.curs = armh->curs.visible;
		asyh->set.core = asyh->core.visible;
		asyh->set.curs = asyh->curs.visible;
	}

	if (asyh->clr.mask || asyh->set.mask)
		nv50_atom(asyh->state.state)->lock_core = true;
	return 0;
}

static void
nv50_head_lut_load(struct drm_crtc *crtc)
{
	struct nv50_disp *disp = nv50_disp(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	void __iomem *lut = nvbo_kmap_obj_iovirtual(nv_crtc->lut.nvbo);
	int i;

	for (i = 0; i < 256; i++) {
		u16 r = nv_crtc->lut.r[i] >> 2;
		u16 g = nv_crtc->lut.g[i] >> 2;
		u16 b = nv_crtc->lut.b[i] >> 2;

		if (disp->disp->oclass < GF110_DISP) {
			writew(r + 0x0000, lut + (i * 0x08) + 0);
			writew(g + 0x0000, lut + (i * 0x08) + 2);
			writew(b + 0x0000, lut + (i * 0x08) + 4);
		} else {
			writew(r + 0x6000, lut + (i * 0x20) + 0);
			writew(g + 0x6000, lut + (i * 0x20) + 2);
			writew(b + 0x6000, lut + (i * 0x20) + 4);
		}
	}
}

static const struct drm_crtc_helper_funcs
nv50_head_help = {
	.load_lut = nv50_head_lut_load,
	.atomic_check = nv50_head_atomic_check,
};

static int
nv50_head_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
		    uint32_t size,
		    struct drm_modeset_acquire_ctx *ctx)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	u32 i;

	for (i = 0; i < size; i++) {
		nv_crtc->lut.r[i] = r[i];
		nv_crtc->lut.g[i] = g[i];
		nv_crtc->lut.b[i] = b[i];
	}

	nv50_head_lut_load(crtc);
	return 0;
}

static void
nv50_head_atomic_destroy_state(struct drm_crtc *crtc,
			       struct drm_crtc_state *state)
{
	struct nv50_head_atom *asyh = nv50_head_atom(state);
	__drm_atomic_helper_crtc_destroy_state(&asyh->state);
	kfree(asyh);
}

static struct drm_crtc_state *
nv50_head_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct nv50_head_atom *armh = nv50_head_atom(crtc->state);
	struct nv50_head_atom *asyh;
	if (!(asyh = kmalloc(sizeof(*asyh), GFP_KERNEL)))
		return NULL;
	__drm_atomic_helper_crtc_duplicate_state(crtc, &asyh->state);
	asyh->view = armh->view;
	asyh->mode = armh->mode;
	asyh->lut  = armh->lut;
	asyh->core = armh->core;
	asyh->curs = armh->curs;
	asyh->base = armh->base;
	asyh->ovly = armh->ovly;
	asyh->dither = armh->dither;
	asyh->procamp = armh->procamp;
	asyh->clr.mask = 0;
	asyh->set.mask = 0;
	return &asyh->state;
}

static void
__drm_atomic_helper_crtc_reset(struct drm_crtc *crtc,
			       struct drm_crtc_state *state)
{
	if (crtc->state)
		crtc->funcs->atomic_destroy_state(crtc, crtc->state);
	crtc->state = state;
	crtc->state->crtc = crtc;
}

static void
nv50_head_reset(struct drm_crtc *crtc)
{
	struct nv50_head_atom *asyh;

	if (WARN_ON(!(asyh = kzalloc(sizeof(*asyh), GFP_KERNEL))))
		return;

	__drm_atomic_helper_crtc_reset(crtc, &asyh->state);
}

static void
nv50_head_destroy(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv50_disp *disp = nv50_disp(crtc->dev);
	struct nv50_head *head = nv50_head(crtc);

	nv50_dmac_destroy(&head->ovly.base, disp->disp);
	nv50_pioc_destroy(&head->oimm.base);

	nouveau_bo_unmap(nv_crtc->lut.nvbo);
	if (nv_crtc->lut.nvbo)
		nouveau_bo_unpin(nv_crtc->lut.nvbo);
	nouveau_bo_ref(NULL, &nv_crtc->lut.nvbo);

	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static const struct drm_crtc_funcs
nv50_head_func = {
	.reset = nv50_head_reset,
	.gamma_set = nv50_head_gamma_set,
	.destroy = nv50_head_destroy,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.set_property = drm_atomic_helper_crtc_set_property,
	.atomic_duplicate_state = nv50_head_atomic_duplicate_state,
	.atomic_destroy_state = nv50_head_atomic_destroy_state,
};

static int
nv50_head_create(struct drm_device *dev, int index)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvif_device *device = &drm->client.device;
	struct nv50_disp *disp = nv50_disp(dev);
	struct nv50_head *head;
	struct nv50_base *base;
	struct nv50_curs *curs;
	struct drm_crtc *crtc;
	int ret, i;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (!head)
		return -ENOMEM;

	head->base.index = index;
	for (i = 0; i < 256; i++) {
		head->base.lut.r[i] = i << 8;
		head->base.lut.g[i] = i << 8;
		head->base.lut.b[i] = i << 8;
	}

	ret = nv50_base_new(drm, head, &base);
	if (ret == 0)
		ret = nv50_curs_new(drm, head, &curs);
	if (ret) {
		kfree(head);
		return ret;
	}

	crtc = &head->base.base;
	drm_crtc_init_with_planes(dev, crtc, &base->wndw.plane,
				  &curs->wndw.plane, &nv50_head_func,
				  "head-%d", head->base.index);
	drm_crtc_helper_add(crtc, &nv50_head_help);
	drm_mode_crtc_set_gamma_size(crtc, 256);

	ret = nouveau_bo_new(&drm->client, 8192, 0x100, TTM_PL_FLAG_VRAM,
			     0, 0x0000, NULL, NULL, &head->base.lut.nvbo);
	if (!ret) {
		ret = nouveau_bo_pin(head->base.lut.nvbo, TTM_PL_FLAG_VRAM, true);
		if (!ret) {
			ret = nouveau_bo_map(head->base.lut.nvbo);
			if (ret)
				nouveau_bo_unpin(head->base.lut.nvbo);
		}
		if (ret)
			nouveau_bo_ref(NULL, &head->base.lut.nvbo);
	}

	if (ret)
		goto out;

	/* allocate overlay resources */
	ret = nv50_oimm_create(device, disp->disp, index, &head->oimm);
	if (ret)
		goto out;

	ret = nv50_ovly_create(device, disp->disp, index, disp->sync->bo.offset,
			       &head->ovly);
	if (ret)
		goto out;

out:
	if (ret)
		nv50_head_destroy(crtc);
	return ret;
}

/******************************************************************************
 * Output path helpers
 *****************************************************************************/
static int
nv50_outp_atomic_check_view(struct drm_encoder *encoder,
			    struct drm_crtc_state *crtc_state,
			    struct drm_connector_state *conn_state,
			    struct drm_display_mode *native_mode)
{
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	struct drm_display_mode *mode = &crtc_state->mode;
	struct drm_connector *connector = conn_state->connector;
	struct nouveau_conn_atom *asyc = nouveau_conn_atom(conn_state);
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);

	NV_ATOMIC(drm, "%s atomic_check\n", encoder->name);
	asyc->scaler.full = false;
	if (!native_mode)
		return 0;

	if (asyc->scaler.mode == DRM_MODE_SCALE_NONE) {
		switch (connector->connector_type) {
		case DRM_MODE_CONNECTOR_LVDS:
		case DRM_MODE_CONNECTOR_eDP:
			/* Force use of scaler for non-EDID modes. */
			if (adjusted_mode->type & DRM_MODE_TYPE_DRIVER)
				break;
			mode = native_mode;
			asyc->scaler.full = true;
			break;
		default:
			break;
		}
	} else {
		mode = native_mode;
	}

	if (!drm_mode_equal(adjusted_mode, mode)) {
		drm_mode_copy(adjusted_mode, mode);
		crtc_state->mode_changed = true;
	}

	return 0;
}

static int
nv50_outp_atomic_check(struct drm_encoder *encoder,
		       struct drm_crtc_state *crtc_state,
		       struct drm_connector_state *conn_state)
{
	struct nouveau_connector *nv_connector =
		nouveau_connector(conn_state->connector);
	return nv50_outp_atomic_check_view(encoder, crtc_state, conn_state,
					   nv_connector->native_mode);
}

/******************************************************************************
 * DAC
 *****************************************************************************/
static void
nv50_dac_dpms(struct drm_encoder *encoder, int mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nv50_disp *disp = nv50_disp(encoder->dev);
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_dac_pwr_v0 pwr;
	} args = {
		.base.version = 1,
		.base.method = NV50_DISP_MTHD_V1_DAC_PWR,
		.base.hasht  = nv_encoder->dcb->hasht,
		.base.hashm  = nv_encoder->dcb->hashm,
		.pwr.state = 1,
		.pwr.data  = 1,
		.pwr.vsync = (mode != DRM_MODE_DPMS_SUSPEND &&
			      mode != DRM_MODE_DPMS_OFF),
		.pwr.hsync = (mode != DRM_MODE_DPMS_STANDBY &&
			      mode != DRM_MODE_DPMS_OFF),
	};

	nvif_mthd(disp->disp, 0, &args, sizeof(args));
}

static void
nv50_dac_disable(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nv50_mast *mast = nv50_mast(encoder->dev);
	const int or = nv_encoder->or;
	u32 *push;

	if (nv_encoder->crtc) {
		push = evo_wait(mast, 4);
		if (push) {
			if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
				evo_mthd(push, 0x0400 + (or * 0x080), 1);
				evo_data(push, 0x00000000);
			} else {
				evo_mthd(push, 0x0180 + (or * 0x020), 1);
				evo_data(push, 0x00000000);
			}
			evo_kick(push, mast);
		}
	}

	nv_encoder->crtc = NULL;
}

static void
nv50_dac_enable(struct drm_encoder *encoder)
{
	struct nv50_mast *mast = nv50_mast(encoder->dev);
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct drm_display_mode *mode = &nv_crtc->base.state->adjusted_mode;
	u32 *push;

	push = evo_wait(mast, 8);
	if (push) {
		if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
			u32 syncs = 0x00000000;

			if (mode->flags & DRM_MODE_FLAG_NHSYNC)
				syncs |= 0x00000001;
			if (mode->flags & DRM_MODE_FLAG_NVSYNC)
				syncs |= 0x00000002;

			evo_mthd(push, 0x0400 + (nv_encoder->or * 0x080), 2);
			evo_data(push, 1 << nv_crtc->index);
			evo_data(push, syncs);
		} else {
			u32 magic = 0x31ec6000 | (nv_crtc->index << 25);
			u32 syncs = 0x00000001;

			if (mode->flags & DRM_MODE_FLAG_NHSYNC)
				syncs |= 0x00000008;
			if (mode->flags & DRM_MODE_FLAG_NVSYNC)
				syncs |= 0x00000010;

			if (mode->flags & DRM_MODE_FLAG_INTERLACE)
				magic |= 0x00000001;

			evo_mthd(push, 0x0404 + (nv_crtc->index * 0x300), 2);
			evo_data(push, syncs);
			evo_data(push, magic);
			evo_mthd(push, 0x0180 + (nv_encoder->or * 0x020), 1);
			evo_data(push, 1 << nv_crtc->index);
		}

		evo_kick(push, mast);
	}

	nv_encoder->crtc = encoder->crtc;
}

static enum drm_connector_status
nv50_dac_detect(struct drm_encoder *encoder, struct drm_connector *connector)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nv50_disp *disp = nv50_disp(encoder->dev);
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_dac_load_v0 load;
	} args = {
		.base.version = 1,
		.base.method = NV50_DISP_MTHD_V1_DAC_LOAD,
		.base.hasht  = nv_encoder->dcb->hasht,
		.base.hashm  = nv_encoder->dcb->hashm,
	};
	int ret;

	args.load.data = nouveau_drm(encoder->dev)->vbios.dactestval;
	if (args.load.data == 0)
		args.load.data = 340;

	ret = nvif_mthd(disp->disp, 0, &args, sizeof(args));
	if (ret || !args.load.load)
		return connector_status_disconnected;

	return connector_status_connected;
}

static const struct drm_encoder_helper_funcs
nv50_dac_help = {
	.dpms = nv50_dac_dpms,
	.atomic_check = nv50_outp_atomic_check,
	.enable = nv50_dac_enable,
	.disable = nv50_dac_disable,
	.detect = nv50_dac_detect
};

static void
nv50_dac_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs
nv50_dac_func = {
	.destroy = nv50_dac_destroy,
};

static int
nv50_dac_create(struct drm_connector *connector, struct dcb_output *dcbe)
{
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nvkm_i2c *i2c = nvxx_i2c(&drm->client.device);
	struct nvkm_i2c_bus *bus;
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;
	int type = DRM_MODE_ENCODER_DAC;

	nv_encoder = kzalloc(sizeof(*nv_encoder), GFP_KERNEL);
	if (!nv_encoder)
		return -ENOMEM;
	nv_encoder->dcb = dcbe;
	nv_encoder->or = ffs(dcbe->or) - 1;

	bus = nvkm_i2c_bus_find(i2c, dcbe->i2c_index);
	if (bus)
		nv_encoder->i2c = &bus->i2c;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(connector->dev, encoder, &nv50_dac_func, type,
			 "dac-%04x-%04x", dcbe->hasht, dcbe->hashm);
	drm_encoder_helper_add(encoder, &nv50_dac_help);

	drm_mode_connector_attach_encoder(connector, encoder);
	return 0;
}

/******************************************************************************
 * Audio
 *****************************************************************************/
static void
nv50_audio_disable(struct drm_encoder *encoder, struct nouveau_crtc *nv_crtc)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nv50_disp *disp = nv50_disp(encoder->dev);
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_sor_hda_eld_v0 eld;
	} args = {
		.base.version = 1,
		.base.method  = NV50_DISP_MTHD_V1_SOR_HDA_ELD,
		.base.hasht   = nv_encoder->dcb->hasht,
		.base.hashm   = (0xf0ff & nv_encoder->dcb->hashm) |
				(0x0100 << nv_crtc->index),
	};

	nvif_mthd(disp->disp, 0, &args, sizeof(args));
}

static void
nv50_audio_enable(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector;
	struct nv50_disp *disp = nv50_disp(encoder->dev);
	struct __packed {
		struct {
			struct nv50_disp_mthd_v1 mthd;
			struct nv50_disp_sor_hda_eld_v0 eld;
		} base;
		u8 data[sizeof(nv_connector->base.eld)];
	} args = {
		.base.mthd.version = 1,
		.base.mthd.method  = NV50_DISP_MTHD_V1_SOR_HDA_ELD,
		.base.mthd.hasht   = nv_encoder->dcb->hasht,
		.base.mthd.hashm   = (0xf0ff & nv_encoder->dcb->hashm) |
				     (0x0100 << nv_crtc->index),
	};

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!drm_detect_monitor_audio(nv_connector->edid))
		return;

	drm_edid_to_eld(&nv_connector->base, nv_connector->edid);
	memcpy(args.data, nv_connector->base.eld, sizeof(args.data));

	nvif_mthd(disp->disp, 0, &args,
		  sizeof(args.base) + drm_eld_size(args.data));
}

/******************************************************************************
 * HDMI
 *****************************************************************************/
static void
nv50_hdmi_disable(struct drm_encoder *encoder, struct nouveau_crtc *nv_crtc)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nv50_disp *disp = nv50_disp(encoder->dev);
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_sor_hdmi_pwr_v0 pwr;
	} args = {
		.base.version = 1,
		.base.method = NV50_DISP_MTHD_V1_SOR_HDMI_PWR,
		.base.hasht  = nv_encoder->dcb->hasht,
		.base.hashm  = (0xf0ff & nv_encoder->dcb->hashm) |
			       (0x0100 << nv_crtc->index),
	};

	nvif_mthd(disp->disp, 0, &args, sizeof(args));
}

static void
nv50_hdmi_enable(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nv50_disp *disp = nv50_disp(encoder->dev);
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_sor_hdmi_pwr_v0 pwr;
	} args = {
		.base.version = 1,
		.base.method = NV50_DISP_MTHD_V1_SOR_HDMI_PWR,
		.base.hasht  = nv_encoder->dcb->hasht,
		.base.hashm  = (0xf0ff & nv_encoder->dcb->hashm) |
			       (0x0100 << nv_crtc->index),
		.pwr.state = 1,
		.pwr.rekey = 56, /* binary driver, and tegra, constant */
	};
	struct nouveau_connector *nv_connector;
	u32 max_ac_packet;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!drm_detect_hdmi_monitor(nv_connector->edid))
		return;

	max_ac_packet  = mode->htotal - mode->hdisplay;
	max_ac_packet -= args.pwr.rekey;
	max_ac_packet -= 18; /* constant from tegra */
	args.pwr.max_ac_packet = max_ac_packet / 32;

	nvif_mthd(disp->disp, 0, &args, sizeof(args));
	nv50_audio_enable(encoder, mode);
}

/******************************************************************************
 * MST
 *****************************************************************************/
#define nv50_mstm(p) container_of((p), struct nv50_mstm, mgr)
#define nv50_mstc(p) container_of((p), struct nv50_mstc, connector)
#define nv50_msto(p) container_of((p), struct nv50_msto, encoder)

struct nv50_mstm {
	struct nouveau_encoder *outp;

	struct drm_dp_mst_topology_mgr mgr;
	struct nv50_msto *msto[4];

	bool modified;
};

struct nv50_mstc {
	struct nv50_mstm *mstm;
	struct drm_dp_mst_port *port;
	struct drm_connector connector;

	struct drm_display_mode *native;
	struct edid *edid;

	int pbn;
};

struct nv50_msto {
	struct drm_encoder encoder;

	struct nv50_head *head;
	struct nv50_mstc *mstc;
	bool disabled;
};

static struct drm_dp_payload *
nv50_msto_payload(struct nv50_msto *msto)
{
	struct nouveau_drm *drm = nouveau_drm(msto->encoder.dev);
	struct nv50_mstc *mstc = msto->mstc;
	struct nv50_mstm *mstm = mstc->mstm;
	int vcpi = mstc->port->vcpi.vcpi, i;

	NV_ATOMIC(drm, "%s: vcpi %d\n", msto->encoder.name, vcpi);
	for (i = 0; i < mstm->mgr.max_payloads; i++) {
		struct drm_dp_payload *payload = &mstm->mgr.payloads[i];
		NV_ATOMIC(drm, "%s: %d: vcpi %d start 0x%02x slots 0x%02x\n",
			  mstm->outp->base.base.name, i, payload->vcpi,
			  payload->start_slot, payload->num_slots);
	}

	for (i = 0; i < mstm->mgr.max_payloads; i++) {
		struct drm_dp_payload *payload = &mstm->mgr.payloads[i];
		if (payload->vcpi == vcpi)
			return payload;
	}

	return NULL;
}

static void
nv50_msto_cleanup(struct nv50_msto *msto)
{
	struct nouveau_drm *drm = nouveau_drm(msto->encoder.dev);
	struct nv50_mstc *mstc = msto->mstc;
	struct nv50_mstm *mstm = mstc->mstm;

	NV_ATOMIC(drm, "%s: msto cleanup\n", msto->encoder.name);
	if (mstc->port && mstc->port->vcpi.vcpi > 0 && !nv50_msto_payload(msto))
		drm_dp_mst_deallocate_vcpi(&mstm->mgr, mstc->port);
	if (msto->disabled) {
		msto->mstc = NULL;
		msto->head = NULL;
		msto->disabled = false;
	}
}

static void
nv50_msto_prepare(struct nv50_msto *msto)
{
	struct nouveau_drm *drm = nouveau_drm(msto->encoder.dev);
	struct nv50_mstc *mstc = msto->mstc;
	struct nv50_mstm *mstm = mstc->mstm;
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_sor_dp_mst_vcpi_v0 vcpi;
	} args = {
		.base.version = 1,
		.base.method = NV50_DISP_MTHD_V1_SOR_DP_MST_VCPI,
		.base.hasht  = mstm->outp->dcb->hasht,
		.base.hashm  = (0xf0ff & mstm->outp->dcb->hashm) |
			       (0x0100 << msto->head->base.index),
	};

	NV_ATOMIC(drm, "%s: msto prepare\n", msto->encoder.name);
	if (mstc->port && mstc->port->vcpi.vcpi > 0) {
		struct drm_dp_payload *payload = nv50_msto_payload(msto);
		if (payload) {
			args.vcpi.start_slot = payload->start_slot;
			args.vcpi.num_slots = payload->num_slots;
			args.vcpi.pbn = mstc->port->vcpi.pbn;
			args.vcpi.aligned_pbn = mstc->port->vcpi.aligned_pbn;
		}
	}

	NV_ATOMIC(drm, "%s: %s: %02x %02x %04x %04x\n",
		  msto->encoder.name, msto->head->base.base.name,
		  args.vcpi.start_slot, args.vcpi.num_slots,
		  args.vcpi.pbn, args.vcpi.aligned_pbn);
	nvif_mthd(&drm->display->disp, 0, &args, sizeof(args));
}

static int
nv50_msto_atomic_check(struct drm_encoder *encoder,
		       struct drm_crtc_state *crtc_state,
		       struct drm_connector_state *conn_state)
{
	struct nv50_mstc *mstc = nv50_mstc(conn_state->connector);
	struct nv50_mstm *mstm = mstc->mstm;
	int bpp = conn_state->connector->display_info.bpc * 3;
	int slots;

	mstc->pbn = drm_dp_calc_pbn_mode(crtc_state->adjusted_mode.clock, bpp);

	slots = drm_dp_find_vcpi_slots(&mstm->mgr, mstc->pbn);
	if (slots < 0)
		return slots;

	return nv50_outp_atomic_check_view(encoder, crtc_state, conn_state,
					   mstc->native);
}

static void
nv50_msto_enable(struct drm_encoder *encoder)
{
	struct nv50_head *head = nv50_head(encoder->crtc);
	struct nv50_msto *msto = nv50_msto(encoder);
	struct nv50_mstc *mstc = NULL;
	struct nv50_mstm *mstm = NULL;
	struct drm_connector *connector;
	u8 proto, depth;
	int slots;
	bool r;

	drm_for_each_connector(connector, encoder->dev) {
		if (connector->state->best_encoder == &msto->encoder) {
			mstc = nv50_mstc(connector);
			mstm = mstc->mstm;
			break;
		}
	}

	if (WARN_ON(!mstc))
		return;

	slots = drm_dp_find_vcpi_slots(&mstm->mgr, mstc->pbn);
	r = drm_dp_mst_allocate_vcpi(&mstm->mgr, mstc->port, mstc->pbn, slots);
	WARN_ON(!r);

	if (mstm->outp->dcb->sorconf.link & 1)
		proto = 0x8;
	else
		proto = 0x9;

	switch (mstc->connector.display_info.bpc) {
	case  6: depth = 0x2; break;
	case  8: depth = 0x5; break;
	case 10:
	default: depth = 0x6; break;
	}

	mstm->outp->update(mstm->outp, head->base.index,
			   &head->base.base.state->adjusted_mode, proto, depth);

	msto->head = head;
	msto->mstc = mstc;
	mstm->modified = true;
}

static void
nv50_msto_disable(struct drm_encoder *encoder)
{
	struct nv50_msto *msto = nv50_msto(encoder);
	struct nv50_mstc *mstc = msto->mstc;
	struct nv50_mstm *mstm = mstc->mstm;

	if (mstc->port)
		drm_dp_mst_reset_vcpi_slots(&mstm->mgr, mstc->port);

	mstm->outp->update(mstm->outp, msto->head->base.index, NULL, 0, 0);
	mstm->modified = true;
	msto->disabled = true;
}

static const struct drm_encoder_helper_funcs
nv50_msto_help = {
	.disable = nv50_msto_disable,
	.enable = nv50_msto_enable,
	.atomic_check = nv50_msto_atomic_check,
};

static void
nv50_msto_destroy(struct drm_encoder *encoder)
{
	struct nv50_msto *msto = nv50_msto(encoder);
	drm_encoder_cleanup(&msto->encoder);
	kfree(msto);
}

static const struct drm_encoder_funcs
nv50_msto = {
	.destroy = nv50_msto_destroy,
};

static int
nv50_msto_new(struct drm_device *dev, u32 heads, const char *name, int id,
	      struct nv50_msto **pmsto)
{
	struct nv50_msto *msto;
	int ret;

	if (!(msto = *pmsto = kzalloc(sizeof(*msto), GFP_KERNEL)))
		return -ENOMEM;

	ret = drm_encoder_init(dev, &msto->encoder, &nv50_msto,
			       DRM_MODE_ENCODER_DPMST, "%s-mst-%d", name, id);
	if (ret) {
		kfree(*pmsto);
		*pmsto = NULL;
		return ret;
	}

	drm_encoder_helper_add(&msto->encoder, &nv50_msto_help);
	msto->encoder.possible_crtcs = heads;
	return 0;
}

static struct drm_encoder *
nv50_mstc_atomic_best_encoder(struct drm_connector *connector,
			      struct drm_connector_state *connector_state)
{
	struct nv50_head *head = nv50_head(connector_state->crtc);
	struct nv50_mstc *mstc = nv50_mstc(connector);
	if (mstc->port) {
		struct nv50_mstm *mstm = mstc->mstm;
		return &mstm->msto[head->base.index]->encoder;
	}
	return NULL;
}

static struct drm_encoder *
nv50_mstc_best_encoder(struct drm_connector *connector)
{
	struct nv50_mstc *mstc = nv50_mstc(connector);
	if (mstc->port) {
		struct nv50_mstm *mstm = mstc->mstm;
		return &mstm->msto[0]->encoder;
	}
	return NULL;
}

static enum drm_mode_status
nv50_mstc_mode_valid(struct drm_connector *connector,
		     struct drm_display_mode *mode)
{
	return MODE_OK;
}

static int
nv50_mstc_get_modes(struct drm_connector *connector)
{
	struct nv50_mstc *mstc = nv50_mstc(connector);
	int ret = 0;

	mstc->edid = drm_dp_mst_get_edid(&mstc->connector, mstc->port->mgr, mstc->port);
	drm_mode_connector_update_edid_property(&mstc->connector, mstc->edid);
	if (mstc->edid) {
		ret = drm_add_edid_modes(&mstc->connector, mstc->edid);
		drm_edid_to_eld(&mstc->connector, mstc->edid);
	}

	if (!mstc->connector.display_info.bpc)
		mstc->connector.display_info.bpc = 8;

	if (mstc->native)
		drm_mode_destroy(mstc->connector.dev, mstc->native);
	mstc->native = nouveau_conn_native_mode(&mstc->connector);
	return ret;
}

static const struct drm_connector_helper_funcs
nv50_mstc_help = {
	.get_modes = nv50_mstc_get_modes,
	.mode_valid = nv50_mstc_mode_valid,
	.best_encoder = nv50_mstc_best_encoder,
	.atomic_best_encoder = nv50_mstc_atomic_best_encoder,
};

static enum drm_connector_status
nv50_mstc_detect(struct drm_connector *connector, bool force)
{
	struct nv50_mstc *mstc = nv50_mstc(connector);
	if (!mstc->port)
		return connector_status_disconnected;
	return drm_dp_mst_detect_port(connector, mstc->port->mgr, mstc->port);
}

static void
nv50_mstc_destroy(struct drm_connector *connector)
{
	struct nv50_mstc *mstc = nv50_mstc(connector);
	drm_connector_cleanup(&mstc->connector);
	kfree(mstc);
}

static const struct drm_connector_funcs
nv50_mstc = {
	.dpms = drm_atomic_helper_connector_dpms,
	.reset = nouveau_conn_reset,
	.detect = nv50_mstc_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = drm_atomic_helper_connector_set_property,
	.destroy = nv50_mstc_destroy,
	.atomic_duplicate_state = nouveau_conn_atomic_duplicate_state,
	.atomic_destroy_state = nouveau_conn_atomic_destroy_state,
	.atomic_set_property = nouveau_conn_atomic_set_property,
	.atomic_get_property = nouveau_conn_atomic_get_property,
};

static int
nv50_mstc_new(struct nv50_mstm *mstm, struct drm_dp_mst_port *port,
	      const char *path, struct nv50_mstc **pmstc)
{
	struct drm_device *dev = mstm->outp->base.base.dev;
	struct nv50_mstc *mstc;
	int ret, i;

	if (!(mstc = *pmstc = kzalloc(sizeof(*mstc), GFP_KERNEL)))
		return -ENOMEM;
	mstc->mstm = mstm;
	mstc->port = port;

	ret = drm_connector_init(dev, &mstc->connector, &nv50_mstc,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		kfree(*pmstc);
		*pmstc = NULL;
		return ret;
	}

	drm_connector_helper_add(&mstc->connector, &nv50_mstc_help);

	mstc->connector.funcs->reset(&mstc->connector);
	nouveau_conn_attach_properties(&mstc->connector);

	for (i = 0; i < ARRAY_SIZE(mstm->msto) && mstm->msto; i++)
		drm_mode_connector_attach_encoder(&mstc->connector, &mstm->msto[i]->encoder);

	drm_object_attach_property(&mstc->connector.base, dev->mode_config.path_property, 0);
	drm_object_attach_property(&mstc->connector.base, dev->mode_config.tile_property, 0);
	drm_mode_connector_set_path_property(&mstc->connector, path);
	return 0;
}

static void
nv50_mstm_cleanup(struct nv50_mstm *mstm)
{
	struct nouveau_drm *drm = nouveau_drm(mstm->outp->base.base.dev);
	struct drm_encoder *encoder;
	int ret;

	NV_ATOMIC(drm, "%s: mstm cleanup\n", mstm->outp->base.base.name);
	ret = drm_dp_check_act_status(&mstm->mgr);

	ret = drm_dp_update_payload_part2(&mstm->mgr);

	drm_for_each_encoder(encoder, mstm->outp->base.base.dev) {
		if (encoder->encoder_type == DRM_MODE_ENCODER_DPMST) {
			struct nv50_msto *msto = nv50_msto(encoder);
			struct nv50_mstc *mstc = msto->mstc;
			if (mstc && mstc->mstm == mstm)
				nv50_msto_cleanup(msto);
		}
	}

	mstm->modified = false;
}

static void
nv50_mstm_prepare(struct nv50_mstm *mstm)
{
	struct nouveau_drm *drm = nouveau_drm(mstm->outp->base.base.dev);
	struct drm_encoder *encoder;
	int ret;

	NV_ATOMIC(drm, "%s: mstm prepare\n", mstm->outp->base.base.name);
	ret = drm_dp_update_payload_part1(&mstm->mgr);

	drm_for_each_encoder(encoder, mstm->outp->base.base.dev) {
		if (encoder->encoder_type == DRM_MODE_ENCODER_DPMST) {
			struct nv50_msto *msto = nv50_msto(encoder);
			struct nv50_mstc *mstc = msto->mstc;
			if (mstc && mstc->mstm == mstm)
				nv50_msto_prepare(msto);
		}
	}
}

static void
nv50_mstm_hotplug(struct drm_dp_mst_topology_mgr *mgr)
{
	struct nv50_mstm *mstm = nv50_mstm(mgr);
	drm_kms_helper_hotplug_event(mstm->outp->base.base.dev);
}

static void
nv50_mstm_destroy_connector(struct drm_dp_mst_topology_mgr *mgr,
			    struct drm_connector *connector)
{
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nv50_mstc *mstc = nv50_mstc(connector);

	drm_connector_unregister(&mstc->connector);

	drm_modeset_lock_all(drm->dev);
	drm_fb_helper_remove_one_connector(&drm->fbcon->helper, &mstc->connector);
	mstc->port = NULL;
	drm_modeset_unlock_all(drm->dev);

	drm_connector_unreference(&mstc->connector);
}

static void
nv50_mstm_register_connector(struct drm_connector *connector)
{
	struct nouveau_drm *drm = nouveau_drm(connector->dev);

	drm_modeset_lock_all(drm->dev);
	drm_fb_helper_add_one_connector(&drm->fbcon->helper, connector);
	drm_modeset_unlock_all(drm->dev);

	drm_connector_register(connector);
}

static struct drm_connector *
nv50_mstm_add_connector(struct drm_dp_mst_topology_mgr *mgr,
			struct drm_dp_mst_port *port, const char *path)
{
	struct nv50_mstm *mstm = nv50_mstm(mgr);
	struct nv50_mstc *mstc;
	int ret;

	ret = nv50_mstc_new(mstm, port, path, &mstc);
	if (ret) {
		if (mstc)
			mstc->connector.funcs->destroy(&mstc->connector);
		return NULL;
	}

	return &mstc->connector;
}

static const struct drm_dp_mst_topology_cbs
nv50_mstm = {
	.add_connector = nv50_mstm_add_connector,
	.register_connector = nv50_mstm_register_connector,
	.destroy_connector = nv50_mstm_destroy_connector,
	.hotplug = nv50_mstm_hotplug,
};

void
nv50_mstm_service(struct nv50_mstm *mstm)
{
	struct drm_dp_aux *aux = mstm->mgr.aux;
	bool handled = true;
	int ret;
	u8 esi[8] = {};

	while (handled) {
		ret = drm_dp_dpcd_read(aux, DP_SINK_COUNT_ESI, esi, 8);
		if (ret != 8) {
			drm_dp_mst_topology_mgr_set_mst(&mstm->mgr, false);
			return;
		}

		drm_dp_mst_hpd_irq(&mstm->mgr, esi, &handled);
		if (!handled)
			break;

		drm_dp_dpcd_write(aux, DP_SINK_COUNT_ESI + 1, &esi[1], 3);
	}
}

void
nv50_mstm_remove(struct nv50_mstm *mstm)
{
	if (mstm)
		drm_dp_mst_topology_mgr_set_mst(&mstm->mgr, false);
}

static int
nv50_mstm_enable(struct nv50_mstm *mstm, u8 dpcd, int state)
{
	struct nouveau_encoder *outp = mstm->outp;
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_sor_dp_mst_link_v0 mst;
	} args = {
		.base.version = 1,
		.base.method = NV50_DISP_MTHD_V1_SOR_DP_MST_LINK,
		.base.hasht = outp->dcb->hasht,
		.base.hashm = outp->dcb->hashm,
		.mst.state = state,
	};
	struct nouveau_drm *drm = nouveau_drm(outp->base.base.dev);
	struct nvif_object *disp = &drm->display->disp;
	int ret;

	if (dpcd >= 0x12) {
		ret = drm_dp_dpcd_readb(mstm->mgr.aux, DP_MSTM_CTRL, &dpcd);
		if (ret < 0)
			return ret;

		dpcd &= ~DP_MST_EN;
		if (state)
			dpcd |= DP_MST_EN;

		ret = drm_dp_dpcd_writeb(mstm->mgr.aux, DP_MSTM_CTRL, dpcd);
		if (ret < 0)
			return ret;
	}

	return nvif_mthd(disp, 0, &args, sizeof(args));
}

int
nv50_mstm_detect(struct nv50_mstm *mstm, u8 dpcd[8], int allow)
{
	int ret, state = 0;

	if (!mstm)
		return 0;

	if (dpcd[0] >= 0x12) {
		ret = drm_dp_dpcd_readb(mstm->mgr.aux, DP_MSTM_CAP, &dpcd[1]);
		if (ret < 0)
			return ret;

		if (!(dpcd[1] & DP_MST_CAP))
			dpcd[0] = 0x11;
		else
			state = allow;
	}

	ret = nv50_mstm_enable(mstm, dpcd[0], state);
	if (ret)
		return ret;

	ret = drm_dp_mst_topology_mgr_set_mst(&mstm->mgr, state);
	if (ret)
		return nv50_mstm_enable(mstm, dpcd[0], 0);

	return mstm->mgr.mst_state;
}

static void
nv50_mstm_fini(struct nv50_mstm *mstm)
{
	if (mstm && mstm->mgr.mst_state)
		drm_dp_mst_topology_mgr_suspend(&mstm->mgr);
}

static void
nv50_mstm_init(struct nv50_mstm *mstm)
{
	if (mstm && mstm->mgr.mst_state)
		drm_dp_mst_topology_mgr_resume(&mstm->mgr);
}

static void
nv50_mstm_del(struct nv50_mstm **pmstm)
{
	struct nv50_mstm *mstm = *pmstm;
	if (mstm) {
		kfree(*pmstm);
		*pmstm = NULL;
	}
}

static int
nv50_mstm_new(struct nouveau_encoder *outp, struct drm_dp_aux *aux, int aux_max,
	      int conn_base_id, struct nv50_mstm **pmstm)
{
	const int max_payloads = hweight8(outp->dcb->heads);
	struct drm_device *dev = outp->base.base.dev;
	struct nv50_mstm *mstm;
	int ret, i;
	u8 dpcd;

	/* This is a workaround for some monitors not functioning
	 * correctly in MST mode on initial module load.  I think
	 * some bad interaction with the VBIOS may be responsible.
	 *
	 * A good ol' off and on again seems to work here ;)
	 */
	ret = drm_dp_dpcd_readb(aux, DP_DPCD_REV, &dpcd);
	if (ret >= 0 && dpcd >= 0x12)
		drm_dp_dpcd_writeb(aux, DP_MSTM_CTRL, 0);

	if (!(mstm = *pmstm = kzalloc(sizeof(*mstm), GFP_KERNEL)))
		return -ENOMEM;
	mstm->outp = outp;
	mstm->mgr.cbs = &nv50_mstm;

	ret = drm_dp_mst_topology_mgr_init(&mstm->mgr, dev, aux, aux_max,
					   max_payloads, conn_base_id);
	if (ret)
		return ret;

	for (i = 0; i < max_payloads; i++) {
		ret = nv50_msto_new(dev, outp->dcb->heads, outp->base.base.name,
				    i, &mstm->msto[i]);
		if (ret)
			return ret;
	}

	return 0;
}

/******************************************************************************
 * SOR
 *****************************************************************************/
static void
nv50_sor_dpms(struct drm_encoder *encoder, int mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nv50_disp *disp = nv50_disp(encoder->dev);
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_sor_pwr_v0 pwr;
	} args = {
		.base.version = 1,
		.base.method = NV50_DISP_MTHD_V1_SOR_PWR,
		.base.hasht  = nv_encoder->dcb->hasht,
		.base.hashm  = nv_encoder->dcb->hashm,
		.pwr.state = mode == DRM_MODE_DPMS_ON,
	};

	nvif_mthd(disp->disp, 0, &args, sizeof(args));
}

static void
nv50_sor_update(struct nouveau_encoder *nv_encoder, u8 head,
		struct drm_display_mode *mode, u8 proto, u8 depth)
{
	struct nv50_dmac *core = &nv50_mast(nv_encoder->base.base.dev)->base;
	u32 *push;

	if (!mode) {
		nv_encoder->ctrl &= ~BIT(head);
		if (!(nv_encoder->ctrl & 0x0000000f))
			nv_encoder->ctrl = 0;
	} else {
		nv_encoder->ctrl |= proto << 8;
		nv_encoder->ctrl |= BIT(head);
	}

	if ((push = evo_wait(core, 6))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			if (mode) {
				if (mode->flags & DRM_MODE_FLAG_NHSYNC)
					nv_encoder->ctrl |= 0x00001000;
				if (mode->flags & DRM_MODE_FLAG_NVSYNC)
					nv_encoder->ctrl |= 0x00002000;
				nv_encoder->ctrl |= depth << 16;
			}
			evo_mthd(push, 0x0600 + (nv_encoder->or * 0x40), 1);
		} else {
			if (mode) {
				u32 magic = 0x31ec6000 | (head << 25);
				u32 syncs = 0x00000001;
				if (mode->flags & DRM_MODE_FLAG_NHSYNC)
					syncs |= 0x00000008;
				if (mode->flags & DRM_MODE_FLAG_NVSYNC)
					syncs |= 0x00000010;
				if (mode->flags & DRM_MODE_FLAG_INTERLACE)
					magic |= 0x00000001;

				evo_mthd(push, 0x0404 + (head * 0x300), 2);
				evo_data(push, syncs | (depth << 6));
				evo_data(push, magic);
			}
			evo_mthd(push, 0x0200 + (nv_encoder->or * 0x20), 1);
		}
		evo_data(push, nv_encoder->ctrl);
		evo_kick(push, core);
	}
}

static void
nv50_sor_disable(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(nv_encoder->crtc);

	nv_encoder->crtc = NULL;

	if (nv_crtc) {
		struct nvkm_i2c_aux *aux = nv_encoder->aux;
		u8 pwr;

		if (aux) {
			int ret = nvkm_rdaux(aux, DP_SET_POWER, &pwr, 1);
			if (ret == 0) {
				pwr &= ~DP_SET_POWER_MASK;
				pwr |=  DP_SET_POWER_D3;
				nvkm_wraux(aux, DP_SET_POWER, &pwr, 1);
			}
		}

		nv_encoder->update(nv_encoder, nv_crtc->index, NULL, 0, 0);
		nv50_audio_disable(encoder, nv_crtc);
		nv50_hdmi_disable(&nv_encoder->base.base, nv_crtc);
	}
}

static void
nv50_sor_enable(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct drm_display_mode *mode = &nv_crtc->base.state->adjusted_mode;
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_sor_lvds_script_v0 lvds;
	} lvds = {
		.base.version = 1,
		.base.method  = NV50_DISP_MTHD_V1_SOR_LVDS_SCRIPT,
		.base.hasht   = nv_encoder->dcb->hasht,
		.base.hashm   = nv_encoder->dcb->hashm,
	};
	struct nv50_disp *disp = nv50_disp(encoder->dev);
	struct drm_device *dev = encoder->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_connector *nv_connector;
	struct nvbios *bios = &drm->vbios;
	u8 proto = 0xf;
	u8 depth = 0x0;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	nv_encoder->crtc = encoder->crtc;

	switch (nv_encoder->dcb->type) {
	case DCB_OUTPUT_TMDS:
		if (nv_encoder->dcb->sorconf.link & 1) {
			proto = 0x1;
			/* Only enable dual-link if:
			 *  - Need to (i.e. rate > 165MHz)
			 *  - DCB says we can
			 *  - Not an HDMI monitor, since there's no dual-link
			 *    on HDMI.
			 */
			if (mode->clock >= 165000 &&
			    nv_encoder->dcb->duallink_possible &&
			    !drm_detect_hdmi_monitor(nv_connector->edid))
				proto |= 0x4;
		} else {
			proto = 0x2;
		}

		nv50_hdmi_enable(&nv_encoder->base.base, mode);
		break;
	case DCB_OUTPUT_LVDS:
		proto = 0x0;

		if (bios->fp_no_ddc) {
			if (bios->fp.dual_link)
				lvds.lvds.script |= 0x0100;
			if (bios->fp.if_is_24bit)
				lvds.lvds.script |= 0x0200;
		} else {
			if (nv_connector->type == DCB_CONNECTOR_LVDS_SPWG) {
				if (((u8 *)nv_connector->edid)[121] == 2)
					lvds.lvds.script |= 0x0100;
			} else
			if (mode->clock >= bios->fp.duallink_transition_clk) {
				lvds.lvds.script |= 0x0100;
			}

			if (lvds.lvds.script & 0x0100) {
				if (bios->fp.strapless_is_24bit & 2)
					lvds.lvds.script |= 0x0200;
			} else {
				if (bios->fp.strapless_is_24bit & 1)
					lvds.lvds.script |= 0x0200;
			}

			if (nv_connector->base.display_info.bpc == 8)
				lvds.lvds.script |= 0x0200;
		}

		nvif_mthd(disp->disp, 0, &lvds, sizeof(lvds));
		break;
	case DCB_OUTPUT_DP:
		if (nv_connector->base.display_info.bpc == 6)
			depth = 0x2;
		else
		if (nv_connector->base.display_info.bpc == 8)
			depth = 0x5;
		else
			depth = 0x6;

		if (nv_encoder->dcb->sorconf.link & 1)
			proto = 0x8;
		else
			proto = 0x9;

		nv50_audio_enable(encoder, mode);
		break;
	default:
		BUG();
		break;
	}

	nv_encoder->update(nv_encoder, nv_crtc->index, mode, proto, depth);
}

static const struct drm_encoder_helper_funcs
nv50_sor_help = {
	.dpms = nv50_sor_dpms,
	.atomic_check = nv50_outp_atomic_check,
	.enable = nv50_sor_enable,
	.disable = nv50_sor_disable,
};

static void
nv50_sor_destroy(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	nv50_mstm_del(&nv_encoder->dp.mstm);
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs
nv50_sor_func = {
	.destroy = nv50_sor_destroy,
};

static int
nv50_sor_create(struct drm_connector *connector, struct dcb_output *dcbe)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nvkm_i2c *i2c = nvxx_i2c(&drm->client.device);
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;
	int type, ret;

	switch (dcbe->type) {
	case DCB_OUTPUT_LVDS: type = DRM_MODE_ENCODER_LVDS; break;
	case DCB_OUTPUT_TMDS:
	case DCB_OUTPUT_DP:
	default:
		type = DRM_MODE_ENCODER_TMDS;
		break;
	}

	nv_encoder = kzalloc(sizeof(*nv_encoder), GFP_KERNEL);
	if (!nv_encoder)
		return -ENOMEM;
	nv_encoder->dcb = dcbe;
	nv_encoder->or = ffs(dcbe->or) - 1;
	nv_encoder->update = nv50_sor_update;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(connector->dev, encoder, &nv50_sor_func, type,
			 "sor-%04x-%04x", dcbe->hasht, dcbe->hashm);
	drm_encoder_helper_add(encoder, &nv50_sor_help);

	drm_mode_connector_attach_encoder(connector, encoder);

	if (dcbe->type == DCB_OUTPUT_DP) {
		struct nvkm_i2c_aux *aux =
			nvkm_i2c_aux_find(i2c, dcbe->i2c_index);
		if (aux) {
			nv_encoder->i2c = &nv_connector->aux.ddc;
			nv_encoder->aux = aux;
		}

		/*TODO: Use DP Info Table to check for support. */
		if (nv50_disp(encoder->dev)->disp->oclass >= GF110_DISP) {
			ret = nv50_mstm_new(nv_encoder, &nv_connector->aux, 16,
					    nv_connector->base.base.id,
					    &nv_encoder->dp.mstm);
			if (ret)
				return ret;
		}
	} else {
		struct nvkm_i2c_bus *bus =
			nvkm_i2c_bus_find(i2c, dcbe->i2c_index);
		if (bus)
			nv_encoder->i2c = &bus->i2c;
	}

	return 0;
}

/******************************************************************************
 * PIOR
 *****************************************************************************/
static void
nv50_pior_dpms(struct drm_encoder *encoder, int mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nv50_disp *disp = nv50_disp(encoder->dev);
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_pior_pwr_v0 pwr;
	} args = {
		.base.version = 1,
		.base.method = NV50_DISP_MTHD_V1_PIOR_PWR,
		.base.hasht  = nv_encoder->dcb->hasht,
		.base.hashm  = nv_encoder->dcb->hashm,
		.pwr.state = mode == DRM_MODE_DPMS_ON,
		.pwr.type = nv_encoder->dcb->type,
	};

	nvif_mthd(disp->disp, 0, &args, sizeof(args));
}

static int
nv50_pior_atomic_check(struct drm_encoder *encoder,
		       struct drm_crtc_state *crtc_state,
		       struct drm_connector_state *conn_state)
{
	int ret = nv50_outp_atomic_check(encoder, crtc_state, conn_state);
	if (ret)
		return ret;
	crtc_state->adjusted_mode.clock *= 2;
	return 0;
}

static void
nv50_pior_disable(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nv50_mast *mast = nv50_mast(encoder->dev);
	const int or = nv_encoder->or;
	u32 *push;

	if (nv_encoder->crtc) {
		push = evo_wait(mast, 4);
		if (push) {
			if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
				evo_mthd(push, 0x0700 + (or * 0x040), 1);
				evo_data(push, 0x00000000);
			}
			evo_kick(push, mast);
		}
	}

	nv_encoder->crtc = NULL;
}

static void
nv50_pior_enable(struct drm_encoder *encoder)
{
	struct nv50_mast *mast = nv50_mast(encoder->dev);
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector;
	struct drm_display_mode *mode = &nv_crtc->base.state->adjusted_mode;
	u8 owner = 1 << nv_crtc->index;
	u8 proto, depth;
	u32 *push;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	switch (nv_connector->base.display_info.bpc) {
	case 10: depth = 0x6; break;
	case  8: depth = 0x5; break;
	case  6: depth = 0x2; break;
	default: depth = 0x0; break;
	}

	switch (nv_encoder->dcb->type) {
	case DCB_OUTPUT_TMDS:
	case DCB_OUTPUT_DP:
		proto = 0x0;
		break;
	default:
		BUG();
		break;
	}

	push = evo_wait(mast, 8);
	if (push) {
		if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
			u32 ctrl = (depth << 16) | (proto << 8) | owner;
			if (mode->flags & DRM_MODE_FLAG_NHSYNC)
				ctrl |= 0x00001000;
			if (mode->flags & DRM_MODE_FLAG_NVSYNC)
				ctrl |= 0x00002000;
			evo_mthd(push, 0x0700 + (nv_encoder->or * 0x040), 1);
			evo_data(push, ctrl);
		}

		evo_kick(push, mast);
	}

	nv_encoder->crtc = encoder->crtc;
}

static const struct drm_encoder_helper_funcs
nv50_pior_help = {
	.dpms = nv50_pior_dpms,
	.atomic_check = nv50_pior_atomic_check,
	.enable = nv50_pior_enable,
	.disable = nv50_pior_disable,
};

static void
nv50_pior_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs
nv50_pior_func = {
	.destroy = nv50_pior_destroy,
};

static int
nv50_pior_create(struct drm_connector *connector, struct dcb_output *dcbe)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nvkm_i2c *i2c = nvxx_i2c(&drm->client.device);
	struct nvkm_i2c_bus *bus = NULL;
	struct nvkm_i2c_aux *aux = NULL;
	struct i2c_adapter *ddc;
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;
	int type;

	switch (dcbe->type) {
	case DCB_OUTPUT_TMDS:
		bus  = nvkm_i2c_bus_find(i2c, NVKM_I2C_BUS_EXT(dcbe->extdev));
		ddc  = bus ? &bus->i2c : NULL;
		type = DRM_MODE_ENCODER_TMDS;
		break;
	case DCB_OUTPUT_DP:
		aux  = nvkm_i2c_aux_find(i2c, NVKM_I2C_AUX_EXT(dcbe->extdev));
		ddc  = aux ? &nv_connector->aux.ddc : NULL;
		type = DRM_MODE_ENCODER_TMDS;
		break;
	default:
		return -ENODEV;
	}

	nv_encoder = kzalloc(sizeof(*nv_encoder), GFP_KERNEL);
	if (!nv_encoder)
		return -ENOMEM;
	nv_encoder->dcb = dcbe;
	nv_encoder->or = ffs(dcbe->or) - 1;
	nv_encoder->i2c = ddc;
	nv_encoder->aux = aux;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(connector->dev, encoder, &nv50_pior_func, type,
			 "pior-%04x-%04x", dcbe->hasht, dcbe->hashm);
	drm_encoder_helper_add(encoder, &nv50_pior_help);

	drm_mode_connector_attach_encoder(connector, encoder);
	return 0;
}

/******************************************************************************
 * Atomic
 *****************************************************************************/

static void
nv50_disp_atomic_commit_core(struct nouveau_drm *drm, u32 interlock)
{
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct nv50_dmac *core = &disp->mast.base;
	struct nv50_mstm *mstm;
	struct drm_encoder *encoder;
	u32 *push;

	NV_ATOMIC(drm, "commit core %08x\n", interlock);

	drm_for_each_encoder(encoder, drm->dev) {
		if (encoder->encoder_type != DRM_MODE_ENCODER_DPMST) {
			mstm = nouveau_encoder(encoder)->dp.mstm;
			if (mstm && mstm->modified)
				nv50_mstm_prepare(mstm);
		}
	}

	if ((push = evo_wait(core, 5))) {
		evo_mthd(push, 0x0084, 1);
		evo_data(push, 0x80000000);
		evo_mthd(push, 0x0080, 2);
		evo_data(push, interlock);
		evo_data(push, 0x00000000);
		nouveau_bo_wr32(disp->sync, 0, 0x00000000);
		evo_kick(push, core);
		if (nvif_msec(&drm->client.device, 2000ULL,
			if (nouveau_bo_rd32(disp->sync, 0))
				break;
			usleep_range(1, 2);
		) < 0)
			NV_ERROR(drm, "EVO timeout\n");
	}

	drm_for_each_encoder(encoder, drm->dev) {
		if (encoder->encoder_type != DRM_MODE_ENCODER_DPMST) {
			mstm = nouveau_encoder(encoder)->dp.mstm;
			if (mstm && mstm->modified)
				nv50_mstm_cleanup(mstm);
		}
	}
}

static void
nv50_disp_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	struct drm_plane_state *plane_state;
	struct drm_plane *plane;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv50_disp *disp = nv50_disp(dev);
	struct nv50_atom *atom = nv50_atom(state);
	struct nv50_outp_atom *outp, *outt;
	u32 interlock_core = 0;
	u32 interlock_chan = 0;
	int i;

	NV_ATOMIC(drm, "commit %d %d\n", atom->lock_core, atom->flush_disable);
	drm_atomic_helper_wait_for_fences(dev, state, false);
	drm_atomic_helper_wait_for_dependencies(state);
	drm_atomic_helper_update_legacy_modeset_state(dev, state);

	if (atom->lock_core)
		mutex_lock(&disp->mutex);

	/* Disable head(s). */
	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		struct nv50_head_atom *asyh = nv50_head_atom(crtc->state);
		struct nv50_head *head = nv50_head(crtc);

		NV_ATOMIC(drm, "%s: clr %04x (set %04x)\n", crtc->name,
			  asyh->clr.mask, asyh->set.mask);

		if (asyh->clr.mask) {
			nv50_head_flush_clr(head, asyh, atom->flush_disable);
			interlock_core |= 1;
		}
	}

	/* Disable plane(s). */
	for_each_plane_in_state(state, plane, plane_state, i) {
		struct nv50_wndw_atom *asyw = nv50_wndw_atom(plane->state);
		struct nv50_wndw *wndw = nv50_wndw(plane);

		NV_ATOMIC(drm, "%s: clr %02x (set %02x)\n", plane->name,
			  asyw->clr.mask, asyw->set.mask);
		if (!asyw->clr.mask)
			continue;

		interlock_chan |= nv50_wndw_flush_clr(wndw, interlock_core,
						      atom->flush_disable,
						      asyw);
	}

	/* Disable output path(s). */
	list_for_each_entry(outp, &atom->outp, head) {
		const struct drm_encoder_helper_funcs *help;
		struct drm_encoder *encoder;

		encoder = outp->encoder;
		help = encoder->helper_private;

		NV_ATOMIC(drm, "%s: clr %02x (set %02x)\n", encoder->name,
			  outp->clr.mask, outp->set.mask);

		if (outp->clr.mask) {
			help->disable(encoder);
			interlock_core |= 1;
			if (outp->flush_disable) {
				nv50_disp_atomic_commit_core(drm, interlock_chan);
				interlock_core = 0;
				interlock_chan = 0;
			}
		}
	}

	/* Flush disable. */
	if (interlock_core) {
		if (atom->flush_disable) {
			nv50_disp_atomic_commit_core(drm, interlock_chan);
			interlock_core = 0;
			interlock_chan = 0;
		}
	}

	/* Update output path(s). */
	list_for_each_entry_safe(outp, outt, &atom->outp, head) {
		const struct drm_encoder_helper_funcs *help;
		struct drm_encoder *encoder;

		encoder = outp->encoder;
		help = encoder->helper_private;

		NV_ATOMIC(drm, "%s: set %02x (clr %02x)\n", encoder->name,
			  outp->set.mask, outp->clr.mask);

		if (outp->set.mask) {
			help->enable(encoder);
			interlock_core = 1;
		}

		list_del(&outp->head);
		kfree(outp);
	}

	/* Update head(s). */
	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		struct nv50_head_atom *asyh = nv50_head_atom(crtc->state);
		struct nv50_head *head = nv50_head(crtc);

		NV_ATOMIC(drm, "%s: set %04x (clr %04x)\n", crtc->name,
			  asyh->set.mask, asyh->clr.mask);

		if (asyh->set.mask) {
			nv50_head_flush_set(head, asyh);
			interlock_core = 1;
		}
	}

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		if (crtc->state->event)
			drm_crtc_vblank_get(crtc);
	}

	/* Update plane(s). */
	for_each_plane_in_state(state, plane, plane_state, i) {
		struct nv50_wndw_atom *asyw = nv50_wndw_atom(plane->state);
		struct nv50_wndw *wndw = nv50_wndw(plane);

		NV_ATOMIC(drm, "%s: set %02x (clr %02x)\n", plane->name,
			  asyw->set.mask, asyw->clr.mask);
		if ( !asyw->set.mask &&
		    (!asyw->clr.mask || atom->flush_disable))
			continue;

		interlock_chan |= nv50_wndw_flush_set(wndw, interlock_core, asyw);
	}

	/* Flush update. */
	if (interlock_core) {
		if (!interlock_chan && atom->state.legacy_cursor_update) {
			u32 *push = evo_wait(&disp->mast, 2);
			if (push) {
				evo_mthd(push, 0x0080, 1);
				evo_data(push, 0x00000000);
				evo_kick(push, &disp->mast);
			}
		} else {
			nv50_disp_atomic_commit_core(drm, interlock_chan);
		}
	}

	if (atom->lock_core)
		mutex_unlock(&disp->mutex);

	/* Wait for HW to signal completion. */
	for_each_plane_in_state(state, plane, plane_state, i) {
		struct nv50_wndw_atom *asyw = nv50_wndw_atom(plane->state);
		struct nv50_wndw *wndw = nv50_wndw(plane);
		int ret = nv50_wndw_wait_armed(wndw, asyw);
		if (ret)
			NV_ERROR(drm, "%s: timeout\n", plane->name);
	}

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		if (crtc->state->event) {
			unsigned long flags;
			/* Get correct count/ts if racing with vblank irq */
			drm_accurate_vblank_count(crtc);
			spin_lock_irqsave(&crtc->dev->event_lock, flags);
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
			spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
			crtc->state->event = NULL;
			drm_crtc_vblank_put(crtc);
		}
	}

	drm_atomic_helper_commit_hw_done(state);
	drm_atomic_helper_cleanup_planes(dev, state);
	drm_atomic_helper_commit_cleanup_done(state);
	drm_atomic_state_put(state);
}

static void
nv50_disp_atomic_commit_work(struct work_struct *work)
{
	struct drm_atomic_state *state =
		container_of(work, typeof(*state), commit_work);
	nv50_disp_atomic_commit_tail(state);
}

static int
nv50_disp_atomic_commit(struct drm_device *dev,
			struct drm_atomic_state *state, bool nonblock)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv50_disp *disp = nv50_disp(dev);
	struct drm_plane_state *plane_state;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	bool active = false;
	int ret, i;

	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	ret = drm_atomic_helper_setup_commit(state, nonblock);
	if (ret)
		goto done;

	INIT_WORK(&state->commit_work, nv50_disp_atomic_commit_work);

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		goto done;

	if (!nonblock) {
		ret = drm_atomic_helper_wait_for_fences(dev, state, true);
		if (ret)
			goto done;
	}

	for_each_plane_in_state(state, plane, plane_state, i) {
		struct nv50_wndw_atom *asyw = nv50_wndw_atom(plane_state);
		struct nv50_wndw *wndw = nv50_wndw(plane);
		if (asyw->set.image) {
			asyw->ntfy.handle = wndw->dmac->sync.handle;
			asyw->ntfy.offset = wndw->ntfy;
			asyw->ntfy.awaken = false;
			asyw->set.ntfy = true;
			nouveau_bo_wr32(disp->sync, wndw->ntfy / 4, 0x00000000);
			wndw->ntfy ^= 0x10;
		}
	}

	drm_atomic_helper_swap_state(state, true);
	drm_atomic_state_get(state);

	if (nonblock)
		queue_work(system_unbound_wq, &state->commit_work);
	else
		nv50_disp_atomic_commit_tail(state);

	drm_for_each_crtc(crtc, dev) {
		if (crtc->state->enable) {
			if (!drm->have_disp_power_ref) {
				drm->have_disp_power_ref = true;
				return ret;
			}
			active = true;
			break;
		}
	}

	if (!active && drm->have_disp_power_ref) {
		pm_runtime_put_autosuspend(dev->dev);
		drm->have_disp_power_ref = false;
	}

done:
	pm_runtime_put_autosuspend(dev->dev);
	return ret;
}

static struct nv50_outp_atom *
nv50_disp_outp_atomic_add(struct nv50_atom *atom, struct drm_encoder *encoder)
{
	struct nv50_outp_atom *outp;

	list_for_each_entry(outp, &atom->outp, head) {
		if (outp->encoder == encoder)
			return outp;
	}

	outp = kzalloc(sizeof(*outp), GFP_KERNEL);
	if (!outp)
		return ERR_PTR(-ENOMEM);

	list_add(&outp->head, &atom->outp);
	outp->encoder = encoder;
	return outp;
}

static int
nv50_disp_outp_atomic_check_clr(struct nv50_atom *atom,
				struct drm_connector *connector)
{
	struct drm_encoder *encoder = connector->state->best_encoder;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	struct nv50_outp_atom *outp;

	if (!(crtc = connector->state->crtc))
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(&atom->state, crtc);
	if (crtc->state->active && drm_atomic_crtc_needs_modeset(crtc_state)) {
		outp = nv50_disp_outp_atomic_add(atom, encoder);
		if (IS_ERR(outp))
			return PTR_ERR(outp);

		if (outp->encoder->encoder_type == DRM_MODE_ENCODER_DPMST) {
			outp->flush_disable = true;
			atom->flush_disable = true;
		}
		outp->clr.ctrl = true;
		atom->lock_core = true;
	}

	return 0;
}

static int
nv50_disp_outp_atomic_check_set(struct nv50_atom *atom,
				struct drm_connector_state *connector_state)
{
	struct drm_encoder *encoder = connector_state->best_encoder;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	struct nv50_outp_atom *outp;

	if (!(crtc = connector_state->crtc))
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(&atom->state, crtc);
	if (crtc_state->active && drm_atomic_crtc_needs_modeset(crtc_state)) {
		outp = nv50_disp_outp_atomic_add(atom, encoder);
		if (IS_ERR(outp))
			return PTR_ERR(outp);

		outp->set.ctrl = true;
		atom->lock_core = true;
	}

	return 0;
}

static int
nv50_disp_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	struct nv50_atom *atom = nv50_atom(state);
	struct drm_connector_state *connector_state;
	struct drm_connector *connector;
	int ret, i;

	ret = drm_atomic_helper_check(dev, state);
	if (ret)
		return ret;

	for_each_connector_in_state(state, connector, connector_state, i) {
		ret = nv50_disp_outp_atomic_check_clr(atom, connector);
		if (ret)
			return ret;

		ret = nv50_disp_outp_atomic_check_set(atom, connector_state);
		if (ret)
			return ret;
	}

	return 0;
}

static void
nv50_disp_atomic_state_clear(struct drm_atomic_state *state)
{
	struct nv50_atom *atom = nv50_atom(state);
	struct nv50_outp_atom *outp, *outt;

	list_for_each_entry_safe(outp, outt, &atom->outp, head) {
		list_del(&outp->head);
		kfree(outp);
	}

	drm_atomic_state_default_clear(state);
}

static void
nv50_disp_atomic_state_free(struct drm_atomic_state *state)
{
	struct nv50_atom *atom = nv50_atom(state);
	drm_atomic_state_default_release(&atom->state);
	kfree(atom);
}

static struct drm_atomic_state *
nv50_disp_atomic_state_alloc(struct drm_device *dev)
{
	struct nv50_atom *atom;
	if (!(atom = kzalloc(sizeof(*atom), GFP_KERNEL)) ||
	    drm_atomic_state_init(dev, &atom->state) < 0) {
		kfree(atom);
		return NULL;
	}
	INIT_LIST_HEAD(&atom->outp);
	return &atom->state;
}

static const struct drm_mode_config_funcs
nv50_disp_func = {
	.fb_create = nouveau_user_framebuffer_create,
	.output_poll_changed = nouveau_fbcon_output_poll_changed,
	.atomic_check = nv50_disp_atomic_check,
	.atomic_commit = nv50_disp_atomic_commit,
	.atomic_state_alloc = nv50_disp_atomic_state_alloc,
	.atomic_state_clear = nv50_disp_atomic_state_clear,
	.atomic_state_free = nv50_disp_atomic_state_free,
};

/******************************************************************************
 * Init
 *****************************************************************************/

void
nv50_display_fini(struct drm_device *dev)
{
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;
	struct drm_plane *plane;

	drm_for_each_plane(plane, dev) {
		struct nv50_wndw *wndw = nv50_wndw(plane);
		if (plane->funcs != &nv50_wndw)
			continue;
		nv50_wndw_fini(wndw);
	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->encoder_type != DRM_MODE_ENCODER_DPMST) {
			nv_encoder = nouveau_encoder(encoder);
			nv50_mstm_fini(nv_encoder->dp.mstm);
		}
	}
}

int
nv50_display_init(struct drm_device *dev)
{
	struct drm_encoder *encoder;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	u32 *push;

	push = evo_wait(nv50_mast(dev), 32);
	if (!push)
		return -EBUSY;

	evo_mthd(push, 0x0088, 1);
	evo_data(push, nv50_mast(dev)->base.sync.handle);
	evo_kick(push, nv50_mast(dev));

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->encoder_type != DRM_MODE_ENCODER_DPMST) {
			const struct drm_encoder_helper_funcs *help;
			struct nouveau_encoder *nv_encoder;

			nv_encoder = nouveau_encoder(encoder);
			help = encoder->helper_private;
			if (help && help->dpms)
				help->dpms(encoder, DRM_MODE_DPMS_ON);

			nv50_mstm_init(nv_encoder->dp.mstm);
		}
	}

	drm_for_each_crtc(crtc, dev) {
		nv50_head_lut_load(crtc);
	}

	drm_for_each_plane(plane, dev) {
		struct nv50_wndw *wndw = nv50_wndw(plane);
		if (plane->funcs != &nv50_wndw)
			continue;
		nv50_wndw_init(wndw);
	}

	return 0;
}

void
nv50_display_destroy(struct drm_device *dev)
{
	struct nv50_disp *disp = nv50_disp(dev);

	nv50_dmac_destroy(&disp->mast.base, disp->disp);

	nouveau_bo_unmap(disp->sync);
	if (disp->sync)
		nouveau_bo_unpin(disp->sync);
	nouveau_bo_ref(NULL, &disp->sync);

	nouveau_display(dev)->priv = NULL;
	kfree(disp);
}

MODULE_PARM_DESC(atomic, "Expose atomic ioctl (default: disabled)");
static int nouveau_atomic = 0;
module_param_named(atomic, nouveau_atomic, int, 0400);

int
nv50_display_create(struct drm_device *dev)
{
	struct nvif_device *device = &nouveau_drm(dev)->client.device;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct dcb_table *dcb = &drm->vbios.dcb;
	struct drm_connector *connector, *tmp;
	struct nv50_disp *disp;
	struct dcb_output *dcbe;
	int crtcs, ret, i;

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;

	mutex_init(&disp->mutex);

	nouveau_display(dev)->priv = disp;
	nouveau_display(dev)->dtor = nv50_display_destroy;
	nouveau_display(dev)->init = nv50_display_init;
	nouveau_display(dev)->fini = nv50_display_fini;
	disp->disp = &nouveau_display(dev)->disp;
	dev->mode_config.funcs = &nv50_disp_func;
	if (nouveau_atomic)
		dev->driver->driver_features |= DRIVER_ATOMIC;

	/* small shared memory area we use for notifiers and semaphores */
	ret = nouveau_bo_new(&drm->client, 4096, 0x1000, TTM_PL_FLAG_VRAM,
			     0, 0x0000, NULL, NULL, &disp->sync);
	if (!ret) {
		ret = nouveau_bo_pin(disp->sync, TTM_PL_FLAG_VRAM, true);
		if (!ret) {
			ret = nouveau_bo_map(disp->sync);
			if (ret)
				nouveau_bo_unpin(disp->sync);
		}
		if (ret)
			nouveau_bo_ref(NULL, &disp->sync);
	}

	if (ret)
		goto out;

	/* allocate master evo channel */
	ret = nv50_core_create(device, disp->disp, disp->sync->bo.offset,
			      &disp->mast);
	if (ret)
		goto out;

	/* create crtc objects to represent the hw heads */
	if (disp->disp->oclass >= GF110_DISP)
		crtcs = nvif_rd32(&device->object, 0x022448);
	else
		crtcs = 2;

	for (i = 0; i < crtcs; i++) {
		ret = nv50_head_create(dev, i);
		if (ret)
			goto out;
	}

	/* create encoder/connector objects based on VBIOS DCB table */
	for (i = 0, dcbe = &dcb->entry[0]; i < dcb->entries; i++, dcbe++) {
		connector = nouveau_connector_create(dev, dcbe->connector);
		if (IS_ERR(connector))
			continue;

		if (dcbe->location == DCB_LOC_ON_CHIP) {
			switch (dcbe->type) {
			case DCB_OUTPUT_TMDS:
			case DCB_OUTPUT_LVDS:
			case DCB_OUTPUT_DP:
				ret = nv50_sor_create(connector, dcbe);
				break;
			case DCB_OUTPUT_ANALOG:
				ret = nv50_dac_create(connector, dcbe);
				break;
			default:
				ret = -ENODEV;
				break;
			}
		} else {
			ret = nv50_pior_create(connector, dcbe);
		}

		if (ret) {
			NV_WARN(drm, "failed to create encoder %d/%d/%d: %d\n",
				     dcbe->location, dcbe->type,
				     ffs(dcbe->or) - 1, ret);
			ret = 0;
		}
	}

	/* cull any connectors we created that don't have an encoder */
	list_for_each_entry_safe(connector, tmp, &dev->mode_config.connector_list, head) {
		if (connector->encoder_ids[0])
			continue;

		NV_WARN(drm, "%s has no encoders, removing\n",
			connector->name);
		connector->funcs->destroy(connector);
	}

out:
	if (ret)
		nv50_display_destroy(dev);
	return ret;
}
