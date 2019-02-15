// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019. Linaro Ltd
 */

#define DEBUG

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/component.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <drm/drm_probe_helper.h>
#include <linux/hdmi.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_bridge.h>
#include <drm/drm_print.h>

#define EDID_SEG_SIZE 256

#define LT9611_4LANES	0

struct lt9611 {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_connector connector;

	struct regmap *regmap;

	struct device_node *dsi0_node;
	struct device_node *dsi1_node;
	struct mipi_dsi_device *dsi0;
	struct mipi_dsi_device *dsi1;

	bool ac_mode;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;

	bool power_on;

	struct regulator_bulk_data supplies[2];

	struct i2c_client *client;

	enum drm_connector_status status;

	u8 edid_buf[EDID_SEG_SIZE];
	u32 vic;
};

static const struct regmap_config lt9611_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
};

struct lt9611_mode {
	u16 hdisplay;
	u16 vdisplay;
	u8 fps;
	u8 lanes;
	u8 intfs;
};

static struct lt9611_mode lt9611_modes[] = {
	{ 3840, 2160, 30, 4, 2 }, /* 3840x2160 24bit 30Hz 4Lane 2ports */
	{ 1920, 1080, 60, 4, 1 }, /* 1080P 24bit 60Hz 4lane 1port */
	{ 1920, 1080, 30, 3, 1 }, /* 1080P 24bit 30Hz 3lane 1port */
	{ 1920, 1080, 24, 3, 1 },
	{ 720, 480, 60, 4, 1 },
	{ 720, 576, 50, 2, 1 },
	{ 640, 480, 60, 2, 1 },
};

static struct lt9611 *bridge_to_lt9611(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9611, bridge);
}

static struct lt9611 *connector_to_lt9611(struct drm_connector *connector)
{
	return container_of(connector, struct lt9611, connector);
}

static int lt9611_mipi_input_analog(struct lt9611 *lt9611)
{
	struct reg_sequence reg_cfg[] = {
		{ 0xff, 0x81 },
		{ 0x06, 0x40 }, /*port A rx current*/
		{ 0x0a, 0xfe }, /*port A ldo voltage set*/
		{ 0x0b, 0xbf }, /*enable port A lprx*/
		{ 0x11, 0x40 }, /*port B rx current*/
		{ 0x15, 0xfe }, /*port B ldo voltage set*/
		{ 0x16, 0xbf }, /*enable port B lprx*/

		{ 0x1c, 0x03 }, /*PortA clk lane no-LP mode*/
		{ 0x20, 0x03 }, /*PortB clk lane with-LP mode*/
	};

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));

	return 0;
}

static int lt9611_mipi_input_digital(struct lt9611 *lt9611,
				     const struct drm_display_mode *mode)
{
	struct reg_sequence reg_cfg[] = {
		{ 0xff, 0x82 },
		{ 0x4f, 0x80 },
		{ 0x50, 0x10 },
		{ 0xff, 0x83 },

		{ 0x02, 0x0a },
		{ 0x06, 0x0a },
	};

	regmap_write(lt9611->regmap, 0xff, 0x83);
	regmap_write(lt9611->regmap, 0x00, LT9611_4LANES);

	if (mode->hdisplay == 3840)
		regmap_write(lt9611->regmap, 0x0a, 0x03);
	else
		regmap_write(lt9611->regmap, 0x0a, 0x00);

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));

	return 0;
}

static void lt9611_mipi_video_setup(struct lt9611 *lt9611,
				    const struct drm_display_mode *mode)
{
	u32 h_total, h_act, hpw, hfp, hss;
	u32 v_total, v_act, vpw, vfp, vss;

	h_total = mode->htotal;
	v_total = mode->vtotal;

	h_act = mode->hdisplay;
	hpw = mode->hsync_end - mode->hsync_start;
	hfp = mode->hsync_start - mode->hdisplay;
	hss = (mode->hsync_end - mode->hsync_start) + (mode->htotal - mode->hsync_end);

	v_act = mode->vdisplay;
	vpw = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vss = (mode->vsync_end - mode->vsync_start) + (mode->vtotal - mode->vsync_end);

	regmap_write(lt9611->regmap, 0xff, 0x83);

	regmap_write(lt9611->regmap, 0x0d, (u8)(v_total / 256));
	regmap_write(lt9611->regmap, 0x0e, (u8)(v_total % 256));

	regmap_write(lt9611->regmap, 0x0f, (u8)(v_act / 256));
	regmap_write(lt9611->regmap, 0x10, (u8)(v_act % 256));

	regmap_write(lt9611->regmap, 0x11, (u8)(h_total / 256));
	regmap_write(lt9611->regmap, 0x12, (u8)(h_total % 256));

	regmap_write(lt9611->regmap, 0x13, (u8)(h_act / 256));
	regmap_write(lt9611->regmap, 0x14, (u8)(h_act % 256));

	regmap_write(lt9611->regmap, 0x15, (u8)(vpw % 256));
	regmap_write(lt9611->regmap, 0x16, (u8)(hpw % 256));

	regmap_write(lt9611->regmap, 0x17, (u8)(vfp % 256));

	regmap_write(lt9611->regmap, 0x18, (u8)(vss % 256));

	regmap_write(lt9611->regmap, 0x19, (u8)(hfp % 256));

	regmap_write(lt9611->regmap, 0x1a, (u8)(hss / 256));
	regmap_write(lt9611->regmap, 0x1b, (u8)(hss % 256));
}

static int lt9611_pcr_setup(struct lt9611 *lt9611,
			    const struct drm_display_mode *mode)
{
	struct reg_sequence reg_cfg[] = {
		{ 0xff, 0x83 },
		{ 0x0b, 0x01 },
		{ 0x0c, 0x10 },
		{ 0x48, 0x00 },
		{ 0x49, 0x81 },

		/* stage 1 */
		{ 0x21, 0x4a },
		{ 0x24, 0x71 },
		{ 0x25, 0x30 },
		{ 0x2a, 0x01 },

		/* stage 2 */
		{ 0x4a, 0x40 },
		{ 0x1d, 0x10 },

		/* MK limit */
		{ 0x2d, 0x38 },
		{ 0x31, 0x08 },
	};

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));

	switch (mode->hdisplay) {
	case 640:
		regmap_write(lt9611->regmap, 0x26, 0x14);
		break;
	case 1920:
		regmap_write(lt9611->regmap, 0x26, 0x37);
		break;
	case 3840:
		regmap_write(lt9611->regmap, 0x0b, 0x03);
		regmap_write(lt9611->regmap, 0x0c, 0xd0);
		regmap_write(lt9611->regmap, 0x48, 0x03);
		regmap_write(lt9611->regmap, 0x49, 0xe0);
		regmap_write(lt9611->regmap, 0x24, 0x72);
		regmap_write(lt9611->regmap, 0x25, 0x00);
		regmap_write(lt9611->regmap, 0x2a, 0x01);
		regmap_write(lt9611->regmap, 0x4a, 0x10);
		regmap_write(lt9611->regmap, 0x1d, 0x10);
		regmap_write(lt9611->regmap, 0x26, 0x37);
		break;
	}

	/* pcr rst */
	regmap_write(lt9611->regmap, 0xff, 0x80);
	regmap_write(lt9611->regmap, 0x11, 0x5a);
	regmap_write(lt9611->regmap, 0x11, 0xfa);

	return 0;
}

static int lt9611_pll_setup(struct lt9611 *lt9611,
			    const struct drm_display_mode *mode)
{
	unsigned int pclk = mode->clock;
	struct reg_sequence reg_cfg[] = {
		/* txpll init */
		{ 0xff, 0x81 },
		{ 0x23, 0x40 },
		{ 0x24, 0x64 },
		{ 0x25, 0x80 },
		{ 0x26, 0x55 },
		{ 0x2c, 0x37 },
		{ 0x2f, 0x01 },
		{ 0x26, 0x55 },
		{ 0x27, 0x66 },
		{ 0x28, 0x88 },
	};

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));

	if (pclk > 150000)
		regmap_write(lt9611->regmap, 0x2d, 0x88);
	else if (pclk > 70000)
		regmap_write(lt9611->regmap, 0x2d, 0x99);
	else
		regmap_write(lt9611->regmap, 0x2d, 0xaa);

	regmap_write(lt9611->regmap, 0xff, 0x82);
	regmap_write(lt9611->regmap, 0xe3, pclk >> 17); /* pclk[19:16] */
	regmap_write(lt9611->regmap, 0xe4, pclk >> 9);  /* pclk[15:8]  */
	regmap_write(lt9611->regmap, 0xe5, pclk >> 1);  /* pclk[7:0]   */

	regmap_write(lt9611->regmap, 0xde, 0x20);
	regmap_write(lt9611->regmap, 0xde, 0xe0);

	regmap_write(lt9611->regmap, 0xff, 0x80);
	regmap_write(lt9611->regmap, 0x16, 0xf1);
	regmap_write(lt9611->regmap, 0x16, 0xf3);

	return 0;
}

static int lt9611_video_check(struct lt9611 *lt9611)
{
	u32 v_total, v_act, h_act_a, h_act_b, h_total_sysclk;
	unsigned int temp;
	int ret;

	/* top module video check */
	regmap_write(lt9611->regmap, 0xff, 0x82);

	/* v_act */
	ret = regmap_read(lt9611->regmap, 0x82, &temp);
	if (ret)
		goto end;

	v_act = temp << 8;
	ret = regmap_read(lt9611->regmap, 0x83, &temp);
	if (ret)
		goto end;
	v_act = v_act + temp;

	/* v_total */
	ret = regmap_read(lt9611->regmap, 0x6c, &temp);
	if (ret)
		goto end;
	v_total = temp << 8;
	ret = regmap_read(lt9611->regmap, 0x6d, &temp);
	if (ret)
		goto end;
	v_total = v_total + temp;

	/* h_total_sysclk */
	ret = regmap_read(lt9611->regmap, 0x86, &temp);
	if (ret)
		goto end;
	h_total_sysclk = temp << 8;
	ret = regmap_read(lt9611->regmap, 0x87, &temp);
	if (ret)
		goto end;
	h_total_sysclk = h_total_sysclk + temp;

	/* h_act_a */
	regmap_write(lt9611->regmap, 0xff, 0x83);
	ret = regmap_read(lt9611->regmap, 0x82, &temp);
	if (ret)
		goto end;
	h_act_a = temp << 8;
	ret = regmap_read(lt9611->regmap, 0x83, &temp);
	if (ret)
		goto end;
	h_act_a = (h_act_a + temp)/3;

	/* h_act_b */
	regmap_write(lt9611->regmap, 0xff, 0x83);
	ret = regmap_read(lt9611->regmap, 0x86, &temp);
	if (ret)
		goto end;
	h_act_b = temp << 8;
	ret = regmap_read(lt9611->regmap, 0x87, &temp);
	if (ret)
		goto end;
	h_act_b = (h_act_b + temp)/3;

	dev_info(lt9611->dev, "video check: h_act_a=%d, h_act_b=%d, v_act=%d, v_total=%d, h_total_sysclk=%d\n",
		h_act_a, h_act_b, v_act, v_total, h_total_sysclk);

	return 0;

end:
	dev_err(lt9611->dev, "read video check error\n");
	return ret;
}

static void lt9611_hdmi_tx_digital(struct lt9611 *lt9611)
{
	regmap_write(lt9611->regmap, 0xff, 0x84);
	regmap_write(lt9611->regmap, 0x43, 0x46 - lt9611->vic);
	regmap_write(lt9611->regmap, 0x44, 0x84);
	regmap_write(lt9611->regmap, 0x47, lt9611->vic);

	regmap_write(lt9611->regmap, 0xff, 0x82);
	regmap_write(lt9611->regmap, 0xd6, 0x8c);
	regmap_write(lt9611->regmap, 0xd7, 0x04);
}

static void lt9611_hdmi_tx_phy(struct lt9611 *lt9611)
{
	struct reg_sequence reg_cfg[] = {
		{ 0xff, 0x81 },
		{ 0x30, 0x6a },
		{ 0x31, 0x44 }, /* HDMI DC mode */
		{ 0x32, 0x4a },
		{ 0x33, 0x0b },
		{ 0x34, 0x00 },
		{ 0x35, 0x00 },
		{ 0x36, 0x00 },
		{ 0x37, 0x44 },
		{ 0x3f, 0x0f },
		{ 0x40, 0xa0 },
		{ 0x41, 0xa0 },
		{ 0x42, 0xa0 },
		{ 0x43, 0xa0 },
		{ 0x44, 0x0a },
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

	regmap_write(lt9611->regmap, 0xff, 0x82);
	regmap_read(lt9611->regmap, 0x0f, &irq_flag3);
	regmap_read(lt9611->regmap, 0x0c, &irq_flag0);

	printk(KERN_ERR "%s() irq_flag0: %#x irq_flag3: %#x\n", __func__, irq_flag0, irq_flag3);

	 /* hpd changed low */
	if (irq_flag3 & 0x80) {
		dev_info(lt9611->dev, "hdmi cable disconnected\n");

		regmap_write(lt9611->regmap, 0xff, 0x82); /* irq 3 clear flag */
		regmap_write(lt9611->regmap, 0x07, 0xbf);
		regmap_write(lt9611->regmap, 0x07, 0x3f);
	}
	 /* hpd changed high */
	if (irq_flag3 & 0x40) {
		dev_info(lt9611->dev, "hdmi cable connected\n");

		regmap_write(lt9611->regmap, 0xff, 0x82); /* irq 3 clear flag */
		regmap_write(lt9611->regmap, 0x07, 0x7f);
		regmap_write(lt9611->regmap, 0x07, 0x3f);
	}

//	if (irq_flag3 & 0xc0)
//		drm_kms_helper_hotplug_event(lt9611->bridge.dev);

	/* video input changed */
	if (irq_flag0 & 0x01) {
		dev_info(lt9611->dev, "video input changed\n");
		regmap_write(lt9611->regmap, 0xff, 0x82); /* irq 0 clear flag */
		regmap_write(lt9611->regmap, 0x9e, 0xff);
		regmap_write(lt9611->regmap, 0x9e, 0xf7);
		regmap_write(lt9611->regmap, 0x04, 0xff);
		regmap_write(lt9611->regmap, 0x04, 0xfe);
	}

	return IRQ_HANDLED;
}

static void lt9611_enable_hpd_interrupts(struct lt9611 *lt9611)
{
	unsigned int val;

	dev_dbg(lt9611->dev, "enabling hpd interrupts\n");

	regmap_write(lt9611->regmap, 0xff, 0x82);
	regmap_read(lt9611->regmap, 0x03, &val);

	val &= ~0xc0;
	regmap_write(lt9611->regmap, 0x03, val);
	regmap_write(lt9611->regmap, 0x07, 0xff); //clear
	regmap_write(lt9611->regmap, 0x07, 0x3f);
}

static int lt9611_power_on(struct lt9611 *lt9611)
{
	int ret;
	const struct reg_sequence seq[] = {
		/* LT9611_System_Init */
		{ 0xFF, 0x81 },
		{ 0x01, 0x18 }, /* sel xtal clock */

		/* timer for frequency meter */
		{ 0xff, 0x82 },
		{ 0x1b, 0x69 }, /*timer 2*/
		{ 0x1c, 0x78 },
		{ 0xcb, 0x69 }, /*timer 1 */
		{ 0xcc, 0x78 },

		/* irq init */
		{ 0xff, 0x82 },
		{ 0x51, 0x01 },
		{ 0x58, 0x0a }, /* hpd irq */
		{ 0x59, 0x80 }, /* hpd debounce width */
		{ 0x9e, 0xf7 }, /* video check irq */

		/* power consumption for work */
		{ 0xff, 0x80 },
		{ 0x04, 0xf0 },
		{ 0x06, 0xf0 },
		{ 0x0a, 0x80 },
		{ 0x0b, 0x40 },
		{ 0x0d, 0xef },
		{ 0x11, 0xfa },
	};

	if (lt9611->power_on)
		return 0;
	
	dev_dbg(lt9611->dev, "power on\n");

	ret = regmap_multi_reg_write(lt9611->regmap, seq, ARRAY_SIZE(seq));
	if (!ret)
		lt9611->power_on = true;

	return ret;
}

static int lt9611_power_off(struct lt9611 *lt9611)
{
	int ret;
	const struct reg_sequence off[] = {
		{ 0xff, 0x81 },
		{ 0x30, 0x6a },
	};

	dev_dbg(lt9611->dev, "power off\n");

	ret = regmap_multi_reg_write(lt9611->regmap, off, ARRAY_SIZE(off));
	if (!ret)
		lt9611->power_on = false;

	return ret;
}

static void lt9611_i2s_init(struct lt9611 *lt9611)
{
	const struct reg_sequence init[] = {
		{ 0xff, 0x82 },
		{ 0xd6, 0x8c },
		{ 0xd7, 0x04 },

		{ 0xff, 0x84 },
		{ 0x06, 0x08 },
		{ 0x07, 0x10 },

		{ 0x34, 0xd4 },
	};

	regmap_multi_reg_write(lt9611->regmap, init, ARRAY_SIZE(init));
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

static struct lt9611_mode *lt9611_find_mode(const struct drm_display_mode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(lt9611_modes); i++) {
		if (lt9611_modes[i].hdisplay == mode->hdisplay &&
		    lt9611_modes[i].vdisplay == mode->vdisplay &&
		    lt9611_modes[i].fps == drm_mode_vrefresh(mode)) {
			return &lt9611_modes[i];
		}
	}

	return NULL;
}

/* connector funcs */
static enum drm_connector_status
lt9611_connector_detect(struct drm_connector *connector, bool force)
{
	struct lt9611 *lt9611 = connector_to_lt9611(connector);
	unsigned int reg_val = 0;
	int connected = 0;

	regmap_write(lt9611->regmap, 0xff, 0x82);
	regmap_read(lt9611->regmap, 0x5e, &reg_val);
	connected  = (reg_val & BIT(2));
	dev_dbg(lt9611->dev, "connected = %x\n", connected);

	lt9611->status = connected ?  connector_status_connected :
				connector_status_disconnected;

	return lt9611->status;
}

static int lt9611_read_edid(struct lt9611 *lt9611)
{
	unsigned int temp;
	int ret = 0;
	int i, j;

	memset(lt9611->edid_buf, 0, EDID_SEG_SIZE);

	regmap_write(lt9611->regmap, 0xff, 0x85);
	regmap_write(lt9611->regmap, 0x03, 0xc9);
	regmap_write(lt9611->regmap, 0x04, 0xa0); /* 0xA0 is EDID device address */
	regmap_write(lt9611->regmap, 0x05, 0x00); /* 0x00 is EDID offset address */
	regmap_write(lt9611->regmap, 0x06, 0x20); /* length for read */
	regmap_write(lt9611->regmap, 0x14, 0x7f);

	for (i = 0 ; i < 8 ; i++) {
		regmap_write(lt9611->regmap, 0x05, i * 32); /* offset address */
		regmap_write(lt9611->regmap, 0x07, 0x36);
		regmap_write(lt9611->regmap, 0x07, 0x31);
		regmap_write(lt9611->regmap, 0x07, 0x37);
		usleep_range(5000, 10000);

		regmap_read(lt9611->regmap, 0x40, &temp);

		if (temp & 0x02) {  /*KEY_DDC_ACCS_DONE=1*/
			for (j = 0; j < 32; j++) {
				regmap_read(lt9611->regmap, 0x83, &temp);
				lt9611->edid_buf[i*32+j] = temp;
			}
		} else if (temp & 0x50) { /* DDC No Ack or Abitration lost */
			dev_err(lt9611->dev, "read edid failed: no ack\n");
			ret = -EIO;
			goto end;
		} else {
			dev_err(lt9611->dev, "read edid failed: access not done\n");
			ret = -EIO;
			goto end;
		}
	}

	dev_dbg(lt9611->dev, "read edid succeeded, checksum = 0x%x\n",
		lt9611->edid_buf[255]);

end:
	regmap_write(lt9611->regmap, 0x07, 0x1f);
	return ret;
}

/* TODO: add support for more extenstion blocks */
static int lt9611_get_edid_block(void *data, u8 *buf, unsigned int block,
				  size_t len)
{
	struct lt9611 *lt9611 = data;
	int ret;

	dev_dbg(lt9611->dev, "get edid block: block=%d, len=%d\n", block, (int)len);

	if (len > 128)
		return -EINVAL;

	/* support up to 1 extension block */
	if (block > 1)
		return -EINVAL;

	if (block == 0) {
		/* always read 2 edid blocks once */
		ret = lt9611_read_edid(lt9611);
		if (ret) {
			dev_err(lt9611->dev, "edid read failed\n");
			return ret;
		}
	}

	if (block % 2 == 0)
		memcpy(buf, lt9611->edid_buf, len);
	else
		memcpy(buf, lt9611->edid_buf + 128, len);

	return 0;
}

static int lt9611_connector_get_modes(struct drm_connector *connector)
{
	struct lt9611 *lt9611 = connector_to_lt9611(connector);
	unsigned int count;
	struct edid *edid;

	dev_dbg(lt9611->dev, "get modes\n");

	lt9611_power_on(lt9611);
	edid = drm_do_get_edid(connector, lt9611_get_edid_block, lt9611);
	drm_connector_update_edid_property(connector, edid);
	count = drm_add_edid_modes(connector, edid);
	kfree(edid);

	return count;
}

static enum drm_mode_status lt9611_connector_mode_valid(
	struct drm_connector *connector, struct drm_display_mode *mode)
{
	struct lt9611_mode *lt9611_mode = lt9611_find_mode(mode);

	return lt9611_mode ? MODE_OK : MODE_BAD;
}

/* bridge funcs */
static void lt9611_bridge_enable(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	const struct reg_sequence on[] = {
		{ 0xff, 0x81 },
		{ 0x30, 0xea },
	};

	dev_dbg(lt9611->dev, "bridge enable\n");

	if (lt9611_power_on(lt9611)) {
		dev_err(lt9611->dev, "power on failed\n");
		return;
	}

	dev_dbg(lt9611->dev, "video on\n");

	lt9611_mipi_input_analog(lt9611);
	lt9611_hdmi_tx_digital(lt9611);
	lt9611_hdmi_tx_phy(lt9611);

	msleep(500);

	lt9611_video_check(lt9611);

	/* Enable HDMI output */
	regmap_multi_reg_write(lt9611->regmap, on, ARRAY_SIZE(on));
}

static void lt9611_bridge_disable(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	int ret;
	const struct reg_sequence hdmi_off[] = {
		{ 0xff, 0x81 },
		{ 0x30, 0x6a },
	};

	dev_dbg(lt9611->dev, "bridge disable\n");

	/* Disable HDMI output */
	ret = regmap_multi_reg_write(lt9611->regmap, hdmi_off, ARRAY_SIZE(hdmi_off));
	if (ret) {
		dev_err(lt9611->dev, "video on failed\n");
		return;
	}

	if (lt9611_power_off(lt9611)) {
		dev_err(lt9611->dev, "power on failed\n");
		return;
	}
}

static struct drm_connector_helper_funcs lt9611_bridge_connector_helper_funcs = {
	.get_modes = lt9611_connector_get_modes,
	.mode_valid = lt9611_connector_mode_valid,
};

static const struct drm_connector_funcs lt9611_bridge_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = lt9611_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct mipi_dsi_device *lt9611_attach_dsi(struct lt9611 *lt9611,
						 struct device_node *dsi_node)
{
	const struct mipi_dsi_device_info info = { "lt9611", 0, NULL };
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	int ret;

	host = of_find_mipi_dsi_host_by_node(dsi_node);
	if (!host) {
		dev_err(lt9611->dev, "failed to find dsi host\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		dev_err(lt9611->dev, "failed to create dsi device\n");
		return dsi;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_VIDEO_HSE;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(lt9611->dev, "failed to attach dsi to host\n");
		mipi_dsi_device_unregister(dsi);
		return ERR_PTR(ret);
	}

	return dsi;
}

static int lt9611_bridge_attach(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	int ret;

	dev_dbg(lt9611->dev, "bridge attach\n");

	ret = drm_connector_init(bridge->dev, &lt9611->connector,
				 &lt9611_bridge_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		DRM_ERROR("Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(&lt9611->connector,
				 &lt9611_bridge_connector_helper_funcs);
	drm_connector_attach_encoder(&lt9611->connector, bridge->encoder);

	if (!bridge->encoder) {
		DRM_ERROR("Parent encoder object not found");
		return -ENODEV;
	}

	/* Attach primary DSI */
	lt9611->dsi0 = lt9611_attach_dsi(lt9611, lt9611->dsi0_node);
	if (IS_ERR(lt9611->dsi0))
		return PTR_ERR(lt9611->dsi0);

	/* Attach secondary DSI, if specified */
	if (lt9611->dsi1_node) {
		lt9611->dsi1 = lt9611_attach_dsi(lt9611, lt9611->dsi1_node);
		if (IS_ERR(lt9611->dsi1)) {
			ret = PTR_ERR(lt9611->dsi1);
			goto err_unregister_dsi0;
		}
	}

	return 0;

err_unregister_dsi0:
	mipi_dsi_device_unregister(lt9611->dsi0);

	return ret;
}

static void lt9611_bridge_detach(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	if (lt9611->dsi1) {
		mipi_dsi_detach(lt9611->dsi1);
		mipi_dsi_device_unregister(lt9611->dsi1);
	}

	mipi_dsi_detach(lt9611->dsi0);
	mipi_dsi_device_unregister(lt9611->dsi0);
}

static enum drm_mode_status
lt9611_bridge_mode_valid(struct drm_bridge *bridge,
			 const struct drm_display_mode *mode)
{
	struct lt9611_mode *lt9611_mode = lt9611_find_mode(mode);
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	if (lt9611_mode->intfs > 1 && !lt9611->dsi1)
		return MODE_PANEL;
	else
		return MODE_OK;
}

static void lt9611_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	dev_dbg(lt9611->dev, "bridge pre_enable\n");

	regmap_write(lt9611->regmap, 0xff, 0x80);
	regmap_write(lt9611->regmap, 0xee, 0x01);
}

static void lt9611_bridge_post_disable(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	dev_dbg(lt9611->dev, "bridge post_disable\n");

	/* TODO: We still need to figure out how to best put the
	 * hardware to sleep while still allowing hotplug
	 * detection to work here. -jstultz
	 */
}

static void lt9611_bridge_mode_set(struct drm_bridge *bridge,
				   const struct drm_display_mode *mode,
				   const struct drm_display_mode *adj_mode)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	struct hdmi_avi_infoframe avi_frame;
	int ret;

	dev_dbg(lt9611->dev, "bridge mode_set: hdisplay=%d, vdisplay=%d, vrefresh=%d, clock=%d\n",
		adj_mode->hdisplay, adj_mode->vdisplay,
		adj_mode->vrefresh, adj_mode->clock);

	lt9611_mipi_input_digital(lt9611, mode);
	lt9611_pll_setup(lt9611, mode);
	lt9611_mipi_video_setup(lt9611, mode);
	lt9611_pcr_setup(lt9611, mode);

	ret = drm_hdmi_avi_infoframe_from_display_mode(&avi_frame,
						       &lt9611->connector,
						       mode);
	if (!ret)
		lt9611->vic = avi_frame.video_code;
}

static const struct drm_bridge_funcs lt9611_bridge_funcs = {
	.attach = lt9611_bridge_attach,
	.detach = lt9611_bridge_detach,
	.mode_valid = lt9611_bridge_mode_valid,
	.pre_enable   = lt9611_bridge_pre_enable,
	.enable = lt9611_bridge_enable,
	.disable = lt9611_bridge_disable,
	.post_disable = lt9611_bridge_post_disable,
	.mode_set = lt9611_bridge_mode_set,
};

static int lt9611_parse_dt(struct device *dev,
	struct lt9611 *lt9611)
{
	lt9611->dsi0_node = of_graph_get_remote_node(dev->of_node, 1, -1);
	if (!lt9611->dsi0_node) {
		DRM_DEV_ERROR(dev,
			"failed to get remote node for primary dsi\n");
		return -ENODEV;
	}

	lt9611->dsi1_node = of_graph_get_remote_node(dev->of_node, 2, -1);

	lt9611->ac_mode = of_property_read_bool(dev->of_node, "lt,ac-mode");
	dev_dbg(lt9611->dev, "ac_mode=%d\n", lt9611->ac_mode);

	return 0;
}

static int lt9611_gpio_init(struct lt9611 *lt9611)
{
	struct device *dev = lt9611->dev;

	lt9611->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lt9611->reset_gpio)) {
		dev_err(dev, "failed to acquire reset gpio\n");
		return PTR_ERR(lt9611->reset_gpio);
	}

	lt9611->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
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

	regmap_write(lt9611->regmap, 0xff, 0x80);
	regmap_write(lt9611->regmap, 0xee, 0x01);

	ret = regmap_read(lt9611->regmap, 0x02, &rev);
	if (ret)
		dev_err(lt9611->dev, "failed to read revision: %d\n", ret);

	dev_info(lt9611->dev, "LT9611 revsion: 0x%x\n", rev);

	return ret;
}

static int lt9611_probe(struct i2c_client *client,
	 const struct i2c_device_id *id)
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

	lt9611->dev = &client->dev;
	lt9611->client = client;

	lt9611->regmap = devm_regmap_init_i2c(client, &lt9611_regmap_config);
	if (IS_ERR(lt9611->regmap)) {
		DRM_ERROR("regmap i2c init failed\n");
		return PTR_ERR(lt9611->regmap);
	}

	ret = lt9611_parse_dt(&client->dev, lt9611);
	if (ret) {
		dev_err(dev, "failed to parse device tree\n");
		return ret;
	}

	ret = lt9611_gpio_init(lt9611);
	if (ret < 0)
		return ret;

	ret = lt9611_regulator_init(lt9611);
	if (ret < 0)
		return ret;

	lt9611_assert_5v(lt9611);

	ret = lt9611_regulator_enable(lt9611);
	if (ret)
		return ret;

	lt9611_reset(lt9611);

	ret = lt9611_read_device_rev(lt9611);
	if (ret) {
		dev_err(dev, "failed to read chip rev\n");
		goto err_disable_regulators;
	}

	lt9611_i2s_init(lt9611);

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

	drm_bridge_add(&lt9611->bridge);

	lt9611_enable_hpd_interrupts(lt9611);

	return 0;

err_disable_regulators:
	regulator_bulk_disable(ARRAY_SIZE(lt9611->supplies), lt9611->supplies);

	of_node_put(lt9611->dsi0_node);
	of_node_put(lt9611->dsi1_node);

	return ret;
}

static int lt9611_remove(struct i2c_client *client)
{
	struct lt9611 *lt9611 = i2c_get_clientdata(client);

	disable_irq(client->irq);

	drm_bridge_remove(&lt9611->bridge);

	regulator_bulk_disable(ARRAY_SIZE(lt9611->supplies), lt9611->supplies);

	of_node_put(lt9611->dsi0_node);
	of_node_put(lt9611->dsi1_node);

	return 0;
}


static struct i2c_device_id lt9611_id[] = {
	{ "lt,lt9611", 0},
	{}
};

static const struct of_device_id lt9611_match_table[] = {
	{.compatible = "lt,lt9611"},
	{}
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

MODULE_LICENSE("GPL v2");
