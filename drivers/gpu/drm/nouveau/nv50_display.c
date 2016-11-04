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

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_gem.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"
#include "nouveau_fence.h"
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

/******************************************************************************
 * Atomic state
 *****************************************************************************/
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
		};
		u16 mask;
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
 * Cursor Immediate
 *****************************************************************************/

struct nv50_curs {
	struct nv50_pioc base;
};

static int
nv50_curs_create(struct nvif_device *device, struct nvif_object *disp,
		 int head, struct nv50_curs *curs)
{
	struct nv50_disp_cursor_v0 args = {
		.head = head,
	};
	static const s32 oclass[] = {
		GK104_DISP_CURSOR,
		GF110_DISP_CURSOR,
		GT214_DISP_CURSOR,
		G82_DISP_CURSOR,
		NV50_DISP_CURSOR,
		0
	};

	return nv50_pioc_create(device, disp, oclass, head, &args, sizeof(args),
				&curs->base);
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

struct nv50_dmac {
	struct nv50_chan base;
	dma_addr_t handle;
	u32 *ptr;

	struct nvif_object sync;
	struct nvif_object vram;

	/* Protects against concurrent pushbuf access to this channel, lock is
	 * grabbed by evo_wait (if the pushbuf reservation is successful) and
	 * dropped again by evo_kick. */
	struct mutex lock;
};

static void
nv50_dmac_destroy(struct nv50_dmac *dmac, struct nvif_object *disp)
{
	struct nvif_device *device = dmac->base.device;

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
		GP104_DISP_CORE_CHANNEL_DMA,
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
	struct nouveau_bo *image;
	struct nv50_curs curs;
	struct nv50_sync sync;
	struct nv50_ovly ovly;
	struct nv50_oimm oimm;

	struct nv50_head_atom arm;
	struct nv50_head_atom asy;
};

#define nv50_head(c) ((struct nv50_head *)nouveau_crtc(c))
#define nv50_curs(c) (&nv50_head(c)->curs)
#define nv50_sync(c) (&nv50_head(c)->sync)
#define nv50_ovly(c) (&nv50_head(c)->ovly)
#define nv50_oimm(c) (&nv50_head(c)->oimm)
#define nv50_chan(c) (&(c)->base.base)
#define nv50_vers(c) nv50_chan(c)->user.oclass

struct nv50_fbdma {
	struct list_head head;
	struct nvif_object core;
	struct nvif_object base[4];
};

struct nv50_disp {
	struct nvif_object *disp;
	struct nv50_mast mast;

	struct list_head fbdma;

	struct nouveau_bo *sync;
};

static struct nv50_disp *
nv50_disp(struct drm_device *dev)
{
	return nouveau_display(dev)->priv;
}

#define nv50_mast(d) (&nv50_disp(d)->mast)

static struct drm_crtc *
nv50_display_crtc_get(struct drm_encoder *encoder)
{
	return nouveau_encoder(encoder)->crtc;
}

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
			printk(KERN_ERR "nouveau: evo channel stalled\n");
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

#define evo_mthd(p,m,s) do {                                                   \
	const u32 _m = (m), _s = (s);                                          \
	if (drm_debug & DRM_UT_KMS)                                            \
		printk(KERN_ERR "%04x %d %s\n", _m, _s, __func__);             \
	*((p)++) = ((_s << 18) | _m);                                          \
} while(0)

#define evo_data(p,d) do {                                                     \
	const u32 _d = (d);                                                    \
	if (drm_debug & DRM_UT_KMS)                                            \
		printk(KERN_ERR "\t%08x\n", _d);                               \
	*((p)++) = _d;                                                         \
} while(0)

static bool
evo_sync_wait(void *data)
{
	if (nouveau_bo_rd32(data, EVO_MAST_NTFY) != 0x00000000)
		return true;
	usleep_range(1, 2);
	return false;
}

static int
evo_sync(struct drm_device *dev)
{
	struct nvif_device *device = &nouveau_drm(dev)->device;
	struct nv50_disp *disp = nv50_disp(dev);
	struct nv50_mast *mast = nv50_mast(dev);
	u32 *push = evo_wait(mast, 8);
	if (push) {
		nouveau_bo_wr32(disp->sync, EVO_MAST_NTFY, 0x00000000);
		evo_mthd(push, 0x0084, 1);
		evo_data(push, 0x80000000 | EVO_MAST_NTFY);
		evo_mthd(push, 0x0080, 2);
		evo_data(push, 0x00000000);
		evo_data(push, 0x00000000);
		evo_kick(push, mast);
		if (nvif_msec(device, 2000,
			if (evo_sync_wait(disp->sync))
				break;
		) >= 0)
			return 0;
	}

	return -EBUSY;
}

/******************************************************************************
 * Page flipping channel
 *****************************************************************************/
struct nouveau_bo *
nv50_display_crtc_sema(struct drm_device *dev, int crtc)
{
	return nv50_disp(dev)->sync;
}

struct nv50_display_flip {
	struct nv50_disp *disp;
	struct nv50_sync *chan;
};

static bool
nv50_display_flip_wait(void *data)
{
	struct nv50_display_flip *flip = data;
	if (nouveau_bo_rd32(flip->disp->sync, flip->chan->addr / 4) ==
					      flip->chan->data)
		return true;
	usleep_range(1, 2);
	return false;
}

void
nv50_display_flip_stop(struct drm_crtc *crtc)
{
	struct nvif_device *device = &nouveau_drm(crtc->dev)->device;
	struct nv50_display_flip flip = {
		.disp = nv50_disp(crtc->dev),
		.chan = nv50_sync(crtc),
	};
	u32 *push;

	push = evo_wait(flip.chan, 8);
	if (push) {
		evo_mthd(push, 0x0084, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x0094, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x00c0, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x0080, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, flip.chan);
	}

	nvif_msec(device, 2000,
		if (nv50_display_flip_wait(&flip))
			break;
	);
}

int
nv50_display_flip_next(struct drm_crtc *crtc, struct drm_framebuffer *fb,
		       struct nouveau_channel *chan, u32 swap_interval)
{
	struct nouveau_framebuffer *nv_fb = nouveau_framebuffer(fb);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv50_head *head = nv50_head(crtc);
	struct nv50_sync *sync = nv50_sync(crtc);
	u32 *push;
	int ret;

	if (crtc->primary->fb->width != fb->width ||
	    crtc->primary->fb->height != fb->height)
		return -EINVAL;

	swap_interval <<= 4;
	if (swap_interval == 0)
		swap_interval |= 0x100;
	if (chan == NULL)
		evo_sync(crtc->dev);

	push = evo_wait(sync, 128);
	if (unlikely(push == NULL))
		return -EBUSY;

	if (chan && chan->user.oclass < G82_CHANNEL_GPFIFO) {
		ret = RING_SPACE(chan, 8);
		if (ret)
			return ret;

		BEGIN_NV04(chan, 0, NV11_SUBCHAN_DMA_SEMAPHORE, 2);
		OUT_RING  (chan, NvEvoSema0 + nv_crtc->index);
		OUT_RING  (chan, sync->addr ^ 0x10);
		BEGIN_NV04(chan, 0, NV11_SUBCHAN_SEMAPHORE_RELEASE, 1);
		OUT_RING  (chan, sync->data + 1);
		BEGIN_NV04(chan, 0, NV11_SUBCHAN_SEMAPHORE_OFFSET, 2);
		OUT_RING  (chan, sync->addr);
		OUT_RING  (chan, sync->data);
	} else
	if (chan && chan->user.oclass < FERMI_CHANNEL_GPFIFO) {
		u64 addr = nv84_fence_crtc(chan, nv_crtc->index) + sync->addr;
		ret = RING_SPACE(chan, 12);
		if (ret)
			return ret;

		BEGIN_NV04(chan, 0, NV11_SUBCHAN_DMA_SEMAPHORE, 1);
		OUT_RING  (chan, chan->vram.handle);
		BEGIN_NV04(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(addr ^ 0x10));
		OUT_RING  (chan, lower_32_bits(addr ^ 0x10));
		OUT_RING  (chan, sync->data + 1);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_WRITE_LONG);
		BEGIN_NV04(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(addr));
		OUT_RING  (chan, lower_32_bits(addr));
		OUT_RING  (chan, sync->data);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_EQUAL);
	} else
	if (chan) {
		u64 addr = nv84_fence_crtc(chan, nv_crtc->index) + sync->addr;
		ret = RING_SPACE(chan, 10);
		if (ret)
			return ret;

		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(addr ^ 0x10));
		OUT_RING  (chan, lower_32_bits(addr ^ 0x10));
		OUT_RING  (chan, sync->data + 1);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_WRITE_LONG |
				 NVC0_SUBCHAN_SEMAPHORE_TRIGGER_YIELD);
		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(addr));
		OUT_RING  (chan, lower_32_bits(addr));
		OUT_RING  (chan, sync->data);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_EQUAL |
				 NVC0_SUBCHAN_SEMAPHORE_TRIGGER_YIELD);
	}

	if (chan) {
		sync->addr ^= 0x10;
		sync->data++;
		FIRE_RING (chan);
	}

	/* queue the flip */
	evo_mthd(push, 0x0100, 1);
	evo_data(push, 0xfffe0000);
	evo_mthd(push, 0x0084, 1);
	evo_data(push, swap_interval);
	if (!(swap_interval & 0x00000100)) {
		evo_mthd(push, 0x00e0, 1);
		evo_data(push, 0x40000000);
	}
	evo_mthd(push, 0x0088, 4);
	evo_data(push, sync->addr);
	evo_data(push, sync->data++);
	evo_data(push, sync->data);
	evo_data(push, sync->base.sync.handle);
	evo_mthd(push, 0x00a0, 2);
	evo_data(push, 0x00000000);
	evo_data(push, 0x00000000);
	evo_mthd(push, 0x00c0, 1);
	evo_data(push, nv_fb->r_handle);
	evo_mthd(push, 0x0110, 2);
	evo_data(push, 0x00000000);
	evo_data(push, 0x00000000);
	if (nv50_vers(sync) < GF110_DISP_BASE_CHANNEL_DMA) {
		evo_mthd(push, 0x0800, 5);
		evo_data(push, nv_fb->nvbo->bo.offset >> 8);
		evo_data(push, 0);
		evo_data(push, (fb->height << 16) | fb->width);
		evo_data(push, nv_fb->r_pitch);
		evo_data(push, nv_fb->r_format);
	} else {
		evo_mthd(push, 0x0400, 5);
		evo_data(push, nv_fb->nvbo->bo.offset >> 8);
		evo_data(push, 0);
		evo_data(push, (fb->height << 16) | fb->width);
		evo_data(push, nv_fb->r_pitch);
		evo_data(push, nv_fb->r_format);
	}
	evo_mthd(push, 0x0080, 1);
	evo_data(push, 0x00000000);
	evo_kick(push, sync);

	nouveau_bo_ref(nv_fb->nvbo, &head->image);
	return 0;
}

/******************************************************************************
 * Head
 *****************************************************************************/
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
			evo_mthd(push, 0x0810 + (head->base.index * 0x400), 6);
			evo_data(push, 0x00000000);
			evo_data(push, (m->v.active  << 16) | m->h.active );
			evo_data(push, (m->v.synce   << 16) | m->h.synce  );
			evo_data(push, (m->v.blanke  << 16) | m->h.blanke );
			evo_data(push, (m->v.blanks  << 16) | m->h.blanks );
			evo_data(push, (m->v.blank2e << 16) | m->v.blank2s);
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
	m->v.blankus = (m->v.active - mode->vdisplay - 2) * m->h.active;
	m->v.blankus *= 1000;
	m->v.blankus /= mode->clock;

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
	struct nv50_head_atom *armh = &head->arm;
	struct nv50_head_atom *asyh = nv50_head_atom(state);

	NV_ATOMIC(drm, "%s atomic_check %d\n", crtc->name, asyh->state.active);
	asyh->clr.mask = 0;
	asyh->set.mask = 0;

	if (asyh->state.active) {
		if (asyh->state.mode_changed)
			nv50_head_atomic_check_mode(head, asyh);

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

	memcpy(armh, asyh, sizeof(*asyh));
	asyh->state.mode_changed = 0;
	return 0;
}

/******************************************************************************
 * CRTC
 *****************************************************************************/
static int
nv50_crtc_set_dither(struct nouveau_crtc *nv_crtc, bool update)
{
	struct nv50_mast *mast = nv50_mast(nv_crtc->base.dev);
	struct nouveau_connector *nv_connector;
	struct drm_connector *connector;
	u32 *push, mode = 0x00;

	nv_connector = nouveau_crtc_connector_get(nv_crtc);
	connector = &nv_connector->base;
	if (nv_connector->dithering_mode == DITHERING_MODE_AUTO) {
		if (nv_crtc->base.primary->fb->depth > connector->display_info.bpc * 3)
			mode = DITHERING_MODE_DYNAMIC2X2;
	} else {
		mode = nv_connector->dithering_mode;
	}

	if (nv_connector->dithering_depth == DITHERING_DEPTH_AUTO) {
		if (connector->display_info.bpc >= 8)
			mode |= DITHERING_DEPTH_8BPC;
	} else {
		mode |= nv_connector->dithering_depth;
	}

	push = evo_wait(mast, 4);
	if (push) {
		if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x08a0 + (nv_crtc->index * 0x0400), 1);
			evo_data(push, mode);
		} else
		if (nv50_vers(mast) < GK104_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0490 + (nv_crtc->index * 0x0300), 1);
			evo_data(push, mode);
		} else {
			evo_mthd(push, 0x04a0 + (nv_crtc->index * 0x0300), 1);
			evo_data(push, mode);
		}

		if (update) {
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, mast);
	}

	return 0;
}

static int
nv50_crtc_set_scale(struct nouveau_crtc *nv_crtc, bool update)
{
	struct nv50_head *head = nv50_head(&nv_crtc->base);
	struct nv50_head_atom *asyh = &head->asy;
	struct drm_crtc *crtc = &nv_crtc->base;
	struct nouveau_connector *nv_connector;
	struct nouveau_conn_atom asyc;

	nv_connector = nouveau_crtc_connector_get(nv_crtc);

	asyc.state.connector = &nv_connector->base;
	asyc.scaler.mode = nv_connector->scaling_mode;
	asyc.scaler.full = nv_connector->scaling_full;
	asyc.scaler.underscan.mode = nv_connector->underscan;
	asyc.scaler.underscan.hborder = nv_connector->underscan_hborder;
	asyc.scaler.underscan.vborder = nv_connector->underscan_vborder;
	nv50_head_atomic_check(&head->base.base, &asyh->state);
	nv50_head_atomic_check_view(&head->arm, asyh, &asyc);
	nv50_head_flush_set(head, asyh);

	if (update) {
		nv50_display_flip_stop(crtc);
		nv50_display_flip_next(crtc, crtc->primary->fb, NULL, 1);
	}

	return 0;
}

static int
nv50_crtc_set_raster_vblank_dmi(struct nouveau_crtc *nv_crtc, u32 usec)
{
	struct nv50_mast *mast = nv50_mast(nv_crtc->base.dev);
	u32 *push;

	push = evo_wait(mast, 8);
	if (!push)
		return -ENOMEM;

	evo_mthd(push, 0x0828 + (nv_crtc->index * 0x400), 1);
	evo_data(push, usec);
	evo_kick(push, mast);
	return 0;
}

static int
nv50_crtc_set_color_vibrance(struct nouveau_crtc *nv_crtc, bool update)
{
	struct nv50_mast *mast = nv50_mast(nv_crtc->base.dev);
	u32 *push, hue, vib;
	int adj;

	adj = (nv_crtc->color_vibrance > 0) ? 50 : 0;
	vib = ((nv_crtc->color_vibrance * 2047 + adj) / 100) & 0xfff;
	hue = ((nv_crtc->vibrant_hue * 2047) / 100) & 0xfff;

	push = evo_wait(mast, 16);
	if (push) {
		if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x08a8 + (nv_crtc->index * 0x400), 1);
			evo_data(push, (hue << 20) | (vib << 8));
		} else {
			evo_mthd(push, 0x0498 + (nv_crtc->index * 0x300), 1);
			evo_data(push, (hue << 20) | (vib << 8));
		}

		if (update) {
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, mast);
	}

	return 0;
}

static int
nv50_crtc_set_image(struct nouveau_crtc *nv_crtc, struct drm_framebuffer *fb,
		    int x, int y, bool update)
{
	struct nouveau_framebuffer *nvfb = nouveau_framebuffer(fb);
	struct nv50_head *head = nv50_head(&nv_crtc->base);
	struct nv50_head_atom *asyh = &head->asy;
	const struct drm_format_info *info;

	info = drm_format_info(nvfb->base.pixel_format);
	if (!info || !info->depth)
		return -EINVAL;

	asyh->base.depth = info->depth;
	asyh->base.cpp = info->cpp[0];
	asyh->base.x = x;
	asyh->base.y = y;
	asyh->base.w = nvfb->base.width;
	asyh->base.h = nvfb->base.height;
	nv50_head_atomic_check(&head->base.base, &asyh->state);
	nv50_head_flush_set(head, asyh);

	if (update) {
		struct nv50_mast *core = nv50_mast(nv_crtc->base.dev);
		u32 *push = evo_wait(core, 2);
		if (push) {
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
			evo_kick(push, core);
		}
	}

	nv_crtc->fb.handle = nvfb->r_handle;
	return 0;
}

static void
nv50_crtc_cursor_show(struct nouveau_crtc *nv_crtc)
{
	struct nv50_mast *mast = nv50_mast(nv_crtc->base.dev);
	struct nv50_head *head = nv50_head(&nv_crtc->base);
	struct nv50_head_atom *asyh = &head->asy;

	asyh->curs.visible = true;
	asyh->curs.handle = mast->base.vram.handle;
	asyh->curs.offset = nv_crtc->cursor.nvbo->bo.offset;
	asyh->curs.layout = 1;
	asyh->curs.format = 1;
	nv50_head_atomic_check(&head->base.base, &asyh->state);
	nv50_head_flush_set(head, asyh);
}

static void
nv50_crtc_cursor_hide(struct nouveau_crtc *nv_crtc)
{
	struct nv50_head *head = nv50_head(&nv_crtc->base);
	struct nv50_head_atom *asyh = &head->asy;

	asyh->curs.visible = false;
	nv50_head_atomic_check(&head->base.base, &asyh->state);
	nv50_head_flush_clr(head, asyh, false);
}

static void
nv50_crtc_cursor_show_hide(struct nouveau_crtc *nv_crtc, bool show, bool update)
{
	struct nv50_mast *mast = nv50_mast(nv_crtc->base.dev);

	if (show && nv_crtc->cursor.nvbo && nv_crtc->base.enabled)
		nv50_crtc_cursor_show(nv_crtc);
	else
		nv50_crtc_cursor_hide(nv_crtc);

	if (update) {
		u32 *push = evo_wait(mast, 2);
		if (push) {
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
			evo_kick(push, mast);
		}
	}
}

static void
nv50_crtc_dpms(struct drm_crtc *crtc, int mode)
{
}

static void
nv50_crtc_prepare(struct drm_crtc *crtc)
{
	struct nv50_head *head = nv50_head(crtc);
	struct nv50_head_atom *asyh = &head->asy;

	nv50_display_flip_stop(crtc);

	asyh->state.active = false;
	nv50_head_atomic_check(&head->base.base, &asyh->state);
	nv50_head_flush_clr(head, asyh, false);
}

static void
nv50_crtc_commit(struct drm_crtc *crtc)
{
	struct nv50_head *head = nv50_head(crtc);
	struct nv50_head_atom *asyh = &head->asy;

	asyh->state.active = true;
	nv50_head_atomic_check(&head->base.base, &asyh->state);
	nv50_head_flush_set(head, asyh);

	nv50_display_flip_next(crtc, crtc->primary->fb, NULL, 1);
}

static bool
nv50_crtc_mode_fixup(struct drm_crtc *crtc, const struct drm_display_mode *mode,
		     struct drm_display_mode *adjusted_mode)
{
	drm_mode_set_crtcinfo(adjusted_mode, CRTC_INTERLACE_HALVE_V);
	return true;
}

static int
nv50_crtc_swap_fbs(struct drm_crtc *crtc, struct drm_framebuffer *old_fb)
{
	struct nouveau_framebuffer *nvfb = nouveau_framebuffer(crtc->primary->fb);
	struct nv50_head *head = nv50_head(crtc);
	int ret;

	ret = nouveau_bo_pin(nvfb->nvbo, TTM_PL_FLAG_VRAM, true);
	if (ret == 0) {
		if (head->image)
			nouveau_bo_unpin(head->image);
		nouveau_bo_ref(nvfb->nvbo, &head->image);
	}

	return ret;
}

static int
nv50_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *umode,
		   struct drm_display_mode *mode, int x, int y,
		   struct drm_framebuffer *old_fb)
{
	struct nv50_mast *mast = nv50_mast(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nouveau_connector *nv_connector;
	int ret;
	struct nv50_head *head = nv50_head(crtc);
	struct nv50_head_atom *asyh = &head->asy;

	memcpy(&asyh->state.mode, umode, sizeof(*umode));
	memcpy(&asyh->state.adjusted_mode, mode, sizeof(*mode));
	asyh->state.active = true;
	asyh->state.mode_changed = true;
	nv50_head_atomic_check(&head->base.base, &asyh->state);

	ret = nv50_crtc_swap_fbs(crtc, old_fb);
	if (ret)
		return ret;

	nv50_head_flush_set(head, asyh);

	nv_connector = nouveau_crtc_connector_get(nv_crtc);
	nv50_crtc_set_dither(nv_crtc, false);
	nv50_crtc_set_scale(nv_crtc, false);

	/* G94 only accepts this after setting scale */
	if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA)
		nv50_crtc_set_raster_vblank_dmi(nv_crtc, asyh->mode.v.blankus);

	nv50_crtc_set_color_vibrance(nv_crtc, false);
	nv50_crtc_set_image(nv_crtc, crtc->primary->fb, x, y, false);
	return 0;
}

static int
nv50_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			struct drm_framebuffer *old_fb)
{
	struct nouveau_drm *drm = nouveau_drm(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	int ret;

	if (!crtc->primary->fb) {
		NV_DEBUG(drm, "No FB bound\n");
		return 0;
	}

	ret = nv50_crtc_swap_fbs(crtc, old_fb);
	if (ret)
		return ret;

	nv50_display_flip_stop(crtc);
	nv50_crtc_set_image(nv_crtc, crtc->primary->fb, x, y, true);
	nv50_display_flip_next(crtc, crtc->primary->fb, NULL, 1);
	return 0;
}

static int
nv50_crtc_mode_set_base_atomic(struct drm_crtc *crtc,
			       struct drm_framebuffer *fb, int x, int y,
			       enum mode_set_atomic state)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	nv50_display_flip_stop(crtc);
	nv50_crtc_set_image(nv_crtc, fb, x, y, true);
	return 0;
}

static void
nv50_crtc_lut_load(struct drm_crtc *crtc)
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

static void
nv50_crtc_disable(struct drm_crtc *crtc)
{
	struct nv50_head *head = nv50_head(crtc);
	evo_sync(crtc->dev);
	if (head->image)
		nouveau_bo_unpin(head->image);
	nouveau_bo_ref(NULL, &head->image);
}

static int
nv50_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
		     uint32_t handle, uint32_t width, uint32_t height)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_gem_object *gem = NULL;
	struct nouveau_bo *nvbo = NULL;
	int ret = 0;

	if (handle) {
		if (width != 64 || height != 64)
			return -EINVAL;

		gem = drm_gem_object_lookup(file_priv, handle);
		if (unlikely(!gem))
			return -ENOENT;
		nvbo = nouveau_gem_object(gem);

		ret = nouveau_bo_pin(nvbo, TTM_PL_FLAG_VRAM, true);
	}

	if (ret == 0) {
		if (nv_crtc->cursor.nvbo)
			nouveau_bo_unpin(nv_crtc->cursor.nvbo);
		nouveau_bo_ref(nvbo, &nv_crtc->cursor.nvbo);
	}
	drm_gem_object_unreference_unlocked(gem);

	nv50_crtc_cursor_show_hide(nv_crtc, true, true);
	return ret;
}

static int
nv50_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv50_curs *curs = nv50_curs(crtc);
	struct nv50_chan *chan = nv50_chan(curs);
	nvif_wr32(&chan->user, 0x0084, (y << 16) | (x & 0xffff));
	nvif_wr32(&chan->user, 0x0080, 0x00000000);

	nv_crtc->cursor_saved_x = x;
	nv_crtc->cursor_saved_y = y;
	return 0;
}

static int
nv50_crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
		    uint32_t size)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	u32 i;

	for (i = 0; i < size; i++) {
		nv_crtc->lut.r[i] = r[i];
		nv_crtc->lut.g[i] = g[i];
		nv_crtc->lut.b[i] = b[i];
	}

	nv50_crtc_lut_load(crtc);

	return 0;
}

static void
nv50_crtc_cursor_restore(struct nouveau_crtc *nv_crtc, int x, int y)
{
	nv50_crtc_cursor_move(&nv_crtc->base, x, y);

	nv50_crtc_cursor_show_hide(nv_crtc, true, true);
}

static void
nv50_crtc_destroy(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv50_disp *disp = nv50_disp(crtc->dev);
	struct nv50_head *head = nv50_head(crtc);
	struct nv50_fbdma *fbdma;

	list_for_each_entry(fbdma, &disp->fbdma, head) {
		nvif_object_fini(&fbdma->base[nv_crtc->index]);
	}

	nv50_dmac_destroy(&head->ovly.base, disp->disp);
	nv50_pioc_destroy(&head->oimm.base);
	nv50_dmac_destroy(&head->sync.base, disp->disp);
	nv50_pioc_destroy(&head->curs.base);

	/*XXX: this shouldn't be necessary, but the core doesn't call
	 *     disconnect() during the cleanup paths
	 */
	if (head->image)
		nouveau_bo_unpin(head->image);
	nouveau_bo_ref(NULL, &head->image);

	/*XXX: ditto */
	if (nv_crtc->cursor.nvbo)
		nouveau_bo_unpin(nv_crtc->cursor.nvbo);
	nouveau_bo_ref(NULL, &nv_crtc->cursor.nvbo);

	nouveau_bo_unmap(nv_crtc->lut.nvbo);
	if (nv_crtc->lut.nvbo)
		nouveau_bo_unpin(nv_crtc->lut.nvbo);
	nouveau_bo_ref(NULL, &nv_crtc->lut.nvbo);

	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static const struct drm_crtc_helper_funcs nv50_crtc_hfunc = {
	.dpms = nv50_crtc_dpms,
	.prepare = nv50_crtc_prepare,
	.commit = nv50_crtc_commit,
	.mode_fixup = nv50_crtc_mode_fixup,
	.mode_set = nv50_crtc_mode_set,
	.mode_set_base = nv50_crtc_mode_set_base,
	.mode_set_base_atomic = nv50_crtc_mode_set_base_atomic,
	.load_lut = nv50_crtc_lut_load,
	.disable = nv50_crtc_disable,
};

static const struct drm_crtc_funcs nv50_crtc_func = {
	.cursor_set = nv50_crtc_cursor_set,
	.cursor_move = nv50_crtc_cursor_move,
	.gamma_set = nv50_crtc_gamma_set,
	.set_config = nouveau_crtc_set_config,
	.destroy = nv50_crtc_destroy,
	.page_flip = nouveau_crtc_page_flip,
};

static int
nv50_crtc_create(struct drm_device *dev, int index)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvif_device *device = &drm->device;
	struct nv50_disp *disp = nv50_disp(dev);
	struct nv50_head *head;
	struct drm_crtc *crtc;
	int ret, i;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (!head)
		return -ENOMEM;

	head->base.index = index;
	head->base.color_vibrance = 50;
	head->base.vibrant_hue = 0;
	head->base.cursor.set_pos = nv50_crtc_cursor_restore;
	for (i = 0; i < 256; i++) {
		head->base.lut.r[i] = i << 8;
		head->base.lut.g[i] = i << 8;
		head->base.lut.b[i] = i << 8;
	}

	crtc = &head->base.base;
	drm_crtc_init(dev, crtc, &nv50_crtc_func);
	drm_crtc_helper_add(crtc, &nv50_crtc_hfunc);
	drm_mode_crtc_set_gamma_size(crtc, 256);

	ret = nouveau_bo_new(dev, 8192, 0x100, TTM_PL_FLAG_VRAM,
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

	/* allocate cursor resources */
	ret = nv50_curs_create(device, disp->disp, index, &head->curs);
	if (ret)
		goto out;

	/* allocate page flip / sync resources */
	ret = nv50_base_create(device, disp->disp, index, disp->sync->bo.offset,
			       &head->sync);
	if (ret)
		goto out;

	head->sync.addr = EVO_FLIP_SEM0(index);
	head->sync.data = 0x00000000;

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
		nv50_crtc_destroy(crtc);
	return ret;
}

/******************************************************************************
 * Encoder helpers
 *****************************************************************************/
static bool
nv50_encoder_mode_fixup(struct drm_encoder *encoder,
			const struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (nv_connector && nv_connector->native_mode) {
		nv_connector->scaling_full = false;
		if (nv_connector->scaling_mode == DRM_MODE_SCALE_NONE) {
			switch (nv_connector->type) {
			case DCB_CONNECTOR_LVDS:
			case DCB_CONNECTOR_LVDS_SPWG:
			case DCB_CONNECTOR_eDP:
				/* force use of scaler for non-edid modes */
				if (adjusted_mode->type & DRM_MODE_TYPE_DRIVER)
					return true;
				nv_connector->scaling_full = true;
				break;
			default:
				return true;
			}
		}

		drm_mode_copy(adjusted_mode, nv_connector->native_mode);
	}

	return true;
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
nv50_dac_commit(struct drm_encoder *encoder)
{
}

static void
nv50_dac_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode,
		  struct drm_display_mode *adjusted_mode)
{
	struct nv50_mast *mast = nv50_mast(encoder->dev);
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	u32 *push;

	nv50_dac_dpms(encoder, DRM_MODE_DPMS_ON);

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

static void
nv50_dac_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nv50_mast *mast = nv50_mast(encoder->dev);
	const int or = nv_encoder->or;
	u32 *push;

	if (nv_encoder->crtc) {
		nv50_crtc_prepare(nv_encoder->crtc);

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

static void
nv50_dac_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_helper_funcs nv50_dac_hfunc = {
	.dpms = nv50_dac_dpms,
	.mode_fixup = nv50_encoder_mode_fixup,
	.prepare = nv50_dac_disconnect,
	.commit = nv50_dac_commit,
	.mode_set = nv50_dac_mode_set,
	.disable = nv50_dac_disconnect,
	.get_crtc = nv50_display_crtc_get,
	.detect = nv50_dac_detect
};

static const struct drm_encoder_funcs nv50_dac_func = {
	.destroy = nv50_dac_destroy,
};

static int
nv50_dac_create(struct drm_connector *connector, struct dcb_output *dcbe)
{
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nvkm_i2c *i2c = nvxx_i2c(&drm->device);
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
	drm_encoder_helper_add(encoder, &nv50_dac_hfunc);

	drm_mode_connector_attach_encoder(connector, encoder);
	return 0;
}

/******************************************************************************
 * Audio
 *****************************************************************************/
static void
nv50_audio_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode)
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

static void
nv50_audio_disconnect(struct drm_encoder *encoder, struct nouveau_crtc *nv_crtc)
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

/******************************************************************************
 * HDMI
 *****************************************************************************/
static void
nv50_hdmi_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode)
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
	nv50_audio_mode_set(encoder, mode);
}

static void
nv50_hdmi_disconnect(struct drm_encoder *encoder, struct nouveau_crtc *nv_crtc)
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

/******************************************************************************
 * MST
 *****************************************************************************/
struct nv50_mstm {
	struct nouveau_encoder *outp;

	struct drm_dp_mst_topology_mgr mgr;
};

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

	if (dpcd[0] >= 0x12 && allow) {
		ret = drm_dp_dpcd_readb(mstm->mgr.aux, DP_MSTM_CAP, &dpcd[1]);
		if (ret < 0)
			return ret;

		state = dpcd[1] & DP_MST_CAP;
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
	int ret;

	if (!(mstm = *pmstm = kzalloc(sizeof(*mstm), GFP_KERNEL)))
		return -ENOMEM;
	mstm->outp = outp;

	ret = drm_dp_mst_topology_mgr_init(&mstm->mgr, dev->dev, aux, aux_max,
					   max_payloads, conn_base_id);
	if (ret)
		return ret;

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
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_sor_dp_pwr_v0 pwr;
	} link = {
		.base.version = 1,
		.base.method = NV50_DISP_MTHD_V1_SOR_DP_PWR,
		.base.hasht  = nv_encoder->dcb->hasht,
		.base.hashm  = nv_encoder->dcb->hashm,
		.pwr.state = mode == DRM_MODE_DPMS_ON,
	};
	struct drm_device *dev = encoder->dev;
	struct drm_encoder *partner;

	nv_encoder->last_dpms = mode;

	list_for_each_entry(partner, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_partner = nouveau_encoder(partner);

		if (partner->encoder_type != DRM_MODE_ENCODER_TMDS)
			continue;

		if (nv_partner != nv_encoder &&
		    nv_partner->dcb->or == nv_encoder->dcb->or) {
			if (nv_partner->last_dpms == DRM_MODE_DPMS_ON)
				return;
			break;
		}
	}

	if (nv_encoder->dcb->type == DCB_OUTPUT_DP) {
		args.pwr.state = 1;
		nvif_mthd(disp->disp, 0, &args, sizeof(args));
		nvif_mthd(disp->disp, 0, &link, sizeof(link));
	} else {
		nvif_mthd(disp->disp, 0, &args, sizeof(args));
	}
}

static void
nv50_sor_ctrl(struct nouveau_encoder *nv_encoder, u32 mask, u32 data)
{
	struct nv50_mast *mast = nv50_mast(nv_encoder->base.base.dev);
	u32 temp = (nv_encoder->ctrl & ~mask) | (data & mask), *push;
	if (temp != nv_encoder->ctrl && (push = evo_wait(mast, 2))) {
		if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0600 + (nv_encoder->or * 0x40), 1);
			evo_data(push, (nv_encoder->ctrl = temp));
		} else {
			evo_mthd(push, 0x0200 + (nv_encoder->or * 0x20), 1);
			evo_data(push, (nv_encoder->ctrl = temp));
		}
		evo_kick(push, mast);
	}
}

static void
nv50_sor_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(nv_encoder->crtc);

	nv_encoder->last_dpms = DRM_MODE_DPMS_OFF;
	nv_encoder->crtc = NULL;

	if (nv_crtc) {
		nv50_crtc_prepare(&nv_crtc->base);
		nv50_sor_ctrl(nv_encoder, 1 << nv_crtc->index, 0);
		nv50_audio_disconnect(encoder, nv_crtc);
		nv50_hdmi_disconnect(&nv_encoder->base.base, nv_crtc);
	}
}

static void
nv50_sor_commit(struct drm_encoder *encoder)
{
}

static void
nv50_sor_mode_set(struct drm_encoder *encoder, struct drm_display_mode *umode,
		  struct drm_display_mode *mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
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
	struct nv50_mast *mast = nv50_mast(encoder->dev);
	struct drm_device *dev = encoder->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_connector *nv_connector;
	struct nvbios *bios = &drm->vbios;
	u32 mask, ctrl;
	u8 owner = 1 << nv_crtc->index;
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

		nv50_hdmi_mode_set(&nv_encoder->base.base, mode);
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
		if (nv_connector->base.display_info.bpc == 6) {
			nv_encoder->dp.datarate = mode->clock * 18 / 8;
			depth = 0x2;
		} else
		if (nv_connector->base.display_info.bpc == 8) {
			nv_encoder->dp.datarate = mode->clock * 24 / 8;
			depth = 0x5;
		} else {
			nv_encoder->dp.datarate = mode->clock * 30 / 8;
			depth = 0x6;
		}

		if (nv_encoder->dcb->sorconf.link & 1)
			proto = 0x8;
		else
			proto = 0x9;
		nv50_audio_mode_set(encoder, mode);
		break;
	default:
		BUG_ON(1);
		break;
	}

	nv50_sor_dpms(&nv_encoder->base.base, DRM_MODE_DPMS_ON);

	if (nv50_vers(mast) >= GF110_DISP) {
		u32 *push = evo_wait(mast, 3);
		if (push) {
			u32 magic = 0x31ec6000 | (nv_crtc->index << 25);
			u32 syncs = 0x00000001;

			if (mode->flags & DRM_MODE_FLAG_NHSYNC)
				syncs |= 0x00000008;
			if (mode->flags & DRM_MODE_FLAG_NVSYNC)
				syncs |= 0x00000010;

			if (mode->flags & DRM_MODE_FLAG_INTERLACE)
				magic |= 0x00000001;

			evo_mthd(push, 0x0404 + (nv_crtc->index * 0x300), 2);
			evo_data(push, syncs | (depth << 6));
			evo_data(push, magic);
			evo_kick(push, mast);
		}

		ctrl = proto << 8;
		mask = 0x00000f00;
	} else {
		ctrl = (depth << 16) | (proto << 8);
		if (mode->flags & DRM_MODE_FLAG_NHSYNC)
			ctrl |= 0x00001000;
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			ctrl |= 0x00002000;
		mask = 0x000f3f00;
	}

	nv50_sor_ctrl(nv_encoder, mask | owner, ctrl | owner);
}

static void
nv50_sor_destroy(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	nv50_mstm_del(&nv_encoder->dp.mstm);
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_helper_funcs nv50_sor_hfunc = {
	.dpms = nv50_sor_dpms,
	.mode_fixup = nv50_encoder_mode_fixup,
	.prepare = nv50_sor_disconnect,
	.commit = nv50_sor_commit,
	.mode_set = nv50_sor_mode_set,
	.disable = nv50_sor_disconnect,
	.get_crtc = nv50_display_crtc_get,
};

static const struct drm_encoder_funcs nv50_sor_func = {
	.destroy = nv50_sor_destroy,
};

static int
nv50_sor_create(struct drm_connector *connector, struct dcb_output *dcbe)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nvkm_i2c *i2c = nvxx_i2c(&drm->device);
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
	nv_encoder->last_dpms = DRM_MODE_DPMS_OFF;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(connector->dev, encoder, &nv50_sor_func, type,
			 "sor-%04x-%04x", dcbe->hasht, dcbe->hashm);
	drm_encoder_helper_add(encoder, &nv50_sor_hfunc);

	drm_mode_connector_attach_encoder(connector, encoder);

	if (dcbe->type == DCB_OUTPUT_DP) {
		struct nvkm_i2c_aux *aux =
			nvkm_i2c_aux_find(i2c, dcbe->i2c_index);
		if (aux) {
			nv_encoder->i2c = &aux->i2c;
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

static bool
nv50_pior_mode_fixup(struct drm_encoder *encoder,
		     const struct drm_display_mode *mode,
		     struct drm_display_mode *adjusted_mode)
{
	if (!nv50_encoder_mode_fixup(encoder, mode, adjusted_mode))
		return false;
	adjusted_mode->clock *= 2;
	return true;
}

static void
nv50_pior_commit(struct drm_encoder *encoder)
{
}

static void
nv50_pior_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode,
		   struct drm_display_mode *adjusted_mode)
{
	struct nv50_mast *mast = nv50_mast(encoder->dev);
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector;
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
		BUG_ON(1);
		break;
	}

	nv50_pior_dpms(encoder, DRM_MODE_DPMS_ON);

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

static void
nv50_pior_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nv50_mast *mast = nv50_mast(encoder->dev);
	const int or = nv_encoder->or;
	u32 *push;

	if (nv_encoder->crtc) {
		nv50_crtc_prepare(nv_encoder->crtc);

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
nv50_pior_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_helper_funcs nv50_pior_hfunc = {
	.dpms = nv50_pior_dpms,
	.mode_fixup = nv50_pior_mode_fixup,
	.prepare = nv50_pior_disconnect,
	.commit = nv50_pior_commit,
	.mode_set = nv50_pior_mode_set,
	.disable = nv50_pior_disconnect,
	.get_crtc = nv50_display_crtc_get,
};

static const struct drm_encoder_funcs nv50_pior_func = {
	.destroy = nv50_pior_destroy,
};

static int
nv50_pior_create(struct drm_connector *connector, struct dcb_output *dcbe)
{
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nvkm_i2c *i2c = nvxx_i2c(&drm->device);
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
		ddc  = aux ? &aux->i2c : NULL;
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
	drm_encoder_helper_add(encoder, &nv50_pior_hfunc);

	drm_mode_connector_attach_encoder(connector, encoder);
	return 0;
}

/******************************************************************************
 * Framebuffer
 *****************************************************************************/

static void
nv50_fbdma_fini(struct nv50_fbdma *fbdma)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(fbdma->base); i++)
		nvif_object_fini(&fbdma->base[i]);
	nvif_object_fini(&fbdma->core);
	list_del(&fbdma->head);
	kfree(fbdma);
}

static int
nv50_fbdma_init(struct drm_device *dev, u32 name, u64 offset, u64 length, u8 kind)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv50_disp *disp = nv50_disp(dev);
	struct nv50_mast *mast = nv50_mast(dev);
	struct __attribute__ ((packed)) {
		struct nv_dma_v0 base;
		union {
			struct nv50_dma_v0 nv50;
			struct gf100_dma_v0 gf100;
			struct gf119_dma_v0 gf119;
		};
	} args = {};
	struct nv50_fbdma *fbdma;
	struct drm_crtc *crtc;
	u32 size = sizeof(args.base);
	int ret;

	list_for_each_entry(fbdma, &disp->fbdma, head) {
		if (fbdma->core.handle == name)
			return 0;
	}

	fbdma = kzalloc(sizeof(*fbdma), GFP_KERNEL);
	if (!fbdma)
		return -ENOMEM;
	list_add(&fbdma->head, &disp->fbdma);

	args.base.target = NV_DMA_V0_TARGET_VRAM;
	args.base.access = NV_DMA_V0_ACCESS_RDWR;
	args.base.start = offset;
	args.base.limit = offset + length - 1;

	if (drm->device.info.chipset < 0x80) {
		args.nv50.part = NV50_DMA_V0_PART_256;
		size += sizeof(args.nv50);
	} else
	if (drm->device.info.chipset < 0xc0) {
		args.nv50.part = NV50_DMA_V0_PART_256;
		args.nv50.kind = kind;
		size += sizeof(args.nv50);
	} else
	if (drm->device.info.chipset < 0xd0) {
		args.gf100.kind = kind;
		size += sizeof(args.gf100);
	} else {
		args.gf119.page = GF119_DMA_V0_PAGE_LP;
		args.gf119.kind = kind;
		size += sizeof(args.gf119);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nv50_head *head = nv50_head(crtc);
		int ret = nvif_object_init(&head->sync.base.base.user, name,
					   NV_DMA_IN_MEMORY, &args, size,
					   &fbdma->base[head->base.index]);
		if (ret) {
			nv50_fbdma_fini(fbdma);
			return ret;
		}
	}

	ret = nvif_object_init(&mast->base.base.user, name, NV_DMA_IN_MEMORY,
			       &args, size, &fbdma->core);
	if (ret) {
		nv50_fbdma_fini(fbdma);
		return ret;
	}

	return 0;
}

static void
nv50_fb_dtor(struct drm_framebuffer *fb)
{
}

static int
nv50_fb_ctor(struct drm_framebuffer *fb)
{
	struct nouveau_framebuffer *nv_fb = nouveau_framebuffer(fb);
	struct nouveau_drm *drm = nouveau_drm(fb->dev);
	struct nouveau_bo *nvbo = nv_fb->nvbo;
	struct nv50_disp *disp = nv50_disp(fb->dev);
	u8 kind = nouveau_bo_tile_layout(nvbo) >> 8;
	u8 tile = nvbo->tile_mode;

	if (drm->device.info.chipset >= 0xc0)
		tile >>= 4; /* yep.. */

	switch (fb->depth) {
	case  8: nv_fb->r_format = 0x1e00; break;
	case 15: nv_fb->r_format = 0xe900; break;
	case 16: nv_fb->r_format = 0xe800; break;
	case 24:
	case 32: nv_fb->r_format = 0xcf00; break;
	case 30: nv_fb->r_format = 0xd100; break;
	default:
		 NV_ERROR(drm, "unknown depth %d\n", fb->depth);
		 return -EINVAL;
	}

	if (disp->disp->oclass < G82_DISP) {
		nv_fb->r_pitch   = kind ? (((fb->pitches[0] / 4) << 4) | tile) :
					    (fb->pitches[0] | 0x00100000);
		nv_fb->r_format |= kind << 16;
	} else
	if (disp->disp->oclass < GF110_DISP) {
		nv_fb->r_pitch  = kind ? (((fb->pitches[0] / 4) << 4) | tile) :
					   (fb->pitches[0] | 0x00100000);
	} else {
		nv_fb->r_pitch  = kind ? (((fb->pitches[0] / 4) << 4) | tile) :
					   (fb->pitches[0] | 0x01000000);
	}
	nv_fb->r_handle = 0xffff0000 | kind;

	return nv50_fbdma_init(fb->dev, nv_fb->r_handle, 0,
			       drm->device.info.ram_user, kind);
}

/******************************************************************************
 * Init
 *****************************************************************************/

void
nv50_display_fini(struct drm_device *dev)
{
}

int
nv50_display_init(struct drm_device *dev)
{
	struct nv50_disp *disp = nv50_disp(dev);
	struct drm_crtc *crtc;
	u32 *push;

	push = evo_wait(nv50_mast(dev), 32);
	if (!push)
		return -EBUSY;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nv50_sync *sync = nv50_sync(crtc);

		nv50_crtc_lut_load(crtc);
		nouveau_bo_wr32(disp->sync, sync->addr / 4, sync->data);
	}

	evo_mthd(push, 0x0088, 1);
	evo_data(push, nv50_mast(dev)->base.sync.handle);
	evo_kick(push, nv50_mast(dev));
	return 0;
}

void
nv50_display_destroy(struct drm_device *dev)
{
	struct nv50_disp *disp = nv50_disp(dev);
	struct nv50_fbdma *fbdma, *fbtmp;

	list_for_each_entry_safe(fbdma, fbtmp, &disp->fbdma, head) {
		nv50_fbdma_fini(fbdma);
	}

	nv50_dmac_destroy(&disp->mast.base, disp->disp);

	nouveau_bo_unmap(disp->sync);
	if (disp->sync)
		nouveau_bo_unpin(disp->sync);
	nouveau_bo_ref(NULL, &disp->sync);

	nouveau_display(dev)->priv = NULL;
	kfree(disp);
}

int
nv50_display_create(struct drm_device *dev)
{
	struct nvif_device *device = &nouveau_drm(dev)->device;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct dcb_table *dcb = &drm->vbios.dcb;
	struct drm_connector *connector, *tmp;
	struct nv50_disp *disp;
	struct dcb_output *dcbe;
	int crtcs, ret, i;

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;
	INIT_LIST_HEAD(&disp->fbdma);

	nouveau_display(dev)->priv = disp;
	nouveau_display(dev)->dtor = nv50_display_destroy;
	nouveau_display(dev)->init = nv50_display_init;
	nouveau_display(dev)->fini = nv50_display_fini;
	nouveau_display(dev)->fb_ctor = nv50_fb_ctor;
	nouveau_display(dev)->fb_dtor = nv50_fb_dtor;
	disp->disp = &nouveau_display(dev)->disp;

	/* small shared memory area we use for notifiers and semaphores */
	ret = nouveau_bo_new(dev, 4096, 0x1000, TTM_PL_FLAG_VRAM,
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
		ret = nv50_crtc_create(dev, i);
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
