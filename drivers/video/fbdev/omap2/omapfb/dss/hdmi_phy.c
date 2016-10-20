/*
 * HDMI PHY
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <video/omapfb_dss.h>

#include "dss.h"
#include "hdmi.h"

struct hdmi_phy_features {
	bool bist_ctrl;
	bool ldo_voltage;
	unsigned long max_phy;
};

static const struct hdmi_phy_features *phy_feat;

void hdmi_phy_dump(struct hdmi_phy_data *phy, struct seq_file *s)
{
#define DUMPPHY(r) seq_printf(s, "%-35s %08x\n", #r,\
		hdmi_read_reg(phy->base, r))

	DUMPPHY(HDMI_TXPHY_TX_CTRL);
	DUMPPHY(HDMI_TXPHY_DIGITAL_CTRL);
	DUMPPHY(HDMI_TXPHY_POWER_CTRL);
	DUMPPHY(HDMI_TXPHY_PAD_CFG_CTRL);
	if (phy_feat->bist_ctrl)
		DUMPPHY(HDMI_TXPHY_BIST_CONTROL);
}

int hdmi_phy_parse_lanes(struct hdmi_phy_data *phy, const u32 *lanes)
{
	int i;

	for (i = 0; i < 8; i += 2) {
		u8 lane, pol;
		int dx, dy;

		dx = lanes[i];
		dy = lanes[i + 1];

		if (dx < 0 || dx >= 8)
			return -EINVAL;

		if (dy < 0 || dy >= 8)
			return -EINVAL;

		if (dx & 1) {
			if (dy != dx - 1)
				return -EINVAL;
			pol = 1;
		} else {
			if (dy != dx + 1)
				return -EINVAL;
			pol = 0;
		}

		lane = dx / 2;

		phy->lane_function[lane] = i / 2;
		phy->lane_polarity[lane] = pol;
	}

	return 0;
}

static void hdmi_phy_configure_lanes(struct hdmi_phy_data *phy)
{
	static const u16 pad_cfg_list[] = {
		0x0123,
		0x0132,
		0x0312,
		0x0321,
		0x0231,
		0x0213,
		0x1023,
		0x1032,
		0x3012,
		0x3021,
		0x2031,
		0x2013,
		0x1203,
		0x1302,
		0x3102,
		0x3201,
		0x2301,
		0x2103,
		0x1230,
		0x1320,
		0x3120,
		0x3210,
		0x2310,
		0x2130,
	};

	u16 lane_cfg = 0;
	int i;
	unsigned lane_cfg_val;
	u16 pol_val = 0;

	for (i = 0; i < 4; ++i)
		lane_cfg |= phy->lane_function[i] << ((3 - i) * 4);

	pol_val |= phy->lane_polarity[0] << 0;
	pol_val |= phy->lane_polarity[1] << 3;
	pol_val |= phy->lane_polarity[2] << 2;
	pol_val |= phy->lane_polarity[3] << 1;

	for (i = 0; i < ARRAY_SIZE(pad_cfg_list); ++i)
		if (pad_cfg_list[i] == lane_cfg)
			break;

	if (WARN_ON(i == ARRAY_SIZE(pad_cfg_list)))
		i = 0;

	lane_cfg_val = i;

	REG_FLD_MOD(phy->base, HDMI_TXPHY_PAD_CFG_CTRL, lane_cfg_val, 26, 22);
	REG_FLD_MOD(phy->base, HDMI_TXPHY_PAD_CFG_CTRL, pol_val, 30, 27);
}

int hdmi_phy_configure(struct hdmi_phy_data *phy, unsigned long hfbitclk,
	unsigned long lfbitclk)
{
	u8 freqout;

	/*
	 * Read address 0 in order to get the SCP reset done completed
	 * Dummy access performed to make sure reset is done
	 */
	hdmi_read_reg(phy->base, HDMI_TXPHY_TX_CTRL);

	/*
	 * In OMAP5+, the HFBITCLK must be divided by 2 before issuing the
	 * HDMI_PHYPWRCMD_LDOON command.
	*/
	if (phy_feat->bist_ctrl)
		REG_FLD_MOD(phy->base, HDMI_TXPHY_BIST_CONTROL, 1, 11, 11);

	/*
	 * If the hfbitclk != lfbitclk, it means the lfbitclk was configured
	 * to be used for TMDS.
	 */
	if (hfbitclk != lfbitclk)
		freqout = 0;
	else if (hfbitclk / 10 < phy_feat->max_phy)
		freqout = 1;
	else
		freqout = 2;

	/*
	 * Write to phy address 0 to configure the clock
	 * use HFBITCLK write HDMI_TXPHY_TX_CONTROL_FREQOUT field
	 */
	REG_FLD_MOD(phy->base, HDMI_TXPHY_TX_CTRL, freqout, 31, 30);

	/* Write to phy address 1 to start HDMI line (TXVALID and TMDSCLKEN) */
	hdmi_write_reg(phy->base, HDMI_TXPHY_DIGITAL_CTRL, 0xF0000000);

	/* Setup max LDO voltage */
	if (phy_feat->ldo_voltage)
		REG_FLD_MOD(phy->base, HDMI_TXPHY_POWER_CTRL, 0xB, 3, 0);

	hdmi_phy_configure_lanes(phy);

	return 0;
}

static const struct hdmi_phy_features omap44xx_phy_feats = {
	.bist_ctrl	=	false,
	.ldo_voltage	=	true,
	.max_phy	=	185675000,
};

static const struct hdmi_phy_features omap54xx_phy_feats = {
	.bist_ctrl	=	true,
	.ldo_voltage	=	false,
	.max_phy	=	186000000,
};

static int hdmi_phy_init_features(struct platform_device *pdev)
{
	struct hdmi_phy_features *dst;
	const struct hdmi_phy_features *src;

	dst = devm_kzalloc(&pdev->dev, sizeof(*dst), GFP_KERNEL);
	if (!dst) {
		dev_err(&pdev->dev, "Failed to allocate HDMI PHY Features\n");
		return -ENOMEM;
	}

	switch (omapdss_get_version()) {
	case OMAPDSS_VER_OMAP4430_ES1:
	case OMAPDSS_VER_OMAP4430_ES2:
	case OMAPDSS_VER_OMAP4:
		src = &omap44xx_phy_feats;
		break;

	case OMAPDSS_VER_OMAP5:
	case OMAPDSS_VER_DRA7xx:
		src = &omap54xx_phy_feats;
		break;

	default:
		return -ENODEV;
	}

	memcpy(dst, src, sizeof(*dst));
	phy_feat = dst;

	return 0;
}

int hdmi_phy_init(struct platform_device *pdev, struct hdmi_phy_data *phy)
{
	int r;
	struct resource *res;

	r = hdmi_phy_init_features(pdev);
	if (r)
		return r;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy");
	if (!res) {
		DSSERR("can't get PHY mem resource\n");
		return -EINVAL;
	}

	phy->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(phy->base)) {
		DSSERR("can't ioremap TX PHY\n");
		return PTR_ERR(phy->base);
	}

	return 0;
}
