// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, ASPEED Technology Inc.
// Authors: KuoHsiang Chou <kuohsiang_chou@aspeedtech.com>

#include <linux/firmware.h>
#include <linux/delay.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "ast_drv.h"
#include "ast_vbios.h"

struct ast_astdp_mode_index_table_entry {
	unsigned int hdisplay;
	unsigned int vdisplay;
	unsigned int mode_index;
};

/* FIXME: Do refresh rate and flags actually matter? */
static const struct ast_astdp_mode_index_table_entry ast_astdp_mode_index_table[] = {
	{  320,  240, ASTDP_320x240_60 },
	{  400,  300, ASTDP_400x300_60 },
	{  512,  384, ASTDP_512x384_60 },
	{  640,  480, ASTDP_640x480_60 },
	{  800,  600, ASTDP_800x600_56 },
	{ 1024,  768, ASTDP_1024x768_60 },
	{ 1152,  864, ASTDP_1152x864_75 },
	{ 1280,  800, ASTDP_1280x800_60_RB },
	{ 1280, 1024, ASTDP_1280x1024_60 },
	{ 1360,  768, ASTDP_1366x768_60 }, // same as 1366x786
	{ 1366,  768, ASTDP_1366x768_60 },
	{ 1440,  900, ASTDP_1440x900_60_RB },
	{ 1600,  900, ASTDP_1600x900_60_RB },
	{ 1600, 1200, ASTDP_1600x1200_60 },
	{ 1680, 1050, ASTDP_1680x1050_60_RB },
	{ 1920, 1080, ASTDP_1920x1080_60 },
	{ 1920, 1200, ASTDP_1920x1200_60 },
	{ 0 }
};

struct ast_astdp_connector_state {
	struct drm_connector_state base;

	int mode_index;
};

static struct ast_astdp_connector_state *
to_ast_astdp_connector_state(const struct drm_connector_state *state)
{
	return container_of(state, struct ast_astdp_connector_state, base);
}

static int ast_astdp_get_mode_index(unsigned int hdisplay, unsigned int vdisplay)
{
	const struct ast_astdp_mode_index_table_entry *entry = ast_astdp_mode_index_table;

	while (entry->hdisplay && entry->vdisplay) {
		if (entry->hdisplay == hdisplay && entry->vdisplay == vdisplay)
			return entry->mode_index;
		++entry;
	}

	return -EINVAL;
}

static bool ast_astdp_is_connected(struct ast_device *ast)
{
	if (!ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xDF, AST_IO_VGACRDF_HPD))
		return false;
	/*
	 * HPD might be set even if no monitor is connected, so also check that
	 * the link training was successful.
	 */
	if (!ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xDC, AST_IO_VGACRDC_LINK_SUCCESS))
		return false;
	return true;
}

static int ast_astdp_read_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct ast_device *ast = data;
	size_t rdlen = round_up(len, 4);
	int ret = 0;
	unsigned int i;

	if (block > 0)
		return -EIO; /* extension headers not supported */

	/*
	 * Protect access to I/O registers from concurrent modesetting
	 * by acquiring the I/O-register lock.
	 */
	mutex_lock(&ast->modeset_lock);

	/* Start reading EDID data */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xe5, (u8)~AST_IO_VGACRE5_EDID_READ_DONE, 0x00);

	for (i = 0; i < rdlen; i += 4) {
		unsigned int offset;
		unsigned int j;
		u8 ediddata[4];
		u8 vgacre4;

		offset = (i + block * EDID_LENGTH) / 4;
		if (offset >= 64) {
			ret = -EIO;
			goto out;
		}
		vgacre4 = offset;

		/*
		 * CRE4[7:0]: Read-Pointer for EDID (Unit: 4bytes); valid range: 0~64
		 */
		ast_set_index_reg(ast, AST_IO_VGACRI, 0xe4, vgacre4);

		/*
		 * CRD7[b0]: valid flag for EDID
		 * CRD6[b0]: mirror read pointer for EDID
		 */
		for (j = 0; j < 200; ++j) {
			u8 vgacrd7, vgacrd6;

			/*
			 * Delay are getting longer with each retry.
			 *
			 * 1. No delay on first try
			 * 2. The Delays are often 2 loops when users request "Display Settings"
			 *	  of right-click of mouse.
			 * 3. The Delays are often longer a lot when system resume from S3/S4.
			 */
			if (j)
				mdelay(j + 1);

			/* Wait for EDID offset to show up in mirror register */
			vgacrd7 = ast_get_index_reg(ast, AST_IO_VGACRI, 0xd7);
			if (vgacrd7 & AST_IO_VGACRD7_EDID_VALID_FLAG) {
				vgacrd6 = ast_get_index_reg(ast, AST_IO_VGACRI, 0xd6);
				if (vgacrd6 == offset)
					break;
			}
		}
		if (j == 200) {
			ret = -EBUSY;
			goto out;
		}

		ediddata[0] = ast_get_index_reg(ast, AST_IO_VGACRI, 0xd8);
		ediddata[1] = ast_get_index_reg(ast, AST_IO_VGACRI, 0xd9);
		ediddata[2] = ast_get_index_reg(ast, AST_IO_VGACRI, 0xda);
		ediddata[3] = ast_get_index_reg(ast, AST_IO_VGACRI, 0xdb);

		if (i == 31) {
			/*
			 * For 128-bytes EDID_1.3,
			 * 1. Add the value of Bytes-126 to Bytes-127.
			 *		The Bytes-127 is Checksum. Sum of all 128bytes should
			 *		equal 0	(mod 256).
			 * 2. Modify Bytes-126 to be 0.
			 *		The Bytes-126 indicates the Number of extensions to
			 *		follow. 0 represents noextensions.
			 */
			ediddata[3] = ediddata[3] + ediddata[2];
			ediddata[2] = 0;
		}

		memcpy(buf, ediddata, min((len - i), 4));
		buf += 4;
	}

out:
	/* Signal end of reading */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xe5, (u8)~AST_IO_VGACRE5_EDID_READ_DONE,
			       AST_IO_VGACRE5_EDID_READ_DONE);

	mutex_unlock(&ast->modeset_lock);

	return ret;
}

/*
 * Launch Aspeed DP
 */
int ast_dp_launch(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	unsigned int i = 10;

	while (i) {
		u8 vgacrd1 = ast_get_index_reg(ast, AST_IO_VGACRI, 0xd1);

		if (vgacrd1 & AST_IO_VGACRD1_MCU_FW_EXECUTING)
			break;
		--i;
		msleep(100);
	}
	if (!i) {
		drm_err(dev, "Wait DPMCU executing timeout\n");
		return -ENODEV;
	}

	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xe5,
			       (u8) ~AST_IO_VGACRE5_EDID_READ_DONE,
			       AST_IO_VGACRE5_EDID_READ_DONE);

	return 0;
}

static bool ast_dp_get_phy_sleep(struct ast_device *ast)
{
	u8 vgacre3 = ast_get_index_reg(ast, AST_IO_VGACRI, 0xe3);

	return (vgacre3 & AST_IO_VGACRE3_DP_PHY_SLEEP);
}

static void ast_dp_set_phy_sleep(struct ast_device *ast, bool sleep)
{
	u8 vgacre3 = 0x00;

	if (sleep)
		vgacre3 |= AST_IO_VGACRE3_DP_PHY_SLEEP;

	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xe3, (u8)~AST_IO_VGACRE3_DP_PHY_SLEEP,
			       vgacre3);
	msleep(50);
}

static void ast_dp_link_training(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	int i;

	for (i = 0; i < 10; i++) {
		u8 vgacrdc;

		if (i)
			msleep(100);

		vgacrdc = ast_get_index_reg(ast, AST_IO_VGACRI, 0xdc);
		if (vgacrdc & AST_IO_VGACRDC_LINK_SUCCESS)
			return;
	}
	drm_err(dev, "Link training failed\n");
}

static bool __ast_dp_wait_enable(struct ast_device *ast, bool enabled)
{
	u8 vgacrdf_test = 0x00;
	u8 vgacrdf;
	unsigned int i;

	if (enabled)
		vgacrdf_test |= AST_IO_VGACRDF_DP_VIDEO_ENABLE;

	for (i = 0; i < 1000; ++i) {
		if (i)
			mdelay(1);
		vgacrdf = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xdf,
						 AST_IO_VGACRDF_DP_VIDEO_ENABLE);
		if (vgacrdf == vgacrdf_test)
			return true;
	}

	return false;
}

static void ast_dp_set_enable(struct ast_device *ast, bool enabled)
{
	struct drm_device *dev = &ast->base;
	u8 vgacre3 = 0x00;

	if (enabled)
		vgacre3 |= AST_IO_VGACRE3_DP_VIDEO_ENABLE;

	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xe3, (u8)~AST_IO_VGACRE3_DP_VIDEO_ENABLE,
			       vgacre3);

	drm_WARN_ON(dev, !__ast_dp_wait_enable(ast, enabled));
}

static void ast_wait_for_vretrace(struct ast_device *ast)
{
	unsigned long timeout = jiffies + HZ;
	u8 vgair1;

	do {
		vgair1 = ast_io_read8(ast, AST_IO_VGAIR1_R);
	} while (!(vgair1 & AST_IO_VGAIR1_VREFRESH) && time_before(jiffies, timeout));
}

/*
 * Encoder
 */

static const struct drm_encoder_funcs ast_astdp_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static enum drm_mode_status
ast_astdp_encoder_helper_mode_valid(struct drm_encoder *encoder,
				    const struct drm_display_mode *mode)
{
	int res;

	res = ast_astdp_get_mode_index(mode->hdisplay, mode->vdisplay);
	if (res < 0)
		return MODE_NOMODE;

	return MODE_OK;
}

static void ast_astdp_encoder_helper_atomic_mode_set(struct drm_encoder *encoder,
						     struct drm_crtc_state *crtc_state,
						     struct drm_connector_state *conn_state)
{
	struct drm_device *dev = encoder->dev;
	struct ast_device *ast = to_ast_device(dev);
	struct ast_crtc_state *ast_crtc_state = to_ast_crtc_state(crtc_state);
	const struct ast_vbios_enhtable *vmode = ast_crtc_state->vmode;
	struct ast_astdp_connector_state *astdp_conn_state =
		to_ast_astdp_connector_state(conn_state);
	int mode_index = astdp_conn_state->mode_index;
	u8 refresh_rate_index;
	u8 vgacre0, vgacre1, vgacre2;

	if (drm_WARN_ON(dev, vmode->refresh_rate_index < 1 || vmode->refresh_rate_index > 255))
		return;
	refresh_rate_index = vmode->refresh_rate_index - 1;

	/* FIXME: Why are we doing this? */
	switch (mode_index) {
	case ASTDP_1280x800_60_RB:
	case ASTDP_1440x900_60_RB:
	case ASTDP_1600x900_60_RB:
	case ASTDP_1680x1050_60_RB:
		mode_index = (u8)(mode_index - (u8)refresh_rate_index);
		break;
	default:
		mode_index = (u8)(mode_index + (u8)refresh_rate_index);
		break;
	}

	/*
	 * CRE0[7:0]: MISC0 ((0x00: 18-bpp) or (0x20: 24-bpp)
	 * CRE1[7:0]: MISC1 (default: 0x00)
	 * CRE2[7:0]: video format index (0x00 ~ 0x20 or 0x40 ~ 0x50)
	 */
	vgacre0 = AST_IO_VGACRE0_24BPP;
	vgacre1 = 0x00;
	vgacre2 = mode_index & 0xff;

	ast_set_index_reg(ast, AST_IO_VGACRI, 0xe0, vgacre0);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xe1, vgacre1);
	ast_set_index_reg(ast, AST_IO_VGACRI, 0xe2, vgacre2);
}

static void ast_astdp_encoder_helper_atomic_enable(struct drm_encoder *encoder,
						   struct drm_atomic_state *state)
{
	struct ast_device *ast = to_ast_device(encoder->dev);
	struct ast_connector *ast_connector = &ast->output.astdp.connector;

	if (ast_connector->physical_status == connector_status_connected) {
		ast_dp_set_phy_sleep(ast, false);
		ast_dp_link_training(ast);

		ast_wait_for_vretrace(ast);
		ast_dp_set_enable(ast, true);
	}
}

static void ast_astdp_encoder_helper_atomic_disable(struct drm_encoder *encoder,
						    struct drm_atomic_state *state)
{
	struct ast_device *ast = to_ast_device(encoder->dev);

	ast_dp_set_enable(ast, false);
	ast_dp_set_phy_sleep(ast, true);
}

static int ast_astdp_encoder_helper_atomic_check(struct drm_encoder *encoder,
						 struct drm_crtc_state *crtc_state,
						 struct drm_connector_state *conn_state)
{
	const struct drm_display_mode *mode = &crtc_state->mode;
	struct ast_astdp_connector_state *astdp_conn_state =
		to_ast_astdp_connector_state(conn_state);
	int res;

	if (drm_atomic_crtc_needs_modeset(crtc_state)) {
		res = ast_astdp_get_mode_index(mode->hdisplay, mode->vdisplay);
		if (res < 0)
			return res;
		astdp_conn_state->mode_index = res;
	}

	return 0;
}

static const struct drm_encoder_helper_funcs ast_astdp_encoder_helper_funcs = {
	.mode_valid = ast_astdp_encoder_helper_mode_valid,
	.atomic_mode_set = ast_astdp_encoder_helper_atomic_mode_set,
	.atomic_enable = ast_astdp_encoder_helper_atomic_enable,
	.atomic_disable = ast_astdp_encoder_helper_atomic_disable,
	.atomic_check = ast_astdp_encoder_helper_atomic_check,
};

/*
 * Connector
 */

static int ast_astdp_connector_helper_get_modes(struct drm_connector *connector)
{
	struct ast_connector *ast_connector = to_ast_connector(connector);
	int count;

	if (ast_connector->physical_status == connector_status_connected) {
		struct ast_device *ast = to_ast_device(connector->dev);
		const struct drm_edid *drm_edid;

		drm_edid = drm_edid_read_custom(connector, ast_astdp_read_edid_block, ast);
		drm_edid_connector_update(connector, drm_edid);
		count = drm_edid_connector_add_modes(connector);
		drm_edid_free(drm_edid);
	} else {
		drm_edid_connector_update(connector, NULL);

		/*
		 * There's no EDID data without a connected monitor. Set BMC-
		 * compatible modes in this case. The XGA default resolution
		 * should work well for all BMCs.
		 */
		count = drm_add_modes_noedid(connector, 4096, 4096);
		if (count)
			drm_set_preferred_mode(connector, 1024, 768);
	}

	return count;
}

static int ast_astdp_connector_helper_detect_ctx(struct drm_connector *connector,
						 struct drm_modeset_acquire_ctx *ctx,
						 bool force)
{
	struct ast_connector *ast_connector = to_ast_connector(connector);
	struct ast_device *ast = to_ast_device(connector->dev);
	enum drm_connector_status status = connector_status_disconnected;
	bool phy_sleep;

	mutex_lock(&ast->modeset_lock);

	phy_sleep = ast_dp_get_phy_sleep(ast);
	if (phy_sleep)
		ast_dp_set_phy_sleep(ast, false);

	if (ast_astdp_is_connected(ast))
		status = connector_status_connected;

	if (phy_sleep && status == connector_status_disconnected)
		ast_dp_set_phy_sleep(ast, true);

	mutex_unlock(&ast->modeset_lock);

	if (status != ast_connector->physical_status)
		++connector->epoch_counter;
	ast_connector->physical_status = status;

	return connector_status_connected;
}

static const struct drm_connector_helper_funcs ast_astdp_connector_helper_funcs = {
	.get_modes = ast_astdp_connector_helper_get_modes,
	.detect_ctx = ast_astdp_connector_helper_detect_ctx,
};

static void ast_astdp_connector_reset(struct drm_connector *connector)
{
	struct ast_astdp_connector_state *astdp_state =
		kzalloc(sizeof(*astdp_state), GFP_KERNEL);

	if (connector->state)
		connector->funcs->atomic_destroy_state(connector, connector->state);

	if (astdp_state)
		__drm_atomic_helper_connector_reset(connector, &astdp_state->base);
	else
		__drm_atomic_helper_connector_reset(connector, NULL);
}

static struct drm_connector_state *
ast_astdp_connector_atomic_duplicate_state(struct drm_connector *connector)
{
	struct ast_astdp_connector_state *new_astdp_state, *astdp_state;
	struct drm_device *dev = connector->dev;

	if (drm_WARN_ON(dev, !connector->state))
		return NULL;

	new_astdp_state = kmalloc(sizeof(*new_astdp_state), GFP_KERNEL);
	if (!new_astdp_state)
		return NULL;
	__drm_atomic_helper_connector_duplicate_state(connector, &new_astdp_state->base);

	astdp_state = to_ast_astdp_connector_state(connector->state);

	new_astdp_state->mode_index = astdp_state->mode_index;

	return &new_astdp_state->base;
}

static void ast_astdp_connector_atomic_destroy_state(struct drm_connector *connector,
						     struct drm_connector_state *state)
{
	struct ast_astdp_connector_state *astdp_state = to_ast_astdp_connector_state(state);

	__drm_atomic_helper_connector_destroy_state(&astdp_state->base);
	kfree(astdp_state);
}

static const struct drm_connector_funcs ast_astdp_connector_funcs = {
	.reset = ast_astdp_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = ast_astdp_connector_atomic_duplicate_state,
	.atomic_destroy_state = ast_astdp_connector_atomic_destroy_state,
};

/*
 * Output
 */

int ast_astdp_output_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct drm_crtc *crtc = &ast->crtc;
	struct drm_encoder *encoder;
	struct ast_connector *ast_connector;
	struct drm_connector *connector;
	int ret;

	/* encoder */

	encoder = &ast->output.astdp.encoder;
	ret = drm_encoder_init(dev, encoder, &ast_astdp_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return ret;
	drm_encoder_helper_add(encoder, &ast_astdp_encoder_helper_funcs);

	encoder->possible_crtcs = drm_crtc_mask(crtc);

	/* connector */

	ast_connector = &ast->output.astdp.connector;
	connector = &ast_connector->base;
	ret = drm_connector_init(dev, connector, &ast_astdp_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret)
		return ret;
	drm_connector_helper_add(connector, &ast_astdp_connector_helper_funcs);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;
	connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	ast_connector->physical_status = connector->status;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	return 0;
}
