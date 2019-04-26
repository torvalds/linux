/*
 * Copyright (C) 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * DOC: VC4 SDTV module
 *
 * The VEC encoder generates PAL or NTSC composite video output.
 *
 * TV mode selection is done by an atomic property on the encoder,
 * because a drm_mode_modeinfo is insufficient to distinguish between
 * PAL and PAL-M or NTSC and NTSC-J.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>

#include "vc4_drv.h"
#include "vc4_regs.h"

/* WSE Registers */
#define VEC_WSE_RESET			0xc0

#define VEC_WSE_CONTROL			0xc4
#define VEC_WSE_WSS_ENABLE		BIT(7)

#define VEC_WSE_WSS_DATA		0xc8
#define VEC_WSE_VPS_DATA1		0xcc
#define VEC_WSE_VPS_CONTROL		0xd0

/* VEC Registers */
#define VEC_REVID			0x100

#define VEC_CONFIG0			0x104
#define VEC_CONFIG0_YDEL_MASK		GENMASK(28, 26)
#define VEC_CONFIG0_YDEL(x)		((x) << 26)
#define VEC_CONFIG0_CDEL_MASK		GENMASK(25, 24)
#define VEC_CONFIG0_CDEL(x)		((x) << 24)
#define VEC_CONFIG0_PBPR_FIL		BIT(18)
#define VEC_CONFIG0_CHROMA_GAIN_MASK	GENMASK(17, 16)
#define VEC_CONFIG0_CHROMA_GAIN_UNITY	(0 << 16)
#define VEC_CONFIG0_CHROMA_GAIN_1_32	(1 << 16)
#define VEC_CONFIG0_CHROMA_GAIN_1_16	(2 << 16)
#define VEC_CONFIG0_CHROMA_GAIN_1_8	(3 << 16)
#define VEC_CONFIG0_CBURST_GAIN_MASK	GENMASK(14, 13)
#define VEC_CONFIG0_CBURST_GAIN_UNITY	(0 << 13)
#define VEC_CONFIG0_CBURST_GAIN_1_128	(1 << 13)
#define VEC_CONFIG0_CBURST_GAIN_1_64	(2 << 13)
#define VEC_CONFIG0_CBURST_GAIN_1_32	(3 << 13)
#define VEC_CONFIG0_CHRBW1		BIT(11)
#define VEC_CONFIG0_CHRBW0		BIT(10)
#define VEC_CONFIG0_SYNCDIS		BIT(9)
#define VEC_CONFIG0_BURDIS		BIT(8)
#define VEC_CONFIG0_CHRDIS		BIT(7)
#define VEC_CONFIG0_PDEN		BIT(6)
#define VEC_CONFIG0_YCDELAY		BIT(4)
#define VEC_CONFIG0_RAMPEN		BIT(2)
#define VEC_CONFIG0_YCDIS		BIT(2)
#define VEC_CONFIG0_STD_MASK		GENMASK(1, 0)
#define VEC_CONFIG0_NTSC_STD		0
#define VEC_CONFIG0_PAL_BDGHI_STD	1
#define VEC_CONFIG0_PAL_N_STD		3

#define VEC_SCHPH			0x108
#define VEC_SOFT_RESET			0x10c
#define VEC_CLMP0_START			0x144
#define VEC_CLMP0_END			0x148
#define VEC_FREQ3_2			0x180
#define VEC_FREQ1_0			0x184

#define VEC_CONFIG1			0x188
#define VEC_CONFIG_VEC_RESYNC_OFF	BIT(18)
#define VEC_CONFIG_RGB219		BIT(17)
#define VEC_CONFIG_CBAR_EN		BIT(16)
#define VEC_CONFIG_TC_OBB		BIT(15)
#define VEC_CONFIG1_OUTPUT_MODE_MASK	GENMASK(12, 10)
#define VEC_CONFIG1_C_Y_CVBS		(0 << 10)
#define VEC_CONFIG1_CVBS_Y_C		(1 << 10)
#define VEC_CONFIG1_PR_Y_PB		(2 << 10)
#define VEC_CONFIG1_RGB			(4 << 10)
#define VEC_CONFIG1_Y_C_CVBS		(5 << 10)
#define VEC_CONFIG1_C_CVBS_Y		(6 << 10)
#define VEC_CONFIG1_C_CVBS_CVBS		(7 << 10)
#define VEC_CONFIG1_DIS_CHR		BIT(9)
#define VEC_CONFIG1_DIS_LUMA		BIT(8)
#define VEC_CONFIG1_YCBCR_IN		BIT(6)
#define VEC_CONFIG1_DITHER_TYPE_LFSR	0
#define VEC_CONFIG1_DITHER_TYPE_COUNTER	BIT(5)
#define VEC_CONFIG1_DITHER_EN		BIT(4)
#define VEC_CONFIG1_CYDELAY		BIT(3)
#define VEC_CONFIG1_LUMADIS		BIT(2)
#define VEC_CONFIG1_COMPDIS		BIT(1)
#define VEC_CONFIG1_CUSTOM_FREQ		BIT(0)

#define VEC_CONFIG2			0x18c
#define VEC_CONFIG2_PROG_SCAN		BIT(15)
#define VEC_CONFIG2_SYNC_ADJ_MASK	GENMASK(14, 12)
#define VEC_CONFIG2_SYNC_ADJ(x)		(((x) / 2) << 12)
#define VEC_CONFIG2_PBPR_EN		BIT(10)
#define VEC_CONFIG2_UV_DIG_DIS		BIT(6)
#define VEC_CONFIG2_RGB_DIG_DIS		BIT(5)
#define VEC_CONFIG2_TMUX_MASK		GENMASK(3, 2)
#define VEC_CONFIG2_TMUX_DRIVE0		(0 << 2)
#define VEC_CONFIG2_TMUX_RG_COMP	(1 << 2)
#define VEC_CONFIG2_TMUX_UV_YC		(2 << 2)
#define VEC_CONFIG2_TMUX_SYNC_YC	(3 << 2)

#define VEC_INTERRUPT_CONTROL		0x190
#define VEC_INTERRUPT_STATUS		0x194
#define VEC_FCW_SECAM_B			0x198
#define VEC_SECAM_GAIN_VAL		0x19c

#define VEC_CONFIG3			0x1a0
#define VEC_CONFIG3_HORIZ_LEN_STD	(0 << 0)
#define VEC_CONFIG3_HORIZ_LEN_MPEG1_SIF	(1 << 0)
#define VEC_CONFIG3_SHAPE_NON_LINEAR	BIT(1)

#define VEC_STATUS0			0x200
#define VEC_MASK0			0x204

#define VEC_CFG				0x208
#define VEC_CFG_SG_MODE_MASK		GENMASK(6, 5)
#define VEC_CFG_SG_MODE(x)		((x) << 5)
#define VEC_CFG_SG_EN			BIT(4)
#define VEC_CFG_VEC_EN			BIT(3)
#define VEC_CFG_MB_EN			BIT(2)
#define VEC_CFG_ENABLE			BIT(1)
#define VEC_CFG_TB_EN			BIT(0)

#define VEC_DAC_TEST			0x20c

#define VEC_DAC_CONFIG			0x210
#define VEC_DAC_CONFIG_LDO_BIAS_CTRL(x)	((x) << 24)
#define VEC_DAC_CONFIG_DRIVER_CTRL(x)	((x) << 16)
#define VEC_DAC_CONFIG_DAC_CTRL(x)	(x)

#define VEC_DAC_MISC			0x214
#define VEC_DAC_MISC_VCD_CTRL_MASK	GENMASK(31, 16)
#define VEC_DAC_MISC_VCD_CTRL(x)	((x) << 16)
#define VEC_DAC_MISC_VID_ACT		BIT(8)
#define VEC_DAC_MISC_VCD_PWRDN		BIT(6)
#define VEC_DAC_MISC_BIAS_PWRDN		BIT(5)
#define VEC_DAC_MISC_DAC_PWRDN		BIT(2)
#define VEC_DAC_MISC_LDO_PWRDN		BIT(1)
#define VEC_DAC_MISC_DAC_RST_N		BIT(0)


/* General VEC hardware state. */
struct vc4_vec {
	struct platform_device *pdev;

	struct drm_encoder *encoder;
	struct drm_connector *connector;

	void __iomem *regs;

	struct clk *clock;

	const struct vc4_vec_tv_mode *tv_mode;

	struct debugfs_regset32 regset;
};

#define VEC_READ(offset) readl(vec->regs + (offset))
#define VEC_WRITE(offset, val) writel(val, vec->regs + (offset))

/* VC4 VEC encoder KMS struct */
struct vc4_vec_encoder {
	struct vc4_encoder base;
	struct vc4_vec *vec;
};

static inline struct vc4_vec_encoder *
to_vc4_vec_encoder(struct drm_encoder *encoder)
{
	return container_of(encoder, struct vc4_vec_encoder, base.base);
}

/* VC4 VEC connector KMS struct */
struct vc4_vec_connector {
	struct drm_connector base;
	struct vc4_vec *vec;

	/* Since the connector is attached to just the one encoder,
	 * this is the reference to it so we can do the best_encoder()
	 * hook.
	 */
	struct drm_encoder *encoder;
};

static inline struct vc4_vec_connector *
to_vc4_vec_connector(struct drm_connector *connector)
{
	return container_of(connector, struct vc4_vec_connector, base);
}

enum vc4_vec_tv_mode_id {
	VC4_VEC_TV_MODE_NTSC,
	VC4_VEC_TV_MODE_NTSC_J,
	VC4_VEC_TV_MODE_PAL,
	VC4_VEC_TV_MODE_PAL_M,
};

struct vc4_vec_tv_mode {
	const struct drm_display_mode *mode;
	void (*mode_set)(struct vc4_vec *vec);
};

static const struct debugfs_reg32 vec_regs[] = {
	VC4_REG32(VEC_WSE_CONTROL),
	VC4_REG32(VEC_WSE_WSS_DATA),
	VC4_REG32(VEC_WSE_VPS_DATA1),
	VC4_REG32(VEC_WSE_VPS_CONTROL),
	VC4_REG32(VEC_REVID),
	VC4_REG32(VEC_CONFIG0),
	VC4_REG32(VEC_SCHPH),
	VC4_REG32(VEC_CLMP0_START),
	VC4_REG32(VEC_CLMP0_END),
	VC4_REG32(VEC_FREQ3_2),
	VC4_REG32(VEC_FREQ1_0),
	VC4_REG32(VEC_CONFIG1),
	VC4_REG32(VEC_CONFIG2),
	VC4_REG32(VEC_INTERRUPT_CONTROL),
	VC4_REG32(VEC_INTERRUPT_STATUS),
	VC4_REG32(VEC_FCW_SECAM_B),
	VC4_REG32(VEC_SECAM_GAIN_VAL),
	VC4_REG32(VEC_CONFIG3),
	VC4_REG32(VEC_STATUS0),
	VC4_REG32(VEC_MASK0),
	VC4_REG32(VEC_CFG),
	VC4_REG32(VEC_DAC_TEST),
	VC4_REG32(VEC_DAC_CONFIG),
	VC4_REG32(VEC_DAC_MISC),
};

static void vc4_vec_ntsc_mode_set(struct vc4_vec *vec)
{
	VEC_WRITE(VEC_CONFIG0, VEC_CONFIG0_NTSC_STD | VEC_CONFIG0_PDEN);
	VEC_WRITE(VEC_CONFIG1, VEC_CONFIG1_C_CVBS_CVBS);
}

static void vc4_vec_ntsc_j_mode_set(struct vc4_vec *vec)
{
	VEC_WRITE(VEC_CONFIG0, VEC_CONFIG0_NTSC_STD);
	VEC_WRITE(VEC_CONFIG1, VEC_CONFIG1_C_CVBS_CVBS);
}

static const struct drm_display_mode ntsc_mode = {
	DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 13500,
		 720, 720 + 14, 720 + 14 + 64, 720 + 14 + 64 + 60, 0,
		 480, 480 + 3, 480 + 3 + 3, 480 + 3 + 3 + 16, 0,
		 DRM_MODE_FLAG_INTERLACE)
};

static void vc4_vec_pal_mode_set(struct vc4_vec *vec)
{
	VEC_WRITE(VEC_CONFIG0, VEC_CONFIG0_PAL_BDGHI_STD);
	VEC_WRITE(VEC_CONFIG1, VEC_CONFIG1_C_CVBS_CVBS);
}

static void vc4_vec_pal_m_mode_set(struct vc4_vec *vec)
{
	VEC_WRITE(VEC_CONFIG0, VEC_CONFIG0_PAL_BDGHI_STD);
	VEC_WRITE(VEC_CONFIG1,
		  VEC_CONFIG1_C_CVBS_CVBS | VEC_CONFIG1_CUSTOM_FREQ);
	VEC_WRITE(VEC_FREQ3_2, 0x223b);
	VEC_WRITE(VEC_FREQ1_0, 0x61d1);
}

static const struct drm_display_mode pal_mode = {
	DRM_MODE("720x576", DRM_MODE_TYPE_DRIVER, 13500,
		 720, 720 + 20, 720 + 20 + 64, 720 + 20 + 64 + 60, 0,
		 576, 576 + 2, 576 + 2 + 3, 576 + 2 + 3 + 20, 0,
		 DRM_MODE_FLAG_INTERLACE)
};

static const struct vc4_vec_tv_mode vc4_vec_tv_modes[] = {
	[VC4_VEC_TV_MODE_NTSC] = {
		.mode = &ntsc_mode,
		.mode_set = vc4_vec_ntsc_mode_set,
	},
	[VC4_VEC_TV_MODE_NTSC_J] = {
		.mode = &ntsc_mode,
		.mode_set = vc4_vec_ntsc_j_mode_set,
	},
	[VC4_VEC_TV_MODE_PAL] = {
		.mode = &pal_mode,
		.mode_set = vc4_vec_pal_mode_set,
	},
	[VC4_VEC_TV_MODE_PAL_M] = {
		.mode = &pal_mode,
		.mode_set = vc4_vec_pal_m_mode_set,
	},
};

static enum drm_connector_status
vc4_vec_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_unknown;
}

static void vc4_vec_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int vc4_vec_connector_get_modes(struct drm_connector *connector)
{
	struct drm_connector_state *state = connector->state;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev,
				  vc4_vec_tv_modes[state->tv.mode].mode);
	if (!mode) {
		DRM_ERROR("Failed to create a new display mode\n");
		return -ENOMEM;
	}

	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_connector_funcs vc4_vec_connector_funcs = {
	.detect = vc4_vec_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = vc4_vec_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs vc4_vec_connector_helper_funcs = {
	.get_modes = vc4_vec_connector_get_modes,
};

static struct drm_connector *vc4_vec_connector_init(struct drm_device *dev,
						    struct vc4_vec *vec)
{
	struct drm_connector *connector = NULL;
	struct vc4_vec_connector *vec_connector;

	vec_connector = devm_kzalloc(dev->dev, sizeof(*vec_connector),
				     GFP_KERNEL);
	if (!vec_connector)
		return ERR_PTR(-ENOMEM);

	connector = &vec_connector->base;
	connector->interlace_allowed = true;

	vec_connector->encoder = vec->encoder;
	vec_connector->vec = vec;

	drm_connector_init(dev, connector, &vc4_vec_connector_funcs,
			   DRM_MODE_CONNECTOR_Composite);
	drm_connector_helper_add(connector, &vc4_vec_connector_helper_funcs);

	drm_object_attach_property(&connector->base,
				   dev->mode_config.tv_mode_property,
				   VC4_VEC_TV_MODE_NTSC);
	vec->tv_mode = &vc4_vec_tv_modes[VC4_VEC_TV_MODE_NTSC];

	drm_connector_attach_encoder(connector, vec->encoder);

	return connector;
}

static const struct drm_encoder_funcs vc4_vec_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static void vc4_vec_encoder_disable(struct drm_encoder *encoder)
{
	struct vc4_vec_encoder *vc4_vec_encoder = to_vc4_vec_encoder(encoder);
	struct vc4_vec *vec = vc4_vec_encoder->vec;
	int ret;

	VEC_WRITE(VEC_CFG, 0);
	VEC_WRITE(VEC_DAC_MISC,
		  VEC_DAC_MISC_VCD_PWRDN |
		  VEC_DAC_MISC_BIAS_PWRDN |
		  VEC_DAC_MISC_DAC_PWRDN |
		  VEC_DAC_MISC_LDO_PWRDN);

	clk_disable_unprepare(vec->clock);

	ret = pm_runtime_put(&vec->pdev->dev);
	if (ret < 0) {
		DRM_ERROR("Failed to release power domain: %d\n", ret);
		return;
	}
}

static void vc4_vec_encoder_enable(struct drm_encoder *encoder)
{
	struct vc4_vec_encoder *vc4_vec_encoder = to_vc4_vec_encoder(encoder);
	struct vc4_vec *vec = vc4_vec_encoder->vec;
	int ret;

	ret = pm_runtime_get_sync(&vec->pdev->dev);
	if (ret < 0) {
		DRM_ERROR("Failed to retain power domain: %d\n", ret);
		return;
	}

	/*
	 * We need to set the clock rate each time we enable the encoder
	 * because there's a chance we share the same parent with the HDMI
	 * clock, and both drivers are requesting different rates.
	 * The good news is, these 2 encoders cannot be enabled at the same
	 * time, thus preventing incompatible rate requests.
	 */
	ret = clk_set_rate(vec->clock, 108000000);
	if (ret) {
		DRM_ERROR("Failed to set clock rate: %d\n", ret);
		return;
	}

	ret = clk_prepare_enable(vec->clock);
	if (ret) {
		DRM_ERROR("Failed to turn on core clock: %d\n", ret);
		return;
	}

	/* Reset the different blocks */
	VEC_WRITE(VEC_WSE_RESET, 1);
	VEC_WRITE(VEC_SOFT_RESET, 1);

	/* Disable the CGSM-A and WSE blocks */
	VEC_WRITE(VEC_WSE_CONTROL, 0);

	/* Write config common to all modes. */

	/*
	 * Color subcarrier phase: phase = 360 * SCHPH / 256.
	 * 0x28 <=> 39.375 deg.
	 */
	VEC_WRITE(VEC_SCHPH, 0x28);

	/*
	 * Reset to default values.
	 */
	VEC_WRITE(VEC_CLMP0_START, 0xac);
	VEC_WRITE(VEC_CLMP0_END, 0xec);
	VEC_WRITE(VEC_CONFIG2,
		  VEC_CONFIG2_UV_DIG_DIS | VEC_CONFIG2_RGB_DIG_DIS);
	VEC_WRITE(VEC_CONFIG3, VEC_CONFIG3_HORIZ_LEN_STD);
	VEC_WRITE(VEC_DAC_CONFIG,
		  VEC_DAC_CONFIG_DAC_CTRL(0xc) |
		  VEC_DAC_CONFIG_DRIVER_CTRL(0xc) |
		  VEC_DAC_CONFIG_LDO_BIAS_CTRL(0x46));

	/* Mask all interrupts. */
	VEC_WRITE(VEC_MASK0, 0);

	vec->tv_mode->mode_set(vec);

	VEC_WRITE(VEC_DAC_MISC,
		  VEC_DAC_MISC_VID_ACT | VEC_DAC_MISC_DAC_RST_N);
	VEC_WRITE(VEC_CFG, VEC_CFG_VEC_EN);
}


static bool vc4_vec_encoder_mode_fixup(struct drm_encoder *encoder,
				       const struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void vc4_vec_encoder_atomic_mode_set(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct vc4_vec_encoder *vc4_vec_encoder = to_vc4_vec_encoder(encoder);
	struct vc4_vec *vec = vc4_vec_encoder->vec;

	vec->tv_mode = &vc4_vec_tv_modes[conn_state->tv.mode];
}

static int vc4_vec_encoder_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	const struct vc4_vec_tv_mode *vec_mode;

	vec_mode = &vc4_vec_tv_modes[conn_state->tv.mode];

	if (conn_state->crtc &&
	    !drm_mode_equal(vec_mode->mode, &crtc_state->adjusted_mode))
		return -EINVAL;

	return 0;
}

static const struct drm_encoder_helper_funcs vc4_vec_encoder_helper_funcs = {
	.disable = vc4_vec_encoder_disable,
	.enable = vc4_vec_encoder_enable,
	.mode_fixup = vc4_vec_encoder_mode_fixup,
	.atomic_check = vc4_vec_encoder_atomic_check,
	.atomic_mode_set = vc4_vec_encoder_atomic_mode_set,
};

static const struct of_device_id vc4_vec_dt_match[] = {
	{ .compatible = "brcm,bcm2835-vec", .data = NULL },
	{ /* sentinel */ },
};

static const char * const tv_mode_names[] = {
	[VC4_VEC_TV_MODE_NTSC] = "NTSC",
	[VC4_VEC_TV_MODE_NTSC_J] = "NTSC-J",
	[VC4_VEC_TV_MODE_PAL] = "PAL",
	[VC4_VEC_TV_MODE_PAL_M] = "PAL-M",
};

static int vc4_vec_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct vc4_vec *vec;
	struct vc4_vec_encoder *vc4_vec_encoder;
	int ret;

	ret = drm_mode_create_tv_properties(drm, ARRAY_SIZE(tv_mode_names),
					    tv_mode_names);
	if (ret)
		return ret;

	vec = devm_kzalloc(dev, sizeof(*vec), GFP_KERNEL);
	if (!vec)
		return -ENOMEM;

	vc4_vec_encoder = devm_kzalloc(dev, sizeof(*vc4_vec_encoder),
				       GFP_KERNEL);
	if (!vc4_vec_encoder)
		return -ENOMEM;
	vc4_vec_encoder->base.type = VC4_ENCODER_TYPE_VEC;
	vc4_vec_encoder->vec = vec;
	vec->encoder = &vc4_vec_encoder->base.base;

	vec->pdev = pdev;
	vec->regs = vc4_ioremap_regs(pdev, 0);
	if (IS_ERR(vec->regs))
		return PTR_ERR(vec->regs);
	vec->regset.base = vec->regs;
	vec->regset.regs = vec_regs;
	vec->regset.nregs = ARRAY_SIZE(vec_regs);

	vec->clock = devm_clk_get(dev, NULL);
	if (IS_ERR(vec->clock)) {
		ret = PTR_ERR(vec->clock);
		if (ret != -EPROBE_DEFER)
			DRM_ERROR("Failed to get clock: %d\n", ret);
		return ret;
	}

	pm_runtime_enable(dev);

	drm_encoder_init(drm, vec->encoder, &vc4_vec_encoder_funcs,
			 DRM_MODE_ENCODER_TVDAC, NULL);
	drm_encoder_helper_add(vec->encoder, &vc4_vec_encoder_helper_funcs);

	vec->connector = vc4_vec_connector_init(drm, vec);
	if (IS_ERR(vec->connector)) {
		ret = PTR_ERR(vec->connector);
		goto err_destroy_encoder;
	}

	dev_set_drvdata(dev, vec);

	vc4->vec = vec;

	vc4_debugfs_add_regset32(drm, "vec_regs", &vec->regset);

	return 0;

err_destroy_encoder:
	drm_encoder_cleanup(vec->encoder);
	pm_runtime_disable(dev);

	return ret;
}

static void vc4_vec_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct vc4_vec *vec = dev_get_drvdata(dev);

	vc4_vec_connector_destroy(vec->connector);
	drm_encoder_cleanup(vec->encoder);
	pm_runtime_disable(dev);

	vc4->vec = NULL;
}

static const struct component_ops vc4_vec_ops = {
	.bind   = vc4_vec_bind,
	.unbind = vc4_vec_unbind,
};

static int vc4_vec_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &vc4_vec_ops);
}

static int vc4_vec_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vc4_vec_ops);
	return 0;
}

struct platform_driver vc4_vec_driver = {
	.probe = vc4_vec_dev_probe,
	.remove = vc4_vec_dev_remove,
	.driver = {
		.name = "vc4_vec",
		.of_match_table = vc4_vec_dt_match,
	},
};
