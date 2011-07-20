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

static int
auxch_rd(struct drm_encoder *encoder, int address, uint8_t *buf, int size)
{
	struct drm_device *dev = encoder->dev;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_i2c_chan *auxch;
	int ret;

	auxch = nouveau_i2c_find(dev, nv_encoder->dcb->i2c_index);
	if (!auxch)
		return -ENODEV;

	ret = nouveau_dp_auxch(auxch, 9, address, buf, size);
	if (ret)
		return ret;

	return 0;
}

static int
auxch_wr(struct drm_encoder *encoder, int address, uint8_t *buf, int size)
{
	struct drm_device *dev = encoder->dev;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_i2c_chan *auxch;
	int ret;

	auxch = nouveau_i2c_find(dev, nv_encoder->dcb->i2c_index);
	if (!auxch)
		return -ENODEV;

	ret = nouveau_dp_auxch(auxch, 8, address, buf, size);
	return ret;
}

static int
nouveau_dp_lane_count_set(struct drm_encoder *encoder, uint8_t cmd)
{
	struct drm_device *dev = encoder->dev;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	uint32_t tmp;
	int or = nv_encoder->or, link = !(nv_encoder->dcb->sorconf.link & 1);

	tmp  = nv_rd32(dev, NV50_SOR_DP_CTRL(or, link));
	tmp &= ~(NV50_SOR_DP_CTRL_ENHANCED_FRAME_ENABLED |
		 NV50_SOR_DP_CTRL_LANE_MASK);
	tmp |= ((1 << (cmd & DP_LANE_COUNT_MASK)) - 1) << 16;
	if (cmd & DP_LANE_COUNT_ENHANCED_FRAME_EN)
		tmp |= NV50_SOR_DP_CTRL_ENHANCED_FRAME_ENABLED;
	nv_wr32(dev, NV50_SOR_DP_CTRL(or, link), tmp);

	return auxch_wr(encoder, DP_LANE_COUNT_SET, &cmd, 1);
}

static int
nouveau_dp_link_bw_set(struct drm_encoder *encoder, uint8_t cmd)
{
	struct drm_device *dev = encoder->dev;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	uint32_t tmp;
	int reg = 0x614300 + (nv_encoder->or * 0x800);

	tmp  = nv_rd32(dev, reg);
	tmp &= 0xfff3ffff;
	if (cmd == DP_LINK_BW_2_7)
		tmp |= 0x00040000;
	nv_wr32(dev, reg, tmp);

	return auxch_wr(encoder, DP_LINK_BW_SET, &cmd, 1);
}

static int
nouveau_dp_link_train_set(struct drm_encoder *encoder, int pattern)
{
	struct drm_device *dev = encoder->dev;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	uint32_t tmp;
	uint8_t cmd;
	int or = nv_encoder->or, link = !(nv_encoder->dcb->sorconf.link & 1);
	int ret;

	tmp  = nv_rd32(dev, NV50_SOR_DP_CTRL(or, link));
	tmp &= ~NV50_SOR_DP_CTRL_TRAINING_PATTERN;
	tmp |= (pattern << 24);
	nv_wr32(dev, NV50_SOR_DP_CTRL(or, link), tmp);

	ret = auxch_rd(encoder, DP_TRAINING_PATTERN_SET, &cmd, 1);
	if (ret)
		return ret;
	cmd &= ~DP_TRAINING_PATTERN_MASK;
	cmd |= (pattern & DP_TRAINING_PATTERN_MASK);
	return auxch_wr(encoder, DP_TRAINING_PATTERN_SET, &cmd, 1);
}

static int
nouveau_dp_max_voltage_swing(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct bit_displayport_encoder_table_entry *dpse;
	struct bit_displayport_encoder_table *dpe;
	int i, dpe_headerlen, max_vs = 0;

	dpe = nouveau_bios_dp_table(dev, nv_encoder->dcb, &dpe_headerlen);
	if (!dpe)
		return false;
	dpse = (void *)((char *)dpe + dpe_headerlen);

	for (i = 0; i < dpe_headerlen; i++, dpse++) {
		if (dpse->vs_level > max_vs)
			max_vs = dpse->vs_level;
	}

	return max_vs;
}

static int
nouveau_dp_max_pre_emphasis(struct drm_encoder *encoder, int vs)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct bit_displayport_encoder_table_entry *dpse;
	struct bit_displayport_encoder_table *dpe;
	int i, dpe_headerlen, max_pre = 0;

	dpe = nouveau_bios_dp_table(dev, nv_encoder->dcb, &dpe_headerlen);
	if (!dpe)
		return false;
	dpse = (void *)((char *)dpe + dpe_headerlen);

	for (i = 0; i < dpe_headerlen; i++, dpse++) {
		if (dpse->vs_level != vs)
			continue;

		if (dpse->pre_level > max_pre)
			max_pre = dpse->pre_level;
	}

	return max_pre;
}

static bool
nouveau_dp_link_train_adjust(struct drm_encoder *encoder, uint8_t *config)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct bit_displayport_encoder_table *dpe;
	int ret, i, dpe_headerlen, vs = 0, pre = 0;
	uint8_t request[2];

	dpe = nouveau_bios_dp_table(dev, nv_encoder->dcb, &dpe_headerlen);
	if (!dpe)
		return false;

	ret = auxch_rd(encoder, DP_ADJUST_REQUEST_LANE0_1, request, 2);
	if (ret)
		return false;

	NV_DEBUG_KMS(dev, "\t\tadjust 0x%02x 0x%02x\n", request[0], request[1]);

	/* Keep all lanes at the same level.. */
	for (i = 0; i < nv_encoder->dp.link_nr; i++) {
		int lane_req = (request[i >> 1] >> ((i & 1) << 2)) & 0xf;
		int lane_vs = lane_req & 3;
		int lane_pre = (lane_req >> 2) & 3;

		if (lane_vs > vs)
			vs = lane_vs;
		if (lane_pre > pre)
			pre = lane_pre;
	}

	if (vs >= nouveau_dp_max_voltage_swing(encoder)) {
		vs  = nouveau_dp_max_voltage_swing(encoder);
		vs |= 4;
	}

	if (pre >= nouveau_dp_max_pre_emphasis(encoder, vs & 3)) {
		pre  = nouveau_dp_max_pre_emphasis(encoder, vs & 3);
		pre |= 4;
	}

	/* Update the configuration for all lanes.. */
	for (i = 0; i < nv_encoder->dp.link_nr; i++)
		config[i] = (pre << 3) | vs;

	return true;
}

static bool
nouveau_dp_link_train_commit(struct drm_encoder *encoder, uint8_t *config)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct bit_displayport_encoder_table_entry *dpse;
	struct bit_displayport_encoder_table *dpe;
	int or = nv_encoder->or, link = !(nv_encoder->dcb->sorconf.link & 1);
	int dpe_headerlen, ret, i;

	NV_DEBUG_KMS(dev, "\t\tconfig 0x%02x 0x%02x 0x%02x 0x%02x\n",
		 config[0], config[1], config[2], config[3]);

	dpe = nouveau_bios_dp_table(dev, nv_encoder->dcb, &dpe_headerlen);
	if (!dpe)
		return false;
	dpse = (void *)((char *)dpe + dpe_headerlen);

	for (i = 0; i < dpe->record_nr; i++, dpse++) {
		if (dpse->vs_level == (config[0] & 3) &&
		    dpse->pre_level == ((config[0] >> 3) & 3))
			break;
	}
	BUG_ON(i == dpe->record_nr);

	for (i = 0; i < nv_encoder->dp.link_nr; i++) {
		const int shift[4] = { 16, 8, 0, 24 };
		uint32_t mask = 0xff << shift[i];
		uint32_t reg0, reg1, reg2;

		reg0  = nv_rd32(dev, NV50_SOR_DP_UNK118(or, link)) & ~mask;
		reg0 |= (dpse->reg0 << shift[i]);
		reg1  = nv_rd32(dev, NV50_SOR_DP_UNK120(or, link)) & ~mask;
		reg1 |= (dpse->reg1 << shift[i]);
		reg2  = nv_rd32(dev, NV50_SOR_DP_UNK130(or, link)) & 0xffff00ff;
		reg2 |= (dpse->reg2 << 8);
		nv_wr32(dev, NV50_SOR_DP_UNK118(or, link), reg0);
		nv_wr32(dev, NV50_SOR_DP_UNK120(or, link), reg1);
		nv_wr32(dev, NV50_SOR_DP_UNK130(or, link), reg2);
	}

	ret = auxch_wr(encoder, DP_TRAINING_LANE0_SET, config, 4);
	if (ret)
		return false;

	return true;
}

bool
nouveau_dp_link_train(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;
	struct bit_displayport_encoder_table *dpe;
	int dpe_headerlen;
	uint8_t config[4], status[3];
	bool cr_done, cr_max_vs, eq_done, hpd_state;
	int ret = 0, i, tries, voltage;

	NV_DEBUG_KMS(dev, "link training!!\n");

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!nv_connector)
		return false;

	dpe = nouveau_bios_dp_table(dev, nv_encoder->dcb, &dpe_headerlen);
	if (!dpe) {
		NV_ERROR(dev, "SOR-%d: no DP encoder table!\n", nv_encoder->or);
		return false;
	}

	/* disable hotplug detect, this flips around on some panels during
	 * link training.
	 */
	hpd_state = pgpio->irq_enable(dev, nv_connector->dcb->gpio_tag, false);

	if (dpe->script0) {
		NV_DEBUG_KMS(dev, "SOR-%d: running DP script 0\n", nv_encoder->or);
		nouveau_bios_run_init_table(dev, le16_to_cpu(dpe->script0),
					    nv_encoder->dcb, -1);
	}

train:
	cr_done = eq_done = false;

	/* set link configuration */
	NV_DEBUG_KMS(dev, "\tbegin train: bw %d, lanes %d\n",
		 nv_encoder->dp.link_bw, nv_encoder->dp.link_nr);

	ret = nouveau_dp_link_bw_set(encoder, nv_encoder->dp.link_bw);
	if (ret)
		return false;

	config[0] = nv_encoder->dp.link_nr;
	if (nv_encoder->dp.dpcd_version >= 0x11 &&
	    nv_encoder->dp.enhanced_frame)
		config[0] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	ret = nouveau_dp_lane_count_set(encoder, config[0]);
	if (ret)
		return false;

	/* clock recovery */
	NV_DEBUG_KMS(dev, "\tbegin cr\n");
	ret = nouveau_dp_link_train_set(encoder, DP_TRAINING_PATTERN_1);
	if (ret)
		goto stop;

	tries = 0;
	voltage = -1;
	memset(config, 0x00, sizeof(config));
	for (;;) {
		if (!nouveau_dp_link_train_commit(encoder, config))
			break;

		udelay(100);

		ret = auxch_rd(encoder, DP_LANE0_1_STATUS, status, 2);
		if (ret)
			break;
		NV_DEBUG_KMS(dev, "\t\tstatus: 0x%02x 0x%02x\n",
			 status[0], status[1]);

		cr_done = true;
		cr_max_vs = false;
		for (i = 0; i < nv_encoder->dp.link_nr; i++) {
			int lane = (status[i >> 1] >> ((i & 1) * 4)) & 0xf;

			if (!(lane & DP_LANE_CR_DONE)) {
				cr_done = false;
				if (config[i] & DP_TRAIN_MAX_PRE_EMPHASIS_REACHED)
					cr_max_vs = true;
				break;
			}
		}

		if ((config[0] & DP_TRAIN_VOLTAGE_SWING_MASK) != voltage) {
			voltage = config[0] & DP_TRAIN_VOLTAGE_SWING_MASK;
			tries = 0;
		}

		if (cr_done || cr_max_vs || (++tries == 5))
			break;

		if (!nouveau_dp_link_train_adjust(encoder, config))
			break;
	}

	if (!cr_done)
		goto stop;

	/* channel equalisation */
	NV_DEBUG_KMS(dev, "\tbegin eq\n");
	ret = nouveau_dp_link_train_set(encoder, DP_TRAINING_PATTERN_2);
	if (ret)
		goto stop;

	for (tries = 0; tries <= 5; tries++) {
		udelay(400);

		ret = auxch_rd(encoder, DP_LANE0_1_STATUS, status, 3);
		if (ret)
			break;
		NV_DEBUG_KMS(dev, "\t\tstatus: 0x%02x 0x%02x\n",
			 status[0], status[1]);

		eq_done = true;
		if (!(status[2] & DP_INTERLANE_ALIGN_DONE))
			eq_done = false;

		for (i = 0; eq_done && i < nv_encoder->dp.link_nr; i++) {
			int lane = (status[i >> 1] >> ((i & 1) * 4)) & 0xf;

			if (!(lane & DP_LANE_CR_DONE)) {
				cr_done = false;
				break;
			}

			if (!(lane & DP_LANE_CHANNEL_EQ_DONE) ||
			    !(lane & DP_LANE_SYMBOL_LOCKED)) {
				eq_done = false;
				break;
			}
		}

		if (eq_done || !cr_done)
			break;

		if (!nouveau_dp_link_train_adjust(encoder, config) ||
		    !nouveau_dp_link_train_commit(encoder, config))
			break;
	}

stop:
	/* end link training */
	ret = nouveau_dp_link_train_set(encoder, DP_TRAINING_PATTERN_DISABLE);
	if (ret)
		return false;

	/* retry at a lower setting, if possible */
	if (!ret && !(eq_done && cr_done)) {
		NV_DEBUG_KMS(dev, "\twe failed\n");
		if (nv_encoder->dp.link_bw != DP_LINK_BW_1_62) {
			NV_DEBUG_KMS(dev, "retry link training at low rate\n");
			nv_encoder->dp.link_bw = DP_LINK_BW_1_62;
			goto train;
		}
	}

	if (dpe->script1) {
		NV_DEBUG_KMS(dev, "SOR-%d: running DP script 1\n", nv_encoder->or);
		nouveau_bios_run_init_table(dev, le16_to_cpu(dpe->script1),
					    nv_encoder->dcb, -1);
	}

	/* re-enable hotplug detect */
	pgpio->irq_enable(dev, nv_connector->dcb->gpio_tag, hpd_state);

	return eq_done;
}

bool
nouveau_dp_detect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	uint8_t dpcd[4];
	int ret;

	ret = auxch_rd(encoder, 0x0000, dpcd, 4);
	if (ret)
		return false;

	NV_DEBUG_KMS(dev, "encoder: link_bw %d, link_nr %d\n"
		      "display: link_bw %d, link_nr %d version 0x%02x\n",
		 nv_encoder->dcb->dpconf.link_bw,
		 nv_encoder->dcb->dpconf.link_nr,
		 dpcd[1], dpcd[2] & 0x0f, dpcd[0]);

	nv_encoder->dp.dpcd_version = dpcd[0];

	nv_encoder->dp.link_bw = dpcd[1];
	if (nv_encoder->dp.link_bw != DP_LINK_BW_1_62 &&
	    !nv_encoder->dcb->dpconf.link_bw)
		nv_encoder->dp.link_bw = DP_LINK_BW_1_62;

	nv_encoder->dp.link_nr = dpcd[2] & DP_MAX_LANE_COUNT_MASK;
	if (nv_encoder->dp.link_nr > nv_encoder->dcb->dpconf.link_nr)
		nv_encoder->dp.link_nr = nv_encoder->dcb->dpconf.link_nr;

	nv_encoder->dp.enhanced_frame = (dpcd[2] & DP_ENHANCED_FRAME_CAP);

	return true;
}

int
nouveau_dp_auxch(struct nouveau_i2c_chan *auxch, int cmd, int addr,
		 uint8_t *data, int data_nr)
{
	return auxch_tx(auxch->dev, auxch->rd, cmd, addr, data, data_nr);
}

static int
nouveau_dp_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct nouveau_i2c_chan *auxch = (struct nouveau_i2c_chan *)adap;
	struct drm_device *dev = auxch->dev;
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
