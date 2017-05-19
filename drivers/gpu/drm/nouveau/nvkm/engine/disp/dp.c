/*
 * Copyright 2014 Red Hat Inc.
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
#include "dp.h"
#include "conn.h"
#include "nv50.h"

#include <subdev/bios.h>
#include <subdev/bios/init.h>
#include <subdev/i2c.h>

#include <nvif/event.h>

struct lt_state {
	struct nvkm_output_dp *outp;
	int link_nr;
	u32 link_bw;
	u8  stat[6];
	u8  conf[4];
	bool pc2;
	u8  pc2stat;
	u8  pc2conf[2];
};

static int
nvkm_dp_train_sense(struct lt_state *lt, bool pc, u32 delay)
{
	struct nvkm_output_dp *outp = lt->outp;
	int ret;

	if (outp->dpcd[DPCD_RC0E_AUX_RD_INTERVAL])
		mdelay(outp->dpcd[DPCD_RC0E_AUX_RD_INTERVAL] * 4);
	else
		udelay(delay);

	ret = nvkm_rdaux(outp->aux, DPCD_LS02, lt->stat, 6);
	if (ret)
		return ret;

	if (pc) {
		ret = nvkm_rdaux(outp->aux, DPCD_LS0C, &lt->pc2stat, 1);
		if (ret)
			lt->pc2stat = 0x00;
		OUTP_DBG(&outp->base, "status %6ph pc2 %02x",
			 lt->stat, lt->pc2stat);
	} else {
		OUTP_DBG(&outp->base, "status %6ph", lt->stat);
	}

	return 0;
}

static int
nvkm_dp_train_drive(struct lt_state *lt, bool pc)
{
	struct nvkm_output_dp *outp = lt->outp;
	int ret, i;

	for (i = 0; i < lt->link_nr; i++) {
		u8 lane = (lt->stat[4 + (i >> 1)] >> ((i & 1) * 4)) & 0xf;
		u8 lpc2 = (lt->pc2stat >> (i * 2)) & 0x3;
		u8 lpre = (lane & 0x0c) >> 2;
		u8 lvsw = (lane & 0x03) >> 0;
		u8 hivs = 3 - lpre;
		u8 hipe = 3;
		u8 hipc = 3;

		if (lpc2 >= hipc)
			lpc2 = hipc | DPCD_LC0F_LANE0_MAX_POST_CURSOR2_REACHED;
		if (lpre >= hipe) {
			lpre = hipe | DPCD_LC03_MAX_SWING_REACHED; /* yes. */
			lvsw = hivs = 3 - (lpre & 3);
		} else
		if (lvsw >= hivs) {
			lvsw = hivs | DPCD_LC03_MAX_SWING_REACHED;
		}

		lt->conf[i] = (lpre << 3) | lvsw;
		lt->pc2conf[i >> 1] |= lpc2 << ((i & 1) * 4);

		OUTP_DBG(&outp->base, "config lane %d %02x %02x",
			 i, lt->conf[i], lpc2);
		outp->func->drv_ctl(outp, i, lvsw & 3, lpre & 3, lpc2 & 3);
	}

	ret = nvkm_wraux(outp->aux, DPCD_LC03(0), lt->conf, 4);
	if (ret)
		return ret;

	if (pc) {
		ret = nvkm_wraux(outp->aux, DPCD_LC0F, lt->pc2conf, 2);
		if (ret)
			return ret;
	}

	return 0;
}

static void
nvkm_dp_train_pattern(struct lt_state *lt, u8 pattern)
{
	struct nvkm_output_dp *outp = lt->outp;
	u8 sink_tp;

	OUTP_DBG(&outp->base, "training pattern %d", pattern);
	outp->func->pattern(outp, pattern);

	nvkm_rdaux(outp->aux, DPCD_LC02, &sink_tp, 1);
	sink_tp &= ~DPCD_LC02_TRAINING_PATTERN_SET;
	sink_tp |= pattern;
	nvkm_wraux(outp->aux, DPCD_LC02, &sink_tp, 1);
}

static int
nvkm_dp_train_eq(struct lt_state *lt)
{
	struct nvkm_output_dp *outp = lt->outp;
	bool eq_done = false, cr_done = true;
	int tries = 0, i;

	if (outp->dpcd[2] & DPCD_RC02_TPS3_SUPPORTED)
		nvkm_dp_train_pattern(lt, 3);
	else
		nvkm_dp_train_pattern(lt, 2);

	do {
		if ((tries &&
		    nvkm_dp_train_drive(lt, lt->pc2)) ||
		    nvkm_dp_train_sense(lt, lt->pc2, 400))
			break;

		eq_done = !!(lt->stat[2] & DPCD_LS04_INTERLANE_ALIGN_DONE);
		for (i = 0; i < lt->link_nr && eq_done; i++) {
			u8 lane = (lt->stat[i >> 1] >> ((i & 1) * 4)) & 0xf;
			if (!(lane & DPCD_LS02_LANE0_CR_DONE))
				cr_done = false;
			if (!(lane & DPCD_LS02_LANE0_CHANNEL_EQ_DONE) ||
			    !(lane & DPCD_LS02_LANE0_SYMBOL_LOCKED))
				eq_done = false;
		}
	} while (!eq_done && cr_done && ++tries <= 5);

	return eq_done ? 0 : -1;
}

static int
nvkm_dp_train_cr(struct lt_state *lt)
{
	bool cr_done = false, abort = false;
	int voltage = lt->conf[0] & DPCD_LC03_VOLTAGE_SWING_SET;
	int tries = 0, i;

	nvkm_dp_train_pattern(lt, 1);

	do {
		if (nvkm_dp_train_drive(lt, false) ||
		    nvkm_dp_train_sense(lt, false, 100))
			break;

		cr_done = true;
		for (i = 0; i < lt->link_nr; i++) {
			u8 lane = (lt->stat[i >> 1] >> ((i & 1) * 4)) & 0xf;
			if (!(lane & DPCD_LS02_LANE0_CR_DONE)) {
				cr_done = false;
				if (lt->conf[i] & DPCD_LC03_MAX_SWING_REACHED)
					abort = true;
				break;
			}
		}

		if ((lt->conf[0] & DPCD_LC03_VOLTAGE_SWING_SET) != voltage) {
			voltage = lt->conf[0] & DPCD_LC03_VOLTAGE_SWING_SET;
			tries = 0;
		}
	} while (!cr_done && !abort && ++tries < 5);

	return cr_done ? 0 : -1;
}

static int
nvkm_dp_train_links(struct lt_state *lt)
{
	struct nvkm_output_dp *outp = lt->outp;
	struct nvkm_disp *disp = outp->base.disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct nvbios_init init = {
		.subdev = subdev,
		.bios = bios,
		.offset = 0x0000,
		.outp = &outp->base.info,
		.crtc = -1,
		.execute = 1,
	};
	u32 lnkcmp;
	u8 sink[2];
	int ret;

	OUTP_DBG(&outp->base, "%d lanes at %d KB/s", lt->link_nr, lt->link_bw);

	/* Intersect misc. capabilities of the OR and sink. */
	if (disp->engine.subdev.device->chipset < 0xd0)
		outp->dpcd[2] &= ~DPCD_RC02_TPS3_SUPPORTED;
	lt->pc2 = outp->dpcd[2] & DPCD_RC02_TPS3_SUPPORTED;

	/* Set desired link configuration on the source. */
	if ((lnkcmp = lt->outp->info.lnkcmp)) {
		if (outp->version < 0x30) {
			while ((lt->link_bw / 10) < nvbios_rd16(bios, lnkcmp))
				lnkcmp += 4;
			init.offset = nvbios_rd16(bios, lnkcmp + 2);
		} else {
			while ((lt->link_bw / 27000) < nvbios_rd08(bios, lnkcmp))
				lnkcmp += 3;
			init.offset = nvbios_rd16(bios, lnkcmp + 1);
		}

		nvbios_exec(&init);
	}

	ret = outp->func->lnk_ctl(outp, lt->link_nr, lt->link_bw / 27000,
				  outp->dpcd[DPCD_RC02] &
					     DPCD_RC02_ENHANCED_FRAME_CAP);
	if (ret) {
		if (ret < 0)
			OUTP_ERR(&outp->base, "lnk_ctl failed with %d", ret);
		return ret;
	}

	outp->func->lnk_pwr(outp, lt->link_nr);

	/* Set desired link configuration on the sink. */
	sink[0] = lt->link_bw / 27000;
	sink[1] = lt->link_nr;
	if (outp->dpcd[DPCD_RC02] & DPCD_RC02_ENHANCED_FRAME_CAP)
		sink[1] |= DPCD_LC01_ENHANCED_FRAME_EN;

	return nvkm_wraux(outp->aux, DPCD_LC00_LINK_BW_SET, sink, 2);
}

static void
nvkm_dp_train_fini(struct lt_state *lt)
{
	struct nvkm_output_dp *outp = lt->outp;
	struct nvkm_disp *disp = outp->base.disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvbios_init init = {
		.subdev = subdev,
		.bios = subdev->device->bios,
		.outp = &outp->base.info,
		.crtc = -1,
		.execute = 1,
	};

	/* Execute AfterLinkTraining script from DP Info table. */
	init.offset = outp->info.script[1],
	nvbios_exec(&init);
}

static void
nvkm_dp_train_init(struct lt_state *lt, bool spread)
{
	struct nvkm_output_dp *outp = lt->outp;
	struct nvkm_disp *disp = outp->base.disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvbios_init init = {
		.subdev = subdev,
		.bios = subdev->device->bios,
		.outp = &outp->base.info,
		.crtc = -1,
		.execute = 1,
	};

	/* Execute EnableSpread/DisableSpread script from DP Info table. */
	if (spread)
		init.offset = outp->info.script[2];
	else
		init.offset = outp->info.script[3];
	nvbios_exec(&init);

	/* Execute BeforeLinkTraining script from DP info table. */
	init.offset = outp->info.script[0];
	nvbios_exec(&init);
}

static const struct dp_rates {
	u32 rate;
	u8  bw;
	u8  nr;
} nvkm_dp_rates[] = {
	{ 2160000, 0x14, 4 },
	{ 1080000, 0x0a, 4 },
	{ 1080000, 0x14, 2 },
	{  648000, 0x06, 4 },
	{  540000, 0x0a, 2 },
	{  540000, 0x14, 1 },
	{  324000, 0x06, 2 },
	{  270000, 0x0a, 1 },
	{  162000, 0x06, 1 },
	{}
};

static void
nvkm_dp_train(struct nvkm_output_dp *outp)
{
	struct nv50_disp *disp = nv50_disp(outp->base.disp);
	const struct dp_rates *cfg = nvkm_dp_rates - 1;
	struct lt_state lt = {
		.outp = outp,
	};
	u8  pwr;
	int ret;

	if (!outp->base.info.location && disp->func->sor.magic)
		disp->func->sor.magic(&outp->base);

	if ((outp->dpcd[2] & 0x1f) > outp->base.info.dpconf.link_nr) {
		outp->dpcd[2] &= ~DPCD_RC02_MAX_LANE_COUNT;
		outp->dpcd[2] |= outp->base.info.dpconf.link_nr;
	}
	if (outp->dpcd[1] > outp->base.info.dpconf.link_bw)
		outp->dpcd[1] = outp->base.info.dpconf.link_bw;

	/* Ensure sink is not in a low-power state. */
	if (!nvkm_rdaux(outp->aux, DPCD_SC00, &pwr, 1)) {
		if ((pwr & DPCD_SC00_SET_POWER) != DPCD_SC00_SET_POWER_D0) {
			pwr &= ~DPCD_SC00_SET_POWER;
			pwr |=  DPCD_SC00_SET_POWER_D0;
			nvkm_wraux(outp->aux, DPCD_SC00, &pwr, 1);
		}
	}

	/* Link training. */
	nvkm_dp_train_init(&lt, outp->dpcd[3] & 0x01);
	while (ret = -EIO, (++cfg)->rate) {
		/* Skip configurations not supported by both OR and sink. */
		while (cfg->nr > (outp->dpcd[2] & DPCD_RC02_MAX_LANE_COUNT) ||
		       cfg->bw > (outp->dpcd[DPCD_RC01_MAX_LINK_RATE]))
			cfg++;
		lt.link_bw = cfg->bw * 27000;
		lt.link_nr = cfg->nr;

		/* Program selected link configuration. */
		ret = nvkm_dp_train_links(&lt);
		if (ret == 0) {
			/* Attempt to train the link in this configuration. */
			memset(lt.stat, 0x00, sizeof(lt.stat));
			if (!nvkm_dp_train_cr(&lt) &&
			    !nvkm_dp_train_eq(&lt))
				break;
		} else
		if (ret) {
			/* nvkm_dp_train_links() handled training, or
			 * we failed to communicate with the sink.
			 */
			break;
		}
	}
	nvkm_dp_train_pattern(&lt, 0);
	nvkm_dp_train_fini(&lt);
	if (ret < 0)
		OUTP_ERR(&outp->base, "link training failed");

	OUTP_DBG(&outp->base, "training complete");
	atomic_set(&outp->lt.done, 1);
}

int
nvkm_output_dp_train(struct nvkm_output *base, u32 datarate)
{
	struct nvkm_output_dp *outp = nvkm_output_dp(base);
	bool retrain = true;
	u8 link[2], stat[3];
	u32 linkrate;
	int ret, i;

	mutex_lock(&outp->mutex);

	/* check that the link is trained at a high enough rate */
	ret = nvkm_rdaux(outp->aux, DPCD_LC00_LINK_BW_SET, link, 2);
	if (ret) {
		OUTP_DBG(&outp->base,
			 "failed to read link config, assuming no sink");
		goto done;
	}

	linkrate = link[0] * 27000 * (link[1] & DPCD_LC01_LANE_COUNT_SET);
	linkrate = (linkrate * 8) / 10; /* 8B/10B coding overhead */
	datarate = (datarate + 9) / 10; /* -> decakilobits */
	if (linkrate < datarate) {
		OUTP_DBG(&outp->base, "link not trained at sufficient rate");
		goto done;
	}

	/* check that link is still trained */
	ret = nvkm_rdaux(outp->aux, DPCD_LS02, stat, 3);
	if (ret) {
		OUTP_DBG(&outp->base,
			 "failed to read link status, assuming no sink");
		goto done;
	}

	if (stat[2] & DPCD_LS04_INTERLANE_ALIGN_DONE) {
		for (i = 0; i < (link[1] & DPCD_LC01_LANE_COUNT_SET); i++) {
			u8 lane = (stat[i >> 1] >> ((i & 1) * 4)) & 0x0f;
			if (!(lane & DPCD_LS02_LANE0_CR_DONE) ||
			    !(lane & DPCD_LS02_LANE0_CHANNEL_EQ_DONE) ||
			    !(lane & DPCD_LS02_LANE0_SYMBOL_LOCKED)) {
				OUTP_DBG(&outp->base,
					 "lane %d not equalised", lane);
				goto done;
			}
		}
		retrain = false;
	} else {
		OUTP_DBG(&outp->base, "no inter-lane alignment");
	}

done:
	if (retrain || !atomic_read(&outp->lt.done)) {
		/* no sink, but still need to configure source */
		if (outp->dpcd[DPCD_RC00_DPCD_REV] == 0x00) {
			outp->dpcd[DPCD_RC01_MAX_LINK_RATE] =
				outp->base.info.dpconf.link_bw;
			outp->dpcd[DPCD_RC02] =
				outp->base.info.dpconf.link_nr;
		}
		nvkm_dp_train(outp);
	}

	mutex_unlock(&outp->mutex);
	return ret;
}

static void
nvkm_output_dp_enable(struct nvkm_output_dp *outp, bool enable)
{
	struct nvkm_i2c_aux *aux = outp->aux;

	if (enable) {
		if (!outp->present) {
			OUTP_DBG(&outp->base, "aux power -> always");
			nvkm_i2c_aux_monitor(aux, true);
			outp->present = true;
		}

		if (!nvkm_rdaux(aux, DPCD_RC00_DPCD_REV, outp->dpcd,
				sizeof(outp->dpcd))) {
			nvkm_output_dp_train(&outp->base, 0);
			return;
		}
	}

	if (outp->present) {
		OUTP_DBG(&outp->base, "aux power -> demand");
		nvkm_i2c_aux_monitor(aux, false);
		outp->present = false;
	}

	atomic_set(&outp->lt.done, 0);
}

static int
nvkm_output_dp_hpd(struct nvkm_notify *notify)
{
	const struct nvkm_i2c_ntfy_rep *line = notify->data;
	struct nvkm_output_dp *outp = container_of(notify, typeof(*outp), hpd);
	struct nvkm_connector *conn = outp->base.conn;
	struct nvkm_disp *disp = outp->base.disp;
	struct nvif_notify_conn_rep_v0 rep = {};

	OUTP_DBG(&outp->base, "HPD: %d", line->mask);
	nvkm_output_dp_enable(outp, true);

	if (line->mask & NVKM_I2C_UNPLUG)
		rep.mask |= NVIF_NOTIFY_CONN_V0_UNPLUG;
	if (line->mask & NVKM_I2C_PLUG)
		rep.mask |= NVIF_NOTIFY_CONN_V0_PLUG;

	nvkm_event_send(&disp->hpd, rep.mask, conn->index, &rep, sizeof(rep));
	return NVKM_NOTIFY_KEEP;
}

static int
nvkm_output_dp_irq(struct nvkm_notify *notify)
{
	const struct nvkm_i2c_ntfy_rep *line = notify->data;
	struct nvkm_output_dp *outp = container_of(notify, typeof(*outp), irq);
	struct nvkm_connector *conn = outp->base.conn;
	struct nvkm_disp *disp = outp->base.disp;
	struct nvif_notify_conn_rep_v0 rep = {
		.mask = NVIF_NOTIFY_CONN_V0_IRQ,
	};

	OUTP_DBG(&outp->base, "IRQ: %d", line->mask);
	nvkm_output_dp_train(&outp->base, 0);

	nvkm_event_send(&disp->hpd, rep.mask, conn->index, &rep, sizeof(rep));
	return NVKM_NOTIFY_KEEP;
}

static void
nvkm_output_dp_fini(struct nvkm_output *base)
{
	struct nvkm_output_dp *outp = nvkm_output_dp(base);
	nvkm_notify_put(&outp->hpd);
	nvkm_notify_put(&outp->irq);
	nvkm_output_dp_enable(outp, false);
}

static void
nvkm_output_dp_init(struct nvkm_output *base)
{
	struct nvkm_output_dp *outp = nvkm_output_dp(base);
	nvkm_notify_put(&outp->base.conn->hpd);
	nvkm_output_dp_enable(outp, true);
	nvkm_notify_get(&outp->irq);
	nvkm_notify_get(&outp->hpd);
}

static void *
nvkm_output_dp_dtor(struct nvkm_output *base)
{
	struct nvkm_output_dp *outp = nvkm_output_dp(base);
	nvkm_notify_fini(&outp->hpd);
	nvkm_notify_fini(&outp->irq);
	return outp;
}

static const struct nvkm_output_func
nvkm_output_dp_func = {
	.dtor = nvkm_output_dp_dtor,
	.init = nvkm_output_dp_init,
	.fini = nvkm_output_dp_fini,
};

int
nvkm_output_dp_ctor(const struct nvkm_output_dp_func *func,
		    struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
		    struct nvkm_i2c_aux *aux, struct nvkm_output_dp *outp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	struct nvkm_bios *bios = device->bios;
	struct nvkm_i2c *i2c = device->i2c;
	u8  hdr, cnt, len;
	u32 data;
	int ret;

	nvkm_output_ctor(&nvkm_output_dp_func, disp, index, dcbE, &outp->base);
	outp->func = func;
	outp->aux = aux;
	if (!outp->aux) {
		OUTP_ERR(&outp->base, "no aux");
		return -ENODEV;
	}

	/* bios data is not optional */
	data = nvbios_dpout_match(bios, outp->base.info.hasht,
				  outp->base.info.hashm, &outp->version,
				  &hdr, &cnt, &len, &outp->info);
	if (!data) {
		OUTP_ERR(&outp->base, "no bios dp data");
		return -ENODEV;
	}

	OUTP_DBG(&outp->base, "bios dp %02x %02x %02x %02x",
		 outp->version, hdr, cnt, len);

	/* link maintenance */
	ret = nvkm_notify_init(NULL, &i2c->event, nvkm_output_dp_irq, true,
			       &(struct nvkm_i2c_ntfy_req) {
				.mask = NVKM_I2C_IRQ,
				.port = outp->aux->id,
			       },
			       sizeof(struct nvkm_i2c_ntfy_req),
			       sizeof(struct nvkm_i2c_ntfy_rep),
			       &outp->irq);
	if (ret) {
		OUTP_ERR(&outp->base, "error monitoring aux irq: %d", ret);
		return ret;
	}

	mutex_init(&outp->mutex);
	atomic_set(&outp->lt.done, 0);

	/* hotplug detect, replaces gpio-based mechanism with aux events */
	ret = nvkm_notify_init(NULL, &i2c->event, nvkm_output_dp_hpd, true,
			       &(struct nvkm_i2c_ntfy_req) {
				.mask = NVKM_I2C_PLUG | NVKM_I2C_UNPLUG,
				.port = outp->aux->id,
			       },
			       sizeof(struct nvkm_i2c_ntfy_req),
			       sizeof(struct nvkm_i2c_ntfy_rep),
			       &outp->hpd);
	if (ret) {
		OUTP_ERR(&outp->base, "error monitoring aux hpd: %d", ret);
		return ret;
	}

	return 0;
}

int
nvkm_output_dp_new_(const struct nvkm_output_dp_func *func,
		    struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
		    struct nvkm_output **poutp)
{
	struct nvkm_i2c *i2c = disp->engine.subdev.device->i2c;
	struct nvkm_i2c_aux *aux = nvkm_i2c_aux_find(i2c, dcbE->i2c_index);
	struct nvkm_output_dp *outp;

	if (!(outp = kzalloc(sizeof(*outp), GFP_KERNEL)))
		return -ENOMEM;
	*poutp = &outp->base;

	return nvkm_output_dp_ctor(func, disp, index, dcbE, aux, outp);
}
