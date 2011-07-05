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

#include "drmP.h"
#include "drm_crtc_helper.h"

#include "nouveau_drv.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"

#define MEM_SYNC 0xe0000001
#define MEM_VRAM 0xe0010000

struct nvd0_display {
	struct nouveau_gpuobj *mem;
	struct {
		dma_addr_t handle;
		u32 *ptr;
	} evo[1];
};

static struct nvd0_display *
nvd0_display(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return dev_priv->engine.display.priv;
}

static int
evo_icmd(struct drm_device *dev, int id, u32 mthd, u32 data)
{
	int ret = 0;
	nv_mask(dev, 0x610700 + (id * 0x10), 0x00000001, 0x00000001);
	nv_wr32(dev, 0x610704 + (id * 0x10), data);
	nv_mask(dev, 0x610704 + (id * 0x10), 0x80000ffc, 0x80000000 | mthd);
	if (!nv_wait(dev, 0x610704 + (id * 0x10), 0x80000000, 0x00000000))
		ret = -EBUSY;
	nv_mask(dev, 0x610700 + (id * 0x10), 0x00000001, 0x00000000);
	return ret;
}

static u32 *
evo_wait(struct drm_device *dev, int id, int nr)
{
	struct nvd0_display *disp = nvd0_display(dev);
	u32 put = nv_rd32(dev, 0x640000 + (id * 0x1000)) / 4;

	if (put + nr >= (PAGE_SIZE / 4)) {
		disp->evo[id].ptr[put] = 0x20000000;

		nv_wr32(dev, 0x640000 + (id * 0x1000), 0x00000000);
		if (!nv_wait(dev, 0x640004 + (id * 0x1000), ~0, 0x00000000)) {
			NV_ERROR(dev, "evo %d dma stalled\n", id);
			return NULL;
		}

		put = 0;
	}

	return disp->evo[id].ptr + put;
}

static void
evo_kick(u32 *push, struct drm_device *dev, int id)
{
	struct nvd0_display *disp = nvd0_display(dev);
	nv_wr32(dev, 0x640000 + (id * 0x1000), (push - disp->evo[id].ptr) << 2);
}

#define evo_mthd(p,m,s) *((p)++) = (((s) << 18) | (m))
#define evo_data(p,d)   *((p)++) = (d)

static struct drm_crtc *
nvd0_display_crtc_get(struct drm_encoder *encoder)
{
	return nouveau_encoder(encoder)->crtc;
}

/******************************************************************************
 * DAC
 *****************************************************************************/

/******************************************************************************
 * SOR
 *****************************************************************************/
static void
nvd0_sor_dpms(struct drm_encoder *encoder, int mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_encoder *partner;
	int or = nv_encoder->or;
	u32 dpms_ctrl;

	nv_encoder->last_dpms = mode;

	list_for_each_entry(partner, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_partner = nouveau_encoder(partner);

		if (partner->encoder_type != DRM_MODE_ENCODER_TMDS)
			continue;

		if (nv_partner != nv_encoder &&
		    nv_partner->dcb->or == nv_encoder->or) {
			if (nv_partner->last_dpms == DRM_MODE_DPMS_ON)
				return;
			break;
		}
	}

	dpms_ctrl  = (mode == DRM_MODE_DPMS_ON);
	dpms_ctrl |= 0x80000000;

	nv_wait(dev, 0x61c004 + (or * 0x0800), 0x80000000, 0x00000000);
	nv_mask(dev, 0x61c004 + (or * 0x0800), 0x80000001, dpms_ctrl);
	nv_wait(dev, 0x61c004 + (or * 0x0800), 0x80000000, 0x00000000);
	nv_wait(dev, 0x61c030 + (or * 0x0800), 0x10000000, 0x00000000);
}

static bool
nvd0_sor_mode_fixup(struct drm_encoder *encoder, struct drm_display_mode *mode,
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
nvd0_sor_prepare(struct drm_encoder *encoder)
{
}

static void
nvd0_sor_commit(struct drm_encoder *encoder)
{
}

static void
nvd0_sor_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode,
		  struct drm_display_mode *adjusted_mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	u32 mode_ctrl = (1 << nv_crtc->index);
	u32 *push;

	if (nv_encoder->dcb->sorconf.link & 1) {
		if (adjusted_mode->clock < 165000)
			mode_ctrl |= 0x00000100;
		else
			mode_ctrl |= 0x00000500;
	} else {
		mode_ctrl |= 0x00000200;
	}

	nvd0_sor_dpms(encoder, DRM_MODE_DPMS_ON);

	push = evo_wait(encoder->dev, 0, 2);
	if (push) {
		evo_mthd(push, 0x0200 + (nv_encoder->or * 0x20), 1);
		evo_data(push, mode_ctrl);
	}

	nv_encoder->crtc = encoder->crtc;
}

static void
nvd0_sor_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;

	if (nv_encoder->crtc) {
		u32 *push = evo_wait(dev, 0, 4);
		if (push) {
			evo_mthd(push, 0x0200 + (nv_encoder->or * 0x20), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
			evo_kick(push, dev, 0);
		}

		nv_encoder->crtc = NULL;
		nv_encoder->last_dpms = DRM_MODE_DPMS_OFF;
	}
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
nvd0_sor_create(struct drm_connector *connector, struct dcb_entry *dcbe)
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
 * IRQ
 *****************************************************************************/
static void
nvd0_display_intr(struct drm_device *dev)
{
	u32 intr = nv_rd32(dev, 0x610088);

	if (intr & 0x00000002) {
		u32 stat = nv_rd32(dev, 0x61009c);
		int chid = ffs(stat) - 1;
		if (chid >= 0) {
			u32 mthd = nv_rd32(dev, 0x6101f0 + (chid * 12));
			u32 data = nv_rd32(dev, 0x6101f4 + (chid * 12));
			u32 unkn = nv_rd32(dev, 0x6101f8 + (chid * 12));

			NV_INFO(dev, "EvoCh: chid %d mthd 0x%04x data 0x%08x "
				     "0x%08x 0x%08x\n",
				chid, (mthd & 0x0000ffc), data, mthd, unkn);
			nv_wr32(dev, 0x61009c, (1 << chid));
			nv_wr32(dev, 0x6101f0 + (chid * 12), 0x90000000);
		}

		intr &= ~0x00000002;
	}

	if (intr & 0x01000000) {
		u32 stat = nv_rd32(dev, 0x6100bc);
		nv_wr32(dev, 0x6100bc, stat);
		intr &= ~0x01000000;
	}

	if (intr & 0x02000000) {
		u32 stat = nv_rd32(dev, 0x6108bc);
		nv_wr32(dev, 0x6108bc, stat);
		intr &= ~0x02000000;
	}

	if (intr)
		NV_INFO(dev, "PDISP: unknown intr 0x%08x\n", intr);
}

/******************************************************************************
 * Init
 *****************************************************************************/
static void
nvd0_display_fini(struct drm_device *dev)
{
	int i;

	/* fini cursors */
	for (i = 14; i >= 13; i--) {
		if (!(nv_rd32(dev, 0x610490 + (i * 0x10)) & 0x00000001))
			continue;

		nv_mask(dev, 0x610490 + (i * 0x10), 0x00000001, 0x00000000);
		nv_wait(dev, 0x610490 + (i * 0x10), 0x00010000, 0x00000000);
		nv_mask(dev, 0x610090, 1 << i, 0x00000000);
		nv_mask(dev, 0x6100a0, 1 << i, 0x00000000);
	}

	/* fini master */
	if (nv_rd32(dev, 0x610490) & 0x00000010) {
		nv_mask(dev, 0x610490, 0x00000010, 0x00000000);
		nv_mask(dev, 0x610490, 0x00000003, 0x00000000);
		nv_wait(dev, 0x610490, 0x80000000, 0x00000000);
		nv_mask(dev, 0x610090, 0x00000001, 0x00000000);
		nv_mask(dev, 0x6100a0, 0x00000001, 0x00000000);
	}
}

int
nvd0_display_init(struct drm_device *dev)
{
	struct nvd0_display *disp = nvd0_display(dev);
	u32 *push;
	int i;

	if (nv_rd32(dev, 0x6100ac) & 0x00000100) {
		nv_wr32(dev, 0x6100ac, 0x00000100);
		nv_mask(dev, 0x6194e8, 0x00000001, 0x00000000);
		if (!nv_wait(dev, 0x6194e8, 0x00000002, 0x00000000)) {
			NV_ERROR(dev, "PDISP: 0x6194e8 0x%08x\n",
				 nv_rd32(dev, 0x6194e8));
			return -EBUSY;
		}
	}

	nv_wr32(dev, 0x610010, (disp->mem->vinst >> 8) | 9);

	/* init master */
	nv_wr32(dev, 0x610494, (disp->evo[0].handle >> 8) | 3);
	nv_wr32(dev, 0x610498, 0x00010000);
	nv_wr32(dev, 0x61049c, 0x00000001);
	nv_mask(dev, 0x610490, 0x00000010, 0x00000010);
	nv_wr32(dev, 0x640000, 0x00000000);
	nv_wr32(dev, 0x610490, 0x01000013);
	if (!nv_wait(dev, 0x610490, 0x80000000, 0x00000000)) {
		NV_ERROR(dev, "PDISP: master 0x%08x\n",
			 nv_rd32(dev, 0x610490));
		return -EBUSY;
	}
	nv_mask(dev, 0x610090, 0x00000001, 0x00000001);
	nv_mask(dev, 0x6100a0, 0x00000001, 0x00000001);

	/* init cursors */
	for (i = 13; i <= 14; i++) {
		nv_wr32(dev, 0x610490 + (i * 0x10), 0x00000001);
		if (!nv_wait(dev, 0x610490 + (i * 0x10), 0x00010000, 0x00010000)) {
			NV_ERROR(dev, "PDISP: curs%d 0x%08x\n", i,
				 nv_rd32(dev, 0x610490 + (i * 0x10)));
			return -EBUSY;
		}

		nv_mask(dev, 0x610090, 1 << i, 1 << i);
		nv_mask(dev, 0x6100a0, 1 << i, 1 << i);
	}

	push = evo_wait(dev, 0, 32);
	if (!push)
		return -EBUSY;
	evo_mthd(push, 0x0088, 1);
	evo_data(push, MEM_SYNC);
	evo_mthd(push, 0x0084, 1);
	evo_data(push, 0x00000000);
	evo_mthd(push, 0x0084, 1);
	evo_data(push, 0x80000000);
	evo_mthd(push, 0x008c, 1);
	evo_data(push, 0x00000000);
	evo_kick(push, dev, 0);

	return 0;
}

void
nvd0_display_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvd0_display *disp = nvd0_display(dev);
	struct pci_dev *pdev = dev->pdev;

	nvd0_display_fini(dev);

	pci_free_consistent(pdev, PAGE_SIZE, disp->evo[0].ptr, disp->evo[0].handle);
	nouveau_gpuobj_ref(NULL, &disp->mem);
	nouveau_irq_unregister(dev, 26);

	dev_priv->engine.display.priv = NULL;
	kfree(disp);
}

int
nvd0_display_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct dcb_table *dcb = &dev_priv->vbios.dcb;
	struct drm_connector *connector, *tmp;
	struct pci_dev *pdev = dev->pdev;
	struct nvd0_display *disp;
	struct dcb_entry *dcbe;
	int ret, i;

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;
	dev_priv->engine.display.priv = disp;

	/* create encoder/connector objects based on VBIOS DCB table */
	for (i = 0, dcbe = &dcb->entry[0]; i < dcb->entries; i++, dcbe++) {
		connector = nouveau_connector_create(dev, dcbe->connector);
		if (IS_ERR(connector))
			continue;

		if (dcbe->location != DCB_LOC_ON_CHIP) {
			NV_WARN(dev, "skipping off-chip encoder %d/%d\n",
				dcbe->type, ffs(dcbe->or) - 1);
			continue;
		}

		switch (dcbe->type) {
		case OUTPUT_TMDS:
			nvd0_sor_create(connector, dcbe);
			break;
		default:
			NV_WARN(dev, "skipping unsupported encoder %d/%d\n",
				dcbe->type, ffs(dcbe->or) - 1);
			continue;
		}
	}

	/* cull any connectors we created that don't have an encoder */
	list_for_each_entry_safe(connector, tmp, &dev->mode_config.connector_list, head) {
		if (connector->encoder_ids[0])
			continue;

		NV_WARN(dev, "%s has no encoders, removing\n",
			drm_get_connector_name(connector));
		connector->funcs->destroy(connector);
	}

	/* setup interrupt handling */
	nouveau_irq_register(dev, 26, nvd0_display_intr);

	/* hash table and dma objects for the memory areas we care about */
	ret = nouveau_gpuobj_new(dev, NULL, 0x4000, 0x10000,
				 NVOBJ_FLAG_ZERO_ALLOC, &disp->mem);
	if (ret)
		goto out;

	nv_wo32(disp->mem, 0x1000, 0x00000049);
	nv_wo32(disp->mem, 0x1004, (disp->mem->vinst + 0x2000) >> 8);
	nv_wo32(disp->mem, 0x1008, (disp->mem->vinst + 0x2fff) >> 8);
	nv_wo32(disp->mem, 0x100c, 0x00000000);
	nv_wo32(disp->mem, 0x1010, 0x00000000);
	nv_wo32(disp->mem, 0x1014, 0x00000000);
	nv_wo32(disp->mem, 0x0000, MEM_SYNC);
	nv_wo32(disp->mem, 0x0004, (0x1000 << 9) | 0x00000001);

	nv_wo32(disp->mem, 0x1020, 0x00000009);
	nv_wo32(disp->mem, 0x1024, 0x00000000);
	nv_wo32(disp->mem, 0x1028, (dev_priv->vram_size - 1) >> 8);
	nv_wo32(disp->mem, 0x102c, 0x00000000);
	nv_wo32(disp->mem, 0x1030, 0x00000000);
	nv_wo32(disp->mem, 0x1034, 0x00000000);
	nv_wo32(disp->mem, 0x0008, MEM_VRAM);
	nv_wo32(disp->mem, 0x000c, (0x1020 << 9) | 0x00000001);

	pinstmem->flush(dev);

	/* push buffers for evo channels */
	disp->evo[0].ptr =
		pci_alloc_consistent(pdev, PAGE_SIZE, &disp->evo[0].handle);
	if (!disp->evo[0].ptr) {
		ret = -ENOMEM;
		goto out;
	}

	ret = nvd0_display_init(dev);
	if (ret)
		goto out;

out:
	if (ret)
		nvd0_display_destroy(dev);
	return ret;
}
