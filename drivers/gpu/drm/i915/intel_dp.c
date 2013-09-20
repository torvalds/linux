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
#include <linux/slab.h>
#include <linux/export.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"

#define DP_LINK_CHECK_TIMEOUT	(10 * 1000)

struct dp_link_dpll {
	int link_bw;
	struct dpll dpll;
};

static const struct dp_link_dpll gen4_dpll[] = {
	{ DP_LINK_BW_1_62,
		{ .p1 = 2, .p2 = 10, .n = 2, .m1 = 23, .m2 = 8 } },
	{ DP_LINK_BW_2_7,
		{ .p1 = 1, .p2 = 10, .n = 1, .m1 = 14, .m2 = 2 } }
};

static const struct dp_link_dpll pch_dpll[] = {
	{ DP_LINK_BW_1_62,
		{ .p1 = 2, .p2 = 10, .n = 1, .m1 = 12, .m2 = 9 } },
	{ DP_LINK_BW_2_7,
		{ .p1 = 1, .p2 = 10, .n = 2, .m1 = 14, .m2 = 8 } }
};

static const struct dp_link_dpll vlv_dpll[] = {
	{ DP_LINK_BW_1_62,
		{ .p1 = 3, .p2 = 2, .n = 5, .m1 = 5, .m2 = 3 } },
	{ DP_LINK_BW_2_7,
		{ .p1 = 2, .p2 = 2, .n = 1, .m1 = 2, .m2 = 27 } }
};

/**
 * is_edp - is the given port attached to an eDP panel (either CPU or PCH)
 * @intel_dp: DP struct
 *
 * If a CPU or PCH DP output is attached to an eDP panel, this function
 * will return true, and false otherwise.
 */
static bool is_edp(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);

	return intel_dig_port->base.type == INTEL_OUTPUT_EDP;
}

static struct drm_device *intel_dp_to_dev(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);

	return intel_dig_port->base.base.dev;
}

static struct intel_dp *intel_attached_dp(struct drm_connector *connector)
{
	return enc_to_intel_dp(&intel_attached_encoder(connector)->base);
}

static void intel_dp_link_down(struct intel_dp *intel_dp);

static int
intel_dp_max_link_bw(struct intel_dp *intel_dp)
{
	int max_link_bw = intel_dp->dpcd[DP_MAX_LINK_RATE];

	switch (max_link_bw) {
	case DP_LINK_BW_1_62:
	case DP_LINK_BW_2_7:
		break;
	case DP_LINK_BW_5_4: /* 1.2 capable displays may advertise higher bw */
		max_link_bw = DP_LINK_BW_2_7;
		break;
	default:
		WARN(1, "invalid max DP link bw val %x, using 1.62Gbps\n",
		     max_link_bw);
		max_link_bw = DP_LINK_BW_1_62;
		break;
	}
	return max_link_bw;
}

/*
 * The units on the numbers in the next two are... bizarre.  Examples will
 * make it clearer; this one parallels an example in the eDP spec.
 *
 * intel_dp_max_data_rate for one lane of 2.7GHz evaluates as:
 *
 *     270000 * 1 * 8 / 10 == 216000
 *
 * The actual data capacity of that configuration is 2.16Gbit/s, so the
 * units are decakilobits.  ->clock in a drm_display_mode is in kilohertz -
 * or equivalently, kilopixels per second - so for 1680x1050R it'd be
 * 119000.  At 18bpp that's 2142000 kilobits per second.
 *
 * Thus the strange-looking division by 10 in intel_dp_link_required, to
 * get the result in decakilobits instead of kilobits.
 */

static int
intel_dp_link_required(int pixel_clock, int bpp)
{
	return (pixel_clock * bpp + 9) / 10;
}

static int
intel_dp_max_data_rate(int max_link_clock, int max_lanes)
{
	return (max_link_clock * max_lanes * 8) / 10;
}

static int
intel_dp_mode_valid(struct drm_connector *connector,
		    struct drm_display_mode *mode)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_display_mode *fixed_mode = intel_connector->panel.fixed_mode;
	int target_clock = mode->clock;
	int max_rate, mode_rate, max_lanes, max_link_clock;

	if (is_edp(intel_dp) && fixed_mode) {
		if (mode->hdisplay > fixed_mode->hdisplay)
			return MODE_PANEL;

		if (mode->vdisplay > fixed_mode->vdisplay)
			return MODE_PANEL;

		target_clock = fixed_mode->clock;
	}

	max_link_clock = drm_dp_bw_code_to_link_rate(intel_dp_max_link_bw(intel_dp));
	max_lanes = drm_dp_max_lane_count(intel_dp->dpcd);

	max_rate = intel_dp_max_data_rate(max_link_clock, max_lanes);
	mode_rate = intel_dp_link_required(target_clock, 18);

	if (mode_rate > max_rate)
		return MODE_CLOCK_HIGH;

	if (mode->clock < 10000)
		return MODE_CLOCK_LOW;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		return MODE_H_ILLEGAL;

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

	/* There is no CLKCFG reg in Valleyview. VLV hrawclk is 200 MHz */
	if (IS_VALLEYVIEW(dev))
		return 200;

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

static void
intel_dp_init_panel_power_sequencer(struct drm_device *dev,
				    struct intel_dp *intel_dp,
				    struct edp_power_seq *out);
static void
intel_dp_init_panel_power_sequencer_registers(struct drm_device *dev,
					      struct intel_dp *intel_dp,
					      struct edp_power_seq *out);

static enum pipe
vlv_power_sequencer_pipe(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_crtc *crtc = intel_dig_port->base.base.crtc;
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_dig_port->port;
	enum pipe pipe;

	/* modeset should have pipe */
	if (crtc)
		return to_intel_crtc(crtc)->pipe;

	/* init time, try to find a pipe with this port selected */
	for (pipe = PIPE_A; pipe <= PIPE_B; pipe++) {
		u32 port_sel = I915_READ(VLV_PIPE_PP_ON_DELAYS(pipe)) &
			PANEL_PORT_SELECT_MASK;
		if (port_sel == PANEL_PORT_SELECT_DPB_VLV && port == PORT_B)
			return pipe;
		if (port_sel == PANEL_PORT_SELECT_DPC_VLV && port == PORT_C)
			return pipe;
	}

	/* shrug */
	return PIPE_A;
}

static u32 _pp_ctrl_reg(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);

	if (HAS_PCH_SPLIT(dev))
		return PCH_PP_CONTROL;
	else
		return VLV_PIPE_PP_CONTROL(vlv_power_sequencer_pipe(intel_dp));
}

static u32 _pp_stat_reg(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);

	if (HAS_PCH_SPLIT(dev))
		return PCH_PP_STATUS;
	else
		return VLV_PIPE_PP_STATUS(vlv_power_sequencer_pipe(intel_dp));
}

static bool ironlake_edp_have_panel_power(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;

	return (I915_READ(_pp_stat_reg(intel_dp)) & PP_ON) != 0;
}

static bool ironlake_edp_have_panel_vdd(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;

	return (I915_READ(_pp_ctrl_reg(intel_dp)) & EDP_FORCE_VDD) != 0;
}

static void
intel_dp_check_edp(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!is_edp(intel_dp))
		return;

	if (!ironlake_edp_have_panel_power(intel_dp) && !ironlake_edp_have_panel_vdd(intel_dp)) {
		WARN(1, "eDP powered off while attempting aux channel communication.\n");
		DRM_DEBUG_KMS("Status 0x%08x Control 0x%08x\n",
			      I915_READ(_pp_stat_reg(intel_dp)),
			      I915_READ(_pp_ctrl_reg(intel_dp)));
	}
}

static uint32_t
intel_dp_aux_wait_done(struct intel_dp *intel_dp, bool has_aux_irq)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t ch_ctl = intel_dp->aux_ch_ctl_reg;
	uint32_t status;
	bool done;

#define C (((status = I915_READ_NOTRACE(ch_ctl)) & DP_AUX_CH_CTL_SEND_BUSY) == 0)
	if (has_aux_irq)
		done = wait_event_timeout(dev_priv->gmbus_wait_queue, C,
					  msecs_to_jiffies_timeout(10));
	else
		done = wait_for_atomic(C, 10) == 0;
	if (!done)
		DRM_ERROR("dp aux hw did not signal timeout (has irq: %i)!\n",
			  has_aux_irq);
#undef C

	return status;
}

static uint32_t get_aux_clock_divider(struct intel_dp *intel_dp,
				      int index)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* The clock divider is based off the hrawclk,
	 * and would like to run at 2MHz. So, take the
	 * hrawclk value and divide by 2 and use that
	 *
	 * Note that PCH attached eDP panels should use a 125MHz input
	 * clock divider.
	 */
	if (IS_VALLEYVIEW(dev)) {
		return index ? 0 : 100;
	} else if (intel_dig_port->port == PORT_A) {
		if (index)
			return 0;
		if (HAS_DDI(dev))
			return DIV_ROUND_CLOSEST(intel_ddi_get_cdclk_freq(dev_priv), 2000);
		else if (IS_GEN6(dev) || IS_GEN7(dev))
			return 200; /* SNB & IVB eDP input clock at 400Mhz */
		else
			return 225; /* eDP input clock at 450Mhz */
	} else if (dev_priv->pch_id == INTEL_PCH_LPT_DEVICE_ID_TYPE) {
		/* Workaround for non-ULT HSW */
		switch (index) {
		case 0: return 63;
		case 1: return 72;
		default: return 0;
		}
	} else if (HAS_PCH_SPLIT(dev)) {
		return index ? 0 : DIV_ROUND_UP(intel_pch_rawclk(dev), 2);
	} else {
		return index ? 0 :intel_hrawclk(dev) / 2;
	}
}

static int
intel_dp_aux_ch(struct intel_dp *intel_dp,
		uint8_t *send, int send_bytes,
		uint8_t *recv, int recv_size)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t ch_ctl = intel_dp->aux_ch_ctl_reg;
	uint32_t ch_data = ch_ctl + 4;
	uint32_t aux_clock_divider;
	int i, ret, recv_bytes;
	uint32_t status;
	int try, precharge, clock = 0;
	bool has_aux_irq = INTEL_INFO(dev)->gen >= 5 && !IS_VALLEYVIEW(dev);

	/* dp aux is extremely sensitive to irq latency, hence request the
	 * lowest possible wakeup latency and so prevent the cpu from going into
	 * deep sleep states.
	 */
	pm_qos_update_request(&dev_priv->pm_qos, 0);

	intel_dp_check_edp(intel_dp);

	if (IS_GEN6(dev))
		precharge = 3;
	else
		precharge = 5;

	intel_aux_display_runtime_get(dev_priv);

	/* Try to wait for any previous AUX channel activity */
	for (try = 0; try < 3; try++) {
		status = I915_READ_NOTRACE(ch_ctl);
		if ((status & DP_AUX_CH_CTL_SEND_BUSY) == 0)
			break;
		msleep(1);
	}

	if (try == 3) {
		WARN(1, "dp_aux_ch not started status 0x%08x\n",
		     I915_READ(ch_ctl));
		ret = -EBUSY;
		goto out;
	}

	/* Only 5 data registers! */
	if (WARN_ON(send_bytes > 20 || recv_size > 20)) {
		ret = -E2BIG;
		goto out;
	}

	while ((aux_clock_divider = get_aux_clock_divider(intel_dp, clock++))) {
		/* Must try at least 3 times according to DP spec */
		for (try = 0; try < 5; try++) {
			/* Load the send data into the aux channel data registers */
			for (i = 0; i < send_bytes; i += 4)
				I915_WRITE(ch_data + i,
					   pack_aux(send + i, send_bytes - i));

			/* Send the command and wait for it to complete */
			I915_WRITE(ch_ctl,
				   DP_AUX_CH_CTL_SEND_BUSY |
				   (has_aux_irq ? DP_AUX_CH_CTL_INTERRUPT : 0) |
				   DP_AUX_CH_CTL_TIME_OUT_400us |
				   (send_bytes << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
				   (precharge << DP_AUX_CH_CTL_PRECHARGE_2US_SHIFT) |
				   (aux_clock_divider << DP_AUX_CH_CTL_BIT_CLOCK_2X_SHIFT) |
				   DP_AUX_CH_CTL_DONE |
				   DP_AUX_CH_CTL_TIME_OUT_ERROR |
				   DP_AUX_CH_CTL_RECEIVE_ERROR);

			status = intel_dp_aux_wait_done(intel_dp, has_aux_irq);

			/* Clear done status and any errors */
			I915_WRITE(ch_ctl,
				   status |
				   DP_AUX_CH_CTL_DONE |
				   DP_AUX_CH_CTL_TIME_OUT_ERROR |
				   DP_AUX_CH_CTL_RECEIVE_ERROR);

			if (status & (DP_AUX_CH_CTL_TIME_OUT_ERROR |
				      DP_AUX_CH_CTL_RECEIVE_ERROR))
				continue;
			if (status & DP_AUX_CH_CTL_DONE)
				break;
		}
		if (status & DP_AUX_CH_CTL_DONE)
			break;
	}

	if ((status & DP_AUX_CH_CTL_DONE) == 0) {
		DRM_ERROR("dp_aux_ch not done status 0x%08x\n", status);
		ret = -EBUSY;
		goto out;
	}

	/* Check for timeout or receive error.
	 * Timeouts occur when the sink is not connected
	 */
	if (status & DP_AUX_CH_CTL_RECEIVE_ERROR) {
		DRM_ERROR("dp_aux_ch receive error status 0x%08x\n", status);
		ret = -EIO;
		goto out;
	}

	/* Timeouts occur when the device isn't connected, so they're
	 * "normal" -- don't fill the kernel log with these */
	if (status & DP_AUX_CH_CTL_TIME_OUT_ERROR) {
		DRM_DEBUG_KMS("dp_aux_ch timeout status 0x%08x\n", status);
		ret = -ETIMEDOUT;
		goto out;
	}

	/* Unload any bytes sent back from the other side */
	recv_bytes = ((status & DP_AUX_CH_CTL_MESSAGE_SIZE_MASK) >>
		      DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT);
	if (recv_bytes > recv_size)
		recv_bytes = recv_size;

	for (i = 0; i < recv_bytes; i += 4)
		unpack_aux(I915_READ(ch_data + i),
			   recv + i, recv_bytes - i);

	ret = recv_bytes;
out:
	pm_qos_update_request(&dev_priv->pm_qos, PM_QOS_DEFAULT_VALUE);
	intel_aux_display_runtime_put(dev_priv);

	return ret;
}

/* Write data to the aux channel in native mode */
static int
intel_dp_aux_native_write(struct intel_dp *intel_dp,
			  uint16_t address, uint8_t *send, int send_bytes)
{
	int ret;
	uint8_t	msg[20];
	int msg_bytes;
	uint8_t	ack;

	if (WARN_ON(send_bytes > 16))
		return -E2BIG;

	intel_dp_check_edp(intel_dp);
	msg[0] = AUX_NATIVE_WRITE << 4;
	msg[1] = address >> 8;
	msg[2] = address & 0xff;
	msg[3] = send_bytes - 1;
	memcpy(&msg[4], send, send_bytes);
	msg_bytes = send_bytes + 4;
	for (;;) {
		ret = intel_dp_aux_ch(intel_dp, msg, msg_bytes, &ack, 1);
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
intel_dp_aux_native_write_1(struct intel_dp *intel_dp,
			    uint16_t address, uint8_t byte)
{
	return intel_dp_aux_native_write(intel_dp, address, &byte, 1);
}

/* read bytes from a native aux channel */
static int
intel_dp_aux_native_read(struct intel_dp *intel_dp,
			 uint16_t address, uint8_t *recv, int recv_bytes)
{
	uint8_t msg[4];
	int msg_bytes;
	uint8_t reply[20];
	int reply_bytes;
	uint8_t ack;
	int ret;

	if (WARN_ON(recv_bytes > 19))
		return -E2BIG;

	intel_dp_check_edp(intel_dp);
	msg[0] = AUX_NATIVE_READ << 4;
	msg[1] = address >> 8;
	msg[2] = address & 0xff;
	msg[3] = recv_bytes - 1;

	msg_bytes = 4;
	reply_bytes = recv_bytes + 1;

	for (;;) {
		ret = intel_dp_aux_ch(intel_dp, msg, msg_bytes,
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
intel_dp_i2c_aux_ch(struct i2c_adapter *adapter, int mode,
		    uint8_t write_byte, uint8_t *read_byte)
{
	struct i2c_algo_dp_aux_data *algo_data = adapter->algo_data;
	struct intel_dp *intel_dp = container_of(adapter,
						struct intel_dp,
						adapter);
	uint16_t address = algo_data->address;
	uint8_t msg[5];
	uint8_t reply[2];
	unsigned retry;
	int msg_bytes;
	int reply_bytes;
	int ret;

	intel_dp_check_edp(intel_dp);
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
		ret = intel_dp_aux_ch(intel_dp,
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
			/*
			 * For now, just give more slack to branch devices. We
			 * could check the DPCD for I2C bit rate capabilities,
			 * and if available, adjust the interval. We could also
			 * be more careful with DP-to-Legacy adapters where a
			 * long legacy cable may force very low I2C bit rates.
			 */
			if (intel_dp->dpcd[DP_DOWNSTREAMPORT_PRESENT] &
			    DP_DWN_STRM_PORT_PRESENT)
				usleep_range(500, 600);
			else
				usleep_range(300, 400);
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
intel_dp_i2c_init(struct intel_dp *intel_dp,
		  struct intel_connector *intel_connector, const char *name)
{
	int	ret;

	DRM_DEBUG_KMS("i2c_init %s\n", name);
	intel_dp->algo.running = false;
	intel_dp->algo.address = 0;
	intel_dp->algo.aux_ch = intel_dp_i2c_aux_ch;

	memset(&intel_dp->adapter, '\0', sizeof(intel_dp->adapter));
	intel_dp->adapter.owner = THIS_MODULE;
	intel_dp->adapter.class = I2C_CLASS_DDC;
	strncpy(intel_dp->adapter.name, name, sizeof(intel_dp->adapter.name) - 1);
	intel_dp->adapter.name[sizeof(intel_dp->adapter.name) - 1] = '\0';
	intel_dp->adapter.algo_data = &intel_dp->algo;
	intel_dp->adapter.dev.parent = &intel_connector->base.kdev;

	ironlake_edp_panel_vdd_on(intel_dp);
	ret = i2c_dp_aux_add_bus(&intel_dp->adapter);
	ironlake_edp_panel_vdd_off(intel_dp, false);
	return ret;
}

static void
intel_dp_set_clock(struct intel_encoder *encoder,
		   struct intel_crtc_config *pipe_config, int link_bw)
{
	struct drm_device *dev = encoder->base.dev;
	const struct dp_link_dpll *divisor = NULL;
	int i, count = 0;

	if (IS_G4X(dev)) {
		divisor = gen4_dpll;
		count = ARRAY_SIZE(gen4_dpll);
	} else if (IS_HASWELL(dev)) {
		/* Haswell has special-purpose DP DDI clocks. */
	} else if (HAS_PCH_SPLIT(dev)) {
		divisor = pch_dpll;
		count = ARRAY_SIZE(pch_dpll);
	} else if (IS_VALLEYVIEW(dev)) {
		divisor = vlv_dpll;
		count = ARRAY_SIZE(vlv_dpll);
	}

	if (divisor && count) {
		for (i = 0; i < count; i++) {
			if (link_bw == divisor[i].link_bw) {
				pipe_config->dpll = divisor[i].dpll;
				pipe_config->clock_set = true;
				break;
			}
		}
	}
}

bool
intel_dp_compute_config(struct intel_encoder *encoder,
			struct intel_crtc_config *pipe_config)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_display_mode *adjusted_mode = &pipe_config->adjusted_mode;
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = dp_to_dig_port(intel_dp)->port;
	struct intel_crtc *intel_crtc = encoder->new_crtc;
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	int lane_count, clock;
	int max_lane_count = drm_dp_max_lane_count(intel_dp->dpcd);
	int max_clock = intel_dp_max_link_bw(intel_dp) == DP_LINK_BW_2_7 ? 1 : 0;
	int bpp, mode_rate;
	static int bws[2] = { DP_LINK_BW_1_62, DP_LINK_BW_2_7 };
	int link_avail, link_clock;

	if (HAS_PCH_SPLIT(dev) && !HAS_DDI(dev) && port != PORT_A)
		pipe_config->has_pch_encoder = true;

	pipe_config->has_dp_encoder = true;

	if (is_edp(intel_dp) && intel_connector->panel.fixed_mode) {
		intel_fixed_panel_mode(intel_connector->panel.fixed_mode,
				       adjusted_mode);
		if (!HAS_PCH_SPLIT(dev))
			intel_gmch_panel_fitting(intel_crtc, pipe_config,
						 intel_connector->panel.fitting_mode);
		else
			intel_pch_panel_fitting(intel_crtc, pipe_config,
						intel_connector->panel.fitting_mode);
	}

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLCLK)
		return false;

	DRM_DEBUG_KMS("DP link computation with max lane count %i "
		      "max bw %02x pixel clock %iKHz\n",
		      max_lane_count, bws[max_clock], adjusted_mode->clock);

	/* Walk through all bpp values. Luckily they're all nicely spaced with 2
	 * bpc in between. */
	bpp = pipe_config->pipe_bpp;
	if (is_edp(intel_dp) && dev_priv->vbt.edp_bpp) {
		DRM_DEBUG_KMS("clamping bpp for eDP panel to BIOS-provided %i\n",
			      dev_priv->vbt.edp_bpp);
		bpp = min_t(int, bpp, dev_priv->vbt.edp_bpp);
	}

	for (; bpp >= 6*3; bpp -= 2*3) {
		mode_rate = intel_dp_link_required(adjusted_mode->clock, bpp);

		for (clock = 0; clock <= max_clock; clock++) {
			for (lane_count = 1; lane_count <= max_lane_count; lane_count <<= 1) {
				link_clock = drm_dp_bw_code_to_link_rate(bws[clock]);
				link_avail = intel_dp_max_data_rate(link_clock,
								    lane_count);

				if (mode_rate <= link_avail) {
					goto found;
				}
			}
		}
	}

	return false;

found:
	if (intel_dp->color_range_auto) {
		/*
		 * See:
		 * CEA-861-E - 5.1 Default Encoding Parameters
		 * VESA DisplayPort Ver.1.2a - 5.1.1.1 Video Colorimetry
		 */
		if (bpp != 18 && drm_match_cea_mode(adjusted_mode) > 1)
			intel_dp->color_range = DP_COLOR_RANGE_16_235;
		else
			intel_dp->color_range = 0;
	}

	if (intel_dp->color_range)
		pipe_config->limited_color_range = true;

	intel_dp->link_bw = bws[clock];
	intel_dp->lane_count = lane_count;
	pipe_config->pipe_bpp = bpp;
	pipe_config->port_clock = drm_dp_bw_code_to_link_rate(intel_dp->link_bw);

	DRM_DEBUG_KMS("DP link bw %02x lane count %d clock %d bpp %d\n",
		      intel_dp->link_bw, intel_dp->lane_count,
		      pipe_config->port_clock, bpp);
	DRM_DEBUG_KMS("DP link bw required %i available %i\n",
		      mode_rate, link_avail);

	intel_link_compute_m_n(bpp, lane_count,
			       adjusted_mode->clock, pipe_config->port_clock,
			       &pipe_config->dp_m_n);

	intel_dp_set_clock(encoder, pipe_config, intel_dp->link_bw);

	return true;
}

void intel_dp_init_link_config(struct intel_dp *intel_dp)
{
	memset(intel_dp->link_configuration, 0, DP_LINK_CONFIGURATION_SIZE);
	intel_dp->link_configuration[0] = intel_dp->link_bw;
	intel_dp->link_configuration[1] = intel_dp->lane_count;
	intel_dp->link_configuration[8] = DP_SET_ANSI_8B10B;
	/*
	 * Check for DPCD version > 1.1 and enhanced framing support
	 */
	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11 &&
	    (intel_dp->dpcd[DP_MAX_LANE_COUNT] & DP_ENHANCED_FRAME_CAP)) {
		intel_dp->link_configuration[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	}
}

static void ironlake_set_pll_cpu_edp(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_crtc *crtc = to_intel_crtc(dig_port->base.base.crtc);
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpa_ctl;

	DRM_DEBUG_KMS("eDP PLL enable for clock %d\n", crtc->config.port_clock);
	dpa_ctl = I915_READ(DP_A);
	dpa_ctl &= ~DP_PLL_FREQ_MASK;

	if (crtc->config.port_clock == 162000) {
		/* For a long time we've carried around a ILK-DevA w/a for the
		 * 160MHz clock. If we're really unlucky, it's still required.
		 */
		DRM_DEBUG_KMS("160MHz cpu eDP clock, might need ilk devA w/a\n");
		dpa_ctl |= DP_PLL_FREQ_160MHZ;
		intel_dp->DP |= DP_PLL_FREQ_160MHZ;
	} else {
		dpa_ctl |= DP_PLL_FREQ_270MHZ;
		intel_dp->DP |= DP_PLL_FREQ_270MHZ;
	}

	I915_WRITE(DP_A, dpa_ctl);

	POSTING_READ(DP_A);
	udelay(500);
}

static void intel_dp_mode_set(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = dp_to_dig_port(intel_dp)->port;
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);
	struct drm_display_mode *adjusted_mode = &crtc->config.adjusted_mode;

	/*
	 * There are four kinds of DP registers:
	 *
	 * 	IBX PCH
	 * 	SNB CPU
	 *	IVB CPU
	 * 	CPT PCH
	 *
	 * IBX PCH and CPU are the same for almost everything,
	 * except that the CPU DP PLL is configured in this
	 * register
	 *
	 * CPT PCH is quite different, having many bits moved
	 * to the TRANS_DP_CTL register instead. That
	 * configuration happens (oddly) in ironlake_pch_enable
	 */

	/* Preserve the BIOS-computed detected bit. This is
	 * supposed to be read-only.
	 */
	intel_dp->DP = I915_READ(intel_dp->output_reg) & DP_DETECTED;

	/* Handle DP bits in common between all three register formats */
	intel_dp->DP |= DP_VOLTAGE_0_4 | DP_PRE_EMPHASIS_0;
	intel_dp->DP |= DP_PORT_WIDTH(intel_dp->lane_count);

	if (intel_dp->has_audio) {
		DRM_DEBUG_DRIVER("Enabling DP audio on pipe %c\n",
				 pipe_name(crtc->pipe));
		intel_dp->DP |= DP_AUDIO_OUTPUT_ENABLE;
		intel_write_eld(&encoder->base, adjusted_mode);
	}

	intel_dp_init_link_config(intel_dp);

	/* Split out the IBX/CPU vs CPT settings */

	if (port == PORT_A && IS_GEN7(dev) && !IS_VALLEYVIEW(dev)) {
		if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
			intel_dp->DP |= DP_SYNC_HS_HIGH;
		if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
			intel_dp->DP |= DP_SYNC_VS_HIGH;
		intel_dp->DP |= DP_LINK_TRAIN_OFF_CPT;

		if (intel_dp->link_configuration[1] & DP_LANE_COUNT_ENHANCED_FRAME_EN)
			intel_dp->DP |= DP_ENHANCED_FRAMING;

		intel_dp->DP |= crtc->pipe << 29;
	} else if (!HAS_PCH_CPT(dev) || port == PORT_A) {
		if (!HAS_PCH_SPLIT(dev) && !IS_VALLEYVIEW(dev))
			intel_dp->DP |= intel_dp->color_range;

		if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
			intel_dp->DP |= DP_SYNC_HS_HIGH;
		if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
			intel_dp->DP |= DP_SYNC_VS_HIGH;
		intel_dp->DP |= DP_LINK_TRAIN_OFF;

		if (intel_dp->link_configuration[1] & DP_LANE_COUNT_ENHANCED_FRAME_EN)
			intel_dp->DP |= DP_ENHANCED_FRAMING;

		if (crtc->pipe == 1)
			intel_dp->DP |= DP_PIPEB_SELECT;
	} else {
		intel_dp->DP |= DP_LINK_TRAIN_OFF_CPT;
	}

	if (port == PORT_A && !IS_VALLEYVIEW(dev))
		ironlake_set_pll_cpu_edp(intel_dp);
}

#define IDLE_ON_MASK		(PP_ON | 0 	  | PP_SEQUENCE_MASK | 0                     | PP_SEQUENCE_STATE_MASK)
#define IDLE_ON_VALUE   	(PP_ON | 0 	  | PP_SEQUENCE_NONE | 0                     | PP_SEQUENCE_STATE_ON_IDLE)

#define IDLE_OFF_MASK		(PP_ON | 0        | PP_SEQUENCE_MASK | 0                     | PP_SEQUENCE_STATE_MASK)
#define IDLE_OFF_VALUE		(0     | 0        | PP_SEQUENCE_NONE | 0                     | PP_SEQUENCE_STATE_OFF_IDLE)

#define IDLE_CYCLE_MASK		(PP_ON | 0        | PP_SEQUENCE_MASK | PP_CYCLE_DELAY_ACTIVE | PP_SEQUENCE_STATE_MASK)
#define IDLE_CYCLE_VALUE	(0     | 0        | PP_SEQUENCE_NONE | 0                     | PP_SEQUENCE_STATE_OFF_IDLE)

static void ironlake_wait_panel_status(struct intel_dp *intel_dp,
				       u32 mask,
				       u32 value)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp_stat_reg, pp_ctrl_reg;

	pp_stat_reg = _pp_stat_reg(intel_dp);
	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	DRM_DEBUG_KMS("mask %08x value %08x status %08x control %08x\n",
			mask, value,
			I915_READ(pp_stat_reg),
			I915_READ(pp_ctrl_reg));

	if (_wait_for((I915_READ(pp_stat_reg) & mask) == value, 5000, 10)) {
		DRM_ERROR("Panel status timeout: status %08x control %08x\n",
				I915_READ(pp_stat_reg),
				I915_READ(pp_ctrl_reg));
	}
}

static void ironlake_wait_panel_on(struct intel_dp *intel_dp)
{
	DRM_DEBUG_KMS("Wait for panel power on\n");
	ironlake_wait_panel_status(intel_dp, IDLE_ON_MASK, IDLE_ON_VALUE);
}

static void ironlake_wait_panel_off(struct intel_dp *intel_dp)
{
	DRM_DEBUG_KMS("Wait for panel power off time\n");
	ironlake_wait_panel_status(intel_dp, IDLE_OFF_MASK, IDLE_OFF_VALUE);
}

static void ironlake_wait_panel_power_cycle(struct intel_dp *intel_dp)
{
	DRM_DEBUG_KMS("Wait for panel power cycle\n");
	ironlake_wait_panel_status(intel_dp, IDLE_CYCLE_MASK, IDLE_CYCLE_VALUE);
}


/* Read the current pp_control value, unlocking the register if it
 * is locked
 */

static  u32 ironlake_get_pp_control(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 control;

	control = I915_READ(_pp_ctrl_reg(intel_dp));
	control &= ~PANEL_UNLOCK_MASK;
	control |= PANEL_UNLOCK_REGS;
	return control;
}

void ironlake_edp_panel_vdd_on(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp;
	u32 pp_stat_reg, pp_ctrl_reg;

	if (!is_edp(intel_dp))
		return;
	DRM_DEBUG_KMS("Turn eDP VDD on\n");

	WARN(intel_dp->want_panel_vdd,
	     "eDP VDD already requested on\n");

	intel_dp->want_panel_vdd = true;

	if (ironlake_edp_have_panel_vdd(intel_dp)) {
		DRM_DEBUG_KMS("eDP VDD already on\n");
		return;
	}

	if (!ironlake_edp_have_panel_power(intel_dp))
		ironlake_wait_panel_power_cycle(intel_dp);

	pp = ironlake_get_pp_control(intel_dp);
	pp |= EDP_FORCE_VDD;

	pp_stat_reg = _pp_stat_reg(intel_dp);
	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);
	DRM_DEBUG_KMS("PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
			I915_READ(pp_stat_reg), I915_READ(pp_ctrl_reg));
	/*
	 * If the panel wasn't on, delay before accessing aux channel
	 */
	if (!ironlake_edp_have_panel_power(intel_dp)) {
		DRM_DEBUG_KMS("eDP was not running\n");
		msleep(intel_dp->panel_power_up_delay);
	}
}

static void ironlake_panel_vdd_off_sync(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp;
	u32 pp_stat_reg, pp_ctrl_reg;

	WARN_ON(!mutex_is_locked(&dev->mode_config.mutex));

	if (!intel_dp->want_panel_vdd && ironlake_edp_have_panel_vdd(intel_dp)) {
		pp = ironlake_get_pp_control(intel_dp);
		pp &= ~EDP_FORCE_VDD;

		pp_stat_reg = _pp_ctrl_reg(intel_dp);
		pp_ctrl_reg = _pp_stat_reg(intel_dp);

		I915_WRITE(pp_ctrl_reg, pp);
		POSTING_READ(pp_ctrl_reg);

		/* Make sure sequencer is idle before allowing subsequent activity */
		DRM_DEBUG_KMS("PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
		I915_READ(pp_stat_reg), I915_READ(pp_ctrl_reg));
		msleep(intel_dp->panel_power_down_delay);
	}
}

static void ironlake_panel_vdd_work(struct work_struct *__work)
{
	struct intel_dp *intel_dp = container_of(to_delayed_work(__work),
						 struct intel_dp, panel_vdd_work);
	struct drm_device *dev = intel_dp_to_dev(intel_dp);

	mutex_lock(&dev->mode_config.mutex);
	ironlake_panel_vdd_off_sync(intel_dp);
	mutex_unlock(&dev->mode_config.mutex);
}

void ironlake_edp_panel_vdd_off(struct intel_dp *intel_dp, bool sync)
{
	if (!is_edp(intel_dp))
		return;

	DRM_DEBUG_KMS("Turn eDP VDD off %d\n", intel_dp->want_panel_vdd);
	WARN(!intel_dp->want_panel_vdd, "eDP VDD not forced on");

	intel_dp->want_panel_vdd = false;

	if (sync) {
		ironlake_panel_vdd_off_sync(intel_dp);
	} else {
		/*
		 * Queue the timer to fire a long
		 * time from now (relative to the power down delay)
		 * to keep the panel power up across a sequence of operations
		 */
		schedule_delayed_work(&intel_dp->panel_vdd_work,
				      msecs_to_jiffies(intel_dp->panel_power_cycle_delay * 5));
	}
}

void ironlake_edp_panel_on(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp;
	u32 pp_ctrl_reg;

	if (!is_edp(intel_dp))
		return;

	DRM_DEBUG_KMS("Turn eDP power on\n");

	if (ironlake_edp_have_panel_power(intel_dp)) {
		DRM_DEBUG_KMS("eDP power already on\n");
		return;
	}

	ironlake_wait_panel_power_cycle(intel_dp);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
	pp = ironlake_get_pp_control(intel_dp);
	if (IS_GEN5(dev)) {
		/* ILK workaround: disable reset around power sequence */
		pp &= ~PANEL_POWER_RESET;
		I915_WRITE(pp_ctrl_reg, pp);
		POSTING_READ(pp_ctrl_reg);
	}

	pp |= POWER_TARGET_ON;
	if (!IS_GEN5(dev))
		pp |= PANEL_POWER_RESET;

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);

	ironlake_wait_panel_on(intel_dp);

	if (IS_GEN5(dev)) {
		pp |= PANEL_POWER_RESET; /* restore panel reset bit */
		I915_WRITE(pp_ctrl_reg, pp);
		POSTING_READ(pp_ctrl_reg);
	}
}

void ironlake_edp_panel_off(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp;
	u32 pp_ctrl_reg;

	if (!is_edp(intel_dp))
		return;

	DRM_DEBUG_KMS("Turn eDP power off\n");

	WARN(!intel_dp->want_panel_vdd, "Need VDD to turn off panel\n");

	pp = ironlake_get_pp_control(intel_dp);
	/* We need to switch off panel power _and_ force vdd, for otherwise some
	 * panels get very unhappy and cease to work. */
	pp &= ~(POWER_TARGET_ON | EDP_FORCE_VDD | PANEL_POWER_RESET | EDP_BLC_ENABLE);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);

	intel_dp->want_panel_vdd = false;

	ironlake_wait_panel_off(intel_dp);
}

void ironlake_edp_backlight_on(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = to_intel_crtc(intel_dig_port->base.base.crtc)->pipe;
	u32 pp;
	u32 pp_ctrl_reg;

	if (!is_edp(intel_dp))
		return;

	DRM_DEBUG_KMS("\n");
	/*
	 * If we enable the backlight right away following a panel power
	 * on, we may see slight flicker as the panel syncs with the eDP
	 * link.  So delay a bit to make sure the image is solid before
	 * allowing it to appear.
	 */
	msleep(intel_dp->backlight_on_delay);
	pp = ironlake_get_pp_control(intel_dp);
	pp |= EDP_BLC_ENABLE;

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);

	intel_panel_enable_backlight(dev, pipe);
}

void ironlake_edp_backlight_off(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp;
	u32 pp_ctrl_reg;

	if (!is_edp(intel_dp))
		return;

	intel_panel_disable_backlight(dev);

	DRM_DEBUG_KMS("\n");
	pp = ironlake_get_pp_control(intel_dp);
	pp &= ~EDP_BLC_ENABLE;

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);
	msleep(intel_dp->backlight_off_delay);
}

static void ironlake_edp_pll_on(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_crtc *crtc = intel_dig_port->base.base.crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpa_ctl;

	assert_pipe_disabled(dev_priv,
			     to_intel_crtc(crtc)->pipe);

	DRM_DEBUG_KMS("\n");
	dpa_ctl = I915_READ(DP_A);
	WARN(dpa_ctl & DP_PLL_ENABLE, "dp pll on, should be off\n");
	WARN(dpa_ctl & DP_PORT_EN, "dp port still on, should be off\n");

	/* We don't adjust intel_dp->DP while tearing down the link, to
	 * facilitate link retraining (e.g. after hotplug). Hence clear all
	 * enable bits here to ensure that we don't enable too much. */
	intel_dp->DP &= ~(DP_PORT_EN | DP_AUDIO_OUTPUT_ENABLE);
	intel_dp->DP |= DP_PLL_ENABLE;
	I915_WRITE(DP_A, intel_dp->DP);
	POSTING_READ(DP_A);
	udelay(200);
}

static void ironlake_edp_pll_off(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_crtc *crtc = intel_dig_port->base.base.crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 dpa_ctl;

	assert_pipe_disabled(dev_priv,
			     to_intel_crtc(crtc)->pipe);

	dpa_ctl = I915_READ(DP_A);
	WARN((dpa_ctl & DP_PLL_ENABLE) == 0,
	     "dp pll off, should be on\n");
	WARN(dpa_ctl & DP_PORT_EN, "dp port still on, should be off\n");

	/* We can't rely on the value tracked for the DP register in
	 * intel_dp->DP because link_down must not change that (otherwise link
	 * re-training will fail. */
	dpa_ctl &= ~DP_PLL_ENABLE;
	I915_WRITE(DP_A, dpa_ctl);
	POSTING_READ(DP_A);
	udelay(200);
}

/* If the sink supports it, try to set the power state appropriately */
void intel_dp_sink_dpms(struct intel_dp *intel_dp, int mode)
{
	int ret, i;

	/* Should have a valid DPCD by this point */
	if (intel_dp->dpcd[DP_DPCD_REV] < 0x11)
		return;

	if (mode != DRM_MODE_DPMS_ON) {
		ret = intel_dp_aux_native_write_1(intel_dp, DP_SET_POWER,
						  DP_SET_POWER_D3);
		if (ret != 1)
			DRM_DEBUG_DRIVER("failed to write sink power state\n");
	} else {
		/*
		 * When turning on, we need to retry for 1ms to give the sink
		 * time to wake up.
		 */
		for (i = 0; i < 3; i++) {
			ret = intel_dp_aux_native_write_1(intel_dp,
							  DP_SET_POWER,
							  DP_SET_POWER_D0);
			if (ret == 1)
				break;
			msleep(1);
		}
	}
}

static bool intel_dp_get_hw_state(struct intel_encoder *encoder,
				  enum pipe *pipe)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = dp_to_dig_port(intel_dp)->port;
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 tmp = I915_READ(intel_dp->output_reg);

	if (!(tmp & DP_PORT_EN))
		return false;

	if (port == PORT_A && IS_GEN7(dev) && !IS_VALLEYVIEW(dev)) {
		*pipe = PORT_TO_PIPE_CPT(tmp);
	} else if (!HAS_PCH_CPT(dev) || port == PORT_A) {
		*pipe = PORT_TO_PIPE(tmp);
	} else {
		u32 trans_sel;
		u32 trans_dp;
		int i;

		switch (intel_dp->output_reg) {
		case PCH_DP_B:
			trans_sel = TRANS_DP_PORT_SEL_B;
			break;
		case PCH_DP_C:
			trans_sel = TRANS_DP_PORT_SEL_C;
			break;
		case PCH_DP_D:
			trans_sel = TRANS_DP_PORT_SEL_D;
			break;
		default:
			return true;
		}

		for_each_pipe(i) {
			trans_dp = I915_READ(TRANS_DP_CTL(i));
			if ((trans_dp & TRANS_DP_PORT_SEL_MASK) == trans_sel) {
				*pipe = i;
				return true;
			}
		}

		DRM_DEBUG_KMS("No pipe for dp port 0x%x found\n",
			      intel_dp->output_reg);
	}

	return true;
}

static void intel_dp_get_config(struct intel_encoder *encoder,
				struct intel_crtc_config *pipe_config)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	u32 tmp, flags = 0;
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = dp_to_dig_port(intel_dp)->port;
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);
	int dotclock;

	if ((port == PORT_A) || !HAS_PCH_CPT(dev)) {
		tmp = I915_READ(intel_dp->output_reg);
		if (tmp & DP_SYNC_HS_HIGH)
			flags |= DRM_MODE_FLAG_PHSYNC;
		else
			flags |= DRM_MODE_FLAG_NHSYNC;

		if (tmp & DP_SYNC_VS_HIGH)
			flags |= DRM_MODE_FLAG_PVSYNC;
		else
			flags |= DRM_MODE_FLAG_NVSYNC;
	} else {
		tmp = I915_READ(TRANS_DP_CTL(crtc->pipe));
		if (tmp & TRANS_DP_HSYNC_ACTIVE_HIGH)
			flags |= DRM_MODE_FLAG_PHSYNC;
		else
			flags |= DRM_MODE_FLAG_NHSYNC;

		if (tmp & TRANS_DP_VSYNC_ACTIVE_HIGH)
			flags |= DRM_MODE_FLAG_PVSYNC;
		else
			flags |= DRM_MODE_FLAG_NVSYNC;
	}

	pipe_config->adjusted_mode.flags |= flags;

	pipe_config->has_dp_encoder = true;

	intel_dp_get_m_n(crtc, pipe_config);

	if (port == PORT_A) {
		if ((I915_READ(DP_A) & DP_PLL_FREQ_MASK) == DP_PLL_FREQ_160MHZ)
			pipe_config->port_clock = 162000;
		else
			pipe_config->port_clock = 270000;
	}

	dotclock = intel_dotclock_calculate(pipe_config->port_clock,
					    &pipe_config->dp_m_n);

	if (HAS_PCH_SPLIT(dev_priv->dev) && port != PORT_A)
		ironlake_check_encoder_dotclock(pipe_config, dotclock);

	pipe_config->adjusted_mode.clock = dotclock;
}

static bool is_edp_psr(struct intel_dp *intel_dp)
{
	return is_edp(intel_dp) &&
		intel_dp->psr_dpcd[0] & DP_PSR_IS_SUPPORTED;
}

static bool intel_edp_is_psr_enabled(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!HAS_PSR(dev))
		return false;

	return I915_READ(EDP_PSR_CTL(dev)) & EDP_PSR_ENABLE;
}

static void intel_edp_psr_write_vsc(struct intel_dp *intel_dp,
				    struct edp_vsc_psr *vsc_psr)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc = to_intel_crtc(dig_port->base.base.crtc);
	u32 ctl_reg = HSW_TVIDEO_DIP_CTL(crtc->config.cpu_transcoder);
	u32 data_reg = HSW_TVIDEO_DIP_VSC_DATA(crtc->config.cpu_transcoder);
	uint32_t *data = (uint32_t *) vsc_psr;
	unsigned int i;

	/* As per BSPec (Pipe Video Data Island Packet), we need to disable
	   the video DIP being updated before program video DIP data buffer
	   registers for DIP being updated. */
	I915_WRITE(ctl_reg, 0);
	POSTING_READ(ctl_reg);

	for (i = 0; i < VIDEO_DIP_VSC_DATA_SIZE; i += 4) {
		if (i < sizeof(struct edp_vsc_psr))
			I915_WRITE(data_reg + i, *data++);
		else
			I915_WRITE(data_reg + i, 0);
	}

	I915_WRITE(ctl_reg, VIDEO_DIP_ENABLE_VSC_HSW);
	POSTING_READ(ctl_reg);
}

static void intel_edp_psr_setup(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct edp_vsc_psr psr_vsc;

	if (intel_dp->psr_setup_done)
		return;

	/* Prepare VSC packet as per EDP 1.3 spec, Table 3.10 */
	memset(&psr_vsc, 0, sizeof(psr_vsc));
	psr_vsc.sdp_header.HB0 = 0;
	psr_vsc.sdp_header.HB1 = 0x7;
	psr_vsc.sdp_header.HB2 = 0x2;
	psr_vsc.sdp_header.HB3 = 0x8;
	intel_edp_psr_write_vsc(intel_dp, &psr_vsc);

	/* Avoid continuous PSR exit by masking memup and hpd */
	I915_WRITE(EDP_PSR_DEBUG_CTL(dev), EDP_PSR_DEBUG_MASK_MEMUP |
		   EDP_PSR_DEBUG_MASK_HPD);

	intel_dp->psr_setup_done = true;
}

static void intel_edp_psr_enable_sink(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t aux_clock_divider = get_aux_clock_divider(intel_dp, 0);
	int precharge = 0x3;
	int msg_size = 5;       /* Header(4) + Message(1) */

	/* Enable PSR in sink */
	if (intel_dp->psr_dpcd[1] & DP_PSR_NO_TRAIN_ON_EXIT)
		intel_dp_aux_native_write_1(intel_dp, DP_PSR_EN_CFG,
					    DP_PSR_ENABLE &
					    ~DP_PSR_MAIN_LINK_ACTIVE);
	else
		intel_dp_aux_native_write_1(intel_dp, DP_PSR_EN_CFG,
					    DP_PSR_ENABLE |
					    DP_PSR_MAIN_LINK_ACTIVE);

	/* Setup AUX registers */
	I915_WRITE(EDP_PSR_AUX_DATA1(dev), EDP_PSR_DPCD_COMMAND);
	I915_WRITE(EDP_PSR_AUX_DATA2(dev), EDP_PSR_DPCD_NORMAL_OPERATION);
	I915_WRITE(EDP_PSR_AUX_CTL(dev),
		   DP_AUX_CH_CTL_TIME_OUT_400us |
		   (msg_size << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
		   (precharge << DP_AUX_CH_CTL_PRECHARGE_2US_SHIFT) |
		   (aux_clock_divider << DP_AUX_CH_CTL_BIT_CLOCK_2X_SHIFT));
}

static void intel_edp_psr_enable_source(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t max_sleep_time = 0x1f;
	uint32_t idle_frames = 1;
	uint32_t val = 0x0;

	if (intel_dp->psr_dpcd[1] & DP_PSR_NO_TRAIN_ON_EXIT) {
		val |= EDP_PSR_LINK_STANDBY;
		val |= EDP_PSR_TP2_TP3_TIME_0us;
		val |= EDP_PSR_TP1_TIME_0us;
		val |= EDP_PSR_SKIP_AUX_EXIT;
	} else
		val |= EDP_PSR_LINK_DISABLE;

	I915_WRITE(EDP_PSR_CTL(dev), val |
		   EDP_PSR_MIN_LINK_ENTRY_TIME_8_LINES |
		   max_sleep_time << EDP_PSR_MAX_SLEEP_TIME_SHIFT |
		   idle_frames << EDP_PSR_IDLE_FRAME_SHIFT |
		   EDP_PSR_ENABLE);
}

static bool intel_edp_psr_match_conditions(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = dig_port->base.base.crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_gem_object *obj = to_intel_framebuffer(crtc->fb)->obj;
	struct intel_encoder *intel_encoder = &dp_to_dig_port(intel_dp)->base;

	if (!HAS_PSR(dev)) {
		DRM_DEBUG_KMS("PSR not supported on this platform\n");
		dev_priv->no_psr_reason = PSR_NO_SOURCE;
		return false;
	}

	if ((intel_encoder->type != INTEL_OUTPUT_EDP) ||
	    (dig_port->port != PORT_A)) {
		DRM_DEBUG_KMS("HSW ties PSR to DDI A (eDP)\n");
		dev_priv->no_psr_reason = PSR_HSW_NOT_DDIA;
		return false;
	}

	if (!is_edp_psr(intel_dp)) {
		DRM_DEBUG_KMS("PSR not supported by this panel\n");
		dev_priv->no_psr_reason = PSR_NO_SINK;
		return false;
	}

	if (!i915_enable_psr) {
		DRM_DEBUG_KMS("PSR disable by flag\n");
		dev_priv->no_psr_reason = PSR_MODULE_PARAM;
		return false;
	}

	crtc = dig_port->base.base.crtc;
	if (crtc == NULL) {
		DRM_DEBUG_KMS("crtc not active for PSR\n");
		dev_priv->no_psr_reason = PSR_CRTC_NOT_ACTIVE;
		return false;
	}

	intel_crtc = to_intel_crtc(crtc);
	if (!intel_crtc_active(crtc)) {
		DRM_DEBUG_KMS("crtc not active for PSR\n");
		dev_priv->no_psr_reason = PSR_CRTC_NOT_ACTIVE;
		return false;
	}

	obj = to_intel_framebuffer(crtc->fb)->obj;
	if (obj->tiling_mode != I915_TILING_X ||
	    obj->fence_reg == I915_FENCE_REG_NONE) {
		DRM_DEBUG_KMS("PSR condition failed: fb not tiled or fenced\n");
		dev_priv->no_psr_reason = PSR_NOT_TILED;
		return false;
	}

	if (I915_READ(SPRCTL(intel_crtc->pipe)) & SPRITE_ENABLE) {
		DRM_DEBUG_KMS("PSR condition failed: Sprite is Enabled\n");
		dev_priv->no_psr_reason = PSR_SPRITE_ENABLED;
		return false;
	}

	if (I915_READ(HSW_STEREO_3D_CTL(intel_crtc->config.cpu_transcoder)) &
	    S3D_ENABLE) {
		DRM_DEBUG_KMS("PSR condition failed: Stereo 3D is Enabled\n");
		dev_priv->no_psr_reason = PSR_S3D_ENABLED;
		return false;
	}

	if (intel_crtc->config.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE) {
		DRM_DEBUG_KMS("PSR condition failed: Interlaced is Enabled\n");
		dev_priv->no_psr_reason = PSR_INTERLACED_ENABLED;
		return false;
	}

	return true;
}

static void intel_edp_psr_do_enable(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);

	if (!intel_edp_psr_match_conditions(intel_dp) ||
	    intel_edp_is_psr_enabled(dev))
		return;

	/* Setup PSR once */
	intel_edp_psr_setup(intel_dp);

	/* Enable PSR on the panel */
	intel_edp_psr_enable_sink(intel_dp);

	/* Enable PSR on the host */
	intel_edp_psr_enable_source(intel_dp);
}

void intel_edp_psr_enable(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);

	if (intel_edp_psr_match_conditions(intel_dp) &&
	    !intel_edp_is_psr_enabled(dev))
		intel_edp_psr_do_enable(intel_dp);
}

void intel_edp_psr_disable(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!intel_edp_is_psr_enabled(dev))
		return;

	I915_WRITE(EDP_PSR_CTL(dev),
		   I915_READ(EDP_PSR_CTL(dev)) & ~EDP_PSR_ENABLE);

	/* Wait till PSR is idle */
	if (_wait_for((I915_READ(EDP_PSR_STATUS_CTL(dev)) &
		       EDP_PSR_STATUS_STATE_MASK) == 0, 2000, 10))
		DRM_ERROR("Timed out waiting for PSR Idle State\n");
}

void intel_edp_psr_update(struct drm_device *dev)
{
	struct intel_encoder *encoder;
	struct intel_dp *intel_dp = NULL;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, base.head)
		if (encoder->type == INTEL_OUTPUT_EDP) {
			intel_dp = enc_to_intel_dp(&encoder->base);

			if (!is_edp_psr(intel_dp))
				return;

			if (!intel_edp_psr_match_conditions(intel_dp))
				intel_edp_psr_disable(intel_dp);
			else
				if (!intel_edp_is_psr_enabled(dev))
					intel_edp_psr_do_enable(intel_dp);
		}
}

static void intel_disable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = dp_to_dig_port(intel_dp)->port;
	struct drm_device *dev = encoder->base.dev;

	/* Make sure the panel is off before trying to change the mode. But also
	 * ensure that we have vdd while we switch off the panel. */
	ironlake_edp_panel_vdd_on(intel_dp);
	ironlake_edp_backlight_off(intel_dp);
	intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_ON);
	ironlake_edp_panel_off(intel_dp);

	/* cpu edp my only be disable _after_ the cpu pipe/plane is disabled. */
	if (!(port == PORT_A || IS_VALLEYVIEW(dev)))
		intel_dp_link_down(intel_dp);
}

static void intel_post_disable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = dp_to_dig_port(intel_dp)->port;
	struct drm_device *dev = encoder->base.dev;

	if (port == PORT_A || IS_VALLEYVIEW(dev)) {
		intel_dp_link_down(intel_dp);
		if (!IS_VALLEYVIEW(dev))
			ironlake_edp_pll_off(intel_dp);
	}
}

static void intel_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t dp_reg = I915_READ(intel_dp->output_reg);

	if (WARN_ON(dp_reg & DP_PORT_EN))
		return;

	ironlake_edp_panel_vdd_on(intel_dp);
	intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_ON);
	intel_dp_start_link_train(intel_dp);
	ironlake_edp_panel_on(intel_dp);
	ironlake_edp_panel_vdd_off(intel_dp, true);
	intel_dp_complete_link_train(intel_dp);
	intel_dp_stop_link_train(intel_dp);
}

static void g4x_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);

	intel_enable_dp(encoder);
	ironlake_edp_backlight_on(intel_dp);
}

static void vlv_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);

	ironlake_edp_backlight_on(intel_dp);
}

static void g4x_pre_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);

	if (dport->port == PORT_A)
		ironlake_edp_pll_on(intel_dp);
}

static void vlv_pre_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	int port = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;
	struct edp_power_seq power_seq;
	u32 val;

	mutex_lock(&dev_priv->dpio_lock);

	val = vlv_dpio_read(dev_priv, pipe, DPIO_DATA_LANE_A(port));
	val = 0;
	if (pipe)
		val |= (1<<21);
	else
		val &= ~(1<<21);
	val |= 0x001000c4;
	vlv_dpio_write(dev_priv, pipe, DPIO_DATA_CHANNEL(port), val);
	vlv_dpio_write(dev_priv, pipe, DPIO_PCS_CLOCKBUF0(port), 0x00760018);
	vlv_dpio_write(dev_priv, pipe, DPIO_PCS_CLOCKBUF8(port), 0x00400888);

	mutex_unlock(&dev_priv->dpio_lock);

	/* init power sequencer on this pipe and port */
	intel_dp_init_panel_power_sequencer(dev, intel_dp, &power_seq);
	intel_dp_init_panel_power_sequencer_registers(dev, intel_dp,
						      &power_seq);

	intel_enable_dp(encoder);

	vlv_wait_port_ready(dev_priv, port);
}

static void vlv_dp_pre_pll_enable(struct intel_encoder *encoder)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc =
		to_intel_crtc(encoder->base.crtc);
	int port = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;

	/* Program Tx lane resets to default */
	mutex_lock(&dev_priv->dpio_lock);
	vlv_dpio_write(dev_priv, pipe, DPIO_PCS_TX(port),
			 DPIO_PCS_TX_LANE2_RESET |
			 DPIO_PCS_TX_LANE1_RESET);
	vlv_dpio_write(dev_priv, pipe, DPIO_PCS_CLK(port),
			 DPIO_PCS_CLK_CRI_RXEB_EIOS_EN |
			 DPIO_PCS_CLK_CRI_RXDIGFILTSG_EN |
			 (1<<DPIO_PCS_CLK_DATAWIDTH_SHIFT) |
				 DPIO_PCS_CLK_SOFT_RESET);

	/* Fix up inter-pair skew failure */
	vlv_dpio_write(dev_priv, pipe, DPIO_PCS_STAGGER1(port), 0x00750f00);
	vlv_dpio_write(dev_priv, pipe, DPIO_TX_CTL(port), 0x00001500);
	vlv_dpio_write(dev_priv, pipe, DPIO_TX_LANE(port), 0x40400000);
	mutex_unlock(&dev_priv->dpio_lock);
}

/*
 * Native read with retry for link status and receiver capability reads for
 * cases where the sink may still be asleep.
 */
static bool
intel_dp_aux_native_read_retry(struct intel_dp *intel_dp, uint16_t address,
			       uint8_t *recv, int recv_bytes)
{
	int ret, i;

	/*
	 * Sinks are *supposed* to come up within 1ms from an off state,
	 * but we're also supposed to retry 3 times per the spec.
	 */
	for (i = 0; i < 3; i++) {
		ret = intel_dp_aux_native_read(intel_dp, address, recv,
					       recv_bytes);
		if (ret == recv_bytes)
			return true;
		msleep(1);
	}

	return false;
}

/*
 * Fetch AUX CH registers 0x202 - 0x207 which contain
 * link status information
 */
static bool
intel_dp_get_link_status(struct intel_dp *intel_dp, uint8_t link_status[DP_LINK_STATUS_SIZE])
{
	return intel_dp_aux_native_read_retry(intel_dp,
					      DP_LANE0_1_STATUS,
					      link_status,
					      DP_LINK_STATUS_SIZE);
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

static uint8_t
intel_dp_voltage_max(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	enum port port = dp_to_dig_port(intel_dp)->port;

	if (IS_VALLEYVIEW(dev))
		return DP_TRAIN_VOLTAGE_SWING_1200;
	else if (IS_GEN7(dev) && port == PORT_A)
		return DP_TRAIN_VOLTAGE_SWING_800;
	else if (HAS_PCH_CPT(dev) && port != PORT_A)
		return DP_TRAIN_VOLTAGE_SWING_1200;
	else
		return DP_TRAIN_VOLTAGE_SWING_800;
}

static uint8_t
intel_dp_pre_emphasis_max(struct intel_dp *intel_dp, uint8_t voltage_swing)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	enum port port = dp_to_dig_port(intel_dp)->port;

	if (HAS_DDI(dev)) {
		switch (voltage_swing & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_400:
			return DP_TRAIN_PRE_EMPHASIS_9_5;
		case DP_TRAIN_VOLTAGE_SWING_600:
			return DP_TRAIN_PRE_EMPHASIS_6;
		case DP_TRAIN_VOLTAGE_SWING_800:
			return DP_TRAIN_PRE_EMPHASIS_3_5;
		case DP_TRAIN_VOLTAGE_SWING_1200:
		default:
			return DP_TRAIN_PRE_EMPHASIS_0;
		}
	} else if (IS_VALLEYVIEW(dev)) {
		switch (voltage_swing & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_400:
			return DP_TRAIN_PRE_EMPHASIS_9_5;
		case DP_TRAIN_VOLTAGE_SWING_600:
			return DP_TRAIN_PRE_EMPHASIS_6;
		case DP_TRAIN_VOLTAGE_SWING_800:
			return DP_TRAIN_PRE_EMPHASIS_3_5;
		case DP_TRAIN_VOLTAGE_SWING_1200:
		default:
			return DP_TRAIN_PRE_EMPHASIS_0;
		}
	} else if (IS_GEN7(dev) && port == PORT_A) {
		switch (voltage_swing & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_400:
			return DP_TRAIN_PRE_EMPHASIS_6;
		case DP_TRAIN_VOLTAGE_SWING_600:
		case DP_TRAIN_VOLTAGE_SWING_800:
			return DP_TRAIN_PRE_EMPHASIS_3_5;
		default:
			return DP_TRAIN_PRE_EMPHASIS_0;
		}
	} else {
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
}

static uint32_t intel_vlv_signal_levels(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);
	struct intel_crtc *intel_crtc =
		to_intel_crtc(dport->base.base.crtc);
	unsigned long demph_reg_value, preemph_reg_value,
		uniqtranscale_reg_value;
	uint8_t train_set = intel_dp->train_set[0];
	int port = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;

	switch (train_set & DP_TRAIN_PRE_EMPHASIS_MASK) {
	case DP_TRAIN_PRE_EMPHASIS_0:
		preemph_reg_value = 0x0004000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_400:
			demph_reg_value = 0x2B405555;
			uniqtranscale_reg_value = 0x552AB83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_600:
			demph_reg_value = 0x2B404040;
			uniqtranscale_reg_value = 0x5548B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_800:
			demph_reg_value = 0x2B245555;
			uniqtranscale_reg_value = 0x5560B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_1200:
			demph_reg_value = 0x2B405555;
			uniqtranscale_reg_value = 0x5598DA3A;
			break;
		default:
			return 0;
		}
		break;
	case DP_TRAIN_PRE_EMPHASIS_3_5:
		preemph_reg_value = 0x0002000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_400:
			demph_reg_value = 0x2B404040;
			uniqtranscale_reg_value = 0x5552B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_600:
			demph_reg_value = 0x2B404848;
			uniqtranscale_reg_value = 0x5580B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_800:
			demph_reg_value = 0x2B404040;
			uniqtranscale_reg_value = 0x55ADDA3A;
			break;
		default:
			return 0;
		}
		break;
	case DP_TRAIN_PRE_EMPHASIS_6:
		preemph_reg_value = 0x0000000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_400:
			demph_reg_value = 0x2B305555;
			uniqtranscale_reg_value = 0x5570B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_600:
			demph_reg_value = 0x2B2B4040;
			uniqtranscale_reg_value = 0x55ADDA3A;
			break;
		default:
			return 0;
		}
		break;
	case DP_TRAIN_PRE_EMPHASIS_9_5:
		preemph_reg_value = 0x0006000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_400:
			demph_reg_value = 0x1B405555;
			uniqtranscale_reg_value = 0x55ADDA3A;
			break;
		default:
			return 0;
		}
		break;
	default:
		return 0;
	}

	mutex_lock(&dev_priv->dpio_lock);
	vlv_dpio_write(dev_priv, pipe, DPIO_TX_OCALINIT(port), 0x00000000);
	vlv_dpio_write(dev_priv, pipe, DPIO_TX_SWING_CTL4(port), demph_reg_value);
	vlv_dpio_write(dev_priv, pipe, DPIO_TX_SWING_CTL2(port),
			 uniqtranscale_reg_value);
	vlv_dpio_write(dev_priv, pipe, DPIO_TX_SWING_CTL3(port), 0x0C782040);
	vlv_dpio_write(dev_priv, pipe, DPIO_PCS_STAGGER0(port), 0x00030000);
	vlv_dpio_write(dev_priv, pipe, DPIO_PCS_CTL_OVER1(port), preemph_reg_value);
	vlv_dpio_write(dev_priv, pipe, DPIO_TX_OCALINIT(port), 0x80000000);
	mutex_unlock(&dev_priv->dpio_lock);

	return 0;
}

static void
intel_get_adjust_train(struct intel_dp *intel_dp, uint8_t link_status[DP_LINK_STATUS_SIZE])
{
	uint8_t v = 0;
	uint8_t p = 0;
	int lane;
	uint8_t voltage_max;
	uint8_t preemph_max;

	for (lane = 0; lane < intel_dp->lane_count; lane++) {
		uint8_t this_v = drm_dp_get_adjust_request_voltage(link_status, lane);
		uint8_t this_p = drm_dp_get_adjust_request_pre_emphasis(link_status, lane);

		if (this_v > v)
			v = this_v;
		if (this_p > p)
			p = this_p;
	}

	voltage_max = intel_dp_voltage_max(intel_dp);
	if (v >= voltage_max)
		v = voltage_max | DP_TRAIN_MAX_SWING_REACHED;

	preemph_max = intel_dp_pre_emphasis_max(intel_dp, v);
	if (p >= preemph_max)
		p = preemph_max | DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

	for (lane = 0; lane < 4; lane++)
		intel_dp->train_set[lane] = v | p;
}

static uint32_t
intel_gen4_signal_levels(uint8_t train_set)
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

/* Gen6's DP voltage swing and pre-emphasis control */
static uint32_t
intel_gen6_edp_signal_levels(uint8_t train_set)
{
	int signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
					 DP_TRAIN_PRE_EMPHASIS_MASK);
	switch (signal_levels) {
	case DP_TRAIN_VOLTAGE_SWING_400 | DP_TRAIN_PRE_EMPHASIS_0:
	case DP_TRAIN_VOLTAGE_SWING_600 | DP_TRAIN_PRE_EMPHASIS_0:
		return EDP_LINK_TRAIN_400_600MV_0DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_400 | DP_TRAIN_PRE_EMPHASIS_3_5:
		return EDP_LINK_TRAIN_400MV_3_5DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_400 | DP_TRAIN_PRE_EMPHASIS_6:
	case DP_TRAIN_VOLTAGE_SWING_600 | DP_TRAIN_PRE_EMPHASIS_6:
		return EDP_LINK_TRAIN_400_600MV_6DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_600 | DP_TRAIN_PRE_EMPHASIS_3_5:
	case DP_TRAIN_VOLTAGE_SWING_800 | DP_TRAIN_PRE_EMPHASIS_3_5:
		return EDP_LINK_TRAIN_600_800MV_3_5DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_800 | DP_TRAIN_PRE_EMPHASIS_0:
	case DP_TRAIN_VOLTAGE_SWING_1200 | DP_TRAIN_PRE_EMPHASIS_0:
		return EDP_LINK_TRAIN_800_1200MV_0DB_SNB_B;
	default:
		DRM_DEBUG_KMS("Unsupported voltage swing/pre-emphasis level:"
			      "0x%x\n", signal_levels);
		return EDP_LINK_TRAIN_400_600MV_0DB_SNB_B;
	}
}

/* Gen7's DP voltage swing and pre-emphasis control */
static uint32_t
intel_gen7_edp_signal_levels(uint8_t train_set)
{
	int signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
					 DP_TRAIN_PRE_EMPHASIS_MASK);
	switch (signal_levels) {
	case DP_TRAIN_VOLTAGE_SWING_400 | DP_TRAIN_PRE_EMPHASIS_0:
		return EDP_LINK_TRAIN_400MV_0DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_400 | DP_TRAIN_PRE_EMPHASIS_3_5:
		return EDP_LINK_TRAIN_400MV_3_5DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_400 | DP_TRAIN_PRE_EMPHASIS_6:
		return EDP_LINK_TRAIN_400MV_6DB_IVB;

	case DP_TRAIN_VOLTAGE_SWING_600 | DP_TRAIN_PRE_EMPHASIS_0:
		return EDP_LINK_TRAIN_600MV_0DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_600 | DP_TRAIN_PRE_EMPHASIS_3_5:
		return EDP_LINK_TRAIN_600MV_3_5DB_IVB;

	case DP_TRAIN_VOLTAGE_SWING_800 | DP_TRAIN_PRE_EMPHASIS_0:
		return EDP_LINK_TRAIN_800MV_0DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_800 | DP_TRAIN_PRE_EMPHASIS_3_5:
		return EDP_LINK_TRAIN_800MV_3_5DB_IVB;

	default:
		DRM_DEBUG_KMS("Unsupported voltage swing/pre-emphasis level:"
			      "0x%x\n", signal_levels);
		return EDP_LINK_TRAIN_500MV_0DB_IVB;
	}
}

/* Gen7.5's (HSW) DP voltage swing and pre-emphasis control */
static uint32_t
intel_hsw_signal_levels(uint8_t train_set)
{
	int signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
					 DP_TRAIN_PRE_EMPHASIS_MASK);
	switch (signal_levels) {
	case DP_TRAIN_VOLTAGE_SWING_400 | DP_TRAIN_PRE_EMPHASIS_0:
		return DDI_BUF_EMP_400MV_0DB_HSW;
	case DP_TRAIN_VOLTAGE_SWING_400 | DP_TRAIN_PRE_EMPHASIS_3_5:
		return DDI_BUF_EMP_400MV_3_5DB_HSW;
	case DP_TRAIN_VOLTAGE_SWING_400 | DP_TRAIN_PRE_EMPHASIS_6:
		return DDI_BUF_EMP_400MV_6DB_HSW;
	case DP_TRAIN_VOLTAGE_SWING_400 | DP_TRAIN_PRE_EMPHASIS_9_5:
		return DDI_BUF_EMP_400MV_9_5DB_HSW;

	case DP_TRAIN_VOLTAGE_SWING_600 | DP_TRAIN_PRE_EMPHASIS_0:
		return DDI_BUF_EMP_600MV_0DB_HSW;
	case DP_TRAIN_VOLTAGE_SWING_600 | DP_TRAIN_PRE_EMPHASIS_3_5:
		return DDI_BUF_EMP_600MV_3_5DB_HSW;
	case DP_TRAIN_VOLTAGE_SWING_600 | DP_TRAIN_PRE_EMPHASIS_6:
		return DDI_BUF_EMP_600MV_6DB_HSW;

	case DP_TRAIN_VOLTAGE_SWING_800 | DP_TRAIN_PRE_EMPHASIS_0:
		return DDI_BUF_EMP_800MV_0DB_HSW;
	case DP_TRAIN_VOLTAGE_SWING_800 | DP_TRAIN_PRE_EMPHASIS_3_5:
		return DDI_BUF_EMP_800MV_3_5DB_HSW;
	default:
		DRM_DEBUG_KMS("Unsupported voltage swing/pre-emphasis level:"
			      "0x%x\n", signal_levels);
		return DDI_BUF_EMP_400MV_0DB_HSW;
	}
}

/* Properly updates "DP" with the correct signal levels. */
static void
intel_dp_set_signal_levels(struct intel_dp *intel_dp, uint32_t *DP)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	enum port port = intel_dig_port->port;
	struct drm_device *dev = intel_dig_port->base.base.dev;
	uint32_t signal_levels, mask;
	uint8_t train_set = intel_dp->train_set[0];

	if (HAS_DDI(dev)) {
		signal_levels = intel_hsw_signal_levels(train_set);
		mask = DDI_BUF_EMP_MASK;
	} else if (IS_VALLEYVIEW(dev)) {
		signal_levels = intel_vlv_signal_levels(intel_dp);
		mask = 0;
	} else if (IS_GEN7(dev) && port == PORT_A) {
		signal_levels = intel_gen7_edp_signal_levels(train_set);
		mask = EDP_LINK_TRAIN_VOL_EMP_MASK_IVB;
	} else if (IS_GEN6(dev) && port == PORT_A) {
		signal_levels = intel_gen6_edp_signal_levels(train_set);
		mask = EDP_LINK_TRAIN_VOL_EMP_MASK_SNB;
	} else {
		signal_levels = intel_gen4_signal_levels(train_set);
		mask = DP_VOLTAGE_MASK | DP_PRE_EMPHASIS_MASK;
	}

	DRM_DEBUG_KMS("Using signal levels %08x\n", signal_levels);

	*DP = (*DP & ~mask) | signal_levels;
}

static bool
intel_dp_set_link_train(struct intel_dp *intel_dp,
			uint32_t dp_reg_value,
			uint8_t dp_train_pat)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_dig_port->port;
	int ret;

	if (HAS_DDI(dev)) {
		uint32_t temp = I915_READ(DP_TP_CTL(port));

		if (dp_train_pat & DP_LINK_SCRAMBLING_DISABLE)
			temp |= DP_TP_CTL_SCRAMBLE_DISABLE;
		else
			temp &= ~DP_TP_CTL_SCRAMBLE_DISABLE;

		temp &= ~DP_TP_CTL_LINK_TRAIN_MASK;
		switch (dp_train_pat & DP_TRAINING_PATTERN_MASK) {
		case DP_TRAINING_PATTERN_DISABLE:
			temp |= DP_TP_CTL_LINK_TRAIN_NORMAL;

			break;
		case DP_TRAINING_PATTERN_1:
			temp |= DP_TP_CTL_LINK_TRAIN_PAT1;
			break;
		case DP_TRAINING_PATTERN_2:
			temp |= DP_TP_CTL_LINK_TRAIN_PAT2;
			break;
		case DP_TRAINING_PATTERN_3:
			temp |= DP_TP_CTL_LINK_TRAIN_PAT3;
			break;
		}
		I915_WRITE(DP_TP_CTL(port), temp);

	} else if (HAS_PCH_CPT(dev) && (IS_GEN7(dev) || port != PORT_A)) {
		dp_reg_value &= ~DP_LINK_TRAIN_MASK_CPT;

		switch (dp_train_pat & DP_TRAINING_PATTERN_MASK) {
		case DP_TRAINING_PATTERN_DISABLE:
			dp_reg_value |= DP_LINK_TRAIN_OFF_CPT;
			break;
		case DP_TRAINING_PATTERN_1:
			dp_reg_value |= DP_LINK_TRAIN_PAT_1_CPT;
			break;
		case DP_TRAINING_PATTERN_2:
			dp_reg_value |= DP_LINK_TRAIN_PAT_2_CPT;
			break;
		case DP_TRAINING_PATTERN_3:
			DRM_ERROR("DP training pattern 3 not supported\n");
			dp_reg_value |= DP_LINK_TRAIN_PAT_2_CPT;
			break;
		}

	} else {
		dp_reg_value &= ~DP_LINK_TRAIN_MASK;

		switch (dp_train_pat & DP_TRAINING_PATTERN_MASK) {
		case DP_TRAINING_PATTERN_DISABLE:
			dp_reg_value |= DP_LINK_TRAIN_OFF;
			break;
		case DP_TRAINING_PATTERN_1:
			dp_reg_value |= DP_LINK_TRAIN_PAT_1;
			break;
		case DP_TRAINING_PATTERN_2:
			dp_reg_value |= DP_LINK_TRAIN_PAT_2;
			break;
		case DP_TRAINING_PATTERN_3:
			DRM_ERROR("DP training pattern 3 not supported\n");
			dp_reg_value |= DP_LINK_TRAIN_PAT_2;
			break;
		}
	}

	I915_WRITE(intel_dp->output_reg, dp_reg_value);
	POSTING_READ(intel_dp->output_reg);

	intel_dp_aux_native_write_1(intel_dp,
				    DP_TRAINING_PATTERN_SET,
				    dp_train_pat);

	if ((dp_train_pat & DP_TRAINING_PATTERN_MASK) !=
	    DP_TRAINING_PATTERN_DISABLE) {
		ret = intel_dp_aux_native_write(intel_dp,
						DP_TRAINING_LANE0_SET,
						intel_dp->train_set,
						intel_dp->lane_count);
		if (ret != intel_dp->lane_count)
			return false;
	}

	return true;
}

static void intel_dp_set_idle_link_train(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_dig_port->port;
	uint32_t val;

	if (!HAS_DDI(dev))
		return;

	val = I915_READ(DP_TP_CTL(port));
	val &= ~DP_TP_CTL_LINK_TRAIN_MASK;
	val |= DP_TP_CTL_LINK_TRAIN_IDLE;
	I915_WRITE(DP_TP_CTL(port), val);

	/*
	 * On PORT_A we can have only eDP in SST mode. There the only reason
	 * we need to set idle transmission mode is to work around a HW issue
	 * where we enable the pipe while not in idle link-training mode.
	 * In this case there is requirement to wait for a minimum number of
	 * idle patterns to be sent.
	 */
	if (port == PORT_A)
		return;

	if (wait_for((I915_READ(DP_TP_STATUS(port)) & DP_TP_STATUS_IDLE_DONE),
		     1))
		DRM_ERROR("Timed out waiting for DP idle patterns\n");
}

/* Enable corresponding port and start training pattern 1 */
void
intel_dp_start_link_train(struct intel_dp *intel_dp)
{
	struct drm_encoder *encoder = &dp_to_dig_port(intel_dp)->base.base;
	struct drm_device *dev = encoder->dev;
	int i;
	uint8_t voltage;
	int voltage_tries, loop_tries;
	uint32_t DP = intel_dp->DP;

	if (HAS_DDI(dev))
		intel_ddi_prepare_link_retrain(encoder);

	/* Write the link configuration data */
	intel_dp_aux_native_write(intel_dp, DP_LINK_BW_SET,
				  intel_dp->link_configuration,
				  DP_LINK_CONFIGURATION_SIZE);

	DP |= DP_PORT_EN;

	memset(intel_dp->train_set, 0, 4);
	voltage = 0xff;
	voltage_tries = 0;
	loop_tries = 0;
	for (;;) {
		/* Use intel_dp->train_set[0] to set the voltage and pre emphasis values */
		uint8_t	    link_status[DP_LINK_STATUS_SIZE];

		intel_dp_set_signal_levels(intel_dp, &DP);

		/* Set training pattern 1 */
		if (!intel_dp_set_link_train(intel_dp, DP,
					     DP_TRAINING_PATTERN_1 |
					     DP_LINK_SCRAMBLING_DISABLE))
			break;

		drm_dp_link_train_clock_recovery_delay(intel_dp->dpcd);
		if (!intel_dp_get_link_status(intel_dp, link_status)) {
			DRM_ERROR("failed to get link status\n");
			break;
		}

		if (drm_dp_clock_recovery_ok(link_status, intel_dp->lane_count)) {
			DRM_DEBUG_KMS("clock recovery OK\n");
			break;
		}

		/* Check to see if we've tried the max voltage */
		for (i = 0; i < intel_dp->lane_count; i++)
			if ((intel_dp->train_set[i] & DP_TRAIN_MAX_SWING_REACHED) == 0)
				break;
		if (i == intel_dp->lane_count) {
			++loop_tries;
			if (loop_tries == 5) {
				DRM_DEBUG_KMS("too many full retries, give up\n");
				break;
			}
			memset(intel_dp->train_set, 0, 4);
			voltage_tries = 0;
			continue;
		}

		/* Check to see if we've tried the same voltage 5 times */
		if ((intel_dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK) == voltage) {
			++voltage_tries;
			if (voltage_tries == 5) {
				DRM_DEBUG_KMS("too many voltage retries, give up\n");
				break;
			}
		} else
			voltage_tries = 0;
		voltage = intel_dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK;

		/* Compute new intel_dp->train_set as requested by target */
		intel_get_adjust_train(intel_dp, link_status);
	}

	intel_dp->DP = DP;
}

void
intel_dp_complete_link_train(struct intel_dp *intel_dp)
{
	bool channel_eq = false;
	int tries, cr_tries;
	uint32_t DP = intel_dp->DP;

	/* channel equalization */
	tries = 0;
	cr_tries = 0;
	channel_eq = false;
	for (;;) {
		uint8_t	    link_status[DP_LINK_STATUS_SIZE];

		if (cr_tries > 5) {
			DRM_ERROR("failed to train DP, aborting\n");
			intel_dp_link_down(intel_dp);
			break;
		}

		intel_dp_set_signal_levels(intel_dp, &DP);

		/* channel eq pattern */
		if (!intel_dp_set_link_train(intel_dp, DP,
					     DP_TRAINING_PATTERN_2 |
					     DP_LINK_SCRAMBLING_DISABLE))
			break;

		drm_dp_link_train_channel_eq_delay(intel_dp->dpcd);
		if (!intel_dp_get_link_status(intel_dp, link_status))
			break;

		/* Make sure clock is still ok */
		if (!drm_dp_clock_recovery_ok(link_status, intel_dp->lane_count)) {
			intel_dp_start_link_train(intel_dp);
			cr_tries++;
			continue;
		}

		if (drm_dp_channel_eq_ok(link_status, intel_dp->lane_count)) {
			channel_eq = true;
			break;
		}

		/* Try 5 times, then try clock recovery if that fails */
		if (tries > 5) {
			intel_dp_link_down(intel_dp);
			intel_dp_start_link_train(intel_dp);
			tries = 0;
			cr_tries++;
			continue;
		}

		/* Compute new intel_dp->train_set as requested by target */
		intel_get_adjust_train(intel_dp, link_status);
		++tries;
	}

	intel_dp_set_idle_link_train(intel_dp);

	intel_dp->DP = DP;

	if (channel_eq)
		DRM_DEBUG_KMS("Channel EQ done. DP Training successful\n");

}

void intel_dp_stop_link_train(struct intel_dp *intel_dp)
{
	intel_dp_set_link_train(intel_dp, intel_dp->DP,
				DP_TRAINING_PATTERN_DISABLE);
}

static void
intel_dp_link_down(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	enum port port = intel_dig_port->port;
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc =
		to_intel_crtc(intel_dig_port->base.base.crtc);
	uint32_t DP = intel_dp->DP;

	/*
	 * DDI code has a strict mode set sequence and we should try to respect
	 * it, otherwise we might hang the machine in many different ways. So we
	 * really should be disabling the port only on a complete crtc_disable
	 * sequence. This function is just called under two conditions on DDI
	 * code:
	 * - Link train failed while doing crtc_enable, and on this case we
	 *   really should respect the mode set sequence and wait for a
	 *   crtc_disable.
	 * - Someone turned the monitor off and intel_dp_check_link_status
	 *   called us. We don't need to disable the whole port on this case, so
	 *   when someone turns the monitor on again,
	 *   intel_ddi_prepare_link_retrain will take care of redoing the link
	 *   train.
	 */
	if (HAS_DDI(dev))
		return;

	if (WARN_ON((I915_READ(intel_dp->output_reg) & DP_PORT_EN) == 0))
		return;

	DRM_DEBUG_KMS("\n");

	if (HAS_PCH_CPT(dev) && (IS_GEN7(dev) || port != PORT_A)) {
		DP &= ~DP_LINK_TRAIN_MASK_CPT;
		I915_WRITE(intel_dp->output_reg, DP | DP_LINK_TRAIN_PAT_IDLE_CPT);
	} else {
		DP &= ~DP_LINK_TRAIN_MASK;
		I915_WRITE(intel_dp->output_reg, DP | DP_LINK_TRAIN_PAT_IDLE);
	}
	POSTING_READ(intel_dp->output_reg);

	/* We don't really know why we're doing this */
	intel_wait_for_vblank(dev, intel_crtc->pipe);

	if (HAS_PCH_IBX(dev) &&
	    I915_READ(intel_dp->output_reg) & DP_PIPEB_SELECT) {
		struct drm_crtc *crtc = intel_dig_port->base.base.crtc;

		/* Hardware workaround: leaving our transcoder select
		 * set to transcoder B while it's off will prevent the
		 * corresponding HDMI output on transcoder A.
		 *
		 * Combine this with another hardware workaround:
		 * transcoder select bit can only be cleared while the
		 * port is enabled.
		 */
		DP &= ~DP_PIPEB_SELECT;
		I915_WRITE(intel_dp->output_reg, DP);

		/* Changes to enable or select take place the vblank
		 * after being written.
		 */
		if (WARN_ON(crtc == NULL)) {
			/* We should never try to disable a port without a crtc
			 * attached. For paranoia keep the code around for a
			 * bit. */
			POSTING_READ(intel_dp->output_reg);
			msleep(50);
		} else
			intel_wait_for_vblank(dev, intel_crtc->pipe);
	}

	DP &= ~DP_AUDIO_OUTPUT_ENABLE;
	I915_WRITE(intel_dp->output_reg, DP & ~DP_PORT_EN);
	POSTING_READ(intel_dp->output_reg);
	msleep(intel_dp->panel_power_down_delay);
}

static bool
intel_dp_get_dpcd(struct intel_dp *intel_dp)
{
	char dpcd_hex_dump[sizeof(intel_dp->dpcd) * 3];

	if (intel_dp_aux_native_read_retry(intel_dp, 0x000, intel_dp->dpcd,
					   sizeof(intel_dp->dpcd)) == 0)
		return false; /* aux transfer failed */

	hex_dump_to_buffer(intel_dp->dpcd, sizeof(intel_dp->dpcd),
			   32, 1, dpcd_hex_dump, sizeof(dpcd_hex_dump), false);
	DRM_DEBUG_KMS("DPCD: %s\n", dpcd_hex_dump);

	if (intel_dp->dpcd[DP_DPCD_REV] == 0)
		return false; /* DPCD not present */

	/* Check if the panel supports PSR */
	memset(intel_dp->psr_dpcd, 0, sizeof(intel_dp->psr_dpcd));
	if (is_edp(intel_dp)) {
		intel_dp_aux_native_read_retry(intel_dp, DP_PSR_SUPPORT,
					       intel_dp->psr_dpcd,
					       sizeof(intel_dp->psr_dpcd));
		if (is_edp_psr(intel_dp))
			DRM_DEBUG_KMS("Detected EDP PSR Panel.\n");
	}

	if (!(intel_dp->dpcd[DP_DOWNSTREAMPORT_PRESENT] &
	      DP_DWN_STRM_PORT_PRESENT))
		return true; /* native DP sink */

	if (intel_dp->dpcd[DP_DPCD_REV] == 0x10)
		return true; /* no per-port downstream info */

	if (intel_dp_aux_native_read_retry(intel_dp, DP_DOWNSTREAM_PORT_0,
					   intel_dp->downstream_ports,
					   DP_MAX_DOWNSTREAM_PORTS) == 0)
		return false; /* downstream port status fetch failed */

	return true;
}

static void
intel_dp_probe_oui(struct intel_dp *intel_dp)
{
	u8 buf[3];

	if (!(intel_dp->dpcd[DP_DOWN_STREAM_PORT_COUNT] & DP_OUI_SUPPORT))
		return;

	ironlake_edp_panel_vdd_on(intel_dp);

	if (intel_dp_aux_native_read_retry(intel_dp, DP_SINK_OUI, buf, 3))
		DRM_DEBUG_KMS("Sink OUI: %02hx%02hx%02hx\n",
			      buf[0], buf[1], buf[2]);

	if (intel_dp_aux_native_read_retry(intel_dp, DP_BRANCH_OUI, buf, 3))
		DRM_DEBUG_KMS("Branch OUI: %02hx%02hx%02hx\n",
			      buf[0], buf[1], buf[2]);

	ironlake_edp_panel_vdd_off(intel_dp, false);
}

static bool
intel_dp_get_sink_irq(struct intel_dp *intel_dp, u8 *sink_irq_vector)
{
	int ret;

	ret = intel_dp_aux_native_read_retry(intel_dp,
					     DP_DEVICE_SERVICE_IRQ_VECTOR,
					     sink_irq_vector, 1);
	if (!ret)
		return false;

	return true;
}

static void
intel_dp_handle_test_request(struct intel_dp *intel_dp)
{
	/* NAK by default */
	intel_dp_aux_native_write_1(intel_dp, DP_TEST_RESPONSE, DP_TEST_NAK);
}

/*
 * According to DP spec
 * 5.1.2:
 *  1. Read DPCD
 *  2. Configure link according to Receiver Capabilities
 *  3. Use Link Training from 2.5.3.3 and 3.5.1.3
 *  4. Check link status on receipt of hot-plug interrupt
 */

void
intel_dp_check_link_status(struct intel_dp *intel_dp)
{
	struct intel_encoder *intel_encoder = &dp_to_dig_port(intel_dp)->base;
	u8 sink_irq_vector;
	u8 link_status[DP_LINK_STATUS_SIZE];

	if (!intel_encoder->connectors_active)
		return;

	if (WARN_ON(!intel_encoder->base.crtc))
		return;

	/* Try to read receiver status if the link appears to be up */
	if (!intel_dp_get_link_status(intel_dp, link_status)) {
		intel_dp_link_down(intel_dp);
		return;
	}

	/* Now read the DPCD to see if it's actually running */
	if (!intel_dp_get_dpcd(intel_dp)) {
		intel_dp_link_down(intel_dp);
		return;
	}

	/* Try to read the source of the interrupt */
	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11 &&
	    intel_dp_get_sink_irq(intel_dp, &sink_irq_vector)) {
		/* Clear interrupt source */
		intel_dp_aux_native_write_1(intel_dp,
					    DP_DEVICE_SERVICE_IRQ_VECTOR,
					    sink_irq_vector);

		if (sink_irq_vector & DP_AUTOMATED_TEST_REQUEST)
			intel_dp_handle_test_request(intel_dp);
		if (sink_irq_vector & (DP_CP_IRQ | DP_SINK_SPECIFIC_IRQ))
			DRM_DEBUG_DRIVER("CP or sink specific irq unhandled\n");
	}

	if (!drm_dp_channel_eq_ok(link_status, intel_dp->lane_count)) {
		DRM_DEBUG_KMS("%s: channel EQ not ok, retraining\n",
			      drm_get_encoder_name(&intel_encoder->base));
		intel_dp_start_link_train(intel_dp);
		intel_dp_complete_link_train(intel_dp);
		intel_dp_stop_link_train(intel_dp);
	}
}

/* XXX this is probably wrong for multiple downstream ports */
static enum drm_connector_status
intel_dp_detect_dpcd(struct intel_dp *intel_dp)
{
	uint8_t *dpcd = intel_dp->dpcd;
	uint8_t type;

	if (!intel_dp_get_dpcd(intel_dp))
		return connector_status_disconnected;

	/* if there's no downstream port, we're done */
	if (!(dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DWN_STRM_PORT_PRESENT))
		return connector_status_connected;

	/* If we're HPD-aware, SINK_COUNT changes dynamically */
	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11 &&
	    intel_dp->downstream_ports[0] & DP_DS_PORT_HPD) {
		uint8_t reg;
		if (!intel_dp_aux_native_read_retry(intel_dp, DP_SINK_COUNT,
						    &reg, 1))
			return connector_status_unknown;
		return DP_GET_SINK_COUNT(reg) ? connector_status_connected
					      : connector_status_disconnected;
	}

	/* If no HPD, poke DDC gently */
	if (drm_probe_ddc(&intel_dp->adapter))
		return connector_status_connected;

	/* Well we tried, say unknown for unreliable port types */
	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11) {
		type = intel_dp->downstream_ports[0] & DP_DS_PORT_TYPE_MASK;
		if (type == DP_DS_PORT_TYPE_VGA ||
		    type == DP_DS_PORT_TYPE_NON_EDID)
			return connector_status_unknown;
	} else {
		type = intel_dp->dpcd[DP_DOWNSTREAMPORT_PRESENT] &
			DP_DWN_STRM_PORT_TYPE_MASK;
		if (type == DP_DWN_STRM_PORT_TYPE_ANALOG ||
		    type == DP_DWN_STRM_PORT_TYPE_OTHER)
			return connector_status_unknown;
	}

	/* Anything else is out of spec, warn and ignore */
	DRM_DEBUG_KMS("Broken DP branch device, ignoring\n");
	return connector_status_disconnected;
}

static enum drm_connector_status
ironlake_dp_detect(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	enum drm_connector_status status;

	/* Can't disconnect eDP, but you can close the lid... */
	if (is_edp(intel_dp)) {
		status = intel_panel_detect(dev);
		if (status == connector_status_unknown)
			status = connector_status_connected;
		return status;
	}

	if (!ibx_digital_port_connected(dev_priv, intel_dig_port))
		return connector_status_disconnected;

	return intel_dp_detect_dpcd(intel_dp);
}

static enum drm_connector_status
g4x_dp_detect(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	uint32_t bit;

	/* Can't disconnect eDP, but you can close the lid... */
	if (is_edp(intel_dp)) {
		enum drm_connector_status status;

		status = intel_panel_detect(dev);
		if (status == connector_status_unknown)
			status = connector_status_connected;
		return status;
	}

	if (IS_VALLEYVIEW(dev)) {
		switch (intel_dig_port->port) {
		case PORT_B:
			bit = PORTB_HOTPLUG_LIVE_STATUS_VLV;
			break;
		case PORT_C:
			bit = PORTC_HOTPLUG_LIVE_STATUS_VLV;
			break;
		case PORT_D:
			bit = PORTD_HOTPLUG_LIVE_STATUS_VLV;
			break;
		default:
			return connector_status_unknown;
		}
	} else {
		switch (intel_dig_port->port) {
		case PORT_B:
			bit = PORTB_HOTPLUG_LIVE_STATUS_G4X;
			break;
		case PORT_C:
			bit = PORTC_HOTPLUG_LIVE_STATUS_G4X;
			break;
		case PORT_D:
			bit = PORTD_HOTPLUG_LIVE_STATUS_G4X;
			break;
		default:
			return connector_status_unknown;
		}
	}

	if ((I915_READ(PORT_HOTPLUG_STAT) & bit) == 0)
		return connector_status_disconnected;

	return intel_dp_detect_dpcd(intel_dp);
}

static struct edid *
intel_dp_get_edid(struct drm_connector *connector, struct i2c_adapter *adapter)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	/* use cached edid if we have one */
	if (intel_connector->edid) {
		struct edid *edid;
		int size;

		/* invalid edid */
		if (IS_ERR(intel_connector->edid))
			return NULL;

		size = (intel_connector->edid->extensions + 1) * EDID_LENGTH;
		edid = kmemdup(intel_connector->edid, size, GFP_KERNEL);
		if (!edid)
			return NULL;

		return edid;
	}

	return drm_get_edid(connector, adapter);
}

static int
intel_dp_get_edid_modes(struct drm_connector *connector, struct i2c_adapter *adapter)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	/* use cached edid if we have one */
	if (intel_connector->edid) {
		/* invalid edid */
		if (IS_ERR(intel_connector->edid))
			return 0;

		return intel_connector_update_modes(connector,
						    intel_connector->edid);
	}

	return intel_ddc_get_modes(connector, adapter);
}

static enum drm_connector_status
intel_dp_detect(struct drm_connector *connector, bool force)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	struct drm_device *dev = connector->dev;
	enum drm_connector_status status;
	struct edid *edid = NULL;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, drm_get_connector_name(connector));

	intel_dp->has_audio = false;

	if (HAS_PCH_SPLIT(dev))
		status = ironlake_dp_detect(intel_dp);
	else
		status = g4x_dp_detect(intel_dp);

	if (status != connector_status_connected)
		return status;

	intel_dp_probe_oui(intel_dp);

	if (intel_dp->force_audio != HDMI_AUDIO_AUTO) {
		intel_dp->has_audio = (intel_dp->force_audio == HDMI_AUDIO_ON);
	} else {
		edid = intel_dp_get_edid(connector, &intel_dp->adapter);
		if (edid) {
			intel_dp->has_audio = drm_detect_monitor_audio(edid);
			kfree(edid);
		}
	}

	if (intel_encoder->type != INTEL_OUTPUT_EDP)
		intel_encoder->type = INTEL_OUTPUT_DISPLAYPORT;
	return connector_status_connected;
}

static int intel_dp_get_modes(struct drm_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_device *dev = connector->dev;
	int ret;

	/* We should parse the EDID data and find out if it has an audio sink
	 */

	ret = intel_dp_get_edid_modes(connector, &intel_dp->adapter);
	if (ret)
		return ret;

	/* if eDP has no EDID, fall back to fixed mode */
	if (is_edp(intel_dp) && intel_connector->panel.fixed_mode) {
		struct drm_display_mode *mode;
		mode = drm_mode_duplicate(dev,
					  intel_connector->panel.fixed_mode);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			return 1;
		}
	}
	return 0;
}

static bool
intel_dp_detect_audio(struct drm_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct edid *edid;
	bool has_audio = false;

	edid = intel_dp_get_edid(connector, &intel_dp->adapter);
	if (edid) {
		has_audio = drm_detect_monitor_audio(edid);
		kfree(edid);
	}

	return has_audio;
}

static int
intel_dp_set_property(struct drm_connector *connector,
		      struct drm_property *property,
		      uint64_t val)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_encoder *intel_encoder = intel_attached_encoder(connector);
	struct intel_dp *intel_dp = enc_to_intel_dp(&intel_encoder->base);
	int ret;

	ret = drm_object_property_set_value(&connector->base, property, val);
	if (ret)
		return ret;

	if (property == dev_priv->force_audio_property) {
		int i = val;
		bool has_audio;

		if (i == intel_dp->force_audio)
			return 0;

		intel_dp->force_audio = i;

		if (i == HDMI_AUDIO_AUTO)
			has_audio = intel_dp_detect_audio(connector);
		else
			has_audio = (i == HDMI_AUDIO_ON);

		if (has_audio == intel_dp->has_audio)
			return 0;

		intel_dp->has_audio = has_audio;
		goto done;
	}

	if (property == dev_priv->broadcast_rgb_property) {
		bool old_auto = intel_dp->color_range_auto;
		uint32_t old_range = intel_dp->color_range;

		switch (val) {
		case INTEL_BROADCAST_RGB_AUTO:
			intel_dp->color_range_auto = true;
			break;
		case INTEL_BROADCAST_RGB_FULL:
			intel_dp->color_range_auto = false;
			intel_dp->color_range = 0;
			break;
		case INTEL_BROADCAST_RGB_LIMITED:
			intel_dp->color_range_auto = false;
			intel_dp->color_range = DP_COLOR_RANGE_16_235;
			break;
		default:
			return -EINVAL;
		}

		if (old_auto == intel_dp->color_range_auto &&
		    old_range == intel_dp->color_range)
			return 0;

		goto done;
	}

	if (is_edp(intel_dp) &&
	    property == connector->dev->mode_config.scaling_mode_property) {
		if (val == DRM_MODE_SCALE_NONE) {
			DRM_DEBUG_KMS("no scaling not supported\n");
			return -EINVAL;
		}

		if (intel_connector->panel.fitting_mode == val) {
			/* the eDP scaling property is not changed */
			return 0;
		}
		intel_connector->panel.fitting_mode = val;

		goto done;
	}

	return -EINVAL;

done:
	if (intel_encoder->base.crtc)
		intel_crtc_restore_mode(intel_encoder->base.crtc);

	return 0;
}

static void
intel_dp_connector_destroy(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	if (!IS_ERR_OR_NULL(intel_connector->edid))
		kfree(intel_connector->edid);

	/* Can't call is_edp() since the encoder may have been destroyed
	 * already. */
	if (connector->connector_type == DRM_MODE_CONNECTOR_eDP)
		intel_panel_fini(&intel_connector->panel);

	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

void intel_dp_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(encoder);
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	struct drm_device *dev = intel_dp_to_dev(intel_dp);

	i2c_del_adapter(&intel_dp->adapter);
	drm_encoder_cleanup(encoder);
	if (is_edp(intel_dp)) {
		cancel_delayed_work_sync(&intel_dp->panel_vdd_work);
		mutex_lock(&dev->mode_config.mutex);
		ironlake_panel_vdd_off_sync(intel_dp);
		mutex_unlock(&dev->mode_config.mutex);
	}
	kfree(intel_dig_port);
}

static const struct drm_connector_funcs intel_dp_connector_funcs = {
	.dpms = intel_connector_dpms,
	.detect = intel_dp_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = intel_dp_set_property,
	.destroy = intel_dp_connector_destroy,
};

static const struct drm_connector_helper_funcs intel_dp_connector_helper_funcs = {
	.get_modes = intel_dp_get_modes,
	.mode_valid = intel_dp_mode_valid,
	.best_encoder = intel_best_encoder,
};

static const struct drm_encoder_funcs intel_dp_enc_funcs = {
	.destroy = intel_dp_encoder_destroy,
};

static void
intel_dp_hot_plug(struct intel_encoder *intel_encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&intel_encoder->base);

	intel_dp_check_link_status(intel_dp);
}

/* Return which DP Port should be selected for Transcoder DP control */
int
intel_trans_dp_port_sel(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct intel_encoder *intel_encoder;
	struct intel_dp *intel_dp;

	for_each_encoder_on_crtc(dev, crtc, intel_encoder) {
		intel_dp = enc_to_intel_dp(&intel_encoder->base);

		if (intel_encoder->type == INTEL_OUTPUT_DISPLAYPORT ||
		    intel_encoder->type == INTEL_OUTPUT_EDP)
			return intel_dp->output_reg;
	}

	return -1;
}

/* check the VBT to see whether the eDP is on DP-D port */
bool intel_dpd_is_edp(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	union child_device_config *p_child;
	int i;

	if (!dev_priv->vbt.child_dev_num)
		return false;

	for (i = 0; i < dev_priv->vbt.child_dev_num; i++) {
		p_child = dev_priv->vbt.child_dev + i;

		if (p_child->common.dvo_port == PORT_IDPD &&
		    p_child->common.device_type == DEVICE_TYPE_eDP)
			return true;
	}
	return false;
}

static void
intel_dp_add_properties(struct intel_dp *intel_dp, struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	intel_attach_force_audio_property(connector);
	intel_attach_broadcast_rgb_property(connector);
	intel_dp->color_range_auto = true;

	if (is_edp(intel_dp)) {
		drm_mode_create_scaling_mode_property(connector->dev);
		drm_object_attach_property(
			&connector->base,
			connector->dev->mode_config.scaling_mode_property,
			DRM_MODE_SCALE_ASPECT);
		intel_connector->panel.fitting_mode = DRM_MODE_SCALE_ASPECT;
	}
}

static void
intel_dp_init_panel_power_sequencer(struct drm_device *dev,
				    struct intel_dp *intel_dp,
				    struct edp_power_seq *out)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct edp_power_seq cur, vbt, spec, final;
	u32 pp_on, pp_off, pp_div, pp;
	int pp_ctrl_reg, pp_on_reg, pp_off_reg, pp_div_reg;

	if (HAS_PCH_SPLIT(dev)) {
		pp_ctrl_reg = PCH_PP_CONTROL;
		pp_on_reg = PCH_PP_ON_DELAYS;
		pp_off_reg = PCH_PP_OFF_DELAYS;
		pp_div_reg = PCH_PP_DIVISOR;
	} else {
		enum pipe pipe = vlv_power_sequencer_pipe(intel_dp);

		pp_ctrl_reg = VLV_PIPE_PP_CONTROL(pipe);
		pp_on_reg = VLV_PIPE_PP_ON_DELAYS(pipe);
		pp_off_reg = VLV_PIPE_PP_OFF_DELAYS(pipe);
		pp_div_reg = VLV_PIPE_PP_DIVISOR(pipe);
	}

	/* Workaround: Need to write PP_CONTROL with the unlock key as
	 * the very first thing. */
	pp = ironlake_get_pp_control(intel_dp);
	I915_WRITE(pp_ctrl_reg, pp);

	pp_on = I915_READ(pp_on_reg);
	pp_off = I915_READ(pp_off_reg);
	pp_div = I915_READ(pp_div_reg);

	/* Pull timing values out of registers */
	cur.t1_t3 = (pp_on & PANEL_POWER_UP_DELAY_MASK) >>
		PANEL_POWER_UP_DELAY_SHIFT;

	cur.t8 = (pp_on & PANEL_LIGHT_ON_DELAY_MASK) >>
		PANEL_LIGHT_ON_DELAY_SHIFT;

	cur.t9 = (pp_off & PANEL_LIGHT_OFF_DELAY_MASK) >>
		PANEL_LIGHT_OFF_DELAY_SHIFT;

	cur.t10 = (pp_off & PANEL_POWER_DOWN_DELAY_MASK) >>
		PANEL_POWER_DOWN_DELAY_SHIFT;

	cur.t11_t12 = ((pp_div & PANEL_POWER_CYCLE_DELAY_MASK) >>
		       PANEL_POWER_CYCLE_DELAY_SHIFT) * 1000;

	DRM_DEBUG_KMS("cur t1_t3 %d t8 %d t9 %d t10 %d t11_t12 %d\n",
		      cur.t1_t3, cur.t8, cur.t9, cur.t10, cur.t11_t12);

	vbt = dev_priv->vbt.edp_pps;

	/* Upper limits from eDP 1.3 spec. Note that we use the clunky units of
	 * our hw here, which are all in 100usec. */
	spec.t1_t3 = 210 * 10;
	spec.t8 = 50 * 10; /* no limit for t8, use t7 instead */
	spec.t9 = 50 * 10; /* no limit for t9, make it symmetric with t8 */
	spec.t10 = 500 * 10;
	/* This one is special and actually in units of 100ms, but zero
	 * based in the hw (so we need to add 100 ms). But the sw vbt
	 * table multiplies it with 1000 to make it in units of 100usec,
	 * too. */
	spec.t11_t12 = (510 + 100) * 10;

	DRM_DEBUG_KMS("vbt t1_t3 %d t8 %d t9 %d t10 %d t11_t12 %d\n",
		      vbt.t1_t3, vbt.t8, vbt.t9, vbt.t10, vbt.t11_t12);

	/* Use the max of the register settings and vbt. If both are
	 * unset, fall back to the spec limits. */
#define assign_final(field)	final.field = (max(cur.field, vbt.field) == 0 ? \
				       spec.field : \
				       max(cur.field, vbt.field))
	assign_final(t1_t3);
	assign_final(t8);
	assign_final(t9);
	assign_final(t10);
	assign_final(t11_t12);
#undef assign_final

#define get_delay(field)	(DIV_ROUND_UP(final.field, 10))
	intel_dp->panel_power_up_delay = get_delay(t1_t3);
	intel_dp->backlight_on_delay = get_delay(t8);
	intel_dp->backlight_off_delay = get_delay(t9);
	intel_dp->panel_power_down_delay = get_delay(t10);
	intel_dp->panel_power_cycle_delay = get_delay(t11_t12);
#undef get_delay

	DRM_DEBUG_KMS("panel power up delay %d, power down delay %d, power cycle delay %d\n",
		      intel_dp->panel_power_up_delay, intel_dp->panel_power_down_delay,
		      intel_dp->panel_power_cycle_delay);

	DRM_DEBUG_KMS("backlight on delay %d, off delay %d\n",
		      intel_dp->backlight_on_delay, intel_dp->backlight_off_delay);

	if (out)
		*out = final;
}

static void
intel_dp_init_panel_power_sequencer_registers(struct drm_device *dev,
					      struct intel_dp *intel_dp,
					      struct edp_power_seq *seq)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp_on, pp_off, pp_div, port_sel = 0;
	int div = HAS_PCH_SPLIT(dev) ? intel_pch_rawclk(dev) : intel_hrawclk(dev);
	int pp_on_reg, pp_off_reg, pp_div_reg;

	if (HAS_PCH_SPLIT(dev)) {
		pp_on_reg = PCH_PP_ON_DELAYS;
		pp_off_reg = PCH_PP_OFF_DELAYS;
		pp_div_reg = PCH_PP_DIVISOR;
	} else {
		enum pipe pipe = vlv_power_sequencer_pipe(intel_dp);

		pp_on_reg = VLV_PIPE_PP_ON_DELAYS(pipe);
		pp_off_reg = VLV_PIPE_PP_OFF_DELAYS(pipe);
		pp_div_reg = VLV_PIPE_PP_DIVISOR(pipe);
	}

	/* And finally store the new values in the power sequencer. */
	pp_on = (seq->t1_t3 << PANEL_POWER_UP_DELAY_SHIFT) |
		(seq->t8 << PANEL_LIGHT_ON_DELAY_SHIFT);
	pp_off = (seq->t9 << PANEL_LIGHT_OFF_DELAY_SHIFT) |
		 (seq->t10 << PANEL_POWER_DOWN_DELAY_SHIFT);
	/* Compute the divisor for the pp clock, simply match the Bspec
	 * formula. */
	pp_div = ((100 * div)/2 - 1) << PP_REFERENCE_DIVIDER_SHIFT;
	pp_div |= (DIV_ROUND_UP(seq->t11_t12, 1000)
			<< PANEL_POWER_CYCLE_DELAY_SHIFT);

	/* Haswell doesn't have any port selection bits for the panel
	 * power sequencer any more. */
	if (IS_VALLEYVIEW(dev)) {
		if (dp_to_dig_port(intel_dp)->port == PORT_B)
			port_sel = PANEL_PORT_SELECT_DPB_VLV;
		else
			port_sel = PANEL_PORT_SELECT_DPC_VLV;
	} else if (HAS_PCH_IBX(dev) || HAS_PCH_CPT(dev)) {
		if (dp_to_dig_port(intel_dp)->port == PORT_A)
			port_sel = PANEL_PORT_SELECT_DPA;
		else
			port_sel = PANEL_PORT_SELECT_DPD;
	}

	pp_on |= port_sel;

	I915_WRITE(pp_on_reg, pp_on);
	I915_WRITE(pp_off_reg, pp_off);
	I915_WRITE(pp_div_reg, pp_div);

	DRM_DEBUG_KMS("panel power sequencer register settings: PP_ON %#x, PP_OFF %#x, PP_DIV %#x\n",
		      I915_READ(pp_on_reg),
		      I915_READ(pp_off_reg),
		      I915_READ(pp_div_reg));
}

static bool intel_edp_init_connector(struct intel_dp *intel_dp,
				     struct intel_connector *intel_connector)
{
	struct drm_connector *connector = &intel_connector->base;
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_display_mode *fixed_mode = NULL;
	struct edp_power_seq power_seq = { 0 };
	bool has_dpcd;
	struct drm_display_mode *scan;
	struct edid *edid;

	if (!is_edp(intel_dp))
		return true;

	intel_dp_init_panel_power_sequencer(dev, intel_dp, &power_seq);

	/* Cache DPCD and EDID for edp. */
	ironlake_edp_panel_vdd_on(intel_dp);
	has_dpcd = intel_dp_get_dpcd(intel_dp);
	ironlake_edp_panel_vdd_off(intel_dp, false);

	if (has_dpcd) {
		if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11)
			dev_priv->no_aux_handshake =
				intel_dp->dpcd[DP_MAX_DOWNSPREAD] &
				DP_NO_AUX_HANDSHAKE_LINK_TRAINING;
	} else {
		/* if this fails, presume the device is a ghost */
		DRM_INFO("failed to retrieve link info, disabling eDP\n");
		return false;
	}

	/* We now know it's not a ghost, init power sequence regs. */
	intel_dp_init_panel_power_sequencer_registers(dev, intel_dp,
						      &power_seq);

	ironlake_edp_panel_vdd_on(intel_dp);
	edid = drm_get_edid(connector, &intel_dp->adapter);
	if (edid) {
		if (drm_add_edid_modes(connector, edid)) {
			drm_mode_connector_update_edid_property(connector,
								edid);
			drm_edid_to_eld(connector, edid);
		} else {
			kfree(edid);
			edid = ERR_PTR(-EINVAL);
		}
	} else {
		edid = ERR_PTR(-ENOENT);
	}
	intel_connector->edid = edid;

	/* prefer fixed mode from EDID if available */
	list_for_each_entry(scan, &connector->probed_modes, head) {
		if ((scan->type & DRM_MODE_TYPE_PREFERRED)) {
			fixed_mode = drm_mode_duplicate(dev, scan);
			break;
		}
	}

	/* fallback to VBT if available for eDP */
	if (!fixed_mode && dev_priv->vbt.lfp_lvds_vbt_mode) {
		fixed_mode = drm_mode_duplicate(dev,
					dev_priv->vbt.lfp_lvds_vbt_mode);
		if (fixed_mode)
			fixed_mode->type |= DRM_MODE_TYPE_PREFERRED;
	}

	ironlake_edp_panel_vdd_off(intel_dp, false);

	intel_panel_init(&intel_connector->panel, fixed_mode);
	intel_panel_setup_backlight(connector);

	return true;
}

bool
intel_dp_init_connector(struct intel_digital_port *intel_dig_port,
			struct intel_connector *intel_connector)
{
	struct drm_connector *connector = &intel_connector->base;
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	struct drm_device *dev = intel_encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_dig_port->port;
	const char *name = NULL;
	int type, error;

	/* Preserve the current hw state. */
	intel_dp->DP = I915_READ(intel_dp->output_reg);
	intel_dp->attached_connector = intel_connector;

	type = DRM_MODE_CONNECTOR_DisplayPort;
	/*
	 * FIXME : We need to initialize built-in panels before external panels.
	 * For X0, DP_C is fixed as eDP. Revisit this as part of VLV eDP cleanup
	 */
	switch (port) {
	case PORT_A:
		type = DRM_MODE_CONNECTOR_eDP;
		break;
	case PORT_C:
		if (IS_VALLEYVIEW(dev))
			type = DRM_MODE_CONNECTOR_eDP;
		break;
	case PORT_D:
		if (HAS_PCH_SPLIT(dev) && intel_dpd_is_edp(dev))
			type = DRM_MODE_CONNECTOR_eDP;
		break;
	default:	/* silence GCC warning */
		break;
	}

	/*
	 * For eDP we always set the encoder type to INTEL_OUTPUT_EDP, but
	 * for DP the encoder type can be set by the caller to
	 * INTEL_OUTPUT_UNKNOWN for DDI, so don't rewrite it.
	 */
	if (type == DRM_MODE_CONNECTOR_eDP)
		intel_encoder->type = INTEL_OUTPUT_EDP;

	DRM_DEBUG_KMS("Adding %s connector on port %c\n",
			type == DRM_MODE_CONNECTOR_eDP ? "eDP" : "DP",
			port_name(port));

	drm_connector_init(dev, connector, &intel_dp_connector_funcs, type);
	drm_connector_helper_add(connector, &intel_dp_connector_helper_funcs);

	connector->interlace_allowed = true;
	connector->doublescan_allowed = 0;

	INIT_DELAYED_WORK(&intel_dp->panel_vdd_work,
			  ironlake_panel_vdd_work);

	intel_connector_attach_encoder(intel_connector, intel_encoder);
	drm_sysfs_connector_add(connector);

	if (HAS_DDI(dev))
		intel_connector->get_hw_state = intel_ddi_connector_get_hw_state;
	else
		intel_connector->get_hw_state = intel_connector_get_hw_state;

	intel_dp->aux_ch_ctl_reg = intel_dp->output_reg + 0x10;
	if (HAS_DDI(dev)) {
		switch (intel_dig_port->port) {
		case PORT_A:
			intel_dp->aux_ch_ctl_reg = DPA_AUX_CH_CTL;
			break;
		case PORT_B:
			intel_dp->aux_ch_ctl_reg = PCH_DPB_AUX_CH_CTL;
			break;
		case PORT_C:
			intel_dp->aux_ch_ctl_reg = PCH_DPC_AUX_CH_CTL;
			break;
		case PORT_D:
			intel_dp->aux_ch_ctl_reg = PCH_DPD_AUX_CH_CTL;
			break;
		default:
			BUG();
		}
	}

	/* Set up the DDC bus. */
	switch (port) {
	case PORT_A:
		intel_encoder->hpd_pin = HPD_PORT_A;
		name = "DPDDC-A";
		break;
	case PORT_B:
		intel_encoder->hpd_pin = HPD_PORT_B;
		name = "DPDDC-B";
		break;
	case PORT_C:
		intel_encoder->hpd_pin = HPD_PORT_C;
		name = "DPDDC-C";
		break;
	case PORT_D:
		intel_encoder->hpd_pin = HPD_PORT_D;
		name = "DPDDC-D";
		break;
	default:
		BUG();
	}

	error = intel_dp_i2c_init(intel_dp, intel_connector, name);
	WARN(error, "intel_dp_i2c_init failed with error %d for port %c\n",
	     error, port_name(port));

	intel_dp->psr_setup_done = false;

	if (!intel_edp_init_connector(intel_dp, intel_connector)) {
		i2c_del_adapter(&intel_dp->adapter);
		if (is_edp(intel_dp)) {
			cancel_delayed_work_sync(&intel_dp->panel_vdd_work);
			mutex_lock(&dev->mode_config.mutex);
			ironlake_panel_vdd_off_sync(intel_dp);
			mutex_unlock(&dev->mode_config.mutex);
		}
		drm_sysfs_connector_remove(connector);
		drm_connector_cleanup(connector);
		return false;
	}

	intel_dp_add_properties(intel_dp, connector);

	/* For G4X desktop chip, PEG_BAND_GAP_DATA 3:0 must first be written
	 * 0xd.  Failure to do so will result in spurious interrupts being
	 * generated on the port when a cable is not attached.
	 */
	if (IS_G4X(dev) && !IS_GM45(dev)) {
		u32 temp = I915_READ(PEG_BAND_GAP_DATA);
		I915_WRITE(PEG_BAND_GAP_DATA, (temp & ~0xf) | 0xd);
	}

	return true;
}

void
intel_dp_init(struct drm_device *dev, int output_reg, enum port port)
{
	struct intel_digital_port *intel_dig_port;
	struct intel_encoder *intel_encoder;
	struct drm_encoder *encoder;
	struct intel_connector *intel_connector;

	intel_dig_port = kzalloc(sizeof(*intel_dig_port), GFP_KERNEL);
	if (!intel_dig_port)
		return;

	intel_connector = kzalloc(sizeof(*intel_connector), GFP_KERNEL);
	if (!intel_connector) {
		kfree(intel_dig_port);
		return;
	}

	intel_encoder = &intel_dig_port->base;
	encoder = &intel_encoder->base;

	drm_encoder_init(dev, &intel_encoder->base, &intel_dp_enc_funcs,
			 DRM_MODE_ENCODER_TMDS);

	intel_encoder->compute_config = intel_dp_compute_config;
	intel_encoder->mode_set = intel_dp_mode_set;
	intel_encoder->disable = intel_disable_dp;
	intel_encoder->post_disable = intel_post_disable_dp;
	intel_encoder->get_hw_state = intel_dp_get_hw_state;
	intel_encoder->get_config = intel_dp_get_config;
	if (IS_VALLEYVIEW(dev)) {
		intel_encoder->pre_pll_enable = vlv_dp_pre_pll_enable;
		intel_encoder->pre_enable = vlv_pre_enable_dp;
		intel_encoder->enable = vlv_enable_dp;
	} else {
		intel_encoder->pre_enable = g4x_pre_enable_dp;
		intel_encoder->enable = g4x_enable_dp;
	}

	intel_dig_port->port = port;
	intel_dig_port->dp.output_reg = output_reg;

	intel_encoder->type = INTEL_OUTPUT_DISPLAYPORT;
	intel_encoder->crtc_mask = (1 << 0) | (1 << 1) | (1 << 2);
	intel_encoder->cloneable = false;
	intel_encoder->hot_plug = intel_dp_hot_plug;

	if (!intel_dp_init_connector(intel_dig_port, intel_connector)) {
		drm_encoder_cleanup(encoder);
		kfree(intel_dig_port);
		kfree(intel_connector);
	}
}
