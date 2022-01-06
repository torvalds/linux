// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, ASPEED Technology Inc.
// Authors: KuoHsiang Chou <kuohsiang_chou@aspeedtech.com>

#include <linux/firmware.h>
#include <linux/delay.h>
#include <drm/drm_print.h>
#include "ast_drv.h"

bool ast_dp_read_edid(struct drm_device *dev, u8 *ediddata)
{
	struct ast_private *ast = to_ast_private(dev);
	u8 i = 0, j = 0;

#ifdef DPControlPower
	u8 bDPState_Change = false;

	// Check DP power off or not.
	if (ast->ASTDP_State & 0x10) {
		// DP power on
		ast_dp_PowerOnOff(dev, 1);
		bDPState_Change = true;
	}
#endif

	if (!(ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD1, 0x20) &&
		ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDC, 0x01) &&
		ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDF, 0x01) &&
		ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE5, 0x01))) {
#ifdef DPControlPower
		// Set back power off
		if (bDPState_Change)
			ast_dp_PowerOnOff(dev, 0);
#endif
		return false;
	}

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE5, 0x00, 0x00);

	for (i = 0; i < 32; i++) {
		/*
		 * CRE4[7:0]: Read-Pointer for EDID (Unit: 4bytes); valid range: 0~64
		 */
		ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE4, 0x00, (u8) i);
		j = 0;

		/*
		 * CRD7[b0]: valid flag for EDID
		 * CRD6[b0]: mirror read pointer for EDID
		 */
		while ((ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD7, 0x01) != 0x01) ||
			(ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD6, 0xFF) != i)) {
			mdelay(j+1);

			if (!(ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD1, 0x20) &&
				ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDC, 0x01) &&
				ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDF, 0x01))) {
				ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE5, 0x00, 0x01);
				return false;
			}

			j++;
			if (j > 200) {
				ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE5, 0x00, 0x01);
				return false;
			}
		}

		*(ediddata) = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD8, 0xFF);
		*(ediddata + 1) = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD9, 0xFF);
		*(ediddata + 2) = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDA, 0xFF);
		*(ediddata + 3) = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDB, 0xFF);

		if (i == 31) {
			*(ediddata + 3) = *(ediddata + 3) + *(ediddata + 2);
			*(ediddata + 2) = 0;
		}

		ediddata += 4;
	}

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE5, 0x00, 0x01);

#ifdef DPControlPower
	// Set back power off
	if (bDPState_Change)
		ast_dp_PowerOnOff(dev, 0);
#endif

	return true;
}

/*
 * Launch Aspeed DP
 */
bool ast_dp_launch(struct drm_device *dev, u8 bPower)
{
	u32 WaitCount = 1;
	u8 bDPTX = 0;
	u8 bDPExecute = 1;

	struct ast_private *ast = to_ast_private(dev);
	// S3 come back, need more time to wait BMC ready.
	if (bPower)
		WaitCount = 300;

	// Fill
	ast->tx_chip_type = AST_TX_NONE;

	// Wait total count by different condition.
	// This is a temp solution for DP check
	bDPTX = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD1, 0x0E);

	// 0xE : ASTDP with DPMCU FW handling
	if (bDPTX == 0x0E) {

		if (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD1, 0x20) != 0x20) {
			// DP would not be ready.
			bDPExecute = 0;
		};

		if (bDPExecute)
			ast->tx_chip_type = AST_TX_ASTDP;
	}

	return true;
}

#ifdef DPControlPower

void ast_dp_PowerOnOff(struct drm_device *dev, u8 Mode)
{
	struct ast_private *ast = to_ast_private(dev);
	// Read and Turn off DP PHY sleep
	u8 bE3 = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE3, 0x0F);

	// Turn on DP PHY sleep
	if (!Mode)
		bE3 |= 0x10;

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE3, 0x00, bE3); // DP Power on/off

	// Save ASTDP power state
	ast->ASTDP_State = bE3;
}

#endif

void ast_dp_SetOnOff(struct drm_device *dev, u8 Mode)
{
	struct ast_private *ast = to_ast_private(dev);

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE3, 0x00, Mode); // video on/off

	// Save ASTDP power state
	ast->ASTDP_State = Mode;

    // If DP plug in and link successful then check video on / off status
	if (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDC, 0x01) &&
		ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDF, 0x01)) {
		Mode <<= 4;
		while (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDF, 0x10) != Mode) {
			// wait 1 ms
			mdelay(1);
		}
	}
}

void ast_dp_SetOutput(struct drm_crtc *crtc, struct ast_vbios_mode_info *vbios_mode)
{
	struct ast_private *ast = to_ast_private(crtc->dev);

	u32 ulRefreshRateIndex;
	u8 ModeIdx;

	ulRefreshRateIndex = vbios_mode->enh_table->refresh_rate_index - 1;

	switch (crtc->mode.crtc_hdisplay) {
	case 320:
		ModeIdx = 0x11;
		break;
	case 400:
		ModeIdx = 0x12;
		break;
	case 512:
		ModeIdx = 0x13;
		break;
	case 640:
		ModeIdx = (0x00 + (u8) ulRefreshRateIndex);
		break;
	case 800:
		ModeIdx = (0x04 + (u8) ulRefreshRateIndex);
		break;
	case 1024:
		ModeIdx = (0x09 + (u8) ulRefreshRateIndex);
		break;
	case 1152:
		ModeIdx = 0x1F;
		break;
	case 1280:
		if (crtc->mode.crtc_vdisplay == 800)
			ModeIdx = (0x17 - (u8) ulRefreshRateIndex);	// For RB/Non-RB
		else		// 1024
			ModeIdx = (0x0D + (u8) ulRefreshRateIndex);
		break;
	case 1360:
	case 1366:
		ModeIdx = 0x1E;
		break;
	case 1440:
		ModeIdx = (0x19 - (u8) ulRefreshRateIndex);	// For RB/Non-RB
		break;
	case 1600:
		if (crtc->mode.crtc_vdisplay == 900)
			ModeIdx = (0x1D - (u8) ulRefreshRateIndex);	// For RB/Non-RB
		else		//1200
			ModeIdx = 0x10;
		break;
	case 1680:
		ModeIdx = (0x1B - (u8) ulRefreshRateIndex);	// For RB/Non-RB
		break;
	case 1920:
		if (crtc->mode.crtc_vdisplay == 1080)
			ModeIdx = 0x15;
		else		//1200
			ModeIdx = 0x14;
		break;
	default:
		return;
	}

	/*
	 * CRE0[7:0]: MISC0 ((0x00: 18-bpp) or (0x20: 24-bpp)
	 * CRE1[7:0]: MISC1 (default: 0x00)
	 * CRE2[7:0]: video format index (0x00 ~ 0x20 or 0x40 ~ 0x50)
	 */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE0, 0x00, 0x20);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE1, 0x00, 0x00);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE2, 0x00, ModeIdx);
}
