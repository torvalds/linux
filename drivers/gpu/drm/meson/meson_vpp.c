/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2014 Endless Mobile
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include "meson_drv.h"
#include "meson_vpp.h"
#include "meson_registers.h"

/**
 * DOC: Video Post Processing
 *
 * VPP Handles all the Post Processing after the Scanout from the VIU
 * We handle the following post processings :
 *
 * - Postblend, Blends the OSD1 only
 *	We exclude OSD2, VS1, VS1 and Preblend output
 * - Vertical OSD Scaler for OSD1 only, we disable vertical scaler and
 *	use it only for interlace scanout
 * - Intermediate FIFO with default Amlogic values
 *
 * What is missing :
 *
 * - Preblend for video overlay pre-scaling
 * - OSD2 support for cursor framebuffer
 * - Video pre-scaling before postblend
 * - Full Vertical/Horizontal OSD scaling to support TV overscan
 * - HDR conversion
 */

void meson_vpp_setup_mux(struct meson_drm *priv, unsigned int mux)
{
	writel(mux, priv->io_base + _REG(VPU_VIU_VENC_MUX_CTRL));
}

/*
 * When the output is interlaced, the OSD must switch between
 * each field using the INTERLACE_SEL_ODD (0) of VIU_OSD1_BLK0_CFG_W0
 * at each vsync.
 * But the vertical scaler can provide such funtionnality if
 * is configured for 2:1 scaling with interlace options enabled.
 */
void meson_vpp_setup_interlace_vscaler_osd1(struct meson_drm *priv,
					    struct drm_rect *input)
{
	writel_relaxed(BIT(3) /* Enable scaler */ |
		       BIT(2), /* Select OSD1 */
			priv->io_base + _REG(VPP_OSD_SC_CTRL0));

	writel_relaxed(((drm_rect_width(input) - 1) << 16) |
		       (drm_rect_height(input) - 1),
			priv->io_base + _REG(VPP_OSD_SCI_WH_M1));
	/* 2:1 scaling */
	writel_relaxed(((input->x1) << 16) | (input->x2),
			priv->io_base + _REG(VPP_OSD_SCO_H_START_END));
	writel_relaxed(((input->y1 >> 1) << 16) | (input->y2 >> 1),
			priv->io_base + _REG(VPP_OSD_SCO_V_START_END));

	/* 2:1 scaling values */
	writel_relaxed(BIT(16), priv->io_base + _REG(VPP_OSD_VSC_INI_PHASE));
	writel_relaxed(BIT(25), priv->io_base + _REG(VPP_OSD_VSC_PHASE_STEP));

	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_HSC_CTRL0));

	writel_relaxed((4 << 0) /* osd_vsc_bank_length */ |
		       (4 << 3) /* osd_vsc_top_ini_rcv_num0 */ |
		       (1 << 8) /* osd_vsc_top_rpt_p0_num0 */ |
		       (6 << 11) /* osd_vsc_bot_ini_rcv_num0 */ |
		       (2 << 16) /* osd_vsc_bot_rpt_p0_num0 */ |
		       BIT(23)	/* osd_prog_interlace */ |
		       BIT(24), /* Enable vertical scaler */
			priv->io_base + _REG(VPP_OSD_VSC_CTRL0));
}

void meson_vpp_disable_interlace_vscaler_osd1(struct meson_drm *priv)
{
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_SC_CTRL0));
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_VSC_CTRL0));
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_HSC_CTRL0));
}

static unsigned int vpp_filter_coefs_4point_bspline[] = {
	0x15561500, 0x14561600, 0x13561700, 0x12561800,
	0x11551a00, 0x11541b00, 0x10541c00, 0x0f541d00,
	0x0f531e00, 0x0e531f00, 0x0d522100, 0x0c522200,
	0x0b522300, 0x0b512400, 0x0a502600, 0x0a4f2700,
	0x094e2900, 0x084e2a00, 0x084d2b00, 0x074c2c01,
	0x074b2d01, 0x064a2f01, 0x06493001, 0x05483201,
	0x05473301, 0x05463401, 0x04453601, 0x04433702,
	0x04423802, 0x03413a02, 0x03403b02, 0x033f3c02,
	0x033d3d03
};

static void meson_vpp_write_scaling_filter_coefs(struct meson_drm *priv,
						 const unsigned int *coefs,
						 bool is_horizontal)
{
	int i;

	writel_relaxed(is_horizontal ? BIT(8) : 0,
			priv->io_base + _REG(VPP_OSD_SCALE_COEF_IDX));
	for (i = 0; i < 33; i++)
		writel_relaxed(coefs[i],
				priv->io_base + _REG(VPP_OSD_SCALE_COEF));
}

void meson_vpp_init(struct meson_drm *priv)
{
	/* set dummy data default YUV black */
	if (meson_vpu_is_compatible(priv, "amlogic,meson-gxl-vpu"))
		writel_relaxed(0x108080, priv->io_base + _REG(VPP_DUMMY_DATA1));
	else if (meson_vpu_is_compatible(priv, "amlogic,meson-gxm-vpu")) {
		writel_bits_relaxed(0xff << 16, 0xff << 16,
				    priv->io_base + _REG(VIU_MISC_CTRL1));
		writel_relaxed(0x20000, priv->io_base + _REG(VPP_DOLBY_CTRL));
		writel_relaxed(0x1020080,
				priv->io_base + _REG(VPP_DUMMY_DATA1));
	}

	/* Initialize vpu fifo control registers */
	writel_relaxed(readl_relaxed(priv->io_base + _REG(VPP_OFIFO_SIZE)) |
			0x77f, priv->io_base + _REG(VPP_OFIFO_SIZE));
	writel_relaxed(0x08080808, priv->io_base + _REG(VPP_HOLD_LINES));

	/* Turn off preblend */
	writel_bits_relaxed(VPP_PREBLEND_ENABLE, 0,
			    priv->io_base + _REG(VPP_MISC));

	/* Turn off POSTBLEND */
	writel_bits_relaxed(VPP_POSTBLEND_ENABLE, 0,
			    priv->io_base + _REG(VPP_MISC));

	/* Force all planes off */
	writel_bits_relaxed(VPP_OSD1_POSTBLEND | VPP_OSD2_POSTBLEND |
			    VPP_VD1_POSTBLEND | VPP_VD2_POSTBLEND, 0,
			    priv->io_base + _REG(VPP_MISC));

	/* Disable Scalers */
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_SC_CTRL0));
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_VSC_CTRL0));
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_HSC_CTRL0));

	/* Write in the proper filter coefficients. */
	meson_vpp_write_scaling_filter_coefs(priv,
				vpp_filter_coefs_4point_bspline, false);
	meson_vpp_write_scaling_filter_coefs(priv,
				vpp_filter_coefs_4point_bspline, true);
}
