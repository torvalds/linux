// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2022 Rockchip Electronics Co., Ltd.
 * Copyright (c) 2024 Collabora Ltd.
 * Copyright (c) 2025 Amazon.com, Inc. or its affiliates.
 *
 * Author: Algea Cao <algea.cao@rock-chips.com>
 * Author: Cristian Ciocaltea <cristian.ciocaltea@collabora.com>
 */
#include <linux/completion.h>
#include <linux/hdmi.h>
#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>

#include <drm/bridge/dw_hdmi_qp.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_hdmi_cec_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_modes.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include <media/cec.h>

#include <sound/hdmi-codec.h>

#include "dw-hdmi-qp.h"

#define DDC_CI_ADDR		0x37
#define DDC_SEGMENT_ADDR	0x30

struct dw_hdmi_qp_i2c {
	struct i2c_adapter	adap;

	struct mutex		lock;	/* used to serialize data transfers */
	struct completion	cmp;
	u8			stat;

	u8			slave_reg;
	bool			is_regaddr;
	bool			is_segment;
};

#ifdef CONFIG_DRM_DW_HDMI_QP_CEC
struct dw_hdmi_qp_cec {
	struct drm_connector *connector;
	int irq;
	u32 addresses;
	struct cec_msg rx_msg;
	u8 tx_status;
	bool tx_done;
	bool rx_done;
};
#endif

struct dw_hdmi_qp {
	struct drm_bridge bridge;

	struct device *dev;
	struct dw_hdmi_qp_i2c *i2c;

#ifdef CONFIG_DRM_DW_HDMI_QP_CEC
	struct dw_hdmi_qp_cec *cec;
#endif

	struct {
		const struct dw_hdmi_qp_phy_ops *ops;
		void *data;
	} phy;

	unsigned long ref_clk_rate;
	struct regmap *regm;
	int main_irq;

	/* Guards curr_conn between atomic hooks and HPD notify readers */
	struct mutex mutex;
	struct drm_connector *curr_conn;
	unsigned long tmds_char_rate;
	bool no_hpd;
};

static void dw_hdmi_qp_write(struct dw_hdmi_qp *hdmi, unsigned int val,
			     int offset)
{
	regmap_write(hdmi->regm, offset, val);
}

static unsigned int dw_hdmi_qp_read(struct dw_hdmi_qp *hdmi, int offset)
{
	unsigned int val = 0;

	regmap_read(hdmi->regm, offset, &val);

	return val;
}

static void dw_hdmi_qp_mod(struct dw_hdmi_qp *hdmi, unsigned int data,
			   unsigned int mask, unsigned int reg)
{
	regmap_update_bits(hdmi->regm, reg, mask, data);
}

static struct dw_hdmi_qp *dw_hdmi_qp_from_bridge(struct drm_bridge *bridge)
{
	return container_of(bridge, struct dw_hdmi_qp, bridge);
}

static void dw_hdmi_qp_set_cts_n(struct dw_hdmi_qp *hdmi, unsigned int cts,
				 unsigned int n)
{
	/* Set N */
	dw_hdmi_qp_mod(hdmi, n, AUDPKT_ACR_N_VALUE, AUDPKT_ACR_CONTROL0);

	/* Set CTS */
	if (cts)
		dw_hdmi_qp_mod(hdmi, AUDPKT_ACR_CTS_OVR_EN, AUDPKT_ACR_CTS_OVR_EN_MSK,
			       AUDPKT_ACR_CONTROL1);
	else
		dw_hdmi_qp_mod(hdmi, 0, AUDPKT_ACR_CTS_OVR_EN_MSK,
			       AUDPKT_ACR_CONTROL1);

	dw_hdmi_qp_mod(hdmi, AUDPKT_ACR_CTS_OVR_VAL(cts), AUDPKT_ACR_CTS_OVR_VAL_MSK,
		       AUDPKT_ACR_CONTROL1);
}

static void dw_hdmi_qp_set_audio_interface(struct dw_hdmi_qp *hdmi,
					   struct hdmi_codec_daifmt *fmt,
					   struct hdmi_codec_params *hparms)
{
	u32 conf0 = 0;

	/* Reset the audio data path of the AVP */
	dw_hdmi_qp_write(hdmi, AVP_DATAPATH_PACKET_AUDIO_SWINIT_P, GLOBAL_SWRESET_REQUEST);

	/* Disable AUDS, ACR, AUDI */
	dw_hdmi_qp_mod(hdmi, 0,
		       PKTSCHED_ACR_TX_EN | PKTSCHED_AUDS_TX_EN | PKTSCHED_AUDI_TX_EN,
		       PKTSCHED_PKT_EN);

	/* Clear the audio FIFO */
	dw_hdmi_qp_write(hdmi, AUDIO_FIFO_CLR_P, AUDIO_INTERFACE_CONTROL0);

	/* Select I2S interface as the audio source */
	dw_hdmi_qp_mod(hdmi, AUD_IF_I2S, AUD_IF_SEL_MSK, AUDIO_INTERFACE_CONFIG0);

	/* Enable the active i2s lanes */
	switch (hparms->channels) {
	case 7 ... 8:
		conf0 |= I2S_LINES_EN(3);
		fallthrough;
	case 5 ... 6:
		conf0 |= I2S_LINES_EN(2);
		fallthrough;
	case 3 ... 4:
		conf0 |= I2S_LINES_EN(1);
		fallthrough;
	default:
		conf0 |= I2S_LINES_EN(0);
		break;
	}

	dw_hdmi_qp_mod(hdmi, conf0, I2S_LINES_EN_MSK, AUDIO_INTERFACE_CONFIG0);

	/*
	 * Enable bpcuv generated internally for L-PCM, or received
	 * from stream for NLPCM/HBR.
	 */
	switch (fmt->bit_fmt) {
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
		conf0 = (hparms->channels == 8) ? AUD_HBR : AUD_ASP;
		conf0 |= I2S_BPCUV_RCV_EN;
		break;
	default:
		conf0 = AUD_ASP | I2S_BPCUV_RCV_DIS;
		break;
	}

	dw_hdmi_qp_mod(hdmi, conf0, I2S_BPCUV_RCV_MSK | AUD_FORMAT_MSK,
		       AUDIO_INTERFACE_CONFIG0);

	/* Enable audio FIFO auto clear when overflow */
	dw_hdmi_qp_mod(hdmi, AUD_FIFO_INIT_ON_OVF_EN, AUD_FIFO_INIT_ON_OVF_MSK,
		       AUDIO_INTERFACE_CONFIG0);
}

/*
 * When transmitting IEC60958 linear PCM audio, these registers allow to
 * configure the channel status information of all the channel status
 * bits in the IEC60958 frame. For the moment this configuration is only
 * used when the I2S audio interface, General Purpose Audio (GPA),
 * or AHB audio DMA (AHBAUDDMA) interface is active
 * (for S/PDIF interface this information comes from the stream).
 */
static void dw_hdmi_qp_set_channel_status(struct dw_hdmi_qp *hdmi,
					  u8 *channel_status, bool ref2stream)
{
	/*
	 * AUDPKT_CHSTATUS_OVR0: { RSV, RSV, CS1, CS0 }
	 * AUDPKT_CHSTATUS_OVR1: { CS6, CS5, CS4, CS3 }
	 *
	 *      |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
	 * CS0: |   Mode    |        d        |  c  |  b  |  a  |
	 * CS1: |               Category Code                   |
	 * CS2: |    Channel Number     |     Source Number     |
	 * CS3: |    Clock Accuracy     |     Sample Freq       |
	 * CS4: |    Ori Sample Freq    |     Word Length       |
	 * CS5: |                                   |   CGMS-A  |
	 * CS6~CS23: Reserved
	 *
	 * a: use of channel status block
	 * b: linear PCM identification: 0 for lpcm, 1 for nlpcm
	 * c: copyright information
	 * d: additional format information
	 */

	if (ref2stream)
		channel_status[0] |= IEC958_AES0_NONAUDIO;

	if ((dw_hdmi_qp_read(hdmi, AUDIO_INTERFACE_CONFIG0) & GENMASK(25, 24)) == AUD_HBR) {
		/* fixup cs for HBR */
		channel_status[3] = (channel_status[3] & 0xf0) | IEC958_AES3_CON_FS_768000;
		channel_status[4] = (channel_status[4] & 0x0f) | IEC958_AES4_CON_ORIGFS_NOTID;
	}

	dw_hdmi_qp_write(hdmi, channel_status[0] | (channel_status[1] << 8),
			 AUDPKT_CHSTATUS_OVR0);

	regmap_bulk_write(hdmi->regm, AUDPKT_CHSTATUS_OVR1, &channel_status[3], 1);

	if (ref2stream)
		dw_hdmi_qp_mod(hdmi, 0,
			       AUDPKT_PBIT_FORCE_EN_MASK | AUDPKT_CHSTATUS_OVR_EN_MASK,
			       AUDPKT_CONTROL0);
	else
		dw_hdmi_qp_mod(hdmi, AUDPKT_PBIT_FORCE_EN | AUDPKT_CHSTATUS_OVR_EN,
			       AUDPKT_PBIT_FORCE_EN_MASK | AUDPKT_CHSTATUS_OVR_EN_MASK,
			       AUDPKT_CONTROL0);
}

static void dw_hdmi_qp_set_sample_rate(struct dw_hdmi_qp *hdmi, unsigned long long tmds_char_rate,
				       unsigned int sample_rate)
{
	unsigned int n, cts;

	drm_hdmi_acr_get_n_cts(tmds_char_rate, sample_rate, &n, &cts);

	dw_hdmi_qp_set_cts_n(hdmi, cts, n);
}

static int dw_hdmi_qp_audio_enable(struct drm_bridge *bridge,
				   struct drm_connector *connector)
{
	struct dw_hdmi_qp *hdmi = dw_hdmi_qp_from_bridge(bridge);

	if (hdmi->tmds_char_rate)
		dw_hdmi_qp_mod(hdmi, 0, AVP_DATAPATH_PACKET_AUDIO_SWDISABLE, GLOBAL_SWDISABLE);

	return 0;
}

static int dw_hdmi_qp_audio_prepare(struct drm_bridge *bridge,
				    struct drm_connector *connector,
				    struct hdmi_codec_daifmt *fmt,
				    struct hdmi_codec_params *hparms)
{
	struct dw_hdmi_qp *hdmi = dw_hdmi_qp_from_bridge(bridge);
	bool ref2stream = false;

	if (!hdmi->tmds_char_rate)
		return -ENODEV;

	if (fmt->bit_clk_provider | fmt->frame_clk_provider) {
		dev_err(hdmi->dev, "unsupported clock settings\n");
		return -EINVAL;
	}

	if (fmt->bit_fmt == SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE)
		ref2stream = true;

	dw_hdmi_qp_set_audio_interface(hdmi, fmt, hparms);
	dw_hdmi_qp_set_sample_rate(hdmi, hdmi->tmds_char_rate, hparms->sample_rate);
	dw_hdmi_qp_set_channel_status(hdmi, hparms->iec.status, ref2stream);
	drm_atomic_helper_connector_hdmi_update_audio_infoframe(connector, &hparms->cea);

	return 0;
}

static void dw_hdmi_qp_audio_disable_regs(struct dw_hdmi_qp *hdmi)
{
	/*
	 * Keep ACR, AUDI, AUDS packet always on to make SINK device
	 * active for better compatibility and user experience.
	 *
	 * This also fix POP sound on some SINK devices which wakeup
	 * from suspend to active.
	 */
	dw_hdmi_qp_mod(hdmi, I2S_BPCUV_RCV_DIS, I2S_BPCUV_RCV_MSK,
		       AUDIO_INTERFACE_CONFIG0);
	dw_hdmi_qp_mod(hdmi, AUDPKT_PBIT_FORCE_EN | AUDPKT_CHSTATUS_OVR_EN,
		       AUDPKT_PBIT_FORCE_EN_MASK | AUDPKT_CHSTATUS_OVR_EN_MASK,
		       AUDPKT_CONTROL0);

	dw_hdmi_qp_mod(hdmi, AVP_DATAPATH_PACKET_AUDIO_SWDISABLE,
		       AVP_DATAPATH_PACKET_AUDIO_SWDISABLE, GLOBAL_SWDISABLE);
}

static void dw_hdmi_qp_audio_disable(struct drm_bridge *bridge,
				     struct drm_connector *connector)
{
	struct dw_hdmi_qp *hdmi = dw_hdmi_qp_from_bridge(bridge);

	drm_atomic_helper_connector_hdmi_clear_audio_infoframe(connector);

	if (hdmi->tmds_char_rate)
		dw_hdmi_qp_audio_disable_regs(hdmi);
}

static int dw_hdmi_qp_i2c_read(struct dw_hdmi_qp *hdmi,
			       unsigned char *buf, unsigned int length)
{
	struct dw_hdmi_qp_i2c *i2c = hdmi->i2c;
	int stat;

	if (!i2c->is_regaddr) {
		dev_dbg(hdmi->dev, "set read register address to 0\n");
		i2c->slave_reg = 0x00;
		i2c->is_regaddr = true;
	}

	while (length--) {
		reinit_completion(&i2c->cmp);

		dw_hdmi_qp_mod(hdmi, i2c->slave_reg++ << 12, I2CM_ADDR,
			       I2CM_INTERFACE_CONTROL0);

		if (i2c->is_segment)
			dw_hdmi_qp_mod(hdmi, I2CM_EXT_READ, I2CM_WR_MASK,
				       I2CM_INTERFACE_CONTROL0);
		else
			dw_hdmi_qp_mod(hdmi, I2CM_FM_READ, I2CM_WR_MASK,
				       I2CM_INTERFACE_CONTROL0);

		stat = wait_for_completion_timeout(&i2c->cmp, HZ / 10);
		if (!stat) {
			dev_dbg_ratelimited(hdmi->dev, "i2c read timed out\n");
			dw_hdmi_qp_write(hdmi, 0x01, I2CM_CONTROL0);
			return -EAGAIN;
		}

		/* Check for error condition on the bus */
		if (i2c->stat & I2CM_NACK_RCVD_IRQ) {
			dev_dbg_ratelimited(hdmi->dev, "i2c read error\n");
			dw_hdmi_qp_write(hdmi, 0x01, I2CM_CONTROL0);
			return -EIO;
		}

		*buf++ = dw_hdmi_qp_read(hdmi, I2CM_INTERFACE_RDDATA_0_3) & 0xff;
		dw_hdmi_qp_mod(hdmi, 0, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);
	}

	i2c->is_segment = false;

	return 0;
}

static int dw_hdmi_qp_i2c_write(struct dw_hdmi_qp *hdmi,
				unsigned char *buf, unsigned int length)
{
	struct dw_hdmi_qp_i2c *i2c = hdmi->i2c;
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

		dw_hdmi_qp_write(hdmi, *buf++, I2CM_INTERFACE_WRDATA_0_3);
		dw_hdmi_qp_mod(hdmi, i2c->slave_reg++ << 12, I2CM_ADDR,
			       I2CM_INTERFACE_CONTROL0);
		dw_hdmi_qp_mod(hdmi, I2CM_FM_WRITE, I2CM_WR_MASK,
			       I2CM_INTERFACE_CONTROL0);

		stat = wait_for_completion_timeout(&i2c->cmp, HZ / 10);
		if (!stat) {
			dev_dbg_ratelimited(hdmi->dev, "i2c write time out!\n");
			dw_hdmi_qp_write(hdmi, 0x01, I2CM_CONTROL0);
			return -EAGAIN;
		}

		/* Check for error condition on the bus */
		if (i2c->stat & I2CM_NACK_RCVD_IRQ) {
			dev_dbg_ratelimited(hdmi->dev, "i2c write nack!\n");
			dw_hdmi_qp_write(hdmi, 0x01, I2CM_CONTROL0);
			return -EIO;
		}

		dw_hdmi_qp_mod(hdmi, 0, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);
	}

	return 0;
}

static int dw_hdmi_qp_i2c_xfer(struct i2c_adapter *adap,
			       struct i2c_msg *msgs, int num)
{
	struct dw_hdmi_qp *hdmi = i2c_get_adapdata(adap);
	struct dw_hdmi_qp_i2c *i2c = hdmi->i2c;
	u8 addr = msgs[0].addr;
	int i, ret = 0;

	if (addr == DDC_CI_ADDR)
		/*
		 * The internal I2C controller does not support the multi-byte
		 * read and write operations needed for DDC/CI.
		 * FIXME: Blacklist the DDC/CI address until we filter out
		 * unsupported I2C operations.
		 */
		return -EOPNOTSUPP;

	for (i = 0; i < num; i++) {
		if (msgs[i].len == 0) {
			dev_err(hdmi->dev,
				"unsupported transfer %d/%d, no data\n",
				i + 1, num);
			return -EOPNOTSUPP;
		}
	}

	guard(mutex)(&i2c->lock);

	/* Unmute DONE and ERROR interrupts */
	dw_hdmi_qp_mod(hdmi, I2CM_NACK_RCVD_MASK_N | I2CM_OP_DONE_MASK_N,
		       I2CM_NACK_RCVD_MASK_N | I2CM_OP_DONE_MASK_N,
		       MAINUNIT_1_INT_MASK_N);

	/* Set slave device address taken from the first I2C message */
	if (addr == DDC_SEGMENT_ADDR && msgs[0].len == 1)
		addr = DDC_ADDR;

	dw_hdmi_qp_mod(hdmi, addr << 5, I2CM_SLVADDR, I2CM_INTERFACE_CONTROL0);

	/* Set slave device register address on transfer */
	i2c->is_regaddr = false;

	/* Set segment pointer for I2C extended read mode operation */
	i2c->is_segment = false;

	for (i = 0; i < num; i++) {
		if (msgs[i].addr == DDC_SEGMENT_ADDR && msgs[i].len == 1) {
			i2c->is_segment = true;
			dw_hdmi_qp_mod(hdmi, DDC_SEGMENT_ADDR, I2CM_SEG_ADDR,
				       I2CM_INTERFACE_CONTROL1);
			dw_hdmi_qp_mod(hdmi, *msgs[i].buf << 7, I2CM_SEG_PTR,
				       I2CM_INTERFACE_CONTROL1);
		} else {
			if (msgs[i].flags & I2C_M_RD)
				ret = dw_hdmi_qp_i2c_read(hdmi, msgs[i].buf,
							  msgs[i].len);
			else
				ret = dw_hdmi_qp_i2c_write(hdmi, msgs[i].buf,
							   msgs[i].len);
		}
		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	/* Mute DONE and ERROR interrupts */
	dw_hdmi_qp_mod(hdmi, 0, I2CM_OP_DONE_MASK_N | I2CM_NACK_RCVD_MASK_N,
		       MAINUNIT_1_INT_MASK_N);

	return ret;
}

static u32 dw_hdmi_qp_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm dw_hdmi_qp_algorithm = {
	.master_xfer	= dw_hdmi_qp_i2c_xfer,
	.functionality	= dw_hdmi_qp_i2c_func,
};

static struct i2c_adapter *dw_hdmi_qp_i2c_adapter(struct dw_hdmi_qp *hdmi)
{
	struct dw_hdmi_qp_i2c *i2c;
	struct i2c_adapter *adap;
	int ret;

	i2c = devm_kzalloc(hdmi->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return ERR_PTR(-ENOMEM);

	mutex_init(&i2c->lock);
	init_completion(&i2c->cmp);

	adap = &i2c->adap;
	adap->owner = THIS_MODULE;
	adap->dev.parent = hdmi->dev;
	adap->algo = &dw_hdmi_qp_algorithm;
	strscpy(adap->name, "DesignWare HDMI QP", sizeof(adap->name));

	i2c_set_adapdata(adap, hdmi);

	ret = devm_i2c_add_adapter(hdmi->dev, adap);
	if (ret) {
		dev_warn(hdmi->dev, "cannot add %s I2C adapter\n", adap->name);
		devm_kfree(hdmi->dev, i2c);
		return ERR_PTR(ret);
	}

	hdmi->i2c = i2c;
	dev_info(hdmi->dev, "registered %s I2C bus driver\n", adap->name);

	return adap;
}

static void dw_hdmi_qp_bridge_atomic_enable(struct drm_bridge *bridge,
					    struct drm_atomic_commit *state)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	unsigned int op_mode;
	int ret;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	if (WARN_ON(!connector))
		return;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!conn_state))
		return;

	if (connector->display_info.is_hdmi) {
		op_mode = 0;
		hdmi->tmds_char_rate = conn_state->hdmi.tmds_char_rate;

		ret = drm_connector_hdmi_enable_scrambling(connector, conn_state);
		if (ret)
			dev_warn(hdmi->dev, "Failed to enable scrambling: %d\n", ret);

		dev_dbg(hdmi->dev, "%s mode=HDMI %s rate=%llu bpc=%u scramb=%d\n", __func__,
			drm_hdmi_connector_get_output_format_name(conn_state->hdmi.output_format),
			conn_state->hdmi.tmds_char_rate, conn_state->hdmi.output_bpc,
			connector->hdmi.scrambler_enabled);
	} else {
		op_mode = OPMODE_DVI;
		dev_dbg(hdmi->dev, "%s mode=DVI\n", __func__);
	}

	drm_connector_get(connector);
	scoped_guard(mutex, &hdmi->mutex)
		hdmi->curr_conn = connector;

	hdmi->phy.ops->init(hdmi, hdmi->phy.data);

	dw_hdmi_qp_mod(hdmi, HDCP2_BYPASS, HDCP2_BYPASS, HDCP2LOGIC_CONFIG0);
	dw_hdmi_qp_mod(hdmi, op_mode, OPMODE_DVI, LINK_CONFIG0);

	drm_atomic_helper_connector_hdmi_update_infoframes(connector, state);
}

static void dw_hdmi_qp_bridge_atomic_disable(struct drm_bridge *bridge,
					     struct drm_atomic_commit *state)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;
	struct drm_connector *connector;

	scoped_guard(mutex, &hdmi->mutex) {
		if (hdmi->curr_conn) {
			drm_connector_put(hdmi->curr_conn);
			hdmi->curr_conn = NULL;
		}
	}

	hdmi->tmds_char_rate = 0;

	connector = drm_atomic_get_old_connector_for_encoder(state, bridge->encoder);
	drm_connector_hdmi_disable_scrambling(connector);

	hdmi->phy.ops->disable(hdmi, hdmi->phy.data);
}

static enum drm_connector_status
dw_hdmi_qp_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;
	const struct drm_edid *drm_edid;

	if (hdmi->no_hpd) {
		drm_edid = drm_edid_read_ddc(connector, bridge->ddc);
		if (drm_edid)
			return connector_status_connected;
		else
			return connector_status_disconnected;
	}

	return hdmi->phy.ops->read_hpd(hdmi, hdmi->phy.data);
}

static const struct drm_edid *
dw_hdmi_qp_bridge_edid_read(struct drm_bridge *bridge,
			    struct drm_connector *connector)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;
	const struct drm_edid *drm_edid;

	drm_edid = drm_edid_read_ddc(connector, bridge->ddc);
	if (!drm_edid)
		dev_dbg(hdmi->dev, "failed to get edid\n");

	return drm_edid;
}

static int dw_hdmi_qp_bridge_scrambler_enable(struct drm_bridge *bridge)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_write(hdmi, 1, SCRAMB_CONFIG0);
	dev_dbg(hdmi->dev, "scrambler enabled\n");

	return 0;
}

static int dw_hdmi_qp_bridge_scrambler_disable(struct drm_bridge *bridge)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_write(hdmi, 0, SCRAMB_CONFIG0);
	dev_dbg(hdmi->dev, "scrambler disabled\n");

	return 0;
}

static int dw_hdmi_qp_bridge_clear_avi_infoframe(struct drm_bridge *bridge)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_AVI_TX_EN | PKTSCHED_GCP_TX_EN,
		       PKTSCHED_PKT_EN);

	return 0;
}

static int dw_hdmi_qp_bridge_clear_hdmi_infoframe(struct drm_bridge *bridge)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_VSI_TX_EN, PKTSCHED_PKT_EN);

	return 0;
}

static int dw_hdmi_qp_bridge_clear_hdr_drm_infoframe(struct drm_bridge *bridge)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_DRMI_TX_EN, PKTSCHED_PKT_EN);

	return 0;
}

static int dw_hdmi_qp_bridge_clear_spd_infoframe(struct drm_bridge *bridge)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_SPDI_TX_EN, PKTSCHED_PKT_EN);

	return 0;
}

static int dw_hdmi_qp_bridge_clear_audio_infoframe(struct drm_bridge *bridge)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_mod(hdmi, 0,
		       PKTSCHED_ACR_TX_EN |
		       PKTSCHED_AUDS_TX_EN |
		       PKTSCHED_AUDI_TX_EN,
		       PKTSCHED_PKT_EN);

	return 0;
}

static void dw_hdmi_qp_write_pkt(struct dw_hdmi_qp *hdmi, const u8 *buffer,
				 size_t start, size_t len, unsigned int reg)
{
	u32 val = 0;
	size_t i;

	for (i = start; i < start + len; i++)
		val |= buffer[i] << ((i % 4) * BITS_PER_BYTE);

	dw_hdmi_qp_write(hdmi, val, reg);
}

static void dw_hdmi_qp_write_infoframe(struct dw_hdmi_qp *hdmi, const u8 *buffer,
				       size_t len, unsigned int reg)
{
	size_t i;

	/* InfoFrame packet header */
	dw_hdmi_qp_write_pkt(hdmi, buffer, 1, 2, reg);

	/* InfoFrame packet body */
	for (i = 0; i < len - 3; i += 4)
		dw_hdmi_qp_write_pkt(hdmi, buffer + 3, i, min(len - i - 3, 4),
				     reg + i + 4);
}

static int dw_hdmi_qp_bridge_write_avi_infoframe(struct drm_bridge *bridge,
						 const u8 *buffer, size_t len)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_bridge_clear_avi_infoframe(bridge);

	dw_hdmi_qp_write_infoframe(hdmi, buffer, len, PKT_AVI_CONTENTS0);

	dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_AVI_FIELDRATE, PKTSCHED_PKT_CONFIG1);
	dw_hdmi_qp_mod(hdmi, PKTSCHED_AVI_TX_EN | PKTSCHED_GCP_TX_EN,
		       PKTSCHED_AVI_TX_EN | PKTSCHED_GCP_TX_EN, PKTSCHED_PKT_EN);

	return 0;
}

static int dw_hdmi_qp_bridge_write_hdmi_infoframe(struct drm_bridge *bridge,
						  const u8 *buffer, size_t len)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_bridge_clear_hdmi_infoframe(bridge);

	dw_hdmi_qp_write_infoframe(hdmi, buffer, len, PKT_VSI_CONTENTS0);

	dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_VSI_FIELDRATE, PKTSCHED_PKT_CONFIG1);
	dw_hdmi_qp_mod(hdmi, PKTSCHED_VSI_TX_EN, PKTSCHED_VSI_TX_EN,
		       PKTSCHED_PKT_EN);

	return 0;
}

static int dw_hdmi_qp_bridge_write_hdr_drm_infoframe(struct drm_bridge *bridge,
						     const u8 *buffer, size_t len)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_bridge_clear_hdr_drm_infoframe(bridge);

	dw_hdmi_qp_write_infoframe(hdmi, buffer, len, PKT_DRMI_CONTENTS0);

	dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_DRMI_FIELDRATE, PKTSCHED_PKT_CONFIG1);
	dw_hdmi_qp_mod(hdmi, PKTSCHED_DRMI_TX_EN, PKTSCHED_DRMI_TX_EN,
		       PKTSCHED_PKT_EN);

	return 0;
}

static int dw_hdmi_qp_bridge_write_spd_infoframe(struct drm_bridge *bridge,
						 const u8 *buffer, size_t len)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_bridge_clear_spd_infoframe(bridge);

	dw_hdmi_qp_write_infoframe(hdmi, buffer, len, PKT_SPDI_CONTENTS0);

	dw_hdmi_qp_mod(hdmi, PKTSCHED_SPDI_TX_EN, PKTSCHED_SPDI_TX_EN,
		       PKTSCHED_PKT_EN);

	return 0;
}

static int dw_hdmi_qp_bridge_write_audio_infoframe(struct drm_bridge *bridge,
						   const u8 *buffer, size_t len)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_bridge_clear_audio_infoframe(bridge);

	/*
	 * AUDI_CONTENTS0: { RSV, HB2, HB1, RSV }
	 * AUDI_CONTENTS1: { PB3, PB2, PB1, PB0 }
	 * AUDI_CONTENTS2: { PB7, PB6, PB5, PB4 }
	 *
	 * PB0: CheckSum
	 * PB1: | CT3    | CT2  | CT1  | CT0  | F13  | CC2 | CC1 | CC0 |
	 * PB2: | F27    | F26  | F25  | SF2  | SF1  | SF0 | SS1 | SS0 |
	 * PB3: | F37    | F36  | F35  | F34  | F33  | F32 | F31 | F30 |
	 * PB4: | CA7    | CA6  | CA5  | CA4  | CA3  | CA2 | CA1 | CA0 |
	 * PB5: | DM_INH | LSV3 | LSV2 | LSV1 | LSV0 | F52 | F51 | F50 |
	 * PB6~PB10: Reserved
	 */
	dw_hdmi_qp_write_infoframe(hdmi, buffer, len, PKT_AUDI_CONTENTS0);

	/* Enable ACR, AUDI, AMD */
	dw_hdmi_qp_mod(hdmi,
		       PKTSCHED_ACR_TX_EN | PKTSCHED_AUDI_TX_EN | PKTSCHED_AMD_TX_EN,
		       PKTSCHED_ACR_TX_EN | PKTSCHED_AUDI_TX_EN | PKTSCHED_AMD_TX_EN,
		       PKTSCHED_PKT_EN);

	/* Enable AUDS */
	dw_hdmi_qp_mod(hdmi, PKTSCHED_AUDS_TX_EN, PKTSCHED_AUDS_TX_EN, PKTSCHED_PKT_EN);

	return 0;
}

#ifdef CONFIG_DRM_DW_HDMI_QP_CEC
static irqreturn_t dw_hdmi_qp_cec_hardirq(int irq, void *dev_id)
{
	struct dw_hdmi_qp *hdmi = dev_id;
	struct dw_hdmi_qp_cec *cec = hdmi->cec;
	irqreturn_t ret = IRQ_HANDLED;
	u32 stat;

	stat = dw_hdmi_qp_read(hdmi, CEC_INT_STATUS);
	if (stat == 0)
		return IRQ_NONE;

	dw_hdmi_qp_write(hdmi, stat, CEC_INT_CLEAR);

	if (stat & CEC_STAT_LINE_ERR) {
		cec->tx_status = CEC_TX_STATUS_ERROR;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	} else if (stat & CEC_STAT_DONE) {
		cec->tx_status = CEC_TX_STATUS_OK;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	} else if (stat & CEC_STAT_NACK) {
		cec->tx_status = CEC_TX_STATUS_NACK;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	}

	if (stat & CEC_STAT_EOM) {
		unsigned int len, i, val;

		val = dw_hdmi_qp_read(hdmi, CEC_RX_COUNT_STATUS);
		len = (val & 0xf) + 1;

		if (len > sizeof(cec->rx_msg.msg))
			len = sizeof(cec->rx_msg.msg);

		for (i = 0; i < 4; i++) {
			val = dw_hdmi_qp_read(hdmi, CEC_RX_DATA3_0 + i * 4);
			cec->rx_msg.msg[i * 4] = val & 0xff;
			cec->rx_msg.msg[i * 4 + 1] = (val >> 8) & 0xff;
			cec->rx_msg.msg[i * 4 + 2] = (val >> 16) & 0xff;
			cec->rx_msg.msg[i * 4 + 3] = (val >> 24) & 0xff;
		}

		dw_hdmi_qp_write(hdmi, 1, CEC_LOCK_CONTROL);

		cec->rx_msg.len = len;
		cec->rx_done = true;

		ret = IRQ_WAKE_THREAD;
	}

	return ret;
}

static irqreturn_t dw_hdmi_qp_cec_thread(int irq, void *dev_id)
{
	struct dw_hdmi_qp *hdmi = dev_id;
	struct dw_hdmi_qp_cec *cec = hdmi->cec;

	if (cec->tx_done) {
		cec->tx_done = false;
		drm_connector_hdmi_cec_transmit_attempt_done(cec->connector,
							     cec->tx_status);
	}

	if (cec->rx_done) {
		cec->rx_done = false;
		drm_connector_hdmi_cec_received_msg(cec->connector, &cec->rx_msg);
	}

	return IRQ_HANDLED;
}

static int dw_hdmi_qp_cec_init(struct drm_bridge *bridge,
			       struct drm_connector *connector)
{
	struct dw_hdmi_qp *hdmi = dw_hdmi_qp_from_bridge(bridge);
	struct dw_hdmi_qp_cec *cec = hdmi->cec;

	cec->connector = connector;

	dw_hdmi_qp_write(hdmi, 0, CEC_TX_COUNT);
	dw_hdmi_qp_write(hdmi, ~0, CEC_INT_CLEAR);
	dw_hdmi_qp_write(hdmi, 0, CEC_INT_MASK_N);

	return devm_request_threaded_irq(hdmi->dev, cec->irq,
					 dw_hdmi_qp_cec_hardirq,
					 dw_hdmi_qp_cec_thread, IRQF_SHARED,
					 dev_name(hdmi->dev), hdmi);
}

static int dw_hdmi_qp_cec_log_addr(struct drm_bridge *bridge, u8 logical_addr)
{
	struct dw_hdmi_qp *hdmi = dw_hdmi_qp_from_bridge(bridge);
	struct dw_hdmi_qp_cec *cec = hdmi->cec;

	if (logical_addr == CEC_LOG_ADDR_INVALID)
		cec->addresses = 0;
	else
		cec->addresses |= BIT(logical_addr) | CEC_ADDR_BROADCAST;

	dw_hdmi_qp_write(hdmi, cec->addresses, CEC_ADDR);

	return 0;
}

static int dw_hdmi_qp_cec_enable(struct drm_bridge *bridge, bool enable)
{
	struct dw_hdmi_qp *hdmi = dw_hdmi_qp_from_bridge(bridge);
	unsigned int irqs;
	u32 swdisable;

	if (!enable) {
		dw_hdmi_qp_write(hdmi, 0, CEC_INT_MASK_N);
		dw_hdmi_qp_write(hdmi, ~0, CEC_INT_CLEAR);

		swdisable = dw_hdmi_qp_read(hdmi, GLOBAL_SWDISABLE);
		swdisable = swdisable | CEC_SWDISABLE;
		dw_hdmi_qp_write(hdmi, swdisable, GLOBAL_SWDISABLE);
	} else {
		swdisable = dw_hdmi_qp_read(hdmi, GLOBAL_SWDISABLE);
		swdisable = swdisable & ~CEC_SWDISABLE;
		dw_hdmi_qp_write(hdmi, swdisable, GLOBAL_SWDISABLE);

		dw_hdmi_qp_write(hdmi, ~0, CEC_INT_CLEAR);
		dw_hdmi_qp_write(hdmi, 1, CEC_LOCK_CONTROL);

		dw_hdmi_qp_cec_log_addr(bridge, CEC_LOG_ADDR_INVALID);

		irqs = CEC_STAT_LINE_ERR | CEC_STAT_NACK | CEC_STAT_EOM |
		       CEC_STAT_DONE;
		dw_hdmi_qp_write(hdmi, ~0, CEC_INT_CLEAR);
		dw_hdmi_qp_write(hdmi, irqs, CEC_INT_MASK_N);
	}

	return 0;
}

static int dw_hdmi_qp_cec_transmit(struct drm_bridge *bridge, u8 attempts,
				   u32 signal_free_time, struct cec_msg *msg)
{
	struct dw_hdmi_qp *hdmi = dw_hdmi_qp_from_bridge(bridge);
	unsigned int i;
	u32 val;

	for (i = 0; i < msg->len; i++) {
		if (!(i % 4))
			val = msg->msg[i];
		if ((i % 4) == 1)
			val |= msg->msg[i] << 8;
		if ((i % 4) == 2)
			val |= msg->msg[i] << 16;
		if ((i % 4) == 3)
			val |= msg->msg[i] << 24;

		if (i == (msg->len - 1) || (i % 4) == 3)
			dw_hdmi_qp_write(hdmi, val, CEC_TX_DATA3_0 + (i / 4) * 4);
	}

	dw_hdmi_qp_write(hdmi, msg->len - 1, CEC_TX_COUNT);
	dw_hdmi_qp_write(hdmi, CEC_CTRL_START, CEC_TX_CONTROL);

	return 0;
}
#else
#define dw_hdmi_qp_cec_init NULL
#define dw_hdmi_qp_cec_enable NULL
#define dw_hdmi_qp_cec_log_addr NULL
#define dw_hdmi_qp_cec_transmit NULL
#endif /* CONFIG_DRM_DW_HDMI_QP_CEC */

static void dw_hdmi_qp_bridge_hpd_enable(struct drm_bridge *bridge)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	if (hdmi->phy.ops->enable_hpd)
		hdmi->phy.ops->enable_hpd(hdmi, hdmi->phy.data);
}

static void dw_hdmi_qp_bridge_hpd_disable(struct drm_bridge *bridge)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	if (hdmi->phy.ops->disable_hpd)
		hdmi->phy.ops->disable_hpd(hdmi, hdmi->phy.data);
}

static const struct drm_bridge_funcs dw_hdmi_qp_bridge_funcs = {
	.atomic_get_output_bus_fmts = drm_atomic_helper_bridge_get_hdmi_output_bus_fmts,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_create_state = drm_atomic_helper_bridge_create_state,
	.atomic_enable = dw_hdmi_qp_bridge_atomic_enable,
	.atomic_disable = dw_hdmi_qp_bridge_atomic_disable,
	.detect = dw_hdmi_qp_bridge_detect,
	.hpd_enable = dw_hdmi_qp_bridge_hpd_enable,
	.hpd_disable = dw_hdmi_qp_bridge_hpd_disable,
	.edid_read = dw_hdmi_qp_bridge_edid_read,
	.hdmi_scrambler_enable = dw_hdmi_qp_bridge_scrambler_enable,
	.hdmi_scrambler_disable = dw_hdmi_qp_bridge_scrambler_disable,
	.hdmi_clear_avi_infoframe = dw_hdmi_qp_bridge_clear_avi_infoframe,
	.hdmi_write_avi_infoframe = dw_hdmi_qp_bridge_write_avi_infoframe,
	.hdmi_clear_hdmi_infoframe = dw_hdmi_qp_bridge_clear_hdmi_infoframe,
	.hdmi_write_hdmi_infoframe = dw_hdmi_qp_bridge_write_hdmi_infoframe,
	.hdmi_clear_hdr_drm_infoframe = dw_hdmi_qp_bridge_clear_hdr_drm_infoframe,
	.hdmi_write_hdr_drm_infoframe = dw_hdmi_qp_bridge_write_hdr_drm_infoframe,
	.hdmi_clear_spd_infoframe = dw_hdmi_qp_bridge_clear_spd_infoframe,
	.hdmi_write_spd_infoframe = dw_hdmi_qp_bridge_write_spd_infoframe,
	.hdmi_clear_audio_infoframe = dw_hdmi_qp_bridge_clear_audio_infoframe,
	.hdmi_write_audio_infoframe = dw_hdmi_qp_bridge_write_audio_infoframe,
	.hdmi_audio_startup = dw_hdmi_qp_audio_enable,
	.hdmi_audio_shutdown = dw_hdmi_qp_audio_disable,
	.hdmi_audio_prepare = dw_hdmi_qp_audio_prepare,
	.hdmi_cec_init = dw_hdmi_qp_cec_init,
	.hdmi_cec_enable = dw_hdmi_qp_cec_enable,
	.hdmi_cec_log_addr = dw_hdmi_qp_cec_log_addr,
	.hdmi_cec_transmit = dw_hdmi_qp_cec_transmit,
};

static irqreturn_t dw_hdmi_qp_main_hardirq(int irq, void *dev_id)
{
	struct dw_hdmi_qp *hdmi = dev_id;
	struct dw_hdmi_qp_i2c *i2c = hdmi->i2c;
	u32 stat;

	stat = dw_hdmi_qp_read(hdmi, MAINUNIT_1_INT_STATUS);

	i2c->stat = stat & (I2CM_OP_DONE_IRQ | I2CM_READ_REQUEST_IRQ |
			    I2CM_NACK_RCVD_IRQ);

	if (i2c->stat) {
		dw_hdmi_qp_write(hdmi, i2c->stat, MAINUNIT_1_INT_CLEAR);
		complete(&i2c->cmp);
	}

	if (stat)
		return IRQ_HANDLED;

	return IRQ_NONE;
}

static const struct regmap_config dw_hdmi_qp_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= EARCRX_1_INT_FORCE,
};

static void dw_hdmi_qp_init_hw(struct dw_hdmi_qp *hdmi)
{
	dw_hdmi_qp_write(hdmi, 0, MAINUNIT_0_INT_MASK_N);
	dw_hdmi_qp_write(hdmi, 0, MAINUNIT_1_INT_MASK_N);
	dw_hdmi_qp_write(hdmi, hdmi->ref_clk_rate, TIMER_BASE_CONFIG0);

	/* Software reset */
	dw_hdmi_qp_write(hdmi, 0x01, I2CM_CONTROL0);
	dw_hdmi_qp_write(hdmi, 0x085c085c, I2CM_FM_SCL_CONFIG0);
	dw_hdmi_qp_mod(hdmi, 0, I2CM_FM_EN, I2CM_INTERFACE_CONTROL0);

	/* Clear DONE and ERROR interrupts */
	dw_hdmi_qp_write(hdmi, I2CM_OP_DONE_CLEAR | I2CM_NACK_RCVD_CLEAR,
			 MAINUNIT_1_INT_CLEAR);

	if (hdmi->phy.ops->setup_hpd)
		hdmi->phy.ops->setup_hpd(hdmi, hdmi->phy.data);
}

struct dw_hdmi_qp *dw_hdmi_qp_bind(struct platform_device *pdev,
				   struct drm_encoder *encoder,
				   const struct dw_hdmi_qp_plat_data *plat_data)
{
	struct device *dev = &pdev->dev;
	struct dw_hdmi_qp *hdmi;
	void __iomem *regs;
	int ret;

	if (!plat_data->phy_ops || !plat_data->phy_ops->init ||
	    !plat_data->phy_ops->disable || !plat_data->phy_ops->read_hpd) {
		dev_err(dev, "Missing platform PHY ops\n");
		return ERR_PTR(-ENODEV);
	}

	hdmi = devm_drm_bridge_alloc(dev, struct dw_hdmi_qp, bridge,
				     &dw_hdmi_qp_bridge_funcs);
	if (IS_ERR(hdmi))
		return ERR_CAST(hdmi);

	ret = devm_mutex_init(dev, &hdmi->mutex);
	if (ret)
		return ERR_PTR(ret);

	hdmi->dev = dev;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return ERR_CAST(regs);

	hdmi->regm = devm_regmap_init_mmio(dev, regs, &dw_hdmi_qp_regmap_config);
	if (IS_ERR(hdmi->regm)) {
		dev_err(dev, "Failed to configure regmap\n");
		return ERR_CAST(hdmi->regm);
	}

	hdmi->phy.ops = plat_data->phy_ops;
	hdmi->phy.data = plat_data->phy_data;

	if (plat_data->ref_clk_rate) {
		hdmi->ref_clk_rate = plat_data->ref_clk_rate;
	} else {
		hdmi->ref_clk_rate = 428571429;
		dev_warn(dev, "Set ref_clk_rate to vendor default\n");
	}

	dw_hdmi_qp_init_hw(hdmi);

	hdmi->main_irq = plat_data->main_irq;
	ret = devm_request_threaded_irq(dev, plat_data->main_irq,
					dw_hdmi_qp_main_hardirq, NULL,
					IRQF_SHARED, dev_name(dev), hdmi);
	if (ret)
		return ERR_PTR(ret);

	hdmi->no_hpd = device_property_read_bool(dev, "no-hpd");

	hdmi->bridge.driver_private = hdmi;
	hdmi->bridge.ops = DRM_BRIDGE_OP_DETECT |
			   DRM_BRIDGE_OP_EDID |
			   DRM_BRIDGE_OP_HDMI |
			   DRM_BRIDGE_OP_HDMI_AUDIO |
			   DRM_BRIDGE_OP_HDMI_HDR_DRM_INFOFRAME |
			   DRM_BRIDGE_OP_HDMI_SPD_INFOFRAME;
	hdmi->bridge.of_node = pdev->dev.of_node;
	hdmi->bridge.type = DRM_MODE_CONNECTOR_HDMIA;
	hdmi->bridge.vendor = "Synopsys";
	hdmi->bridge.product = "DW HDMI QP TX";

	if (hdmi->no_hpd) {
		hdmi->bridge.supported_hdmi_ver = HDMI_VERSION_1_4;
	} else {
		hdmi->bridge.supported_hdmi_ver = HDMI_VERSION_2_0;
		hdmi->bridge.ops |= DRM_BRIDGE_OP_HPD;
	}

	if (plat_data->supported_formats)
		hdmi->bridge.supported_formats = plat_data->supported_formats;

	if (plat_data->max_bpc)
		hdmi->bridge.max_bpc = plat_data->max_bpc;

	hdmi->bridge.ddc = dw_hdmi_qp_i2c_adapter(hdmi);
	if (IS_ERR(hdmi->bridge.ddc))
		return ERR_CAST(hdmi->bridge.ddc);

	hdmi->bridge.hdmi_audio_max_i2s_playback_channels = 8;
	hdmi->bridge.hdmi_audio_dev = dev;
	hdmi->bridge.hdmi_audio_dai_port = 1;

#ifdef CONFIG_DRM_DW_HDMI_QP_CEC
	if (plat_data->cec_irq) {
		hdmi->bridge.ops |= DRM_BRIDGE_OP_HDMI_CEC_ADAPTER;
		hdmi->bridge.hdmi_cec_dev = dev;
		hdmi->bridge.hdmi_cec_adapter_name = dev_name(dev);

		hdmi->cec = devm_kzalloc(hdmi->dev, sizeof(*hdmi->cec), GFP_KERNEL);
		if (!hdmi->cec)
			return ERR_PTR(-ENOMEM);

		hdmi->cec->irq = plat_data->cec_irq;
	} else {
		dev_warn(dev, "Disabled CEC support due to missing IRQ\n");
	}
#endif

	ret = devm_drm_bridge_add(dev, &hdmi->bridge);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_bridge_attach(encoder, &hdmi->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		return ERR_PTR(ret);

	return hdmi;
}
EXPORT_SYMBOL_GPL(dw_hdmi_qp_bind);

/**
 * dw_hdmi_qp_hpd_notify() - Notify a hot-plug detection event
 * @hdmi: pointer to the DW HDMI QP controller
 *
 * Platform drivers should call this from their threaded IRQ handler or work
 * function to notify the bridge of a connection status change.
 * The bridge's .read_hpd() phy_ops callback is used to read the current
 * connection status.
 */
void dw_hdmi_qp_hpd_notify(struct dw_hdmi_qp *hdmi)
{
	enum drm_connector_status status;
	struct drm_connector *conn;

	scoped_guard(mutex, &hdmi->mutex) {
		conn = hdmi->curr_conn;
		if (conn)
			drm_connector_get(conn);
	}

	status = hdmi->phy.ops->read_hpd(hdmi, hdmi->phy.data);
	dev_dbg(hdmi->dev, "%s status=%d\n", __func__, status);

	/*
	 * When the display pipeline has been already active, switch to
	 * drm_connector_helper_hpd_irq_event() to ensure .detect_ctx()
	 * gets invoked, i.e. via drm_helper_probe_detect(), because
	 * drm_bridge_hpd_notify() defers to a delayed hotplug path in
	 * this case.
	 */
	if (conn && status == connector_status_connected)
		drm_connector_helper_hpd_irq_event(conn);
	else
		drm_bridge_hpd_notify(&hdmi->bridge, status);

	if (conn)
		drm_connector_put(conn);
}
EXPORT_SYMBOL_GPL(dw_hdmi_qp_hpd_notify);

void dw_hdmi_qp_suspend(struct device *dev, struct dw_hdmi_qp *hdmi)
{
	disable_irq(hdmi->main_irq);
}
EXPORT_SYMBOL_GPL(dw_hdmi_qp_suspend);

void dw_hdmi_qp_resume(struct device *dev, struct dw_hdmi_qp *hdmi)
{
	dw_hdmi_qp_init_hw(hdmi);
	enable_irq(hdmi->main_irq);
}
EXPORT_SYMBOL_GPL(dw_hdmi_qp_resume);

MODULE_AUTHOR("Algea Cao <algea.cao@rock-chips.com>");
MODULE_AUTHOR("Cristian Ciocaltea <cristian.ciocaltea@collabora.com>");
MODULE_DESCRIPTION("DW HDMI QP transmitter library");
MODULE_LICENSE("GPL");
