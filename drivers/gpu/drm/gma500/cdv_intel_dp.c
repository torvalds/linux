/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Packard <keithp@keithp.com>
 *
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_drm.h"
#include "psb_intel_reg.h"
#include "drm_dp_helper.h"


#define DP_LINK_STATUS_SIZE	6
#define DP_LINK_CHECK_TIMEOUT	(10 * 1000)

#define DP_LINK_CONFIGURATION_SIZE	9

#define CDV_FAST_LINK_TRAIN	1

struct psb_intel_dp {
	uint32_t output_reg;
	uint32_t DP;
	uint8_t  link_configuration[DP_LINK_CONFIGURATION_SIZE];
	bool has_audio;
	int force_audio;
	uint32_t color_range;
	uint8_t link_bw;
	uint8_t lane_count;
	uint8_t dpcd[4];
	struct psb_intel_output *output;
	struct i2c_adapter adapter;
	struct i2c_algo_dp_aux_data algo;
	uint8_t	train_set[4];
	uint8_t link_status[DP_LINK_STATUS_SIZE];
};

struct ddi_regoff {
	uint32_t	PreEmph1;
	uint32_t	PreEmph2;
	uint32_t	VSwing1;
	uint32_t	VSwing2;
	uint32_t	VSwing3;
	uint32_t	VSwing4;
	uint32_t	VSwing5;
};

static struct ddi_regoff ddi_DP_train_table[] = {
	{.PreEmph1 = 0x812c, .PreEmph2 = 0x8124, .VSwing1 = 0x8154,
	.VSwing2 = 0x8148, .VSwing3 = 0x814C, .VSwing4 = 0x8150,
	.VSwing5 = 0x8158,},
	{.PreEmph1 = 0x822c, .PreEmph2 = 0x8224, .VSwing1 = 0x8254,
	.VSwing2 = 0x8248, .VSwing3 = 0x824C, .VSwing4 = 0x8250,
	.VSwing5 = 0x8258,},
};

static uint32_t dp_vswing_premph_table[] = {
        0x55338954,	0x4000,
        0x554d8954,	0x2000,
        0x55668954,	0,
        0x559ac0d4,	0x6000,
};
/**
 * is_edp - is the given port attached to an eDP panel (either CPU or PCH)
 * @intel_dp: DP struct
 *
 * If a CPU or PCH DP output is attached to an eDP panel, this function
 * will return true, and false otherwise.
 */
static bool is_edp(struct psb_intel_output *output)
{
	return output->type == INTEL_OUTPUT_EDP;
}


static void psb_intel_dp_start_link_train(struct psb_intel_output *output);
static void psb_intel_dp_complete_link_train(struct psb_intel_output *output);
static void psb_intel_dp_link_down(struct psb_intel_output *output);

static int
psb_intel_dp_max_lane_count(struct psb_intel_output *output)
{
	struct psb_intel_dp *intel_dp = output->dev_priv;
	int max_lane_count = 4;

	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11) {
		max_lane_count = intel_dp->dpcd[DP_MAX_LANE_COUNT] & 0x1f;
		switch (max_lane_count) {
		case 1: case 2: case 4:
			break;
		default:
			max_lane_count = 4;
		}
	}
	return max_lane_count;
}

static int
psb_intel_dp_max_link_bw(struct psb_intel_output *output)
{
	struct psb_intel_dp *intel_dp = output->dev_priv;
	int max_link_bw = intel_dp->dpcd[DP_MAX_LINK_RATE];

	switch (max_link_bw) {
	case DP_LINK_BW_1_62:
	case DP_LINK_BW_2_7:
		break;
	default:
		max_link_bw = DP_LINK_BW_1_62;
		break;
	}
	return max_link_bw;
}

static int
psb_intel_dp_link_clock(uint8_t link_bw)
{
	if (link_bw == DP_LINK_BW_2_7)
		return 270000;
	else
		return 162000;
}

static int
psb_intel_dp_link_required(int pixel_clock, int bpp)
{
	return (pixel_clock * bpp + 7) / 8;
}

static int
psb_intel_dp_max_data_rate(int max_link_clock, int max_lanes)
{
	return (max_link_clock * max_lanes * 19) / 20;
}

static int
psb_intel_dp_mode_valid(struct drm_connector *connector,
		    struct drm_display_mode *mode)
{
	struct psb_intel_output *output = to_psb_intel_output(connector);
	struct drm_device *dev = connector->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	int max_link_clock = psb_intel_dp_link_clock(psb_intel_dp_max_link_bw(output));
	int max_lanes = psb_intel_dp_max_lane_count(output);

	if (is_edp(output) && dev_priv->panel_fixed_mode) {
		if (mode->hdisplay > dev_priv->panel_fixed_mode->hdisplay)
			return MODE_PANEL;

		if (mode->vdisplay > dev_priv->panel_fixed_mode->vdisplay)
			return MODE_PANEL;
	}

	/* only refuse the mode on non eDP since we have seen some weird eDP panels
	   which are outside spec tolerances but somehow work by magic */
	if (!is_edp(output) &&
	    (psb_intel_dp_link_required(mode->clock, 24)
	     > psb_intel_dp_max_data_rate(max_link_clock, max_lanes)))
		return MODE_CLOCK_HIGH;

	if (mode->clock < 10000)
		return MODE_CLOCK_LOW;

	return MODE_OK;
}

static uint32_t
pack_aux(uint8_t *src, int src_bytes)
{
	int	i;
	uint32_t v = 0;

	if (src_bytes > 4)
		src_bytes = 4;
	for (i = 0; i < src_bytes; i++)
		v |= ((uint32_t) src[i]) << ((3-i) * 8);
	return v;
}

static void
unpack_aux(uint32_t src, uint8_t *dst, int dst_bytes)
{
	int i;
	if (dst_bytes > 4)
		dst_bytes = 4;
	for (i = 0; i < dst_bytes; i++)
		dst[i] = src >> ((3-i) * 8);
}

static int
psb_intel_dp_aux_ch(struct psb_intel_output *output,
		uint8_t *send, int send_bytes,
		uint8_t *recv, int recv_size)
{
	struct psb_intel_dp *intel_dp = output->dev_priv;
	uint32_t output_reg = intel_dp->output_reg;
	struct drm_device *dev = output->base.dev;
	uint32_t ch_ctl = output_reg + 0x10;
	uint32_t ch_data = ch_ctl + 4;
	int i;
	int recv_bytes;
	uint32_t status;
	uint32_t aux_clock_divider;
	int try, precharge;

	/* The clock divider is based off the hrawclk,
	 * and would like to run at 2MHz. So, take the
	 * hrawclk value and divide by 2 and use that
	 * On CDV platform it uses 200MHz as hrawclk.
	 *
	 */
	aux_clock_divider = 200 / 2;

	precharge = 4;

	if (REG_READ(ch_ctl) & DP_AUX_CH_CTL_SEND_BUSY) {
		DRM_ERROR("dp_aux_ch not started status 0x%08x\n",
			  REG_READ(ch_ctl));
		return -EBUSY;
	}

	/* Must try at least 3 times according to DP spec */
	for (try = 0; try < 5; try++) {
		/* Load the send data into the aux channel data registers */
		for (i = 0; i < send_bytes; i += 4)
			REG_WRITE(ch_data + i,
				   pack_aux(send + i, send_bytes - i));
	
		/* Send the command and wait for it to complete */
		REG_WRITE(ch_ctl,
			   DP_AUX_CH_CTL_SEND_BUSY |
			   DP_AUX_CH_CTL_TIME_OUT_400us |
			   (send_bytes << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
			   (precharge << DP_AUX_CH_CTL_PRECHARGE_2US_SHIFT) |
			   (aux_clock_divider << DP_AUX_CH_CTL_BIT_CLOCK_2X_SHIFT) |
			   DP_AUX_CH_CTL_DONE |
			   DP_AUX_CH_CTL_TIME_OUT_ERROR |
			   DP_AUX_CH_CTL_RECEIVE_ERROR);
		for (;;) {
			status = REG_READ(ch_ctl);
			if ((status & DP_AUX_CH_CTL_SEND_BUSY) == 0)
				break;
			udelay(100);
		}
	
		/* Clear done status and any errors */
		REG_WRITE(ch_ctl,
			   status |
			   DP_AUX_CH_CTL_DONE |
			   DP_AUX_CH_CTL_TIME_OUT_ERROR |
			   DP_AUX_CH_CTL_RECEIVE_ERROR);
		if (status & DP_AUX_CH_CTL_DONE)
			break;
	}

	if ((status & DP_AUX_CH_CTL_DONE) == 0) {
		DRM_ERROR("dp_aux_ch not done status 0x%08x\n", status);
		return -EBUSY;
	}

	/* Check for timeout or receive error.
	 * Timeouts occur when the sink is not connected
	 */
	if (status & DP_AUX_CH_CTL_RECEIVE_ERROR) {
		DRM_ERROR("dp_aux_ch receive error status 0x%08x\n", status);
		return -EIO;
	}

	/* Timeouts occur when the device isn't connected, so they're
	 * "normal" -- don't fill the kernel log with these */
	if (status & DP_AUX_CH_CTL_TIME_OUT_ERROR) {
		DRM_DEBUG_KMS("dp_aux_ch timeout status 0x%08x\n", status);
		return -ETIMEDOUT;
	}

	/* Unload any bytes sent back from the other side */
	recv_bytes = ((status & DP_AUX_CH_CTL_MESSAGE_SIZE_MASK) >>
		      DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT);
	if (recv_bytes > recv_size)
		recv_bytes = recv_size;
	
	for (i = 0; i < recv_bytes; i += 4)
		unpack_aux(REG_READ(ch_data + i),
			   recv + i, recv_bytes - i);

	return recv_bytes;
}

/* Write data to the aux channel in native mode */
static int
psb_intel_dp_aux_native_write(struct psb_intel_output *output,
			  uint16_t address, uint8_t *send, int send_bytes)
{
	int ret;
	uint8_t	msg[20];
	int msg_bytes;
	uint8_t	ack;

	if (send_bytes > 16)
		return -1;
	msg[0] = AUX_NATIVE_WRITE << 4;
	msg[1] = address >> 8;
	msg[2] = address & 0xff;
	msg[3] = send_bytes - 1;
	memcpy(&msg[4], send, send_bytes);
	msg_bytes = send_bytes + 4;
	for (;;) {
		ret = psb_intel_dp_aux_ch(output, msg, msg_bytes, &ack, 1);
		if (ret < 0)
			return ret;
		if ((ack & AUX_NATIVE_REPLY_MASK) == AUX_NATIVE_REPLY_ACK)
			break;
		else if ((ack & AUX_NATIVE_REPLY_MASK) == AUX_NATIVE_REPLY_DEFER)
			udelay(100);
		else
			return -EIO;
	}
	return send_bytes;
}

/* Write a single byte to the aux channel in native mode */
static int
psb_intel_dp_aux_native_write_1(struct psb_intel_output *output,
			    uint16_t address, uint8_t byte)
{
	return psb_intel_dp_aux_native_write(output, address, &byte, 1);
}

/* read bytes from a native aux channel */
static int
psb_intel_dp_aux_native_read(struct psb_intel_output *output,
			 uint16_t address, uint8_t *recv, int recv_bytes)
{
	uint8_t msg[4];
	int msg_bytes;
	uint8_t reply[20];
	int reply_bytes;
	uint8_t ack;
	int ret;

	msg[0] = AUX_NATIVE_READ << 4;
	msg[1] = address >> 8;
	msg[2] = address & 0xff;
	msg[3] = recv_bytes - 1;

	msg_bytes = 4;
	reply_bytes = recv_bytes + 1;

	for (;;) {
		ret = psb_intel_dp_aux_ch(output, msg, msg_bytes,
				      reply, reply_bytes);
		if (ret == 0)
			return -EPROTO;
		if (ret < 0)
			return ret;
		ack = reply[0];
		if ((ack & AUX_NATIVE_REPLY_MASK) == AUX_NATIVE_REPLY_ACK) {
			memcpy(recv, reply + 1, ret - 1);
			return ret - 1;
		}
		else if ((ack & AUX_NATIVE_REPLY_MASK) == AUX_NATIVE_REPLY_DEFER)
			udelay(100);
		else
			return -EIO;
	}
}

static int
psb_intel_dp_i2c_aux_ch(struct i2c_adapter *adapter, int mode,
		    uint8_t write_byte, uint8_t *read_byte)
{
	struct i2c_algo_dp_aux_data *algo_data = adapter->algo_data;
	struct psb_intel_dp *intel_dp = container_of(adapter,
						struct psb_intel_dp,
						adapter);
	struct psb_intel_output *output = intel_dp->output;
	uint16_t address = algo_data->address;
	uint8_t msg[5];
	uint8_t reply[2];
	unsigned retry;
	int msg_bytes;
	int reply_bytes;
	int ret;

	/* Set up the command byte */
	if (mode & MODE_I2C_READ)
		msg[0] = AUX_I2C_READ << 4;
	else
		msg[0] = AUX_I2C_WRITE << 4;

	if (!(mode & MODE_I2C_STOP))
		msg[0] |= AUX_I2C_MOT << 4;

	msg[1] = address >> 8;
	msg[2] = address;

	switch (mode) {
	case MODE_I2C_WRITE:
		msg[3] = 0;
		msg[4] = write_byte;
		msg_bytes = 5;
		reply_bytes = 1;
		break;
	case MODE_I2C_READ:
		msg[3] = 0;
		msg_bytes = 4;
		reply_bytes = 2;
		break;
	default:
		msg_bytes = 3;
		reply_bytes = 1;
		break;
	}

	for (retry = 0; retry < 5; retry++) {
		ret = psb_intel_dp_aux_ch(output,
				      msg, msg_bytes,
				      reply, reply_bytes);
		if (ret < 0) {
			DRM_DEBUG_KMS("aux_ch failed %d\n", ret);
			return ret;
		}

		switch (reply[0] & AUX_NATIVE_REPLY_MASK) {
		case AUX_NATIVE_REPLY_ACK:
			/* I2C-over-AUX Reply field is only valid
			 * when paired with AUX ACK.
			 */
			break;
		case AUX_NATIVE_REPLY_NACK:
			DRM_DEBUG_KMS("aux_ch native nack\n");
			return -EREMOTEIO;
		case AUX_NATIVE_REPLY_DEFER:
			udelay(100);
			continue;
		default:
			DRM_ERROR("aux_ch invalid native reply 0x%02x\n",
				  reply[0]);
			return -EREMOTEIO;
		}

		switch (reply[0] & AUX_I2C_REPLY_MASK) {
		case AUX_I2C_REPLY_ACK:
			if (mode == MODE_I2C_READ) {
				*read_byte = reply[1];
			}
			return reply_bytes - 1;
		case AUX_I2C_REPLY_NACK:
			DRM_DEBUG_KMS("aux_i2c nack\n");
			return -EREMOTEIO;
		case AUX_I2C_REPLY_DEFER:
			DRM_DEBUG_KMS("aux_i2c defer\n");
			udelay(100);
			break;
		default:
			DRM_ERROR("aux_i2c invalid reply 0x%02x\n", reply[0]);
			return -EREMOTEIO;
		}
	}

	DRM_ERROR("too many retries, giving up\n");
	return -EREMOTEIO;
}

static int
psb_intel_dp_i2c_init(struct psb_intel_output *output, const char *name)
{
	struct psb_intel_dp *intel_dp = output->dev_priv;
	DRM_DEBUG_KMS("i2c_init %s\n", name);
	intel_dp->algo.running = false;
	intel_dp->algo.address = 0;
	intel_dp->algo.aux_ch = psb_intel_dp_i2c_aux_ch;

	memset(&intel_dp->adapter, '\0', sizeof (intel_dp->adapter));
	intel_dp->adapter.owner = THIS_MODULE;
	intel_dp->adapter.class = I2C_CLASS_DDC;
	strncpy (intel_dp->adapter.name, name, sizeof(intel_dp->adapter.name) - 1);
	intel_dp->adapter.name[sizeof(intel_dp->adapter.name) - 1] = '\0';
	intel_dp->adapter.algo_data = &intel_dp->algo;
	intel_dp->adapter.dev.parent = &output->base.kdev;

	return i2c_dp_aux_add_bus(&intel_dp->adapter);
}

static bool
psb_intel_dp_mode_fixup(struct drm_encoder *encoder, struct drm_display_mode *mode,
		    struct drm_display_mode *adjusted_mode)
{
	struct psb_intel_output *output = enc_to_psb_intel_output(encoder);
	struct psb_intel_dp *intel_dp = output->dev_priv;
	int lane_count, clock;
	int max_lane_count = psb_intel_dp_max_lane_count(output);
	int max_clock = psb_intel_dp_max_link_bw(output) == DP_LINK_BW_2_7 ? 1 : 0;
	static int bws[2] = { DP_LINK_BW_1_62, DP_LINK_BW_2_7 };


	for (lane_count = 1; lane_count <= max_lane_count; lane_count <<= 1) {
		for (clock = max_clock; clock >= 0; clock--) {
			int link_avail = psb_intel_dp_max_data_rate(psb_intel_dp_link_clock(bws[clock]), lane_count);

			if (psb_intel_dp_link_required(mode->clock, 24)
					<= link_avail) {
				intel_dp->link_bw = bws[clock];
				intel_dp->lane_count = lane_count;
				adjusted_mode->clock = psb_intel_dp_link_clock(intel_dp->link_bw);
				DRM_DEBUG_KMS("Display port link bw %02x lane "
						"count %d clock %d\n",
				       intel_dp->link_bw, intel_dp->lane_count,
				       adjusted_mode->clock);
				return true;
			}
		}
	}

	return false;
}

struct psb_intel_dp_m_n {
	uint32_t	tu;
	uint32_t	gmch_m;
	uint32_t	gmch_n;
	uint32_t	link_m;
	uint32_t	link_n;
};

static void
psb_intel_reduce_ratio(uint32_t *num, uint32_t *den)
{
	/*
	while (*num > 0xffffff || *den > 0xffffff) {
		*num >>= 1;
		*den >>= 1;
	}*/
	uint64_t value, m;
	m = *num;
	value = m * (0x800000);
	m = do_div(value, *den);
	*num = value;
	*den = 0x800000;
}

static void
psb_intel_dp_compute_m_n(int bpp,
		     int nlanes,
		     int pixel_clock,
		     int link_clock,
		     struct psb_intel_dp_m_n *m_n)
{
	m_n->tu = 64;
	m_n->gmch_m = (pixel_clock * bpp + 7) >> 3;
	m_n->gmch_n = link_clock * nlanes;
	psb_intel_reduce_ratio(&m_n->gmch_m, &m_n->gmch_n);
	m_n->link_m = pixel_clock;
	m_n->link_n = link_clock;
	psb_intel_reduce_ratio(&m_n->link_m, &m_n->link_n);
}

void
psb_intel_dp_set_m_n(struct drm_crtc *crtc, struct drm_display_mode *mode,
		 struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_encoder *encoder;
	struct psb_intel_crtc *intel_crtc = to_psb_intel_crtc(crtc);
	int lane_count = 4, bpp = 24;
	struct psb_intel_dp_m_n m_n;
	int pipe = intel_crtc->pipe;

	/*
	 * Find the lane count in the intel_encoder private
	 */
	list_for_each_entry(encoder, &mode_config->encoder_list, head) {
		struct psb_intel_output *intel_output;
		struct psb_intel_dp *intel_dp;

		if (encoder->crtc != crtc)
			continue;

		intel_output = enc_to_psb_intel_output(encoder);
		intel_dp = intel_output->dev_priv;
		if (intel_output->type == INTEL_OUTPUT_DISPLAYPORT) {
			lane_count = intel_dp->lane_count;
			break;
		} else if (is_edp(intel_output)) {
			lane_count = intel_dp->lane_count;
			break;
		}
	}

	/*
	 * Compute the GMCH and Link ratios. The '3' here is
	 * the number of bytes_per_pixel post-LUT, which we always
	 * set up for 8-bits of R/G/B, or 3 bytes total.
	 */
	psb_intel_dp_compute_m_n(bpp, lane_count,
			     mode->clock, adjusted_mode->clock, &m_n);

	{
		REG_WRITE(PIPE_GMCH_DATA_M(pipe),
			   ((m_n.tu - 1) << PIPE_GMCH_DATA_M_TU_SIZE_SHIFT) |
			   m_n.gmch_m);
		REG_WRITE(PIPE_GMCH_DATA_N(pipe), m_n.gmch_n);
		REG_WRITE(PIPE_DP_LINK_M(pipe), m_n.link_m);
		REG_WRITE(PIPE_DP_LINK_N(pipe), m_n.link_n);
	}
}

static void
psb_intel_dp_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode,
		  struct drm_display_mode *adjusted_mode)
{
	struct psb_intel_output *intel_output = enc_to_psb_intel_output(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	struct psb_intel_crtc *intel_crtc = to_psb_intel_crtc(crtc);
	struct psb_intel_dp *intel_dp = intel_output->dev_priv;


	intel_dp->DP = DP_VOLTAGE_0_4 | DP_PRE_EMPHASIS_0;
	intel_dp->DP |= intel_dp->color_range;

	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		intel_dp->DP |= DP_SYNC_HS_HIGH;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		intel_dp->DP |= DP_SYNC_VS_HIGH;

	intel_dp->DP |= DP_LINK_TRAIN_OFF;

	switch (intel_dp->lane_count) {
	case 1:
		intel_dp->DP |= DP_PORT_WIDTH_1;
		break;
	case 2:
		intel_dp->DP |= DP_PORT_WIDTH_2;
		break;
	case 4:
		intel_dp->DP |= DP_PORT_WIDTH_4;
		break;
	}
	if (intel_dp->has_audio)
		intel_dp->DP |= DP_AUDIO_OUTPUT_ENABLE;

	memset(intel_dp->link_configuration, 0, DP_LINK_CONFIGURATION_SIZE);
	intel_dp->link_configuration[0] = intel_dp->link_bw;
	intel_dp->link_configuration[1] = intel_dp->lane_count;

	/*
	 * Check for DPCD version > 1.1 and enhanced framing support
	 */
	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11 &&
	    (intel_dp->dpcd[DP_MAX_LANE_COUNT] & DP_ENHANCED_FRAME_CAP)) {
		intel_dp->link_configuration[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
		intel_dp->DP |= DP_ENHANCED_FRAMING;
	}

	/* CPT DP's pipe select is decided in TRANS_DP_CTL */
	if (intel_crtc->pipe == 1)
		intel_dp->DP |= DP_PIPEB_SELECT;

	DRM_DEBUG_KMS("DP expected reg is %x\n", intel_dp->DP);
}


/* If the sink supports it, try to set the power state appropriately */
static void psb_intel_dp_sink_dpms(struct psb_intel_output *output, int mode)
{
	struct psb_intel_dp *intel_dp = output->dev_priv;
	int ret, i;

	/* Should have a valid DPCD by this point */
	if (intel_dp->dpcd[DP_DPCD_REV] < 0x11)
		return;

	if (mode != DRM_MODE_DPMS_ON) {
		ret = psb_intel_dp_aux_native_write_1(output, DP_SET_POWER,
						  DP_SET_POWER_D3);
		if (ret != 1)
			DRM_DEBUG_DRIVER("failed to write sink power state\n");
	} else {
		/*
		 * When turning on, we need to retry for 1ms to give the sink
		 * time to wake up.
		 */
		for (i = 0; i < 3; i++) {
			ret = psb_intel_dp_aux_native_write_1(output,
							  DP_SET_POWER,
							  DP_SET_POWER_D0);
			if (ret == 1)
				break;
			udelay(1000);
		}
	}
}

static void psb_intel_dp_prepare(struct drm_encoder *encoder)
{
	struct psb_intel_output *output = enc_to_psb_intel_output(encoder);

	/* Wake up the sink first */
	psb_intel_dp_sink_dpms(output, DRM_MODE_DPMS_ON);

	psb_intel_dp_link_down(output);
}

static void psb_intel_dp_commit(struct drm_encoder *encoder)
{
	struct psb_intel_output *output = enc_to_psb_intel_output(encoder);

	psb_intel_dp_start_link_train(output);

	psb_intel_dp_complete_link_train(output);

}

static void
psb_intel_dp_dpms(struct drm_encoder *encoder, int mode)
{
	struct psb_intel_output *intel_output = enc_to_psb_intel_output(encoder);
	struct psb_intel_dp *intel_dp = intel_output->dev_priv;
	struct drm_device *dev = encoder->dev;
	uint32_t dp_reg = REG_READ(intel_dp->output_reg);

	if (mode != DRM_MODE_DPMS_ON) {
		psb_intel_dp_sink_dpms(intel_output, mode);
		psb_intel_dp_link_down(intel_output);
	} else {
		psb_intel_dp_sink_dpms(intel_output, mode);
		if (!(dp_reg & DP_PORT_EN)) {
			psb_intel_dp_start_link_train(intel_output);
			psb_intel_dp_complete_link_train(intel_output);
		}
	}
}

/*
 * Native read with retry for link status and receiver capability reads for
 * cases where the sink may still be asleep.
 */
static bool
psb_intel_dp_aux_native_read_retry(struct psb_intel_output *output, uint16_t address,
			       uint8_t *recv, int recv_bytes)
{
	int ret, i;

	/*
	 * Sinks are *supposed* to come up within 1ms from an off state,
	 * but we're also supposed to retry 3 times per the spec.
	 */
	for (i = 0; i < 3; i++) {
		ret = psb_intel_dp_aux_native_read(output, address, recv,
					       recv_bytes);
		if (ret == recv_bytes)
			return true;
		udelay(1000);
	}

	return false;
}

/*
 * Fetch AUX CH registers 0x202 - 0x207 which contain
 * link status information
 */
static bool
psb_intel_dp_get_link_status(struct psb_intel_output *output)
{
	struct psb_intel_dp *intel_dp = output->dev_priv;
	return psb_intel_dp_aux_native_read_retry(output,
					      DP_LANE0_1_STATUS,
					      intel_dp->link_status,
					      DP_LINK_STATUS_SIZE);
}

static uint8_t
psb_intel_dp_link_status(uint8_t link_status[DP_LINK_STATUS_SIZE],
		     int r)
{
	return link_status[r - DP_LANE0_1_STATUS];
}

static uint8_t
psb_intel_get_adjust_request_voltage(uint8_t link_status[DP_LINK_STATUS_SIZE],
				 int lane)
{
	int	    i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int	    s = ((lane & 1) ?
			 DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT :
			 DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT);
	uint8_t l = psb_intel_dp_link_status(link_status, i);

	return ((l >> s) & 3) << DP_TRAIN_VOLTAGE_SWING_SHIFT;
}

static uint8_t
psb_intel_get_adjust_request_pre_emphasis(uint8_t link_status[DP_LINK_STATUS_SIZE],
				      int lane)
{
	int	    i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int	    s = ((lane & 1) ?
			 DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT :
			 DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT);
	uint8_t l = psb_intel_dp_link_status(link_status, i);

	return ((l >> s) & 3) << DP_TRAIN_PRE_EMPHASIS_SHIFT;
}


#if 0
static char	*voltage_names[] = {
	"0.4V", "0.6V", "0.8V", "1.2V"
};
static char	*pre_emph_names[] = {
	"0dB", "3.5dB", "6dB", "9.5dB"
};
static char	*link_train_names[] = {
	"pattern 1", "pattern 2", "idle", "off"
};
#endif

#define CDV_DP_VOLTAGE_MAX	    DP_TRAIN_VOLTAGE_SWING_1200
/*
static uint8_t
psb_intel_dp_pre_emphasis_max(uint8_t voltage_swing)
{
	switch (voltage_swing & DP_TRAIN_VOLTAGE_SWING_MASK) {
	case DP_TRAIN_VOLTAGE_SWING_400:
		return DP_TRAIN_PRE_EMPHASIS_6;
	case DP_TRAIN_VOLTAGE_SWING_600:
		return DP_TRAIN_PRE_EMPHASIS_6;
	case DP_TRAIN_VOLTAGE_SWING_800:
		return DP_TRAIN_PRE_EMPHASIS_3_5;
	case DP_TRAIN_VOLTAGE_SWING_1200:
	default:
		return DP_TRAIN_PRE_EMPHASIS_0;
	}
}
*/
static void
psb_intel_get_adjust_train(struct psb_intel_output *output)
{
	struct psb_intel_dp *intel_dp = output->dev_priv;
	uint8_t v = 0;
	uint8_t p = 0;
	int lane;

	for (lane = 0; lane < intel_dp->lane_count; lane++) {
		uint8_t this_v = psb_intel_get_adjust_request_voltage(intel_dp->link_status, lane);
		uint8_t this_p = psb_intel_get_adjust_request_pre_emphasis(intel_dp->link_status, lane);

		if (this_v > v)
			v = this_v;
		if (this_p > p)
			p = this_p;
	}
	
	if (v >= CDV_DP_VOLTAGE_MAX)
		v = CDV_DP_VOLTAGE_MAX | DP_TRAIN_MAX_SWING_REACHED;

	if (p == DP_TRAIN_PRE_EMPHASIS_MASK)
		p |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;
		
	for (lane = 0; lane < 4; lane++)
		intel_dp->train_set[lane] = v | p;
}


static uint8_t
psb_intel_get_lane_status(uint8_t link_status[DP_LINK_STATUS_SIZE],
		      int lane)
{
	int i = DP_LANE0_1_STATUS + (lane >> 1);
	int s = (lane & 1) * 4;
	uint8_t l = psb_intel_dp_link_status(link_status, i);

	return (l >> s) & 0xf;
}

/* Check for clock recovery is done on all channels */
static bool
psb_intel_clock_recovery_ok(uint8_t link_status[DP_LINK_STATUS_SIZE], int lane_count)
{
	int lane;
	uint8_t lane_status;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = psb_intel_get_lane_status(link_status, lane);
		if ((lane_status & DP_LANE_CR_DONE) == 0)
			return false;
	}
	return true;
}

/* Check to see if channel eq is done on all channels */
#define CHANNEL_EQ_BITS (DP_LANE_CR_DONE|\
			 DP_LANE_CHANNEL_EQ_DONE|\
			 DP_LANE_SYMBOL_LOCKED)
static bool
psb_intel_channel_eq_ok(struct psb_intel_output *output)
{
	struct psb_intel_dp *intel_dp = output->dev_priv;
	uint8_t lane_align;
	uint8_t lane_status;
	int lane;

	lane_align = psb_intel_dp_link_status(intel_dp->link_status,
					  DP_LANE_ALIGN_STATUS_UPDATED);
	if ((lane_align & DP_INTERLANE_ALIGN_DONE) == 0)
		return false;
	for (lane = 0; lane < intel_dp->lane_count; lane++) {
		lane_status = psb_intel_get_lane_status(intel_dp->link_status, lane);
		if ((lane_status & CHANNEL_EQ_BITS) != CHANNEL_EQ_BITS)
			return false;
	}
	return true;
}

static bool
psb_intel_dp_set_link_train(struct psb_intel_output *output,
			uint32_t dp_reg_value,
			uint8_t dp_train_pat)
{
	
	struct drm_device *dev = output->base.dev;
	int ret;
	struct psb_intel_dp *intel_dp = output->dev_priv;

	REG_WRITE(intel_dp->output_reg, dp_reg_value);
	REG_READ(intel_dp->output_reg);

	ret = psb_intel_dp_aux_native_write_1(output,
				    DP_TRAINING_PATTERN_SET,
				    dp_train_pat);

	if (ret != 1) {
		DRM_DEBUG_KMS("Failure in setting link pattern %x\n",
				dp_train_pat);
		return false;
	}

	return true;
}


static bool
psb_intel_dplink_set_level(struct psb_intel_output *output,
			uint8_t dp_train_pat)
{
	
	int ret;
	struct psb_intel_dp *intel_dp = output->dev_priv;

	ret = psb_intel_dp_aux_native_write(output,
					DP_TRAINING_LANE0_SET,
					intel_dp->train_set,
					intel_dp->lane_count);

	if (ret != intel_dp->lane_count) {
		DRM_DEBUG_KMS("Failure in setting level %d, lane_cnt= %d\n",
				intel_dp->train_set[0], intel_dp->lane_count);
		return false;
	}
	return true;
}

static void
psb_intel_dp_set_vswing_premph(struct psb_intel_output *output, uint8_t signal_level)
{
	struct drm_device *dev = output->base.dev;
	struct psb_intel_dp *intel_dp = output->dev_priv;
	struct ddi_regoff *ddi_reg;
	int vswing, premph, index;

	if (intel_dp->output_reg == DP_B)
		ddi_reg = &ddi_DP_train_table[0];
	else
		ddi_reg = &ddi_DP_train_table[1];

	vswing = (signal_level & DP_TRAIN_VOLTAGE_SWING_MASK);
	premph = ((signal_level & DP_TRAIN_PRE_EMPHASIS_MASK)) >>
				DP_TRAIN_PRE_EMPHASIS_SHIFT;

	if (vswing + premph > 3)
		return;
#ifdef CDV_FAST_LINK_TRAIN
	return;
#endif
	DRM_DEBUG_KMS("Test2\n");
	//return ;
	psb_sb_reset(dev);
	/* ;Swing voltage programming
        ;gfx_dpio_set_reg(0xc058, 0x0505313A) */
	psb_sb_write(dev, ddi_reg->VSwing5, 0x0505313A);

	/* ;gfx_dpio_set_reg(0x8154, 0x43406055) */
	psb_sb_write(dev, ddi_reg->VSwing1, 0x43406055);

	/* ;gfx_dpio_set_reg(0x8148, 0x55338954)
	 * The VSwing_PreEmph table is also considered based on the vswing/premp
	 */
	index = (vswing + premph) * 2;
	if (premph == 1 && vswing == 1) {
		psb_sb_write(dev, ddi_reg->VSwing2, 0x055738954);
	} else
		psb_sb_write(dev, ddi_reg->VSwing2, dp_vswing_premph_table[index]);

	/* ;gfx_dpio_set_reg(0x814c, 0x40802040) */
	if ((vswing + premph) == DP_TRAIN_VOLTAGE_SWING_1200)
		psb_sb_write(dev, ddi_reg->VSwing3, 0x70802040);
	else
		psb_sb_write(dev, ddi_reg->VSwing3, 0x40802040);

	/* ;gfx_dpio_set_reg(0x8150, 0x2b405555) */
	//psb_sb_write(dev, ddi_reg->VSwing4, 0x2b405555);

	/* ;gfx_dpio_set_reg(0x8154, 0xc3406055) */
	psb_sb_write(dev, ddi_reg->VSwing1, 0xc3406055);

	/* ;Pre emphasis programming
	 * ;gfx_dpio_set_reg(0xc02c, 0x1f030040)
	 */
	psb_sb_write(dev, ddi_reg->PreEmph1, 0x1f030040);

	/* ;gfx_dpio_set_reg(0x8124, 0x00004000) */
	index = 2 * premph + 1;
	psb_sb_write(dev, ddi_reg->PreEmph2, dp_vswing_premph_table[index]);
	return;	
}


/* Enable corresponding port and start training pattern 1 */
static void
psb_intel_dp_start_link_train(struct psb_intel_output *output)
{
	struct drm_device *dev = output->base.dev;
	struct psb_intel_dp *intel_dp = output->dev_priv;
	int i;
	uint8_t voltage;
	bool clock_recovery = false;
	int tries;
	u32 reg;
	uint32_t DP = intel_dp->DP;

	DP |= DP_PORT_EN;
		DP &= ~DP_LINK_TRAIN_MASK;
		
		reg = DP;	
		reg |= DP_LINK_TRAIN_PAT_1;
	/* Enable output, wait for it to become active */
	REG_WRITE(intel_dp->output_reg, reg);
	REG_READ(intel_dp->output_reg);
	psb_intel_wait_for_vblank(dev);

	DRM_DEBUG_KMS("Link config\n");
	/* Write the link configuration data */
	psb_intel_dp_aux_native_write(output, DP_LINK_BW_SET,
				  intel_dp->link_configuration,
				  2);

	memset(intel_dp->train_set, 0, 4);
	voltage = 0;
	tries = 0;
	clock_recovery = false;

	DRM_DEBUG_KMS("Start train\n");
		reg = DP | DP_LINK_TRAIN_PAT_1;


	for (;;) {
		/* Use intel_dp->train_set[0] to set the voltage and pre emphasis values */

		if (!psb_intel_dp_set_link_train(output, reg, DP_TRAINING_PATTERN_1)) {
			DRM_DEBUG_KMS("Failure in aux-transfer setting pattern 1\n");
		}
		psb_intel_dp_set_vswing_premph(output, intel_dp->train_set[0]);
		/* Set training pattern 1 */

		psb_intel_dplink_set_level(output, DP_TRAINING_PATTERN_1);

		udelay(200);
		if (!psb_intel_dp_get_link_status(output))
			break;

		if (psb_intel_clock_recovery_ok(intel_dp->link_status, intel_dp->lane_count)) {
			DRM_DEBUG_KMS("PT1 train is done\n");
			clock_recovery = true;
			break;
		}

		/* Check to see if we've tried the max voltage */
		for (i = 0; i < intel_dp->lane_count; i++)
			if ((intel_dp->train_set[i] & DP_TRAIN_MAX_SWING_REACHED) == 0)
				break;
		if (i == intel_dp->lane_count)
			break;

		/* Check to see if we've tried the same voltage 5 times */
		if ((intel_dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK) == voltage) {
			++tries;
			if (tries == 5)
				break;
		} else
			tries = 0;
		voltage = intel_dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK;

		/* Compute new intel_dp->train_set as requested by target */
		psb_intel_get_adjust_train(output);

	}

	if (!clock_recovery) {
		DRM_DEBUG_KMS("failure in DP patter 1 training, train set %x\n", intel_dp->train_set[0]);
	}
	
	intel_dp->DP = DP;
}

static void
psb_intel_dp_complete_link_train(struct psb_intel_output *output)
{
	struct drm_device *dev = output->base.dev;
	struct psb_intel_dp *intel_dp = output->dev_priv;
	bool channel_eq = false;
	int tries, cr_tries;
	u32 reg;
	uint32_t DP = intel_dp->DP;

	/* channel equalization */
	tries = 0;
	cr_tries = 0;
	channel_eq = false;

	DRM_DEBUG_KMS("\n");
		reg = DP | DP_LINK_TRAIN_PAT_2;

	for (;;) {
		/* channel eq pattern */
		if (!psb_intel_dp_set_link_train(output, reg,
					     DP_TRAINING_PATTERN_2)) {
			DRM_DEBUG_KMS("Failure in aux-transfer setting pattern 2\n");
		}
		/* Use intel_dp->train_set[0] to set the voltage and pre emphasis values */

		if (cr_tries > 5) {
			DRM_ERROR("failed to train DP, aborting\n");
			psb_intel_dp_link_down(output);
			break;
		}

		psb_intel_dp_set_vswing_premph(output, intel_dp->train_set[0]);

		psb_intel_dplink_set_level(output, DP_TRAINING_PATTERN_2);

		udelay(1000);
		if (!psb_intel_dp_get_link_status(output))
			break;

		/* Make sure clock is still ok */
		if (!psb_intel_clock_recovery_ok(intel_dp->link_status, intel_dp->lane_count)) {
			psb_intel_dp_start_link_train(output);
			cr_tries++;
			continue;
		}

		if (psb_intel_channel_eq_ok(output)) {
			DRM_DEBUG_KMS("PT2 train is done\n");
			channel_eq = true;
			break;
		}

		/* Try 5 times, then try clock recovery if that fails */
		if (tries > 5) {
			psb_intel_dp_link_down(output);
			psb_intel_dp_start_link_train(output);
			tries = 0;
			cr_tries++;
			continue;
		}

		/* Compute new intel_dp->train_set as requested by target */
		psb_intel_get_adjust_train(output);
		++tries;

	}

	reg = DP | DP_LINK_TRAIN_OFF;

	REG_WRITE(intel_dp->output_reg, reg);
	REG_READ(intel_dp->output_reg);
	psb_intel_dp_aux_native_write_1(output,
				    DP_TRAINING_PATTERN_SET, DP_TRAINING_PATTERN_DISABLE);
}

static void
psb_intel_dp_link_down(struct psb_intel_output *output)
{
	struct drm_device *dev = output->base.dev;
	struct psb_intel_dp *intel_dp = output->dev_priv;
	uint32_t DP = intel_dp->DP;

	if ((REG_READ(intel_dp->output_reg) & DP_PORT_EN) == 0)
		return;

	DRM_DEBUG_KMS("\n");


	{
		DP &= ~DP_LINK_TRAIN_MASK;
		REG_WRITE(intel_dp->output_reg, DP | DP_LINK_TRAIN_PAT_IDLE);
	}
	REG_READ(intel_dp->output_reg);

	msleep(17);

	REG_WRITE(intel_dp->output_reg, DP & ~DP_PORT_EN);
	REG_READ(intel_dp->output_reg);
}

static enum drm_connector_status
cdv_dp_detect(struct psb_intel_output *output)
{
	struct psb_intel_dp *intel_dp = output->dev_priv;
	enum drm_connector_status status;

	status = connector_status_disconnected;
	if (psb_intel_dp_aux_native_read(output, 0x000, intel_dp->dpcd,
				     sizeof (intel_dp->dpcd)) == sizeof (intel_dp->dpcd))
	{
		if (intel_dp->dpcd[DP_DPCD_REV] != 0)
			status = connector_status_connected;
	}
	if (status == connector_status_connected)
		DRM_DEBUG_KMS("DPCD: Rev=%x LN_Rate=%x LN_CNT=%x LN_DOWNSP=%x\n",
			intel_dp->dpcd[0], intel_dp->dpcd[1],
			intel_dp->dpcd[2], intel_dp->dpcd[3]);
	return status;
}

/**
 * Uses CRT_HOTPLUG_EN and CRT_HOTPLUG_STAT to detect DP connection.
 *
 * \return true if DP port is connected.
 * \return false if DP port is disconnected.
 */
static enum drm_connector_status
psb_intel_dp_detect(struct drm_connector *connector, bool force)
{
	struct psb_intel_output *output = to_psb_intel_output(connector);
	struct psb_intel_dp *intel_dp = output->dev_priv;
	enum drm_connector_status status;
	struct edid *edid = NULL;

	intel_dp->has_audio = false;

	status = cdv_dp_detect(output);
	if (status != connector_status_connected)
		return status;

	if (intel_dp->force_audio) {
		intel_dp->has_audio = intel_dp->force_audio > 0;
	} else {
		edid = drm_get_edid(connector, &intel_dp->adapter);
		if (edid) {
			intel_dp->has_audio = drm_detect_monitor_audio(edid);
			connector->display_info.raw_edid = NULL;
			kfree(edid);
		}
	}

	return connector_status_connected;
}

static int psb_intel_dp_get_modes(struct drm_connector *connector)
{
	struct psb_intel_output *intel_output = to_psb_intel_output(connector);
	struct psb_intel_dp *intel_dp = intel_output->dev_priv;
	struct edid *edid = NULL;
	int ret = 0;


	edid = drm_get_edid(&intel_output->base,
			 	&intel_dp->adapter);
	if (edid) {
		drm_mode_connector_update_edid_property(&intel_output->
							base, edid);
		ret = drm_add_edid_modes(&intel_output->base, edid);
		kfree(edid);
	}

	return ret;
}

static bool
psb_intel_dp_detect_audio(struct drm_connector *connector)
{
	struct psb_intel_output *output = to_psb_intel_output(connector);
	struct psb_intel_dp *intel_dp = output->dev_priv;
	struct edid *edid;
	bool has_audio = false;

	edid = drm_get_edid(connector, &intel_dp->adapter);
	if (edid) {
		has_audio = drm_detect_monitor_audio(edid);

		connector->display_info.raw_edid = NULL;
		kfree(edid);
	}

	return has_audio;
}

static int
psb_intel_dp_set_property(struct drm_connector *connector,
		      struct drm_property *property,
		      uint64_t val)
{
	struct drm_psb_private *dev_priv = connector->dev->dev_private;
	struct psb_intel_output *output = to_psb_intel_output(connector);
	struct psb_intel_dp *intel_dp = output->dev_priv;
	int ret;

	ret = drm_connector_property_set_value(connector, property, val);
	if (ret)
		return ret;

	if (property == dev_priv->force_audio_property) {
		int i = val;
		bool has_audio;

		if (i == intel_dp->force_audio)
			return 0;

		intel_dp->force_audio = i;

		if (i == 0)
			has_audio = psb_intel_dp_detect_audio(connector);
		else
			has_audio = i > 0;

		if (has_audio == intel_dp->has_audio)
			return 0;

		intel_dp->has_audio = has_audio;
		goto done;
	}

	if (property == dev_priv->broadcast_rgb_property) {
		if (val == !!intel_dp->color_range)
			return 0;

		intel_dp->color_range = val ? DP_COLOR_RANGE_16_235 : 0;
		goto done;
	}

	return -EINVAL;

done:
	if (output->enc.crtc) {
		struct drm_crtc *crtc = output->enc.crtc;
		drm_crtc_helper_set_mode(crtc, &crtc->mode,
					 crtc->x, crtc->y,
					 crtc->fb);
	}

	return 0;
}

static void
psb_intel_dp_destroy (struct drm_connector *connector)
{
	struct psb_intel_output *output = to_psb_intel_output(connector);
	struct psb_intel_dp *intel_dp = output->dev_priv;

	i2c_del_adapter(&intel_dp->adapter);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static void psb_intel_dp_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_helper_funcs psb_intel_dp_helper_funcs = {
	.dpms = psb_intel_dp_dpms,
	.mode_fixup = psb_intel_dp_mode_fixup,
	.prepare = psb_intel_dp_prepare,
	.mode_set = psb_intel_dp_mode_set,
	.commit = psb_intel_dp_commit,
};

static const struct drm_connector_funcs psb_intel_dp_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = psb_intel_dp_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = psb_intel_dp_set_property,
	.destroy = psb_intel_dp_destroy,
};

static const struct drm_connector_helper_funcs psb_intel_dp_connector_helper_funcs = {
	.get_modes = psb_intel_dp_get_modes,
	.mode_valid = psb_intel_dp_mode_valid,
	.best_encoder = psb_intel_best_encoder,
};

static const struct drm_encoder_funcs psb_intel_dp_enc_funcs = {
	.destroy = psb_intel_dp_encoder_destroy,
};


static void
psb_intel_dp_add_properties(struct psb_intel_output *output, struct drm_connector *connector)
{
	psb_intel_attach_force_audio_property(connector);
	psb_intel_attach_broadcast_rgb_property(connector);
}

void
psb_intel_dp_init(struct drm_device *dev, struct psb_intel_mode_device *mode_dev, int output_reg)
{
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct psb_intel_output *psb_intel_output;
	struct psb_intel_dp *intel_dp;
	const char *name = NULL;
	int type;

	psb_intel_output = kzalloc(sizeof(struct psb_intel_output) +
			       sizeof(struct psb_intel_dp), GFP_KERNEL);
	if (!psb_intel_output)
		return;

	intel_dp = (struct psb_intel_dp *)(psb_intel_output + 1);
	psb_intel_output->mode_dev = mode_dev;
	connector = &psb_intel_output->base;
	encoder = &psb_intel_output->enc;
	psb_intel_output->dev_priv=intel_dp;
	intel_dp->output = psb_intel_output;

	intel_dp->output_reg = output_reg;
	
	type = DRM_MODE_CONNECTOR_DisplayPort;
	psb_intel_output->type = INTEL_OUTPUT_DISPLAYPORT;

	drm_connector_init(dev, connector, &psb_intel_dp_connector_funcs, type);
	drm_connector_helper_add(connector, &psb_intel_dp_connector_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	drm_encoder_init(dev, encoder, &psb_intel_dp_enc_funcs,
			 DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &psb_intel_dp_helper_funcs);

	drm_mode_connector_attach_encoder(&psb_intel_output->base,
					  &psb_intel_output->enc);

	drm_sysfs_connector_add(connector);

	/* Set up the DDC bus. */
	switch (output_reg) {
		case DP_B:
			name = "DPDDC-B";
			psb_intel_output->ddi_select = (DP_MASK | DDI0_SELECT);
			break;
		case DP_C:
			name = "DPDDC-C";
			psb_intel_output->ddi_select = (DP_MASK | DDI1_SELECT);
			break;
	}

	psb_intel_dp_i2c_init(psb_intel_output, name);
	psb_intel_dp_add_properties(psb_intel_output, connector);

}
