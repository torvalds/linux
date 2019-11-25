// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/delay.h>

#include "hdmi.h"

static void hdmi_phy_8x60_powerup(struct hdmi_phy *phy,
		unsigned long int pixclock)
{
	/* De-serializer delay D/C for non-lbk mode: */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG0,
		       HDMI_8x60_PHY_REG0_DESER_DEL_CTRL(3));

	if (pixclock == 27000000) {
		/* video_format == HDMI_VFRMT_720x480p60_16_9 */
		hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG1,
			       HDMI_8x60_PHY_REG1_DTEST_MUX_SEL(5) |
			       HDMI_8x60_PHY_REG1_OUTVOL_SWING_CTRL(3));
	} else {
		hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG1,
			       HDMI_8x60_PHY_REG1_DTEST_MUX_SEL(5) |
			       HDMI_8x60_PHY_REG1_OUTVOL_SWING_CTRL(4));
	}

	/* No matter what, start from the power down mode: */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG2,
		       HDMI_8x60_PHY_REG2_PD_PWRGEN |
		       HDMI_8x60_PHY_REG2_PD_PLL |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_4 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_3 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_2 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_1 |
		       HDMI_8x60_PHY_REG2_PD_DESER);

	/* Turn PowerGen on: */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG2,
		       HDMI_8x60_PHY_REG2_PD_PLL |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_4 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_3 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_2 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_1 |
		       HDMI_8x60_PHY_REG2_PD_DESER);

	/* Turn PLL power on: */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG2,
		       HDMI_8x60_PHY_REG2_PD_DRIVE_4 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_3 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_2 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_1 |
		       HDMI_8x60_PHY_REG2_PD_DESER);

	/* Write to HIGH after PLL power down de-assert: */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG3,
		       HDMI_8x60_PHY_REG3_PLL_ENABLE);

	/* ASIC power on; PHY REG9 = 0 */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG9, 0);

	/* Enable PLL lock detect, PLL lock det will go high after lock
	 * Enable the re-time logic
	 */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG12,
		       HDMI_8x60_PHY_REG12_RETIMING_EN |
		       HDMI_8x60_PHY_REG12_PLL_LOCK_DETECT_EN);

	/* Drivers are on: */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG2,
		       HDMI_8x60_PHY_REG2_PD_DESER);

	/* If the RX detector is needed: */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG2,
		       HDMI_8x60_PHY_REG2_RCV_SENSE_EN |
		       HDMI_8x60_PHY_REG2_PD_DESER);

	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG4, 0);
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG5, 0);
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG6, 0);
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG7, 0);
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG8, 0);
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG9, 0);
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG10, 0);
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG11, 0);

	/* If we want to use lock enable based on counting: */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG12,
		       HDMI_8x60_PHY_REG12_RETIMING_EN |
		       HDMI_8x60_PHY_REG12_PLL_LOCK_DETECT_EN |
		       HDMI_8x60_PHY_REG12_FORCE_LOCK);
}

static void hdmi_phy_8x60_powerdown(struct hdmi_phy *phy)
{
	/* Assert RESET PHY from controller */
	hdmi_phy_write(phy, REG_HDMI_PHY_CTRL,
		       HDMI_PHY_CTRL_SW_RESET);
	udelay(10);
	/* De-assert RESET PHY from controller */
	hdmi_phy_write(phy, REG_HDMI_PHY_CTRL, 0);
	/* Turn off Driver */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG2,
		       HDMI_8x60_PHY_REG2_PD_DRIVE_4 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_3 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_2 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_1 |
		       HDMI_8x60_PHY_REG2_PD_DESER);
	udelay(10);
	/* Disable PLL */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG3, 0);
	/* Power down PHY, but keep RX-sense: */
	hdmi_phy_write(phy, REG_HDMI_8x60_PHY_REG2,
		       HDMI_8x60_PHY_REG2_RCV_SENSE_EN |
		       HDMI_8x60_PHY_REG2_PD_PWRGEN |
		       HDMI_8x60_PHY_REG2_PD_PLL |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_4 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_3 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_2 |
		       HDMI_8x60_PHY_REG2_PD_DRIVE_1 |
		       HDMI_8x60_PHY_REG2_PD_DESER);
}

const struct hdmi_phy_cfg msm_hdmi_phy_8x60_cfg = {
	.type = MSM_HDMI_PHY_8x60,
	.powerup = hdmi_phy_8x60_powerup,
	.powerdown = hdmi_phy_8x60_powerdown,
};
