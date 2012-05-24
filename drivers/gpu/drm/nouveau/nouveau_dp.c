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

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_i2c.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"
#include "nouveau_gpio.h"

/******************************************************************************
 * aux channel util functions
 *****************************************************************************/
#define AUX_DBG(fmt, args...) do {                                             \
	if (nouveau_reg_debug & NOUVEAU_REG_DEBUG_AUXCH) {                     \
		NV_PRINTK(KERN_DEBUG, dev, "AUXCH(%d): " fmt, ch, ##args);     \
	}                                                                      \
} while (0)
#define AUX_ERR(fmt, args...) NV_ERROR(dev, "AUXCH(%d): " fmt, ch, ##args)

static void
auxch_fini(struct drm_device *dev, int ch)
{
	nv_mask(dev, 0x00e4e4 + (ch * 0x50), 0x00310000, 0x00000000);
}

static int
auxch_init(struct drm_device *dev, int ch)
{
	const u32 unksel = 1; /* nfi which to use, or if it matters.. */
	const u32 ureq = unksel ? 0x00100000 : 0x00200000;
	const u32 urep = unksel ? 0x01000000 : 0x02000000;
	u32 ctrl, timeout;

	/* wait up to 1ms for any previous transaction to be done... */
	timeout = 1000;
	do {
		ctrl = nv_rd32(dev, 0x00e4e4 + (ch * 0x50));
		udelay(1);
		if (!timeout--) {
			AUX_ERR("begin idle timeout 0x%08x", ctrl);
			return -EBUSY;
		}
	} while (ctrl & 0x03010000);

	/* set some magic, and wait up to 1ms for it to appear */
	nv_mask(dev, 0x00e4e4 + (ch * 0x50), 0x00300000, ureq);
	timeout = 1000;
	do {
		ctrl = nv_rd32(dev, 0x00e4e4 + (ch * 0x50));
		udelay(1);
		if (!timeout--) {
			AUX_ERR("magic wait 0x%08x\n", ctrl);
			auxch_fini(dev, ch);
			return -EBUSY;
		}
	} while ((ctrl & 0x03000000) != urep);

	return 0;
}

static int
auxch_tx(struct drm_device *dev, int ch, u8 type, u32 addr, u8 *data, u8 size)
{
	u32 ctrl, stat, timeout, retries;
	u32 xbuf[4] = {};
	int ret, i;

	AUX_DBG("%d: 0x%08x %d\n", type, addr, size);

	ret = auxch_init(dev, ch);
	if (ret)
		goto out;

	stat = nv_rd32(dev, 0x00e4e8 + (ch * 0x50));
	if (!(stat & 0x10000000)) {
		AUX_DBG("sink not detected\n");
		ret = -ENXIO;
		goto out;
	}

	if (!(type & 1)) {
		memcpy(xbuf, data, size);
		for (i = 0; i < 16; i += 4) {
			AUX_DBG("wr 0x%08x\n", xbuf[i / 4]);
			nv_wr32(dev, 0x00e4c0 + (ch * 0x50) + i, xbuf[i / 4]);
		}
	}

	ctrl  = nv_rd32(dev, 0x00e4e4 + (ch * 0x50));
	ctrl &= ~0x0001f0ff;
	ctrl |= type << 12;
	ctrl |= size - 1;
	nv_wr32(dev, 0x00e4e0 + (ch * 0x50), addr);

	/* retry transaction a number of times on failure... */
	ret = -EREMOTEIO;
	for (retries = 0; retries < 32; retries++) {
		/* reset, and delay a while if this is a retry */
		nv_wr32(dev, 0x00e4e4 + (ch * 0x50), 0x80000000 | ctrl);
		nv_wr32(dev, 0x00e4e4 + (ch * 0x50), 0x00000000 | ctrl);
		if (retries)
			udelay(400);

		/* transaction request, wait up to 1ms for it to complete */
		nv_wr32(dev, 0x00e4e4 + (ch * 0x50), 0x00010000 | ctrl);

		timeout = 1000;
		do {
			ctrl = nv_rd32(dev, 0x00e4e4 + (ch * 0x50));
			udelay(1);
			if (!timeout--) {
				AUX_ERR("tx req timeout 0x%08x\n", ctrl);
				goto out;
			}
		} while (ctrl & 0x00010000);

		/* read status, and check if transaction completed ok */
		stat = nv_mask(dev, 0x00e4e8 + (ch * 0x50), 0, 0);
		if (!(stat & 0x000f0f00)) {
			ret = 0;
			break;
		}

		AUX_DBG("%02d 0x%08x 0x%08x\n", retries, ctrl, stat);
	}

	if (type & 1) {
		for (i = 0; i < 16; i += 4) {
			xbuf[i / 4] = nv_rd32(dev, 0x00e4d0 + (ch * 0x50) + i);
			AUX_DBG("rd 0x%08x\n", xbuf[i / 4]);
		}
		memcpy(data, xbuf, size);
	}

out:
	auxch_fini(dev, ch);
	return ret;
}

u8 *
nouveau_dp_bios_data(struct drm_device *dev, struct dcb_entry *dcb, u8 **entry)
{
	struct bit_entry d;
	u8 *table;
	int i;

	if (bit_table(dev, 'd', &d)) {
		NV_ERROR(dev, "BIT 'd' table not found\n");
		return NULL;
	}

	if (d.version != 1) {
		NV_ERROR(dev, "BIT 'd' table version %d unknown\n", d.version);
		return NULL;
	}

	table = ROMPTR(dev, d.data[0]);
	if (!table) {
		NV_ERROR(dev, "displayport table pointer invalid\n");
		return NULL;
	}

	switch (table[0]) {
	case 0x20:
	case 0x21:
	case 0x30:
	case 0x40:
		break;
	default:
		NV_ERROR(dev, "displayport table 0x%02x unknown\n", table[0]);
		return NULL;
	}

	for (i = 0; i < table[3]; i++) {
		*entry = ROMPTR(dev, table[table[1] + (i * table[2])]);
		if (*entry && bios_encoder_match(dcb, ROM32((*entry)[0])))
			return table;
	}

	NV_ERROR(dev, "displayport encoder table not found\n");
	return NULL;
}

/******************************************************************************
 * link training
 *****************************************************************************/
struct dp_state {
	struct dp_train_func *func;
	struct dcb_entry *dcb;
	int auxch;
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
	u8 sink[2];

	NV_DEBUG_KMS(dev, "%d lanes at %d KB/s\n", dp->link_nr, dp->link_bw);

	/* set desired link configuration on the source */
	dp->func->link_set(dev, dp->dcb, dp->crtc, dp->link_nr, dp->link_bw,
			   dp->dpcd[2] & DP_ENHANCED_FRAME_CAP);

	/* inform the sink of the new configuration */
	sink[0] = dp->link_bw / 27000;
	sink[1] = dp->link_nr;
	if (dp->dpcd[2] & DP_ENHANCED_FRAME_CAP)
		sink[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	auxch_tx(dev, dp->auxch, 8, DP_LINK_BW_SET, sink, 2);
}

static void
dp_set_training_pattern(struct drm_device *dev, struct dp_state *dp, u8 pattern)
{
	u8 sink_tp;

	NV_DEBUG_KMS(dev, "training pattern %d\n", pattern);

	dp->func->train_set(dev, dp->dcb, pattern);

	auxch_tx(dev, dp->auxch, 9, DP_TRAINING_PATTERN_SET, &sink_tp, 1);
	sink_tp &= ~DP_TRAINING_PATTERN_MASK;
	sink_tp |= pattern;
	auxch_tx(dev, dp->auxch, 8, DP_TRAINING_PATTERN_SET, &sink_tp, 1);
}

static int
dp_link_train_commit(struct drm_device *dev, struct dp_state *dp)
{
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

		NV_DEBUG_KMS(dev, "config lane %d %02x\n", i, dp->conf[i]);
		dp->func->train_adj(dev, dp->dcb, i, lvsw, lpre);
	}

	return auxch_tx(dev, dp->auxch, 8, DP_TRAINING_LANE0_SET, dp->conf, 4);
}

static int
dp_link_train_update(struct drm_device *dev, struct dp_state *dp, u32 delay)
{
	int ret;

	udelay(delay);

	ret = auxch_tx(dev, dp->auxch, 9, DP_LANE0_1_STATUS, dp->stat, 6);
	if (ret)
		return ret;

	NV_DEBUG_KMS(dev, "status %02x %02x %02x %02x %02x %02x\n",
		     dp->stat[0], dp->stat[1], dp->stat[2], dp->stat[3],
		     dp->stat[4], dp->stat[5]);
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
dp_set_downspread(struct drm_device *dev, struct dp_state *dp, bool enable)
{
	u16 script = 0x0000;
	u8 *entry, *table = nouveau_dp_bios_data(dev, dp->dcb, &entry);
	if (table) {
		if (table[0] >= 0x20 && table[0] <= 0x30) {
			if (enable) script = ROM16(entry[12]);
			else        script = ROM16(entry[14]);
		} else
		if (table[0] == 0x40) {
			if (enable) script = ROM16(entry[11]);
			else        script = ROM16(entry[13]);
		}
	}

	nouveau_bios_run_init_table(dev, script, dp->dcb, dp->crtc);
}

static void
dp_link_train_init(struct drm_device *dev, struct dp_state *dp)
{
	u16 script = 0x0000;
	u8 *entry, *table = nouveau_dp_bios_data(dev, dp->dcb, &entry);
	if (table) {
		if (table[0] >= 0x20 && table[0] <= 0x30)
			script = ROM16(entry[6]);
		else
		if (table[0] == 0x40)
			script = ROM16(entry[5]);
	}

	nouveau_bios_run_init_table(dev, script, dp->dcb, dp->crtc);
}

static void
dp_link_train_fini(struct drm_device *dev, struct dp_state *dp)
{
	u16 script = 0x0000;
	u8 *entry, *table = nouveau_dp_bios_data(dev, dp->dcb, &entry);
	if (table) {
		if (table[0] >= 0x20 && table[0] <= 0x30)
			script = ROM16(entry[8]);
		else
		if (table[0] == 0x40)
			script = ROM16(entry[7]);
	}

	nouveau_bios_run_init_table(dev, script, dp->dcb, dp->crtc);
}

bool
nouveau_dp_link_train(struct drm_encoder *encoder, u32 datarate,
		      struct dp_train_func *func)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector =
		nouveau_encoder_connector_get(nv_encoder);
	struct drm_device *dev = encoder->dev;
	struct nouveau_i2c_chan *auxch;
	const u32 bw_list[] = { 270000, 162000, 0 };
	const u32 *link_bw = bw_list;
	struct dp_state dp;

	auxch = nouveau_i2c_find(dev, nv_encoder->dcb->i2c_index);
	if (!auxch)
		return false;

	dp.func = func;
	dp.dcb = nv_encoder->dcb;
	dp.crtc = nv_crtc->index;
	dp.auxch = auxch->drive;
	dp.dpcd = nv_encoder->dp.dpcd;

	/* adjust required bandwidth for 8B/10B coding overhead */
	datarate = (datarate / 8) * 10;

	/* some sinks toggle hotplug in response to some of the actions
	 * we take during link training (DP_SET_POWER is one), we need
	 * to ignore them for the moment to avoid races.
	 */
	nouveau_gpio_irq(dev, 0, nv_connector->hpd, 0xff, false);

	/* enable down-spreading, if possible */
	dp_set_downspread(dev, &dp, nv_encoder->dp.dpcd[3] & 1);

	/* execute pre-train script from vbios */
	dp_link_train_init(dev, &dp);

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
	nouveau_gpio_irq(dev, 0, nv_connector->hpd, 0xff, true);
	return true;
}

void
nouveau_dp_dpms(struct drm_encoder *encoder, int mode, u32 datarate,
		struct dp_train_func *func)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_i2c_chan *auxch;
	u8 status;

	auxch = nouveau_i2c_find(encoder->dev, nv_encoder->dcb->i2c_index);
	if (!auxch)
		return;

	if (mode == DRM_MODE_DPMS_ON)
		status = DP_SET_POWER_D0;
	else
		status = DP_SET_POWER_D3;

	nouveau_dp_auxch(auxch, 8, DP_SET_POWER, &status, 1);

	if (mode == DRM_MODE_DPMS_ON)
		nouveau_dp_link_train(encoder, datarate, func);
}

bool
nouveau_dp_detect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct nouveau_i2c_chan *auxch;
	u8 *dpcd = nv_encoder->dp.dpcd;
	int ret;

	auxch = nouveau_i2c_find(dev, nv_encoder->dcb->i2c_index);
	if (!auxch)
		return false;

	ret = auxch_tx(dev, auxch->drive, 9, DP_DPCD_REV, dpcd, 8);
	if (ret)
		return false;

	nv_encoder->dp.link_bw = 27000 * dpcd[1];
	nv_encoder->dp.link_nr = dpcd[2] & DP_MAX_LANE_COUNT_MASK;

	NV_DEBUG_KMS(dev, "display: %dx%d dpcd 0x%02x\n",
		     nv_encoder->dp.link_nr, nv_encoder->dp.link_bw, dpcd[0]);
	NV_DEBUG_KMS(dev, "encoder: %dx%d\n",
		     nv_encoder->dcb->dpconf.link_nr,
		     nv_encoder->dcb->dpconf.link_bw);

	if (nv_encoder->dcb->dpconf.link_nr < nv_encoder->dp.link_nr)
		nv_encoder->dp.link_nr = nv_encoder->dcb->dpconf.link_nr;
	if (nv_encoder->dcb->dpconf.link_bw < nv_encoder->dp.link_bw)
		nv_encoder->dp.link_bw = nv_encoder->dcb->dpconf.link_bw;

	NV_DEBUG_KMS(dev, "maximum: %dx%d\n",
		     nv_encoder->dp.link_nr, nv_encoder->dp.link_bw);

	return true;
}

int
nouveau_dp_auxch(struct nouveau_i2c_chan *auxch, int cmd, int addr,
		 uint8_t *data, int data_nr)
{
	return auxch_tx(auxch->dev, auxch->drive, cmd, addr, data, data_nr);
}

static int
nouveau_dp_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct nouveau_i2c_chan *auxch = (struct nouveau_i2c_chan *)adap;
	struct i2c_msg *msg = msgs;
	int ret, mcnt = num;

	while (mcnt--) {
		u8 remaining = msg->len;
		u8 *ptr = msg->buf;

		while (remaining) {
			u8 cnt = (remaining > 16) ? 16 : remaining;
			u8 cmd;

			if (msg->flags & I2C_M_RD)
				cmd = AUX_I2C_READ;
			else
				cmd = AUX_I2C_WRITE;

			if (mcnt || remaining > 16)
				cmd |= AUX_I2C_MOT;

			ret = nouveau_dp_auxch(auxch, cmd, msg->addr, ptr, cnt);
			if (ret < 0)
				return ret;

			ptr += cnt;
			remaining -= cnt;
		}

		msg++;
	}

	return num;
}

static u32
nouveau_dp_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

const struct i2c_algorithm nouveau_dp_i2c_algo = {
	.master_xfer = nouveau_dp_i2c_xfer,
	.functionality = nouveau_dp_i2c_func
};
