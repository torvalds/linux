/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_CATALOG_H_
#define _DP_CATALOG_H_

#include <drm/drm_modes.h>

#include "dp_parser.h"
#include "disp/msm_disp_snapshot.h"

/* interrupts */
#define DP_INTR_HPD		BIT(0)
#define DP_INTR_AUX_I2C_DONE	BIT(3)
#define DP_INTR_WRONG_ADDR	BIT(6)
#define DP_INTR_TIMEOUT		BIT(9)
#define DP_INTR_NACK_DEFER	BIT(12)
#define DP_INTR_WRONG_DATA_CNT	BIT(15)
#define DP_INTR_I2C_NACK	BIT(18)
#define DP_INTR_I2C_DEFER	BIT(21)
#define DP_INTR_PLL_UNLOCKED	BIT(24)
#define DP_INTR_AUX_ERROR	BIT(27)

#define DP_INTR_READY_FOR_VIDEO		BIT(0)
#define DP_INTR_IDLE_PATTERN_SENT	BIT(3)
#define DP_INTR_FRAME_END		BIT(6)
#define DP_INTR_CRC_UPDATED		BIT(9)

#define DP_AUX_CFG_MAX_VALUE_CNT 3

/* PHY AUX config registers */
enum dp_phy_aux_config_type {
	PHY_AUX_CFG0,
	PHY_AUX_CFG1,
	PHY_AUX_CFG2,
	PHY_AUX_CFG3,
	PHY_AUX_CFG4,
	PHY_AUX_CFG5,
	PHY_AUX_CFG6,
	PHY_AUX_CFG7,
	PHY_AUX_CFG8,
	PHY_AUX_CFG9,
	PHY_AUX_CFG_MAX,
};

enum dp_catalog_audio_sdp_type {
	DP_AUDIO_SDP_STREAM,
	DP_AUDIO_SDP_TIMESTAMP,
	DP_AUDIO_SDP_INFOFRAME,
	DP_AUDIO_SDP_COPYMANAGEMENT,
	DP_AUDIO_SDP_ISRC,
	DP_AUDIO_SDP_MAX,
};

enum dp_catalog_audio_header_type {
	DP_AUDIO_SDP_HEADER_1,
	DP_AUDIO_SDP_HEADER_2,
	DP_AUDIO_SDP_HEADER_3,
	DP_AUDIO_SDP_HEADER_MAX,
};

struct dp_catalog {
	u32 aux_data;
	u32 total;
	u32 sync_start;
	u32 width_blanking;
	u32 dp_active;
	enum dp_catalog_audio_sdp_type sdp_type;
	enum dp_catalog_audio_header_type sdp_header;
	u32 audio_data;
};

/* Debug module */
void dp_catalog_snapshot(struct dp_catalog *dp_catalog, struct msm_disp_state *disp_state);

/* AUX APIs */
u32 dp_catalog_aux_read_data(struct dp_catalog *dp_catalog);
int dp_catalog_aux_write_data(struct dp_catalog *dp_catalog);
int dp_catalog_aux_write_trans(struct dp_catalog *dp_catalog);
int dp_catalog_aux_clear_trans(struct dp_catalog *dp_catalog, bool read);
int dp_catalog_aux_clear_hw_interrupts(struct dp_catalog *dp_catalog);
void dp_catalog_aux_reset(struct dp_catalog *dp_catalog);
void dp_catalog_aux_enable(struct dp_catalog *dp_catalog, bool enable);
void dp_catalog_aux_update_cfg(struct dp_catalog *dp_catalog);
u32 dp_catalog_aux_get_irq(struct dp_catalog *dp_catalog);

/* DP Controller APIs */
void dp_catalog_ctrl_state_ctrl(struct dp_catalog *dp_catalog, u32 state);
void dp_catalog_ctrl_config_ctrl(struct dp_catalog *dp_catalog, u32 config);
void dp_catalog_ctrl_lane_mapping(struct dp_catalog *dp_catalog);
void dp_catalog_ctrl_mainlink_ctrl(struct dp_catalog *dp_catalog, bool enable);
void dp_catalog_ctrl_config_misc(struct dp_catalog *dp_catalog, u32 cc, u32 tb);
void dp_catalog_ctrl_config_msa(struct dp_catalog *dp_catalog, u32 rate,
				u32 stream_rate_khz, bool fixed_nvid);
int dp_catalog_ctrl_set_pattern(struct dp_catalog *dp_catalog, u32 pattern);
void dp_catalog_ctrl_reset(struct dp_catalog *dp_catalog);
bool dp_catalog_ctrl_mainlink_ready(struct dp_catalog *dp_catalog);
void dp_catalog_ctrl_enable_irq(struct dp_catalog *dp_catalog, bool enable);
void dp_catalog_hpd_config_intr(struct dp_catalog *dp_catalog,
			u32 intr_mask, bool en);
void dp_catalog_ctrl_hpd_config(struct dp_catalog *dp_catalog);
u32 dp_catalog_link_is_connected(struct dp_catalog *dp_catalog);
u32 dp_catalog_hpd_get_intr_status(struct dp_catalog *dp_catalog);
void dp_catalog_ctrl_phy_reset(struct dp_catalog *dp_catalog);
int dp_catalog_ctrl_update_vx_px(struct dp_catalog *dp_catalog, u8 v_level,
				u8 p_level);
int dp_catalog_ctrl_get_interrupt(struct dp_catalog *dp_catalog);
void dp_catalog_ctrl_update_transfer_unit(struct dp_catalog *dp_catalog,
				u32 dp_tu, u32 valid_boundary,
				u32 valid_boundary2);
void dp_catalog_ctrl_send_phy_pattern(struct dp_catalog *dp_catalog,
				u32 pattern);
u32 dp_catalog_ctrl_read_phy_pattern(struct dp_catalog *dp_catalog);

/* DP Panel APIs */
int dp_catalog_panel_timing_cfg(struct dp_catalog *dp_catalog);
void dp_catalog_dump_regs(struct dp_catalog *dp_catalog);
void dp_catalog_panel_tpg_enable(struct dp_catalog *dp_catalog,
				struct drm_display_mode *drm_mode);
void dp_catalog_panel_tpg_disable(struct dp_catalog *dp_catalog);

struct dp_catalog *dp_catalog_get(struct device *dev, struct dp_io *io);

/* DP Audio APIs */
void dp_catalog_audio_get_header(struct dp_catalog *catalog);
void dp_catalog_audio_set_header(struct dp_catalog *catalog);
void dp_catalog_audio_config_acr(struct dp_catalog *catalog);
void dp_catalog_audio_enable(struct dp_catalog *catalog);
void dp_catalog_audio_config_sdp(struct dp_catalog *catalog);
void dp_catalog_audio_init(struct dp_catalog *catalog);
void dp_catalog_audio_sfe_level(struct dp_catalog *catalog);

#endif /* _DP_CATALOG_H_ */
