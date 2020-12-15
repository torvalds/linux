/*
 * Copyright 2009 Red Hat Inc.
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

#include <drm/drm_dp_helper.h>

#include "nouveau_drv.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"

#include <nvif/class.h>
#include <nvif/cl5070.h>

MODULE_PARM_DESC(mst, "Enable DisplayPort multi-stream (default: enabled)");
static int nouveau_mst = 1;
module_param_named(mst, nouveau_mst, int, 0400);

static void
nouveau_dp_probe_oui(struct drm_device *dev, struct nvkm_i2c_aux *aux, u8 *dpcd)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	u8 buf[3];

	if (!(dpcd[DP_DOWN_STREAM_PORT_COUNT] & DP_OUI_SUPPORT))
		return;

	if (!nvkm_rdaux(aux, DP_SINK_OUI, buf, 3))
		NV_DEBUG(drm, "Sink OUI: %02hx%02hx%02hx\n",
			     buf[0], buf[1], buf[2]);

	if (!nvkm_rdaux(aux, DP_BRANCH_OUI, buf, 3))
		NV_DEBUG(drm, "Branch OUI: %02hx%02hx%02hx\n",
			     buf[0], buf[1], buf[2]);

}

int
nouveau_dp_detect(struct nouveau_encoder *nv_encoder)
{
	struct drm_device *dev = nv_encoder->base.base.dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_i2c_aux *aux;
	u8 dpcd[8];
	int ret;

	aux = nv_encoder->aux;
	if (!aux)
		return -ENODEV;

	ret = nvkm_rdaux(aux, DP_DPCD_REV, dpcd, sizeof(dpcd));
	if (ret)
		return ret;

	nv_encoder->dp.link_bw = 27000 * dpcd[1];
	nv_encoder->dp.link_nr = dpcd[2] & DP_MAX_LANE_COUNT_MASK;

	NV_DEBUG(drm, "display: %dx%d dpcd 0x%02x\n",
		     nv_encoder->dp.link_nr, nv_encoder->dp.link_bw, dpcd[0]);
	NV_DEBUG(drm, "encoder: %dx%d\n",
		     nv_encoder->dcb->dpconf.link_nr,
		     nv_encoder->dcb->dpconf.link_bw);

	if (nv_encoder->dcb->dpconf.link_nr < nv_encoder->dp.link_nr)
		nv_encoder->dp.link_nr = nv_encoder->dcb->dpconf.link_nr;
	if (nv_encoder->dcb->dpconf.link_bw < nv_encoder->dp.link_bw)
		nv_encoder->dp.link_bw = nv_encoder->dcb->dpconf.link_bw;

	NV_DEBUG(drm, "maximum: %dx%d\n",
		     nv_encoder->dp.link_nr, nv_encoder->dp.link_bw);

	nouveau_dp_probe_oui(dev, aux, dpcd);

	ret = nv50_mstm_detect(nv_encoder->dp.mstm, dpcd, nouveau_mst);
	if (ret == 1)
		return NOUVEAU_DP_MST;
	if (ret == 0)
		return NOUVEAU_DP_SST;
	return ret;
}

/* TODO:
 * - Use the minimum possible BPC here, once we add support for the max bpc
 *   property.
 * - Validate the mode against downstream port caps (see
 *   drm_dp_downstream_max_clock())
 * - Validate against the DP caps advertised by the GPU (we don't check these
 *   yet)
 */
enum drm_mode_status
nv50_dp_mode_valid(struct drm_connector *connector,
		   struct nouveau_encoder *outp,
		   const struct drm_display_mode *mode,
		   unsigned *out_clock)
{
	const unsigned min_clock = 25000;
	unsigned max_clock, clock;
	enum drm_mode_status ret;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE && !outp->caps.dp_interlace)
		return MODE_NO_INTERLACE;

	max_clock = outp->dp.link_nr * outp->dp.link_bw;
	clock = mode->clock * (connector->display_info.bpc * 3) / 10;

	ret = nouveau_conn_mode_clock_valid(mode, min_clock, max_clock,
					    &clock);
	if (out_clock)
		*out_clock = clock;
	return ret;
}
