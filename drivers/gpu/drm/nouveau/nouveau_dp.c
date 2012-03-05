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

static u32
dp_link_bw_get(struct drm_device *dev, int or, int link)
{
	u32 ctrl = nv_rd32(dev, 0x614300 + (or * 0x800));
	if (!(ctrl & 0x000c0000))
		return 162000;
	return 270000;
}

static int
dp_lane_count_get(struct drm_device *dev, int or, int link)
{
	u32 ctrl = nv_rd32(dev, NV50_SOR_DP_CTRL(or, link));
	switch (ctrl & 0x000f0000) {
	case 0x00010000: return 1;
	case 0x00030000: return 2;
	default:
		return 4;
	}
}

void
nouveau_dp_tu_update(struct drm_device *dev, int or, int link, u32 clk, u32 bpp)
{
	const u32 symbol = 100000;
	int bestTU = 0, bestVTUi = 0, bestVTUf = 0, bestVTUa = 0;
	int TU, VTUi, VTUf, VTUa;
	u64 link_data_rate, link_ratio, unk;
	u32 best_diff = 64 * symbol;
	u32 link_nr, link_bw, r;

	/* calculate packed data rate for each lane */
	link_nr = dp_lane_count_get(dev, or, link);
	link_data_rate = (clk * bpp / 8) / link_nr;

	/* calculate ratio of packed data rate to link symbol rate */
	link_bw = dp_link_bw_get(dev, or, link);
	link_ratio = link_data_rate * symbol;
	r = do_div(link_ratio, link_bw);

	for (TU = 64; TU >= 32; TU--) {
		/* calculate average number of valid symbols in each TU */
		u32 tu_valid = link_ratio * TU;
		u32 calc, diff;

		/* find a hw representation for the fraction.. */
		VTUi = tu_valid / symbol;
		calc = VTUi * symbol;
		diff = tu_valid - calc;
		if (diff) {
			if (diff >= (symbol / 2)) {
				VTUf = symbol / (symbol - diff);
				if (symbol - (VTUf * diff))
					VTUf++;

				if (VTUf <= 15) {
					VTUa  = 1;
					calc += symbol - (symbol / VTUf);
				} else {
					VTUa  = 0;
					VTUf  = 1;
					calc += symbol;
				}
			} else {
				VTUa  = 0;
				VTUf  = min((int)(symbol / diff), 15);
				calc += symbol / VTUf;
			}

			diff = calc - tu_valid;
		} else {
			/* no remainder, but the hw doesn't like the fractional
			 * part to be zero.  decrement the integer part and
			 * have the fraction add a whole symbol back
			 */
			VTUa = 0;
			VTUf = 1;
			VTUi--;
		}

		if (diff < best_diff) {
			best_diff = diff;
			bestTU = TU;
			bestVTUa = VTUa;
			bestVTUf = VTUf;
			bestVTUi = VTUi;
			if (diff == 0)
				break;
		}
	}

	if (!bestTU) {
		NV_ERROR(dev, "DP: unable to find suitable config\n");
		return;
	}

	/* XXX close to vbios numbers, but not right */
	unk  = (symbol - link_ratio) * bestTU;
	unk *= link_ratio;
	r = do_div(unk, symbol);
	r = do_div(unk, symbol);
	unk += 6;

	nv_mask(dev, NV50_SOR_DP_CTRL(or, link), 0x000001fc, bestTU << 2);
	nv_mask(dev, NV50_SOR_DP_SCFG(or, link), 0x010f7f3f, bestVTUa << 24 |
							     bestVTUf << 16 |
							     bestVTUi << 8 |
							     unk);
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
	struct dcb_entry *dcb;
	u8 *table;
	u8 *entry;
	int auxch;
	int crtc;
	int or;
	int link;
	u8 *dpcd;
	int link_nr;
	u32 link_bw;
	u8  stat[6];
	u8  conf[4];
};

static void
dp_set_link_config(struct drm_device *dev, struct dp_state *dp)
{
	int or = dp->or, link = dp->link;
	u8 *entry, sink[2];
	u32 dp_ctrl;
	u16 script;

	NV_DEBUG_KMS(dev, "%d lanes at %d KB/s\n", dp->link_nr, dp->link_bw);

	/* set selected link rate on source */
	switch (dp->link_bw) {
	case 270000:
		nv_mask(dev, 0x614300 + (or * 0x800), 0x000c0000, 0x00040000);
		sink[0] = DP_LINK_BW_2_7;
		break;
	default:
		nv_mask(dev, 0x614300 + (or * 0x800), 0x000c0000, 0x00000000);
		sink[0] = DP_LINK_BW_1_62;
		break;
	}

	/* offset +0x0a of each dp encoder table entry is a pointer to another
	 * table, that has (among other things) pointers to more scripts that
	 * need to be executed, this time depending on link speed.
	 */
	entry = ROMPTR(dev, dp->entry[10]);
	if (entry) {
		if (dp->table[0] < 0x30) {
			while (dp->link_bw < (ROM16(entry[0]) * 10))
				entry += 4;
			script = ROM16(entry[2]);
		} else {
			while (dp->link_bw < (entry[0] * 27000))
				entry += 3;
			script = ROM16(entry[1]);
		}

		nouveau_bios_run_init_table(dev, script, dp->dcb, dp->crtc);
	}

	/* configure lane count on the source */
	dp_ctrl = ((1 << dp->link_nr) - 1) << 16;
	sink[1] = dp->link_nr;
	if (dp->dpcd[2] & DP_ENHANCED_FRAME_CAP) {
		dp_ctrl |= 0x00004000;
		sink[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	}

	nv_mask(dev, NV50_SOR_DP_CTRL(or, link), 0x001f4000, dp_ctrl);

	/* inform the sink of the new configuration */
	auxch_tx(dev, dp->auxch, 8, DP_LINK_BW_SET, sink, 2);
}

static void
dp_set_training_pattern(struct drm_device *dev, struct dp_state *dp, u8 tp)
{
	u8 sink_tp;

	NV_DEBUG_KMS(dev, "training pattern %d\n", tp);

	nv_mask(dev, NV50_SOR_DP_CTRL(dp->or, dp->link), 0x0f000000, tp << 24);

	auxch_tx(dev, dp->auxch, 9, DP_TRAINING_PATTERN_SET, &sink_tp, 1);
	sink_tp &= ~DP_TRAINING_PATTERN_MASK;
	sink_tp |= tp;
	auxch_tx(dev, dp->auxch, 8, DP_TRAINING_PATTERN_SET, &sink_tp, 1);
}

static const u8 nv50_lane_map[] = { 16, 8, 0, 24 };
static const u8 nvaf_lane_map[] = { 24, 16, 8, 0 };

static int
dp_link_train_commit(struct drm_device *dev, struct dp_state *dp)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 mask = 0, drv = 0, pre = 0, unk = 0;
	const u8 *shifts;
	int link = dp->link;
	int or = dp->or;
	int i;

	if (dev_priv->chipset != 0xaf)
		shifts = nv50_lane_map;
	else
		shifts = nvaf_lane_map;

	for (i = 0; i < dp->link_nr; i++) {
		u8 *conf = dp->entry + dp->table[4];
		u8 lane = (dp->stat[4 + (i >> 1)] >> ((i & 1) * 4)) & 0xf;
		u8 lpre = (lane & 0x0c) >> 2;
		u8 lvsw = (lane & 0x03) >> 0;

		mask |= 0xff << shifts[i];
		unk |= 1 << (shifts[i] >> 3);

		dp->conf[i] = (lpre << 3) | lvsw;
		if (lvsw == DP_TRAIN_VOLTAGE_SWING_1200)
			dp->conf[i] |= DP_TRAIN_MAX_SWING_REACHED;
		if (lpre == DP_TRAIN_PRE_EMPHASIS_9_5)
			dp->conf[i] |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

		NV_DEBUG_KMS(dev, "config lane %d %02x\n", i, dp->conf[i]);

		if (dp->table[0] < 0x30) {
			u8 *last = conf + (dp->entry[4] * dp->table[5]);
			while (lvsw != conf[0] || lpre != conf[1]) {
				conf += dp->table[5];
				if (conf >= last)
					return -EINVAL;
			}

			conf += 2;
		} else {
			/* no lookup table anymore, set entries for each
			 * combination of voltage swing and pre-emphasis
			 * level allowed by the DP spec.
			 */
			switch (lvsw) {
			case 0: lpre += 0; break;
			case 1: lpre += 4; break;
			case 2: lpre += 7; break;
			case 3: lpre += 9; break;
			}

			conf = conf + (lpre * dp->table[5]);
			conf++;
		}

		drv |= conf[0] << shifts[i];
		pre |= conf[1] << shifts[i];
		unk  = (unk & ~0x0000ff00) | (conf[2] << 8);
	}

	nv_mask(dev, NV50_SOR_DP_UNK118(or, link), mask, drv);
	nv_mask(dev, NV50_SOR_DP_UNK120(or, link), mask, pre);
	nv_mask(dev, NV50_SOR_DP_UNK130(or, link), 0x0000ff0f, unk);

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

bool
nouveau_dp_link_train(struct drm_encoder *encoder, u32 datarate)
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

	dp.table = nouveau_dp_bios_data(dev, nv_encoder->dcb, &dp.entry);
	if (!dp.table)
		return -EINVAL;

	dp.dcb = nv_encoder->dcb;
	dp.crtc = nv_crtc->index;
	dp.auxch = auxch->drive;
	dp.or = nv_encoder->or;
	dp.link = !(nv_encoder->dcb->sorconf.link & 1);
	dp.dpcd = nv_encoder->dp.dpcd;

	/* some sinks toggle hotplug in response to some of the actions
	 * we take during link training (DP_SET_POWER is one), we need
	 * to ignore them for the moment to avoid races.
	 */
	nouveau_gpio_irq(dev, 0, nv_connector->hpd, 0xff, false);

	/* enable down-spreading, if possible */
	if (dp.table[1] >= 16) {
		u16 script = ROM16(dp.entry[14]);
		if (nv_encoder->dp.dpcd[3] & 1)
			script = ROM16(dp.entry[12]);

		nouveau_bios_run_init_table(dev, script, dp.dcb, dp.crtc);
	}

	/* execute pre-train script from vbios */
	nouveau_bios_run_init_table(dev, ROM16(dp.entry[6]), dp.dcb, dp.crtc);

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
	nouveau_bios_run_init_table(dev, ROM16(dp.entry[8]), dp.dcb, dp.crtc);

	/* re-enable hotplug detect */
	nouveau_gpio_irq(dev, 0, nv_connector->hpd, 0xff, true);
	return true;
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
