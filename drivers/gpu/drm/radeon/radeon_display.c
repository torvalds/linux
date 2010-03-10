/*
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 */
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon.h"

#include "atom.h"
#include <asm/div64.h>

#include "drm_crtc_helper.h"
#include "drm_edid.h"

static int radeon_ddc_dump(struct drm_connector *connector);

static void avivo_crtc_load_lut(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int i;

	DRM_DEBUG("%d\n", radeon_crtc->crtc_id);
	WREG32(AVIVO_DC_LUTA_CONTROL + radeon_crtc->crtc_offset, 0);

	WREG32(AVIVO_DC_LUTA_BLACK_OFFSET_BLUE + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_DC_LUTA_BLACK_OFFSET_GREEN + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_DC_LUTA_BLACK_OFFSET_RED + radeon_crtc->crtc_offset, 0);

	WREG32(AVIVO_DC_LUTA_WHITE_OFFSET_BLUE + radeon_crtc->crtc_offset, 0xffff);
	WREG32(AVIVO_DC_LUTA_WHITE_OFFSET_GREEN + radeon_crtc->crtc_offset, 0xffff);
	WREG32(AVIVO_DC_LUTA_WHITE_OFFSET_RED + radeon_crtc->crtc_offset, 0xffff);

	WREG32(AVIVO_DC_LUT_RW_SELECT, radeon_crtc->crtc_id);
	WREG32(AVIVO_DC_LUT_RW_MODE, 0);
	WREG32(AVIVO_DC_LUT_WRITE_EN_MASK, 0x0000003f);

	WREG8(AVIVO_DC_LUT_RW_INDEX, 0);
	for (i = 0; i < 256; i++) {
		WREG32(AVIVO_DC_LUT_30_COLOR,
			     (radeon_crtc->lut_r[i] << 20) |
			     (radeon_crtc->lut_g[i] << 10) |
			     (radeon_crtc->lut_b[i] << 0));
	}

	WREG32(AVIVO_D1GRPH_LUT_SEL + radeon_crtc->crtc_offset, radeon_crtc->crtc_id);
}

static void evergreen_crtc_load_lut(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int i;

	DRM_DEBUG("%d\n", radeon_crtc->crtc_id);
	WREG32(EVERGREEN_DC_LUT_CONTROL + radeon_crtc->crtc_offset, 0);

	WREG32(EVERGREEN_DC_LUT_BLACK_OFFSET_BLUE + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_DC_LUT_BLACK_OFFSET_GREEN + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_DC_LUT_BLACK_OFFSET_RED + radeon_crtc->crtc_offset, 0);

	WREG32(EVERGREEN_DC_LUT_WHITE_OFFSET_BLUE + radeon_crtc->crtc_offset, 0xffff);
	WREG32(EVERGREEN_DC_LUT_WHITE_OFFSET_GREEN + radeon_crtc->crtc_offset, 0xffff);
	WREG32(EVERGREEN_DC_LUT_WHITE_OFFSET_RED + radeon_crtc->crtc_offset, 0xffff);

	WREG32(EVERGREEN_DC_LUT_RW_MODE, radeon_crtc->crtc_id);
	WREG32(EVERGREEN_DC_LUT_WRITE_EN_MASK, 0x00000007);

	WREG32(EVERGREEN_DC_LUT_RW_INDEX, 0);
	for (i = 0; i < 256; i++) {
		WREG32(EVERGREEN_DC_LUT_30_COLOR,
		       (radeon_crtc->lut_r[i] << 20) |
		       (radeon_crtc->lut_g[i] << 10) |
		       (radeon_crtc->lut_b[i] << 0));
	}
}

static void legacy_crtc_load_lut(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int i;
	uint32_t dac2_cntl;

	dac2_cntl = RREG32(RADEON_DAC_CNTL2);
	if (radeon_crtc->crtc_id == 0)
		dac2_cntl &= (uint32_t)~RADEON_DAC2_PALETTE_ACC_CTL;
	else
		dac2_cntl |= RADEON_DAC2_PALETTE_ACC_CTL;
	WREG32(RADEON_DAC_CNTL2, dac2_cntl);

	WREG8(RADEON_PALETTE_INDEX, 0);
	for (i = 0; i < 256; i++) {
		WREG32(RADEON_PALETTE_30_DATA,
			     (radeon_crtc->lut_r[i] << 20) |
			     (radeon_crtc->lut_g[i] << 10) |
			     (radeon_crtc->lut_b[i] << 0));
	}
}

void radeon_crtc_load_lut(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;

	if (!crtc->enabled)
		return;

	if (ASIC_IS_DCE4(rdev))
		evergreen_crtc_load_lut(crtc);
	else if (ASIC_IS_AVIVO(rdev))
		avivo_crtc_load_lut(crtc);
	else
		legacy_crtc_load_lut(crtc);
}

/** Sets the color ramps on behalf of fbcon */
void radeon_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
			      u16 blue, int regno)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	radeon_crtc->lut_r[regno] = red >> 6;
	radeon_crtc->lut_g[regno] = green >> 6;
	radeon_crtc->lut_b[regno] = blue >> 6;
}

/** Gets the color ramps on behalf of fbcon */
void radeon_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			      u16 *blue, int regno)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	*red = radeon_crtc->lut_r[regno] << 6;
	*green = radeon_crtc->lut_g[regno] << 6;
	*blue = radeon_crtc->lut_b[regno] << 6;
}

static void radeon_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				  u16 *blue, uint32_t size)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	int i;

	if (size != 256) {
		return;
	}

	/* userspace palettes are always correct as is */
	for (i = 0; i < 256; i++) {
		radeon_crtc->lut_r[i] = red[i] >> 6;
		radeon_crtc->lut_g[i] = green[i] >> 6;
		radeon_crtc->lut_b[i] = blue[i] >> 6;
	}
	radeon_crtc_load_lut(crtc);
}

static void radeon_crtc_destroy(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(radeon_crtc);
}

static const struct drm_crtc_funcs radeon_crtc_funcs = {
	.cursor_set = radeon_crtc_cursor_set,
	.cursor_move = radeon_crtc_cursor_move,
	.gamma_set = radeon_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = radeon_crtc_destroy,
};

static void radeon_crtc_init(struct drm_device *dev, int index)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc;
	int i;

	radeon_crtc = kzalloc(sizeof(struct radeon_crtc) + (RADEONFB_CONN_LIMIT * sizeof(struct drm_connector *)), GFP_KERNEL);
	if (radeon_crtc == NULL)
		return;

	drm_crtc_init(dev, &radeon_crtc->base, &radeon_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&radeon_crtc->base, 256);
	radeon_crtc->crtc_id = index;
	rdev->mode_info.crtcs[index] = radeon_crtc;

#if 0
	radeon_crtc->mode_set.crtc = &radeon_crtc->base;
	radeon_crtc->mode_set.connectors = (struct drm_connector **)(radeon_crtc + 1);
	radeon_crtc->mode_set.num_connectors = 0;
#endif

	for (i = 0; i < 256; i++) {
		radeon_crtc->lut_r[i] = i << 2;
		radeon_crtc->lut_g[i] = i << 2;
		radeon_crtc->lut_b[i] = i << 2;
	}

	if (rdev->is_atom_bios && (ASIC_IS_AVIVO(rdev) || radeon_r4xx_atom))
		radeon_atombios_init_crtc(dev, radeon_crtc);
	else
		radeon_legacy_init_crtc(dev, radeon_crtc);
}

static const char *encoder_names[34] = {
	"NONE",
	"INTERNAL_LVDS",
	"INTERNAL_TMDS1",
	"INTERNAL_TMDS2",
	"INTERNAL_DAC1",
	"INTERNAL_DAC2",
	"INTERNAL_SDVOA",
	"INTERNAL_SDVOB",
	"SI170B",
	"CH7303",
	"CH7301",
	"INTERNAL_DVO1",
	"EXTERNAL_SDVOA",
	"EXTERNAL_SDVOB",
	"TITFP513",
	"INTERNAL_LVTM1",
	"VT1623",
	"HDMI_SI1930",
	"HDMI_INTERNAL",
	"INTERNAL_KLDSCP_TMDS1",
	"INTERNAL_KLDSCP_DVO1",
	"INTERNAL_KLDSCP_DAC1",
	"INTERNAL_KLDSCP_DAC2",
	"SI178",
	"MVPU_FPGA",
	"INTERNAL_DDI",
	"VT1625",
	"HDMI_SI1932",
	"DP_AN9801",
	"DP_DP501",
	"INTERNAL_UNIPHY",
	"INTERNAL_KLDSCP_LVTMA",
	"INTERNAL_UNIPHY1",
	"INTERNAL_UNIPHY2",
};

static const char *connector_names[15] = {
	"Unknown",
	"VGA",
	"DVI-I",
	"DVI-D",
	"DVI-A",
	"Composite",
	"S-video",
	"LVDS",
	"Component",
	"DIN",
	"DisplayPort",
	"HDMI-A",
	"HDMI-B",
	"TV",
	"eDP",
};

static const char *hpd_names[7] = {
	"NONE",
	"HPD1",
	"HPD2",
	"HPD3",
	"HPD4",
	"HPD5",
	"HPD6",
};

static void radeon_print_display_setup(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;
	uint32_t devices;
	int i = 0;

	DRM_INFO("Radeon Display Connectors\n");
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		radeon_connector = to_radeon_connector(connector);
		DRM_INFO("Connector %d:\n", i);
		DRM_INFO("  %s\n", connector_names[connector->connector_type]);
		if (radeon_connector->hpd.hpd != RADEON_HPD_NONE)
			DRM_INFO("  %s\n", hpd_names[radeon_connector->hpd.hpd]);
		if (radeon_connector->ddc_bus) {
			DRM_INFO("  DDC: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
				 radeon_connector->ddc_bus->rec.mask_clk_reg,
				 radeon_connector->ddc_bus->rec.mask_data_reg,
				 radeon_connector->ddc_bus->rec.a_clk_reg,
				 radeon_connector->ddc_bus->rec.a_data_reg,
				 radeon_connector->ddc_bus->rec.en_clk_reg,
				 radeon_connector->ddc_bus->rec.en_data_reg,
				 radeon_connector->ddc_bus->rec.y_clk_reg,
				 radeon_connector->ddc_bus->rec.y_data_reg);
		} else {
			if (connector->connector_type == DRM_MODE_CONNECTOR_VGA ||
			    connector->connector_type == DRM_MODE_CONNECTOR_DVII ||
			    connector->connector_type == DRM_MODE_CONNECTOR_DVID ||
			    connector->connector_type == DRM_MODE_CONNECTOR_DVIA ||
			    connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
			    connector->connector_type == DRM_MODE_CONNECTOR_HDMIB)
				DRM_INFO("  DDC: no ddc bus - possible BIOS bug - please report to xorg-driver-ati@lists.x.org\n");
		}
		DRM_INFO("  Encoders:\n");
		list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
			radeon_encoder = to_radeon_encoder(encoder);
			devices = radeon_encoder->devices & radeon_connector->devices;
			if (devices) {
				if (devices & ATOM_DEVICE_CRT1_SUPPORT)
					DRM_INFO("    CRT1: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_CRT2_SUPPORT)
					DRM_INFO("    CRT2: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_LCD1_SUPPORT)
					DRM_INFO("    LCD1: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_DFP1_SUPPORT)
					DRM_INFO("    DFP1: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_DFP2_SUPPORT)
					DRM_INFO("    DFP2: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_DFP3_SUPPORT)
					DRM_INFO("    DFP3: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_DFP4_SUPPORT)
					DRM_INFO("    DFP4: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_DFP5_SUPPORT)
					DRM_INFO("    DFP5: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_TV1_SUPPORT)
					DRM_INFO("    TV1: %s\n", encoder_names[radeon_encoder->encoder_id]);
				if (devices & ATOM_DEVICE_CV_SUPPORT)
					DRM_INFO("    CV: %s\n", encoder_names[radeon_encoder->encoder_id]);
			}
		}
		i++;
	}
}

static bool radeon_setup_enc_conn(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	struct drm_connector *drm_connector;
	bool ret = false;

	if (rdev->bios) {
		if (rdev->is_atom_bios) {
			if (rdev->family >= CHIP_R600)
				ret = radeon_get_atom_connector_info_from_object_table(dev);
			else
				ret = radeon_get_atom_connector_info_from_supported_devices_table(dev);
		} else {
			ret = radeon_get_legacy_connector_info_from_bios(dev);
			if (ret == false)
				ret = radeon_get_legacy_connector_info_from_table(dev);
		}
	} else {
		if (!ASIC_IS_AVIVO(rdev))
			ret = radeon_get_legacy_connector_info_from_table(dev);
	}
	if (ret) {
		radeon_setup_encoder_clones(dev);
		radeon_print_display_setup(dev);
		list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head)
			radeon_ddc_dump(drm_connector);
	}

	return ret;
}

int radeon_ddc_get_modes(struct radeon_connector *radeon_connector)
{
	struct drm_device *dev = radeon_connector->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	int ret = 0;

	if ((radeon_connector->base.connector_type == DRM_MODE_CONNECTOR_DisplayPort) ||
	    (radeon_connector->base.connector_type == DRM_MODE_CONNECTOR_eDP)) {
		struct radeon_connector_atom_dig *dig = radeon_connector->con_priv;
		if ((dig->dp_sink_type == CONNECTOR_OBJECT_ID_DISPLAYPORT ||
		     dig->dp_sink_type == CONNECTOR_OBJECT_ID_eDP) && dig->dp_i2c_bus)
			radeon_connector->edid = drm_get_edid(&radeon_connector->base, &dig->dp_i2c_bus->adapter);
	}
	if (!radeon_connector->ddc_bus)
		return -1;
	if (!radeon_connector->edid) {
		radeon_connector->edid = drm_get_edid(&radeon_connector->base, &radeon_connector->ddc_bus->adapter);
	}
	/* some servers provide a hardcoded edid in rom for KVMs */
	if (!radeon_connector->edid)
		radeon_connector->edid = radeon_combios_get_hardcoded_edid(rdev);
	if (radeon_connector->edid) {
		drm_mode_connector_update_edid_property(&radeon_connector->base, radeon_connector->edid);
		ret = drm_add_edid_modes(&radeon_connector->base, radeon_connector->edid);
		return ret;
	}
	drm_mode_connector_update_edid_property(&radeon_connector->base, NULL);
	return 0;
}

static int radeon_ddc_dump(struct drm_connector *connector)
{
	struct edid *edid;
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	int ret = 0;

	if (!radeon_connector->ddc_bus)
		return -1;
	edid = drm_get_edid(connector, &radeon_connector->ddc_bus->adapter);
	if (edid) {
		kfree(edid);
	}
	return ret;
}

static inline uint32_t radeon_div(uint64_t n, uint32_t d)
{
	uint64_t mod;

	n += d / 2;

	mod = do_div(n, d);
	return n;
}

static void radeon_compute_pll_legacy(struct radeon_pll *pll,
				      uint64_t freq,
				      uint32_t *dot_clock_p,
				      uint32_t *fb_div_p,
				      uint32_t *frac_fb_div_p,
				      uint32_t *ref_div_p,
				      uint32_t *post_div_p)
{
	uint32_t min_ref_div = pll->min_ref_div;
	uint32_t max_ref_div = pll->max_ref_div;
	uint32_t min_post_div = pll->min_post_div;
	uint32_t max_post_div = pll->max_post_div;
	uint32_t min_fractional_feed_div = 0;
	uint32_t max_fractional_feed_div = 0;
	uint32_t best_vco = pll->best_vco;
	uint32_t best_post_div = 1;
	uint32_t best_ref_div = 1;
	uint32_t best_feedback_div = 1;
	uint32_t best_frac_feedback_div = 0;
	uint32_t best_freq = -1;
	uint32_t best_error = 0xffffffff;
	uint32_t best_vco_diff = 1;
	uint32_t post_div;

	DRM_DEBUG("PLL freq %llu %u %u\n", freq, pll->min_ref_div, pll->max_ref_div);
	freq = freq * 1000;

	if (pll->flags & RADEON_PLL_USE_REF_DIV)
		min_ref_div = max_ref_div = pll->reference_div;
	else {
		while (min_ref_div < max_ref_div-1) {
			uint32_t mid = (min_ref_div + max_ref_div) / 2;
			uint32_t pll_in = pll->reference_freq / mid;
			if (pll_in < pll->pll_in_min)
				max_ref_div = mid;
			else if (pll_in > pll->pll_in_max)
				min_ref_div = mid;
			else
				break;
		}
	}

	if (pll->flags & RADEON_PLL_USE_POST_DIV)
		min_post_div = max_post_div = pll->post_div;

	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV) {
		min_fractional_feed_div = pll->min_frac_feedback_div;
		max_fractional_feed_div = pll->max_frac_feedback_div;
	}

	for (post_div = min_post_div; post_div <= max_post_div; ++post_div) {
		uint32_t ref_div;

		if ((pll->flags & RADEON_PLL_NO_ODD_POST_DIV) && (post_div & 1))
			continue;

		/* legacy radeons only have a few post_divs */
		if (pll->flags & RADEON_PLL_LEGACY) {
			if ((post_div == 5) ||
			    (post_div == 7) ||
			    (post_div == 9) ||
			    (post_div == 10) ||
			    (post_div == 11) ||
			    (post_div == 13) ||
			    (post_div == 14) ||
			    (post_div == 15))
				continue;
		}

		for (ref_div = min_ref_div; ref_div <= max_ref_div; ++ref_div) {
			uint32_t feedback_div, current_freq = 0, error, vco_diff;
			uint32_t pll_in = pll->reference_freq / ref_div;
			uint32_t min_feed_div = pll->min_feedback_div;
			uint32_t max_feed_div = pll->max_feedback_div + 1;

			if (pll_in < pll->pll_in_min || pll_in > pll->pll_in_max)
				continue;

			while (min_feed_div < max_feed_div) {
				uint32_t vco;
				uint32_t min_frac_feed_div = min_fractional_feed_div;
				uint32_t max_frac_feed_div = max_fractional_feed_div + 1;
				uint32_t frac_feedback_div;
				uint64_t tmp;

				feedback_div = (min_feed_div + max_feed_div) / 2;

				tmp = (uint64_t)pll->reference_freq * feedback_div;
				vco = radeon_div(tmp, ref_div);

				if (vco < pll->pll_out_min) {
					min_feed_div = feedback_div + 1;
					continue;
				} else if (vco > pll->pll_out_max) {
					max_feed_div = feedback_div;
					continue;
				}

				while (min_frac_feed_div < max_frac_feed_div) {
					frac_feedback_div = (min_frac_feed_div + max_frac_feed_div) / 2;
					tmp = (uint64_t)pll->reference_freq * 10000 * feedback_div;
					tmp += (uint64_t)pll->reference_freq * 1000 * frac_feedback_div;
					current_freq = radeon_div(tmp, ref_div * post_div);

					if (pll->flags & RADEON_PLL_PREFER_CLOSEST_LOWER) {
						error = freq - current_freq;
						error = error < 0 ? 0xffffffff : error;
					} else
						error = abs(current_freq - freq);
					vco_diff = abs(vco - best_vco);

					if ((best_vco == 0 && error < best_error) ||
					    (best_vco != 0 &&
					     (error < best_error - 100 ||
					      (abs(error - best_error) < 100 && vco_diff < best_vco_diff)))) {
						best_post_div = post_div;
						best_ref_div = ref_div;
						best_feedback_div = feedback_div;
						best_frac_feedback_div = frac_feedback_div;
						best_freq = current_freq;
						best_error = error;
						best_vco_diff = vco_diff;
					} else if (current_freq == freq) {
						if (best_freq == -1) {
							best_post_div = post_div;
							best_ref_div = ref_div;
							best_feedback_div = feedback_div;
							best_frac_feedback_div = frac_feedback_div;
							best_freq = current_freq;
							best_error = error;
							best_vco_diff = vco_diff;
						} else if (((pll->flags & RADEON_PLL_PREFER_LOW_REF_DIV) && (ref_div < best_ref_div)) ||
							   ((pll->flags & RADEON_PLL_PREFER_HIGH_REF_DIV) && (ref_div > best_ref_div)) ||
							   ((pll->flags & RADEON_PLL_PREFER_LOW_FB_DIV) && (feedback_div < best_feedback_div)) ||
							   ((pll->flags & RADEON_PLL_PREFER_HIGH_FB_DIV) && (feedback_div > best_feedback_div)) ||
							   ((pll->flags & RADEON_PLL_PREFER_LOW_POST_DIV) && (post_div < best_post_div)) ||
							   ((pll->flags & RADEON_PLL_PREFER_HIGH_POST_DIV) && (post_div > best_post_div))) {
							best_post_div = post_div;
							best_ref_div = ref_div;
							best_feedback_div = feedback_div;
							best_frac_feedback_div = frac_feedback_div;
							best_freq = current_freq;
							best_error = error;
							best_vco_diff = vco_diff;
						}
					}
					if (current_freq < freq)
						min_frac_feed_div = frac_feedback_div + 1;
					else
						max_frac_feed_div = frac_feedback_div;
				}
				if (current_freq < freq)
					min_feed_div = feedback_div + 1;
				else
					max_feed_div = feedback_div;
			}
		}
	}

	*dot_clock_p = best_freq / 10000;
	*fb_div_p = best_feedback_div;
	*frac_fb_div_p = best_frac_feedback_div;
	*ref_div_p = best_ref_div;
	*post_div_p = best_post_div;
}

static bool
calc_fb_div(struct radeon_pll *pll,
	    uint32_t freq,
            uint32_t post_div,
            uint32_t ref_div,
            uint32_t *fb_div,
            uint32_t *fb_div_frac)
{
	fixed20_12 feedback_divider, a, b;
	u32 vco_freq;

	vco_freq = freq * post_div;
	/* feedback_divider = vco_freq * ref_div / pll->reference_freq; */
	a.full = rfixed_const(pll->reference_freq);
	feedback_divider.full = rfixed_const(vco_freq);
	feedback_divider.full = rfixed_div(feedback_divider, a);
	a.full = rfixed_const(ref_div);
	feedback_divider.full = rfixed_mul(feedback_divider, a);

	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV) {
		/* feedback_divider = floor((feedback_divider * 10.0) + 0.5) * 0.1; */
		a.full = rfixed_const(10);
		feedback_divider.full = rfixed_mul(feedback_divider, a);
		feedback_divider.full += rfixed_const_half(0);
		feedback_divider.full = rfixed_floor(feedback_divider);
		feedback_divider.full = rfixed_div(feedback_divider, a);

		/* *fb_div = floor(feedback_divider); */
		a.full = rfixed_floor(feedback_divider);
		*fb_div = rfixed_trunc(a);
		/* *fb_div_frac = fmod(feedback_divider, 1.0) * 10.0; */
		a.full = rfixed_const(10);
		b.full = rfixed_mul(feedback_divider, a);

		feedback_divider.full = rfixed_floor(feedback_divider);
		feedback_divider.full = rfixed_mul(feedback_divider, a);
		feedback_divider.full = b.full - feedback_divider.full;
		*fb_div_frac = rfixed_trunc(feedback_divider);
	} else {
		/* *fb_div = floor(feedback_divider + 0.5); */
		feedback_divider.full += rfixed_const_half(0);
		feedback_divider.full = rfixed_floor(feedback_divider);

		*fb_div = rfixed_trunc(feedback_divider);
		*fb_div_frac = 0;
	}

	if (((*fb_div) < pll->min_feedback_div) || ((*fb_div) > pll->max_feedback_div))
		return false;
	else
		return true;
}

static bool
calc_fb_ref_div(struct radeon_pll *pll,
		uint32_t freq,
		uint32_t post_div,
		uint32_t *fb_div,
                uint32_t *fb_div_frac,
                uint32_t *ref_div)
{
	fixed20_12 ffreq, max_error, error, pll_out, a;
	u32 vco;

	ffreq.full = rfixed_const(freq);
	/* max_error = ffreq * 0.0025; */
	a.full = rfixed_const(400);
	max_error.full = rfixed_div(ffreq, a);

	for ((*ref_div) = pll->min_ref_div; (*ref_div) < pll->max_ref_div; ++(*ref_div)) {
		if (calc_fb_div(pll, freq, post_div, (*ref_div), fb_div, fb_div_frac)) {
			vco = pll->reference_freq * (((*fb_div) * 10) + (*fb_div_frac));
			vco = vco / ((*ref_div) * 10);

			if ((vco < pll->pll_out_min) || (vco > pll->pll_out_max))
				continue;

			/* pll_out = vco / post_div; */
			a.full = rfixed_const(post_div);
			pll_out.full = rfixed_const(vco);
			pll_out.full = rfixed_div(pll_out, a);

			if (pll_out.full >= ffreq.full) {
				error.full = pll_out.full - ffreq.full;
				if (error.full <= max_error.full)
					return true;
			}
		}
	}
	return false;
}

static void radeon_compute_pll_new(struct radeon_pll *pll,
				   uint64_t freq,
				   uint32_t *dot_clock_p,
				   uint32_t *fb_div_p,
				   uint32_t *frac_fb_div_p,
				   uint32_t *ref_div_p,
				   uint32_t *post_div_p)
{
	u32 fb_div = 0, fb_div_frac = 0, post_div = 0, ref_div = 0;
	u32 best_freq = 0, vco_frequency;

	/* freq = freq / 10; */
	do_div(freq, 10);

	if (pll->flags & RADEON_PLL_USE_POST_DIV) {
		post_div = pll->post_div;
		if ((post_div < pll->min_post_div) || (post_div > pll->max_post_div))
			goto done;

		vco_frequency = freq * post_div;
		if ((vco_frequency < pll->pll_out_min) || (vco_frequency > pll->pll_out_max))
			goto done;

		if (pll->flags & RADEON_PLL_USE_REF_DIV) {
			ref_div = pll->reference_div;
			if ((ref_div < pll->min_ref_div) || (ref_div > pll->max_ref_div))
				goto done;
			if (!calc_fb_div(pll, freq, post_div, ref_div, &fb_div, &fb_div_frac))
				goto done;
		}
	} else {
		for (post_div = pll->max_post_div; post_div >= pll->min_post_div; --post_div) {
			if (pll->flags & RADEON_PLL_LEGACY) {
				if ((post_div == 5) ||
				    (post_div == 7) ||
				    (post_div == 9) ||
				    (post_div == 10) ||
				    (post_div == 11))
					continue;
			}

			if ((pll->flags & RADEON_PLL_NO_ODD_POST_DIV) && (post_div & 1))
				continue;

			vco_frequency = freq * post_div;
			if ((vco_frequency < pll->pll_out_min) || (vco_frequency > pll->pll_out_max))
				continue;
			if (pll->flags & RADEON_PLL_USE_REF_DIV) {
				ref_div = pll->reference_div;
				if ((ref_div < pll->min_ref_div) || (ref_div > pll->max_ref_div))
					goto done;
				if (calc_fb_div(pll, freq, post_div, ref_div, &fb_div, &fb_div_frac))
					break;
			} else {
				if (calc_fb_ref_div(pll, freq, post_div, &fb_div, &fb_div_frac, &ref_div))
					break;
			}
		}
	}

	best_freq = pll->reference_freq * 10 * fb_div;
	best_freq += pll->reference_freq * fb_div_frac;
	best_freq = best_freq / (ref_div * post_div);

done:
	if (best_freq == 0)
		DRM_ERROR("Couldn't find valid PLL dividers\n");

	*dot_clock_p = best_freq / 10;
	*fb_div_p = fb_div;
	*frac_fb_div_p = fb_div_frac;
	*ref_div_p = ref_div;
	*post_div_p = post_div;

	DRM_DEBUG("%u %d.%d, %d, %d\n", *dot_clock_p, *fb_div_p, *frac_fb_div_p, *ref_div_p, *post_div_p);
}

void radeon_compute_pll(struct radeon_pll *pll,
			uint64_t freq,
			uint32_t *dot_clock_p,
			uint32_t *fb_div_p,
			uint32_t *frac_fb_div_p,
			uint32_t *ref_div_p,
			uint32_t *post_div_p)
{
	switch (pll->algo) {
	case PLL_ALGO_NEW:
		radeon_compute_pll_new(pll, freq, dot_clock_p, fb_div_p,
				       frac_fb_div_p, ref_div_p, post_div_p);
		break;
	case PLL_ALGO_LEGACY:
	default:
		radeon_compute_pll_legacy(pll, freq, dot_clock_p, fb_div_p,
					  frac_fb_div_p, ref_div_p, post_div_p);
		break;
	}
}

static void radeon_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct radeon_framebuffer *radeon_fb = to_radeon_framebuffer(fb);
	struct drm_device *dev = fb->dev;

	if (fb->fbdev)
		radeonfb_remove(dev, fb);

	if (radeon_fb->obj)
		drm_gem_object_unreference_unlocked(radeon_fb->obj);
	drm_framebuffer_cleanup(fb);
	kfree(radeon_fb);
}

static int radeon_user_framebuffer_create_handle(struct drm_framebuffer *fb,
						  struct drm_file *file_priv,
						  unsigned int *handle)
{
	struct radeon_framebuffer *radeon_fb = to_radeon_framebuffer(fb);

	return drm_gem_handle_create(file_priv, radeon_fb->obj, handle);
}

static const struct drm_framebuffer_funcs radeon_fb_funcs = {
	.destroy = radeon_user_framebuffer_destroy,
	.create_handle = radeon_user_framebuffer_create_handle,
};

struct drm_framebuffer *
radeon_framebuffer_create(struct drm_device *dev,
			  struct drm_mode_fb_cmd *mode_cmd,
			  struct drm_gem_object *obj)
{
	struct radeon_framebuffer *radeon_fb;

	radeon_fb = kzalloc(sizeof(*radeon_fb), GFP_KERNEL);
	if (radeon_fb == NULL) {
		return NULL;
	}
	drm_framebuffer_init(dev, &radeon_fb->base, &radeon_fb_funcs);
	drm_helper_mode_fill_fb_struct(&radeon_fb->base, mode_cmd);
	radeon_fb->obj = obj;
	return &radeon_fb->base;
}

static struct drm_framebuffer *
radeon_user_framebuffer_create(struct drm_device *dev,
			       struct drm_file *file_priv,
			       struct drm_mode_fb_cmd *mode_cmd)
{
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(dev, file_priv, mode_cmd->handle);
	if (obj ==  NULL) {
		dev_err(&dev->pdev->dev, "No GEM object associated to handle 0x%08X, "
			"can't create framebuffer\n", mode_cmd->handle);
		return NULL;
	}
	return radeon_framebuffer_create(dev, mode_cmd, obj);
}

static const struct drm_mode_config_funcs radeon_mode_funcs = {
	.fb_create = radeon_user_framebuffer_create,
	.fb_changed = radeonfb_probe,
};

struct drm_prop_enum_list {
	int type;
	char *name;
};

static struct drm_prop_enum_list radeon_tmds_pll_enum_list[] =
{	{ 0, "driver" },
	{ 1, "bios" },
};

static struct drm_prop_enum_list radeon_tv_std_enum_list[] =
{	{ TV_STD_NTSC, "ntsc" },
	{ TV_STD_PAL, "pal" },
	{ TV_STD_PAL_M, "pal-m" },
	{ TV_STD_PAL_60, "pal-60" },
	{ TV_STD_NTSC_J, "ntsc-j" },
	{ TV_STD_SCART_PAL, "scart-pal" },
	{ TV_STD_PAL_CN, "pal-cn" },
	{ TV_STD_SECAM, "secam" },
};

static int radeon_modeset_create_props(struct radeon_device *rdev)
{
	int i, sz;

	if (rdev->is_atom_bios) {
		rdev->mode_info.coherent_mode_property =
			drm_property_create(rdev->ddev,
					    DRM_MODE_PROP_RANGE,
					    "coherent", 2);
		if (!rdev->mode_info.coherent_mode_property)
			return -ENOMEM;

		rdev->mode_info.coherent_mode_property->values[0] = 0;
		rdev->mode_info.coherent_mode_property->values[1] = 1;
	}

	if (!ASIC_IS_AVIVO(rdev)) {
		sz = ARRAY_SIZE(radeon_tmds_pll_enum_list);
		rdev->mode_info.tmds_pll_property =
			drm_property_create(rdev->ddev,
					    DRM_MODE_PROP_ENUM,
					    "tmds_pll", sz);
		for (i = 0; i < sz; i++) {
			drm_property_add_enum(rdev->mode_info.tmds_pll_property,
					      i,
					      radeon_tmds_pll_enum_list[i].type,
					      radeon_tmds_pll_enum_list[i].name);
		}
	}

	rdev->mode_info.load_detect_property =
		drm_property_create(rdev->ddev,
				    DRM_MODE_PROP_RANGE,
				    "load detection", 2);
	if (!rdev->mode_info.load_detect_property)
		return -ENOMEM;
	rdev->mode_info.load_detect_property->values[0] = 0;
	rdev->mode_info.load_detect_property->values[1] = 1;

	drm_mode_create_scaling_mode_property(rdev->ddev);

	sz = ARRAY_SIZE(radeon_tv_std_enum_list);
	rdev->mode_info.tv_std_property =
		drm_property_create(rdev->ddev,
				    DRM_MODE_PROP_ENUM,
				    "tv standard", sz);
	for (i = 0; i < sz; i++) {
		drm_property_add_enum(rdev->mode_info.tv_std_property,
				      i,
				      radeon_tv_std_enum_list[i].type,
				      radeon_tv_std_enum_list[i].name);
	}

	return 0;
}

int radeon_modeset_init(struct radeon_device *rdev)
{
	int i;
	int ret;

	drm_mode_config_init(rdev->ddev);
	rdev->mode_info.mode_config_initialized = true;

	rdev->ddev->mode_config.funcs = (void *)&radeon_mode_funcs;

	if (ASIC_IS_AVIVO(rdev)) {
		rdev->ddev->mode_config.max_width = 8192;
		rdev->ddev->mode_config.max_height = 8192;
	} else {
		rdev->ddev->mode_config.max_width = 4096;
		rdev->ddev->mode_config.max_height = 4096;
	}

	rdev->ddev->mode_config.fb_base = rdev->mc.aper_base;

	ret = radeon_modeset_create_props(rdev);
	if (ret) {
		return ret;
	}

	/* check combios for a valid hardcoded EDID - Sun servers */
	if (!rdev->is_atom_bios) {
		/* check for hardcoded EDID in BIOS */
		radeon_combios_check_hardcoded_edid(rdev);
	}

	if (rdev->flags & RADEON_SINGLE_CRTC)
		rdev->num_crtc = 1;
	else {
		if (ASIC_IS_DCE4(rdev))
			rdev->num_crtc = 6;
		else
			rdev->num_crtc = 2;
	}

	/* allocate crtcs */
	for (i = 0; i < rdev->num_crtc; i++) {
		radeon_crtc_init(rdev->ddev, i);
	}

	/* okay we should have all the bios connectors */
	ret = radeon_setup_enc_conn(rdev->ddev);
	if (!ret) {
		return ret;
	}
	/* initialize hpd */
	radeon_hpd_init(rdev);
	drm_helper_initial_config(rdev->ddev);
	return 0;
}

void radeon_modeset_fini(struct radeon_device *rdev)
{
	kfree(rdev->mode_info.bios_hardcoded_edid);

	if (rdev->mode_info.mode_config_initialized) {
		radeon_hpd_fini(rdev);
		drm_mode_config_cleanup(rdev->ddev);
		rdev->mode_info.mode_config_initialized = false;
	}
}

bool radeon_crtc_scaling_mode_fixup(struct drm_crtc *crtc,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_encoder *encoder;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct radeon_encoder *radeon_encoder;
	bool first = true;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		radeon_encoder = to_radeon_encoder(encoder);
		if (encoder->crtc != crtc)
			continue;
		if (first) {
			/* set scaling */
			if (radeon_encoder->rmx_type == RMX_OFF)
				radeon_crtc->rmx_type = RMX_OFF;
			else if (mode->hdisplay < radeon_encoder->native_mode.hdisplay ||
				 mode->vdisplay < radeon_encoder->native_mode.vdisplay)
				radeon_crtc->rmx_type = radeon_encoder->rmx_type;
			else
				radeon_crtc->rmx_type = RMX_OFF;
			/* copy native mode */
			memcpy(&radeon_crtc->native_mode,
			       &radeon_encoder->native_mode,
				sizeof(struct drm_display_mode));
			first = false;
		} else {
			if (radeon_crtc->rmx_type != radeon_encoder->rmx_type) {
				/* WARNING: Right now this can't happen but
				 * in the future we need to check that scaling
				 * are consistent accross different encoder
				 * (ie all encoder can work with the same
				 *  scaling).
				 */
				DRM_ERROR("Scaling not consistent accross encoder.\n");
				return false;
			}
		}
	}
	if (radeon_crtc->rmx_type != RMX_OFF) {
		fixed20_12 a, b;
		a.full = rfixed_const(crtc->mode.vdisplay);
		b.full = rfixed_const(radeon_crtc->native_mode.hdisplay);
		radeon_crtc->vsc.full = rfixed_div(a, b);
		a.full = rfixed_const(crtc->mode.hdisplay);
		b.full = rfixed_const(radeon_crtc->native_mode.vdisplay);
		radeon_crtc->hsc.full = rfixed_div(a, b);
	} else {
		radeon_crtc->vsc.full = rfixed_const(1);
		radeon_crtc->hsc.full = rfixed_const(1);
	}
	return true;
}
