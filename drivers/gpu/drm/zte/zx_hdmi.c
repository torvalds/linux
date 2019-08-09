// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2016 Linaro Ltd.
 * Copyright 2016 ZTE Corporation.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drmP.h>

#include <sound/hdmi-codec.h>

#include "zx_hdmi_regs.h"
#include "zx_vou.h"

#define ZX_HDMI_INFOFRAME_SIZE		31
#define DDC_SEGMENT_ADDR		0x30

struct zx_hdmi_i2c {
	struct i2c_adapter adap;
	struct mutex lock;
};

struct zx_hdmi {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct zx_hdmi_i2c *ddc;
	struct device *dev;
	struct drm_device *drm;
	void __iomem *mmio;
	struct clk *cec_clk;
	struct clk *osc_clk;
	struct clk *xclk;
	bool sink_is_hdmi;
	bool sink_has_audio;
	struct platform_device *audio_pdev;
};

#define to_zx_hdmi(x) container_of(x, struct zx_hdmi, x)

static inline u8 hdmi_readb(struct zx_hdmi *hdmi, u16 offset)
{
	return readl_relaxed(hdmi->mmio + offset * 4);
}

static inline void hdmi_writeb(struct zx_hdmi *hdmi, u16 offset, u8 val)
{
	writel_relaxed(val, hdmi->mmio + offset * 4);
}

static inline void hdmi_writeb_mask(struct zx_hdmi *hdmi, u16 offset,
				    u8 mask, u8 val)
{
	u8 tmp;

	tmp = hdmi_readb(hdmi, offset);
	tmp = (tmp & ~mask) | (val & mask);
	hdmi_writeb(hdmi, offset, tmp);
}

static int zx_hdmi_infoframe_trans(struct zx_hdmi *hdmi,
				   union hdmi_infoframe *frame, u8 fsel)
{
	u8 buffer[ZX_HDMI_INFOFRAME_SIZE];
	int num;
	int i;

	hdmi_writeb(hdmi, TPI_INFO_FSEL, fsel);

	num = hdmi_infoframe_pack(frame, buffer, ZX_HDMI_INFOFRAME_SIZE);
	if (num < 0) {
		DRM_DEV_ERROR(hdmi->dev, "failed to pack infoframe: %d\n", num);
		return num;
	}

	for (i = 0; i < num; i++)
		hdmi_writeb(hdmi, TPI_INFO_B0 + i, buffer[i]);

	hdmi_writeb_mask(hdmi, TPI_INFO_EN, TPI_INFO_TRANS_RPT,
			 TPI_INFO_TRANS_RPT);
	hdmi_writeb_mask(hdmi, TPI_INFO_EN, TPI_INFO_TRANS_EN,
			 TPI_INFO_TRANS_EN);

	return num;
}

static int zx_hdmi_config_video_vsi(struct zx_hdmi *hdmi,
				    struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;
	int ret;

	ret = drm_hdmi_vendor_infoframe_from_display_mode(&frame.vendor.hdmi,
							  &hdmi->connector,
							  mode);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev, "failed to get vendor infoframe: %d\n",
			      ret);
		return ret;
	}

	return zx_hdmi_infoframe_trans(hdmi, &frame, FSEL_VSIF);
}

static int zx_hdmi_config_video_avi(struct zx_hdmi *hdmi,
				    struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;
	int ret;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						       &hdmi->connector,
						       mode);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev, "failed to get avi infoframe: %d\n",
			      ret);
		return ret;
	}

	/* We always use YUV444 for HDMI output. */
	frame.avi.colorspace = HDMI_COLORSPACE_YUV444;

	return zx_hdmi_infoframe_trans(hdmi, &frame, FSEL_AVI);
}

static void zx_hdmi_encoder_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adj_mode)
{
	struct zx_hdmi *hdmi = to_zx_hdmi(encoder);

	if (hdmi->sink_is_hdmi) {
		zx_hdmi_config_video_avi(hdmi, mode);
		zx_hdmi_config_video_vsi(hdmi, mode);
	}
}

static void zx_hdmi_phy_start(struct zx_hdmi *hdmi)
{
	/* Copy from ZTE BSP code */
	hdmi_writeb(hdmi, 0x222, 0x0);
	hdmi_writeb(hdmi, 0x224, 0x4);
	hdmi_writeb(hdmi, 0x909, 0x0);
	hdmi_writeb(hdmi, 0x7b0, 0x90);
	hdmi_writeb(hdmi, 0x7b1, 0x00);
	hdmi_writeb(hdmi, 0x7b2, 0xa7);
	hdmi_writeb(hdmi, 0x7b8, 0xaa);
	hdmi_writeb(hdmi, 0x7b2, 0xa7);
	hdmi_writeb(hdmi, 0x7b3, 0x0f);
	hdmi_writeb(hdmi, 0x7b4, 0x0f);
	hdmi_writeb(hdmi, 0x7b5, 0x55);
	hdmi_writeb(hdmi, 0x7b7, 0x03);
	hdmi_writeb(hdmi, 0x7b9, 0x12);
	hdmi_writeb(hdmi, 0x7ba, 0x32);
	hdmi_writeb(hdmi, 0x7bc, 0x68);
	hdmi_writeb(hdmi, 0x7be, 0x40);
	hdmi_writeb(hdmi, 0x7bf, 0x84);
	hdmi_writeb(hdmi, 0x7c1, 0x0f);
	hdmi_writeb(hdmi, 0x7c8, 0x02);
	hdmi_writeb(hdmi, 0x7c9, 0x03);
	hdmi_writeb(hdmi, 0x7ca, 0x40);
	hdmi_writeb(hdmi, 0x7dc, 0x31);
	hdmi_writeb(hdmi, 0x7e2, 0x04);
	hdmi_writeb(hdmi, 0x7e0, 0x06);
	hdmi_writeb(hdmi, 0x7cb, 0x68);
	hdmi_writeb(hdmi, 0x7f9, 0x02);
	hdmi_writeb(hdmi, 0x7b6, 0x02);
	hdmi_writeb(hdmi, 0x7f3, 0x0);
}

static void zx_hdmi_hw_enable(struct zx_hdmi *hdmi)
{
	/* Enable pclk */
	hdmi_writeb_mask(hdmi, CLKPWD, CLKPWD_PDIDCK, CLKPWD_PDIDCK);

	/* Enable HDMI for TX */
	hdmi_writeb_mask(hdmi, FUNC_SEL, FUNC_HDMI_EN, FUNC_HDMI_EN);

	/* Enable deep color packet */
	hdmi_writeb_mask(hdmi, P2T_CTRL, P2T_DC_PKT_EN, P2T_DC_PKT_EN);

	/* Enable HDMI/MHL mode for output */
	hdmi_writeb_mask(hdmi, TEST_TXCTRL, TEST_TXCTRL_HDMI_MODE,
			 TEST_TXCTRL_HDMI_MODE);

	/* Configure reg_qc_sel */
	hdmi_writeb(hdmi, HDMICTL4, 0x3);

	/* Enable interrupt */
	hdmi_writeb_mask(hdmi, INTR1_MASK, INTR1_MONITOR_DETECT,
			 INTR1_MONITOR_DETECT);

	/* Start up phy */
	zx_hdmi_phy_start(hdmi);
}

static void zx_hdmi_hw_disable(struct zx_hdmi *hdmi)
{
	/* Disable interrupt */
	hdmi_writeb_mask(hdmi, INTR1_MASK, INTR1_MONITOR_DETECT, 0);

	/* Disable deep color packet */
	hdmi_writeb_mask(hdmi, P2T_CTRL, P2T_DC_PKT_EN, P2T_DC_PKT_EN);

	/* Disable HDMI for TX */
	hdmi_writeb_mask(hdmi, FUNC_SEL, FUNC_HDMI_EN, 0);

	/* Disable pclk */
	hdmi_writeb_mask(hdmi, CLKPWD, CLKPWD_PDIDCK, 0);
}

static void zx_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct zx_hdmi *hdmi = to_zx_hdmi(encoder);

	clk_prepare_enable(hdmi->cec_clk);
	clk_prepare_enable(hdmi->osc_clk);
	clk_prepare_enable(hdmi->xclk);

	zx_hdmi_hw_enable(hdmi);

	vou_inf_enable(VOU_HDMI, encoder->crtc);
}

static void zx_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct zx_hdmi *hdmi = to_zx_hdmi(encoder);

	vou_inf_disable(VOU_HDMI, encoder->crtc);

	zx_hdmi_hw_disable(hdmi);

	clk_disable_unprepare(hdmi->xclk);
	clk_disable_unprepare(hdmi->osc_clk);
	clk_disable_unprepare(hdmi->cec_clk);
}

static const struct drm_encoder_helper_funcs zx_hdmi_encoder_helper_funcs = {
	.enable	= zx_hdmi_encoder_enable,
	.disable = zx_hdmi_encoder_disable,
	.mode_set = zx_hdmi_encoder_mode_set,
};

static const struct drm_encoder_funcs zx_hdmi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int zx_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct zx_hdmi *hdmi = to_zx_hdmi(connector);
	struct edid *edid;
	int ret;

	edid = drm_get_edid(connector, &hdmi->ddc->adap);
	if (!edid)
		return 0;

	hdmi->sink_is_hdmi = drm_detect_hdmi_monitor(edid);
	hdmi->sink_has_audio = drm_detect_monitor_audio(edid);
	drm_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);

	return ret;
}

static enum drm_mode_status
zx_hdmi_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	return MODE_OK;
}

static struct drm_connector_helper_funcs zx_hdmi_connector_helper_funcs = {
	.get_modes = zx_hdmi_connector_get_modes,
	.mode_valid = zx_hdmi_connector_mode_valid,
};

static enum drm_connector_status
zx_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct zx_hdmi *hdmi = to_zx_hdmi(connector);

	return (hdmi_readb(hdmi, TPI_HPD_RSEN) & TPI_HPD_CONNECTION) ?
		connector_status_connected : connector_status_disconnected;
}

static const struct drm_connector_funcs zx_hdmi_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = zx_hdmi_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int zx_hdmi_register(struct drm_device *drm, struct zx_hdmi *hdmi)
{
	struct drm_encoder *encoder = &hdmi->encoder;

	encoder->possible_crtcs = VOU_CRTC_MASK;

	drm_encoder_init(drm, encoder, &zx_hdmi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(encoder, &zx_hdmi_encoder_helper_funcs);

	hdmi->connector.polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_init(drm, &hdmi->connector, &zx_hdmi_connector_funcs,
			   DRM_MODE_CONNECTOR_HDMIA);
	drm_connector_helper_add(&hdmi->connector,
				 &zx_hdmi_connector_helper_funcs);

	drm_connector_attach_encoder(&hdmi->connector, encoder);

	return 0;
}

static irqreturn_t zx_hdmi_irq_thread(int irq, void *dev_id)
{
	struct zx_hdmi *hdmi = dev_id;

	drm_helper_hpd_irq_event(hdmi->connector.dev);

	return IRQ_HANDLED;
}

static irqreturn_t zx_hdmi_irq_handler(int irq, void *dev_id)
{
	struct zx_hdmi *hdmi = dev_id;
	u8 lstat;

	lstat = hdmi_readb(hdmi, L1_INTR_STAT);

	/* Monitor detect/HPD interrupt */
	if (lstat & L1_INTR_STAT_INTR1) {
		u8 stat;

		stat = hdmi_readb(hdmi, INTR1_STAT);
		hdmi_writeb(hdmi, INTR1_STAT, stat);

		if (stat & INTR1_MONITOR_DETECT)
			return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static int zx_hdmi_audio_startup(struct device *dev, void *data)
{
	struct zx_hdmi *hdmi = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &hdmi->encoder;

	vou_inf_hdmi_audio_sel(encoder->crtc, VOU_HDMI_AUD_SPDIF);

	return 0;
}

static void zx_hdmi_audio_shutdown(struct device *dev, void *data)
{
	struct zx_hdmi *hdmi = dev_get_drvdata(dev);

	/* Disable audio input */
	hdmi_writeb_mask(hdmi, AUD_EN, AUD_IN_EN, 0);
}

static inline int zx_hdmi_audio_get_n(unsigned int fs)
{
	unsigned int n;

	if (fs && (fs % 44100) == 0)
		n = 6272 * (fs / 44100);
	else
		n = fs * 128 / 1000;

	return n;
}

static int zx_hdmi_audio_hw_params(struct device *dev,
				   void *data,
				   struct hdmi_codec_daifmt *daifmt,
				   struct hdmi_codec_params *params)
{
	struct zx_hdmi *hdmi = dev_get_drvdata(dev);
	struct hdmi_audio_infoframe *cea = &params->cea;
	union hdmi_infoframe frame;
	int n;

	/* We only support spdif for now */
	if (daifmt->fmt != HDMI_SPDIF) {
		DRM_DEV_ERROR(hdmi->dev, "invalid daifmt %d\n", daifmt->fmt);
		return -EINVAL;
	}

	switch (params->sample_width) {
	case 16:
		hdmi_writeb_mask(hdmi, TPI_AUD_CONFIG, SPDIF_SAMPLE_SIZE_MASK,
				 SPDIF_SAMPLE_SIZE_16BIT);
		break;
	case 20:
		hdmi_writeb_mask(hdmi, TPI_AUD_CONFIG, SPDIF_SAMPLE_SIZE_MASK,
				 SPDIF_SAMPLE_SIZE_20BIT);
		break;
	case 24:
		hdmi_writeb_mask(hdmi, TPI_AUD_CONFIG, SPDIF_SAMPLE_SIZE_MASK,
				 SPDIF_SAMPLE_SIZE_24BIT);
		break;
	default:
		DRM_DEV_ERROR(hdmi->dev, "invalid sample width %d\n",
			      params->sample_width);
		return -EINVAL;
	}

	/* CTS is calculated by hardware, and we only need to take care of N */
	n = zx_hdmi_audio_get_n(params->sample_rate);
	hdmi_writeb(hdmi, N_SVAL1, n & 0xff);
	hdmi_writeb(hdmi, N_SVAL2, (n >> 8) & 0xff);
	hdmi_writeb(hdmi, N_SVAL3, (n >> 16) & 0xf);

	/* Enable spdif mode */
	hdmi_writeb_mask(hdmi, AUD_MODE, SPDIF_EN, SPDIF_EN);

	/* Enable audio input */
	hdmi_writeb_mask(hdmi, AUD_EN, AUD_IN_EN, AUD_IN_EN);

	memcpy(&frame.audio, cea, sizeof(*cea));

	return zx_hdmi_infoframe_trans(hdmi, &frame, FSEL_AUDIO);
}

static int zx_hdmi_audio_digital_mute(struct device *dev, void *data,
				      bool enable)
{
	struct zx_hdmi *hdmi = dev_get_drvdata(dev);

	if (enable)
		hdmi_writeb_mask(hdmi, TPI_AUD_CONFIG, TPI_AUD_MUTE,
				 TPI_AUD_MUTE);
	else
		hdmi_writeb_mask(hdmi, TPI_AUD_CONFIG, TPI_AUD_MUTE, 0);

	return 0;
}

static int zx_hdmi_audio_get_eld(struct device *dev, void *data,
				 uint8_t *buf, size_t len)
{
	struct zx_hdmi *hdmi = dev_get_drvdata(dev);
	struct drm_connector *connector = &hdmi->connector;

	memcpy(buf, connector->eld, min(sizeof(connector->eld), len));

	return 0;
}

static const struct hdmi_codec_ops zx_hdmi_codec_ops = {
	.audio_startup = zx_hdmi_audio_startup,
	.hw_params = zx_hdmi_audio_hw_params,
	.audio_shutdown = zx_hdmi_audio_shutdown,
	.digital_mute = zx_hdmi_audio_digital_mute,
	.get_eld = zx_hdmi_audio_get_eld,
};

static struct hdmi_codec_pdata zx_hdmi_codec_pdata = {
	.ops = &zx_hdmi_codec_ops,
	.spdif = 1,
};

static int zx_hdmi_audio_register(struct zx_hdmi *hdmi)
{
	struct platform_device *pdev;

	pdev = platform_device_register_data(hdmi->dev, HDMI_CODEC_DRV_NAME,
					     PLATFORM_DEVID_AUTO,
					     &zx_hdmi_codec_pdata,
					     sizeof(zx_hdmi_codec_pdata));
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	hdmi->audio_pdev = pdev;

	return 0;
}

static int zx_hdmi_i2c_read(struct zx_hdmi *hdmi, struct i2c_msg *msg)
{
	int len = msg->len;
	u8 *buf = msg->buf;
	int retry = 0;
	int ret = 0;

	/* Bits [9:8] of bytes */
	hdmi_writeb(hdmi, ZX_DDC_DIN_CNT2, (len >> 8) & 0xff);
	/* Bits [7:0] of bytes */
	hdmi_writeb(hdmi, ZX_DDC_DIN_CNT1, len & 0xff);

	/* Clear FIFO */
	hdmi_writeb_mask(hdmi, ZX_DDC_CMD, DDC_CMD_MASK, DDC_CMD_CLEAR_FIFO);

	/* Kick off the read */
	hdmi_writeb_mask(hdmi, ZX_DDC_CMD, DDC_CMD_MASK,
			 DDC_CMD_SEQUENTIAL_READ);

	while (len > 0) {
		int cnt, i;

		/* FIFO needs some time to get ready */
		usleep_range(500, 1000);

		cnt = hdmi_readb(hdmi, ZX_DDC_DOUT_CNT) & DDC_DOUT_CNT_MASK;
		if (cnt == 0) {
			if (++retry > 5) {
				DRM_DEV_ERROR(hdmi->dev,
					      "DDC FIFO read timed out!");
				return -ETIMEDOUT;
			}
			continue;
		}

		for (i = 0; i < cnt; i++)
			*buf++ = hdmi_readb(hdmi, ZX_DDC_DATA);
		len -= cnt;
	}

	return ret;
}

static int zx_hdmi_i2c_write(struct zx_hdmi *hdmi, struct i2c_msg *msg)
{
	/*
	 * The DDC I2C adapter is only for reading EDID data, so we assume
	 * that the write to this adapter must be the EDID data offset.
	 */
	if ((msg->len != 1) ||
	    ((msg->addr != DDC_ADDR) && (msg->addr != DDC_SEGMENT_ADDR)))
		return -EINVAL;

	if (msg->addr == DDC_SEGMENT_ADDR)
		hdmi_writeb(hdmi, ZX_DDC_SEGM, msg->addr << 1);
	else if (msg->addr == DDC_ADDR)
		hdmi_writeb(hdmi, ZX_DDC_ADDR, msg->addr << 1);

	hdmi_writeb(hdmi, ZX_DDC_OFFSET, msg->buf[0]);

	return 0;
}

static int zx_hdmi_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			    int num)
{
	struct zx_hdmi *hdmi = i2c_get_adapdata(adap);
	struct zx_hdmi_i2c *ddc = hdmi->ddc;
	int i, ret = 0;

	mutex_lock(&ddc->lock);

	/* Enable DDC master access */
	hdmi_writeb_mask(hdmi, TPI_DDC_MASTER_EN, HW_DDC_MASTER, HW_DDC_MASTER);

	for (i = 0; i < num; i++) {
		DRM_DEV_DEBUG(hdmi->dev,
			      "xfer: num: %d/%d, len: %d, flags: %#x\n",
			      i + 1, num, msgs[i].len, msgs[i].flags);

		if (msgs[i].flags & I2C_M_RD)
			ret = zx_hdmi_i2c_read(hdmi, &msgs[i]);
		else
			ret = zx_hdmi_i2c_write(hdmi, &msgs[i]);

		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	/* Disable DDC master access */
	hdmi_writeb_mask(hdmi, TPI_DDC_MASTER_EN, HW_DDC_MASTER, 0);

	mutex_unlock(&ddc->lock);

	return ret;
}

static u32 zx_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm zx_hdmi_algorithm = {
	.master_xfer	= zx_hdmi_i2c_xfer,
	.functionality	= zx_hdmi_i2c_func,
};

static int zx_hdmi_ddc_register(struct zx_hdmi *hdmi)
{
	struct i2c_adapter *adap;
	struct zx_hdmi_i2c *ddc;
	int ret;

	ddc = devm_kzalloc(hdmi->dev, sizeof(*ddc), GFP_KERNEL);
	if (!ddc)
		return -ENOMEM;

	hdmi->ddc = ddc;
	mutex_init(&ddc->lock);

	adap = &ddc->adap;
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_DDC;
	adap->dev.parent = hdmi->dev;
	adap->algo = &zx_hdmi_algorithm;
	snprintf(adap->name, sizeof(adap->name), "zx hdmi i2c");

	ret = i2c_add_adapter(adap);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev, "failed to add I2C adapter: %d\n",
			      ret);
		return ret;
	}

	i2c_set_adapdata(adap, hdmi);

	return 0;
}

static int zx_hdmi_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct resource *res;
	struct zx_hdmi *hdmi;
	int irq;
	int ret;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = dev;
	hdmi->drm = drm;

	dev_set_drvdata(dev, hdmi);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdmi->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(hdmi->mmio)) {
		ret = PTR_ERR(hdmi->mmio);
		DRM_DEV_ERROR(dev, "failed to remap hdmi region: %d\n", ret);
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	hdmi->cec_clk = devm_clk_get(hdmi->dev, "osc_cec");
	if (IS_ERR(hdmi->cec_clk)) {
		ret = PTR_ERR(hdmi->cec_clk);
		DRM_DEV_ERROR(dev, "failed to get cec_clk: %d\n", ret);
		return ret;
	}

	hdmi->osc_clk = devm_clk_get(hdmi->dev, "osc_clk");
	if (IS_ERR(hdmi->osc_clk)) {
		ret = PTR_ERR(hdmi->osc_clk);
		DRM_DEV_ERROR(dev, "failed to get osc_clk: %d\n", ret);
		return ret;
	}

	hdmi->xclk = devm_clk_get(hdmi->dev, "xclk");
	if (IS_ERR(hdmi->xclk)) {
		ret = PTR_ERR(hdmi->xclk);
		DRM_DEV_ERROR(dev, "failed to get xclk: %d\n", ret);
		return ret;
	}

	ret = zx_hdmi_ddc_register(hdmi);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to register ddc: %d\n", ret);
		return ret;
	}

	ret = zx_hdmi_audio_register(hdmi);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to register audio: %d\n", ret);
		return ret;
	}

	ret = zx_hdmi_register(drm, hdmi);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to register hdmi: %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(dev, irq, zx_hdmi_irq_handler,
					zx_hdmi_irq_thread, IRQF_SHARED,
					dev_name(dev), hdmi);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to request threaded irq: %d\n", ret);
		return ret;
	}

	return 0;
}

static void zx_hdmi_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct zx_hdmi *hdmi = dev_get_drvdata(dev);

	hdmi->connector.funcs->destroy(&hdmi->connector);
	hdmi->encoder.funcs->destroy(&hdmi->encoder);

	if (hdmi->audio_pdev)
		platform_device_unregister(hdmi->audio_pdev);
}

static const struct component_ops zx_hdmi_component_ops = {
	.bind = zx_hdmi_bind,
	.unbind = zx_hdmi_unbind,
};

static int zx_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &zx_hdmi_component_ops);
}

static int zx_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &zx_hdmi_component_ops);
	return 0;
}

static const struct of_device_id zx_hdmi_of_match[] = {
	{ .compatible = "zte,zx296718-hdmi", },
	{ /* end */ },
};
MODULE_DEVICE_TABLE(of, zx_hdmi_of_match);

struct platform_driver zx_hdmi_driver = {
	.probe = zx_hdmi_probe,
	.remove = zx_hdmi_remove,
	.driver	= {
		.name = "zx-hdmi",
		.of_match_table	= zx_hdmi_of_match,
	},
};
