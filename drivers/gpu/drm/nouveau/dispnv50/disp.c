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
#include "disp.h"
#include "atom.h"
#include "core.h"
#include "head.h"
#include "wndw.h"

#include <linux/dma-mapping.h>
#include <linux/hdmi.h>
#include <linux/component.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_scdc_helper.h>
#include <drm/drm_vblank.h>

#include <nvif/class.h>
#include <nvif/cl0002.h>
#include <nvif/cl5070.h>
#include <nvif/cl507d.h>
#include <nvif/event.h>
#include <nvif/timer.h>

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_gem.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_fence.h"
#include "nouveau_fbcon.h"

#include <subdev/bios/dp.h>

/******************************************************************************
 * Atomic state
 *****************************************************************************/

struct nv50_outp_atom {
	struct list_head head;

	struct drm_encoder *encoder;
	bool flush_disable;

	union nv50_outp_atom_mask {
		struct {
			bool ctrl:1;
		};
		u8 mask;
	} set, clr;
};

/******************************************************************************
 * EVO channel
 *****************************************************************************/

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
					nvif_object_map(&chan->user, NULL, 0);
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
 * DMA EVO channel
 *****************************************************************************/

void
nv50_dmac_destroy(struct nv50_dmac *dmac)
{
	nvif_object_fini(&dmac->vram);
	nvif_object_fini(&dmac->sync);

	nv50_chan_destroy(&dmac->base);

	nvif_mem_fini(&dmac->push);
}

int
nv50_dmac_create(struct nvif_device *device, struct nvif_object *disp,
		 const s32 *oclass, u8 head, void *data, u32 size, u64 syncbuf,
		 struct nv50_dmac *dmac)
{
	struct nouveau_cli *cli = (void *)device->object.client;
	struct nv50_disp_core_channel_dma_v0 *args = data;
	u8 type = NVIF_MEM_COHERENT;
	int ret;

	mutex_init(&dmac->lock);

	/* Pascal added support for 47-bit physical addresses, but some
	 * parts of EVO still only accept 40-bit PAs.
	 *
	 * To avoid issues on systems with large amounts of RAM, and on
	 * systems where an IOMMU maps pages at a high address, we need
	 * to allocate push buffers in VRAM instead.
	 *
	 * This appears to match NVIDIA's behaviour on Pascal.
	 */
	if (device->info.family == NV_DEVICE_INFO_V0_PASCAL)
		type |= NVIF_MEM_VRAM;

	ret = nvif_mem_init_map(&cli->mmu, type, 0x1000, &dmac->push);
	if (ret)
		return ret;

	dmac->ptr = dmac->push.object.map.ptr;

	args->pushbuf = nvif_handle(&dmac->push.object);

	ret = nv50_chan_create(device, disp, oclass, head, data, size,
			       &dmac->base);
	if (ret)
		return ret;

	if (!syncbuf)
		return 0;

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
 * EVO channel helpers
 *****************************************************************************/
static void
evo_flush(struct nv50_dmac *dmac)
{
	/* Push buffer fetches are not coherent with BAR1, we need to ensure
	 * writes have been flushed right through to VRAM before writing PUT.
	 */
	if (dmac->push.type & NVIF_MEM_VRAM) {
		struct nvif_device *device = dmac->base.device;
		nvif_wr32(&device->object, 0x070000, 0x00000001);
		nvif_msec(device, 2000,
			if (!(nvif_rd32(&device->object, 0x070000) & 0x00000002))
				break;
		);
	}
}

u32 *
evo_wait(struct nv50_dmac *evoc, int nr)
{
	struct nv50_dmac *dmac = evoc;
	struct nvif_device *device = dmac->base.device;
	u32 put = nvif_rd32(&dmac->base.user, 0x0000) / 4;

	mutex_lock(&dmac->lock);
	if (put + nr >= (PAGE_SIZE / 4) - 8) {
		dmac->ptr[put] = 0x20000000;
		evo_flush(dmac);

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

void
evo_kick(u32 *push, struct nv50_dmac *evoc)
{
	struct nv50_dmac *dmac = evoc;

	evo_flush(dmac);

	nvif_wr32(&dmac->base.user, 0x0000, (push - dmac->ptr) << 2);
	mutex_unlock(&dmac->lock);
}

/******************************************************************************
 * Output path helpers
 *****************************************************************************/
static void
nv50_outp_release(struct nouveau_encoder *nv_encoder)
{
	struct nv50_disp *disp = nv50_disp(nv_encoder->base.base.dev);
	struct {
		struct nv50_disp_mthd_v1 base;
	} args = {
		.base.version = 1,
		.base.method = NV50_DISP_MTHD_V1_RELEASE,
		.base.hasht  = nv_encoder->dcb->hasht,
		.base.hashm  = nv_encoder->dcb->hashm,
	};

	nvif_mthd(&disp->disp->object, 0, &args, sizeof(args));
	nv_encoder->or = -1;
	nv_encoder->link = 0;
}

static int
nv50_outp_acquire(struct nouveau_encoder *nv_encoder, bool hda)
{
	struct nouveau_drm *drm = nouveau_drm(nv_encoder->base.base.dev);
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_acquire_v0 info;
	} args = {
		.base.version = 1,
		.base.method = NV50_DISP_MTHD_V1_ACQUIRE,
		.base.hasht  = nv_encoder->dcb->hasht,
		.base.hashm  = nv_encoder->dcb->hashm,
		.info.hda = hda,
	};
	int ret;

	ret = nvif_mthd(&disp->disp->object, 0, &args, sizeof(args));
	if (ret) {
		NV_ERROR(drm, "error acquiring output path: %d\n", ret);
		return ret;
	}

	nv_encoder->or = args.info.or;
	nv_encoder->link = args.info.link;
	return 0;
}

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
			/* Don't force scaler for EDID modes with
			 * same size as the native one (e.g. different
			 * refresh rate)
			 */
			if (mode->hdisplay == native_mode->hdisplay &&
			    mode->vdisplay == native_mode->vdisplay &&
			    mode->type & DRM_MODE_TYPE_DRIVER)
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
	struct drm_connector *connector = conn_state->connector;
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nv50_head_atom *asyh = nv50_head_atom(crtc_state);
	int ret;

	ret = nv50_outp_atomic_check_view(encoder, crtc_state, conn_state,
					  nv_connector->native_mode);
	if (ret)
		return ret;

	if (crtc_state->mode_changed || crtc_state->connectors_changed)
		asyh->or.bpc = connector->display_info.bpc;

	return 0;
}

/******************************************************************************
 * DAC
 *****************************************************************************/
static void
nv50_dac_disable(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nv50_core *core = nv50_disp(encoder->dev)->core;
	if (nv_encoder->crtc)
		core->func->dac->ctrl(core, nv_encoder->or, 0x00000000, NULL);
	nv_encoder->crtc = NULL;
	nv50_outp_release(nv_encoder);
}

static void
nv50_dac_enable(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nv50_head_atom *asyh = nv50_head_atom(nv_crtc->base.state);
	struct nv50_core *core = nv50_disp(encoder->dev)->core;

	nv50_outp_acquire(nv_encoder, false);

	core->func->dac->ctrl(core, nv_encoder->or, 1 << nv_crtc->index, asyh);
	asyh->or.depth = 0;

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

	ret = nvif_mthd(&disp->disp->object, 0, &args, sizeof(args));
	if (ret || !args.load.load)
		return connector_status_disconnected;

	return connector_status_connected;
}

static const struct drm_encoder_helper_funcs
nv50_dac_help = {
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

	bus = nvkm_i2c_bus_find(i2c, dcbe->i2c_index);
	if (bus)
		nv_encoder->i2c = &bus->i2c;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(connector->dev, encoder, &nv50_dac_func, type,
			 "dac-%04x-%04x", dcbe->hasht, dcbe->hashm);
	drm_encoder_helper_add(encoder, &nv50_dac_help);

	drm_connector_attach_encoder(connector, encoder);
	return 0;
}

/*
 * audio component binding for ELD notification
 */
static void
nv50_audio_component_eld_notify(struct drm_audio_component *acomp, int port,
				int dev_id)
{
	if (acomp && acomp->audio_ops && acomp->audio_ops->pin_eld_notify)
		acomp->audio_ops->pin_eld_notify(acomp->audio_ops->audio_ptr,
						 port, dev_id);
}

static int
nv50_audio_component_get_eld(struct device *kdev, int port, int dev_id,
			     bool *enabled, unsigned char *buf, int max_bytes)
{
	struct drm_device *drm_dev = dev_get_drvdata(kdev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct drm_encoder *encoder;
	struct nouveau_encoder *nv_encoder;
	struct nouveau_connector *nv_connector;
	struct nouveau_crtc *nv_crtc;
	int ret = 0;

	*enabled = false;
	drm_for_each_encoder(encoder, drm->dev) {
		nv_encoder = nouveau_encoder(encoder);
		nv_connector = nouveau_encoder_connector_get(nv_encoder);
		nv_crtc = nouveau_crtc(encoder->crtc);
		if (!nv_connector || !nv_crtc || nv_encoder->or != port ||
		    nv_crtc->index != dev_id)
			continue;
		*enabled = nv_encoder->audio;
		if (*enabled) {
			ret = drm_eld_size(nv_connector->base.eld);
			memcpy(buf, nv_connector->base.eld,
			       min(max_bytes, ret));
		}
		break;
	}
	return ret;
}

static const struct drm_audio_component_ops nv50_audio_component_ops = {
	.get_eld = nv50_audio_component_get_eld,
};

static int
nv50_audio_component_bind(struct device *kdev, struct device *hda_kdev,
			  void *data)
{
	struct drm_device *drm_dev = dev_get_drvdata(kdev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct drm_audio_component *acomp = data;

	if (WARN_ON(!device_link_add(hda_kdev, kdev, DL_FLAG_STATELESS)))
		return -ENOMEM;

	drm_modeset_lock_all(drm_dev);
	acomp->ops = &nv50_audio_component_ops;
	acomp->dev = kdev;
	drm->audio.component = acomp;
	drm_modeset_unlock_all(drm_dev);
	return 0;
}

static void
nv50_audio_component_unbind(struct device *kdev, struct device *hda_kdev,
			    void *data)
{
	struct drm_device *drm_dev = dev_get_drvdata(kdev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct drm_audio_component *acomp = data;

	drm_modeset_lock_all(drm_dev);
	drm->audio.component = NULL;
	acomp->ops = NULL;
	acomp->dev = NULL;
	drm_modeset_unlock_all(drm_dev);
}

static const struct component_ops nv50_audio_component_bind_ops = {
	.bind   = nv50_audio_component_bind,
	.unbind = nv50_audio_component_unbind,
};

static void
nv50_audio_component_init(struct nouveau_drm *drm)
{
	if (!component_add(drm->dev->dev, &nv50_audio_component_bind_ops))
		drm->audio.component_registered = true;
}

static void
nv50_audio_component_fini(struct nouveau_drm *drm)
{
	if (drm->audio.component_registered) {
		component_del(drm->dev->dev, &nv50_audio_component_bind_ops);
		drm->audio.component_registered = false;
	}
}

/******************************************************************************
 * Audio
 *****************************************************************************/
static void
nv50_audio_disable(struct drm_encoder *encoder, struct nouveau_crtc *nv_crtc)
{
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);
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

	if (!nv_encoder->audio)
		return;

	nv_encoder->audio = false;
	nvif_mthd(&disp->disp->object, 0, &args, sizeof(args));

	nv50_audio_component_eld_notify(drm->audio.component, nv_encoder->or,
					nv_crtc->index);
}

static void
nv50_audio_enable(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);
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

	memcpy(args.data, nv_connector->base.eld, sizeof(args.data));

	nvif_mthd(&disp->disp->object, 0, &args,
		  sizeof(args.base) + drm_eld_size(args.data));
	nv_encoder->audio = true;

	nv50_audio_component_eld_notify(drm->audio.component, nv_encoder->or,
					nv_crtc->index);
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

	nvif_mthd(&disp->disp->object, 0, &args, sizeof(args));
}

static void
nv50_hdmi_enable(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nv50_disp *disp = nv50_disp(encoder->dev);
	struct {
		struct nv50_disp_mthd_v1 base;
		struct nv50_disp_sor_hdmi_pwr_v0 pwr;
		u8 infoframes[2 * 17]; /* two frames, up to 17 bytes each */
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
	struct drm_hdmi_info *hdmi;
	u32 max_ac_packet;
	union hdmi_infoframe avi_frame;
	union hdmi_infoframe vendor_frame;
	bool high_tmds_clock_ratio = false, scrambling = false;
	u8 config;
	int ret;
	int size;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!drm_detect_hdmi_monitor(nv_connector->edid))
		return;

	hdmi = &nv_connector->base.display_info.hdmi;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&avi_frame.avi,
						       &nv_connector->base, mode);
	if (!ret) {
		/* We have an AVI InfoFrame, populate it to the display */
		args.pwr.avi_infoframe_length
			= hdmi_infoframe_pack(&avi_frame, args.infoframes, 17);
	}

	ret = drm_hdmi_vendor_infoframe_from_display_mode(&vendor_frame.vendor.hdmi,
							  &nv_connector->base, mode);
	if (!ret) {
		/* We have a Vendor InfoFrame, populate it to the display */
		args.pwr.vendor_infoframe_length
			= hdmi_infoframe_pack(&vendor_frame,
					      args.infoframes
					      + args.pwr.avi_infoframe_length,
					      17);
	}

	max_ac_packet  = mode->htotal - mode->hdisplay;
	max_ac_packet -= args.pwr.rekey;
	max_ac_packet -= 18; /* constant from tegra */
	args.pwr.max_ac_packet = max_ac_packet / 32;

	if (hdmi->scdc.scrambling.supported) {
		high_tmds_clock_ratio = mode->clock > 340000;
		scrambling = high_tmds_clock_ratio ||
			hdmi->scdc.scrambling.low_rates;
	}

	args.pwr.scdc =
		NV50_DISP_SOR_HDMI_PWR_V0_SCDC_SCRAMBLE * scrambling |
		NV50_DISP_SOR_HDMI_PWR_V0_SCDC_DIV_BY_4 * high_tmds_clock_ratio;

	size = sizeof(args.base)
		+ sizeof(args.pwr)
		+ args.pwr.avi_infoframe_length
		+ args.pwr.vendor_infoframe_length;
	nvif_mthd(&disp->disp->object, 0, &args, size);

	nv50_audio_enable(encoder, mode);

	/* If SCDC is supported by the downstream monitor, update
	 * divider / scrambling settings to what we programmed above.
	 */
	if (!hdmi->scdc.scrambling.supported)
		return;

	ret = drm_scdc_readb(nv_encoder->i2c, SCDC_TMDS_CONFIG, &config);
	if (ret < 0) {
		NV_ERROR(drm, "Failure to read SCDC_TMDS_CONFIG: %d\n", ret);
		return;
	}
	config &= ~(SCDC_TMDS_BIT_CLOCK_RATIO_BY_40 | SCDC_SCRAMBLING_ENABLE);
	config |= SCDC_TMDS_BIT_CLOCK_RATIO_BY_40 * high_tmds_clock_ratio;
	config |= SCDC_SCRAMBLING_ENABLE * scrambling;
	ret = drm_scdc_writeb(nv_encoder->i2c, SCDC_TMDS_CONFIG, config);
	if (ret < 0)
		NV_ERROR(drm, "Failure to write SCDC_TMDS_CONFIG = 0x%02x: %d\n",
			 config, ret);
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

	bool modified;
	bool disabled;
	int links;
};

struct nv50_mstc {
	struct nv50_mstm *mstm;
	struct drm_dp_mst_port *port;
	struct drm_connector connector;

	struct drm_display_mode *native;
	struct edid *edid;
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

	WARN_ON(!mutex_is_locked(&mstm->mgr.payload_lock));

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

	if (!msto->disabled)
		return;

	NV_ATOMIC(drm, "%s: msto cleanup\n", msto->encoder.name);

	drm_dp_mst_deallocate_vcpi(&mstm->mgr, mstc->port);

	msto->mstc = NULL;
	msto->disabled = false;
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

	mutex_lock(&mstm->mgr.payload_lock);

	NV_ATOMIC(drm, "%s: msto prepare\n", msto->encoder.name);
	if (mstc->port->vcpi.vcpi > 0) {
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

	nvif_mthd(&drm->display->disp.object, 0, &args, sizeof(args));
	mutex_unlock(&mstm->mgr.payload_lock);
}

static int
nv50_msto_atomic_check(struct drm_encoder *encoder,
		       struct drm_crtc_state *crtc_state,
		       struct drm_connector_state *conn_state)
{
	struct drm_atomic_state *state = crtc_state->state;
	struct drm_connector *connector = conn_state->connector;
	struct nv50_mstc *mstc = nv50_mstc(connector);
	struct nv50_mstm *mstm = mstc->mstm;
	struct nv50_head_atom *asyh = nv50_head_atom(crtc_state);
	int slots;
	int ret;

	ret = nv50_outp_atomic_check_view(encoder, crtc_state, conn_state,
					  mstc->native);
	if (ret)
		return ret;

	if (!crtc_state->mode_changed && !crtc_state->connectors_changed)
		return 0;

	/*
	 * When restoring duplicated states, we need to make sure that the bw
	 * remains the same and avoid recalculating it, as the connector's bpc
	 * may have changed after the state was duplicated
	 */
	if (!state->duplicated) {
		const int clock = crtc_state->adjusted_mode.clock;

		asyh->or.bpc = connector->display_info.bpc;
		asyh->dp.pbn = drm_dp_calc_pbn_mode(clock, asyh->or.bpc * 3,
						    false);
	}

	slots = drm_dp_atomic_find_vcpi_slots(state, &mstm->mgr, mstc->port,
					      asyh->dp.pbn, 0);
	if (slots < 0)
		return slots;

	asyh->dp.tu = slots;

	return 0;
}

static u8
nv50_dp_bpc_to_depth(unsigned int bpc)
{
	switch (bpc) {
	case  6: return 0x2;
	case  8: return 0x5;
	case 10: /* fall-through */
	default: return 0x6;
	}
}

static void
nv50_msto_enable(struct drm_encoder *encoder)
{
	struct nv50_head *head = nv50_head(encoder->crtc);
	struct nv50_head_atom *armh = nv50_head_atom(head->base.base.state);
	struct nv50_msto *msto = nv50_msto(encoder);
	struct nv50_mstc *mstc = NULL;
	struct nv50_mstm *mstm = NULL;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	u8 proto;
	bool r;

	drm_connector_list_iter_begin(encoder->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->state->best_encoder == &msto->encoder) {
			mstc = nv50_mstc(connector);
			mstm = mstc->mstm;
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	if (WARN_ON(!mstc))
		return;

	r = drm_dp_mst_allocate_vcpi(&mstm->mgr, mstc->port, armh->dp.pbn,
				     armh->dp.tu);
	if (!r)
		DRM_DEBUG_KMS("Failed to allocate VCPI\n");

	if (!mstm->links++)
		nv50_outp_acquire(mstm->outp, false /*XXX: MST audio.*/);

	if (mstm->outp->link & 1)
		proto = 0x8;
	else
		proto = 0x9;

	mstm->outp->update(mstm->outp, head->base.index, armh, proto,
			   nv50_dp_bpc_to_depth(armh->or.bpc));

	msto->mstc = mstc;
	mstm->modified = true;
}

static void
nv50_msto_disable(struct drm_encoder *encoder)
{
	struct nv50_msto *msto = nv50_msto(encoder);
	struct nv50_mstc *mstc = msto->mstc;
	struct nv50_mstm *mstm = mstc->mstm;

	drm_dp_mst_reset_vcpi_slots(&mstm->mgr, mstc->port);

	mstm->outp->update(mstm->outp, msto->head->base.index, NULL, 0, 0);
	mstm->modified = true;
	if (!--mstm->links)
		mstm->disabled = true;
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

static struct nv50_msto *
nv50_msto_new(struct drm_device *dev, struct nv50_head *head, int id)
{
	struct nv50_msto *msto;
	int ret;

	msto = kzalloc(sizeof(*msto), GFP_KERNEL);
	if (!msto)
		return ERR_PTR(-ENOMEM);

	ret = drm_encoder_init(dev, &msto->encoder, &nv50_msto,
			       DRM_MODE_ENCODER_DPMST, "mst-%d", id);
	if (ret) {
		kfree(msto);
		return ERR_PTR(ret);
	}

	drm_encoder_helper_add(&msto->encoder, &nv50_msto_help);
	msto->encoder.possible_crtcs = drm_crtc_mask(&head->base.base);
	msto->head = head;
	return msto;
}

static struct drm_encoder *
nv50_mstc_atomic_best_encoder(struct drm_connector *connector,
			      struct drm_connector_state *connector_state)
{
	struct nv50_mstc *mstc = nv50_mstc(connector);
	struct drm_crtc *crtc = connector_state->crtc;

	if (!(mstc->mstm->outp->dcb->heads & drm_crtc_mask(crtc)))
		return NULL;

	return &nv50_head(crtc)->msto->encoder;
}

static enum drm_mode_status
nv50_mstc_mode_valid(struct drm_connector *connector,
		     struct drm_display_mode *mode)
{
	struct nv50_mstc *mstc = nv50_mstc(connector);
	struct nouveau_encoder *outp = mstc->mstm->outp;

	/* TODO: calculate the PBN from the dotclock and validate against the
	 * MSTB's max possible PBN
	 */

	return nv50_dp_mode_valid(connector, outp, mode, NULL);
}

static int
nv50_mstc_get_modes(struct drm_connector *connector)
{
	struct nv50_mstc *mstc = nv50_mstc(connector);
	int ret = 0;

	mstc->edid = drm_dp_mst_get_edid(&mstc->connector, mstc->port->mgr, mstc->port);
	drm_connector_update_edid_property(&mstc->connector, mstc->edid);
	if (mstc->edid)
		ret = drm_add_edid_modes(&mstc->connector, mstc->edid);

	/*
	 * XXX: Since we don't use HDR in userspace quite yet, limit the bpc
	 * to 8 to save bandwidth on the topology. In the future, we'll want
	 * to properly fix this by dynamically selecting the highest possible
	 * bpc that would fit in the topology
	 */
	if (connector->display_info.bpc)
		connector->display_info.bpc =
			clamp(connector->display_info.bpc, 6U, 8U);
	else
		connector->display_info.bpc = 8;

	if (mstc->native)
		drm_mode_destroy(mstc->connector.dev, mstc->native);
	mstc->native = nouveau_conn_native_mode(&mstc->connector);
	return ret;
}

static int
nv50_mstc_atomic_check(struct drm_connector *connector,
		       struct drm_atomic_state *state)
{
	struct nv50_mstc *mstc = nv50_mstc(connector);
	struct drm_dp_mst_topology_mgr *mgr = &mstc->mstm->mgr;
	struct drm_connector_state *new_conn_state =
		drm_atomic_get_new_connector_state(state, connector);
	struct drm_connector_state *old_conn_state =
		drm_atomic_get_old_connector_state(state, connector);
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *new_crtc = new_conn_state->crtc;

	if (!old_conn_state->crtc)
		return 0;

	/* We only want to free VCPI if this state disables the CRTC on this
	 * connector
	 */
	if (new_crtc) {
		crtc_state = drm_atomic_get_new_crtc_state(state, new_crtc);

		if (!crtc_state ||
		    !drm_atomic_crtc_needs_modeset(crtc_state) ||
		    crtc_state->enable)
			return 0;
	}

	return drm_dp_atomic_release_vcpi_slots(state, mgr, mstc->port);
}

static int
nv50_mstc_detect(struct drm_connector *connector,
		 struct drm_modeset_acquire_ctx *ctx, bool force)
{
	struct nv50_mstc *mstc = nv50_mstc(connector);
	int ret;

	if (drm_connector_is_unregistered(connector))
		return connector_status_disconnected;

	ret = pm_runtime_get_sync(connector->dev->dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_autosuspend(connector->dev->dev);
		return connector_status_disconnected;
	}

	ret = drm_dp_mst_detect_port(connector, ctx, mstc->port->mgr,
				     mstc->port);

	pm_runtime_mark_last_busy(connector->dev->dev);
	pm_runtime_put_autosuspend(connector->dev->dev);
	return ret;
}

static const struct drm_connector_helper_funcs
nv50_mstc_help = {
	.get_modes = nv50_mstc_get_modes,
	.mode_valid = nv50_mstc_mode_valid,
	.atomic_best_encoder = nv50_mstc_atomic_best_encoder,
	.atomic_check = nv50_mstc_atomic_check,
	.detect_ctx = nv50_mstc_detect,
};

static void
nv50_mstc_destroy(struct drm_connector *connector)
{
	struct nv50_mstc *mstc = nv50_mstc(connector);

	drm_connector_cleanup(&mstc->connector);
	drm_dp_mst_put_port_malloc(mstc->port);

	kfree(mstc);
}

static const struct drm_connector_funcs
nv50_mstc = {
	.reset = nouveau_conn_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
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
	struct drm_crtc *crtc;
	struct nv50_mstc *mstc;
	int ret;

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

	drm_for_each_crtc(crtc, dev) {
		if (!(mstm->outp->dcb->heads & drm_crtc_mask(crtc)))
			continue;

		drm_connector_attach_encoder(&mstc->connector,
					     &nv50_head(crtc)->msto->encoder);
	}

	drm_object_attach_property(&mstc->connector.base, dev->mode_config.path_property, 0);
	drm_object_attach_property(&mstc->connector.base, dev->mode_config.tile_property, 0);
	drm_connector_set_path_property(&mstc->connector, path);
	drm_dp_mst_get_port_malloc(port);
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

	if (mstm->disabled) {
		if (!mstm->links)
			nv50_outp_release(mstm->outp);
		mstm->disabled = false;
	}
}

static struct drm_connector *
nv50_mstm_add_connector(struct drm_dp_mst_topology_mgr *mgr,
			struct drm_dp_mst_port *port, const char *path)
{
	struct nv50_mstm *mstm = nv50_mstm(mgr);
	struct nv50_mstc *mstc;
	int ret;

	ret = nv50_mstc_new(mstm, port, path, &mstc);
	if (ret)
		return NULL;

	return &mstc->connector;
}

static const struct drm_dp_mst_topology_cbs
nv50_mstm = {
	.add_connector = nv50_mstm_add_connector,
};

void
nv50_mstm_service(struct nv50_mstm *mstm)
{
	struct drm_dp_aux *aux = mstm ? mstm->mgr.aux : NULL;
	bool handled = true;
	int ret;
	u8 esi[8] = {};

	if (!aux)
		return;

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
	struct nvif_object *disp = &drm->display->disp.object;
	int ret;

	if (dpcd >= 0x12) {
		/* Even if we're enabling MST, start with disabling the
		 * branching unit to clear any sink-side MST topology state
		 * that wasn't set by us
		 */
		ret = drm_dp_dpcd_writeb(mstm->mgr.aux, DP_MSTM_CTRL, 0);
		if (ret < 0)
			return ret;

		if (state) {
			/* Now, start initializing */
			ret = drm_dp_dpcd_writeb(mstm->mgr.aux, DP_MSTM_CTRL,
						 DP_MST_EN);
			if (ret < 0)
				return ret;
		}
	}

	return nvif_mthd(disp, 0, &args, sizeof(args));
}

int
nv50_mstm_detect(struct nv50_mstm *mstm, u8 dpcd[8], int allow)
{
	struct drm_dp_aux *aux;
	int ret;
	bool old_state, new_state;
	u8 mstm_ctrl;

	if (!mstm)
		return 0;

	mutex_lock(&mstm->mgr.lock);

	old_state = mstm->mgr.mst_state;
	new_state = old_state;
	aux = mstm->mgr.aux;

	if (old_state) {
		/* Just check that the MST hub is still as we expect it */
		ret = drm_dp_dpcd_readb(aux, DP_MSTM_CTRL, &mstm_ctrl);
		if (ret < 0 || !(mstm_ctrl & DP_MST_EN)) {
			DRM_DEBUG_KMS("Hub gone, disabling MST topology\n");
			new_state = false;
		}
	} else if (dpcd[0] >= 0x12) {
		ret = drm_dp_dpcd_readb(aux, DP_MSTM_CAP, &dpcd[1]);
		if (ret < 0)
			goto probe_error;

		if (!(dpcd[1] & DP_MST_CAP))
			dpcd[0] = 0x11;
		else
			new_state = allow;
	}

	if (new_state == old_state) {
		mutex_unlock(&mstm->mgr.lock);
		return new_state;
	}

	ret = nv50_mstm_enable(mstm, dpcd[0], new_state);
	if (ret)
		goto probe_error;

	mutex_unlock(&mstm->mgr.lock);

	ret = drm_dp_mst_topology_mgr_set_mst(&mstm->mgr, new_state);
	if (ret)
		return nv50_mstm_enable(mstm, dpcd[0], 0);

	return new_state;

probe_error:
	mutex_unlock(&mstm->mgr.lock);
	return ret;
}

static void
nv50_mstm_fini(struct nv50_mstm *mstm)
{
	if (mstm && mstm->mgr.mst_state)
		drm_dp_mst_topology_mgr_suspend(&mstm->mgr);
}

static void
nv50_mstm_init(struct nv50_mstm *mstm, bool runtime)
{
	int ret;

	if (!mstm || !mstm->mgr.mst_state)
		return;

	ret = drm_dp_mst_topology_mgr_resume(&mstm->mgr, !runtime);
	if (ret == -1) {
		drm_dp_mst_topology_mgr_set_mst(&mstm->mgr, false);
		drm_kms_helper_hotplug_event(mstm->mgr.dev);
	}
}

static void
nv50_mstm_del(struct nv50_mstm **pmstm)
{
	struct nv50_mstm *mstm = *pmstm;
	if (mstm) {
		drm_dp_mst_topology_mgr_destroy(&mstm->mgr);
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

	return 0;
}

/******************************************************************************
 * SOR
 *****************************************************************************/
static void
nv50_sor_update(struct nouveau_encoder *nv_encoder, u8 head,
		struct nv50_head_atom *asyh, u8 proto, u8 depth)
{
	struct nv50_disp *disp = nv50_disp(nv_encoder->base.base.dev);
	struct nv50_core *core = disp->core;

	if (!asyh) {
		nv_encoder->ctrl &= ~BIT(head);
		if (!(nv_encoder->ctrl & 0x0000000f))
			nv_encoder->ctrl = 0;
	} else {
		nv_encoder->ctrl |= proto << 8;
		nv_encoder->ctrl |= BIT(head);
		asyh->or.depth = depth;
	}

	core->func->sor->ctrl(core, nv_encoder->or, nv_encoder->ctrl, asyh);
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
		nv50_outp_release(nv_encoder);
	}
}

static void
nv50_sor_enable(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nv50_head_atom *asyh = nv50_head_atom(nv_crtc->base.state);
	struct drm_display_mode *mode = &asyh->state.adjusted_mode;
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
	bool hda = false;
	u8 proto = 0xf;
	u8 depth = 0x0;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	nv_encoder->crtc = encoder->crtc;

	if ((disp->disp->object.oclass == GT214_DISP ||
	     disp->disp->object.oclass >= GF110_DISP) &&
	    drm_detect_monitor_audio(nv_connector->edid))
		hda = true;
	nv50_outp_acquire(nv_encoder, hda);

	switch (nv_encoder->dcb->type) {
	case DCB_OUTPUT_TMDS:
		if (nv_encoder->link & 1) {
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

			if (asyh->or.bpc == 8)
				lvds.lvds.script |= 0x0200;
		}

		nvif_mthd(&disp->disp->object, 0, &lvds, sizeof(lvds));
		break;
	case DCB_OUTPUT_DP:
		depth = nv50_dp_bpc_to_depth(asyh->or.bpc);

		if (nv_encoder->link & 1)
			proto = 0x8;
		else
			proto = 0x9;

		nv50_audio_enable(encoder, mode);
		break;
	default:
		BUG();
		break;
	}

	nv_encoder->update(nv_encoder, nv_crtc->index, asyh, proto, depth);
}

static const struct drm_encoder_helper_funcs
nv50_sor_help = {
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

static bool nv50_has_mst(struct nouveau_drm *drm)
{
	struct nvkm_bios *bios = nvxx_bios(&drm->client.device);
	u32 data;
	u8 ver, hdr, cnt, len;

	data = nvbios_dp_table(bios, &ver, &hdr, &cnt, &len);
	return data && ver >= 0x40 && (nvbios_rd08(bios, data + 0x08) & 0x04);
}

static int
nv50_sor_create(struct drm_connector *connector, struct dcb_output *dcbe)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_drm *drm = nouveau_drm(connector->dev);
	struct nvkm_i2c *i2c = nvxx_i2c(&drm->client.device);
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;
	struct nv50_disp *disp = nv50_disp(connector->dev);
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
	nv_encoder->update = nv50_sor_update;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(connector->dev, encoder, &nv50_sor_func, type,
			 "sor-%04x-%04x", dcbe->hasht, dcbe->hashm);
	drm_encoder_helper_add(encoder, &nv50_sor_help);

	drm_connector_attach_encoder(connector, encoder);

	disp->core->func->sor->get_caps(disp, nv_encoder, ffs(dcbe->or) - 1);

	if (dcbe->type == DCB_OUTPUT_DP) {
		struct nvkm_i2c_aux *aux =
			nvkm_i2c_aux_find(i2c, dcbe->i2c_index);

		if (aux) {
			if (disp->disp->object.oclass < GF110_DISP) {
				/* HW has no support for address-only
				 * transactions, so we're required to
				 * use custom I2C-over-AUX code.
				 */
				nv_encoder->i2c = &aux->i2c;
			} else {
				nv_encoder->i2c = &nv_connector->aux.ddc;
			}
			nv_encoder->aux = aux;
		}

		if (nv_connector->type != DCB_CONNECTOR_eDP &&
		    nv50_has_mst(drm)) {
			ret = nv50_mstm_new(nv_encoder, &nv_connector->aux,
					    16, nv_connector->base.base.id,
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
	struct nv50_core *core = nv50_disp(encoder->dev)->core;
	if (nv_encoder->crtc)
		core->func->pior->ctrl(core, nv_encoder->or, 0x00000000, NULL);
	nv_encoder->crtc = NULL;
	nv50_outp_release(nv_encoder);
}

static void
nv50_pior_enable(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nv50_head_atom *asyh = nv50_head_atom(nv_crtc->base.state);
	struct nv50_core *core = nv50_disp(encoder->dev)->core;
	u8 owner = 1 << nv_crtc->index;
	u8 proto;

	nv50_outp_acquire(nv_encoder, false);

	switch (asyh->or.bpc) {
	case 10: asyh->or.depth = 0x6; break;
	case  8: asyh->or.depth = 0x5; break;
	case  6: asyh->or.depth = 0x2; break;
	default: asyh->or.depth = 0x0; break;
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

	core->func->pior->ctrl(core, nv_encoder->or, (proto << 8) | owner, asyh);
	nv_encoder->crtc = encoder->crtc;
}

static const struct drm_encoder_helper_funcs
nv50_pior_help = {
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
	struct drm_device *dev = connector->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv50_disp *disp = nv50_disp(dev);
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
	nv_encoder->i2c = ddc;
	nv_encoder->aux = aux;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(connector->dev, encoder, &nv50_pior_func, type,
			 "pior-%04x-%04x", dcbe->hasht, dcbe->hashm);
	drm_encoder_helper_add(encoder, &nv50_pior_help);

	drm_connector_attach_encoder(connector, encoder);

	disp->core->func->pior->get_caps(disp, nv_encoder, ffs(dcbe->or) - 1);

	return 0;
}

/******************************************************************************
 * Atomic
 *****************************************************************************/

static void
nv50_disp_atomic_commit_core(struct drm_atomic_state *state, u32 *interlock)
{
	struct nouveau_drm *drm = nouveau_drm(state->dev);
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct nv50_core *core = disp->core;
	struct nv50_mstm *mstm;
	struct drm_encoder *encoder;

	NV_ATOMIC(drm, "commit core %08x\n", interlock[NV50_DISP_INTERLOCK_BASE]);

	drm_for_each_encoder(encoder, drm->dev) {
		if (encoder->encoder_type != DRM_MODE_ENCODER_DPMST) {
			mstm = nouveau_encoder(encoder)->dp.mstm;
			if (mstm && mstm->modified)
				nv50_mstm_prepare(mstm);
		}
	}

	core->func->ntfy_init(disp->sync, NV50_DISP_CORE_NTFY);
	core->func->update(core, interlock, true);
	if (core->func->ntfy_wait_done(disp->sync, NV50_DISP_CORE_NTFY,
				       disp->core->chan.base.device))
		NV_ERROR(drm, "core notifier timeout\n");

	drm_for_each_encoder(encoder, drm->dev) {
		if (encoder->encoder_type != DRM_MODE_ENCODER_DPMST) {
			mstm = nouveau_encoder(encoder)->dp.mstm;
			if (mstm && mstm->modified)
				nv50_mstm_cleanup(mstm);
		}
	}
}

static void
nv50_disp_atomic_commit_wndw(struct drm_atomic_state *state, u32 *interlock)
{
	struct drm_plane_state *new_plane_state;
	struct drm_plane *plane;
	int i;

	for_each_new_plane_in_state(state, plane, new_plane_state, i) {
		struct nv50_wndw *wndw = nv50_wndw(plane);
		if (interlock[wndw->interlock.type] & wndw->interlock.data) {
			if (wndw->func->update)
				wndw->func->update(wndw, interlock);
		}
	}
}

static void
nv50_disp_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_crtc_state *new_crtc_state, *old_crtc_state;
	struct drm_crtc *crtc;
	struct drm_plane_state *new_plane_state;
	struct drm_plane *plane;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv50_disp *disp = nv50_disp(dev);
	struct nv50_atom *atom = nv50_atom(state);
	struct nv50_core *core = disp->core;
	struct nv50_outp_atom *outp, *outt;
	u32 interlock[NV50_DISP_INTERLOCK__SIZE] = {};
	int i;

	NV_ATOMIC(drm, "commit %d %d\n", atom->lock_core, atom->flush_disable);
	drm_atomic_helper_wait_for_fences(dev, state, false);
	drm_atomic_helper_wait_for_dependencies(state);
	drm_atomic_helper_update_legacy_modeset_state(dev, state);

	if (atom->lock_core)
		mutex_lock(&disp->mutex);

	/* Disable head(s). */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct nv50_head_atom *asyh = nv50_head_atom(new_crtc_state);
		struct nv50_head *head = nv50_head(crtc);

		NV_ATOMIC(drm, "%s: clr %04x (set %04x)\n", crtc->name,
			  asyh->clr.mask, asyh->set.mask);

		if (old_crtc_state->active && !new_crtc_state->active) {
			pm_runtime_put_noidle(dev->dev);
			drm_crtc_vblank_off(crtc);
		}

		if (asyh->clr.mask) {
			nv50_head_flush_clr(head, asyh, atom->flush_disable);
			interlock[NV50_DISP_INTERLOCK_CORE] |= 1;
		}
	}

	/* Disable plane(s). */
	for_each_new_plane_in_state(state, plane, new_plane_state, i) {
		struct nv50_wndw_atom *asyw = nv50_wndw_atom(new_plane_state);
		struct nv50_wndw *wndw = nv50_wndw(plane);

		NV_ATOMIC(drm, "%s: clr %02x (set %02x)\n", plane->name,
			  asyw->clr.mask, asyw->set.mask);
		if (!asyw->clr.mask)
			continue;

		nv50_wndw_flush_clr(wndw, interlock, atom->flush_disable, asyw);
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
			interlock[NV50_DISP_INTERLOCK_CORE] |= 1;
			if (outp->flush_disable) {
				nv50_disp_atomic_commit_wndw(state, interlock);
				nv50_disp_atomic_commit_core(state, interlock);
				memset(interlock, 0x00, sizeof(interlock));
			}
		}
	}

	/* Flush disable. */
	if (interlock[NV50_DISP_INTERLOCK_CORE]) {
		if (atom->flush_disable) {
			nv50_disp_atomic_commit_wndw(state, interlock);
			nv50_disp_atomic_commit_core(state, interlock);
			memset(interlock, 0x00, sizeof(interlock));
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
			interlock[NV50_DISP_INTERLOCK_CORE] = 1;
		}

		list_del(&outp->head);
		kfree(outp);
	}

	/* Update head(s). */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct nv50_head_atom *asyh = nv50_head_atom(new_crtc_state);
		struct nv50_head *head = nv50_head(crtc);

		NV_ATOMIC(drm, "%s: set %04x (clr %04x)\n", crtc->name,
			  asyh->set.mask, asyh->clr.mask);

		if (asyh->set.mask) {
			nv50_head_flush_set(head, asyh);
			interlock[NV50_DISP_INTERLOCK_CORE] = 1;
		}

		if (new_crtc_state->active) {
			if (!old_crtc_state->active) {
				drm_crtc_vblank_on(crtc);
				pm_runtime_get_noresume(dev->dev);
			}
			if (new_crtc_state->event)
				drm_crtc_vblank_get(crtc);
		}
	}

	/* Update window->head assignment.
	 *
	 * This has to happen in an update that's not interlocked with
	 * any window channels to avoid hitting HW error checks.
	 *
	 *TODO: Proper handling of window ownership (Turing apparently
	 *      supports non-fixed mappings).
	 */
	if (core->assign_windows) {
		core->func->wndw.owner(core);
		core->func->update(core, interlock, false);
		core->assign_windows = false;
		interlock[NV50_DISP_INTERLOCK_CORE] = 0;
	}

	/* Update plane(s). */
	for_each_new_plane_in_state(state, plane, new_plane_state, i) {
		struct nv50_wndw_atom *asyw = nv50_wndw_atom(new_plane_state);
		struct nv50_wndw *wndw = nv50_wndw(plane);

		NV_ATOMIC(drm, "%s: set %02x (clr %02x)\n", plane->name,
			  asyw->set.mask, asyw->clr.mask);
		if ( !asyw->set.mask &&
		    (!asyw->clr.mask || atom->flush_disable))
			continue;

		nv50_wndw_flush_set(wndw, interlock, asyw);
	}

	/* Flush update. */
	nv50_disp_atomic_commit_wndw(state, interlock);

	if (interlock[NV50_DISP_INTERLOCK_CORE]) {
		if (interlock[NV50_DISP_INTERLOCK_BASE] ||
		    interlock[NV50_DISP_INTERLOCK_OVLY] ||
		    interlock[NV50_DISP_INTERLOCK_WNDW] ||
		    !atom->state.legacy_cursor_update)
			nv50_disp_atomic_commit_core(state, interlock);
		else
			disp->core->func->update(disp->core, interlock, false);
	}

	if (atom->lock_core)
		mutex_unlock(&disp->mutex);

	/* Wait for HW to signal completion. */
	for_each_new_plane_in_state(state, plane, new_plane_state, i) {
		struct nv50_wndw_atom *asyw = nv50_wndw_atom(new_plane_state);
		struct nv50_wndw *wndw = nv50_wndw(plane);
		int ret = nv50_wndw_wait_armed(wndw, asyw);
		if (ret)
			NV_ERROR(drm, "%s: timeout\n", plane->name);
	}

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (new_crtc_state->event) {
			unsigned long flags;
			/* Get correct count/ts if racing with vblank irq */
			if (new_crtc_state->active)
				drm_crtc_accurate_vblank_count(crtc);
			spin_lock_irqsave(&crtc->dev->event_lock, flags);
			drm_crtc_send_vblank_event(crtc, new_crtc_state->event);
			spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

			new_crtc_state->event = NULL;
			if (new_crtc_state->active)
				drm_crtc_vblank_put(crtc);
		}
	}

	drm_atomic_helper_commit_hw_done(state);
	drm_atomic_helper_cleanup_planes(dev, state);
	drm_atomic_helper_commit_cleanup_done(state);
	drm_atomic_state_put(state);

	/* Drop the RPM ref we got from nv50_disp_atomic_commit() */
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
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
	struct drm_plane_state *new_plane_state;
	struct drm_plane *plane;
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
			goto err_cleanup;
	}

	ret = drm_atomic_helper_swap_state(state, true);
	if (ret)
		goto err_cleanup;

	for_each_new_plane_in_state(state, plane, new_plane_state, i) {
		struct nv50_wndw_atom *asyw = nv50_wndw_atom(new_plane_state);
		struct nv50_wndw *wndw = nv50_wndw(plane);

		if (asyw->set.image)
			nv50_wndw_ntfy_enable(wndw, asyw);
	}

	drm_atomic_state_get(state);

	/*
	 * Grab another RPM ref for the commit tail, which will release the
	 * ref when it's finished
	 */
	pm_runtime_get_noresume(dev->dev);

	if (nonblock)
		queue_work(system_unbound_wq, &state->commit_work);
	else
		nv50_disp_atomic_commit_tail(state);

err_cleanup:
	if (ret)
		drm_atomic_helper_cleanup_planes(dev, state);
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
				struct drm_connector_state *old_connector_state)
{
	struct drm_encoder *encoder = old_connector_state->best_encoder;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_crtc *crtc;
	struct nv50_outp_atom *outp;

	if (!(crtc = old_connector_state->crtc))
		return 0;

	old_crtc_state = drm_atomic_get_old_crtc_state(&atom->state, crtc);
	new_crtc_state = drm_atomic_get_new_crtc_state(&atom->state, crtc);
	if (old_crtc_state->active && drm_atomic_crtc_needs_modeset(new_crtc_state)) {
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
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;
	struct nv50_outp_atom *outp;

	if (!(crtc = connector_state->crtc))
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(&atom->state, crtc);
	if (new_crtc_state->active && drm_atomic_crtc_needs_modeset(new_crtc_state)) {
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
	struct drm_connector_state *old_connector_state, *new_connector_state;
	struct drm_connector *connector;
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;
	int ret, i;

	/* We need to handle colour management on a per-plane basis. */
	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (new_crtc_state->color_mgmt_changed) {
			ret = drm_atomic_add_affected_planes(state, crtc);
			if (ret)
				return ret;
		}
	}

	ret = drm_atomic_helper_check(dev, state);
	if (ret)
		return ret;

	for_each_oldnew_connector_in_state(state, connector, old_connector_state, new_connector_state, i) {
		ret = nv50_disp_outp_atomic_check_clr(atom, old_connector_state);
		if (ret)
			return ret;

		ret = nv50_disp_outp_atomic_check_set(atom, new_connector_state);
		if (ret)
			return ret;
	}

	ret = drm_dp_mst_atomic_check(state);
	if (ret)
		return ret;

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

static void
nv50_display_fini(struct drm_device *dev, bool suspend)
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

static int
nv50_display_init(struct drm_device *dev, bool resume, bool runtime)
{
	struct nv50_core *core = nv50_disp(dev)->core;
	struct drm_encoder *encoder;
	struct drm_plane *plane;

	if (resume || runtime)
		core->func->init(core);

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->encoder_type != DRM_MODE_ENCODER_DPMST) {
			struct nouveau_encoder *nv_encoder =
				nouveau_encoder(encoder);
			nv50_mstm_init(nv_encoder->dp.mstm, runtime);
		}
	}

	drm_for_each_plane(plane, dev) {
		struct nv50_wndw *wndw = nv50_wndw(plane);
		if (plane->funcs != &nv50_wndw)
			continue;
		nv50_wndw_init(wndw);
	}

	return 0;
}

static void
nv50_display_destroy(struct drm_device *dev)
{
	struct nv50_disp *disp = nv50_disp(dev);

	nv50_audio_component_fini(nouveau_drm(dev));

	nvif_object_unmap(&disp->caps);
	nvif_object_fini(&disp->caps);
	nv50_core_del(&disp->core);

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
	struct nvif_device *device = &nouveau_drm(dev)->client.device;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct dcb_table *dcb = &drm->vbios.dcb;
	struct drm_connector *connector, *tmp;
	struct nv50_disp *disp;
	struct dcb_output *dcbe;
	int crtcs, ret, i;
	bool has_mst = nv50_has_mst(drm);

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
	dev->mode_config.quirk_addfb_prefer_xbgr_30bpp = true;
	dev->mode_config.normalize_zpos = true;

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
	ret = nv50_core_new(drm, &disp->core);
	if (ret)
		goto out;

	disp->core->func->init(disp->core);
	if (disp->core->func->caps_init) {
		ret = disp->core->func->caps_init(drm, disp);
		if (ret)
			goto out;
	}

	/* Assign the correct format modifiers */
	if (disp->disp->object.oclass >= TU102_DISP)
		nouveau_display(dev)->format_modifiers = wndwc57e_modifiers;
	else
	if (disp->disp->object.oclass >= GF110_DISP)
		nouveau_display(dev)->format_modifiers = disp90xx_modifiers;
	else
		nouveau_display(dev)->format_modifiers = disp50xx_modifiers;

	/* create crtc objects to represent the hw heads */
	if (disp->disp->object.oclass >= GV100_DISP)
		crtcs = nvif_rd32(&device->object, 0x610060) & 0xff;
	else
	if (disp->disp->object.oclass >= GF110_DISP)
		crtcs = nvif_rd32(&device->object, 0x612004) & 0xf;
	else
		crtcs = 0x3;

	for (i = 0; i < fls(crtcs); i++) {
		struct nv50_head *head;

		if (!(crtcs & (1 << i)))
			continue;

		head = nv50_head_create(dev, i);
		if (IS_ERR(head)) {
			ret = PTR_ERR(head);
			goto out;
		}

		if (has_mst) {
			head->msto = nv50_msto_new(dev, head, i);
			if (IS_ERR(head->msto)) {
				ret = PTR_ERR(head->msto);
				head->msto = NULL;
				goto out;
			}

			/*
			 * FIXME: This is a hack to workaround the following
			 * issues:
			 *
			 * https://gitlab.gnome.org/GNOME/mutter/issues/759
			 * https://gitlab.freedesktop.org/xorg/xserver/merge_requests/277
			 *
			 * Once these issues are closed, this should be
			 * removed
			 */
			head->msto->encoder.possible_crtcs = crtcs;
		}
	}

	/* create encoder/connector objects based on VBIOS DCB table */
	for (i = 0, dcbe = &dcb->entry[0]; i < dcb->entries; i++, dcbe++) {
		connector = nouveau_connector_create(dev, dcbe);
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
		if (connector->possible_encoders)
			continue;

		NV_WARN(drm, "%s has no encoders, removing\n",
			connector->name);
		connector->funcs->destroy(connector);
	}

	/* Disable vblank irqs aggressively for power-saving, safe on nv50+ */
	dev->vblank_disable_immediate = true;

	nv50_audio_component_init(drm);

out:
	if (ret)
		nv50_display_destroy(dev);
	return ret;
}

/******************************************************************************
 * Format modifiers
 *****************************************************************************/

/****************************************************************
 *            Log2(block height) ----------------------------+  *
 *            Page Kind ----------------------------------+  |  *
 *            Gob Height/Page Kind Generation ------+     |  |  *
 *                          Sector layout -------+  |     |  |  *
 *                          Compression ------+  |  |     |  |  */
const u64 disp50xx_modifiers[] = { /*         |  |  |     |  |  */
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 0),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 1),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 2),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 3),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 4),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 5),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 0),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 1),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 2),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 3),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 4),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 5),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 0),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 1),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 2),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 3),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 4),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 5),
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

/****************************************************************
 *            Log2(block height) ----------------------------+  *
 *            Page Kind ----------------------------------+  |  *
 *            Gob Height/Page Kind Generation ------+     |  |  *
 *                          Sector layout -------+  |     |  |  *
 *                          Compression ------+  |  |     |  |  */
const u64 disp90xx_modifiers[] = { /*         |  |  |     |  |  */
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 0),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 1),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 2),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 3),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 4),
	DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 5),
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};
