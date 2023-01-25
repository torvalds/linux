// SPDX-License-Identifier: GPL-2.0-only
/*
 * Chrontel CH7033 Video Encoder Driver
 *
 * Copyright (C) 2019,2020 Lubomir Rintel
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

/* Page 0, Register 0x07 */
enum {
	DRI_PD		= BIT(3),
	IO_PD		= BIT(5),
};

/* Page 0, Register 0x08 */
enum {
	DRI_PDDRI	= GENMASK(7, 4),
	PDDAC		= GENMASK(3, 1),
	PANEN		= BIT(0),
};

/* Page 0, Register 0x09 */
enum {
	DPD		= BIT(7),
	GCKOFF		= BIT(6),
	TV_BP		= BIT(5),
	SCLPD		= BIT(4),
	SDPD		= BIT(3),
	VGA_PD		= BIT(2),
	HDBKPD		= BIT(1),
	HDMI_PD		= BIT(0),
};

/* Page 0, Register 0x0a */
enum {
	MEMINIT		= BIT(7),
	MEMIDLE		= BIT(6),
	MEMPD		= BIT(5),
	STOP		= BIT(4),
	LVDS_PD		= BIT(3),
	HD_DVIB		= BIT(2),
	HDCP_PD		= BIT(1),
	MCU_PD		= BIT(0),
};

/* Page 0, Register 0x18 */
enum {
	IDF		= GENMASK(7, 4),
	INTEN		= BIT(3),
	SWAP		= GENMASK(2, 0),
};

enum {
	BYTE_SWAP_RGB	= 0,
	BYTE_SWAP_RBG	= 1,
	BYTE_SWAP_GRB	= 2,
	BYTE_SWAP_GBR	= 3,
	BYTE_SWAP_BRG	= 4,
	BYTE_SWAP_BGR	= 5,
};

/* Page 0, Register 0x19 */
enum {
	HPO_I		= BIT(5),
	VPO_I		= BIT(4),
	DEPO_I		= BIT(3),
	CRYS_EN		= BIT(2),
	GCLKFREQ	= GENMASK(2, 0),
};

/* Page 0, Register 0x2e */
enum {
	HFLIP		= BIT(7),
	VFLIP		= BIT(6),
	DEPO_O		= BIT(5),
	HPO_O		= BIT(4),
	VPO_O		= BIT(3),
	TE		= GENMASK(2, 0),
};

/* Page 0, Register 0x2b */
enum {
	SWAPS		= GENMASK(7, 4),
	VFMT		= GENMASK(3, 0),
};

/* Page 0, Register 0x54 */
enum {
	COMP_BP		= BIT(7),
	DAC_EN_T	= BIT(6),
	HWO_HDMI_HI	= GENMASK(5, 3),
	HOO_HDMI_HI	= GENMASK(2, 0),
};

/* Page 0, Register 0x57 */
enum {
	FLDSEN		= BIT(7),
	VWO_HDMI_HI	= GENMASK(5, 3),
	VOO_HDMI_HI	= GENMASK(2, 0),
};

/* Page 0, Register 0x7e */
enum {
	HDMI_LVDS_SEL	= BIT(7),
	DE_GEN		= BIT(6),
	PWM_INDEX_HI	= BIT(5),
	USE_DE		= BIT(4),
	R_INT		= GENMASK(3, 0),
};

/* Page 1, Register 0x07 */
enum {
	BPCKSEL		= BIT(7),
	DRI_CMFB_EN	= BIT(6),
	CEC_PUEN	= BIT(5),
	CEC_T		= BIT(3),
	CKINV		= BIT(2),
	CK_TVINV	= BIT(1),
	DRI_CKS2	= BIT(0),
};

/* Page 1, Register 0x08 */
enum {
	DACG		= BIT(6),
	DACKTST		= BIT(5),
	DEDGEB		= BIT(4),
	SYO		= BIT(3),
	DRI_IT_LVDS	= GENMASK(2, 1),
	DISPON		= BIT(0),
};

/* Page 1, Register 0x0c */
enum {
	DRI_PLL_CP	= GENMASK(7, 6),
	DRI_PLL_DIVSEL	= BIT(5),
	DRI_PLL_N1_1	= BIT(4),
	DRI_PLL_N1_0	= BIT(3),
	DRI_PLL_N3_1	= BIT(2),
	DRI_PLL_N3_0	= BIT(1),
	DRI_PLL_CKTSTEN = BIT(0),
};

/* Page 1, Register 0x6b */
enum {
	VCO3CS		= GENMASK(7, 6),
	ICPGBK2_0	= GENMASK(5, 3),
	DRI_VCO357SC	= BIT(2),
	PDPLL2		= BIT(1),
	DRI_PD_SER	= BIT(0),
};

/* Page 1, Register 0x6c */
enum {
	PLL2N11		= GENMASK(7, 4),
	PLL2N5_4	= BIT(3),
	PLL2N5_TOP	= BIT(2),
	DRI_PLL_PD	= BIT(1),
	PD_I2CM		= BIT(0),
};

/* Page 3, Register 0x28 */
enum {
	DIFF_EN		= GENMASK(7, 6),
	CORREC_EN	= GENMASK(5, 4),
	VGACLK_BP	= BIT(3),
	HM_LV_SEL	= BIT(2),
	HD_VGA_SEL	= BIT(1),
};

/* Page 3, Register 0x2a */
enum {
	LVDSCLK_BP	= BIT(7),
	HDTVCLK_BP	= BIT(6),
	HDMICLK_BP	= BIT(5),
	HDTV_BP		= BIT(4),
	HDMI_BP		= BIT(3),
	THRWL		= GENMASK(2, 0),
};

/* Page 4, Register 0x52 */
enum {
	PGM_ARSTB	= BIT(7),
	MCU_ARSTB	= BIT(6),
	MCU_RETB	= BIT(2),
	RESETIB		= BIT(1),
	RESETDB		= BIT(0),
};

struct ch7033_priv {
	struct regmap *regmap;
	struct drm_bridge *next_bridge;
	struct drm_bridge bridge;
	struct drm_connector connector;
};

#define conn_to_ch7033_priv(x) \
	container_of(x, struct ch7033_priv, connector)
#define bridge_to_ch7033_priv(x) \
	container_of(x, struct ch7033_priv, bridge)


static enum drm_connector_status ch7033_connector_detect(
	struct drm_connector *connector, bool force)
{
	struct ch7033_priv *priv = conn_to_ch7033_priv(connector);

	return drm_bridge_detect(priv->next_bridge);
}

static const struct drm_connector_funcs ch7033_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = ch7033_connector_detect,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int ch7033_connector_get_modes(struct drm_connector *connector)
{
	struct ch7033_priv *priv = conn_to_ch7033_priv(connector);
	struct edid *edid;
	int ret;

	edid = drm_bridge_get_edid(priv->next_bridge, connector);
	drm_connector_update_edid_property(connector, edid);
	if (edid) {
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	} else {
		ret = drm_add_modes_noedid(connector, 1920, 1080);
		drm_set_preferred_mode(connector, 1024, 768);
	}

	return ret;
}

static struct drm_encoder *ch7033_connector_best_encoder(
			struct drm_connector *connector)
{
	struct ch7033_priv *priv = conn_to_ch7033_priv(connector);

	return priv->bridge.encoder;
}

static const struct drm_connector_helper_funcs ch7033_connector_helper_funcs = {
	.get_modes = ch7033_connector_get_modes,
	.best_encoder = ch7033_connector_best_encoder,
};

static void ch7033_hpd_event(void *arg, enum drm_connector_status status)
{
	struct ch7033_priv *priv = arg;

	if (priv->bridge.dev)
		drm_helper_hpd_irq_event(priv->connector.dev);
}

static int ch7033_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct ch7033_priv *priv = bridge_to_ch7033_priv(bridge);
	struct drm_connector *connector = &priv->connector;
	int ret;

	ret = drm_bridge_attach(bridge->encoder, priv->next_bridge, bridge,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		return ret;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	if (priv->next_bridge->ops & DRM_BRIDGE_OP_DETECT) {
		connector->polled = DRM_CONNECTOR_POLL_HPD;
	} else {
		connector->polled = DRM_CONNECTOR_POLL_CONNECT |
				    DRM_CONNECTOR_POLL_DISCONNECT;
	}

	if (priv->next_bridge->ops & DRM_BRIDGE_OP_HPD) {
		drm_bridge_hpd_enable(priv->next_bridge, ch7033_hpd_event,
				      priv);
	}

	drm_connector_helper_add(connector,
				 &ch7033_connector_helper_funcs);
	ret = drm_connector_init_with_ddc(bridge->dev, &priv->connector,
					  &ch7033_connector_funcs,
					  priv->next_bridge->type,
					  priv->next_bridge->ddc);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	return drm_connector_attach_encoder(&priv->connector, bridge->encoder);
}

static void ch7033_bridge_detach(struct drm_bridge *bridge)
{
	struct ch7033_priv *priv = bridge_to_ch7033_priv(bridge);

	if (priv->next_bridge->ops & DRM_BRIDGE_OP_HPD)
		drm_bridge_hpd_disable(priv->next_bridge);
	drm_connector_cleanup(&priv->connector);
}

static enum drm_mode_status ch7033_bridge_mode_valid(struct drm_bridge *bridge,
				     const struct drm_display_info *info,
				     const struct drm_display_mode *mode)
{
	if (mode->clock > 165000)
		return MODE_CLOCK_HIGH;
	if (mode->hdisplay >= 1920)
		return MODE_BAD_HVALUE;
	if (mode->vdisplay >= 1080)
		return MODE_BAD_VVALUE;
	return MODE_OK;
}

static void ch7033_bridge_disable(struct drm_bridge *bridge)
{
	struct ch7033_priv *priv = bridge_to_ch7033_priv(bridge);

	regmap_write(priv->regmap, 0x03, 0x04);
	regmap_update_bits(priv->regmap, 0x52, RESETDB, 0x00);
}

static void ch7033_bridge_enable(struct drm_bridge *bridge)
{
	struct ch7033_priv *priv = bridge_to_ch7033_priv(bridge);

	regmap_write(priv->regmap, 0x03, 0x04);
	regmap_update_bits(priv->regmap, 0x52, RESETDB, RESETDB);
}

static void ch7033_bridge_mode_set(struct drm_bridge *bridge,
				   const struct drm_display_mode *mode,
				   const struct drm_display_mode *adjusted_mode)
{
	struct ch7033_priv *priv = bridge_to_ch7033_priv(bridge);
	int hbporch = mode->hsync_start - mode->hdisplay;
	int hsynclen = mode->hsync_end - mode->hsync_start;
	int vbporch = mode->vsync_start - mode->vdisplay;
	int vsynclen = mode->vsync_end - mode->vsync_start;

	/*
	 * Page 4
	 */
	regmap_write(priv->regmap, 0x03, 0x04);

	/* Turn everything off to set all the registers to their defaults. */
	regmap_write(priv->regmap, 0x52, 0x00);
	/* Bring I/O block up. */
	regmap_write(priv->regmap, 0x52, RESETIB);

	/*
	 * Page 0
	 */
	regmap_write(priv->regmap, 0x03, 0x00);

	/* Bring up parts we need from the power down. */
	regmap_update_bits(priv->regmap, 0x07, DRI_PD | IO_PD, 0);
	regmap_update_bits(priv->regmap, 0x08, DRI_PDDRI | PDDAC | PANEN, 0);
	regmap_update_bits(priv->regmap, 0x09, DPD | GCKOFF |
					       HDMI_PD | VGA_PD, 0);
	regmap_update_bits(priv->regmap, 0x0a, HD_DVIB, 0);

	/* Horizontal input timing. */
	regmap_write(priv->regmap, 0x0b, (mode->htotal >> 8) << 3 |
					 (mode->hdisplay >> 8));
	regmap_write(priv->regmap, 0x0c, mode->hdisplay);
	regmap_write(priv->regmap, 0x0d, mode->htotal);
	regmap_write(priv->regmap, 0x0e, (hsynclen >> 8) << 3 |
					 (hbporch >> 8));
	regmap_write(priv->regmap, 0x0f, hbporch);
	regmap_write(priv->regmap, 0x10, hsynclen);

	/* Vertical input timing. */
	regmap_write(priv->regmap, 0x11, (mode->vtotal >> 8) << 3 |
					 (mode->vdisplay >> 8));
	regmap_write(priv->regmap, 0x12, mode->vdisplay);
	regmap_write(priv->regmap, 0x13, mode->vtotal);
	regmap_write(priv->regmap, 0x14, ((vsynclen >> 8) << 3) |
					 (vbporch >> 8));
	regmap_write(priv->regmap, 0x15, vbporch);
	regmap_write(priv->regmap, 0x16, vsynclen);

	/* Input color swap. */
	regmap_update_bits(priv->regmap, 0x18, SWAP, BYTE_SWAP_BGR);

	/* Input clock and sync polarity. */
	regmap_update_bits(priv->regmap, 0x19, 0x1, mode->clock >> 16);
	regmap_update_bits(priv->regmap, 0x19, HPO_I | VPO_I | GCLKFREQ,
			   (mode->flags & DRM_MODE_FLAG_PHSYNC) ? HPO_I : 0 |
			   (mode->flags & DRM_MODE_FLAG_PVSYNC) ? VPO_I : 0 |
			   mode->clock >> 16);
	regmap_write(priv->regmap, 0x1a, mode->clock >> 8);
	regmap_write(priv->regmap, 0x1b, mode->clock);

	/* Horizontal output timing. */
	regmap_write(priv->regmap, 0x1f, (mode->htotal >> 8) << 3 |
					 (mode->hdisplay >> 8));
	regmap_write(priv->regmap, 0x20, mode->hdisplay);
	regmap_write(priv->regmap, 0x21, mode->htotal);

	/* Vertical output timing. */
	regmap_write(priv->regmap, 0x25, (mode->vtotal >> 8) << 3 |
					 (mode->vdisplay >> 8));
	regmap_write(priv->regmap, 0x26, mode->vdisplay);
	regmap_write(priv->regmap, 0x27, mode->vtotal);

	/* VGA channel bypass */
	regmap_update_bits(priv->regmap, 0x2b, VFMT, 9);

	/* Output sync polarity. */
	regmap_update_bits(priv->regmap, 0x2e, HPO_O | VPO_O,
			   (mode->flags & DRM_MODE_FLAG_PHSYNC) ? HPO_O : 0 |
			   (mode->flags & DRM_MODE_FLAG_PVSYNC) ? VPO_O : 0);

	/* HDMI horizontal output timing. */
	regmap_update_bits(priv->regmap, 0x54, HWO_HDMI_HI | HOO_HDMI_HI,
					       (hsynclen >> 8) << 3 |
					       (hbporch >> 8));
	regmap_write(priv->regmap, 0x55, hbporch);
	regmap_write(priv->regmap, 0x56, hsynclen);

	/* HDMI vertical output timing. */
	regmap_update_bits(priv->regmap, 0x57, VWO_HDMI_HI | VOO_HDMI_HI,
					       (vsynclen >> 8) << 3 |
					       (vbporch >> 8));
	regmap_write(priv->regmap, 0x58, vbporch);
	regmap_write(priv->regmap, 0x59, vsynclen);

	/* Pick HDMI, not LVDS. */
	regmap_update_bits(priv->regmap, 0x7e, HDMI_LVDS_SEL, HDMI_LVDS_SEL);

	/*
	 * Page 1
	 */
	regmap_write(priv->regmap, 0x03, 0x01);

	/* No idea what these do, but VGA is wobbly and blinky without them. */
	regmap_update_bits(priv->regmap, 0x07, CKINV, CKINV);
	regmap_update_bits(priv->regmap, 0x08, DISPON, DISPON);

	/* DRI PLL */
	regmap_update_bits(priv->regmap, 0x0c, DRI_PLL_DIVSEL, DRI_PLL_DIVSEL);
	if (mode->clock <= 40000) {
		regmap_update_bits(priv->regmap, 0x0c, DRI_PLL_N1_1 |
						       DRI_PLL_N1_0 |
						       DRI_PLL_N3_1 |
						       DRI_PLL_N3_0,
						       0);
	} else if (mode->clock < 80000) {
		regmap_update_bits(priv->regmap, 0x0c, DRI_PLL_N1_1 |
						       DRI_PLL_N1_0 |
						       DRI_PLL_N3_1 |
						       DRI_PLL_N3_0,
						       DRI_PLL_N3_0 |
						       DRI_PLL_N1_0);
	} else {
		regmap_update_bits(priv->regmap, 0x0c, DRI_PLL_N1_1 |
						       DRI_PLL_N1_0 |
						       DRI_PLL_N3_1 |
						       DRI_PLL_N3_0,
						       DRI_PLL_N3_1 |
						       DRI_PLL_N1_1);
	}

	/* This seems to be color calibration for VGA. */
	regmap_write(priv->regmap, 0x64, 0x29); /* LSB Blue */
	regmap_write(priv->regmap, 0x65, 0x29); /* LSB Green */
	regmap_write(priv->regmap, 0x66, 0x29); /* LSB Red */
	regmap_write(priv->regmap, 0x67, 0x00); /* MSB Blue */
	regmap_write(priv->regmap, 0x68, 0x00); /* MSB Green */
	regmap_write(priv->regmap, 0x69, 0x00); /* MSB Red */

	regmap_update_bits(priv->regmap, 0x6b, DRI_PD_SER, 0x00);
	regmap_update_bits(priv->regmap, 0x6c, DRI_PLL_PD, 0x00);

	/*
	 * Page 3
	 */
	regmap_write(priv->regmap, 0x03, 0x03);

	/* More bypasses and apparently another HDMI/LVDS selector. */
	regmap_update_bits(priv->regmap, 0x28, VGACLK_BP | HM_LV_SEL,
					       VGACLK_BP | HM_LV_SEL);
	regmap_update_bits(priv->regmap, 0x2a, HDMICLK_BP | HDMI_BP,
					       HDMICLK_BP | HDMI_BP);

	/*
	 * Page 4
	 */
	regmap_write(priv->regmap, 0x03, 0x04);

	/* Output clock. */
	regmap_write(priv->regmap, 0x10, mode->clock >> 16);
	regmap_write(priv->regmap, 0x11, mode->clock >> 8);
	regmap_write(priv->regmap, 0x12, mode->clock);
}

static const struct drm_bridge_funcs ch7033_bridge_funcs = {
	.attach = ch7033_bridge_attach,
	.detach = ch7033_bridge_detach,
	.mode_valid = ch7033_bridge_mode_valid,
	.disable = ch7033_bridge_disable,
	.enable = ch7033_bridge_enable,
	.mode_set = ch7033_bridge_mode_set,
};

static const struct regmap_config ch7033_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x7f,
};

static int ch7033_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ch7033_priv *priv;
	unsigned int val;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1, NULL,
					  &priv->next_bridge);
	if (ret)
		return ret;

	priv->regmap = devm_regmap_init_i2c(client, &ch7033_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev, "regmap init failed\n");
		return PTR_ERR(priv->regmap);
	}

	ret = regmap_read(priv->regmap, 0x00, &val);
	if (ret < 0) {
		dev_err(&client->dev, "error reading the model id: %d\n", ret);
		return ret;
	}
	if ((val & 0xf7) != 0x56) {
		dev_err(&client->dev, "the device is not a ch7033\n");
		return -ENODEV;
	}

	regmap_write(priv->regmap, 0x03, 0x04);
	ret = regmap_read(priv->regmap, 0x51, &val);
	if (ret < 0) {
		dev_err(&client->dev, "error reading the model id: %d\n", ret);
		return ret;
	}
	if ((val & 0x0f) != 3) {
		dev_err(&client->dev, "unknown revision %u\n", val);
		return -ENODEV;
	}

	INIT_LIST_HEAD(&priv->bridge.list);
	priv->bridge.funcs = &ch7033_bridge_funcs;
	priv->bridge.of_node = dev->of_node;
	drm_bridge_add(&priv->bridge);

	dev_info(dev, "Chrontel CH7033 Video Encoder\n");
	return 0;
}

static void ch7033_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ch7033_priv *priv = dev_get_drvdata(dev);

	drm_bridge_remove(&priv->bridge);
}

static const struct of_device_id ch7033_dt_ids[] = {
	{ .compatible = "chrontel,ch7033", },
	{ }
};
MODULE_DEVICE_TABLE(of, ch7033_dt_ids);

static const struct i2c_device_id ch7033_ids[] = {
	{ "ch7033", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ch7033_ids);

static struct i2c_driver ch7033_driver = {
	.probe_new = ch7033_probe,
	.remove = ch7033_remove,
	.driver = {
		.name = "ch7033",
		.of_match_table = of_match_ptr(ch7033_dt_ids),
	},
	.id_table = ch7033_ids,
};

module_i2c_driver(ch7033_driver);

MODULE_AUTHOR("Lubomir Rintel <lkundrak@v3.sk>");
MODULE_DESCRIPTION("Chrontel CH7033 Video Encoder Driver");
MODULE_LICENSE("GPL v2");
