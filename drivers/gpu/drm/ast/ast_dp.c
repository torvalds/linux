// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, ASPEED Technology Inc.
// Authors: KuoHsiang Chou <kuohsiang_chou@aspeedtech.com>

#include <linux/firmware.h>
#include <linux/delay.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "ast_drv.h"

static bool ast_astdp_is_connected(struct ast_device *ast)
{
	if (!ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xDF, AST_IO_VGACRDF_HPD))
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

static bool ast_dp_power_is_on(struct ast_device *ast)
{
	u8 vgacre3;

	vgacre3 = ast_get_index_reg(ast, AST_IO_VGACRI, 0xe3);

	return !(vgacre3 & AST_DP_PHY_SLEEP);
}

static void ast_dp_power_on_off(struct drm_device *dev, bool on)
{
	struct ast_device *ast = to_ast_device(dev);
	// Read and Turn off DP PHY sleep
	u8 bE3 = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xE3, AST_DP_VIDEO_ENABLE);

	// Turn on DP PHY sleep
	if (!on)
		bE3 |= AST_DP_PHY_SLEEP;

	// DP Power on/off
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xE3, (u8) ~AST_DP_PHY_SLEEP, bE3);

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

static void ast_dp_set_on_off(struct drm_device *dev, bool on)
{
	struct ast_device *ast = to_ast_device(dev);
	u8 video_on_off = on;
	u32 i = 0;

	// Video On/Off
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xE3, (u8) ~AST_DP_VIDEO_ENABLE, on);

	video_on_off <<= 4;
	while (ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xDF,
						ASTDP_MIRROR_VIDEO_ENABLE) != video_on_off) {
		// wait 1 ms
		mdelay(1);
		if (++i > 200)
			break;
	}
}

static void ast_dp_set_mode(struct drm_crtc *crtc, struct ast_vbios_mode_info *vbios_mode)
{
	struct ast_device *ast = to_ast_device(crtc->dev);

	u32 ulRefreshRateIndex;
	u8 ModeIdx;

	ulRefreshRateIndex = vbios_mode->enh_table->refresh_rate_index - 1;

	switch (crtc->mode.crtc_hdisplay) {
	case 320:
		ModeIdx = ASTDP_320x240_60;
		break;
	case 400:
		ModeIdx = ASTDP_400x300_60;
		break;
	case 512:
		ModeIdx = ASTDP_512x384_60;
		break;
	case 640:
		ModeIdx = (ASTDP_640x480_60 + (u8) ulRefreshRateIndex);
		break;
	case 800:
		ModeIdx = (ASTDP_800x600_56 + (u8) ulRefreshRateIndex);
		break;
	case 1024:
		ModeIdx = (ASTDP_1024x768_60 + (u8) ulRefreshRateIndex);
		break;
	case 1152:
		ModeIdx = ASTDP_1152x864_75;
		break;
	case 1280:
		if (crtc->mode.crtc_vdisplay == 800)
			ModeIdx = (ASTDP_1280x800_60_RB - (u8) ulRefreshRateIndex);
		else		// 1024
			ModeIdx = (ASTDP_1280x1024_60 + (u8) ulRefreshRateIndex);
		break;
	case 1360:
	case 1366:
		ModeIdx = ASTDP_1366x768_60;
		break;
	case 1440:
		ModeIdx = (ASTDP_1440x900_60_RB - (u8) ulRefreshRateIndex);
		break;
	case 1600:
		if (crtc->mode.crtc_vdisplay == 900)
			ModeIdx = (ASTDP_1600x900_60_RB - (u8) ulRefreshRateIndex);
		else		//1200
			ModeIdx = ASTDP_1600x1200_60;
		break;
	case 1680:
		ModeIdx = (ASTDP_1680x1050_60_RB - (u8) ulRefreshRateIndex);
		break;
	case 1920:
		if (crtc->mode.crtc_vdisplay == 1080)
			ModeIdx = ASTDP_1920x1080_60;
		else		//1200
			ModeIdx = ASTDP_1920x1200_60;
		break;
	default:
		return;
	}

	/*
	 * CRE0[7:0]: MISC0 ((0x00: 18-bpp) or (0x20: 24-bpp)
	 * CRE1[7:0]: MISC1 (default: 0x00)
	 * CRE2[7:0]: video format index (0x00 ~ 0x20 or 0x40 ~ 0x50)
	 */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xE0, ASTDP_AND_CLEAR_MASK,
			       ASTDP_MISC0_24bpp);
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xE1, ASTDP_AND_CLEAR_MASK, ASTDP_MISC1);
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xE2, ASTDP_AND_CLEAR_MASK, ModeIdx);
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

static void ast_astdp_encoder_helper_atomic_mode_set(struct drm_encoder *encoder,
						     struct drm_crtc_state *crtc_state,
						     struct drm_connector_state *conn_state)
{
	struct drm_crtc *crtc = crtc_state->crtc;
	struct ast_crtc_state *ast_crtc_state = to_ast_crtc_state(crtc_state);
	struct ast_vbios_mode_info *vbios_mode_info = &ast_crtc_state->vbios_mode_info;

	ast_dp_set_mode(crtc, vbios_mode_info);
}

static void ast_astdp_encoder_helper_atomic_enable(struct drm_encoder *encoder,
						   struct drm_atomic_state *state)
{
	struct drm_device *dev = encoder->dev;
	struct ast_device *ast = to_ast_device(dev);
	struct ast_connector *ast_connector = &ast->output.astdp.connector;

	if (ast_connector->physical_status == connector_status_connected) {
		ast_dp_power_on_off(dev, AST_DP_POWER_ON);
		ast_dp_link_training(ast);

		ast_wait_for_vretrace(ast);
		ast_dp_set_on_off(dev, 1);
	}
}

static void ast_astdp_encoder_helper_atomic_disable(struct drm_encoder *encoder,
						    struct drm_atomic_state *state)
{
	struct drm_device *dev = encoder->dev;

	ast_dp_set_on_off(dev, 0);
	ast_dp_power_on_off(dev, AST_DP_POWER_OFF);
}

static const struct drm_encoder_helper_funcs ast_astdp_encoder_helper_funcs = {
	.atomic_mode_set = ast_astdp_encoder_helper_atomic_mode_set,
	.atomic_enable = ast_astdp_encoder_helper_atomic_enable,
	.atomic_disable = ast_astdp_encoder_helper_atomic_disable,
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
	struct drm_device *dev = connector->dev;
	struct ast_device *ast = to_ast_device(connector->dev);
	enum drm_connector_status status = connector_status_disconnected;
	bool power_is_on;

	mutex_lock(&ast->modeset_lock);

	power_is_on = ast_dp_power_is_on(ast);
	if (!power_is_on)
		ast_dp_power_on_off(dev, true);

	if (ast_astdp_is_connected(ast))
		status = connector_status_connected;

	if (!power_is_on && status == connector_status_disconnected)
		ast_dp_power_on_off(dev, false);

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

static const struct drm_connector_funcs ast_astdp_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int ast_astdp_connector_init(struct drm_device *dev, struct drm_connector *connector)
{
	int ret;

	ret = drm_connector_init(dev, connector, &ast_astdp_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret)
		return ret;

	drm_connector_helper_add(connector, &ast_astdp_connector_helper_funcs);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	return 0;
}

int ast_astdp_output_init(struct ast_device *ast)
{
	struct drm_device *dev = &ast->base;
	struct drm_crtc *crtc = &ast->crtc;
	struct drm_encoder *encoder = &ast->output.astdp.encoder;
	struct ast_connector *ast_connector = &ast->output.astdp.connector;
	struct drm_connector *connector = &ast_connector->base;
	int ret;

	ret = drm_encoder_init(dev, encoder, &ast_astdp_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return ret;
	drm_encoder_helper_add(encoder, &ast_astdp_encoder_helper_funcs);

	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = ast_astdp_connector_init(dev, connector);
	if (ret)
		return ret;
	ast_connector->physical_status = connector->status;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	return 0;
}
