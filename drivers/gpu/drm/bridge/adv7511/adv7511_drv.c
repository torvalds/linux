// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices ADV7511 HDMI transmitter driver
 *
 * Copyright 2012 Analog Devices Inc.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#include <media/cec.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "adv7511.h"

/* ADI recommended values for proper operation. */
static const struct reg_sequence adv7511_fixed_registers[] = {
	{ 0x98, 0x03 },
	{ 0x9a, 0xe0 },
	{ 0x9c, 0x30 },
	{ 0x9d, 0x61 },
	{ 0xa2, 0xa4 },
	{ 0xa3, 0xa4 },
	{ 0xe0, 0xd0 },
	{ 0xf9, 0x00 },
	{ 0x55, 0x02 },
};

/* -----------------------------------------------------------------------------
 * Register access
 */

static const uint8_t adv7511_register_defaults[] = {
	0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 00 */
	0x00, 0x00, 0x01, 0x0e, 0xbc, 0x18, 0x01, 0x13,
	0x25, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 10 */
	0x46, 0x62, 0x04, 0xa8, 0x00, 0x00, 0x1c, 0x84,
	0x1c, 0xbf, 0x04, 0xa8, 0x1e, 0x70, 0x02, 0x1e, /* 20 */
	0x00, 0x00, 0x04, 0xa8, 0x08, 0x12, 0x1b, 0xac,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 30 */
	0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xb0,
	0x00, 0x50, 0x90, 0x7e, 0x79, 0x70, 0x00, 0x00, /* 40 */
	0x00, 0xa8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x02, 0x0d, 0x00, 0x00, 0x00, 0x00, /* 50 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 60 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 70 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 80 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, /* 90 */
	0x0b, 0x02, 0x00, 0x18, 0x5a, 0x60, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x80, 0x08, 0x04, 0x00, 0x00, /* a0 */
	0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x14,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* b0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* c0 */
	0x00, 0x03, 0x00, 0x00, 0x02, 0x00, 0x01, 0x04,
	0x30, 0xff, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, /* d0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x01,
	0x80, 0x75, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, /* e0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x11, 0x00, /* f0 */
	0x00, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static bool adv7511_register_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADV7511_REG_CHIP_REVISION:
	case ADV7511_REG_SPDIF_FREQ:
	case ADV7511_REG_CTS_AUTOMATIC1:
	case ADV7511_REG_CTS_AUTOMATIC2:
	case ADV7511_REG_VIC_DETECTED:
	case ADV7511_REG_VIC_SEND:
	case ADV7511_REG_AUX_VIC_DETECTED:
	case ADV7511_REG_STATUS:
	case ADV7511_REG_GC(1):
	case ADV7511_REG_INT(0):
	case ADV7511_REG_INT(1):
	case ADV7511_REG_PLL_STATUS:
	case ADV7511_REG_AN(0):
	case ADV7511_REG_AN(1):
	case ADV7511_REG_AN(2):
	case ADV7511_REG_AN(3):
	case ADV7511_REG_AN(4):
	case ADV7511_REG_AN(5):
	case ADV7511_REG_AN(6):
	case ADV7511_REG_AN(7):
	case ADV7511_REG_HDCP_STATUS:
	case ADV7511_REG_BCAPS:
	case ADV7511_REG_BKSV(0):
	case ADV7511_REG_BKSV(1):
	case ADV7511_REG_BKSV(2):
	case ADV7511_REG_BKSV(3):
	case ADV7511_REG_BKSV(4):
	case ADV7511_REG_DDC_STATUS:
	case ADV7511_REG_EDID_READ_CTRL:
	case ADV7511_REG_BSTATUS(0):
	case ADV7511_REG_BSTATUS(1):
	case ADV7511_REG_CHIP_ID_HIGH:
	case ADV7511_REG_CHIP_ID_LOW:
		return true;
	}

	return false;
}

static const struct regmap_config adv7511_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0xff,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults_raw = adv7511_register_defaults,
	.num_reg_defaults_raw = ARRAY_SIZE(adv7511_register_defaults),

	.volatile_reg = adv7511_register_volatile,
};

/* -----------------------------------------------------------------------------
 * Hardware configuration
 */

static void adv7511_set_colormap(struct adv7511 *adv7511, bool enable,
				 const uint16_t *coeff,
				 unsigned int scaling_factor)
{
	unsigned int i;

	regmap_update_bits(adv7511->regmap, ADV7511_REG_CSC_UPPER(1),
			   ADV7511_CSC_UPDATE_MODE, ADV7511_CSC_UPDATE_MODE);

	if (enable) {
		for (i = 0; i < 12; ++i) {
			regmap_update_bits(adv7511->regmap,
					   ADV7511_REG_CSC_UPPER(i),
					   0x1f, coeff[i] >> 8);
			regmap_write(adv7511->regmap,
				     ADV7511_REG_CSC_LOWER(i),
				     coeff[i] & 0xff);
		}
	}

	if (enable)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_CSC_UPPER(0),
				   0xe0, 0x80 | (scaling_factor << 5));
	else
		regmap_update_bits(adv7511->regmap, ADV7511_REG_CSC_UPPER(0),
				   0x80, 0x00);

	regmap_update_bits(adv7511->regmap, ADV7511_REG_CSC_UPPER(1),
			   ADV7511_CSC_UPDATE_MODE, 0);
}

static int adv7511_packet_enable(struct adv7511 *adv7511, unsigned int packet)
{
	if (packet & 0xff)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_PACKET_ENABLE0,
				   packet, 0xff);

	if (packet & 0xff00) {
		packet >>= 8;
		regmap_update_bits(adv7511->regmap, ADV7511_REG_PACKET_ENABLE1,
				   packet, 0xff);
	}

	return 0;
}

static int adv7511_packet_disable(struct adv7511 *adv7511, unsigned int packet)
{
	if (packet & 0xff)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_PACKET_ENABLE0,
				   packet, 0x00);

	if (packet & 0xff00) {
		packet >>= 8;
		regmap_update_bits(adv7511->regmap, ADV7511_REG_PACKET_ENABLE1,
				   packet, 0x00);
	}

	return 0;
}

/* Coefficients for adv7511 color space conversion */
static const uint16_t adv7511_csc_ycbcr_to_rgb[] = {
	0x0734, 0x04ad, 0x0000, 0x1c1b,
	0x1ddc, 0x04ad, 0x1f24, 0x0135,
	0x0000, 0x04ad, 0x087c, 0x1b77,
};

static void adv7511_set_config_csc(struct adv7511 *adv7511,
				   struct drm_connector *connector,
				   bool rgb, bool hdmi_mode)
{
	struct adv7511_video_config config;
	bool output_format_422, output_format_ycbcr;
	unsigned int mode;
	uint8_t infoframe[17];

	config.hdmi_mode = hdmi_mode;

	hdmi_avi_infoframe_init(&config.avi_infoframe);

	config.avi_infoframe.scan_mode = HDMI_SCAN_MODE_UNDERSCAN;

	if (rgb) {
		config.csc_enable = false;
		config.avi_infoframe.colorspace = HDMI_COLORSPACE_RGB;
	} else {
		config.csc_scaling_factor = ADV7511_CSC_SCALING_4;
		config.csc_coefficents = adv7511_csc_ycbcr_to_rgb;

		if ((connector->display_info.color_formats &
		     DRM_COLOR_FORMAT_YCRCB422) &&
		    config.hdmi_mode) {
			config.csc_enable = false;
			config.avi_infoframe.colorspace =
				HDMI_COLORSPACE_YUV422;
		} else {
			config.csc_enable = true;
			config.avi_infoframe.colorspace = HDMI_COLORSPACE_RGB;
		}
	}

	if (config.hdmi_mode) {
		mode = ADV7511_HDMI_CFG_MODE_HDMI;

		switch (config.avi_infoframe.colorspace) {
		case HDMI_COLORSPACE_YUV444:
			output_format_422 = false;
			output_format_ycbcr = true;
			break;
		case HDMI_COLORSPACE_YUV422:
			output_format_422 = true;
			output_format_ycbcr = true;
			break;
		default:
			output_format_422 = false;
			output_format_ycbcr = false;
			break;
		}
	} else {
		mode = ADV7511_HDMI_CFG_MODE_DVI;
		output_format_422 = false;
		output_format_ycbcr = false;
	}

	adv7511_packet_disable(adv7511, ADV7511_PACKET_ENABLE_AVI_INFOFRAME);

	adv7511_set_colormap(adv7511, config.csc_enable,
			     config.csc_coefficents,
			     config.csc_scaling_factor);

	regmap_update_bits(adv7511->regmap, ADV7511_REG_VIDEO_INPUT_CFG1, 0x81,
			   (output_format_422 << 7) | output_format_ycbcr);

	regmap_update_bits(adv7511->regmap, ADV7511_REG_HDCP_HDMI_CFG,
			   ADV7511_HDMI_CFG_MODE_MASK, mode);

	hdmi_avi_infoframe_pack(&config.avi_infoframe, infoframe,
				sizeof(infoframe));

	/* The AVI infoframe id is not configurable */
	regmap_bulk_write(adv7511->regmap, ADV7511_REG_AVI_INFOFRAME_VERSION,
			  infoframe + 1, sizeof(infoframe) - 1);

	adv7511_packet_enable(adv7511, ADV7511_PACKET_ENABLE_AVI_INFOFRAME);
}

static void adv7511_set_link_config(struct adv7511 *adv7511,
				    const struct adv7511_link_config *config)
{
	/*
	 * The input style values documented in the datasheet don't match the
	 * hardware register field values :-(
	 */
	static const unsigned int input_styles[4] = { 0, 2, 1, 3 };

	unsigned int clock_delay;
	unsigned int color_depth;
	unsigned int input_id;

	clock_delay = (config->clock_delay + 1200) / 400;
	color_depth = config->input_color_depth == 8 ? 3
		    : (config->input_color_depth == 10 ? 1 : 2);

	/* TODO Support input ID 6 */
	if (config->input_colorspace != HDMI_COLORSPACE_YUV422)
		input_id = config->input_clock == ADV7511_INPUT_CLOCK_DDR
			 ? 5 : 0;
	else if (config->input_clock == ADV7511_INPUT_CLOCK_DDR)
		input_id = config->embedded_sync ? 8 : 7;
	else if (config->input_clock == ADV7511_INPUT_CLOCK_2X)
		input_id = config->embedded_sync ? 4 : 3;
	else
		input_id = config->embedded_sync ? 2 : 1;

	regmap_update_bits(adv7511->regmap, ADV7511_REG_I2C_FREQ_ID_CFG, 0xf,
			   input_id);
	regmap_update_bits(adv7511->regmap, ADV7511_REG_VIDEO_INPUT_CFG1, 0x7e,
			   (color_depth << 4) |
			   (input_styles[config->input_style] << 2));
	regmap_write(adv7511->regmap, ADV7511_REG_VIDEO_INPUT_CFG2,
		     config->input_justification << 3);
	regmap_write(adv7511->regmap, ADV7511_REG_TIMING_GEN_SEQ,
		     config->sync_pulse << 2);

	regmap_write(adv7511->regmap, 0xba, clock_delay << 5);

	adv7511->embedded_sync = config->embedded_sync;
	adv7511->hsync_polarity = config->hsync_polarity;
	adv7511->vsync_polarity = config->vsync_polarity;
	adv7511->rgb = config->input_colorspace == HDMI_COLORSPACE_RGB;
}

static void __adv7511_power_on(struct adv7511 *adv7511)
{
	adv7511->current_edid_segment = -1;

	regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER,
			   ADV7511_POWER_POWER_DOWN, 0);
	if (adv7511->i2c_main->irq) {
		/*
		 * Documentation says the INT_ENABLE registers are reset in
		 * POWER_DOWN mode. My 7511w preserved the bits, however.
		 * Still, let's be safe and stick to the documentation.
		 */
		regmap_write(adv7511->regmap, ADV7511_REG_INT_ENABLE(0),
			     ADV7511_INT0_EDID_READY | ADV7511_INT0_HPD);
		regmap_update_bits(adv7511->regmap,
				   ADV7511_REG_INT_ENABLE(1),
				   ADV7511_INT1_DDC_ERROR,
				   ADV7511_INT1_DDC_ERROR);
	}

	/*
	 * Per spec it is allowed to pulse the HPD signal to indicate that the
	 * EDID information has changed. Some monitors do this when they wakeup
	 * from standby or are enabled. When the HPD goes low the adv7511 is
	 * reset and the outputs are disabled which might cause the monitor to
	 * go to standby again. To avoid this we ignore the HPD pin for the
	 * first few seconds after enabling the output. On the other hand
	 * adv7535 require to enable HPD Override bit for proper HPD.
	 */
	if (adv7511->type == ADV7535)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER2,
				   ADV7535_REG_POWER2_HPD_OVERRIDE,
				   ADV7535_REG_POWER2_HPD_OVERRIDE);
	else
		regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER2,
				   ADV7511_REG_POWER2_HPD_SRC_MASK,
				   ADV7511_REG_POWER2_HPD_SRC_NONE);

	/* HACK: If we don't delay here edid probing doesn't work properly */
	msleep(200);
}

static void adv7511_power_on(struct adv7511 *adv7511)
{
	__adv7511_power_on(adv7511);

	/*
	 * Most of the registers are reset during power down or when HPD is low.
	 */
	regcache_sync(adv7511->regmap);

	if (adv7511->type == ADV7533 || adv7511->type == ADV7535)
		adv7533_dsi_power_on(adv7511);
	adv7511->powered = true;
}

static void __adv7511_power_off(struct adv7511 *adv7511)
{
	/* TODO: setup additional power down modes */
	if (adv7511->type == ADV7535)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER2,
				   ADV7535_REG_POWER2_HPD_OVERRIDE, 0);

	regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER,
			   ADV7511_POWER_POWER_DOWN,
			   ADV7511_POWER_POWER_DOWN);
	regmap_update_bits(adv7511->regmap,
			   ADV7511_REG_INT_ENABLE(1),
			   ADV7511_INT1_DDC_ERROR, 0);
	regcache_mark_dirty(adv7511->regmap);
}

static void adv7511_power_off(struct adv7511 *adv7511)
{
	__adv7511_power_off(adv7511);
	if (adv7511->type == ADV7533 || adv7511->type == ADV7535)
		adv7533_dsi_power_off(adv7511);
	adv7511->powered = false;
}

/* -----------------------------------------------------------------------------
 * Interrupt and hotplug detection
 */

static bool adv7511_hpd(struct adv7511 *adv7511)
{
	unsigned int irq0;
	int ret;

	ret = regmap_read(adv7511->regmap, ADV7511_REG_INT(0), &irq0);
	if (ret < 0)
		return false;

	if (irq0 & ADV7511_INT0_HPD) {
		regmap_write(adv7511->regmap, ADV7511_REG_INT(0),
			     ADV7511_INT0_HPD);
		return true;
	}

	return false;
}

static void adv7511_hpd_work(struct work_struct *work)
{
	struct adv7511 *adv7511 = container_of(work, struct adv7511, hpd_work);
	enum drm_connector_status status;
	unsigned int val;
	int ret;

	ret = regmap_read(adv7511->regmap, ADV7511_REG_STATUS, &val);
	if (ret < 0)
		status = connector_status_disconnected;
	else if (val & ADV7511_STATUS_HPD)
		status = connector_status_connected;
	else
		status = connector_status_disconnected;

	/*
	 * The bridge resets its registers on unplug. So when we get a plug
	 * event and we're already supposed to be powered, cycle the bridge to
	 * restore its state.
	 */
	if (status == connector_status_connected &&
	    adv7511->connector.status == connector_status_disconnected &&
	    adv7511->powered) {
		regcache_mark_dirty(adv7511->regmap);
		adv7511_power_on(adv7511);
	}

	if (adv7511->connector.status != status) {
		adv7511->connector.status = status;

		if (adv7511->connector.dev) {
			if (status == connector_status_disconnected)
				cec_phys_addr_invalidate(adv7511->cec_adap);
			drm_kms_helper_hotplug_event(adv7511->connector.dev);
		} else {
			drm_bridge_hpd_notify(&adv7511->bridge, status);
		}
	}
}

static int adv7511_irq_process(struct adv7511 *adv7511, bool process_hpd)
{
	unsigned int irq0, irq1;
	int ret;

	ret = regmap_read(adv7511->regmap, ADV7511_REG_INT(0), &irq0);
	if (ret < 0)
		return ret;

	ret = regmap_read(adv7511->regmap, ADV7511_REG_INT(1), &irq1);
	if (ret < 0)
		return ret;

	regmap_write(adv7511->regmap, ADV7511_REG_INT(0), irq0);
	regmap_write(adv7511->regmap, ADV7511_REG_INT(1), irq1);

	if (process_hpd && irq0 & ADV7511_INT0_HPD && adv7511->bridge.encoder)
		schedule_work(&adv7511->hpd_work);

	if (irq0 & ADV7511_INT0_EDID_READY || irq1 & ADV7511_INT1_DDC_ERROR) {
		adv7511->edid_read = true;

		if (adv7511->i2c_main->irq)
			wake_up_all(&adv7511->wq);
	}

#ifdef CONFIG_DRM_I2C_ADV7511_CEC
	adv7511_cec_irq_process(adv7511, irq1);
#endif

	return 0;
}

static irqreturn_t adv7511_irq_handler(int irq, void *devid)
{
	struct adv7511 *adv7511 = devid;
	int ret;

	ret = adv7511_irq_process(adv7511, true);
	return ret < 0 ? IRQ_NONE : IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * EDID retrieval
 */

static int adv7511_wait_for_edid(struct adv7511 *adv7511, int timeout)
{
	int ret;

	if (adv7511->i2c_main->irq) {
		ret = wait_event_interruptible_timeout(adv7511->wq,
				adv7511->edid_read, msecs_to_jiffies(timeout));
	} else {
		for (; timeout > 0; timeout -= 25) {
			ret = adv7511_irq_process(adv7511, false);
			if (ret < 0)
				break;

			if (adv7511->edid_read)
				break;

			msleep(25);
		}
	}

	return adv7511->edid_read ? 0 : -EIO;
}

static int adv7511_get_edid_block(void *data, u8 *buf, unsigned int block,
				  size_t len)
{
	struct adv7511 *adv7511 = data;
	struct i2c_msg xfer[2];
	uint8_t offset;
	unsigned int i;
	int ret;

	if (len > 128)
		return -EINVAL;

	if (adv7511->current_edid_segment != block / 2) {
		unsigned int status;

		ret = regmap_read(adv7511->regmap, ADV7511_REG_DDC_STATUS,
				  &status);
		if (ret < 0)
			return ret;

		if (status != 2) {
			adv7511->edid_read = false;
			regmap_write(adv7511->regmap, ADV7511_REG_EDID_SEGMENT,
				     block);
			ret = adv7511_wait_for_edid(adv7511, 200);
			if (ret < 0)
				return ret;
		}

		/* Break this apart, hopefully more I2C controllers will
		 * support 64 byte transfers than 256 byte transfers
		 */

		xfer[0].addr = adv7511->i2c_edid->addr;
		xfer[0].flags = 0;
		xfer[0].len = 1;
		xfer[0].buf = &offset;
		xfer[1].addr = adv7511->i2c_edid->addr;
		xfer[1].flags = I2C_M_RD;
		xfer[1].len = 64;
		xfer[1].buf = adv7511->edid_buf;

		offset = 0;

		for (i = 0; i < 4; ++i) {
			ret = i2c_transfer(adv7511->i2c_edid->adapter, xfer,
					   ARRAY_SIZE(xfer));
			if (ret < 0)
				return ret;
			else if (ret != 2)
				return -EIO;

			xfer[1].buf += 64;
			offset += 64;
		}

		adv7511->current_edid_segment = block / 2;
	}

	if (block % 2 == 0)
		memcpy(buf, adv7511->edid_buf, len);
	else
		memcpy(buf, adv7511->edid_buf + 128, len);

	return 0;
}

/* -----------------------------------------------------------------------------
 * ADV75xx helpers
 */

static struct edid *adv7511_get_edid(struct adv7511 *adv7511,
				     struct drm_connector *connector)
{
	struct edid *edid;

	/* Reading the EDID only works if the device is powered */
	if (!adv7511->powered) {
		unsigned int edid_i2c_addr =
					(adv7511->i2c_edid->addr << 1);

		__adv7511_power_on(adv7511);

		/* Reset the EDID_I2C_ADDR register as it might be cleared */
		regmap_write(adv7511->regmap, ADV7511_REG_EDID_I2C_ADDR,
			     edid_i2c_addr);
	}

	edid = drm_do_get_edid(connector, adv7511_get_edid_block, adv7511);

	if (!adv7511->powered)
		__adv7511_power_off(adv7511);

	adv7511_set_config_csc(adv7511, connector, adv7511->rgb,
			       drm_detect_hdmi_monitor(edid));

	cec_s_phys_addr_from_edid(adv7511->cec_adap, edid);

	return edid;
}

static int adv7511_get_modes(struct adv7511 *adv7511,
			     struct drm_connector *connector)
{
	struct edid *edid;
	unsigned int count;

	edid = adv7511_get_edid(adv7511, connector);

	drm_connector_update_edid_property(connector, edid);
	count = drm_add_edid_modes(connector, edid);

	kfree(edid);

	return count;
}

static enum drm_connector_status
adv7511_detect(struct adv7511 *adv7511, struct drm_connector *connector)
{
	enum drm_connector_status status;
	unsigned int val;
	bool hpd;
	int ret;

	ret = regmap_read(adv7511->regmap, ADV7511_REG_STATUS, &val);
	if (ret < 0)
		return connector_status_disconnected;

	if (val & ADV7511_STATUS_HPD)
		status = connector_status_connected;
	else
		status = connector_status_disconnected;

	hpd = adv7511_hpd(adv7511);

	/* The chip resets itself when the cable is disconnected, so in case
	 * there is a pending HPD interrupt and the cable is connected there was
	 * at least one transition from disconnected to connected and the chip
	 * has to be reinitialized. */
	if (status == connector_status_connected && hpd && adv7511->powered) {
		regcache_mark_dirty(adv7511->regmap);
		adv7511_power_on(adv7511);
		if (connector)
			adv7511_get_modes(adv7511, connector);
		if (adv7511->status == connector_status_connected)
			status = connector_status_disconnected;
	} else {
		/* Renable HPD sensing */
		if (adv7511->type == ADV7535)
			regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER2,
					   ADV7535_REG_POWER2_HPD_OVERRIDE,
					   ADV7535_REG_POWER2_HPD_OVERRIDE);
		else
			regmap_update_bits(adv7511->regmap, ADV7511_REG_POWER2,
					   ADV7511_REG_POWER2_HPD_SRC_MASK,
					   ADV7511_REG_POWER2_HPD_SRC_BOTH);
	}

	adv7511->status = status;
	return status;
}

static enum drm_mode_status adv7511_mode_valid(struct adv7511 *adv7511,
			      const struct drm_display_mode *mode)
{
	if (mode->clock > 165000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static void adv7511_mode_set(struct adv7511 *adv7511,
			     const struct drm_display_mode *mode,
			     const struct drm_display_mode *adj_mode)
{
	unsigned int low_refresh_rate;
	unsigned int hsync_polarity = 0;
	unsigned int vsync_polarity = 0;

	if (adv7511->embedded_sync) {
		unsigned int hsync_offset, hsync_len;
		unsigned int vsync_offset, vsync_len;

		hsync_offset = adj_mode->crtc_hsync_start -
			       adj_mode->crtc_hdisplay;
		vsync_offset = adj_mode->crtc_vsync_start -
			       adj_mode->crtc_vdisplay;
		hsync_len = adj_mode->crtc_hsync_end -
			    adj_mode->crtc_hsync_start;
		vsync_len = adj_mode->crtc_vsync_end -
			    adj_mode->crtc_vsync_start;

		/* The hardware vsync generator has a off-by-one bug */
		vsync_offset += 1;

		regmap_write(adv7511->regmap, ADV7511_REG_HSYNC_PLACEMENT_MSB,
			     ((hsync_offset >> 10) & 0x7) << 5);
		regmap_write(adv7511->regmap, ADV7511_REG_SYNC_DECODER(0),
			     (hsync_offset >> 2) & 0xff);
		regmap_write(adv7511->regmap, ADV7511_REG_SYNC_DECODER(1),
			     ((hsync_offset & 0x3) << 6) |
			     ((hsync_len >> 4) & 0x3f));
		regmap_write(adv7511->regmap, ADV7511_REG_SYNC_DECODER(2),
			     ((hsync_len & 0xf) << 4) |
			     ((vsync_offset >> 6) & 0xf));
		regmap_write(adv7511->regmap, ADV7511_REG_SYNC_DECODER(3),
			     ((vsync_offset & 0x3f) << 2) |
			     ((vsync_len >> 8) & 0x3));
		regmap_write(adv7511->regmap, ADV7511_REG_SYNC_DECODER(4),
			     vsync_len & 0xff);

		hsync_polarity = !(adj_mode->flags & DRM_MODE_FLAG_PHSYNC);
		vsync_polarity = !(adj_mode->flags & DRM_MODE_FLAG_PVSYNC);
	} else {
		enum adv7511_sync_polarity mode_hsync_polarity;
		enum adv7511_sync_polarity mode_vsync_polarity;

		/**
		 * If the input signal is always low or always high we want to
		 * invert or let it passthrough depending on the polarity of the
		 * current mode.
		 **/
		if (adj_mode->flags & DRM_MODE_FLAG_NHSYNC)
			mode_hsync_polarity = ADV7511_SYNC_POLARITY_LOW;
		else
			mode_hsync_polarity = ADV7511_SYNC_POLARITY_HIGH;

		if (adj_mode->flags & DRM_MODE_FLAG_NVSYNC)
			mode_vsync_polarity = ADV7511_SYNC_POLARITY_LOW;
		else
			mode_vsync_polarity = ADV7511_SYNC_POLARITY_HIGH;

		if (adv7511->hsync_polarity != mode_hsync_polarity &&
		    adv7511->hsync_polarity !=
		    ADV7511_SYNC_POLARITY_PASSTHROUGH)
			hsync_polarity = 1;

		if (adv7511->vsync_polarity != mode_vsync_polarity &&
		    adv7511->vsync_polarity !=
		    ADV7511_SYNC_POLARITY_PASSTHROUGH)
			vsync_polarity = 1;
	}

	if (drm_mode_vrefresh(mode) <= 24)
		low_refresh_rate = ADV7511_LOW_REFRESH_RATE_24HZ;
	else if (drm_mode_vrefresh(mode) <= 25)
		low_refresh_rate = ADV7511_LOW_REFRESH_RATE_25HZ;
	else if (drm_mode_vrefresh(mode) <= 30)
		low_refresh_rate = ADV7511_LOW_REFRESH_RATE_30HZ;
	else
		low_refresh_rate = ADV7511_LOW_REFRESH_RATE_NONE;

	regmap_update_bits(adv7511->regmap, 0xfb,
		0x6, low_refresh_rate << 1);
	regmap_update_bits(adv7511->regmap, 0x17,
		0x60, (vsync_polarity << 6) | (hsync_polarity << 5));

	drm_mode_copy(&adv7511->curr_mode, adj_mode);

	/*
	 * TODO Test first order 4:2:2 to 4:4:4 up conversion method, which is
	 * supposed to give better results.
	 */

	adv7511->f_tmds = mode->clock;
}

/* -----------------------------------------------------------------------------
 * DRM Connector Operations
 */

static struct adv7511 *connector_to_adv7511(struct drm_connector *connector)
{
	return container_of(connector, struct adv7511, connector);
}

static int adv7511_connector_get_modes(struct drm_connector *connector)
{
	struct adv7511 *adv = connector_to_adv7511(connector);

	return adv7511_get_modes(adv, connector);
}

static enum drm_mode_status
adv7511_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	struct adv7511 *adv = connector_to_adv7511(connector);

	return adv7511_mode_valid(adv, mode);
}

static struct drm_connector_helper_funcs adv7511_connector_helper_funcs = {
	.get_modes = adv7511_connector_get_modes,
	.mode_valid = adv7511_connector_mode_valid,
};

static enum drm_connector_status
adv7511_connector_detect(struct drm_connector *connector, bool force)
{
	struct adv7511 *adv = connector_to_adv7511(connector);

	return adv7511_detect(adv, connector);
}

static const struct drm_connector_funcs adv7511_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = adv7511_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int adv7511_connector_init(struct adv7511 *adv)
{
	struct drm_bridge *bridge = &adv->bridge;
	int ret;

	if (!bridge->encoder) {
		DRM_ERROR("Parent encoder object not found");
		return -ENODEV;
	}

	if (adv->i2c_main->irq)
		adv->connector.polled = DRM_CONNECTOR_POLL_HPD;
	else
		adv->connector.polled = DRM_CONNECTOR_POLL_CONNECT |
				DRM_CONNECTOR_POLL_DISCONNECT;

	ret = drm_connector_init(bridge->dev, &adv->connector,
				 &adv7511_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret < 0) {
		DRM_ERROR("Failed to initialize connector with drm\n");
		return ret;
	}
	drm_connector_helper_add(&adv->connector,
				 &adv7511_connector_helper_funcs);
	drm_connector_attach_encoder(&adv->connector, bridge->encoder);

	return 0;
}

/* -----------------------------------------------------------------------------
 * DRM Bridge Operations
 */

static struct adv7511 *bridge_to_adv7511(struct drm_bridge *bridge)
{
	return container_of(bridge, struct adv7511, bridge);
}

static void adv7511_bridge_enable(struct drm_bridge *bridge)
{
	struct adv7511 *adv = bridge_to_adv7511(bridge);

	adv7511_power_on(adv);
}

static void adv7511_bridge_disable(struct drm_bridge *bridge)
{
	struct adv7511 *adv = bridge_to_adv7511(bridge);

	adv7511_power_off(adv);
}

static void adv7511_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adj_mode)
{
	struct adv7511 *adv = bridge_to_adv7511(bridge);

	adv7511_mode_set(adv, mode, adj_mode);
}

static enum drm_mode_status adv7511_bridge_mode_valid(struct drm_bridge *bridge,
						      const struct drm_display_info *info,
		const struct drm_display_mode *mode)
{
	struct adv7511 *adv = bridge_to_adv7511(bridge);

	if (adv->type == ADV7533 || adv->type == ADV7535)
		return adv7533_mode_valid(adv, mode);
	else
		return adv7511_mode_valid(adv, mode);
}

static int adv7511_bridge_attach(struct drm_bridge *bridge,
				 enum drm_bridge_attach_flags flags)
{
	struct adv7511 *adv = bridge_to_adv7511(bridge);
	int ret = 0;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		ret = adv7511_connector_init(adv);
		if (ret < 0)
			return ret;
	}

	if (adv->type == ADV7533 || adv->type == ADV7535)
		ret = adv7533_attach_dsi(adv);

	if (adv->i2c_main->irq)
		regmap_write(adv->regmap, ADV7511_REG_INT_ENABLE(0),
			     ADV7511_INT0_HPD);

	return ret;
}

static enum drm_connector_status adv7511_bridge_detect(struct drm_bridge *bridge)
{
	struct adv7511 *adv = bridge_to_adv7511(bridge);

	return adv7511_detect(adv, NULL);
}

static struct edid *adv7511_bridge_get_edid(struct drm_bridge *bridge,
					    struct drm_connector *connector)
{
	struct adv7511 *adv = bridge_to_adv7511(bridge);

	return adv7511_get_edid(adv, connector);
}

static void adv7511_bridge_hpd_notify(struct drm_bridge *bridge,
				      enum drm_connector_status status)
{
	struct adv7511 *adv = bridge_to_adv7511(bridge);

	if (status == connector_status_disconnected)
		cec_phys_addr_invalidate(adv->cec_adap);
}

static const struct drm_bridge_funcs adv7511_bridge_funcs = {
	.enable = adv7511_bridge_enable,
	.disable = adv7511_bridge_disable,
	.mode_set = adv7511_bridge_mode_set,
	.mode_valid = adv7511_bridge_mode_valid,
	.attach = adv7511_bridge_attach,
	.detect = adv7511_bridge_detect,
	.get_edid = adv7511_bridge_get_edid,
	.hpd_notify = adv7511_bridge_hpd_notify,
};

/* -----------------------------------------------------------------------------
 * Probe & remove
 */

static const char * const adv7511_supply_names[] = {
	"avdd",
	"dvdd",
	"pvdd",
	"bgvdd",
	"dvdd-3v",
};

static const char * const adv7533_supply_names[] = {
	"avdd",
	"dvdd",
	"pvdd",
	"a2vdd",
	"v3p3",
	"v1p2",
};

static int adv7511_init_regulators(struct adv7511 *adv)
{
	struct device *dev = &adv->i2c_main->dev;
	const char * const *supply_names;
	unsigned int i;
	int ret;

	if (adv->type == ADV7511) {
		supply_names = adv7511_supply_names;
		adv->num_supplies = ARRAY_SIZE(adv7511_supply_names);
	} else {
		supply_names = adv7533_supply_names;
		adv->num_supplies = ARRAY_SIZE(adv7533_supply_names);
	}

	adv->supplies = devm_kcalloc(dev, adv->num_supplies,
				     sizeof(*adv->supplies), GFP_KERNEL);
	if (!adv->supplies)
		return -ENOMEM;

	for (i = 0; i < adv->num_supplies; i++)
		adv->supplies[i].supply = supply_names[i];

	ret = devm_regulator_bulk_get(dev, adv->num_supplies, adv->supplies);
	if (ret)
		return ret;

	return regulator_bulk_enable(adv->num_supplies, adv->supplies);
}

static void adv7511_uninit_regulators(struct adv7511 *adv)
{
	regulator_bulk_disable(adv->num_supplies, adv->supplies);
}

static bool adv7511_cec_register_volatile(struct device *dev, unsigned int reg)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct adv7511 *adv7511 = i2c_get_clientdata(i2c);

	if (adv7511->type == ADV7533 || adv7511->type == ADV7535)
		reg -= ADV7533_REG_CEC_OFFSET;

	switch (reg) {
	case ADV7511_REG_CEC_RX_FRAME_HDR:
	case ADV7511_REG_CEC_RX_FRAME_DATA0...
		ADV7511_REG_CEC_RX_FRAME_DATA0 + 14:
	case ADV7511_REG_CEC_RX_FRAME_LEN:
	case ADV7511_REG_CEC_RX_BUFFERS:
	case ADV7511_REG_CEC_TX_LOW_DRV_CNT:
		return true;
	}

	return false;
}

static const struct regmap_config adv7511_cec_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0xff,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = adv7511_cec_register_volatile,
};

static int adv7511_init_cec_regmap(struct adv7511 *adv)
{
	int ret;

	adv->i2c_cec = i2c_new_ancillary_device(adv->i2c_main, "cec",
						ADV7511_CEC_I2C_ADDR_DEFAULT);
	if (IS_ERR(adv->i2c_cec))
		return PTR_ERR(adv->i2c_cec);

	regmap_write(adv->regmap, ADV7511_REG_CEC_I2C_ADDR,
		     adv->i2c_cec->addr << 1);

	i2c_set_clientdata(adv->i2c_cec, adv);

	adv->regmap_cec = devm_regmap_init_i2c(adv->i2c_cec,
					&adv7511_cec_regmap_config);
	if (IS_ERR(adv->regmap_cec)) {
		ret = PTR_ERR(adv->regmap_cec);
		goto err;
	}

	if (adv->type == ADV7533 || adv->type == ADV7535) {
		ret = adv7533_patch_cec_registers(adv);
		if (ret)
			goto err;
	}

	return 0;
err:
	i2c_unregister_device(adv->i2c_cec);
	return ret;
}

static int adv7511_parse_dt(struct device_node *np,
			    struct adv7511_link_config *config)
{
	const char *str;
	int ret;

	of_property_read_u32(np, "adi,input-depth", &config->input_color_depth);
	if (config->input_color_depth != 8 && config->input_color_depth != 10 &&
	    config->input_color_depth != 12)
		return -EINVAL;

	ret = of_property_read_string(np, "adi,input-colorspace", &str);
	if (ret < 0)
		return ret;

	if (!strcmp(str, "rgb"))
		config->input_colorspace = HDMI_COLORSPACE_RGB;
	else if (!strcmp(str, "yuv422"))
		config->input_colorspace = HDMI_COLORSPACE_YUV422;
	else if (!strcmp(str, "yuv444"))
		config->input_colorspace = HDMI_COLORSPACE_YUV444;
	else
		return -EINVAL;

	ret = of_property_read_string(np, "adi,input-clock", &str);
	if (ret < 0)
		return ret;

	if (!strcmp(str, "1x"))
		config->input_clock = ADV7511_INPUT_CLOCK_1X;
	else if (!strcmp(str, "2x"))
		config->input_clock = ADV7511_INPUT_CLOCK_2X;
	else if (!strcmp(str, "ddr"))
		config->input_clock = ADV7511_INPUT_CLOCK_DDR;
	else
		return -EINVAL;

	if (config->input_colorspace == HDMI_COLORSPACE_YUV422 ||
	    config->input_clock != ADV7511_INPUT_CLOCK_1X) {
		ret = of_property_read_u32(np, "adi,input-style",
					   &config->input_style);
		if (ret)
			return ret;

		if (config->input_style < 1 || config->input_style > 3)
			return -EINVAL;

		ret = of_property_read_string(np, "adi,input-justification",
					      &str);
		if (ret < 0)
			return ret;

		if (!strcmp(str, "left"))
			config->input_justification =
				ADV7511_INPUT_JUSTIFICATION_LEFT;
		else if (!strcmp(str, "evenly"))
			config->input_justification =
				ADV7511_INPUT_JUSTIFICATION_EVENLY;
		else if (!strcmp(str, "right"))
			config->input_justification =
				ADV7511_INPUT_JUSTIFICATION_RIGHT;
		else
			return -EINVAL;

	} else {
		config->input_style = 1;
		config->input_justification = ADV7511_INPUT_JUSTIFICATION_LEFT;
	}

	of_property_read_u32(np, "adi,clock-delay", &config->clock_delay);
	if (config->clock_delay < -1200 || config->clock_delay > 1600)
		return -EINVAL;

	config->embedded_sync = of_property_read_bool(np, "adi,embedded-sync");

	/* Hardcode the sync pulse configurations for now. */
	config->sync_pulse = ADV7511_INPUT_SYNC_PULSE_NONE;
	config->vsync_polarity = ADV7511_SYNC_POLARITY_PASSTHROUGH;
	config->hsync_polarity = ADV7511_SYNC_POLARITY_PASSTHROUGH;

	return 0;
}

static int adv7511_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct adv7511_link_config link_config;
	struct adv7511 *adv7511;
	struct device *dev = &i2c->dev;
	unsigned int val;
	int ret;

	if (!dev->of_node)
		return -EINVAL;

	adv7511 = devm_kzalloc(dev, sizeof(*adv7511), GFP_KERNEL);
	if (!adv7511)
		return -ENOMEM;

	adv7511->i2c_main = i2c;
	adv7511->powered = false;
	adv7511->status = connector_status_disconnected;

	if (dev->of_node)
		adv7511->type = (enum adv7511_type)of_device_get_match_data(dev);
	else
		adv7511->type = id->driver_data;

	memset(&link_config, 0, sizeof(link_config));

	if (adv7511->type == ADV7511)
		ret = adv7511_parse_dt(dev->of_node, &link_config);
	else
		ret = adv7533_parse_dt(dev->of_node, adv7511);
	if (ret)
		return ret;

	ret = adv7511_init_regulators(adv7511);
	if (ret) {
		dev_err(dev, "failed to init regulators\n");
		return ret;
	}

	/*
	 * The power down GPIO is optional. If present, toggle it from active to
	 * inactive to wake up the encoder.
	 */
	adv7511->gpio_pd = devm_gpiod_get_optional(dev, "pd", GPIOD_OUT_HIGH);
	if (IS_ERR(adv7511->gpio_pd)) {
		ret = PTR_ERR(adv7511->gpio_pd);
		goto uninit_regulators;
	}

	if (adv7511->gpio_pd) {
		usleep_range(5000, 6000);
		gpiod_set_value_cansleep(adv7511->gpio_pd, 0);
	}

	adv7511->regmap = devm_regmap_init_i2c(i2c, &adv7511_regmap_config);
	if (IS_ERR(adv7511->regmap)) {
		ret = PTR_ERR(adv7511->regmap);
		goto uninit_regulators;
	}

	ret = regmap_read(adv7511->regmap, ADV7511_REG_CHIP_REVISION, &val);
	if (ret)
		goto uninit_regulators;
	dev_dbg(dev, "Rev. %d\n", val);

	if (adv7511->type == ADV7511)
		ret = regmap_register_patch(adv7511->regmap,
					    adv7511_fixed_registers,
					    ARRAY_SIZE(adv7511_fixed_registers));
	else
		ret = adv7533_patch_registers(adv7511);
	if (ret)
		goto uninit_regulators;

	adv7511_packet_disable(adv7511, 0xffff);

	adv7511->i2c_edid = i2c_new_ancillary_device(i2c, "edid",
					ADV7511_EDID_I2C_ADDR_DEFAULT);
	if (IS_ERR(adv7511->i2c_edid)) {
		ret = PTR_ERR(adv7511->i2c_edid);
		goto uninit_regulators;
	}

	regmap_write(adv7511->regmap, ADV7511_REG_EDID_I2C_ADDR,
		     adv7511->i2c_edid->addr << 1);

	adv7511->i2c_packet = i2c_new_ancillary_device(i2c, "packet",
					ADV7511_PACKET_I2C_ADDR_DEFAULT);
	if (IS_ERR(adv7511->i2c_packet)) {
		ret = PTR_ERR(adv7511->i2c_packet);
		goto err_i2c_unregister_edid;
	}

	regmap_write(adv7511->regmap, ADV7511_REG_PACKET_I2C_ADDR,
		     adv7511->i2c_packet->addr << 1);

	ret = adv7511_init_cec_regmap(adv7511);
	if (ret)
		goto err_i2c_unregister_packet;

	INIT_WORK(&adv7511->hpd_work, adv7511_hpd_work);

	if (i2c->irq) {
		init_waitqueue_head(&adv7511->wq);

		ret = devm_request_threaded_irq(dev, i2c->irq, NULL,
						adv7511_irq_handler,
						IRQF_ONESHOT, dev_name(dev),
						adv7511);
		if (ret)
			goto err_unregister_cec;
	}

	adv7511_power_off(adv7511);

	i2c_set_clientdata(i2c, adv7511);

	if (adv7511->type == ADV7511)
		adv7511_set_link_config(adv7511, &link_config);

	ret = adv7511_cec_init(dev, adv7511);
	if (ret)
		goto err_unregister_cec;

	adv7511->bridge.funcs = &adv7511_bridge_funcs;
	adv7511->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID
			    | DRM_BRIDGE_OP_HPD;
	adv7511->bridge.of_node = dev->of_node;
	adv7511->bridge.type = DRM_MODE_CONNECTOR_HDMIA;

	drm_bridge_add(&adv7511->bridge);

	adv7511_audio_init(dev, adv7511);
	return 0;

err_unregister_cec:
	cec_unregister_adapter(adv7511->cec_adap);
	i2c_unregister_device(adv7511->i2c_cec);
	if (adv7511->cec_clk)
		clk_disable_unprepare(adv7511->cec_clk);
err_i2c_unregister_packet:
	i2c_unregister_device(adv7511->i2c_packet);
err_i2c_unregister_edid:
	i2c_unregister_device(adv7511->i2c_edid);
uninit_regulators:
	adv7511_uninit_regulators(adv7511);

	return ret;
}

static int adv7511_remove(struct i2c_client *i2c)
{
	struct adv7511 *adv7511 = i2c_get_clientdata(i2c);

	if (adv7511->type == ADV7533 || adv7511->type == ADV7535)
		adv7533_detach_dsi(adv7511);
	i2c_unregister_device(adv7511->i2c_cec);
	if (adv7511->cec_clk)
		clk_disable_unprepare(adv7511->cec_clk);

	adv7511_uninit_regulators(adv7511);

	drm_bridge_remove(&adv7511->bridge);

	adv7511_audio_exit(adv7511);

	cec_unregister_adapter(adv7511->cec_adap);

	i2c_unregister_device(adv7511->i2c_packet);
	i2c_unregister_device(adv7511->i2c_edid);

	return 0;
}

static const struct i2c_device_id adv7511_i2c_ids[] = {
	{ "adv7511", ADV7511 },
	{ "adv7511w", ADV7511 },
	{ "adv7513", ADV7511 },
	{ "adv7533", ADV7533 },
	{ "adv7535", ADV7535 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv7511_i2c_ids);

static const struct of_device_id adv7511_of_ids[] = {
	{ .compatible = "adi,adv7511", .data = (void *)ADV7511 },
	{ .compatible = "adi,adv7511w", .data = (void *)ADV7511 },
	{ .compatible = "adi,adv7513", .data = (void *)ADV7511 },
	{ .compatible = "adi,adv7533", .data = (void *)ADV7533 },
	{ .compatible = "adi,adv7535", .data = (void *)ADV7535 },
	{ }
};
MODULE_DEVICE_TABLE(of, adv7511_of_ids);

static struct mipi_dsi_driver adv7533_dsi_driver = {
	.driver.name = "adv7533",
};

static struct i2c_driver adv7511_driver = {
	.driver = {
		.name = "adv7511",
		.of_match_table = adv7511_of_ids,
	},
	.id_table = adv7511_i2c_ids,
	.probe = adv7511_probe,
	.remove = adv7511_remove,
};

static int __init adv7511_init(void)
{
	int ret;

	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI)) {
		ret = mipi_dsi_driver_register(&adv7533_dsi_driver);
		if (ret)
			return ret;
	}

	ret = i2c_add_driver(&adv7511_driver);
	if (ret) {
		if (IS_ENABLED(CONFIG_DRM_MIPI_DSI))
			mipi_dsi_driver_unregister(&adv7533_dsi_driver);
	}

	return ret;
}
module_init(adv7511_init);

static void __exit adv7511_exit(void)
{
	i2c_del_driver(&adv7511_driver);

	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI))
		mipi_dsi_driver_unregister(&adv7533_dsi_driver);
}
module_exit(adv7511_exit);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ADV7511 HDMI transmitter driver");
MODULE_LICENSE("GPL");
