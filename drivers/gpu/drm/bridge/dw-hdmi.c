/*
 * DesignWare High-Definition Multimedia Interface (HDMI) driver
 *
 * Copyright (C) 2013-2015 Mentor Graphics Inc.
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc.
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Designware High-Definition Multimedia Interface (HDMI) driver
 *
 */
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/hdmi.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/spinlock.h>

#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder_slave.h>
#include <drm/drm_scdc_helper.h>
#include <drm/bridge/dw_hdmi.h>
#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif

#include <uapi/linux/media-bus-format.h>
#include <uapi/linux/videodev2.h>

#include "dw-hdmi.h"
#include "dw-hdmi-audio.h"
#include "dw-hdmi-hdcp.h"

#define HDMI_EDID_LEN		512
#define DDC_SEGMENT_ADDR       0x30

enum hdmi_datamap {
	RGB444_8B = 0x01,
	RGB444_10B = 0x03,
	RGB444_12B = 0x05,
	RGB444_16B = 0x07,
	YCbCr444_8B = 0x09,
	YCbCr444_10B = 0x0B,
	YCbCr444_12B = 0x0D,
	YCbCr444_16B = 0x0F,
	YCbCr422_8B = 0x16,
	YCbCr422_10B = 0x14,
	YCbCr422_12B = 0x12,
};

/*
 * Unless otherwise noted, entries in this table are 100% optimization.
 * Values can be obtained from hdmi_compute_n() but that function is
 * slow so we pre-compute values we expect to see.
 *
 * All 32k and 48k values are expected to be the same (due to the way
 * the math works) for any rate that's an exact kHz.
 */
static const struct dw_hdmi_audio_tmds_n common_tmds_n_table[] = {
	{ .tmds = 25175000, .n_32k = 4096, .n_44k1 = 12854, .n_48k = 6144, },
	{ .tmds = 25200000, .n_32k = 4096, .n_44k1 = 5656, .n_48k = 6144, },
	{ .tmds = 27000000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 28320000, .n_32k = 4096, .n_44k1 = 5586, .n_48k = 6144, },
	{ .tmds = 30240000, .n_32k = 4096, .n_44k1 = 5642, .n_48k = 6144, },
	{ .tmds = 31500000, .n_32k = 4096, .n_44k1 = 5600, .n_48k = 6144, },
	{ .tmds = 32000000, .n_32k = 4096, .n_44k1 = 5733, .n_48k = 6144, },
	{ .tmds = 33750000, .n_32k = 4096, .n_44k1 = 6272, .n_48k = 6144, },
	{ .tmds = 36000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	{ .tmds = 40000000, .n_32k = 4096, .n_44k1 = 5733, .n_48k = 6144, },
	{ .tmds = 49500000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 50000000, .n_32k = 4096, .n_44k1 = 5292, .n_48k = 6144, },
	{ .tmds = 54000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	{ .tmds = 65000000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 68250000, .n_32k = 4096, .n_44k1 = 5376, .n_48k = 6144, },
	{ .tmds = 71000000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 72000000, .n_32k = 4096, .n_44k1 = 5635, .n_48k = 6144, },
	{ .tmds = 73250000, .n_32k = 4096, .n_44k1 = 14112, .n_48k = 6144, },
	{ .tmds = 74250000, .n_32k = 4096, .n_44k1 = 6272, .n_48k = 6144, },
	{ .tmds = 75000000, .n_32k = 4096, .n_44k1 = 5880, .n_48k = 6144, },
	{ .tmds = 78750000, .n_32k = 4096, .n_44k1 = 5600, .n_48k = 6144, },
	{ .tmds = 78800000, .n_32k = 4096, .n_44k1 = 5292, .n_48k = 6144, },
	{ .tmds = 79500000, .n_32k = 4096, .n_44k1 = 4704, .n_48k = 6144, },
	{ .tmds = 83500000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 85500000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 88750000, .n_32k = 4096, .n_44k1 = 14112, .n_48k = 6144, },
	{ .tmds = 97750000, .n_32k = 4096, .n_44k1 = 14112, .n_48k = 6144, },
	{ .tmds = 101000000, .n_32k = 4096, .n_44k1 = 7056, .n_48k = 6144, },
	{ .tmds = 106500000, .n_32k = 4096, .n_44k1 = 4704, .n_48k = 6144, },
	{ .tmds = 108000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	{ .tmds = 115500000, .n_32k = 4096, .n_44k1 = 5712, .n_48k = 6144, },
	{ .tmds = 119000000, .n_32k = 4096, .n_44k1 = 5544, .n_48k = 6144, },
	{ .tmds = 135000000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 146250000, .n_32k = 4096, .n_44k1 = 6272, .n_48k = 6144, },
	{ .tmds = 148500000, .n_32k = 4096, .n_44k1 = 5488, .n_48k = 6144, },
	{ .tmds = 154000000, .n_32k = 4096, .n_44k1 = 5544, .n_48k = 6144, },
	{ .tmds = 162000000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },

	/* For 297 MHz+ HDMI spec have some other rule for setting N */
	{ .tmds = 297000000, .n_32k = 3073, .n_44k1 = 4704, .n_48k = 5120, },
	{ .tmds = 594000000, .n_32k = 3073, .n_44k1 = 9408, .n_48k = 10240, },

	/* End of table */
	{ .tmds = 0,         .n_32k = 0,    .n_44k1 = 0,    .n_48k = 0, },
};

static const u16 csc_coeff_default[3][4] = {
	{ 0x2000, 0x0000, 0x0000, 0x0000 },
	{ 0x0000, 0x2000, 0x0000, 0x0000 },
	{ 0x0000, 0x0000, 0x2000, 0x0000 }
};

static const u16 csc_coeff_rgb_out_eitu601[3][4] = {
	{ 0x2000, 0x6926, 0x74fd, 0x010e },
	{ 0x2000, 0x2cdd, 0x0000, 0x7e9a },
	{ 0x2000, 0x0000, 0x38b4, 0x7e3b }
};

static const u16 csc_coeff_rgb_out_eitu709[3][4] = {
	{ 0x2000, 0x7106, 0x7a02, 0x00a7 },
	{ 0x2000, 0x3264, 0x0000, 0x7e6d },
	{ 0x2000, 0x0000, 0x3b61, 0x7e25 }
};

static const u16 csc_coeff_rgb_in_eitu601[3][4] = {
	{ 0x2591, 0x1322, 0x074b, 0x0000 },
	{ 0x6535, 0x2000, 0x7acc, 0x0200 },
	{ 0x6acd, 0x7534, 0x2000, 0x0200 }
};

static const u16 csc_coeff_rgb_in_eitu709[3][4] = {
	{ 0x2dc5, 0x0d9b, 0x049e, 0x0000 },
	{ 0x62f0, 0x2000, 0x7d11, 0x0200 },
	{ 0x6756, 0x78ab, 0x2000, 0x0200 }
};

struct hdmi_vmode {
	bool mdataenablepolarity;

	unsigned int previous_pixelclock;
	unsigned int mpixelclock;
	unsigned int mpixelrepetitioninput;
	unsigned int mpixelrepetitionoutput;
	unsigned int previous_tmdsclock;
	unsigned int mtmdsclock;
};

struct hdmi_data_info {
	unsigned int enc_in_bus_format;
	unsigned int enc_out_bus_format;
	unsigned int enc_in_encoding;
	unsigned int enc_out_encoding;
	unsigned int pix_repet_factor;
	struct hdmi_vmode video_mode;
	bool update;
};

struct dw_hdmi_i2c {
	struct i2c_adapter	adap;

	struct mutex		lock;
	struct completion	cmp;
	u8			stat;

	u8			slave_reg;
	bool			is_regaddr;
	bool			is_segment;

	unsigned int		scl_high_ns;
	unsigned int		scl_low_ns;
};

struct dw_hdmi_phy_data {
	enum dw_hdmi_phy_type type;
	const char *name;
	unsigned int gen;
	bool has_svsret;
	int (*configure)(struct dw_hdmi *hdmi,
			 const struct dw_hdmi_plat_data *pdata,
			 unsigned long mpixelclock);
};

struct dw_hdmi {
	struct drm_connector connector;
	struct drm_encoder *encoder;
	struct drm_bridge bridge;
	struct platform_device *hdcp_dev;
	enum dw_hdmi_devtype dev_type;
	unsigned int version;

	struct platform_device *audio;
	struct device *dev;
	struct clk *isfr_clk;
	struct clk *iahb_clk;
	struct dw_hdmi_i2c *i2c;

	struct hdmi_data_info hdmi_data;
	const struct dw_hdmi_plat_data *plat_data;
	struct dw_hdcp *hdcp;

	int vic;

	u8 edid[HDMI_EDID_LEN];
	bool cable_plugin;

	struct {
		const struct dw_hdmi_phy_ops *ops;
		const char *name;
		void *data;
		bool enabled;
	} phy;

	struct drm_display_mode previous_mode;

	struct i2c_adapter *ddc;
	void __iomem *regs;
	bool sink_is_hdmi;
	bool sink_has_audio;
	bool hpd_state;

	struct delayed_work work;
	struct workqueue_struct *workqueue;

	struct mutex mutex;		/* for state below and previous_mode */
	enum drm_connector_force force;	/* mutex-protected force state */
	bool disabled;			/* DRM has disabled our bridge */
	bool bridge_is_on;		/* indicates the bridge is on */
	bool rxsense;			/* rxsense state */
	u8 phy_mask;			/* desired phy int mask settings */

	spinlock_t audio_lock;
	struct mutex audio_mutex;
	struct dentry *debugfs_dir;
	unsigned int sample_rate;
	unsigned int audio_cts;
	unsigned int audio_n;
	bool audio_enable;

#ifdef CONFIG_SWITCH
	struct switch_dev switchdev;
#endif
	int irq;

	void (*write)(struct dw_hdmi *hdmi, u8 val, int offset);
	u8 (*read)(struct dw_hdmi *hdmi, int offset);
};

#define HDMI_IH_PHY_STAT0_RX_SENSE \
	(HDMI_IH_PHY_STAT0_RX_SENSE0 | HDMI_IH_PHY_STAT0_RX_SENSE1 | \
	 HDMI_IH_PHY_STAT0_RX_SENSE2 | HDMI_IH_PHY_STAT0_RX_SENSE3)

#define HDMI_PHY_RX_SENSE \
	(HDMI_PHY_RX_SENSE0 | HDMI_PHY_RX_SENSE1 | \
	 HDMI_PHY_RX_SENSE2 | HDMI_PHY_RX_SENSE3)

static void dw_hdmi_writel(struct dw_hdmi *hdmi, u8 val, int offset)
{
	writel(val, hdmi->regs + (offset << 2));
}

static u8 dw_hdmi_readl(struct dw_hdmi *hdmi, int offset)
{
	return readl(hdmi->regs + (offset << 2));
}

static void dw_hdmi_writeb(struct dw_hdmi *hdmi, u8 val, int offset)
{
	writeb(val, hdmi->regs + offset);
}

static u8 dw_hdmi_readb(struct dw_hdmi *hdmi, int offset)
{
	return readb(hdmi->regs + offset);
}

static inline void hdmi_writeb(struct dw_hdmi *hdmi, u8 val, int offset)
{
	hdmi->write(hdmi, val, offset);
}

static inline u8 hdmi_readb(struct dw_hdmi *hdmi, int offset)
{
	return hdmi->read(hdmi, offset);
}

static void hdmi_modb(struct dw_hdmi *hdmi, u8 data, u8 mask, unsigned reg)
{
	u8 val = hdmi_readb(hdmi, reg) & ~mask;

	val |= data & mask;
	hdmi_writeb(hdmi, val, reg);
}

static void hdmi_mask_writeb(struct dw_hdmi *hdmi, u8 data, unsigned int reg,
			     u8 shift, u8 mask)
{
	hdmi_modb(hdmi, data << shift, mask, reg);
}

static void repo_hpd_event(struct work_struct *p_work)
{
	struct dw_hdmi *hdmi = container_of(p_work, struct dw_hdmi, work.work);

	if (hdmi->bridge.dev)
		drm_helper_hpd_irq_event(hdmi->bridge.dev);
#ifdef CONFIG_SWITCH
	if (hdmi->hpd_state)
		switch_set_state(&hdmi->switchdev, 1);
	else
		switch_set_state(&hdmi->switchdev, 0);
#endif
}

static bool check_hdmi_irq(struct dw_hdmi *hdmi, int intr_stat,
			   int phy_int_pol)
{
	int msecs;

	/* To determine whether interrupt type is HPD */
	if (!(intr_stat & HDMI_IH_PHY_STAT0_HPD))
		return false;

	if (phy_int_pol & HDMI_PHY_HPD) {
		dev_dbg(hdmi->dev, "dw hdmi plug in\n");
		msecs = 150;
		hdmi->hpd_state = true;
	} else {
		dev_dbg(hdmi->dev, "dw hdmi plug out\n");
		msecs = 20;
		hdmi->hpd_state = false;
	}
	mod_delayed_work(hdmi->workqueue, &hdmi->work, msecs_to_jiffies(msecs));

	return true;
}

static void init_hpd_work(struct dw_hdmi *hdmi)
{
	hdmi->workqueue = create_workqueue("hpd_queue");
	INIT_DELAYED_WORK(&hdmi->work, repo_hpd_event);
}

static void dw_hdmi_i2c_set_divs(struct dw_hdmi *hdmi)
{
	unsigned long clk_rate_khz;
	unsigned long low_ns, high_ns;
	unsigned long div_low, div_high;

	/* Standard-mode */
	if (hdmi->i2c->scl_high_ns < 4000)
		high_ns = 4708;
	else
		high_ns = hdmi->i2c->scl_high_ns;

	if (hdmi->i2c->scl_low_ns < 4700)
		low_ns = 4916;
	else
		low_ns = hdmi->i2c->scl_low_ns;

	/* Adjust to avoid overflow */
	clk_rate_khz = DIV_ROUND_UP(clk_get_rate(hdmi->isfr_clk), 1000);

	div_low = (clk_rate_khz * low_ns) / 1000000;
	if ((clk_rate_khz * low_ns) % 1000000)
		div_low++;

	div_high = (clk_rate_khz * high_ns) / 1000000;
	if ((clk_rate_khz * high_ns) % 1000000)
		div_high++;

	/* Maximum divider supported by hw is 0xffff */
	if (div_low > 0xffff)
		div_low = 0xffff;

	if (div_high > 0xffff)
		div_high = 0xffff;

	hdmi_writeb(hdmi, div_high & 0xff, HDMI_I2CM_SS_SCL_HCNT_0_ADDR);
	hdmi_writeb(hdmi, (div_high >> 8) & 0xff,
		    HDMI_I2CM_SS_SCL_HCNT_1_ADDR);
	hdmi_writeb(hdmi, div_low & 0xff, HDMI_I2CM_SS_SCL_LCNT_0_ADDR);
	hdmi_writeb(hdmi, (div_low >> 8) & 0xff,
		    HDMI_I2CM_SS_SCL_LCNT_1_ADDR);
}

static void dw_hdmi_i2c_init(struct dw_hdmi *hdmi)
{
	/* Software reset */
	hdmi_writeb(hdmi, 0x00, HDMI_I2CM_SOFTRSTZ);

	/* Set Standard Mode speed */
	hdmi_modb(hdmi, HDMI_I2CM_DIV_STD_MODE,
		  HDMI_I2CM_DIV_FAST_STD_MODE, HDMI_I2CM_DIV);

	/* Set done, not acknowledged and arbitration interrupt polarities */
	hdmi_writeb(hdmi, HDMI_I2CM_INT_DONE_POL, HDMI_I2CM_INT);
	hdmi_writeb(hdmi, HDMI_I2CM_CTLINT_NAC_POL | HDMI_I2CM_CTLINT_ARB_POL,
		    HDMI_I2CM_CTLINT);

	/* Clear DONE and ERROR interrupts */
	hdmi_writeb(hdmi, HDMI_IH_I2CM_STAT0_ERROR | HDMI_IH_I2CM_STAT0_DONE,
		    HDMI_IH_I2CM_STAT0);

	/* Mute DONE and ERROR interrupts */
	hdmi_writeb(hdmi, HDMI_IH_I2CM_STAT0_ERROR | HDMI_IH_I2CM_STAT0_DONE,
		    HDMI_IH_MUTE_I2CM_STAT0);

	/* set SDA high level holding time */
	hdmi_writeb(hdmi, 0x48, HDMI_I2CM_SDA_HOLD);

	dw_hdmi_i2c_set_divs(hdmi);
}

static int dw_hdmi_i2c_read(struct dw_hdmi *hdmi,
			    unsigned char *buf, unsigned int length)
{
	struct dw_hdmi_i2c *i2c = hdmi->i2c;
	int stat;

	if (!i2c->is_regaddr) {
		dev_dbg(hdmi->dev, "set read register address to 0\n");
		i2c->slave_reg = 0x00;
		i2c->is_regaddr = true;
	}

	while (length--) {
		reinit_completion(&i2c->cmp);

		hdmi_writeb(hdmi, i2c->slave_reg++, HDMI_I2CM_ADDRESS);
		if (i2c->is_segment)
			hdmi_writeb(hdmi, HDMI_I2CM_OPERATION_READ_EXT,
				    HDMI_I2CM_OPERATION);
		else
			hdmi_writeb(hdmi, HDMI_I2CM_OPERATION_READ,
				    HDMI_I2CM_OPERATION);

		stat = wait_for_completion_timeout(&i2c->cmp, HZ / 10);
		if (!stat)
			return -EAGAIN;

		/* Check for error condition on the bus */
		if (i2c->stat & HDMI_IH_I2CM_STAT0_ERROR)
			return -EIO;

		*buf++ = hdmi_readb(hdmi, HDMI_I2CM_DATAI);
	}
	i2c->is_segment = false;

	return 0;
}

static int dw_hdmi_i2c_write(struct dw_hdmi *hdmi,
			     unsigned char *buf, unsigned int length)
{
	struct dw_hdmi_i2c *i2c = hdmi->i2c;
	int stat;

	if (!i2c->is_regaddr) {
		/* Use the first write byte as register address */
		i2c->slave_reg = buf[0];
		length--;
		buf++;
		i2c->is_regaddr = true;
	}

	while (length--) {
		reinit_completion(&i2c->cmp);

		hdmi_writeb(hdmi, *buf++, HDMI_I2CM_DATAO);
		hdmi_writeb(hdmi, i2c->slave_reg++, HDMI_I2CM_ADDRESS);
		hdmi_writeb(hdmi, HDMI_I2CM_OPERATION_WRITE,
			    HDMI_I2CM_OPERATION);

	stat = wait_for_completion_timeout(&i2c->cmp, HZ / 10);
		if (!stat)
			return -EAGAIN;

		/* Check for error condition on the bus */
		if (i2c->stat & HDMI_IH_I2CM_STAT0_ERROR)
			return -EIO;
	}

	return 0;
}

static int dw_hdmi_i2c_xfer(struct i2c_adapter *adap,
			    struct i2c_msg *msgs, int num)
{
	struct dw_hdmi *hdmi = i2c_get_adapdata(adap);
	struct dw_hdmi_i2c *i2c = hdmi->i2c;
	u8 addr = msgs[0].addr;
	int i, ret = 0;

	dev_dbg(hdmi->dev, "xfer: num: %d, addr: %#x\n", num, addr);

	for (i = 0; i < num; i++) {
		if (msgs[i].len == 0) {
			dev_dbg(hdmi->dev,
				"unsupported transfer %d/%d, no data\n",
				i + 1, num);
			return -EOPNOTSUPP;
		}
	}

	mutex_lock(&i2c->lock);

	hdmi_writeb(hdmi, 0x00, HDMI_IH_MUTE_I2CM_STAT0);

	/* Set slave device address taken from the first I2C message */
	if (addr == DDC_SEGMENT_ADDR && msgs[0].len == 1)
		addr = DDC_ADDR;
	hdmi_writeb(hdmi, addr, HDMI_I2CM_SLAVE);

	/* Set slave device register address on transfer */
	i2c->is_regaddr = false;

	/* Set segment pointer for I2C extended read mode operation */
	i2c->is_segment = false;

	for (i = 0; i < num; i++) {
		dev_dbg(hdmi->dev, "xfer: num: %d/%d, len: %d, flags: %#x\n",
			i + 1, num, msgs[i].len, msgs[i].flags);
		if (msgs[i].addr == DDC_SEGMENT_ADDR && msgs[i].len == 1) {
			i2c->is_segment = true;
			hdmi_writeb(hdmi, DDC_SEGMENT_ADDR, HDMI_I2CM_SEGADDR);
			hdmi_writeb(hdmi, *msgs[i].buf, HDMI_I2CM_SEGPTR);
		} else {
			if (msgs[i].flags & I2C_M_RD)
				ret = dw_hdmi_i2c_read(hdmi, msgs[i].buf,
						       msgs[i].len);
			else
				ret = dw_hdmi_i2c_write(hdmi, msgs[i].buf,
							msgs[i].len);
		}
		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	/* Mute DONE and ERROR interrupts */
	hdmi_writeb(hdmi, HDMI_IH_I2CM_STAT0_ERROR | HDMI_IH_I2CM_STAT0_DONE,
		    HDMI_IH_MUTE_I2CM_STAT0);

	mutex_unlock(&i2c->lock);

	return ret;
}

static u32 dw_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm dw_hdmi_algorithm = {
	.master_xfer	= dw_hdmi_i2c_xfer,
	.functionality	= dw_hdmi_i2c_func,
};

static struct i2c_adapter *dw_hdmi_i2c_adapter(struct dw_hdmi *hdmi)
{
	struct i2c_adapter *adap;
	struct dw_hdmi_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(hdmi->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return ERR_PTR(-ENOMEM);

	mutex_init(&i2c->lock);
	init_completion(&i2c->cmp);

	adap = &i2c->adap;
	adap->class = I2C_CLASS_DDC;
	adap->owner = THIS_MODULE;
	adap->dev.parent = hdmi->dev;
	adap->dev.of_node = hdmi->dev->of_node;
	adap->algo = &dw_hdmi_algorithm;
	strlcpy(adap->name, "DesignWare HDMI", sizeof(adap->name));
	i2c_set_adapdata(adap, hdmi);

	ret = i2c_add_adapter(adap);
	if (ret) {
		dev_warn(hdmi->dev, "cannot add %s I2C adapter\n", adap->name);
		devm_kfree(hdmi->dev, i2c);
		return ERR_PTR(ret);
	}

	hdmi->i2c = i2c;

	dev_info(hdmi->dev, "registered %s I2C bus driver\n", adap->name);

	return adap;
}

static void hdmi_set_cts_n(struct dw_hdmi *hdmi, unsigned int cts,
			   unsigned int n)
{
	/* Must be set/cleared first */
	hdmi_modb(hdmi, 0, HDMI_AUD_CTS3_CTS_MANUAL, HDMI_AUD_CTS3);

	/* nshift factor = 0 */
	hdmi_modb(hdmi, 0, HDMI_AUD_CTS3_N_SHIFT_MASK, HDMI_AUD_CTS3);

	hdmi_writeb(hdmi, ((cts >> 16) & HDMI_AUD_CTS3_AUDCTS19_16_MASK) |
		    HDMI_AUD_CTS3_CTS_MANUAL, HDMI_AUD_CTS3);
	hdmi_writeb(hdmi, (cts >> 8) & 0xff, HDMI_AUD_CTS2);
	hdmi_writeb(hdmi, cts & 0xff, HDMI_AUD_CTS1);

	hdmi_writeb(hdmi, (n >> 16) & 0x0f, HDMI_AUD_N3);
	hdmi_writeb(hdmi, (n >> 8) & 0xff, HDMI_AUD_N2);
	hdmi_writeb(hdmi, n & 0xff, HDMI_AUD_N1);
}

static int hdmi_match_tmds_n_table(struct dw_hdmi *hdmi,
				   unsigned long pixel_clk,
				   unsigned long freq)
{
	const struct dw_hdmi_plat_data *plat_data = hdmi->plat_data;
	const struct dw_hdmi_audio_tmds_n *tmds_n = NULL;
	int i;

	if (plat_data->tmds_n_table) {
		for (i = 0; plat_data->tmds_n_table[i].tmds != 0; i++) {
			if (pixel_clk == plat_data->tmds_n_table[i].tmds) {
				tmds_n = &plat_data->tmds_n_table[i];
				break;
			}
		}
	}

	if (tmds_n == NULL) {
		for (i = 0; common_tmds_n_table[i].tmds != 0; i++) {
			if (pixel_clk == common_tmds_n_table[i].tmds) {
				tmds_n = &common_tmds_n_table[i];
				break;
			}
		}
	}

	if (tmds_n == NULL)
		return -ENOENT;

	switch (freq) {
	case 32000:
		return tmds_n->n_32k;
	case 44100:
	case 88200:
	case 176400:
		return (freq / 44100) * tmds_n->n_44k1;
	case 48000:
	case 96000:
	case 192000:
		return (freq / 48000) * tmds_n->n_48k;
	default:
		return -ENOENT;
	}
}

static u64 hdmi_audio_math_diff(unsigned int freq, unsigned int n,
				unsigned int pixel_clk)
{
	u64 final, diff;
	u64 cts;

	final = (u64)pixel_clk * n;

	cts = final;
	do_div(cts, 128 * freq);

	diff = final - (u64)cts * (128 * freq);

	return diff;
}

static unsigned int hdmi_compute_n(struct dw_hdmi *hdmi,
				   unsigned long pixel_clk,
				   unsigned long freq)
{
	unsigned int min_n = DIV_ROUND_UP((128 * freq), 1500);
	unsigned int max_n = (128 * freq) / 300;
	unsigned int ideal_n = (128 * freq) / 1000;
	unsigned int best_n_distance = ideal_n;
	unsigned int best_n = 0;
	u64 best_diff = U64_MAX;
	int n;

	/* If the ideal N could satisfy the audio math, then just take it */
	if (hdmi_audio_math_diff(freq, ideal_n, pixel_clk) == 0)
		return ideal_n;

	for (n = min_n; n <= max_n; n++) {
		u64 diff = hdmi_audio_math_diff(freq, n, pixel_clk);

		if (diff < best_diff || (diff == best_diff &&
		    abs(n - ideal_n) < best_n_distance)) {
			best_n = n;
			best_diff = diff;
			best_n_distance = abs(best_n - ideal_n);
		}

		/*
		 * The best N already satisfy the audio math, and also be
		 * the closest value to ideal N, so just cut the loop.
		 */
		if ((best_diff == 0) && (abs(n - ideal_n) > best_n_distance))
			break;
	}

	return best_n;
}

static unsigned int hdmi_find_n(struct dw_hdmi *hdmi, unsigned long pixel_clk,
				unsigned long sample_rate)
{
	int n;

	n = hdmi_match_tmds_n_table(hdmi, pixel_clk, sample_rate);
	if (n > 0)
		return n;

	dev_warn(hdmi->dev, "Rate %lu missing; compute N dynamically\n",
		 pixel_clk);

	return hdmi_compute_n(hdmi, pixel_clk, sample_rate);
}

static void hdmi_set_clk_regenerator(struct dw_hdmi *hdmi,
	unsigned long pixel_clk, unsigned int sample_rate)
{
	unsigned long ftdms = pixel_clk;
	unsigned int n, cts;
	u64 tmp;

	n = hdmi_find_n(hdmi, pixel_clk, sample_rate);

	/*
	 * Compute the CTS value from the N value.  Note that CTS and N
	 * can be up to 20 bits in total, so we need 64-bit math.  Also
	 * note that our TDMS clock is not fully accurate; it is accurate
	 * to kHz.  This can introduce an unnecessary remainder in the
	 * calculation below, so we don't try to warn about that.
	 */
	tmp = (u64)ftdms * n;
	do_div(tmp, 128 * sample_rate);
	cts = tmp;

	dev_dbg(hdmi->dev, "%s: fs=%uHz ftdms=%lu.%03luMHz N=%d cts=%d\n",
		__func__, sample_rate, ftdms / 1000000, (ftdms / 1000) % 1000,
		n, cts);

	spin_lock_irq(&hdmi->audio_lock);
	hdmi->audio_n = n;
	hdmi->audio_cts = cts;
	hdmi_set_cts_n(hdmi, cts, hdmi->audio_enable ? n : 0);
	spin_unlock_irq(&hdmi->audio_lock);
}

static void hdmi_init_clk_regenerator(struct dw_hdmi *hdmi)
{
	mutex_lock(&hdmi->audio_mutex);
	hdmi_set_clk_regenerator(hdmi, 74250000, hdmi->sample_rate);
	mutex_unlock(&hdmi->audio_mutex);
}

static void hdmi_clk_regenerator_update_pixel_clock(struct dw_hdmi *hdmi)
{
	mutex_lock(&hdmi->audio_mutex);
	hdmi_set_clk_regenerator(hdmi, hdmi->hdmi_data.video_mode.mtmdsclock,
				 hdmi->sample_rate);
	mutex_unlock(&hdmi->audio_mutex);
}

void dw_hdmi_set_sample_rate(struct dw_hdmi *hdmi, unsigned int rate)
{
	mutex_lock(&hdmi->audio_mutex);
	hdmi->sample_rate = rate;
	hdmi_set_clk_regenerator(hdmi, hdmi->hdmi_data.video_mode.mtmdsclock,
				 hdmi->sample_rate);
	mutex_unlock(&hdmi->audio_mutex);
}
EXPORT_SYMBOL_GPL(dw_hdmi_set_sample_rate);

void dw_hdmi_audio_enable(struct dw_hdmi *hdmi)
{
	unsigned long flags;

	spin_lock_irqsave(&hdmi->audio_lock, flags);
	hdmi->audio_enable = true;
	hdmi_set_cts_n(hdmi, hdmi->audio_cts, hdmi->audio_n);
	spin_unlock_irqrestore(&hdmi->audio_lock, flags);
}
EXPORT_SYMBOL_GPL(dw_hdmi_audio_enable);

void dw_hdmi_audio_disable(struct dw_hdmi *hdmi)
{
	unsigned long flags;

	spin_lock_irqsave(&hdmi->audio_lock, flags);
	hdmi->audio_enable = false;
	hdmi_set_cts_n(hdmi, hdmi->audio_cts, 0);
	spin_unlock_irqrestore(&hdmi->audio_lock, flags);
}
EXPORT_SYMBOL_GPL(dw_hdmi_audio_disable);

static bool hdmi_bus_fmt_is_rgb(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_RGB121212_1X36:
	case MEDIA_BUS_FMT_RGB161616_1X48:
		return true;

	default:
		return false;
	}
}

static bool hdmi_bus_fmt_is_yuv444(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_YUV12_1X36:
	case MEDIA_BUS_FMT_YUV16_1X48:
		return true;

	default:
		return false;
	}
}

static bool hdmi_bus_fmt_is_yuv422(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_UYVY12_1X24:
		return true;

	default:
		return false;
	}
}

static bool hdmi_bus_fmt_is_yuv420(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
	case MEDIA_BUS_FMT_UYYVYY12_0_5X36:
	case MEDIA_BUS_FMT_UYYVYY16_0_5X48:
		return true;

	default:
		return false;
	}
}

static int hdmi_bus_fmt_color_depth(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		return 8;

	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		return 10;

	case MEDIA_BUS_FMT_RGB121212_1X36:
	case MEDIA_BUS_FMT_YUV12_1X36:
	case MEDIA_BUS_FMT_UYVY12_1X24:
	case MEDIA_BUS_FMT_UYYVYY12_0_5X36:
		return 12;

	case MEDIA_BUS_FMT_RGB161616_1X48:
	case MEDIA_BUS_FMT_YUV16_1X48:
	case MEDIA_BUS_FMT_UYYVYY16_0_5X48:
		return 16;

	default:
		return 0;
	}
}

/*
 * this submodule is responsible for the video data synchronization.
 * for example, for RGB 4:4:4 input, the data map is defined as
 *			pin{47~40} <==> R[7:0]
 *			pin{31~24} <==> G[7:0]
 *			pin{15~8}  <==> B[7:0]
 */
static void hdmi_video_sample(struct dw_hdmi *hdmi)
{
	int color_format = 0;
	u8 val;

	switch (hdmi->hdmi_data.enc_in_bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
		color_format = 0x01;
		break;
	case MEDIA_BUS_FMT_RGB101010_1X30:
		color_format = 0x03;
		break;
	case MEDIA_BUS_FMT_RGB121212_1X36:
		color_format = 0x05;
		break;
	case MEDIA_BUS_FMT_RGB161616_1X48:
		color_format = 0x07;
		break;

	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		color_format = 0x09;
		break;
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		color_format = 0x0B;
		break;
	case MEDIA_BUS_FMT_YUV12_1X36:
	case MEDIA_BUS_FMT_UYYVYY12_0_5X36:
		color_format = 0x0D;
		break;
	case MEDIA_BUS_FMT_YUV16_1X48:
	case MEDIA_BUS_FMT_UYYVYY16_0_5X48:
		color_format = 0x0F;
		break;

	case MEDIA_BUS_FMT_UYVY8_1X16:
		color_format = 0x16;
		break;
	case MEDIA_BUS_FMT_UYVY10_1X20:
		color_format = 0x14;
		break;
	case MEDIA_BUS_FMT_UYVY12_1X24:
		color_format = 0x12;
		break;

	default:
		return;
	}

	val = HDMI_TX_INVID0_INTERNAL_DE_GENERATOR_DISABLE |
		((color_format << HDMI_TX_INVID0_VIDEO_MAPPING_OFFSET) &
		HDMI_TX_INVID0_VIDEO_MAPPING_MASK);
	hdmi_writeb(hdmi, val, HDMI_TX_INVID0);

	/* Enable TX stuffing: When DE is inactive, fix the output data to 0 */
	val = HDMI_TX_INSTUFFING_BDBDATA_STUFFING_ENABLE |
		HDMI_TX_INSTUFFING_RCRDATA_STUFFING_ENABLE |
		HDMI_TX_INSTUFFING_GYDATA_STUFFING_ENABLE;
	hdmi_writeb(hdmi, val, HDMI_TX_INSTUFFING);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_GYDATA0);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_GYDATA1);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_RCRDATA0);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_RCRDATA1);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_BCBDATA0);
	hdmi_writeb(hdmi, 0x0, HDMI_TX_BCBDATA1);
}

static int is_color_space_conversion(struct dw_hdmi *hdmi)
{
	return hdmi->hdmi_data.enc_in_bus_format != hdmi->hdmi_data.enc_out_bus_format;
}

static int is_color_space_decimation(struct dw_hdmi *hdmi)
{
	if (!hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_out_bus_format))
		return 0;

	if (hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_in_bus_format) ||
	    hdmi_bus_fmt_is_yuv444(hdmi->hdmi_data.enc_in_bus_format))
		return 1;

	return 0;
}

static int is_color_space_interpolation(struct dw_hdmi *hdmi)
{
	if (!hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_in_bus_format))
		return 0;

	if (hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_out_bus_format) ||
	    hdmi_bus_fmt_is_yuv444(hdmi->hdmi_data.enc_out_bus_format))
		return 1;

	return 0;
}

static void dw_hdmi_update_csc_coeffs(struct dw_hdmi *hdmi)
{
	const u16 (*csc_coeff)[3][4] = &csc_coeff_default;
	unsigned i;
	u32 csc_scale = 1;

	if (is_color_space_conversion(hdmi)) {
		if (hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_out_bus_format)) {
			if (hdmi->hdmi_data.enc_out_encoding ==
						V4L2_YCBCR_ENC_601)
				csc_coeff = &csc_coeff_rgb_out_eitu601;
			else
				csc_coeff = &csc_coeff_rgb_out_eitu709;
		} else if (hdmi_bus_fmt_is_rgb(
					hdmi->hdmi_data.enc_in_bus_format)) {
			if (hdmi->hdmi_data.enc_out_encoding ==
						V4L2_YCBCR_ENC_601)
				csc_coeff = &csc_coeff_rgb_in_eitu601;
			else
				csc_coeff = &csc_coeff_rgb_in_eitu709;
			csc_scale = 0;
		}
	}

	/* The CSC registers are sequential, alternating MSB then LSB */
	for (i = 0; i < ARRAY_SIZE(csc_coeff_default[0]); i++) {
		u16 coeff_a = (*csc_coeff)[0][i];
		u16 coeff_b = (*csc_coeff)[1][i];
		u16 coeff_c = (*csc_coeff)[2][i];

		hdmi_writeb(hdmi, coeff_a & 0xff, HDMI_CSC_COEF_A1_LSB + i * 2);
		hdmi_writeb(hdmi, coeff_a >> 8, HDMI_CSC_COEF_A1_MSB + i * 2);
		hdmi_writeb(hdmi, coeff_b & 0xff, HDMI_CSC_COEF_B1_LSB + i * 2);
		hdmi_writeb(hdmi, coeff_b >> 8, HDMI_CSC_COEF_B1_MSB + i * 2);
		hdmi_writeb(hdmi, coeff_c & 0xff, HDMI_CSC_COEF_C1_LSB + i * 2);
		hdmi_writeb(hdmi, coeff_c >> 8, HDMI_CSC_COEF_C1_MSB + i * 2);
	}

	hdmi_modb(hdmi, csc_scale, HDMI_CSC_SCALE_CSCSCALE_MASK,
		  HDMI_CSC_SCALE);
}

static void hdmi_video_csc(struct dw_hdmi *hdmi)
{
	int color_depth = 0;
	int interpolation = HDMI_CSC_CFG_INTMODE_DISABLE;
	int decimation = 0;

	/* YCC422 interpolation to 444 mode */
	if (is_color_space_interpolation(hdmi))
		interpolation = HDMI_CSC_CFG_INTMODE_CHROMA_INT_FORMULA1;
	else if (is_color_space_decimation(hdmi))
		decimation = HDMI_CSC_CFG_DECMODE_CHROMA_INT_FORMULA3;

	switch (hdmi_bus_fmt_color_depth(hdmi->hdmi_data.enc_out_bus_format)) {
	case 8:
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_24BPP;
		break;
	case 10:
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_30BPP;
		break;
	case 12:
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_36BPP;
		break;
	case 16:
		color_depth = HDMI_CSC_SCALE_CSC_COLORDE_PTH_48BPP;
		break;

	default:
		return;
	}

	/* Configure the CSC registers */
	hdmi_writeb(hdmi, interpolation | decimation, HDMI_CSC_CFG);
	hdmi_modb(hdmi, color_depth, HDMI_CSC_SCALE_CSC_COLORDE_PTH_MASK,
		  HDMI_CSC_SCALE);

	dw_hdmi_update_csc_coeffs(hdmi);
}

/*
 * HDMI video packetizer is used to packetize the data.
 * for example, if input is YCC422 mode or repeater is used,
 * data should be repacked this module can be bypassed.
 */
static void hdmi_video_packetize(struct dw_hdmi *hdmi)
{
	unsigned int color_depth = 0;
	unsigned int remap_size = HDMI_VP_REMAP_YCC422_16bit;
	unsigned int output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_PP;
	struct hdmi_data_info *hdmi_data = &hdmi->hdmi_data;
	u8 val, vp_conf;

	if (hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_out_bus_format) ||
	    hdmi_bus_fmt_is_yuv444(hdmi->hdmi_data.enc_out_bus_format) ||
	    hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format)) {
		switch (hdmi_bus_fmt_color_depth(
					hdmi->hdmi_data.enc_out_bus_format)) {
		case 8:
			color_depth = 0;
			output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_BYPASS;
			break;
		case 10:
			color_depth = 5;
			break;
		case 12:
			color_depth = 6;
			break;
		case 16:
			color_depth = 7;
			break;
		default:
			output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_BYPASS;
		}
	} else if (hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_out_bus_format)) {
		switch (hdmi_bus_fmt_color_depth(
					hdmi->hdmi_data.enc_out_bus_format)) {
		case 0:
		case 8:
			remap_size = HDMI_VP_REMAP_YCC422_16bit;
			break;
		case 10:
			remap_size = HDMI_VP_REMAP_YCC422_20bit;
			break;
		case 12:
			remap_size = HDMI_VP_REMAP_YCC422_24bit;
			break;

		default:
			return;
		}
		output_select = HDMI_VP_CONF_OUTPUT_SELECTOR_YCC422;
	} else {
		return;
	}

	/* set the packetizer registers */
	val = (color_depth << HDMI_VP_PR_CD_COLOR_DEPTH_OFFSET) &
	      HDMI_VP_PR_CD_COLOR_DEPTH_MASK;
	hdmi_writeb(hdmi, val, HDMI_VP_PR_CD);

	hdmi_modb(hdmi, HDMI_VP_STUFF_PR_STUFFING_STUFFING_MODE,
		  HDMI_VP_STUFF_PR_STUFFING_MASK, HDMI_VP_STUFF);

	/* Data from pixel repeater block */
	if (hdmi_data->pix_repet_factor > 0) {
		vp_conf = HDMI_VP_CONF_PR_EN_ENABLE |
			  HDMI_VP_CONF_BYPASS_SELECT_PIX_REPEATER;
	} else { /* data from packetizer block */
		vp_conf = HDMI_VP_CONF_PR_EN_DISABLE |
			  HDMI_VP_CONF_BYPASS_SELECT_VID_PACKETIZER;
	}

	hdmi_modb(hdmi, vp_conf,
		  HDMI_VP_CONF_PR_EN_MASK |
		  HDMI_VP_CONF_BYPASS_SELECT_MASK, HDMI_VP_CONF);

	if ((color_depth == 5 && hdmi->previous_mode.htotal % 4) ||
	    (color_depth == 6 && hdmi->previous_mode.htotal % 2))
		hdmi_modb(hdmi, 0, HDMI_VP_STUFF_IDEFAULT_PHASE_MASK,
			  HDMI_VP_STUFF);
	else
		hdmi_modb(hdmi, 1 << HDMI_VP_STUFF_IDEFAULT_PHASE_OFFSET,
			HDMI_VP_STUFF_IDEFAULT_PHASE_MASK, HDMI_VP_STUFF);

	hdmi_writeb(hdmi, remap_size, HDMI_VP_REMAP);

	if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_PP) {
		vp_conf = HDMI_VP_CONF_BYPASS_EN_DISABLE |
			  HDMI_VP_CONF_PP_EN_ENABLE |
			  HDMI_VP_CONF_YCC422_EN_DISABLE;
	} else if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_YCC422) {
		vp_conf = HDMI_VP_CONF_BYPASS_EN_DISABLE |
			  HDMI_VP_CONF_PP_EN_DISABLE |
			  HDMI_VP_CONF_YCC422_EN_ENABLE;
	} else if (output_select == HDMI_VP_CONF_OUTPUT_SELECTOR_BYPASS) {
		vp_conf = HDMI_VP_CONF_BYPASS_EN_ENABLE |
			  HDMI_VP_CONF_PP_EN_DISABLE |
			  HDMI_VP_CONF_YCC422_EN_DISABLE;
	} else {
		return;
	}

	hdmi_modb(hdmi, vp_conf,
		  HDMI_VP_CONF_BYPASS_EN_MASK | HDMI_VP_CONF_PP_EN_ENMASK |
		  HDMI_VP_CONF_YCC422_EN_MASK, HDMI_VP_CONF);

	hdmi_modb(hdmi, HDMI_VP_STUFF_PP_STUFFING_STUFFING_MODE |
			HDMI_VP_STUFF_YCC422_STUFFING_STUFFING_MODE,
		  HDMI_VP_STUFF_PP_STUFFING_MASK |
		  HDMI_VP_STUFF_YCC422_STUFFING_MASK, HDMI_VP_STUFF);

	hdmi_modb(hdmi, output_select, HDMI_VP_CONF_OUTPUT_SELECTOR_MASK,
		  HDMI_VP_CONF);
}

/* -----------------------------------------------------------------------------
 * Synopsys PHY Handling
 */

static inline void hdmi_phy_test_clear(struct dw_hdmi *hdmi,
				       unsigned char bit)
{
	hdmi_modb(hdmi, bit << HDMI_PHY_TST0_TSTCLR_OFFSET,
		  HDMI_PHY_TST0_TSTCLR_MASK, HDMI_PHY_TST0);
}

static inline void hdmi_phy_test_enable(struct dw_hdmi *hdmi,
					unsigned char bit)
{
	hdmi_modb(hdmi, bit << HDMI_PHY_TST0_TSTEN_OFFSET,
		  HDMI_PHY_TST0_TSTEN_MASK, HDMI_PHY_TST0);
}

static inline void hdmi_phy_test_clock(struct dw_hdmi *hdmi,
				       unsigned char bit)
{
	hdmi_modb(hdmi, bit << HDMI_PHY_TST0_TSTCLK_OFFSET,
		  HDMI_PHY_TST0_TSTCLK_MASK, HDMI_PHY_TST0);
}

static inline void hdmi_phy_test_din(struct dw_hdmi *hdmi,
				     unsigned char bit)
{
	hdmi_writeb(hdmi, bit, HDMI_PHY_TST1);
}

static inline void hdmi_phy_test_dout(struct dw_hdmi *hdmi,
				      unsigned char bit)
{
	hdmi_writeb(hdmi, bit, HDMI_PHY_TST2);
}

static bool hdmi_phy_wait_i2c_done(struct dw_hdmi *hdmi, int msec)
{
	u32 val;

	while ((val = hdmi_readb(hdmi, HDMI_IH_I2CMPHY_STAT0) & 0x3) == 0) {
		if (msec-- == 0)
			return false;
		udelay(1000);
	}
	hdmi_writeb(hdmi, val, HDMI_IH_I2CMPHY_STAT0);

	return true;
}

void dw_hdmi_phy_i2c_write(struct dw_hdmi *hdmi, unsigned short data,
			   unsigned char addr)
{
	hdmi_writeb(hdmi, 0xFF, HDMI_IH_I2CMPHY_STAT0);
	hdmi_writeb(hdmi, addr, HDMI_PHY_I2CM_ADDRESS_ADDR);
	hdmi_writeb(hdmi, (unsigned char)(data >> 8),
		    HDMI_PHY_I2CM_DATAO_1_ADDR);
	hdmi_writeb(hdmi, (unsigned char)(data >> 0),
		    HDMI_PHY_I2CM_DATAO_0_ADDR);
	hdmi_writeb(hdmi, HDMI_PHY_I2CM_OPERATION_ADDR_WRITE,
		    HDMI_PHY_I2CM_OPERATION_ADDR);
	hdmi_phy_wait_i2c_done(hdmi, 1000);
}
EXPORT_SYMBOL_GPL(dw_hdmi_phy_i2c_write);

static int hdmi_phy_i2c_read(struct dw_hdmi *hdmi, unsigned char addr)
{
	int val;

	hdmi_writeb(hdmi, 0xFF, HDMI_IH_I2CMPHY_STAT0);
	hdmi_writeb(hdmi, addr, HDMI_PHY_I2CM_ADDRESS_ADDR);
	hdmi_writeb(hdmi, 0, HDMI_PHY_I2CM_DATAI_1_ADDR);
	hdmi_writeb(hdmi, 0, HDMI_PHY_I2CM_DATAI_0_ADDR);
	hdmi_writeb(hdmi, HDMI_PHY_I2CM_OPERATION_ADDR_READ,
		    HDMI_PHY_I2CM_OPERATION_ADDR);
	hdmi_phy_wait_i2c_done(hdmi, 1000);
	val = hdmi_readb(hdmi, HDMI_PHY_I2CM_DATAI_1_ADDR);
	val = (val & 0xff) << 8;
	val += hdmi_readb(hdmi, HDMI_PHY_I2CM_DATAI_0_ADDR) & 0xff;
	return val;
}

static void dw_hdmi_phy_enable_powerdown(struct dw_hdmi *hdmi, bool enable)
{
	hdmi_mask_writeb(hdmi, !enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_PDZ_OFFSET,
			 HDMI_PHY_CONF0_PDZ_MASK);
}

static void dw_hdmi_phy_enable_tmds(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_ENTMDS_OFFSET,
			 HDMI_PHY_CONF0_ENTMDS_MASK);
}

static void dw_hdmi_phy_enable_svsret(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_SVSRET_OFFSET,
			 HDMI_PHY_CONF0_SVSRET_MASK);
}

static void dw_hdmi_phy_gen2_pddq(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_GEN2_PDDQ_OFFSET,
			 HDMI_PHY_CONF0_GEN2_PDDQ_MASK);
}

static void dw_hdmi_phy_gen2_txpwron(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_GEN2_TXPWRON_OFFSET,
			 HDMI_PHY_CONF0_GEN2_TXPWRON_MASK);
}

static void dw_hdmi_phy_sel_data_en_pol(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_SELDATAENPOL_OFFSET,
			 HDMI_PHY_CONF0_SELDATAENPOL_MASK);
}

static void dw_hdmi_phy_sel_interface_control(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_SELDIPIF_OFFSET,
			 HDMI_PHY_CONF0_SELDIPIF_MASK);
}

static void dw_hdmi_phy_power_off(struct dw_hdmi *hdmi)
{
	const struct dw_hdmi_phy_data *phy = hdmi->phy.data;
	unsigned int i;
	u16 val;

	if (phy->gen == 1) {
		dw_hdmi_phy_enable_tmds(hdmi, 0);
		dw_hdmi_phy_enable_powerdown(hdmi, true);
		return;
	}

	dw_hdmi_phy_gen2_txpwron(hdmi, 0);

	/*
	 * Wait for TX_PHY_LOCK to be deasserted to indicate that the PHY went
	 * to low power mode.
	 */
	for (i = 0; i < 5; ++i) {
		val = hdmi_readb(hdmi, HDMI_PHY_STAT0);
		if (!(val & HDMI_PHY_TX_PHY_LOCK))
			break;

		usleep_range(1000, 2000);
	}

	if (val & HDMI_PHY_TX_PHY_LOCK)
		dev_warn(hdmi->dev, "PHY failed to power down\n");
	else
		dev_dbg(hdmi->dev, "PHY powered down in %u iterations\n", i);

	dw_hdmi_phy_gen2_pddq(hdmi, 1);
}

static int dw_hdmi_phy_power_on(struct dw_hdmi *hdmi)
{
	const struct dw_hdmi_phy_data *phy = hdmi->phy.data;
	unsigned int i;
	u8 val;

	if (phy->gen == 1) {
		dw_hdmi_phy_enable_powerdown(hdmi, false);

		/* Toggle TMDS enable. */
		dw_hdmi_phy_enable_tmds(hdmi, 0);
		dw_hdmi_phy_enable_tmds(hdmi, 1);
		return 0;
	}

	dw_hdmi_phy_gen2_txpwron(hdmi, 1);
	dw_hdmi_phy_gen2_pddq(hdmi, 0);

	/* Wait for PHY PLL lock */
	for (i = 0; i < 5; ++i) {
		val = hdmi_readb(hdmi, HDMI_PHY_STAT0) & HDMI_PHY_TX_PHY_LOCK;
		if (val)
			break;

		usleep_range(1000, 2000);
	}

	if (!val) {
		dev_err(hdmi->dev, "PHY PLL failed to lock\n");
		return -ETIMEDOUT;
	}

	dev_dbg(hdmi->dev, "PHY PLL locked %u iterations\n", i);
	return 0;
}

/*
 * PHY configuration function for the DWC HDMI 3D TX PHY. Based on the available
 * information the DWC MHL PHY has the same register layout and is thus also
 * supported by this function.
 */
static int hdmi_phy_configure_dwc_hdmi_3d_tx(struct dw_hdmi *hdmi,
		const struct dw_hdmi_plat_data *pdata,
		unsigned long mpixelclock)
{
	const struct dw_hdmi_mpll_config *mpll_config = pdata->mpll_cfg;
	const struct dw_hdmi_curr_ctrl *curr_ctrl = pdata->cur_ctr;
	const struct dw_hdmi_phy_config *phy_config = pdata->phy_config;
	unsigned int tmdsclock = hdmi->hdmi_data.video_mode.mtmdsclock;
	unsigned int depth =
		hdmi_bus_fmt_color_depth(hdmi->hdmi_data.enc_out_bus_format);

	if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format) &&
	    pdata->mpll_cfg_420)
		mpll_config = pdata->mpll_cfg_420;

	/* PLL/MPLL Cfg - always match on final entry */
	for (; mpll_config->mpixelclock != ~0UL; mpll_config++)
		if (mpixelclock <= mpll_config->mpixelclock)
			break;

	for (; curr_ctrl->mpixelclock != ~0UL; curr_ctrl++)
		if (tmdsclock <= curr_ctrl->mpixelclock)
			break;

	for (; phy_config->mpixelclock != ~0UL; phy_config++)
		if (tmdsclock <= phy_config->mpixelclock)
			break;

	if (mpll_config->mpixelclock == ~0UL ||
	    curr_ctrl->mpixelclock == ~0UL ||
	    phy_config->mpixelclock == ~0UL)
		return -EINVAL;

	if (!hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_out_bus_format))
		depth = fls(depth - 8);
	else
		depth = 0;
	if (depth)
		depth--;

	dw_hdmi_phy_i2c_write(hdmi, mpll_config->res[depth].cpce,
			      HDMI_3D_TX_PHY_CPCE_CTRL);
	dw_hdmi_phy_i2c_write(hdmi, mpll_config->res[depth].gmp,
			      HDMI_3D_TX_PHY_GMPCTRL);
	dw_hdmi_phy_i2c_write(hdmi, curr_ctrl->curr[depth],
			      HDMI_3D_TX_PHY_CURRCTRL);

	dw_hdmi_phy_i2c_write(hdmi, 0, HDMI_3D_TX_PHY_PLLPHBYCTRL);
	dw_hdmi_phy_i2c_write(hdmi, HDMI_3D_TX_PHY_MSM_CTRL_CKO_SEL_FB_CLK,
			      HDMI_3D_TX_PHY_MSM_CTRL);

	dw_hdmi_phy_i2c_write(hdmi, phy_config->term, HDMI_3D_TX_PHY_TXTERM);
	dw_hdmi_phy_i2c_write(hdmi, phy_config->sym_ctr,
			      HDMI_3D_TX_PHY_CKSYMTXCTRL);
	dw_hdmi_phy_i2c_write(hdmi, phy_config->vlev_ctr,
			      HDMI_3D_TX_PHY_VLEVCTRL);

	/* Override and disable clock termination. */
	dw_hdmi_phy_i2c_write(hdmi, HDMI_3D_TX_PHY_CKCALCTRL_OVERRIDE,
			      HDMI_3D_TX_PHY_CKCALCTRL);
	return 0;
}

static int hdmi_phy_configure(struct dw_hdmi *hdmi)
{
	const struct dw_hdmi_phy_data *phy = hdmi->phy.data;
	const struct dw_hdmi_plat_data *pdata = hdmi->plat_data;
	unsigned long mpixelclock = hdmi->hdmi_data.video_mode.mpixelclock;
	unsigned long mtmdsclock = hdmi->hdmi_data.video_mode.mtmdsclock;
	int ret;

	dw_hdmi_phy_power_off(hdmi);

	/* Control for TMDS Bit Period/TMDS Clock-Period Ratio */
	if (hdmi->connector.display_info.hdmi.scdc.supported) {
		if (mtmdsclock > 340000000)
			drm_scdc_set_high_tmds_clock_ratio(hdmi->ddc, 1);
		else
			drm_scdc_set_high_tmds_clock_ratio(hdmi->ddc, 0);
	}

	/* Leave low power consumption mode by asserting SVSRET. */
	if (phy->has_svsret)
		dw_hdmi_phy_enable_svsret(hdmi, 1);

	/* PHY reset. The reset signal is active high on Gen2 PHYs. */
	hdmi_writeb(hdmi, HDMI_MC_PHYRSTZ_PHYRSTZ, HDMI_MC_PHYRSTZ);
	hdmi_writeb(hdmi, 0, HDMI_MC_PHYRSTZ);

	hdmi_writeb(hdmi, HDMI_MC_HEACPHY_RST_ASSERT, HDMI_MC_HEACPHY_RST);

	hdmi_phy_test_clear(hdmi, 1);
	hdmi_writeb(hdmi, HDMI_PHY_I2CM_SLAVE_ADDR_PHY_GEN2,
		    HDMI_PHY_I2CM_SLAVE_ADDR);
	hdmi_phy_test_clear(hdmi, 0);

	/* Write to the PHY as configured by the platform */
	if (pdata->configure_phy)
		ret = pdata->configure_phy(hdmi, pdata, mpixelclock);
	else
		ret = phy->configure(hdmi, pdata, mpixelclock);
	if (ret) {
		dev_err(hdmi->dev, "PHY configuration failed (clock %lu)\n",
			mpixelclock);
		return ret;
	}

	/* Wait for resuming transmission of TMDS clock and data */
	if (mtmdsclock > 340000000)
		msleep(100);

	return dw_hdmi_phy_power_on(hdmi);
}

static int dw_hdmi_phy_init(struct dw_hdmi *hdmi, void *data,
			    struct drm_display_mode *mode)
{
	int i, ret;

	/* HDMI Phy spec says to do the phy initialization sequence twice */
	for (i = 0; i < 2; i++) {
		dw_hdmi_phy_sel_data_en_pol(hdmi, 1);
		dw_hdmi_phy_sel_interface_control(hdmi, 0);

		ret = hdmi_phy_configure(hdmi);
		if (ret)
			return ret;
	}

	return 0;
}

static void dw_hdmi_phy_disable(struct dw_hdmi *hdmi, void *data)
{
	dw_hdmi_phy_power_off(hdmi);
}

enum drm_connector_status dw_hdmi_phy_read_hpd(struct dw_hdmi *hdmi,
						      void *data)
{
	return hdmi_readb(hdmi, HDMI_PHY_STAT0) & HDMI_PHY_HPD ?
		connector_status_connected : connector_status_disconnected;
}
EXPORT_SYMBOL_GPL(dw_hdmi_phy_read_hpd);

static const struct dw_hdmi_phy_ops dw_hdmi_synopsys_phy_ops = {
	.init = dw_hdmi_phy_init,
	.disable = dw_hdmi_phy_disable,
	.read_hpd = dw_hdmi_phy_read_hpd,
};

/* -----------------------------------------------------------------------------
 * HDMI TX Setup
 */

static void hdmi_tx_hdcp_config(struct dw_hdmi *hdmi,
				const struct drm_display_mode *mode)
{
	struct hdmi_vmode *vmode = &hdmi->hdmi_data.video_mode;
	u8 vsync_pol, hsync_pol, data_pol, hdmi_dvi;

	/* Configure the video polarity */
	vsync_pol = mode->flags & DRM_MODE_FLAG_PVSYNC ?
		    HDMI_A_VIDPOLCFG_VSYNCPOL_ACTIVE_HIGH :
		    HDMI_A_VIDPOLCFG_VSYNCPOL_ACTIVE_LOW;
	hsync_pol = mode->flags & DRM_MODE_FLAG_PHSYNC ?
		    HDMI_A_VIDPOLCFG_HSYNCPOL_ACTIVE_HIGH :
		    HDMI_A_VIDPOLCFG_HSYNCPOL_ACTIVE_LOW;
	data_pol = vmode->mdataenablepolarity ?
		    HDMI_A_VIDPOLCFG_DATAENPOL_ACTIVE_HIGH :
		    HDMI_A_VIDPOLCFG_DATAENPOL_ACTIVE_LOW;
	hdmi_modb(hdmi, vsync_pol | hsync_pol | data_pol,
		  HDMI_A_VIDPOLCFG_VSYNCPOL_MASK |
		  HDMI_A_VIDPOLCFG_HSYNCPOL_MASK |
		  HDMI_A_VIDPOLCFG_DATAENPOL_MASK,
		  HDMI_A_VIDPOLCFG);

	/* Config the display mode */
	hdmi_dvi = hdmi->sink_is_hdmi ? HDMI_A_HDCPCFG0_HDMIDVI_HDMI :
		   HDMI_A_HDCPCFG0_HDMIDVI_DVI;
	hdmi_modb(hdmi, hdmi_dvi, HDMI_A_HDCPCFG0_HDMIDVI_MASK,
		  HDMI_A_HDCPCFG0);

	if (hdmi->hdcp && hdmi->hdcp->hdcp_start)
		hdmi->hdcp->hdcp_start(hdmi->hdcp);
}

static void hdmi_config_AVI(struct dw_hdmi *hdmi, struct drm_display_mode *mode)
{
	struct hdmi_avi_infoframe frame;
	u8 val;
	bool is_hdmi2 = false;

	if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format) ||
	    hdmi->connector.display_info.hdmi.scdc.supported)
		is_hdmi2 = true;
	/* Initialise info frame from DRM mode */
	drm_hdmi_avi_infoframe_from_display_mode(&frame, mode, is_hdmi2);

	if (hdmi_bus_fmt_is_yuv444(hdmi->hdmi_data.enc_out_bus_format))
		frame.colorspace = HDMI_COLORSPACE_YUV444;
	else if (hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_out_bus_format))
		frame.colorspace = HDMI_COLORSPACE_YUV422;
	else if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format))
		frame.colorspace = HDMI_COLORSPACE_YUV420;
	else
		frame.colorspace = HDMI_COLORSPACE_RGB;

	/* Set up colorimetry */
	switch (hdmi->hdmi_data.enc_out_encoding) {
	case V4L2_YCBCR_ENC_601:
		if (hdmi->hdmi_data.enc_in_encoding == V4L2_YCBCR_ENC_XV601)
			frame.colorimetry = HDMI_COLORIMETRY_EXTENDED;
		else
			frame.colorimetry = HDMI_COLORIMETRY_ITU_601;
		frame.extended_colorimetry =
				HDMI_EXTENDED_COLORIMETRY_XV_YCC_601;
		break;
	case V4L2_YCBCR_ENC_709:
		if (hdmi->hdmi_data.enc_in_encoding == V4L2_YCBCR_ENC_XV709)
			frame.colorimetry = HDMI_COLORIMETRY_EXTENDED;
		else
			frame.colorimetry = HDMI_COLORIMETRY_ITU_709;
		frame.extended_colorimetry =
				HDMI_EXTENDED_COLORIMETRY_XV_YCC_709;
		break;
	case V4L2_YCBCR_ENC_BT2020:
		if (hdmi->hdmi_data.enc_in_encoding == V4L2_YCBCR_ENC_BT2020)
			frame.colorimetry = HDMI_COLORIMETRY_EXTENDED;
		else
			frame.colorimetry = HDMI_COLORIMETRY_ITU_709;
		frame.extended_colorimetry =
				HDMI_EXTENDED_COLORIMETRY_BT2020;
		break;
	default: /* Carries no data */
		frame.colorimetry = HDMI_COLORIMETRY_ITU_601;
		frame.extended_colorimetry =
				HDMI_EXTENDED_COLORIMETRY_XV_YCC_601;
		break;
	}

	frame.scan_mode = HDMI_SCAN_MODE_NONE;

	/*
	 * The Designware IP uses a different byte format from standard
	 * AVI info frames, though generally the bits are in the correct
	 * bytes.
	 */

	/*
	 * AVI data byte 1 differences: Colorspace in bits 0,1,7 rather than
	 * 5,6,7, active aspect present in bit 6 rather than 4.
	 */
	val = (frame.scan_mode & 3) << 4 | (frame.colorspace & 0x3);
	if (frame.active_aspect & 15)
		val |= HDMI_FC_AVICONF0_ACTIVE_FMT_INFO_PRESENT;
	if (frame.top_bar || frame.bottom_bar)
		val |= HDMI_FC_AVICONF0_BAR_DATA_HORIZ_BAR;
	if (frame.left_bar || frame.right_bar)
		val |= HDMI_FC_AVICONF0_BAR_DATA_VERT_BAR;
	hdmi_writeb(hdmi, val, HDMI_FC_AVICONF0);

	/* AVI data byte 2 differences: none */
	val = ((frame.colorimetry & 0x3) << 6) |
	      ((frame.picture_aspect & 0x3) << 4) |
	      (frame.active_aspect & 0xf);
	hdmi_writeb(hdmi, val, HDMI_FC_AVICONF1);

	/* AVI data byte 3 differences: none */
	val = ((frame.extended_colorimetry & 0x7) << 4) |
	      ((frame.quantization_range & 0x3) << 2) |
	      (frame.nups & 0x3);
	if (frame.itc)
		val |= HDMI_FC_AVICONF2_IT_CONTENT_VALID;
	hdmi_writeb(hdmi, val, HDMI_FC_AVICONF2);

	/* AVI data byte 4 differences: none */
	val = frame.video_code & 0x7f;
	hdmi_writeb(hdmi, val, HDMI_FC_AVIVID);

	/* AVI Data Byte 5- set up input and output pixel repetition */
	val = (((hdmi->hdmi_data.video_mode.mpixelrepetitioninput + 1) <<
		HDMI_FC_PRCONF_INCOMING_PR_FACTOR_OFFSET) &
		HDMI_FC_PRCONF_INCOMING_PR_FACTOR_MASK) |
		((hdmi->hdmi_data.video_mode.mpixelrepetitionoutput <<
		HDMI_FC_PRCONF_OUTPUT_PR_FACTOR_OFFSET) &
		HDMI_FC_PRCONF_OUTPUT_PR_FACTOR_MASK);
	hdmi_writeb(hdmi, val, HDMI_FC_PRCONF);

	/*
	 * AVI data byte 5 differences: content type in 0,1 rather than 4,5,
	 * ycc range in bits 2,3 rather than 6,7
	 */
	val = ((frame.ycc_quantization_range & 0x3) << 2) |
	      (frame.content_type & 0x3);
	hdmi_writeb(hdmi, val, HDMI_FC_AVICONF3);

	/* AVI Data Bytes 6-13 */
	hdmi_writeb(hdmi, frame.top_bar & 0xff, HDMI_FC_AVIETB0);
	hdmi_writeb(hdmi, (frame.top_bar >> 8) & 0xff, HDMI_FC_AVIETB1);
	hdmi_writeb(hdmi, frame.bottom_bar & 0xff, HDMI_FC_AVISBB0);
	hdmi_writeb(hdmi, (frame.bottom_bar >> 8) & 0xff, HDMI_FC_AVISBB1);
	hdmi_writeb(hdmi, frame.left_bar & 0xff, HDMI_FC_AVIELB0);
	hdmi_writeb(hdmi, (frame.left_bar >> 8) & 0xff, HDMI_FC_AVIELB1);
	hdmi_writeb(hdmi, frame.right_bar & 0xff, HDMI_FC_AVISRB0);
	hdmi_writeb(hdmi, (frame.right_bar >> 8) & 0xff, HDMI_FC_AVISRB1);
}

static void hdmi_config_vendor_specific_infoframe(struct dw_hdmi *hdmi,
						 struct drm_display_mode *mode)
{
	struct hdmi_vendor_infoframe frame;
	u8 buffer[10];
	ssize_t err;

	/* Disable HDMI vendor specific infoframe send */
	hdmi_mask_writeb(hdmi, 0, HDMI_FC_DATAUTO0, HDMI_FC_DATAUTO0_VSD_OFFSET,
			HDMI_FC_DATAUTO0_VSD_MASK);

	err = drm_hdmi_vendor_infoframe_from_display_mode(&frame, mode);
	if (err < 0)
		/*
		 * Going into that statement does not means vendor infoframe
		 * fails. It just informed us that vendor infoframe is not
		 * needed for the selected mode. Only 4k or stereoscopic 3D
		 * mode requires vendor infoframe. So just simply return.
		 */
		return;

	err = hdmi_vendor_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "Failed to pack vendor infoframe: %zd\n",
			err);
		return;
	}

	/* Set the length of HDMI vendor specific InfoFrame payload */
	hdmi_writeb(hdmi, buffer[2], HDMI_FC_VSDSIZE);

	/* Set 24bit IEEE Registration Identifier */
	hdmi_writeb(hdmi, buffer[4], HDMI_FC_VSDIEEEID0);
	hdmi_writeb(hdmi, buffer[5], HDMI_FC_VSDIEEEID1);
	hdmi_writeb(hdmi, buffer[6], HDMI_FC_VSDIEEEID2);

	/* Set HDMI_Video_Format and HDMI_VIC/3D_Structure */
	hdmi_writeb(hdmi, buffer[7], HDMI_FC_VSDPAYLOAD0);
	hdmi_writeb(hdmi, buffer[8], HDMI_FC_VSDPAYLOAD1);

	if (frame.s3d_struct >= HDMI_3D_STRUCTURE_SIDE_BY_SIDE_HALF)
		hdmi_writeb(hdmi, buffer[9], HDMI_FC_VSDPAYLOAD2);

	/* Packet frame interpolation */
	hdmi_writeb(hdmi, 1, HDMI_FC_DATAUTO1);

	/* Auto packets per frame and line spacing */
	hdmi_writeb(hdmi, 0x11, HDMI_FC_DATAUTO2);

	/* Configures the Frame Composer On RDRB mode */
	hdmi_mask_writeb(hdmi, 1, HDMI_FC_DATAUTO0, HDMI_FC_DATAUTO0_VSD_OFFSET,
			HDMI_FC_DATAUTO0_VSD_MASK);
}

#define HDR_LSB(n) ((n) & 0xff)
#define HDR_MSB(n) (((n) & 0xff00) >> 8)

/* Set Dynamic Range and Mastering Infoframe */
static void hdmi_config_hdr_infoframe(struct dw_hdmi *hdmi)
{
	struct hdmi_drm_infoframe frame;
	struct hdr_static_metadata *hdr_metadata;
	struct drm_connector_state *conn_state = hdmi->connector.state;
	struct drm_hdmi_info *hdmi_info = &hdmi->connector.display_info.hdmi;
	int ret;

	/* Dynamic Range and Mastering Infoframe is introduced in v2.11a. */
	if (hdmi->version < 0x211a) {
		DRM_ERROR("Not support DRM Infoframe\n");
		return;
	}

	hdmi_modb(hdmi, HDMI_FC_PACKET_DRM_TX_DEN,
		  HDMI_FC_PACKET_DRM_TX_EN_MASK, HDMI_FC_PACKET_TX_EN);

	if (!hdmi_info->hdr_panel_metadata.eotf) {
		DRM_DEBUG("No need to set HDR metadata in infoframe\n");
		return;
	}

	if (!conn_state->hdr_source_metadata_blob_ptr) {
		DRM_DEBUG("source metadata not set yet\n");
		return;
	}

	hdr_metadata = (struct hdr_static_metadata *)
		conn_state->hdr_source_metadata_blob_ptr->data;

	if (!(hdmi_info->hdr_panel_metadata.eotf & BIT(hdr_metadata->eotf))) {
		DRM_ERROR("Not support EOTF %d\n", hdr_metadata->eotf);
		return;
	}

	ret = drm_hdmi_infoframe_set_hdr_metadata(&frame, hdr_metadata);
	if (ret < 0) {
		DRM_ERROR("couldn't set HDR metadata in infoframe\n");
		return;
	}

	hdmi_writeb(hdmi, 1, HDMI_FC_DRM_HB0);
	hdmi_writeb(hdmi, frame.length, HDMI_FC_DRM_HB1);
	hdmi_writeb(hdmi, frame.eotf, HDMI_FC_DRM_PB0);
	hdmi_writeb(hdmi, frame.metadata_type, HDMI_FC_DRM_PB1);
	hdmi_writeb(hdmi, HDR_LSB(frame.display_primaries_x[0]),
		    HDMI_FC_DRM_PB2);
	hdmi_writeb(hdmi, HDR_MSB(frame.display_primaries_x[0]),
		    HDMI_FC_DRM_PB3);
	hdmi_writeb(hdmi, HDR_LSB(frame.display_primaries_y[0]),
		    HDMI_FC_DRM_PB4);
	hdmi_writeb(hdmi, HDR_MSB(frame.display_primaries_y[0]),
		    HDMI_FC_DRM_PB5);
	hdmi_writeb(hdmi, HDR_LSB(frame.display_primaries_x[1]),
		    HDMI_FC_DRM_PB6);
	hdmi_writeb(hdmi, HDR_MSB(frame.display_primaries_x[1]),
		    HDMI_FC_DRM_PB7);
	hdmi_writeb(hdmi, HDR_LSB(frame.display_primaries_y[1]),
		    HDMI_FC_DRM_PB8);
	hdmi_writeb(hdmi, HDR_MSB(frame.display_primaries_y[1]),
		    HDMI_FC_DRM_PB9);
	hdmi_writeb(hdmi, HDR_LSB(frame.display_primaries_x[2]),
		    HDMI_FC_DRM_PB10);
	hdmi_writeb(hdmi, HDR_MSB(frame.display_primaries_x[2]),
		    HDMI_FC_DRM_PB11);
	hdmi_writeb(hdmi, HDR_LSB(frame.display_primaries_y[2]),
		    HDMI_FC_DRM_PB12);
	hdmi_writeb(hdmi, HDR_MSB(frame.display_primaries_y[2]),
		    HDMI_FC_DRM_PB13);
	hdmi_writeb(hdmi, HDR_LSB(frame.white_point_x), HDMI_FC_DRM_PB14);
	hdmi_writeb(hdmi, HDR_MSB(frame.white_point_x), HDMI_FC_DRM_PB15);
	hdmi_writeb(hdmi, HDR_LSB(frame.white_point_y), HDMI_FC_DRM_PB16);
	hdmi_writeb(hdmi, HDR_MSB(frame.white_point_y), HDMI_FC_DRM_PB17);
	hdmi_writeb(hdmi, HDR_LSB(frame.max_mastering_display_luminance),
		    HDMI_FC_DRM_PB18);
	hdmi_writeb(hdmi, HDR_MSB(frame.max_mastering_display_luminance),
		    HDMI_FC_DRM_PB19);
	hdmi_writeb(hdmi, HDR_LSB(frame.min_mastering_display_luminance),
		    HDMI_FC_DRM_PB20);
	hdmi_writeb(hdmi, HDR_MSB(frame.min_mastering_display_luminance),
		    HDMI_FC_DRM_PB21);
	hdmi_writeb(hdmi, HDR_LSB(frame.max_cll), HDMI_FC_DRM_PB22);
	hdmi_writeb(hdmi, HDR_MSB(frame.max_cll), HDMI_FC_DRM_PB23);
	hdmi_writeb(hdmi, HDR_LSB(frame.max_fall), HDMI_FC_DRM_PB24);
	hdmi_writeb(hdmi, HDR_MSB(frame.max_fall), HDMI_FC_DRM_PB25);
	hdmi_writeb(hdmi, 1, HDMI_FC_DRM_UP);
	hdmi_modb(hdmi, HDMI_FC_PACKET_DRM_TX_EN,
		  HDMI_FC_PACKET_DRM_TX_EN_MASK, HDMI_FC_PACKET_TX_EN);

	if (conn_state->hdr_metadata_changed)
		conn_state->hdr_metadata_changed = false;

	DRM_DEBUG("%s eotf %d end\n", __func__, hdr_metadata->eotf);
}

static unsigned int
hdmi_get_tmdsclock(struct dw_hdmi *hdmi, unsigned long mpixelclock)
{
	unsigned int tmdsclock = mpixelclock;
	unsigned int depth =
		hdmi_bus_fmt_color_depth(hdmi->hdmi_data.enc_out_bus_format);

	if (!hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_out_bus_format)) {
		switch (depth) {
		case 16:
			tmdsclock = mpixelclock * 2;
			break;
		case 12:
			tmdsclock = mpixelclock * 3 / 2;
			break;
		case 10:
			tmdsclock = mpixelclock * 5 / 4;
			break;
		default:
			break;
		}
	}

	return tmdsclock;
}

static void hdmi_av_composer(struct dw_hdmi *hdmi,
			     const struct drm_display_mode *mode)
{
	u8 inv_val, bytes;
	struct drm_hdmi_info *hdmi_info = &hdmi->connector.display_info.hdmi;
	struct hdmi_vmode *vmode = &hdmi->hdmi_data.video_mode;
	int hblank, vblank, h_de_hs, v_de_vs, hsync_len, vsync_len;
	unsigned int hdisplay, vdisplay;

	vmode->previous_pixelclock = vmode->mpixelclock;
	vmode->mpixelclock = mode->crtc_clock * 1000;
	if ((mode->flags & DRM_MODE_FLAG_3D_MASK) ==
		DRM_MODE_FLAG_3D_FRAME_PACKING)
		vmode->mpixelclock *= 2;
	dev_dbg(hdmi->dev, "final pixclk = %d\n", vmode->mpixelclock);
	vmode->previous_tmdsclock = vmode->mtmdsclock;
	vmode->mtmdsclock = hdmi_get_tmdsclock(hdmi, vmode->mpixelclock);
	if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format))
		vmode->mtmdsclock /= 2;
	dev_dbg(hdmi->dev, "final tmdsclk = %d\n", vmode->mtmdsclock);

	/* Set up HDMI_FC_INVIDCONF
	 * fc_invidconf.HDCP_keepout must be set (1'b1)
	 * when activate the scrambler feature.
	 */
	inv_val = (vmode->mtmdsclock > 340000000 ||
		   hdmi_info->scdc.scrambling.low_rates ?
		   HDMI_FC_INVIDCONF_HDCP_KEEPOUT_ACTIVE :
		   HDMI_FC_INVIDCONF_HDCP_KEEPOUT_INACTIVE);

	inv_val |= mode->flags & DRM_MODE_FLAG_PVSYNC ?
		HDMI_FC_INVIDCONF_VSYNC_IN_POLARITY_ACTIVE_HIGH :
		HDMI_FC_INVIDCONF_VSYNC_IN_POLARITY_ACTIVE_LOW;

	inv_val |= mode->flags & DRM_MODE_FLAG_PHSYNC ?
		HDMI_FC_INVIDCONF_HSYNC_IN_POLARITY_ACTIVE_HIGH :
		HDMI_FC_INVIDCONF_HSYNC_IN_POLARITY_ACTIVE_LOW;

	inv_val |= (vmode->mdataenablepolarity ?
		HDMI_FC_INVIDCONF_DE_IN_POLARITY_ACTIVE_HIGH :
		HDMI_FC_INVIDCONF_DE_IN_POLARITY_ACTIVE_LOW);

	if (hdmi->vic == 39)
		inv_val |= HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC_ACTIVE_HIGH;
	else
		inv_val |= mode->flags & DRM_MODE_FLAG_INTERLACE ?
			HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC_ACTIVE_HIGH :
			HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC_ACTIVE_LOW;

	inv_val |= mode->flags & DRM_MODE_FLAG_INTERLACE ?
		HDMI_FC_INVIDCONF_IN_I_P_INTERLACED :
		HDMI_FC_INVIDCONF_IN_I_P_PROGRESSIVE;

	inv_val |= hdmi->sink_is_hdmi ?
		HDMI_FC_INVIDCONF_DVI_MODEZ_HDMI_MODE :
		HDMI_FC_INVIDCONF_DVI_MODEZ_DVI_MODE;

	hdmi_writeb(hdmi, inv_val, HDMI_FC_INVIDCONF);

	hdisplay = mode->hdisplay;
	hblank = mode->htotal - mode->hdisplay;
	h_de_hs = mode->hsync_start - mode->hdisplay;
	hsync_len = mode->hsync_end - mode->hsync_start;

	/*
	 * When we're setting a YCbCr420 mode, we need
	 * to adjust the horizontal timing to suit.
	 */
	if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format)) {
		hdisplay /= 2;
		hblank /= 2;
		h_de_hs /= 2;
		hsync_len /= 2;
	}

	vdisplay = mode->vdisplay;
	vblank = mode->vtotal - mode->vdisplay;
	v_de_vs = mode->vsync_start - mode->vdisplay;
	vsync_len = mode->vsync_end - mode->vsync_start;

	/*
	 * When we're setting an interlaced mode, we need
	 * to adjust the vertical timing to suit.
	 */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		vdisplay /= 2;
		vblank /= 2;
		v_de_vs /= 2;
		vsync_len /= 2;
	} else if ((mode->flags & DRM_MODE_FLAG_3D_MASK) ==
		DRM_MODE_FLAG_3D_FRAME_PACKING) {
		vdisplay += mode->vtotal;
	}

	/* Scrambling Control */
	if (hdmi_info->scdc.supported) {
		if (vmode->mtmdsclock > 340000000 ||
		    hdmi_info->scdc.scrambling.low_rates) {
			drm_scdc_readb(&hdmi->i2c->adap, SCDC_SINK_VERSION,
				       &bytes);
			drm_scdc_writeb(&hdmi->i2c->adap, SCDC_SOURCE_VERSION,
					bytes);
			drm_scdc_set_scrambling(&hdmi->i2c->adap, 1);
			hdmi_writeb(hdmi, (u8)~HDMI_MC_SWRSTZ_TMDSSWRST_REQ,
				    HDMI_MC_SWRSTZ);
			hdmi_writeb(hdmi, 1, HDMI_FC_SCRAMBLER_CTRL);
		} else {
			hdmi_writeb(hdmi, 0, HDMI_FC_SCRAMBLER_CTRL);
			hdmi_writeb(hdmi, (u8)~HDMI_MC_SWRSTZ_TMDSSWRST_REQ,
				    HDMI_MC_SWRSTZ);
			drm_scdc_set_scrambling(&hdmi->i2c->adap, 0);
		}
	}

	/* Set up horizontal active pixel width */
	hdmi_writeb(hdmi, hdisplay >> 8, HDMI_FC_INHACTV1);
	hdmi_writeb(hdmi, hdisplay, HDMI_FC_INHACTV0);

	/* Set up vertical active lines */
	hdmi_writeb(hdmi, vdisplay >> 8, HDMI_FC_INVACTV1);
	hdmi_writeb(hdmi, vdisplay, HDMI_FC_INVACTV0);

	/* Set up horizontal blanking pixel region width */
	hdmi_writeb(hdmi, hblank >> 8, HDMI_FC_INHBLANK1);
	hdmi_writeb(hdmi, hblank, HDMI_FC_INHBLANK0);

	/* Set up vertical blanking pixel region width */
	hdmi_writeb(hdmi, vblank, HDMI_FC_INVBLANK);

	/* Set up HSYNC active edge delay width (in pixel clks) */
	hdmi_writeb(hdmi, h_de_hs >> 8, HDMI_FC_HSYNCINDELAY1);
	hdmi_writeb(hdmi, h_de_hs, HDMI_FC_HSYNCINDELAY0);

	/* Set up VSYNC active edge delay (in lines) */
	hdmi_writeb(hdmi, v_de_vs, HDMI_FC_VSYNCINDELAY);

	/* Set up HSYNC active pulse width (in pixel clks) */
	hdmi_writeb(hdmi, hsync_len >> 8, HDMI_FC_HSYNCINWIDTH1);
	hdmi_writeb(hdmi, hsync_len, HDMI_FC_HSYNCINWIDTH0);

	/* Set up VSYNC active edge delay (in lines) */
	hdmi_writeb(hdmi, vsync_len, HDMI_FC_VSYNCINWIDTH);
}

/* HDMI Initialization Step B.4 */
static void dw_hdmi_enable_video_path(struct dw_hdmi *hdmi)
{
	u8 clkdis;

	/* control period minimum duration */
	hdmi_writeb(hdmi, 12, HDMI_FC_CTRLDUR);
	hdmi_writeb(hdmi, 32, HDMI_FC_EXCTRLDUR);
	hdmi_writeb(hdmi, 1, HDMI_FC_EXCTRLSPAC);

	/* Set to fill TMDS data channels */
	hdmi_writeb(hdmi, 0x0B, HDMI_FC_CH0PREAM);
	hdmi_writeb(hdmi, 0x16, HDMI_FC_CH1PREAM);
	hdmi_writeb(hdmi, 0x21, HDMI_FC_CH2PREAM);

	/* Enable pixel clock and tmds data path */
	clkdis = 0x7F;
	clkdis &= ~HDMI_MC_CLKDIS_PIXELCLK_DISABLE;
	hdmi_writeb(hdmi, clkdis, HDMI_MC_CLKDIS);

	clkdis &= ~HDMI_MC_CLKDIS_TMDSCLK_DISABLE;
	hdmi_writeb(hdmi, clkdis, HDMI_MC_CLKDIS);

	/* Enable csc path */
	if (is_color_space_conversion(hdmi)) {
		clkdis &= ~HDMI_MC_CLKDIS_CSCCLK_DISABLE;
		hdmi_writeb(hdmi, clkdis, HDMI_MC_CLKDIS);
	}

	/* Enable pixel repetition path */
	if (hdmi->hdmi_data.video_mode.mpixelrepetitioninput) {
		clkdis &= ~HDMI_MC_CLKDIS_PREPCLK_DISABLE;
		hdmi_writeb(hdmi, clkdis, HDMI_MC_CLKDIS);
	}

	/* Enable color space conversion if needed */
	if (is_color_space_conversion(hdmi))
		hdmi_writeb(hdmi, HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_IN_PATH,
			    HDMI_MC_FLOWCTRL);
	else
		hdmi_writeb(hdmi, HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_BYPASS,
			    HDMI_MC_FLOWCTRL);
}

static void hdmi_enable_audio_clk(struct dw_hdmi *hdmi)
{
	hdmi_modb(hdmi, 0, HDMI_MC_CLKDIS_AUDCLK_DISABLE, HDMI_MC_CLKDIS);
}

/* Workaround to clear the overflow condition */
static void dw_hdmi_clear_overflow(struct dw_hdmi *hdmi)
{
	unsigned int count;
	unsigned int i;
	u8 val;

	/*
	 * Under some circumstances the Frame Composer arithmetic unit can miss
	 * an FC register write due to being busy processing the previous one.
	 * The issue can be worked around by issuing a TMDS software reset and
	 * then write one of the FC registers several times.
	 *
	 * The number of iterations matters and depends on the HDMI TX revision
	 * (and possibly on the platform). So far only i.MX6Q (v1.30a) and
	 * i.MX6DL (v1.31a) have been identified as needing the workaround, with
	 * 4 and 1 iterations respectively.
	 */

	switch (hdmi->version) {
	case 0x130a:
		count = 4;
		break;
	case 0x131a:
	case 0x200a:
	case 0x201a:
	case 0x211a:
		count = 1;
		break;
	default:
		return;
	}

	/* TMDS software reset */
	hdmi_writeb(hdmi, (u8)~HDMI_MC_SWRSTZ_TMDSSWRST_REQ, HDMI_MC_SWRSTZ);

	val = hdmi_readb(hdmi, HDMI_FC_INVIDCONF);
	for (i = 0; i < count; i++)
		hdmi_writeb(hdmi, val, HDMI_FC_INVIDCONF);
}

static void hdmi_enable_overflow_interrupts(struct dw_hdmi *hdmi)
{
	hdmi_writeb(hdmi, 0, HDMI_FC_MASK2);
	hdmi_writeb(hdmi, 0, HDMI_IH_MUTE_FC_STAT2);
}

static void hdmi_disable_overflow_interrupts(struct dw_hdmi *hdmi)
{
	hdmi_writeb(hdmi, HDMI_IH_MUTE_FC_STAT2_OVERFLOW_MASK,
		    HDMI_IH_MUTE_FC_STAT2);
}

static int dw_hdmi_setup(struct dw_hdmi *hdmi, struct drm_display_mode *mode)
{
	int ret;
	void *data = hdmi->plat_data->phy_data;
	bool need_delay = false;

	hdmi_disable_overflow_interrupts(hdmi);

	hdmi->vic = drm_match_cea_mode(mode);

	if (!hdmi->vic) {
		dev_dbg(hdmi->dev, "Non-CEA mode used in HDMI\n");
	} else {
		dev_dbg(hdmi->dev, "CEA mode used vic=%d\n", hdmi->vic);
	}

	if (hdmi->plat_data->get_enc_out_encoding)
		hdmi->hdmi_data.enc_out_encoding =
			hdmi->plat_data->get_enc_out_encoding(data);
	else if ((hdmi->vic == 6) || (hdmi->vic == 7) ||
		 (hdmi->vic == 21) || (hdmi->vic == 22) ||
		 (hdmi->vic == 2) || (hdmi->vic == 3) ||
		 (hdmi->vic == 17) || (hdmi->vic == 18))
		hdmi->hdmi_data.enc_out_encoding = V4L2_YCBCR_ENC_601;
	else
		hdmi->hdmi_data.enc_out_encoding = V4L2_YCBCR_ENC_709;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK) {
		hdmi->hdmi_data.video_mode.mpixelrepetitionoutput = 1;
		hdmi->hdmi_data.video_mode.mpixelrepetitioninput = 1;
	} else {
		hdmi->hdmi_data.video_mode.mpixelrepetitionoutput = 0;
		hdmi->hdmi_data.video_mode.mpixelrepetitioninput = 0;
	}

	/* TOFIX: Get input format from plat data or fallback to RGB888 */
	if (hdmi->plat_data->get_input_bus_format)
		hdmi->hdmi_data.enc_in_bus_format =
			hdmi->plat_data->get_input_bus_format(data);
	else if (hdmi->plat_data->input_bus_format)
		hdmi->hdmi_data.enc_in_bus_format =
			hdmi->plat_data->input_bus_format;
	else
		hdmi->hdmi_data.enc_in_bus_format =
			MEDIA_BUS_FMT_RGB888_1X24;

	/* TOFIX: Default to RGB888 output format */
	if (hdmi->plat_data->get_output_bus_format)
		hdmi->hdmi_data.enc_out_bus_format =
			hdmi->plat_data->get_output_bus_format(data);
	else
		hdmi->hdmi_data.enc_out_bus_format =
			MEDIA_BUS_FMT_RGB888_1X24;

	/* TOFIX: Get input encoding from plat data or fallback to none */
	if (hdmi->plat_data->get_enc_in_encoding)
		hdmi->hdmi_data.enc_in_encoding =
			hdmi->plat_data->get_enc_in_encoding(data);
	else if (hdmi->plat_data->input_bus_encoding)
		hdmi->hdmi_data.enc_in_encoding =
			hdmi->plat_data->input_bus_encoding;
	else
		hdmi->hdmi_data.enc_in_encoding = V4L2_YCBCR_ENC_DEFAULT;
	/*
	 * According to the dw-hdmi specification 6.4.2
	 * vp_pr_cd[3:0]:
	 * 0000b: No pixel repetition (pixel sent only once)
	 * 0001b: Pixel sent two times (pixel repeated once)
	 */
	hdmi->hdmi_data.pix_repet_factor =
		(mode->flags & DRM_MODE_FLAG_DBLCLK) ? 1 : 0;
	hdmi->hdmi_data.video_mode.mdataenablepolarity = true;

	/* HDMI Initialization Step B.1 */
	hdmi_av_composer(hdmi, mode);

	/* HDMI Initializateion Step B.2 */
	if (!hdmi->phy.enabled ||
	    hdmi->hdmi_data.video_mode.previous_pixelclock !=
	    hdmi->hdmi_data.video_mode.mpixelclock ||
	    hdmi->hdmi_data.video_mode.previous_tmdsclock !=
	    hdmi->hdmi_data.video_mode.mtmdsclock) {
		ret = hdmi->phy.ops->init(hdmi, hdmi->phy.data,
					  &hdmi->previous_mode);
		if (ret)
			return ret;
		hdmi->phy.enabled = true;
	} else {
		need_delay = true;
	}
	/* HDMI Initialization Step B.3 */
	dw_hdmi_enable_video_path(hdmi);

	if (hdmi->sink_has_audio) {
		dev_dbg(hdmi->dev, "sink has audio support\n");

		/* HDMI Initialization Step E - Configure audio */
		hdmi_clk_regenerator_update_pixel_clock(hdmi);
		hdmi_enable_audio_clk(hdmi);
	}

	/* not for DVI mode */
	if (hdmi->sink_is_hdmi) {
		dev_dbg(hdmi->dev, "%s HDMI mode\n", __func__);

		/* HDMI Initialization Step F - Configure AVI InfoFrame */
		hdmi_config_AVI(hdmi, mode);
		hdmi_config_vendor_specific_infoframe(hdmi, mode);
		hdmi_config_hdr_infoframe(hdmi);
	} else {
		dev_dbg(hdmi->dev, "%s DVI mode\n", __func__);
	}

	hdmi_video_packetize(hdmi);
	hdmi_video_csc(hdmi);
	hdmi_video_sample(hdmi);
	hdmi_tx_hdcp_config(hdmi, mode);
	dw_hdmi_clear_overflow(hdmi);
	if (hdmi->cable_plugin && hdmi->sink_is_hdmi)
		hdmi_enable_overflow_interrupts(hdmi);

	/* XXX: Add delay to make csc work before unmute video. */
	if (need_delay)
		msleep(50);
	return 0;
}

static void initialize_hdmi_ih_mutes(struct dw_hdmi *hdmi)
{
	u8 ih_mute;

	/*
	 * Boot up defaults are:
	 * HDMI_IH_MUTE   = 0x03 (disabled)
	 * HDMI_IH_MUTE_* = 0x00 (enabled)
	 *
	 * Disable top level interrupt bits in HDMI block
	 */
	ih_mute = hdmi_readb(hdmi, HDMI_IH_MUTE) |
		  HDMI_IH_MUTE_MUTE_WAKEUP_INTERRUPT |
		  HDMI_IH_MUTE_MUTE_ALL_INTERRUPT;

	hdmi_writeb(hdmi, ih_mute, HDMI_IH_MUTE);

	/* by default mask all interrupts */
	hdmi_writeb(hdmi, 0xff, HDMI_VP_MASK);
	hdmi_writeb(hdmi, 0xff, HDMI_FC_MASK0);
	hdmi_writeb(hdmi, 0xff, HDMI_FC_MASK1);
	hdmi_writeb(hdmi, 0xff, HDMI_FC_MASK2);
	hdmi_writeb(hdmi, 0xff, HDMI_PHY_MASK0);
	hdmi_writeb(hdmi, 0xff, HDMI_PHY_I2CM_INT_ADDR);
	hdmi_writeb(hdmi, 0xff, HDMI_PHY_I2CM_CTLINT_ADDR);
	hdmi_writeb(hdmi, 0xff, HDMI_AUD_INT);
	hdmi_writeb(hdmi, 0xff, HDMI_AUD_SPDIFINT);
	hdmi_writeb(hdmi, 0xff, HDMI_AUD_HBR_MASK);
	hdmi_writeb(hdmi, 0xff, HDMI_GP_MASK);
	hdmi_writeb(hdmi, 0xff, HDMI_A_APIINTMSK);
	hdmi_writeb(hdmi, 0xff, HDMI_CEC_MASK);
	hdmi_writeb(hdmi, 0xff, HDMI_I2CM_INT);
	hdmi_writeb(hdmi, 0xff, HDMI_I2CM_CTLINT);

	/* Disable interrupts in the IH_MUTE_* registers */
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_FC_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_FC_STAT1);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_FC_STAT2);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_AS_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_PHY_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_I2CM_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_CEC_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_VP_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_I2CMPHY_STAT0);
	hdmi_writeb(hdmi, 0xff, HDMI_IH_MUTE_AHBDMAAUD_STAT0);

	/* Enable top level interrupt bits in HDMI block */
	ih_mute &= ~(HDMI_IH_MUTE_MUTE_WAKEUP_INTERRUPT |
		    HDMI_IH_MUTE_MUTE_ALL_INTERRUPT);
	hdmi_writeb(hdmi, ih_mute, HDMI_IH_MUTE);
}

static void dw_hdmi_poweron(struct dw_hdmi *hdmi)
{
	hdmi->bridge_is_on = true;
	dw_hdmi_setup(hdmi, &hdmi->previous_mode);
}

static void dw_hdmi_poweroff(struct dw_hdmi *hdmi)
{
	if (hdmi->phy.enabled) {
		hdmi->phy.ops->disable(hdmi, hdmi->phy.data);
		hdmi->phy.enabled = false;
	}

	if (hdmi->hdcp && hdmi->hdcp->hdcp_stop)
		hdmi->hdcp->hdcp_stop(hdmi->hdcp);
	hdmi->bridge_is_on = false;
}

static void dw_hdmi_update_power(struct dw_hdmi *hdmi)
{
	int force = hdmi->force;

	if (hdmi->disabled) {
		force = DRM_FORCE_OFF;
	} else if (force == DRM_FORCE_UNSPECIFIED) {
		if (hdmi->rxsense)
			force = DRM_FORCE_ON;
		else
			force = DRM_FORCE_OFF;
	}

	if (force == DRM_FORCE_OFF) {
		if (hdmi->bridge_is_on)
			dw_hdmi_poweroff(hdmi);
	} else {
		if (!hdmi->bridge_is_on)
			dw_hdmi_poweron(hdmi);
	}
}

/*
 * Adjust the detection of RXSENSE according to whether we have a forced
 * connection mode enabled, or whether we have been disabled.  There is
 * no point processing RXSENSE interrupts if we have a forced connection
 * state, or DRM has us disabled.
 *
 * We also disable rxsense interrupts when we think we're disconnected
 * to avoid floating TDMS signals giving false rxsense interrupts.
 *
 * Note: we still need to listen for HPD interrupts even when DRM has us
 * disabled so that we can detect a connect event.
 */
static void dw_hdmi_update_phy_mask(struct dw_hdmi *hdmi)
{
	u8 old_mask = hdmi->phy_mask;

	if (hdmi->force || hdmi->disabled || !hdmi->rxsense)
		hdmi->phy_mask |= HDMI_PHY_RX_SENSE;
	else
		hdmi->phy_mask &= ~HDMI_PHY_RX_SENSE;

	if (old_mask != hdmi->phy_mask)
		hdmi_writeb(hdmi, hdmi->phy_mask, HDMI_PHY_MASK0);
}

static void dw_hdmi_bridge_mode_set(struct drm_bridge *bridge,
				    struct drm_display_mode *orig_mode,
				    struct drm_display_mode *mode)
{
	struct dw_hdmi *hdmi = bridge->driver_private;

	mutex_lock(&hdmi->mutex);

	/* Store the display mode for plugin/DKMS poweron events */
	memcpy(&hdmi->previous_mode, mode, sizeof(hdmi->previous_mode));

	mutex_unlock(&hdmi->mutex);
}

static void dw_hdmi_bridge_disable(struct drm_bridge *bridge)
{
	struct dw_hdmi *hdmi = bridge->driver_private;

	mutex_lock(&hdmi->mutex);
	hdmi->disabled = true;
	dw_hdmi_update_power(hdmi);
	dw_hdmi_update_phy_mask(hdmi);
	mutex_unlock(&hdmi->mutex);
}

static void dw_hdmi_bridge_enable(struct drm_bridge *bridge)
{
	struct dw_hdmi *hdmi = bridge->driver_private;

	mutex_lock(&hdmi->mutex);
	hdmi->disabled = false;
	dw_hdmi_update_power(hdmi);
	dw_hdmi_update_phy_mask(hdmi);
	mutex_unlock(&hdmi->mutex);
}

static void dw_hdmi_bridge_nop(struct drm_bridge *bridge)
{
	/* do nothing */
}

static enum drm_connector_status
dw_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);

	mutex_lock(&hdmi->mutex);
	hdmi->force = DRM_FORCE_UNSPECIFIED;
	dw_hdmi_update_power(hdmi);
	dw_hdmi_update_phy_mask(hdmi);
	mutex_unlock(&hdmi->mutex);

	return hdmi->phy.ops->read_hpd(hdmi, hdmi->phy.data);
}

static int dw_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);
	struct edid *edid;
	struct drm_display_mode *mode;
	const u8 def_modes[6] = {4, 16, 31, 19, 17, 2};
	struct hdr_static_metadata *metedata =
			&connector->display_info.hdmi.hdr_panel_metadata;
	int i, ret = 0;

	if (!hdmi->ddc)
		return 0;

	edid = drm_get_edid(connector, hdmi->ddc);
	if (edid) {
		dev_dbg(hdmi->dev, "got edid: width[%d] x height[%d]\n",
			edid->width_cm, edid->height_cm);

		hdmi->sink_is_hdmi = drm_detect_hdmi_monitor(edid);
		hdmi->sink_has_audio = drm_detect_monitor_audio(edid);
		drm_mode_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		/* Store the ELD */
		drm_edid_to_eld(connector, edid);
		drm_mode_connector_update_hdr_property(connector, metedata);
		kfree(edid);
	} else {
		hdmi->sink_is_hdmi = true;
		hdmi->sink_has_audio = true;
		for (i = 0; i < sizeof(def_modes); i++) {
			mode = drm_display_mode_from_vic_index(connector,
							       def_modes,
							       31, i);
			if (mode) {
				if (!i)
					mode->type = DRM_MODE_TYPE_PREFERRED;
				drm_mode_probed_add(connector, mode);
				ret++;
			}
		}
		dev_info(hdmi->dev, "failed to get edid\n");
	}

	return ret;
}

static enum drm_mode_status
dw_hdmi_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	struct dw_hdmi *hdmi = container_of(connector,
					   struct dw_hdmi, connector);
	enum drm_mode_status mode_status = MODE_OK;

	if (hdmi->plat_data->mode_valid)
		mode_status = hdmi->plat_data->mode_valid(connector, mode);

	return mode_status;
}

static struct drm_encoder *dw_hdmi_connector_best_encoder(struct drm_connector
							   *connector)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);

	return hdmi->encoder;
}

static void dw_hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static void
dw_hdmi_connector_atomic_begin(struct drm_connector *connector,
			       struct drm_connector_state *conn_state)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					    connector);
	void *data = hdmi->plat_data->phy_data;
	unsigned int enc_in_bus_format;
	unsigned int enc_out_bus_format;
	unsigned int enc_in_encoding;
	unsigned int enc_out_encoding;

	if (!hdmi->hpd_state || !conn_state->crtc)
		return;

	if (!hdmi->hdmi_data.video_mode.mpixelclock)
		return;

	if (hdmi->plat_data->get_enc_in_encoding)
		enc_in_encoding = hdmi->plat_data->get_enc_in_encoding(data);
	else
		enc_in_encoding = hdmi->hdmi_data.enc_in_encoding;
	if (hdmi->plat_data->get_enc_out_encoding)
		enc_out_encoding = hdmi->plat_data->get_enc_out_encoding(data);
	else
		enc_out_encoding = hdmi->hdmi_data.enc_out_encoding;
	if (hdmi->plat_data->get_input_bus_format)
		enc_in_bus_format =
			hdmi->plat_data->get_input_bus_format(data);
	else
		enc_in_bus_format = hdmi->hdmi_data.enc_in_bus_format;
	if (hdmi->plat_data->get_output_bus_format)
		enc_out_bus_format =
			hdmi->plat_data->get_output_bus_format(data);
	else
		enc_out_bus_format = hdmi->hdmi_data.enc_out_bus_format;

	if (enc_in_encoding != hdmi->hdmi_data.enc_in_encoding ||
	    enc_out_encoding != hdmi->hdmi_data.enc_out_encoding ||
	    enc_in_bus_format != hdmi->hdmi_data.enc_in_bus_format ||
	    enc_out_bus_format != hdmi->hdmi_data.enc_out_bus_format) {
		hdmi->hdmi_data.update = true;
		hdmi_writeb(hdmi, HDMI_FC_GCP_SET_AVMUTE, HDMI_FC_GCP);
		/* XXX: Add delay to make av mute work on sink*/
		msleep(50);
	} else {
		hdmi->hdmi_data.update = false;
	}
}

static void
dw_hdmi_connector_atomic_flush(struct drm_connector *connector,
			       struct drm_connector_state *conn_state)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);
	struct drm_display_mode *mode = NULL;

	if (!hdmi->hpd_state || !conn_state->crtc)
		return;

	DRM_DEBUG("%s\n", __func__);

	/*
	 * If HDMI is enabled in uboot, it's need to record
	 * drm_display_mode and set phy status to enabled.
	 */
	if (!hdmi->hdmi_data.video_mode.mpixelclock) {
		mode = &conn_state->crtc->mode;
		memcpy(&hdmi->previous_mode, mode, sizeof(hdmi->previous_mode));
		hdmi->hdmi_data.video_mode.mpixelclock = mode->clock;
		hdmi->hdmi_data.video_mode.previous_pixelclock = mode->clock;
		hdmi->hdmi_data.video_mode.previous_tmdsclock = mode->clock;
		hdmi->phy.enabled = true;
		return;
	}

	if (hdmi->hdmi_data.update) {
		dw_hdmi_setup(hdmi, &hdmi->previous_mode);
		hdmi_writeb(hdmi, HDMI_FC_GCP_CLEAR_AVMUTE, HDMI_FC_GCP);
		hdmi->hdmi_data.update = false;
	} else if (connector->state->hdr_metadata_changed &&
		   hdmi->sink_is_hdmi) {
		hdmi_config_hdr_infoframe(hdmi);
	}
}

static int
dw_hdmi_atomic_connector_set_property(struct drm_connector *connector,
				      struct drm_connector_state *state,
				      struct drm_property *property,
				      uint64_t val)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);
	const struct dw_hdmi_property_ops *ops =
				hdmi->plat_data->property_ops;

	if (ops && ops->set_property)
		return ops->set_property(connector, state, property,
					 val, hdmi->plat_data->phy_data);
	else
		return -EINVAL;
}

static int
dw_hdmi_atomic_connector_get_property(struct drm_connector *connector,
				      const struct drm_connector_state *state,
				      struct drm_property *property,
				      uint64_t *val)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);
	const struct dw_hdmi_property_ops *ops =
				hdmi->plat_data->property_ops;

	if (ops && ops->get_property)
		return ops->get_property(connector, state, property,
					 val, hdmi->plat_data->phy_data);
	else
		return -EINVAL;
}

static int
dw_hdmi_connector_set_property(struct drm_connector *connector,
			       struct drm_property *property, uint64_t val)
{
	return dw_hdmi_atomic_connector_set_property(connector, NULL,
						     property, val);
}

static void dw_hdmi_connector_force(struct drm_connector *connector)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);

	mutex_lock(&hdmi->mutex);
#ifdef CONFIG_SWITCH
	if (!hdmi->disabled && hdmi->force != connector->force) {
		if (connector->force == DRM_FORCE_OFF)
			switch_set_state(&hdmi->switchdev, 0);
		else if (connector->force == DRM_FORCE_ON)
			switch_set_state(&hdmi->switchdev, 1);
	}
#endif
	hdmi->force = connector->force;
	dw_hdmi_update_power(hdmi);
	dw_hdmi_update_phy_mask(hdmi);
	mutex_unlock(&hdmi->mutex);
}

static const struct drm_connector_funcs dw_hdmi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = dw_hdmi_connector_detect,
	.destroy = dw_hdmi_connector_destroy,
	.force = dw_hdmi_connector_force,
};

static const struct drm_connector_funcs dw_hdmi_atomic_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = dw_hdmi_connector_detect,
	.destroy = dw_hdmi_connector_destroy,
	.force = dw_hdmi_connector_force,
	.reset = drm_atomic_helper_connector_reset,
	.set_property = dw_hdmi_connector_set_property,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = dw_hdmi_atomic_connector_set_property,
	.atomic_get_property = dw_hdmi_atomic_connector_get_property,
};

static const struct drm_connector_helper_funcs dw_hdmi_connector_helper_funcs = {
	.get_modes = dw_hdmi_connector_get_modes,
	.mode_valid = dw_hdmi_connector_mode_valid,
	.best_encoder = dw_hdmi_connector_best_encoder,
	.atomic_begin = dw_hdmi_connector_atomic_begin,
	.atomic_flush = dw_hdmi_connector_atomic_flush,
};

static const struct drm_bridge_funcs dw_hdmi_bridge_funcs = {
	.enable = dw_hdmi_bridge_enable,
	.disable = dw_hdmi_bridge_disable,
	.pre_enable = dw_hdmi_bridge_nop,
	.post_disable = dw_hdmi_bridge_nop,
	.mode_set = dw_hdmi_bridge_mode_set,
};

static irqreturn_t dw_hdmi_i2c_irq(struct dw_hdmi *hdmi)
{
	struct dw_hdmi_i2c *i2c = hdmi->i2c;
	unsigned int stat;

	stat = hdmi_readb(hdmi, HDMI_IH_I2CM_STAT0);
	if (!stat)
		return IRQ_NONE;

	hdmi_writeb(hdmi, stat, HDMI_IH_I2CM_STAT0);

	i2c->stat = stat;

	complete(&i2c->cmp);

	return IRQ_HANDLED;
}

static irqreturn_t dw_hdmi_hardirq(int irq, void *dev_id)
{
	struct dw_hdmi *hdmi = dev_id;
	u8 intr_stat, hdcp_stat;
	irqreturn_t ret = IRQ_NONE;

	if (hdmi->i2c)
		ret = dw_hdmi_i2c_irq(hdmi);

	intr_stat = hdmi_readb(hdmi, HDMI_IH_PHY_STAT0);
	if (intr_stat) {
		hdmi_writeb(hdmi, ~0, HDMI_IH_MUTE_PHY_STAT0);
		return IRQ_WAKE_THREAD;
	}

	hdcp_stat = hdmi_readb(hdmi, HDMI_A_APIINTSTAT);
	if (hdcp_stat) {
		dev_dbg(hdmi->dev, "HDCP irq %#x\n", hdcp_stat);
		hdmi_writeb(hdmi, 0xff, HDMI_A_APIINTMSK);
		return IRQ_WAKE_THREAD;
	}

	return ret;
}

static irqreturn_t dw_hdmi_irq(int irq, void *dev_id)
{
	struct dw_hdmi *hdmi = dev_id;
	u8 intr_stat, phy_int_pol, phy_pol_mask, phy_stat, hdcp_stat;

	intr_stat = hdmi_readb(hdmi, HDMI_IH_PHY_STAT0);
	phy_int_pol = hdmi_readb(hdmi, HDMI_PHY_POL0);
	phy_stat = hdmi_readb(hdmi, HDMI_PHY_STAT0);

	phy_pol_mask = 0;
	if (intr_stat & HDMI_IH_PHY_STAT0_HPD)
		phy_pol_mask |= HDMI_PHY_HPD;
	if (intr_stat & HDMI_IH_PHY_STAT0_RX_SENSE0)
		phy_pol_mask |= HDMI_PHY_RX_SENSE0;
	if (intr_stat & HDMI_IH_PHY_STAT0_RX_SENSE1)
		phy_pol_mask |= HDMI_PHY_RX_SENSE1;
	if (intr_stat & HDMI_IH_PHY_STAT0_RX_SENSE2)
		phy_pol_mask |= HDMI_PHY_RX_SENSE2;
	if (intr_stat & HDMI_IH_PHY_STAT0_RX_SENSE3)
		phy_pol_mask |= HDMI_PHY_RX_SENSE3;

	if (phy_pol_mask)
		hdmi_modb(hdmi, ~phy_int_pol, phy_pol_mask, HDMI_PHY_POL0);

	/*
	 * RX sense tells us whether the TDMS transmitters are detecting
	 * load - in other words, there's something listening on the
	 * other end of the link.  Use this to decide whether we should
	 * power on the phy as HPD may be toggled by the sink to merely
	 * ask the source to re-read the EDID.
	 */
	if (intr_stat &
	    (HDMI_IH_PHY_STAT0_RX_SENSE | HDMI_IH_PHY_STAT0_HPD)) {
		mutex_lock(&hdmi->mutex);
		if (!hdmi->bridge_is_on && !hdmi->force) {
			/*
			 * If the RX sense status indicates we're disconnected,
			 * clear the software rxsense status.
			 */
			if (!(phy_stat & HDMI_PHY_RX_SENSE))
				hdmi->rxsense = false;

			/*
			 * Only set the software rxsense status when both
			 * rxsense and hpd indicates we're connected.
			 * This avoids what seems to be bad behaviour in
			 * at least iMX6S versions of the phy.
			 */
			if (phy_stat & HDMI_PHY_HPD)
				hdmi->rxsense = true;

			dw_hdmi_update_power(hdmi);
			dw_hdmi_update_phy_mask(hdmi);
		}
		mutex_unlock(&hdmi->mutex);
	}

	check_hdmi_irq(hdmi, intr_stat, phy_int_pol);

	hdmi_writeb(hdmi, intr_stat, HDMI_IH_PHY_STAT0);
	hdmi_writeb(hdmi, ~(HDMI_IH_PHY_STAT0_HPD | HDMI_IH_PHY_STAT0_RX_SENSE),
		    HDMI_IH_MUTE_PHY_STAT0);

	hdcp_stat = hdmi_readb(hdmi, HDMI_A_APIINTSTAT);
	if (hdcp_stat) {
		if (hdmi->hdcp && hdmi->hdcp->hdcp_isr)
			hdmi->hdcp->hdcp_isr(hdmi->hdcp, hdcp_stat);
		hdmi_writeb(hdmi, hdcp_stat, HDMI_A_APIINTCLR);
		hdmi_writeb(hdmi, 0x00, HDMI_A_APIINTMSK);
	}

	return IRQ_HANDLED;
}

static const struct dw_hdmi_phy_data dw_hdmi_phys[] = {
	{
		.type = DW_HDMI_PHY_DWC_HDMI_TX_PHY,
		.name = "DWC HDMI TX PHY",
		.gen = 1,
	}, {
		.type = DW_HDMI_PHY_DWC_MHL_PHY_HEAC,
		.name = "DWC MHL PHY + HEAC PHY",
		.gen = 2,
		.has_svsret = true,
		.configure = hdmi_phy_configure_dwc_hdmi_3d_tx,
	}, {
		.type = DW_HDMI_PHY_DWC_MHL_PHY,
		.name = "DWC MHL PHY",
		.gen = 2,
		.has_svsret = true,
		.configure = hdmi_phy_configure_dwc_hdmi_3d_tx,
	}, {
		.type = DW_HDMI_PHY_DWC_HDMI_3D_TX_PHY_HEAC,
		.name = "DWC HDMI 3D TX PHY + HEAC PHY",
		.gen = 2,
		.configure = hdmi_phy_configure_dwc_hdmi_3d_tx,
	}, {
		.type = DW_HDMI_PHY_DWC_HDMI_3D_TX_PHY,
		.name = "DWC HDMI 3D TX PHY",
		.gen = 2,
		.configure = hdmi_phy_configure_dwc_hdmi_3d_tx,
	}, {
		.type = DW_HDMI_PHY_DWC_HDMI20_TX_PHY,
		.name = "DWC HDMI 2.0 TX PHY",
		.gen = 2,
		.has_svsret = true,
		.configure = hdmi_phy_configure_dwc_hdmi_3d_tx,
	}, {
		.type = DW_HDMI_PHY_VENDOR_PHY,
		.name = "Vendor PHY",
	}
};

static int dw_hdmi_detect_phy(struct dw_hdmi *hdmi)
{
	unsigned int i;
	u8 phy_type;

	phy_type = hdmi_readb(hdmi, HDMI_CONFIG2_ID);

	/*
	 * RK3228 and RK3328 phy_type is DW_HDMI_PHY_DWC_HDMI20_TX_PHY,
	 * but it has a vedor phy.
	 */
	if (phy_type == DW_HDMI_PHY_VENDOR_PHY ||
	    hdmi->dev_type == RK3328_HDMI ||
	    hdmi->dev_type == RK3228_HDMI) {
		/* Vendor PHYs require support from the glue layer. */
		if (!hdmi->plat_data->phy_ops || !hdmi->plat_data->phy_name) {
			dev_err(hdmi->dev,
				"Vendor HDMI PHY not supported by glue layer\n");
			return -ENODEV;
		}

		hdmi->phy.ops = hdmi->plat_data->phy_ops;
		hdmi->phy.data = hdmi->plat_data->phy_data;
		hdmi->phy.name = hdmi->plat_data->phy_name;
		return 0;
	}

	/* Synopsys PHYs are handled internally. */
	for (i = 0; i < ARRAY_SIZE(dw_hdmi_phys); ++i) {
		if (dw_hdmi_phys[i].type == phy_type) {
			hdmi->phy.ops = &dw_hdmi_synopsys_phy_ops;
			hdmi->phy.name = dw_hdmi_phys[i].name;
			hdmi->phy.data = (void *)&dw_hdmi_phys[i];

			if (!dw_hdmi_phys[i].configure &&
			    !hdmi->plat_data->configure_phy) {
				dev_err(hdmi->dev, "%s requires platform support\n",
					hdmi->phy.name);
				return -ENODEV;
			}

			return 0;
		}
	}

	dev_err(hdmi->dev, "Unsupported HDMI PHY type (%02x)\n", phy_type);
	return -ENODEV;
}

static void dw_hdmi_attatch_properties(struct dw_hdmi *hdmi)
{
	const struct dw_hdmi_property_ops *ops =
				hdmi->plat_data->property_ops;

	if (ops && ops->attatch_properties)
		return ops->attatch_properties(&hdmi->connector,
					       hdmi->version,
					       hdmi->plat_data->phy_data);
}

static void dw_hdmi_destroy_properties(struct dw_hdmi *hdmi)
{
	const struct dw_hdmi_property_ops *ops =
				hdmi->plat_data->property_ops;

	if (ops && ops->destroy_properties)
		return ops->destroy_properties(&hdmi->connector,
					       hdmi->plat_data->phy_data);
}

static int dw_hdmi_register(struct drm_device *drm, struct dw_hdmi *hdmi)
{
	struct drm_encoder *encoder = hdmi->encoder;
	struct drm_bridge *bridge = &hdmi->bridge;
	int ret;

	bridge->driver_private = hdmi;
	bridge->funcs = &dw_hdmi_bridge_funcs;
	ret = drm_bridge_attach(drm, bridge);
	if (ret) {
		DRM_ERROR("Failed to initialize bridge with drm\n");
		return -EINVAL;
	}

	encoder->bridge = bridge;
	hdmi->connector.polled = DRM_CONNECTOR_POLL_HPD;
	hdmi->connector.port = hdmi->dev->of_node;

	drm_connector_helper_add(&hdmi->connector,
				 &dw_hdmi_connector_helper_funcs);

	if (drm_core_check_feature(drm, DRIVER_ATOMIC))
		drm_connector_init(drm, &hdmi->connector,
				   &dw_hdmi_atomic_connector_funcs,
				   DRM_MODE_CONNECTOR_HDMIA);
	else
		drm_connector_init(drm, &hdmi->connector,
				   &dw_hdmi_connector_funcs,
				   DRM_MODE_CONNECTOR_HDMIA);

	drm_mode_connector_attach_encoder(&hdmi->connector, encoder);

	dw_hdmi_attatch_properties(hdmi);

	return 0;
}

static int dw_hdmi_status_show(struct seq_file *s, void *v)
{
	struct dw_hdmi *hdmi = s->private;
	u32 val;

	seq_puts(s, "PHY: ");
	if (!hdmi->phy.enabled) {
		seq_puts(s, "disabled\n");
		return 0;
	}
	seq_puts(s, "enabled\t\t\tMode: ");
	if (hdmi->sink_is_hdmi)
		seq_puts(s, "HDMI\n");
	else
		seq_puts(s, "DVI\n");
	if (hdmi->hdmi_data.video_mode.mtmdsclock > 340000000)
		val = hdmi->hdmi_data.video_mode.mtmdsclock / 4;
	else
		val = hdmi->hdmi_data.video_mode.mtmdsclock;
	seq_printf(s, "Pixel Clk: %uHz\t\tTMDS Clk: %uHz\n",
		   hdmi->hdmi_data.video_mode.mpixelclock, val);
	seq_puts(s, "Color Format: ");
	if (hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_out_bus_format))
		seq_puts(s, "RGB");
	else if (hdmi_bus_fmt_is_yuv444(hdmi->hdmi_data.enc_out_bus_format))
		seq_puts(s, "YUV444");
	else if (hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_out_bus_format))
		seq_puts(s, "YUV422");
	else if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format))
		seq_puts(s, "YUV420");
	else
		seq_puts(s, "UNKNOWN");
	val =  hdmi_bus_fmt_color_depth(hdmi->hdmi_data.enc_out_bus_format);
	seq_printf(s, "\t\tColor Depth: %d bit\n", val);
	seq_puts(s, "Colorimetry: ");
	switch (hdmi->hdmi_data.enc_out_encoding) {
	case V4L2_YCBCR_ENC_601:
		seq_puts(s, "ITU.BT601");
		break;
	case V4L2_YCBCR_ENC_709:
		seq_puts(s, "ITU.BT709");
		break;
	case V4L2_YCBCR_ENC_BT2020:
		seq_puts(s, "ITU.BT2020");
		break;
	default: /* Carries no data */
		seq_puts(s, "ITU.BT601");
		break;
	}

	seq_puts(s, "\t\tEOTF: ");

	if (hdmi->version < 0x211a) {
		seq_puts(s, "Unsupported\n");
		return 0;
	}

	val = hdmi_readb(hdmi, HDMI_FC_PACKET_TX_EN);
	if (!(val & HDMI_FC_PACKET_DRM_TX_EN_MASK)) {
		seq_puts(s, "Off\n");
		return 0;
	}

	switch (hdmi_readb(hdmi, HDMI_FC_DRM_PB0)) {
	case TRADITIONAL_GAMMA_SDR:
		seq_puts(s, "SDR");
		break;
	case TRADITIONAL_GAMMA_HDR:
		seq_puts(s, "HDR");
		break;
	case SMPTE_ST2084:
		seq_puts(s, "ST2084");
		break;
	case HLG:
		seq_puts(s, "HLG");
		break;
	default:
		seq_puts(s, "Not Defined\n");
		return 0;
	}

	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB3) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB2);
	seq_printf(s, "\nx0: %d", val);
	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB5) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB4);
	seq_printf(s, "\t\t\t\ty0: %d\n", val);
	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB7) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB6);
	seq_printf(s, "x1: %d", val);
	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB9) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB8);
	seq_printf(s, "\t\t\t\ty1: %d\n", val);
	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB11) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB10);
	seq_printf(s, "x2: %d", val);
	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB13) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB12);
	seq_printf(s, "\t\t\t\ty2: %d\n", val);
	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB15) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB14);
	seq_printf(s, "white x: %d", val);
	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB17) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB16);
	seq_printf(s, "\t\t\twhite y: %d\n", val);
	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB19) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB18);
	seq_printf(s, "max lum: %d", val);
	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB21) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB20);
	seq_printf(s, "\t\t\tmin lum: %d\n", val);
	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB23) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB22);
	seq_printf(s, "max cll: %d", val);
	val = hdmi_readb(hdmi, HDMI_FC_DRM_PB25) << 8;
	val |= hdmi_readb(hdmi, HDMI_FC_DRM_PB24);
	seq_printf(s, "\t\t\tmax fall: %d\n", val);
	return 0;
}

static int dw_hdmi_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, dw_hdmi_status_show, inode->i_private);
}

static const struct file_operations dw_hdmi_status_fops = {
	.owner = THIS_MODULE,
	.open = dw_hdmi_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

struct dw_hdmi_reg_table {
	int reg_base;
	int reg_end;
};

static const struct dw_hdmi_reg_table hdmi_reg_table[] = {
	{HDMI_DESIGN_ID, HDMI_CONFIG3_ID},
	{HDMI_IH_FC_STAT0, HDMI_IH_MUTE},
	{HDMI_TX_INVID0, HDMI_TX_BCBDATA1},
	{HDMI_VP_STATUS, HDMI_VP_POL},
	{HDMI_FC_INVIDCONF, HDMI_FC_DBGTMDS2},
	{HDMI_PHY_CONF0, HDMI_PHY_POL0},
	{HDMI_PHY_I2CM_SLAVE_ADDR, HDMI_PHY_I2CM_FS_SCL_LCNT_0_ADDR},
	{HDMI_AUD_CONF0, 0x3624},
	{HDMI_MC_SFRDIV, HDMI_MC_HEACPHY_RST},
	{HDMI_CSC_CFG, HDMI_CSC_COEF_C4_LSB},
	{HDMI_A_HDCPCFG0, 0x52bb},
	{0x7800, 0x7818},
	{0x7900, 0x790e},
	{HDMI_CEC_CTRL, HDMI_CEC_WKUPCTRL},
	{HDMI_I2CM_SLAVE, 0x7e31},
};

static int dw_hdmi_ctrl_show(struct seq_file *s, void *v)
{
	struct dw_hdmi *hdmi = s->private;
	u32 i = 0, j = 0, val = 0;

	seq_puts(s, "\n>>>hdmi_ctl reg ");
	for (i = 0; i < 16; i++)
		seq_printf(s, " %2x", i);
	seq_puts(s, "\n---------------------------------------------------");

	for (i = 0; i < ARRAY_SIZE(hdmi_reg_table); i++) {
		for (j = hdmi_reg_table[i].reg_base;
		     j <= hdmi_reg_table[i].reg_end; j++) {
			val = hdmi_readb(hdmi, j);
			if ((j - hdmi_reg_table[i].reg_base) % 16 == 0)
				seq_printf(s, "\n>>>hdmi_ctl %04x:", j);
			seq_printf(s, " %02x", val);
		}
	}
	seq_puts(s, "\n---------------------------------------------------\n");

	return 0;
}

static int dw_hdmi_ctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, dw_hdmi_ctrl_show, inode->i_private);
}

static ssize_t
dw_hdmi_ctrl_write(struct file *file, const char __user *buf,
		   size_t count, loff_t *ppos)
{
	struct dw_hdmi *hdmi =
		((struct seq_file *)file->private_data)->private;
	u32 reg, val;
	char kbuf[25];

	if (hdmi->dev_type == RK3228_HDMI)
		return -EFAULT;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	if (sscanf(kbuf, "%x%x", &reg, &val) == -1)
		return -EFAULT;
	if ((reg < 0) || (reg > HDMI_I2CM_FS_SCL_LCNT_0_ADDR)) {
		dev_err(hdmi->dev, "it is no a hdmi register\n");
		return count;
	}
	dev_info(hdmi->dev, "/**********hdmi register config******/");
	dev_info(hdmi->dev, "\n reg=%x val=%x\n", reg, val);
	hdmi_writeb(hdmi, val, reg);
	return count;
}

static const struct file_operations dw_hdmi_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = dw_hdmi_ctrl_open,
	.read = seq_read,
	.write = dw_hdmi_ctrl_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int dw_hdmi_phy_show(struct seq_file *s, void *v)
{
	struct dw_hdmi *hdmi = s->private;
	u32 i, val;

	seq_puts(s, "\n>>>hdmi_phy reg\n");
	for (i = 0; i < 0x28; i++) {
		val = hdmi_phy_i2c_read(hdmi, i);
		seq_printf(s, "regs %02x val %04x\n", i, val);
	}
	return 0;
}

static int dw_hdmi_phy_open(struct inode *inode, struct file *file)
{
	return single_open(file, dw_hdmi_phy_show, inode->i_private);
}

static ssize_t
dw_hdmi_phy_write(struct file *file, const char __user *buf,
		  size_t count, loff_t *ppos)
{
	struct dw_hdmi *hdmi =
		((struct seq_file *)file->private_data)->private;
	u32 reg, val;
	char kbuf[25];

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	if (sscanf(kbuf, "%x%x", &reg, &val) == -1)
		return -EFAULT;
	if ((reg < 0) || (reg > 0x100)) {
		dev_err(hdmi->dev, "it is not a hdmi phy register\n");
		return count;
	}
	dev_info(hdmi->dev, "/*******hdmi phy register config******/");
	dev_info(hdmi->dev, "\n reg=%x val=%x\n", reg, val);
	dw_hdmi_phy_i2c_write(hdmi, val, reg);
	return count;
}

static const struct file_operations dw_hdmi_phy_fops = {
	.owner = THIS_MODULE,
	.open = dw_hdmi_phy_open,
	.read = seq_read,
	.write = dw_hdmi_phy_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static void dw_hdmi_register_debugfs(struct device *dev, struct dw_hdmi *hdmi)
{
	hdmi->debugfs_dir = debugfs_create_dir("dw-hdmi", NULL);
	if (IS_ERR(hdmi->debugfs_dir)) {
		dev_err(dev, "failed to create debugfs dir!\n");
		return;
	}

	debugfs_create_file("status", 0400, hdmi->debugfs_dir,
			    hdmi, &dw_hdmi_status_fops);

	debugfs_create_file("ctrl", 0400, hdmi->debugfs_dir,
			    hdmi, &dw_hdmi_ctrl_fops);

	if (hdmi->dev_type != RK3228_HDMI &&
	    hdmi->dev_type != RK3328_HDMI)
		debugfs_create_file("phy", 0400, hdmi->debugfs_dir,
				    hdmi, &dw_hdmi_phy_fops);
}

static void dw_hdmi_register_hdcp(struct device *dev, struct dw_hdmi *hdmi,
				  u32 val, bool hdcp1x_enable)
{
	struct dw_hdcp hdmi_hdcp = {
		.hdmi = hdmi,
		.write = hdmi_writeb,
		.read = hdmi_readb,
		.regs = hdmi->regs,
		.reg_io_width = val,
		.enable = hdcp1x_enable,
	};
	struct platform_device_info hdcp_device_info = {
		.parent = dev,
		.id = PLATFORM_DEVID_AUTO,
		.res = NULL,
		.num_res = 0,
		.name = DW_HDCP_DRIVER_NAME,
		.data = &hdmi_hdcp,
		.size_data = sizeof(hdmi_hdcp),
		.dma_mask = DMA_BIT_MASK(32),
	};

	hdmi->hdcp_dev = platform_device_register_full(&hdcp_device_info);
	if (IS_ERR(hdmi->hdcp_dev))
		dev_err(dev, "failed to register hdcp!\n");
	else
		hdmi->hdcp = hdmi->hdcp_dev->dev.platform_data;
}

int dw_hdmi_bind(struct device *dev, struct device *master,
		 void *data, struct drm_encoder *encoder,
		 struct resource *iores, int irq,
		 const struct dw_hdmi_plat_data *plat_data)
{
	struct drm_device *drm = data;
	struct device_node *np = dev->of_node;
	struct platform_device_info pdevinfo;
	struct device_node *ddc_node;
	struct dw_hdmi *hdmi;
	int ret;
	u8 prod_id0;
	u8 prod_id1;
	u32 val = 1;
	u8 config0;
	u8 config3;
	bool hdcp1x_enable = false;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->connector.interlace_allowed = 1;
	hdmi->connector.stereo_allowed = 1;

	hdmi->plat_data = plat_data;
	hdmi->dev = dev;
	hdmi->dev_type = plat_data->dev_type;
	hdmi->sample_rate = 48000;
	hdmi->encoder = encoder;
	hdmi->disabled = true;
	hdmi->rxsense = true;
	hdmi->phy_mask = (u8)~(HDMI_PHY_HPD | HDMI_PHY_RX_SENSE);
	hdmi->irq = irq;

	mutex_init(&hdmi->mutex);
	mutex_init(&hdmi->audio_mutex);
	spin_lock_init(&hdmi->audio_lock);

	of_property_read_u32(np, "reg-io-width", &val);

	switch (val) {
	case 4:
		hdmi->write = dw_hdmi_writel;
		hdmi->read = dw_hdmi_readl;
		break;
	case 1:
		hdmi->write = dw_hdmi_writeb;
		hdmi->read = dw_hdmi_readb;
		break;
	default:
		dev_err(dev, "reg-io-width must be 1 or 4\n");
		return -EINVAL;
	}

	ddc_node = of_parse_phandle(np, "ddc-i2c-bus", 0);
	if (ddc_node) {
		hdmi->ddc = of_find_i2c_adapter_by_node(ddc_node);
		of_node_put(ddc_node);
		if (!hdmi->ddc) {
			dev_dbg(hdmi->dev, "failed to read ddc node\n");
			return -EPROBE_DEFER;
		}

	} else {
		dev_dbg(hdmi->dev, "no ddc property found\n");
	}

	/* If DDC bus is not specified, try to register HDMI I2C bus */
	if (!hdmi->ddc) {
		hdmi->ddc = dw_hdmi_i2c_adapter(hdmi);
		if (IS_ERR(hdmi->ddc))
			hdmi->ddc = NULL;
		/*
		 * Read high and low time from device tree. If not available use
		 * the default timing scl clock rate is about 99.6KHz.
		 */
		if (of_property_read_u32(np, "ddc-i2c-scl-high-time-ns",
					 &hdmi->i2c->scl_high_ns))
			hdmi->i2c->scl_high_ns = 4708;
		if (of_property_read_u32(np, "ddc-i2c-scl-low-time-ns",
					 &hdmi->i2c->scl_low_ns))
			hdmi->i2c->scl_low_ns = 4916;
	}

	hdmi->regs = devm_ioremap_resource(dev, iores);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

	hdmi->isfr_clk = devm_clk_get(hdmi->dev, "isfr");
	if (IS_ERR(hdmi->isfr_clk)) {
		ret = PTR_ERR(hdmi->isfr_clk);
		dev_err(hdmi->dev, "Unable to get HDMI isfr clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->isfr_clk);
	if (ret) {
		dev_err(hdmi->dev, "Cannot enable HDMI isfr clock: %d\n", ret);
		return ret;
	}

	hdmi->iahb_clk = devm_clk_get(hdmi->dev, "iahb");
	if (IS_ERR(hdmi->iahb_clk)) {
		ret = PTR_ERR(hdmi->iahb_clk);
		dev_err(hdmi->dev, "Unable to get HDMI iahb clk: %d\n", ret);
		goto err_isfr;
	}

	ret = clk_prepare_enable(hdmi->iahb_clk);
	if (ret) {
		dev_err(hdmi->dev, "Cannot enable HDMI iahb clock: %d\n", ret);
		goto err_isfr;
	}

	/* Product and revision IDs */
	hdmi->version = (hdmi_readb(hdmi, HDMI_DESIGN_ID) << 8)
		      | (hdmi_readb(hdmi, HDMI_REVISION_ID) << 0);
	prod_id0 = hdmi_readb(hdmi, HDMI_PRODUCT_ID0);
	prod_id1 = hdmi_readb(hdmi, HDMI_PRODUCT_ID1);

	if (prod_id0 != HDMI_PRODUCT_ID0_HDMI_TX ||
	    (prod_id1 & ~HDMI_PRODUCT_ID1_HDCP) != HDMI_PRODUCT_ID1_HDMI_TX) {
		dev_err(dev, "Unsupported HDMI controller (%04x:%02x:%02x)\n",
			hdmi->version, prod_id0, prod_id1);
		ret = -ENODEV;
		goto err_iahb;
	}

	ret = dw_hdmi_detect_phy(hdmi);
	if (ret < 0)
		goto err_iahb;

	dev_info(dev, "Detected HDMI TX controller v%x.%03x %s HDCP (%s)\n",
		 hdmi->version >> 12, hdmi->version & 0xfff,
		 prod_id1 & HDMI_PRODUCT_ID1_HDCP ? "with" : "without",
		 hdmi->phy.name);

	init_hpd_work(hdmi);
	initialize_hdmi_ih_mutes(hdmi);

	ret = devm_request_threaded_irq(dev, irq, dw_hdmi_hardirq,
					dw_hdmi_irq, IRQF_SHARED,
					dev_name(dev), hdmi);
	if (ret)
		goto err_iahb;

	/*
	 * To prevent overflows in HDMI_IH_FC_STAT2, set the clk regenerator
	 * N and cts values before enabling phy
	 */
	hdmi_init_clk_regenerator(hdmi);

	hdmi_writeb(hdmi, HDMI_PHY_I2CM_INT_ADDR_DONE_POL,
		    HDMI_PHY_I2CM_INT_ADDR);

	hdmi_writeb(hdmi, HDMI_PHY_I2CM_CTLINT_ADDR_NAC_POL |
		    HDMI_PHY_I2CM_CTLINT_ADDR_ARBITRATION_POL,
		    HDMI_PHY_I2CM_CTLINT_ADDR);

	/* Re-init HPD polarity */
	hdmi_writeb(hdmi, HDMI_PHY_HPD | HDMI_PHY_RX_SENSE, HDMI_PHY_POL0);

	/* Unmask HPD, clear transitory interrupts, then unmute */
	hdmi_writeb(hdmi, hdmi->phy_mask, HDMI_PHY_MASK0);

	if (hdmi->dev_type == RK3288_HDMI && hdmi->version == 0x200a)
		hdmi->connector.ycbcr_420_allowed = false;
	else
		hdmi->connector.ycbcr_420_allowed = true;

	ret = dw_hdmi_register(drm, hdmi);
	if (ret)
		goto err_iahb;

#ifdef CONFIG_SWITCH
	hdmi->switchdev.name = "hdmi";
	switch_dev_register(&hdmi->switchdev);
#endif

	hdmi_writeb(hdmi, ~(HDMI_IH_PHY_STAT0_HPD | HDMI_IH_PHY_STAT0_RX_SENSE),
		    HDMI_IH_MUTE_PHY_STAT0);

	/* Unmute I2CM interrupts and reset HDMI DDC I2C master controller */
	if (hdmi->i2c)
		dw_hdmi_i2c_init(hdmi);

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.parent = dev;
	pdevinfo.id = PLATFORM_DEVID_AUTO;

	config0 = hdmi_readb(hdmi, HDMI_CONFIG0_ID);
	config3 = hdmi_readb(hdmi, HDMI_CONFIG3_ID);

	if (config3 & HDMI_CONFIG3_AHBAUDDMA) {
		struct dw_hdmi_audio_data audio;

		audio.phys = iores->start;
		audio.base = hdmi->regs;
		audio.irq = irq;
		audio.hdmi = hdmi;
		audio.eld = hdmi->connector.eld;

		pdevinfo.name = "dw-hdmi-ahb-audio";
		pdevinfo.data = &audio;
		pdevinfo.size_data = sizeof(audio);
		pdevinfo.dma_mask = DMA_BIT_MASK(32);
		hdmi->audio = platform_device_register_full(&pdevinfo);
	} else if (config0 & HDMI_CONFIG0_I2S) {
		struct dw_hdmi_i2s_audio_data audio;

		audio.hdmi	= hdmi;
		audio.write	= hdmi_writeb;
		audio.read	= hdmi_readb;
		audio.mod	= hdmi_modb;

		pdevinfo.name = "dw-hdmi-i2s-audio";
		pdevinfo.data = &audio;
		pdevinfo.size_data = sizeof(audio);
		pdevinfo.dma_mask = DMA_BIT_MASK(32);
		hdmi->audio = platform_device_register_full(&pdevinfo);
	}

	dev_set_drvdata(dev, hdmi);

	dw_hdmi_register_debugfs(dev, hdmi);

	if (of_property_read_bool(np, "hdcp1x-enable"))
		hdcp1x_enable = true;
	dw_hdmi_register_hdcp(dev, hdmi, val, hdcp1x_enable);

	return 0;

err_iahb:
	if (hdmi->i2c)
		i2c_del_adapter(&hdmi->i2c->adap);

	clk_disable_unprepare(hdmi->iahb_clk);
err_isfr:
	clk_disable_unprepare(hdmi->isfr_clk);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_hdmi_bind);

void dw_hdmi_unbind(struct device *dev, struct device *master, void *data)
{
	struct dw_hdmi *hdmi = dev_get_drvdata(dev);

	if (hdmi->irq)
		disable_irq(hdmi->irq);

	cancel_delayed_work(&hdmi->work);
	flush_workqueue(hdmi->workqueue);
	destroy_workqueue(hdmi->workqueue);

	debugfs_remove_recursive(hdmi->debugfs_dir);

	if (hdmi->audio && !IS_ERR(hdmi->audio))
		platform_device_unregister(hdmi->audio);

	if (hdmi->hdcp_dev && !IS_ERR(hdmi->hdcp_dev))
		platform_device_unregister(hdmi->hdcp_dev);

	/* Disable all interrupts */
	hdmi_writeb(hdmi, ~0, HDMI_IH_MUTE_PHY_STAT0);

#ifdef CONFIG_SWITCH
	switch_dev_unregister(&hdmi->switchdev);
#endif
	dw_hdmi_destroy_properties(hdmi);
	hdmi->connector.funcs->destroy(&hdmi->connector);
	hdmi->encoder->funcs->destroy(hdmi->encoder);

	clk_disable_unprepare(hdmi->iahb_clk);
	clk_disable_unprepare(hdmi->isfr_clk);

	if (hdmi->i2c)
		i2c_del_adapter(&hdmi->i2c->adap);
	else
		i2c_put_adapter(hdmi->ddc);
}
EXPORT_SYMBOL_GPL(dw_hdmi_unbind);

static void dw_hdmi_reg_initial(struct dw_hdmi *hdmi)
{
	if (hdmi_readb(hdmi, HDMI_IH_MUTE)) {
		initialize_hdmi_ih_mutes(hdmi);
		hdmi_writeb(hdmi, HDMI_PHY_I2CM_INT_ADDR_DONE_POL,
			    HDMI_PHY_I2CM_INT_ADDR);

		hdmi_writeb(hdmi, HDMI_PHY_I2CM_CTLINT_ADDR_NAC_POL |
			    HDMI_PHY_I2CM_CTLINT_ADDR_ARBITRATION_POL,
			    HDMI_PHY_I2CM_CTLINT_ADDR);

		hdmi_writeb(hdmi, HDMI_PHY_HPD | HDMI_PHY_RX_SENSE,
			    HDMI_PHY_POL0);
		hdmi_writeb(hdmi, hdmi->phy_mask, HDMI_PHY_MASK0);
		hdmi_writeb(hdmi, ~(HDMI_IH_PHY_STAT0_HPD |
			    HDMI_IH_PHY_STAT0_RX_SENSE),
			    HDMI_IH_MUTE_PHY_STAT0);
	}
}

void dw_hdmi_suspend(struct device *dev)
{
	struct dw_hdmi *hdmi = dev_get_drvdata(dev);

	mutex_lock(&hdmi->mutex);
	if (hdmi->irq)
		disable_irq(hdmi->irq);
	mutex_unlock(&hdmi->mutex);
	pinctrl_pm_select_sleep_state(dev);
}
EXPORT_SYMBOL_GPL(dw_hdmi_suspend);

void dw_hdmi_resume(struct device *dev)
{
	struct dw_hdmi *hdmi = dev_get_drvdata(dev);

	pinctrl_pm_select_default_state(dev);
	mutex_lock(&hdmi->mutex);
	dw_hdmi_reg_initial(hdmi);
	if (hdmi->i2c)
		dw_hdmi_i2c_init(hdmi);
	if (hdmi->irq)
		enable_irq(hdmi->irq);
	mutex_unlock(&hdmi->mutex);
}
EXPORT_SYMBOL_GPL(dw_hdmi_resume);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_AUTHOR("Yakir Yang <ykk@rock-chips.com>");
MODULE_AUTHOR("Vladimir Zapolskiy <vladimir_zapolskiy@mentor.com>");
MODULE_DESCRIPTION("DW HDMI transmitter driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dw-hdmi");
