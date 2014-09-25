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
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>

#include <nvif/class.h>

#include "nouveau_drm.h"
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
 * EVO channel
 *****************************************************************************/

struct nv50_chan {
	struct nvif_object user;
};

static int
nv50_chan_create(struct nvif_object *disp, const u32 *oclass, u8 head,
		 void *data, u32 size, struct nv50_chan *chan)
{
	while (oclass[0]) {
		int ret = nvif_object_init(disp, NULL, (oclass[0] << 16) | head,
					   oclass[0], data, size,
					  &chan->user);
		if (oclass++, ret == 0) {
			nvif_object_map(&chan->user);
			return ret;
		}
	}
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
nv50_pioc_create(struct nvif_object *disp, const u32 *oclass, u8 head,
		 void *data, u32 size, struct nv50_pioc *pioc)
{
	return nv50_chan_create(disp, oclass, head, data, size, &pioc->base);
}

/******************************************************************************
 * Cursor Immediate
 *****************************************************************************/

struct nv50_curs {
	struct nv50_pioc base;
};

static int
nv50_curs_create(struct nvif_object *disp, int head, struct nv50_curs *curs)
{
	struct nv50_disp_cursor_v0 args = {
		.head = head,
	};
	static const u32 oclass[] = {
		GK104_DISP_CURSOR,
		GF110_DISP_CURSOR,
		GT214_DISP_CURSOR,
		G82_DISP_CURSOR,
		NV50_DISP_CURSOR,
		0
	};

	return nv50_pioc_create(disp, oclass, head, &args, sizeof(args),
			       &curs->base);
}

/******************************************************************************
 * Overlay Immediate
 *****************************************************************************/

struct nv50_oimm {
	struct nv50_pioc base;
};

static int
nv50_oimm_create(struct nvif_object *disp, int head, struct nv50_oimm *oimm)
{
	struct nv50_disp_cursor_v0 args = {
		.head = head,
	};
	static const u32 oclass[] = {
		GK104_DISP_OVERLAY,
		GF110_DISP_OVERLAY,
		GT214_DISP_OVERLAY,
		G82_DISP_OVERLAY,
		NV50_DISP_OVERLAY,
		0
	};

	return nv50_pioc_create(disp, oclass, head, &args, sizeof(args),
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
	nvif_object_fini(&dmac->vram);
	nvif_object_fini(&dmac->sync);

	nv50_chan_destroy(&dmac->base);

	if (dmac->ptr) {
		struct pci_dev *pdev = nvkm_device(nvif_device(disp))->pdev;
		pci_free_consistent(pdev, PAGE_SIZE, dmac->ptr, dmac->handle);
	}
}

static int
nv50_dmac_create(struct nvif_object *disp, const u32 *oclass, u8 head,
		 void *data, u32 size, u64 syncbuf,
		 struct nv50_dmac *dmac)
{
	struct nvif_device *device = nvif_device(disp);
	struct nv50_disp_core_channel_dma_v0 *args = data;
	struct nvif_object pushbuf;
	int ret;

	mutex_init(&dmac->lock);

	dmac->ptr = pci_alloc_consistent(nvkm_device(device)->pdev,
					 PAGE_SIZE, &dmac->handle);
	if (!dmac->ptr)
		return -ENOMEM;

	ret = nvif_object_init(nvif_object(device), NULL,
			       args->pushbuf, NV_DMA_FROM_MEMORY,
			       &(struct nv_dma_v0) {
					.target = NV_DMA_V0_TARGET_PCI_US,
					.access = NV_DMA_V0_ACCESS_RD,
					.start = dmac->handle + 0x0000,
					.limit = dmac->handle + 0x0fff,
			       }, sizeof(struct nv_dma_v0), &pushbuf);
	if (ret)
		return ret;

	ret = nv50_chan_create(disp, oclass, head, data, size, &dmac->base);
	nvif_object_fini(&pushbuf);
	if (ret)
		return ret;

	ret = nvif_object_init(&dmac->base.user, NULL, 0xf0000000,
			       NV_DMA_IN_MEMORY,
			       &(struct nv_dma_v0) {
					.target = NV_DMA_V0_TARGET_VRAM,
					.access = NV_DMA_V0_ACCESS_RDWR,
					.start = syncbuf + 0x0000,
					.limit = syncbuf + 0x0fff,
			       }, sizeof(struct nv_dma_v0),
			       &dmac->sync);
	if (ret)
		return ret;

	ret = nvif_object_init(&dmac->base.user, NULL, 0xf0000001,
			       NV_DMA_IN_MEMORY,
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
nv50_core_create(struct nvif_object *disp, u64 syncbuf, struct nv50_mast *core)
{
	struct nv50_disp_core_channel_dma_v0 args = {
		.pushbuf = 0xb0007d00,
	};
	static const u32 oclass[] = {
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

	return nv50_dmac_create(disp, oclass, 0, &args, sizeof(args), syncbuf,
			       &core->base);
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
nv50_base_create(struct nvif_object *disp, int head, u64 syncbuf,
		 struct nv50_sync *base)
{
	struct nv50_disp_base_channel_dma_v0 args = {
		.pushbuf = 0xb0007c00 | head,
		.head = head,
	};
	static const u32 oclass[] = {
		GK110_DISP_BASE_CHANNEL_DMA,
		GK104_DISP_BASE_CHANNEL_DMA,
		GF110_DISP_BASE_CHANNEL_DMA,
		GT214_DISP_BASE_CHANNEL_DMA,
		GT200_DISP_BASE_CHANNEL_DMA,
		G82_DISP_BASE_CHANNEL_DMA,
		NV50_DISP_BASE_CHANNEL_DMA,
		0
	};

	return nv50_dmac_create(disp, oclass, head, &args, sizeof(args),
				syncbuf, &base->base);
}

/******************************************************************************
 * Overlay
 *****************************************************************************/

struct nv50_ovly {
	struct nv50_dmac base;
};

static int
nv50_ovly_create(struct nvif_object *disp, int head, u64 syncbuf,
		 struct nv50_ovly *ovly)
{
	struct nv50_disp_overlay_channel_dma_v0 args = {
		.pushbuf = 0xb0007e00 | head,
		.head = head,
	};
	static const u32 oclass[] = {
		GK104_DISP_OVERLAY_CONTROL_DMA,
		GF110_DISP_OVERLAY_CONTROL_DMA,
		GT214_DISP_OVERLAY_CHANNEL_DMA,
		GT200_DISP_OVERLAY_CHANNEL_DMA,
		G82_DISP_OVERLAY_CHANNEL_DMA,
		NV50_DISP_OVERLAY_CHANNEL_DMA,
		0
	};

	return nv50_dmac_create(disp, oclass, head, &args, sizeof(args),
				syncbuf, &ovly->base);
}

struct nv50_head {
	struct nouveau_crtc base;
	struct nouveau_bo *image;
	struct nv50_curs curs;
	struct nv50_sync sync;
	struct nv50_ovly ovly;
	struct nv50_oimm oimm;
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
	u32 put = nvif_rd32(&dmac->base.user, 0x0000) / 4;

	mutex_lock(&dmac->lock);
	if (put + nr >= (PAGE_SIZE / 4) - 8) {
		dmac->ptr[put] = 0x20000000;

		nvif_wr32(&dmac->base.user, 0x0000, 0x00000000);
		if (!nvkm_wait(&dmac->base.user, 0x0004, ~0, 0x00000000)) {
			mutex_unlock(&dmac->lock);
			nv_error(nvkm_object(&dmac->base.user), "channel stalled\n");
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

#define evo_mthd(p,m,s) *((p)++) = (((s) << 18) | (m))
#define evo_data(p,d)   *((p)++) = (d)

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
		if (nv_wait_cb(nvkm_device(device), evo_sync_wait, disp->sync))
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

	nv_wait_cb(nvkm_device(device), nv50_display_flip_wait, &flip);
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

	swap_interval <<= 4;
	if (swap_interval == 0)
		swap_interval |= 0x100;
	if (chan == NULL)
		evo_sync(crtc->dev);

	push = evo_wait(sync, 128);
	if (unlikely(push == NULL))
		return -EBUSY;

	if (chan && chan->object->oclass < G82_CHANNEL_GPFIFO) {
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
	if (chan && chan->object->oclass < FERMI_CHANNEL_GPFIFO) {
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
	struct nv50_mast *mast = nv50_mast(nv_crtc->base.dev);
	struct drm_display_mode *omode, *umode = &nv_crtc->base.mode;
	struct drm_crtc *crtc = &nv_crtc->base;
	struct nouveau_connector *nv_connector;
	int mode = DRM_MODE_SCALE_NONE;
	u32 oX, oY, *push;

	/* start off at the resolution we programmed the crtc for, this
	 * effectively handles NONE/FULL scaling
	 */
	nv_connector = nouveau_crtc_connector_get(nv_crtc);
	if (nv_connector && nv_connector->native_mode)
		mode = nv_connector->scaling_mode;

	if (mode != DRM_MODE_SCALE_NONE)
		omode = nv_connector->native_mode;
	else
		omode = umode;

	oX = omode->hdisplay;
	oY = omode->vdisplay;
	if (omode->flags & DRM_MODE_FLAG_DBLSCAN)
		oY *= 2;

	/* add overscan compensation if necessary, will keep the aspect
	 * ratio the same as the backend mode unless overridden by the
	 * user setting both hborder and vborder properties.
	 */
	if (nv_connector && ( nv_connector->underscan == UNDERSCAN_ON ||
			     (nv_connector->underscan == UNDERSCAN_AUTO &&
			      nv_connector->edid &&
			      drm_detect_hdmi_monitor(nv_connector->edid)))) {
		u32 bX = nv_connector->underscan_hborder;
		u32 bY = nv_connector->underscan_vborder;
		u32 aspect = (oY << 19) / oX;

		if (bX) {
			oX -= (bX * 2);
			if (bY) oY -= (bY * 2);
			else    oY  = ((oX * aspect) + (aspect / 2)) >> 19;
		} else {
			oX -= (oX >> 4) + 32;
			if (bY) oY -= (bY * 2);
			else    oY  = ((oX * aspect) + (aspect / 2)) >> 19;
		}
	}

	/* handle CENTER/ASPECT scaling, taking into account the areas
	 * removed already for overscan compensation
	 */
	switch (mode) {
	case DRM_MODE_SCALE_CENTER:
		oX = min((u32)umode->hdisplay, oX);
		oY = min((u32)umode->vdisplay, oY);
		/* fall-through */
	case DRM_MODE_SCALE_ASPECT:
		if (oY < oX) {
			u32 aspect = (umode->hdisplay << 19) / umode->vdisplay;
			oX = ((oY * aspect) + (aspect / 2)) >> 19;
		} else {
			u32 aspect = (umode->vdisplay << 19) / umode->hdisplay;
			oY = ((oX * aspect) + (aspect / 2)) >> 19;
		}
		break;
	default:
		break;
	}

	push = evo_wait(mast, 8);
	if (push) {
		if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
			/*XXX: SCALE_CTRL_ACTIVE??? */
			evo_mthd(push, 0x08d8 + (nv_crtc->index * 0x400), 2);
			evo_data(push, (oY << 16) | oX);
			evo_data(push, (oY << 16) | oX);
			evo_mthd(push, 0x08a4 + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x08c8 + (nv_crtc->index * 0x400), 1);
			evo_data(push, umode->vdisplay << 16 | umode->hdisplay);
		} else {
			evo_mthd(push, 0x04c0 + (nv_crtc->index * 0x300), 3);
			evo_data(push, (oY << 16) | oX);
			evo_data(push, (oY << 16) | oX);
			evo_data(push, (oY << 16) | oX);
			evo_mthd(push, 0x0494 + (nv_crtc->index * 0x300), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x04b8 + (nv_crtc->index * 0x300), 1);
			evo_data(push, umode->vdisplay << 16 | umode->hdisplay);
		}

		evo_kick(push, mast);

		if (update) {
			nv50_display_flip_stop(crtc);
			nv50_display_flip_next(crtc, crtc->primary->fb,
					       NULL, 1);
		}
	}

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
	struct nv50_mast *mast = nv50_mast(nv_crtc->base.dev);
	u32 *push;

	push = evo_wait(mast, 16);
	if (push) {
		if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0860 + (nv_crtc->index * 0x400), 1);
			evo_data(push, nvfb->nvbo->bo.offset >> 8);
			evo_mthd(push, 0x0868 + (nv_crtc->index * 0x400), 3);
			evo_data(push, (fb->height << 16) | fb->width);
			evo_data(push, nvfb->r_pitch);
			evo_data(push, nvfb->r_format);
			evo_mthd(push, 0x08c0 + (nv_crtc->index * 0x400), 1);
			evo_data(push, (y << 16) | x);
			if (nv50_vers(mast) > NV50_DISP_CORE_CHANNEL_DMA) {
				evo_mthd(push, 0x0874 + (nv_crtc->index * 0x400), 1);
				evo_data(push, nvfb->r_handle);
			}
		} else {
			evo_mthd(push, 0x0460 + (nv_crtc->index * 0x300), 1);
			evo_data(push, nvfb->nvbo->bo.offset >> 8);
			evo_mthd(push, 0x0468 + (nv_crtc->index * 0x300), 4);
			evo_data(push, (fb->height << 16) | fb->width);
			evo_data(push, nvfb->r_pitch);
			evo_data(push, nvfb->r_format);
			evo_data(push, nvfb->r_handle);
			evo_mthd(push, 0x04b0 + (nv_crtc->index * 0x300), 1);
			evo_data(push, (y << 16) | x);
		}

		if (update) {
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, mast);
	}

	nv_crtc->fb.handle = nvfb->r_handle;
	return 0;
}

static void
nv50_crtc_cursor_show(struct nouveau_crtc *nv_crtc)
{
	struct nv50_mast *mast = nv50_mast(nv_crtc->base.dev);
	u32 *push = evo_wait(mast, 16);
	if (push) {
		if (nv50_vers(mast) < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + (nv_crtc->index * 0x400), 2);
			evo_data(push, 0x85000000);
			evo_data(push, nv_crtc->cursor.nvbo->bo.offset >> 8);
		} else
		if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + (nv_crtc->index * 0x400), 2);
			evo_data(push, 0x85000000);
			evo_data(push, nv_crtc->cursor.nvbo->bo.offset >> 8);
			evo_mthd(push, 0x089c + (nv_crtc->index * 0x400), 1);
			evo_data(push, mast->base.vram.handle);
		} else {
			evo_mthd(push, 0x0480 + (nv_crtc->index * 0x300), 2);
			evo_data(push, 0x85000000);
			evo_data(push, nv_crtc->cursor.nvbo->bo.offset >> 8);
			evo_mthd(push, 0x048c + (nv_crtc->index * 0x300), 1);
			evo_data(push, mast->base.vram.handle);
		}
		evo_kick(push, mast);
	}
}

static void
nv50_crtc_cursor_hide(struct nouveau_crtc *nv_crtc)
{
	struct nv50_mast *mast = nv50_mast(nv_crtc->base.dev);
	u32 *push = evo_wait(mast, 16);
	if (push) {
		if (nv50_vers(mast) < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x05000000);
		} else
		if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x05000000);
			evo_mthd(push, 0x089c + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x00000000);
		} else {
			evo_mthd(push, 0x0480 + (nv_crtc->index * 0x300), 1);
			evo_data(push, 0x05000000);
			evo_mthd(push, 0x048c + (nv_crtc->index * 0x300), 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, mast);
	}
}

static void
nv50_crtc_cursor_show_hide(struct nouveau_crtc *nv_crtc, bool show, bool update)
{
	struct nv50_mast *mast = nv50_mast(nv_crtc->base.dev);

	if (show)
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
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv50_mast *mast = nv50_mast(crtc->dev);
	u32 *push;

	nv50_display_flip_stop(crtc);

	push = evo_wait(mast, 6);
	if (push) {
		if (nv50_vers(mast) < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0874 + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x0840 + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x40000000);
		} else
		if (nv50_vers(mast) <  GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0874 + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x0840 + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x40000000);
			evo_mthd(push, 0x085c + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x00000000);
		} else {
			evo_mthd(push, 0x0474 + (nv_crtc->index * 0x300), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x0440 + (nv_crtc->index * 0x300), 1);
			evo_data(push, 0x03000000);
			evo_mthd(push, 0x045c + (nv_crtc->index * 0x300), 1);
			evo_data(push, 0x00000000);
		}

		evo_kick(push, mast);
	}

	nv50_crtc_cursor_show_hide(nv_crtc, false, false);
}

static void
nv50_crtc_commit(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv50_mast *mast = nv50_mast(crtc->dev);
	u32 *push;

	push = evo_wait(mast, 32);
	if (push) {
		if (nv50_vers(mast) < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0874 + (nv_crtc->index * 0x400), 1);
			evo_data(push, nv_crtc->fb.handle);
			evo_mthd(push, 0x0840 + (nv_crtc->index * 0x400), 2);
			evo_data(push, 0xc0000000);
			evo_data(push, nv_crtc->lut.nvbo->bo.offset >> 8);
		} else
		if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0874 + (nv_crtc->index * 0x400), 1);
			evo_data(push, nv_crtc->fb.handle);
			evo_mthd(push, 0x0840 + (nv_crtc->index * 0x400), 2);
			evo_data(push, 0xc0000000);
			evo_data(push, nv_crtc->lut.nvbo->bo.offset >> 8);
			evo_mthd(push, 0x085c + (nv_crtc->index * 0x400), 1);
			evo_data(push, mast->base.vram.handle);
		} else {
			evo_mthd(push, 0x0474 + (nv_crtc->index * 0x300), 1);
			evo_data(push, nv_crtc->fb.handle);
			evo_mthd(push, 0x0440 + (nv_crtc->index * 0x300), 4);
			evo_data(push, 0x83000000);
			evo_data(push, nv_crtc->lut.nvbo->bo.offset >> 8);
			evo_data(push, 0x00000000);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x045c + (nv_crtc->index * 0x300), 1);
			evo_data(push, mast->base.vram.handle);
			evo_mthd(push, 0x0430 + (nv_crtc->index * 0x300), 1);
			evo_data(push, 0xffffff00);
		}

		evo_kick(push, mast);
	}

	nv50_crtc_cursor_show_hide(nv_crtc, nv_crtc->cursor.visible, true);
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

	ret = nouveau_bo_pin(nvfb->nvbo, TTM_PL_FLAG_VRAM);
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
	u32 ilace = (mode->flags & DRM_MODE_FLAG_INTERLACE) ? 2 : 1;
	u32 vscan = (mode->flags & DRM_MODE_FLAG_DBLSCAN) ? 2 : 1;
	u32 hactive, hsynce, hbackp, hfrontp, hblanke, hblanks;
	u32 vactive, vsynce, vbackp, vfrontp, vblanke, vblanks;
	u32 vblan2e = 0, vblan2s = 1;
	u32 *push;
	int ret;

	hactive = mode->htotal;
	hsynce  = mode->hsync_end - mode->hsync_start - 1;
	hbackp  = mode->htotal - mode->hsync_end;
	hblanke = hsynce + hbackp;
	hfrontp = mode->hsync_start - mode->hdisplay;
	hblanks = mode->htotal - hfrontp - 1;

	vactive = mode->vtotal * vscan / ilace;
	vsynce  = ((mode->vsync_end - mode->vsync_start) * vscan / ilace) - 1;
	vbackp  = (mode->vtotal - mode->vsync_end) * vscan / ilace;
	vblanke = vsynce + vbackp;
	vfrontp = (mode->vsync_start - mode->vdisplay) * vscan / ilace;
	vblanks = vactive - vfrontp - 1;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		vblan2e = vactive + vsynce + vbackp;
		vblan2s = vblan2e + (mode->vdisplay * vscan / ilace);
		vactive = (vactive * 2) + 1;
	}

	ret = nv50_crtc_swap_fbs(crtc, old_fb);
	if (ret)
		return ret;

	push = evo_wait(mast, 64);
	if (push) {
		if (nv50_vers(mast) < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0804 + (nv_crtc->index * 0x400), 2);
			evo_data(push, 0x00800000 | mode->clock);
			evo_data(push, (ilace == 2) ? 2 : 0);
			evo_mthd(push, 0x0810 + (nv_crtc->index * 0x400), 6);
			evo_data(push, 0x00000000);
			evo_data(push, (vactive << 16) | hactive);
			evo_data(push, ( vsynce << 16) | hsynce);
			evo_data(push, (vblanke << 16) | hblanke);
			evo_data(push, (vblanks << 16) | hblanks);
			evo_data(push, (vblan2e << 16) | vblan2s);
			evo_mthd(push, 0x082c + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x0900 + (nv_crtc->index * 0x400), 2);
			evo_data(push, 0x00000311);
			evo_data(push, 0x00000100);
		} else {
			evo_mthd(push, 0x0410 + (nv_crtc->index * 0x300), 6);
			evo_data(push, 0x00000000);
			evo_data(push, (vactive << 16) | hactive);
			evo_data(push, ( vsynce << 16) | hsynce);
			evo_data(push, (vblanke << 16) | hblanke);
			evo_data(push, (vblanks << 16) | hblanks);
			evo_data(push, (vblan2e << 16) | vblan2s);
			evo_mthd(push, 0x042c + (nv_crtc->index * 0x300), 1);
			evo_data(push, 0x00000000); /* ??? */
			evo_mthd(push, 0x0450 + (nv_crtc->index * 0x300), 3);
			evo_data(push, mode->clock * 1000);
			evo_data(push, 0x00200000); /* ??? */
			evo_data(push, mode->clock * 1000);
			evo_mthd(push, 0x04d0 + (nv_crtc->index * 0x300), 2);
			evo_data(push, 0x00000311);
			evo_data(push, 0x00000100);
		}

		evo_kick(push, mast);
	}

	nv_connector = nouveau_crtc_connector_get(nv_crtc);
	nv50_crtc_set_dither(nv_crtc, false);
	nv50_crtc_set_scale(nv_crtc, false);
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
	struct drm_device *dev = crtc->dev;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	bool visible = (handle != 0);
	int i, ret = 0;

	if (visible) {
		if (width != 64 || height != 64)
			return -EINVAL;

		gem = drm_gem_object_lookup(dev, file_priv, handle);
		if (unlikely(!gem))
			return -ENOENT;
		nvbo = nouveau_gem_object(gem);

		ret = nouveau_bo_map(nvbo);
		if (ret == 0) {
			for (i = 0; i < 64 * 64; i++) {
				u32 v = nouveau_bo_rd32(nvbo, i);
				nouveau_bo_wr32(nv_crtc->cursor.nvbo, i, v);
			}
			nouveau_bo_unmap(nvbo);
		}

		drm_gem_object_unreference_unlocked(gem);
	}

	if (visible != nv_crtc->cursor.visible) {
		nv50_crtc_cursor_show_hide(nv_crtc, visible, true);
		nv_crtc->cursor.visible = visible;
	}

	return ret;
}

static int
nv50_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct nv50_curs *curs = nv50_curs(crtc);
	struct nv50_chan *chan = nv50_chan(curs);
	nvif_wr32(&chan->user, 0x0084, (y << 16) | (x & 0xffff));
	nvif_wr32(&chan->user, 0x0080, 0x00000000);
	return 0;
}

static void
nv50_crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
		    uint32_t start, uint32_t size)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	u32 end = min_t(u32, start + size, 256);
	u32 i;

	for (i = start; i < end; i++) {
		nv_crtc->lut.r[i] = r[i];
		nv_crtc->lut.g[i] = g[i];
		nv_crtc->lut.b[i] = b[i];
	}

	nv50_crtc_lut_load(crtc);
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

	nouveau_bo_unmap(nv_crtc->cursor.nvbo);
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

static void
nv50_cursor_set_pos(struct nouveau_crtc *nv_crtc, int x, int y)
{
}

static void
nv50_cursor_set_offset(struct nouveau_crtc *nv_crtc, uint32_t offset)
{
}

static int
nv50_crtc_create(struct drm_device *dev, int index)
{
	struct nv50_disp *disp = nv50_disp(dev);
	struct nv50_head *head;
	struct drm_crtc *crtc;
	int ret, i;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (!head)
		return -ENOMEM;

	head->base.index = index;
	head->base.set_dither = nv50_crtc_set_dither;
	head->base.set_scale = nv50_crtc_set_scale;
	head->base.set_color_vibrance = nv50_crtc_set_color_vibrance;
	head->base.color_vibrance = 50;
	head->base.vibrant_hue = 0;
	head->base.cursor.set_offset = nv50_cursor_set_offset;
	head->base.cursor.set_pos = nv50_cursor_set_pos;
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
			     0, 0x0000, NULL, &head->base.lut.nvbo);
	if (!ret) {
		ret = nouveau_bo_pin(head->base.lut.nvbo, TTM_PL_FLAG_VRAM);
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

	nv50_crtc_lut_load(crtc);

	/* allocate cursor resources */
	ret = nv50_curs_create(disp->disp, index, &head->curs);
	if (ret)
		goto out;

	ret = nouveau_bo_new(dev, 64 * 64 * 4, 0x100, TTM_PL_FLAG_VRAM,
			     0, 0x0000, NULL, &head->base.cursor.nvbo);
	if (!ret) {
		ret = nouveau_bo_pin(head->base.cursor.nvbo, TTM_PL_FLAG_VRAM);
		if (!ret) {
			ret = nouveau_bo_map(head->base.cursor.nvbo);
			if (ret)
				nouveau_bo_unpin(head->base.lut.nvbo);
		}
		if (ret)
			nouveau_bo_ref(NULL, &head->base.cursor.nvbo);
	}

	if (ret)
		goto out;

	/* allocate page flip / sync resources */
	ret = nv50_base_create(disp->disp, index, disp->sync->bo.offset,
			      &head->sync);
	if (ret)
		goto out;

	head->sync.addr = EVO_FLIP_SEM0(index);
	head->sync.data = 0x00000000;

	/* allocate overlay resources */
	ret = nv50_oimm_create(disp->disp, index, &head->oimm);
	if (ret)
		goto out;

	ret = nv50_ovly_create(disp->disp, index, disp->sync->bo.offset,
			      &head->ovly);
	if (ret)
		goto out;

out:
	if (ret)
		nv50_crtc_destroy(crtc);
	return ret;
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

static bool
nv50_dac_mode_fixup(struct drm_encoder *encoder,
		    const struct drm_display_mode *mode,
		    struct drm_display_mode *adjusted_mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (nv_connector && nv_connector->native_mode) {
		if (nv_connector->scaling_mode != DRM_MODE_SCALE_NONE) {
			int id = adjusted_mode->base.id;
			*adjusted_mode = *nv_connector->native_mode;
			adjusted_mode->base.id = id;
		}
	}

	return true;
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
	.mode_fixup = nv50_dac_mode_fixup,
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
	struct nouveau_i2c *i2c = nvkm_i2c(&drm->device);
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;
	int type = DRM_MODE_ENCODER_DAC;

	nv_encoder = kzalloc(sizeof(*nv_encoder), GFP_KERNEL);
	if (!nv_encoder)
		return -ENOMEM;
	nv_encoder->dcb = dcbe;
	nv_encoder->or = ffs(dcbe->or) - 1;
	nv_encoder->i2c = i2c->find(i2c, dcbe->i2c_index);

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(connector->dev, encoder, &nv50_dac_func, type);
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
	struct nouveau_connector *nv_connector;
	struct nv50_disp *disp = nv50_disp(encoder->dev);
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_sor_hda_eld_v0 eld;
		u8 data[sizeof(nv_connector->base.eld)];
	} args = {
		.base.version = 1,
		.base.method  = NV50_DISP_MTHD_V1_SOR_HDA_ELD,
		.base.hasht   = nv_encoder->dcb->hasht,
		.base.hashm   = nv_encoder->dcb->hashm,
	};

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!drm_detect_monitor_audio(nv_connector->edid))
		return;

	drm_edid_to_eld(&nv_connector->base, nv_connector->edid);
	memcpy(args.data, nv_connector->base.eld, sizeof(args.data));

	nvif_mthd(disp->disp, 0, &args, sizeof(args));
}

static void
nv50_audio_disconnect(struct drm_encoder *encoder)
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
		.base.hashm   = nv_encoder->dcb->hashm,
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

	nv50_audio_disconnect(encoder);

	nvif_mthd(disp->disp, 0, &args, sizeof(args));
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

static bool
nv50_sor_mode_fixup(struct drm_encoder *encoder,
		    const struct drm_display_mode *mode,
		    struct drm_display_mode *adjusted_mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (nv_connector && nv_connector->native_mode) {
		if (nv_connector->scaling_mode != DRM_MODE_SCALE_NONE) {
			int id = adjusted_mode->base.id;
			*adjusted_mode = *nv_connector->native_mode;
			adjusted_mode->base.id = id;
		}
	}

	return true;
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
			if (mode->clock < 165000)
				proto = 0x1;
			else
				proto = 0x5;
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
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_helper_funcs nv50_sor_hfunc = {
	.dpms = nv50_sor_dpms,
	.mode_fixup = nv50_sor_mode_fixup,
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
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nouveau_i2c *i2c = nvkm_i2c(&drm->device);
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;
	int type;

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
	nv_encoder->i2c = i2c->find(i2c, dcbe->i2c_index);
	nv_encoder->last_dpms = DRM_MODE_DPMS_OFF;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(connector->dev, encoder, &nv50_sor_func, type);
	drm_encoder_helper_add(encoder, &nv50_sor_hfunc);

	drm_mode_connector_attach_encoder(connector, encoder);
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
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (nv_connector && nv_connector->native_mode) {
		if (nv_connector->scaling_mode != DRM_MODE_SCALE_NONE) {
			int id = adjusted_mode->base.id;
			*adjusted_mode = *nv_connector->native_mode;
			adjusted_mode->base.id = id;
		}
	}

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
	struct nouveau_i2c *i2c = nvkm_i2c(&drm->device);
	struct nouveau_i2c_port *ddc = NULL;
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;
	int type;

	switch (dcbe->type) {
	case DCB_OUTPUT_TMDS:
		ddc  = i2c->find_type(i2c, NV_I2C_TYPE_EXTDDC(dcbe->extdev));
		type = DRM_MODE_ENCODER_TMDS;
		break;
	case DCB_OUTPUT_DP:
		ddc  = i2c->find_type(i2c, NV_I2C_TYPE_EXTAUX(dcbe->extdev));
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

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(connector->dev, encoder, &nv50_pior_func, type);
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
			struct gf110_dma_v0 gf110;
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
		args.gf110.page = GF110_DMA_V0_PAGE_LP;
		args.gf110.kind = kind;
		size += sizeof(args.gf110);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nv50_head *head = nv50_head(crtc);
		int ret = nvif_object_init(&head->sync.base.base.user, NULL,
					    name, NV_DMA_IN_MEMORY, &args, size,
					   &fbdma->base[head->base.index]);
		if (ret) {
			nv50_fbdma_fini(fbdma);
			return ret;
		}
	}

	ret = nvif_object_init(&mast->base.base.user, NULL, name,
				NV_DMA_IN_MEMORY, &args, size,
			       &fbdma->core);
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

	if (nvbo->tile_flags & NOUVEAU_GEM_TILE_NONCONTIG) {
		NV_ERROR(drm, "framebuffer requires contiguous bo\n");
		return -EINVAL;
	}

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
			     0, 0x0000, NULL, &disp->sync);
	if (!ret) {
		ret = nouveau_bo_pin(disp->sync, TTM_PL_FLAG_VRAM);
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
	ret = nv50_core_create(disp->disp, disp->sync->bo.offset,
			      &disp->mast);
	if (ret)
		goto out;

	/* create crtc objects to represent the hw heads */
	if (disp->disp->oclass >= GF110_DISP)
		crtcs = nvif_rd32(device, 0x022448);
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
