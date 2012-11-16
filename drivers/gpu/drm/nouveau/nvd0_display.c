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

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_gem.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"
#include "nouveau_fence.h"
#include "nv50_display.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <core/class.h>

#include <subdev/timer.h>
#include <subdev/bar.h>
#include <subdev/fb.h>

#define EVO_DMA_NR 9

#define EVO_MASTER  (0x00)
#define EVO_FLIP(c) (0x01 + (c))
#define EVO_OVLY(c) (0x05 + (c))
#define EVO_OIMM(c) (0x09 + (c))
#define EVO_CURS(c) (0x0d + (c))

/* offsets in shared sync bo of various structures */
#define EVO_SYNC(c, o) ((c) * 0x0100 + (o))
#define EVO_MAST_NTFY     EVO_SYNC(  0, 0x00)
#define EVO_FLIP_SEM0(c)  EVO_SYNC((c), 0x00)
#define EVO_FLIP_SEM1(c)  EVO_SYNC((c), 0x10)

#define EVO_CORE_HANDLE      (0xd1500000)
#define EVO_CHAN_HANDLE(t,i) (0xd15c0000 | (((t) & 0x00ff) << 8) | (i))
#define EVO_CHAN_OCLASS(t,c) ((nv_hclass(c) & 0xff00) | ((t) & 0x00ff))
#define EVO_PUSH_HANDLE(t,i) (0xd15b0000 | (i) |                               \
			      (((NV50_DISP_##t##_CLASS) & 0x00ff) << 8))

/******************************************************************************
 * EVO channel
 *****************************************************************************/

struct nvd0_chan {
	struct nouveau_object *user;
	u32 handle;
};

static int
nvd0_chan_create(struct nouveau_object *core, u32 bclass, u8 head,
		 void *data, u32 size, struct nvd0_chan *chan)
{
	struct nouveau_object *client = nv_pclass(core, NV_CLIENT_CLASS);
	const u32 oclass = EVO_CHAN_OCLASS(bclass, core);
	const u32 handle = EVO_CHAN_HANDLE(bclass, head);
	int ret;

	ret = nouveau_object_new(client, EVO_CORE_HANDLE, handle,
				 oclass, data, size, &chan->user);
	if (ret)
		return ret;

	chan->handle = handle;
	return 0;
}

static void
nvd0_chan_destroy(struct nouveau_object *core, struct nvd0_chan *chan)
{
	struct nouveau_object *client = nv_pclass(core, NV_CLIENT_CLASS);
	if (chan->handle)
		nouveau_object_del(client, EVO_CORE_HANDLE, chan->handle);
}

/******************************************************************************
 * PIO EVO channel
 *****************************************************************************/

struct nvd0_pioc {
	struct nvd0_chan base;
};

static void
nvd0_pioc_destroy(struct nouveau_object *core, struct nvd0_pioc *pioc)
{
	nvd0_chan_destroy(core, &pioc->base);
}

static int
nvd0_pioc_create(struct nouveau_object *core, u32 bclass, u8 head,
		 void *data, u32 size, struct nvd0_pioc *pioc)
{
	return nvd0_chan_create(core, bclass, head, data, size, &pioc->base);
}

/******************************************************************************
 * DMA EVO channel
 *****************************************************************************/

struct nvd0_dmac {
	struct nvd0_chan base;
	dma_addr_t handle;
	u32 *ptr;
};

static void
nvd0_dmac_destroy(struct nouveau_object *core, struct nvd0_dmac *dmac)
{
	if (dmac->ptr) {
		struct pci_dev *pdev = nv_device(core)->pdev;
		pci_free_consistent(pdev, PAGE_SIZE, dmac->ptr, dmac->handle);
	}

	nvd0_chan_destroy(core, &dmac->base);
}

static int
nvd0_dmac_create(struct nouveau_object *core, u32 bclass, u8 head,
		 void *data, u32 size, u64 syncbuf,
		 struct nvd0_dmac *dmac)
{
	struct nouveau_fb *pfb = nouveau_fb(core);
	struct nouveau_object *client = nv_pclass(core, NV_CLIENT_CLASS);
	struct nouveau_object *object;
	u32 pushbuf = *(u32 *)data;
	dma_addr_t handle;
	void *ptr;
	int ret;

	ptr = pci_alloc_consistent(nv_device(core)->pdev, PAGE_SIZE, &handle);
	if (!ptr)
		return -ENOMEM;

	ret = nouveau_object_new(client, NVDRM_DEVICE, pushbuf,
				 NV_DMA_FROM_MEMORY_CLASS,
				 &(struct nv_dma_class) {
					.flags = NV_DMA_TARGET_PCI_US |
						 NV_DMA_ACCESS_RD,
					.start = handle + 0x0000,
					.limit = handle + 0x0fff,
				 }, sizeof(struct nv_dma_class), &object);
	if (ret)
		return ret;

	ret = nvd0_chan_create(core, bclass, head, data, size, &dmac->base);
	if (ret)
		return ret;

	dmac->handle = handle;
	dmac->ptr = ptr;

	ret = nouveau_object_new(client, dmac->base.handle, NvEvoSync,
				 NV_DMA_IN_MEMORY_CLASS,
				 &(struct nv_dma_class) {
					.flags = NV_DMA_TARGET_VRAM |
						 NV_DMA_ACCESS_RDWR,
					.start = syncbuf + 0x0000,
					.limit = syncbuf + 0x0fff,
				 }, sizeof(struct nv_dma_class), &object);
	if (ret)
		goto out;

	ret = nouveau_object_new(client, dmac->base.handle, NvEvoVRAM,
				 NV_DMA_IN_MEMORY_CLASS,
				 &(struct nv_dma_class) {
					.flags = NV_DMA_TARGET_VRAM |
						 NV_DMA_ACCESS_RDWR,
					.start = 0,
					.limit = pfb->ram.size - 1,
				 }, sizeof(struct nv_dma_class), &object);
	if (ret)
		goto out;

	ret = nouveau_object_new(client, dmac->base.handle, NvEvoVRAM_LP,
				 NV_DMA_IN_MEMORY_CLASS,
				 &(struct nv_dma_class) {
					.flags = NV_DMA_TARGET_VRAM |
						 NV_DMA_ACCESS_RDWR,
					.start = 0,
					.limit = pfb->ram.size - 1,
					.conf0 = NVD0_DMA_CONF0_ENABLE |
						 NVD0_DMA_CONF0_PAGE_LP,
				 }, sizeof(struct nv_dma_class), &object);
	if (ret)
		goto out;

	ret = nouveau_object_new(client, dmac->base.handle, NvEvoFB32,
				 NV_DMA_IN_MEMORY_CLASS,
				 &(struct nv_dma_class) {
					.flags = NV_DMA_TARGET_VRAM |
						 NV_DMA_ACCESS_RDWR,
					.start = 0,
					.limit = pfb->ram.size - 1,
					.conf0 = 0x00fe |
						 NVD0_DMA_CONF0_ENABLE |
						 NVD0_DMA_CONF0_PAGE_LP,
				 }, sizeof(struct nv_dma_class), &object);
out:
	if (ret)
		nvd0_dmac_destroy(core, dmac);
	return ret;
}

struct nvd0_mast {
	struct nvd0_dmac base;
};

struct nvd0_curs {
	struct nvd0_pioc base;
};

struct nvd0_sync {
	struct nvd0_dmac base;
	struct {
		u32 offset;
		u16 value;
	} sem;
};

struct nvd0_ovly {
	struct nvd0_dmac base;
};

struct nvd0_oimm {
	struct nvd0_pioc base;
};

struct nvd0_head {
	struct nouveau_crtc base;
	struct nvd0_curs curs;
	struct nvd0_sync sync;
	struct nvd0_ovly ovly;
	struct nvd0_oimm oimm;
};

#define nvd0_head(c) ((struct nvd0_head *)nouveau_crtc(c))
#define nvd0_curs(c) (&nvd0_head(c)->curs)
#define nvd0_sync(c) (&nvd0_head(c)->sync)
#define nvd0_ovly(c) (&nvd0_head(c)->ovly)
#define nvd0_oimm(c) (&nvd0_head(c)->oimm)
#define nvd0_chan(c) (&(c)->base.base)
#define nvd0_vers(c) nv_mclass(nvd0_chan(c)->user)

struct nvd0_disp {
	struct nouveau_object *core;
	struct nvd0_mast mast;

	u32 modeset;

	struct nouveau_bo *sync;
};

static struct nvd0_disp *
nvd0_disp(struct drm_device *dev)
{
	return nouveau_display(dev)->priv;
}

#define nvd0_mast(d) (&nvd0_disp(d)->mast)

static struct drm_crtc *
nvd0_display_crtc_get(struct drm_encoder *encoder)
{
	return nouveau_encoder(encoder)->crtc;
}

/******************************************************************************
 * EVO channel helpers
 *****************************************************************************/
static u32 *
evo_wait(void *evoc, int nr)
{
	struct nvd0_dmac *dmac = evoc;
	u32 put = nv_ro32(dmac->base.user, 0x0000) / 4;

	if (put + nr >= (PAGE_SIZE / 4) - 8) {
		dmac->ptr[put] = 0x20000000;

		nv_wo32(dmac->base.user, 0x0000, 0x00000000);
		if (!nv_wait(dmac->base.user, 0x0004, ~0, 0x00000000)) {
			NV_ERROR(dmac->base.user, "channel stalled\n");
			return NULL;
		}

		put = 0;
	}

	return dmac->ptr + put;
}

static void
evo_kick(u32 *push, void *evoc)
{
	struct nvd0_dmac *dmac = evoc;
	nv_wo32(dmac->base.user, 0x0000, (push - dmac->ptr) << 2);
}

#define evo_mthd(p,m,s) *((p)++) = (((s) << 18) | (m))
#define evo_data(p,d)   *((p)++) = (d)

static bool
evo_sync_wait(void *data)
{
	return nouveau_bo_rd32(data, EVO_MAST_NTFY) != 0x00000000;
}

static int
evo_sync(struct drm_device *dev)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nvd0_disp *disp = nvd0_disp(dev);
	struct nvd0_mast *mast = nvd0_mast(dev);
	u32 *push = evo_wait(mast, 8);
	if (push) {
		nouveau_bo_wr32(disp->sync, EVO_MAST_NTFY, 0x00000000);
		evo_mthd(push, 0x0084, 1);
		evo_data(push, 0x80000000 | EVO_MAST_NTFY);
		evo_mthd(push, 0x0080, 2);
		evo_data(push, 0x00000000);
		evo_data(push, 0x00000000);
		evo_kick(push, mast);
		if (nv_wait_cb(device, evo_sync_wait, disp->sync))
			return 0;
	}

	return -EBUSY;
}

/******************************************************************************
 * Page flipping channel
 *****************************************************************************/
struct nouveau_bo *
nvd0_display_crtc_sema(struct drm_device *dev, int crtc)
{
	return nvd0_disp(dev)->sync;
}

void
nvd0_display_flip_stop(struct drm_crtc *crtc)
{
	struct nvd0_sync *sync = nvd0_sync(crtc);
	u32 *push;

	push = evo_wait(sync, 8);
	if (push) {
		evo_mthd(push, 0x0084, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x0094, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x00c0, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x0080, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, sync);
	}
}

int
nvd0_display_flip_next(struct drm_crtc *crtc, struct drm_framebuffer *fb,
		       struct nouveau_channel *chan, u32 swap_interval)
{
	struct nouveau_framebuffer *nv_fb = nouveau_framebuffer(fb);
	struct nvd0_disp *disp = nvd0_disp(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nvd0_sync *sync = nvd0_sync(crtc);
	u64 offset;
	u32 *push;
	int ret;

	swap_interval <<= 4;
	if (swap_interval == 0)
		swap_interval |= 0x100;

	push = evo_wait(sync, 128);
	if (unlikely(push == NULL))
		return -EBUSY;

	/* synchronise with the rendering channel, if necessary */
	if (likely(chan)) {
		ret = RING_SPACE(chan, 10);
		if (ret)
			return ret;


		offset  = nvc0_fence_crtc(chan, nv_crtc->index);
		offset += sync->sem.offset;

		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(offset));
		OUT_RING  (chan, lower_32_bits(offset));
		OUT_RING  (chan, 0xf00d0000 | sync->sem.value);
		OUT_RING  (chan, 0x1002);
		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(offset));
		OUT_RING  (chan, lower_32_bits(offset ^ 0x10));
		OUT_RING  (chan, 0x74b1e000);
		OUT_RING  (chan, 0x1001);
		FIRE_RING (chan);
	} else {
		nouveau_bo_wr32(disp->sync, sync->sem.offset / 4,
				0xf00d0000 | sync->sem.value);
		evo_sync(crtc->dev);
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
	evo_data(push, sync->sem.offset);
	evo_data(push, 0xf00d0000 | sync->sem.value);
	evo_data(push, 0x74b1e000);
	evo_data(push, NvEvoSync);
	evo_mthd(push, 0x00a0, 2);
	evo_data(push, 0x00000000);
	evo_data(push, 0x00000000);
	evo_mthd(push, 0x00c0, 1);
	evo_data(push, nv_fb->r_dma);
	evo_mthd(push, 0x0110, 2);
	evo_data(push, 0x00000000);
	evo_data(push, 0x00000000);
	evo_mthd(push, 0x0400, 5);
	evo_data(push, nv_fb->nvbo->bo.offset >> 8);
	evo_data(push, 0);
	evo_data(push, (fb->height << 16) | fb->width);
	evo_data(push, nv_fb->r_pitch);
	evo_data(push, nv_fb->r_format);
	evo_mthd(push, 0x0080, 1);
	evo_data(push, 0x00000000);
	evo_kick(push, sync);

	sync->sem.offset ^= 0x10;
	sync->sem.value++;
	return 0;
}

/******************************************************************************
 * CRTC
 *****************************************************************************/
static int
nvd0_crtc_set_dither(struct nouveau_crtc *nv_crtc, bool update)
{
	struct nvd0_mast *mast = nvd0_mast(nv_crtc->base.dev);
	struct nouveau_connector *nv_connector;
	struct drm_connector *connector;
	u32 *push, mode = 0x00;

	nv_connector = nouveau_crtc_connector_get(nv_crtc);
	connector = &nv_connector->base;
	if (nv_connector->dithering_mode == DITHERING_MODE_AUTO) {
		if (nv_crtc->base.fb->depth > connector->display_info.bpc * 3)
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
		if (nvd0_vers(mast) < NVD0_DISP_MAST_CLASS) {
			evo_mthd(push, 0x08a0 + (nv_crtc->index * 0x0400), 1);
			evo_data(push, mode);
		} else
		if (nvd0_vers(mast) < NVE0_DISP_MAST_CLASS) {
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
nvd0_crtc_set_scale(struct nouveau_crtc *nv_crtc, bool update)
{
	struct nvd0_mast *mast = nvd0_mast(nv_crtc->base.dev);
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
		if (nvd0_vers(mast) < NVD0_DISP_MAST_CLASS) {
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
			nvd0_display_flip_stop(crtc);
			nvd0_display_flip_next(crtc, crtc->fb, NULL, 1);
		}
	}

	return 0;
}

static int
nvd0_crtc_set_image(struct nouveau_crtc *nv_crtc, struct drm_framebuffer *fb,
		    int x, int y, bool update)
{
	struct nouveau_framebuffer *nvfb = nouveau_framebuffer(fb);
	struct nvd0_mast *mast = nvd0_mast(nv_crtc->base.dev);
	u32 *push;

	push = evo_wait(mast, 16);
	if (push) {
		if (nvd0_vers(mast) < NVD0_DISP_MAST_CLASS) {
			evo_mthd(push, 0x0860 + (nv_crtc->index * 0x400), 1);
			evo_data(push, nvfb->nvbo->bo.offset >> 8);
			evo_mthd(push, 0x0868 + (nv_crtc->index * 0x400), 3);
			evo_data(push, (fb->height << 16) | fb->width);
			evo_data(push, nvfb->r_pitch);
			evo_data(push, nvfb->r_format);
			evo_mthd(push, 0x08c0 + (nv_crtc->index * 0x400), 1);
			evo_data(push, (y << 16) | x);
			if (nvd0_vers(mast) > NV50_DISP_MAST_CLASS) {
				evo_mthd(push, 0x0874 + (nv_crtc->index * 0x400), 1);
				evo_data(push, nvfb->r_dma);
			}
		} else {
			evo_mthd(push, 0x0460 + (nv_crtc->index * 0x300), 1);
			evo_data(push, nvfb->nvbo->bo.offset >> 8);
			evo_mthd(push, 0x0468 + (nv_crtc->index * 0x300), 4);
			evo_data(push, (fb->height << 16) | fb->width);
			evo_data(push, nvfb->r_pitch);
			evo_data(push, nvfb->r_format);
			evo_data(push, nvfb->r_dma);
			evo_mthd(push, 0x04b0 + (nv_crtc->index * 0x300), 1);
			evo_data(push, (y << 16) | x);
		}

		if (update) {
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, mast);
	}

	nv_crtc->fb.tile_flags = nvfb->r_dma;
	return 0;
}

static void
nvd0_crtc_cursor_show(struct nouveau_crtc *nv_crtc)
{
	struct nvd0_mast *mast = nvd0_mast(nv_crtc->base.dev);
	u32 *push = evo_wait(mast, 16);
	if (push) {
		if (nvd0_vers(mast) < NV84_DISP_MAST_CLASS) {
			evo_mthd(push, 0x0880 + (nv_crtc->index * 0x400), 2);
			evo_data(push, 0x85000000);
			evo_data(push, nv_crtc->cursor.nvbo->bo.offset >> 8);
		} else
		if (nvd0_vers(mast) < NVD0_DISP_MAST_CLASS) {
			evo_mthd(push, 0x0880 + (nv_crtc->index * 0x400), 2);
			evo_data(push, 0x85000000);
			evo_data(push, nv_crtc->cursor.nvbo->bo.offset >> 8);
			evo_mthd(push, 0x089c + (nv_crtc->index * 0x400), 1);
			evo_data(push, NvEvoVRAM);
		} else {
			evo_mthd(push, 0x0480 + (nv_crtc->index * 0x300), 2);
			evo_data(push, 0x85000000);
			evo_data(push, nv_crtc->cursor.nvbo->bo.offset >> 8);
			evo_mthd(push, 0x048c + (nv_crtc->index * 0x300), 1);
			evo_data(push, NvEvoVRAM);
		}
		evo_kick(push, mast);
	}
}

static void
nvd0_crtc_cursor_hide(struct nouveau_crtc *nv_crtc)
{
	struct nvd0_mast *mast = nvd0_mast(nv_crtc->base.dev);
	u32 *push = evo_wait(mast, 16);
	if (push) {
		if (nvd0_vers(mast) < NV84_DISP_MAST_CLASS) {
			evo_mthd(push, 0x0880 + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x05000000);
		} else
		if (nvd0_vers(mast) < NVD0_DISP_MAST_CLASS) {
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
nvd0_crtc_cursor_show_hide(struct nouveau_crtc *nv_crtc, bool show, bool update)
{
	struct nvd0_mast *mast = nvd0_mast(nv_crtc->base.dev);

	if (show)
		nvd0_crtc_cursor_show(nv_crtc);
	else
		nvd0_crtc_cursor_hide(nv_crtc);

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
nvd0_crtc_dpms(struct drm_crtc *crtc, int mode)
{
}

static void
nvd0_crtc_prepare(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nvd0_mast *mast = nvd0_mast(crtc->dev);
	u32 *push;

	nvd0_display_flip_stop(crtc);

	push = evo_wait(mast, 2);
	if (push) {
		if (nvd0_vers(mast) < NV84_DISP_MAST_CLASS) {
			evo_mthd(push, 0x0874 + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x0840 + (nv_crtc->index * 0x400), 1);
			evo_data(push, 0x40000000);
		} else
		if (nvd0_vers(mast) <  NVD0_DISP_MAST_CLASS) {
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

	nvd0_crtc_cursor_show_hide(nv_crtc, false, false);
}

static void
nvd0_crtc_commit(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nvd0_mast *mast = nvd0_mast(crtc->dev);
	u32 *push;

	push = evo_wait(mast, 32);
	if (push) {
		if (nvd0_vers(mast) < NV84_DISP_MAST_CLASS) {
			evo_mthd(push, 0x0874 + (nv_crtc->index * 0x400), 1);
			evo_data(push, NvEvoVRAM_LP);
			evo_mthd(push, 0x0840 + (nv_crtc->index * 0x400), 2);
			evo_data(push, 0xc0000000);
			evo_data(push, nv_crtc->lut.nvbo->bo.offset >> 8);
		} else
		if (nvd0_vers(mast) < NVD0_DISP_MAST_CLASS) {
			evo_mthd(push, 0x0874 + (nv_crtc->index * 0x400), 1);
			evo_data(push, nv_crtc->fb.tile_flags);
			evo_mthd(push, 0x0840 + (nv_crtc->index * 0x400), 2);
			evo_data(push, 0xc0000000);
			evo_data(push, nv_crtc->lut.nvbo->bo.offset >> 8);
			evo_mthd(push, 0x085c + (nv_crtc->index * 0x400), 1);
			evo_data(push, NvEvoVRAM);
		} else {
			evo_mthd(push, 0x0474 + (nv_crtc->index * 0x300), 1);
			evo_data(push, nv_crtc->fb.tile_flags);
			evo_mthd(push, 0x0440 + (nv_crtc->index * 0x300), 4);
			evo_data(push, 0x83000000);
			evo_data(push, nv_crtc->lut.nvbo->bo.offset >> 8);
			evo_data(push, 0x00000000);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x045c + (nv_crtc->index * 0x300), 1);
			evo_data(push, NvEvoVRAM);
			evo_mthd(push, 0x0430 + (nv_crtc->index * 0x300), 1);
			evo_data(push, 0xffffff00);
		}

		evo_kick(push, mast);
	}

	nvd0_crtc_cursor_show_hide(nv_crtc, nv_crtc->cursor.visible, true);
	nvd0_display_flip_next(crtc, crtc->fb, NULL, 1);
}

static bool
nvd0_crtc_mode_fixup(struct drm_crtc *crtc, const struct drm_display_mode *mode,
		     struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int
nvd0_crtc_swap_fbs(struct drm_crtc *crtc, struct drm_framebuffer *old_fb)
{
	struct nouveau_framebuffer *nvfb = nouveau_framebuffer(crtc->fb);
	int ret;

	ret = nouveau_bo_pin(nvfb->nvbo, TTM_PL_FLAG_VRAM);
	if (ret)
		return ret;

	if (old_fb) {
		nvfb = nouveau_framebuffer(old_fb);
		nouveau_bo_unpin(nvfb->nvbo);
	}

	return 0;
}

static int
nvd0_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *umode,
		   struct drm_display_mode *mode, int x, int y,
		   struct drm_framebuffer *old_fb)
{
	struct nvd0_mast *mast = nvd0_mast(crtc->dev);
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

	ret = nvd0_crtc_swap_fbs(crtc, old_fb);
	if (ret)
		return ret;

	push = evo_wait(mast, 64);
	if (push) {
		if (nvd0_vers(mast) < NVD0_DISP_MAST_CLASS) {
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
	nvd0_crtc_set_dither(nv_crtc, false);
	nvd0_crtc_set_scale(nv_crtc, false);
	nvd0_crtc_set_image(nv_crtc, crtc->fb, x, y, false);
	return 0;
}

static int
nvd0_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			struct drm_framebuffer *old_fb)
{
	struct nouveau_drm *drm = nouveau_drm(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	int ret;

	if (!crtc->fb) {
		NV_DEBUG(drm, "No FB bound\n");
		return 0;
	}

	ret = nvd0_crtc_swap_fbs(crtc, old_fb);
	if (ret)
		return ret;

	nvd0_display_flip_stop(crtc);
	nvd0_crtc_set_image(nv_crtc, crtc->fb, x, y, true);
	nvd0_display_flip_next(crtc, crtc->fb, NULL, 1);
	return 0;
}

static int
nvd0_crtc_mode_set_base_atomic(struct drm_crtc *crtc,
			       struct drm_framebuffer *fb, int x, int y,
			       enum mode_set_atomic state)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	nvd0_display_flip_stop(crtc);
	nvd0_crtc_set_image(nv_crtc, fb, x, y, true);
	return 0;
}

static void
nvd0_crtc_lut_load(struct drm_crtc *crtc)
{
	struct nvd0_disp *disp = nvd0_disp(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	void __iomem *lut = nvbo_kmap_obj_iovirtual(nv_crtc->lut.nvbo);
	int i;

	for (i = 0; i < 256; i++) {
		u16 r = nv_crtc->lut.r[i] >> 2;
		u16 g = nv_crtc->lut.g[i] >> 2;
		u16 b = nv_crtc->lut.b[i] >> 2;

		if (nv_mclass(disp->core) < NVD0_DISP_CLASS) {
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

static int
nvd0_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
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
		nvd0_crtc_cursor_show_hide(nv_crtc, visible, true);
		nv_crtc->cursor.visible = visible;
	}

	return ret;
}

static int
nvd0_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct nvd0_curs *curs = nvd0_curs(crtc);
	struct nvd0_chan *chan = nvd0_chan(curs);
	nv_wo32(chan->user, 0x0084, (y << 16) | (x & 0xffff));
	nv_wo32(chan->user, 0x0080, 0x00000000);
	return 0;
}

static void
nvd0_crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
		    uint32_t start, uint32_t size)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	u32 end = max(start + size, (u32)256);
	u32 i;

	for (i = start; i < end; i++) {
		nv_crtc->lut.r[i] = r[i];
		nv_crtc->lut.g[i] = g[i];
		nv_crtc->lut.b[i] = b[i];
	}

	nvd0_crtc_lut_load(crtc);
}

static void
nvd0_crtc_destroy(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nvd0_disp *disp = nvd0_disp(crtc->dev);
	struct nvd0_head *head = nvd0_head(crtc);
	nvd0_dmac_destroy(disp->core, &head->ovly.base);
	nvd0_pioc_destroy(disp->core, &head->oimm.base);
	nvd0_dmac_destroy(disp->core, &head->sync.base);
	nvd0_pioc_destroy(disp->core, &head->curs.base);
	nouveau_bo_unmap(nv_crtc->cursor.nvbo);
	nouveau_bo_ref(NULL, &nv_crtc->cursor.nvbo);
	nouveau_bo_unmap(nv_crtc->lut.nvbo);
	nouveau_bo_ref(NULL, &nv_crtc->lut.nvbo);
	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static const struct drm_crtc_helper_funcs nvd0_crtc_hfunc = {
	.dpms = nvd0_crtc_dpms,
	.prepare = nvd0_crtc_prepare,
	.commit = nvd0_crtc_commit,
	.mode_fixup = nvd0_crtc_mode_fixup,
	.mode_set = nvd0_crtc_mode_set,
	.mode_set_base = nvd0_crtc_mode_set_base,
	.mode_set_base_atomic = nvd0_crtc_mode_set_base_atomic,
	.load_lut = nvd0_crtc_lut_load,
};

static const struct drm_crtc_funcs nvd0_crtc_func = {
	.cursor_set = nvd0_crtc_cursor_set,
	.cursor_move = nvd0_crtc_cursor_move,
	.gamma_set = nvd0_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = nvd0_crtc_destroy,
	.page_flip = nouveau_crtc_page_flip,
};

static void
nvd0_cursor_set_pos(struct nouveau_crtc *nv_crtc, int x, int y)
{
}

static void
nvd0_cursor_set_offset(struct nouveau_crtc *nv_crtc, uint32_t offset)
{
}

static int
nvd0_crtc_create(struct drm_device *dev, struct nouveau_object *core, int index)
{
	struct nvd0_disp *disp = nvd0_disp(dev);
	struct nvd0_head *head;
	struct drm_crtc *crtc;
	int ret, i;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (!head)
		return -ENOMEM;

	head->base.index = index;
	head->base.set_dither = nvd0_crtc_set_dither;
	head->base.set_scale = nvd0_crtc_set_scale;
	head->base.cursor.set_offset = nvd0_cursor_set_offset;
	head->base.cursor.set_pos = nvd0_cursor_set_pos;
	for (i = 0; i < 256; i++) {
		head->base.lut.r[i] = i << 8;
		head->base.lut.g[i] = i << 8;
		head->base.lut.b[i] = i << 8;
	}

	crtc = &head->base.base;
	drm_crtc_init(dev, crtc, &nvd0_crtc_func);
	drm_crtc_helper_add(crtc, &nvd0_crtc_hfunc);
	drm_mode_crtc_set_gamma_size(crtc, 256);

	ret = nouveau_bo_new(dev, 8192, 0x100, TTM_PL_FLAG_VRAM,
			     0, 0x0000, NULL, &head->base.lut.nvbo);
	if (!ret) {
		ret = nouveau_bo_pin(head->base.lut.nvbo, TTM_PL_FLAG_VRAM);
		if (!ret)
			ret = nouveau_bo_map(head->base.lut.nvbo);
		if (ret)
			nouveau_bo_ref(NULL, &head->base.lut.nvbo);
	}

	if (ret)
		goto out;

	nvd0_crtc_lut_load(crtc);

	/* allocate cursor resources */
	ret = nvd0_pioc_create(disp->core, NV50_DISP_CURS_CLASS, index,
			      &(struct nv50_display_curs_class) {
					.head = index,
			      }, sizeof(struct nv50_display_curs_class),
			      &head->curs.base);
	if (ret)
		goto out;

	ret = nouveau_bo_new(dev, 64 * 64 * 4, 0x100, TTM_PL_FLAG_VRAM,
			     0, 0x0000, NULL, &head->base.cursor.nvbo);
	if (!ret) {
		ret = nouveau_bo_pin(head->base.cursor.nvbo, TTM_PL_FLAG_VRAM);
		if (!ret)
			ret = nouveau_bo_map(head->base.cursor.nvbo);
		if (ret)
			nouveau_bo_ref(NULL, &head->base.cursor.nvbo);
	}

	if (ret)
		goto out;

	/* allocate page flip / sync resources */
	ret = nvd0_dmac_create(disp->core, NV50_DISP_SYNC_CLASS, index,
			      &(struct nv50_display_sync_class) {
					.pushbuf = EVO_PUSH_HANDLE(SYNC, index),
					.head = index,
			      }, sizeof(struct nv50_display_sync_class),
			      disp->sync->bo.offset, &head->sync.base);
	if (ret)
		goto out;

	head->sync.sem.offset = EVO_SYNC(1 + index, 0x00);

	/* allocate overlay resources */
	ret = nvd0_pioc_create(disp->core, NV50_DISP_OIMM_CLASS, index,
			      &(struct nv50_display_oimm_class) {
					.head = index,
			      }, sizeof(struct nv50_display_oimm_class),
			      &head->oimm.base);
	if (ret)
		goto out;

	ret = nvd0_dmac_create(disp->core, NV50_DISP_OVLY_CLASS, index,
			      &(struct nv50_display_ovly_class) {
					.pushbuf = EVO_PUSH_HANDLE(OVLY, index),
					.head = index,
			      }, sizeof(struct nv50_display_ovly_class),
			      disp->sync->bo.offset, &head->ovly.base);
	if (ret)
		goto out;

out:
	if (ret)
		nvd0_crtc_destroy(crtc);
	return ret;
}

/******************************************************************************
 * DAC
 *****************************************************************************/
static void
nvd0_dac_dpms(struct drm_encoder *encoder, int mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nvd0_disp *disp = nvd0_disp(encoder->dev);
	int or = nv_encoder->or;
	u32 dpms_ctrl;

	dpms_ctrl = 0x00000000;
	if (mode == DRM_MODE_DPMS_STANDBY || mode == DRM_MODE_DPMS_OFF)
		dpms_ctrl |= 0x00000001;
	if (mode == DRM_MODE_DPMS_SUSPEND || mode == DRM_MODE_DPMS_OFF)
		dpms_ctrl |= 0x00000004;

	nv_call(disp->core, NV50_DISP_DAC_PWR + or, dpms_ctrl);
}

static bool
nvd0_dac_mode_fixup(struct drm_encoder *encoder,
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
nvd0_dac_commit(struct drm_encoder *encoder)
{
}

static void
nvd0_dac_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode,
		  struct drm_display_mode *adjusted_mode)
{
	struct nvd0_mast *mast = nvd0_mast(encoder->dev);
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	u32 *push;

	nvd0_dac_dpms(encoder, DRM_MODE_DPMS_ON);

	push = evo_wait(mast, 8);
	if (push) {
		if (nvd0_vers(mast) < NVD0_DISP_MAST_CLASS) {
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
nvd0_dac_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nvd0_mast *mast = nvd0_mast(encoder->dev);
	const int or = nv_encoder->or;
	u32 *push;

	if (nv_encoder->crtc) {
		nvd0_crtc_prepare(nv_encoder->crtc);

		push = evo_wait(mast, 4);
		if (push) {
			if (nvd0_vers(mast) < NVD0_DISP_MAST_CLASS) {
				evo_mthd(push, 0x0400 + (or * 0x080), 1);
				evo_data(push, 0x00000000);
			} else {
				evo_mthd(push, 0x0180 + (or * 0x020), 1);
				evo_data(push, 0x00000000);
			}

			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
			evo_kick(push, mast);
		}
	}

	nv_encoder->crtc = NULL;
}

static enum drm_connector_status
nvd0_dac_detect(struct drm_encoder *encoder, struct drm_connector *connector)
{
	struct nvd0_disp *disp = nvd0_disp(encoder->dev);
	int ret, or = nouveau_encoder(encoder)->or;
	u32 load = 0;

	ret = nv_exec(disp->core, NV50_DISP_DAC_LOAD + or, &load, sizeof(load));
	if (ret || load != 7)
		return connector_status_disconnected;

	return connector_status_connected;
}

static void
nvd0_dac_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_helper_funcs nvd0_dac_hfunc = {
	.dpms = nvd0_dac_dpms,
	.mode_fixup = nvd0_dac_mode_fixup,
	.prepare = nvd0_dac_disconnect,
	.commit = nvd0_dac_commit,
	.mode_set = nvd0_dac_mode_set,
	.disable = nvd0_dac_disconnect,
	.get_crtc = nvd0_display_crtc_get,
	.detect = nvd0_dac_detect
};

static const struct drm_encoder_funcs nvd0_dac_func = {
	.destroy = nvd0_dac_destroy,
};

static int
nvd0_dac_create(struct drm_connector *connector, struct dcb_output *dcbe)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;

	nv_encoder = kzalloc(sizeof(*nv_encoder), GFP_KERNEL);
	if (!nv_encoder)
		return -ENOMEM;
	nv_encoder->dcb = dcbe;
	nv_encoder->or = ffs(dcbe->or) - 1;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(dev, encoder, &nvd0_dac_func, DRM_MODE_ENCODER_DAC);
	drm_encoder_helper_add(encoder, &nvd0_dac_hfunc);

	drm_mode_connector_attach_encoder(connector, encoder);
	return 0;
}

/******************************************************************************
 * Audio
 *****************************************************************************/
static void
nvd0_audio_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;
	struct nvd0_disp *disp = nvd0_disp(encoder->dev);

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!drm_detect_monitor_audio(nv_connector->edid))
		return;

	drm_edid_to_eld(&nv_connector->base, nv_connector->edid);

	nv_exec(disp->core, NVA3_DISP_SOR_HDA_ELD + nv_encoder->or,
			    nv_connector->base.eld,
			    nv_connector->base.eld[2] * 4);
}

static void
nvd0_audio_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nvd0_disp *disp = nvd0_disp(encoder->dev);

	nv_exec(disp->core, NVA3_DISP_SOR_HDA_ELD + nv_encoder->or, NULL, 0);
}

/******************************************************************************
 * HDMI
 *****************************************************************************/
static void
nvd0_hdmi_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector;
	struct nvd0_disp *disp = nvd0_disp(encoder->dev);
	const u32 moff = (nv_crtc->index << 3) | nv_encoder->or;
	u32 rekey = 56; /* binary driver, and tegra constant */
	u32 max_ac_packet;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!drm_detect_hdmi_monitor(nv_connector->edid))
		return;

	max_ac_packet  = mode->htotal - mode->hdisplay;
	max_ac_packet -= rekey;
	max_ac_packet -= 18; /* constant from tegra */
	max_ac_packet /= 32;

	nv_call(disp->core, NV84_DISP_SOR_HDMI_PWR + moff,
			    NV84_DISP_SOR_HDMI_PWR_STATE_ON |
			    (max_ac_packet << 16) | rekey);

	nvd0_audio_mode_set(encoder, mode);
}

static void
nvd0_hdmi_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(nv_encoder->crtc);
	struct nvd0_disp *disp = nvd0_disp(encoder->dev);
	const u32 moff = (nv_crtc->index << 3) | nv_encoder->or;

	nvd0_audio_disconnect(encoder);

	nv_call(disp->core, NV84_DISP_SOR_HDMI_PWR + moff, 0x00000000);
}

/******************************************************************************
 * SOR
 *****************************************************************************/
static void
nvd0_sor_dpms(struct drm_encoder *encoder, int mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct nvd0_disp *disp = nvd0_disp(dev);
	struct drm_encoder *partner;
	int or = nv_encoder->or;

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

	nv_call(disp->core, NV50_DISP_SOR_PWR + or, (mode == DRM_MODE_DPMS_ON));

	if (nv_encoder->dcb->type == DCB_OUTPUT_DP)
		nouveau_dp_dpms(encoder, mode, nv_encoder->dp.datarate, disp->core);
}

static bool
nvd0_sor_mode_fixup(struct drm_encoder *encoder,
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
nvd0_sor_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	u32 *push;

	if (nv_encoder->crtc) {
		nvd0_crtc_prepare(nv_encoder->crtc);

		push = evo_wait(nvd0_mast(dev), 4);
		if (push) {
			evo_mthd(push, 0x0200 + (nv_encoder->or * 0x20), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
			evo_kick(push, nvd0_mast(dev));
		}

		nvd0_hdmi_disconnect(encoder);

		nv_encoder->crtc = NULL;
		nv_encoder->last_dpms = DRM_MODE_DPMS_OFF;
	}
}

static void
nvd0_sor_prepare(struct drm_encoder *encoder)
{
	nvd0_sor_disconnect(encoder);
	if (nouveau_encoder(encoder)->dcb->type == DCB_OUTPUT_DP)
		evo_sync(encoder->dev);
}

static void
nvd0_sor_commit(struct drm_encoder *encoder)
{
}

static void
nvd0_sor_mode_set(struct drm_encoder *encoder, struct drm_display_mode *umode,
		  struct drm_display_mode *mode)
{
	struct nvd0_disp *disp = nvd0_disp(encoder->dev);
	struct drm_device *dev = encoder->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector;
	struct nvbios *bios = &drm->vbios;
	int or = nv_encoder->or;
	u32 mode_ctrl = (1 << nv_crtc->index);
	u32 syncs, magic, *push;
	u32 or_config;

	syncs = 0x00000001;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		syncs |= 0x00000008;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		syncs |= 0x00000010;

	magic = 0x31ec6000 | (nv_crtc->index << 25);
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		magic |= 0x00000001;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	switch (nv_encoder->dcb->type) {
	case DCB_OUTPUT_TMDS:
		if (nv_encoder->dcb->sorconf.link & 1) {
			if (mode->clock < 165000)
				mode_ctrl |= 0x00000100;
			else
				mode_ctrl |= 0x00000500;
		} else {
			mode_ctrl |= 0x00000200;
		}

		nvd0_hdmi_mode_set(encoder, mode);
		break;
	case DCB_OUTPUT_LVDS:
		or_config = (mode_ctrl & 0x00000f00) >> 8;
		if (bios->fp_no_ddc) {
			if (bios->fp.dual_link)
				or_config |= 0x0100;
			if (bios->fp.if_is_24bit)
				or_config |= 0x0200;
		} else {
			if (nv_connector->type == DCB_CONNECTOR_LVDS_SPWG) {
				if (((u8 *)nv_connector->edid)[121] == 2)
					or_config |= 0x0100;
			} else
			if (mode->clock >= bios->fp.duallink_transition_clk) {
				or_config |= 0x0100;
			}

			if (or_config & 0x0100) {
				if (bios->fp.strapless_is_24bit & 2)
					or_config |= 0x0200;
			} else {
				if (bios->fp.strapless_is_24bit & 1)
					or_config |= 0x0200;
			}

			if (nv_connector->base.display_info.bpc == 8)
				or_config |= 0x0200;
		}

		nv_call(disp->core, NV50_DISP_SOR_LVDS_SCRIPT + or, or_config);
		break;
	case DCB_OUTPUT_DP:
		if (nv_connector->base.display_info.bpc == 6) {
			nv_encoder->dp.datarate = mode->clock * 18 / 8;
			syncs |= 0x00000002 << 6;
		} else {
			nv_encoder->dp.datarate = mode->clock * 24 / 8;
			syncs |= 0x00000005 << 6;
		}

		if (nv_encoder->dcb->sorconf.link & 1)
			mode_ctrl |= 0x00000800;
		else
			mode_ctrl |= 0x00000900;
		break;
	default:
		BUG_ON(1);
		break;
	}

	nvd0_sor_dpms(encoder, DRM_MODE_DPMS_ON);

	push = evo_wait(nvd0_mast(dev), 8);
	if (push) {
		evo_mthd(push, 0x0404 + (nv_crtc->index * 0x300), 2);
		evo_data(push, syncs);
		evo_data(push, magic);
		evo_mthd(push, 0x0200 + (nv_encoder->or * 0x020), 1);
		evo_data(push, mode_ctrl);
		evo_kick(push, nvd0_mast(dev));
	}

	nv_encoder->crtc = encoder->crtc;
}

static void
nvd0_sor_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_helper_funcs nvd0_sor_hfunc = {
	.dpms = nvd0_sor_dpms,
	.mode_fixup = nvd0_sor_mode_fixup,
	.prepare = nvd0_sor_prepare,
	.commit = nvd0_sor_commit,
	.mode_set = nvd0_sor_mode_set,
	.disable = nvd0_sor_disconnect,
	.get_crtc = nvd0_display_crtc_get,
};

static const struct drm_encoder_funcs nvd0_sor_func = {
	.destroy = nvd0_sor_destroy,
};

static int
nvd0_sor_create(struct drm_connector *connector, struct dcb_output *dcbe)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;

	nv_encoder = kzalloc(sizeof(*nv_encoder), GFP_KERNEL);
	if (!nv_encoder)
		return -ENOMEM;
	nv_encoder->dcb = dcbe;
	nv_encoder->or = ffs(dcbe->or) - 1;
	nv_encoder->last_dpms = DRM_MODE_DPMS_OFF;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(dev, encoder, &nvd0_sor_func, DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &nvd0_sor_hfunc);

	drm_mode_connector_attach_encoder(connector, encoder);
	return 0;
}

/******************************************************************************
 * Init
 *****************************************************************************/
void
nvd0_display_fini(struct drm_device *dev)
{
}

int
nvd0_display_init(struct drm_device *dev)
{
	u32 *push = evo_wait(nvd0_mast(dev), 32);
	if (push) {
		evo_mthd(push, 0x0088, 1);
		evo_data(push, NvEvoSync);
		evo_mthd(push, 0x0084, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x0084, 1);
		evo_data(push, 0x80000000);
		evo_mthd(push, 0x008c, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, nvd0_mast(dev));
		return 0;
	}

	return -EBUSY;
}

void
nvd0_display_destroy(struct drm_device *dev)
{
	struct nvd0_disp *disp = nvd0_disp(dev);

	nvd0_dmac_destroy(disp->core, &disp->mast.base);

	nouveau_bo_unmap(disp->sync);
	nouveau_bo_ref(NULL, &disp->sync);

	nouveau_display(dev)->priv = NULL;
	kfree(disp);
}

int
nvd0_display_create(struct drm_device *dev)
{
	static const u16 oclass[] = {
		NVE0_DISP_CLASS,
		NVD0_DISP_CLASS,
	};
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct dcb_table *dcb = &drm->vbios.dcb;
	struct drm_connector *connector, *tmp;
	struct nvd0_disp *disp;
	struct dcb_output *dcbe;
	int crtcs, ret, i;

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;

	nouveau_display(dev)->priv = disp;
	nouveau_display(dev)->dtor = nvd0_display_destroy;
	nouveau_display(dev)->init = nvd0_display_init;
	nouveau_display(dev)->fini = nvd0_display_fini;

	/* small shared memory area we use for notifiers and semaphores */
	ret = nouveau_bo_new(dev, 4096, 0x1000, TTM_PL_FLAG_VRAM,
			     0, 0x0000, NULL, &disp->sync);
	if (!ret) {
		ret = nouveau_bo_pin(disp->sync, TTM_PL_FLAG_VRAM);
		if (!ret)
			ret = nouveau_bo_map(disp->sync);
		if (ret)
			nouveau_bo_ref(NULL, &disp->sync);
	}

	if (ret)
		goto out;

	/* attempt to allocate a supported evo display class */
	ret = -ENODEV;
	for (i = 0; ret && i < ARRAY_SIZE(oclass); i++) {
		ret = nouveau_object_new(nv_object(drm), NVDRM_DEVICE,
					 0xd1500000, oclass[i], NULL, 0,
					 &disp->core);
	}

	if (ret)
		goto out;

	/* allocate master evo channel */
	ret = nvd0_dmac_create(disp->core, NV50_DISP_MAST_CLASS, 0,
			      &(struct nv50_display_mast_class) {
					.pushbuf = EVO_PUSH_HANDLE(MAST, 0),
			      }, sizeof(struct nv50_display_mast_class),
			      disp->sync->bo.offset, &disp->mast.base);
	if (ret)
		goto out;

	/* create crtc objects to represent the hw heads */
	crtcs = nv_rd32(device, 0x022448);
	for (i = 0; i < crtcs; i++) {
		ret = nvd0_crtc_create(dev, disp->core, i);
		if (ret)
			goto out;
	}

	/* create encoder/connector objects based on VBIOS DCB table */
	for (i = 0, dcbe = &dcb->entry[0]; i < dcb->entries; i++, dcbe++) {
		connector = nouveau_connector_create(dev, dcbe->connector);
		if (IS_ERR(connector))
			continue;

		if (dcbe->location != DCB_LOC_ON_CHIP) {
			NV_WARN(drm, "skipping off-chip encoder %d/%d\n",
				dcbe->type, ffs(dcbe->or) - 1);
			continue;
		}

		switch (dcbe->type) {
		case DCB_OUTPUT_TMDS:
		case DCB_OUTPUT_LVDS:
		case DCB_OUTPUT_DP:
			nvd0_sor_create(connector, dcbe);
			break;
		case DCB_OUTPUT_ANALOG:
			nvd0_dac_create(connector, dcbe);
			break;
		default:
			NV_WARN(drm, "skipping unsupported encoder %d/%d\n",
				dcbe->type, ffs(dcbe->or) - 1);
			continue;
		}
	}

	/* cull any connectors we created that don't have an encoder */
	list_for_each_entry_safe(connector, tmp, &dev->mode_config.connector_list, head) {
		if (connector->encoder_ids[0])
			continue;

		NV_WARN(drm, "%s has no encoders, removing\n",
			drm_get_connector_name(connector));
		connector->funcs->destroy(connector);
	}

out:
	if (ret)
		nvd0_display_destroy(dev);
	return ret;
}
