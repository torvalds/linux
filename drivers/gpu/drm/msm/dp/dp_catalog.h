/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_CATALOG_H_
#define _DP_CATALOG_H_

#include <drm/drm_modes.h>

#include "dp_utils.h"
#include "disp/msm_disp_snapshot.h"

/* interrupts */
#define DP_INTR_HPD		BIT(0)
#define DP_INTR_AUX_XFER_DONE	BIT(3)
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

#define DP_HW_VERSION_1_0	0x10000000
#define DP_HW_VERSION_1_2	0x10020000

struct msm_dp_catalog {
	bool wide_bus_en;
};

/* Debug module */
void msm_dp_catalog_snapshot(struct msm_dp_catalog *msm_dp_catalog, struct msm_disp_state *disp_state);

/* AUX APIs */
u32 msm_dp_catalog_aux_read_data(struct msm_dp_catalog *msm_dp_catalog);
int msm_dp_catalog_aux_write_data(struct msm_dp_catalog *msm_dp_catalog, u32 data);
int msm_dp_catalog_aux_write_trans(struct msm_dp_catalog *msm_dp_catalog, u32 data);
int msm_dp_catalog_aux_clear_trans(struct msm_dp_catalog *msm_dp_catalog, bool read);
int msm_dp_catalog_aux_clear_hw_interrupts(struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_aux_reset(struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_aux_enable(struct msm_dp_catalog *msm_dp_catalog, bool enable);
int msm_dp_catalog_aux_wait_for_hpd_connect_state(struct msm_dp_catalog *msm_dp_catalog,
					      unsigned long wait_us);
u32 msm_dp_catalog_aux_get_irq(struct msm_dp_catalog *msm_dp_catalog);

/* DP Controller APIs */
void msm_dp_catalog_ctrl_state_ctrl(struct msm_dp_catalog *msm_dp_catalog, u32 state);
void msm_dp_catalog_ctrl_config_ctrl(struct msm_dp_catalog *msm_dp_catalog, u32 config);
void msm_dp_catalog_ctrl_lane_mapping(struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_ctrl_mainlink_ctrl(struct msm_dp_catalog *msm_dp_catalog, bool enable);
void msm_dp_catalog_ctrl_psr_mainlink_enable(struct msm_dp_catalog *msm_dp_catalog, bool enable);
void msm_dp_catalog_setup_peripheral_flush(struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_ctrl_config_misc(struct msm_dp_catalog *msm_dp_catalog, u32 cc, u32 tb);
void msm_dp_catalog_ctrl_config_msa(struct msm_dp_catalog *msm_dp_catalog, u32 rate,
				u32 stream_rate_khz, bool is_ycbcr_420);
int msm_dp_catalog_ctrl_set_pattern_state_bit(struct msm_dp_catalog *msm_dp_catalog, u32 pattern);
u32 msm_dp_catalog_hw_revision(const struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_ctrl_reset(struct msm_dp_catalog *msm_dp_catalog);
bool msm_dp_catalog_ctrl_mainlink_ready(struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_ctrl_enable_irq(struct msm_dp_catalog *msm_dp_catalog, bool enable);
void msm_dp_catalog_hpd_config_intr(struct msm_dp_catalog *msm_dp_catalog,
			u32 intr_mask, bool en);
void msm_dp_catalog_ctrl_hpd_enable(struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_ctrl_hpd_disable(struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_ctrl_config_psr(struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_ctrl_set_psr(struct msm_dp_catalog *msm_dp_catalog, bool enter);
u32 msm_dp_catalog_link_is_connected(struct msm_dp_catalog *msm_dp_catalog);
u32 msm_dp_catalog_hpd_get_intr_status(struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_ctrl_phy_reset(struct msm_dp_catalog *msm_dp_catalog);
int msm_dp_catalog_ctrl_get_interrupt(struct msm_dp_catalog *msm_dp_catalog);
u32 msm_dp_catalog_ctrl_read_psr_interrupt_status(struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_ctrl_update_transfer_unit(struct msm_dp_catalog *msm_dp_catalog,
				u32 msm_dp_tu, u32 valid_boundary,
				u32 valid_boundary2);
void msm_dp_catalog_ctrl_send_phy_pattern(struct msm_dp_catalog *msm_dp_catalog,
				u32 pattern);
u32 msm_dp_catalog_ctrl_read_phy_pattern(struct msm_dp_catalog *msm_dp_catalog);

/* DP Panel APIs */
int msm_dp_catalog_panel_timing_cfg(struct msm_dp_catalog *msm_dp_catalog, u32 total,
				u32 sync_start, u32 width_blanking, u32 msm_dp_active);
void msm_dp_catalog_panel_enable_vsc_sdp(struct msm_dp_catalog *msm_dp_catalog, struct dp_sdp *vsc_sdp);
void msm_dp_catalog_panel_disable_vsc_sdp(struct msm_dp_catalog *msm_dp_catalog);
void msm_dp_catalog_panel_tpg_enable(struct msm_dp_catalog *msm_dp_catalog,
				struct drm_display_mode *drm_mode);
void msm_dp_catalog_panel_tpg_disable(struct msm_dp_catalog *msm_dp_catalog);

struct msm_dp_catalog *msm_dp_catalog_get(struct device *dev);

/* DP Audio APIs */
void msm_dp_catalog_write_audio_stream(struct msm_dp_catalog *msm_dp_catalog,
				       struct dp_sdp_header *sdp_hdr);
void msm_dp_catalog_write_audio_timestamp(struct msm_dp_catalog *msm_dp_catalog,
					  struct dp_sdp_header *sdp_hdr);
void msm_dp_catalog_write_audio_infoframe(struct msm_dp_catalog *msm_dp_catalog,
					  struct dp_sdp_header *sdp_hdr);
void msm_dp_catalog_write_audio_copy_mgmt(struct msm_dp_catalog *msm_dp_catalog,
					  struct dp_sdp_header *sdp_hdr);
void msm_dp_catalog_write_audio_isrc(struct msm_dp_catalog *msm_dp_catalog,
				     struct dp_sdp_header *sdp_hdr);
void msm_dp_catalog_audio_config_acr(struct msm_dp_catalog *catalog, u32 select);
void msm_dp_catalog_audio_enable(struct msm_dp_catalog *catalog, bool enable);
void msm_dp_catalog_audio_config_sdp(struct msm_dp_catalog *catalog);
void msm_dp_catalog_audio_sfe_level(struct msm_dp_catalog *catalog, u32 safe_to_exit_level);

#endif /* _DP_CATALOG_H_ */
