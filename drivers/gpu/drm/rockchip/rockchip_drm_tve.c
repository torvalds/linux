/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/mutex.h>
#include <linux/mfd/syscon.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include <uapi/linux/videodev2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_tve.h"
#include "rockchip_drm_vop.h"

#define RK322X_VDAC_STANDARD 0x15

static const struct drm_display_mode cvbs_mode[] = {
	{ DRM_MODE("720x576i", DRM_MODE_TYPE_DRIVER, 13500, 720, 753,
		   816, 864, 0, 576, 580, 586, 625, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
		   0, },

	{ DRM_MODE("720x480i", DRM_MODE_TYPE_DRIVER, 13500, 720, 753,
		   815, 858, 0, 480, 480, 486, 525, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
		   0, },
};

struct env_config {
	u32 offset;
	u32 value;
};

static struct env_config ntsc_bt656_config[] = {
	{ BT656_DECODER_CROP, 0x00000000 },
	{ BT656_DECODER_SIZE, 0x01e002d0 },
	{ BT656_DECODER_HTOTAL_HS_END, 0x035a003e },
	{ BT656_DECODER_VACT_ST_HACT_ST, 0x00160069 },
	{ BT656_DECODER_VTOTAL_VS_END, 0x020d0003 },
	{ BT656_DECODER_VS_ST_END_F1, 0x01060109 },
	{ BT656_DECODER_DBG_REG, 0x024002d0 },
	{ BT656_DECODER_CTRL, 0x00000009 },
};

static struct env_config ntsc_tve_config[] = {
	{ TVE_MODE_CTRL, 0x000af906 },
	{ TVE_HOR_TIMING1, 0x00c07a81 },
	{ TVE_HOR_TIMING2, 0x169810fc },
	{ TVE_HOR_TIMING3, 0x96b40000 },
	{ TVE_SUB_CAR_FRQ, 0x21f07bd7 },
	{ TVE_IMAGE_POSITION, 0x001500d6 },
	{ TVE_ROUTING, 0x10088880 },
	{ TVE_SYNC_ADJUST, 0x00000000 },
	{ TVE_STATUS, 0x00000000 },
	{ TVE_CTRL, 0x00000000 },
	{ TVE_INTR_STATUS, 0x00000000 },
	{ TVE_INTR_EN, 0x00000000 },
	{ TVE_INTR_CLR, 0x00000000 },
	{ TVE_COLOR_BUSRT_SAT, 0x0052543c },
	{ TVE_CHROMA_BANDWIDTH, 0x00000002 },
	{ TVE_BRIGHTNESS_CONTRAST, 0x00008300 },
	{ TVE_CLAMP, 0x00000000 },
};

static struct env_config pal_bt656_config[] = {
	{ BT656_DECODER_CROP, 0x00000000 },
	{ BT656_DECODER_SIZE, 0x024002d0 },
	{ BT656_DECODER_HTOTAL_HS_END, 0x0360003f },
	{ BT656_DECODER_VACT_ST_HACT_ST, 0x0016006f },
	{ BT656_DECODER_VTOTAL_VS_END, 0x02710003 },
	{ BT656_DECODER_VS_ST_END_F1, 0x0138013b },
	{ BT656_DECODER_DBG_REG, 0x024002d0 },
	{ BT656_DECODER_CTRL, 0x00000009 },
};

static struct env_config pal_tve_config[] = {
	{ TVE_MODE_CTRL, 0x010ab906 },
	{ TVE_HOR_TIMING1, 0x00c28381 },
	{ TVE_HOR_TIMING2, 0x267d111d },
	{ TVE_HOR_TIMING3, 0x66c00880 },
	{ TVE_SUB_CAR_FRQ, 0x2a098acb },
	{ TVE_IMAGE_POSITION, 0x001500f6 },
	{ TVE_ROUTING, 0x10008882 },
	{ TVE_SYNC_ADJUST, 0x00000000 },
	{ TVE_STATUS, 0x000000b0 },
	{ TVE_CTRL, 0x00000000 },
	{ TVE_INTR_STATUS, 0x00000000 },
	{ TVE_INTR_EN, 0x00000000 },
	{ TVE_INTR_CLR, 0x00000000 },
	{ TVE_COLOR_BUSRT_SAT, 0x00356245 },
	{ TVE_CHROMA_BANDWIDTH, 0x00000022 },
	{ TVE_BRIGHTNESS_CONTRAST, 0x0000aa00 },
	{ TVE_CLAMP, 0x00000000 },
};

#define BT656_ENV_CONFIG_SIZE		(sizeof(ntsc_bt656_config) / sizeof(struct env_config))
#define TVE_ENV_CONFIG_SIZE		(sizeof(ntsc_tve_config) / sizeof(struct env_config))

#define tve_writel(offset, v)		writel_relaxed(v, tve->regbase + (offset))
#define tve_readl(offset)		readl_relaxed(tve->regbase + (offset))

#define tve_dac_writel(offset, v)	writel_relaxed(v, tve->vdacbase + (offset))
#define tve_dac_readl(offset)		readl_relaxed(tve->vdacbase + (offset))

#define tve_dac_grf_writel(offset, v)	regmap_write(tve->dac_grf, offset, v)
#define tve_dac_grf_readl(offset, v)	regmap_read(tve->dac_grf, offset, v)

#define connector_to_tve(x)		container_of(x, struct rockchip_tve, connector)
#define encoder_to_tve(x)		container_of(x, struct rockchip_tve, encoder)

struct rockchip_tve_data {
	int input_format;
	int soc_type;
};

static void tve_write_block(struct rockchip_tve *tve, struct env_config *config, int len)
{
	int i;

	for (i = 0; i < len; i++)
		tve_writel(config[i].offset, config[i].value);
}

static int
rockchip_tve_get_modes(struct drm_connector *connector)
{
	int count;
	struct rockchip_tve *tve = connector_to_tve(connector);

	for (count = 0; count < ARRAY_SIZE(cvbs_mode); count++) {
		struct drm_display_mode *mode_ptr;

		mode_ptr = drm_mode_duplicate(connector->dev,
					      &cvbs_mode[count]);
		if (tve->preferred_mode == count)
			mode_ptr->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode_ptr);
	}

	return count;
}

static enum drm_mode_status
rockchip_tve_mode_valid(struct drm_connector *connector,
			struct drm_display_mode *mode)
{
	return MODE_OK;
}

static struct drm_encoder *rockchip_tve_best_encoder(struct drm_connector
						     *connector)
{
	struct rockchip_tve *tve = connector_to_tve(connector);

	return &tve->encoder;
}

static void rockchip_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static enum drm_connector_status
rockchip_tve_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rockchip_tve_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static void tve_set_mode(struct rockchip_tve *tve)
{
	struct env_config *bt656_cfg, *tve_cfg;
	int mode = tve->tv_format;

	if (tve->soc_type == SOC_RK3528) {
		tve_writel(TVE_LUMA_FILTER1, tve->lumafilter0);
		tve_writel(TVE_LUMA_FILTER2, tve->lumafilter1);
		tve_writel(TVE_LUMA_FILTER3, tve->lumafilter2);
		tve_writel(TVE_LUMA_FILTER4, tve->lumafilter3);
		tve_writel(TVE_LUMA_FILTER5, tve->lumafilter4);
		tve_writel(TVE_LUMA_FILTER6, tve->lumafilter5);
		tve_writel(TVE_LUMA_FILTER7, tve->lumafilter6);
		tve_writel(TVE_LUMA_FILTER8, tve->lumafilter7);
	} else {
		dev_dbg(tve->dev, "tve set mode:%d\n", mode);
		if (tve->input_format == INPUT_FORMAT_RGB)
			tve_writel(TV_CTRL, v_CVBS_MODE(mode) | v_CLK_UPSTREAM_EN(2) |
				   v_TIMING_EN(2) | v_LUMA_FILTER_GAIN(0) |
				   v_LUMA_FILTER_UPSAMPLE(1) | v_CSC_PATH(0));
		else
			tve_writel(TV_CTRL, v_CVBS_MODE(mode) | v_CLK_UPSTREAM_EN(2) |
				   v_TIMING_EN(2) | v_LUMA_FILTER_GAIN(0) |
				   v_LUMA_FILTER_UPSAMPLE(1) | v_CSC_PATH(3));

		tve_writel(TV_LUMA_FILTER0, tve->lumafilter0);
		tve_writel(TV_LUMA_FILTER1, tve->lumafilter1);
		tve_writel(TV_LUMA_FILTER2, tve->lumafilter2);
	}

	if (mode == TVOUT_CVBS_NTSC) {
		dev_dbg(tve->dev, "NTSC MODE\n");

		if (tve->soc_type == SOC_RK3528) {
			bt656_cfg = ntsc_bt656_config;
			tve_cfg = ntsc_tve_config;

			tve_write_block(tve, bt656_cfg, BT656_ENV_CONFIG_SIZE);
			tve_write_block(tve, tve_cfg, TVE_ENV_CONFIG_SIZE);
		} else {
			tve_writel(TV_ROUTING, v_DAC_SENSE_EN(0) | v_Y_IRE_7_5(1) |
				   v_Y_AGC_PULSE_ON(0) | v_Y_VIDEO_ON(1) |
				   v_YPP_MODE(1) | v_Y_SYNC_ON(1) | v_PIC_MODE(mode));
			tve_writel(TV_BW_CTRL, v_CHROMA_BW(BP_FILTER_NTSC) |
				   v_COLOR_DIFF_BW(COLOR_DIFF_FILTER_BW_1_3));
			tve_writel(TV_SATURATION, 0x0042543C);
			if (tve->test_mode)
				tve_writel(TV_BRIGHTNESS_CONTRAST, 0x00008300);
			else
				tve_writel(TV_BRIGHTNESS_CONTRAST, 0x00007900);

			tve_writel(TV_FREQ_SC,	0x21F07BD7);
			tve_writel(TV_SYNC_TIMING, 0x00C07a81);
			tve_writel(TV_ADJ_TIMING, 0x96B40000 | 0x70);
			tve_writel(TV_ACT_ST,	0x001500D6);
			tve_writel(TV_ACT_TIMING, 0x069800FC | (1 << 12) | (1 << 28));
		}
	} else if (mode == TVOUT_CVBS_PAL) {
		dev_dbg(tve->dev, "PAL MODE\n");

		if (tve->soc_type == SOC_RK3528) {
			bt656_cfg = pal_bt656_config;
			tve_cfg = pal_tve_config;

			tve_write_block(tve, bt656_cfg, BT656_ENV_CONFIG_SIZE);
			tve_write_block(tve, tve_cfg, TVE_ENV_CONFIG_SIZE);
		} else {
			tve_writel(TV_ROUTING, v_DAC_SENSE_EN(0) | v_Y_IRE_7_5(0) |
				   v_Y_AGC_PULSE_ON(0) | v_Y_VIDEO_ON(1) |
				   v_YPP_MODE(1) | v_Y_SYNC_ON(1) | v_PIC_MODE(mode));
			tve_writel(TV_BW_CTRL, v_CHROMA_BW(BP_FILTER_PAL) |
				   v_COLOR_DIFF_BW(COLOR_DIFF_FILTER_BW_1_3));

			tve_writel(TV_SATURATION, tve->saturation);
			tve_writel(TV_BRIGHTNESS_CONTRAST, tve->brightcontrast);

			tve_writel(TV_FREQ_SC,	0x2A098ACB);
			tve_writel(TV_SYNC_TIMING, 0x00C28381);
			tve_writel(TV_ADJ_TIMING, (0xc << 28) | 0x06c00800 | 0x80);
			tve_writel(TV_ACT_ST,	0x001500F6);
			tve_writel(TV_ACT_TIMING, 0x0694011D | (1 << 12) | (2 << 28));

			tve_writel(TV_ADJ_TIMING, tve->adjtiming);
			tve_writel(TV_ACT_TIMING, 0x0694011D | (1 << 12) | (2 << 28));
		}
	}

	if (tve->soc_type == SOC_RK3528) {
		u32 upsample_mode = 0;
		u32 mask = 0;
		u32 val = 0;
		bool upsample_en;

		upsample_en = tve->upsample_mode ? 1 : 0;
		if (upsample_en)
			upsample_mode = tve->upsample_mode - 1;
		mask = m_TVE_DCLK_POL | m_TVE_DCLK_EN | m_DCLK_UPSAMPLE_2X4X |
		       m_DCLK_UPSAMPLE_EN | m_TVE_MODE | m_TVE_EN;
		val = v_TVE_DCLK_POL(0) | v_TVE_DCLK_EN(1) | v_DCLK_UPSAMPLE_2X4X(upsample_mode) |
		      v_DCLK_UPSAMPLE_EN(upsample_en) | v_TVE_MODE(tve->tv_format) | v_TVE_EN(1);

		tve_dac_grf_writel(RK3528_VO_GRF_CVBS_CON, (mask << 16) | val);
	}
}

static void dac_init(struct rockchip_tve *tve)
{
	tve_dac_writel(VDAC_VDAC1, v_CUR_REG(tve->dac1level) |
				   m_DR_PWR_DOWN | m_BG_PWR_DOWN);
	tve_dac_writel(VDAC_VDAC2, v_CUR_CTR(tve->daclevel));
	tve_dac_writel(VDAC_VDAC3, v_CAB_EN(0));
}

static void dac_enable(struct rockchip_tve *tve, bool enable)
{
	u32 mask = 0;
	u32 val = 0;
	u32 grfreg = 0;
	u32 offset = 0;

	if (enable) {
		dev_dbg(tve->dev, "dac enable\n");

		if (tve->soc_type == SOC_RK3036) {
			mask = m_VBG_EN | m_DAC_EN | m_DAC_GAIN;
			val = m_VBG_EN | m_DAC_EN | v_DAC_GAIN(tve->daclevel);
			grfreg = RK3036_GRF_SOC_CON3;
		} else if (tve->soc_type == SOC_RK312X) {
			mask = m_VBG_EN | m_DAC_EN | m_DAC_GAIN;
			val = m_VBG_EN | m_DAC_EN | v_DAC_GAIN(tve->daclevel);
			grfreg = RK312X_GRF_TVE_CON;
		} else if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328) {
			val = v_CUR_REG(tve->dac1level) | v_DR_PWR_DOWN(0) | v_BG_PWR_DOWN(0);
			offset = VDAC_VDAC1;
		} else if (tve->soc_type == SOC_RK3528) {
			/*
			 * Reset the vdac
			 */
			tve_dac_writel(VDAC_CLK_RST, v_ANALOG_RST(0) | v_DIGITAL_RST(0));
			msleep(20);
			tve_dac_writel(VDAC_CLK_RST, v_ANALOG_RST(1) | v_DIGITAL_RST(1));

			tve_dac_writel(VDAC_CURRENT_CTRL, v_OUT_CURRENT(tve->vdac_out_current));

			val = v_REF_VOLTAGE(7) | v_DAC_PWN(1) | v_BIAS_PWN(1);
			offset = VDAC_PWM_REF_CTRL;
		}
	} else {
		dev_dbg(tve->dev, "dac disable\n");

		if (tve->soc_type == SOC_RK312X) {
			mask = m_VBG_EN | m_DAC_EN;
			grfreg = RK312X_GRF_TVE_CON;
		} else if (tve->soc_type == SOC_RK3036) {
			mask = m_VBG_EN | m_DAC_EN;
			grfreg = RK3036_GRF_SOC_CON3;
		} else if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328) {
			val = v_CUR_REG(tve->dac1level) | m_DR_PWR_DOWN | m_BG_PWR_DOWN;
			offset = VDAC_VDAC1;
		} else if (tve->soc_type == SOC_RK3528) {
			val = v_DAC_PWN(0) | v_BIAS_PWN(0);
			offset = VDAC_PWM_REF_CTRL;
		}
	}

	if (grfreg)
		tve_dac_grf_writel(grfreg, (mask << 16) | val);
	else if (tve->vdacbase)
		tve_dac_writel(offset, val);
}

static int cvbs_set_disable(struct rockchip_tve *tve)
{
	int ret = 0;

	dev_dbg(tve->dev, "%s\n", __func__);
	if (!tve->enable)
		return 0;

	ret = pm_runtime_put(tve->dev);
	if (ret < 0) {
		dev_err(tve->dev, "failed to put pm runtime: %d\n", ret);
		return ret;
	}
	dac_enable(tve, false);
	tve->enable = 0;

	return 0;
}

/*
 * RK3528 supports bt656 to cvbs, and the others support rgb to cvbs.
 *
 *  ┌──────────┐
 *  │ rgb data ├─────────────────────────────────────┐
 *  └──────────┘                                     │
 *                                                   ▼
 * ┌────────────┐    ┌───────────────┐    ┌───────────────────┐    ┌──────┐    ┌────────┐
 * │ bt656 data ├───►│ bt656 decoder ├───►│ cvbs(tve) encoder ├───►│ vdac ├───►│ screen │
 * └────────────┘    └───────────────┘    └───────────────────┘    └──────┘    └────────┘
 *
 */
static int cvbs_set_enable(struct rockchip_tve *tve)
{
	int ret = 0;

	dev_dbg(tve->dev, "%s\n", __func__);
	if (tve->enable)
		return 0;

	ret = pm_runtime_get_sync(tve->dev);
	if (ret < 0) {
		dev_err(tve->dev, "failed to get pm runtime: %d\n", ret);
		return ret;
	}
	tve_set_mode(tve);
	msleep(1000);
	dac_enable(tve, true);
	tve->enable = 1;

	return 0;
}

static void rockchip_tve_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_tve *tve = encoder_to_tve(encoder);

	mutex_lock(&tve->suspend_lock);

	dev_dbg(tve->dev, "tve encoder enable\n");
	cvbs_set_enable(tve);

	mutex_unlock(&tve->suspend_lock);
}

static void rockchip_tve_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_tve *tve = encoder_to_tve(encoder);

	mutex_lock(&tve->suspend_lock);

	dev_dbg(tve->dev, "tve encoder enable\n");
	cvbs_set_disable(tve);

	mutex_unlock(&tve->suspend_lock);
}

static void rockchip_tve_encoder_mode_set(struct drm_encoder *encoder,
					  struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct rockchip_tve *tve = encoder_to_tve(encoder);

	dev_dbg(tve->dev, "encoder mode set:%s\n", adjusted_mode->name);

	if (adjusted_mode->vdisplay == 576)
		tve->tv_format = TVOUT_CVBS_PAL;
	else
		tve->tv_format = TVOUT_CVBS_NTSC;

	if (tve->enable) {
		dac_enable(tve, false);
		msleep(200);

		tve_set_mode(tve);
		dac_enable(tve, true);
	}
}

static bool
rockchip_tve_encoder_mode_fixup(struct drm_encoder *encoder,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int
rockchip_tve_encoder_atomic_check(struct drm_encoder *encoder,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct rockchip_tve *tve = encoder_to_tve(encoder);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;

	s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_type = DRM_MODE_CONNECTOR_TV;
	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_YUV8_1X24;

	/*
	 * For RK3528:
	 * VOP -> BT656 output -> BT656 decoder -> TVE encoder -> CVBS output
	 */
	if (tve->soc_type == SOC_RK3528)
		s->output_if |= VOP_OUTPUT_IF_BT656;
	s->color_space = V4L2_COLORSPACE_SMPTE170M;
	s->tv_state = &conn_state->tv;

	return 0;
}

static const struct drm_connector_helper_funcs
rockchip_tve_connector_helper_funcs = {
	.mode_valid = rockchip_tve_mode_valid,
	.get_modes = rockchip_tve_get_modes,
	.best_encoder = rockchip_tve_best_encoder,
};

static const struct drm_encoder_funcs rockchip_tve_encoder_funcs = {
	.destroy = rockchip_encoder_destroy,
};

static const struct drm_connector_funcs rockchip_tve_connector_funcs = {
	.detect = rockchip_tve_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rockchip_tve_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_encoder_helper_funcs
rockchip_tve_encoder_helper_funcs = {
	.mode_fixup = rockchip_tve_encoder_mode_fixup,
	.mode_set = rockchip_tve_encoder_mode_set,
	.enable = rockchip_tve_encoder_enable,
	.disable = rockchip_tve_encoder_disable,
	.atomic_check = rockchip_tve_encoder_atomic_check,
};

static int tve_read_otp_by_name(struct rockchip_tve *tve, char *name, u8 *val, u8 default_val)
{
	struct nvmem_cell *cell;
	size_t len;
	unsigned char *efuse_buf;
	int ret = -EINVAL;

	*val = default_val;
	cell = nvmem_cell_get(tve->dev, name);
	if (!IS_ERR(cell)) {
		efuse_buf = nvmem_cell_read(cell, &len);
		nvmem_cell_put(cell);
		if (!IS_ERR(efuse_buf)) {
			*val = efuse_buf[0];
			kfree(efuse_buf);
			return 0;
		}
	}

	dev_err(tve->dev, "failed to read %s from otp, use default\n", name);

	return ret;
}

static int tve_parse_dt(struct device_node *np, struct rockchip_tve *tve)
{
	int ret, val;
	u8 out_current, version;

	ret = of_property_read_u32(np, "rockchip,tvemode", &val);
	if (ret < 0) {
		tve->preferred_mode = 0;
	} else if (val > 1) {
		dev_err(tve->dev, "tve mode value invalid\n");
		return -EINVAL;
	}
	tve->preferred_mode = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter0", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter0 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter1", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter1 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter2", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter2 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter3", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter3 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter4", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter4 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter5", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter5 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter6", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter6 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter7", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter7 = val;

	ret = of_property_read_u32(np, "rockchip,tve-upsample", &val);
	if (val > DCLK_UPSAMPLEx4 || ret < 0)
		return -EINVAL;
	tve->upsample_mode = val;

	/*
	 * Read vdac output current from OTP if exists, and the default
	 * current val is 0xd2.
	 */
	ret = tve_read_otp_by_name(tve, "out-current", &out_current, 0xd2);
	if (!ret) {
		if (out_current) {
			/*
			 * If test version is 0x0, the value of vdac out current
			 * needs to be reduced by one.
			 */
			ret = tve_read_otp_by_name(tve, "version", &version, 0x0);
			if (!ret) {
				if (version == 0x0)
					out_current -= 1;
			}
		} else {
			/*
			 * If the current value read from OTP is 0, set it to default.
			 */
			out_current = 0xd2;
		}
	}
	tve->vdac_out_current = out_current;

	return 0;
}

static int tve_parse_dt_legacy(struct device_node *np, struct rockchip_tve *tve)
{
	int ret, val;
	u32 getdac = 0;
	size_t len;
	struct nvmem_cell *cell;
	unsigned char *efuse_buf;

	ret = of_property_read_u32(np, "rockchip,tvemode", &val);
	if (ret < 0) {
		tve->preferred_mode = 0;
	} else if (val > 1) {
		dev_err(tve->dev, "tve mode value invalid\n");
		return -EINVAL;
	}
	tve->preferred_mode = val;

	ret = of_property_read_u32(np, "rockchip,saturation", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->saturation = val;

	ret = of_property_read_u32(np, "rockchip,brightcontrast", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->brightcontrast = val;

	ret = of_property_read_u32(np, "rockchip,adjtiming", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->adjtiming = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter0", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter0 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter1", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter1 = val;

	ret = of_property_read_u32(np, "rockchip,lumafilter2", &val);
	if (val == 0 || ret < 0)
		return -EINVAL;
	tve->lumafilter2 = val;

	ret = of_property_read_u32(np, "rockchip,daclevel", &val);
	if (val == 0 || ret < 0) {
		return -EINVAL;
	} else {
		tve->daclevel = val;
		if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328) {
			cell = nvmem_cell_get(tve->dev, "tve_dac_adj");
			if (IS_ERR(cell)) {
				dev_dbg(tve->dev, "failed to get id cell: %ld\n", PTR_ERR(cell));
			} else {
				efuse_buf = nvmem_cell_read(cell, &len);
				nvmem_cell_put(cell);
				if (IS_ERR(efuse_buf))
					return PTR_ERR(efuse_buf);
				if (len == 1)
					getdac = efuse_buf[0];
				kfree(efuse_buf);

				if (getdac > 0) {
					tve->daclevel = getdac + 5 + val - RK322X_VDAC_STANDARD;
					if (tve->daclevel > 0x3f) {
						dev_err(tve->dev, "rk322x daclevel error!\n");
						tve->daclevel = val;
					}
				}
			}
		}
	}

	if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328) {
		ret = of_property_read_u32(np, "rockchip,dac1level", &val);
		if ((val == 0) || (ret < 0))
			return -EINVAL;
		tve->dac1level = val;
	}

	return 0;
}

static bool tve_check_lumafilter(struct rockchip_tve *tve)
{
	int lumafilter[8] = {INT_MAX};

	/*
	 * The default lumafilter value is 0. If lumafilter value
	 * is equal to the dts value, uboot logo is enabled.
	 */
	if (tve->soc_type == SOC_RK3528) {
		lumafilter[0] = tve_readl(TVE_LUMA_FILTER1);
		lumafilter[1] = tve_readl(TVE_LUMA_FILTER2);
		lumafilter[2] = tve_readl(TVE_LUMA_FILTER3);
		lumafilter[3] = tve_readl(TVE_LUMA_FILTER4);
		lumafilter[4] = tve_readl(TVE_LUMA_FILTER5);
		lumafilter[5] = tve_readl(TVE_LUMA_FILTER6);
		lumafilter[6] = tve_readl(TVE_LUMA_FILTER7);
		lumafilter[7] = tve_readl(TVE_LUMA_FILTER8);

		if (lumafilter[0] == tve->lumafilter0 &&
		    lumafilter[1] == tve->lumafilter1 &&
		    lumafilter[2] == tve->lumafilter2 &&
		    lumafilter[3] == tve->lumafilter3 &&
		    lumafilter[4] == tve->lumafilter4 &&
		    lumafilter[5] == tve->lumafilter5 &&
		    lumafilter[6] == tve->lumafilter6 &&
		    lumafilter[7] == tve->lumafilter7) {
			return true;
		}
	} else {
		lumafilter[0] = tve_readl(TV_LUMA_FILTER0);
		lumafilter[1] = tve_readl(TV_LUMA_FILTER1);
		lumafilter[2] = tve_readl(TV_LUMA_FILTER2);

		if (lumafilter[0] == tve->lumafilter0 &&
		    lumafilter[1] == tve->lumafilter1 &&
		    lumafilter[2] == tve->lumafilter2) {
			return true;
		}
	}

	return false;
}

static void check_uboot_logo(struct rockchip_tve *tve)
{
	int vdac;

	if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328) {
		vdac = tve_dac_readl(VDAC_VDAC1);
		/* Whether the dac power has been turned down. */
		if (vdac & m_DR_PWR_DOWN) {
			tve->connector.dpms = DRM_MODE_DPMS_OFF;
			return;
		}
	}

	if (tve_check_lumafilter(tve)) {
		tve->connector.dpms = DRM_MODE_DPMS_ON;
		return;
	}

	if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328)
		dac_init(tve);

	tve->connector.dpms = DRM_MODE_DPMS_OFF;
}

static const struct rockchip_tve_data rk3036_tve = {
	.soc_type = SOC_RK3036,
	.input_format = INPUT_FORMAT_RGB,
};

static const struct rockchip_tve_data rk312x_tve = {
	.soc_type = SOC_RK312X,
	.input_format = INPUT_FORMAT_RGB,
};

static const struct rockchip_tve_data rk322x_tve = {
	.soc_type = SOC_RK322X,
	.input_format = INPUT_FORMAT_YUV,
};

static const struct rockchip_tve_data rk3328_tve = {
	.soc_type = SOC_RK3328,
	.input_format = INPUT_FORMAT_YUV,
};

static const struct rockchip_tve_data rk3528_tve = {
	.soc_type = SOC_RK3528,
	.input_format = INPUT_FORMAT_YUV,
};

static const struct of_device_id rockchip_tve_dt_ids[] = {
	{ .compatible = "rockchip,rk3036-tve", .data = &rk3036_tve },
	{ .compatible = "rockchip,rk312x-tve", .data = &rk312x_tve },
	{ .compatible = "rockchip,rk322x-tve", .data = &rk322x_tve },
	{ .compatible = "rockchip,rk3328-tve", .data = &rk3328_tve },
	{ .compatible = "rockchip,rk3528-tve", .data = &rk3528_tve },
	{}
};

MODULE_DEVICE_TABLE(of, rockchip_tve_dt_ids);

static int rockchip_tve_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm_dev = data;
	struct device_node *np = dev->of_node;
	const struct of_device_id *match;
	const struct rockchip_tve_data *tve_data;
	struct rockchip_tve *tve;
	struct resource *res;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	tve = devm_kzalloc(dev, sizeof(*tve), GFP_KERNEL);
	if (!tve)
		return -ENOMEM;

	match = of_match_node(rockchip_tve_dt_ids, np);
	if (!match) {
		dev_err(tve->dev, "tve can't match node\n");
		return -EINVAL;
	}

	tve->dev = &pdev->dev;
	tve_data = of_device_get_match_data(dev);
	if (tve_data) {
		tve->soc_type = tve_data->soc_type;
		tve->input_format = tve_data->input_format;
	}

	if (tve->soc_type == SOC_RK3528)
		ret = tve_parse_dt(np, tve);
	else
		ret = tve_parse_dt_legacy(np, tve);
	if (ret) {
		dev_err(tve->dev, "TVE parse dts error!");
		return -EINVAL;
	}

	tve->enable = 0;
	tve->drm_dev = drm_dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tve->reg_phy_base = res->start;
	tve->len = resource_size(res);
	tve->regbase = devm_ioremap(tve->dev, res->start, tve->len);
	if (IS_ERR(tve->regbase)) {
		dev_err(tve->dev,
			"tv encoder device map registers failed!");
		return PTR_ERR(tve->regbase);
	}

	if (tve->soc_type == SOC_RK322X || tve->soc_type == SOC_RK3328 ||
	    tve->soc_type == SOC_RK3528) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		tve->len = resource_size(res);
		tve->vdacbase = devm_ioremap(tve->dev, res->start, tve->len);
		if (IS_ERR(tve->vdacbase)) {
			dev_err(tve->dev, "tv encoder device dac map registers failed!");
			return PTR_ERR(tve->vdacbase);
		}
	}

	if (tve->soc_type == SOC_RK3036) {
		tve->aclk = devm_clk_get(tve->dev, "aclk");
		if (IS_ERR(tve->aclk)) {
			dev_err(tve->dev, "Unable to get tve aclk\n");
			return PTR_ERR(tve->aclk);
		}

		ret = clk_prepare_enable(tve->aclk);
		if (ret) {
			dev_err(tve->dev, "Cannot enable tve aclk: %d\n", ret);
			return ret;
		}
	} else if (tve->soc_type == SOC_RK3528) {
		tve->hclk = devm_clk_get(tve->dev, "hclk");
		if (IS_ERR(tve->hclk)) {
			dev_err(tve->dev, "Unable to get tve hclk\n");
			return PTR_ERR(tve->hclk);
		}

		ret = clk_prepare_enable(tve->hclk);
		if (ret) {
			dev_err(tve->dev, "Cannot enable tve hclk: %d\n", ret);
			return ret;
		}

		tve->pclk_vdac = devm_clk_get(tve->dev, "pclk_vdac");
		if (IS_ERR(tve->pclk_vdac)) {
			dev_err(tve->dev, "Unable to get vdac pclk\n");
			return PTR_ERR(tve->pclk_vdac);
		}

		ret = clk_prepare_enable(tve->pclk_vdac);
		if (ret) {
			dev_err(tve->dev, "Cannot enable vdac pclk: %d\n", ret);
			return ret;
		}

		tve->dclk = devm_clk_get(tve->dev, "dclk");
		if (IS_ERR(tve->dclk)) {
			dev_err(tve->dev, "Unable to get tve dclk\n");
			return PTR_ERR(tve->dclk);
		}

		ret = clk_prepare_enable(tve->dclk);
		if (ret) {
			dev_err(tve->dev, "Cannot enable tve dclk: %d\n", ret);
			return ret;
		}

		if (tve->upsample_mode == DCLK_UPSAMPLEx4) {
			tve->dclk_4x = devm_clk_get(tve->dev, "dclk_4x");
			if (IS_ERR(tve->dclk_4x)) {
				dev_err(tve->dev, "Unable to get tve dclk_4x\n");
				return PTR_ERR(tve->dclk_4x);
			}

			ret = clk_prepare_enable(tve->dclk_4x);
			if (ret) {
				dev_err(tve->dev, "Cannot enable tve dclk_4x: %d\n", ret);
				return ret;
			}
		}
	}

	tve->dac_grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");

	mutex_init(&tve->suspend_lock);
	check_uboot_logo(tve);
	tve->tv_format = TVOUT_CVBS_PAL;
	encoder = &tve->encoder;
	encoder->possible_crtcs = rockchip_drm_of_find_possible_crtcs(drm_dev,
								      dev->of_node);
	dev_dbg(tve->dev, "possible_crtc:%d\n", encoder->possible_crtcs);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_tve_encoder_funcs,
			       DRM_MODE_ENCODER_TVDAC, NULL);
	if (ret < 0) {
		dev_err(tve->dev, "failed to initialize encoder with drm\n");
		goto err_disable_aclk;
	}

	drm_encoder_helper_add(encoder, &rockchip_tve_encoder_helper_funcs);

	connector = &tve->connector;
	connector->interlace_allowed = 1;
	ret = drm_connector_init(drm_dev, connector,
				 &rockchip_tve_connector_funcs,
				 DRM_MODE_CONNECTOR_TV);
	if (ret < 0) {
		dev_dbg(tve->dev, "failed to initialize connector with drm\n");
		goto err_free_encoder;
	}

	drm_connector_helper_add(connector,
				 &rockchip_tve_connector_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		dev_dbg(tve->dev, "failed to attach connector and encoder\n");
		goto err_free_connector;
	}
	tve->sub_dev.connector = &tve->connector;
	tve->sub_dev.of_node = tve->dev->of_node;
	rockchip_drm_register_sub_dev(&tve->sub_dev);

	pm_runtime_enable(dev);
	dev_set_drvdata(dev, tve);
	dev_dbg(tve->dev, "%s tv encoder probe ok\n", match->compatible);

	return 0;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
err_disable_aclk:
	if (tve->soc_type == SOC_RK3036)
		clk_disable_unprepare(tve->aclk);

	return ret;
}

static void rockchip_tve_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct rockchip_tve *tve = dev_get_drvdata(dev);

	rockchip_drm_unregister_sub_dev(&tve->sub_dev);
	rockchip_tve_encoder_disable(&tve->encoder);

	drm_connector_cleanup(&tve->connector);
	drm_encoder_cleanup(&tve->encoder);

	pm_runtime_disable(dev);
	dev_set_drvdata(dev, NULL);
}

static const struct component_ops rockchip_tve_component_ops = {
	.bind = rockchip_tve_bind,
	.unbind = rockchip_tve_unbind,
};

static int rockchip_tve_probe(struct platform_device *pdev)
{
	component_add(&pdev->dev, &rockchip_tve_component_ops);

	return 0;
}

static void rockchip_tve_shutdown(struct platform_device *pdev)
{
	struct rockchip_tve *tve = dev_get_drvdata(&pdev->dev);

	if (!tve)
		return;

	mutex_lock(&tve->suspend_lock);

	dev_dbg(tve->dev, "tve shutdown\n");
	cvbs_set_disable(tve);

	mutex_unlock(&tve->suspend_lock);
}

static int rockchip_tve_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rockchip_tve_component_ops);

	return 0;
}

struct platform_driver rockchip_tve_driver = {
	.probe = rockchip_tve_probe,
	.remove = rockchip_tve_remove,
	.shutdown = rockchip_tve_shutdown,
	.driver = {
		   .name = "rockchip-tve",
		   .of_match_table = of_match_ptr(rockchip_tve_dt_ids),
	},
};

MODULE_AUTHOR("Algea Cao <Algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP TVE Driver");
MODULE_LICENSE("GPL v2");
