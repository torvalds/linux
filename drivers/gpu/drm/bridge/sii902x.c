// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018 Renesas Electronics
 *
 * Copyright (C) 2016 Atmel
 *		      Bo Shen <voice.shen@atmel.com>
 *
 * Authors:	      Bo Shen <voice.shen@atmel.com>
 *		      Boris Brezillon <boris.brezillon@free-electrons.com>
 *		      Wu, Songjun <Songjun.Wu@atmel.com>
 *
 * Copyright (C) 2010-2011 Freescale Semiconductor, Inc. All Rights Reserved.
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c-mux.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/clk.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include <sound/hdmi-codec.h>

#define SII902X_TPI_VIDEO_DATA			0x0

#define SII902X_TPI_PIXEL_REPETITION		0x8
#define SII902X_TPI_AVI_PIXEL_REP_BUS_24BIT     BIT(5)
#define SII902X_TPI_AVI_PIXEL_REP_RISING_EDGE   BIT(4)
#define SII902X_TPI_AVI_PIXEL_REP_4X		3
#define SII902X_TPI_AVI_PIXEL_REP_2X		1
#define SII902X_TPI_AVI_PIXEL_REP_NONE		0
#define SII902X_TPI_CLK_RATIO_HALF		(0 << 6)
#define SII902X_TPI_CLK_RATIO_1X		(1 << 6)
#define SII902X_TPI_CLK_RATIO_2X		(2 << 6)
#define SII902X_TPI_CLK_RATIO_4X		(3 << 6)

#define SII902X_TPI_AVI_IN_FORMAT		0x9
#define SII902X_TPI_AVI_INPUT_BITMODE_12BIT	BIT(7)
#define SII902X_TPI_AVI_INPUT_DITHER		BIT(6)
#define SII902X_TPI_AVI_INPUT_RANGE_LIMITED	(2 << 2)
#define SII902X_TPI_AVI_INPUT_RANGE_FULL	(1 << 2)
#define SII902X_TPI_AVI_INPUT_RANGE_AUTO	(0 << 2)
#define SII902X_TPI_AVI_INPUT_COLORSPACE_BLACK	(3 << 0)
#define SII902X_TPI_AVI_INPUT_COLORSPACE_YUV422	(2 << 0)
#define SII902X_TPI_AVI_INPUT_COLORSPACE_YUV444	(1 << 0)
#define SII902X_TPI_AVI_INPUT_COLORSPACE_RGB	(0 << 0)

#define SII902X_TPI_AVI_INFOFRAME		0x0c

#define SII902X_SYS_CTRL_DATA			0x1a
#define SII902X_SYS_CTRL_PWR_DWN		BIT(4)
#define SII902X_SYS_CTRL_AV_MUTE		BIT(3)
#define SII902X_SYS_CTRL_DDC_BUS_REQ		BIT(2)
#define SII902X_SYS_CTRL_DDC_BUS_GRTD		BIT(1)
#define SII902X_SYS_CTRL_OUTPUT_MODE		BIT(0)
#define SII902X_SYS_CTRL_OUTPUT_HDMI		1
#define SII902X_SYS_CTRL_OUTPUT_DVI		0

#define SII902X_REG_CHIPID(n)			(0x1b + (n))

#define SII902X_PWR_STATE_CTRL			0x1e
#define SII902X_AVI_POWER_STATE_MSK		GENMASK(1, 0)
#define SII902X_AVI_POWER_STATE_D(l)		((l) & SII902X_AVI_POWER_STATE_MSK)

/* Audio  */
#define SII902X_TPI_I2S_ENABLE_MAPPING_REG	0x1f
#define SII902X_TPI_I2S_CONFIG_FIFO0			(0 << 0)
#define SII902X_TPI_I2S_CONFIG_FIFO1			(1 << 0)
#define SII902X_TPI_I2S_CONFIG_FIFO2			(2 << 0)
#define SII902X_TPI_I2S_CONFIG_FIFO3			(3 << 0)
#define SII902X_TPI_I2S_LEFT_RIGHT_SWAP			(1 << 2)
#define SII902X_TPI_I2S_AUTO_DOWNSAMPLE			(1 << 3)
#define SII902X_TPI_I2S_SELECT_SD0			(0 << 4)
#define SII902X_TPI_I2S_SELECT_SD1			(1 << 4)
#define SII902X_TPI_I2S_SELECT_SD2			(2 << 4)
#define SII902X_TPI_I2S_SELECT_SD3			(3 << 4)
#define SII902X_TPI_I2S_FIFO_ENABLE			(1 << 7)

#define SII902X_TPI_I2S_INPUT_CONFIG_REG	0x20
#define SII902X_TPI_I2S_FIRST_BIT_SHIFT_YES		(0 << 0)
#define SII902X_TPI_I2S_FIRST_BIT_SHIFT_NO		(1 << 0)
#define SII902X_TPI_I2S_SD_DIRECTION_MSB_FIRST		(0 << 1)
#define SII902X_TPI_I2S_SD_DIRECTION_LSB_FIRST		(1 << 1)
#define SII902X_TPI_I2S_SD_JUSTIFY_LEFT			(0 << 2)
#define SII902X_TPI_I2S_SD_JUSTIFY_RIGHT		(1 << 2)
#define SII902X_TPI_I2S_WS_POLARITY_LOW			(0 << 3)
#define SII902X_TPI_I2S_WS_POLARITY_HIGH		(1 << 3)
#define SII902X_TPI_I2S_MCLK_MULTIPLIER_128		(0 << 4)
#define SII902X_TPI_I2S_MCLK_MULTIPLIER_256		(1 << 4)
#define SII902X_TPI_I2S_MCLK_MULTIPLIER_384		(2 << 4)
#define SII902X_TPI_I2S_MCLK_MULTIPLIER_512		(3 << 4)
#define SII902X_TPI_I2S_MCLK_MULTIPLIER_768		(4 << 4)
#define SII902X_TPI_I2S_MCLK_MULTIPLIER_1024		(5 << 4)
#define SII902X_TPI_I2S_MCLK_MULTIPLIER_1152		(6 << 4)
#define SII902X_TPI_I2S_MCLK_MULTIPLIER_192		(7 << 4)
#define SII902X_TPI_I2S_SCK_EDGE_FALLING		(0 << 7)
#define SII902X_TPI_I2S_SCK_EDGE_RISING			(1 << 7)

#define SII902X_TPI_I2S_STRM_HDR_BASE	0x21
#define SII902X_TPI_I2S_STRM_HDR_SIZE	5

#define SII902X_TPI_AUDIO_CONFIG_BYTE2_REG	0x26
#define SII902X_TPI_AUDIO_CODING_STREAM_HEADER		(0 << 0)
#define SII902X_TPI_AUDIO_CODING_PCM			(1 << 0)
#define SII902X_TPI_AUDIO_CODING_AC3			(2 << 0)
#define SII902X_TPI_AUDIO_CODING_MPEG1			(3 << 0)
#define SII902X_TPI_AUDIO_CODING_MP3			(4 << 0)
#define SII902X_TPI_AUDIO_CODING_MPEG2			(5 << 0)
#define SII902X_TPI_AUDIO_CODING_AAC			(6 << 0)
#define SII902X_TPI_AUDIO_CODING_DTS			(7 << 0)
#define SII902X_TPI_AUDIO_CODING_ATRAC			(8 << 0)
#define SII902X_TPI_AUDIO_MUTE_DISABLE			(0 << 4)
#define SII902X_TPI_AUDIO_MUTE_ENABLE			(1 << 4)
#define SII902X_TPI_AUDIO_LAYOUT_2_CHANNELS		(0 << 5)
#define SII902X_TPI_AUDIO_LAYOUT_8_CHANNELS		(1 << 5)
#define SII902X_TPI_AUDIO_INTERFACE_DISABLE		(0 << 6)
#define SII902X_TPI_AUDIO_INTERFACE_SPDIF		(1 << 6)
#define SII902X_TPI_AUDIO_INTERFACE_I2S			(2 << 6)

#define SII902X_TPI_AUDIO_CONFIG_BYTE3_REG	0x27
#define SII902X_TPI_AUDIO_FREQ_STREAM			(0 << 3)
#define SII902X_TPI_AUDIO_FREQ_32KHZ			(1 << 3)
#define SII902X_TPI_AUDIO_FREQ_44KHZ			(2 << 3)
#define SII902X_TPI_AUDIO_FREQ_48KHZ			(3 << 3)
#define SII902X_TPI_AUDIO_FREQ_88KHZ			(4 << 3)
#define SII902X_TPI_AUDIO_FREQ_96KHZ			(5 << 3)
#define SII902X_TPI_AUDIO_FREQ_176KHZ			(6 << 3)
#define SII902X_TPI_AUDIO_FREQ_192KHZ			(7 << 3)
#define SII902X_TPI_AUDIO_SAMPLE_SIZE_STREAM		(0 << 6)
#define SII902X_TPI_AUDIO_SAMPLE_SIZE_16		(1 << 6)
#define SII902X_TPI_AUDIO_SAMPLE_SIZE_20		(2 << 6)
#define SII902X_TPI_AUDIO_SAMPLE_SIZE_24		(3 << 6)

#define SII902X_TPI_AUDIO_CONFIG_BYTE4_REG	0x28

#define SII902X_INT_ENABLE			0x3c
#define SII902X_INT_STATUS			0x3d
#define SII902X_HOTPLUG_EVENT			BIT(0)
#define SII902X_PLUGGED_STATUS			BIT(2)

#define SII902X_REG_TPI_RQB			0xc7

/* Indirect internal register access */
#define SII902X_IND_SET_PAGE			0xbc
#define SII902X_IND_OFFSET			0xbd
#define SII902X_IND_VALUE			0xbe

#define SII902X_TPI_MISC_INFOFRAME_BASE		0xbf
#define SII902X_TPI_MISC_INFOFRAME_END		0xde
#define SII902X_TPI_MISC_INFOFRAME_SIZE	\
	(SII902X_TPI_MISC_INFOFRAME_END - SII902X_TPI_MISC_INFOFRAME_BASE)

#define SII902X_I2C_BUS_ACQUISITION_TIMEOUT_MS	500

struct sii902x {
	struct i2c_client *i2c;
	struct regmap *regmap;
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct gpio_desc *reset_gpio;
	struct i2c_mux_core *i2cmux;
	/*
	 * Mutex protects audio and video functions from interfering
	 * each other, by keeping their i2c command sequences atomic.
	 */
	struct mutex mutex;
	struct sii902x_audio {
		struct platform_device *pdev;
		struct clk *mclk;
		u32 i2s_fifo_sequence[4];
	} audio;
};

static int sii902x_read_unlocked(struct i2c_client *i2c, u8 reg, u8 *val)
{
	union i2c_smbus_data data;
	int ret;

	ret = __i2c_smbus_xfer(i2c->adapter, i2c->addr, i2c->flags,
			       I2C_SMBUS_READ, reg, I2C_SMBUS_BYTE_DATA, &data);

	if (ret < 0)
		return ret;

	*val = data.byte;
	return 0;
}

static int sii902x_write_unlocked(struct i2c_client *i2c, u8 reg, u8 val)
{
	union i2c_smbus_data data;

	data.byte = val;

	return __i2c_smbus_xfer(i2c->adapter, i2c->addr, i2c->flags,
				I2C_SMBUS_WRITE, reg, I2C_SMBUS_BYTE_DATA,
				&data);
}

static int sii902x_update_bits_unlocked(struct i2c_client *i2c, u8 reg, u8 mask,
					u8 val)
{
	int ret;
	u8 status;

	ret = sii902x_read_unlocked(i2c, reg, &status);
	if (ret)
		return ret;
	status &= ~mask;
	status |= val & mask;
	return sii902x_write_unlocked(i2c, reg, status);
}

static inline struct sii902x *bridge_to_sii902x(struct drm_bridge *bridge)
{
	return container_of(bridge, struct sii902x, bridge);
}

static inline struct sii902x *connector_to_sii902x(struct drm_connector *con)
{
	return container_of(con, struct sii902x, connector);
}

static void sii902x_reset(struct sii902x *sii902x)
{
	if (!sii902x->reset_gpio)
		return;

	gpiod_set_value(sii902x->reset_gpio, 1);

	/* The datasheet says treset-min = 100us. Make it 150us to be sure. */
	usleep_range(150, 200);

	gpiod_set_value(sii902x->reset_gpio, 0);
}

static enum drm_connector_status
sii902x_connector_detect(struct drm_connector *connector, bool force)
{
	struct sii902x *sii902x = connector_to_sii902x(connector);
	unsigned int status;

	mutex_lock(&sii902x->mutex);

	regmap_read(sii902x->regmap, SII902X_INT_STATUS, &status);

	mutex_unlock(&sii902x->mutex);

	return (status & SII902X_PLUGGED_STATUS) ?
	       connector_status_connected : connector_status_disconnected;
}

static const struct drm_connector_funcs sii902x_connector_funcs = {
	.detect = sii902x_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int sii902x_get_modes(struct drm_connector *connector)
{
	struct sii902x *sii902x = connector_to_sii902x(connector);
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	u8 output_mode = SII902X_SYS_CTRL_OUTPUT_DVI;
	struct edid *edid;
	int num = 0, ret;

	mutex_lock(&sii902x->mutex);

	edid = drm_get_edid(connector, sii902x->i2cmux->adapter[0]);
	drm_connector_update_edid_property(connector, edid);
	if (edid) {
		if (drm_detect_hdmi_monitor(edid))
			output_mode = SII902X_SYS_CTRL_OUTPUT_HDMI;

		num = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	ret = drm_display_info_set_bus_formats(&connector->display_info,
					       &bus_format, 1);
	if (ret)
		goto error_out;

	ret = regmap_update_bits(sii902x->regmap, SII902X_SYS_CTRL_DATA,
				 SII902X_SYS_CTRL_OUTPUT_MODE, output_mode);
	if (ret)
		goto error_out;

	ret = num;

error_out:
	mutex_unlock(&sii902x->mutex);

	return ret;
}

static enum drm_mode_status sii902x_mode_valid(struct drm_connector *connector,
					       struct drm_display_mode *mode)
{
	/* TODO: check mode */

	return MODE_OK;
}

static const struct drm_connector_helper_funcs sii902x_connector_helper_funcs = {
	.get_modes = sii902x_get_modes,
	.mode_valid = sii902x_mode_valid,
};

static void sii902x_bridge_disable(struct drm_bridge *bridge)
{
	struct sii902x *sii902x = bridge_to_sii902x(bridge);

	mutex_lock(&sii902x->mutex);

	regmap_update_bits(sii902x->regmap, SII902X_SYS_CTRL_DATA,
			   SII902X_SYS_CTRL_PWR_DWN,
			   SII902X_SYS_CTRL_PWR_DWN);

	mutex_unlock(&sii902x->mutex);
}

static void sii902x_bridge_enable(struct drm_bridge *bridge)
{
	struct sii902x *sii902x = bridge_to_sii902x(bridge);

	mutex_lock(&sii902x->mutex);

	regmap_update_bits(sii902x->regmap, SII902X_PWR_STATE_CTRL,
			   SII902X_AVI_POWER_STATE_MSK,
			   SII902X_AVI_POWER_STATE_D(0));
	regmap_update_bits(sii902x->regmap, SII902X_SYS_CTRL_DATA,
			   SII902X_SYS_CTRL_PWR_DWN, 0);

	mutex_unlock(&sii902x->mutex);
}

static void sii902x_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adj)
{
	struct sii902x *sii902x = bridge_to_sii902x(bridge);
	struct regmap *regmap = sii902x->regmap;
	u8 buf[HDMI_INFOFRAME_SIZE(AVI)];
	struct hdmi_avi_infoframe frame;
	u16 pixel_clock_10kHz = adj->clock / 10;
	int ret;

	buf[0] = pixel_clock_10kHz & 0xff;
	buf[1] = pixel_clock_10kHz >> 8;
	buf[2] = adj->vrefresh;
	buf[3] = 0x00;
	buf[4] = adj->hdisplay;
	buf[5] = adj->hdisplay >> 8;
	buf[6] = adj->vdisplay;
	buf[7] = adj->vdisplay >> 8;
	buf[8] = SII902X_TPI_CLK_RATIO_1X | SII902X_TPI_AVI_PIXEL_REP_NONE |
		 SII902X_TPI_AVI_PIXEL_REP_BUS_24BIT;
	buf[9] = SII902X_TPI_AVI_INPUT_RANGE_AUTO |
		 SII902X_TPI_AVI_INPUT_COLORSPACE_RGB;

	mutex_lock(&sii902x->mutex);

	ret = regmap_bulk_write(regmap, SII902X_TPI_VIDEO_DATA, buf, 10);
	if (ret)
		goto out;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame,
						       &sii902x->connector, adj);
	if (ret < 0) {
		DRM_ERROR("couldn't fill AVI infoframe\n");
		goto out;
	}

	ret = hdmi_avi_infoframe_pack(&frame, buf, sizeof(buf));
	if (ret < 0) {
		DRM_ERROR("failed to pack AVI infoframe: %d\n", ret);
		goto out;
	}

	/* Do not send the infoframe header, but keep the CRC field. */
	regmap_bulk_write(regmap, SII902X_TPI_AVI_INFOFRAME,
			  buf + HDMI_INFOFRAME_HEADER_SIZE - 1,
			  HDMI_AVI_INFOFRAME_SIZE + 1);

out:
	mutex_unlock(&sii902x->mutex);
}

static int sii902x_bridge_attach(struct drm_bridge *bridge)
{
	struct sii902x *sii902x = bridge_to_sii902x(bridge);
	struct drm_device *drm = bridge->dev;
	int ret;

	drm_connector_helper_add(&sii902x->connector,
				 &sii902x_connector_helper_funcs);

	if (!drm_core_check_feature(drm, DRIVER_ATOMIC)) {
		dev_err(&sii902x->i2c->dev,
			"sii902x driver is only compatible with DRM devices supporting atomic updates\n");
		return -ENOTSUPP;
	}

	ret = drm_connector_init(drm, &sii902x->connector,
				 &sii902x_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;

	if (sii902x->i2c->irq > 0)
		sii902x->connector.polled = DRM_CONNECTOR_POLL_HPD;
	else
		sii902x->connector.polled = DRM_CONNECTOR_POLL_CONNECT;

	drm_connector_attach_encoder(&sii902x->connector, bridge->encoder);

	return 0;
}

static const struct drm_bridge_funcs sii902x_bridge_funcs = {
	.attach = sii902x_bridge_attach,
	.mode_set = sii902x_bridge_mode_set,
	.disable = sii902x_bridge_disable,
	.enable = sii902x_bridge_enable,
};

static int sii902x_mute(struct sii902x *sii902x, bool mute)
{
	struct device *dev = &sii902x->i2c->dev;
	unsigned int val = mute ? SII902X_TPI_AUDIO_MUTE_ENABLE :
		SII902X_TPI_AUDIO_MUTE_DISABLE;

	dev_dbg(dev, "%s: %s\n", __func__, mute ? "Muted" : "Unmuted");

	return regmap_update_bits(sii902x->regmap,
				  SII902X_TPI_AUDIO_CONFIG_BYTE2_REG,
				  SII902X_TPI_AUDIO_MUTE_ENABLE, val);
}

static const int sii902x_mclk_div_table[] = {
	128, 256, 384, 512, 768, 1024, 1152, 192 };

static int sii902x_select_mclk_div(u8 *i2s_config_reg, unsigned int rate,
				   unsigned int mclk)
{
	int div = mclk / rate;
	int distance = 100000;
	u8 i, nearest = 0;

	for (i = 0; i < ARRAY_SIZE(sii902x_mclk_div_table); i++) {
		unsigned int d = abs(div - sii902x_mclk_div_table[i]);

		if (d >= distance)
			continue;

		nearest = i;
		distance = d;
		if (d == 0)
			break;
	}

	*i2s_config_reg |= nearest << 4;

	return sii902x_mclk_div_table[nearest];
}

static const struct sii902x_sample_freq {
	u32 freq;
	u8 val;
} sii902x_sample_freq[] = {
	{ .freq = 32000,	.val = SII902X_TPI_AUDIO_FREQ_32KHZ },
	{ .freq = 44000,	.val = SII902X_TPI_AUDIO_FREQ_44KHZ },
	{ .freq = 48000,	.val = SII902X_TPI_AUDIO_FREQ_48KHZ },
	{ .freq = 88000,	.val = SII902X_TPI_AUDIO_FREQ_88KHZ },
	{ .freq = 96000,	.val = SII902X_TPI_AUDIO_FREQ_96KHZ },
	{ .freq = 176000,	.val = SII902X_TPI_AUDIO_FREQ_176KHZ },
	{ .freq = 192000,	.val = SII902X_TPI_AUDIO_FREQ_192KHZ },
};

static int sii902x_audio_hw_params(struct device *dev, void *data,
				   struct hdmi_codec_daifmt *daifmt,
				   struct hdmi_codec_params *params)
{
	struct sii902x *sii902x = dev_get_drvdata(dev);
	u8 i2s_config_reg = SII902X_TPI_I2S_SD_DIRECTION_MSB_FIRST;
	u8 config_byte2_reg = (SII902X_TPI_AUDIO_INTERFACE_I2S |
			       SII902X_TPI_AUDIO_MUTE_ENABLE |
			       SII902X_TPI_AUDIO_CODING_PCM);
	u8 config_byte3_reg = 0;
	u8 infoframe_buf[HDMI_INFOFRAME_SIZE(AUDIO)];
	unsigned long mclk_rate;
	int i, ret;

	if (daifmt->bit_clk_master || daifmt->frame_clk_master) {
		dev_dbg(dev, "%s: I2S master mode not supported\n", __func__);
		return -EINVAL;
	}

	switch (daifmt->fmt) {
	case HDMI_I2S:
		i2s_config_reg |= SII902X_TPI_I2S_FIRST_BIT_SHIFT_YES |
			SII902X_TPI_I2S_SD_JUSTIFY_LEFT;
		break;
	case HDMI_RIGHT_J:
		i2s_config_reg |= SII902X_TPI_I2S_SD_JUSTIFY_RIGHT;
		break;
	case HDMI_LEFT_J:
		i2s_config_reg |= SII902X_TPI_I2S_SD_JUSTIFY_LEFT;
		break;
	default:
		dev_dbg(dev, "%s: Unsupported i2s format %u\n", __func__,
			daifmt->fmt);
		return -EINVAL;
	}

	if (daifmt->bit_clk_inv)
		i2s_config_reg |= SII902X_TPI_I2S_SCK_EDGE_FALLING;
	else
		i2s_config_reg |= SII902X_TPI_I2S_SCK_EDGE_RISING;

	if (daifmt->frame_clk_inv)
		i2s_config_reg |= SII902X_TPI_I2S_WS_POLARITY_LOW;
	else
		i2s_config_reg |= SII902X_TPI_I2S_WS_POLARITY_HIGH;

	if (params->channels > 2)
		config_byte2_reg |= SII902X_TPI_AUDIO_LAYOUT_8_CHANNELS;
	else
		config_byte2_reg |= SII902X_TPI_AUDIO_LAYOUT_2_CHANNELS;

	switch (params->sample_width) {
	case 16:
		config_byte3_reg |= SII902X_TPI_AUDIO_SAMPLE_SIZE_16;
		break;
	case 20:
		config_byte3_reg |= SII902X_TPI_AUDIO_SAMPLE_SIZE_20;
		break;
	case 24:
	case 32:
		config_byte3_reg |= SII902X_TPI_AUDIO_SAMPLE_SIZE_24;
		break;
	default:
		dev_err(dev, "%s: Unsupported sample width %u\n", __func__,
			params->sample_width);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(sii902x_sample_freq); i++) {
		if (params->sample_rate == sii902x_sample_freq[i].freq) {
			config_byte3_reg |= sii902x_sample_freq[i].val;
			break;
		}
	}

	ret = clk_prepare_enable(sii902x->audio.mclk);
	if (ret) {
		dev_err(dev, "Enabling mclk failed: %d\n", ret);
		return ret;
	}

	mclk_rate = clk_get_rate(sii902x->audio.mclk);

	ret = sii902x_select_mclk_div(&i2s_config_reg, params->sample_rate,
				      mclk_rate);
	if (mclk_rate != ret * params->sample_rate)
		dev_dbg(dev, "Inaccurate reference clock (%ld/%d != %u)\n",
			mclk_rate, ret, params->sample_rate);

	mutex_lock(&sii902x->mutex);

	ret = regmap_write(sii902x->regmap,
			   SII902X_TPI_AUDIO_CONFIG_BYTE2_REG,
			   config_byte2_reg);
	if (ret < 0)
		goto out;

	ret = regmap_write(sii902x->regmap, SII902X_TPI_I2S_INPUT_CONFIG_REG,
			   i2s_config_reg);
	if (ret)
		goto out;

	for (i = 0; i < ARRAY_SIZE(sii902x->audio.i2s_fifo_sequence) &&
		    sii902x->audio.i2s_fifo_sequence[i]; i++)
		regmap_write(sii902x->regmap,
			     SII902X_TPI_I2S_ENABLE_MAPPING_REG,
			     sii902x->audio.i2s_fifo_sequence[i]);

	ret = regmap_write(sii902x->regmap, SII902X_TPI_AUDIO_CONFIG_BYTE3_REG,
			   config_byte3_reg);
	if (ret)
		goto out;

	ret = regmap_bulk_write(sii902x->regmap, SII902X_TPI_I2S_STRM_HDR_BASE,
				params->iec.status,
				min((size_t) SII902X_TPI_I2S_STRM_HDR_SIZE,
				    sizeof(params->iec.status)));
	if (ret)
		goto out;

	ret = hdmi_audio_infoframe_pack(&params->cea, infoframe_buf,
					sizeof(infoframe_buf));
	if (ret < 0) {
		dev_err(dev, "%s: Failed to pack audio infoframe: %d\n",
			__func__, ret);
		goto out;
	}

	ret = regmap_bulk_write(sii902x->regmap,
				SII902X_TPI_MISC_INFOFRAME_BASE,
				infoframe_buf,
				min(ret, SII902X_TPI_MISC_INFOFRAME_SIZE));
	if (ret)
		goto out;

	/* Decode Level 0 Packets */
	ret = regmap_write(sii902x->regmap, SII902X_IND_SET_PAGE, 0x02);
	if (ret)
		goto out;

	ret = regmap_write(sii902x->regmap, SII902X_IND_OFFSET, 0x24);
	if (ret)
		goto out;

	ret = regmap_write(sii902x->regmap, SII902X_IND_VALUE, 0x02);
	if (ret)
		goto out;

	dev_dbg(dev, "%s: hdmi audio enabled\n", __func__);
out:
	mutex_unlock(&sii902x->mutex);

	if (ret) {
		clk_disable_unprepare(sii902x->audio.mclk);
		dev_err(dev, "%s: hdmi audio enable failed: %d\n", __func__,
			ret);
	}

	return ret;
}

static void sii902x_audio_shutdown(struct device *dev, void *data)
{
	struct sii902x *sii902x = dev_get_drvdata(dev);

	mutex_lock(&sii902x->mutex);

	regmap_write(sii902x->regmap, SII902X_TPI_AUDIO_CONFIG_BYTE2_REG,
		     SII902X_TPI_AUDIO_INTERFACE_DISABLE);

	mutex_unlock(&sii902x->mutex);

	clk_disable_unprepare(sii902x->audio.mclk);
}

static int sii902x_audio_digital_mute(struct device *dev,
				      void *data, bool enable)
{
	struct sii902x *sii902x = dev_get_drvdata(dev);

	mutex_lock(&sii902x->mutex);

	sii902x_mute(sii902x, enable);

	mutex_unlock(&sii902x->mutex);

	return 0;
}

static int sii902x_audio_get_eld(struct device *dev, void *data,
				 uint8_t *buf, size_t len)
{
	struct sii902x *sii902x = dev_get_drvdata(dev);

	mutex_lock(&sii902x->mutex);

	memcpy(buf, sii902x->connector.eld,
	       min(sizeof(sii902x->connector.eld), len));

	mutex_unlock(&sii902x->mutex);

	return 0;
}

static const struct hdmi_codec_ops sii902x_audio_codec_ops = {
	.hw_params = sii902x_audio_hw_params,
	.audio_shutdown = sii902x_audio_shutdown,
	.digital_mute = sii902x_audio_digital_mute,
	.get_eld = sii902x_audio_get_eld,
};

static int sii902x_audio_codec_init(struct sii902x *sii902x,
				    struct device *dev)
{
	static const u8 audio_fifo_id[] = {
		SII902X_TPI_I2S_CONFIG_FIFO0,
		SII902X_TPI_I2S_CONFIG_FIFO1,
		SII902X_TPI_I2S_CONFIG_FIFO2,
		SII902X_TPI_I2S_CONFIG_FIFO3,
	};
	static const u8 i2s_lane_id[] = {
		SII902X_TPI_I2S_SELECT_SD0,
		SII902X_TPI_I2S_SELECT_SD1,
		SII902X_TPI_I2S_SELECT_SD2,
		SII902X_TPI_I2S_SELECT_SD3,
	};
	struct hdmi_codec_pdata codec_data = {
		.ops = &sii902x_audio_codec_ops,
		.i2s = 1, /* Only i2s support for now. */
		.spdif = 0,
		.max_i2s_channels = 0,
	};
	u8 lanes[4];
	int num_lanes, i;

	if (!of_property_read_bool(dev->of_node, "#sound-dai-cells")) {
		dev_dbg(dev, "%s: No \"#sound-dai-cells\", no audio\n",
			__func__);
		return 0;
	}

	num_lanes = of_property_read_variable_u8_array(dev->of_node,
						       "sil,i2s-data-lanes",
						       lanes, 1,
						       ARRAY_SIZE(lanes));

	if (num_lanes == -EINVAL) {
		dev_dbg(dev,
			"%s: No \"sil,i2s-data-lanes\", use default <0>\n",
			__func__);
		num_lanes = 1;
		lanes[0] = 0;
	} else if (num_lanes < 0) {
		dev_err(dev,
			"%s: Error gettin \"sil,i2s-data-lanes\": %d\n",
			__func__, num_lanes);
		return num_lanes;
	}
	codec_data.max_i2s_channels = 2 * num_lanes;

	for (i = 0; i < num_lanes; i++)
		sii902x->audio.i2s_fifo_sequence[i] |= audio_fifo_id[i] |
			i2s_lane_id[lanes[i]] |	SII902X_TPI_I2S_FIFO_ENABLE;

	if (IS_ERR(sii902x->audio.mclk)) {
		dev_err(dev, "%s: No clock (audio mclk) found: %ld\n",
			__func__, PTR_ERR(sii902x->audio.mclk));
		return 0;
	}

	sii902x->audio.pdev = platform_device_register_data(
		dev, HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_AUTO,
		&codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(sii902x->audio.pdev);
}

static const struct regmap_range sii902x_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0xff },
};

static const struct regmap_access_table sii902x_volatile_table = {
	.yes_ranges = sii902x_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(sii902x_volatile_ranges),
};

static const struct regmap_config sii902x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.disable_locking = true, /* struct sii902x mutex should be enough */
	.max_register = SII902X_TPI_MISC_INFOFRAME_END,
	.volatile_table = &sii902x_volatile_table,
	.cache_type = REGCACHE_NONE,
};

static irqreturn_t sii902x_interrupt(int irq, void *data)
{
	struct sii902x *sii902x = data;
	unsigned int status = 0;

	mutex_lock(&sii902x->mutex);

	regmap_read(sii902x->regmap, SII902X_INT_STATUS, &status);
	regmap_write(sii902x->regmap, SII902X_INT_STATUS, status);

	mutex_unlock(&sii902x->mutex);

	if ((status & SII902X_HOTPLUG_EVENT) && sii902x->bridge.dev)
		drm_helper_hpd_irq_event(sii902x->bridge.dev);

	return IRQ_HANDLED;
}

/*
 * The purpose of sii902x_i2c_bypass_select is to enable the pass through
 * mode of the HDMI transmitter. Do not use regmap from within this function,
 * only use sii902x_*_unlocked functions to read/modify/write registers.
 * We are holding the parent adapter lock here, keep this in mind before
 * adding more i2c transactions.
 *
 * Also, since SII902X_SYS_CTRL_DATA is used with regmap_update_bits elsewhere
 * in this driver, we need to make sure that we only touch 0x1A[2:1] from
 * within sii902x_i2c_bypass_select and sii902x_i2c_bypass_deselect, and that
 * we leave the remaining bits as we have found them.
 */
static int sii902x_i2c_bypass_select(struct i2c_mux_core *mux, u32 chan_id)
{
	struct sii902x *sii902x = i2c_mux_priv(mux);
	struct device *dev = &sii902x->i2c->dev;
	unsigned long timeout;
	u8 status;
	int ret;

	ret = sii902x_update_bits_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
					   SII902X_SYS_CTRL_DDC_BUS_REQ,
					   SII902X_SYS_CTRL_DDC_BUS_REQ);
	if (ret)
		return ret;

	timeout = jiffies +
		  msecs_to_jiffies(SII902X_I2C_BUS_ACQUISITION_TIMEOUT_MS);
	do {
		ret = sii902x_read_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
					    &status);
		if (ret)
			return ret;
	} while (!(status & SII902X_SYS_CTRL_DDC_BUS_GRTD) &&
		 time_before(jiffies, timeout));

	if (!(status & SII902X_SYS_CTRL_DDC_BUS_GRTD)) {
		dev_err(dev, "Failed to acquire the i2c bus\n");
		return -ETIMEDOUT;
	}

	return sii902x_write_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
				      status);
}

/*
 * The purpose of sii902x_i2c_bypass_deselect is to disable the pass through
 * mode of the HDMI transmitter. Do not use regmap from within this function,
 * only use sii902x_*_unlocked functions to read/modify/write registers.
 * We are holding the parent adapter lock here, keep this in mind before
 * adding more i2c transactions.
 *
 * Also, since SII902X_SYS_CTRL_DATA is used with regmap_update_bits elsewhere
 * in this driver, we need to make sure that we only touch 0x1A[2:1] from
 * within sii902x_i2c_bypass_select and sii902x_i2c_bypass_deselect, and that
 * we leave the remaining bits as we have found them.
 */
static int sii902x_i2c_bypass_deselect(struct i2c_mux_core *mux, u32 chan_id)
{
	struct sii902x *sii902x = i2c_mux_priv(mux);
	struct device *dev = &sii902x->i2c->dev;
	unsigned long timeout;
	unsigned int retries;
	u8 status;
	int ret;

	/*
	 * When the HDMI transmitter is in pass through mode, we need an
	 * (undocumented) additional delay between STOP and START conditions
	 * to guarantee the bus won't get stuck.
	 */
	udelay(30);

	/*
	 * Sometimes the I2C bus can stall after failure to use the
	 * EDID channel. Retry a few times to see if things clear
	 * up, else continue anyway.
	 */
	retries = 5;
	do {
		ret = sii902x_read_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
					    &status);
		retries--;
	} while (ret && retries);
	if (ret) {
		dev_err(dev, "failed to read status (%d)\n", ret);
		return ret;
	}

	ret = sii902x_update_bits_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
					   SII902X_SYS_CTRL_DDC_BUS_REQ |
					   SII902X_SYS_CTRL_DDC_BUS_GRTD, 0);
	if (ret)
		return ret;

	timeout = jiffies +
		  msecs_to_jiffies(SII902X_I2C_BUS_ACQUISITION_TIMEOUT_MS);
	do {
		ret = sii902x_read_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
					    &status);
		if (ret)
			return ret;
	} while (status & (SII902X_SYS_CTRL_DDC_BUS_REQ |
			   SII902X_SYS_CTRL_DDC_BUS_GRTD) &&
		 time_before(jiffies, timeout));

	if (status & (SII902X_SYS_CTRL_DDC_BUS_REQ |
		      SII902X_SYS_CTRL_DDC_BUS_GRTD)) {
		dev_err(dev, "failed to release the i2c bus\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static const struct drm_bridge_timings default_sii902x_timings = {
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE
		 | DRM_BUS_FLAG_SYNC_SAMPLE_NEGEDGE
		 | DRM_BUS_FLAG_DE_HIGH,
};

static int sii902x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	unsigned int status = 0;
	struct sii902x *sii902x;
	u8 chipid[4];
	int ret;

	ret = i2c_check_functionality(client->adapter,
				      I2C_FUNC_SMBUS_BYTE_DATA);
	if (!ret) {
		dev_err(dev, "I2C adapter not suitable\n");
		return -EIO;
	}

	sii902x = devm_kzalloc(dev, sizeof(*sii902x), GFP_KERNEL);
	if (!sii902x)
		return -ENOMEM;

	sii902x->i2c = client;
	sii902x->regmap = devm_regmap_init_i2c(client, &sii902x_regmap_config);
	if (IS_ERR(sii902x->regmap))
		return PTR_ERR(sii902x->regmap);

	sii902x->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(sii902x->reset_gpio)) {
		dev_err(dev, "Failed to retrieve/request reset gpio: %ld\n",
			PTR_ERR(sii902x->reset_gpio));
		return PTR_ERR(sii902x->reset_gpio);
	}

	mutex_init(&sii902x->mutex);

	sii902x_reset(sii902x);

	ret = regmap_write(sii902x->regmap, SII902X_REG_TPI_RQB, 0x0);
	if (ret)
		return ret;

	ret = regmap_bulk_read(sii902x->regmap, SII902X_REG_CHIPID(0),
			       &chipid, 4);
	if (ret) {
		dev_err(dev, "regmap_read failed %d\n", ret);
		return ret;
	}

	if (chipid[0] != 0xb0) {
		dev_err(dev, "Invalid chipid: %02x (expecting 0xb0)\n",
			chipid[0]);
		return -EINVAL;
	}

	/* Clear all pending interrupts */
	regmap_read(sii902x->regmap, SII902X_INT_STATUS, &status);
	regmap_write(sii902x->regmap, SII902X_INT_STATUS, status);

	if (client->irq > 0) {
		regmap_write(sii902x->regmap, SII902X_INT_ENABLE,
			     SII902X_HOTPLUG_EVENT);

		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						sii902x_interrupt,
						IRQF_ONESHOT, dev_name(dev),
						sii902x);
		if (ret)
			return ret;
	}

	sii902x->bridge.funcs = &sii902x_bridge_funcs;
	sii902x->bridge.of_node = dev->of_node;
	sii902x->bridge.timings = &default_sii902x_timings;
	drm_bridge_add(&sii902x->bridge);

	sii902x_audio_codec_init(sii902x, dev);

	i2c_set_clientdata(client, sii902x);

	sii902x->i2cmux = i2c_mux_alloc(client->adapter, dev,
					1, 0, I2C_MUX_GATE,
					sii902x_i2c_bypass_select,
					sii902x_i2c_bypass_deselect);
	if (!sii902x->i2cmux)
		return -ENOMEM;

	sii902x->i2cmux->priv = sii902x;
	return i2c_mux_add_adapter(sii902x->i2cmux, 0, 0, 0);
}

static int sii902x_remove(struct i2c_client *client)

{
	struct sii902x *sii902x = i2c_get_clientdata(client);

	i2c_mux_del_adapters(sii902x->i2cmux);
	drm_bridge_remove(&sii902x->bridge);

	return 0;
}

static const struct of_device_id sii902x_dt_ids[] = {
	{ .compatible = "sil,sii9022", },
	{ }
};
MODULE_DEVICE_TABLE(of, sii902x_dt_ids);

static const struct i2c_device_id sii902x_i2c_ids[] = {
	{ "sii9022", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, sii902x_i2c_ids);

static struct i2c_driver sii902x_driver = {
	.probe = sii902x_probe,
	.remove = sii902x_remove,
	.driver = {
		.name = "sii902x",
		.of_match_table = sii902x_dt_ids,
	},
	.id_table = sii902x_i2c_ids,
};
module_i2c_driver(sii902x_driver);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@free-electrons.com>");
MODULE_DESCRIPTION("SII902x RGB -> HDMI bridges");
MODULE_LICENSE("GPL");
