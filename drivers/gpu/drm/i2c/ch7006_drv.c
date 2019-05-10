/*
 * Copyright (C) 2009 Francisco Jerez.
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

#include "ch7006_priv.h"

/* DRM encoder functions */

static void ch7006_encoder_set_config(struct drm_encoder *encoder,
				      void *params)
{
	struct ch7006_priv *priv = to_ch7006_priv(encoder);

	priv->params = *(struct ch7006_encoder_params *)params;
}

static void ch7006_encoder_destroy(struct drm_encoder *encoder)
{
	struct ch7006_priv *priv = to_ch7006_priv(encoder);

	drm_property_destroy(encoder->dev, priv->scale_property);

	kfree(priv);
	to_encoder_slave(encoder)->slave_priv = NULL;

	drm_i2c_encoder_destroy(encoder);
}

static void  ch7006_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	struct ch7006_state *state = &priv->state;

	ch7006_dbg(client, "\n");

	if (mode == priv->last_dpms)
		return;
	priv->last_dpms = mode;

	ch7006_setup_power_state(encoder);

	ch7006_load_reg(client, state, CH7006_POWER);
}

static void ch7006_encoder_save(struct drm_encoder *encoder)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	struct ch7006_priv *priv = to_ch7006_priv(encoder);

	ch7006_dbg(client, "\n");

	ch7006_state_save(client, &priv->saved_state);
}

static void ch7006_encoder_restore(struct drm_encoder *encoder)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	struct ch7006_priv *priv = to_ch7006_priv(encoder);

	ch7006_dbg(client, "\n");

	ch7006_state_load(client, &priv->saved_state);
}

static bool ch7006_encoder_mode_fixup(struct drm_encoder *encoder,
				      const struct drm_display_mode *mode,
				      struct drm_display_mode *adjusted_mode)
{
	struct ch7006_priv *priv = to_ch7006_priv(encoder);

	/* The ch7006 is painfully picky with the input timings so no
	 * custom modes for now... */

	priv->mode = ch7006_lookup_mode(encoder, mode);

	return !!priv->mode;
}

static int ch7006_encoder_mode_valid(struct drm_encoder *encoder,
				     struct drm_display_mode *mode)
{
	if (ch7006_lookup_mode(encoder, mode))
		return MODE_OK;
	else
		return MODE_BAD;
}

static void ch7006_encoder_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *drm_mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	struct ch7006_encoder_params *params = &priv->params;
	struct ch7006_state *state = &priv->state;
	uint8_t *regs = state->regs;
	const struct ch7006_mode *mode = priv->mode;
	const struct ch7006_tv_norm_info *norm = &ch7006_tv_norms[priv->norm];
	int start_active;

	ch7006_dbg(client, "\n");

	regs[CH7006_DISPMODE] = norm->dispmode | mode->dispmode;
	regs[CH7006_BWIDTH] = 0;
	regs[CH7006_INPUT_FORMAT] = bitf(CH7006_INPUT_FORMAT_FORMAT,
					 params->input_format);

	regs[CH7006_CLKMODE] = CH7006_CLKMODE_SUBC_LOCK
		| bitf(CH7006_CLKMODE_XCM, params->xcm)
		| bitf(CH7006_CLKMODE_PCM, params->pcm);
	if (params->clock_mode)
		regs[CH7006_CLKMODE] |= CH7006_CLKMODE_MASTER;
	if (params->clock_edge)
		regs[CH7006_CLKMODE] |= CH7006_CLKMODE_POS_EDGE;

	start_active = (drm_mode->htotal & ~0x7) - (drm_mode->hsync_start & ~0x7);
	regs[CH7006_POV] = bitf(CH7006_POV_START_ACTIVE_8, start_active);
	regs[CH7006_START_ACTIVE] = bitf(CH7006_START_ACTIVE_0, start_active);

	regs[CH7006_INPUT_SYNC] = 0;
	if (params->sync_direction)
		regs[CH7006_INPUT_SYNC] |= CH7006_INPUT_SYNC_OUTPUT;
	if (params->sync_encoding)
		regs[CH7006_INPUT_SYNC] |= CH7006_INPUT_SYNC_EMBEDDED;
	if (drm_mode->flags & DRM_MODE_FLAG_PVSYNC)
		regs[CH7006_INPUT_SYNC] |= CH7006_INPUT_SYNC_PVSYNC;
	if (drm_mode->flags & DRM_MODE_FLAG_PHSYNC)
		regs[CH7006_INPUT_SYNC] |= CH7006_INPUT_SYNC_PHSYNC;

	regs[CH7006_DETECT] = 0;
	regs[CH7006_BCLKOUT] = 0;

	regs[CH7006_SUBC_INC3] = 0;
	if (params->pout_level)
		regs[CH7006_SUBC_INC3] |= CH7006_SUBC_INC3_POUT_3_3V;

	regs[CH7006_SUBC_INC4] = 0;
	if (params->active_detect)
		regs[CH7006_SUBC_INC4] |= CH7006_SUBC_INC4_DS_INPUT;

	regs[CH7006_PLL_CONTROL] = priv->saved_state.regs[CH7006_PLL_CONTROL];

	ch7006_setup_levels(encoder);
	ch7006_setup_subcarrier(encoder);
	ch7006_setup_pll(encoder);
	ch7006_setup_power_state(encoder);
	ch7006_setup_properties(encoder);

	ch7006_state_load(client, state);
}

static enum drm_connector_status ch7006_encoder_detect(struct drm_encoder *encoder,
						       struct drm_connector *connector)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	struct ch7006_state *state = &priv->state;
	int det;

	ch7006_dbg(client, "\n");

	ch7006_save_reg(client, state, CH7006_DETECT);
	ch7006_save_reg(client, state, CH7006_POWER);
	ch7006_save_reg(client, state, CH7006_CLKMODE);

	ch7006_write(client, CH7006_POWER, CH7006_POWER_RESET |
					   bitfs(CH7006_POWER_LEVEL, NORMAL));
	ch7006_write(client, CH7006_CLKMODE, CH7006_CLKMODE_MASTER);

	ch7006_write(client, CH7006_DETECT, CH7006_DETECT_SENSE);

	ch7006_write(client, CH7006_DETECT, 0);

	det = ch7006_read(client, CH7006_DETECT);

	ch7006_load_reg(client, state, CH7006_CLKMODE);
	ch7006_load_reg(client, state, CH7006_POWER);
	ch7006_load_reg(client, state, CH7006_DETECT);

	if ((det & (CH7006_DETECT_SVIDEO_Y_TEST|
		    CH7006_DETECT_SVIDEO_C_TEST|
		    CH7006_DETECT_CVBS_TEST)) == 0)
		priv->subconnector = DRM_MODE_SUBCONNECTOR_SCART;
	else if ((det & (CH7006_DETECT_SVIDEO_Y_TEST|
			 CH7006_DETECT_SVIDEO_C_TEST)) == 0)
		priv->subconnector = DRM_MODE_SUBCONNECTOR_SVIDEO;
	else if ((det & CH7006_DETECT_CVBS_TEST) == 0)
		priv->subconnector = DRM_MODE_SUBCONNECTOR_Composite;
	else
		priv->subconnector = DRM_MODE_SUBCONNECTOR_Unknown;

	drm_object_property_set_value(&connector->base,
			encoder->dev->mode_config.tv_subconnector_property,
							priv->subconnector);

	return priv->subconnector ? connector_status_connected :
					connector_status_disconnected;
}

static int ch7006_encoder_get_modes(struct drm_encoder *encoder,
				    struct drm_connector *connector)
{
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	const struct ch7006_mode *mode;
	int n = 0;

	for (mode = ch7006_modes; mode->mode.clock; mode++) {
		if (~mode->valid_scales & 1<<priv->scale ||
		    ~mode->valid_norms & 1<<priv->norm)
			continue;

		drm_mode_probed_add(connector,
				drm_mode_duplicate(encoder->dev, &mode->mode));

		n++;
	}

	return n;
}

static int ch7006_encoder_create_resources(struct drm_encoder *encoder,
					   struct drm_connector *connector)
{
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_mode_config *conf = &dev->mode_config;

	drm_mode_create_tv_properties(dev, NUM_TV_NORMS, ch7006_tv_norm_names);

	priv->scale_property = drm_property_create_range(dev, 0, "scale", 0, 2);
	if (!priv->scale_property)
		return -ENOMEM;

	drm_object_attach_property(&connector->base, conf->tv_select_subconnector_property,
				      priv->select_subconnector);
	drm_object_attach_property(&connector->base, conf->tv_subconnector_property,
				      priv->subconnector);
	drm_object_attach_property(&connector->base, conf->tv_left_margin_property,
				      priv->hmargin);
	drm_object_attach_property(&connector->base, conf->tv_bottom_margin_property,
				      priv->vmargin);
	drm_object_attach_property(&connector->base, conf->tv_mode_property,
				      priv->norm);
	drm_object_attach_property(&connector->base, conf->tv_brightness_property,
				      priv->brightness);
	drm_object_attach_property(&connector->base, conf->tv_contrast_property,
				      priv->contrast);
	drm_object_attach_property(&connector->base, conf->tv_flicker_reduction_property,
				      priv->flicker);
	drm_object_attach_property(&connector->base, priv->scale_property,
				      priv->scale);

	return 0;
}

static int ch7006_encoder_set_property(struct drm_encoder *encoder,
				       struct drm_connector *connector,
				       struct drm_property *property,
				       uint64_t val)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	struct ch7006_state *state = &priv->state;
	struct drm_mode_config *conf = &encoder->dev->mode_config;
	struct drm_crtc *crtc = encoder->crtc;
	bool modes_changed = false;

	ch7006_dbg(client, "\n");

	if (property == conf->tv_select_subconnector_property) {
		priv->select_subconnector = val;

		ch7006_setup_power_state(encoder);

		ch7006_load_reg(client, state, CH7006_POWER);

	} else if (property == conf->tv_left_margin_property) {
		priv->hmargin = val;

		ch7006_setup_properties(encoder);

		ch7006_load_reg(client, state, CH7006_POV);
		ch7006_load_reg(client, state, CH7006_HPOS);

	} else if (property == conf->tv_bottom_margin_property) {
		priv->vmargin = val;

		ch7006_setup_properties(encoder);

		ch7006_load_reg(client, state, CH7006_POV);
		ch7006_load_reg(client, state, CH7006_VPOS);

	} else if (property == conf->tv_mode_property) {
		if (connector->dpms != DRM_MODE_DPMS_OFF)
			return -EINVAL;

		priv->norm = val;

		modes_changed = true;

	} else if (property == conf->tv_brightness_property) {
		priv->brightness = val;

		ch7006_setup_levels(encoder);

		ch7006_load_reg(client, state, CH7006_BLACK_LEVEL);

	} else if (property == conf->tv_contrast_property) {
		priv->contrast = val;

		ch7006_setup_properties(encoder);

		ch7006_load_reg(client, state, CH7006_CONTRAST);

	} else if (property == conf->tv_flicker_reduction_property) {
		priv->flicker = val;

		ch7006_setup_properties(encoder);

		ch7006_load_reg(client, state, CH7006_FFILTER);

	} else if (property == priv->scale_property) {
		if (connector->dpms != DRM_MODE_DPMS_OFF)
			return -EINVAL;

		priv->scale = val;

		modes_changed = true;

	} else {
		return -EINVAL;
	}

	if (modes_changed) {
		drm_helper_probe_single_connector_modes(connector, 0, 0);

		if (crtc)
			drm_crtc_helper_set_mode(crtc, &crtc->mode,
						 crtc->x, crtc->y,
						 crtc->primary->fb);
	}

	return 0;
}

static const struct drm_encoder_slave_funcs ch7006_encoder_funcs = {
	.set_config = ch7006_encoder_set_config,
	.destroy = ch7006_encoder_destroy,
	.dpms = ch7006_encoder_dpms,
	.save = ch7006_encoder_save,
	.restore = ch7006_encoder_restore,
	.mode_fixup = ch7006_encoder_mode_fixup,
	.mode_valid = ch7006_encoder_mode_valid,
	.mode_set = ch7006_encoder_mode_set,
	.detect = ch7006_encoder_detect,
	.get_modes = ch7006_encoder_get_modes,
	.create_resources = ch7006_encoder_create_resources,
	.set_property = ch7006_encoder_set_property,
};


/* I2C driver functions */

static int ch7006_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	uint8_t addr = CH7006_VERSION_ID;
	uint8_t val;
	int ret;

	ch7006_dbg(client, "\n");

	ret = i2c_master_send(client, &addr, sizeof(addr));
	if (ret < 0)
		goto fail;

	ret = i2c_master_recv(client, &val, sizeof(val));
	if (ret < 0)
		goto fail;

	ch7006_info(client, "Detected version ID: %x\n", val);

	/* I don't know what this is for, but otherwise I get no
	 * signal.
	 */
	ch7006_write(client, 0x3d, 0x0);

	return 0;

fail:
	ch7006_err(client, "Error %d reading version ID\n", ret);

	return -ENODEV;
}

static int ch7006_remove(struct i2c_client *client)
{
	ch7006_dbg(client, "\n");

	return 0;
}

static int ch7006_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	ch7006_dbg(client, "\n");

	ch7006_write(client, 0x3d, 0x0);

	return 0;
}

static int ch7006_encoder_init(struct i2c_client *client,
			       struct drm_device *dev,
			       struct drm_encoder_slave *encoder)
{
	struct ch7006_priv *priv;
	int i;

	ch7006_dbg(client, "\n");

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	encoder->slave_priv = priv;
	encoder->slave_funcs = &ch7006_encoder_funcs;

	priv->norm = TV_NORM_PAL;
	priv->select_subconnector = DRM_MODE_SUBCONNECTOR_Automatic;
	priv->subconnector = DRM_MODE_SUBCONNECTOR_Unknown;
	priv->scale = 1;
	priv->contrast = 50;
	priv->brightness = 50;
	priv->flicker = 50;
	priv->hmargin = 50;
	priv->vmargin = 50;
	priv->last_dpms = -1;
	priv->chip_version = ch7006_read(client, CH7006_VERSION_ID);

	if (ch7006_tv_norm) {
		for (i = 0; i < NUM_TV_NORMS; i++) {
			if (!strcmp(ch7006_tv_norm_names[i], ch7006_tv_norm)) {
				priv->norm = i;
				break;
			}
		}

		if (i == NUM_TV_NORMS)
			ch7006_err(client, "Invalid TV norm setting \"%s\".\n",
				   ch7006_tv_norm);
	}

	if (ch7006_scale >= 0 && ch7006_scale <= 2)
		priv->scale = ch7006_scale;
	else
		ch7006_err(client, "Invalid scale setting \"%d\".\n",
			   ch7006_scale);

	return 0;
}

static const struct i2c_device_id ch7006_ids[] = {
	{ "ch7006", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ch7006_ids);

static const struct dev_pm_ops ch7006_pm_ops = {
	.resume = ch7006_resume,
};

static struct drm_i2c_encoder_driver ch7006_driver = {
	.i2c_driver = {
		.probe = ch7006_probe,
		.remove = ch7006_remove,

		.driver = {
			.name = "ch7006",
			.pm = &ch7006_pm_ops,
		},

		.id_table = ch7006_ids,
	},

	.encoder_init = ch7006_encoder_init,
};


/* Module initialization */

static int __init ch7006_init(void)
{
	return drm_i2c_encoder_register(THIS_MODULE, &ch7006_driver);
}

static void __exit ch7006_exit(void)
{
	drm_i2c_encoder_unregister(&ch7006_driver);
}

int ch7006_debug;
module_param_named(debug, ch7006_debug, int, 0600);
MODULE_PARM_DESC(debug, "Enable debug output.");

char *ch7006_tv_norm;
module_param_named(tv_norm, ch7006_tv_norm, charp, 0600);
MODULE_PARM_DESC(tv_norm, "Default TV norm.\n"
		 "\t\tSupported: PAL, PAL-M, PAL-N, PAL-Nc, PAL-60, NTSC-M, NTSC-J.\n"
		 "\t\tDefault: PAL");

int ch7006_scale = 1;
module_param_named(scale, ch7006_scale, int, 0600);
MODULE_PARM_DESC(scale, "Default scale.\n"
		 "\t\tSupported: 0 -> Select video modes with a higher blanking ratio.\n"
		 "\t\t\t1 -> Select default video modes.\n"
		 "\t\t\t2 -> Select video modes with a lower blanking ratio.");

MODULE_AUTHOR("Francisco Jerez <currojerez@riseup.net>");
MODULE_DESCRIPTION("Chrontel ch7006 TV encoder driver");
MODULE_LICENSE("GPL and additional rights");

module_init(ch7006_init);
module_exit(ch7006_exit);
