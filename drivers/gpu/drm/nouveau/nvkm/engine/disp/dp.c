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
	struct nvkm_dp *dp;
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
	struct nvkm_dp *dp = lt->dp;
	int ret;

	if (dp->dpcd[DPCD_RC0E_AUX_RD_INTERVAL])
		mdelay(dp->dpcd[DPCD_RC0E_AUX_RD_INTERVAL] * 4);
	else
		udelay(delay);

	ret = nvkm_rdaux(dp->aux, DPCD_LS02, lt->stat, 6);
	if (ret)
		return ret;

	if (pc) {
		ret = nvkm_rdaux(dp->aux, DPCD_LS0C, &lt->pc2stat, 1);
		if (ret)
			lt->pc2stat = 0x00;
		OUTP_TRACE(&dp->outp, "status %6ph pc2 %02x",
			   lt->stat, lt->pc2stat);
	} else {
		OUTP_TRACE(&dp->outp, "status %6ph", lt->stat);
	}

	return 0;
}

static int
nvkm_dp_train_drive(struct lt_state *lt, bool pc)
{
	struct nvkm_dp *dp = lt->dp;
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

		OUTP_TRACE(&dp->outp, "config lane %d %02x %02x",
			   i, lt->conf[i], lpc2);
		dp->func->drv_ctl(dp, i, lvsw & 3, lpre & 3, lpc2 & 3);
	}

	ret = nvkm_wraux(dp->aux, DPCD_LC03(0), lt->conf, 4);
	if (ret)
		return ret;

	if (pc) {
		ret = nvkm_wraux(dp->aux, DPCD_LC0F, lt->pc2conf, 2);
		if (ret)
			return ret;
	}

	return 0;
}

static void
nvkm_dp_train_pattern(struct lt_state *lt, u8 pattern)
{
	struct nvkm_dp *dp = lt->dp;
	u8 sink_tp;

	OUTP_TRACE(&dp->outp, "training pattern %d", pattern);
	dp->func->pattern(dp, pattern);

	nvkm_rdaux(dp->aux, DPCD_LC02, &sink_tp, 1);
	sink_tp &= ~DPCD_LC02_TRAINING_PATTERN_SET;
	sink_tp |= pattern;
	nvkm_wraux(dp->aux, DPCD_LC02, &sink_tp, 1);
}

static int
nvkm_dp_train_eq(struct lt_state *lt)
{
	bool eq_done = false, cr_done = true;
	int tries = 0, i;

	if (lt->dp->dpcd[2] & DPCD_RC02_TPS3_SUPPORTED)
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
	struct nvkm_dp *dp = lt->dp;
	struct nvkm_disp *disp = dp->outp.disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct nvbios_init init = {
		.subdev = subdev,
		.bios = bios,
		.offset = 0x0000,
		.outp = &dp->outp.info,
		.crtc = -1,
		.execute = 1,
	};
	u32 lnkcmp;
	u8 sink[2];
	int ret;

	OUTP_DBG(&dp->outp, "%d lanes at %d KB/s", lt->link_nr, lt->link_bw);

	/* Intersect misc. capabilities of the OR and sink. */
	if (disp->engine.subdev.device->chipset < 0xd0)
		dp->dpcd[2] &= ~DPCD_RC02_TPS3_SUPPORTED;
	lt->pc2 = dp->dpcd[2] & DPCD_RC02_TPS3_SUPPORTED;

	/* Set desired link configuration on the source. */
	if ((lnkcmp = lt->dp->info.lnkcmp)) {
		if (dp->version < 0x30) {
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

	ret = dp->func->lnk_ctl(dp, lt->link_nr, lt->link_bw / 27000,
				dp->dpcd[DPCD_RC02] &
					 DPCD_RC02_ENHANCED_FRAME_CAP);
	if (ret) {
		if (ret < 0)
			OUTP_ERR(&dp->outp, "lnk_ctl failed with %d", ret);
		return ret;
	}

	dp->func->lnk_pwr(dp, lt->link_nr);

	/* Set desired link configuration on the sink. */
	sink[0] = lt->link_bw / 27000;
	sink[1] = lt->link_nr;
	if (dp->dpcd[DPCD_RC02] & DPCD_RC02_ENHANCED_FRAME_CAP)
		sink[1] |= DPCD_LC01_ENHANCED_FRAME_EN;

	return nvkm_wraux(dp->aux, DPCD_LC00_LINK_BW_SET, sink, 2);
}

static void
nvkm_dp_train_fini(struct lt_state *lt)
{
	struct nvkm_dp *dp = lt->dp;
	struct nvkm_subdev *subdev = &dp->outp.disp->engine.subdev;
	struct nvbios_init init = {
		.subdev = subdev,
		.bios = subdev->device->bios,
		.outp = &dp->outp.info,
		.crtc = -1,
		.execute = 1,
	};

	/* Execute AfterLinkTraining script from DP Info table. */
	init.offset = dp->info.script[1],
	nvbios_exec(&init);
}

static void
nvkm_dp_train_init(struct lt_state *lt, bool spread)
{
	struct nvkm_dp *dp = lt->dp;
	struct nvkm_subdev *subdev = &dp->outp.disp->engine.subdev;
	struct nvbios_init init = {
		.subdev = subdev,
		.bios = subdev->device->bios,
		.outp = &dp->outp.info,
		.crtc = -1,
		.execute = 1,
	};

	/* Execute EnableSpread/DisableSpread script from DP Info table. */
	if (spread)
		init.offset = dp->info.script[2];
	else
		init.offset = dp->info.script[3];
	nvbios_exec(&init);

	/* Execute BeforeLinkTraining script from DP Info table. */
	init.offset = dp->info.script[0];
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
nvkm_dp_train(struct nvkm_dp *dp)
{
	struct nv50_disp *disp = nv50_disp(dp->outp.disp);
	const struct dp_rates *cfg = nvkm_dp_rates - 1;
	struct lt_state lt = {
		.dp = dp,
	};
	u8  pwr;
	int ret;

	if (!dp->outp.info.location && disp->func->sor.magic)
		disp->func->sor.magic(&dp->outp);

	if ((dp->dpcd[2] & 0x1f) > dp->outp.info.dpconf.link_nr) {
		dp->dpcd[2] &= ~DPCD_RC02_MAX_LANE_COUNT;
		dp->dpcd[2] |= dp->outp.info.dpconf.link_nr;
	}
	if (dp->dpcd[1] > dp->outp.info.dpconf.link_bw)
		dp->dpcd[1] = dp->outp.info.dpconf.link_bw;

	/* Ensure sink is not in a low-power state. */
	if (!nvkm_rdaux(dp->aux, DPCD_SC00, &pwr, 1)) {
		if ((pwr & DPCD_SC00_SET_POWER) != DPCD_SC00_SET_POWER_D0) {
			pwr &= ~DPCD_SC00_SET_POWER;
			pwr |=  DPCD_SC00_SET_POWER_D0;
			nvkm_wraux(dp->aux, DPCD_SC00, &pwr, 1);
		}
	}

	/* Link training. */
	nvkm_dp_train_init(&lt, dp->dpcd[3] & 0x01);
	while (ret = -EIO, (++cfg)->rate) {
		/* Skip configurations not supported by both OR and sink. */
		while (cfg->nr > (dp->dpcd[2] & DPCD_RC02_MAX_LANE_COUNT) ||
		       cfg->bw > (dp->dpcd[DPCD_RC01_MAX_LINK_RATE]))
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
		OUTP_ERR(&dp->outp, "training failed");

	OUTP_DBG(&dp->outp, "training done");
	atomic_set(&dp->lt.done, 1);
}

int
nvkm_output_dp_train(struct nvkm_outp *outp, u32 datarate)
{
	struct nvkm_dp *dp = nvkm_dp(outp);
	bool retrain = true;
	u8 link[2], stat[3];
	u32 linkrate;
	int ret, i;

	mutex_lock(&dp->mutex);

	/* check that the link is trained at a high enough rate */
	ret = nvkm_rdaux(dp->aux, DPCD_LC00_LINK_BW_SET, link, 2);
	if (ret) {
		OUTP_DBG(&dp->outp,
			 "failed to read link config, assuming no sink");
		goto done;
	}

	linkrate = link[0] * 27000 * (link[1] & DPCD_LC01_LANE_COUNT_SET);
	linkrate = (linkrate * 8) / 10; /* 8B/10B coding overhead */
	datarate = (datarate + 9) / 10; /* -> decakilobits */
	if (linkrate < datarate) {
		OUTP_DBG(&dp->outp, "link not trained at sufficient rate");
		goto done;
	}

	/* check that link is still trained */
	ret = nvkm_rdaux(dp->aux, DPCD_LS02, stat, 3);
	if (ret) {
		OUTP_DBG(&dp->outp,
			 "failed to read link status, assuming no sink");
		goto done;
	}

	if (stat[2] & DPCD_LS04_INTERLANE_ALIGN_DONE) {
		for (i = 0; i < (link[1] & DPCD_LC01_LANE_COUNT_SET); i++) {
			u8 lane = (stat[i >> 1] >> ((i & 1) * 4)) & 0x0f;
			if (!(lane & DPCD_LS02_LANE0_CR_DONE) ||
			    !(lane & DPCD_LS02_LANE0_CHANNEL_EQ_DONE) ||
			    !(lane & DPCD_LS02_LANE0_SYMBOL_LOCKED)) {
				OUTP_DBG(&dp->outp,
					 "lane %d not equalised", lane);
				goto done;
			}
		}
		retrain = false;
	} else {
		OUTP_DBG(&dp->outp, "no inter-lane alignment");
	}

done:
	if (retrain || !atomic_read(&dp->lt.done)) {
		/* no sink, but still need to configure source */
		if (dp->dpcd[DPCD_RC00_DPCD_REV] == 0x00) {
			dp->dpcd[DPCD_RC01_MAX_LINK_RATE] =
				dp->outp.info.dpconf.link_bw;
			dp->dpcd[DPCD_RC02] =
				dp->outp.info.dpconf.link_nr;
		}
		nvkm_dp_train(dp);
	}

	mutex_unlock(&dp->mutex);
	return ret;
}

static void
nvkm_dp_enable(struct nvkm_dp *dp, bool enable)
{
	struct nvkm_i2c_aux *aux = dp->aux;

	if (enable) {
		if (!dp->present) {
			OUTP_DBG(&dp->outp, "aux power -> always");
			nvkm_i2c_aux_monitor(aux, true);
			dp->present = true;
		}

		if (!nvkm_rdaux(aux, DPCD_RC00_DPCD_REV, dp->dpcd,
				sizeof(dp->dpcd))) {
			nvkm_output_dp_train(&dp->outp, 0);
			return;
		}
	}

	if (dp->present) {
		OUTP_DBG(&dp->outp, "aux power -> demand");
		nvkm_i2c_aux_monitor(aux, false);
		dp->present = false;
	}

	atomic_set(&dp->lt.done, 0);
}

static int
nvkm_dp_hpd(struct nvkm_notify *notify)
{
	const struct nvkm_i2c_ntfy_rep *line = notify->data;
	struct nvkm_dp *dp = container_of(notify, typeof(*dp), hpd);
	struct nvkm_conn *conn = dp->outp.conn;
	struct nvkm_disp *disp = dp->outp.disp;
	struct nvif_notify_conn_rep_v0 rep = {};

	OUTP_DBG(&dp->outp, "HPD: %d", line->mask);
	nvkm_dp_enable(dp, true);

	if (line->mask & NVKM_I2C_UNPLUG)
		rep.mask |= NVIF_NOTIFY_CONN_V0_UNPLUG;
	if (line->mask & NVKM_I2C_PLUG)
		rep.mask |= NVIF_NOTIFY_CONN_V0_PLUG;

	nvkm_event_send(&disp->hpd, rep.mask, conn->index, &rep, sizeof(rep));
	return NVKM_NOTIFY_KEEP;
}

static int
nvkm_dp_irq(struct nvkm_notify *notify)
{
	const struct nvkm_i2c_ntfy_rep *line = notify->data;
	struct nvkm_dp *dp = container_of(notify, typeof(*dp), irq);
	struct nvkm_conn *conn = dp->outp.conn;
	struct nvkm_disp *disp = dp->outp.disp;
	struct nvif_notify_conn_rep_v0 rep = {
		.mask = NVIF_NOTIFY_CONN_V0_IRQ,
	};

	OUTP_DBG(&dp->outp, "IRQ: %d", line->mask);
	nvkm_output_dp_train(&dp->outp, 0);

	nvkm_event_send(&disp->hpd, rep.mask, conn->index, &rep, sizeof(rep));
	return NVKM_NOTIFY_KEEP;
}

static void
nvkm_dp_fini(struct nvkm_outp *outp)
{
	struct nvkm_dp *dp = nvkm_dp(outp);
	nvkm_notify_put(&dp->hpd);
	nvkm_notify_put(&dp->irq);
	nvkm_dp_enable(dp, false);
}

static void
nvkm_dp_init(struct nvkm_outp *outp)
{
	struct nvkm_dp *dp = nvkm_dp(outp);
	nvkm_notify_put(&dp->outp.conn->hpd);
	nvkm_dp_enable(dp, true);
	nvkm_notify_get(&dp->irq);
	nvkm_notify_get(&dp->hpd);
}

static void *
nvkm_dp_dtor(struct nvkm_outp *outp)
{
	struct nvkm_dp *dp = nvkm_dp(outp);
	nvkm_notify_fini(&dp->hpd);
	nvkm_notify_fini(&dp->irq);
	return dp;
}

static const struct nvkm_outp_func
nvkm_dp_func = {
	.dtor = nvkm_dp_dtor,
	.init = nvkm_dp_init,
	.fini = nvkm_dp_fini,
};

static int
nvkm_dp_ctor(struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
	     struct nvkm_i2c_aux *aux, struct nvkm_dp *dp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	struct nvkm_bios *bios = device->bios;
	struct nvkm_i2c *i2c = device->i2c;
	u8  hdr, cnt, len;
	u32 data;
	int ret;

	nvkm_outp_ctor(&nvkm_dp_func, disp, index, dcbE, &dp->outp);
	dp->aux = aux;
	if (!dp->aux) {
		OUTP_ERR(&dp->outp, "no aux");
		return -ENODEV;
	}

	/* bios data is not optional */
	data = nvbios_dpout_match(bios, dp->outp.info.hasht,
				  dp->outp.info.hashm, &dp->version,
				  &hdr, &cnt, &len, &dp->info);
	if (!data) {
		OUTP_ERR(&dp->outp, "no bios dp data");
		return -ENODEV;
	}

	OUTP_DBG(&dp->outp, "bios dp %02x %02x %02x %02x",
		 dp->version, hdr, cnt, len);

	/* link maintenance */
	ret = nvkm_notify_init(NULL, &i2c->event, nvkm_dp_irq, true,
			       &(struct nvkm_i2c_ntfy_req) {
				.mask = NVKM_I2C_IRQ,
				.port = dp->aux->id,
			       },
			       sizeof(struct nvkm_i2c_ntfy_req),
			       sizeof(struct nvkm_i2c_ntfy_rep),
			       &dp->irq);
	if (ret) {
		OUTP_ERR(&dp->outp, "error monitoring aux irq: %d", ret);
		return ret;
	}

	mutex_init(&dp->mutex);
	atomic_set(&dp->lt.done, 0);

	/* hotplug detect, replaces gpio-based mechanism with aux events */
	ret = nvkm_notify_init(NULL, &i2c->event, nvkm_dp_hpd, true,
			       &(struct nvkm_i2c_ntfy_req) {
				.mask = NVKM_I2C_PLUG | NVKM_I2C_UNPLUG,
				.port = dp->aux->id,
			       },
			       sizeof(struct nvkm_i2c_ntfy_req),
			       sizeof(struct nvkm_i2c_ntfy_rep),
			       &dp->hpd);
	if (ret) {
		OUTP_ERR(&dp->outp, "error monitoring aux hpd: %d", ret);
		return ret;
	}

	return 0;
}

int
nvkm_output_dp_new_(const struct nvkm_output_dp_func *func,
		    struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
		    struct nvkm_outp **poutp)
{
	struct nvkm_i2c *i2c = disp->engine.subdev.device->i2c;
	struct nvkm_i2c_aux *aux;
	struct nvkm_dp *dp;

	if (dcbE->location == 0)
		aux = nvkm_i2c_aux_find(i2c, NVKM_I2C_AUX_CCB(dcbE->i2c_index));
	else
		aux = nvkm_i2c_aux_find(i2c, NVKM_I2C_AUX_EXT(dcbE->extdev));

	if (!(dp = kzalloc(sizeof(*dp), GFP_KERNEL)))
		return -ENOMEM;
	dp->func = func;
	*poutp = &dp->outp;

	return nvkm_dp_ctor(disp, index, dcbE, aux, dp);
}
