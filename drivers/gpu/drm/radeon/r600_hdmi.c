/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Christian König.
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
 * Authors: Christian König
 */
#include <linux/hdmi.h>
#include <linux/gcd.h>
#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "r600d.h"
#include "atom.h"

/*
 * HDMI color format
 */
enum r600_hdmi_color_format {
	RGB = 0,
	YCC_422 = 1,
	YCC_444 = 2
};

/*
 * IEC60958 status bits
 */
enum r600_hdmi_iec_status_bits {
	AUDIO_STATUS_DIG_ENABLE   = 0x01,
	AUDIO_STATUS_V            = 0x02,
	AUDIO_STATUS_VCFG         = 0x04,
	AUDIO_STATUS_EMPHASIS     = 0x08,
	AUDIO_STATUS_COPYRIGHT    = 0x10,
	AUDIO_STATUS_NONAUDIO     = 0x20,
	AUDIO_STATUS_PROFESSIONAL = 0x40,
	AUDIO_STATUS_LEVEL        = 0x80
};

static const struct radeon_hdmi_acr r600_hdmi_predefined_acr[] = {
    /*	     32kHz	  44.1kHz	48kHz    */
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


/*
 * check if the chipset is supported
 */
static int r600_audio_chipset_supported(struct radeon_device *rdev)
{
	return ASIC_IS_DCE2(rdev) && !ASIC_IS_NODCE(rdev);
}

static struct r600_audio_pin r600_audio_status(struct radeon_device *rdev)
{
	struct r600_audio_pin status;
	uint32_t value;

	value = RREG32(R600_AUDIO_RATE_BPS_CHANNEL);

	/* number of channels */
	status.channels = (value & 0x7) + 1;

	/* bits per sample */
	switch ((value & 0xF0) >> 4) {
	case 0x0:
		status.bits_per_sample = 8;
		break;
	case 0x1:
		status.bits_per_sample = 16;
		break;
	case 0x2:
		status.bits_per_sample = 20;
		break;
	case 0x3:
		status.bits_per_sample = 24;
		break;
	case 0x4:
		status.bits_per_sample = 32;
		break;
	default:
		dev_err(rdev->dev, "Unknown bits per sample 0x%x, using 16\n",
			(int)value);
		status.bits_per_sample = 16;
	}

	/* current sampling rate in HZ */
	if (value & 0x4000)
		status.rate = 44100;
	else
		status.rate = 48000;
	status.rate *= ((value >> 11) & 0x7) + 1;
	status.rate /= ((value >> 8) & 0x7) + 1;

	value = RREG32(R600_AUDIO_STATUS_BITS);

	/* iec 60958 status bits */
	status.status_bits = value & 0xff;

	/* iec 60958 category code */
	status.category_code = (value >> 8) & 0xff;

	return status;
}

/*
 * update all hdmi interfaces with current audio parameters
 */
void r600_audio_update_hdmi(struct work_struct *work)
{
	struct radeon_device *rdev = container_of(work, struct radeon_device,
						  audio_work);
	struct drm_device *dev = rdev->ddev;
	struct r600_audio_pin audio_status = r600_audio_status(rdev);
	struct drm_encoder *encoder;
	bool changed = false;

	if (rdev->audio.pin[0].channels != audio_status.channels ||
	    rdev->audio.pin[0].rate != audio_status.rate ||
	    rdev->audio.pin[0].bits_per_sample != audio_status.bits_per_sample ||
	    rdev->audio.pin[0].status_bits != audio_status.status_bits ||
	    rdev->audio.pin[0].category_code != audio_status.category_code) {
		rdev->audio.pin[0] = audio_status;
		changed = true;
	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (!radeon_encoder_is_digital(encoder))
			continue;
		if (changed || r600_hdmi_buffer_status_changed(encoder))
			r600_hdmi_update_audio_settings(encoder);
	}
}

/* enable the audio stream */
void r600_audio_enable(struct radeon_device *rdev,
		       struct r600_audio_pin *pin,
		       u8 enable_mask)
{
	u32 tmp = RREG32(AZ_HOT_PLUG_CONTROL);

	if (!pin)
		return;

	if (enable_mask) {
		tmp |= AUDIO_ENABLED;
		if (enable_mask & 1)
			tmp |= PIN0_AUDIO_ENABLED;
		if (enable_mask & 2)
			tmp |= PIN1_AUDIO_ENABLED;
		if (enable_mask & 4)
			tmp |= PIN2_AUDIO_ENABLED;
		if (enable_mask & 8)
			tmp |= PIN3_AUDIO_ENABLED;
	} else {
		tmp &= ~(AUDIO_ENABLED |
			 PIN0_AUDIO_ENABLED |
			 PIN1_AUDIO_ENABLED |
			 PIN2_AUDIO_ENABLED |
			 PIN3_AUDIO_ENABLED);
	}

	WREG32(AZ_HOT_PLUG_CONTROL, tmp);
}

/*
 * initialize the audio vars
 */
int r600_audio_init(struct radeon_device *rdev)
{
	if (!radeon_audio || !r600_audio_chipset_supported(rdev))
		return 0;

	rdev->audio.enabled = true;

	rdev->audio.num_pins = 1;
	rdev->audio.pin[0].channels = -1;
	rdev->audio.pin[0].rate = -1;
	rdev->audio.pin[0].bits_per_sample = -1;
	rdev->audio.pin[0].status_bits = 0;
	rdev->audio.pin[0].category_code = 0;
	rdev->audio.pin[0].id = 0;
	/* disable audio.  it will be set up later */
	r600_audio_enable(rdev, &rdev->audio.pin[0], 0);

	return 0;
}

/*
 * release the audio timer
 * TODO: How to do this correctly on SMP systems?
 */
void r600_audio_fini(struct radeon_device *rdev)
{
	if (!rdev->audio.enabled)
		return;

	r600_audio_enable(rdev, &rdev->audio.pin[0], 0);

	rdev->audio.enabled = false;
}

struct r600_audio_pin *r600_audio_get_pin(struct radeon_device *rdev)
{
	/* only one pin on 6xx-NI */
	return &rdev->audio.pin[0];
}

/*
 * calculate CTS and N values if they are not found in the table
 */
static void r600_hdmi_calc_cts(uint32_t clock, int *CTS, int *N, int freq)
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
		printk(KERN_WARNING "Calculated ACR N value is too small. You may experience audio problems.\n");
	if (n > (128*freq/300))
		printk(KERN_WARNING "Calculated ACR N value is too large. You may experience audio problems.\n");

	*N = n;
	*CTS = cts;

	DRM_DEBUG("Calculated ACR timing N=%d CTS=%d for frequency %d\n",
		  *N, *CTS, freq);
}

struct radeon_hdmi_acr r600_hdmi_acr(uint32_t clock)
{
	struct radeon_hdmi_acr res;
	u8 i;

	/* Precalculated values for common clocks */
	for (i = 0; i < ARRAY_SIZE(r600_hdmi_predefined_acr); i++) {
		if (r600_hdmi_predefined_acr[i].clock == clock)
			return r600_hdmi_predefined_acr[i];
	}

	/* And odd clocks get manually calculated */
	r600_hdmi_calc_cts(clock, &res.cts_32khz, &res.n_32khz, 32000);
	r600_hdmi_calc_cts(clock, &res.cts_44_1khz, &res.n_44_1khz, 44100);
	r600_hdmi_calc_cts(clock, &res.cts_48khz, &res.n_48khz, 48000);

	return res;
}

/*
 * update the N and CTS parameters for a given pixel clock rate
 */
void r600_hdmi_update_ACR(struct drm_encoder *encoder, uint32_t clock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_hdmi_acr acr = r600_hdmi_acr(clock);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;

	WREG32_P(HDMI0_ACR_32_0 + offset,
		 HDMI0_ACR_CTS_32(acr.cts_32khz),
		 ~HDMI0_ACR_CTS_32_MASK);
	WREG32_P(HDMI0_ACR_32_1 + offset,
		 HDMI0_ACR_N_32(acr.n_32khz),
		 ~HDMI0_ACR_N_32_MASK);

	WREG32_P(HDMI0_ACR_44_0 + offset,
		 HDMI0_ACR_CTS_44(acr.cts_44_1khz),
		 ~HDMI0_ACR_CTS_44_MASK);
	WREG32_P(HDMI0_ACR_44_1 + offset,
		 HDMI0_ACR_N_44(acr.n_44_1khz),
		 ~HDMI0_ACR_N_44_MASK);

	WREG32_P(HDMI0_ACR_48_0 + offset,
		 HDMI0_ACR_CTS_48(acr.cts_48khz),
		 ~HDMI0_ACR_CTS_48_MASK);
	WREG32_P(HDMI0_ACR_48_1 + offset,
		 HDMI0_ACR_N_48(acr.n_48khz),
		 ~HDMI0_ACR_N_48_MASK);
}

/*
 * build a HDMI Video Info Frame
 */
void r600_hdmi_update_avi_infoframe(struct drm_encoder *encoder, void *buffer,
				    size_t size)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;
	uint8_t *frame = buffer + 3;
	uint8_t *header = buffer;

	WREG32(HDMI0_AVI_INFO0 + offset,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(HDMI0_AVI_INFO1 + offset,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x7] << 24));
	WREG32(HDMI0_AVI_INFO2 + offset,
		frame[0x8] | (frame[0x9] << 8) | (frame[0xA] << 16) | (frame[0xB] << 24));
	WREG32(HDMI0_AVI_INFO3 + offset,
		frame[0xC] | (frame[0xD] << 8) | (header[1] << 24));
}

/*
 * build a Audio Info Frame
 */
static void r600_hdmi_update_audio_infoframe(struct drm_encoder *encoder,
					     const void *buffer, size_t size)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;
	const u8 *frame = buffer + 3;

	WREG32(HDMI0_AUDIO_INFO0 + offset,
		frame[0x0] | (frame[0x1] << 8) | (frame[0x2] << 16) | (frame[0x3] << 24));
	WREG32(HDMI0_AUDIO_INFO1 + offset,
		frame[0x4] | (frame[0x5] << 8) | (frame[0x6] << 16) | (frame[0x8] << 24));
}

/*
 * test if audio buffer is filled enough to start playing
 */
static bool r600_hdmi_is_audio_buffer_filled(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;

	return (RREG32(HDMI0_STATUS + offset) & 0x10) != 0;
}

/*
 * have buffer status changed since last call?
 */
int r600_hdmi_buffer_status_changed(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	int status, result;

	if (!dig->afmt || !dig->afmt->enabled)
		return 0;

	status = r600_hdmi_is_audio_buffer_filled(encoder);
	result = dig->afmt->last_buffer_filled_status != status;
	dig->afmt->last_buffer_filled_status = status;

	return result;
}

/*
 * write the audio workaround status to the hardware
 */
void r600_hdmi_audio_workaround(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	uint32_t offset = dig->afmt->offset;
	bool hdmi_audio_workaround = false; /* FIXME */
	u32 value;

	if (!hdmi_audio_workaround ||
	    r600_hdmi_is_audio_buffer_filled(encoder))
		value = 0; /* disable workaround */
	else
		value = HDMI0_AUDIO_TEST_EN; /* enable workaround */
	WREG32_P(HDMI0_AUDIO_PACKET_CONTROL + offset,
		 value, ~HDMI0_AUDIO_TEST_EN);
}

void r600_audio_set_dto(struct drm_encoder *encoder, u32 clock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	u32 base_rate = 24000;
	u32 max_ratio = clock / base_rate;
	u32 dto_phase;
	u32 dto_modulo = clock;
	u32 wallclock_ratio;
	u32 dto_cntl;

	if (!dig || !dig->afmt)
		return;

	if (max_ratio >= 8) {
		dto_phase = 192 * 1000;
		wallclock_ratio = 3;
	} else if (max_ratio >= 4) {
		dto_phase = 96 * 1000;
		wallclock_ratio = 2;
	} else if (max_ratio >= 2) {
		dto_phase = 48 * 1000;
		wallclock_ratio = 1;
	} else {
		dto_phase = 24 * 1000;
		wallclock_ratio = 0;
	}

	/* there are two DTOs selected by DCCG_AUDIO_DTO_SELECT.
	 * doesn't matter which one you use.  Just use the first one.
	 */
	/* XXX two dtos; generally use dto0 for hdmi */
	/* Express [24MHz / target pixel clock] as an exact rational
	 * number (coefficient of two integer numbers.  DCCG_AUDIO_DTOx_PHASE
	 * is the numerator, DCCG_AUDIO_DTOx_MODULE is the denominator
	 */
	if (ASIC_IS_DCE32(rdev)) {
		if (dig->dig_encoder == 0) {
			dto_cntl = RREG32(DCCG_AUDIO_DTO0_CNTL) & ~DCCG_AUDIO_DTO_WALLCLOCK_RATIO_MASK;
			dto_cntl |= DCCG_AUDIO_DTO_WALLCLOCK_RATIO(wallclock_ratio);
			WREG32(DCCG_AUDIO_DTO0_CNTL, dto_cntl);
			WREG32(DCCG_AUDIO_DTO0_PHASE, dto_phase);
			WREG32(DCCG_AUDIO_DTO0_MODULE, dto_modulo);
			WREG32(DCCG_AUDIO_DTO_SELECT, 0); /* select DTO0 */
		} else {
			dto_cntl = RREG32(DCCG_AUDIO_DTO1_CNTL) & ~DCCG_AUDIO_DTO_WALLCLOCK_RATIO_MASK;
			dto_cntl |= DCCG_AUDIO_DTO_WALLCLOCK_RATIO(wallclock_ratio);
			WREG32(DCCG_AUDIO_DTO1_CNTL, dto_cntl);
			WREG32(DCCG_AUDIO_DTO1_PHASE, dto_phase);
			WREG32(DCCG_AUDIO_DTO1_MODULE, dto_modulo);
			WREG32(DCCG_AUDIO_DTO_SELECT, 1); /* select DTO1 */
		}
	} else {
		/* according to the reg specs, this should DCE3.2 only, but in
		 * practice it seems to cover DCE2.0/3.0/3.1 as well.
		 */
		if (dig->dig_encoder == 0) {
			WREG32(DCCG_AUDIO_DTO0_PHASE, base_rate * 100);
			WREG32(DCCG_AUDIO_DTO0_MODULE, clock * 100);
			WREG32(DCCG_AUDIO_DTO_SELECT, 0); /* select DTO0 */
		} else {
			WREG32(DCCG_AUDIO_DTO1_PHASE, base_rate * 100);
			WREG32(DCCG_AUDIO_DTO1_MODULE, clock * 100);
			WREG32(DCCG_AUDIO_DTO_SELECT, 1); /* select DTO1 */
		}
	}
}

/*
 * update the info frames with the data from the current display mode
 */
void r600_hdmi_setmode(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AVI_INFOFRAME_SIZE];
	struct hdmi_avi_infoframe frame;
	uint32_t offset;
	uint32_t acr_ctl;
	ssize_t err;

	if (!dig || !dig->afmt)
		return;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (!dig->afmt->enabled)
		return;
	offset = dig->afmt->offset;

	/* disable audio prior to setting up hw */
	dig->afmt->pin = r600_audio_get_pin(rdev);
	r600_audio_enable(rdev, dig->afmt->pin, 0xf);

	r600_audio_set_dto(encoder, mode->clock);

	WREG32_P(HDMI0_AUDIO_PACKET_CONTROL + offset,
		 HDMI0_AUDIO_SAMPLE_SEND | /* send audio packets */
		 HDMI0_AUDIO_DELAY_EN(1) | /* default audio delay */
		 HDMI0_AUDIO_PACKETS_PER_LINE(3) | /* should be suffient for all audio modes and small enough for all hblanks */
		 HDMI0_60958_CS_UPDATE, /* allow 60958 channel status fields to be updated */
		 ~(HDMI0_AUDIO_SAMPLE_SEND |
		   HDMI0_AUDIO_DELAY_EN_MASK |
		   HDMI0_AUDIO_PACKETS_PER_LINE_MASK |
		   HDMI0_60958_CS_UPDATE));

	/* DCE 3.0 uses register that's normally for CRC_CONTROL */
	acr_ctl = ASIC_IS_DCE3(rdev) ? DCE3_HDMI0_ACR_PACKET_CONTROL :
				       HDMI0_ACR_PACKET_CONTROL;
	WREG32_P(acr_ctl + offset,
		 HDMI0_ACR_SOURCE | /* select SW CTS value - XXX verify that hw CTS works on all families */
		 HDMI0_ACR_AUTO_SEND, /* allow hw to sent ACR packets when required */
		 ~(HDMI0_ACR_SOURCE |
		   HDMI0_ACR_AUTO_SEND));

	WREG32_OR(HDMI0_VBI_PACKET_CONTROL + offset,
		  HDMI0_NULL_SEND | /* send null packets when required */
		  HDMI0_GC_SEND | /* send general control packets */
		  HDMI0_GC_CONT); /* send general control packets every frame */

	WREG32_OR(HDMI0_INFOFRAME_CONTROL0 + offset,
		  HDMI0_AVI_INFO_SEND | /* enable AVI info frames */
		  HDMI0_AVI_INFO_CONT | /* send AVI info frames every frame/field */
		  HDMI0_AUDIO_INFO_SEND | /* enable audio info frames (frames won't be set until audio is enabled) */
		  HDMI0_AUDIO_INFO_UPDATE); /* required for audio info values to be updated */

	WREG32_P(HDMI0_INFOFRAME_CONTROL1 + offset,
		 HDMI0_AVI_INFO_LINE(2) | /* anything other than 0 */
		 HDMI0_AUDIO_INFO_LINE(2), /* anything other than 0 */
		 ~(HDMI0_AVI_INFO_LINE_MASK |
		   HDMI0_AUDIO_INFO_LINE_MASK));

	WREG32_AND(HDMI0_GC + offset,
		   ~HDMI0_GC_AVMUTE); /* unset HDMI0_GC_AVMUTE */

	err = drm_hdmi_avi_infoframe_from_display_mode(&frame, mode);
	if (err < 0) {
		DRM_ERROR("failed to setup AVI infoframe: %zd\n", err);
		return;
	}

	err = hdmi_avi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		DRM_ERROR("failed to pack AVI infoframe: %zd\n", err);
		return;
	}

	r600_hdmi_update_avi_infoframe(encoder, buffer, sizeof(buffer));

	/* fglrx duplicates INFOFRAME_CONTROL0 & INFOFRAME_CONTROL1 ops here */

	WREG32_AND(HDMI0_GENERIC_PACKET_CONTROL + offset,
		   ~(HDMI0_GENERIC0_SEND |
		     HDMI0_GENERIC0_CONT |
		     HDMI0_GENERIC0_UPDATE |
		     HDMI0_GENERIC1_SEND |
		     HDMI0_GENERIC1_CONT |
		     HDMI0_GENERIC0_LINE_MASK |
		     HDMI0_GENERIC1_LINE_MASK));

	r600_hdmi_update_ACR(encoder, mode->clock);

	WREG32_P(HDMI0_60958_0 + offset,
		 HDMI0_60958_CS_CHANNEL_NUMBER_L(1),
		 ~(HDMI0_60958_CS_CHANNEL_NUMBER_L_MASK |
		   HDMI0_60958_CS_CLOCK_ACCURACY_MASK));

	WREG32_P(HDMI0_60958_1 + offset,
		 HDMI0_60958_CS_CHANNEL_NUMBER_R(2),
		 ~HDMI0_60958_CS_CHANNEL_NUMBER_R_MASK);

	/* it's unknown what these bits do excatly, but it's indeed quite useful for debugging */
	WREG32(HDMI0_RAMP_CONTROL0 + offset, 0x00FFFFFF);
	WREG32(HDMI0_RAMP_CONTROL1 + offset, 0x007FFFFF);
	WREG32(HDMI0_RAMP_CONTROL2 + offset, 0x00000001);
	WREG32(HDMI0_RAMP_CONTROL3 + offset, 0x00000001);

	/* enable audio after to setting up hw */
	r600_audio_enable(rdev, dig->afmt->pin, 0xf);
}

/**
 * r600_hdmi_update_audio_settings - Update audio infoframe
 *
 * @encoder: drm encoder
 *
 * Gets info about current audio stream and updates audio infoframe.
 */
void r600_hdmi_update_audio_settings(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	struct r600_audio_pin audio = r600_audio_status(rdev);
	uint8_t buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AUDIO_INFOFRAME_SIZE];
	struct hdmi_audio_infoframe frame;
	uint32_t offset;
	uint32_t value;
	ssize_t err;

	if (!dig->afmt || !dig->afmt->enabled)
		return;
	offset = dig->afmt->offset;

	DRM_DEBUG("%s with %d channels, %d Hz sampling rate, %d bits per sample,\n",
		 r600_hdmi_is_audio_buffer_filled(encoder) ? "playing" : "stopped",
		  audio.channels, audio.rate, audio.bits_per_sample);
	DRM_DEBUG("0x%02X IEC60958 status bits and 0x%02X category code\n",
		  (int)audio.status_bits, (int)audio.category_code);

	err = hdmi_audio_infoframe_init(&frame);
	if (err < 0) {
		DRM_ERROR("failed to setup audio infoframe\n");
		return;
	}

	frame.channels = audio.channels;

	err = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		DRM_ERROR("failed to pack audio infoframe\n");
		return;
	}

	value = RREG32(HDMI0_AUDIO_PACKET_CONTROL + offset);
	if (value & HDMI0_AUDIO_TEST_EN)
		WREG32(HDMI0_AUDIO_PACKET_CONTROL + offset,
		       value & ~HDMI0_AUDIO_TEST_EN);

	WREG32_OR(HDMI0_CONTROL + offset,
		  HDMI0_ERROR_ACK);

	WREG32_AND(HDMI0_INFOFRAME_CONTROL0 + offset,
		   ~HDMI0_AUDIO_INFO_SOURCE);

	r600_hdmi_update_audio_infoframe(encoder, buffer, sizeof(buffer));

	WREG32_OR(HDMI0_INFOFRAME_CONTROL0 + offset,
		  HDMI0_AUDIO_INFO_CONT |
		  HDMI0_AUDIO_INFO_UPDATE);
}

/*
 * enable the HDMI engine
 */
void r600_hdmi_enable(struct drm_encoder *encoder, bool enable)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
	u32 hdmi = HDMI0_ERROR_ACK;

	if (!dig || !dig->afmt)
		return;

	/* Silent, r600_hdmi_enable will raise WARN for us */
	if (enable && dig->afmt->enabled)
		return;
	if (!enable && !dig->afmt->enabled)
		return;

	/* Older chipsets require setting HDMI and routing manually */
	if (!ASIC_IS_DCE3(rdev)) {
		if (enable)
			hdmi |= HDMI0_ENABLE;
		switch (radeon_encoder->encoder_id) {
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1:
			if (enable) {
				WREG32_OR(AVIVO_TMDSA_CNTL, AVIVO_TMDSA_CNTL_HDMI_EN);
				hdmi |= HDMI0_STREAM(HDMI0_STREAM_TMDSA);
			} else {
				WREG32_AND(AVIVO_TMDSA_CNTL, ~AVIVO_TMDSA_CNTL_HDMI_EN);
			}
			break;
		case ENCODER_OBJECT_ID_INTERNAL_LVTM1:
			if (enable) {
				WREG32_OR(AVIVO_LVTMA_CNTL, AVIVO_LVTMA_CNTL_HDMI_EN);
				hdmi |= HDMI0_STREAM(HDMI0_STREAM_LVTMA);
			} else {
				WREG32_AND(AVIVO_LVTMA_CNTL, ~AVIVO_LVTMA_CNTL_HDMI_EN);
			}
			break;
		case ENCODER_OBJECT_ID_INTERNAL_DDI:
			if (enable) {
				WREG32_OR(DDIA_CNTL, DDIA_HDMI_EN);
				hdmi |= HDMI0_STREAM(HDMI0_STREAM_DDIA);
			} else {
				WREG32_AND(DDIA_CNTL, ~DDIA_HDMI_EN);
			}
			break;
		case ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1:
			if (enable)
				hdmi |= HDMI0_STREAM(HDMI0_STREAM_DVOA);
			break;
		default:
			dev_err(rdev->dev, "Invalid encoder for HDMI: 0x%X\n",
				radeon_encoder->encoder_id);
			break;
		}
		WREG32(HDMI0_CONTROL + dig->afmt->offset, hdmi);
	}

	if (rdev->irq.installed) {
		/* if irq is available use it */
		/* XXX: shouldn't need this on any asics.  Double check DCE2/3 */
		if (enable)
			radeon_irq_kms_enable_afmt(rdev, dig->afmt->id);
		else
			radeon_irq_kms_disable_afmt(rdev, dig->afmt->id);
	}

	dig->afmt->enabled = enable;

	DRM_DEBUG("%sabling HDMI interface @ 0x%04X for encoder 0x%x\n",
		  enable ? "En" : "Dis", dig->afmt->offset, radeon_encoder->encoder_id);
}

