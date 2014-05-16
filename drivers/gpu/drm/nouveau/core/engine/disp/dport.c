/*
 * Copyright 2013 Red Hat Inc.
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

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/dp.h>
#include <subdev/bios/init.h>
#include <subdev/i2c.h>

#include <engine/disp.h>

#include <core/class.h>

#include "dport.h"

#define DBG(fmt, args...) nv_debug(dp->disp, "DP:%04x:%04x: " fmt,             \
				   dp->outp->hasht, dp->outp->hashm, ##args)
#define ERR(fmt, args...) nv_error(dp->disp, "DP:%04x:%04x: " fmt,             \
				   dp->outp->hasht, dp->outp->hashm, ##args)

/******************************************************************************
 * link training
 *****************************************************************************/
struct dp_state {
	const struct nouveau_dp_func *func;
	struct nouveau_disp *disp;
	struct dcb_output *outp;
	struct nvbios_dpout info;
	u8 version;
	struct nouveau_i2c_port *aux;
	int head;
	u8  dpcd[16];
	int link_nr;
	u32 link_bw;
	u8  stat[6];
	u8  conf[4];
	bool pc2;
	u8  pc2stat;
	u8  pc2conf[2];
};

static int
dp_set_link_config(struct dp_state *dp)
{
	struct nouveau_disp *disp = dp->disp;
	struct nouveau_bios *bios = nouveau_bios(disp);
	struct nvbios_init init = {
		.subdev = nv_subdev(dp->disp),
		.bios = bios,
		.offset = 0x0000,
		.outp = dp->outp,
		.crtc = dp->head,
		.execute = 1,
	};
	u32 lnkcmp;
	u8 sink[2];
	int ret;

	DBG("%d lanes at %d KB/s\n", dp->link_nr, dp->link_bw);

	/* set desired link configuration on the source */
	if ((lnkcmp = dp->info.lnkcmp)) {
		if (dp->version < 0x30) {
			while ((dp->link_bw / 10) < nv_ro16(bios, lnkcmp))
				lnkcmp += 4;
			init.offset = nv_ro16(bios, lnkcmp + 2);
		} else {
			while ((dp->link_bw / 27000) < nv_ro08(bios, lnkcmp))
				lnkcmp += 3;
			init.offset = nv_ro16(bios, lnkcmp + 1);
		}

		nvbios_exec(&init);
	}

	ret = dp->func->lnk_ctl(dp->disp, dp->outp, dp->head,
				dp->link_nr, dp->link_bw / 27000,
				dp->dpcd[DPCD_RC02] &
					 DPCD_RC02_ENHANCED_FRAME_CAP);
	if (ret) {
		ERR("lnk_ctl failed with %d\n", ret);
		return ret;
	}

	/* set desired link configuration on the sink */
	sink[0] = dp->link_bw / 27000;
	sink[1] = dp->link_nr;
	if (dp->dpcd[DPCD_RC02] & DPCD_RC02_ENHANCED_FRAME_CAP)
		sink[1] |= DPCD_LC01_ENHANCED_FRAME_EN;

	return nv_wraux(dp->aux, DPCD_LC00, sink, 2);
}

static void
dp_set_training_pattern(struct dp_state *dp, u8 pattern)
{
	u8 sink_tp;

	DBG("training pattern %d\n", pattern);
	dp->func->pattern(dp->disp, dp->outp, dp->head, pattern);

	nv_rdaux(dp->aux, DPCD_LC02, &sink_tp, 1);
	sink_tp &= ~DPCD_LC02_TRAINING_PATTERN_SET;
	sink_tp |= pattern;
	nv_wraux(dp->aux, DPCD_LC02, &sink_tp, 1);
}

static int
dp_link_train_commit(struct dp_state *dp, bool pc)
{
	const struct nouveau_dp_func *func = dp->func;
	struct nouveau_disp *disp = dp->disp;
	int ret, i;

	for (i = 0; i < dp->link_nr; i++) {
		u8 lane = (dp->stat[4 + (i >> 1)] >> ((i & 1) * 4)) & 0xf;
		u8 lpre = (lane & 0x0c) >> 2;
		u8 lvsw = (lane & 0x03) >> 0;

		dp->conf[i] = (lpre << 3) | lvsw;
		if (lvsw == 3)
			dp->conf[i] |= DPCD_LC03_MAX_SWING_REACHED;
		if (lpre == 3)
			dp->conf[i] |= DPCD_LC03_MAX_PRE_EMPHASIS_REACHED;
		dp->pc2conf[i >> 1] |= 4 << ((i & 1) * 4);

		DBG("config lane %d %02x\n", i, dp->conf[i]);
		func->drv_ctl(disp, dp->outp, dp->head, i, lvsw, lpre);
	}

	ret = nv_wraux(dp->aux, DPCD_LC03(0), dp->conf, 4);
	if (ret)
		return ret;

	if (pc) {
		ret = nv_wraux(dp->aux, DPCD_LC0F, dp->pc2conf, 2);
		if (ret)
			return ret;
	}

	return 0;
}

static int
dp_link_train_update(struct dp_state *dp, bool pc, u32 delay)
{
	int ret;

	if (dp->dpcd[DPCD_RC0E_AUX_RD_INTERVAL])
		mdelay(dp->dpcd[DPCD_RC0E_AUX_RD_INTERVAL] * 4);
	else
		udelay(delay);

	ret = nv_rdaux(dp->aux, DPCD_LS02, dp->stat, 6);
	if (ret)
		return ret;

	if (pc) {
		ret = nv_rdaux(dp->aux, DPCD_LS0C, &dp->pc2stat, 1);
		if (ret)
			dp->pc2stat = 0x00;
		DBG("status %6ph pc2 %02x\n", dp->stat, dp->pc2stat);
	} else {
		DBG("status %6ph\n", dp->stat);
	}

	return 0;
}

static int
dp_link_train_cr(struct dp_state *dp)
{
	bool cr_done = false, abort = false;
	int voltage = dp->conf[0] & DPCD_LC03_VOLTAGE_SWING_SET;
	int tries = 0, i;

	dp_set_training_pattern(dp, 1);

	do {
		if (dp_link_train_commit(dp, false) ||
		    dp_link_train_update(dp, false, 100))
			break;

		cr_done = true;
		for (i = 0; i < dp->link_nr; i++) {
			u8 lane = (dp->stat[i >> 1] >> ((i & 1) * 4)) & 0xf;
			if (!(lane & DPCD_LS02_LANE0_CR_DONE)) {
				cr_done = false;
				if (dp->conf[i] & DPCD_LC03_MAX_SWING_REACHED)
					abort = true;
				break;
			}
		}

		if ((dp->conf[0] & DPCD_LC03_VOLTAGE_SWING_SET) != voltage) {
			voltage = dp->conf[0] & DPCD_LC03_VOLTAGE_SWING_SET;
			tries = 0;
		}
	} while (!cr_done && !abort && ++tries < 5);

	return cr_done ? 0 : -1;
}

static int
dp_link_train_eq(struct dp_state *dp)
{
	bool eq_done = false, cr_done = true;
	int tries = 0, i;

	if (dp->dpcd[2] & DPCD_RC02_TPS3_SUPPORTED)
		dp_set_training_pattern(dp, 3);
	else
		dp_set_training_pattern(dp, 2);

	do {
		if (dp_link_train_update(dp, dp->pc2, 400))
			break;

		eq_done = !!(dp->stat[2] & DPCD_LS04_INTERLANE_ALIGN_DONE);
		for (i = 0; i < dp->link_nr && eq_done; i++) {
			u8 lane = (dp->stat[i >> 1] >> ((i & 1) * 4)) & 0xf;
			if (!(lane & DPCD_LS02_LANE0_CR_DONE))
				cr_done = false;
			if (!(lane & DPCD_LS02_LANE0_CHANNEL_EQ_DONE) ||
			    !(lane & DPCD_LS02_LANE0_SYMBOL_LOCKED))
				eq_done = false;
		}

		if (dp_link_train_commit(dp, dp->pc2))
			break;
	} while (!eq_done && cr_done && ++tries <= 5);

	return eq_done ? 0 : -1;
}

static void
dp_link_train_init(struct dp_state *dp, bool spread)
{
	struct nvbios_init init = {
		.subdev = nv_subdev(dp->disp),
		.bios = nouveau_bios(dp->disp),
		.outp = dp->outp,
		.crtc = dp->head,
		.execute = 1,
	};

	/* set desired spread */
	if (spread)
		init.offset = dp->info.script[2];
	else
		init.offset = dp->info.script[3];
	nvbios_exec(&init);

	/* pre-train script */
	init.offset = dp->info.script[0];
	nvbios_exec(&init);
}

static void
dp_link_train_fini(struct dp_state *dp)
{
	struct nvbios_init init = {
		.subdev = nv_subdev(dp->disp),
		.bios = nouveau_bios(dp->disp),
		.outp = dp->outp,
		.crtc = dp->head,
		.execute = 1,
	};

	/* post-train script */
	init.offset = dp->info.script[1],
	nvbios_exec(&init);
}

static const struct dp_rates {
	u32 rate;
	u8  bw;
	u8  nr;
} nouveau_dp_rates[] = {
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

int
nouveau_dp_train(struct nouveau_disp *disp, const struct nouveau_dp_func *func,
		 struct dcb_output *outp, int head, u32 datarate)
{
	struct nouveau_bios *bios = nouveau_bios(disp);
	struct nouveau_i2c *i2c = nouveau_i2c(disp);
	const struct dp_rates *cfg = nouveau_dp_rates;
	struct dp_state _dp = {
		.disp = disp,
		.func = func,
		.outp = outp,
		.head = head,
	}, *dp = &_dp;
	u8  hdr, cnt, len;
	u32 data;
	int ret;

	/* find the bios displayport data relevant to this output */
	data = nvbios_dpout_match(bios, outp->hasht, outp->hashm, &dp->version,
				 &hdr, &cnt, &len, &dp->info);
	if (!data) {
		ERR("bios data not found\n");
		return -EINVAL;
	}

	/* acquire the aux channel and fetch some info about the display */
	if (outp->location)
		dp->aux = i2c->find_type(i2c, NV_I2C_TYPE_EXTAUX(outp->extdev));
	else
		dp->aux = i2c->find(i2c, NV_I2C_TYPE_DCBI2C(outp->i2c_index));
	if (!dp->aux) {
		ERR("no aux channel?!\n");
		return -ENODEV;
	}

	ret = nv_rdaux(dp->aux, 0x00000, dp->dpcd, sizeof(dp->dpcd));
	if (ret) {
		/* it's possible the display has been unplugged before we
		 * get here.  we still need to execute the full set of
		 * vbios scripts, and program the OR at a high enough
		 * frequency to satisfy the target mode.  failure to do
		 * so results at best in an UPDATE hanging, and at worst
		 * with PDISP running away to join the circus.
		 */
		dp->dpcd[1] = dp->outp->dpconf.link_bw;
		dp->dpcd[2] = dp->outp->dpconf.link_nr;
		dp->dpcd[3] = 0x00;
		ERR("failed to read DPCD\n");
	}

	/* bring capabilities within encoder limits */
	if (nv_mclass(disp) < NVD0_DISP_CLASS)
		dp->dpcd[2] &= ~DPCD_RC02_TPS3_SUPPORTED;
	if ((dp->dpcd[2] & 0x1f) > dp->outp->dpconf.link_nr) {
		dp->dpcd[2] &= ~DPCD_RC02_MAX_LANE_COUNT;
		dp->dpcd[2] |= dp->outp->dpconf.link_nr;
	}
	if (dp->dpcd[1] > dp->outp->dpconf.link_bw)
		dp->dpcd[1] = dp->outp->dpconf.link_bw;
	dp->pc2 = dp->dpcd[2] & DPCD_RC02_TPS3_SUPPORTED;

	/* restrict link config to the lowest required rate, if requested */
	if (datarate) {
		datarate = (datarate / 8) * 10; /* 8B/10B coding overhead */
		while (cfg[1].rate >= datarate)
			cfg++;
	}
	cfg--;

	/* enable down-spreading and execute pre-train script from vbios */
	dp_link_train_init(dp, dp->dpcd[3] & 0x01);

	while (ret = -EIO, (++cfg)->rate) {
		/* select next configuration supported by encoder and sink */
		while (cfg->nr > (dp->dpcd[2] & DPCD_RC02_MAX_LANE_COUNT) ||
		       cfg->bw > (dp->dpcd[DPCD_RC01_MAX_LINK_RATE]))
			cfg++;
		dp->link_bw = cfg->bw * 27000;
		dp->link_nr = cfg->nr;

		/* program selected link configuration */
		ret = dp_set_link_config(dp);
		if (ret == 0) {
			/* attempt to train the link at this configuration */
			memset(dp->stat, 0x00, sizeof(dp->stat));
			if (!dp_link_train_cr(dp) &&
			    !dp_link_train_eq(dp))
				break;
		} else
		if (ret) {
			/* dp_set_link_config() handled training, or
			 * we failed to communicate with the sink.
			 */
			break;
		}
	}

	/* finish link training */
	dp_set_training_pattern(dp, 0);
	if (ret < 0)
		ERR("link training failed\n");

	/* execute post-train script from vbios */
	dp_link_train_fini(dp);
	return (ret < 0) ? false : true;
}
