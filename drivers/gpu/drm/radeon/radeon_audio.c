/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 * Authors: Slava Grigorev <slava.grigorev@amd.com>
 */

#include <linux/gcd.h>
#include <linux/component.h>

#include <drm/drm_crtc.h>
#include <drm/drm_eld.h>
#include <drm/drm_edid.h>
#include "dce6_afmt.h"
#include "evergreen_hdmi.h"
#include "radeon.h"
#include "atom.h"
#include "r600.h"
#include "radeon_audio.h"

void dce6_audio_enable(struct radeon_device *rdev, struct r600_audio_pin *pin,
		u8 enable_mask);
struct r600_audio_pin *r600_audio_get_pin(struct radeon_device *rdev);
struct r600_audio_pin *dce6_audio_get_pin(struct radeon_device *rdev);
static void radeon_audio_hdmi_mode_set(struct drm_encoder *encoder,
	struct drm_display_mode *mode);
static void radeon_audio_dp_mode_set(struct drm_encoder *encoder,
	struct drm_display_mode *mode);

static const u32 pin_offsets[7] = {
	(0x5e00 - 0x5e00),
	(0x5e18 - 0x5e00),
	(0x5e30 - 0x5e00),
	(0x5e48 - 0x5e00),
	(0x5e60 - 0x5e00),
	(0x5e78 - 0x5e00),
	(0x5e90 - 0x5e00),
};

static u32 radeon_audio_rreg(struct radeon_device *rdev, u32 offset, u32 reg)
{
	return RREG32(reg);
}

static void radeon_audio_wreg(struct radeon_device *rdev, u32 offset,
		u32 reg, u32 v)
{
	WREG32(reg, v);
}

static struct radeon_audio_basic_funcs r600_funcs = {
	.endpoint_rreg = radeon_audio_rreg,
	.endpoint_wreg = radeon_audio_wreg,
	.enable = r600_audio_enable,
};

static struct radeon_audio_basic_funcs dce32_funcs = {
	.endpoint_rreg = radeon_audio_rreg,
	.endpoint_wreg = radeon_audio_wreg,
	.enable = r600_audio_enable,
};

static struct radeon_audio_basic_funcs dce4_funcs = {
	.endpoint_rreg = radeon_audio_rreg,
	.endpoint_wreg = radeon_audio_wreg,
	.enable = dce4_audio_enable,
};

static struct radeon_audio_basic_funcs dce6_funcs = {
	.endpoint_rreg = dce6_endpoint_rreg,
	.endpoint_wreg = dce6_endpoint_wreg,
	.enable = dce6_audio_enable,
};

static struct radeon_audio_funcs r600_hdmi_funcs = {
	.get_pin = r600_audio_get_pin,
	.set_dto = r600_hdmi_audio_set_dto,
	.update_acr = r600_hdmi_update_acr,
	.set_vbi_packet = r600_set_vbi_packet,
	.set_avi_packet = r600_set_avi_packet,
	.set_audio_packet = r600_set_audio_packet,
	.set_mute = r600_set_mute,
	.mode_set = radeon_audio_hdmi_mode_set,
	.dpms = r600_hdmi_enable,
};

static struct radeon_audio_funcs dce32_hdmi_funcs = {
	.get_pin = r600_audio_get_pin,
	.write_sad_regs = dce3_2_afmt_write_sad_regs,
	.write_speaker_allocation = dce3_2_afmt_hdmi_write_speaker_allocation,
	.set_dto = dce3_2_audio_set_dto,
	.update_acr = dce3_2_hdmi_update_acr,
	.set_vbi_packet = r600_set_vbi_packet,
	.set_avi_packet = r600_set_avi_packet,
	.set_audio_packet = dce3_2_set_audio_packet,
	.set_mute = dce3_2_set_mute,
	.mode_set = radeon_audio_hdmi_mode_set,
	.dpms = r600_hdmi_enable,
};

static struct radeon_audio_funcs dce32_dp_funcs = {
	.get_pin = r600_audio_get_pin,
	.write_sad_regs = dce3_2_afmt_write_sad_regs,
	.write_speaker_allocation = dce3_2_afmt_dp_write_speaker_allocation,
	.set_dto = dce3_2_audio_set_dto,
	.set_avi_packet = r600_set_avi_packet,
	.set_audio_packet = dce3_2_set_audio_packet,
};

static struct radeon_audio_funcs dce4_hdmi_funcs = {
	.get_pin = r600_audio_get_pin,
	.write_sad_regs = evergreen_hdmi_write_sad_regs,
	.write_speaker_allocation = dce4_afmt_hdmi_write_speaker_allocation,
	.write_latency_fields = dce4_afmt_write_latency_fields,
	.set_dto = dce4_hdmi_audio_set_dto,
	.update_acr = evergreen_hdmi_update_acr,
	.set_vbi_packet = dce4_set_vbi_packet,
	.set_color_depth = dce4_hdmi_set_color_depth,
	.set_avi_packet = evergreen_set_avi_packet,
	.set_audio_packet = dce4_set_audio_packet,
	.set_mute = dce4_set_mute,
	.mode_set = radeon_audio_hdmi_mode_set,
	.dpms = evergreen_hdmi_enable,
};

static struct radeon_audio_funcs dce4_dp_funcs = {
	.get_pin = r600_audio_get_pin,
	.write_sad_regs = evergreen_hdmi_write_sad_regs,
	.write_speaker_allocation = dce4_afmt_dp_write_speaker_allocation,
	.write_latency_fields = dce4_afmt_write_latency_fields,
	.set_dto = dce4_dp_audio_set_dto,
	.set_avi_packet = evergreen_set_avi_packet,
	.set_audio_packet = dce4_set_audio_packet,
	.mode_set = radeon_audio_dp_mode_set,
	.dpms = evergreen_dp_enable,
};

static struct radeon_audio_funcs dce6_hdmi_funcs = {
	.select_pin = dce6_afmt_select_pin,
	.get_pin = dce6_audio_get_pin,
	.write_sad_regs = dce6_afmt_write_sad_regs,
	.write_speaker_allocation = dce6_afmt_hdmi_write_speaker_allocation,
	.write_latency_fields = dce6_afmt_write_latency_fields,
	.set_dto = dce6_hdmi_audio_set_dto,
	.update_acr = evergreen_hdmi_update_acr,
	.set_vbi_packet = dce4_set_vbi_packet,
	.set_color_depth = dce4_hdmi_set_color_depth,
	.set_avi_packet = evergreen_set_avi_packet,
	.set_audio_packet = dce4_set_audio_packet,
	.set_mute = dce4_set_mute,
	.mode_set = radeon_audio_hdmi_mode_set,
	.dpms = evergreen_hdmi_enable,
};

static struct radeon_audio_funcs dce6_dp_funcs = {
	.select_pin = dce6_afmt_select_pin,
	.get_pin = dce6_audio_get_pin,
	.write_sad_regs = dce6_afmt_write_sad_regs,
	.write_speaker_allocation = dce6_afmt_dp_write_speaker_allocation,
	.write_latency_fields = dce6_afmt_write_latency_fields,
	.set_dto = dce6_dp_audio_set_dto,
	.set_avi_packet = evergreen_set_avi_packet,
	.set_audio_packet = dce4_set_audio_packet,
	.mode_set = radeon_audio_dp_mode_set,
	.dpms = evergreen_dp_enable,
};

static void radeon_audio_component_notify(struct radeon_device *rdev, int port);

static void radeon_audio_enable(struct radeon_device *rdev,
				struct r600_audio_pin *pin, u8 enable_mask)
{
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;
	struct radeon_encoder_atom_dig *dig;
	int pin_count = 0;

	if (!pin)
		return;

	if (rdev->mode_info.mode_config_initialized) {
		list_for_each_entry(encoder, &rdev_to_drm(rdev)->mode_config.encoder_list, head) {
			if (radeon_encoder_is_digital(encoder)) {
				radeon_encoder = to_radeon_encoder(encoder);
				dig = radeon_encoder->enc_priv;
				if (dig->pin == pin)
					pin_count++;
			}
		}

		if ((pin_count > 1) && (enable_mask == 0))
			return;
	}

	if (rdev->audio.funcs->enable)
		rdev->audio.funcs->enable(rdev, pin, enable_mask);

	radeon_audio_component_notify(rdev, pin->id);
}

static void radeon_audio_interface_init(struct radeon_device *rdev)
{
	if (ASIC_IS_DCE6(rdev)) {
		rdev->audio.funcs = &dce6_funcs;
		rdev->audio.hdmi_funcs = &dce6_hdmi_funcs;
		rdev->audio.dp_funcs = &dce6_dp_funcs;
	} else if (ASIC_IS_DCE4(rdev)) {
		rdev->audio.funcs = &dce4_funcs;
		rdev->audio.hdmi_funcs = &dce4_hdmi_funcs;
		rdev->audio.dp_funcs = &dce4_dp_funcs;
	} else if (ASIC_IS_DCE32(rdev)) {
		rdev->audio.funcs = &dce32_funcs;
		rdev->audio.hdmi_funcs = &dce32_hdmi_funcs;
		rdev->audio.dp_funcs = &dce32_dp_funcs;
	} else {
		rdev->audio.funcs = &r600_funcs;
		rdev->audio.hdmi_funcs = &r600_hdmi_funcs;
		rdev->audio.dp_funcs = NULL;
	}
}

static int radeon_audio_chipset_supported(struct radeon_device *rdev)
{
	return ASIC_IS_DCE2(rdev) && !ASIC_IS_NODCE(rdev);
}

int radeon_audio_init(struct radeon_device *rdev)
{
	int i;

	if (!radeon_audio || !radeon_audio_chipset_supported(rdev))
		return 0;

	rdev->audio.enabled = true;

	if (ASIC_IS_DCE83(rdev))		/* KB: 2 streams, 3 endpoints */
		rdev->audio.num_pins = 3;
	else if (ASIC_IS_DCE81(rdev))	/* KV: 4 streams, 7 endpoints */
		rdev->audio.num_pins = 7;
	else if (ASIC_IS_DCE8(rdev))	/* BN/HW: 6 streams, 7 endpoints */
		rdev->audio.num_pins = 7;
	else if (ASIC_IS_DCE64(rdev))	/* OL: 2 streams, 2 endpoints */
		rdev->audio.num_pins = 2;
	else if (ASIC_IS_DCE61(rdev))	/* TN: 4 streams, 6 endpoints */
		rdev->audio.num_pins = 6;
	else if (ASIC_IS_DCE6(rdev))	/* SI: 6 streams, 6 endpoints */
		rdev->audio.num_pins = 6;
	else
		rdev->audio.num_pins = 1;

	for (i = 0; i < rdev->audio.num_pins; i++) {
		rdev->audio.pin[i].channels = -1;
		rdev->audio.pin[i].rate = -1;
		rdev->audio.pin[i].bits_per_sample = -1;
		rdev->audio.pin[i].status_bits = 0;
		rdev->audio.pin[i].category_code = 0;
		rdev->audio.pin[i].connected = false;
		rdev->audio.pin[i].offset = pin_offsets[i];
		rdev->audio.pin[i].id = i;
	}

	radeon_audio_interface_init(rdev);

	/* disable audio.  it will be set up later */
	for (i = 0; i < rdev->audio.num_pins; i++)
		radeon_audio_enable(rdev, &rdev->audio.pin[i], 0);

	return 0;
}

u32 radeon_audio_endpoint_rreg(struct radeon_device *rdev, u32 offset, u32 reg)
{
	if (rdev->audio.funcs->endpoint_rreg)
		return rdev->audio.funcs->endpoint_rreg(rdev, offset, reg);

	return 0;
}

void radeon_audio_endpoint_wreg(struct radeon_device *rdev, u32 offset,
	u32 reg, u32 v)
{
	if (rdev->audio.funcs->endpoint_wreg)
		rdev->audio.funcs->endpoint_wreg(rdev, offset, reg, v);
}

static void radeon_audio_write_sad_regs(struct drm_encoder *encoder)
{
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct cea_sad *sads;
	int sad_count;

	if (!connector)
		return;

	sad_count = drm_edid_to_sad(radeon_connector->edid, &sads);
	if (sad_count < 0)
		DRM_ERROR("Couldn't read SADs: %d\n", sad_count);
	if (sad_count <= 0)
		return;
	BUG_ON(!sads);

	if (radeon_encoder->audio && radeon_encoder->audio->write_sad_regs)
		radeon_encoder->audio->write_sad_regs(encoder, sads, sad_count);

	kfree(sads);
}

static void radeon_audio_write_speaker_allocation(struct drm_encoder *encoder)
{
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	u8 *sadb = NULL;
	int sad_count;

	if (!connector)
		return;

	sad_count = drm_edid_to_speaker_allocation(radeon_connector->edid, &sadb);
	if (sad_count < 0) {
		DRM_DEBUG("Couldn't read Speaker Allocation Data Block: %d\n",
			  sad_count);
		sad_count = 0;
	}

	if (radeon_encoder->audio && radeon_encoder->audio->write_speaker_allocation)
		radeon_encoder->audio->write_speaker_allocation(encoder, sadb, sad_count);

	kfree(sadb);
}

static void radeon_audio_write_latency_fields(struct drm_encoder *encoder,
					      struct drm_display_mode *mode)
{
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

	if (!connector)
		return;

	if (radeon_encoder->audio && radeon_encoder->audio->write_latency_fields)
		radeon_encoder->audio->write_latency_fields(encoder, connector, mode);
}

struct r600_audio_pin *radeon_audio_get_pin(struct drm_encoder *encoder)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

	if (radeon_encoder->audio && radeon_encoder->audio->get_pin)
		return radeon_encoder->audio->get_pin(rdev);

	return NULL;
}

static void radeon_audio_select_pin(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

	if (radeon_encoder->audio && radeon_encoder->audio->select_pin)
		radeon_encoder->audio->select_pin(encoder);
}

void radeon_audio_detect(struct drm_connector *connector,
			 struct drm_encoder *encoder,
			 enum drm_connector_status status)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig;

	if (!radeon_audio_chipset_supported(rdev))
		return;

	if (!radeon_encoder_is_digital(encoder))
		return;

	dig = radeon_encoder->enc_priv;

	if (status == connector_status_connected) {
		if (connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort) {
			struct radeon_connector *radeon_connector = to_radeon_connector(connector);

			if (radeon_dp_getsinktype(radeon_connector) ==
			    CONNECTOR_OBJECT_ID_DISPLAYPORT)
				radeon_encoder->audio = rdev->audio.dp_funcs;
			else
				radeon_encoder->audio = rdev->audio.hdmi_funcs;
		} else {
			radeon_encoder->audio = rdev->audio.hdmi_funcs;
		}

		if (connector->display_info.has_audio) {
			if (!dig->pin)
				dig->pin = radeon_audio_get_pin(encoder);
			radeon_audio_enable(rdev, dig->pin, 0xf);
		} else {
			radeon_audio_enable(rdev, dig->pin, 0);
			dig->pin = NULL;
		}
	} else {
		radeon_audio_enable(rdev, dig->pin, 0);
		dig->pin = NULL;
	}
}

void radeon_audio_fini(struct radeon_device *rdev)
{
	int i;

	if (!rdev->audio.enabled)
		return;

	for (i = 0; i < rdev->audio.num_pins; i++)
		radeon_audio_enable(rdev, &rdev->audio.pin[i], 0);

	rdev->audio.enabled = false;
}

static void radeon_audio_set_dto(struct drm_encoder *encoder, unsigned int clock)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_crtc *crtc = to_radeon_crtc(encoder->crtc);

	if (radeon_encoder->audio && radeon_encoder->audio->set_dto)
		radeon_encoder->audio->set_dto(rdev, crtc, clock);
}

static int radeon_audio_set_avi_packet(struct drm_encoder *encoder,
				       struct drm_display_mode *mode)
{
	struct radeon_device *rdev = encoder->dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AVI_INFOFRAME_SIZE];
	struct hdmi_avi_infoframe frame;
	int err;

	if (!connector)
		return -EINVAL;

	err = drm_hdmi_avi_infoframe_from_display_mode(&frame, connector, mode);
	if (err < 0) {
		DRM_ERROR("failed to setup AVI infoframe: %d\n", err);
		return err;
	}

	if (radeon_encoder->output_csc != RADEON_OUTPUT_CSC_BYPASS) {
		drm_hdmi_avi_infoframe_quant_range(&frame, connector, mode,
						   radeon_encoder->output_csc == RADEON_OUTPUT_CSC_TVRGB ?
						   HDMI_QUANTIZATION_RANGE_LIMITED :
						   HDMI_QUANTIZATION_RANGE_FULL);
	}

	err = hdmi_avi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		DRM_ERROR("failed to pack AVI infoframe: %d\n", err);
		return err;
	}

	if (dig && dig->afmt && radeon_encoder->audio &&
	    radeon_encoder->audio->set_avi_packet)
		radeon_encoder->audio->set_avi_packet(rdev, dig->afmt->offset,
			buffer, sizeof(buffer));

	return 0;
}

/*
 * calculate CTS and N values if they are not found in the table
 */
static void radeon_audio_calc_cts(unsigned int clock, int *CTS, int *N, int freq)
{
	int n, cts;
	unsigned long div, mul;

	/* Safe, but overly large values */
	n = 128 * freq;
	cts = clock * 1000;

	/* Smallest valid fraction */
	div = gcd(n, cts);

	n /= div;
	cts /= div;

	/*
	 * The optimal N is 128*freq/1000. Calculate the closest larger
	 * value that doesn't truncate any bits.
	 */
	mul = ((128*freq/1000) + (n-1))/n;

	n *= mul;
	cts *= mul;

	/* Check that we are in spec (not always possible) */
	if (n < (128*freq/1500))
		pr_warn("Calculated ACR N value is too small. You may experience audio problems.\n");
	if (n > (128*freq/300))
		pr_warn("Calculated ACR N value is too large. You may experience audio problems.\n");

	*N = n;
	*CTS = cts;

	DRM_DEBUG("Calculated ACR timing N=%d CTS=%d for frequency %d\n",
		*N, *CTS, freq);
}

static const struct radeon_hdmi_acr *radeon_audio_acr(unsigned int clock)
{
	static struct radeon_hdmi_acr res;
	u8 i;

	static const struct radeon_hdmi_acr hdmi_predefined_acr[] = {
		/*       32kHz    44.1kHz   48kHz    */
		/* Clock      N     CTS      N     CTS      N     CTS */
		{  25175,  4096,  25175, 28224, 125875,  6144,  25175 }, /*  25,20/1.001 MHz */
		{  25200,  4096,  25200,  6272,  28000,  6144,  25200 }, /*  25.20       MHz */
		{  27000,  4096,  27000,  6272,  30000,  6144,  27000 }, /*  27.00       MHz */
		{  27027,  4096,  27027,  6272,  30030,  6144,  27027 }, /*  27.00*1.001 MHz */
		{  54000,  4096,  54000,  6272,  60000,  6144,  54000 }, /*  54.00       MHz */
		{  54054,  4096,  54054,  6272,  60060,  6144,  54054 }, /*  54.00*1.001 MHz */
		{  74176,  4096,  74176,  5733,  75335,  6144,  74176 }, /*  74.25/1.001 MHz */
		{  74250,  4096,  74250,  6272,  82500,  6144,  74250 }, /*  74.25       MHz */
		{ 148352,  4096, 148352,  5733, 150670,  6144, 148352 }, /* 148.50/1.001 MHz */
		{ 148500,  4096, 148500,  6272, 165000,  6144, 148500 }, /* 148.50       MHz */
	};

	/* Precalculated values for common clocks */
	for (i = 0; i < ARRAY_SIZE(hdmi_predefined_acr); i++)
		if (hdmi_predefined_acr[i].clock == clock)
			return &hdmi_predefined_acr[i];

	/* And odd clocks get manually calculated */
	radeon_audio_calc_cts(clock, &res.cts_32khz, &res.n_32khz, 32000);
	radeon_audio_calc_cts(clock, &res.cts_44_1khz, &res.n_44_1khz, 44100);
	radeon_audio_calc_cts(clock, &res.cts_48khz, &res.n_48khz, 48000);

	return &res;
}

/*
 * update the N and CTS parameters for a given pixel clock rate
 */
static void radeon_audio_update_acr(struct drm_encoder *encoder, unsigned int clock)
{
	const struct radeon_hdmi_acr *acr = radeon_audio_acr(clock);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;

	if (!dig || !dig->afmt)
		return;

	if (radeon_encoder->audio && radeon_encoder->audio->update_acr)
		radeon_encoder->audio->update_acr(encoder, dig->afmt->offset, acr);
}

static void radeon_audio_set_vbi_packet(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;

	if (!dig || !dig->afmt)
		return;

	if (radeon_encoder->audio && radeon_encoder->audio->set_vbi_packet)
		radeon_encoder->audio->set_vbi_packet(encoder, dig->afmt->offset);
}

static void radeon_hdmi_set_color_depth(struct drm_encoder *encoder)
{
	int bpc = 8;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;

	if (!dig || !dig->afmt)
		return;

	if (encoder->crtc) {
		struct radeon_crtc *radeon_crtc = to_radeon_crtc(encoder->crtc);
		bpc = radeon_crtc->bpc;
	}

	if (radeon_encoder->audio && radeon_encoder->audio->set_color_depth)
		radeon_encoder->audio->set_color_depth(encoder, dig->afmt->offset, bpc);
}

static void radeon_audio_set_audio_packet(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;

	if (!dig || !dig->afmt)
		return;

	if (radeon_encoder->audio && radeon_encoder->audio->set_audio_packet)
		radeon_encoder->audio->set_audio_packet(encoder, dig->afmt->offset);
}

static void radeon_audio_set_mute(struct drm_encoder *encoder, bool mute)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;

	if (!dig || !dig->afmt)
		return;

	if (radeon_encoder->audio && radeon_encoder->audio->set_mute)
		radeon_encoder->audio->set_mute(encoder, dig->afmt->offset, mute);
}

/*
 * update the info frames with the data from the current display mode
 */
static void radeon_audio_hdmi_mode_set(struct drm_encoder *encoder,
				       struct drm_display_mode *mode)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);

	if (!dig || !dig->afmt)
		return;

	if (!connector)
		return;

	if (connector->display_info.has_audio) {
		radeon_audio_set_mute(encoder, true);

		radeon_audio_write_speaker_allocation(encoder);
		radeon_audio_write_sad_regs(encoder);
		radeon_audio_write_latency_fields(encoder, mode);
		radeon_audio_set_dto(encoder, mode->clock);
		radeon_audio_set_vbi_packet(encoder);
		radeon_hdmi_set_color_depth(encoder);
		radeon_audio_update_acr(encoder, mode->clock);
		radeon_audio_set_audio_packet(encoder);
		radeon_audio_select_pin(encoder);

		if (radeon_audio_set_avi_packet(encoder, mode) < 0)
			return;

		radeon_audio_set_mute(encoder, false);
	} else {
		radeon_hdmi_set_color_depth(encoder);

		if (radeon_audio_set_avi_packet(encoder, mode) < 0)
			return;
	}
}

static void radeon_audio_dp_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct drm_connector *connector = radeon_get_connector_for_encoder(encoder);

	if (!dig || !dig->afmt)
		return;

	if (!connector)
		return;

	if (connector->display_info.has_audio) {
		radeon_audio_write_speaker_allocation(encoder);
		radeon_audio_write_sad_regs(encoder);
		radeon_audio_write_latency_fields(encoder, mode);
		radeon_audio_set_dto(encoder, rdev->clock.vco_freq * 10);
		radeon_audio_set_audio_packet(encoder);
		radeon_audio_select_pin(encoder);

		if (radeon_audio_set_avi_packet(encoder, mode) < 0)
			return;
	}
}

void radeon_audio_mode_set(struct drm_encoder *encoder,
			   struct drm_display_mode *mode)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

	if (radeon_encoder->audio && radeon_encoder->audio->mode_set)
		radeon_encoder->audio->mode_set(encoder, mode);
}

void radeon_audio_dpms(struct drm_encoder *encoder, int mode)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

	if (radeon_encoder->audio && radeon_encoder->audio->dpms)
		radeon_encoder->audio->dpms(encoder, mode == DRM_MODE_DPMS_ON);
}

unsigned int radeon_audio_decode_dfs_div(unsigned int div)
{
	if (div >= 8 && div < 64)
		return (div - 8) * 25 + 200;
	else if (div >= 64 && div < 96)
		return (div - 64) * 50 + 1600;
	else if (div >= 96 && div < 128)
		return (div - 96) * 100 + 3200;
	else
		return 0;
}

/*
 * Audio component support
 */
static void radeon_audio_component_notify(struct radeon_device *rdev, int port)
{
	struct drm_audio_component *acomp;

	mutex_lock(&rdev->audio.component_mutex);
	acomp = rdev->audio.component;
	if (acomp && acomp->audio_ops && acomp->audio_ops->pin_eld_notify)
		acomp->audio_ops->pin_eld_notify(acomp->audio_ops->audio_ptr,
						 port, -1);
	mutex_unlock(&rdev->audio.component_mutex);
}

static int radeon_audio_component_get_eld(struct device *kdev, int port,
					  int pipe, bool *enabled,
					  unsigned char *buf, int max_bytes)
{
	struct drm_device *dev = dev_get_drvdata(kdev);
	struct radeon_device *rdev = dev->dev_private;
	struct drm_encoder *encoder;
	struct radeon_encoder *radeon_encoder;
	struct radeon_encoder_atom_dig *dig;
	struct drm_connector *connector;
	int ret = 0;

	*enabled = false;
	if (!rdev->audio.enabled || !rdev->mode_info.mode_config_initialized)
		return 0;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		const struct drm_connector_helper_funcs *connector_funcs =
				connector->helper_private;
		encoder = connector_funcs->best_encoder(connector);

		if (!encoder)
			continue;

		if (!radeon_encoder_is_digital(encoder))
			continue;
		radeon_encoder = to_radeon_encoder(encoder);
		dig = radeon_encoder->enc_priv;
		if (!dig->pin || dig->pin->id != port)
			continue;
		*enabled = true;
		mutex_lock(&connector->eld_mutex);
		ret = drm_eld_size(connector->eld);
		memcpy(buf, connector->eld, min(max_bytes, ret));
		mutex_unlock(&connector->eld_mutex);
		break;
	}

	return ret;
}

static const struct drm_audio_component_ops radeon_audio_component_ops = {
	.get_eld = radeon_audio_component_get_eld,
};

static int radeon_audio_component_bind(struct device *kdev,
				       struct device *hda_kdev, void *data)
{
	struct drm_device *dev = dev_get_drvdata(kdev);
	struct radeon_device *rdev = dev->dev_private;
	struct drm_audio_component *acomp = data;

	if (WARN_ON(!device_link_add(hda_kdev, kdev, DL_FLAG_STATELESS)))
		return -ENOMEM;

	mutex_lock(&rdev->audio.component_mutex);
	acomp->ops = &radeon_audio_component_ops;
	acomp->dev = kdev;
	rdev->audio.component = acomp;
	mutex_unlock(&rdev->audio.component_mutex);

	return 0;
}

static void radeon_audio_component_unbind(struct device *kdev,
					  struct device *hda_kdev, void *data)
{
	struct drm_device *dev = dev_get_drvdata(kdev);
	struct radeon_device *rdev = dev->dev_private;
	struct drm_audio_component *acomp = data;

	device_link_remove(hda_kdev, kdev);

	mutex_lock(&rdev->audio.component_mutex);
	rdev->audio.component = NULL;
	acomp->ops = NULL;
	acomp->dev = NULL;
	mutex_unlock(&rdev->audio.component_mutex);
}

static const struct component_ops radeon_audio_component_bind_ops = {
	.bind	= radeon_audio_component_bind,
	.unbind	= radeon_audio_component_unbind,
};

void radeon_audio_component_init(struct radeon_device *rdev)
{
	if (rdev->audio.component_registered ||
	    !radeon_audio || !radeon_audio_chipset_supported(rdev))
		return;

	if (!component_add(rdev->dev, &radeon_audio_component_bind_ops))
		rdev->audio.component_registered = true;
}

void radeon_audio_component_fini(struct radeon_device *rdev)
{
	if (rdev->audio.component_registered) {
		component_del(rdev->dev, &radeon_audio_component_bind_ops);
		rdev->audio.component_registered = false;
	}
}
