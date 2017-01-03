#ifndef __ROCKCHIP_DP_H__
#define __ROCKCHIP_DP_H__

#include "../hdmi/rockchip-hdmi.h"
#include "rockchip_dp_core.h"

int cdn_dp_get_edid(void *dp, u8 *edid, unsigned int block);
int cdn_dp_encoder_mode_set(void *dp, struct dp_disp_info *disp_info);
int cdn_dp_encoder_enable(void *dp);
int cdn_dp_connector_detect(void *dp);
int cdn_dp_encoder_disable(void *dp);
int cdn_dp_audio_hw_params(void *dp);
int cdn_dp_audio_digital_mute(void *dp, bool enable);
int cdn_dp_fb_resume(void *dp_dev);
int cdn_dp_fb_suspend(void *dp_dev);

#endif
