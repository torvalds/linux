/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "dc.h"
#include "modules/color/color_gamma.h"
#include "basics/conversion.h"

/*
 * The DC interface to HW gives us the following color management blocks
 * per pipe (surface):
 *
 * - Input gamma LUT (de-normalized)
 * - Input CSC (normalized)
 * - Surface degamma LUT (normalized)
 * - Surface CSC (normalized)
 * - Surface regamma LUT (normalized)
 * - Output CSC (normalized)
 *
 * But these aren't a direct mapping to DRM color properties. The current DRM
 * interface exposes CRTC degamma, CRTC CTM and CRTC regamma while our hardware
 * is essentially giving:
 *
 * Plane CTM -> Plane degamma -> Plane CTM -> Plane regamma -> Plane CTM
 *
 * The input gamma LUT block isn't really applicable here since it operates
 * on the actual input data itself rather than the HW fp representation. The
 * input and output CSC blocks are technically available to use as part of
 * the DC interface but are typically used internally by DC for conversions
 * between color spaces. These could be blended together with user
 * adjustments in the future but for now these should remain untouched.
 *
 * The pipe blending also happens after these blocks so we don't actually
 * support any CRTC props with correct blending with multiple planes - but we
 * can still support CRTC color management properties in DM in most single
 * plane cases correctly with clever management of the DC interface in DM.
 *
 * As per DRM documentation, blocks should be in hardware bypass when their
 * respective property is set to NULL. A linear DGM/RGM LUT should also
 * considered as putting the respective block into bypass mode.
 *
 * This means that the following
 * configuration is assumed to be the default:
 *
 * Plane DGM Bypass -> Plane CTM Bypass -> Plane RGM Bypass -> ...
 * CRTC DGM Bypass -> CRTC CTM Bypass -> CRTC RGM Bypass
 */

#define MAX_DRM_LUT_VALUE 0xFFFF

/*
 * Initialize the color module.
 *
 * We're not using the full color module, only certain components.
 * Only call setup functions for components that we need.
 */
void amdgpu_dm_init_color_mod(void)
{
	setup_x_points_distribution();
}

/* Extracts the DRM lut and lut size from a blob. */
static const struct drm_color_lut *
__extract_blob_lut(const struct drm_property_blob *blob, uint32_t *size)
{
	*size = blob ? drm_color_lut_size(blob) : 0;
	return blob ? (struct drm_color_lut *)blob->data : NULL;
}

/*
 * Return true if the given lut is a linear mapping of values, i.e. it acts
 * like a bypass LUT.
 *
 * It is considered linear if the lut represents:
 * f(a) = (0xFF00/MAX_COLOR_LUT_ENTRIES-1)a; for integer a in
 *                                           [0, MAX_COLOR_LUT_ENTRIES)
 */
static bool __is_lut_linear(const struct drm_color_lut *lut, uint32_t size)
{
	int i;
	uint32_t expected;
	int delta;

	for (i = 0; i < size; i++) {
		/* All color values should equal */
		if ((lut[i].red != lut[i].green) || (lut[i].green != lut[i].blue))
			return false;

		expected = i * MAX_DRM_LUT_VALUE / (size-1);

		/* Allow a +/-1 error. */
		delta = lut[i].red - expected;
		if (delta < -1 || 1 < delta)
			return false;
	}
	return true;
}

/**
 * Convert the drm_color_lut to dc_gamma. The conversion depends on the size
 * of the lut - whether or not it's legacy.
 */
static void __drm_lut_to_dc_gamma(const struct drm_color_lut *lut,
				  struct dc_gamma *gamma, bool is_legacy)
{
	uint32_t r, g, b;
	int i;

	if (is_legacy) {
		for (i = 0; i < MAX_COLOR_LEGACY_LUT_ENTRIES; i++) {
			r = drm_color_lut_extract(lut[i].red, 16);
			g = drm_color_lut_extract(lut[i].green, 16);
			b = drm_color_lut_extract(lut[i].blue, 16);

			gamma->entries.red[i] = dc_fixpt_from_int(r);
			gamma->entries.green[i] = dc_fixpt_from_int(g);
			gamma->entries.blue[i] = dc_fixpt_from_int(b);
		}
		return;
	}

	/* else */
	for (i = 0; i < MAX_COLOR_LUT_ENTRIES; i++) {
		r = drm_color_lut_extract(lut[i].red, 16);
		g = drm_color_lut_extract(lut[i].green, 16);
		b = drm_color_lut_extract(lut[i].blue, 16);

		gamma->entries.red[i] = dc_fixpt_from_fraction(r, MAX_DRM_LUT_VALUE);
		gamma->entries.green[i] = dc_fixpt_from_fraction(g, MAX_DRM_LUT_VALUE);
		gamma->entries.blue[i] = dc_fixpt_from_fraction(b, MAX_DRM_LUT_VALUE);
	}
}

/*
 * Converts a DRM CTM to a DC CSC float matrix.
 * The matrix needs to be a 3x4 (12 entry) matrix.
 */
static void __drm_ctm_to_dc_matrix(const struct drm_color_ctm *ctm,
				   struct fixed31_32 *matrix)
{
	int64_t val;
	int i;

	/*
	 * DRM gives a 3x3 matrix, but DC wants 3x4. Assuming we're operating
	 * with homogeneous coordinates, augment the matrix with 0's.
	 *
	 * The format provided is S31.32, using signed-magnitude representation.
	 * Our fixed31_32 is also S31.32, but is using 2's complement. We have
	 * to convert from signed-magnitude to 2's complement.
	 */
	for (i = 0; i < 12; i++) {
		/* Skip 4th element */
		if (i % 4 == 3) {
			matrix[i] = dc_fixpt_zero;
			continue;
		}

		/* gamut_remap_matrix[i] = ctm[i - floor(i/4)] */
		val = ctm->matrix[i - (i / 4)];
		/* If negative, convert to 2's complement. */
		if (val & (1ULL << 63))
			val = -(val & ~(1ULL << 63));

		matrix[i].value = val;
	}
}

/* Calculates the legacy transfer function - only for sRGB input space. */
static int __set_legacy_tf(struct dc_transfer_func *func,
			   const struct drm_color_lut *lut, uint32_t lut_size,
			   bool has_rom)
{
	struct dc_gamma *gamma = NULL;
	struct calculate_buffer cal_buffer = {0};
	bool res;

	ASSERT(lut && lut_size == MAX_COLOR_LEGACY_LUT_ENTRIES);

	cal_buffer.buffer_index = -1;

	gamma = dc_create_gamma();
	if (!gamma)
		return -ENOMEM;

	gamma->type = GAMMA_RGB_256;
	gamma->num_entries = lut_size;
	__drm_lut_to_dc_gamma(lut, gamma, true);

	res = mod_color_calculate_regamma_params(func, gamma, true, has_rom,
						 NULL, &cal_buffer);

	dc_gamma_release(&gamma);

	return res ? 0 : -ENOMEM;
}

/* Calculates the output transfer function based on expected input space. */
static int __set_output_tf(struct dc_transfer_func *func,
			   const struct drm_color_lut *lut, uint32_t lut_size,
			   bool has_rom)
{
	struct dc_gamma *gamma = NULL;
	struct calculate_buffer cal_buffer = {0};
	bool res;

	ASSERT(lut && lut_size == MAX_COLOR_LUT_ENTRIES);

	cal_buffer.buffer_index = -1;

	gamma = dc_create_gamma();
	if (!gamma)
		return -ENOMEM;

	gamma->num_entries = lut_size;
	__drm_lut_to_dc_gamma(lut, gamma, false);

	if (func->tf == TRANSFER_FUNCTION_LINEAR) {
		/*
		 * Color module doesn't like calculating regamma params
		 * on top of a linear input. But degamma params can be used
		 * instead to simulate this.
		 */
		gamma->type = GAMMA_CUSTOM;
		res = mod_color_calculate_degamma_params(NULL, func,
							gamma, true);
	} else {
		/*
		 * Assume sRGB. The actual mapping will depend on whether the
		 * input was legacy or not.
		 */
		gamma->type = GAMMA_CS_TFM_1D;
		res = mod_color_calculate_regamma_params(func, gamma, false,
							 has_rom, NULL, &cal_buffer);
	}

	dc_gamma_release(&gamma);

	return res ? 0 : -ENOMEM;
}

/* Caculates the input transfer function based on expected input space. */
static int __set_input_tf(struct dc_transfer_func *func,
			  const struct drm_color_lut *lut, uint32_t lut_size)
{
	struct dc_gamma *gamma = NULL;
	bool res;

	gamma = dc_create_gamma();
	if (!gamma)
		return -ENOMEM;

	gamma->type = GAMMA_CUSTOM;
	gamma->num_entries = lut_size;

	__drm_lut_to_dc_gamma(lut, gamma, false);

	res = mod_color_calculate_degamma_params(NULL, func, gamma, true);
	dc_gamma_release(&gamma);

	return res ? 0 : -ENOMEM;
}

/**
 * amdgpu_dm_update_crtc_color_mgmt: Maps DRM color management to DC stream.
 * @crtc: amdgpu_dm crtc state
 *
 * With no plane level color management properties we're free to use any
 * of the HW blocks as long as the CRTC CTM always comes before the
 * CRTC RGM and after the CRTC DGM.
 *
 * The CRTC RGM block will be placed in the RGM LUT block if it is non-linear.
 * The CRTC DGM block will be placed in the DGM LUT block if it is non-linear.
 * The CRTC CTM will be placed in the gamut remap block if it is non-linear.
 *
 * The RGM block is typically more fully featured and accurate across
 * all ASICs - DCE can't support a custom non-linear CRTC DGM.
 *
 * For supporting both plane level color management and CRTC level color
 * management at once we have to either restrict the usage of CRTC properties
 * or blend adjustments together.
 *
 * Returns 0 on success.
 */
int amdgpu_dm_update_crtc_color_mgmt(struct dm_crtc_state *crtc)
{
	struct dc_stream_state *stream = crtc->stream;
	struct amdgpu_device *adev =
		(struct amdgpu_device *)crtc->base.state->dev->dev_private;
	bool has_rom = adev->asic_type <= CHIP_RAVEN;
	struct drm_color_ctm *ctm = NULL;
	const struct drm_color_lut *degamma_lut, *regamma_lut;
	uint32_t degamma_size, regamma_size;
	bool has_regamma, has_degamma;
	bool is_legacy;
	int r;

	degamma_lut = __extract_blob_lut(crtc->base.degamma_lut, &degamma_size);
	if (degamma_lut && degamma_size != MAX_COLOR_LUT_ENTRIES)
		return -EINVAL;

	regamma_lut = __extract_blob_lut(crtc->base.gamma_lut, &regamma_size);
	if (regamma_lut && regamma_size != MAX_COLOR_LUT_ENTRIES &&
	    regamma_size != MAX_COLOR_LEGACY_LUT_ENTRIES)
		return -EINVAL;

	has_degamma =
		degamma_lut && !__is_lut_linear(degamma_lut, degamma_size);

	has_regamma =
		regamma_lut && !__is_lut_linear(regamma_lut, regamma_size);

	is_legacy = regamma_size == MAX_COLOR_LEGACY_LUT_ENTRIES;

	/* Reset all adjustments. */
	crtc->cm_has_degamma = false;
	crtc->cm_is_degamma_srgb = false;

	/* Setup regamma and degamma. */
	if (is_legacy) {
		/*
		 * Legacy regamma forces us to use the sRGB RGM as a base.
		 * This also means we can't use linear DGM since DGM needs
		 * to use sRGB as a base as well, resulting in incorrect CRTC
		 * DGM and CRTC CTM.
		 *
		 * TODO: Just map this to the standard regamma interface
		 * instead since this isn't really right. One of the cases
		 * where this setup currently fails is trying to do an
		 * inverse color ramp in legacy userspace.
		 */
		crtc->cm_is_degamma_srgb = true;
		stream->out_transfer_func->type = TF_TYPE_DISTRIBUTED_POINTS;
		stream->out_transfer_func->tf = TRANSFER_FUNCTION_SRGB;

		r = __set_legacy_tf(stream->out_transfer_func, regamma_lut,
				    regamma_size, has_rom);
		if (r)
			return r;
	} else if (has_regamma) {
		/* CRTC RGM goes into RGM LUT. */
		stream->out_transfer_func->type = TF_TYPE_DISTRIBUTED_POINTS;
		stream->out_transfer_func->tf = TRANSFER_FUNCTION_LINEAR;

		r = __set_output_tf(stream->out_transfer_func, regamma_lut,
				    regamma_size, has_rom);
		if (r)
			return r;
	} else {
		/*
		 * No CRTC RGM means we can just put the block into bypass
		 * since we don't have any plane level adjustments using it.
		 */
		stream->out_transfer_func->type = TF_TYPE_BYPASS;
		stream->out_transfer_func->tf = TRANSFER_FUNCTION_LINEAR;
	}

	/*
	 * CRTC DGM goes into DGM LUT. It would be nice to place it
	 * into the RGM since it's a more featured block but we'd
	 * have to place the CTM in the OCSC in that case.
	 */
	crtc->cm_has_degamma = has_degamma;

	/* Setup CRTC CTM. */
	if (crtc->base.ctm) {
		ctm = (struct drm_color_ctm *)crtc->base.ctm->data;

		/*
		 * Gamut remapping must be used for gamma correction
		 * since it comes before the regamma correction.
		 *
		 * OCSC could be used for gamma correction, but we'd need to
		 * blend the adjustments together with the required output
		 * conversion matrix - so just use the gamut remap block
		 * for now.
		 */
		__drm_ctm_to_dc_matrix(ctm, stream->gamut_remap_matrix.matrix);

		stream->gamut_remap_matrix.enable_remap = true;
		stream->csc_color_matrix.enable_adjustment = false;
	} else {
		/* Bypass CTM. */
		stream->gamut_remap_matrix.enable_remap = false;
		stream->csc_color_matrix.enable_adjustment = false;
	}

	return 0;
}

/**
 * amdgpu_dm_update_plane_color_mgmt: Maps DRM color management to DC plane.
 * @crtc: amdgpu_dm crtc state
 * @ dc_plane_state: target DC surface
 *
 * Update the underlying dc_stream_state's input transfer function (ITF) in
 * preparation for hardware commit. The transfer function used depends on
 * the prepartion done on the stream for color management.
 *
 * Returns 0 on success.
 */
int amdgpu_dm_update_plane_color_mgmt(struct dm_crtc_state *crtc,
				      struct dc_plane_state *dc_plane_state)
{
	const struct drm_color_lut *degamma_lut;
	enum dc_transfer_func_predefined tf = TRANSFER_FUNCTION_SRGB;
	uint32_t degamma_size;
	int r;

	/* Get the correct base transfer function for implicit degamma. */
	switch (dc_plane_state->format) {
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		/* DC doesn't have a transfer function for BT601 specifically. */
		tf = TRANSFER_FUNCTION_BT709;
		break;
	default:
		break;
	}

	if (crtc->cm_has_degamma) {
		degamma_lut = __extract_blob_lut(crtc->base.degamma_lut,
						 &degamma_size);
		ASSERT(degamma_size == MAX_COLOR_LUT_ENTRIES);

		dc_plane_state->in_transfer_func->type =
			TF_TYPE_DISTRIBUTED_POINTS;

		/*
		 * This case isn't fully correct, but also fairly
		 * uncommon. This is userspace trying to use a
		 * legacy gamma LUT + atomic degamma LUT
		 * at the same time.
		 *
		 * Legacy gamma requires the input to be in linear
		 * space, so that means we need to apply an sRGB
		 * degamma. But color module also doesn't support
		 * a user ramp in this case so the degamma will
		 * be lost.
		 *
		 * Even if we did support it, it's still not right:
		 *
		 * Input -> CRTC DGM -> sRGB DGM -> CRTC CTM ->
		 * sRGB RGM -> CRTC RGM -> Output
		 *
		 * The CSC will be done in the wrong space since
		 * we're applying an sRGB DGM on top of the CRTC
		 * DGM.
		 *
		 * TODO: Don't use the legacy gamma interface and just
		 * map these to the atomic one instead.
		 */
		if (crtc->cm_is_degamma_srgb)
			dc_plane_state->in_transfer_func->tf = tf;
		else
			dc_plane_state->in_transfer_func->tf =
				TRANSFER_FUNCTION_LINEAR;

		r = __set_input_tf(dc_plane_state->in_transfer_func,
				   degamma_lut, degamma_size);
		if (r)
			return r;
	} else if (crtc->cm_is_degamma_srgb) {
		/*
		 * For legacy gamma support we need the regamma input
		 * in linear space. Assume that the input is sRGB.
		 */
		dc_plane_state->in_transfer_func->type = TF_TYPE_PREDEFINED;
		dc_plane_state->in_transfer_func->tf = tf;

		if (tf != TRANSFER_FUNCTION_SRGB &&
		    !mod_color_calculate_degamma_params(NULL,
			    dc_plane_state->in_transfer_func, NULL, false))
			return -ENOMEM;
	} else {
		/* ...Otherwise we can just bypass the DGM block. */
		dc_plane_state->in_transfer_func->type = TF_TYPE_BYPASS;
		dc_plane_state->in_transfer_func->tf = TRANSFER_FUNCTION_LINEAR;
	}

	return 0;
}
