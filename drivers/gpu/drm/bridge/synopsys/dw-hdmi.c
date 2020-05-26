// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DesignWare High-Definition Multimedia Interface (HDMI) driver
 *
 * Copyright (C) 2013-2015 Mentor Graphics Inc.
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc.
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>

#include <media/cec-notifier.h>

#include <uapi/linux/media-bus-format.h>
#include <uapi/linux/videodev2.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_scdc_helper.h>

#include "dw-hdmi-audio.h"
#include "dw-hdmi-cec.h"
#include "dw-hdmi.h"

#define DDC_CI_ADDR		0x37
#define DDC_SEGMENT_ADDR	0x30

#define HDMI_EDID_LEN		512

/* DW-HDMI Controller >= 0x200a are at least compliant with SCDC version 1 */
#define SCDC_MIN_SOURCE_VERSION	0x1

#define HDMI14_MAX_TMDSCLK	340000000

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

static const u16 csc_coeff_rgb_full_to_rgb_limited[3][4] = {
	{ 0x1b7c, 0x0000, 0x0000, 0x0020 },
	{ 0x0000, 0x1b7c, 0x0000, 0x0020 },
	{ 0x0000, 0x0000, 0x1b7c, 0x0020 }
};

struct hdmi_vmode {
	bool mdataenablepolarity;

	unsigned int mpixelclock;
	unsigned int mpixelrepetitioninput;
	unsigned int mpixelrepetitionoutput;
	unsigned int mtmdsclock;
};

struct hdmi_data_info {
	unsigned int enc_in_bus_format;
	unsigned int enc_out_bus_format;
	unsigned int enc_in_encoding;
	unsigned int enc_out_encoding;
	unsigned int pix_repet_factor;
	unsigned int hdcp_enable;
	struct hdmi_vmode video_mode;
	bool rgb_limited_range;
};

struct dw_hdmi_i2c {
	struct i2c_adapter	adap;

	struct mutex		lock;	/* used to serialize data transfers */
	struct completion	cmp;
	u8			stat;

	u8			slave_reg;
	bool			is_regaddr;
	bool			is_segment;
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
	struct drm_bridge bridge;

	unsigned int version;

	struct platform_device *audio;
	struct platform_device *cec;
	struct device *dev;
	struct clk *isfr_clk;
	struct clk *iahb_clk;
	struct clk *cec_clk;
	struct dw_hdmi_i2c *i2c;

	struct hdmi_data_info hdmi_data;
	const struct dw_hdmi_plat_data *plat_data;

	int vic;

	u8 edid[HDMI_EDID_LEN];

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

	struct pinctrl *pinctrl;
	struct pinctrl_state *default_state;
	struct pinctrl_state *unwedge_state;

	struct mutex mutex;		/* for state below and previous_mode */
	enum drm_connector_force force;	/* mutex-protected force state */
	struct drm_connector *curr_conn;/* current connector (only valid when !disabled) */
	bool disabled;			/* DRM has disabled our bridge */
	bool bridge_is_on;		/* indicates the bridge is on */
	bool rxsense;			/* rxsense state */
	u8 phy_mask;			/* desired phy int mask settings */
	u8 mc_clkdis;			/* clock disable register */

	spinlock_t audio_lock;
	struct mutex audio_mutex;
	unsigned int sample_rate;
	unsigned int audio_cts;
	unsigned int audio_n;
	bool audio_enable;

	unsigned int reg_shift;
	struct regmap *regm;
	void (*enable_audio)(struct dw_hdmi *hdmi);
	void (*disable_audio)(struct dw_hdmi *hdmi);

	struct mutex cec_notifier_mutex;
	struct cec_notifier *cec_notifier;

	hdmi_codec_plugged_cb plugged_cb;
	struct device *codec_dev;
	enum drm_connector_status last_connector_result;
};

#define HDMI_IH_PHY_STAT0_RX_SENSE \
	(HDMI_IH_PHY_STAT0_RX_SENSE0 | HDMI_IH_PHY_STAT0_RX_SENSE1 | \
	 HDMI_IH_PHY_STAT0_RX_SENSE2 | HDMI_IH_PHY_STAT0_RX_SENSE3)

#define HDMI_PHY_RX_SENSE \
	(HDMI_PHY_RX_SENSE0 | HDMI_PHY_RX_SENSE1 | \
	 HDMI_PHY_RX_SENSE2 | HDMI_PHY_RX_SENSE3)

static inline void hdmi_writeb(struct dw_hdmi *hdmi, u8 val, int offset)
{
	regmap_write(hdmi->regm, offset << hdmi->reg_shift, val);
}

static inline u8 hdmi_readb(struct dw_hdmi *hdmi, int offset)
{
	unsigned int val = 0;

	regmap_read(hdmi->regm, offset << hdmi->reg_shift, &val);

	return val;
}

static void handle_plugged_change(struct dw_hdmi *hdmi, bool plugged)
{
	if (hdmi->plugged_cb && hdmi->codec_dev)
		hdmi->plugged_cb(hdmi->codec_dev, plugged);
}

int dw_hdmi_set_plugged_cb(struct dw_hdmi *hdmi, hdmi_codec_plugged_cb fn,
			   struct device *codec_dev)
{
	bool plugged;

	mutex_lock(&hdmi->mutex);
	hdmi->plugged_cb = fn;
	hdmi->codec_dev = codec_dev;
	plugged = hdmi->last_connector_result == connector_status_connected;
	handle_plugged_change(hdmi, plugged);
	mutex_unlock(&hdmi->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(dw_hdmi_set_plugged_cb);

static void hdmi_modb(struct dw_hdmi *hdmi, u8 data, u8 mask, unsigned reg)
{
	regmap_update_bits(hdmi->regm, reg << hdmi->reg_shift, mask, data);
}

static void hdmi_mask_writeb(struct dw_hdmi *hdmi, u8 data, unsigned int reg,
			     u8 shift, u8 mask)
{
	hdmi_modb(hdmi, data << shift, mask, reg);
}

static void dw_hdmi_i2c_init(struct dw_hdmi *hdmi)
{
	hdmi_writeb(hdmi, HDMI_PHY_I2CM_INT_ADDR_DONE_POL,
		    HDMI_PHY_I2CM_INT_ADDR);

	hdmi_writeb(hdmi, HDMI_PHY_I2CM_CTLINT_ADDR_NAC_POL |
		    HDMI_PHY_I2CM_CTLINT_ADDR_ARBITRATION_POL,
		    HDMI_PHY_I2CM_CTLINT_ADDR);

	/* Software reset */
	hdmi_writeb(hdmi, 0x00, HDMI_I2CM_SOFTRSTZ);

	/* Set Standard Mode speed (determined to be 100KHz on iMX6) */
	hdmi_writeb(hdmi, 0x00, HDMI_I2CM_DIV);

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
}

static bool dw_hdmi_i2c_unwedge(struct dw_hdmi *hdmi)
{
	/* If no unwedge state then give up */
	if (!hdmi->unwedge_state)
		return false;

	dev_info(hdmi->dev, "Attempting to unwedge stuck i2c bus\n");

	/*
	 * This is a huge hack to workaround a problem where the dw_hdmi i2c
	 * bus could sometimes get wedged.  Once wedged there doesn't appear
	 * to be any way to unwedge it (including the HDMI_I2CM_SOFTRSTZ)
	 * other than pulsing the SDA line.
	 *
	 * We appear to be able to pulse the SDA line (in the eyes of dw_hdmi)
	 * by:
	 * 1. Remux the pin as a GPIO output, driven low.
	 * 2. Wait a little while.  1 ms seems to work, but we'll do 10.
	 * 3. Immediately jump to remux the pin as dw_hdmi i2c again.
	 *
	 * At the moment of remuxing, the line will still be low due to its
	 * recent stint as an output, but then it will be pulled high by the
	 * (presumed) external pullup.  dw_hdmi seems to see this as a rising
	 * edge and that seems to get it out of its jam.
	 *
	 * This wedging was only ever seen on one TV, and only on one of
	 * its HDMI ports.  It happened when the TV was powered on while the
	 * device was plugged in.  A scope trace shows the TV bringing both SDA
	 * and SCL low, then bringing them both back up at roughly the same
	 * time.  Presumably this confuses dw_hdmi because it saw activity but
	 * no real STOP (maybe it thinks there's another master on the bus?).
	 * Giving it a clean rising edge of SDA while SCL is already high
	 * presumably makes dw_hdmi see a STOP which seems to bring dw_hdmi out
	 * of its stupor.
	 *
	 * Note that after coming back alive, transfers seem to immediately
	 * resume, so if we unwedge due to a timeout we should wait a little
	 * longer for our transfer to finish, since it might have just started
	 * now.
	 */
	pinctrl_select_state(hdmi->pinctrl, hdmi->unwedge_state);
	msleep(10);
	pinctrl_select_state(hdmi->pinctrl, hdmi->default_state);

	return true;
}

static int dw_hdmi_i2c_wait(struct dw_hdmi *hdmi)
{
	struct dw_hdmi_i2c *i2c = hdmi->i2c;
	int stat;

	stat = wait_for_completion_timeout(&i2c->cmp, HZ / 10);
	if (!stat) {
		/* If we can't unwedge, return timeout */
		if (!dw_hdmi_i2c_unwedge(hdmi))
			return -EAGAIN;

		/* We tried to unwedge; give it another chance */
		stat = wait_for_completion_timeout(&i2c->cmp, HZ / 10);
		if (!stat)
			return -EAGAIN;
	}

	/* Check for error condition on the bus */
	if (i2c->stat & HDMI_IH_I2CM_STAT0_ERROR)
		return -EIO;

	return 0;
}

static int dw_hdmi_i2c_read(struct dw_hdmi *hdmi,
			    unsigned char *buf, unsigned int length)
{
	struct dw_hdmi_i2c *i2c = hdmi->i2c;
	int ret;

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

		ret = dw_hdmi_i2c_wait(hdmi);
		if (ret)
			return ret;

		*buf++ = hdmi_readb(hdmi, HDMI_I2CM_DATAI);
	}
	i2c->is_segment = false;

	return 0;
}

static int dw_hdmi_i2c_write(struct dw_hdmi *hdmi,
			     unsigned char *buf, unsigned int length)
{
	struct dw_hdmi_i2c *i2c = hdmi->i2c;
	int ret;

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

		ret = dw_hdmi_i2c_wait(hdmi);
		if (ret)
			return ret;
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

	if (addr == DDC_CI_ADDR)
		/*
		 * The internal I2C controller does not support the multi-byte
		 * read and write operations needed for DDC/CI.
		 * TOFIX: Blacklist the DDC/CI address until we filter out
		 * unsupported I2C operations.
		 */
		return -EOPNOTSUPP;

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

	/* Unmute DONE and ERROR interrupts */
	hdmi_writeb(hdmi, 0x00, HDMI_IH_MUTE_I2CM_STAT0);

	/* Set slave device address taken from the first I2C message */
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

	/* Use automatic CTS generation mode when CTS is not set */
	if (cts)
		hdmi_writeb(hdmi, ((cts >> 16) &
				   HDMI_AUD_CTS3_AUDCTS19_16_MASK) |
				  HDMI_AUD_CTS3_CTS_MANUAL,
			    HDMI_AUD_CTS3);
	else
		hdmi_writeb(hdmi, 0, HDMI_AUD_CTS3);
	hdmi_writeb(hdmi, (cts >> 8) & 0xff, HDMI_AUD_CTS2);
	hdmi_writeb(hdmi, cts & 0xff, HDMI_AUD_CTS1);

	hdmi_writeb(hdmi, (n >> 16) & 0x0f, HDMI_AUD_N3);
	hdmi_writeb(hdmi, (n >> 8) & 0xff, HDMI_AUD_N2);
	hdmi_writeb(hdmi, n & 0xff, HDMI_AUD_N1);
}

static unsigned int hdmi_compute_n(unsigned int freq, unsigned long pixel_clk)
{
	unsigned int n = (128 * freq) / 1000;
	unsigned int mult = 1;

	while (freq > 48000) {
		mult *= 2;
		freq /= 2;
	}

	switch (freq) {
	case 32000:
		if (pixel_clk == 25175000)
			n = 4576;
		else if (pixel_clk == 27027000)
			n = 4096;
		else if (pixel_clk == 74176000 || pixel_clk == 148352000)
			n = 11648;
		else
			n = 4096;
		n *= mult;
		break;

	case 44100:
		if (pixel_clk == 25175000)
			n = 7007;
		else if (pixel_clk == 74176000)
			n = 17836;
		else if (pixel_clk == 148352000)
			n = 8918;
		else
			n = 6272;
		n *= mult;
		break;

	case 48000:
		if (pixel_clk == 25175000)
			n = 6864;
		else if (pixel_clk == 27027000)
			n = 6144;
		else if (pixel_clk == 74176000)
			n = 11648;
		else if (pixel_clk == 148352000)
			n = 5824;
		else
			n = 6144;
		n *= mult;
		break;

	default:
		break;
	}

	return n;
}

/*
 * When transmitting IEC60958 linear PCM audio, these registers allow to
 * configure the channel status information of all the channel status
 * bits in the IEC60958 frame. For the moment this configuration is only
 * used when the I2S audio interface, General Purpose Audio (GPA),
 * or AHB audio DMA (AHBAUDDMA) interface is active
 * (for S/PDIF interface this information comes from the stream).
 */
void dw_hdmi_set_channel_status(struct dw_hdmi *hdmi,
				u8 *channel_status)
{
	/*
	 * Set channel status register for frequency and word length.
	 * Use default values for other registers.
	 */
	hdmi_writeb(hdmi, channel_status[3], HDMI_FC_AUDSCHNLS7);
	hdmi_writeb(hdmi, channel_status[4], HDMI_FC_AUDSCHNLS8);
}
EXPORT_SYMBOL_GPL(dw_hdmi_set_channel_status);

static void hdmi_set_clk_regenerator(struct dw_hdmi *hdmi,
	unsigned long pixel_clk, unsigned int sample_rate)
{
	unsigned long ftdms = pixel_clk;
	unsigned int n, cts;
	u8 config3;
	u64 tmp;

	n = hdmi_compute_n(sample_rate, pixel_clk);

	config3 = hdmi_readb(hdmi, HDMI_CONFIG3_ID);

	/* Only compute CTS when using internal AHB audio */
	if (config3 & HDMI_CONFIG3_AHBAUDDMA) {
		/*
		 * Compute the CTS value from the N value.  Note that CTS and N
		 * can be up to 20 bits in total, so we need 64-bit math.  Also
		 * note that our TDMS clock is not fully accurate; it is
		 * accurate to kHz.  This can introduce an unnecessary remainder
		 * in the calculation below, so we don't try to warn about that.
		 */
		tmp = (u64)ftdms * n;
		do_div(tmp, 128 * sample_rate);
		cts = tmp;

		dev_dbg(hdmi->dev, "%s: fs=%uHz ftdms=%lu.%03luMHz N=%d cts=%d\n",
			__func__, sample_rate,
			ftdms / 1000000, (ftdms / 1000) % 1000,
			n, cts);
	} else {
		cts = 0;
	}

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

void dw_hdmi_set_channel_count(struct dw_hdmi *hdmi, unsigned int cnt)
{
	u8 layout;

	mutex_lock(&hdmi->audio_mutex);

	/*
	 * For >2 channel PCM audio, we need to select layout 1
	 * and set an appropriate channel map.
	 */
	if (cnt > 2)
		layout = HDMI_FC_AUDSCONF_AUD_PACKET_LAYOUT_LAYOUT1;
	else
		layout = HDMI_FC_AUDSCONF_AUD_PACKET_LAYOUT_LAYOUT0;

	hdmi_modb(hdmi, layout, HDMI_FC_AUDSCONF_AUD_PACKET_LAYOUT_MASK,
		  HDMI_FC_AUDSCONF);

	/* Set the audio infoframes channel count */
	hdmi_modb(hdmi, (cnt - 1) << HDMI_FC_AUDICONF0_CC_OFFSET,
		  HDMI_FC_AUDICONF0_CC_MASK, HDMI_FC_AUDICONF0);

	mutex_unlock(&hdmi->audio_mutex);
}
EXPORT_SYMBOL_GPL(dw_hdmi_set_channel_count);

void dw_hdmi_set_channel_allocation(struct dw_hdmi *hdmi, unsigned int ca)
{
	mutex_lock(&hdmi->audio_mutex);

	hdmi_writeb(hdmi, ca, HDMI_FC_AUDICONF2);

	mutex_unlock(&hdmi->audio_mutex);
}
EXPORT_SYMBOL_GPL(dw_hdmi_set_channel_allocation);

static void hdmi_enable_audio_clk(struct dw_hdmi *hdmi, bool enable)
{
	if (enable)
		hdmi->mc_clkdis &= ~HDMI_MC_CLKDIS_AUDCLK_DISABLE;
	else
		hdmi->mc_clkdis |= HDMI_MC_CLKDIS_AUDCLK_DISABLE;
	hdmi_writeb(hdmi, hdmi->mc_clkdis, HDMI_MC_CLKDIS);
}

static void dw_hdmi_ahb_audio_enable(struct dw_hdmi *hdmi)
{
	hdmi_set_cts_n(hdmi, hdmi->audio_cts, hdmi->audio_n);
}

static void dw_hdmi_ahb_audio_disable(struct dw_hdmi *hdmi)
{
	hdmi_set_cts_n(hdmi, hdmi->audio_cts, 0);
}

static void dw_hdmi_i2s_audio_enable(struct dw_hdmi *hdmi)
{
	hdmi_set_cts_n(hdmi, hdmi->audio_cts, hdmi->audio_n);
	hdmi_enable_audio_clk(hdmi, true);
}

static void dw_hdmi_i2s_audio_disable(struct dw_hdmi *hdmi)
{
	hdmi_enable_audio_clk(hdmi, false);
}

void dw_hdmi_audio_enable(struct dw_hdmi *hdmi)
{
	unsigned long flags;

	spin_lock_irqsave(&hdmi->audio_lock, flags);
	hdmi->audio_enable = true;
	if (hdmi->enable_audio)
		hdmi->enable_audio(hdmi);
	spin_unlock_irqrestore(&hdmi->audio_lock, flags);
}
EXPORT_SYMBOL_GPL(dw_hdmi_audio_enable);

void dw_hdmi_audio_disable(struct dw_hdmi *hdmi)
{
	unsigned long flags;

	spin_lock_irqsave(&hdmi->audio_lock, flags);
	hdmi->audio_enable = false;
	if (hdmi->disable_audio)
		hdmi->disable_audio(hdmi);
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
	struct hdmi_data_info *hdmi_data = &hdmi->hdmi_data;
	bool is_input_rgb, is_output_rgb;

	is_input_rgb = hdmi_bus_fmt_is_rgb(hdmi_data->enc_in_bus_format);
	is_output_rgb = hdmi_bus_fmt_is_rgb(hdmi_data->enc_out_bus_format);

	return (is_input_rgb != is_output_rgb) ||
	       (is_input_rgb && is_output_rgb && hdmi_data->rgb_limited_range);
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

static bool is_csc_needed(struct dw_hdmi *hdmi)
{
	return is_color_space_conversion(hdmi) ||
	       is_color_space_decimation(hdmi) ||
	       is_color_space_interpolation(hdmi);
}

static void dw_hdmi_update_csc_coeffs(struct dw_hdmi *hdmi)
{
	const u16 (*csc_coeff)[3][4] = &csc_coeff_default;
	bool is_input_rgb, is_output_rgb;
	unsigned i;
	u32 csc_scale = 1;

	is_input_rgb = hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_in_bus_format);
	is_output_rgb = hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_out_bus_format);

	if (!is_input_rgb && is_output_rgb) {
		if (hdmi->hdmi_data.enc_out_encoding == V4L2_YCBCR_ENC_601)
			csc_coeff = &csc_coeff_rgb_out_eitu601;
		else
			csc_coeff = &csc_coeff_rgb_out_eitu709;
	} else if (is_input_rgb && !is_output_rgb) {
		if (hdmi->hdmi_data.enc_out_encoding == V4L2_YCBCR_ENC_601)
			csc_coeff = &csc_coeff_rgb_in_eitu601;
		else
			csc_coeff = &csc_coeff_rgb_in_eitu709;
		csc_scale = 0;
	} else if (is_input_rgb && is_output_rgb &&
		   hdmi->hdmi_data.rgb_limited_range) {
		csc_coeff = &csc_coeff_rgb_full_to_rgb_limited;
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
			color_depth = 4;
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
	val = ((color_depth << HDMI_VP_PR_CD_COLOR_DEPTH_OFFSET) &
		HDMI_VP_PR_CD_COLOR_DEPTH_MASK) |
		((hdmi_data->pix_repet_factor <<
		HDMI_VP_PR_CD_DESIRED_PR_FACTOR_OFFSET) &
		HDMI_VP_PR_CD_DESIRED_PR_FACTOR_MASK);
	hdmi_writeb(hdmi, val, HDMI_VP_PR_CD);

	hdmi_modb(hdmi, HDMI_VP_STUFF_PR_STUFFING_STUFFING_MODE,
		  HDMI_VP_STUFF_PR_STUFFING_MASK, HDMI_VP_STUFF);

	/* Data from pixel repeater block */
	if (hdmi_data->pix_repet_factor > 1) {
		vp_conf = HDMI_VP_CONF_PR_EN_ENABLE |
			  HDMI_VP_CONF_BYPASS_SELECT_PIX_REPEATER;
	} else { /* data from packetizer block */
		vp_conf = HDMI_VP_CONF_PR_EN_DISABLE |
			  HDMI_VP_CONF_BYPASS_SELECT_VID_PACKETIZER;
	}

	hdmi_modb(hdmi, vp_conf,
		  HDMI_VP_CONF_PR_EN_MASK |
		  HDMI_VP_CONF_BYPASS_SELECT_MASK, HDMI_VP_CONF);

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

/* Filter out invalid setups to avoid configuring SCDC and scrambling */
static bool dw_hdmi_support_scdc(struct dw_hdmi *hdmi,
				 const struct drm_display_info *display)
{
	/* Completely disable SCDC support for older controllers */
	if (hdmi->version < 0x200a)
		return false;

	/* Disable if no DDC bus */
	if (!hdmi->ddc)
		return false;

	/* Disable if SCDC is not supported, or if an HF-VSDB block is absent */
	if (!display->hdmi.scdc.supported ||
	    !display->hdmi.scdc.scrambling.supported)
		return false;

	/*
	 * Disable if display only support low TMDS rates and scrambling
	 * for low rates is not supported either
	 */
	if (!display->hdmi.scdc.scrambling.low_rates &&
	    display->max_tmds_clock <= 340000)
		return false;

	return true;
}

/*
 * HDMI2.0 Specifies the following procedure for High TMDS Bit Rates:
 * - The Source shall suspend transmission of the TMDS clock and data
 * - The Source shall write to the TMDS_Bit_Clock_Ratio bit to change it
 * from a 0 to a 1 or from a 1 to a 0
 * - The Source shall allow a minimum of 1 ms and a maximum of 100 ms from
 * the time the TMDS_Bit_Clock_Ratio bit is written until resuming
 * transmission of TMDS clock and data
 *
 * To respect the 100ms maximum delay, the dw_hdmi_set_high_tmds_clock_ratio()
 * helper should called right before enabling the TMDS Clock and Data in
 * the PHY configuration callback.
 */
void dw_hdmi_set_high_tmds_clock_ratio(struct dw_hdmi *hdmi,
				       const struct drm_display_info *display)
{
	unsigned long mtmdsclock = hdmi->hdmi_data.video_mode.mtmdsclock;

	/* Control for TMDS Bit Period/TMDS Clock-Period Ratio */
	if (dw_hdmi_support_scdc(hdmi, display)) {
		if (mtmdsclock > HDMI14_MAX_TMDSCLK)
			drm_scdc_set_high_tmds_clock_ratio(hdmi->ddc, 1);
		else
			drm_scdc_set_high_tmds_clock_ratio(hdmi->ddc, 0);
	}
}
EXPORT_SYMBOL_GPL(dw_hdmi_set_high_tmds_clock_ratio);

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

void dw_hdmi_phy_gen2_pddq(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_GEN2_PDDQ_OFFSET,
			 HDMI_PHY_CONF0_GEN2_PDDQ_MASK);
}
EXPORT_SYMBOL_GPL(dw_hdmi_phy_gen2_pddq);

void dw_hdmi_phy_gen2_txpwron(struct dw_hdmi *hdmi, u8 enable)
{
	hdmi_mask_writeb(hdmi, enable, HDMI_PHY_CONF0,
			 HDMI_PHY_CONF0_GEN2_TXPWRON_OFFSET,
			 HDMI_PHY_CONF0_GEN2_TXPWRON_MASK);
}
EXPORT_SYMBOL_GPL(dw_hdmi_phy_gen2_txpwron);

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

void dw_hdmi_phy_reset(struct dw_hdmi *hdmi)
{
	/* PHY reset. The reset signal is active high on Gen2 PHYs. */
	hdmi_writeb(hdmi, HDMI_MC_PHYRSTZ_PHYRSTZ, HDMI_MC_PHYRSTZ);
	hdmi_writeb(hdmi, 0, HDMI_MC_PHYRSTZ);
}
EXPORT_SYMBOL_GPL(dw_hdmi_phy_reset);

void dw_hdmi_phy_i2c_set_addr(struct dw_hdmi *hdmi, u8 address)
{
	hdmi_phy_test_clear(hdmi, 1);
	hdmi_writeb(hdmi, address, HDMI_PHY_I2CM_SLAVE_ADDR);
	hdmi_phy_test_clear(hdmi, 0);
}
EXPORT_SYMBOL_GPL(dw_hdmi_phy_i2c_set_addr);

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

	/* TOFIX Will need 420 specific PHY configuration tables */

	/* PLL/MPLL Cfg - always match on final entry */
	for (; mpll_config->mpixelclock != ~0UL; mpll_config++)
		if (mpixelclock <= mpll_config->mpixelclock)
			break;

	for (; curr_ctrl->mpixelclock != ~0UL; curr_ctrl++)
		if (mpixelclock <= curr_ctrl->mpixelclock)
			break;

	for (; phy_config->mpixelclock != ~0UL; phy_config++)
		if (mpixelclock <= phy_config->mpixelclock)
			break;

	if (mpll_config->mpixelclock == ~0UL ||
	    curr_ctrl->mpixelclock == ~0UL ||
	    phy_config->mpixelclock == ~0UL)
		return -EINVAL;

	dw_hdmi_phy_i2c_write(hdmi, mpll_config->res[0].cpce,
			      HDMI_3D_TX_PHY_CPCE_CTRL);
	dw_hdmi_phy_i2c_write(hdmi, mpll_config->res[0].gmp,
			      HDMI_3D_TX_PHY_GMPCTRL);
	dw_hdmi_phy_i2c_write(hdmi, curr_ctrl->curr[0],
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

static int hdmi_phy_configure(struct dw_hdmi *hdmi,
			      const struct drm_display_info *display)
{
	const struct dw_hdmi_phy_data *phy = hdmi->phy.data;
	const struct dw_hdmi_plat_data *pdata = hdmi->plat_data;
	unsigned long mpixelclock = hdmi->hdmi_data.video_mode.mpixelclock;
	unsigned long mtmdsclock = hdmi->hdmi_data.video_mode.mtmdsclock;
	int ret;

	dw_hdmi_phy_power_off(hdmi);

	dw_hdmi_set_high_tmds_clock_ratio(hdmi, display);

	/* Leave low power consumption mode by asserting SVSRET. */
	if (phy->has_svsret)
		dw_hdmi_phy_enable_svsret(hdmi, 1);

	dw_hdmi_phy_reset(hdmi);

	hdmi_writeb(hdmi, HDMI_MC_HEACPHY_RST_ASSERT, HDMI_MC_HEACPHY_RST);

	dw_hdmi_phy_i2c_set_addr(hdmi, HDMI_PHY_I2CM_SLAVE_ADDR_PHY_GEN2);

	/* Write to the PHY as configured by the platform */
	if (pdata->configure_phy)
		ret = pdata->configure_phy(hdmi, pdata->priv_data, mpixelclock);
	else
		ret = phy->configure(hdmi, pdata, mpixelclock);
	if (ret) {
		dev_err(hdmi->dev, "PHY configuration failed (clock %lu)\n",
			mpixelclock);
		return ret;
	}

	/* Wait for resuming transmission of TMDS clock and data */
	if (mtmdsclock > HDMI14_MAX_TMDSCLK)
		msleep(100);

	return dw_hdmi_phy_power_on(hdmi);
}

static int dw_hdmi_phy_init(struct dw_hdmi *hdmi, void *data,
			    const struct drm_display_info *display,
			    const struct drm_display_mode *mode)
{
	int i, ret;

	/* HDMI Phy spec says to do the phy initialization sequence twice */
	for (i = 0; i < 2; i++) {
		dw_hdmi_phy_sel_data_en_pol(hdmi, 1);
		dw_hdmi_phy_sel_interface_control(hdmi, 0);

		ret = hdmi_phy_configure(hdmi, display);
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

void dw_hdmi_phy_update_hpd(struct dw_hdmi *hdmi, void *data,
			    bool force, bool disabled, bool rxsense)
{
	u8 old_mask = hdmi->phy_mask;

	if (force || disabled || !rxsense)
		hdmi->phy_mask |= HDMI_PHY_RX_SENSE;
	else
		hdmi->phy_mask &= ~HDMI_PHY_RX_SENSE;

	if (old_mask != hdmi->phy_mask)
		hdmi_writeb(hdmi, hdmi->phy_mask, HDMI_PHY_MASK0);
}
EXPORT_SYMBOL_GPL(dw_hdmi_phy_update_hpd);

void dw_hdmi_phy_setup_hpd(struct dw_hdmi *hdmi, void *data)
{
	/*
	 * Configure the PHY RX SENSE and HPD interrupts polarities and clear
	 * any pending interrupt.
	 */
	hdmi_writeb(hdmi, HDMI_PHY_HPD | HDMI_PHY_RX_SENSE, HDMI_PHY_POL0);
	hdmi_writeb(hdmi, HDMI_IH_PHY_STAT0_HPD | HDMI_IH_PHY_STAT0_RX_SENSE,
		    HDMI_IH_PHY_STAT0);

	/* Enable cable hot plug irq. */
	hdmi_writeb(hdmi, hdmi->phy_mask, HDMI_PHY_MASK0);

	/* Clear and unmute interrupts. */
	hdmi_writeb(hdmi, HDMI_IH_PHY_STAT0_HPD | HDMI_IH_PHY_STAT0_RX_SENSE,
		    HDMI_IH_PHY_STAT0);
	hdmi_writeb(hdmi, ~(HDMI_IH_PHY_STAT0_HPD | HDMI_IH_PHY_STAT0_RX_SENSE),
		    HDMI_IH_MUTE_PHY_STAT0);
}
EXPORT_SYMBOL_GPL(dw_hdmi_phy_setup_hpd);

static const struct dw_hdmi_phy_ops dw_hdmi_synopsys_phy_ops = {
	.init = dw_hdmi_phy_init,
	.disable = dw_hdmi_phy_disable,
	.read_hpd = dw_hdmi_phy_read_hpd,
	.update_hpd = dw_hdmi_phy_update_hpd,
	.setup_hpd = dw_hdmi_phy_setup_hpd,
};

/* -----------------------------------------------------------------------------
 * HDMI TX Setup
 */

static void hdmi_tx_hdcp_config(struct dw_hdmi *hdmi)
{
	u8 de;

	if (hdmi->hdmi_data.video_mode.mdataenablepolarity)
		de = HDMI_A_VIDPOLCFG_DATAENPOL_ACTIVE_HIGH;
	else
		de = HDMI_A_VIDPOLCFG_DATAENPOL_ACTIVE_LOW;

	/* disable rx detect */
	hdmi_modb(hdmi, HDMI_A_HDCPCFG0_RXDETECT_DISABLE,
		  HDMI_A_HDCPCFG0_RXDETECT_MASK, HDMI_A_HDCPCFG0);

	hdmi_modb(hdmi, de, HDMI_A_VIDPOLCFG_DATAENPOL_MASK, HDMI_A_VIDPOLCFG);

	hdmi_modb(hdmi, HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE_DISABLE,
		  HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE_MASK, HDMI_A_HDCPCFG1);
}

static void hdmi_config_AVI(struct dw_hdmi *hdmi,
			    const struct drm_connector *connector,
			    const struct drm_display_mode *mode)
{
	struct hdmi_avi_infoframe frame;
	u8 val;

	/* Initialise info frame from DRM mode */
	drm_hdmi_avi_infoframe_from_display_mode(&frame, connector, mode);

	if (hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_out_bus_format)) {
		drm_hdmi_avi_infoframe_quant_range(&frame, connector, mode,
						   hdmi->hdmi_data.rgb_limited_range ?
						   HDMI_QUANTIZATION_RANGE_LIMITED :
						   HDMI_QUANTIZATION_RANGE_FULL);
	} else {
		frame.quantization_range = HDMI_QUANTIZATION_RANGE_DEFAULT;
		frame.ycc_quantization_range =
			HDMI_YCC_QUANTIZATION_RANGE_LIMITED;
	}

	if (hdmi_bus_fmt_is_yuv444(hdmi->hdmi_data.enc_out_bus_format))
		frame.colorspace = HDMI_COLORSPACE_YUV444;
	else if (hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_out_bus_format))
		frame.colorspace = HDMI_COLORSPACE_YUV422;
	else if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format))
		frame.colorspace = HDMI_COLORSPACE_YUV420;
	else
		frame.colorspace = HDMI_COLORSPACE_RGB;

	/* Set up colorimetry */
	if (!hdmi_bus_fmt_is_rgb(hdmi->hdmi_data.enc_out_bus_format)) {
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
		default: /* Carries no data */
			frame.colorimetry = HDMI_COLORIMETRY_ITU_601;
			frame.extended_colorimetry =
					HDMI_EXTENDED_COLORIMETRY_XV_YCC_601;
			break;
		}
	} else {
		frame.colorimetry = HDMI_COLORIMETRY_NONE;
		frame.extended_colorimetry =
			HDMI_EXTENDED_COLORIMETRY_XV_YCC_601;
	}

	/*
	 * The Designware IP uses a different byte format from standard
	 * AVI info frames, though generally the bits are in the correct
	 * bytes.
	 */

	/*
	 * AVI data byte 1 differences: Colorspace in bits 0,1 rather than 5,6,
	 * scan info in bits 4,5 rather than 0,1 and active aspect present in
	 * bit 6 rather than 4.
	 */
	val = (frame.scan_mode & 3) << 4 | (frame.colorspace & 3);
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
						  const struct drm_connector *connector,
						  const struct drm_display_mode *mode)
{
	struct hdmi_vendor_infoframe frame;
	u8 buffer[10];
	ssize_t err;

	err = drm_hdmi_vendor_infoframe_from_display_mode(&frame, connector,
							  mode);
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
	hdmi_mask_writeb(hdmi, 0, HDMI_FC_DATAUTO0, HDMI_FC_DATAUTO0_VSD_OFFSET,
			HDMI_FC_DATAUTO0_VSD_MASK);

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

static void hdmi_config_drm_infoframe(struct dw_hdmi *hdmi,
				      const struct drm_connector *connector)
{
	const struct drm_connector_state *conn_state = connector->state;
	struct hdmi_drm_infoframe frame;
	u8 buffer[30];
	ssize_t err;
	int i;

	if (!hdmi->plat_data->use_drm_infoframe)
		return;

	hdmi_modb(hdmi, HDMI_FC_PACKET_TX_EN_DRM_DISABLE,
		  HDMI_FC_PACKET_TX_EN_DRM_MASK, HDMI_FC_PACKET_TX_EN);

	err = drm_hdmi_infoframe_set_hdr_metadata(&frame, conn_state);
	if (err < 0)
		return;

	err = hdmi_drm_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "Failed to pack drm infoframe: %zd\n", err);
		return;
	}

	hdmi_writeb(hdmi, frame.version, HDMI_FC_DRM_HB0);
	hdmi_writeb(hdmi, frame.length, HDMI_FC_DRM_HB1);

	for (i = 0; i < frame.length; i++)
		hdmi_writeb(hdmi, buffer[4 + i], HDMI_FC_DRM_PB0 + i);

	hdmi_writeb(hdmi, 1, HDMI_FC_DRM_UP);
	hdmi_modb(hdmi, HDMI_FC_PACKET_TX_EN_DRM_ENABLE,
		  HDMI_FC_PACKET_TX_EN_DRM_MASK, HDMI_FC_PACKET_TX_EN);
}

static void hdmi_av_composer(struct dw_hdmi *hdmi,
			     const struct drm_display_info *display,
			     const struct drm_display_mode *mode)
{
	u8 inv_val, bytes;
	const struct drm_hdmi_info *hdmi_info = &display->hdmi;
	struct hdmi_vmode *vmode = &hdmi->hdmi_data.video_mode;
	int hblank, vblank, h_de_hs, v_de_vs, hsync_len, vsync_len;
	unsigned int vdisplay, hdisplay;

	vmode->mpixelclock = mode->clock * 1000;

	dev_dbg(hdmi->dev, "final pixclk = %d\n", vmode->mpixelclock);

	vmode->mtmdsclock = vmode->mpixelclock;

	if (!hdmi_bus_fmt_is_yuv422(hdmi->hdmi_data.enc_out_bus_format)) {
		switch (hdmi_bus_fmt_color_depth(
				hdmi->hdmi_data.enc_out_bus_format)) {
		case 16:
			vmode->mtmdsclock = vmode->mpixelclock * 2;
			break;
		case 12:
			vmode->mtmdsclock = vmode->mpixelclock * 3 / 2;
			break;
		case 10:
			vmode->mtmdsclock = vmode->mpixelclock * 5 / 4;
			break;
		}
	}

	if (hdmi_bus_fmt_is_yuv420(hdmi->hdmi_data.enc_out_bus_format))
		vmode->mtmdsclock /= 2;

	dev_dbg(hdmi->dev, "final tmdsclock = %d\n", vmode->mtmdsclock);

	/* Set up HDMI_FC_INVIDCONF */
	inv_val = (hdmi->hdmi_data.hdcp_enable ||
		   (dw_hdmi_support_scdc(hdmi, display) &&
		    (vmode->mtmdsclock > HDMI14_MAX_TMDSCLK ||
		     hdmi_info->scdc.scrambling.low_rates)) ?
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
	}

	/* Scrambling Control */
	if (dw_hdmi_support_scdc(hdmi, display)) {
		if (vmode->mtmdsclock > HDMI14_MAX_TMDSCLK ||
		    hdmi_info->scdc.scrambling.low_rates) {
			/*
			 * HDMI2.0 Specifies the following procedure:
			 * After the Source Device has determined that
			 * SCDC_Present is set (=1), the Source Device should
			 * write the accurate Version of the Source Device
			 * to the Source Version field in the SCDCS.
			 * Source Devices compliant shall set the
			 * Source Version = 1.
			 */
			drm_scdc_readb(hdmi->ddc, SCDC_SINK_VERSION,
				       &bytes);
			drm_scdc_writeb(hdmi->ddc, SCDC_SOURCE_VERSION,
				min_t(u8, bytes, SCDC_MIN_SOURCE_VERSION));

			/* Enabled Scrambling in the Sink */
			drm_scdc_set_scrambling(hdmi->ddc, 1);

			/*
			 * To activate the scrambler feature, you must ensure
			 * that the quasi-static configuration bit
			 * fc_invidconf.HDCP_keepout is set at configuration
			 * time, before the required mc_swrstzreq.tmdsswrst_req
			 * reset request is issued.
			 */
			hdmi_writeb(hdmi, (u8)~HDMI_MC_SWRSTZ_TMDSSWRST_REQ,
				    HDMI_MC_SWRSTZ);
			hdmi_writeb(hdmi, 1, HDMI_FC_SCRAMBLER_CTRL);
		} else {
			hdmi_writeb(hdmi, 0, HDMI_FC_SCRAMBLER_CTRL);
			hdmi_writeb(hdmi, (u8)~HDMI_MC_SWRSTZ_TMDSSWRST_REQ,
				    HDMI_MC_SWRSTZ);
			drm_scdc_set_scrambling(hdmi->ddc, 0);
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
	/* control period minimum duration */
	hdmi_writeb(hdmi, 12, HDMI_FC_CTRLDUR);
	hdmi_writeb(hdmi, 32, HDMI_FC_EXCTRLDUR);
	hdmi_writeb(hdmi, 1, HDMI_FC_EXCTRLSPAC);

	/* Set to fill TMDS data channels */
	hdmi_writeb(hdmi, 0x0B, HDMI_FC_CH0PREAM);
	hdmi_writeb(hdmi, 0x16, HDMI_FC_CH1PREAM);
	hdmi_writeb(hdmi, 0x21, HDMI_FC_CH2PREAM);

	/* Enable pixel clock and tmds data path */
	hdmi->mc_clkdis |= HDMI_MC_CLKDIS_HDCPCLK_DISABLE |
			   HDMI_MC_CLKDIS_CSCCLK_DISABLE |
			   HDMI_MC_CLKDIS_AUDCLK_DISABLE |
			   HDMI_MC_CLKDIS_PREPCLK_DISABLE |
			   HDMI_MC_CLKDIS_TMDSCLK_DISABLE;
	hdmi->mc_clkdis &= ~HDMI_MC_CLKDIS_PIXELCLK_DISABLE;
	hdmi_writeb(hdmi, hdmi->mc_clkdis, HDMI_MC_CLKDIS);

	hdmi->mc_clkdis &= ~HDMI_MC_CLKDIS_TMDSCLK_DISABLE;
	hdmi_writeb(hdmi, hdmi->mc_clkdis, HDMI_MC_CLKDIS);

	/* Enable csc path */
	if (is_csc_needed(hdmi)) {
		hdmi->mc_clkdis &= ~HDMI_MC_CLKDIS_CSCCLK_DISABLE;
		hdmi_writeb(hdmi, hdmi->mc_clkdis, HDMI_MC_CLKDIS);

		hdmi_writeb(hdmi, HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_IN_PATH,
			    HDMI_MC_FLOWCTRL);
	} else {
		hdmi->mc_clkdis |= HDMI_MC_CLKDIS_CSCCLK_DISABLE;
		hdmi_writeb(hdmi, hdmi->mc_clkdis, HDMI_MC_CLKDIS);

		hdmi_writeb(hdmi, HDMI_MC_FLOWCTRL_FEED_THROUGH_OFF_CSC_BYPASS,
			    HDMI_MC_FLOWCTRL);
	}
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
	 * (and possibly on the platform). So far i.MX6Q (v1.30a), i.MX6DL
	 * (v1.31a) and multiple Allwinner SoCs (v1.32a) have been identified
	 * as needing the workaround, with 4 iterations for v1.30a and 1
	 * iteration for others.
	 * The Amlogic Meson GX SoCs (v2.01a) have been identified as needing
	 * the workaround with a single iteration.
	 * The Rockchip RK3288 SoC (v2.00a) and RK3328/RK3399 SoCs (v2.11a) have
	 * been identified as needing the workaround with a single iteration.
	 */

	switch (hdmi->version) {
	case 0x130a:
		count = 4;
		break;
	case 0x131a:
	case 0x132a:
	case 0x200a:
	case 0x201a:
	case 0x211a:
	case 0x212a:
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

static void hdmi_disable_overflow_interrupts(struct dw_hdmi *hdmi)
{
	hdmi_writeb(hdmi, HDMI_IH_MUTE_FC_STAT2_OVERFLOW_MASK,
		    HDMI_IH_MUTE_FC_STAT2);
}

static int dw_hdmi_setup(struct dw_hdmi *hdmi,
			 const struct drm_connector *connector,
			 const struct drm_display_mode *mode)
{
	int ret;

	hdmi_disable_overflow_interrupts(hdmi);

	hdmi->vic = drm_match_cea_mode(mode);

	if (!hdmi->vic) {
		dev_dbg(hdmi->dev, "Non-CEA mode used in HDMI\n");
	} else {
		dev_dbg(hdmi->dev, "CEA mode used vic=%d\n", hdmi->vic);
	}

	if ((hdmi->vic == 6) || (hdmi->vic == 7) ||
	    (hdmi->vic == 21) || (hdmi->vic == 22) ||
	    (hdmi->vic == 2) || (hdmi->vic == 3) ||
	    (hdmi->vic == 17) || (hdmi->vic == 18))
		hdmi->hdmi_data.enc_out_encoding = V4L2_YCBCR_ENC_601;
	else
		hdmi->hdmi_data.enc_out_encoding = V4L2_YCBCR_ENC_709;

	hdmi->hdmi_data.video_mode.mpixelrepetitionoutput = 0;
	hdmi->hdmi_data.video_mode.mpixelrepetitioninput = 0;

	if (hdmi->hdmi_data.enc_in_bus_format == MEDIA_BUS_FMT_FIXED)
		hdmi->hdmi_data.enc_in_bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	/* TOFIX: Get input encoding from plat data or fallback to none */
	if (hdmi->plat_data->input_bus_encoding)
		hdmi->hdmi_data.enc_in_encoding =
			hdmi->plat_data->input_bus_encoding;
	else
		hdmi->hdmi_data.enc_in_encoding = V4L2_YCBCR_ENC_DEFAULT;

	if (hdmi->hdmi_data.enc_out_bus_format == MEDIA_BUS_FMT_FIXED)
		hdmi->hdmi_data.enc_out_bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	hdmi->hdmi_data.rgb_limited_range = hdmi->sink_is_hdmi &&
		drm_default_rgb_quant_range(mode) ==
		HDMI_QUANTIZATION_RANGE_LIMITED;

	hdmi->hdmi_data.pix_repet_factor = 0;
	hdmi->hdmi_data.hdcp_enable = 0;
	hdmi->hdmi_data.video_mode.mdataenablepolarity = true;

	/* HDMI Initialization Step B.1 */
	hdmi_av_composer(hdmi, &connector->display_info, mode);

	/* HDMI Initializateion Step B.2 */
	ret = hdmi->phy.ops->init(hdmi, hdmi->phy.data,
				  &connector->display_info,
				  &hdmi->previous_mode);
	if (ret)
		return ret;
	hdmi->phy.enabled = true;

	/* HDMI Initialization Step B.3 */
	dw_hdmi_enable_video_path(hdmi);

	if (hdmi->sink_has_audio) {
		dev_dbg(hdmi->dev, "sink has audio support\n");

		/* HDMI Initialization Step E - Configure audio */
		hdmi_clk_regenerator_update_pixel_clock(hdmi);
		hdmi_enable_audio_clk(hdmi, hdmi->audio_enable);
	}

	/* not for DVI mode */
	if (hdmi->sink_is_hdmi) {
		dev_dbg(hdmi->dev, "%s HDMI mode\n", __func__);

		/* HDMI Initialization Step F - Configure AVI InfoFrame */
		hdmi_config_AVI(hdmi, connector, mode);
		hdmi_config_vendor_specific_infoframe(hdmi, connector, mode);
		hdmi_config_drm_infoframe(hdmi, connector);
	} else {
		dev_dbg(hdmi->dev, "%s DVI mode\n", __func__);
	}

	hdmi_video_packetize(hdmi);
	hdmi_video_csc(hdmi);
	hdmi_video_sample(hdmi);
	hdmi_tx_hdcp_config(hdmi);

	dw_hdmi_clear_overflow(hdmi);

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

	/*
	 * The curr_conn field is guaranteed to be valid here, as this function
	 * is only be called when !hdmi->disabled.
	 */
	dw_hdmi_setup(hdmi, hdmi->curr_conn, &hdmi->previous_mode);
}

static void dw_hdmi_poweroff(struct dw_hdmi *hdmi)
{
	if (hdmi->phy.enabled) {
		hdmi->phy.ops->disable(hdmi, hdmi->phy.data);
		hdmi->phy.enabled = false;
	}

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
	if (hdmi->phy.ops->update_hpd)
		hdmi->phy.ops->update_hpd(hdmi, hdmi->phy.data,
					  hdmi->force, hdmi->disabled,
					  hdmi->rxsense);
}

static enum drm_connector_status dw_hdmi_detect(struct dw_hdmi *hdmi)
{
	enum drm_connector_status result;

	mutex_lock(&hdmi->mutex);
	hdmi->force = DRM_FORCE_UNSPECIFIED;
	dw_hdmi_update_power(hdmi);
	dw_hdmi_update_phy_mask(hdmi);
	mutex_unlock(&hdmi->mutex);

	result = hdmi->phy.ops->read_hpd(hdmi, hdmi->phy.data);

	mutex_lock(&hdmi->mutex);
	if (result != hdmi->last_connector_result) {
		dev_dbg(hdmi->dev, "read_hpd result: %d", result);
		handle_plugged_change(hdmi,
				      result == connector_status_connected);
		hdmi->last_connector_result = result;
	}
	mutex_unlock(&hdmi->mutex);

	return result;
}

static struct edid *dw_hdmi_get_edid(struct dw_hdmi *hdmi,
				     struct drm_connector *connector)
{
	struct edid *edid;

	if (!hdmi->ddc)
		return NULL;

	edid = drm_get_edid(connector, hdmi->ddc);
	if (!edid) {
		dev_dbg(hdmi->dev, "failed to get edid\n");
		return NULL;
	}

	dev_dbg(hdmi->dev, "got edid: width[%d] x height[%d]\n",
		edid->width_cm, edid->height_cm);

	hdmi->sink_is_hdmi = drm_detect_hdmi_monitor(edid);
	hdmi->sink_has_audio = drm_detect_monitor_audio(edid);

	return edid;
}

/* -----------------------------------------------------------------------------
 * DRM Connector Operations
 */

static enum drm_connector_status
dw_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);
	return dw_hdmi_detect(hdmi);
}

static int dw_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);
	struct edid *edid;
	int ret;

	edid = dw_hdmi_get_edid(hdmi, connector);
	if (!edid)
		return 0;

	drm_connector_update_edid_property(connector, edid);
	cec_notifier_set_phys_addr_from_edid(hdmi->cec_notifier, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);

	return ret;
}

static bool hdr_metadata_equal(const struct drm_connector_state *old_state,
			       const struct drm_connector_state *new_state)
{
	struct drm_property_blob *old_blob = old_state->hdr_output_metadata;
	struct drm_property_blob *new_blob = new_state->hdr_output_metadata;

	if (!old_blob || !new_blob)
		return old_blob == new_blob;

	if (old_blob->length != new_blob->length)
		return false;

	return !memcmp(old_blob->data, new_blob->data, old_blob->length);
}

static int dw_hdmi_connector_atomic_check(struct drm_connector *connector,
					  struct drm_atomic_state *state)
{
	struct drm_connector_state *old_state =
		drm_atomic_get_old_connector_state(state, connector);
	struct drm_connector_state *new_state =
		drm_atomic_get_new_connector_state(state, connector);
	struct drm_crtc *crtc = new_state->crtc;
	struct drm_crtc_state *crtc_state;

	if (!crtc)
		return 0;

	if (!hdr_metadata_equal(old_state, new_state)) {
		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		crtc_state->mode_changed = true;
	}

	return 0;
}

static void dw_hdmi_connector_force(struct drm_connector *connector)
{
	struct dw_hdmi *hdmi = container_of(connector, struct dw_hdmi,
					     connector);

	mutex_lock(&hdmi->mutex);
	hdmi->force = connector->force;
	dw_hdmi_update_power(hdmi);
	dw_hdmi_update_phy_mask(hdmi);
	mutex_unlock(&hdmi->mutex);
}

static const struct drm_connector_funcs dw_hdmi_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = dw_hdmi_connector_detect,
	.destroy = drm_connector_cleanup,
	.force = dw_hdmi_connector_force,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs dw_hdmi_connector_helper_funcs = {
	.get_modes = dw_hdmi_connector_get_modes,
	.atomic_check = dw_hdmi_connector_atomic_check,
};

static int dw_hdmi_connector_create(struct dw_hdmi *hdmi)
{
	struct drm_connector *connector = &hdmi->connector;
	struct cec_connector_info conn_info;
	struct cec_notifier *notifier;

	if (hdmi->version >= 0x200a)
		connector->ycbcr_420_allowed =
			hdmi->plat_data->ycbcr_420_allowed;
	else
		connector->ycbcr_420_allowed = false;

	connector->interlace_allowed = 1;
	connector->polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_helper_add(connector, &dw_hdmi_connector_helper_funcs);

	drm_connector_init_with_ddc(hdmi->bridge.dev, connector,
				    &dw_hdmi_connector_funcs,
				    DRM_MODE_CONNECTOR_HDMIA,
				    hdmi->ddc);

	/*
	 * drm_connector_attach_max_bpc_property() requires the
	 * connector to have a state.
	 */
	drm_atomic_helper_connector_reset(connector);

	drm_connector_attach_max_bpc_property(connector, 8, 16);

	if (hdmi->version >= 0x200a && hdmi->plat_data->use_drm_infoframe)
		drm_object_attach_property(&connector->base,
			connector->dev->mode_config.hdr_output_metadata_property, 0);

	drm_connector_attach_encoder(connector, hdmi->bridge.encoder);

	cec_fill_conn_info_from_drm(&conn_info, connector);

	notifier = cec_notifier_conn_register(hdmi->dev, NULL, &conn_info);
	if (!notifier)
		return -ENOMEM;

	mutex_lock(&hdmi->cec_notifier_mutex);
	hdmi->cec_notifier = notifier;
	mutex_unlock(&hdmi->cec_notifier_mutex);

	return 0;
}

/* -----------------------------------------------------------------------------
 * DRM Bridge Operations
 */

/*
 * Possible output formats :
 * - MEDIA_BUS_FMT_UYYVYY16_0_5X48,
 * - MEDIA_BUS_FMT_UYYVYY12_0_5X36,
 * - MEDIA_BUS_FMT_UYYVYY10_0_5X30,
 * - MEDIA_BUS_FMT_UYYVYY8_0_5X24,
 * - MEDIA_BUS_FMT_YUV16_1X48,
 * - MEDIA_BUS_FMT_RGB161616_1X48,
 * - MEDIA_BUS_FMT_UYVY12_1X24,
 * - MEDIA_BUS_FMT_YUV12_1X36,
 * - MEDIA_BUS_FMT_RGB121212_1X36,
 * - MEDIA_BUS_FMT_UYVY10_1X20,
 * - MEDIA_BUS_FMT_YUV10_1X30,
 * - MEDIA_BUS_FMT_RGB101010_1X30,
 * - MEDIA_BUS_FMT_UYVY8_1X16,
 * - MEDIA_BUS_FMT_YUV8_1X24,
 * - MEDIA_BUS_FMT_RGB888_1X24,
 */

/* Can return a maximum of 11 possible output formats for a mode/connector */
#define MAX_OUTPUT_SEL_FORMATS	11

static u32 *dw_hdmi_bridge_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					unsigned int *num_output_fmts)
{
	struct drm_connector *conn = conn_state->connector;
	struct drm_display_info *info = &conn->display_info;
	struct drm_display_mode *mode = &crtc_state->mode;
	u8 max_bpc = conn_state->max_requested_bpc;
	bool is_hdmi2_sink = info->hdmi.scdc.supported ||
			     (info->color_formats & DRM_COLOR_FORMAT_YCRCB420);
	u32 *output_fmts;
	unsigned int i = 0;

	*num_output_fmts = 0;

	output_fmts = kcalloc(MAX_OUTPUT_SEL_FORMATS, sizeof(*output_fmts),
			      GFP_KERNEL);
	if (!output_fmts)
		return NULL;

	/* If dw-hdmi is the only bridge, avoid negociating with ourselves */
	if (list_is_singular(&bridge->encoder->bridge_chain)) {
		*num_output_fmts = 1;
		output_fmts[0] = MEDIA_BUS_FMT_FIXED;

		return output_fmts;
	}

	/*
	 * If the current mode enforces 4:2:0, force the output but format
	 * to 4:2:0 and do not add the YUV422/444/RGB formats
	 */
	if (conn->ycbcr_420_allowed &&
	    (drm_mode_is_420_only(info, mode) ||
	     (is_hdmi2_sink && drm_mode_is_420_also(info, mode)))) {

		/* Order bus formats from 16bit to 8bit if supported */
		if (max_bpc >= 16 && info->bpc == 16 &&
		    (info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_48))
			output_fmts[i++] = MEDIA_BUS_FMT_UYYVYY16_0_5X48;

		if (max_bpc >= 12 && info->bpc >= 12 &&
		    (info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_36))
			output_fmts[i++] = MEDIA_BUS_FMT_UYYVYY12_0_5X36;

		if (max_bpc >= 10 && info->bpc >= 10 &&
		    (info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_30))
			output_fmts[i++] = MEDIA_BUS_FMT_UYYVYY10_0_5X30;

		/* Default 8bit fallback */
		output_fmts[i++] = MEDIA_BUS_FMT_UYYVYY8_0_5X24;

		*num_output_fmts = i;

		return output_fmts;
	}

	/*
	 * Order bus formats from 16bit to 8bit and from YUV422 to RGB
	 * if supported. In any case the default RGB888 format is added
	 */

	if (max_bpc >= 16 && info->bpc == 16) {
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			output_fmts[i++] = MEDIA_BUS_FMT_YUV16_1X48;

		output_fmts[i++] = MEDIA_BUS_FMT_RGB161616_1X48;
	}

	if (max_bpc >= 12 && info->bpc >= 12) {
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			output_fmts[i++] = MEDIA_BUS_FMT_UYVY12_1X24;

		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			output_fmts[i++] = MEDIA_BUS_FMT_YUV12_1X36;

		output_fmts[i++] = MEDIA_BUS_FMT_RGB121212_1X36;
	}

	if (max_bpc >= 10 && info->bpc >= 10) {
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			output_fmts[i++] = MEDIA_BUS_FMT_UYVY10_1X20;

		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			output_fmts[i++] = MEDIA_BUS_FMT_YUV10_1X30;

		output_fmts[i++] = MEDIA_BUS_FMT_RGB101010_1X30;
	}

	if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
		output_fmts[i++] = MEDIA_BUS_FMT_UYVY8_1X16;

	if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
		output_fmts[i++] = MEDIA_BUS_FMT_YUV8_1X24;

	/* Default 8bit RGB fallback */
	output_fmts[i++] = MEDIA_BUS_FMT_RGB888_1X24;

	*num_output_fmts = i;

	return output_fmts;
}

/*
 * Possible input formats :
 * - MEDIA_BUS_FMT_RGB888_1X24
 * - MEDIA_BUS_FMT_YUV8_1X24
 * - MEDIA_BUS_FMT_UYVY8_1X16
 * - MEDIA_BUS_FMT_UYYVYY8_0_5X24
 * - MEDIA_BUS_FMT_RGB101010_1X30
 * - MEDIA_BUS_FMT_YUV10_1X30
 * - MEDIA_BUS_FMT_UYVY10_1X20
 * - MEDIA_BUS_FMT_UYYVYY10_0_5X30
 * - MEDIA_BUS_FMT_RGB121212_1X36
 * - MEDIA_BUS_FMT_YUV12_1X36
 * - MEDIA_BUS_FMT_UYVY12_1X24
 * - MEDIA_BUS_FMT_UYYVYY12_0_5X36
 * - MEDIA_BUS_FMT_RGB161616_1X48
 * - MEDIA_BUS_FMT_YUV16_1X48
 * - MEDIA_BUS_FMT_UYYVYY16_0_5X48
 */

/* Can return a maximum of 3 possible input formats for an output format */
#define MAX_INPUT_SEL_FORMATS	3

static u32 *dw_hdmi_bridge_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					u32 output_fmt,
					unsigned int *num_input_fmts)
{
	u32 *input_fmts;
	unsigned int i = 0;

	*num_input_fmts = 0;

	input_fmts = kcalloc(MAX_INPUT_SEL_FORMATS, sizeof(*input_fmts),
			     GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	switch (output_fmt) {
	/* If MEDIA_BUS_FMT_FIXED is tested, return default bus format */
	case MEDIA_BUS_FMT_FIXED:
		input_fmts[i++] = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	/* 8bit */
	case MEDIA_BUS_FMT_RGB888_1X24:
		input_fmts[i++] = MEDIA_BUS_FMT_RGB888_1X24;
		input_fmts[i++] = MEDIA_BUS_FMT_YUV8_1X24;
		input_fmts[i++] = MEDIA_BUS_FMT_UYVY8_1X16;
		break;
	case MEDIA_BUS_FMT_YUV8_1X24:
		input_fmts[i++] = MEDIA_BUS_FMT_YUV8_1X24;
		input_fmts[i++] = MEDIA_BUS_FMT_UYVY8_1X16;
		input_fmts[i++] = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
		input_fmts[i++] = MEDIA_BUS_FMT_UYVY8_1X16;
		input_fmts[i++] = MEDIA_BUS_FMT_YUV8_1X24;
		input_fmts[i++] = MEDIA_BUS_FMT_RGB888_1X24;
		break;

	/* 10bit */
	case MEDIA_BUS_FMT_RGB101010_1X30:
		input_fmts[i++] = MEDIA_BUS_FMT_RGB101010_1X30;
		input_fmts[i++] = MEDIA_BUS_FMT_YUV10_1X30;
		input_fmts[i++] = MEDIA_BUS_FMT_UYVY10_1X20;
		break;
	case MEDIA_BUS_FMT_YUV10_1X30:
		input_fmts[i++] = MEDIA_BUS_FMT_YUV10_1X30;
		input_fmts[i++] = MEDIA_BUS_FMT_UYVY10_1X20;
		input_fmts[i++] = MEDIA_BUS_FMT_RGB101010_1X30;
		break;
	case MEDIA_BUS_FMT_UYVY10_1X20:
		input_fmts[i++] = MEDIA_BUS_FMT_UYVY10_1X20;
		input_fmts[i++] = MEDIA_BUS_FMT_YUV10_1X30;
		input_fmts[i++] = MEDIA_BUS_FMT_RGB101010_1X30;
		break;

	/* 12bit */
	case MEDIA_BUS_FMT_RGB121212_1X36:
		input_fmts[i++] = MEDIA_BUS_FMT_RGB121212_1X36;
		input_fmts[i++] = MEDIA_BUS_FMT_YUV12_1X36;
		input_fmts[i++] = MEDIA_BUS_FMT_UYVY12_1X24;
		break;
	case MEDIA_BUS_FMT_YUV12_1X36:
		input_fmts[i++] = MEDIA_BUS_FMT_YUV12_1X36;
		input_fmts[i++] = MEDIA_BUS_FMT_UYVY12_1X24;
		input_fmts[i++] = MEDIA_BUS_FMT_RGB121212_1X36;
		break;
	case MEDIA_BUS_FMT_UYVY12_1X24:
		input_fmts[i++] = MEDIA_BUS_FMT_UYVY12_1X24;
		input_fmts[i++] = MEDIA_BUS_FMT_YUV12_1X36;
		input_fmts[i++] = MEDIA_BUS_FMT_RGB121212_1X36;
		break;

	/* 16bit */
	case MEDIA_BUS_FMT_RGB161616_1X48:
		input_fmts[i++] = MEDIA_BUS_FMT_RGB161616_1X48;
		input_fmts[i++] = MEDIA_BUS_FMT_YUV16_1X48;
		break;
	case MEDIA_BUS_FMT_YUV16_1X48:
		input_fmts[i++] = MEDIA_BUS_FMT_YUV16_1X48;
		input_fmts[i++] = MEDIA_BUS_FMT_RGB161616_1X48;
		break;

	/*YUV 4:2:0 */
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
	case MEDIA_BUS_FMT_UYYVYY12_0_5X36:
	case MEDIA_BUS_FMT_UYYVYY16_0_5X48:
		input_fmts[i++] = output_fmt;
		break;
	}

	*num_input_fmts = i;

	if (*num_input_fmts == 0) {
		kfree(input_fmts);
		input_fmts = NULL;
	}

	return input_fmts;
}

static int dw_hdmi_bridge_atomic_check(struct drm_bridge *bridge,
				       struct drm_bridge_state *bridge_state,
				       struct drm_crtc_state *crtc_state,
				       struct drm_connector_state *conn_state)
{
	struct dw_hdmi *hdmi = bridge->driver_private;

	hdmi->hdmi_data.enc_out_bus_format =
			bridge_state->output_bus_cfg.format;

	hdmi->hdmi_data.enc_in_bus_format =
			bridge_state->input_bus_cfg.format;

	dev_dbg(hdmi->dev, "input format 0x%04x, output format 0x%04x\n",
		bridge_state->input_bus_cfg.format,
		bridge_state->output_bus_cfg.format);

	return 0;
}

static int dw_hdmi_bridge_attach(struct drm_bridge *bridge,
				 enum drm_bridge_attach_flags flags)
{
	struct dw_hdmi *hdmi = bridge->driver_private;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	return dw_hdmi_connector_create(hdmi);
}

static void dw_hdmi_bridge_detach(struct drm_bridge *bridge)
{
	struct dw_hdmi *hdmi = bridge->driver_private;

	mutex_lock(&hdmi->cec_notifier_mutex);
	cec_notifier_conn_unregister(hdmi->cec_notifier);
	hdmi->cec_notifier = NULL;
	mutex_unlock(&hdmi->cec_notifier_mutex);
}

static enum drm_mode_status
dw_hdmi_bridge_mode_valid(struct drm_bridge *bridge,
			  const struct drm_display_info *info,
			  const struct drm_display_mode *mode)
{
	struct dw_hdmi *hdmi = bridge->driver_private;
	const struct dw_hdmi_plat_data *pdata = hdmi->plat_data;
	enum drm_mode_status mode_status = MODE_OK;

	/* We don't support double-clocked modes */
	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		return MODE_BAD;

	if (pdata->mode_valid)
		mode_status = pdata->mode_valid(hdmi, pdata->priv_data, info,
						mode);

	return mode_status;
}

static void dw_hdmi_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *orig_mode,
				    const struct drm_display_mode *mode)
{
	struct dw_hdmi *hdmi = bridge->driver_private;

	mutex_lock(&hdmi->mutex);

	/* Store the display mode for plugin/DKMS poweron events */
	memcpy(&hdmi->previous_mode, mode, sizeof(hdmi->previous_mode));

	mutex_unlock(&hdmi->mutex);
}

static void dw_hdmi_bridge_atomic_disable(struct drm_bridge *bridge,
					  struct drm_bridge_state *old_state)
{
	struct dw_hdmi *hdmi = bridge->driver_private;

	mutex_lock(&hdmi->mutex);
	hdmi->disabled = true;
	hdmi->curr_conn = NULL;
	dw_hdmi_update_power(hdmi);
	dw_hdmi_update_phy_mask(hdmi);
	mutex_unlock(&hdmi->mutex);
}

static void dw_hdmi_bridge_atomic_enable(struct drm_bridge *bridge,
					 struct drm_bridge_state *old_state)
{
	struct dw_hdmi *hdmi = bridge->driver_private;
	struct drm_atomic_state *state = old_state->base.state;
	struct drm_connector *connector;

	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);

	mutex_lock(&hdmi->mutex);
	hdmi->disabled = false;
	hdmi->curr_conn = connector;
	dw_hdmi_update_power(hdmi);
	dw_hdmi_update_phy_mask(hdmi);
	mutex_unlock(&hdmi->mutex);
}

static enum drm_connector_status dw_hdmi_bridge_detect(struct drm_bridge *bridge)
{
	struct dw_hdmi *hdmi = bridge->driver_private;

	return dw_hdmi_detect(hdmi);
}

static struct edid *dw_hdmi_bridge_get_edid(struct drm_bridge *bridge,
					    struct drm_connector *connector)
{
	struct dw_hdmi *hdmi = bridge->driver_private;

	return dw_hdmi_get_edid(hdmi, connector);
}

static const struct drm_bridge_funcs dw_hdmi_bridge_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.attach = dw_hdmi_bridge_attach,
	.detach = dw_hdmi_bridge_detach,
	.atomic_check = dw_hdmi_bridge_atomic_check,
	.atomic_get_output_bus_fmts = dw_hdmi_bridge_atomic_get_output_bus_fmts,
	.atomic_get_input_bus_fmts = dw_hdmi_bridge_atomic_get_input_bus_fmts,
	.atomic_enable = dw_hdmi_bridge_atomic_enable,
	.atomic_disable = dw_hdmi_bridge_atomic_disable,
	.mode_set = dw_hdmi_bridge_mode_set,
	.mode_valid = dw_hdmi_bridge_mode_valid,
	.detect = dw_hdmi_bridge_detect,
	.get_edid = dw_hdmi_bridge_get_edid,
};

/* -----------------------------------------------------------------------------
 * IRQ Handling
 */

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
	u8 intr_stat;
	irqreturn_t ret = IRQ_NONE;

	if (hdmi->i2c)
		ret = dw_hdmi_i2c_irq(hdmi);

	intr_stat = hdmi_readb(hdmi, HDMI_IH_PHY_STAT0);
	if (intr_stat) {
		hdmi_writeb(hdmi, ~0, HDMI_IH_MUTE_PHY_STAT0);
		return IRQ_WAKE_THREAD;
	}

	return ret;
}

void dw_hdmi_setup_rx_sense(struct dw_hdmi *hdmi, bool hpd, bool rx_sense)
{
	mutex_lock(&hdmi->mutex);

	if (!hdmi->force) {
		/*
		 * If the RX sense status indicates we're disconnected,
		 * clear the software rxsense status.
		 */
		if (!rx_sense)
			hdmi->rxsense = false;

		/*
		 * Only set the software rxsense status when both
		 * rxsense and hpd indicates we're connected.
		 * This avoids what seems to be bad behaviour in
		 * at least iMX6S versions of the phy.
		 */
		if (hpd)
			hdmi->rxsense = true;

		dw_hdmi_update_power(hdmi);
		dw_hdmi_update_phy_mask(hdmi);
	}
	mutex_unlock(&hdmi->mutex);
}
EXPORT_SYMBOL_GPL(dw_hdmi_setup_rx_sense);

static irqreturn_t dw_hdmi_irq(int irq, void *dev_id)
{
	struct dw_hdmi *hdmi = dev_id;
	u8 intr_stat, phy_int_pol, phy_pol_mask, phy_stat;

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
		dw_hdmi_setup_rx_sense(hdmi,
				       phy_stat & HDMI_PHY_HPD,
				       phy_stat & HDMI_PHY_RX_SENSE);

		if ((phy_stat & (HDMI_PHY_RX_SENSE | HDMI_PHY_HPD)) == 0) {
			mutex_lock(&hdmi->cec_notifier_mutex);
			cec_notifier_phys_addr_invalidate(hdmi->cec_notifier);
			mutex_unlock(&hdmi->cec_notifier_mutex);
		}
	}

	if (intr_stat & HDMI_IH_PHY_STAT0_HPD) {
		enum drm_connector_status status = phy_int_pol & HDMI_PHY_HPD
						 ? connector_status_connected
						 : connector_status_disconnected;

		dev_dbg(hdmi->dev, "EVENT=%s\n",
			status == connector_status_connected ?
			"plugin" : "plugout");

		if (hdmi->bridge.dev) {
			drm_helper_hpd_irq_event(hdmi->bridge.dev);
			drm_bridge_hpd_notify(&hdmi->bridge, status);
		}
	}

	hdmi_writeb(hdmi, intr_stat, HDMI_IH_PHY_STAT0);
	hdmi_writeb(hdmi, ~(HDMI_IH_PHY_STAT0_HPD | HDMI_IH_PHY_STAT0_RX_SENSE),
		    HDMI_IH_MUTE_PHY_STAT0);

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

	phy_type = hdmi->plat_data->phy_force_vendor ?
				DW_HDMI_PHY_VENDOR_PHY :
				hdmi_readb(hdmi, HDMI_CONFIG2_ID);

	if (phy_type == DW_HDMI_PHY_VENDOR_PHY) {
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

static void dw_hdmi_cec_enable(struct dw_hdmi *hdmi)
{
	mutex_lock(&hdmi->mutex);
	hdmi->mc_clkdis &= ~HDMI_MC_CLKDIS_CECCLK_DISABLE;
	hdmi_writeb(hdmi, hdmi->mc_clkdis, HDMI_MC_CLKDIS);
	mutex_unlock(&hdmi->mutex);
}

static void dw_hdmi_cec_disable(struct dw_hdmi *hdmi)
{
	mutex_lock(&hdmi->mutex);
	hdmi->mc_clkdis |= HDMI_MC_CLKDIS_CECCLK_DISABLE;
	hdmi_writeb(hdmi, hdmi->mc_clkdis, HDMI_MC_CLKDIS);
	mutex_unlock(&hdmi->mutex);
}

static const struct dw_hdmi_cec_ops dw_hdmi_cec_ops = {
	.write = hdmi_writeb,
	.read = hdmi_readb,
	.enable = dw_hdmi_cec_enable,
	.disable = dw_hdmi_cec_disable,
};

static const struct regmap_config hdmi_regmap_8bit_config = {
	.reg_bits	= 32,
	.val_bits	= 8,
	.reg_stride	= 1,
	.max_register	= HDMI_I2CM_FS_SCL_LCNT_0_ADDR,
};

static const struct regmap_config hdmi_regmap_32bit_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= HDMI_I2CM_FS_SCL_LCNT_0_ADDR << 2,
};

static void dw_hdmi_init_hw(struct dw_hdmi *hdmi)
{
	initialize_hdmi_ih_mutes(hdmi);

	/*
	 * Reset HDMI DDC I2C master controller and mute I2CM interrupts.
	 * Even if we are using a separate i2c adapter doing this doesn't
	 * hurt.
	 */
	dw_hdmi_i2c_init(hdmi);

	if (hdmi->phy.ops->setup_hpd)
		hdmi->phy.ops->setup_hpd(hdmi, hdmi->phy.data);
}

static struct dw_hdmi *
__dw_hdmi_probe(struct platform_device *pdev,
		const struct dw_hdmi_plat_data *plat_data)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct platform_device_info pdevinfo;
	struct device_node *ddc_node;
	struct dw_hdmi_cec_data cec;
	struct dw_hdmi *hdmi;
	struct resource *iores = NULL;
	int irq;
	int ret;
	u32 val = 1;
	u8 prod_id0;
	u8 prod_id1;
	u8 config0;
	u8 config3;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return ERR_PTR(-ENOMEM);

	hdmi->plat_data = plat_data;
	hdmi->dev = dev;
	hdmi->sample_rate = 48000;
	hdmi->disabled = true;
	hdmi->rxsense = true;
	hdmi->phy_mask = (u8)~(HDMI_PHY_HPD | HDMI_PHY_RX_SENSE);
	hdmi->mc_clkdis = 0x7f;
	hdmi->last_connector_result = connector_status_disconnected;

	mutex_init(&hdmi->mutex);
	mutex_init(&hdmi->audio_mutex);
	mutex_init(&hdmi->cec_notifier_mutex);
	spin_lock_init(&hdmi->audio_lock);

	ddc_node = of_parse_phandle(np, "ddc-i2c-bus", 0);
	if (ddc_node) {
		hdmi->ddc = of_get_i2c_adapter_by_node(ddc_node);
		of_node_put(ddc_node);
		if (!hdmi->ddc) {
			dev_dbg(hdmi->dev, "failed to read ddc node\n");
			return ERR_PTR(-EPROBE_DEFER);
		}

	} else {
		dev_dbg(hdmi->dev, "no ddc property found\n");
	}

	if (!plat_data->regm) {
		const struct regmap_config *reg_config;

		of_property_read_u32(np, "reg-io-width", &val);
		switch (val) {
		case 4:
			reg_config = &hdmi_regmap_32bit_config;
			hdmi->reg_shift = 2;
			break;
		case 1:
			reg_config = &hdmi_regmap_8bit_config;
			break;
		default:
			dev_err(dev, "reg-io-width must be 1 or 4\n");
			return ERR_PTR(-EINVAL);
		}

		iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		hdmi->regs = devm_ioremap_resource(dev, iores);
		if (IS_ERR(hdmi->regs)) {
			ret = PTR_ERR(hdmi->regs);
			goto err_res;
		}

		hdmi->regm = devm_regmap_init_mmio(dev, hdmi->regs, reg_config);
		if (IS_ERR(hdmi->regm)) {
			dev_err(dev, "Failed to configure regmap\n");
			ret = PTR_ERR(hdmi->regm);
			goto err_res;
		}
	} else {
		hdmi->regm = plat_data->regm;
	}

	hdmi->isfr_clk = devm_clk_get(hdmi->dev, "isfr");
	if (IS_ERR(hdmi->isfr_clk)) {
		ret = PTR_ERR(hdmi->isfr_clk);
		dev_err(hdmi->dev, "Unable to get HDMI isfr clk: %d\n", ret);
		goto err_res;
	}

	ret = clk_prepare_enable(hdmi->isfr_clk);
	if (ret) {
		dev_err(hdmi->dev, "Cannot enable HDMI isfr clock: %d\n", ret);
		goto err_res;
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

	hdmi->cec_clk = devm_clk_get(hdmi->dev, "cec");
	if (PTR_ERR(hdmi->cec_clk) == -ENOENT) {
		hdmi->cec_clk = NULL;
	} else if (IS_ERR(hdmi->cec_clk)) {
		ret = PTR_ERR(hdmi->cec_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(hdmi->dev, "Cannot get HDMI cec clock: %d\n",
				ret);

		hdmi->cec_clk = NULL;
		goto err_iahb;
	} else {
		ret = clk_prepare_enable(hdmi->cec_clk);
		if (ret) {
			dev_err(hdmi->dev, "Cannot enable HDMI cec clock: %d\n",
				ret);
			goto err_iahb;
		}
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

	dw_hdmi_init_hw(hdmi);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_iahb;
	}

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

	/* If DDC bus is not specified, try to register HDMI I2C bus */
	if (!hdmi->ddc) {
		/* Look for (optional) stuff related to unwedging */
		hdmi->pinctrl = devm_pinctrl_get(dev);
		if (!IS_ERR(hdmi->pinctrl)) {
			hdmi->unwedge_state =
				pinctrl_lookup_state(hdmi->pinctrl, "unwedge");
			hdmi->default_state =
				pinctrl_lookup_state(hdmi->pinctrl, "default");

			if (IS_ERR(hdmi->default_state) ||
			    IS_ERR(hdmi->unwedge_state)) {
				if (!IS_ERR(hdmi->unwedge_state))
					dev_warn(dev,
						 "Unwedge requires default pinctrl\n");
				hdmi->default_state = NULL;
				hdmi->unwedge_state = NULL;
			}
		}

		hdmi->ddc = dw_hdmi_i2c_adapter(hdmi);
		if (IS_ERR(hdmi->ddc))
			hdmi->ddc = NULL;
	}

	hdmi->bridge.driver_private = hdmi;
	hdmi->bridge.funcs = &dw_hdmi_bridge_funcs;
	hdmi->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID
			 | DRM_BRIDGE_OP_HPD;
#ifdef CONFIG_OF
	hdmi->bridge.of_node = pdev->dev.of_node;
#endif

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.parent = dev;
	pdevinfo.id = PLATFORM_DEVID_AUTO;

	config0 = hdmi_readb(hdmi, HDMI_CONFIG0_ID);
	config3 = hdmi_readb(hdmi, HDMI_CONFIG3_ID);

	if (iores && config3 & HDMI_CONFIG3_AHBAUDDMA) {
		struct dw_hdmi_audio_data audio;

		audio.phys = iores->start;
		audio.base = hdmi->regs;
		audio.irq = irq;
		audio.hdmi = hdmi;
		audio.eld = hdmi->connector.eld;
		hdmi->enable_audio = dw_hdmi_ahb_audio_enable;
		hdmi->disable_audio = dw_hdmi_ahb_audio_disable;

		pdevinfo.name = "dw-hdmi-ahb-audio";
		pdevinfo.data = &audio;
		pdevinfo.size_data = sizeof(audio);
		pdevinfo.dma_mask = DMA_BIT_MASK(32);
		hdmi->audio = platform_device_register_full(&pdevinfo);
	} else if (config0 & HDMI_CONFIG0_I2S) {
		struct dw_hdmi_i2s_audio_data audio;

		audio.hdmi	= hdmi;
		audio.eld	= hdmi->connector.eld;
		audio.write	= hdmi_writeb;
		audio.read	= hdmi_readb;
		hdmi->enable_audio = dw_hdmi_i2s_audio_enable;
		hdmi->disable_audio = dw_hdmi_i2s_audio_disable;

		pdevinfo.name = "dw-hdmi-i2s-audio";
		pdevinfo.data = &audio;
		pdevinfo.size_data = sizeof(audio);
		pdevinfo.dma_mask = DMA_BIT_MASK(32);
		hdmi->audio = platform_device_register_full(&pdevinfo);
	}

	if (config0 & HDMI_CONFIG0_CEC) {
		cec.hdmi = hdmi;
		cec.ops = &dw_hdmi_cec_ops;
		cec.irq = irq;

		pdevinfo.name = "dw-hdmi-cec";
		pdevinfo.data = &cec;
		pdevinfo.size_data = sizeof(cec);
		pdevinfo.dma_mask = 0;

		hdmi->cec = platform_device_register_full(&pdevinfo);
	}

	return hdmi;

err_iahb:
	if (hdmi->i2c) {
		i2c_del_adapter(&hdmi->i2c->adap);
		hdmi->ddc = NULL;
	}

	clk_disable_unprepare(hdmi->iahb_clk);
	if (hdmi->cec_clk)
		clk_disable_unprepare(hdmi->cec_clk);
err_isfr:
	clk_disable_unprepare(hdmi->isfr_clk);
err_res:
	i2c_put_adapter(hdmi->ddc);

	return ERR_PTR(ret);
}

static void __dw_hdmi_remove(struct dw_hdmi *hdmi)
{
	if (hdmi->audio && !IS_ERR(hdmi->audio))
		platform_device_unregister(hdmi->audio);
	if (!IS_ERR(hdmi->cec))
		platform_device_unregister(hdmi->cec);

	/* Disable all interrupts */
	hdmi_writeb(hdmi, ~0, HDMI_IH_MUTE_PHY_STAT0);

	clk_disable_unprepare(hdmi->iahb_clk);
	clk_disable_unprepare(hdmi->isfr_clk);
	if (hdmi->cec_clk)
		clk_disable_unprepare(hdmi->cec_clk);

	if (hdmi->i2c)
		i2c_del_adapter(&hdmi->i2c->adap);
	else
		i2c_put_adapter(hdmi->ddc);
}

/* -----------------------------------------------------------------------------
 * Probe/remove API, used from platforms based on the DRM bridge API.
 */
struct dw_hdmi *dw_hdmi_probe(struct platform_device *pdev,
			      const struct dw_hdmi_plat_data *plat_data)
{
	struct dw_hdmi *hdmi;

	hdmi = __dw_hdmi_probe(pdev, plat_data);
	if (IS_ERR(hdmi))
		return hdmi;

	drm_bridge_add(&hdmi->bridge);

	return hdmi;
}
EXPORT_SYMBOL_GPL(dw_hdmi_probe);

void dw_hdmi_remove(struct dw_hdmi *hdmi)
{
	drm_bridge_remove(&hdmi->bridge);

	__dw_hdmi_remove(hdmi);
}
EXPORT_SYMBOL_GPL(dw_hdmi_remove);

/* -----------------------------------------------------------------------------
 * Bind/unbind API, used from platforms based on the component framework.
 */
struct dw_hdmi *dw_hdmi_bind(struct platform_device *pdev,
			     struct drm_encoder *encoder,
			     const struct dw_hdmi_plat_data *plat_data)
{
	struct dw_hdmi *hdmi;
	int ret;

	hdmi = __dw_hdmi_probe(pdev, plat_data);
	if (IS_ERR(hdmi))
		return hdmi;

	ret = drm_bridge_attach(encoder, &hdmi->bridge, NULL, 0);
	if (ret) {
		dw_hdmi_remove(hdmi);
		DRM_ERROR("Failed to initialize bridge with drm\n");
		return ERR_PTR(ret);
	}

	return hdmi;
}
EXPORT_SYMBOL_GPL(dw_hdmi_bind);

void dw_hdmi_unbind(struct dw_hdmi *hdmi)
{
	__dw_hdmi_remove(hdmi);
}
EXPORT_SYMBOL_GPL(dw_hdmi_unbind);

void dw_hdmi_resume(struct dw_hdmi *hdmi)
{
	dw_hdmi_init_hw(hdmi);
}
EXPORT_SYMBOL_GPL(dw_hdmi_resume);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_AUTHOR("Yakir Yang <ykk@rock-chips.com>");
MODULE_AUTHOR("Vladimir Zapolskiy <vladimir_zapolskiy@mentor.com>");
MODULE_DESCRIPTION("DW HDMI transmitter driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dw-hdmi");
