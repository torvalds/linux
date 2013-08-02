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
	u8  dpcd[4];
	int link_nr;
	u32 link_bw;
	u8  stat[6];
	u8  conf[4];
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

	DBG("%d lanes at %d KB/s\n", dp->link_nr, dp->link_bw);

	/* set desired link configuration on the sink */
	sink[0] = dp->link_bw / 27000;
	sink[1] = dp->link_nr;
	if (dp->dpcd[DPCD_RC02] & DPCD_RC02_ENHANCED_FRAME_CAP)
		sink[1] |= DPCD_LC01_ENHANCED_FRAME_EN;

	nv_wraux(dp->aux, DPCD_LC00, sink, 2);

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

	return dp->func->lnk_ctl(dp->disp, dp->outp, dp->head,
				 dp->link_nr, dp->link_bw / 27000,
				 dp->dpcd[DPCD_RC02] &
					  DPCD_RC02_ENHANCED_FRAME_CAP);
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
dp_link_train_commit(struct dp_state *dp)
{
	int i;

	for (i = 0; i < dp->link_nr; i++) {
		u8 lane = (dp->stat[4 + (i >> 1)] >> ((i & 1) * 4)) & 0xf;
		u8 lpre = (lane & 0x0c) >> 2;
		u8 lvsw = (lane & 0x03) >> 0;

		dp->conf[i] = (lpre << 3) | lvsw;
		if (lvsw == 3)
			dp->conf[i] |= DPCD_LC03_MAX_SWING_REACHED;
		if (lpre == 3)
			dp->conf[i] |= DPCD_LC03_MAX_PRE_EMPHASIS_REACHED;

		DBG("config lane %d %02x\n", i, dp->conf[i]);
		dp->func->drv_ctl(dp->disp, dp->outp, dp->head, i, lvsw, lpre);
	}

	return nv_wraux(dp->aux, DPCD_LC03(0), dp->conf, 4);
}

static int
dp_link_train_update(struct dp_state *dp, u32 delay)
{
	int ret;

	udelay(delay);

	ret = nv_rdaux(dp->aux, DPCD_LS02, dp->stat, 6);
	if (ret)
		return ret;

	DBG("status %6ph\n", dp->stat);
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
		if (dp_link_train_commit(dp) ||
		    dp_link_train_update(dp, 100))
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

	dp_set_training_pattern(dp, 2);

	do {
		if (dp_link_train_update(dp, 400))
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

		if (dp_link_train_commit(dp))
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

int
nouveau_dp_train(struct nouveau_disp *disp, const struct nouveau_dp_func *func,
		 struct dcb_output *outp, int head, u32 datarate)
{
	struct nouveau_bios *bios = nouveau_bios(disp);
	struct nouveau_i2c *i2c = nouveau_i2c(disp);
	struct dp_state _dp = {
		.disp = disp,
		.func = func,
		.outp = outp,
		.head = head,
	}, *dp = &_dp;
	const u32 bw_list[] = { 270000, 162000, 0 };
	const u32 *link_bw = bw_list;
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
		ERR("failed to read DPCD\n");
		return ret;
	}

	/* adjust required bandwidth for 8B/10B coding overhead */
	datarate = (datarate / 8) * 10;

	/* enable down-spreading and execute pre-train script from vbios */
	dp_link_train_init(dp, dp->dpcd[3] & 0x01);

	/* start off at highest link rate supported by encoder and display */
	while (*link_bw > (dp->dpcd[1] * 27000))
		link_bw++;

	while (link_bw[0]) {
		/* find minimum required lane count at this link rate */
		dp->link_nr = dp->dpcd[2] & DPCD_RC02_MAX_LANE_COUNT;
		while ((dp->link_nr >> 1) * link_bw[0] > datarate)
			dp->link_nr >>= 1;

		/* drop link rate to minimum with this lane count */
		while ((link_bw[1] * dp->link_nr) > datarate)
			link_bw++;
		dp->link_bw = link_bw[0];

		/* program selected link configuration */
		ret = dp_set_link_config(dp);
		if (ret == 0) {
			/* attempt to train the link at this configuration */
			memset(dp->stat, 0x00, sizeof(dp->stat));
			if (!dp_link_train_cr(dp) &&
			    !dp_link_train_eq(dp))
				break;
		} else
		if (ret >= 1) {
			/* dp_set_link_config() handled training */
			break;
		}

		/* retry at lower rate */
		link_bw++;
	}

	/* finish link training */
	dp_set_training_pattern(dp, 0);

	/* execute post-train script from vbios */
	dp_link_train_fini(dp);
	return true;
}
