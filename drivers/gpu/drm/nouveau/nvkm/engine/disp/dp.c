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
	struct nvkm_outp *outp;

	int repeaters;
	int repeater;

	u8  stat[6];
	u8  conf[4];
	bool pc2;
	u8  pc2stat;
	u8  pc2conf[2];
};

static int
nvkm_dp_train_sense(struct lt_state *lt, bool pc, u32 delay)
{
	struct nvkm_outp *outp = lt->outp;
	u32 addr;
	int ret;

	usleep_range(delay, delay * 2);

	if (lt->repeater)
		addr = DPCD_LTTPR_LANE0_1_STATUS(lt->repeater);
	else
		addr = DPCD_LS02;

	ret = nvkm_rdaux(outp->dp.aux, addr, &lt->stat[0], 3);
	if (ret)
		return ret;

	if (lt->repeater)
		addr = DPCD_LTTPR_LANE0_1_ADJUST(lt->repeater);
	else
		addr = DPCD_LS06;

	ret = nvkm_rdaux(outp->dp.aux, addr, &lt->stat[4], 2);
	if (ret)
		return ret;

	if (pc) {
		ret = nvkm_rdaux(outp->dp.aux, DPCD_LS0C, &lt->pc2stat, 1);
		if (ret)
			lt->pc2stat = 0x00;

		OUTP_TRACE(outp, "status %6ph pc2 %02x", lt->stat, lt->pc2stat);
	} else {
		OUTP_TRACE(outp, "status %6ph", lt->stat);
	}

	return 0;
}

static int
nvkm_dp_train_drive(struct lt_state *lt, bool pc)
{
	struct nvkm_outp *outp = lt->outp;
	struct nvkm_ior *ior = outp->ior;
	struct nvkm_bios *bios = ior->disp->engine.subdev.device->bios;
	struct nvbios_dpout info;
	struct nvbios_dpcfg ocfg;
	u8  ver, hdr, cnt, len;
	u32 addr;
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

		OUTP_TRACE(outp, "config lane %d %02x %02x", i, lt->conf[i], lpc2);

		if (lt->repeater != lt->repeaters)
			continue;

		data = nvbios_dpout_match(bios, outp->info.hasht, outp->info.hashm,
					  &ver, &hdr, &cnt, &len, &info);
		if (!data)
			continue;

		data = nvbios_dpcfg_match(bios, data, lpc2 & 3, lvsw & 3, lpre & 3,
					  &ver, &hdr, &cnt, &len, &ocfg);
		if (!data)
			continue;

		ior->func->dp->drive(ior, i, ocfg.pc, ocfg.dc, ocfg.pe, ocfg.tx_pu);
	}

	if (lt->repeater)
		addr = DPCD_LTTPR_LANE0_SET(lt->repeater);
	else
		addr = DPCD_LC03(0);

	ret = nvkm_wraux(outp->dp.aux, addr, lt->conf, 4);
	if (ret)
		return ret;

	if (pc) {
		ret = nvkm_wraux(outp->dp.aux, DPCD_LC0F, lt->pc2conf, 2);
		if (ret)
			return ret;
	}

	return 0;
}

static void
nvkm_dp_train_pattern(struct lt_state *lt, u8 pattern)
{
	struct nvkm_outp *outp = lt->outp;
	u32 addr;
	u8 sink_tp;

	OUTP_TRACE(outp, "training pattern %d", pattern);
	outp->ior->func->dp->pattern(outp->ior, pattern);

	if (lt->repeater)
		addr = DPCD_LTTPR_PATTERN_SET(lt->repeater);
	else
		addr = DPCD_LC02;

	nvkm_rdaux(outp->dp.aux, addr, &sink_tp, 1);
	sink_tp &= ~DPCD_LC02_TRAINING_PATTERN_SET;
	sink_tp |= (pattern != 4) ? pattern : 7;

	if (pattern != 0)
		sink_tp |=  DPCD_LC02_SCRAMBLING_DISABLE;
	else
		sink_tp &= ~DPCD_LC02_SCRAMBLING_DISABLE;
	nvkm_wraux(outp->dp.aux, addr, &sink_tp, 1);
}

static int
nvkm_dp_train_eq(struct lt_state *lt)
{
	struct nvkm_i2c_aux *aux = lt->outp->dp.aux;
	bool eq_done = false, cr_done = true;
	int tries = 0, usec = 0, i;
	u8 data;

	if (lt->repeater) {
		if (!nvkm_rdaux(aux, DPCD_LTTPR_AUX_RD_INTERVAL(lt->repeater), &data, sizeof(data)))
			usec = (data & DPCD_RC0E_AUX_RD_INTERVAL) * 4000;

		nvkm_dp_train_pattern(lt, 4);
	} else {
		if (lt->outp->dp.dpcd[DPCD_RC00_DPCD_REV] >= 0x14 &&
		    lt->outp->dp.dpcd[DPCD_RC03] & DPCD_RC03_TPS4_SUPPORTED)
			nvkm_dp_train_pattern(lt, 4);
		else
		if (lt->outp->dp.dpcd[DPCD_RC00_DPCD_REV] >= 0x12 &&
		    lt->outp->dp.dpcd[DPCD_RC02] & DPCD_RC02_TPS3_SUPPORTED)
			nvkm_dp_train_pattern(lt, 3);
		else
			nvkm_dp_train_pattern(lt, 2);

		usec = (lt->outp->dp.dpcd[DPCD_RC0E] & DPCD_RC0E_AUX_RD_INTERVAL) * 4000;
	}

	do {
		if ((tries &&
		    nvkm_dp_train_drive(lt, lt->pc2)) ||
		    nvkm_dp_train_sense(lt, lt->pc2, usec ? usec : 400))
			break;

		eq_done = !!(lt->stat[2] & DPCD_LS04_INTERLANE_ALIGN_DONE);
		for (i = 0; i < lt->outp->ior->dp.nr && eq_done; i++) {
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
	int tries = 0, usec = 0, i;

	nvkm_dp_train_pattern(lt, 1);

	if (lt->outp->dp.dpcd[DPCD_RC00_DPCD_REV] < 0x14 && !lt->repeater)
		usec = (lt->outp->dp.dpcd[DPCD_RC0E] & DPCD_RC0E_AUX_RD_INTERVAL) * 4000;

	do {
		if (nvkm_dp_train_drive(lt, false) ||
		    nvkm_dp_train_sense(lt, false, usec ? usec : 100))
			break;

		cr_done = true;
		for (i = 0; i < lt->outp->ior->dp.nr; i++) {
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
nvkm_dp_train_links(struct nvkm_outp *outp, int rate)
{
	struct nvkm_ior *ior = outp->ior;
	struct nvkm_disp *disp = outp->disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct lt_state lt = {
		.outp = outp,
	};
	u32 lnkcmp;
	u8 sink[2], data;
	int ret;

	OUTP_DBG(outp, "training %d x %d MB/s", ior->dp.nr, ior->dp.bw * 27);

	/* Intersect misc. capabilities of the OR and sink. */
	if (disp->engine.subdev.device->chipset < 0x110)
		outp->dp.dpcd[DPCD_RC03] &= ~DPCD_RC03_TPS4_SUPPORTED;
	if (disp->engine.subdev.device->chipset < 0xd0)
		outp->dp.dpcd[DPCD_RC02] &= ~DPCD_RC02_TPS3_SUPPORTED;
	lt.pc2 = outp->dp.dpcd[DPCD_RC02] & DPCD_RC02_TPS3_SUPPORTED;

	if (AMPERE_IED_HACK(disp) && (lnkcmp = lt.outp->dp.info.script[0])) {
		/* Execute BeforeLinkTraining script from DP Info table. */
		while (ior->dp.bw < nvbios_rd08(bios, lnkcmp))
			lnkcmp += 3;
		lnkcmp = nvbios_rd16(bios, lnkcmp + 1);

		nvbios_init(&outp->disp->engine.subdev, lnkcmp,
			init.outp = &outp->info;
			init.or   = ior->id;
			init.link = ior->asy.link;
		);
	}

	/* Set desired link configuration on the source. */
	if ((lnkcmp = lt.outp->dp.info.lnkcmp)) {
		if (outp->dp.version < 0x30) {
			while ((ior->dp.bw * 2700) < nvbios_rd16(bios, lnkcmp))
				lnkcmp += 4;
			lnkcmp = nvbios_rd16(bios, lnkcmp + 2);
		} else {
			while (ior->dp.bw < nvbios_rd08(bios, lnkcmp))
				lnkcmp += 3;
			lnkcmp = nvbios_rd16(bios, lnkcmp + 1);
		}

		nvbios_init(subdev, lnkcmp,
			init.outp = &outp->info;
			init.or   = ior->id;
			init.link = ior->asy.link;
		);
	}

	ret = ior->func->dp->links(ior, outp->dp.aux);
	if (ret) {
		if (ret < 0) {
			OUTP_ERR(outp, "train failed with %d", ret);
			return ret;
		}
		return 0;
	}

	ior->func->dp->power(ior, ior->dp.nr);

	/* Select LTTPR non-transparent mode if we have a valid configuration,
	 * use transparent mode otherwise.
	 */
	if (outp->dp.lttpr[0] >= 0x14) {
		data = DPCD_LTTPR_MODE_TRANSPARENT;
		nvkm_wraux(outp->dp.aux, DPCD_LTTPR_MODE, &data, sizeof(data));

		if (outp->dp.lttprs) {
			data = DPCD_LTTPR_MODE_NON_TRANSPARENT;
			nvkm_wraux(outp->dp.aux, DPCD_LTTPR_MODE, &data, sizeof(data));
			lt.repeaters = outp->dp.lttprs;
		}
	}

	/* Set desired link configuration on the sink. */
	sink[0] = (outp->dp.rate[rate].dpcd < 0) ? ior->dp.bw : 0;
	sink[1] = ior->dp.nr;
	if (ior->dp.ef)
		sink[1] |= DPCD_LC01_ENHANCED_FRAME_EN;

	ret = nvkm_wraux(outp->dp.aux, DPCD_LC00_LINK_BW_SET, sink, 2);
	if (ret)
		return ret;

	if (outp->dp.rate[rate].dpcd >= 0) {
		ret = nvkm_rdaux(outp->dp.aux, DPCD_LC15_LINK_RATE_SET, &sink[0], sizeof(sink[0]));
		if (ret)
			return ret;

		sink[0] &= ~DPCD_LC15_LINK_RATE_SET_MASK;
		sink[0] |= outp->dp.rate[rate].dpcd;

		ret = nvkm_wraux(outp->dp.aux, DPCD_LC15_LINK_RATE_SET, &sink[0], sizeof(sink[0]));
		if (ret)
			return ret;
	}

	/* Attempt to train the link in this configuration. */
	for (lt.repeater = lt.repeaters; lt.repeater >= 0; lt.repeater--) {
		if (lt.repeater)
			OUTP_DBG(outp, "training LTTPR%d", lt.repeater);
		else
			OUTP_DBG(outp, "training sink");

		memset(lt.stat, 0x00, sizeof(lt.stat));
		ret = nvkm_dp_train_cr(&lt);
		if (ret == 0)
			ret = nvkm_dp_train_eq(&lt);
		nvkm_dp_train_pattern(&lt, 0);
	}

	return ret;
}

static void
nvkm_dp_train_fini(struct nvkm_outp *outp)
{
	/* Execute AfterLinkTraining script from DP Info table. */
	nvbios_init(&outp->disp->engine.subdev, outp->dp.info.script[1],
		init.outp = &outp->info;
		init.or   = outp->ior->id;
		init.link = outp->ior->asy.link;
	);
}

static void
nvkm_dp_train_init(struct nvkm_outp *outp)
{
	/* Execute EnableSpread/DisableSpread script from DP Info table. */
	if (outp->dp.dpcd[DPCD_RC03] & DPCD_RC03_MAX_DOWNSPREAD) {
		nvbios_init(&outp->disp->engine.subdev, outp->dp.info.script[2],
			init.outp = &outp->info;
			init.or   = outp->ior->id;
			init.link = outp->ior->asy.link;
		);
	} else {
		nvbios_init(&outp->disp->engine.subdev, outp->dp.info.script[3],
			init.outp = &outp->info;
			init.or   = outp->ior->id;
			init.link = outp->ior->asy.link;
		);
	}

	if (!AMPERE_IED_HACK(outp->disp)) {
		/* Execute BeforeLinkTraining script from DP Info table. */
		nvbios_init(&outp->disp->engine.subdev, outp->dp.info.script[0],
			init.outp = &outp->info;
			init.or   = outp->ior->id;
			init.link = outp->ior->asy.link;
		);
	}
}

static int
nvkm_dp_train(struct nvkm_outp *outp, u32 dataKBps)
{
	struct nvkm_ior *ior = outp->ior;
	int ret = -EINVAL, nr, rate;
	u8  pwr;

	/* Ensure sink is not in a low-power state. */
	if (!nvkm_rdaux(outp->dp.aux, DPCD_SC00, &pwr, 1)) {
		if ((pwr & DPCD_SC00_SET_POWER) != DPCD_SC00_SET_POWER_D0) {
			pwr &= ~DPCD_SC00_SET_POWER;
			pwr |=  DPCD_SC00_SET_POWER_D0;
			nvkm_wraux(outp->dp.aux, DPCD_SC00, &pwr, 1);
		}
	}

	ior->dp.mst = outp->dp.lt.mst;
	ior->dp.ef = outp->dp.dpcd[DPCD_RC02] & DPCD_RC02_ENHANCED_FRAME_CAP;
	ior->dp.nr = 0;

	/* Link training. */
	OUTP_DBG(outp, "training");
	nvkm_dp_train_init(outp);
	for (nr = outp->dp.links; ret < 0 && nr; nr >>= 1) {
		for (rate = 0; ret < 0 && rate < outp->dp.rates; rate++) {
			if (outp->dp.rate[rate].rate * nr >= dataKBps || WARN_ON(!ior->dp.nr)) {
				/* Program selected link configuration. */
				ior->dp.bw = outp->dp.rate[rate].rate / 27000;
				ior->dp.nr = nr;
				ret = nvkm_dp_train_links(outp, rate);
			}
		}
	}
	nvkm_dp_train_fini(outp);
	if (ret < 0)
		OUTP_ERR(outp, "training failed");
	else
		OUTP_DBG(outp, "training done");
	atomic_set(&outp->dp.lt.done, 1);
	return ret;
}

void
nvkm_dp_disable(struct nvkm_outp *outp, struct nvkm_ior *ior)
{
	/* Execute DisableLT script from DP Info Table. */
	nvbios_init(&ior->disp->engine.subdev, outp->dp.info.script[4],
		init.outp = &outp->info;
		init.or   = ior->id;
		init.link = ior->arm.link;
	);
}

static void
nvkm_dp_release(struct nvkm_outp *outp)
{
	/* Prevent link from being retrained if sink sends an IRQ. */
	atomic_set(&outp->dp.lt.done, 0);
	outp->ior->dp.nr = 0;
}

static int
nvkm_dp_acquire(struct nvkm_outp *outp)
{
	struct nvkm_ior *ior = outp->ior;
	struct nvkm_head *head;
	bool retrain = true;
	u32 datakbps = 0;
	u32 dataKBps;
	u32 linkKBps;
	u8  stat[3];
	int ret, i;

	mutex_lock(&outp->dp.mutex);

	/* Check that link configuration meets current requirements. */
	list_for_each_entry(head, &outp->disp->heads, head) {
		if (ior->asy.head & (1 << head->id)) {
			u32 khz = (head->asy.hz >> ior->asy.rgdiv) / 1000;
			datakbps += khz * head->asy.or.depth;
		}
	}

	linkKBps = ior->dp.bw * 27000 * ior->dp.nr;
	dataKBps = DIV_ROUND_UP(datakbps, 8);
	OUTP_DBG(outp, "data %d KB/s link %d KB/s mst %d->%d",
		 dataKBps, linkKBps, ior->dp.mst, outp->dp.lt.mst);
	if (linkKBps < dataKBps || ior->dp.mst != outp->dp.lt.mst) {
		OUTP_DBG(outp, "link requirements changed");
		goto done;
	}

	/* Check that link is still trained. */
	ret = nvkm_rdaux(outp->dp.aux, DPCD_LS02, stat, 3);
	if (ret) {
		OUTP_DBG(outp, "failed to read link status, assuming no sink");
		goto done;
	}

	if (stat[2] & DPCD_LS04_INTERLANE_ALIGN_DONE) {
		for (i = 0; i < ior->dp.nr; i++) {
			u8 lane = (stat[i >> 1] >> ((i & 1) * 4)) & 0x0f;
			if (!(lane & DPCD_LS02_LANE0_CR_DONE) ||
			    !(lane & DPCD_LS02_LANE0_CHANNEL_EQ_DONE) ||
			    !(lane & DPCD_LS02_LANE0_SYMBOL_LOCKED)) {
				OUTP_DBG(outp, "lane %d not equalised", lane);
				goto done;
			}
		}
		retrain = false;
	} else {
		OUTP_DBG(outp, "no inter-lane alignment");
	}

done:
	if (retrain || !atomic_read(&outp->dp.lt.done))
		ret = nvkm_dp_train(outp, dataKBps);
	mutex_unlock(&outp->dp.mutex);
	return ret;
}

static bool
nvkm_dp_enable_supported_link_rates(struct nvkm_outp *outp)
{
	u8 sink_rates[DPCD_RC10_SUPPORTED_LINK_RATES__SIZE];
	int i, j, k;

	if (outp->conn->info.type != DCB_CONNECTOR_eDP ||
	    outp->dp.dpcd[DPCD_RC00_DPCD_REV] < 0x13 ||
	    nvkm_rdaux(outp->dp.aux, DPCD_RC10_SUPPORTED_LINK_RATES(0),
		       sink_rates, sizeof(sink_rates)))
		return false;

	for (i = 0; i < ARRAY_SIZE(sink_rates); i += 2) {
		const u32 rate = ((sink_rates[i + 1] << 8) | sink_rates[i]) * 200 / 10;

		if (!rate || WARN_ON(outp->dp.rates == ARRAY_SIZE(outp->dp.rate)))
			break;

		if (rate > outp->info.dpconf.link_bw * 27000) {
			OUTP_DBG(outp, "rate %d !outp", rate);
			continue;
		}

		for (j = 0; j < outp->dp.rates; j++) {
			if (rate > outp->dp.rate[j].rate) {
				for (k = outp->dp.rates; k > j; k--)
					outp->dp.rate[k] = outp->dp.rate[k - 1];
				break;
			}
		}

		outp->dp.rate[j].dpcd = i / 2;
		outp->dp.rate[j].rate = rate;
		outp->dp.rates++;
	}

	for (i = 0; i < outp->dp.rates; i++)
		OUTP_DBG(outp, "link_rate[%d] = %d", outp->dp.rate[i].dpcd, outp->dp.rate[i].rate);

	return outp->dp.rates != 0;
}

static bool
nvkm_dp_enable(struct nvkm_outp *outp, bool enable)
{
	struct nvkm_i2c_aux *aux = outp->dp.aux;

	if (enable) {
		if (!outp->dp.present) {
			OUTP_DBG(outp, "aux power -> always");
			nvkm_i2c_aux_monitor(aux, true);
			outp->dp.present = true;
		}

		/* Detect any LTTPRs before reading DPCD receiver caps. */
		if (!nvkm_rdaux(aux, DPCD_LTTPR_REV, outp->dp.lttpr, sizeof(outp->dp.lttpr)) &&
		    outp->dp.lttpr[0] >= 0x14 && outp->dp.lttpr[2]) {
			switch (outp->dp.lttpr[2]) {
			case 0x80: outp->dp.lttprs = 1; break;
			case 0x40: outp->dp.lttprs = 2; break;
			case 0x20: outp->dp.lttprs = 3; break;
			case 0x10: outp->dp.lttprs = 4; break;
			case 0x08: outp->dp.lttprs = 5; break;
			case 0x04: outp->dp.lttprs = 6; break;
			case 0x02: outp->dp.lttprs = 7; break;
			case 0x01: outp->dp.lttprs = 8; break;
			default:
				/* Unknown LTTPR count, we'll switch to transparent mode. */
				WARN_ON(1);
				outp->dp.lttprs = 0;
				break;
			}
		} else {
			/* No LTTPR support, or zero LTTPR count - don't touch it at all. */
			memset(outp->dp.lttpr, 0x00, sizeof(outp->dp.lttpr));
		}

		if (!nvkm_rdaux(aux, DPCD_RC00_DPCD_REV, outp->dp.dpcd, sizeof(outp->dp.dpcd))) {
			const u8 rates[] = { 0x1e, 0x14, 0x0a, 0x06, 0 };
			const u8 *rate;
			int rate_max;

			outp->dp.rates = 0;
			outp->dp.links = outp->dp.dpcd[DPCD_RC02] & DPCD_RC02_MAX_LANE_COUNT;
			outp->dp.links = min(outp->dp.links, outp->info.dpconf.link_nr);
			if (outp->dp.lttprs && outp->dp.lttpr[4])
				outp->dp.links = min_t(int, outp->dp.links, outp->dp.lttpr[4]);

			rate_max = outp->dp.dpcd[DPCD_RC01_MAX_LINK_RATE];
			rate_max = min(rate_max, outp->info.dpconf.link_bw);
			if (outp->dp.lttprs && outp->dp.lttpr[1])
				rate_max = min_t(int, rate_max, outp->dp.lttpr[1]);

			if (!nvkm_dp_enable_supported_link_rates(outp)) {
				for (rate = rates; *rate; rate++) {
					if (*rate > rate_max)
						continue;

					if (WARN_ON(outp->dp.rates == ARRAY_SIZE(outp->dp.rate)))
						break;

					outp->dp.rate[outp->dp.rates].dpcd = -1;
					outp->dp.rate[outp->dp.rates].rate = *rate * 27000;
					outp->dp.rates++;
				}
			}

			return true;
		}
	}

	if (outp->dp.present) {
		OUTP_DBG(outp, "aux power -> demand");
		nvkm_i2c_aux_monitor(aux, false);
		outp->dp.present = false;
	}

	atomic_set(&outp->dp.lt.done, 0);
	return false;
}

static int
nvkm_dp_hpd(struct nvkm_notify *notify)
{
	const struct nvkm_i2c_ntfy_rep *line = notify->data;
	struct nvkm_outp *outp = container_of(notify, typeof(*outp), dp.hpd);
	struct nvkm_conn *conn = outp->conn;
	struct nvkm_disp *disp = outp->disp;
	struct nvif_notify_conn_rep_v0 rep = {};

	OUTP_DBG(outp, "HPD: %d", line->mask);
	if (line->mask & NVKM_I2C_IRQ) {
		if (atomic_read(&outp->dp.lt.done))
			outp->func->acquire(outp);
		rep.mask |= NVIF_NOTIFY_CONN_V0_IRQ;
	} else {
		nvkm_dp_enable(outp, true);
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
	nvkm_notify_put(&outp->dp.hpd);
	nvkm_dp_enable(outp, false);
}

static void
nvkm_dp_init(struct nvkm_outp *outp)
{
	struct nvkm_gpio *gpio = outp->disp->engine.subdev.device->gpio;

	nvkm_notify_put(&outp->conn->hpd);

	/* eDP panels need powering on by us (if the VBIOS doesn't default it
	 * to on) before doing any AUX channel transactions.  LVDS panel power
	 * is handled by the SOR itself, and not required for LVDS DDC.
	 */
	if (outp->conn->info.type == DCB_CONNECTOR_eDP) {
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
		if (!nvkm_dp_enable(outp, true) && power == 0)
			nvkm_gpio_set(gpio, 0, DCB_GPIO_PANEL_POWER, 0xff, 0);
	} else {
		nvkm_dp_enable(outp, true);
	}

	nvkm_notify_get(&outp->dp.hpd);
}

static void *
nvkm_dp_dtor(struct nvkm_outp *outp)
{
	nvkm_notify_fini(&outp->dp.hpd);
	return outp;
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

int
nvkm_dp_new(struct nvkm_disp *disp, int index, struct dcb_output *dcbE, struct nvkm_outp **poutp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	struct nvkm_bios *bios = device->bios;
	struct nvkm_i2c *i2c = device->i2c;
	struct nvkm_outp *outp;
	u8  hdr, cnt, len;
	u32 data;
	int ret;

	ret = nvkm_outp_new_(&nvkm_dp_func, disp, index, dcbE, poutp);
	outp = *poutp;
	if (ret)
		return ret;

	if (dcbE->location == 0)
		outp->dp.aux = nvkm_i2c_aux_find(i2c, NVKM_I2C_AUX_CCB(dcbE->i2c_index));
	else
		outp->dp.aux = nvkm_i2c_aux_find(i2c, NVKM_I2C_AUX_EXT(dcbE->extdev));
	if (!outp->dp.aux) {
		OUTP_ERR(outp, "no aux");
		return -EINVAL;
	}

	/* bios data is not optional */
	data = nvbios_dpout_match(bios, outp->info.hasht, outp->info.hashm,
				  &outp->dp.version, &hdr, &cnt, &len, &outp->dp.info);
	if (!data) {
		OUTP_ERR(outp, "no bios dp data");
		return -EINVAL;
	}

	OUTP_DBG(outp, "bios dp %02x %02x %02x %02x", outp->dp.version, hdr, cnt, len);

	/* hotplug detect, replaces gpio-based mechanism with aux events */
	ret = nvkm_notify_init(NULL, &i2c->event, nvkm_dp_hpd, true,
			       &(struct nvkm_i2c_ntfy_req) {
				.mask = NVKM_I2C_PLUG | NVKM_I2C_UNPLUG |
					NVKM_I2C_IRQ,
				.port = outp->dp.aux->id,
			       },
			       sizeof(struct nvkm_i2c_ntfy_req),
			       sizeof(struct nvkm_i2c_ntfy_rep),
			       &outp->dp.hpd);
	if (ret) {
		OUTP_ERR(outp, "error monitoring aux hpd: %d", ret);
		return ret;
	}

	mutex_init(&outp->dp.mutex);
	atomic_set(&outp->dp.lt.done, 0);
	return 0;
}
