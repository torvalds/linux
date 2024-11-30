// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019-2020. Linaro Limited.
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <sound/hdmi-codec.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>

#define EDID_SEG_SIZE	256
#define EDID_LEN	32
#define EDID_LOOP	8
#define KEY_DDC_ACCS_DONE 0x02
#define DDC_NO_ACK	0x50

#define LT9611_4LANES	0

struct lt9611 {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;

	struct regmap *regmap;

	struct device_node *dsi0_node;
	struct device_node *dsi1_node;
	struct mipi_dsi_device *dsi0;
	struct mipi_dsi_device *dsi1;
	struct platform_device *audio_pdev;

	bool ac_mode;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;

	bool power_on;
	bool sleep;

	struct regulator_bulk_data supplies[2];

	struct i2c_client *client;

	enum drm_connector_status status;

	u8 edid_buf[EDID_SEG_SIZE];
};

#define LT9611_PAGE_CONTROL	0xff

static const struct regmap_range_cfg lt9611_ranges[] = {
	{
		.name = "register_range",
		.range_min =  0,
		.range_max = 0x85ff,
		.selector_reg = LT9611_PAGE_CONTROL,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 0x100,
	},
};

static const struct regmap_config lt9611_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xffff,
	.ranges = lt9611_ranges,
	.num_ranges = ARRAY_SIZE(lt9611_ranges),
};

static struct lt9611 *bridge_to_lt9611(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9611, bridge);
}

static int lt9611_mipi_input_analog(struct lt9611 *lt9611)
{
	const struct reg_sequence reg_cfg[] = {
		{ 0x8106, 0x40 }, /* port A rx current */
		{ 0x810a, 0xfe }, /* port A ldo voltage set */
		{ 0x810b, 0xbf }, /* enable port A lprx */
		{ 0x8111, 0x40 }, /* port B rx current */
		{ 0x8115, 0xfe }, /* port B ldo voltage set */
		{ 0x8116, 0xbf }, /* enable port B lprx */

		{ 0x811c, 0x03 }, /* PortA clk lane no-LP mode */
		{ 0x8120, 0x03 }, /* PortB clk lane with-LP mode */
	};

	return regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));
}

static int lt9611_mipi_input_digital(struct lt9611 *lt9611,
				     const struct drm_display_mode *mode)
{
	struct reg_sequence reg_cfg[] = {
		{ 0x8300, LT9611_4LANES },
		{ 0x830a, 0x00 },
		{ 0x824f, 0x80 },
		{ 0x8250, 0x10 },
		{ 0x8302, 0x0a },
		{ 0x8306, 0x0a },
	};

	if (lt9611->dsi1_node)
		reg_cfg[1].def = 0x03;

	return regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));
}

static void lt9611_mipi_video_setup(struct lt9611 *lt9611,
				    const struct drm_display_mode *mode)
{
	u32 h_total, hactive, hsync_len, hfront_porch, hsync_porch;
	u32 v_total, vactive, vsync_len, vfront_porch, vsync_porch;

	h_total = mode->htotal;
	v_total = mode->vtotal;

	hactive = mode->hdisplay;
	hsync_len = mode->hsync_end - mode->hsync_start;
	hfront_porch = mode->hsync_start - mode->hdisplay;
	hsync_porch = mode->htotal - mode->hsync_start;

	vactive = mode->vdisplay;
	vsync_len = mode->vsync_end - mode->vsync_start;
	vfront_porch = mode->vsync_start - mode->vdisplay;
	vsync_porch = mode->vtotal - mode->vsync_start;

	regmap_write(lt9611->regmap, 0x830d, (u8)(v_total / 256));
	regmap_write(lt9611->regmap, 0x830e, (u8)(v_total % 256));

	regmap_write(lt9611->regmap, 0x830f, (u8)(vactive / 256));
	regmap_write(lt9611->regmap, 0x8310, (u8)(vactive % 256));

	regmap_write(lt9611->regmap, 0x8311, (u8)(h_total / 256));
	regmap_write(lt9611->regmap, 0x8312, (u8)(h_total % 256));

	regmap_write(lt9611->regmap, 0x8313, (u8)(hactive / 256));
	regmap_write(lt9611->regmap, 0x8314, (u8)(hactive % 256));

	regmap_write(lt9611->regmap, 0x8315, (u8)(vsync_len % 256));
	regmap_write(lt9611->regmap, 0x8316, (u8)(hsync_len % 256));

	regmap_write(lt9611->regmap, 0x8317, (u8)(vfront_porch % 256));

	regmap_write(lt9611->regmap, 0x8318, (u8)(vsync_porch % 256));

	regmap_write(lt9611->regmap, 0x8319, (u8)(hfront_porch % 256));

	regmap_write(lt9611->regmap, 0x831a, (u8)(hsync_porch / 256) |
						((hfront_porch / 256) << 4));
	regmap_write(lt9611->regmap, 0x831b, (u8)(hsync_porch % 256));
}

static void lt9611_pcr_setup(struct lt9611 *lt9611, const struct drm_display_mode *mode, unsigned int postdiv)
{
	unsigned int pcr_m = mode->clock * 5 * postdiv / 27000;
	const struct reg_sequence reg_cfg[] = {
		{ 0x830b, 0x01 },
		{ 0x830c, 0x10 },
		{ 0x8348, 0x00 },
		{ 0x8349, 0x81 },

		/* stage 1 */
		{ 0x8321, 0x4a },
		{ 0x8324, 0x71 },
		{ 0x8325, 0x30 },
		{ 0x832a, 0x01 },

		/* stage 2 */
		{ 0x834a, 0x40 },

		/* MK limit */
		{ 0x832d, 0x38 },
		{ 0x8331, 0x08 },
	};
	u8 pol = 0x10;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		pol |= 0x2;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		pol |= 0x1;
	regmap_write(lt9611->regmap, 0x831d, pol);

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));
	if (lt9611->dsi1_node) {
		unsigned int hact = mode->hdisplay;

		hact >>= 2;
		hact += 0x50;
		hact = min(hact, 0x3e0U);
		regmap_write(lt9611->regmap, 0x830b, hact / 256);
		regmap_write(lt9611->regmap, 0x830c, hact % 256);
		regmap_write(lt9611->regmap, 0x8348, hact / 256);
		regmap_write(lt9611->regmap, 0x8349, hact % 256);
	}

	regmap_write(lt9611->regmap, 0x8326, pcr_m);

	/* pcr rst */
	regmap_write(lt9611->regmap, 0x8011, 0x5a);
	regmap_write(lt9611->regmap, 0x8011, 0xfa);
}

static int lt9611_pll_setup(struct lt9611 *lt9611, const struct drm_display_mode *mode, unsigned int *postdiv)
{
	unsigned int pclk = mode->clock;
	const struct reg_sequence reg_cfg[] = {
		/* txpll init */
		{ 0x8123, 0x40 },
		{ 0x8124, 0x64 },
		{ 0x8125, 0x80 },
		{ 0x8126, 0x55 },
		{ 0x812c, 0x37 },
		{ 0x812f, 0x01 },
		{ 0x8126, 0x55 },
		{ 0x8127, 0x66 },
		{ 0x8128, 0x88 },
		{ 0x812a, 0x20 },
	};

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));

	if (pclk > 150000) {
		regmap_write(lt9611->regmap, 0x812d, 0x88);
		*postdiv = 1;
	} else if (pclk > 70000) {
		regmap_write(lt9611->regmap, 0x812d, 0x99);
		*postdiv = 2;
	} else {
		regmap_write(lt9611->regmap, 0x812d, 0xaa);
		*postdiv = 4;
	}

	/*
	 * first divide pclk by 2 first
	 *  - write divide by 64k to 19:16 bits which means shift by 17
	 *  - write divide by 256 to 15:8 bits which means shift by 9
	 *  - write remainder to 7:0 bits, which means shift by 1
	 */
	regmap_write(lt9611->regmap, 0x82e3, pclk >> 17); /* pclk[19:16] */
	regmap_write(lt9611->regmap, 0x82e4, pclk >> 9);  /* pclk[15:8]  */
	regmap_write(lt9611->regmap, 0x82e5, pclk >> 1);  /* pclk[7:0]   */

	regmap_write(lt9611->regmap, 0x82de, 0x20);
	regmap_write(lt9611->regmap, 0x82de, 0xe0);

	regmap_write(lt9611->regmap, 0x8016, 0xf1);
	regmap_write(lt9611->regmap, 0x8016, 0xf3);

	return 0;
}

static int lt9611_read_video_check(struct lt9611 *lt9611, unsigned int reg)
{
	unsigned int temp, temp2;
	int ret;

	ret = regmap_read(lt9611->regmap, reg, &temp);
	if (ret)
		return ret;
	temp <<= 8;
	ret = regmap_read(lt9611->regmap, reg + 1, &temp2);
	if (ret)
		return ret;

	return (temp + temp2);
}

static int lt9611_video_check(struct lt9611 *lt9611)
{
	u32 v_total, vactive, hactive_a, hactive_b, h_total_sysclk;
	int temp;

	/* top module video check */

	/* vactive */
	temp = lt9611_read_video_check(lt9611, 0x8282);
	if (temp < 0)
		goto end;
	vactive = temp;

	/* v_total */
	temp = lt9611_read_video_check(lt9611, 0x826c);
	if (temp < 0)
		goto end;
	v_total = temp;

	/* h_total_sysclk */
	temp = lt9611_read_video_check(lt9611, 0x8286);
	if (temp < 0)
		goto end;
	h_total_sysclk = temp;

	/* hactive_a */
	temp = lt9611_read_video_check(lt9611, 0x8382);
	if (temp < 0)
		goto end;
	hactive_a = temp / 3;

	/* hactive_b */
	temp = lt9611_read_video_check(lt9611, 0x8386);
	if (temp < 0)
		goto end;
	hactive_b = temp / 3;

	dev_info(lt9611->dev,
		 "video check: hactive_a=%d, hactive_b=%d, vactive=%d, v_total=%d, h_total_sysclk=%d\n",
		 hactive_a, hactive_b, vactive, v_total, h_total_sysclk);

	return 0;

end:
	dev_err(lt9611->dev, "read video check error\n");
	return temp;
}

static void lt9611_hdmi_tx_digital(struct lt9611 *lt9611, bool is_hdmi)
{
	if (is_hdmi)
		regmap_write(lt9611->regmap, 0x82d6, 0x8c);
	else
		regmap_write(lt9611->regmap, 0x82d6, 0x0c);
	regmap_write(lt9611->regmap, 0x82d7, 0x04);
}

static void lt9611_hdmi_tx_phy(struct lt9611 *lt9611)
{
	struct reg_sequence reg_cfg[] = {
		{ 0x8130, 0x6a },
		{ 0x8131, 0x44 }, /* HDMI DC mode */
		{ 0x8132, 0x4a },
		{ 0x8133, 0x0b },
		{ 0x8134, 0x00 },
		{ 0x8135, 0x00 },
		{ 0x8136, 0x00 },
		{ 0x8137, 0x44 },
		{ 0x813f, 0x0f },
		{ 0x8140, 0xa0 },
		{ 0x8141, 0xa0 },
		{ 0x8142, 0xa0 },
		{ 0x8143, 0xa0 },
		{ 0x8144, 0x0a },
	};

	/* HDMI AC mode */
	if (lt9611->ac_mode)
		reg_cfg[2].def = 0x73;

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));
}

static irqreturn_t lt9611_irq_thread_handler(int irq, void *dev_id)
{
	struct lt9611 *lt9611 = dev_id;
	unsigned int irq_flag0 = 0;
	unsigned int irq_flag3 = 0;

	regmap_read(lt9611->regmap, 0x820f, &irq_flag3);
	regmap_read(lt9611->regmap, 0x820c, &irq_flag0);

	/* hpd changed low */
	if (irq_flag3 & 0x80) {
		dev_info(lt9611->dev, "hdmi cable disconnected\n");

		regmap_write(lt9611->regmap, 0x8207, 0xbf);
		regmap_write(lt9611->regmap, 0x8207, 0x3f);
	}

	/* hpd changed high */
	if (irq_flag3 & 0x40) {
		dev_info(lt9611->dev, "hdmi cable connected\n");

		regmap_write(lt9611->regmap, 0x8207, 0x7f);
		regmap_write(lt9611->regmap, 0x8207, 0x3f);
	}

	if (irq_flag3 & 0xc0 && lt9611->bridge.dev)
		drm_kms_helper_hotplug_event(lt9611->bridge.dev);

	/* video input changed */
	if (irq_flag0 & 0x01) {
		dev_info(lt9611->dev, "video input changed\n");
		regmap_write(lt9611->regmap, 0x829e, 0xff);
		regmap_write(lt9611->regmap, 0x829e, 0xf7);
		regmap_write(lt9611->regmap, 0x8204, 0xff);
		regmap_write(lt9611->regmap, 0x8204, 0xfe);
	}

	return IRQ_HANDLED;
}

static void lt9611_enable_hpd_interrupts(struct lt9611 *lt9611)
{
	unsigned int val;

	regmap_read(lt9611->regmap, 0x8203, &val);

	val &= ~0xc0;
	regmap_write(lt9611->regmap, 0x8203, val);
	regmap_write(lt9611->regmap, 0x8207, 0xff); /* clear */
	regmap_write(lt9611->regmap, 0x8207, 0x3f);
}

static void lt9611_sleep_setup(struct lt9611 *lt9611)
{
	const struct reg_sequence sleep_setup[] = {
		{ 0x8024, 0x76 },
		{ 0x8023, 0x01 },
		{ 0x8157, 0x03 }, /* set addr pin as output */
		{ 0x8149, 0x0b },

		{ 0x8102, 0x48 }, /* MIPI Rx power down */
		{ 0x8123, 0x80 },
		{ 0x8130, 0x00 },
		{ 0x8011, 0x0a },
	};

	regmap_multi_reg_write(lt9611->regmap,
			       sleep_setup, ARRAY_SIZE(sleep_setup));
	lt9611->sleep = true;
}

static int lt9611_power_on(struct lt9611 *lt9611)
{
	int ret;
	const struct reg_sequence seq[] = {
		/* LT9611_System_Init */
		{ 0x8101, 0x18 }, /* sel xtal clock */

		/* timer for frequency meter */
		{ 0x821b, 0x69 }, /* timer 2 */
		{ 0x821c, 0x78 },
		{ 0x82cb, 0x69 }, /* timer 1 */
		{ 0x82cc, 0x78 },

		/* irq init */
		{ 0x8251, 0x01 },
		{ 0x8258, 0x0a }, /* hpd irq */
		{ 0x8259, 0x80 }, /* hpd debounce width */
		{ 0x829e, 0xf7 }, /* video check irq */

		/* power consumption for work */
		{ 0x8004, 0xf0 },
		{ 0x8006, 0xf0 },
		{ 0x800a, 0x80 },
		{ 0x800b, 0x40 },
		{ 0x800d, 0xef },
		{ 0x8011, 0xfa },
	};

	if (lt9611->power_on)
		return 0;

	ret = regmap_multi_reg_write(lt9611->regmap, seq, ARRAY_SIZE(seq));
	if (!ret)
		lt9611->power_on = true;

	return ret;
}

static int lt9611_power_off(struct lt9611 *lt9611)
{
	int ret;

	ret = regmap_write(lt9611->regmap, 0x8130, 0x6a);
	if (!ret)
		lt9611->power_on = false;

	return ret;
}

static void lt9611_reset(struct lt9611 *lt9611)
{
	gpiod_set_value_cansleep(lt9611->reset_gpio, 1);
	msleep(20);

	gpiod_set_value_cansleep(lt9611->reset_gpio, 0);
	msleep(20);

	gpiod_set_value_cansleep(lt9611->reset_gpio, 1);
	msleep(100);
}

static void lt9611_assert_5v(struct lt9611 *lt9611)
{
	if (!lt9611->enable_gpio)
		return;

	gpiod_set_value_cansleep(lt9611->enable_gpio, 1);
	msleep(20);
}

static int lt9611_regulator_init(struct lt9611 *lt9611)
{
	int ret;

	lt9611->supplies[0].supply = "vdd";
	lt9611->supplies[1].supply = "vcc";

	ret = devm_regulator_bulk_get(lt9611->dev, 2, lt9611->supplies);
	if (ret < 0)
		return ret;

	return regulator_set_load(lt9611->supplies[0].consumer, 300000);
}

static int lt9611_regulator_enable(struct lt9611 *lt9611)
{
	int ret;

	ret = regulator_enable(lt9611->supplies[0].consumer);
	if (ret < 0)
		return ret;

	usleep_range(1000, 10000);

	ret = regulator_enable(lt9611->supplies[1].consumer);
	if (ret < 0) {
		regulator_disable(lt9611->supplies[0].consumer);
		return ret;
	}

	return 0;
}

static enum drm_connector_status lt9611_bridge_detect(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	unsigned int reg_val = 0;
	int connected = 0;

	regmap_read(lt9611->regmap, 0x825e, &reg_val);
	connected  = (reg_val & (BIT(2) | BIT(0)));

	lt9611->status = connected ?  connector_status_connected :
				connector_status_disconnected;

	return lt9611->status;
}

static int lt9611_read_edid(struct lt9611 *lt9611)
{
	unsigned int temp;
	int ret = 0;
	int i, j;

	/* memset to clear old buffer, if any */
	memset(lt9611->edid_buf, 0, sizeof(lt9611->edid_buf));

	regmap_write(lt9611->regmap, 0x8503, 0xc9);

	/* 0xA0 is EDID device address */
	regmap_write(lt9611->regmap, 0x8504, 0xa0);
	/* 0x00 is EDID offset address */
	regmap_write(lt9611->regmap, 0x8505, 0x00);

	/* length for read */
	regmap_write(lt9611->regmap, 0x8506, EDID_LEN);
	regmap_write(lt9611->regmap, 0x8514, 0x7f);

	for (i = 0; i < EDID_LOOP; i++) {
		/* offset address */
		regmap_write(lt9611->regmap, 0x8505, i * EDID_LEN);
		regmap_write(lt9611->regmap, 0x8507, 0x36);
		regmap_write(lt9611->regmap, 0x8507, 0x31);
		regmap_write(lt9611->regmap, 0x8507, 0x37);
		usleep_range(5000, 10000);

		regmap_read(lt9611->regmap, 0x8540, &temp);

		if (temp & KEY_DDC_ACCS_DONE) {
			for (j = 0; j < EDID_LEN; j++) {
				regmap_read(lt9611->regmap, 0x8583, &temp);
				lt9611->edid_buf[i * EDID_LEN + j] = temp;
			}

		} else if (temp & DDC_NO_ACK) { /* DDC No Ack or Abitration lost */
			dev_err(lt9611->dev, "read edid failed: no ack\n");
			ret = -EIO;
			goto end;

		} else {
			dev_err(lt9611->dev, "read edid failed: access not done\n");
			ret = -EIO;
			goto end;
		}
	}

end:
	regmap_write(lt9611->regmap, 0x8507, 0x1f);
	return ret;
}

static int
lt9611_get_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct lt9611 *lt9611 = data;
	int ret;

	if (len > 128)
		return -EINVAL;

	/* supports up to 1 extension block */
	/* TODO: add support for more extension blocks */
	if (block > 1)
		return -EINVAL;

	if (block == 0) {
		ret = lt9611_read_edid(lt9611);
		if (ret) {
			dev_err(lt9611->dev, "edid read failed\n");
			return ret;
		}
	}

	block %= 2;
	memcpy(buf, lt9611->edid_buf + (block * 128), len);

	return 0;
}

/* bridge funcs */
static void
lt9611_bridge_atomic_enable(struct drm_bridge *bridge,
			    struct drm_bridge_state *old_bridge_state)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	struct drm_atomic_state *state = old_bridge_state->base.state;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	struct drm_display_mode *mode;
	unsigned int postdiv;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	if (WARN_ON(!connector))
		return;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!conn_state))
		return;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (WARN_ON(!crtc_state))
		return;

	mode = &crtc_state->adjusted_mode;

	lt9611_mipi_input_digital(lt9611, mode);
	lt9611_pll_setup(lt9611, mode, &postdiv);
	lt9611_mipi_video_setup(lt9611, mode);
	lt9611_pcr_setup(lt9611, mode, postdiv);

	if (lt9611_power_on(lt9611)) {
		dev_err(lt9611->dev, "power on failed\n");
		return;
	}

	lt9611_mipi_input_analog(lt9611);
	drm_atomic_helper_connector_hdmi_update_infoframes(connector, state);
	lt9611_hdmi_tx_digital(lt9611, connector->display_info.is_hdmi);
	lt9611_hdmi_tx_phy(lt9611);

	msleep(500);

	lt9611_video_check(lt9611);

	/* Enable HDMI output */
	regmap_write(lt9611->regmap, 0x8130, 0xea);
}

static void
lt9611_bridge_atomic_disable(struct drm_bridge *bridge,
			     struct drm_bridge_state *old_bridge_state)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	int ret;

	/* Disable HDMI output */
	ret = regmap_write(lt9611->regmap, 0x8130, 0x6a);
	if (ret) {
		dev_err(lt9611->dev, "video on failed\n");
		return;
	}

	if (lt9611_power_off(lt9611)) {
		dev_err(lt9611->dev, "power on failed\n");
		return;
	}
}

static struct mipi_dsi_device *lt9611_attach_dsi(struct lt9611 *lt9611,
						 struct device_node *dsi_node)
{
	const struct mipi_dsi_device_info info = { "lt9611", 0, lt9611->dev->of_node};
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	struct device *dev = lt9611->dev;
	int ret;

	host = of_find_mipi_dsi_host_by_node(dsi_node);
	if (!host)
		return ERR_PTR(dev_err_probe(lt9611->dev, -EPROBE_DEFER, "failed to find dsi host\n"));

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi)) {
		dev_err(lt9611->dev, "failed to create dsi device\n");
		return dsi;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_VIDEO_HSE;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host\n");
		return ERR_PTR(ret);
	}

	return dsi;
}

static int lt9611_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	return drm_bridge_attach(bridge->encoder, lt9611->next_bridge,
				 bridge, flags);
}

static enum drm_mode_status lt9611_bridge_mode_valid(struct drm_bridge *bridge,
						     const struct drm_display_info *info,
						     const struct drm_display_mode *mode)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	if (mode->hdisplay > 3840)
		return MODE_BAD_HVALUE;

	if (mode->hdisplay > 2000 && !lt9611->dsi1_node)
		return MODE_PANEL;

	return MODE_OK;
}

static int lt9611_bridge_atomic_check(struct drm_bridge *bridge,
				      struct drm_bridge_state *bridge_state,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	return drm_atomic_helper_connector_hdmi_check(conn_state->connector,
						      conn_state->state);
}

static void lt9611_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					    struct drm_bridge_state *old_bridge_state)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	static const struct reg_sequence reg_cfg[] = {
		{ 0x8102, 0x12 },
		{ 0x8123, 0x40 },
		{ 0x8130, 0xea },
		{ 0x8011, 0xfa },
	};

	if (!lt9611->sleep)
		return;

	regmap_multi_reg_write(lt9611->regmap,
			       reg_cfg, ARRAY_SIZE(reg_cfg));

	lt9611->sleep = false;
}

static void
lt9611_bridge_atomic_post_disable(struct drm_bridge *bridge,
				  struct drm_bridge_state *old_bridge_state)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	lt9611_sleep_setup(lt9611);
}

static const struct drm_edid *lt9611_bridge_edid_read(struct drm_bridge *bridge,
						      struct drm_connector *connector)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	lt9611_power_on(lt9611);
	return drm_edid_read_custom(connector, lt9611_get_edid_block, lt9611);
}

static void lt9611_bridge_hpd_enable(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	lt9611_enable_hpd_interrupts(lt9611);
}

#define MAX_INPUT_SEL_FORMATS	1

static u32 *
lt9611_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
				 struct drm_bridge_state *bridge_state,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state,
				 u32 output_fmt,
				 unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(MAX_INPUT_SEL_FORMATS, sizeof(*input_fmts),
			     GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	/* This is the DSI-end bus format */
	input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
	*num_input_fmts = 1;

	return input_fmts;
}

/*
 * Other working frames:
 * - 0x01, 0x84df
 * - 0x04, 0x84c0
 */
#define LT9611_INFOFRAME_AUDIO	0x02
#define LT9611_INFOFRAME_AVI	0x08
#define LT9611_INFOFRAME_SPD	0x10
#define LT9611_INFOFRAME_VENDOR	0x20

static int lt9611_hdmi_clear_infoframe(struct drm_bridge *bridge,
				       enum hdmi_infoframe_type type)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	unsigned int mask;

	switch (type) {
	case HDMI_INFOFRAME_TYPE_AVI:
		mask = LT9611_INFOFRAME_AVI;
		break;

	case HDMI_INFOFRAME_TYPE_SPD:
		mask = LT9611_INFOFRAME_SPD;
		break;

	case HDMI_INFOFRAME_TYPE_VENDOR:
		mask = LT9611_INFOFRAME_VENDOR;
		break;

	default:
		drm_dbg_driver(lt9611->bridge.dev, "Unsupported HDMI InfoFrame %x\n", type);
		mask = 0;
		break;
	}

	if (mask)
		regmap_update_bits(lt9611->regmap, 0x843d, mask, 0);

	return 0;
}

static int lt9611_hdmi_write_infoframe(struct drm_bridge *bridge,
				       enum hdmi_infoframe_type type,
				       const u8 *buffer, size_t len)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	unsigned int mask, addr;
	int i;

	switch (type) {
	case HDMI_INFOFRAME_TYPE_AVI:
		mask = LT9611_INFOFRAME_AVI;
		addr = 0x8440;
		break;

	case HDMI_INFOFRAME_TYPE_SPD:
		mask = LT9611_INFOFRAME_SPD;
		addr = 0x8493;
		break;

	case HDMI_INFOFRAME_TYPE_VENDOR:
		mask = LT9611_INFOFRAME_VENDOR;
		addr = 0x8474;
		break;

	default:
		drm_dbg_driver(lt9611->bridge.dev, "Unsupported HDMI InfoFrame %x\n", type);
		mask = 0;
		break;
	}

	if (mask) {
		for (i = 0; i < len; i++)
			regmap_write(lt9611->regmap, addr + i, buffer[i]);

		regmap_update_bits(lt9611->regmap, 0x843d, mask, mask);
	}

	return 0;
}

static enum drm_mode_status
lt9611_hdmi_tmds_char_rate_valid(const struct drm_bridge *bridge,
				 const struct drm_display_mode *mode,
				 unsigned long long tmds_rate)
{
	/* 297 MHz for 4k@30 mode */
	if (tmds_rate > 297000000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_bridge_funcs lt9611_bridge_funcs = {
	.attach = lt9611_bridge_attach,
	.mode_valid = lt9611_bridge_mode_valid,
	.detect = lt9611_bridge_detect,
	.edid_read = lt9611_bridge_edid_read,
	.hpd_enable = lt9611_bridge_hpd_enable,

	.atomic_check = lt9611_bridge_atomic_check,
	.atomic_pre_enable = lt9611_bridge_atomic_pre_enable,
	.atomic_enable = lt9611_bridge_atomic_enable,
	.atomic_disable = lt9611_bridge_atomic_disable,
	.atomic_post_disable = lt9611_bridge_atomic_post_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_get_input_bus_fmts = lt9611_atomic_get_input_bus_fmts,

	.hdmi_tmds_char_rate_valid = lt9611_hdmi_tmds_char_rate_valid,
	.hdmi_write_infoframe = lt9611_hdmi_write_infoframe,
	.hdmi_clear_infoframe = lt9611_hdmi_clear_infoframe,
};

static int lt9611_parse_dt(struct device *dev,
			   struct lt9611 *lt9611)
{
	lt9611->dsi0_node = of_graph_get_remote_node(dev->of_node, 0, -1);
	if (!lt9611->dsi0_node) {
		dev_err(lt9611->dev, "failed to get remote node for primary dsi\n");
		return -ENODEV;
	}

	lt9611->dsi1_node = of_graph_get_remote_node(dev->of_node, 1, -1);

	lt9611->ac_mode = of_property_read_bool(dev->of_node, "lt,ac-mode");

	return drm_of_find_panel_or_bridge(dev->of_node, 2, -1, NULL, &lt9611->next_bridge);
}

static int lt9611_gpio_init(struct lt9611 *lt9611)
{
	struct device *dev = lt9611->dev;

	lt9611->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lt9611->reset_gpio)) {
		dev_err(dev, "failed to acquire reset gpio\n");
		return PTR_ERR(lt9611->reset_gpio);
	}

	lt9611->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						      GPIOD_OUT_LOW);
	if (IS_ERR(lt9611->enable_gpio)) {
		dev_err(dev, "failed to acquire enable gpio\n");
		return PTR_ERR(lt9611->enable_gpio);
	}

	return 0;
}

static int lt9611_read_device_rev(struct lt9611 *lt9611)
{
	unsigned int rev;
	int ret;

	regmap_write(lt9611->regmap, 0x80ee, 0x01);
	ret = regmap_read(lt9611->regmap, 0x8002, &rev);
	if (ret)
		dev_err(lt9611->dev, "failed to read revision: %d\n", ret);
	else
		dev_info(lt9611->dev, "LT9611 revision: 0x%x\n", rev);

	return ret;
}

static int lt9611_hdmi_hw_params(struct device *dev, void *data,
				 struct hdmi_codec_daifmt *fmt,
				 struct hdmi_codec_params *hparms)
{
	struct lt9611 *lt9611 = data;

	if (hparms->sample_rate == 48000)
		regmap_write(lt9611->regmap, 0x840f, 0x2b);
	else if (hparms->sample_rate == 96000)
		regmap_write(lt9611->regmap, 0x840f, 0xab);
	else
		return -EINVAL;

	regmap_write(lt9611->regmap, 0x8435, 0x00);
	regmap_write(lt9611->regmap, 0x8436, 0x18);
	regmap_write(lt9611->regmap, 0x8437, 0x00);

	return 0;
}

static int lt9611_audio_startup(struct device *dev, void *data)
{
	struct lt9611 *lt9611 = data;

	regmap_write(lt9611->regmap, 0x82d6, 0x8c);
	regmap_write(lt9611->regmap, 0x82d7, 0x04);

	regmap_write(lt9611->regmap, 0x8406, 0x08);
	regmap_write(lt9611->regmap, 0x8407, 0x10);

	regmap_write(lt9611->regmap, 0x8434, 0xd5);

	return 0;
}

static void lt9611_audio_shutdown(struct device *dev, void *data)
{
	struct lt9611 *lt9611 = data;

	regmap_write(lt9611->regmap, 0x8406, 0x00);
	regmap_write(lt9611->regmap, 0x8407, 0x00);
}

static int lt9611_hdmi_i2s_get_dai_id(struct snd_soc_component *component,
				      struct device_node *endpoint)
{
	struct of_endpoint of_ep;
	int ret;

	ret = of_graph_parse_endpoint(endpoint, &of_ep);
	if (ret < 0)
		return ret;

	/*
	 * HDMI sound should be located as reg = <2>
	 * Then, it is sound port 0
	 */
	if (of_ep.port == 2)
		return 0;

	return -EINVAL;
}

static const struct hdmi_codec_ops lt9611_codec_ops = {
	.hw_params	= lt9611_hdmi_hw_params,
	.audio_shutdown = lt9611_audio_shutdown,
	.audio_startup	= lt9611_audio_startup,
	.get_dai_id	= lt9611_hdmi_i2s_get_dai_id,
};

static struct hdmi_codec_pdata codec_data = {
	.ops = &lt9611_codec_ops,
	.max_i2s_channels = 8,
	.i2s = 1,
};

static int lt9611_audio_init(struct device *dev, struct lt9611 *lt9611)
{
	codec_data.data = lt9611;
	lt9611->audio_pdev =
		platform_device_register_data(dev, HDMI_CODEC_DRV_NAME,
					      PLATFORM_DEVID_AUTO,
					      &codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(lt9611->audio_pdev);
}

static void lt9611_audio_exit(struct lt9611 *lt9611)
{
	if (lt9611->audio_pdev) {
		platform_device_unregister(lt9611->audio_pdev);
		lt9611->audio_pdev = NULL;
	}
}

static int lt9611_probe(struct i2c_client *client)
{
	struct lt9611 *lt9611;
	struct device *dev = &client->dev;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "device doesn't support I2C\n");
		return -ENODEV;
	}

	lt9611 = devm_kzalloc(dev, sizeof(*lt9611), GFP_KERNEL);
	if (!lt9611)
		return -ENOMEM;

	lt9611->dev = dev;
	lt9611->client = client;
	lt9611->sleep = false;

	lt9611->regmap = devm_regmap_init_i2c(client, &lt9611_regmap_config);
	if (IS_ERR(lt9611->regmap)) {
		dev_err(lt9611->dev, "regmap i2c init failed\n");
		return PTR_ERR(lt9611->regmap);
	}

	ret = lt9611_parse_dt(dev, lt9611);
	if (ret) {
		dev_err(dev, "failed to parse device tree\n");
		return ret;
	}

	ret = lt9611_gpio_init(lt9611);
	if (ret < 0)
		goto err_of_put;

	ret = lt9611_regulator_init(lt9611);
	if (ret < 0)
		goto err_of_put;

	lt9611_assert_5v(lt9611);

	ret = lt9611_regulator_enable(lt9611);
	if (ret)
		goto err_of_put;

	lt9611_reset(lt9611);

	ret = lt9611_read_device_rev(lt9611);
	if (ret) {
		dev_err(dev, "failed to read chip rev\n");
		goto err_disable_regulators;
	}

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					lt9611_irq_thread_handler,
					IRQF_ONESHOT, "lt9611", lt9611);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		goto err_disable_regulators;
	}

	i2c_set_clientdata(client, lt9611);

	lt9611->bridge.funcs = &lt9611_bridge_funcs;
	lt9611->bridge.of_node = client->dev.of_node;
	lt9611->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID |
			     DRM_BRIDGE_OP_HPD | DRM_BRIDGE_OP_MODES |
			     DRM_BRIDGE_OP_HDMI;
	lt9611->bridge.type = DRM_MODE_CONNECTOR_HDMIA;
	lt9611->bridge.vendor = "Lontium";
	lt9611->bridge.product = "LT9611";

	drm_bridge_add(&lt9611->bridge);

	/* Attach primary DSI */
	lt9611->dsi0 = lt9611_attach_dsi(lt9611, lt9611->dsi0_node);
	if (IS_ERR(lt9611->dsi0)) {
		ret = PTR_ERR(lt9611->dsi0);
		goto err_remove_bridge;
	}

	/* Attach secondary DSI, if specified */
	if (lt9611->dsi1_node) {
		lt9611->dsi1 = lt9611_attach_dsi(lt9611, lt9611->dsi1_node);
		if (IS_ERR(lt9611->dsi1)) {
			ret = PTR_ERR(lt9611->dsi1);
			goto err_remove_bridge;
		}
	}

	lt9611_enable_hpd_interrupts(lt9611);

	ret = lt9611_audio_init(dev, lt9611);
	if (ret)
		goto err_remove_bridge;

	return 0;

err_remove_bridge:
	drm_bridge_remove(&lt9611->bridge);

err_disable_regulators:
	regulator_bulk_disable(ARRAY_SIZE(lt9611->supplies), lt9611->supplies);

err_of_put:
	of_node_put(lt9611->dsi1_node);
	of_node_put(lt9611->dsi0_node);

	return ret;
}

static void lt9611_remove(struct i2c_client *client)
{
	struct lt9611 *lt9611 = i2c_get_clientdata(client);

	disable_irq(client->irq);
	lt9611_audio_exit(lt9611);
	drm_bridge_remove(&lt9611->bridge);

	regulator_bulk_disable(ARRAY_SIZE(lt9611->supplies), lt9611->supplies);

	of_node_put(lt9611->dsi1_node);
	of_node_put(lt9611->dsi0_node);
}

static const struct i2c_device_id lt9611_id[] = {
	{ "lontium,lt9611", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, lt9611_id);

static const struct of_device_id lt9611_match_table[] = {
	{ .compatible = "lontium,lt9611" },
	{ }
};
MODULE_DEVICE_TABLE(of, lt9611_match_table);

static struct i2c_driver lt9611_driver = {
	.driver = {
		.name = "lt9611",
		.of_match_table = lt9611_match_table,
	},
	.probe = lt9611_probe,
	.remove = lt9611_remove,
	.id_table = lt9611_id,
};
module_i2c_driver(lt9611_driver);

MODULE_DESCRIPTION("Lontium LT9611 DSI/HDMI bridge driver");
MODULE_LICENSE("GPL v2");
