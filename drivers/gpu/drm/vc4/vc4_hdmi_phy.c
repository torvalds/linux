// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Broadcom
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "vc4_hdmi.h"
#include "vc4_hdmi_regs.h"

void vc4_hdmi_phy_init(struct vc4_hdmi *vc4_hdmi, struct drm_display_mode *mode)
{
	/* PHY should be in reset, like
	 * vc4_hdmi_encoder_disable() does.
	 */

	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL, 0xf << 16);
	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL, 0);
}

void vc4_hdmi_phy_disable(struct vc4_hdmi *vc4_hdmi)
{
	HDMI_WRITE(HDMI_TX_PHY_RESET_CTL, 0xf << 16);
}
