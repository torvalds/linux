/*
 * Copyright (C) 2010 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/module.h>

#include <drm/drm_drv.h>
#include <drm/drm_encoder_slave.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/i2c/sil164.h>

struct sil164_priv {
	struct sil164_encoder_params config;
	struct i2c_client *duallink_slave;

	uint8_t saved_state[0x10];
	uint8_t saved_slave_state[0x10];
};

#define to_sil164_priv(x) \
	((struct sil164_priv *)to_encoder_slave(x)->slave_priv)

#define sil164_dbg(client, format, ...) do {				\
		if (drm_debug_enabled(DRM_UT_KMS))			\
			dev_printk(KERN_DEBUG, &client->dev,		\
				   "%s: " format, __func__, ## __VA_ARGS__); \
	} while (0)
#define sil164_info(client, format, ...)		\
	dev_info(&client->dev, format, __VA_ARGS__)
#define sil164_err(client, format, ...)			\
	dev_err(&client->dev, format, __VA_ARGS__)

#define SIL164_I2C_ADDR_MASTER			0x38
#define SIL164_I2C_ADDR_SLAVE			0x39

/* HW register definitions */

#define SIL164_VENDOR_LO			0x0
#define SIL164_VENDOR_HI			0x1
#define SIL164_DEVICE_LO			0x2
#define SIL164_DEVICE_HI			0x3
#define SIL164_REVISION				0x4
#define SIL164_FREQ_MIN				0x6
#define SIL164_FREQ_MAX				0x7
#define SIL164_CONTROL0				0x8
#  define SIL164_CONTROL0_POWER_ON		0x01
#  define SIL164_CONTROL0_EDGE_RISING		0x02
#  define SIL164_CONTROL0_INPUT_24BIT		0x04
#  define SIL164_CONTROL0_DUAL_EDGE		0x08
#  define SIL164_CONTROL0_HSYNC_ON		0x10
#  define SIL164_CONTROL0_VSYNC_ON		0x20
#define SIL164_DETECT				0x9
#  define SIL164_DETECT_INTR_STAT		0x01
#  define SIL164_DETECT_HOTPLUG_STAT		0x02
#  define SIL164_DETECT_RECEIVER_STAT		0x04
#  define SIL164_DETECT_INTR_MODE_RECEIVER	0x00
#  define SIL164_DETECT_INTR_MODE_HOTPLUG	0x08
#  define SIL164_DETECT_OUT_MODE_HIGH		0x00
#  define SIL164_DETECT_OUT_MODE_INTR		0x10
#  define SIL164_DETECT_OUT_MODE_RECEIVER	0x20
#  define SIL164_DETECT_OUT_MODE_HOTPLUG	0x30
#  define SIL164_DETECT_VSWING_STAT		0x80
#define SIL164_CONTROL1				0xa
#  define SIL164_CONTROL1_DESKEW_ENABLE		0x10
#  define SIL164_CONTROL1_DESKEW_INCR_SHIFT	5
#define SIL164_GPIO				0xb
#define SIL164_CONTROL2				0xc
#  define SIL164_CONTROL2_FILTER_ENABLE		0x01
#  define SIL164_CONTROL2_FILTER_SETTING_SHIFT	1
#  define SIL164_CONTROL2_DUALLINK_MASTER	0x40
#  define SIL164_CONTROL2_SYNC_CONT		0x80
#define SIL164_DUALLINK				0xd
#  define SIL164_DUALLINK_ENABLE		0x10
#  define SIL164_DUALLINK_SKEW_SHIFT		5
#define SIL164_PLLZONE				0xe
#  define SIL164_PLLZONE_STAT			0x08
#  define SIL164_PLLZONE_FORCE_ON		0x10
#  define SIL164_PLLZONE_FORCE_HIGH		0x20

/* HW access functions */

static void
sil164_write(struct i2c_client *client, uint8_t addr, uint8_t val)
{
	uint8_t buf[] = {addr, val};
	int ret;

	ret = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (ret < 0)
		sil164_err(client, "Error %d writing to subaddress 0x%x\n",
			   ret, addr);
}

static uint8_t
sil164_read(struct i2c_client *client, uint8_t addr)
{
	uint8_t val;
	int ret;

	ret = i2c_master_send(client, &addr, sizeof(addr));
	if (ret < 0)
		goto fail;

	ret = i2c_master_recv(client, &val, sizeof(val));
	if (ret < 0)
		goto fail;

	return val;

fail:
	sil164_err(client, "Error %d reading from subaddress 0x%x\n",
		   ret, addr);
	return 0;
}

static void
sil164_save_state(struct i2c_client *client, uint8_t *state)
{
	int i;

	for (i = 0x8; i <= 0xe; i++)
		state[i] = sil164_read(client, i);
}

static void
sil164_restore_state(struct i2c_client *client, uint8_t *state)
{
	int i;

	for (i = 0x8; i <= 0xe; i++)
		sil164_write(client, i, state[i]);
}

static void
sil164_set_power_state(struct i2c_client *client, bool on)
{
	uint8_t control0 = sil164_read(client, SIL164_CONTROL0);

	if (on)
		control0 |= SIL164_CONTROL0_POWER_ON;
	else
		control0 &= ~SIL164_CONTROL0_POWER_ON;

	sil164_write(client, SIL164_CONTROL0, control0);
}

static void
sil164_init_state(struct i2c_client *client,
		  struct sil164_encoder_params *config,
		  bool duallink)
{
	sil164_write(client, SIL164_CONTROL0,
		     SIL164_CONTROL0_HSYNC_ON |
		     SIL164_CONTROL0_VSYNC_ON |
		     (config->input_edge ? SIL164_CONTROL0_EDGE_RISING : 0) |
		     (config->input_width ? SIL164_CONTROL0_INPUT_24BIT : 0) |
		     (config->input_dual ? SIL164_CONTROL0_DUAL_EDGE : 0));

	sil164_write(client, SIL164_DETECT,
		     SIL164_DETECT_INTR_STAT |
		     SIL164_DETECT_OUT_MODE_RECEIVER);

	sil164_write(client, SIL164_CONTROL1,
		     (config->input_skew ? SIL164_CONTROL1_DESKEW_ENABLE : 0) |
		     (((config->input_skew + 4) & 0x7)
		      << SIL164_CONTROL1_DESKEW_INCR_SHIFT));

	sil164_write(client, SIL164_CONTROL2,
		     SIL164_CONTROL2_SYNC_CONT |
		     (config->pll_filter ? 0 : SIL164_CONTROL2_FILTER_ENABLE) |
		     (4 << SIL164_CONTROL2_FILTER_SETTING_SHIFT));

	sil164_write(client, SIL164_PLLZONE, 0);

	if (duallink)
		sil164_write(client, SIL164_DUALLINK,
			     SIL164_DUALLINK_ENABLE |
			     (((config->duallink_skew + 4) & 0x7)
			      << SIL164_DUALLINK_SKEW_SHIFT));
	else
		sil164_write(client, SIL164_DUALLINK, 0);
}

/* DRM encoder functions */

static void
sil164_encoder_set_config(struct drm_encoder *encoder, void *params)
{
	struct sil164_priv *priv = to_sil164_priv(encoder);

	priv->config = *(struct sil164_encoder_params *)params;
}

static void
sil164_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct sil164_priv *priv = to_sil164_priv(encoder);
	bool on = (mode == DRM_MODE_DPMS_ON);
	bool duallink = (on && encoder->crtc->mode.clock > 165000);

	sil164_set_power_state(drm_i2c_encoder_get_client(encoder), on);

	if (priv->duallink_slave)
		sil164_set_power_state(priv->duallink_slave, duallink);
}

static void
sil164_encoder_save(struct drm_encoder *encoder)
{
	struct sil164_priv *priv = to_sil164_priv(encoder);

	sil164_save_state(drm_i2c_encoder_get_client(encoder),
			  priv->saved_state);

	if (priv->duallink_slave)
		sil164_save_state(priv->duallink_slave,
				  priv->saved_slave_state);
}

static void
sil164_encoder_restore(struct drm_encoder *encoder)
{
	struct sil164_priv *priv = to_sil164_priv(encoder);

	sil164_restore_state(drm_i2c_encoder_get_client(encoder),
			     priv->saved_state);

	if (priv->duallink_slave)
		sil164_restore_state(priv->duallink_slave,
				     priv->saved_slave_state);
}

static int
sil164_encoder_mode_valid(struct drm_encoder *encoder,
			  struct drm_display_mode *mode)
{
	struct sil164_priv *priv = to_sil164_priv(encoder);

	if (mode->clock < 32000)
		return MODE_CLOCK_LOW;

	if (mode->clock > 330000 ||
	    (mode->clock > 165000 && !priv->duallink_slave))
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static void
sil164_encoder_mode_set(struct drm_encoder *encoder,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct sil164_priv *priv = to_sil164_priv(encoder);
	bool duallink = adjusted_mode->clock > 165000;

	sil164_init_state(drm_i2c_encoder_get_client(encoder),
			  &priv->config, duallink);

	if (priv->duallink_slave)
		sil164_init_state(priv->duallink_slave,
				  &priv->config, duallink);

	sil164_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
}

static enum drm_connector_status
sil164_encoder_detect(struct drm_encoder *encoder,
		      struct drm_connector *connector)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);

	if (sil164_read(client, SIL164_DETECT) & SIL164_DETECT_HOTPLUG_STAT)
		return connector_status_connected;
	else
		return connector_status_disconnected;
}

static int
sil164_encoder_get_modes(struct drm_encoder *encoder,
			 struct drm_connector *connector)
{
	return 0;
}

static int
sil164_encoder_create_resources(struct drm_encoder *encoder,
				struct drm_connector *connector)
{
	return 0;
}

static int
sil164_encoder_set_property(struct drm_encoder *encoder,
			    struct drm_connector *connector,
			    struct drm_property *property,
			    uint64_t val)
{
	return 0;
}

static void
sil164_encoder_destroy(struct drm_encoder *encoder)
{
	struct sil164_priv *priv = to_sil164_priv(encoder);

	i2c_unregister_device(priv->duallink_slave);

	kfree(priv);
	drm_i2c_encoder_destroy(encoder);
}

static const struct drm_encoder_slave_funcs sil164_encoder_funcs = {
	.set_config = sil164_encoder_set_config,
	.destroy = sil164_encoder_destroy,
	.dpms = sil164_encoder_dpms,
	.save = sil164_encoder_save,
	.restore = sil164_encoder_restore,
	.mode_valid = sil164_encoder_mode_valid,
	.mode_set = sil164_encoder_mode_set,
	.detect = sil164_encoder_detect,
	.get_modes = sil164_encoder_get_modes,
	.create_resources = sil164_encoder_create_resources,
	.set_property = sil164_encoder_set_property,
};

/* I2C driver functions */

static int
sil164_probe(struct i2c_client *client)
{
	int vendor = sil164_read(client, SIL164_VENDOR_HI) << 8 |
		sil164_read(client, SIL164_VENDOR_LO);
	int device = sil164_read(client, SIL164_DEVICE_HI) << 8 |
		sil164_read(client, SIL164_DEVICE_LO);
	int rev = sil164_read(client, SIL164_REVISION);

	if (vendor != 0x1 || device != 0x6) {
		sil164_dbg(client, "Unknown device %x:%x.%x\n",
			   vendor, device, rev);
		return -ENODEV;
	}

	sil164_info(client, "Detected device %x:%x.%x\n",
		    vendor, device, rev);

	return 0;
}

static struct i2c_client *
sil164_detect_slave(struct i2c_client *client)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg = {
		.addr = SIL164_I2C_ADDR_SLAVE,
		.len = 0,
	};
	const struct i2c_board_info info = {
		I2C_BOARD_INFO("sil164", SIL164_I2C_ADDR_SLAVE)
	};

	if (i2c_transfer(adap, &msg, 1) != 1) {
		sil164_dbg(adap, "No dual-link slave found.");
		return NULL;
	}

	return i2c_new_client_device(adap, &info);
}

static int
sil164_encoder_init(struct i2c_client *client,
		    struct drm_device *dev,
		    struct drm_encoder_slave *encoder)
{
	struct sil164_priv *priv;
	struct i2c_client *slave_client;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	encoder->slave_priv = priv;
	encoder->slave_funcs = &sil164_encoder_funcs;

	slave_client = sil164_detect_slave(client);
	if (!IS_ERR(slave_client))
		priv->duallink_slave = slave_client;

	return 0;
}

static const struct i2c_device_id sil164_ids[] = {
	{ "sil164" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sil164_ids);

static struct drm_i2c_encoder_driver sil164_driver = {
	.i2c_driver = {
		.probe = sil164_probe,
		.driver = {
			.name = "sil164",
		},
		.id_table = sil164_ids,
	},
	.encoder_init = sil164_encoder_init,
};

/* Module initialization */

static int __init
sil164_init(void)
{
	return drm_i2c_encoder_register(THIS_MODULE, &sil164_driver);
}

static void __exit
sil164_exit(void)
{
	drm_i2c_encoder_unregister(&sil164_driver);
}

MODULE_AUTHOR("Francisco Jerez <currojerez@riseup.net>");
MODULE_DESCRIPTION("Silicon Image sil164 TMDS transmitter driver");
MODULE_LICENSE("GPL and additional rights");

module_init(sil164_init);
module_exit(sil164_exit);
