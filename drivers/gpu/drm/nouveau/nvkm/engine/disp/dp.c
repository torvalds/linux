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
#include "head.h"
#include "ior.h"

#include <subdev/bios.h>
#include <subdev/bios/init.h>
#include <subdev/gpio.h>
#include <subdev/i2c.h>

#include <nvif/event.h>

/* IED scripts are no longer used by UEFI/RM from Ampere, but have been updated for
 * the x86 option ROM.  However, the relevant VBIOS table versions weren't modified,
 * so we're unable to detect this in a nice way.
 */
#define AMPERE_IED_HACK(disp) ((disp)->engine.subdev.device->card_type >= GA100)

struct lt_state {
	struct nvkm_dp *dp;
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
	struct nvkm_ior *ior = dp->outp.ior;
	struct nvkm_bios *bios = ior->disp->engine.subdev.device->bios;
	struct nvbios_dpout info;
	struct nvbios_dpcfg ocfg;
	u8  ver, hdr, cnt, len;
	u32 data;
	int ret, i;

	for (i = 0; i < ior->dp.nr; i++) {
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

		data = nvbios_dpout_match(bios, dp->outp.info.hasht,
						dp->outp.info.hashm,
					  &ver, &hdr, &cnt, &len, &info);
		if (!data)
			continue;

		data = nvbios_dpcfg_match(bios, data, lpc2 & 3, lvsw & 3,
					  lpre & 3, &ver, &hdr, &cnt, &len,
					  &ocfg);
		if (!data)
			continue;

		ior->func->dp.drive(ior, i, ocfg.pc, ocfg.dc,
					    ocfg.pe, ocfg.tx_pu);
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
	dp->outp.ior->func->dp.pattern(dp->outp.ior, pattern);

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

	if (lt->dp->dpcd[DPCD_RC02] & DPCD_RC02_TPS3_SUPPORTED)
		nvkm_dp_train_pattern(lt, 3);
	else
		nvkm_dp_train_pattern(lt, 2);

	do {
		if ((tries &&
		    nvkm_dp_train_drive(lt, lt->pc2)) ||
		    nvkm_dp_train_sense(lt, lt->pc2, 400))
			break;

		eq_done = !!(lt->stat[2] & DPCD_LS04_INTERLANE_ALIGN_DONE);
		for (i = 0; i < lt->dp->outp.ior->dp.nr && eq_done; i++) {
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
		for (i = 0; i < lt->dp->outp.ior->dp.nr; i++) {
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
nvkm_dp_train_links(struct nvkm_dp *dp)
{
	struct nvkm_ior *ior = dp->outp.ior;
	struct nvkm_disp *disp = dp->outp.disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct lt_state lt = {
		.dp = dp,
	};
	u32 lnkcmp;
	u8 sink[2];
	int ret;

	OUTP_DBG(&dp->outp, "training %d x %d MB/s",
		 ior->dp.nr, ior->dp.bw * 27);

	/* Intersect misc. capabilities of the OR and sink. */
	if (disp->engine.subdev.device->chipset < 0xd0)
		dp->dpcd[DPCD_RC02] &= ~DPCD_RC02_TPS3_SUPPORTED;
	lt.pc2 = dp->dpcd[DPCD_RC02] & DPCD_RC02_TPS3_SUPPORTED;

	if (AMPERE_IED_HACK(disp) && (lnkcmp = lt.dp->info.script[0])) {
		/* Execute BeforeLinkTraining script from DP Info table. */
		while (ior->dp.bw < nvbios_rd08(bios, lnkcmp))
			lnkcmp += 3;
		lnkcmp = nvbios_rd16(bios, lnkcmp + 1);

		nvbios_init(&dp->outp.disp->engine.subdev, lnkcmp,
			init.outp = &dp->outp.info;
			init.or   = ior->id;
			init.link = ior->asy.link;
		);
	}

	/* Set desired link configuration on the source. */
	if ((lnkcmp = lt.dp->info.lnkcmp)) {
		if (dp->version < 0x30) {
			while ((ior->dp.bw * 2700) < nvbios_rd16(bios, lnkcmp))
				lnkcmp += 4;
			lnkcmp = nvbios_rd16(bios, lnkcmp + 2);
		} else {
			while (ior->dp.bw < nvbios_rd08(bios, lnkcmp))
				lnkcmp += 3;
			lnkcmp = nvbios_rd16(bios, lnkcmp + 1);
		}

		nvbios_init(subdev, lnkcmp,
			init.outp = &dp->outp.info;
			init.or   = ior->id;
			init.link = ior->asy.link;
		);
	}

	ret = ior->func->dp.links(ior, dp->aux);
	if (ret) {
		if (ret < 0) {
			OUTP_ERR(&dp->outp, "train failed with %d", ret);
			return ret;
		}
		return 0;
	}

	ior->func->dp.power(ior, ior->dp.nr);

	/* Set desired link configuration on the sink. */
	sink[0] = ior->dp.bw;
	sink[1] = ior->dp.nr;
	if (ior->dp.ef)
		sink[1] |= DPCD_LC01_ENHANCED_FRAME_EN;

	ret = nvkm_wraux(dp->aux, DPCD_LC00_LINK_BW_SET, sink, 2);
	if (ret)
		return ret;

	/* Attempt to train the link in this configuration. */
	memset(lt.stat, 0x00, sizeof(lt.stat));
	ret = nvkm_dp_train_cr(&lt);
	if (ret == 0)
		ret = nvkm_dp_train_eq(&lt);
	nvkm_dp_train_pattern(&lt, 0);
	return ret;
}

static void
nvkm_dp_train_fini(struct nvkm_dp *dp)
{
	/* Execute AfterLinkTraining script from DP Info table. */
	nvbios_init(&dp->outp.disp->engine.subdev, dp->info.script[1],
		init.outp = &dp->outp.info;
		init.or   = dp->outp.ior->id;
		init.link = dp->outp.ior->asy.link;
	);
}

static void
nvkm_dp_train_init(struct nvkm_dp *dp)
{
	/* Execute EnableSpread/DisableSpread script from DP Info table. */
	if (dp->dpcd[DPCD_RC03] & DPCD_RC03_MAX_DOWNSPREAD) {
		nvbios_init(&dp->outp.disp->engine.subdev, dp->info.script[2],
			init.outp = &dp->outp.info;
			init.or   = dp->outp.ior->id;
			init.link = dp->outp.ior->asy.link;
		);
	} else {
		nvbios_init(&dp->outp.disp->engine.subdev, dp->info.script[3],
			init.outp = &dp->outp.info;
			init.or   = dp->outp.ior->id;
			init.link = dp->outp.ior->asy.link;
		);
	}

	if (!AMPERE_IED_HACK(dp->outp.disp)) {
		/* Execute BeforeLinkTraining script from DP Info table. */
		nvbios_init(&dp->outp.disp->engine.subdev, dp->info.script[0],
			init.outp = &dp->outp.info;
			init.or   = dp->outp.ior->id;
			init.link = dp->outp.ior->asy.link;
		);
	}
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

static int
nvkm_dp_train(struct nvkm_dp *dp, u32 dataKBps)
{
	struct nvkm_ior *ior = dp->outp.ior;
	const u8 sink_nr = dp->dpcd[DPCD_RC02] & DPCD_RC02_MAX_LANE_COUNT;
	const u8 sink_bw = dp->dpcd[DPCD_RC01_MAX_LINK_RATE];
	const u8 outp_nr = dp->outp.info.dpconf.link_nr;
	const u8 outp_bw = dp->outp.info.dpconf.link_bw;
	const struct dp_rates *failsafe = NULL, *cfg;
	int ret = -EINVAL;
	u8  pwr;

	/* Find the lowest configuration of the OR that can support
	 * the required link rate.
	 *
	 * We will refuse to program the OR to lower rates, even if
	 * link training fails at higher rates (or even if the sink
	 * can't support the rate at all, though the DD is supposed
	 * to prevent such situations from happening).
	 *
	 * Attempting to do so can cause the entire display to hang,
	 * and it's better to have a failed modeset than that.
	 */
	for (cfg = nvkm_dp_rates; cfg->rate; cfg++) {
		if (cfg->nr <= outp_nr && cfg->bw <= outp_bw) {
			/* Try to respect sink limits too when selecting
			 * lowest link configuration.
			 */
			if (!failsafe ||
			    (cfg->nr <= sink_nr && cfg->bw <= sink_bw))
				failsafe = cfg;
		}

		if (failsafe && cfg[1].rate < dataKBps)
			break;
	}

	if (WARN_ON(!failsafe))
		return ret;

	/* Ensure sink is not in a low-power state. */
	if (!nvkm_rdaux(dp->aux, DPCD_SC00, &pwr, 1)) {
		if ((pwr & DPCD_SC00_SET_POWER) != DPCD_SC00_SET_POWER_D0) {
			pwr &= ~DPCD_SC00_SET_POWER;
			pwr |=  DPCD_SC00_SET_POWER_D0;
			nvkm_wraux(dp->aux, DPCD_SC00, &pwr, 1);
		}
	}

	/* Link training. */
	OUTP_DBG(&dp->outp, "training (min: %d x %d MB/s)",
		 failsafe->nr, failsafe->bw * 27);
	nvkm_dp_train_init(dp);
	for (cfg = nvkm_dp_rates; ret < 0 && cfg <= failsafe; cfg++) {
		/* Skip configurations not supported by both OR and sink. */
		if ((cfg->nr > outp_nr || cfg->bw > outp_bw ||
		     cfg->nr > sink_nr || cfg->bw > sink_bw)) {
			if (cfg != failsafe)
				continue;
			OUTP_ERR(&dp->outp, "link rate unsupported by sink");
		}
		ior->dp.mst = dp->lt.mst;
		ior->dp.ef = dp->dpcd[DPCD_RC02] & DPCD_RC02_ENHANCED_FRAME_CAP;
		ior->dp.bw = cfg->bw;
		ior->dp.nr = cfg->nr;

		/* Program selected link configuration. */
		ret = nvkm_dp_train_links(dp);
	}
	nvkm_dp_train_fini(dp);
	if (ret < 0)
		OUTP_ERR(&dp->outp, "training failed");
	else
		OUTP_DBG(&dp->outp, "training done");
	atomic_set(&dp->lt.done, 1);
	return ret;
}

void
nvkm_dp_disable(struct nvkm_outp *outp, struct nvkm_ior *ior)
{
	struct nvkm_dp *dp = nvkm_dp(outp);

	/* Execute DisableLT script from DP Info Table. */
	nvbios_init(&ior->disp->engine.subdev, dp->info.script[4],
		init.outp = &dp->outp.info;
		init.or   = ior->id;
		init.link = ior->arm.link;
	);
}

static void
nvkm_dp_release(struct nvkm_outp *outp)
{
	struct nvkm_dp *dp = nvkm_dp(outp);

	/* Prevent link from being retrained if sink sends an IRQ. */
	atomic_set(&dp->lt.done, 0);
	dp->outp.ior->dp.nr = 0;
}

static int
nvkm_dp_acquire(struct nvkm_outp *outp)
{
	struct nvkm_dp *dp = nvkm_dp(outp);
	struct nvkm_ior *ior = dp->outp.ior;
	struct nvkm_head *head;
	bool retrain = true;
	u32 datakbps = 0;
	u32 dataKBps;
	u32 linkKBps;
	u8  stat[3];
	int ret, i;

	mutex_lock(&dp->mutex);

	/* Check that link configuration meets current requirements. */
	list_for_each_entry(head, &outp->disp->head, head) {
		if (ior->asy.head & (1 << head->id)) {
			u32 khz = (head->asy.hz >> ior->asy.rgdiv) / 1000;
			datakbps += khz * head->asy.or.depth;
		}
	}

	linkKBps = ior->dp.bw * 27000 * ior->dp.nr;
	dataKBps = DIV_ROUND_UP(datakbps, 8);
	OUTP_DBG(&dp->outp, "data %d KB/s link %d KB/s mst %d->%d",
		 dataKBps, linkKBps, ior->dp.mst, dp->lt.mst);
	if (linkKBps < dataKBps || ior->dp.mst != dp->lt.mst) {
		OUTP_DBG(&dp->outp, "link requirements changed");
		goto done;
	}

	/* Check that link is still trained. */
	ret = nvkm_rdaux(dp->aux, DPCD_LS02, stat, 3);
	if (ret) {
		OUTP_DBG(&dp->outp,
			 "failed to read link status, assuming no sink");
		goto done;
	}

	if (stat[2] & DPCD_LS04_INTERLANE_ALIGN_DONE) {
		for (i = 0; i < ior->dp.nr; i++) {
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
	if (retrain || !atomic_read(&dp->lt.done))
		ret = nvkm_dp_train(dp, dataKBps);
	mutex_unlock(&dp->mutex);
	return ret;
}

static bool
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
				sizeof(dp->dpcd)))
			return true;
	}

	if (dp->present) {
		OUTP_DBG(&dp->outp, "aux power -> demand");
		nvkm_i2c_aux_monitor(aux, false);
		dp->present = false;
	}

	atomic_set(&dp->lt.done, 0);
	return false;
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
	if (line->mask & NVKM_I2C_IRQ) {
		if (atomic_read(&dp->lt.done))
			dp->outp.func->acquire(&dp->outp);
		rep.mask |= NVIF_NOTIFY_CONN_V0_IRQ;
	} else {
		nvkm_dp_enable(dp, true);
	}

	if (line->mask & NVKM_I2C_UNPLUG)
		rep.mask |= NVIF_NOTIFY_CONN_V0_UNPLUG;
	if (line->mask & NVKM_I2C_PLUG)
		rep.mask |= NVIF_NOTIFY_CONN_V0_PLUG;

	nvkm_event_send(&disp->hpd, rep.mask, conn->index, &rep, sizeof(rep));
	return NVKM_NOTIFY_KEEP;
}

static void
nvkm_dp_fini(struct nvkm_outp *outp)
{
	struct nvkm_dp *dp = nvkm_dp(outp);
	nvkm_notify_put(&dp->hpd);
	nvkm_dp_enable(dp, false);
}

static void
nvkm_dp_init(struct nvkm_outp *outp)
{
	struct nvkm_gpio *gpio = outp->disp->engine.subdev.device->gpio;
	struct nvkm_dp *dp = nvkm_dp(outp);

	nvkm_notify_put(&dp->outp.conn->hpd);

	/* eDP panels need powering on by us (if the VBIOS doesn't default it
	 * to on) before doing any AUX channel transactions.  LVDS panel power
	 * is handled by the SOR itself, and not required for LVDS DDC.
	 */
	if (dp->outp.conn->info.type == DCB_CONNECTOR_eDP) {
		int power = nvkm_gpio_get(gpio, 0, DCB_GPIO_PANEL_POWER, 0xff);
		if (power == 0)
			nvkm_gpio_set(gpio, 0, DCB_GPIO_PANEL_POWER, 0xff, 1);

		/* We delay here unconditionally, even if already powered,
		 * because some laptop panels having a significant resume
		 * delay before the panel begins responding.
		 *
		 * This is likely a bit of a hack, but no better idea for
		 * handling this at the moment.
		 */
		msleep(300);

		/* If the eDP panel can't be detected, we need to restore
		 * the panel power GPIO to avoid breaking another output.
		 */
		if (!nvkm_dp_enable(dp, true) && power == 0)
			nvkm_gpio_set(gpio, 0, DCB_GPIO_PANEL_POWER, 0xff, 0);
	} else {
		nvkm_dp_enable(dp, true);
	}

	nvkm_notify_get(&dp->hpd);
}

static void *
nvkm_dp_dtor(struct nvkm_outp *outp)
{
	struct nvkm_dp *dp = nvkm_dp(outp);
	nvkm_notify_fini(&dp->hpd);
	return dp;
}

static const struct nvkm_outp_func
nvkm_dp_func = {
	.dtor = nvkm_dp_dtor,
	.init = nvkm_dp_init,
	.fini = nvkm_dp_fini,
	.acquire = nvkm_dp_acquire,
	.release = nvkm_dp_release,
	.disable = nvkm_dp_disable,
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

	ret = nvkm_outp_ctor(&nvkm_dp_func, disp, index, dcbE, &dp->outp);
	if (ret)
		return ret;

	dp->aux = aux;
	if (!dp->aux) {
		OUTP_ERR(&dp->outp, "no aux");
		return -EINVAL;
	}

	/* bios data is not optional */
	data = nvbios_dpout_match(bios, dp->outp.info.hasht,
				  dp->outp.info.hashm, &dp->version,
				  &hdr, &cnt, &len, &dp->info);
	if (!data) {
		OUTP_ERR(&dp->outp, "no bios dp data");
		return -EINVAL;
	}

	OUTP_DBG(&dp->outp, "bios dp %02x %02x %02x %02x",
		 dp->version, hdr, cnt, len);

	/* hotplug detect, replaces gpio-based mechanism with aux events */
	ret = nvkm_notify_init(NULL, &i2c->event, nvkm_dp_hpd, true,
			       &(struct nvkm_i2c_ntfy_req) {
				.mask = NVKM_I2C_PLUG | NVKM_I2C_UNPLUG |
					NVKM_I2C_IRQ,
				.port = dp->aux->id,
			       },
			       sizeof(struct nvkm_i2c_ntfy_req),
			       sizeof(struct nvkm_i2c_ntfy_rep),
			       &dp->hpd);
	if (ret) {
		OUTP_ERR(&dp->outp, "error monitoring aux hpd: %d", ret);
		return ret;
	}

	mutex_init(&dp->mutex);
	atomic_set(&dp->lt.done, 0);
	return 0;
}

int
nvkm_dp_new(struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
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
	*poutp = &dp->outp;

	return nvkm_dp_ctor(disp, index, dcbE, aux, dp);
}
