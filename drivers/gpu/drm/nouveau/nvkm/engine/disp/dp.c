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

#include <drm/display/drm_dp.h>

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

static int
nvkm_dp_mst_id_put(struct nvkm_outp *outp, u32 id)
{
	return 0;
}

static int
nvkm_dp_mst_id_get(struct nvkm_outp *outp, u32 *pid)
{
	*pid = BIT(outp->index);
	return 0;
}

static int
nvkm_dp_aux_xfer(struct nvkm_outp *outp, u8 type, u32 addr, u8 *data, u8 *size)
{
	int ret = nvkm_i2c_aux_acquire(outp->dp.aux);

	if (ret)
		return ret;

	ret = nvkm_i2c_aux_xfer(outp->dp.aux, false, type, addr, data, size);
	nvkm_i2c_aux_release(outp->dp.aux);
	return ret;
}

static int
nvkm_dp_aux_pwr(struct nvkm_outp *outp, bool pu)
{
	outp->dp.enabled = pu;
	nvkm_dp_enable(outp, outp->dp.enabled);
	return 0;
}

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
nvkm_dp_train_link(struct nvkm_outp *outp, int rate)
{
	struct nvkm_ior *ior = outp->ior;
	struct lt_state lt = {
		.outp = outp,
		.pc2 = outp->dp.dpcd[DPCD_RC02] & DPCD_RC02_TPS3_SUPPORTED,
		.repeaters = outp->dp.lttprs,
	};
	u8 sink[2];
	int ret;

	OUTP_DBG(outp, "training %dx%02x", ior->dp.nr, ior->dp.bw);

	/* Set desired link configuration on the sink. */
	sink[0] = (outp->dp.rate[rate].dpcd < 0) ? ior->dp.bw : 0;
	sink[1] = ior->dp.nr;
	if (ior->dp.ef)
		sink[1] |= DPCD_LC01_ENHANCED_FRAME_EN;
	if (outp->dp.lt.post_adj)
		sink[1] |= 0x20;

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

static int
nvkm_dp_train_links(struct nvkm_outp *outp, int rate)
{
	struct nvkm_ior *ior = outp->ior;
	struct nvkm_disp *disp = outp->disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	u32 lnkcmp;
	int ret;

	OUTP_DBG(outp, "programming link for %dx%02x", ior->dp.nr, ior->dp.bw);

	/* Intersect misc. capabilities of the OR and sink. */
	if (disp->engine.subdev.device->chipset < 0x110)
		outp->dp.dpcd[DPCD_RC03] &= ~DPCD_RC03_TPS4_SUPPORTED;
	if (disp->engine.subdev.device->chipset < 0xd0)
		outp->dp.dpcd[DPCD_RC02] &= ~DPCD_RC02_TPS3_SUPPORTED;

	if (AMPERE_IED_HACK(disp) && (lnkcmp = outp->dp.info.script[0])) {
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
	if ((lnkcmp = outp->dp.info.lnkcmp)) {
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

	/* Attempt to train the link in this configuration. */
	return nvkm_dp_train_link(outp, rate);
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
nvkm_dp_drive(struct nvkm_outp *outp, u8 lanes, u8 pe[4], u8 vs[4])
{
	struct lt_state lt = {
		.outp = outp,
		.stat[4] = (pe[0] << 2) | (vs[0] << 0) |
			   (pe[1] << 6) | (vs[1] << 4),
		.stat[5] = (pe[2] << 2) | (vs[2] << 0) |
			   (pe[3] << 6) | (vs[3] << 4),
	};

	return nvkm_dp_train_drive(&lt, false);
}

static int
nvkm_dp_train(struct nvkm_outp *outp, bool retrain)
{
	struct nvkm_ior *ior = outp->ior;
	int ret, rate;

	for (rate = 0; rate < outp->dp.rates; rate++) {
		if (outp->dp.rate[rate].rate == (retrain ? ior->dp.bw : outp->dp.lt.bw) * 27000)
			break;
	}

	if (WARN_ON(rate == outp->dp.rates))
		return -EINVAL;

	/* Retraining link?  Skip source configuration, it can mess up the active modeset. */
	if (retrain) {
		mutex_lock(&outp->dp.mutex);
		ret = nvkm_dp_train_link(outp, rate);
		mutex_unlock(&outp->dp.mutex);
		return ret;
	}

	mutex_lock(&outp->dp.mutex);
	OUTP_DBG(outp, "training");

	ior->dp.mst = outp->dp.lt.mst;
	ior->dp.ef = outp->dp.dpcd[DPCD_RC02] & DPCD_RC02_ENHANCED_FRAME_CAP;
	ior->dp.bw = outp->dp.lt.bw;
	ior->dp.nr = outp->dp.lt.nr;

	nvkm_dp_train_init(outp);
	ret = nvkm_dp_train_links(outp, rate);
	nvkm_dp_train_fini(outp);
	if (ret < 0)
		OUTP_ERR(outp, "training failed");
	else
		OUTP_DBG(outp, "training done");

	mutex_unlock(&outp->dp.mutex);
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
	outp->ior->dp.nr = 0;
	nvkm_dp_disable(outp, outp->ior);

	nvkm_outp_release(outp);
}

void
nvkm_dp_enable(struct nvkm_outp *outp, bool auxpwr)
{
	struct nvkm_gpio *gpio = outp->disp->engine.subdev.device->gpio;
	struct nvkm_i2c_aux *aux = outp->dp.aux;

	if (auxpwr && !outp->dp.aux_pwr) {
		/* eDP panels need powering on by us (if the VBIOS doesn't default it
		 * to on) before doing any AUX channel transactions.  LVDS panel power
		 * is handled by the SOR itself, and not required for LVDS DDC.
		 */
		if (outp->conn->info.type == DCB_CONNECTOR_eDP) {
			int power = nvkm_gpio_get(gpio, 0, DCB_GPIO_PANEL_POWER, 0xff);
			if (power == 0) {
				nvkm_gpio_set(gpio, 0, DCB_GPIO_PANEL_POWER, 0xff, 1);
				outp->dp.aux_pwr_pu = true;
			}

			/* We delay here unconditionally, even if already powered,
			 * because some laptop panels having a significant resume
			 * delay before the panel begins responding.
			 *
			 * This is likely a bit of a hack, but no better idea for
			 * handling this at the moment.
			 */
			msleep(300);
		}

		OUTP_DBG(outp, "aux power -> always");
		nvkm_i2c_aux_monitor(aux, true);
		outp->dp.aux_pwr = true;
	} else
	if (!auxpwr && outp->dp.aux_pwr) {
		OUTP_DBG(outp, "aux power -> demand");
		nvkm_i2c_aux_monitor(aux, false);
		outp->dp.aux_pwr = false;

		/* Restore eDP panel GPIO to its prior state if we changed it, as
		 * it could potentially interfere with other outputs.
		 */
		if (outp->conn->info.type == DCB_CONNECTOR_eDP) {
			if (outp->dp.aux_pwr_pu) {
				nvkm_gpio_set(gpio, 0, DCB_GPIO_PANEL_POWER, 0xff, 0);
				outp->dp.aux_pwr_pu = false;
			}
		}
	}
}

static void
nvkm_dp_fini(struct nvkm_outp *outp)
{
	nvkm_dp_enable(outp, false);
}

static void
nvkm_dp_init(struct nvkm_outp *outp)
{
	nvkm_outp_init(outp);
	nvkm_dp_enable(outp, outp->dp.enabled);
}

static void *
nvkm_dp_dtor(struct nvkm_outp *outp)
{
	return outp;
}

static const struct nvkm_outp_func
nvkm_dp_func = {
	.dtor = nvkm_dp_dtor,
	.init = nvkm_dp_init,
	.fini = nvkm_dp_fini,
	.detect = nvkm_outp_detect,
	.inherit = nvkm_outp_inherit,
	.acquire = nvkm_outp_acquire,
	.release = nvkm_dp_release,
	.bl.get = nvkm_outp_bl_get,
	.bl.set = nvkm_outp_bl_set,
	.dp.aux_pwr = nvkm_dp_aux_pwr,
	.dp.aux_xfer = nvkm_dp_aux_xfer,
	.dp.train = nvkm_dp_train,
	.dp.drive = nvkm_dp_drive,
	.dp.mst_id_get = nvkm_dp_mst_id_get,
	.dp.mst_id_put = nvkm_dp_mst_id_put,
};

int
nvkm_dp_new(struct nvkm_disp *disp, int index, struct dcb_output *dcbE, struct nvkm_outp **poutp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	struct nvkm_bios *bios = device->bios;
	struct nvkm_i2c *i2c = device->i2c;
	struct nvkm_outp *outp;
	u8  ver, hdr, cnt, len;
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

	data = nvbios_dp_table(bios, &ver, &hdr, &cnt, &len);
	outp->dp.mst = data && ver >= 0x40 && (nvbios_rd08(bios, data + 0x08) & 0x04);

	mutex_init(&outp->dp.mutex);
	return 0;
}
