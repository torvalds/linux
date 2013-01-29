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

#include <drm/drmP.h>
#include <drm/drm_dp_helper.h>

#include "nouveau_drm.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"

#include <core/class.h>

#include <subdev/gpio.h>
#include <subdev/i2c.h>

/******************************************************************************
 * link training
 *****************************************************************************/
struct dp_state {
	struct nouveau_i2c_port *auxch;
	struct nouveau_object *core;
	struct dcb_output *dcb;
	int crtc;
	u8 *dpcd;
	int link_nr;
	u32 link_bw;
	u8  stat[6];
	u8  conf[4];
};

static void
dp_set_link_config(struct drm_device *dev, struct dp_state *dp)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct dcb_output *dcb = dp->dcb;
	const u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	const u32 moff = (dp->crtc << 3) | (link << 2) | or;
	u8 sink[2];
	u32 data;

	NV_DEBUG(drm, "%d lanes at %d KB/s\n", dp->link_nr, dp->link_bw);

	/* set desired link configuration on the source */
	data = ((dp->link_bw / 27000) << 8) | dp->link_nr;
	if (dp->dpcd[2] & DP_ENHANCED_FRAME_CAP)
		data |= NV94_DISP_SOR_DP_LNKCTL_FRAME_ENH;

	nv_call(dp->core, NV94_DISP_SOR_DP_LNKCTL + moff, data);

	/* inform the sink of the new configuration */
	sink[0] = dp->link_bw / 27000;
	sink[1] = dp->link_nr;
	if (dp->dpcd[2] & DP_ENHANCED_FRAME_CAP)
		sink[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	nv_wraux(dp->auxch, DP_LINK_BW_SET, sink, 2);
}

static void
dp_set_training_pattern(struct drm_device *dev, struct dp_state *dp, u8 pattern)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct dcb_output *dcb = dp->dcb;
	const u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	const u32 moff = (dp->crtc << 3) | (link << 2) | or;
	u8 sink_tp;

	NV_DEBUG(drm, "training pattern %d\n", pattern);

	nv_call(dp->core, NV94_DISP_SOR_DP_TRAIN + moff, pattern);

	nv_rdaux(dp->auxch, DP_TRAINING_PATTERN_SET, &sink_tp, 1);
	sink_tp &= ~DP_TRAINING_PATTERN_MASK;
	sink_tp |= pattern;
	nv_wraux(dp->auxch, DP_TRAINING_PATTERN_SET, &sink_tp, 1);
}

static int
dp_link_train_commit(struct drm_device *dev, struct dp_state *dp)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct dcb_output *dcb = dp->dcb;
	const u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	const u32 moff = (dp->crtc << 3) | (link << 2) | or;
	int i;

	for (i = 0; i < dp->link_nr; i++) {
		u8 lane = (dp->stat[4 + (i >> 1)] >> ((i & 1) * 4)) & 0xf;
		u8 lpre = (lane & 0x0c) >> 2;
		u8 lvsw = (lane & 0x03) >> 0;

		dp->conf[i] = (lpre << 3) | lvsw;
		if (lvsw == DP_TRAIN_VOLTAGE_SWING_1200)
			dp->conf[i] |= DP_TRAIN_MAX_SWING_REACHED;
		if ((lpre << 3) == DP_TRAIN_PRE_EMPHASIS_9_5)
			dp->conf[i] |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

		NV_DEBUG(drm, "config lane %d %02x\n", i, dp->conf[i]);

		nv_call(dp->core, NV94_DISP_SOR_DP_DRVCTL(i) + moff, (lvsw << 8) | lpre);
	}

	return nv_wraux(dp->auxch, DP_TRAINING_LANE0_SET, dp->conf, 4);
}

static int
dp_link_train_update(struct drm_device *dev, struct dp_state *dp, u32 delay)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	int ret;

	udelay(delay);

	ret = nv_rdaux(dp->auxch, DP_LANE0_1_STATUS, dp->stat, 6);
	if (ret)
		return ret;

	NV_DEBUG(drm, "status %*ph\n", 6, dp->stat);
	return 0;
}

static int
dp_link_train_cr(struct drm_device *dev, struct dp_state *dp)
{
	bool cr_done = false, abort = false;
	int voltage = dp->conf[0] & DP_TRAIN_VOLTAGE_SWING_MASK;
	int tries = 0, i;

	dp_set_training_pattern(dev, dp, DP_TRAINING_PATTERN_1);

	do {
		if (dp_link_train_commit(dev, dp) ||
		    dp_link_train_update(dev, dp, 100))
			break;

		cr_done = true;
		for (i = 0; i < dp->link_nr; i++) {
			u8 lane = (dp->stat[i >> 1] >> ((i & 1) * 4)) & 0xf;
			if (!(lane & DP_LANE_CR_DONE)) {
				cr_done = false;
				if (dp->conf[i] & DP_TRAIN_MAX_SWING_REACHED)
					abort = true;
				break;
			}
		}

		if ((dp->conf[0] & DP_TRAIN_VOLTAGE_SWING_MASK) != voltage) {
			voltage = dp->conf[0] & DP_TRAIN_VOLTAGE_SWING_MASK;
			tries = 0;
		}
	} while (!cr_done && !abort && ++tries < 5);

	return cr_done ? 0 : -1;
}

static int
dp_link_train_eq(struct drm_device *dev, struct dp_state *dp)
{
	bool eq_done, cr_done = true;
	int tries = 0, i;

	dp_set_training_pattern(dev, dp, DP_TRAINING_PATTERN_2);

	do {
		if (dp_link_train_update(dev, dp, 400))
			break;

		eq_done = !!(dp->stat[2] & DP_INTERLANE_ALIGN_DONE);
		for (i = 0; i < dp->link_nr && eq_done; i++) {
			u8 lane = (dp->stat[i >> 1] >> ((i & 1) * 4)) & 0xf;
			if (!(lane & DP_LANE_CR_DONE))
				cr_done = false;
			if (!(lane & DP_LANE_CHANNEL_EQ_DONE) ||
			    !(lane & DP_LANE_SYMBOL_LOCKED))
				eq_done = false;
		}

		if (dp_link_train_commit(dev, dp))
			break;
	} while (!eq_done && cr_done && ++tries <= 5);

	return eq_done ? 0 : -1;
}

static void
dp_link_train_init(struct drm_device *dev, struct dp_state *dp, bool spread)
{
	struct dcb_output *dcb = dp->dcb;
	const u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	const u32 moff = (dp->crtc << 3) | (link << 2) | or;

	nv_call(dp->core, NV94_DISP_SOR_DP_TRAIN + moff, (spread ?
			  NV94_DISP_SOR_DP_TRAIN_INIT_SPREAD_ON :
			  NV94_DISP_SOR_DP_TRAIN_INIT_SPREAD_OFF) |
			  NV94_DISP_SOR_DP_TRAIN_OP_INIT);
}

static void
dp_link_train_fini(struct drm_device *dev, struct dp_state *dp)
{
	struct dcb_output *dcb = dp->dcb;
	const u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	const u32 moff = (dp->crtc << 3) | (link << 2) | or;

	nv_call(dp->core, NV94_DISP_SOR_DP_TRAIN + moff,
			  NV94_DISP_SOR_DP_TRAIN_OP_FINI);
}

static bool
nouveau_dp_link_train(struct drm_encoder *encoder, u32 datarate,
		      struct nouveau_object *core)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector =
		nouveau_encoder_connector_get(nv_encoder);
	struct drm_device *dev = encoder->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_i2c *i2c = nouveau_i2c(drm->device);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	const u32 bw_list[] = { 270000, 162000, 0 };
	const u32 *link_bw = bw_list;
	struct dp_state dp;

	dp.auxch = i2c->find(i2c, nv_encoder->dcb->i2c_index);
	if (!dp.auxch)
		return false;

	dp.core = core;
	dp.dcb = nv_encoder->dcb;
	dp.crtc = nv_crtc->index;
	dp.dpcd = nv_encoder->dp.dpcd;

	/* adjust required bandwidth for 8B/10B coding overhead */
	datarate = (datarate / 8) * 10;

	/* some sinks toggle hotplug in response to some of the actions
	 * we take during link training (DP_SET_POWER is one), we need
	 * to ignore them for the moment to avoid races.
	 */
	gpio->irq(gpio, 0, nv_connector->hpd, 0xff, false);

	/* enable down-spreading and execute pre-train script from vbios */
	dp_link_train_init(dev, &dp, nv_encoder->dp.dpcd[3] & 1);

	/* start off at highest link rate supported by encoder and display */
	while (*link_bw > nv_encoder->dp.link_bw)
		link_bw++;

	while (link_bw[0]) {
		/* find minimum required lane count at this link rate */
		dp.link_nr = nv_encoder->dp.link_nr;
		while ((dp.link_nr >> 1) * link_bw[0] > datarate)
			dp.link_nr >>= 1;

		/* drop link rate to minimum with this lane count */
		while ((link_bw[1] * dp.link_nr) > datarate)
			link_bw++;
		dp.link_bw = link_bw[0];

		/* program selected link configuration */
		dp_set_link_config(dev, &dp);

		/* attempt to train the link at this configuration */
		memset(dp.stat, 0x00, sizeof(dp.stat));
		if (!dp_link_train_cr(dev, &dp) &&
		    !dp_link_train_eq(dev, &dp))
			break;

		/* retry at lower rate */
		link_bw++;
	}

	/* finish link training */
	dp_set_training_pattern(dev, &dp, DP_TRAINING_PATTERN_DISABLE);

	/* execute post-train script from vbios */
	dp_link_train_fini(dev, &dp);

	/* re-enable hotplug detect */
	gpio->irq(gpio, 0, nv_connector->hpd, 0xff, true);
	return true;
}

void
nouveau_dp_dpms(struct drm_encoder *encoder, int mode, u32 datarate,
		struct nouveau_object *core)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);
	struct nouveau_i2c *i2c = nouveau_i2c(drm->device);
	struct nouveau_i2c_port *auxch;
	u8 status;

	auxch = i2c->find(i2c, nv_encoder->dcb->i2c_index);
	if (!auxch)
		return;

	if (mode == DRM_MODE_DPMS_ON)
		status = DP_SET_POWER_D0;
	else
		status = DP_SET_POWER_D3;

	nv_wraux(auxch, DP_SET_POWER, &status, 1);

	if (mode == DRM_MODE_DPMS_ON)
		nouveau_dp_link_train(encoder, datarate, core);
}

static void
nouveau_dp_probe_oui(struct drm_device *dev, struct nouveau_i2c_port *auxch,
		     u8 *dpcd)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	u8 buf[3];

	if (!(dpcd[DP_DOWN_STREAM_PORT_COUNT] & DP_OUI_SUPPORT))
		return;

	if (!nv_rdaux(auxch, DP_SINK_OUI, buf, 3))
		NV_DEBUG(drm, "Sink OUI: %02hx%02hx%02hx\n",
			     buf[0], buf[1], buf[2]);

	if (!nv_rdaux(auxch, DP_BRANCH_OUI, buf, 3))
		NV_DEBUG(drm, "Branch OUI: %02hx%02hx%02hx\n",
			     buf[0], buf[1], buf[2]);

}

bool
nouveau_dp_detect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_i2c *i2c = nouveau_i2c(drm->device);
	struct nouveau_i2c_port *auxch;
	u8 *dpcd = nv_encoder->dp.dpcd;
	int ret;

	auxch = i2c->find(i2c, nv_encoder->dcb->i2c_index);
	if (!auxch)
		return false;

	ret = nv_rdaux(auxch, DP_DPCD_REV, dpcd, 8);
	if (ret)
		return false;

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

	nouveau_dp_probe_oui(dev, auxch, dpcd);

	return true;
}
