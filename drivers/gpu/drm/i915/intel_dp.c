/*
 * Copyright © 2008 Intel Corporation
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
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "intel_dp.h"

#define DP_LINK_STATUS_SIZE	6
#define DP_LINK_CHECK_TIMEOUT	(10 * 1000)

#define DP_LINK_CONFIGURATION_SIZE	9

#define IS_eDP(i) ((i)->type == INTEL_OUTPUT_EDP)

struct intel_dp_priv {
	uint32_t output_reg;
	uint32_t DP;
	uint8_t  link_configuration[DP_LINK_CONFIGURATION_SIZE];
	uint32_t save_DP;
	uint8_t  save_link_configuration[DP_LINK_CONFIGURATION_SIZE];
	bool has_audio;
	int dpms_mode;
	uint8_t link_bw;
	uint8_t lane_count;
	uint8_t dpcd[4];
	struct intel_output *intel_output;
	struct i2c_adapter adapter;
	struct i2c_algo_dp_aux_data algo;
};

static void
intel_dp_link_train(struct intel_output *intel_output, uint32_t DP,
		    uint8_t link_configuration[DP_LINK_CONFIGURATION_SIZE]);

static void
intel_dp_link_down(struct intel_output *intel_output, uint32_t DP);

void
intel_edp_link_config (struct intel_output *intel_output,
		int *lane_num, int *link_bw)
{
	struct intel_dp_priv   *dp_priv = intel_output->dev_priv;

	*lane_num = dp_priv->lane_count;
	if (dp_priv->link_bw == DP_LINK_BW_1_62)
		*link_bw = 162000;
	else if (dp_priv->link_bw == DP_LINK_BW_2_7)
		*link_bw = 270000;
}

static int
intel_dp_max_lane_count(struct intel_output *intel_output)
{
	struct intel_dp_priv   *dp_priv = intel_output->dev_priv;
	int max_lane_count = 4;

	if (dp_priv->dpcd[0] >= 0x11) {
		max_lane_count = dp_priv->dpcd[2] & 0x1f;
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
intel_dp_max_link_bw(struct intel_output *intel_output)
{
	struct intel_dp_priv   *dp_priv = intel_output->dev_priv;
	int max_link_bw = dp_priv->dpcd[1];

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
intel_dp_link_clock(uint8_t link_bw)
{
	if (link_bw == DP_LINK_BW_2_7)
		return 270000;
	else
		return 162000;
}

/* I think this is a fiction */
static int
intel_dp_link_required(int pixel_clock)
{
	return pixel_clock * 3;
}

static int
intel_dp_mode_valid(struct drm_connector *connector,
		    struct drm_display_mode *mode)
{
	struct intel_output *intel_output = to_intel_output(connector);
	int max_link_clock = intel_dp_link_clock(intel_dp_max_link_bw(intel_output));
	int max_lanes = intel_dp_max_lane_count(intel_output);

	if (intel_dp_link_required(mode->clock) > max_link_clock * max_lanes)
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

/* hrawclock is 1/4 the FSB frequency */
static int
intel_hrawclk(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t clkcfg;

	clkcfg = I915_READ(CLKCFG);
	switch (clkcfg & CLKCFG_FSB_MASK) {
	case CLKCFG_FSB_400:
		return 100;
	case CLKCFG_FSB_533:
		return 133;
	case CLKCFG_FSB_667:
		return 166;
	case CLKCFG_FSB_800:
		return 200;
	case CLKCFG_FSB_1067:
		return 266;
	case CLKCFG_FSB_1333:
		return 333;
	/* these two are just a guess; one of them might be right */
	case CLKCFG_FSB_1600:
	case CLKCFG_FSB_1600_ALT:
		return 400;
	default:
		return 133;
	}
}

static int
intel_dp_aux_ch(struct intel_output *intel_output,
		uint8_t *send, int send_bytes,
		uint8_t *recv, int recv_size)
{
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;
	uint32_t output_reg = dp_priv->output_reg;
	struct drm_device *dev = intel_output->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t ch_ctl = output_reg + 0x10;
	uint32_t ch_data = ch_ctl + 4;
	int i;
	int recv_bytes;
	uint32_t ctl;
	uint32_t status;
	uint32_t aux_clock_divider;
	int try;

	/* The clock divider is based off the hrawclk,
	 * and would like to run at 2MHz. So, take the
	 * hrawclk value and divide by 2 and use that
	 */
	if (IS_eDP(intel_output))
		aux_clock_divider = 225; /* eDP input clock at 450Mhz */
	else if (IS_IGDNG(dev))
		aux_clock_divider = 62; /* IGDNG: input clock fixed at 125Mhz */
	else
		aux_clock_divider = intel_hrawclk(dev) / 2;

	/* Must try at least 3 times according to DP spec */
	for (try = 0; try < 5; try++) {
		/* Load the send data into the aux channel data registers */
		for (i = 0; i < send_bytes; i += 4) {
			uint32_t    d = pack_aux(send + i, send_bytes - i);
	
			I915_WRITE(ch_data + i, d);
		}
	
		ctl = (DP_AUX_CH_CTL_SEND_BUSY |
		       DP_AUX_CH_CTL_TIME_OUT_400us |
		       (send_bytes << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
		       (5 << DP_AUX_CH_CTL_PRECHARGE_2US_SHIFT) |
		       (aux_clock_divider << DP_AUX_CH_CTL_BIT_CLOCK_2X_SHIFT) |
		       DP_AUX_CH_CTL_DONE |
		       DP_AUX_CH_CTL_TIME_OUT_ERROR |
		       DP_AUX_CH_CTL_RECEIVE_ERROR);
	
		/* Send the command and wait for it to complete */
		I915_WRITE(ch_ctl, ctl);
		(void) I915_READ(ch_ctl);
		for (;;) {
			udelay(100);
			status = I915_READ(ch_ctl);
			if ((status & DP_AUX_CH_CTL_SEND_BUSY) == 0)
				break;
		}
	
		/* Clear done status and any errors */
		I915_WRITE(ch_ctl, (status |
				DP_AUX_CH_CTL_DONE |
				DP_AUX_CH_CTL_TIME_OUT_ERROR |
				DP_AUX_CH_CTL_RECEIVE_ERROR));
		(void) I915_READ(ch_ctl);
		if ((status & DP_AUX_CH_CTL_TIME_OUT_ERROR) == 0)
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
	
	for (i = 0; i < recv_bytes; i += 4) {
		uint32_t    d = I915_READ(ch_data + i);

		unpack_aux(d, recv + i, recv_bytes - i);
	}

	return recv_bytes;
}

/* Write data to the aux channel in native mode */
static int
intel_dp_aux_native_write(struct intel_output *intel_output,
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
		ret = intel_dp_aux_ch(intel_output, msg, msg_bytes, &ack, 1);
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
intel_dp_aux_native_write_1(struct intel_output *intel_output,
			    uint16_t address, uint8_t byte)
{
	return intel_dp_aux_native_write(intel_output, address, &byte, 1);
}

/* read bytes from a native aux channel */
static int
intel_dp_aux_native_read(struct intel_output *intel_output,
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
		ret = intel_dp_aux_ch(intel_output, msg, msg_bytes,
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
intel_dp_i2c_aux_ch(struct i2c_adapter *adapter,
		    uint8_t *send, int send_bytes,
		    uint8_t *recv, int recv_bytes)
{
	struct intel_dp_priv *dp_priv = container_of(adapter,
						     struct intel_dp_priv,
						     adapter);
	struct intel_output *intel_output = dp_priv->intel_output;

	return intel_dp_aux_ch(intel_output,
			       send, send_bytes, recv, recv_bytes);
}

static int
intel_dp_i2c_init(struct intel_output *intel_output, const char *name)
{
	struct intel_dp_priv   *dp_priv = intel_output->dev_priv;

	DRM_DEBUG_KMS("i2c_init %s\n", name);
	dp_priv->algo.running = false;
	dp_priv->algo.address = 0;
	dp_priv->algo.aux_ch = intel_dp_i2c_aux_ch;

	memset(&dp_priv->adapter, '\0', sizeof (dp_priv->adapter));
	dp_priv->adapter.owner = THIS_MODULE;
	dp_priv->adapter.class = I2C_CLASS_DDC;
	strncpy (dp_priv->adapter.name, name, sizeof(dp_priv->adapter.name) - 1);
	dp_priv->adapter.name[sizeof(dp_priv->adapter.name) - 1] = '\0';
	dp_priv->adapter.algo_data = &dp_priv->algo;
	dp_priv->adapter.dev.parent = &intel_output->base.kdev;
	
	return i2c_dp_aux_add_bus(&dp_priv->adapter);
}

static bool
intel_dp_mode_fixup(struct drm_encoder *encoder, struct drm_display_mode *mode,
		    struct drm_display_mode *adjusted_mode)
{
	struct intel_output *intel_output = enc_to_intel_output(encoder);
	struct intel_dp_priv   *dp_priv = intel_output->dev_priv;
	int lane_count, clock;
	int max_lane_count = intel_dp_max_lane_count(intel_output);
	int max_clock = intel_dp_max_link_bw(intel_output) == DP_LINK_BW_2_7 ? 1 : 0;
	static int bws[2] = { DP_LINK_BW_1_62, DP_LINK_BW_2_7 };

	for (lane_count = 1; lane_count <= max_lane_count; lane_count <<= 1) {
		for (clock = 0; clock <= max_clock; clock++) {
			int link_avail = intel_dp_link_clock(bws[clock]) * lane_count;

			if (intel_dp_link_required(mode->clock) <= link_avail) {
				dp_priv->link_bw = bws[clock];
				dp_priv->lane_count = lane_count;
				adjusted_mode->clock = intel_dp_link_clock(dp_priv->link_bw);
				DRM_DEBUG_KMS("Display port link bw %02x lane "
						"count %d clock %d\n",
				       dp_priv->link_bw, dp_priv->lane_count,
				       adjusted_mode->clock);
				return true;
			}
		}
	}
	return false;
}

struct intel_dp_m_n {
	uint32_t	tu;
	uint32_t	gmch_m;
	uint32_t	gmch_n;
	uint32_t	link_m;
	uint32_t	link_n;
};

static void
intel_reduce_ratio(uint32_t *num, uint32_t *den)
{
	while (*num > 0xffffff || *den > 0xffffff) {
		*num >>= 1;
		*den >>= 1;
	}
}

static void
intel_dp_compute_m_n(int bytes_per_pixel,
		     int nlanes,
		     int pixel_clock,
		     int link_clock,
		     struct intel_dp_m_n *m_n)
{
	m_n->tu = 64;
	m_n->gmch_m = pixel_clock * bytes_per_pixel;
	m_n->gmch_n = link_clock * nlanes;
	intel_reduce_ratio(&m_n->gmch_m, &m_n->gmch_n);
	m_n->link_m = pixel_clock;
	m_n->link_n = link_clock;
	intel_reduce_ratio(&m_n->link_m, &m_n->link_n);
}

void
intel_dp_set_m_n(struct drm_crtc *crtc, struct drm_display_mode *mode,
		 struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int lane_count = 4;
	struct intel_dp_m_n m_n;

	/*
	 * Find the lane count in the intel_output private
	 */
	list_for_each_entry(connector, &mode_config->connector_list, head) {
		struct intel_output *intel_output = to_intel_output(connector);
		struct intel_dp_priv *dp_priv = intel_output->dev_priv;

		if (!connector->encoder || connector->encoder->crtc != crtc)
			continue;

		if (intel_output->type == INTEL_OUTPUT_DISPLAYPORT) {
			lane_count = dp_priv->lane_count;
			break;
		}
	}

	/*
	 * Compute the GMCH and Link ratios. The '3' here is
	 * the number of bytes_per_pixel post-LUT, which we always
	 * set up for 8-bits of R/G/B, or 3 bytes total.
	 */
	intel_dp_compute_m_n(3, lane_count,
			     mode->clock, adjusted_mode->clock, &m_n);

	if (IS_IGDNG(dev)) {
		if (intel_crtc->pipe == 0) {
			I915_WRITE(TRANSA_DATA_M1,
				   ((m_n.tu - 1) << PIPE_GMCH_DATA_M_TU_SIZE_SHIFT) |
				   m_n.gmch_m);
			I915_WRITE(TRANSA_DATA_N1, m_n.gmch_n);
			I915_WRITE(TRANSA_DP_LINK_M1, m_n.link_m);
			I915_WRITE(TRANSA_DP_LINK_N1, m_n.link_n);
		} else {
			I915_WRITE(TRANSB_DATA_M1,
				   ((m_n.tu - 1) << PIPE_GMCH_DATA_M_TU_SIZE_SHIFT) |
				   m_n.gmch_m);
			I915_WRITE(TRANSB_DATA_N1, m_n.gmch_n);
			I915_WRITE(TRANSB_DP_LINK_M1, m_n.link_m);
			I915_WRITE(TRANSB_DP_LINK_N1, m_n.link_n);
		}
	} else {
		if (intel_crtc->pipe == 0) {
			I915_WRITE(PIPEA_GMCH_DATA_M,
				   ((m_n.tu - 1) << PIPE_GMCH_DATA_M_TU_SIZE_SHIFT) |
				   m_n.gmch_m);
			I915_WRITE(PIPEA_GMCH_DATA_N,
				   m_n.gmch_n);
			I915_WRITE(PIPEA_DP_LINK_M, m_n.link_m);
			I915_WRITE(PIPEA_DP_LINK_N, m_n.link_n);
		} else {
			I915_WRITE(PIPEB_GMCH_DATA_M,
				   ((m_n.tu - 1) << PIPE_GMCH_DATA_M_TU_SIZE_SHIFT) |
				   m_n.gmch_m);
			I915_WRITE(PIPEB_GMCH_DATA_N,
					m_n.gmch_n);
			I915_WRITE(PIPEB_DP_LINK_M, m_n.link_m);
			I915_WRITE(PIPEB_DP_LINK_N, m_n.link_n);
		}
	}
}

static void
intel_dp_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode,
		  struct drm_display_mode *adjusted_mode)
{
	struct intel_output *intel_output = enc_to_intel_output(encoder);
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;
	struct drm_crtc *crtc = intel_output->enc.crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	dp_priv->DP = (DP_LINK_TRAIN_OFF |
			DP_VOLTAGE_0_4 |
			DP_PRE_EMPHASIS_0 |
			DP_SYNC_VS_HIGH |
			DP_SYNC_HS_HIGH);

	switch (dp_priv->lane_count) {
	case 1:
		dp_priv->DP |= DP_PORT_WIDTH_1;
		break;
	case 2:
		dp_priv->DP |= DP_PORT_WIDTH_2;
		break;
	case 4:
		dp_priv->DP |= DP_PORT_WIDTH_4;
		break;
	}
	if (dp_priv->has_audio)
		dp_priv->DP |= DP_AUDIO_OUTPUT_ENABLE;

	memset(dp_priv->link_configuration, 0, DP_LINK_CONFIGURATION_SIZE);
	dp_priv->link_configuration[0] = dp_priv->link_bw;
	dp_priv->link_configuration[1] = dp_priv->lane_count;

	/*
	 * Check for DPCD version > 1.1,
	 * enable enahanced frame stuff in that case
	 */
	if (dp_priv->dpcd[0] >= 0x11) {
		dp_priv->link_configuration[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
		dp_priv->DP |= DP_ENHANCED_FRAMING;
	}

	if (intel_crtc->pipe == 1)
		dp_priv->DP |= DP_PIPEB_SELECT;

	if (IS_eDP(intel_output)) {
		/* don't miss out required setting for eDP */
		dp_priv->DP |= DP_PLL_ENABLE;
		if (adjusted_mode->clock < 200000)
			dp_priv->DP |= DP_PLL_FREQ_160MHZ;
		else
			dp_priv->DP |= DP_PLL_FREQ_270MHZ;
	}
}

static void igdng_edp_backlight_on (struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp;

	DRM_DEBUG_KMS("\n");
	pp = I915_READ(PCH_PP_CONTROL);
	pp |= EDP_BLC_ENABLE;
	I915_WRITE(PCH_PP_CONTROL, pp);
}

static void igdng_edp_backlight_off (struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp;

	DRM_DEBUG_KMS("\n");
	pp = I915_READ(PCH_PP_CONTROL);
	pp &= ~EDP_BLC_ENABLE;
	I915_WRITE(PCH_PP_CONTROL, pp);
}

static void
intel_dp_dpms(struct drm_encoder *encoder, int mode)
{
	struct intel_output *intel_output = enc_to_intel_output(encoder);
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;
	struct drm_device *dev = intel_output->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dp_reg = I915_READ(dp_priv->output_reg);

	if (mode != DRM_MODE_DPMS_ON) {
		if (dp_reg & DP_PORT_EN) {
			intel_dp_link_down(intel_output, dp_priv->DP);
			if (IS_eDP(intel_output))
				igdng_edp_backlight_off(dev);
		}
	} else {
		if (!(dp_reg & DP_PORT_EN)) {
			intel_dp_link_train(intel_output, dp_priv->DP, dp_priv->link_configuration);
			if (IS_eDP(intel_output))
				igdng_edp_backlight_on(dev);
		}
	}
	dp_priv->dpms_mode = mode;
}

/*
 * Fetch AUX CH registers 0x202 - 0x207 which contain
 * link status information
 */
static bool
intel_dp_get_link_status(struct intel_output *intel_output,
			 uint8_t link_status[DP_LINK_STATUS_SIZE])
{
	int ret;

	ret = intel_dp_aux_native_read(intel_output,
				       DP_LANE0_1_STATUS,
				       link_status, DP_LINK_STATUS_SIZE);
	if (ret != DP_LINK_STATUS_SIZE)
		return false;
	return true;
}

static uint8_t
intel_dp_link_status(uint8_t link_status[DP_LINK_STATUS_SIZE],
		     int r)
{
	return link_status[r - DP_LANE0_1_STATUS];
}

static void
intel_dp_save(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);
	struct drm_device *dev = intel_output->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;

	dp_priv->save_DP = I915_READ(dp_priv->output_reg);
	intel_dp_aux_native_read(intel_output, DP_LINK_BW_SET,
				 dp_priv->save_link_configuration,
				 sizeof (dp_priv->save_link_configuration));
}

static uint8_t
intel_get_adjust_request_voltage(uint8_t link_status[DP_LINK_STATUS_SIZE],
				 int lane)
{
	int	    i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int	    s = ((lane & 1) ?
			 DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT :
			 DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT);
	uint8_t l = intel_dp_link_status(link_status, i);

	return ((l >> s) & 3) << DP_TRAIN_VOLTAGE_SWING_SHIFT;
}

static uint8_t
intel_get_adjust_request_pre_emphasis(uint8_t link_status[DP_LINK_STATUS_SIZE],
				      int lane)
{
	int	    i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int	    s = ((lane & 1) ?
			 DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT :
			 DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT);
	uint8_t l = intel_dp_link_status(link_status, i);

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

/*
 * These are source-specific values; current Intel hardware supports
 * a maximum voltage of 800mV and a maximum pre-emphasis of 6dB
 */
#define I830_DP_VOLTAGE_MAX	    DP_TRAIN_VOLTAGE_SWING_800

static uint8_t
intel_dp_pre_emphasis_max(uint8_t voltage_swing)
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

static void
intel_get_adjust_train(struct intel_output *intel_output,
		       uint8_t link_status[DP_LINK_STATUS_SIZE],
		       int lane_count,
		       uint8_t train_set[4])
{
	uint8_t v = 0;
	uint8_t p = 0;
	int lane;

	for (lane = 0; lane < lane_count; lane++) {
		uint8_t this_v = intel_get_adjust_request_voltage(link_status, lane);
		uint8_t this_p = intel_get_adjust_request_pre_emphasis(link_status, lane);

		if (this_v > v)
			v = this_v;
		if (this_p > p)
			p = this_p;
	}

	if (v >= I830_DP_VOLTAGE_MAX)
		v = I830_DP_VOLTAGE_MAX | DP_TRAIN_MAX_SWING_REACHED;

	if (p >= intel_dp_pre_emphasis_max(v))
		p = intel_dp_pre_emphasis_max(v) | DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

	for (lane = 0; lane < 4; lane++)
		train_set[lane] = v | p;
}

static uint32_t
intel_dp_signal_levels(uint8_t train_set, int lane_count)
{
	uint32_t	signal_levels = 0;

	switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
	case DP_TRAIN_VOLTAGE_SWING_400:
	default:
		signal_levels |= DP_VOLTAGE_0_4;
		break;
	case DP_TRAIN_VOLTAGE_SWING_600:
		signal_levels |= DP_VOLTAGE_0_6;
		break;
	case DP_TRAIN_VOLTAGE_SWING_800:
		signal_levels |= DP_VOLTAGE_0_8;
		break;
	case DP_TRAIN_VOLTAGE_SWING_1200:
		signal_levels |= DP_VOLTAGE_1_2;
		break;
	}
	switch (train_set & DP_TRAIN_PRE_EMPHASIS_MASK) {
	case DP_TRAIN_PRE_EMPHASIS_0:
	default:
		signal_levels |= DP_PRE_EMPHASIS_0;
		break;
	case DP_TRAIN_PRE_EMPHASIS_3_5:
		signal_levels |= DP_PRE_EMPHASIS_3_5;
		break;
	case DP_TRAIN_PRE_EMPHASIS_6:
		signal_levels |= DP_PRE_EMPHASIS_6;
		break;
	case DP_TRAIN_PRE_EMPHASIS_9_5:
		signal_levels |= DP_PRE_EMPHASIS_9_5;
		break;
	}
	return signal_levels;
}

static uint8_t
intel_get_lane_status(uint8_t link_status[DP_LINK_STATUS_SIZE],
		      int lane)
{
	int i = DP_LANE0_1_STATUS + (lane >> 1);
	int s = (lane & 1) * 4;
	uint8_t l = intel_dp_link_status(link_status, i);

	return (l >> s) & 0xf;
}

/* Check for clock recovery is done on all channels */
static bool
intel_clock_recovery_ok(uint8_t link_status[DP_LINK_STATUS_SIZE], int lane_count)
{
	int lane;
	uint8_t lane_status;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = intel_get_lane_status(link_status, lane);
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
intel_channel_eq_ok(uint8_t link_status[DP_LINK_STATUS_SIZE], int lane_count)
{
	uint8_t lane_align;
	uint8_t lane_status;
	int lane;

	lane_align = intel_dp_link_status(link_status,
					  DP_LANE_ALIGN_STATUS_UPDATED);
	if ((lane_align & DP_INTERLANE_ALIGN_DONE) == 0)
		return false;
	for (lane = 0; lane < lane_count; lane++) {
		lane_status = intel_get_lane_status(link_status, lane);
		if ((lane_status & CHANNEL_EQ_BITS) != CHANNEL_EQ_BITS)
			return false;
	}
	return true;
}

static bool
intel_dp_set_link_train(struct intel_output *intel_output,
			uint32_t dp_reg_value,
			uint8_t dp_train_pat,
			uint8_t train_set[4],
			bool first)
{
	struct drm_device *dev = intel_output->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;
	int ret;

	I915_WRITE(dp_priv->output_reg, dp_reg_value);
	POSTING_READ(dp_priv->output_reg);
	if (first)
		intel_wait_for_vblank(dev);

	intel_dp_aux_native_write_1(intel_output,
				    DP_TRAINING_PATTERN_SET,
				    dp_train_pat);

	ret = intel_dp_aux_native_write(intel_output,
					DP_TRAINING_LANE0_SET, train_set, 4);
	if (ret != 4)
		return false;

	return true;
}

static void
intel_dp_link_train(struct intel_output *intel_output, uint32_t DP,
		    uint8_t link_configuration[DP_LINK_CONFIGURATION_SIZE])
{
	struct drm_device *dev = intel_output->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;
	uint8_t	train_set[4];
	uint8_t link_status[DP_LINK_STATUS_SIZE];
	int i;
	uint8_t voltage;
	bool clock_recovery = false;
	bool channel_eq = false;
	bool first = true;
	int tries;

	/* Write the link configuration data */
	intel_dp_aux_native_write(intel_output, 0x100,
				  link_configuration, DP_LINK_CONFIGURATION_SIZE);

	DP |= DP_PORT_EN;
	DP &= ~DP_LINK_TRAIN_MASK;
	memset(train_set, 0, 4);
	voltage = 0xff;
	tries = 0;
	clock_recovery = false;
	for (;;) {
		/* Use train_set[0] to set the voltage and pre emphasis values */
		uint32_t    signal_levels = intel_dp_signal_levels(train_set[0], dp_priv->lane_count);
		DP = (DP & ~(DP_VOLTAGE_MASK|DP_PRE_EMPHASIS_MASK)) | signal_levels;

		if (!intel_dp_set_link_train(intel_output, DP | DP_LINK_TRAIN_PAT_1,
					     DP_TRAINING_PATTERN_1, train_set, first))
			break;
		first = false;
		/* Set training pattern 1 */

		udelay(100);
		if (!intel_dp_get_link_status(intel_output, link_status))
			break;

		if (intel_clock_recovery_ok(link_status, dp_priv->lane_count)) {
			clock_recovery = true;
			break;
		}

		/* Check to see if we've tried the max voltage */
		for (i = 0; i < dp_priv->lane_count; i++)
			if ((train_set[i] & DP_TRAIN_MAX_SWING_REACHED) == 0)
				break;
		if (i == dp_priv->lane_count)
			break;

		/* Check to see if we've tried the same voltage 5 times */
		if ((train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK) == voltage) {
			++tries;
			if (tries == 5)
				break;
		} else
			tries = 0;
		voltage = train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK;

		/* Compute new train_set as requested by target */
		intel_get_adjust_train(intel_output, link_status, dp_priv->lane_count, train_set);
	}

	/* channel equalization */
	tries = 0;
	channel_eq = false;
	for (;;) {
		/* Use train_set[0] to set the voltage and pre emphasis values */
		uint32_t    signal_levels = intel_dp_signal_levels(train_set[0], dp_priv->lane_count);
		DP = (DP & ~(DP_VOLTAGE_MASK|DP_PRE_EMPHASIS_MASK)) | signal_levels;

		/* channel eq pattern */
		if (!intel_dp_set_link_train(intel_output, DP | DP_LINK_TRAIN_PAT_2,
					     DP_TRAINING_PATTERN_2, train_set,
					     false))
			break;

		udelay(400);
		if (!intel_dp_get_link_status(intel_output, link_status))
			break;

		if (intel_channel_eq_ok(link_status, dp_priv->lane_count)) {
			channel_eq = true;
			break;
		}

		/* Try 5 times */
		if (tries > 5)
			break;

		/* Compute new train_set as requested by target */
		intel_get_adjust_train(intel_output, link_status, dp_priv->lane_count, train_set);
		++tries;
	}

	I915_WRITE(dp_priv->output_reg, DP | DP_LINK_TRAIN_OFF);
	POSTING_READ(dp_priv->output_reg);
	intel_dp_aux_native_write_1(intel_output,
				    DP_TRAINING_PATTERN_SET, DP_TRAINING_PATTERN_DISABLE);
}

static void
intel_dp_link_down(struct intel_output *intel_output, uint32_t DP)
{
	struct drm_device *dev = intel_output->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;

	DRM_DEBUG_KMS("\n");

	if (IS_eDP(intel_output)) {
		DP &= ~DP_PLL_ENABLE;
		I915_WRITE(dp_priv->output_reg, DP);
		POSTING_READ(dp_priv->output_reg);
		udelay(100);
	}

	DP &= ~DP_LINK_TRAIN_MASK;
	I915_WRITE(dp_priv->output_reg, DP | DP_LINK_TRAIN_PAT_IDLE);
	POSTING_READ(dp_priv->output_reg);

	udelay(17000);

	if (IS_eDP(intel_output))
		DP |= DP_LINK_TRAIN_OFF;
	I915_WRITE(dp_priv->output_reg, DP & ~DP_PORT_EN);
	POSTING_READ(dp_priv->output_reg);
}

static void
intel_dp_restore(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;

	if (dp_priv->save_DP & DP_PORT_EN)
		intel_dp_link_train(intel_output, dp_priv->save_DP, dp_priv->save_link_configuration);
	else
		intel_dp_link_down(intel_output,  dp_priv->save_DP);
}

/*
 * According to DP spec
 * 5.1.2:
 *  1. Read DPCD
 *  2. Configure link according to Receiver Capabilities
 *  3. Use Link Training from 2.5.3.3 and 3.5.1.3
 *  4. Check link status on receipt of hot-plug interrupt
 */

static void
intel_dp_check_link_status(struct intel_output *intel_output)
{
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;
	uint8_t link_status[DP_LINK_STATUS_SIZE];

	if (!intel_output->enc.crtc)
		return;

	if (!intel_dp_get_link_status(intel_output, link_status)) {
		intel_dp_link_down(intel_output, dp_priv->DP);
		return;
	}

	if (!intel_channel_eq_ok(link_status, dp_priv->lane_count))
		intel_dp_link_train(intel_output, dp_priv->DP, dp_priv->link_configuration);
}

static enum drm_connector_status
igdng_dp_detect(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;
	enum drm_connector_status status;

	status = connector_status_disconnected;
	if (intel_dp_aux_native_read(intel_output,
				     0x000, dp_priv->dpcd,
				     sizeof (dp_priv->dpcd)) == sizeof (dp_priv->dpcd))
	{
		if (dp_priv->dpcd[0] != 0)
			status = connector_status_connected;
	}
	return status;
}

/**
 * Uses CRT_HOTPLUG_EN and CRT_HOTPLUG_STAT to detect DP connection.
 *
 * \return true if DP port is connected.
 * \return false if DP port is disconnected.
 */
static enum drm_connector_status
intel_dp_detect(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);
	struct drm_device *dev = intel_output->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;
	uint32_t temp, bit;
	enum drm_connector_status status;

	dp_priv->has_audio = false;

	if (IS_IGDNG(dev))
		return igdng_dp_detect(connector);

	temp = I915_READ(PORT_HOTPLUG_EN);

	I915_WRITE(PORT_HOTPLUG_EN,
	       temp |
	       DPB_HOTPLUG_INT_EN |
	       DPC_HOTPLUG_INT_EN |
	       DPD_HOTPLUG_INT_EN);

	POSTING_READ(PORT_HOTPLUG_EN);

	switch (dp_priv->output_reg) {
	case DP_B:
		bit = DPB_HOTPLUG_INT_STATUS;
		break;
	case DP_C:
		bit = DPC_HOTPLUG_INT_STATUS;
		break;
	case DP_D:
		bit = DPD_HOTPLUG_INT_STATUS;
		break;
	default:
		return connector_status_unknown;
	}

	temp = I915_READ(PORT_HOTPLUG_STAT);

	if ((temp & bit) == 0)
		return connector_status_disconnected;

	status = connector_status_disconnected;
	if (intel_dp_aux_native_read(intel_output,
				     0x000, dp_priv->dpcd,
				     sizeof (dp_priv->dpcd)) == sizeof (dp_priv->dpcd))
	{
		if (dp_priv->dpcd[0] != 0)
			status = connector_status_connected;
	}
	return status;
}

static int intel_dp_get_modes(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);
	struct drm_device *dev = intel_output->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	/* We should parse the EDID data and find out if it has an audio sink
	 */

	ret = intel_ddc_get_modes(intel_output);
	if (ret)
		return ret;

	/* if eDP has no EDID, try to use fixed panel mode from VBT */
	if (IS_eDP(intel_output)) {
		if (dev_priv->panel_fixed_mode != NULL) {
			struct drm_display_mode *mode;
			mode = drm_mode_duplicate(dev, dev_priv->panel_fixed_mode);
			drm_mode_probed_add(connector, mode);
			return 1;
		}
	}
	return 0;
}

static void
intel_dp_destroy (struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);

	if (intel_output->i2c_bus)
		intel_i2c_destroy(intel_output->i2c_bus);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(intel_output);
}

static const struct drm_encoder_helper_funcs intel_dp_helper_funcs = {
	.dpms = intel_dp_dpms,
	.mode_fixup = intel_dp_mode_fixup,
	.prepare = intel_encoder_prepare,
	.mode_set = intel_dp_mode_set,
	.commit = intel_encoder_commit,
};

static const struct drm_connector_funcs intel_dp_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = intel_dp_save,
	.restore = intel_dp_restore,
	.detect = intel_dp_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = intel_dp_destroy,
};

static const struct drm_connector_helper_funcs intel_dp_connector_helper_funcs = {
	.get_modes = intel_dp_get_modes,
	.mode_valid = intel_dp_mode_valid,
	.best_encoder = intel_best_encoder,
};

static void intel_dp_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs intel_dp_enc_funcs = {
	.destroy = intel_dp_enc_destroy,
};

void
intel_dp_hot_plug(struct intel_output *intel_output)
{
	struct intel_dp_priv *dp_priv = intel_output->dev_priv;

	if (dp_priv->dpms_mode == DRM_MODE_DPMS_ON)
		intel_dp_check_link_status(intel_output);
}

void
intel_dp_init(struct drm_device *dev, int output_reg)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_connector *connector;
	struct intel_output *intel_output;
	struct intel_dp_priv *dp_priv;
	const char *name = NULL;

	intel_output = kcalloc(sizeof(struct intel_output) + 
			       sizeof(struct intel_dp_priv), 1, GFP_KERNEL);
	if (!intel_output)
		return;

	dp_priv = (struct intel_dp_priv *)(intel_output + 1);

	connector = &intel_output->base;
	drm_connector_init(dev, connector, &intel_dp_connector_funcs,
			   DRM_MODE_CONNECTOR_DisplayPort);
	drm_connector_helper_add(connector, &intel_dp_connector_helper_funcs);

	if (output_reg == DP_A)
		intel_output->type = INTEL_OUTPUT_EDP;
	else
		intel_output->type = INTEL_OUTPUT_DISPLAYPORT;

	if (output_reg == DP_B)
		intel_output->clone_mask = (1 << INTEL_DP_B_CLONE_BIT);
	else if (output_reg == DP_C)
		intel_output->clone_mask = (1 << INTEL_DP_C_CLONE_BIT);
	else if (output_reg == DP_D)
		intel_output->clone_mask = (1 << INTEL_DP_D_CLONE_BIT);

	if (IS_eDP(intel_output)) {
		intel_output->crtc_mask = (1 << 1);
		intel_output->clone_mask = (1 << INTEL_EDP_CLONE_BIT);
	} else
		intel_output->crtc_mask = (1 << 0) | (1 << 1);
	connector->interlace_allowed = true;
	connector->doublescan_allowed = 0;

	dp_priv->intel_output = intel_output;
	dp_priv->output_reg = output_reg;
	dp_priv->has_audio = false;
	dp_priv->dpms_mode = DRM_MODE_DPMS_ON;
	intel_output->dev_priv = dp_priv;

	drm_encoder_init(dev, &intel_output->enc, &intel_dp_enc_funcs,
			 DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(&intel_output->enc, &intel_dp_helper_funcs);

	drm_mode_connector_attach_encoder(&intel_output->base,
					  &intel_output->enc);
	drm_sysfs_connector_add(connector);

	/* Set up the DDC bus. */
	switch (output_reg) {
		case DP_A:
			name = "DPDDC-A";
			break;
		case DP_B:
		case PCH_DP_B:
			name = "DPDDC-B";
			break;
		case DP_C:
		case PCH_DP_C:
			name = "DPDDC-C";
			break;
		case DP_D:
		case PCH_DP_D:
			name = "DPDDC-D";
			break;
	}

	intel_dp_i2c_init(intel_output, name);

	intel_output->ddc_bus = &dp_priv->adapter;
	intel_output->hot_plug = intel_dp_hot_plug;

	if (output_reg == DP_A) {
		/* initialize panel mode from VBT if available for eDP */
		if (dev_priv->lfp_lvds_vbt_mode) {
			dev_priv->panel_fixed_mode =
				drm_mode_duplicate(dev, dev_priv->lfp_lvds_vbt_mode);
			if (dev_priv->panel_fixed_mode) {
				dev_priv->panel_fixed_mode->type |=
					DRM_MODE_TYPE_PREFERRED;
			}
		}
	}

	/* For G4X desktop chip, PEG_BAND_GAP_DATA 3:0 must first be written
	 * 0xd.  Failure to do so will result in spurious interrupts being
	 * generated on the port when a cable is not attached.
	 */
	if (IS_G4X(dev) && !IS_GM45(dev)) {
		u32 temp = I915_READ(PEG_BAND_GAP_DATA);
		I915_WRITE(PEG_BAND_GAP_DATA, (temp & ~0xf) | 0xd);
	}
}
