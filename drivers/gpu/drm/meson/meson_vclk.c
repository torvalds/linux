// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include "meson_drv.h"
#include "meson_vclk.h"

/**
 * DOC: Video Clocks
 *
 * VCLK is the "Pixel Clock" frequency generator from a dedicated PLL.
 * We handle the following encodings :
 *
 * - CVBS 27MHz generator via the VCLK2 to the VENCI and VDAC blocks
 * - HDMI Pixel Clocks generation
 *
 * What is missing :
 *
 * - Genenate Pixel clocks for 2K/4K 10bit formats
 *
 * Clock generator scheme :
 *
 * .. code::
 *
 *    __________   _________            _____
 *   |          | |         |          |     |--ENCI
 *   | HDMI PLL |-| PLL_DIV |--- VCLK--|     |--ENCL
 *   |__________| |_________| \        | MUX |--ENCP
 *                             --VCLK2-|     |--VDAC
 *                                     |_____|--HDMI-TX
 *
 * Final clocks can take input for either VCLK or VCLK2, but
 * VCLK is the preferred path for HDMI clocking and VCLK2 is the
 * preferred path for CVBS VDAC clocking.
 *
 * VCLK and VCLK2 have fixed divided clocks paths for /1, /2, /4, /6 or /12.
 *
 * The PLL_DIV can achieve an additional fractional dividing like
 * 1.5, 3.5, 3.75... to generate special 2K and 4K 10bit clocks.
 */

/* HHI Registers */
#define HHI_VID_PLL_CLK_DIV	0x1a0 /* 0x68 offset in data sheet */
#define VID_PLL_EN		BIT(19)
#define VID_PLL_BYPASS		BIT(18)
#define VID_PLL_PRESET		BIT(15)
#define HHI_VIID_CLK_DIV	0x128 /* 0x4a offset in data sheet */
#define VCLK2_DIV_MASK		0xff
#define VCLK2_DIV_EN		BIT(16)
#define VCLK2_DIV_RESET		BIT(17)
#define CTS_VDAC_SEL_MASK	(0xf << 28)
#define CTS_VDAC_SEL_SHIFT	28
#define HHI_VIID_CLK_CNTL	0x12c /* 0x4b offset in data sheet */
#define VCLK2_EN		BIT(19)
#define VCLK2_SEL_MASK		(0x7 << 16)
#define VCLK2_SEL_SHIFT		16
#define VCLK2_SOFT_RESET	BIT(15)
#define VCLK2_DIV1_EN		BIT(0)
#define HHI_VID_CLK_DIV		0x164 /* 0x59 offset in data sheet */
#define VCLK_DIV_MASK		0xff
#define VCLK_DIV_EN		BIT(16)
#define VCLK_DIV_RESET		BIT(17)
#define CTS_ENCP_SEL_MASK	(0xf << 24)
#define CTS_ENCP_SEL_SHIFT	24
#define CTS_ENCI_SEL_MASK	(0xf << 28)
#define CTS_ENCI_SEL_SHIFT	28
#define HHI_VID_CLK_CNTL	0x17c /* 0x5f offset in data sheet */
#define VCLK_EN			BIT(19)
#define VCLK_SEL_MASK		(0x7 << 16)
#define VCLK_SEL_SHIFT		16
#define VCLK_SOFT_RESET		BIT(15)
#define VCLK_DIV1_EN		BIT(0)
#define VCLK_DIV2_EN		BIT(1)
#define VCLK_DIV4_EN		BIT(2)
#define VCLK_DIV6_EN		BIT(3)
#define VCLK_DIV12_EN		BIT(4)
#define HHI_VID_CLK_CNTL2	0x194 /* 0x65 offset in data sheet */
#define CTS_ENCI_EN		BIT(0)
#define CTS_ENCP_EN		BIT(2)
#define CTS_VDAC_EN		BIT(4)
#define HDMI_TX_PIXEL_EN	BIT(5)
#define HHI_HDMI_CLK_CNTL	0x1cc /* 0x73 offset in data sheet */
#define HDMI_TX_PIXEL_SEL_MASK	(0xf << 16)
#define HDMI_TX_PIXEL_SEL_SHIFT	16
#define CTS_HDMI_SYS_SEL_MASK	(0x7 << 9)
#define CTS_HDMI_SYS_DIV_MASK	(0x7f)
#define CTS_HDMI_SYS_EN		BIT(8)

#define HHI_VDAC_CNTL0		0x2F4 /* 0xbd offset in data sheet */
#define HHI_VDAC_CNTL1		0x2F8 /* 0xbe offset in data sheet */

#define HHI_HDMI_PLL_CNTL	0x320 /* 0xc8 offset in data sheet */
#define HHI_HDMI_PLL_CNTL2	0x324 /* 0xc9 offset in data sheet */
#define HHI_HDMI_PLL_CNTL3	0x328 /* 0xca offset in data sheet */
#define HHI_HDMI_PLL_CNTL4	0x32C /* 0xcb offset in data sheet */
#define HHI_HDMI_PLL_CNTL5	0x330 /* 0xcc offset in data sheet */
#define HHI_HDMI_PLL_CNTL6	0x334 /* 0xcd offset in data sheet */
#define HHI_HDMI_PLL_CNTL7	0x338 /* 0xce offset in data sheet */

#define HDMI_PLL_RESET		BIT(28)
#define HDMI_PLL_RESET_G12A	BIT(29)
#define HDMI_PLL_LOCK		BIT(31)
#define HDMI_PLL_LOCK_G12A	(3 << 30)

#define FREQ_1000_1001(_freq)	DIV_ROUND_CLOSEST(_freq * 1000, 1001)

/* VID PLL Dividers */
enum {
	VID_PLL_DIV_1 = 0,
	VID_PLL_DIV_2,
	VID_PLL_DIV_2p5,
	VID_PLL_DIV_3,
	VID_PLL_DIV_3p5,
	VID_PLL_DIV_3p75,
	VID_PLL_DIV_4,
	VID_PLL_DIV_5,
	VID_PLL_DIV_6,
	VID_PLL_DIV_6p25,
	VID_PLL_DIV_7,
	VID_PLL_DIV_7p5,
	VID_PLL_DIV_12,
	VID_PLL_DIV_14,
	VID_PLL_DIV_15,
};

void meson_vid_pll_set(struct meson_drm *priv, unsigned int div)
{
	unsigned int shift_val = 0;
	unsigned int shift_sel = 0;

	/* Disable vid_pll output clock */
	regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV, VID_PLL_EN, 0);
	regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV, VID_PLL_PRESET, 0);

	switch (div) {
	case VID_PLL_DIV_2:
		shift_val = 0x0aaa;
		shift_sel = 0;
		break;
	case VID_PLL_DIV_2p5:
		shift_val = 0x5294;
		shift_sel = 2;
		break;
	case VID_PLL_DIV_3:
		shift_val = 0x0db6;
		shift_sel = 0;
		break;
	case VID_PLL_DIV_3p5:
		shift_val = 0x36cc;
		shift_sel = 1;
		break;
	case VID_PLL_DIV_3p75:
		shift_val = 0x6666;
		shift_sel = 2;
		break;
	case VID_PLL_DIV_4:
		shift_val = 0x0ccc;
		shift_sel = 0;
		break;
	case VID_PLL_DIV_5:
		shift_val = 0x739c;
		shift_sel = 2;
		break;
	case VID_PLL_DIV_6:
		shift_val = 0x0e38;
		shift_sel = 0;
		break;
	case VID_PLL_DIV_6p25:
		shift_val = 0x0000;
		shift_sel = 3;
		break;
	case VID_PLL_DIV_7:
		shift_val = 0x3c78;
		shift_sel = 1;
		break;
	case VID_PLL_DIV_7p5:
		shift_val = 0x78f0;
		shift_sel = 2;
		break;
	case VID_PLL_DIV_12:
		shift_val = 0x0fc0;
		shift_sel = 0;
		break;
	case VID_PLL_DIV_14:
		shift_val = 0x3f80;
		shift_sel = 1;
		break;
	case VID_PLL_DIV_15:
		shift_val = 0x7f80;
		shift_sel = 2;
		break;
	}

	if (div == VID_PLL_DIV_1)
		/* Enable vid_pll bypass to HDMI pll */
		regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV,
				   VID_PLL_BYPASS, VID_PLL_BYPASS);
	else {
		/* Disable Bypass */
		regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV,
				   VID_PLL_BYPASS, 0);
		/* Clear sel */
		regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV,
				   3 << 16, 0);
		regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV,
				   VID_PLL_PRESET, 0);
		regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV,
				   0x7fff, 0);

		/* Setup sel and val */
		regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV,
				   3 << 16, shift_sel << 16);
		regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV,
				   VID_PLL_PRESET, VID_PLL_PRESET);
		regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV,
				   0x7fff, shift_val);

		regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV,
				   VID_PLL_PRESET, 0);
	}

	/* Enable the vid_pll output clock */
	regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV,
				VID_PLL_EN, VID_PLL_EN);
}

/*
 * Setup VCLK2 for 27MHz, and enable clocks for ENCI and VDAC
 *
 * TOFIX: Refactor into table to also handle HDMI frequency and paths
 */
static void meson_venci_cvbs_clock_config(struct meson_drm *priv)
{
	unsigned int val;

	/* Setup PLL to output 1.485GHz */
	if (meson_vpu_is_compatible(priv, "amlogic,meson-gxbb-vpu")) {
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x5800023d);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL2, 0x00404e00);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL3, 0x0d5c5091);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL4, 0x801da72c);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL5, 0x71486980);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL6, 0x00000e55);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x4800023d);

		/* Poll for lock bit */
		regmap_read_poll_timeout(priv->hhi, HHI_HDMI_PLL_CNTL, val,
					 (val & HDMI_PLL_LOCK), 10, 0);
	} else if (meson_vpu_is_compatible(priv, "amlogic,meson-gxm-vpu") ||
		   meson_vpu_is_compatible(priv, "amlogic,meson-gxl-vpu")) {
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x4000027b);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL2, 0x800cb300);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL3, 0xa6212844);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL4, 0x0c4d000c);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL5, 0x001fa729);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL6, 0x01a31500);

		/* Reset PLL */
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL,
					HDMI_PLL_RESET, HDMI_PLL_RESET);
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL,
					HDMI_PLL_RESET, 0);

		/* Poll for lock bit */
		regmap_read_poll_timeout(priv->hhi, HHI_HDMI_PLL_CNTL, val,
					 (val & HDMI_PLL_LOCK), 10, 0);
	} else if (meson_vpu_is_compatible(priv, "amlogic,meson-g12a-vpu")) {
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x1a0504f7);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL2, 0x00010000);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL3, 0x00000000);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL4, 0x6a28dc00);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL5, 0x65771290);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL6, 0x39272000);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL7, 0x56540000);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x3a0504f7);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x1a0504f7);

		/* Poll for lock bit */
		regmap_read_poll_timeout(priv->hhi, HHI_HDMI_PLL_CNTL, val,
			((val & HDMI_PLL_LOCK_G12A) == HDMI_PLL_LOCK_G12A),
			10, 0);
	}

	/* Disable VCLK2 */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL, VCLK2_EN, 0);

	/* Setup vid_pll to /1 */
	meson_vid_pll_set(priv, VID_PLL_DIV_1);

	/* Setup the VCLK2 divider value to achieve 27MHz */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_DIV,
				VCLK2_DIV_MASK, (55 - 1));

	/* select vid_pll for vclk2 */
	if (meson_vpu_is_compatible(priv, "amlogic,meson-g12a-vpu"))
		regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL,
					VCLK2_SEL_MASK, (0 << VCLK2_SEL_SHIFT));
	else
		regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL,
					VCLK2_SEL_MASK, (4 << VCLK2_SEL_SHIFT));

	/* enable vclk2 gate */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL, VCLK2_EN, VCLK2_EN);

	/* select vclk_div1 for enci */
	regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
				CTS_ENCI_SEL_MASK, (8 << CTS_ENCI_SEL_SHIFT));
	/* select vclk_div1 for vdac */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_DIV,
				CTS_VDAC_SEL_MASK, (8 << CTS_VDAC_SEL_SHIFT));

	/* release vclk2_div_reset and enable vclk2_div */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_DIV,
				VCLK2_DIV_EN | VCLK2_DIV_RESET, VCLK2_DIV_EN);

	/* enable vclk2_div1 gate */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL,
				VCLK2_DIV1_EN, VCLK2_DIV1_EN);

	/* reset vclk2 */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL,
				VCLK2_SOFT_RESET, VCLK2_SOFT_RESET);
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL,
				VCLK2_SOFT_RESET, 0);

	/* enable enci_clk */
	regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL2,
				CTS_ENCI_EN, CTS_ENCI_EN);
	/* enable vdac_clk */
	regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL2,
				CTS_VDAC_EN, CTS_VDAC_EN);
}

enum {
/* PLL	O1 O2 O3 VP DV     EN TX */
/* 4320 /4 /4 /1 /5 /1  => /2 /2 */
	MESON_VCLK_HDMI_ENCI_54000 = 0,
/* 4320 /4 /4 /1 /5 /1  => /1 /2 */
	MESON_VCLK_HDMI_DDR_54000,
/* 2970 /4 /1 /1 /5 /1  => /1 /2 */
	MESON_VCLK_HDMI_DDR_148500,
/* 2970 /2 /2 /2 /5 /1  => /1 /1 */
	MESON_VCLK_HDMI_74250,
/* 2970 /1 /2 /2 /5 /1  => /1 /1 */
	MESON_VCLK_HDMI_148500,
/* 2970 /1 /1 /1 /5 /2  => /1 /1 */
	MESON_VCLK_HDMI_297000,
/* 5940 /1 /1 /2 /5 /1  => /1 /1 */
	MESON_VCLK_HDMI_594000
};

struct meson_vclk_params {
	unsigned int pixel_freq;
	unsigned int pll_base_freq;
	unsigned int pll_od1;
	unsigned int pll_od2;
	unsigned int pll_od3;
	unsigned int vid_pll_div;
	unsigned int vclk_div;
} params[] = {
	[MESON_VCLK_HDMI_ENCI_54000] = {
		.pixel_freq = 54000,
		.pll_base_freq = 4320000,
		.pll_od1 = 4,
		.pll_od2 = 4,
		.pll_od3 = 1,
		.vid_pll_div = VID_PLL_DIV_5,
		.vclk_div = 1,
	},
	[MESON_VCLK_HDMI_DDR_54000] = {
		.pixel_freq = 54000,
		.pll_base_freq = 4320000,
		.pll_od1 = 4,
		.pll_od2 = 4,
		.pll_od3 = 1,
		.vid_pll_div = VID_PLL_DIV_5,
		.vclk_div = 1,
	},
	[MESON_VCLK_HDMI_DDR_148500] = {
		.pixel_freq = 148500,
		.pll_base_freq = 2970000,
		.pll_od1 = 4,
		.pll_od2 = 1,
		.pll_od3 = 1,
		.vid_pll_div = VID_PLL_DIV_5,
		.vclk_div = 1,
	},
	[MESON_VCLK_HDMI_74250] = {
		.pixel_freq = 74250,
		.pll_base_freq = 2970000,
		.pll_od1 = 2,
		.pll_od2 = 2,
		.pll_od3 = 2,
		.vid_pll_div = VID_PLL_DIV_5,
		.vclk_div = 1,
	},
	[MESON_VCLK_HDMI_148500] = {
		.pixel_freq = 148500,
		.pll_base_freq = 2970000,
		.pll_od1 = 1,
		.pll_od2 = 2,
		.pll_od3 = 2,
		.vid_pll_div = VID_PLL_DIV_5,
		.vclk_div = 1,
	},
	[MESON_VCLK_HDMI_297000] = {
		.pixel_freq = 297000,
		.pll_base_freq = 5940000,
		.pll_od1 = 2,
		.pll_od2 = 1,
		.pll_od3 = 1,
		.vid_pll_div = VID_PLL_DIV_5,
		.vclk_div = 2,
	},
	[MESON_VCLK_HDMI_594000] = {
		.pixel_freq = 594000,
		.pll_base_freq = 5940000,
		.pll_od1 = 1,
		.pll_od2 = 1,
		.pll_od3 = 2,
		.vid_pll_div = VID_PLL_DIV_5,
		.vclk_div = 1,
	},
	{ /* sentinel */ },
};

static inline unsigned int pll_od_to_reg(unsigned int od)
{
	switch (od) {
	case 1:
		return 0;
	case 2:
		return 1;
	case 4:
		return 2;
	case 8:
		return 3;
	}

	/* Invalid */
	return 0;
}

void meson_hdmi_pll_set_params(struct meson_drm *priv, unsigned int m,
			       unsigned int frac, unsigned int od1,
			       unsigned int od2, unsigned int od3)
{
	unsigned int val;

	if (meson_vpu_is_compatible(priv, "amlogic,meson-gxbb-vpu")) {
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x58000200 | m);
		if (frac)
			regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL2,
				     0x00004000 | frac);
		else
			regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL2,
				     0x00000000);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL3, 0x0d5c5091);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL4, 0x801da72c);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL5, 0x71486980);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL6, 0x00000e55);

		/* Enable and unreset */
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL,
				   0x7 << 28, 0x4 << 28);

		/* Poll for lock bit */
		regmap_read_poll_timeout(priv->hhi, HHI_HDMI_PLL_CNTL,
					 val, (val & HDMI_PLL_LOCK), 10, 0);
	} else if (meson_vpu_is_compatible(priv, "amlogic,meson-gxm-vpu") ||
		   meson_vpu_is_compatible(priv, "amlogic,meson-gxl-vpu")) {
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x40000200 | m);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL2, 0x800cb000 | frac);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL3, 0x860f30c4);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL4, 0x0c8e0000);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL5, 0x001fa729);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL6, 0x01a31500);

		/* Reset PLL */
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL,
				HDMI_PLL_RESET, HDMI_PLL_RESET);
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL,
				HDMI_PLL_RESET, 0);

		/* Poll for lock bit */
		regmap_read_poll_timeout(priv->hhi, HHI_HDMI_PLL_CNTL, val,
				(val & HDMI_PLL_LOCK), 10, 0);
	} else if (meson_vpu_is_compatible(priv, "amlogic,meson-g12a-vpu")) {
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x0b3a0400 | m);

		/* Enable and reset */
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL,
				   0x3 << 28, 0x3 << 28);

		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL2, frac);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL3, 0x00000000);

		/* G12A HDMI PLL Needs specific parameters for 5.4GHz */
		if (m >= 0xf7) {
			if (frac < 0x10000) {
				regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL4,
							0x6a685c00);
				regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL5,
							0x11551293);
			} else {
				regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL4,
							0xea68dc00);
				regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL5,
							0x65771290);
			}
			regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL6, 0x39272000);
			regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL7, 0x55540000);
		} else {
			regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL4, 0x0a691c00);
			regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL5, 0x33771290);
			regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL6, 0x39270000);
			regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL7, 0x50540000);
		}

		do {
			/* Reset PLL */
			regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL,
					HDMI_PLL_RESET_G12A, HDMI_PLL_RESET_G12A);

			/* UN-Reset PLL */
			regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL,
					HDMI_PLL_RESET_G12A, 0);

			/* Poll for lock bits */
			if (!regmap_read_poll_timeout(priv->hhi,
						      HHI_HDMI_PLL_CNTL, val,
						      ((val & HDMI_PLL_LOCK_G12A)
						        == HDMI_PLL_LOCK_G12A),
						      10, 100))
				break;
		} while(1);
	}

	if (meson_vpu_is_compatible(priv, "amlogic,meson-gxbb-vpu"))
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL2,
				3 << 16, pll_od_to_reg(od1) << 16);
	else if (meson_vpu_is_compatible(priv, "amlogic,meson-gxm-vpu") ||
		 meson_vpu_is_compatible(priv, "amlogic,meson-gxl-vpu"))
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL3,
				3 << 21, pll_od_to_reg(od1) << 21);
	else if (meson_vpu_is_compatible(priv, "amlogic,meson-g12a-vpu"))
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL,
				3 << 16, pll_od_to_reg(od1) << 16);

	if (meson_vpu_is_compatible(priv, "amlogic,meson-gxbb-vpu"))
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL2,
				3 << 22, pll_od_to_reg(od2) << 22);
	else if (meson_vpu_is_compatible(priv, "amlogic,meson-gxm-vpu") ||
		 meson_vpu_is_compatible(priv, "amlogic,meson-gxl-vpu"))
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL3,
				3 << 23, pll_od_to_reg(od2) << 23);
	else if (meson_vpu_is_compatible(priv, "amlogic,meson-g12a-vpu"))
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL,
				3 << 18, pll_od_to_reg(od2) << 18);

	if (meson_vpu_is_compatible(priv, "amlogic,meson-gxbb-vpu"))
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL2,
				3 << 18, pll_od_to_reg(od3) << 18);
	else if (meson_vpu_is_compatible(priv, "amlogic,meson-gxm-vpu") ||
		 meson_vpu_is_compatible(priv, "amlogic,meson-gxl-vpu"))
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL3,
				3 << 19, pll_od_to_reg(od3) << 19);
	else if (meson_vpu_is_compatible(priv, "amlogic,meson-g12a-vpu"))
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL,
				3 << 20, pll_od_to_reg(od3) << 20);
}

#define XTAL_FREQ 24000

static unsigned int meson_hdmi_pll_get_m(struct meson_drm *priv,
					 unsigned int pll_freq)
{
	/* The GXBB PLL has a /2 pre-multiplier */
	if (meson_vpu_is_compatible(priv, "amlogic,meson-gxbb-vpu"))
		pll_freq /= 2;

	return pll_freq / XTAL_FREQ;
}

#define HDMI_FRAC_MAX_GXBB	4096
#define HDMI_FRAC_MAX_GXL	1024
#define HDMI_FRAC_MAX_G12A	131072

static unsigned int meson_hdmi_pll_get_frac(struct meson_drm *priv,
					    unsigned int m,
					    unsigned int pll_freq)
{
	unsigned int parent_freq = XTAL_FREQ;
	unsigned int frac_max = HDMI_FRAC_MAX_GXL;
	unsigned int frac_m;
	unsigned int frac;

	/* The GXBB PLL has a /2 pre-multiplier and a larger FRAC width */
	if (meson_vpu_is_compatible(priv, "amlogic,meson-gxbb-vpu")) {
		frac_max = HDMI_FRAC_MAX_GXBB;
		parent_freq *= 2;
	}

	if (meson_vpu_is_compatible(priv, "amlogic,meson-g12a-vpu"))
		frac_max = HDMI_FRAC_MAX_G12A;

	/* We can have a perfect match !*/
	if (pll_freq / m == parent_freq &&
	    pll_freq % m == 0)
		return 0;

	frac = div_u64((u64)pll_freq * (u64)frac_max, parent_freq);
	frac_m = m * frac_max;
	if (frac_m > frac)
		return frac_max;
	frac -= frac_m;

	return min((u16)frac, (u16)(frac_max - 1));
}

static bool meson_hdmi_pll_validate_params(struct meson_drm *priv,
					   unsigned int m,
					   unsigned int frac)
{
	if (meson_vpu_is_compatible(priv, "amlogic,meson-gxbb-vpu")) {
		/* Empiric supported min/max dividers */
		if (m < 53 || m > 123)
			return false;
		if (frac >= HDMI_FRAC_MAX_GXBB)
			return false;
	} else if (meson_vpu_is_compatible(priv, "amlogic,meson-gxm-vpu") ||
		   meson_vpu_is_compatible(priv, "amlogic,meson-gxl-vpu") ||
		   meson_vpu_is_compatible(priv, "amlogic,meson-g12a-vpu")) {
		/* Empiric supported min/max dividers */
		if (m < 106 || m > 247)
			return false;
		if (frac >= HDMI_FRAC_MAX_GXL)
			return false;
	}

	return true;
}

static bool meson_hdmi_pll_find_params(struct meson_drm *priv,
				       unsigned int freq,
				       unsigned int *m,
				       unsigned int *frac,
				       unsigned int *od)
{
	/* Cycle from /16 to /2 */
	for (*od = 16 ; *od > 1 ; *od >>= 1) {
		*m = meson_hdmi_pll_get_m(priv, freq * *od);
		if (!*m)
			continue;
		*frac = meson_hdmi_pll_get_frac(priv, *m, freq * *od);

		DRM_DEBUG_DRIVER("PLL params for %dkHz: m=%x frac=%x od=%d\n",
				 freq, *m, *frac, *od);

		if (meson_hdmi_pll_validate_params(priv, *m, *frac))
			return true;
	}

	return false;
}

/* pll_freq is the frequency after the OD dividers */
enum drm_mode_status
meson_vclk_dmt_supported_freq(struct meson_drm *priv, unsigned int freq)
{
	unsigned int od, m, frac;

	/* In DMT mode, path after PLL is always /10 */
	freq *= 10;

	if (meson_hdmi_pll_find_params(priv, freq, &m, &frac, &od))
		return MODE_OK;

	return MODE_CLOCK_RANGE;
}
EXPORT_SYMBOL_GPL(meson_vclk_dmt_supported_freq);

/* pll_freq is the frequency after the OD dividers */
static void meson_hdmi_pll_generic_set(struct meson_drm *priv,
				       unsigned int pll_freq)
{
	unsigned int od, m, frac, od1, od2, od3;

	if (meson_hdmi_pll_find_params(priv, pll_freq, &m, &frac, &od)) {
		od3 = 1;
		if (od < 4) {
			od1 = 2;
			od2 = 1;
		} else {
			od2 = od / 4;
			od1 = od / od2;
		}

		DRM_DEBUG_DRIVER("PLL params for %dkHz: m=%x frac=%x od=%d/%d/%d\n",
				 pll_freq, m, frac, od1, od2, od3);

		meson_hdmi_pll_set_params(priv, m, frac, od1, od2, od3);

		return;
	}

	DRM_ERROR("Fatal, unable to find parameters for PLL freq %d\n",
		  pll_freq);
}

enum drm_mode_status
meson_vclk_vic_supported_freq(unsigned int freq)
{
	int i;

	DRM_DEBUG_DRIVER("freq = %d\n", freq);

	for (i = 0 ; params[i].pixel_freq ; ++i) {
		DRM_DEBUG_DRIVER("i = %d pixel_freq = %d alt = %d\n",
				 i, params[i].pixel_freq,
				 FREQ_1000_1001(params[i].pixel_freq));
		/* Match strict frequency */
		if (freq == params[i].pixel_freq)
			return MODE_OK;
		/* Match 1000/1001 variant */
		if (freq == FREQ_1000_1001(params[i].pixel_freq))
			return MODE_OK;
	}

	return MODE_CLOCK_RANGE;
}
EXPORT_SYMBOL_GPL(meson_vclk_vic_supported_freq);

static void meson_vclk_set(struct meson_drm *priv, unsigned int pll_base_freq,
			   unsigned int od1, unsigned int od2, unsigned int od3,
			   unsigned int vid_pll_div, unsigned int vclk_div,
			   unsigned int hdmi_tx_div, unsigned int venc_div,
			   bool hdmi_use_enci, bool vic_alternate_clock)
{
	unsigned int m = 0, frac = 0;

	/* Set HDMI-TX sys clock */
	regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL,
			   CTS_HDMI_SYS_SEL_MASK, 0);
	regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL,
			   CTS_HDMI_SYS_DIV_MASK, 0);
	regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL,
			   CTS_HDMI_SYS_EN, CTS_HDMI_SYS_EN);

	/* Set HDMI PLL rate */
	if (!od1 && !od2 && !od3) {
		meson_hdmi_pll_generic_set(priv, pll_base_freq);
	} else if (meson_vpu_is_compatible(priv, "amlogic,meson-gxbb-vpu")) {
		switch (pll_base_freq) {
		case 2970000:
			m = 0x3d;
			frac = vic_alternate_clock ? 0xd02 : 0xe00;
			break;
		case 4320000:
			m = vic_alternate_clock ? 0x59 : 0x5a;
			frac = vic_alternate_clock ? 0xe8f : 0;
			break;
		case 5940000:
			m = 0x7b;
			frac = vic_alternate_clock ? 0xa05 : 0xc00;
			break;
		}

		meson_hdmi_pll_set_params(priv, m, frac, od1, od2, od3);
	} else if (meson_vpu_is_compatible(priv, "amlogic,meson-gxm-vpu") ||
		   meson_vpu_is_compatible(priv, "amlogic,meson-gxl-vpu")) {
		switch (pll_base_freq) {
		case 2970000:
			m = 0x7b;
			frac = vic_alternate_clock ? 0x281 : 0x300;
			break;
		case 4320000:
			m = vic_alternate_clock ? 0xb3 : 0xb4;
			frac = vic_alternate_clock ? 0x347 : 0;
			break;
		case 5940000:
			m = 0xf7;
			frac = vic_alternate_clock ? 0x102 : 0x200;
			break;
		}

		meson_hdmi_pll_set_params(priv, m, frac, od1, od2, od3);
	} else if (meson_vpu_is_compatible(priv, "amlogic,meson-g12a-vpu")) {
		switch (pll_base_freq) {
		case 2970000:
			m = 0x7b;
			frac = vic_alternate_clock ? 0x140b4 : 0x18000;
			break;
		case 4320000:
			m = vic_alternate_clock ? 0xb3 : 0xb4;
			frac = vic_alternate_clock ? 0x1a3ee : 0;
			break;
		case 5940000:
			m = 0xf7;
			frac = vic_alternate_clock ? 0x8148 : 0x10000;
			break;
		}

		meson_hdmi_pll_set_params(priv, m, frac, od1, od2, od3);
	}

	/* Setup vid_pll divider */
	meson_vid_pll_set(priv, vid_pll_div);

	/* Set VCLK div */
	regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL,
			   VCLK_SEL_MASK, 0);
	regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
			   VCLK_DIV_MASK, vclk_div - 1);

	/* Set HDMI-TX source */
	switch (hdmi_tx_div) {
	case 1:
		/* enable vclk_div1 gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL,
				   VCLK_DIV1_EN, VCLK_DIV1_EN);

		/* select vclk_div1 for HDMI-TX */
		regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL,
				   HDMI_TX_PIXEL_SEL_MASK, 0);
		break;
	case 2:
		/* enable vclk_div2 gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL,
				   VCLK_DIV2_EN, VCLK_DIV2_EN);

		/* select vclk_div2 for HDMI-TX */
		regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL,
			HDMI_TX_PIXEL_SEL_MASK, 1 << HDMI_TX_PIXEL_SEL_SHIFT);
		break;
	case 4:
		/* enable vclk_div4 gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL,
				   VCLK_DIV4_EN, VCLK_DIV4_EN);

		/* select vclk_div4 for HDMI-TX */
		regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL,
			HDMI_TX_PIXEL_SEL_MASK, 2 << HDMI_TX_PIXEL_SEL_SHIFT);
		break;
	case 6:
		/* enable vclk_div6 gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL,
				   VCLK_DIV6_EN, VCLK_DIV6_EN);

		/* select vclk_div6 for HDMI-TX */
		regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL,
			HDMI_TX_PIXEL_SEL_MASK, 3 << HDMI_TX_PIXEL_SEL_SHIFT);
		break;
	case 12:
		/* enable vclk_div12 gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL,
				   VCLK_DIV12_EN, VCLK_DIV12_EN);

		/* select vclk_div12 for HDMI-TX */
		regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL,
			HDMI_TX_PIXEL_SEL_MASK, 4 << HDMI_TX_PIXEL_SEL_SHIFT);
		break;
	}
	regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL2,
				   HDMI_TX_PIXEL_EN, HDMI_TX_PIXEL_EN);

	/* Set ENCI/ENCP Source */
	switch (venc_div) {
	case 1:
		/* enable vclk_div1 gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL,
				   VCLK_DIV1_EN, VCLK_DIV1_EN);

		if (hdmi_use_enci)
			/* select vclk_div1 for enci */
			regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
					   CTS_ENCI_SEL_MASK, 0);
		else
			/* select vclk_div1 for encp */
			regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
					   CTS_ENCP_SEL_MASK, 0);
		break;
	case 2:
		/* enable vclk_div2 gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL,
				   VCLK_DIV2_EN, VCLK_DIV2_EN);

		if (hdmi_use_enci)
			/* select vclk_div2 for enci */
			regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
				CTS_ENCI_SEL_MASK, 1 << CTS_ENCI_SEL_SHIFT);
		else
			/* select vclk_div2 for encp */
			regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
				CTS_ENCP_SEL_MASK, 1 << CTS_ENCP_SEL_SHIFT);
		break;
	case 4:
		/* enable vclk_div4 gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL,
				   VCLK_DIV4_EN, VCLK_DIV4_EN);

		if (hdmi_use_enci)
			/* select vclk_div4 for enci */
			regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
				CTS_ENCI_SEL_MASK, 2 << CTS_ENCI_SEL_SHIFT);
		else
			/* select vclk_div4 for encp */
			regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
				CTS_ENCP_SEL_MASK, 2 << CTS_ENCP_SEL_SHIFT);
		break;
	case 6:
		/* enable vclk_div6 gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL,
				   VCLK_DIV6_EN, VCLK_DIV6_EN);

		if (hdmi_use_enci)
			/* select vclk_div6 for enci */
			regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
				CTS_ENCI_SEL_MASK, 3 << CTS_ENCI_SEL_SHIFT);
		else
			/* select vclk_div6 for encp */
			regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
				CTS_ENCP_SEL_MASK, 3 << CTS_ENCP_SEL_SHIFT);
		break;
	case 12:
		/* enable vclk_div12 gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL,
				   VCLK_DIV12_EN, VCLK_DIV12_EN);

		if (hdmi_use_enci)
			/* select vclk_div12 for enci */
			regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
				CTS_ENCI_SEL_MASK, 4 << CTS_ENCI_SEL_SHIFT);
		else
			/* select vclk_div12 for encp */
			regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV,
				CTS_ENCP_SEL_MASK, 4 << CTS_ENCP_SEL_SHIFT);
		break;
	}

	if (hdmi_use_enci)
		/* Enable ENCI clock gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL2,
				   CTS_ENCI_EN, CTS_ENCI_EN);
	else
		/* Enable ENCP clock gate */
		regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL2,
				   CTS_ENCP_EN, CTS_ENCP_EN);

	regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL, VCLK_EN, VCLK_EN);
}

void meson_vclk_setup(struct meson_drm *priv, unsigned int target,
		      unsigned int vclk_freq, unsigned int venc_freq,
		      unsigned int dac_freq, bool hdmi_use_enci)
{
	bool vic_alternate_clock = false;
	unsigned int freq;
	unsigned int hdmi_tx_div;
	unsigned int venc_div;

	if (target == MESON_VCLK_TARGET_CVBS) {
		meson_venci_cvbs_clock_config(priv);
		return;
	} else if (target == MESON_VCLK_TARGET_DMT) {
		/* The DMT clock path is fixed after the PLL:
		 * - automatic PLL freq + OD management
		 * - vid_pll_div = VID_PLL_DIV_5
		 * - vclk_div = 2
		 * - hdmi_tx_div = 1
		 * - venc_div = 1
		 * - encp encoder
		 */
		meson_vclk_set(priv, vclk_freq * 10, 0, 0, 0,
			       VID_PLL_DIV_5, 2, 1, 1, false, false);
		return;
	}

	hdmi_tx_div = vclk_freq / dac_freq;

	if (hdmi_tx_div == 0) {
		pr_err("Fatal Error, invalid HDMI-TX freq %d\n",
		       dac_freq);
		return;
	}

	venc_div = vclk_freq / venc_freq;

	if (venc_div == 0) {
		pr_err("Fatal Error, invalid HDMI venc freq %d\n",
		       venc_freq);
		return;
	}

	for (freq = 0 ; params[freq].pixel_freq ; ++freq) {
		if (vclk_freq == params[freq].pixel_freq ||
		    vclk_freq == FREQ_1000_1001(params[freq].pixel_freq)) {
			if (vclk_freq != params[freq].pixel_freq)
				vic_alternate_clock = true;
			else
				vic_alternate_clock = false;

			if (freq == MESON_VCLK_HDMI_ENCI_54000 &&
			    !hdmi_use_enci)
				continue;

			if (freq == MESON_VCLK_HDMI_DDR_54000 &&
			    hdmi_use_enci)
				continue;

			if (freq == MESON_VCLK_HDMI_DDR_148500 &&
			    dac_freq == vclk_freq)
				continue;

			if (freq == MESON_VCLK_HDMI_148500 &&
			    dac_freq != vclk_freq)
				continue;
			break;
		}
	}

	if (!params[freq].pixel_freq) {
		pr_err("Fatal Error, invalid HDMI vclk freq %d\n", vclk_freq);
		return;
	}

	meson_vclk_set(priv, params[freq].pll_base_freq,
		       params[freq].pll_od1, params[freq].pll_od2,
		       params[freq].pll_od3, params[freq].vid_pll_div,
		       params[freq].vclk_div, hdmi_tx_div, venc_div,
		       hdmi_use_enci, vic_alternate_clock);
}
EXPORT_SYMBOL_GPL(meson_vclk_setup);
